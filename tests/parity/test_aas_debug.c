#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <cmocka.h>

#include "botlib/aas/aas_debug.h"
#include "botlib/aas/aas_local.h"
#include "botlib/aas/aas_map.h"
#include "botlib/aas/aas_sound.h"
#include "botlib/common/l_crc.h"
#include "botlib/common/l_libvar.h"
#include "botlib/common/l_memory.h"
#include "botlib/interface/botlib_interface.h"
#include "botlib/precomp/l_precomp.h"
#include "q2bridge/bridge.h"

#ifndef PROJECT_SOURCE_DIR
#error "PROJECT_SOURCE_DIR must be defined for the soundconfig parity fixtures."
#endif

#define ARRAY_LEN(x) (sizeof(x) / sizeof((x)[0]))
#define TEST_RETAIL_TFL_WALK 0x00000002
#define TEST_RETAIL_TFL_AIR 0x00008000
#define TEST_RETAIL_TFL_WATER 0x00010000

typedef struct captured_print_s
{
    int priority;
    char message[256];
} captured_print_t;

typedef struct aas_debug_test_context_s
{
    botlib_import_table_t imports;
    bot_import_extended_t bridge_imports;
    captured_print_t prints[64];
    size_t print_count;
    bsp_trace_t bridge_trace_result;
	bsp_trace_t bridge_trace_results[8];
	size_t bridge_trace_result_count;
    int bridge_trace_count;
    vec3_t bridge_trace_start;
    vec3_t bridge_trace_mins;
    vec3_t bridge_trace_maxs;
    vec3_t bridge_trace_end;
    int bridge_trace_passent;
    int bridge_trace_contentmask;
	vec3_t bridge_trace_starts[8];
	vec3_t bridge_trace_ends[8];
	int bridge_trace_passents[8];
	int bridge_trace_contentmasks[8];
    int bridge_point_contents_result;
	int bridge_point_contents_results[16];
	size_t bridge_point_contents_result_count;
    int bridge_point_contents_count;
    vec3_t bridge_point_contents_point;
	vec3_t bridge_point_contents_points[16];
    aas_area_t *areas;
    aas_reachability_t *reachability;
    aas_areasettings_t *areasettings;
    aas_plane_t *planes;
    aas_node_t *nodes;
    aas_vertex_t *vertexes;
    aas_edge_t *edges;
    int *edgeIndex;
    aas_face_t *faces;
    int *faceIndex;
    aas_portal_t *portals;
    aas_cluster_t *clusters;
	int *portalIndex;
	aas_bspmodel_t *bspModels;
	aas_bspnode_t *bspNodes;
	aas_bspleaf_t *bspLeaves;
	unsigned short *bspLeafBrushes;
	aas_plane_t *bspPlanes;
	aas_bspbrushside_t *bspBrushSides;
	aas_bspbrush_t *bspBrushes;
} aas_debug_test_context_t;

static aas_debug_test_context_t *g_active_context = NULL;

static void Mock_Print(int priority, const char *fmt, ...)
{
    if (g_active_context == NULL || fmt == NULL)
    {
        return;
    }

    va_list args;
    va_start(args, fmt);

    char buffer[256];
    vsnprintf(buffer, sizeof(buffer), fmt, args);

    va_end(args);

    size_t index = g_active_context->print_count;
    if (index >= ARRAY_LEN(g_active_context->prints))
    {
        index = ARRAY_LEN(g_active_context->prints) - 1U;
    }

    captured_print_t *slot = &g_active_context->prints[index];
    slot->priority = priority;
    strncpy(slot->message, buffer, sizeof(slot->message) - 1U);
    slot->message[sizeof(slot->message) - 1U] = '\0';

    if (g_active_context->print_count < ARRAY_LEN(g_active_context->prints))
    {
        g_active_context->print_count += 1U;
    }
}

static bsp_trace_t Mock_BridgeTrace(vec3_t start,
                                    vec3_t mins,
                                    vec3_t maxs,
                                    vec3_t end,
                                    int passent,
                                    int contentmask)
{
    if (g_active_context != NULL)
    {
		size_t index = (size_t)g_active_context->bridge_trace_count;
        g_active_context->bridge_trace_count += 1;
        if (start != NULL)
        {
            VectorCopy(start, g_active_context->bridge_trace_start);
        }
        if (mins != NULL)
        {
            VectorCopy(mins, g_active_context->bridge_trace_mins);
        }
        if (maxs != NULL)
        {
            VectorCopy(maxs, g_active_context->bridge_trace_maxs);
        }
        if (end != NULL)
        {
            VectorCopy(end, g_active_context->bridge_trace_end);
        }
        g_active_context->bridge_trace_passent = passent;
        g_active_context->bridge_trace_contentmask = contentmask;
		if (index < ARRAY_LEN(g_active_context->bridge_trace_starts))
		{
			VectorCopy(start, g_active_context->bridge_trace_starts[index]);
			VectorCopy(end, g_active_context->bridge_trace_ends[index]);
			g_active_context->bridge_trace_passents[index] = passent;
			g_active_context->bridge_trace_contentmasks[index] = contentmask;
		}
		if (index < g_active_context->bridge_trace_result_count)
		{
			return g_active_context->bridge_trace_results[index];
		}
        return g_active_context->bridge_trace_result;
    }

    bsp_trace_t trace;
    memset(&trace, 0, sizeof(trace));
    trace.fraction = 1.0f;
    return trace;
}

static int Mock_BridgePointContents(vec3_t point)
{
    if (g_active_context == NULL)
    {
        return 0;
    }

	size_t index = (size_t)g_active_context->bridge_point_contents_count;
	g_active_context->bridge_point_contents_count += 1;
    if (point != NULL)
    {
        VectorCopy(point, g_active_context->bridge_point_contents_point);
		if (index < ARRAY_LEN(g_active_context->bridge_point_contents_points))
		{
			VectorCopy(point,
				g_active_context->bridge_point_contents_points[index]);
		}
    }
	if (index < g_active_context->bridge_point_contents_result_count)
	{
		return g_active_context->bridge_point_contents_results[index];
	}
    return g_active_context->bridge_point_contents_result;
}

static void Mock_Reset(aas_debug_test_context_t *context)
{
    if (context == NULL)
    {
        return;
    }

    context->print_count = 0U;
    memset(context->prints, 0, sizeof(context->prints));
    memset(&context->bridge_trace_result, 0, sizeof(context->bridge_trace_result));
    context->bridge_trace_result.fraction = 1.0f;
	memset(context->bridge_trace_results, 0, sizeof(context->bridge_trace_results));
	context->bridge_trace_result_count = 0U;
    context->bridge_trace_count = 0;
    VectorClear(context->bridge_trace_start);
    VectorClear(context->bridge_trace_mins);
    VectorClear(context->bridge_trace_maxs);
    VectorClear(context->bridge_trace_end);
    context->bridge_trace_passent = 0;
    context->bridge_trace_contentmask = 0;
	memset(context->bridge_trace_starts, 0, sizeof(context->bridge_trace_starts));
	memset(context->bridge_trace_ends, 0, sizeof(context->bridge_trace_ends));
	memset(context->bridge_trace_passents, 0, sizeof(context->bridge_trace_passents));
	memset(context->bridge_trace_contentmasks,
		0,
		sizeof(context->bridge_trace_contentmasks));
    context->bridge_point_contents_result = 0;
	memset(context->bridge_point_contents_results,
		0,
		sizeof(context->bridge_point_contents_results));
	context->bridge_point_contents_result_count = 0U;
    context->bridge_point_contents_count = 0;
    VectorClear(context->bridge_point_contents_point);
	memset(context->bridge_point_contents_points,
		0,
		sizeof(context->bridge_point_contents_points));
}

static const char *Mock_FindPrint(const aas_debug_test_context_t *context, const char *needle)
{
    if (context == NULL || needle == NULL)
    {
        return NULL;
    }

    for (size_t index = 0; index < context->print_count; ++index)
    {
        if (strstr(context->prints[index].message, needle) != NULL)
        {
            return context->prints[index].message;
        }
    }

    return NULL;
}

/*
=============
BuildMockMap

Build a one-based AAS fixture with retail outgoing reachability spans.
=============
*/
static void BuildMockMap(aas_debug_test_context_t *context)
{
    assert_non_null(context);

    aasworld.loaded = qtrue;
    aasworld.initialized = qtrue;
    aasworld.numAreas = 4;
	aasworld.numReachability = 3;
    aasworld.numAreaSettings = aasworld.numAreas;

    context->areas = (aas_area_t *)calloc((size_t)aasworld.numAreas, sizeof(aas_area_t));
    assert_non_null(context->areas);
    aasworld.areas = context->areas;

    for (int areanum = 1; areanum < aasworld.numAreas; ++areanum)
    {
        aas_area_t *area = &context->areas[areanum];
        area->areanum = areanum;
        area->numfaces = areanum * 2;
        area->firstface = areanum * 4;
        area->mins[0] = (float)((areanum - 1) * 100 - 16);
        area->mins[1] = -16.0f;
        area->mins[2] = -16.0f;
        area->maxs[0] = (float)((areanum - 1) * 100 + 16);
        area->maxs[1] = 16.0f;
        area->maxs[2] = 16.0f;
        area->center[0] = (float)((areanum - 1) * 100);
        area->center[1] = 0.0f;
        area->center[2] = 0.0f;
    }

    context->areasettings = (aas_areasettings_t *)calloc((size_t)aasworld.numAreaSettings,
                                                        sizeof(aas_areasettings_t));
    assert_non_null(context->areasettings);
    aasworld.areasettings = context->areasettings;

    for (int areanum = 1; areanum < aasworld.numAreaSettings; ++areanum)
    {
        aas_areasettings_t *settings = &context->areasettings[areanum];
        settings->cluster = areanum * 10;
        settings->presencetype = areanum + 4;
        settings->firstreachablearea = 0;
		settings->numreachableareas = 0;
        settings->contents = 0;
    }
	context->areasettings[1].firstreachablearea = 1;
	context->areasettings[1].numreachableareas = 1;
	context->areasettings[2].firstreachablearea = 2;
	context->areasettings[2].numreachableareas = 1;
	context->areasettings[3].firstreachablearea = 3;
	context->areasettings[3].numreachableareas = 0;

    context->reachability = (aas_reachability_t *)calloc((size_t)aasworld.numReachability, sizeof(aas_reachability_t));
    assert_non_null(context->reachability);
    aasworld.reachability = context->reachability;

	aas_reachability_t *first = &context->reachability[1];
	first->facenum = 1;
    first->areanum = 2;
    first->traveltype = 7;
    first->traveltime = 30;
    first->start[0] = 0.0f;
    first->start[1] = 0.0f;
    first->start[2] = 0.0f;
    first->end[0] = 100.0f;
    first->end[1] = 0.0f;
    first->end[2] = 0.0f;

	aas_reachability_t *second = &context->reachability[2];
	second->facenum = 1;
    second->areanum = 3;
    second->traveltype = 9;
    second->traveltime = 45;
    second->start[0] = 100.0f;
    second->start[1] = 0.0f;
    second->start[2] = 0.0f;
    second->end[0] = 200.0f;
    second->end[1] = 0.0f;
    second->end[2] = 0.0f;

	aasworld.reachabilityFromArea =
		(int *)calloc((size_t)aasworld.numReachability, sizeof(int));
	assert_non_null(aasworld.reachabilityFromArea);
	aasworld.reachabilityFromArea[1] = 1;
	aasworld.reachabilityFromArea[2] = 2;

	/* Successful retail map loads initialise both fixed entity-link pools. */
	AAS_InitBSPLinkHeap();
	AAS_InitAASLinkHeap();

    aasworld.numPlanes = 2;
    context->planes = (aas_plane_t *)calloc((size_t)aasworld.numPlanes, sizeof(aas_plane_t));
    assert_non_null(context->planes);
    aasworld.planes = context->planes;
    VectorSet(context->planes[0].normal, 1.0f, 0.0f, 0.0f);
    context->planes[0].dist = 50.0f;
    VectorSet(context->planes[1].normal, 0.0f, 0.0f, 1.0f);
    context->planes[1].dist = 0.0f;

    aasworld.numNodes = 2;
    context->nodes = (aas_node_t *)calloc((size_t)aasworld.numNodes, sizeof(aas_node_t));
    assert_non_null(context->nodes);
    aasworld.nodes = context->nodes;
    context->nodes[1].planenum = 0;
    context->nodes[1].children[0] = -2;
    context->nodes[1].children[1] = -1;

    aasworld.numVertexes = 4;
    context->vertexes = (aas_vertex_t *)calloc((size_t)aasworld.numVertexes, sizeof(aas_vertex_t));
    assert_non_null(context->vertexes);
    aasworld.vertexes = context->vertexes;
    VectorSet(context->vertexes[0], 80.0f, -20.0f, 0.0f);
    VectorSet(context->vertexes[1], 120.0f, -20.0f, 0.0f);
    VectorSet(context->vertexes[2], 120.0f, 20.0f, 0.0f);
    VectorSet(context->vertexes[3], 80.0f, 20.0f, 0.0f);

    aasworld.numEdges = 5;
    context->edges = (aas_edge_t *)calloc((size_t)aasworld.numEdges, sizeof(aas_edge_t));
    assert_non_null(context->edges);
    aasworld.edges = context->edges;
    context->edges[1].v[0] = 0;
    context->edges[1].v[1] = 1;
    context->edges[2].v[0] = 1;
    context->edges[2].v[1] = 2;
    context->edges[3].v[0] = 2;
    context->edges[3].v[1] = 3;
    context->edges[4].v[0] = 3;
    context->edges[4].v[1] = 0;

    aasworld.edgeIndexSize = 4;
    context->edgeIndex = (int *)calloc((size_t)aasworld.edgeIndexSize, sizeof(int));
    assert_non_null(context->edgeIndex);
    aasworld.edgeIndex = context->edgeIndex;
    context->edgeIndex[0] = -4;
    context->edgeIndex[1] = -3;
    context->edgeIndex[2] = -2;
    context->edgeIndex[3] = -1;

    aasworld.numFaces = 2;
    context->faces = (aas_face_t *)calloc((size_t)aasworld.numFaces, sizeof(aas_face_t));
    assert_non_null(context->faces);
    aasworld.faces = context->faces;
    context->faces[1].planenum = 1;
    context->faces[1].faceflags = AAS_FACE_GROUND;
    context->faces[1].numedges = 4;
    context->faces[1].firstedge = 0;
    context->faces[1].frontarea = 2;

    aasworld.faceIndexSize = 1;
    context->faceIndex = (int *)calloc((size_t)aasworld.faceIndexSize, sizeof(int));
    assert_non_null(context->faceIndex);
    aasworld.faceIndex = context->faceIndex;
    context->faceIndex[0] = 1;
    context->areas[2].numfaces = 1;
    context->areas[2].firstface = 0;
}

static void ConfigureRoutePredictionFixture(aas_debug_test_context_t *context)
{
    assert_non_null(context);

    AAS_ClearReachabilityData();
    AAS_FreeAllRoutingCaches();

    free(context->reachability);
	aasworld.numReachability = 3;
    context->reachability = (aas_reachability_t *)calloc((size_t)aasworld.numReachability, sizeof(aas_reachability_t));
    assert_non_null(context->reachability);
    aasworld.reachability = context->reachability;

    aasworld.numAreaSettings = aasworld.numAreas;
    context->areasettings[1].firstreachablearea = 1;
    context->areasettings[1].numreachableareas = 1;
    context->areasettings[1].contents = 0;
    context->areasettings[2].firstreachablearea = 2;
    context->areasettings[2].numreachableareas = 1;
    context->areasettings[2].contents = 0;
    context->areasettings[3].firstreachablearea = 0;
    context->areasettings[3].numreachableareas = 0;
    context->areasettings[3].contents = 0;

    aas_reachability_t *first = &context->reachability[1];
    first->facenum = 1;
    first->areanum = 2;
    first->traveltype = TRAVEL_WALK;
    first->traveltime = 30;
    VectorSet(first->start, 0.0f, 0.0f, 0.0f);
    VectorSet(first->end, 100.0f, 0.0f, 0.0f);

    aas_reachability_t *second = &context->reachability[2];
    second->facenum = 2;
    second->areanum = 3;
    second->traveltype = TRAVEL_JUMP;
    second->traveltime = 45;
    VectorSet(second->start, 100.0f, 0.0f, 0.0f);
    VectorSet(second->end, 200.0f, 0.0f, 0.0f);

	AAS_InitTravelFlagFromType();
	AAS_InitAreaContentsTravelFlags();
	assert_int_equal(AAS_PrepareReachability(), BLERR_NOERROR);
	assert_true(AAS_InitRetailRoutingCaches());
}

/*
=============
ConfigureAlternativeRouteFaceFixture

Connect the three routed areas in stored face order for retail flood tests.
=============
*/
static void ConfigureAlternativeRouteFaceFixture(
	aas_debug_test_context_t *context)
{
	assert_non_null(context);

	free(context->faces);
	aasworld.numFaces = 3;
	context->faces = (aas_face_t *)calloc(
		(size_t)aasworld.numFaces,
		sizeof(*context->faces));
	assert_non_null(context->faces);
	aasworld.faces = context->faces;
	context->faces[1].frontarea = 1;
	context->faces[1].backarea = 2;
	context->faces[2].frontarea = 2;
	context->faces[2].backarea = 3;

	free(context->faceIndex);
	aasworld.faceIndexSize = 4;
	context->faceIndex = (int *)calloc(
		(size_t)aasworld.faceIndexSize,
		sizeof(*context->faceIndex));
	assert_non_null(context->faceIndex);
	aasworld.faceIndex = context->faceIndex;
	context->faceIndex[0] = 1;
	context->faceIndex[1] = 1;
	context->faceIndex[2] = 2;
	context->faceIndex[3] = 2;

	context->areas[1].firstface = 0;
	context->areas[1].numfaces = 1;
	context->areas[2].firstface = 1;
	context->areas[2].numfaces = 2;
	context->areas[3].firstface = 3;
	context->areas[3].numfaces = 1;
	context->areasettings[3].numreachableareas = 1;

	assert_true(AAS_InitRetailRoutingCaches());
}

/*
=============
ConfigureRoutePassAreaFixture

Build the reachability fixture used to verify generated pass-area stops.
=============
*/
static void ConfigureRoutePassAreaFixture(aas_debug_test_context_t *context)
{
    assert_non_null(context);

    AAS_ClearReachabilityData();
    AAS_FreeAllRoutingCaches();

    free(context->reachability);
	aasworld.numReachability = 3;
    context->reachability = (aas_reachability_t *)calloc((size_t)aasworld.numReachability, sizeof(aas_reachability_t));
    assert_non_null(context->reachability);
    aasworld.reachability = context->reachability;

    aasworld.numAreaSettings = aasworld.numAreas;
    for (int areanum = 1; areanum < aasworld.numAreas; ++areanum)
    {
        context->areasettings[areanum].firstreachablearea = 0;
        context->areasettings[areanum].numreachableareas = 0;
        context->areasettings[areanum].contents = 0;
        context->areasettings[areanum].areaflags = 0;
    }

    context->areasettings[1].firstreachablearea = 1;
    context->areasettings[1].numreachableareas = 1;

    aas_reachability_t *direct = &context->reachability[1];
    direct->facenum = 1;
    direct->areanum = 3;
    direct->traveltype = TRAVEL_GRAPPLEHOOK;
    direct->traveltime = 60;
    VectorSet(direct->start, 0.0f, 0.0f, 0.0f);
    VectorSet(direct->end, 100.0f, 0.0f, 0.0f);

    AAS_InitTravelFlagFromType();
    AAS_InitAreaContentsTravelFlags();
    assert_int_equal(AAS_PrepareReachability(), BLERR_NOERROR);
    AAS_InvalidateRouteCache();
}

static void ConfigureBSPBrushModelFixture(aas_debug_test_context_t *context)
{
	assert_non_null(context);

	aasworld.numBspModels = 2;
	context->bspModels = (aas_bspmodel_t *)calloc((size_t)aasworld.numBspModels, sizeof(aas_bspmodel_t));
	assert_non_null(context->bspModels);
	aasworld.bspModels = context->bspModels;
	context->bspModels[0].headnode = -1;
	VectorSet(context->bspModels[1].mins, -4.0f, -12.0f, -8.0f);
	VectorSet(context->bspModels[1].maxs, 4.0f, 12.0f, 8.0f);
	VectorClear(context->bspModels[1].origin);
	context->bspModels[1].headnode = -2;

	aasworld.numBspLeaves = 2;
	context->bspLeaves = (aas_bspleaf_t *)calloc((size_t)aasworld.numBspLeaves, sizeof(aas_bspleaf_t));
	assert_non_null(context->bspLeaves);
	aasworld.bspLeaves = context->bspLeaves;
	context->bspLeaves[1].contents = CONTENTS_SOLID;
	context->bspLeaves[1].firstleafbrush = 0;
	context->bspLeaves[1].numleafbrushes = 1;

	aasworld.bspLeafBrushIndexSize = 1;
	context->bspLeafBrushes =
	    (unsigned short *)calloc((size_t)aasworld.bspLeafBrushIndexSize, sizeof(unsigned short));
	assert_non_null(context->bspLeafBrushes);
	aasworld.bspLeafBrushes = context->bspLeafBrushes;
	context->bspLeafBrushes[0] = 0;

	aasworld.numBspPlanes = 6;
	context->bspPlanes = (aas_plane_t *)calloc((size_t)aasworld.numBspPlanes, sizeof(aas_plane_t));
	assert_non_null(context->bspPlanes);
	aasworld.bspPlanes = context->bspPlanes;
	VectorSet(context->bspPlanes[0].normal, 1.0f, 0.0f, 0.0f);
	context->bspPlanes[0].dist = 10.0f;
	context->bspPlanes[0].type = 0;
	VectorSet(context->bspPlanes[1].normal, -1.0f, 0.0f, 0.0f);
	context->bspPlanes[1].dist = 10.0f;
	context->bspPlanes[1].type = 0;
	VectorSet(context->bspPlanes[2].normal, 0.0f, 1.0f, 0.0f);
	context->bspPlanes[2].dist = 10.0f;
	context->bspPlanes[2].type = 1;
	VectorSet(context->bspPlanes[3].normal, 0.0f, -1.0f, 0.0f);
	context->bspPlanes[3].dist = 10.0f;
	context->bspPlanes[3].type = 1;
	VectorSet(context->bspPlanes[4].normal, 0.0f, 0.0f, 1.0f);
	context->bspPlanes[4].dist = 10.0f;
	context->bspPlanes[4].type = 2;
	VectorSet(context->bspPlanes[5].normal, 0.0f, 0.0f, -1.0f);
	context->bspPlanes[5].dist = 10.0f;
	context->bspPlanes[5].type = 2;

	aasworld.numBspBrushSides = 6;
	context->bspBrushSides =
	    (aas_bspbrushside_t *)calloc((size_t)aasworld.numBspBrushSides, sizeof(aas_bspbrushside_t));
	assert_non_null(context->bspBrushSides);
	aasworld.bspBrushSides = context->bspBrushSides;
	for (int side = 0; side < aasworld.numBspBrushSides; ++side)
	{
		context->bspBrushSides[side].planenum = (unsigned short)side;
	}

	aasworld.numBspBrushes = 1;
	context->bspBrushes = (aas_bspbrush_t *)calloc((size_t)aasworld.numBspBrushes, sizeof(aas_bspbrush_t));
	assert_non_null(context->bspBrushes);
	aasworld.bspBrushes = context->bspBrushes;
	context->bspBrushes[0].firstside = 0;
	context->bspBrushes[0].numsides = 6;
	context->bspBrushes[0].contents = CONTENTS_SOLID;
}

static void CleanupRoutePredictionFixture(void)
{
    AAS_ClearReachabilityData();
    AAS_FreeAllRoutingCaches();
    free(aasworld.areacontentstravelflags);
    aasworld.areacontentstravelflags = NULL;
}

/*
=============
ConfigureRetailRouteCacheFixture

Build cluster and portal metadata for the retail cache-table lifecycle tests.
=============
*/
static void ConfigureRetailRouteCacheFixture(aas_debug_test_context_t *context)
{
	assert_non_null(context);
	assert_null(context->clusters);
	assert_null(context->portals);

	aasworld.numClusters = 2;
	context->clusters =
		(aas_cluster_t *)calloc((size_t)aasworld.numClusters, sizeof(aas_cluster_t));
	assert_non_null(context->clusters);
	aasworld.clusters = context->clusters;
	context->clusters[0].numareas = 2;
	context->clusters[1].numareas = 3;

	aasworld.numPortals = 2;
	context->portals =
		(aas_portal_t *)calloc((size_t)aasworld.numPortals, sizeof(aas_portal_t));
	assert_non_null(context->portals);
	aasworld.portals = context->portals;
	context->portals[1].areanum = 3;
	context->portals[1].frontcluster = 1;
	context->portals[1].backcluster = 0;
	context->portals[1].clusterareanum[0] = 2;
	context->portals[1].clusterareanum[1] = 1;

	context->areasettings[1].cluster = 1;
	context->areasettings[1].clusterareanum = 0;
	context->areasettings[2].cluster = 1;
	context->areasettings[2].clusterareanum = 1;
	context->areasettings[3].cluster = -1;
	context->areasettings[3].clusterareanum = 0;

	assert_true(AAS_InitRetailRoutingCaches());
}

/*
=============
ConfigureRetailAreaPropagationFixture

Build a cluster graph that exposes retail FIFO order and equal-time retention.
=============
*/
static void ConfigureRetailAreaPropagationFixture(aas_debug_test_context_t *context)
{
	assert_non_null(context);
	assert_null(context->clusters);
	assert_null(context->portals);

	AAS_ClearReachabilityData();
	AAS_FreeAllRoutingCaches();
	free(context->areas);
	free(context->areasettings);
	free(context->reachability);
	context->areas = NULL;
	context->areasettings = NULL;
	context->reachability = NULL;

	aasworld.numAreas = 6;
	aasworld.numAreaSettings = aasworld.numAreas;
	aasworld.numReachability = 7;
	context->areas =
		(aas_area_t *)calloc((size_t)aasworld.numAreas, sizeof(aas_area_t));
	context->areasettings = (aas_areasettings_t *)calloc(
		(size_t)aasworld.numAreaSettings,
		sizeof(aas_areasettings_t));
	context->reachability = (aas_reachability_t *)calloc(
		(size_t)aasworld.numReachability,
		sizeof(aas_reachability_t));
	assert_non_null(context->areas);
	assert_non_null(context->areasettings);
	assert_non_null(context->reachability);
	aasworld.areas = context->areas;
	aasworld.areasettings = context->areasettings;
	aasworld.reachability = context->reachability;

	aasworld.numClusters = 2;
	context->clusters =
		(aas_cluster_t *)calloc((size_t)aasworld.numClusters, sizeof(aas_cluster_t));
	assert_non_null(context->clusters);
	aasworld.clusters = context->clusters;
	context->clusters[1].numareas = 5;
	aasworld.numPortals = 0;
	aasworld.portals = NULL;

	for (int areanum = 1; areanum < aasworld.numAreas; ++areanum)
	{
		context->areas[areanum].areanum = areanum;
		context->areasettings[areanum].cluster = 1;
		context->areasettings[areanum].clusterareanum = areanum - 1;
		context->areasettings[areanum].presencetype = PRESENCE_NORMAL;
	}
	context->areasettings[1].firstreachablearea = 1;
	context->areasettings[1].numreachableareas = 2;
	context->areasettings[2].firstreachablearea = 3;
	context->areasettings[2].numreachableareas = 1;
	context->areasettings[3].firstreachablearea = 4;
	context->areasettings[3].numreachableareas = 1;
	context->areasettings[4].firstreachablearea = 5;
	context->areasettings[4].numreachableareas = 1;
	context->areasettings[5].firstreachablearea = 6;
	context->areasettings[5].numreachableareas = 1;

	context->reachability[1].areanum = 2;
	context->reachability[1].traveltype = TRAVEL_WALK;
	context->reachability[1].traveltime = 5;
	context->reachability[1].start[0] = 10.0f;
	context->reachability[2].areanum = 3;
	context->reachability[2].traveltype = TRAVEL_WALK;
	context->reachability[2].traveltime = 5;
	context->reachability[2].start[0] = 100.0f;
	context->reachability[3].areanum = 4;
	context->reachability[3].traveltype = TRAVEL_WALK;
	context->reachability[3].traveltime = 10;
	context->reachability[4].areanum = 4;
	context->reachability[4].traveltype = TRAVEL_WALK;
	context->reachability[4].traveltime = 10;
	context->reachability[5].areanum = 4;
	context->reachability[5].traveltype = TRAVEL_JUMP;
	context->reachability[5].traveltime = 1;
	context->reachability[6].areanum = 1;
	context->reachability[6].traveltype = TRAVEL_WALK;
	context->reachability[6].traveltime = 5;

	assert_int_equal(AAS_PrepareReachability(), BLERR_NOERROR);
	assert_true(AAS_InitRetailRoutingCaches());
}

/*
=============
ResetRetailPortalGraphFixture

Replace the default map with cleared routing arrays for a synthetic portal graph.
=============
*/
static void ResetRetailPortalGraphFixture(aas_debug_test_context_t *context,
	int numareas,
	int numreachability,
	int numclusters,
	int numportals,
	int portalindexsize)
{
	assert_non_null(context);

	AAS_ClearReachabilityData();
	AAS_FreeAllRoutingCaches();
	free(context->areas);
	free(context->areasettings);
	free(context->reachability);
	free(context->clusters);
	free(context->portals);
	free(context->portalIndex);
	context->areas = NULL;
	context->areasettings = NULL;
	context->reachability = NULL;
	context->clusters = NULL;
	context->portals = NULL;
	context->portalIndex = NULL;

	aasworld.numAreas = numareas;
	aasworld.numAreaSettings = numareas;
	aasworld.numReachability = numreachability;
	aasworld.numClusters = numclusters;
	aasworld.numPortals = numportals;
	aasworld.portalIndexSize = portalindexsize;
	context->areas =
		(aas_area_t *)calloc((size_t)numareas, sizeof(aas_area_t));
	context->areasettings = (aas_areasettings_t *)calloc(
		(size_t)numareas,
		sizeof(aas_areasettings_t));
	context->reachability = (aas_reachability_t *)calloc(
		(size_t)numreachability,
		sizeof(aas_reachability_t));
	context->clusters =
		(aas_cluster_t *)calloc((size_t)numclusters, sizeof(aas_cluster_t));
	context->portals =
		(aas_portal_t *)calloc((size_t)numportals, sizeof(aas_portal_t));
	context->portalIndex =
		(int *)calloc((size_t)portalindexsize, sizeof(int));
	assert_non_null(context->areas);
	assert_non_null(context->areasettings);
	assert_non_null(context->reachability);
	assert_non_null(context->clusters);
	assert_non_null(context->portals);
	assert_non_null(context->portalIndex);
	aasworld.areas = context->areas;
	aasworld.areasettings = context->areasettings;
	aasworld.reachability = context->reachability;
	aasworld.clusters = context->clusters;
	aasworld.portals = context->portals;
	aasworld.portalIndex = context->portalIndex;

	for (int areanum = 1; areanum < numareas; ++areanum)
	{
		context->areas[areanum].areanum = areanum;
		context->areasettings[areanum].presencetype = PRESENCE_NORMAL;
	}
}

/*
=============
ConfigureRetailPortalLineFixture

Build a bidirectional two-portal chain spanning three clusters.
=============
*/
static void ConfigureRetailPortalLineFixture(aas_debug_test_context_t *context)
{
	ResetRetailPortalGraphFixture(context, 6, 7, 4, 3, 4);

	context->clusters[1].numareas = 2;
	context->clusters[1].numportals = 1;
	context->clusters[1].firstportal = 0;
	context->clusters[2].numareas = 2;
	context->clusters[2].numportals = 2;
	context->clusters[2].firstportal = 1;
	context->clusters[3].numareas = 2;
	context->clusters[3].numportals = 1;
	context->clusters[3].firstportal = 3;
	context->portalIndex[0] = 1;
	context->portalIndex[1] = 1;
	context->portalIndex[2] = 2;
	context->portalIndex[3] = 2;

	context->portals[1].areanum = 4;
	context->portals[1].frontcluster = 1;
	context->portals[1].backcluster = 2;
	context->portals[1].clusterareanum[0] = 1;
	context->portals[1].clusterareanum[1] = 0;
	context->portals[2].areanum = 5;
	context->portals[2].frontcluster = 2;
	context->portals[2].backcluster = 3;
	context->portals[2].clusterareanum[0] = 1;
	context->portals[2].clusterareanum[1] = 1;

	context->areasettings[1].cluster = 1;
	context->areasettings[1].clusterareanum = 0;
	context->areasettings[1].firstreachablearea = 1;
	context->areasettings[1].numreachableareas = 1;
	context->areasettings[3].cluster = 3;
	context->areasettings[3].clusterareanum = 0;
	context->areasettings[3].firstreachablearea = 2;
	context->areasettings[3].numreachableareas = 1;
	context->areasettings[4].cluster = -1;
	context->areasettings[4].firstreachablearea = 3;
	context->areasettings[4].numreachableareas = 2;
	context->areasettings[5].cluster = -2;
	context->areasettings[5].firstreachablearea = 5;
	context->areasettings[5].numreachableareas = 2;

	context->reachability[1].areanum = 4;
	context->reachability[2].areanum = 5;
	context->reachability[3].areanum = 1;
	context->reachability[4].areanum = 5;
	context->reachability[5].areanum = 4;
	context->reachability[6].areanum = 3;
	for (int reachnum = 1; reachnum < aasworld.numReachability; ++reachnum)
	{
		context->reachability[reachnum].traveltype = TRAVEL_WALK;
		context->reachability[reachnum].traveltime = 4;
	}

	assert_int_equal(AAS_PrepareReachability(), BLERR_NOERROR);
	assert_true(AAS_InitRetailRoutingCaches());
}

/*
=============
ConfigureRetailPortalEqualFixture

Build a diamond whose equal third-portal costs expose retained FIFO state.
=============
*/
static void ConfigureRetailPortalEqualFixture(aas_debug_test_context_t *context)
{
	ResetRetailPortalGraphFixture(context, 7, 6, 4, 4, 6);

	context->clusters[1].numareas = 3;
	context->clusters[1].numportals = 2;
	context->clusters[1].firstportal = 0;
	context->clusters[2].numareas = 2;
	context->clusters[2].numportals = 2;
	context->clusters[2].firstportal = 2;
	context->clusters[3].numareas = 2;
	context->clusters[3].numportals = 2;
	context->clusters[3].firstportal = 4;
	context->portalIndex[0] = 1;
	context->portalIndex[1] = 2;
	context->portalIndex[2] = 1;
	context->portalIndex[3] = 3;
	context->portalIndex[4] = 2;
	context->portalIndex[5] = 3;

	context->portals[1].areanum = 4;
	context->portals[1].frontcluster = 1;
	context->portals[1].backcluster = 2;
	context->portals[1].clusterareanum[0] = 1;
	context->portals[1].clusterareanum[1] = 0;
	context->portals[2].areanum = 5;
	context->portals[2].frontcluster = 1;
	context->portals[2].backcluster = 3;
	context->portals[2].clusterareanum[0] = 2;
	context->portals[2].clusterareanum[1] = 0;
	context->portals[3].areanum = 6;
	context->portals[3].frontcluster = 2;
	context->portals[3].backcluster = 3;
	context->portals[3].clusterareanum[0] = 1;
	context->portals[3].clusterareanum[1] = 1;

	context->areasettings[1].cluster = 1;
	context->areasettings[1].clusterareanum = 0;
	context->areasettings[1].firstreachablearea = 1;
	context->areasettings[1].numreachableareas = 1;
	context->areasettings[4].cluster = -1;
	context->areasettings[4].firstreachablearea = 2;
	context->areasettings[4].numreachableareas = 1;
	context->areasettings[5].cluster = -2;
	context->areasettings[5].firstreachablearea = 3;
	context->areasettings[5].numreachableareas = 1;
	context->areasettings[6].cluster = -3;
	context->areasettings[6].firstreachablearea = 4;
	context->areasettings[6].numreachableareas = 2;

	context->reachability[1].areanum = 4;
	context->reachability[2].areanum = 1;
	context->reachability[3].areanum = 1;
	context->reachability[4].areanum = 4;
	context->reachability[5].areanum = 5;
	for (int reachnum = 1; reachnum < aasworld.numReachability; ++reachnum)
	{
		context->reachability[reachnum].traveltype = TRAVEL_WALK;
		context->reachability[reachnum].traveltime = 4;
	}

	assert_int_equal(AAS_PrepareReachability(), BLERR_NOERROR);
	assert_true(AAS_InitRetailRoutingCaches());
}

static int setup_aas_debug(void **state)
{
    aas_debug_test_context_t *context = (aas_debug_test_context_t *)calloc(1, sizeof(*context));
    assert_non_null(context);

    context->imports.Print = Mock_Print;
    BotInterface_SetImportTable(&context->imports);
    context->bridge_imports.Trace = Mock_BridgeTrace;
    context->bridge_imports.PointContents = Mock_BridgePointContents;
    Q2Bridge_SetImportTable(&context->bridge_imports);

    g_active_context = context;
    BuildMockMap(context);

    *state = context;
    return 0;
}

/*
=============
teardown_aas_debug

Release prepared reachability metadata and the in-memory debug fixture.
=============
*/
static int teardown_aas_debug(void **state)
{
    aas_debug_test_context_t *context = (aas_debug_test_context_t *)(*state);
	BotInterface_SetImportTable(NULL);
	Q2Bridge_ClearImportTable();
	AAS_ClearReachabilityData();
	AAS_FreeAllRoutingCaches();
	free(aasworld.areacontentstravelflags);
	aasworld.areacontentstravelflags = NULL;

    if (context != NULL)
    {
        if (context->areas != NULL)
        {
            free(context->areas);
        }
        if (context->areasettings != NULL)
        {
            free(context->areasettings);
        }
        if (context->reachability != NULL)
        {
            free(context->reachability);
        }
        if (context->planes != NULL)
        {
            free(context->planes);
        }
        if (context->nodes != NULL)
        {
            free(context->nodes);
        }
        if (context->vertexes != NULL)
        {
            free(context->vertexes);
        }
        if (context->edges != NULL)
        {
            free(context->edges);
        }
        if (context->edgeIndex != NULL)
        {
            free(context->edgeIndex);
        }
        if (context->faces != NULL)
        {
            free(context->faces);
        }
        if (context->faceIndex != NULL)
        {
            free(context->faceIndex);
        }
        if (context->portals != NULL)
        {
            free(context->portals);
        }
        if (context->clusters != NULL)
        {
            free(context->clusters);
        }
		if (context->portalIndex != NULL)
		{
			free(context->portalIndex);
		}
		if (context->bspModels != NULL)
		{
			free(context->bspModels);
		}
		if (context->bspNodes != NULL)
		{
			free(context->bspNodes);
		}
		if (context->bspLeaves != NULL)
		{
			free(context->bspLeaves);
		}
		if (context->bspLeafBrushes != NULL)
		{
			free(context->bspLeafBrushes);
		}
		if (context->bspPlanes != NULL)
		{
			free(context->bspPlanes);
		}
		if (context->bspBrushSides != NULL)
		{
			free(context->bspBrushSides);
		}
		if (context->bspBrushes != NULL)
		{
			free(context->bspBrushes);
		}
    }

    memset(&aasworld, 0, sizeof(aasworld));
    g_active_context = NULL;
    free(context);
    return 0;
}

/*
=============
test_bot_test_dumps_area_info

Verify area diagnostics enumerate the area's authoritative outgoing span.
=============
*/
static void test_bot_test_dumps_area_info(void **state)
{
    aas_debug_test_context_t *context = (aas_debug_test_context_t *)(*state);
    Mock_Reset(context);

    vec3_t origin = { 10.0f, 0.0f, 0.0f };
    vec3_t angles = { 0.0f, 90.0f, 0.0f };

    AAS_DebugBotTest(7, "2", origin, angles);

    assert_true(context->print_count > 0);
    assert_non_null(Mock_FindPrint(context, "bot_test entity 7"));
    assert_non_null(Mock_FindPrint(context, "area 2:"));
	assert_non_null(Mock_FindPrint(context, "reach[2]: 2 -> 3"));
}

/*
=============
test_bot_test_rejects_area_lump_count

Ensure the terminal area-lump count is not accepted as a real area index.
=============
*/
static void test_bot_test_rejects_area_lump_count(void **state)
{
	aas_debug_test_context_t *context = (aas_debug_test_context_t *)(*state);
	Mock_Reset(context);

	vec3_t origin = {100.0f, 0.0f, 0.0f};
	vec3_t angles = {0.0f, 0.0f, 0.0f};
	AAS_DebugBotTest(7, "4", origin, angles);

	assert_non_null(Mock_FindPrint(context, "area 2:"));
	assert_null(Mock_FindPrint(context, "area 0:"));
}

/*
=============
test_bot_test_resolves_point_through_aas_tree

Ensure point fallback follows the AAS node tree instead of overlapping bounds.
=============
*/
static void test_bot_test_resolves_point_through_aas_tree(void **state)
{
	aas_debug_test_context_t *context = (aas_debug_test_context_t *)(*state);
	Mock_Reset(context);
	context->areas[1].maxs[0] = 200.0f;

	vec3_t origin = {100.0f, 0.0f, 0.0f};
	vec3_t angles = {0.0f, 0.0f, 0.0f};
	AAS_DebugBotTest(7, NULL, origin, angles);

	assert_non_null(Mock_FindPrint(context, "area 2:"));
	assert_null(Mock_FindPrint(context, "area 1:"));
}

static void test_aas_showpath_reports_path(void **state)
{
    aas_debug_test_context_t *context = (aas_debug_test_context_t *)(*state);
    Mock_Reset(context);

    vec3_t start = { 0.0f, 0.0f, 0.0f };
    vec3_t goal = { 200.0f, 0.0f, 0.0f };

    AAS_DebugShowPath(1, 3, start, goal);

    assert_non_null(Mock_FindPrint(context, "aas_showpath start=1 goal=3"));
    assert_non_null(Mock_FindPrint(context, "step 0: 1 -> 2"));
	assert_non_null(Mock_FindPrint(context, "step 1: 2 -> 3"));
	assert_non_null(Mock_FindPrint(context, "total steps=2"));

	free(aasworld.reachabilityFromArea);
	aasworld.reachabilityFromArea = NULL;
	Mock_Reset(context);
	AAS_DebugShowPath(1, 3, start, goal);

	assert_non_null(Mock_FindPrint(context, "step 0: 1 -> 2"));
	assert_non_null(Mock_FindPrint(context, "step 1: 2 -> 3"));
}

/*
=============
test_aas_showpath_reports_unreachable_span

Ensure empty or sentinel-based outgoing spans cannot use unrelated source data.
=============
*/
static void test_aas_showpath_reports_unreachable_span(void **state)
{
	aas_debug_test_context_t *context = (aas_debug_test_context_t *)(*state);
	Mock_Reset(context);
	context->areasettings[2].numreachableareas = 0;

	vec3_t start = {0.0f, 0.0f, 0.0f};
	vec3_t goal = {200.0f, 0.0f, 0.0f};
	AAS_DebugShowPath(1, 3, start, goal);

	assert_non_null(Mock_FindPrint(context, "no path found from 1 to 3"));
	assert_null(Mock_FindPrint(context, "step 0:"));

	Mock_Reset(context);
	context->areasettings[2].firstreachablearea = 0;
	context->areasettings[2].numreachableareas = 1;
	AAS_DebugShowPath(1, 3, start, goal);

	assert_non_null(Mock_FindPrint(context, "no path found from 1 to 3"));
	assert_null(Mock_FindPrint(context, "step 0:"));
}

static void test_aas_showareas_lists_requested_areas(void **state)
{
    aas_debug_test_context_t *context = (aas_debug_test_context_t *)(*state);
    Mock_Reset(context);

    int areas[] = { 1, 3 };
    AAS_DebugShowAreas(areas, ARRAY_LEN(areas));

    assert_non_null(Mock_FindPrint(context, "listing 2 areas"));
    assert_non_null(Mock_FindPrint(context, "area 1:"));
    assert_non_null(Mock_FindPrint(context, "area 3:"));
    assert_non_null(Mock_FindPrint(context, "no reachability links from area 3"));
}

/*
=============
test_aas_showareas_excludes_area_lump_count

Ensure the all-areas diagnostic omits the dummy zero and terminal count slot.
=============
*/
static void test_aas_showareas_excludes_area_lump_count(void **state)
{
	aas_debug_test_context_t *context = (aas_debug_test_context_t *)(*state);
	Mock_Reset(context);

	AAS_DebugShowAreas(NULL, 0U);

	assert_non_null(Mock_FindPrint(context, "dumping all 3 areas"));
	assert_non_null(Mock_FindPrint(context, "area 1:"));
	assert_non_null(Mock_FindPrint(context, "area 3:"));
	assert_null(Mock_FindPrint(context, "area 0:"));
}

/*
=============
test_aas_sample_helpers_use_loaded_planes_and_area_settings

Verify sampling and reachability-copy helpers use the loaded AAS records.
=============
*/
static void test_aas_sample_helpers_use_loaded_planes_and_area_settings(void **state)
{
    aas_debug_test_context_t *context = (aas_debug_test_context_t *)(*state);
    Mock_Reset(context);

    vec3_t back_point = { 0.0f, 0.0f, 0.0f };
    vec3_t front_point = { 100.0f, 0.0f, 0.0f };

    assert_int_equal(AAS_PointAreaNum(back_point), 1);
    assert_int_equal(AAS_PointAreaNum(front_point), 2);
    assert_ptr_equal(AAS_PlaneFromNum(0), &context->planes[0]);
    assert_ptr_equal(AAS_PlaneFromNum(1), &context->planes[1]);
    assert_int_equal(AAS_AreaCluster(2), 20);

    context->bridge_trace_result.fraction = 0.25f;
    VectorSet(context->bridge_trace_result.endpos, 25.0f, 0.0f, 0.0f);
    bsp_trace_t bridged_trace = AAS_Trace(back_point,
                                          NULL,
                                          NULL,
                                          front_point,
                                          7,
                                          CONTENTS_SOLID | CONTENTS_PLAYERCLIP);
    assert_int_equal(context->bridge_trace_count, 1);
    assert_float_equal(bridged_trace.fraction, 0.25f, 0.001f);
    assert_float_equal(bridged_trace.endpos[0], 25.0f, 0.001f);
    assert_int_equal(context->bridge_trace_passent, 7);
    assert_int_equal(context->bridge_trace_contentmask, CONTENTS_SOLID | CONTENTS_PLAYERCLIP);
    assert_float_equal(context->bridge_trace_end[0], 100.0f, 0.001f);

	/*
	 * Retail AAS_PointContents is sub_10003080, a single statement:
	 * "1000308e  return data_10063ff0(arg1)". data_10063ff0 is the bot_import
	 * PointContents slot (the host wires it to gi.pointcontents), so the DLL
	 * never consults its own BSP for this call - it returns whatever the
	 * engine says, verbatim, and always costs exactly one import call.
	 */
    context->bridge_point_contents_result = CONTENTS_WATER | CONTENTS_SLIME;
	assert_int_equal(AAS_PointContents(front_point),
		CONTENTS_WATER | CONTENTS_SLIME);
	assert_int_equal(context->bridge_point_contents_count, 1);
	assert_float_equal(context->bridge_point_contents_point[0], 100.0f, 0.001f);
	context->bridge_point_contents_result = 0;
	context->bridge_point_contents_count = 0;

    int bbox_areas[4];
    vec3_t bbox_mins = { 40.0f, -8.0f, -8.0f };
    vec3_t bbox_maxs = { 60.0f, 8.0f, 8.0f };
    int bbox_count = AAS_BBoxAreas(bbox_mins, bbox_maxs, bbox_areas, (int)ARRAY_LEN(bbox_areas));
    assert_int_equal(bbox_count, 2);
    assert_int_equal(bbox_areas[0], 2);
    assert_int_equal(bbox_areas[1], 1);

    int trace_areas[4];
    vec3_t trace_points[4];
    vec3_t trace_start = { 0.0f, 0.0f, 0.0f };
    vec3_t trace_end = { 100.0f, 0.0f, 0.0f };
    int trace_count = AAS_TraceAreas(trace_start,
                                     trace_end,
                                     trace_areas,
                                     trace_points,
                                     (int)ARRAY_LEN(trace_areas));
    assert_int_equal(trace_count, 2);
    assert_int_equal(trace_areas[0], 1);
    assert_int_equal(trace_areas[1], 2);
    assert_float_equal(trace_points[0][0], 0.0f, 0.001f);
    assert_float_equal(trace_points[1][0], 50.0f, 0.001f);

    aas_areainfo_t info;
    memset(&info, 0, sizeof(info));
    assert_int_equal(AAS_AreaInfo(2, &info), (int)sizeof(info));
    assert_int_equal(info.cluster, 20);
    assert_int_equal(info.contents, 0);
    assert_int_equal(info.presencetype, 6);
    assert_float_equal(info.center[0], 100.0f, 0.001f);

    assert_int_equal(AAS_Loaded(), qtrue);
    assert_int_equal(AAS_Initialized(), qtrue);
    assert_float_equal(AAS_Time(), 0.0f, 0.001f);

    aas_reachability_t copied_reach;
    memset(&copied_reach, 0xff, sizeof(copied_reach));
    AAS_ReachabilityFromNum(1, &copied_reach);
	assert_int_equal(copied_reach.areanum, 2);
	assert_int_equal(copied_reach.traveltype, 7);
	assert_int_equal(copied_reach.traveltime, 30);

    AAS_ReachabilityFromNum(99, &copied_reach);
    assert_int_equal(copied_reach.areanum, 0);
    assert_int_equal(copied_reach.traveltype, 0);

    assert_false(AAS_AreaDisabled(2));
    assert_int_equal(AAS_EnableRoutingArea(2, -1), qtrue);
    assert_int_equal(AAS_EnableRoutingArea(2, 0), qtrue);
    assert_true(AAS_AreaDisabled(2));
    assert_int_equal(AAS_EnableRoutingArea(2, -1), qfalse);
    assert_int_equal(AAS_EnableRoutingArea(2, 1), qfalse);
    assert_false(AAS_AreaDisabled(2));

    assert_int_equal(AAS_AreaCluster(99), 0);
    assert_non_null(Mock_FindPrint(context, "AAS_AreaCluster: invalid area number"));

    aasworld.numClusters = 3;
    context->clusters = (aas_cluster_t *)calloc((size_t)aasworld.numClusters, sizeof(aas_cluster_t));
    assert_non_null(context->clusters);
    aasworld.clusters = context->clusters;
	context->clusters[0].numareas = 5;
	context->clusters[1].numareas = 7;
	context->clusters[2].numareas = 11;
    assert_int_equal(AAS_PointReachabilityAreaIndex(NULL), 23);

    context->areasettings[2].cluster = 1;
    context->areasettings[2].clusterareanum = 3;
    assert_int_equal(AAS_PointReachabilityAreaIndex(front_point), 8);

    aasworld.numPortals = 2;
    context->portals = (aas_portal_t *)calloc((size_t)aasworld.numPortals, sizeof(aas_portal_t));
    assert_non_null(context->portals);
    aasworld.portals = context->portals;
    context->portals[1].frontcluster = 2;
    context->portals[1].clusterareanum[0] = 4;
    context->areasettings[1].cluster = -1;
    assert_int_equal(AAS_PointReachabilityAreaIndex(back_point), 16);

    context->areasettings[1].contents |= AAS_AREACONTENTS_CLUSTERPORTAL;
    context->areasettings[2].contents |= AAS_AREACONTENTS_JUMPPAD;
    context->areasettings[2].areaflags |= AAS_AREA_GROUNDED;
    context->areasettings[2].areaflags |= AAS_AREA_LADDER;
    assert_true(AAS_AreaClusterPortal(1));
    assert_true(AAS_AreaJumpPad(2));
    assert_true(AAS_AreaGrounded(2));
    assert_true(AAS_AreaLadder(2));

    vec3_t face_point = { 100.0f, 0.0f, 0.0f };
    vec3_t outside_face = { 140.0f, 0.0f, 0.0f };
    assert_true(AAS_PointInsideFace(1, face_point, 0.01f));
    assert_false(AAS_PointInsideFace(1, outside_face, 0.01f));
    assert_ptr_equal(AAS_AreaGroundFace(2, face_point), &context->faces[1]);
    assert_null(AAS_AreaGroundFace(2, outside_face));

    vec3_t normal;
    float dist = -1.0f;
    AAS_FacePlane(1, normal, &dist);
    assert_float_equal(normal[2], 1.0f, 0.001f);
    assert_float_equal(dist, 0.0f, 0.001f);
    assert_float_equal(AAS_FaceArea(&context->faces[1]), 1600.0f, 0.001f);
    assert_float_equal(AAS_AreaGroundFaceArea(2), 1600.0f, 0.001f);
}

static void test_aas_predict_route_uses_reachability_cache_and_stop_events(void **state)
{
    aas_debug_test_context_t *context = (aas_debug_test_context_t *)(*state);
    Mock_Reset(context);
    ConfigureRoutePredictionFixture(context);

    vec3_t origin = { 0.0f, 0.0f, 0.0f };

    assert_int_equal(AAS_AreaReachabilityToGoalArea(1, origin, 3, TFL_DEFAULT), 1);
    assert_int_equal(AAS_AreaReachabilityToGoalArea(2, origin, 3, TFL_DEFAULT), 2);
    assert_int_equal(AAS_AreaTravelTimeToGoalArea(1, origin, 3, TFL_DEFAULT), 77);

    aas_predictroute_t route;
    assert_true(AAS_PredictRoute(&route,
                                 1,
                                 origin,
                                 3,
                                 TFL_DEFAULT,
                                 0,
                                 0,
                                 RSE_NONE,
                                 0,
                                 0,
                                 0));
    assert_int_equal(route.stopevent, RSE_NONE);
    assert_int_equal(route.endarea, 3);
    assert_int_equal(route.endtravelflags, TFL_JUMP);
    assert_int_equal(route.numareas, 2);
    assert_int_equal(route.time, 206);
    assert_float_equal(route.endpos[0], 200.0f, 0.001f);

    assert_int_equal(AAS_BridgeWalkable(2), qfalse);
    assert_int_equal(AAS_AreaVisible(1, 2), qfalse);

	/*
	 * Retail AAS_RandomGoalArea is sub_1001a410 and has no swim shortcut: the
	 * only candidate gate is "1001a473  if (j_sub_10011040(ebx) != 0 &&
	 * j_sub_10019fa0(arg1, ebx, arg2) u> 0)" - reachability plus a non-zero
	 * travel time. It then drops 300 units from the area centre
	 * ("1001a518  var_54_1 = eax_9 - 300f", presence type 4, passent -1),
	 * accepts on startsolid alone ("1001a53e  if (var_50 == 0)") with no
	 * fraction, containing-area or ground-face-area test, and reports the
	 * TRACED ENDPOINT's area, not the candidate ("1001a565  eax_15 =
	 * j_sub_1001ae60(&var_48); 1001a578  *arg3 = eax_15"). AAS_AREA_LIQUID is
	 * therefore irrelevant to the result.
	 *
	 * The start index at 1001a43f is rand()-driven, so the fixture has to give
	 * the same answer for every draw: park area 1's back leaf in solid, and
	 * its centre trace is startsolid so the 1001a53e gate rejects it. Area 3
	 * has no reachability and is skipped at 1001a473, which leaves area 2 as
	 * the only acceptable candidate whichever index the RNG hands out - hence
	 * the repeat loop, which pins that independence rather than one draw.
	 */
	int savedbackleaf = context->nodes[1].children[1];
	context->nodes[1].children[1] = 0;
	for (int draw = 0; draw < 32; ++draw)
	{
		int randomGoalArea = 0;
		vec3_t randomGoalOrigin;
		VectorClear(randomGoalOrigin);
		assert_true(AAS_RandomGoalArea(1,
			TFL_DEFAULT,
			&randomGoalArea,
			randomGoalOrigin));
		assert_int_equal(randomGoalArea, 2);
		assert_float_equal(randomGoalOrigin[0], 100.0f, 0.001f);
		/* The origin is the traced endpoint 300 units below the centre. */
		assert_float_equal(randomGoalOrigin[2], -300.0f, 0.001f);
	}
	context->nodes[1].children[1] = savedbackleaf;

    vec3_t enemyOrigin = { 200.0f, 80.0f, 0.0f };
    assert_int_equal(AAS_NearestHideArea(0, origin, 1, 0, enemyOrigin, 3, TFL_DEFAULT), 2);
    assert_int_equal(AAS_NearestHideArea(0, origin, 1, 0, enemyOrigin, 2, TFL_DEFAULT), 0);

    assert_true(AAS_PredictRoute(&route,
                                 1,
                                 origin,
                                 3,
                                 TFL_DEFAULT,
                                 0,
                                 0,
                                 RSE_USETRAVELTYPE,
                                 0,
                                 TFL_JUMP,
                                 0));
    assert_int_equal(route.stopevent, RSE_USETRAVELTYPE);
    assert_int_equal(route.endarea, 2);
    assert_int_equal(route.endtravelflags, TFL_JUMP);
    assert_int_equal(route.numareas, 1);
    assert_int_equal(route.time, 31);
    assert_float_equal(route.endpos[0], 100.0f, 0.001f);

    assert_true(AAS_PredictRoute(&route,
                                 1,
                                 origin,
                                 3,
                                 TFL_DEFAULT,
                                 0,
                                 0,
                                 RSE_ENTERAREA,
                                 0,
                                 0,
                                 3));
    assert_int_equal(route.stopevent, RSE_ENTERAREA);
    assert_int_equal(route.endarea, 3);
    assert_int_equal(route.numareas, 2);
    assert_float_equal(route.endpos[0], 100.0f, 0.001f);

    context->areasettings[3].contents = AAS_AREACONTENTS_LAVA;
    AAS_InitAreaContentsTravelFlags();
    AAS_InvalidateRouteCache();
    assert_true(AAS_PredictRoute(&route,
                                 1,
                                 origin,
                                 3,
                                 TFL_DEFAULT | TFL_LAVA,
                                 0,
                                 0,
                                 RSE_ENTERCONTENTS,
                                 AAS_AREACONTENTS_LAVA,
                                 0,
                                 0));
    assert_int_equal(route.stopevent, RSE_ENTERCONTENTS);
    assert_int_equal(route.endarea, 3);
    assert_int_equal(route.endcontents, AAS_AREACONTENTS_LAVA);
    assert_int_equal(route.time, 206);
    assert_float_equal(route.endpos[0], 200.0f, 0.001f);

    context->areasettings[3].contents = 0;
    AAS_InitAreaContentsTravelFlags();
    AAS_InvalidateRouteCache();
    assert_int_equal(AAS_EnableRoutingArea(2, 0), qtrue);
    assert_false(AAS_PredictRoute(&route,
                                  1,
                                  origin,
                                  3,
                                  TFL_DEFAULT,
                                  0,
                                  0,
                                  RSE_NONE,
                                  0,
                                  0,
                                  0));
    assert_int_equal(route.stopevent, RSE_NOROUTE);
    assert_int_equal(AAS_EnableRoutingArea(2, 1), qfalse);

	CleanupRoutePredictionFixture();
}

/*
=============
test_retail_alternative_route_reuses_map_scratch_and_face_order

Pin retail endpoint lookup, route-portal filtering, face flooding, and lifetime.
=============
*/
static void test_retail_alternative_route_reuses_map_scratch_and_face_order(
	void **state)
{
	aas_debug_test_context_t *context =
		(aas_debug_test_context_t *)(*state);
	Mock_Reset(context);
	ConfigureRoutePredictionFixture(context);
	ConfigureAlternativeRouteFaceFixture(context);

	aas_node_t *savednodes = aasworld.nodes;
	int savednumnodes = aasworld.numNodes;
	aasworld.nodes = NULL;
	aasworld.numNodes = 0;

	vec3_t start = { 0.0f, 0.0f, 0.0f };
	vec3_t goal = { 200.0f, 0.0f, 0.0f };
	aas_altroutegoal_t routegoals[4];

	for (int areanum = 1; areanum < aasworld.numAreas; ++areanum)
	{
		context->areasettings[areanum].contents = 0;
	}
	context->areasettings[1].contents = AAS_AREACONTENTS_ROUTEPORTAL;

	assert_int_equal(
		AAS_AreaTravelTimeToGoalArea(1, NULL, 3, TFL_DEFAULT),
		76);
	memset(routegoals, 0, sizeof(routegoals));
	assert_int_equal(AAS_AlternativeRouteGoals(start,
		3,
		goal,
		1,
		TFL_DEFAULT,
		routegoals,
		(int)ARRAY_LEN(routegoals),
		ALTROUTEGOAL_VIEWPORTALS),
		1);
	assert_int_equal(routegoals[0].areanum, 1);
	assert_int_equal(routegoals[0].starttraveltime, 1);
	assert_int_equal(routegoals[0].goaltraveltime, 76);
	assert_int_equal(routegoals[0].extratraveltime, 1);
	assert_non_null(Mock_FindPrint(context, "1 alternative route goals"));

	context->areasettings[2].contents = AAS_AREACONTENTS_ROUTEPORTAL;
	memset(routegoals, 0, sizeof(routegoals));
	assert_int_equal(AAS_AlternativeRouteGoals(start,
		0,
		goal,
		0,
		TFL_DEFAULT,
		routegoals,
		(int)ARRAY_LEN(routegoals),
		ALTROUTEGOAL_ALL),
		1);
	assert_int_equal(routegoals[0].areanum, 1);

	context->areasettings[3].contents = AAS_AREACONTENTS_ROUTEPORTAL;
	memset(routegoals, 0, sizeof(routegoals));
	assert_int_equal(AAS_AlternativeRouteGoals(start,
		1,
		goal,
		3,
		TFL_DEFAULT,
		routegoals,
		(int)ARRAY_LEN(routegoals),
		ALTROUTEGOAL_ALL),
		1);
	assert_int_equal(routegoals[0].areanum, 2);
	assert_float_equal(routegoals[0].origin[0], 100.0f, 0.001f);
	assert_int_equal(routegoals[0].starttraveltime, 30);
	assert_int_equal(routegoals[0].goaltraveltime, 45);
	assert_int_equal(routegoals[0].extratraveltime, UINT16_MAX);

	context->areasettings[2].contents = 0;
	memset(routegoals, 0, sizeof(routegoals));
	assert_int_equal(AAS_AlternativeRouteGoals(start,
		1,
		goal,
		3,
		TFL_DEFAULT,
		routegoals,
		(int)ARRAY_LEN(routegoals),
		ALTROUTEGOAL_ALL),
		2);
	assert_int_equal(routegoals[0].areanum, 1);
	assert_int_equal(routegoals[1].areanum, 3);

	context->areasettings[1].contents = 0;
	context->areasettings[2].contents = AAS_AREACONTENTS_ROUTEPORTAL;
	context->areasettings[3].contents = 0;
	AAS_InvalidateRouteCache();
	memset(routegoals, 0, sizeof(routegoals));
	assert_int_equal(AAS_AlternativeRouteGoals(start,
		1,
		goal,
		3,
		TFL_DEFAULT,
		routegoals,
		(int)ARRAY_LEN(routegoals),
		ALTROUTEGOAL_ALL),
		1);
	assert_int_equal(routegoals[0].areanum, 2);

	AAS_FreeAllRoutingCaches();
	assert_int_equal(AAS_AlternativeRouteGoals(start,
		1,
		goal,
		3,
		TFL_DEFAULT,
		routegoals,
		(int)ARRAY_LEN(routegoals),
		ALTROUTEGOAL_ALL),
		0);
	assert_true(AAS_InitRetailRoutingCaches());
	assert_int_equal(AAS_AlternativeRouteGoals(start,
		1,
		goal,
		3,
		TFL_DEFAULT,
		routegoals,
		(int)ARRAY_LEN(routegoals),
		ALTROUTEGOAL_ALL),
		1);

	aasworld.nodes = savednodes;
	aasworld.numNodes = savednumnodes;
	CleanupRoutePredictionFixture();
}

static void test_aas_predict_route_checks_generated_reachability_pass_areas(void **state)
{
    aas_debug_test_context_t *context = (aas_debug_test_context_t *)(*state);
    Mock_Reset(context);
    ConfigureRoutePassAreaFixture(context);

    vec3_t origin = { 0.0f, 0.0f, 0.0f };
    int travelFlags = TFL_DEFAULT | TFL_GRAPPLEHOOK;

    assert_int_equal(AAS_AreaReachabilityToGoalArea(1, origin, 3, travelFlags), 1);

    aas_predictroute_t route;
    assert_true(AAS_PredictRoute(&route,
                                 1,
                                 origin,
                                 3,
                                 travelFlags,
                                 0,
                                 0,
                                 RSE_NONE,
                                 0,
                                 0,
                                 0));
    assert_int_equal(route.stopevent, RSE_NONE);
    assert_int_equal(route.endarea, 3);
    assert_int_equal(route.endtravelflags, TFL_GRAPPLEHOOK);
    assert_int_equal(route.numareas, 1);
    assert_int_equal(route.time, 61);

    assert_true(AAS_PredictRoute(&route,
                                 1,
                                 origin,
                                 3,
                                 travelFlags,
                                 0,
                                 0,
                                 RSE_ENTERAREA,
                                 0,
                                 0,
                                 2));
    assert_int_equal(route.stopevent, RSE_ENTERAREA);
    assert_int_equal(route.endarea, 2);
    assert_int_equal(route.numareas, 1);
    assert_float_equal(route.endpos[0], 0.0f, 0.001f);

    context->areasettings[2].contents = AAS_AREACONTENTS_LAVA;
    AAS_InitAreaContentsTravelFlags();
    AAS_InvalidateRouteCache();

    assert_true(AAS_PredictRoute(&route,
                                 1,
                                 origin,
                                 3,
                                 travelFlags | TFL_LAVA,
                                 0,
                                 0,
                                 RSE_ENTERCONTENTS,
                                 AAS_AREACONTENTS_LAVA,
                                 0,
                                 0));
    assert_int_equal(route.stopevent, RSE_ENTERCONTENTS);
    assert_int_equal(route.endarea, 2);
    assert_int_equal(route.endcontents, AAS_AREACONTENTS_LAVA);
    assert_int_equal(route.time, 61);
    assert_float_equal(route.endpos[0], 100.0f, 0.001f);

    CleanupRoutePredictionFixture();
}

static void test_aas_trace_client_bbox_uses_presence_and_tree_hits(void **state)
{
    aas_debug_test_context_t *context = (aas_debug_test_context_t *)(*state);
    Mock_Reset(context);

    vec3_t start = { 0.0f, 0.0f, 0.0f };
    vec3_t end = { 100.0f, 0.0f, 0.0f };

    aas_trace_t pass = AAS_TraceClientBBox(start, end, PRESENCE_CROUCH, -1);
    assert_false(pass.startsolid);
    assert_float_equal(pass.fraction, 1.0f, 0.001f);
    assert_float_equal(pass.endpos[0], 100.0f, 0.001f);
    assert_int_equal(pass.area, 0);
    assert_int_equal(pass.lastarea, 2);

    aas_trace_t blocked_presence = AAS_TraceClientBBox(start, end, PRESENCE_NORMAL, -1);
    assert_true(blocked_presence.startsolid);
    assert_float_equal(blocked_presence.fraction, 0.0f, 0.001f);
    assert_float_equal(blocked_presence.endpos[0], 0.0f, 0.001f);
    assert_int_equal(blocked_presence.area, 1);

    int saved_front_child = context->nodes[1].children[0];
    context->nodes[1].children[0] = 0;
    aas_trace_t solid = AAS_TraceClientBBox(start, end, PRESENCE_CROUCH, -1);
    context->nodes[1].children[0] = saved_front_child;

    assert_false(solid.startsolid);
    assert_float_equal(solid.fraction, 0.49875f, 0.0001f);
    assert_float_equal(solid.endpos[0], 49.75f, 0.0001f);
    assert_int_equal(solid.area, 0);
    assert_int_equal(solid.lastarea, 1);
    assert_int_equal(solid.planenum, 1);

    aas_trace_t face_trace;
    memset(&face_trace, 0, sizeof(face_trace));
    face_trace.lastarea = 2;
    face_trace.planenum = 1;
    VectorSet(face_trace.endpos, 100.0f, 0.0f, 0.0f);
    assert_ptr_equal(AAS_TraceEndFace(&face_trace), &context->faces[1]);

    VectorSet(face_trace.endpos, 140.0f, 0.0f, 0.0f);
    assert_null(AAS_TraceEndFace(&face_trace));

    face_trace.startsolid = qtrue;
    VectorSet(face_trace.endpos, 100.0f, 0.0f, 0.0f);
    assert_null(AAS_TraceEndFace(&face_trace));
}

/*
=============
test_retail_client_bbox_trace_plane_band_clamp_and_presence_mask

Pin the 0x1001b3f3 plane band, 0x1001b497 clamp, and direct presence mask.
=============
*/
static void test_retail_client_bbox_trace_plane_band_clamp_and_presence_mask(
	void **state)
{
	aas_debug_test_context_t *context =
		(aas_debug_test_context_t *)(*state);
	Mock_Reset(context);

	vec3_t nearplane = {49.99975f, 0.0f, 0.0f};
	aas_trace_t band = AAS_TraceClientBBox(nearplane,
		nearplane,
		PRESENCE_CROUCH,
		-1);
	assert_false(band.startsolid);
	assert_float_equal(band.fraction, 1.0f, 0.00001f);
	assert_int_equal(band.lastarea, 2);

	vec3_t frontpoint = {100.0f, 0.0f, 0.0f};
	aas_trace_t presence = AAS_TraceClientBBox(frontpoint,
		frontpoint,
		PRESENCE_NONE,
		-1);
	assert_true(presence.startsolid);
	assert_float_equal(presence.fraction, 0.0f, 0.00001f);
	assert_float_equal(presence.endpos[0], frontpoint[0], 0.00001f);
	assert_int_equal(presence.area, 2);

	int savedbackchild = context->nodes[1].children[1];
	context->nodes[1].children[1] = 0;
	vec3_t clampstart = {50.01f, 0.0f, 0.0f};
	vec3_t clampend = {49.0f, 0.0f, 0.0f};
	aas_trace_t clamped = AAS_TraceClientBBox(clampstart,
		clampend,
		PRESENCE_CROUCH,
		-1);
	context->nodes[1].children[1] = savedbackchild;

	assert_true(clamped.startsolid);
	assert_float_equal(clamped.fraction, 0.0f, 0.00001f);
	assert_float_equal(clamped.endpos[0], clampstart[0], 0.00001f);
	assert_int_equal(clamped.lastarea, 2);
	assert_int_equal(clamped.area, 0);
	assert_int_equal(clamped.planenum, 0);
}

/*
=============
test_retail_client_bbox_trace_preserves_link_order_and_pass_boundary

Verify equal entity hits retain list-head order and negative passent skips all.
=============
*/
static void test_retail_client_bbox_trace_preserves_link_order_and_pass_boundary(
	void **state)
{
	aas_debug_test_context_t *context =
		(aas_debug_test_context_t *)(*state);
	Mock_Reset(context);

	AASEntityFrame entity;
	memset(&entity, 0, sizeof(entity));
	entity.solid = SOLID_BBOX;
	entity.bounds_dirty = true;
	entity.origin_dirty = true;
	VectorSet(entity.origin, 75.0f, 0.0f, 0.0f);
	VectorSet(entity.previous_origin, 75.0f, 0.0f, 0.0f);
	VectorSet(entity.mins, -4.0f, -8.0f, -8.0f);
	VectorSet(entity.maxs, 4.0f, 8.0f, 8.0f);

	assert_int_equal(AAS_UpdateEntity(4, &entity), BLERR_NOERROR);
	assert_int_equal(AAS_UpdateEntity(5, &entity), BLERR_NOERROR);
	assert_non_null(aasworld.areaEntityLists[2]);
	assert_int_equal(aasworld.areaEntityLists[2]->entnum, 5);

	vec3_t start = {0.0f, 0.0f, 0.0f};
	vec3_t end = {100.0f, 0.0f, 0.0f};
	/*
	 * Retail sub_1000dda0 stores the presence boxes as two 3-entry stack
	 * tables: mins at 1000ddc2 are 0xC1800000/0xC1800000/0xC1C00000 and maxs
	 * at 1000de0a are 0x41800000/0x41800000/0x42000000 - i.e. the Quake II
	 * 32x32 player, half extent 16, not Q3's 15. The swept entity box is
	 * therefore x in [71 - 16, 79 + 16], so a 100-unit trace from the origin
	 * first touches it at x = 55 and the fraction is 0.55, not 0.56.
	 */
	aas_trace_t headhit = AAS_TraceClientBBox(start,
		end,
		PRESENCE_CROUCH,
		99);
	assert_int_equal(headhit.ent, 5);
	assert_float_equal(headhit.fraction, 0.55f, 0.001f);
	assert_int_equal(headhit.lastarea, 1);
	assert_int_equal(headhit.area, 0);
	assert_int_equal(headhit.planenum, 0);

	aas_trace_t passedhead = AAS_TraceClientBBox(start,
		end,
		PRESENCE_CROUCH,
		5);
	assert_int_equal(passedhead.ent, 4);
	assert_float_equal(passedhead.fraction, 0.55f, 0.001f);

	aas_trace_t disabled = AAS_TraceClientBBox(start,
		end,
		PRESENCE_CROUCH,
		-1);
	assert_int_equal(disabled.ent, 0);
	assert_float_equal(disabled.fraction, 1.0f, 0.00001f);
	assert_int_equal(disabled.lastarea, 2);

	assert_int_equal(AAS_UpdateEntity(4, NULL), BLERR_NOERROR);
	assert_int_equal(AAS_UpdateEntity(5, NULL), BLERR_NOERROR);
}

/*
=============
test_retail_client_bbox_trace_guards_64_entry_stack_geometry

Pin the retail 0x800-byte stack geometry while retaining a safe overflow exit.
=============
*/
static void test_retail_client_bbox_trace_guards_64_entry_stack_geometry(
	void **state)
{
	aas_debug_test_context_t *context =
		(aas_debug_test_context_t *)(*state);
	Mock_Reset(context);

	free(context->planes);
	aasworld.numPlanes = 64;
	context->planes = (aas_plane_t *)calloc(
		(size_t)aasworld.numPlanes,
		sizeof(*context->planes));
	assert_non_null(context->planes);
	aasworld.planes = context->planes;

	free(context->nodes);
	aasworld.numNodes = 65;
	context->nodes = (aas_node_t *)calloc(
		(size_t)aasworld.numNodes,
		sizeof(*context->nodes));
	assert_non_null(context->nodes);
	aasworld.nodes = context->nodes;

	for (int nodenum = 1; nodenum < aasworld.numNodes; ++nodenum)
	{
		int planenum = nodenum - 1;
		VectorSet(context->planes[planenum].normal, 1.0f, 0.0f, 0.0f);
		context->planes[planenum].dist = 100.0f - (float)nodenum;
		context->nodes[nodenum].planenum = planenum;
		context->nodes[nodenum].children[0] = -1;
		context->nodes[nodenum].children[1] =
			(nodenum + 1 < aasworld.numNodes) ? nodenum + 1 : -1;
	}

	vec3_t start = {0.0f, 0.0f, 0.0f};
	vec3_t end = {100.0f, 0.0f, 0.0f};
	aas_trace_t trace = AAS_TraceClientBBox(start,
		end,
		PRESENCE_CROUCH,
		-1);
	assert_false(trace.startsolid);
	assert_float_equal(trace.fraction, 0.0f, 0.00001f);
	assert_int_equal(trace.lastarea, 0);
	assert_non_null(Mock_FindPrint(context,
		"AAS_TraceBoundingBox: stack overflow"));
}

static void test_aas_trace_client_bbox_hits_linked_entities(void **state)
{
    aas_debug_test_context_t *context = (aas_debug_test_context_t *)(*state);
    Mock_Reset(context);

    AASEntityFrame entity;
    memset(&entity, 0, sizeof(entity));
    entity.solid = SOLID_BBOX;
    entity.modelindex = 1;
    entity.bounds_dirty = true;
    entity.origin_dirty = true;
    VectorSet(entity.origin, 75.0f, 0.0f, 0.0f);
    VectorSet(entity.previous_origin, 75.0f, 0.0f, 0.0f);
    VectorSet(entity.mins, -4.0f, -8.0f, -8.0f);
    VectorSet(entity.maxs, 4.0f, 8.0f, 8.0f);

	size_t tracked_before = BotMemory_TotalAllocated();
    assert_int_equal(AAS_UpdateEntity(4, &entity), BLERR_NOERROR);
    assert_true(BotMemory_TotalAllocated() > tracked_before);
    assert_non_null(aasworld.entities);
    assert_true(aasworld.areaEntityListCount > 2U);
    assert_non_null(aasworld.areaEntityLists[2]);

    vec3_t start = { 0.0f, 0.0f, 0.0f };
    vec3_t end = { 100.0f, 0.0f, 0.0f };
    vec3_t boxmins;
    vec3_t boxmaxs;
	/*
	 * Retail sub_1000dda0 uses half extent 16 on X and Y (0xC1800000 /
	 * 0x41800000 in the tables at 1000ddc2 and 1000de0a), so every sweep
	 * below expands the entity box [71,79] out to [55,95): fraction 0.55 /
	 * endpos 55 forward, 0.05 / endpos 95 reversed, and exp_dist - the swept
	 * extent sub_10003680 adds back into cplane.dist - is 16.
	 */
    AAS_PresenceTypeBoundingBox(PRESENCE_CROUCH, boxmins, boxmaxs);
	assert_float_equal(boxmins[0], -16.0f, 0.001f);
	assert_float_equal(boxmaxs[0], 16.0f, 0.001f);

    bsp_trace_t entity_trace;
    memset(&entity_trace, 0, sizeof(entity_trace));
    entity_trace.fraction = 1.0f;
    assert_true(AAS_EntityCollision(4,
                                    start,
                                    boxmins,
                                    boxmaxs,
                                    end,
                                    CONTENTS_SOLID | CONTENTS_PLAYERCLIP,
                                    &entity_trace));
    assert_false(entity_trace.startsolid);
    assert_float_equal(entity_trace.fraction, 0.55f, 0.001f);
    assert_float_equal(entity_trace.endpos[0], 55.0f, 0.001f);
    assert_float_equal(entity_trace.plane.normal[0], -1.0f, 0.001f);
	assert_float_equal(entity_trace.plane.dist, -71.0f, 0.001f);
	assert_int_equal(entity_trace.plane.type, 0);
	assert_int_equal(entity_trace.plane.signbits, 0);
	assert_float_equal(entity_trace.exp_dist, 16.0f, 0.001f);
	assert_int_equal(entity_trace.sidenum, -1);
	assert_int_equal(entity_trace.contents, 0);
    assert_int_equal(entity_trace.ent, 4);

	bsp_trace_t reverse_entity_trace;
	memset(&reverse_entity_trace, 0, sizeof(reverse_entity_trace));
	reverse_entity_trace.fraction = 1.0f;
	assert_true(AAS_EntityCollision(4,
	                                end,
	                                boxmins,
	                                boxmaxs,
	                                start,
	                                CONTENTS_SOLID | CONTENTS_PLAYERCLIP,
	                                &reverse_entity_trace));
	assert_false(reverse_entity_trace.startsolid);
	assert_float_equal(reverse_entity_trace.fraction, 0.05f, 0.001f);
	assert_float_equal(reverse_entity_trace.endpos[0], 95.0f, 0.001f);
	assert_float_equal(reverse_entity_trace.plane.normal[0], 1.0f, 0.001f);
	assert_float_equal(reverse_entity_trace.plane.dist, 79.0f, 0.001f);
	assert_int_equal(reverse_entity_trace.plane.signbits, 0);
	assert_float_equal(reverse_entity_trace.exp_dist, 16.0f, 0.001f);
	assert_int_equal(reverse_entity_trace.sidenum, -1);
	assert_int_equal(reverse_entity_trace.contents, 0);
	assert_int_equal(reverse_entity_trace.ent, 4);

    aas_trace_t hit = AAS_TraceClientBBox(start, end, PRESENCE_CROUCH, 99);
    assert_false(hit.startsolid);
    assert_float_equal(hit.fraction, 0.55f, 0.001f);
    assert_float_equal(hit.endpos[0], 55.0f, 0.001f);
    assert_float_equal(hit.plane.normal[0], -1.0f, 0.001f);
    assert_int_equal(hit.ent, 4);
    assert_int_equal(hit.area, 0);
    assert_int_equal(hit.lastarea, 1);

    aas_trace_t passentity = AAS_TraceClientBBox(start, end, PRESENCE_CROUCH, 4);
    assert_false(passentity.startsolid);
    assert_float_equal(passentity.fraction, 1.0f, 0.001f);
    assert_int_equal(passentity.ent, 0);
    assert_int_equal(passentity.lastarea, 2);

    vec3_t inside = { 60.0f, 0.0f, 0.0f };

	bsp_trace_t inside_entity_trace;
	memset(&inside_entity_trace, 0, sizeof(inside_entity_trace));
	inside_entity_trace.fraction = 1.0f;
	assert_true(AAS_EntityCollision(4,
	                                inside,
	                                boxmins,
	                                boxmaxs,
	                                end,
	                                CONTENTS_SOLID | CONTENTS_PLAYERCLIP,
	                                &inside_entity_trace));
	assert_true(inside_entity_trace.startsolid);
	assert_true(inside_entity_trace.allsolid);
	assert_float_equal(inside_entity_trace.fraction, 0.0f, 0.001f);
	assert_float_equal(inside_entity_trace.endpos[0], 60.0f, 0.001f);
	assert_int_equal(inside_entity_trace.sidenum, -1);
	assert_int_equal(inside_entity_trace.contents, 0);

	/*
	 * The retail SOLID_BBOX start-inside writer replaces an existing trace,
	 * even when that trace is already a zero-fraction startsolid result.
	 */
	bsp_trace_t bbox_priority_trace;
	memset(&bbox_priority_trace, 0, sizeof(bbox_priority_trace));
	bbox_priority_trace.fraction = 0.0f;
	bbox_priority_trace.startsolid = qtrue;
	bbox_priority_trace.ent = 99;
	assert_true(AAS_EntityCollision(4,
	                                inside,
	                                boxmins,
	                                boxmaxs,
	                                end,
	                                CONTENTS_SOLID | CONTENTS_PLAYERCLIP,
	                                &bbox_priority_trace));
	assert_true(bbox_priority_trace.startsolid);
	assert_true(bbox_priority_trace.allsolid);
	assert_int_equal(bbox_priority_trace.ent, 4);

	vec3_t inside_margin = { 94.25f, 0.0f, 0.0f };
	bsp_trace_t margin_entity_trace;
	memset(&margin_entity_trace, 0, sizeof(margin_entity_trace));
	margin_entity_trace.fraction = 1.0f;
	assert_true(AAS_EntityCollision(4,
	                                inside_margin,
	                                boxmins,
	                                boxmaxs,
	                                end,
	                                CONTENTS_SOLID | CONTENTS_PLAYERCLIP,
	                                &margin_entity_trace));
	assert_true(margin_entity_trace.startsolid);
	assert_true(margin_entity_trace.allsolid);
	assert_float_equal(margin_entity_trace.fraction, 0.0f, 0.001f);
	assert_float_equal(margin_entity_trace.endpos[0], 94.25f, 0.001f);

    aas_trace_t startsolid = AAS_TraceClientBBox(inside, end, PRESENCE_CROUCH, 99);
    assert_true(startsolid.startsolid);
    assert_float_equal(startsolid.fraction, 0.0f, 0.001f);
    assert_float_equal(startsolid.endpos[0], 60.0f, 0.001f);
    assert_int_equal(startsolid.ent, 4);

    AASEntityFrame mover;
    memset(&mover, 0, sizeof(mover));
    mover.solid = SOLID_BSP;
    mover.modelindex = 8;
    mover.modelindex2 = 9;
    mover.modelindex3 = 10;
    mover.modelindex4 = 11;
    mover.frame = 12;
    mover.skinnum = 13;
    mover.effects = 14;
    mover.renderfx = 15;
    mover.sound = 16;
    mover.event_id = 17;
    mover.last_update_time = 4.5f;
    mover.frame_delta = 0.125f;
    mover.bounds_dirty = true;
    mover.origin_dirty = true;
    mover.is_mover = true;
    VectorSet(mover.origin, 25.0f, 80.0f, 4.0f);
    VectorSet(mover.previous_origin, 20.0f, 80.0f, 4.0f);
    VectorSet(mover.old_origin, 20.0f, 80.0f, 4.0f);
    VectorSet(mover.angles, 0.0f, 90.0f, 0.0f);
    VectorSet(mover.mins, -16.0f, -16.0f, 0.0f);
    VectorSet(mover.maxs, 16.0f, 16.0f, 32.0f);

    assert_int_equal(AAS_UpdateEntity(5, &mover), BLERR_NOERROR);

    aas_entityinfo_t info;
    memset(&info, 0, sizeof(info));
    AAS_EntityInfo(5, &info);
    assert_true(info.valid);
    assert_int_equal(info.type, AAS_ENTITYTYPE_MOVER);
    assert_int_equal(info.number, 5);
    assert_int_equal(info.solid, SOLID_BSP);
    assert_int_equal(info.modelindex, 8);
    assert_int_equal(info.modelindex2, 9);
    assert_int_equal(info.modelindex3, 10);
    assert_int_equal(info.modelindex4, 11);
    assert_int_equal(info.frame, 12);
    assert_int_equal(info.skinnum, 13);
    assert_int_equal(info.eventid, 17);
    assert_float_equal(info.origin[0], 25.0f, 0.001f);
    assert_float_equal(info.lastvisorigin[0], 20.0f, 0.001f);
    assert_float_equal(info.ltime, 4.5f, 0.001f);
    assert_float_equal(info.update_time, 0.125f, 0.001f);

    vec3_t mover_origin;
    VectorClear(mover_origin);
    AAS_EntityOrigin(5, mover_origin);
    assert_float_equal(mover_origin[1], 80.0f, 0.001f);
    assert_int_equal(AAS_EntityModelindex(5), 8);
	assert_int_equal(AAS_EntityRenderFX(5), 15);
	/* sub_1000ade0 exposes the stored one-based index as a zero-based model. */
	assert_int_equal(AAS_EntityModelNum(5), 7);
    assert_int_equal(AAS_ModelNumForEntity(5), 7);

    vec3_t mover_mins;
    vec3_t mover_maxs;
    AAS_EntitySize(5, mover_mins, mover_maxs);
    assert_float_equal(mover_mins[0], -16.0f, 0.001f);
    assert_float_equal(mover_maxs[2], 32.0f, 0.001f);

    VectorClear(mover_origin);
    assert_true(AAS_OriginOfMoverWithModelNum(7, mover_origin));
    assert_float_equal(mover_origin[0], 25.0f, 0.001f);
	/* sub_1000ae30 scans raw entity slots, without live or mover filtering. */
	aasworld.entities[5].inuse = qfalse;
	aasworld.entities[5].isMover = qfalse;
	VectorClear(mover_origin);
	assert_true(AAS_OriginOfMoverWithModelNum(7, mover_origin));
	assert_float_equal(mover_origin[0], 25.0f, 0.001f);
	vec3_t nearest_origin = {25.0f, 80.0f, 4.0f};
	assert_int_equal(AAS_NearestEntity(nearest_origin, 8), 5);
	assert_int_equal(AAS_NearestEntity(nearest_origin, 99), 0);
    assert_false(AAS_OriginOfMoverWithModelNum(99, mover_origin));

    assert_int_equal(AAS_UpdateEntity(5, NULL), BLERR_NOERROR);
    assert_int_equal(AAS_UpdateEntity(4, NULL), BLERR_NOERROR);
    FreeMemory(aasworld.entities);
    FreeMemory(aasworld.areaEntityLists);
	assert_int_equal(BotMemory_TotalAllocated(), tracked_before);
    aasworld.entities = NULL;
    aasworld.maxEntities = 0;
    aasworld.areaEntityLists = NULL;
    aasworld.areaEntityListCount = 0U;
}

static void test_aas_bsp_model_bounds_and_entity_collision_use_brush_lumps(void **state)
{
	aas_debug_test_context_t *context = (aas_debug_test_context_t *)(*state);
	Mock_Reset(context);
	vec3_t unavailable_mins = { 11.0f, 12.0f, 13.0f };
	vec3_t unavailable_maxs = { 21.0f, 22.0f, 23.0f };
	vec3_t unavailable_origin = { 31.0f, 32.0f, 33.0f };
	vec3_t unavailable_angles = { 0.0f, 0.0f, 0.0f };
	/* Retail sub_10005e60 leaves all outputs untouched before a BSP load. */
	AAS_BSPModelMinsMaxsOrigin(0,
							 unavailable_angles,
							 unavailable_mins,
							 unavailable_maxs,
							 unavailable_origin);
	assert_float_equal(unavailable_mins[0], 11.0f, 0.001f);
	assert_float_equal(unavailable_maxs[1], 22.0f, 0.001f);
	assert_float_equal(unavailable_origin[2], 33.0f, 0.001f);
	ConfigureBSPBrushModelFixture(context);

	vec3_t zero;
	VectorClear(zero);

	vec3_t mins;
	vec3_t maxs;
	vec3_t origin;
	AAS_BSPModelMinsMaxsOrigin(1, zero, mins, maxs, origin);
	assert_float_equal(mins[0], -4.0f, 0.001f);
	assert_float_equal(mins[1], -12.0f, 0.001f);
	assert_float_equal(maxs[0], 4.0f, 0.001f);
	assert_float_equal(maxs[1], 12.0f, 0.001f);
	assert_float_equal(origin[0], 0.0f, 0.001f);

	vec3_t yaw90 = { 0.0f, 90.0f, 0.0f };
	AAS_BSPModelMinsMaxsOrigin(1, yaw90, mins, maxs, origin);
	assert_float_equal(mins[0], -12.0f, 0.001f);
	assert_float_equal(maxs[0], 12.0f, 0.001f);
	assert_float_equal(mins[1], -4.0f, 0.001f);
	assert_float_equal(maxs[1], 4.0f, 0.001f);

	vec3_t min_only = { 100.0f, 100.0f, 100.0f };
	vec3_t max_only = { 200.0f, 200.0f, 200.0f };
	/* Retail sub_10005e60 computes either optional rotated-bound output alone. */
	AAS_BSPModelMinsMaxsOrigin(1, yaw90, min_only, NULL, NULL);
	AAS_BSPModelMinsMaxsOrigin(1, yaw90, NULL, max_only, NULL);
	assert_float_equal(min_only[0], -12.0f, 0.001f);
	assert_float_equal(min_only[1], -4.0f, 0.001f);
	assert_float_equal(max_only[0], 12.0f, 0.001f);
	assert_float_equal(max_only[1], 4.0f, 0.001f);

	vec3_t start = { -40.0f, 0.0f, 0.0f };
	vec3_t end = { 40.0f, 0.0f, 0.0f };
	bsp_trace_t model_trace = AAS_TraceBSPModel(1,
	                                            zero,
	                                            zero,
	                                            start,
	                                            zero,
	                                            zero,
	                                            end,
	                                            CONTENTS_SOLID);
	assert_false(model_trace.startsolid);
	assert_float_equal(model_trace.fraction, 0.3749375f, 0.0001f);
	assert_float_equal(model_trace.endpos[0], -10.005f, 0.001f);
	assert_float_equal(model_trace.plane.normal[0], -1.0f, 0.001f);
	assert_int_equal(model_trace.plane.signbits, 0);
	assert_int_equal(model_trace.contents, CONTENTS_SOLID);

	/*
	 * Collision uses a transformed world plane, but sub_10004310 copies the
	 * selected model-local cplane back out of the BSP plane table unchanged.
	 */
	vec3_t rotated_start = { 0.0f, -40.0f, 0.0f };
	vec3_t rotated_end = { 0.0f, 40.0f, 0.0f };
	bsp_trace_t rotated_model_trace = AAS_TraceBSPModel(1,
		                                                   yaw90,
		                                                   zero,
		                                                   rotated_start,
		                                                   zero,
		                                                   zero,
		                                                   rotated_end,
		                                                   CONTENTS_SOLID);
	assert_false(rotated_model_trace.startsolid);
	assert_float_equal(rotated_model_trace.fraction, 0.3749375f, 0.0001f);
	assert_float_equal(rotated_model_trace.endpos[1], -10.005f, 0.001f);
	assert_float_equal(rotated_model_trace.plane.normal[0], -1.0f, 0.001f);
	assert_float_equal(rotated_model_trace.plane.normal[1], 0.0f, 0.001f);
	assert_int_equal(rotated_model_trace.plane.signbits, 0);

	/*
	 * sub_100044f0 rotates each model plane into world space before selecting
	 * the axial trace box's support extent. Transforming endpoints alone would
	 * incorrectly apply the world X extent as local X here and hit at -7.005.
	 */
	vec3_t asymmetric_mins = { -3.0f, -1.0f, 0.0f };
	vec3_t asymmetric_maxs = { 3.0f, 1.0f, 0.0f };
	bsp_trace_t asymmetric_rotated_trace = AAS_TraceBSPModel(1,
		                                                     yaw90,
		                                                     zero,
		                                                     rotated_start,
		                                                     asymmetric_mins,
		                                                     asymmetric_maxs,
		                                                     rotated_end,
		                                                     CONTENTS_SOLID);
	assert_false(asymmetric_rotated_trace.startsolid);
	assert_float_equal(asymmetric_rotated_trace.fraction, 0.3874375f, 0.0001f);
	assert_float_equal(asymmetric_rotated_trace.endpos[1], -9.005f, 0.001f);
	assert_float_equal(asymmetric_rotated_trace.plane.normal[0], -1.0f, 0.001f);

	/*
	 * The same raw cplane copy is retained for a translated, unrotated model:
	 * collision occurs at the world-space offset, but the reported distance is
	 * still the selected local BSP plane's 10.0 value.
	 */
	vec3_t translated_origin = { 25.0f, 0.0f, 0.0f };
	vec3_t translated_start = { 0.0f, 0.0f, 0.0f };
	vec3_t translated_end = { 60.0f, 0.0f, 0.0f };
	bsp_trace_t translated_model_trace = AAS_TraceBSPModel(1,
		                                                    zero,
		                                                    translated_origin,
		                                                    translated_start,
		                                                    zero,
		                                                    zero,
		                                                    translated_end,
		                                                    CONTENTS_SOLID);
	assert_false(translated_model_trace.startsolid);
	assert_float_equal(translated_model_trace.fraction, 0.2499167f, 0.0001f);
	assert_float_equal(translated_model_trace.endpos[0], 14.995f, 0.001f);
	assert_float_equal(translated_model_trace.plane.normal[0], -1.0f, 0.001f);
	assert_float_equal(translated_model_trace.plane.dist, 10.0f, 0.001f);

	/*
	 * The retail BSP walker treats a start 0.003 units in front of a split
	 * plane as back-side-only when its endpoint is behind that plane.  It must
	 * not visit the front leaf's brush just because the segment crosses zero.
	 */
	context->bspNodes = (aas_bspnode_t *)calloc(1U, sizeof(aas_bspnode_t));
	assert_non_null(context->bspNodes);
	aasworld.bspNodes = context->bspNodes;
	aasworld.numBspNodes = 1;
	context->bspNodes[0].planenum = 0;
	context->bspNodes[0].children[0] = -2;
	context->bspNodes[0].children[1] = -1;
	context->bspModels[1].headnode = 0;
	vec3_t near_plane_start = {10.003f, 0.0f, 0.0f};
	vec3_t near_plane_end = {-20.0f, 0.0f, 0.0f};
	bsp_trace_t near_plane_trace = AAS_TraceBSPModel(1,
		zero,
		zero,
		near_plane_start,
		zero,
		zero,
		near_plane_end,
		CONTENTS_SOLID);
	assert_false(near_plane_trace.startsolid);
	assert_float_equal(near_plane_trace.fraction, 1.0f, 0.0001f);
	assert_float_equal(near_plane_trace.endpos[0], -20.0f, 0.001f);
	assert_int_equal(near_plane_trace.contents, 0);

	/*
	 * The front-child fast path is strictly greater than -0.005.  At the
	 * exact negative boundary retail instead routes to child 1, leaving this
	 * leaf-0 brush untouched.
	 */
	vec3_t exact_negative_epsilon = {
		context->bspPlanes[0].dist - 0.005f, 0.0f, 0.0f
	};
	bsp_trace_t exact_negative_epsilon_trace = AAS_TraceBSPModel(1,
		zero,
		zero,
		exact_negative_epsilon,
		zero,
		zero,
		exact_negative_epsilon,
		CONTENTS_SOLID);
	assert_false(exact_negative_epsilon_trace.startsolid);
	assert_float_equal(exact_negative_epsilon_trace.fraction, 1.0f, 0.0001f);
	assert_int_equal(exact_negative_epsilon_trace.contents, 0);
	context->bspModels[1].headnode = -2;
	free(context->bspNodes);
	context->bspNodes = NULL;
	aasworld.bspNodes = NULL;
	aasworld.numBspNodes = 0;

	vec3_t inside_model = { 0.0f, 0.0f, 0.0f };
	bsp_trace_t inside_model_trace = AAS_TraceBSPModel(1,
	                                                   zero,
	                                                   zero,
	                                                   inside_model,
	                                                   zero,
	                                                   zero,
	                                                   end,
	                                                   CONTENTS_SOLID);
	assert_true(inside_model_trace.startsolid);
	assert_true(inside_model_trace.allsolid);
	assert_float_equal(inside_model_trace.fraction, 0.0f, 0.001f);
	assert_float_equal(inside_model_trace.endpos[0], 0.0f, 0.001f);

	/*
	 * A nested SOLID_BSP start-inside trace is not a priority override: the
	 * raw outer collision path copies it only when its fraction is strictly
	 * lower than the caller's current result.
	 */
	bsp_trace_t model_priority_trace;
	memset(&model_priority_trace, 0, sizeof(model_priority_trace));
	model_priority_trace.fraction = 0.0f;
	model_priority_trace.startsolid = qtrue;
	model_priority_trace.ent = 99;
	assert_false(AAS_EntityCollision(6,
	                                 inside_model,
	                                 zero,
	                                 zero,
	                                 end,
	                                 CONTENTS_SOLID,
	                                 &model_priority_trace));
	assert_true(model_priority_trace.startsolid);
	assert_int_equal(model_priority_trace.ent, 99);

	AASEntityFrame mover;
	memset(&mover, 0, sizeof(mover));
	mover.solid = SOLID_BSP;
	mover.modelindex = 2;
	mover.bounds_dirty = true;
	mover.origin_dirty = true;
	VectorSet(mover.origin, 25.0f, 0.0f, 0.0f);
	VectorSet(mover.previous_origin, 25.0f, 0.0f, 0.0f);
	VectorSet(mover.mins, -10.0f, -10.0f, -10.0f);
	VectorSet(mover.maxs, 10.0f, 10.0f, 10.0f);
	assert_int_equal(AAS_UpdateEntity(6, &mover), BLERR_NOERROR);

	vec3_t entity_start = { 0.0f, 0.0f, 0.0f };
	vec3_t entity_end = { 60.0f, 0.0f, 0.0f };
	bsp_trace_t entity_trace;
	memset(&entity_trace, 0, sizeof(entity_trace));
	entity_trace.fraction = 1.0f;
	assert_true(AAS_EntityCollision(6,
	                                entity_start,
	                                zero,
	                                zero,
	                                entity_end,
	                                CONTENTS_SOLID,
	                                &entity_trace));
	assert_false(entity_trace.startsolid);
	assert_float_equal(entity_trace.fraction, 0.2499167f, 0.0001f);
	assert_float_equal(entity_trace.endpos[0], 14.995f, 0.001f);
	assert_float_equal(entity_trace.plane.normal[0], -1.0f, 0.001f);
	assert_int_equal(entity_trace.ent, 6);
	assert_int_equal(context->bridge_trace_count, 0);

	/*
	 * The DLL's own contents walk is sub_100057a0, entered through
	 * sub_10005a10 ("10005a44  return j_sub_100057a0(arg1, 0, &var_c,
	 * &var_c)" - world model, zero origin, zero angles). Retail keeps that
	 * code but nothing reaches it: AAS_PointContents (sub_10003080) tail-calls
	 * the bot_import slot instead, so the walk is exercised directly here.
	 * AAS_PointContents itself must stay a pure delegation - one import call
	 * per invocation, engine result verbatim - and must not consult the loaded
	 * brush lumps at all.
	 */
	vec3_t mover_contents_point = { 25.0f, 0.0f, 0.0f };
	assert_int_equal(AAS_BSPModelPointContents(mover_contents_point,
			0,
			NULL,
			NULL,
			qtrue),
		CONTENTS_SOLID);
	context->bridge_point_contents_result = CONTENTS_LAVA;
	assert_int_equal(AAS_PointContents(mover_contents_point), CONTENTS_LAVA);
	assert_int_equal(context->bridge_point_contents_count, 1);
	context->bridge_point_contents_result = 0;
	context->bridge_point_contents_count = 0;

	AASEntityFrame bbox;
	memset(&bbox, 0, sizeof(bbox));
	bbox.solid = SOLID_BBOX;
	bbox.bounds_dirty = true;
	bbox.origin_dirty = true;
	VectorSet(bbox.origin, 50.0f, 0.0f, 0.0f);
	VectorSet(bbox.previous_origin, 50.0f, 0.0f, 0.0f);
	VectorSet(bbox.mins, -2.0f, -2.0f, -2.0f);
	VectorSet(bbox.maxs, 2.0f, 2.0f, 2.0f);
	assert_int_equal(AAS_UpdateEntity(7, &bbox), BLERR_NOERROR);
	vec3_t bbox_contents_point = { 50.0f, 0.0f, 0.0f };
	assert_int_equal(AAS_BSPModelPointContents(bbox_contents_point,
			0,
			NULL,
			NULL,
			qtrue),
		CONTENTS_MONSTER);

	/*
	 * The BBOX path in sub_10003680 does not inspect the content mask; unlike
	 * a SOLID_BSP brush trace it remains a collision even when that mask is 0.
	 */
	bsp_trace_t bbox_mask_zero_trace;
	memset(&bbox_mask_zero_trace, 0, sizeof(bbox_mask_zero_trace));
	bbox_mask_zero_trace.fraction = 1.0f;
	vec3_t bbox_trace_start = { 40.0f, 0.0f, 0.0f };
	vec3_t bbox_trace_end = { 60.0f, 0.0f, 0.0f };
	assert_true(AAS_EntityCollision(7,
		bbox_trace_start,
		zero,
		zero,
		bbox_trace_end,
		0,
		&bbox_mask_zero_trace));
	assert_float_equal(bbox_mask_zero_trace.fraction, 0.4f, 0.0001f);
	assert_int_equal(bbox_mask_zero_trace.ent, 7);
	assert_int_equal(bbox_mask_zero_trace.contents, 0);

	context->bspPlanes[1].dist = 0.0f;
	mover.angles_dirty = true;
	VectorSet(mover.angles, 0.0f, 90.0f, 0.0f);
	assert_int_equal(AAS_UpdateEntity(6, &mover), BLERR_NOERROR);
	/*
	 * sub_100057a0 applies the -angles matrix directly. With yaw +90 this
	 * maps the +Y world offset to the brush's positive local X half-space;
	 * using a transpose of that already-inverted matrix would choose -X.
	 */
	vec3_t rotated_mover_contents_point = { 25.0f, 5.0f, 0.0f };
	assert_int_equal(AAS_BSPModelPointContents(rotated_mover_contents_point,
			0,
			NULL,
			NULL,
			qtrue),
		CONTENTS_SOLID);

	assert_int_equal(AAS_UpdateEntity(6, NULL), BLERR_NOERROR);
	assert_int_equal(AAS_UpdateEntity(7, NULL), BLERR_NOERROR);
	FreeMemory(aasworld.entities);
	FreeMemory(aasworld.areaEntityLists);
	free(aasworld.bspLeafEntityLists);
	aasworld.entities = NULL;
	aasworld.maxEntities = 0;
	aasworld.areaEntityLists = NULL;
	aasworld.areaEntityListCount = 0U;
	aasworld.bspLeafEntityLists = NULL;
	aasworld.bspLeafEntityListCount = 0U;
}

/*
=============
test_retail_route_cache_layout_tables_and_lookup

Verify the x86 header layout, contiguous head tables, and exact list lookup.
=============
*/
static void test_retail_route_cache_layout_tables_and_lookup(void **state)
{
	aas_debug_test_context_t *context = (aas_debug_test_context_t *)(*state);
	ConfigureRetailRouteCacheFixture(context);

	assert_int_equal(sizeof(aas_retailroutingcache32_t), 0x2c);
	assert_int_equal(offsetof(aas_retailroutingcache32_t, time), 0x00);
	assert_int_equal(offsetof(aas_retailroutingcache32_t, cluster), 0x04);
	assert_int_equal(offsetof(aas_retailroutingcache32_t, areanum), 0x08);
	assert_int_equal(offsetof(aas_retailroutingcache32_t, origin), 0x0c);
	assert_int_equal(offsetof(aas_retailroutingcache32_t, starttraveltime), 0x18);
	assert_int_equal(offsetof(aas_retailroutingcache32_t, travelflags), 0x1c);
	assert_int_equal(offsetof(aas_retailroutingcache32_t, prev), 0x20);
	assert_int_equal(offsetof(aas_retailroutingcache32_t, next), 0x24);
	assert_int_equal(offsetof(aas_retailroutingcache32_t, traveltimes), 0x28);
	assert_int_equal(AAS_RetailRoutingCacheSize(0), 0x2c);
	assert_int_equal(AAS_RetailRoutingCacheSize(3), 0x32);
	assert_int_equal(AAS_RetailRoutingCacheSize(-1), 0);

	assert_non_null(aasworld.retailClusterAreaCache);
	assert_non_null(aasworld.retailPortalCache);
	assert_ptr_equal(aasworld.retailClusterAreaCache[1],
		aasworld.retailClusterAreaCache[0] + 2);
	for (int clusterareanum = 0; clusterareanum < 2; ++clusterareanum)
	{
		assert_null(aasworld.retailClusterAreaCache[0][clusterareanum]);
	}
	for (int clusterareanum = 0; clusterareanum < 3; ++clusterareanum)
	{
		assert_null(aasworld.retailClusterAreaCache[1][clusterareanum]);
	}
	for (int areanum = 0; areanum < aasworld.numAreas; ++areanum)
	{
		assert_null(aasworld.retailPortalCache[areanum]);
	}

	aasworld.time = 2.5f;
	aas_retailroutingcache_t *area =
		AAS_GetRetailAreaRoutingCache(1, 2, 0x1234);
	assert_non_null(area);
	assert_ptr_equal(aasworld.retailClusterAreaCache[1][1], area);
	assert_int_equal(area->cluster, 1);
	assert_int_equal(area->areanum, 2);
	assert_int_equal(area->travelflags, 0x1234);
	assert_float_equal(area->time, 2.5f, 0.001f);
	assert_float_equal(area->starttraveltime, 1.0f, 0.001f);
	assert_float_equal(area->origin[0], context->areas[2].center[0], 0.001f);
	assert_null(area->prev);
	assert_null(area->next);
	for (int index = 0; index < context->clusters[1].numareas; ++index)
	{
		assert_int_equal(area->traveltimes[index], 0);
	}

	aasworld.time = 4.0f;
	assert_ptr_equal(AAS_GetRetailAreaRoutingCache(1, 2, 0x1234), area);
	assert_float_equal(area->time, 4.0f, 0.001f);

	aas_retailroutingcache_t *secondarea =
		AAS_GetRetailAreaRoutingCache(1, 2, 0x5678);
	assert_non_null(secondarea);
	assert_ptr_equal(aasworld.retailClusterAreaCache[1][1], secondarea);
	assert_ptr_equal(secondarea->next, area);
	assert_ptr_equal(area->prev, secondarea);
	assert_null(secondarea->prev);

	aas_retailroutingcache_t *portalarea =
		AAS_GetRetailAreaRoutingCache(1, 3, 0x4567);
	assert_non_null(portalarea);
	assert_ptr_equal(aasworld.retailClusterAreaCache[1][2], portalarea);

	aas_retailroutingcache_t *portal =
		AAS_GetRetailPortalRoutingCache(1, 2, 0x2222);
	assert_non_null(portal);
	assert_ptr_equal(aasworld.retailPortalCache[2], portal);
	assert_int_equal(portal->cluster, 1);
	assert_int_equal(portal->areanum, 2);
	assert_int_equal(portal->travelflags, 0x2222);
	assert_float_equal(portal->origin[0], context->areas[2].center[0], 0.001f);
	for (int index = 0; index < aasworld.numPortals; ++index)
	{
		assert_int_equal(portal->traveltimes[index], 0);
	}

	aas_retailroutingcache_t *secondportal =
		AAS_GetRetailPortalRoutingCache(0, 2, 0x3333);
	assert_non_null(secondportal);
	assert_ptr_equal(aasworld.retailPortalCache[2], secondportal);
	assert_ptr_equal(secondportal->next, portal);
	assert_ptr_equal(portal->prev, secondportal);

	AAS_FreeAllRoutingCaches();
	assert_null(aasworld.retailClusterAreaCache);
	assert_null(aasworld.retailPortalCache);
}

/*
=============
test_retail_route_cache_strict_fifteen_second_aging

Verify strict 15-second expiry and correct middle/tail list unlinking.
=============
*/
static void test_retail_route_cache_strict_fifteen_second_aging(void **state)
{
	aas_debug_test_context_t *context = (aas_debug_test_context_t *)(*state);
	ConfigureRetailRouteCacheFixture(context);

	aasworld.time = 0.0f;
	assert_non_null(AAS_GetRetailAreaRoutingCache(1, 2, 11));
	assert_non_null(AAS_GetRetailPortalRoutingCache(1, 2, 11));
	aasworld.time = 5.0f;
	aas_retailroutingcache_t *boundaryarea =
		AAS_GetRetailAreaRoutingCache(1, 2, 22);
	aas_retailroutingcache_t *boundaryportal =
		AAS_GetRetailPortalRoutingCache(1, 2, 22);
	assert_non_null(boundaryarea);
	assert_non_null(boundaryportal);
	aasworld.time = 10.0f;
	aas_retailroutingcache_t *fresharea =
		AAS_GetRetailAreaRoutingCache(1, 2, 33);
	aas_retailroutingcache_t *freshportal =
		AAS_GetRetailPortalRoutingCache(1, 2, 33);
	assert_non_null(fresharea);
	assert_non_null(freshportal);

	aasworld.time = 20.0f;
	AAS_AgeRetailRoutingCaches();
	assert_ptr_equal(aasworld.retailClusterAreaCache[1][1], fresharea);
	assert_ptr_equal(fresharea->next, boundaryarea);
	assert_ptr_equal(boundaryarea->prev, fresharea);
	assert_null(boundaryarea->next);
	assert_ptr_equal(aasworld.retailPortalCache[2], freshportal);
	assert_ptr_equal(freshportal->next, boundaryportal);
	assert_ptr_equal(boundaryportal->prev, freshportal);
	assert_null(boundaryportal->next);

	assert_ptr_equal(AAS_GetRetailAreaRoutingCache(1, 2, 22), boundaryarea);
	assert_float_equal(boundaryarea->time, 20.0f, 0.001f);
	aasworld.time = 25.0f;
	AAS_AgeRetailRoutingCaches();
	assert_ptr_equal(aasworld.retailClusterAreaCache[1][1], fresharea);
	assert_ptr_equal(fresharea->next, boundaryarea);
	assert_ptr_equal(aasworld.retailPortalCache[2], freshportal);
	assert_null(freshportal->next);

	aasworld.time = 25.1f;
	AAS_AgeRetailRoutingCaches();
	assert_ptr_equal(aasworld.retailClusterAreaCache[1][1], boundaryarea);
	assert_null(boundaryarea->prev);
	assert_null(boundaryarea->next);
	assert_null(aasworld.retailPortalCache[2]);

	aasworld.time = 35.0f;
	AAS_AgeRetailRoutingCaches();
	assert_ptr_equal(aasworld.retailClusterAreaCache[1][1], boundaryarea);
	aasworld.time = 35.1f;
	AAS_AgeRetailRoutingCaches();
	assert_null(aasworld.retailClusterAreaCache[1][1]);
}

/*
=============
test_retail_area_cache_propagates_fifo_and_counts

Pin reverse-list order, equal-time route retention, and update accounting.
=============
*/
static void test_retail_area_cache_propagates_fifo_and_counts(void **state)
{
	aas_debug_test_context_t *context = (aas_debug_test_context_t *)(*state);
	ConfigureRetailAreaPropagationFixture(context);
	AAS_RouteFrameResetDiagnostics();

	int travelflags = TEST_RETAIL_TFL_WALK | TEST_RETAIL_TFL_AIR;
	aas_retailroutingcache_t *cache =
		AAS_GetRetailAreaRoutingCache(1, 4, travelflags);
	assert_non_null(cache);
	assert_int_equal(cache->traveltimes[0], 18);
	assert_int_equal(cache->traveltimes[1], 12);
	assert_int_equal(cache->traveltimes[2], 12);
	assert_int_equal(cache->traveltimes[3], 1);
	assert_int_equal(cache->traveltimes[4], 56);
	assert_int_equal(AAS_RetailAreaCacheUpdateCount(), 1);
	assert_int_equal(AAS_RetailFrameRoutingUpdateCount(), 1);

	aasworld.time = 3.0f;
	assert_ptr_equal(AAS_GetRetailAreaRoutingCache(1, 4, travelflags), cache);
	assert_int_equal(AAS_RetailAreaCacheUpdateCount(), 1);
	assert_int_equal(AAS_RetailFrameRoutingUpdateCount(), 1);

	AAS_RouteFrameUpdate();
	assert_int_equal(AAS_RetailAreaCacheUpdateCount(), 1);
	assert_int_equal(AAS_RetailFrameRoutingUpdateCount(), 0);
	assert_non_null(AAS_GetRetailAreaRoutingCache(1,
		4,
		travelflags | 0x40000000));
	assert_int_equal(AAS_RetailAreaCacheUpdateCount(), 2);
	assert_int_equal(AAS_RetailFrameRoutingUpdateCount(), 1);
}

/*
=============
test_retail_area_cache_filters_travel_contents_and_cluster

Pin retail reach type, destination contents, and source-cluster filtering.
=============
*/
static void test_retail_area_cache_filters_travel_contents_and_cluster(void **state)
{
	aas_debug_test_context_t *context = (aas_debug_test_context_t *)(*state);
	ConfigureRetailAreaPropagationFixture(context);

	aas_retailroutingcache_t *cache =
		AAS_GetRetailAreaRoutingCache(1, 4, TEST_RETAIL_TFL_AIR);
	assert_non_null(cache);
	assert_int_equal(cache->traveltimes[0], 0);
	assert_int_equal(cache->traveltimes[1], 0);
	assert_int_equal(cache->traveltimes[2], 0);
	assert_int_equal(cache->traveltimes[3], 1);
	assert_int_equal(cache->traveltimes[4], 0);

	assert_true(AAS_InitRetailRoutingCaches());
	context->areasettings[4].contents = AAS_AREACONTENTS_WATER;
	cache = AAS_GetRetailAreaRoutingCache(1,
		4,
		TEST_RETAIL_TFL_WALK | TEST_RETAIL_TFL_AIR);
	assert_non_null(cache);
	assert_int_equal(cache->traveltimes[0], 0);
	assert_int_equal(cache->traveltimes[1], 0);
	assert_int_equal(cache->traveltimes[2], 0);
	assert_int_equal(cache->traveltimes[3], 1);

	assert_true(AAS_InitRetailRoutingCaches());
	cache = AAS_GetRetailAreaRoutingCache(1,
		4,
		TEST_RETAIL_TFL_WALK | TEST_RETAIL_TFL_WATER);
	assert_non_null(cache);
	assert_int_equal(cache->traveltimes[0], 0);
	assert_int_equal(cache->traveltimes[1], 12);
	assert_int_equal(cache->traveltimes[2], 12);
	assert_int_equal(cache->traveltimes[3], 1);
	assert_int_equal(cache->traveltimes[4], 0);

	assert_true(AAS_InitRetailRoutingCaches());
	context->areasettings[5].cluster = 2;
	cache = AAS_GetRetailAreaRoutingCache(1,
		4,
		TEST_RETAIL_TFL_WALK | TEST_RETAIL_TFL_WATER |
			TEST_RETAIL_TFL_AIR);
	assert_non_null(cache);
	assert_int_equal(cache->traveltimes[0], 18);
	assert_int_equal(cache->traveltimes[1], 12);
	assert_int_equal(cache->traveltimes[2], 12);
	assert_int_equal(cache->traveltimes[3], 1);
	assert_int_equal(cache->traveltimes[4], 0);
}

/*
=============
test_retail_area_cache_wraps_ushort_travel_times

Verify every propagation addition retains retail unsigned-short wraparound.
=============
*/
static void test_retail_area_cache_wraps_ushort_travel_times(void **state)
{
	aas_debug_test_context_t *context = (aas_debug_test_context_t *)(*state);
	ConfigureRetailAreaPropagationFixture(context);
	context->reachability[3].traveltime = 0xffffU;
	context->reachability[4].traveltime = 0xffffU;

	aas_retailroutingcache_t *cache = AAS_GetRetailAreaRoutingCache(1,
		4,
		TEST_RETAIL_TFL_WALK | TEST_RETAIL_TFL_AIR);
	assert_non_null(cache);
	assert_int_equal(cache->traveltimes[0], 7);
	assert_int_equal(cache->traveltimes[1], 1);
	assert_int_equal(cache->traveltimes[2], 1);
	assert_int_equal(cache->traveltimes[3], 1);
	assert_int_equal(cache->traveltimes[4], 45);
}

/*
=============
test_retail_portal_cache_propagates_both_sides_and_lifecycle

Pin both cluster-side transitions, portal goal seeding, hits, and list aging.
=============
*/
static void test_retail_portal_cache_propagates_both_sides_and_lifecycle(void **state)
{
	aas_debug_test_context_t *context = (aas_debug_test_context_t *)(*state);
	ConfigureRetailPortalLineFixture(context);
	AAS_RouteFrameResetDiagnostics();
	int travelflags = TEST_RETAIL_TFL_WALK | TEST_RETAIL_TFL_AIR;

	aasworld.time = 1.0f;
	aas_retailroutingcache_t *backcache =
		AAS_GetRetailPortalRoutingCache(3, 3, travelflags);
	assert_non_null(backcache);
	assert_int_equal(backcache->traveltimes[1], 13);
	assert_int_equal(backcache->traveltimes[2], 7);
	assert_float_equal(backcache->time, 1.0f, 0.001f);
	assert_int_equal(AAS_RetailPortalCacheUpdateCount(), 1);
	assert_int_equal(AAS_RetailAreaCacheUpdateCount(), 3);
	assert_int_equal(AAS_RetailFrameRoutingUpdateCount(), 3);

	aasworld.time = 2.0f;
	assert_ptr_equal(AAS_GetRetailPortalRoutingCache(3, 3, travelflags),
		backcache);
	assert_float_equal(backcache->time, 2.0f, 0.001f);
	assert_int_equal(AAS_RetailPortalCacheUpdateCount(), 1);
	assert_int_equal(AAS_RetailAreaCacheUpdateCount(), 3);
	assert_int_equal(AAS_RetailFrameRoutingUpdateCount(), 3);

	aasworld.time = 3.0f;
	aas_retailroutingcache_t *frontcache =
		AAS_GetRetailPortalRoutingCache(1, 1, travelflags);
	assert_non_null(frontcache);
	assert_int_equal(frontcache->traveltimes[1], 7);
	assert_int_equal(frontcache->traveltimes[2], 13);
	assert_int_equal(AAS_RetailPortalCacheUpdateCount(), 2);
	assert_int_equal(AAS_RetailAreaCacheUpdateCount(), 6);
	assert_int_equal(AAS_RetailFrameRoutingUpdateCount(), 6);

	aasworld.time = 4.0f;
	aas_retailroutingcache_t *seededcache =
		AAS_GetRetailPortalRoutingCache(2, 5, travelflags);
	assert_non_null(seededcache);
	assert_int_equal(seededcache->traveltimes[1], 7);
	assert_int_equal(seededcache->traveltimes[2], 1);
	assert_int_equal(AAS_RetailPortalCacheUpdateCount(), 3);
	assert_int_equal(AAS_RetailAreaCacheUpdateCount(), 6);
	assert_int_equal(AAS_RetailFrameRoutingUpdateCount(), 6);

	aasworld.time = 5.0f;
	aas_retailroutingcache_t *secondback =
		AAS_GetRetailPortalRoutingCache(3,
			3,
			travelflags | 0x40000000);
	assert_non_null(secondback);
	assert_ptr_equal(aasworld.retailPortalCache[3], secondback);
	assert_ptr_equal(secondback->next, backcache);
	assert_ptr_equal(backcache->prev, secondback);
	assert_int_equal(secondback->traveltimes[1], 13);
	assert_int_equal(secondback->traveltimes[2], 7);
	assert_int_equal(AAS_RetailPortalCacheUpdateCount(), 4);
	assert_int_equal(AAS_RetailAreaCacheUpdateCount(), 9);
	assert_int_equal(AAS_RetailFrameRoutingUpdateCount(), 9);

	aasworld.time = 20.0f;
	AAS_AgeRetailRoutingCaches();
	assert_ptr_equal(aasworld.retailPortalCache[3], secondback);
	assert_null(secondback->prev);
	assert_null(secondback->next);
	assert_null(aasworld.retailPortalCache[1]);
	assert_null(aasworld.retailPortalCache[5]);
	aasworld.time = 20.1f;
	AAS_AgeRetailRoutingCaches();
	assert_null(aasworld.retailPortalCache[3]);
}

/*
=============
test_retail_portal_cache_inherits_travel_and_content_masks

Verify portal expansion uses the exactly filtered cluster-area cache results.
=============
*/
static void test_retail_portal_cache_inherits_travel_and_content_masks(void **state)
{
	aas_debug_test_context_t *context = (aas_debug_test_context_t *)(*state);
	ConfigureRetailPortalLineFixture(context);
	AAS_RouteFrameResetDiagnostics();

	aas_retailroutingcache_t *cache =
		AAS_GetRetailPortalRoutingCache(3, 3, TEST_RETAIL_TFL_AIR);
	assert_non_null(cache);
	assert_int_equal(cache->traveltimes[1], 0);
	assert_int_equal(cache->traveltimes[2], 0);

	assert_true(AAS_InitRetailRoutingCaches());
	context->areasettings[3].contents = AAS_AREACONTENTS_WATER;
	cache = AAS_GetRetailPortalRoutingCache(3,
		3,
		TEST_RETAIL_TFL_WALK | TEST_RETAIL_TFL_AIR);
	assert_non_null(cache);
	assert_int_equal(cache->traveltimes[1], 0);
	assert_int_equal(cache->traveltimes[2], 0);

	assert_true(AAS_InitRetailRoutingCaches());
	cache = AAS_GetRetailPortalRoutingCache(3,
		3,
		TEST_RETAIL_TFL_WALK | TEST_RETAIL_TFL_WATER);
	assert_non_null(cache);
	assert_int_equal(cache->traveltimes[1], 0);
	assert_int_equal(cache->traveltimes[2], 7);
	assert_int_equal(AAS_RetailPortalCacheUpdateCount(), 3);
}

/*
=============
test_retail_portal_cache_wraps_ushort_costs

Verify cross-cluster accumulation wraps before the strict cache comparison.
=============
*/
static void test_retail_portal_cache_wraps_ushort_costs(void **state)
{
	aas_debug_test_context_t *context = (aas_debug_test_context_t *)(*state);
	ConfigureRetailPortalLineFixture(context);
	context->reachability[4].traveltime = 29998;
	context->reachability[6].traveltime = 39998;
	AAS_RouteFrameResetDiagnostics();

	aas_retailroutingcache_t *cache = AAS_GetRetailPortalRoutingCache(3,
		3,
		TEST_RETAIL_TFL_WALK | TEST_RETAIL_TFL_AIR);
	assert_non_null(cache);
	assert_int_equal(cache->traveltimes[1], 4465);
	assert_int_equal(cache->traveltimes[2], 40001);
	assert_int_equal(AAS_RetailPortalCacheUpdateCount(), 1);
	assert_int_equal(AAS_RetailAreaCacheUpdateCount(), 3);
}

/*
=============
test_retail_portal_cache_retains_equal_fifo_side

Pin strict equal-cost rejection by observing which cluster pops the shared portal.
=============
*/
static void test_retail_portal_cache_retains_equal_fifo_side(void **state)
{
	aas_debug_test_context_t *context = (aas_debug_test_context_t *)(*state);
	ConfigureRetailPortalEqualFixture(context);
	AAS_RouteFrameResetDiagnostics();
	int travelflags = TEST_RETAIL_TFL_WALK | TEST_RETAIL_TFL_AIR;

	aas_retailroutingcache_t *cache =
		AAS_GetRetailPortalRoutingCache(1, 1, travelflags);
	assert_non_null(cache);
	assert_int_equal(cache->traveltimes[1], 7);
	assert_int_equal(cache->traveltimes[2], 7);
	assert_int_equal(cache->traveltimes[3], 13);
	assert_non_null(aasworld.retailClusterAreaCache[3][1]);
	assert_int_equal(aasworld.retailClusterAreaCache[3][1]->areanum, 6);
	assert_int_equal(aasworld.retailClusterAreaCache[3][1]->travelflags,
		travelflags);
	assert_null(aasworld.retailClusterAreaCache[2][1]);
	assert_int_equal(AAS_RetailPortalCacheUpdateCount(), 1);
	assert_int_equal(AAS_RetailAreaCacheUpdateCount(), 4);
}

/*
=============
test_retail_route_query_same_cluster_origin_and_first_hop

Pin direct cluster-cache consumption, strict first-hop order, and successor
origin-to-reach local travel-time addition.
=============
*/
static void test_retail_route_query_same_cluster_origin_and_first_hop(void **state)
{
	aas_debug_test_context_t *context = (aas_debug_test_context_t *)(*state);
	ConfigureRetailAreaPropagationFixture(context);
	AAS_RouteFrameResetDiagnostics();
	int travelflags = TEST_RETAIL_TFL_WALK | TEST_RETAIL_TFL_AIR;

	assert_int_equal(AAS_AreaTravelTimeToGoalArea(1,
		NULL,
		4,
		travelflags),
		18);
	assert_int_equal(AAS_AreaReachabilityToGoalArea(1,
		NULL,
		4,
		travelflags),
		1);

	vec3_t origin = {0.0f, 0.0f, 0.0f};
	assert_int_equal(AAS_AreaTravelTimeToGoalArea(1,
		origin,
		4,
		travelflags),
		21);
	VectorSet(origin, 10.0f, 0.0f, 0.0f);
	assert_int_equal(AAS_AreaTravelTimeToGoalArea(1,
		origin,
		4,
		travelflags),
		19);
	assert_int_equal(AAS_RetailAreaCacheUpdateCount(), 1);
}

/*
=============
test_retail_route_query_filters_and_projects_travel_flags

Verify raw Gladiator masks filter reach and area contents while the Q3-shaped
public default mask is projected onto the retail content-bit positions.
=============
*/
static void test_retail_route_query_filters_and_projects_travel_flags(void **state)
{
	aas_debug_test_context_t *context = (aas_debug_test_context_t *)(*state);
	ConfigureRetailAreaPropagationFixture(context);
	AAS_RouteFrameResetDiagnostics();

	assert_int_equal(AAS_AreaTravelTimeToGoalArea(1,
		NULL,
		4,
		TEST_RETAIL_TFL_WALK),
		0);
	assert_int_equal(AAS_AreaTravelTimeToGoalArea(1,
		NULL,
		4,
		TEST_RETAIL_TFL_AIR),
		0);
	assert_int_equal(AAS_AreaTravelTimeToGoalArea(1,
		NULL,
		4,
		TEST_RETAIL_TFL_WALK | TEST_RETAIL_TFL_AIR),
		18);
	assert_int_equal(AAS_AreaReachabilityToGoalArea(1,
		NULL,
		4,
		TEST_RETAIL_TFL_AIR),
		0);
	assert_int_equal(AAS_AreaTravelTimeToGoalArea(1,
		NULL,
		4,
		TFL_DEFAULT),
		18);

	context->areasettings[4].contents = AAS_AREACONTENTS_WATER;
	assert_true(AAS_InitRetailRoutingCaches());
	AAS_RouteFrameResetDiagnostics();
	assert_int_equal(AAS_AreaTravelTimeToGoalArea(1,
		NULL,
		4,
		TEST_RETAIL_TFL_WALK | TEST_RETAIL_TFL_AIR),
		0);
	assert_int_equal(AAS_AreaTravelTimeToGoalArea(1,
		NULL,
		4,
		TEST_RETAIL_TFL_WALK | TEST_RETAIL_TFL_AIR |
			TEST_RETAIL_TFL_WATER),
		18);
	assert_int_equal(AAS_AreaTravelTimeToGoalArea(1,
		NULL,
		4,
		TFL_DEFAULT),
		18);
}

/*
=============
test_retail_route_query_crosses_portals_and_adjusts_portal_sides

Pin the portal-cache value at 0x1001a109, its source-cluster area-cache sum,
and both same-cluster portal adjustment branches.
=============
*/
static void test_retail_route_query_crosses_portals_and_adjusts_portal_sides(void **state)
{
	aas_debug_test_context_t *context = (aas_debug_test_context_t *)(*state);
	ConfigureRetailPortalLineFixture(context);
	AAS_RouteFrameResetDiagnostics();
	int travelflags = TEST_RETAIL_TFL_WALK | TEST_RETAIL_TFL_AIR;

	assert_int_equal(AAS_AreaTravelTimeToGoalArea(1,
		NULL,
		3,
		travelflags),
		19);
	assert_int_equal(AAS_AreaReachabilityToGoalArea(1,
		NULL,
		3,
		travelflags),
		1);
	vec3_t origin = {0.0f, 0.0f, 0.0f};
	assert_int_equal(AAS_AreaTravelTimeToGoalArea(1,
		origin,
		3,
		travelflags),
		20);
	assert_int_equal(AAS_AreaTravelTimeToGoalArea(4,
		NULL,
		3,
		travelflags),
		13);
	assert_int_equal(AAS_AreaTravelTimeToGoalArea(1,
		NULL,
		4,
		travelflags),
		6);
	assert_int_equal(AAS_AreaTravelTimeToGoalArea(4,
		NULL,
		1,
		travelflags),
		6);
}

/*
=============
test_retail_route_query_wraps_cross_cluster_ushort_sum

Verify the public cache consumer wraps the final portal-plus-area addition
before its strict unsigned-short comparison.
=============
*/
static void test_retail_route_query_wraps_cross_cluster_ushort_sum(void **state)
{
	aas_debug_test_context_t *context = (aas_debug_test_context_t *)(*state);
	ConfigureRetailPortalLineFixture(context);
	context->reachability[1].traveltime = 65000;
	context->reachability[4].traveltime = 29998;
	context->reachability[6].traveltime = 39998;
	AAS_RouteFrameResetDiagnostics();

	assert_int_equal(AAS_AreaTravelTimeToGoalArea(1,
		NULL,
		3,
		TEST_RETAIL_TFL_WALK | TEST_RETAIL_TFL_AIR),
		3931);
}

/*
=============
test_retail_route_query_preserves_guards_and_same_area_order

Pin uninitialized and invalid handling, the user-required same-area ordering,
and retail's frame-routing-update cutoff after ten.
=============
*/
static void test_retail_route_query_preserves_guards_and_same_area_order(void **state)
{
	aas_debug_test_context_t *context = (aas_debug_test_context_t *)(*state);
	ConfigureRetailAreaPropagationFixture(context);
	AAS_RouteFrameResetDiagnostics();
	int travelflags = TEST_RETAIL_TFL_WALK | TEST_RETAIL_TFL_AIR;

	aasworld.initialized = qfalse;
	assert_int_equal(AAS_AreaTravelTimeToGoalArea(1,
		NULL,
		1,
		travelflags),
		0);
	assert_int_equal(AAS_AreaReachabilityToGoalArea(1,
		NULL,
		1,
		travelflags),
		0);
	aasworld.initialized = qtrue;
	assert_int_equal(AAS_AreaTravelTimeToGoalArea(999,
		NULL,
		999,
		travelflags),
		1);
	assert_int_equal(AAS_AreaReachabilityToGoalArea(999,
		NULL,
		999,
		travelflags),
		0);

	Mock_Reset(context);
	assert_int_equal(AAS_AreaTravelTimeToGoalArea(0,
		NULL,
		4,
		travelflags),
		0);
	assert_non_null(Mock_FindPrint(context, "areanum 0 out of range"));
	assert_int_equal(AAS_AreaTravelTimeToGoalArea(1,
		NULL,
		99,
		travelflags),
		0);
	assert_non_null(Mock_FindPrint(context, "goalareanum 99 out of range"));

	for (int index = 0; index < 11; ++index)
	{
		assert_non_null(AAS_GetRetailAreaRoutingCache(1,
			4,
			travelflags | (index << 24)));
	}
	assert_int_equal(AAS_RetailFrameRoutingUpdateCount(), 11);
	assert_int_equal(AAS_AreaTravelTimeToGoalArea(1,
		NULL,
		4,
		travelflags),
		0);
	assert_int_equal(AAS_AreaReachabilityToGoalArea(1,
		NULL,
		4,
		travelflags),
		0);
	assert_int_equal(AAS_AreaTravelTimeToGoalArea(4,
		NULL,
		4,
		travelflags),
		1);

	AAS_RouteFrameResetDiagnostics();
	assert_int_equal(AAS_AreaTravelTimeToGoalArea(1,
		NULL,
		4,
		travelflags),
		18);
}

/*
=============
VisibilityPrepareEntity

Populate one retail entity slot around a requested origin.
=============
*/
static void VisibilityPrepareEntity(int entnum,
	const vec3_t origin,
	const vec3_t mins,
	const vec3_t maxs)
{
	aas_entity_t *entity = &aasworld.entities[entnum];
	entity->inuse = qtrue;
	entity->number = entnum;
	VectorCopy(origin, entity->origin);
	VectorCopy(mins, entity->mins);
	VectorCopy(maxs, entity->maxs);
}

/*
=============
VisibilityLinkEntity

Give an entity the non-null retail area-link gate used by VisibleEntities.
=============
*/
static void VisibilityLinkEntity(int entnum)
{
	AAS_InitAASLinkHeap();
	aas_link_t *link = AAS_AllocAASLink();
	assert_non_null(link);
	memset(link, 0, sizeof(*link));
	link->entnum = entnum;
	aasworld.entities[entnum].areas = link;
}

/*
=============
VisibilityReleaseEntities

Release visibility-test slots through the configured entity lifecycle.
=============
*/
static void VisibilityReleaseEntities(void)
{
	assert_int_equal(AAS_ConfigureEntityLimits(0, 0), BLERR_NOERROR);
}

/*
=============
test_retail_visibility_setup_enumeration_and_next_entity

Pin setup-time slot numbering, live/dead filtering independent of area links,
ascending capped clients, maxclients rather than maxentities, and NextEntity.
=============
*/
static void test_retail_visibility_setup_enumeration_and_next_entity(void **state)
{
	aas_debug_test_context_t *context =
		(aas_debug_test_context_t *)*state;
	Mock_Reset(context);
	assert_int_equal(AAS_ConfigureEntityLimits(8, 3), BLERR_NOERROR);
	assert_int_equal(aasworld.maxEntities, 8);
	assert_int_equal(aasworld.maxClients, 3);
	assert_non_null(aasworld.entities);
	for (int entnum = 0; entnum < aasworld.maxEntities; ++entnum)
	{
		assert_int_equal(aasworld.entities[entnum].number, entnum);
		assert_false(aasworld.entities[entnum].inuse);
	}

	vec3_t mins = {-4.0f, -4.0f, -4.0f};
	vec3_t maxs = {4.0f, 4.0f, 4.0f};
	vec3_t first_origin = {100.0f, 0.0f, 0.0f};
	vec3_t stale_origin = {200.0f, 0.0f, 0.0f};
	vec3_t third_origin = {300.0f, 0.0f, 0.0f};
	vec3_t beyond_clients_origin = {400.0f, 0.0f, 0.0f};
	VisibilityPrepareEntity(1, first_origin, mins, maxs);
	VisibilityPrepareEntity(2, stale_origin, mins, maxs);
	aasworld.entities[2].inuse = qfalse;
	VisibilityLinkEntity(2);
	VisibilityPrepareEntity(3, third_origin, mins, maxs);
	VisibilityPrepareEntity(4, beyond_clients_origin, mins, maxs);

	vec3_t eye = {0.0f, 0.0f, 0.0f};
	vec3_t viewangles = {0.0f, 0.0f, 0.0f};
	int entitynums[8] = {0};
	int count = AAS_VisibleEntities(0,
		eye,
		viewangles,
		360.0f,
		(int)ARRAY_LEN(entitynums),
		entitynums);
	assert_int_equal(count, 2);
	assert_int_equal(entitynums[0], 1);
	assert_int_equal(entitynums[1], 3);
	assert_int_equal(context->bridge_trace_count, 2);

	Mock_Reset(context);
	memset(entitynums, 0, sizeof(entitynums));
	count = AAS_VisibleEntities(0,
		eye,
		viewangles,
		360.0f,
		1,
		entitynums);
	assert_int_equal(count, 1);
	assert_int_equal(entitynums[0], 1);
	assert_int_equal(context->bridge_trace_count, 1);

	for (int entnum = 0; entnum < aasworld.maxEntities; ++entnum)
	{
		aasworld.entities[entnum].inuse = qfalse;
	}
	aasworld.entities[0].inuse = qtrue;
	aasworld.entities[2].inuse = qtrue;
	aasworld.entities[7].inuse = qtrue;
	aasworld.loaded = qfalse;
	assert_int_equal(AAS_NextEntity(-9), 0);
	aasworld.loaded = qtrue;
	assert_int_equal(AAS_NextEntity(-9), 0);
	assert_int_equal(AAS_NextEntity(0), 2);
	assert_int_equal(AAS_NextEntity(2), 7);
	assert_int_equal(AAS_NextEntity(7), 0);

	VisibilityReleaseEntities();
}

/*
=============
test_retail_entity_visibility_quantized_inclusive_fov

Pin both inclusive half-FOV axes and the retail 16-bit angle quantization.
=============
*/
static void test_retail_entity_visibility_quantized_inclusive_fov(void **state)
{
	aas_debug_test_context_t *context =
		(aas_debug_test_context_t *)*state;
	assert_int_equal(AAS_ConfigureEntityLimits(3, 1), BLERR_NOERROR);
	vec3_t origin = {100.0f, 0.0f, 0.0f};
	vec3_t bounds = {0.0f, 0.0f, 0.0f};
	vec3_t eye = {0.0f, 0.0f, 0.0f};
	VisibilityPrepareEntity(1, origin, bounds, bounds);

	Mock_Reset(context);
	vec3_t viewangles = {45.0f, -45.0f, 0.0f};
	assert_true(AAS_EntityVisible(0, eye, viewangles, 90.0f, 1));
	assert_int_equal(context->bridge_trace_count, 1);

	Mock_Reset(context);
	VectorSet(viewangles, 0.0f, -45.0001f, 0.0f);
	assert_true(AAS_EntityVisible(0, eye, viewangles, 90.0f, 1));
	assert_int_equal(context->bridge_trace_count, 1);

	Mock_Reset(context);
	VectorSet(viewangles, 0.0f, -45.01f, 0.0f);
	assert_false(AAS_EntityVisible(0, eye, viewangles, 90.0f, 1));
	assert_int_equal(context->bridge_trace_count, 0);
	assert_int_equal(context->bridge_point_contents_count, 0);

	VisibilityReleaseEntities();
}

/*
=============
test_retail_entity_visibility_three_sample_order

Pin center, bottom, then top sampling, with PVS preceding each target/eye
contents pair and each blocked trace proceeding to the next sample.
=============
*/
static void test_retail_entity_visibility_three_sample_order(void **state)
{
	aas_debug_test_context_t *context =
		(aas_debug_test_context_t *)*state;
	assert_int_equal(AAS_ConfigureEntityLimits(3, 1), BLERR_NOERROR);
	vec3_t origin = {100.0f, 0.0f, 0.0f};
	vec3_t mins = {-4.0f, -4.0f, -10.0f};
	vec3_t maxs = {4.0f, 4.0f, 30.0f};
	vec3_t eye = {0.0f, 0.0f, 10.0f};
	vec3_t viewangles = {0.0f, 0.0f, 0.0f};
	VisibilityPrepareEntity(1, origin, mins, maxs);

	Mock_Reset(context);
	context->bridge_trace_result.fraction = 0.25f;
	context->bridge_trace_result.ent = -1;
	assert_false(AAS_EntityVisible(0, eye, viewangles, 360.0f, 1));
	assert_int_equal(context->bridge_trace_count, 3);
	assert_int_equal(context->bridge_point_contents_count, 6);
	assert_float_equal(context->bridge_point_contents_points[0][2],
		10.0f,
		0.001f);
	assert_memory_equal(context->bridge_point_contents_points[1],
		eye,
		sizeof(vec3_t));
	assert_float_equal(context->bridge_point_contents_points[2][2],
		0.0f,
		0.001f);
	assert_memory_equal(context->bridge_point_contents_points[3],
		eye,
		sizeof(vec3_t));
	assert_float_equal(context->bridge_point_contents_points[4][2],
		40.0f,
		0.001f);
	assert_memory_equal(context->bridge_point_contents_points[5],
		eye,
		sizeof(vec3_t));
	assert_float_equal(context->bridge_trace_ends[0][2], 10.0f, 0.001f);
	assert_float_equal(context->bridge_trace_ends[1][2], 0.0f, 0.001f);
	assert_float_equal(context->bridge_trace_ends[2][2], 40.0f, 0.001f);

	VisibilityReleaseEntities();
}

/*
=============
test_retail_entity_visibility_direct_hit

Pin the target-entity trace hit success path before lower samples are tried.
=============
*/
static void test_retail_entity_visibility_direct_hit(void **state)
{
	aas_debug_test_context_t *context =
		(aas_debug_test_context_t *)*state;
	assert_int_equal(AAS_ConfigureEntityLimits(3, 1), BLERR_NOERROR);
	vec3_t origin = {100.0f, 0.0f, 0.0f};
	vec3_t bounds = {0.0f, 0.0f, 0.0f};
	vec3_t eye = {0.0f, 0.0f, 0.0f};
	vec3_t viewangles = {0.0f, 0.0f, 0.0f};
	VisibilityPrepareEntity(1, origin, bounds, bounds);

	Mock_Reset(context);
	context->bridge_trace_result.fraction = 0.25f;
	context->bridge_trace_result.ent = 1;
	assert_true(AAS_EntityVisible(7, eye, viewangles, 90.0f, 1));
	assert_int_equal(context->bridge_trace_count, 1);
	assert_int_equal(context->bridge_trace_passents[0], 7);
	assert_int_equal(context->bridge_trace_contentmasks[0], 0x02030003);

	VisibilityReleaseEntities();
}

/*
=============
test_retail_entity_visibility_fluid_reversal

Pin eye-wet/target-dry endpoint and entity reversal plus the XOR fluid mask.
=============
*/
static void test_retail_entity_visibility_fluid_reversal(void **state)
{
	aas_debug_test_context_t *context =
		(aas_debug_test_context_t *)*state;
	assert_int_equal(AAS_ConfigureEntityLimits(3, 1), BLERR_NOERROR);
	vec3_t origin = {100.0f, 0.0f, 0.0f};
	vec3_t bounds = {0.0f, 0.0f, 0.0f};
	vec3_t eye = {0.0f, 0.0f, 0.0f};
	vec3_t viewangles = {0.0f, 0.0f, 0.0f};
	VisibilityPrepareEntity(1, origin, bounds, bounds);

	Mock_Reset(context);
	context->bridge_point_contents_results[0] = 0;
	context->bridge_point_contents_results[1] = CONTENTS_WATER;
	context->bridge_point_contents_result_count = 2U;
	context->bridge_trace_result.fraction = 0.25f;
	context->bridge_trace_result.ent = 7;
	assert_true(AAS_EntityVisible(7, eye, viewangles, 90.0f, 1));
	assert_int_equal(context->bridge_trace_count, 1);
	assert_memory_equal(context->bridge_trace_starts[0],
		origin,
		sizeof(vec3_t));
	assert_memory_equal(context->bridge_trace_ends[0], eye, sizeof(vec3_t));
	assert_int_equal(context->bridge_trace_passents[0], 1);
	assert_int_equal(context->bridge_trace_contentmasks[0], 0x0203003b);

	VisibilityReleaseEntities();
}

/*
=============
test_retail_entity_visibility_translucent_fluid_continuation

Pin the second trace from the fluid hit point with fluid bits removed.
=============
*/
static void test_retail_entity_visibility_translucent_fluid_continuation(void **state)
{
	aas_debug_test_context_t *context =
		(aas_debug_test_context_t *)*state;
	assert_int_equal(AAS_ConfigureEntityLimits(3, 1), BLERR_NOERROR);
	vec3_t origin = {100.0f, 0.0f, 0.0f};
	vec3_t bounds = {0.0f, 0.0f, 0.0f};
	vec3_t eye = {0.0f, 0.0f, 0.0f};
	vec3_t viewangles = {0.0f, 0.0f, 0.0f};
	VisibilityPrepareEntity(1, origin, bounds, bounds);

	Mock_Reset(context);
	context->bridge_point_contents_results[0] = CONTENTS_WATER;
	context->bridge_point_contents_results[1] = 0;
	context->bridge_point_contents_result_count = 2U;
	context->bridge_trace_results[0].fraction = 0.5f;
	context->bridge_trace_results[0].contents = CONTENTS_WATER;
	context->bridge_trace_results[0].surface.flags = 0x10;
	VectorSet(context->bridge_trace_results[0].endpos, 50.0f, 0.0f, 0.0f);
	context->bridge_trace_results[1].fraction = 1.0f;
	context->bridge_trace_result_count = 2U;
	assert_true(AAS_EntityVisible(7, eye, viewangles, 90.0f, 1));
	assert_int_equal(context->bridge_trace_count, 2);
	assert_int_equal(context->bridge_trace_contentmasks[0], 0x0203003b);
	assert_int_equal(context->bridge_trace_contentmasks[1], 0x02030003);
	assert_int_equal(context->bridge_trace_passents[0], 7);
	assert_int_equal(context->bridge_trace_passents[1], 7);
	assert_float_equal(context->bridge_trace_starts[1][0], 50.0f, 0.001f);
	assert_memory_equal(context->bridge_trace_ends[1], origin, sizeof(vec3_t));

	VisibilityReleaseEntities();
}

/*
=============
WriteSoundConfigFixture

Emit a temporary soundconfig the retail loader can be pointed at by libvar.
=============
*/
static void WriteSoundConfigFixture(const char *path, const char *text)
{
	FILE *file = fopen(path, "wb");
	assert_non_null(file);
	assert_true(fputs(text, file) >= 0);
	assert_int_equal(fclose(file), 0);
}

/*
=============
setup_aas_sound

Bring up an isolated libvar registry and print capture for soundconfig tests.
=============
*/
static int setup_aas_sound(void **state)
{
	aas_debug_test_context_t *context =
		(aas_debug_test_context_t *)calloc(1, sizeof(*context));
	assert_non_null(context);

	context->imports.Print = Mock_Print;
	BotInterface_SetImportTable(&context->imports);
	g_active_context = context;
	LibVar_Init();

	*state = context;
	return 0;
}

/*
=============
teardown_aas_sound

Release the sound subsystem, lexer and libvar state between soundconfig tests.
=============
*/
static int teardown_aas_sound(void **state)
{
	AAS_SoundSubsystem_ResetState();
	PC_ShutdownLexer();
	LibVar_Shutdown();
	CRC_ResetSourceChecksums();
	BotMemory_Shutdown();
	BotInterface_SetImportTable(NULL);
	g_active_context = NULL;
	free(*state);
	*state = NULL;
	return 0;
}

/*
=============
test_retail_sound_emit_replaces_active_record_on_zero_timeofs

Pin the x87 equal-bit test at 0x1001ce9a: an immediate emit unlinks the active
(entity, soundindex) record, a delayed emit does not, and the end time is
(frame + duration) + timeofs as computed at 0x1001cee4.
=============
*/
static void test_retail_sound_emit_replaces_active_record_on_zero_timeofs(
	void **state)
{
	(void)state;
	const char *config_path =
		PROJECT_SOURCE_DIR "/aas_sound_parity_replace.c";
	WriteSoundConfigFixture(config_path,
		"soundinfo\n"
		"{\n"
		"\tname \"parity/replace.wav\"\n"
		"\tvolume 40\n"
		"\tduration 4\n"
		"\ttype 7\n"
		"\trecognition 0\n"
		"\tstring \"\"\n"
		"}\n");

	LibVarSet("max_soundinfo", "4");
	LibVarSet("max_aassounds", "8");
	LibVarSet("soundconfig", config_path);
	assert_int_equal(AAS_SoundSubsystem_Init(), BLERR_NOERROR);

	char asset[] = "parity/replace.wav";
	char *assets[] = {asset};
	assert_true(AAS_SoundSubsystem_RegisterMapAssets(1, assets));

	vec3_t origin = {1.0f, 2.0f, 3.0f};
	AAS_SoundSubsystem_SetFrameTime(0.0f);
	assert_int_equal(AAS_SoundSubsystem_UpdateSound(origin, 5, 1, 0, 0.5f,
		1.0f, 0.0f), BLERR_NOERROR);
	AAS_SoundSubsystem_SetFrameTime(1.0f);
	assert_int_equal(AAS_SoundSubsystem_SoundEventCount(), 1U);

	/* timeofs == 0 replaces the live record instead of duplicating it. */
	assert_int_equal(AAS_SoundSubsystem_UpdateSound(origin, 5, 1, 0, 0.5f,
		1.0f, 0.0f), BLERR_NOERROR);
	assert_int_equal(AAS_SoundSubsystem_SoundEventCount(), 0U);
	assert_null(AAS_SoundSubsystem_SoundEvent(0));

	AAS_SoundSubsystem_SetFrameTime(1.001f);
	assert_int_equal(AAS_SoundSubsystem_SoundEventCount(), 1U);
	const aas_sound_event_t *event = AAS_SoundSubsystem_SoundEvent(0);
	assert_non_null(event);
	assert_float_equal(event->start, 1.0f, 0.0001f);
	assert_float_equal(event->end, 5.0f, 0.0001f);

	/*
	 * A negative timeofs is not the retail trigger: mask 0x40 is the equal
	 * bit, not the less-than bit, so the live record survives an emit that
	 * the pre-fix reconstruction would have replaced.
	 */
	assert_int_equal(AAS_SoundSubsystem_UpdateSound(origin, 5, 1, 0, 0.5f,
		1.0f, -0.25f), BLERR_NOERROR);
	assert_int_equal(AAS_SoundSubsystem_SoundEventCount(), 1U);
	assert_ptr_equal(AAS_SoundSubsystem_SoundEvent(0), event);
	assert_float_equal(event->end, 5.0f, 0.0001f);

	/* A positive timeofs likewise leaves the live record alone at emit time. */
	assert_int_equal(AAS_SoundSubsystem_UpdateSound(origin, 5, 1, 0, 0.5f,
		1.0f, 0.5f), BLERR_NOERROR);
	assert_int_equal(AAS_SoundSubsystem_SoundEventCount(), 1U);
	assert_ptr_equal(AAS_SoundSubsystem_SoundEvent(0), event);

	/*
	 * The scheduled-to-active promotion at 0x1001cffa runs its own
	 * (entity, soundindex) removal, so the delayed records still collapse to
	 * one active entry once their start times pass.
	 */
	AAS_SoundSubsystem_SetFrameTime(2.0f);
	assert_int_equal(AAS_SoundSubsystem_SoundEventCount(), 1U);
	const aas_sound_event_t *delayed = AAS_SoundSubsystem_SoundEvent(0);
	assert_non_null(delayed);
	assert_float_equal(delayed->start, 1.501f, 0.0001f);
	assert_float_equal(delayed->end, 5.501f, 0.0001f);

	assert_int_equal(remove(config_path), 0);
}

/*
=============
test_retail_soundinfo_records_are_zero_filled

Pin the 0x1001c904 __memfill_u32(record, 0, 0x2c) clear: omitted fields read
back as zero rather than as the fielddef clamp limits.
=============
*/
static void test_retail_soundinfo_records_are_zero_filled(void **state)
{
	(void)state;
	const char *config_path =
		PROJECT_SOURCE_DIR "/aas_sound_parity_zerofill.c";
	WriteSoundConfigFixture(config_path,
		"soundinfo\n"
		"{\n"
		"\tname \"parity/sparse.wav\"\n"
		"\ttype 7\n"
		"}\n");

	LibVarSet("max_soundinfo", "4");
	LibVarSet("max_aassounds", "8");
	LibVarSet("soundconfig", config_path);
	assert_int_equal(AAS_SoundSubsystem_Init(), BLERR_NOERROR);
	assert_int_equal(AAS_SoundSubsystem_InfoCount(), 1U);

	const aas_soundinfo_t *info = AAS_SoundSubsystem_Info(0U);
	assert_non_null(info);
	assert_string_equal(info->name, "parity/sparse.wav");
	assert_int_equal(info->type, 7);
	assert_float_equal(info->volume, 0.0f, 0.0001f);
	assert_float_equal(info->duration, 0.0f, 0.0001f);
	assert_float_equal(info->recognition, 0.0f, 0.0001f);
	assert_string_equal(info->string, "");

	/*
	 * A zero duration yields end == start, so the record is dropped by the
	 * expiry sweep of the very next frame pass instead of surviving the ten
	 * seconds the old 10.0f default granted it.
	 */
	char asset[] = "parity/sparse.wav";
	char *assets[] = {asset};
	assert_true(AAS_SoundSubsystem_RegisterMapAssets(1, assets));
	vec3_t origin = {0.0f, 0.0f, 0.0f};
	AAS_SoundSubsystem_SetFrameTime(0.0f);
	assert_int_equal(AAS_SoundSubsystem_UpdateSound(origin, 9, 0, 0, 1.0f,
		1.0f, 0.0f), BLERR_NOERROR);
	AAS_SoundSubsystem_SetFrameTime(0.001f);
	assert_int_equal(AAS_SoundSubsystem_SoundEventCount(), 1U);
	const aas_sound_event_t *event = AAS_SoundSubsystem_SoundEvent(0);
	assert_non_null(event);
	assert_float_equal(event->start, 0.0f, 0.0001f);
	assert_float_equal(event->end, 0.0f, 0.0001f);
	AAS_SoundSubsystem_SetFrameTime(0.002f);
	assert_int_equal(AAS_SoundSubsystem_SoundEventCount(), 0U);

	assert_int_equal(remove(config_path), 0);
}

/*
=============
test_retail_soundinfo_bounds_reject_out_of_range_values

Pin the FT_BOUNDED volume [0, 80] and duration [0, 10] fielddefs at .data
0x1005c08c/0x1005c0a8: an out-of-range value aborts the whole load.
=============
*/
static void test_retail_soundinfo_bounds_reject_out_of_range_values(
	void **state)
{
	aas_debug_test_context_t *context = (aas_debug_test_context_t *)*state;
	const char *config_path =
		PROJECT_SOURCE_DIR "/aas_sound_parity_bounds.c";
	WriteSoundConfigFixture(config_path,
		"soundinfo\n"
		"{\n"
		"\tname \"parity/loud.wav\"\n"
		"\tvolume 100.0\n"
		"\tduration 0.2\n"
		"\ttype 1\n"
		"\trecognition 0\n"
		"\tstring \"\"\n"
		"}\n");

	LibVarSet("max_soundinfo", "4");
	LibVarSet("max_aassounds", "8");
	LibVarSet("soundconfig", config_path);
	Mock_Reset(context);
	assert_int_equal(AAS_SoundSubsystem_Init(), BLERR_NOERROR);
	assert_int_equal(AAS_SoundSubsystem_InfoCount(), 0U);
	assert_non_null(Mock_FindPrint(context,
		"float out of range [0.000000, 80.000000]"));
	assert_null(Mock_FindPrint(context, "loaded "));

	/* An integer token takes the same bound through the whole-number path. */
	WriteSoundConfigFixture(config_path,
		"soundinfo\n"
		"{\n"
		"\tname \"parity/loud.wav\"\n"
		"\tvolume 100\n"
		"\tduration 0.2\n"
		"\ttype 1\n"
		"\trecognition 0\n"
		"\tstring \"\"\n"
		"}\n");
	CRC_ResetSourceChecksums();
	Mock_Reset(context);
	assert_int_equal(AAS_SoundSubsystem_Init(), BLERR_NOERROR);
	assert_int_equal(AAS_SoundSubsystem_InfoCount(), 0U);
	assert_non_null(Mock_FindPrint(context,
		"value 100 out of range [0.000000, 80.000000]"));

	/* The inclusive upper bound the shipped sounds.c relies on still passes. */
	WriteSoundConfigFixture(config_path,
		"soundinfo\n"
		"{\n"
		"\tname \"parity/loud.wav\"\n"
		"\tvolume 80.0\n"
		"\tduration 2.98\n"
		"\ttype 1\n"
		"\trecognition 0\n"
		"\tstring \"\"\n"
		"}\n");
	CRC_ResetSourceChecksums();
	Mock_Reset(context);
	assert_int_equal(AAS_SoundSubsystem_Init(), BLERR_NOERROR);
	assert_int_equal(AAS_SoundSubsystem_InfoCount(), 1U);

	/* Duration keeps its own [0, 10] clamp. */
	WriteSoundConfigFixture(config_path,
		"soundinfo\n"
		"{\n"
		"\tname \"parity/long.wav\"\n"
		"\tvolume 10.0\n"
		"\tduration 10.5\n"
		"\ttype 1\n"
		"\trecognition 0\n"
		"\tstring \"\"\n"
		"}\n");
	CRC_ResetSourceChecksums();
	Mock_Reset(context);
	assert_int_equal(AAS_SoundSubsystem_Init(), BLERR_NOERROR);
	assert_int_equal(AAS_SoundSubsystem_InfoCount(), 0U);
	assert_non_null(Mock_FindPrint(context,
		"float out of range [0.000000, 10.000000]"));

	assert_int_equal(remove(config_path), 0);
}

/*
=============
test_retail_soundconfig_reports_source_errors_and_load_provenance

Pin SourceError routing at 0x1001c9bd, which adds the "file %s, line %d: "
prefix from 0x1003924a, and the loose-file "loaded %s" report at 0x1001c9e7.
=============
*/
static void test_retail_soundconfig_reports_source_errors_and_load_provenance(
	void **state)
{
	aas_debug_test_context_t *context = (aas_debug_test_context_t *)*state;
	const char *config_path =
		PROJECT_SOURCE_DIR "/aas_sound_parity_errors.c";
	WriteSoundConfigFixture(config_path,
		"soundinfo\n"
		"{\n"
		"\tname \"parity/one.wav\"\n"
		"\tvolume 40\n"
		"\tduration 1\n"
		"\ttype 1\n"
		"\trecognition 0\n"
		"\tstring \"\"\n"
		"}\n"
		"weaponinfo\n");

	LibVarSet("max_soundinfo", "4");
	LibVarSet("max_aassounds", "8");
	LibVarSet("soundconfig", config_path);
	Mock_Reset(context);
	assert_int_equal(AAS_SoundSubsystem_Init(), BLERR_NOERROR);
	const char *unknown = Mock_FindPrint(context,
		"unknown definition weaponinfo");
	assert_non_null(unknown);
	assert_non_null(strstr(unknown, "line 10:"));
	assert_non_null(strstr(unknown, "file "));
	assert_null(Mock_FindPrint(context, "loaded "));

	/* Over-capacity is reported through the same helper. */
	LibVarSet("max_soundinfo", "0");
	CRC_ResetSourceChecksums();
	Mock_Reset(context);
	assert_int_equal(AAS_SoundSubsystem_Init(), BLERR_NOERROR);
	const char *overflow = Mock_FindPrint(context,
		"more than 0 sound infos defined");
	assert_non_null(overflow);
	assert_non_null(strstr(overflow, "line 1:"));

	/* A clean parse reports the requested name at PRT_MESSAGE. */
	WriteSoundConfigFixture(config_path,
		"soundinfo\n"
		"{\n"
		"\tname \"parity/one.wav\"\n"
		"\tvolume 40\n"
		"\tduration 1\n"
		"\ttype 1\n"
		"\trecognition 0\n"
		"\tstring \"\"\n"
		"}\n");
	LibVarSet("max_soundinfo", "4");
	CRC_ResetSourceChecksums();
	Mock_Reset(context);
	assert_int_equal(AAS_SoundSubsystem_Init(), BLERR_NOERROR);
	char expected[512];
	snprintf(expected, sizeof(expected), "loaded %s\n", config_path);
	const char *loaded = Mock_FindPrint(context, expected);
	assert_non_null(loaded);
	assert_int_equal(
		context->prints[context->print_count - 1U].priority, PRT_MESSAGE);

	assert_int_equal(remove(config_path), 0);
}

/*
=============
test_retail_soundindex_table_folds_case

Pin the _stricmp mapping at 0x1001d1c9: asset names differing only in case
still resolve, and no path normalisation is applied.
=============
*/
static void test_retail_soundindex_table_folds_case(void **state)
{
	(void)state;
	const char *config_path =
		PROJECT_SOURCE_DIR "/aas_sound_parity_case.c";
	WriteSoundConfigFixture(config_path,
		"soundinfo\n"
		"{\n"
		"\tname \"player/step1.wav\"\n"
		"\tvolume 80\n"
		"\tduration 0.2\n"
		"\ttype 1\n"
		"\trecognition 0\n"
		"\tstring \"\"\n"
		"}\n");

	LibVarSet("max_soundinfo", "4");
	LibVarSet("max_aassounds", "8");
	LibVarSet("soundconfig", config_path);
	assert_int_equal(AAS_SoundSubsystem_Init(), BLERR_NOERROR);

	char mixed_case[] = "Player/Step1.wav";
	char backslashed[] = "player\\step1.wav";
	char prefixed[] = "sound/player/step1.wav";
	char *assets[] = {mixed_case, backslashed, prefixed};
	assert_true(AAS_SoundSubsystem_RegisterMapAssets(3, assets));

	const aas_soundinfo_t *folded = AAS_SoundSubsystem_InfoForSoundIndex(0);
	assert_non_null(folded);
	assert_string_equal(folded->name, "player/step1.wav");
	assert_int_equal(AAS_SoundSubsystem_SoundTypeForIndex(0), 1);
	/* Retail folds case only: separators and prefixes are not normalised. */
	assert_null(AAS_SoundSubsystem_InfoForSoundIndex(1));
	assert_null(AAS_SoundSubsystem_InfoForSoundIndex(2));

	assert_int_equal(remove(config_path), 0);
}

/*
=============
main

Run the focused AAS debug and sampling parity tests.
=============
*/
int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_bot_test_dumps_area_info, setup_aas_debug, teardown_aas_debug),
		cmocka_unit_test_setup_teardown(test_bot_test_rejects_area_lump_count,
			setup_aas_debug,
			teardown_aas_debug),
		cmocka_unit_test_setup_teardown(test_bot_test_resolves_point_through_aas_tree,
			setup_aas_debug,
			teardown_aas_debug),
        cmocka_unit_test_setup_teardown(test_aas_showpath_reports_path, setup_aas_debug, teardown_aas_debug),
		cmocka_unit_test_setup_teardown(test_aas_showpath_reports_unreachable_span,
			setup_aas_debug,
			teardown_aas_debug),
        cmocka_unit_test_setup_teardown(test_aas_showareas_lists_requested_areas, setup_aas_debug, teardown_aas_debug),
		cmocka_unit_test_setup_teardown(test_aas_showareas_excludes_area_lump_count,
			setup_aas_debug,
			teardown_aas_debug),
        cmocka_unit_test_setup_teardown(test_aas_sample_helpers_use_loaded_planes_and_area_settings,
                                        setup_aas_debug,
                                        teardown_aas_debug),
        cmocka_unit_test_setup_teardown(test_aas_predict_route_uses_reachability_cache_and_stop_events,
                                        setup_aas_debug,
                                        teardown_aas_debug),
		cmocka_unit_test_setup_teardown(
			test_retail_alternative_route_reuses_map_scratch_and_face_order,
			setup_aas_debug,
			teardown_aas_debug),
        cmocka_unit_test_setup_teardown(test_aas_predict_route_checks_generated_reachability_pass_areas,
                                        setup_aas_debug,
                                        teardown_aas_debug),
        cmocka_unit_test_setup_teardown(test_aas_trace_client_bbox_uses_presence_and_tree_hits,
                                        setup_aas_debug,
                                        teardown_aas_debug),
		cmocka_unit_test_setup_teardown(
			test_retail_client_bbox_trace_plane_band_clamp_and_presence_mask,
			setup_aas_debug,
			teardown_aas_debug),
		cmocka_unit_test_setup_teardown(
			test_retail_client_bbox_trace_preserves_link_order_and_pass_boundary,
			setup_aas_debug,
			teardown_aas_debug),
		cmocka_unit_test_setup_teardown(
			test_retail_client_bbox_trace_guards_64_entry_stack_geometry,
			setup_aas_debug,
			teardown_aas_debug),
        cmocka_unit_test_setup_teardown(test_aas_trace_client_bbox_hits_linked_entities,
                                        setup_aas_debug,
                                        teardown_aas_debug),
		cmocka_unit_test_setup_teardown(test_aas_bsp_model_bounds_and_entity_collision_use_brush_lumps,
		                                setup_aas_debug,
		                                teardown_aas_debug),
		cmocka_unit_test_setup_teardown(test_retail_route_cache_layout_tables_and_lookup,
			setup_aas_debug,
			teardown_aas_debug),
		cmocka_unit_test_setup_teardown(test_retail_route_cache_strict_fifteen_second_aging,
			setup_aas_debug,
			teardown_aas_debug),
		cmocka_unit_test_setup_teardown(test_retail_area_cache_propagates_fifo_and_counts,
			setup_aas_debug,
			teardown_aas_debug),
		cmocka_unit_test_setup_teardown(test_retail_area_cache_filters_travel_contents_and_cluster,
			setup_aas_debug,
			teardown_aas_debug),
		cmocka_unit_test_setup_teardown(test_retail_area_cache_wraps_ushort_travel_times,
			setup_aas_debug,
			teardown_aas_debug),
		cmocka_unit_test_setup_teardown(test_retail_portal_cache_propagates_both_sides_and_lifecycle,
			setup_aas_debug,
			teardown_aas_debug),
		cmocka_unit_test_setup_teardown(test_retail_portal_cache_inherits_travel_and_content_masks,
			setup_aas_debug,
			teardown_aas_debug),
		cmocka_unit_test_setup_teardown(test_retail_portal_cache_wraps_ushort_costs,
			setup_aas_debug,
			teardown_aas_debug),
		cmocka_unit_test_setup_teardown(test_retail_portal_cache_retains_equal_fifo_side,
			setup_aas_debug,
			teardown_aas_debug),
		cmocka_unit_test_setup_teardown(test_retail_route_query_same_cluster_origin_and_first_hop,
			setup_aas_debug,
			teardown_aas_debug),
		cmocka_unit_test_setup_teardown(test_retail_route_query_filters_and_projects_travel_flags,
			setup_aas_debug,
			teardown_aas_debug),
		cmocka_unit_test_setup_teardown(test_retail_route_query_crosses_portals_and_adjusts_portal_sides,
			setup_aas_debug,
			teardown_aas_debug),
		cmocka_unit_test_setup_teardown(test_retail_route_query_wraps_cross_cluster_ushort_sum,
			setup_aas_debug,
			teardown_aas_debug),
		cmocka_unit_test_setup_teardown(test_retail_route_query_preserves_guards_and_same_area_order,
			setup_aas_debug,
			teardown_aas_debug),
		cmocka_unit_test_setup_teardown(
			test_retail_visibility_setup_enumeration_and_next_entity,
			setup_aas_debug,
			teardown_aas_debug),
		cmocka_unit_test_setup_teardown(
			test_retail_entity_visibility_quantized_inclusive_fov,
			setup_aas_debug,
			teardown_aas_debug),
		cmocka_unit_test_setup_teardown(
			test_retail_entity_visibility_three_sample_order,
			setup_aas_debug,
			teardown_aas_debug),
		cmocka_unit_test_setup_teardown(
			test_retail_entity_visibility_direct_hit,
			setup_aas_debug,
			teardown_aas_debug),
		cmocka_unit_test_setup_teardown(
			test_retail_entity_visibility_fluid_reversal,
			setup_aas_debug,
			teardown_aas_debug),
		cmocka_unit_test_setup_teardown(
			test_retail_entity_visibility_translucent_fluid_continuation,
			setup_aas_debug,
			teardown_aas_debug),
		cmocka_unit_test_setup_teardown(
			test_retail_sound_emit_replaces_active_record_on_zero_timeofs,
			setup_aas_sound,
			teardown_aas_sound),
		cmocka_unit_test_setup_teardown(
			test_retail_soundinfo_records_are_zero_filled,
			setup_aas_sound,
			teardown_aas_sound),
		cmocka_unit_test_setup_teardown(
			test_retail_soundinfo_bounds_reject_out_of_range_values,
			setup_aas_sound,
			teardown_aas_sound),
		cmocka_unit_test_setup_teardown(
			test_retail_soundconfig_reports_source_errors_and_load_provenance,
			setup_aas_sound,
			teardown_aas_sound),
		cmocka_unit_test_setup_teardown(
			test_retail_soundindex_table_folds_case,
			setup_aas_sound,
			teardown_aas_sound),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
