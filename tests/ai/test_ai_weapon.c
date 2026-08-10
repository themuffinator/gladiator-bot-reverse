#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <direct.h>
#define TEST_MKDIR(path) _mkdir(path)
#define TEST_RMDIR(path) _rmdir(path)
#else
#include <sys/stat.h>
#include <unistd.h>
#define TEST_MKDIR(path) mkdir((path), 0777)
#define TEST_RMDIR(path) rmdir(path)
#endif

#ifndef cmocka_skip
#define cmocka_skip(...) skip()
#endif

#include "botlib/ai_weapon/bot_weapon.h"
#include "botlib/ai_goal/bot_goal.h"
#include "botlib/ea/ea_local.h"
#include "botlib/common/l_crc.h"
#include "botlib/common/l_libvar.h"
#include "botlib/common/l_memory.h"
#include "botlib/interface/botlib_interface.h"
#include "q2bridge/bridge.h"
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
#define TEST_MAX_CLIENT_COMMANDS 8

typedef struct test_log_message_s {
    int priority;
    char text[256];
} test_log_message_t;

typedef struct test_client_command_s {
	int client;
	char text[256];
	char argument[256];
	bool terminated;
} test_client_command_t;

static struct {
    test_log_message_t entries[TEST_MAX_LOG_MESSAGES];
    int count;
} g_test_log;

static struct {
	test_client_command_t entries[TEST_MAX_CLIENT_COMMANDS];
	int count;
} g_test_client_commands;

static void test_reset_log(void)
{
    g_test_log.count = 0;
    for (int i = 0; i < TEST_MAX_LOG_MESSAGES; ++i) {
        g_test_log.entries[i].priority = 0;
        g_test_log.entries[i].text[0] = '\0';
    }
}

static void test_reset_client_commands(void)
{
	g_test_client_commands.count = 0;
	for (int i = 0; i < TEST_MAX_CLIENT_COMMANDS; ++i) {
		g_test_client_commands.entries[i].client = -1;
		g_test_client_commands.entries[i].text[0] = '\0';
		g_test_client_commands.entries[i].argument[0] = '\0';
		g_test_client_commands.entries[i].terminated = false;
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

/*
=============
test_capture_client_command

Captures the retail command token, its optional first argument, and terminator.
=============
*/
static void test_capture_client_command(int client, char *fmt, ...)
{
	if (fmt == NULL || g_test_client_commands.count >= TEST_MAX_CLIENT_COMMANDS) {
		return;
	}

	va_list args;
	va_start(args, fmt);

	test_client_command_t *slot = &g_test_client_commands.entries[g_test_client_commands.count++];
	slot->client = client;
	snprintf(slot->text, sizeof(slot->text), "%s", fmt);
	slot->text[sizeof(slot->text) - 1] = '\0';

	char *argument = va_arg(args, char *);
	if (argument != NULL) {
		snprintf(slot->argument, sizeof(slot->argument), "%s", argument);
		slot->argument[sizeof(slot->argument) - 1] = '\0';
	}
	slot->terminated = va_arg(args, char *) == NULL;

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

static bot_import_extended_t g_test_q2_imports = {
	.BotClientCommand = test_capture_client_command,
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
	test_reset_client_commands();
    BotInterface_SetImportTable(&g_test_imports);
    LibVar_Init();
    configure_asset_libvars();
    assert_true(BotMemory_Init(TEST_BOTLIB_HEAP_SIZE));
}

static void teardown_botlib_environment(void)
{
	Q2Bridge_ClearImportTable();
	BotShutdownWeights();
	LibVar_Shutdown();
	CRC_ResetSourceChecksums();
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

/*
=============
write_weapon_fixture

Writes a temporary weapon-weight script for cache wiring tests.
=============
*/
static void write_weapon_fixture(const char *path, const char *contents)
{
	FILE *file = fopen(path, "wb");
	assert_non_null(file);
	assert_true(fputs(contents, file) >= 0);
	assert_int_equal(fclose(file), 0);
}

typedef struct test_ai_pak_entry_s
{
	const char *name;
	const char *contents;
	uint32_t offset;
	uint32_t length;
} test_ai_pak_entry_t;

/*
=============
write_ai_pak_u32

Writes one portable little-endian integer to a runtime PAK fixture.
=============
*/
static void write_ai_pak_u32(FILE *file, uint32_t value)
{
	unsigned char bytes[4];

	bytes[0] = (unsigned char)(value & 0xffU);
	bytes[1] = (unsigned char)((value >> 8) & 0xffU);
	bytes[2] = (unsigned char)((value >> 16) & 0xffU);
	bytes[3] = (unsigned char)((value >> 24) & 0xffU);
	assert_int_equal(fwrite(bytes, 1U, sizeof(bytes), file), sizeof(bytes));
}

/*
=============
write_ai_pak_fixture

Creates a minimal Quake PAK containing only logical AI configuration assets.
=============
*/
static void write_ai_pak_fixture(const char *path,
	test_ai_pak_entry_t *entries,
	size_t entry_count)
{
	uint32_t data_offset = 12U;
	FILE *file;

	for (size_t i = 0; i < entry_count; ++i)
	{
		size_t length = strlen(entries[i].contents);
		assert_true(length <= UINT32_MAX - data_offset);
		entries[i].offset = data_offset;
		entries[i].length = (uint32_t)length;
		data_offset += entries[i].length;
	}
	assert_true(entry_count <= UINT32_MAX / 64U);

	file = fopen(path, "wb");
	assert_non_null(file);
	assert_int_equal(fwrite("PACK", 1U, 4U, file), 4U);
	write_ai_pak_u32(file, data_offset);
	write_ai_pak_u32(file, (uint32_t)(entry_count * 64U));

	for (size_t i = 0; i < entry_count; ++i)
	{
		assert_int_equal(fwrite(entries[i].contents,
			1U,
			entries[i].length,
			file),
			entries[i].length);
	}
	for (size_t i = 0; i < entry_count; ++i)
	{
		unsigned char name[56] = { 0 };
		size_t name_length = strlen(entries[i].name);
		assert_true(name_length < sizeof(name));
		memcpy(name, entries[i].name, name_length);
		assert_int_equal(fwrite(name, 1U, sizeof(name), file), sizeof(name));
		write_ai_pak_u32(file, entries[i].offset);
		write_ai_pak_u32(file, entries[i].length);
	}
	assert_int_equal(fclose(file), 0);
}

/*
=============
remove_ai_pak_fixture

Removes only the known runtime PAK and extracted cache paths from this test.
=============
*/
static void remove_ai_pak_fixture(void)
{
	static const char *files[] = {
		"__gladiator_pak_ai_fixture/.pak_cache/pak0/weapons.c",
		"__gladiator_pak_ai_fixture/.pak_cache/pak0/weapon_defs.inc",
		"__gladiator_pak_ai_fixture/.pak_cache/pak0/items.c",
		"__gladiator_pak_ai_fixture/.pak_cache/pak0/item_defs.inc",
		"__gladiator_pak_ai_fixture/pak0.pak",
	};
	static const char *directories[] = {
		"__gladiator_pak_ai_fixture/.pak_cache/pak0",
		"__gladiator_pak_ai_fixture/.pak_cache",
		"__gladiator_pak_ai_fixture",
	};

	for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); ++i)
	{
		(void)remove(files[i]);
	}
	for (size_t i = 0; i < sizeof(directories) / sizeof(directories[0]); ++i)
	{
		(void)TEST_RMDIR(directories[i]);
	}
}

/*
=============
ai_checksum_name_present

Reports whether preprocessing retained one caller-facing logical filename.
=============
*/
static bool ai_checksum_name_present(const char *expected)
{
	for (size_t i = 0; i < CRC_SourceChecksumCount(); ++i)
	{
		uint16_t checksum = 0;
		char name[CRC_SOURCE_NAME_MAX];
		if (!CRC_SourceChecksumAt(i, &checksum, name, sizeof(name)))
		{
			continue;
		}
		if (strcmp(name, expected) == 0)
		{
			return true;
		}
	}

	return false;
}

/*
=============
create_ai_pak_fixture

Creates default-named weapon and item configs whose complete definitions live
in sibling PAK entries.
=============
*/
static void create_ai_pak_fixture(void)
{
	static const char fixture_root[] = "__gladiator_pak_ai_fixture";
	static const char pak_path[] = "__gladiator_pak_ai_fixture/pak0.pak";
	test_ai_pak_entry_t entries[] = {
		{ "weapons.c", "#include \"weapon_defs.inc\"\n", 0U, 0U },
		{
			"weapon_defs.inc",
			"#define PAK_PROJECTILE_DAMAGE 47\n"
			"projectileinfo\n"
			"{\n"
			"name \"pak_projectile\"\n"
			"model \"models/objects/pak/tris.md2\"\n"
			"damage PAK_PROJECTILE_DAMAGE\n"
			"}\n"
			"weaponinfo\n"
			"{\n"
			"name \"PAK Sibling Weapon\"\n"
			"model \"models/weapons/pak/tris.md2\"\n"
			"weaponindex 17\n"
			"projectile \"pak_projectile\"\n"
			"numprojectiles 1\n"
			"ammoindex 23\n"
			"}\n",
			0U,
			0U,
		},
		{ "items.c", "#include \"item_defs.inc\"\n", 0U, 0U },
		{
			"item_defs.inc",
			"iteminfo \"pak_sibling_item\"\n"
			"{\n"
			"name \"PAK Sibling Item\"\n"
			"model \"models/items/pak/tris.md2\"\n"
			"type 9\n"
			"index 77\n"
			"respawntime 30\n"
			"mins {-1,-2,-3}\n"
			"maxs {1,2,3}\n"
			"}\n",
			0U,
			0U,
		},
	};

	remove_ai_pak_fixture();
	assert_int_equal(TEST_MKDIR(fixture_root), 0);
	write_ai_pak_fixture(pak_path, entries, sizeof(entries) / sizeof(entries[0]));
}

static int weapon_index_by_name(const bot_weapon_config_t *config, const char *name)
{
    if (config == NULL || name == NULL) {
        return -1;
    }

    for (int i = 0; i < config->num_weapons; ++i) {
        if (strcmp(config->weapons[i].name, name) == 0) {
            return i;
        }
    }

    return -1;
}

/*
=============
test_pak_only_weapon_config_resolves_sibling_include

Exercises logical-name preprocessing through AI_LoadWeaponLibrary when neither
the default-named config nor its complete definition sibling exists loose.
=============
*/
static void test_pak_only_weapon_config_resolves_sibling_include(void **state)
{
	(void)state;
	static const char fixture_root[] = "__gladiator_pak_ai_fixture";
	create_ai_pak_fixture();

	setup_botlib_environment();
	LibVarSet("basedir", fixture_root);
	LibVarSet("gamedir", "");
	LibVarSet("cddir", "");
	LibVarSet("gladiator_asset_dir", fixture_root);
	CRC_ResetSourceChecksums();

	ai_weapon_library_t *library = AI_LoadWeaponLibrary("weapons.c");
	const bot_weapon_config_t *config = AI_GetWeaponConfig(library);
	bool source_is_pak = library != NULL && strstr(library->source_path,
		"__gladiator_pak_ai_fixture/.pak_cache/pak0/weapons.c") != NULL;
	int projectile_count = config != NULL ? config->num_projectiles : -1;
	int weapon_count = config != NULL ? config->num_weapons : -1;
	bool projectile_matches = config != NULL && projectile_count == 1
		&& strcmp(config->projectiles[0].name, "pak_projectile") == 0
		&& config->projectiles[0].damage == 47;
	bool weapon_matches = config != NULL && weapon_count == 1
		&& strcmp(config->weapons[0].name, "PAK Sibling Weapon") == 0
		&& config->weapons[0].projectileinfo == &config->projectiles[0];
	bool main_key_present = ai_checksum_name_present("weapons.c");
	bool sibling_key_present = ai_checksum_name_present("weapon_defs.inc");

	AI_UnloadWeaponLibrary(library);
	teardown_botlib_environment();
	remove_ai_pak_fixture();

	assert_true(source_is_pak);
	assert_int_equal(projectile_count, 1);
	assert_int_equal(weapon_count, 1);
	assert_true(projectile_matches);
	assert_true(weapon_matches);
	assert_true(main_key_present);
	assert_true(sibling_key_present);
}

/*
=============
test_pak_only_itemconfig_resolves_sibling_include

Exercises logical-name preprocessing through BotSetupGoalAI with the retail
default itemconfig name and a PAK-only sibling definition.
=============
*/
static void test_pak_only_itemconfig_resolves_sibling_include(void **state)
{
	(void)state;
	static const char fixture_root[] = "__gladiator_pak_ai_fixture";
	create_ai_pak_fixture();

	setup_botlib_environment();
	LibVarSet("basedir", fixture_root);
	LibVarSet("gamedir", "");
	LibVarSet("cddir", "");
	LibVarSet("gladiator_asset_dir", fixture_root);
	LibVarSet("itemconfig", "items.c");
	CRC_ResetSourceChecksums();

	int setup_status = BotSetupGoalAI();
	bool main_key_present = ai_checksum_name_present("items.c");
	bool sibling_key_present = ai_checksum_name_present("item_defs.inc");

	BotShutdownGoalAI();
	teardown_botlib_environment();
	remove_ai_pak_fixture();

	assert_int_equal(setup_status, BLERR_NOERROR);
	assert_true(main_key_present);
	assert_true(sibling_key_present);
}

/*
=============
test_weapon_struct_layout_matches_hlil_offsets

Pins the compact Gladiator weapon and projectile row layout recovered from HLIL.
=============
*/
static void test_weapon_struct_layout_matches_hlil_offsets(void **state)
{
	(void)state;

	assert_int_equal(BOT_WEAPON_INFO_RETAIL_SIZE, 0x158);
	assert_int_equal(BOT_PROJECTILE_INFO_RETAIL_SIZE, 0x0d0);
	assert_int_equal(offsetof(bot_weapon_info_t, number), BOT_WEAPON_INFO_NUMBER_OFFSET);
	assert_int_equal(offsetof(bot_weapon_info_t, name), BOT_WEAPON_INFO_NAME_OFFSET);
	assert_int_equal(offsetof(bot_weapon_info_t, model), BOT_WEAPON_INFO_MODEL_OFFSET);
	assert_int_equal(offsetof(bot_weapon_info_t, activate), BOT_WEAPON_INFO_ACTIVATE_OFFSET);
	assert_int_equal(offsetof(bot_weapon_info_t, projectileinfo),
					 BOT_WEAPON_INFO_PROJECTILEINFO_OFFSET);
	assert_int_equal(sizeof(bot_weapon_projectile_t), BOT_PROJECTILE_INFO_RETAIL_SIZE);
	assert_true(sizeof(bot_weapon_info_t) >= BOT_WEAPON_INFO_RETAIL_SIZE);
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

    assert_int_equal(config->weapons[0].number, 0);
    assert_string_equal(config->weapons[0].name, "Blaster");
    assert_non_null(config->weapons[0].projectileinfo);
    assert_ptr_equal(config->weapons[0].projectileinfo, &config->projectiles[0]);
    assert_string_equal(config->weapons[0].projectileinfo->name, "blasterbolt");

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
	assert_int_equal(AI_WeaponWeightsConfigByteSize(weights),
					 MemoryByteSize(weights->config));
	assert_int_equal(AI_WeaponWeightsIndexByteSize(weights),
					 MemoryByteSize(weights->index_by_weapon));

    int blaster_index = weapon_index_by_name(weapon_config, "Blaster");
    assert_true(blaster_index >= 0);
    int machinegun_index = weapon_index_by_name(weapon_config, "Machinegun");
    assert_true(machinegun_index >= 0);
    int shotgun_index = weapon_index_by_name(weapon_config, "Shotgun");
    assert_true(shotgun_index >= 0);

    assert_int_equal(weapon_config->weapons[machinegun_index].weaponindex, INVENTORY_MACHINEGUN);
    assert_int_equal(weapon_config->weapons[machinegun_index].ammoindex, INVENTORY_BULLETS);

    const float expected_blaster_weight = 20.0f;
    float actual_blaster_weight = AI_WeaponWeightForClient(weights, blaster_index);
    assert_true(fabsf(actual_blaster_weight - expected_blaster_weight) < 0.01f);

    const float expected_machinegun_weight = 70.0f;
    float actual_machinegun_weight = AI_WeaponWeightForClient(weights, machinegun_index);
    if (fabsf(actual_machinegun_weight - expected_machinegun_weight) >= 0.01f) {
        fail_msg("Machinegun reference weight differed from Gladiator default weapon weights: expected %.2f, got %.2f",
                 expected_machinegun_weight,
                 actual_machinegun_weight);
    }

    test_reset_log();
    char temp_weight_path[L_tmpnam];
    assert_non_null(tmpnam(temp_weight_path));

    FILE *temp_file = fopen(temp_weight_path, "w");
    assert_non_null(temp_file);
    fputs("weight \"Blaster\"\n{\n    return 20;\n}\n", temp_file);
    fclose(temp_file);

    ai_weapon_weights_t *missing_weights = AI_LoadWeaponWeights(temp_weight_path);
    assert_non_null(missing_weights);
    for (int i = 0; i < g_test_log.count; ++i) {
        assert_true(g_test_log.entries[i].priority != PRT_ERROR);
        assert_true(g_test_log.entries[i].priority != PRT_FATAL);
    }
    assert_float_equal(AI_WeaponWeightForClient(missing_weights, blaster_index), 20.0f, 0.01f);
    assert_float_equal(AI_WeaponWeightForClient(missing_weights, shotgun_index), 0.0f, 0.01f);

    remove(temp_weight_path);

    AI_FreeWeaponWeights(missing_weights);
    AI_FreeWeaponWeights(weights);
    AI_UnloadWeaponLibrary(library);
    teardown_botlib_environment();
}

/*
=============
test_weapon_weights_cache_uses_caller_filename

Pins the AI weapon-weight loader to Q3's filename-keyed weight cache.
=============
*/
static void test_weapon_weights_cache_uses_caller_filename(void **state)
{
	(void)state;

	char weapon_config_path[512];
	asset_path_or_skip("dev_tools/assets/weapons.c", weapon_config_path, sizeof(weapon_config_path));

	char fixture_root[PATH_MAX];
	int written = snprintf(fixture_root,
		sizeof(fixture_root),
		"%s/tests/support/assets",
		PROJECT_SOURCE_DIR);
	assert_true(written > 0 && written < (int)sizeof(fixture_root));

	char fixture_path[PATH_MAX];
	written = snprintf(fixture_path,
		sizeof(fixture_path),
		"%s/tests/support/assets/bots/weapon_cache_retained_tmp.w",
		PROJECT_SOURCE_DIR);
	assert_true(written > 0 && written < (int)sizeof(fixture_path));

	write_weapon_fixture(fixture_path,
		"weight \"Blaster\"\n"
		"{\n"
		"return 29;\n"
		"}\n");

	setup_botlib_environment();
	ai_weapon_library_t *library = AI_LoadWeaponLibrary(NULL);
	assert_non_null(library);
	assert_string_equal(library->source_path, weapon_config_path);

	LibVarSet("gladiator_asset_dir", fixture_root);
	LibVarSet("bot_reloadcharacters", "0");

	ai_weapon_weights_t *first = AI_LoadWeaponWeights("bots/weapon_cache_retained_tmp.w");
	assert_non_null(first);
	assert_non_null(first->config);

	assert_int_equal(remove(fixture_path), 0);

	ai_weapon_weights_t *second = AI_LoadWeaponWeights("bots/weapon_cache_retained_tmp.w");
	assert_non_null(second);
	assert_ptr_equal(second->config, first->config);

	int weapon_handle = BotAllocWeaponState();
	assert_true(weapon_handle > 0);
	assert_int_equal(BotLoadWeaponWeights(weapon_handle, "bots/weapon_cache_retained_tmp.w"),
					 BLERR_NOERROR);
	BotFreeWeaponState(weapon_handle);

	AI_FreeWeaponWeights(second);
	AI_FreeWeaponWeights(first);
	AI_UnloadWeaponLibrary(library);
	teardown_botlib_environment();
}

/*
=============
test_weapon_weight_binding_preserves_hlil_load_order

Verifies weapon weights can load before the global weaponconfig exists.
=============
*/
static void test_weapon_weight_binding_preserves_hlil_load_order(void **state)
{
	(void)state;

	char weapon_config_path[512];
	char weight_config_path[512];
	asset_path_or_skip("dev_tools/assets/weapons.c", weapon_config_path, sizeof(weapon_config_path));
	asset_path_or_skip("dev_tools/assets/default/defaul_w.c", weight_config_path, sizeof(weight_config_path));

	(void)weight_config_path;

	setup_botlib_environment();

	ai_weapon_weights_t *early_weights = AI_LoadWeaponWeights("default/defaul_w.c");
	assert_non_null(early_weights);
	assert_null(early_weights->definitions);
	assert_null(early_weights->index_by_weapon);
	assert_int_equal(early_weights->index_count, 0);
	assert_float_equal(AI_WeaponWeightForClient(early_weights, 3), 0.0f, 0.01f);

	int weapon_handle = BotAllocWeaponState();
	assert_true(weapon_handle > 0);
	assert_int_equal(BotLoadWeaponWeights(weapon_handle, "default/defaul_w.c"),
					 BLERR_CANNOTLOADWEAPONCONFIG);

	int attached_handle = BotAllocWeaponState();
	assert_true(attached_handle > 0);
	assert_int_equal(BotWeaponStateAttachWeights(attached_handle, early_weights),
					 BLERR_CANNOTLOADWEAPONCONFIG);

	ai_weapon_library_t *library = AI_LoadWeaponLibrary(NULL);
	assert_non_null(library);
	assert_string_equal(library->source_path, weapon_config_path);

	const bot_weapon_config_t *weapon_config = AI_GetWeaponConfig(library);
	assert_non_null(weapon_config);

	int machinegun_index = weapon_index_by_name(weapon_config, "Machinegun");
	assert_true(machinegun_index >= 0);

	int inventory[MAX_ITEMS];
	memset(inventory, 0, sizeof(inventory));
	inventory[INVENTORY_BLASTER] = 1;
	inventory[INVENTORY_MACHINEGUN] = 1;
	inventory[INVENTORY_BULLETS] = 50;

	assert_int_equal(BotChooseBestFightWeapon(attached_handle, inventory), 3);
	assert_non_null(early_weights->definitions);
	assert_non_null(early_weights->index_by_weapon);
	assert_int_equal(early_weights->index_count, weapon_config->num_weapons);
	assert_float_equal(AI_WeaponWeightForClient(early_weights, machinegun_index), 70.0f, 0.01f);

	assert_int_equal(BotChooseBestFightWeapon(weapon_handle, inventory), 3);

	BotFreeWeaponState(weapon_handle);
	BotFreeWeaponState(attached_handle);
	AI_FreeWeaponWeights(early_weights);
	AI_UnloadWeaponLibrary(library);
	teardown_botlib_environment();
}

/*
=============
test_weapon_selection_rebinds_unbound_state_weights

Verifies selection refreshes stale weight bindings against the active config.
=============
*/
static void test_weapon_selection_rebinds_unbound_state_weights(void **state)
{
	(void)state;

	char weapon_config_path[512];
	char weight_config_path[512];
	asset_path_or_skip("dev_tools/assets/weapons.c", weapon_config_path, sizeof(weapon_config_path));
	asset_path_or_skip("dev_tools/assets/default/defaul_w.c", weight_config_path, sizeof(weight_config_path));

	(void)weapon_config_path;
	(void)weight_config_path;

	setup_botlib_environment();

	ai_weapon_library_t *library = AI_LoadWeaponLibrary(NULL);
	assert_non_null(library);

	const bot_weapon_config_t *weapon_config = AI_GetWeaponConfig(library);
	assert_non_null(weapon_config);

	ai_weapon_weights_t *weights = AI_LoadWeaponWeights("default/defaul_w.c");
	assert_non_null(weights);
	assert_ptr_equal(weights->definitions, weapon_config);

	int weapon_handle = BotAllocWeaponState();
	assert_true(weapon_handle > 0);
	assert_int_equal(BotWeaponStateAttachWeights(weapon_handle, weights), BLERR_NOERROR);

	assert_true(AI_WeaponWeightsBindConfig(weights, NULL));
	assert_null(weights->definitions);
	assert_null(weights->index_by_weapon);
	assert_int_equal(weights->index_count, 0);

	int inventory[MAX_ITEMS];
	memset(inventory, 0, sizeof(inventory));
	inventory[INVENTORY_BLASTER] = 1;
	inventory[INVENTORY_MACHINEGUN] = 1;
	inventory[INVENTORY_BULLETS] = 50;

	assert_int_equal(BotChooseBestFightWeapon(weapon_handle, inventory), 3);
	assert_ptr_equal(weights->definitions, weapon_config);
	assert_non_null(weights->index_by_weapon);
	assert_int_equal(weights->index_count, weapon_config->num_weapons);

	BotFreeWeaponState(weapon_handle);
	AI_FreeWeaponWeights(weights);
	AI_UnloadWeaponLibrary(library);
	teardown_botlib_environment();
}

static void test_weapon_model_lookup_and_info_copy_match_hlil_helpers(void **state)
{
    (void)state;

    char weapon_config_path[512];
    asset_path_or_skip("dev_tools/assets/weapons.c", weapon_config_path, sizeof(weapon_config_path));

    setup_botlib_environment();
    ai_weapon_library_t *library = AI_LoadWeaponLibrary(NULL);
    assert_non_null(library);

    const bot_weapon_config_t *weapon_config = AI_GetWeaponConfig(library);
    assert_non_null(weapon_config);

    int rocket_index = weapon_index_by_name(weapon_config, "Rocket Launcher");
    assert_true(rocket_index >= 0);

	const char *rocket_model = weapon_config->weapons[rocket_index].model;
	char upper_model[BOT_WEAPON_MAX_STRINGFIELD];
	size_t model_index = 0;
	for (; model_index + 1 < sizeof(upper_model) && rocket_model[model_index] != '\0'; ++model_index)
	{
		char ch = rocket_model[model_index];
		if (ch >= 'a' && ch <= 'z')
		{
			ch = (char)(ch - ('a' - 'A'));
		}
		upper_model[model_index] = ch;
	}
	upper_model[model_index] = '\0';

	assert_int_equal(AI_WeaponNumberForModel(rocket_model), rocket_index);
	assert_string_equal(AI_WeaponNameForModel(rocket_model), "Rocket Launcher");
	assert_int_equal(AI_WeaponNumberForModel(upper_model), rocket_index);
	assert_string_equal(AI_WeaponNameForModel(upper_model), "Rocket Launcher");
	assert_int_equal(AI_WeaponNumberForModel("models/weapons/not_real/tris.md2"), -1);
	assert_string_equal(AI_WeaponNameForModel("models/weapons/not_real/tris.md2"), "unknown weapon");

    int weapon_handle = BotAllocWeaponState();
    assert_true(weapon_handle > 0);

    bot_weapon_info_t copied;
    BotGetWeaponInfo(weapon_handle, rocket_index, &copied);
    assert_int_equal(copied.number, rocket_index);
    assert_string_equal(copied.name, "Rocket Launcher");
    assert_ptr_equal(copied.projectileinfo, weapon_config->weapons[rocket_index].projectileinfo);

    BotFreeWeaponState(weapon_handle);
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
	assert_int_equal(BotGetTopRankedWeapon(weapon_handle), 0);

    memset(inventory, 0, sizeof(inventory));
    inventory[INVENTORY_BLASTER] = 1;
    inventory[INVENTORY_MACHINEGUN] = 1;
    inventory[INVENTORY_BULLETS] = 50;
    best_weapon = BotChooseBestFightWeapon(weapon_handle, inventory);
    assert_int_equal(best_weapon, 3);
	assert_int_equal(BotGetTopRankedWeapon(weapon_handle), 0);

    memset(inventory, 0, sizeof(inventory));
    inventory[INVENTORY_BLASTER] = 1;
    inventory[INVENTORY_MACHINEGUN] = 1;
    inventory[INVENTORY_BULLETS] = 50;
    inventory[INVENTORY_ROCKETLAUNCHER] = 1;
    inventory[INVENTORY_ROCKETS] = 10;
    inventory[ENEMY_HORIZONTAL_DIST] = 999;
    best_weapon = BotChooseBestFightWeapon(weapon_handle, inventory);
    assert_int_equal(best_weapon, 6);
	assert_int_equal(BotGetTopRankedWeapon(weapon_handle), 0);

    BotFreeWeaponState(weapon_handle);
    AI_UnloadWeaponLibrary(library);
    teardown_botlib_environment();
}

static void test_weapon_selector_queues_use_command_like_hlil(void **state)
{
	(void)state;

	char weapon_config_path[512];
	char weight_config_path[512];
	asset_path_or_skip("dev_tools/assets/weapons.c", weapon_config_path, sizeof(weapon_config_path));
	asset_path_or_skip("dev_tools/assets/default/defaul_w.c", weight_config_path, sizeof(weight_config_path));

	setup_botlib_environment();
	Q2Bridge_SetImportTable(&g_test_q2_imports);
	assert_int_equal(EA_Init(MAX_CLIENTS), BLERR_NOERROR);

	ai_weapon_library_t *library = AI_LoadWeaponLibrary(NULL);
	assert_non_null(library);

	int weapon_handle = BotAllocWeaponState();
	assert_true(weapon_handle > 0);
	ai_weapon_weights_t *selector_weights = AI_LoadWeaponWeights("default/defaul_w.c");
	assert_non_null(selector_weights);
	assert_int_equal(BotWeaponStateAttachWeights(weapon_handle, selector_weights), BLERR_NOERROR);

	int inventory[MAX_ITEMS];
	memset(inventory, 0, sizeof(inventory));
	inventory[INVENTORY_BLASTER] = 1;
	inventory[INVENTORY_MACHINEGUN] = 1;
	inventory[INVENTORY_BULLETS] = 50;

	test_reset_client_commands();
	int best_weapon = BotSelectBestFightWeapon(2, weapon_handle, inventory, 1.0f);
	assert_int_equal(best_weapon, 3);

	bot_input_t input = {0};
	assert_int_equal(EA_GetInput(2, 0.1f, &input), BLERR_NOERROR);
	assert_int_equal(g_test_client_commands.count, 1);
	assert_int_equal(g_test_client_commands.entries[0].client, 2);
	assert_string_equal(g_test_client_commands.entries[0].text, "use");
	assert_string_equal(g_test_client_commands.entries[0].argument, "Machinegun");
	assert_true(g_test_client_commands.entries[0].terminated);

	test_reset_client_commands();
	best_weapon = BotSelectBestFightWeapon(2, weapon_handle, inventory, 2.0f);
	assert_int_equal(best_weapon, 3);
	assert_int_equal(EA_GetInput(2, 0.1f, &input), BLERR_NOERROR);
	assert_int_equal(g_test_client_commands.count, 0);

	inventory[INVENTORY_ROCKETLAUNCHER] = 1;
	inventory[INVENTORY_ROCKETS] = 10;
	inventory[ENEMY_HORIZONTAL_DIST] = 999;

	best_weapon = BotSelectBestFightWeapon(2, weapon_handle, inventory, 6.0f);
	assert_int_equal(best_weapon, 6);
	assert_int_equal(EA_GetInput(2, 0.1f, &input), BLERR_NOERROR);
	assert_int_equal(g_test_client_commands.count, 1);
	assert_string_equal(g_test_client_commands.entries[0].text, "use");
	assert_string_equal(g_test_client_commands.entries[0].argument, "Rocket Launcher");
	assert_true(g_test_client_commands.entries[0].terminated);

	test_reset_client_commands();
	for (int index = 0; index < selector_weights->index_count; ++index)
	{
		selector_weights->index_by_weapon[index] = -1;
	}
	best_weapon = BotSelectBestFightWeapon(2, weapon_handle, inventory, 20.0f);
	assert_int_equal(best_weapon, 6);
	assert_int_equal(BotGetTopRankedWeapon(weapon_handle), 6);
	assert_int_equal(EA_GetInput(2, 0.1f, &input), BLERR_NOERROR);
	assert_int_equal(g_test_client_commands.count, 0);

	BotFreeWeaponState(weapon_handle);
	AI_FreeWeaponWeights(selector_weights);
	AI_UnloadWeaponLibrary(library);
	EA_Shutdown();
	teardown_botlib_environment();
}

/*
=============
test_weapon_selector_honors_live_model_sync

Pins the pre-selector current-model sync performed by Gladiator's bot frame.
=============
*/
static void test_weapon_selector_honors_live_model_sync(void **state)
{
	(void)state;

	char weapon_config_path[512];
	char weight_config_path[512];
	asset_path_or_skip("dev_tools/assets/weapons.c", weapon_config_path, sizeof(weapon_config_path));
	asset_path_or_skip("dev_tools/assets/default/defaul_w.c", weight_config_path, sizeof(weight_config_path));

	(void)weight_config_path;

	setup_botlib_environment();
	Q2Bridge_SetImportTable(&g_test_q2_imports);
	assert_int_equal(EA_Init(MAX_CLIENTS), BLERR_NOERROR);

	ai_weapon_library_t *library = AI_LoadWeaponLibrary(NULL);
	assert_non_null(library);
	assert_string_equal(library->source_path, weapon_config_path);

	const bot_weapon_config_t *weapon_config = AI_GetWeaponConfig(library);
	assert_non_null(weapon_config);

	int machinegun_index = weapon_index_by_name(weapon_config, "Machinegun");
	int rocket_index = weapon_index_by_name(weapon_config, "Rocket Launcher");
	assert_true(machinegun_index >= 0);
	assert_true(rocket_index >= 0);

	int weapon_handle = BotAllocWeaponState();
	assert_true(weapon_handle > 0);
	assert_int_equal(BotLoadWeaponWeights(weapon_handle, "default/defaul_w.c"), BLERR_NOERROR);

	int inventory[MAX_ITEMS];
	memset(inventory, 0, sizeof(inventory));
	inventory[INVENTORY_BLASTER] = 1;
	inventory[INVENTORY_MACHINEGUN] = 1;
	inventory[INVENTORY_BULLETS] = 50;

	BotWeaponStateSetCurrentModel(weapon_handle, weapon_config->weapons[machinegun_index].model);

	test_reset_client_commands();
	int best_weapon = BotSelectBestFightWeapon(2, weapon_handle, inventory, 1.0f);
	assert_int_equal(best_weapon, 3);
	assert_int_equal(BotGetTopRankedWeapon(weapon_handle), 3);

	bot_input_t input = {0};
	assert_int_equal(EA_GetInput(2, 0.1f, &input), BLERR_NOERROR);
	assert_int_equal(g_test_client_commands.count, 0);

	inventory[INVENTORY_ROCKETLAUNCHER] = 1;
	inventory[INVENTORY_ROCKETS] = 10;
	inventory[ENEMY_HORIZONTAL_DIST] = 999;

	best_weapon = BotSelectBestFightWeapon(2, weapon_handle, inventory, 2.0f);
	assert_int_equal(best_weapon, 6);
	assert_int_equal(EA_GetInput(2, 0.1f, &input), BLERR_NOERROR);
	assert_int_equal(g_test_client_commands.count, 1);
	assert_string_equal(g_test_client_commands.entries[0].text, "use");
	assert_string_equal(g_test_client_commands.entries[0].argument, "Rocket Launcher");
	assert_true(g_test_client_commands.entries[0].terminated);

	BotFreeWeaponState(weapon_handle);
	AI_UnloadWeaponLibrary(library);
	EA_Shutdown();
	teardown_botlib_environment();
}

/*
=============
test_weapon_frame_sync_supplies_cached_client_and_inventory

Pins Gladiator's frame sync helper that stores client, inventory, and model.
=============
*/
static void test_weapon_frame_sync_supplies_cached_client_and_inventory(void **state)
{
	(void)state;

	char weapon_config_path[512];
	char weight_config_path[512];
	asset_path_or_skip("dev_tools/assets/weapons.c", weapon_config_path, sizeof(weapon_config_path));
	asset_path_or_skip("dev_tools/assets/default/defaul_w.c", weight_config_path, sizeof(weight_config_path));

	(void)weight_config_path;

	setup_botlib_environment();
	Q2Bridge_SetImportTable(&g_test_q2_imports);
	assert_int_equal(EA_Init(MAX_CLIENTS), BLERR_NOERROR);

	ai_weapon_library_t *library = AI_LoadWeaponLibrary(NULL);
	assert_non_null(library);
	assert_string_equal(library->source_path, weapon_config_path);

	const bot_weapon_config_t *weapon_config = AI_GetWeaponConfig(library);
	assert_non_null(weapon_config);

	int blaster_index = weapon_index_by_name(weapon_config, "Blaster");
	assert_true(blaster_index >= 0);

	int weapon_handle = BotAllocWeaponState();
	assert_true(weapon_handle > 0);
	assert_int_equal(BotLoadWeaponWeights(weapon_handle, "default/defaul_w.c"), BLERR_NOERROR);

	int inventory[MAX_ITEMS];
	memset(inventory, 0, sizeof(inventory));
	inventory[INVENTORY_BLASTER] = 1;
	inventory[INVENTORY_MACHINEGUN] = 1;
	inventory[INVENTORY_BULLETS] = 50;

	BotWeaponStateSyncFrame(weapon_handle,
							2,
							inventory,
							weapon_config->weapons[blaster_index].model);

	assert_int_equal(BotChooseBestFightWeapon(weapon_handle, NULL), 3);

	BotWeaponStateSyncFrame(weapon_handle,
							2,
							inventory,
							weapon_config->weapons[blaster_index].model);

	test_reset_client_commands();
	int best_weapon = BotSelectBestFightWeapon(-1, weapon_handle, NULL, 1.0f);
	assert_int_equal(best_weapon, 3);

	bot_input_t input = {0};
	assert_int_equal(EA_GetInput(2, 0.1f, &input), BLERR_NOERROR);
	assert_int_equal(g_test_client_commands.count, 1);
	assert_int_equal(g_test_client_commands.entries[0].client, 2);
	assert_string_equal(g_test_client_commands.entries[0].text, "use");
	assert_string_equal(g_test_client_commands.entries[0].argument, "Machinegun");
	assert_true(g_test_client_commands.entries[0].terminated);

	BotResetWeaponState(weapon_handle);
	assert_int_equal(BotChooseBestFightWeapon(weapon_handle, NULL), 0);

	BotFreeWeaponState(weapon_handle);
	AI_UnloadWeaponLibrary(library);
	EA_Shutdown();
	teardown_botlib_environment();
}

/*
=============
test_weapon_reset_preserves_weights_and_clears_activation_gate

Pins Gladiator's reset helper: keep weights, clear current selection and delay.
=============
*/
static void test_weapon_reset_preserves_weights_and_clears_activation_gate(void **state)
{
	(void)state;

	char weapon_config_path[512];
	char weight_config_path[512];
	asset_path_or_skip("dev_tools/assets/weapons.c", weapon_config_path, sizeof(weapon_config_path));
	asset_path_or_skip("dev_tools/assets/default/defaul_w.c", weight_config_path, sizeof(weight_config_path));

	(void)weight_config_path;

	setup_botlib_environment();
	Q2Bridge_SetImportTable(&g_test_q2_imports);
	assert_int_equal(EA_Init(MAX_CLIENTS), BLERR_NOERROR);

	ai_weapon_library_t *library = AI_LoadWeaponLibrary(NULL);
	assert_non_null(library);
	assert_string_equal(library->source_path, weapon_config_path);

	const bot_weapon_config_t *weapon_config = AI_GetWeaponConfig(library);
	assert_non_null(weapon_config);

	int machinegun_index = weapon_index_by_name(weapon_config, "Machinegun");
	assert_true(machinegun_index >= 0);

	int weapon_handle = BotAllocWeaponState();
	assert_true(weapon_handle > 0);
	assert_int_equal(BotLoadWeaponWeights(weapon_handle, "default/defaul_w.c"), BLERR_NOERROR);

	int inventory[MAX_ITEMS];
	memset(inventory, 0, sizeof(inventory));
	inventory[INVENTORY_BLASTER] = 1;
	inventory[INVENTORY_MACHINEGUN] = 1;
	inventory[INVENTORY_BULLETS] = 50;

	test_reset_client_commands();
	int best_weapon = BotSelectBestFightWeapon(2, weapon_handle, inventory, 1.0f);
	assert_int_equal(best_weapon, 3);
	bot_input_t input = {0};
	assert_int_equal(EA_GetInput(2, 0.1f, &input), BLERR_NOERROR);
	assert_int_equal(g_test_client_commands.count, 1);
	assert_string_equal(g_test_client_commands.entries[0].text, "use");
	assert_string_equal(g_test_client_commands.entries[0].argument, "Machinegun");
	assert_true(g_test_client_commands.entries[0].terminated);

	BotResetWeaponState(weapon_handle);
	assert_int_equal(BotGetTopRankedWeapon(weapon_handle), 0);
	BotWeaponStateSetCurrentModel(weapon_handle, weapon_config->weapons[machinegun_index].model);

	inventory[INVENTORY_ROCKETLAUNCHER] = 1;
	inventory[INVENTORY_ROCKETS] = 10;
	inventory[ENEMY_HORIZONTAL_DIST] = 999;

	test_reset_client_commands();
	best_weapon = BotSelectBestFightWeapon(2, weapon_handle, inventory, 2.0f);
	assert_int_equal(best_weapon, 6);
	assert_int_equal(BotGetTopRankedWeapon(weapon_handle), 6);
	assert_int_equal(EA_GetInput(2, 0.1f, &input), BLERR_NOERROR);
	assert_int_equal(g_test_client_commands.count, 1);
	assert_string_equal(g_test_client_commands.entries[0].text, "use");
	assert_string_equal(g_test_client_commands.entries[0].argument, "Rocket Launcher");
	assert_true(g_test_client_commands.entries[0].terminated);

	BotFreeWeaponState(weapon_handle);
	AI_UnloadWeaponLibrary(library);
	EA_Shutdown();
	teardown_botlib_environment();
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_weapon_struct_layout_matches_hlil_offsets),
		cmocka_unit_test(test_pak_only_weapon_config_resolves_sibling_include),
		cmocka_unit_test(test_pak_only_itemconfig_resolves_sibling_include),
        cmocka_unit_test(test_weapon_library_reports_expected_counts),
        cmocka_unit_test(test_weapon_weights_align_with_reference_values),
        cmocka_unit_test(test_weapon_weights_cache_uses_caller_filename),
        cmocka_unit_test(test_weapon_weight_binding_preserves_hlil_load_order),
        cmocka_unit_test(test_weapon_selection_rebinds_unbound_state_weights),
        cmocka_unit_test(test_weapon_model_lookup_and_info_copy_match_hlil_helpers),
        cmocka_unit_test(test_bot_choose_best_fight_weapon_matches_reference),
        cmocka_unit_test(test_weapon_selector_queues_use_command_like_hlil),
        cmocka_unit_test(test_weapon_selector_honors_live_model_sync),
        cmocka_unit_test(test_weapon_frame_sync_supplies_cached_client_and_inventory),
        cmocka_unit_test(test_weapon_reset_preserves_weights_and_clears_activation_gate),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
