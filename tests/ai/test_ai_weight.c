#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef cmocka_skip
#define cmocka_skip(...) skip()
#endif

#include "botlib/ai_weight/bot_weight.h"
#include "botlib/common/l_libvar.h"
#include "botlib/common/l_memory.h"
#include "inv.h"

#ifndef PROJECT_SOURCE_DIR
#error "PROJECT_SOURCE_DIR must be defined so regression tests can resolve asset paths."
#endif

/*
=============
read_normalized_byte

Reads text fixtures while ignoring CR bytes introduced by platform checkout.
=============
*/
static int read_normalized_byte(FILE *file)
{
	int c;

	do {
		c = fgetc(file);
	} while (c == '\r');

	return c;
}

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

	BotShutdownWeights();
	BotMemory_Shutdown();
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

/*
=============
write_weight_fixture

Writes a temporary text weight fixture for parser edge-case checks.
=============
*/
static void write_weight_fixture(const char *path, const char *contents)
{
	FILE *file = fopen(path, "wb");
	assert_non_null(file);
	assert_true(fputs(contents, file) >= 0);
	assert_int_equal(fclose(file), 0);
}

/*
=============
test_cache_follows_bot_reloadcharacters_flag

Confirms the retail weight cache is retained unless character reloading is on.
=============
*/
static void test_cache_follows_bot_reloadcharacters_flag(void **state)
{
	(void)state;

	char fixture_root[512];
	int written = snprintf(fixture_root, sizeof(fixture_root), "%s/tests/support/assets", PROJECT_SOURCE_DIR);
	assert_true(written > 0 && written < (int)sizeof(fixture_root));

	char default_root[512];
	written = snprintf(default_root, sizeof(default_root), "%s/dev_tools/assets", PROJECT_SOURCE_DIR);
	assert_true(written > 0 && written < (int)sizeof(default_root));

	BotShutdownWeights();
	LibVarSet("gladiator_asset_dir", fixture_root);
	LibVarSet("bot_reloadcharacters", "0");

	bot_weight_config_t *cached_first = ReadWeightConfig("bots/sample_weight.c");
	assert_non_null(cached_first);
	bot_weight_config_t *cached_second = ReadWeightConfig("bots/sample_weight.c");
	assert_ptr_equal(cached_first, cached_second);

	FreeWeightConfig(cached_first);
	bot_weight_config_t *cached_after_free = ReadWeightConfig("bots/sample_weight.c");
	assert_ptr_equal(cached_first, cached_after_free);

	BotShutdownWeights();
	LibVarSet("bot_reloadcharacters", "1");

	bot_weight_config_t *reload_first = ReadWeightConfig("bots/sample_weight.c");
	assert_non_null(reload_first);
	bot_weight_config_t *reload_second = ReadWeightConfig("bots/sample_weight.c");
	assert_non_null(reload_second);
	assert_true(reload_first != reload_second);

	FreeWeightConfig(reload_first);
	FreeWeightConfig(reload_second);

	LibVarSet("bot_reloadcharacters", "0");
	LibVarSet("gladiator_asset_dir", default_root);
}

/*
=============
test_fuzzy_weight_uses_retail_integer_scale

Locks in the original integer division used between adjacent fuzzy cases.
=============
*/
static void test_fuzzy_weight_uses_retail_integer_scale(void **state)
{
	(void)state;

	char fixture_path[512];
	int written = snprintf(fixture_path,
						   sizeof(fixture_path),
						   "%s/tests/support/assets/bots/retail_integer_scale_tmp.w",
						   PROJECT_SOURCE_DIR);
	assert_true(written > 0 && written < (int)sizeof(fixture_path));

	const char *fixture =
		"weight \"scale_test\"\n"
		"{\n"
		"switch(3)\n"
		"{\n"
		"case 0:\n"
		"{\n"
		"return 10;\n"
		"}\n"
		"case 10:\n"
		"{\n"
		"return 20;\n"
		"}\n"
		"default:\n"
		"{\n"
		"return 30;\n"
		"}\n"
		"}\n"
		"}\n"
		"\n"
		"weight \"negative_test\"\n"
		"{\n"
		"return -5;\n"
		"}\n";

	write_weight_fixture(fixture_path, fixture);
	LibVarSet("bot_reloadcharacters", "1");

	bot_weight_config_t *config = ReadWeightConfig(fixture_path);
	assert_non_null(config);

	int scale_index = find_weight_index(config, "scale_test");
	int negative_index = find_weight_index(config, "negative_test");
	assert_true(scale_index >= 0);
	assert_true(negative_index >= 0);

	int inventory[256] = {0};
	inventory[3] = 5;

	assert_true(fabsf(FuzzyWeight(inventory, config, scale_index) - 20.0f) < 0.01f);
	assert_true(fabsf(FuzzyWeightUndecided(inventory, config, scale_index) - 20.0f) < 0.01f);
	assert_true(fabsf(FuzzyWeight(inventory, config, negative_index) - 5.0f) < 0.01f);

	FreeWeightConfig(config);
	LibVarSet("bot_reloadcharacters", "0");
	remove(fixture_path);
}

/*
=============
test_merge_weight_configs_averages_balance_nodes

Pins the two-config Gladiator merge helper reconstructed from the HLIL.
=============
*/
static void test_merge_weight_configs_averages_balance_nodes(void **state)
{
	(void)state;

	char fixture_a[512];
	int written = snprintf(fixture_a,
						   sizeof(fixture_a),
						   "%s/tests/support/assets/bots/merge_a_tmp.w",
						   PROJECT_SOURCE_DIR);
	assert_true(written > 0 && written < (int)sizeof(fixture_a));

	char fixture_b[512];
	written = snprintf(fixture_b,
					   sizeof(fixture_b),
					   "%s/tests/support/assets/bots/merge_b_tmp.w",
					   PROJECT_SOURCE_DIR);
	assert_true(written > 0 && written < (int)sizeof(fixture_b));

	const char *config_a_text =
		"weight \"merge_test\"\n"
		"{\n"
		"return balance(10,0,100);\n"
		"}\n";
	const char *config_b_text =
		"weight \"merge_test\"\n"
		"{\n"
		"return balance(30,0,100);\n"
		"}\n";

	write_weight_fixture(fixture_a, config_a_text);
	write_weight_fixture(fixture_b, config_b_text);
	LibVarSet("bot_reloadcharacters", "1");

	bot_weight_config_t *config_a = ReadWeightConfig(fixture_a);
	bot_weight_config_t *config_b = ReadWeightConfig(fixture_b);
	assert_non_null(config_a);
	assert_non_null(config_b);

	assert_int_equal(MergeWeightConfigs(config_a, config_b), 1);

	int weight_index = find_weight_index(config_a, "merge_test");
	assert_true(weight_index >= 0);
	assert_true(fabsf(FuzzyWeight(NULL, config_a, weight_index) - 20.0f) < 0.01f);

	FreeWeightConfig(config_a);
	FreeWeightConfig(config_b);
	LibVarSet("bot_reloadcharacters", "0");
	remove(fixture_a);
	remove(fixture_b);
}

static void test_default_weapon_shotgun_weight_matches_reference(void **state)
{
    (void)state;

    bot_weight_config_t *config = load_weight_config_or_skip("dev_tools/assets/default/defaul_w.c");

    int shotgun_index = find_weight_index(config, "weapon_shotgun");
    assert_true(shotgun_index >= 0);

    int inventory[256] = {0};
    inventory[INVENTORY_SHOTGUN] = 1;
    inventory[INVENTORY_SHELLS] = 10;

    const float expected_weight = 10.0f;
    float actual_weight = FuzzyWeight(inventory, config, shotgun_index);

    if (fabsf(actual_weight - expected_weight) > 0.01f) {
        FreeWeightConfig(config);
        fail_msg("weapon_shotgun weight differed from Gladiator default item weights: expected %.2f, got %.2f", expected_weight, actual_weight);
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
    inventory[INVENTORY_QUAD] = 0;

    const float expected_weight = 70.0f;
    float actual_weight = FuzzyWeight(inventory, config, quad_index);

    if (fabsf(actual_weight - expected_weight) > 0.01f) {
        FreeWeightConfig(config);
        fail_msg("item_quad weight differed from Gladiator default item weights: expected %.2f, got %.2f", expected_weight, actual_weight);
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

	int single_index = BotFindFuzzyWeight(handle, "single_value");
	assert_true(single_index >= 0);
	assert_true(fabsf(BotFuzzyWeightHandle(handle, NULL, single_index) - 42.0f) < 0.01f);
	assert_int_equal(BotFindFuzzyWeight(handle, "missing_weight"), -1);

    char expected_path[512];
    written = snprintf(expected_path, sizeof(expected_path), "%s/tests/support/assets/bots/sample_weight_expected.w", PROJECT_SOURCE_DIR);
    assert_true(written > 0 && written < (int)sizeof(expected_path));

    char output_path[512];
    written = snprintf(output_path, sizeof(output_path), "%s/tests/support/assets/bots/sample_weight_out.w", PROJECT_SOURCE_DIR);
    assert_true(written > 0 && written < (int)sizeof(output_path));

    remove(output_path);

    assert_true(BotWriteWeights(handle, output_path));

    FILE *expected = fopen(expected_path, "rb");
    assert_non_null(expected);
    FILE *actual = fopen(output_path, "rb");
    assert_non_null(actual);

	for (;;) {
		int c_expected = read_normalized_byte(expected);
		int c_actual = read_normalized_byte(actual);
		assert_int_equal(c_expected, c_actual);
		if (c_expected == EOF) {
			break;
		}
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
        cmocka_unit_test(test_cache_follows_bot_reloadcharacters_flag),
        cmocka_unit_test(test_fuzzy_weight_uses_retail_integer_scale),
        cmocka_unit_test(test_merge_weight_configs_averages_balance_nodes),
        cmocka_unit_test(test_default_weapon_shotgun_weight_matches_reference),
        cmocka_unit_test(test_default_item_quad_weight_matches_reference),
        cmocka_unit_test(test_writer_serialises_weights_like_reference),
    };

    return cmocka_run_group_tests(tests, weight_tests_setup, weight_tests_teardown);
}

