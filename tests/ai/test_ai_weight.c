#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "botlib/ai/weight/bot_weight.h"
#include "inv.h"

#ifndef PROJECT_SOURCE_DIR
#error "PROJECT_SOURCE_DIR must be defined so regression tests can resolve asset paths."
#endif

static bot_weight_config_t *load_weight_config_or_skip(const char *relative_path)
{
    char absolute_path[512];
    int written = snprintf(absolute_path, sizeof(absolute_path), "%s/%s", PROJECT_SOURCE_DIR, relative_path);
    assert_true(written > 0 && written < (int)sizeof(absolute_path));

    bot_weight_config_t *config = ReadWeightConfig(absolute_path);
    if (config == NULL) {
        cmocka_skip();
    }

    return config;
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

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_default_weapon_shotgun_weight_matches_reference),
        cmocka_unit_test(test_default_item_quad_weight_matches_reference),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}

