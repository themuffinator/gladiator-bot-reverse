#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "botlib_ai_weight/bot_weight.h"
#include "botlib_common/l_libvar.h"
#include "inv.h"

#ifndef PROJECT_SOURCE_DIR
#error "PROJECT_SOURCE_DIR must be defined so regression tests can resolve asset paths."
#endif

static bot_weight_config_t *load_weight_config_or_skip(const char *relative_path)
{
    char absolute_path[512];
    int written = snprintf(absolute_path, sizeof(absolute_path), "%s/%s", PROJECT_SOURCE_DIR, relative_path);
    assert_true(written > 0 && written < (int)sizeof(absolute_path));

    FILE *file = fopen(absolute_path, "rb");
    if (file == NULL) {
        cmocka_skip();
    }
    fclose(file);

    bot_weight_config_t *config = ReadWeightConfig(relative_path);
    if (config == NULL) {
        cmocka_skip();
    }

    return config;
}

static int weight_tests_setup(void **state)
{
    (void)state;

    LibVar_Init();

    char asset_root[512];
    int written = snprintf(asset_root, sizeof(asset_root), "%s/dev_tools/assets", PROJECT_SOURCE_DIR);
    assert_true(written > 0 && written < (int)sizeof(asset_root));

    LibVarSet("basedir", asset_root);
    LibVarSet("gamedir", "");
    LibVarSet("cddir", "");
    LibVarSet("gladiator_asset_dir", "");
    LibVarSet("itemconfig", "items.c");

    return 0;
}

static int weight_tests_teardown(void **state)
{
    (void)state;

    LibVar_Shutdown();
    return 0;
}

static int find_weight_index(const bot_weight_config_t *config, const char *name)
{
    for (int i = 0; i < config->num_weights; ++i) {
        if (config->weights[i].name != NULL && strcmp(config->weights[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static void test_default_weapon_shotgun_weight_matches_reference(void **state)
{
    (void)state;

    bot_weight_config_t *config = load_weight_config_or_skip("dev_tools/assets/default/defaul_w.c");

    int shotgun_index = find_weight_index(config, "weapon_shotgun");
    assert_true(shotgun_index >= 0);

    int inventory[256] = {0};
    inventory[INVENTORY_SHOTGUN] = 1;  // already own the shotgun
    inventory[INVENTORY_SHELLS] = 10;  // ammunition threshold hit in Quake III's scripts

    const float expected_weight = 0.0f;  // Quake III returns zero weight when the weapon is already owned
    float actual_weight = FuzzyWeight(inventory, config, shotgun_index);

    if (fabsf(actual_weight - expected_weight) > 0.01f) {
        FreeWeightConfig(config);
        fail_msg("weapon_shotgun weight differed from Quake III reference: expected %.2f, got %.2f", expected_weight, actual_weight);
    }

    FreeWeightConfig(config);
}

static void test_default_item_quad_weight_matches_reference(void **state)
{
    (void)state;

    bot_weight_config_t *config = load_weight_config_or_skip("dev_tools/assets/default/defaul_i.c");

    int quad_index = find_weight_index(config, "item_quad");
    assert_true(quad_index >= 0);

    int inventory[256] = {0};
    inventory[INVENTORY_QUAD] = 0;  // bot does not currently own Quad Damage

    const float expected_weight = 70.0f;  // default Quake III configuration centre weight
    float actual_weight = FuzzyWeight(inventory, config, quad_index);

    if (fabsf(actual_weight - expected_weight) > 0.01f) {
        FreeWeightConfig(config);
        fail_msg("item_quad weight differed from Quake III reference: expected %.2f, got %.2f", expected_weight, actual_weight);
    }

    FreeWeightConfig(config);
}

static void test_writer_serialises_weights_like_reference(void **state)
{
    (void)state;

    char fixture_root[512];
    int written = snprintf(fixture_root, sizeof(fixture_root), "%s/tests/support/assets", PROJECT_SOURCE_DIR);
    assert_true(written > 0 && written < (int)sizeof(fixture_root));

    char default_root[512];
    written = snprintf(default_root, sizeof(default_root), "%s/dev_tools/assets", PROJECT_SOURCE_DIR);
    assert_true(written > 0 && written < (int)sizeof(default_root));

    LibVarSet("gladiator_asset_dir", fixture_root);

    int handle = BotAllocWeightConfig();
    assert_true(handle > 0);

    assert_true(BotLoadWeights(handle, "bots/sample_weight.c"));

    const char *relative_output = "bots/sample_weight_out.w";

    char expected_path[512];
    written = snprintf(expected_path, sizeof(expected_path), "%s/tests/support/assets/bots/sample_weight_expected.w", PROJECT_SOURCE_DIR);
    assert_true(written > 0 && written < (int)sizeof(expected_path));

    char output_path[512];
    written = snprintf(output_path, sizeof(output_path), "%s/tests/support/assets/bots/sample_weight_out.w", PROJECT_SOURCE_DIR);
    assert_true(written > 0 && written < (int)sizeof(output_path));

    remove(output_path);

    assert_true(BotWriteWeights(handle, relative_output));

    FILE *expected = fopen(expected_path, "rb");
    assert_non_null(expected);
    FILE *actual = fopen(output_path, "rb");
    assert_non_null(actual);

    fseek(expected, 0, SEEK_END);
    long expected_size = ftell(expected);
    fseek(expected, 0, SEEK_SET);

    fseek(actual, 0, SEEK_END);
    long actual_size = ftell(actual);
    fseek(actual, 0, SEEK_SET);

    assert_true(expected_size >= 0 && actual_size >= 0);
    assert_int_equal(expected_size, actual_size);

    while (expected_size-- > 0) {
        int c_expected = fgetc(expected);
        int c_actual = fgetc(actual);
        assert_int_equal(c_expected, c_actual);
    }

    fclose(expected);
    fclose(actual);

    remove(output_path);

    BotFreeWeightConfig(handle);

    LibVarSet("gladiator_asset_dir", default_root);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_default_weapon_shotgun_weight_matches_reference),
        cmocka_unit_test(test_default_item_quad_weight_matches_reference),
        cmocka_unit_test(test_writer_serialises_weights_like_reference),
    };

    return cmocka_run_group_tests(tests, weight_tests_setup, weight_tests_teardown);
}

