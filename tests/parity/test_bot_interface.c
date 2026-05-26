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
	{ "CvarGet", offsetof(bot_import_t, CvarGet) },
	{ "Error", offsetof(bot_import_t, Error) },
	{ "Trace", offsetof(bot_import_t, Trace) },
	{ "PointContents", offsetof(bot_import_t, PointContents) },
	{ "GetMemory", offsetof(bot_import_t, GetMemory) },
	{ "FreeMemory", offsetof(bot_import_t, FreeMemory) },
	{ "DebugLineCreate", offsetof(bot_import_t, DebugLineCreate) },
	{ "DebugLineDelete", offsetof(bot_import_t, DebugLineDelete) },
	{ "DebugLineShow", offsetof(bot_import_t, DebugLineShow) },
	{ "AddCommand", offsetof(bot_import_t, AddCommand) },
	{ "RemoveCommand", offsetof(bot_import_t, RemoveCommand) },
	{ "CmdArgc", offsetof(bot_import_t, CmdArgc) },
	{ "CmdArgv", offsetof(bot_import_t, CmdArgv) },
};

typedef struct bot_interface_test_context_s
{
    asset_env_t assets;
    mock_bot_import_t mock;
    bot_export_t *api;
    botlib_contract_catalogue_t catalogue;
    bool libvar_initialised;
} bot_interface_test_context_t;

static mock_bot_import_t *g_active_mock = NULL;
static int g_mock_import_libvar_set_status = BLERR_NOERROR;
static bool ensure_map_fixture(const asset_env_t *assets, const char *stem);

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

static int Mock_ImportBotLibVarSet(const char *var_name, const char *value)
{
    (void)var_name;
    (void)value;
    return g_mock_import_libvar_set_status;
}

static void Mock_BotClientCommand(int client, char *fmt, ...)
{
    (void)client;
    (void)fmt;
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

static void *Mock_GetMemory(int size)
{
    return malloc((size_t)size);
}

static void Mock_FreeMemory(void *ptr)
{
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
    mock->print_count = 0;
    mock->bot_input_count = 0;
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
    context->api = GetBotAPI(&context->mock.table);
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
	                         "BotShutdownClient: library not initialised",
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
	                         "BotMoveClient: library not initialised",
	                         PRT_ERROR);
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
	                         "BotClientSettings: library not initialised",
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
 "BotSettings: library not initialised",
 PRT_ERROR);
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
test_bot_test_debug_draw_toggles_bridge

Confirms the Test command toggles bridge debug line state.
=============
*/
static void test_bot_test_debug_draw_toggles_bridge(void **state)
{
	bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;

	Mock_Reset(&context->mock);
	assert_false(Q2Bridge_DebugLinesEnabled());

	int status = context->api->BotSetupLibrary();
	assert_int_equal(status, BLERR_NOERROR);
	assert_false(Q2Bridge_DebugLinesEnabled());

	Mock_ClearPrints(&context->mock);
	status = context->api->Test(0, "debug_draw on", NULL, NULL);
	assert_int_equal(status, BLERR_NOERROR);
	assert_true(Q2Bridge_DebugLinesEnabled());
	Mock_AssertPrintContains(&context->mock,
	                         "Test debug_draw: enabled",
	                         PRT_MESSAGE);

	Mock_ClearPrints(&context->mock);
	status = context->api->Test(0, "debug_draw off", NULL, NULL);
	assert_int_equal(status, BLERR_NOERROR);
	assert_false(Q2Bridge_DebugLinesEnabled());
	Mock_AssertPrintContains(&context->mock,
	                         "Test debug_draw: disabled",
	                         PRT_MESSAGE);

	Mock_ClearPrints(&context->mock);
	status = context->api->Test(0, "debug_draw", NULL, NULL);
	assert_int_equal(status, BLERR_NOERROR);
	assert_true(Q2Bridge_DebugLinesEnabled());
	Mock_AssertPrintContains(&context->mock,
	                         "Test debug_draw: enabled",
	                         PRT_MESSAGE);

	context->api->BotShutdownLibrary();
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
    status = context->api->BotLoadMap("maps/test1.bsp", 0, NULL, 2, sounds, 0, NULL);
    assert_int_equal(status, BLERR_NOERROR);

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

    context->api->Test(0, "sounds", origin, origin);
    assert_non_null(Mock_FindPrint(&context->mock, "Test sounds: 1 queued"));
    assert_non_null(Mock_FindPrint(&context->mock, "sound[0]: ent=3"));

    context->api->Test(0, "pointlights", origin, origin);
    assert_non_null(Mock_FindPrint(&context->mock, "Test pointlights: 1 queued"));
    assert_non_null(Mock_FindPrint(&context->mock, "light[0]: ent=5"));

    bot_settings_t settings;
    memset(&settings, 0, sizeof(settings));
    snprintf(settings.characterfile, sizeof(settings.characterfile), "bots/babe_c.c");
    snprintf(settings.charactername, sizeof(settings.charactername), "babe");

    status = context->api->BotSetupClient(1, &settings);
    assert_int_equal(status, BLERR_NOERROR);

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

    assert_non_null(Mock_FindPrint(&context->mock, "------- BotLib Shutdown -------"));
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
    assert_int_equal(status, BLERR_NOERROR);

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
    assert_int_equal(status, BLERR_NOERROR);

    bot_client_state_t *client_state = BotState_Get(1);
    assert_non_null(client_state);
    assert_non_null(client_state->chat_state);

    status = context->api->BotConsoleMessage(1, CMS_CHAT, "hello gladiator");
    assert_int_equal(status, BLERR_NOERROR);

    context->api->Test(1, "dump_chat", NULL, NULL);
    assert_non_null(Mock_FindPrint(&context->mock, "hello gladiator"));

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

    status = context->api->BotAI(1, 0.05f);
    assert_int_equal(status, BLERR_AIUPDATEINACTIVECLIENT);

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
	assert_true(context->api->BotNextConsoleMessage(chat,
		&message_type,
		message,
		sizeof(message)));
	assert_int_equal(message_type, 0);
	assert_true(message[0] != '\0');
	assert_null(strstr(message, "\\v"));

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

static void test_bot_lib_var_set_propagates_import_status(void **state)
{
    bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;

    Mock_Reset(&context->mock);

    const botlib_import_table_t *original_imports = BotInterface_GetImportTable();
    assert_non_null(original_imports);

    botlib_import_table_t override_imports = *original_imports;
    override_imports.BotLibVarSet = Mock_ImportBotLibVarSet;

    BotInterface_SetImportTable(&override_imports);

    g_mock_import_libvar_set_status = BLERR_NOERROR;
    int status = context->api->BotLibVarSet("test_override", "42");
    assert_int_equal(status, BLERR_NOERROR);

    g_mock_import_libvar_set_status = BLERR_LIBRARYNOTSETUP;
    status = context->api->BotLibVarSet("test_override", "84");
    assert_int_equal(status, BLERR_LIBRARYNOTSETUP);

    g_mock_import_libvar_set_status = BLERR_INVALIDIMPORT;
    status = context->api->BotLibVarSet("test_override", "168");
    assert_int_equal(status, BLERR_INVALIDIMPORT);

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
    assert_int_equal(status, BLERR_NOERROR);

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

    vec3_t goal_delta;
    VectorSubtract(mover_goal.origin, update.origin, goal_delta);
    float expected_speed = sqrtf(goal_delta[0] * goal_delta[0] +
                                 goal_delta[1] * goal_delta[1] +
                                 goal_delta[2] * goal_delta[2]);
    assert_true(expected_speed > 0.0f);

    vec3_t expected_dir = {goal_delta[0] / expected_speed,
                           goal_delta[1] / expected_speed,
                           goal_delta[2] / expected_speed};

    assert_float_equal(final_input->speed, 400.0f, 0.0001f);
    assert_float_equal(final_input->dir[0], expected_dir[0], 0.0001f);
    assert_float_equal(final_input->dir[1], expected_dir[1], 0.0001f);
    assert_float_equal(final_input->dir[2], expected_dir[2], 0.0001f);
    assert_int_equal(final_input->actionflags, 0);

    context->api->BotShutdownClient(1);
    context->api->BotShutdownLibrary();

    bot_mover_fixture_shutdown(&fixture);
    LibVarSet("bot_developer", "0");
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
    assert_int_equal(status, BLERR_NOERROR);

    status = context->api->BotConsoleMessage(1, CMS_CHAT, "hello gladiator");
    assert_success_status(&context->catalogue, "BotLibConsoleMessage", status);

    context->api->Test(1, "dump_chat", NULL, NULL);
    assert_non_null(Mock_FindPrint(&context->mock, "hello gladiator"));

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
    assert_int_equal(status, BLERR_NOERROR);

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
    mover_update.modelindex = mover_modelnum;

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
		cmocka_unit_test_setup_teardown(test_console_commands_register,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_console_commands_invoke,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_load_map_requires_library,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_setup_library_guard_emits_message,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_shutdown_library_guard_emits_message,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_shutdown_library_releases_weapon_state_handles,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_shutdown_client_requires_library,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_move_client_requires_library,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_client_settings_requires_library,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_settings_requires_library,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_weight_exports_cover_guards_and_round_trip,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_test_debug_draw_toggles_bridge,
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
		cmocka_unit_test_setup_teardown(test_chat_initial_exports_preserve_raw_type_aliases,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_setup_library_wires_chat_setup,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_lib_var_set_propagates_import_status,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_lib_var_cache_tracks_updates,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_update_entity_populates_aas,
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
