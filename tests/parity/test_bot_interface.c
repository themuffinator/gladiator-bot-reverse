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
#include "botlib/ai/ai_dm.h"
#include "botlib/ai_chat/ai_chat.h"
#include "botlib/ea/ea_local.h"
#include "botlib/ai_move/bot_move.h"
#include "botlib/ai_move/mover_catalogue.h"
#include "botlib/ai/goal_move_orchestrator.h"
#include "botlib/ai_goal/bot_goal.h"
#include "botlib/ai_weapon/bot_weapon.h"
#include "botlib/common/l_log.h"
#include "botlib/common/l_crc.h"
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
	RETAIL_INVENTORY_ARMORBODY = 1,
	RETAIL_INVENTORY_ARMORCOMBAT = 2,
	RETAIL_INVENTORY_ARMORJACKET = 3,
	RETAIL_INVENTORY_POWERSCREEN = 5,
	RETAIL_INVENTORY_POWERSHIELD = 6,
	RETAIL_INVENTORY_SUPERSHOTGUN = 9,
	RETAIL_INVENTORY_MACHINEGUN = 10,
	RETAIL_INVENTORY_CHAINGUN = 11,
	RETAIL_INVENTORY_GRENADES = 12,
	RETAIL_INVENTORY_GRENADELAUNCHER = 13,
	RETAIL_INVENTORY_ROCKETLAUNCHER = 14,
	RETAIL_INVENTORY_HYPERBLASTER = 15,
	RETAIL_INVENTORY_RAILGUN = 16,
	RETAIL_INVENTORY_BFG10K = 17,
	RETAIL_INVENTORY_SHELLS = 18,
	RETAIL_INVENTORY_BULLETS = 19,
	RETAIL_INVENTORY_CELLS = 20,
	RETAIL_INVENTORY_ROCKETS = 21,
	RETAIL_INVENTORY_SLUGS = 22,
	RETAIL_INVENTORY_QUAD = 23,
	RETAIL_INVENTORY_INVULNERABILITY = 24,
	RETAIL_INVENTORY_SILENCER = 25,
	RETAIL_INVENTORY_REBREATHER = 26,
	RETAIL_INVENTORY_HEALTH = 41,
	RETAIL_INVENTORY_FLAG1 = 43,
	RETAIL_INVENTORY_FLAG2 = 44,
	RETAIL_INVENTORY_TECH1 = 45,
	RETAIL_INVENTORY_TECH2 = 46,
	RETAIL_INVENTORY_TECH3 = 47,
	RETAIL_INVENTORY_TECH4 = 48,
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
	bot_import_extended_t table;
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
	bool trace_blocked;
	int trace_entity;
	int point_contents_result;
	bool point_contents_height_split;
	float point_contents_split_z;
	int point_contents_result_above;
	vec3_t point_contents_points[64];
	size_t point_contents_command_counts[64];
	size_t point_contents_count;
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

static const import_field_descriptor_t g_extended_retail_export_layout[] = {
	{ "BotVersion", offsetof(bot_export_extended_t, BotVersion) },
	{ "BotSetupLibrary", offsetof(bot_export_extended_t, BotSetupLibrary) },
	{ "BotShutdownLibrary", offsetof(bot_export_extended_t, BotShutdownLibrary) },
	{ "BotLibraryInitialized", offsetof(bot_export_extended_t, BotLibraryInitialized) },
	{ "BotLibVarSet", offsetof(bot_export_extended_t, BotLibVarSet) },
	{ "BotDefine", offsetof(bot_export_extended_t, BotDefine) },
	{ "BotLoadMap", offsetof(bot_export_extended_t, BotLoadMap) },
	{ "BotSetupClient", offsetof(bot_export_extended_t, BotSetupClient) },
	{ "BotShutdownClient", offsetof(bot_export_extended_t, BotShutdownClient) },
	{ "BotMoveClient", offsetof(bot_export_extended_t, BotMoveClient) },
	{ "BotClientSettings", offsetof(bot_export_extended_t, BotClientSettings) },
	{ "BotSettings", offsetof(bot_export_extended_t, BotSettings) },
	{ "BotStartFrame", offsetof(bot_export_extended_t, BotStartFrame) },
	{ "BotUpdateClient", offsetof(bot_export_extended_t, BotUpdateClient) },
	{ "BotUpdateEntity", offsetof(bot_export_extended_t, BotUpdateEntity) },
	{ "BotAddSound", offsetof(bot_export_extended_t, BotAddSound) },
	{ "BotAddPointLight", offsetof(bot_export_extended_t, BotAddPointLight) },
	{ "BotAI", offsetof(bot_export_extended_t, BotAI) },
	{ "BotConsoleMessage", offsetof(bot_export_extended_t, BotConsoleMessage) },
	{ "Test", offsetof(bot_export_extended_t, Test) },
};

typedef struct bot_interface_test_context_s
{
    asset_env_t assets;
    mock_bot_import_t mock;
    bot_export_extended_t *api;
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
const aas_bspentity_t *BotAI_EntityToActivate(const aas_bspentity_t *entities,
	int blockentity);
static allocator_callback_capture_t g_primary_allocator_capture;
static allocator_callback_capture_t g_alternate_allocator_capture;

typedef struct setup_order_capture_s
{
	char names[64][64];
	bool library_initialised[64];
	size_t count;
} setup_order_capture_t;

static setup_order_capture_t g_setup_order_capture;

/*
=============
Mock_CaptureSetupLibVarGet

Records the setup libvar sequence and the committed library state at each read.
=============
*/
static void Mock_CaptureSetupLibVarGet(const char *var_name,
	const char *value,
	int status)
{
	(void)value;
	(void)status;

	if (var_name == NULL ||
		g_setup_order_capture.count >= ARRAY_LEN(g_setup_order_capture.names))
	{
		return;
	}

	size_t index = g_setup_order_capture.count++;
	snprintf(g_setup_order_capture.names[index],
		sizeof(g_setup_order_capture.names[index]),
		"%s",
		var_name);
	g_setup_order_capture.library_initialised[index] =
		BotLibraryInitialized();
}

/*
=============
Mock_ConfigureLibraryImports

Restores the baseline asset libvars after acquiring a fresh public API table.
=============
*/
static void Mock_ConfigureLibraryImports(bot_interface_test_context_t *context)
{
	assert_non_null(context);
	assert_non_null(context->api);
	assert_int_equal(context->api->BotLibVarSet("basedir",
		context->assets.asset_root), BLERR_NOERROR);
	assert_int_equal(context->api->BotLibVarSet("gamedir", ""),
		BLERR_NOERROR);
	assert_int_equal(context->api->BotLibVarSet("cddir", ""),
		BLERR_NOERROR);
	assert_int_equal(context->api->BotLibVarSet("gladiator_asset_dir", ""),
		BLERR_NOERROR);
	assert_int_equal(context->api->BotLibVarSet("weaponconfig", "weapons.c"),
		BLERR_NOERROR);
	assert_int_equal(context->api->BotLibVarSet("max_weaponinfo", "64"),
		BLERR_NOERROR);
	assert_int_equal(context->api->BotLibVarSet("max_projectileinfo", "64"),
		BLERR_NOERROR);
	assert_int_equal(context->api->BotLibVarSet("itemconfig", "items.c"),
		BLERR_NOERROR);
}

/*
=============
Mock_ReacquireAPI

Reacquires tables and imports after retail shutdown has zeroed the adapters.
=============
*/
static void Mock_ReacquireAPI(bot_interface_test_context_t *context)
{
	assert_non_null(context);
	context->api = GetBotAPIEx(&context->mock.table,
		sizeof(context->mock.table));
	assert_non_null(context->api);
	Mock_ConfigureLibraryImports(context);
}

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
	assert_int_equal(ARRAY_LEN(g_extended_retail_export_layout),
		ARRAY_LEN(g_retail_export_layout));

	for (size_t index = 0; index < ARRAY_LEN(g_retail_export_layout); ++index)
	{
		const import_field_descriptor_t *field =
			&g_retail_export_layout[index];
		const import_field_descriptor_t *extended_field =
			&g_extended_retail_export_layout[index];
		size_t expected_offset = index * sizeof(void (*)(void));
		if (field->offset != expected_offset)
		{
			fail_msg("export '%s' offset %zu diverges from retail slot %zu",
				field->name,
				field->offset,
				expected_offset);
		}
		if (strcmp(extended_field->name, field->name) != 0 ||
			extended_field->offset != field->offset)
		{
			fail_msg("extended export '%s' offset %zu diverges from retail '%s' offset %zu",
				extended_field->name,
				extended_field->offset,
				field->name,
				field->offset);
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

/*
=============
Mock_BotClientCommand

Captures imported client commands and the single argument used by chat, retail
item-use/drop actions, gestures, and the retail `removebot` request, whose one
argument is the speaking bot's ClientName (0x1001ed0a).
=============
*/
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
	if (strcmp(fmt, "say") == 0 || strcmp(fmt, "say_team") == 0 ||
		strcmp(fmt, "use") == 0 ||
		strcmp(fmt, "drop") == 0 ||
		strcmp(fmt, "wave") == 0 ||
		strcmp(fmt, "removebot") == 0)
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

/*
=============
Mock_Trace

Returns an unobstructed trace unless a fixture-free node test requests a
solid visibility block.
=============
*/
static bsp_trace_t Mock_Trace(vec3_t start,
	vec3_t mins,
	vec3_t maxs,
	vec3_t end,
	int passent,
	int contentmask)
{
	(void)start;
	(void)mins;
	(void)maxs;
	(void)end;
	(void)passent;
	(void)contentmask;

	bsp_trace_t trace;
	memset(&trace, 0, sizeof(trace));
	trace.fraction = g_active_mock != NULL && g_active_mock->trace_blocked
		? 0.0f
		: 1.0f;
	trace.ent = g_active_mock != NULL ? g_active_mock->trace_entity : 0;
	return trace;
}

/*
=============
Mock_PointContents

Records each sampled point and the number of commands already issued so item-
use tests can pin both eye-position sampling and callback order. The optional
height split lets a test give the feet and the eye different contents, which is
what separates retail's eye-sampling camp test from the origin the sibling
AAS_Swimming call uses.
=============
*/
static int Mock_PointContents(vec3_t point)
{
	if (g_active_mock == NULL)
	{
		return 0;
	}

	size_t index = g_active_mock->point_contents_count;
	if (index < ARRAY_LEN(g_active_mock->point_contents_points))
	{
		VectorCopy(point, g_active_mock->point_contents_points[index]);
		g_active_mock->point_contents_command_counts[index] =
			g_active_mock->client_command_count;
	}
	g_active_mock->point_contents_count += 1U;
	if (g_active_mock->point_contents_height_split &&
		point[2] >= g_active_mock->point_contents_split_z)
	{
		return g_active_mock->point_contents_result_above;
	}
	return g_active_mock->point_contents_result;
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
	memset(mock->point_contents_points, 0, sizeof(mock->point_contents_points));
	memset(mock->point_contents_command_counts,
		0,
		sizeof(mock->point_contents_command_counts));
	mock->print_count = 0;
	mock->bot_input_count = 0;
	mock->client_command_count = 0;
	mock->trace_blocked = false;
	mock->point_contents_result = 0;
	mock->point_contents_height_split = false;
	mock->point_contents_split_z = 0.0f;
	mock->point_contents_result_above = 0;
	mock->point_contents_count = 0;
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
	const char *log_path = "botlib.log";
	FILE *existing_log = fopen(log_path, "rb");
	if (existing_log != NULL)
	{
		fclose(existing_log);
		cmocka_skip();
	}
	LibVarSet("log", "1");

	Mock_Reset(&context->mock);

	int status = context->api->BotSetupLibrary();
	assert_int_equal(status, BLERR_NOERROR);
	assert_non_null(BotLib_LogFile());
	assert_non_null(Mock_FindPrint(&context->mock, "Opened log botlib.log\n"));

	assert_int_equal(context->mock.command_count, 3);
	assert_string_equal(context->mock.commands[0].name, "bot_test");
	assert_non_null(context->mock.commands[0].function);
	assert_string_equal(context->mock.commands[1].name, "aas_showpath");
	assert_non_null(context->mock.commands[1].function);
	assert_string_equal(context->mock.commands[2].name, "aas_showareas");

	context->api->BotShutdownLibrary();
	assert_int_equal(context->mock.command_count, 0);
	assert_null(BotLib_LogFile());
	assert_int_equal(remove(log_path), 0);
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
	Mock_ConfigureLibraryImports(context);

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
	BotInterface_SetImportCapture(NULL);
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
	assert_int_equal(sizeof(bot_import_t),
		offsetof(bot_import_t, DebugLineShow) +
		sizeof(context->mock.table.DebugLineShow));
	assert_int_equal(offsetof(bot_import_extended_t, DebugLineShow),
		offsetof(bot_import_t, DebugLineShow));
	assert_true(sizeof(bot_import_extended_t) > sizeof(bot_import_t));

	bot_import_t retail_import;
	memcpy(&retail_import, &context->mock.table, sizeof(retail_import));
	bot_export_t *retail_api = GetBotAPI(&retail_import);
	assert_non_null(retail_api);
	assert_int_equal(sizeof(bot_export_t),
		offsetof(bot_export_t, Test) + sizeof(retail_api->Test));
	assert_int_equal(offsetof(bot_export_extended_t, Test),
		offsetof(bot_export_t, Test));
	assert_true(sizeof(bot_export_extended_t) > sizeof(bot_export_t));
	assert_ptr_not_equal(retail_api, context->api);
	const bot_import_extended_t *copied = Q2Bridge_GetImportTable();
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
	assert_int_equal(catalogue_entry->modelindex, -1);

	vec3_t origin = {0.0f, 0.0f, 0.0f};
	status = context->api->BotAddSound(origin, 0, 0, 0, 1.0f, 1.0f, 0.0f);
	assert_int_equal(status, BLERR_NOERROR);
	assert_int_equal(AAS_SoundSubsystem_SoundEventCount(), 0);
	status = context->api->BotAddSound(origin, 0, 0, 1, 1.0f, 1.0f, 0.0f);
	assert_int_equal(status, BLERR_INVALIDSOUNDINDEX);
	const captured_print_t *invalid_sound =
		Mock_FindPrintEntry(&context->mock, "sound index 1 out of range");
	assert_non_null(invalid_sound);
	assert_int_equal(invalid_sound->type, PRT_FATAL);
	assert_string_equal(invalid_sound->message,
		"sound index 1 out of range [0, 1]\n");

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
	assert_int_equal(AAS_SoundSubsystem_SoundEventCount(), 0);
	assert_string_equal(AAS_SoundSubsystem_AssetName(0), "sound/replacement.wav");
	assert_string_equal(AAS_SoundSubsystem_AssetName(1), "sound/new-index.wav");

	catalogue_entry = BotMove_MoverCatalogueFindByModel(1);
	assert_non_null(catalogue_entry);
	assert_int_equal(catalogue_entry->modelindex, -1);

	status = context->api->BotAddSound(origin, 0, 0, 1, 1.0f, 1.0f, 0.0f);
	assert_int_equal(status, BLERR_NOERROR);
	assert_int_equal(AAS_SoundSubsystem_SoundEventCount(), 0);

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
test_bot_goal_setup_failure_retains_loaded_weapon_config

Pins retail's partial AI setup lifetime: a goal-config failure returns without
unloading the weapon config that was successfully loaded immediately before it.
=============
*/
static void test_bot_goal_setup_failure_retains_loaded_weapon_config(void **state)
{
	bot_interface_test_context_t *context =
		(bot_interface_test_context_t *)*state;

	Mock_Reset(&context->mock);
	assert_null(AI_GetActiveWeaponConfig());
	assert_int_equal(context->api->BotLibVarSet("itemconfig",
		"definitely_missing_setup_items.c"),
		BLERR_NOERROR);

	int status = context->api->BotSetupLibrary();
	assert_int_equal(status, BLERR_CANNOTLOADITEMCONFIG);
	assert_true(BotLibraryInitialized());
	assert_true(AAS_Initialized());
	assert_non_null(AI_GetActiveWeaponConfig());
	assert_false(EA_IsInitialised());

	int weapon_state = context->api->BotAllocWeaponState();
	assert_true(weapon_state > 0);
	const bot_weaponstate_t *retained = BotWeaponStatePeek(weapon_state);
	assert_non_null(retained);
	assert_ptr_equal(retained->config, AI_GetActiveWeaponConfig());
	context->api->BotFreeWeaponState(weapon_state);

	assert_int_equal(context->api->BotShutdownLibrary(), BLERR_NOERROR);
	assert_null(AI_GetActiveWeaponConfig());
}

/*
=============
test_bot_setup_commits_before_retail_libvar_sequence

Pins the retail setup flag and max-client, movement-libvar, then AAS ordering.
=============
*/
static void test_bot_setup_commits_before_retail_libvar_sequence(void **state)
{
	bot_interface_test_context_t *context =
		(bot_interface_test_context_t *)*state;
	static const char *expected_names[] = {
		"maxclients",
		"maxentities",
		"sv_friction",
		"sv_stopspeed",
		"sv_gravity",
		"sv_waterfriction",
		"sv_watergravity",
		"sv_maxvelocity",
		"sv_maxwalkvelocity",
		"sv_maxcrouchvelocity",
		"sv_maxswimvelocity",
		"sv_maxacceleration",
		"sv_airaccelerate",
		"sv_step",
		"sv_maxbarrier",
		"sv_maxsteepness",
		"sv_jumpvel",
		"sv_maxwaterjump",
	};
	const botlib_import_capture_t capture = {
		.BotLibVarGet = Mock_CaptureSetupLibVarGet,
	};

	memset(&g_setup_order_capture, 0, sizeof(g_setup_order_capture));
	BotInterface_SetImportCapture(&capture);
	int status = context->api->BotSetupLibrary();
	BotInterface_SetImportCapture(NULL);
	assert_int_equal(status, BLERR_NOERROR);

	size_t prefix_start = g_setup_order_capture.count;
	for (size_t index = 0; index < g_setup_order_capture.count; ++index)
	{
		if (strcmp(g_setup_order_capture.names[index], expected_names[0]) == 0)
		{
			prefix_start = index;
			break;
		}
	}
	assert_true(prefix_start < g_setup_order_capture.count);
	assert_true(g_setup_order_capture.count - prefix_start >=
		ARRAY_LEN(expected_names));

	for (size_t index = 0; index < ARRAY_LEN(expected_names); ++index)
	{
		size_t captured_index = prefix_start + index;
		assert_string_equal(g_setup_order_capture.names[captured_index],
			expected_names[index]);
		assert_true(g_setup_order_capture.library_initialised[captured_index]);
	}

	status = context->api->BotShutdownLibrary();
	assert_int_equal(status, BLERR_NOERROR);
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
	assert_null(context->api->BotLibraryInitialized);
	assert_false(BotLibraryInitialized());
}

/*
=============
test_bot_shutdown_clears_public_adapter_tables

Pins the DLL lifetime contract: shutdown clears the copied import/export
blocks, and a later host setup must reacquire and rebind the API table.
=============
*/
static void test_bot_shutdown_clears_public_adapter_tables(void **state)
{
	bot_interface_test_context_t *context =
		(bot_interface_test_context_t *)*state;
	bot_import_t retail_import;
	memcpy(&retail_import, &context->mock.table, sizeof(retail_import));

	bot_export_t *retail_api = GetBotAPI(&retail_import);
	assert_non_null(retail_api);
	Mock_ConfigureLibraryImports(context);
	assert_int_equal(retail_api->BotSetupLibrary(), BLERR_NOERROR);
	const botlib_import_capture_t capture = {0};
	BotInterface_SetImportCapture(&capture);
	assert_ptr_equal(BotInterface_GetImportCapture(), &capture);
	int (*shutdown_library)(void) = retail_api->BotShutdownLibrary;
	assert_non_null(shutdown_library);
	assert_int_equal(shutdown_library(), BLERR_NOERROR);

	bot_export_t cleared_retail = {0};
	bot_export_extended_t cleared_extended = {0};
	assert_memory_equal(retail_api, &cleared_retail, sizeof(cleared_retail));
	assert_memory_equal(context->api, &cleared_extended, sizeof(cleared_extended));
	assert_null(BotInterface_GetImportTable());
	assert_null(BotInterface_GetImportCapture());
	assert_null(Q2Bridge_GetImportTable());
	assert_false(BotLibraryInitialized());

	retail_api = GetBotAPI(&retail_import);
	assert_non_null(retail_api);
	context->api = GetBotAPIEx(&context->mock.table,
		sizeof(context->mock.table));
	assert_non_null(context->api);
	Mock_ConfigureLibraryImports(context);
	assert_int_equal(retail_api->BotSetupLibrary(), BLERR_NOERROR);
	assert_true(BotLibraryInitialized());
	assert_int_equal(retail_api->BotShutdownLibrary(), BLERR_NOERROR);
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

	bot_settings_t missing_settings;
	memset(&missing_settings, 0, sizeof(missing_settings));
	snprintf(missing_settings.characterfile,
		sizeof(missing_settings.characterfile),
		"bots/missing_character.c");
	snprintf(missing_settings.charactername,
		sizeof(missing_settings.charactername),
		"missing");
	Mock_ClearPrints(&context->mock);
	status = context->api->BotSetupClient(1, &missing_settings);
	assert_false(status);
	bot_client_state_t *partial_state = BotState_Get(1);
	assert_non_null(partial_state);
	assert_false(partial_state->active);
	assert_null(partial_state->character);
	qboolean found_retail_client_diagnostic = qfalse;
	for (size_t index = 0; index < context->mock.print_count; ++index)
	{
		const captured_print_t *entry = &context->mock.prints[index];
		if (entry->type == PRT_FATAL &&
			strcmp(entry->message,
				"couldn't load bot character missing from bots/missing_character.c\n") == 0)
		{
			found_retail_client_diagnostic = qtrue;
			break;
		}
	}
	assert_true(found_retail_client_diagnostic);

	status = context->api->BotSetupClient(1, &settings);
	assert_true(status);
	assert_int_equal(BotState_ActiveClientCount(), 1);

	bot_client_state_t *active_state = BotState_Get(1);
	assert_non_null(active_state);

	size_t source_count_before_duplicate = CRC_SourceChecksumCount();
	snprintf(aasworld.mapName, sizeof(aasworld.mapName),
		"__gladiator_client_setup_map");
	aasworld.bspEntityChecksum = 0xD218U;
	Mock_ClearPrints(&context->mock);
	status = context->api->BotSetupClient(1, &settings);
	assert_false(status);
	assert_ptr_equal(BotState_Get(1), active_state);
	assert_int_equal(BotState_ActiveClientCount(), 1);
	/* Retail's export thunk 0x10037f2c calls sub_100085f0 (the map
	   entity-lump source-checksum registration) unconditionally, after both
	   guards but before the core sub_10029480 whose "already setup" early-out
	   lives at 0x100294a0.  The map name changed above, so this first
	   duplicate call registers exactly one previously unseen record. */
	assert_int_equal(CRC_SourceChecksumCount(),
		source_count_before_duplicate + 1U);
	Mock_AssertPrintContains(&context->mock,
		"client 1 already setup\n",
		PRT_FATAL);

	Mock_ClearPrints(&context->mock);
	status = context->api->BotSetupClient(1, NULL);
	assert_false(status);
	/* The second duplicate registers the same map name and is rejected by
	   the strcmp==0 path in retail 0x100376b0, so the count is unchanged.
	   Registration is not conditional on the settings pointer: sub_100085f0
	   takes no arguments and runs before arg2 is ever read. */
	assert_int_equal(CRC_SourceChecksumCount(),
		source_count_before_duplicate + 1U);
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

	Mock_ReacquireAPI(context);
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

	Mock_ReacquireAPI(context);
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
	assert_null(context->api->BotGetWeaponInfo);
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
	status = context->api->BotLibVarSet("maxentities", "7");
	assert_int_equal(status, BLERR_NOERROR);

	status = context->api->BotSetupLibrary();
	assert_int_equal(status, BLERR_NOERROR);
	assert_int_equal(BotState_ClientCapacity(), 2);
	assert_int_equal(BotState_ActiveClientCount(), 0);
	assert_int_equal(aasworld.maxClients, 2);
	assert_int_equal(aasworld.maxEntities, 7);
	assert_non_null(aasworld.entities);
	for (int entnum = 0; entnum < aasworld.maxEntities; ++entnum)
	{
		assert_int_equal(aasworld.entities[entnum].number, entnum);
		assert_false(aasworld.entities[entnum].inuse);
	}

	bot_client_state_t *state_slot_zero = BotState_Get(0);
	bot_client_state_t *state_slot_one = BotState_Get(1);
	bot_client_state_t *state_sentinel = BotState_Get(2);
	const bot_clientsettings_t *settings_slot_zero =
		BotState_ClientSettings(0);
	const bot_clientsettings_t *settings_slot_one =
		BotState_ClientSettings(1);
	const bot_clientsettings_t *settings_sentinel =
		BotState_ClientSettings(2);
	assert_non_null(state_slot_zero);
	assert_non_null(state_slot_one);
	assert_non_null(state_sentinel);
	assert_non_null(settings_slot_zero);
	assert_non_null(settings_slot_one);
	assert_non_null(settings_sentinel);
	assert_int_equal((unsigned char *)state_slot_one -
		(unsigned char *)state_slot_zero,
		BOT_STATE_RETAIL_RECORD_SIZE);
	assert_int_equal((const unsigned char *)settings_slot_one -
		(const unsigned char *)settings_slot_zero,
		BOT_STATE_RETAIL_CLIENT_SETTINGS_SIZE);
	assert_ptr_not_equal(state_sentinel,
		(unsigned char *)state_slot_zero +
			2U * BOT_STATE_RETAIL_RECORD_SIZE);
	assert_ptr_not_equal(settings_sentinel,
		(const unsigned char *)settings_slot_zero +
			2U * BOT_STATE_RETAIL_CLIENT_SETTINGS_SIZE);

	const size_t state_payload_size =
		2U * BOT_STATE_RETAIL_RECORD_SIZE;
	const size_t settings_payload_size =
		2U * BOT_STATE_RETAIL_CLIENT_SETTINGS_SIZE;
	const size_t state_total_size = MemoryByteSize(state_slot_zero);
	const size_t settings_total_size = MemoryByteSize(settings_slot_zero);
	assert_true(state_total_size >= state_payload_size);
	assert_true(settings_total_size >= settings_payload_size);
	assert_int_equal(state_total_size - state_payload_size,
		settings_total_size - settings_payload_size);

	unsigned char zero_record[BOT_STATE_RETAIL_RECORD_SIZE];
	memset(zero_record, 0, sizeof(zero_record));
	assert_memory_equal(state_slot_one, zero_record, sizeof(zero_record));
	assert_ptr_equal(BotState_Create(1), state_slot_one);
	assert_memory_equal(state_slot_one, zero_record, sizeof(zero_record));
	assert_int_equal(state_slot_one->team, 0);
	assert_int_equal(state_slot_one->ltg_teammate, 0);
	assert_float_equal(state_slot_one->combat.enemy_visible_time,
		0.0f,
		0.0001f);

	bot_clientsettings_t live_settings;
	memset(&live_settings, 0, sizeof(live_settings));
	snprintf(live_settings.netname, sizeof(live_settings.netname), "Capacity Babe");
	snprintf(live_settings.skin, sizeof(live_settings.skin), "female/athena");

	status = context->api->BotClientSettings(1, &live_settings);
	assert_int_equal(status, BLERR_NOERROR);
	assert_string_equal(BotState_ClientName(1), "Capacity Babe");
	assert_int_equal(BotState_FindClientByName("Capacity Babe"), 1);
	assert_int_equal(BotState_FindClientByName("capacity babe"), 1);
	assert_int_equal(BotState_FindClientByName("pAcItY"), 1);

	snprintf(live_settings.netname, sizeof(live_settings.netname), "Sentinel Babe");
	Mock_ClearPrints(&context->mock);
	status = context->api->BotClientSettings(2, &live_settings);
	assert_int_equal(status, BLERR_NOERROR);
	assert_null(Mock_FindPrint(&context->mock, "invalid client number"));
	assert_non_null(BotState_ClientSettings(2));
	/*
	 * BotClientSettings accepts client == maxclients through retail's
	 * inclusive ValidClientNumber (0x10037900), but ClientName and ClientSkin
	 * apply the stricter `client < num_clients` bound at 0x10028f3e /
	 * 0x10028f8e, because the settings table holds exactly num_clients
	 * records.  The sentinel therefore stores the value and the readers still
	 * warn.
	 */
	Mock_ClearPrints(&context->mock);
	assert_string_equal(BotState_ClientName(2), "");
	Mock_AssertSinglePrint(&context->mock,
		PRT_WARNING,
		"ClientName: client 2 out of range\n");
	assert_int_equal(BotState_FindClientByName("Capacity Babe"), 1);
	assert_int_equal(BotState_FindClientByName("Sentinel Babe"), -1);

	Mock_ClearPrints(&context->mock);
	status = context->api->BotClientSettings(3, &live_settings);
	assert_int_equal(status, BLERR_INVALIDCLIENTNUMBER);
	Mock_AssertSinglePrint(&context->mock,
		PRT_ERROR,
		"BotClientSettings: invalid client number 3, [0, 2]\n");
	assert_null(BotState_ClientSettings(3));

	Mock_ClearPrints(&context->mock);
	assert_string_equal(BotState_ClientName(-1), "");
	Mock_AssertSinglePrint(&context->mock,
		PRT_WARNING,
		"ClientName: client -1 out of range\n");

	Mock_ClearPrints(&context->mock);
	assert_string_equal(BotState_ClientSkin(3), "");
	Mock_AssertSinglePrint(&context->mock,
		PRT_WARNING,
		"ClientSkin: client 3 out of range\n");

	bot_settings_t setup_settings;
	memset(&setup_settings, 0, sizeof(setup_settings));
	snprintf(setup_settings.characterfile, sizeof(setup_settings.characterfile), "bots/babe_c.c");
	snprintf(setup_settings.charactername, sizeof(setup_settings.charactername), "babe");

	Mock_ClearPrints(&context->mock);
	status = context->api->BotSetupClient(2, &setup_settings);
	assert_true(status);
	assert_null(Mock_FindPrint(&context->mock, "invalid client number"));
	assert_ptr_equal(BotState_Get(2), state_sentinel);
	assert_int_equal(state_sentinel->client_number, 2);
	assert_int_equal(state_sentinel->entity_number, 3);
	assert_int_equal(BotState_ActiveClientCount(), 1);

	status = context->api->BotShutdownClient(2);
	assert_int_equal(status, BLERR_NOERROR);
	assert_ptr_equal(BotState_Get(2), state_sentinel);
	assert_memory_equal(state_sentinel, zero_record, sizeof(zero_record));
	assert_int_equal(BotState_ActiveClientCount(), 0);

	status = context->api->BotSetupClient(1, &setup_settings);
	assert_true(status);
	assert_ptr_equal(BotState_Get(1), state_slot_one);
	assert_int_equal(BotState_ActiveClientCount(), 1);

	Mock_ClearPrints(&context->mock);
	status = context->api->BotMoveClient(1, 2);
	assert_int_equal(status, BLERR_NOERROR);
	assert_null(Mock_FindPrint(&context->mock, "invalid client number"));
	assert_ptr_equal(BotState_Get(1), state_slot_one);
	assert_memory_equal(state_slot_one, zero_record, sizeof(zero_record));
	assert_ptr_equal(BotState_Get(2), state_sentinel);
	assert_true(state_sentinel->active);
	assert_int_equal(state_sentinel->client_number, 1);
	assert_int_equal(state_sentinel->entity_number, 2);
	assert_int_equal(BotState_ActiveClientCount(), 1);

	status = context->api->BotShutdownClient(2);
	assert_int_equal(status, BLERR_NOERROR);
	assert_ptr_equal(BotState_Get(2), state_sentinel);
	assert_memory_equal(state_sentinel, zero_record, sizeof(zero_record));
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

/*
=============
test_pointlight_heap_is_not_initialised_by_library_setup

Pin the uncalled retail max_aaslights initializer: BotSetup configures sound
pools but leaves point-light storage empty, regardless of its libvar value.
=============
*/
static void test_pointlight_heap_is_not_initialised_by_library_setup(void **state)
{
	bot_interface_test_context_t *context =
		(bot_interface_test_context_t *)*state;
	Mock_Reset(&context->mock);
	assert_int_equal(context->api->BotLibVarSet("max_soundinfo", "0"),
		BLERR_NOERROR);
	assert_int_equal(context->api->BotLibVarSet("max_aassounds", "0"),
		BLERR_NOERROR);
	assert_int_equal(context->api->BotLibVarSet("max_aaslights", "1"),
		BLERR_NOERROR);
	assert_int_equal(context->api->BotSetupLibrary(), BLERR_NOERROR);
	assert_null(Mock_FindPrint(&context->mock,
		"AAS_Sound: max_soundinfo disabled"));

	Mock_Reset(&context->mock);
	vec3_t origin = {1.0f, 2.0f, 3.0f};
	assert_int_equal(context->api->BotAddPointLight(origin,
		7,
		64.0f,
		0.1f,
		0.2f,
		0.3f,
		0.0f,
		0.5f),
		BLERR_NOERROR);
	assert_int_equal(AAS_SoundSubsystem_PointLightCount(), 0);
	assert_non_null(Mock_FindPrint(&context->mock, "WARNING: empty light heap"));
	assert_null(Mock_FindPrint(&context->mock,
		"BotAddPointLight: point light queue capacity exceeded"));
	assert_int_equal(context->api->BotShutdownLibrary(), BLERR_NOERROR);

	Mock_Reset(&context->mock);
	Mock_ReacquireAPI(context);
	assert_int_equal(context->api->BotLibVarSet("max_aaslights", "0"),
		BLERR_NOERROR);
	assert_int_equal(context->api->BotSetupLibrary(), BLERR_NOERROR);
	Mock_Reset(&context->mock);
	assert_int_equal(context->api->BotAddPointLight(origin,
		7,
		64.0f,
		0.1f,
		0.2f,
		0.3f,
		0.0f,
		0.5f),
		BLERR_NOERROR);
	assert_non_null(Mock_FindPrint(&context->mock, "WARNING: empty light heap"));
	assert_null(Mock_FindPrint(&context->mock,
		"BotAddPointLight: point light queue capacity exceeded"));
	assert_int_equal(context->api->BotShutdownLibrary(), BLERR_NOERROR);
}

static void test_bot_load_map_and_sensory_queues(void **state)
{
    bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;

	if (!ensure_map_fixture(&context->assets, "test1"))
	{
		cmocka_skip();
	}

    Mock_Reset(&context->mock);

    assert_int_equal(context->api->BotLibVarSet("max_soundinfo", "64"),
		BLERR_NOERROR);
    assert_int_equal(context->api->BotLibVarSet("max_aassounds", "4"),
		BLERR_NOERROR);

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

	assert_int_equal((int)AAS_SoundSubsystem_SoundEventCount(), 0);
	assert_int_equal(context->api->BotStartFrame(0.001f), BLERR_NOERROR);
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
    assert_int_equal((int)AAS_SoundSubsystem_PointLightCount(), 0);
	assert_non_null(Mock_FindPrint(&context->mock, "WARNING: empty light heap"));

    const aas_pointlight_event_summary_t *light_summaries = NULL;
    size_t light_summary_count = AAS_SoundSubsystem_PointLightSummaries(&light_summaries);
    assert_int_equal(light_summary_count, 0);
    assert_null(light_summaries);

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
	enemy_state->client_number = 2;
	enemy_state->entity_number = 3;
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
	Mock_ReacquireAPI(context);

    assert_int_equal(context->api->BotLibVarSet("max_soundinfo", "1"),
		BLERR_NOERROR);
    assert_int_equal(context->api->BotLibVarSet("max_aassounds", "2"),
		BLERR_NOERROR);

    status = context->api->BotSetupLibrary();
    assert_int_equal(status, BLERR_NOERROR);
    assert_non_null(Mock_FindPrint(&context->mock, "AAS_Sound: discarding soundinfo"));

    Mock_Reset(&context->mock);

    status = context->api->BotLoadMap("maps/test1.bsp", 0, NULL, 2, sounds, 0, NULL);
    assert_int_equal(status, BLERR_NOERROR);
    assert_int_equal((int)AAS_SoundSubsystem_InfoCount(), 1);

    context->api->BotShutdownLibrary();
	Mock_ReacquireAPI(context);

    Mock_Reset(&context->mock);

    assert_int_equal(context->api->BotLibVarSet("max_soundinfo", "0"),
		BLERR_NOERROR);
    assert_int_equal(context->api->BotLibVarSet("max_aassounds", "0"),
		BLERR_NOERROR);
	assert_int_equal(context->api->BotLibVarSet("max_aaslights", "1"),
		BLERR_NOERROR);

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
	assert_int_equal(AAS_SoundSubsystem_PointLightCount(), 0);
	assert_non_null(Mock_FindPrint(&context->mock, "WARNING: empty light heap"));
	assert_null(Mock_FindPrint(&context->mock,
		"BotAddPointLight: point light queue capacity exceeded"));

	context->api->BotShutdownLibrary();
	Mock_ReacquireAPI(context);
	Mock_Reset(&context->mock);
	assert_int_equal(context->api->BotLibVarSet("max_aaslights", "0"),
		BLERR_NOERROR);
	status = context->api->BotSetupLibrary();
	assert_int_equal(status, BLERR_NOERROR);
	status = context->api->BotLoadMap("maps/test1.bsp", 0, NULL, 2, sounds, 0, NULL);
	assert_int_equal(status, BLERR_NOERROR);
	status = context->api->BotAddPointLight(origin,
		7,
		64.0f,
		0.1f,
		0.2f,
		0.3f,
		0.0f,
		0.5f);
	assert_int_equal(status, BLERR_NOERROR);
	assert_non_null(Mock_FindPrint(&context->mock, "WARNING: empty light heap"));
	assert_null(Mock_FindPrint(&context->mock,
		"BotAddPointLight: point light queue capacity exceeded"));

    context->api->BotShutdownLibrary();

	assert_null(Mock_FindPrint(&context->mock, "------- BotLib Shutdown -------"));
    assert_null(Mock_FindPrint(&context->mock, "stub invoked"));

    assert_false(aasworld.initialized);
    assert_false(EA_IsInitialised());
    assert_false(L_Utils_IsInitialised());
    assert_false(L_Struct_IsInitialised());
    assert_null(Bridge_MaxClients());
}

/*
=============
test_bot_load_map_keeps_clients_active_and_runs_deathmatch_pass

Pins retail's map-load reset driver 0x10029c10: sub_10029a40 saves the record
head across the 0x10029afc memset and writes it back at 0x10029b42, so a client
set up before the load keeps the only active flag BotUpdateClient tests
(0x1002989d). Also pins the trailing sub_10028c30 pass at 0x10029c63, which
registers the twelve deathmatch libvars and warns once per unresolved CTF flag.
=============
*/
static void test_bot_load_map_keeps_clients_active_and_runs_deathmatch_pass(
	void **state)
{
	bot_interface_test_context_t *context =
		(bot_interface_test_context_t *)*state;

	if (!ensure_map_fixture(&context->assets, "test1"))
	{
		cmocka_skip();
	}

	Mock_Reset(&context->mock);
	assert_int_equal(context->api->BotLibVarSet("maxclients", "4"),
		BLERR_NOERROR);
	assert_int_equal(context->api->BotSetupLibrary(), BLERR_NOERROR);
	/* Setup drops the whole variable list (LibVar_ResetCache), so the CTF gate
	   sub_10028c30 reads has to be armed after it. */
	assert_int_equal(context->api->BotLibVarSet("ctf", "1"), BLERR_NOERROR);

	bot_settings_t settings;
	memset(&settings, 0, sizeof(settings));
	snprintf(settings.characterfile,
		sizeof(settings.characterfile),
		"bots/babe_c.c");
	snprintf(settings.charactername, sizeof(settings.charactername), "babe");
	assert_true(context->api->BotSetupClient(0, &settings));

	Mock_ClearPrints(&context->mock);
	assert_int_equal(context->api->BotLoadMap("maps/test1.bsp",
		0,
		NULL,
		0,
		NULL,
		0,
		NULL),
		BLERR_NOERROR);

	/* The two CTF warnings sit between the map-loading banners, in retail's
	   red-then-blue order (0x10028d5e then 0x10028d86). */
	size_t red_warning = context->mock.print_count;
	size_t blue_warning = context->mock.print_count;
	size_t opening_banner = context->mock.print_count;
	size_t closing_banner = context->mock.print_count;
	for (size_t index = 0; index < context->mock.print_count; ++index)
	{
		const captured_print_t *entry = &context->mock.prints[index];
		if (entry->type == PRT_MESSAGE &&
			strcmp(entry->message,
				"------------ Map Loading ------------\n") == 0)
		{
			opening_banner = index;
		}
		else if (entry->type == PRT_MESSAGE &&
			strcmp(entry->message,
				"-------------------------------------\n") == 0 &&
			closing_banner == context->mock.print_count)
		{
			closing_banner = index;
		}
		else if (entry->type == PRT_WARNING &&
			strcmp(entry->message, "CTF without Red Flag\n") == 0)
		{
			red_warning = index;
		}
		else if (entry->type == PRT_WARNING &&
			strcmp(entry->message, "CTF without Blue Flag\n") == 0)
		{
			blue_warning = index;
		}
	}
	assert_true(opening_banner < context->mock.print_count);
	assert_true(closing_banner < context->mock.print_count);

	/* The warning is emitted for exactly the flags BotGetLevelItemGoal cannot
	   resolve after the load, and only then. */
	bot_goal_t flag_goal;
	char red_flag_name[] = "Red Flag";
	char blue_flag_name[] = "Blue Flag";
	const bool red_missing = context->api->BotGetLevelItemGoal(-1,
		red_flag_name,
		&flag_goal) < 0;
	const bool blue_missing = context->api->BotGetLevelItemGoal(-1,
		blue_flag_name,
		&flag_goal) < 0;
	if (red_missing)
	{
		assert_true(red_warning < context->mock.print_count);
		assert_true(opening_banner < red_warning);
		assert_true(red_warning < closing_banner);
	}
	else
	{
		assert_int_equal(red_warning, context->mock.print_count);
	}
	if (blue_missing)
	{
		assert_true(blue_warning < context->mock.print_count);
		assert_true(opening_banner < blue_warning);
		assert_true(blue_warning < closing_banner);
	}
	else
	{
		assert_int_equal(blue_warning, context->mock.print_count);
	}
	if (red_missing && blue_missing)
	{
		/* Retail resolves red before blue (0x10028d5e then 0x10028d86). */
		assert_true(red_warning < blue_warning);
	}

	/* sub_10028c30 registers the three libvars nothing else in the library
	   touches; LibVarGetString yields the empty string for a name that was
	   never registered. */
	assert_string_equal(LibVarGetString("runes"), "0");
	assert_string_equal(LibVarGetString("teamplay_shell"), "0");
	assert_string_equal(LibVarGetString("assimilation"), "0");
	assert_string_equal(LibVarGetString("rocketjump"), "1");
	/* LibVar never overwrites an existing value, so the ctf setting above
	   survives its own registration. */
	assert_string_equal(LibVarGetString("ctf"), "1");

	/* Retail keeps the record head, so the client set up before the load is
	   still updatable and no PRT_FATAL inactive diagnostic is emitted. */
	bot_client_state_t *bot = BotState_Get(0);
	assert_non_null(bot);
	assert_true(bot->active);

	bot_updateclient_t update;
	memset(&update, 0, sizeof(update));
	update.pm_type = PM_NORMAL;
	update.pm_flags = PMF_ON_GROUND;
	VectorSet(update.origin, 8.0f, 16.0f, 24.0f);
	update.stats[STAT_HEALTH] = 100;
	Mock_ClearPrints(&context->mock);
	assert_int_equal(context->api->BotStartFrame(0.1f), BLERR_NOERROR);
	assert_int_equal(context->api->BotUpdateClient(0, &update), BLERR_NOERROR);
	assert_null(Mock_FindPrint(&context->mock,
		"tried to updated inactive bot client\n"));
	assert_true(bot->client_update_valid);
	assert_float_equal(bot->last_client_update.origin[0], 8.0f, 0.0001f);
	assert_float_equal(bot->last_client_update.origin[1], 16.0f, 0.0001f);
	assert_float_equal(bot->last_client_update.origin[2], 24.0f, 0.0001f);

	assert_int_equal(context->api->BotLibVarSet("ctf", "0"), BLERR_NOERROR);
	assert_int_equal(context->api->BotShutdownClient(0), BLERR_NOERROR);
	assert_int_equal(context->api->BotShutdownLibrary(), BLERR_NOERROR);
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
	enemy_state->client_number = 2;
	enemy_state->entity_number = 3;
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
    assert_float_equal(context->mock.inputs[0].viewangles[1], 0.0f, 0.0001f);
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
	const bot_console_message_node_t *head = BotNextConsoleMessage(
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
	assert_int_equal(ClientFromName("Babe"), 1);
	assert_int_equal(ClientFromName("babe"), 0);
	assert_int_equal(ClientFromName("missing"), 0);
	assert_int_equal(BotState_FindClientByName("babe"), 1);

	bot_client_state_t *client_state = BotState_Get(1);
	assert_non_null(client_state);
	client_state->combat.current_enemy = 3;

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

	/* Retail's no-space template captures the separator on the victim name.
	 * Exact ClientFromName lookup aliases both misses to client zero. */
	assert_int_equal(client_state->bot_death_type, 0);
	assert_int_equal(client_state->enemy_death_type, 0);
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
	BotInitLevelItems();

	bot_settings_t settings;
	memset(&settings, 0, sizeof(settings));
	snprintf(settings.characterfile, sizeof(settings.characterfile), "bots/babe_c.c");
	snprintf(settings.charactername, sizeof(settings.charactername), "babe");
	assert_true(context->api->BotSetupClient(1, &settings));

	/*
	 * BotAddressedToBot gates on BotSameTeam(bs, client + 1) and returns 0
	 * when it fails (ref be_ai2_dmq2.c:2384-2386), so the speaker has to be a
	 * real teammate by retail's own rules for any of these command handlers to
	 * run.  Retail has no semantic team field - BotSameTeam resolves the
	 * relationship from teamplay_shell, ch, teamplay, dmflags and ctf - so the
	 * fixture establishes it the way a server would.
	 *
	 * Identical skins satisfy both arms a test may select: the teamplay arm
	 * compares the FULL skin with _strcmpi (0x10023683) and the DF_SKINTEAMS
	 * arm compares only the suffix from '/' (0x1002380e).  DF_SKINTEAMS is the
	 * default here so the handlers run with teamplay off; a test that wants
	 * the negative gate sets dmflags/teamplay to 0 explicitly and still gets
	 * "not same team", because no arm then matches.
	 */
	bot_clientsettings_t presentation;
	memset(&presentation, 0, sizeof(presentation));
	snprintf(presentation.netname, sizeof(presentation.netname), "Babe");
	snprintf(presentation.skin, sizeof(presentation.skin), "female/athena");
	assert_int_equal(context->api->BotClientSettings(1, &presentation),
		BLERR_NOERROR);
	memset(&presentation, 0, sizeof(presentation));
	snprintf(presentation.netname, sizeof(presentation.netname), "Commander");
	snprintf(presentation.skin, sizeof(presentation.skin), "female/athena");
	assert_int_equal(context->api->BotClientSettings(2, &presentation),
		BLERR_NOERROR);

	bot_client_state_t *bot = BotState_Get(1);
	assert_non_null(bot);
	bot_client_state_t *teammate = BotState_Create(2);
	assert_non_null(teammate);
	teammate->client_number = 2;
	teammate->entity_number = 3;
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

Queues one aged chat message and advances the console dispatcher through a
stand-node frame, isolating command parsing from the separately tested LTG
scheduler execution.
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
	bot_client_state_t *bot = BotState_Get(1);
	assert_non_null(bot);
	/* Keep parser tests out of the enter-game and LTG scheduler paths. */
	bot->enter_game_time = -1000.0f;
	bot->chat_standing = false;
	bot->ai_node = BOT_AI_NODE_STAND;
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
		const char *argument = mock->client_commands[index].argument;
		if ((strcmp(command, "say_team") == 0 &&
				strstr(argument, text) != NULL) ||
			(strncmp(command, "say_team ", 9U) == 0 &&
				strstr(command, text) != NULL))
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
		if (strcmp(command, "say_team") == 0)
		{
			return mock->client_commands[index - 1U].argument;
		}
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
	aasworld.areas = GetClearedMemory((size_t)aasworld.numAreas *
		sizeof(*aasworld.areas));
	aasworld.areasettings = GetClearedMemory((size_t)aasworld.numAreaSettings *
		sizeof(*aasworld.areasettings));
	assert_non_null(aasworld.areas);
	assert_non_null(aasworld.areasettings);
	VectorSet(aasworld.areas[1].mins, -128.0f, -128.0f, -64.0f);
	VectorSet(aasworld.areas[1].maxs, 128.0f, 128.0f, 128.0f);
	aasworld.areasettings[1].presencetype =
		PRESENCE_NORMAL | PRESENCE_CROUCH;
	aasworld.areasettings[1].numreachableareas = 1;
	TranslateEntity_SetWorldLoaded(qtrue);
	BotInitLevelItems();
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
		if (strcmp(context->mock.client_commands[index].command,
			"say_team") == 0 ||
			strncmp(context->mock.client_commands[index].command,
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
	assert_int_equal(bot->ltg_teammate, 0);
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

	bot->enter_game_time = 123.0f;
	bot->get_flag_away_time = 456.0f;
	BotState_ResetForNewMap(bot);
	assert_int_equal(bot->ltg_type, 0);
	assert_int_equal(bot->ltg_teammate, 0);
	assert_int_equal(bot->team_goal_number, 0);
	assert_float_equal(bot->enter_game_time, 0.0f, 0.0001f);
	assert_float_equal(bot->get_flag_away_time, 0.0f, 0.0001f);

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

Pins case 16's three gates, unit factors, punctuation normalization, and the
100-unit fallback for values outside the valid range.
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
	/* Retail UnifyWhiteSpaces converts the period to a space before atof. */
	assert_float_equal(bot->formation_dist, 100.0f, 0.0001f);
	process_console_team_command(context,
		15.0f,
		"(Commander): Babe the formation intervening space is 15.625 meter");
	assert_float_equal(bot->formation_dist, 480.0f, 0.0001f);
	process_console_team_command(context,
		18.0f,
		"(Commander): Babe the formation intervening space is 1.49 meter");
	assert_float_equal(bot->formation_dist, 100.0f, 0.0001f);
	process_console_team_command(context,
		21.0f,
		"(Commander): Babe the formation intervening space is 15.626 meter");
	assert_float_equal(bot->formation_dist, 480.0f, 0.0001f);
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
	assert_int_equal(context->api->BotLibVarSet("teamplay", "0"),
		BLERR_NOERROR);
	assert_int_equal(context->api->BotLibVarSet("ctf", "0"),
		BLERR_NOERROR);
	assert_int_equal(context->api->BotLibVarSet("dmflags", "0"),
		BLERR_NOERROR);
	strcpy(bot->subteam, "Keep");
	bot->formation_dist = 77.0f;
	bot->ltg_type = 1;

	process_console_team_command(context,
		3.0f,
		"(Commander): Babe form the wedge formation");
	assert_string_equal(bot->subteam, "Keep");
	assert_float_equal(bot->formation_dist, 77.0f, 0.0001f);
	assert_int_equal(bot->ltg_type, 1);
	assert_float_equal(LibVarGetValue("teamplay"), 0.0f, 0.0001f);
	assert_float_equal(LibVarGetValue("ctf"), 0.0f, 0.0001f);
	assert_float_equal(LibVarGetValue("dmflags"), 0.0f, 0.0001f);

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
	assert_int_equal(bot->team_goal.entitynum, 2);
	assert_int_equal(bot->team_goal.areanum, 1);
	assert_int_equal(bot->team_goal.number, 302);
	/* Retail level-item lookup leaves flags untouched; the initial zero survives
	 * the subsequent in-place camp-there point update. */
	assert_int_equal(bot->team_goal.flags, 0);
	assert_float_equal(bot->team_goal.origin[0], 0.0f, 0.0001f);
	assert_float_equal(bot->team_goal.mins[0], -8.0f, 0.0001f);
	assert_float_equal(bot->team_goal.maxs[2], 8.0f, 0.0001f);

	teammate->client_update_valid = true;
	VectorSet(teammate->last_client_update.origin, 32.0f, 0.0f, 32.0f);
	process_console_team_command(context,
		24.0f,
		"(Commander): Babe camp here");
	assert_int_equal(bot->team_goal.entitynum, 3);
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
	assert_ptr_equal(bot->checkpoints->name,
		bot->checkpoints->name_storage);
	void *allocation_probe = GetMemory(1U);
	assert_non_null(allocation_probe);
	assert_int_equal(MemoryByteSize(bot->checkpoints) -
		MemoryByteSize(allocation_probe),
		sizeof(*bot->checkpoints) + strlen("Alpha"));
	FreeMemory(allocation_probe);
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
	/* The decimal point is normalized to a separator before sscanf, so only the
	 * first three integer fields are consumed before the retail +0.5 offset. */
	assert_float_equal(bot->checkpoints->goal.origin[2], 32.5f, 0.0001f);
	assert_non_null(bot->checkpoints->next);
	assert_string_equal(bot->checkpoints->next->name, "Rival");
	assert_null(bot->checkpoints->next->next);
	assert_int_equal(count_team_chat_commands(&context->mock), 1U);
	assert_true(find_constructed_team_chat(&context->mock,
		"checkpoint alpha at 48 0 32 confirmed"));

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
allocate_test_console_waypoint

Allocates a tracked flexible-name waypoint for direct state-machine fixtures.
=============
*/
static bot_console_waypoint_t *allocate_test_console_waypoint(const char *name)
{
	const char *waypoint_name = name != NULL ? name : "";
	size_t name_bytes = strlen(waypoint_name) + 1U;
	bot_console_waypoint_t *waypoint = GetClearedMemory(
		sizeof(*waypoint) + name_bytes);
	assert_non_null(waypoint);
	waypoint->name = waypoint->name_storage;
	memcpy(waypoint->name, waypoint_name, name_bytes);
	return waypoint;
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

	bot_console_waypoint_t *old = allocate_test_console_waypoint("Old Patrol");
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
	FreeMemory(old);
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
	ally->client_number = 3;
	ally->entity_number = 4;
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
	assert_int_equal(bot->team_goal.entitynum, 3);
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
	assert_int_equal(bot->team_goal.entitynum, 3);
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
test_bot_console_ctf_rush_base_is_dormant_but_enemy_flag_order_runs

Pins retail preprocessing and matching together. Both CTF phrases that name a
syn.c flag word are dormant: `base` canonicalizes to a flag name so the literal
rush-base template can never match (case 6), and `enemy flag` canonicalizes to
`Red Flag`/`Blue Flag` while retail StringReplaceWords (sub_1002af30) copies
only strlen(tail) bytes and drops the terminator, leaving a `Flagag`/`Flagg`
residue that defeats the case-7 template's trailing `flag` literal. Case 7 is
exercised with a synonym-free phrasing (`get the flag`), which retail accepts.
=============
*/
static void test_bot_console_ctf_rush_base_is_dormant_but_enemy_flag_order_runs(
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

	bot->ltg_type = 9;
	bot->team_message_time = 44.0f;
	bot->team_goal_time = 77.0f;
	bot->rush_base_away_time = 55.0f;
	process_console_team_command(context,
		15.0f,
		"(Commander): Other rush base");
	assert_int_equal(bot->ltg_type, 9);
	assert_float_equal(bot->team_message_time, 44.0f, 0.0001f);
	assert_float_equal(bot->team_goal_time, 77.0f, 0.0001f);
	assert_float_equal(bot->rush_base_away_time, 55.0f, 0.0001f);

	process_console_team_command(context,
		18.0f,
		"(Commander): Babe rush base");
	assert_int_equal(bot->ltg_type, 9);
	assert_float_equal(bot->team_message_time, 44.0f, 0.0001f);
	assert_float_equal(bot->team_goal_time, 77.0f, 0.0001f);
	assert_float_equal(bot->rush_base_away_time, 55.0f, 0.0001f);

	process_console_team_command(context,
		21.0f,
		"(Commander): Other get the flag");
	assert_int_equal(bot->ltg_type, 9);
	assert_float_equal(bot->team_message_time, 44.0f, 0.0001f);
	assert_float_equal(bot->team_goal_time, 77.0f, 0.0001f);
	assert_float_equal(bot->rush_base_away_time, 55.0f, 0.0001f);

	/* The addressed `enemy flag` phrasing can never reach case 7 under ctf:
	   BotReplaceSynonyms rewrites the pair to the group's first phrase
	   (`Red Flag`/`Blue Flag`, syn.c:63 and :67), and retail
	   StringReplaceWords' memmove omits the terminator, so the shorter
	   replacement at end of string leaves a `Flagag`/`Flagg` residue that the
	   template's trailing `flag` literal cannot consume. */
	process_console_team_command(context,
		22.0f,
		"(Commander): Babe get the enemy flag");
	assert_int_equal(bot->ltg_type, 9);
	assert_float_equal(bot->team_message_time, 44.0f, 0.0001f);
	assert_float_equal(bot->team_goal_time, 77.0f, 0.0001f);
	assert_float_equal(bot->rush_base_away_time, 55.0f, 0.0001f);

	srand(5);
	process_console_team_command(context,
		24.0f,
		"(Commander): Babe get the flag");
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
	client_state->enter_game_time = -1000.0f;
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
		const char *command = context->mock.client_commands[index].command;
		const char *argument = context->mock.client_commands[index].argument;
		if (context->mock.client_commands[index].client == 1 &&
			((strcmp(command, "say") == 0 && argument[0] != '\0') ||
				strncmp(command, "say ", 4U) == 0))
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
test_q3_compat_chat_exports_preserve_type_aliases

Checks the extended bridge keeps its successor-shaped chat adapters without
conflating them with the retail-exact direct entry points.
=============
*/
static void test_q3_compat_chat_exports_preserve_type_aliases(void **state)
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
	/* The live MTCONTEXT_CLIENTOBITUARY template is unspaced
	   (`VICTIM, "was railed by", KILLER`, dev_tools/assets/match.c:198 -- the
	   spaced 3.14 forms are inside a block comment and never parsed), and
	   retail StringsMatch (0x1002c800) stores a raw span: ptr is the cursor
	   where the variable opened and length is (found position of the next
	   literal) - ptr, or strlen(ptr) for a trailing variable.  So the
	   separator stays on both sides of a capture -- trailing on VICTIM,
	   leading on KILLER. */
	context->api->BotMatchVariable(&match, 0, variable, sizeof(variable));
	assert_string_equal(variable, "Alice ");
	context->api->BotMatchVariable(&match, 1, variable, sizeof(variable));
	assert_string_equal(variable, " Bob");

	bot_chatstate_t *chat = context->api->BotAllocChatState();
	assert_non_null(chat);
	assert_int_equal(context->api->BotLoadChatFile(chat,
		"bots/babe_t.c",
		"babe"),
		BLERR_NOERROR);

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
		"couldn't find definitely_missing_rchat.c\n",
		PRT_ERROR);

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

    fixture->areas = (aas_area_t *)GetClearedMemory(
        (size_t)(aasworld.numAreas + 1) * sizeof(aas_area_t));
    fixture->area_settings = (aas_areasettings_t *)GetClearedMemory(
        (size_t)aasworld.numAreaSettings * sizeof(aas_areasettings_t));
    fixture->reachability = (aas_reachability_t *)GetClearedMemory(
        (size_t)aasworld.numReachability * sizeof(aas_reachability_t));
    fixture->entities = (aas_entity_t *)GetClearedMemory(
        (size_t)aasworld.maxEntities * sizeof(aas_entity_t));

    assert_non_null(fixture->areas);
    assert_non_null(fixture->area_settings);
    assert_non_null(fixture->reachability);
    assert_non_null(fixture->entities);

    aasworld.areas = fixture->areas;
    aasworld.areasettings = fixture->area_settings;
    aasworld.reachability = fixture->reachability;
    aasworld.entities = fixture->entities;

    /* The fixture exercises AAS-area links; no BSP leaves are installed. */
    AAS_InitAASLinkHeap();
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

/*
=============
test_bot_interface_mover_parity

Retail AAS_UpdateEntity (sub_1000a920) relinks changed BSP entities without
emitting a brush-model diagnostic.
=============
*/
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
	assert_true(BotMove_MoverCatalogueFinalize(model_entries,
		ARRAY_LEN(model_entries)));

    bot_updateentity_t mover_update;
    memset(&mover_update, 0, sizeof(mover_update));
    VectorSet(mover_update.origin, 64.0f, 0.0f, 16.0f);
    VectorSet(mover_update.old_origin, 64.0f, 0.0f, 16.0f);
    VectorSet(mover_update.mins, -32.0f, -32.0f, -16.0f);
    VectorSet(mover_update.maxs, 32.0f, 32.0f, 16.0f);
    mover_update.solid = SOLID_BSP;
    mover_update.modelindex = mover_entry.modelnum + 1;

	Mock_ClearPrints(&context->mock);
	status = context->api->BotUpdateEntity(3, &mover_update);
	assert_int_equal(status, BLERR_NOERROR);
	assert_int_equal(context->mock.print_count, 0);
	Mock_Reset(&context->mock);

    bot_settings_t settings;
    memset(&settings, 0, sizeof(settings));
    snprintf(settings.characterfile, sizeof(settings.characterfile), "bots/babe_c.c");
    snprintf(settings.charactername, sizeof(settings.charactername), "babe");
    status = context->api->BotSetupClient(1, &settings);
    assert_true(status);

    bot_client_state_t *client_state = BotState_Get(1);
    assert_non_null(client_state);
	client_state->enter_game_time = -1000.0f;

    bot_goal_t mover_goal;
    memset(&mover_goal, 0, sizeof(mover_goal));
    mover_goal.number = 42;
    mover_goal.areanum = 3;
    VectorSet(mover_goal.origin, 256.0f, 0.0f, 32.0f);

    status = context->api->BotPushGoal(client_state->goal_handle, &mover_goal);
    assert_true(status != 0);
	client_state->long_term_goal_time = 20.0f;

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

	bot_movestate_t *move_state = BotMoveStateFromHandle(
		client_state->move_handle);
	assert_non_null(move_state);
	move_state->avoidreach[0] = 73;
	move_state->avoidreachtimes[0] = aasworld.time + 10.0f;
	move_state->avoidreachtries[0] = 5;

	status = context->api->BotAI(1, 0.05f);
	assert_int_equal(status, BLERR_NOERROR);
	assert_true(client_state->has_move_result);
	assert_true(client_state->last_move_result.failure);
	assert_float_equal(client_state->long_term_goal_time, 0.0f, 0.0001f);
	assert_int_equal(move_state->avoidreach[0], 0);
	assert_float_equal(move_state->avoidreachtimes[0], 0.0f, 0.0001f);
	assert_int_equal(move_state->avoidreachtries[0], 0);

	Mock_ClearPrints(&context->mock);
	BotDumpAvoidGoals(client_state->goal_handle);
	assert_int_equal(context->mock.print_count, 0);

    ai_avoid_list_t *avoid = AI_GoalState_GetAvoidList(client_state->goal_state);
    assert_non_null(avoid);
	assert_false(AI_AvoidList_Contains(avoid, mover_goal.number, 0.1f));

	Mock_Reset(&context->mock);

	(void)context->api->BotPopGoal(client_state->goal_handle);
	mover_goal.areanum = 2;
	VectorSet(mover_goal.origin, 128.0f, 0.0f, 32.0f);
	assert_true(context->api->BotPushGoal(client_state->goal_handle,
		&mover_goal));

	fixture.reachability[1].traveltype = TRAVEL_ELEVATOR;
	fixture.reachability[1].facenum = mover_entry.modelnum;
	AAS_InitTravelFlagFromType();
	assert_int_equal(AAS_PrepareReachability(), BLERR_NOERROR);
	client_state->long_term_goal_time = 26.0f;

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
	assert_int_equal(client_state->last_move_result.traveltype, 0);
    assert_int_equal(client_state->last_move_result.type, RESULTTYPE_ELEVATORUP);
    assert_true((client_state->last_move_result.flags & MOVERESULT_WAITING) != 0);

	assert_float_equal(final_input->speed, 360.0f, 0.0001f);
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
test_ai_battle_chase_preserves_mover_set_view

Pins Battle Chase's `MOVERESULT_MOVEMENTVIEWSET` exit: a rocket-jump mover
sets its own down-facing view and skips the private accelerated view turn.
=============
*/
static void test_ai_battle_chase_preserves_mover_set_view(void **state)
{
	bot_interface_test_context_t *context =
		(bot_interface_test_context_t *)*state;
	Mock_Reset(&context->mock);
	assert_int_equal(context->api->BotSetupLibrary(), BLERR_NOERROR);

	bot_mover_fixture_t fixture;
	memset(&fixture, 0, sizeof(fixture));
	bot_mover_fixture_init(&fixture);
	fixture.reachability[1].traveltype = TRAVEL_ROCKETJUMP;
	fixture.reachability[1].facenum = 0;
	AAS_InitTravelFlagFromType();
	assert_int_equal(AAS_PrepareReachability(), BLERR_NOERROR);

	bot_settings_t settings;
	memset(&settings, 0, sizeof(settings));
	snprintf(settings.characterfile,
		sizeof(settings.characterfile),
		"bots/babe_c.c");
	snprintf(settings.charactername,
		sizeof(settings.charactername),
		"babe");
	assert_true(context->api->BotSetupClient(1, &settings));
	bot_client_state_t *bot = BotState_Get(1);
	assert_non_null(bot);
	bot->enter_game_time = -1000.0f;

	assert_int_equal(context->api->BotLibVarSet("rocketjump", "1"),
		BLERR_NOERROR);
	assert_int_equal(context->api->BotStartFrame(1.0f), BLERR_NOERROR);
	bot_updateclient_t update = {0};
	update.pm_flags = PMF_ON_GROUND;
	update.stats[STAT_HEALTH] = 100;
	update.inventory[RETAIL_INVENTORY_HEALTH] = 100;
	update.inventory[RETAIL_INVENTORY_ROCKETLAUNCHER] = 1;
	update.inventory[RETAIL_INVENTORY_ROCKETS] = 3;
	update.inventory[RETAIL_USING_INVULNERABILITY] = 1;
	assert_int_equal(context->api->BotUpdateClient(1, &update),
		BLERR_NOERROR);

	bot->ai_node = BOT_AI_NODE_BATTLE_CHASE;
	bot->combat.current_enemy = 2;
	bot->combat.last_enemy_area = 2;
	bot->combat.chase_time = aasworld.time + 10.0f;
	bot->long_term_goal_time = aasworld.time + 20.0f;
	bot->nearby_goal_time = aasworld.time + 5.0f;
	bot->nearby_goal_check_time = aasworld.time + 30.0f;
	VectorSet(bot->combat.last_enemy_origin, 128.0f, 0.0f, 32.0f);
	vec3_t ninety_degree_delta = {0.0f, 90.0f, 0.0f};
	AI_DMState_ApplyDeltaAngles(bot->dm_state, ninety_degree_delta);

	assert_int_equal(context->api->BotAI(1, 0.1f), BLERR_NOERROR);
	assert_true(bot->has_move_result);
	assert_int_equal(bot->last_move_result.traveltype, 0);
	assert_true((bot->last_move_result.flags &
		MOVERESULT_MOVEMENTVIEWSET) != 0);
	assert_true(context->mock.bot_input_count > 0U);
	const bot_input_t *input = &context->mock.inputs[
		context->mock.bot_input_count - 1U];
	assert_float_equal(input->viewangles[PITCH], 90.0f, 0.0001f);
	assert_float_equal(input->viewangles[YAW], 0.0f, 0.0001f);

	assert_int_equal(context->api->BotShutdownClient(1), BLERR_NOERROR);
	assert_int_equal(context->api->BotShutdownLibrary(), BLERR_NOERROR);
	bot_mover_fixture_shutdown(&fixture);
}

/*
=============
test_ai_battle_chase_arrival_area_expires_deadline

Pins Battle Chase's post-move area gate: retail clears the deadline when the
mover has reached the remembered enemy area, independently of contact with the
last-seen goal's eight-unit bounds.
=============
*/
static void test_ai_battle_chase_arrival_area_expires_deadline(void **state)
{
	bot_interface_test_context_t *context =
		(bot_interface_test_context_t *)*state;
	Mock_Reset(&context->mock);
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
	bot_client_state_t *bot = BotState_Get(1);
	assert_non_null(bot);
	bot->enter_game_time = -1000.0f;

	assert_int_equal(context->api->BotStartFrame(1.0f), BLERR_NOERROR);
	bot_updateclient_t update = {0};
	update.pm_type = PM_NORMAL;
	update.pm_flags = PMF_ON_GROUND;
	VectorSet(update.origin, 100.0f, 0.0f, 32.0f);
	update.stats[STAT_HEALTH] = 100;
	update.inventory[RETAIL_INVENTORY_HEALTH] = 100;
	assert_int_equal(context->api->BotUpdateClient(1, &update),
		BLERR_NOERROR);

	bot->ai_node = BOT_AI_NODE_BATTLE_CHASE;
	bot->combat.current_enemy = 2;
	bot->combat.last_enemy_area = 2;
	bot->combat.chase_time = aasworld.time + 10.0f;
	VectorSet(bot->combat.last_enemy_origin, 180.0f, 0.0f, 32.0f);

	assert_int_equal(context->api->BotAI(1, 0.1f), BLERR_NOERROR);
	assert_true(bot->has_move_result);
	bot_movestate_t *move_state = BotMoveStateFromHandle(bot->move_handle);
	assert_non_null(move_state);
	assert_int_equal(move_state->areanum, 2);
	assert_float_equal(bot->combat.chase_time, 0.0f, 0.0001f);

	assert_int_equal(context->api->BotShutdownClient(1), BLERR_NOERROR);
	assert_int_equal(context->api->BotShutdownLibrary(), BLERR_NOERROR);
	bot_mover_fixture_shutdown(&fixture);
}

/*
=============
test_ai_battle_nbg_records_enemy_location

Pins Battle NBG's retained-enemy refresh: it stores the live enemy's reachable
area and origin before evaluating the retained nearby goal.
=============
*/
static void test_ai_battle_nbg_records_enemy_location(void **state)
{
	bot_interface_test_context_t *context =
		(bot_interface_test_context_t *)*state;
	Mock_Reset(&context->mock);
	assert_int_equal(context->api->BotSetupLibrary(), BLERR_NOERROR);

	bot_mover_fixture_t fixture;
	memset(&fixture, 0, sizeof(fixture));
	bot_mover_fixture_init(&fixture);
	aasworld.maxClients = 4;

	bot_settings_t settings;
	memset(&settings, 0, sizeof(settings));
	snprintf(settings.characterfile,
		sizeof(settings.characterfile),
		"bots/babe_c.c");
	snprintf(settings.charactername,
		sizeof(settings.charactername),
		"babe");
	assert_true(context->api->BotSetupClient(1, &settings));
	bot_client_state_t *bot = BotState_Get(1);
	assert_non_null(bot);
	bot->enter_game_time = -1000.0f;

	assert_int_equal(context->api->BotStartFrame(1.0f), BLERR_NOERROR);
	bot_updateclient_t update = {0};
	update.pm_type = PM_NORMAL;
	update.pm_flags = PMF_ON_GROUND;
	update.stats[STAT_HEALTH] = 100;
	update.inventory[RETAIL_INVENTORY_HEALTH] = 100;
	assert_int_equal(context->api->BotUpdateClient(1, &update),
		BLERR_NOERROR);

	aas_entity_t *enemy = &aasworld.entities[2];
	enemy->inuse = qtrue;
	enemy->number = 2;
	enemy->modelindex = 255;
	VectorSet(enemy->origin, 128.0f, 0.0f, 32.0f);
	VectorCopy(enemy->origin, enemy->old_origin);

	bot_goal_t nearby_goal = {0};
	nearby_goal.number = 71;
	nearby_goal.areanum = 2;
	VectorSet(nearby_goal.origin, 128.0f, 0.0f, 32.0f);
	VectorSet(nearby_goal.mins, -8.0f, -8.0f, -8.0f);
	VectorSet(nearby_goal.maxs, 8.0f, 8.0f, 8.0f);
	assert_true(context->api->BotPushGoal(bot->goal_handle, &nearby_goal) != 0);

	bot->ai_node = BOT_AI_NODE_BATTLE_NBG;
	bot->combat.current_enemy = 2;
	bot->nearby_goal_time = aasworld.time + 5.0f;
	bot->combat.last_enemy_area = 0;
	VectorClear(bot->combat.last_enemy_origin);

	assert_int_equal(context->api->BotAI(1, 0.1f), BLERR_NOERROR);
	assert_int_equal(bot->combat.last_enemy_area, 2);
	assert_float_equal(bot->combat.last_enemy_origin[0], 128.0f, 0.0001f);
	assert_float_equal(bot->combat.last_enemy_origin[1], 0.0f, 0.0001f);
	assert_float_equal(bot->combat.last_enemy_origin[2], 32.0f, 0.0001f);

	assert_int_equal(context->api->BotShutdownClient(1), BLERR_NOERROR);
	assert_int_equal(context->api->BotShutdownLibrary(), BLERR_NOERROR);
	bot_mover_fixture_shutdown(&fixture);
}

/*
=============
test_ai_entity_to_activate_follows_trigger_target_chain

Pins retail BotEntityToActivate's reverse target traversal: an unshootable
blocked door backtracks out of an unrecognized nested branch before selecting
the next root activator. The blocked entity's engine model index is resolved
through the retail model-name table rather than treated as an inline suffix.
=============
*/
static void test_ai_entity_to_activate_follows_trigger_target_chain(void **state)
{
	(void)state;
	bot_mover_fixture_t fixture;
	memset(&fixture, 0, sizeof(fixture));
	bot_mover_fixture_init(&fixture);
	fixture.entities[1].inuse = qtrue;
	fixture.entities[1].number = 1;
	fixture.entities[1].solid = SOLID_BSP;
	fixture.entities[1].modelindex = 2;

	char empty_model[] = "";
	char world_model[] = "maps/test.bsp";
	char inline_model[] = "*1";
	char *models[] = {empty_model, world_model, inline_model};
	assert_int_equal(AAS_LoadMap(NULL,
		ARRAY_LEN(models),
		models,
		0,
		NULL,
		0,
		NULL),
		BLERR_NOERROR);

	static const char entity_lump[] =
		"{\n"
		"\"classname\" \"worldspawn\"\n"
		"}\n"
		"{\n"
		"\"classname\" \"func_door\"\n"
		"\"model\" \"*1\"\n"
		"\"targetname\" \"door_signal\"\n"
		"}\n"
		"{\n"
		"\"classname\" \"trigger_counter\"\n"
		"\"target\" \"door_signal\"\n"
		"\"targetname\" \"counter_signal\"\n"
		"}\n"
		"{\n"
		"\"classname\" \"target_speaker\"\n"
		"\"target\" \"counter_signal\"\n"
		"}\n"
		"{\n"
		"\"classname\" \"trigger_relay\"\n"
		"\"target\" \"counter_signal\"\n"
		"\"targetname\" \"relay_signal\"\n"
		"}\n"
		"{\n"
		"\"classname\" \"func_button\"\n"
		"\"model\" \"*2\"\n"
		"\"target\" \"relay_signal\"\n"
		"\"health\" \"10\"\n"
		"}\n"
		"{\n"
		"\"classname\" \"trigger_once\"\n"
		"\"model\" \"*3\"\n"
		"\"target\" \"door_signal\"\n"
		"}\n";

	aas_bspentity_t *entities = AAS_ParseBSPEntities(entity_lump,
		strlen(entity_lump));
	assert_non_null(entities);
	const aas_bspentity_t *activation = BotAI_EntityToActivate(entities, 1);
	assert_non_null(activation);
	assert_string_equal(AAS_ValueForBSPEpairKey(activation, "classname"),
		"trigger_once");
	assert_string_equal(AAS_ValueForBSPEpairKey(activation, "model"), "*3");
	AAS_FreeBSPEntities(entities);
	bot_mover_fixture_shutdown(&fixture);
}

/*
=============
test_ai_battle_chase_handles_blocked_move

Pins the post-move BotAIBlocked handoff shared by direct Battle Chase, Retreat,
and NBG movement: an unrecognized blocker retries on the perpendicular axis
instead of submitting the blocked route direction.
=============
*/
static void test_ai_battle_chase_handles_blocked_move(void **state)
{
	bot_interface_test_context_t *context =
		(bot_interface_test_context_t *)*state;
	Mock_Reset(&context->mock);
	assert_int_equal(context->api->BotSetupLibrary(), BLERR_NOERROR);

	bot_mover_fixture_t fixture;
	memset(&fixture, 0, sizeof(fixture));
	bot_mover_fixture_init(&fixture);
	/* Retail derives swimming from the engine point-contents callback rather
	 * than the AAS area's contents flags. */
	context->mock.point_contents_result = CONTENTS_WATER;

	bot_settings_t settings;
	memset(&settings, 0, sizeof(settings));
	snprintf(settings.characterfile,
		sizeof(settings.characterfile),
		"bots/babe_c.c");
	snprintf(settings.charactername,
		sizeof(settings.charactername),
		"babe");
	assert_true(context->api->BotSetupClient(1, &settings));
	bot_client_state_t *bot = BotState_Get(1);
	assert_non_null(bot);
	bot->enter_game_time = -1000.0f;

	assert_int_equal(context->api->BotStartFrame(1.0f), BLERR_NOERROR);
	bot_updateclient_t update = {0};
	update.pm_type = PM_NORMAL;
	update.pm_flags = 0;
	update.stats[STAT_HEALTH] = 100;
	update.inventory[RETAIL_INVENTORY_HEALTH] = 100;
	assert_int_equal(context->api->BotUpdateClient(1, &update),
		BLERR_NOERROR);

	bot->ai_node = BOT_AI_NODE_BATTLE_CHASE;
	bot->combat.current_enemy = 2;
	bot->combat.last_enemy_area = 2;
	bot->combat.chase_time = aasworld.time + 10.0f;
	bot->long_term_goal_time = aasworld.time + 20.0f;
	bot->nearby_goal_time = aasworld.time + 5.0f;
	bot->nearby_goal_check_time = aasworld.time + 30.0f;
	VectorSet(bot->combat.last_enemy_origin, 128.0f, 0.0f, 32.0f);
	context->mock.trace_blocked = true;
	context->mock.trace_entity = 7;

	assert_int_equal(context->api->BotAI(1, 0.1f), BLERR_NOERROR);
	assert_true(bot->has_move_result);
	assert_true(bot->last_move_result.blocked != 0);
	assert_true(context->mock.bot_input_count > 0U);
	const bot_input_t *input = &context->mock.inputs[
		context->mock.bot_input_count - 1U];
	assert_float_equal(input->speed, 400.0f, 0.0001f);
	assert_float_equal(input->dir[0], 0.0f, 0.0001f);
	assert_true(fabsf(input->dir[1]) > 0.99f);
	assert_float_equal(bot->long_term_goal_time, 21.0f, 0.0001f);
	assert_float_equal(bot->nearby_goal_time, 6.0f, 0.0001f);

	assert_int_equal(context->api->BotShutdownClient(1), BLERR_NOERROR);
	assert_int_equal(context->api->BotShutdownLibrary(), BLERR_NOERROR);
	bot_mover_fixture_shutdown(&fixture);
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

Pins sub_10021290's stale cached skinnum byte and effect projections. Retail
AAS_UpdateEntity (sub_1000a920) deliberately skips the incoming skinnum, so
changing update.skinnum cannot select a weapon destination.
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
			assert_int_equal(inventory[slot], 0);
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

/*
=============
test_general_item_use_retail_order_and_boundaries

Pins sub_10021500's four independent uses, exact callback order, strict raw
slot gates, liquid mask, eye-position sample, stale-slot reads, and immutability.
=============
*/
static void test_general_item_use_retail_order_and_boundaries(void **state)
{
	bot_interface_test_context_t *context =
		(bot_interface_test_context_t *)*state;
	Mock_Reset(&context->mock);
	assert_false(EA_IsInitialised());
	assert_int_equal(EA_Init(8), BLERR_NOERROR);

	bot_client_state_t bot;
	memset(&bot, 0, sizeof(bot));
	bot.client_number = 3;
	VectorSet(bot.last_client_update.origin, 100.0f, 200.0f, 300.0f);
	VectorSet(bot.last_client_update.viewoffset, 1.0f, 2.0f, 3.0f);
	int *inventory = bot.last_client_update.inventory;
	inventory[RETAIL_INVENTORY_SILENCER] = 1;
	inventory[RETAIL_INVENTORY_REBREATHER] = 1;
	inventory[RETAIL_INVENTORY_POWERSHIELD] = 1;
	inventory[RETAIL_INVENTORY_POWERSCREEN] = 1;
	inventory[RETAIL_INVENTORY_QUAD] = 723;
	inventory[RETAIL_INVENTORY_INVULNERABILITY] = 724;
	inventory[RETAIL_USING_QUAD] = -204;
	inventory[RETAIL_USING_INVULNERABILITY] = -205;
	context->mock.point_contents_result = 0x38;

	bot_client_state_t snapshot = bot;
	BotAI_UseItems(&bot);
	assert_memory_equal(&bot, &snapshot, sizeof(bot));
	assert_int_equal(context->mock.client_command_count, 4U);
	const char *expected_items[] = {
		"Silencer",
		"Rebreather",
		"Power Shield",
		"Power Screen",
	};
	for (size_t index = 0; index < ARRAY_LEN(expected_items); ++index)
	{
		assert_int_equal(context->mock.client_commands[index].client, 3);
		assert_string_equal(context->mock.client_commands[index].command, "use");
		assert_string_equal(context->mock.client_commands[index].argument,
			expected_items[index]);
	}
	assert_int_equal(context->mock.point_contents_count, 1U);
	assert_int_equal(context->mock.point_contents_command_counts[0], 1U);
	vec3_t expected_eye = {101.0f, 202.0f, 303.0f};
	assert_memory_equal(context->mock.point_contents_points[0],
		expected_eye,
		sizeof(expected_eye));

	Mock_Reset(&context->mock);
	BotAI_UseItems(NULL);
	assert_int_equal(context->mock.client_command_count, 0U);
	assert_int_equal(context->mock.point_contents_count, 0U);

	const struct
	{
		int inventory_slot;
		int contents;
		const char *item;
	} ownership_cases[] = {
		{RETAIL_INVENTORY_SILENCER, 0, "Silencer"},
		{RETAIL_INVENTORY_REBREATHER, 0x08, "Rebreather"},
		{RETAIL_INVENTORY_POWERSHIELD, 0, "Power Shield"},
		{RETAIL_INVENTORY_POWERSCREEN, 0, "Power Screen"},
	};
	const int ownership_values[] = {-1, 0, 1};
	for (size_t item_index = 0;
		item_index < ARRAY_LEN(ownership_cases);
		++item_index)
	{
		for (size_t value_index = 0;
			value_index < ARRAY_LEN(ownership_values);
			++value_index)
		{
			memset(inventory, 0, sizeof(bot.last_client_update.inventory));
			inventory[ownership_cases[item_index].inventory_slot] =
				ownership_values[value_index];
			Mock_Reset(&context->mock);
			context->mock.point_contents_result =
				ownership_cases[item_index].contents;
			BotAI_UseItems(&bot);
			assert_int_equal(context->mock.point_contents_count, 1U);
			if (ownership_values[value_index] > 0)
			{
				assert_int_equal(context->mock.client_command_count, 1U);
				assert_int_equal(context->mock.client_commands[0].client, 3);
				assert_string_equal(context->mock.client_commands[0].command,
					"use");
				assert_string_equal(context->mock.client_commands[0].argument,
					ownership_cases[item_index].item);
			}
			else
			{
				assert_int_equal(context->mock.client_command_count, 0U);
			}
		}
	}

	const struct
	{
		int inventory_slot;
		int active_slot;
		int contents;
		const char *item;
	} active_cases[] = {
		{RETAIL_INVENTORY_REBREATHER,
			RETAIL_USING_REBREATHER,
			0x08,
			"Rebreather"},
		{RETAIL_INVENTORY_POWERSHIELD,
			RETAIL_USING_POWERSHIELD,
			0,
			"Power Shield"},
		{RETAIL_INVENTORY_POWERSCREEN,
			RETAIL_USING_POWERSCREEN,
			0,
			"Power Screen"},
	};
	const int active_values[] = {-1, 0, 1};
	for (size_t item_index = 0; item_index < ARRAY_LEN(active_cases); ++item_index)
	{
		for (size_t value_index = 0;
			value_index < ARRAY_LEN(active_values);
			++value_index)
		{
			memset(inventory, 0, sizeof(bot.last_client_update.inventory));
			inventory[active_cases[item_index].inventory_slot] = 1;
			inventory[active_cases[item_index].active_slot] =
				active_values[value_index];
			Mock_Reset(&context->mock);
			context->mock.point_contents_result =
				active_cases[item_index].contents;
			BotAI_UseItems(&bot);
			assert_int_equal(context->mock.client_command_count,
				active_values[value_index] == 0 ? 1U : 0U);
			if (active_values[value_index] == 0)
			{
				assert_string_equal(context->mock.client_commands[0].argument,
					active_cases[item_index].item);
			}
		}
	}

	const int contents_cases[] = {0, 0x40, 0x08, 0x10, 0x20, 0x38};
	for (size_t index = 0; index < ARRAY_LEN(contents_cases); ++index)
	{
		memset(inventory, 0, sizeof(bot.last_client_update.inventory));
		inventory[RETAIL_INVENTORY_REBREATHER] = 1;
		Mock_Reset(&context->mock);
		context->mock.point_contents_result = contents_cases[index];
		BotAI_UseItems(&bot);
		assert_int_equal(context->mock.point_contents_count, 1U);
		assert_int_equal(context->mock.client_command_count,
			(contents_cases[index] & 0x38) != 0 ? 1U : 0U);
	}

	EA_Shutdown();
	assert_false(EA_IsInitialised());
}

/*
=============
test_battle_item_use_retail_order_and_boundaries

Pins sub_100215e0's Quad-first return, Invulnerability fallback, strict raw
ownership/active gates, client forwarding, stale slots, and state immutability.
=============
*/
static void test_battle_item_use_retail_order_and_boundaries(void **state)
{
	bot_interface_test_context_t *context =
		(bot_interface_test_context_t *)*state;
	Mock_Reset(&context->mock);
	assert_false(EA_IsInitialised());
	assert_int_equal(EA_Init(8), BLERR_NOERROR);

	bot_client_state_t bot;
	memset(&bot, 0, sizeof(bot));
	bot.client_number = 4;
	int *inventory = bot.last_client_update.inventory;
	inventory[RETAIL_INVENTORY_QUAD] = 1;
	inventory[RETAIL_INVENTORY_INVULNERABILITY] = 1;
	inventory[RETAIL_INVENTORY_SILENCER] = 725;
	inventory[RETAIL_INVENTORY_REBREATHER] = 726;
	inventory[RETAIL_USING_REBREATHER] = -207;
	inventory[RETAIL_USING_POWERSCREEN] = -210;
	inventory[RETAIL_USING_POWERSHIELD] = -211;
	context->mock.point_contents_result = 0x38;

	bot_client_state_t snapshot = bot;
	BotAI_BattleUseItems(&bot);
	assert_memory_equal(&bot, &snapshot, sizeof(bot));
	assert_int_equal(context->mock.client_command_count, 1U);
	assert_int_equal(context->mock.client_commands[0].client, 4);
	assert_string_equal(context->mock.client_commands[0].command, "use");
	assert_string_equal(context->mock.client_commands[0].argument,
		"Quad Damage");
	assert_int_equal(context->mock.point_contents_count, 0U);

	Mock_Reset(&context->mock);
	BotAI_BattleUseItems(NULL);
	assert_int_equal(context->mock.client_command_count, 0U);
	assert_int_equal(context->mock.point_contents_count, 0U);

	const struct
	{
		int owned_quad;
		int active_quad;
	} quad_fallback_cases[] = {
		{-1, 0},
		{0, 0},
		{1, -1},
		{1, 1},
	};
	for (size_t index = 0; index < ARRAY_LEN(quad_fallback_cases); ++index)
	{
		memset(inventory, 0, sizeof(bot.last_client_update.inventory));
		inventory[RETAIL_INVENTORY_QUAD] =
			quad_fallback_cases[index].owned_quad;
		inventory[RETAIL_USING_QUAD] =
			quad_fallback_cases[index].active_quad;
		inventory[RETAIL_INVENTORY_INVULNERABILITY] = 1;
		Mock_Reset(&context->mock);
		BotAI_BattleUseItems(&bot);
		assert_int_equal(context->mock.client_command_count, 1U);
		assert_int_equal(context->mock.client_commands[0].client, 4);
		assert_string_equal(context->mock.client_commands[0].argument,
			"Invulnerability");
	}

	const struct
	{
		int owned_invulnerability;
		int active_invulnerability;
		int expected_uses;
	} invulnerability_cases[] = {
		{-1, 0, 0},
		{0, 0, 0},
		{1, -1, 0},
		{1, 1, 0},
		{1, 0, 1},
	};
	for (size_t index = 0;
		index < ARRAY_LEN(invulnerability_cases);
		++index)
	{
		memset(inventory, 0, sizeof(bot.last_client_update.inventory));
		inventory[RETAIL_INVENTORY_INVULNERABILITY] =
			invulnerability_cases[index].owned_invulnerability;
		inventory[RETAIL_USING_INVULNERABILITY] =
			invulnerability_cases[index].active_invulnerability;
		Mock_Reset(&context->mock);
		BotAI_BattleUseItems(&bot);
		assert_int_equal(context->mock.client_command_count,
			(size_t)invulnerability_cases[index].expected_uses);
		if (invulnerability_cases[index].expected_uses != 0)
		{
			assert_int_equal(context->mock.client_commands[0].client, 4);
			assert_string_equal(context->mock.client_commands[0].argument,
				"Invulnerability");
		}
		assert_int_equal(context->mock.point_contents_count, 0U);
	}

	EA_Shutdown();
	assert_false(EA_IsInitialised());
}

/*
=============
test_battle_flag_retreat_and_chase_decisions

Pins sub_10021650's CTF and two-flag return contract plus sub_100228c0 and
sub_10022930's deliberately asymmetric retreat/chase decisions.
=============
*/
static void test_battle_flag_retreat_and_chase_decisions(void **state)
{
	(void)state;
	bot_client_state_t bot;
	memset(&bot, 0, sizeof(bot));
	int *inventory = bot.last_client_update.inventory;

	LibVarSet("ctf", "1");
	libvar_t *ctf = LibVarGet("ctf");
	assert_non_null(ctf);

	inventory[RETAIL_INVENTORY_FLAG1] = 1;
	inventory[RETAIL_INVENTORY_FLAG2] = 1;
	ctf->value = 0.0f;
	assert_int_equal(BotAI_CarryingFlag(&bot), 0);
	ctf->value = NAN;
	assert_int_equal(BotAI_CarryingFlag(&bot), 0);
	ctf->value = -1.0f;
	assert_int_equal(BotAI_CarryingFlag(&bot), 1);

	ctf->value = 1.0f;
	assert_int_equal(BotAI_CarryingFlag(&bot), 1);
	inventory[RETAIL_INVENTORY_FLAG1] = 0;
	assert_int_equal(BotAI_CarryingFlag(&bot), 2);
	inventory[RETAIL_INVENTORY_FLAG1] = -1;
	inventory[RETAIL_INVENTORY_FLAG2] = 0;
	assert_int_equal(BotAI_CarryingFlag(&bot), 0);
	inventory[RETAIL_INVENTORY_FLAG2] = 1;
	assert_int_equal(BotAI_CarryingFlag(&bot), 2);

	inventory[RETAIL_INVENTORY_HEALTH] = 70;
	inventory[RETAIL_ENEMY_HEIGHT] = 200;
	inventory[RETAIL_INVENTORY_SUPERSHOTGUN] = 1;
	inventory[RETAIL_INVENTORY_SHELLS] = 21;
	ctf->value = 0.0f;
	assert_int_equal(BotAI_WantsToRetreat(&bot), qfalse);
	assert_int_equal(BotAI_WantsToChase(&bot), qtrue);

	ctf->value = 1.0f;
	assert_int_equal(BotAI_WantsToRetreat(&bot), qtrue);
	assert_int_equal(BotAI_WantsToChase(&bot), qtrue);
	inventory[RETAIL_INVENTORY_FLAG2] = 0;
	bot.ltg_type = 4;
	assert_int_equal(BotAI_WantsToRetreat(&bot), qtrue);
	assert_int_equal(BotAI_WantsToChase(&bot), qtrue);

	bot.ltg_type = 0;
	inventory[RETAIL_INVENTORY_SUPERSHOTGUN] = 0;
	assert_int_equal(BotAI_WantsToRetreat(&bot), qtrue);
	assert_int_equal(BotAI_WantsToChase(&bot), qfalse);

	bot_client_state_t snapshot = bot;
	(void)BotAI_CarryingFlag(&bot);
	(void)BotAI_Aggression(&bot);
	(void)BotAI_WantsToRetreat(&bot);
	(void)BotAI_WantsToChase(&bot);
	assert_memory_equal(&bot, &snapshot, sizeof(bot));
}

/*
=============
test_battle_aggression_retail_gate_boundaries

Pins sub_100226c0's gate ordering and every strict/non-strict health, armor,
powerup, weapon, and ammunition threshold at the raw retail slots.
=============
*/
static void test_battle_aggression_retail_gate_boundaries(void **state)
{
	(void)state;
	bot_client_state_t bot;
	memset(&bot, 0, sizeof(bot));
	int *inventory = bot.last_client_update.inventory;
	inventory[RETAIL_INVENTORY_HEALTH] = 70;
	inventory[RETAIL_ENEMY_HEIGHT] = 200;
	inventory[RETAIL_INVENTORY_SUPERSHOTGUN] = 1;
	inventory[RETAIL_INVENTORY_SHELLS] = 21;
	inventory[RETAIL_ENEMY_POWERSHIELD] = 748;
	assert_float_equal(BotAI_Aggression(&bot), 100.0f, 0.0001f);

	inventory[RETAIL_USING_INVULNERABILITY] = -1;
	inventory[RETAIL_ENEMY_INVULNERABILITY] = 1;
	inventory[RETAIL_ENEMY_QUAD] = 1;
	inventory[RETAIL_ENEMY_POWERSCREEN] = 1;
	inventory[RETAIL_ENEMY_HEIGHT] = 201;
	inventory[RETAIL_INVENTORY_HEALTH] = 0;
	assert_float_equal(BotAI_Aggression(&bot), 100.0f, 0.0001f);

	inventory[RETAIL_USING_INVULNERABILITY] = 0;
	inventory[RETAIL_ENEMY_HEIGHT] = 200;
	inventory[RETAIL_INVENTORY_HEALTH] = 70;
	assert_float_equal(BotAI_Aggression(&bot), 0.0f, 0.0001f);
	inventory[RETAIL_ENEMY_INVULNERABILITY] = 0;
	assert_float_equal(BotAI_Aggression(&bot), 0.0f, 0.0001f);
	inventory[RETAIL_USING_QUAD] = -1;
	inventory[RETAIL_ENEMY_POWERSCREEN] = 0;
	assert_float_equal(BotAI_Aggression(&bot), 100.0f, 0.0001f);
	inventory[RETAIL_ENEMY_POWERSCREEN] = 1;
	assert_float_equal(BotAI_Aggression(&bot), 0.0f, 0.0001f);

	inventory[RETAIL_ENEMY_QUAD] = 0;
	inventory[RETAIL_USING_POWERSHIELD] = 100;
	inventory[RETAIL_INVENTORY_CELLS] = 50;
	assert_float_equal(BotAI_Aggression(&bot), 0.0f, 0.0001f);
	inventory[RETAIL_USING_POWERSCREEN] = -1;
	inventory[RETAIL_INVENTORY_CELLS] = 49;
	assert_float_equal(BotAI_Aggression(&bot), 0.0f, 0.0001f);
	inventory[RETAIL_INVENTORY_CELLS] = 50;
	assert_float_equal(BotAI_Aggression(&bot), 100.0f, 0.0001f);

	inventory[RETAIL_ENEMY_POWERSCREEN] = 0;
	inventory[RETAIL_USING_POWERSCREEN] = 0;
	inventory[RETAIL_ENEMY_HEIGHT] = 201;
	assert_float_equal(BotAI_Aggression(&bot), 0.0f, 0.0001f);
	inventory[RETAIL_ENEMY_HEIGHT] = 200;
	assert_float_equal(BotAI_Aggression(&bot), 100.0f, 0.0001f);

	inventory[RETAIL_INVENTORY_HEALTH] = 39;
	inventory[RETAIL_INVENTORY_ARMORBODY] = 100;
	inventory[RETAIL_INVENTORY_ARMORCOMBAT] = 100;
	inventory[RETAIL_INVENTORY_ARMORJACKET] = 100;
	assert_float_equal(BotAI_Aggression(&bot), 0.0f, 0.0001f);
	inventory[RETAIL_INVENTORY_HEALTH] = 40;
	inventory[RETAIL_INVENTORY_ARMORBODY] = 0;
	inventory[RETAIL_INVENTORY_ARMORCOMBAT] = 0;
	inventory[RETAIL_INVENTORY_ARMORJACKET] = 0;
	assert_float_equal(BotAI_Aggression(&bot), 0.0f, 0.0001f);

	const struct
	{
		int slot;
		int threshold;
	} armor_cases[] = {
		{RETAIL_INVENTORY_ARMORBODY, 40},
		{RETAIL_INVENTORY_ARMORCOMBAT, 50},
		{RETAIL_INVENTORY_ARMORJACKET, 60},
	};
	for (size_t index = 0; index < ARRAY_LEN(armor_cases); ++index)
	{
		inventory[RETAIL_INVENTORY_HEALTH] = 69;
		inventory[RETAIL_INVENTORY_ARMORBODY] = 0;
		inventory[RETAIL_INVENTORY_ARMORCOMBAT] = 0;
		inventory[RETAIL_INVENTORY_ARMORJACKET] = 0;
		inventory[armor_cases[index].slot] = armor_cases[index].threshold - 1;
		assert_float_equal(BotAI_Aggression(&bot), 0.0f, 0.0001f);
		inventory[armor_cases[index].slot] = armor_cases[index].threshold;
		assert_float_equal(BotAI_Aggression(&bot), 100.0f, 0.0001f);
	}
	inventory[RETAIL_INVENTORY_HEALTH] = 70;
	inventory[RETAIL_INVENTORY_ARMORBODY] = 0;
	inventory[RETAIL_INVENTORY_ARMORCOMBAT] = 0;
	inventory[RETAIL_INVENTORY_ARMORJACKET] = 0;
	assert_float_equal(BotAI_Aggression(&bot), 100.0f, 0.0001f);

	const struct
	{
		int weapon_slot;
		int ammunition_slot;
		int threshold;
	} weapon_cases[] = {
		{RETAIL_INVENTORY_BFG10K, RETAIL_INVENTORY_CELLS, 50},
		{RETAIL_INVENTORY_RAILGUN, RETAIL_INVENTORY_SLUGS, 5},
		{RETAIL_INVENTORY_HYPERBLASTER, RETAIL_INVENTORY_CELLS, 50},
		{RETAIL_INVENTORY_ROCKETLAUNCHER, RETAIL_INVENTORY_ROCKETS, 5},
		{RETAIL_INVENTORY_GRENADELAUNCHER, RETAIL_INVENTORY_GRENADES, 10},
		{RETAIL_INVENTORY_CHAINGUN, RETAIL_INVENTORY_BULLETS, 100},
		{RETAIL_INVENTORY_MACHINEGUN, RETAIL_INVENTORY_BULLETS, 75},
		{RETAIL_INVENTORY_SUPERSHOTGUN, RETAIL_INVENTORY_SHELLS, 20},
	};
	for (size_t index = 0; index < ARRAY_LEN(weapon_cases); ++index)
	{
		memset(inventory, 0, sizeof(bot.last_client_update.inventory));
		inventory[RETAIL_INVENTORY_HEALTH] = 70;
		inventory[RETAIL_ENEMY_HEIGHT] = 200;
		inventory[weapon_cases[index].weapon_slot] = 1;
		inventory[weapon_cases[index].ammunition_slot] =
			weapon_cases[index].threshold;
		assert_float_equal(BotAI_Aggression(&bot), 0.0f, 0.0001f);
		inventory[weapon_cases[index].ammunition_slot]++;
		assert_float_equal(BotAI_Aggression(&bot), 100.0f, 0.0001f);
		inventory[weapon_cases[index].weapon_slot] = -1;
		assert_float_equal(BotAI_Aggression(&bot), 0.0f, 0.0001f);
	}

	bot_client_state_t snapshot = bot;
	(void)BotAI_Aggression(&bot);
	assert_memory_equal(&bot, &snapshot, sizeof(bot));
}

/*
=============
test_battle_rocket_jump_retail_gate_boundaries

Pins sub_10022990's ordered raw inventory gates, invulnerability bypass,
health/armor thresholds, and inclusive weapon-jumping characteristic boundary.
=============
*/
static void test_battle_rocket_jump_retail_gate_boundaries(void **state)
{
	bot_interface_test_context_t *context =
		(bot_interface_test_context_t *)*state;
	Mock_Reset(&context->mock);
	assert_int_equal(context->api->BotLibVarSet("maxclients", "4"),
		BLERR_NOERROR);
	assert_int_equal(context->api->BotSetupLibrary(), BLERR_NOERROR);

	const struct
	{
		const char *file;
		const char *name;
	} profiles[] = {
		{"bots/java_c.c", "java"},
		{"bots/hunk_c.c", "hunk"},
		{"bots/zero_c.c", "zero"},
	};
	bot_client_state_t *profile_states[ARRAY_LEN(profiles)];
	for (size_t index = 0; index < ARRAY_LEN(profiles); ++index)
	{
		bot_settings_t settings;
		memset(&settings, 0, sizeof(settings));
		snprintf(settings.characterfile,
			sizeof(settings.characterfile),
			"%s",
			profiles[index].file);
		snprintf(settings.charactername,
			sizeof(settings.charactername),
			"%s",
			profiles[index].name);
		assert_true(context->api->BotSetupClient((int)index + 1, &settings));
		profile_states[index] = BotState_Get((int)index + 1);
		assert_non_null(profile_states[index]);
	}

	assert_int_equal(BotAI_CanAndWantsToRocketJump(NULL), qfalse);

	bot_client_state_t bot;
	memset(&bot, 0, sizeof(bot));
	int *inventory = bot.last_client_update.inventory;
	bot.character = profile_states[1]->character;
	inventory[RETAIL_INVENTORY_ROCKETLAUNCHER] = 1;
	inventory[RETAIL_INVENTORY_ROCKETS] = 3;
	inventory[RETAIL_INVENTORY_HEALTH] = 90;
	inventory[RETAIL_USING_POWERSHIELD] = 712;
	inventory[RETAIL_ENEMY_INVULNERABILITY] = 746;
	LibVarSet("rocketjump", "0");
	assert_int_equal(BotAI_CanAndWantsToRocketJump(&bot), qtrue);

	inventory[RETAIL_INVENTORY_ROCKETLAUNCHER] = 0;
	assert_int_equal(BotAI_CanAndWantsToRocketJump(&bot), qfalse);
	inventory[RETAIL_INVENTORY_ROCKETLAUNCHER] = -1;
	assert_int_equal(BotAI_CanAndWantsToRocketJump(&bot), qfalse);
	inventory[RETAIL_INVENTORY_ROCKETLAUNCHER] = 1;

	inventory[RETAIL_INVENTORY_ROCKETS] = 2;
	assert_int_equal(BotAI_CanAndWantsToRocketJump(&bot), qfalse);
	inventory[RETAIL_INVENTORY_ROCKETS] = 3;
	inventory[RETAIL_USING_QUAD] = -1;
	assert_int_equal(BotAI_CanAndWantsToRocketJump(&bot), qfalse);
	inventory[RETAIL_USING_QUAD] = 1;
	assert_int_equal(BotAI_CanAndWantsToRocketJump(&bot), qfalse);
	inventory[RETAIL_USING_QUAD] = 0;

	inventory[RETAIL_USING_INVULNERABILITY] = -1;
	inventory[RETAIL_INVENTORY_HEALTH] = -100;
	inventory[RETAIL_INVENTORY_ARMORBODY] = -100;
	inventory[RETAIL_INVENTORY_ARMORCOMBAT] = -100;
	inventory[RETAIL_INVENTORY_ARMORJACKET] = -100;
	bot.character = NULL;
	assert_int_equal(BotAI_CanAndWantsToRocketJump(&bot), qtrue);
	inventory[RETAIL_INVENTORY_ROCKETLAUNCHER] = 0;
	assert_int_equal(BotAI_CanAndWantsToRocketJump(&bot), qfalse);
	inventory[RETAIL_INVENTORY_ROCKETLAUNCHER] = 1;
	inventory[RETAIL_INVENTORY_ROCKETS] = 2;
	assert_int_equal(BotAI_CanAndWantsToRocketJump(&bot), qfalse);
	inventory[RETAIL_INVENTORY_ROCKETS] = 3;
	inventory[RETAIL_USING_QUAD] = 1;
	assert_int_equal(BotAI_CanAndWantsToRocketJump(&bot), qfalse);
	inventory[RETAIL_USING_QUAD] = 0;
	inventory[RETAIL_USING_INVULNERABILITY] = 0;
	bot.character = profile_states[1]->character;

	inventory[RETAIL_INVENTORY_HEALTH] = 59;
	inventory[RETAIL_INVENTORY_ARMORBODY] = 100;
	inventory[RETAIL_INVENTORY_ARMORCOMBAT] = 100;
	inventory[RETAIL_INVENTORY_ARMORJACKET] = 100;
	assert_int_equal(BotAI_CanAndWantsToRocketJump(&bot), qfalse);
	inventory[RETAIL_INVENTORY_HEALTH] = 60;
	inventory[RETAIL_INVENTORY_ARMORBODY] = 0;
	inventory[RETAIL_INVENTORY_ARMORCOMBAT] = 0;
	inventory[RETAIL_INVENTORY_ARMORJACKET] = 0;
	assert_int_equal(BotAI_CanAndWantsToRocketJump(&bot), qfalse);
	inventory[RETAIL_INVENTORY_ARMORBODY] = 40;
	assert_int_equal(BotAI_CanAndWantsToRocketJump(&bot), qtrue);
	inventory[RETAIL_INVENTORY_ARMORBODY] = 0;
	inventory[RETAIL_INVENTORY_HEALTH] = 89;
	assert_int_equal(BotAI_CanAndWantsToRocketJump(&bot), qfalse);

	const struct
	{
		int slot;
		int threshold;
	} armor_cases[] = {
		{RETAIL_INVENTORY_ARMORBODY, 40},
		{RETAIL_INVENTORY_ARMORCOMBAT, 50},
		{RETAIL_INVENTORY_ARMORJACKET, 60},
	};
	for (size_t index = 0; index < ARRAY_LEN(armor_cases); ++index)
	{
		inventory[RETAIL_INVENTORY_ARMORBODY] = 0;
		inventory[RETAIL_INVENTORY_ARMORCOMBAT] = 0;
		inventory[RETAIL_INVENTORY_ARMORJACKET] = 0;
		inventory[armor_cases[index].slot] = armor_cases[index].threshold - 1;
		assert_int_equal(BotAI_CanAndWantsToRocketJump(&bot), qfalse);
		inventory[armor_cases[index].slot] = armor_cases[index].threshold;
		assert_int_equal(BotAI_CanAndWantsToRocketJump(&bot), qtrue);
	}

	inventory[RETAIL_INVENTORY_HEALTH] = 90;
	inventory[RETAIL_INVENTORY_ARMORBODY] = 0;
	inventory[RETAIL_INVENTORY_ARMORCOMBAT] = 0;
	inventory[RETAIL_INVENTORY_ARMORJACKET] = 0;
	bot.character = profile_states[0]->character;
	assert_int_equal(BotAI_CanAndWantsToRocketJump(&bot), qfalse);
	bot.character = profile_states[1]->character;
	assert_int_equal(BotAI_CanAndWantsToRocketJump(&bot), qtrue);
	bot.character = profile_states[2]->character;
	assert_int_equal(BotAI_CanAndWantsToRocketJump(&bot), qtrue);

	bot_client_state_t snapshot = bot;
	(void)BotAI_CanAndWantsToRocketJump(&bot);
	assert_memory_equal(&bot, &snapshot, sizeof(bot));
	assert_int_equal(inventory[RETAIL_USING_POWERSHIELD], 712);
	assert_int_equal(inventory[RETAIL_ENEMY_INVULNERABILITY], 746);

	for (int client = (int)ARRAY_LEN(profiles); client >= 1; --client)
	{
		assert_int_equal(context->api->BotShutdownClient(client), BLERR_NOERROR);
	}
	assert_int_equal(context->api->BotShutdownLibrary(), BLERR_NOERROR);
}

/*
=============
BotFindEnemy_PrepareEntity

Seeds one host-native AAS client entity without loading a BSP/AAS fixture.
=============
*/
static aas_entity_t *BotFindEnemy_PrepareEntity(int entity_number,
	float x,
	float y,
	float z)
{
	assert_non_null(aasworld.entities);
	assert_true(entity_number >= 0);
	assert_true(entity_number < aasworld.maxEntities);

	aas_entity_t *entity = &aasworld.entities[entity_number];
	entity->inuse = qtrue;
	entity->number = entity_number;
	VectorSet(entity->origin, x, y, z);
	VectorCopy(entity->origin, entity->old_origin);
	VectorCopy(entity->origin, entity->previousOrigin);
	VectorClear(entity->angles);
	VectorClear(entity->mins);
	VectorClear(entity->maxs);
	entity->solid = 0;
	entity->isMover = qfalse;
	entity->modelindex = 255;
	entity->modelindex2 = 0;
	entity->modelindex3 = 0;
	entity->modelindex4 = 0;
	entity->frame = 0;
	entity->skinnum = 0;
	entity->effects = 0;
	entity->renderfx = 0;
	entity->sound = 0;
	entity->eventid = 0;
	return entity;
}

/*
=============
BotFindEnemy_SetupHarness

Initialises a character-backed client and the fixture-free AAS entity table.
=============
*/
static bot_client_state_t *BotFindEnemy_SetupHarness(
	bot_interface_test_context_t *context,
	int max_clients,
	const char *character_file,
	const char *character_name)
{
	assert_non_null(context);
	Mock_Reset(&context->mock);

	char value[32];
	snprintf(value, sizeof(value), "%d", max_clients);
	assert_int_equal(context->api->BotLibVarSet("maxclients", value),
		BLERR_NOERROR);
	assert_int_equal(context->api->BotLibVarSet("maxentities", "64"),
		BLERR_NOERROR);
	assert_int_equal(context->api->BotSetupLibrary(), BLERR_NOERROR);
	assert_true(aasworld.initialized);
	assert_int_equal(aasworld.maxClients, max_clients);
	assert_non_null(aasworld.entities);

	bot_settings_t settings;
	memset(&settings, 0, sizeof(settings));
	snprintf(settings.characterfile,
		sizeof(settings.characterfile),
		"%s",
		character_file);
	snprintf(settings.charactername,
		sizeof(settings.charactername),
		"%s",
		character_name);
	assert_true(context->api->BotSetupClient(0, &settings));

	bot_client_state_t *bot = BotState_Get(0);
	assert_non_null(bot);
	assert_non_null(bot->dm_state);
	VectorClear(bot->last_client_update.origin);
	VectorClear(bot->last_client_update.viewoffset);
	VectorClear(bot->last_client_update.viewangles);
	bot->last_client_update.inventory[RETAIL_INVENTORY_HEALTH] = 100;
	bot->combat.last_known_health = 100;
	bot->ltg_type = 0;
	AI_DMState_Reset(bot->dm_state);

	LibVarSet("teamplay_shell", "0");
	LibVarSet("ch", "0");
	LibVarSet("teamplay", "0");
	LibVarSet("dmflags", "0");
	LibVarSet("ctf", "0");
	BotFindEnemy_PrepareEntity(1, 0.0f, 0.0f, 0.0f);
	return bot;
}

/*
=============
BotFindEnemy_ResetCallState

Restores the raw health comparison and observable enemy-selection sentinels.
=============
*/
static void BotFindEnemy_ResetCallState(bot_client_state_t *bot, int health)
{
	assert_non_null(bot);
	bot->last_client_update.inventory[RETAIL_INVENTORY_HEALTH] = health;
	bot->combat.last_known_health = health;
	bot->combat.current_enemy = -77;
	bot->combat.enemy_sight_time = -88.0f;
}

/*
=============
BotFindEnemy_SetSkin

Stores the presentation skin used by retail's entity-to-client team mapping.
=============
*/
static void BotFindEnemy_SetSkin(bot_interface_test_context_t *context,
	int client,
	const char *skin)
{
	bot_clientsettings_t settings;
	memset(&settings, 0, sizeof(settings));
	snprintf(settings.skin, sizeof(settings.skin), "%s", skin);
	assert_int_equal(context->api->BotClientSettings(client, &settings),
		BLERR_NOERROR);
}

/*
=============
test_find_enemy_live_predicate_boundaries

Pins sub_10021710's effects, client-number, model, and death-frame gates.
=============
*/
static void test_find_enemy_live_predicate_boundaries(void **state)
{
	bot_interface_test_context_t *context =
		(bot_interface_test_context_t *)*state;
	bot_client_state_t *bot = BotFindEnemy_SetupHarness(context,
		4,
		"bots/babe_c.c",
		"babe");

	const struct
	{
		int number;
		int modelindex;
		int frame;
		int effects;
		bool live;
	} cases[] = {
		{2, 255, 0, 0, true},
		{2, 255, 0, 1, true},
		{2, 255, 0, EF_GIB, false},
		{2, 255, 0, EF_FLIES, false},
		{0, 255, 0, 0, false},
		{5, 255, 0, 0, false},
		{1, 255, 0, 0, false},
		{2, 254, 0, 0, false},
		{2, 255, 172, 0, true},
		{2, 255, 173, 0, false},
		{2, 255, 197, 0, false},
		{2, 255, 198, 0, true},
	};

	for (size_t index = 0; index < ARRAY_LEN(cases); ++index)
	{
		aas_entity_t *candidate = BotFindEnemy_PrepareEntity(2,
			100.0f,
			0.0f,
			0.0f);
		candidate->number = cases[index].number;
		candidate->modelindex = cases[index].modelindex;
		candidate->frame = cases[index].frame;
		candidate->effects = cases[index].effects;
		BotFindEnemy_ResetCallState(bot, 100);

		ai_dm_enemy_info_t enemy;
		memset(&enemy, 0xa5, sizeof(enemy));
		int found = BotAI_FindEnemy(bot, &enemy);
		assert_int_equal(found, cases[index].live ? qtrue : qfalse);
		assert_int_equal(enemy.valid, cases[index].live);
		if (cases[index].live)
		{
			assert_int_equal(enemy.entity, 2);
			assert_int_equal(bot->combat.current_enemy, 2);
		}
		else
		{
			assert_int_equal(enemy.entity, -1);
			assert_int_equal(bot->combat.current_enemy, -77);
			assert_float_equal(bot->combat.enemy_sight_time,
				-88.0f,
				0.0001f);
		}
		assert_int_equal(bot->combat.last_known_health, 100);
	}
}

/*
=============
test_find_enemy_numeric_first_and_visible_cap

Proves ascending entity order wins over distance and the first 16 visible
records consume the retail result cap even when they are later rejected.
=============
*/
static void test_find_enemy_numeric_first_and_visible_cap(void **state)
{
	bot_interface_test_context_t *context =
		(bot_interface_test_context_t *)*state;
	bot_client_state_t *bot = BotFindEnemy_SetupHarness(context,
		17,
		"bots/babe_c.c",
		"babe");

	aas_entity_t *first = BotFindEnemy_PrepareEntity(2,
		800.0f,
		0.0f,
		0.0f);
	first->frame = 46;
	BotFindEnemy_PrepareEntity(3, 100.0f, 0.0f, 0.0f);
	BotFindEnemy_ResetCallState(bot, 100);
	ai_dm_enemy_info_t enemy;
	assert_int_equal(BotAI_FindEnemy(bot, &enemy), qtrue);
	assert_int_equal(enemy.entity, 2);
	assert_float_equal(enemy.distance, 800.0f, 0.0001f);

	for (int entity_number = 2; entity_number <= 16; ++entity_number)
	{
		aas_entity_t *rejected = BotFindEnemy_PrepareEntity(entity_number,
			100.0f + (float)entity_number,
			0.0f,
			0.0f);
		rejected->modelindex = 1;
	}
	BotFindEnemy_PrepareEntity(17, 50.0f, 0.0f, 0.0f);
	BotFindEnemy_ResetCallState(bot, 100);
	assert_int_equal(BotAI_FindEnemy(bot, &enemy), qfalse);
	assert_false(enemy.valid);
	assert_int_equal(enemy.entity, -1);
	assert_int_equal(bot->combat.current_enemy, -77);
}

/*
=============
test_find_enemy_nonaccelerated_range_fov_and_close_boundaries

Pins the no-3D 900-unit cap, 810-unit FOV saturation, and inclusive 300-unit
close acceptance before the retreat fallback.
=============
*/
static void test_find_enemy_nonaccelerated_range_fov_and_close_boundaries(
	void **state)
{
	bot_interface_test_context_t *context =
		(bot_interface_test_context_t *)*state;
	bot_client_state_t *bot = BotFindEnemy_SetupHarness(context,
		4,
		"bots/hunk_c.c",
		"hunk");
	ai_dm_enemy_info_t enemy;

	aas_entity_t *candidate = BotFindEnemy_PrepareEntity(2,
		900.0f,
		0.0f,
		0.0f);
	candidate->frame = 46;
	BotFindEnemy_ResetCallState(bot, 100);
	assert_int_equal(BotAI_FindEnemy(bot, &enemy), qtrue);
	assert_float_equal(enemy.distance, 900.0f, 0.0001f);
	assert_float_equal(enemy.field_of_view, 360.0f, 0.0001f);

	candidate = BotFindEnemy_PrepareEntity(2, 900.01f, 0.0f, 0.0f);
	candidate->frame = 46;
	BotFindEnemy_ResetCallState(bot, 100);
	assert_int_equal(BotAI_FindEnemy(bot, &enemy), qfalse);
	assert_int_equal(bot->combat.current_enemy, -77);

	candidate = BotFindEnemy_PrepareEntity(2, 809.0f, 0.0f, 0.0f);
	candidate->frame = 46;
	BotFindEnemy_ResetCallState(bot, 100);
	assert_int_equal(BotAI_FindEnemy(bot, &enemy), qtrue);
	assert_float_equal(enemy.field_of_view,
		90.0f + 809.0f / 3.0f,
		0.0001f);

	candidate = BotFindEnemy_PrepareEntity(2, 810.0f, 0.0f, 0.0f);
	candidate->frame = 46;
	BotFindEnemy_ResetCallState(bot, 100);
	assert_int_equal(BotAI_FindEnemy(bot, &enemy), qtrue);
	assert_float_equal(enemy.field_of_view, 360.0f, 0.0001f);

	memset(bot->last_client_update.inventory,
		0,
		sizeof(bot->last_client_update.inventory));
	candidate = BotFindEnemy_PrepareEntity(2, 300.0f, 0.0f, 0.0f);
	BotFindEnemy_ResetCallState(bot, 70);
	assert_int_equal(BotAI_FindEnemy(bot, &enemy), qtrue);
	assert_float_equal(enemy.distance, 300.0f, 0.0001f);

	candidate = BotFindEnemy_PrepareEntity(2, 300.01f, 0.0f, 0.0f);
	BotFindEnemy_ResetCallState(bot, 70);
	assert_int_equal(BotAI_FindEnemy(bot, &enemy), qfalse);
	assert_int_equal(bot->combat.current_enemy, -77);

	VectorSet(bot->last_client_update.viewangles, 0.0f, 180.0f, 0.0f);
	candidate->frame = 46;
	BotFindEnemy_ResetCallState(bot, 70);
	assert_int_equal(BotAI_FindEnemy(bot, &enemy), qtrue);
	vec3_t delta_angles = {0.0f, 180.0f, 0.0f};
	AI_DMState_ApplyDeltaAngles(bot->dm_state, delta_angles);
	VectorClear(bot->last_client_update.viewangles);
	BotFindEnemy_ResetCallState(bot, 70);
	assert_int_equal(BotAI_FindEnemy(bot, &enemy), qfalse);
}

/*
=============
test_find_enemy_accelerated_profile_has_no_900_cap

Confirms characteristic 45 removes the nonaccelerated distance rejection.
=============
*/
static void test_find_enemy_accelerated_profile_has_no_900_cap(void **state)
{
	bot_interface_test_context_t *context =
		(bot_interface_test_context_t *)*state;
	bot_client_state_t *bot = BotFindEnemy_SetupHarness(context,
		4,
		"bots/babe_c.c",
		"babe");
	aas_entity_t *candidate = BotFindEnemy_PrepareEntity(2,
		901.0f,
		0.0f,
		0.0f);
	candidate->frame = 46;
	BotFindEnemy_ResetCallState(bot, 100);
	ai_dm_enemy_info_t enemy;
	assert_int_equal(BotAI_FindEnemy(bot, &enemy), qtrue);
	assert_int_equal(enemy.entity, 2);
	assert_float_equal(enemy.distance, 901.0f, 0.0001f);
	assert_float_equal(enemy.field_of_view, 360.0f, 0.0001f);
}

/*
=============
test_find_enemy_team_precedence_and_entity_client_mapping

Pins sub_10023550's shell, CH, teamplay, skin, CTF, and model precedence.
=============
*/
static void test_find_enemy_team_precedence_and_entity_client_mapping(
	void **state)
{
	bot_interface_test_context_t *context =
		(bot_interface_test_context_t *)*state;
	bot_client_state_t *bot = BotFindEnemy_SetupHarness(context,
		4,
		"bots/babe_c.c",
		"babe");
	aas_entity_t *self = &aasworld.entities[1];
	aas_entity_t *candidate = BotFindEnemy_PrepareEntity(2,
		100.0f,
		0.0f,
		0.0f);
	BotFindEnemy_SetSkin(context, 0, "male/grunt");
	BotFindEnemy_SetSkin(context, 1, "female/athena");

	candidate->number = 0;
	LibVarSet("teamplay_shell", "1");
	assert_int_equal(BotAI_SameTeam(bot, 2), qfalse);
	candidate->number = 2;
	self->renderfx = 0x0401;
	candidate->renderfx = 0x0402;
	LibVarSet("ch", "1");
	LibVarSet("teamplay", "1");
	assert_int_equal(BotAI_SameTeam(bot, 2), qtrue);
	candidate->renderfx = 0x0802;
	assert_int_equal(BotAI_SameTeam(bot, 2), qfalse);

	LibVarSet("teamplay_shell", "0");
	self->modelindex3 = 7;
	candidate->modelindex3 = 8;
	assert_int_equal(BotAI_SameTeam(bot, 2), qtrue);
	candidate->modelindex3 = 7;
	assert_int_equal(BotAI_SameTeam(bot, 2), qfalse);

	LibVarSet("ch", "0");
	BotFindEnemy_SetSkin(context, 1, "MALE/GRUNT");
	assert_int_equal(BotAI_SameTeam(bot, 2), qtrue);
	BotFindEnemy_SetSkin(context, 1, "female/grunt");
	assert_int_equal(BotAI_SameTeam(bot, 2), qfalse);

	LibVarSet("teamplay", "0");
	LibVarSet("dmflags", "64");
	assert_int_equal(BotAI_SameTeam(bot, 2), qtrue);
	BotFindEnemy_SetSkin(context, 1, "female/athena");
	assert_int_equal(BotAI_SameTeam(bot, 2), qfalse);
	LibVarSet("dmflags", "0");
	LibVarSet("ctf", "1");
	BotFindEnemy_SetSkin(context, 1, "female/GRUNT");
	assert_int_equal(BotAI_SameTeam(bot, 2), qtrue);

	LibVarSet("ctf", "0");
	LibVarSet("dmflags", "192");
	BotFindEnemy_SetSkin(context, 1, "male/athena");
	assert_int_equal(BotAI_SameTeam(bot, 2), qfalse);
	BotFindEnemy_SetSkin(context, 1, "female/grunt");
	assert_int_equal(BotAI_SameTeam(bot, 2), qtrue);

	LibVarSet("dmflags", "128");
	BotFindEnemy_SetSkin(context, 1, "male/athena");
	assert_int_equal(BotAI_SameTeam(bot, 2), qtrue);
	BotFindEnemy_SetSkin(context, 1, "female/grunt");
	assert_int_equal(BotAI_SameTeam(bot, 2), qfalse);
	BotFindEnemy_SetSkin(context, 0, "");
	BotFindEnemy_SetSkin(context, 1, "");
	assert_int_equal(BotAI_SameTeam(bot, 2), qtrue);

	LibVarSet("dmflags", "0");
	assert_int_equal(BotAI_SameTeam(bot, 2), qfalse);
	BotFindEnemy_SetSkin(context, 0, "model/target");
	BotFindEnemy_SetSkin(context, 1, "model/wrong");
	BotFindEnemy_SetSkin(context, 2, "other/target");
	aas_entity_t *mapped = BotFindEnemy_PrepareEntity(3,
		100.0f,
		0.0f,
		0.0f);
	mapped->number = 3;
	LibVarSet("dmflags", "64");
	assert_int_equal(BotAI_SameTeam(bot, 3), qtrue);
}

/*
=============
test_find_enemy_shooting_facing_and_retreat_fallback

Pins attack-frame bounds, the candidate-facing 160-degree check, and the
inventory-before-retreat fallback side effect.
=============
*/
static void test_find_enemy_shooting_facing_and_retreat_fallback(void **state)
{
	bot_interface_test_context_t *context =
		(bot_interface_test_context_t *)*state;
	bot_client_state_t *bot = BotFindEnemy_SetupHarness(context,
		4,
		"bots/babe_c.c",
		"babe");
	memset(bot->last_client_update.inventory,
		0,
		sizeof(bot->last_client_update.inventory));
	bot->last_client_update.inventory[RETAIL_INVENTORY_HEALTH] = 70;
	aas_entity_t *candidate = BotFindEnemy_PrepareEntity(2,
		500.0f,
		0.0f,
		0.0f);

	const struct
	{
		int frame;
		bool accepted;
	} shooting_cases[] = {
		{45, false},
		{46, true},
		{53, true},
		{54, false},
	};
	for (size_t index = 0; index < ARRAY_LEN(shooting_cases); ++index)
	{
		candidate->frame = shooting_cases[index].frame;
		VectorClear(candidate->angles);
		BotFindEnemy_ResetCallState(bot, 70);
		ai_dm_enemy_info_t enemy;
		assert_int_equal(BotAI_FindEnemy(bot, &enemy),
			shooting_cases[index].accepted ? qtrue : qfalse);
		if (shooting_cases[index].accepted)
		{
			assert_true(enemy.is_shooting);
		}
		else
		{
			assert_false(enemy.valid);
		}
	}

	candidate->frame = 0;
	VectorSet(candidate->angles, 0.0f, 100.01f, 0.0f);
	BotFindEnemy_ResetCallState(bot, 70);
	ai_dm_enemy_info_t enemy;
	assert_int_equal(BotAI_FindEnemy(bot, &enemy), qtrue);
	VectorSet(candidate->angles, 0.0f, 100.0f, 0.0f);
	BotFindEnemy_ResetCallState(bot, 70);
	assert_int_equal(BotAI_FindEnemy(bot, &enemy), qfalse);

	VectorClear(candidate->angles);
	bot->last_client_update.inventory[RETAIL_ENEMY_HORIZONTAL_DIST] = -1;
	BotFindEnemy_ResetCallState(bot, 70);
	assert_int_equal(BotAI_FindEnemy(bot, &enemy), qfalse);
	assert_int_equal(bot->last_client_update.inventory[
		RETAIL_ENEMY_HORIZONTAL_DIST],
		500);

	bot->last_client_update.inventory[RETAIL_INVENTORY_BFG10K] = 1;
	bot->last_client_update.inventory[RETAIL_INVENTORY_CELLS] = 51;
	BotFindEnemy_ResetCallState(bot, 70);
	assert_int_equal(BotAI_FindEnemy(bot, &enemy), qtrue);
	assert_int_equal(enemy.entity, 2);
	assert_false(enemy.is_shooting);
}

/*
=============
test_find_enemy_health_sight_and_failure_side_effects

Proves raw inventory health comparison, AAS sight time, exact success writes,
and failure preservation of the previous enemy and sight timestamp.
=============
*/
static void test_find_enemy_health_sight_and_failure_side_effects(void **state)
{
	bot_interface_test_context_t *context =
		(bot_interface_test_context_t *)*state;
	bot_client_state_t *bot = BotFindEnemy_SetupHarness(context,
		4,
		"bots/babe_c.c",
		"babe");
	aas_entity_t *candidate = BotFindEnemy_PrepareEntity(2,
		500.0f,
		0.0f,
		0.0f);
	candidate->frame = 46;
	aasworld.time = 42.5f;

	bot->combat.current_enemy = 91;
	bot->combat.enemy_visible = true;
	bot->combat.enemy_visible_time = 11.0f;
	bot->combat.enemy_sight_time = 12.0f;
	bot->combat.enemy_death_time = 13.0f;
	bot->combat.enemy_last_seen_time = 14.0f;
	VectorSet(bot->combat.last_enemy_origin, 1.0f, 2.0f, 3.0f);
	VectorSet(bot->combat.last_enemy_velocity, 4.0f, 5.0f, 6.0f);
	bot->combat.revenge_enemy = 92;
	bot->combat.revenge_kills = 93;
	bot->combat.last_known_health = 0;
	bot->combat.last_damage_amount = 94;
	bot->combat.last_damage_time = 15.0f;
	bot->combat.last_health_valid = false;
	bot->combat.took_damage = true;
	bot->last_client_update.inventory[RETAIL_INVENTORY_HEALTH] = -1;
	bot_combat_state_t expected = bot->combat;
	expected.current_enemy = 2;
	expected.enemy_sight_time = 42.5f;
	expected.last_known_health = -1;

	ai_dm_enemy_info_t enemy;
	assert_int_equal(BotAI_FindEnemy(bot, &enemy), qtrue);
	assert_memory_equal(&bot->combat, &expected, sizeof(expected));
	assert_true(enemy.valid);
	assert_true(enemy.triggered_by_damage);
	assert_float_equal(enemy.field_of_view, 360.0f, 0.0001f);
	assert_float_equal(enemy.last_seen_time, 42.5f, 0.0001f);

	candidate->frame = 173;
	bot->last_client_update.inventory[RETAIL_INVENTORY_HEALTH] = 10;
	expected = bot->combat;
	expected.last_known_health = 10;
	memset(&enemy, 0xa5, sizeof(enemy));
	assert_int_equal(BotAI_FindEnemy(bot, &enemy), qfalse);
	assert_memory_equal(&bot->combat, &expected, sizeof(expected));
	assert_false(enemy.valid);
	assert_int_equal(enemy.entity, -1);
}

typedef struct bot_ai_node_loop_probe_s
{
	int calls;
	int complete_call;
} bot_ai_node_loop_probe_t;

/*
=============
BotAINode_LoopProbeStep

Counts scheduler dispatches and optionally completes on an exact call.
=============
*/
static int BotAINode_LoopProbeStep(bot_client_state_t *bot, void *context)
{
	bot_ai_node_loop_probe_t *probe =
		(bot_ai_node_loop_probe_t *)context;
	assert_non_null(bot);
	assert_non_null(probe);

	probe->calls++;
	return probe->complete_call > 0 &&
		probe->calls >= probe->complete_call;
}

/*
=============
BotAINode_ClearEnemyEntities

Removes fixture-free enemy candidates while preserving AAS allocations.
=============
*/
static void BotAINode_ClearEnemyEntities(void)
{
	assert_non_null(aasworld.entities);
	for (int entity_number = 2;
		entity_number <= aasworld.maxClients;
		++entity_number)
	{
		aasworld.entities[entity_number].inuse = qfalse;
		aasworld.entities[entity_number].number = entity_number;
	}
	BotFindEnemy_PrepareEntity(1, 0.0f, 0.0f, 0.0f);
}

/*
=============
BotAINode_PrepareFrame

Seeds one fixture-free node frame with deterministic combat ownership.
=============
*/
static void BotAINode_PrepareFrame(bot_interface_test_context_t *context,
	bot_client_state_t *bot,
	bot_ai_node_t node,
	int current_enemy,
	float sight_time,
	int health,
	bool aggressive,
	bool trace_blocked)
{
	assert_non_null(context);
	assert_non_null(bot);

	memset(&bot->combat, 0, sizeof(bot->combat));
	bot->combat.current_enemy = current_enemy;
	bot->combat.last_enemy_area = 1;
	bot->combat.chase_time = aasworld.time + 10.0f;
	VectorSet(bot->combat.last_enemy_origin, 100.0f, 0.0f, 0.0f);
	bot->combat.enemy_sight_time = sight_time;
	bot->combat.last_known_health = 100;
	bot->ai_node = node;
	bot->ai_node_switches = -1;
	bot->ai_node_overflow = true;
	bot->chat_standing = false;
	bot->enter_game_time = -1000.0f;
	bot->ltg_type = 0;
	bot->nearby_goal_time = 0.0f;
	bot->nearby_goal_check_time = 0.0f;
	bot->long_term_goal_time = 0.0f;
	bot->client_update_valid = true;
	context->api->BotEmptyGoalStack(bot->goal_handle);

	memset(bot->last_client_update.inventory,
		0,
		sizeof(bot->last_client_update.inventory));
	memset(bot->last_client_update.stats,
		0,
		sizeof(bot->last_client_update.stats));
	bot->last_client_update.stats[STAT_HEALTH] = health;
	bot->last_client_update.inventory[RETAIL_INVENTORY_HEALTH] = health;
	if (aggressive)
	{
		bot->last_client_update.inventory[RETAIL_INVENTORY_BFG10K] = 1;
		bot->last_client_update.inventory[RETAIL_INVENTORY_CELLS] = 51;
	}
	VectorClear(bot->last_client_update.origin);
	VectorClear(bot->last_client_update.velocity);
	VectorClear(bot->last_client_update.viewoffset);
	VectorClear(bot->last_client_update.viewangles);
	AI_DMState_Reset(bot->dm_state);
	context->mock.trace_blocked = trace_blocked;
}

/*
=============
BotAINode_PushNearbyGoal

Builds the live NBG-stack state required by retail's Seek-NBG node.
=============
*/
static void BotAINode_PushNearbyGoal(bot_interface_test_context_t *context,
	bot_client_state_t *bot,
	int number)
{
	bot_goal_t goal = {0};

	assert_non_null(context);
	assert_non_null(bot);

	goal.number = number;
	goal.areanum = 1;
	VectorSet(goal.origin, 100.0f, 0.0f, 0.0f);
	VectorSet(goal.mins, -8.0f, -8.0f, -8.0f);
	VectorSet(goal.maxs, 8.0f, 8.0f, 8.0f);
	assert_true(context->api->BotPushGoal(bot->goal_handle, &goal) != 0);
	bot->nearby_goal_time = aasworld.time + 5.0f;
}

/*
=============
BotAINode_RunFrame

Runs one export-level AI frame and verifies its bridge status.
=============
*/
static void BotAINode_RunFrame(bot_interface_test_context_t *context)
{
	assert_non_null(context);
	assert_int_equal(context->api->BotAI(0, 0.1f), BLERR_NOERROR);
}

/*
=============
BotAINode_QueueConsoleMessages

Fills the client's console queue past retail's ten-entry read-delay threshold so
one frame drains it whole.
=============
*/
static void BotAINode_QueueConsoleMessages(bot_interface_test_context_t *context,
	bot_client_state_t *bot,
	int count)
{
	assert_non_null(context);
	assert_non_null(bot);

	for (int index = 0; index < count; ++index)
	{
		char message[32];
		snprintf(message, sizeof(message), "console %d", index);
		assert_int_equal(context->api->BotConsoleMessage(bot->client_number,
			CMS_NORMAL,
			message),
			BLERR_NOERROR);
	}
	assert_int_equal(BotNumConsoleMessages(bot->chat_state), count);
}

/*
=============
BotAINode_RunFrameWithThinktime

Runs one export-level AI frame with a caller-selected think duration so the
Seek-LTG random-chat probability boundary can be deterministic.
=============
*/
static void BotAINode_RunFrameWithThinktime(bot_interface_test_context_t *context,
	float thinktime)
{
	assert_non_null(context);
	assert_int_equal(context->api->BotAI(0, thinktime), BLERR_NOERROR);
}

/*
=============
test_ai_node_scheduler_exact_switch_limit

Pins same-frame completion accounting and retail's exact 50-dispatch cap.
=============
*/
static void test_ai_node_scheduler_exact_switch_limit(void **state)
{
	(void)state;
	bot_client_state_t bot;
	memset(&bot, 0, sizeof(bot));

	bot_ai_node_loop_probe_t probe = {0, 2};
	assert_int_equal(BotAI_RunNodeSwitchLoop(&bot,
		BotAINode_LoopProbeStep,
		&probe),
		qtrue);
	assert_int_equal(probe.calls, 2);
	assert_int_equal(bot.ai_node_switches, 1);
	assert_false(bot.ai_node_overflow);

	memset(&probe, 0, sizeof(probe));
	assert_int_equal(BotAI_RunNodeSwitchLoop(&bot,
		BotAINode_LoopProbeStep,
		&probe),
		qfalse);
	assert_int_equal(probe.calls, BOT_AI_MAX_NODE_SWITCHES);
	assert_int_equal(bot.ai_node_switches, BOT_AI_MAX_NODE_SWITCHES);
	assert_true(bot.ai_node_overflow);
}

/*
=============
test_ai_node_find_enemy_schedule_and_clear_matrix

Pins which retail nodes scan, which clear current enemy first, and which retain
the authoritative sight timestamp on failed acquisition. Activate's scan occurs
only after its committed movement step.
=============
*/
static void test_ai_node_find_enemy_schedule_and_clear_matrix(void **state)
{
	bot_interface_test_context_t *context =
		(bot_interface_test_context_t *)*state;
	bot_client_state_t *bot = BotFindEnemy_SetupHarness(context,
		4,
		"bots/babe_c.c",
		"babe");

	const struct
	{
		bot_ai_node_t node;
		int current_enemy;
		bool live_current;
		bool trace_blocked;
		int expected_enemy;
		int expected_health;
		bot_ai_node_t expected_node;
		int expected_switches;
	} cases[] = {
		{BOT_AI_NODE_STAND, 3, false, false, 3, 75, BOT_AI_NODE_STAND, 0},
		{BOT_AI_NODE_ACTIVATE_ENTITY, 3, false, false, 0, 75, BOT_AI_NODE_ACTIVATE_ENTITY, 0},
		{BOT_AI_NODE_SEEK_NBG, 3, false, false, 0, 75, BOT_AI_NODE_SEEK_NBG, 0},
		{BOT_AI_NODE_SEEK_LTG, 3, false, false, 0, 75, BOT_AI_NODE_SEEK_LTG, 0},
		{BOT_AI_NODE_BATTLE_FIGHT, 2, true, false, 2, 100, BOT_AI_NODE_BATTLE_FIGHT, 0},
		{BOT_AI_NODE_BATTLE_CHASE, 2, true, true, 2, 75, BOT_AI_NODE_BATTLE_CHASE, 0},
	};

	for (size_t index = 0; index < ARRAY_LEN(cases); ++index)
	{
		BotAINode_ClearEnemyEntities();
		if (cases[index].live_current)
		{
			BotFindEnemy_PrepareEntity(2, 100.0f, 0.0f, 0.0f);
		}
		BotAINode_PrepareFrame(context,
			bot,
			cases[index].node,
			cases[index].current_enemy,
			73.25f,
			75,
			true,
			cases[index].trace_blocked);
		if (cases[index].node == BOT_AI_NODE_SEEK_NBG)
		{
			BotAINode_PushNearbyGoal(context, bot, 700 + (int)index);
		}

		BotAINode_RunFrame(context);
		assert_int_equal(bot->combat.current_enemy,
			cases[index].expected_enemy);
		assert_float_equal(bot->combat.enemy_sight_time,
			73.25f,
			0.0001f);
		assert_int_equal(bot->combat.last_known_health,
			cases[index].expected_health);
		assert_int_equal(bot->ai_node, cases[index].expected_node);
		assert_int_equal(bot->ai_node_switches, cases[index].expected_switches);
		assert_false(bot->ai_node_overflow);
	}
}

/*
=============
test_ai_activate_entity_goal_lifetime_contact_failure_and_enemy_scan

Pins Gladiator's standalone activation goal: expiry or contact transition
through Seek-NBG and Seek-LTG in the same scheduler frame, while a failed
move preserves the independent activation deadline and clears only NBG time.
The committed move then precedes the node's delayed enemy scan.
=============
*/
static void test_ai_activate_entity_goal_lifetime_contact_failure_and_enemy_scan(
	void **state)
{
	bot_interface_test_context_t *context =
		(bot_interface_test_context_t *)*state;
	bot_client_state_t *bot = BotFindEnemy_SetupHarness(context,
		4,
		"bots/babe_c.c",
		"babe");

	BotAINode_ClearEnemyEntities();
	BotFindEnemy_PrepareEntity(2, 100.0f, 0.0f, 0.0f);
	aasworld.time = 74.0f;
	BotAINode_PrepareFrame(context,
		bot,
		BOT_AI_NODE_ACTIVATE_ENTITY,
		9,
		-15.0f,
		75,
		true,
		false);
	memset(&bot->activation_goal, 0, sizeof(bot->activation_goal));
	bot->activation_goal.areanum = 1;
	VectorSet(bot->activation_goal.origin, 128.0f, 0.0f, 0.0f);
	VectorSet(bot->activation_goal.mins, -8.0f, -8.0f, -8.0f);
	VectorSet(bot->activation_goal.maxs, 8.0f, 8.0f, 8.0f);
	bot->activation_goal_time = aasworld.time - 0.001f;
	BotAINode_RunFrame(context);
	assert_int_equal(bot->ai_node, BOT_AI_NODE_BATTLE_FIGHT);
	assert_int_equal(bot->ai_node_switches, 3);
	assert_int_equal(bot->combat.current_enemy, 2);
	assert_float_equal(bot->activation_goal_time, 73.999f, 0.0001f);
	assert_int_equal(bot->activation_goal.areanum, 1);

	BotAINode_ClearEnemyEntities();
	BotFindEnemy_PrepareEntity(2, 100.0f, 0.0f, 0.0f);
	aasworld.time = 75.0f;
	BotAINode_PrepareFrame(context,
		bot,
		BOT_AI_NODE_ACTIVATE_ENTITY,
		9,
		-15.0f,
		75,
		true,
		false);
	memset(&bot->activation_goal, 0, sizeof(bot->activation_goal));
	bot->activation_goal.areanum = 1;
	VectorClear(bot->activation_goal.origin);
	VectorSet(bot->activation_goal.mins, -8.0f, -8.0f, -8.0f);
	VectorSet(bot->activation_goal.maxs, 8.0f, 8.0f, 8.0f);
	bot->activation_goal_time = aasworld.time + 10.0f;
	BotAINode_RunFrame(context);
	assert_int_equal(bot->ai_node, BOT_AI_NODE_BATTLE_FIGHT);
	assert_int_equal(bot->ai_node_switches, 3);
	assert_int_equal(bot->combat.current_enemy, 2);
	assert_float_equal(bot->activation_goal_time, 0.0f, 0.0001f);
	assert_int_equal(bot->activation_goal.areanum, 1);

	BotAINode_ClearEnemyEntities();
	aasworld.time = 76.0f;
	BotAINode_PrepareFrame(context,
		bot,
		BOT_AI_NODE_ACTIVATE_ENTITY,
		0,
		-15.0f,
		75,
		true,
		false);
	bot->last_client_update.pm_flags = PMF_ON_GROUND;
	memset(&bot->activation_goal, 0, sizeof(bot->activation_goal));
	bot->activation_goal.areanum = 1;
	VectorSet(bot->activation_goal.origin, 128.0f, 0.0f, 0.0f);
	VectorSet(bot->activation_goal.mins, -8.0f, -8.0f, -8.0f);
	VectorSet(bot->activation_goal.maxs, 8.0f, 8.0f, 8.0f);
	bot->activation_goal_time = aasworld.time + 10.0f;
	bot->nearby_goal_time = aasworld.time + 2.0f;
	BotAINode_RunFrame(context);
	assert_int_equal(bot->ai_node, BOT_AI_NODE_ACTIVATE_ENTITY);
	assert_true(bot->has_move_result);
	assert_true(bot->last_move_result.failure);
	assert_float_equal(bot->activation_goal_time, 86.0f, 0.0001f);
	assert_float_equal(bot->nearby_goal_time, 0.0f, 0.0001f);

	BotAINode_ClearEnemyEntities();
	BotFindEnemy_PrepareEntity(2, 100.0f, 0.0f, 0.0f);
	aasworld.time = 77.0f;
	BotAINode_PrepareFrame(context,
		bot,
		BOT_AI_NODE_ACTIVATE_ENTITY,
		0,
		-15.0f,
		100,
		true,
		false);
	bot->last_client_update.pm_flags = PMF_ON_GROUND;
	memset(&bot->activation_goal, 0, sizeof(bot->activation_goal));
	bot->activation_goal.areanum = 1;
	VectorSet(bot->activation_goal.origin, 128.0f, 0.0f, 0.0f);
	VectorSet(bot->activation_goal.mins, -8.0f, -8.0f, -8.0f);
	VectorSet(bot->activation_goal.maxs, 8.0f, 8.0f, 8.0f);
	bot->activation_goal_time = aasworld.time + 10.0f;
	BotAINode_RunFrame(context);
	assert_true(bot->has_move_result);
	assert_true(bot->last_move_result.failure);
	assert_int_equal(bot->ai_node, BOT_AI_NODE_BATTLE_FIGHT);
	assert_int_equal(bot->ai_node_switches, 0);
	assert_int_equal(bot->combat.current_enemy, 2);
	assert_float_equal(bot->activation_goal_time, 87.0f, 0.0001f);
}

/*
=============
test_ai_node_immediate_delayed_and_retreat_transitions

Separates Stand/LTG same-frame fights from Seek-NBG's post-movement
acquisition and pins each acquisition caller's retreat destination.
=============
*/
static void test_ai_node_immediate_delayed_and_retreat_transitions(void **state)
{
	bot_interface_test_context_t *context =
		(bot_interface_test_context_t *)*state;
	bot_client_state_t *bot = BotFindEnemy_SetupHarness(context,
		4,
		"bots/babe_c.c",
		"babe");

	const bot_ai_node_t immediate_nodes[] = {
		BOT_AI_NODE_STAND,
		BOT_AI_NODE_SEEK_LTG,
	};
	for (size_t index = 0; index < ARRAY_LEN(immediate_nodes); ++index)
	{
		BotAINode_ClearEnemyEntities();
		BotFindEnemy_PrepareEntity(2, 100.0f, 0.0f, 0.0f);
		aasworld.time = 30.0f + (float)index;
		BotAINode_PrepareFrame(context,
			bot,
			immediate_nodes[index],
			9,
			-10.0f,
			75,
			true,
			false);
		BotAINode_RunFrame(context);
		assert_int_equal(bot->ai_node, BOT_AI_NODE_BATTLE_FIGHT);
		assert_int_equal(bot->ai_node_switches, 1);
		assert_int_equal(bot->combat.current_enemy, 2);
		assert_float_equal(bot->combat.enemy_sight_time,
			aasworld.time,
			0.0001f);
		assert_true(bot->combat.enemy_visible);
	}

	const bot_ai_node_t delayed_nodes[] = {
		BOT_AI_NODE_SEEK_NBG,
	};
	for (size_t index = 0; index < ARRAY_LEN(delayed_nodes); ++index)
	{
		BotAINode_ClearEnemyEntities();
		BotFindEnemy_PrepareEntity(2, 100.0f, 0.0f, 0.0f);
		aasworld.time = 40.0f + (float)index;
		BotAINode_PrepareFrame(context,
			bot,
			delayed_nodes[index],
			9,
			-20.0f,
			75,
			true,
			false);
		if (delayed_nodes[index] == BOT_AI_NODE_SEEK_NBG)
		{
			BotAINode_PushNearbyGoal(context, bot, 710 + (int)index);
		}

		BotAINode_RunFrame(context);
		assert_int_equal(bot->ai_node, BOT_AI_NODE_BATTLE_FIGHT);
		assert_int_equal(bot->ai_node_switches, 0);
		assert_int_equal(bot->combat.current_enemy, 2);
		assert_float_equal(bot->combat.enemy_sight_time,
			aasworld.time,
			0.0001f);
		assert_false(bot->combat.enemy_visible);

		bot->client_update_valid = true;
		BotAINode_RunFrame(context);
		assert_int_equal(bot->ai_node, BOT_AI_NODE_BATTLE_FIGHT);
		assert_true(bot->combat.enemy_visible);
	}

	const struct
	{
		bot_ai_node_t node;
		bot_ai_node_t expected_node;
		int expected_switches;
	} retreat_cases[] = {
		{BOT_AI_NODE_SEEK_NBG, BOT_AI_NODE_BATTLE_NBG, 0},
		{BOT_AI_NODE_SEEK_LTG, BOT_AI_NODE_BATTLE_RETREAT, 1},
	};
	for (size_t index = 0; index < ARRAY_LEN(retreat_cases); ++index)
	{
		BotAINode_ClearEnemyEntities();
		BotFindEnemy_PrepareEntity(2, 100.0f, 0.0f, 0.0f);
		aasworld.time = 50.0f + (float)index;
		BotAINode_PrepareFrame(context,
			bot,
			retreat_cases[index].node,
			9,
			-30.0f,
			30,
			false,
			false);
		if (retreat_cases[index].node == BOT_AI_NODE_SEEK_NBG)
		{
			BotAINode_PushNearbyGoal(context, bot, 720 + (int)index);
		}

		BotAINode_RunFrame(context);
		assert_int_equal(bot->ai_node,
			retreat_cases[index].expected_node);
		assert_int_equal(bot->ai_node_switches,
			retreat_cases[index].expected_switches);
		assert_int_equal(bot->combat.current_enemy, 2);
		assert_float_equal(bot->combat.enemy_sight_time,
			aasworld.time,
			0.0001f);
		assert_false(bot->combat.enemy_visible);
	}
}

/*
=============
test_ai_seek_nbg_scans_before_private_view_turn

Pins Seek-NBG's late-frame ordering (0x1001f59d scans, 0x1001f5e1 turns): the
post-movement enemy candidate is committed before the private view turn, so the
turn runs at the character's CHARACTERISTIC_VIEW_ACCELERATION (hunk_c.c: 360
deg/s, i.e. 36 deg per 0.1s think, quantised to 35.996704) rather than the
no-enemy literal 100 deg/s (9.997559). The bot is placed on ground and the goal
shares its area so BotMoveToGoal reaches BotMoveInGoalArea and writes a real
movedir: retail's BotClearMoveResult clears only the 0x18-byte status prefix
(0x10031e26), so a move result that reaches no travel branch leaves movedir
uninitialised and the private ideal view angle indeterminate.
=============
*/
static void test_ai_seek_nbg_scans_before_private_view_turn(void **state)
{
	bot_interface_test_context_t *context =
		(bot_interface_test_context_t *)*state;
	bot_client_state_t *bot = BotFindEnemy_SetupHarness(context,
		4,
		"bots/hunk_c.c",
		"hunk");
	bot_goal_t nearby_goal = {0};
	vec3_t viewangles = {0.0f, 0.0f, 0.0f};

	BotAINode_ClearEnemyEntities();
	BotFindEnemy_PrepareEntity(2, -200.0f, 346.41016f, 0.0f);
	aasworld.time = 41.0f;
	BotAINode_PrepareFrame(context,
		bot,
		BOT_AI_NODE_SEEK_NBG,
		0,
		-20.0f,
		75,
		true,
		false);
	bot->last_client_update.pm_flags |= PMF_ON_GROUND;
	nearby_goal.number = 711;
	nearby_goal.areanum = 0;
	VectorSet(nearby_goal.origin, 0.0f, 100.0f, 0.0f);
	VectorSet(nearby_goal.mins, -8.0f, -8.0f, -8.0f);
	VectorSet(nearby_goal.maxs, 8.0f, 8.0f, 8.0f);
	assert_true(context->api->BotPushGoal(bot->goal_handle, &nearby_goal) != 0);
	bot->nearby_goal_time = aasworld.time + 5.0f;

	BotAINode_RunFrame(context);
	assert_int_equal(bot->ai_node, BOT_AI_NODE_BATTLE_FIGHT);
	assert_int_equal(bot->combat.current_enemy, 2);
	AI_DMState_GetViewAngles(bot->dm_state, viewangles);
	assert_float_equal(viewangles[YAW], 35.996704f, 0.0001f);
}

/*
=============
test_ai_weapon_selection_is_combat_node_local

Pins the retail selector schedule: standing and Battle Chase do not select,
while live Fight, active Retreat, and Battle NBG do.
The low-attack-skill profile isolates that schedule from Fight's inherited
direct-movement state.
=============
*/
static void test_ai_weapon_selection_is_combat_node_local(void **state)
{
	bot_interface_test_context_t *context =
		(bot_interface_test_context_t *)*state;
	bot_client_state_t *bot = BotFindEnemy_SetupHarness(context,
		4,
		"bots/zero_c.c",
		"zero");
	BotState_EmitPendingClientCommands(bot);
	Mock_Reset(&context->mock);

	const struct
	{
		bot_ai_node_t node;
		bool live_enemy;
		bool trace_blocked;
		bool goal;
		bool retreat;
		size_t expected_commands;
	} cases[] = {
		{BOT_AI_NODE_STAND, false, false, false, false, 0U},
		{BOT_AI_NODE_BATTLE_CHASE, true, true, false, false, 0U},
		{BOT_AI_NODE_BATTLE_FIGHT, true, false, false, false, 1U},
		{BOT_AI_NODE_BATTLE_RETREAT, true, false, true, true, 1U},
		{BOT_AI_NODE_BATTLE_NBG, true, false, true, false, 1U},
	};

	for (size_t index = 0; index < ARRAY_LEN(cases); ++index)
	{
		Mock_Reset(&context->mock);
		assert_int_equal(EA_ResetClient(bot->client_number), BLERR_NOERROR);
		BotResetWeaponState(bot->weapon_state);
		bot->current_weapon = 0;
		BotAINode_ClearEnemyEntities();
		if (cases[index].live_enemy)
		{
			BotFindEnemy_PrepareEntity(2, 100.0f, 0.0f, 0.0f);
		}

		aasworld.time = 110.0f + (float)index;
		BotAINode_PrepareFrame(context,
			bot,
			cases[index].node,
			cases[index].live_enemy ? 2 : 0,
			aasworld.time - 1.0f,
			100,
			true,
			cases[index].trace_blocked);
		if (cases[index].goal)
		{
			BotAINode_PushNearbyGoal(context, bot, 730 + (int)index);
			bot->nearby_goal_time = aasworld.time + 5.0f;
		}
		if (cases[index].retreat)
		{
			/* Get-flag is the recovered retail LTG type at interface-local value 4. */
			bot->ltg_type = 4;
			assert_true(BotAI_WantsToRetreat(bot));
		}
		BotAINode_RunFrame(context);
		assert_int_equal(context->mock.client_command_count,
			cases[index].expected_commands);
		if (cases[index].expected_commands != 0U)
		{
			assert_int_equal(context->mock.client_commands[0].client, 0);
			assert_true(strcmp(context->mock.client_commands[0].command,
				"use") == 0 ||
				strncmp(context->mock.client_commands[0].command,
					"use ",
					4U) == 0);
		}
		if (cases[index].node == BOT_AI_NODE_BATTLE_FIGHT)
		{
			assert_true(context->mock.bot_input_count > 0U);
			assert_int_equal(context->mock.inputs[
				context->mock.bot_input_count - 1U].weapon,
				0);
		}
	}
}

/*
=============
test_ai_battle_fight_runs_general_item_use

Pins Fight's BattleUseItems-then-UseItems sequence after weapon selection.
=============
*/
static void test_ai_battle_fight_runs_general_item_use(void **state)
{
	bot_interface_test_context_t *context =
		(bot_interface_test_context_t *)*state;
	bot_client_state_t *bot = BotFindEnemy_SetupHarness(context,
		4,
		"bots/babe_c.c",
		"babe");
	BotState_EmitPendingClientCommands(bot);
	Mock_Reset(&context->mock);
	BotResetWeaponState(bot->weapon_state);

	BotAINode_ClearEnemyEntities();
	BotFindEnemy_PrepareEntity(2, 100.0f, 0.0f, 0.0f);
	aasworld.time = 116.0f;
	BotAINode_PrepareFrame(context,
		bot,
		BOT_AI_NODE_BATTLE_FIGHT,
		2,
		aasworld.time - 1.0f,
		100,
		true,
		false);
	bot->last_client_update.inventory[RETAIL_INVENTORY_QUAD] = 1;
	bot->last_client_update.inventory[RETAIL_INVENTORY_SILENCER] = 1;

	BotAINode_RunFrame(context);
	int quad_command = -1;
	int silencer_command = -1;
	for (size_t index = 0; index < context->mock.client_command_count; ++index)
	{
		if (strcmp(context->mock.client_commands[index].command, "use") != 0)
		{
			continue;
		}
		if (strcmp(context->mock.client_commands[index].argument,
			"Quad Damage") == 0)
		{
			quad_command = (int)index;
		}
		else if (strcmp(context->mock.client_commands[index].argument,
			"Silencer") == 0)
		{
			silencer_command = (int)index;
		}
	}

	assert_true(quad_command >= 0);
	assert_true(silencer_command >= 0);
	assert_true(quad_command < silencer_command);
}

/*
=============
test_ai_active_battle_movers_run_general_item_use

Pins the retail general item pass in active Battle Chase, NBG, and Retreat
before each node refreshes its move state.
=============
*/
static void test_ai_active_battle_movers_run_general_item_use(void **state)
{
	bot_interface_test_context_t *context =
		(bot_interface_test_context_t *)*state;
	bot_client_state_t *bot = BotFindEnemy_SetupHarness(context,
		4,
		"bots/babe_c.c",
		"babe");
	BotState_EmitPendingClientCommands(bot);

	const struct
	{
		bot_ai_node_t node;
		bool trace_blocked;
		bool push_goal;
		bool retreat;
	} cases[] = {
		{BOT_AI_NODE_BATTLE_CHASE, true, false, false},
		{BOT_AI_NODE_BATTLE_NBG, false, true, false},
		{BOT_AI_NODE_BATTLE_RETREAT, false, true, true},
	};

	for (size_t case_index = 0; case_index < ARRAY_LEN(cases); ++case_index)
	{
		Mock_Reset(&context->mock);
		assert_int_equal(EA_ResetClient(bot->client_number), BLERR_NOERROR);
		BotResetWeaponState(bot->weapon_state);
		bot->current_weapon = 0;

		BotAINode_ClearEnemyEntities();
		BotFindEnemy_PrepareEntity(2, 100.0f, 0.0f, 0.0f);
		aasworld.time = 118.0f + (float)case_index;
		BotAINode_PrepareFrame(context,
			bot,
			cases[case_index].node,
			2,
			aasworld.time - 1.0f,
			100,
			true,
			cases[case_index].trace_blocked);
		bot->last_client_update.inventory[RETAIL_INVENTORY_SILENCER] = 1;
		if (cases[case_index].push_goal)
		{
			BotAINode_PushNearbyGoal(context,
				bot,
				760 + (int)case_index);
			bot->nearby_goal_time = aasworld.time + 5.0f;
		}
		if (cases[case_index].retreat)
		{
			bot->ltg_type = 4;
			assert_true(BotAI_WantsToRetreat(bot));
		}

		BotAINode_RunFrame(context);
		bool used_silencer = false;
		for (size_t command_index = 0;
			command_index < context->mock.client_command_count;
			++command_index)
		{
			if (strcmp(context->mock.client_commands[command_index].command,
				"use") == 0 &&
				strcmp(context->mock.client_commands[command_index].argument,
					"Silencer") == 0)
			{
				used_silencer = true;
				break;
			}
		}
		assert_true(used_silencer);
	}
}

/*
=============
test_ai_battle_fight_leaves_move_state_untouched

Pins Fight's minimum attack-skill gate. Retail leaves the mover untouched when
BotAttackMove returns before its post-gate move-state setup.
=============
*/
static void test_ai_battle_fight_leaves_move_state_untouched(void **state)
{
	bot_interface_test_context_t *context =
		(bot_interface_test_context_t *)*state;
	bot_client_state_t *bot = BotFindEnemy_SetupHarness(context,
		4,
		"bots/zero_c.c",
		"zero");

	BotAINode_ClearEnemyEntities();
	BotFindEnemy_PrepareEntity(2, 100.0f, 0.0f, 0.0f);
	aasworld.time = 117.0f;
	BotAINode_PrepareFrame(context,
		bot,
		BOT_AI_NODE_BATTLE_FIGHT,
		2,
		aasworld.time - 1.0f,
		100,
		true,
		false);

	bot_movestate_t *move_state = BotMoveStateFromHandle(bot->move_handle);
	assert_non_null(move_state);
	move_state->client = 71;
	move_state->entitynum = 72;

	BotAINode_RunFrame(context);
	assert_int_equal(move_state->client, 71);
	assert_int_equal(move_state->entitynum, 72);
}

/*
=============
test_ai_battle_fight_does_not_invent_rocket_jump

Pins Battle Fight's raw tail: enabled rocketjump travel does not append an
autonomous vertical jump after BotAttackMove, aim, and BotCheckAttack.
=============
*/
static void test_ai_battle_fight_does_not_invent_rocket_jump(void **state)
{
	bot_interface_test_context_t *context =
		(bot_interface_test_context_t *)*state;
	bot_client_state_t *bot = BotFindEnemy_SetupHarness(context,
		4,
		"bots/zero_c.c",
		"zero");

	assert_int_equal(context->api->BotLibVarSet("rocketjump", "1"),
		BLERR_NOERROR);
	BotAINode_ClearEnemyEntities();
	BotFindEnemy_PrepareEntity(2, 100.0f, 0.0f, 128.0f);
	aasworld.time = 117.0f;
	BotAINode_PrepareFrame(context,
		bot,
		BOT_AI_NODE_BATTLE_FIGHT,
		2,
		aasworld.time - 1.0f,
		100,
		true,
		false);

	BotAINode_RunFrame(context);
	assert_true(context->mock.bot_input_count > 0U);
	assert_int_equal(context->mock.inputs[
		context->mock.bot_input_count - 1U].actionflags & ACTION_JUMP,
		0);
	assert_int_equal(context->api->BotLibVarSet("rocketjump", "0"),
		BLERR_NOERROR);
}

/*
=============
test_ai_battle_fight_initialises_move_state_after_attack_gates

Pins sub_10022e10's normal-path setup call: it follows the pizza and minimum
attack-skill returns, rather than running at the Fight node entry.
=============
*/
static void test_ai_battle_fight_initialises_move_state_after_attack_gates(void **state)
{
	bot_interface_test_context_t *context =
		(bot_interface_test_context_t *)*state;
	bot_client_state_t *bot = BotFindEnemy_SetupHarness(context,
		4,
		"bots/babe_c.c",
		"babe");

	BotAINode_ClearEnemyEntities();
	/* Use the retail swim dispatcher so this setup-order test needs no walk
	 * collision fixture. */
	BotFindEnemy_PrepareEntity(2, 0.0f, 0.0f, 0.0f);
	context->mock.point_contents_result = CONTENTS_WATER;
	aasworld.time = 117.0f;
	BotAINode_PrepareFrame(context,
		bot,
		BOT_AI_NODE_BATTLE_FIGHT,
		2,
		aasworld.time - 1.0f,
		100,
		true,
		false);
	bot->last_client_update.pm_flags = PMF_ON_GROUND;

	bot_movestate_t *move_state = BotMoveStateFromHandle(bot->move_handle);
	assert_non_null(move_state);
	move_state->client = 71;
	move_state->entitynum = 72;
	move_state->thinktime = 7.0f;

	BotAINode_RunFrame(context);
	assert_int_equal(move_state->client, bot->client_number);
	assert_int_equal(move_state->entitynum, bot->entity_number);
	assert_float_equal(move_state->thinktime, 0.1f, 0.0001f);
	assert_true((move_state->moveflags & MFL_ONGROUND) != 0);
}

/*
=============
test_ai_node_fight_and_chase_enemy_ownership

Proves Fight reuses its retained enemy, an old-visible Chase does not refresh
sight, and Chase scans only after its retained target is no longer visible.
=============
*/
static void test_ai_node_fight_and_chase_enemy_ownership(void **state)
{
	bot_interface_test_context_t *context =
		(bot_interface_test_context_t *)*state;
	bot_client_state_t *bot = BotFindEnemy_SetupHarness(context,
		4,
		"bots/babe_c.c",
		"babe");
	ai_dm_metrics_t metrics;

	BotAINode_ClearEnemyEntities();
	BotFindEnemy_PrepareEntity(2, 100.0f, 0.0f, 0.0f);
	aasworld.time = 60.0f;
	BotAINode_PrepareFrame(context,
		bot,
		BOT_AI_NODE_BATTLE_FIGHT,
		2,
		12.5f,
		75,
		true,
		false);
	BotAINode_RunFrame(context);
	assert_int_equal(bot->combat.current_enemy, 2);
	assert_float_equal(bot->combat.enemy_sight_time, 12.5f, 0.0001f);
	assert_int_equal(bot->combat.last_known_health, 100);
	assert_true(bot->combat.enemy_visible);
	AI_DMState_GetMetrics(bot->dm_state, &metrics);
	assert_int_equal(metrics.enemy_entity, 2);

	BotAINode_ClearEnemyEntities();
	BotFindEnemy_PrepareEntity(2, 100.0f, 0.0f, 0.0f);
	aasworld.time = 61.0f;
	BotAINode_PrepareFrame(context,
		bot,
		BOT_AI_NODE_BATTLE_CHASE,
		2,
		22.5f,
		75,
		true,
		false);
	BotAINode_RunFrame(context);
	assert_int_equal(bot->ai_node, BOT_AI_NODE_BATTLE_FIGHT);
	assert_int_equal(bot->ai_node_switches, 1);
	assert_int_equal(bot->combat.current_enemy, 2);
	assert_float_equal(bot->combat.enemy_sight_time, 22.5f, 0.0001f);
	assert_int_equal(bot->combat.last_known_health, 100);
	assert_true(bot->combat.enemy_visible);

	BotAINode_ClearEnemyEntities();
	BotFindEnemy_PrepareEntity(2, 100.0f, 0.0f, 0.0f);
	aasworld.time = 62.0f;
	BotAINode_PrepareFrame(context,
		bot,
		BOT_AI_NODE_BATTLE_CHASE,
		5,
		32.5f,
		75,
		true,
		false);
	BotAINode_RunFrame(context);
	assert_int_equal(bot->ai_node, BOT_AI_NODE_BATTLE_FIGHT);
	assert_int_equal(bot->ai_node_switches, 1);
	assert_int_equal(bot->combat.current_enemy, 2);
	assert_float_equal(bot->combat.enemy_sight_time, 62.0f, 0.0001f);
	assert_int_equal(bot->combat.last_known_health, 75);
	assert_true(bot->combat.enemy_visible);
}

/*
=============
test_ai_battle_fight_dead_enemy_enters_kill_chat_stand

Pins Battle Fight's dead-enemy branch: retail constructs the subtype-selected
kill chat, waits its typing time in Stand, and only then resumes Seek LTG.
=============
*/
static void test_ai_battle_fight_dead_enemy_enters_kill_chat_stand(void **state)
{
	bot_interface_test_context_t *context =
		(bot_interface_test_context_t *)*state;
	bot_client_state_t *bot = BotFindEnemy_SetupHarness(context,
		4,
		"bots/babe_c.c",
		"babe");

	assert_int_equal(context->api->BotLibVarSet("nochat", "0"),
		BLERR_NOERROR);
	assert_int_equal(context->api->BotLibVarSet("fastchat", "1"),
		BLERR_NOERROR);
	BotAINode_ClearEnemyEntities();
	aas_entity_t *enemy = BotFindEnemy_PrepareEntity(2,
		100.0f,
		0.0f,
		0.0f);
	enemy->effects = EF_GIB;
	aasworld.time = 70.0f;
	BotAINode_PrepareFrame(context,
		bot,
		BOT_AI_NODE_BATTLE_FIGHT,
		2,
		69.0f,
		100,
		true,
		false);
	bot->last_client_update.pm_flags = PMF_ON_GROUND;
	bot->enemy_death_type = 13;

	BotAINode_RunFrame(context);
	assert_int_equal(bot->ai_node, BOT_AI_NODE_STAND);
	assert_true(bot->chat_standing);
	assert_true(bot->stand_time > aasworld.time);
	assert_true(BotChatLength(bot->chat_state) > 0);

	aasworld.time = bot->stand_time;
	bot->client_update_valid = true;
	BotAINode_RunFrame(context);
	assert_int_equal(bot->ai_node, BOT_AI_NODE_STAND);
	assert_true(bot->chat_standing);
	assert_true(BotChatLength(bot->chat_state) > 0);

	aasworld.time += 0.001f;
	bot->client_update_valid = true;
	BotAINode_RunFrame(context);
	assert_int_equal(bot->ai_node, BOT_AI_NODE_SEEK_LTG);
	assert_false(bot->chat_standing);
	assert_int_equal(BotChatLength(bot->chat_state), 0);

	assert_int_equal(context->api->BotLibVarSet("fastchat", "0"),
		BLERR_NOERROR);
}

/*
=============
test_ai_stand_acquires_enemy_before_pending_chat_wait

Pins Stand's HLIL ordering: a pending chat's typing deadline cannot postpone
the node's first visible-enemy acquisition trial.
=============
*/
static void test_ai_stand_acquires_enemy_before_pending_chat_wait(void **state)
{
	bot_interface_test_context_t *context =
		(bot_interface_test_context_t *)*state;
	bot_client_state_t *bot = BotFindEnemy_SetupHarness(context,
		4,
		"bots/babe_c.c",
		"babe");

	BotAINode_ClearEnemyEntities();
	BotFindEnemy_PrepareEntity(2, 100.0f, 0.0f, 0.0f);
	aasworld.time = 75.0f;
	BotAINode_PrepareFrame(context,
		bot,
		BOT_AI_NODE_STAND,
		0,
		0.0f,
		100,
		true,
		false);
	BotInitialChat(bot->chat_state,
		"kill_telefrag",
		"",
		NULL);
	assert_true(BotChatLength(bot->chat_state) > 0);
	bot->chat_standing = true;
	bot->stand_time = aasworld.time + 30.0f;

	BotAINode_RunFrame(context);
	assert_int_equal(bot->ai_node, BOT_AI_NODE_BATTLE_FIGHT);
	assert_int_equal(bot->combat.current_enemy, 2);
	assert_true(bot->chat_standing);
	assert_true(BotChatLength(bot->chat_state) > 0);
}

/*
=============
test_ai_stand_without_enemy_advances_private_view

Pins Stand's no-enemy body: after the acquisition trial fails, retail invokes
sub_10029150 before it considers the pending-chat deadline.
=============
*/
static void test_ai_stand_without_enemy_advances_private_view(void **state)
{
	bot_interface_test_context_t *context =
		(bot_interface_test_context_t *)*state;
	bot_client_state_t *bot = BotFindEnemy_SetupHarness(context,
		4,
		"bots/babe_c.c",
		"babe");
	vec3_t ideal_viewangles = {0.0f, 90.0f, 0.0f};

	BotAINode_ClearEnemyEntities();
	aasworld.time = 76.0f;
	BotAINode_PrepareFrame(context,
		bot,
		BOT_AI_NODE_STAND,
		0,
		0.0f,
		100,
		true,
		false);
	AI_DMState_SetIdealViewAngles(bot->dm_state, ideal_viewangles);

	BotAINode_RunFrame(context);
	assert_int_equal(bot->ai_node, BOT_AI_NODE_STAND);
	assert_float_equal(context->mock.inputs[
		context->mock.bot_input_count - 1U].speed,
		0.0f,
		0.0001f);
	assert_float_equal(context->mock.inputs[
		context->mock.bot_input_count - 1U].viewangles[YAW],
		9.99756f,
		0.01f);
}

/*
=============
test_ai_stand_expiry_advances_view_before_seek_ltg

Pins Stand's strict expiry handoff: retail turns once before entering Seek LTG,
whose no-goal return advances that retained turn a second time in the frame.
=============
*/
static void test_ai_stand_expiry_advances_view_before_seek_ltg(void **state)
{
	bot_interface_test_context_t *context =
		(bot_interface_test_context_t *)*state;
	bot_client_state_t *bot = BotFindEnemy_SetupHarness(context,
		4,
		"bots/babe_c.c",
		"babe");
	vec3_t ideal_viewangles = {0.0f, 90.0f, 0.0f};

	BotAINode_ClearEnemyEntities();
	aasworld.time = 77.0f;
	BotAINode_PrepareFrame(context,
		bot,
		BOT_AI_NODE_STAND,
		0,
		0.0f,
		100,
		true,
		false);
	bot->chat_standing = true;
	bot->stand_time = aasworld.time - 0.001f;
	AI_DMState_SetIdealViewAngles(bot->dm_state, ideal_viewangles);

	BotAINode_RunFrame(context);
	assert_int_equal(bot->ai_node, BOT_AI_NODE_SEEK_LTG);
	assert_false(bot->chat_standing);
	assert_float_equal(context->mock.inputs[
		context->mock.bot_input_count - 1U].viewangles[YAW],
		29.99268f,
		0.02f);
}

/*
=============
test_ai_stand_squatt_guard_removes_bot

Pins Stand's private anti-hack guard: an expired chat stand emits the retail
message and removebot command without consuming the chat or entering Seek-LTG.
The removebot request carries the speaking bot's ClientName (0x1001ed0a), so
the host removes that bot rather than the first in-use bot edict.
=============
*/
static void test_ai_stand_squatt_guard_removes_bot(void **state)
{
	bot_interface_test_context_t *context =
		(bot_interface_test_context_t *)*state;
	bot_client_state_t *bot = BotFindEnemy_SetupHarness(context,
		4,
		"bots/babe_c.c",
		"babe");

	bot_clientsettings_t presentation;
	memset(&presentation, 0, sizeof(presentation));
	snprintf(presentation.netname, sizeof(presentation.netname), "Babe");
	assert_int_equal(context->api->BotClientSettings(bot->client_number,
		&presentation),
		BLERR_NOERROR);
	assert_string_equal(BotState_ClientName(bot->client_number), "Babe");

	assert_int_equal(context->api->BotLibVarSet("__squatt", "1"),
		BLERR_NOERROR);
	BotAINode_ClearEnemyEntities();
	aasworld.time = 78.0f;
	BotAINode_PrepareFrame(context,
		bot,
		BOT_AI_NODE_STAND,
		0,
		0.0f,
		100,
		true,
		false);
	BotInitialChat(bot->chat_state,
		"kill_telefrag",
		"",
		NULL);
	assert_true(BotChatLength(bot->chat_state) > 0);
	bot->chat_standing = true;
	bot->stand_time = aasworld.time - 0.001f;
	Mock_Reset(&context->mock);

	BotAINode_RunFrame(context);
	assert_int_equal(bot->ai_node, BOT_AI_NODE_STAND);
	assert_true(bot->chat_standing);
	assert_true(BotChatLength(bot->chat_state) > 0);

	bool said_guard_message = false;
	bool sent_removebot = false;
	for (size_t index = 0; index < context->mock.client_command_count; ++index)
	{
		if (strcmp(context->mock.client_commands[index].command, "say") == 0)
		{
			assert_int_equal(context->mock.client_commands[index].client,
				bot->client_number);
			assert_string_equal(context->mock.client_commands[index].argument,
				"I never hacked your brain...\n");
			said_guard_message = true;
		}
		else if (strcmp(context->mock.client_commands[index].command,
			"removebot") == 0)
		{
			assert_int_equal(context->mock.client_commands[index].client,
				bot->client_number);
			/* Retail 0x1001ed0a stores ClientName(bs->client) in the single
			   argument slot ahead of the NULL sentinel at 0x1001ecff. */
			assert_string_equal(context->mock.client_commands[index].argument,
				"Babe");
			sent_removebot = true;
		}
	}
	assert_true(said_guard_message);
	assert_true(sent_removebot);

	/* Retail 0x10028f57: an out-of-range client warns and yields the shared
	   empty string rather than indexing past the name table. */
	Mock_ClearPrints(&context->mock);
	assert_string_equal(BotState_ClientName(MAX_CLIENTS), "");
	Mock_AssertPrintContains(&context->mock,
		"ClientName: client",
		PRT_WARNING);
	Mock_ClearPrints(&context->mock);
	assert_string_equal(BotState_ClientName(-1), "");
	Mock_AssertPrintContains(&context->mock,
		"ClientName: client",
		PRT_WARNING);

	assert_int_equal(context->api->BotLibVarSet("__squatt", "0"),
		BLERR_NOERROR);
}

/*
=============
test_ai_battle_chase_uses_entry_deadline

Verifies the retail Battle Chase entry starts a fresh ten-second deadline and
that the node remains eligible exactly at that deadline, expiring only after
time has advanced beyond it.
=============
*/
static void test_ai_battle_chase_uses_entry_deadline(void **state)
{
	bot_interface_test_context_t *context =
		(bot_interface_test_context_t *)*state;
	bot_client_state_t *bot = BotFindEnemy_SetupHarness(context,
		4,
		"bots/babe_c.c",
		"babe");
	ai_dm_metrics_t metrics;

	BotAINode_ClearEnemyEntities();
	BotFindEnemy_PrepareEntity(2, 100.0f, 0.0f, 0.0f);
	aasworld.time = 80.0f;
	BotAINode_PrepareFrame(context,
		bot,
		BOT_AI_NODE_BATTLE_FIGHT,
		2,
		79.0f,
		75,
		true,
		true);
	BotAINode_RunFrame(context);
	assert_int_equal(bot->ai_node, BOT_AI_NODE_BATTLE_CHASE);
	assert_float_equal(bot->combat.chase_time, 90.0f, 0.0001f);
	AI_DMState_GetMetrics(bot->dm_state, &metrics);
	assert_float_equal(metrics.chase_time, 90.0f, 0.0001f);

	aasworld.time = 90.0f;
	bot->client_update_valid = true;
	BotAINode_RunFrame(context);
	assert_int_equal(bot->ai_node, BOT_AI_NODE_BATTLE_CHASE);
	assert_true(bot->has_move_result);

	aasworld.time = 90.001f;
	bot->client_update_valid = true;
	BotAINode_RunFrame(context);
	assert_int_equal(bot->ai_node, BOT_AI_NODE_SEEK_LTG);
}

/*
=============
test_ai_battle_chase_reached_last_seen_goal_expires

Pins the distinct last-seen-goal contact gate: reaching its eight-unit box
zeros Battle Chase's deadline before the strict expiration comparison.
=============
*/
static void test_ai_battle_chase_reached_last_seen_goal_expires(void **state)
{
	bot_interface_test_context_t *context =
		(bot_interface_test_context_t *)*state;
	bot_client_state_t *bot = BotFindEnemy_SetupHarness(context,
		4,
		"bots/babe_c.c",
		"babe");

	BotAINode_ClearEnemyEntities();
	BotFindEnemy_PrepareEntity(2, 100.0f, 0.0f, 0.0f);
	aasworld.time = 100.0f;
	BotAINode_PrepareFrame(context,
		bot,
		BOT_AI_NODE_BATTLE_CHASE,
		2,
		99.0f,
		75,
		true,
		true);
	VectorClear(bot->combat.last_enemy_origin);
	BotAINode_RunFrame(context);
	assert_float_equal(bot->combat.chase_time, 0.0f, 0.0001f);
	assert_int_equal(bot->ai_node, BOT_AI_NODE_SEEK_LTG);
}

/*
=============
test_ai_battle_nbg_expiry_pops_nearby_goal

Pins Battle NBG's strict five-second expiry: it removes the retained nearby
goal and resumes Battle Fight when no stacked long-term goal remains.
=============
*/
static void test_ai_battle_nbg_expiry_pops_nearby_goal(void **state)
{
	bot_interface_test_context_t *context =
		(bot_interface_test_context_t *)*state;
	bot_client_state_t *bot = BotFindEnemy_SetupHarness(context,
		4,
		"bots/babe_c.c",
		"babe");

	BotAINode_ClearEnemyEntities();
	BotFindEnemy_PrepareEntity(2, 100.0f, 0.0f, 0.0f);
	aasworld.time = 120.0f;
	BotAINode_PrepareFrame(context,
		bot,
		BOT_AI_NODE_BATTLE_NBG,
		2,
		119.0f,
		75,
		true,
		false);
	context->api->BotEmptyGoalStack(bot->goal_handle);
	bot_goal_t nearby_goal = {0};
	nearby_goal.number = 71;
	nearby_goal.areanum = 1;
	nearby_goal.entitynum = 2;
	VectorSet(nearby_goal.origin, 100.0f, 0.0f, 0.0f);
	VectorSet(nearby_goal.mins, -8.0f, -8.0f, -8.0f);
	VectorSet(nearby_goal.maxs, 8.0f, 8.0f, 8.0f);
	assert_true(context->api->BotPushGoal(bot->goal_handle, &nearby_goal) != 0);
	bot->nearby_goal_time = aasworld.time - 0.001f;

	BotAINode_RunFrame(context);
	assert_int_equal(bot->ai_node, BOT_AI_NODE_BATTLE_FIGHT);
	assert_int_equal(context->api->BotGetTopGoal(bot->goal_handle, &nearby_goal),
		0);
}

/*
=============
test_ai_battle_nbg_uses_touch_without_static_item_visibility_completion

Pins the node-specific nearby-goal gate. A clear trace to an untouching static
item completes Seek NBG, but Battle NBG retains that same goal until contact.
=============
*/
static void test_ai_battle_nbg_uses_touch_without_static_item_visibility_completion(
	void **state)
{
	bot_interface_test_context_t *context =
		(bot_interface_test_context_t *)*state;
	bot_client_state_t *bot = BotFindEnemy_SetupHarness(context,
		4,
		"bots/babe_c.c",
		"babe");
	bot_goal_t long_term_goal = {0};
	bot_goal_t static_goal = {0};
	bot_goal_t top_goal = {0};
	vec3_t eye = {0.0f, 0.0f, 0.0f};
	vec3_t viewangles = {0.0f, 0.0f, 0.0f};

	long_term_goal.number = 760;
	long_term_goal.areanum = 1;
	VectorSet(long_term_goal.origin, 192.0f, 0.0f, 0.0f);
	VectorSet(long_term_goal.mins, -8.0f, -8.0f, -8.0f);
	VectorSet(long_term_goal.maxs, 8.0f, 8.0f, 8.0f);
	static_goal.number = 761;
	static_goal.entitynum = 0;
	static_goal.areanum = 1;
	static_goal.flags = GFL_ITEM;
	VectorSet(static_goal.origin, 128.0f, 0.0f, 0.0f);
	VectorSet(static_goal.mins, -8.0f, -8.0f, -8.0f);
	VectorSet(static_goal.maxs, 8.0f, 8.0f, 8.0f);

	BotAINode_ClearEnemyEntities();
	aasworld.time = 121.0f;
	BotAINode_PrepareFrame(context,
		bot,
		BOT_AI_NODE_SEEK_NBG,
		0,
		-1000.0f,
		100,
		true,
		false);
	assert_true(context->api->BotPushGoal(bot->goal_handle,
		&long_term_goal) != 0);
	assert_true(context->api->BotPushGoal(bot->goal_handle, &static_goal) != 0);
	bot->long_term_goal_time = aasworld.time + 20.0f;
	bot->nearby_goal_time = aasworld.time + 5.0f;
	assert_false(context->api->BotTouchingGoal(
		bot->last_client_update.origin,
		&static_goal));
	assert_true(context->api->BotItemGoalInVisButNotVisible(0,
		eye,
		viewangles,
		&static_goal));

	BotAINode_RunFrame(context);
	assert_int_equal(bot->ai_node, BOT_AI_NODE_SEEK_LTG);
	assert_true(context->api->BotGetTopGoal(bot->goal_handle, &top_goal) != 0);
	assert_int_equal(top_goal.number, long_term_goal.number);

	BotAINode_ClearEnemyEntities();
	BotFindEnemy_PrepareEntity(2, 100.0f, 0.0f, 0.0f);
	aasworld.time = 122.0f;
	BotAINode_PrepareFrame(context,
		bot,
		BOT_AI_NODE_BATTLE_NBG,
		2,
		121.0f,
		75,
		true,
		false);
	assert_true(context->api->BotPushGoal(bot->goal_handle, &static_goal) != 0);
	bot->nearby_goal_time = aasworld.time + 5.0f;
	assert_false(context->api->BotTouchingGoal(
		bot->last_client_update.origin,
		&static_goal));
	assert_true(context->api->BotItemGoalInVisButNotVisible(0,
		eye,
		viewangles,
		&static_goal));

	BotAINode_RunFrame(context);
	assert_int_equal(bot->ai_node, BOT_AI_NODE_BATTLE_NBG);
	memset(&top_goal, 0, sizeof(top_goal));
	assert_true(context->api->BotGetTopGoal(bot->goal_handle, &top_goal) != 0);
	assert_int_equal(top_goal.number, static_goal.number);
}

/*
=============
test_ai_battle_retreat_exits_to_chase_when_safe

Pins Battle Retreat's early exit: once its strict chase predicate passes, the
node discards the retained goal stack and starts a fresh Battle Chase.
=============
*/
static void test_ai_battle_retreat_exits_to_chase_when_safe(void **state)
{
	bot_interface_test_context_t *context =
		(bot_interface_test_context_t *)*state;
	bot_client_state_t *bot = BotFindEnemy_SetupHarness(context,
		4,
		"bots/babe_c.c",
		"babe");

	BotAINode_ClearEnemyEntities();
	BotFindEnemy_PrepareEntity(2, 100.0f, 0.0f, 0.0f);
	aasworld.time = 140.0f;
	BotAINode_PrepareFrame(context,
		bot,
		BOT_AI_NODE_BATTLE_RETREAT,
		2,
		139.0f,
		100,
		true,
		true);
	context->api->BotEmptyGoalStack(bot->goal_handle);
	bot_goal_t retreat_goal = {0};
	retreat_goal.number = 72;
	retreat_goal.areanum = 1;
	VectorSet(retreat_goal.origin, 200.0f, 0.0f, 0.0f);
	assert_true(context->api->BotPushGoal(bot->goal_handle, &retreat_goal) != 0);
	assert_false(BotAI_WantsToRetreat(bot));

	BotAINode_RunFrame(context);
	assert_int_equal(bot->ai_node, BOT_AI_NODE_BATTLE_CHASE);
	assert_float_equal(bot->combat.chase_time, 150.0f, 0.0001f);
	assert_int_equal(context->api->BotGetTopGoal(bot->goal_handle, &retreat_goal),
		0);
}

/*
=============
test_ai_battle_retreat_attempts_chase_despite_get_flag_when_aggressive

Pins the raw Battle Retreat handoff to WantsToChase rather than the logical
negation of WantsToRetreat: get-flag requests a retreat on acquisition, but
cannot suppress the later high-aggression Chase attempt.
=============
*/
static void test_ai_battle_retreat_attempts_chase_despite_get_flag_when_aggressive(void **state)
{
	bot_interface_test_context_t *context =
		(bot_interface_test_context_t *)*state;
	bot_client_state_t *bot = BotFindEnemy_SetupHarness(context,
		4,
		"bots/babe_c.c",
		"babe");

	BotAINode_ClearEnemyEntities();
	BotFindEnemy_PrepareEntity(2, 100.0f, 0.0f, 0.0f);
	aasworld.time = 142.0f;
	BotAINode_PrepareFrame(context,
		bot,
		BOT_AI_NODE_BATTLE_RETREAT,
		2,
		141.0f,
		100,
		true,
		true);
	bot_goal_t retreat_goal = {0};
	retreat_goal.number = 73;
	retreat_goal.areanum = 1;
	VectorSet(retreat_goal.origin, 200.0f, 0.0f, 0.0f);
	assert_true(context->api->BotPushGoal(bot->goal_handle, &retreat_goal) != 0);
	/* Get-flag is the recovered retail LTG type at interface-local value 4. */
	bot->ltg_type = 4;
	assert_true(BotAI_WantsToRetreat(bot));
	assert_true(BotAI_WantsToChase(bot));
	bot->combat.chase_time = 0.0f;

	BotAINode_RunFrame(context);
	/* The frame tail reselects Retreat because get-flag still requests it. */
	assert_int_equal(bot->ai_node, BOT_AI_NODE_BATTLE_RETREAT);
	assert_float_equal(bot->combat.chase_time, 152.0f, 0.0001f);
	assert_int_equal(context->api->BotGetTopGoal(bot->goal_handle, &retreat_goal),
		0);
}

/*
=============
test_ai_battle_retreat_without_goal_keeps_idle_view

Pins Battle Retreat's no-LTG return: retail remains in Battle Retreat and runs
the private view turn without fabricating a chat-stand transition, movement,
or general-item use.
=============
*/
static void test_ai_battle_retreat_without_goal_keeps_idle_view(void **state)
{
	bot_interface_test_context_t *context =
		(bot_interface_test_context_t *)*state;
	bot_client_state_t *bot = BotFindEnemy_SetupHarness(context,
		4,
		"bots/babe_c.c",
		"babe");
	vec3_t ideal_viewangles = {0.0f, 90.0f, 0.0f};

	BotAINode_ClearEnemyEntities();
	BotFindEnemy_PrepareEntity(2, 100.0f, 0.0f, 0.0f);
	aasworld.time = 145.0f;
	BotAINode_PrepareFrame(context,
		bot,
		BOT_AI_NODE_BATTLE_RETREAT,
		2,
		144.0f,
		30,
		false,
		false);
	assert_true(BotAI_WantsToRetreat(bot));
	AI_DMState_SetIdealViewAngles(bot->dm_state, ideal_viewangles);
	bot->last_client_update.inventory[RETAIL_INVENTORY_SILENCER] = 1;

	BotAINode_RunFrame(context);
	assert_int_equal(bot->ai_node, BOT_AI_NODE_BATTLE_RETREAT);
	assert_float_equal(context->mock.inputs[
		context->mock.bot_input_count - 1U].speed,
		0.0f,
		0.0001f);
	assert_true(context->mock.inputs[
		context->mock.bot_input_count - 1U].viewangles[YAW] > 0.0f);
	for (size_t command_index = 0;
		command_index < context->mock.client_command_count;
		++command_index)
	{
		assert_false(strcmp(context->mock.client_commands[command_index].command,
			"use") == 0 &&
			strcmp(context->mock.client_commands[command_index].argument,
				"Silencer") == 0);
	}
}

/*
=============
test_ai_battle_retreat_failed_move_preserves_nearby_lease

Pins Battle Retreat's direct-move failure against the shared nearby-goal
lease. Retail clears the retained LTG deadline while retaining that lease,
matching Battle Chase but differing from Battle NBG.
=============
*/
static void test_ai_battle_retreat_failed_move_preserves_nearby_lease(void **state)
{
	bot_interface_test_context_t *context =
		(bot_interface_test_context_t *)*state;
	bot_client_state_t *bot = BotFindEnemy_SetupHarness(context,
		4,
		"bots/babe_c.c",
		"babe");
	bot_goal_t retreat_goal = {0};

	BotAINode_ClearEnemyEntities();
	BotFindEnemy_PrepareEntity(2, 100.0f, 0.0f, 0.0f);
	aasworld.time = 147.0f;
	BotAINode_PrepareFrame(context,
		bot,
		BOT_AI_NODE_BATTLE_RETREAT,
		2,
		146.0f,
		30,
		false,
		false);
	bot->last_client_update.pm_flags = PMF_ON_GROUND;
	retreat_goal.number = 74;
	retreat_goal.areanum = 1;
	VectorSet(retreat_goal.origin, 200.0f, 0.0f, 0.0f);
	assert_true(context->api->BotPushGoal(bot->goal_handle, &retreat_goal) != 0);
	assert_true(BotAI_WantsToRetreat(bot));
	assert_false(BotAI_WantsToChase(bot));
	bot->long_term_goal_time = aasworld.time + 20.0f;
	bot->nearby_goal_time = aasworld.time + 5.0f;
	bot->nearby_goal_check_time = aasworld.time + 30.0f;

	BotAINode_RunFrame(context);
	assert_int_equal(bot->ai_node, BOT_AI_NODE_BATTLE_RETREAT);
	assert_true(bot->has_move_result);
	assert_true(bot->last_move_result.failure);
	assert_float_equal(bot->long_term_goal_time, 0.0f, 0.0001f);
	assert_float_equal(bot->nearby_goal_time, 152.0f, 0.0001f);
}

/*
=============
test_ai_lifecycle_nodes_gate_pmove_states

Pins the HLIL pmove lifecycle: observer and intermission reset level-local
state, intermission sends end/start-level chats at their distinct handoffs, a
no-chat death respawns strictly after its zero-delay deadline, and a selected death chat retains its
killer context through typing time before its one respawn action.
=============
*/
static void test_ai_lifecycle_nodes_gate_pmove_states(void **state)
{
	bot_interface_test_context_t *context =
		(bot_interface_test_context_t *)*state;
	bot_client_state_t *bot = BotFindEnemy_SetupHarness(context,
		4,
		"bots/babe_c.c",
		"babe");

	assert_int_equal(context->api->BotLibVarSet("nochat", "1"),
		BLERR_NOERROR);
	BotFindEnemy_PrepareEntity(2, 100.0f, 0.0f, 0.0f);
	BotAINode_PrepareFrame(context,
		bot,
		BOT_AI_NODE_BATTLE_FIGHT,
		2,
		4.0f,
		100,
		true,
		false);
	/*
	 * Retail's BotDeathmatchAI runs BotUpdateInventory (0x10028b15) and
	 * BotCheckConsoleMessages (0x10028b1b) before it so much as looks at the
	 * node, so an observer, dead or frozen frame still drains the console
	 * queue. Queuing more than ten entries also clears the sub-ten read-delay
	 * deferral, making the drain single-frame and exact.
	 */
	BotAINode_QueueConsoleMessages(context, bot, 12);
	bot->last_client_update.pm_type = PM_SPECTATOR;
	BotAINode_RunFrame(context);
	assert_int_equal(BotNumConsoleMessages(bot->chat_state), 0);
	assert_int_equal(context->mock.inputs[
		context->mock.bot_input_count - 1].actionflags,
		0);
	assert_int_equal(bot->combat.current_enemy, 0);
	assert_int_equal(bot->ai_node, BOT_AI_NODE_OBSERVER);
	assert_false(bot->client_update_valid);

	bot->client_update_valid = true;
	bot->last_client_update.pm_type = PM_DEAD;
	BotAINode_QueueConsoleMessages(context, bot, 12);
	bot->goal_snapshot_count = 2;
	bot->active_goal_number = 19;
	bot->combat.current_enemy = 2;
	bot->combat.last_enemy_area = 4;
	BotAINode_RunFrame(context);
	assert_int_equal(BotNumConsoleMessages(bot->chat_state), 0);
	assert_int_equal(context->mock.inputs[
		context->mock.bot_input_count - 1].actionflags,
		0);
	assert_true(bot->respawn_requested);
	assert_false(bot->respawn_action_sent);
	assert_int_equal(bot->goal_snapshot_count, 0);
	assert_int_equal(bot->active_goal_number, 0);
	assert_int_equal(bot->combat.current_enemy, 2);
	assert_int_equal(bot->combat.last_enemy_area, 0);

	aasworld.time += 0.001f;
	bot->client_update_valid = true;
	bot->last_client_update.pm_type = PM_GIB;
	BotAINode_RunFrame(context);
	assert_int_equal(context->mock.inputs[
		context->mock.bot_input_count - 1].actionflags,
		ACTION_RESPAWN);
	assert_true(bot->respawn_requested);
	assert_true(bot->respawn_action_sent);
	assert_int_equal(bot->combat.current_enemy, 0);

	BotAINode_ClearEnemyEntities();
	BotAINode_PushNearbyGoal(context, bot, 801);
	bot->long_term_goal_time = aasworld.time + 20.0f;
	bot->client_update_valid = true;
	bot->last_client_update.pm_type = PM_NORMAL;
	BotAINode_RunFrame(context);
	assert_false(bot->respawn_requested);
	assert_int_equal(bot->ai_node, BOT_AI_NODE_SEEK_LTG);
	assert_false(bot->has_move_result);

	assert_int_equal(context->api->BotLibVarSet("nochat", "0"),
		BLERR_NOERROR);
	assert_int_equal(context->api->BotLibVarSet("fastchat", "1"),
		BLERR_NOERROR);
	BotFindEnemy_PrepareEntity(2, 100.0f, 0.0f, 0.0f);
	aasworld.time = 10.0f;
	BotAINode_PrepareFrame(context,
		bot,
		BOT_AI_NODE_BATTLE_FIGHT,
		2,
		10.0f,
		100,
		true,
		false);
	bot->bot_death_type = 12;
	bot->last_client_update.pm_type = PM_DEAD;
	BotAINode_RunFrame(context);
	assert_int_equal(context->mock.inputs[
		context->mock.bot_input_count - 1].actionflags,
		0);
	assert_true(bot->respawn_requested);
	assert_false(bot->respawn_action_sent);
	assert_true(BotChatLength(bot->chat_state) > 0);
	assert_true(bot->respawn_time > aasworld.time);
	assert_int_equal(bot->combat.current_enemy, 2);

	aasworld.time = bot->respawn_time;
	bot->client_update_valid = true;
	bot->last_client_update.pm_type = PM_GIB;
	BotAINode_RunFrame(context);
	assert_int_equal(context->mock.inputs[
		context->mock.bot_input_count - 1].actionflags,
		0);
	assert_false(bot->respawn_action_sent);
	assert_true(BotChatLength(bot->chat_state) > 0);
	assert_int_equal(bot->combat.current_enemy, 2);

	aasworld.time += 0.001f;
	bot->client_update_valid = true;
	bot->last_client_update.pm_type = PM_GIB;
	BotAINode_RunFrame(context);
	assert_int_equal(context->mock.inputs[
		context->mock.bot_input_count - 1].actionflags,
		ACTION_RESPAWN);
	assert_true(bot->respawn_action_sent);
	assert_int_equal(BotChatLength(bot->chat_state), 0);
	assert_int_equal(bot->combat.current_enemy, 0);

	bot->client_update_valid = true;
	bot->last_client_update.pm_type = PM_NORMAL;
	BotAINode_RunFrame(context);
	assert_false(bot->respawn_requested);

	BotAINode_ClearEnemyEntities();
	aasworld.time = 20.0f;
	bot->enter_game_time = aasworld.time;
	bot->ai_node = BOT_AI_NODE_SEEK_LTG;
	bot->client_update_valid = true;
	bot->last_client_update.pm_type = PM_NORMAL;
	bot->last_client_update.pm_flags = PMF_ON_GROUND;
	context->mock.trace_blocked = false;
	BotAINode_RunFrame(context);
	assert_int_equal(bot->ai_node, BOT_AI_NODE_STAND);
	assert_true(bot->chat_standing);
	assert_true(BotChatLength(bot->chat_state) > 0);

	aasworld.time = bot->stand_time;
	bot->client_update_valid = true;
	bot->last_client_update.pm_type = PM_NORMAL;
	bot->last_client_update.pm_flags = PMF_ON_GROUND;
	BotAINode_RunFrame(context);
	assert_true(bot->chat_standing);
	assert_true(BotChatLength(bot->chat_state) > 0);

	aasworld.time += 0.001f;
	bot->client_update_valid = true;
	bot->last_client_update.pm_type = PM_NORMAL;
	bot->last_client_update.pm_flags = PMF_ON_GROUND;
	BotAINode_RunFrame(context);
	/* Retail retries the enter-game gate on every frame inside the setup-time
	 * window. A successful retry reconstructs the pending line and renews the
	 * Stand deadline; there is no successor-only "attempted" latch. */
	assert_true(bot->chat_standing);
	assert_true(BotChatLength(bot->chat_state) > 0);

	aasworld.time = fmaxf(bot->stand_time,
		bot->enter_game_time + 8.0f) + 0.001f;
	bot->client_update_valid = true;
	bot->last_client_update.pm_type = PM_NORMAL;
	bot->last_client_update.pm_flags = PMF_ON_GROUND;
	BotAINode_RunFrame(context);
	assert_false(bot->chat_standing);
	assert_int_equal(BotChatLength(bot->chat_state), 0);
	bot->enter_game_time = -1000.0f;

	bot->client_update_valid = true;
	bot->last_client_update.pm_type = PM_FREEZE;
	bot->ai_node = BOT_AI_NODE_BATTLE_FIGHT;
	bot->combat.current_enemy = 2;
	BotAINode_QueueConsoleMessages(context, bot, 12);
	BotAINode_RunFrame(context);
	assert_int_equal(BotNumConsoleMessages(bot->chat_state), 0);
	assert_int_equal(context->mock.inputs[
		context->mock.bot_input_count - 1].actionflags,
		0);
	assert_int_equal(bot->combat.current_enemy, 0);
	assert_int_equal(bot->ai_node, BOT_AI_NODE_INTERMISSION);

	bot->client_update_valid = true;
	bot->last_client_update.pm_type = PM_NORMAL;
	BotAINode_RunFrame(context);
	assert_int_equal(bot->ai_node, BOT_AI_NODE_STAND);
	assert_true(bot->chat_standing);
	assert_true(bot->stand_time > aasworld.time);
	assert_true(BotChatLength(bot->chat_state) > 0);

	aasworld.time = bot->stand_time;
	bot->client_update_valid = true;
	bot->last_client_update.pm_type = PM_NORMAL;
	BotAINode_RunFrame(context);
	assert_true(bot->chat_standing);
	assert_true(BotChatLength(bot->chat_state) > 0);

	aasworld.time += 0.001f;
	bot->client_update_valid = true;
	bot->last_client_update.pm_type = PM_NORMAL;
	BotAINode_RunFrame(context);
	assert_false(bot->chat_standing);
	assert_int_equal(BotChatLength(bot->chat_state), 0);

	assert_int_equal(context->api->BotLibVarSet("fastchat", "0"),
		BLERR_NOERROR);
}

/*
=============
test_ai_seek_ltg_random_chat_gates

Pins sub_10022470's random-chat handoff before Seek-LTG acquires an enemy or
selects a goal, including its ordered team-goal exclusions.
=============
*/
static void test_ai_seek_ltg_random_chat_gates(void **state)
{
	bot_interface_test_context_t *context =
		(bot_interface_test_context_t *)*state;
	bot_client_state_t *bot = BotFindEnemy_SetupHarness(context,
		4,
		"bots/babe_c.c",
		"babe");

	assert_int_equal(context->api->BotLibVarSet("nochat", "0"),
		BLERR_NOERROR);
	assert_int_equal(context->api->BotLibVarSet("fastchat", "1"),
		BLERR_NOERROR);
	BotAINode_ClearEnemyEntities();
	aasworld.time = 160.0f;
	BotAINode_PrepareFrame(context,
		bot,
		BOT_AI_NODE_SEEK_LTG,
		0,
		-1000.0f,
		100,
		true,
		false);
	bot->last_client_update.pm_flags = PMF_ON_GROUND;
	BotAINode_RunFrameWithThinktime(context, 10.0f);
	assert_int_equal(bot->ai_node, BOT_AI_NODE_STAND);
	assert_true(bot->chat_standing);
	assert_true(bot->stand_time > aasworld.time);
	assert_true(BotChatLength(bot->chat_state) > 0);

	(void)BotEnterChat(bot->chat_state, bot->client_number, 0);
	assert_int_equal(BotChatLength(bot->chat_state), 0);
	bot->chat_standing = false;
	bot->ai_node = BOT_AI_NODE_SEEK_LTG;
	bot->ltg_type = 1;
	bot->client_update_valid = true;
	BotAINode_RunFrameWithThinktime(context, 10.0f);
	assert_int_equal(bot->ai_node, BOT_AI_NODE_SEEK_LTG);
	assert_false(bot->chat_standing);

	assert_int_equal(context->api->BotLibVarSet("fastchat", "0"),
		BLERR_NOERROR);
}

/*
=============
test_ai_seek_ltg_waves_after_recent_enemy_death

Pins sub_1001f760's strict five-second post-death wave trial before Seek-LTG
attempts its next enemy acquisition.
=============
*/
static void test_ai_seek_ltg_waves_after_recent_enemy_death(void **state)
{
	bot_interface_test_context_t *context =
		(bot_interface_test_context_t *)*state;
	bot_client_state_t *bot = BotFindEnemy_SetupHarness(context,
		4,
		"bots/babe_c.c",
		"babe");

	assert_int_equal(context->api->BotLibVarSet("nochat", "1"),
		BLERR_NOERROR);
	assert_int_equal(context->api->BotLibVarSet("ctf", "0"), BLERR_NOERROR);
	BotAINode_ClearEnemyEntities();
	aasworld.time = 200.0f;
	BotAINode_PrepareFrame(context,
		bot,
		BOT_AI_NODE_SEEK_LTG,
		0,
		-1000.0f,
		100,
		true,
		false);
	bot->combat.enemy_death_time = aasworld.time - 4.999f;
	Mock_Reset(&context->mock);
	BotAINode_RunFrameWithThinktime(context, 1.0f);
	size_t wave_count = 0U;
	for (size_t index = 0; index < context->mock.client_command_count; ++index)
	{
		if (strcmp(context->mock.client_commands[index].command, "wave") != 0)
		{
			continue;
		}

		assert_int_equal(context->mock.client_commands[index].client,
			bot->client_number);
		assert_true(strcmp(context->mock.client_commands[index].argument, "0") == 0 ||
			strcmp(context->mock.client_commands[index].argument, "2") == 0);
		++wave_count;
	}
	assert_int_equal(wave_count, 1U);

	aasworld.time = 205.0f;
	BotAINode_PrepareFrame(context,
		bot,
		BOT_AI_NODE_SEEK_LTG,
		0,
		-1000.0f,
		100,
		true,
		false);
	bot->combat.enemy_death_time = aasworld.time - 5.0f;
	Mock_Reset(&context->mock);
	BotAINode_RunFrameWithThinktime(context, 1.0f);
	for (size_t index = 0; index < context->mock.client_command_count; ++index)
	{
		assert_string_not_equal(context->mock.client_commands[index].command,
			"wave");
	}
}

/*
=============
test_ai_seek_ltg_nearby_goal_schedule

Pins Seek-LTG's strict half-second NBG probe, same-frame Seek-NBG handoff,
general-item-use ordering, five-second NBG lifetime, and return to its
underlying long-term goal after expiry.
=============
*/
static void test_ai_seek_ltg_nearby_goal_schedule(void **state)
{
	bot_interface_test_context_t *context =
		(bot_interface_test_context_t *)*state;
	bot_client_state_t *bot = BotFindEnemy_SetupHarness(context,
		4,
		"bots/babe_c.c",
		"babe");
	bot_goal_t long_term_goal = {0};
	bot_goal_t retained_goal = {0};
	bot_goal_t top_goal = {0};

	setup_console_command_aas_world();
	register_console_command_goal(context, "weapon_rocketlauncher", 730, 64.0f);
	assert_int_equal(context->api->BotLibVarSet("nochat", "1"),
		BLERR_NOERROR);
	BotAINode_ClearEnemyEntities();
	aasworld.time = 600.0f;
	BotAINode_PrepareFrame(context,
		bot,
		BOT_AI_NODE_SEEK_LTG,
		0,
		-1000.0f,
		100,
		true,
		false);
	long_term_goal.number = 731;
	long_term_goal.areanum = 1;
	VectorSet(long_term_goal.origin, 112.0f, 0.0f, 32.0f);
	VectorSet(long_term_goal.mins, -8.0f, -8.0f, -8.0f);
	VectorSet(long_term_goal.maxs, 8.0f, 8.0f, 8.0f);
	retained_goal.number = 729;
	retained_goal.areanum = 1;
	VectorSet(retained_goal.origin, 128.0f, 0.0f, 32.0f);
	VectorSet(retained_goal.mins, -8.0f, -8.0f, -8.0f);
	VectorSet(retained_goal.maxs, 8.0f, 8.0f, 8.0f);
	assert_true(context->api->BotPushGoal(bot->goal_handle, &retained_goal) != 0);
	assert_true(context->api->BotPushGoal(bot->goal_handle, &long_term_goal) != 0);
	bot->long_term_goal_time = aasworld.time + 20.0f;
	bot->nearby_goal_check_time = aasworld.time;
	bot->last_client_update.inventory[RETAIL_INVENTORY_SILENCER] = 1;

	BotAINode_RunFrame(context);
	assert_int_equal(bot->ai_node, BOT_AI_NODE_SEEK_LTG);
	assert_float_equal(bot->nearby_goal_check_time, 600.0f, 0.0001f);
	bool used_silencer = false;
	for (size_t command_index = 0;
		command_index < context->mock.client_command_count;
		++command_index)
	{
		if (strcmp(context->mock.client_commands[command_index].command,
			"use") == 0 &&
			strcmp(context->mock.client_commands[command_index].argument,
				"Silencer") == 0)
		{
			used_silencer = true;
			break;
		}
	}
	assert_true(used_silencer);
	assert_true(context->api->BotGetTopGoal(bot->goal_handle, &top_goal) != 0);
	assert_int_equal(top_goal.number, long_term_goal.number);

	aasworld.time = 600.001f;
	bot->client_update_valid = true;
	Mock_Reset(&context->mock);
	bot->has_move_result = false;
	memset(&bot->last_move_result, 0, sizeof(bot->last_move_result));
	bot->last_client_update.inventory[RETAIL_INVENTORY_SILENCER] = 1;
	BotAINode_RunFrame(context);
	assert_int_equal(bot->ai_node, BOT_AI_NODE_SEEK_NBG);
	assert_float_equal(bot->nearby_goal_check_time, 600.501f, 0.0001f);
	assert_float_equal(bot->nearby_goal_time, 605.001f, 0.0001f);
	assert_true(bot->has_move_result);
	assert_int_equal(bot->active_goal_number, 730);
	assert_int_equal(bot->ai_node_switches, 1);
	assert_false(bot->ai_node_overflow);
	assert_true(context->mock.bot_input_count > 0U);
	used_silencer = false;
	for (size_t command_index = 0;
		command_index < context->mock.client_command_count;
		++command_index)
	{
		if (strcmp(context->mock.client_commands[command_index].command,
			"use") == 0 &&
			strcmp(context->mock.client_commands[command_index].argument,
				"Silencer") == 0)
		{
			used_silencer = true;
			break;
		}
	}
	assert_true(used_silencer);
	assert_true(context->api->BotGetTopGoal(bot->goal_handle, &top_goal) != 0);
	assert_int_equal(top_goal.number, 730);

	aasworld.time = 605.002f;
	bot->client_update_valid = true;
	BotAINode_RunFrame(context);
	assert_int_equal(bot->ai_node, BOT_AI_NODE_SEEK_LTG);
	assert_float_equal(bot->nearby_goal_check_time, 605.502f, 0.0001f);
	assert_true(context->api->BotGetTopGoal(bot->goal_handle, &top_goal) != 0);
	assert_int_equal(top_goal.number, long_term_goal.number);

	/*
	 * The twenty second item lease expires here, so BotLongTermGoal pops the
	 * old goal and reselects.  The rocket launcher is not a candidate yet: the
	 * nearby-goal selection at t = 600.001 armed its avoid slot, and both
	 * selectors use the item's respawn time - 30 seconds, since this fixture
	 * leaves respawntime zero and retail substitutes 30 for that
	 * (be_ai_goal.c BotChooseLTGItem/BotChooseNBGItem, dll 1002FEB0/10030260).
	 * With every level item avoided, retail falls through to its
	 * AAS_RandomGoalArea roam goal, which carries number 0 and GFL_ROAM.
	 */
	aasworld.time = 620.001f;
	bot->client_update_valid = true;
	BotAINode_RunFrame(context);
	assert_true(context->api->BotGetTopGoal(bot->goal_handle, &top_goal) != 0);
	assert_int_equal(top_goal.number, 0);
	assert_int_equal(top_goal.flags & GFL_ROAM, GFL_ROAM);
	assert_float_equal(bot->long_term_goal_time, 640.001f, 0.0001f);

	/*
	 * Once that avoid slot lapses at t = 630.001 the item is a candidate again,
	 * so the next lease expiry commits it as the ordinary long-term goal.
	 */
	aasworld.time = 660.002f;
	bot->client_update_valid = true;
	BotAINode_RunFrame(context);
	assert_true(context->api->BotGetTopGoal(bot->goal_handle, &top_goal) != 0);
	assert_int_equal(top_goal.number, 730);
}

/*
=============
test_ai_seek_ltg_failed_reselection_preserves_lower_stack

Pins the failed-selector branch independently of roam/item success: expiring
the top LTG pops only that entry and leaves the older stack goal available.
=============
*/
static void test_ai_seek_ltg_failed_reselection_preserves_lower_stack(void **state)
{
	bot_interface_test_context_t *context =
		(bot_interface_test_context_t *)*state;
	bot_client_state_t *bot = BotFindEnemy_SetupHarness(context,
		4,
		"bots/babe_c.c",
		"babe");
	bot_goal_t retained_goal = {0};
	bot_goal_t expired_goal = {0};
	bot_goal_t top_goal = {0};

	setup_console_command_aas_world();
	BotAINode_ClearEnemyEntities();
	aasworld.time = 606.0f;
	BotAINode_PrepareFrame(context,
		bot,
		BOT_AI_NODE_SEEK_LTG,
		0,
		-1000.0f,
		100,
		true,
		false);
	retained_goal.number = 771;
	retained_goal.areanum = 1;
	VectorSet(retained_goal.origin, 192.0f, 0.0f, 32.0f);
	VectorSet(retained_goal.mins, -8.0f, -8.0f, -8.0f);
	VectorSet(retained_goal.maxs, 8.0f, 8.0f, 8.0f);
	expired_goal.number = 772;
	expired_goal.areanum = 1;
	VectorSet(expired_goal.origin, 128.0f, 0.0f, 32.0f);
	VectorSet(expired_goal.mins, -8.0f, -8.0f, -8.0f);
	VectorSet(expired_goal.maxs, 8.0f, 8.0f, 8.0f);
	assert_true(context->api->BotPushGoal(bot->goal_handle,
		&retained_goal) != 0);
	assert_true(context->api->BotPushGoal(bot->goal_handle, &expired_goal) != 0);
	bot->long_term_goal_time = aasworld.time - 0.001f;
	bot->nearby_goal_check_time = aasworld.time + 10.0f;
	aasworld.areasettings[1].numreachableareas = 0;

	BotAINode_RunFrame(context);
	assert_int_equal(bot->ai_node, BOT_AI_NODE_SEEK_LTG);
	assert_true(context->api->BotGetTopGoal(bot->goal_handle, &top_goal) != 0);
	assert_int_equal(top_goal.number, retained_goal.number);
}

/*
=============
test_ai_seek_nbg_failed_move_preserves_ltg_deadline

Pins Seek-NBG's failed-move result: it clears only the five-second nearby
lease, retaining the underlying long-term-item deadline for the fallback.
=============
*/
static void test_ai_seek_nbg_failed_move_preserves_ltg_deadline(void **state)
{
	bot_interface_test_context_t *context =
		(bot_interface_test_context_t *)*state;
	bot_client_state_t *bot = BotFindEnemy_SetupHarness(context,
		4,
		"bots/babe_c.c",
		"babe");
	bot_goal_t long_term_goal = {0};
	bot_goal_t nearby_goal = {0};

	BotAINode_ClearEnemyEntities();
	aasworld.time = 610.0f;
	BotAINode_PrepareFrame(context,
		bot,
		BOT_AI_NODE_SEEK_NBG,
		0,
		-1000.0f,
		100,
		true,
		true);
	bot->last_client_update.pm_flags = PMF_ON_GROUND;

	long_term_goal.number = 741;
	long_term_goal.areanum = 1;
	VectorSet(long_term_goal.origin, 256.0f, 0.0f, 32.0f);
	VectorSet(long_term_goal.mins, -8.0f, -8.0f, -8.0f);
	VectorSet(long_term_goal.maxs, 8.0f, 8.0f, 8.0f);
	nearby_goal.number = 742;
	nearby_goal.areanum = 1;
	VectorSet(nearby_goal.origin, 64.0f, 0.0f, 32.0f);
	VectorSet(nearby_goal.mins, -8.0f, -8.0f, -8.0f);
	VectorSet(nearby_goal.maxs, 8.0f, 8.0f, 8.0f);
	assert_true(context->api->BotPushGoal(bot->goal_handle, &long_term_goal) != 0);
	assert_true(context->api->BotPushGoal(bot->goal_handle, &nearby_goal) != 0);
	bot->long_term_goal_time = aasworld.time + 20.0f;
	bot->nearby_goal_time = aasworld.time + 5.0f;

	BotAINode_RunFrame(context);
	assert_int_equal(bot->ai_node, BOT_AI_NODE_SEEK_NBG);
	assert_true(bot->has_move_result);
	assert_true(bot->last_move_result.failure);
	assert_float_equal(bot->nearby_goal_time, 0.0f, 0.0001f);
	assert_float_equal(bot->long_term_goal_time, 630.0f, 0.0001f);
}

/*
=============
test_ai_battle_chase_failed_move_preserves_nearby_lease

Pins Battle Chase's distinct failed-move clock reset.  The recovered result
path clears the retained LTG deadline while leaving a pre-existing nearby-item
lease intact; it must not reuse Seek-NBG's nearby-lease failure behaviour.
=============
*/
static void test_ai_battle_chase_failed_move_preserves_nearby_lease(void **state)
{
	bot_interface_test_context_t *context =
		(bot_interface_test_context_t *)*state;
	bot_client_state_t *bot = BotFindEnemy_SetupHarness(context,
		4,
		"bots/babe_c.c",
		"babe");

	BotAINode_ClearEnemyEntities();
	aasworld.time = 640.0f;
	BotAINode_PrepareFrame(context,
		bot,
		BOT_AI_NODE_BATTLE_CHASE,
		2,
		-1000.0f,
		100,
		true,
		true);
	bot->last_client_update.pm_flags = PMF_ON_GROUND;
	bot->long_term_goal_time = aasworld.time + 20.0f;
	bot->nearby_goal_time = aasworld.time + 5.0f;
	bot->nearby_goal_check_time = aasworld.time + 30.0f;

	BotAINode_RunFrame(context);
	assert_int_equal(bot->ai_node, BOT_AI_NODE_BATTLE_CHASE);
	assert_true(bot->has_move_result);
	assert_true(bot->last_move_result.failure);
	assert_float_equal(bot->long_term_goal_time, 0.0f, 0.0001f);
	assert_float_equal(bot->nearby_goal_time, 645.0f, 0.0001f);
}

/*
=============
test_ai_reached_ctf_tech_drops_conflicting_rune

Pins sub_100262c0's goal-contact tech drop: both CTF/runes gates apply, and
the raw tech-four Haste exception is intentionally retained.
=============
*/
static void test_ai_reached_ctf_tech_drops_conflicting_rune(void **state)
{
	bot_interface_test_context_t *context =
		(bot_interface_test_context_t *)*state;
	bot_client_state_t *bot = BotFindEnemy_SetupHarness(context,
		4,
		"bots/babe_c.c",
		"babe");
	char *models[] = {
		"maps/tech_test.bsp",
		"models/ctf/resistance/tris.md2",
		"models/ctf/strength/tris.md2",
		"models/ctf/haste/tris.md2",
		"models/ctf/regeneration/tris.md2",
	};
	const struct
	{
		const char *ctf;
		const char *runes;
		int modelindex;
		int held_tech;
		bool expect_drop;
	} cases[] = {
		{"1", "1", 2, RETAIL_INVENTORY_TECH1, true},
		{"1", "1", 3, RETAIL_INVENTORY_TECH4, false},
		{"1", "1", 4, RETAIL_INVENTORY_TECH4, true},
		{"1", "0", 2, RETAIL_INVENTORY_TECH1, false},
		{"0", "1", 2, RETAIL_INVENTORY_TECH1, false},
	};

	assert_int_equal(context->api->BotLoadMap(NULL,
		(int)ARRAY_LEN(models),
		models,
		0,
		NULL,
		0,
		NULL),
		BLERR_NOERROR);
	setup_console_command_aas_world();

	for (size_t index = 0; index < ARRAY_LEN(cases); ++index)
	{
		bot_updateentity_t tech_entity = {0};
		bot_goal_t goal = {0};
		tech_entity.modelindex = cases[index].modelindex;
		assert_int_equal(context->api->BotUpdateEntity(12, &tech_entity),
			BLERR_NOERROR);
		assert_int_equal(context->api->BotLibVarSet("ctf",
			(char *)cases[index].ctf),
			BLERR_NOERROR);
		assert_int_equal(context->api->BotLibVarSet("runes",
			(char *)cases[index].runes),
			BLERR_NOERROR);

		BotAINode_ClearEnemyEntities();
		aasworld.time = 630.0f + (float)index;
		BotAINode_PrepareFrame(context,
			bot,
			BOT_AI_NODE_SEEK_NBG,
			0,
			-1000.0f,
			100,
			true,
			false);
		goal.entitynum = 12;
		goal.number = 740 + (int)index;
		goal.areanum = 1;
		goal.flags = GFL_ITEM;
		VectorSet(goal.mins, -8.0f, -8.0f, -8.0f);
		VectorSet(goal.maxs, 8.0f, 8.0f, 8.0f);
		assert_true(context->api->BotPushGoal(bot->goal_handle, &goal) != 0);
		bot->nearby_goal_time = aasworld.time + 5.0f;
		bot->last_client_update.inventory[cases[index].held_tech] = 1;

		Mock_Reset(&context->mock);
		BotAINode_RunFrame(context);
		bool dropped_tech = false;
		for (size_t command_index = 0;
			command_index < context->mock.client_command_count;
			++command_index)
		{
			if (strcmp(context->mock.client_commands[command_index].command,
				"drop") == 0 &&
				strcmp(context->mock.client_commands[command_index].argument,
					"tech") == 0)
			{
				dropped_tech = true;
				break;
			}
		}
		assert_int_equal(dropped_tech, cases[index].expect_drop);
	}
}

/*
=============
test_ai_seek_nbg_clears_missing_item_goal

Pins the item-specific Seek-NBG exit when a static item should be visible but
its stale entity record proves it has disappeared, including the immediate
return to Seek-LTG's normal half-second NBG probe.
=============
*/
static void test_ai_seek_nbg_clears_missing_item_goal(void **state)
{
	bot_interface_test_context_t *context =
		(bot_interface_test_context_t *)*state;
	bot_client_state_t *bot = BotFindEnemy_SetupHarness(context,
		4,
		"bots/babe_c.c",
		"babe");
	bot_goal_t long_term_goal = {0};
	bot_goal_t nearby_goal = {0};
	bot_goal_t top_goal = {0};

	setup_console_command_aas_world();
	BotAINode_ClearEnemyEntities();
	aasworld.time = 620.0f;
	BotAINode_PrepareFrame(context,
		bot,
		BOT_AI_NODE_SEEK_NBG,
		0,
		-1000.0f,
		100,
		true,
		false);
	long_term_goal.number = 732;
	long_term_goal.areanum = 1;
	VectorSet(long_term_goal.origin, 112.0f, 0.0f, 32.0f);
	VectorSet(long_term_goal.mins, -8.0f, -8.0f, -8.0f);
	VectorSet(long_term_goal.maxs, 8.0f, 8.0f, 8.0f);
	nearby_goal.number = 3;
	nearby_goal.entitynum = 3;
	nearby_goal.areanum = 1;
	nearby_goal.flags = GFL_ITEM;
	VectorSet(nearby_goal.origin, 64.0f, 0.0f, 32.0f);
	VectorSet(nearby_goal.mins, -8.0f, -8.0f, -8.0f);
	VectorSet(nearby_goal.maxs, 8.0f, 8.0f, 8.0f);
	assert_true(context->api->BotPushGoal(bot->goal_handle, &long_term_goal) != 0);
	assert_true(context->api->BotPushGoal(bot->goal_handle, &nearby_goal) != 0);
	bot->long_term_goal_time = aasworld.time + 20.0f;
	bot->nearby_goal_time = aasworld.time + 5.0f;

	BotAINode_RunFrame(context);
	assert_int_equal(bot->ai_node, BOT_AI_NODE_SEEK_LTG);
	assert_float_equal(bot->nearby_goal_check_time, 620.5f, 0.0001f);
	assert_true(context->api->BotGetTopGoal(bot->goal_handle, &top_goal) != 0);
	assert_int_equal(top_goal.number, long_term_goal.number);
}

/*
=============
test_ai_team_goal_scheduler_direct_branches

Pins BotLongTermGoal's direct defend, camp, and patrol branches: their delayed
acknowledgements, strict expiry, nearby camp arrival/crouch hold, and patrol
direction bit must run before ordinary item-goal selection. Defend's ordinary
move result also advances the retained no-enemy private view turn; the defend
frame stands the bot on ground and shares the goal's area so the move result
carries a written movedir, because retail leaves that field undefined when
BotMoveToGoal reaches no travel branch.
=============
*/
static void test_ai_team_goal_scheduler_direct_branches(void **state)
{
	bot_interface_test_context_t *context =
		(bot_interface_test_context_t *)*state;
	bot_client_state_t *bot = BotFindEnemy_SetupHarness(context,
		4,
		"bots/babe_c.c",
		"babe");

	assert_int_equal(context->api->BotLibVarSet("nochat", "1"),
		BLERR_NOERROR);
	BotAINode_ClearEnemyEntities();
	aasworld.time = 180.0f;
	BotAINode_PrepareFrame(context,
		bot,
		BOT_AI_NODE_SEEK_LTG,
		0,
		-1000.0f,
		100,
		true,
		false);
	bot->ltg_type = 3;
	bot->team_goal_number = 401;
	bot->team_goal.number = 401;
	/* Retail's BotClearMoveResult (0x10031e26) clears only the 0x18-byte
	   status prefix, so bot_moveresult_t::movedir stays uninitialised whenever
	   BotMoveToGoal reaches no travel branch -- and the private view turn below
	   reads exactly that field. Standing the bot on ground and giving the team
	   goal the bot's own (fixture-free) area number 0 sends BotMoveToGoal into
	   BotMoveInGoalArea, which writes movedir = normalize(goal - origin). */
	bot->team_goal.areanum = 0;
	VectorSet(bot->team_goal.origin, 200.0f, 0.0f, 0.0f);
	VectorSet(bot->team_goal.mins, -8.0f, -8.0f, -8.0f);
	VectorSet(bot->team_goal.maxs, 8.0f, 8.0f, 8.0f);
	bot->team_message_time = 179.0f;
	bot->team_goal_time = 190.0f;
	bot->last_client_update.pm_flags |= PMF_ON_GROUND;
	vec3_t ninety_degree_delta = {0.0f, 90.0f, 0.0f};
	AI_DMState_ApplyDeltaAngles(bot->dm_state, ninety_degree_delta);
	BotAINode_RunFrame(context);
	assert_float_equal(bot->team_message_time, 0.0f, 0.0001f);
	assert_int_equal(bot->active_goal_number, 401);
	assert_true(bot->has_move_result);
	/* movedir = (1,0,0) -> ideal yaw 0; with no enemy the private turn runs at
	   the no-enemy literal 100 deg/s, i.e. one 10-degree step away from the
	   seeded 90, quantised by AI_DMAngleMod. */
	assert_float_equal(context->mock.inputs[
		context->mock.bot_input_count - 1U].viewangles[YAW],
		80.00244f,
		0.01f);

	bot->last_client_update.pm_flags = 0;
	aasworld.time = 190.001f;
	bot->client_update_valid = true;
	BotAINode_RunFrame(context);
	assert_int_equal(bot->ltg_type, 0);

	aasworld.time = 200.0f;
	bot->client_update_valid = true;
	bot->ai_node = BOT_AI_NODE_SEEK_LTG;
	bot->ltg_type = 6;
	bot->ltg_teammate = 0;
	bot->team_goal.number = 402;
	bot->team_goal.areanum = 1;
	VectorClear(bot->team_goal.origin);
	VectorSet(bot->team_goal.mins, -8.0f, -8.0f, -8.0f);
	VectorSet(bot->team_goal.maxs, 8.0f, 8.0f, 8.0f);
	bot->team_message_time = 199.0f;
	bot->team_goal_time = 210.0f;
	bot->arrive_time = 0.0f;
	/* Retail 0x1001e2a5 ends the nearby-camp epilogue with BotResetAvoidReach
	   (sub_10034af0), which clears the whole table, not the single-slot
	   sub_10034b20 that only expires the newest entry and decrements its
	   retry count. */
	bot_movestate_t *camp_movestate = BotMoveStateFromHandle(bot->move_handle);
	assert_non_null(camp_movestate);
	camp_movestate->avoidreach[0] = 73;
	camp_movestate->avoidreachtimes[0] = aasworld.time + 10.0f;
	camp_movestate->avoidreachtries[0] = 5;
	BotAINode_RunFrame(context);
	assert_float_equal(bot->team_message_time, 0.0f, 0.0001f);
	assert_float_equal(bot->arrive_time, 200.0f, 0.0001f);
	assert_int_equal(camp_movestate->avoidreach[0], 0);
	assert_float_equal(camp_movestate->avoidreachtimes[0], 0.0f, 0.0001f);
	assert_int_equal(camp_movestate->avoidreachtries[0], 0);

	aasworld.time = 203.0f;
	bot->client_update_valid = true;
	AI_DMState_SetAttackCrouchTime(bot->dm_state, 210.0f);
	srand(1);
	BotAINode_RunFrame(context);
	assert_true((context->mock.inputs[
		context->mock.bot_input_count - 1U].actionflags & ACTION_CROUCH) != 0);

	bot_console_waypoint_t *first = allocate_test_console_waypoint("First");
	bot_console_waypoint_t *second = allocate_test_console_waypoint("Second");
	first->goal.number = 403;
	first->goal.areanum = 1;
	VectorClear(first->goal.origin);
	VectorSet(first->goal.mins, -8.0f, -8.0f, -8.0f);
	VectorSet(first->goal.maxs, 8.0f, 8.0f, 8.0f);
	second->goal.number = 404;
	second->goal.areanum = 1;
	VectorSet(second->goal.origin, 200.0f, 0.0f, 0.0f);
	VectorSet(second->goal.mins, -8.0f, -8.0f, -8.0f);
	VectorSet(second->goal.maxs, 8.0f, 8.0f, 8.0f);
	first->next = second;
	second->prev = first;
	bot->patrol_points = first;
	bot->current_patrol_point = first;
	bot->patrol_flags = 0;
	bot->ltg_type = 7;
	bot->team_message_time = 199.0f;
	bot->team_goal_time = 220.0f;
	bot->client_update_valid = true;
	VectorClear(bot->last_client_update.origin);
	BotAINode_RunFrame(context);
	assert_float_equal(bot->team_message_time, 0.0f, 0.0001f);
	assert_ptr_equal(bot->current_patrol_point, second);
	assert_int_equal(bot->patrol_flags & 0x04, 0x04);
	assert_int_equal(bot->active_goal_number, 404);

	aasworld.time = 220.001f;
	bot->client_update_valid = true;
	VectorCopy(second->goal.origin, bot->last_client_update.origin);
	BotAINode_RunFrame(context);
	assert_int_equal(bot->ltg_type, 0);
	assert_ptr_equal(bot->current_patrol_point, first);

	BotState_FreeConsoleWaypoints(bot->patrol_points);
	bot->patrol_points = NULL;
	bot->current_patrol_point = NULL;

	/*
	 * Defend inside seventy units takes the same whole-table reset as camp:
	 * retail 0x1001df88 calls BotResetAvoidReach (sub_10034af0), while the
	 * single-slot sub_10034b20 would leave the reachability number in place
	 * and merely decrement its retry count.
	 */
	aasworld.time = 230.0f;
	bot->client_update_valid = true;
	bot->ai_node = BOT_AI_NODE_SEEK_LTG;
	bot->ltg_type = 3;
	bot->team_goal_number = 401;
	bot->team_goal.number = 401;
	bot->team_goal.areanum = 1;
	VectorSet(bot->team_goal.origin, 32.0f, 0.0f, 0.0f);
	bot->team_message_time = 0.0f;
	bot->team_goal_time = 260.0f;
	bot->defend_away_time = 0.0f;
	VectorClear(bot->last_client_update.origin);
	bot_movestate_t *defend_movestate = BotMoveStateFromHandle(bot->move_handle);
	assert_non_null(defend_movestate);
	defend_movestate->avoidreach[0] = 73;
	defend_movestate->avoidreachtimes[0] = aasworld.time + 10.0f;
	defend_movestate->avoidreachtries[0] = 5;
	BotAINode_RunFrame(context);
	assert_int_equal(bot->ltg_type, 3);
	assert_int_equal(defend_movestate->avoidreach[0], 0);
	assert_float_equal(defend_movestate->avoidreachtimes[0], 0.0f, 0.0001f);
	assert_int_equal(defend_movestate->avoidreachtries[0], 0);
	assert_true(bot->defend_away_time >= 235.0f);
	assert_true(bot->defend_away_time <= 245.0f);

	/*
	 * Camp's liquid abort samples the eye (retail 0x1001e275 reads bs->eye at
	 * arg1 + 0x6b0, written as origin + view_offset by 0x100289ec), not the
	 * origin the AAS_Swimming test just above it uses. Feet in water with a
	 * dry eye must therefore leave the camp order standing.
	 */
	aasworld.time = 270.0f;
	bot->client_update_valid = true;
	bot->ai_node = BOT_AI_NODE_SEEK_LTG;
	bot->ltg_type = 6;
	bot->ltg_teammate = 0;
	bot->team_goal.number = 402;
	bot->team_goal.areanum = 1;
	VectorClear(bot->team_goal.origin);
	bot->team_message_time = 0.0f;
	bot->team_goal_time = 320.0f;
	bot->arrive_time = 270.0f;
	VectorClear(bot->last_client_update.origin);
	VectorSet(bot->last_client_update.viewoffset, 0.0f, 0.0f, 22.0f);
	context->mock.point_contents_height_split = true;
	context->mock.point_contents_split_z = 10.0f;
	context->mock.point_contents_result = CONTENTS_WATER;
	context->mock.point_contents_result_above = 0;
	context->mock.point_contents_count = 0U;
	memset(context->mock.point_contents_points,
		0,
		sizeof(context->mock.point_contents_points));
	BotAINode_RunFrame(context);
	assert_int_equal(bot->ltg_type, 6);
	bool sampled_camp_eye = false;
	bool sampled_camp_feet = false;
	for (size_t index = 0;
		index < context->mock.point_contents_count &&
			index < ARRAY_LEN(context->mock.point_contents_points);
		++index)
	{
		const float z = context->mock.point_contents_points[index][2];
		if (z >= 21.9f && z <= 22.1f)
		{
			sampled_camp_eye = true;
		}
		if (z >= -2.1f && z <= -1.9f)
		{
			sampled_camp_feet = true;
		}
	}
	assert_true(sampled_camp_eye);
	assert_true(sampled_camp_feet);

	/* Mirror case: a dry floor with the eye submerged does abort the camp. */
	aasworld.time = 271.0f;
	bot->client_update_valid = true;
	bot->ai_node = BOT_AI_NODE_SEEK_LTG;
	bot->ltg_type = 6;
	bot->team_message_time = 0.0f;
	bot->arrive_time = 271.0f;
	context->mock.point_contents_result = 0;
	context->mock.point_contents_result_above = CONTENTS_WATER;
	BotAINode_RunFrame(context);
	assert_int_equal(bot->ltg_type, 0);

	context->mock.point_contents_height_split = false;
	context->mock.point_contents_result = 0;
	context->mock.point_contents_result_above = 0;
	VectorClear(bot->last_client_update.viewoffset);
}

/*
=============
test_ai_team_help_scheduler_tracks_live_teammate

Pins LTG help's delayed acknowledgement, live one-based entity goal refresh,
and strict ten-second visibility expiry before the generic goal path runs.
=============
*/
static void test_ai_team_help_scheduler_tracks_live_teammate(void **state)
{
	bot_interface_test_context_t *context =
		(bot_interface_test_context_t *)*state;
	bot_client_state_t *bot = BotFindEnemy_SetupHarness(context,
		4,
		"bots/babe_c.c",
		"babe");
	setup_console_command_aas_world();
	aasworld.areas[1].maxs[0] = 256.0f;
	bot_client_state_t *teammate = BotState_Create(1);
	assert_non_null(teammate);
	teammate->client_number = 1;
	teammate->entity_number = 2;
	BotFindEnemy_PrepareEntity(2, 200.0f, 0.0f, 0.0f);

	assert_int_equal(context->api->BotLibVarSet("nochat", "1"),
		BLERR_NOERROR);
	assert_int_equal(context->api->BotLibVarSet("teamplay", "1"),
		BLERR_NOERROR);
	aasworld.time = 300.0f;
	BotAINode_PrepareFrame(context,
		bot,
		BOT_AI_NODE_SEEK_LTG,
		0,
		-1000.0f,
		100,
		true,
		false);
	bot->ltg_type = 1;
	bot->ltg_teammate = 1;
	bot->team_message_time = 299.0f;
	bot->team_goal_time = 360.0f;
	bot->teammate_visible_time = 300.0f;
	BotAINode_RunFrame(context);
	assert_float_equal(bot->team_message_time, 0.0f, 0.0001f);
	assert_int_equal(bot->ltg_type, 1);
	assert_int_equal(bot->team_goal.entitynum, 2);
	assert_int_equal(bot->team_goal.areanum, 1);
	assert_float_equal(bot->team_goal.origin[0], 200.0f, 0.0001f);
	assert_int_equal(bot->active_goal_number, bot->team_goal.number);

	aasworld.time = 310.001f;
	aasworld.entities[2].inuse = qfalse;
	bot->client_update_valid = true;
	BotAINode_RunFrame(context);
	assert_int_equal(bot->ltg_type, 0);
	BotState_Destroy(teammate->client_number);
}

/*
=============
test_ai_team_accompany_scheduler_tracks_formation_and_loss

Pins LTG accompany's live teammate goal, formation-distance arrival look and
crouch hold, then the strict sixty-second lost-companion clear.
=============
*/
static void test_ai_team_accompany_scheduler_tracks_formation_and_loss(void **state)
{
	bot_interface_test_context_t *context =
		(bot_interface_test_context_t *)*state;
	bot_client_state_t *bot = BotFindEnemy_SetupHarness(context,
		4,
		"bots/babe_c.c",
		"babe");
	setup_console_command_aas_world();
	aasworld.areas[1].maxs[0] = 256.0f;
	bot_client_state_t *teammate = BotState_Create(1);
	assert_non_null(teammate);
	teammate->client_number = 1;
	teammate->entity_number = 2;
	BotFindEnemy_PrepareEntity(2, 200.0f, 0.0f, 0.0f);

	assert_int_equal(context->api->BotLibVarSet("nochat", "1"),
		BLERR_NOERROR);
	assert_int_equal(context->api->BotLibVarSet("teamplay", "1"),
		BLERR_NOERROR);
	aasworld.time = 400.0f;
	BotAINode_PrepareFrame(context,
		bot,
		BOT_AI_NODE_SEEK_LTG,
		0,
		-1000.0f,
		100,
		true,
		false);
	bot->ltg_type = 2;
	bot->ltg_teammate = 1;
	bot->team_message_time = 399.0f;
	bot->team_goal_time = 500.0f;
	bot->teammate_visible_time = 400.0f;
	bot->formation_dist = 112.0f;
	/* Stand the bot on ground so BotMoveToGoal resolves the same area as the
	   accompany goal and reaches BotMoveInGoalArea. Retail's BotClearMoveResult
	   (0x10031e26) clears only the 0x18-byte status prefix, so without this the
	   frame's view target would be read from an uninitialised movedir. */
	bot->last_client_update.pm_flags = PMF_ON_GROUND;
	BotAINode_RunFrame(context);
	assert_float_equal(bot->team_message_time, 0.0f, 0.0001f);
	assert_int_equal(bot->ltg_type, 2);
	assert_int_equal(bot->team_goal.entitynum, 2);
	assert_int_equal(bot->team_goal.areanum, 1);
	assert_float_equal(bot->team_goal.origin[0], 200.0f, 0.0001f);

	aasworld.time = 401.0f;
	bot->client_update_valid = true;
	VectorSet(bot->last_client_update.origin, 160.0f, 0.0f, 0.0f);
	VectorSet(bot->last_client_update.viewangles, 0.0f, 90.0f, 0.0f);
	BotAINode_RunFrame(context);
	assert_float_equal(bot->arrive_time, 401.0f, 0.0001f);
	assert_int_equal(bot->ltg_type, 2);
	assert_true(context->mock.bot_input_count > 0U);
	assert_float_equal(context->mock.inputs[
		context->mock.bot_input_count - 1U].speed,
		0.0f,
		0.0001f);
	assert_float_equal(context->mock.inputs[
		context->mock.bot_input_count - 1U].viewangles[YAW],
		0.0f,
		0.0001f);

	aasworld.time = 404.0f;
	bot->client_update_valid = true;
	AI_DMState_SetAttackCrouchTime(bot->dm_state, 410.0f);
	srand(1);
	BotAINode_RunFrame(context);
	assert_true((context->mock.inputs[
		context->mock.bot_input_count - 1U].actionflags & ACTION_CROUCH) != 0);
	ai_dm_metrics_t metrics;
	AI_DMState_GetMetrics(bot->dm_state, &metrics);
	assert_float_equal(metrics.ideal_viewangles[YAW], 180.0f, 0.0001f);
	assert_float_equal(context->mock.inputs[
		context->mock.bot_input_count - 1U].viewangles[YAW],
		9.99756f,
		0.01f);

	aasworld.time = 461.001f;
	bot->client_update_valid = true;
	VectorClear(bot->last_client_update.origin);
	bot->teammate_visible_time = 401.0f;
	context->mock.trace_blocked = true;
	BotAINode_RunFrame(context);
	assert_int_equal(bot->ltg_type, 0);
	assert_float_equal(bot->teammate_visible_time, 461.001f, 0.0001f);
	BotState_Destroy(teammate->client_number);
}

/*
=============
test_ai_seek_ltg_no_goal_advances_private_view

Pins sub_1001f760's no-goal exit: after BotLongTermGoal returns null, Seek LTG
submits no movement without refreshing the move state and advances the retained
no-enemy private view turn.
=============
*/
static void test_ai_seek_ltg_no_goal_advances_private_view(void **state)
{
	bot_interface_test_context_t *context =
		(bot_interface_test_context_t *)*state;
	bot_client_state_t *bot = BotFindEnemy_SetupHarness(context,
		4,
		"bots/babe_c.c",
		"babe");
	vec3_t ideal_viewangles = {0.0f, 90.0f, 0.0f};

	aasworld.time = 475.0f;
	BotAINode_PrepareFrame(context,
		bot,
		BOT_AI_NODE_SEEK_LTG,
		0,
		-1000.0f,
		100,
		true,
		false);
	bot_movestate_t *move_state = BotMoveStateFromHandle(bot->move_handle);
	assert_non_null(move_state);
	VectorSet(move_state->origin, 731.0f, 732.0f, 733.0f);
	move_state->areanum = 71;
	AI_DMState_SetIdealViewAngles(bot->dm_state, ideal_viewangles);
	BotAINode_RunFrame(context);

	assert_float_equal(context->mock.inputs[
		context->mock.bot_input_count - 1U].speed,
		0.0f,
		0.0001f);
	assert_float_equal(context->mock.inputs[
		context->mock.bot_input_count - 1U].viewangles[YAW],
		9.99756f,
		0.01f);
	assert_float_equal(move_state->origin[0], 731.0f, 0.0001f);
	assert_float_equal(move_state->origin[1], 732.0f, 0.0001f);
	assert_float_equal(move_state->origin[2], 733.0f, 0.0001f);
	assert_int_equal(move_state->areanum, 71);
}

/*
=============
test_ai_team_ctf_scheduler_routes_team_flags

Pins CTF get-flag's opposing-flag target, delayed start message, and strict
deadline, then rush-base's carried-flag home target and post-touch away timer.
=============
*/
static void test_ai_team_ctf_scheduler_routes_team_flags(void **state)
{
	bot_interface_test_context_t *context =
		(bot_interface_test_context_t *)*state;
	bot_client_state_t *bot = BotFindEnemy_SetupHarness(context,
		4,
		"bots/babe_c.c",
		"babe");
	setup_console_command_aas_world();
	register_console_command_goal(context, "Red Flag", 501, -96.0f);
	register_console_command_goal(context, "Blue Flag", 502, 96.0f);

	assert_int_equal(context->api->BotLibVarSet("nochat", "1"),
		BLERR_NOERROR);
	assert_int_equal(context->api->BotLibVarSet("ctf", "1"),
		BLERR_NOERROR);
	aasworld.time = 500.0f;
	BotAINode_PrepareFrame(context,
		bot,
		BOT_AI_NODE_SEEK_LTG,
		0,
		-1000.0f,
		100,
		true,
		false);
	bot->team = 1;
	bot->ltg_type = 4;
	bot->team_message_time = 499.0f;
	bot->team_goal_time = 600.0f;
	BotAINode_RunFrame(context);
	assert_float_equal(bot->team_message_time, 0.0f, 0.0001f);
	assert_int_equal(bot->ltg_type, 4);
	assert_int_equal(bot->active_goal_number, 502);

	aasworld.time = 600.001f;
	bot->client_update_valid = true;
	VectorClear(bot->last_client_update.origin);
	BotAINode_RunFrame(context);
	assert_int_equal(bot->ltg_type, 0);

	aasworld.time = 610.0f;
	bot->client_update_valid = true;
	bot->ltg_type = 5;
	bot->team_goal_time = 620.0f;
	bot->rush_base_away_time = 0.0f;
	bot->last_client_update.inventory[RETAIL_INVENTORY_FLAG1] = 1;
	VectorClear(bot->last_client_update.origin);
	BotAINode_RunFrame(context);
	assert_int_equal(bot->ltg_type, 5);
	assert_int_equal(bot->active_goal_number, 501);

	aasworld.time = 611.0f;
	bot->client_update_valid = true;
	VectorSet(bot->last_client_update.origin, -96.0f, 0.0f, 32.0f);
	srand(7);
	BotAINode_RunFrame(context);
	assert_int_equal(bot->ltg_type, 5);
	assert_true(bot->rush_base_away_time > 616.0f);
	assert_true(bot->rush_base_away_time <= 626.0f);

	aasworld.time = 612.0f;
	bot->client_update_valid = true;
	bot->rush_base_away_time = 0.0f;
	bot->team_goal_time = 611.5f;
	BotAINode_RunFrame(context);
	assert_int_equal(bot->ltg_type, 0);

	/*
	 * Retail's ltgtype 5 branch clears the order unconditionally only on the
	 * deadline (0x1001e57e -> 0x1001e580). The carried-flag test lives inside
	 * the BotTouchingGoal branch at 0x1001e5a9, so a bot that has lost the flag
	 * on the way home keeps rushing its own base until it arrives.
	 */
	aasworld.time = 613.0f;
	bot->client_update_valid = true;
	bot->ltg_type = 5;
	bot->team_goal_time = 700.0f;
	bot->rush_base_away_time = 0.0f;
	bot->last_client_update.inventory[RETAIL_INVENTORY_FLAG1] = 0;
	bot->last_client_update.inventory[RETAIL_INVENTORY_FLAG2] = 0;
	VectorClear(bot->last_client_update.origin);
	assert_int_equal(BotAI_CarryingFlag(bot), 0);
	BotAINode_RunFrame(context);
	assert_int_equal(bot->ltg_type, 5);
	assert_int_equal(bot->active_goal_number, 501);
	assert_float_equal(bot->rush_base_away_time, 0.0f, 0.0001f);

	/* Arriving at the home flag without one does end the order (0x1001e5a9's
	   miss arm), and it does not arm the away timer. */
	aasworld.time = 614.0f;
	bot->client_update_valid = true;
	bot->ltg_type = 5;
	bot->team_goal_time = 700.0f;
	bot->rush_base_away_time = 0.0f;
	VectorSet(bot->last_client_update.origin, -96.0f, 0.0f, 32.0f);
	BotAINode_RunFrame(context);
	assert_int_equal(bot->ltg_type, 0);
	assert_float_equal(bot->rush_base_away_time, 0.0f, 0.0001f);
}

/*
=============
test_ai_seek_ltg_automatic_ctf_goal_scheduler

Pins sub_10026440's own CTF selection: missing static flags start the fixed
roam lease, which suppresses reassignment until an aggressive bot later picks
its home flag for a defend LTG.
=============
*/
static void test_ai_seek_ltg_automatic_ctf_goal_scheduler(void **state)
{
	bot_interface_test_context_t *context =
		(bot_interface_test_context_t *)*state;
	bot_client_state_t *bot = BotFindEnemy_SetupHarness(context,
		4,
		"bots/babe_c.c",
		"babe");
	setup_console_command_aas_world();

	assert_int_equal(context->api->BotLibVarSet("nochat", "1"),
		BLERR_NOERROR);
	assert_int_equal(context->api->BotLibVarSet("ctf", "1"),
		BLERR_NOERROR);
	BotAINode_ClearEnemyEntities();
	aasworld.time = 630.0f;
	BotAINode_PrepareFrame(context,
		bot,
		BOT_AI_NODE_SEEK_LTG,
		0,
		-1000.0f,
		100,
		true,
		false);
	bot->team = 1;
	srand(1);
	BotAINode_RunFrame(context);
	assert_int_equal(bot->ltg_type, 0);
	assert_float_equal(bot->get_flag_away_time, 690.0f, 0.0001f);

	register_console_command_goal(context, "Red Flag", 611, -96.0f);
	register_console_command_goal(context, "Blue Flag", 612, 96.0f);
	aasworld.time = 650.0f;
	BotAINode_PrepareFrame(context,
		bot,
		BOT_AI_NODE_SEEK_LTG,
		0,
		-1000.0f,
		100,
		true,
		false);
	bot->team = 1;
	srand(1);
	BotAINode_RunFrame(context);
	assert_int_equal(bot->ltg_type, 0);
	assert_float_equal(bot->get_flag_away_time, 690.0f, 0.0001f);

	aasworld.time = 691.0f;
	BotAINode_PrepareFrame(context,
		bot,
		BOT_AI_NODE_SEEK_LTG,
		0,
		-1000.0f,
		100,
		true,
		false);
	bot->team = 1;
	srand(1);
	BotAINode_RunFrame(context);
	assert_int_equal(bot->ltg_type, 3);
	assert_int_equal(bot->team_goal_number, 611);
	assert_float_equal(bot->team_goal_time, 811.0f, 0.0001f);
	assert_int_equal(bot->active_goal_number, 611);
}

/*
=============
test_ai_battle_retreat_carried_flag_promotes_home_goal

Pins Battle Retreat's CTF carrier handoff to the home-base LTG before its
nearby-goal probe and direct movement work.
=============
*/
static void test_ai_battle_retreat_carried_flag_promotes_home_goal(void **state)
{
	bot_interface_test_context_t *context =
		(bot_interface_test_context_t *)*state;
	bot_client_state_t *bot = BotFindEnemy_SetupHarness(context,
		4,
		"bots/babe_c.c",
		"babe");
	setup_console_command_aas_world();
	register_console_command_goal(context, "Red Flag", 601, -96.0f);
	register_console_command_goal(context, "Blue Flag", 602, 96.0f);

	assert_int_equal(context->api->BotLibVarSet("nochat", "1"),
		BLERR_NOERROR);
	assert_int_equal(context->api->BotLibVarSet("ctf", "1"),
		BLERR_NOERROR);
	BotAINode_ClearEnemyEntities();
	BotFindEnemy_PrepareEntity(2, 100.0f, 0.0f, 0.0f);
	aasworld.time = 620.0f;
	BotAINode_PrepareFrame(context,
		bot,
		BOT_AI_NODE_BATTLE_RETREAT,
		2,
		619.0f,
		30,
		true,
		false);
	bot->team = 1;
	bot->ltg_type = 0;
	bot->rush_base_away_time = 999.0f;
	bot->team_goal_time = 0.0f;
	bot->last_client_update.inventory[RETAIL_INVENTORY_FLAG1] = 1;

	BotAINode_RunFrame(context);
	assert_int_equal(bot->ltg_type, 5);
	assert_float_equal(bot->rush_base_away_time, 0.0f, 0.0001f);
	assert_float_equal(bot->team_goal_time, 740.0f, 0.0001f);
	assert_int_equal(bot->active_goal_number, 601);

	/* Retail only starts the Rush Base lease as the LTG changes. */
	aasworld.time = 621.0f;
	BotAINode_PrepareFrame(context,
		bot,
		BOT_AI_NODE_BATTLE_RETREAT,
		2,
		620.0f,
		30,
		true,
		false);
	bot->ltg_type = 5;
	bot->rush_base_away_time = 0.0f;
	bot->team_goal_time = 740.0f;
	bot->last_client_update.inventory[RETAIL_INVENTORY_FLAG1] = 1;
	BotAINode_RunFrame(context);
	assert_float_equal(bot->team_goal_time, 740.0f, 0.0001f);
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

/*
=============
test_bot_start_frame_entity_lifecycle

Pins retail frame invalidation without an invented stale-link sweep.
=============
*/
static void test_bot_start_frame_entity_lifecycle(void **state)
{
    bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;

    Mock_Reset(&context->mock);

    int status = context->api->BotSetupLibrary();
    assert_int_equal(status, BLERR_NOERROR);

    const int entityNum = 7;
    assert_non_null(aasworld.entities);
	AAS_InitAASLinkHeap();
	aas_link_t *link = AAS_AllocAASLink();
    assert_non_null(link);
	memset(link, 0, sizeof(*link));
    aasworld.entities[entityNum].areas = link;
    aasworld.entities[entityNum].inuse = qtrue;

    status = context->api->BotStartFrame(0.1f);
    assert_int_equal(status, BLERR_NOERROR);
    assert_false(aasworld.entities[entityNum].inuse);
    assert_non_null(aasworld.entities[entityNum].areas);
    assert_false(aasworld.entitiesValid);

    status = context->api->BotStartFrame(0.2f);
    assert_int_equal(status, BLERR_NOERROR);
    assert_non_null(aasworld.entities[entityNum].areas);
    assert_false(aasworld.entitiesValid);

    AAS_ResetEntityLinks();
    assert_null(aasworld.entities[entityNum].areas);

    context->api->BotShutdownLibrary();
}

/*
=============
test_bot_start_frame_matches_retail_frame_sequence

Pins the raw frame order, routing counter reset, and one-shot diagnostics.
=============
*/
static void test_bot_start_frame_matches_retail_frame_sequence(void **state)
{
    bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;

    Mock_Reset(&context->mock);

    int status = context->api->BotSetupLibrary();
    assert_int_equal(status, BLERR_NOERROR);

    AAS_RouteFrameResetDiagnostics();
    AAS_RetailResetCacheUpdateCounts();
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
    assert_int_equal(AAS_RouteFrameSkipCounter(), 0);
    assert_int_equal(AAS_RouteFrameWorkCounter(), 0);
    assert_int_equal(AAS_RouteFrameLastBudget(), 0);
    assert_false(AAS_RouteFrameForceWriteActive());
    assert_int_equal(AAS_ReachabilityFrameSkipCounter(), 0);
    assert_int_equal(AAS_ReachabilityFrameWorkCounter(), 0);
    assert_false(AAS_ReachabilityForceReachabilityActive());
    assert_false(AAS_ReachabilityForceClusteringActive());
    assert_int_equal(AAS_RetailFrameRoutingUpdateCount(), 0);

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
    assert_int_equal(AAS_RouteFrameSkipCounter(), 0);
    assert_int_equal(AAS_RouteFrameWorkCounter(), 0);
    assert_int_equal(AAS_RouteFrameLastBudget(), 0);
    assert_false(AAS_RouteFrameForceWriteActive());
    assert_int_equal(AAS_ReachabilityFrameSkipCounter(), 0);
    assert_int_equal(AAS_ReachabilityFrameWorkCounter(), 0);
    assert_false(AAS_ReachabilityForceReachabilityActive());
    assert_false(AAS_ReachabilityForceClusteringActive());
    assert_int_equal(AAS_RetailFrameRoutingUpdateCount(), 0);

    AAS_RouteFrameResetDiagnostics();
    AAS_RetailResetCacheUpdateCounts();
    Mock_ClearPrints(&context->mock);
    status = context->api->BotLibVarSet("showcacheupdates", "1");
    assert_int_equal(status, BLERR_NOERROR);
    status = context->api->BotLibVarSet("showmemoryusage", "1");
    assert_int_equal(status, BLERR_NOERROR);
    status = context->api->BotLibVarSet("memorydump", "1");
    assert_int_equal(status, BLERR_NOERROR);

    status = context->api->BotStartFrame(0.4f);
    assert_int_equal(status, BLERR_NOERROR);
    assert_float_equal(LibVarValue("showcacheupdates", "0"), 0.0f, 0.0001f);
    assert_float_equal(LibVarValue("showmemoryusage", "0"), 0.0f, 0.0001f);
    assert_float_equal(LibVarValue("memorydump", "0"), 0.0f, 0.0001f);
    assert_non_null(Mock_FindPrint(&context->mock, "0 area cache updates\n"));
    assert_non_null(Mock_FindPrint(&context->mock, "0 portal cache updates\n"));
    assert_non_null(Mock_FindPrint(&context->mock, "total botlib memory:"));
    assert_non_null(Mock_FindPrint(&context->mock, "total memory blocks:"));

    context->api->BotShutdownLibrary();
}

/*
=============
test_bot_start_frame_updates_goal_time

Pins the shared AAS clock used by the retail avoid-goal expiration helpers.
=============
*/
static void test_bot_start_frame_updates_goal_time(void **state)
{
	bot_interface_test_context_t *context =
		(bot_interface_test_context_t *)*state;

	Mock_Reset(&context->mock);

	int status = context->api->BotSetupLibrary();
	assert_int_equal(status, BLERR_NOERROR);

	int handle = context->api->BotAllocGoalState(1);
	assert_true(handle > 0);

	status = context->api->BotStartFrame(100.0f);
	assert_int_equal(status, BLERR_NOERROR);
	context->api->BotAddAvoidGoal(handle, 77, 4.0f);
	assert_float_equal(context->api->BotAvoidGoalTime(handle, 77),
		4.0f,
		0.0001f);

	status = context->api->BotStartFrame(102.5f);
	assert_int_equal(status, BLERR_NOERROR);
	assert_float_equal(context->api->BotAvoidGoalTime(handle, 77),
		1.5f,
		0.0001f);

	context->api->BotFreeGoalState(handle);
	context->api->BotShutdownLibrary();
}

/*
=============
test_bot_add_avoid_spot_export_is_compatibility_noop

Verify the extended Quake III avoid-spot export cannot mutate the retail state.
=============
*/
static void test_bot_add_avoid_spot_export_is_compatibility_noop(void **state)
{
	bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;

	assert_non_null(context->api->BotAddAvoidSpot);

	int status = context->api->BotSetupLibrary();
	assert_int_equal(status, BLERR_NOERROR);

	int handle = context->api->BotAllocMoveState();
	assert_true(handle > 0);

	vec3_t origin;
	VectorSet(origin, 12.0f, 24.0f, 36.0f);

	bot_movestate_t *ms = BotMoveStateFromHandle(handle);
	assert_non_null(ms);
	ms->lastreachnum = 17;
	ms->reachability_time = 23.0f;
	bot_movestate_t before = *ms;
	context->api->BotAddAvoidSpot(handle, origin, 64.0f, AVOID_ALWAYS);
	assert_memory_equal(ms, &before, sizeof(*ms));

	context->api->BotAddAvoidSpot(handle, origin, 0.0f, AVOID_CLEAR);
	assert_memory_equal(ms, &before, sizeof(*ms));

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
		cmocka_unit_test_setup_teardown(test_bot_goal_setup_failure_retains_loaded_weapon_config,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_setup_commits_before_retail_libvar_sequence,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_shutdown_library_guard_emits_message,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_library_initialized_reports_aas_state,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_shutdown_clears_public_adapter_tables,
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
		cmocka_unit_test_setup_teardown(
			test_pointlight_heap_is_not_initialised_by_library_setup,
			setup_bot_interface,
			teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_load_map_and_sensory_queues,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(
			test_bot_load_map_keeps_clients_active_and_runs_deathmatch_pass,
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
		cmocka_unit_test_setup_teardown(test_bot_console_ctf_rush_base_is_dormant_but_enemy_flag_order_runs,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_console_reply_enters_and_completes_stand,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_q3_compat_chat_exports_preserve_type_aliases,
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
		cmocka_unit_test_setup_teardown(test_general_item_use_retail_order_and_boundaries,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_battle_item_use_retail_order_and_boundaries,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_battle_flag_retreat_and_chase_decisions,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_battle_aggression_retail_gate_boundaries,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_battle_rocket_jump_retail_gate_boundaries,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_find_enemy_live_predicate_boundaries,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_find_enemy_numeric_first_and_visible_cap,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(
			test_find_enemy_nonaccelerated_range_fov_and_close_boundaries,
			setup_bot_interface,
			teardown_bot_interface),
		cmocka_unit_test_setup_teardown(
			test_find_enemy_accelerated_profile_has_no_900_cap,
			setup_bot_interface,
			teardown_bot_interface),
		cmocka_unit_test_setup_teardown(
			test_find_enemy_team_precedence_and_entity_client_mapping,
			setup_bot_interface,
			teardown_bot_interface),
		cmocka_unit_test_setup_teardown(
			test_find_enemy_shooting_facing_and_retreat_fallback,
			setup_bot_interface,
			teardown_bot_interface),
		cmocka_unit_test_setup_teardown(
			test_find_enemy_health_sight_and_failure_side_effects,
			setup_bot_interface,
			teardown_bot_interface),
		cmocka_unit_test(test_ai_node_scheduler_exact_switch_limit),
		cmocka_unit_test_setup_teardown(
			test_ai_node_find_enemy_schedule_and_clear_matrix,
			setup_bot_interface,
			teardown_bot_interface),
		cmocka_unit_test_setup_teardown(
			test_ai_activate_entity_goal_lifetime_contact_failure_and_enemy_scan,
			setup_bot_interface,
			teardown_bot_interface),
		cmocka_unit_test_setup_teardown(
			test_ai_node_immediate_delayed_and_retreat_transitions,
			setup_bot_interface,
			teardown_bot_interface),
		cmocka_unit_test_setup_teardown(
			test_ai_seek_nbg_scans_before_private_view_turn,
			setup_bot_interface,
			teardown_bot_interface),
		cmocka_unit_test_setup_teardown(
			test_ai_weapon_selection_is_combat_node_local,
			setup_bot_interface,
			teardown_bot_interface),
		cmocka_unit_test_setup_teardown(
			test_ai_battle_fight_runs_general_item_use,
			setup_bot_interface,
			teardown_bot_interface),
		cmocka_unit_test_setup_teardown(
			test_ai_active_battle_movers_run_general_item_use,
			setup_bot_interface,
			teardown_bot_interface),
		cmocka_unit_test_setup_teardown(
			test_ai_battle_fight_leaves_move_state_untouched,
			setup_bot_interface,
			teardown_bot_interface),
		cmocka_unit_test_setup_teardown(
			test_ai_battle_fight_does_not_invent_rocket_jump,
			setup_bot_interface,
			teardown_bot_interface),
		cmocka_unit_test_setup_teardown(
			test_ai_battle_fight_initialises_move_state_after_attack_gates,
			setup_bot_interface,
			teardown_bot_interface),
		cmocka_unit_test_setup_teardown(
			test_ai_node_fight_and_chase_enemy_ownership,
			setup_bot_interface,
			teardown_bot_interface),
		cmocka_unit_test_setup_teardown(
			test_ai_battle_fight_dead_enemy_enters_kill_chat_stand,
			setup_bot_interface,
			teardown_bot_interface),
		cmocka_unit_test_setup_teardown(
			test_ai_stand_acquires_enemy_before_pending_chat_wait,
			setup_bot_interface,
			teardown_bot_interface),
		cmocka_unit_test_setup_teardown(
			test_ai_stand_without_enemy_advances_private_view,
			setup_bot_interface,
			teardown_bot_interface),
		cmocka_unit_test_setup_teardown(
			test_ai_stand_expiry_advances_view_before_seek_ltg,
			setup_bot_interface,
			teardown_bot_interface),
		cmocka_unit_test_setup_teardown(
			test_ai_stand_squatt_guard_removes_bot,
			setup_bot_interface,
			teardown_bot_interface),
		cmocka_unit_test_setup_teardown(
			test_ai_battle_chase_uses_entry_deadline,
			setup_bot_interface,
			teardown_bot_interface),
		cmocka_unit_test_setup_teardown(
			test_ai_battle_chase_reached_last_seen_goal_expires,
			setup_bot_interface,
			teardown_bot_interface),
		cmocka_unit_test_setup_teardown(
			test_ai_battle_nbg_records_enemy_location,
			setup_bot_interface,
			teardown_bot_interface),
		cmocka_unit_test_setup_teardown(
			test_ai_battle_nbg_expiry_pops_nearby_goal,
			setup_bot_interface,
			teardown_bot_interface),
		cmocka_unit_test_setup_teardown(
			test_ai_battle_nbg_uses_touch_without_static_item_visibility_completion,
			setup_bot_interface,
			teardown_bot_interface),
		cmocka_unit_test_setup_teardown(
			test_ai_battle_retreat_exits_to_chase_when_safe,
			setup_bot_interface,
			teardown_bot_interface),
		cmocka_unit_test_setup_teardown(
			test_ai_battle_retreat_attempts_chase_despite_get_flag_when_aggressive,
			setup_bot_interface,
			teardown_bot_interface),
		cmocka_unit_test_setup_teardown(
			test_ai_battle_retreat_without_goal_keeps_idle_view,
			setup_bot_interface,
			teardown_bot_interface),
		cmocka_unit_test_setup_teardown(
			test_ai_battle_retreat_failed_move_preserves_nearby_lease,
			setup_bot_interface,
			teardown_bot_interface),
		cmocka_unit_test_setup_teardown(
			test_ai_lifecycle_nodes_gate_pmove_states,
			setup_bot_interface,
			teardown_bot_interface),
		cmocka_unit_test_setup_teardown(
			test_ai_seek_ltg_random_chat_gates,
			setup_bot_interface,
			teardown_bot_interface),
		cmocka_unit_test_setup_teardown(
			test_ai_seek_ltg_waves_after_recent_enemy_death,
			setup_bot_interface,
			teardown_bot_interface),
		cmocka_unit_test_setup_teardown(
			test_ai_seek_ltg_nearby_goal_schedule,
			setup_bot_interface,
			teardown_bot_interface),
		cmocka_unit_test_setup_teardown(
			test_ai_seek_ltg_failed_reselection_preserves_lower_stack,
			setup_bot_interface,
			teardown_bot_interface),
		cmocka_unit_test_setup_teardown(
			test_ai_seek_nbg_failed_move_preserves_ltg_deadline,
			setup_bot_interface,
			teardown_bot_interface),
		cmocka_unit_test_setup_teardown(
			test_ai_battle_chase_failed_move_preserves_nearby_lease,
			setup_bot_interface,
			teardown_bot_interface),
		cmocka_unit_test_setup_teardown(
			test_ai_reached_ctf_tech_drops_conflicting_rune,
			setup_bot_interface,
			teardown_bot_interface),
		cmocka_unit_test_setup_teardown(
			test_ai_seek_nbg_clears_missing_item_goal,
			setup_bot_interface,
			teardown_bot_interface),
		cmocka_unit_test_setup_teardown(
			test_ai_team_goal_scheduler_direct_branches,
			setup_bot_interface,
			teardown_bot_interface),
		cmocka_unit_test_setup_teardown(
			test_ai_team_help_scheduler_tracks_live_teammate,
			setup_bot_interface,
			teardown_bot_interface),
		cmocka_unit_test_setup_teardown(
			test_ai_team_accompany_scheduler_tracks_formation_and_loss,
			setup_bot_interface,
			teardown_bot_interface),
		cmocka_unit_test_setup_teardown(
			test_ai_seek_ltg_no_goal_advances_private_view,
			setup_bot_interface,
			teardown_bot_interface),
		cmocka_unit_test_setup_teardown(
			test_ai_team_ctf_scheduler_routes_team_flags,
			setup_bot_interface,
			teardown_bot_interface),
		cmocka_unit_test_setup_teardown(
			test_ai_seek_ltg_automatic_ctf_goal_scheduler,
			setup_bot_interface,
			teardown_bot_interface),
		cmocka_unit_test_setup_teardown(
			test_ai_battle_retreat_carried_flag_promotes_home_goal,
			setup_bot_interface,
			teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_interface_mover_parity,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(
			test_ai_battle_chase_preserves_mover_set_view,
			setup_bot_interface,
			teardown_bot_interface),
		cmocka_unit_test_setup_teardown(
			test_ai_battle_chase_arrival_area_expires_deadline,
			setup_bot_interface,
			teardown_bot_interface),
		cmocka_unit_test_setup_teardown(
			test_ai_entity_to_activate_follows_trigger_target_chain,
			setup_bot_interface,
			teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_ai_battle_chase_handles_blocked_move,
			setup_bot_interface,
			teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_start_frame_entity_lifecycle,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_start_frame_matches_retail_frame_sequence,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_start_frame_updates_goal_time,
							setup_bot_interface,
							teardown_bot_interface),
		cmocka_unit_test_setup_teardown(test_bot_add_avoid_spot_export_is_compatibility_noop,
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
