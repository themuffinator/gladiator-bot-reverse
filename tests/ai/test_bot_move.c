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
#include "botlib/ai_move/mover_catalogue.h"
#include "botlib/aas/aas_local.h"
#include "botlib/common/l_libvar.h"
#include "botlib/common/l_memory.h"
#include "botlib/common/l_utils.h"
#include "botlib/ea/ea_local.h"
#include "botlib/interface/botlib_interface.h"
#include "q2bridge/bridge.h"
#include "q2bridge/bridge_config.h"

bot_moveresult_t BotTravel_Walk(bot_movestate_t *ms,
	const aas_reachability_t *reach);
bot_moveresult_t BotTravel_Crouch(bot_movestate_t *ms,
	const aas_reachability_t *reach);
bot_moveresult_t BotTravel_BarrierJump(bot_movestate_t *ms,
	const aas_reachability_t *reach);
bot_moveresult_t BotFinishTravel_BarrierJump(bot_movestate_t *ms,
	const aas_reachability_t *reach);
bot_moveresult_t BotTravel_Swim(bot_movestate_t *ms,
	const aas_reachability_t *reach);
bot_moveresult_t BotTravel_WaterJump(bot_movestate_t *ms,
	const aas_reachability_t *reach);
bot_moveresult_t BotTravel_WalkOffLedge(bot_movestate_t *ms,
	const aas_reachability_t *reach);
bot_moveresult_t BotFinishTravel_WalkOffLedge(bot_movestate_t *ms,
	const aas_reachability_t *reach);
bot_moveresult_t BotTravel_Jump(bot_movestate_t *ms,
	const aas_reachability_t *reach);
bot_moveresult_t BotFinishTravel_Jump(bot_movestate_t *ms,
	const aas_reachability_t *reach);
bot_moveresult_t BotTravel_Ladder(bot_movestate_t *ms,
	const aas_reachability_t *reach);
bot_moveresult_t BotTravel_Teleport(bot_movestate_t *ms,
	const aas_reachability_t *reach);
bot_moveresult_t BotTravel_Elevator(bot_movestate_t *ms,
	const aas_reachability_t *reach);
bot_moveresult_t BotFinishTravel_Elevator(bot_movestate_t *ms,
	const aas_reachability_t *reach);
bot_moveresult_t BotTravel_RocketJump(bot_movestate_t *ms,
	const aas_reachability_t *reach);
bot_moveresult_t BotFinishTravel_WeaponJump(bot_movestate_t *ms,
	const aas_reachability_t *reach);

#ifndef MAX_TEST_COMMANDS
#define MAX_TEST_COMMANDS 16
#endif

#ifndef MAX_TEST_TRACES
#define MAX_TEST_TRACES 64
#endif

#ifndef MAX_TEST_POINT_CONTENTS
#define MAX_TEST_POINT_CONTENTS 8
#endif

#ifndef MAX_TEST_DEBUG_LINES
#define MAX_TEST_DEBUG_LINES 16
#endif

#ifndef MAX_TEST_DEBUG_EVENTS
#define MAX_TEST_DEBUG_EVENTS 32
#endif

#ifndef MAX_TEST_PRINTS
#define MAX_TEST_PRINTS 8
#endif

enum
{
	TEST_DEBUG_EVENT_PRINT = 1,
	TEST_DEBUG_EVENT_CREATE,
	TEST_DEBUG_EVENT_SHOW
};

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
static int g_point_contents_box_active;
static vec3_t g_point_contents_box_mins;
static vec3_t g_point_contents_box_maxs;
static int g_point_contents_box_contents;
static int g_debug_line_create_count;
static int g_debug_line_show_count;
static int g_debug_line_show_id_log[MAX_TEST_DEBUG_LINES];
static vec3_t g_debug_line_start_log[MAX_TEST_DEBUG_LINES];
static vec3_t g_debug_line_end_log[MAX_TEST_DEBUG_LINES];
static int g_debug_line_color_log[MAX_TEST_DEBUG_LINES];
static int g_debug_event_count;
static int g_debug_event_log[MAX_TEST_DEBUG_EVENTS];
static int g_bridge_print_count;
static int g_bridge_print_priority_log[MAX_TEST_PRINTS];
static char g_bridge_print_log[MAX_TEST_PRINTS][128];
static aas_area_t g_predict_trace_areas[4];
static aas_areasettings_t g_predict_trace_settings[4];
static aas_node_t g_predict_trace_nodes[5];
static aas_plane_t g_predict_trace_planes[6];
static aas_entity_t g_predict_trace_entities[4];
static aas_link_t g_predict_trace_entity_link;
static aas_link_t *g_predict_trace_area_entities[4];
static aas_bspmodel_t g_mover_models[10];

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
	g_point_contents_box_active = 0;
	g_point_contents_box_contents = 0;
	VectorClear(g_point_contents_box_mins);
	VectorClear(g_point_contents_box_maxs);
}

/*
=============
test_attach_bridge_contents_box

Attach one axis-aligned contents volume to the engine PointContents import.

Retail AAS_PointContents is nothing but a jump through the import table's
PointContents slot (0x1000308e "return data_10063ff0(arg1)"), so every AAS
contents probe - the predictor's swim and feet samples, BotGapDistance's water
exception and AAS_PointContents itself - resolves through this one mock.
=============
*/
static void test_attach_bridge_contents_box(const vec3_t mins,
	const vec3_t maxs,
	int contents)
{
	VectorCopy(mins, g_point_contents_box_mins);
	VectorCopy(maxs, g_point_contents_box_maxs);
	g_point_contents_box_contents = contents;
	g_point_contents_box_active = 1;
}

/*
=============
test_setup_aas_trace_storage

Attach cleared synthetic AAS trace storage to the movement-test world.
=============
*/
static void test_setup_aas_trace_storage(int numareas, int numnodes, int numplanes)
{
	memset(g_predict_trace_areas, 0, sizeof(g_predict_trace_areas));
	memset(g_predict_trace_settings, 0, sizeof(g_predict_trace_settings));
	memset(g_predict_trace_nodes, 0, sizeof(g_predict_trace_nodes));
	memset(g_predict_trace_planes, 0, sizeof(g_predict_trace_planes));
	memset(g_predict_trace_entities, 0, sizeof(g_predict_trace_entities));
	memset(&g_predict_trace_entity_link, 0, sizeof(g_predict_trace_entity_link));
	memset(g_predict_trace_area_entities, 0, sizeof(g_predict_trace_area_entities));

	aasworld.loaded = qtrue;
	aasworld.numAreas = numareas;
	aasworld.areas = g_predict_trace_areas;
	aasworld.numAreaSettings = numareas;
	aasworld.areasettings = g_predict_trace_settings;
	aasworld.numNodes = numnodes;
	aasworld.nodes = g_predict_trace_nodes;
	aasworld.numPlanes = numplanes;
	aasworld.planes = g_predict_trace_planes;
	aasworld.maxEntities = 0;
	aasworld.entities = NULL;
	aasworld.areaEntityListCount = 0;
	aasworld.areaEntityLists = NULL;

	for (int areanum = 1; areanum < numareas; ++areanum)
	{
		g_predict_trace_areas[areanum].areanum = areanum;
		VectorSet(g_predict_trace_areas[areanum].mins, -4096.0f, -4096.0f, -4096.0f);
		VectorSet(g_predict_trace_areas[areanum].maxs, 4096.0f, 4096.0f, 4096.0f);
		g_predict_trace_settings[areanum].presencetype = PRESENCE_NORMAL | PRESENCE_CROUCH;
	}
}

/*
=============
test_setup_open_aas_trace_world

Build one open AAS area on both sides of a harmless partition plane.
=============
*/
static void test_setup_open_aas_trace_world(void)
{
	test_setup_aas_trace_storage(2, 2, 2);
	g_predict_trace_nodes[1].planenum = 0;
	g_predict_trace_nodes[1].children[0] = -1;
	g_predict_trace_nodes[1].children[1] = -1;
	VectorSet(g_predict_trace_planes[0].normal, 1.0f, 0.0f, 0.0f);
	VectorSet(g_predict_trace_planes[1].normal, -1.0f, 0.0f, 0.0f);
}

/*
=============
test_setup_blocked_aas_trace_world

Build an AAS area that rejects normal presence and reports start-solid traces.
=============
*/
static void test_setup_blocked_aas_trace_world(void)
{
	test_setup_open_aas_trace_world();
	g_predict_trace_settings[1].presencetype = PRESENCE_CROUCH;
}

/*
=============
test_setup_floor_aas_trace_world

Build one open area above a solid horizontal AAS plane.
=============
*/
static void test_setup_floor_aas_trace_world(float floorheight)
{
	test_setup_aas_trace_storage(2, 2, 2);
	g_predict_trace_nodes[1].planenum = 0;
	g_predict_trace_nodes[1].children[0] = -1;
	g_predict_trace_nodes[1].children[1] = 0;
	VectorSet(g_predict_trace_planes[0].normal, 0.0f, 0.0f, 1.0f);
	g_predict_trace_planes[0].dist = floorheight;
	VectorSet(g_predict_trace_planes[1].normal, 0.0f, 0.0f, -1.0f);
	g_predict_trace_planes[1].dist = -floorheight;
}

/*
=============
test_attach_floor_aas_geometry

Attach a single reachable AAS leaf above a floor without replacing test-owned
area settings, reachabilities, entities, or area-entity links.
=============
*/
static void test_attach_floor_aas_geometry(float floorheight)
{
	memset(g_predict_trace_areas, 0, sizeof(g_predict_trace_areas));
	memset(g_predict_trace_nodes, 0, sizeof(g_predict_trace_nodes));
	memset(g_predict_trace_planes, 0, sizeof(g_predict_trace_planes));

	aasworld.loaded = qtrue;
	aasworld.initialized = qtrue;
	aasworld.areas = g_predict_trace_areas;
	aasworld.numNodes = 2;
	aasworld.nodes = g_predict_trace_nodes;
	aasworld.numPlanes = 2;
	aasworld.planes = g_predict_trace_planes;

	g_predict_trace_areas[1].areanum = 1;
	VectorSet(g_predict_trace_areas[1].mins, -4096.0f, -4096.0f, floorheight);
	VectorSet(g_predict_trace_areas[1].maxs, 4096.0f, 4096.0f, 4096.0f);
	g_predict_trace_nodes[1].planenum = 0;
	g_predict_trace_nodes[1].children[0] = -1;
	g_predict_trace_nodes[1].children[1] = 0;
	VectorSet(g_predict_trace_planes[0].normal, 0.0f, 0.0f, 1.0f);
	g_predict_trace_planes[0].dist = floorheight;
	VectorSet(g_predict_trace_planes[1].normal, 0.0f, 0.0f, -1.0f);
	g_predict_trace_planes[1].dist = -floorheight;

	if (aasworld.areasettings != NULL && aasworld.numAreaSettings > 1)
	{
		aasworld.areasettings[1].presencetype |= PRESENCE_NORMAL | PRESENCE_CROUCH;
	}
}

/*
=============
test_setup_gap_aas_trace_world

Build level ground on both sides of a finite open gap along the X axis.
=============
*/
static void test_setup_gap_aas_trace_world(float gapstart, float gapend)
{
	test_setup_aas_trace_storage(4, 5, 6);

	g_predict_trace_nodes[1].planenum = 0;
	g_predict_trace_nodes[1].children[0] = 3;
	g_predict_trace_nodes[1].children[1] = 2;
	g_predict_trace_nodes[2].planenum = 4;
	g_predict_trace_nodes[2].children[0] = -1;
	g_predict_trace_nodes[2].children[1] = 0;
	g_predict_trace_nodes[3].planenum = 2;
	g_predict_trace_nodes[3].children[0] = 4;
	g_predict_trace_nodes[3].children[1] = -2;
	g_predict_trace_nodes[4].planenum = 4;
	g_predict_trace_nodes[4].children[0] = -3;
	g_predict_trace_nodes[4].children[1] = 0;

	VectorSet(g_predict_trace_planes[0].normal, 1.0f, 0.0f, 0.0f);
	g_predict_trace_planes[0].dist = gapstart;
	VectorSet(g_predict_trace_planes[1].normal, -1.0f, 0.0f, 0.0f);
	g_predict_trace_planes[1].dist = -gapstart;
	VectorSet(g_predict_trace_planes[2].normal, 1.0f, 0.0f, 0.0f);
	g_predict_trace_planes[2].dist = gapend;
	VectorSet(g_predict_trace_planes[3].normal, -1.0f, 0.0f, 0.0f);
	g_predict_trace_planes[3].dist = -gapend;
	VectorSet(g_predict_trace_planes[4].normal, 0.0f, 0.0f, 1.0f);
	VectorSet(g_predict_trace_planes[5].normal, 0.0f, 0.0f, -1.0f);
}

/*
=============
test_setup_stacked_aas_trace_world

Build an unreachable upper area over a reachable lower area and solid floor.
=============
*/
static void test_setup_stacked_aas_trace_world(float splitheight)
{
	test_setup_aas_trace_storage(3, 3, 4);

	g_predict_trace_nodes[1].planenum = 0;
	g_predict_trace_nodes[1].children[0] = -2;
	g_predict_trace_nodes[1].children[1] = 2;
	g_predict_trace_nodes[2].planenum = 2;
	g_predict_trace_nodes[2].children[0] = -1;
	g_predict_trace_nodes[2].children[1] = 0;

	VectorSet(g_predict_trace_planes[0].normal, 0.0f, 0.0f, 1.0f);
	g_predict_trace_planes[0].dist = splitheight;
	VectorSet(g_predict_trace_planes[1].normal, 0.0f, 0.0f, -1.0f);
	g_predict_trace_planes[1].dist = -splitheight;
	VectorSet(g_predict_trace_planes[2].normal, 0.0f, 0.0f, 1.0f);
	VectorSet(g_predict_trace_planes[3].normal, 0.0f, 0.0f, -1.0f);
	g_predict_trace_settings[1].firstreachablearea = 1;
	g_predict_trace_settings[1].numreachableareas = 1;
}

/*
=============
test_attach_mover_bsp_model

Attach deterministic inline-model bounds for mover reachability geometry.
=============
*/
static void test_attach_mover_bsp_model(int modelnum,
	const vec3_t mins,
	const vec3_t maxs,
	const vec3_t origin)
{
	assert_true(modelnum >= 0 && modelnum < (int)(sizeof(g_mover_models) / sizeof(g_mover_models[0])));
	memset(g_mover_models, 0, sizeof(g_mover_models));
	aasworld.numBspModels = modelnum + 1;
	aasworld.bspModels = g_mover_models;
	VectorCopy(mins, g_mover_models[modelnum].mins);
	VectorCopy(maxs, g_mover_models[modelnum].maxs);
	VectorCopy(origin, g_mover_models[modelnum].origin);
}

/*
=============
test_setup_split_aas_trace_world

Build two open AAS areas separated at the requested X coordinate.
=============
*/
static void test_setup_split_aas_trace_world(float splitx)
{
	test_setup_aas_trace_storage(3, 2, 2);
	g_predict_trace_nodes[1].planenum = 0;
	g_predict_trace_nodes[1].children[0] = -2;
	g_predict_trace_nodes[1].children[1] = -1;
	VectorSet(g_predict_trace_planes[0].normal, 1.0f, 0.0f, 0.0f);
	g_predict_trace_planes[0].dist = splitx;
	VectorSet(g_predict_trace_planes[1].normal, -1.0f, 0.0f, 0.0f);
	g_predict_trace_planes[1].dist = -splitx;
}

/*
=============
test_setup_step_aas_trace_world

Build an eight-unit step behind a vertical wall at X = 4.
=============
*/
static void test_setup_step_aas_trace_world(void)
{
	test_setup_aas_trace_storage(3, 3, 4);
	g_predict_trace_nodes[1].planenum = 0;
	g_predict_trace_nodes[1].children[0] = 2;
	g_predict_trace_nodes[1].children[1] = -1;
	g_predict_trace_nodes[2].planenum = 2;
	g_predict_trace_nodes[2].children[0] = -2;
	g_predict_trace_nodes[2].children[1] = 0;

	VectorSet(g_predict_trace_planes[0].normal, 1.0f, 0.0f, 0.0f);
	g_predict_trace_planes[0].dist = 4.0f;
	VectorSet(g_predict_trace_planes[1].normal, -1.0f, 0.0f, 0.0f);
	g_predict_trace_planes[1].dist = -4.0f;
	VectorSet(g_predict_trace_planes[2].normal, 0.0f, 0.0f, 1.0f);
	g_predict_trace_planes[2].dist = 8.0f;
	VectorSet(g_predict_trace_planes[3].normal, 0.0f, 0.0f, -1.0f);
	g_predict_trace_planes[3].dist = -8.0f;
}

/*
=============
test_setup_near_vertical_step_aas_trace_world

Build a step whose wall has a small nonzero vertical normal component.
=============
*/
static void test_setup_near_vertical_step_aas_trace_world(void)
{
	test_setup_step_aas_trace_world();
	VectorSet(g_predict_trace_planes[0].normal, 1.0f, 0.0f, 0.00005f);
	VectorSet(g_predict_trace_planes[1].normal, -1.0f, 0.0f, -0.00005f);
}

/*
=============
test_setup_entity_aas_trace_world

Build one open AAS area containing a linked solid entity in the trace origin.
=============
*/
static void test_setup_entity_aas_trace_world(void)
{
	test_setup_open_aas_trace_world();
	aasworld.maxEntities = 4;
	aasworld.entities = g_predict_trace_entities;
	aasworld.areaEntityListCount = 2;
	aasworld.areaEntityLists = g_predict_trace_area_entities;

	g_predict_trace_entities[3].inuse = qtrue;
	g_predict_trace_entities[3].number = 3;
	g_predict_trace_entities[3].solid = SOLID_BBOX;
	VectorSet(g_predict_trace_entities[3].mins, -4.0f, -4.0f, -4.0f);
	VectorSet(g_predict_trace_entities[3].maxs, 4.0f, 4.0f, 4.0f);
	g_predict_trace_entity_link.entnum = 3;
	g_predict_trace_entity_link.areanum = 1;
	g_predict_trace_area_entities[1] = &g_predict_trace_entity_link;
	g_predict_trace_entities[3].areas = &g_predict_trace_entity_link;
}

/*
=============
test_setup_offset_entity_aas_trace_world

Build a distant entity collision whose embedded plane differs from planenum 0.
=============
*/
static void test_setup_offset_entity_aas_trace_world(void)
{
	test_setup_entity_aas_trace_world();
	VectorSet(g_predict_trace_planes[0].normal, 0.0f, 0.0f, 1.0f);
	VectorSet(g_predict_trace_planes[1].normal, 0.0f, 0.0f, -1.0f);
	VectorSet(g_predict_trace_entities[3].mins, 30.0f, -100.0f, -100.0f);
	VectorSet(g_predict_trace_entities[3].maxs, 34.0f, 100.0f, 100.0f);
}

/*
=============
test_append_debug_event

Append one predictor visualization import call to the ordered test log.
=============
*/
static void test_append_debug_event(int event)
{
	if (g_debug_event_count < MAX_TEST_DEBUG_EVENTS)
	{
		g_debug_event_log[g_debug_event_count] = event;
	}
	g_debug_event_count++;
}

/*
=============
test_reset_debug_visualization_log

Reset captured predictor prints and debug-line import calls.
=============
*/
static void test_reset_debug_visualization_log(void)
{
	g_debug_line_create_count = 0;
	g_debug_line_show_count = 0;
	memset(g_debug_line_show_id_log, 0, sizeof(g_debug_line_show_id_log));
	memset(g_debug_line_start_log, 0, sizeof(g_debug_line_start_log));
	memset(g_debug_line_end_log, 0, sizeof(g_debug_line_end_log));
	memset(g_debug_line_color_log, 0, sizeof(g_debug_line_color_log));
	g_debug_event_count = 0;
	memset(g_debug_event_log, 0, sizeof(g_debug_event_log));
	g_bridge_print_count = 0;
	memset(g_bridge_print_priority_log, 0, sizeof(g_bridge_print_priority_log));
	memset(g_bridge_print_log, 0, sizeof(g_bridge_print_log));
}

/*
=============
test_bridge_print

Capture engine print imports used by predictor visualization diagnostics.
=============
*/
static void test_bridge_print(int type, char *fmt, ...)
{
	int index = g_bridge_print_count;
	if (index >= 0 && index < MAX_TEST_PRINTS)
	{
		g_bridge_print_priority_log[index] = type;
		if (fmt != NULL)
		{
			va_list args;
			va_start(args, fmt);
			vsnprintf(g_bridge_print_log[index], sizeof(g_bridge_print_log[index]), fmt, args);
			va_end(args);
			g_bridge_print_log[index][sizeof(g_bridge_print_log[index]) - 1] = '\0';
		}
	}
	g_bridge_print_count++;
	test_append_debug_event(TEST_DEBUG_EVENT_PRINT);
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
	if (end != NULL)
	{
		VectorCopy(end, trace.endpos);
	}
    return trace;
}

/*
=============
test_set_trace_result

Queue one trace result for movement-prediction tests.
=============
*/
static void test_set_trace_result(int index, float fraction, const vec3_t endpos, const vec3_t normal)
{
	assert_true(index >= 0 && index < MAX_TEST_TRACES);

	bsp_trace_t *trace = &g_trace_results[index];
	memset(trace, 0, sizeof(*trace));
	trace->fraction = fraction;
	if (endpos != NULL)
	{
		VectorCopy(endpos, trace->endpos);
	}
	if (normal != NULL)
	{
		VectorCopy(normal, trace->plane.normal);
	}
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

	if (g_point_contents_box_active && point != NULL)
	{
		int inside = 1;
		for (int axis = 0; axis < 3; ++axis)
		{
			if (point[axis] < g_point_contents_box_mins[axis] ||
				point[axis] > g_point_contents_box_maxs[axis])
			{
				inside = 0;
				break;
			}
		}
		if (inside)
		{
			return g_point_contents_box_contents;
		}
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

/*
=============
test_bridge_debug_line_create

Return distinct positive identifiers and capture retail line creation order.
=============
*/
static int test_bridge_debug_line_create(void)
{
	g_debug_line_create_count++;
	test_append_debug_event(TEST_DEBUG_EVENT_CREATE);
	return g_debug_line_create_count;
}

/*
=============
test_bridge_debug_line_delete

Accept unused debug-line deletion imports in the movement fixture.
=============
*/
static void test_bridge_debug_line_delete(int line)
{
	(void)line;
}

/*
=============
test_bridge_debug_line_show

Capture predictor line endpoints, colors, identifiers, and import order.
=============
*/
static void test_bridge_debug_line_show(int line, vec3_t start, vec3_t end, int color)
{
	int index = g_debug_line_show_count;
	if (index >= 0 && index < MAX_TEST_DEBUG_LINES)
	{
		g_debug_line_show_id_log[index] = line;
		if (start != NULL)
		{
			VectorCopy(start, g_debug_line_start_log[index]);
		}
		if (end != NULL)
		{
			VectorCopy(end, g_debug_line_end_log[index]);
		}
		g_debug_line_color_log[index] = color;
	}
	g_debug_line_show_count++;
	test_append_debug_event(TEST_DEBUG_EVENT_SHOW);
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

static bot_import_extended_t g_test_bridge_imports = {
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
	Q2Bridge_SetDebugLinesEnabled(false);
    BotInterface_SetImportTable(&g_test_lib_imports);
    LibVar_Init();
    BridgeConfig_Init();
	if (BotSetupMoveAI() == 0)
	{
		return -1;
	}
	test_reset_debug_visualization_log();

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

	for (int handle = 1; handle <= MAX_CLIENTS; ++handle)
	{
		BotFreeMoveStateHandle(handle);
	}
	BotShutdownMoveAI();
    EA_Shutdown();
    BridgeConfig_Shutdown();
    LibVar_Shutdown();
	Q2Bridge_SetDebugLinesEnabled(false);
    Q2Bridge_ClearImportTable();
    BotInterface_SetImportTable(NULL);
    return 0;
}

/*
=============
test_bot_move_retail_struct_layout

Pin the public retail movement-result and movement-state ABI.
=============
*/
static void test_bot_move_retail_struct_layout(void **state)
{
	(void)state;

	assert_int_equal(sizeof(bot_moveresult_t), 0x30);
	assert_int_equal(offsetof(bot_moveresult_t, movedir), 0x18);
	assert_int_equal(offsetof(bot_moveresult_t, ideal_viewangles), 0x24);
	assert_int_equal(sizeof(bot_movestate_t), 0x80);
	assert_int_equal(offsetof(bot_movestate_t, areanum), 0x40);
	assert_int_equal(offsetof(bot_movestate_t, lastreachnum), 0x4c);
	assert_int_equal(offsetof(bot_movestate_t, moveflags), 0x60);
	assert_int_equal(offsetof(bot_movestate_t, reachability_time), 0x70);
}

/*
=============
test_bot_clear_move_result_preserves_vector_suffix

Verify retail clears only the six-dword status prefix and returns its argument.
=============
*/
static void test_bot_clear_move_result_preserves_vector_suffix(void **state)
{
	(void)state;

	bot_moveresult_t result;
	memset(&result, 0xa5, sizeof(result));
	assert_ptr_equal(BotClearMoveResult(&result), &result);

	const unsigned char *bytes = (const unsigned char *)&result;
	for (size_t offset = 0; offset < sizeof(result); ++offset)
	{
		assert_int_equal(bytes[offset], offset < 0x18 ? 0x00 : 0xa5);
	}
}

/*
=============
test_bot_setup_move_ai_registers_retail_contract

Pin the exact sixteen retail movement libvar names, defaults, and success return.
=============
*/
static void test_bot_setup_move_ai_registers_retail_contract(void **state)
{
	(void)state;

	static const struct
	{
		const char *name;
		const char *value;
	} expected[] = {
		{"sv_friction", "6"},
		{"sv_stopspeed", "100"},
		{"sv_gravity", "800"},
		{"sv_waterfriction", "1"},
		{"sv_watergravity", "400"},
		{"sv_maxvelocity", "300"},
		{"sv_maxwalkvelocity", "300"},
		{"sv_maxcrouchvelocity", "100"},
		{"sv_maxswimvelocity", "150"},
		{"sv_maxacceleration", "2200"},
		{"sv_airaccelerate", "0"},
		{"sv_step", "18"},
		{"sv_maxbarrier", "50"},
		{"sv_maxsteepness", "0.7"},
		{"sv_jumpvel", "224"},
		{"sv_maxwaterjump", "21"},
	};

	BotShutdownMoveAI();
	assert_true(BotSetupMoveAI() != 0);
	for (size_t index = 0; index < sizeof(expected) / sizeof(expected[0]); ++index)
	{
		assert_non_null(LibVarGet(expected[index].name));
		assert_string_equal(LibVarGetString(expected[index].name), expected[index].value);
	}
	assert_null(LibVarGet("sv_maxaccelerate"));
}

/*
=============
test_bot_shutdown_invalidates_handle_registry

Verify shutdown invalidates non-owning handle slots and setup restarts handle
allocation from the first retail-compatible adapter slot.
=============
*/
static void test_bot_shutdown_invalidates_handle_registry(void **state)
{
	(void)state;

	int handle = BotAllocMoveStateHandle();
	assert_int_equal(handle, 1);
	assert_non_null(BotMoveStateFromHandle(handle));

	BotShutdownMoveAI();
	assert_null(BotMoveStateFromHandle(handle));
	assert_true(BotSetupMoveAI() != 0);

	int replacement = BotAllocMoveStateHandle();
	assert_int_equal(replacement, 1);
	assert_non_null(BotMoveStateFromHandle(replacement));
	BotFreeMoveStateHandle(replacement);
}

/*
=============
test_bot_move_handles_elevator_landing

Verify ground dispatch selects the retail elevator reach and waiting state.
=============
*/
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
	test_attach_floor_aas_geometry(-1.5f);

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
    aasworld.entities[2].modelindex = 6;

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

	int handle = BotAllocMoveStateHandle();
    assert_int_not_equal(handle, 0);
    bot_movestate_t *ms = BotMoveStateFromHandle(handle);
    assert_non_null(ms);
    ms->entitynum = 1;
    ms->areanum = 1;
	ms->presencetype = PRESENCE_NORMAL;
	ms->moveflags = MFL_ONGROUND;
    VectorClear(ms->origin);

    bot_goal_t goal = {0};
    goal.areanum = 2;

    bot_moveresult_t result;
    BotClearMoveResult(&result);

	BotMoveToGoalHandle(&result, handle, &goal, TFL_DEFAULT);

    assert_false(result.blocked);
    assert_int_equal(result.type, RESULTTYPE_ELEVATORUP);
    assert_int_equal(ms->lastreachnum, 1);
	assert_int_equal(ms->reachareanum, 1);
    float timeoutDelta = ms->reachability_time - aasworld.time;
	assert_true(timeoutDelta > 9.9f && timeoutDelta < 10.1f);

	BotFreeMoveStateHandle(handle);
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

/*
=============
test_bot_travel_elevator_waits_when_platform_is_up

Pins Gladiator's active elevator wait branch when the mover is not down.
=============
*/
static void test_bot_travel_elevator_waits_when_platform_is_up(void **state)
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
	aasworld.reachability[1].traveltype = TRAVEL_ELEVATOR;
	aasworld.reachability[1].traveltime = 1;
	aasworld.reachability[1].facenum = 7;
	VectorSet(aasworld.reachability[1].start, 0.0f, 0.0f, 64.0f);
	VectorSet(aasworld.reachability[1].end, 0.0f, 0.0f, 128.0f);
	aasworld.travelflagfortype[TRAVEL_ELEVATOR] = TFL_ELEVATOR;

	aasworld.maxEntities = 8;
	aasworld.entities = calloc((size_t)aasworld.maxEntities, sizeof(aas_entity_t));
	assert_non_null(aasworld.entities);
	aasworld.entities[3].inuse = qtrue;
	aasworld.entities[3].solid = SOLID_BSP;
	aasworld.entities[3].modelindex = 8;
	VectorSet(aasworld.entities[3].origin, 0.0f, 0.0f, 96.0f);
	VectorSet(aasworld.entities[3].mins, -16.0f, -16.0f, -16.0f);
	VectorSet(aasworld.entities[3].maxs, 16.0f, 16.0f, 16.0f);

	int handle = BotAllocMoveStateHandle();
	assert_true(handle > 0);
	bot_movestate_t *ms = BotMoveStateFromHandle(handle);
	assert_non_null(ms);
	BotResetMoveStateHandle(handle);
	ms->client = 0;
	ms->entitynum = 5;
	ms->areanum = 1;
	VectorSet(ms->origin, -120.0f, 0.0f, 64.0f);

	bot_goal_t goal;
	memset(&goal, 0, sizeof(goal));
	goal.areanum = 2;
	VectorSet(goal.origin, 0.0f, 0.0f, 128.0f);

	bot_moveresult_t result;
	result = BotTravel_Elevator(ms, &aasworld.reachability[1]);

	assert_false(result.failure);
	assert_int_equal(result.traveltype, 0);
	assert_int_equal(result.type, RESULTTYPE_ELEVATORUP);
	assert_true((result.flags & MOVERESULT_WAITING) != 0);
	assert_float_equal(result.movedir[0], 1.0f, 0.0001f);
	assert_float_equal(result.movedir[1], 0.0f, 0.0001f);

	bot_input_t input;
	assert_int_equal(EA_GetInput(0, 0.1f, &input), BLERR_NOERROR);
	assert_float_equal(input.speed, 360.0f, 0.0001f);
	assert_float_equal(input.dir[0], 1.0f, 0.0001f);
	assert_true((input.actionflags & ACTION_JUMP) == 0);

	BotFreeMoveStateHandle(handle);
	free(aasworld.areasettings);
	free(aasworld.reachability);
	free(aasworld.entities);
	aasworld.areasettings = NULL;
	aasworld.reachability = NULL;
	aasworld.entities = NULL;
	aasworld.numAreas = 0;
	aasworld.numAreaSettings = 0;
	aasworld.numReachability = 0;
	aasworld.maxEntities = 0;
}

/*
=============
test_bot_travel_elevator_moves_to_bottom_center_when_down

Pins the active elevator center-approach branch once the mover is down.
=============
*/
static void test_bot_travel_elevator_moves_to_bottom_center_when_down(void **state)
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
	aasworld.reachability[1].traveltype = TRAVEL_ELEVATOR;
	aasworld.reachability[1].traveltime = 1;
	aasworld.reachability[1].facenum = 7;
	VectorSet(aasworld.reachability[1].start, 40.0f, 0.0f, 64.0f);
	VectorSet(aasworld.reachability[1].end, 40.0f, 0.0f, 128.0f);
	aasworld.travelflagfortype[TRAVEL_ELEVATOR] = TFL_ELEVATOR;
	vec3_t modelmins = {-16.0f, -16.0f, -16.0f};
	vec3_t modelmaxs = {16.0f, 16.0f, 16.0f};
	vec3_t modelorigin = {0.0f, 0.0f, 0.0f};
	test_attach_mover_bsp_model(7, modelmins, modelmaxs, modelorigin);

	aasworld.maxEntities = 8;
	aasworld.entities = calloc((size_t)aasworld.maxEntities, sizeof(aas_entity_t));
	assert_non_null(aasworld.entities);
	aasworld.entities[3].inuse = qtrue;
	aasworld.entities[3].solid = SOLID_BSP;
	aasworld.entities[3].modelindex = 8;
	VectorClear(aasworld.entities[3].origin);
	VectorSet(aasworld.entities[3].mins, -16.0f, -16.0f, -16.0f);
	VectorSet(aasworld.entities[3].maxs, 16.0f, 16.0f, 16.0f);

	int handle = BotAllocMoveStateHandle();
	assert_true(handle > 0);
	bot_movestate_t *ms = BotMoveStateFromHandle(handle);
	assert_non_null(ms);
	BotResetMoveStateHandle(handle);
	ms->client = 0;
	ms->entitynum = 5;
	ms->areanum = 1;
	VectorSet(ms->origin, -50.0f, 0.0f, 64.0f);

	bot_goal_t goal;
	memset(&goal, 0, sizeof(goal));
	goal.areanum = 2;
	VectorSet(goal.origin, 40.0f, 0.0f, 128.0f);

	bot_moveresult_t result;
	result = BotTravel_Elevator(ms, &aasworld.reachability[1]);

	assert_false(result.failure);
	assert_int_equal(result.traveltype, 0);
	assert_true((result.flags & MOVERESULT_WAITING) == 0);
	assert_float_equal(result.movedir[0], 1.0f, 0.0001f);
	assert_float_equal(result.movedir[1], 0.0f, 0.0001f);

	bot_input_t input;
	assert_int_equal(EA_GetInput(0, 0.1f, &input), BLERR_NOERROR);
	assert_float_equal(input.speed, 300.0f, 0.0001f);
	assert_float_equal(input.dir[0], 1.0f, 0.0001f);

	BotFreeMoveStateHandle(handle);
	free(aasworld.areasettings);
	free(aasworld.reachability);
	free(aasworld.entities);
	aasworld.areasettings = NULL;
	aasworld.reachability = NULL;
	aasworld.entities = NULL;
	aasworld.numAreas = 0;
	aasworld.numAreaSettings = 0;
	aasworld.numReachability = 0;
	aasworld.maxEntities = 0;
}

/*
=============
test_bot_finish_elevator_prefers_closer_vertical_endpoint

Pins Gladiator's airborne elevator finish helper.
=============
*/
static void test_bot_finish_elevator_prefers_closer_vertical_endpoint(void **state)
{
	(void)state;

	aasworld.numAreaSettings = 3;
	aasworld.areasettings = calloc((size_t)aasworld.numAreaSettings, sizeof(aas_areasettings_t));
	assert_non_null(aasworld.areasettings);
	aasworld.numReachability = 2;
	aasworld.reachability = calloc((size_t)aasworld.numReachability, sizeof(aas_reachability_t));
	assert_non_null(aasworld.reachability);
	aasworld.reachability[1].areanum = 2;
	aasworld.reachability[1].traveltype = TRAVEL_ELEVATOR;
	aasworld.reachability[1].facenum = 7;
	VectorSet(aasworld.reachability[1].start, 0.0f, 0.0f, 0.0f);
	VectorSet(aasworld.reachability[1].end, 30.0f, 0.0f, 100.0f);
	vec3_t modelmins = {-16.0f, -16.0f, -16.0f};
	vec3_t modelmaxs = {16.0f, 16.0f, 16.0f};
	vec3_t modelorigin = {0.0f, 0.0f, 0.0f};
	test_attach_mover_bsp_model(7, modelmins, modelmaxs, modelorigin);

	aasworld.maxEntities = 8;
	aasworld.entities = calloc((size_t)aasworld.maxEntities, sizeof(aas_entity_t));
	assert_non_null(aasworld.entities);
	aasworld.entities[3].inuse = qtrue;
	aasworld.entities[3].solid = SOLID_BSP;
	aasworld.entities[3].modelindex = 8;
	VectorClear(aasworld.entities[3].origin);
	VectorSet(aasworld.entities[3].mins, -16.0f, -16.0f, -16.0f);
	VectorSet(aasworld.entities[3].maxs, 16.0f, 16.0f, 16.0f);

	int handle = BotAllocMoveStateHandle();
	assert_true(handle > 0);
	bot_movestate_t *ms = BotMoveStateFromHandle(handle);
	assert_non_null(ms);
	BotResetMoveStateHandle(handle);
	ms->client = 0;
	ms->entitynum = 5;
	ms->areanum = 1;
	ms->lastreachnum = 1;
	ms->lastgoalareanum = 2;
	ms->lastareanum = 1;
	ms->reachability_time = aasworld.time + 1.0f;
	VectorSet(ms->origin, 0.0f, 0.0f, 90.0f);

	bot_goal_t goal;
	memset(&goal, 0, sizeof(goal));
	goal.areanum = 2;
	VectorSet(goal.origin, 30.0f, 0.0f, 100.0f);

	bot_moveresult_t result;
	result = BotFinishTravel_Elevator(ms, &aasworld.reachability[1]);

	assert_false(result.failure);
	assert_int_equal(result.traveltype, 0);

	bot_input_t input;
	assert_int_equal(EA_GetInput(0, 0.1f, &input), BLERR_NOERROR);
	assert_float_equal(input.speed, 300.0f, 0.0001f);
	assert_float_equal(input.dir[0], 0.9486833f, 0.0001f);
	assert_float_equal(input.dir[2], 0.3162278f, 0.0001f);

	BotFreeMoveStateHandle(handle);
	free(aasworld.areasettings);
	free(aasworld.reachability);
	free(aasworld.entities);
	aasworld.areasettings = NULL;
	aasworld.reachability = NULL;
	aasworld.entities = NULL;
	aasworld.numAreaSettings = 0;
	aasworld.numReachability = 0;
	aasworld.maxEntities = 0;
}

/*
=============
test_bot_travel_funcbob_preserves_retail_unsupported_result

Pins the retail default dispatch for the unsupported func_bobbing travel type.
=============
*/
static void test_bot_travel_funcbob_preserves_retail_unsupported_result(void **state)
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
	aasworld.reachability[1].traveltype = TRAVEL_FUNCBOB;
	aasworld.reachability[1].traveltime = 1;
	aasworld.reachability[1].facenum = (1 << 16) | 8;
	aasworld.reachability[1].edgenum = (0 << 16) | 100;
	VectorSet(aasworld.reachability[1].start, 0.0f, 0.0f, 0.0f);
	VectorSet(aasworld.reachability[1].end, 160.0f, 0.0f, 0.0f);
	aasworld.travelflagfortype[TRAVEL_FUNCBOB] = TFL_FUNCBOB;
	test_attach_floor_aas_geometry(-1.5f);

	aasworld.maxEntities = 9;
	aasworld.entities = calloc((size_t)aasworld.maxEntities, sizeof(aas_entity_t));
	assert_non_null(aasworld.entities);
	aasworld.entities[4].inuse = qtrue;
	aasworld.entities[4].solid = SOLID_BSP;
	aasworld.entities[4].modelindex = 9;
	VectorSet(aasworld.entities[4].origin, 50.0f, 0.0f, 0.0f);
	VectorSet(aasworld.entities[4].mins, -16.0f, -16.0f, -16.0f);
	VectorSet(aasworld.entities[4].maxs, 16.0f, 16.0f, 16.0f);

	int handle = BotAllocMoveStateHandle();
	assert_true(handle > 0);
	bot_movestate_t *ms = BotMoveStateFromHandle(handle);
	assert_non_null(ms);
	BotResetMoveStateHandle(handle);
	ms->client = 0;
	ms->entitynum = 5;
	ms->areanum = 1;
	ms->presencetype = PRESENCE_NORMAL;
	ms->moveflags = MFL_ONGROUND;
	VectorSet(ms->origin, -80.0f, 0.0f, 0.0f);

	bot_goal_t goal;
	memset(&goal, 0, sizeof(goal));
	goal.areanum = 2;
	VectorSet(goal.origin, 160.0f, 0.0f, 0.0f);

	bot_moveresult_t result;
	BotMoveToGoalHandle(&result, handle, &goal, TFL_DEFAULT);

	assert_false(result.failure);
	assert_int_equal(result.traveltype, TRAVEL_FUNCBOB);
	assert_int_equal(result.type, 0);
	assert_int_equal(result.flags, 0);
	assert_float_equal(ms->reachability_time - aasworld.time, 8.0f, 0.0001f);

	bot_input_t input;
	assert_int_equal(EA_GetInput(0, 0.1f, &input), BLERR_NOERROR);
	assert_float_equal(input.speed, 0.0f, 0.0001f);

	BotFreeMoveStateHandle(handle);
	free(aasworld.areasettings);
	free(aasworld.reachability);
	free(aasworld.entities);
	aasworld.areasettings = NULL;
	aasworld.reachability = NULL;
	aasworld.entities = NULL;
	aasworld.numAreas = 0;
	aasworld.numAreaSettings = 0;
	aasworld.numReachability = 0;
	aasworld.maxEntities = 0;
}

/*
=============
test_aas_clientmove_retail_layout

Pin the exact 0x50-byte Gladiator client-movement result and field offsets.
=============
*/
static void test_aas_clientmove_retail_layout(void **state)
{
	(void)state;

	assert_int_equal(sizeof(aas_clientmove_trace_t), 0x24U);
	assert_int_equal(offsetof(aas_clientmove_trace_t, startsolid), 0x00U);
	assert_int_equal(offsetof(aas_clientmove_trace_t, fraction), 0x04U);
	assert_int_equal(offsetof(aas_clientmove_trace_t, endpos), 0x08U);
	assert_int_equal(offsetof(aas_clientmove_trace_t, ent), 0x14U);
	assert_int_equal(offsetof(aas_clientmove_trace_t, lastarea), 0x18U);
	assert_int_equal(offsetof(aas_clientmove_trace_t, area), 0x1cU);
	assert_int_equal(offsetof(aas_clientmove_trace_t, planenum), 0x20U);

	assert_int_equal(sizeof(aas_clientmove_t), 0x50U);
	assert_int_equal(offsetof(aas_clientmove_t, endpos), 0x00U);
	assert_int_equal(offsetof(aas_clientmove_t, velocity), 0x0cU);
	assert_int_equal(offsetof(aas_clientmove_t, trace), 0x18U);
	assert_int_equal(offsetof(aas_clientmove_t, presencetype), 0x3cU);
	assert_int_equal(offsetof(aas_clientmove_t, stopevent), 0x40U);
	assert_int_equal(offsetof(aas_clientmove_t, endcontents), 0x44U);
	assert_int_equal(offsetof(aas_clientmove_t, time), 0x48U);
	assert_int_equal(offsetof(aas_clientmove_t, frames), 0x4cU);
}

/*
=============
test_aas_swimming_ignores_aas_area_contents

Pin retail's raw BSP point-contents probe without an AAS-area fallback.
=============
*/
static void test_aas_swimming_ignores_aas_area_contents(void **state)
{
	(void)state;

	test_setup_open_aas_trace_world();
	g_predict_trace_settings[1].contents = AAS_AREACONTENTS_WATER |
		AAS_AREACONTENTS_SLIME | AAS_AREACONTENTS_LAVA;

	vec3_t origin;
	VectorSet(origin, 1.0f, 2.0f, 3.0f);
	assert_false(AAS_Swimming(origin));
	assert_int_equal(g_point_contents_call_count, 1);
	assert_float_equal(g_point_contents_log[0][0], 1.0f, 0.0001f);
	assert_float_equal(g_point_contents_log[0][1], 2.0f, 0.0001f);
	assert_float_equal(g_point_contents_log[0][2], 1.0f, 0.0001f);
}

/*
=============
test_aas_on_ground_uses_retail_probe_distance

Pin retail's four-unit trace and two-unit accepted ground distance.
=============
*/
static void test_aas_on_ground_uses_retail_probe_distance(void **state)
{
	(void)state;

	test_setup_floor_aas_trace_world(0.0f);
	vec3_t origin;
	VectorSet(origin, 0.0f, 0.0f, 3.0f);
	assert_false(AAS_OnGround(origin, PRESENCE_NORMAL, 3));

	VectorSet(origin, 0.0f, 0.0f, 1.5f);
	assert_true(AAS_OnGround(origin, PRESENCE_NORMAL, 3));
	assert_int_equal(g_trace_call_count, 0);
}

/*
=============
test_aas_predict_client_movement_uses_waterfriction_on_ground

Pins Gladiator's retail use of sv_waterfriction outside liquid while grounded.
=============
*/
static void test_aas_predict_client_movement_uses_waterfriction_on_ground(void **state)
{
	(void)state;

	LibVarSet("sv_friction", "2");
	LibVarSet("sv_waterfriction", "0.5");
	test_setup_open_aas_trace_world();

	aas_clientmove_t move;
	vec3_t origin;
	vec3_t velocity;
	vec3_t cmdmove;
	VectorSet(origin, 0.0f, 0.0f, 0.0f);
	VectorSet(velocity, 200.0f, 0.0f, 0.0f);
	VectorClear(cmdmove);

	assert_true(AAS_PredictClientMovement(&move,
	                                      3,
	                                      origin,
	                                      PRESENCE_NORMAL,
	                                      qtrue,
	                                      velocity,
	                                      cmdmove,
	                                      0,
	                                      1,
	                                      0.1f,
	                                      SE_NONE,
	                                      0,
	                                      qfalse));
	assert_int_equal(move.frames, 1);
	assert_int_equal(move.stopevent, SE_NONE);
	assert_float_equal(move.endpos[0], 19.0f, 0.0001f);
	assert_float_equal(move.endpos[1], 0.0f, 0.0001f);
	assert_int_equal(g_trace_call_count, 0);
}

/*
=============
test_aas_predict_client_movement_uses_friction_while_swimming

Pins Gladiator's retail use of sv_friction while swimming.
=============
*/
static void test_aas_predict_client_movement_uses_friction_while_swimming(void **state)
{
	(void)state;

	LibVarSet("sv_friction", "2");
	LibVarSet("sv_waterfriction", "0.5");
	test_setup_open_aas_trace_world();
	g_point_contents_results[0] = CONTENTS_WATER;
	g_point_contents_result_count = 1;

	aas_clientmove_t move;
	vec3_t origin;
	vec3_t velocity;
	vec3_t cmdmove;
	VectorClear(origin);
	VectorSet(velocity, 200.0f, 0.0f, 0.0f);
	VectorClear(cmdmove);

	assert_true(AAS_PredictClientMovement(&move,
		3,
		origin,
		PRESENCE_NORMAL,
		qfalse,
		velocity,
		cmdmove,
		0,
		1,
		0.1f,
		SE_NONE,
		0,
		qfalse));
	assert_float_equal(move.endpos[0], 16.0f, 0.0001f);
	assert_int_equal(g_trace_call_count, 0);
	assert_int_equal(g_point_contents_call_count, 2);
}

/*
=============
test_aas_predict_client_movement_skips_zero_maxframes

Pins retail's signed pre-loop exit when the maximum frame count is zero.
=============
*/
static void test_aas_predict_client_movement_skips_zero_maxframes(void **state)
{
	(void)state;

	test_setup_blocked_aas_trace_world();
	Q2Bridge_SetDebugLinesEnabled(true);

	aas_clientmove_t move;
	vec3_t origin;
	vec3_t velocity;
	vec3_t cmdmove;
	VectorSet(origin, 1.0f, 2.0f, 3.0f);
	VectorSet(velocity, 100.0f, -50.0f, 25.0f);
	VectorSet(cmdmove, 400.0f, 200.0f, 100.0f);

	assert_true(AAS_PredictClientMovement(&move,
		3,
		origin,
		PRESENCE_NORMAL,
		qtrue,
		velocity,
		cmdmove,
		3,
		0,
		0.1f,
		SE_HITGROUND | SE_LEAVEGROUND | SE_GAP,
		0,
		qtrue));
	assert_float_equal(move.endpos[0], 1.0f, 0.0001f);
	assert_float_equal(move.endpos[1], 2.0f, 0.0001f);
	assert_float_equal(move.endpos[2], 3.25f, 0.0001f);
	assert_float_equal(move.velocity[0], 10.0f, 0.0001f);
	assert_float_equal(move.velocity[1], -5.0f, 0.0001f);
	assert_float_equal(move.velocity[2], 2.5f, 0.0001f);
	assert_int_equal(move.frames, 0);
	assert_float_equal(move.time, 0.0f, 0.0001f);
	assert_int_equal(move.stopevent, SE_NONE);
	assert_int_equal(move.endcontents, 4);
	assert_int_equal(g_trace_call_count, 0);
	assert_int_equal(g_point_contents_call_count, 0);
	assert_int_equal(g_debug_line_create_count, 0);
	assert_int_equal(g_debug_line_show_count, 0);
	assert_int_equal(g_bridge_print_count, 0);
}

/*
=============
test_aas_predict_client_movement_skips_negative_maxframes

Pins retail's signed pre-loop exit for negative maximum frame counts.
=============
*/
static void test_aas_predict_client_movement_skips_negative_maxframes(void **state)
{
	(void)state;

	test_setup_blocked_aas_trace_world();
	Q2Bridge_SetDebugLinesEnabled(true);

	aas_clientmove_t move;
	vec3_t origin;
	vec3_t velocity;
	vec3_t cmdmove;
	VectorSet(origin, -3.0f, 4.0f, 5.0f);
	VectorSet(velocity, -20.0f, 30.0f, 40.0f);
	VectorSet(cmdmove, 200.0f, -400.0f, 300.0f);

	assert_true(AAS_PredictClientMovement(&move,
		3,
		origin,
		PRESENCE_NORMAL,
		qfalse,
		velocity,
		cmdmove,
		2,
		-4,
		0.1f,
		SE_HITGROUND | SE_LEAVEGROUND | SE_GAP,
		0,
		qtrue));
	assert_float_equal(move.endpos[0], -3.0f, 0.0001f);
	assert_float_equal(move.endpos[1], 4.0f, 0.0001f);
	assert_float_equal(move.endpos[2], 5.25f, 0.0001f);
	assert_float_equal(move.velocity[0], -2.0f, 0.0001f);
	assert_float_equal(move.velocity[1], 3.0f, 0.0001f);
	assert_float_equal(move.velocity[2], 4.0f, 0.0001f);
	assert_int_equal(move.frames, 0);
	assert_float_equal(move.time, 0.0f, 0.0001f);
	assert_int_equal(move.stopevent, SE_NONE);
	assert_int_equal(move.endcontents, 4);
	assert_int_equal(g_trace_call_count, 0);
	assert_int_equal(g_point_contents_call_count, 0);
	assert_int_equal(g_debug_line_create_count, 0);
	assert_int_equal(g_debug_line_show_count, 0);
	assert_int_equal(g_bridge_print_count, 0);
}

/*
=============
test_aas_predict_client_movement_uses_signed_cmdframes_gate

Pins zero and negative command-frame counts as inactive signed loop bounds.
=============
*/
static void test_aas_predict_client_movement_uses_signed_cmdframes_gate(void **state)
{
	(void)state;

	test_setup_open_aas_trace_world();
	vec3_t origin;
	vec3_t velocity;
	vec3_t cmdmove;
	VectorClear(origin);
	VectorClear(velocity);
	VectorSet(cmdmove, 100.0f, 0.0f, 0.0f);

	aas_clientmove_t negative_move;
	assert_true(AAS_PredictClientMovement(&negative_move,
		3,
		origin,
		PRESENCE_NORMAL,
		qtrue,
		velocity,
		cmdmove,
		-2,
		1,
		0.1f,
		SE_NONE,
		0,
		qfalse));
	assert_float_equal(negative_move.endpos[0], 0.0f, 0.0001f);

	aas_clientmove_t zero_move;
	assert_true(AAS_PredictClientMovement(&zero_move,
		3,
		origin,
		PRESENCE_NORMAL,
		qtrue,
		velocity,
		cmdmove,
		0,
		1,
		0.1f,
		SE_NONE,
		0,
		qfalse));
	assert_float_equal(zero_move.endpos[0], 0.0f, 0.0001f);

	aas_clientmove_t active_move;
	assert_true(AAS_PredictClientMovement(&active_move,
		3,
		origin,
		PRESENCE_NORMAL,
		qtrue,
		velocity,
		cmdmove,
		1,
		1,
		0.1f,
		SE_NONE,
		0,
		qfalse));
	assert_float_equal(active_move.endpos[0], 10.0f, 0.0001f);
	assert_int_equal(g_trace_call_count, 0);
}

/*
=============
test_aas_predict_client_movement_uses_zero_frametime_directly

Pins retail's finite zero-time jump bias without a successor safety clamp.
=============
*/
static void test_aas_predict_client_movement_uses_zero_frametime_directly(void **state)
{
	(void)state;

	test_setup_open_aas_trace_world();
	aas_clientmove_t move;
	vec3_t origin;
	vec3_t velocity;
	vec3_t cmdmove;
	VectorClear(origin);
	VectorSet(velocity, 100.0f, -50.0f, 25.0f);
	VectorSet(cmdmove, 0.0f, 0.0f, 400.0f);

	assert_true(AAS_PredictClientMovement(&move,
		3,
		origin,
		PRESENCE_NORMAL,
		qtrue,
		velocity,
		cmdmove,
		1,
		1,
		0.0f,
		SE_NONE,
		0,
		qfalse));
	assert_float_equal(move.endpos[0], 0.0f, 0.0001f);
	assert_float_equal(move.endpos[1], 0.0f, 0.0001f);
	assert_float_equal(move.endpos[2], 5.25f, 0.0001f);
	assert_float_equal(move.velocity[0], 0.0f, 0.0001f);
	assert_float_equal(move.velocity[1], 0.0f, 0.0001f);
	assert_float_equal(move.velocity[2], 5.0f, 0.0001f);
	assert_float_equal(move.time, 0.0f, 0.0001f);
	assert_int_equal(move.frames, 1);
	assert_int_equal(move.stopevent, SE_NONE);
	aas_clientmove_trace_t zero_trace;
	memset(&zero_trace, 0, sizeof(zero_trace));
	assert_memory_equal(&move.trace, &zero_trace, sizeof(move.trace));
	assert_int_equal(move.endcontents, 4);
	assert_int_equal(g_trace_call_count, 0);
}

/*
=============
test_aas_predict_client_movement_uses_negative_frametime_directly

Pins retail's signed displacement and gravity arithmetic for negative time.
=============
*/
static void test_aas_predict_client_movement_uses_negative_frametime_directly(void **state)
{
	(void)state;

	test_setup_open_aas_trace_world();
	aas_clientmove_t move;
	vec3_t origin;
	vec3_t velocity;
	vec3_t cmdmove;
	VectorClear(origin);
	VectorSet(velocity, 100.0f, -20.0f, 0.0f);
	VectorClear(cmdmove);

	assert_true(AAS_PredictClientMovement(&move,
		3,
		origin,
		PRESENCE_NORMAL,
		qfalse,
		velocity,
		cmdmove,
		0,
		1,
		-0.1f,
		SE_NONE,
		0,
		qfalse));
	assert_float_equal(move.endpos[0], -10.0f, 0.0001f);
	assert_float_equal(move.endpos[1], 2.0f, 0.0001f);
	assert_float_equal(move.endpos[2], 8.25f, 0.0001f);
	assert_float_equal(move.velocity[0], -10.0f, 0.0001f);
	assert_float_equal(move.velocity[1], 2.0f, 0.0001f);
	assert_float_equal(move.velocity[2], 8.0f, 0.0001f);
	assert_float_equal(move.time, -0.1f, 0.0001f);
	assert_int_equal(move.frames, 1);
	assert_int_equal(move.stopevent, SE_NONE);
	aas_clientmove_trace_t zero_trace;
	memset(&zero_trace, 0, sizeof(zero_trace));
	assert_memory_equal(&move.trace, &zero_trace, sizeof(move.trace));
	assert_int_equal(move.endcontents, 4);
	assert_int_equal(g_trace_call_count, 0);
}

/*
=============
test_aas_predict_client_movement_uses_planenum_plane_table

Pins retail collision response to the AAS plane table rather than the embedded
successor trace plane.
=============
*/
static void test_aas_predict_client_movement_uses_planenum_plane_table(void **state)
{
	(void)state;

	test_setup_offset_entity_aas_trace_world();
	aas_clientmove_t move;
	vec3_t origin;
	vec3_t velocity;
	vec3_t cmdmove;
	VectorClear(origin);
	VectorSet(velocity, 200.0f, 0.0f, -500.0f);
	VectorClear(cmdmove);

	assert_true(AAS_PredictClientMovement(&move,
		2,
		origin,
		PRESENCE_NORMAL,
		qfalse,
		velocity,
		cmdmove,
		0,
		1,
		0.1f,
		SE_HITGROUNDDAMAGE,
		0,
		qfalse));
	assert_int_equal(move.stopevent, SE_HITGROUNDDAMAGE);
	assert_int_equal(move.trace.ent, 3);
	assert_int_equal(move.trace.planenum, 0);
	assert_int_equal(move.endcontents, 4);
	assert_float_equal(g_predict_trace_planes[0].normal[0], 0.0f, 0.0001f);
	assert_float_equal(g_predict_trace_planes[0].normal[2], 1.0f, 0.0001f);
	assert_float_equal(move.velocity[0], 20.0f, 0.0001f);
	assert_float_equal(move.velocity[2], 0.0f, 0.0001f);
	assert_int_equal(g_trace_call_count, 0);
}

/*
=============
test_aas_predict_client_movement_requires_exact_vertical_plane

Pins retail's exact zero comparison for the step-plane vertical component.
=============
*/
static void test_aas_predict_client_movement_requires_exact_vertical_plane(void **state)
{
	(void)state;

	test_setup_near_vertical_step_aas_trace_world();
	Q2Bridge_SetDebugLinesEnabled(true);

	aas_clientmove_t move;
	vec3_t origin;
	vec3_t velocity;
	vec3_t cmdmove;
	VectorClear(origin);
	VectorSet(velocity, 200.0f, 0.0f, 80.0f);
	VectorClear(cmdmove);

	assert_true(AAS_PredictClientMovement(&move,
		3,
		origin,
		PRESENCE_NORMAL,
		qtrue,
		velocity,
		cmdmove,
		0,
		1,
		0.1f,
		SE_NONE,
		0,
		qtrue));
	assert_int_equal(g_debug_line_create_count, 2);
	assert_int_equal(g_debug_line_show_count, 2);
	assert_int_equal(g_debug_line_color_log[0], (int)LINECOLOR_RED);
	assert_int_equal(g_debug_line_color_log[1], (int)LINECOLOR_RED);
	assert_true(move.endpos[2] > 0.0f);
	assert_true(move.endpos[2] < 1.0f);
	assert_int_equal(g_trace_call_count, 0);
}

/*
=============
test_aas_predict_client_movement_defaults_liquid_trace

Pins retail's zeroed result trace when a feet-contents event stops prediction.
=============
*/
static void test_aas_predict_client_movement_defaults_liquid_trace(void **state)
{
	(void)state;

	test_setup_open_aas_trace_world();
	g_point_contents_results[0] = 0;
	g_point_contents_results[1] = CONTENTS_LAVA;
	g_point_contents_result_count = 2;

	aas_clientmove_t move;
	vec3_t origin;
	vec3_t velocity;
	vec3_t cmdmove;
	VectorClear(origin);
	VectorClear(velocity);
	VectorClear(cmdmove);

	assert_true(AAS_PredictClientMovement(&move,
		3,
		origin,
		PRESENCE_NORMAL,
		qfalse,
		velocity,
		cmdmove,
		0,
		1,
		0.1f,
		SE_ENTERLAVA,
		0,
		qfalse));
	aas_clientmove_trace_t zero_trace;
	memset(&zero_trace, 0, sizeof(zero_trace));
	assert_int_equal(move.stopevent, SE_ENTERLAVA);
	assert_int_equal(move.endcontents, CONTENTS_LAVA);
	assert_memory_equal(&move.trace, &zero_trace, sizeof(move.trace));
	assert_int_equal(g_trace_call_count, 0);
}

/*
=============
test_aas_predict_client_movement_maps_water_to_slime_event

Pin the retail feet-probe quirk and low-byte stop-event argument behavior.
=============
*/
static void test_aas_predict_client_movement_maps_water_to_slime_event(void **state)
{
	(void)state;

	test_setup_open_aas_trace_world();
	g_point_contents_results[0] = 0;
	g_point_contents_results[1] = CONTENTS_WATER;
	g_point_contents_result_count = 2;

	aas_clientmove_t move;
	vec3_t origin;
	vec3_t velocity;
	vec3_t cmdmove;
	VectorClear(origin);
	VectorClear(velocity);
	VectorClear(cmdmove);

	assert_true(AAS_PredictClientMovement(&move,
		3,
		origin,
		PRESENCE_NORMAL,
		qfalse,
		velocity,
		cmdmove,
		0,
		1,
		0.1f,
		SE_ENTERSLIME | SE_TOUCHTELEPORTER,
		0,
		qfalse));
	assert_int_equal(move.stopevent, SE_ENTERSLIME);
	assert_int_equal(move.endcontents, CONTENTS_WATER);
	assert_int_equal(g_point_contents_call_count, 2);
}

/*
=============
test_aas_predict_client_movement_ignores_area_liquid_event

Pin the retail feet-event path to raw BSP contents rather than AAS area flags.
=============
*/
static void test_aas_predict_client_movement_ignores_area_liquid_event(void **state)
{
	(void)state;

	test_setup_open_aas_trace_world();
	g_predict_trace_settings[1].contents = AAS_AREACONTENTS_WATER;

	aas_clientmove_t move;
	vec3_t origin;
	vec3_t velocity;
	vec3_t cmdmove;
	VectorClear(origin);
	VectorClear(velocity);
	VectorClear(cmdmove);

	assert_true(AAS_PredictClientMovement(&move,
		3,
		origin,
		PRESENCE_NORMAL,
		qfalse,
		velocity,
		cmdmove,
		0,
		1,
		0.1f,
		SE_ENTERWATER,
		0,
		qfalse));
	assert_int_equal(move.stopevent, SE_NONE);
	assert_int_equal(g_point_contents_call_count, 2);
}

/*
=============
test_aas_predict_client_movement_skips_feet_probe_while_rising

Pin the retail non-positive vertical-displacement gate on feet contents.
=============
*/
static void test_aas_predict_client_movement_skips_feet_probe_while_rising(void **state)
{
	(void)state;

	test_setup_open_aas_trace_world();
	g_point_contents_results[0] = 0;
	g_point_contents_results[1] = CONTENTS_LAVA;
	g_point_contents_result_count = 2;

	aas_clientmove_t move;
	vec3_t origin;
	vec3_t velocity;
	vec3_t cmdmove;
	VectorClear(origin);
	VectorSet(velocity, 0.0f, 0.0f, 150.0f);
	VectorClear(cmdmove);

	assert_true(AAS_PredictClientMovement(&move,
		3,
		origin,
		PRESENCE_NORMAL,
		qfalse,
		velocity,
		cmdmove,
		0,
		1,
		0.1f,
		SE_ENTERLAVA,
		0,
		qfalse));
	assert_int_equal(move.stopevent, SE_NONE);
	assert_float_equal(move.velocity[2], 7.0f, 0.0001f);
	assert_float_equal(move.endpos[2], 7.25f, 0.0001f);
	assert_int_equal(g_point_contents_call_count, 1);
}

/*
=============
test_aas_predict_client_movement_uses_aas_trace_only

Pins the retail AAS trace path even when the engine trace import would block.
=============
*/
static void test_aas_predict_client_movement_uses_aas_trace_only(void **state)
{
	(void)state;

	test_setup_open_aas_trace_world();
	vec3_t blocked;
	VectorClear(blocked);
	test_set_trace_result(0, 0.0f, blocked, NULL);
	g_trace_results[0].startsolid = qtrue;
	g_trace_result_count = 1;

	aas_clientmove_t move;
	vec3_t origin;
	vec3_t velocity;
	vec3_t cmdmove;
	VectorClear(origin);
	VectorSet(velocity, 200.0f, 0.0f, 0.0f);
	VectorClear(cmdmove);

	assert_true(AAS_PredictClientMovement(&move,
		3,
		origin,
		PRESENCE_NORMAL,
		qfalse,
		velocity,
		cmdmove,
		0,
		1,
		0.1f,
		SE_NONE,
		0,
		qfalse));
	assert_float_equal(move.endpos[0], 20.0f, 0.0001f);
	aas_clientmove_trace_t zero_trace;
	memset(&zero_trace, 0, sizeof(zero_trace));
	assert_memory_equal(&move.trace, &zero_trace, sizeof(move.trace));
	assert_int_equal(move.endcontents, 4);
	assert_int_equal(g_trace_call_count, 0);
}

/*
=============
test_aas_predict_client_movement_forwards_pass_entity

Pins the predictor's unchanged entity number forwarding into the AAS trace.
=============
*/
static void test_aas_predict_client_movement_forwards_pass_entity(void **state)
{
	(void)state;

	test_setup_entity_aas_trace_world();
	aas_clientmove_t move;
	vec3_t origin;
	vec3_t velocity;
	vec3_t cmdmove;
	VectorClear(origin);
	VectorClear(velocity);
	VectorClear(cmdmove);

	assert_true(AAS_PredictClientMovement(&move,
		3,
		origin,
		PRESENCE_NORMAL,
		qfalse,
		velocity,
		cmdmove,
		0,
		1,
		0.1f,
		SE_NONE,
		0,
		qfalse));

	memset(&move, 0xa5, sizeof(move));
	assert_false(AAS_PredictClientMovement(&move,
		2,
		origin,
		PRESENCE_NORMAL,
		qfalse,
		velocity,
		cmdmove,
		0,
		1,
		0.1f,
		SE_NONE,
		0,
		qfalse));
	aas_clientmove_t zero_move;
	memset(&zero_move, 0, sizeof(zero_move));
	assert_memory_equal(&move, &zero_move, sizeof(move));
	assert_int_equal(g_trace_call_count, 0);
}

/*
=============
test_aas_predict_client_movement_zeroes_result_at_clip_limit

Pins retail's zero-result abort before accepting a clear twenty-first trace.
=============
*/
static void test_aas_predict_client_movement_zeroes_result_at_clip_limit(void **state)
{
	(void)state;

	test_setup_blocked_aas_trace_world();

	aas_clientmove_t move;
	aas_clientmove_t zero_move;
	memset(&move, 0xa5, sizeof(move));
	memset(&zero_move, 0, sizeof(zero_move));

	vec3_t origin;
	vec3_t velocity;
	vec3_t cmdmove;
	VectorClear(origin);
	VectorSet(velocity, 200.0f, 0.0f, 0.0f);
	VectorClear(cmdmove);

	assert_false(AAS_PredictClientMovement(&move,
		3,
		origin,
		PRESENCE_NORMAL,
		qfalse,
		velocity,
		cmdmove,
		0,
		1,
		0.1f,
		SE_NONE,
		0,
		qfalse));
	assert_int_equal(g_trace_call_count, 0);
	assert_int_equal(g_point_contents_call_count, 1);
	assert_memory_equal(&move, &zero_move, sizeof(move));
}

/*
=============
test_aas_predict_client_movement_clamps_retail_ground_axes

Pins Gladiator's independent sv_maxacceleration clamp for ground X/Y input.
=============
*/
static void test_aas_predict_client_movement_clamps_retail_ground_axes(void **state)
{
	(void)state;

	LibVarSet("sv_maxacceleration", "50");
	test_setup_open_aas_trace_world();
	aas_clientmove_t move;
	vec3_t origin;
	vec3_t velocity;
	vec3_t cmdmove;
	VectorClear(origin);
	VectorClear(velocity);
	VectorSet(cmdmove, 100.0f, -100.0f, 0.0f);

	assert_true(AAS_PredictClientMovement(&move,
		3,
		origin,
		PRESENCE_NORMAL,
		qtrue,
		velocity,
		cmdmove,
		1,
		1,
		0.1f,
		SE_NONE,
		0,
		qfalse));
	assert_float_equal(move.endpos[0], 5.0f, 0.0001f);
	assert_float_equal(move.endpos[1], -5.0f, 0.0001f);
	assert_int_equal(g_trace_call_count, 0);
}

/*
=============
test_aas_predict_client_movement_ignores_air_command_acceleration

Pins retail's zero command axes while airborne and outside liquid.
=============
*/
static void test_aas_predict_client_movement_ignores_air_command_acceleration(void **state)
{
	(void)state;
	test_setup_open_aas_trace_world();

	aas_clientmove_t move;
	vec3_t origin;
	vec3_t velocity;
	vec3_t cmdmove;
	VectorClear(origin);
	VectorClear(velocity);
	VectorSet(cmdmove, 400.0f, 0.0f, 0.0f);

	assert_true(AAS_PredictClientMovement(&move,
		3,
		origin,
		PRESENCE_NORMAL,
		qfalse,
		velocity,
		cmdmove,
		1,
		1,
		0.1f,
		SE_NONE,
		0,
		qfalse));
	assert_float_equal(move.endpos[0], 0.0f, 0.0001f);
	assert_int_equal(g_trace_call_count, 0);
}

/*
=============
test_aas_predict_client_movement_clamps_three_swim_axes

Pins retail swimming command acceleration across X, Y, and Z independently.
=============
*/
static void test_aas_predict_client_movement_clamps_three_swim_axes(void **state)
{
	(void)state;

	LibVarSet("sv_maxacceleration", "50");
	test_setup_open_aas_trace_world();
	g_point_contents_results[0] = CONTENTS_WATER;
	g_point_contents_result_count = 1;

	aas_clientmove_t move;
	vec3_t origin;
	vec3_t velocity;
	vec3_t cmdmove;
	VectorClear(origin);
	VectorClear(velocity);
	VectorSet(cmdmove, 100.0f, -100.0f, 100.0f);

	assert_true(AAS_PredictClientMovement(&move,
		3,
		origin,
		PRESENCE_NORMAL,
		qfalse,
		velocity,
		cmdmove,
		1,
		1,
		0.1f,
		SE_NONE,
		0,
		qfalse));
	assert_float_equal(move.endpos[0], 5.0f, 0.0001f);
	assert_float_equal(move.endpos[1], -5.0f, 0.0001f);
	assert_float_equal(move.endpos[2], 1.25f, 0.0001f);
	assert_int_equal(g_trace_call_count, 0);
	assert_int_equal(g_point_contents_call_count, 1);
}

/*
=============
test_aas_predict_client_movement_visualizes_trace_and_startsolid

Pins Gladiator's visualize flag, start-solid diagnostic, and red trace line.
=============
*/
static void test_aas_predict_client_movement_visualizes_trace_and_startsolid(void **state)
{
	(void)state;

	test_setup_blocked_aas_trace_world();
	Q2Bridge_SetDebugLinesEnabled(true);

	aas_clientmove_t move;
	vec3_t origin;
	vec3_t velocity;
	vec3_t cmdmove;
	VectorClear(origin);
	VectorSet(velocity, 200.0f, 0.0f, 0.0f);
	VectorClear(cmdmove);

	assert_false(AAS_PredictClientMovement(&move,
		3,
		origin,
		PRESENCE_NORMAL,
		qtrue,
		velocity,
		cmdmove,
		0,
		1,
		0.1f,
		SE_NONE,
		0,
		qtrue));
	assert_int_equal(g_bridge_print_count, 21);
	assert_int_equal(g_bridge_print_priority_log[0], PRT_MESSAGE);
	assert_string_equal(g_bridge_print_log[0], "PredictMovement: start solid\n");
	assert_int_equal(g_debug_line_create_count, 21);
	assert_int_equal(g_debug_line_show_count, 21);
	assert_int_equal(g_debug_line_show_id_log[0], 1);
	assert_int_equal(g_debug_line_color_log[0], (int)LINECOLOR_RED);
	assert_float_equal(g_debug_line_start_log[0][0], 0.0f, 0.0001f);
	assert_float_equal(g_debug_line_start_log[0][1], 0.0f, 0.0001f);
	assert_float_equal(g_debug_line_start_log[0][2], 0.25f, 0.0001f);
	assert_float_equal(g_debug_line_end_log[0][0], 0.0f, 0.0001f);
	assert_float_equal(g_debug_line_end_log[0][1], 0.0f, 0.0001f);
	assert_float_equal(g_debug_line_end_log[0][2], 0.25f, 0.0001f);
	assert_int_equal(g_debug_event_count, 63);
	assert_int_equal(g_debug_event_log[0], TEST_DEBUG_EVENT_PRINT);
	assert_int_equal(g_debug_event_log[1], TEST_DEBUG_EVENT_CREATE);
	assert_int_equal(g_debug_event_log[2], TEST_DEBUG_EVENT_SHOW);
	assert_int_equal(g_trace_call_count, 0);

	test_reset_trace_log();
	test_reset_debug_visualization_log();
	assert_false(AAS_PredictClientMovement(&move,
		3,
		origin,
		PRESENCE_NORMAL,
		qtrue,
		velocity,
		cmdmove,
		0,
		1,
		0.1f,
		SE_NONE,
		0,
		qfalse));
	assert_int_equal(g_bridge_print_count, 0);
	assert_int_equal(g_debug_line_create_count, 0);
	assert_int_equal(g_debug_line_show_count, 0);
}

/*
=============
test_aas_predict_client_movement_visualizes_retail_step

Pins the accepted-step blue line between the surrounding red movement traces.
=============
*/
static void test_aas_predict_client_movement_visualizes_retail_step(void **state)
{
	(void)state;

	test_setup_step_aas_trace_world();
	Q2Bridge_SetDebugLinesEnabled(true);

	aas_clientmove_t move;
	vec3_t origin;
	vec3_t velocity;
	vec3_t cmdmove;
	VectorClear(origin);
	VectorSet(velocity, 200.0f, 0.0f, 80.0f);
	VectorClear(cmdmove);

	assert_true(AAS_PredictClientMovement(&move,
		3,
		origin,
		PRESENCE_NORMAL,
		qtrue,
		velocity,
		cmdmove,
		0,
		1,
		0.1f,
		SE_NONE,
		0,
		qtrue));
	assert_int_equal(g_bridge_print_count, 0);
	assert_int_equal(g_debug_line_create_count, 3);
	assert_int_equal(g_debug_line_show_count, 3);
	assert_int_equal(g_debug_event_count, 6);
	for (int i = 0; i < 3; ++i)
	{
		assert_int_equal(g_debug_line_show_id_log[i], i + 1);
		assert_int_equal(g_debug_event_log[i * 2], TEST_DEBUG_EVENT_CREATE);
		assert_int_equal(g_debug_event_log[i * 2 + 1], TEST_DEBUG_EVENT_SHOW);
	}
	assert_int_equal(g_debug_line_color_log[0], (int)LINECOLOR_RED);
	assert_int_equal(g_debug_line_color_log[1], (int)LINECOLOR_BLUE);
	assert_int_equal(g_debug_line_color_log[2], (int)LINECOLOR_RED);
	assert_float_equal(g_debug_line_start_log[0][0], 0.0f, 0.0001f);
	assert_float_equal(g_debug_line_start_log[0][2], 0.25f, 0.0001f);
	assert_float_equal(g_debug_line_end_log[0][0], 3.75f, 0.0001f);
	assert_float_equal(g_debug_line_end_log[0][2], 0.25f, 0.0001f);
	assert_float_equal(g_debug_line_start_log[1][0], 3.75f, 0.0001f);
	assert_float_equal(g_debug_line_start_log[1][2], 0.25f, 0.0001f);
	assert_float_equal(g_debug_line_end_log[1][0], 3.75f, 0.0001f);
	assert_float_equal(g_debug_line_end_log[1][2], 8.25f, 0.0001f);
	assert_float_equal(g_debug_line_start_log[2][0], 3.75f, 0.0001f);
	assert_float_equal(g_debug_line_start_log[2][2], 8.25f, 0.0001f);
	assert_float_equal(g_debug_line_end_log[2][0], 17.75f, 0.0001f);
	assert_float_equal(g_debug_line_end_log[2][2], 8.25f, 0.0001f);
	assert_int_equal(g_trace_call_count, 0);
}

/*
=============
test_aas_predict_client_movement_uses_retail_ground_damage_cutoff

Pins Gladiator's 30-point ground-damage stop threshold. The equivalent Q3
path uses 40, so this 33.64-point landing distinguishes the retail behavior.
=============
*/
static void test_aas_predict_client_movement_uses_retail_ground_damage_cutoff(void **state)
{
	(void)state;

	test_setup_floor_aas_trace_world(-20.0f);

	aas_clientmove_t move;
	vec3_t origin;
	vec3_t velocity;
	vec3_t cmdmove;
	VectorClear(origin);
	VectorSet(velocity, 0.0f, 0.0f, -500.0f);
	VectorClear(cmdmove);

	assert_true(AAS_PredictClientMovement(&move,
		3,
		origin,
		PRESENCE_NORMAL,
		qfalse,
		velocity,
		cmdmove,
		0,
		1,
		0.1f,
		SE_HITGROUNDDAMAGE,
		0,
		qfalse));
	assert_int_equal(move.stopevent, SE_HITGROUNDDAMAGE);
	assert_int_equal(move.frames, 0);
	assert_float_equal(move.time, 0.0f, 0.0001f);
	assert_int_equal(g_trace_call_count, 0);
}

/*
=============
test_aas_predict_client_movement_ignores_q3_enter_area_stop

Gladiator's predictor has no stop-area argument or traced-area stop branch.
Keep the successor ABI parameter inert even when movement enters that area.
=============
*/
static void test_aas_predict_client_movement_ignores_q3_enter_area_stop(void **state)
{
	(void)state;

	test_setup_split_aas_trace_world(4.0f);

	aas_clientmove_t move;
	vec3_t origin;
	vec3_t velocity;
	vec3_t cmdmove;
	VectorClear(origin);
	VectorSet(velocity, 200.0f, 0.0f, 0.0f);
	VectorClear(cmdmove);

	assert_true(AAS_PredictClientMovement(&move,
		3,
		origin,
		PRESENCE_NORMAL,
		qtrue,
		velocity,
		cmdmove,
		0,
		1,
		0.1f,
		SE_ENTERAREA,
		2,
		qfalse));
	assert_int_equal(move.stopevent, SE_NONE);
	assert_int_equal(move.frames, 1);
	assert_int_equal(AAS_PointAreaNum(move.endpos), 2);
	assert_int_equal(g_trace_call_count, 0);
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
	test_setup_floor_aas_trace_world(0.0f);

	int handle = BotAllocMoveStateHandle();
	assert_true(handle > 0);

	bot_movestate_t *ms = BotMoveStateFromHandle(handle);
	assert_non_null(ms);
	BotResetMoveStateHandle(handle);
	ms->client = 0;
	ms->entitynum = 0;
	ms->moveflags = MFL_ONGROUND;
	ms->thinktime = 0.1f;

	vec3_t dir;
	VectorSet(dir, 1.0f, 1.0f, 0.0f);

	assert_true(BotMoveInDirectionHandle(handle, dir, 280.0f, MOVE_JUMP | MOVE_CROUCH));

	bot_input_t input;
	assert_int_equal(EA_GetInput(0, 0.1f, &input), BLERR_NOERROR);
	assert_float_equal(input.speed, 280.0f, 0.0001f);
	assert_float_equal(input.dir[0], 135.181854f, 0.0001f);
	assert_float_equal(input.dir[1], 135.181854f, 0.0001f);
	assert_float_equal(input.dir[2], 0.0f, 0.0001f);
	assert_true((input.actionflags & ACTION_JUMP) != 0);
	assert_true((input.actionflags & ACTION_CROUCH) != 0);
	assert_int_equal(ms->jumpreach, 0);
	assert_int_equal(g_trace_call_count, 0);

	BotFreeMoveStateHandle(handle);
}

/*
=============
test_bot_move_in_direction_barrier_jump_uses_retail_probe

Pins the retail internal-AAS up-forward-down barrier probe for direct walking.
=============
*/
static void test_bot_move_in_direction_barrier_jump_uses_retail_probe(void **state)
{
	(void)state;
	test_setup_step_aas_trace_world();
	g_predict_trace_planes[2].dist = 24.0f;
	g_predict_trace_planes[3].dist = -24.0f;

	int handle = BotAllocMoveStateHandle();
	assert_true(handle > 0);

	bot_movestate_t *ms = BotMoveStateFromHandle(handle);
	assert_non_null(ms);
	BotResetMoveStateHandle(handle);
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

	vec3_t dir;
	VectorSet(dir, 1.0f, 0.0f, 0.0f);

	assert_true(BotMoveInDirectionHandle(handle, dir, 200.0f, MOVE_WALK));
	assert_int_equal(g_trace_call_count, 0);

	bot_input_t input;
	assert_int_equal(EA_GetInput(0, 0.1f, &input), BLERR_NOERROR);
	assert_float_equal(input.speed, 200.0f, 0.0001f);
	assert_float_equal(input.dir[0], 1.0f, 0.0001f);
	assert_float_equal(input.dir[1], 0.0f, 0.0001f);
	assert_true((input.actionflags & ACTION_JUMP) != 0);
	assert_true((ms->moveflags & MFL_BARRIERJUMP) != 0);
	assert_int_equal(ms->jumpreach, 0);

	BotFreeMoveStateHandle(handle);
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

	int handle = BotAllocMoveStateHandle();
	assert_true(handle > 0);

	bot_movestate_t *ms = BotMoveStateFromHandle(handle);
	assert_non_null(ms);
	BotResetMoveStateHandle(handle);
	ms->client = 0;
	ms->entitynum = 3;
	ms->moveflags = MFL_BARRIERJUMP;
	ms->velocity[2] = 40.0f;

	vec3_t dir;
	VectorSet(dir, 0.0f, 2.0f, 0.0f);

	assert_true(BotMoveInDirectionHandle(handle, dir, 150.0f, MOVE_WALK));

	bot_input_t input;
	assert_int_equal(EA_GetInput(0, 0.1f, &input), BLERR_NOERROR);
	assert_float_equal(input.speed, 150.0f, 0.0001f);
	assert_float_equal(input.dir[0], 0.0f, 0.0001f);
	assert_float_equal(input.dir[1], 2.0f, 0.0001f);
	assert_true((input.actionflags & ACTION_JUMP) == 0);

	BotFreeMoveStateHandle(handle);
}

/*
=============
test_bot_move_in_direction_gap_probe_forces_jump

Pins retail's internal-AAS gap probe that promotes walking into a jump.
=============
*/
static void test_bot_move_in_direction_gap_probe_forces_jump(void **state)
{
	(void)state;
	test_setup_gap_aas_trace_world(4.0f, 24.0f);

	int handle = BotAllocMoveStateHandle();
	assert_true(handle > 0);

	bot_movestate_t *ms = BotMoveStateFromHandle(handle);
	assert_non_null(ms);
	BotResetMoveStateHandle(handle);
	ms->client = 0;
	ms->entitynum = 9;
	ms->moveflags = MFL_ONGROUND;
	ms->thinktime = 0.1f;
	VectorSet(ms->origin, 0.0f, 0.0f, 0.0f);

	vec3_t dir;
	VectorSet(dir, 1.0f, 0.0f, 0.0f);

	assert_true(BotMoveInDirectionHandle(handle, dir, 160.0f, MOVE_WALK));
	assert_int_equal(g_trace_call_count, 0);
	assert_true(g_point_contents_call_count >= 3);

	bot_input_t input;
	assert_int_equal(EA_GetInput(0, 0.1f, &input), BLERR_NOERROR);
	assert_float_equal(input.speed, 160.0f, 0.0001f);
	assert_float_equal(input.dir[0], 109.224289f, 0.0001f);
	assert_true((input.actionflags & ACTION_JUMP) != 0);
	assert_int_equal(ms->jumpreach, 0);

	BotFreeMoveStateHandle(handle);
}

/*
=============
test_bot_move_in_direction_gap_over_water_does_not_jump

Pins retail's water exception for direct-walk gap probing.
=============
*/
static void test_bot_move_in_direction_gap_over_water_does_not_jump(void **state)
{
	(void)state;
	test_setup_gap_aas_trace_world(4.0f, 12.0f);
	vec3_t watermins;
	vec3_t watermaxs;
	VectorSet(watermins, 4.0f, -100.0f, -200.0f);
	VectorSet(watermaxs, 12.0f, 100.0f, -1.0f);
	/*
	 * Retail AAS_PointContents forwards straight to the engine PointContents
	 * import (0x10003080: "return data_10063ff0(arg1)"), so the gap water has
	 * to be visible through the bridge mock rather than through an internal
	 * AAS BSP brush walk.
	 */
	test_attach_bridge_contents_box(watermins, watermaxs, CONTENTS_WATER);

	int handle = BotAllocMoveStateHandle();
	assert_true(handle > 0);

	bot_movestate_t *ms = BotMoveStateFromHandle(handle);
	assert_non_null(ms);
	BotResetMoveStateHandle(handle);
	ms->client = 0;
	ms->entitynum = 9;
	ms->moveflags = MFL_ONGROUND;
	ms->thinktime = 0.1f;
	VectorSet(ms->origin, 0.0f, 0.0f, 0.0f);

	vec3_t waterprobe;
	VectorSet(waterprobe, 8.0f, 0.0f, -80.0f);
	assert_int_equal(AAS_PointContents(waterprobe), CONTENTS_WATER);
	assert_int_equal(AAS_PointContents(ms->origin), 0);

	vec3_t dir;
	VectorSet(dir, 1.0f, 0.0f, 0.0f);

	assert_true(BotMoveInDirectionHandle(handle, dir, 160.0f, MOVE_WALK));
	assert_int_equal(g_trace_call_count, 0);
	/*
	 * Two explicit probes above plus exactly four engine probes from the move
	 * itself: BotMoveInDirection's own AAS_Swimming test, BotGapDistance's
	 * water exception, and the predictor's frame-0 swim and feet samples
	 * before AAS_OnGround stops it on SE_HITGROUND.
	 */
	assert_int_equal(g_point_contents_call_count, 6);
	assert_float_equal(g_point_contents_log[3][0], 8.0f, 0.0001f);
	assert_true(g_point_contents_log[3][2] < -25.0f);

	bot_input_t input;
	assert_int_equal(EA_GetInput(0, 0.1f, &input), BLERR_NOERROR);
	assert_float_equal(input.speed, 160.0f, 0.0001f);
	assert_float_equal(input.dir[0], 16.138197f, 0.0001f);
	assert_true((input.actionflags & ACTION_JUMP) == 0);
	assert_int_equal(ms->jumpreach, 0);

	BotFreeMoveStateHandle(handle);
}

/*
=============
test_bot_move_in_direction_prediction_rejects_lava

Pins retail AAS predictor stop-event rejection for direct walking into lava.
=============
*/
static void test_bot_move_in_direction_prediction_rejects_lava(void **state)
{
	(void)state;
	test_setup_floor_aas_trace_world(0.0f);

	int handle = BotAllocMoveStateHandle();
	assert_true(handle > 0);

	bot_movestate_t *ms = BotMoveStateFromHandle(handle);
	assert_non_null(ms);
	BotResetMoveStateHandle(handle);
	ms->client = 0;
	ms->entitynum = 4;
	ms->moveflags = MFL_ONGROUND;
	ms->thinktime = 0.1f;
	VectorSet(ms->origin, 0.0f, 0.0f, 0.0f);

	g_point_contents_result_count = 3;
	g_point_contents_results[0] = 0;
	g_point_contents_results[1] = 0;
	g_point_contents_results[2] = CONTENTS_LAVA;

	vec3_t dir;
	VectorSet(dir, 1.0f, 0.0f, 0.0f);

	assert_false(BotMoveInDirectionHandle(handle, dir, 160.0f, MOVE_WALK));
	assert_int_equal(g_trace_call_count, 0);
	assert_int_equal(g_point_contents_call_count, 3);

	bot_input_t input;
	assert_int_equal(EA_GetInput(0, 0.1f, &input), BLERR_NOERROR);
	assert_float_equal(input.speed, 0.0f, 0.0001f);
	assert_int_equal(input.actionflags, 0);

	BotFreeMoveStateHandle(handle);
}

/*
=============
test_bot_move_in_direction_prediction_rejects_short_progress

Pins retail's predicted-horizontal-progress blockage check.
=============
*/
static void test_bot_move_in_direction_prediction_rejects_short_progress(void **state)
{
	(void)state;
	test_setup_floor_aas_trace_world(0.0f);

	int handle = BotAllocMoveStateHandle();
	assert_true(handle > 0);

	bot_movestate_t *ms = BotMoveStateFromHandle(handle);
	assert_non_null(ms);
	BotResetMoveStateHandle(handle);
	ms->client = 0;
	ms->entitynum = 5;
	ms->moveflags = MFL_ONGROUND;
	ms->thinktime = 1.0f;
	VectorSet(ms->origin, 0.0f, 0.0f, 0.0f);

	vec3_t dir;
	VectorSet(dir, 1.0f, 0.0f, 0.0f);

	assert_false(BotMoveInDirectionHandle(handle, dir, 160.0f, MOVE_WALK));
	assert_int_equal(g_trace_call_count, 0);

	bot_input_t input;
	assert_int_equal(EA_GetInput(0, 0.1f, &input), BLERR_NOERROR);
	assert_float_equal(input.speed, 0.0f, 0.0001f);
	assert_int_equal(input.actionflags, 0);

	BotFreeMoveStateHandle(handle);
}

/*
=============
test_bot_travel_walk_targets_start_and_slows_for_gap

Pins retail walk travel's reach-start steering and eight-unit gap sampling.
=============
*/
static void test_bot_travel_walk_targets_start_and_slows_for_gap(void **state)
{
	(void)state;
	test_setup_gap_aas_trace_world(20.0f, 48.0f);

	aasworld.numAreas = 4;
	aasworld.numAreaSettings = 4;
	aasworld.areasettings = calloc((size_t)aasworld.numAreaSettings, sizeof(aas_areasettings_t));
	assert_non_null(aasworld.areasettings);
	aasworld.areasettings[1].areaflags = TEST_AAS_AREAFLAG_GROUNDED;
	aasworld.areasettings[1].presencetype = PRESENCE_NORMAL | PRESENCE_CROUCH;
	aasworld.areasettings[1].firstreachablearea = 1;
	aasworld.areasettings[1].numreachableareas = 1;
	aasworld.areasettings[2].presencetype = PRESENCE_NORMAL | PRESENCE_CROUCH;
	aasworld.areasettings[3].presencetype = PRESENCE_NORMAL | PRESENCE_CROUCH;

	aasworld.numReachability = 2;
	aasworld.reachability = calloc((size_t)aasworld.numReachability, sizeof(aas_reachability_t));
	assert_non_null(aasworld.reachability);
	aasworld.reachability[1].areanum = 2;
	aasworld.reachability[1].traveltype = TRAVEL_WALK;
	aasworld.reachability[1].traveltime = 1;
	VectorSet(aasworld.reachability[1].start, 64.0f, 32.0f, 0.0f);
	VectorSet(aasworld.reachability[1].end, 128.0f, 0.0f, 0.0f);
	aasworld.travelflagfortype[TRAVEL_WALK] = TFL_WALK;

	int handle = BotAllocMoveStateHandle();
	assert_true(handle > 0);
	bot_movestate_t *ms = BotMoveStateFromHandle(handle);
	assert_non_null(ms);
	BotResetMoveStateHandle(handle);
	ms->client = 0;
	ms->entitynum = 5;
	ms->areanum = 1;
	VectorSet(ms->origin, 0.0f, 0.0f, 0.0f);

	bot_goal_t goal;
	memset(&goal, 0, sizeof(goal));
	goal.areanum = 2;
	VectorSet(goal.origin, 128.0f, 0.0f, 0.0f);

	bot_moveresult_t result;
	result = BotTravel_Walk(ms, &aasworld.reachability[1]);

	assert_false(result.failure);
	assert_int_equal(result.traveltype, 0);
	assert_float_equal(result.movedir[0], 0.8944272f, 0.0001f);
	assert_float_equal(result.movedir[1], 0.4472136f, 0.0001f);
	assert_int_equal(g_trace_call_count, 1);
	assert_int_equal(g_trace_passent_log[0], 5);
	assert_int_equal(g_trace_contentmask_log[0], MASK_PLAYERSOLID);
	assert_float_equal(g_trace_end_log[0][0], 2.683281f, 0.0001f);
	assert_float_equal(g_trace_end_log[0][1], 1.341641f, 0.0001f);
	/*
	 * BotGapDistance's water exception probes the engine once for the gap it
	 * finds. Retail AAS_PointContents is a straight jump through the import
	 * table's PointContents slot (0x10003080 "return data_10063ff0(arg1)"), so
	 * that probe reaches the bridge mock; only the pre-fix internal AAS BSP
	 * brush walk kept the counter at zero here.
	 */
	assert_int_equal(g_point_contents_call_count, 1);
	assert_true(g_point_contents_log[0][2] < 0.0f);

	bot_input_t input;
	assert_int_equal(EA_GetInput(0, 0.1f, &input), BLERR_NOERROR);
	assert_float_equal(input.speed, 48.0f, 0.0001f);
	assert_float_equal(input.dir[0], 0.8944272f, 0.0001f);
	assert_float_equal(input.dir[1], 0.4472136f, 0.0001f);
	assert_true((input.actionflags & ACTION_CROUCH) == 0);

	BotFreeMoveStateHandle(handle);
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

Pins retail walk travel's near-start end steering and crouch-area action.
=============
*/
static void test_bot_travel_walk_switches_to_end_and_crouches_near_crouch_area(void **state)
{
	(void)state;
	test_setup_floor_aas_trace_world(0.0f);

	aasworld.numAreas = 2;
	aasworld.numAreaSettings = 3;
	aasworld.areasettings = calloc((size_t)aasworld.numAreaSettings, sizeof(aas_areasettings_t));
	assert_non_null(aasworld.areasettings);
	aasworld.areasettings[1].areaflags = TEST_AAS_AREAFLAG_GROUNDED;
	aasworld.areasettings[1].presencetype = PRESENCE_NORMAL | PRESENCE_CROUCH;
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

	int handle = BotAllocMoveStateHandle();
	assert_true(handle > 0);
	bot_movestate_t *ms = BotMoveStateFromHandle(handle);
	assert_non_null(ms);
	BotResetMoveStateHandle(handle);
	ms->client = 0;
	ms->entitynum = 5;
	ms->areanum = 1;
	VectorSet(ms->origin, 0.0f, 0.0f, 0.0f);

	bot_goal_t goal;
	memset(&goal, 0, sizeof(goal));
	goal.areanum = 2;
	VectorSet(goal.origin, 0.0f, 6.0f, 0.0f);

	bot_moveresult_t result;
	result = BotTravel_Walk(ms, &aasworld.reachability[1]);

	assert_false(result.failure);
	assert_int_equal(result.traveltype, 0);
	assert_float_equal(result.movedir[0], 0.0f, 0.0001f);
	assert_float_equal(result.movedir[1], 1.0f, 0.0001f);
	assert_int_equal(g_trace_call_count, 1);

	bot_input_t input;
	assert_int_equal(EA_GetInput(0, 0.1f, &input), BLERR_NOERROR);
	assert_float_equal(input.speed, 400.0f, 0.0001f);
	assert_float_equal(input.dir[0], 0.0f, 0.0001f);
	assert_float_equal(input.dir[1], 1.0f, 0.0001f);
	assert_true((input.actionflags & ACTION_CROUCH) != 0);

	BotFreeMoveStateHandle(handle);
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

	int handle = BotAllocMoveStateHandle();
	assert_true(handle > 0);
	bot_movestate_t *ms = BotMoveStateFromHandle(handle);
	assert_non_null(ms);
	BotResetMoveStateHandle(handle);
	ms->client = 0;
	ms->entitynum = 5;
	ms->areanum = 1;
	VectorClear(ms->origin);

	bot_goal_t goal;
	memset(&goal, 0, sizeof(goal));
	goal.areanum = 2;
	VectorSet(goal.origin, 30.0f, 40.0f, 0.0f);

	bot_moveresult_t result;
	result = BotTravel_Crouch(ms, &aasworld.reachability[1]);

	assert_false(result.failure);
	assert_int_equal(result.traveltype, 0);
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

	BotFreeMoveStateHandle(handle);
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

	int handle = BotAllocMoveStateHandle();
	assert_true(handle > 0);
	bot_movestate_t *ms = BotMoveStateFromHandle(handle);
	assert_non_null(ms);
	BotResetMoveStateHandle(handle);
	ms->client = 0;
	ms->entitynum = 5;
	ms->areanum = 1;
	VectorClear(ms->origin);

	bot_goal_t goal;
	memset(&goal, 0, sizeof(goal));
	goal.areanum = 2;
	VectorSet(goal.origin, 30.0f, 40.0f, 32.0f);

	bot_moveresult_t result;
	result = BotTravel_BarrierJump(ms, &aasworld.reachability[1]);

	assert_false(result.failure);
	assert_int_equal(result.traveltype, 0);
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

	BotFreeMoveStateHandle(handle);
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

	int handle = BotAllocMoveStateHandle();
	assert_true(handle > 0);
	bot_movestate_t *ms = BotMoveStateFromHandle(handle);
	assert_non_null(ms);
	BotResetMoveStateHandle(handle);
	ms->client = 0;
	ms->entitynum = 5;
	ms->areanum = 1;
	VectorClear(ms->origin);

	bot_goal_t goal;
	memset(&goal, 0, sizeof(goal));
	goal.areanum = 2;
	VectorSet(goal.origin, 6.0f, 0.0f, 32.0f);

	bot_moveresult_t result;
	result = BotTravel_BarrierJump(ms, &aasworld.reachability[1]);

	assert_false(result.failure);
	assert_int_equal(result.traveltype, 0);
	assert_float_equal(result.movedir[0], 1.0f, 0.0001f);
	assert_float_equal(result.movedir[1], 0.0f, 0.0001f);
	assert_int_equal(g_trace_call_count, 1);

	bot_input_t input;
	assert_int_equal(EA_GetInput(0, 0.1f, &input), BLERR_NOERROR);
	assert_float_equal(input.speed, 0.0f, 0.0001f);
	assert_true((input.actionflags & ACTION_JUMP) != 0);
	assert_true((ms->moveflags & MFL_BARRIERJUMP) == 0);
	assert_int_equal(ms->jumpreach, 0);

	BotFreeMoveStateHandle(handle);
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

	int handle = BotAllocMoveStateHandle();
	assert_true(handle > 0);
	bot_movestate_t *ms = BotMoveStateFromHandle(handle);
	assert_non_null(ms);
	BotResetMoveStateHandle(handle);
	ms->client = 0;
	ms->entitynum = 5;
	ms->areanum = 1;
	VectorClear(ms->origin);

	bot_goal_t goal;
	memset(&goal, 0, sizeof(goal));
	goal.areanum = 2;
	VectorSet(goal.origin, 128.0f, 0.0f, -32.0f);

	bot_moveresult_t result;
	result = BotTravel_WalkOffLedge(ms, &aasworld.reachability[1]);

	assert_false(result.failure);
	assert_int_equal(result.traveltype, 0);
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

	BotFreeMoveStateHandle(handle);
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

	int handle = BotAllocMoveStateHandle();
	assert_true(handle > 0);
	bot_movestate_t *ms = BotMoveStateFromHandle(handle);
	assert_non_null(ms);
	BotResetMoveStateHandle(handle);
	ms->client = 0;
	ms->entitynum = 5;
	ms->areanum = 1;
	VectorClear(ms->origin);

	bot_goal_t goal;
	memset(&goal, 0, sizeof(goal));
	goal.areanum = 2;
	VectorSet(goal.origin, 40.0f, 0.0f, -64.0f);

	bot_moveresult_t result;
	result = BotTravel_WalkOffLedge(ms, &aasworld.reachability[1]);

	assert_false(result.failure);
	assert_int_equal(result.traveltype, 0);
	assert_int_equal(g_trace_call_count, 2);

	bot_input_t input;
	assert_int_equal(EA_GetInput(0, 0.1f, &input), BLERR_NOERROR);
	assert_float_equal(input.speed, 100.0f, 0.0001f);
	assert_float_equal(input.dir[0], 1.0f, 0.0001f);
	assert_float_equal(input.dir[1], 0.0f, 0.0001f);

	BotFreeMoveStateHandle(handle);
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

	int handle = BotAllocMoveStateHandle();
	assert_true(handle > 0);
	bot_movestate_t *ms = BotMoveStateFromHandle(handle);
	assert_non_null(ms);
	BotResetMoveStateHandle(handle);
	ms->client = 0;
	ms->entitynum = 5;
	ms->areanum = 1;
	VectorSet(ms->origin, -10.0f, 0.0f, 0.0f);

	bot_goal_t goal;
	memset(&goal, 0, sizeof(goal));
	goal.areanum = 2;
	VectorSet(goal.origin, 10.0f, 0.0f, -32.0f);

	bot_moveresult_t result;
	result = BotTravel_WalkOffLedge(ms, &aasworld.reachability[1]);

	assert_false(result.failure);
	assert_int_equal(result.traveltype, 0);
	assert_int_equal(g_trace_call_count, 2);

	bot_input_t input;
	assert_int_equal(EA_GetInput(0, 0.1f, &input), BLERR_NOERROR);
	assert_float_equal(input.speed, 120.0f, 0.0001f);
	assert_float_equal(input.dir[0], 1.0f, 0.0001f);

	BotFreeMoveStateHandle(handle);
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

	int handle = BotAllocMoveStateHandle();
	assert_true(handle > 0);
	bot_movestate_t *ms = BotMoveStateFromHandle(handle);
	assert_non_null(ms);
	BotResetMoveStateHandle(handle);
	ms->client = 0;
	ms->entitynum = 5;
	ms->areanum = 1;
	VectorClear(ms->origin);

	bot_goal_t goal;
	memset(&goal, 0, sizeof(goal));
	goal.areanum = 2;
	VectorSet(goal.origin, 0.0f, 0.0f, 64.0f);

	bot_moveresult_t result;
	result = BotTravel_Ladder(ms, &aasworld.reachability[1]);

	assert_false(result.failure);
	assert_int_equal(result.traveltype, 0);
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

	BotFreeMoveStateHandle(handle);
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
test_bot_travel_grapple_uses_raw_two_degree_gate

Pin the raw retail angle gate where 16-bit AngleDelta rounds 2.001 degrees
below the two-degree movement threshold.
=============
*/
static void test_bot_travel_grapple_uses_raw_two_degree_gate(void **state)
{
	(void)state;

	bot_movestate_t movestate;
	memset(&movestate, 0, sizeof(movestate));
	movestate.client = 0;
	movestate.entitynum = 5;
	movestate.presencetype = PRESENCE_NORMAL;
	movestate.viewangles[1] = -2.001f;

	aas_reachability_t reach;
	memset(&reach, 0, sizeof(reach));
	reach.traveltype = TRAVEL_GRAPPLEHOOK;
	VectorSet(reach.start, 4.0f, 0.0f, 0.0f);
	VectorSet(reach.end, 100.0f, 0.0f, 0.0f);

	assert_true(AngleDelta(0.0f, movestate.viewangles[1]) < 2.0f);
	bot_moveresult_t result = BotTravel_Grapple(&movestate, &reach);

	assert_true((result.flags & MOVERESULT_MOVEMENTVIEW) != 0);
	assert_true((movestate.moveflags & MFL_GRAPPLEATTACHED) == 0);
	assert_int_equal(g_command_count, 0);
	assert_float_equal(result.movedir[0], 1.0f, 0.0001f);

	bot_input_t input;
	assert_int_equal(EA_GetInput(0, 0.1f, &input), BLERR_NOERROR);
	assert_float_equal(input.speed, 16.0f, 0.0001f);
}

/*
=============
test_bot_travel_grapple_attaches_with_retail_distance_sentinel

Pin the aligned-view hook-on branch, including the raw 0x497423f0 pattern the
original writes into lastgrappledist so the first attached frame always reads
as closing distance.
=============
*/
static void test_bot_travel_grapple_attaches_with_retail_distance_sentinel(void **state)
{
	(void)state;

	bot_movestate_t movestate;
	memset(&movestate, 0, sizeof(movestate));
	movestate.client = 0;
	movestate.entitynum = 5;
	movestate.presencetype = PRESENCE_NORMAL;
	movestate.lastgrappledist = 12.0f;

	aas_reachability_t reach;
	memset(&reach, 0, sizeof(reach));
	reach.traveltype = TRAVEL_GRAPPLEHOOK;
	VectorSet(reach.start, 4.0f, 0.0f, 0.0f);
	VectorSet(reach.end, 100.0f, 0.0f, 0.0f);

	bot_moveresult_t result = BotTravel_Grapple(&movestate, &reach);

	assert_true((result.flags & MOVERESULT_MOVEMENTVIEW) != 0);
	assert_true((movestate.moveflags & MFL_GRAPPLEATTACHED) != 0);
	assert_int_equal(g_command_count, 1);
	assert_string_equal(g_command_log[0], "hookon");
	assert_float_equal(movestate.lastgrappledist, 999999.0f, 0.0f);

	unsigned int pattern = 0;
	memcpy(&pattern, &movestate.lastgrappledist, sizeof(pattern));
	assert_int_equal((int)pattern, (int)0x497423f0u);

	/* The hook-on branch never steers, so no movement direction is produced. */
	assert_float_equal(result.movedir[0], 0.0f, 0.0001f);
	assert_float_equal(result.movedir[1], 0.0f, 0.0001f);
	assert_float_equal(result.movedir[2], 0.0f, 0.0001f);
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

	int handle = BotAllocMoveStateHandle();
	assert_true(handle > 0);
	bot_movestate_t *ms = BotMoveStateFromHandle(handle);
	assert_non_null(ms);
	BotResetMoveStateHandle(handle);
	ms->client = 0;
	ms->entitynum = 5;
	ms->areanum = 1;
	VectorClear(ms->origin);

	bot_goal_t goal;
	memset(&goal, 0, sizeof(goal));
	goal.areanum = 2;
	VectorSet(goal.origin, 80.0f, 0.0f, 0.0f);

	bot_moveresult_t result;
	result = BotTravel_Swim(ms, &aasworld.reachability[1]);

	assert_false(result.failure);
	assert_int_equal(result.traveltype, 0);
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

	BotFreeMoveStateHandle(handle);
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

	int handle = BotAllocMoveStateHandle();
	assert_true(handle > 0);
	bot_movestate_t *ms = BotMoveStateFromHandle(handle);
	assert_non_null(ms);
	BotResetMoveStateHandle(handle);
	ms->client = 0;
	ms->entitynum = 5;
	ms->areanum = 1;
	VectorClear(ms->origin);

	bot_goal_t goal;
	memset(&goal, 0, sizeof(goal));
	goal.areanum = 2;
	VectorSet(goal.origin, 24.0f, 0.0f, 0.0f);

	bot_moveresult_t result;
	result = BotTravel_WaterJump(ms, &aasworld.reachability[1]);

	assert_false(result.failure);
	assert_int_equal(result.traveltype, 0);
	assert_true((result.flags & MOVERESULT_MOVEMENTVIEW) != 0);
	assert_true(result.movedir[0] > 0.4f);
	assert_int_equal(g_trace_call_count, 0);

	bot_input_t input;
	assert_int_equal(EA_GetInput(0, 0.1f, &input), BLERR_NOERROR);
	assert_float_equal(input.speed, 0.0f, 0.0001f);
	assert_true((input.actionflags & ACTION_MOVEFORWARD) != 0);
	assert_true((input.actionflags & ACTION_MOVEUP) != 0);

	BotFreeMoveStateHandle(handle);
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

	int handle = BotAllocMoveStateHandle();
	assert_true(handle > 0);
	bot_movestate_t *ms = BotMoveStateFromHandle(handle);
	assert_non_null(ms);
	BotResetMoveStateHandle(handle);
	ms->client = 0;
	ms->entitynum = 5;
	ms->areanum = 1;
	VectorClear(ms->origin);

	bot_goal_t goal;
	memset(&goal, 0, sizeof(goal));
	goal.areanum = 2;
	VectorSet(goal.origin, 128.0f, 0.0f, 32.0f);

	bot_moveresult_t result;
	result = BotTravel_Teleport(ms, &aasworld.reachability[1]);

	assert_false(result.failure);
	assert_int_equal(result.traveltype, 0);
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

	BotFreeMoveStateHandle(handle);
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

Pins retail jump travel's run-up branch before the bot reaches the launch
window.

AAS_JumpReachRunStart is real movement prediction in retail (sub_1000f010
@0x1000f097 hands sub_1000f840 a cmdmove of hordir*400 for one command frame,
two 0.1s frames, stop-event mask 0x7c), so the fixture has to stand the bot on
solid AAS ground; without a floor the predictor immediately reports SE_GAP at
the start position and the run-up direction degenerates.
=============
*/
static void test_bot_travel_jump_runs_to_runstart(void **state)
{
	(void)state;

	aasworld.numAreas = 2;
	aasworld.numAreaSettings = 3;
	aasworld.areasettings = calloc((size_t)aasworld.numAreaSettings, sizeof(aas_areasettings_t));
	assert_non_null(aasworld.areasettings);
	aasworld.areasettings[1].areaflags = TEST_AAS_AREAFLAG_GROUNDED;
	aasworld.areasettings[1].firstreachablearea = 1;
	aasworld.areasettings[1].numreachableareas = 1;
	test_attach_floor_aas_geometry(0.0f);

	aasworld.numReachability = 2;
	aasworld.reachability = calloc((size_t)aasworld.numReachability, sizeof(aas_reachability_t));
	assert_non_null(aasworld.reachability);
	aasworld.reachability[1].areanum = 2;
	aasworld.reachability[1].traveltype = TRAVEL_JUMP;
	aasworld.reachability[1].traveltime = 1;
	VectorSet(aasworld.reachability[1].start, 80.0f, 0.0f, 0.0f);
	VectorSet(aasworld.reachability[1].end, 160.0f, 0.0f, 0.0f);
	aasworld.travelflagfortype[TRAVEL_JUMP] = TFL_JUMP;

	int handle = BotAllocMoveStateHandle();
	assert_true(handle > 0);
	bot_movestate_t *ms = BotMoveStateFromHandle(handle);
	assert_non_null(ms);
	BotResetMoveStateHandle(handle);
	ms->client = 0;
	ms->entitynum = 5;
	ms->areanum = 1;
	ms->reachareanum = 1;
	VectorSet(ms->origin, -40.0f, 0.0f, 0.0f);

	bot_goal_t goal;
	memset(&goal, 0, sizeof(goal));
	goal.areanum = 2;
	VectorSet(goal.origin, 160.0f, 0.0f, 0.0f);

	bot_moveresult_t result;
	ms->lastreachnum = 1;
	result = BotTravel_Jump(ms, &aasworld.reachability[1]);

	assert_false(result.failure);
	assert_int_equal(result.traveltype, 0);
	assert_float_equal(result.movedir[0], 1.0f, 0.0001f);
	assert_float_equal(result.movedir[1], 0.0f, 0.0001f);
	assert_int_equal(g_trace_call_count, 0);
	assert_int_equal(ms->jumpreach, 0);

	bot_input_t input;
	assert_int_equal(EA_GetInput(0, 0.1f, &input), BLERR_NOERROR);
	/*
	 * The predictor carries the run-up point roughly 61 units back along -X, so
	 * BotTravel_Jump re-extrapolates runstart to reach->start - 80 units and the
	 * bot is 40 units short of it: 400 - (400 - 40 * 5) = 200.
	 */
	assert_float_equal(input.speed, 200.0f, 0.0001f);
	assert_float_equal(input.dir[0], 1.0f, 0.0001f);
	assert_true((input.actionflags & ACTION_JUMP) == 0);
	assert_true((input.actionflags & ACTION_DELAYEDJUMP) == 0);

	BotFreeMoveStateHandle(handle);
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
test_bot_travel_jump_runstart_hazard_falls_back_to_start

Pins the AAS run-start hazard fallback before active jump travel shortens gaps.

Retail AAS_JumpReachRunStart (sub_1000f010) predicts the run-up with stop-event
mask 0x7c and then tests the returned stop event against 0x38 at 0x1000f0d0, so
only SE_ENTERSLIME, SE_ENTERLAVA and SE_HITGROUNDDAMAGE collapse the run-up
point back onto reach->start (+1 unit of Z, 0x1000f0da-0x1000f0e3). There is no
fixed 40/80 unit hazard sampling: the predictor's own feet probe, 22 units under
the frame-0 position, is what sees the slime here.
=============
*/
static void test_bot_travel_jump_runstart_hazard_falls_back_to_start(void **state)
{
	(void)state;

	aasworld.numAreas = 2;
	aasworld.numAreaSettings = 3;
	aasworld.areasettings = calloc((size_t)aasworld.numAreaSettings, sizeof(aas_areasettings_t));
	assert_non_null(aasworld.areasettings);
	aasworld.areasettings[1].areaflags = TEST_AAS_AREAFLAG_GROUNDED;
	aasworld.areasettings[1].firstreachablearea = 1;
	aasworld.areasettings[1].numreachableareas = 1;
	test_attach_floor_aas_geometry(0.0f);

	aasworld.numReachability = 2;
	aasworld.reachability = calloc((size_t)aasworld.numReachability, sizeof(aas_reachability_t));
	assert_non_null(aasworld.reachability);
	aasworld.reachability[1].areanum = 2;
	aasworld.reachability[1].traveltype = TRAVEL_JUMP;
	aasworld.reachability[1].traveltime = 1;
	VectorSet(aasworld.reachability[1].start, 80.0f, 0.0f, 0.0f);
	VectorSet(aasworld.reachability[1].end, 160.0f, 0.0f, 0.0f);
	aasworld.travelflagfortype[TRAVEL_JUMP] = TFL_JUMP;

	/*
	 * Probe 0 is the predictor's frame-0 AAS_Swimming sample two units under
	 * the start; probe 1 is the frame-0 feet sample after the client has been
	 * carried back along -X and clipped onto the floor.
	 */
	g_point_contents_result_count = 2;
	g_point_contents_results[0] = 0;
	g_point_contents_results[1] = CONTENTS_SLIME;

	int handle = BotAllocMoveStateHandle();
	assert_true(handle > 0);
	bot_movestate_t *ms = BotMoveStateFromHandle(handle);
	assert_non_null(ms);
	BotResetMoveStateHandle(handle);
	ms->client = 0;
	ms->entitynum = 5;
	ms->areanum = 1;
	VectorSet(ms->origin, -40.0f, 0.0f, 0.0f);

	bot_goal_t goal;
	memset(&goal, 0, sizeof(goal));
	goal.areanum = 2;
	VectorSet(goal.origin, 160.0f, 0.0f, 0.0f);

	bot_moveresult_t result;
	ms->lastreachnum = 1;
	result = BotTravel_Jump(ms, &aasworld.reachability[1]);

	assert_false(result.failure);
	assert_int_equal(result.traveltype, 0);
	assert_float_equal(result.movedir[0], 1.0f, 0.0001f);
	assert_float_equal(result.movedir[1], 0.0f, 0.0001f);
	assert_int_equal(g_trace_call_count, 0);
	/* The slime stop event ends the prediction inside frame 0, so exactly the
	   swim probe and the feet probe reach the engine. */
	assert_int_equal(g_point_contents_call_count, 2);
	assert_float_equal(g_point_contents_log[0][0], 80.0f, 0.0001f);
	assert_float_equal(g_point_contents_log[0][2], -0.75f, 0.0001f);
	/* Feet probe: carried back along -X, then 22 units below the clipped
	   position that sits a traceplane epsilon above the floor. */
	assert_true(g_point_contents_log[1][0] < 80.0f);
	assert_float_equal(g_point_contents_log[1][2], -21.842789f, 0.0001f);
	assert_int_equal(ms->jumpreach, 0);

	bot_input_t input;
	assert_int_equal(EA_GetInput(0, 0.1f, &input), BLERR_NOERROR);
	/*
	 * runstart collapsed onto reach->start, so BotTravel_Jump's hordir is zero,
	 * runstart stays at reach->start and the bot is the full 80 unit clamp away:
	 * 400 - (400 - 80 * 5) = 400.
	 */
	assert_float_equal(input.speed, 400.0f, 0.0001f);
	assert_float_equal(input.dir[0], 1.0f, 0.0001f);
	assert_true((input.actionflags & ACTION_JUMP) == 0);
	assert_true((input.actionflags & ACTION_DELAYEDJUMP) == 0);

	BotFreeMoveStateHandle(handle);
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
test_bot_travel_jump_launches_near_start

Pins retail jump travel's close launch branch and immediate jump action.
=============
*/
static void test_bot_travel_jump_launches_near_start(void **state)
{
	(void)state;

	aasworld.numAreas = 2;
	aasworld.numAreaSettings = 3;
	aasworld.areasettings = calloc((size_t)aasworld.numAreaSettings, sizeof(aas_areasettings_t));
	assert_non_null(aasworld.areasettings);
	aasworld.areasettings[1].areaflags = TEST_AAS_AREAFLAG_GROUNDED;
	aasworld.areasettings[1].firstreachablearea = 1;
	aasworld.areasettings[1].numreachableareas = 1;
	/*
	 * Retail's run-up point comes from real movement prediction
	 * (sub_1000f010 @0x1000f097), so the bot needs solid AAS ground to run
	 * back over; without a floor the predictor stops on SE_GAP at the start.
	 */
	test_attach_floor_aas_geometry(0.0f);

	aasworld.numReachability = 2;
	aasworld.reachability = calloc((size_t)aasworld.numReachability, sizeof(aas_reachability_t));
	assert_non_null(aasworld.reachability);
	aasworld.reachability[1].areanum = 2;
	aasworld.reachability[1].traveltype = TRAVEL_JUMP;
	aasworld.reachability[1].traveltime = 1;
	VectorSet(aasworld.reachability[1].start, 80.0f, 0.0f, 0.0f);
	VectorSet(aasworld.reachability[1].end, 160.0f, 0.0f, 0.0f);
	aasworld.travelflagfortype[TRAVEL_JUMP] = TFL_JUMP;

	int handle = BotAllocMoveStateHandle();
	assert_true(handle > 0);
	bot_movestate_t *ms = BotMoveStateFromHandle(handle);
	assert_non_null(ms);
	BotResetMoveStateHandle(handle);
	ms->client = 0;
	ms->entitynum = 5;
	ms->areanum = 1;
	ms->reachareanum = 1;
	VectorSet(ms->origin, 70.0f, 0.0f, 0.0f);

	bot_goal_t goal;
	memset(&goal, 0, sizeof(goal));
	goal.areanum = 2;
	VectorSet(goal.origin, 160.0f, 0.0f, 0.0f);

	bot_moveresult_t result;
	ms->lastreachnum = 1;
	result = BotTravel_Jump(ms, &aasworld.reachability[1]);

	assert_false(result.failure);
	assert_int_equal(result.traveltype, 0);
	assert_float_equal(result.movedir[0], 1.0f, 0.0001f);
	assert_int_equal(g_trace_call_count, 0);
	/*
	 * The predicted run-up point lies behind reach->start, so the bot at 70 is
	 * already past it: the start and run-start directions oppose, the dot
	 * product drops under -0.8 and retail takes the launch branch.
	 */
	assert_int_equal(ms->jumpreach, 1);

	bot_input_t input;
	assert_int_equal(EA_GetInput(0, 0.1f, &input), BLERR_NOERROR);
	assert_float_equal(input.speed, 565.0f, 0.0001f);
	assert_float_equal(input.dir[0], 1.0f, 0.0001f);
	assert_true((input.actionflags & ACTION_JUMP) != 0);
	assert_true((input.actionflags & ACTION_DELAYEDJUMP) == 0);

	BotFreeMoveStateHandle(handle);
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

Pins retail jump travel's delayed-jump action window before the immediate launch
threshold.
=============
*/
static void test_bot_travel_jump_uses_delayed_jump_window(void **state)
{
	(void)state;

	aasworld.numAreas = 2;
	aasworld.numAreaSettings = 3;
	aasworld.areasettings = calloc((size_t)aasworld.numAreaSettings, sizeof(aas_areasettings_t));
	assert_non_null(aasworld.areasettings);
	aasworld.areasettings[1].areaflags = TEST_AAS_AREAFLAG_GROUNDED;
	aasworld.areasettings[1].firstreachablearea = 1;
	aasworld.areasettings[1].numreachableareas = 1;
	/*
	 * Retail's run-up point comes from real movement prediction
	 * (sub_1000f010 @0x1000f097), so the bot needs solid AAS ground to run
	 * back over; without a floor the predictor stops on SE_GAP at the start.
	 */
	test_attach_floor_aas_geometry(0.0f);

	aasworld.numReachability = 2;
	aasworld.reachability = calloc((size_t)aasworld.numReachability, sizeof(aas_reachability_t));
	assert_non_null(aasworld.reachability);
	aasworld.reachability[1].areanum = 2;
	aasworld.reachability[1].traveltype = TRAVEL_JUMP;
	aasworld.reachability[1].traveltime = 1;
	VectorSet(aasworld.reachability[1].start, 80.0f, 0.0f, 0.0f);
	VectorSet(aasworld.reachability[1].end, 160.0f, 0.0f, 0.0f);
	aasworld.travelflagfortype[TRAVEL_JUMP] = TFL_JUMP;

	int handle = BotAllocMoveStateHandle();
	assert_true(handle > 0);
	bot_movestate_t *ms = BotMoveStateFromHandle(handle);
	assert_non_null(ms);
	BotResetMoveStateHandle(handle);
	ms->client = 0;
	ms->entitynum = 5;
	ms->areanum = 1;
	ms->reachareanum = 1;
	VectorSet(ms->origin, 52.0f, 0.0f, 0.0f);

	bot_goal_t goal;
	memset(&goal, 0, sizeof(goal));
	goal.areanum = 2;
	VectorSet(goal.origin, 160.0f, 0.0f, 0.0f);

	bot_moveresult_t result;
	ms->lastreachnum = 1;
	result = BotTravel_Jump(ms, &aasworld.reachability[1]);

	assert_false(result.failure);
	assert_int_equal(result.traveltype, 0);
	assert_float_equal(result.movedir[0], 1.0f, 0.0001f);
	/*
	 * At 52 the bot is 28 units from reach->start, past the predicted run-up
	 * point: retail takes the launch branch but 28 falls in [24, 32), so the
	 * jump is queued rather than pressed.
	 */
	assert_int_equal(ms->jumpreach, 1);

	bot_input_t input;
	assert_int_equal(EA_GetInput(0, 0.1f, &input), BLERR_NOERROR);
	assert_float_equal(input.speed, 565.0f, 0.0001f);
	assert_true((input.actionflags & ACTION_JUMP) == 0);
	assert_true((input.actionflags & ACTION_DELAYEDJUMP) != 0);

	BotFreeMoveStateHandle(handle);
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
test_bot_travel_rocketjump_approaches_start_with_weapon_view

Pins Gladiator's active rocket-jump approach branch.
=============
*/
static void test_bot_travel_rocketjump_approaches_start_with_weapon_view(void **state)
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
	aasworld.reachability[1].traveltype = TRAVEL_ROCKETJUMP;
	aasworld.reachability[1].traveltime = 1;
	VectorSet(aasworld.reachability[1].start, 40.0f, 0.0f, 16.0f);
	VectorSet(aasworld.reachability[1].end, 128.0f, 0.0f, 64.0f);
	aasworld.travelflagfortype[TRAVEL_ROCKETJUMP] = TFL_ROCKETJUMP;

	int handle = BotAllocMoveStateHandle();
	assert_true(handle > 0);
	bot_movestate_t *ms = BotMoveStateFromHandle(handle);
	assert_non_null(ms);
	BotResetMoveStateHandle(handle);
	ms->client = 0;
	ms->entitynum = 5;
	ms->areanum = 1;
	VectorClear(ms->origin);

	bot_goal_t goal;
	memset(&goal, 0, sizeof(goal));
	goal.areanum = 2;
	VectorSet(goal.origin, 128.0f, 0.0f, 64.0f);

	bot_moveresult_t result;
	result = BotTravel_RocketJump(ms, &aasworld.reachability[1]);

	assert_false(result.failure);
	assert_int_equal(result.traveltype, 0);
	assert_true((result.flags & MOVERESULT_MOVEMENTVIEWSET) != 0);
	assert_float_equal(result.movedir[0], 1.0f, 0.0001f);
	assert_float_equal(result.movedir[1], 0.0f, 0.0001f);
	assert_float_equal(result.movedir[2], 0.0f, 0.0001f);
	assert_int_equal(g_trace_call_count, 0);
	assert_int_equal(ms->jumpreach, 0);
	assert_float_equal(ms->reachability_time, 0.0f, 0.0001f);

	bot_input_t input;
	assert_int_equal(EA_GetInput(0, 0.1f, &input), BLERR_NOERROR);
	assert_float_equal(input.speed, 200.0f, 0.0001f);
	assert_float_equal(input.dir[0], 1.0f, 0.0001f);
	assert_true((input.actionflags & ACTION_JUMP) == 0);
	assert_true((input.actionflags & ACTION_ATTACK) == 0);
	assert_int_equal(input.weapon, 0);
	assert_float_equal(input.viewangles[PITCH], 90.0f, 0.0001f);
	assert_float_equal(input.viewangles[YAW], 0.0f, 0.0001f);
	assert_int_equal(g_command_count, 1);
	assert_string_equal(g_command_log[0], "use");

	BotFreeMoveStateHandle(handle);
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
test_bot_travel_rocketjump_launches_at_start

Pins Gladiator's close-range rocket-jump fire branch.
=============
*/
static void test_bot_travel_rocketjump_launches_at_start(void **state)
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
	aasworld.reachability[1].traveltype = TRAVEL_ROCKETJUMP;
	aasworld.reachability[1].traveltime = 1;
	VectorSet(aasworld.reachability[1].start, 2.0f, 0.0f, 16.0f);
	VectorSet(aasworld.reachability[1].end, 128.0f, 0.0f, 64.0f);
	aasworld.travelflagfortype[TRAVEL_ROCKETJUMP] = TFL_ROCKETJUMP;

	int handle = BotAllocMoveStateHandle();
	assert_true(handle > 0);
	bot_movestate_t *ms = BotMoveStateFromHandle(handle);
	assert_non_null(ms);
	BotResetMoveStateHandle(handle);
	ms->client = 0;
	ms->entitynum = 5;
	ms->areanum = 1;
	ms->lastreachnum = 1;
	VectorClear(ms->origin);

	bot_goal_t goal;
	memset(&goal, 0, sizeof(goal));
	goal.areanum = 2;
	VectorSet(goal.origin, 128.0f, 0.0f, 64.0f);

	bot_moveresult_t result;
	result = BotTravel_RocketJump(ms, &aasworld.reachability[1]);

	assert_false(result.failure);
	assert_int_equal(result.traveltype, 0);
	assert_true((result.flags & MOVERESULT_MOVEMENTVIEWSET) != 0);
	assert_float_equal(result.movedir[0], 1.0f, 0.0001f);
	assert_int_equal(ms->jumpreach, 1);

	bot_input_t input;
	assert_int_equal(EA_GetInput(0, 0.1f, &input), BLERR_NOERROR);
	assert_float_equal(input.speed, 565.0f, 0.0001f);
	assert_float_equal(input.dir[0], 1.0f, 0.0001f);
	assert_float_equal(input.dir[2], 0.0f, 0.0001f);
	assert_true((input.actionflags & ACTION_JUMP) != 0);
	assert_true((input.actionflags & ACTION_ATTACK) != 0);
	assert_int_equal(input.weapon, 0);
	assert_float_equal(input.viewangles[PITCH], 90.0f, 0.0001f);
	assert_int_equal(g_command_count, 1);
	assert_string_equal(g_command_log[0], "use");

	BotFreeMoveStateHandle(handle);
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
test_bot_travel_jumppad_preserves_retail_unsupported_result

Pins retail's unsupported active jumppad dispatch and eight-second timeout.
=============
*/
static void test_bot_travel_jumppad_preserves_retail_unsupported_result(void **state)
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
	aasworld.reachability[1].traveltype = TRAVEL_JUMPPAD;
	aasworld.reachability[1].traveltime = 1;
	VectorSet(aasworld.reachability[1].start, 30.0f, 40.0f, 24.0f);
	VectorSet(aasworld.reachability[1].end, 128.0f, 0.0f, 64.0f);
	aasworld.travelflagfortype[TRAVEL_JUMPPAD] = TFL_JUMPPAD;
	test_attach_floor_aas_geometry(-1.5f);

	g_trace_result_count = 1;
	memset(&g_trace_results[0], 0, sizeof(g_trace_results[0]));
	g_trace_results[0].fraction = 1.0f;

	int handle = BotAllocMoveStateHandle();
	assert_true(handle > 0);
	bot_movestate_t *ms = BotMoveStateFromHandle(handle);
	assert_non_null(ms);
	BotResetMoveStateHandle(handle);
	ms->client = 0;
	ms->entitynum = 5;
	ms->areanum = 1;
	ms->presencetype = PRESENCE_NORMAL;
	ms->moveflags = MFL_ONGROUND;
	VectorClear(ms->origin);

	bot_goal_t goal;
	memset(&goal, 0, sizeof(goal));
	goal.areanum = 2;
	VectorSet(goal.origin, 128.0f, 0.0f, 64.0f);

	bot_moveresult_t result;
	BotMoveToGoalHandle(&result, handle, &goal, TFL_DEFAULT);

	assert_false(result.failure);
	assert_int_equal(result.traveltype, TRAVEL_JUMPPAD);
	assert_int_equal(result.type, 0);
	assert_int_equal(result.flags, 0);
	assert_int_equal(g_trace_call_count, 0);
	float timeoutDelta = ms->reachability_time - aasworld.time;
	assert_true(timeoutDelta > 7.9f && timeoutDelta < 8.1f);

	bot_input_t input;
	assert_int_equal(EA_GetInput(0, 0.1f, &input), BLERR_NOERROR);
	assert_float_equal(input.speed, 0.0f, 0.0001f);
	assert_int_equal(input.actionflags, 0);

	BotFreeMoveStateHandle(handle);
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
test_bot_travel_bfgjump_uses_retail_unsupported_path

Pins Gladiator's active type-13 diagnostic branch.
=============
*/
static void test_bot_travel_bfgjump_uses_retail_unsupported_path(void **state)
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
	aasworld.reachability[1].traveltype = TRAVEL_BFGJUMP;
	aasworld.reachability[1].traveltime = 1;
	VectorSet(aasworld.reachability[1].start, 2.0f, 0.0f, 16.0f);
	VectorSet(aasworld.reachability[1].end, 128.0f, 0.0f, 64.0f);
	aasworld.travelflagfortype[TRAVEL_BFGJUMP] = TFL_BFGJUMP;
	test_attach_floor_aas_geometry(-1.5f);

	int handle = BotAllocMoveStateHandle();
	assert_true(handle > 0);
	bot_movestate_t *ms = BotMoveStateFromHandle(handle);
	assert_non_null(ms);
	BotResetMoveStateHandle(handle);
	ms->client = 0;
	ms->entitynum = 5;
	ms->areanum = 1;
	ms->presencetype = PRESENCE_NORMAL;
	ms->moveflags = MFL_ONGROUND;
	VectorClear(ms->origin);

	bot_goal_t goal;
	memset(&goal, 0, sizeof(goal));
	goal.areanum = 2;
	VectorSet(goal.origin, 128.0f, 0.0f, 64.0f);

	bot_moveresult_t result;
	BotMoveToGoalHandle(&result, handle, &goal, TFL_DEFAULT | TFL_BFGJUMP);

	assert_false(result.failure);
	assert_int_equal(result.traveltype, TRAVEL_BFGJUMP);
	assert_int_equal(ms->jumpreach, 0);
	float timeoutDelta = ms->reachability_time - aasworld.time;
	assert_true(timeoutDelta > 7.9f && timeoutDelta < 8.1f);

	bot_input_t input;
	assert_int_equal(EA_GetInput(0, 0.1f, &input), BLERR_NOERROR);
	assert_float_equal(input.speed, 0.0f, 0.0001f);
	assert_int_equal(input.actionflags, 0);
	assert_int_equal(input.weapon, 0);

	BotFreeMoveStateHandle(handle);
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
test_bot_finish_bfgjump_uses_retail_unsupported_path

Pins Gladiator's airborne type-13 diagnostic branch.
=============
*/
static void test_bot_finish_bfgjump_uses_retail_unsupported_path(void **state)
{
	(void)state;

	aasworld.time = 3.0f;
	aasworld.initialized = qtrue;
	aasworld.numReachability = 2;
	aasworld.reachability = calloc((size_t)aasworld.numReachability, sizeof(aas_reachability_t));
	assert_non_null(aasworld.reachability);
	aasworld.reachability[1].areanum = 2;
	aasworld.reachability[1].traveltype = TRAVEL_BFGJUMP;
	VectorSet(aasworld.reachability[1].start, 64.0f, 0.0f, 0.0f);
	VectorSet(aasworld.reachability[1].end, 128.0f, 0.0f, 0.0f);

	int handle = BotAllocMoveStateHandle();
	assert_true(handle > 0);
	bot_movestate_t *ms = BotMoveStateFromHandle(handle);
	assert_non_null(ms);
	BotResetMoveStateHandle(handle);
	ms->client = 0;
	ms->entitynum = 0;
	ms->areanum = 1;
	ms->lastreachnum = 1;
	ms->lastgoalareanum = 2;
	ms->lastareanum = 1;
	ms->reachability_time = aasworld.time + 1.0f;
	VectorSet(ms->origin, 80.0f, 0.0f, 32.0f);

	bot_goal_t goal;
	memset(&goal, 0, sizeof(goal));
	goal.areanum = 2;
	VectorSet(goal.origin, 128.0f, 0.0f, 0.0f);

	bot_moveresult_t result;
	BotMoveToGoalHandle(&result, handle, &goal, TFL_DEFAULT | TFL_BFGJUMP);

	assert_false(result.failure);
	assert_int_equal(result.traveltype, TRAVEL_BFGJUMP);

	bot_input_t input;
	assert_int_equal(EA_GetInput(0, 0.1f, &input), BLERR_NOERROR);
	assert_float_equal(input.speed, 0.0f, 0.0001f);
	assert_int_equal(input.actionflags, 0);

	BotFreeMoveStateHandle(handle);
	free(aasworld.reachability);
	aasworld.reachability = NULL;
	aasworld.numReachability = 0;
}

/*
=============
test_bot_finish_rocketjump_drives_endpoint_at_retail_speed

Pins Gladiator's direct 800-speed airborne rocket-jump finish.
=============
*/
static void test_bot_finish_rocketjump_drives_endpoint_at_retail_speed(void **state)
{
	(void)state;

	aasworld.time = 3.0f;
	aasworld.numReachability = 2;
	aasworld.reachability = calloc((size_t)aasworld.numReachability, sizeof(aas_reachability_t));
	assert_non_null(aasworld.reachability);
	aasworld.reachability[1].areanum = 2;
	aasworld.reachability[1].traveltype = TRAVEL_ROCKETJUMP;
	VectorSet(aasworld.reachability[1].start, 64.0f, 0.0f, 0.0f);
	VectorSet(aasworld.reachability[1].end, 128.0f, 0.0f, 64.0f);

	int handle = BotAllocMoveStateHandle();
	assert_true(handle > 0);
	bot_movestate_t *ms = BotMoveStateFromHandle(handle);
	assert_non_null(ms);
	BotResetMoveStateHandle(handle);
	ms->client = 0;
	ms->entitynum = 0;
	ms->areanum = 1;
	ms->lastreachnum = 1;
	ms->lastgoalareanum = 2;
	ms->lastareanum = 1;
	ms->reachability_time = aasworld.time + 1.0f;
	ms->jumpreach = 1;
	VectorSet(ms->origin, 80.0f, 16.0f, 32.0f);
	VectorSet(ms->velocity, 48.0f, 160.0f, 240.0f);

	bot_goal_t goal;
	memset(&goal, 0, sizeof(goal));
	goal.areanum = 2;
	VectorSet(goal.origin, 128.0f, 0.0f, 64.0f);

	bot_moveresult_t result;
	result = BotFinishTravel_WeaponJump(ms, &aasworld.reachability[1]);

	assert_false(result.failure);
	assert_int_equal(result.traveltype, 0);
	assert_float_equal(result.movedir[0], 0.9486833f, 0.0001f);
	assert_float_equal(result.movedir[1], -0.3162278f, 0.0001f);
	assert_float_equal(result.movedir[2], 0.0f, 0.0001f);

	bot_input_t input;
	assert_int_equal(EA_GetInput(0, 0.1f, &input), BLERR_NOERROR);
	assert_float_equal(input.speed, 565.0f, 0.0001f);
	assert_float_equal(input.dir[0], 0.9486833f, 0.0001f);
	assert_float_equal(input.dir[1], -0.3162278f, 0.0001f);
	assert_float_equal(input.dir[2], 0.0f, 0.0001f);
	assert_true((input.actionflags & ACTION_JUMP) == 0);
	assert_true((input.actionflags & ACTION_ATTACK) == 0);

	BotFreeMoveStateHandle(handle);
	free(aasworld.reachability);
	aasworld.reachability = NULL;
	aasworld.numReachability = 0;
}

/*
=============
test_bot_finish_rocketjump_waits_for_launch_state

Pins Gladiator's `jumpreach` guard before applying rocket-jump finish movement.
=============
*/
static void test_bot_finish_rocketjump_waits_for_launch_state(void **state)
{
	(void)state;

	aasworld.time = 3.0f;
	aasworld.numReachability = 2;
	aasworld.reachability = calloc((size_t)aasworld.numReachability, sizeof(aas_reachability_t));
	assert_non_null(aasworld.reachability);
	aasworld.reachability[1].areanum = 2;
	aasworld.reachability[1].traveltype = TRAVEL_ROCKETJUMP;
	VectorSet(aasworld.reachability[1].start, 64.0f, 0.0f, 0.0f);
	VectorSet(aasworld.reachability[1].end, 128.0f, 0.0f, 64.0f);

	int handle = BotAllocMoveStateHandle();
	assert_true(handle > 0);
	bot_movestate_t *ms = BotMoveStateFromHandle(handle);
	assert_non_null(ms);
	BotResetMoveStateHandle(handle);
	ms->client = 0;
	ms->entitynum = 0;
	ms->areanum = 1;
	ms->lastreachnum = 1;
	ms->lastgoalareanum = 2;
	ms->lastareanum = 1;
	ms->reachability_time = aasworld.time + 1.0f;
	ms->jumpreach = 0;
	VectorSet(ms->origin, 80.0f, 16.0f, 32.0f);
	VectorSet(ms->velocity, 48.0f, 160.0f, 240.0f);

	bot_goal_t goal;
	memset(&goal, 0, sizeof(goal));
	goal.areanum = 2;
	VectorSet(goal.origin, 128.0f, 0.0f, 64.0f);

	bot_moveresult_t result;
	result = BotFinishTravel_WeaponJump(ms, &aasworld.reachability[1]);

	assert_false(result.failure);
	assert_int_equal(result.traveltype, 0);
	assert_float_equal(result.movedir[0], 0.0f, 0.0001f);
	assert_float_equal(result.movedir[1], 0.0f, 0.0001f);
	assert_float_equal(result.movedir[2], 0.0f, 0.0001f);

	bot_input_t input;
	assert_int_equal(EA_GetInput(0, 0.1f, &input), BLERR_NOERROR);
	assert_float_equal(input.speed, 0.0f, 0.0001f);
	assert_int_equal(input.actionflags, 0);

	BotFreeMoveStateHandle(handle);
	free(aasworld.reachability);
	aasworld.reachability = NULL;
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

	int handle = BotAllocMoveStateHandle();
	assert_true(handle > 0);

	bot_movestate_t *ms = BotMoveStateFromHandle(handle);
	assert_non_null(ms);
	BotResetMoveStateHandle(handle);
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
	result = BotFinishTravel_Jump(ms, &aasworld.reachability[1]);

	assert_false(result.failure);
	assert_int_equal(result.traveltype, 0);
	assert_float_equal(result.movedir[0], 1.0f, 0.0001f);
	assert_float_equal(result.movedir[1], 0.0f, 0.0001f);
	assert_float_equal(result.movedir[2], 0.0f, 0.0001f);

	bot_input_t input;
	assert_int_equal(EA_GetInput(0, 0.1f, &input), BLERR_NOERROR);
	assert_float_equal(input.speed, 565.0f, 0.0001f);
	assert_true((input.actionflags & ACTION_JUMP) == 0);
	assert_int_equal(ms->lastreachnum, 1);

	BotFreeMoveStateHandle(handle);
	free(aasworld.reachability);
	aasworld.reachability = NULL;
	aasworld.numReachability = 0;
}

/*
=============
test_bot_reachability_area_skips_ground_pass_when_disabled

Pins retail's first-pass fallback when the optional ground search is disabled.
=============
*/
static void test_bot_reachability_area_skips_ground_pass_when_disabled(void **state)
{
	(void)state;
	test_setup_stacked_aas_trace_world(32.0f);

	vec3_t origin;
	VectorSet(origin, 0.0f, 0.0f, 64.0f);

	assert_int_equal(AAS_PointAreaNum(origin), 2);
	assert_int_equal(AAS_AreaReachability(2), 0);
	assert_int_equal(BotReachabilityArea(origin, 0), 2);
	assert_int_equal(g_trace_call_count, 0);
}

/*
=============
test_bot_reachability_area_ground_pass_finds_lower_reachable_area

Pins retail's crouch-presence ground pass from an unreachable upper area to a
reachable lower area.
=============
*/
static void test_bot_reachability_area_ground_pass_finds_lower_reachable_area(void **state)
{
	(void)state;
	test_setup_stacked_aas_trace_world(32.0f);

	vec3_t origin;
	VectorSet(origin, 0.0f, 0.0f, 64.0f);

	assert_int_equal(AAS_PointAreaNum(origin), 2);
	assert_int_equal(AAS_AreaReachability(1), 1);
	assert_int_equal(BotReachabilityArea(origin, 1), 1);
	assert_int_equal(g_trace_call_count, 0);
}

/*
=============
test_bot_movement_view_target_uses_retail_route_context

Pins retail route preview from the previous reach end to the next reach end,
including the original fifteen-unit vertical target offset.
=============
*/
static void test_bot_movement_view_target_uses_retail_route_context(void **state)
{
	(void)state;

	aasworld.time = 4.0f;
	aasworld.initialized = qtrue;
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

	int handle = BotAllocMoveStateHandle();
	assert_true(handle > 0);

	bot_movestate_t *ms = BotMoveStateFromHandle(handle);
	assert_non_null(ms);
	BotResetMoveStateHandle(handle);
	ms->areanum = 1;
	ms->lastareanum = 1;
	ms->lastgoalareanum = 3;
	ms->lastreachnum = 1;
	VectorSet(ms->origin, 0.0f, 0.0f, 0.0f);
	bot_goal_t goal;
	memset(&goal, 0, sizeof(goal));
	goal.areanum = 3;
	VectorSet(goal.origin, 128.0f, 0.0f, 0.0f);

	vec3_t target;
	VectorClear(target);
	assert_true(BotMovementViewTargetHandle(handle, &goal, TFL_DEFAULT, 96.0f, target));
	assert_float_equal(target[0], 128.0f, 0.0001f);
	assert_float_equal(target[1], 0.0f, 0.0001f);
	assert_float_equal(target[2], -15.0f, 0.0001f);

	BotFreeMoveStateHandle(handle);
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
		cmocka_unit_test_setup_teardown(test_bot_move_retail_struct_layout,
			test_setup,
			test_teardown),
		cmocka_unit_test_setup_teardown(test_bot_clear_move_result_preserves_vector_suffix,
			test_setup,
			test_teardown),
		cmocka_unit_test_setup_teardown(test_bot_setup_move_ai_registers_retail_contract,
			test_setup,
			test_teardown),
		cmocka_unit_test_setup_teardown(test_bot_shutdown_invalidates_handle_registry,
			test_setup,
		                                test_teardown),
        cmocka_unit_test_setup_teardown(test_bot_move_handles_elevator_landing,
                                        test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_bot_travel_elevator_waits_when_platform_is_up,
                                        test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_bot_travel_elevator_moves_to_bottom_center_when_down,
                                        test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_bot_finish_elevator_prefers_closer_vertical_endpoint,
                                        test_setup,
                                        test_teardown),
		cmocka_unit_test_setup_teardown(test_bot_travel_funcbob_preserves_retail_unsupported_result,
                                        test_setup,
                                        test_teardown),
		cmocka_unit_test_setup_teardown(test_aas_clientmove_retail_layout,
			test_setup,
			test_teardown),
		cmocka_unit_test_setup_teardown(test_aas_swimming_ignores_aas_area_contents,
			test_setup,
			test_teardown),
		cmocka_unit_test_setup_teardown(test_aas_on_ground_uses_retail_probe_distance,
			test_setup,
			test_teardown),
        cmocka_unit_test_setup_teardown(test_aas_predict_client_movement_uses_waterfriction_on_ground,
                                        test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_aas_predict_client_movement_uses_friction_while_swimming,
                                        test_setup,
                                        test_teardown),
		cmocka_unit_test_setup_teardown(test_aas_predict_client_movement_skips_zero_maxframes,
			test_setup,
			test_teardown),
		cmocka_unit_test_setup_teardown(test_aas_predict_client_movement_skips_negative_maxframes,
			test_setup,
			test_teardown),
		cmocka_unit_test_setup_teardown(test_aas_predict_client_movement_uses_signed_cmdframes_gate,
			test_setup,
			test_teardown),
		cmocka_unit_test_setup_teardown(test_aas_predict_client_movement_uses_zero_frametime_directly,
			test_setup,
			test_teardown),
		cmocka_unit_test_setup_teardown(test_aas_predict_client_movement_uses_negative_frametime_directly,
			test_setup,
			test_teardown),
		cmocka_unit_test_setup_teardown(test_aas_predict_client_movement_uses_planenum_plane_table,
			test_setup,
			test_teardown),
		cmocka_unit_test_setup_teardown(test_aas_predict_client_movement_requires_exact_vertical_plane,
			test_setup,
			test_teardown),
		cmocka_unit_test_setup_teardown(test_aas_predict_client_movement_defaults_liquid_trace,
			test_setup,
			test_teardown),
		cmocka_unit_test_setup_teardown(test_aas_predict_client_movement_maps_water_to_slime_event,
			test_setup,
			test_teardown),
		cmocka_unit_test_setup_teardown(test_aas_predict_client_movement_ignores_area_liquid_event,
			test_setup,
			test_teardown),
		cmocka_unit_test_setup_teardown(test_aas_predict_client_movement_skips_feet_probe_while_rising,
			test_setup,
			test_teardown),
		cmocka_unit_test_setup_teardown(test_aas_predict_client_movement_uses_aas_trace_only,
			test_setup,
			test_teardown),
		cmocka_unit_test_setup_teardown(test_aas_predict_client_movement_forwards_pass_entity,
			test_setup,
			test_teardown),
		cmocka_unit_test_setup_teardown(test_aas_predict_client_movement_zeroes_result_at_clip_limit,
			test_setup,
			test_teardown),
		cmocka_unit_test_setup_teardown(test_aas_predict_client_movement_clamps_retail_ground_axes,
			test_setup,
			test_teardown),
		cmocka_unit_test_setup_teardown(test_aas_predict_client_movement_ignores_air_command_acceleration,
			test_setup,
			test_teardown),
		cmocka_unit_test_setup_teardown(test_aas_predict_client_movement_clamps_three_swim_axes,
			test_setup,
			test_teardown),
		cmocka_unit_test_setup_teardown(test_aas_predict_client_movement_visualizes_trace_and_startsolid,
			test_setup,
			test_teardown),
		cmocka_unit_test_setup_teardown(test_aas_predict_client_movement_visualizes_retail_step,
			test_setup,
			test_teardown),
		cmocka_unit_test_setup_teardown(test_aas_predict_client_movement_uses_retail_ground_damage_cutoff,
			test_setup,
			test_teardown),
		cmocka_unit_test_setup_teardown(test_aas_predict_client_movement_ignores_q3_enter_area_stop,
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
        cmocka_unit_test_setup_teardown(test_bot_move_in_direction_prediction_rejects_lava,
                                        test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_bot_move_in_direction_prediction_rejects_short_progress,
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
		cmocka_unit_test_setup_teardown(test_bot_travel_grapple_uses_raw_two_degree_gate,
			test_setup,
			test_teardown),
		cmocka_unit_test_setup_teardown(test_bot_travel_grapple_attaches_with_retail_distance_sentinel,
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
        cmocka_unit_test_setup_teardown(test_bot_travel_jump_runstart_hazard_falls_back_to_start,
                                        test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_bot_travel_jump_launches_near_start,
                                        test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_bot_travel_jump_uses_delayed_jump_window,
                                        test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_bot_travel_rocketjump_approaches_start_with_weapon_view,
                                        test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_bot_travel_rocketjump_launches_at_start,
                                        test_setup,
                                        test_teardown),
		cmocka_unit_test_setup_teardown(test_bot_travel_jumppad_preserves_retail_unsupported_result,
                                        test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_bot_travel_bfgjump_uses_retail_unsupported_path,
                                        test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_bot_finish_bfgjump_uses_retail_unsupported_path,
                                        test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_bot_finish_rocketjump_drives_endpoint_at_retail_speed,
                                        test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_bot_finish_rocketjump_waits_for_launch_state,
                                        test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_bot_move_airborne_finishes_last_jump_reachability,
                                        test_setup,
                                        test_teardown),
		cmocka_unit_test_setup_teardown(test_bot_reachability_area_skips_ground_pass_when_disabled,
                                        test_setup,
                                        test_teardown),
		cmocka_unit_test_setup_teardown(test_bot_reachability_area_ground_pass_finds_lower_reachable_area,
                                        test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_bot_movement_view_target_uses_retail_route_context,
                                        test_setup,
                                        test_teardown),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
