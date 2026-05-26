#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
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

/*
=============
load_weight_config_or_skip

Loads an asset-backed weight file, skipping when optional fixtures are absent.
=============
*/
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

/*
=============
weight_tests_setup

Initialises libvars so the weight loader resolves Gladiator assets.
=============
*/
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

/*
=============
weight_tests_teardown

Drops weight caches and libvars between test groups.
=============
*/
static int weight_tests_teardown(void **state)
{
	(void)state;

	BotShutdownWeights();
	BotMemory_Shutdown();
	LibVar_Shutdown();
	return 0;
}

/*
=============
find_weight_index

Finds a named fuzzy weight in a parsed config.
=============
*/
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
write_many_weights_fixture

Writes a generated fixture that crosses the retail 128-weight parser limit.
=============
*/
static void write_many_weights_fixture(const char *path, int count)
{
	FILE *file = fopen(path, "wb");
	assert_non_null(file);

	for (int i = 0; i < count; ++i) {
		assert_true(fprintf(file,
			"weight \"limit_%03d\"\n"
			"{\n"
			"return %d;\n"
			"}\n",
			i,
			i) > 0);
	}

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

	char retained_fixture[512];
	written = snprintf(retained_fixture,
					   sizeof(retained_fixture),
					   "%s/tests/support/assets/bots/cache_retained_tmp.w",
					   PROJECT_SOURCE_DIR);
	assert_true(written > 0 && written < (int)sizeof(retained_fixture));

	write_weight_fixture(retained_fixture,
		"weight \"retained\"\n"
		"{\n"
		"return 13;\n"
		"}\n");

	bot_weight_config_t *retained_first = ReadWeightConfig("bots/cache_retained_tmp.w");
	assert_non_null(retained_first);
	remove(retained_fixture);

	bot_weight_config_t *retained_second = ReadWeightConfig("bots/cache_retained_tmp.w");
	assert_ptr_equal(retained_first, retained_second);

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
test_free_weight_config_respects_retail_reload_gate

Pins Q3's FreeWeightConfig gate for direct, non-cacheable loads.
=============
*/
static void test_free_weight_config_respects_retail_reload_gate(void **state)
{
	(void)state;

	char fixture_path[512];
	int written = snprintf(fixture_path,
						   sizeof(fixture_path),
						   "%s/tests/support/assets/bots/free_direct_tmp.w",
						   PROJECT_SOURCE_DIR);
	assert_true(written > 0 && written < (int)sizeof(fixture_path));

	write_weight_fixture(fixture_path,
		"weight \"direct_free\"\n"
		"{\n"
		"return 7;\n"
		"}\n");

	const char *defines[] = {"UNUSED_DEFINE 1"};
	LibVarSet("bot_reloadcharacters", "0");

	bot_weight_config_t *config = ReadWeightConfigWithDefines(fixture_path, defines, 1);
	assert_non_null(config);
	size_t allocated_before_free = BotMemory_TotalAllocated();

	FreeWeightConfig(config);
	assert_int_equal(BotMemory_TotalAllocated(), allocated_before_free);

	LibVarSet("bot_reloadcharacters", "1");
	FreeWeightConfig(config);
	assert_true(BotMemory_TotalAllocated() < allocated_before_free);

	LibVarSet("bot_reloadcharacters", "0");
	remove(fixture_path);
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
test_fuzzy_weight_undecided_samples_balance_range

Pins the retail undecided evaluator's balance-range and final-leaf behavior.
=============
*/
static void test_fuzzy_weight_undecided_samples_balance_range(void **state)
{
	(void)state;

	bot_weight_config_t config = {0};
	bot_fuzzy_seperator_t range = {0};
	bot_fuzzy_seperator_t final = {0};

	range.index = 2;
	range.value = 100;
	range.type = BOTLIB_WEIGHT_TYPE_BALANCE;
	range.weight = 15.0f;
	range.min_weight = 10.0f;
	range.max_weight = 20.0f;
	range.next = &final;

	final.index = 2;
	final.value = 200;
	final.type = BOTLIB_WEIGHT_TYPE_BALANCE;
	final.weight = 70.0f;
	final.min_weight = 60.0f;
	final.max_weight = 80.0f;

	config.num_weights = 1;
	config.weights[0].first_seperator = &range;

	int inventory[256] = {0};
	inventory[2] = 50;
	for (unsigned int seed = 0; seed < 32; ++seed) {
		srand(seed);
		float sampled = FuzzyWeightUndecided(inventory, &config, 0);
		assert_true(sampled >= range.min_weight);
		assert_true(sampled <= range.max_weight);
	}

	inventory[2] = 250;
	assert_true(fabsf(FuzzyWeightUndecided(inventory, &config, 0) - final.weight) < 0.01f);
}

/*
=============
test_fuzzy_weight_random_scale_matches_retail_mask

Pins the Q3/Gladiator random() scale used by balance sampling and mutation.
=============
*/
static void test_fuzzy_weight_random_scale_matches_retail_mask(void **state)
{
	(void)state;

	bot_weight_config_t config = {0};
	bot_fuzzy_seperator_t range = {0};

	range.index = 2;
	range.value = 100;
	range.type = BOTLIB_WEIGHT_TYPE_BALANCE;
	range.weight = 0.0f;
	range.min_weight = 0.0f;
	range.max_weight = 32767.0f;

	config.num_weights = 1;
	config.weights[0].first_seperator = &range;

	int inventory[256] = {0};
	inventory[2] = 50;

	for (unsigned int seed = 0U; seed < 32U; ++seed) {
		srand(seed);
		float expected = (float)(rand() & 0x7fff);

		srand(seed);
		float sampled = FuzzyWeightUndecided(inventory, &config, 0);
		assert_true(fabsf(sampled - expected) < 0.001f);
	}
}

/*
=============
test_switch_without_default_appends_zero_default

Pins the retail parser warning path that appends an implicit default separator.
=============
*/
static void test_switch_without_default_appends_zero_default(void **state)
{
	(void)state;

	char fixture_path[512];
	int written = snprintf(fixture_path,
						   sizeof(fixture_path),
						   "%s/tests/support/assets/bots/missing_default_tmp.w",
						   PROJECT_SOURCE_DIR);
	assert_true(written > 0 && written < (int)sizeof(fixture_path));

	const char *fixture =
		"weight \"missing_default\"\n"
		"{\n"
		"switch(3)\n"
		"{\n"
		"case 5:\n"
		"{\n"
		"return 25;\n"
		"}\n"
		"}\n"
		"}\n";

	write_weight_fixture(fixture_path, fixture);
	LibVarSet("bot_reloadcharacters", "1");

	bot_weight_config_t *config = ReadWeightConfig(fixture_path);
	assert_non_null(config);

	int weight_index = find_weight_index(config, "missing_default");
	assert_true(weight_index >= 0);

	bot_fuzzy_seperator_t *root = config->weights[weight_index].first_seperator;
	assert_non_null(root);
	assert_non_null(root->next);
	assert_int_equal(root->next->value, BOTLIB_WEIGHT_MAX_VALUE);

	int inventory[256] = {0};
	inventory[3] = 4;
	assert_true(fabsf(FuzzyWeight(inventory, config, weight_index) - 25.0f) < 0.01f);
	inventory[3] = 10;
	assert_true(fabsf(FuzzyWeight(inventory, config, weight_index) - 0.0f) < 0.01f);

	FreeWeightConfig(config);
	LibVarSet("bot_reloadcharacters", "0");
	remove(fixture_path);
}

/*
=============
test_parser_keeps_first_128_weights

Verifies the retail `"too many fuzzy weights"` path stops after 128 entries.
=============
*/
static void test_parser_keeps_first_128_weights(void **state)
{
	(void)state;

	char fixture_path[512];
	int written = snprintf(fixture_path,
						   sizeof(fixture_path),
						   "%s/tests/support/assets/bots/weight_limit_tmp.w",
						   PROJECT_SOURCE_DIR);
	assert_true(written > 0 && written < (int)sizeof(fixture_path));

	write_many_weights_fixture(fixture_path, BOTLIB_MAX_WEIGHTS + 1);
	LibVarSet("bot_reloadcharacters", "1");

	bot_weight_config_t *config = ReadWeightConfig(fixture_path);
	assert_non_null(config);
	assert_int_equal(config->num_weights, BOTLIB_MAX_WEIGHTS);

	int last_index = find_weight_index(config, "limit_127");
	assert_true(last_index >= 0);
	assert_int_equal(find_weight_index(config, "limit_128"), -1);
	assert_true(fabsf(FuzzyWeight(NULL, config, last_index) - 127.0f) < 0.01f);

	FreeWeightConfig(config);
	LibVarSet("bot_reloadcharacters", "0");
	remove(fixture_path);
}

/*
=============
test_evalfloat_macro_values_preserve_default_weight_math

Pins the precompiler number-value cache used by $evalfloat weight macros.
=============
*/
static void test_evalfloat_macro_values_preserve_default_weight_math(void **state)
{
	(void)state;

	char fixture_path[512];
	int written = snprintf(fixture_path,
						   sizeof(fixture_path),
						   "%s/tests/support/assets/bots/evalfloat_tmp.w",
						   PROJECT_SOURCE_DIR);
	assert_true(written > 0 && written < (int)sizeof(fixture_path));

	const char *fixture =
		"#define BASE_WEIGHT 70\n"
		"#define SPREAD 15\n"
		"#define MZ(value) (value) < 0 ? 0 : (value)\n"
		"#define POWERUP_SCALE(value) balance($evalfloat(MZ(value)), $evalfloat(MZ(value-SPREAD)), $evalfloat(MZ(value+SPREAD)))\n"
		"weight \"evalfloat_powerup\"\n"
		"{\n"
		"return POWERUP_SCALE(BASE_WEIGHT);\n"
		"}\n";

	write_weight_fixture(fixture_path, fixture);
	LibVarSet("bot_reloadcharacters", "1");

	bot_weight_config_t *config = ReadWeightConfig(fixture_path);
	assert_non_null(config);

	int weight_index = find_weight_index(config, "evalfloat_powerup");
	assert_true(weight_index >= 0);
	assert_true(fabsf(FuzzyWeight(NULL, config, weight_index) - 70.0f) < 0.01f);

	FreeWeightConfig(config);
	LibVarSet("bot_reloadcharacters", "0");
	remove(fixture_path);
}

/*
=============
test_scoped_global_defines_do_not_leak_between_weight_loads

Pins ReadWeightConfigWithDefines cleanup for DMFLAGS-style weight branches.
=============
*/
static void test_scoped_global_defines_do_not_leak_between_weight_loads(void **state)
{
	(void)state;

	char fixture_path[512];
	int written = snprintf(fixture_path,
						   sizeof(fixture_path),
						   "%s/tests/support/assets/bots/scoped_define_tmp.w",
						   PROJECT_SOURCE_DIR);
	assert_true(written > 0 && written < (int)sizeof(fixture_path));

	const char *fixture =
		"#ifndef DMFLAGS\n"
		"#define DMFLAGS 0\n"
		"#endif\n"
		"#define DF_WEAPONS_STAY 4\n"
		"weight \"dmflags_branch\"\n"
		"{\n"
		"#if (DMFLAGS & DF_WEAPONS_STAY)\n"
		"return 0;\n"
		"#else\n"
		"return 42;\n"
		"#endif\n"
		"}\n";
	const char *defines[] = {
		"#define DMFLAGS 4",
	};

	write_weight_fixture(fixture_path, fixture);
	LibVarSet("bot_reloadcharacters", "1");

	bot_weight_config_t *defined_config = ReadWeightConfigWithDefines(fixture_path, defines, 1);
	assert_non_null(defined_config);
	int weight_index = find_weight_index(defined_config, "dmflags_branch");
	assert_true(weight_index >= 0);
	assert_true(fabsf(FuzzyWeight(NULL, defined_config, weight_index) - 0.0f) < 0.01f);
	FreeWeightConfig(defined_config);

	bot_weight_config_t *plain_config = ReadWeightConfig(fixture_path);
	assert_non_null(plain_config);
	weight_index = find_weight_index(plain_config, "dmflags_branch");
	assert_true(weight_index >= 0);
	assert_true(fabsf(FuzzyWeight(NULL, plain_config, weight_index) - 42.0f) < 0.01f);

	FreeWeightConfig(plain_config);
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
		"}\n"
		"\n"
		"weight \"merge_nested\"\n"
		"{\n"
		"switch(3)\n"
		"{\n"
		"case 1:\n"
		"{\n"
		"switch(4)\n"
		"{\n"
		"case 1:\n"
		"{\n"
		"return balance(10,0,100);\n"
		"}\n"
		"default:\n"
		"{\n"
		"return balance(50,0,100);\n"
		"}\n"
		"}\n"
		"}\n"
		"default:\n"
		"{\n"
		"return balance(70,0,100);\n"
		"}\n"
		"}\n"
		"}\n";
	const char *config_b_text =
		"weight \"merge_test\"\n"
		"{\n"
		"return balance(30,0,100);\n"
		"}\n"
		"\n"
		"weight \"merge_nested\"\n"
		"{\n"
		"switch(3)\n"
		"{\n"
		"case 1:\n"
		"{\n"
		"switch(4)\n"
		"{\n"
		"case 1:\n"
		"{\n"
		"return balance(30,0,100);\n"
		"}\n"
		"default:\n"
		"{\n"
		"return balance(90,0,100);\n"
		"}\n"
		"}\n"
		"}\n"
		"default:\n"
		"{\n"
		"return balance(110,0,100);\n"
		"}\n"
		"}\n"
		"}\n";

	write_weight_fixture(fixture_a, config_a_text);
	write_weight_fixture(fixture_b, config_b_text);
	LibVarSet("bot_reloadcharacters", "1");

	bot_weight_config_t *config_a = ReadWeightConfig(fixture_a);
	bot_weight_config_t *config_b = ReadWeightConfig(fixture_b);
	assert_non_null(config_a);
	assert_non_null(config_b);

	assert_int_equal(MergeWeightConfigs(config_a, config_b), 2);

	int weight_index = find_weight_index(config_a, "merge_test");
	assert_true(weight_index >= 0);
	assert_true(fabsf(FuzzyWeight(NULL, config_a, weight_index) - 20.0f) < 0.01f);

	int nested_index = find_weight_index(config_a, "merge_nested");
	assert_true(nested_index >= 0);

	int inventory[256] = {0};
	inventory[3] = 0;
	inventory[4] = 0;
	float nested_weight = FuzzyWeight(inventory, config_a, nested_index);
	if (fabsf(nested_weight - 10.0f) >= 0.01f) {
		fail_msg("nested merge followed an unexpected branch: expected %.2f, got %.2f", 10.0f, nested_weight);
	}

	inventory[3] = 2;
	inventory[4] = 0;
	nested_weight = FuzzyWeight(inventory, config_a, nested_index);
	if (fabsf(nested_weight - 70.0f) >= 0.01f) {
		fail_msg("merge sibling walk diverged from Gladiator: expected %.2f, got %.2f", 70.0f, nested_weight);
	}

	FreeWeightConfig(config_a);
	FreeWeightConfig(config_b);
	LibVarSet("bot_reloadcharacters", "0");
	remove(fixture_a);
	remove(fixture_b);
}

/*
=============
test_interbreed_weight_configs_preserves_child_self_cross

Pins the Q3 child-branch self-cross used by goal fuzzy interbreeding.
=============
*/
static void test_interbreed_weight_configs_preserves_child_self_cross(void **state)
{
	(void)state;

	char fixture_a[512];
	int written = snprintf(fixture_a,
						   sizeof(fixture_a),
						   "%s/tests/support/assets/bots/interbreed_a_tmp.w",
						   PROJECT_SOURCE_DIR);
	assert_true(written > 0 && written < (int)sizeof(fixture_a));

	char fixture_b[512];
	written = snprintf(fixture_b,
					   sizeof(fixture_b),
					   "%s/tests/support/assets/bots/interbreed_b_tmp.w",
					   PROJECT_SOURCE_DIR);
	assert_true(written > 0 && written < (int)sizeof(fixture_b));

	char fixture_out[512];
	written = snprintf(fixture_out,
					   sizeof(fixture_out),
					   "%s/tests/support/assets/bots/interbreed_out_tmp.w",
					   PROJECT_SOURCE_DIR);
	assert_true(written > 0 && written < (int)sizeof(fixture_out));

	const char *config_a_text =
		"weight \"interbreed_top\"\n"
		"{\n"
		"return balance(10,0,100);\n"
		"}\n"
		"\n"
		"weight \"interbreed_nested\"\n"
		"{\n"
		"switch(3)\n"
		"{\n"
		"case 1:\n"
		"{\n"
		"switch(4)\n"
		"{\n"
		"case 1:\n"
		"{\n"
		"return balance(10,0,100);\n"
		"}\n"
		"default:\n"
		"{\n"
		"return balance(50,0,100);\n"
		"}\n"
		"}\n"
		"}\n"
		"default:\n"
		"{\n"
		"return balance(70,0,100);\n"
		"}\n"
		"}\n"
		"}\n";
	const char *config_b_text =
		"weight \"interbreed_top\"\n"
		"{\n"
		"return balance(30,0,100);\n"
		"}\n"
		"\n"
		"weight \"interbreed_nested\"\n"
		"{\n"
		"switch(3)\n"
		"{\n"
		"case 1:\n"
		"{\n"
		"switch(4)\n"
		"{\n"
		"case 1:\n"
		"{\n"
		"return balance(30,0,100);\n"
		"}\n"
		"default:\n"
		"{\n"
		"return balance(90,0,100);\n"
		"}\n"
		"}\n"
		"}\n"
		"default:\n"
		"{\n"
		"return balance(110,0,100);\n"
		"}\n"
		"}\n"
		"}\n";
	const char *config_out_text =
		"weight \"interbreed_top\"\n"
		"{\n"
		"return balance(0,0,100);\n"
		"}\n"
		"\n"
		"weight \"interbreed_nested\"\n"
		"{\n"
		"switch(3)\n"
		"{\n"
		"case 1:\n"
		"{\n"
		"switch(4)\n"
		"{\n"
		"case 1:\n"
		"{\n"
		"return balance(0,0,100);\n"
		"}\n"
		"default:\n"
		"{\n"
		"return balance(0,0,100);\n"
		"}\n"
		"}\n"
		"}\n"
		"default:\n"
		"{\n"
		"return balance(0,0,100);\n"
		"}\n"
		"}\n"
		"}\n";

	write_weight_fixture(fixture_a, config_a_text);
	write_weight_fixture(fixture_b, config_b_text);
	write_weight_fixture(fixture_out, config_out_text);
	LibVarSet("bot_reloadcharacters", "1");

	bot_weight_config_t *config_a = ReadWeightConfig(fixture_a);
	bot_weight_config_t *config_b = ReadWeightConfig(fixture_b);
	bot_weight_config_t *config_out = ReadWeightConfig(fixture_out);
	assert_non_null(config_a);
	assert_non_null(config_b);
	assert_non_null(config_out);

	InterbreedWeightConfigs(config_a, config_b, config_out);

	int top_index = find_weight_index(config_out, "interbreed_top");
	assert_true(top_index >= 0);
	assert_true(fabsf(FuzzyWeight(NULL, config_out, top_index) - 20.0f) < 0.01f);

	int nested_index = find_weight_index(config_out, "interbreed_nested");
	assert_true(nested_index >= 0);

	int inventory[256] = {0};
	inventory[3] = 0;
	inventory[4] = 0;
	float nested_weight = FuzzyWeight(inventory, config_out, nested_index);
	if (fabsf(nested_weight - 30.0f) >= 0.01f) {
		fail_msg("nested interbreed followed an unexpected branch: expected %.2f, got %.2f", 30.0f, nested_weight);
	}

	FreeWeightConfig(config_a);
	FreeWeightConfig(config_b);
	FreeWeightConfig(config_out);
	LibVarSet("bot_reloadcharacters", "0");
	remove(fixture_a);
	remove(fixture_b);
	remove(fixture_out);
}

/*
=============
test_evolve_weight_config_clamps_balance_weights

Pins Gladiator's HLIL mutation behavior: weights clamp to their balance range.
=============
*/
static void test_evolve_weight_config_clamps_balance_weights(void **state)
{
	(void)state;

	bot_weight_config_t config = {0};
	bot_fuzzy_seperator_t seperator = {0};
	config.num_weights = 1;
	config.weights[0].first_seperator = &seperator;

	for (unsigned int seed = 0; seed < 1024; ++seed) {
		seperator.type = BOTLIB_WEIGHT_TYPE_BALANCE;
		seperator.weight = 0.0f;
		seperator.min_weight = 0.0f;
		seperator.max_weight = 100.0f;
		seperator.child = NULL;
		seperator.next = NULL;

		srand(seed);
		EvolveWeightConfig(&config);

		assert_true(seperator.weight >= seperator.min_weight);
		assert_true(seperator.weight <= seperator.max_weight);
		assert_true(fabsf(seperator.min_weight - 0.0f) < 0.01f);
		assert_true(fabsf(seperator.max_weight - 100.0f) < 0.01f);
	}
}

/*
=============
test_scale_helpers_follow_retail_bounds

Pins the low-level ScaleWeight and ScaleBalanceRange helpers mapped from HLIL.
=============
*/
static void test_scale_helpers_follow_retail_bounds(void **state)
{
	(void)state;

	bot_weight_config_t config = {0};
	bot_fuzzy_seperator_t first = {0};
	bot_fuzzy_seperator_t second = {0};
	char target_name[] = "scale_target";

	first.type = BOTLIB_WEIGHT_TYPE_BALANCE;
	first.weight = 50.0f;
	first.min_weight = 0.0f;
	first.max_weight = 100.0f;
	first.next = &second;

	second.type = BOTLIB_WEIGHT_TYPE_BALANCE;
	second.weight = 30.0f;
	second.min_weight = 10.0f;
	second.max_weight = 50.0f;

	config.num_weights = 1;
	config.weights[0].name = target_name;
	config.weights[0].first_seperator = &first;

	ScaleWeight(&config, "missing_weight", 0.0f);
	assert_true(fabsf(first.weight - 50.0f) < 0.01f);
	assert_true(fabsf(second.weight - 30.0f) < 0.01f);

	ScaleWeight(&config, "scale_target", -1.0f);
	assert_true(fabsf(first.weight - 0.0f) < 0.01f);
	assert_true(fabsf(second.weight - 10.0f) < 0.01f);

	ScaleWeight(&config, "scale_target", 2.0f);
	assert_true(fabsf(first.weight - 100.0f) < 0.01f);
	assert_true(fabsf(second.weight - 50.0f) < 0.01f);

	bot_weight_config_t range_config = {0};
	bot_fuzzy_seperator_t range = {0};
	char range_name[] = "range_target";

	range.type = BOTLIB_WEIGHT_TYPE_BALANCE;
	range.weight = 50.0f;
	range.min_weight = 0.0f;
	range.max_weight = 100.0f;

	range_config.num_weights = 1;
	range_config.weights[0].name = range_name;
	range_config.weights[0].first_seperator = &range;

	ScaleBalanceRange(&range_config, 0.5f);
	assert_true(fabsf(range.weight - 50.0f) < 0.01f);
	assert_true(fabsf(range.min_weight - 25.0f) < 0.01f);
	assert_true(fabsf(range.max_weight - 75.0f) < 0.01f);

	ScaleBalanceRange(&range_config, -10.0f);
	assert_true(fabsf(range.min_weight - 50.0f) < 0.01f);
	assert_true(fabsf(range.max_weight - 50.0f) < 0.01f);
}

/*
=============
test_default_weapon_shotgun_weight_matches_reference

Checks Gladiator's default item weight for a shotgun pickup already owned.
=============
*/
static void test_default_weapon_shotgun_weight_matches_reference(void **state)
{
	(void)state;

	bot_weight_config_t *config = load_weight_config_or_skip("dev_tools/assets/default/defaul_i.c");

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

/*
=============
test_default_item_quad_weight_matches_reference

Checks Gladiator's default quad item weight after macro evaluation.
=============
*/
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

/*
=============
test_handle_exports_set_and_read_weight_configs

Exercises direct read and runtime-set wiring exposed through the weight surface.
=============
*/
static void test_handle_exports_set_and_read_weight_configs(void **state)
{
	(void)state;

	char fixture_root[512];
	int written = snprintf(fixture_root,
						   sizeof(fixture_root),
						   "%s/tests/support/assets",
						   PROJECT_SOURCE_DIR);
	assert_true(written > 0 && written < (int)sizeof(fixture_root));

	char default_root[512];
	written = snprintf(default_root,
					   sizeof(default_root),
					   "%s/dev_tools/assets",
					   PROJECT_SOURCE_DIR);
	assert_true(written > 0 && written < (int)sizeof(default_root));

	LibVarSet("gladiator_asset_dir", fixture_root);
	LibVarSet("bot_reloadcharacters", "1");

	bot_weight_config_t *direct_config = BotReadWeightsFile("bots/sample_weight.c");
	assert_non_null(direct_config);
	int direct_index = find_weight_index(direct_config, "single_value");
	assert_true(direct_index >= 0);
	assert_true(fabsf(FuzzyWeight(NULL, direct_config, direct_index) - 42.0f) < 0.01f);
	FreeWeightConfig(direct_config);

	int handle = BotAllocWeightConfig();
	assert_true(handle > 0);
	assert_true(BotLoadWeights(handle, "bots/sample_weight.c"));

	int single_index = BotFindFuzzyWeight(handle, "single_value");
	assert_true(single_index >= 0);
	assert_int_equal(BotFindFuzzyWeight(9999, "single_value"), -1);
	assert_true(fabsf(BotFuzzyWeightHandle(9999, NULL, single_index) - 0.0f) < 0.01f);

	assert_true(BotSetWeight(handle, "single_value", 55.0f));
	assert_true(fabsf(BotFuzzyWeightHandle(handle, NULL, single_index) - 55.0f) < 0.01f);
	assert_int_equal(BotSetWeight(handle, "missing_weight", 1.0f), 0);

	BotFreeWeightConfig(handle);

	LibVarSet("bot_reloadcharacters", "0");
	LibVarSet("gladiator_asset_dir", default_root);
}

/*
=============
test_writer_serialises_weights_like_reference

Verifies handle wrappers and the Gladiator writer footer against fixtures.
=============
*/
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

/*
=============
main

Runs the reconstructed ai_weight parity regression suite.
=============
*/
int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_cache_follows_bot_reloadcharacters_flag),
		cmocka_unit_test(test_free_weight_config_respects_retail_reload_gate),
		cmocka_unit_test(test_fuzzy_weight_uses_retail_integer_scale),
		cmocka_unit_test(test_fuzzy_weight_undecided_samples_balance_range),
		cmocka_unit_test(test_fuzzy_weight_random_scale_matches_retail_mask),
		cmocka_unit_test(test_switch_without_default_appends_zero_default),
		cmocka_unit_test(test_parser_keeps_first_128_weights),
		cmocka_unit_test(test_evalfloat_macro_values_preserve_default_weight_math),
		cmocka_unit_test(test_scoped_global_defines_do_not_leak_between_weight_loads),
		cmocka_unit_test(test_merge_weight_configs_averages_balance_nodes),
		cmocka_unit_test(test_interbreed_weight_configs_preserves_child_self_cross),
		cmocka_unit_test(test_evolve_weight_config_clamps_balance_weights),
		cmocka_unit_test(test_scale_helpers_follow_retail_bounds),
		cmocka_unit_test(test_default_weapon_shotgun_weight_matches_reference),
		cmocka_unit_test(test_default_item_quad_weight_matches_reference),
		cmocka_unit_test(test_handle_exports_set_and_read_weight_configs),
		cmocka_unit_test(test_writer_serialises_weights_like_reference),
	};

	return cmocka_run_group_tests(tests, weight_tests_setup, weight_tests_teardown);
}

