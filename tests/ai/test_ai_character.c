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

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#include "botlib/ai/character/bot_character.h"
#include "botlib/ai/chat/ai_chat.h"
#include "botlib/ai/weapon/bot_weapon.h"
#include "botlib/ai/weight/bot_weight.h"
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
    ai_weapon_library_t *weapon_library;
    bot_export_t *exports;
} test_environment_t;

typedef struct ai_character_definition_s ai_character_definition_t;

struct ai_character_profile_s {
    char character_filename[128];
    float requested_skill;
    bot_weight_config_t *item_weights;
    ai_weapon_weights_t *weapon_weights;
    void *chat_state;
    ai_character_definition_t *definition_blob;
};

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

    ai_character_profile_t *profile = AI_LoadCharacter("bots/babe_c.c", 1.0f);
    assert_non_null(profile);

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

    const bot_weapon_config_t *weapon_config = AI_GetWeaponConfig(env->weapon_library);
    assert_non_null(weapon_config);
    int rocket_index = weapon_index_by_name(weapon_config, "Rocket Launcher");
    assert_true(rocket_index >= 0);

    float rocket_weight = AI_WeaponWeightForClient(weapon_weights, rocket_index);
    assert_true(rocket_weight > 0.0f);

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

    BotFreeCharacter(handle);
    assert_non_null(BotCharacterFromHandle(cached_handle));

    BotFreeCharacter(cached_handle);
    assert_null(BotCharacterFromHandle(handle));

    test_reset_log();
    int missing_handle = BotLoadCharacter("bots/does_not_exist.c", 1.0f);
    assert_int_equal(missing_handle, 0);
    assert_true(test_log_contains("couldn't load bot character"));
}

static void test_bot_setup_client_exposes_profile(void **state)
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

    bot_settings_t settings;
    memset(&settings, 0, sizeof(settings));
    snprintf(settings.characterfile, sizeof(settings.characterfile), "bots/babe_c.c");
    snprintf(settings.charactername, sizeof(settings.charactername), "arena_tester");

    status = env->exports->BotSetupClient(0, &settings);
    assert_int_equal(status, BLERR_NOERROR);
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
        cmocka_unit_test_setup_teardown(test_bot_setup_client_exposes_profile,
                                        bot_setup_client_setup,
                                        bot_setup_client_teardown),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
