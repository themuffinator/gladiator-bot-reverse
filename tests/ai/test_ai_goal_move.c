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

#ifndef cmocka_skip
#define cmocka_skip(...) skip()
#endif

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
#include "botlib/ai_goal/ai_goal.h"
#include "botlib/ai_goal/bot_goal.h"
#include "botlib/ai/goal_move_orchestrator.h"
#include "botlib/common/l_libvar.h"
#include "botlib/common/l_memory.h"
#include "botlib/interface/bot_state.h"
#include "botlib/interface/botlib_interface.h"
#include "q2bridge/aas_translation.h"
#include "q2bridge/botlib.h"
#include "../support/asset_env.h"

#ifndef PROJECT_SOURCE_DIR
#error "PROJECT_SOURCE_DIR must be defined so regression tests can resolve asset paths."
#endif

#define TEST_BOTLIB_HEAP_SIZE (8u << 20)
#define TEST_MAX_LOG_MESSAGES 64
#define TEST_INVENTORY_ROCKETLAUNCHER 14

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
    trace.fraction = 1.0f;
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
    .CvarGet = NULL,
    .Error = NULL,
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

    LibVarSet("basedir", env->assets.asset_root);
    LibVarSet("gamedir", "");
    LibVarSet("cddir", "");
    LibVarSet("gladiator_asset_dir", "");
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

    env->exports->BotLibVarSet("basedir", env->assets.asset_root);
    env->exports->BotLibVarSet("gamedir", "");
    env->exports->BotLibVarSet("cddir", "");
    env->exports->BotLibVarSet("gladiator_asset_dir", "");
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
    snprintf(settings->charactername, sizeof(settings->charactername), "babe");
}

static void activate_test_client(test_environment_t *env)
{
    bot_settings_t settings;
    configure_standard_bot_settings(&settings);

    int status = env->exports->BotSetupClient(0, &settings);
    assert_int_equal(status, BLERR_NOERROR);
    env->client_active = true;
}

/*
=============
write_goal_weight_fixture

Writes a temporary item-weight script for exported goal wiring checks.
=============
*/
static void write_goal_weight_fixture(const char *path, const char *contents)
{
	FILE *file = fopen(path, "wb");
	assert_non_null(file);
	assert_true(fputs(contents, file) >= 0);
	assert_int_equal(fclose(file), 0);
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

/*
=============
test_goal_setup_loads_item_weights_into_goal_state

Checks the HLIL setup path that loads CHARACTERISTIC_ITEMWEIGHTS into the
botlib goal state, not only the parsed character profile.
=============
*/
static void test_goal_setup_loads_item_weights_into_goal_state(void **state)
{
	test_environment_t *env = (test_environment_t *)(*state);
	activate_test_client(env);

	bot_client_state_t *slot = BotState_Get(0);
	assert_non_null(slot);
	assert_true(slot->goal_handle > 0);
	assert_non_null(slot->item_weights);

	bot_levelitem_setup_t setup;
	memset(&setup, 0, sizeof(setup));
	setup.classname = "weapon_rocketlauncher";
	setup.goal.number = 301;
	setup.goal.entitynum = 301;
	setup.goal.flags = GFL_ITEM;
	setup.flags = GFL_ITEM;
	VectorSet(setup.goal.origin, 64.0f, 0.0f, 16.0f);
	VectorSet(setup.goal.mins, -16.0f, -16.0f, -16.0f);
	VectorSet(setup.goal.maxs, 16.0f, 16.0f, 16.0f);

	assert_int_equal(env->exports->BotRegisterLevelItem(&setup), 301);
	assert_true(env->exports->BotWeightIndex(slot->goal_handle, "weapon_rocketlauncher") >= 0);
	assert_true(BotGoal_ItemWeightIndexByteSize(slot->goal_handle) >= sizeof(int));
}

/*
=============
test_goal_stack_uses_retail_zero_sentinel

Pins the retail goal stack contract: stack slot zero is unused, top zero means
empty, and overflow reports failure without discarding older goals.
=============
*/
static void test_goal_stack_uses_retail_zero_sentinel(void **state)
{
	test_environment_t *env = (test_environment_t *)(*state);

	int handle = env->exports->BotAllocGoalState(5);
	assert_true(handle > 0);

	const bot_goalstate_t *debug = BotGoalStatePeek(handle);
	assert_non_null(debug);
	assert_int_equal(debug->goalstacktop, 0);

	bot_goal_t goal;
	memset(&goal, 0, sizeof(goal));
	for (int i = 1; i < BOT_GOAL_MAX_STACK; ++i)
	{
		goal.number = 700 + i;
		goal.areanum = i;
		assert_true(env->exports->BotPushGoal(handle, &goal));
		debug = BotGoalStatePeek(handle);
		assert_non_null(debug);
		assert_int_equal(debug->goalstacktop, i);
	}

	goal.number = 799;
	assert_false(env->exports->BotPushGoal(handle, &goal));
	debug = BotGoalStatePeek(handle);
	assert_non_null(debug);
	assert_int_equal(debug->goalstacktop, BOT_GOAL_MAX_STACK - 1);

	bot_goal_t top;
	memset(&top, 0, sizeof(top));
	assert_true(env->exports->BotGetTopGoal(handle, &top));
	assert_int_equal(top.number, 700 + BOT_GOAL_MAX_STACK - 1);

	bot_goal_t second;
	memset(&second, 0, sizeof(second));
	assert_true(env->exports->BotGetSecondGoal(handle, &second));
	assert_int_equal(second.number, 700 + BOT_GOAL_MAX_STACK - 2);

	for (int i = 1; i < BOT_GOAL_MAX_STACK; ++i)
	{
		assert_true(env->exports->BotPopGoal(handle));
	}

	assert_false(env->exports->BotPopGoal(handle));
	assert_false(env->exports->BotGetTopGoal(handle, &top));
	debug = BotGoalStatePeek(handle);
	assert_non_null(debug);
	assert_int_equal(debug->goalstacktop, 0);

	env->exports->BotFreeGoalState(handle);
}

/*
=============
test_goal_itemconfig_failure_mapping

Pins the HLIL setup failure path where the shared item configuration is absent:
goal setup returns 0x1d, while per-client item weights report 0x1c.
=============
*/
static void test_goal_itemconfig_failure_mapping(void **state)
{
	test_environment_t *env = (test_environment_t *)(*state);

	assert_int_equal(env->exports->BotLibVarSet("itemconfig", "missing_items.c"), BLERR_NOERROR);
	assert_int_equal(BotSetupGoalAI(), BLERR_CANNOTLOADITEMCONFIG);

	int handle = env->exports->BotAllocGoalState(4);
	assert_true(handle > 0);
	assert_int_equal(env->exports->BotLoadItemWeights(handle, "bots/babe_i.c"),
					 BLERR_CANNOTLOADITEMWEIGHTS);
	assert_int_equal(BotGoal_ItemWeightIndexByteSize(handle), 0);
	env->exports->BotFreeGoalState(handle);

	assert_int_equal(env->exports->BotLibVarSet("itemconfig", "items.c"), BLERR_NOERROR);
	assert_int_equal(BotSetupGoalAI(), BLERR_NOERROR);
}

/*
=============
test_goal_fuzzy_logic_reuses_child_config

Confirms the Q3-shaped genetic helper writes into the child's existing item
weight config instead of cloning or replacing a parent config.
=============
*/
static void test_goal_fuzzy_logic_reuses_child_config(void **state)
{
	test_environment_t *env = (test_environment_t *)(*state);

	assert_int_equal(env->exports->BotLibVarSet("bot_reloadcharacters", "1"), BLERR_NOERROR);

	int parent1 = env->exports->BotAllocGoalState(1);
	int parent2 = env->exports->BotAllocGoalState(2);
	int child = env->exports->BotAllocGoalState(3);
	assert_true(parent1 > 0);
	assert_true(parent2 > 0);
	assert_true(child > 0);

	assert_int_equal(env->exports->BotLoadItemWeights(parent1, "bots/babe_i.c"), BLERR_NOERROR);
	assert_int_equal(env->exports->BotLoadItemWeights(parent2, "bots/bill_i.c"), BLERR_NOERROR);
	assert_int_equal(env->exports->BotLoadItemWeights(child, "bots/byte_i.c"), BLERR_NOERROR);

	const bot_goalstate_t *parent2_state = BotGoalStatePeek(parent2);
	const bot_goalstate_t *child_state = BotGoalStatePeek(child);
	assert_non_null(parent2_state);
	assert_non_null(child_state);
	assert_non_null(parent2_state->itemweightconfig);
	assert_non_null(child_state->itemweightconfig);

	const bot_weight_config_t *child_config = child_state->itemweightconfig;
	int parent2_index = BotWeight_FindIndex(parent2_state->itemweightconfig, "weapon_rocketlauncher");
	int child_index = BotWeight_FindIndex(child_config, "weapon_rocketlauncher");
	assert_true(parent2_index >= 0);
	assert_true(child_index >= 0);

	int inventory[MAX_ITEMS];
	memset(inventory, 0, sizeof(inventory));
	inventory[TEST_INVENTORY_ROCKETLAUNCHER] = 1;

	float before = FuzzyWeight(inventory, child_config, child_index);
	float parent2_value = FuzzyWeight(inventory, parent2_state->itemweightconfig, parent2_index);
	assert_true(fabsf(parent2_value - before) > 0.0001f);

	env->exports->BotInterbreedGoalFuzzyLogic(parent1, parent2, child);

	child_state = BotGoalStatePeek(child);
	assert_non_null(child_state);
	assert_ptr_equal(child_state->itemweightconfig, child_config);

	float after = FuzzyWeight(inventory, child_state->itemweightconfig, child_index);
	assert_true(fabsf(after - before) > 0.0001f);

	env->exports->BotFreeGoalState(child);
	env->exports->BotFreeGoalState(parent2);
	env->exports->BotFreeGoalState(parent1);
	assert_int_equal(env->exports->BotLibVarSet("bot_reloadcharacters", "0"), BLERR_NOERROR);
}

/*
=============
test_goal_item_scoring_uses_undecided_fuzzy_weights

Pins the Q3 UNDECIDEDFUZZY route from goal item scoring into ai_weight.
=============
*/
static void test_goal_item_scoring_uses_undecided_fuzzy_weights(void **state)
{
	test_environment_t *env = (test_environment_t *)(*state);
	activate_test_client(env);

	bot_client_state_t *slot = BotState_Get(0);
	assert_non_null(slot);
	assert_true(slot->goal_handle > 0);

	char fixture_path[PATH_MAX];
	int written = snprintf(fixture_path,
		sizeof(fixture_path),
		"%s/tests/support/assets/bots/goal_undecided_tmp.w",
		PROJECT_SOURCE_DIR);
	assert_true(written > 0 && written < (int)sizeof(fixture_path));

	const char *fixture =
		"weight \"goal_undecided_item\"\n"
		"{\n"
		"switch(0)\n"
		"{\n"
		"case 1:\n"
		"{\n"
		"return balance(1000,10,20);\n"
		"}\n"
		"default:\n"
		"{\n"
		"return balance(5,5,5);\n"
		"}\n"
		"}\n"
		"}\n";
	write_goal_weight_fixture(fixture_path, fixture);

	bot_levelitem_setup_t setup;
	memset(&setup, 0, sizeof(setup));
	setup.classname = "goal_undecided_item";
	setup.goal.number = 303;
	setup.goal.entitynum = 303;
	setup.goal.areanum = 1;
	setup.goal.flags = GFL_ITEM;
	setup.flags = GFL_ITEM;
	VectorSet(setup.goal.origin, 16.0f, 0.0f, 16.0f);
	VectorSet(setup.goal.mins, -16.0f, -16.0f, -16.0f);
	VectorSet(setup.goal.maxs, 16.0f, 16.0f, 16.0f);

	assert_int_equal(env->exports->BotRegisterLevelItem(&setup), 303);
	assert_int_equal(env->exports->BotLoadItemWeights(slot->goal_handle, fixture_path), BLERR_NOERROR);
	assert_true(env->exports->BotWeightIndex(slot->goal_handle, "goal_undecided_item") >= 0);

	int inventory[MAX_ITEMS];
	memset(inventory, 0, sizeof(inventory));

	vec3_t origin;
	VectorSet(origin, 0.0f, 0.0f, 0.0f);

	int travel_time = -1;
	float score = BotGoal_EvaluateStackGoal(slot->goal_handle,
		&setup.goal,
		origin,
		0,
		inventory,
		TFL_DEFAULT,
		&travel_time);

	assert_int_equal(travel_time, 0);
	assert_true(score >= 10.0f);
	assert_true(score <= 20.0f);

	remove(fixture_path);
}

/*
=============
test_goal_retail_exports_and_avoid_sync

Exercises the retail goal helpers that are now wired through GetBotAPI and
verifies orchestrator synchronisation no longer erases botlib-owned avoid
timers.
=============
*/
static void test_goal_retail_exports_and_avoid_sync(void **state)
{
	test_environment_t *env = (test_environment_t *)(*state);
	activate_test_client(env);

	assert_non_null(env->exports->BotEmptyGoalStack);
	assert_non_null(env->exports->BotRemoveFromAvoidGoals);
	assert_non_null(env->exports->BotAvoidGoalTime);
	assert_non_null(env->exports->BotSetAvoidGoalTime);
	assert_non_null(env->exports->BotDumpAvoidGoals);
	assert_non_null(env->exports->BotDumpGoalStack);
	assert_non_null(env->exports->BotGoalName);
	assert_non_null(env->exports->BotGetLevelItemGoal);
	assert_non_null(env->exports->BotGetNextCampSpotGoal);
	assert_non_null(env->exports->BotGetMapLocationGoal);
	assert_non_null(env->exports->BotInterbreedGoalFuzzyLogic);
	assert_non_null(env->exports->BotSaveGoalFuzzyLogic);
	assert_non_null(env->exports->BotMutateGoalFuzzyLogic);

	bot_client_state_t *slot = BotState_Get(0);
	assert_non_null(slot);

	env->exports->BotSetAvoidGoalTime(slot->goal_handle, 77, 5.0f);
	assert_float_equal(env->exports->BotAvoidGoalTime(slot->goal_handle, 77), 5.0f, 0.0001f);

	ai_avoid_list_t *avoid = AI_GoalState_GetAvoidList(slot->goal_state);
	assert_non_null(avoid);
	assert_true(AI_AvoidList_Add(avoid, 77, 2.0f));

	AI_GoalBotlib_SynchroniseAvoid(slot->goal_handle, slot->goal_state, 1.0f);
	assert_float_equal(env->exports->BotAvoidGoalTime(slot->goal_handle, 77), 4.0f, 0.0001f);

	env->exports->BotRemoveFromAvoidGoals(slot->goal_handle, 77);
	assert_float_equal(env->exports->BotAvoidGoalTime(slot->goal_handle, 77), 0.0f, 0.0001f);

	bot_levelitem_setup_t setup;
	memset(&setup, 0, sizeof(setup));
	setup.classname = "weapon_rocketlauncher";
	setup.goal.number = 302;
	setup.goal.entitynum = 302;
	setup.goal.flags = GFL_ITEM;
	setup.flags = GFL_ITEM;
	VectorSet(setup.goal.origin, 128.0f, 16.0f, 24.0f);
	VectorSet(setup.goal.mins, -16.0f, -16.0f, -16.0f);
	VectorSet(setup.goal.maxs, 16.0f, 16.0f, 16.0f);

	assert_int_equal(env->exports->BotRegisterLevelItem(&setup), 302);

	env->exports->BotSetAvoidGoalTime(slot->goal_handle, 302, -1.0f);
	assert_float_equal(env->exports->BotAvoidGoalTime(slot->goal_handle, 302),
					   30.0f,
					   0.0001f);

	bot_goal_t found_goal;
	memset(&found_goal, 0, sizeof(found_goal));
	assert_int_equal(env->exports->BotGetLevelItemGoal(0, "weapon_rocketlauncher", &found_goal), 302);
	assert_int_equal(found_goal.number, 302);

	char goal_name[64];
	memset(goal_name, 0, sizeof(goal_name));
	env->exports->BotGoalName(302, goal_name, sizeof(goal_name));
	assert_true(goal_name[0] != '\0');

	memset(goal_name, 0x7f, sizeof(goal_name));
	env->exports->BotGoalName(123456, goal_name, sizeof(goal_name));
	assert_string_equal(goal_name, "");

	assert_true(env->exports->BotPushGoal(slot->goal_handle, &found_goal));
	assert_true(env->exports->BotGetTopGoal(slot->goal_handle, &found_goal));
	env->exports->BotDumpGoalStack(slot->goal_handle);
	env->exports->BotEmptyGoalStack(slot->goal_handle);
	assert_false(env->exports->BotGetTopGoal(slot->goal_handle, &found_goal));
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

static void submit_client_update(bot_export_t *exports,
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
    for (int i = 0; i < MAX_ITEMS; ++i) {
        update.inventory[i] = 1;
    }

    int status = exports->BotUpdateClient(0, &update);
    assert_int_equal(status, BLERR_NOERROR);
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
    submit_client_update(env->exports, 1.0f, client_origin, client_viewangles);

    submit_client_update(env->exports, 1.0f, client_origin, client_viewangles);
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
    assert_float_equal(g_bot_input_log.last_command.viewangles[0], client_viewangles[0], 0.01f);
    assert_float_equal(g_bot_input_log.last_command.viewangles[1], client_viewangles[1], 0.01f);
    assert_float_equal(g_bot_input_log.last_command.viewangles[2], client_viewangles[2], 0.01f);

    vec3_t expected_delta;
    VectorSubtract(primary_origin, client_origin, expected_delta);
    vec3_t expected_dir;
    float expected_speed = test_normalise_direction(expected_dir, expected_delta);

    assert_float_equal(g_bot_input_log.last_command.speed, expected_speed, 0.0001f);
    assert_float_equal(g_bot_input_log.last_command.dir[0], expected_dir[0], 0.0001f);
    assert_float_equal(g_bot_input_log.last_command.dir[1], expected_dir[1], 0.0001f);
    assert_float_equal(g_bot_input_log.last_command.dir[2], expected_dir[2], 0.0001f);

    assert_int_equal(slot->current_weapon, 6);
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
    submit_client_update(env->exports, 2.0f, client_origin, client_viewangles);

    int original_handle = slot->move_handle;
    qboolean previous_loaded = aasworld.loaded;

    slot->move_handle = MAX_CLIENTS + 5;
    aasworld.loaded = qtrue;

    submit_client_update(env->exports, 1.0f, NULL, NULL);
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

static void test_dm_enemy_selection_filters_invisible_and_chat(void **state)
{
    test_environment_t *env = (test_environment_t *)(*state);
    activate_test_client(env);

    bot_client_state_t *self = BotState_Get(0);
    assert_non_null(self);
    self->team = 1;

    bot_updateclient_t self_update;
    memset(&self_update, 0, sizeof(self_update));
    VectorSet(self_update.origin, 0.0f, 0.0f, 24.0f);
    VectorSet(self_update.viewangles, 0.0f, 0.0f, 0.0f);
    self_update.stats[STAT_HEALTH] = 100;
    for (int i = 0; i < MAX_ITEMS; ++i)
    {
        self_update.inventory[i] = 1;
    }

    bot_updateclient_t enemy_update;
    memset(&enemy_update, 0, sizeof(enemy_update));
    VectorSet(enemy_update.origin, 128.0f, 0.0f, 24.0f);
    VectorSet(enemy_update.viewangles, 0.0f, 180.0f, 0.0f);
    enemy_update.stats[STAT_HEALTH] = 100;

    bot_updateentity_t enemy_entity;
    memset(&enemy_entity, 0, sizeof(enemy_entity));
    VectorCopy(enemy_update.origin, enemy_entity.origin);

    bot_client_state_t *enemy_state = BotState_Create(1);
    assert_non_null(enemy_state);
    enemy_state->active = true;
    enemy_state->team = 1;
    enemy_state->last_client_update = enemy_update;
    enemy_state->client_update_valid = true;
    aasworld.loaded = qtrue;
    TranslateEntity_SetWorldLoaded(qtrue);

    int status = env->exports->BotStartFrame(0.1f);
    assert_int_equal(status, BLERR_NOERROR);
    status = env->exports->BotUpdateClient(0, &self_update);
    assert_int_equal(status, BLERR_NOERROR);
    status = env->exports->BotUpdateEntity(1, &enemy_entity);
    assert_int_equal(status, BLERR_NOERROR);

    status = env->exports->BotAI(0, 0.1f);
    assert_int_equal(status, BLERR_NOERROR);
    assert_int_equal(self->combat.current_enemy, -1);

    enemy_state->team = 2;
    enemy_entity.renderfx = RF_TRANSLUCENT;

    status = env->exports->BotStartFrame(0.2f);
    assert_int_equal(status, BLERR_NOERROR);
    status = env->exports->BotUpdateClient(0, &self_update);
    assert_int_equal(status, BLERR_NOERROR);
    status = env->exports->BotUpdateEntity(1, &enemy_entity);
    assert_int_equal(status, BLERR_NOERROR);
    enemy_state->last_client_update = enemy_update;
    enemy_state->client_update_valid = true;

    status = env->exports->BotAI(0, 0.1f);
    assert_int_equal(status, BLERR_NOERROR);
    assert_int_equal(self->combat.current_enemy, -1);

    enemy_entity.renderfx = 0;
    enemy_state->last_client_update.stats[STAT_LAYOUTS] = 1;

    status = env->exports->BotStartFrame(0.3f);
    assert_int_equal(status, BLERR_NOERROR);
    status = env->exports->BotUpdateClient(0, &self_update);
    assert_int_equal(status, BLERR_NOERROR);
    status = env->exports->BotUpdateEntity(1, &enemy_entity);
    assert_int_equal(status, BLERR_NOERROR);

    status = env->exports->BotAI(0, 0.1f);
    assert_int_equal(status, BLERR_NOERROR);
    assert_int_equal(self->combat.current_enemy, -1);

    enemy_state->last_client_update.stats[STAT_LAYOUTS] = 0;

    status = env->exports->BotStartFrame(0.4f);
    assert_int_equal(status, BLERR_NOERROR);
    status = env->exports->BotUpdateClient(0, &self_update);
    assert_int_equal(status, BLERR_NOERROR);
    status = env->exports->BotUpdateEntity(1, &enemy_entity);
    assert_int_equal(status, BLERR_NOERROR);

    status = env->exports->BotAI(0, 0.1f);
    assert_int_equal(status, BLERR_NOERROR);
    assert_int_equal(self->combat.current_enemy, 1);
    assert_true(self->combat.enemy_visible);

    BotState_Destroy(1);
}

static void test_dm_enemy_selection_damage_alert(void **state)
{
    test_environment_t *env = (test_environment_t *)(*state);
    activate_test_client(env);

    bot_client_state_t *self = BotState_Get(0);
    assert_non_null(self);
    self->team = 1;

    bot_updateclient_t self_update;
    memset(&self_update, 0, sizeof(self_update));
    VectorSet(self_update.origin, 0.0f, 0.0f, 24.0f);
    VectorSet(self_update.viewangles, 0.0f, 0.0f, 0.0f);
    self_update.stats[STAT_HEALTH] = 100;
    for (int i = 0; i < MAX_ITEMS; ++i)
    {
        self_update.inventory[i] = 1;
    }

    bot_updateclient_t enemy_update;
    memset(&enemy_update, 0, sizeof(enemy_update));
    VectorSet(enemy_update.origin, -128.0f, 0.0f, 24.0f);
    VectorSet(enemy_update.viewangles, 0.0f, 0.0f, 0.0f);
    enemy_update.stats[STAT_HEALTH] = 100;

    bot_updateentity_t enemy_entity;
    memset(&enemy_entity, 0, sizeof(enemy_entity));
    VectorCopy(enemy_update.origin, enemy_entity.origin);

    bot_client_state_t *enemy_state = BotState_Create(1);
    assert_non_null(enemy_state);
    enemy_state->active = true;
    enemy_state->team = 2;
    enemy_state->last_client_update = enemy_update;
    enemy_state->client_update_valid = true;
    aasworld.loaded = qtrue;
    TranslateEntity_SetWorldLoaded(qtrue);

    int status = env->exports->BotStartFrame(1.0f);
    assert_int_equal(status, BLERR_NOERROR);
    status = env->exports->BotUpdateClient(0, &self_update);
    assert_int_equal(status, BLERR_NOERROR);
    status = env->exports->BotUpdateEntity(1, &enemy_entity);
    assert_int_equal(status, BLERR_NOERROR);

    status = env->exports->BotAI(0, 0.1f);
    assert_int_equal(status, BLERR_NOERROR);
    assert_int_equal(self->combat.current_enemy, -1);

    self_update.stats[STAT_HEALTH] = 60;
    enemy_state->last_client_update = enemy_update;
    enemy_state->client_update_valid = true;

    status = env->exports->BotStartFrame(1.1f);
    assert_int_equal(status, BLERR_NOERROR);
    status = env->exports->BotUpdateClient(0, &self_update);
    assert_int_equal(status, BLERR_NOERROR);
    status = env->exports->BotUpdateEntity(1, &enemy_entity);
    assert_int_equal(status, BLERR_NOERROR);

    status = env->exports->BotAI(0, 0.1f);
    assert_int_equal(status, BLERR_NOERROR);
    assert_int_equal(self->combat.current_enemy, 1);
    assert_true(self->combat.took_damage);
    assert_true(self->combat.enemy_visible);
    assert_true(self->combat.last_damage_time >= 1.1f);

    BotState_Destroy(1);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_setup_allocates_goal_move_states,
                                        goal_move_setup,
                                        goal_move_teardown),
        cmocka_unit_test_setup_teardown(test_goal_setup_loads_item_weights_into_goal_state,
                                        goal_move_setup,
                                        goal_move_teardown),
        cmocka_unit_test_setup_teardown(test_goal_stack_uses_retail_zero_sentinel,
                                        goal_move_setup,
                                        goal_move_teardown),
        cmocka_unit_test_setup_teardown(test_goal_itemconfig_failure_mapping,
                                        goal_move_setup,
                                        goal_move_teardown),
        cmocka_unit_test_setup_teardown(test_goal_fuzzy_logic_reuses_child_config,
                                        goal_move_setup,
                                        goal_move_teardown),
        cmocka_unit_test_setup_teardown(test_goal_item_scoring_uses_undecided_fuzzy_weights,
                                        goal_move_setup,
                                        goal_move_teardown),
        cmocka_unit_test_setup_teardown(test_goal_retail_exports_and_avoid_sync,
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
        cmocka_unit_test_setup_teardown(test_dm_enemy_selection_filters_invisible_and_chat,
                                        goal_move_setup,
                                        goal_move_teardown),
        cmocka_unit_test_setup_teardown(test_dm_enemy_selection_damage_alert,
                                        goal_move_setup,
                                        goal_move_teardown),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}

