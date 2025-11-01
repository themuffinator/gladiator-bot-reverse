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

#include "botlib/aas/aas_local.h"
#include "botlib/ai/goal/ai_goal.h"
#include "botlib/ai/goal/bot_goal.h"
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

typedef struct goal_move_log_s {
    int notify_count;
    ai_goal_selection_t last_selection;
} goal_move_log_t;

typedef struct goal_move_service_context_s {
    bot_client_state_t *slot;
    goal_move_log_t *log;
    float travel_time;
    int default_area;
} goal_move_service_context_t;

static float goal_service_weight(void *ctx, const ai_goal_candidate_t *candidate)
{
    (void)ctx;
    if (candidate == NULL)
    {
        return 0.0f;
    }
    return candidate->base_weight;
}

static float goal_service_travel(void *ctx, int start_area, const ai_goal_candidate_t *candidate)
{
    goal_move_service_context_t *context = (goal_move_service_context_t *)ctx;
    if (context == NULL || candidate == NULL)
    {
        return 0.0f;
    }

    if (context->travel_time > 0.0f)
    {
        return context->travel_time;
    }

    if (start_area <= 0 || candidate->area <= 0)
    {
        return 0.0f;
    }

    vec3_t origin = {0.0f, 0.0f, 0.0f};
    if (context->slot != NULL && context->slot->client_update_valid)
    {
        VectorCopy(context->slot->last_client_update.origin, origin);
    }

    vec3_t delta;
    VectorSubtract(candidate->origin, origin, delta);
    return sqrtf(delta[0] * delta[0] + delta[1] * delta[1] + delta[2] * delta[2]);
}

static void goal_service_notify(void *ctx, const ai_goal_selection_t *selection)
{
    goal_move_service_context_t *context = (goal_move_service_context_t *)ctx;
    if (context == NULL || context->log == NULL || selection == NULL)
    {
        return;
    }

    context->log->notify_count += 1;
    context->log->last_selection = *selection;
}

static int goal_service_area(void *ctx, const vec3_t origin)
{
    (void)origin;
    goal_move_service_context_t *context = (goal_move_service_context_t *)ctx;
    if (context == NULL || context->default_area <= 0)
    {
        return 1;
    }
    return context->default_area;
}

static void prepare_goal_move_services(bot_client_state_t *slot,
                                       goal_move_service_context_t *context,
                                       goal_move_log_t *log)
{
    assert_non_null(slot);
    assert_non_null(context);

    context->slot = slot;
    context->log = log;
    context->travel_time = 0.0f;
    context->default_area = 1;

    ai_goal_services_t services = {
        .weight_fn = goal_service_weight,
        .travel_time_fn = goal_service_travel,
        .notify_fn = goal_service_notify,
        .area_fn = goal_service_area,
        .userdata = context,
        .avoid_duration = 5.0f,
    };
    AI_GoalState_SetServices(slot->goal_state, &services);
    AI_GoalState_SetCurrentArea(slot->goal_state, context->default_area);
}

static void seed_goal_candidates(bot_client_state_t *slot)
{
    assert_non_null(slot);

    AI_GoalState_ClearCandidates(slot->goal_state);

    ai_goal_candidate_t candidate;
    memset(&candidate, 0, sizeof(candidate));
    candidate.item_index = 1;
    candidate.area = 1;
    candidate.base_weight = 25.0f;
    VectorSet(candidate.origin, 128.0f, 0.0f, 0.0f);
    assert_true(AI_GoalState_AddCandidate(slot->goal_state, &candidate));
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

    LibVarSet("gladiator_asset_dir", env->assets.asset_root);
    LibVarSet("weaponconfig", "weapons.c");
    LibVarSet("itemconfig", "items.c");
    LibVarSet("max_weaponinfo", "64");
    LibVarSet("max_projectileinfo", "64");
    LibVarSet("dmflags", "0");
    LibVarSet("usehook", "1");
    LibVarSet("rocketjump", "1");

    assert_true(BotMemory_Init(TEST_BOTLIB_HEAP_SIZE));
    env->memory_initialised = true;

    env->exports = GetBotAPI(&g_test_bot_import);
    assert_non_null(env->exports);

    env->exports->BotLibVarSet("gladiator_asset_dir", env->assets.asset_root);
    env->exports->BotLibVarSet("weaponconfig", "weapons.c");
    env->exports->BotLibVarSet("itemconfig", "items.c");
    env->exports->BotLibVarSet("max_weaponinfo", "64");
    env->exports->BotLibVarSet("max_projectileinfo", "64");
    env->exports->BotLibVarSet("dmflags", "0");
    env->exports->BotLibVarSet("usehook", "1");
    env->exports->BotLibVarSet("rocketjump", "1");

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

static void reset_goal_runtime(bot_client_state_t *slot)
{
    assert_non_null(slot);

    if (slot->goal_handle > 0)
    {
        AI_GoalBotlib_ResetState(slot->goal_handle);
    }

    slot->goal_snapshot_count = 0;
    memset(slot->goal_snapshot, 0, sizeof(slot->goal_snapshot));
    slot->active_goal_number = 0;
}

static float test_normalise_direction(vec3_t out, const vec3_t in)
{
    if (out == NULL || in == NULL)
    {
        return 0.0f;
    }

    float length = sqrtf(in[0] * in[0] + in[1] * in[1] + in[2] * in[2]);
    if (length <= 0.0001f)
    {
        VectorClear(out);
        return 0.0f;
    }

    float inv = 1.0f / length;
    out[0] = in[0] * inv;
    out[1] = in[1] * inv;
    out[2] = in[2] * inv;
    return length;
}

static void push_stack_goal(test_environment_t *env,
                            int number,
                            const vec3_t origin,
                            int areanum,
                            float base_weight)
{
    assert_non_null(env);

    bot_client_state_t *slot = BotState_Get(0);
    assert_non_null(slot);

    char classname[32];
    snprintf(classname, sizeof(classname), "test_item_%d", number);

    bot_levelitem_setup_t setup = {
        .classname = classname,
        .goal = {
            .areanum = areanum,
            .entitynum = number,
            .number = number,
            .flags = GFL_ITEM,
        },
        .respawntime = 0.0f,
        .weight = base_weight,
        .flags = GFL_ITEM,
    };
    VectorCopy(origin, setup.goal.origin);
    VectorSet(setup.goal.mins, -16.0f, -16.0f, -16.0f);
    VectorSet(setup.goal.maxs, 16.0f, 16.0f, 16.0f);

    int registered = BotGoal_RegisterLevelItem(&setup);
    assert_int_equal(registered, number);

    int pushed = env->exports->BotPushGoal(slot->goal_handle, &setup.goal);
    assert_int_not_equal(pushed, 0);
}

/**
 * Drives the BotUpdateClient bridge with optional origin/view overrides so
 * tests can seed precise positional data without duplicating boilerplate.
 */
static void submit_client_update_with_view(bot_export_t *exports,
                                           float frame_time,
                                           const vec3_t origin,
                                           const vec3_t viewangles)
{
    assert_non_null(exports);

    bot_updateclient_t update;
    memset(&update, 0, sizeof(update));

    if (origin != NULL)
    {
        VectorCopy(origin, update.origin);
    }
    if (viewangles != NULL)
    {
        VectorCopy(viewangles, update.viewangles);
    }

    exports->BotStartFrame(frame_time);
    for (int i = 0; i < MAX_ITEMS; ++i)
    {
        update.inventory[i] = 1;
    }

    int status = exports->BotUpdateClient(0, &update);
    assert_int_equal(status, BLERR_NOERROR);
}

static void submit_client_update(bot_export_t *exports, float frame_time)
{
    submit_client_update_with_view(exports, frame_time, NULL, NULL);
}

static void submit_enemy_entity(bot_export_t *exports, int ent, const vec3_t origin)
{
    bot_updateentity_t enemy;
    memset(&enemy, 0, sizeof(enemy));
    VectorCopy(origin, enemy.origin);
    int status = exports->BotUpdateEntity(ent, &enemy);
    assert_int_equal(status, BLERR_NOERROR);
}

static void test_goal_refresh_and_movement_dispatch_order(void **state)
{
    test_environment_t *env = (test_environment_t *)(*state);
    activate_test_client(env);

    bot_client_state_t *slot = BotState_Get(0);
    assert_non_null(slot);

    reset_goal_runtime(slot);

    vec3_t secondary_origin;
    VectorSet(secondary_origin, -64.0f, 32.0f, 0.0f);
    vec3_t primary_origin;
    VectorSet(primary_origin, 128.0f, 0.0f, 0.0f);

    push_stack_goal(env, 7, secondary_origin, 77, 15.0f);
    push_stack_goal(env, 3, primary_origin, 33, 25.0f);

    vec3_t client_origin;
    VectorSet(client_origin, 16.0f, 8.0f, 0.0f);
    vec3_t client_viewangles;
    VectorSet(client_viewangles, 5.0f, 10.0f, -2.0f);
    submit_client_update_with_view(env->exports, 1.0f, client_origin, client_viewangles);

    submit_client_update(env->exports, 1.0f);
    test_reset_bot_input_log();

    int status = env->exports->BotAI(0, 0.1f);
    assert_int_equal(status, BLERR_NOERROR);

    assert_int_equal(slot->goal_snapshot_count, 2);
    assert_int_equal(slot->goal_snapshot[0].number, 3);
    assert_int_equal(slot->goal_snapshot[1].number, 7);
    assert_int_equal(slot->active_goal_number, 3);

    assert_int_equal(g_bot_input_log.count, 1);
    assert_int_equal(g_bot_input_log.last_client, 0);
    assert_float_equal(g_bot_input_log.last_command.thinktime, 0.1f, 0.0001f);
    assert_float_equal(g_bot_input_log.last_command.viewangles[0], client_viewangles[0], 0.0001f);
    assert_float_equal(g_bot_input_log.last_command.viewangles[1], client_viewangles[1], 0.0001f);
    assert_float_equal(g_bot_input_log.last_command.viewangles[2], client_viewangles[2], 0.0001f);

    vec3_t expected_delta;
    VectorSubtract(primary_origin, client_origin, expected_delta);
    vec3_t expected_dir;
    float expected_speed = test_normalise_direction(expected_dir, expected_delta);

    assert_float_equal(g_bot_input_log.last_command.speed, expected_speed, 0.0001f);
    assert_float_equal(g_bot_input_log.last_command.dir[0], expected_dir[0], 0.0001f);
    assert_float_equal(g_bot_input_log.last_command.dir[1], expected_dir[1], 0.0001f);
    assert_float_equal(g_bot_input_log.last_command.dir[2], expected_dir[2], 0.0001f);

    assert_int_equal(slot->current_weapon, 0);
    assert_false(slot->has_move_result);
}

static void test_movement_error_propagates_without_submission(void **state)
{
    test_environment_t *env = (test_environment_t *)(*state);
    activate_test_client(env);

    bot_client_state_t *slot = BotState_Get(0);
    assert_non_null(slot);

    reset_goal_runtime(slot);

    vec3_t goal_origin;
    VectorSet(goal_origin, 128.0f, 0.0f, 0.0f);
    push_stack_goal(env, 11, goal_origin, 41, 20.0f);

    vec3_t client_origin;
    VectorSet(client_origin, 16.0f, 8.0f, 0.0f);
    vec3_t client_viewangles;
    VectorSet(client_viewangles, 0.0f, 90.0f, 0.0f);
    submit_client_update_with_view(env->exports, 2.0f, client_origin, client_viewangles);

    int original_handle = slot->move_handle;
    qboolean previous_loaded = aasworld.loaded;

    slot->move_handle = MAX_CLIENTS + 5;
    aasworld.loaded = qtrue;

    submit_client_update(env->exports, 1.0f);
    test_reset_bot_input_log();

    int status = env->exports->BotAI(0, 0.2f);
    assert_int_equal(status, BLERR_INVALIDIMPORT);

    ai_avoid_list_t *avoid = AI_GoalState_GetAvoidList(slot->goal_state);
    assert_true(AI_AvoidList_Contains(avoid, 11, 0.0f));
    assert_int_equal(g_bot_input_log.count, 0);

    slot->move_handle = original_handle;
    aasworld.loaded = previous_loaded;
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
        .weight_fn = NULL,
        .travel_time_fn = NULL,
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

static void test_dm_enemy_attack_and_weapon_selection(void **state)
{
    test_environment_t *env = (test_environment_t *)(*state);
    activate_test_client(env);

    bot_client_state_t *slot = BotState_Get(0);
    assert_non_null(slot);

    goal_move_service_context_t context;
    goal_move_log_t log = {0};
    prepare_goal_move_services(slot, &context, &log);
    seed_goal_candidates(slot);

    submit_client_update(env->exports, 1.0f);
    vec3_t enemy_origin;
    VectorSet(enemy_origin, 196.0f, 32.0f, 0.0f);
    submit_enemy_entity(env->exports, 1, enemy_origin);

    test_reset_bot_input_log();

    int status = env->exports->BotAI(0, 0.1f);
    assert_int_equal(status, BLERR_NOERROR);

    assert_int_equal(g_bot_input_log.count, 1);
    assert_true((g_bot_input_log.last_command.actionflags & ACTION_ATTACK) != 0);
    assert_true(g_bot_input_log.last_command.weapon > 0);
}

static void test_dm_rocketjump_respects_libvar(void **state)
{
    test_environment_t *env = (test_environment_t *)(*state);
    activate_test_client(env);

    bot_client_state_t *slot = BotState_Get(0);
    assert_non_null(slot);

    goal_move_service_context_t context;
    goal_move_log_t log = {0};
    prepare_goal_move_services(slot, &context, &log);
    context.travel_time = 40.0f;
    seed_goal_candidates(slot);

    vec3_t elevated_enemy;
    VectorSet(elevated_enemy, 320.0f, -24.0f, 192.0f);

    submit_client_update(env->exports, 2.0f);
    submit_enemy_entity(env->exports, 1, elevated_enemy);

    test_reset_bot_input_log();

    int status = env->exports->BotAI(0, 0.1f);
    assert_int_equal(status, BLERR_NOERROR);
    assert_true((g_bot_input_log.last_command.actionflags & ACTION_JUMP) != 0);

    ai_avoid_list_t *avoid = AI_GoalState_GetAvoidList(slot->goal_state);
    assert_true(AI_AvoidList_Contains(avoid, 1, 2.0f));

    LibVarSet("rocketjump", "0");

    submit_client_update(env->exports, 3.0f);
    submit_enemy_entity(env->exports, 1, elevated_enemy);

    test_reset_bot_input_log();

    status = env->exports->BotAI(0, 0.1f);
    assert_int_equal(status, BLERR_NOERROR);
    assert_true((g_bot_input_log.last_command.actionflags & ACTION_JUMP) == 0);
}

static void test_dm_enemy_skips_invisible_targets(void **state)
{
    test_environment_t *env = (test_environment_t *)(*state);
    activate_test_client(env);

    bot_client_state_t *slot = BotState_Get(0);
    assert_non_null(slot);
    reset_goal_runtime(slot);

    vec3_t client_origin = {0.0f, 0.0f, 0.0f};
    vec3_t client_viewangles = {0.0f, 0.0f, 0.0f};
    submit_client_update_with_view(env->exports, 1.0f, client_origin, client_viewangles);
    submit_client_update(env->exports, 1.0f);

    bot_updateentity_t self_entity;
    memset(&self_entity, 0, sizeof(self_entity));
    VectorCopy(client_origin, self_entity.origin);
    self_entity.renderfx = RF_SHELL_RED;
    assert_int_equal(env->exports->BotUpdateEntity(0, &self_entity), BLERR_NOERROR);

    bot_updateentity_t invisible;
    memset(&invisible, 0, sizeof(invisible));
    VectorSet(invisible.origin, 256.0f, 0.0f, 0.0f);
    invisible.renderfx = RF_TRANSLUCENT;
    assert_int_equal(env->exports->BotUpdateEntity(1, &invisible), BLERR_NOERROR);

    bot_updateentity_t visible;
    memset(&visible, 0, sizeof(visible));
    VectorSet(visible.origin, 128.0f, 0.0f, 0.0f);
    assert_int_equal(env->exports->BotUpdateEntity(2, &visible), BLERR_NOERROR);

    submit_client_update(env->exports, 1.5f);
    test_reset_bot_input_log();

    int status = env->exports->BotAI(0, 0.1f);
    assert_int_equal(status, BLERR_NOERROR);
    assert_int_equal(slot->enemy_entity, 2);
    assert_float_equal(slot->enemy_last_known_origin[0], visible.origin[0], 0.01f);
    assert_float_equal(slot->enemy_last_known_origin[1], visible.origin[1], 0.01f);
    assert_float_equal(slot->enemy_last_known_origin[2], visible.origin[2], 0.01f);
}

static void test_dm_enemy_skips_chatting_targets_for_cautious_bots(void **state)
{
    test_environment_t *env = (test_environment_t *)(*state);
    activate_test_client(env);

    bot_client_state_t *slot = BotState_Get(0);
    assert_non_null(slot);
    reset_goal_runtime(slot);

    vec3_t client_origin = {0.0f, 0.0f, 0.0f};
    vec3_t client_viewangles = {0.0f, 0.0f, 0.0f};
    submit_client_update_with_view(env->exports, 2.0f, client_origin, client_viewangles);
    submit_client_update(env->exports, 2.0f);

    bot_updateentity_t self_entity;
    memset(&self_entity, 0, sizeof(self_entity));
    VectorCopy(client_origin, self_entity.origin);
    self_entity.renderfx = RF_SHELL_RED;
    assert_int_equal(env->exports->BotUpdateEntity(0, &self_entity), BLERR_NOERROR);

    bot_updateentity_t chatting;
    memset(&chatting, 0, sizeof(chatting));
    VectorSet(chatting.origin, 64.0f, 0.0f, 0.0f);
    chatting.sound = CHAN_VOICE;
    assert_int_equal(env->exports->BotUpdateEntity(3, &chatting), BLERR_NOERROR);

    bot_updateentity_t quiet;
    memset(&quiet, 0, sizeof(quiet));
    VectorSet(quiet.origin, 192.0f, 0.0f, 0.0f);
    assert_int_equal(env->exports->BotUpdateEntity(4, &quiet), BLERR_NOERROR);

    submit_client_update(env->exports, 2.5f);
    test_reset_bot_input_log();

    int status = env->exports->BotAI(0, 0.1f);
    assert_int_equal(status, BLERR_NOERROR);
    assert_int_equal(slot->enemy_entity, 4);
    assert_float_equal(slot->enemy_last_known_origin[0], quiet.origin[0], 0.01f);
    assert_float_equal(slot->enemy_last_known_origin[1], quiet.origin[1], 0.01f);
    assert_float_equal(slot->enemy_last_known_origin[2], quiet.origin[2], 0.01f);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_setup_allocates_goal_move_states,
                                        goal_move_setup,
                                        goal_move_teardown),
        cmocka_unit_test_setup_teardown(test_goal_snapshot_and_dispatch_uses_stack,
                                        goal_move_setup,
                                        goal_move_teardown),
        cmocka_unit_test_setup_teardown(test_movement_error_propagates_without_submission,
                                        goal_move_setup,
                                        goal_move_teardown),
        cmocka_unit_test_setup_teardown(test_bot_update_client_propagates_area_errors,
                                        goal_move_setup,
                                        goal_move_teardown),
        cmocka_unit_test_setup_teardown(test_dm_enemy_attack_and_weapon_selection,
                                        goal_move_setup,
                                        goal_move_teardown),
        cmocka_unit_test_setup_teardown(test_dm_rocketjump_respects_libvar,
                                        goal_move_setup,
                                        goal_move_teardown),
        cmocka_unit_test_setup_teardown(test_dm_enemy_skips_invisible_targets,
                                        goal_move_setup,
                                        goal_move_teardown),
        cmocka_unit_test_setup_teardown(test_dm_enemy_skips_chatting_targets_for_cautious_bots,
                                        goal_move_setup,
                                        goal_move_teardown),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}

