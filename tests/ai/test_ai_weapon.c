#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "botlib/ai/weapon/bot_weapon.h"
#include "botlib/common/l_libvar.h"
#include "botlib/common/l_memory.h"
#include "botlib/interface/botlib_interface.h"
#include "q2bridge/botlib.h"

#include "inv.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifndef PROJECT_SOURCE_DIR
#error "PROJECT_SOURCE_DIR must be defined so regression tests can resolve asset paths."
#endif

#define TEST_BOTLIB_HEAP_SIZE (1u << 20)
#define TEST_MAX_LOG_MESSAGES 32

typedef struct test_log_message_s {
    int priority;
    char text[256];
} test_log_message_t;

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

static void test_capture_print(int priority, const char *fmt, ...)
{
    if (g_test_log.count >= TEST_MAX_LOG_MESSAGES) {
        return;
    }

    va_list args;
    va_start(args, fmt);
    test_log_message_t *slot = &g_test_log.entries[g_test_log.count++];
    slot->priority = priority;
    vsnprintf(slot->text, sizeof(slot->text), fmt, args);
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

static const botlib_import_table_t g_test_imports = {
    .Print = test_capture_print,
    .DPrint = test_capture_dprint,
    .BotLibVarGet = test_libvar_get,
    .BotLibVarSet = test_libvar_set,
};

static void configure_asset_libvars(void)
{
    char asset_root[PATH_MAX];
    int written = snprintf(asset_root, sizeof(asset_root), "%s/dev_tools/assets", PROJECT_SOURCE_DIR);
    assert_true(written > 0 && (size_t)written < sizeof(asset_root));

    LibVarSet("basedir", asset_root);
    LibVarSet("gamedir", "");
    LibVarSet("cddir", "");
    LibVarSet("gladiator_asset_dir", "");
    LibVarSet("weaponconfig", "weapons.c");
    LibVarSet("itemconfig", "items.c");
    LibVarSet("max_weaponinfo", "64");
    LibVarSet("max_projectileinfo", "64");
}

static void setup_botlib_environment(void)
{
    test_reset_log();
    BotInterface_SetImportTable(&g_test_imports);
    LibVar_Init();
    configure_asset_libvars();
    assert_true(BotMemory_Init(TEST_BOTLIB_HEAP_SIZE));
}

static void teardown_botlib_environment(void)
{
    LibVar_Shutdown();
    BotMemory_Shutdown();
    BotInterface_SetImportTable(NULL);
}

static void asset_path_or_skip(const char *relative_path, char *out, size_t out_size)
{
    int written = snprintf(out, out_size, "%s/%s", PROJECT_SOURCE_DIR, relative_path);
    assert_true(written > 0 && (size_t)written < out_size);

    FILE *fp = fopen(out, "rb");
    if (fp == NULL) {
        cmocka_skip();
    }

    fclose(fp);
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

static void test_weapon_library_reports_expected_counts(void **state)
{
    (void)state;

    char weapon_config_path[512];
    asset_path_or_skip("dev_tools/assets/weapons.c", weapon_config_path, sizeof(weapon_config_path));

    setup_botlib_environment();
    ai_weapon_library_t *library = AI_LoadWeaponLibrary(NULL);
    assert_non_null(library);
    assert_string_equal(library->source_path, weapon_config_path);

    const bot_weapon_config_t *config = AI_GetWeaponConfig(library);
    assert_non_null(config);

    assert_int_equal(config->num_weapons, 20);
    assert_int_equal(config->num_projectiles, 20);

    AI_UnloadWeaponLibrary(library);
    teardown_botlib_environment();
}

static void test_weapon_weights_align_with_reference_values(void **state)
{
    (void)state;

    char weapon_config_path[512];
    char weight_config_path[512];
    asset_path_or_skip("dev_tools/assets/weapons.c", weapon_config_path, sizeof(weapon_config_path));
    asset_path_or_skip("dev_tools/assets/default/defaul_w.c", weight_config_path, sizeof(weight_config_path));

    (void)weight_config_path;

    setup_botlib_environment();
    ai_weapon_library_t *library = AI_LoadWeaponLibrary(NULL);
    assert_non_null(library);
    assert_string_equal(library->source_path, weapon_config_path);

    const bot_weapon_config_t *weapon_config = AI_GetWeaponConfig(library);
    assert_non_null(weapon_config);

    ai_weapon_weights_t *weights = AI_LoadWeaponWeights("default/defaul_w.c");
    assert_non_null(weights);

    int blaster_index = weapon_index_by_name(weapon_config, "Blaster");
    assert_true(blaster_index >= 0);
    int machinegun_index = weapon_index_by_name(weapon_config, "Machinegun");
    assert_true(machinegun_index >= 0);
    int shotgun_index = weapon_index_by_name(weapon_config, "Shotgun");
    assert_true(shotgun_index >= 0);

    const float expected_blaster_weight = 20.0f;
    float actual_blaster_weight = AI_WeaponWeightForClient(weights, blaster_index);
    assert_true(fabsf(actual_blaster_weight - expected_blaster_weight) < 0.01f);

    const float expected_machinegun_weight = 70.0f;
    float actual_machinegun_weight = AI_WeaponWeightForClient(weights, machinegun_index);
    assert_true(fabsf(actual_machinegun_weight - expected_machinegun_weight) < 0.01f);

    test_reset_log();
    char temp_weight_path[L_tmpnam];
    assert_non_null(tmpnam(temp_weight_path));

    FILE *temp_file = fopen(temp_weight_path, "w");
    assert_non_null(temp_file);
    fputs("weight \"Blaster\"\n{\n    return 20;\n}\n", temp_file);
    fclose(temp_file);

    ai_weapon_weights_t *missing_weights = AI_LoadWeaponWeights(temp_weight_path);
    assert_null(missing_weights);
    assert_true(g_test_log.count > 0);

    char expected_warning[256];
    snprintf(expected_warning,
             sizeof(expected_warning),
             "item info %d \"%s\" has no fuzzy weight\n",
             weapon_config->weapons[shotgun_index].number,
             weapon_config->weapons[shotgun_index].name);
    assert_string_equal(g_test_log.entries[g_test_log.count - 1].text, expected_warning);

    remove(temp_weight_path);

    AI_FreeWeaponWeights(weights);
    AI_UnloadWeaponLibrary(library);
    teardown_botlib_environment();
}

static void test_bot_choose_best_fight_weapon_matches_reference(void **state)
{
    (void)state;

    char weapon_config_path[512];
    char weight_config_path[512];
    asset_path_or_skip("dev_tools/assets/weapons.c", weapon_config_path, sizeof(weapon_config_path));
    asset_path_or_skip("dev_tools/assets/default/defaul_w.c", weight_config_path, sizeof(weight_config_path));

    setup_botlib_environment();
    ai_weapon_library_t *library = AI_LoadWeaponLibrary(NULL);
    assert_non_null(library);
    assert_string_equal(library->source_path, weapon_config_path);

    (void)weight_config_path;

    int weapon_handle = BotAllocWeaponState();
    assert_true(weapon_handle > 0);
    assert_int_equal(BotLoadWeaponWeights(weapon_handle, "default/defaul_w.c"), BLERR_NOERROR);

    int inventory[MAX_ITEMS];
    memset(inventory, 0, sizeof(inventory));

    inventory[INVENTORY_BLASTER] = 1;
    int best_weapon = BotChooseBestFightWeapon(weapon_handle, inventory);
    assert_int_equal(best_weapon, 0);

    memset(inventory, 0, sizeof(inventory));
    inventory[INVENTORY_BLASTER] = 1;
    inventory[INVENTORY_MACHINEGUN] = 1;
    inventory[INVENTORY_BULLETS] = 50;
    best_weapon = BotChooseBestFightWeapon(weapon_handle, inventory);
    assert_int_equal(best_weapon, 3);

    memset(inventory, 0, sizeof(inventory));
    inventory[INVENTORY_BLASTER] = 1;
    inventory[INVENTORY_MACHINEGUN] = 1;
    inventory[INVENTORY_BULLETS] = 50;
    inventory[INVENTORY_ROCKETLAUNCHER] = 1;
    inventory[INVENTORY_ROCKETS] = 10;
    inventory[ENEMY_HORIZONTAL_DIST] = 999;
    best_weapon = BotChooseBestFightWeapon(weapon_handle, inventory);
    assert_int_equal(best_weapon, 7);

    BotFreeWeaponState(weapon_handle);
    AI_UnloadWeaponLibrary(library);
    teardown_botlib_environment();
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_weapon_library_reports_expected_counts),
        cmocka_unit_test(test_weapon_weights_align_with_reference_values),
        cmocka_unit_test(test_bot_choose_best_fight_weapon_matches_reference),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
