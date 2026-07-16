#include <stdarg.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <cmocka.h>

#include <math.h>

#ifndef cmocka_skip
#define cmocka_skip(...) skip()
#endif

#ifdef _WIN32
#include <direct.h>
#define chdir _chdir
#define getcwd _getcwd
#define mkdir(path, mode) _mkdir(path)
#define rmdir _rmdir
#define unlink _unlink
#else
#include <unistd.h>
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#include "botlib/aas/aas_local.h"
#include "botlib/aas/aas_map.h"
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
    bot_export_extended_t *exports;
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
static int g_trace_log_count;
static vec3_t g_trace_log_last_end;
static int g_trace_log_last_passent;
static int g_trace_log_last_contentmask;
static float g_trace_log_fraction = 1.0f;

static void test_reset_bot_input_log(void)
{
    memset(&g_bot_input_log, 0, sizeof(g_bot_input_log));
}

/*
=============
test_reset_trace_log
=============
*/
static void test_reset_trace_log(void)
{
	g_trace_log_count = 0;
	VectorClear(g_trace_log_last_end);
	g_trace_log_last_passent = 0;
	g_trace_log_last_contentmask = 0;
	g_trace_log_fraction = 1.0f;
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
    (void)mins;
    (void)maxs;
	if (end != NULL)
	{
		VectorCopy(end, g_trace_log_last_end);
	}
	g_trace_log_last_passent = passent;
	g_trace_log_last_contentmask = contentmask;
	g_trace_log_count++;
    bsp_trace_t trace;
    memset(&trace, 0, sizeof(trace));
	trace.fraction = g_trace_log_fraction;
	if (start != NULL && end != NULL)
	{
		for (int i = 0; i < 3; ++i)
		{
			trace.endpos[i] = start[i] + (end[i] - start[i]) * g_trace_log_fraction;
		}
	}
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

static bot_import_extended_t g_test_bot_import = {
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
	test_reset_trace_log();
	BotGoal_SetCurrentTime(0.0f);

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

    env->exports = GetBotAPIEx(&g_test_bot_import, sizeof(g_test_bot_import));
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
    assert_true(status);
    env->client_active = true;
}

/*
=============
write_goal_text_fixture

Writes a temporary text fixture for exported goal wiring checks.
=============
*/
static void write_goal_text_fixture(const char *path, const char *contents)
{
	FILE *file = fopen(path, "wb");
	assert_non_null(file);
	assert_true(fputs(contents, file) >= 0);
	assert_int_equal(fclose(file), 0);
}

/*
=============
write_goal_weight_fixture

Writes a temporary item-weight script for exported goal wiring checks.
=============
*/
static void write_goal_weight_fixture(const char *path, const char *contents)
{
	write_goal_text_fixture(path, contents);
}

/*
=============
write_goal_bsp_entity_fixture

Writes a minimal Quake II BSP shell containing only an entity lump.
=============
*/
static void write_goal_bsp_entity_fixture(const char *path, const char *entity_lump)
{
	assert_non_null(path);
	assert_non_null(entity_lump);

	size_t entity_lump_length = strlen(entity_lump);
	assert_true(entity_lump_length <= INT32_MAX);

	q2_bsp_header_t header;
	memset(&header, 0, sizeof(header));
	header.ident = Q2_BSP_IDENT;
	header.version = Q2_BSP_VERSION;
	header.lumps[Q2_BSP_LUMP_ENTITIES].offset = (int32_t)sizeof(header);
	header.lumps[Q2_BSP_LUMP_ENTITIES].length = (int32_t)entity_lump_length;

	FILE *file = fopen(path, "wb");
	assert_non_null(file);
	assert_int_equal((int)fwrite(&header, sizeof(header), 1U, file), 1);
	assert_int_equal((int)fwrite(entity_lump, 1U, entity_lump_length, file),
		(int)entity_lump_length);
	assert_int_equal(fclose(file), 0);
}

/*
=============
ensure_goal_fixture_directory
=============
*/
static bool ensure_goal_fixture_directory(const char *path)
{
	assert_non_null(path);

	if (mkdir(path, 0777) == 0)
	{
		return true;
	}

	assert_int_equal(errno, EEXIST);
	return false;
}

static void test_setup_allocates_goal_move_states(void **state)
{
    test_environment_t *env = (test_environment_t *)(*state);
    activate_test_client(env);

    bot_client_state_t *slot = BotState_Get(0);
    assert_non_null(slot);
    assert_true(slot->active);
	assert_int_equal(slot->client_number, 0);
	assert_int_equal(slot->entity_number, 1);
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
	bot_goal_t top;
	memset(&top, 0, sizeof(top));
	bot_goal_t second;
	memset(&second, 0, sizeof(second));

	assert_false(env->exports->BotGetTopGoal(handle, &top));
	assert_false(env->exports->BotGetSecondGoal(handle, &second));

	goal.number = 701;
	goal.areanum = 1;
	assert_true(env->exports->BotPushGoal(handle, &goal));
	assert_true(env->exports->BotGetTopGoal(handle, &top));
	assert_int_equal(top.number, 701);
	assert_false(env->exports->BotGetSecondGoal(handle, &second));
	assert_true(env->exports->BotPopGoal(handle));
	assert_false(env->exports->BotGetTopGoal(handle, &top));

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

	memset(&top, 0, sizeof(top));
	assert_true(env->exports->BotGetTopGoal(handle, &top));
	assert_int_equal(top.number, 700 + BOT_GOAL_MAX_STACK - 1);

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
test_goal_itemconfig_enforces_max_iteminfo

Pins sub_1002ed20's dynamic max_iteminfo table: the item after the configured
limit is a source error, the partial table is discarded, and goal setup fails.
=============
*/
static void test_goal_itemconfig_enforces_max_iteminfo(void **state)
{
	test_environment_t *env = (test_environment_t *)(*state);
	char fixture_path[PATH_MAX];
	int written = snprintf(fixture_path, sizeof(fixture_path),
		"%s/tests/support/assets/goal_max_iteminfo_tmp.c", PROJECT_SOURCE_DIR);
	assert_true(written > 0 && written < (int)sizeof(fixture_path));

	const char *fixture =
		"iteminfo \"goal_limit_first\"\n"
		"{\n"
		"name \"First\"\n"
		"}\n"
		"iteminfo \"goal_limit_second\"\n"
		"{\n"
		"name \"Second\"\n"
		"}\n";
	write_goal_text_fixture(fixture_path, fixture);

	assert_int_equal(env->exports->BotLibVarSet("max_iteminfo", "1"), BLERR_NOERROR);
	LibVarSet("max_iteminfo", "1");
	assert_int_equal(env->exports->BotLibVarSet("itemconfig", fixture_path),
		BLERR_NOERROR);
	LibVarSet("itemconfig", fixture_path);
	test_reset_log();

	assert_int_equal(BotSetupGoalAI(), BLERR_CANNOTLOADITEMCONFIG);
	bool reported_capacity = false;
	for (int i = 0; i < g_test_log.count; ++i)
	{
		if (strstr(g_test_log.entries[i].text,
			"more than 1 item info defined") != NULL)
		{
			reported_capacity = true;
			break;
		}
	}
	assert_true(reported_capacity);

	assert_int_equal(env->exports->BotLibVarSet("max_iteminfo", "256"), BLERR_NOERROR);
	LibVarSet("max_iteminfo", "256");
	assert_int_equal(env->exports->BotLibVarSet("itemconfig", "items.c"), BLERR_NOERROR);
	LibVarSet("itemconfig", "items.c");
	assert_int_equal(BotSetupGoalAI(), BLERR_NOERROR);
	assert_int_equal(remove(fixture_path), 0);
}

/*
=============
test_goal_item_weights_use_all_itemconfig_entries

Pins sub_1002f100's table construction: fuzzy-weight indices cover every
loaded iteminfo definition, including items absent from the current level.
=============
*/
static void test_goal_item_weights_use_all_itemconfig_entries(void **state)
{
	test_environment_t *env = (test_environment_t *)(*state);
	activate_test_client(env);

	bot_client_state_t *slot = BotState_Get(0);
	assert_non_null(slot);
	assert_true(slot->goal_handle > 0);

	char itemconfig_path[PATH_MAX];
	char weight_path[PATH_MAX];
	int itemconfig_written = snprintf(itemconfig_path,
		sizeof(itemconfig_path),
		"%s/tests/support/assets/goal_iteminfo_weights_tmp.c",
		PROJECT_SOURCE_DIR);
	int weight_written = snprintf(weight_path,
		sizeof(weight_path),
		"%s/tests/support/assets/goal_iteminfo_weights_tmp.w",
		PROJECT_SOURCE_DIR);
	assert_true(itemconfig_written > 0 && itemconfig_written < (int)sizeof(itemconfig_path));
	assert_true(weight_written > 0 && weight_written < (int)sizeof(weight_path));

	write_goal_text_fixture(itemconfig_path,
		"iteminfo \"goal_weight_present\"\n"
		"{\n"
		"name \"Present\"\n"
		"}\n"
		"iteminfo \"goal_weight_missing\"\n"
		"{\n"
		"name \"Missing\"\n"
		"}\n");
	write_goal_weight_fixture(weight_path,
		"weight \"goal_weight_present\"\n"
		"{\n"
		"return balance(10,10,10);\n"
		"}\n");

	assert_int_equal(env->exports->BotLibVarSet("itemconfig", itemconfig_path),
		BLERR_NOERROR);
	LibVarSet("itemconfig", itemconfig_path);
	assert_int_equal(BotSetupGoalAI(), BLERR_NOERROR);
	test_reset_log();
	assert_int_equal(env->exports->BotLoadItemWeights(slot->goal_handle, weight_path), BLERR_NOERROR);

	const bot_goalstate_t *goal_state = BotGoalStatePeek(slot->goal_handle);
	assert_non_null(goal_state);
	assert_non_null(goal_state->itemweightconfig);
	assert_int_equal(goal_state->itemweightconfig->num_weights, 1);
	assert_string_equal(goal_state->itemweightconfig->weights[0].name,
		"goal_weight_present");
	assert_int_equal(goal_state->itemweightcount, 2);
	assert_true(BotGoal_ItemWeightIndexByteSize(slot->goal_handle) >= 2 * sizeof(int));
	assert_true(env->exports->BotWeightIndex(slot->goal_handle, "goal_weight_present") >= 0);
	assert_int_equal(env->exports->BotWeightIndex(slot->goal_handle, "goal_weight_missing"), -1);
	bool reported_missing_weight = false;
	for (int i = 0; i < g_test_log.count; ++i)
	{
		if (strcmp(g_test_log.entries[i].text,
			"item info 1 \"goal_weight_missing\" has no fuzzy weight\n") == 0)
		{
			reported_missing_weight = true;
			break;
		}
	}
	assert_true(reported_missing_weight);

	assert_int_equal(env->exports->BotLibVarSet("itemconfig", "items.c"), BLERR_NOERROR);
	LibVarSet("itemconfig", "items.c");
	assert_int_equal(BotSetupGoalAI(), BLERR_NOERROR);
	assert_int_equal(remove(itemconfig_path), 0);
	assert_int_equal(remove(weight_path), 0);
}

/*
=============
test_goal_level_item_pool_honors_max_levelitems

Pins sub_1002f1a0/sub_1002f270's dynamic level-item pool and its exhaustion
diagnostic rather than relying on the reconstruction's former fixed 512 slots.
=============
*/
static void test_goal_level_item_pool_honors_max_levelitems(void **state)
{
	test_environment_t *env = (test_environment_t *)(*state);
	assert_int_equal(env->exports->BotLibVarSet("max_levelitems", "1"), BLERR_NOERROR);
	LibVarSet("max_levelitems", "1");
	assert_int_equal(BotSetupGoalAI(), BLERR_NOERROR);

	bot_levelitem_setup_t setup;
	memset(&setup, 0, sizeof(setup));
	setup.classname = "weapon_rocketlauncher";
	setup.goal.flags = GFL_ITEM;
	setup.flags = GFL_ITEM;
	setup.goal.number = 721;
	assert_int_equal(env->exports->BotRegisterLevelItem(&setup), 721);

	setup.goal.number = 722;
	test_reset_log();
	assert_int_equal(env->exports->BotRegisterLevelItem(&setup), 0);
	assert_true(g_test_log.count > 0);
	assert_int_equal(g_test_log.entries[0].priority, PRT_FATAL);
	assert_string_equal(g_test_log.entries[0].text, "out of level items\n");

	BotGoal_UnregisterLevelItem(721);
	assert_int_equal(env->exports->BotRegisterLevelItem(&setup), 722);

	assert_int_equal(env->exports->BotLibVarSet("max_levelitems", "512"), BLERR_NOERROR);
	LibVarSet("max_levelitems", "512");
	assert_int_equal(BotSetupGoalAI(), BLERR_NOERROR);
}

/*
=============
test_goal_item_weights_cache_uses_caller_filename

Pins the exported goal-item path into the Q3 weight cache: retained configs
must be found by the caller's script name before filesystem resolution.
=============
*/
static void test_goal_item_weights_cache_uses_caller_filename(void **state)
{
	test_environment_t *env = (test_environment_t *)(*state);

	char fixture_root[PATH_MAX];
	int written = snprintf(fixture_root,
		sizeof(fixture_root),
		"%s/tests/support/assets",
		PROJECT_SOURCE_DIR);
	assert_true(written > 0 && written < (int)sizeof(fixture_root));

	char fixture_path[PATH_MAX];
	written = snprintf(fixture_path,
		sizeof(fixture_path),
		"%s/tests/support/assets/bots/goal_cache_retained_tmp.w",
		PROJECT_SOURCE_DIR);
	assert_true(written > 0 && written < (int)sizeof(fixture_path));

	const char *fixture =
		"weight \"weapon_rocketlauncher\"\n"
		"{\n"
		"return 17;\n"
		"}\n";
	write_goal_weight_fixture(fixture_path, fixture);

	assert_int_equal(env->exports->BotLibVarSet("gladiator_asset_dir", fixture_root), BLERR_NOERROR);
	LibVarSet("gladiator_asset_dir", fixture_root);
	assert_int_equal(env->exports->BotLibVarSet("bot_reloadcharacters", "0"), BLERR_NOERROR);
	LibVarSet("bot_reloadcharacters", "0");

	int handle = env->exports->BotAllocGoalState(6);
	assert_true(handle > 0);

	assert_int_equal(env->exports->BotLoadItemWeights(handle, "bots/goal_cache_retained_tmp.w"),
		BLERR_NOERROR);
	const bot_goalstate_t *goal_state = BotGoalStatePeek(handle);
	assert_non_null(goal_state);
	assert_non_null(goal_state->itemweightconfig);
	const bot_weight_config_t *first_config = goal_state->itemweightconfig;

	assert_int_equal(remove(fixture_path), 0);

	assert_int_equal(env->exports->BotLoadItemWeights(handle, "bots/goal_cache_retained_tmp.w"),
		BLERR_NOERROR);
	goal_state = BotGoalStatePeek(handle);
	assert_non_null(goal_state);
	assert_ptr_equal(goal_state->itemweightconfig, first_config);

	assert_int_equal(env->exports->BotLibVarSet("bot_reloadcharacters", "1"), BLERR_NOERROR);
	LibVarSet("bot_reloadcharacters", "1");
	env->exports->BotFreeGoalState(handle);
	assert_int_equal(env->exports->BotLibVarSet("gladiator_asset_dir", ""), BLERR_NOERROR);
	LibVarSet("gladiator_asset_dir", "");
	assert_int_equal(env->exports->BotLibVarSet("bot_reloadcharacters", "0"), BLERR_NOERROR);
	LibVarSet("bot_reloadcharacters", "0");
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

Exercises the in-repo extended goal helpers wired through GetBotAPIEx and
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

	BotGoal_SetCurrentTime(1.0f);
	env->exports->BotSetAvoidGoalTime(slot->goal_handle, 77, 5.0f);
	assert_float_equal(env->exports->BotAvoidGoalTime(slot->goal_handle, 77), 5.0f, 0.0001f);

	ai_avoid_list_t *avoid = AI_GoalState_GetAvoidList(slot->goal_state);
	assert_non_null(avoid);
	assert_true(AI_AvoidList_Add(avoid, 77, 3.0f));

	AI_GoalBotlib_SynchroniseAvoid(slot->goal_handle, slot->goal_state, 2.0f);
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

/*
=============
test_goal_negative_avoid_time_uses_respawn_for_dropped_items

Pins the public BotSetAvoidGoalTime negative-time path: it derives from the
level item's respawn/default/minimum rules, not the chosen-dropped-item avoid
shortcut.
=============
*/
static void test_goal_negative_avoid_time_uses_respawn_for_dropped_items(void **state)
{
	test_environment_t *env = (test_environment_t *)(*state);
	activate_test_client(env);

	bot_client_state_t *slot = BotState_Get(0);
	assert_non_null(slot);

	BotGoal_SetCurrentTime(1.0f);
	env->exports->BotResetAvoidGoals(slot->goal_handle);

	bot_levelitem_setup_t setup;
	memset(&setup, 0, sizeof(setup));
	setup.classname = "goal_dropped_negative_avoid_item";
	setup.goal.number = 308;
	setup.goal.entitynum = 308;
	setup.goal.areanum = 1;
	setup.flags = GFL_ITEM | GFL_DROPPED;
	setup.respawntime = 45.0f;
	VectorSet(setup.goal.origin, 48.0f, 0.0f, 16.0f);
	VectorSet(setup.goal.mins, -16.0f, -16.0f, -16.0f);
	VectorSet(setup.goal.maxs, 16.0f, 16.0f, 16.0f);

	assert_int_equal(env->exports->BotRegisterLevelItem(&setup), 308);
	env->exports->BotSetAvoidGoalTime(slot->goal_handle, 308, -1.0f);
	assert_float_equal(env->exports->BotAvoidGoalTime(slot->goal_handle, 308),
		45.0f,
		0.0001f);
}

/*
=============
test_goal_avoid_goals_use_retail_fixed_slots

Pins Gladiator's fixed 64-slot avoid table: active entries are not compacted or
evicted, duplicate active inserts do not refresh an earlier slot, and expired
slots are reused.
=============
*/
static void test_goal_avoid_goals_use_retail_fixed_slots(void **state)
{
	test_environment_t *env = (test_environment_t *)(*state);
	activate_test_client(env);

	bot_client_state_t *slot = BotState_Get(0);
	assert_non_null(slot);

	BotGoal_SetCurrentTime(1.0f);
	env->exports->BotResetAvoidGoals(slot->goal_handle);
	for (int i = 0; i < BOT_GOAL_MAX_AVOID; ++i)
	{
		env->exports->BotSetAvoidGoalTime(slot->goal_handle, 1000 + i, 30.0f);
	}

	assert_float_equal(env->exports->BotAvoidGoalTime(slot->goal_handle, 1000), 30.0f, 0.0001f);
	env->exports->BotSetAvoidGoalTime(slot->goal_handle, 9000, 30.0f);
	assert_float_equal(env->exports->BotAvoidGoalTime(slot->goal_handle, 9000), 0.0f, 0.0001f);
	assert_float_equal(env->exports->BotAvoidGoalTime(slot->goal_handle, 1000), 30.0f, 0.0001f);

	env->exports->BotSetAvoidGoalTime(slot->goal_handle, 1000, 60.0f);
	assert_float_equal(env->exports->BotAvoidGoalTime(slot->goal_handle, 1000), 30.0f, 0.0001f);

	BotGoal_SetCurrentTime(32.0f);
	assert_float_equal(env->exports->BotAvoidGoalTime(slot->goal_handle, 1000), 0.0f, 0.0001f);
	env->exports->BotSetAvoidGoalTime(slot->goal_handle, 9000, 5.0f);
	assert_float_equal(env->exports->BotAvoidGoalTime(slot->goal_handle, 9000), 5.0f, 0.0001f);
}

/*
=============
test_goal_level_item_goal_uses_retail_index_cursor

Pins the Gladiator cursor contract: level items are scanned newest-first, the
index argument is only a lower bound, and an exhausted classname search
returns -1.
=============
*/
static void test_goal_level_item_goal_uses_retail_index_cursor(void **state)
{
	test_environment_t *env = (test_environment_t *)(*state);

	bot_levelitem_setup_t first_setup;
	memset(&first_setup, 0, sizeof(first_setup));
	first_setup.classname = "goal_cursor_item";
	first_setup.goal.number = 310;
	first_setup.goal.entitynum = 310;
	first_setup.goal.areanum = 1;
	first_setup.flags = GFL_ITEM;
	VectorSet(first_setup.goal.origin, 24.0f, 0.0f, 16.0f);
	VectorSet(first_setup.goal.mins, -16.0f, -16.0f, -16.0f);
	VectorSet(first_setup.goal.maxs, 16.0f, 16.0f, 16.0f);

	bot_levelitem_setup_t second_setup;
	memset(&second_setup, 0, sizeof(second_setup));
	second_setup.classname = "goal_cursor_item";
	second_setup.goal.number = 312;
	second_setup.goal.entitynum = 312;
	second_setup.goal.areanum = 1;
	second_setup.flags = GFL_ITEM;
	VectorSet(second_setup.goal.origin, 48.0f, 0.0f, 16.0f);
	VectorSet(second_setup.goal.mins, -16.0f, -16.0f, -16.0f);
	VectorSet(second_setup.goal.maxs, 16.0f, 16.0f, 16.0f);

	assert_int_equal(env->exports->BotRegisterLevelItem(&first_setup), 310);
	assert_int_equal(env->exports->BotRegisterLevelItem(&second_setup), 312);

	bot_goal_t goal;
	memset(&goal, 0, sizeof(goal));
	assert_int_equal(env->exports->BotGetLevelItemGoal(0, "goal_cursor_item", &goal), 312);
	assert_int_equal(goal.number, 312);

	memset(&goal, 0, sizeof(goal));
	assert_int_equal(env->exports->BotGetLevelItemGoal(311, "goal_cursor_item", &goal), 312);
	assert_int_equal(goal.number, 312);

	memset(&goal, 0, sizeof(goal));
	assert_int_equal(env->exports->BotGetLevelItemGoal(312, "goal_cursor_item", &goal), -1);
	assert_int_equal(env->exports->BotGetLevelItemGoal(0, "missing_goal_cursor_item", &goal), -1);
}

/*
=============
test_goal_level_item_goal_preserves_retail_tail_fields

Pins sub_1002f890's partial goal write: level-item lookup leaves the caller's
flags and iteminfo fields untouched, and does not apply item-selection
eligibility filters.
=============
*/
static void test_goal_level_item_goal_preserves_retail_tail_fields(void **state)
{
	test_environment_t *env = (test_environment_t *)(*state);

	bot_levelitem_setup_t roam_setup;
	memset(&roam_setup, 0, sizeof(roam_setup));
	roam_setup.classname = "goal_public_roam_item";
	roam_setup.goal.number = 313;
	roam_setup.goal.entitynum = 0;
	roam_setup.goal.areanum = 1;
	roam_setup.goal.flags = GFL_ITEM | GFL_ROAM;
	roam_setup.flags = GFL_ITEM | GFL_ROAM;
	roam_setup.itemflags = 1;
	VectorSet(roam_setup.goal.origin, 24.0f, 0.0f, 16.0f);
	VectorSet(roam_setup.goal.mins, -12.0f, -12.0f, -12.0f);
	VectorSet(roam_setup.goal.maxs, 12.0f, 12.0f, 12.0f);

	bot_levelitem_setup_t dropped_setup;
	memset(&dropped_setup, 0, sizeof(dropped_setup));
	dropped_setup.classname = "goal_public_dropped_item";
	dropped_setup.goal.number = 314;
	dropped_setup.goal.entitynum = 314;
	dropped_setup.goal.areanum = 1;
	dropped_setup.goal.flags = GFL_ITEM | GFL_DROPPED;
	dropped_setup.flags = GFL_ITEM | GFL_DROPPED;
	dropped_setup.respawntime = 30.0f;
	VectorSet(dropped_setup.goal.origin, 48.0f, 0.0f, 16.0f);
	VectorSet(dropped_setup.goal.mins, -16.0f, -16.0f, -16.0f);
	VectorSet(dropped_setup.goal.maxs, 16.0f, 16.0f, 16.0f);

	assert_int_equal(env->exports->BotRegisterLevelItem(&roam_setup), 313);
	assert_int_equal(env->exports->BotRegisterLevelItem(&dropped_setup), 314);

	bot_goal_t goal;
	memset(&goal, 0, sizeof(goal));
	goal.flags = 0x13579;
	goal.iteminfo = -71;
	assert_int_equal(env->exports->BotGetLevelItemGoal(0, "goal_public_roam_item", &goal), 313);
	assert_int_equal(goal.number, 313);
	assert_int_equal(goal.flags, 0x13579);
	assert_int_equal(goal.iteminfo, -71);

	memset(&goal, 0, sizeof(goal));
	goal.flags = 0x2468a;
	goal.iteminfo = 91;
	assert_int_equal(env->exports->BotGetLevelItemGoal(0, "goal_public_dropped_item", &goal), 314);
	assert_int_equal(goal.number, 314);
	assert_int_equal(goal.flags, 0x2468a);
	assert_int_equal(goal.iteminfo, 91);
}

/*
=============
test_goal_touching_uses_presence_bounds

Pins the retail touching contract: the bot origin is tested against goal bounds
expanded by the normal player presence box, not only the raw item bounds.
=============
*/
static void test_goal_touching_uses_presence_bounds(void **state)
{
	test_environment_t *env = (test_environment_t *)(*state);

	bot_goal_t goal;
	memset(&goal, 0, sizeof(goal));
	VectorSet(goal.origin, 100.0f, 100.0f, 16.0f);
	VectorSet(goal.mins, -8.0f, -8.0f, -8.0f);
	VectorSet(goal.maxs, 8.0f, 8.0f, 8.0f);

	vec3_t origin;
	VectorSet(origin, 122.0f, 100.0f, 16.0f);
	assert_true(env->exports->BotTouchingGoal(origin, &goal));

	VectorSet(origin, 124.0f, 100.0f, 16.0f);
	assert_false(env->exports->BotTouchingGoal(origin, &goal));

	VectorSet(origin, 100.0f, 100.0f, 48.0f);
	assert_true(env->exports->BotTouchingGoal(origin, &goal));

	VectorSet(origin, 100.0f, 100.0f, 49.0f);
	assert_false(env->exports->BotTouchingGoal(origin, &goal));
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

	/* These tests stage direct LTGs; only the registered backing item is an item. */
	bot_goal_t stack_goal = setup.goal;
	stack_goal.flags = 0;
    int pushed = env->exports->BotPushGoal(slot->goal_handle, &stack_goal);
    assert_int_not_equal(pushed, 0);
}

static void submit_client_update(bot_export_extended_t *exports,
                                 float frame_time,
                                 const vec3_t origin,
                                 const vec3_t viewangles)
{
    assert_non_null(exports);
    bot_updateclient_t update;
    memset(&update, 0, sizeof(update));
	/* BotAI treats a zero health snapshot as the retail dead-state boundary. */
	update.stats[STAT_HEALTH] = 100;

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

static void submit_enemy_entity(bot_export_extended_t *exports,
	int ent,
	const vec3_t origin)
{
    bot_updateentity_t enemy;
    memset(&enemy, 0, sizeof(enemy));
    VectorCopy(origin, enemy.origin);
    int status = exports->BotUpdateEntity(ent, &enemy);
    assert_int_equal(status, BLERR_NOERROR);
}

typedef struct test_goal_aas_fixture_s
{
	aas_world_t saved_world;
	aas_area_t *areas;
	aas_areasettings_t *area_settings;
	aas_entity_t *entities;
} test_goal_aas_fixture_t;

/*
=============
test_goal_aas_fixture_begin
=============
*/
static void test_goal_aas_fixture_begin(test_goal_aas_fixture_t *fixture, int max_entities)
{
	assert_non_null(fixture);

	fixture->saved_world = aasworld;
	memset(&aasworld, 0, sizeof(aasworld));
	aasworld.loaded = qtrue;
	aasworld.initialized = qtrue;
	aasworld.time = 0.0f;
	aasworld.numAreas = 2; /* retail counts the dummy zero area: one real area */
	aasworld.numAreaSettings = 2;
	aasworld.maxEntities = max_entities;

	fixture->areas = (aas_area_t *)calloc(2, sizeof(aas_area_t));
	fixture->area_settings = (aas_areasettings_t *)calloc(2, sizeof(aas_areasettings_t));
	fixture->entities = (aas_entity_t *)calloc((size_t)max_entities, sizeof(aas_entity_t));
	assert_non_null(fixture->areas);
	assert_non_null(fixture->area_settings);
	assert_non_null(fixture->entities);

	aasworld.areas = fixture->areas;
	aasworld.areasettings = fixture->area_settings;
	aasworld.entities = fixture->entities;
	VectorSet(aasworld.areas[1].mins, -128.0f, -128.0f, -64.0f);
	VectorSet(aasworld.areas[1].maxs, 128.0f, 128.0f, 128.0f);
	aasworld.areasettings[1].numreachableareas = 1;
	TranslateEntity_SetWorldLoaded(qtrue);
}

/*
=============
test_goal_aas_fixture_end
=============
*/
static void test_goal_aas_fixture_end(test_goal_aas_fixture_t *fixture)
{
	assert_non_null(fixture);

	free(fixture->areas);
	free(fixture->area_settings);
	free(fixture->entities);
	aasworld = fixture->saved_world;
	TranslateEntity_SetWorldLoaded(aasworld.loaded);
}

/*
=============
test_goal_choose_uses_timeout_for_dropped_semantics

Pins the selected-item contract: dropped handling is keyed off the temporary
level-item timeout, not merely a stored flag bit.
=============
*/
static void test_goal_choose_uses_timeout_for_dropped_semantics(void **state)
{
	test_environment_t *env = (test_environment_t *)(*state);
	activate_test_client(env);

	bot_client_state_t *slot = BotState_Get(0);
	assert_non_null(slot);
	assert_true(slot->goal_handle > 0);

	test_goal_aas_fixture_t fixture;
	test_goal_aas_fixture_begin(&fixture, 4);
	BotGoal_SetCurrentTime(1.0f);

	char fixture_path[PATH_MAX];
	int written = snprintf(fixture_path,
		sizeof(fixture_path),
		"%s/tests/support/assets/bots/goal_timeout_dropped_tmp.w",
		PROJECT_SOURCE_DIR);
	assert_true(written > 0 && written < (int)sizeof(fixture_path));

	const char *weights =
		"weight \"goal_timeout_dropped_item\"\n"
		"{\n"
		"return balance(100,100,100);\n"
		"}\n";
	write_goal_weight_fixture(fixture_path, weights);

	bot_levelitem_setup_t setup;
	memset(&setup, 0, sizeof(setup));
	setup.classname = "goal_timeout_dropped_item";
	setup.goal.number = 315;
	setup.goal.entitynum = 315;
	setup.goal.areanum = 1;
	setup.goal.flags = GFL_ITEM | GFL_DROPPED;
	setup.flags = GFL_ITEM | GFL_DROPPED;
	VectorSet(setup.goal.origin, 32.0f, 0.0f, 16.0f);
	VectorSet(setup.goal.mins, -16.0f, -16.0f, -16.0f);
	VectorSet(setup.goal.maxs, 16.0f, 16.0f, 16.0f);

	assert_int_equal(env->exports->BotRegisterLevelItem(&setup), 315);
	assert_int_equal(env->exports->BotLoadItemWeights(slot->goal_handle, fixture_path), BLERR_NOERROR);

	int inventory[MAX_ITEMS];
	memset(inventory, 0, sizeof(inventory));

	vec3_t origin;
	VectorSet(origin, 0.0f, 0.0f, 0.0f);
	assert_int_equal(env->exports->BotChooseLTGItem(slot->goal_handle,
		origin,
		inventory,
		TFL_DEFAULT),
		1);

	bot_goal_t chosen;
	memset(&chosen, 0, sizeof(chosen));
	assert_true(env->exports->BotGetTopGoal(slot->goal_handle, &chosen));
	assert_int_equal(chosen.number, 315);
	assert_int_equal(chosen.flags, GFL_ITEM);
	assert_float_equal(env->exports->BotAvoidGoalTime(slot->goal_handle, 315),
		30.0f,
		0.0001f);

	test_goal_aas_fixture_end(&fixture);
	assert_int_equal(remove(fixture_path), 0);
}

/*
=============
test_goal_item_vis_trace_targets_retail_mins_point

Pins the retail visibility target for item goals: Gladiator/Q3 trace to
goal->origin + goal->mins, not the bbox center.
=============
*/
static void test_goal_item_vis_trace_targets_retail_mins_point(void **state)
{
	test_environment_t *env = (test_environment_t *)(*state);

	test_goal_aas_fixture_t fixture;
	test_goal_aas_fixture_begin(&fixture, 4);
	aasworld.time = 8.0f;
	aasworld.entities[2].number = 2;
	aasworld.entities[2].inuse = qtrue;
	aasworld.entities[2].lastUpdateTime = 7.0f;

	bot_goal_t goal;
	memset(&goal, 0, sizeof(goal));
	goal.flags = GFL_ITEM;
	goal.entitynum = 2;
	VectorSet(goal.origin, 64.0f, 32.0f, 16.0f);
	VectorSet(goal.mins, -16.0f, -12.0f, -8.0f);
	VectorSet(goal.maxs, 32.0f, 24.0f, 40.0f);

	vec3_t eye;
	vec3_t viewangles;
	VectorSet(eye, 0.0f, 0.0f, 32.0f);
	VectorClear(viewangles);
	test_reset_trace_log();

	assert_true(env->exports->BotItemGoalInVisButNotVisible(1, eye, viewangles, &goal));
	assert_int_equal(g_trace_log_count, 1);
	assert_int_equal(g_trace_log_last_passent, 1);
	assert_int_equal(g_trace_log_last_contentmask, CONTENTS_SOLID);
	assert_float_equal(g_trace_log_last_end[0], 48.0f, 0.0001f);
	assert_float_equal(g_trace_log_last_end[1], 20.0f, 0.0001f);
	assert_float_equal(g_trace_log_last_end[2], 8.0f, 0.0001f);

	test_goal_aas_fixture_end(&fixture);
}

/*
=============
test_goal_choose_skips_unlinked_static_items_except_roam

Pins the retail LTG selector guard that ignores static BSP item records until
entity-item refresh links them to a live entity, while separately preserving
the explicit compatibility-only roam-goal registration path.
=============
*/
static void test_goal_choose_skips_unlinked_static_items_except_roam(void **state)
{
	test_environment_t *env = (test_environment_t *)(*state);
	activate_test_client(env);

	bot_client_state_t *slot = BotState_Get(0);
	assert_non_null(slot);
	assert_true(slot->goal_handle > 0);

	test_goal_aas_fixture_t fixture;
	test_goal_aas_fixture_begin(&fixture, 4);

	char fixture_path[PATH_MAX];
	int written = snprintf(fixture_path,
		sizeof(fixture_path),
		"%s/tests/support/assets/bots/goal_unlinked_static_tmp.w",
		PROJECT_SOURCE_DIR);
	assert_true(written > 0 && written < (int)sizeof(fixture_path));

	const char *weights =
		"weight \"goal_static_entity_item\"\n"
		"{\n"
		"return balance(100,100,100);\n"
		"}\n"
		"weight \"goal_roam_static_item\"\n"
		"{\n"
		"return balance(10,10,10);\n"
		"}\n";
	write_goal_weight_fixture(fixture_path, weights);

	bot_levelitem_setup_t static_setup;
	memset(&static_setup, 0, sizeof(static_setup));
	static_setup.classname = "goal_static_entity_item";
	static_setup.goal.number = 304;
	static_setup.goal.entitynum = 0;
	static_setup.goal.areanum = 1;
	static_setup.goal.flags = GFL_ITEM;
	static_setup.flags = GFL_ITEM;
	VectorSet(static_setup.goal.origin, 16.0f, 0.0f, 16.0f);
	VectorSet(static_setup.goal.mins, -16.0f, -16.0f, -16.0f);
	VectorSet(static_setup.goal.maxs, 16.0f, 16.0f, 16.0f);

	bot_levelitem_setup_t roam_setup;
	memset(&roam_setup, 0, sizeof(roam_setup));
	roam_setup.classname = "goal_roam_static_item";
	roam_setup.goal.number = 305;
	roam_setup.goal.entitynum = 0;
	roam_setup.goal.areanum = 1;
	roam_setup.goal.flags = GFL_ITEM | GFL_ROAM;
	roam_setup.flags = GFL_ITEM | GFL_ROAM;
	roam_setup.weight = 1.0f;
	VectorSet(roam_setup.goal.origin, 32.0f, 0.0f, 16.0f);
	VectorSet(roam_setup.goal.mins, -16.0f, -16.0f, -16.0f);
	VectorSet(roam_setup.goal.maxs, 16.0f, 16.0f, 16.0f);

	assert_int_equal(env->exports->BotRegisterLevelItem(&static_setup), 304);
	assert_int_equal(env->exports->BotRegisterLevelItem(&roam_setup), 305);
	assert_int_equal(env->exports->BotLoadItemWeights(slot->goal_handle, fixture_path), BLERR_NOERROR);
	assert_true(env->exports->BotWeightIndex(slot->goal_handle, "goal_static_entity_item") >= 0);
	assert_true(env->exports->BotWeightIndex(slot->goal_handle, "goal_roam_static_item") >= 0);

	int inventory[MAX_ITEMS];
	memset(inventory, 0, sizeof(inventory));

	vec3_t origin;
	VectorSet(origin, 0.0f, 0.0f, 0.0f);

	int roam_travel = -1;
	float roam_score = BotGoal_EvaluateStackGoal(slot->goal_handle,
		&roam_setup.goal,
		origin,
		1,
		inventory,
		TFL_DEFAULT,
		&roam_travel);
	assert_int_equal(roam_travel, 1);
	assert_true(roam_score > 0.0f);

	assert_int_equal(env->exports->BotChooseLTGItem(slot->goal_handle, origin, inventory, TFL_DEFAULT), 1);

	bot_goal_t chosen;
	memset(&chosen, 0, sizeof(chosen));
	assert_true(env->exports->BotGetTopGoal(slot->goal_handle, &chosen));

	test_goal_aas_fixture_end(&fixture);
	assert_int_equal(remove(fixture_path), 0);
	assert_int_equal(chosen.number, 305);
}

/*
=============
test_goal_nbg_uses_retail_strict_maxtime

Pins the retail nearby-goal selector contract: travel time must be strictly
less than maxtime, and LTG repetition is controlled by avoid timers rather
than a separate same-goal-number filter.
=============
*/
static void test_goal_nbg_uses_retail_strict_maxtime(void **state)
{
	test_environment_t *env = (test_environment_t *)(*state);
	activate_test_client(env);

	bot_client_state_t *slot = BotState_Get(0);
	assert_non_null(slot);
	assert_true(slot->goal_handle > 0);

	test_goal_aas_fixture_t fixture;
	test_goal_aas_fixture_begin(&fixture, 4);

	char fixture_path[PATH_MAX];
	int written = snprintf(fixture_path,
		sizeof(fixture_path),
		"%s/tests/support/assets/bots/goal_nbg_maxtime_tmp.w",
		PROJECT_SOURCE_DIR);
	assert_true(written > 0 && written < (int)sizeof(fixture_path));

	const char *weights =
		"weight \"goal_nbg_maxtime_item\"\n"
		"{\n"
		"return balance(100,100,100);\n"
		"}\n";
	write_goal_weight_fixture(fixture_path, weights);

	bot_levelitem_setup_t setup;
	memset(&setup, 0, sizeof(setup));
	setup.classname = "goal_nbg_maxtime_item";
	setup.goal.number = 306;
	setup.goal.entitynum = 306;
	setup.goal.areanum = 1;
	setup.goal.flags = GFL_ITEM;
	setup.flags = GFL_ITEM;
	VectorSet(setup.goal.origin, 32.0f, 0.0f, 16.0f);
	VectorSet(setup.goal.mins, -16.0f, -16.0f, -16.0f);
	VectorSet(setup.goal.maxs, 16.0f, 16.0f, 16.0f);

	assert_int_equal(env->exports->BotRegisterLevelItem(&setup), 306);
	assert_int_equal(env->exports->BotLoadItemWeights(slot->goal_handle, fixture_path), BLERR_NOERROR);
	assert_true(env->exports->BotWeightIndex(slot->goal_handle, "goal_nbg_maxtime_item") >= 0);

	int inventory[MAX_ITEMS];
	memset(inventory, 0, sizeof(inventory));

	vec3_t origin;
	VectorSet(origin, 0.0f, 0.0f, 0.0f);

	assert_int_equal(env->exports->BotChooseNBGItem(slot->goal_handle,
		origin,
		inventory,
		TFL_DEFAULT,
		NULL,
		0.0f),
		0);

	bot_goal_t chosen;
	memset(&chosen, 0, sizeof(chosen));
	assert_false(env->exports->BotGetTopGoal(slot->goal_handle, &chosen));

	assert_int_equal(env->exports->BotChooseNBGItem(slot->goal_handle,
		origin,
		inventory,
		TFL_DEFAULT,
		NULL,
		1.0f),
		0);
	assert_false(env->exports->BotGetTopGoal(slot->goal_handle, &chosen));

	bot_goal_t ltg = setup.goal;
	assert_int_equal(env->exports->BotChooseNBGItem(slot->goal_handle,
		origin,
		inventory,
		TFL_DEFAULT,
		&ltg,
		2.0f),
		1);
	assert_true(env->exports->BotGetTopGoal(slot->goal_handle, &chosen));
	assert_int_equal(chosen.number, 306);

	test_goal_aas_fixture_end(&fixture);
	assert_int_equal(remove(fixture_path), 0);
}

/*
=============
test_goal_choose_rejects_start_area_without_reachability

Pins the retail start-area guard: a current area with no reachability links is
rejected even after a prior selection established a valid area.
=============
*/
static void test_goal_choose_rejects_start_area_without_reachability(void **state)
{
	test_environment_t *env = (test_environment_t *)(*state);
	activate_test_client(env);

	bot_client_state_t *slot = BotState_Get(0);
	assert_non_null(slot);
	assert_true(slot->goal_handle > 0);

	test_goal_aas_fixture_t fixture;
	test_goal_aas_fixture_begin(&fixture, 4);

	char fixture_path[PATH_MAX];
	int written = snprintf(fixture_path,
		sizeof(fixture_path),
		"%s/tests/support/assets/bots/goal_start_reach_tmp.w",
		PROJECT_SOURCE_DIR);
	assert_true(written > 0 && written < (int)sizeof(fixture_path));

	const char *weights =
		"weight \"goal_start_reach_item\"\n"
		"{\n"
		"return balance(100,100,100);\n"
		"}\n";
	write_goal_weight_fixture(fixture_path, weights);

	bot_levelitem_setup_t setup;
	memset(&setup, 0, sizeof(setup));
	setup.classname = "goal_start_reach_item";
	setup.goal.number = 307;
	setup.goal.entitynum = 307;
	setup.goal.areanum = 1;
	setup.goal.flags = GFL_ITEM;
	setup.flags = GFL_ITEM;
	VectorSet(setup.goal.origin, 32.0f, 0.0f, 16.0f);
	VectorSet(setup.goal.mins, -16.0f, -16.0f, -16.0f);
	VectorSet(setup.goal.maxs, 16.0f, 16.0f, 16.0f);

	assert_int_equal(env->exports->BotRegisterLevelItem(&setup), 307);
	assert_int_equal(env->exports->BotLoadItemWeights(slot->goal_handle, fixture_path), BLERR_NOERROR);

	int inventory[MAX_ITEMS];
	memset(inventory, 0, sizeof(inventory));

	vec3_t origin;
	VectorSet(origin, 0.0f, 0.0f, 0.0f);
	assert_int_equal(env->exports->BotChooseLTGItem(slot->goal_handle,
		origin,
		inventory,
		TFL_DEFAULT),
		1);
	env->exports->BotEmptyGoalStack(slot->goal_handle);
	env->exports->BotRemoveFromAvoidGoals(slot->goal_handle, setup.goal.number);
	aasworld.areasettings[1].numreachableareas = 0;

	assert_int_equal(env->exports->BotChooseLTGItem(slot->goal_handle,
		origin,
		inventory,
		TFL_DEFAULT),
		0);

	bot_goal_t chosen;
	memset(&chosen, 0, sizeof(chosen));
	assert_false(env->exports->BotGetTopGoal(slot->goal_handle, &chosen));

	test_goal_aas_fixture_end(&fixture);
	assert_int_equal(remove(fixture_path), 0);
}

/*
=============
test_goal_init_filters_notspawnflags_entities

Pins the Gladiator level-item initialization gate that skips BSP item entities
when their spawnflags overlap the notspawnflags libvar.
=============
*/
static void test_goal_init_filters_notspawnflags_entities(void **state)
{
	test_environment_t *env = (test_environment_t *)(*state);

	char fixture_root[PATH_MAX];
	int written = snprintf(fixture_root,
		sizeof(fixture_root),
		"%s/tests/support/assets",
		PROJECT_SOURCE_DIR);
	assert_true(written > 0 && written < (int)sizeof(fixture_root));

	char maps_dir[PATH_MAX];
	written = snprintf(maps_dir, sizeof(maps_dir), "%s/maps", fixture_root);
	assert_true(written > 0 && written < (int)sizeof(maps_dir));
	(void)ensure_goal_fixture_directory(maps_dir);

	char bsp_path[PATH_MAX];
	written = snprintf(bsp_path,
		sizeof(bsp_path),
		"%s/goal_spawnfilter_tmp.bsp",
		maps_dir);
	assert_true(written > 0 && written < (int)sizeof(bsp_path));

	const char *entity_lump =
		"{\n"
		"\"classname\" \"target_location\"\n"
		"\"message\" \"spawnfilter marker\"\n"
		"\"origin\" \"0 0 16\"\n"
		"}\n"
		"{\n"
		"\"classname\" \"weapon_rocketlauncher\"\n"
		"\"origin\" \"-64 0 16\"\n"
		"\"spawnflags\" \"2048\"\n"
		"}\n"
		"{\n"
		"\"classname\" \"weapon_rocketlauncher\"\n"
		"\"origin\" \"64 0 16\"\n"
		"\"spawnflags\" \"0\"\n"
		"\"notbot\" \"1\"\n"
		"}\n"
		"{\n"
		"\"classname\" \"WEAPON_ROCKETLAUNCHER\"\n"
		"\"origin\" \"96 0 16\"\n"
		"\"spawnflags\" \"0\"\n"
		"}\n";
	write_goal_bsp_entity_fixture(bsp_path, entity_lump);

	char itemconfig_path[PATH_MAX];
	written = snprintf(itemconfig_path, sizeof(itemconfig_path), "%s/items.c", fixture_root);
	assert_true(written > 0 && written < (int)sizeof(itemconfig_path));
	const char *itemconfig_fixture =
		"iteminfo \"weapon_rocketlauncher\"\n"
		"{\n"
		"name \"Rocket Launcher\"\n"
		"model \"models/weapons/g_rocket/tris.md2\"\n"
		"mins {-15, -15, -15}\n"
		"maxs {15, 15, 15}\n"
		"respawntime 30\n"
		"}\n";
	write_goal_text_fixture(itemconfig_path, itemconfig_fixture);

	char previous_cwd[PATH_MAX];
	assert_non_null(getcwd(previous_cwd, sizeof(previous_cwd)));

	test_goal_aas_fixture_t fixture;
	test_goal_aas_fixture_begin(&fixture, 8);
	snprintf(aasworld.mapName, sizeof(aasworld.mapName), "goal_spawnfilter_tmp");
	assert_int_equal(env->exports->BotLibVarSet("itemconfig", "items.c"), BLERR_NOERROR);
	LibVarSet("itemconfig", "items.c");
	LibVarSet("notspawnflags", "2048");
	char *models[8] = {0};
	models[7] = "models/weapons/g_rocket/tris.md2";
	BotGoal_SetMapModelIndexes(8, models);
	assert_int_equal(chdir(fixture_root), 0);
	BotInitLevelItems();
	assert_int_equal(chdir(previous_cwd), 0);
	aas_entity_t *entity = &aasworld.entities[3];
	entity->inuse = qtrue;
	entity->number = 3;
	entity->modelindex = 7;
	VectorSet(entity->origin, 64.0f, 0.0f, 16.0f);
	VectorCopy(entity->origin, entity->previousOrigin);
	BotUpdateEntityItems();

	bot_goal_t goal;
	memset(&goal, 0, sizeof(goal));
	int first_goal = BotGetLevelItemGoal(0, "Rocket Launcher", &goal);

	bot_goal_t next_goal;
	memset(&next_goal, 0, sizeof(next_goal));
	int next_result = BotGetLevelItemGoal(goal.number, "Rocket Launcher", &next_goal);
	bot_goal_t case_variant_goal;
	memset(&case_variant_goal, 0, sizeof(case_variant_goal));
	int case_variant_result = BotGetLevelItemGoal(0, "rocket launcher", &case_variant_goal);

	bot_goal_t location_goal;
	memset(&location_goal, 0, sizeof(location_goal));
	int location_result = BotGetMapLocationGoal("spawnfilter marker", &location_goal);

	test_goal_aas_fixture_end(&fixture);
	assert_int_equal(unlink(bsp_path), 0);
	assert_int_equal(unlink(itemconfig_path), 0);
	(void)rmdir(maps_dir);

	assert_int_equal(first_goal, 1);
	assert_int_equal(goal.entitynum, 3);
	assert_int_equal(goal.areanum, 1);
	assert_float_equal(goal.origin[0], 64.0f, 0.0001f);
	assert_int_equal(next_result, -1);
	assert_int_equal(case_variant_result, -1);
	assert_int_equal(location_result, 1);
}

/*
=============
test_goal_raw_dropped_item_selection

Pins the Gladiator LTG/NBG dropped-item score bonus and selected-goal shape:
raw timed items gain twenty score points, the pushed goal remains GFL_ITEM,
and only an exactly-zero respawn time receives the 30-second avoid fallback.
=============
*/
static void test_goal_raw_dropped_item_selection(void **state)
{
	test_environment_t *env = (test_environment_t *)(*state);
	activate_test_client(env);

	bot_client_state_t *slot = BotState_Get(0);
	assert_non_null(slot);
	assert_true(slot->goal_handle > 0);

	char fixture_root[PATH_MAX];
	int written = snprintf(fixture_root,
		sizeof(fixture_root),
		"%s/tests/support/assets",
		PROJECT_SOURCE_DIR);
	assert_true(written > 0 && written < (int)sizeof(fixture_root));

	char maps_dir[PATH_MAX];
	written = snprintf(maps_dir, sizeof(maps_dir), "%s/maps", fixture_root);
	assert_true(written > 0 && written < (int)sizeof(maps_dir));
	(void)ensure_goal_fixture_directory(maps_dir);

	char bsp_path[PATH_MAX];
	written = snprintf(bsp_path, sizeof(bsp_path), "%s/goal_dropped_score_tmp.bsp", maps_dir);
	assert_true(written > 0 && written < (int)sizeof(bsp_path));
	write_goal_bsp_entity_fixture(bsp_path,
		"{\n"
		"\"classname\" \"weapon_shotgun\"\n"
		"\"origin\" \"20 0 16\"\n"
		"}\n");

	char itemconfig_path[PATH_MAX];
	written = snprintf(itemconfig_path,
		sizeof(itemconfig_path),
		"%s/goal_dropped_score_items_tmp.c",
		fixture_root);
	assert_true(written > 0 && written < (int)sizeof(itemconfig_path));
	write_goal_text_fixture(itemconfig_path,
		"iteminfo \"weapon_shotgun\"\n"
		"{\n"
		"name \"Shotgun\"\n"
		"model \"models/weapons/g_shotg/tris.md2\"\n"
		"mins {-15, -15, -15}\n"
		"maxs {15, 15, 15}\n"
		"respawntime 30\n"
		"}\n"
		"iteminfo \"weapon_rocketlauncher\"\n"
		"{\n"
		"name \"Rocket Launcher\"\n"
		"model \"models/weapons/g_rocket/tris.md2\"\n"
		"mins {-15, -15, -15}\n"
		"maxs {15, 15, 15}\n"
		"respawntime -4\n"
		"}\n");

	char weight_path[PATH_MAX];
	written = snprintf(weight_path,
		sizeof(weight_path),
		"%s/goal_dropped_score_tmp.w",
		fixture_root);
	assert_true(written > 0 && written < (int)sizeof(weight_path));
	write_goal_weight_fixture(weight_path,
		"weight \"weapon_shotgun\"\n"
		"{\n"
		"return balance(1,1,1);\n"
		"}\n"
		"weight \"weapon_rocketlauncher\"\n"
		"{\n"
		"return balance(0.9,0.9,0.9);\n"
		"}\n");

	char previous_cwd[PATH_MAX];
	assert_non_null(getcwd(previous_cwd, sizeof(previous_cwd)));

	test_goal_aas_fixture_t fixture;
	test_goal_aas_fixture_begin(&fixture, 8);
	snprintf(aasworld.mapName, sizeof(aasworld.mapName), "goal_dropped_score_tmp");
	assert_int_equal(env->exports->BotLibVarSet("itemconfig", itemconfig_path), BLERR_NOERROR);
	LibVarSet("itemconfig", itemconfig_path);
	char *models[16] = {0};
	models[7] = "models/weapons/g_rocket/tris.md2";
	models[8] = "models/weapons/g_shotg/tris.md2";
	BotGoal_SetMapModelIndexes(16, models);
	assert_int_equal(chdir(fixture_root), 0);
	BotInitLevelItems();
	assert_int_equal(chdir(previous_cwd), 0);

	aas_entity_t *shotgun = &aasworld.entities[3];
	shotgun->inuse = qtrue;
	shotgun->number = 3;
	shotgun->modelindex = 8;
	VectorSet(shotgun->origin, 20.0f, 0.0f, 16.0f);
	VectorCopy(shotgun->origin, shotgun->previousOrigin);

	aas_entity_t *rocket = &aasworld.entities[5];
	rocket->inuse = qtrue;
	rocket->number = 5;
	rocket->modelindex = 7;
	VectorSet(rocket->origin, 32.0f, 0.0f, 16.0f);
	VectorCopy(rocket->origin, rocket->previousOrigin);

	aasworld.time = 1.0f;
	BotGoal_SetCurrentTime(aasworld.time);
	BotUpdateEntityItems();
	assert_int_equal(env->exports->BotLoadItemWeights(slot->goal_handle, weight_path), BLERR_NOERROR);

	int inventory[MAX_ITEMS];
	memset(inventory, 0, sizeof(inventory));
	vec3_t origin;
	VectorSet(origin, 0.0f, 0.0f, 0.0f);
	assert_int_equal(env->exports->BotChooseLTGItem(slot->goal_handle,
		origin,
		inventory,
		TFL_DEFAULT),
		1);

	bot_goal_t chosen;
	memset(&chosen, 0, sizeof(chosen));
	assert_true(env->exports->BotGetTopGoal(slot->goal_handle, &chosen));

	test_goal_aas_fixture_end(&fixture);
	assert_int_equal(unlink(bsp_path), 0);
	assert_int_equal(unlink(itemconfig_path), 0);
	assert_int_equal(unlink(weight_path), 0);
	(void)rmdir(maps_dir);

	assert_int_equal(chosen.number, 6);
	assert_int_equal(chosen.flags, GFL_ITEM);
	assert_float_equal(env->exports->BotAvoidGoalTime(slot->goal_handle, 6), 0.0f, 0.0001f);
}

/*
=============
test_goal_init_drops_all_bsp_items_to_floor

Pins the retail BotInitLevelItems path that drops every parsed item—also an
item marked floating by spawnflags—to the floor before projecting the public
goal origin to its best reachable area. It also retains the known-item,
missing-origin diagnostic.
=============
*/
static void test_goal_init_drops_all_bsp_items_to_floor(void **state)
{
	test_environment_t *env = (test_environment_t *)(*state);
	activate_test_client(env);

	bot_client_state_t *slot = BotState_Get(0);
	assert_non_null(slot);
	assert_true(slot->goal_handle > 0);

	char fixture_root[PATH_MAX];
	int written = snprintf(fixture_root,
		sizeof(fixture_root),
		"%s/tests/support/assets",
		PROJECT_SOURCE_DIR);
	assert_true(written > 0 && written < (int)sizeof(fixture_root));

	char maps_dir[PATH_MAX];
	written = snprintf(maps_dir, sizeof(maps_dir), "%s/maps", fixture_root);
	assert_true(written > 0 && written < (int)sizeof(maps_dir));
	(void)ensure_goal_fixture_directory(maps_dir);

	char bsp_path[PATH_MAX];
	written = snprintf(bsp_path,
		sizeof(bsp_path),
		"%s/goal_dropfloor_tmp.bsp",
		maps_dir);
	assert_true(written > 0 && written < (int)sizeof(bsp_path));

	const char *entity_lump =
		"{\n"
		"\"classname\" \"weapon_rocketlauncher\"\n"
		"\"origin\" \"0 0 64\"\n"
		"\"spawnflags\" \"1\"\n"
		"}\n"
		"{\n"
		"\"classname\" \"weapon_rocketlauncher\"\n"
		"}\n";
	write_goal_bsp_entity_fixture(bsp_path, entity_lump);

	char itemconfig_path[PATH_MAX];
	written = snprintf(itemconfig_path, sizeof(itemconfig_path), "%s/items.c", fixture_root);
	assert_true(written > 0 && written < (int)sizeof(itemconfig_path));
	const char *itemconfig_fixture =
		"iteminfo \"weapon_rocketlauncher\"\n"
		"{\n"
		"name \"Rocket Launcher\"\n"
		"model \"models/weapons/g_rocket/tris.md2\"\n"
		"mins {-15, -15, -15}\n"
		"maxs {15, 15, 15}\n"
		"respawntime 30\n"
		"}\n";
	write_goal_text_fixture(itemconfig_path, itemconfig_fixture);

	char weight_path[PATH_MAX];
	written = snprintf(weight_path, sizeof(weight_path), "%s/goal_dropfloor_tmp.w", fixture_root);
	assert_true(written > 0 && written < (int)sizeof(weight_path));
	write_goal_weight_fixture(weight_path,
		"weight \"weapon_rocketlauncher\"\n"
		"{\n"
		"return balance(100,100,100);\n"
		"}\n");

	char previous_cwd[PATH_MAX];
	assert_non_null(getcwd(previous_cwd, sizeof(previous_cwd)));

	test_goal_aas_fixture_t fixture;
	test_goal_aas_fixture_begin(&fixture, 8);
	snprintf(aasworld.mapName, sizeof(aasworld.mapName), "goal_dropfloor_tmp");
	assert_int_equal(env->exports->BotLibVarSet("itemconfig", "items.c"), BLERR_NOERROR);
	LibVarSet("itemconfig", "items.c");
	BotGoal_SetMapModelIndexes(0, NULL);
	test_reset_trace_log();
	test_reset_log();
	g_trace_log_fraction = 0.5f;
	assert_int_equal(chdir(fixture_root), 0);
	BotInitLevelItems();
	assert_int_equal(chdir(previous_cwd), 0);
	assert_int_equal(env->exports->BotLoadItemWeights(slot->goal_handle, weight_path), BLERR_NOERROR);

	bot_goal_t goal;
	memset(&goal, 0, sizeof(goal));
	int result = BotGetLevelItemGoal(0, "Rocket Launcher", &goal);
	assert_int_equal(g_trace_log_count, 1);
	assert_int_equal(g_trace_log_last_passent, 0);
	assert_int_equal(g_trace_log_last_contentmask, CONTENTS_SOLID);
	assert_float_equal(g_trace_log_last_end[2], -36.0f, 0.0001f);

	int inventory[MAX_ITEMS];
	memset(inventory, 0, sizeof(inventory));
	vec3_t bot_origin;
	VectorSet(bot_origin, 0.0f, 0.0f, 0.0f);
	aasworld.time = 1.0f;
	BotGoal_SetCurrentTime(aasworld.time);
	test_reset_trace_log();
	assert_int_equal(env->exports->BotChooseLTGItem(slot->goal_handle,
		bot_origin,
		inventory,
		TFL_DEFAULT),
		1);
	assert_int_equal(g_trace_log_count, 1);
	assert_int_equal(g_trace_log_last_passent, 1);
	bot_goal_t selected;
	memset(&selected, 0, sizeof(selected));
	assert_true(env->exports->BotGetTopGoal(slot->goal_handle, &selected));
	assert_float_equal(env->exports->BotAvoidGoalTime(slot->goal_handle, 1), 30.0f, 0.0001f);
	env->exports->BotRemoveFromAvoidGoals(slot->goal_handle, selected.number);
	BotGoal_MarkItemTaken(selected.number, 60.0f);
	env->exports->BotEmptyGoalStack(slot->goal_handle);
	assert_int_equal(env->exports->BotChooseLTGItem(slot->goal_handle,
		bot_origin,
		inventory,
		TFL_DEFAULT),
		1);
	env->exports->BotRemoveFromAvoidGoals(slot->goal_handle, selected.number);
	env->exports->BotEmptyGoalStack(slot->goal_handle);
	bot_goal_t filler;
	memset(&filler, 0, sizeof(filler));
	filler.number = 999;
	filler.areanum = 1;
	for (int i = 1; i < BOT_GOAL_MAX_STACK; ++i)
	{
		assert_true(env->exports->BotPushGoal(slot->goal_handle, &filler));
	}
	assert_int_equal(env->exports->BotChooseLTGItem(slot->goal_handle,
		bot_origin,
		inventory,
		TFL_DEFAULT),
		1);
	assert_float_equal(env->exports->BotAvoidGoalTime(slot->goal_handle, 1), 30.0f, 0.0001f);
	bot_goal_t overflow_top;
	memset(&overflow_top, 0, sizeof(overflow_top));
	assert_true(env->exports->BotGetTopGoal(slot->goal_handle, &overflow_top));
	assert_int_equal(overflow_top.number, filler.number);

	g_trace_log_fraction = 1.0f;
	test_goal_aas_fixture_end(&fixture);
	assert_int_equal(unlink(bsp_path), 0);
	assert_int_equal(unlink(itemconfig_path), 0);
	assert_int_equal(unlink(weight_path), 0);
	(void)rmdir(maps_dir);

	assert_int_equal(result, 1);
	assert_float_equal(goal.origin[2], 14.25f, 0.0001f);
	assert_int_equal(selected.number, 1);
	assert_int_equal(selected.entitynum, 0);
	assert_int_equal(selected.flags, GFL_ITEM);
	bool reported_missing_origin = false;
	for (int i = 0; i < g_test_log.count; ++i)
	{
		if (g_test_log.entries[i].priority == PRT_ERROR
			&& strcmp(g_test_log.entries[i].text,
				"item weapon_rocketlauncher without origin\n") == 0)
		{
			reported_missing_origin = true;
			break;
		}
	}
	assert_true(reported_missing_origin);
}

/*
=============
test_goal_info_entities_use_retail_head_insertion_order

Pins the Q3 successor storage order for parsed info entities: target locations
and camp spots are pushed to the head of their lists as BSP entities are read.
=============
*/
static void test_goal_info_entities_use_retail_head_insertion_order(void **state)
{
	test_environment_t *env = (test_environment_t *)(*state);

	char fixture_root[PATH_MAX];
	int written = snprintf(fixture_root,
		sizeof(fixture_root),
		"%s/tests/support/assets",
		PROJECT_SOURCE_DIR);
	assert_true(written > 0 && written < (int)sizeof(fixture_root));

	char maps_dir[PATH_MAX];
	written = snprintf(maps_dir, sizeof(maps_dir), "%s/maps", fixture_root);
	assert_true(written > 0 && written < (int)sizeof(maps_dir));
	(void)ensure_goal_fixture_directory(maps_dir);

	char bsp_path[PATH_MAX];
	written = snprintf(bsp_path,
		sizeof(bsp_path),
		"%s/goal_info_order_tmp.bsp",
		maps_dir);
	assert_true(written > 0 && written < (int)sizeof(bsp_path));

	const char *entity_lump =
		"{\n"
		"\"classname\" \"target_location\"\n"
		"\"message\" \"dupe location\"\n"
		"\"origin\" \"16 0 16\"\n"
		"}\n"
		"{\n"
		"\"classname\" \"target_location\"\n"
		"\"message\" \"dupe location\"\n"
		"\"origin\" \"48 0 16\"\n"
		"}\n"
		"{\n"
		"\"classname\" \"info_camp\"\n"
		"\"message\" \"first camp\"\n"
		"\"origin\" \"24 0 16\"\n"
		"}\n"
		"{\n"
		"\"classname\" \"info_camp\"\n"
		"\"message\" \"second camp\"\n"
		"\"origin\" \"64 0 16\"\n"
		"}\n";
	write_goal_bsp_entity_fixture(bsp_path, entity_lump);

	char itemconfig_path[PATH_MAX];
	written = snprintf(itemconfig_path, sizeof(itemconfig_path), "%s/items.c", fixture_root);
	assert_true(written > 0 && written < (int)sizeof(itemconfig_path));
	const char *itemconfig_fixture =
		"iteminfo \"weapon_rocketlauncher\"\n"
		"{\n"
		"name \"Rocket Launcher\"\n"
		"model \"models/weapons/g_rocket/tris.md2\"\n"
		"mins {-15, -15, -15}\n"
		"maxs {15, 15, 15}\n"
		"respawntime 30\n"
		"}\n";
	write_goal_text_fixture(itemconfig_path, itemconfig_fixture);

	char previous_cwd[PATH_MAX];
	assert_non_null(getcwd(previous_cwd, sizeof(previous_cwd)));

	test_goal_aas_fixture_t fixture;
	test_goal_aas_fixture_begin(&fixture, 8);
	snprintf(aasworld.mapName, sizeof(aasworld.mapName), "goal_info_order_tmp");
	assert_int_equal(env->exports->BotLibVarSet("itemconfig", "items.c"), BLERR_NOERROR);
	LibVarSet("itemconfig", "items.c");
	BotGoal_SetMapModelIndexes(0, NULL);
	assert_int_equal(chdir(fixture_root), 0);
	BotInitLevelItems();
	assert_int_equal(chdir(previous_cwd), 0);

	bot_goal_t location_goal;
	memset(&location_goal, 0, sizeof(location_goal));
	assert_int_equal(BotGetMapLocationGoal("dupe location", &location_goal), 1);
	assert_float_equal(location_goal.origin[0], 48.0f, 0.0001f);

	bot_goal_t camp_goal;
	memset(&camp_goal, 0, sizeof(camp_goal));
	int next = BotGetNextCampSpotGoal(0, &camp_goal);
	assert_int_equal(next, 1);
	assert_float_equal(camp_goal.origin[0], 64.0f, 0.0001f);

	memset(&camp_goal, 0, sizeof(camp_goal));
	next = BotGetNextCampSpotGoal(next, &camp_goal);
	assert_int_equal(next, 2);
	assert_float_equal(camp_goal.origin[0], 24.0f, 0.0001f);

	memset(&camp_goal, 0, sizeof(camp_goal));
	assert_int_equal(BotGetNextCampSpotGoal(next, &camp_goal), 0);

	test_goal_aas_fixture_end(&fixture);
	assert_int_equal(unlink(bsp_path), 0);
	assert_int_equal(unlink(itemconfig_path), 0);
	(void)rmdir(maps_dir);
}

/*
=============
test_goal_static_level_item_links_live_entity_model

Pins the Gladiator HLIL dynamic-item pass that links a known static level item
to a settled live entity sharing the item model index.
=============
*/
static void test_goal_static_level_item_links_live_entity_model(void **state)
{
	test_environment_t *env = (test_environment_t *)(*state);
	(void)env;

	test_goal_aas_fixture_t fixture;
	test_goal_aas_fixture_begin(&fixture, 8);

	char *models[16] = {0};
	models[7] = "models/weapons/g_rocket/tris.md2";
	BotGoal_SetMapModelIndexes(16, models);
	BotInitLevelItems();
	BotGoal_SetCurrentTime(0.0f);

	bot_levelitem_setup_t setup;
	memset(&setup, 0, sizeof(setup));
	setup.classname = "weapon_rocketlauncher";
	setup.goal.number = 41;
	setup.goal.flags = GFL_ITEM;
	setup.flags = GFL_ITEM;
	VectorSet(setup.goal.origin, 16.0f, 0.0f, 16.0f);
	VectorSet(setup.goal.mins, -15.0f, -15.0f, -15.0f);
	VectorSet(setup.goal.maxs, 15.0f, 15.0f, 15.0f);
	assert_int_equal(BotGoal_RegisterLevelItem(&setup), 41);

	aas_entity_t *entity = &aasworld.entities[5];
	entity->inuse = qtrue;
	entity->number = 5;
	entity->modelindex = 7;
	VectorSet(entity->origin, 20.0f, 0.0f, 16.0f);
	VectorCopy(entity->origin, entity->previousOrigin);

	aasworld.time = 4.0f;
	BotGoal_SetCurrentTime(aasworld.time);
	BotUpdateEntityItems();

	bot_goal_t goal;
	memset(&goal, 0, sizeof(goal));
	assert_int_equal(BotGetLevelItemGoal(0, "weapon_rocketlauncher", &goal), 41);
	assert_int_equal(goal.entitynum, 5);
	assert_int_equal(goal.areanum, 1);
	assert_float_equal(goal.origin[0], 20.0f, 0.0001f);

	test_goal_aas_fixture_end(&fixture);
}

/*
=============
test_goal_dynamic_model_change_preserves_prior_item_link

Pins sub_1002fa20's model-change behavior: existing links are not pruned
when an entity changes model, so the new model receives its own dropped goal.
=============
*/
static void test_goal_dynamic_model_change_preserves_prior_item_link(void **state)
{
	test_environment_t *env = (test_environment_t *)(*state);
	(void)env;

	test_goal_aas_fixture_t fixture;
	test_goal_aas_fixture_begin(&fixture, 8);

	char *models[16] = {0};
	models[7] = "models/weapons/g_rocket/tris.md2";
	models[8] = "models/weapons/g_shotg/tris.md2";
	BotGoal_SetMapModelIndexes(16, models);
	BotInitLevelItems();

	bot_levelitem_setup_t setup;
	memset(&setup, 0, sizeof(setup));
	setup.classname = "weapon_rocketlauncher";
	setup.goal.number = 41;
	setup.goal.flags = GFL_ITEM;
	setup.flags = GFL_ITEM;
	VectorSet(setup.goal.origin, 16.0f, 0.0f, 16.0f);
	VectorSet(setup.goal.mins, -15.0f, -15.0f, -15.0f);
	VectorSet(setup.goal.maxs, 15.0f, 15.0f, 15.0f);
	assert_int_equal(BotGoal_RegisterLevelItem(&setup), 41);

	aas_entity_t *entity = &aasworld.entities[5];
	entity->inuse = qtrue;
	entity->number = 5;
	entity->modelindex = 7;
	VectorSet(entity->origin, 20.0f, 0.0f, 16.0f);
	VectorCopy(entity->origin, entity->previousOrigin);

	aasworld.time = 4.0f;
	BotGoal_SetCurrentTime(aasworld.time);
	BotUpdateEntityItems();

	entity->modelindex = 8;
	aasworld.time = 5.0f;
	BotGoal_SetCurrentTime(aasworld.time);
	BotUpdateEntityItems();

	bot_goal_t goal;
	memset(&goal, 0, sizeof(goal));
	assert_int_equal(BotGetLevelItemGoal(0, "weapon_rocketlauncher", &goal), 41);
	assert_int_equal(goal.entitynum, 5);

	memset(&goal, 0, sizeof(goal));
	assert_int_equal(BotGetLevelItemGoal(0, "Shotgun", &goal), 6);
	assert_int_equal(goal.entitynum, 5);

	test_goal_aas_fixture_end(&fixture);
}

/*
=============
test_goal_dynamic_entity_items_match_item_models

Pins dropped-item reconstruction from live model indexes and its retail
thirty-second timeout.
=============
*/
static void test_goal_dynamic_entity_items_match_item_models(void **state)
{
	test_environment_t *env = (test_environment_t *)(*state);
	(void)env;

	test_goal_aas_fixture_t fixture;
	test_goal_aas_fixture_begin(&fixture, 8);

	char *models[16] = {0};
	models[7] = "models/weapons/g_rocket/tris.md2";
	BotGoal_SetMapModelIndexes(16, models);
	BotInitLevelItems();

	aas_entity_t *entity = &aasworld.entities[5];
	entity->inuse = qtrue;
	entity->number = 5;
	entity->modelindex = 7;
	VectorSet(entity->origin, 32.0f, 0.0f, 16.0f);
	VectorCopy(entity->origin, entity->previousOrigin);

	aasworld.time = 4.0f;
	BotGoal_SetCurrentTime(aasworld.time);
	BotUpdateEntityItems();

	bot_goal_t goal;
	memset(&goal, 0, sizeof(goal));
	goal.flags = 0x6b;
	goal.iteminfo = -12;
	assert_int_equal(BotGetLevelItemGoal(0, "Rocket Launcher", &goal), 5);
	assert_int_equal(goal.entitynum, 5);
	assert_int_equal(goal.areanum, 1);
	assert_int_equal(goal.flags, 0x6b);
	assert_int_equal(goal.iteminfo, -12);

	entity->inuse = qfalse;
	aasworld.time = 35.0f;
	BotGoal_SetCurrentTime(aasworld.time);
	BotUpdateEntityItems();
	memset(&goal, 0, sizeof(goal));
	assert_int_equal(BotGetLevelItemGoal(0, "Rocket Launcher", &goal), -1);

	test_goal_aas_fixture_end(&fixture);
}

/*
=============
test_goal_dynamic_entity_items_register_jumppad_areas

Pins sub_1002fa20's pre-Q3 behavior: temporary dropped items are registered
even when their resolved goal area is a jump-pad area.
=============
*/
static void test_goal_dynamic_entity_items_register_jumppad_areas(void **state)
{
	test_environment_t *env = (test_environment_t *)(*state);
	(void)env;

	test_goal_aas_fixture_t fixture;
	test_goal_aas_fixture_begin(&fixture, 8);
	aasworld.areasettings[1].contents = AAS_AREACONTENTS_JUMPPAD;

	char *models[16] = {0};
	models[7] = "models/weapons/g_rocket/tris.md2";
	BotGoal_SetMapModelIndexes(16, models);
	BotInitLevelItems();

	aas_entity_t *entity = &aasworld.entities[5];
	entity->inuse = qtrue;
	entity->number = 5;
	entity->modelindex = 7;
	VectorSet(entity->origin, 32.0f, 0.0f, 16.0f);
	VectorCopy(entity->origin, entity->previousOrigin);

	aasworld.time = 4.0f;
	BotGoal_SetCurrentTime(aasworld.time);
	BotUpdateEntityItems();

	bot_goal_t goal;
	memset(&goal, 0, sizeof(goal));
	assert_int_equal(BotGetLevelItemGoal(0, "Rocket Launcher", &goal), 5);
	assert_int_equal(goal.entitynum, 5);
	assert_int_equal(goal.areanum, 1);

	test_goal_aas_fixture_end(&fixture);
}

/*
=============
test_goal_update_refreshes_entity_items_before_selection

Pins the Gladiator BotAI-side once-per-second entity item refresh before LTG
selection, including the raw split between a moving entity position and the
public best-reachable goal origin established when it first links.
=============
*/
static void test_goal_update_refreshes_entity_items_before_selection(void **state)
{
	test_environment_t *env = (test_environment_t *)(*state);
	activate_test_client(env);

	bot_client_state_t *slot = BotState_Get(0);
	assert_non_null(slot);
	assert_true(slot->goal_handle > 0);

	test_goal_aas_fixture_t fixture;
	test_goal_aas_fixture_begin(&fixture, 8);

	char *models[16] = {0};
	models[7] = "models/weapons/g_rocket/tris.md2";
	BotGoal_SetMapModelIndexes(16, models);
	BotInitLevelItems();
	BotGoal_SetCurrentTime(0.0f);

	bot_levelitem_setup_t setup;
	memset(&setup, 0, sizeof(setup));
	setup.classname = "weapon_rocketlauncher";
	setup.goal.number = 306;
	setup.goal.entitynum = 0;
	setup.goal.areanum = 1;
	setup.goal.flags = GFL_ITEM;
	setup.flags = GFL_ITEM;
	VectorSet(setup.goal.origin, 16.0f, 0.0f, 16.0f);
	VectorSet(setup.goal.mins, -15.0f, -15.0f, -15.0f);
	VectorSet(setup.goal.maxs, 15.0f, 15.0f, 15.0f);
	assert_int_equal(env->exports->BotRegisterLevelItem(&setup), 306);

	char fixture_path[PATH_MAX];
	int written = snprintf(fixture_path,
		sizeof(fixture_path),
		"%s/tests/support/assets/bots/goal_entity_refresh_tmp.w",
		PROJECT_SOURCE_DIR);
	assert_true(written > 0 && written < (int)sizeof(fixture_path));

	const char *weights =
		"weight \"weapon_rocketlauncher\"\n"
		"{\n"
		"return balance(25,25,25);\n"
		"}\n";
	write_goal_weight_fixture(fixture_path, weights);
	assert_int_equal(env->exports->BotLoadItemWeights(slot->goal_handle, fixture_path), BLERR_NOERROR);

	aas_entity_t *entity = &aasworld.entities[5];
	entity->inuse = qtrue;
	entity->number = 5;
	entity->modelindex = 7;
	VectorSet(entity->origin, 20.0f, 0.0f, 16.0f);
	VectorCopy(entity->origin, entity->previousOrigin);

	int inventory[MAX_ITEMS];
	memset(inventory, 0, sizeof(inventory));
	vec3_t origin;
	VectorSet(origin, -64.0f, 0.0f, 0.0f);

	aasworld.time = 2.0f;
	int status = AI_GoalBotlib_Update(slot->goal_handle,
		origin,
		inventory,
		TFL_DEFAULT,
		2.0f,
		0.0f);
	assert_int_equal(status, BLERR_NOERROR);

	bot_goal_t goal;
	memset(&goal, 0, sizeof(goal));
	assert_true(env->exports->BotGetTopGoal(slot->goal_handle, &goal));
	assert_int_equal(goal.number, 306);
	assert_int_equal(goal.entitynum, 5);

	bot_goal_t tracked_goal;
	memset(&tracked_goal, 0, sizeof(tracked_goal));
	assert_int_equal(BotGetLevelItemGoal(0, "weapon_rocketlauncher", &tracked_goal), 306);
	assert_float_equal(tracked_goal.origin[0], 20.0f, 0.0001f);

	VectorSet(entity->origin, 48.0f, 0.0f, 16.0f);
	VectorCopy(entity->origin, entity->previousOrigin);

	aasworld.time = 2.5f;
	status = AI_GoalBotlib_Update(slot->goal_handle,
		origin,
		inventory,
		TFL_DEFAULT,
		2.5f,
		0.0f);
	assert_int_equal(status, BLERR_NOERROR);
	memset(&tracked_goal, 0, sizeof(tracked_goal));
	assert_int_equal(BotGetLevelItemGoal(0, "weapon_rocketlauncher", &tracked_goal), 306);
	assert_float_equal(tracked_goal.origin[0], 20.0f, 0.0001f);

	aasworld.time = 3.1f;
	status = AI_GoalBotlib_Update(slot->goal_handle,
		origin,
		inventory,
		TFL_DEFAULT,
		3.1f,
		0.0f);
	assert_int_equal(status, BLERR_NOERROR);
	memset(&tracked_goal, 0, sizeof(tracked_goal));
	assert_int_equal(BotGetLevelItemGoal(0, "weapon_rocketlauncher", &tracked_goal), 306);
	assert_float_equal(tracked_goal.origin[0], 20.0f, 0.0001f);

	test_goal_aas_fixture_end(&fixture);
	assert_int_equal(remove(fixture_path), 0);
}

/*
=============
test_goal_refresh_and_movement_dispatch_order

Confirms staged direct LTGs retain their stack order and do not synthesize a
fallback movement command when the AAS world is unavailable.
=============
*/
static void test_goal_refresh_and_movement_dispatch_order(void **state)
{
    test_environment_t *env = (test_environment_t *)(*state);
    activate_test_client(env);

    bot_client_state_t *slot = BotState_Get(0);
    assert_non_null(slot);
	slot->enter_game_chat_attempted = true;

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
	/* Retain the manually staged LTG instead of asking the selector to replace it. */
	slot->long_term_goal_time = AAS_Time() + 20.0f;

    int status = env->exports->BotAI(0, 0.1f);
    assert_int_equal(status, BLERR_NOERROR);

	bot_goal_t active_goal;
	bot_goal_t next_goal;
	assert_true(env->exports->BotGetTopGoal(slot->goal_handle, &active_goal));
	assert_true(AI_GoalBotlib_GetSecondGoal(slot->goal_handle, &next_goal));
	assert_int_equal(active_goal.number, 3);
	assert_int_equal(next_goal.number, 7);

    assert_int_equal(g_bot_input_log.count, 1);
    assert_int_equal(g_bot_input_log.last_client, 0);
    assert_float_equal(g_bot_input_log.last_command.thinktime, 0.1f, 0.0001f);
	/* Direct LTG movement advances the private view after submitting its mover input. */

    assert_float_equal(g_bot_input_log.last_command.speed, 0.0f, 0.0001f);
    assert_float_equal(g_bot_input_log.last_command.dir[0], 0.0f, 0.0001f);
    assert_float_equal(g_bot_input_log.last_command.dir[1], 0.0f, 0.0001f);
    assert_float_equal(g_bot_input_log.last_command.dir[2], 0.0f, 0.0001f);

	assert_int_equal(slot->current_weapon, 0);
    assert_true(slot->has_move_result);
}

/*
=============
test_movement_error_propagates_without_submission

Pins the retail nonfatal invalid-move-handle result: it submits a stationary
frame without manufacturing an avoid goal or a bot-interface error.
=============
*/
static void test_movement_error_propagates_without_submission(void **state)
{
    test_environment_t *env = (test_environment_t *)(*state);
    activate_test_client(env);

    bot_client_state_t *slot = BotState_Get(0);
    assert_non_null(slot);
	slot->enter_game_chat_attempted = true;

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
	/* The invalid move handle is reached only while this staged LTG is retained. */
	slot->long_term_goal_time = AAS_Time() + 20.0f;

    int status = env->exports->BotAI(0, 0.2f);
    assert_int_equal(status, BLERR_NOERROR);

    ai_avoid_list_t *avoid = AI_GoalState_GetAvoidList(slot->goal_state);
    assert_false(AI_AvoidList_Contains(avoid, 11, 0.0f));
    assert_int_equal(g_bot_input_log.count, 1);

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

/*
=============
test_dm_enemy_selection_uses_retail_live_player_predicate

Pins entity numbering, death effects/frames, and the absence of the older
translucent/chatting exclusions in Gladiator's acquisition path.
=============
*/
static void test_dm_enemy_selection_uses_retail_live_player_predicate(void **state)
{
    test_environment_t *env = (test_environment_t *)(*state);
    activate_test_client(env);

    bot_client_state_t *self = BotState_Get(0);
    assert_non_null(self);
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
	VectorCopy(enemy_update.origin, enemy_entity.old_origin);
	enemy_entity.modelindex = 255;
	enemy_entity.frame = 173;

    bot_client_state_t *enemy_state = BotState_Create(1);
    assert_non_null(enemy_state);
    enemy_state->active = true;
	enemy_state->team = self->team;
    enemy_state->last_client_update = enemy_update;
	enemy_state->last_client_update.stats[STAT_LAYOUTS] = 1;
    enemy_state->client_update_valid = true;
    aasworld.loaded = qtrue;
    TranslateEntity_SetWorldLoaded(qtrue);

    int status = env->exports->BotStartFrame(0.1f);
    assert_int_equal(status, BLERR_NOERROR);
    status = env->exports->BotUpdateClient(0, &self_update);
    assert_int_equal(status, BLERR_NOERROR);
    status = env->exports->BotUpdateEntity(2, &enemy_entity);
    assert_int_equal(status, BLERR_NOERROR);

    status = env->exports->BotAI(0, 0.1f);
    assert_int_equal(status, BLERR_NOERROR);
	assert_int_equal(self->combat.current_enemy, 0);

	enemy_entity.frame = 0;
	enemy_entity.effects = EF_GIB;

    status = env->exports->BotStartFrame(0.2f);
    assert_int_equal(status, BLERR_NOERROR);
    status = env->exports->BotUpdateClient(0, &self_update);
    assert_int_equal(status, BLERR_NOERROR);
    status = env->exports->BotUpdateEntity(2, &enemy_entity);
    assert_int_equal(status, BLERR_NOERROR);
    enemy_state->last_client_update = enemy_update;
    enemy_state->client_update_valid = true;

    status = env->exports->BotAI(0, 0.1f);
    assert_int_equal(status, BLERR_NOERROR);
	assert_int_equal(self->combat.current_enemy, 0);

	enemy_entity.effects = 0;
	enemy_entity.renderfx = RF_TRANSLUCENT;

    status = env->exports->BotStartFrame(0.3f);
    assert_int_equal(status, BLERR_NOERROR);
    status = env->exports->BotUpdateClient(0, &self_update);
    assert_int_equal(status, BLERR_NOERROR);
    status = env->exports->BotUpdateEntity(2, &enemy_entity);
    assert_int_equal(status, BLERR_NOERROR);

    status = env->exports->BotAI(0, 0.1f);
    assert_int_equal(status, BLERR_NOERROR);
    assert_int_equal(self->combat.current_enemy, 2);
	assert_int_equal(self->ai_node, BOT_AI_NODE_BATTLE_RETREAT);
	assert_false(self->combat.enemy_visible);
	bot_movestate_t *move_state = BotMoveStateFromHandle(self->move_handle);
	assert_non_null(move_state);
	/* Raw BotAttackMove uses BotMoveInDirection without refreshing move input. */
	assert_int_equal(move_state->client, 0);
	assert_int_equal(move_state->entitynum, 0);

    BotState_Destroy(1);
}

/*
=============
test_dm_enemy_selection_damage_alert

Verifies inventory-health decrease widens acquisition to 360 degrees without
writing the removed synthetic damage telemetry.
=============
*/
static void test_dm_enemy_selection_damage_alert(void **state)
{
    test_environment_t *env = (test_environment_t *)(*state);
    activate_test_client(env);

    bot_client_state_t *self = BotState_Get(0);
    assert_non_null(self);
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
	VectorCopy(enemy_update.origin, enemy_entity.old_origin);
	enemy_entity.modelindex = 255;

    bot_client_state_t *enemy_state = BotState_Create(1);
    assert_non_null(enemy_state);
    enemy_state->active = true;
    enemy_state->last_client_update = enemy_update;
    enemy_state->client_update_valid = true;
    aasworld.loaded = qtrue;
    TranslateEntity_SetWorldLoaded(qtrue);

    int status = env->exports->BotStartFrame(1.0f);
    assert_int_equal(status, BLERR_NOERROR);
    status = env->exports->BotUpdateClient(0, &self_update);
    assert_int_equal(status, BLERR_NOERROR);
    status = env->exports->BotUpdateEntity(2, &enemy_entity);
    assert_int_equal(status, BLERR_NOERROR);

    status = env->exports->BotAI(0, 0.1f);
    assert_int_equal(status, BLERR_NOERROR);
	assert_int_equal(self->combat.current_enemy, 0);

    self_update.stats[STAT_HEALTH] = 60;
    enemy_state->last_client_update = enemy_update;
    enemy_state->client_update_valid = true;

    status = env->exports->BotStartFrame(1.1f);
    assert_int_equal(status, BLERR_NOERROR);
    status = env->exports->BotUpdateClient(0, &self_update);
    assert_int_equal(status, BLERR_NOERROR);
    status = env->exports->BotUpdateEntity(2, &enemy_entity);
    assert_int_equal(status, BLERR_NOERROR);

    status = env->exports->BotAI(0, 0.1f);
    assert_int_equal(status, BLERR_NOERROR);
    assert_int_equal(self->combat.current_enemy, 2);
	assert_int_equal(self->combat.last_known_health, 60);
	assert_false(self->combat.took_damage);
	assert_int_equal(self->ai_node, BOT_AI_NODE_BATTLE_RETREAT);
	assert_false(self->combat.enemy_visible);
	assert_true(self->combat.last_damage_time < -1.0e30f);

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
		cmocka_unit_test_setup_teardown(test_goal_itemconfig_enforces_max_iteminfo,
										goal_move_setup,
										goal_move_teardown),
		cmocka_unit_test_setup_teardown(test_goal_item_weights_use_all_itemconfig_entries,
										goal_move_setup,
										goal_move_teardown),
		cmocka_unit_test_setup_teardown(test_goal_level_item_pool_honors_max_levelitems,
                                        goal_move_setup,
                                        goal_move_teardown),
        cmocka_unit_test_setup_teardown(test_goal_item_weights_cache_uses_caller_filename,
                                        goal_move_setup,
                                        goal_move_teardown),
        cmocka_unit_test_setup_teardown(test_goal_fuzzy_logic_reuses_child_config,
                                        goal_move_setup,
                                        goal_move_teardown),
        cmocka_unit_test_setup_teardown(test_goal_item_scoring_uses_undecided_fuzzy_weights,
                                        goal_move_setup,
                                        goal_move_teardown),
        cmocka_unit_test_setup_teardown(test_goal_choose_skips_unlinked_static_items_except_roam,
                                        goal_move_setup,
                                        goal_move_teardown),
        cmocka_unit_test_setup_teardown(test_goal_nbg_uses_retail_strict_maxtime,
                                        goal_move_setup,
                                        goal_move_teardown),
        cmocka_unit_test_setup_teardown(test_goal_choose_rejects_start_area_without_reachability,
                                        goal_move_setup,
                                        goal_move_teardown),
        cmocka_unit_test_setup_teardown(test_goal_retail_exports_and_avoid_sync,
                                        goal_move_setup,
                                        goal_move_teardown),
        cmocka_unit_test_setup_teardown(test_goal_negative_avoid_time_uses_respawn_for_dropped_items,
                                        goal_move_setup,
                                        goal_move_teardown),
        cmocka_unit_test_setup_teardown(test_goal_avoid_goals_use_retail_fixed_slots,
                                        goal_move_setup,
                                        goal_move_teardown),
        cmocka_unit_test_setup_teardown(test_goal_level_item_goal_uses_retail_index_cursor,
                                        goal_move_setup,
                                        goal_move_teardown),
		cmocka_unit_test_setup_teardown(test_goal_level_item_goal_preserves_retail_tail_fields,
                                        goal_move_setup,
                                        goal_move_teardown),
        cmocka_unit_test_setup_teardown(test_goal_choose_uses_timeout_for_dropped_semantics,
                                        goal_move_setup,
                                        goal_move_teardown),
        cmocka_unit_test_setup_teardown(test_goal_touching_uses_presence_bounds,
                                        goal_move_setup,
                                        goal_move_teardown),
        cmocka_unit_test_setup_teardown(test_goal_item_vis_trace_targets_retail_mins_point,
                                        goal_move_setup,
                                        goal_move_teardown),
        cmocka_unit_test_setup_teardown(test_goal_init_filters_notspawnflags_entities,
                                        goal_move_setup,
                                        goal_move_teardown),
		cmocka_unit_test_setup_teardown(test_goal_raw_dropped_item_selection,
										goal_move_setup,
										goal_move_teardown),
		cmocka_unit_test_setup_teardown(test_goal_init_drops_all_bsp_items_to_floor,
                                        goal_move_setup,
                                        goal_move_teardown),
        cmocka_unit_test_setup_teardown(test_goal_info_entities_use_retail_head_insertion_order,
                                        goal_move_setup,
                                        goal_move_teardown),
        cmocka_unit_test_setup_teardown(test_goal_static_level_item_links_live_entity_model,
                                        goal_move_setup,
                                        goal_move_teardown),
        cmocka_unit_test_setup_teardown(test_goal_dynamic_model_change_preserves_prior_item_link,
                                        goal_move_setup,
                                        goal_move_teardown),
        cmocka_unit_test_setup_teardown(test_goal_dynamic_entity_items_match_item_models,
                                        goal_move_setup,
                                        goal_move_teardown),
		cmocka_unit_test_setup_teardown(test_goal_dynamic_entity_items_register_jumppad_areas,
                                        goal_move_setup,
                                        goal_move_teardown),
        cmocka_unit_test_setup_teardown(test_goal_update_refreshes_entity_items_before_selection,
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
        cmocka_unit_test_setup_teardown(test_dm_enemy_selection_uses_retail_live_player_predicate,
                                        goal_move_setup,
                                        goal_move_teardown),
        cmocka_unit_test_setup_teardown(test_dm_enemy_selection_damage_alert,
                                        goal_move_setup,
                                        goal_move_teardown),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}

