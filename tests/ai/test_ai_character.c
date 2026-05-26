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

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#include "botlib/ai_character/bot_character.h"
#include "botlib/ai_chat/ai_chat.h"
#include "botlib/ai_weapon/bot_weapon.h"
#include "botlib/ai_weight/bot_weight.h"
#include "botlib/common/l_libvar.h"
#include "botlib/common/l_memory.h"
#include "botlib/ea/ea_local.h"
#include "botlib/interface/bot_state.h"
#include "botlib/interface/botlib_interface.h"
#include "q2bridge/botlib.h"
#include "../support/asset_env.h"

#include "chars.h"

#ifndef PROJECT_SOURCE_DIR
#error "PROJECT_SOURCE_DIR must be defined so regression tests can resolve asset paths."
#endif

#define TEST_BOTLIB_HEAP_SIZE (8u << 20)
#define TEST_MAX_LOG_MESSAGES 64
#define TEST_MAX_CLIENT_COMMANDS 16

enum q3_characteristic_index_e {
	Q3_CHARACTERISTIC_NAME = 0,
	Q3_CHARACTERISTIC_GENDER = 1,
	Q3_CHARACTERISTIC_ATTACK_SKILL = 2,
	Q3_CHARACTERISTIC_WEAPONWEIGHTS = 3,
	Q3_CHARACTERISTIC_AIM_SKILL = 16,
	Q3_CHARACTERISTIC_CHAT_NAME = 22,
	Q3_CHARACTERISTIC_CHAT_CPM = 23,
	Q3_CHARACTERISTIC_ITEMWEIGHTS = 40,
	Q3_CHARACTERISTIC_WALKER = 48,
};

typedef struct test_log_message_s {
    int priority;
    char text[256];
} test_log_message_t;

typedef struct test_client_command_s {
	int client;
	char text[256];
} test_client_command_t;

typedef struct test_environment_s {
    asset_env_t assets;
    bool libvar_initialised;
    bool memory_initialised;
    bool import_table_set;
    bool library_setup;
    bool client_active;
    ai_weapon_library_t *weapon_library;
    bot_export_t *exports;
} test_environment_t;

static struct {
    test_log_message_t entries[TEST_MAX_LOG_MESSAGES];
    int count;
} g_test_log;

static struct {
	test_client_command_t entries[TEST_MAX_CLIENT_COMMANDS];
	int count;
} g_test_client_commands;

static void test_reset_client_commands(void)
{
	g_test_client_commands.count = 0;
	for (int i = 0; i < TEST_MAX_CLIENT_COMMANDS; ++i) {
		g_test_client_commands.entries[i].client = -1;
		g_test_client_commands.entries[i].text[0] = '\0';
	}
}

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

static bool test_log_contains(const char *needle)
{
    if (needle == NULL || *needle == '\0') {
        return false;
    }

    for (int i = 0; i < g_test_log.count; ++i) {
        if (strstr(g_test_log.entries[i].text, needle) != NULL) {
            return true;
        }
    }

    return false;
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

static void test_bot_input(int client, bot_input_t *input)
{
    (void)client;
    (void)input;
}

static void test_bot_client_command(int client, char *fmt, ...)
{
	if (g_test_client_commands.count >= TEST_MAX_CLIENT_COMMANDS) {
		return;
	}

	test_client_command_t *slot = &g_test_client_commands.entries[g_test_client_commands.count++];
	slot->client = client;
	snprintf(slot->text, sizeof(slot->text), "%s", fmt != NULL ? fmt : "");
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

static int character_profile_setup(void **state)
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
	test_reset_client_commands();
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

    assert_true(BotMemory_Init(TEST_BOTLIB_HEAP_SIZE));
    env->memory_initialised = true;

    *state = env;
    return 0;
}

static int character_profile_teardown(void **state)
{
    test_environment_t *env = (test_environment_t *)(*state);
    if (env == NULL) {
        return 0;
    }

    if (env->memory_initialised) {
        BotShutdownCharacters();
        BotShutdownWeights();
    }

    if (env->weapon_library != NULL) {
        AI_UnloadWeaponLibrary(env->weapon_library);
        env->weapon_library = NULL;
    }

    if (env->memory_initialised) {
        BotMemory_Shutdown();
        env->memory_initialised = false;
    }

    if (env->libvar_initialised) {
        LibVar_Shutdown();
        env->libvar_initialised = false;
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

static int bot_setup_client_setup(void **state)
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
	test_reset_client_commands();
    BotInterface_SetImportTable(&g_test_imports);
    env->import_table_set = true;

    LibVar_Init();
    env->libvar_initialised = true;

    env->exports = GetBotAPI(&g_test_bot_import);
    assert_non_null(env->exports);

    *state = env;
    return 0;
}

static int bot_setup_client_teardown(void **state)
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

    if (env->import_table_set) {
        BotInterface_SetImportTable(NULL);
        env->import_table_set = false;
    }

    asset_env_cleanup(&env->assets);
    free(env);
    *state = NULL;
    return 0;
}

static void asset_path_or_skip(const char *relative_path, char *out, size_t out_size)
{
    int written = snprintf(out, out_size, "%s/%s", PROJECT_SOURCE_DIR, relative_path);
    if (written <= 0 || (size_t)written >= out_size) {
        cmocka_skip();
    }

    FILE *file = fopen(out, "rb");
    if (file == NULL) {
        cmocka_skip();
    }
    fclose(file);
}

/*
=============
write_character_fixture

Writes temporary character or weight scripts for profile wiring tests.
=============
*/
static void write_character_fixture(const char *path, const char *contents)
{
	FILE *file = fopen(path, "wb");
	assert_non_null(file);
	assert_true(fputs(contents, file) >= 0);
	assert_int_equal(fclose(file), 0);
}

/*
=============
q3_botfiles_root_or_skip

Resolves the checked-in Q3 botfiles root for successor-format character tests.
=============
*/
static void q3_botfiles_root_or_skip(char *out, size_t out_size)
{
	int written = snprintf(out,
		out_size,
		"%s/dev_tools/Quake-III-Arena-assets/botfiles",
		PROJECT_SOURCE_DIR);
	if (written <= 0 || (size_t)written >= out_size)
	{
		cmocka_skip();
	}

	char chars_path[PATH_MAX];
	written = snprintf(chars_path, sizeof(chars_path), "%s/chars.h", out);
	if (written <= 0 || (size_t)written >= sizeof(chars_path))
	{
		cmocka_skip();
	}

	FILE *file = fopen(chars_path, "rb");
	if (file == NULL)
	{
		cmocka_skip();
	}
	fclose(file);
}

static int weapon_index_by_name(const bot_weapon_config_t *config, const char *name)
{
    if (config == NULL || name == NULL) {
        return -1;
    }

    for (int i = 0; i < config->num_weapons; ++i) {
        if (config->weapons[i].name != NULL && strcmp(config->weapons[i].name, name) == 0) {
            return i;
        }
    }

    return -1;
}

static void test_babe_character_profile(void **state)
{
    test_environment_t *env = (test_environment_t *)(*state);
    assert_non_null(env);

    char weapon_config_path[PATH_MAX];
    asset_path_or_skip("dev_tools/assets/weapons.c", weapon_config_path, sizeof(weapon_config_path));
    (void)weapon_config_path;

    env->weapon_library = AI_LoadWeaponLibrary(NULL);
    assert_non_null(env->weapon_library);
    assert_string_equal(env->weapon_library->source_path, weapon_config_path);

    test_reset_log();
    ai_character_profile_t *profile = AI_LoadCharacter("bots/babe_c.c", 1.0f);
    assert_non_null(profile);
    assert_string_equal(profile->character_name, "babe");
    assert_true(test_log_contains("bytes character"));
    assert_true(test_log_contains("bytes item weights"));
    assert_true(test_log_contains("bytes weapon weights"));
    assert_true(test_log_contains("bytes weapon index"));
    assert_true(test_log_contains("bytes chat file"));

    const char *chat_file = AI_CharacteristicAsString(profile, CHARACTERISTIC_CHAT_FILE);
    assert_non_null(chat_file);
    assert_string_equal(chat_file, "bots/babe_t.c");

    float aggression = AI_CharacteristicAsFloat(profile, CHARACTERISTIC_AGGRESSION);
    assert_true(fabsf(aggression - 0.7f) < 0.0001f);

    float grapple_user = AI_CharacteristicAsFloat(profile, CHARACTERISTIC_GRAPPLE_USER);
    assert_true(fabsf(grapple_user - 1.0f) < 0.0001f);

    int chat_cpm = AI_CharacteristicAsInteger(profile, CHARACTERISTIC_CHAT_CPM);
    assert_int_equal(chat_cpm, 400);

    bot_weight_config_t *item_weights = AI_ItemWeightsForCharacter(profile);
    assert_non_null(item_weights);
    assert_true(BotWeight_FindIndex(item_weights, "weapon_rocketlauncher") >= 0);

    ai_weapon_weights_t *weapon_weights = AI_WeaponWeightsForCharacter(profile);
    assert_non_null(weapon_weights);
    assert_non_null(weapon_weights->config);
    assert_true(BotWeight_FindIndex(weapon_weights->config, "Rocket Launcher") >= 0);

    const bot_weapon_config_t *weapon_config = AI_GetWeaponConfig(env->weapon_library);
    assert_non_null(weapon_config);
    int rocket_index = weapon_index_by_name(weapon_config, "Rocket Launcher");
    assert_true(rocket_index >= 0);

    struct ai_character_profile_s *internal = (struct ai_character_profile_s *)profile;
    assert_non_null(internal->chat_state);
    assert_true(BotChat_HasSynonymPhrase((bot_chatstate_t *)internal->chat_state,
                                         "CONTEXT_NEARBYITEM",
                                         "Shotgun"));

    AI_FreeCharacter(profile);
}

/*
=============
test_character_weight_cache_uses_caller_filenames

Pins character-owned item and weapon weights to the filename-keyed ai_weight cache.
=============
*/
static void test_character_weight_cache_uses_caller_filenames(void **state)
{
	test_environment_t *env = (test_environment_t *)(*state);
	assert_non_null(env);

	char weapon_config_path[PATH_MAX];
	asset_path_or_skip("dev_tools/assets/weapons.c", weapon_config_path, sizeof(weapon_config_path));

	char fixture_root[PATH_MAX];
	int written = snprintf(fixture_root,
		sizeof(fixture_root),
		"%s/tests/support/assets",
		PROJECT_SOURCE_DIR);
	assert_true(written > 0 && written < (int)sizeof(fixture_root));

	char character_path[PATH_MAX];
	written = snprintf(character_path,
		sizeof(character_path),
		"%s/tests/support/assets/bots/character_cache_retained_tmp_c.c",
		PROJECT_SOURCE_DIR);
	assert_true(written > 0 && written < (int)sizeof(character_path));

	char item_path[PATH_MAX];
	written = snprintf(item_path,
		sizeof(item_path),
		"%s/tests/support/assets/bots/character_cache_retained_tmp_i.w",
		PROJECT_SOURCE_DIR);
	assert_true(written > 0 && written < (int)sizeof(item_path));

	char weapon_path[PATH_MAX];
	written = snprintf(weapon_path,
		sizeof(weapon_path),
		"%s/tests/support/assets/bots/character_cache_retained_tmp_w.w",
		PROJECT_SOURCE_DIR);
	assert_true(written > 0 && written < (int)sizeof(weapon_path));

	write_character_fixture(item_path,
		"weight \"weapon_rocketlauncher\"\n"
		"{\n"
		"return 31;\n"
		"}\n");
	write_character_fixture(weapon_path,
		"weight \"Blaster\"\n"
		"{\n"
		"return 23;\n"
		"}\n");
	write_character_fixture(character_path,
		"character \"cacheprobe\"\n"
		"{\n"
		"0 \"Cache Probe\"\n"
		"3 \"male\"\n"
		"5 \"bots/character_cache_retained_tmp_w.w\"\n"
		"12 \"bots/babe_t.c\"\n"
		"13 \"babe\"\n"
		"28 \"bots/character_cache_retained_tmp_i.w\"\n"
		"}\n");

	env->weapon_library = AI_LoadWeaponLibrary(NULL);
	assert_non_null(env->weapon_library);
	assert_string_equal(env->weapon_library->source_path, weapon_config_path);

	LibVarSet("gladiator_asset_dir", fixture_root);
	LibVarSet("bot_reloadcharacters", "0");

	ai_character_profile_t *first = AI_LoadCharacter(character_path, 1.0f);
	assert_non_null(first);
	bot_weight_config_t *first_item = AI_ItemWeightsForCharacter(first);
	ai_weapon_weights_t *first_weapon = AI_WeaponWeightsForCharacter(first);
	assert_non_null(first_item);
	assert_non_null(first_weapon);
	assert_non_null(first_weapon->config);

	assert_int_equal(remove(item_path), 0);
	assert_int_equal(remove(weapon_path), 0);

	ai_character_profile_t *second = AI_LoadCharacter(character_path, 1.0f);
	assert_non_null(second);
	assert_ptr_equal(AI_ItemWeightsForCharacter(second), first_item);
	ai_weapon_weights_t *second_weapon = AI_WeaponWeightsForCharacter(second);
	assert_non_null(second_weapon);
	assert_ptr_equal(second_weapon->config, first_weapon->config);

	AI_FreeCharacter(second);
	AI_FreeCharacter(first);
	assert_int_equal(remove(character_path), 0);
}

static void test_bot_character_exports(void **state)
{
    test_environment_t *env = (test_environment_t *)(*state);
    assert_non_null(env);

    char weapon_config_path[PATH_MAX];
    asset_path_or_skip("dev_tools/assets/weapons.c", weapon_config_path, sizeof(weapon_config_path));

    env->weapon_library = AI_LoadWeaponLibrary(NULL);
    assert_non_null(env->weapon_library);
    assert_string_equal(env->weapon_library->source_path, weapon_config_path);

    test_reset_log();
    int handle = BotLoadCharacter("bots/babe_c.c", 1.0f);
    assert_true(handle > 0);
    assert_true(test_log_contains("loaded bot character"));

    ai_character_profile_t *profile = BotCharacterFromHandle(handle);
    assert_non_null(profile);

    test_reset_log();
    const char *missing_string = AI_CharacteristicAsString(profile, AI_CharacteristicCount(profile) + 3);
    assert_non_null(missing_string);
    assert_string_equal(missing_string, "");
    assert_true(test_log_contains("does not exist"));

    test_reset_log();
    const char *wrong_string = AI_CharacteristicAsString(profile, CHARACTERISTIC_CHAT_CPM);
    assert_null(wrong_string);
    assert_true(test_log_contains("not a string"));

    test_reset_log();
    assert_true(fabsf(AI_CharacteristicAsFloat(profile, CHARACTERISTIC_CHAT_FILE)) < 0.0001f);
    assert_true(test_log_contains("not a float"));

    test_reset_log();
    assert_int_equal(AI_CharacteristicAsInteger(profile, CHARACTERISTIC_CHAT_FILE), 0);
    assert_true(test_log_contains("not a integer"));

    float aggression = Characteristic_Float(handle, CHARACTERISTIC_AGGRESSION);
    assert_true(fabsf(aggression - 0.7f) < 0.0001f);

    int chat_cpm = Characteristic_Integer(handle, CHARACTERISTIC_CHAT_CPM);
    assert_int_equal(chat_cpm, 400);

    char chat_file[64];
    Characteristic_String(handle, CHARACTERISTIC_CHAT_FILE, chat_file, sizeof(chat_file));
    assert_string_equal(chat_file, "bots/babe_t.c");

    test_reset_log();
    int cached_handle = BotLoadCharacter("bots/babe_c.c", 1.0f);
    assert_int_equal(cached_handle, handle);
    assert_true(test_log_contains("reusing cached character"));

	test_reset_log();
    BotFreeCharacter(handle);
    assert_non_null(BotCharacterFromHandle(cached_handle));
	assert_true(test_log_contains("retained cached character"));

    BotFreeCharacter(cached_handle);
    assert_non_null(BotCharacterFromHandle(handle));

	LibVarSet("bot_reloadcharacters", "1");
	BotFreeCharacter(handle);
	assert_null(BotCharacterFromHandle(handle));

	test_reset_log();
	int reload_first = BotLoadCharacter("bots/babe_c.c", 1.0f);
	assert_true(reload_first > 0);
	int reload_second = BotLoadCharacter("bots/babe_c.c", 1.0f);
	assert_true(reload_second > 0);
	assert_int_not_equal(reload_first, reload_second);
	assert_true(test_log_contains("loaded bot character"));

	BotFreeCharacter(reload_first);
	assert_null(BotCharacterFromHandle(reload_first));
	BotFreeCharacter(reload_second);
	assert_null(BotCharacterFromHandle(reload_second));
	LibVarSet("bot_reloadcharacters", "0");

    test_reset_log();
    int missing_handle = BotLoadCharacter("bots/does_not_exist.c", 1.0f);
    assert_int_equal(missing_handle, 0);
    assert_true(test_log_contains("couldn't load bot character"));

    test_reset_log();
    int named_handle = BotLoadNamedCharacter("bots/babe_c.c", "babe", 1.0f);
    assert_true(named_handle > 0);
    assert_true(test_log_contains("loaded bot character"));
    BotFreeCharacter(named_handle);

    test_reset_log();
    int wrong_name_handle = BotLoadNamedCharacter("bots/babe_c.c", "Babe", 1.0f);
    assert_int_equal(wrong_name_handle, 0);
    assert_true(test_log_contains("couldn't find character Babe"));
}

/*
=============
test_q3_skill_character_exports

Confirms Q3 skill-block profiles load exactly and interpolate through the export cache.
=============
*/
static void test_q3_skill_character_exports(void **state)
{
	test_environment_t *env = (test_environment_t *)(*state);
	assert_non_null(env);

	char skill_character_path[PATH_MAX];
	asset_path_or_skip("tests/support/assets/bots/q3_skill_c.c",
		skill_character_path,
		sizeof(skill_character_path));

	assert_true(AI_CharacterFileUsesSkillBlocks(skill_character_path));

	int near_anchor_handle = BotLoadCharacter(skill_character_path, 4.005f);
	assert_true(near_anchor_handle > 0);
	assert_true(fabsf(Characteristic_Float(near_anchor_handle, CHARACTERISTIC_ATTACK_SKILL) - 0.7015f) < 0.0001f);
	assert_true(fabsf(Characteristic_Float(near_anchor_handle, CHARACTERISTIC_AIM_SKILL) - 0.801f) < 0.0001f);
	assert_int_equal(Characteristic_Integer(near_anchor_handle, CHARACTERISTIC_CHAT_CPM), 250);

	int exact_handle = BotLoadCharacterSkill(skill_character_path, 4.0f);
	assert_true(exact_handle > 0);

	ai_character_profile_t *exact = BotCharacterFromHandle(exact_handle);
	assert_non_null(exact);
	assert_null(AI_ItemWeightsForCharacter(exact));
	assert_null(AI_WeaponWeightsForCharacter(exact));
	assert_string_equal(AI_CharacteristicAsString(exact, CHARACTERISTIC_NAME), "Q3 Skill Four");
	assert_true(fabsf(Characteristic_Float(exact_handle, CHARACTERISTIC_ATTACK_SKILL) - 0.7f) < 0.0001f);
	assert_int_equal(Characteristic_Integer(exact_handle, CHARACTERISTIC_CHAT_CPM), 250);
	assert_int_equal(AI_CharacteristicCount(exact), 80);

	char hidden_sentinel[32];
	memset(hidden_sentinel, 'x', sizeof(hidden_sentinel));
	test_reset_log();
	Characteristic_String(exact_handle, 80, hidden_sentinel, sizeof(hidden_sentinel));
	assert_string_equal(hidden_sentinel, "");
	assert_true(test_log_contains("characteristic 80 does not exist"));

	test_reset_log();
	int cached_exact = BotLoadCharacterSkill(skill_character_path, 4.0f);
	assert_int_equal(cached_exact, exact_handle);
	assert_true(test_log_contains("reusing cached character"));

	LibVarSet("bot_reloadcharacters", "1");
	int reload_exact = BotLoadCharacterSkill(skill_character_path, 4.0f);
	assert_true(reload_exact > 0);
	assert_int_not_equal(reload_exact, exact_handle);
	BotFreeCharacter(reload_exact);
	assert_null(BotCharacterFromHandle(reload_exact));
	LibVarSet("bot_reloadcharacters", "0");

	int clamped_low = BotLoadCharacter(skill_character_path, 0.25f);
	assert_true(clamped_low > 0);
	ai_character_profile_t *low = BotCharacterFromHandle(clamped_low);
	assert_non_null(low);
	assert_string_equal(AI_CharacteristicAsString(low, CHARACTERISTIC_NAME), "Q3 Skill One");
	assert_true(fabsf(Characteristic_Float(clamped_low, CHARACTERISTIC_ATTACK_SKILL) - 0.1f) < 0.0001f);
	assert_int_equal(Characteristic_Integer(clamped_low, CHARACTERISTIC_CHAT_CPM), 100);

	int clamped_high = BotLoadCharacter(skill_character_path, 6.0f);
	assert_true(clamped_high > 0);
	ai_character_profile_t *high = BotCharacterFromHandle(clamped_high);
	assert_non_null(high);
	assert_string_equal(AI_CharacteristicAsString(high, CHARACTERISTIC_NAME), "Q3 Skill Five");
	assert_true(fabsf(Characteristic_Float(clamped_high, CHARACTERISTIC_ATTACK_SKILL) - 1.0f) < 0.0001f);
	assert_int_equal(Characteristic_Integer(clamped_high, CHARACTERISTIC_CHAT_CPM), 400);

	int interpolated_handle = BotLoadCharacter(skill_character_path, 2.5f);
	assert_true(interpolated_handle > 0);

	ai_character_profile_t *interpolated = BotCharacterFromHandle(interpolated_handle);
	assert_non_null(interpolated);
	assert_true(fabsf(Characteristic_Float(interpolated_handle, CHARACTERISTIC_ATTACK_SKILL) - 0.4f) < 0.0001f);
	assert_true(fabsf(Characteristic_Float(interpolated_handle, CHARACTERISTIC_AIM_SKILL) - 0.5f) < 0.0001f);
	assert_int_equal(Characteristic_Integer(interpolated_handle, CHARACTERISTIC_CHAT_CPM), 100);
	assert_string_equal(AI_CharacteristicAsString(interpolated, CHARACTERISTIC_CHAT_NAME), "skillone");

	test_reset_log();
	LibVarSet("bot_reloadcharacters", "1");
	int reload_interpolated = BotLoadCharacter(skill_character_path, 2.5f);
	assert_int_equal(reload_interpolated, interpolated_handle);
	assert_true(test_log_contains("reusing cached character"));
	BotFreeCharacter(reload_interpolated);
	assert_null(BotCharacterFromHandle(interpolated_handle));
	LibVarSet("bot_reloadcharacters", "0");

	BotFreeCharacter(near_anchor_handle);
	BotFreeCharacter(clamped_high);
	BotFreeCharacter(clamped_low);
	BotFreeCharacter(exact_handle);
}

/*
=============
test_q3_skill_fallback_cache_exports

Confirms Q3 fallback skills are cached by the file and skill that actually loaded.
=============
*/
static void test_q3_skill_fallback_cache_exports(void **state)
{
	test_environment_t *env = (test_environment_t *)(*state);
	assert_non_null(env);

	char sparse_character_path[PATH_MAX];
	asset_path_or_skip("tests/support/assets/bots/q3_sparse_c.c",
		sparse_character_path,
		sizeof(sparse_character_path));

	assert_true(AI_CharacterFileUsesSkillBlocks(sparse_character_path));

	int first_fallback = BotLoadCharacterSkill(sparse_character_path, 3.0f);
	assert_true(first_fallback > 0);

	ai_character_profile_t *first = BotCharacterFromHandle(first_fallback);
	assert_non_null(first);
	assert_string_equal(AI_CharacteristicAsString(first, CHARACTERISTIC_NAME), "Q3 Sparse Skill One");
	assert_true(fabsf(Characteristic_Float(first_fallback, CHARACTERISTIC_ATTACK_SKILL) - 0.15f) < 0.0001f);
	assert_int_equal(Characteristic_Integer(first_fallback, CHARACTERISTIC_CHAT_CPM), 90);

	test_reset_log();
	int second_fallback = BotLoadCharacterSkill(sparse_character_path, 4.0f);
	assert_int_equal(second_fallback, first_fallback);
	assert_true(test_log_contains("reusing cached character"));

	BotFreeCharacter(second_fallback);
	BotFreeCharacter(first_fallback);
}

/*
=============
test_q3_asset_character_exports

Confirms native Q3 skill files load through includes, defaults, and interpolation.
=============
*/
static void test_q3_asset_character_exports(void **state)
{
	test_environment_t *env = (test_environment_t *)(*state);
	assert_non_null(env);

	char q3_botfiles_root[PATH_MAX];
	q3_botfiles_root_or_skip(q3_botfiles_root, sizeof(q3_botfiles_root));

	LibVarSet("basedir", q3_botfiles_root);
	LibVarSet("gamedir", "");
	LibVarSet("cddir", "");
	LibVarSet("gladiator_asset_dir", "");

	assert_true(AI_CharacterFileUsesSkillBlocks("bots/xian_c.c"));

	test_reset_log();
	int missing_default_handle = BotLoadCharacterSkill("bots/not_a_real_character.c", 4.0f);
	assert_true(missing_default_handle > 0);
	assert_true(test_log_contains("reusing cached character bots/default_c.c"));

	ai_character_profile_t *missing_default = BotCharacterFromHandle(missing_default_handle);
	assert_non_null(missing_default);
	assert_string_equal(AI_CharacterProfileFilename(missing_default), "bots/default_c.c");
	assert_string_equal(AI_CharacteristicAsString(missing_default, Q3_CHARACTERISTIC_NAME), "Player");
	assert_string_equal(AI_CharacteristicAsString(missing_default, Q3_CHARACTERISTIC_ITEMWEIGHTS), "bots/daemia_i.c");
	assert_true(fabsf(Characteristic_Float(missing_default_handle, Q3_CHARACTERISTIC_ATTACK_SKILL) - 1.0f) < 0.0001f);

	int direct_unclamped_handle = BotLoadCharacterSkill("bots/xian_c.c", 6.0f);
	assert_true(direct_unclamped_handle > 0);

	ai_character_profile_t *direct_unclamped = BotCharacterFromHandle(direct_unclamped_handle);
	assert_non_null(direct_unclamped);
	assert_string_equal(AI_CharacterProfileFilename(direct_unclamped), "bots/xian_c.c");
	assert_string_equal(AI_CharacteristicAsString(direct_unclamped, Q3_CHARACTERISTIC_NAME), "Xian");
	assert_true(fabsf(AI_CharacterProfileSkill(direct_unclamped) - 1.0f) < 0.0001f);
	assert_true(fabsf(Characteristic_Float(direct_unclamped_handle, Q3_CHARACTERISTIC_ATTACK_SKILL) - 1.0f) < 0.0001f);

	LibVarSet("bot_reloadcharacters", "1");
	BotFreeCharacter(direct_unclamped_handle);
	assert_null(BotCharacterFromHandle(direct_unclamped_handle));
	LibVarSet("bot_reloadcharacters", "0");

	int exact_handle = BotLoadCharacterSkill("bots/xian_c.c", 4.0f);
	assert_true(exact_handle > 0);

	ai_character_profile_t *exact = BotCharacterFromHandle(exact_handle);
	assert_non_null(exact);
	assert_null(AI_ItemWeightsForCharacter(exact));
	assert_null(AI_WeaponWeightsForCharacter(exact));
	assert_string_equal(AI_CharacteristicAsString(exact, Q3_CHARACTERISTIC_NAME), "Xian");
	assert_string_equal(AI_CharacteristicAsString(exact, Q3_CHARACTERISTIC_GENDER), "male");
	assert_string_equal(AI_CharacteristicAsString(exact, Q3_CHARACTERISTIC_ITEMWEIGHTS), "bots/xian_i.c");
	assert_true(fabsf(Characteristic_Float(exact_handle, Q3_CHARACTERISTIC_ATTACK_SKILL) - 0.75f) < 0.0001f);
	assert_int_equal(Characteristic_Integer(exact_handle, Q3_CHARACTERISTIC_CHAT_CPM), 400);
	assert_true(fabsf(Characteristic_Float(exact_handle, Q3_CHARACTERISTIC_WALKER)) < 0.0001f);

	int cadaver_any_handle = BotLoadCharacterSkill("bots/cadaver_c.c", 3.0f);
	assert_true(cadaver_any_handle > 0);

	ai_character_profile_t *cadaver_any = BotCharacterFromHandle(cadaver_any_handle);
	assert_non_null(cadaver_any);
	assert_string_equal(AI_CharacterProfileFilename(cadaver_any), "bots/cadaver_c.c");
	assert_string_equal(AI_CharacteristicAsString(cadaver_any, Q3_CHARACTERISTIC_NAME), "Cadaver");
	assert_true(fabsf(Characteristic_Float(cadaver_any_handle, Q3_CHARACTERISTIC_ATTACK_SKILL) - 0.5f) < 0.0001f);

	test_reset_log();
	int cached_cadaver_any = BotLoadCharacterSkill("bots/cadaver_c.c", 3.0f);
	assert_int_equal(cached_cadaver_any, cadaver_any_handle);
	assert_true(test_log_contains("reusing cached character bots/cadaver_c.c"));
	assert_false(test_log_contains("loaded skill 1 from bots/cadaver_c.c"));

	int interpolated_handle = BotLoadCharacter("bots/xian_c.c", 2.5f);
	assert_true(interpolated_handle > 0);

	ai_character_profile_t *interpolated = BotCharacterFromHandle(interpolated_handle);
	assert_non_null(interpolated);
	assert_string_equal(AI_CharacteristicAsString(interpolated, Q3_CHARACTERISTIC_CHAT_NAME), "xian");
	assert_true(fabsf(Characteristic_Float(interpolated_handle, Q3_CHARACTERISTIC_ATTACK_SKILL) - 0.5f) < 0.0001f);
	assert_true(fabsf(Characteristic_Float(interpolated_handle, Q3_CHARACTERISTIC_AIM_SKILL) - 0.5f) < 0.0001f);
	assert_int_equal(Characteristic_Integer(interpolated_handle, Q3_CHARACTERISTIC_CHAT_CPM), 400);

	int fractional_missing_default = BotLoadCharacter("bots/fractional_missing_character.c", 2.5f);
	assert_true(fractional_missing_default > 0);

	ai_character_profile_t *fractional_default = BotCharacterFromHandle(fractional_missing_default);
	assert_non_null(fractional_default);
	assert_string_equal(AI_CharacterProfileFilename(fractional_default), "bots/default_c.c");
	assert_string_equal(AI_CharacteristicAsString(fractional_default, Q3_CHARACTERISTIC_NAME), "Player");
	assert_true(fabsf(Characteristic_Float(fractional_missing_default, Q3_CHARACTERISTIC_ATTACK_SKILL) - 0.625f) < 0.0001f);
	assert_true(fabsf(Characteristic_Float(fractional_missing_default, Q3_CHARACTERISTIC_AIM_SKILL) - 0.625f) < 0.0001f);
	assert_int_equal(Characteristic_Integer(fractional_missing_default, Q3_CHARACTERISTIC_CHAT_CPM), 400);

	test_reset_log();
	int public_missing_default = BotLoadCharacter("bots/another_missing_character.c", 4.0f);
	assert_int_equal(public_missing_default, missing_default_handle);
	assert_true(test_log_contains("reusing cached character"));

	BotFreeCharacter(public_missing_default);
	BotFreeCharacter(cached_cadaver_any);
	BotFreeCharacter(cadaver_any_handle);
	BotFreeCharacter(missing_default_handle);
	BotFreeCharacter(fractional_missing_default);
	BotFreeCharacter(interpolated_handle);
	BotFreeCharacter(exact_handle);
}

static void test_bot_setup_client_exposes_profile(void **state)
{
    test_environment_t *env = (test_environment_t *)(*state);
    assert_non_null(env);
    assert_non_null(env->exports);

	test_reset_log();
	assert_int_equal(env->exports->BotLoadCharacter("bots/babe_c.c", 1.0f), 0);
	assert_true(test_log_contains("BotLoadCharacter: library not initialised"));

	char guarded_string[16];
	memset(guarded_string, 'x', sizeof(guarded_string));
	env->exports->Characteristic_String(1, CHARACTERISTIC_NAME, guarded_string, sizeof(guarded_string));
	assert_string_equal(guarded_string, "");

    char weapon_config_path[PATH_MAX];
    asset_path_or_skip("dev_tools/assets/weapons.c", weapon_config_path, sizeof(weapon_config_path));

    env->exports->BotLibVarSet("basedir", env->assets.asset_root);
    env->exports->BotLibVarSet("gamedir", "");
    env->exports->BotLibVarSet("cddir", "");
    env->exports->BotLibVarSet("gladiator_asset_dir", "");
    env->exports->BotLibVarSet("weaponconfig", "weapons.c");
    env->exports->BotLibVarSet("itemconfig", "items.c");
    env->exports->BotLibVarSet("max_weaponinfo", "64");
    env->exports->BotLibVarSet("max_projectileinfo", "64");

    int status = env->exports->BotSetupLibrary();
    assert_int_equal(status, BLERR_NOERROR);
    env->library_setup = true;
	assert_int_equal(BotState_ClientCapacity(), 4);
	assert_int_equal(BotState_ActiveClientCount(), 0);
	const bot_clientsettings_t *initial_client_settings = BotState_ClientSettings(0);
	assert_non_null(initial_client_settings);
	assert_string_equal(initial_client_settings->netname, "");
	assert_string_equal(initial_client_settings->skin, "");

	int exported_handle = env->exports->BotLoadCharacter("bots/babe_c.c", 1.0f);
	assert_true(exported_handle > 0);

	char exported_name[64];
	env->exports->Characteristic_String(exported_handle,
		CHARACTERISTIC_NAME,
		exported_name,
		sizeof(exported_name));
	assert_string_equal(exported_name, "Silicon Babe");
	env->exports->BotFreeCharacter(exported_handle);

    bot_settings_t settings;
    memset(&settings, 0, sizeof(settings));
    snprintf(settings.characterfile, sizeof(settings.characterfile), "bots/babe_c.c");
    snprintf(settings.charactername, sizeof(settings.charactername), "babe");

	bot_clientsettings_t live_client_settings;
	memset(&live_client_settings, 0, sizeof(live_client_settings));
	snprintf(live_client_settings.netname, sizeof(live_client_settings.netname), "Spawn Babe");
	snprintf(live_client_settings.skin, sizeof(live_client_settings.skin), "female/athena");
	status = env->exports->BotClientSettings(0, &live_client_settings);
	assert_int_equal(status, BLERR_NOERROR);
	assert_string_equal(BotState_ClientSettings(0)->netname, "Spawn Babe");
	assert_string_equal(BotState_ClientName(0), "Spawn Babe");
	assert_string_equal(BotState_ClientSkin(0), "female/athena");
	assert_int_equal(BotState_FindClientByName("Spawn Babe"), 0);
	assert_string_equal(BotState_ClientName(MAX_CLIENTS), "");
	assert_string_equal(BotState_ClientSkin(MAX_CLIENTS), "");

    test_reset_log();
    status = env->exports->BotSetupClient(0, &settings);
    assert_int_equal(status, BLERR_NOERROR);
	assert_int_equal(BotState_ActiveClientCount(), 1);
    assert_true(test_log_contains("bytes weapon index"));
    assert_true(test_log_contains("bytes item index"));
    env->client_active = true;

    bot_client_state_t *state_slot = BotState_Get(0);
    assert_non_null(state_slot);
    assert_true(state_slot->active);
    assert_true(state_slot->character_handle > 0);

    struct ai_character_profile_s *profile = (struct ai_character_profile_s *)state_slot->character;
    assert_non_null(profile);
    assert_non_null(profile->item_weights);
    assert_non_null(profile->weapon_weights);
    assert_non_null(profile->chat_state);

    assert_ptr_equal(profile->item_weights, state_slot->item_weights);
    assert_ptr_equal(profile->weapon_weights, state_slot->weapon_weights);
    assert_ptr_equal(profile->chat_state, state_slot->chat_state);

	bot_weapon_info_t blaster;
	memset(&blaster, 0, sizeof(blaster));
	env->exports->BotGetWeaponInfo(state_slot->weapon_state, 0, &blaster);
	assert_string_equal(blaster.name, "Blaster");

	bot_weapon_info_t machinegun;
	memset(&machinegun, 0, sizeof(machinegun));
	env->exports->BotGetWeaponInfo(state_slot->weapon_state, 3, &machinegun);
	assert_string_equal(machinegun.name, "Machinegun");
	assert_true(machinegun.weaponindex > 0 && machinegun.weaponindex < MAX_ITEMS);
	assert_true(machinegun.ammoindex > 0 && machinegun.ammoindex < MAX_ITEMS);

	int inventory[MAX_ITEMS];
	memset(inventory, 0, sizeof(inventory));
	if (blaster.weaponindex > 0 && blaster.weaponindex < MAX_ITEMS)
	{
		inventory[blaster.weaponindex] = 1;
	}
	inventory[machinegun.weaponindex] = 1;
	inventory[machinegun.ammoindex] = 50;
	assert_int_equal(env->exports->BotChooseBestFightWeapon(state_slot->weapon_state, inventory), 3);

    assert_true(BotChat_HasSynonymPhrase((bot_chatstate_t *)profile->chat_state,
                                         "CONTEXT_NEARBYITEM",
                                         "Shotgun"));
	assert_string_equal(BotChatName((bot_chatstate_t *)profile->chat_state), "babe");
	assert_int_equal(BotChatClient((bot_chatstate_t *)profile->chat_state), 0);

    assert_string_equal(state_slot->client_settings.netname, "Spawn Babe");
	assert_string_equal(state_slot->client_settings.skin, "female/athena");

	memset(&live_client_settings, 0, sizeof(live_client_settings));
	snprintf(live_client_settings.netname, sizeof(live_client_settings.netname), "Live Babe");
	snprintf(live_client_settings.skin, sizeof(live_client_settings.skin), "female/venus");
	status = env->exports->BotClientSettings(0, &live_client_settings);
	assert_int_equal(status, BLERR_NOERROR);
	assert_string_equal(state_slot->client_settings.netname, "Live Babe");
	assert_string_equal(state_slot->client_settings.skin, "female/venus");
	assert_string_equal(BotState_ClientName(0), "Live Babe");
	assert_string_equal(BotState_ClientSkin(0), "female/venus");

	bot_settings_t updated_settings = settings;
	snprintf(updated_settings.ailibrary, sizeof(updated_settings.ailibrary), "gladiator.dll");
	status = env->exports->BotSettings(0, &updated_settings);
	assert_int_equal(status, BLERR_NOERROR);
	assert_string_equal(state_slot->settings.ailibrary, "gladiator.dll");

	test_reset_client_commands();
	BotState_EmitPendingClientCommands(state_slot);
	bot_input_t command_input;
	memset(&command_input, 0, sizeof(command_input));
	status = EA_GetInput(0, 0.05f, &command_input);
	assert_int_equal(status, BLERR_NOERROR);
	assert_int_equal(g_test_client_commands.count, 1);
	assert_int_equal(g_test_client_commands.entries[0].client, 0);
	assert_string_equal(g_test_client_commands.entries[0].text, "gender female");

	test_reset_client_commands();
	BotState_EmitPendingClientCommands(state_slot);
	memset(&command_input, 0, sizeof(command_input));
	status = EA_GetInput(0, 0.05f, &command_input);
	assert_int_equal(status, BLERR_NOERROR);
	assert_int_equal(g_test_client_commands.count, 0);

	status = env->exports->BotShutdownClient(0);
	assert_int_equal(status, BLERR_NOERROR);
	assert_int_equal(BotState_ActiveClientCount(), 0);
	env->client_active = false;

	env->exports->BotLibVarSet("altnames", "1");
	status = env->exports->BotSetupClient(0, &settings);
	assert_int_equal(status, BLERR_NOERROR);
	assert_int_equal(BotState_ActiveClientCount(), 1);
	env->client_active = true;

	state_slot = BotState_Get(0);
	assert_non_null(state_slot);
	assert_true(state_slot->active);
	assert_string_equal(state_slot->client_settings.netname, "Live Babe");
	profile = (struct ai_character_profile_s *)state_slot->character;
	assert_non_null(profile);
	assert_non_null(profile->chat_state);
	assert_string_equal(BotChatName((bot_chatstate_t *)profile->chat_state), "babe");
	assert_int_equal(BotChatClient((bot_chatstate_t *)profile->chat_state), 0);

	memset(&live_client_settings, 0, sizeof(live_client_settings));
	snprintf(live_client_settings.netname, sizeof(live_client_settings.netname), "Epsilon");
	snprintf(live_client_settings.skin, sizeof(live_client_settings.skin), "female/venus");
	status = env->exports->BotClientSettings(0, &live_client_settings);
	assert_int_equal(status, BLERR_NOERROR);
	assert_string_equal(state_slot->client_settings.netname, "Epsilon");

	test_reset_client_commands();
	BotState_EmitPendingClientCommands(state_slot);
	memset(&command_input, 0, sizeof(command_input));
	status = EA_GetInput(0, 0.05f, &command_input);
	assert_int_equal(status, BLERR_NOERROR);
	assert_int_equal(g_test_client_commands.count, 2);
	assert_int_equal(g_test_client_commands.entries[0].client, 0);
	assert_string_equal(g_test_client_commands.entries[0].text, "gender female");
	assert_int_equal(g_test_client_commands.entries[1].client, 0);
	assert_string_equal(g_test_client_commands.entries[1].text, "name Epsilon");

	memset(&live_client_settings, 0, sizeof(live_client_settings));
	snprintf(live_client_settings.netname, sizeof(live_client_settings.netname), "Moved Babe");
	snprintf(live_client_settings.skin, sizeof(live_client_settings.skin), "female/phoenix");
	status = env->exports->BotClientSettings(1, &live_client_settings);
	assert_int_equal(status, BLERR_NOERROR);

	status = env->exports->BotMoveClient(0, 1);
	assert_int_equal(status, BLERR_NOERROR);
	assert_int_equal(BotState_ActiveClientCount(), 1);
	env->client_active = false;
	assert_null(BotState_Get(0));
	state_slot = BotState_Get(1);
	assert_non_null(state_slot);
	assert_int_equal(state_slot->client_number, 1);
	assert_string_equal(state_slot->client_settings.netname, "Moved Babe");
	assert_string_equal(state_slot->client_settings.skin, "female/phoenix");
	assert_string_equal(BotState_ClientName(1), "Moved Babe");
	assert_string_equal(BotState_ClientSkin(1), "female/phoenix");
	assert_int_equal(BotState_FindClientByName("Moved Babe"), 1);
	profile = (struct ai_character_profile_s *)state_slot->character;
	assert_non_null(profile);
	assert_non_null(profile->chat_state);
	assert_string_equal(BotChatName((bot_chatstate_t *)profile->chat_state), "babe");
	assert_int_equal(BotChatClient((bot_chatstate_t *)profile->chat_state), 1);

	status = env->exports->BotShutdownClient(1);
	assert_int_equal(status, BLERR_NOERROR);
	assert_int_equal(BotState_ActiveClientCount(), 0);

	status = env->exports->BotShutdownLibrary();
	assert_int_equal(status, BLERR_NOERROR);
	env->library_setup = false;
	assert_int_equal(BotState_ClientCapacity(), 0);
	assert_int_equal(BotState_ActiveClientCount(), 0);

	status = env->exports->BotSetupLibrary();
	assert_int_equal(status, BLERR_NOERROR);
	env->library_setup = true;
	assert_int_equal(BotState_ClientCapacity(), 4);
	assert_int_equal(BotState_ActiveClientCount(), 0);
	const bot_clientsettings_t *reset_client_settings = BotState_ClientSettings(0);
	assert_non_null(reset_client_settings);
	assert_string_equal(reset_client_settings->netname, "");
	assert_string_equal(reset_client_settings->skin, "");
	assert_string_equal(BotState_ClientName(0), "");
	assert_string_equal(BotState_ClientSkin(0), "");
}

/*
=============
test_bot_state_map_reset_preserves_character_wiring

Pins the HLIL map-load reset helper that preserves character-owned resources.
=============
*/
static void test_bot_state_map_reset_preserves_character_wiring(void **state)
{
	test_environment_t *env = (test_environment_t *)(*state);
	assert_non_null(env);
	assert_non_null(env->exports);

	char weapon_config_path[PATH_MAX];
	asset_path_or_skip("dev_tools/assets/weapons.c", weapon_config_path, sizeof(weapon_config_path));

	env->exports->BotLibVarSet("basedir", env->assets.asset_root);
	env->exports->BotLibVarSet("gamedir", "");
	env->exports->BotLibVarSet("cddir", "");
	env->exports->BotLibVarSet("gladiator_asset_dir", "");
	env->exports->BotLibVarSet("weaponconfig", "weapons.c");
	env->exports->BotLibVarSet("itemconfig", "items.c");
	env->exports->BotLibVarSet("max_weaponinfo", "64");
	env->exports->BotLibVarSet("max_projectileinfo", "64");

	int status = env->exports->BotSetupLibrary();
	assert_int_equal(status, BLERR_NOERROR);
	env->library_setup = true;

	bot_clientsettings_t live_client_settings;
	memset(&live_client_settings, 0, sizeof(live_client_settings));
	snprintf(live_client_settings.netname, sizeof(live_client_settings.netname), "Reset Babe");
	snprintf(live_client_settings.skin, sizeof(live_client_settings.skin), "female/athena");
	status = env->exports->BotClientSettings(0, &live_client_settings);
	assert_int_equal(status, BLERR_NOERROR);

	bot_settings_t settings;
	memset(&settings, 0, sizeof(settings));
	snprintf(settings.characterfile, sizeof(settings.characterfile), "bots/babe_c.c");
	snprintf(settings.charactername, sizeof(settings.charactername), "babe");

	status = env->exports->BotSetupClient(0, &settings);
	assert_int_equal(status, BLERR_NOERROR);
	env->client_active = true;

	bot_client_state_t *state_slot = BotState_Get(0);
	assert_non_null(state_slot);
	assert_true(state_slot->active);

	ai_character_profile_t *character = state_slot->character;
	bot_weight_config_t *item_weights = state_slot->item_weights;
	ai_weapon_weights_t *weapon_weights = state_slot->weapon_weights;
	bot_chatstate_t *chat_state = (bot_chatstate_t *)state_slot->chat_state;
	ai_goal_state_t *goal_state = state_slot->goal_state;
	ai_move_state_t *move_state = state_slot->move_state;
	ai_dm_state_t *dm_state = state_slot->dm_state;
	bot_movestate_t *move_handle_state = BotMoveStateFromHandle(state_slot->move_handle);
	assert_non_null(character);
	assert_non_null(item_weights);
	assert_non_null(weapon_weights);
	assert_non_null(chat_state);
	assert_non_null(goal_state);
	assert_non_null(move_state);
	assert_non_null(dm_state);
	assert_non_null(move_handle_state);

	bot_goal_t goal;
	memset(&goal, 0, sizeof(goal));
	goal.number = 77;
	goal.areanum = 3;
	assert_true(BotPushGoal(state_slot->goal_handle, &goal));
	bot_goal_t top_goal;
	memset(&top_goal, 0, sizeof(top_goal));
	assert_true(BotGetTopGoal(state_slot->goal_handle, &top_goal));
	assert_int_equal(top_goal.number, 77);

	state_slot->client_update_valid = true;
	state_slot->last_update_time = 123.0f;
	state_slot->goal_snapshot_count = 1;
	state_slot->goal_snapshot[0] = goal;
	state_slot->active_goal_number = 77;
	state_slot->current_weapon = 3;
	state_slot->client_commands_pending = true;
	state_slot->has_move_result = true;
	state_slot->last_move_result.type = RESULTTYPE_ELEVATORUP;
	state_slot->combat.current_enemy = 9;
	state_slot->combat.enemy_visible = true;
	state_slot->goal_state->active_goal.valid = true;
	state_slot->move_state->has_last_result = true;
	move_handle_state->client = 99;
	move_handle_state->areanum = 12;

	BotState_ResetForNewMap(state_slot);

	assert_true(state_slot->active);
	assert_int_equal(BotState_ActiveClientCount(), 1);
	assert_ptr_equal(state_slot->character, character);
	assert_ptr_equal(state_slot->item_weights, item_weights);
	assert_ptr_equal(state_slot->weapon_weights, weapon_weights);
	assert_ptr_equal(state_slot->chat_state, chat_state);
	assert_ptr_equal(state_slot->goal_state, goal_state);
	assert_ptr_equal(state_slot->move_state, move_state);
	assert_ptr_equal(state_slot->dm_state, dm_state);
	assert_ptr_equal(BotMoveStateFromHandle(state_slot->move_handle), move_handle_state);
	assert_string_equal(state_slot->settings.characterfile, "bots/babe_c.c");
	assert_string_equal(state_slot->client_settings.netname, "Reset Babe");
	assert_string_equal(BotChatName(chat_state), "babe");
	assert_int_equal(BotChatClient(chat_state), 0);
	assert_ptr_equal(AI_MoveState_GetAvoidList(state_slot->move_state),
		AI_GoalState_GetAvoidList(state_slot->goal_state));

	assert_false(state_slot->client_update_valid);
	assert_float_equal(state_slot->last_update_time, 0.0f, 0.0001f);
	assert_int_equal(state_slot->goal_snapshot_count, 0);
	assert_int_equal(state_slot->active_goal_number, 0);
	assert_int_equal(state_slot->current_weapon, 0);
	assert_false(state_slot->client_commands_pending);
	assert_false(state_slot->has_move_result);
	assert_int_equal(state_slot->last_move_result.type, 0);
	assert_int_equal(state_slot->combat.current_enemy, -1);
	assert_false(state_slot->combat.enemy_visible);
	assert_false(state_slot->goal_state->active_goal.valid);
	assert_false(state_slot->move_state->has_last_result);
	assert_int_equal(move_handle_state->client, 0);
	assert_int_equal(move_handle_state->areanum, 0);
	memset(&top_goal, 0, sizeof(top_goal));
	assert_false(BotGetTopGoal(state_slot->goal_handle, &top_goal));

	ai_dm_metrics_t dm_metrics;
	AI_DMState_GetMetrics(state_slot->dm_state, &dm_metrics);
	assert_int_equal(dm_metrics.enemy_entity, -1);

	status = env->exports->BotShutdownClient(0);
	assert_int_equal(status, BLERR_NOERROR);
	env->client_active = false;
	assert_int_equal(BotState_ActiveClientCount(), 0);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_babe_character_profile,
                                        character_profile_setup,
                                        character_profile_teardown),
        cmocka_unit_test_setup_teardown(test_character_weight_cache_uses_caller_filenames,
                                        character_profile_setup,
                                        character_profile_teardown),
        cmocka_unit_test_setup_teardown(test_bot_character_exports,
                                        character_profile_setup,
                                        character_profile_teardown),
        cmocka_unit_test_setup_teardown(test_q3_skill_character_exports,
                                        character_profile_setup,
                                        character_profile_teardown),
        cmocka_unit_test_setup_teardown(test_q3_skill_fallback_cache_exports,
                                        character_profile_setup,
                                        character_profile_teardown),
        cmocka_unit_test_setup_teardown(test_q3_asset_character_exports,
                                        character_profile_setup,
                                        character_profile_teardown),
        cmocka_unit_test_setup_teardown(test_bot_setup_client_exposes_profile,
                                        bot_setup_client_setup,
                                        bot_setup_client_teardown),
		cmocka_unit_test_setup_teardown(test_bot_state_map_reset_preserves_character_wiring,
										bot_setup_client_setup,
										bot_setup_client_teardown),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
