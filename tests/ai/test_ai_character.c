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

    test_reset_log();
    status = env->exports->BotSetupClient(0, &settings);
    assert_int_equal(status, BLERR_NOERROR);
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

    assert_true(BotChat_HasSynonymPhrase((bot_chatstate_t *)profile->chat_state,
                                         "CONTEXT_NEARBYITEM",
                                         "Shotgun"));

    assert_string_equal(state_slot->client_settings.netname, "Silicon Babe");
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_babe_character_profile,
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
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
