#include <stdarg.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>

#include <cmocka.h>

#include "botlib/ai_move/bot_move.h"
#include "botlib/aas/aas_local.h"
#include "botlib/common/l_libvar.h"
#include "botlib/common/l_memory.h"
#include "botlib/ea/ea_local.h"
#include "botlib/interface/botlib_interface.h"
#include "q2bridge/bridge.h"
#include "q2bridge/bridge_config.h"

#ifndef MAX_TEST_COMMANDS
#define MAX_TEST_COMMANDS 16
#endif

#ifndef MAX_TEST_TRACES
#define MAX_TEST_TRACES 8
#endif

#ifndef MAX_TEST_POINT_CONTENTS
#define MAX_TEST_POINT_CONTENTS 8
#endif

static char g_command_log[MAX_TEST_COMMANDS][128];
static int g_command_count;
static bsp_trace_t g_trace_results[MAX_TEST_TRACES];
static int g_trace_result_count;
static int g_trace_call_count;
static vec3_t g_trace_start_log[MAX_TEST_TRACES];
static vec3_t g_trace_mins_log[MAX_TEST_TRACES];
static vec3_t g_trace_maxs_log[MAX_TEST_TRACES];
static vec3_t g_trace_end_log[MAX_TEST_TRACES];
static int g_trace_passent_log[MAX_TEST_TRACES];
static int g_trace_contentmask_log[MAX_TEST_TRACES];
static int g_point_contents_results[MAX_TEST_POINT_CONTENTS];
static int g_point_contents_result_count;
static int g_point_contents_call_count;
static vec3_t g_point_contents_log[MAX_TEST_POINT_CONTENTS];

#define TEST_AAS_AREAFLAG_GROUNDED 1

static void test_reset_command_log(void)
{
    g_command_count = 0;
    for (int i = 0; i < MAX_TEST_COMMANDS; ++i)
    {
        g_command_log[i][0] = '\0';
    }
}

/*
=============
test_reset_trace_log

Reset trace mock results and captured trace inputs.
=============
*/
static void test_reset_trace_log(void)
{
	g_trace_result_count = 0;
	g_trace_call_count = 0;
	memset(g_trace_results, 0, sizeof(g_trace_results));
	memset(g_trace_start_log, 0, sizeof(g_trace_start_log));
	memset(g_trace_mins_log, 0, sizeof(g_trace_mins_log));
	memset(g_trace_maxs_log, 0, sizeof(g_trace_maxs_log));
	memset(g_trace_end_log, 0, sizeof(g_trace_end_log));
	memset(g_trace_passent_log, 0, sizeof(g_trace_passent_log));
	memset(g_trace_contentmask_log, 0, sizeof(g_trace_contentmask_log));
	g_point_contents_result_count = 0;
	g_point_contents_call_count = 0;
	memset(g_point_contents_results, 0, sizeof(g_point_contents_results));
	memset(g_point_contents_log, 0, sizeof(g_point_contents_log));
}

static void test_bridge_print(int type, char *fmt, ...)
{
    (void)type;
    (void)fmt;
}

static bsp_trace_t test_bridge_trace(vec3_t start,
                                     vec3_t mins,
                                     vec3_t maxs,
                                     vec3_t end,
                                     int passent,
                                     int contentmask)
{
	int index = g_trace_call_count;
	if (index >= 0 && index < MAX_TEST_TRACES)
	{
		if (start != NULL)
		{
			VectorCopy(start, g_trace_start_log[index]);
		}
		if (mins != NULL)
		{
			VectorCopy(mins, g_trace_mins_log[index]);
		}
		if (maxs != NULL)
		{
			VectorCopy(maxs, g_trace_maxs_log[index]);
		}
		if (end != NULL)
		{
			VectorCopy(end, g_trace_end_log[index]);
		}
		g_trace_passent_log[index] = passent;
		g_trace_contentmask_log[index] = contentmask;
	}
	g_trace_call_count++;

	if (index >= 0 && index < g_trace_result_count)
	{
		return g_trace_results[index];
	}

    bsp_trace_t trace;
    memset(&trace, 0, sizeof(trace));
    trace.fraction = 1.0f;
    return trace;
}

static int test_bridge_point_contents(vec3_t point)
{
	int index = g_point_contents_call_count;
	if (index >= 0 && index < MAX_TEST_POINT_CONTENTS && point != NULL)
	{
		VectorCopy(point, g_point_contents_log[index]);
	}
	g_point_contents_call_count++;

	if (index >= 0 && index < g_point_contents_result_count)
	{
		return g_point_contents_results[index];
	}

	return 0;
}

static void *test_bridge_get_memory(int size)
{
    return calloc(1, (size_t)(size > 0 ? size : 0));
}

static void test_bridge_free_memory(void *ptr)
{
    free(ptr);
}

static int test_bridge_debug_line_create(void)
{
    return 0;
}

static void test_bridge_debug_line_delete(int line)
{
    (void)line;
}

static void test_bridge_debug_line_show(int line, vec3_t start, vec3_t end, int color)
{
    (void)line;
    (void)start;
    (void)end;
    (void)color;
}

static void test_bridge_bot_input(int client, bot_input_t *input)
{
    (void)client;
    (void)input;
}

static void test_bridge_bot_client_command(int client, char *fmt, ...)
{
    (void)client;
    if (g_command_count >= MAX_TEST_COMMANDS || fmt == NULL)
    {
        return;
    }

    va_list args;
    va_start(args, fmt);
    vsnprintf(g_command_log[g_command_count], sizeof(g_command_log[g_command_count]), fmt, args);
    va_end(args);

    g_command_log[g_command_count][sizeof(g_command_log[g_command_count]) - 1] = '\0';
    g_command_count += 1;
}

static bot_import_t g_test_bridge_imports = {
    .BotInput = test_bridge_bot_input,
    .BotClientCommand = test_bridge_bot_client_command,
    .Print = test_bridge_print,
    .CvarGet = NULL,
    .Error = NULL,
    .Trace = test_bridge_trace,
    .PointContents = test_bridge_point_contents,
    .GetMemory = test_bridge_get_memory,
    .FreeMemory = test_bridge_free_memory,
    .DebugLineCreate = test_bridge_debug_line_create,
    .DebugLineDelete = test_bridge_debug_line_delete,
    .DebugLineShow = test_bridge_debug_line_show,
};

static void test_botlib_print(int priority, const char *fmt, ...)
{
    (void)priority;
    (void)fmt;
}

static void test_botlib_dprint(const char *fmt, ...)
{
    (void)fmt;
}

static int test_botlib_var_get(const char *var_name, char *value, size_t size)
{
    (void)var_name;
    if (value == NULL || size == 0)
    {
        return -1;
    }
    value[0] = '\0';
    return -1;
}

static int test_botlib_var_set(const char *var_name, const char *value)
{
    (void)var_name;
    (void)value;
    return 0;
}

static botlib_import_table_t g_test_lib_imports = {
    .Print = test_botlib_print,
    .DPrint = test_botlib_dprint,
    .BotLibVarGet = test_botlib_var_get,
    .BotLibVarSet = test_botlib_var_set,
};

static int test_setup(void **state)
{
    (void)state;

    test_reset_command_log();
	test_reset_trace_log();
    Q2Bridge_SetImportTable(&g_test_bridge_imports);
    BotInterface_SetImportTable(&g_test_lib_imports);
    LibVar_Init();
    BridgeConfig_Init();
    LibVarSet("laserhook", "1");
    LibVarSet("usehook", "1");

    if (EA_Init(1) != BLERR_NOERROR)
    {
        return -1;
    }

    memset(&aasworld, 0, sizeof(aasworld));
    aasworld.loaded = qtrue;
    return 0;
}

static int test_teardown(void **state)
{
    (void)state;

    EA_Shutdown();
    BridgeConfig_Shutdown();
    LibVar_Shutdown();
    Q2Bridge_ClearImportTable();
    BotInterface_SetImportTable(NULL);
    return 0;
}

static void test_bot_move_handles_elevator_landing(void **state)
{
    (void)state;

    BotMove_MoverCatalogueReset();

    aasworld.time = 2.0f;
    aasworld.numAreas = 2;
    aasworld.numAreaSettings = 3;
    aasworld.areasettings = calloc(3, sizeof(aas_areasettings_t));
    assert_non_null(aasworld.areasettings);
    aasworld.areasettings[1].firstreachablearea = 1;
    aasworld.areasettings[1].numreachableareas = 1;

    aasworld.numReachability = 2;
    aasworld.reachability = calloc(2, sizeof(aas_reachability_t));
    assert_non_null(aasworld.reachability);
    aasworld.reachability[1].areanum = 2;
    aasworld.reachability[1].traveltype = TRAVEL_ELEVATOR;
    aasworld.reachability[1].facenum = 5;

    aasworld.travelflagfortype[TRAVEL_WALK] = TFL_WALK;
    aasworld.travelflagfortype[TRAVEL_ELEVATOR] = TFL_ELEVATOR;

    aasworld.areaEntityListCount = 3U;
    aasworld.areaEntityLists = calloc(aasworld.areaEntityListCount, sizeof(aas_link_t *));
    assert_non_null(aasworld.areaEntityLists);

    aasworld.maxEntities = 3;
    aasworld.entities = calloc((size_t)aasworld.maxEntities, sizeof(aas_entity_t));
    assert_non_null(aasworld.entities);

    aasworld.entities[1].inuse = qtrue;
    aasworld.entities[1].number = 1;

    aasworld.entities[2].inuse = qtrue;
    aasworld.entities[2].number = 2;
    aasworld.entities[2].solid = SOLID_BSP;
    aasworld.entities[2].modelindex = 5;

    aas_link_t *botLink = calloc(1, sizeof(aas_link_t));
    aas_link_t *moverLink = calloc(1, sizeof(aas_link_t));
    assert_non_null(botLink);
    assert_non_null(moverLink);

    botLink->entnum = 1;
    botLink->areanum = 1;
    moverLink->entnum = 2;
    moverLink->areanum = 1;
    moverLink->next_ent = botLink;
    botLink->prev_ent = moverLink;

    aasworld.areaEntityLists[1] = moverLink;
    aasworld.entities[1].areas = botLink;
    aasworld.entities[2].areas = moverLink;

    bot_mover_catalogue_entry_t entry = {
        .modelnum = 5,
        .lip = 8.0f,
        .height = 0.0f,
        .speed = 200.0f,
        .spawnflags = 0,
        .doortype = 0,
    };
    assert_true(BotMove_MoverCatalogueInsert(&entry));

    int handle = BotAllocMoveState();
    assert_int_not_equal(handle, 0);
    bot_movestate_t *ms = BotMoveStateFromHandle(handle);
    assert_non_null(ms);
    ms->entitynum = 1;
    ms->areanum = 1;
    VectorClear(ms->origin);

    bot_goal_t goal = {0};
    goal.areanum = 2;

    bot_moveresult_t result;
    BotClearMoveResult(&result);

    BotMoveToGoal(&result, handle, &goal, TFL_DEFAULT);

    assert_false(result.blocked);
    assert_true(result.flags & MOVERESULT_ONTOPOF_ELEVATOR);
    assert_int_equal(result.type, RESULTTYPE_ELEVATORUP);
    assert_int_equal(ms->lastreachnum, 1);
    assert_int_equal(ms->reachareanum, 2);
    float timeoutDelta = ms->reachability_time - aasworld.time;
    assert_true(timeoutDelta > 4.9f && timeoutDelta < 5.1f);

    BotFreeMoveState(handle);
    BotMove_MoverCatalogueReset();

    free(botLink);
    free(moverLink);
    free(aasworld.areasettings);
    free(aasworld.reachability);
    free(aasworld.areaEntityLists);
    free(aasworld.entities);

    aasworld.areasettings = NULL;
    aasworld.reachability = NULL;
    aasworld.areaEntityLists = NULL;
    aasworld.entities = NULL;
    aasworld.numAreaSettings = 0;
    aasworld.numReachability = 0;
    aasworld.areaEntityListCount = 0U;
    aasworld.maxEntities = 0;
    aasworld.numAreas = 0;
}

static void test_bot_travel_grapple_hook_toggles(void **state)
{
    (void)state;

    bot_movestate_t ms;
    memset(&ms, 0, sizeof(ms));
    ms.client = 0;
    ms.viewangles[YAW] = 90.0f;
    ms.viewangles[PITCH] = 0.0f;

    aas_reachability_t reach = {0};
    reach.traveltype = TRAVEL_GRAPPLEHOOK;
    VectorSet(reach.start, 0.0f, 0.0f, 0.0f);
    VectorSet(reach.end, 0.0f, 64.0f, 0.0f);

    aasworld.time = 0.0f;

    bot_moveresult_t result = BotTravel_Grapple(&ms, &reach);
    assert_false(result.failure);
    assert_true(result.flags & MOVERESULT_MOVEMENTWEAPON);
    assert_true(ms.moveflags & MFL_ACTIVEGRAPPLE);

    bot_input_t input = {0};
    assert_int_equal(EA_GetInput(0, 0.1f, &input), BLERR_NOERROR);
    assert_true(input.actionflags & ACTION_ATTACK);
    assert_int_equal(g_command_count, 2);
    assert_string_equal(g_command_log[0], "precache models/weapons/grapple/hook/tris.md2");
    assert_string_equal(g_command_log[1], "hookon");

    test_reset_command_log();
    aasworld.time = 1.0f;

    bot_moveresult_t release = BotTravel_Grapple(&ms, &reach);
    assert_false(release.failure);
    assert_true(release.flags & MOVERESULT_MOVEMENTWEAPON);
    assert_int_equal(EA_GetInput(0, 0.1f, &input), BLERR_NOERROR);
    assert_int_equal(g_command_count, 1);
    assert_string_equal(g_command_log[0], "hookoff");
    assert_false(ms.moveflags & MFL_ACTIVEGRAPPLE);
}

/*
=============
test_bot_move_in_direction_submits_actions

Verifies direct movement requests issue EA movement, jump, and crouch input.
=============
*/
static void test_bot_move_in_direction_submits_actions(void **state)
{
	(void)state;

	int handle = BotAllocMoveState();
	assert_true(handle > 0);

	bot_movestate_t *ms = BotMoveStateFromHandle(handle);
	assert_non_null(ms);
	BotResetMoveState(handle);
	ms->client = 0;
	ms->entitynum = 0;
	ms->moveflags = MFL_ONGROUND;

	vec3_t dir;
	VectorSet(dir, 1.0f, 1.0f, 0.0f);

	assert_true(BotMoveInDirection(handle, dir, 280.0f, MOVE_JUMP | MOVE_CROUCH));

	bot_input_t input;
	assert_int_equal(EA_GetInput(0, 0.1f, &input), BLERR_NOERROR);
	assert_float_equal(input.speed, 280.0f, 0.0001f);
	assert_float_equal(input.dir[0], 0.7071067f, 0.0001f);
	assert_float_equal(input.dir[1], 0.7071067f, 0.0001f);
	assert_float_equal(input.dir[2], 0.0f, 0.0001f);
	assert_true((input.actionflags & ACTION_JUMP) != 0);
	assert_true((input.actionflags & ACTION_CROUCH) != 0);
	assert_int_equal(ms->jumpreach, 1);

	BotFreeMoveState(handle);
}

/*
=============
test_bot_move_in_direction_barrier_jump_uses_retail_probe

Pins the Q3 vertical-forward-down barrier trace sequence for direct walking.
=============
*/
static void test_bot_move_in_direction_barrier_jump_uses_retail_probe(void **state)
{
	(void)state;

	int handle = BotAllocMoveState();
	assert_true(handle > 0);

	bot_movestate_t *ms = BotMoveStateFromHandle(handle);
	assert_non_null(ms);
	BotResetMoveState(handle);
	ms->client = 0;
	ms->entitynum = 7;
	ms->moveflags = MFL_ONGROUND;
	ms->thinktime = 0.1f;
	VectorSet(ms->origin, 0.0f, 0.0f, 0.0f);

	assert_non_null(Bridge_MaxStep());
	assert_string_equal(Bridge_MaxStep()->name, "sv_step");
	assert_non_null(Bridge_MaxBarrier());
	assert_string_equal(Bridge_MaxBarrier()->name, "sv_maxbarrier");
	LibVarSet("sv_step", "20");

	g_trace_result_count = 3;
	memset(&g_trace_results[0], 0, sizeof(g_trace_results[0]));
	g_trace_results[0].fraction = 1.0f;
	VectorSet(g_trace_results[0].endpos, 0.0f, 0.0f, 50.0f);
	memset(&g_trace_results[1], 0, sizeof(g_trace_results[1]));
	g_trace_results[1].fraction = 1.0f;
	VectorSet(g_trace_results[1].endpos, 10.0f, 0.0f, 50.0f);
	memset(&g_trace_results[2], 0, sizeof(g_trace_results[2]));
	g_trace_results[2].fraction = 0.5f;
	VectorSet(g_trace_results[2].endpos, 10.0f, 0.0f, 24.0f);

	vec3_t dir;
	VectorSet(dir, 1.0f, 0.0f, 0.0f);

	assert_true(BotMoveInDirection(handle, dir, 200.0f, MOVE_WALK));
	assert_int_equal(g_trace_call_count, 3);
	for (int i = 0; i < 3; ++i)
	{
		assert_int_equal(g_trace_passent_log[i], 7);
		assert_int_equal(g_trace_contentmask_log[i], CONTENTS_SOLID | CONTENTS_PLAYERCLIP);
		assert_float_equal(g_trace_mins_log[i][0], -16.0f, 0.0001f);
		assert_float_equal(g_trace_mins_log[i][1], -16.0f, 0.0001f);
		assert_float_equal(g_trace_mins_log[i][2], -24.0f, 0.0001f);
		assert_float_equal(g_trace_maxs_log[i][0], 16.0f, 0.0001f);
		assert_float_equal(g_trace_maxs_log[i][1], 16.0f, 0.0001f);
		assert_float_equal(g_trace_maxs_log[i][2], 32.0f, 0.0001f);
	}
	assert_float_equal(g_trace_end_log[0][2], 50.0f, 0.0001f);
	assert_float_equal(g_trace_start_log[1][2], 50.0f, 0.0001f);
	assert_float_equal(g_trace_end_log[1][0], 10.0f, 0.0001f);
	assert_float_equal(g_trace_end_log[1][2], 50.0f, 0.0001f);
	assert_float_equal(g_trace_start_log[2][0], 10.0f, 0.0001f);
	assert_float_equal(g_trace_end_log[2][2], 0.0f, 0.0001f);

	bot_input_t input;
	assert_int_equal(EA_GetInput(0, 0.1f, &input), BLERR_NOERROR);
	assert_float_equal(input.speed, 200.0f, 0.0001f);
	assert_float_equal(input.dir[0], 1.0f, 0.0001f);
	assert_float_equal(input.dir[1], 0.0f, 0.0001f);
	assert_true((input.actionflags & ACTION_JUMP) != 0);
	assert_true((ms->moveflags & MFL_BARRIERJUMP) != 0);
	assert_int_equal(ms->jumpreach, 1);

	BotFreeMoveState(handle);
}

/*
=============
test_bot_move_in_direction_airborne_barrier_jump_continues_late

Pins Q3's late-airborne barrier-jump movement continuation.
=============
*/
static void test_bot_move_in_direction_airborne_barrier_jump_continues_late(void **state)
{
	(void)state;

	int handle = BotAllocMoveState();
	assert_true(handle > 0);

	bot_movestate_t *ms = BotMoveStateFromHandle(handle);
	assert_non_null(ms);
	BotResetMoveState(handle);
	ms->client = 0;
	ms->entitynum = 3;
	ms->moveflags = MFL_BARRIERJUMP;
	ms->velocity[2] = 40.0f;

	vec3_t dir;
	VectorSet(dir, 0.0f, 2.0f, 0.0f);

	assert_true(BotMoveInDirection(handle, dir, 150.0f, MOVE_WALK));

	bot_input_t input;
	assert_int_equal(EA_GetInput(0, 0.1f, &input), BLERR_NOERROR);
	assert_float_equal(input.speed, 150.0f, 0.0001f);
	assert_float_equal(input.dir[0], 0.0f, 0.0001f);
	assert_float_equal(input.dir[1], 1.0f, 0.0001f);
	assert_true((input.actionflags & ACTION_JUMP) == 0);

	BotFreeMoveState(handle);
}

/*
=============
test_bot_move_in_direction_gap_probe_forces_jump

Pins Q3's direct-walk gap probe that promotes walking into a jump.
=============
*/
static void test_bot_move_in_direction_gap_probe_forces_jump(void **state)
{
	(void)state;

	int handle = BotAllocMoveState();
	assert_true(handle > 0);

	bot_movestate_t *ms = BotMoveStateFromHandle(handle);
	assert_non_null(ms);
	BotResetMoveState(handle);
	ms->client = 0;
	ms->entitynum = 9;
	ms->moveflags = MFL_ONGROUND;
	ms->thinktime = 0.1f;
	VectorSet(ms->origin, 0.0f, 0.0f, 0.0f);

	g_trace_result_count = 3;
	memset(&g_trace_results[0], 0, sizeof(g_trace_results[0]));
	g_trace_results[0].fraction = 1.0f;
	VectorSet(g_trace_results[0].endpos, 0.0f, 0.0f, 0.0f);
	memset(&g_trace_results[1], 0, sizeof(g_trace_results[1]));
	g_trace_results[1].fraction = 0.5f;
	VectorSet(g_trace_results[1].endpos, 0.0f, 0.0f, -16.0f);
	memset(&g_trace_results[2], 0, sizeof(g_trace_results[2]));
	g_trace_results[2].fraction = 0.5f;
	VectorSet(g_trace_results[2].endpos, 8.0f, 0.0f, -60.0f);

	vec3_t dir;
	VectorSet(dir, 1.0f, 0.0f, 0.0f);

	assert_true(BotMoveInDirection(handle, dir, 160.0f, MOVE_WALK));
	assert_int_equal(g_trace_call_count, 3);
	assert_int_equal(g_point_contents_call_count, 2);
	assert_int_equal(g_trace_passent_log[1], 9);
	assert_int_equal(g_trace_contentmask_log[1], CONTENTS_SOLID | CONTENTS_PLAYERCLIP);
	assert_float_equal(g_trace_mins_log[1][0], -15.0f, 0.0001f);
	assert_float_equal(g_trace_mins_log[1][1], -15.0f, 0.0001f);
	assert_float_equal(g_trace_mins_log[1][2], -24.0f, 0.0001f);
	assert_float_equal(g_trace_maxs_log[1][0], 15.0f, 0.0001f);
	assert_float_equal(g_trace_maxs_log[1][1], 15.0f, 0.0001f);
	assert_float_equal(g_trace_maxs_log[1][2], 8.0f, 0.0001f);
	assert_float_equal(g_trace_end_log[1][2], -60.0f, 0.0001f);
	assert_float_equal(g_trace_start_log[2][0], 8.0f, 0.0001f);
	assert_float_equal(g_trace_start_log[2][2], 9.0f, 0.0001f);
	assert_float_equal(g_trace_end_log[2][2], -89.0f, 0.0001f);
	assert_float_equal(g_point_contents_log[1][0], 8.0f, 0.0001f);
	assert_float_equal(g_point_contents_log[1][2], -80.0f, 0.0001f);

	bot_input_t input;
	assert_int_equal(EA_GetInput(0, 0.1f, &input), BLERR_NOERROR);
	assert_float_equal(input.speed, 160.0f, 0.0001f);
	assert_float_equal(input.dir[0], 1.0f, 0.0001f);
	assert_true((input.actionflags & ACTION_JUMP) != 0);
	assert_int_equal(ms->jumpreach, 1);

	BotFreeMoveState(handle);
}

/*
=============
test_bot_move_in_direction_gap_over_water_does_not_jump

Pins Q3's water exception for direct-walk gap probing.
=============
*/
static void test_bot_move_in_direction_gap_over_water_does_not_jump(void **state)
{
	(void)state;

	int handle = BotAllocMoveState();
	assert_true(handle > 0);

	bot_movestate_t *ms = BotMoveStateFromHandle(handle);
	assert_non_null(ms);
	BotResetMoveState(handle);
	ms->client = 0;
	ms->entitynum = 9;
	ms->moveflags = MFL_ONGROUND;
	ms->thinktime = 0.1f;
	VectorSet(ms->origin, 0.0f, 0.0f, 0.0f);

	g_point_contents_result_count = 2;
	g_point_contents_results[0] = 0;
	g_point_contents_results[1] = CONTENTS_WATER;

	g_trace_result_count = 3;
	memset(&g_trace_results[0], 0, sizeof(g_trace_results[0]));
	g_trace_results[0].fraction = 1.0f;
	VectorSet(g_trace_results[0].endpos, 0.0f, 0.0f, 0.0f);
	memset(&g_trace_results[1], 0, sizeof(g_trace_results[1]));
	g_trace_results[1].fraction = 0.5f;
	VectorSet(g_trace_results[1].endpos, 0.0f, 0.0f, -16.0f);
	memset(&g_trace_results[2], 0, sizeof(g_trace_results[2]));
	g_trace_results[2].fraction = 0.5f;
	VectorSet(g_trace_results[2].endpos, 8.0f, 0.0f, -60.0f);

	vec3_t dir;
	VectorSet(dir, 1.0f, 0.0f, 0.0f);

	assert_true(BotMoveInDirection(handle, dir, 160.0f, MOVE_WALK));
	assert_int_equal(g_trace_call_count, 3);
	assert_int_equal(g_point_contents_call_count, 2);

	bot_input_t input;
	assert_int_equal(EA_GetInput(0, 0.1f, &input), BLERR_NOERROR);
	assert_float_equal(input.speed, 160.0f, 0.0001f);
	assert_float_equal(input.dir[0], 1.0f, 0.0001f);
	assert_true((input.actionflags & ACTION_JUMP) == 0);
	assert_int_equal(ms->jumpreach, 0);

	BotFreeMoveState(handle);
}

/*
=============
test_bot_travel_walk_targets_start_and_slows_for_gap

Pins Q3 walk travel's reach-start steering and gap-based speed reduction.
=============
*/
static void test_bot_travel_walk_targets_start_and_slows_for_gap(void **state)
{
	(void)state;

	aasworld.numAreas = 2;
	aasworld.numAreaSettings = 3;
	aasworld.areasettings = calloc((size_t)aasworld.numAreaSettings, sizeof(aas_areasettings_t));
	assert_non_null(aasworld.areasettings);
	aasworld.areasettings[1].areaflags = TEST_AAS_AREAFLAG_GROUNDED;
	aasworld.areasettings[1].firstreachablearea = 1;
	aasworld.areasettings[1].numreachableareas = 1;
	aasworld.areasettings[2].presencetype = PRESENCE_NORMAL;

	aasworld.numReachability = 2;
	aasworld.reachability = calloc((size_t)aasworld.numReachability, sizeof(aas_reachability_t));
	assert_non_null(aasworld.reachability);
	aasworld.reachability[1].areanum = 2;
	aasworld.reachability[1].traveltype = TRAVEL_WALK;
	aasworld.reachability[1].traveltime = 1;
	VectorSet(aasworld.reachability[1].start, 64.0f, 32.0f, 0.0f);
	VectorSet(aasworld.reachability[1].end, 128.0f, 0.0f, 0.0f);
	aasworld.travelflagfortype[TRAVEL_WALK] = TFL_WALK;

	g_trace_result_count = 3;
	memset(&g_trace_results[0], 0, sizeof(g_trace_results[0]));
	g_trace_results[0].fraction = 1.0f;
	memset(&g_trace_results[1], 0, sizeof(g_trace_results[1]));
	g_trace_results[1].fraction = 0.5f;
	VectorSet(g_trace_results[1].endpos, 0.0f, 0.0f, -16.0f);
	memset(&g_trace_results[2], 0, sizeof(g_trace_results[2]));
	g_trace_results[2].fraction = 0.5f;
	VectorSet(g_trace_results[2].endpos, 8.0f, 4.0f, -60.0f);

	int handle = BotAllocMoveState();
	assert_true(handle > 0);
	bot_movestate_t *ms = BotMoveStateFromHandle(handle);
	assert_non_null(ms);
	BotResetMoveState(handle);
	ms->client = 0;
	ms->entitynum = 5;
	ms->areanum = 1;
	VectorSet(ms->origin, 0.0f, 0.0f, 0.0f);

	bot_goal_t goal;
	memset(&goal, 0, sizeof(goal));
	goal.areanum = 2;
	VectorSet(goal.origin, 128.0f, 0.0f, 0.0f);

	bot_moveresult_t result;
	BotMoveToGoal(&result, handle, &goal, TFL_DEFAULT);

	assert_false(result.failure);
	assert_int_equal(result.traveltype, TRAVEL_WALK);
	assert_float_equal(result.movedir[0], 0.8944272f, 0.0001f);
	assert_float_equal(result.movedir[1], 0.4472136f, 0.0001f);
	assert_int_equal(g_trace_call_count, 3);
	assert_float_equal(g_trace_start_log[2][0], 7.1554f, 0.0001f);
	assert_float_equal(g_trace_start_log[2][1], 3.5777f, 0.0001f);
	assert_float_equal(g_trace_start_log[2][2], 9.0f, 0.0001f);
	assert_float_equal(g_trace_end_log[2][2], -89.0f, 0.0001f);

	bot_input_t input;
	assert_int_equal(EA_GetInput(0, 0.1f, &input), BLERR_NOERROR);
	assert_float_equal(input.speed, 56.0f, 0.0001f);
	assert_float_equal(input.dir[0], 0.8944272f, 0.0001f);
	assert_float_equal(input.dir[1], 0.4472136f, 0.0001f);
	assert_true((input.actionflags & ACTION_CROUCH) == 0);

	BotFreeMoveState(handle);
	free(aasworld.areasettings);
	free(aasworld.reachability);
	aasworld.areasettings = NULL;
	aasworld.reachability = NULL;
	aasworld.numAreas = 0;
	aasworld.numAreaSettings = 0;
	aasworld.numReachability = 0;
}

/*
=============
test_bot_travel_walk_switches_to_end_and_crouches_near_crouch_area

Pins Q3 walk travel's near-start end steering and crouch-area action.
=============
*/
static void test_bot_travel_walk_switches_to_end_and_crouches_near_crouch_area(void **state)
{
	(void)state;

	aasworld.numAreas = 2;
	aasworld.numAreaSettings = 3;
	aasworld.areasettings = calloc((size_t)aasworld.numAreaSettings, sizeof(aas_areasettings_t));
	assert_non_null(aasworld.areasettings);
	aasworld.areasettings[1].areaflags = TEST_AAS_AREAFLAG_GROUNDED;
	aasworld.areasettings[1].firstreachablearea = 1;
	aasworld.areasettings[1].numreachableareas = 1;
	aasworld.areasettings[2].presencetype = PRESENCE_CROUCH;

	aasworld.numReachability = 2;
	aasworld.reachability = calloc((size_t)aasworld.numReachability, sizeof(aas_reachability_t));
	assert_non_null(aasworld.reachability);
	aasworld.reachability[1].areanum = 2;
	aasworld.reachability[1].traveltype = TRAVEL_WALK;
	aasworld.reachability[1].traveltime = 1;
	VectorSet(aasworld.reachability[1].start, 4.0f, 0.0f, 0.0f);
	VectorSet(aasworld.reachability[1].end, 0.0f, 6.0f, 0.0f);
	aasworld.travelflagfortype[TRAVEL_WALK] = TFL_WALK;

	g_point_contents_result_count = 2;
	g_point_contents_results[0] = 0;
	g_point_contents_results[1] = CONTENTS_WATER;

	g_trace_result_count = 3;
	memset(&g_trace_results[0], 0, sizeof(g_trace_results[0]));
	g_trace_results[0].fraction = 1.0f;
	memset(&g_trace_results[1], 0, sizeof(g_trace_results[1]));
	g_trace_results[1].fraction = 0.5f;
	VectorSet(g_trace_results[1].endpos, 0.0f, 0.0f, -16.0f);
	memset(&g_trace_results[2], 0, sizeof(g_trace_results[2]));
	g_trace_results[2].fraction = 0.5f;
	VectorSet(g_trace_results[2].endpos, 0.0f, 8.0f, -60.0f);

	int handle = BotAllocMoveState();
	assert_true(handle > 0);
	bot_movestate_t *ms = BotMoveStateFromHandle(handle);
	assert_non_null(ms);
	BotResetMoveState(handle);
	ms->client = 0;
	ms->entitynum = 5;
	ms->areanum = 1;
	VectorSet(ms->origin, 0.0f, 0.0f, 0.0f);

	bot_goal_t goal;
	memset(&goal, 0, sizeof(goal));
	goal.areanum = 2;
	VectorSet(goal.origin, 0.0f, 6.0f, 0.0f);

	bot_moveresult_t result;
	BotMoveToGoal(&result, handle, &goal, TFL_DEFAULT);

	assert_false(result.failure);
	assert_int_equal(result.traveltype, TRAVEL_WALK);
	assert_float_equal(result.movedir[0], 0.0f, 0.0001f);
	assert_float_equal(result.movedir[1], 1.0f, 0.0001f);

	bot_input_t input;
	assert_int_equal(EA_GetInput(0, 0.1f, &input), BLERR_NOERROR);
	assert_float_equal(input.speed, 400.0f, 0.0001f);
	assert_float_equal(input.dir[0], 0.0f, 0.0001f);
	assert_float_equal(input.dir[1], 1.0f, 0.0001f);
	assert_true((input.actionflags & ACTION_CROUCH) != 0);

	BotFreeMoveState(handle);
	free(aasworld.areasettings);
	free(aasworld.reachability);
	aasworld.areasettings = NULL;
	aasworld.reachability = NULL;
	aasworld.numAreas = 0;
	aasworld.numAreaSettings = 0;
	aasworld.numReachability = 0;
}

/*
=============
test_bot_travel_crouch_uses_retail_speed

Pins Q3 crouch travel's end steering, blocked probe, crouch action, and fixed
400-unit movement speed.
=============
*/
static void test_bot_travel_crouch_uses_retail_speed(void **state)
{
	(void)state;

	aasworld.numAreas = 2;
	aasworld.numAreaSettings = 3;
	aasworld.areasettings = calloc((size_t)aasworld.numAreaSettings, sizeof(aas_areasettings_t));
	assert_non_null(aasworld.areasettings);
	aasworld.areasettings[1].areaflags = TEST_AAS_AREAFLAG_GROUNDED;
	aasworld.areasettings[1].firstreachablearea = 1;
	aasworld.areasettings[1].numreachableareas = 1;

	aasworld.numReachability = 2;
	aasworld.reachability = calloc((size_t)aasworld.numReachability, sizeof(aas_reachability_t));
	assert_non_null(aasworld.reachability);
	aasworld.reachability[1].areanum = 2;
	aasworld.reachability[1].traveltype = TRAVEL_CROUCH;
	aasworld.reachability[1].traveltime = 1;
	VectorSet(aasworld.reachability[1].end, 30.0f, 40.0f, 0.0f);
	aasworld.travelflagfortype[TRAVEL_CROUCH] = TFL_CROUCH;

	g_trace_result_count = 1;
	memset(&g_trace_results[0], 0, sizeof(g_trace_results[0]));
	g_trace_results[0].fraction = 1.0f;

	int handle = BotAllocMoveState();
	assert_true(handle > 0);
	bot_movestate_t *ms = BotMoveStateFromHandle(handle);
	assert_non_null(ms);
	BotResetMoveState(handle);
	ms->client = 0;
	ms->entitynum = 5;
	ms->areanum = 1;
	VectorClear(ms->origin);

	bot_goal_t goal;
	memset(&goal, 0, sizeof(goal));
	goal.areanum = 2;
	VectorSet(goal.origin, 30.0f, 40.0f, 0.0f);

	bot_moveresult_t result;
	BotMoveToGoal(&result, handle, &goal, TFL_DEFAULT);

	assert_false(result.failure);
	assert_int_equal(result.traveltype, TRAVEL_CROUCH);
	assert_float_equal(result.movedir[0], 0.6f, 0.0001f);
	assert_float_equal(result.movedir[1], 0.8f, 0.0001f);
	assert_int_equal(g_trace_call_count, 1);
	assert_float_equal(g_trace_end_log[0][0], 1.8f, 0.0001f);
	assert_float_equal(g_trace_end_log[0][1], 2.4f, 0.0001f);

	bot_input_t input;
	assert_int_equal(EA_GetInput(0, 0.1f, &input), BLERR_NOERROR);
	assert_float_equal(input.speed, 400.0f, 0.0001f);
	assert_float_equal(input.dir[0], 0.6f, 0.0001f);
	assert_float_equal(input.dir[1], 0.8f, 0.0001f);
	assert_true((input.actionflags & ACTION_CROUCH) != 0);

	BotFreeMoveState(handle);
	free(aasworld.areasettings);
	free(aasworld.reachability);
	aasworld.areasettings = NULL;
	aasworld.reachability = NULL;
	aasworld.numAreas = 0;
	aasworld.numAreaSettings = 0;
	aasworld.numReachability = 0;
}

/*
=============
test_bot_travel_barrier_jump_approaches_start_until_close

Pins Q3 barrier-jump travel's approach-to-start branch and distance-based
speed cap before the actual jump range.
=============
*/
static void test_bot_travel_barrier_jump_approaches_start_until_close(void **state)
{
	(void)state;

	aasworld.numAreas = 2;
	aasworld.numAreaSettings = 3;
	aasworld.areasettings = calloc((size_t)aasworld.numAreaSettings, sizeof(aas_areasettings_t));
	assert_non_null(aasworld.areasettings);
	aasworld.areasettings[1].areaflags = TEST_AAS_AREAFLAG_GROUNDED;
	aasworld.areasettings[1].firstreachablearea = 1;
	aasworld.areasettings[1].numreachableareas = 1;

	aasworld.numReachability = 2;
	aasworld.reachability = calloc((size_t)aasworld.numReachability, sizeof(aas_reachability_t));
	assert_non_null(aasworld.reachability);
	aasworld.reachability[1].areanum = 2;
	aasworld.reachability[1].traveltype = TRAVEL_BARRIERJUMP;
	aasworld.reachability[1].traveltime = 1;
	VectorSet(aasworld.reachability[1].start, 30.0f, 40.0f, 0.0f);
	VectorSet(aasworld.reachability[1].end, 30.0f, 40.0f, 32.0f);
	aasworld.travelflagfortype[TRAVEL_BARRIERJUMP] = TFL_BARRIERJUMP;

	g_trace_result_count = 1;
	memset(&g_trace_results[0], 0, sizeof(g_trace_results[0]));
	g_trace_results[0].fraction = 1.0f;

	int handle = BotAllocMoveState();
	assert_true(handle > 0);
	bot_movestate_t *ms = BotMoveStateFromHandle(handle);
	assert_non_null(ms);
	BotResetMoveState(handle);
	ms->client = 0;
	ms->entitynum = 5;
	ms->areanum = 1;
	VectorClear(ms->origin);

	bot_goal_t goal;
	memset(&goal, 0, sizeof(goal));
	goal.areanum = 2;
	VectorSet(goal.origin, 30.0f, 40.0f, 32.0f);

	bot_moveresult_t result;
	BotMoveToGoal(&result, handle, &goal, TFL_DEFAULT);

	assert_false(result.failure);
	assert_int_equal(result.traveltype, TRAVEL_BARRIERJUMP);
	assert_float_equal(result.movedir[0], 0.6f, 0.0001f);
	assert_float_equal(result.movedir[1], 0.8f, 0.0001f);
	assert_int_equal(g_trace_call_count, 1);

	bot_input_t input;
	assert_int_equal(EA_GetInput(0, 0.1f, &input), BLERR_NOERROR);
	assert_float_equal(input.speed, 300.0f, 0.0001f);
	assert_float_equal(input.dir[0], 0.6f, 0.0001f);
	assert_float_equal(input.dir[1], 0.8f, 0.0001f);
	assert_true((input.actionflags & ACTION_JUMP) == 0);
	assert_true((ms->moveflags & MFL_BARRIERJUMP) == 0);
	assert_int_equal(ms->jumpreach, 0);

	BotFreeMoveState(handle);
	free(aasworld.areasettings);
	free(aasworld.reachability);
	aasworld.areasettings = NULL;
	aasworld.reachability = NULL;
	aasworld.numAreas = 0;
	aasworld.numAreaSettings = 0;
	aasworld.numReachability = 0;
}

/*
=============
test_bot_travel_barrier_jump_jumps_when_close

Pins Q3 barrier-jump travel's close-range jump trigger.
=============
*/
static void test_bot_travel_barrier_jump_jumps_when_close(void **state)
{
	(void)state;

	aasworld.numAreas = 2;
	aasworld.numAreaSettings = 3;
	aasworld.areasettings = calloc((size_t)aasworld.numAreaSettings, sizeof(aas_areasettings_t));
	assert_non_null(aasworld.areasettings);
	aasworld.areasettings[1].areaflags = TEST_AAS_AREAFLAG_GROUNDED;
	aasworld.areasettings[1].firstreachablearea = 1;
	aasworld.areasettings[1].numreachableareas = 1;

	aasworld.numReachability = 2;
	aasworld.reachability = calloc((size_t)aasworld.numReachability, sizeof(aas_reachability_t));
	assert_non_null(aasworld.reachability);
	aasworld.reachability[1].areanum = 2;
	aasworld.reachability[1].traveltype = TRAVEL_BARRIERJUMP;
	aasworld.reachability[1].traveltime = 1;
	VectorSet(aasworld.reachability[1].start, 6.0f, 0.0f, 0.0f);
	VectorSet(aasworld.reachability[1].end, 6.0f, 0.0f, 32.0f);
	aasworld.travelflagfortype[TRAVEL_BARRIERJUMP] = TFL_BARRIERJUMP;

	g_trace_result_count = 1;
	memset(&g_trace_results[0], 0, sizeof(g_trace_results[0]));
	g_trace_results[0].fraction = 1.0f;

	int handle = BotAllocMoveState();
	assert_true(handle > 0);
	bot_movestate_t *ms = BotMoveStateFromHandle(handle);
	assert_non_null(ms);
	BotResetMoveState(handle);
	ms->client = 0;
	ms->entitynum = 5;
	ms->areanum = 1;
	VectorClear(ms->origin);

	bot_goal_t goal;
	memset(&goal, 0, sizeof(goal));
	goal.areanum = 2;
	VectorSet(goal.origin, 6.0f, 0.0f, 32.0f);

	bot_moveresult_t result;
	BotMoveToGoal(&result, handle, &goal, TFL_DEFAULT);

	assert_false(result.failure);
	assert_int_equal(result.traveltype, TRAVEL_BARRIERJUMP);
	assert_float_equal(result.movedir[0], 1.0f, 0.0001f);
	assert_float_equal(result.movedir[1], 0.0f, 0.0001f);
	assert_int_equal(g_trace_call_count, 1);

	bot_input_t input;
	assert_int_equal(EA_GetInput(0, 0.1f, &input), BLERR_NOERROR);
	assert_float_equal(input.speed, 0.0f, 0.0001f);
	assert_true((input.actionflags & ACTION_JUMP) != 0);
	assert_true((ms->moveflags & MFL_BARRIERJUMP) != 0);
	assert_int_equal(ms->jumpreach, 1);

	BotFreeMoveState(handle);
	free(aasworld.areasettings);
	free(aasworld.reachability);
	aasworld.areasettings = NULL;
	aasworld.reachability = NULL;
	aasworld.numAreas = 0;
	aasworld.numAreaSettings = 0;
	aasworld.numReachability = 0;
}

/*
=============
test_bot_travel_walkoffledge_far_uses_endpoint_at_retail_speed

Pins the far walk-off-ledge branch: probe twice, steer to the endpoint, and
move at the fixed retail 400 speed.
=============
*/
static void test_bot_travel_walkoffledge_far_uses_endpoint_at_retail_speed(void **state)
{
	(void)state;

	aasworld.numAreas = 2;
	aasworld.numAreaSettings = 3;
	aasworld.areasettings = calloc((size_t)aasworld.numAreaSettings, sizeof(aas_areasettings_t));
	assert_non_null(aasworld.areasettings);
	aasworld.areasettings[1].areaflags = TEST_AAS_AREAFLAG_GROUNDED;
	aasworld.areasettings[1].firstreachablearea = 1;
	aasworld.areasettings[1].numreachableareas = 1;

	aasworld.numReachability = 2;
	aasworld.reachability = calloc((size_t)aasworld.numReachability, sizeof(aas_reachability_t));
	assert_non_null(aasworld.reachability);
	aasworld.reachability[1].areanum = 2;
	aasworld.reachability[1].traveltype = TRAVEL_WALKOFFLEDGE;
	aasworld.reachability[1].traveltime = 1;
	VectorSet(aasworld.reachability[1].start, 64.0f, 0.0f, 0.0f);
	VectorSet(aasworld.reachability[1].end, 128.0f, 0.0f, -32.0f);
	aasworld.travelflagfortype[TRAVEL_WALKOFFLEDGE] = TFL_WALKOFFLEDGE;

	g_trace_result_count = 2;
	memset(&g_trace_results[0], 0, sizeof(g_trace_results[0]));
	g_trace_results[0].fraction = 1.0f;
	memset(&g_trace_results[1], 0, sizeof(g_trace_results[1]));
	g_trace_results[1].fraction = 1.0f;

	int handle = BotAllocMoveState();
	assert_true(handle > 0);
	bot_movestate_t *ms = BotMoveStateFromHandle(handle);
	assert_non_null(ms);
	BotResetMoveState(handle);
	ms->client = 0;
	ms->entitynum = 5;
	ms->areanum = 1;
	VectorClear(ms->origin);

	bot_goal_t goal;
	memset(&goal, 0, sizeof(goal));
	goal.areanum = 2;
	VectorSet(goal.origin, 128.0f, 0.0f, -32.0f);

	bot_moveresult_t result;
	BotMoveToGoal(&result, handle, &goal, TFL_DEFAULT);

	assert_false(result.failure);
	assert_int_equal(result.traveltype, TRAVEL_WALKOFFLEDGE);
	assert_float_equal(result.movedir[0], 1.0f, 0.0001f);
	assert_float_equal(result.movedir[1], 0.0f, 0.0001f);
	assert_int_equal(g_trace_call_count, 2);
	assert_float_equal(g_trace_end_log[1][0], 3.0f, 0.0001f);
	assert_float_equal(g_trace_end_log[1][2], 0.0f, 0.0001f);

	bot_input_t input;
	assert_int_equal(EA_GetInput(0, 0.1f, &input), BLERR_NOERROR);
	assert_float_equal(input.speed, 400.0f, 0.0001f);
	assert_float_equal(input.dir[0], 1.0f, 0.0001f);
	assert_true((input.actionflags & ACTION_JUMP) == 0);

	BotFreeMoveState(handle);
	free(aasworld.areasettings);
	free(aasworld.reachability);
	aasworld.areasettings = NULL;
	aasworld.reachability = NULL;
	aasworld.numAreas = 0;
	aasworld.numAreaSettings = 0;
	aasworld.numReachability = 0;
}

/*
=============
test_bot_travel_walkoffledge_uses_jump_fall_velocity

Pins the near walk-off-ledge branch that computes horizontal fall velocity from
the reachability start/end pair.
=============
*/
static void test_bot_travel_walkoffledge_uses_jump_fall_velocity(void **state)
{
	(void)state;

	aasworld.numAreas = 2;
	aasworld.numAreaSettings = 3;
	aasworld.areasettings = calloc((size_t)aasworld.numAreaSettings, sizeof(aas_areasettings_t));
	assert_non_null(aasworld.areasettings);
	aasworld.areasettings[1].areaflags = TEST_AAS_AREAFLAG_GROUNDED;
	aasworld.areasettings[1].firstreachablearea = 1;
	aasworld.areasettings[1].numreachableareas = 1;

	aasworld.numReachability = 2;
	aasworld.reachability = calloc((size_t)aasworld.numReachability, sizeof(aas_reachability_t));
	assert_non_null(aasworld.reachability);
	aasworld.reachability[1].areanum = 2;
	aasworld.reachability[1].traveltype = TRAVEL_WALKOFFLEDGE;
	aasworld.reachability[1].traveltime = 1;
	VectorSet(aasworld.reachability[1].start, 0.0f, 0.0f, 0.0f);
	VectorSet(aasworld.reachability[1].end, 40.0f, 0.0f, -64.0f);
	aasworld.travelflagfortype[TRAVEL_WALKOFFLEDGE] = TFL_WALKOFFLEDGE;

	g_trace_result_count = 2;
	memset(&g_trace_results[0], 0, sizeof(g_trace_results[0]));
	g_trace_results[0].fraction = 1.0f;
	memset(&g_trace_results[1], 0, sizeof(g_trace_results[1]));
	g_trace_results[1].fraction = 1.0f;

	int handle = BotAllocMoveState();
	assert_true(handle > 0);
	bot_movestate_t *ms = BotMoveStateFromHandle(handle);
	assert_non_null(ms);
	BotResetMoveState(handle);
	ms->client = 0;
	ms->entitynum = 5;
	ms->areanum = 1;
	VectorClear(ms->origin);

	bot_goal_t goal;
	memset(&goal, 0, sizeof(goal));
	goal.areanum = 2;
	VectorSet(goal.origin, 40.0f, 0.0f, -64.0f);

	bot_moveresult_t result;
	BotMoveToGoal(&result, handle, &goal, TFL_DEFAULT);

	assert_false(result.failure);
	assert_int_equal(result.traveltype, TRAVEL_WALKOFFLEDGE);
	assert_int_equal(g_trace_call_count, 2);

	bot_input_t input;
	assert_int_equal(EA_GetInput(0, 0.1f, &input), BLERR_NOERROR);
	assert_float_equal(input.speed, 100.0f, 0.0001f);
	assert_float_equal(input.dir[0], 1.0f, 0.0001f);
	assert_float_equal(input.dir[1], 0.0f, 0.0001f);

	BotFreeMoveState(handle);
	free(aasworld.areasettings);
	free(aasworld.reachability);
	aasworld.areasettings = NULL;
	aasworld.reachability = NULL;
	aasworld.numAreas = 0;
	aasworld.numAreaSettings = 0;
	aasworld.numReachability = 0;
}

/*
=============
test_bot_travel_walkoffledge_short_edge_scales_speed

Pins Gladiator's short walk-off-ledge speed formula for nearly vertical edges.
=============
*/
static void test_bot_travel_walkoffledge_short_edge_scales_speed(void **state)
{
	(void)state;

	aasworld.numAreas = 2;
	aasworld.numAreaSettings = 3;
	aasworld.areasettings = calloc((size_t)aasworld.numAreaSettings, sizeof(aas_areasettings_t));
	assert_non_null(aasworld.areasettings);
	aasworld.areasettings[1].areaflags = TEST_AAS_AREAFLAG_GROUNDED;
	aasworld.areasettings[1].firstreachablearea = 1;
	aasworld.areasettings[1].numreachableareas = 1;

	aasworld.numReachability = 2;
	aasworld.reachability = calloc((size_t)aasworld.numReachability, sizeof(aas_reachability_t));
	assert_non_null(aasworld.reachability);
	aasworld.reachability[1].areanum = 2;
	aasworld.reachability[1].traveltype = TRAVEL_WALKOFFLEDGE;
	aasworld.reachability[1].traveltime = 1;
	VectorSet(aasworld.reachability[1].start, 0.0f, 0.0f, 0.0f);
	VectorSet(aasworld.reachability[1].end, 10.0f, 0.0f, -32.0f);
	aasworld.travelflagfortype[TRAVEL_WALKOFFLEDGE] = TFL_WALKOFFLEDGE;

	g_trace_result_count = 2;
	memset(&g_trace_results[0], 0, sizeof(g_trace_results[0]));
	g_trace_results[0].fraction = 1.0f;
	memset(&g_trace_results[1], 0, sizeof(g_trace_results[1]));
	g_trace_results[1].fraction = 1.0f;

	int handle = BotAllocMoveState();
	assert_true(handle > 0);
	bot_movestate_t *ms = BotMoveStateFromHandle(handle);
	assert_non_null(ms);
	BotResetMoveState(handle);
	ms->client = 0;
	ms->entitynum = 5;
	ms->areanum = 1;
	VectorSet(ms->origin, -10.0f, 0.0f, 0.0f);

	bot_goal_t goal;
	memset(&goal, 0, sizeof(goal));
	goal.areanum = 2;
	VectorSet(goal.origin, 10.0f, 0.0f, -32.0f);

	bot_moveresult_t result;
	BotMoveToGoal(&result, handle, &goal, TFL_DEFAULT);

	assert_false(result.failure);
	assert_int_equal(result.traveltype, TRAVEL_WALKOFFLEDGE);
	assert_int_equal(g_trace_call_count, 2);

	bot_input_t input;
	assert_int_equal(EA_GetInput(0, 0.1f, &input), BLERR_NOERROR);
	assert_float_equal(input.speed, 120.0f, 0.0001f);
	assert_float_equal(input.dir[0], 1.0f, 0.0001f);

	BotFreeMoveState(handle);
	free(aasworld.areasettings);
	free(aasworld.reachability);
	aasworld.areasettings = NULL;
	aasworld.reachability = NULL;
	aasworld.numAreas = 0;
	aasworld.numAreaSettings = 0;
	aasworld.numReachability = 0;
}

/*
=============
test_bot_travel_ladder_uses_forward_action_and_view

Pins Q3 ladder travel: focus the movement view and submit move-forward with no
velocity move.
=============
*/
static void test_bot_travel_ladder_uses_forward_action_and_view(void **state)
{
	(void)state;

	aasworld.numAreas = 2;
	aasworld.numAreaSettings = 3;
	aasworld.areasettings = calloc((size_t)aasworld.numAreaSettings, sizeof(aas_areasettings_t));
	assert_non_null(aasworld.areasettings);
	aasworld.areasettings[1].areaflags = TEST_AAS_AREAFLAG_GROUNDED;
	aasworld.areasettings[1].firstreachablearea = 1;
	aasworld.areasettings[1].numreachableareas = 1;

	aasworld.numReachability = 2;
	aasworld.reachability = calloc((size_t)aasworld.numReachability, sizeof(aas_reachability_t));
	assert_non_null(aasworld.reachability);
	aasworld.reachability[1].areanum = 2;
	aasworld.reachability[1].traveltype = TRAVEL_LADDER;
	aasworld.reachability[1].traveltime = 1;
	VectorSet(aasworld.reachability[1].end, 0.0f, 0.0f, 64.0f);
	aasworld.travelflagfortype[TRAVEL_LADDER] = TFL_LADDER;

	int handle = BotAllocMoveState();
	assert_true(handle > 0);
	bot_movestate_t *ms = BotMoveStateFromHandle(handle);
	assert_non_null(ms);
	BotResetMoveState(handle);
	ms->client = 0;
	ms->entitynum = 5;
	ms->areanum = 1;
	VectorClear(ms->origin);

	bot_goal_t goal;
	memset(&goal, 0, sizeof(goal));
	goal.areanum = 2;
	VectorSet(goal.origin, 0.0f, 0.0f, 64.0f);

	bot_moveresult_t result;
	BotMoveToGoal(&result, handle, &goal, TFL_DEFAULT);

	assert_false(result.failure);
	assert_int_equal(result.traveltype, TRAVEL_LADDER);
	assert_true((result.flags & MOVERESULT_MOVEMENTVIEW) != 0);
	assert_float_equal(result.movedir[0], 0.0f, 0.0001f);
	assert_float_equal(result.movedir[1], 0.0f, 0.0001f);
	assert_float_equal(result.movedir[2], 1.0f, 0.0001f);
	assert_int_equal(g_trace_call_count, 0);

	bot_input_t input;
	assert_int_equal(EA_GetInput(0, 0.1f, &input), BLERR_NOERROR);
	assert_float_equal(input.speed, 0.0f, 0.0001f);
	assert_float_equal(input.dir[0], 0.0f, 0.0001f);
	assert_true((input.actionflags & ACTION_MOVEFORWARD) != 0);

	BotFreeMoveState(handle);
	free(aasworld.areasettings);
	free(aasworld.reachability);
	aasworld.areasettings = NULL;
	aasworld.reachability = NULL;
	aasworld.numAreas = 0;
	aasworld.numAreaSettings = 0;
	aasworld.numReachability = 0;
}

/*
=============
test_bot_travel_swim_targets_start_at_retail_speed

Pins Q3 swim travel's reach-start steering and fixed 400 speed.
=============
*/
static void test_bot_travel_swim_targets_start_at_retail_speed(void **state)
{
	(void)state;

	aasworld.numAreas = 2;
	aasworld.numAreaSettings = 3;
	aasworld.areasettings = calloc((size_t)aasworld.numAreaSettings, sizeof(aas_areasettings_t));
	assert_non_null(aasworld.areasettings);
	aasworld.areasettings[1].areaflags = TEST_AAS_AREAFLAG_GROUNDED;
	aasworld.areasettings[1].firstreachablearea = 1;
	aasworld.areasettings[1].numreachableareas = 1;

	aasworld.numReachability = 2;
	aasworld.reachability = calloc((size_t)aasworld.numReachability, sizeof(aas_reachability_t));
	assert_non_null(aasworld.reachability);
	aasworld.reachability[1].areanum = 2;
	aasworld.reachability[1].traveltype = TRAVEL_SWIM;
	aasworld.reachability[1].traveltime = 1;
	VectorSet(aasworld.reachability[1].start, 30.0f, 40.0f, 10.0f);
	VectorSet(aasworld.reachability[1].end, 80.0f, 0.0f, 0.0f);
	aasworld.travelflagfortype[TRAVEL_SWIM] = TFL_SWIM;

	g_trace_result_count = 1;
	memset(&g_trace_results[0], 0, sizeof(g_trace_results[0]));
	g_trace_results[0].fraction = 1.0f;

	int handle = BotAllocMoveState();
	assert_true(handle > 0);
	bot_movestate_t *ms = BotMoveStateFromHandle(handle);
	assert_non_null(ms);
	BotResetMoveState(handle);
	ms->client = 0;
	ms->entitynum = 5;
	ms->areanum = 1;
	VectorClear(ms->origin);

	bot_goal_t goal;
	memset(&goal, 0, sizeof(goal));
	goal.areanum = 2;
	VectorSet(goal.origin, 80.0f, 0.0f, 0.0f);

	bot_moveresult_t result;
	BotMoveToGoal(&result, handle, &goal, TFL_DEFAULT);

	assert_false(result.failure);
	assert_int_equal(result.traveltype, TRAVEL_SWIM);
	assert_true((result.flags & MOVERESULT_SWIMVIEW) != 0);
	assert_float_equal(result.movedir[0], 0.5883484f, 0.0001f);
	assert_float_equal(result.movedir[1], 0.7844645f, 0.0001f);
	assert_float_equal(result.movedir[2], 0.1961161f, 0.0001f);
	assert_int_equal(g_trace_call_count, 1);
	assert_float_equal(g_trace_end_log[0][0], 1.7650f, 0.0001f);
	assert_float_equal(g_trace_end_log[0][1], 2.3534f, 0.0001f);

	bot_input_t input;
	assert_int_equal(EA_GetInput(0, 0.1f, &input), BLERR_NOERROR);
	assert_float_equal(input.speed, 400.0f, 0.0001f);
	assert_float_equal(input.dir[0], 0.5883484f, 0.0001f);
	assert_float_equal(input.dir[1], 0.7844645f, 0.0001f);
	assert_float_equal(input.dir[2], 0.1961161f, 0.0001f);

	BotFreeMoveState(handle);
	free(aasworld.areasettings);
	free(aasworld.reachability);
	aasworld.areasettings = NULL;
	aasworld.reachability = NULL;
	aasworld.numAreas = 0;
	aasworld.numAreaSettings = 0;
	aasworld.numReachability = 0;
}

/*
=============
test_bot_travel_waterjump_submits_forward_and_up_actions

Pins Q3 water-jump travel's action-based movement near the exit point.
=============
*/
static void test_bot_travel_waterjump_submits_forward_and_up_actions(void **state)
{
	(void)state;

	srand(0);

	aasworld.numAreas = 2;
	aasworld.numAreaSettings = 3;
	aasworld.areasettings = calloc((size_t)aasworld.numAreaSettings, sizeof(aas_areasettings_t));
	assert_non_null(aasworld.areasettings);
	aasworld.areasettings[1].areaflags = TEST_AAS_AREAFLAG_GROUNDED;
	aasworld.areasettings[1].firstreachablearea = 1;
	aasworld.areasettings[1].numreachableareas = 1;

	aasworld.numReachability = 2;
	aasworld.reachability = calloc((size_t)aasworld.numReachability, sizeof(aas_reachability_t));
	assert_non_null(aasworld.reachability);
	aasworld.reachability[1].areanum = 2;
	aasworld.reachability[1].traveltype = TRAVEL_WATERJUMP;
	aasworld.reachability[1].traveltime = 1;
	VectorSet(aasworld.reachability[1].end, 24.0f, 0.0f, 0.0f);
	aasworld.travelflagfortype[TRAVEL_WATERJUMP] = TFL_WATERJUMP;

	int handle = BotAllocMoveState();
	assert_true(handle > 0);
	bot_movestate_t *ms = BotMoveStateFromHandle(handle);
	assert_non_null(ms);
	BotResetMoveState(handle);
	ms->client = 0;
	ms->entitynum = 5;
	ms->areanum = 1;
	VectorClear(ms->origin);

	bot_goal_t goal;
	memset(&goal, 0, sizeof(goal));
	goal.areanum = 2;
	VectorSet(goal.origin, 24.0f, 0.0f, 0.0f);

	bot_moveresult_t result;
	BotMoveToGoal(&result, handle, &goal, TFL_DEFAULT);

	assert_false(result.failure);
	assert_int_equal(result.traveltype, TRAVEL_WATERJUMP);
	assert_true((result.flags & MOVERESULT_MOVEMENTVIEW) != 0);
	assert_true(result.movedir[0] > 0.4f);
	assert_int_equal(g_trace_call_count, 0);

	bot_input_t input;
	assert_int_equal(EA_GetInput(0, 0.1f, &input), BLERR_NOERROR);
	assert_float_equal(input.speed, 0.0f, 0.0001f);
	assert_true((input.actionflags & ACTION_MOVEFORWARD) != 0);
	assert_true((input.actionflags & ACTION_MOVEUP) != 0);

	BotFreeMoveState(handle);
	free(aasworld.areasettings);
	free(aasworld.reachability);
	aasworld.areasettings = NULL;
	aasworld.reachability = NULL;
	aasworld.numAreas = 0;
	aasworld.numAreaSettings = 0;
	aasworld.numReachability = 0;
}

/*
=============
test_bot_travel_teleport_approaches_start_and_slows_near

Pins active teleport travel before the teleport flag is reported.
=============
*/
static void test_bot_travel_teleport_approaches_start_and_slows_near(void **state)
{
	(void)state;

	aasworld.numAreas = 2;
	aasworld.numAreaSettings = 3;
	aasworld.areasettings = calloc((size_t)aasworld.numAreaSettings, sizeof(aas_areasettings_t));
	assert_non_null(aasworld.areasettings);
	aasworld.areasettings[1].areaflags = TEST_AAS_AREAFLAG_GROUNDED;
	aasworld.areasettings[1].firstreachablearea = 1;
	aasworld.areasettings[1].numreachableareas = 1;

	aasworld.numReachability = 2;
	aasworld.reachability = calloc((size_t)aasworld.numReachability, sizeof(aas_reachability_t));
	assert_non_null(aasworld.reachability);
	aasworld.reachability[1].areanum = 2;
	aasworld.reachability[1].traveltype = TRAVEL_TELEPORT;
	aasworld.reachability[1].traveltime = 1;
	VectorSet(aasworld.reachability[1].start, 20.0f, 0.0f, 12.0f);
	VectorSet(aasworld.reachability[1].end, 128.0f, 0.0f, 32.0f);
	aasworld.travelflagfortype[TRAVEL_TELEPORT] = TFL_TELEPORT;

	g_trace_result_count = 1;
	memset(&g_trace_results[0], 0, sizeof(g_trace_results[0]));
	g_trace_results[0].fraction = 1.0f;

	int handle = BotAllocMoveState();
	assert_true(handle > 0);
	bot_movestate_t *ms = BotMoveStateFromHandle(handle);
	assert_non_null(ms);
	BotResetMoveState(handle);
	ms->client = 0;
	ms->entitynum = 5;
	ms->areanum = 1;
	VectorClear(ms->origin);

	bot_goal_t goal;
	memset(&goal, 0, sizeof(goal));
	goal.areanum = 2;
	VectorSet(goal.origin, 128.0f, 0.0f, 32.0f);

	bot_moveresult_t result;
	BotMoveToGoal(&result, handle, &goal, TFL_DEFAULT);

	assert_false(result.failure);
	assert_int_equal(result.traveltype, TRAVEL_TELEPORT);
	assert_float_equal(result.movedir[0], 1.0f, 0.0001f);
	assert_float_equal(result.movedir[1], 0.0f, 0.0001f);
	assert_float_equal(result.movedir[2], 0.0f, 0.0001f);
	assert_int_equal(g_trace_call_count, 1);
	assert_float_equal(g_trace_end_log[0][0], 3.0f, 0.0001f);
	assert_float_equal(g_trace_end_log[0][2], 0.0f, 0.0001f);

	bot_input_t input;
	assert_int_equal(EA_GetInput(0, 0.1f, &input), BLERR_NOERROR);
	assert_float_equal(input.speed, 200.0f, 0.0001f);
	assert_float_equal(input.dir[0], 1.0f, 0.0001f);
	assert_true((result.flags & MOVERESULT_SWIMVIEW) == 0);

	BotFreeMoveState(handle);
	free(aasworld.areasettings);
	free(aasworld.reachability);
	aasworld.areasettings = NULL;
	aasworld.reachability = NULL;
	aasworld.numAreas = 0;
	aasworld.numAreaSettings = 0;
	aasworld.numReachability = 0;
}

/*
=============
test_bot_travel_jump_runs_to_runstart

Pins active jump travel's run-up branch before the bot reaches the launch
window.
=============
*/
static void test_bot_travel_jump_runs_to_runstart(void **state)
{
	(void)state;

	aasworld.numAreas = 2;
	aasworld.areas = calloc(3, sizeof(aas_area_t));
	assert_non_null(aasworld.areas);
	aasworld.numAreaSettings = 3;
	aasworld.areasettings = calloc((size_t)aasworld.numAreaSettings, sizeof(aas_areasettings_t));
	assert_non_null(aasworld.areasettings);
	aasworld.areasettings[1].areaflags = TEST_AAS_AREAFLAG_GROUNDED;
	aasworld.areasettings[1].firstreachablearea = 1;
	aasworld.areasettings[1].numreachableareas = 1;
	VectorSet(aasworld.areas[1].mins, -128.0f, -32.0f, -32.0f);
	VectorSet(aasworld.areas[1].maxs, 90.0f, 32.0f, 32.0f);
	VectorSet(aasworld.areas[2].mins, 91.0f, -32.0f, -32.0f);
	VectorSet(aasworld.areas[2].maxs, 200.0f, 32.0f, 64.0f);

	aasworld.numReachability = 2;
	aasworld.reachability = calloc((size_t)aasworld.numReachability, sizeof(aas_reachability_t));
	assert_non_null(aasworld.reachability);
	aasworld.reachability[1].areanum = 2;
	aasworld.reachability[1].traveltype = TRAVEL_JUMP;
	aasworld.reachability[1].traveltime = 1;
	VectorSet(aasworld.reachability[1].start, 80.0f, 0.0f, 0.0f);
	VectorSet(aasworld.reachability[1].end, 160.0f, 0.0f, 0.0f);
	aasworld.travelflagfortype[TRAVEL_JUMP] = TFL_JUMP;

	int handle = BotAllocMoveState();
	assert_true(handle > 0);
	bot_movestate_t *ms = BotMoveStateFromHandle(handle);
	assert_non_null(ms);
	BotResetMoveState(handle);
	ms->client = 0;
	ms->entitynum = 5;
	ms->areanum = 1;
	VectorSet(ms->origin, -40.0f, 0.0f, 0.0f);

	bot_goal_t goal;
	memset(&goal, 0, sizeof(goal));
	goal.areanum = 2;
	VectorSet(goal.origin, 160.0f, 0.0f, 0.0f);

	bot_moveresult_t result;
	BotMoveToGoal(&result, handle, &goal, TFL_DEFAULT);

	assert_false(result.failure);
	assert_int_equal(result.traveltype, TRAVEL_JUMP);
	assert_float_equal(result.movedir[0], 1.0f, 0.0001f);
	assert_float_equal(result.movedir[1], 0.0f, 0.0001f);
	assert_int_equal(g_trace_call_count, 0);
	assert_int_equal(ms->jumpreach, 0);

	bot_input_t input;
	assert_int_equal(EA_GetInput(0, 0.1f, &input), BLERR_NOERROR);
	assert_float_equal(input.speed, 200.0f, 0.0001f);
	assert_float_equal(input.dir[0], 1.0f, 0.0001f);
	assert_true((input.actionflags & ACTION_JUMP) == 0);
	assert_true((input.actionflags & ACTION_DELAYEDJUMP) == 0);

	BotFreeMoveState(handle);
	free(aasworld.areas);
	free(aasworld.areasettings);
	free(aasworld.reachability);
	aasworld.areas = NULL;
	aasworld.areasettings = NULL;
	aasworld.reachability = NULL;
	aasworld.numAreas = 0;
	aasworld.numAreaSettings = 0;
	aasworld.numReachability = 0;
}

/*
=============
test_bot_travel_jump_launches_near_start

Pins active jump travel's close launch branch and immediate jump action.
=============
*/
static void test_bot_travel_jump_launches_near_start(void **state)
{
	(void)state;

	aasworld.numAreas = 2;
	aasworld.areas = calloc(3, sizeof(aas_area_t));
	assert_non_null(aasworld.areas);
	aasworld.numAreaSettings = 3;
	aasworld.areasettings = calloc((size_t)aasworld.numAreaSettings, sizeof(aas_areasettings_t));
	assert_non_null(aasworld.areasettings);
	aasworld.areasettings[1].areaflags = TEST_AAS_AREAFLAG_GROUNDED;
	aasworld.areasettings[1].firstreachablearea = 1;
	aasworld.areasettings[1].numreachableareas = 1;
	VectorSet(aasworld.areas[1].mins, -128.0f, -32.0f, -32.0f);
	VectorSet(aasworld.areas[1].maxs, 90.0f, 32.0f, 32.0f);
	VectorSet(aasworld.areas[2].mins, 91.0f, -32.0f, -32.0f);
	VectorSet(aasworld.areas[2].maxs, 200.0f, 32.0f, 64.0f);

	aasworld.numReachability = 2;
	aasworld.reachability = calloc((size_t)aasworld.numReachability, sizeof(aas_reachability_t));
	assert_non_null(aasworld.reachability);
	aasworld.reachability[1].areanum = 2;
	aasworld.reachability[1].traveltype = TRAVEL_JUMP;
	aasworld.reachability[1].traveltime = 1;
	VectorSet(aasworld.reachability[1].start, 80.0f, 0.0f, 0.0f);
	VectorSet(aasworld.reachability[1].end, 160.0f, 0.0f, 0.0f);
	aasworld.travelflagfortype[TRAVEL_JUMP] = TFL_JUMP;

	int handle = BotAllocMoveState();
	assert_true(handle > 0);
	bot_movestate_t *ms = BotMoveStateFromHandle(handle);
	assert_non_null(ms);
	BotResetMoveState(handle);
	ms->client = 0;
	ms->entitynum = 5;
	ms->areanum = 1;
	VectorSet(ms->origin, 70.0f, 0.0f, 0.0f);

	bot_goal_t goal;
	memset(&goal, 0, sizeof(goal));
	goal.areanum = 2;
	VectorSet(goal.origin, 160.0f, 0.0f, 0.0f);

	bot_moveresult_t result;
	BotMoveToGoal(&result, handle, &goal, TFL_DEFAULT);

	assert_false(result.failure);
	assert_int_equal(result.traveltype, TRAVEL_JUMP);
	assert_float_equal(result.movedir[0], 1.0f, 0.0001f);
	assert_int_equal(g_trace_call_count, 0);
	assert_int_equal(ms->jumpreach, 1);

	bot_input_t input;
	assert_int_equal(EA_GetInput(0, 0.1f, &input), BLERR_NOERROR);
	assert_float_equal(input.speed, 600.0f, 0.0001f);
	assert_float_equal(input.dir[0], 1.0f, 0.0001f);
	assert_true((input.actionflags & ACTION_JUMP) != 0);
	assert_true((input.actionflags & ACTION_DELAYEDJUMP) == 0);

	BotFreeMoveState(handle);
	free(aasworld.areas);
	free(aasworld.areasettings);
	free(aasworld.reachability);
	aasworld.areas = NULL;
	aasworld.areasettings = NULL;
	aasworld.reachability = NULL;
	aasworld.numAreas = 0;
	aasworld.numAreaSettings = 0;
	aasworld.numReachability = 0;
}

/*
=============
test_bot_travel_jump_uses_delayed_jump_window

Pins active jump travel's delayed-jump action window before the immediate launch
threshold.
=============
*/
static void test_bot_travel_jump_uses_delayed_jump_window(void **state)
{
	(void)state;

	aasworld.numAreas = 2;
	aasworld.areas = calloc(3, sizeof(aas_area_t));
	assert_non_null(aasworld.areas);
	aasworld.numAreaSettings = 3;
	aasworld.areasettings = calloc((size_t)aasworld.numAreaSettings, sizeof(aas_areasettings_t));
	assert_non_null(aasworld.areasettings);
	aasworld.areasettings[1].areaflags = TEST_AAS_AREAFLAG_GROUNDED;
	aasworld.areasettings[1].firstreachablearea = 1;
	aasworld.areasettings[1].numreachableareas = 1;
	VectorSet(aasworld.areas[1].mins, -128.0f, -32.0f, -32.0f);
	VectorSet(aasworld.areas[1].maxs, 90.0f, 32.0f, 32.0f);
	VectorSet(aasworld.areas[2].mins, 91.0f, -32.0f, -32.0f);
	VectorSet(aasworld.areas[2].maxs, 200.0f, 32.0f, 64.0f);

	aasworld.numReachability = 2;
	aasworld.reachability = calloc((size_t)aasworld.numReachability, sizeof(aas_reachability_t));
	assert_non_null(aasworld.reachability);
	aasworld.reachability[1].areanum = 2;
	aasworld.reachability[1].traveltype = TRAVEL_JUMP;
	aasworld.reachability[1].traveltime = 1;
	VectorSet(aasworld.reachability[1].start, 80.0f, 0.0f, 0.0f);
	VectorSet(aasworld.reachability[1].end, 160.0f, 0.0f, 0.0f);
	aasworld.travelflagfortype[TRAVEL_JUMP] = TFL_JUMP;

	int handle = BotAllocMoveState();
	assert_true(handle > 0);
	bot_movestate_t *ms = BotMoveStateFromHandle(handle);
	assert_non_null(ms);
	BotResetMoveState(handle);
	ms->client = 0;
	ms->entitynum = 5;
	ms->areanum = 1;
	VectorSet(ms->origin, 52.0f, 0.0f, 0.0f);

	bot_goal_t goal;
	memset(&goal, 0, sizeof(goal));
	goal.areanum = 2;
	VectorSet(goal.origin, 160.0f, 0.0f, 0.0f);

	bot_moveresult_t result;
	BotMoveToGoal(&result, handle, &goal, TFL_DEFAULT);

	assert_false(result.failure);
	assert_int_equal(result.traveltype, TRAVEL_JUMP);
	assert_float_equal(result.movedir[0], 1.0f, 0.0001f);
	assert_int_equal(ms->jumpreach, 1);

	bot_input_t input;
	assert_int_equal(EA_GetInput(0, 0.1f, &input), BLERR_NOERROR);
	assert_float_equal(input.speed, 600.0f, 0.0001f);
	assert_true((input.actionflags & ACTION_JUMP) == 0);
	assert_true((input.actionflags & ACTION_DELAYEDJUMP) != 0);

	BotFreeMoveState(handle);
	free(aasworld.areas);
	free(aasworld.areasettings);
	free(aasworld.reachability);
	aasworld.areas = NULL;
	aasworld.areasettings = NULL;
	aasworld.reachability = NULL;
	aasworld.numAreas = 0;
	aasworld.numAreaSettings = 0;
	aasworld.numReachability = 0;
}

/*
=============
test_bot_move_airborne_finishes_last_jump_reachability

Pins the retail airborne path that continues the previous reachability instead
of selecting a fresh ground route.
=============
*/
static void test_bot_move_airborne_finishes_last_jump_reachability(void **state)
{
	(void)state;

	aasworld.time = 3.0f;
	aasworld.numReachability = 2;
	aasworld.reachability = calloc((size_t)aasworld.numReachability, sizeof(aas_reachability_t));
	assert_non_null(aasworld.reachability);

	aasworld.reachability[1].areanum = 2;
	aasworld.reachability[1].traveltype = TRAVEL_JUMP;
	VectorSet(aasworld.reachability[1].start, 64.0f, 0.0f, 0.0f);
	VectorSet(aasworld.reachability[1].end, 128.0f, 0.0f, 0.0f);

	int handle = BotAllocMoveState();
	assert_true(handle > 0);

	bot_movestate_t *ms = BotMoveStateFromHandle(handle);
	assert_non_null(ms);
	BotResetMoveState(handle);
	ms->client = 0;
	ms->entitynum = 0;
	ms->areanum = 1;
	ms->lastreachnum = 1;
	ms->lastgoalareanum = 2;
	ms->lastareanum = 1;
	ms->reachability_time = aasworld.time + 1.0f;
	ms->jumpreach = 1;
	VectorSet(ms->origin, 80.0f, 0.0f, 32.0f);
	VectorSet(ms->velocity, 180.0f, 0.0f, 120.0f);

	bot_goal_t goal;
	memset(&goal, 0, sizeof(goal));
	goal.areanum = 2;
	VectorSet(goal.origin, 128.0f, 0.0f, 0.0f);

	bot_moveresult_t result;
	BotMoveToGoal(&result, handle, &goal, TFL_DEFAULT);

	assert_false(result.failure);
	assert_int_equal(result.traveltype, TRAVEL_JUMP);
	assert_float_equal(result.movedir[0], 1.0f, 0.0001f);
	assert_float_equal(result.movedir[1], 0.0f, 0.0001f);
	assert_float_equal(result.movedir[2], 0.0f, 0.0001f);

	bot_input_t input;
	assert_int_equal(EA_GetInput(0, 0.1f, &input), BLERR_NOERROR);
	assert_float_equal(input.speed, 800.0f, 0.0001f);
	assert_true((input.actionflags & ACTION_JUMP) == 0);
	assert_int_equal(ms->lastreachnum, 1);

	BotFreeMoveState(handle);
	free(aasworld.reachability);
	aasworld.reachability = NULL;
	aasworld.numReachability = 0;
}

/*
=============
test_bot_reachability_area_world_hit_uses_crouch_bounds

Pins the retail area probe's crouch presence box and world-hit early return.
=============
*/
static void test_bot_reachability_area_world_hit_uses_crouch_bounds(void **state)
{
	(void)state;

	vec3_t origin;
	VectorSet(origin, 10.0f, 20.0f, 30.0f);

	g_trace_result_count = 1;
	memset(&g_trace_results[0], 0, sizeof(g_trace_results[0]));
	g_trace_results[0].fraction = 0.25f;
	g_trace_results[0].ent = 0;
	VectorCopy(origin, g_trace_results[0].endpos);

	assert_int_equal(BotReachabilityArea(origin, 7), 0);
	assert_int_equal(g_trace_call_count, 1);
	assert_int_equal(g_trace_passent_log[0], 7);
	assert_int_equal(g_trace_contentmask_log[0], CONTENTS_SOLID | CONTENTS_PLAYERCLIP);
	assert_float_equal(g_trace_mins_log[0][0], -15.0f, 0.0001f);
	assert_float_equal(g_trace_mins_log[0][1], -15.0f, 0.0001f);
	assert_float_equal(g_trace_mins_log[0][2], -24.0f, 0.0001f);
	assert_float_equal(g_trace_maxs_log[0][0], 15.0f, 0.0001f);
	assert_float_equal(g_trace_maxs_log[0][1], 15.0f, 0.0001f);
	assert_float_equal(g_trace_maxs_log[0][2], 8.0f, 0.0001f);
	assert_float_equal(g_trace_end_log[0][2], 27.0f, 0.0001f);
}

/*
=============
test_bot_reachability_area_non_mover_falls_down_unowned

Pins the Q3 fallback that traces down with no pass entity when standing on a
non-mover entity outside a reachable area.
=============
*/
static void test_bot_reachability_area_non_mover_falls_down_unowned(void **state)
{
	(void)state;

	aasworld.numAreas = 1;
	aasworld.numAreaSettings = 2;
	aasworld.areas = calloc(2, sizeof(aas_area_t));
	assert_non_null(aasworld.areas);
	aasworld.areasettings = calloc(2, sizeof(aas_areasettings_t));
	assert_non_null(aasworld.areasettings);

	aasworld.areas[1].areanum = 1;
	VectorSet(aasworld.areas[1].mins, -16.0f, -16.0f, -64.0f);
	VectorSet(aasworld.areas[1].maxs, 16.0f, 16.0f, 0.0f);
	aasworld.areasettings[1].numreachableareas = 1;

	vec3_t origin;
	VectorSet(origin, 128.0f, 0.0f, 64.0f);
	vec3_t ground;
	VectorSet(ground, 0.0f, 0.0f, -32.0f);

	g_trace_result_count = 2;
	memset(&g_trace_results[0], 0, sizeof(g_trace_results[0]));
	g_trace_results[0].fraction = 0.5f;
	g_trace_results[0].ent = 2;
	VectorCopy(origin, g_trace_results[0].endpos);
	memset(&g_trace_results[1], 0, sizeof(g_trace_results[1]));
	g_trace_results[1].fraction = 0.5f;
	g_trace_results[1].ent = 0;
	VectorCopy(ground, g_trace_results[1].endpos);

	assert_int_equal(BotReachabilityArea(origin, 7), 1);
	assert_int_equal(g_trace_call_count, 2);
	assert_int_equal(g_trace_passent_log[0], 7);
	assert_int_equal(g_trace_passent_log[1], -1);
	assert_float_equal(g_trace_mins_log[1][0], -15.0f, 0.0001f);
	assert_float_equal(g_trace_maxs_log[1][2], 8.0f, 0.0001f);
	assert_float_equal(g_trace_end_log[1][2], -736.0f, 0.0001f);

	free(aasworld.areas);
	free(aasworld.areasettings);
	aasworld.areas = NULL;
	aasworld.areasettings = NULL;
	aasworld.numAreas = 0;
	aasworld.numAreaSettings = 0;
}

/*
=============
test_bot_movement_view_target_uses_retail_route_context

Pins Q3 lookahead routing: continue from the previous reach end while ignoring
live avoid spots that are only meant to block actual movement choices.
=============
*/
static void test_bot_movement_view_target_uses_retail_route_context(void **state)
{
	(void)state;

	aasworld.time = 4.0f;
	aasworld.numAreas = 3;
	aasworld.numAreaSettings = 4;
	aasworld.areasettings = calloc((size_t)aasworld.numAreaSettings, sizeof(aas_areasettings_t));
	assert_non_null(aasworld.areasettings);
	aasworld.areasettings[1].firstreachablearea = 1;
	aasworld.areasettings[1].numreachableareas = 1;
	aasworld.areasettings[2].firstreachablearea = 2;
	aasworld.areasettings[2].numreachableareas = 2;

	aasworld.numReachability = 4;
	aasworld.reachability = calloc((size_t)aasworld.numReachability, sizeof(aas_reachability_t));
	assert_non_null(aasworld.reachability);

	aasworld.travelflagfortype[TRAVEL_WALK] = TFL_WALK;

	aasworld.reachability[1].areanum = 2;
	aasworld.reachability[1].traveltype = TRAVEL_WALK;
	aasworld.reachability[1].traveltime = 1;
	VectorSet(aasworld.reachability[1].start, 32.0f, 0.0f, 0.0f);
	VectorSet(aasworld.reachability[1].end, 64.0f, 0.0f, 0.0f);

	aasworld.reachability[2].areanum = 1;
	aasworld.reachability[2].traveltype = TRAVEL_WALK;
	aasworld.reachability[2].traveltime = 1;
	VectorSet(aasworld.reachability[2].start, 64.0f, 16.0f, 0.0f);
	VectorSet(aasworld.reachability[2].end, 32.0f, 16.0f, 0.0f);

	aasworld.reachability[3].areanum = 3;
	aasworld.reachability[3].traveltype = TRAVEL_WALK;
	aasworld.reachability[3].traveltime = 1;
	VectorSet(aasworld.reachability[3].start, 96.0f, 0.0f, 0.0f);
	VectorSet(aasworld.reachability[3].end, 128.0f, 0.0f, 0.0f);

	int handle = BotAllocMoveState();
	assert_true(handle > 0);

	bot_movestate_t *ms = BotMoveStateFromHandle(handle);
	assert_non_null(ms);
	BotResetMoveState(handle);
	ms->areanum = 1;
	ms->lastareanum = 1;
	ms->lastgoalareanum = 3;
	ms->lastreachnum = 1;
	VectorSet(ms->origin, 0.0f, 0.0f, 0.0f);
	VectorSet(ms->avoidspots[0].origin, 96.0f, 0.0f, 0.0f);
	ms->avoidspots[0].radius = 48.0f;
	ms->avoidspots[0].type = AVOID_ALWAYS;
	ms->numavoidspots = 1;

	bot_goal_t goal;
	memset(&goal, 0, sizeof(goal));
	goal.areanum = 3;
	VectorSet(goal.origin, 128.0f, 0.0f, 0.0f);

	vec3_t target;
	VectorClear(target);
	assert_true(BotMovementViewTarget(handle, &goal, TFL_DEFAULT, 96.0f, target));
	assert_float_equal(target[0], 96.0f, 0.0001f);
	assert_float_equal(target[1], 0.0f, 0.0001f);
	assert_float_equal(target[2], 0.0f, 0.0001f);

	BotFreeMoveState(handle);
	free(aasworld.areasettings);
	free(aasworld.reachability);
	aasworld.areasettings = NULL;
	aasworld.reachability = NULL;
	aasworld.numAreaSettings = 0;
	aasworld.numReachability = 0;
	aasworld.numAreas = 0;
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_bot_move_handles_elevator_landing,
                                        test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_bot_travel_grapple_hook_toggles,
                                        test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_bot_move_in_direction_submits_actions,
                                        test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_bot_move_in_direction_barrier_jump_uses_retail_probe,
                                        test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_bot_move_in_direction_airborne_barrier_jump_continues_late,
                                        test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_bot_move_in_direction_gap_probe_forces_jump,
                                        test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_bot_move_in_direction_gap_over_water_does_not_jump,
                                        test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_bot_travel_walk_targets_start_and_slows_for_gap,
                                        test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_bot_travel_walk_switches_to_end_and_crouches_near_crouch_area,
                                        test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_bot_travel_crouch_uses_retail_speed,
                                        test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_bot_travel_barrier_jump_approaches_start_until_close,
                                        test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_bot_travel_barrier_jump_jumps_when_close,
                                        test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_bot_travel_walkoffledge_far_uses_endpoint_at_retail_speed,
                                        test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_bot_travel_walkoffledge_uses_jump_fall_velocity,
                                        test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_bot_travel_walkoffledge_short_edge_scales_speed,
                                        test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_bot_travel_ladder_uses_forward_action_and_view,
                                        test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_bot_travel_swim_targets_start_at_retail_speed,
                                        test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_bot_travel_waterjump_submits_forward_and_up_actions,
                                        test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_bot_travel_teleport_approaches_start_and_slows_near,
                                        test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_bot_travel_jump_runs_to_runstart,
                                        test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_bot_travel_jump_launches_near_start,
                                        test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_bot_travel_jump_uses_delayed_jump_window,
                                        test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_bot_move_airborne_finishes_last_jump_reachability,
                                        test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_bot_reachability_area_world_hit_uses_crouch_bounds,
                                        test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_bot_reachability_area_non_mover_falls_down_unowned,
                                        test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_bot_movement_view_target_uses_retail_route_context,
                                        test_setup,
                                        test_teardown),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
