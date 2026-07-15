#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>

#include <cmocka.h>

#include "q2bridge/bridge.h"
#include "botlib/interface/bot_interface.h"
#include "botlib/interface/botlib_interface.h"
#include "botlib/interface/bot_state.h"
#include "botlib/ai_chat/ai_chat.h"
#include "botlib/ea/ea_local.h"
#include "botlib/ai_move/bot_move.h"
#include "botlib/ai_move/mover_catalogue.h"
#include "botlib/ai/goal_move_orchestrator.h"
#include "botlib/ai_goal/bot_goal.h"
#include "botlib/common/l_log.h"
#include "botlib/common/l_memory.h"
#include "botlib/common/l_libvar.h"
#include "botlib/common/l_struct.h"
#include "botlib/common/l_utils.h"
#include "botlib/aas/aas_sound.h"
#include "botlib/aas/aas_map.h"
#include "botlib/aas/aas_local.h"
#include "botlib/aas/aas_debug.h"
#include "botlib_contract_loader.h"
#include "../support/asset_env.h"
#include "q2bridge/bridge_config.h"
#include "q2bridge/update_translator.h"

#ifndef cmocka_skip
#define cmocka_skip(...) skip()
#endif

#define ARRAY_LEN(x) (sizeof(x) / sizeof((x)[0]))

#ifndef AREACONTENTS_MOVER
#define AREACONTENTS_MOVER 1024
#endif

enum retail_battle_inventory_slot_e
{
	RETAIL_INVENTORY_CELLS = 20,
	RETAIL_INVENTORY_HEALTH = 41,
	RETAIL_ENEMY_HORIZONTAL_DIST = 200,
	RETAIL_ENEMY_HEIGHT = 201,
	RETAIL_USING_QUAD = 204,
	RETAIL_USING_INVULNERABILITY = 205,
	RETAIL_USING_SILENCER = 206,
	RETAIL_USING_REBREATHER = 207,
	RETAIL_USING_ENVIRONMENTSUIT = 208,
	RETAIL_USING_ANCIENTHEAD = 209,
	RETAIL_USING_POWERSCREEN = 210,
	RETAIL_USING_POWERSHIELD = 211,
	RETAIL_ENEMY_BLASTER = 230,
	RETAIL_ENEMY_GRAPPLE = 241,
	RETAIL_ENEMY_QUAD = 245,
	RETAIL_ENEMY_INVULNERABILITY = 246,
	RETAIL_ENEMY_POWERSCREEN = 247,
	RETAIL_ENEMY_POWERSHIELD = 248,
};

typedef struct captured_print_s
{
    int type;
    char message[1024];
} captured_print_t;

typedef struct mock_bot_import_s
{
	bot_import_t table;
	captured_print_t prints[128];
	size_t print_count;
	bot_input_t inputs[64];
	int input_clients[64];
	size_t bot_input_count;
	struct
	{
		int client;
		char command[512];
		char argument[512];
	} client_commands[64];
	size_t client_command_count;
	struct
	{
		char name[64];
		void (*function)(void);
	} commands[16];
	size_t command_count;
	char *command_args[16];
	int command_argc;
} mock_bot_import_t;

typedef struct import_field_descriptor_s
{
	const char *name;
	size_t offset;
} import_field_descriptor_t;

static const char *g_retail_import_symbols[] = {
	"BotInput",
	"BotClientCommand",
	"Print",
	"Trace",
	"PointContents",
	"GetMemory",
	"FreeMemory",
	"DebugLineCreate",
	"DebugLineDelete",
	"DebugLineShow",
};

static const import_field_descriptor_t g_import_field_layout[] = {
	{ "BotInput", offsetof(bot_import_t, BotInput) },
	{ "BotClientCommand", offsetof(bot_import_t, BotClientCommand) },
	{ "Print", offsetof(bot_import_t, Print) },
	{ "Trace", offsetof(bot_import_t, Trace) },
	{ "PointContents", offsetof(bot_import_t, PointContents) },
	{ "GetMemory", offsetof(bot_import_t, GetMemory) },
	{ "FreeMemory", offsetof(bot_import_t, FreeMemory) },
	{ "DebugLineCreate", offsetof(bot_import_t, DebugLineCreate) },
	{ "DebugLineDelete", offsetof(bot_import_t, DebugLineDelete) },
	{ "DebugLineShow", offsetof(bot_import_t, DebugLineShow) },
	{ "CvarGet", offsetof(bot_import_t, CvarGet) },
	{ "Error", offsetof(bot_import_t, Error) },
	{ "AddCommand", offsetof(bot_import_t, AddCommand) },
	{ "RemoveCommand", offsetof(bot_import_t, RemoveCommand) },
	{ "CmdArgc", offsetof(bot_import_t, CmdArgc) },
	{ "CmdArgv", offsetof(bot_import_t, CmdArgv) },
};

static const import_field_descriptor_t g_retail_export_layout[] = {
	{ "BotVersion", offsetof(bot_export_t, BotVersion) },
	{ "BotSetupLibrary", offsetof(bot_export_t, BotSetupLibrary) },
	{ "BotShutdownLibrary", offsetof(bot_export_t, BotShutdownLibrary) },
	{ "BotLibraryInitialized", offsetof(bot_export_t, BotLibraryInitialized) },
	{ "BotLibVarSet", offsetof(bot_export_t, BotLibVarSet) },
	{ "BotDefine", offsetof(bot_export_t, BotDefine) },
	{ "BotLoadMap", offsetof(bot_export_t, BotLoadMap) },
	{ "BotSetupClient", offsetof(bot_export_t, BotSetupClient) },
	{ "BotShutdownClient", offsetof(bot_export_t, BotShutdownClient) },
	{ "BotMoveClient", offsetof(bot_export_t, BotMoveClient) },
	{ "BotClientSettings", offsetof(bot_export_t, BotClientSettings) },
	{ "BotSettings", offsetof(bot_export_t, BotSettings) },
	{ "BotStartFrame", offsetof(bot_export_t, BotStartFrame) },
	{ "BotUpdateClient", offsetof(bot_export_t, BotUpdateClient) },
	{ "BotUpdateEntity", offsetof(bot_export_t, BotUpdateEntity) },
	{ "BotAddSound", offsetof(bot_export_t, BotAddSound) },
	{ "BotAddPointLight", offsetof(bot_export_t, BotAddPointLight) },
	{ "BotAI", offsetof(bot_export_t, BotAI) },
	{ "BotConsoleMessage", offsetof(bot_export_t, BotConsoleMessage) },
	{ "Test", offsetof(bot_export_t, Test) },
};

typedef struct bot_interface_test_context_s
{
    asset_env_t assets;
    mock_bot_import_t mock;
    bot_export_t *api;
    botlib_contract_catalogue_t catalogue;
    bool libvar_initialised;
} bot_interface_test_context_t;

typedef struct allocator_callback_capture_s
{
	size_t allocation_count;
	size_t free_count;
	int last_request_size;
	void *last_allocated_header;
	void *last_freed_header;
} allocator_callback_capture_t;

static mock_bot_import_t *g_active_mock = NULL;
static int g_mock_import_libvar_set_status = BLERR_NOERROR;
static int g_mock_import_libvar_set_count = 0;
static bool ensure_map_fixture(const asset_env_t *assets, const char *stem);
static allocator_callback_capture_t g_primary_allocator_capture;
static allocator_callback_capture_t g_alternate_allocator_capture;

/*
=============
test_import_table_matches_retail_symbol_list

Ensures the current bridge import table layout mirrors the retail DLL
symbol order and flags deviations for missing or additional slots.
=============
*/
static void test_import_table_matches_retail_symbol_list(void **state)
{
	(void)state;

	size_t expected_count = ARRAY_LEN(g_retail_import_symbols);
	size_t actual_count = ARRAY_LEN(g_import_field_layout);
	size_t expected_index = 0;
	size_t deviation_count = 0;

	if (actual_count < expected_count)
	{
		fail_msg("Import table is missing %zu retail slots", expected_count - actual_count);
	}

	for (size_t i = 0; i < actual_count && expected_index < expected_count; ++i)
	{
		const import_field_descriptor_t *field = &g_import_field_layout[i];
		if (strcmp(field->name, g_retail_import_symbols[expected_index]) != 0)
		{
			continue;
		}

		size_t expected_offset = expected_index * sizeof(void (*)(void));
		if (field->offset != expected_offset)
		{
			print_message("import parity: '%s' offset %zu diverges from retail slot %zu\n",
			              field->name,
			              field->offset,
			              expected_offset);
			deviation_count += 1U;
		}

		expected_index += 1U;
	}

	if (expected_index != expected_count)
	{
		fail_msg("Import table missing expected retail symbol '%s' at slot %zu",
		         g_retail_import_symbols[expected_index],
		         expected_index);
	}

	if (actual_count > expected_count)
	{
		print_message("import parity: %zu additional slots beyond retail reference detected\n",
		             actual_count - expected_count);
	}

	if (deviation_count == 0)
	{
		print_message("import parity: retail symbol order preserved across %zu entries\n",
		             expected_count);
	}

	assert_int_equal(deviation_count, 0U);
}

/*
=============
test_bot_input_preserves_retail_prefix_layout

Pins the original actionflags slot ahead of the successor weapon extension.
=============
*/
static void test_bot_input_preserves_retail_prefix_layout(void **state)
{
	(void)state;

	assert_int_equal(offsetof(bot_input_t, thinktime), 0U);
	assert_int_equal(offsetof(bot_input_t, dir), sizeof(float));
	assert_int_equal(offsetof(bot_input_t, speed), sizeof(float) * 4U);
	assert_int_equal(offsetof(bot_input_t, viewangles), sizeof(float) * 5U);
	assert_int_equal(offsetof(bot_input_t, actionflags), sizeof(float) * 8U);
	assert_int_equal(offsetof(bot_input_t, weapon),
		offsetof(bot_input_t, actionflags) + sizeof(int));
}

/*
=============
test_export_table_preserves_retail_prefix_layout

Pins the original twenty exports to the leading contiguous ABI slots.
=============
*/
static void test_export_table_preserves_retail_prefix_layout(void **state)
{
	(void)state;

	for (size_t index = 0; index < ARRAY_LEN(g_retail_export_layout); ++index)
	{
		const import_field_descriptor_t *field =
			&g_retail_export_layout[index];
		size_t expected_offset = index * sizeof(void (*)(void));
		if (field->offset != expected_offset)
		{
			fail_msg("export '%s' offset %zu diverges from retail slot %zu",
				field->name,
				field->offset,
				expected_offset);
		}
	}
}



static void Mock_Print(int type, char *fmt, ...)
{
if (g_active_mock == NULL || fmt == NULL)
    {
        return;
    }

    va_list args;
    va_start(args, fmt);

    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), fmt, args);

    va_end(args);

    if (g_active_mock->print_count < ARRAY_LEN(g_active_mock->prints))
    {
        captured_print_t *slot = &g_active_mock->prints[g_active_mock->print_count++];
        slot->type = type;
        strncpy(slot->message, buffer, sizeof(slot->message) - 1);
        slot->message[sizeof(slot->message) - 1] = '\0';
    }
    else if (!g_active_mock->print_count)
    {
        return;
    }
    else
    {
        memmove(&g_active_mock->prints[0],
                &g_active_mock->prints[1],
                (ARRAY_LEN(g_active_mock->prints) - 1) * sizeof(g_active_mock->prints[0]));

        captured_print_t *slot = &g_active_mock->prints[ARRAY_LEN(g_active_mock->prints) - 1];
        slot->type = type;
        strncpy(slot->message, buffer, sizeof(slot->message) - 1);
        slot->message[sizeof(slot->message) - 1] = '\0';
    }
}

static void Mock_Error(const char *fmt, ...)
{
    if (fmt == NULL)
    {
        return;
    }

    va_list args;
    va_start(args, fmt);

    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), fmt, args);

    va_end(args);

    Mock_Print(PRT_ERROR, "%s", buffer);
}

static cvar_t *Mock_CvarGet(const char *name, const char *default_value, int flags)
{
    (void)name;
    (void)default_value;
    (void)flags;
    return NULL;
}

static void Mock_BotInput(int client, bot_input_t *input)
{
    if (g_active_mock == NULL || input == NULL)
    {
        return;
    }

    size_t index = g_active_mock->bot_input_count;
    if (index >= ARRAY_LEN(g_active_mock->inputs))
    {
        return;
    }

    g_active_mock->inputs[index] = *input;
    g_active_mock->input_clients[index] = client;
    g_active_mock->bot_input_count += 1U;
}

/*
=============
Mock_ImportBotLibVarSet

Records bridge-only setter calls so the retail export can prove isolation.
=============
*/
static int Mock_ImportBotLibVarSet(const char *var_name, const char *value)
{
    (void)var_name;
    (void)value;
	g_mock_import_libvar_set_count += 1;
    return g_mock_import_libvar_set_status;
}

static void Mock_BotClientCommand(int client, char *fmt, ...)
{
	if (g_active_mock == NULL || fmt == NULL ||
		g_active_mock->client_command_count >= ARRAY_LEN(g_active_mock->client_commands))
	{
		return;
	}

	va_list args;
	va_start(args, fmt);
	size_t index = g_active_mock->client_command_count++;
	g_active_mock->client_commands[index].client = client;
	if (strcmp(fmt, "say_team") == 0)
	{
		const char *argument = va_arg(args, const char *);
		snprintf(g_active_mock->client_commands[index].command,
			sizeof(g_active_mock->client_commands[index].command),
			"%s",
			fmt);
		snprintf(g_active_mock->client_commands[index].argument,
			sizeof(g_active_mock->client_commands[index].argument),
			"%s",
			argument != NULL ? argument : "");
	}
	else
	{
		vsnprintf(g_active_mock->client_commands[index].command,
			sizeof(g_active_mock->client_commands[index].command),
			fmt,
			args);
	}
	va_end(args);
}

static bsp_trace_t Mock_Trace(vec3_t start, vec3_t mins, vec3_t maxs, vec3_t end, int passent, int contentmask)
{
    (void)start;
    (void)mins;
    (void)maxs;
    (void)end;
    (void)passent;
    (void)contentmask;

    bsp_trace_t trace;
    memset(&trace, 0, sizeof(trace));
    trace.fraction = 1.0f;
    return trace;
}

static int Mock_PointContents(vec3_t point)
{
    (void)point;
    return 0;
}

/*
=============
Mock_GetMemory

Allocates through and records the import table originally passed to GetBotAPI.
=============
*/
static void *Mock_GetMemory(int size)
{
	g_primary_allocator_capture.allocation_count += 1U;
	g_primary_allocator_capture.last_request_size = size;
	g_primary_allocator_capture.last_allocated_header = malloc((size_t)size);
	return g_primary_allocator_capture.last_allocated_header;
}

/*
=============
Mock_FreeMemory

Releases and records headers through the originally imported callback.
=============
*/
static void Mock_FreeMemory(void *ptr)
{
	g_primary_allocator_capture.free_count += 1U;
	g_primary_allocator_capture.last_freed_header = ptr;
	free(ptr);
}

/*
=============
Alternate_GetMemory

Captures allocations if the allocator incorrectly follows a mutated import table.
=============
*/
static void *Alternate_GetMemory(int size)
{
	g_alternate_allocator_capture.allocation_count += 1U;
	g_alternate_allocator_capture.last_request_size = size;
	g_alternate_allocator_capture.last_allocated_header = malloc((size_t)size);
	return g_alternate_allocator_capture.last_allocated_header;
}

/*
=============
Alternate_FreeMemory

Captures releases if the allocator incorrectly follows a mutated import table.
=============
*/
static void Alternate_FreeMemory(void *ptr)
{
	g_alternate_allocator_capture.free_count += 1U;
	g_alternate_allocator_capture.last_freed_header = ptr;
	free(ptr);
}

static int Mock_DebugLineCreate(void)
{
    return 1;
}

static void Mock_DebugLineDelete(int line)
{
    (void)line;
}

static void Mock_DebugLineShow(int line, vec3_t start, vec3_t end, int color)
{
    (void)line;
    (void)start;
    (void)end;
    (void)color;
}

static void Mock_ClearCommandArgs(mock_bot_import_t *mock)
{
    if (mock == NULL)
    {
        return;
    }

    for (int index = 0; index < mock->command_argc; ++index)
    {
        free(mock->command_args[index]);
        mock->command_args[index] = NULL;
    }

    mock->command_argc = 0;
}

static char *Mock_DuplicateString(const char *text)
{
    if (text == NULL)
    {
        return NULL;
    }

    size_t length = strlen(text);
    char *copy = (char *)malloc(length + 1);
    if (copy == NULL)
    {
        return NULL;
    }

    memcpy(copy, text, length);
    copy[length] = '\0';
    return copy;
}

static void Mock_SetCommandArgs(mock_bot_import_t *mock, const char *command, const char *args_line)
{
    if (mock == NULL)
    {
        return;
    }

    Mock_ClearCommandArgs(mock);

    if (command != NULL)
    {
        mock->command_args[mock->command_argc++] = Mock_DuplicateString(command);
    }

    if (args_line == NULL)
    {
        return;
    }

    const char *cursor = args_line;
    while (*cursor != '\0' && mock->command_argc < (int)ARRAY_LEN(mock->command_args))
    {
        while (*cursor != '\0' && isspace((unsigned char)*cursor))
        {
            ++cursor;
        }

        if (*cursor == '\0')
        {
            break;
        }

        const char *start = cursor;
        while (*cursor != '\0' && !isspace((unsigned char)*cursor))
        {
            ++cursor;
        }

        size_t length = (size_t)(cursor - start);
        char *token = (char *)malloc(length + 1);
        if (token == NULL)
        {
            break;
        }

        memcpy(token, start, length);
        token[length] = '\0';
        mock->command_args[mock->command_argc++] = token;
    }
}

static void Mock_AddCommand(const char *name, void (*function)(void))
{
    if (g_active_mock == NULL || name == NULL || function == NULL)
    {
        return;
    }

    if (g_active_mock->command_count >= ARRAY_LEN(g_active_mock->commands))
    {
        return;
    }

    size_t index = g_active_mock->command_count++;
    strncpy(g_active_mock->commands[index].name, name, sizeof(g_active_mock->commands[index].name) - 1);
    g_active_mock->commands[index].name[sizeof(g_active_mock->commands[index].name) - 1] = '\0';
    g_active_mock->commands[index].function = function;
}

static void Mock_RemoveCommand(const char *name)
{
    if (g_active_mock == NULL || name == NULL)
    {
        return;
    }

    for (size_t index = 0; index < g_active_mock->command_count; ++index)
    {
        if (strcmp(g_active_mock->commands[index].name, name) == 0)
        {
            for (size_t move = index; move + 1 < g_active_mock->command_count; ++move)
            {
                g_active_mock->commands[move] = g_active_mock->commands[move + 1];
            }
            g_active_mock->command_count -= 1U;
            break;
        }
    }
}

static int Mock_CmdArgc(void)
{
    if (g_active_mock == NULL)
    {
        return 0;
    }

    return g_active_mock->command_argc;
}

static const char *Mock_CmdArgv(int index)
{
    if (g_active_mock == NULL || index < 0 || index >= g_active_mock->command_argc)
    {
        return NULL;
    }

    return g_active_mock->command_args[index];
}

static void Mock_InvokeCommand(mock_bot_import_t *mock, const char *name, const char *args)
{
    if (mock == NULL || name == NULL)
    {
        return;
    }

    for (size_t index = 0; index < mock->command_count; ++index)
    {
        if (strcmp(mock->commands[index].name, name) == 0)
        {
            Mock_SetCommandArgs(mock, name, args);
            if (mock->commands[index].function != NULL)
            {
                mock->commands[index].function();
            }
            Mock_ClearCommandArgs(mock);
            return;
        }
    }
}

static void Mock_Reset(mock_bot_import_t *mock)
{
    if (mock == NULL)
    {
        return;
    }

    memset(mock->prints, 0, sizeof(mock->prints));
    memset(mock->inputs, 0, sizeof(mock->inputs));
    memset(mock->input_clients, 0, sizeof(mock->input_clients));
	memset(mock->client_commands, 0, sizeof(mock->client_commands));
    mock->print_count = 0;
    mock->bot_input_count = 0;
	mock->client_command_count = 0;
    mock->command_count = 0;
    Mock_ClearCommandArgs(mock);
}

static void Mock_ClearPrints(mock_bot_import_t *mock)
{
    if (mock == NULL)
    {
        return;
    }

    memset(mock->prints, 0, sizeof(mock->prints));
    mock->print_count = 0;
}

static void Mock_CopyPrints(const mock_bot_import_t *mock,
                            captured_print_t *dest,
                            size_t dest_capacity,
                            size_t *out_count)
{
    if (out_count == NULL)
    {
        return;
    }

    *out_count = 0;

    if (mock == NULL || dest == NULL || dest_capacity == 0)
    {
        return;
    }

    size_t count = mock->print_count;
    if (count > dest_capacity)
    {
        count = dest_capacity;
    }

    for (size_t index = 0; index < count; ++index)
    {
        dest[index] = mock->prints[index];
    }

    *out_count = count;
}

static void Mock_AssertPrintsMatch(const captured_print_t *expected,
                                   size_t expected_count,
                                   const mock_bot_import_t *actual)
{
    assert_non_null(expected);
    assert_non_null(actual);

    assert_int_equal(actual->print_count, expected_count);
    for (size_t index = 0; index < expected_count; ++index)
    {
        assert_int_equal(actual->prints[index].type, expected[index].type);
        assert_string_equal(actual->prints[index].message, expected[index].message);
    }
}

static const char *Mock_FindPrint(const mock_bot_import_t *mock, const char *needle)
{
    if (mock == NULL || needle == NULL)
    {
        return NULL;
    }

    for (size_t index = 0; index < mock->print_count; ++index)
    {
        if (strstr(mock->prints[index].message, needle) != NULL)
        {
            return mock->prints[index].message;
        }
    }

    return NULL;
}

static const captured_print_t *Mock_FindPrintEntry(const mock_bot_import_t *mock,
                                                   const char *needle)
{
    if (mock == NULL || needle == NULL)
    {
        return NULL;
    }

    for (size_t index = 0; index < mock->print_count; ++index)
    {
        if (strstr(mock->prints[index].message, needle) != NULL)
        {
            return &mock->prints[index];
        }
    }

    return NULL;
}

static void Mock_AssertPrintContains(const mock_bot_import_t *mock,
					 const char *needle,
					 int expected_type)
{
	const captured_print_t *entry = Mock_FindPrintEntry(mock, needle);
	assert_non_null(entry);
	if (expected_type >= 0)
	{
		assert_int_equal(entry->type, expected_type);
	}
}

/*
=============
Mock_AssertSinglePrint

Checks an exact one-message diagnostic contract.
=============
*/
static void Mock_AssertSinglePrint(const mock_bot_import_t *mock,
	int expected_type,
	const char *expected_message)
{
	assert_non_null(mock);
	assert_non_null(expected_message);
	assert_int_equal(mock->print_count, 1);
	assert_int_equal(mock->prints[0].type, expected_type);
	assert_string_equal(mock->prints[0].message, expected_message);
}

/*
=============
test_console_commands_register

Confirms BotSetupLibrary registers the console helpers and teardown clears them.
=============
*/
static void test_console_commands_register(void **state)
{
	bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;

	Mock_Reset(&context->mock);

	int status = context->api->BotSetupLibrary();
	assert_int_equal(status, BLERR_NOERROR);

	assert_int_equal(context->mock.command_count, 3);
	assert_string_equal(context->mock.commands[0].name, "bot_test");
	assert_non_null(context->mock.commands[0].function);
	assert_string_equal(context->mock.commands[1].name, "aas_showpath");
	assert_non_null(context->mock.commands[1].function);
	assert_string_equal(context->mock.commands[2].name, "aas_showareas");

	context->api->BotShutdownLibrary();
	assert_int_equal(context->mock.command_count, 0);
}

/*
=============
test_console_commands_invoke

Validates the exported commands delegate to the same handlers as the bridge.
=============
*/

static void test_console_commands_invoke(void **state)
{
    bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;

    if (!ensure_map_fixture(&context->assets, "test1"))
    {
        cmocka_skip();
    }

    Mock_Reset(&context->mock);

    int status = context->api->BotSetupLibrary();
    assert_int_equal(status, BLERR_NOERROR);
    assert_int_equal(context->mock.command_count, 3);

    char *sounds[] = {"player/step1.wav"};
    status = context->api->BotLoadMap("maps/test1.bsp", 0, NULL, 1, sounds, 0, NULL);
    assert_int_equal(status, BLERR_NOERROR);
    assert_true(aasworld.loaded);
    assert_true(aasworld.numAreas > 0);

    vec3_t zero = {0.0f, 0.0f, 0.0f};

    captured_print_t expected_bot_test[ARRAY_LEN(context->mock.prints)];
    size_t expected_bot_test_count = 0U;

    Mock_ClearPrints(&context->mock);
    AAS_DebugBotTest(0, "1", zero, zero);
    Mock_CopyPrints(&context->mock,
                    expected_bot_test,
                    ARRAY_LEN(expected_bot_test),
                    &expected_bot_test_count);

    Mock_ClearPrints(&context->mock);
    Mock_InvokeCommand(&context->mock, "bot_test", "1");
    Mock_AssertPrintsMatch(expected_bot_test, expected_bot_test_count, &context->mock);

    captured_print_t expected_showpath[ARRAY_LEN(context->mock.prints)];
    size_t expected_showpath_count = 0U;

    Mock_ClearPrints(&context->mock);
    AAS_DebugShowPath(1, 2, zero, zero);
    Mock_CopyPrints(&context->mock,
                    expected_showpath,
                    ARRAY_LEN(expected_showpath),
                    &expected_showpath_count);

    Mock_ClearPrints(&context->mock);
    Mock_InvokeCommand(&context->mock, "aas_showpath", "1 2");
    Mock_AssertPrintsMatch(expected_showpath, expected_showpath_count, &context->mock);

    int invalid_area = aasworld.numAreas + 5;
    int area_list[] = {1, invalid_area};
    captured_print_t expected_showareas[ARRAY_LEN(context->mock.prints)];
    size_t expected_showareas_count = 0U;

    Mock_ClearPrints(&context->mock);
    AAS_DebugShowAreas(area_list, ARRAY_LEN(area_list));
    Mock_CopyPrints(&context->mock,
                    expected_showareas,
                    ARRAY_LEN(expected_showareas),
                    &expected_showareas_count);

    char area_args[64];
    snprintf(area_args, sizeof(area_args), "%d %d", area_list[0], area_list[1]);

    Mock_ClearPrints(&context->mock);
    Mock_InvokeCommand(&context->mock, "aas_showareas", area_args);
    Mock_AssertPrintsMatch(expected_showareas, expected_showareas_count, &context->mock);

    context->api->BotShutdownLibrary();
}

static int setup_bot_interface(void **state)
{
    bot_interface_test_context_t *context =
        (bot_interface_test_context_t *)calloc(1, sizeof(*context));
    assert_non_null(context);

    char contract_path[1024];
    snprintf(contract_path,
             sizeof(contract_path),
             "%s/tests/reference/botlib_contract.json",
             PROJECT_SOURCE_DIR);
    int load_status = BotlibContract_Load(contract_path, &context->catalogue);
    assert_int_equal(load_status, 0);

    context->mock.table.BotInput = Mock_BotInput;
    context->mock.table.BotClientCommand = Mock_BotClientCommand;
    context->mock.table.Print = Mock_Print;
    context->mock.table.CvarGet = Mock_CvarGet;
    context->mock.table.Error = Mock_Error;
    context->mock.table.Trace = Mock_Trace;
    context->mock.table.PointContents = Mock_PointContents;
    context->mock.table.GetMemory = Mock_GetMemory;
    context->mock.table.FreeMemory = Mock_FreeMemory;
    context->mock.table.DebugLineCreate = Mock_DebugLineCreate;
    context->mock.table.DebugLineDelete = Mock_DebugLineDelete;
    context->mock.table.DebugLineShow = Mock_DebugLineShow;
    context->mock.table.AddCommand = Mock_AddCommand;
    context->mock.table.RemoveCommand = Mock_RemoveCommand;
    context->mock.table.CmdArgc = Mock_CmdArgc;
    context->mock.table.CmdArgv = Mock_CmdArgv;

    LibVar_Init();
    context->libvar_initialised = true;

    if (!asset_env_initialise(&context->assets))
    {
        asset_env_cleanup(&context->assets);
        free(context);
        cmocka_skip();
    }

    char weapon_config_path[PATH_MAX];
    int written = snprintf(weapon_config_path,
                           sizeof(weapon_config_path),
                           "%s/weapons.c",
                           context->assets.asset_root);
    if (written <= 0 || (size_t)written >= sizeof(weapon_config_path))
    {
        asset_env_cleanup(&context->assets);
        free(context);
        cmocka_skip();
    }

    LibVarSet("basedir", context->assets.asset_root);
    LibVarSet("gamedir", "");
    LibVarSet("cddir", "");
    LibVarSet("gladiator_asset_dir", "");
    LibVarSet("weaponconfig", "weapons.c");
    LibVarSet("max_weaponinfo", "64");
    LibVarSet("max_projectileinfo", "64");
    LibVarSet("itemconfig", "items.c");

    g_active_mock = &context->mock;
	context->api = GetBotAPIEx(&context->mock.table,
		sizeof(context->mock.table));
    assert_non_null(context->api);

    *state = context;
    return 0;
}

/*
=============
teardown_bot_interface

Releases the parity harness fixtures and shuts down the active botlib.
=============
*/
static int teardown_bot_interface(void **state)
{
	bot_interface_test_context_t *context =
		(bot_interface_test_context_t *)(state != NULL ? *state : NULL);

	if (context != NULL && context->api != NULL && BotLibraryInitialized())
	{
		context->api->BotShutdownLibrary();
	}

	if (context != NULL)
	{
		asset_env_cleanup(&context->assets);
	}

	if (context != NULL && context->libvar_initialised)
	{
		LibVar_Shutdown();
		context->libvar_initialised = false;
	}

	BotState_ShutdownAll();
	g_active_mock = NULL;
	BotInterface_SetImportTable(NULL);
	if (context != NULL)
	{
		BotlibContract_Free(&context->catalogue);
	}
	free(context);
	return 0;
}

/*
=============
test_get_bot_api_copies_allocator_callbacks

Proves allocator imports remain fixed after the caller mutates its source table.
=============
*/
static void test_get_bot_api_copies_allocator_callbacks(void **state)
{
	bot_interface_test_context_t *context =
		(bot_interface_test_context_t *)*state;

	memset(&g_primary_allocator_capture, 0,
		sizeof(g_primary_allocator_capture));
	memset(&g_alternate_allocator_capture, 0,
		sizeof(g_alternate_allocator_capture));

	context->mock.table.GetMemory = Alternate_GetMemory;
	context->mock.table.FreeMemory = Alternate_FreeMemory;

	const size_t payload_size = 53;
	void *payload = GetMemory(payload_size);
	assert_non_null(payload);
	assert_int_equal(g_primary_allocator_capture.allocation_count, 1);
	assert_int_equal(g_alternate_allocator_capture.allocation_count, 0);

	const size_t total_size = MemoryByteSize(payload);
	assert_true(total_size > payload_size);
	assert_int_equal(g_primary_allocator_capture.last_request_size,
		(int)total_size);
	void *header = (unsigned char *)payload - (total_size - payload_size);
	assert_ptr_equal(header, g_primary_allocator_capture.last_allocated_header);

	FreeMemory(payload);
	assert_int_equal(g_primary_allocator_capture.free_count, 1);
	assert_ptr_equal(g_primary_allocator_capture.last_freed_header, header);
	assert_int_equal(g_alternate_allocator_capture.free_count, 0);

	context->mock.table.GetMemory = Mock_GetMemory;
	context->mock.table.FreeMemory = Mock_FreeMemory;
}

/*
=============
test_get_bot_api_copies_retail_import_table

Proves ordinary retail imports do not follow later caller-table mutation.
=============
*/
static void test_get_bot_api_copies_retail_import_table(void **state)
{
	bot_interface_test_context_t *context =
		(bot_interface_test_context_t *)*state;
	void (*original_print)(int, char *, ...) = context->mock.table.Print;

	assert_ptr_not_equal(Q2Bridge_GetImportTable(), &context->mock.table);
	Mock_ClearPrints(&context->mock);
	context->mock.table.Print = NULL;

	int status = context->api->BotLoadMap("unused", 0, NULL, 0, NULL, 0, NULL);
	assert_int_equal(status, BLERR_LIBRARYNOTSETUP);
	Mock_AssertSinglePrint(&context->mock,
		PRT_ERROR,
		"BotLoadMap: bot library used before being setup\n");

	Mock_ClearPrints(&context->mock);
	Q2_Print(PRT_MESSAGE, "copied retail import\n");
	Mock_AssertSinglePrint(&context->mock,
		PRT_MESSAGE,
		"copied retail import\n");

	context->mock.table.Print = original_print;
}

/*
=============
test_get_bot_api_bounds_retail_import_prefix

Proves the retail entry point copies ten callbacks without reading extensions.
=============
*/
static void test_get_bot_api_bounds_retail_import_prefix(void **state)
{
	bot_interface_test_context_t *context =
		(bot_interface_test_context_t *)*state;
	assert_int_equal(BOT_IMPORT_RETAIL_SIZE,
		10U * sizeof(void (*)(void)));

	bot_export_t *retail_api = GetBotAPI(&context->mock.table);
	assert_non_null(retail_api);
	const bot_import_t *copied = Q2Bridge_GetImportTable();
	assert_non_null(copied);
	assert_ptr_equal(copied->BotInput, context->mock.table.BotInput);
	assert_ptr_equal(copied->DebugLineShow,
		context->mock.table.DebugLineShow);
	assert_null(copied->CvarGet);
	assert_null(copied->Error);
	assert_null(copied->AddCommand);
	assert_null(copied->RemoveCommand);
	assert_null(copied->CmdArgc);
	assert_null(copied->CmdArgv);

	context->api = GetBotAPIEx(&context->mock.table,
		sizeof(context->mock.table));
	assert_non_null(context->api);
}

static void test_bot_load_map_requires_library(void **state)
{
    bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;

    Mock_Reset(&context->mock);

    int status = context->api->BotLoadMap("maps/test.bsp", 0, NULL, 0, NULL, 0, NULL);
    assert_true(context->mock.print_count > 0);

    const botlib_contract_export_t *guard =
        BotlibContract_FindExport(&context->catalogue, "GuardLibrarySetup");
    assert_non_null(guard);
    const botlib_contract_scenario_t *failure = BotlibContract_FindScenario(guard, "failure");
    assert_non_null(failure);
    const botlib_contract_message_t *message =
        BotlibContract_FindMessageWithSeverity(failure, context->mock.prints[0].type);
    assert_non_null(message);

    char expected_message[1024];
    snprintf(expected_message, sizeof(expected_message), message->text, "BotLoadMap");
    assert_string_equal(context->mock.prints[0].message, expected_message);

    const botlib_contract_return_code_t *expected_status =
        BotlibContract_FindReturnCode(failure, BLERR_LIBRARYNOTSETUP);
    assert_non_null(expected_status);
    assert_int_equal(status, expected_status->value);
}

/*
=============
test_bot_load_map_null_refreshes_assets_without_reset

Pins NULL map refreshes to asset updates without map, client, or frame resets.
=============
*/
static void test_bot_load_map_null_refreshes_assets_without_reset(void **state)
{
	bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;
	bot_settings_t settings;
	memset(&settings, 0, sizeof(settings));
	snprintf(settings.characterfile, sizeof(settings.characterfile), "bots/babe_c.c");
	snprintf(settings.charactername, sizeof(settings.charactername), "babe");

	Mock_Reset(&context->mock);

	int status = context->api->BotSetupLibrary();
	assert_int_equal(status, BLERR_NOERROR);
	status = context->api->BotSetupClient(1, &settings);
	assert_true(status);

	bot_client_state_t *client_state = BotState_Get(1);
	assert_non_null(client_state);
	client_state->client_update_valid = true;
	client_state->last_client_update.origin[0] = 123.0f;
	client_state->last_update_time = 17.0f;

	aasworld.loaded = qtrue;
	aasworld.initialized = qtrue;
	aasworld.time = 29.0f;
	snprintf(aasworld.mapName, sizeof(aasworld.mapName), "retained_world");
	TranslateEntity_SetWorldLoaded(qtrue);

	BotMove_MoverCatalogueReset();
	bot_mover_catalogue_entry_t mover = {0};
	mover.modelnum = 1;
	mover.kind = BOT_MOVER_KIND_FUNC_PLAT;
	assert_true(BotMove_MoverCatalogueInsert(&mover));

	char *models[] = {"maps/retained.bsp", "*1"};
	char *sounds[] = {"sound/old.wav"};
	char *images[] = {"pics/retained.pcx"};

	Mock_ClearPrints(&context->mock);
	status = context->api->BotLoadMap(NULL, 2, models, 1, sounds, 1, images);
	assert_int_equal(status, BLERR_NOERROR);
	assert_int_equal(context->mock.print_count, 0);
	assert_string_equal(AAS_SoundSubsystem_AssetName(0), "sound/old.wav");

	const bot_mover_catalogue_entry_t *catalogue_entry =
		BotMove_MoverCatalogueFindByModel(1);
	assert_non_null(catalogue_entry);
	assert_int_equal(catalogue_entry->modelindex, 1);

	vec3_t origin = {0.0f, 0.0f, 0.0f};
	status = context->api->BotAddSound(origin, 0, 0, 0, 1.0f, 1.0f, 0.0f);
	assert_int_equal(status, BLERR_NOERROR);
	assert_int_equal(AAS_SoundSubsystem_SoundEventCount(), 1);
	status = context->api->BotAddSound(origin, 0, 0, 1, 1.0f, 1.0f, 0.0f);
	assert_int_equal(status, BLERR_INVALIDSOUNDINDEX);

	char *refreshed_models[] = {
		"maps/retained.bsp",
		"models/objects/other/tris.md2",
		"*1",
	};
	char *refreshed_sounds[] = {
		"sound/replacement.wav",
		"sound/new-index.wav",
	};

	Mock_ClearPrints(&context->mock);
	status = context->api->BotLoadMap(NULL,
		3,
		refreshed_models,
		2,
		refreshed_sounds,
		1,
		images);
	assert_int_equal(status, BLERR_NOERROR);
	assert_int_equal(context->mock.print_count, 0);

	assert_true(aasworld.loaded);
	assert_true(aasworld.initialized);
	assert_float_equal(aasworld.time, 29.0f, 0.0001f);
	assert_string_equal(aasworld.mapName, "retained_world");
	assert_ptr_equal(BotState_Get(1), client_state);
	assert_true(client_state->client_update_valid);
	assert_float_equal(client_state->last_client_update.origin[0], 123.0f, 0.0001f);
	assert_float_equal(client_state->last_update_time, 17.0f, 0.0001f);
	assert_int_equal(BotState_ActiveClientCount(), 1);
	assert_int_equal(AAS_SoundSubsystem_SoundEventCount(), 1);
	assert_string_equal(AAS_SoundSubsystem_AssetName(0), "sound/replacement.wav");
	assert_string_equal(AAS_SoundSubsystem_AssetName(1), "sound/new-index.wav");

	catalogue_entry = BotMove_MoverCatalogueFindByModel(1);
	assert_non_null(catalogue_entry);
	assert_int_equal(catalogue_entry->modelindex, 2);

	status = context->api->BotAddSound(origin, 0, 0, 1, 1.0f, 1.0f, 0.0f);
	assert_int_equal(status, BLERR_NOERROR);
	assert_int_equal(AAS_SoundSubsystem_SoundEventCount(), 2);

	char *ignored_sounds[] = {"sound/ignored.wav"};
	Mock_ClearPrints(&context->mock);
	status = context->api->BotLoadMap("", 0, NULL, 1, ignored_sounds, 0, NULL);
	assert_int_equal(status, BLERR_NOBSPFILE);
	assert_int_equal(context->mock.print_count, 2);
	assert_int_equal(context->mock.prints[0].type, PRT_MESSAGE);
	assert_string_equal(context->mock.prints[0].message,
		"------------ Map Loading ------------\n");
	assert_null(Mock_FindPrint(&context->mock,
		"-------------------------------------\n"));
	const captured_print_t *missing_bsp =
		Mock_FindPrintEntry(&context->mock, "couldn't find the bsp file");
	assert_non_null(missing_bsp);
	assert_int_equal(missing_bsp->type, PRT_FATAL);
#ifdef _WIN32
	assert_string_equal(missing_bsp->message,
		"couldn't find the bsp file maps\\.bsp\n");
#else
	assert_string_equal(missing_bsp->message,
		"couldn't find the bsp file maps/.bsp\n");
#endif
	assert_string_equal(AAS_SoundSubsystem_AssetName(0), "sound/ignored.wav");
	assert_null(AAS_SoundSubsystem_AssetName(1));
	assert_false(aasworld.loaded);
	assert_ptr_equal(BotState_Get(1), client_state);
	assert_true(client_state->client_update_valid);

	status = context->api->BotShutdownClient(1);
	assert_int_equal(status, BLERR_NOERROR);
	status = context->api->BotShutdownLibrary();
	assert_int_equal(status, BLERR_NOERROR);
}


/*
=============
test_bot_setup_library_guard_emits_message

Ensures repeated setup attempts log the legacy guard diagnostic.
=============
*/
static void test_bot_setup_library_guard_emits_message(void **state)
{
	bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;

	Mock_Reset(&context->mock);

	int status = context->api->BotSetupLibrary();
	assert_int_equal(status, BLERR_NOERROR);

	Mock_ClearPrints(&context->mock);

	status = context->api->BotSetupLibrary();
	assert_int_equal(status, BLERR_LIBRARYALREADYSETUP);
	Mock_AssertPrintContains(&context->mock, "bot library already setup", PRT_ERROR);

	context->api->BotShutdownLibrary();
}

/*
=============
test_bot_setup_failure_retains_retail_setup_flag

Pins the committed setup flag and AAS-before-AI-before-EA failure ordering.
=============
*/
static void test_bot_setup_failure_retains_retail_setup_flag(void **state)
{
	bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;

	Mock_Reset(&context->mock);
	assert_false(BotLibraryInitialized());
	assert_false(AAS_Initialized());
	assert_false(EA_IsInitialised());

	int status = context->api->BotLibVarSet(
		"weaponconfig", "definitely_missing_setup_weapon.c");
	assert_int_equal(status, BLERR_NOERROR);

	status = context->api->BotSetupLibrary();
	assert_int_equal(status, BLERR_CANNOTLOADWEAPONCONFIG);
	assert_true(BotLibraryInitialized());
	assert_true(AAS_Initialized());
	assert_int_equal(context->api->BotLibraryInitialized(), qtrue);
	assert_false(EA_IsInitialised());
	assert_null(Mock_FindPrint(&context->mock,
		"-------------------------------------\n"));

	Mock_ClearPrints(&context->mock);
	status = context->api->BotSetupLibrary();
	assert_int_equal(status, BLERR_LIBRARYALREADYSETUP);
	Mock_AssertSinglePrint(&context->mock,
		PRT_ERROR,
		"bot library already setup\n");

	status = context->api->BotShutdownLibrary();
	assert_int_equal(status, BLERR_NOERROR);
	assert_false(BotLibraryInitialized());
	assert_false(AAS_Initialized());
	assert_false(EA_IsInitialised());
}

/*
=============
test_bot_shutdown_library_guard_emits_message

Validates the shutdown export emits the HLIL guard diagnostic.
=============
*/
static void test_bot_shutdown_library_guard_emits_message(void **state)
{
	bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;

	Mock_Reset(&context->mock);
	Mock_ClearPrints(&context->mock);

	int status = context->api->BotShutdownLibrary();
	assert_int_equal(status, BLERR_LIBRARYNOTSETUP);
	Mock_AssertPrintContains(&context->mock, "bot library already shutdown", PRT_ERROR);

	status = context->api->BotSetupLibrary();
	assert_int_equal(status, BLERR_NOERROR);
	Mock_ClearPrints(&context->mock);
	status = context->api->BotShutdownLibrary();
	assert_int_equal(status, BLERR_NOERROR);
	assert_non_null(Mock_FindPrint(&context->mock, "AAS shutdown.\n"));
	assert_null(Mock_FindPrint(&context->mock, "BotLib Shutdown"));
}

/*
=============
test_bot_library_initialized_reports_aas_state

Pins the exported initialized query to the retail AAS continuation flag.
=============
*/
static void test_bot_library_initialized_reports_aas_state(void **state)
{
	bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;

	assert_int_equal(context->api->BotLibraryInitialized(), qfalse);

	int status = context->api->BotSetupLibrary();
	assert_int_equal(status, BLERR_NOERROR);
	assert_true(BotLibraryInitialized());
	assert_int_equal(context->api->BotLibraryInitialized(), qtrue);

	aasworld.initialized = qfalse;
	assert_true(BotLibraryInitialized());
	assert_int_equal(context->api->BotLibraryInitialized(), qfalse);
	aasworld.initialized = qtrue;

	status = context->api->BotShutdownLibrary();
	assert_int_equal(status, BLERR_NOERROR);
	assert_int_equal(context->api->BotLibraryInitialized(), qfalse);
}

/*
=============
test_bot_define_preserves_retail_failure_contract

Confirms a failed global define emits the retail diagnostic but still returns zero.
=============
*/
static void test_bot_define_preserves_retail_failure_contract(void **state)
{
	bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;

	Mock_Reset(&context->mock);

	int status = context->api->BotDefine("123");
	assert_int_equal(status, BLERR_NOERROR);

	const captured_print_t *entry =
		Mock_FindPrintEntry(&context->mock, "couldn't add define 123\n");
	assert_non_null(entry);
	assert_int_equal(entry->type, PRT_ERROR);
	assert_string_equal(entry->message, "couldn't add define 123\n");
}

/*
=============
test_entity_exports_validate_entity_first

Pins the shared retail entity guard ahead of per-export payload validation.
=============
*/
static void test_entity_exports_validate_entity_first(void **state)
{
	bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;
	vec3_t origin = {0.0f, 0.0f, 0.0f};

	Mock_Reset(&context->mock);

	int status = context->api->BotSetupLibrary();
	assert_int_equal(status, BLERR_NOERROR);

	Mock_ClearPrints(&context->mock);
	status = context->api->BotUpdateEntity(-1, NULL);
	assert_int_equal(status, BLERR_INVALIDENTITYNUMBER);
	const captured_print_t *entry =
		Mock_FindPrintEntry(&context->mock,
			"BotUpdateEntity: invalid entity number -1, [0, 1024]\n");
	assert_non_null(entry);
	assert_int_equal(entry->type, PRT_ERROR);
	assert_string_equal(entry->message,
		"BotUpdateEntity: invalid entity number -1, [0, 1024]\n");

	Mock_ClearPrints(&context->mock);
	status = context->api->BotAddSound(origin, -1, 0, -1, 1.0f, 1.0f, 0.0f);
	assert_int_equal(status, BLERR_INVALIDENTITYNUMBER);
	entry = Mock_FindPrintEntry(&context->mock,
		"BotUpdateSound: invalid entity number -1, [0, 1024]\n");
	assert_non_null(entry);
	assert_int_equal(entry->type, PRT_ERROR);
	assert_string_equal(entry->message,
		"BotUpdateSound: invalid entity number -1, [0, 1024]\n");

	Mock_ClearPrints(&context->mock);
	status = context->api->BotAddPointLight(origin,
		-1,
		64.0f,
		1.0f,
		1.0f,
		1.0f,
		0.0f,
		0.0f);
	assert_int_equal(status, BLERR_INVALIDENTITYNUMBER);
	entry = Mock_FindPrintEntry(&context->mock,
		"BotAddPointLight: invalid entity number -1, [0, 1024]\n");
	assert_non_null(entry);
	assert_int_equal(entry->type, PRT_ERROR);
	assert_string_equal(entry->message,
		"BotAddPointLight: invalid entity number -1, [0, 1024]\n");

	Mock_ClearPrints(&context->mock);
	status = context->api->BotAddPointLight(origin,
		1024,
		64.0f,
		1.0f,
		1.0f,
		1.0f,
		0.0f,
		0.0f);
	assert_int_equal(status, BLERR_NOERROR);
	assert_null(Mock_FindPrint(&context->mock, "invalid entity number"));

	Mock_ClearPrints(&context->mock);
	status = context->api->BotAddPointLight(origin,
		1025,
		64.0f,
		1.0f,
		1.0f,
		1.0f,
		0.0f,
		0.0f);
	assert_int_equal(status, BLERR_INVALIDENTITYNUMBER);
	entry = Mock_FindPrintEntry(&context->mock,
		"BotAddPointLight: invalid entity number 1025, [0, 1024]\n");
	assert_non_null(entry);
	assert_int_equal(entry->type, PRT_ERROR);
	assert_string_equal(entry->message,
		"BotAddPointLight: invalid entity number 1025, [0, 1024]\n");

	context->api->BotShutdownLibrary();
}

/*
=============
test_bot_console_message_inactive_contract

Checks the retail inactive-client code and misspelled diagnostic verbatim.
=============
*/
static void test_bot_console_message_inactive_contract(void **state)
{
	bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;

	Mock_Reset(&context->mock);

	int status = context->api->BotSetupLibrary();
	assert_int_equal(status, BLERR_NOERROR);

	Mock_ClearPrints(&context->mock);
	status = context->api->BotConsoleMessage(1, CMS_CHAT, "hello");
	assert_int_equal(status, BLERR_AICMFORINACTIVECLIENT);

	const captured_print_t *entry = Mock_FindPrintEntry(&context->mock,
		"recieved console message for inactive bot client\n");
	assert_non_null(entry);
	assert_int_equal(entry->type, PRT_ERROR);
	assert_string_equal(entry->message,
		"recieved console message for inactive bot client\n");

	context->api->BotShutdownLibrary();
}

/*
=============
test_bot_setup_client_preserves_retail_boolean_abi

Pins setup-client guard, invalid, duplicate, and success results to boolean ABI.
=============
*/
static void test_bot_setup_client_preserves_retail_boolean_abi(void **state)
{
	bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;
	bot_settings_t settings;
	memset(&settings, 0, sizeof(settings));
	snprintf(settings.characterfile, sizeof(settings.characterfile), "bots/babe_c.c");
	snprintf(settings.charactername, sizeof(settings.charactername), "babe");

	Mock_Reset(&context->mock);

	int status = context->api->BotSetupClient(1, &settings);
	assert_false(status);
	assert_int_equal(BotState_ActiveClientCount(), 0);
	Mock_AssertPrintContains(&context->mock,
		"BotSetupClient: bot library used before being setup\n",
		PRT_ERROR);

	status = context->api->BotSetupLibrary();
	assert_int_equal(status, BLERR_NOERROR);

	Mock_ClearPrints(&context->mock);
	status = context->api->BotSetupClient(MAX_CLIENTS, &settings);
	assert_false(status);
	assert_int_equal(BotState_ActiveClientCount(), 0);
	Mock_AssertPrintContains(&context->mock,
		"BotSetupClient: invalid client",
		PRT_ERROR);

	status = context->api->BotSetupClient(1, &settings);
	assert_true(status);
	assert_int_equal(BotState_ActiveClientCount(), 1);

	bot_client_state_t *active_state = BotState_Get(1);
	assert_non_null(active_state);

	Mock_ClearPrints(&context->mock);
	status = context->api->BotSetupClient(1, &settings);
	assert_false(status);
	assert_ptr_equal(BotState_Get(1), active_state);
	assert_int_equal(BotState_ActiveClientCount(), 1);
	Mock_AssertPrintContains(&context->mock,
		"client 1 already setup\n",
		PRT_FATAL);

	Mock_ClearPrints(&context->mock);
	status = context->api->BotSetupClient(1, NULL);
	assert_false(status);
	Mock_AssertSinglePrint(&context->mock,
		PRT_FATAL,
		"client 1 already setup\n");

	status = context->api->BotShutdownClient(1);
	assert_int_equal(status, BLERR_NOERROR);
	status = context->api->BotShutdownLibrary();
	assert_int_equal(status, BLERR_NOERROR);
}

/*
=============
test_bot_shutdown_library_releases_weapon_state_handles

Ensures library teardown drains the reconstructed weapon-state handle table.
=============
*/
static void test_bot_shutdown_library_releases_weapon_state_handles(void **state)
{
	bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;

	Mock_Reset(&context->mock);

	int status = context->api->BotSetupLibrary();
	assert_int_equal(status, BLERR_NOERROR);
	assert_int_equal(BotState_ActiveClientCount(), 0);

	for (int index = 0; index < MAX_CLIENTS; ++index)
	{
		int handle = context->api->BotAllocWeaponState();
		assert_true(handle > 0);
	}

	Mock_ClearPrints(&context->mock);
	assert_int_equal(context->api->BotAllocWeaponState(), 0);
	Mock_AssertPrintContains(&context->mock,
	                         "BotAllocWeaponState: no free weapon state slots",
	                         PRT_ERROR);

	status = context->api->BotShutdownLibrary();
	assert_int_equal(status, BLERR_NOERROR);

	status = context->api->BotSetupLibrary();
	assert_int_equal(status, BLERR_NOERROR);

	int handle = context->api->BotAllocWeaponState();
	assert_true(handle > 0);

	context->api->BotFreeWeaponState(handle);
	status = context->api->BotShutdownLibrary();
	assert_int_equal(status, BLERR_NOERROR);
}

/*
=============
test_shutdown_library_releases_client_weapon_wiring

Verifies full library shutdown clears bot client state before freeing character
weapon weights and lets the same client attach weapon weights after restart.
=============
*/
static void test_shutdown_library_releases_client_weapon_wiring(void **state)
{
	bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;

	Mock_Reset(&context->mock);

	int status = context->api->BotSetupLibrary();
	assert_int_equal(status, BLERR_NOERROR);

	bot_settings_t settings;
	memset(&settings, 0, sizeof(settings));
	snprintf(settings.characterfile, sizeof(settings.characterfile), "bots/babe_c.c");
	snprintf(settings.charactername, sizeof(settings.charactername), "babe");

	status = context->api->BotSetupClient(1, &settings);
	assert_true(status);
	assert_int_equal(BotState_ActiveClientCount(), 1);

	bot_client_state_t *state_slot = BotState_Get(1);
	assert_non_null(state_slot);
	assert_true(state_slot->weapon_state > 0);
	assert_non_null(state_slot->weapon_weights);

	status = context->api->BotShutdownLibrary();
	assert_int_equal(status, BLERR_NOERROR);
	assert_null(BotState_Get(1));
	assert_int_equal(BotState_ActiveClientCount(), 0);

	status = context->api->BotSetupLibrary();
	assert_int_equal(status, BLERR_NOERROR);
	assert_int_equal(BotState_ActiveClientCount(), 0);

	status = context->api->BotSetupClient(1, &settings);
	assert_true(status);
	assert_int_equal(BotState_ActiveClientCount(), 1);

	state_slot = BotState_Get(1);
	assert_non_null(state_slot);
	assert_true(state_slot->weapon_state > 0);
	assert_non_null(state_slot->weapon_weights);

	bot_weapon_info_t blaster;
	memset(&blaster, 0, sizeof(blaster));
	context->api->BotGetWeaponInfo(state_slot->weapon_state, 0, &blaster);
	assert_string_equal(blaster.name, "Blaster");

	bot_weapon_info_t machinegun;
	memset(&machinegun, 0, sizeof(machinegun));
	context->api->BotGetWeaponInfo(state_slot->weapon_state, 3, &machinegun);
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

	assert_int_equal(context->api->BotChooseBestFightWeapon(state_slot->weapon_state,
															inventory),
					 3);

	status = context->api->BotShutdownLibrary();
	assert_int_equal(status, BLERR_NOERROR);
	assert_null(BotState_Get(1));
	assert_int_equal(BotState_ActiveClientCount(), 0);
}

/*
=============
test_weapon_exports_drive_weighted_selection

Pins the public export-table wiring for weapon weights, info, scoring, and reset.
=============
*/
static void test_weapon_exports_drive_weighted_selection(void **state)
{
	bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;

	Mock_Reset(&context->mock);

	int status = context->api->BotSetupLibrary();
	assert_int_equal(status, BLERR_NOERROR);

	int handle = context->api->BotAllocWeaponState();
	assert_true(handle > 0);

	status = context->api->BotLoadWeaponWeights(handle, "default/defaul_w.c");
	assert_int_equal(status, BLERR_NOERROR);

	bot_weapon_info_t blaster;
	memset(&blaster, 0, sizeof(blaster));
	context->api->BotGetWeaponInfo(handle, 0, &blaster);
	assert_string_equal(blaster.name, "Blaster");
	assert_non_null(blaster.projectileinfo);
	assert_string_equal(blaster.projectileinfo->name, "blasterbolt");

	bot_weapon_info_t machinegun;
	memset(&machinegun, 0, sizeof(machinegun));
	context->api->BotGetWeaponInfo(handle, 3, &machinegun);
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

	assert_int_equal(context->api->BotChooseBestFightWeapon(handle, inventory), 3);
	assert_int_equal(context->api->BotGetTopRankedWeapon(handle), 0);

	context->api->BotResetWeaponState(handle);
	assert_int_equal(context->api->BotGetTopRankedWeapon(handle), 0);

	context->api->BotFreeWeaponWeights(handle);
	assert_int_equal(context->api->BotChooseBestFightWeapon(handle, inventory), 0);

	context->api->BotFreeWeaponState(handle);
	status = context->api->BotShutdownLibrary();
	assert_int_equal(status, BLERR_NOERROR);
}

/*
=============
test_weapon_info_export_zeroes_invalid_queries

Pins BotGetWeaponInfo's retail validation order and caller-buffer clearing.
=============
*/
static void test_weapon_info_export_zeroes_invalid_queries(void **state)
{
	bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;

	Mock_Reset(&context->mock);

	int status = context->api->BotSetupLibrary();
	assert_int_equal(status, BLERR_NOERROR);

	int handle = context->api->BotAllocWeaponState();
	assert_true(handle > 0);

	bot_weapon_info_t zero;
	memset(&zero, 0, sizeof(zero));

	bot_weapon_info_t info;
	memset(&info, 0x7f, sizeof(info));
	Mock_ClearPrints(&context->mock);
	context->api->BotGetWeaponInfo(handle, 999, &info);
	assert_memory_equal(&info, &zero, sizeof(info));
	Mock_AssertPrintContains(&context->mock, "weapon number out of range", PRT_ERROR);

	memset(&info, 0x7f, sizeof(info));
	Mock_ClearPrints(&context->mock);
	context->api->BotGetWeaponInfo(MAX_CLIENTS + 1, 0, &info);
	assert_memory_equal(&info, &zero, sizeof(info));
	Mock_AssertPrintContains(&context->mock, "weapon state handle", PRT_FATAL);

	context->api->BotFreeWeaponState(handle);

	memset(&info, 0x7f, sizeof(info));
	Mock_ClearPrints(&context->mock);
	status = context->api->BotShutdownLibrary();
	assert_int_equal(status, BLERR_NOERROR);
	context->api->BotGetWeaponInfo(handle, 0, &info);
	assert_memory_equal(&info, &zero, sizeof(info));
	Mock_AssertPrintContains(&context->mock,
	                         "BotGetWeaponInfo: bot library used before being setup\n",
	                         PRT_ERROR);
}

/*
=============
test_bot_shutdown_client_requires_library

Checks the shutdown export enforces the library guard path.
=============
*/
static void test_bot_shutdown_client_requires_library(void **state)
{
	bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;

	Mock_Reset(&context->mock);
	Mock_ClearPrints(&context->mock);

	int status = context->api->BotShutdownClient(1);
	assert_int_equal(status, BLERR_LIBRARYNOTSETUP);
	Mock_AssertPrintContains(&context->mock,
	                         "BotShutdownClient: bot library used before being setup\n",
	                         PRT_ERROR);
}

/*
=============
test_bot_move_client_requires_library

Ensures BotMoveClient logs the guard diagnostic before setup.
=============
*/
static void test_bot_move_client_requires_library(void **state)
{
	bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;

	Mock_Reset(&context->mock);
	Mock_ClearPrints(&context->mock);

	int status = context->api->BotMoveClient(0, 1);
	assert_int_equal(status, BLERR_LIBRARYNOTSETUP);
	Mock_AssertPrintContains(&context->mock,
	                         "BotMoveClient: bot library used before being setup\n",
	                         PRT_ERROR);
}

/*
=============
test_client_runtime_failure_diagnostics

Pins the retail client-state failure codes, severities, and diagnostics.
=============
*/
static void test_client_runtime_failure_diagnostics(void **state)
{
	bot_interface_test_context_t *context =
		(bot_interface_test_context_t *)*state;
	bot_settings_t settings = {0};
	bot_updateclient_t update = {0};

	snprintf(settings.characterfile, sizeof(settings.characterfile),
		"bots/babe_c.c");
	snprintf(settings.charactername, sizeof(settings.charactername), "babe");

	int status = context->api->BotSetupLibrary();
	assert_int_equal(status, BLERR_NOERROR);

	Mock_ClearPrints(&context->mock);
	status = context->api->BotShutdownClient(1);
	assert_int_equal(status, BLERR_AICLIENTALREADYSHUTDOWN);
	Mock_AssertSinglePrint(&context->mock,
		PRT_ERROR,
		"client 1 already shutdown\n");

	Mock_ClearPrints(&context->mock);
	status = context->api->BotMoveClient(1, 2);
	assert_int_equal(status, BLERR_AIMOVEINACTIVECLIENT);
	Mock_AssertSinglePrint(&context->mock,
		PRT_FATAL,
		"tried to move inactive bot client\n");

	Mock_ClearPrints(&context->mock);
	status = context->api->BotUpdateClient(1, &update);
	assert_int_equal(status, BLERR_AIUPDATEINACTIVECLIENT);
	Mock_AssertSinglePrint(&context->mock,
		PRT_FATAL,
		"tried to updated inactive bot client\n");

	Mock_ClearPrints(&context->mock);
	status = context->api->BotSettings(1, &settings);
	assert_int_equal(status, BLERR_SETTINGSINACTIVECLIENT);
	Mock_AssertSinglePrint(&context->mock,
		PRT_FATAL,
		"tried to update settings of inactive client\n");

	Mock_ClearPrints(&context->mock);
	status = context->api->BotAI(1, 0.1f);
	assert_int_equal(status, BLERR_AICLIENTNOTSETUP);
	Mock_AssertSinglePrint(&context->mock,
		PRT_FATAL,
		"client 1 hasn't been setup\n");

	status = context->api->BotSetupClient(1, &settings);
	assert_true(status);
	status = context->api->BotSetupClient(2, &settings);
	assert_true(status);

	Mock_ClearPrints(&context->mock);
	status = context->api->BotMoveClient(1, 1);
	assert_int_equal(status, BLERR_AIMOVETOACTIVECLIENT);
	Mock_AssertSinglePrint(&context->mock,
		PRT_FATAL,
		"tried to move client to active client\n");

	Mock_ClearPrints(&context->mock);
	status = context->api->BotMoveClient(1, 2);
	assert_int_equal(status, BLERR_AIMOVETOACTIVECLIENT);
	Mock_AssertSinglePrint(&context->mock,
		PRT_FATAL,
		"tried to move client to active client\n");

	assert_int_equal(context->api->BotShutdownClient(1), BLERR_NOERROR);
	assert_int_equal(context->api->BotShutdownClient(2), BLERR_NOERROR);
	assert_int_equal(context->api->BotShutdownLibrary(), BLERR_NOERROR);
}

/*
=============
test_retail_exports_share_exact_library_guard

Pins every retail runtime wrapper to the shared setup guard and function name.
=============
*/
static void test_retail_exports_share_exact_library_guard(void **state)
{
	bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;
	const captured_print_t *entry;
	vec3_t origin = {0.0f, 0.0f, 0.0f};
	int status;

	Mock_Reset(&context->mock);

	status = context->api->BotStartFrame(1.0f);
	assert_int_equal(status, BLERR_LIBRARYNOTSETUP);
	entry = Mock_FindPrintEntry(&context->mock,
		"BotStartFrame: bot library used before being setup\n");
	assert_non_null(entry);
	assert_int_equal(entry->type, PRT_ERROR);
	assert_string_equal(entry->message,
		"BotStartFrame: bot library used before being setup\n");

	Mock_ClearPrints(&context->mock);
	status = context->api->BotUpdateClient(0, NULL);
	assert_int_equal(status, BLERR_LIBRARYNOTSETUP);
	entry = Mock_FindPrintEntry(&context->mock,
		"BotUpdateClient: bot library used before being setup\n");
	assert_non_null(entry);
	assert_int_equal(entry->type, PRT_ERROR);
	assert_string_equal(entry->message,
		"BotUpdateClient: bot library used before being setup\n");

	Mock_ClearPrints(&context->mock);
	status = context->api->BotUpdateEntity(0, NULL);
	assert_int_equal(status, BLERR_LIBRARYNOTSETUP);
	entry = Mock_FindPrintEntry(&context->mock,
		"BotUpdateEntity: bot library used before being setup\n");
	assert_non_null(entry);
	assert_int_equal(entry->type, PRT_ERROR);
	assert_string_equal(entry->message,
		"BotUpdateEntity: bot library used before being setup\n");

	Mock_ClearPrints(&context->mock);
	status = context->api->BotAddSound(origin, 0, 0, 0, 1.0f, 1.0f, 0.0f);
	assert_int_equal(status, BLERR_LIBRARYNOTSETUP);
	entry = Mock_FindPrintEntry(&context->mock,
		"BotUpdateSound: bot library used before being setup\n");
	assert_non_null(entry);
	assert_int_equal(entry->type, PRT_ERROR);
	assert_string_equal(entry->message,
		"BotUpdateSound: bot library used before being setup\n");

	Mock_ClearPrints(&context->mock);
	status = context->api->BotAddPointLight(origin,
		0,
		64.0f,
		1.0f,
		1.0f,
		1.0f,
		0.0f,
		0.0f);
	assert_int_equal(status, BLERR_LIBRARYNOTSETUP);
	entry = Mock_FindPrintEntry(&context->mock,
		"BotAddPointLight: bot library used before being setup\n");
	assert_non_null(entry);
	assert_int_equal(entry->type, PRT_ERROR);
	assert_string_equal(entry->message,
		"BotAddPointLight: bot library used before being setup\n");

	Mock_ClearPrints(&context->mock);
	status = context->api->BotAI(0, 0.1f);
	assert_int_equal(status, BLERR_LIBRARYNOTSETUP);
	entry = Mock_FindPrintEntry(&context->mock,
		"BotAI: bot library used before being setup\n");
	assert_non_null(entry);
	assert_int_equal(entry->type, PRT_ERROR);
	assert_string_equal(entry->message,
		"BotAI: bot library used before being setup\n");

	Mock_ClearPrints(&context->mock);
	status = context->api->BotConsoleMessage(0, CMS_CHAT, "guarded");
	assert_int_equal(status, BLERR_LIBRARYNOTSETUP);
	entry = Mock_FindPrintEntry(&context->mock,
		"BotConsoleMessage: bot library used before being setup\n");
	assert_non_null(entry);
	assert_int_equal(entry->type, PRT_ERROR);
	assert_string_equal(entry->message,
		"BotConsoleMessage: bot library used before being setup\n");
}

/*
=============
test_bot_client_settings_requires_library

Verifies BotClientSettings refuses to run before setup.
=============
*/
static void test_bot_client_settings_requires_library(void **state)
{
	bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;

	Mock_Reset(&context->mock);
	Mock_ClearPrints(&context->mock);

	bot_clientsettings_t settings;
	memset(&settings, 0x7f, sizeof(settings));

	int status = context->api->BotClientSettings(1, &settings);
	assert_int_equal(status, BLERR_LIBRARYNOTSETUP);
	Mock_AssertPrintContains(&context->mock,
	                         "BotClientSettings: bot library used before being setup\n",
	                         PRT_ERROR);
}

/*
=============
test_bot_settings_requires_library

Verifies BotSettings enforces the library guard before use.
=============
*/
static void test_bot_settings_requires_library(void **state)
{
	bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;

	Mock_Reset(&context->mock);
	Mock_ClearPrints(&context->mock);

	bot_settings_t settings;
	memset(&settings, 0x7f, sizeof(settings));

	int status = context->api->BotSettings(1, &settings);
	assert_int_equal(status, BLERR_LIBRARYNOTSETUP);
	Mock_AssertPrintContains(&context->mock,
	                         "BotSettings: bot library used before being setup\n",
	                         PRT_ERROR);
}

/*
=============
test_bot_client_settings_store_slot_mirrors

Pins BotClientSettings as the retail game-to-botlib presentation setter.
=============
*/
static void test_bot_client_settings_store_slot_mirrors(void **state)
{
	bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;

	Mock_Reset(&context->mock);

	int status = context->api->BotSetupLibrary();
	assert_int_equal(status, BLERR_NOERROR);

	bot_clientsettings_t live_settings;
	memset(&live_settings, 0, sizeof(live_settings));
	snprintf(live_settings.netname, sizeof(live_settings.netname), "PreSetup Babe");
	snprintf(live_settings.skin, sizeof(live_settings.skin), "female/athena");

	status = context->api->BotClientSettings(MAX_CLIENTS, &live_settings);
	assert_int_equal(status, BLERR_INVALIDCLIENTNUMBER);
	Mock_AssertPrintContains(&context->mock, "BotClientSettings: invalid client", PRT_ERROR);

	Mock_ClearPrints(&context->mock);
	status = context->api->BotClientSettings(1, NULL);
	assert_int_equal(status, BLERR_INVALIDIMPORT);
	Mock_AssertPrintContains(&context->mock, "BotClientSettings: NULL output buffer", PRT_ERROR);

	Mock_ClearPrints(&context->mock);
	status = context->api->BotClientSettings(1, &live_settings);
	assert_int_equal(status, BLERR_NOERROR);

	const bot_clientsettings_t *stored_settings = BotState_ClientSettings(1);
	assert_non_null(stored_settings);
	assert_string_equal(stored_settings->netname, "PreSetup Babe");
	assert_string_equal(stored_settings->skin, "female/athena");
	assert_string_equal(BotState_ClientName(1), "PreSetup Babe");
	assert_string_equal(BotState_ClientSkin(1), "female/athena");
	assert_int_equal(BotState_FindClientByName("PreSetup Babe"), 1);
	assert_string_equal(BotState_ClientName(MAX_CLIENTS), "");
	assert_string_equal(BotState_ClientSkin(MAX_CLIENTS), "");

	bot_settings_t setup_settings;
	memset(&setup_settings, 0, sizeof(setup_settings));
	snprintf(setup_settings.characterfile, sizeof(setup_settings.characterfile), "bots/babe_c.c");
	snprintf(setup_settings.charactername, sizeof(setup_settings.charactername), "babe");

	status = context->api->BotSetupClient(1, &setup_settings);
	assert_true(status);
	assert_int_equal(BotState_ActiveClientCount(), 1);

	bot_client_state_t *client_state = BotState_Get(1);
	assert_non_null(client_state);
	assert_string_equal(client_state->client_settings.netname, "PreSetup Babe");
	assert_string_equal(client_state->client_settings.skin, "female/athena");

	memset(&live_settings, 0, sizeof(live_settings));
	snprintf(live_settings.netname, sizeof(live_settings.netname), "Live Babe");
	snprintf(live_settings.skin, sizeof(live_settings.skin), "female/venus");

	status = context->api->BotClientSettings(1, &live_settings);
	assert_int_equal(status, BLERR_NOERROR);
	assert_string_equal(stored_settings->netname, "Live Babe");
	assert_string_equal(stored_settings->skin, "female/venus");
	assert_string_equal(client_state->client_settings.netname, "Live Babe");
	assert_string_equal(client_state->client_settings.skin, "female/venus");
	assert_string_equal(BotState_ClientName(1), "Live Babe");
	assert_string_equal(BotState_ClientSkin(1), "female/venus");
	assert_int_equal(BotState_FindClientByName("Live Babe"), 1);

	status = context->api->BotShutdownClient(1);
	assert_int_equal(status, BLERR_NOERROR);
	assert_int_equal(BotState_ActiveClientCount(), 0);
}

/*
=============
test_client_validator_exact_contract

Pins each client wrapper to the shared inclusive range and exact retail text.
=============
*/
static void test_client_validator_exact_contract(void **state)
{
	bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;
	bot_settings_t bot_settings = {0};

	Mock_Reset(&context->mock);
	int status = context->api->BotLibVarSet("maxclients", "2");
	assert_int_equal(status, BLERR_NOERROR);
	status = context->api->BotSetupLibrary();
	assert_int_equal(status, BLERR_NOERROR);

	Mock_ClearPrints(&context->mock);
	status = context->api->BotSetupClient(3, &bot_settings);
	assert_false(status);
	Mock_AssertSinglePrint(&context->mock,
		PRT_ERROR,
		"BotSetupClient: invalid client number 3, [0, 2]\n");

	Mock_ClearPrints(&context->mock);
	status = context->api->BotShutdownClient(-1);
	assert_int_equal(status, BLERR_INVALIDCLIENTNUMBER);
	Mock_AssertSinglePrint(&context->mock,
		PRT_ERROR,
		"BotShutdownClient: invalid client number -1, [0, 2]\n");

	Mock_ClearPrints(&context->mock);
	status = context->api->BotMoveClient(-1, 0);
	assert_int_equal(status, BLERR_INVALIDCLIENTNUMBER);
	Mock_AssertSinglePrint(&context->mock,
		PRT_ERROR,
		"BotMoveClient, parm0: invalid client number -1, [0, 2]\n");

	Mock_ClearPrints(&context->mock);
	status = context->api->BotMoveClient(0, 3);
	assert_int_equal(status, BLERR_INVALIDCLIENTNUMBER);
	Mock_AssertSinglePrint(&context->mock,
		PRT_ERROR,
		"BotMoveClient, parm1: invalid client number 3, [0, 2]\n");

	Mock_ClearPrints(&context->mock);
	status = context->api->BotClientSettings(3, NULL);
	assert_int_equal(status, BLERR_INVALIDCLIENTNUMBER);
	Mock_AssertSinglePrint(&context->mock,
		PRT_ERROR,
		"BotClientSettings: invalid client number 3, [0, 2]\n");

	Mock_ClearPrints(&context->mock);
	status = context->api->BotSettings(-1, NULL);
	assert_int_equal(status, BLERR_INVALIDCLIENTNUMBER);
	Mock_AssertSinglePrint(&context->mock,
		PRT_ERROR,
		"BotSettings: invalid client number -1, [0, 2]\n");

	Mock_ClearPrints(&context->mock);
	status = context->api->BotUpdateClient(3, NULL);
	assert_int_equal(status, BLERR_INVALIDCLIENTNUMBER);
	Mock_AssertSinglePrint(&context->mock,
		PRT_ERROR,
		"BotUpdateClient: invalid client number 3, [0, 2]\n");

	Mock_ClearPrints(&context->mock);
	status = context->api->BotAI(-1, 0.1f);
	assert_int_equal(status, BLERR_INVALIDCLIENTNUMBER);
	Mock_AssertSinglePrint(&context->mock,
		PRT_ERROR,
		"BotAI: invalid client number -1, [0, 2]\n");

	Mock_ClearPrints(&context->mock);
	status = context->api->BotConsoleMessage(3, CMS_CHAT, "message");
	assert_int_equal(status, BLERR_INVALIDCLIENTNUMBER);
	Mock_AssertSinglePrint(&context->mock,
		PRT_ERROR,
		"BotConsoleMessage: invalid client number 3, [0, 2]\n");

	status = context->api->BotShutdownLibrary();
	assert_int_equal(status, BLERR_NOERROR);
}

/*
=============
test_bot_client_capacity_uses_setup_maxclients

Pins maxclients plus the UB-safe compatibility sentinel at the retail endpoint.
=============
*/
static void test_bot_client_capacity_uses_setup_maxclients(void **state)
{
	bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;

	Mock_Reset(&context->mock);

	int status = context->api->BotLibVarSet("maxclients", "2");
	assert_int_equal(status, BLERR_NOERROR);

	status = context->api->BotSetupLibrary();
	assert_int_equal(status, BLERR_NOERROR);
	assert_int_equal(BotState_ClientCapacity(), 2);
	assert_int_equal(BotState_ActiveClientCount(), 0);

	bot_clientsettings_t live_settings;
	memset(&live_settings, 0, sizeof(live_settings));
	snprintf(live_settings.netname, sizeof(live_settings.netname), "Capacity Babe");
	snprintf(live_settings.skin, sizeof(live_settings.skin), "female/athena");

	status = context->api->BotClientSettings(1, &live_settings);
	assert_int_equal(status, BLERR_NOERROR);
	assert_string_equal(BotState_ClientName(1), "Capacity Babe");
	assert_int_equal(BotState_FindClientByName("Capacity Babe"), 1);

	snprintf(live_settings.netname, sizeof(live_settings.netname), "Sentinel Babe");
	Mock_ClearPrints(&context->mock);
	status = context->api->BotClientSettings(2, &live_settings);
	assert_int_equal(status, BLERR_NOERROR);
	assert_null(Mock_FindPrint(&context->mock, "invalid client number"));
	assert_non_null(BotState_ClientSettings(2));
	assert_string_equal(BotState_ClientName(2), "Sentinel Babe");
	assert_int_equal(BotState_FindClientByName("Capacity Babe"), 1);
	assert_int_equal(BotState_FindClientByName("Sentinel Babe"), 0);

	Mock_ClearPrints(&context->mock);
	status = context->api->BotClientSettings(3, &live_settings);
	assert_int_equal(status, BLERR_INVALIDCLIENTNUMBER);
	Mock_AssertSinglePrint(&context->mock,
		PRT_ERROR,
		"BotClientSettings: invalid client number 3, [0, 2]\n");
	assert_null(BotState_ClientSettings(3));

	bot_settings_t setup_settings;
	memset(&setup_settings, 0, sizeof(setup_settings));
	snprintf(setup_settings.characterfile, sizeof(setup_settings.characterfile), "bots/babe_c.c");
	snprintf(setup_settings.charactername, sizeof(setup_settings.charactername), "babe");

	Mock_ClearPrints(&context->mock);
	status = context->api->BotSetupClient(2, &setup_settings);
	assert_true(status);
	assert_null(Mock_FindPrint(&context->mock, "invalid client number"));
	assert_non_null(BotState_Get(2));
	assert_int_equal(BotState_ActiveClientCount(), 1);

	status = context->api->BotShutdownClient(2);
	assert_int_equal(status, BLERR_NOERROR);
	assert_int_equal(BotState_ActiveClientCount(), 0);

	status = context->api->BotSetupClient(1, &setup_settings);
	assert_true(status);
	assert_int_equal(BotState_ActiveClientCount(), 1);

	Mock_ClearPrints(&context->mock);
	status = context->api->BotMoveClient(1, 2);
	assert_int_equal(status, BLERR_NOERROR);
	assert_null(Mock_FindPrint(&context->mock, "invalid client number"));
	assert_null(BotState_Get(1));
	assert_non_null(BotState_Get(2));
	assert_int_equal(BotState_ActiveClientCount(), 1);

	status = context->api->BotShutdownClient(2);
	assert_int_equal(status, BLERR_NOERROR);
	assert_int_equal(BotState_ActiveClientCount(), 0);

	status = context->api->BotShutdownLibrary();
	assert_int_equal(status, BLERR_NOERROR);
	assert_int_equal(BotState_ClientCapacity(), 0);
}

/*
=============
test_bot_settings_updates_active_setup_block

Pins BotSettings as the active-client setup setter reconstructed from HLIL.
=============
*/
static void test_bot_settings_updates_active_setup_block(void **state)
{
	bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;

	Mock_Reset(&context->mock);

	int status = context->api->BotSetupLibrary();
	assert_int_equal(status, BLERR_NOERROR);
	assert_int_equal(BotState_ActiveClientCount(), 0);

	bot_settings_t updated_settings;
	memset(&updated_settings, 0, sizeof(updated_settings));
	snprintf(updated_settings.characterfile, sizeof(updated_settings.characterfile), "bots/babe_c.c");
	snprintf(updated_settings.charactername, sizeof(updated_settings.charactername), "babe");
	snprintf(updated_settings.ailibrary, sizeof(updated_settings.ailibrary), "gladiator.dll");

	status = context->api->BotSettings(1, &updated_settings);
	assert_int_equal(status, BLERR_SETTINGSINACTIVECLIENT);
	Mock_AssertPrintContains(&context->mock,
		"tried to update settings of inactive client\n",
		PRT_FATAL);

	Mock_ClearPrints(&context->mock);
	status = context->api->BotSettings(MAX_CLIENTS, &updated_settings);
	assert_int_equal(status, BLERR_INVALIDCLIENTNUMBER);
	Mock_AssertPrintContains(&context->mock, "BotSettings: invalid client", PRT_ERROR);

	Mock_ClearPrints(&context->mock);
	status = context->api->BotSettings(1, NULL);
	assert_int_equal(status, BLERR_SETTINGSINACTIVECLIENT);
	Mock_AssertSinglePrint(&context->mock,
		PRT_FATAL,
		"tried to update settings of inactive client\n");

	bot_settings_t setup_settings;
	memset(&setup_settings, 0, sizeof(setup_settings));
	snprintf(setup_settings.characterfile, sizeof(setup_settings.characterfile), "bots/babe_c.c");
	snprintf(setup_settings.charactername, sizeof(setup_settings.charactername), "babe");

	status = context->api->BotSetupClient(1, &setup_settings);
	assert_true(status);
	assert_int_equal(BotState_ActiveClientCount(), 1);

	bot_client_state_t *client_state = BotState_Get(1);
	assert_non_null(client_state);
	assert_string_equal(client_state->settings.ailibrary, "");

	Mock_ClearPrints(&context->mock);
	status = context->api->BotSettings(1, NULL);
	assert_int_equal(status, BLERR_INVALIDIMPORT);
	Mock_AssertSinglePrint(&context->mock,
		PRT_ERROR,
		"[bot_interface] BotSettings: NULL output buffer\n");

	status = context->api->BotSettings(1, &updated_settings);
	assert_int_equal(status, BLERR_NOERROR);
	assert_string_equal(client_state->settings.characterfile, "bots/babe_c.c");
	assert_string_equal(client_state->settings.charactername, "babe");
	assert_string_equal(client_state->settings.ailibrary, "gladiator.dll");

	status = context->api->BotShutdownClient(1);
	assert_int_equal(status, BLERR_NOERROR);
	assert_int_equal(BotState_ActiveClientCount(), 0);

	Mock_ClearPrints(&context->mock);
	status = context->api->BotSettings(1, &updated_settings);
	assert_int_equal(status, BLERR_SETTINGSINACTIVECLIENT);
	Mock_AssertPrintContains(&context->mock,
		"tried to update settings of inactive client\n",
		PRT_FATAL);
}

/*
=============
test_weight_exports_cover_guards_and_round_trip

Validates the weight export guard path, malformed filename handling, and
round-trip serialisation through BotWriteWeights using the reference contract
diagnostics.
=============
*/
static void test_weight_exports_cover_guards_and_round_trip(void **state)
{
bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;

Mock_Reset(&context->mock);

int handle = context->api->BotAllocWeightConfig();
assert_int_equal(handle, 0);

const botlib_contract_export_t *guard =
BotlibContract_FindExport(&context->catalogue, "GuardLibrarySetup");
assert_non_null(guard);
const botlib_contract_scenario_t *failure =
BotlibContract_FindScenario(guard, "failure");
assert_non_null(failure);
const botlib_contract_message_t *guard_message =
BotlibContract_FindMessageWithSeverity(failure, context->mock.prints[0].type);
assert_non_null(guard_message);

char expected_guard[1024];
snprintf(expected_guard, sizeof(expected_guard), guard_message->text, "BotAllocWeightConfig");
assert_string_equal(context->mock.prints[0].message, expected_guard);

int status = context->api->BotSetupLibrary();
assert_int_equal(status, BLERR_NOERROR);

handle = context->api->BotAllocWeightConfig();
assert_true(handle > 0);
Mock_ClearPrints(&context->mock);

status = context->api->BotLoadWeights(handle, NULL);
assert_int_equal(status, 0);
Mock_AssertPrintContains(&context->mock, "BotLoadWeights: filename required", PRT_ERROR);

Mock_ClearPrints(&context->mock);
char sample_path[PATH_MAX];
int written = snprintf(sample_path,
                       sizeof(sample_path),
                       "%s/tests/support/assets/bots/sample_weight.c",
                       PROJECT_SOURCE_DIR);
assert_true(written > 0 && (size_t)written < sizeof(sample_path));

status = context->api->BotLoadWeights(handle, sample_path);
assert_int_equal(status, 1);

assert_non_null(context->api->BotFindFuzzyWeight);
assert_non_null(context->api->BotFuzzyWeightHandle);
int single_index = context->api->BotFindFuzzyWeight(handle, "single_value");
assert_true(single_index >= 0);
assert_float_equal(context->api->BotFuzzyWeightHandle(handle, NULL, single_index),
				   42.0f,
				   0.01f);
assert_int_equal(context->api->BotFindFuzzyWeight(handle, "missing_weight"), -1);

char out_path[PATH_MAX];
const char *tmpdir = getenv("TEMP");
if (tmpdir == NULL || tmpdir[0] == '\0')
{
	tmpdir = getenv("TMP");
}
if (tmpdir == NULL || tmpdir[0] == '\0')
{
	tmpdir = PROJECT_SOURCE_DIR;
}
snprintf(out_path, sizeof(out_path), "%s/tests_weight_roundtrip.c", tmpdir);
unlink(out_path);

status = context->api->BotWriteWeights(handle, out_path);
assert_int_equal(status, 1);
Mock_AssertPrintContains(&context->mock, "written succesfully", PRT_MESSAGE);

FILE *round_trip = fopen(out_path, "rb");
assert_non_null(round_trip);

char buffer[64];
size_t read_bytes = fread(buffer, 1, sizeof(buffer), round_trip);
fclose(round_trip);
assert_true(read_bytes > 0);

context->api->BotFreeWeightConfig(handle);
unlink(out_path);
}

/*
=============
test_bot_test_export_is_retail_noop

Pins the retail Test export as a pure return-zero function with no side effects.
=============
*/
static void test_bot_test_export_is_retail_noop(void **state)
{
	bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;

	Mock_Reset(&context->mock);
	assert_false(Q2Bridge_DebugLinesEnabled());

	int status = context->api->Test(7, "debug_draw on", NULL, NULL);
	assert_int_equal(status, BLERR_NOERROR);
	assert_false(Q2Bridge_DebugLinesEnabled());
	assert_int_equal(context->mock.print_count, 0);

	status = context->api->Test(0, NULL, NULL, NULL);
	assert_int_equal(status, BLERR_NOERROR);
	assert_false(Q2Bridge_DebugLinesEnabled());
	assert_int_equal(context->mock.print_count, 0);
}

static void test_bot_load_map_and_sensory_queues(void **state)
{
    bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;

	if (!ensure_map_fixture(&context->assets, "test1"))
	{
		cmocka_skip();
	}

    Mock_Reset(&context->mock);

    LibVarSet("max_soundinfo", "64");
    LibVarSet("max_aassounds", "4");

    assert_string_equal(context->api->BotVersion(), "BotLib v0.96");

    int status = context->api->BotSetupLibrary();
    assert_int_equal(status, BLERR_NOERROR);

    assert_non_null(Mock_FindPrint(&context->mock, "------- BotLib Initialization -------"));
    assert_non_null(Mock_FindPrint(&context->mock, "BotLib v0.96"));
    assert_null(Mock_FindPrint(&context->mock, "stub invoked"));

    assert_non_null(Mock_FindPrint(&context->mock, "AAS initialized."));
    assert_true(aasworld.initialized);
    assert_true(EA_IsInitialised());
    assert_true(L_Utils_IsInitialised());
    assert_true(L_Struct_IsInitialised());
    assert_non_null(Bridge_MaxClients());

    size_t info_count = AAS_SoundSubsystem_InfoCount();
    assert_true(info_count > 0);

    int step_index = AAS_SoundSubsystem_FindInfoIndex("player/step1.wav");
    assert_true(step_index >= 0);
    const aas_soundinfo_t *step_info = AAS_SoundSubsystem_Info((size_t)step_index);
    assert_non_null(step_info);
    assert_string_equal(step_info->name, "player/step1.wav");
    assert_float_equal(step_info->volume, 80.0f, 0.01f);
    assert_int_equal(step_info->type, 1);

    char *sounds[] = {"player/step1.wav", "weapons/blastf1a.wav"};
	Mock_ClearPrints(&context->mock);
    status = context->api->BotLoadMap("maps/test1.bsp", 0, NULL, 2, sounds, 0, NULL);
    assert_int_equal(status, BLERR_NOERROR);
	assert_true(context->mock.print_count >= 2);
	assert_int_equal(context->mock.prints[0].type, PRT_MESSAGE);
	assert_string_equal(context->mock.prints[0].message,
		"------------ Map Loading ------------\n");
	assert_int_equal(context->mock.prints[context->mock.print_count - 1].type,
		PRT_MESSAGE);
	assert_string_equal(
		context->mock.prints[context->mock.print_count - 1].message,
		"-------------------------------------\n");

    vec3_t origin = {0.0f, 32.0f, 16.0f};
    status = context->api->BotAddSound(origin, 3, 1, 1, 0.5f, 0.7f, 0.0f);
    assert_int_equal(status, BLERR_NOERROR);

    assert_int_equal((int)AAS_SoundSubsystem_SoundEventCount(), 1);
    const aas_sound_event_t *event = AAS_SoundSubsystem_SoundEvent(0);
    assert_non_null(event);
    assert_int_equal(event->ent, 3);
    assert_int_equal(event->soundindex, 1);
    assert_float_equal(event->volume, 0.5f, 0.0001f);
    const aas_soundinfo_t *event_info = AAS_SoundSubsystem_InfoForSoundIndex(event->soundindex);
    assert_non_null(event_info);
    assert_string_equal(event_info->name, "weapons/blastf1a.wav");
    assert_int_equal(AAS_SoundSubsystem_SoundTypeForIndex(event->soundindex), event_info->type);

    const aas_sound_event_summary_t *sound_summaries = NULL;
    size_t summary_count = AAS_SoundSubsystem_SoundSummaries(&sound_summaries);
    assert_int_equal(summary_count, 1);
    assert_non_null(sound_summaries);
    const aas_sound_event_summary_t *summary = &sound_summaries[0];
    assert_non_null(summary);
    assert_non_null(summary->event);
    assert_ptr_equal(summary->event, event);
    assert_true(summary->has_info);
    assert_ptr_equal(summary->info, event_info);
    assert_int_equal(summary->sound_type, event_info->type);
    assert_false(summary->expired);

    status = context->api->BotAddSound(origin, 4, 2, 5, 0.3f, 1.0f, 0.1f);
    assert_int_equal(status, BLERR_INVALIDSOUNDINDEX);
    assert_int_equal((int)AAS_SoundSubsystem_SoundEventCount(), 1);

    status = context->api->BotAddPointLight(origin, 5, 128.0f, 1.0f, 0.3f, 0.2f, 0.0f, 0.25f);
    assert_int_equal(status, BLERR_NOERROR);
    assert_int_equal((int)AAS_SoundSubsystem_PointLightCount(), 1);

    const aas_pointlight_event_summary_t *light_summaries = NULL;
    size_t light_summary_count = AAS_SoundSubsystem_PointLightSummaries(&light_summaries);
    assert_int_equal(light_summary_count, 1);
    assert_non_null(light_summaries);
    const aas_pointlight_event_summary_t *light_summary = &light_summaries[0];
    assert_non_null(light_summary);
    assert_non_null(light_summary->event);
    assert_ptr_equal(light_summary->event, AAS_SoundSubsystem_PointLight(0));
    assert_true(light_summary->has_expiry);
    assert_float_equal(light_summary->expiry_time, 0.25f, 0.0001f);
    assert_false(light_summary->expired);

    bot_settings_t settings;
    memset(&settings, 0, sizeof(settings));
    snprintf(settings.characterfile, sizeof(settings.characterfile), "bots/babe_c.c");
    snprintf(settings.charactername, sizeof(settings.charactername), "babe");

    status = context->api->BotSetupClient(1, &settings);
    assert_true(status);

    bot_client_state_t *self_state = BotState_Get(1);
    assert_non_null(self_state);
    self_state->team = 1;

    bot_goal_t chase_goal;
    memset(&chase_goal, 0, sizeof(chase_goal));
    chase_goal.number = 0;
    chase_goal.areanum = 1;
    VectorSet(chase_goal.origin, 256.0f, 0.0f, 24.0f);
    status = context->api->BotPushGoal(self_state->goal_handle, &chase_goal);
    assert_true(status != 0);

    bot_client_state_t *enemy_state = BotState_Create(2);
    assert_non_null(enemy_state);
    enemy_state->active = true;
    enemy_state->team = 2;

    bot_updateclient_t self_update;
    memset(&self_update, 0, sizeof(self_update));
    VectorSet(self_update.origin, 0.0f, 0.0f, 24.0f);
    VectorSet(self_update.viewangles, 0.0f, 0.0f, 0.0f);
    self_update.stats[STAT_HEALTH] = 100;
    self_update.pm_flags = PMF_ON_GROUND;
    for (int i = 0; i < MAX_ITEMS; ++i)
    {
        self_update.inventory[i] = 1;
    }

    bot_updateclient_t enemy_update;
    memset(&enemy_update, 0, sizeof(enemy_update));
    VectorSet(enemy_update.origin, 256.0f, 0.0f, 24.0f);
    VectorSet(enemy_update.viewangles, 0.0f, 180.0f, 0.0f);
    enemy_update.stats[STAT_HEALTH] = 100;
    enemy_state->last_client_update = enemy_update;
    enemy_state->client_update_valid = true;

    bot_updateentity_t enemy_entity;
    memset(&enemy_entity, 0, sizeof(enemy_entity));
    VectorCopy(enemy_update.origin, enemy_entity.origin);
    VectorCopy(enemy_update.origin, enemy_entity.old_origin);

    status = context->api->BotStartFrame(0.2f);
    assert_int_equal(status, BLERR_NOERROR);

    status = context->api->BotUpdateClient(1, &self_update);
    assert_int_equal(status, BLERR_NOERROR);

    status = context->api->BotUpdateEntity(2, &enemy_entity);
    assert_int_equal(status, BLERR_NOERROR);

    status = context->api->BotAI(1, 0.05f);
    assert_int_equal(status, BLERR_NOERROR);

    assert_true(context->mock.bot_input_count > 0);
    const bot_input_t *final_input = &context->mock.inputs[context->mock.bot_input_count - 1];
    assert_float_equal(final_input->thinktime, 0.05f, 0.0001f);
    assert_true(final_input->dir[0] > 0.99f);
    assert_float_equal(final_input->dir[1], 0.0f, 0.0001f);
    assert_float_equal(final_input->dir[2], 0.0f, 0.0001f);
    assert_true(final_input->speed >= 256.0f);
    assert_true(final_input->speed <= 400.0f);
    assert_int_equal(final_input->actionflags, 0);

    assert_int_equal(self_state->combat.current_enemy, 2);
    assert_true(self_state->combat.enemy_visible);
    assert_int_equal(self_state->active_goal_number, chase_goal.number);

    context->api->BotShutdownClient(1);
    BotState_Destroy(2);

    Mock_Reset(&context->mock);

    context->api->BotShutdownLibrary();

    LibVarSet("max_soundinfo", "1");
    LibVarSet("max_aassounds", "2");

    status = context->api->BotSetupLibrary();
    assert_int_equal(status, BLERR_NOERROR);
    assert_non_null(Mock_FindPrint(&context->mock, "AAS_Sound: discarding soundinfo"));

    Mock_Reset(&context->mock);

    status = context->api->BotLoadMap("maps/test1.bsp", 0, NULL, 2, sounds, 0, NULL);
    assert_int_equal(status, BLERR_NOERROR);
    assert_int_equal((int)AAS_SoundSubsystem_InfoCount(), 1);

    context->api->BotShutdownLibrary();

    Mock_Reset(&context->mock);

    LibVarSet("max_soundinfo", "0");
    LibVarSet("max_aassounds", "0");

    status = context->api->BotSetupLibrary();
    assert_int_equal(status, BLERR_NOERROR);
    assert_non_null(Mock_FindPrint(&context->mock, "AAS_Sound: max_soundinfo disabled"));

    Mock_Reset(&context->mock);

    status = context->api->BotLoadMap("maps/test1.bsp", 0, NULL, 2, sounds, 0, NULL);
    assert_int_equal(status, BLERR_NOERROR);

    status = context->api->BotAddSound(origin, 6, 2, 0, 1.0f, 1.0f, 0.0f);
    assert_int_equal(status, BLERR_NOERROR);
    assert_non_null(Mock_FindPrint(&context->mock, "BotAddSound: sound queue capacity exceeded"));

    status = context->api->BotAddPointLight(origin, 7, 64.0f, 0.1f, 0.2f, 0.3f, 0.0f, 0.5f);
    assert_int_equal(status, BLERR_NOERROR);
    assert_non_null(Mock_FindPrint(&context->mock, "BotAddPointLight: point light queue capacity exceeded"));

    context->api->BotShutdownLibrary();

	assert_null(Mock_FindPrint(&context->mock, "------- BotLib Shutdown -------"));
    assert_null(Mock_FindPrint(&context->mock, "stub invoked"));

    assert_false(aasworld.initialized);
    assert_false(EA_IsInitialised());
    assert_false(L_Utils_IsInitialised());
    assert_false(L_Struct_IsInitialised());
    assert_null(Bridge_MaxClients());
}

static void test_bot_usehook_defaults_disabled(void **state)
{
    bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;

	if (!ensure_map_fixture(&context->assets, "test1"))
	{
		cmocka_skip();
	}

    Mock_Reset(&context->mock);

    int status = context->api->BotSetupLibrary();
    assert_int_equal(status, BLERR_NOERROR);

    libvar_t *usehook = Bridge_UseHook();
    assert_non_null(usehook);
    assert_float_equal(usehook->value, 0.0f, 0.0001f);
    assert_string_equal(usehook->string, "0");

    char *sounds[] = {"player/step1.wav"};
    status = context->api->BotLoadMap("maps/test1.bsp", 0, NULL, 1, sounds, 0, NULL);
    assert_int_equal(status, BLERR_NOERROR);

    bot_settings_t settings;
    memset(&settings, 0, sizeof(settings));
    snprintf(settings.characterfile, sizeof(settings.characterfile), "bots/babe_c.c");
    snprintf(settings.charactername, sizeof(settings.charactername), "babe");

    status = context->api->BotSetupClient(1, &settings);
    assert_true(status);

    bot_client_state_t *self_state = BotState_Get(1);
    assert_non_null(self_state);
    self_state->team = 1;

    bot_goal_t chase_goal;
    memset(&chase_goal, 0, sizeof(chase_goal));
    chase_goal.number = 0;
    chase_goal.areanum = 1;
    VectorSet(chase_goal.origin, 1024.0f, 0.0f, 24.0f);
    status = context->api->BotPushGoal(self_state->goal_handle, &chase_goal);
    assert_true(status != 0);

    bot_client_state_t *enemy_state = BotState_Create(2);
    assert_non_null(enemy_state);
    enemy_state->active = true;
    enemy_state->team = 2;

    bot_updateclient_t self_update;
    memset(&self_update, 0, sizeof(self_update));
    VectorSet(self_update.origin, 0.0f, 0.0f, 24.0f);
    VectorSet(self_update.viewangles, 0.0f, 0.0f, 0.0f);
    self_update.stats[STAT_HEALTH] = 100;
    self_update.pm_flags = PMF_ON_GROUND;
    for (int i = 0; i < MAX_ITEMS; ++i)
    {
        self_update.inventory[i] = 1;
    }

    bot_updateclient_t enemy_update;
    memset(&enemy_update, 0, sizeof(enemy_update));
    VectorSet(enemy_update.origin, 1024.0f, 0.0f, 24.0f);
    VectorSet(enemy_update.viewangles, 0.0f, 180.0f, 0.0f);
    enemy_update.stats[STAT_HEALTH] = 100;
    enemy_state->last_client_update = enemy_update;
    enemy_state->client_update_valid = true;

    bot_updateentity_t enemy_entity;
    memset(&enemy_entity, 0, sizeof(enemy_entity));
    VectorCopy(enemy_update.origin, enemy_entity.origin);
    VectorCopy(enemy_update.origin, enemy_entity.old_origin);

    status = context->api->BotStartFrame(0.2f);
    assert_int_equal(status, BLERR_NOERROR);

    status = context->api->BotUpdateClient(1, &self_update);
    assert_int_equal(status, BLERR_NOERROR);

    status = context->api->BotUpdateEntity(2, &enemy_entity);
    assert_int_equal(status, BLERR_NOERROR);

    status = context->api->BotAI(1, 0.05f);
    assert_int_equal(status, BLERR_NOERROR);

    assert_true(context->mock.bot_input_count > 0);
    const bot_input_t *final_input = &context->mock.inputs[context->mock.bot_input_count - 1];
    assert_float_equal(final_input->speed, 200.0f, 0.0001f);

    context->api->BotShutdownClient(1);
    BotState_Destroy(2);

    context->api->BotShutdownLibrary();
}

static void test_bot_console_message_and_ai_pipeline(void **state)
{
    bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;

    Mock_Reset(&context->mock);

    int status = context->api->BotSetupLibrary();
    assert_int_equal(status, BLERR_NOERROR);

    bot_settings_t settings;
    memset(&settings, 0, sizeof(settings));
    snprintf(settings.characterfile, sizeof(settings.characterfile), "bots/babe_c.c");
    snprintf(settings.charactername, sizeof(settings.charactername), "babe");

    status = context->api->BotSetupClient(1, &settings);
    assert_true(status);

    bot_client_state_t *client_state = BotState_Get(1);
    assert_non_null(client_state);
    assert_non_null(client_state->chat_state);

    status = context->api->BotConsoleMessage(1, CMS_CHAT, "hello gladiator");
    assert_int_equal(status, BLERR_NOERROR);

    assert_int_equal(BotNumConsoleMessages(client_state->chat_state), 1);

    context->api->BotStartFrame(0.1f);

    bot_updateclient_t update;
    memset(&update, 0, sizeof(update));
    update.viewangles[1] = 45.0f;

    status = context->api->BotUpdateClient(1, &update);
    assert_int_equal(status, BLERR_NOERROR);

    status = context->api->BotAI(1, 0.05f);
    assert_int_equal(status, BLERR_NOERROR);
    assert_int_equal(context->mock.bot_input_count, 1);
    assert_int_equal(context->mock.input_clients[0], 1);
    assert_float_equal(context->mock.inputs[0].thinktime, 0.05f, 0.0001f);
    assert_float_equal(context->mock.inputs[0].viewangles[1], 45.0f, 0.0001f);
	assert_int_equal(BotNumConsoleMessages(client_state->chat_state), 1);

    status = context->api->BotAI(1, 0.05f);
    assert_int_equal(status, BLERR_AIUPDATEINACTIVECLIENT);

	status = context->api->BotStartFrame(3.0f);
	assert_int_equal(status, BLERR_NOERROR);
	status = context->api->BotUpdateClient(1, &update);
	assert_int_equal(status, BLERR_NOERROR);
	status = context->api->BotAI(1, 0.05f);
	assert_int_equal(status, BLERR_NOERROR);
	assert_int_equal(BotNumConsoleMessages(client_state->chat_state), 0);

    context->api->BotShutdownClient(1);
    context->api->BotShutdownLibrary();
}

/*
=============
test_bot_console_message_flood_preserves_deferred_head

Verifies the tenth message bypasses the age delay, self chat is removed, and
the now-nine-message queue stops on its next recent chat head.
=============
*/
static void test_bot_console_message_flood_preserves_deferred_head(void **state)
{
	bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;
	Mock_Reset(&context->mock);

	int status = context->api->BotSetupLibrary();
	assert_int_equal(status, BLERR_NOERROR);

	bot_settings_t settings;
	memset(&settings, 0, sizeof(settings));
	snprintf(settings.characterfile, sizeof(settings.characterfile), "bots/babe_c.c");
	snprintf(settings.charactername, sizeof(settings.charactername), "babe");
	assert_true(context->api->BotSetupClient(1, &settings));

	bot_clientsettings_t presentation;
	memset(&presentation, 0, sizeof(presentation));
	snprintf(presentation.netname, sizeof(presentation.netname), "Babe");
	assert_int_equal(context->api->BotClientSettings(1, &presentation), BLERR_NOERROR);

	assert_int_equal(context->api->BotConsoleMessage(1,
		CMS_CHAT,
		"(Babe): ignored"),
		BLERR_NOERROR);
	assert_int_equal(context->api->BotConsoleMessage(1,
		CMS_CHAT,
		"Alice: abnormal"),
		BLERR_NOERROR);
	for (int index = 0; index < 8; ++index)
	{
		char message[32];
		snprintf(message, sizeof(message), "normal %d", index);
		assert_int_equal(context->api->BotConsoleMessage(1,
			CMS_NORMAL,
			message),
			BLERR_NOERROR);
	}

	bot_client_state_t *client_state = BotState_Get(1);
	assert_non_null(client_state);
	assert_int_equal(BotNumConsoleMessages(client_state->chat_state), 10);

	assert_int_equal(context->api->BotStartFrame(0.1f), BLERR_NOERROR);
	bot_updateclient_t update;
	memset(&update, 0, sizeof(update));
	assert_int_equal(context->api->BotUpdateClient(1, &update), BLERR_NOERROR);
	assert_int_equal(context->api->BotAI(1, 0.05f), BLERR_NOERROR);

	assert_int_equal(BotNumConsoleMessages(client_state->chat_state), 9);
	const bot_console_message_node_t *head = BotNextConsoleMessageNode(
		client_state->chat_state);
	assert_non_null(head);
	assert_int_equal(head->type, CMS_CHAT);
	assert_string_equal(head->message, "Alice: abnormal");

	assert_int_equal(context->api->BotLibVarSet("nochat", "1"), BLERR_NOERROR);
	assert_int_equal(context->api->BotStartFrame(3.0f), BLERR_NOERROR);
	assert_int_equal(context->api->BotUpdateClient(1, &update), BLERR_NOERROR);
	assert_int_equal(context->api->BotAI(1, 0.05f), BLERR_NOERROR);
	assert_int_equal(BotNumConsoleMessages(client_state->chat_state), 0);
	assert_false(client_state->chat_standing);

	context->api->BotShutdownClient(1);
	context->api->BotShutdownLibrary();
}

/*
=============
test_bot_console_message_classifies_death_updates

Exercises the retail context-seven BotFindMatch handoff and its directly
representable self/enemy death subtype state updates.
=============
*/
static void test_bot_console_message_classifies_death_updates(void **state)
{
	bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;
	Mock_Reset(&context->mock);

	int status = context->api->BotSetupLibrary();
	assert_int_equal(status, BLERR_NOERROR);

	bot_settings_t settings;
	memset(&settings, 0, sizeof(settings));
	snprintf(settings.characterfile, sizeof(settings.characterfile), "bots/babe_c.c");
	snprintf(settings.charactername, sizeof(settings.charactername), "babe");
	assert_true(context->api->BotSetupClient(1, &settings));

	bot_clientsettings_t presentation;
	memset(&presentation, 0, sizeof(presentation));
	snprintf(presentation.netname, sizeof(presentation.netname), "Babe");
	assert_int_equal(context->api->BotClientSettings(1, &presentation), BLERR_NOERROR);
	memset(&presentation, 0, sizeof(presentation));
	snprintf(presentation.netname, sizeof(presentation.netname), "Enemy");
	assert_int_equal(context->api->BotClientSettings(2, &presentation), BLERR_NOERROR);

	bot_client_state_t *client_state = BotState_Get(1);
	assert_non_null(client_state);
	client_state->combat.current_enemy = 2;

	assert_int_equal(context->api->BotConsoleMessage(1,
		CMS_NORMAL,
		"Babe was railed by Alice"),
		BLERR_NOERROR);
	assert_int_equal(context->api->BotConsoleMessage(1,
		CMS_NORMAL,
		"Enemy was railed by Alice"),
		BLERR_NOERROR);

	assert_int_equal(context->api->BotStartFrame(0.1f), BLERR_NOERROR);
	bot_updateclient_t update;
	memset(&update, 0, sizeof(update));
	assert_int_equal(context->api->BotUpdateClient(1, &update), BLERR_NOERROR);
	assert_int_equal(context->api->BotAI(1, 0.05f), BLERR_NOERROR);

	assert_int_equal(client_state->bot_death_type, 11);
	assert_int_equal(client_state->enemy_death_type, 11);
	assert_int_equal(BotNumConsoleMessages(client_state->chat_state), 0);

	context->api->BotShutdownClient(1);
	context->api->BotShutdownLibrary();
}

/*
=============
test_bot_console_message_tracks_team_leadership

Pins the retail teamplay gate, exact-then-substring client lookup, paired
leadership state changes, and Gladiator's ST_I start-variable behavior.
=============
*/
static void test_bot_console_message_tracks_team_leadership(void **state)
{
	bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;
	Mock_Reset(&context->mock);

	int status = context->api->BotSetupLibrary();
	assert_int_equal(status, BLERR_NOERROR);

	bot_settings_t settings;
	memset(&settings, 0, sizeof(settings));
	snprintf(settings.characterfile, sizeof(settings.characterfile), "bots/babe_c.c");
	snprintf(settings.charactername, sizeof(settings.charactername), "babe");
	assert_true(context->api->BotSetupClient(1, &settings));

	bot_clientsettings_t presentation;
	memset(&presentation, 0, sizeof(presentation));
	snprintf(presentation.netname, sizeof(presentation.netname), "Babe");
	assert_int_equal(context->api->BotClientSettings(1, &presentation), BLERR_NOERROR);
	memset(&presentation, 0, sizeof(presentation));
	snprintf(presentation.netname, sizeof(presentation.netname), "Leader Alice");
	assert_int_equal(context->api->BotClientSettings(2, &presentation), BLERR_NOERROR);

	bot_client_state_t *client_state = BotState_Get(1);
	assert_non_null(client_state);
	assert_string_equal(client_state->team_leader, "");

	bot_updateclient_t update;
	memset(&update, 0, sizeof(update));
	assert_int_equal(context->api->BotConsoleMessage(1,
		CMS_CHAT,
		"(Commander): Alice is the team leader"),
		BLERR_NOERROR);
	assert_int_equal(context->api->BotStartFrame(3.0f), BLERR_NOERROR);
	assert_int_equal(context->api->BotUpdateClient(1, &update), BLERR_NOERROR);
	assert_int_equal(context->api->BotAI(1, 0.05f), BLERR_NOERROR);
	assert_string_equal(client_state->team_leader, "");

	assert_int_equal(context->api->BotLibVarSet("dmflags", "64"), BLERR_NOERROR);
	assert_int_equal(context->api->BotConsoleMessage(1,
		CMS_CHAT,
		"(Commander): Alice is the team leader"),
		BLERR_NOERROR);
	assert_int_equal(context->api->BotStartFrame(6.0f), BLERR_NOERROR);
	assert_int_equal(context->api->BotUpdateClient(1, &update), BLERR_NOERROR);
	assert_int_equal(context->api->BotAI(1, 0.05f), BLERR_NOERROR);
	assert_string_equal(client_state->team_leader, "Leader Alice");

	assert_int_equal(context->api->BotConsoleMessage(1,
		CMS_CHAT,
		"(Leader Alice): I am the team leader"),
		BLERR_NOERROR);
	assert_int_equal(context->api->BotStartFrame(9.0f), BLERR_NOERROR);
	assert_int_equal(context->api->BotUpdateClient(1, &update), BLERR_NOERROR);
	assert_int_equal(context->api->BotAI(1, 0.05f), BLERR_NOERROR);
	assert_string_equal(client_state->team_leader, "");

	assert_int_equal(context->api->BotConsoleMessage(1,
		CMS_CHAT,
		"(Commander): Alice is the team leader"),
		BLERR_NOERROR);
	assert_int_equal(context->api->BotStartFrame(12.0f), BLERR_NOERROR);
	assert_int_equal(context->api->BotUpdateClient(1, &update), BLERR_NOERROR);
	assert_int_equal(context->api->BotAI(1, 0.05f), BLERR_NOERROR);
	assert_string_equal(client_state->team_leader, "Leader Alice");

	assert_int_equal(context->api->BotConsoleMessage(1,
		CMS_CHAT,
		"(Leader Alice): I will not be the team leader"),
		BLERR_NOERROR);
	assert_int_equal(context->api->BotStartFrame(15.0f), BLERR_NOERROR);
	assert_int_equal(context->api->BotUpdateClient(1, &update), BLERR_NOERROR);
	assert_int_equal(context->api->BotAI(1, 0.05f), BLERR_NOERROR);
	assert_string_equal(client_state->team_leader, "");
	assert_int_equal(BotNumConsoleMessages(client_state->chat_state), 0);

	context->api->BotShutdownClient(1);
	context->api->BotShutdownLibrary();
}

/*
=============
test_bot_console_wait_reports_retail_unknown_match

Verifies Gladiator's explicit MSG_WAIT dispatcher diagnostic and consumption
behavior even when team command execution is otherwise disabled.
=============
*/
static void test_bot_console_wait_reports_retail_unknown_match(void **state)
{
	bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;
	Mock_Reset(&context->mock);

	int status = context->api->BotSetupLibrary();
	assert_int_equal(status, BLERR_NOERROR);

	bot_settings_t settings;
	memset(&settings, 0, sizeof(settings));
	snprintf(settings.characterfile, sizeof(settings.characterfile), "bots/babe_c.c");
	snprintf(settings.charactername, sizeof(settings.charactername), "babe");
	assert_true(context->api->BotSetupClient(1, &settings));

	bot_clientsettings_t presentation;
	memset(&presentation, 0, sizeof(presentation));
	snprintf(presentation.netname, sizeof(presentation.netname), "Babe");
	assert_int_equal(context->api->BotClientSettings(1, &presentation), BLERR_NOERROR);

	assert_int_equal(context->api->BotConsoleMessage(1,
		CMS_CHAT,
		"(Commander): Babe wait for me"),
		BLERR_NOERROR);
	Mock_ClearPrints(&context->mock);
	assert_int_equal(context->api->BotStartFrame(3.0f), BLERR_NOERROR);
	bot_updateclient_t update;
	memset(&update, 0, sizeof(update));
	assert_int_equal(context->api->BotUpdateClient(1, &update), BLERR_NOERROR);
	Mock_ClearPrints(&context->mock);
	assert_int_equal(context->api->BotAI(1, 0.05f), BLERR_NOERROR);
	size_t diagnostic_count = 0;
	for (size_t index = 0; index < context->mock.print_count; ++index)
	{
		if (context->mock.prints[index].type == PRT_MESSAGE &&
			strcmp(context->mock.prints[index].message,
				"unknown match type\n") == 0)
		{
			++diagnostic_count;
		}
	}
	assert_int_equal(diagnostic_count, 1);

	bot_client_state_t *client_state = BotState_Get(1);
	assert_non_null(client_state);
	assert_int_equal(BotNumConsoleMessages(client_state->chat_state), 0);

	context->api->BotShutdownClient(1);
	context->api->BotShutdownLibrary();
}

/*
=============
setup_console_team_command_fixture

Creates the named bot and teammate state required by the retail addressed
team-command gate.
=============
*/
static bot_client_state_t *setup_console_team_command_fixture(
	bot_interface_test_context_t *context,
	bot_client_state_t **teammate_out)
{
	Mock_Reset(&context->mock);
	assert_int_equal(context->api->BotSetupLibrary(), BLERR_NOERROR);

	bot_settings_t settings;
	memset(&settings, 0, sizeof(settings));
	snprintf(settings.characterfile, sizeof(settings.characterfile), "bots/babe_c.c");
	snprintf(settings.charactername, sizeof(settings.charactername), "babe");
	assert_true(context->api->BotSetupClient(1, &settings));

	bot_clientsettings_t presentation;
	memset(&presentation, 0, sizeof(presentation));
	snprintf(presentation.netname, sizeof(presentation.netname), "Babe");
	snprintf(presentation.skin, sizeof(presentation.skin), "female/athena");
	assert_int_equal(context->api->BotClientSettings(1, &presentation),
		BLERR_NOERROR);
	memset(&presentation, 0, sizeof(presentation));
	snprintf(presentation.netname, sizeof(presentation.netname), "Commander");
	snprintf(presentation.skin, sizeof(presentation.skin), "male/athena");
	assert_int_equal(context->api->BotClientSettings(2, &presentation),
		BLERR_NOERROR);

	bot_client_state_t *bot = BotState_Get(1);
	assert_non_null(bot);
	bot->team = 1;
	bot_client_state_t *teammate = BotState_Create(2);
	assert_non_null(teammate);
	teammate->team = 1;
	BotState_SetActive(teammate, true);
	if (teammate_out != NULL)
	{
		*teammate_out = teammate;
	}
	return bot;
}

/*
=============
process_console_team_command

Queues one aged chat message and advances a complete client AI frame.
=============
*/
static void process_console_team_command(bot_interface_test_context_t *context,
	float time,
	const char *message)
{
	assert_int_equal(context->api->BotConsoleMessage(1,
		CMS_CHAT,
		(char *)message),
		BLERR_NOERROR);
	assert_int_equal(context->api->BotStartFrame(time), BLERR_NOERROR);
	bot_updateclient_t update;
	memset(&update, 0, sizeof(update));
	update.pm_type = PM_NORMAL;
	update.pm_flags = PMF_ON_GROUND;
	assert_int_equal(context->api->BotUpdateClient(1, &update), BLERR_NOERROR);
	assert_int_equal(context->api->BotAI(1, 0.05f), BLERR_NOERROR);
}

/*
=============
count_team_chat_commands

Counts both direct retail EA_SayTeam calls and constructed team chat commands.
=============
*/
static size_t count_team_chat_commands(const mock_bot_import_t *mock)
{
	size_t count = 0U;
	for (size_t index = 0; index < mock->client_command_count; ++index)
	{
		const char *command = mock->client_commands[index].command;
		if (strcmp(command, "say_team") == 0 ||
			strncmp(command, "say_team ", 9U) == 0)
		{
			++count;
		}
	}
	return count;
}

/*
=============
find_constructed_team_chat

Finds a constructed team chat containing the supplied replacement text.
=============
*/
static bool find_constructed_team_chat(const mock_bot_import_t *mock,
	const char *text)
{
	for (size_t index = 0; index < mock->client_command_count; ++index)
	{
		const char *command = mock->client_commands[index].command;
		if (strncmp(command, "say_team ", 9U) == 0 &&
			strstr(command, text) != NULL)
		{
			return true;
		}
	}
	return false;
}

/*
=============
latest_constructed_team_chat

Returns the newest constructed team-chat command for branch-specific status
assertions.
=============
*/
static const char *latest_constructed_team_chat(const mock_bot_import_t *mock)
{
	if (mock == NULL)
	{
		return NULL;
	}

	for (size_t index = mock->client_command_count; index > 0U; --index)
	{
		const char *command = mock->client_commands[index - 1U].command;
		if (strncmp(command, "say_team ", 9U) == 0)
		{
			return command;
		}
	}
	return NULL;
}

/*
=============
setup_console_command_aas_world

Installs one bounded reachable area for camp/checkpoint/patrol command tests.
The normal library shutdown remains the sole owner of these allocations.
=============
*/
static void setup_console_command_aas_world(void)
{
	assert_null(aasworld.areas);
	assert_null(aasworld.areasettings);
	aasworld.loaded = qtrue;
	aasworld.initialized = qtrue;
	aasworld.numAreas = 2;
	aasworld.numAreaSettings = 2;
	aasworld.areas = calloc((size_t)aasworld.numAreas, sizeof(*aasworld.areas));
	aasworld.areasettings = calloc((size_t)aasworld.numAreaSettings,
		sizeof(*aasworld.areasettings));
	assert_non_null(aasworld.areas);
	assert_non_null(aasworld.areasettings);
	VectorSet(aasworld.areas[1].mins, -128.0f, -128.0f, -64.0f);
	VectorSet(aasworld.areas[1].maxs, 128.0f, 128.0f, 128.0f);
	aasworld.areasettings[1].presencetype =
		PRESENCE_NORMAL | PRESENCE_CROUCH;
	aasworld.areasettings[1].numreachableareas = 1;
	TranslateEntity_SetWorldLoaded(qtrue);
}

/*
=============
register_console_command_goal

Registers a named static item goal inside the synthetic command-test area.
=============
*/
static void register_console_command_goal(bot_interface_test_context_t *context,
	const char *classname,
	int number,
	float x)
{
	bot_levelitem_setup_t setup;
	memset(&setup, 0, sizeof(setup));
	setup.classname = classname;
	VectorSet(setup.goal.origin, x, 0.0f, 32.0f);
	setup.goal.areanum = 1;
	VectorSet(setup.goal.mins, -8.0f, -8.0f, -8.0f);
	VectorSet(setup.goal.maxs, 8.0f, 8.0f, 8.0f);
	setup.goal.entitynum = number;
	setup.goal.number = number;
	setup.goal.flags = GFL_ITEM;
	setup.flags = GFL_ITEM;
	assert_int_equal(context->api->BotRegisterLevelItem(&setup), number);
}

/*
=============
find_direct_team_chat

Finds an exact direct EA_SayTeam payload in the captured import commands.
=============
*/
static bool find_direct_team_chat(const mock_bot_import_t *mock,
	const char *text)
{
	if (mock == NULL || text == NULL)
	{
		return false;
	}

	for (size_t index = 0; index < mock->client_command_count; ++index)
	{
		if (strcmp(mock->client_commands[index].command, "say_team") == 0 &&
			strcmp(mock->client_commands[index].argument, text) == 0)
		{
			return true;
		}
	}
	return false;
}

/*
=============
test_bot_console_what_are_you_doing_reports_every_ltg_type

Pins case 11's LTG 1..7 chat-type map, zero-based teammate conversion, retail
easy-name cleanup, goal/item-name source, and immediate team destination.
=============
*/
static void test_bot_console_what_are_you_doing_reports_every_ltg_type(
	void **state)
{
	bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;
	bot_client_state_t *bot = setup_console_team_command_fixture(context, NULL);
	assert_int_equal(context->api->BotLibVarSet("nochat", "1"), BLERR_NOERROR);
	assert_float_equal(LibVarGetValue("teamplay"), 0.0f, 0.0001f);

	bot_clientsettings_t target_presentation;
	memset(&target_presentation, 0, sizeof(target_presentation));
	snprintf(target_presentation.netname,
		sizeof(target_presentation.netname),
		"[X]Mr Foo_Bar!");
	assert_int_equal(context->api->BotClientSettings(3, &target_presentation),
		BLERR_NOERROR);

	bot_levelitem_setup_t goal_setup;
	memset(&goal_setup, 0, sizeof(goal_setup));
	goal_setup.classname = "weapon_rocketlauncher";
	goal_setup.goal.number = 302;
	goal_setup.goal.entitynum = 302;
	goal_setup.goal.flags = GFL_ITEM;
	goal_setup.flags = GFL_ITEM;
	assert_int_equal(context->api->BotRegisterLevelItem(&goal_setup), 302);

	char goal_name[64];
	context->api->BotGoalName(302, goal_name, (int)sizeof(goal_name));
	assert_string_equal(goal_name, "Rocket Launcher");

	size_t expected_team_chats = 0U;
	const char *reply;
	BotState_SetLongTermGoal(bot, 1, 3, 0);
	assert_int_equal(bot->ltg_teammate, 3);
	process_console_team_command(context,
		3.0f,
		"(Commander): Babe what are you doing?");
	assert_int_equal(count_team_chat_commands(&context->mock),
		++expected_team_chats);
	reply = latest_constructed_team_chat(&context->mock);
	assert_non_null(reply);
	assert_non_null(strstr(reply, "help"));
	assert_non_null(strstr(reply, "foo_bar"));
	assert_int_equal(BotChatLength(bot->chat_state), 0);

	BotState_SetLongTermGoal(bot, 2, 3, 0);
	process_console_team_command(context,
		6.0f,
		"(Commander): Babe what are you doing?");
	assert_int_equal(count_team_chat_commands(&context->mock),
		++expected_team_chats);
	reply = latest_constructed_team_chat(&context->mock);
	assert_non_null(reply);
	assert_non_null(strstr(reply, "foo_bar"));
	assert_true(strstr(reply, "accompany") != NULL ||
		strstr(reply, "following") != NULL ||
		strstr(reply, "cover ") != NULL ||
		strstr(reply, "bodyguard") != NULL);
	assert_int_equal(BotChatLength(bot->chat_state), 0);

	BotState_SetLongTermGoal(bot, 3, -1, 302);
	process_console_team_command(context,
		9.0f,
		"(Commander): Babe what are you doing?");
	assert_int_equal(count_team_chat_commands(&context->mock),
		++expected_team_chats);
	reply = latest_constructed_team_chat(&context->mock);
	assert_non_null(reply);
	assert_non_null(strstr(reply, goal_name));
	assert_true(strstr(reply, "defending") != NULL ||
		strstr(reply, "guard") != NULL ||
		strstr(reply, "supervision") != NULL);
	assert_int_equal(BotChatLength(bot->chat_state), 0);

	BotState_SetLongTermGoal(bot, 4, -1, 0);
	process_console_team_command(context,
		12.0f,
		"(Commander): Babe what are you doing?");
	assert_int_equal(count_team_chat_commands(&context->mock),
		++expected_team_chats);
	reply = latest_constructed_team_chat(&context->mock);
	assert_non_null(reply);
	assert_non_null(strstr(reply, "flag"));
	assert_int_equal(BotChatLength(bot->chat_state), 0);

	BotState_SetLongTermGoal(bot, 5, -1, 0);
	process_console_team_command(context,
		15.0f,
		"(Commander): Babe what are you doing?");
	assert_int_equal(count_team_chat_commands(&context->mock),
		++expected_team_chats);
	reply = latest_constructed_team_chat(&context->mock);
	assert_non_null(reply);
	assert_non_null(strstr(reply, "base"));
	assert_int_equal(BotChatLength(bot->chat_state), 0);

	BotState_SetLongTermGoal(bot, 6, -1, 0);
	process_console_team_command(context,
		18.0f,
		"(Commander): Babe what are you doing?");
	assert_int_equal(count_team_chat_commands(&context->mock),
		++expected_team_chats);
	reply = latest_constructed_team_chat(&context->mock);
	assert_non_null(reply);
	assert_non_null(strstr(reply, "camping"));
	assert_int_equal(BotChatLength(bot->chat_state), 0);

	BotState_SetLongTermGoal(bot, 7, -1, 0);
	process_console_team_command(context,
		21.0f,
		"(Commander): Babe what are you doing?");
	assert_int_equal(count_team_chat_commands(&context->mock),
		++expected_team_chats);
	reply = latest_constructed_team_chat(&context->mock);
	assert_non_null(reply);
	assert_non_null(strstr(reply, "patrolling"));
	assert_int_equal(BotChatLength(bot->chat_state), 0);

	size_t constructed_count = 0U;
	for (size_t index = 0; index < context->mock.client_command_count; ++index)
	{
		if (strncmp(context->mock.client_commands[index].command,
			"say_team ",
			9U) == 0)
		{
			assert_int_equal(context->mock.client_commands[index].client, 1);
			++constructed_count;
		}
	}
	assert_int_equal(constructed_count, expected_team_chats);

	context->api->BotShutdownClient(1);
	context->api->BotShutdownLibrary();
}

/*
=============
test_bot_console_what_are_you_doing_preserves_gates_and_empty_sources

Pins teammate/addressee rejection, the unmatched unaddressed form, LTG 0/8
no-ops, empty target/goal substitutions, and semantic state reset defaults.
=============
*/
static void test_bot_console_what_are_you_doing_preserves_gates_and_empty_sources(
	void **state)
{
	bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;
	bot_client_state_t *teammate = NULL;
	bot_client_state_t *bot = setup_console_team_command_fixture(context,
		&teammate);
	assert_int_equal(context->api->BotLibVarSet("nochat", "1"), BLERR_NOERROR);
	assert_int_equal(bot->ltg_type, 0);
	assert_int_equal(bot->ltg_teammate, -1);
	assert_int_equal(bot->team_goal_number, 0);

	BotState_SetLongTermGoal(bot, 1, -1, 0);
	teammate->team = 2;
	process_console_team_command(context,
		3.0f,
		"(Commander): Babe what are you doing?");
	assert_int_equal(count_team_chat_commands(&context->mock), 0U);

	teammate->team = 1;
	process_console_team_command(context,
		6.0f,
		"(Commander): Other what are you doing?");
	assert_int_equal(count_team_chat_commands(&context->mock), 0U);
	process_console_team_command(context,
		9.0f,
		"(Commander): what are you doing?");
	assert_int_equal(count_team_chat_commands(&context->mock), 0U);

	BotState_SetLongTermGoal(bot, 0, -1, 0);
	process_console_team_command(context,
		12.0f,
		"(Commander): Babe what are you doing?");
	assert_int_equal(count_team_chat_commands(&context->mock), 0U);
	BotState_SetLongTermGoal(bot, 8, -1, 0);
	process_console_team_command(context,
		15.0f,
		"(Commander): Babe what are you doing?");
	assert_int_equal(count_team_chat_commands(&context->mock), 0U);

	BotState_SetLongTermGoal(bot, 1, -1, 0);
	assert_string_equal(BotState_ClientName(bot->ltg_teammate), "");
	process_console_team_command(context,
		18.0f,
		"(Commander): Babe what are you doing?");
	assert_int_equal(count_team_chat_commands(&context->mock), 1U);
	const char *reply = latest_constructed_team_chat(&context->mock);
	assert_non_null(reply);
	assert_non_null(strstr(reply, "help"));
	assert_null(strstr(reply, "Babe"));
	assert_null(strstr(reply, "babe"));
	assert_null(strstr(reply, "Commander"));
	assert_null(strstr(reply, "commander"));
	assert_int_equal(BotChatLength(bot->chat_state), 0);

	BotState_SetLongTermGoal(bot, 3, -1, 123456);
	char missing_goal_name[64];
	memset(missing_goal_name, 0x7f, sizeof(missing_goal_name));
	context->api->BotGoalName(123456,
		missing_goal_name,
		(int)sizeof(missing_goal_name));
	assert_string_equal(missing_goal_name, "");
	process_console_team_command(context,
		21.0f,
		"(Commander): Babe what are you doing?");
	assert_int_equal(count_team_chat_commands(&context->mock), 2U);
	reply = latest_constructed_team_chat(&context->mock);
	assert_non_null(reply);
	assert_true(strstr(reply, "defending") != NULL ||
		strstr(reply, "guard") != NULL ||
		strstr(reply, "supervision") != NULL);
	assert_null(strstr(reply, "123456"));
	assert_int_equal(BotChatLength(bot->chat_state), 0);

	BotState_ResetForNewMap(bot);
	assert_int_equal(bot->ltg_type, 0);
	assert_int_equal(bot->ltg_teammate, -1);
	assert_int_equal(bot->team_goal_number, 0);

	context->api->BotShutdownClient(1);
	BotState_Destroy(2);
	context->api->BotShutdownLibrary();
}

/*
=============
test_bot_console_subteam_cases_preserve_retail_gates_and_storage

Pins cases 12/13 to teamplay, teammate, addressee-list and subteam-alias gates,
including the 31-byte terminator and constructed team announcements.
=============
*/
static void test_bot_console_subteam_cases_preserve_retail_gates_and_storage(
	void **state)
{
	bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;
	bot_client_state_t *teammate = NULL;
	bot_client_state_t *bot = setup_console_team_command_fixture(context,
		&teammate);
	const char *long_team = "AlphaABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

	process_console_team_command(context,
		3.0f,
		"(Commander): Babe join team AlphaABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");
	assert_string_equal(bot->subteam, "");
	assert_int_equal(count_team_chat_commands(&context->mock), 0U);

	assert_int_equal(context->api->BotLibVarSet("teamplay", "1"),
		BLERR_NOERROR);
	teammate->team = 2;
	process_console_team_command(context,
		6.0f,
		"(Commander): Babe join team AlphaABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");
	assert_string_equal(bot->subteam, "");
	assert_int_equal(count_team_chat_commands(&context->mock), 0U);

	teammate->team = 1;
	process_console_team_command(context,
		9.0f,
		"(Commander): Other join team AlphaABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");
	assert_string_equal(bot->subteam, "");
	assert_int_equal(count_team_chat_commands(&context->mock), 0U);

	process_console_team_command(context,
		12.0f,
		"(Commander): everyone join team AlphaABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");
	assert_int_equal(strlen(bot->subteam), 31U);
	assert_memory_equal(bot->subteam,
		"AlphaABCDEFGHIJKLMNOPQRSTUVWXYZ",
		31U);
	assert_int_equal(bot->subteam[31], '\0');
	assert_true(find_constructed_team_chat(&context->mock, long_team));
	assert_int_equal(count_team_chat_commands(&context->mock), 1U);

	process_console_team_command(context,
		15.0f,
		"(Commander): Alpha leave your team");
	assert_string_equal(bot->subteam, "");
	assert_memory_equal(bot->subteam, "\0\0\0\0", sizeof(int));
	assert_int_equal(bot->subteam[4], 'a');
	assert_int_equal(count_team_chat_commands(&context->mock), 2U);
	assert_true(find_constructed_team_chat(&context->mock,
		"AlphaABCDEFGHIJKLMNOPQRSTUVWXYZ"));

	process_console_team_command(context,
		18.0f,
		"(Commander): Other and Babe join team Duo");
	assert_string_equal(bot->subteam, "Duo");
	assert_int_equal(count_team_chat_commands(&context->mock), 3U);

	process_console_team_command(context,
		21.0f,
		"(Commander): everyone leave your team");
	assert_string_equal(bot->subteam, "");
	assert_int_equal(count_team_chat_commands(&context->mock), 4U);

	process_console_team_command(context,
		24.0f,
		"(Commander): everyone leave your team");
	assert_string_equal(bot->subteam, "");
	assert_int_equal(count_team_chat_commands(&context->mock), 4U);

	context->api->BotShutdownClient(1);
	BotState_Destroy(2);
	context->api->BotShutdownLibrary();
}

/*
=============
test_bot_console_formation_damage_cases_are_ungated

Pins cases 14/15 to the same exact elementary say-team literal with no
teamplay, sender-team, or addressee gate.
=============
*/
static void test_bot_console_formation_damage_cases_are_ungated(void **state)
{
	bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;
	bot_client_state_t *teammate = NULL;
	(void)setup_console_team_command_fixture(context, &teammate);
	teammate->team = 2;

	process_console_team_command(context,
		3.0f,
		"(Commander): Nobody create a new wedge formation");
	process_console_team_command(context,
		6.0f,
		"(Commander): Nobody your formation position is left relative to Commander");

	const char *literal =
		"the part of my brain to create formations has been damaged";
	size_t direct_count = 0U;
	for (size_t index = 0; index < context->mock.client_command_count; ++index)
	{
		if (strcmp(context->mock.client_commands[index].command, "say_team") == 0)
		{
			assert_int_equal(context->mock.client_commands[index].client, 1);
			assert_string_equal(context->mock.client_commands[index].argument,
				literal);
			++direct_count;
		}
	}
	assert_int_equal(direct_count, 2U);

	context->api->BotShutdownClient(1);
	BotState_Destroy(2);
	context->api->BotShutdownLibrary();
}

/*
=============
test_bot_console_formation_space_preserves_conversion_and_limits

Pins case 16's three gates, unit factors, inclusive endpoints, and 100-unit
fallback for both sides of the valid range.
=============
*/
static void test_bot_console_formation_space_preserves_conversion_and_limits(
	void **state)
{
	bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;
	bot_client_state_t *teammate = NULL;
	bot_client_state_t *bot = setup_console_team_command_fixture(context,
		&teammate);
	bot->formation_dist = 77.0f;

	process_console_team_command(context,
		3.0f,
		"(Commander): Babe the formation intervening space is 1.5 meter");
	assert_float_equal(bot->formation_dist, 77.0f, 0.0001f);

	assert_int_equal(context->api->BotLibVarSet("teamplay", "1"),
		BLERR_NOERROR);
	teammate->team = 2;
	process_console_team_command(context,
		6.0f,
		"(Commander): Babe the formation intervening space is 1.5 meter");
	assert_float_equal(bot->formation_dist, 77.0f, 0.0001f);

	teammate->team = 1;
	process_console_team_command(context,
		9.0f,
		"(Commander): Other the formation intervening space is 1.5 meter");
	assert_float_equal(bot->formation_dist, 77.0f, 0.0001f);

	process_console_team_command(context,
		12.0f,
		"(Commander): Babe the formation intervening space is 1.5 meter");
	assert_float_equal(bot->formation_dist, 48.0f, 0.0001f);
	process_console_team_command(context,
		15.0f,
		"(Commander): Babe the formation intervening space is 15.625 meter");
	assert_float_equal(bot->formation_dist, 500.0f, 0.0001f);
	process_console_team_command(context,
		18.0f,
		"(Commander): Babe the formation intervening space is 1.49 meter");
	assert_float_equal(bot->formation_dist, 100.0f, 0.0001f);
	process_console_team_command(context,
		21.0f,
		"(Commander): Babe the formation intervening space is 15.626 meter");
	assert_float_equal(bot->formation_dist, 100.0f, 0.0001f);
	process_console_team_command(context,
		24.0f,
		"(Commander): Babe the formation intervening space is 5 feet");
	assert_float_equal(bot->formation_dist, 48.768f, 0.001f);

	context->api->BotShutdownClient(1);
	BotState_Destroy(2);
	context->api->BotShutdownLibrary();
}

/*
=============
test_bot_console_doformation_and_dismiss_preserve_exact_ltg_effects

Pins case 17 as a true no-op and case 18 as a gated clear restricted to LTG
types one and two.
=============
*/
static void test_bot_console_doformation_and_dismiss_preserve_exact_ltg_effects(
	void **state)
{
	bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;
	bot_client_state_t *teammate = NULL;
	bot_client_state_t *bot = setup_console_team_command_fixture(context,
		&teammate);
	strcpy(bot->subteam, "Keep");
	bot->formation_dist = 77.0f;
	bot->ltg_type = 1;

	process_console_team_command(context,
		3.0f,
		"(Commander): Babe form the wedge formation");
	assert_string_equal(bot->subteam, "Keep");
	assert_float_equal(bot->formation_dist, 77.0f, 0.0001f);
	assert_int_equal(bot->ltg_type, 1);

	process_console_team_command(context,
		6.0f,
		"(Commander): Babe dismiss");
	assert_int_equal(bot->ltg_type, 1);
	assert_int_equal(context->api->BotLibVarSet("teamplay", "1"),
		BLERR_NOERROR);
	teammate->team = 2;
	process_console_team_command(context,
		9.0f,
		"(Commander): Babe dismiss");
	assert_int_equal(bot->ltg_type, 1);
	teammate->team = 1;
	process_console_team_command(context,
		12.0f,
		"(Commander): Other dismiss");
	assert_int_equal(bot->ltg_type, 1);
	process_console_team_command(context,
		15.0f,
		"(Commander): Babe dismiss");
	assert_int_equal(bot->ltg_type, 0);

	bot->ltg_type = 2;
	process_console_team_command(context,
		18.0f,
		"(Commander): Babe dismiss");
	assert_int_equal(bot->ltg_type, 0);
	bot->ltg_type = 3;
	process_console_team_command(context,
		21.0f,
		"(Commander): Babe dismiss");
	assert_int_equal(bot->ltg_type, 3);
	assert_string_equal(bot->subteam, "Keep");
	assert_float_equal(bot->formation_dist, 77.0f, 0.0001f);
	assert_int_equal(count_team_chat_commands(&context->mock), 0U);

	context->api->BotShutdownClient(1);
	BotState_Destroy(2);
	context->api->BotShutdownLibrary();
}

/*
=============
test_bot_console_camp_preserves_retail_goal_branches_and_deadlines

Pins case 19's team/address gates, item/there/here goals, failure chats, LTG
side effects, random message window, and default/explicit command durations.
=============
*/
static void test_bot_console_camp_preserves_retail_goal_branches_and_deadlines(
	void **state)
{
	bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;
	bot_client_state_t *teammate = NULL;
	bot_client_state_t *bot = setup_console_team_command_fixture(context,
		&teammate);
	setup_console_command_aas_world();
	register_console_command_goal(context,
		"weapon_rocketlauncher",
		302,
		16.0f);
	assert_int_equal(context->api->BotLibVarSet("nochat", "1"),
		BLERR_NOERROR);
	BotState_SetLongTermGoal(bot, 3, -1, 777);

	process_console_team_command(context,
		3.0f,
		"(Commander): Babe camp near the Rocket Launcher");
	assert_int_equal(bot->ltg_type, 3);
	assert_int_equal(bot->team_goal_number, 777);

	assert_int_equal(context->api->BotLibVarSet("teamplay", "1"),
		BLERR_NOERROR);
	teammate->team = 2;
	process_console_team_command(context,
		6.0f,
		"(Commander): Babe camp near the Rocket Launcher");
	assert_int_equal(bot->ltg_type, 3);
	teammate->team = 1;
	process_console_team_command(context,
		9.0f,
		"(Commander): Other camp near the Rocket Launcher");
	assert_int_equal(bot->ltg_type, 3);
	assert_int_equal(count_team_chat_commands(&context->mock), 0U);

	srand(1);
	process_console_team_command(context,
		12.0f,
		"(Commander): Babe camp near the Rocket Launcher");
	assert_int_equal(bot->ltg_type, 6);
	assert_int_equal(bot->ltg_teammate, 2);
	assert_int_equal(bot->team_goal_number, 302);
	assert_int_equal(bot->team_goal.number, 302);
	assert_int_equal(bot->team_goal.entitynum, 302);
	assert_int_equal(bot->team_goal.areanum, 1);
	assert_float_equal(bot->team_goal.origin[0], 16.0f, 0.0001f);
	assert_true(bot->team_message_time >= 12.0f);
	assert_true(bot->team_message_time <= 14.0f);
	assert_float_equal(bot->team_goal_time, 312.0f, 0.0001f);
	assert_float_equal(bot->arrive_time, 0.0f, 0.0001f);
	assert_int_equal(count_team_chat_commands(&context->mock), 0U);

	process_console_team_command(context,
		15.0f,
		"(Commander): Babe camp near the Rocket Launcher for 2 seconds");
	assert_float_equal(bot->team_goal_time, 17.0f, 0.0001f);
	process_console_team_command(context,
		18.0f,
		"(Commander): Babe camp near the Rocket Launcher for 2 minutes");
	assert_float_equal(bot->team_goal_time, 138.0f, 0.0001f);

	process_console_team_command(context,
		21.0f,
		"(Commander): Babe camp there");
	assert_int_equal(bot->team_goal.entitynum, 1);
	assert_int_equal(bot->team_goal.areanum, 1);
	assert_int_equal(bot->team_goal.number, 302);
	assert_int_equal(bot->team_goal.flags, GFL_ITEM);
	assert_float_equal(bot->team_goal.origin[0], 0.0f, 0.0001f);
	assert_float_equal(bot->team_goal.mins[0], -8.0f, 0.0001f);
	assert_float_equal(bot->team_goal.maxs[2], 8.0f, 0.0001f);

	teammate->client_update_valid = true;
	VectorSet(teammate->last_client_update.origin, 32.0f, 0.0f, 32.0f);
	process_console_team_command(context,
		24.0f,
		"(Commander): Babe camp here");
	assert_int_equal(bot->team_goal.entitynum, 2);
	assert_int_equal(bot->team_goal.areanum, 1);
	assert_float_equal(bot->team_goal.origin[0], 32.0f, 0.0001f);
	assert_float_equal(bot->team_goal.origin[2], 32.0f, 0.0001f);

	teammate->client_update_valid = false;
	bot_goal_t successful_goal = bot->team_goal;
	process_console_team_command(context,
		27.0f,
		"(Commander): Babe camp here");
	assert_int_equal(bot->team_goal.entitynum, 0);
	successful_goal.entitynum = 0;
	assert_memory_equal(&bot->team_goal, &successful_goal, sizeof(successful_goal));
	assert_int_equal(count_team_chat_commands(&context->mock), 1U);
	assert_true(find_constructed_team_chat(&context->mock, "Commander"));

	process_console_team_command(context,
		30.0f,
		"(Commander): Babe camp near the Missing Place");
	assert_memory_equal(&bot->team_goal,
		&successful_goal,
		sizeof(successful_goal));
	assert_int_equal(count_team_chat_commands(&context->mock), 2U);
	assert_true(find_constructed_team_chat(&context->mock, "Missing Place"));

	context->api->BotShutdownClient(1);
	BotState_Destroy(2);
	context->api->BotShutdownLibrary();
}

/*
=============
test_bot_console_checkpoint_stores_independently_of_addressing

Pins case 20's teamplay-only storage gate, z adjustment, duplicate replacement,
case-insensitive names, addressed replies, and invalid-area no-op boundary.
=============
*/
static void test_bot_console_checkpoint_stores_independently_of_addressing(
	void **state)
{
	bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;
	bot_client_state_t *teammate = NULL;
	bot_client_state_t *bot = setup_console_team_command_fixture(context,
		&teammate);
	setup_console_command_aas_world();
	assert_int_equal(context->api->BotLibVarSet("nochat", "1"),
		BLERR_NOERROR);

	process_console_team_command(context,
		3.0f,
		"(Commander): Other checkpoint Alpha is at 16 0 32");
	assert_null(bot->checkpoints);

	assert_int_equal(context->api->BotLibVarSet("teamplay", "1"),
		BLERR_NOERROR);
	process_console_team_command(context,
		6.0f,
		"(Commander): Other checkpoint Alpha is at 16 0 32");
	assert_non_null(bot->checkpoints);
	assert_string_equal(bot->checkpoints->name, "Alpha");
	assert_float_equal(bot->checkpoints->goal.origin[2], 32.5f, 0.0001f);
	assert_int_equal(count_team_chat_commands(&context->mock), 0U);

	teammate->team = 2;
	process_console_team_command(context,
		9.0f,
		"(Commander): Babe checkpoint Rival is at 24 0 32");
	assert_string_equal(bot->checkpoints->name, "Rival");
	assert_non_null(bot->checkpoints->next);
	assert_string_equal(bot->checkpoints->next->name, "Alpha");
	assert_int_equal(count_team_chat_commands(&context->mock), 0U);

	teammate->team = 1;
	process_console_team_command(context,
		12.0f,
		"(Commander): Babe checkpoint alpha is at 48 0 32.6");
	assert_string_equal(bot->checkpoints->name, "alpha");
	assert_float_equal(bot->checkpoints->goal.origin[0], 48.0f, 0.0001f);
	assert_float_equal(bot->checkpoints->goal.origin[2], 33.1f, 0.0001f);
	assert_non_null(bot->checkpoints->next);
	assert_string_equal(bot->checkpoints->next->name, "Rival");
	assert_null(bot->checkpoints->next->next);
	assert_int_equal(count_team_chat_commands(&context->mock), 1U);
	assert_true(find_constructed_team_chat(&context->mock,
		"checkpoint alpha at 48 0 33 confirmed"));

	process_console_team_command(context,
		15.0f,
		"(Commander): checkpoint Bravo is at 64 0 32");
	assert_string_equal(bot->checkpoints->name, "Bravo");
	assert_non_null(bot->checkpoints->next);
	assert_ptr_equal(bot->checkpoints->next->prev, bot->checkpoints);
	assert_int_equal(count_team_chat_commands(&context->mock), 2U);
	assert_true(find_constructed_team_chat(&context->mock, "checkpoint Bravo"));

	process_console_team_command(context,
		18.0f,
		"(Commander): Babe checkpoint Invalid is at 512 0 32");
	assert_string_equal(bot->checkpoints->name, "Bravo");
	assert_int_equal(count_team_chat_commands(&context->mock), 3U);
	assert_true(find_constructed_team_chat(&context->mock,
		"invalid checkpoint"));

	process_console_team_command(context,
		21.0f,
		"(Commander): Other checkpoint Silent is at 512 0 32");
	assert_string_equal(bot->checkpoints->name, "Bravo");
	assert_int_equal(count_team_chat_commands(&context->mock), 3U);

	BotState_ResetForNewMap(bot);
	assert_null(bot->checkpoints);
	assert_null(bot->patrol_points);
	assert_null(bot->current_patrol_point);
	context->api->BotShutdownClient(1);
	BotState_Destroy(2);
	context->api->BotShutdownLibrary();
}

/*
=============
test_bot_console_patrol_preserves_lists_flags_failures_and_deadlines

Pins case 21's gates, one-point preservation, key-area failure clear, ordered
waypoint replacement, loop/reverse flags, LTG state, and timing semantics.
=============
*/
static void test_bot_console_patrol_preserves_lists_flags_failures_and_deadlines(
	void **state)
{
	bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;
	bot_client_state_t *teammate = NULL;
	bot_client_state_t *bot = setup_console_team_command_fixture(context,
		&teammate);
	setup_console_command_aas_world();
	register_console_command_goal(context,
		"weapon_rocketlauncher",
		302,
		16.0f);
	register_console_command_goal(context, "item_quad", 303, 64.0f);
	assert_int_equal(context->api->BotLibVarSet("nochat", "1"),
		BLERR_NOERROR);

	bot_console_waypoint_t *old = calloc(1, sizeof(*old));
	assert_non_null(old);
	strcpy(old->name, "Old Patrol");
	bot->patrol_points = old;
	bot->current_patrol_point = old;
	bot->patrol_flags = 99;
	bot->ltg_type = 4;
	bot->ltg_teammate = 6;

	process_console_team_command(context,
		3.0f,
		"(Commander): Babe patrol from Rocket Launcher to Quad Damage and back");
	assert_ptr_equal(bot->patrol_points, old);
	assert_int_equal(bot->ltg_type, 4);

	assert_int_equal(context->api->BotLibVarSet("teamplay", "1"),
		BLERR_NOERROR);
	process_console_team_command(context,
		6.0f,
		"(Commander): Other patrol from Rocket Launcher to Quad Damage and back");
	assert_ptr_equal(bot->patrol_points, old);
	assert_int_equal(bot->ltg_type, 4);

	process_console_team_command(context,
		9.0f,
		"(Commander): Babe patrol from Rocket Launcher");
	assert_ptr_equal(bot->patrol_points, old);
	assert_ptr_equal(bot->current_patrol_point, old);
	assert_int_equal(bot->patrol_flags, 99);
	assert_int_equal(bot->ltg_type, 4);
	assert_true(find_direct_team_chat(&context->mock,
		"I need more key points to patrol\n"));

	process_console_team_command(context,
		12.0f,
		"(Commander): Babe patrol from Missing Place to Rocket Launcher and back");
	assert_null(bot->patrol_points);
	assert_ptr_equal(bot->current_patrol_point, old);
	assert_int_equal(bot->patrol_flags, 99);
	assert_int_equal(bot->ltg_type, 4);
	assert_true(find_constructed_team_chat(&context->mock, "Missing Place"));
	free(old);
	old = NULL;

	srand(2);
	process_console_team_command(context,
		15.0f,
		"(Commander): patrol from Rocket Launcher to Quad Damage and back");
	assert_non_null(bot->patrol_points);
	assert_string_equal(bot->patrol_points->name, "Rocket Launcher");
	assert_non_null(bot->patrol_points->next);
	assert_string_equal(bot->patrol_points->next->name, "Quad Damage");
	assert_ptr_equal(bot->patrol_points->next->prev, bot->patrol_points);
	assert_null(bot->patrol_points->next->next);
	assert_ptr_equal(bot->current_patrol_point, bot->patrol_points);
	assert_int_equal(bot->patrol_flags, 1);
	assert_int_equal(bot->ltg_type, 7);
	assert_int_equal(bot->ltg_teammate, 6);
	assert_true(bot->team_message_time >= 15.0f);
	assert_true(bot->team_message_time <= 17.0f);
	assert_float_equal(bot->team_goal_time, 315.0f, 0.0001f);

	process_console_team_command(context,
		18.0f,
		"(Commander): Babe patrol from Quad Damage to Rocket Launcher and reverse for 2 seconds");
	assert_non_null(bot->patrol_points);
	assert_string_equal(bot->patrol_points->name, "Quad Damage");
	assert_non_null(bot->patrol_points->next);
	assert_string_equal(bot->patrol_points->next->name, "Rocket Launcher");
	assert_null(bot->patrol_points->next->next);
	assert_int_equal(bot->patrol_flags, 2);
	assert_int_equal(bot->ltg_type, 7);
	assert_int_equal(bot->ltg_teammate, 6);
	assert_float_equal(bot->team_goal_time, 20.0f, 0.0001f);

	process_console_team_command(context,
		21.0f,
		"(Commander): Babe patrol from Rocket Launcher to Quad Damage");
	assert_non_null(bot->patrol_points);
	assert_string_equal(bot->patrol_points->name, "Rocket Launcher");
	assert_non_null(bot->patrol_points->next);
	assert_string_equal(bot->patrol_points->next->name, "Quad Damage");
	assert_null(bot->patrol_points->next->next);
	assert_int_equal(bot->patrol_flags, 0);
	assert_float_equal(bot->team_goal_time, 321.0f, 0.0001f);
	assert_int_equal(count_team_chat_commands(&context->mock), 2U);

	context->api->BotShutdownClient(1);
	BotState_Destroy(2);
	context->api->BotShutdownLibrary();
}

/*
=============
test_bot_console_help_accompany_preserves_retail_target_and_goal_boundaries

Pins cases 3/4 teamplay and address gates, teammate parsing, fuzzy failures,
entity-only goal clearing, near-item fallback, and distinct LTG deadlines.
=============
*/
static void test_bot_console_help_accompany_preserves_retail_target_and_goal_boundaries(
	void **state)
{
	bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;
	bot_client_state_t *commander = NULL;
	bot_client_state_t *bot = setup_console_team_command_fixture(context,
		&commander);
	setup_console_command_aas_world();
	register_console_command_goal(context,
		"weapon_rocketlauncher",
		302,
		48.0f);
	assert_int_equal(context->api->BotLibVarSet("nochat", "1"),
		BLERR_NOERROR);

	bot_clientsettings_t presentation;
	memset(&presentation, 0, sizeof(presentation));
	snprintf(presentation.netname, sizeof(presentation.netname), "Ally Ranger");
	snprintf(presentation.skin, sizeof(presentation.skin), "male/athena");
	assert_int_equal(context->api->BotClientSettings(3, &presentation),
		BLERR_NOERROR);
	bot_client_state_t *ally = BotState_Create(3);
	assert_non_null(ally);
	ally->team = 1;
	BotState_SetActive(ally, true);

	bot->ltg_type = 9;
	bot->team_goal_number = 777;
	VectorSet(bot->team_goal.origin, 7.0f, 8.0f, 9.0f);
	bot->team_goal.areanum = 33;
	VectorSet(bot->team_goal.mins, -3.0f, -4.0f, -5.0f);
	VectorSet(bot->team_goal.maxs, 3.0f, 4.0f, 5.0f);
	bot->team_goal.entitynum = 91;
	bot->team_goal.number = 777;
	bot->team_goal.flags = GFL_ITEM;
	bot->team_goal.iteminfo = 17;
	bot_goal_t untouched_goal = bot->team_goal;

	process_console_team_command(context,
		3.0f,
		"(Commander): Babe help me");
	assert_int_equal(bot->ltg_type, 9);
	assert_memory_equal(&bot->team_goal, &untouched_goal, sizeof(untouched_goal));

	assert_int_equal(context->api->BotLibVarSet("teamplay", "1"),
		BLERR_NOERROR);
	process_console_team_command(context,
		6.0f,
		"(Commander): Other help me");
	assert_int_equal(bot->ltg_type, 9);
	assert_memory_equal(&bot->team_goal, &untouched_goal, sizeof(untouched_goal));

	commander->team = 2;
	process_console_team_command(context,
		9.0f,
		"(Commander): Babe help me");
	assert_int_equal(bot->ltg_type, 9);
	commander->team = 1;

	process_console_team_command(context,
		12.0f,
		"(Commander): Babe help Babe");
	assert_int_equal(bot->ltg_type, 9);
	assert_memory_equal(&bot->team_goal, &untouched_goal, sizeof(untouched_goal));

	process_console_team_command(context,
		15.0f,
		"(Commander): Babe help Missing Ranger");
	assert_int_equal(bot->ltg_type, 9);
	assert_memory_equal(&bot->team_goal, &untouched_goal, sizeof(untouched_goal));
	assert_true(find_constructed_team_chat(&context->mock, "Missing Ranger"));

	process_console_team_command(context,
		18.0f,
		"(Commander): Babe help Ally Ranger");
	assert_int_equal(bot->ltg_type, 9);
	assert_int_equal(bot->team_goal.entitynum, 0);
	untouched_goal.entitynum = 0;
	assert_memory_equal(&bot->team_goal, &untouched_goal, sizeof(untouched_goal));
	assert_true(find_constructed_team_chat(&context->mock, "Ally Ranger"));

	process_console_team_command(context,
		21.0f,
		"(Commander): Babe help me");
	assert_int_equal(bot->ltg_type, 9);
	assert_int_equal(bot->team_goal.entitynum, 0);
	assert_true(find_constructed_team_chat(&context->mock, "Commander"));

	commander->client_update_valid = true;
	VectorSet(commander->last_client_update.origin, 24.0f, 0.0f, 32.0f);
	srand(3);
	process_console_team_command(context,
		24.0f,
		"(Commander): Babe help me");
	assert_int_equal(bot->ltg_type, 1);
	assert_int_equal(bot->ltg_teammate, 2);
	assert_int_equal(bot->team_goal.entitynum, 2);
	assert_int_equal(bot->team_goal.areanum, 1);
	assert_float_equal(bot->team_goal.origin[0], 24.0f, 0.0001f);
	assert_float_equal(bot->team_goal.mins[0], -8.0f, 0.0001f);
	assert_float_equal(bot->team_goal.maxs[2], 8.0f, 0.0001f);
	assert_int_equal(bot->team_goal.number, 777);
	assert_int_equal(bot->team_goal.flags, GFL_ITEM);
	assert_int_equal(bot->team_goal.iteminfo, 17);
	assert_int_equal(bot->team_goal_number, 777);
	assert_float_equal(bot->teammate_visible_time, 24.0f, 0.0001f);
	assert_true(bot->team_message_time >= 24.0f);
	assert_true(bot->team_message_time <= 26.0f);
	assert_float_equal(bot->team_goal_time, 84.0f, 0.0001f);

	ally->client_update_valid = false;
	process_console_team_command(context,
		27.0f,
		"(Commander): Babe help Ally Ranger near the Rocket Launcher");
	assert_int_equal(bot->ltg_type, 1);
	assert_int_equal(bot->ltg_teammate, 3);
	assert_int_equal(bot->team_goal.entitynum, 302);
	assert_int_equal(bot->team_goal.number, 302);
	assert_int_equal(bot->team_goal_number, 302);
	assert_float_equal(bot->teammate_visible_time, 27.0f, 0.0001f);
	assert_float_equal(bot->team_goal_time, 87.0f, 0.0001f);

	bot->formation_dist = 13.0f;
	bot->arrive_time = 19.0f;
	process_console_team_command(context,
		30.0f,
		"(Commander): Babe follow me");
	assert_int_equal(bot->ltg_type, 2);
	assert_int_equal(bot->ltg_teammate, 2);
	assert_float_equal(bot->team_goal_time, 270.0f, 0.0001f);
	assert_float_equal(bot->formation_dist, 112.0f, 0.0001f);
	assert_float_equal(bot->arrive_time, 0.0f, 0.0001f);

	process_console_team_command(context,
		33.0f,
		"(Commander): Babe follow me for 2 seconds");
	assert_int_equal(bot->ltg_type, 2);
	assert_float_equal(bot->team_goal_time, 35.0f, 0.0001f);
	assert_float_equal(bot->teammate_visible_time, 33.0f, 0.0001f);
	assert_int_equal(count_team_chat_commands(&context->mock), 3U);

	context->api->BotShutdownClient(1);
	BotState_Destroy(3);
	BotState_Destroy(2);
	context->api->BotShutdownLibrary();
}

/*
=============
test_bot_console_defend_key_area_preserves_retail_gates_and_deadlines

Pins case 5's teamplay/address gates, unresolved-goal reply, static goal copy,
random message window, default/explicit duration, and defend-away reset.
=============
*/
static void test_bot_console_defend_key_area_preserves_retail_gates_and_deadlines(
	void **state)
{
	bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;
	bot_client_state_t *commander = NULL;
	bot_client_state_t *bot = setup_console_team_command_fixture(context,
		&commander);
	setup_console_command_aas_world();
	register_console_command_goal(context,
		"weapon_rocketlauncher",
		302,
		48.0f);
	assert_int_equal(context->api->BotLibVarSet("nochat", "1"),
		BLERR_NOERROR);
	bot->ltg_type = 8;
	bot->defend_away_time = 44.0f;
	bot->team_goal.entitynum = 91;
	bot->team_goal.number = 777;
	bot->team_goal_number = 777;
	bot_goal_t untouched_goal = bot->team_goal;

	process_console_team_command(context,
		3.0f,
		"(Commander): Babe defend the Rocket Launcher");
	assert_int_equal(bot->ltg_type, 8);
	assert_float_equal(bot->defend_away_time, 44.0f, 0.0001f);

	assert_int_equal(context->api->BotLibVarSet("teamplay", "1"),
		BLERR_NOERROR);
	process_console_team_command(context,
		6.0f,
		"(Commander): Other defend the Rocket Launcher");
	assert_int_equal(bot->ltg_type, 8);

	commander->team = 2;
	process_console_team_command(context,
		9.0f,
		"(Commander): Babe defend the Rocket Launcher");
	assert_int_equal(bot->ltg_type, 8);
	commander->team = 1;

	process_console_team_command(context,
		12.0f,
		"(Commander): Babe defend the Missing Place");
	assert_int_equal(bot->ltg_type, 8);
	assert_memory_equal(&bot->team_goal, &untouched_goal, sizeof(untouched_goal));
	assert_true(find_constructed_team_chat(&context->mock, "Missing Place"));

	srand(4);
	process_console_team_command(context,
		15.0f,
		"(Commander): Babe defend the Rocket Launcher");
	assert_int_equal(bot->ltg_type, 3);
	assert_int_equal(bot->team_goal.entitynum, 302);
	assert_int_equal(bot->team_goal.number, 302);
	assert_int_equal(bot->team_goal_number, 302);
	assert_float_equal(bot->team_goal.origin[0], 48.0f, 0.0001f);
	assert_true(bot->team_message_time >= 15.0f);
	assert_true(bot->team_message_time <= 17.0f);
	assert_float_equal(bot->team_goal_time, 135.0f, 0.0001f);
	assert_float_equal(bot->defend_away_time, 0.0f, 0.0001f);

	bot->defend_away_time = 23.0f;
	process_console_team_command(context,
		18.0f,
		"(Commander): Babe defend the Rocket Launcher for 2 seconds");
	assert_int_equal(bot->ltg_type, 3);
	assert_float_equal(bot->team_goal_time, 20.0f, 0.0001f);
	assert_float_equal(bot->defend_away_time, 0.0f, 0.0001f);
	assert_int_equal(count_team_chat_commands(&context->mock), 1U);

	context->api->BotShutdownClient(1);
	BotState_Destroy(2);
	context->api->BotShutdownLibrary();
}

/*
=============
test_bot_console_ctf_orders_require_both_flags_and_preserve_exact_resets

Pins cases 6/7 to the ctf/two-flag/address gate, including operation with the
teamplay cvar off, fixed LTG durations, and case-6-only rush-away clearing.
=============
*/
static void test_bot_console_ctf_orders_require_both_flags_and_preserve_exact_resets(
	void **state)
{
	bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;
	bot_client_state_t *commander = NULL;
	bot_client_state_t *bot = setup_console_team_command_fixture(context,
		&commander);
	setup_console_command_aas_world();
	assert_int_equal(context->api->BotLibVarSet("nochat", "1"),
		BLERR_NOERROR);
	bot->ltg_type = 9;
	bot->rush_base_away_time = 55.0f;

	process_console_team_command(context,
		3.0f,
		"(Commander): Babe rush base");
	assert_int_equal(bot->ltg_type, 9);

	assert_int_equal(context->api->BotLibVarSet("ctf", "1"),
		BLERR_NOERROR);
	process_console_team_command(context,
		6.0f,
		"(Commander): Babe rush base");
	assert_int_equal(bot->ltg_type, 9);

	register_console_command_goal(context, "Red Flag", 401, -48.0f);
	process_console_team_command(context,
		9.0f,
		"(Commander): Babe rush base");
	assert_int_equal(bot->ltg_type, 9);

	register_console_command_goal(context, "Blue Flag", 402, 48.0f);
	bot_goal_t flag_goal;
	char red_flag_name[] = "Red Flag";
	char blue_flag_name[] = "Blue Flag";
	assert_true(context->api->BotGetLevelItemGoal(-1,
		red_flag_name,
		&flag_goal) >= 0);
	assert_int_equal(flag_goal.areanum, 1);
	assert_true(context->api->BotGetLevelItemGoal(-1,
		blue_flag_name,
		&flag_goal) >= 0);
	assert_int_equal(flag_goal.areanum, 1);
	commander->team = 2;
	process_console_team_command(context,
		12.0f,
		"(Commander): Babe rush base");
	assert_int_equal(bot->ltg_type, 9);
	commander->team = 1;

	srand(5);
	process_console_team_command(context,
		15.0f,
		"(Commander): Other rush base");
	assert_int_equal(bot->ltg_type, 5);
	assert_true(bot->team_message_time >= 15.0f);
	assert_true(bot->team_message_time <= 17.0f);
	assert_float_equal(bot->team_goal_time, 135.0f, 0.0001f);
	assert_float_equal(bot->rush_base_away_time, 0.0f, 0.0001f);

	bot->ltg_type = 9;
	bot->rush_base_away_time = 55.0f;
	process_console_team_command(context,
		18.0f,
		"(Commander): Babe rush base");
	assert_int_equal(bot->ltg_type, 5);
	assert_true(bot->team_message_time >= 18.0f);
	assert_true(bot->team_message_time <= 20.0f);
	assert_float_equal(bot->team_goal_time, 138.0f, 0.0001f);
	assert_float_equal(bot->rush_base_away_time, 0.0f, 0.0001f);

	process_console_team_command(context,
		21.0f,
		"(Commander): Other get the enemy flag");
	assert_int_equal(bot->ltg_type, 5);
	assert_float_equal(bot->rush_base_away_time, 0.0f, 0.0001f);

	bot->rush_base_away_time = 55.0f;
	process_console_team_command(context,
		24.0f,
		"(Commander): Babe get the enemy flag");
	assert_int_equal(bot->ltg_type, 4);
	assert_true(bot->team_message_time >= 24.0f);
	assert_true(bot->team_message_time <= 26.0f);
	assert_float_equal(bot->team_goal_time, 204.0f, 0.0001f);
	assert_float_equal(bot->rush_base_away_time, 55.0f, 0.0001f);
	assert_int_equal(count_team_chat_commands(&context->mock), 0U);

	context->api->BotShutdownClient(1);
	BotState_Destroy(2);
	context->api->BotShutdownLibrary();
}

/*
=============
test_bot_console_reply_enters_and_completes_stand

Pins the two retail random gates, exact-node removal, pending reply typing
deadline, stationary frame, and eventual global say dispatch.
=============
*/
static void test_bot_console_reply_enters_and_completes_stand(void **state)
{
	bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;
	Mock_Reset(&context->mock);

	int status = context->api->BotSetupLibrary();
	assert_int_equal(status, BLERR_NOERROR);

	bot_settings_t settings;
	memset(&settings, 0, sizeof(settings));
	snprintf(settings.characterfile, sizeof(settings.characterfile), "bots/sparta_c.c");
	snprintf(settings.charactername, sizeof(settings.charactername), "spartacus");
	assert_true(context->api->BotSetupClient(1, &settings));
	assert_int_equal(context->api->BotLibVarSet("teamplay", "1"), BLERR_NOERROR);

	bot_clientsettings_t presentation;
	memset(&presentation, 0, sizeof(presentation));
	snprintf(presentation.netname, sizeof(presentation.netname), "Spartacus");
	assert_int_equal(context->api->BotClientSettings(1, &presentation), BLERR_NOERROR);

	assert_int_equal(context->api->BotConsoleMessage(1,
		CMS_CHAT,
		"Alice: abnormal"),
		BLERR_NOERROR);
	for (int index = 0; index < 9; ++index)
	{
		char message[32];
		snprintf(message, sizeof(message), "normal %d", index);
		assert_int_equal(context->api->BotConsoleMessage(1,
			CMS_NORMAL,
			message),
			BLERR_NOERROR);
	}

	bot_client_state_t *client_state = BotState_Get(1);
	assert_non_null(client_state);
	assert_int_equal(BotNumConsoleMessages(client_state->chat_state), 10);

	bot_updateclient_t update;
	memset(&update, 0, sizeof(update));
	update.pm_type = PM_NORMAL;
	update.pm_flags = PMF_ON_GROUND;

	srand(1);
	assert_int_equal(context->api->BotStartFrame(0.1f), BLERR_NOERROR);
	assert_int_equal(context->api->BotUpdateClient(1, &update), BLERR_NOERROR);
	assert_int_equal(context->api->BotAI(1, 0.05f), BLERR_NOERROR);

	assert_true(client_state->chat_standing);
	assert_true(client_state->stand_time > 0.1f);
	assert_true(BotChatLength(client_state->chat_state) > 0);
	assert_int_equal(BotNumConsoleMessages(client_state->chat_state), 9);
	assert_true(context->mock.bot_input_count > 0);
	const bot_input_t *stand_input =
		&context->mock.inputs[context->mock.bot_input_count - 1U];
	assert_float_equal(stand_input->speed, 0.0f, 0.0001f);

	float stand_deadline = client_state->stand_time;
	assert_int_equal(context->api->BotStartFrame(stand_deadline + 0.01f),
		BLERR_NOERROR);
	assert_int_equal(context->api->BotUpdateClient(1, &update), BLERR_NOERROR);
	assert_int_equal(context->api->BotAI(1, 0.05f), BLERR_NOERROR);

	assert_false(client_state->chat_standing);
	assert_int_equal(BotChatLength(client_state->chat_state), 0);
	assert_int_equal(BotNumConsoleMessages(client_state->chat_state), 0);
	bool found_reply_command = false;
	for (size_t index = 0; index < context->mock.client_command_count; ++index)
	{
		if (context->mock.client_commands[index].client == 1 &&
			strncmp(context->mock.client_commands[index].command, "say ", 4U) == 0)
		{
			found_reply_command = true;
			break;
		}
	}
	assert_true(found_reply_command);

	context->api->BotShutdownClient(1);
	context->api->BotShutdownLibrary();
}

/*
=============
test_chat_initial_exports_preserve_raw_type_aliases

Checks the bridge exposes the reconstructed initial-chat count and construction
helpers without disturbing existing chat state ownership.
=============
*/
static void test_chat_initial_exports_preserve_raw_type_aliases(void **state)
{
	bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;

	Mock_Reset(&context->mock);

	assert_non_null(context->api->BotNumInitialChats);
	assert_non_null(context->api->BotInitialChat);
	assert_non_null(context->api->BotChatLength);
	assert_non_null(context->api->BotGetChatMessage);
	assert_non_null(context->api->BotSetChatGender);
	assert_non_null(context->api->BotSetChatName);
	assert_non_null(context->api->BotReplyChatWithContexts);
	assert_non_null(context->api->StringContains);
	assert_non_null(context->api->BotFindMatch);
	assert_non_null(context->api->BotMatchVariable);
	assert_non_null(context->api->UnifyWhiteSpaces);
	assert_non_null(context->api->BotReplaceSynonyms);

	assert_int_equal(context->api->BotNumInitialChats(NULL, "game_exit"), 0);
	assert_int_equal(context->api->BotInitialChat(NULL, "game_exit", 0, NULL), 0);
	assert_int_equal(context->api->BotReplyChatWithContexts(NULL,
		"hello",
		0,
		0,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL),
		0);
	assert_int_equal(context->api->BotChatLength(NULL), 0);
	assert_int_equal(context->api->StringContains("Alpha Beta", "beta", 0), -1);

	int status = context->api->BotSetupLibrary();
	assert_int_equal(status, BLERR_NOERROR);

	assert_int_equal(context->api->StringContains("Alpha Beta", "beta", 0), 6);
	char whitespace[] = "  Alpha\t\tBeta\n ";
	context->api->UnifyWhiteSpaces(whitespace);
	assert_string_equal(whitespace, "Alpha Beta");

	char synonym_text[256] = "I can't stay";
	context->api->BotReplaceSynonyms(synonym_text, 1);
	assert_string_equal(synonym_text, "I can not stay");

	bot_match_t match;
	assert_true(context->api->BotFindMatch("Alice was railed by Bob\n",
		&match,
		1));
	assert_int_equal(match.type, 1);
	assert_int_equal(match.subtype, 11);
	char variable[256];
	context->api->BotMatchVariable(&match, 0, variable, sizeof(variable));
	assert_string_equal(variable, "Alice");
	context->api->BotMatchVariable(&match, 1, variable, sizeof(variable));
	assert_string_equal(variable, "Bob");

	bot_chatstate_t *chat = context->api->BotAllocChatState();
	assert_non_null(chat);
	assert_true(context->api->BotLoadChatFile(chat, "bots/babe_t.c", "babe"));

	const int exit_count = context->api->BotNumInitialChats(chat, "exit_game");
	assert_true(exit_count > 0);
	assert_int_equal(context->api->BotNumInitialChats(chat, "game_exit"), exit_count);

	assert_true(context->api->BotInitialChat(chat,
		"game_exit",
		0,
		"Babe",
		"Opponent",
		"[invalid]",
		"[invalid]",
		"base1",
		NULL));
	assert_true(context->api->BotChatLength(chat) > 0);

	char copied_message[256];
	context->api->BotGetChatMessage(chat,
		copied_message,
		sizeof(copied_message));
	assert_true(copied_message[0] != '\0');
	assert_null(strstr(copied_message, "\\v"));
	assert_int_equal(context->api->BotChatLength(chat), 0);

	int message_type = -1;
	char message[256];
	assert_false(context->api->BotNextConsoleMessage(chat,
		&message_type,
		message,
		sizeof(message)));

	context->api->BotQueueConsoleMessage(chat, CMS_CHAT, "incoming console text");
	assert_true(context->api->BotNextConsoleMessage(chat,
		&message_type,
		message,
		sizeof(message)));
	assert_int_equal(message_type, CMS_CHAT);
	assert_string_equal(message, "incoming console text");

	context->api->BotFreeChatState(chat);
	context->api->BotShutdownLibrary();
}

/*
=============
test_bot_setup_library_wires_chat_setup

Pins the HLIL setup sequence that calls the shared chat loader during library
initialisation.
=============
*/
static void test_bot_setup_library_wires_chat_setup(void **state)
{
	bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;

	Mock_Reset(&context->mock);

	int status = context->api->BotLibVarSet("rchatfile", "definitely_missing_rchat.c");
	assert_int_equal(status, BLERR_NOERROR);

	status = context->api->BotSetupLibrary();
	assert_int_equal(status, BLERR_NOERROR);

	Mock_AssertPrintContains(&context->mock,
		"BotSetupChatAI: couldn't load reply chats definitely_missing_rchat.c",
		PRT_WARNING);

	context->api->BotShutdownLibrary();
}

/*
=============
test_bot_lib_var_set_preserves_retail_local_contract

Pins the export to an unconditional local update and zero return value.
=============
*/
static void test_bot_lib_var_set_preserves_retail_local_contract(void **state)
{
    bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;

    Mock_Reset(&context->mock);

    const botlib_import_table_t *original_imports = BotInterface_GetImportTable();
    assert_non_null(original_imports);

    botlib_import_table_t override_imports = *original_imports;
    override_imports.BotLibVarSet = Mock_ImportBotLibVarSet;

    BotInterface_SetImportTable(&override_imports);
	g_mock_import_libvar_set_count = 0;

    g_mock_import_libvar_set_status = BLERR_NOERROR;
    int status = context->api->BotLibVarSet("test_override", "42");
    assert_int_equal(status, BLERR_NOERROR);
	assert_string_equal(LibVarGetString("test_override"), "42");

    g_mock_import_libvar_set_status = BLERR_LIBRARYNOTSETUP;
    status = context->api->BotLibVarSet("test_override", "84");
	assert_int_equal(status, BLERR_NOERROR);
	assert_string_equal(LibVarGetString("test_override"), "84");

    g_mock_import_libvar_set_status = BLERR_INVALIDIMPORT;
    status = context->api->BotLibVarSet("test_override", "168");
	assert_int_equal(status, BLERR_NOERROR);
	assert_string_equal(LibVarGetString("test_override"), "168");
	assert_int_equal(g_mock_import_libvar_set_count, 0);

	status = context->api->BotLibVarSet(NULL, NULL);
	assert_int_equal(status, BLERR_NOERROR);

    BotInterface_SetImportTable(original_imports);
}

static void test_bot_lib_var_cache_tracks_updates(void **state)
{
    bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;

    Mock_Reset(&context->mock);

    int status = context->api->BotSetupLibrary();
    assert_int_equal(status, BLERR_NOERROR);

    const botlib_import_table_t *imports = BotInterface_GetImportTable();
    assert_non_null(imports);
    assert_non_null(imports->BotLibVarGet);

    char buffer[128];

    memset(buffer, 0, sizeof(buffer));
    status = imports->BotLibVarGet("cache_probe", buffer, sizeof(buffer));
    assert_int_equal(status, BLERR_INVALIDIMPORT);

    status = context->api->BotLibVarSet("cache_probe", "alpha");
    assert_int_equal(status, BLERR_NOERROR);

    memset(buffer, 0, sizeof(buffer));
    status = imports->BotLibVarGet("cache_probe", buffer, sizeof(buffer));
    assert_int_equal(status, BLERR_NOERROR);
    assert_string_equal(buffer, "alpha");

    status = context->api->BotLibVarSet("cache_probe", "beta");
    assert_int_equal(status, BLERR_NOERROR);

    memset(buffer, 0, sizeof(buffer));
    status = imports->BotLibVarGet("cache_probe", buffer, sizeof(buffer));
    assert_int_equal(status, BLERR_NOERROR);
    assert_string_equal(buffer, "beta");

    status = context->api->BotLibVarSet("cache_secondary", "gamma");
    assert_int_equal(status, BLERR_NOERROR);

    memset(buffer, 0, sizeof(buffer));
    status = imports->BotLibVarGet("cache_secondary", buffer, sizeof(buffer));
    assert_int_equal(status, BLERR_NOERROR);
    assert_string_equal(buffer, "gamma");
}

static void test_bot_update_entity_populates_aas(void **state)
{
    bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;

	if (!ensure_map_fixture(&context->assets, "test2"))
	{
		cmocka_skip();
	}

    Mock_Reset(&context->mock);

    int status = context->api->BotSetupLibrary();
    assert_int_equal(status, BLERR_NOERROR);

    status = context->api->BotLoadMap("maps/test2.bsp", 0, NULL, 0, NULL, 0, NULL);
    assert_int_equal(status, BLERR_NOERROR);

    status = context->api->BotStartFrame(0.0f);
    assert_int_equal(status, BLERR_NOERROR);

    bot_updateentity_t entity;
    memset(&entity, 0, sizeof(entity));
    entity.origin[0] = 16.0f;
    entity.origin[1] = 24.0f;
    entity.origin[2] = 48.0f;
    entity.old_origin[0] = 16.0f;
    entity.old_origin[1] = 24.0f;
    entity.old_origin[2] = 48.0f;
    entity.solid = 31;
    entity.modelindex = 2;

    status = context->api->BotUpdateEntity(5, &entity);
    assert_int_equal(status, BLERR_NOERROR);
    assert_true(aasworld.entitiesValid);
    assert_non_null(aasworld.entities);
    assert_int_equal(aasworld.entities[5].number, 5);

    AASEntityFrame translated;
    assert_true(Bridge_ReadEntityFrame(5, &translated));
    assert_float_equal(translated.last_update_time, 0.0f, 0.0001f);
    assert_float_equal(translated.frame_delta, 0.0f, 0.0001f);
    assert_false(translated.is_mover);

    status = context->api->BotStartFrame(0.25f);
    assert_int_equal(status, BLERR_NOERROR);

    bot_updateentity_t follow_up = entity;
    follow_up.origin[0] = 24.0f;
    follow_up.origin[1] = 32.0f;
    follow_up.origin[2] = 52.0f;
    follow_up.old_origin[0] = entity.origin[0];
    follow_up.old_origin[1] = entity.origin[1];
    follow_up.old_origin[2] = entity.origin[2];

    status = context->api->BotUpdateEntity(5, &follow_up);
    assert_int_equal(status, BLERR_NOERROR);

    assert_true(Bridge_ReadEntityFrame(5, &translated));
    assert_float_equal(translated.last_update_time, 0.25f, 0.0001f);
    assert_float_equal(translated.frame_delta, 0.25f, 0.0001f);
    assert_true(translated.frame_delta > 0.0f);
    assert_false(translated.is_mover);

    context->api->BotShutdownLibrary();
}

typedef struct bot_mover_fixture_s
{
    aas_area_t *areas;
    aas_areasettings_t *area_settings;
    aas_reachability_t *reachability;
    aas_entity_t *entities;
} bot_mover_fixture_t;

static void bot_mover_fixture_init(bot_mover_fixture_t *fixture)
{
    memset(&aasworld, 0, sizeof(aasworld));
    aasworld.loaded = qtrue;
    aasworld.initialized = qtrue;
    aasworld.time = 0.0f;
    aasworld.numAreas = 3;
    aasworld.numAreaSettings = 4;
    aasworld.numReachability = 3;
    aasworld.maxEntities = 8;

    fixture->areas = (aas_area_t *)calloc((size_t)(aasworld.numAreas + 1), sizeof(aas_area_t));
    fixture->area_settings =
        (aas_areasettings_t *)calloc((size_t)aasworld.numAreaSettings, sizeof(aas_areasettings_t));
    fixture->reachability =
        (aas_reachability_t *)calloc((size_t)aasworld.numReachability, sizeof(aas_reachability_t));
    fixture->entities = (aas_entity_t *)calloc((size_t)aasworld.maxEntities, sizeof(aas_entity_t));

    assert_non_null(fixture->areas);
    assert_non_null(fixture->area_settings);
    assert_non_null(fixture->reachability);
    assert_non_null(fixture->entities);

    aasworld.areas = fixture->areas;
    aasworld.areasettings = fixture->area_settings;
    aasworld.reachability = fixture->reachability;
    aasworld.entities = fixture->entities;
    TranslateEntity_SetWorldLoaded(qtrue);

    VectorSet(aasworld.areas[1].mins, -64.0f, -64.0f, 0.0f);
    VectorSet(aasworld.areas[1].maxs, 64.0f, 64.0f, 64.0f);
    VectorSet(aasworld.areas[2].mins, -64.0f, -64.0f, 0.0f);
    VectorSet(aasworld.areas[2].maxs, 192.0f, 64.0f, 64.0f);
    VectorSet(aasworld.areas[3].mins, 192.0f, -64.0f, 0.0f);
    VectorSet(aasworld.areas[3].maxs, 320.0f, 64.0f, 64.0f);

    fixture->area_settings[1].firstreachablearea = 1;
    fixture->area_settings[1].numreachableareas = 1;
    fixture->area_settings[2].firstreachablearea = 2;
    fixture->area_settings[2].numreachableareas = 1;

    fixture->reachability[1].areanum = 2;
    fixture->reachability[1].traveltype = TRAVEL_WALK;
    VectorClear(fixture->reachability[1].start);
    VectorSet(fixture->reachability[1].end, 128.0f, 0.0f, 32.0f);
    fixture->reachability[1].facenum = 0; /* ensure no mover reach is registered */

    fixture->reachability[2].areanum = 3;
    fixture->reachability[2].traveltype = TRAVEL_WALK;
    VectorSet(fixture->reachability[2].start, 128.0f, 0.0f, 32.0f);
    VectorSet(fixture->reachability[2].end, 256.0f, 0.0f, 32.0f);

    AAS_InitTravelFlagFromType();
    assert_int_equal(AAS_PrepareReachability(), BLERR_NOERROR);
}

/*
=============
bot_mover_fixture_shutdown

Releases mover fixture state through the AAS owner after the fixture arrays have
been installed into aasworld.
=============
*/
static void bot_mover_fixture_shutdown(bot_mover_fixture_t *fixture)
{
	if (fixture == NULL)
	{
		return;
	}

	AAS_Shutdown();
	memset(fixture, 0, sizeof(*fixture));

	BotMove_MoverCatalogueReset();
}

static void test_bot_interface_mover_parity(void **state)
{
    bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;

    Mock_Reset(&context->mock);

    int status = context->api->BotSetupLibrary();
    assert_int_equal(status, BLERR_NOERROR);

    bot_mover_fixture_t fixture;
    memset(&fixture, 0, sizeof(fixture));
    bot_mover_fixture_init(&fixture);

    LibVarSet("bot_developer", "1");

    BotMove_MoverCatalogueReset();
    bot_mover_catalogue_entry_t mover_entry = {
        .modelnum = 8,
        .lip = 0.0f,
        .height = 0.0f,
        .speed = 0.0f,
        .spawnflags = 0,
        .doortype = 0,
        .kind = BOT_MOVER_KIND_FUNC_PLAT,
    };
    assert_true(BotMove_MoverCatalogueInsert(&mover_entry));

    char model_name[] = "*8";
    char *model_entries[] = {model_name};
    botinterface_asset_list_t asset_models = {
        .entries = model_entries,
        .count = ARRAY_LEN(model_entries),
    };
    assert_true(BotMove_MoverCatalogueFinalize(&asset_models));

    bot_updateentity_t mover_update;
    memset(&mover_update, 0, sizeof(mover_update));
    VectorSet(mover_update.origin, 64.0f, 0.0f, 16.0f);
    VectorSet(mover_update.old_origin, 64.0f, 0.0f, 16.0f);
    VectorSet(mover_update.mins, -32.0f, -32.0f, -16.0f);
    VectorSet(mover_update.maxs, 32.0f, 32.0f, 16.0f);
    mover_update.solid = SOLID_BSP;
    mover_update.modelindex = mover_entry.modelnum + 1;

    status = context->api->BotUpdateEntity(3, &mover_update);
    assert_int_equal(status, BLERR_NOERROR);

    Mock_AssertPrintContains(&context->mock, "relinking brush model", PRT_MESSAGE);
    Mock_Reset(&context->mock);

    bot_settings_t settings;
    memset(&settings, 0, sizeof(settings));
    snprintf(settings.characterfile, sizeof(settings.characterfile), "bots/babe_c.c");
    snprintf(settings.charactername, sizeof(settings.charactername), "babe");
    status = context->api->BotSetupClient(1, &settings);
    assert_true(status);

    bot_client_state_t *client_state = BotState_Get(1);
    assert_non_null(client_state);

    bot_goal_t mover_goal;
    memset(&mover_goal, 0, sizeof(mover_goal));
    mover_goal.number = 42;
    mover_goal.areanum = 3;
    VectorSet(mover_goal.origin, 256.0f, 0.0f, 32.0f);

    status = context->api->BotPushGoal(client_state->goal_handle, &mover_goal);
    assert_true(status != 0);

    status = context->api->BotStartFrame(0.1f);
    assert_int_equal(status, BLERR_NOERROR);

    status = context->api->BotUpdateEntity(3, &mover_update);
    assert_int_equal(status, BLERR_NOERROR);

    bot_updateentity_t client_entity_update;
    memset(&client_entity_update, 0, sizeof(client_entity_update));
    VectorSet(client_entity_update.origin, 64.0f, 0.0f, 32.0f);
    VectorSet(client_entity_update.old_origin, 64.0f, 0.0f, 32.0f);
    VectorSet(client_entity_update.mins, -16.0f, -16.0f, -24.0f);
    VectorSet(client_entity_update.maxs, 16.0f, 16.0f, 32.0f);
    client_entity_update.solid = SOLID_BBOX;

    status = context->api->BotUpdateEntity(1, &client_entity_update);
    assert_int_equal(status, BLERR_NOERROR);

    bot_updateclient_t update;
    memset(&update, 0, sizeof(update));
    VectorSet(update.origin, 64.0f, 0.0f, 32.0f);
    update.pm_flags = PMF_ON_GROUND;
    update.viewangles[1] = 90.0f;
    update.stats[STAT_HEALTH] = 100;
    for (int i = 0; i < MAX_ITEMS; ++i)
    {
        update.inventory[i] = 1;
    }

    status = context->api->BotUpdateClient(1, &update);
    assert_int_equal(status, BLERR_NOERROR);

    status = context->api->BotAI(1, 0.05f);
    assert_int_equal(status, BLERR_INVALIDIMPORT);

    Mock_AssertPrintContains(&context->mock, "without reachability", PRT_MESSAGE);

    BotDumpAvoidGoals(client_state->goal_handle);
    Mock_AssertPrintContains(&context->mock, "BotDumpAvoidGoals", PRT_MESSAGE);

    ai_avoid_list_t *avoid = AI_GoalState_GetAvoidList(client_state->goal_state);
    assert_non_null(avoid);
    assert_true(AI_AvoidList_Contains(avoid, mover_goal.number, 0.1f));

    Mock_Reset(&context->mock);

    fixture.reachability[1].traveltype = TRAVEL_ELEVATOR;
    fixture.reachability[1].facenum = mover_entry.modelnum;
    AAS_InvalidateRouteCache();

    status = context->api->BotStartFrame(6.0f);
    assert_int_equal(status, BLERR_NOERROR);

    status = context->api->BotUpdateEntity(3, &mover_update);
    assert_int_equal(status, BLERR_NOERROR);

    status = context->api->BotUpdateEntity(1, &client_entity_update);
    assert_int_equal(status, BLERR_NOERROR);

    status = context->api->BotUpdateClient(1, &update);
    assert_int_equal(status, BLERR_NOERROR);

    status = context->api->BotAI(1, 0.05f);
    assert_int_equal(status, BLERR_NOERROR);

    assert_true(context->mock.bot_input_count > 0U);
    const bot_input_t *final_input =
        &context->mock.inputs[context->mock.bot_input_count - 1U];
    assert_float_equal(final_input->thinktime, 0.05f, 0.0001f);

    assert_true(client_state->has_move_result);
    assert_int_equal(client_state->last_move_result.traveltype, TRAVEL_ELEVATOR);
    assert_int_equal(client_state->last_move_result.type, RESULTTYPE_ELEVATORUP);
    assert_true((client_state->last_move_result.flags & MOVERESULT_WAITING) != 0);

    assert_float_equal(final_input->speed, 0.0f, 0.0001f);
    assert_float_equal(final_input->dir[0], -1.0f, 0.0001f);
    assert_float_equal(final_input->dir[1], 0.0f, 0.0001f);
    assert_float_equal(final_input->dir[2], 0.0f, 0.0001f);
    assert_int_equal(final_input->actionflags, 0);

    context->api->BotShutdownClient(1);
    context->api->BotShutdownLibrary();

    bot_mover_fixture_shutdown(&fixture);
    LibVarSet("bot_developer", "0");
}

/*
=============
test_battle_inventory_powerup_timers_and_power_armor

Pins sub_10021020's signed health copy, image-name timer refreshes, truncating
countdowns, power-armor grace window, and untouched-slot behaviour.
=============
*/
static void test_battle_inventory_powerup_timers_and_power_armor(void **state)
{
	bot_interface_test_context_t *context =
		(bot_interface_test_context_t *)*state;
	Mock_Reset(&context->mock);

	assert_int_equal(context->api->BotSetupLibrary(), BLERR_NOERROR);
	char *images[] = {
		"",
		"P_QUAD",
		"p_invulnerability",
		"p_rebreather",
		"p_envirosuit",
		"i_powershield",
		"i_jacketarmor",
	};
	assert_int_equal(context->api->BotLoadMap(NULL,
		0,
		NULL,
		0,
		NULL,
		(int)ARRAY_LEN(images),
		images),
		BLERR_NOERROR);

	bot_settings_t settings;
	memset(&settings, 0, sizeof(settings));
	snprintf(settings.characterfile,
		sizeof(settings.characterfile),
		"bots/babe_c.c");
	snprintf(settings.charactername,
		sizeof(settings.charactername),
		"babe");
	assert_true(context->api->BotSetupClient(1, &settings));

	bot_updateclient_t update;
	memset(&update, 0, sizeof(update));
	update.pm_type = PM_NORMAL;
	update.pm_flags = PMF_ON_GROUND | PMF_TIME_WATERJUMP;
	update.pm_time = 1;
	update.stats[STAT_HEALTH] = 137;
	update.stats[STAT_TIMER_ICON] = 1;
	update.stats[STAT_TIMER] = 5;
	update.stats[STAT_ARMOR_ICON] = 5;
	update.inventory[RETAIL_INVENTORY_CELLS] = 37;
	update.inventory[RETAIL_USING_SILENCER] = 606;
	update.inventory[RETAIL_USING_ANCIENTHEAD] = 609;
	update.inventory[RETAIL_ENEMY_HORIZONTAL_DIST] = 8200;
	update.inventory[RETAIL_ENEMY_HEIGHT] = 8201;
	for (int slot = RETAIL_ENEMY_BLASTER; slot <= RETAIL_ENEMY_GRAPPLE; ++slot)
	{
		update.inventory[slot] = 8000 + slot;
	}
	for (int slot = RETAIL_ENEMY_QUAD; slot <= RETAIL_ENEMY_POWERSCREEN; ++slot)
	{
		update.inventory[slot] = 8000 + slot;
	}
	const int untouched_slots[] = {202, 203, 212, 213, 214, 242, 243, 244, 248};
	for (size_t index = 0; index < ARRAY_LEN(untouched_slots); ++index)
	{
		update.inventory[untouched_slots[index]] = 700 + (int)index;
	}

	assert_int_equal(context->api->BotStartFrame(10.0f), BLERR_NOERROR);
	assert_int_equal(context->api->BotUpdateClient(1, &update), BLERR_NOERROR);
	assert_int_equal(context->api->BotAI(1, 0.05f), BLERR_NOERROR);

	bot_client_state_t *client = BotState_Get(1);
	assert_non_null(client);
	int *inventory = client->last_client_update.inventory;
	assert_int_equal(inventory[RETAIL_INVENTORY_HEALTH], 137);
	assert_int_equal(inventory[RETAIL_USING_QUAD], 5);
	assert_int_equal(inventory[RETAIL_USING_INVULNERABILITY], 0);
	assert_int_equal(inventory[RETAIL_USING_REBREATHER], 0);
	assert_int_equal(inventory[RETAIL_USING_ENVIRONMENTSUIT], 0);
	assert_int_equal(inventory[RETAIL_USING_POWERSCREEN], 37);
	assert_int_equal(inventory[RETAIL_USING_POWERSHIELD], 37);
	assert_int_equal(inventory[RETAIL_USING_SILENCER], 606);
	assert_int_equal(inventory[RETAIL_USING_ANCIENTHEAD], 609);
	assert_float_equal(client->quad_time, 15.0f, 0.0001f);
	assert_float_equal(client->power_armor_time, 10.0f, 0.0001f);
	assert_int_equal(client->last_client_update.pm_flags, update.pm_flags);
	assert_int_equal(inventory[RETAIL_ENEMY_HORIZONTAL_DIST], 8200);
	assert_int_equal(inventory[RETAIL_ENEMY_HEIGHT], 8201);
	for (int slot = RETAIL_ENEMY_BLASTER; slot <= RETAIL_ENEMY_GRAPPLE; ++slot)
	{
		assert_int_equal(inventory[slot], 8000 + slot);
	}
	for (int slot = RETAIL_ENEMY_QUAD; slot <= RETAIL_ENEMY_POWERSCREEN; ++slot)
	{
		assert_int_equal(inventory[slot], 8000 + slot);
	}
	for (size_t index = 0; index < ARRAY_LEN(untouched_slots); ++index)
	{
		assert_int_equal(inventory[untouched_slots[index]], 700 + (int)index);
	}

	update.stats[STAT_TIMER_ICON] = 2;
	update.stats[STAT_TIMER] = 7;
	update.stats[STAT_ARMOR_ICON] = 6;
	update.inventory[RETAIL_INVENTORY_CELLS] = 42;
	assert_int_equal(context->api->BotStartFrame(10.5f), BLERR_NOERROR);
	assert_int_equal(context->api->BotUpdateClient(1, &update), BLERR_NOERROR);
	assert_int_equal(context->api->BotAI(1, 0.05f), BLERR_NOERROR);
	inventory = client->last_client_update.inventory;
	assert_int_equal(inventory[RETAIL_USING_QUAD], 4);
	assert_int_equal(inventory[RETAIL_USING_INVULNERABILITY], 7);
	assert_int_equal(inventory[RETAIL_USING_POWERSCREEN], 42);
	assert_int_equal(inventory[RETAIL_USING_POWERSHIELD], 42);
	assert_float_equal(client->invulnerability_time, 17.5f, 0.0001f);

	update.stats[STAT_TIMER_ICON] = 3;
	update.stats[STAT_TIMER] = 8;
	update.inventory[RETAIL_INVENTORY_CELLS] = 44;
	assert_int_equal(context->api->BotStartFrame(10.91f), BLERR_NOERROR);
	assert_int_equal(context->api->BotUpdateClient(1, &update), BLERR_NOERROR);
	assert_int_equal(context->api->BotAI(1, 0.05f), BLERR_NOERROR);
	inventory = client->last_client_update.inventory;
	assert_int_equal(inventory[RETAIL_USING_REBREATHER], 8);
	assert_int_equal(inventory[RETAIL_USING_POWERSCREEN], 0);
	assert_int_equal(inventory[RETAIL_USING_POWERSHIELD], 0);
	assert_float_equal(client->rebreather_time, 18.91f, 0.0001f);

	update.stats[STAT_TIMER_ICON] = 4;
	update.stats[STAT_TIMER] = 9;
	update.stats[STAT_ARMOR_ICON] = 0;
	update.inventory[RETAIL_USING_POWERSCREEN] = 91;
	update.inventory[RETAIL_USING_POWERSHIELD] = 92;
	assert_int_equal(context->api->BotStartFrame(11.25f), BLERR_NOERROR);
	assert_int_equal(context->api->BotUpdateClient(1, &update), BLERR_NOERROR);
	assert_int_equal(context->api->BotAI(1, 0.05f), BLERR_NOERROR);
	inventory = client->last_client_update.inventory;
	assert_int_equal(inventory[RETAIL_USING_QUAD], 3);
	assert_int_equal(inventory[RETAIL_USING_INVULNERABILITY], 6);
	assert_int_equal(inventory[RETAIL_USING_REBREATHER], 7);
	assert_int_equal(inventory[RETAIL_USING_ENVIRONMENTSUIT], 9);
	assert_int_equal(inventory[RETAIL_USING_POWERSCREEN], 91);
	assert_int_equal(inventory[RETAIL_USING_POWERSHIELD], 92);
	assert_float_equal(client->environmentsuit_time, 20.25f, 0.0001f);

	update.stats[STAT_HEALTH] = -7;
	update.stats[STAT_TIMER_ICON] = 99;
	update.stats[STAT_TIMER] = 30;
	update.stats[STAT_ARMOR_ICON] = 6;
	assert_int_equal(context->api->BotStartFrame(30.0f), BLERR_NOERROR);
	assert_int_equal(context->api->BotUpdateClient(1, &update), BLERR_NOERROR);
	assert_int_equal(context->api->BotAI(1, 0.05f), BLERR_NOERROR);
	inventory = client->last_client_update.inventory;
	assert_int_equal(inventory[RETAIL_INVENTORY_HEALTH], -7);
	assert_int_equal(inventory[RETAIL_USING_QUAD], 0);
	assert_int_equal(inventory[RETAIL_USING_INVULNERABILITY], 0);
	assert_int_equal(inventory[RETAIL_USING_REBREATHER], 0);
	assert_int_equal(inventory[RETAIL_USING_ENVIRONMENTSUIT], 0);
	assert_int_equal(inventory[RETAIL_USING_POWERSCREEN], 0);
	assert_int_equal(inventory[RETAIL_USING_POWERSHIELD], 0);
	assert_int_equal(inventory[RETAIL_USING_SILENCER], 606);
	assert_int_equal(inventory[RETAIL_USING_ANCIENTHEAD], 609);
	assert_int_equal(client->last_client_update.pm_flags, update.pm_flags);
	assert_int_equal(inventory[RETAIL_ENEMY_HORIZONTAL_DIST], 8200);
	assert_int_equal(inventory[RETAIL_ENEMY_HEIGHT], 8201);
	for (int slot = RETAIL_ENEMY_BLASTER; slot <= RETAIL_ENEMY_GRAPPLE; ++slot)
	{
		assert_int_equal(inventory[slot], 8000 + slot);
	}
	for (int slot = RETAIL_ENEMY_QUAD; slot <= RETAIL_ENEMY_POWERSCREEN; ++slot)
	{
		assert_int_equal(inventory[slot], 8000 + slot);
	}
	for (size_t index = 0; index < ARRAY_LEN(untouched_slots); ++index)
	{
		assert_int_equal(inventory[untouched_slots[index]], 700 + (int)index);
	}

	BotState_ResetForNewMap(client);
	assert_float_equal(client->power_armor_time, 0.0f, 0.0001f);
	assert_float_equal(client->quad_time, 0.0f, 0.0001f);
	assert_float_equal(client->invulnerability_time, 0.0f, 0.0001f);
	assert_float_equal(client->rebreather_time, 0.0f, 0.0001f);
	assert_float_equal(client->environmentsuit_time, 0.0f, 0.0001f);

	assert_int_equal(context->api->BotShutdownClient(1), BLERR_NOERROR);
	assert_int_equal(context->api->BotShutdownLibrary(), BLERR_NOERROR);
}

/*
=============
test_enemy_battle_inventory_weapon_and_effect_projection

Pins sub_10021290's second skinnum byte, all twelve one-hot destinations,
effect booleans, displacement truncation, and intentionally untouched gaps.
=============
*/
static void test_enemy_battle_inventory_weapon_and_effect_projection(void **state)
{
	bot_interface_test_context_t *context =
		(bot_interface_test_context_t *)*state;
	Mock_Reset(&context->mock);
	assert_int_equal(context->api->BotLibVarSet("maxclients", "4"),
		BLERR_NOERROR);
	assert_int_equal(context->api->BotSetupLibrary(), BLERR_NOERROR);

	bot_mover_fixture_t fixture;
	memset(&fixture, 0, sizeof(fixture));
	bot_mover_fixture_init(&fixture);

	bot_settings_t settings;
	memset(&settings, 0, sizeof(settings));
	snprintf(settings.characterfile,
		sizeof(settings.characterfile),
		"bots/babe_c.c");
	snprintf(settings.charactername,
		sizeof(settings.charactername),
		"babe");
	assert_true(context->api->BotSetupClient(1, &settings));

	bot_updateclient_t update;
	memset(&update, 0, sizeof(update));
	update.pm_type = PM_NORMAL;
	update.pm_flags = PMF_ON_GROUND | PMF_TIME_WATERJUMP;
	update.pm_time = 1;
	update.stats[STAT_HEALTH] = 100;
	VectorSet(update.origin, 0.0f, 0.0f, 32.0f);
	VectorSet(update.viewangles, -45.0f, 53.130102f, 0.0f);
	update.inventory[242] = 742;
	update.inventory[243] = 743;
	update.inventory[244] = 744;
	update.inventory[RETAIL_ENEMY_POWERSHIELD] = 748;

	bot_updateentity_t enemy;
	memset(&enemy, 0, sizeof(enemy));
	VectorSet(enemy.origin, 3.0f, 4.0f, 27.0f);
	VectorCopy(enemy.origin, enemy.old_origin);

	const int expected_slots[] = {
		230, 231, 232, 233, 234, 240,
		235, 236, 237, 238, 239, 241,
	};
	bot_client_state_t *client = BotState_Get(1);
	assert_non_null(client);

	for (int weapon = 1; weapon <= 12; ++weapon)
	{
		for (int slot = RETAIL_ENEMY_BLASTER;
			slot <= RETAIL_ENEMY_GRAPPLE;
			++slot)
		{
			update.inventory[slot] = 900 + slot;
		}
		update.inventory[RETAIL_ENEMY_QUAD] = 945;
		update.inventory[RETAIL_ENEMY_INVULNERABILITY] = 946;
		update.inventory[RETAIL_ENEMY_POWERSCREEN] = 947;
		enemy.skinnum = 0x44005a | (weapon << 8);
		enemy.effects = weapon == 1
			? EF_QUAD | EF_PENT | EF_POWERSCREEN
			: 0;

		float frame_time = 20.0f + (float)weapon * 0.1f;
		assert_int_equal(context->api->BotStartFrame(frame_time),
			BLERR_NOERROR);
		assert_int_equal(context->api->BotUpdateEntity(2, &enemy),
			BLERR_NOERROR);
		assert_int_equal(context->api->BotUpdateClient(1, &update),
			BLERR_NOERROR);
		assert_true(BotAI_UpdateEnemyBattleInventory(client, 2));

		int *inventory = client->last_client_update.inventory;
		assert_int_equal(inventory[RETAIL_ENEMY_HORIZONTAL_DIST], 5);
		assert_int_equal(inventory[RETAIL_ENEMY_HEIGHT], -5);
		for (int slot = RETAIL_ENEMY_BLASTER;
			slot <= RETAIL_ENEMY_GRAPPLE;
			++slot)
		{
			assert_int_equal(inventory[slot],
				slot == expected_slots[weapon - 1] ? 1 : 0);
		}
		assert_int_equal(inventory[RETAIL_ENEMY_QUAD], weapon == 1);
		assert_int_equal(inventory[RETAIL_ENEMY_INVULNERABILITY],
			weapon == 1);
		assert_int_equal(inventory[RETAIL_ENEMY_POWERSCREEN], weapon == 1);
		assert_int_equal(inventory[242], 742);
		assert_int_equal(inventory[243], 743);
		assert_int_equal(inventory[244], 744);
		assert_int_equal(inventory[RETAIL_ENEMY_POWERSHIELD], 748);
		assert_int_equal(client->last_client_update.pm_flags, update.pm_flags);
	}

	enemy.skinnum = 0x44005a;
	enemy.effects = 0;
	for (int slot = RETAIL_ENEMY_BLASTER;
		slot <= RETAIL_ENEMY_GRAPPLE;
		++slot)
	{
		update.inventory[slot] = 800 + slot;
	}
	assert_int_equal(context->api->BotStartFrame(22.0f), BLERR_NOERROR);
	assert_int_equal(context->api->BotUpdateEntity(2, &enemy), BLERR_NOERROR);
	assert_int_equal(context->api->BotUpdateClient(1, &update), BLERR_NOERROR);
	assert_true(BotAI_UpdateEnemyBattleInventory(client, 2));
	for (int slot = RETAIL_ENEMY_BLASTER;
		slot <= RETAIL_ENEMY_GRAPPLE;
		++slot)
	{
		assert_int_equal(client->last_client_update.inventory[slot], 0);
	}
	assert_int_equal(client->last_client_update.inventory[RETAIL_ENEMY_QUAD], 0);
	assert_int_equal(client->last_client_update.inventory[RETAIL_ENEMY_INVULNERABILITY], 0);
	assert_int_equal(client->last_client_update.inventory[RETAIL_ENEMY_POWERSCREEN], 0);
	assert_int_equal(client->last_client_update.inventory[RETAIL_ENEMY_POWERSHIELD], 748);

	assert_int_equal(context->api->BotShutdownClient(1), BLERR_NOERROR);
	assert_int_equal(context->api->BotShutdownLibrary(), BLERR_NOERROR);
	bot_mover_fixture_shutdown(&fixture);
}

static bool ensure_map_fixture(const asset_env_t *assets, const char *stem)
{
    if (assets == NULL || stem == NULL)
    {
        return false;
    }

    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/maps/%s.bsp", assets->asset_root, stem);
    FILE *file = fopen(path, "rb");
    if (file == NULL)
    {
        print_message("bot interface parity skipped: missing %s\n", path);
        return false;
    }
    fclose(file);

    snprintf(path, sizeof(path), "%s/maps/%s.aas", assets->asset_root, stem);
    file = fopen(path, "rb");
    if (file == NULL)
    {
        print_message("bot interface parity skipped: missing %s\n", path);
        return false;
    }
    fclose(file);

    return true;
}

static bool ensure_mover_fixture(const asset_env_t *assets)
{
    return ensure_map_fixture(assets, "test_mover");
}

static bool find_mover_area_center(vec3_t center_out, int *area_out)
{
    if (!aasworld.loaded || aasworld.areasettings == NULL || aasworld.areas == NULL)
    {
        return false;
    }

    for (int area = 1; area <= aasworld.numAreas; ++area)
    {
        if ((aasworld.areasettings[area].contents & AREACONTENTS_MOVER) == 0)
        {
            continue;
        }

        if (center_out != NULL)
        {
            VectorCopy(aasworld.areas[area].center, center_out);
        }

        if (area_out != NULL)
        {
            *area_out = area;
        }

        return true;
    }

    return false;
}

static void assert_success_status(const botlib_contract_catalogue_t *catalogue,
                                  const char *export_name,
                                  int status)
{
    const botlib_contract_export_t *export_entry =
        BotlibContract_FindExport(catalogue, export_name);
    assert_non_null(export_entry);

    const botlib_contract_scenario_t *scenario =
        BotlibContract_FindScenario(export_entry, "success");
    if (scenario == NULL)
    {
        scenario = BotlibContract_FindScenario(export_entry, NULL);
    }
    assert_non_null(scenario);

    const botlib_contract_return_code_t *expected =
        BotlibContract_FindReturnCode(scenario, status);

    if (expected != NULL)
    {
        assert_int_equal(status, expected->value);
    }
    else
    {
        assert_int_equal(status, BLERR_NOERROR);
    }
}

static void test_bot_end_to_end_pipeline_with_assets(void **state)
{
    bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;

    if (!ensure_map_fixture(&context->assets, "2box4"))
    {
        cmocka_skip();
    }

    Mock_Reset(&context->mock);

    int status = context->api->BotSetupLibrary();
    assert_success_status(&context->catalogue, "BotLibSetup", status);
    assert_non_null(Mock_FindPrint(&context->mock, "------- BotLib Initialization -------"));

    status = context->api->BotLoadMap("maps/2box4.bsp", 0, NULL, 0, NULL, 0, NULL);
    assert_success_status(&context->catalogue, "BotLibLoadMap", status);
    assert_true(aasworld.loaded);

    bot_settings_t settings;
    memset(&settings, 0, sizeof(settings));
    snprintf(settings.characterfile, sizeof(settings.characterfile), "bots/babe_c.c");
    snprintf(settings.charactername, sizeof(settings.charactername), "babe");
    settings.ailibrary[0] = '\0';

    status = context->api->BotSetupClient(1, &settings);
    assert_true(status);

    status = context->api->BotConsoleMessage(1, CMS_CHAT, "hello gladiator");
    assert_success_status(&context->catalogue, "BotLibConsoleMessage", status);

    bot_client_state_t *client_state = BotState_Get(1);
    assert_non_null(client_state);
    assert_non_null(client_state->chat_state);
    assert_int_equal(BotNumConsoleMessages(client_state->chat_state), 1);

    status = context->api->BotStartFrame(0.1f);
    assert_success_status(&context->catalogue, "BotLibStartFrame", status);

    bot_updateclient_t update;
    memset(&update, 0, sizeof(update));
    update.viewangles[1] = 45.0f;

    status = context->api->BotUpdateClient(1, &update);
    assert_int_equal(status, BLERR_NOERROR);

    status = context->api->BotAI(1, 0.05f);
    assert_int_equal(status, BLERR_NOERROR);
    assert_true(context->mock.bot_input_count > 0);
    assert_int_equal(context->mock.input_clients[0], 1);
    assert_float_equal(context->mock.inputs[0].thinktime, 0.05f, 0.0001f);

    context->api->BotShutdownClient(1);
}

static void test_bot_bridge_tracks_mover_entity_updates(void **state)
{
    bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;

    if (!ensure_mover_fixture(&context->assets))
    {
        cmocka_skip();
    }

    Mock_Reset(&context->mock);

    int status = context->api->BotSetupLibrary();
    assert_int_equal(status, BLERR_NOERROR);

    status = context->api->BotLoadMap("maps/test_mover.bsp", 0, NULL, 0, NULL, 0, NULL);
    assert_int_equal(status, BLERR_NOERROR);
    assert_true(aasworld.loaded);

    vec3_t mover_center;
    int mover_areanum = 0;
    assert_true(find_mover_area_center(mover_center, &mover_areanum));
    assert_true(mover_areanum > 0);

    const bot_mover_catalogue_entry_t *mover_entry = NULL;
    int mover_modelnum = 0;
    for (int candidate = 1; candidate < 64; ++candidate)
    {
        mover_entry = BotMove_MoverCatalogueFindByModel(candidate);
        if (mover_entry != NULL)
        {
            mover_modelnum = candidate;
            break;
        }
    }
    assert_non_null(mover_entry);
    assert_true(BotMove_MoverCatalogueIsModelMover(mover_modelnum));

    bot_settings_t settings;
    memset(&settings, 0, sizeof(settings));
    snprintf(settings.characterfile, sizeof(settings.characterfile), "bots/babe_c.c");
    snprintf(settings.charactername, sizeof(settings.charactername), "babe");
    status = context->api->BotSetupClient(1, &settings);
    assert_true(status);

    bot_updateclient_t client_update;
    memset(&client_update, 0, sizeof(client_update));
    client_update.pm_type = PM_NORMAL;
    VectorCopy(mover_center, client_update.origin);
    client_update.origin[2] = aasworld.areas[mover_areanum].maxs[2] + 24.0f;
    client_update.pm_flags = 0;
    client_update.stats[STAT_HEALTH] = 100;
    for (int item = 0; item < MAX_ITEMS; ++item)
    {
        client_update.inventory[item] = 1;
    }

    status = context->api->BotStartFrame(0.0f);
    assert_int_equal(status, BLERR_NOERROR);

    bot_updateentity_t mover_update;
    memset(&mover_update, 0, sizeof(mover_update));
    VectorCopy(mover_center, mover_update.origin);
    mover_update.origin[2] = aasworld.areas[mover_areanum].mins[2];
    VectorCopy(mover_update.origin, mover_update.old_origin);
    VectorSet(mover_update.mins, -16.0f, -16.0f, -8.0f);
    VectorSet(mover_update.maxs, 16.0f, 16.0f, 8.0f);
    mover_update.solid = 3;
    mover_update.modelindex = mover_modelnum + 1;

    const int mover_entity_num = 32;
    status = context->api->BotUpdateEntity(mover_entity_num, &mover_update);
    assert_int_equal(status, BLERR_NOERROR);

    status = context->api->BotUpdateClient(1, &client_update);
    assert_int_equal(status, BLERR_NOERROR);

    status = context->api->BotStartFrame(0.2f);
    assert_int_equal(status, BLERR_NOERROR);

    bot_updateentity_t mover_raised = mover_update;
    mover_raised.old_origin[2] = mover_update.origin[2];
    mover_raised.origin[2] = aasworld.areas[mover_areanum].maxs[2];
    status = context->api->BotUpdateEntity(mover_entity_num, &mover_raised);
    assert_int_equal(status, BLERR_NOERROR);

    bot_updateclient_t client_on_mover = client_update;
    client_on_mover.origin[2] = mover_raised.origin[2];
    client_on_mover.pm_flags = PMF_ON_GROUND;
    status = context->api->BotUpdateClient(1, &client_on_mover);
    assert_int_equal(status, BLERR_NOERROR);

    status = context->api->BotAI(1, 0.05f);
    assert_int_equal(status, BLERR_NOERROR);
    assert_true(context->mock.bot_input_count > 0);

    AASEntityFrame mover_frame;
    memset(&mover_frame, 0, sizeof(mover_frame));
    assert_true(Bridge_ReadEntityFrame(mover_entity_num, &mover_frame));
    assert_true(mover_frame.is_mover);
    assert_true(mover_frame.origin_dirty);
    assert_float_equal(mover_frame.origin[2], mover_raised.origin[2], 0.0001f);
    assert_float_equal(mover_frame.previous_origin[2], mover_update.origin[2], 0.0001f);
    assert_float_equal(mover_frame.frame_delta, 0.2f, 0.0001f);

    AASClientFrame client_frame;
    memset(&client_frame, 0, sizeof(client_frame));
    assert_true(Bridge_ReadClientFrame(1, &client_frame));
    assert_float_equal(client_frame.origin[2], client_on_mover.origin[2], 0.0001f);
    assert_float_equal(client_frame.last_update_time, 0.2f, 0.0001f);
    assert_float_equal(client_frame.frame_delta, 0.2f, 0.0001f);

    assert_int_equal(AAS_ModelNumForEntity(mover_entity_num), mover_modelnum);

    context->api->BotShutdownClient(1);
}

static void test_bot_start_frame_entity_lifecycle(void **state)
{
    bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;

	if (!ensure_map_fixture(&context->assets, "test2"))
	{
		cmocka_skip();
	}

    Mock_Reset(&context->mock);

    int status = context->api->BotSetupLibrary();
    assert_int_equal(status, BLERR_NOERROR);

    status = context->api->BotLoadMap("maps/test2.bsp", 0, NULL, 0, NULL, 0, NULL);
    assert_int_equal(status, BLERR_NOERROR);
    assert_true(aasworld.loaded);

    status = context->api->BotStartFrame(0.0f);
    assert_int_equal(status, BLERR_NOERROR);

    bot_updateentity_t entity;
    memset(&entity, 0, sizeof(entity));
    entity.solid = 31;
    entity.modelindex = 2;
    VectorSet(entity.origin, 32.0f, 24.0f, 40.0f);
    VectorCopy(entity.origin, entity.old_origin);
    VectorSet(entity.mins, -16.0f, -16.0f, -16.0f);
    VectorSet(entity.maxs, 16.0f, 16.0f, 16.0f);

    const int entityNum = 7;
    status = context->api->BotUpdateEntity(entityNum, &entity);
    assert_int_equal(status, BLERR_NOERROR);
    assert_true(aasworld.entitiesValid);
    assert_non_null(aasworld.entities);
    assert_true(aasworld.entities[entityNum].inuse);

    const aas_entity_t *tracked = &aasworld.entities[entityNum];
    assert_non_null(tracked->areas);

    status = context->api->BotStartFrame(0.1f);
    assert_int_equal(status, BLERR_NOERROR);
    assert_false(aasworld.entities[entityNum].inuse);
    assert_non_null(aasworld.entities[entityNum].areas);
    assert_false(aasworld.entitiesValid);

    status = context->api->BotStartFrame(0.2f);
    assert_int_equal(status, BLERR_NOERROR);
    assert_null(aasworld.entities[entityNum].areas);
    assert_true(aasworld.entities[entityNum].outsideAllAreas);
    assert_false(aasworld.entitiesValid);
    assert_int_equal(aasworld.numFrames, 3);

    context->api->BotShutdownLibrary();
}

static void test_bot_start_frame_updates_routing_diagnostics(void **state)
{
    bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;

	if (!ensure_map_fixture(&context->assets, "test2"))
	{
		cmocka_skip();
	}

    Mock_Reset(&context->mock);

    int status = context->api->BotSetupLibrary();
    assert_int_equal(status, BLERR_NOERROR);

    status = context->api->BotLoadMap("maps/test2.bsp", 0, NULL, 0, NULL, 0, NULL);
    assert_int_equal(status, BLERR_NOERROR);
    assert_true(aasworld.loaded);

    AAS_RouteFrameResetDiagnostics();
    AAS_ReachabilityFrameResetDiagnostics();

    status = context->api->BotLibVarSet("framereachability", "0");
    assert_int_equal(status, BLERR_NOERROR);
    status = context->api->BotLibVarSet("forcewrite", "0");
    assert_int_equal(status, BLERR_NOERROR);
    status = context->api->BotLibVarSet("forcereachability", "0");
    assert_int_equal(status, BLERR_NOERROR);
    status = context->api->BotLibVarSet("forceclustering", "0");
    assert_int_equal(status, BLERR_NOERROR);

    status = context->api->BotStartFrame(0.0f);
    assert_int_equal(status, BLERR_NOERROR);
    assert_int_equal(AAS_RouteFrameSkipCounter(), 1);
    assert_int_equal(AAS_RouteFrameWorkCounter(), 0);
    assert_int_equal(AAS_RouteFrameLastBudget(), 0);
    assert_false(AAS_RouteFrameForceWriteActive());
    assert_int_equal(AAS_ReachabilityFrameSkipCounter(), 1);
    assert_int_equal(AAS_ReachabilityFrameWorkCounter(), 0);
    assert_false(AAS_ReachabilityForceReachabilityActive());
    assert_false(AAS_ReachabilityForceClusteringActive());

    status = context->api->BotLibVarSet("framereachability", "16");
    assert_int_equal(status, BLERR_NOERROR);
    status = context->api->BotLibVarSet("forcewrite", "1");
    assert_int_equal(status, BLERR_NOERROR);
    status = context->api->BotLibVarSet("forcereachability", "1");
    assert_int_equal(status, BLERR_NOERROR);
    status = context->api->BotLibVarSet("forceclustering", "1");
    assert_int_equal(status, BLERR_NOERROR);

    status = context->api->BotStartFrame(0.2f);
    assert_int_equal(status, BLERR_NOERROR);
    assert_int_equal(AAS_RouteFrameSkipCounter(), 1);
    assert_int_equal(AAS_RouteFrameWorkCounter(), 1);
    assert_int_equal(AAS_RouteFrameLastBudget(), 16);
    assert_true(AAS_RouteFrameForceWriteActive());
    assert_int_equal(AAS_ReachabilityFrameSkipCounter(), 1);
    assert_int_equal(AAS_ReachabilityFrameWorkCounter(), 1);
    assert_true(AAS_ReachabilityForceReachabilityActive());
    assert_true(AAS_ReachabilityForceClusteringActive());

    context->api->BotShutdownLibrary();
}

/*
=============
test_bot_add_avoid_spot_export_wires_move_state

Verifies the Quake III movement avoid-spot export is present and mutates the
underlying reconstructed move state.
=============
*/
static void test_bot_add_avoid_spot_export_wires_move_state(void **state)
{
	bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;

	assert_non_null(context->api->BotAddAvoidSpot);

	int status = context->api->BotSetupLibrary();
	assert_int_equal(status, BLERR_NOERROR);

	int handle = context->api->BotAllocMoveState();
	assert_true(handle > 0);

	vec3_t origin;
	VectorSet(origin, 12.0f, 24.0f, 36.0f);
	context->api->BotAddAvoidSpot(handle, origin, 64.0f, AVOID_ALWAYS);

	bot_movestate_t *ms = BotMoveStateFromHandle(handle);
	assert_non_null(ms);
	assert_int_equal(ms->numavoidspots, 1);
	assert_float_equal(ms->avoidspots[0].origin[0], 12.0f, 0.0001f);
	assert_float_equal(ms->avoidspots[0].origin[1], 24.0f, 0.0001f);
	assert_float_equal(ms->avoidspots[0].origin[2], 36.0f, 0.0001f);
	assert_float_equal(ms->avoidspots[0].radius, 64.0f, 0.0001f);
	assert_int_equal(ms->avoidspots[0].type, AVOID_ALWAYS);

	context->api->BotAddAvoidSpot(handle, origin, 0.0f, AVOID_CLEAR);
	assert_int_equal(ms->numavoidspots, 0);

	context->api->BotFreeMoveState(handle);
	context->api->BotShutdownLibrary();
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_import_table_matches_retail_symbol_list),
		cmocka_unit_test(test_bot_input_preserves_retail_prefix_layout),
		cmocka_unit_test(test_export_table_preserves_retail_prefix_layout),
		cmocka_unit_test_setup_teardown(test_get_bot_api_copies_allocator_callbacks,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_get_bot_api_copies_retail_import_table,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_get_bot_api_bounds_retail_import_prefix,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_console_commands_register,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_console_commands_invoke,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_load_map_requires_library,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_load_map_null_refreshes_assets_without_reset,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_setup_library_guard_emits_message,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_setup_failure_retains_retail_setup_flag,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_shutdown_library_guard_emits_message,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_library_initialized_reports_aas_state,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_define_preserves_retail_failure_contract,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_entity_exports_validate_entity_first,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_console_message_inactive_contract,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_setup_client_preserves_retail_boolean_abi,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_shutdown_library_releases_weapon_state_handles,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_shutdown_library_releases_client_weapon_wiring,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_weapon_exports_drive_weighted_selection,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_weapon_info_export_zeroes_invalid_queries,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_shutdown_client_requires_library,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_move_client_requires_library,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_client_runtime_failure_diagnostics,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_retail_exports_share_exact_library_guard,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_client_settings_requires_library,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_settings_requires_library,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_client_settings_store_slot_mirrors,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_client_validator_exact_contract,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_client_capacity_uses_setup_maxclients,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_settings_updates_active_setup_block,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_weight_exports_cover_guards_and_round_trip,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_test_export_is_retail_noop,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_load_map_and_sensory_queues,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_usehook_defaults_disabled,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_console_message_and_ai_pipeline,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_console_message_flood_preserves_deferred_head,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_console_message_classifies_death_updates,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_console_message_tracks_team_leadership,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_console_wait_reports_retail_unknown_match,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_console_what_are_you_doing_reports_every_ltg_type,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_console_what_are_you_doing_preserves_gates_and_empty_sources,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_console_subteam_cases_preserve_retail_gates_and_storage,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_console_formation_damage_cases_are_ungated,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_console_formation_space_preserves_conversion_and_limits,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_console_doformation_and_dismiss_preserve_exact_ltg_effects,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_console_camp_preserves_retail_goal_branches_and_deadlines,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_console_checkpoint_stores_independently_of_addressing,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_console_patrol_preserves_lists_flags_failures_and_deadlines,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_console_help_accompany_preserves_retail_target_and_goal_boundaries,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_console_defend_key_area_preserves_retail_gates_and_deadlines,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_console_ctf_orders_require_both_flags_and_preserve_exact_resets,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_console_reply_enters_and_completes_stand,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_chat_initial_exports_preserve_raw_type_aliases,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_setup_library_wires_chat_setup,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_lib_var_set_preserves_retail_local_contract,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_lib_var_cache_tracks_updates,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_update_entity_populates_aas,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_battle_inventory_powerup_timers_and_power_armor,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_enemy_battle_inventory_weapon_and_effect_projection,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_interface_mover_parity,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_start_frame_entity_lifecycle,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_start_frame_updates_routing_diagnostics,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_add_avoid_spot_export_wires_move_state,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_end_to_end_pipeline_with_assets,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_bridge_tracks_mover_entity_updates,
							setup_bot_interface,
							teardown_bot_interface),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
