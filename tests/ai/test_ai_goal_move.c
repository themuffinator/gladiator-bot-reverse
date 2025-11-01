#include <stdarg.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <cmocka.h>

#include <math.h>

#ifdef _WIN32
#include <direct.h>
#define chdir _chdir
#define getcwd _getcwd
#define unlink _unlink
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#include "botlib/ai/goal_move_orchestrator.h"
#include "botlib/common/l_libvar.h"
#include "botlib/common/l_memory.h"
#include "botlib/interface/bot_state.h"
#include "botlib/interface/botlib_interface.h"
#include "q2bridge/botlib.h"
#include "../support/asset_env.h"

#ifndef PROJECT_SOURCE_DIR
#error "PROJECT_SOURCE_DIR must be defined so regression tests can resolve asset paths."
#endif

#define TEST_BOTLIB_HEAP_SIZE (1u << 20)
#define TEST_MAX_LOG_MESSAGES 64

typedef struct test_log_message_s {
    int priority;
    char text[256];
} test_log_message_t;

typedef struct test_environment_s {
    asset_env_t assets;
    bool libvar_initialised;
    bool memory_initialised;
    bool import_table_set;
    bool library_setup;
    bool client_active;
    bot_export_t *exports;
} test_environment_t;

static struct {
    test_log_message_t entries[TEST_MAX_LOG_MESSAGES];
    int count;
} g_test_log;

static void test_reset_log(void)
{
    g_test_log.count = 0;
    for (int i = 0; i < TEST_MAX_LOG_MESSAGES; ++i) {
        g_test_log.entries[i].priority = 0;
        g_test_log.entries[i].text[0] = '\0';
    }
}

static void test_capture_vmessage(int priority, const char *fmt, va_list args)
{
    if (g_test_log.count >= TEST_MAX_LOG_MESSAGES) {
        return;
    }

    test_log_message_t *slot = &g_test_log.entries[g_test_log.count++];
    slot->priority = priority;
    vsnprintf(slot->text, sizeof(slot->text), fmt != NULL ? fmt : "", args);
}

static void test_capture_botlib_print(int priority, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    test_capture_vmessage(priority, fmt, args);
    va_end(args);
}

static void test_capture_import_print(int priority, char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    test_capture_vmessage(priority, fmt, args);
    va_end(args);
}

static void test_capture_dprint(const char *fmt, ...)
{
    (void)fmt;
}

static int test_libvar_get(const char *var_name, char *value, size_t size)
{
    (void)var_name;
    if (value == NULL || size == 0) {
        return -1;
    }
    value[0] = '\0';
    return -1;
}

static int test_libvar_set(const char *var_name, const char *value)
{
    (void)var_name;
    (void)value;
    return 0;
}

typedef struct test_bot_input_log_s {
    int count;
    int last_client;
    bot_input_t last_command;
} test_bot_input_log_t;

static test_bot_input_log_t g_bot_input_log;

static void test_reset_bot_input_log(void)
{
    memset(&g_bot_input_log, 0, sizeof(g_bot_input_log));
}

static void test_bot_input(int client, bot_input_t *input)
{
    g_bot_input_log.last_client = client;
    if (input != NULL) {
        g_bot_input_log.last_command = *input;
    }
    g_bot_input_log.count += 1;
}

static void test_bot_client_command(int client, char *fmt, ...)
{
    (void)client;
    (void)fmt;
}

static bsp_trace_t test_trace(vec3_t start, vec3_t mins, vec3_t maxs, vec3_t end, int passent, int contentmask)
{
    (void)start;
    (void)mins;
    (void)maxs;
    (void)end;
    (void)passent;
    (void)contentmask;
    bsp_trace_t trace;
    memset(&trace, 0, sizeof(trace));
    return trace;
}

static int test_point_contents(vec3_t point)
{
    (void)point;
    return 0;
}

static void *test_get_memory(int size)
{
    if (size <= 0) {
        return NULL;
    }
    return calloc(1, (size_t)size);
}

static void test_free_memory(void *ptr)
{
    free(ptr);
}

static int test_debug_line_create(void)
{
    return 0;
}

static void test_debug_line_delete(int line)
{
    (void)line;
}

static void test_debug_line_show(int line, vec3_t start, vec3_t end, int color)
{
    (void)line;
    (void)start;
    (void)end;
    (void)color;
}

static const botlib_import_table_t g_test_imports = {
    .Print = test_capture_botlib_print,
    .DPrint = test_capture_dprint,
    .BotLibVarGet = test_libvar_get,
    .BotLibVarSet = test_libvar_set,
};

static bot_import_t g_test_bot_import = {
    .BotInput = test_bot_input,
    .BotClientCommand = test_bot_client_command,
    .Print = test_capture_import_print,
    .Trace = test_trace,
    .PointContents = test_point_contents,
    .GetMemory = test_get_memory,
    .FreeMemory = test_free_memory,
    .DebugLineCreate = test_debug_line_create,
    .DebugLineDelete = test_debug_line_delete,
    .DebugLineShow = test_debug_line_show,
};

static int goal_move_setup(void **state)
{
    test_environment_t *env = (test_environment_t *)calloc(1, sizeof(test_environment_t));
    if (env == NULL) {
        return -1;
    }

    if (!asset_env_initialise(&env->assets)) {
        asset_env_cleanup(&env->assets);
        free(env);
        cmocka_skip();
    }

    test_reset_log();
    test_reset_bot_input_log();

    BotInterface_SetImportTable(&g_test_imports);
    env->import_table_set = true;

    LibVar_Init();
    env->libvar_initialised = true;

    assert_true(BotMemory_Init(TEST_BOTLIB_HEAP_SIZE));
    env->memory_initialised = true;

    env->exports = GetBotAPI(&g_test_bot_import);
    assert_non_null(env->exports);

    int status = env->exports->BotSetupLibrary();
    assert_int_equal(status, BLERR_NOERROR);
    env->library_setup = true;

    *state = env;
    return 0;
}

static int goal_move_teardown(void **state)
{
    test_environment_t *env = (test_environment_t *)(*state);
    if (env == NULL) {
        return 0;
    }

    if (env->client_active && env->exports != NULL) {
        env->exports->BotShutdownClient(0);
        env->client_active = false;
    }

    if (env->library_setup && env->exports != NULL) {
        env->exports->BotShutdownLibrary();
        env->library_setup = false;
    }

    if (env->libvar_initialised) {
        LibVar_Shutdown();
        env->libvar_initialised = false;
    }

    if (env->memory_initialised) {
        BotMemory_Shutdown();
        env->memory_initialised = false;
    }

    if (env->import_table_set) {
        BotInterface_SetImportTable(NULL);
        env->import_table_set = false;
    }

    asset_env_cleanup(&env->assets);
    free(env);
    *state = NULL;
    return 0;
}

static void configure_standard_bot_settings(bot_settings_t *settings)
{
    memset(settings, 0, sizeof(*settings));
    snprintf(settings->characterfile, sizeof(settings->characterfile), "bots/babe_c.c");
    snprintf(settings->charactername, sizeof(settings->charactername), "arena_tester");
}

static void activate_test_client(test_environment_t *env)
{
    bot_settings_t settings;
    configure_standard_bot_settings(&settings);

    int status = env->exports->BotSetupClient(0, &settings);
    assert_int_equal(status, BLERR_NOERROR);
    env->client_active = true;
}

static void test_setup_allocates_goal_move_states(void **state)
{
    test_environment_t *env = (test_environment_t *)(*state);
    activate_test_client(env);

    bot_client_state_t *slot = BotState_Get(0);
    assert_non_null(slot);
    assert_true(slot->active);
    assert_non_null(slot->goal_state);
    assert_non_null(slot->move_state);

    ai_avoid_list_t *goal_avoid = AI_GoalState_GetAvoidList(slot->goal_state);
    ai_avoid_list_t *move_avoid = AI_MoveState_GetAvoidList(slot->move_state);
    assert_non_null(goal_avoid);
    assert_ptr_equal(goal_avoid, move_avoid);
}

typedef struct goal_move_log_s {
    int entries[8];
    int count;
} goal_move_log_t;

enum goal_move_log_marker {
    GOAL_LOG_NOTIFY = 1,
    GOAL_LOG_PATH = 2,
    GOAL_LOG_SUBMIT = 3,
};

static void goal_move_log_append(goal_move_log_t *log, int marker)
{
    if (log == NULL || log->count >= (int)(sizeof(log->entries) / sizeof(log->entries[0]))) {
        return;
    }
    log->entries[log->count++] = marker;
}

typedef struct goal_move_service_context_s {
    int start_area;
    int blocked_item;
    float travel_time;
    int path_status;
    goal_move_log_t *log;
} goal_move_service_context_t;

static float test_goal_weight(void *ctx, const ai_goal_candidate_t *candidate)
{
    (void)ctx;
    return (candidate != NULL) ? candidate->base_weight : 0.0f;
}

static float test_goal_travel(void *ctx, int start_area, const ai_goal_candidate_t *candidate)
{
    goal_move_service_context_t *context = (goal_move_service_context_t *)ctx;
    if (context == NULL || candidate == NULL) {
        return 0.0f;
    }

    if (candidate->item_index == context->blocked_item) {
        return -1.0f;
    }

    (void)start_area;
    return context->travel_time;
}

static void test_goal_notify(void *ctx, const ai_goal_selection_t *selection)
{
    goal_move_service_context_t *context = (goal_move_service_context_t *)ctx;
    (void)selection;
    goal_move_log_append(context->log, GOAL_LOG_NOTIFY);
}

static int test_goal_area(void *ctx, const vec3_t origin)
{
    goal_move_service_context_t *context = (goal_move_service_context_t *)ctx;
    (void)origin;
    return context != NULL ? context->start_area : 0;
}

static int test_move_path(void *ctx,
                          const ai_goal_selection_t *goal,
                          ai_avoid_list_t *avoid,
                          bot_input_t *out_input)
{
    goal_move_service_context_t *context = (goal_move_service_context_t *)ctx;
    goal_move_log_append(context->log, GOAL_LOG_PATH);

    if (context->path_status != BLERR_NOERROR) {
        return context->path_status;
    }

    if (out_input != NULL && goal != NULL && goal->valid) {
        VectorCopy(goal->candidate.origin, out_input->dir);
        out_input->speed = goal->score;
    }

    (void)avoid;
    return BLERR_NOERROR;
}

static void test_move_submit(void *ctx, int client, const bot_input_t *input)
{
    goal_move_service_context_t *context = (goal_move_service_context_t *)ctx;
    goal_move_log_append(context->log, GOAL_LOG_SUBMIT);
    test_bot_input(client, (bot_input_t *)input);
}

static void prepare_goal_move_services(bot_client_state_t *slot,
                                       goal_move_service_context_t *context,
                                       goal_move_log_t *log)
{
    context->start_area = 7;
    context->blocked_item = 2;
    context->travel_time = 3.0f;
    context->path_status = BLERR_NOERROR;
    context->log = log;

    ai_goal_services_t goal_services = {
        .weight_fn = test_goal_weight,
        .travel_time_fn = test_goal_travel,
        .notify_fn = test_goal_notify,
        .area_fn = test_goal_area,
        .userdata = context,
        .avoid_duration = 4.0f,
    };

    ai_move_services_t move_services = {
        .path_fn = test_move_path,
        .submit_fn = test_move_submit,
        .userdata = context,
    };

    AI_GoalState_SetServices(slot->goal_state, &goal_services);
    AI_MoveState_SetServices(slot->move_state, &move_services);
}

static void seed_goal_candidates(bot_client_state_t *slot)
{
    AI_GoalState_ClearCandidates(slot->goal_state);

    ai_goal_candidate_t primary = {
        .item_index = 1,
        .area = 10,
        .travel_flags = 0,
        .base_weight = 25.0f,
    };
    VectorSet(primary.origin, 128.0f, 0.0f, 0.0f);
    assert_true(AI_GoalState_AddCandidate(slot->goal_state, &primary));

    ai_goal_candidate_t blocked = {
        .item_index = 2,
        .area = 12,
        .travel_flags = 0,
        .base_weight = 40.0f,
    };
    VectorSet(blocked.origin, -64.0f, 32.0f, 0.0f);
    assert_true(AI_GoalState_AddCandidate(slot->goal_state, &blocked));
}

static void submit_client_update(bot_export_t *exports)
{
    bot_updateclient_t update;
    memset(&update, 0, sizeof(update));
    VectorSet(update.origin, 16.0f, 8.0f, 0.0f);
    VectorSet(update.viewangles, 5.0f, 10.0f, -2.0f);

    exports->BotStartFrame(1.0f);
    int status = exports->BotUpdateClient(0, &update);
    assert_int_equal(status, BLERR_NOERROR);
}

static void test_goal_refresh_and_movement_dispatch_order(void **state)
{
    test_environment_t *env = (test_environment_t *)(*state);
    activate_test_client(env);

    bot_client_state_t *slot = BotState_Get(0);
    assert_non_null(slot);

    goal_move_service_context_t context;
    goal_move_log_t log = {0};
    prepare_goal_move_services(slot, &context, &log);
    seed_goal_candidates(slot);

    submit_client_update(env->exports);
    test_reset_bot_input_log();

    int status = env->exports->BotAI(0, 0.1f);
    assert_int_equal(status, BLERR_NOERROR);

    assert_int_equal(log.count, 3);
    assert_int_equal(log.entries[0], GOAL_LOG_NOTIFY);
    assert_int_equal(log.entries[1], GOAL_LOG_PATH);
    assert_int_equal(log.entries[2], GOAL_LOG_SUBMIT);

    ai_avoid_list_t *avoid = AI_GoalState_GetAvoidList(slot->goal_state);
    assert_true(AI_AvoidList_Contains(avoid, context.blocked_item, 1.0f));

    assert_int_equal(g_bot_input_log.count, 1);
    assert_int_equal(g_bot_input_log.last_client, 0);
    assert_float_equal(g_bot_input_log.last_command.thinktime, 0.1f, 0.0001f);
    assert_float_equal(g_bot_input_log.last_command.speed, context.travel_time == 0.0f ? 25.0f : (25.0f - context.travel_time), 0.0001f);
}

static void test_movement_error_propagates_without_submission(void **state)
{
    test_environment_t *env = (test_environment_t *)(*state);
    activate_test_client(env);

    bot_client_state_t *slot = BotState_Get(0);
    assert_non_null(slot);

    goal_move_service_context_t context;
    goal_move_log_t log = {0};
    prepare_goal_move_services(slot, &context, &log);
    context.path_status = BLERR_INVALIDIMPORT;
    seed_goal_candidates(slot);

    submit_client_update(env->exports);
    test_reset_bot_input_log();

    int status = env->exports->BotAI(0, 0.2f);
    assert_int_equal(status, BLERR_INVALIDIMPORT);

    assert_int_equal(log.count, 2);
    assert_int_equal(log.entries[0], GOAL_LOG_NOTIFY);
    assert_int_equal(log.entries[1], GOAL_LOG_PATH);
    assert_int_equal(g_bot_input_log.count, 0);
}

static int failing_goal_area(void *ctx, const vec3_t origin)
{
    (void)ctx;
    (void)origin;
    return -1;
}

static void test_bot_update_client_propagates_area_errors(void **state)
{
    test_environment_t *env = (test_environment_t *)(*state);
    activate_test_client(env);

    bot_client_state_t *slot = BotState_Get(0);
    assert_non_null(slot);

    ai_goal_services_t goal_services = {
        .weight_fn = test_goal_weight,
        .travel_time_fn = test_goal_travel,
        .notify_fn = NULL,
        .area_fn = failing_goal_area,
        .userdata = NULL,
        .avoid_duration = 2.0f,
    };
    AI_GoalState_SetServices(slot->goal_state, &goal_services);

    bot_updateclient_t update;
    memset(&update, 0, sizeof(update));

    env->exports->BotStartFrame(2.5f);
    int status = env->exports->BotUpdateClient(0, &update);
    assert_int_equal(status, BLERR_INVALIDIMPORT);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_setup_allocates_goal_move_states,
                                        goal_move_setup,
                                        goal_move_teardown),
        cmocka_unit_test_setup_teardown(test_goal_refresh_and_movement_dispatch_order,
                                        goal_move_setup,
                                        goal_move_teardown),
        cmocka_unit_test_setup_teardown(test_movement_error_propagates_without_submission,
                                        goal_move_setup,
                                        goal_move_teardown),
        cmocka_unit_test_setup_teardown(test_bot_update_client_propagates_area_errors,
                                        goal_move_setup,
                                        goal_move_teardown),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}

