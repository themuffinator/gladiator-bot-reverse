#include <stdarg.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <cmocka.h>

#include <math.h>

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

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#include "botlib/ai_character/bot_character.h"
#include "botlib/ai_chat/ai_chat.h"
#include "botlib/ai/ai_dm.h"
#include "botlib/ai_goal/bot_goal.h"
#include "botlib/ai_weapon/bot_weapon.h"
#include "botlib/ai_weight/bot_weight.h"
#include "botlib/common/l_crc.h"
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
#define TEST_RETAIL_CHARACTER_FILENAME_SIZE 0x104

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
    char text[512];
} test_log_message_t;

typedef struct test_client_command_s {
	int client;
	char text[256];
	char argument[256];
	bool terminated;
} test_client_command_t;

typedef struct test_environment_s {
    asset_env_t assets;
    bool libvar_initialised;
    bool memory_initialised;
    bool import_table_set;
    bool library_setup;
    bool client_active;
    ai_weapon_library_t *weapon_library;
    bot_export_extended_t *exports;
} test_environment_t;

typedef union test_raw_character_value_u {
	int integer_value;
	float float_value;
	char *string_value;
} test_raw_character_value_t;

typedef struct test_raw_characteristic_s {
	ai_character_value_type_t type;
	test_raw_character_value_t value;
} test_raw_characteristic_t;

typedef struct test_raw_character_prefix_s {
	int32_t highest_characteristic;
	test_raw_characteristic_t characteristics[1];
} test_raw_character_prefix_t;

typedef struct test_memory_block_prefix_s {
	uint32_t magic;
	uint8_t *payload;
	size_t total_size;
	struct test_memory_block_prefix_s *prev;
	struct test_memory_block_prefix_s *next;
} test_memory_block_prefix_t;

static struct {
    test_log_message_t entries[TEST_MAX_LOG_MESSAGES];
    int count;
} g_test_log;

static struct {
	test_client_command_t entries[TEST_MAX_CLIENT_COMMANDS];
	int count;
} g_test_client_commands;

/*
=============
test_reset_client_commands

Clears captured command tokens, arguments, and terminator state.
=============
*/
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

/*
=============
retail_client_slot_released

Retail allocates its client records as one fixed-stride slab at library setup
and keeps that slab until library shutdown, so shutting a client down clears
its record in place rather than releasing a per-client allocation.  A released
client is therefore recognised by a retained slot whose record is cleared, not
by a null slot pointer.
=============
*/
static bool retail_client_slot_released(int client)
{
	const bot_client_state_t *state = BotState_Get(client);
	return state != NULL && !state->active && state->character == NULL &&
		state->chat_state == NULL;
}

/*
=============
test_log_occurrences

Counts every occurrence of a substring across captured diagnostics.
=============
*/
static int test_log_occurrences(const char *needle)
{
	if (needle == NULL || needle[0] == '\0')
	{
		return 0;
	}

	int occurrences = 0;
	size_t needle_length = strlen(needle);
	for (int i = 0; i < g_test_log.count; ++i)
	{
		const char *cursor = g_test_log.entries[i].text;
		const char *match;
		while ((match = strstr(cursor, needle)) != NULL)
		{
			occurrences++;
			cursor = match + needle_length;
		}
	}

	return occurrences;
}

/*
=============
test_log_matches

Finds one captured diagnostic with the exact priority and rendered text.
=============
*/
static bool test_log_matches(int priority, const char *text)
{
	if (text == NULL)
	{
		return false;
	}

	for (int i = 0; i < g_test_log.count; ++i)
	{
		if (g_test_log.entries[i].priority == priority &&
			strcmp(g_test_log.entries[i].text, text) == 0)
		{
			return true;
		}
	}

	return false;
}

/*
=============
test_log_matches_bracketed

Matches a rendered diagnostic by its leading and trailing text, for messages
whose middle is an environment-dependent resolved path.
=============
*/
static bool test_log_matches_bracketed(int priority,
	const char *prefix,
	const char *suffix)
{
	if (prefix == NULL || suffix == NULL)
	{
		return false;
	}

	const size_t prefix_length = strlen(prefix);
	const size_t suffix_length = strlen(suffix);
	for (int i = 0; i < g_test_log.count; ++i)
	{
		if (g_test_log.entries[i].priority != priority)
		{
			continue;
		}

		const char *text = g_test_log.entries[i].text;
		const size_t length = strlen(text);
		if (length < prefix_length + suffix_length)
		{
			continue;
		}
		if (strncmp(text, prefix, prefix_length) == 0 &&
			strcmp(text + length - suffix_length, suffix) == 0)
		{
			return true;
		}
	}

	return false;
}

/*
=============
test_log_precedes

Checks the exact ordering of two rendered diagnostics in the captured log.
=============
*/
static bool test_log_precedes(int first_priority,
	const char *first_text,
	int second_priority,
	const char *second_text)
{
	int first_index = -1;
	int second_index = -1;

	for (int i = 0; i < g_test_log.count; ++i)
	{
		if (first_index < 0 &&
			g_test_log.entries[i].priority == first_priority &&
			strcmp(g_test_log.entries[i].text, first_text) == 0)
		{
			first_index = i;
		}
		if (second_index < 0 &&
			g_test_log.entries[i].priority == second_priority &&
			strcmp(g_test_log.entries[i].text, second_text) == 0)
		{
			second_index = i;
		}
	}

	return first_index >= 0 && second_index >= 0 && first_index < second_index;
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

/*
=============
test_bot_client_command

Captures the retail token, first argument, and explicit NULL terminator.
=============
*/
static void test_bot_client_command(int client, char *fmt, ...)
{
	if (g_test_client_commands.count >= TEST_MAX_CLIENT_COMMANDS) {
		return;
	}

	test_client_command_t *slot = &g_test_client_commands.entries[g_test_client_commands.count++];
	slot->client = client;
	snprintf(slot->text, sizeof(slot->text), "%s", fmt != NULL ? fmt : "");
	if (fmt != NULL &&
		(strcmp(fmt, "gender") == 0 || strcmp(fmt, "name") == 0 ||
			strcmp(fmt, "say") == 0 || strcmp(fmt, "say_team") == 0))
	{
		va_list args;
		va_start(args, fmt);
		const char *argument = va_arg(args, const char *);
		const void *terminator = va_arg(args, const void *);
		va_end(args);
		snprintf(slot->argument,
			sizeof(slot->argument),
			"%s",
			argument != NULL ? argument : "");
		slot->terminated = terminator == NULL;
	}
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
		BotShutdownCharacterHandles();
		AI_ShutdownCharacterDefinitions();
        BotShutdownWeights();
    }

    if (env->weapon_library != NULL) {
        AI_UnloadWeaponLibrary(env->weapon_library);
        env->weapon_library = NULL;
    }

    if (env->libvar_initialised) {
        LibVar_Shutdown();
        env->libvar_initialised = false;
    }

	if (env->memory_initialised) {
		CRC_ResetSourceChecksums();
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

    env->exports = GetBotAPIEx(&g_test_bot_import, sizeof(g_test_bot_import));
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

/*
=============
setup_retail_test_library

Configures and starts the full retail-facing bot library for client setup tests.
=============
*/
static int setup_retail_test_library(test_environment_t *env)
{
	if (env == NULL || env->exports == NULL)
	{
		return BLERR_INVALIDIMPORT;
	}

	env->exports->BotLibVarSet("basedir", env->assets.asset_root);
	env->exports->BotLibVarSet("gamedir", "");
	env->exports->BotLibVarSet("cddir", "");
	env->exports->BotLibVarSet("gladiator_asset_dir", "");
	env->exports->BotLibVarSet("weaponconfig", "weapons.c");
	env->exports->BotLibVarSet("itemconfig", "items.c");
	env->exports->BotLibVarSet("max_weaponinfo", "64");
	env->exports->BotLibVarSet("max_projectileinfo", "64");

	int status = env->exports->BotSetupLibrary();
	if (status == BLERR_NOERROR)
	{
		env->library_setup = true;
	}
	return status;
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
write_character_pak_u32

Writes one little-endian directory value for a runtime character PAK.
=============
*/
static void write_character_pak_u32(FILE *file, uint32_t value)
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
write_character_pak_fixture

Creates a one-entry Quake PAK for exact character provenance diagnostics.
=============
*/
static void write_character_pak_fixture(const char *path,
	const char *entry_name,
	const char *contents)
{
	size_t content_length = strlen(contents);
	size_t name_length = strlen(entry_name);
	assert_true(content_length <= UINT32_MAX - 12U);
	assert_true(name_length < 56U);

	FILE *file = fopen(path, "wb");
	assert_non_null(file);
	assert_int_equal(fwrite("PACK", 1U, 4U, file), 4U);
	write_character_pak_u32(file, 12U + (uint32_t)content_length);
	write_character_pak_u32(file, 64U);
	assert_int_equal(fwrite(contents, 1U, content_length, file), content_length);

	unsigned char name[56] = { 0 };
	memcpy(name, entry_name, name_length);
	assert_int_equal(fwrite(name, 1U, sizeof(name), file), sizeof(name));
	write_character_pak_u32(file, 12U);
	write_character_pak_u32(file, (uint32_t)content_length);
	assert_int_equal(fclose(file), 0);
}

/*
=============
remove_character_pak_fixture

Removes only the known package, extracted entry, and private fixture folders.
=============
*/
static void remove_character_pak_fixture(const char *fixture_root)
{
	char path[PATH_MAX];
	int written = snprintf(path,
		sizeof(path),
		"%s/.pak_cache/pak0/bots/retail_pak_log_tmp_c.c",
		fixture_root);
	if (written > 0 && written < (int)sizeof(path))
	{
		(void)remove(path);
	}

	const char *directories[] = {
		"/.pak_cache/pak0/bots",
		"/.pak_cache/pak0",
		"/.pak_cache",
		"",
	};
	for (size_t i = 0; i < sizeof(directories) / sizeof(directories[0]); ++i)
	{
		written = snprintf(path,
			sizeof(path),
			"%s%s",
			fixture_root,
			directories[i]);
		if (written > 0 && written < (int)sizeof(path))
		{
			(void)TEST_RMDIR(path);
		}
	}

	written = snprintf(path, sizeof(path), "%s/pak0.pak", fixture_root);
	if (written > 0 && written < (int)sizeof(path))
	{
		(void)remove(path);
	}
	(void)TEST_RMDIR(fixture_root);
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

/*
=============
test_retail_packed_character_allocation_layout

Pins the raw highest-index prefix, native slot geometry, and trailing strings.
=============
*/
static void test_retail_packed_character_allocation_layout(void **state)
{
	test_environment_t *env = (test_environment_t *)(*state);
	assert_non_null(env);

	static const char first_string[] = "alpha";
	static const char last_string[] = "omega";
	static const int expected_highest = 5;
	char fixture_path[PATH_MAX];
	int written = snprintf(fixture_path,
		sizeof(fixture_path),
		"%s/tests/support/assets/bots/retail_packed_layout_tmp_c.c",
		PROJECT_SOURCE_DIR);
	assert_true(written > 0 && written < (int)sizeof(fixture_path));
	(void)remove(fixture_path);
	write_character_fixture(fixture_path,
		"character \"packedlayout\"\n"
		"{\n"
		"0 \"alpha\"\n"
		"2 17\n"
		"5 \"omega\"\n"
		"}\n");

	bot_character_t *character =
		AI_LoadCharacterDefinition(fixture_path, "packedlayout");
	bool loaded = character != NULL;
	bool raw_header_matches = false;
	bool raw_slots_match = false;
	bool trailing_strings_match = false;
	bool packed_size_matches = false;
	size_t slot_offset = offsetof(test_raw_character_prefix_t, characteristics);
	size_t slot_size = sizeof(test_raw_characteristic_t);
	size_t expected_payload_size = slot_offset +
		(size_t)(expected_highest + 1) * slot_size +
		sizeof(first_string) + sizeof(last_string);

	if (character != NULL)
	{
		test_raw_character_prefix_t *raw =
			(test_raw_character_prefix_t *)character;
		raw_header_matches = raw->highest_characteristic == expected_highest &&
			AI_CharacteristicDefinitionCount(character) == expected_highest;
		raw_slots_match =
			raw->characteristics[0].type == AI_CHARACTER_VALUE_STRING &&
			raw->characteristics[1].type == AI_CHARACTER_VALUE_NONE &&
			raw->characteristics[2].type == AI_CHARACTER_VALUE_INTEGER &&
			raw->characteristics[2].value.integer_value == 17 &&
			raw->characteristics[expected_highest].type ==
				AI_CHARACTER_VALUE_STRING;

		char *string_storage = (char *)character + slot_offset +
			(size_t)(expected_highest + 1) * slot_size;
		trailing_strings_match =
			raw->characteristics[0].value.string_value == string_storage &&
			raw->characteristics[expected_highest].value.string_value ==
				string_storage + sizeof(first_string) &&
			strcmp(string_storage, first_string) == 0 &&
			strcmp(string_storage + sizeof(first_string), last_string) == 0;

		size_t tracked_size = MemoryByteSize(character);
		packed_size_matches = tracked_size ==
			sizeof(test_memory_block_prefix_t) + expected_payload_size;
		AI_FreeCharacterDefinition(character);
	}
	int remove_status = remove(fixture_path);

	assert_true(loaded);
	assert_true(raw_header_matches);
	assert_true(raw_slots_match);
	assert_true(trailing_strings_match);
#if UINTPTR_MAX == UINT32_MAX
	assert_int_equal(slot_offset, 4U);
	assert_int_equal(slot_size, 8U);
	assert_int_equal(expected_payload_size,
		4U + 8U * (size_t)(expected_highest + 1) +
		sizeof(first_string) + sizeof(last_string));
#else
	assert_int_equal(slot_offset, 8U);
	assert_int_equal(slot_size, 16U);
#endif
	assert_true(packed_size_matches);
	assert_int_equal(remove_status, 0);
}

/*
=============
test_retail_characteristic_integer_ftol_edges

Pins x87 __ftol's 64-bit conversion followed by the retail low-dword store.
=============
*/
static void test_retail_characteristic_integer_ftol_edges(void **state)
{
	test_environment_t *env = (test_environment_t *)(*state);
	assert_non_null(env);

	char fixture_path[PATH_MAX];
	int written = snprintf(fixture_path,
		sizeof(fixture_path),
		"%s/tests/support/assets/bots/retail_ftol_edges_tmp_c.c",
		PROJECT_SOURCE_DIR);
	assert_true(written > 0 && written < (int)sizeof(fixture_path));
	(void)remove(fixture_path);
	write_character_fixture(fixture_path,
		"character \"ftoledges\"\n"
		"{\n"
		"0 2147483648.0\n"
		"1 4294967296.0\n"
		"2 1.0\n"
		"3 100000000000000000000.0\n"
		"4 3458179.375\n"
		"5 0xffffffff\n"
		"6 4294967296\n"
		"7 0\n"
		"}\n");

	bot_character_t *character =
		AI_LoadCharacterDefinition(fixture_path, "ftoledges");
	bool loaded = character != NULL;
	bool source_values_match = false;
	int positive_int32_overflow = 0;
	int positive_low_dword_wrap = 0;
	int negative_low_dword_wrap = 0;
	int positive_int64_overflow = 0;
	uint32_t token_float_bits = 0;
	float nan_value_bound = 0.0f;
	float nan_minimum_bound = 0.0f;
	if (character != NULL)
	{
		test_raw_character_prefix_t *raw =
			(test_raw_character_prefix_t *)character;
		source_values_match =
			raw->characteristics[0].type == AI_CHARACTER_VALUE_FLOAT &&
			raw->characteristics[0].value.float_value == 2147483648.0f &&
			raw->characteristics[1].type == AI_CHARACTER_VALUE_FLOAT &&
			raw->characteristics[1].value.float_value == 4294967296.0f &&
			raw->characteristics[2].type == AI_CHARACTER_VALUE_FLOAT &&
			raw->characteristics[3].type == AI_CHARACTER_VALUE_FLOAT &&
			raw->characteristics[4].type == AI_CHARACTER_VALUE_FLOAT &&
			raw->characteristics[5].type == AI_CHARACTER_VALUE_INTEGER &&
			raw->characteristics[5].value.integer_value == -1 &&
			raw->characteristics[6].type == AI_CHARACTER_VALUE_INTEGER &&
			raw->characteristics[6].value.integer_value == 0 &&
			isfinite(raw->characteristics[3].value.float_value) &&
			raw->characteristics[3].value.float_value > (float)INT64_MAX;
		memcpy(&token_float_bits,
			&raw->characteristics[4].value.float_value,
			sizeof(token_float_bits));

		/* The lexer exposes '-' separately, so inject one parsed float slot. */
		raw->characteristics[2].value.float_value = -2147483904.0f;
		positive_int32_overflow = Characteristic_Integer(character, 0);
		positive_low_dword_wrap = Characteristic_Integer(character, 1);
		negative_low_dword_wrap = Characteristic_Integer(character, 2);
		positive_int64_overflow = Characteristic_Integer(character, 3);
		raw->characteristics[2].value.float_value = NAN;
		nan_value_bound = Characteristic_BFloat(character, 2, -7.0f, 7.0f);
		nan_minimum_bound = Characteristic_BFloat(character,
			4,
			NAN,
			5000000.0f);
		AI_FreeCharacterDefinition(character);
	}
	int remove_status = remove(fixture_path);

	assert_true(loaded);
	assert_true(source_values_match);
	assert_int_equal(positive_int32_overflow, INT_MIN);
	assert_int_equal(positive_low_dword_wrap, 0);
	assert_int_equal(negative_low_dword_wrap, 2147483392);
	assert_int_equal(positive_int64_overflow, 0);
	assert_int_equal(token_float_bits, UINT32_C(0x4a53120d));
	assert_float_equal(nan_value_bound, -7.0f, 0.0f);
	assert_true(isnan(nan_minimum_bound));
	assert_int_equal(remove_status, 0);
}

/*
=============
test_retail_character_success_log_uses_logical_request

Requires the exact caller-supplied name and loose logical filename in the log.
=============
*/
static void test_retail_character_success_log_uses_logical_request(void **state)
{
	test_environment_t *env = (test_environment_t *)(*state);
	assert_non_null(env);

	static const char logical_name[] = "bots/retail_loose_log_tmp_c.c";
	static const char requested_name[] = "requested_loose";
	char fixture_root[PATH_MAX];
	char fixture_path[PATH_MAX];
	int written = snprintf(fixture_root,
		sizeof(fixture_root),
		"%s/tests/support/assets",
		PROJECT_SOURCE_DIR);
	assert_true(written > 0 && written < (int)sizeof(fixture_root));
	written = snprintf(fixture_path,
		sizeof(fixture_path),
		"%s/%s",
		fixture_root,
		logical_name);
	assert_true(written > 0 && written < (int)sizeof(fixture_path));
	(void)remove(fixture_path);
	write_character_fixture(fixture_path,
		"character \"requested_loose\"\n"
		"{\n"
		"0 \"Logical Name\"\n"
		"1 0\n"
		"}\n");

	LibVarSet("gladiator_asset_dir", fixture_root);
	test_reset_log();
	bot_character_t *character =
		BotLoadCharacter(logical_name, requested_name);
	char expected_log[PATH_MAX];
	written = snprintf(expected_log,
		sizeof(expected_log),
		"loaded %s from %s\n",
		requested_name,
		logical_name);
	assert_true(written > 0 && written < (int)sizeof(expected_log));
	bool exact_log = test_log_matches(PRT_MESSAGE, expected_log);
	bool physical_path_hidden = !test_log_contains(fixture_path);
	if (character != NULL)
	{
		BotFreeCharacter(character);
	}
	LibVarSet("gladiator_asset_dir", "");
	int remove_status = remove(fixture_path);

	assert_non_null(character);
	assert_true(exact_log);
	assert_true(physical_path_hidden);
	assert_int_equal(remove_status, 0);
}

/*
=============
test_retail_character_success_log_reports_pak_provenance

Requires a non-empty PAK entry to log its container plus the logical filename.
=============
*/
static void test_retail_character_success_log_reports_pak_provenance(void **state)
{
	test_environment_t *env = (test_environment_t *)(*state);
	assert_non_null(env);

	static const char logical_name[] = "bots/retail_pak_log_tmp_c.c";
	static const char requested_name[] = "requested_pak";
	char fixture_root[PATH_MAX];
	char pak_path[PATH_MAX];
	int written = snprintf(fixture_root,
		sizeof(fixture_root),
		"%s/tests/support/assets/__gladiator_character_pak_fixture",
		PROJECT_SOURCE_DIR);
	assert_true(written > 0 && written < (int)sizeof(fixture_root));
	written = snprintf(pak_path,
		sizeof(pak_path),
		"%s/pak0.pak",
		fixture_root);
	assert_true(written > 0 && written < (int)sizeof(pak_path));

	remove_character_pak_fixture(fixture_root);
	assert_int_equal(TEST_MKDIR(fixture_root), 0);
	write_character_pak_fixture(pak_path,
		logical_name,
		"character \"requested_pak\"\n"
		"{\n"
		"0 \"Package Name\"\n"
		"1 0\n"
		"}\n");

	LibVarSet("gladiator_asset_dir", fixture_root);
	test_reset_log();
	bot_character_t *character =
		BotLoadCharacter(logical_name, requested_name);
	char expected_log[PATH_MAX * 2];
	written = snprintf(expected_log,
		sizeof(expected_log),
		"loaded %s from %s\\%s\n",
		requested_name,
		pak_path,
		logical_name);
	assert_true(written > 0 && written < (int)sizeof(expected_log));
	bool exact_log = test_log_matches(PRT_MESSAGE, expected_log);
	if (character != NULL)
	{
		BotFreeCharacter(character);
	}
	LibVarSet("gladiator_asset_dir", "");
	remove_character_pak_fixture(fixture_root);
	FILE *pak_probe = fopen(pak_path, "rb");
	bool pak_removed = pak_probe == NULL;
	if (pak_probe != NULL)
	{
		fclose(pak_probe);
	}

	assert_non_null(character);
	assert_true(exact_log);
	assert_true(pak_removed);
}

/*
=============
test_retail_character_filename_copy_and_writable_empty

Pins the retail 0x104-byte filename copy and writable empty-string sentinel.
=============
*/
static void test_retail_character_filename_copy_and_writable_empty(void **state)
{
	test_environment_t *env = (test_environment_t *)(*state);
	assert_non_null(env);

	test_reset_log();
	assert_null(BotLoadCharacter("", "empty_filename"));
	assert_true(test_log_matches(PRT_ERROR, "couldn't find \n"));

	char retail_filename[TEST_RETAIL_CHARACTER_FILENAME_SIZE];
	int written = snprintf(retail_filename,
		sizeof(retail_filename),
		"%s/tests/support/assets/bots/",
		PROJECT_SOURCE_DIR);
	assert_true(written > 0 &&
		written < TEST_RETAIL_CHARACTER_FILENAME_SIZE - 1);
	size_t prefix_length = (size_t)written;
	memset(retail_filename + prefix_length,
		'r',
		(TEST_RETAIL_CHARACTER_FILENAME_SIZE - 1U) - prefix_length);
	retail_filename[TEST_RETAIL_CHARACTER_FILENAME_SIZE - 1] = '\0';
	assert_int_equal(strlen(retail_filename),
		TEST_RETAIL_CHARACTER_FILENAME_SIZE - 1U);

	(void)remove(retail_filename);
	write_character_fixture(retail_filename,
		"character \"longcopy\"\n"
		"{\n"
		"0 \"Long Filename Copy\"\n"
		"2 2\n"
		"}\n");

	char overlong_request[TEST_RETAIL_CHARACTER_FILENAME_SIZE + 32];
	written = snprintf(overlong_request,
		sizeof(overlong_request),
		"%signored_suffix",
		retail_filename);
	assert_true(written >= TEST_RETAIL_CHARACTER_FILENAME_SIZE &&
		written < (int)sizeof(overlong_request));

	test_reset_log();
	bot_character_t *character =
		BotLoadCharacter(overlong_request, "longcopy");
	assert_non_null(character);
	char expected_log[512];
	written = snprintf(expected_log,
		sizeof(expected_log),
		"loaded longcopy from %s\n",
		retail_filename);
	assert_true(written > 0 && written < (int)sizeof(expected_log));
	assert_true(test_log_matches(PRT_MESSAGE, expected_log));
	assert_string_equal(Characteristic_String(character, 0),
		"Long Filename Copy");

	test_reset_log();
	char *uninitialized = Characteristic_String(character, 1);
	assert_non_null(uninitialized);
	assert_string_equal(uninitialized, "");
	assert_true(test_log_matches(PRT_ERROR,
		"characteristic 1 is not initialized\n"));
	uninitialized[0] = 'x';
	assert_int_equal(uninitialized[0], 'x');
	uninitialized[0] = '\0';

	test_reset_log();
	char *invalid = Characteristic_String(character, 3);
	assert_ptr_equal(invalid, uninitialized);
	assert_string_equal(invalid, "");
	assert_true(test_log_matches(PRT_ERROR,
		"characteristic 3 does not exist\n"));
	invalid[0] = 'y';
	assert_int_equal(invalid[0], 'y');
	invalid[0] = '\0';

	BotFreeCharacter(character);
	assert_int_equal(remove(retail_filename), 0);
}

/*
=============
test_retail_named_character_profile

Pins named retail loading to a definition-only profile and its hidden maximum slot.
=============
*/
static void test_retail_named_character_profile(void **state)
{
	test_environment_t *env = (test_environment_t *)(*state);
	assert_non_null(env);

	test_reset_log();
	ai_character_profile_t *profile =
		AI_LoadCharacterNamed("bots/babe_c.c", "babe", 1.0f);
	assert_non_null(profile);
	assert_string_equal(profile->character_name, "babe");
	assert_non_null(profile->definition_blob);
	assert_null(AI_ItemWeightsForCharacter(profile));
	assert_null(AI_WeaponWeightsForCharacter(profile));
	assert_true(test_log_contains("loaded babe from bots/babe_c.c"));
	assert_false(test_log_contains("bytes item weights"));
	assert_false(test_log_contains("bytes weapon weights"));
	assert_false(test_log_contains("bytes chat file"));

	assert_int_equal(AI_CharacteristicCount(profile), CHARACTERISTIC_BUTTKISSER);
	assert_string_equal(AI_CharacteristicAsString(profile, CHARACTERISTIC_CHAT_FILE),
		"bots/babe_t.c");
	assert_true(fabsf(AI_CharacteristicAsFloat(profile,
		CHARACTERISTIC_AGGRESSION) - 0.7f) < 0.0001f);
	assert_true(fabsf(AI_CharacteristicAsFloat(profile,
		CHARACTERISTIC_GRAPPLE_USER) - 1.0f) < 0.0001f);
	assert_int_equal(AI_CharacteristicAsInteger(profile, CHARACTERISTIC_CHAT_CPM), 400);

	test_reset_log();
	assert_true(fabsf(AI_CharacteristicAsFloat(profile,
		CHARACTERISTIC_BUTTKISSER)) < 0.0001f);
	assert_true(test_log_contains("characteristic 53 does not exist"));

	AI_FreeCharacter(profile);
}

/*
=============
test_retail_character_parser_edges

Pins retail multi-block merging, duplicate rejection, trailing validation, and EOF acceptance.
=============
*/
static void test_retail_character_parser_edges(void **state)
{
	test_environment_t *env = (test_environment_t *)(*state);
	assert_non_null(env);

	char merge_path[PATH_MAX];
	char duplicate_path[PATH_MAX];
	char trailing_path[PATH_MAX];
	char eof_path[PATH_MAX];
	char long_name_path[PATH_MAX];
	int written = snprintf(merge_path,
		sizeof(merge_path),
		"%s/tests/support/assets/bots/retail_merge_tmp_c.c",
		PROJECT_SOURCE_DIR);
	assert_true(written > 0 && written < (int)sizeof(merge_path));
	written = snprintf(duplicate_path,
		sizeof(duplicate_path),
		"%s/tests/support/assets/bots/retail_duplicate_tmp_c.c",
		PROJECT_SOURCE_DIR);
	assert_true(written > 0 && written < (int)sizeof(duplicate_path));
	written = snprintf(trailing_path,
		sizeof(trailing_path),
		"%s/tests/support/assets/bots/retail_trailing_tmp_c.c",
		PROJECT_SOURCE_DIR);
	assert_true(written > 0 && written < (int)sizeof(trailing_path));
	written = snprintf(eof_path,
		sizeof(eof_path),
		"%s/tests/support/assets/bots/retail_eof_tmp_c.c",
		PROJECT_SOURCE_DIR);
	assert_true(written > 0 && written < (int)sizeof(eof_path));
	written = snprintf(long_name_path,
		sizeof(long_name_path),
		"%s/tests/support/assets/bots/retail_long_name_tmp_c.c",
		PROJECT_SOURCE_DIR);
	assert_true(written > 0 && written < (int)sizeof(long_name_path));

	write_character_fixture(merge_path,
		"character \"merge\"\n"
		"{\n"
		"0 \"Merged Retail\"\n"
		"2 2\n"
		"}\n"
		"character \"other\"\n"
		"{\n"
		"0 \"Other\"\n"
		"}\n"
		"character \"merge\"\n"
		"{\n"
		"3 3.75\n"
		"5 \"raw/value.w\"\n"
		"6 99\n"
		"}\n");
	write_character_fixture(duplicate_path,
		"character \"duplicate\" { 0 \"first\" }\n"
		"character \"duplicate\" { 0 \"second\" }\n");
	write_character_fixture(trailing_path,
		"character \"trailing\" { 0 \"valid block\" }\n"
		"unexpected_definition { }\n");
	write_character_fixture(eof_path,
		"character \"eof\"\n"
		"{\n"
		"0 \"Accepted EOF\"\n"
		"1 7\n");
	char long_name[141];
	memset(long_name, 'L', sizeof(long_name) - 1);
	long_name[sizeof(long_name) - 1] = '\0';
	char long_name_fixture[512];
	written = snprintf(long_name_fixture,
		sizeof(long_name_fixture),
		"character \"%s\"\n"
		"{\n"
		"0 \"Long Name Match\"\n"
		"1 11\n"
		"}\n",
		long_name);
	assert_true(written > 0 && written < (int)sizeof(long_name_fixture));
	write_character_fixture(long_name_path, long_name_fixture);

	bot_character_t *character = BotLoadCharacter(merge_path, "merge");
	assert_non_null(character);
	assert_int_equal(AI_CharacteristicDefinitionCount(character), 6);
	const char *raw_name = Characteristic_String(character, 0);
	assert_non_null(raw_name);
	assert_string_equal(raw_name, "Merged Retail");
	assert_ptr_equal(raw_name, AI_CharacteristicDefinitionAsString(character, 0));
	assert_int_equal(Characteristic_Integer(character, 2), 2);
	assert_true(fabsf(Characteristic_Float(character, 3) - 3.75f) < 0.0001f);
	assert_string_equal(Characteristic_String(character, 5), "raw/value.w");
	assert_int_equal(Characteristic_BInteger(character, 2, 3, 8), 3);
	test_reset_log();
	assert_int_equal(Characteristic_Integer(character, 6), 0);
	assert_true(test_log_contains("characteristic 6 does not exist"));
	BotFreeCharacter(character);

	test_reset_log();
	assert_null(BotLoadCharacter(merge_path, ""));
	assert_true(test_log_contains("couldn't find character "));

	test_reset_log();
	assert_null(BotLoadCharacter(duplicate_path, "duplicate"));
	assert_true(test_log_contains("characteristic 0 already initialized"));
	char diagnostic_prefix[PATH_MAX + 32];
	written = snprintf(diagnostic_prefix,
		sizeof(diagnostic_prefix),
		"file %s, line ",
		duplicate_path);
	assert_true(written > 0 && written < (int)sizeof(diagnostic_prefix));
	assert_true(test_log_contains(diagnostic_prefix));

	test_reset_log();
	assert_null(BotLoadCharacter(trailing_path, "trailing"));
	assert_true(test_log_contains("unknown definition unexpected_definition"));
	written = snprintf(diagnostic_prefix,
		sizeof(diagnostic_prefix),
		"file %s, line ",
		trailing_path);
	assert_true(written > 0 && written < (int)sizeof(diagnostic_prefix));
	assert_true(test_log_contains(diagnostic_prefix));

	test_reset_log();
	character = BotLoadCharacter(eof_path, "eof");
	assert_non_null(character);
	assert_int_equal(AI_CharacteristicDefinitionCount(character), 1);
	assert_string_equal(Characteristic_String(character, 0), "Accepted EOF");
	assert_int_equal(test_log_occurrences("couldn't read expected token"), 2);
	BotFreeCharacter(character);

	character = BotLoadCharacter(long_name_path, long_name);
	assert_non_null(character);
	assert_int_equal(AI_CharacteristicDefinitionCount(character), 1);
	assert_string_equal(Characteristic_String(character, 0), "Long Name Match");
	BotFreeCharacter(character);

	assert_int_equal(remove(long_name_path), 0);
	assert_int_equal(remove(eof_path), 0);
	assert_int_equal(remove(trailing_path), 0);
	assert_int_equal(remove(duplicate_path), 0);
	assert_int_equal(remove(merge_path), 0);
}

/*
=============
test_retail_character_handle_exports

Confirms named Gladiator handle entries are fresh and use the handle accessor ABI.
=============
*/
static void test_retail_character_handle_exports(void **state)
{
	test_environment_t *env = (test_environment_t *)(*state);
	assert_non_null(env);

	LibVarSet("bot_reloadcharacters", "0");
	int first_handle =
		BotLoadNamedCharacterHandle("bots/babe_c.c", "babe", 1.0f);
	int second_handle =
		BotLoadNamedCharacterHandle("bots/babe_c.c", "babe", 1.0f);
	assert_true(first_handle > 0);
	assert_true(second_handle > 0);
	assert_int_not_equal(first_handle, second_handle);

	ai_character_profile_t *first = BotCharacterFromHandle(first_handle);
	ai_character_profile_t *second = BotCharacterFromHandle(second_handle);
	assert_non_null(first);
	assert_non_null(second);
	assert_ptr_not_equal(first, second);
	assert_ptr_not_equal(first->definition_blob, second->definition_blob);
	assert_null(AI_ItemWeightsForCharacter(first));
	assert_null(AI_WeaponWeightsForCharacter(first));

	assert_true(fabsf(Characteristic_FloatHandle(first_handle,
		CHARACTERISTIC_AGGRESSION) - 0.7f) < 0.0001f);
	assert_int_equal(Characteristic_IntegerHandle(first_handle,
		CHARACTERISTIC_CHAT_CPM), 400);
	char chat_file[64];
	Characteristic_StringHandle(first_handle,
		CHARACTERISTIC_CHAT_FILE,
		chat_file,
		sizeof(chat_file));
	assert_string_equal(chat_file, "bots/babe_t.c");

	test_reset_log();
	Characteristic_StringHandle(first_handle,
		CHARACTERISTIC_CHAT_CPM,
		chat_file,
		sizeof(chat_file));
	assert_string_equal(chat_file, "bots/babe_t.c");
	assert_true(test_log_contains("characteristic 14 is not a string"));

	BotFreeCharacterHandle(first_handle);
	assert_null(BotCharacterFromHandle(first_handle));
	assert_non_null(BotCharacterFromHandle(second_handle));
	BotFreeCharacterHandle(second_handle);
	assert_null(BotCharacterFromHandle(second_handle));

	test_reset_log();
	assert_int_equal(BotLoadNamedCharacterHandle("bots/does_not_exist.c",
		"babe",
		1.0f), 0);
	assert_true(test_log_contains("couldn't find bots/does_not_exist.c"));

	test_reset_log();
	assert_int_equal(BotLoadNamedCharacterHandle("bots/babe_c.c",
		"Babe",
		1.0f), 0);
	assert_true(test_log_contains("couldn't find character Babe"));
}

/*
=============
test_q3_handle_failure_diagnostics

Pins Q3 fatal handle diagnostics, bounded-accessor ordering, and buffer preservation.
=============
*/
static void test_q3_handle_failure_diagnostics(void **state)
{
	test_environment_t *env = (test_environment_t *)(*state);
	assert_non_null(env);

	test_reset_log();
	assert_null(BotCharacterFromHandle(0));
	assert_true(test_log_matches(PRT_FATAL,
		"character handle 0 out of range\n"));

	test_reset_log();
	BotFreeCharacterHandle(0);
	assert_int_equal(g_test_log.count, 0);

	LibVarSet("bot_reloadcharacters", "1");
	test_reset_log();
	BotFreeCharacterHandle(0);
	assert_true(test_log_matches(PRT_FATAL,
		"character handle 0 out of range\n"));
	LibVarSet("bot_reloadcharacters", "0");

	char invalid_character_log[64];
	int written = snprintf(invalid_character_log,
		sizeof(invalid_character_log),
		"invalid character %d\n",
		MAX_CLIENTS);
	assert_true(written > 0 && written < (int)sizeof(invalid_character_log));
	test_reset_log();
	assert_null(BotCharacterFromHandle(MAX_CLIENTS));
	assert_true(test_log_matches(PRT_FATAL, invalid_character_log));

	test_reset_log();
	assert_true(fabsf(Characteristic_BFloatHandle(0,
		CHARACTERISTIC_ATTACK_SKILL,
		2.0f,
		1.0f)) < 0.0001f);
	assert_true(test_log_matches(PRT_FATAL,
		"character handle 0 out of range\n"));
	assert_false(test_log_contains("cannot bound characteristic"));

	test_reset_log();
	assert_int_equal(Characteristic_BIntegerHandle(0,
		CHARACTERISTIC_CHAT_CPM,
		2,
		1), 0);
	assert_true(test_log_matches(PRT_FATAL,
		"character handle 0 out of range\n"));
	assert_false(test_log_contains("cannot bound characteristic"));

	char preserved[32] = "unchanged";
	test_reset_log();
	Characteristic_StringHandle(0,
		CHARACTERISTIC_NAME,
		preserved,
		sizeof(preserved));
	assert_string_equal(preserved, "unchanged");
	assert_true(test_log_matches(PRT_FATAL,
		"character handle 0 out of range\n"));
}

/*
=============
test_q3_skill_character_handle_exports

Confirms Q3 skill-block profiles load exactly and interpolate through the handle cache.
=============
*/
static void test_q3_skill_character_handle_exports(void **state)
{
	test_environment_t *env = (test_environment_t *)(*state);
	assert_non_null(env);

	char skill_character_path[PATH_MAX];
	asset_path_or_skip("tests/support/assets/bots/q3_skill_c.c",
		skill_character_path,
		sizeof(skill_character_path));

	assert_true(AI_CharacterFileUsesSkillBlocks(skill_character_path));

	int near_anchor_handle = BotLoadCharacterHandle(skill_character_path, 4.005f);
	assert_true(near_anchor_handle > 0);
	assert_true(fabsf(Characteristic_FloatHandle(near_anchor_handle, CHARACTERISTIC_ATTACK_SKILL) - 0.7015f) < 0.0001f);
	assert_true(fabsf(Characteristic_FloatHandle(near_anchor_handle, CHARACTERISTIC_AIM_SKILL) - 0.801f) < 0.0001f);
	assert_int_equal(Characteristic_IntegerHandle(near_anchor_handle, CHARACTERISTIC_CHAT_CPM), 250);
	BotShutdownCharacterHandles();
	assert_null(BotCharacterFromHandle(near_anchor_handle));

	int exact_handle = BotLoadCharacterSkillHandle(skill_character_path, 4.0f);
	assert_true(exact_handle > 0);

	ai_character_profile_t *exact = BotCharacterFromHandle(exact_handle);
	assert_non_null(exact);
	assert_null(AI_ItemWeightsForCharacter(exact));
	assert_null(AI_WeaponWeightsForCharacter(exact));
	assert_string_equal(AI_CharacteristicAsString(exact, CHARACTERISTIC_NAME), "Q3 Skill Four");
	assert_true(fabsf(Characteristic_FloatHandle(exact_handle, CHARACTERISTIC_ATTACK_SKILL) - 0.7f) < 0.0001f);
	assert_int_equal(Characteristic_IntegerHandle(exact_handle, CHARACTERISTIC_CHAT_CPM), 250);
	assert_int_equal(AI_CharacteristicCount(exact), 80);

	char hidden_sentinel[32] = "unchanged";
	test_reset_log();
	Characteristic_StringHandle(exact_handle, 80, hidden_sentinel, sizeof(hidden_sentinel));
	assert_string_equal(hidden_sentinel, "unchanged");
	assert_true(test_log_contains("characteristic 80 does not exist"));

	test_reset_log();
	int cached_exact = BotLoadCharacterSkillHandle(skill_character_path, 4.0f);
	assert_int_equal(cached_exact, exact_handle);
	char expected_cache_log[PATH_MAX + 64];
	int written = snprintf(expected_cache_log,
		sizeof(expected_cache_log),
		"loaded cached skill 4.000000 from %s\n",
		skill_character_path);
	assert_true(written > 0 && written < (int)sizeof(expected_cache_log));
	assert_true(test_log_matches(PRT_MESSAGE, expected_cache_log));

	test_reset_log();
	int within_epsilon = BotLoadCharacterSkillHandle(skill_character_path, 4.009f);
	assert_int_equal(within_epsilon, exact_handle);
	written = snprintf(expected_cache_log,
		sizeof(expected_cache_log),
		"loaded cached skill 4.009000 from %s\n",
		skill_character_path);
	assert_true(written > 0 && written < (int)sizeof(expected_cache_log));
	assert_true(test_log_matches(PRT_MESSAGE, expected_cache_log));

	test_reset_log();
	int at_epsilon = BotLoadCharacterSkillHandle(skill_character_path, 4.01f);
	assert_true(at_epsilon > 0);
	assert_int_not_equal(at_epsilon, exact_handle);
	assert_false(test_log_contains("loaded cached skill"));

	LibVarSet("bot_reloadcharacters", "1");
	int reload_exact = BotLoadCharacterSkillHandle(skill_character_path, 4.0f);
	assert_true(reload_exact > 0);
	assert_int_not_equal(reload_exact, exact_handle);
	BotFreeCharacterHandle(reload_exact);
	assert_null(BotCharacterFromHandle(reload_exact));
	LibVarSet("bot_reloadcharacters", "0");

	int clamped_low = BotLoadCharacterHandle(skill_character_path, 0.25f);
	assert_true(clamped_low > 0);
	ai_character_profile_t *low = BotCharacterFromHandle(clamped_low);
	assert_non_null(low);
	assert_string_equal(AI_CharacteristicAsString(low, CHARACTERISTIC_NAME), "Q3 Skill One");
	assert_true(fabsf(Characteristic_FloatHandle(clamped_low, CHARACTERISTIC_ATTACK_SKILL) - 0.1f) < 0.0001f);
	assert_int_equal(Characteristic_IntegerHandle(clamped_low, CHARACTERISTIC_CHAT_CPM), 100);

	int clamped_high = BotLoadCharacterHandle(skill_character_path, 6.0f);
	assert_true(clamped_high > 0);
	ai_character_profile_t *high = BotCharacterFromHandle(clamped_high);
	assert_non_null(high);
	assert_string_equal(AI_CharacteristicAsString(high, CHARACTERISTIC_NAME), "Q3 Skill Five");
	assert_true(fabsf(Characteristic_FloatHandle(clamped_high, CHARACTERISTIC_ATTACK_SKILL) - 1.0f) < 0.0001f);
	assert_int_equal(Characteristic_IntegerHandle(clamped_high, CHARACTERISTIC_CHAT_CPM), 400);

	int interpolated_handle = BotLoadCharacterHandle(skill_character_path, 2.5f);
	assert_true(interpolated_handle > 0);

	ai_character_profile_t *interpolated = BotCharacterFromHandle(interpolated_handle);
	assert_non_null(interpolated);
	assert_true(fabsf(Characteristic_FloatHandle(interpolated_handle, CHARACTERISTIC_ATTACK_SKILL) - 0.4f) < 0.0001f);
	assert_true(fabsf(Characteristic_FloatHandle(interpolated_handle, CHARACTERISTIC_AIM_SKILL) - 0.5f) < 0.0001f);
	assert_int_equal(Characteristic_IntegerHandle(interpolated_handle, CHARACTERISTIC_CHAT_CPM), 100);
	assert_string_equal(AI_CharacteristicAsString(interpolated, CHARACTERISTIC_CHAT_NAME), "skillone");

	test_reset_log();
	LibVarSet("bot_reloadcharacters", "1");
	int reload_interpolated = BotLoadCharacterHandle(skill_character_path, 2.5f);
	assert_int_equal(reload_interpolated, interpolated_handle);
	written = snprintf(expected_cache_log,
		sizeof(expected_cache_log),
		"loaded cached skill 2.500000 from %s\n",
		skill_character_path);
	assert_true(written > 0 && written < (int)sizeof(expected_cache_log));
	assert_true(test_log_matches(PRT_MESSAGE, expected_cache_log));
	BotFreeCharacterHandle(reload_interpolated);
	assert_null(BotCharacterFromHandle(interpolated_handle));
	LibVarSet("bot_reloadcharacters", "0");

	BotFreeCharacterHandle(clamped_high);
	BotFreeCharacterHandle(clamped_low);
	BotFreeCharacterHandle(exact_handle);
}

/*
=============
test_q3_character_string_cleanup_handle

Confirms Q3 string cleanup invalidates strings without freeing the profile or numerics.
=============
*/
static void test_q3_character_string_cleanup_handle(void **state)
{
	test_environment_t *env = (test_environment_t *)(*state);
	assert_non_null(env);

	char skill_character_path[PATH_MAX];
	asset_path_or_skip("tests/support/assets/bots/q3_skill_c.c",
		skill_character_path,
		sizeof(skill_character_path));

	int handle = BotLoadCharacterSkillHandle(skill_character_path, 4.0f);
	assert_true(handle > 0);
	ai_character_profile_t *profile = BotCharacterFromHandle(handle);
	assert_non_null(profile);
	assert_string_equal(AI_CharacteristicAsString(profile, CHARACTERISTIC_NAME),
		"Q3 Skill Four");
	assert_true(fabsf(Characteristic_FloatHandle(handle,
		CHARACTERISTIC_ATTACK_SKILL) - 0.7f) < 0.0001f);

	BotFreeCharacterStringsHandle(profile);
	assert_ptr_equal(BotCharacterFromHandle(handle), profile);
	assert_int_equal(AI_CharacteristicType(profile, CHARACTERISTIC_NAME),
		AI_CHARACTER_VALUE_NONE);
	assert_true(fabsf(Characteristic_FloatHandle(handle,
		CHARACTERISTIC_ATTACK_SKILL) - 0.7f) < 0.0001f);

	char name[64] = "unchanged";
	test_reset_log();
	Characteristic_StringHandle(handle,
		CHARACTERISTIC_NAME,
		name,
		sizeof(name));
	assert_string_equal(name, "unchanged");
	assert_true(test_log_contains("characteristic 0 is not initialized"));

	LibVarSet("bot_reloadcharacters", "1");
	BotFreeCharacterHandle(handle);
	assert_null(BotCharacterFromHandle(handle));
	LibVarSet("bot_reloadcharacters", "0");
}

/*
=============
test_q3_default_reload_preloads_default

Confirms Q3 preloads a cached default before reloading the requested default file.
=============
*/
static void test_q3_default_reload_preloads_default(void **state)
{
	test_environment_t *env = (test_environment_t *)(*state);
	assert_non_null(env);

	char q3_botfiles_root[PATH_MAX];
	q3_botfiles_root_or_skip(q3_botfiles_root, sizeof(q3_botfiles_root));
	LibVarSet("basedir", q3_botfiles_root);
	LibVarSet("gamedir", "");
	LibVarSet("cddir", "");
	LibVarSet("gladiator_asset_dir", "");

	BotShutdownCharacterHandles();
	LibVarSet("bot_reloadcharacters", "1");
	test_reset_log();
	int requested_handle =
		BotLoadCharacterSkillHandle("bots/default_c.c", 4.0f);
	assert_int_equal(requested_handle, 2);

	ai_character_profile_t *preloaded_default = BotCharacterFromHandle(1);
	ai_character_profile_t *requested_default =
		BotCharacterFromHandle(requested_handle);
	assert_non_null(preloaded_default);
	assert_non_null(requested_default);
	assert_ptr_not_equal(preloaded_default, requested_default);
	assert_ptr_not_equal(preloaded_default->definition_blob,
		requested_default->definition_blob);
	assert_string_equal(AI_CharacterProfileFilename(preloaded_default),
		"bots/default_c.c");
	assert_string_equal(AI_CharacterProfileFilename(requested_default),
		"bots/default_c.c");
	assert_int_equal(test_log_occurrences(
		"loaded skill 4 from bots/default_c.c\n"), 2);

	BotShutdownCharacterHandles();
	LibVarSet("bot_reloadcharacters", "0");
}

/*
=============
test_q3_empty_filename_uses_default

Confirms an explicit empty Q3 filename still walks the retail default fallback ladder.
=============
*/
static void test_q3_empty_filename_uses_default(void **state)
{
	test_environment_t *env = (test_environment_t *)(*state);
	assert_non_null(env);

	char q3_botfiles_root[PATH_MAX];
	q3_botfiles_root_or_skip(q3_botfiles_root, sizeof(q3_botfiles_root));
	LibVarSet("basedir", q3_botfiles_root);
	LibVarSet("gamedir", "");
	LibVarSet("cddir", "");
	LibVarSet("gladiator_asset_dir", "");
	LibVarSet("bot_reloadcharacters", "0");

	BotShutdownCharacterHandles();
	test_reset_log();
	int handle = BotLoadCharacterSkillHandle("", 4.0f);
	assert_int_equal(handle, 1);
	ai_character_profile_t *profile = BotCharacterFromHandle(handle);
	assert_non_null(profile);
	assert_string_equal(AI_CharacterProfileFilename(profile),
		"bots/default_c.c");
	assert_true(test_log_matches(PRT_MESSAGE,
		"loaded cached default skill 4 from \n"));

	BotShutdownCharacterHandles();
}

/*
=============
test_q3_noncache_success_logs

Pins the four branch-specific Q3 success diagnostics when cache reuse is disabled.
=============
*/
static void test_q3_noncache_success_logs(void **state)
{
	test_environment_t *env = (test_environment_t *)(*state);
	assert_non_null(env);

	char q3_botfiles_root[PATH_MAX];
	q3_botfiles_root_or_skip(q3_botfiles_root, sizeof(q3_botfiles_root));
	LibVarSet("basedir", q3_botfiles_root);
	LibVarSet("gamedir", "");
	LibVarSet("cddir", "");
	LibVarSet("gladiator_asset_dir", "");
	LibVarSet("bot_reloadcharacters", "1");

	test_reset_log();
	assert_true(BotLoadCharacterSkillHandle("bots/xian_c.c", 4.0f) > 0);
	assert_true(test_log_matches(PRT_MESSAGE,
		"loaded skill 4 from bots/xian_c.c\n"));
	BotShutdownCharacterHandles();

	test_reset_log();
	assert_true(BotLoadCharacterSkillHandle("bots/missing_exact.c", 4.0f) > 0);
	assert_true(test_log_matches(PRT_MESSAGE,
		"loaded default skill 4 from bots/missing_exact.c\n"));
	BotShutdownCharacterHandles();

	test_reset_log();
	assert_true(BotLoadCharacterSkillHandle("bots/cadaver_c.c", 3.0f) > 0);
	assert_true(test_log_matches(PRT_MESSAGE,
		"loaded skill 1.000000 from bots/cadaver_c.c\n"));
	BotShutdownCharacterHandles();

	test_reset_log();
	assert_true(BotLoadCharacterSkillHandle("bots/missing_any.c", 3.0f) > 0);
	assert_true(test_log_matches(PRT_MESSAGE,
		"loaded default skill 1.000000 from bots/missing_any.c\n"));
	BotShutdownCharacterHandles();

	LibVarSet("bot_reloadcharacters", "0");
}

/*
=============
test_q3_skill_fallback_cache_handles

Confirms Q3 fallback skills are cached by the file and skill that actually loaded.
=============
*/
static void test_q3_skill_fallback_cache_handles(void **state)
{
	test_environment_t *env = (test_environment_t *)(*state);
	assert_non_null(env);

	char sparse_character_path[PATH_MAX];
	asset_path_or_skip("tests/support/assets/bots/q3_sparse_c.c",
		sparse_character_path,
		sizeof(sparse_character_path));

	assert_true(AI_CharacterFileUsesSkillBlocks(sparse_character_path));

	int first_fallback = BotLoadCharacterSkillHandle(sparse_character_path, 3.0f);
	assert_true(first_fallback > 0);

	ai_character_profile_t *first = BotCharacterFromHandle(first_fallback);
	assert_non_null(first);
	assert_string_equal(AI_CharacteristicAsString(first, CHARACTERISTIC_NAME), "Q3 Sparse Skill One");
	assert_true(fabsf(Characteristic_FloatHandle(first_fallback, CHARACTERISTIC_ATTACK_SKILL) - 0.15f) < 0.0001f);
	assert_int_equal(Characteristic_IntegerHandle(first_fallback, CHARACTERISTIC_CHAT_CPM), 90);

	test_reset_log();
	int second_fallback = BotLoadCharacterSkillHandle(sparse_character_path, 4.0f);
	assert_int_equal(second_fallback, first_fallback);
	char expected_cache_log[PATH_MAX + 64];
	int written = snprintf(expected_cache_log,
		sizeof(expected_cache_log),
		"loaded cached skill 1.000000 from %s\n",
		sparse_character_path);
	assert_true(written > 0 && written < (int)sizeof(expected_cache_log));
	assert_true(test_log_matches(PRT_MESSAGE, expected_cache_log));

	BotFreeCharacterHandle(second_fallback);
	BotFreeCharacterHandle(first_fallback);
}

/*
=============
test_q3_asset_character_handles

Confirms native Q3 skill files load through includes, defaults, and interpolation.
=============
*/
static void test_q3_asset_character_handles(void **state)
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
	int missing_default_handle = BotLoadCharacterSkillHandle("bots/not_a_real_character.c", 4.0f);
	assert_true(missing_default_handle > 0);
	assert_true(test_log_matches(PRT_MESSAGE,
		"loaded cached default skill 4 from bots/not_a_real_character.c\n"));

	test_reset_log();
	int missing_default_any =
		BotLoadCharacterSkillHandle("bots/not_a_real_character_any.c", 3.0f);
	assert_int_equal(missing_default_any, missing_default_handle);
	assert_true(test_log_matches(PRT_MESSAGE,
		"loaded cached default skill 4.000000 from bots/not_a_real_character_any.c\n"));

	ai_character_profile_t *missing_default = BotCharacterFromHandle(missing_default_handle);
	assert_non_null(missing_default);
	assert_string_equal(AI_CharacterProfileFilename(missing_default), "bots/default_c.c");
	assert_string_equal(AI_CharacteristicAsString(missing_default, Q3_CHARACTERISTIC_NAME), "Player");
	assert_string_equal(AI_CharacteristicAsString(missing_default, Q3_CHARACTERISTIC_ITEMWEIGHTS), "bots/daemia_i.c");
	assert_true(fabsf(Characteristic_FloatHandle(missing_default_handle, Q3_CHARACTERISTIC_ATTACK_SKILL) - 1.0f) < 0.0001f);

	int direct_unclamped_handle = BotLoadCharacterSkillHandle("bots/xian_c.c", 6.0f);
	assert_true(direct_unclamped_handle > 0);

	ai_character_profile_t *direct_unclamped = BotCharacterFromHandle(direct_unclamped_handle);
	assert_non_null(direct_unclamped);
	assert_string_equal(AI_CharacterProfileFilename(direct_unclamped), "bots/xian_c.c");
	assert_string_equal(AI_CharacteristicAsString(direct_unclamped, Q3_CHARACTERISTIC_NAME), "Xian");
	assert_true(fabsf(AI_CharacterProfileSkill(direct_unclamped) - 1.0f) < 0.0001f);
	assert_true(fabsf(Characteristic_FloatHandle(direct_unclamped_handle, Q3_CHARACTERISTIC_ATTACK_SKILL) - 1.0f) < 0.0001f);

	LibVarSet("bot_reloadcharacters", "1");
	BotFreeCharacterHandle(direct_unclamped_handle);
	assert_null(BotCharacterFromHandle(direct_unclamped_handle));
	LibVarSet("bot_reloadcharacters", "0");

	int exact_handle = BotLoadCharacterSkillHandle("bots/xian_c.c", 4.0f);
	assert_true(exact_handle > 0);

	ai_character_profile_t *exact = BotCharacterFromHandle(exact_handle);
	assert_non_null(exact);
	assert_null(AI_ItemWeightsForCharacter(exact));
	assert_null(AI_WeaponWeightsForCharacter(exact));
	assert_string_equal(AI_CharacteristicAsString(exact, Q3_CHARACTERISTIC_NAME), "Xian");
	assert_string_equal(AI_CharacteristicAsString(exact, Q3_CHARACTERISTIC_GENDER), "male");
	assert_string_equal(AI_CharacteristicAsString(exact, Q3_CHARACTERISTIC_ITEMWEIGHTS), "bots/xian_i.c");
	assert_true(fabsf(Characteristic_FloatHandle(exact_handle, Q3_CHARACTERISTIC_ATTACK_SKILL) - 0.75f) < 0.0001f);
	assert_int_equal(Characteristic_IntegerHandle(exact_handle, Q3_CHARACTERISTIC_CHAT_CPM), 400);
	assert_true(fabsf(Characteristic_FloatHandle(exact_handle, Q3_CHARACTERISTIC_WALKER)) < 0.0001f);

	int cadaver_any_handle = BotLoadCharacterSkillHandle("bots/cadaver_c.c", 3.0f);
	assert_true(cadaver_any_handle > 0);

	ai_character_profile_t *cadaver_any = BotCharacterFromHandle(cadaver_any_handle);
	assert_non_null(cadaver_any);
	assert_string_equal(AI_CharacterProfileFilename(cadaver_any), "bots/cadaver_c.c");
	assert_string_equal(AI_CharacteristicAsString(cadaver_any, Q3_CHARACTERISTIC_NAME), "Cadaver");
	assert_true(fabsf(Characteristic_FloatHandle(cadaver_any_handle, Q3_CHARACTERISTIC_ATTACK_SKILL) - 0.5f) < 0.0001f);

	test_reset_log();
	int cached_cadaver_any = BotLoadCharacterSkillHandle("bots/cadaver_c.c", 3.0f);
	assert_int_equal(cached_cadaver_any, cadaver_any_handle);
	assert_true(test_log_matches(PRT_MESSAGE,
		"loaded cached skill 1.000000 from bots/cadaver_c.c\n"));
	assert_false(test_log_contains("loaded skill 1 from bots/cadaver_c.c"));

	int interpolated_handle = BotLoadCharacterHandle("bots/xian_c.c", 2.5f);
	assert_true(interpolated_handle > 0);

	ai_character_profile_t *interpolated = BotCharacterFromHandle(interpolated_handle);
	assert_non_null(interpolated);
	assert_string_equal(AI_CharacteristicAsString(interpolated, Q3_CHARACTERISTIC_CHAT_NAME), "xian");
	assert_true(fabsf(Characteristic_FloatHandle(interpolated_handle, Q3_CHARACTERISTIC_ATTACK_SKILL) - 0.5f) < 0.0001f);
	assert_true(fabsf(Characteristic_FloatHandle(interpolated_handle, Q3_CHARACTERISTIC_AIM_SKILL) - 0.5f) < 0.0001f);
	assert_int_equal(Characteristic_IntegerHandle(interpolated_handle, Q3_CHARACTERISTIC_CHAT_CPM), 400);

	int fractional_missing_default = BotLoadCharacterHandle("bots/fractional_missing_character.c", 2.5f);
	assert_true(fractional_missing_default > 0);

	ai_character_profile_t *fractional_default = BotCharacterFromHandle(fractional_missing_default);
	assert_non_null(fractional_default);
	assert_string_equal(AI_CharacterProfileFilename(fractional_default), "bots/default_c.c");
	assert_string_equal(AI_CharacteristicAsString(fractional_default, Q3_CHARACTERISTIC_NAME), "Player");
	assert_true(fabsf(Characteristic_FloatHandle(fractional_missing_default, Q3_CHARACTERISTIC_ATTACK_SKILL) - 0.625f) < 0.0001f);
	assert_true(fabsf(Characteristic_FloatHandle(fractional_missing_default, Q3_CHARACTERISTIC_AIM_SKILL) - 0.625f) < 0.0001f);
	assert_int_equal(Characteristic_IntegerHandle(fractional_missing_default, Q3_CHARACTERISTIC_CHAT_CPM), 400);

	test_reset_log();
	int public_missing_default = BotLoadCharacterHandle("bots/another_missing_character.c", 4.0f);
	assert_int_equal(public_missing_default, missing_default_handle);
	assert_true(test_log_matches(PRT_MESSAGE,
		"loaded cached default skill 4 from bots/another_missing_character.c\n"));

	BotFreeCharacterHandle(public_missing_default);
	BotFreeCharacterHandle(missing_default_any);
	BotFreeCharacterHandle(cached_cadaver_any);
	BotFreeCharacterHandle(cadaver_any_handle);
	BotFreeCharacterHandle(missing_default_handle);
	BotFreeCharacterHandle(fractional_missing_default);
	BotFreeCharacterHandle(interpolated_handle);
	BotFreeCharacterHandle(exact_handle);
}

/*
=============
test_retail_setup_rejects_empty_or_missing_item_weights

Pins the retail setup failure when characteristic 28 supplies no filename.
=============
*/
static void test_retail_setup_rejects_empty_or_missing_item_weights(void **state)
{
	test_environment_t *env = (test_environment_t *)(*state);
	assert_non_null(env);
	assert_non_null(env->exports);

	char fixture_path[PATH_MAX];
	int written = snprintf(fixture_path,
		sizeof(fixture_path),
		"%s/tests/support/assets/bots/retail_missing_itemweights_tmp_c.c",
		PROJECT_SOURCE_DIR);
	assert_true(written > 0 && written < (int)sizeof(fixture_path));
	(void)remove(fixture_path);
	write_character_fixture(fixture_path,
		"character \"missingitem\"\n"
		"{\n"
		"0 \"Missing Item Weights\"\n"
		"3 \"male\"\n"
		"5 \"bots/babe_w.c\"\n"
		"12 \"bots/babe_t.c\"\n"
		"13 \"missingitem\"\n"
		"52 0\n"
		"}\n"
		"character \"emptyitem\"\n"
		"{\n"
		"0 \"Empty Item Weights\"\n"
		"3 \"male\"\n"
		"5 \"bots/babe_w.c\"\n"
		"12 \"bots/babe_t.c\"\n"
		"13 \"emptyitem\"\n"
		"28 \"\"\n"
		"52 0\n"
		"}\n");

	int library_status = setup_retail_test_library(env);
	int missing_status = -1;
	int empty_status = -1;
	int retry_status = -1;
	int retry_shutdown_status = -1;
	int library_shutdown_status = -1;
	int restart_library_status = -1;
	int restart_character_handle = 0;
	int restart_library_shutdown_status = -1;
	int missing_goal_handle = 0;
	int empty_goal_handle = 0;
	bool missing_partial_retained = false;
	bool empty_partial_retained = false;
	bool retry_state_active = false;
	bool retry_state_cleaned = false;
	bool partial_handles_cleaned = false;
	bool missing_slot_diagnostic = false;
	bool missing_path_diagnostic = false;
	bool missing_load_diagnostic = false;
	bool missing_used_no_fallback = false;
	bool empty_path_diagnostic = false;
	bool empty_load_diagnostic = false;
	bool empty_used_no_fallback = false;
	bool restart_api_reacquired = false;
	bool restart_character_queried = false;
	bool restart_character_freed = false;

	if (library_status == BLERR_NOERROR)
	{
		bot_settings_t settings;
		memset(&settings, 0, sizeof(settings));
		snprintf(settings.characterfile,
			sizeof(settings.characterfile),
			"%s",
			fixture_path);
		snprintf(settings.charactername,
			sizeof(settings.charactername),
			"missingitem");

		test_reset_log();
		missing_status = env->exports->BotSetupClient(0, &settings);
		bot_client_state_t *partial_state = BotState_Get(0);
		if (partial_state != NULL)
		{
			missing_goal_handle = partial_state->goal_handle;
			missing_partial_retained = !partial_state->active &&
				partial_state->character != NULL &&
				partial_state->goal_handle > 0 &&
				partial_state->item_weights == NULL &&
				partial_state->weapon_state == 0 &&
				partial_state->chat_state == NULL;
		}
		missing_slot_diagnostic = test_log_matches(PRT_ERROR,
			"characteristic 28 is not initialized\n");
		missing_path_diagnostic = test_log_matches(PRT_ERROR,
			"couldn't find \n");
		missing_load_diagnostic = test_log_matches(PRT_FATAL,
			"couldn't load weights\n");
		missing_used_no_fallback = !test_log_contains("bots/babe_i.c") &&
			!test_log_contains("items.c");

		snprintf(settings.charactername,
			sizeof(settings.charactername),
			"emptyitem");
		test_reset_log();
		empty_status = env->exports->BotSetupClient(0, &settings);
		partial_state = BotState_Get(0);
		if (partial_state != NULL)
		{
			empty_goal_handle = partial_state->goal_handle;
			empty_partial_retained = !partial_state->active &&
				partial_state->character != NULL &&
				partial_state->goal_handle > 0 &&
				partial_state->item_weights == NULL &&
				partial_state->weapon_state == 0 &&
				partial_state->chat_state == NULL;
		}
		empty_path_diagnostic = test_log_matches(PRT_ERROR,
			"couldn't find \n");
		empty_load_diagnostic = test_log_matches(PRT_FATAL,
			"couldn't load weights\n");
		empty_used_no_fallback = !test_log_contains("bots/babe_i.c") &&
			!test_log_contains("items.c");

		snprintf(settings.characterfile,
			sizeof(settings.characterfile),
			"bots/babe_c.c");
		snprintf(settings.charactername,
			sizeof(settings.charactername),
			"babe");
		test_reset_log();
		retry_status = env->exports->BotSetupClient(0, &settings);
		bot_client_state_t *retry_state = BotState_Get(0);
		retry_state_active = retry_state != NULL && retry_state->active;
		if (retry_state != NULL)
		{
			retry_shutdown_status = env->exports->BotShutdownClient(0);
		}
		retry_state_cleaned = retail_client_slot_released(0);
	}

	if (env->library_setup)
	{
		library_shutdown_status = env->exports->BotShutdownLibrary();
		env->library_setup = false;
		partial_handles_cleaned =
			BotGoalStatePeek(missing_goal_handle) == NULL &&
			BotGoalStatePeek(empty_goal_handle) == NULL;
	}
	if (library_shutdown_status == BLERR_NOERROR)
	{
		env->exports = GetBotAPIEx(&g_test_bot_import,
			sizeof(g_test_bot_import));
		restart_api_reacquired = env->exports != NULL;
		if (restart_api_reacquired)
		{
			restart_library_status = setup_retail_test_library(env);
			if (restart_library_status == BLERR_NOERROR)
			{
				restart_character_handle = env->exports->BotLoadCharacter(
					"bots/babe_c.c",
					1.0f);
				if (restart_character_handle > 0)
				{
					char restart_name[64];
					restart_name[0] = '\0';
					env->exports->Characteristic_String(restart_character_handle,
						CHARACTERISTIC_NAME,
						restart_name,
						sizeof(restart_name));
					restart_character_queried =
						strcmp(restart_name, "Silicon Babe") == 0;
					env->exports->BotFreeCharacter(restart_character_handle);
					restart_character_freed =
						BotCharacterFromHandle(restart_character_handle) == NULL;
				}
				restart_library_shutdown_status =
					env->exports->BotShutdownLibrary();
				env->library_setup = false;
			}
		}
	}
	int remove_status = remove(fixture_path);

	assert_int_equal(library_status, BLERR_NOERROR);
	assert_false(missing_status);
	assert_true(missing_partial_retained);
	assert_true(missing_goal_handle > 0);
	assert_true(missing_slot_diagnostic);
	assert_true(missing_path_diagnostic);
	assert_true(missing_load_diagnostic);
	assert_true(missing_used_no_fallback);
	assert_false(empty_status);
	assert_true(empty_partial_retained);
	assert_true(empty_goal_handle > 0);
	assert_int_equal(empty_goal_handle, missing_goal_handle);
	assert_true(empty_path_diagnostic);
	assert_true(empty_load_diagnostic);
	assert_true(empty_used_no_fallback);
	assert_true(retry_status);
	assert_true(retry_state_active);
	assert_int_equal(retry_shutdown_status, BLERR_NOERROR);
	assert_true(retry_state_cleaned);
	assert_int_equal(library_shutdown_status, BLERR_NOERROR);
	assert_true(partial_handles_cleaned);
	assert_true(restart_api_reacquired);
	assert_int_equal(restart_library_status, BLERR_NOERROR);
	assert_true(restart_character_handle > 0);
	assert_true(restart_character_queried);
	assert_true(restart_character_freed);
	assert_int_equal(restart_library_shutdown_status, BLERR_NOERROR);
	assert_int_equal(remove_status, 0);
}

/*
=============
test_retail_setup_rejects_empty_or_missing_weapon_weights

Pins the raw empty-path fatal diagnostic and prevents the successor fallback.
=============
*/
static void test_retail_setup_rejects_empty_or_missing_weapon_weights(void **state)
{
	test_environment_t *env = (test_environment_t *)(*state);
	assert_non_null(env);
	assert_non_null(env->exports);

	char fixture_path[PATH_MAX];
	int written = snprintf(fixture_path,
		sizeof(fixture_path),
		"%s/tests/support/assets/bots/retail_missing_weaponweights_tmp_c.c",
		PROJECT_SOURCE_DIR);
	assert_true(written > 0 && written < (int)sizeof(fixture_path));
	(void)remove(fixture_path);
	write_character_fixture(fixture_path,
		"character \"missingweapon\"\n"
		"{\n"
		"0 \"Missing Weapon Weights\"\n"
		"3 \"male\"\n"
		"12 \"bots/babe_t.c\"\n"
		"13 \"missingweapon\"\n"
		"28 \"bots/babe_i.c\"\n"
		"52 0\n"
		"}\n"
		"character \"emptyweapon\"\n"
		"{\n"
		"0 \"Empty Weapon Weights\"\n"
		"3 \"male\"\n"
		"5 \"\"\n"
		"12 \"bots/babe_t.c\"\n"
		"13 \"emptyweapon\"\n"
		"28 \"bots/babe_i.c\"\n"
		"52 0\n"
		"}\n");

	int library_status = setup_retail_test_library(env);
	int missing_status = -1;
	int empty_status = -1;
	int retry_status = -1;
	int retry_shutdown_status = -1;
	int library_shutdown_status = -1;
	int missing_goal_handle = 0;
	int empty_goal_handle = 0;
	int missing_weapon_state = 0;
	int empty_weapon_state = 0;
	int repeated_failure_count = 0;
	bool missing_partial_retained = false;
	bool empty_partial_retained = false;
	bool repeated_failures_reused_handles = true;
	bool retry_state_active = false;
	bool retry_state_cleaned = false;
	bool partial_handles_cleaned = false;
	bool missing_slot_diagnostic = false;
	bool missing_find_diagnostic = false;
	bool missing_raw_path_diagnostic = false;
	bool missing_diagnostic_order = false;
	bool missing_used_no_fallback = false;
	bool empty_find_diagnostic = false;
	bool empty_raw_path_diagnostic = false;
	bool empty_diagnostic_order = false;
	bool empty_used_no_fallback = false;

	if (library_status == BLERR_NOERROR)
	{
		bot_settings_t settings;
		memset(&settings, 0, sizeof(settings));
		snprintf(settings.characterfile,
			sizeof(settings.characterfile),
			"%s",
			fixture_path);
		snprintf(settings.charactername,
			sizeof(settings.charactername),
			"missingweapon");

		test_reset_log();
		missing_status = env->exports->BotSetupClient(0, &settings);
		bot_client_state_t *partial_state = BotState_Get(0);
		if (partial_state != NULL)
		{
			missing_goal_handle = partial_state->goal_handle;
			missing_weapon_state = partial_state->weapon_state;
			missing_partial_retained = !partial_state->active &&
				partial_state->character != NULL &&
				partial_state->goal_handle > 0 &&
				partial_state->item_weights != NULL &&
				partial_state->weapon_state > 0 &&
				partial_state->weapon_weights == NULL &&
				partial_state->chat_state == NULL;
		}
		missing_slot_diagnostic = test_log_matches(PRT_ERROR,
			"characteristic 5 is not initialized\n");
		missing_find_diagnostic = test_log_matches(PRT_ERROR,
			"couldn't find \n");
		missing_raw_path_diagnostic = test_log_matches(PRT_FATAL,
			"couldn't load weapon config \n");
		missing_diagnostic_order = test_log_precedes(PRT_ERROR,
			"couldn't find \n",
			PRT_FATAL,
			"couldn't load weapon config \n");
		missing_used_no_fallback =
			!test_log_contains("default/defaul_w.c");

		snprintf(settings.charactername,
			sizeof(settings.charactername),
			"emptyweapon");
		test_reset_log();
		empty_status = env->exports->BotSetupClient(0, &settings);
		partial_state = BotState_Get(0);
		if (partial_state != NULL)
		{
			empty_goal_handle = partial_state->goal_handle;
			empty_weapon_state = partial_state->weapon_state;
			empty_partial_retained = !partial_state->active &&
				partial_state->character != NULL &&
				partial_state->goal_handle > 0 &&
				partial_state->item_weights != NULL &&
				partial_state->weapon_state > 0 &&
				partial_state->weapon_weights == NULL &&
				partial_state->chat_state == NULL;
		}
		empty_find_diagnostic = test_log_matches(PRT_ERROR,
			"couldn't find \n");
		empty_raw_path_diagnostic = test_log_matches(PRT_FATAL,
			"couldn't load weapon config \n");
		empty_diagnostic_order = test_log_precedes(PRT_ERROR,
			"couldn't find \n",
			PRT_FATAL,
			"couldn't load weapon config \n");
		empty_used_no_fallback =
			!test_log_contains("default/defaul_w.c");

		for (int attempt = 0; attempt <= MAX_CLIENTS; ++attempt)
		{
			test_reset_log();
			int repeated_status = env->exports->BotSetupClient(0, &settings);
			partial_state = BotState_Get(0);
			if (repeated_status || partial_state == NULL ||
				partial_state->active ||
				partial_state->goal_handle != empty_goal_handle ||
				partial_state->weapon_state != empty_weapon_state ||
				partial_state->item_weights == NULL ||
				partial_state->weapon_weights != NULL)
			{
				repeated_failures_reused_handles = false;
				break;
			}
			++repeated_failure_count;
		}

		snprintf(settings.characterfile,
			sizeof(settings.characterfile),
			"bots/babe_c.c");
		snprintf(settings.charactername,
			sizeof(settings.charactername),
			"babe");
		test_reset_log();
		retry_status = env->exports->BotSetupClient(0, &settings);
		bot_client_state_t *retry_state = BotState_Get(0);
		retry_state_active = retry_state != NULL && retry_state->active;
		if (retry_state != NULL)
		{
			retry_shutdown_status = env->exports->BotShutdownClient(0);
		}
		retry_state_cleaned = retail_client_slot_released(0);
	}

	if (env->library_setup)
	{
		library_shutdown_status = env->exports->BotShutdownLibrary();
		env->library_setup = false;
		partial_handles_cleaned =
			BotGoalStatePeek(missing_goal_handle) == NULL &&
			BotGoalStatePeek(empty_goal_handle) == NULL &&
			BotWeaponStatePeek(missing_weapon_state) == NULL &&
			BotWeaponStatePeek(empty_weapon_state) == NULL;
	}
	int remove_status = remove(fixture_path);

	assert_int_equal(library_status, BLERR_NOERROR);
	assert_false(missing_status);
	assert_true(missing_partial_retained);
	assert_true(missing_goal_handle > 0);
	assert_true(missing_weapon_state > 0);
	assert_true(missing_slot_diagnostic);
	assert_true(missing_find_diagnostic);
	assert_true(missing_raw_path_diagnostic);
	assert_true(missing_diagnostic_order);
	assert_true(missing_used_no_fallback);
	assert_false(empty_status);
	assert_true(empty_partial_retained);
	assert_true(empty_goal_handle > 0);
	assert_true(empty_weapon_state > 0);
	assert_int_equal(empty_goal_handle, missing_goal_handle);
	assert_int_equal(empty_weapon_state, missing_weapon_state);
	assert_true(empty_find_diagnostic);
	assert_true(empty_raw_path_diagnostic);
	assert_true(empty_diagnostic_order);
	assert_true(empty_used_no_fallback);
	assert_true(repeated_failures_reused_handles);
	assert_int_equal(repeated_failure_count, MAX_CLIENTS + 1);
	assert_true(retry_status);
	assert_true(retry_state_active);
	assert_int_equal(retry_shutdown_status, BLERR_NOERROR);
	assert_true(retry_state_cleaned);
	assert_int_equal(library_shutdown_status, BLERR_NOERROR);
	assert_true(partial_handles_cleaned);
	assert_int_equal(remove_status, 0);
}

/*
=============
test_retail_setup_loads_chat_when_nochat_enabled

Pins retail setup ownership: nochat suppresses chatter, not chat-file loading.
=============
*/
static void test_retail_setup_loads_chat_when_nochat_enabled(void **state)
{
	test_environment_t *env = (test_environment_t *)(*state);
	assert_non_null(env);
	assert_non_null(env->exports);

	int library_status = setup_retail_test_library(env);
	int setup_status = false;
	int shutdown_status = -1;
	int library_shutdown_status = -1;
	bool chat_loaded = false;

	if (library_status == BLERR_NOERROR)
	{
		assert_int_equal(env->exports->BotLibVarSet("nochat", "1"),
			BLERR_NOERROR);
		bot_settings_t settings;
		memset(&settings, 0, sizeof(settings));
		snprintf(settings.characterfile,
			sizeof(settings.characterfile),
			"bots/babe_c.c");
		snprintf(settings.charactername,
			sizeof(settings.charactername),
			"babe");

		setup_status = env->exports->BotSetupClient(0, &settings);
		bot_client_state_t *client_state = BotState_Get(0);
		chat_loaded = client_state != NULL && client_state->active &&
			client_state->chat_state != NULL &&
			strcmp(BotChatName(client_state->chat_state), "") == 0 &&
			BotChatClient(client_state->chat_state) == -1;
		if (client_state != NULL && client_state->active)
		{
			shutdown_status = env->exports->BotShutdownClient(0);
		}
	}

	if (env->library_setup)
	{
		library_shutdown_status = env->exports->BotShutdownLibrary();
		env->library_setup = false;
	}

	assert_int_equal(library_status, BLERR_NOERROR);
	assert_true(setup_status);
	assert_true(chat_loaded);
	assert_int_equal(shutdown_status, BLERR_NOERROR);
	assert_int_equal(library_shutdown_status, BLERR_NOERROR);
}

/*
=============
test_retail_move_overwrites_inactive_partial_destination

Pins retail BotMoveClient overwrite and owner cleanup for an in-use inactive slot.
=============
*/
static void test_retail_move_overwrites_inactive_partial_destination(void **state)
{
	test_environment_t *env = (test_environment_t *)(*state);
	assert_non_null(env);
	assert_non_null(env->exports);

	char fixture_path[PATH_MAX];
	int written = snprintf(fixture_path,
		sizeof(fixture_path),
		"%s/tests/support/assets/bots/retail_move_partial_tmp_c.c",
		PROJECT_SOURCE_DIR);
	assert_true(written > 0 && written < (int)sizeof(fixture_path));
	(void)remove(fixture_path);
	write_character_fixture(fixture_path,
		"character \"partialmove\"\n"
		"{\n"
		"0 \"Partial Move\"\n"
		"3 \"male\"\n"
		"5 \"bots/babe_w.c\"\n"
		"12 \"bots/babe_t.c\"\n"
		"13 \"missing_partial_move\"\n"
		"28 \"bots/babe_i.c\"\n"
		"52 0\n"
		"}\n");

	int library_status = setup_retail_test_library(env);
	int partial_status = true;
	int source_status = false;
	int move_status = -1;
	int shutdown_status = -1;
	int library_shutdown_status = -1;
	int partial_goal_handle = 0;
	int partial_weapon_handle = 0;
	int restart_library_status = -1;
	int restart_shutdown_status = -1;
	bot_chatstate_t *partial_chat_state = NULL;
	bool partial_retained = false;
	bool partial_chat_diagnostics = false;
	bool move_count_neutral = false;
	bool move_wiring_matches = false;
	bool partial_owner_abandoned = false;
	bool abandoned_owner_drained = false;
	bool restart_api_reacquired = false;
	bool destination_cleaned = false;

	if (library_status == BLERR_NOERROR)
	{
		bot_settings_t partial_settings;
		memset(&partial_settings, 0, sizeof(partial_settings));
		snprintf(partial_settings.characterfile,
			sizeof(partial_settings.characterfile),
			"%s",
			fixture_path);
		snprintf(partial_settings.charactername,
			sizeof(partial_settings.charactername),
			"partialmove");
		test_reset_log();
		partial_status = env->exports->BotSetupClient(1, &partial_settings);
		bot_client_state_t *partial_state = BotState_Get(1);
		if (partial_state != NULL)
		{
			partial_goal_handle = partial_state->goal_handle;
			partial_weapon_handle = partial_state->weapon_state;
			partial_chat_state = partial_state->chat_state;
			partial_retained = !partial_state->active &&
				partial_state->character != NULL &&
				partial_state->goal_handle > 0 &&
				partial_state->weapon_state > 0 &&
				partial_state->chat_state != NULL;
		}
		/*
		 * The chat-not-found diagnostic reports the resolved container
		 * (retail 0x1002ddc5 passes bot_fileref_t.path), which is an absolute
		 * path here, so match on its ends rather than the whole line.
		 */
		partial_chat_diagnostics = test_log_matches_bracketed(PRT_ERROR,
			"couldn't find chat missing_partial_move in ",
			"bots/babe_t.c\n") &&
			test_log_matches(PRT_FATAL,
				"couldn't load chat missing_partial_move from bots/babe_t.c\n");

		bot_settings_t source_settings;
		memset(&source_settings, 0, sizeof(source_settings));
		snprintf(source_settings.characterfile,
			sizeof(source_settings.characterfile),
			"bots/babe_c.c");
		snprintf(source_settings.charactername,
			sizeof(source_settings.charactername),
			"babe");
		source_status = env->exports->BotSetupClient(0, &source_settings);
		if (source_status)
		{
			env->client_active = true;
			int active_before_move = BotState_ActiveClientCount();
			move_status = env->exports->BotMoveClient(0, 1);
			if (move_status == BLERR_NOERROR)
			{
				env->client_active = false;
				bot_client_state_t *moved_state = BotState_Get(1);
				move_count_neutral = active_before_move == 1 &&
					BotState_ActiveClientCount() == active_before_move;
				move_wiring_matches = retail_client_slot_released(0) &&
					moved_state != NULL && moved_state->active &&
					moved_state->client_number == 0 &&
					moved_state->entity_number == 1;
				partial_owner_abandoned =
					BotGoalStatePeek(partial_goal_handle) != NULL &&
					BotWeaponStatePeek(partial_weapon_handle) != NULL;
				shutdown_status = env->exports->BotShutdownClient(1);
				destination_cleaned = retail_client_slot_released(1) &&
					BotState_ActiveClientCount() == 0;
			}
		}
	}

	if (env->client_active)
	{
		(void)env->exports->BotShutdownClient(0);
		env->client_active = false;
	}
	if (env->library_setup)
	{
		library_shutdown_status = env->exports->BotShutdownLibrary();
		env->library_setup = false;
		abandoned_owner_drained =
			BotGoalStatePeek(partial_goal_handle) == NULL &&
			BotWeaponStatePeek(partial_weapon_handle) == NULL;
	}
	if (library_shutdown_status == BLERR_NOERROR)
	{
		env->exports = GetBotAPIEx(&g_test_bot_import,
			sizeof(g_test_bot_import));
		restart_api_reacquired = env->exports != NULL;
		if (restart_api_reacquired)
		{
			restart_library_status = setup_retail_test_library(env);
			if (restart_library_status == BLERR_NOERROR)
			{
				restart_shutdown_status =
					env->exports->BotShutdownLibrary();
				env->library_setup = false;
			}
		}
	}
	int remove_status = remove(fixture_path);

	assert_int_equal(library_status, BLERR_NOERROR);
	assert_false(partial_status);
	assert_true(partial_retained);
	assert_true(partial_goal_handle > 0);
	assert_true(partial_weapon_handle > 0);
	assert_non_null(partial_chat_state);
	assert_true(partial_chat_diagnostics);
	assert_true(source_status);
	assert_int_equal(move_status, BLERR_NOERROR);
	assert_true(move_count_neutral);
	assert_true(move_wiring_matches);
	assert_true(partial_owner_abandoned);
	assert_true(abandoned_owner_drained);
	assert_int_equal(shutdown_status, BLERR_NOERROR);
	assert_true(destination_cleaned);
	assert_int_equal(library_shutdown_status, BLERR_NOERROR);
	assert_true(restart_api_reacquired);
	assert_int_equal(restart_library_status, BLERR_NOERROR);
	assert_int_equal(restart_shutdown_status, BLERR_NOERROR);
	assert_int_equal(remove_status, 0);
}

/*
=============
test_bot_setup_client_owns_resources

Confirms setup keeps the packed character definition separate from state-owned resources.
=============
*/
static void test_bot_setup_client_owns_resources(void **state)
{
    test_environment_t *env = (test_environment_t *)(*state);
    assert_non_null(env);
    assert_non_null(env->exports);

	test_reset_log();
	assert_int_equal(env->exports->BotLoadCharacter("bots/babe_c.c", 1.0f), 0);
	assert_true(test_log_contains("BotLoadCharacter: bot library used before being setup\n"));

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
	assert_true(status);
	assert_int_equal(BotState_ActiveClientCount(), 1);
	env->client_active = true;

    bot_client_state_t *state_slot = BotState_Get(0);
    assert_non_null(state_slot);
    assert_true(state_slot->active);
	assert_non_null(state_slot->character);
	assert_int_equal(AI_CharacteristicDefinitionCount(state_slot->character),
		CHARACTERISTIC_BUTTKISSER);
	assert_non_null(state_slot->item_weights);
	assert_non_null(state_slot->weapon_weights);
	assert_non_null(state_slot->chat_state);

	const bot_goalstate_t *goal_owner = BotGoalStatePeek(state_slot->goal_handle);
	const bot_weaponstate_t *weapon_owner =
		BotWeaponStatePeek(state_slot->weapon_state);
	assert_non_null(goal_owner);
	assert_non_null(weapon_owner);
	assert_true(weapon_owner->fresh_weights);
	assert_ptr_equal(goal_owner->itemweightconfig, state_slot->item_weights);
	assert_ptr_equal(weapon_owner->weights, state_slot->weapon_weights);

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

	assert_true(BotChat_HasSynonymPhrase(state_slot->chat_state,
                                         "CONTEXT_NEARBYITEM",
                                         "Shotgun"));
	assert_string_equal(BotChatName(state_slot->chat_state), "");
	assert_int_equal(BotChatClient(state_slot->chat_state), -1);

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
	assert_string_equal(g_test_client_commands.entries[0].text, "gender");
	assert_string_equal(g_test_client_commands.entries[0].argument, "female");
	assert_true(g_test_client_commands.entries[0].terminated);

	test_reset_client_commands();
	BotState_EmitPendingClientCommands(state_slot);
	memset(&command_input, 0, sizeof(command_input));
	status = EA_GetInput(0, 0.05f, &command_input);
	assert_int_equal(status, BLERR_NOERROR);
	assert_int_equal(g_test_client_commands.count, 0);

	assert_int_equal(env->exports->BotLibVarSet("fastchat", "1"),
		BLERR_NOERROR);
	test_reset_client_commands();
	status = env->exports->BotShutdownClient(0);
	assert_int_equal(status, BLERR_NOERROR);
	assert_int_equal(BotState_ActiveClientCount(), 0);
	assert_int_equal(g_test_client_commands.count, 1);
	assert_int_equal(g_test_client_commands.entries[0].client, 0);
	assert_string_equal(g_test_client_commands.entries[0].text, "say");
	assert_true(g_test_client_commands.entries[0].argument[0] != '\0');
	assert_true(g_test_client_commands.entries[0].terminated);
	env->client_active = false;

	env->exports->BotLibVarSet("altnames", "1");
	status = env->exports->BotSetupClient(0, &settings);
	assert_true(status);
	assert_int_equal(BotState_ActiveClientCount(), 1);
	env->client_active = true;

	state_slot = BotState_Get(0);
	assert_non_null(state_slot);
	assert_true(state_slot->active);
	assert_string_equal(state_slot->client_settings.netname, "Live Babe");
	assert_non_null(state_slot->character);
	assert_non_null(state_slot->chat_state);
	assert_string_equal(BotChatName(state_slot->chat_state), "");
	assert_int_equal(BotChatClient(state_slot->chat_state), -1);

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
	assert_string_equal(g_test_client_commands.entries[0].text, "gender");
	assert_string_equal(g_test_client_commands.entries[0].argument, "female");
	assert_true(g_test_client_commands.entries[0].terminated);
	assert_int_equal(g_test_client_commands.entries[1].client, 0);
	assert_string_equal(g_test_client_commands.entries[1].text, "name");
	assert_string_equal(g_test_client_commands.entries[1].argument, "Epsilon");
	assert_true(g_test_client_commands.entries[1].terminated);

	memset(&live_client_settings, 0, sizeof(live_client_settings));
	snprintf(live_client_settings.netname, sizeof(live_client_settings.netname), "Moved Babe");
	snprintf(live_client_settings.skin, sizeof(live_client_settings.skin), "female/phoenix");
	status = env->exports->BotClientSettings(1, &live_client_settings);
	assert_int_equal(status, BLERR_NOERROR);

	status = env->exports->BotMoveClient(0, 1);
	assert_int_equal(status, BLERR_NOERROR);
	assert_int_equal(BotState_ActiveClientCount(), 1);
	env->client_active = false;
	assert_true(retail_client_slot_released(0));
	state_slot = BotState_Get(1);
	assert_non_null(state_slot);
	assert_int_equal(state_slot->client_number, 0);
	assert_int_equal(state_slot->entity_number, 1);
	assert_string_equal(state_slot->client_settings.netname, "Epsilon");
	assert_string_equal(state_slot->client_settings.skin, "female/venus");
	assert_string_equal(BotState_ClientName(1), "Moved Babe");
	assert_string_equal(BotState_ClientSkin(1), "female/phoenix");
	assert_int_equal(BotState_FindClientByName("Moved Babe"), 1);
	assert_non_null(state_slot->character);
	assert_non_null(state_slot->chat_state);
	assert_string_equal(BotChatName(state_slot->chat_state), "");
	assert_int_equal(BotChatClient(state_slot->chat_state), -1);
	ai_dm_metrics_t moved_metrics;
	AI_DMState_GetMetrics(state_slot->dm_state, &moved_metrics);
	assert_int_equal(moved_metrics.client_number, 0);

	test_reset_client_commands();
	status = env->exports->BotShutdownClient(1);
	assert_int_equal(status, BLERR_NOERROR);
	assert_int_equal(BotState_ActiveClientCount(), 0);
	assert_int_equal(g_test_client_commands.count, 1);
	assert_int_equal(g_test_client_commands.entries[0].client, 0);
	assert_string_equal(g_test_client_commands.entries[0].text, "say");
	assert_true(g_test_client_commands.entries[0].argument[0] != '\0');
	assert_true(g_test_client_commands.entries[0].terminated);

	status = env->exports->BotShutdownLibrary();
	assert_int_equal(status, BLERR_NOERROR);
	env->library_setup = false;
	assert_int_equal(BotState_ClientCapacity(), 0);
	assert_int_equal(BotState_ActiveClientCount(), 0);

	env->exports = GetBotAPIEx(&g_test_bot_import, sizeof(g_test_bot_import));
	assert_non_null(env->exports);
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
test_retail_lifecycle_chat_uses_easy_client_name

Pins EasyClientName variables and the absence of a persona-name setup fallback.
=============
*/
static void test_retail_lifecycle_chat_uses_easy_client_name(void **state)
{
	test_environment_t *env = (test_environment_t *)(*state);
	assert_non_null(env);
	assert_non_null(env->exports);

	char character_path[PATH_MAX];
	char chat_path[PATH_MAX];
	int written = snprintf(character_path,
		sizeof(character_path),
		"%s/tests/support/assets/bots/retail_lifecycle_name_tmp_c.c",
		PROJECT_SOURCE_DIR);
	assert_true(written > 0 && written < (int)sizeof(character_path));
	written = snprintf(chat_path,
		sizeof(chat_path),
		"%s/tests/support/assets/bots/retail_lifecycle_name_tmp_t.c",
		PROJECT_SOURCE_DIR);
	assert_true(written > 0 && written < (int)sizeof(chat_path));
	(void)remove(character_path);
	(void)remove(chat_path);

	write_character_fixture(chat_path,
		"chat \"lifecycle\"\n"
		"{\n"
		"type \"exit_game\"\n"
		"{\n"
		"\"name<\", 0, \">\";\n"
		"}\n"
		"}\n");

	char character_source[PATH_MAX + 512];
	written = snprintf(character_source,
		sizeof(character_source),
		"character \"lifecycle\"\n"
		"{\n"
		"0 \"PersonaFallback\"\n"
		"3 \"female\"\n"
		"5 \"bots/babe_w.c\"\n"
		"12 \"%s\"\n"
		"13 \"lifecycle\"\n"
		"18 1.0\n"
		"28 \"bots/babe_i.c\"\n"
		"52 0\n"
		"}\n",
		chat_path);
	assert_true(written > 0 && written < (int)sizeof(character_source));
	write_character_fixture(character_path, character_source);

	int library_status = setup_retail_test_library(env);
	bool setup_name_clear = false;
	bool easy_name_matches = false;
	bool empty_name_matches = false;
	if (library_status == BLERR_NOERROR)
	{
		bot_settings_t settings;
		memset(&settings, 0, sizeof(settings));
		written = snprintf(settings.characterfile,
			sizeof(settings.characterfile),
			"%s",
			character_path);
		assert_true(written > 0 && written < (int)sizeof(settings.characterfile));
		snprintf(settings.charactername,
			sizeof(settings.charactername),
			"lifecycle");

		bot_clientsettings_t client_settings;
		memset(&client_settings, 0, sizeof(client_settings));
		snprintf(client_settings.netname,
			sizeof(client_settings.netname),
			"[X] Mr Foo!");
		assert_int_equal(env->exports->BotClientSettings(0, &client_settings),
			BLERR_NOERROR);
		assert_true(env->exports->BotSetupClient(0, &settings));
		env->client_active = true;
		bot_client_state_t *client = BotState_Get(0);
		setup_name_clear = client != NULL && client->chat_state != NULL &&
			strcmp(BotChatName(client->chat_state), "") == 0 &&
			BotChatClient(client->chat_state) == -1;
		assert_int_equal(env->exports->BotLibVarSet("fastchat", "1"),
			BLERR_NOERROR);
		test_reset_client_commands();
		assert_int_equal(env->exports->BotShutdownClient(0), BLERR_NOERROR);
		env->client_active = false;
		easy_name_matches = g_test_client_commands.count == 1 &&
			strcmp(g_test_client_commands.entries[0].text, "say") == 0 &&
			strcmp(g_test_client_commands.entries[0].argument,
				"name<foo>") == 0 &&
			g_test_client_commands.entries[0].terminated;

		memset(&client_settings, 0, sizeof(client_settings));
		assert_int_equal(env->exports->BotClientSettings(0, &client_settings),
			BLERR_NOERROR);
		assert_true(env->exports->BotSetupClient(0, &settings));
		env->client_active = true;
		test_reset_client_commands();
		assert_int_equal(env->exports->BotShutdownClient(0), BLERR_NOERROR);
		env->client_active = false;
		empty_name_matches = g_test_client_commands.count == 1 &&
			strcmp(g_test_client_commands.entries[0].text, "say") == 0 &&
			strcmp(g_test_client_commands.entries[0].argument, "name<>") == 0 &&
			g_test_client_commands.entries[0].terminated;
	}

	int character_remove_status = remove(character_path);
	int chat_remove_status = remove(chat_path);
	assert_int_equal(library_status, BLERR_NOERROR);
	assert_true(setup_name_clear);
	assert_true(easy_name_matches);
	assert_true(empty_name_matches);
	assert_int_equal(character_remove_status, 0);
	assert_int_equal(chat_remove_status, 0);
}

/*
=============
test_same_character_clients_own_isolated_resources

Pins fresh per-client retail character, weight, and chat ownership across teardown.
=============
*/
static void test_same_character_clients_own_isolated_resources(void **state)
{
	test_environment_t *env = (test_environment_t *)(*state);
	assert_non_null(env);
	assert_non_null(env->exports);

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
	snprintf(settings.characterfile,
		sizeof(settings.characterfile),
		"bots/babe_c.c");
	snprintf(settings.charactername,
		sizeof(settings.charactername),
		"babe");

	assert_true(env->exports->BotSetupClient(0, &settings));
	env->client_active = true;
	assert_true(env->exports->BotSetupClient(1, &settings));
	assert_int_equal(BotState_ActiveClientCount(), 2);

	bot_client_state_t *first = BotState_Get(0);
	bot_client_state_t *second = BotState_Get(1);
	assert_non_null(first);
	assert_non_null(second);
	assert_ptr_not_equal(first->character, second->character);
	const char *first_character_name = Characteristic_String(first->character,
		CHARACTERISTIC_NAME);
	const char *second_character_name = Characteristic_String(second->character,
		CHARACTERISTIC_NAME);
	assert_string_equal(first_character_name, "Silicon Babe");
	assert_string_equal(second_character_name, "Silicon Babe");
	assert_ptr_not_equal(first_character_name, second_character_name);
	assert_ptr_not_equal(first->item_weights, second->item_weights);
	assert_ptr_not_equal(first->weapon_weights, second->weapon_weights);
	assert_ptr_not_equal(first->weapon_weights->config,
		second->weapon_weights->config);
	assert_ptr_not_equal(first->chat_state, second->chat_state);

	const bot_goalstate_t *first_goal = BotGoalStatePeek(first->goal_handle);
	const bot_goalstate_t *second_goal = BotGoalStatePeek(second->goal_handle);
	const bot_weaponstate_t *first_weapon =
		BotWeaponStatePeek(first->weapon_state);
	const bot_weaponstate_t *second_weapon =
		BotWeaponStatePeek(second->weapon_state);
	assert_non_null(first_goal);
	assert_non_null(second_goal);
	assert_non_null(first_weapon);
	assert_non_null(second_weapon);
	assert_true(first_weapon->fresh_weights);
	assert_true(second_weapon->fresh_weights);
	assert_ptr_equal(first_goal->itemweightconfig, first->item_weights);
	assert_ptr_equal(second_goal->itemweightconfig, second->item_weights);
	assert_ptr_equal(first_weapon->weights, first->weapon_weights);
	assert_ptr_equal(second_weapon->weights, second->weapon_weights);

	assert_string_equal(BotChatName(first->chat_state), "");
	assert_string_equal(BotChatName(second->chat_state), "");
	assert_ptr_not_equal(BotChatName(first->chat_state),
		BotChatName(second->chat_state));
	assert_int_equal(BotChatClient(first->chat_state), -1);
	assert_int_equal(BotChatClient(second->chat_state), -1);
	BotQueueConsoleMessage(first->chat_state, CMS_CHAT, "first client only");
	assert_int_equal(BotNumConsoleMessages(first->chat_state), 1);
	assert_int_equal(BotNumConsoleMessages(second->chat_state), 0);

	bot_character_t *second_character = second->character;
	bot_weight_config_t *second_items = second->item_weights;
	ai_weapon_weights_t *second_weapons = second->weapon_weights;
	bot_chatstate_t *second_chat = second->chat_state;
	env->exports->BotLibVarSet("bot_reloadcharacters", "1");
	status = env->exports->BotShutdownClient(0);
	assert_int_equal(status, BLERR_NOERROR);
	env->client_active = false;
	assert_true(retail_client_slot_released(0));
	assert_int_equal(BotState_ActiveClientCount(), 1);
	second = BotState_Get(1);
	assert_non_null(second);
	assert_ptr_equal(second->character, second_character);
	assert_ptr_equal(second->item_weights, second_items);
	assert_ptr_equal(second->weapon_weights, second_weapons);
	assert_ptr_equal(second->chat_state, second_chat);
	assert_string_equal(Characteristic_String(second->character,
		CHARACTERISTIC_NAME), "Silicon Babe");
	assert_true(BotWeight_FindIndex(second->item_weights,
		"weapon_rocketlauncher") >= 0);
	assert_true(BotWeight_FindIndex(second->weapon_weights->config,
		"Rocket Launcher") >= 0);
	assert_string_equal(BotChatName(second->chat_state), "");
	assert_int_equal(BotChatClient(second->chat_state), -1);
	assert_int_equal(BotNumConsoleMessages(second->chat_state), 0);

	status = env->exports->BotShutdownClient(1);
	assert_int_equal(status, BLERR_NOERROR);
	assert_true(retail_client_slot_released(1));
	assert_int_equal(BotState_ActiveClientCount(), 0);
	env->exports->BotLibVarSet("bot_reloadcharacters", "0");
}

/*
=============
test_bot_state_map_reset_preserves_character_wiring

Pins the HLIL map-load reset helper that preserves client-owned resources.
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
	assert_true(status);
	env->client_active = true;

	bot_client_state_t *state_slot = BotState_Get(0);
	assert_non_null(state_slot);
	assert_true(state_slot->active);

	bot_character_t *character = state_slot->character;
	bot_weight_config_t *item_weights = state_slot->item_weights;
	ai_weapon_weights_t *weapon_weights = state_slot->weapon_weights;
	bot_chatstate_t *chat_state = state_slot->chat_state;
	ai_goal_state_t *goal_state = state_slot->goal_state;
	ai_dm_state_t *dm_state = state_slot->dm_state;
	bot_movestate_t *move_handle_state = BotMoveStateFromHandle(state_slot->move_handle);
	assert_non_null(character);
	assert_non_null(item_weights);
	assert_non_null(weapon_weights);
	assert_non_null(chat_state);
	assert_non_null(goal_state);
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
	move_handle_state->client = 99;
	move_handle_state->areanum = 12;
	move_handle_state->lastreachnum = 37;

	BotState_ResetForNewMap(state_slot);

	assert_true(state_slot->active);
	assert_int_equal(BotState_ActiveClientCount(), 1);
	assert_ptr_equal(state_slot->character, character);
	assert_ptr_equal(state_slot->item_weights, item_weights);
	assert_ptr_equal(state_slot->weapon_weights, weapon_weights);
	assert_ptr_equal(state_slot->chat_state, chat_state);
	assert_ptr_equal(state_slot->goal_state, goal_state);
	assert_ptr_equal(state_slot->dm_state, dm_state);
	assert_ptr_equal(BotMoveStateFromHandle(state_slot->move_handle), move_handle_state);
	assert_string_equal(state_slot->settings.characterfile, "bots/babe_c.c");
	assert_string_equal(state_slot->client_settings.netname, "Reset Babe");
	assert_string_equal(BotChatName(chat_state), "");
	assert_int_equal(BotChatClient(chat_state), -1);
	assert_non_null(AI_GoalState_GetAvoidList(state_slot->goal_state));

	assert_false(state_slot->client_update_valid);
	assert_float_equal(state_slot->last_update_time, 0.0f, 0.0001f);
	assert_int_equal(state_slot->goal_snapshot_count, 0);
	assert_int_equal(state_slot->active_goal_number, 0);
	assert_int_equal(state_slot->current_weapon, 0);
	assert_false(state_slot->client_commands_pending);
	assert_false(state_slot->has_move_result);
	assert_int_equal(state_slot->last_move_result.type, 0);
	assert_int_equal(state_slot->combat.current_enemy, 0);
	assert_false(state_slot->combat.enemy_visible);
	assert_false(state_slot->goal_state->active_goal.valid);
	assert_int_equal(move_handle_state->client, 0);
	assert_int_equal(move_handle_state->areanum, 0);
	assert_int_equal(move_handle_state->lastreachnum, 0);
	memset(&top_goal, 0, sizeof(top_goal));
	assert_false(BotGetTopGoal(state_slot->goal_handle, &top_goal));

	ai_dm_metrics_t dm_metrics;
	AI_DMState_GetMetrics(state_slot->dm_state, &dm_metrics);
	assert_int_equal(dm_metrics.enemy_entity, 0);

	status = env->exports->BotShutdownClient(0);
	assert_int_equal(status, BLERR_NOERROR);
	env->client_active = false;
	assert_int_equal(BotState_ActiveClientCount(), 0);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup_teardown(
			test_retail_packed_character_allocation_layout,
			character_profile_setup,
			character_profile_teardown),
		cmocka_unit_test_setup_teardown(
			test_retail_characteristic_integer_ftol_edges,
			character_profile_setup,
			character_profile_teardown),
		cmocka_unit_test_setup_teardown(
			test_retail_character_success_log_uses_logical_request,
			character_profile_setup,
			character_profile_teardown),
		cmocka_unit_test_setup_teardown(
			test_retail_character_success_log_reports_pak_provenance,
			character_profile_setup,
			character_profile_teardown),
		cmocka_unit_test_setup_teardown(
			test_retail_character_filename_copy_and_writable_empty,
			character_profile_setup,
			character_profile_teardown),
		cmocka_unit_test_setup_teardown(test_retail_named_character_profile,
			character_profile_setup,
			character_profile_teardown),
		cmocka_unit_test_setup_teardown(test_retail_character_parser_edges,
			character_profile_setup,
			character_profile_teardown),
		cmocka_unit_test_setup_teardown(test_retail_character_handle_exports,
			character_profile_setup,
			character_profile_teardown),
		cmocka_unit_test_setup_teardown(test_q3_handle_failure_diagnostics,
			character_profile_setup,
			character_profile_teardown),
		cmocka_unit_test_setup_teardown(test_q3_skill_character_handle_exports,
			character_profile_setup,
			character_profile_teardown),
		cmocka_unit_test_setup_teardown(test_q3_character_string_cleanup_handle,
			character_profile_setup,
			character_profile_teardown),
		cmocka_unit_test_setup_teardown(test_q3_default_reload_preloads_default,
			character_profile_setup,
			character_profile_teardown),
		cmocka_unit_test_setup_teardown(test_q3_empty_filename_uses_default,
			character_profile_setup,
			character_profile_teardown),
		cmocka_unit_test_setup_teardown(test_q3_noncache_success_logs,
			character_profile_setup,
			character_profile_teardown),
		cmocka_unit_test_setup_teardown(test_q3_skill_fallback_cache_handles,
			character_profile_setup,
			character_profile_teardown),
		cmocka_unit_test_setup_teardown(test_q3_asset_character_handles,
			character_profile_setup,
			character_profile_teardown),
		cmocka_unit_test_setup_teardown(
			test_retail_setup_rejects_empty_or_missing_item_weights,
			bot_setup_client_setup,
			bot_setup_client_teardown),
		cmocka_unit_test_setup_teardown(
			test_retail_setup_rejects_empty_or_missing_weapon_weights,
			bot_setup_client_setup,
			bot_setup_client_teardown),
		cmocka_unit_test_setup_teardown(
			test_retail_setup_loads_chat_when_nochat_enabled,
			bot_setup_client_setup,
			bot_setup_client_teardown),
		cmocka_unit_test_setup_teardown(
			test_retail_move_overwrites_inactive_partial_destination,
			bot_setup_client_setup,
			bot_setup_client_teardown),
		cmocka_unit_test_setup_teardown(test_bot_setup_client_owns_resources,
			bot_setup_client_setup,
			bot_setup_client_teardown),
		cmocka_unit_test_setup_teardown(
			test_retail_lifecycle_chat_uses_easy_client_name,
			bot_setup_client_setup,
			bot_setup_client_teardown),
		cmocka_unit_test_setup_teardown(test_same_character_clients_own_isolated_resources,
			bot_setup_client_setup,
			bot_setup_client_teardown),
		cmocka_unit_test_setup_teardown(test_bot_state_map_reset_preserves_character_wiring,
			bot_setup_client_setup,
			bot_setup_client_teardown),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
