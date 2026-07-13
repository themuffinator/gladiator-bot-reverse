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
#include "botlib/interface/botlib_interface.h"
#include "q2bridge/bridge.h"

#define ARRAY_LEN(x) (sizeof(x) / sizeof((x)[0]))

typedef struct captured_print_s
{
    int priority;
    char message[256];
} captured_print_t;

typedef struct aas_debug_test_context_s
{
    botlib_import_table_t imports;
    bot_import_t bridge_imports;
    captured_print_t prints[64];
    size_t print_count;
    bsp_trace_t bridge_trace_result;
    int bridge_trace_count;
    vec3_t bridge_trace_start;
    vec3_t bridge_trace_mins;
    vec3_t bridge_trace_maxs;
    vec3_t bridge_trace_end;
    int bridge_trace_passent;
    int bridge_trace_contentmask;
    int bridge_point_contents_result;
    int bridge_point_contents_count;
    vec3_t bridge_point_contents_point;
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

    g_active_context->bridge_point_contents_count += 1;
    if (point != NULL)
    {
        VectorCopy(point, g_active_context->bridge_point_contents_point);
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
    context->bridge_trace_count = 0;
    VectorClear(context->bridge_trace_start);
    VectorClear(context->bridge_trace_mins);
    VectorClear(context->bridge_trace_maxs);
    VectorClear(context->bridge_trace_end);
    context->bridge_trace_passent = 0;
    context->bridge_trace_contentmask = 0;
    context->bridge_point_contents_result = 0;
    context->bridge_point_contents_count = 0;
    VectorClear(context->bridge_point_contents_point);
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

static void BuildMockMap(aas_debug_test_context_t *context)
{
    assert_non_null(context);

    aasworld.loaded = qtrue;
    aasworld.initialized = qtrue;
    aasworld.numAreas = 4;
    aasworld.numReachability = 2;
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
        settings->numreachableareas = 1;
        settings->firstreachablearea = 0;
        settings->contents = 0;
    }

    context->reachability = (aas_reachability_t *)calloc((size_t)aasworld.numReachability, sizeof(aas_reachability_t));
    assert_non_null(context->reachability);
    aasworld.reachability = context->reachability;

    aas_reachability_t *first = &context->reachability[0];
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

    aas_reachability_t *second = &context->reachability[1];
    second->facenum = 2;
    second->areanum = 3;
    second->traveltype = 9;
    second->traveltime = 45;
    second->start[0] = 100.0f;
    second->start[1] = 0.0f;
    second->start[2] = 0.0f;
    second->end[0] = 200.0f;
    second->end[1] = 0.0f;
    second->end[2] = 0.0f;

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
    AAS_InvalidateRouteCache();
}

static void ConfigureRoutePassAreaFixture(aas_debug_test_context_t *context)
{
    assert_non_null(context);

    AAS_ClearReachabilityData();
    AAS_FreeAllRoutingCaches();

    free(context->reachability);
    aasworld.numReachability = 2;
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
	VectorSet(context->bspModels[1].mins, -4.0f, -12.0f, -8.0f);
	VectorSet(context->bspModels[1].maxs, 4.0f, 12.0f, 8.0f);
	VectorClear(context->bspModels[1].origin);
	context->bspModels[1].headnode = -1;

	aasworld.numBspLeaves = 1;
	context->bspLeaves = (aas_bspleaf_t *)calloc((size_t)aasworld.numBspLeaves, sizeof(aas_bspleaf_t));
	assert_non_null(context->bspLeaves);
	aasworld.bspLeaves = context->bspLeaves;
	context->bspLeaves[0].contents = CONTENTS_SOLID;
	context->bspLeaves[0].firstleafbrush = 0;
	context->bspLeaves[0].numleafbrushes = 1;

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

static int teardown_aas_debug(void **state)
{
    aas_debug_test_context_t *context = (aas_debug_test_context_t *)(*state);
    BotInterface_SetImportTable(NULL);
    Q2Bridge_ClearImportTable();

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
    assert_non_null(Mock_FindPrint(context, "reach[1]: 2 -> 3"));
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

    context->bridge_point_contents_result = CONTENTS_WATER | CONTENTS_SLIME;
    assert_int_equal(AAS_PointContents(front_point), CONTENTS_WATER | CONTENTS_SLIME);
    assert_int_equal(context->bridge_point_contents_count, 1);
    assert_float_equal(context->bridge_point_contents_point[0], 100.0f, 0.001f);

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
    assert_int_equal(copied_reach.areanum, 3);
    assert_int_equal(copied_reach.traveltype, 9);
    assert_int_equal(copied_reach.traveltime, 45);

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

    vec3_t goal = { 200.0f, 0.0f, 0.0f };
    aas_altroutegoal_t altroutegoals[4];
    memset(altroutegoals, 0, sizeof(altroutegoals));
    int numaltroutegoals = AAS_AlternativeRouteGoals(origin,
                                                     1,
                                                     goal,
                                                     3,
                                                     TFL_DEFAULT,
                                                     altroutegoals,
                                                     (int)ARRAY_LEN(altroutegoals),
                                                     ALTROUTEGOAL_ALL);
    assert_int_equal(numaltroutegoals, 1);
    assert_int_equal(altroutegoals[0].areanum, 2);
    assert_float_equal(altroutegoals[0].origin[0], 100.0f, 0.001f);
    assert_int_equal(altroutegoals[0].starttraveltime, 31);
    assert_int_equal(altroutegoals[0].goaltraveltime, 45);
    assert_int_equal(altroutegoals[0].extratraveltime, 0);

    assert_int_equal(AAS_AlternativeRouteGoals(origin,
                                               1,
                                               goal,
                                               3,
                                               TFL_DEFAULT,
                                               altroutegoals,
                                               (int)ARRAY_LEN(altroutegoals),
                                               ALTROUTEGOAL_CLUSTERPORTALS),
                     0);

    context->areasettings[2].contents |= AAS_AREACONTENTS_CLUSTERPORTAL;
    memset(altroutegoals, 0, sizeof(altroutegoals));
    numaltroutegoals = AAS_AlternativeRouteGoals(origin,
                                                1,
                                                goal,
                                                3,
                                                TFL_DEFAULT,
                                                altroutegoals,
                                                (int)ARRAY_LEN(altroutegoals),
                                                ALTROUTEGOAL_CLUSTERPORTALS);
    assert_int_equal(numaltroutegoals, 1);
    assert_int_equal(altroutegoals[0].areanum, 2);
    context->areasettings[2].contents = 0;

    assert_int_equal(AAS_BridgeWalkable(2), qfalse);
    assert_int_equal(AAS_AreaVisible(1, 2), qfalse);

    context->areasettings[2].areaflags |= AAS_AREA_LIQUID;
    int randomGoalArea = 0;
    vec3_t randomGoalOrigin;
    VectorClear(randomGoalOrigin);
    assert_true(AAS_RandomGoalArea(1, TFL_DEFAULT, &randomGoalArea, randomGoalOrigin));
    assert_int_equal(randomGoalArea, 2);
    assert_float_equal(randomGoalOrigin[0], 100.0f, 0.001f);
    context->areasettings[2].areaflags = 0;

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
    assert_int_equal(solid.planenum, 0);

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

    assert_int_equal(AAS_UpdateEntity(4, &entity), BLERR_NOERROR);
    assert_non_null(aasworld.entities);
    assert_true(aasworld.areaEntityListCount > 2U);
    assert_non_null(aasworld.areaEntityLists[2]);

    vec3_t start = { 0.0f, 0.0f, 0.0f };
    vec3_t end = { 100.0f, 0.0f, 0.0f };
    vec3_t boxmins;
    vec3_t boxmaxs;
    AAS_PresenceTypeBoundingBox(PRESENCE_CROUCH, boxmins, boxmaxs);

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
    assert_float_equal(entity_trace.fraction, 0.56f, 0.001f);
    assert_float_equal(entity_trace.endpos[0], 56.0f, 0.001f);
    assert_float_equal(entity_trace.plane.normal[0], -1.0f, 0.001f);
    assert_int_equal(entity_trace.ent, 4);

    aas_trace_t hit = AAS_TraceClientBBox(start, end, PRESENCE_CROUCH, 99);
    assert_false(hit.startsolid);
    assert_float_equal(hit.fraction, 0.56f, 0.001f);
    assert_float_equal(hit.endpos[0], 56.0f, 0.001f);
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
    assert_int_equal(AAS_EntityModelNum(5), 8);
    assert_int_equal(AAS_ModelNumForEntity(5), 7);

    vec3_t mover_mins;
    vec3_t mover_maxs;
    AAS_EntitySize(5, mover_mins, mover_maxs);
    assert_float_equal(mover_mins[0], -16.0f, 0.001f);
    assert_float_equal(mover_maxs[2], 32.0f, 0.001f);

    VectorClear(mover_origin);
    assert_true(AAS_OriginOfMoverWithModelNum(7, mover_origin));
    assert_float_equal(mover_origin[0], 25.0f, 0.001f);
    assert_false(AAS_OriginOfMoverWithModelNum(99, mover_origin));

    assert_int_equal(AAS_UpdateEntity(5, NULL), BLERR_NOERROR);
    assert_int_equal(AAS_UpdateEntity(4, NULL), BLERR_NOERROR);
    free(aasworld.entities);
    free(aasworld.areaEntityLists);
    aasworld.entities = NULL;
    aasworld.maxEntities = 0;
    aasworld.areaEntityLists = NULL;
    aasworld.areaEntityListCount = 0U;
}

static void test_aas_bsp_model_bounds_and_entity_collision_use_brush_lumps(void **state)
{
	aas_debug_test_context_t *context = (aas_debug_test_context_t *)(*state);
	Mock_Reset(context);
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
	assert_int_equal(model_trace.contents, CONTENTS_SOLID);

	AASEntityFrame mover;
	memset(&mover, 0, sizeof(mover));
	mover.solid = SOLID_BSP;
	mover.modelindex = 2;
	VectorSet(mover.origin, 25.0f, 0.0f, 0.0f);
	VectorSet(mover.previous_origin, 25.0f, 0.0f, 0.0f);
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

	assert_int_equal(AAS_UpdateEntity(6, NULL), BLERR_NOERROR);
	free(aasworld.entities);
	free(aasworld.areaEntityLists);
	aasworld.entities = NULL;
	aasworld.maxEntities = 0;
	aasworld.areaEntityLists = NULL;
	aasworld.areaEntityListCount = 0U;
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_bot_test_dumps_area_info, setup_aas_debug, teardown_aas_debug),
        cmocka_unit_test_setup_teardown(test_aas_showpath_reports_path, setup_aas_debug, teardown_aas_debug),
        cmocka_unit_test_setup_teardown(test_aas_showareas_lists_requested_areas, setup_aas_debug, teardown_aas_debug),
        cmocka_unit_test_setup_teardown(test_aas_sample_helpers_use_loaded_planes_and_area_settings,
                                        setup_aas_debug,
                                        teardown_aas_debug),
        cmocka_unit_test_setup_teardown(test_aas_predict_route_uses_reachability_cache_and_stop_events,
                                        setup_aas_debug,
                                        teardown_aas_debug),
        cmocka_unit_test_setup_teardown(test_aas_predict_route_checks_generated_reachability_pass_areas,
                                        setup_aas_debug,
                                        teardown_aas_debug),
        cmocka_unit_test_setup_teardown(test_aas_trace_client_bbox_uses_presence_and_tree_hits,
                                        setup_aas_debug,
                                        teardown_aas_debug),
        cmocka_unit_test_setup_teardown(test_aas_trace_client_bbox_hits_linked_entities,
                                        setup_aas_debug,
                                        teardown_aas_debug),
		cmocka_unit_test_setup_teardown(test_aas_bsp_model_bounds_and_entity_collision_use_brush_lumps,
		                                setup_aas_debug,
		                                teardown_aas_debug),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
