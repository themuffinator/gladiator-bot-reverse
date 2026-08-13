#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <cmocka.h>

#include "botlib/aas/aas_local.h"
#include "botlib/common/l_libvar.h"
#include "botlib/common/l_memory.h"

/*
=============
ResetReachWorld

Drop every generator allocation and clear the synthetic AAS world between
retail reachability contract cases.
=============
*/
static void ResetReachWorld(void)
{
	AAS_ShutDownReachabilityHeap();
	AAS_ClearReachabilityData();
	if (aasworld.reachability != NULL)
	{
		FreeMemory(aasworld.reachability);
	}
	memset(&aasworld, 0, sizeof(aasworld));
}

/*
=============
test_reach_teleport_admits_grounded_source_areas

Retail (sub_10015bb0 @0x10015f21) admits teleporter source areas with
AAS_AreaGrounded, not with teleporter area contents. Quake II never marks an
area with AAS_AREACONTENTS_TELEPORTER, so the contents test generated no links
at all on shipped maps.
=============
*/
static void test_reach_teleport_admits_grounded_source_areas(void **state)
{
	(void)state;
	const char entity_data[] =
		"{\n"
		"\"classname\" \"misc_teleporter\"\n"
		"\"origin\" \"0 0 0\"\n"
		"\"target\" \"teleport_destination\"\n"
		"}\n"
		"{\n"
		"\"classname\" \"misc_teleporter_dest\"\n"
		"\"targetname\" \"teleport_destination\"\n"
		"\"origin\" \"100 0 16\"\n"
		"}\n";

	for (int grounded = 0; grounded < 2; ++grounded)
	{
		memset(&aasworld, 0, sizeof(aasworld));
		aas_bspentity_t *entities = AAS_ParseBSPEntities(entity_data,
			sizeof(entity_data) - 1U);
		assert_non_null(entities);

		aas_area_t areas[3] = {0};
		aas_areasettings_t settings[3] = {0};
		aas_node_t nodes[3] = {0};
		aas_plane_t planes[2] = {0};
		areas[1].areanum = 1;
		VectorSet(areas[1].mins, -64.0f, -64.0f, -64.0f);
		VectorSet(areas[1].maxs, 49.0f, 64.0f, 64.0f);
		areas[2].areanum = 2;
		VectorSet(areas[2].mins, 50.0f, -64.0f, 0.0f);
		VectorSet(areas[2].maxs, 160.0f, 64.0f, 128.0f);
		/*
		 * The source area carries teleporter contents in both passes; only the
		 * grounded areaflag decides whether retail creates the link.
		 */
		settings[1].contents = AAS_AREACONTENTS_TELEPORTER;
		settings[1].areaflags = grounded ? AAS_AREA_GROUNDED : 0;
		settings[1].presencetype = PRESENCE_CROUCH;
		settings[2].areaflags = AAS_AREA_GROUNDED;
		settings[2].presencetype = PRESENCE_CROUCH;
		nodes[1].planenum = 0;
		nodes[1].children[0] = 2;
		nodes[1].children[1] = -1;
		nodes[2].planenum = 1;
		nodes[2].children[0] = -2;
		nodes[2].children[1] = 0;
		planes[0].normal[0] = 1.0f;
		planes[0].dist = 50.0f;
		planes[1].normal[2] = 1.0f;

		aasworld.loaded = qtrue;
		aasworld.numAreas = 3;
		aasworld.areas = areas;
		aasworld.numAreaSettings = 3;
		aasworld.areasettings = settings;
		aasworld.numNodes = 3;
		aasworld.nodes = nodes;
		aasworld.numPlanes = 2;
		aasworld.planes = planes;

		AAS_InitReachability();
		if (grounded)
		{
			assert_int_equal(AAS_Reachability_TeleportEntityList(entities), 1);
			assert_true(AAS_ReachabilityExists(1, 2));
			AAS_StoreReachability();
			assert_int_equal(aasworld.numReachability, 2);
			assert_int_equal(settings[1].numreachableareas, 1);
			assert_int_equal(aasworld.reachability[1].areanum, 2);
			assert_int_equal(aasworld.reachability[1].traveltype,
				TRAVEL_TELEPORT);
			assert_int_equal(aasworld.reachability[1].traveltime, 50);
		}
		else
		{
			assert_int_equal(AAS_Reachability_TeleportEntityList(entities), 0);
			assert_false(AAS_ReachabilityExists(1, 2));
		}

		AAS_FreeBSPEntities(entities);
		ResetReachWorld();
	}
}

/*
=============
BuildAdjacentEdgeWorld

Populate the paired coplanar boundary edges shared by the retail step,
barrier, downhill-walk, water-jump, and walk-off-ledge branches.
=============
*/
static void BuildAdjacentEdgeWorld(float height,
	qboolean source_liquid,
	aas_area_t *areas,
	aas_areasettings_t *settings,
	aas_vertex_t *vertexes,
	aas_edge_t *edges,
	int *edge_index,
	aas_face_t *faces,
	int *face_index,
	aas_plane_t *planes)
{
	memset(&aasworld, 0, sizeof(aasworld));

	VectorSet(vertexes[0], 0.0f, 0.0f, 0.0f);
	VectorSet(vertexes[1], 0.0f, 10.0f, 0.0f);
	VectorSet(vertexes[2], 0.0f, 0.0f, height);
	VectorSet(vertexes[3], 0.0f, 10.0f, height);

	edges[0].v[0] = 0;
	edges[0].v[1] = 0;
	edges[1].v[0] = 0;
	edges[1].v[1] = 1;
	edges[2].v[0] = 2;
	edges[2].v[1] = 3;
	edge_index[0] = -1;
	edge_index[1] = 2;
	face_index[0] = source_liquid ? -1 : 1;
	face_index[1] = 2;

	areas[1].areanum = 1;
	areas[1].firstface = 0;
	areas[1].numfaces = 1;
	VectorSet(areas[1].mins, -10.0f, -1.0f, -1.0f);
	VectorSet(areas[1].maxs, 0.0f, 11.0f, 32.0f);
	areas[2].areanum = 2;
	areas[2].firstface = 1;
	areas[2].numfaces = 1;
	VectorSet(areas[2].mins, 0.0f, -1.0f, height - 1.0f);
	VectorSet(areas[2].maxs, 10.0f, 11.0f,
		height + 32.0f > 4.0f ? height + 32.0f : 4.0f);
	settings[1].areaflags = source_liquid ? AAS_AREA_LIQUID : AAS_AREA_GROUNDED;
	settings[1].presencetype = PRESENCE_NORMAL;
	settings[2].areaflags = AAS_AREA_GROUNDED;
	settings[2].presencetype = PRESENCE_NORMAL;
	faces[1].planenum = 0;
	faces[1].faceflags = source_liquid ? 0 : AAS_FACE_GROUND;
	faces[1].firstedge = 0;
	faces[1].numedges = 1;
	faces[2].planenum = 0;
	faces[2].faceflags = AAS_FACE_GROUND;
	faces[2].firstedge = 1;
	faces[2].numedges = 1;
	planes[0].normal[2] = 1.0f;
	planes[1].normal[2] = 1.0f;

	aasworld.loaded = qtrue;
	aasworld.numAreas = 3;
	aasworld.areas = areas;
	aasworld.numAreaSettings = 3;
	aasworld.areasettings = settings;
	aasworld.numVertexes = 4;
	aasworld.vertexes = vertexes;
	aasworld.numEdges = 3;
	aasworld.edges = edges;
	aasworld.edgeIndexSize = 2;
	aasworld.edgeIndex = edge_index;
	aasworld.numFaces = 3;
	aasworld.faces = faces;
	aasworld.faceIndexSize = 2;
	aasworld.faceIndex = face_index;
	aasworld.numPlanes = 2;
	aasworld.planes = planes;
}

/*
=============
test_reach_adjacent_edge_retail_travel_times

Pin the literal travel times retail stores for each adjacent-edge branch:
step 1 plus the two 400 unit penalties (0x100130c5, 0x10013116, 0x1001311f),
barrier jump 400 (0x100133ad), downhill walk 1 (0x1001347a), water jump 700
(0x10013280), and walk off ledge 100 (0x10013638).
=============
*/
static void test_reach_adjacent_edge_retail_travel_times(void **state)
{
	(void)state;
	typedef struct
	{
		float height;
		qboolean source_liquid;
		int traveltype;
		int traveltime;
	} adjacent_case_t;
	/*
	 * The synthetic world has no geometry beyond the shared edge, so
	 * AAS_NearbySolidOrGap reports a gap and only the ground-face-area
	 * penalty applies to the step link: 1 + 400.
	 */
	const adjacent_case_t cases[] = {
		{8.0f, qfalse, TRAVEL_WALK, 401},
		{30.0f, qfalse, TRAVEL_BARRIERJUMP, 400},
		{-8.0f, qfalse, TRAVEL_WALK, 1},
		{30.0f, qtrue, TRAVEL_WATERJUMP, 700},
		{-40.0f, qfalse, TRAVEL_WALKOFFLEDGE, 100}
	};

	for (size_t case_index = 0;
		case_index < sizeof(cases) / sizeof(cases[0]);
		++case_index)
	{
		const adjacent_case_t *testcase = &cases[case_index];
		aas_area_t areas[3] = {0};
		aas_areasettings_t settings[3] = {0};
		aas_vertex_t vertexes[4] = {0};
		aas_edge_t edges[3] = {0};
		int edge_index[2] = {0};
		aas_face_t faces[3] = {0};
		int face_index[2] = {0};
		aas_plane_t planes[2] = {0};
		BuildAdjacentEdgeWorld(testcase->height, testcase->source_liquid,
			areas, settings, vertexes, edges, edge_index, faces, face_index,
			planes);

		AAS_InitReachability();
		assert_true(AAS_Reachability_Step_Barrier_WaterJump_WalkOffLedge(1, 2));
		AAS_StoreReachability();
		assert_int_equal(aasworld.numReachability, 2);
		assert_int_equal(aasworld.reachability[1].areanum, 2);
		assert_int_equal(aasworld.reachability[1].traveltype,
			testcase->traveltype);
		assert_int_equal(aasworld.reachability[1].traveltime,
			testcase->traveltime);

		ResetReachWorld();
	}
}

/*
=============
test_reach_walk_off_ledge_requires_fall_damage_gate

Retail only reaches the walk-off-ledge branch when
-AAS_FallDamageDistance() < dist or the destination is swimmable
(0x100134bc-0x100134d9); a lethal drop into dry land produces no link at all.
=============
*/
static void test_reach_walk_off_ledge_requires_fall_damage_gate(void **state)
{
	(void)state;
	/* sv_gravity 800 puts AAS_FallDamageDistance() at 187 units. */
	assert_int_equal(AAS_FallDamageDistance(), 187);

	aas_area_t areas[3] = {0};
	aas_areasettings_t settings[3] = {0};
	aas_vertex_t vertexes[4] = {0};
	aas_edge_t edges[3] = {0};
	int edge_index[2] = {0};
	aas_face_t faces[3] = {0};
	int face_index[2] = {0};
	aas_plane_t planes[2] = {0};
	BuildAdjacentEdgeWorld(-400.0f, qfalse, areas, settings, vertexes, edges,
		edge_index, faces, face_index, planes);

	AAS_InitReachability();
	assert_false(AAS_Reachability_Step_Barrier_WaterJump_WalkOffLedge(1, 2));
	assert_false(AAS_ReachabilityExists(1, 2));
	ResetReachWorld();

	/* The same drop into a swim area still links, at the flat cost of 100. */
	BuildAdjacentEdgeWorld(-400.0f, qfalse, areas, settings, vertexes, edges,
		edge_index, faces, face_index, planes);
	settings[2].areaflags = AAS_AREA_GROUNDED | AAS_AREA_LIQUID;
	settings[2].presencetype = PRESENCE_NORMAL;

	AAS_InitReachability();
	assert_true(AAS_Reachability_Step_Barrier_WaterJump_WalkOffLedge(1, 2));
	AAS_StoreReachability();
	assert_int_equal(aasworld.numReachability, 2);
	assert_int_equal(aasworld.reachability[1].traveltype, TRAVEL_WALKOFFLEDGE);
	assert_int_equal(aasworld.reachability[1].traveltime, 100);
	ResetReachWorld();
}

/*
=============
test_reach_equal_floor_retail_travel_time

Retail seeds the equal-floor link with 1, adds 300 for a crouch-only
destination (0x1001200a) and two 100 unit penalties afterwards
(0x10012037, 0x10012040).
=============
*/
static void test_reach_equal_floor_retail_travel_time(void **state)
{
	(void)state;
	memset(&aasworld, 0, sizeof(aasworld));

	aas_area_t areas[3] = {0};
	aas_areasettings_t settings[3] = {0};
	aas_vertex_t vertexes[4] = {
		{0.0f, 0.0f, 0.0f},
		{0.0f, 10.0f, 0.0f},
		{-10.0f, 0.0f, 0.0f},
		{10.0f, 0.0f, 0.0f}
	};
	aas_edge_t edges[6] = {
		{{0, 0}}, {{0, 1}}, {{1, 2}},
		{{2, 0}}, {{0, 3}}, {{3, 1}}
	};
	int edge_index[6] = {-1, 2, 3, 1, 4, 5};
	aas_face_t faces[3] = {0};
	int face_index[2] = {1, 2};
	aas_plane_t planes[1] = {0};

	areas[1].areanum = 1;
	areas[1].numfaces = 1;
	areas[1].firstface = 0;
	VectorSet(areas[1].mins, -10.0f, 0.0f, 0.0f);
	VectorSet(areas[1].maxs, 0.0f, 10.0f, 0.0f);
	areas[2].areanum = 2;
	areas[2].numfaces = 1;
	areas[2].firstface = 1;
	VectorSet(areas[2].mins, 0.0f, 0.0f, 0.0f);
	VectorSet(areas[2].maxs, 10.0f, 10.0f, 0.0f);
	settings[1].areaflags = AAS_AREA_GROUNDED;
	settings[1].presencetype = PRESENCE_NORMAL;
	settings[2].areaflags = AAS_AREA_GROUNDED;
	settings[2].presencetype = PRESENCE_CROUCH;
	faces[1].planenum = 0;
	faces[1].faceflags = AAS_FACE_GROUND;
	faces[1].numedges = 3;
	faces[1].firstedge = 0;
	faces[2].planenum = 0;
	faces[2].faceflags = AAS_FACE_GROUND;
	faces[2].numedges = 3;
	faces[2].firstedge = 3;
	planes[0].normal[2] = 1.0f;

	aasworld.loaded = qtrue;
	aasworld.numAreas = 3;
	aasworld.areas = areas;
	aasworld.numAreaSettings = 3;
	aasworld.areasettings = settings;
	aasworld.numVertexes = 4;
	aasworld.vertexes = vertexes;
	aasworld.numEdges = 6;
	aasworld.edges = edges;
	aasworld.edgeIndexSize = 6;
	aasworld.edgeIndex = edge_index;
	aasworld.numFaces = 3;
	aasworld.faces = faces;
	aasworld.faceIndexSize = 2;
	aasworld.faceIndex = face_index;
	aasworld.numPlanes = 1;
	aasworld.planes = planes;

	/* The destination ground face is far below the retail 500 unit threshold. */
	AAS_InitReachability();
	assert_true(AAS_AreaGroundFaceArea(2) < 500.0f);
	assert_true(AAS_Reachability_EqualFloorHeight(1, 2));
	AAS_StoreReachability();
	assert_int_equal(aasworld.numReachability, 2);
	assert_int_equal(aasworld.reachability[1].traveltype, TRAVEL_WALK);
	assert_int_equal(aasworld.reachability[1].traveltime, 401);

	ResetReachWorld();
}

/*
=============
test_reach_equal_floor_tie_break_uses_vertex_sum_length

Retail measures the tie-break length on the SUM of the edge vertices before
halving it (0x10011d09 then 0x10011d1c), so the edge furthest from the world
origin wins even when it is the shorter of the two.
=============
*/
static void test_reach_equal_floor_tie_break_uses_vertex_sum_length(void **state)
{
	(void)state;
	memset(&aasworld, 0, sizeof(aasworld));

	aas_area_t areas[3] = {0};
	aas_areasettings_t settings[3] = {0};
	/*
	 * Near edge: length 2, vertex sum length 0.
	 * Far edge:  length 1, vertex sum length 201.
	 */
	aas_vertex_t vertexes[4] = {
		{0.0f, -1.0f, 0.0f},
		{0.0f, 1.0f, 0.0f},
		{0.0f, 100.0f, 0.0f},
		{0.0f, 101.0f, 0.0f}
	};
	aas_edge_t edges[3] = {{{0, 0}}, {{0, 1}}, {{2, 3}}};
	int edge_index[4] = {1, 2, 1, 2};
	aas_face_t faces[3] = {0};
	int face_index[2] = {1, 2};
	aas_plane_t planes[1] = {0};

	areas[1].areanum = 1;
	areas[1].numfaces = 1;
	areas[1].firstface = 0;
	VectorSet(areas[1].mins, -10.0f, -10.0f, -1.0f);
	VectorSet(areas[1].maxs, 0.0f, 110.0f, 10.0f);
	areas[2].areanum = 2;
	areas[2].numfaces = 1;
	areas[2].firstface = 1;
	VectorSet(areas[2].mins, 0.0f, -10.0f, -1.0f);
	VectorSet(areas[2].maxs, 10.0f, 110.0f, 10.0f);
	settings[1].areaflags = AAS_AREA_GROUNDED;
	settings[1].presencetype = PRESENCE_NORMAL;
	settings[2].areaflags = AAS_AREA_GROUNDED;
	settings[2].presencetype = PRESENCE_NORMAL;
	faces[1].planenum = 0;
	faces[1].faceflags = AAS_FACE_GROUND;
	faces[1].firstedge = 0;
	faces[1].numedges = 2;
	faces[2].planenum = 0;
	faces[2].faceflags = AAS_FACE_GROUND;
	faces[2].firstedge = 2;
	faces[2].numedges = 2;
	planes[0].normal[2] = 1.0f;

	aasworld.loaded = qtrue;
	aasworld.numAreas = 3;
	aasworld.areas = areas;
	aasworld.numAreaSettings = 3;
	aasworld.areasettings = settings;
	aasworld.numVertexes = 4;
	aasworld.vertexes = vertexes;
	aasworld.numEdges = 3;
	aasworld.edges = edges;
	aasworld.edgeIndexSize = 4;
	aasworld.edgeIndex = edge_index;
	aasworld.numFaces = 3;
	aasworld.faces = faces;
	aasworld.faceIndexSize = 2;
	aasworld.faceIndex = face_index;
	aasworld.numPlanes = 1;
	aasworld.planes = planes;

	AAS_InitReachability();
	assert_true(AAS_Reachability_EqualFloorHeight(1, 2));
	AAS_StoreReachability();
	assert_int_equal(aasworld.numReachability, 2);
	/* Edge 2 is the far, shorter edge; the true edge length would pick 1. */
	assert_int_equal(aasworld.reachability[1].edgenum, 2);
	assert_float_equal(aasworld.reachability[1].start[1], 100.5f, 0.0001f);

	ResetReachWorld();
}

/*
=============
test_reach_ladder_uses_truncated_magnitude

Retail resolves abs() on the ladder plane normals and the shared edge delta to
the integer abs (0x1001524b, 0x1001527d, 0x100152e5), so the predicate is
really |trunc(x)| < 1. A ladder plane sloped at normal z 0.8 with a 0.8 unit
edge delta still takes the symmetric two-way TRAVEL_LADDER branch.
=============
*/
static void test_reach_ladder_uses_truncated_magnitude(void **state)
{
	(void)state;
	memset(&aasworld, 0, sizeof(aasworld));

	aas_area_t areas[3] = {0};
	aas_areasettings_t settings[3] = {0};
	aas_vertex_t vertexes[2] = {
		{0.0f, 0.0f, 0.0f},
		{10.0f, 0.0f, 0.8f}
	};
	aas_edge_t edges[2] = {{{0, 0}}, {{0, 1}}};
	int edge_index[2] = {1, 1};
	aas_face_t faces[3] = {0};
	int face_index[2] = {1, 2};
	aas_plane_t planes[1] = {0};

	areas[1].areanum = 1;
	areas[1].firstface = 0;
	areas[1].numfaces = 1;
	VectorSet(areas[1].mins, -64.0f, -64.0f, -64.0f);
	VectorSet(areas[1].maxs, 64.0f, 64.0f, 64.0f);
	areas[2].areanum = 2;
	areas[2].firstface = 1;
	areas[2].numfaces = 1;
	VectorSet(areas[2].mins, -64.0f, -64.0f, -64.0f);
	VectorSet(areas[2].maxs, 64.0f, 64.0f, 64.0f);
	settings[1].areaflags = AAS_AREA_LADDER;
	settings[1].presencetype = PRESENCE_NORMAL;
	settings[2].areaflags = AAS_AREA_LADDER;
	settings[2].presencetype = PRESENCE_NORMAL;
	faces[1].planenum = 0;
	faces[1].faceflags = AAS_FACE_LADDER;
	faces[1].firstedge = 0;
	faces[1].numedges = 1;
	faces[2].planenum = 0;
	faces[2].faceflags = AAS_FACE_LADDER;
	faces[2].firstedge = 1;
	faces[2].numedges = 1;
	/* A sloped ladder face: fabsf(0.8f) would reject it as non-vertical. */
	planes[0].normal[1] = 0.6f;
	planes[0].normal[2] = 0.8f;

	aasworld.loaded = qtrue;
	aasworld.numAreas = 3;
	aasworld.areas = areas;
	aasworld.numAreaSettings = 3;
	aasworld.areasettings = settings;
	aasworld.numVertexes = 2;
	aasworld.vertexes = vertexes;
	aasworld.numEdges = 2;
	aasworld.edges = edges;
	aasworld.edgeIndexSize = 2;
	aasworld.edgeIndex = edge_index;
	aasworld.numFaces = 3;
	aasworld.faces = faces;
	aasworld.faceIndexSize = 2;
	aasworld.faceIndex = face_index;
	aasworld.numPlanes = 1;
	aasworld.planes = planes;

	AAS_InitReachability();
	assert_true(AAS_Reachability_Ladder(1, 2));
	assert_true(AAS_ReachabilityExists(1, 2));
	assert_true(AAS_ReachabilityExists(2, 1));
	AAS_StoreReachability();
	assert_int_equal(aasworld.numReachability, 3);
	assert_int_equal(settings[1].numreachableareas, 1);
	assert_int_equal(settings[2].numreachableareas, 1);
	assert_int_equal(
		aasworld.reachability[settings[1].firstreachablearea].traveltype,
		TRAVEL_LADDER);
	assert_int_equal(
		aasworld.reachability[settings[1].firstreachablearea].traveltime, 10);
	assert_int_equal(
		aasworld.reachability[settings[2].firstreachablearea].traveltype,
		TRAVEL_LADDER);

	ResetReachWorld();
}

/*
=============
test_reach_continue_init_creates_reachability_delay

Retail creates reachability_delay from its own "100" default string
(0x100189ad) rather than reading a host cvar, so the per-frame budget is
100 ms and the variable is present after the first generator frame.
=============
*/
static void test_reach_continue_init_creates_reachability_delay(void **state)
{
	(void)state;
	memset(&aasworld, 0, sizeof(aasworld));
	LibVarDeAllocAll();
	assert_null(LibVarGet("reachability_delay"));

	aas_area_t areas[2] = {0};
	aas_areasettings_t settings[2] = {0};
	areas[1].areanum = 1;
	VectorSet(areas[1].mins, -1.0f, -1.0f, -1.0f);
	VectorSet(areas[1].maxs, 1.0f, 1.0f, 1.0f);
	settings[1].areaflags = AAS_AREA_GROUNDED;
	settings[1].presencetype = PRESENCE_NORMAL;

	aasworld.loaded = qtrue;
	aasworld.numAreas = 2;
	aasworld.areas = areas;
	aasworld.numAreaSettings = 2;
	aasworld.areasettings = settings;

	AAS_InitReachability();
	assert_true(AAS_ContinueInitReachability());

	libvar_t *delay = LibVarGet("reachability_delay");
	assert_non_null(delay);
	assert_string_equal(delay->string, "100");
	assert_float_equal(delay->value, 100.0f, 0.0001f);

	ResetReachWorld();
	LibVarDeAllocAll();
}

/*
=============
test_reach_jump_crosses_a_gap

Retail sub_10013cc0 predicts the probe jump with a ZERO starting velocity and
puts the horizontal speed in the command vector (0x1001497e passes
data_100631cc as the velocity and the VectorScale(hordir, speed) result, with Z
replaced by sv_jumpvel, as cmdmove).  The predictor's on-ground command loop
drives frame_test_vel straight at cmdmove * frametime, so the later Quake III
arrangement - speed in the velocity, a purely vertical cmdmove - zeroes the
horizontal velocity on frame 0 and the probe jump lands back on the source
area, which made the generator emit no jump links whatsoever.

The landing is accepted by walking back from the predicted end position in 8
unit steps up to -32, a quarter unit high, until AAS_PointAreaNum reports the
destination area (0x100149c8-0x10014a05).
=============
*/
static void test_reach_jump_crosses_a_gap(void **state)
{
	(void)state;
	memset(&aasworld, 0, sizeof(aasworld));

	/*
	 * Two ground platforms at Z 0, x [-64, 0] and x [64, 128], with a 64 unit
	 * gap between them that drops far past sv_maxbarrier.
	 */
	aas_vertex_t vertexes[8] = {
		{-64.0f, -64.0f, 0.0f},
		{0.0f, -64.0f, 0.0f},
		{0.0f, 64.0f, 0.0f},
		{-64.0f, 64.0f, 0.0f},
		{64.0f, -64.0f, 0.0f},
		{128.0f, -64.0f, 0.0f},
		{128.0f, 64.0f, 0.0f},
		{64.0f, 64.0f, 0.0f}
	};
	aas_edge_t edges[9] = {
		{{0, 0}},
		{{0, 1}}, {{1, 2}}, {{2, 3}}, {{3, 0}},
		{{4, 5}}, {{5, 6}}, {{6, 7}}, {{7, 4}}
	};
	int edge_index[9] = {0, 1, 2, 3, 4, 5, 6, 7, 8};
	aas_face_t faces[3] = {0};
	int face_index[3] = {0, 1, 2};
	aas_area_t areas[4] = {0};
	aas_areasettings_t settings[4] = {0};
	aas_plane_t planes[8] = {0};
	aas_node_t nodes[6] = {0};

	faces[1].planenum = 4;
	faces[1].faceflags = AAS_FACE_GROUND;
	faces[1].firstedge = 1;
	faces[1].numedges = 4;
	faces[2].planenum = 4;
	faces[2].faceflags = AAS_FACE_GROUND;
	faces[2].firstedge = 5;
	faces[2].numedges = 4;

	areas[1].areanum = 1;
	areas[1].firstface = 1;
	areas[1].numfaces = 1;
	VectorSet(areas[1].mins, -64.0f, -64.0f, 0.0f);
	VectorSet(areas[1].maxs, 0.0f, 64.0f, 64.0f);
	areas[2].areanum = 2;
	areas[2].firstface = 2;
	areas[2].numfaces = 1;
	VectorSet(areas[2].mins, 64.0f, -64.0f, 0.0f);
	VectorSet(areas[2].maxs, 128.0f, 64.0f, 64.0f);
	areas[3].areanum = 3;
	VectorSet(areas[3].mins, 0.0f, -64.0f, -1000.0f);
	VectorSet(areas[3].maxs, 64.0f, 64.0f, 64.0f);

	settings[1].areaflags = AAS_AREA_GROUNDED;
	settings[1].presencetype = PRESENCE_NORMAL | PRESENCE_CROUCH;
	settings[2].areaflags = AAS_AREA_GROUNDED;
	settings[2].presencetype = PRESENCE_NORMAL | PRESENCE_CROUCH;
	settings[3].presencetype = PRESENCE_NORMAL | PRESENCE_CROUCH;

	VectorSet(planes[0].normal, 1.0f, 0.0f, 0.0f);
	planes[0].dist = 0.0f;
	VectorSet(planes[1].normal, -1.0f, 0.0f, 0.0f);
	planes[1].dist = 0.0f;
	VectorSet(planes[2].normal, 1.0f, 0.0f, 0.0f);
	planes[2].dist = 64.0f;
	VectorSet(planes[3].normal, -1.0f, 0.0f, 0.0f);
	planes[3].dist = -64.0f;
	VectorSet(planes[4].normal, 0.0f, 0.0f, 1.0f);
	planes[4].dist = 0.0f;
	VectorSet(planes[5].normal, 0.0f, 0.0f, -1.0f);
	planes[5].dist = 0.0f;
	VectorSet(planes[6].normal, 0.0f, 0.0f, 1.0f);
	planes[6].dist = -1000.0f;
	VectorSet(planes[7].normal, 0.0f, 0.0f, -1.0f);
	planes[7].dist = 1000.0f;

	nodes[1].planenum = 0;
	nodes[1].children[0] = 2;
	nodes[1].children[1] = 5;
	nodes[2].planenum = 2;
	nodes[2].children[0] = 3;
	nodes[2].children[1] = 4;
	nodes[3].planenum = 4;
	nodes[3].children[0] = -2;
	nodes[3].children[1] = 0;
	nodes[4].planenum = 6;
	nodes[4].children[0] = -3;
	nodes[4].children[1] = 0;
	nodes[5].planenum = 4;
	nodes[5].children[0] = -1;
	nodes[5].children[1] = 0;

	aasworld.loaded = qtrue;
	aasworld.numAreas = 4;
	aasworld.areas = areas;
	aasworld.numAreaSettings = 4;
	aasworld.areasettings = settings;
	aasworld.numVertexes = 8;
	aasworld.vertexes = vertexes;
	aasworld.numEdges = 9;
	aasworld.edges = edges;
	aasworld.edgeIndexSize = 9;
	aasworld.edgeIndex = edge_index;
	aasworld.numFaces = 3;
	aasworld.faces = faces;
	aasworld.faceIndexSize = 3;
	aasworld.faceIndex = face_index;
	aasworld.numPlanes = 8;
	aasworld.planes = planes;
	aasworld.numNodes = 6;
	aasworld.nodes = nodes;

	AAS_InitReachability();
	/* Retail returns 0 from sub_10013cc0 even when it links; the link itself
	   is the observable. */
	AAS_Reachability_Jump(1, 2);
	AAS_StoreReachability();

	assert_int_equal(aasworld.numReachability, 2);
	assert_int_equal(aasworld.reachability[1].traveltype, TRAVEL_JUMP);
	assert_int_equal(aasworld.reachability[1].areanum, 2);
	assert_float_equal(aasworld.reachability[1].start[0], 0.0f, 0.0001f);
	assert_float_equal(aasworld.reachability[1].end[0], 64.0f, 0.0001f);
	/*
	 * Retail costs the link with one expression at 0x10014a8f:
	 * (int)(VectorDistance(end, start) * 240 / sv_maxwalkvelocity + 600), so
	 * 64 * 240 / 300 + 600 = 651.
	 */
	assert_int_equal(aasworld.reachability[1].traveltime, 651);

	ResetReachWorld();
}

/*
=============
main

Run the retail AAS reachability generator contract tests.
=============
*/
int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_reach_teleport_admits_grounded_source_areas),
		cmocka_unit_test(test_reach_adjacent_edge_retail_travel_times),
		cmocka_unit_test(test_reach_walk_off_ledge_requires_fall_damage_gate),
		cmocka_unit_test(test_reach_equal_floor_retail_travel_time),
		cmocka_unit_test(test_reach_equal_floor_tie_break_uses_vertex_sum_length),
		cmocka_unit_test(test_reach_ladder_uses_truncated_magnitude),
		cmocka_unit_test(test_reach_jump_crosses_a_gap),
		cmocka_unit_test(test_reach_continue_init_creates_reachability_delay)
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
