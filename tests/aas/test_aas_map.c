#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <setjmp.h>
#include <cmocka.h>

#include <stdbool.h>

#ifndef cmocka_skip
#define cmocka_skip(...) skip()
#endif

#ifdef _WIN32
#include <direct.h>
#define chdir _chdir
#define getcwd _getcwd
#define unlink _unlink
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#include "botlib/aas/aas_map.h"
#include "botlib/aas/aas_local.h"
#include "botlib/common/l_libvar.h"
#include "botlib/common/l_memory.h"
#include "botlib/interface/botlib_interface.h"
#include "q2bridge/aas_translation.h"
#include "q2bridge/botlib.h"
#include "q2bridge/bridge.h"
#include "q2bridge/bridge_config.h"

#ifndef PROJECT_SOURCE_DIR
#error "PROJECT_SOURCE_DIR must be defined so regression tests can resolve asset paths."
#endif

#define TEST_BOTLIB_HEAP_SIZE (1u << 18)

typedef struct aas_test_environment_s {
    char asset_root[PATH_MAX];
    char previous_cwd[PATH_MAX];
    bool have_previous_cwd;
    bool libvar_initialised;
    bool memory_initialised;
    bool import_table_set;
    bool bridge_config_initialised;
} aas_test_environment_t;

static void test_capture_print(int priority, const char *fmt, ...)
{
    (void)priority;
    (void)fmt;
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

static int g_test_point_contents;
static qboolean g_test_gap_trace_enabled;
static qboolean g_test_grapple_trace_enabled;
static float g_test_source_ground_max_x;
static float g_test_destination_ground_min_x;
static float g_test_ground_height;
static float g_test_destination_ground_height;

/*
=============
test_point_contents

Return the deterministic BSP contents mask selected by an in-memory AAS test.
=============
*/
static int test_point_contents(vec3_t point)
{
	(void)point;
	return g_test_point_contents;
}

/*
=============
test_gap_trace

Provide two separated horizontal support regions for deterministic jump
prediction while leaving the intervening span empty.
=============
*/
static bsp_trace_t test_gap_trace(vec3_t start,
	vec3_t mins,
	vec3_t maxs,
	vec3_t end,
	int passent,
	int contentmask)
{
	(void)mins;
	(void)maxs;
	(void)passent;
	(void)contentmask;
	bsp_trace_t trace;
	memset(&trace, 0, sizeof(trace));
	trace.fraction = 1.0f;
	VectorCopy(end, trace.endpos);
	trace.plane.normal[2] = 1.0f;
	if (g_test_grapple_trace_enabled && contentmask == MASK_SHOT &&
		end[0] > start[0])
	{
		trace.fraction = 0.02f;
		for (int axis = 0; axis < 3; ++axis)
		{
			trace.endpos[axis] = start[axis] +
				trace.fraction * (end[axis] - start[axis]);
		}
		return trace;
	}
	if (!g_test_gap_trace_enabled)
	{
		return trace;
	}

	float delta_z = end[2] - start[2];
	if (delta_z < 0.0f)
	{
		float bestfraction = 2.0f;
		float bestheight = 0.0f;
		if (start[2] >= g_test_ground_height &&
			end[2] <= g_test_ground_height)
		{
			float fraction = (g_test_ground_height - start[2]) / delta_z;
			float hit_x = start[0] + fraction * (end[0] - start[0]);
			if (hit_x <= g_test_source_ground_max_x)
			{
				bestfraction = fraction;
				bestheight = g_test_ground_height;
			}
		}
		if (start[2] >= g_test_destination_ground_height &&
			end[2] <= g_test_destination_ground_height)
		{
			float fraction = (g_test_destination_ground_height - start[2]) /
				delta_z;
			float hit_x = start[0] + fraction * (end[0] - start[0]);
			if (hit_x >= g_test_destination_ground_min_x &&
				fraction < bestfraction)
			{
				bestfraction = fraction;
				bestheight = g_test_destination_ground_height;
			}
		}
		if (bestfraction <= 1.0f)
		{
			trace.fraction = bestfraction;
			for (int axis = 0; axis < 3; ++axis)
			{
				trace.endpos[axis] = start[axis] +
					bestfraction * (end[axis] - start[axis]);
			}
			trace.endpos[2] = bestheight;
			return trace;
		}
	}

	if (end[0] >= g_test_destination_ground_min_x &&
		end[2] <= g_test_destination_ground_height)
	{
		float fraction = 0.0f;
		float delta_x = end[0] - start[0];
		if (start[0] < g_test_destination_ground_min_x && delta_x > 0.0f)
		{
			fraction = (g_test_destination_ground_min_x - start[0]) / delta_x;
		}
		trace.fraction = fraction;
		VectorCopy(end, trace.endpos);
		trace.endpos[0] = g_test_destination_ground_min_x;
		trace.endpos[2] = g_test_destination_ground_height;
	}
	return trace;
}

static const botlib_import_table_t g_test_imports = {
    .Print = test_capture_print,
    .DPrint = test_capture_dprint,
    .BotLibVarGet = test_libvar_get,
    .BotLibVarSet = test_libvar_set,
};

static bot_import_t g_test_q2_imports = {
	.Trace = test_gap_trace,
	.PointContents = test_point_contents,
};

#ifdef _WIN32
static int test_setenv(const char *name, const char *value)
{
    return _putenv_s(name, value);
}

static void test_unsetenv(const char *name)
{
    _putenv_s(name, "");
}
#else
static int test_setenv(const char *name, const char *value)
{
    return setenv(name, value, 1);
}

static void test_unsetenv(const char *name)
{
    unsetenv(name);
}
#endif

static bool aas_environment_initialise(aas_test_environment_t *env)
{
    if (env == NULL) {
        return false;
    }

    const char *override_root = getenv("GLADIATOR_AAS_TEST_ASSET_DIR");
    if (override_root != NULL && override_root[0] != '\0') {
        int written = snprintf(env->asset_root, sizeof(env->asset_root), "%s", override_root);
        if (written <= 0 || (size_t)written >= sizeof(env->asset_root)) {
            return false;
        }
    } else {
        int written = snprintf(env->asset_root,
                               sizeof(env->asset_root),
                               "%s/dev_tools/assets",
                               PROJECT_SOURCE_DIR);
        if (written <= 0 || (size_t)written >= sizeof(env->asset_root)) {
            return false;
        }
    }

    char map_probe[PATH_MAX];
    snprintf(map_probe, sizeof(map_probe), "%s/maps/test_nav.bsp", env->asset_root);
    FILE *probe = fopen(map_probe, "rb");
    if (probe == NULL) {
        print_message("AAS regression harness skipped: missing test_nav.bsp in %s\n",
                      env->asset_root);
        return false;
    }
    fclose(probe);

    snprintf(map_probe, sizeof(map_probe), "%s/maps/test_nav.aas", env->asset_root);
    probe = fopen(map_probe, "rb");
    if (probe == NULL) {
        print_message("AAS regression harness skipped: missing test_nav.aas in %s\n",
                      env->asset_root);
        return false;
    }
    fclose(probe);

    if (getcwd(env->previous_cwd, sizeof(env->previous_cwd)) != NULL) {
        env->have_previous_cwd = true;
    }

    if (chdir(env->asset_root) != 0) {
        return false;
    }

    if (test_setenv("GLADIATOR_ASSET_DIR", env->asset_root) != 0) {
        return false;
    }

    return true;
}

static void aas_environment_cleanup(aas_test_environment_t *env)
{
    if (env == NULL) {
        return;
    }

    test_unsetenv("GLADIATOR_ASSET_DIR");

    if (env->have_previous_cwd) {
        chdir(env->previous_cwd);
        env->have_previous_cwd = false;
    }
}

static int aas_environment_setup(void **state)
{
    aas_test_environment_t *env = (aas_test_environment_t *)calloc(1, sizeof(aas_test_environment_t));
    if (env == NULL) {
        return -1;
    }

    if (!aas_environment_initialise(env)) {
        aas_environment_cleanup(env);
        free(env);
        cmocka_skip();
    }

    BotInterface_SetImportTable(&g_test_imports);
    env->import_table_set = true;

    LibVar_Init();
    env->libvar_initialised = true;

    if (!BridgeConfig_Init()) {
        aas_environment_cleanup(env);
        free(env);
        cmocka_skip();
    }
    env->bridge_config_initialised = true;

    if (!BotMemory_Init(TEST_BOTLIB_HEAP_SIZE)) {
        aas_environment_cleanup(env);
        free(env);
        cmocka_skip();
    }
    env->memory_initialised = true;

    *state = env;
    return 0;
}

static int aas_environment_teardown(void **state)
{
    aas_test_environment_t *env = (aas_test_environment_t *)(*state);
    if (env == NULL) {
        return 0;
    }

    AAS_Shutdown();

    if (env->bridge_config_initialised) {
        BridgeConfig_Shutdown();
        env->bridge_config_initialised = false;
    }

    if (env->memory_initialised) {
        BotMemory_Shutdown();
        env->memory_initialised = false;
    }

    if (env->libvar_initialised) {
        LibVar_Shutdown();
        env->libvar_initialised = false;
    }

    if (env->import_table_set) {
        BotInterface_SetImportTable(NULL);
        env->import_table_set = false;
    }

    aas_environment_cleanup(env);
    free(env);
    *state = NULL;
    return 0;
}

static void assert_entity_area_membership(int ent, const int *expected, size_t expected_count)
{
    assert_true(ent >= 0);
    assert_non_null(aasworld.entities);
    aas_entity_t *entity = &aasworld.entities[ent];

    int observed[16];
    size_t observed_count = 0;
    memset(observed, 0, sizeof(observed));

    for (aas_link_t *link = entity->areas; link != NULL; link = link->next_area) {
        if (observed_count < sizeof(observed) / sizeof(observed[0])) {
            observed[observed_count] = link->areanum;
        }
        observed_count++;
    }

    assert_int_equal(observed_count, expected_count);

    for (size_t i = 0; i < expected_count; ++i) {
        assert_non_null(expected);
        bool found = false;
        for (size_t j = 0; j < observed_count; ++j) {
            if (observed[j] == expected[i]) {
                found = true;
                break;
            }
        }
        assert_true(found);
    }
}

static void assert_area_entity_list_contains(int areanum, int ent)
{
    assert_true(areanum >= 0);
    assert_true(aasworld.areaEntityLists != NULL);
    assert_true((size_t)areanum < aasworld.areaEntityListCount);

    aas_link_t *head = aasworld.areaEntityLists[areanum];
    bool found = false;
    for (aas_link_t *link = head; link != NULL; link = link->next_ent) {
        if (link->entnum == ent) {
            found = true;
            break;
        }
    }
    assert_true(found);
}

static void assert_travel_time_for_area(int areanum, unsigned short expected_time)
{
    bool matched = false;
    for (int index = 0; index < aasworld.numReachability; ++index) {
        const aas_reachability_t *reach = &aasworld.reachability[index];
        if (reach->areanum == areanum) {
            assert_int_equal(reach->traveltime, expected_time);
            matched = true;
        }
    }
    assert_true(matched);
}

static void test_aas_loads_sample_map(void **state)
{
    (void)state;

    int status = AAS_LoadMap("test_nav", 0, NULL, 0, NULL, 0, NULL);
    assert_int_equal(status, BLERR_NOERROR);

    assert_true(aasworld.loaded);
    assert_true(aasworld.initialized);
    assert_int_equal(aasworld.numAreas, 3);
    assert_int_equal(aasworld.numReachability, 2);
    assert_int_equal(aasworld.numNodes, 1);
    assert_int_equal(aasworld.bspChecksum, -331439195);
    assert_int_equal(aasworld.aasChecksum, -42330527);

    AAS_Shutdown();
}

static void test_aas_entity_linking_and_reachability(void **state)
{
    (void)state;

    int status = AAS_LoadMap("test_nav", 0, NULL, 0, NULL, 0, NULL);
    assert_int_equal(status, BLERR_NOERROR);

    bot_updateentity_t fixtures[3];
    memset(fixtures, 0, sizeof(fixtures));

    fixtures[0].origin[0] = 16.0f;
    fixtures[0].origin[1] = 16.0f;
    fixtures[0].origin[2] = 16.0f;
    fixtures[0].old_origin[0] = 16.0f;
    fixtures[0].old_origin[1] = 16.0f;
    fixtures[0].old_origin[2] = 16.0f;
    fixtures[0].mins[0] = -8.0f;
    fixtures[0].mins[1] = -8.0f;
    fixtures[0].mins[2] = -8.0f;
    fixtures[0].maxs[0] = 8.0f;
    fixtures[0].maxs[1] = 8.0f;
    fixtures[0].maxs[2] = 8.0f;

    fixtures[1].origin[0] = 48.0f;
    fixtures[1].origin[1] = 16.0f;
    fixtures[1].origin[2] = 16.0f;
    fixtures[1].old_origin[0] = 48.0f;
    fixtures[1].old_origin[1] = 16.0f;
    fixtures[1].old_origin[2] = 16.0f;
    fixtures[1].mins[0] = -40.0f;
    fixtures[1].mins[1] = -8.0f;
    fixtures[1].mins[2] = -8.0f;
    fixtures[1].maxs[0] = 40.0f;
    fixtures[1].maxs[1] = 8.0f;
    fixtures[1].maxs[2] = 8.0f;

    fixtures[2].origin[0] = 160.0f;
    fixtures[2].origin[1] = 160.0f;
    fixtures[2].origin[2] = 160.0f;
    fixtures[2].old_origin[0] = 160.0f;
    fixtures[2].old_origin[1] = 160.0f;
    fixtures[2].old_origin[2] = 160.0f;
    fixtures[2].mins[0] = -8.0f;
    fixtures[2].mins[1] = -8.0f;
    fixtures[2].mins[2] = -8.0f;
    fixtures[2].maxs[0] = 8.0f;
    fixtures[2].maxs[1] = 8.0f;
    fixtures[2].maxs[2] = 8.0f;

    aasworld.time = 1.0f;
    AASEntityFrame translated = {0};
    status = TranslateEntityUpdate(1, &fixtures[0], &translated);
    assert_int_equal(status, BLERR_NOERROR);
    status = AAS_UpdateEntity(1, &translated);
    assert_int_equal(status, BLERR_NOERROR);
    assert_true(aasworld.entitiesValid);
    assert_non_null(aasworld.entities);
    assert_false(aasworld.entities[1].outsideAllAreas);
    assert_int_equal(aasworld.entities[1].areaOccupancyCount, 1);
    int single_area[] = {1};
    assert_entity_area_membership(1, single_area, 1);
    assert_area_entity_list_contains(1, 1);

    aasworld.time = 2.0f;
    status = TranslateEntityUpdate(1, &fixtures[1], &translated);
    assert_int_equal(status, BLERR_NOERROR);
    status = AAS_UpdateEntity(1, &translated);
    assert_int_equal(status, BLERR_NOERROR);
    assert_false(aasworld.entities[1].outsideAllAreas);
    assert_int_equal(aasworld.entities[1].areaOccupancyCount, 2);
    int dual_areas[] = {1, 2};
    assert_entity_area_membership(1, dual_areas, 2);
    assert_area_entity_list_contains(1, 1);
    assert_area_entity_list_contains(2, 1);

    aasworld.time = 3.0f;
    status = TranslateEntityUpdate(1, &fixtures[2], &translated);
    assert_int_equal(status, BLERR_NOERROR);
    status = AAS_UpdateEntity(1, &translated);
    assert_int_equal(status, BLERR_NOERROR);
    assert_true(aasworld.entities[1].outsideAllAreas);
    assert_int_equal(aasworld.entities[1].areaOccupancyCount, 0);
    assert_entity_area_membership(1, NULL, 0);

    /* Validate reachability metadata seeded by the sample file. */
    assert_travel_time_for_area(2, 40);
    assert_travel_time_for_area(1, 42);

    AAS_Shutdown();
}

static void test_routing_frame_respects_framereachability(void **state)
{
    (void)state;

    LibVarSet("forcewrite", "0");
    LibVarSet("framereachability", "0");

    AAS_RouteFrameResetDiagnostics();
    AAS_RouteFrameUpdate();

    assert_int_equal(AAS_RouteFrameWorkCounter(), 0);
    assert_int_equal(AAS_RouteFrameSkipCounter(), 1);
    assert_int_equal(AAS_RouteFrameLastBudget(), 0);
    assert_false(AAS_RouteFrameForceWriteActive());

    LibVarSet("framereachability", "8");

    AAS_RouteFrameResetDiagnostics();
    AAS_RouteFrameUpdate();

    assert_int_equal(AAS_RouteFrameWorkCounter(), 1);
    assert_int_equal(AAS_RouteFrameSkipCounter(), 0);
    assert_int_equal(AAS_RouteFrameLastBudget(), 8);

    LibVarSet("framereachability", "0");
}

static void test_routing_frame_forcewrite_toggle(void **state)
{
    (void)state;

    LibVarSet("framereachability", "4");

    LibVarSet("forcewrite", "0");
    AAS_RouteFrameResetDiagnostics();
    AAS_RouteFrameUpdate();
    assert_false(AAS_RouteFrameForceWriteActive());

    LibVarSet("forcewrite", "1");
    AAS_RouteFrameResetDiagnostics();
    AAS_RouteFrameUpdate();
    assert_true(AAS_RouteFrameForceWriteActive());

    LibVarSet("forcewrite", "0");
    LibVarSet("framereachability", "0");
}

static void test_reachability_force_reachability_toggle(void **state)
{
    (void)state;

    LibVarSet("forcereachability", "0");
    LibVarSet("forceclustering", "0");

    AAS_ReachabilityFrameResetDiagnostics();
    AAS_ReachabilityFrameUpdate();

    assert_int_equal(AAS_ReachabilityFrameWorkCounter(), 0);
    assert_int_equal(AAS_ReachabilityFrameSkipCounter(), 1);
    assert_false(AAS_ReachabilityForceReachabilityActive());
    assert_false(AAS_ReachabilityForceClusteringActive());

    LibVarSet("forcereachability", "1");

    AAS_ReachabilityFrameResetDiagnostics();
    AAS_ReachabilityFrameUpdate();

    assert_int_equal(AAS_ReachabilityFrameWorkCounter(), 1);
    assert_true(AAS_ReachabilityForceReachabilityActive());

    LibVarSet("forcereachability", "0");
}

static void test_reachability_force_clustering_toggle(void **state)
{
    (void)state;

    LibVarSet("forcereachability", "0");
    LibVarSet("forceclustering", "1");

    AAS_ReachabilityFrameResetDiagnostics();
    AAS_ReachabilityFrameUpdate();

    assert_int_equal(AAS_ReachabilityFrameWorkCounter(), 1);
    assert_true(AAS_ReachabilityForceClusteringActive());
    assert_false(AAS_ReachabilityForceReachabilityActive());

    LibVarSet("forceclustering", "0");
}

/*
=============
test_reachability_geometry_helpers

Validates the Q3-derived face center, face area, and convex area volume helpers
against an in-memory unit tetrahedron.
=============
*/
static void test_reachability_geometry_helpers(void **state)
{
	(void)state;
	memset(&aasworld, 0, sizeof(aasworld));

	aas_area_t areas[2] = {0};
	aas_vertex_t vertexes[4] = {
		{0.0f, 0.0f, 0.0f},
		{1.0f, 0.0f, 0.0f},
		{0.0f, 1.0f, 0.0f},
		{0.0f, 0.0f, 1.0f}
	};
	aas_edge_t edges[12] = {
		{{0, 1}}, {{1, 2}}, {{2, 0}},
		{{0, 3}}, {{3, 1}}, {{1, 0}},
		{{0, 2}}, {{2, 3}}, {{3, 0}},
		{{1, 3}}, {{3, 2}}, {{2, 1}}
	};
	int edge_index[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
	aas_face_t faces[4] = {0};
	int face_index[4] = {0, 1, 2, 3};
	aas_plane_t planes[4] = {0};

	areas[1].areanum = 1;
	areas[1].numfaces = 4;
	areas[1].firstface = 0;
	for (int index = 0; index < 4; ++index)
	{
		faces[index].planenum = index;
		faces[index].numedges = 3;
		faces[index].firstedge = index * 3;
		faces[index].backarea = 1;
	}
	planes[0].normal[2] = 1.0f;
	planes[1].normal[1] = 1.0f;
	planes[2].normal[0] = 1.0f;
	planes[3].normal[0] = 0.577350269f;
	planes[3].normal[1] = 0.577350269f;
	planes[3].normal[2] = 0.577350269f;
	planes[3].dist = 0.577350269f;

	aasworld.areas = areas;
	aasworld.numAreas = 1;
	aasworld.vertexes = vertexes;
	aasworld.numVertexes = 4;
	aasworld.edges = edges;
	aasworld.numEdges = 12;
	aasworld.edgeIndex = edge_index;
	aasworld.edgeIndexSize = 12;
	aasworld.faces = faces;
	aasworld.numFaces = 4;
	aasworld.faceIndex = face_index;
	aasworld.faceIndexSize = 4;
	aasworld.planes = planes;
	aasworld.numPlanes = 4;

	vec3_t center;
	AAS_FaceCenter(0, center);
	assert_float_equal(center[0], 1.0f / 3.0f, 0.0001f);
	assert_float_equal(center[1], 1.0f / 3.0f, 0.0001f);
	assert_float_equal(center[2], 0.0f, 0.0001f);
	assert_float_equal(AAS_FaceArea(&faces[3]), 0.8660254f, 0.0001f);
	assert_float_equal(AAS_AreaVolume(1), 1.0f / 6.0f, 0.0001f);

	memset(&aasworld, 0, sizeof(aasworld));
}

/*
=============
test_reachability_area_and_link_helpers

Exercises generator predicates, linked-area selection, and loaded reachability
duplicate detection without external map assets.
=============
*/
static void test_reachability_area_and_link_helpers(void **state)
{
	(void)state;
	memset(&aasworld, 0, sizeof(aasworld));

	aas_area_t areas[4] = {0};
	aas_areasettings_t settings[4] = {0};
	aas_reachability_t reachability[3] = {0};
	settings[1].firstreachablearea = 1;
	settings[1].numreachableareas = 2;
	settings[2].areaflags = AAS_AREA_GROUNDED;
	settings[3].areaflags = AAS_AREA_LIQUID;
	settings[3].contents = AAS_AREACONTENTS_LAVA |
		AAS_AREACONTENTS_SLIME |
		AAS_AREACONTENTS_TELEPORTER;
	reachability[1].areanum = 2;
	reachability[2].areanum = 3;
	VectorSet(areas[2].mins, 50.0f, -8.0f, -32.0f);
	VectorSet(areas[2].maxs, 60.0f, 8.0f, 32.0f);
	VectorSet(areas[3].mins, 70.0f, -8.0f, -32.0f);
	VectorSet(areas[3].maxs, 80.0f, 8.0f, 32.0f);
	aasworld.loaded = qtrue;
	aasworld.areas = areas;
	aasworld.areasettings = settings;
	aasworld.numAreaSettings = 4;
	aasworld.reachability = reachability;
	aasworld.numReachability = 3;
	aasworld.numAreas = 4;

	assert_true(AAS_AreaGrounded(2));
	assert_true(AAS_AreaLiquid(3));
	assert_int_equal(AAS_AreaLava(3), AAS_AREACONTENTS_LAVA);
	assert_int_equal(AAS_AreaSlime(3), AAS_AREACONTENTS_SLIME);
	assert_int_equal(AAS_AreaTeleporter(3), AAS_AREACONTENTS_TELEPORTER);
	assert_true(AAS_ReachabilityExists(1, 2));
	assert_true(AAS_ReachabilityExists(1, 3));
	assert_false(AAS_ReachabilityExists(2, 1));

	aas_link_t second = {0};
	second.areanum = 2;
	aas_link_t first = {0};
	first.areanum = 1;
	first.next_area = &second;
	assert_int_equal(AAS_BestReachableLinkArea(&first), 2);
	first.next_area = NULL;
	assert_int_equal(AAS_BestReachableLinkArea(&first), 1);

	vec3_t start = {0.0f, 0.0f, 0.0f};
	vec3_t end = {10.0f, 0.0f, 0.0f};
	settings[3].areaflags = 0;
	assert_true(AAS_NearbySolidOrGap(start, end));
	settings[3].areaflags = AAS_AREA_GROUNDED;
	assert_false(AAS_NearbySolidOrGap(start, end));
	areas[2].mins[0] = 90.0f;
	areas[2].maxs[0] = 100.0f;
	assert_true(AAS_NearbySolidOrGap(start, end));

	memset(&aasworld, 0, sizeof(aasworld));
}

/*
=============
test_reachability_physics_helpers

Pins the retail fallback gravity, velocity, fall-damage, and jump estimates used
before bridge tuning variables are available.
=============
*/
static void test_reachability_physics_helpers(void **state)
{
	(void)state;
	assert_int_equal(AAS_FallDamageDistance(), 187);
	assert_float_equal(AAS_FallDelta(100.0f), 16.0f, 0.0001f);
	assert_float_equal(AAS_MaxJumpHeight(224.0f), 31.36f, 0.0001f);
	assert_float_equal(AAS_MaxJumpDistance(224.0f), 402.198f, 0.001f);
	assert_int_equal(AAS_BarrierJumpTravelTime(), 2);

	vec3_t first_start = {0.0f, 0.0f, 0.0f};
	vec3_t first_end = {10.0f, 0.0f, 0.0f};
	vec3_t second_start = {0.0f, 5.0f, 0.0f};
	vec3_t second_end = {10.0f, 5.0f, 0.0f};
	aas_plane_t plane = {0};
	plane.normal[2] = 1.0f;
	vec3_t beststart1 = {0.0f, 0.0f, 0.0f};
	vec3_t bestend1 = {0.0f, 0.0f, 0.0f};
	vec3_t beststart2 = {0.0f, 0.0f, 0.0f};
	vec3_t bestend2 = {0.0f, 0.0f, 0.0f};
	float edgedistance = AAS_ClosestEdgePoints(first_start,
		first_end,
		second_start,
		second_end,
		&plane,
		&plane,
		beststart1,
		bestend1,
		beststart2,
		bestend2,
		99999.0f);
	assert_float_equal(edgedistance, 5.0f, 0.0001f);
	assert_float_equal(beststart1[0], 10.0f, 0.0001f);
	assert_float_equal(beststart1[1], 0.0f, 0.0001f);
	assert_float_equal(bestend1[0], 10.0f, 0.0001f);
	assert_float_equal(bestend1[1], 5.0f, 0.0001f);
}

/*
=============
test_reachability_swim_generation_and_storage

Builds two liquid areas around one shared face and verifies the retail swim
link, small-volume penalty, duplicate lookup, and one-based storage metadata.
=============
*/
static void test_reachability_swim_generation_and_storage(void **state)
{
	(void)state;
	memset(&aasworld, 0, sizeof(aasworld));
	Q2Bridge_SetImportTable(&g_test_q2_imports);
	g_test_point_contents = CONTENTS_WATER;

	aas_area_t areas[3] = {0};
	aas_areasettings_t settings[3] = {0};
	aas_vertex_t vertexes[3] = {
		{0.0f, 0.0f, 0.0f},
		{0.0f, 8.0f, 0.0f},
		{0.0f, 0.0f, 8.0f}
	};
	aas_edge_t edges[3] = {{{0, 1}}, {{1, 2}}, {{2, 0}}};
	int edge_index[3] = {0, 1, 2};
	aas_face_t faces[2] = {0};
	int face_index[2] = {1, -1};
	aas_plane_t planes[2] = {0};

	areas[1].areanum = 1;
	areas[1].numfaces = 1;
	areas[1].firstface = 0;
	VectorSet(areas[1].mins, -10.0f, -1.0f, -1.0f);
	VectorSet(areas[1].maxs, 0.0f, 9.0f, 9.0f);
	areas[2].areanum = 2;
	areas[2].numfaces = 1;
	areas[2].firstface = 1;
	VectorSet(areas[2].mins, 0.0f, -1.0f, -1.0f);
	VectorSet(areas[2].maxs, 10.0f, 9.0f, 9.0f);
	settings[1].areaflags = AAS_AREA_LIQUID;
	settings[1].presencetype = PRESENCE_NORMAL;
	settings[2].areaflags = AAS_AREA_LIQUID;
	settings[2].presencetype = PRESENCE_NORMAL;
	faces[1].planenum = 0;
	faces[1].numedges = 3;
	faces[1].firstedge = 0;
	faces[1].frontarea = 2;
	faces[1].backarea = 1;
	planes[0].normal[0] = 1.0f;
	planes[1].normal[0] = -1.0f;

	aasworld.loaded = qtrue;
	aasworld.numAreas = 3;
	aasworld.areas = areas;
	aasworld.numAreaSettings = 3;
	aasworld.areasettings = settings;
	aasworld.numVertexes = 3;
	aasworld.vertexes = vertexes;
	aasworld.numEdges = 3;
	aasworld.edges = edges;
	aasworld.edgeIndexSize = 3;
	aasworld.edgeIndex = edge_index;
	aasworld.numFaces = 2;
	aasworld.faces = faces;
	aasworld.faceIndexSize = 2;
	aasworld.faceIndex = face_index;
	aasworld.numPlanes = 2;
	aasworld.planes = planes;

	AAS_InitReachability();
	assert_true(AAS_Reachability_Swim(1, 2));
	assert_true(AAS_ReachabilityExists(1, 2));
	AAS_StoreReachability();
	assert_int_equal(aasworld.numReachability, 2);
	assert_int_equal(settings[1].firstreachablearea, 1);
	assert_int_equal(settings[1].numreachableareas, 1);
	assert_int_equal(aasworld.reachability[0].areanum, 0);
	assert_int_equal(aasworld.reachability[1].areanum, 2);
	assert_int_equal(aasworld.reachability[1].facenum, 1);
	assert_int_equal(aasworld.reachability[1].traveltype, TRAVEL_SWIM);
	assert_int_equal(aasworld.reachability[1].traveltime, 201);
	assert_float_equal(aasworld.reachability[1].end[0], 2.0f, 0.0001f);

	AAS_ShutDownReachabilityHeap();
	AAS_ClearReachabilityData();
	free(aasworld.reachability);
	g_test_point_contents = 0;
	Q2Bridge_SetImportTable(NULL);
	memset(&aasworld, 0, sizeof(aasworld));
}

/*
=============
test_reachability_equal_floor_generation_and_storage

Pins lowest/longest shared-edge walk generation, inside offsets, destination
crouch startup cost, and flattened area reachability spans.
=============
*/
static void test_reachability_equal_floor_generation_and_storage(void **state)
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

	AAS_InitReachability();
	assert_true(AAS_Reachability_EqualFloorHeight(1, 2));
	assert_true(AAS_ReachabilityExists(1, 2));
	AAS_StoreReachability();
	assert_int_equal(aasworld.numReachability, 2);
	assert_int_equal(settings[1].firstreachablearea, 1);
	assert_int_equal(settings[1].numreachableareas, 1);
	assert_int_equal(aasworld.reachability[0].areanum, 0);
	assert_int_equal(aasworld.reachability[1].areanum, 2);
	assert_int_equal(aasworld.reachability[1].edgenum, -1);
	assert_int_equal(aasworld.reachability[1].traveltype, TRAVEL_WALK);
	assert_int_equal(aasworld.reachability[1].traveltime, 301);
	assert_float_equal(aasworld.reachability[1].start[0], 0.1f, 0.0001f);
	assert_float_equal(aasworld.reachability[1].end[0], 5.0f, 0.0001f);
	assert_float_equal(aasworld.reachability[1].end[2], 0.125f, 0.0001f);

	AAS_ShutDownReachabilityHeap();
	AAS_ClearReachabilityData();
	free(aasworld.reachability);
	memset(&aasworld, 0, sizeof(aasworld));
}

/*
=============
test_reachability_adjacent_edge_travel_branches

Builds paired coplanar boundary edges at several height differences and pins
the retail step, barrier, downhill-walk, water-jump, and walk-off-ledge branch
ordering, endpoints, costs, and one-based storage.
=============
*/
static void test_reachability_adjacent_edge_travel_branches(void **state)
{
	(void)state;
	typedef struct
	{
		float height;
		qboolean source_liquid;
		int traveltype;
		int traveltime;
		float start_x;
		float end_x;
	} adjacent_case_t;
	const adjacent_case_t cases[] = {
		{8.0f, qfalse, TRAVEL_WALK, 0, 0.1f, 5.0f},
		{30.0f, qfalse, TRAVEL_BARRIERJUMP, 100, 0.1f, 5.0f},
		{-8.0f, qfalse, TRAVEL_WALK, 1, 0.1f, 5.0f},
		{30.0f, qtrue, TRAVEL_WATERJUMP, 400, 0.0f, 15.0f},
		{-40.0f, qfalse, TRAVEL_WALKOFFLEDGE, 72, 0.0f, 2.0f}
	};

	for (size_t case_index = 0;
		case_index < sizeof(cases) / sizeof(cases[0]);
		++case_index)
	{
		const adjacent_case_t *testcase = &cases[case_index];
		memset(&aasworld, 0, sizeof(aasworld));
		aas_area_t areas[3] = {0};
		aas_areasettings_t settings[3] = {0};
		aas_vertex_t vertexes[4] = {
			{0.0f, 0.0f, 0.0f},
			{0.0f, 10.0f, 0.0f},
			{0.0f, 0.0f, 0.0f},
			{0.0f, 10.0f, 0.0f}
		};
		vertexes[2][2] = testcase->height;
		vertexes[3][2] = testcase->height;
		aas_edge_t edges[3] = {{{0, 0}}, {{0, 1}}, {{2, 3}}};
		int edge_index[2] = {-1, 2};
		aas_face_t faces[3] = {0};
		int face_index[2] = {testcase->source_liquid ? -1 : 1, 2};
		aas_plane_t planes[2] = {0};

		areas[1].areanum = 1;
		areas[1].firstface = 0;
		areas[1].numfaces = 1;
		VectorSet(areas[1].mins, -10.0f, -1.0f, -1.0f);
		VectorSet(areas[1].maxs, 0.0f, 11.0f, 32.0f);
		areas[2].areanum = 2;
		areas[2].firstface = 1;
		areas[2].numfaces = 1;
		VectorSet(areas[2].mins, 0.0f, -1.0f, testcase->height - 1.0f);
		VectorSet(areas[2].maxs, 10.0f, 11.0f,
			testcase->height + 32.0f > 4.0f ?
				testcase->height + 32.0f : 4.0f);
		settings[1].areaflags = testcase->source_liquid ?
			AAS_AREA_LIQUID : AAS_AREA_GROUNDED;
		settings[1].presencetype = PRESENCE_NORMAL;
		settings[2].areaflags = AAS_AREA_GROUNDED;
		settings[2].presencetype = PRESENCE_NORMAL;
		faces[1].planenum = 0;
		faces[1].faceflags = testcase->source_liquid ? 0 : AAS_FACE_GROUND;
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

		AAS_InitReachability();
		assert_true(AAS_Reachability_Step_Barrier_WaterJump_WalkOffLedge(1, 2));
		AAS_StoreReachability();
		assert_int_equal(aasworld.numReachability, 2);
		assert_int_equal(settings[1].firstreachablearea, 1);
		assert_int_equal(settings[1].numreachableareas, 1);
		assert_int_equal(aasworld.reachability[1].areanum, 2);
		assert_int_equal(aasworld.reachability[1].traveltype, testcase->traveltype);
		assert_int_equal(aasworld.reachability[1].traveltime, testcase->traveltime);
		assert_float_equal(aasworld.reachability[1].start[0],
			testcase->start_x, 0.0001f);
		assert_float_equal(aasworld.reachability[1].end[0],
			testcase->end_x, 0.0001f);

		AAS_ShutDownReachabilityHeap();
		AAS_ClearReachabilityData();
		free(aasworld.reachability);
		memset(&aasworld, 0, sizeof(aasworld));
	}
}

/*
=============
test_reachability_jump_generation_and_rejections

Pins closest-edge jump prediction across a synthetic gap, the retail false
return after successful linking, stored jump cost, and crouch-area rejection.
=============
*/
static void test_reachability_jump_generation_and_rejections(void **state)
{
	(void)state;
	memset(&aasworld, 0, sizeof(aasworld));
	Q2Bridge_SetImportTable(&g_test_q2_imports);
	g_test_gap_trace_enabled = qtrue;
	g_test_source_ground_max_x = 0.0f;
	g_test_destination_ground_min_x = 64.0f;
	g_test_ground_height = 0.0f;
	g_test_destination_ground_height = 0.0f;
	g_test_point_contents = 0;

	aas_area_t areas[3] = {0};
	aas_areasettings_t settings[3] = {0};
	aas_vertex_t vertexes[4] = {
		{0.0f, 0.0f, 0.0f},
		{0.0f, 10.0f, 0.0f},
		{64.0f, 0.0f, 0.0f},
		{64.0f, 10.0f, 0.0f}
	};
	aas_edge_t edges[3] = {{{0, 0}}, {{0, 1}}, {{2, 3}}};
	int edge_index[2] = {1, 2};
	aas_face_t faces[3] = {0};
	int face_index[2] = {1, 2};
	aas_plane_t planes[1] = {0};

	areas[1].areanum = 1;
	areas[1].firstface = 0;
	areas[1].numfaces = 1;
	VectorSet(areas[1].mins, -20.0f, -2.0f, -2.0f);
	VectorSet(areas[1].maxs, 0.0f, 12.0f, 32.0f);
	areas[2].areanum = 2;
	areas[2].firstface = 1;
	areas[2].numfaces = 1;
	VectorSet(areas[2].mins, 64.0f, -2.0f, -2.0f);
	VectorSet(areas[2].maxs, 100.0f, 12.0f, 32.0f);
	settings[1].areaflags = AAS_AREA_GROUNDED;
	settings[1].presencetype = PRESENCE_NORMAL;
	settings[2].areaflags = AAS_AREA_GROUNDED;
	settings[2].presencetype = PRESENCE_NORMAL;
	faces[1].planenum = 0;
	faces[1].faceflags = AAS_FACE_GROUND;
	faces[1].firstedge = 0;
	faces[1].numedges = 1;
	faces[2].planenum = 0;
	faces[2].faceflags = AAS_FACE_GROUND;
	faces[2].firstedge = 1;
	faces[2].numedges = 1;
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
	aasworld.edgeIndexSize = 2;
	aasworld.edgeIndex = edge_index;
	aasworld.numFaces = 3;
	aasworld.faces = faces;
	aasworld.faceIndexSize = 2;
	aasworld.faceIndex = face_index;
	aasworld.numPlanes = 1;
	aasworld.planes = planes;

	AAS_InitReachability();
	assert_false(AAS_Reachability_Jump(1, 2));
	assert_true(AAS_ReachabilityExists(1, 2));
	AAS_StoreReachability();
	assert_int_equal(aasworld.numReachability, 2);
	assert_int_equal(aasworld.reachability[1].traveltype, TRAVEL_JUMP);
	assert_int_equal(aasworld.reachability[1].traveltime, 351);
	assert_float_equal(aasworld.reachability[1].start[0], 0.0f, 0.0001f);
	assert_float_equal(aasworld.reachability[1].end[0], 64.0f, 0.0001f);

	AAS_ShutDownReachabilityHeap();
	AAS_ClearReachabilityData();
	free(aasworld.reachability);
	aasworld.reachability = NULL;
	aasworld.numReachability = 0;
	settings[2].presencetype = PRESENCE_CROUCH;
	AAS_InitReachability();
	assert_false(AAS_Reachability_Jump(1, 2));
	assert_false(AAS_ReachabilityExists(1, 2));

	AAS_ShutDownReachabilityHeap();
	g_test_gap_trace_enabled = qfalse;
	Q2Bridge_SetImportTable(NULL);
	memset(&aasworld, 0, sizeof(aasworld));
}

/*
=============
test_reachability_ladder_shared_edge_generation

Pins the symmetric retail ladder transition across a horizontal edge shared
by two vertical, same-facing ladder surfaces, including the 32-unit offsets.
=============
*/
static void test_reachability_ladder_shared_edge_generation(void **state)
{
	(void)state;
	memset(&aasworld, 0, sizeof(aasworld));

	aas_area_t areas[3] = {0};
	aas_areasettings_t settings[3] = {0};
	aas_vertex_t vertexes[2] = {
		{0.0f, 0.0f, 0.0f},
		{0.0f, 10.0f, 0.0f}
	};
	aas_edge_t edges[2] = {{{0, 0}}, {{0, 1}}};
	int edge_index[2] = {1, 1};
	aas_face_t faces[3] = {0};
	int face_index[2] = {1, 2};
	aas_plane_t planes[1] = {0};

	areas[1].areanum = 1;
	areas[1].firstface = 0;
	areas[1].numfaces = 1;
	VectorSet(areas[1].mins, -16.0f, -1.0f, -32.0f);
	VectorSet(areas[1].maxs, 0.0f, 11.0f, 0.0f);
	areas[2].areanum = 2;
	areas[2].firstface = 1;
	areas[2].numfaces = 1;
	VectorSet(areas[2].mins, -16.0f, -1.0f, 0.0f);
	VectorSet(areas[2].maxs, 0.0f, 11.0f, 32.0f);
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
	planes[0].normal[0] = 1.0f;

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
	assert_int_equal(settings[1].firstreachablearea, 1);
	assert_int_equal(settings[1].numreachableareas, 1);
	assert_int_equal(settings[2].firstreachablearea, 2);
	assert_int_equal(settings[2].numreachableareas, 1);
	assert_int_equal(aasworld.reachability[0].areanum, 0);
	assert_int_equal(aasworld.reachability[1].areanum, 2);
	assert_int_equal(aasworld.reachability[1].facenum, 1);
	assert_int_equal(aasworld.reachability[1].edgenum, 1);
	assert_int_equal(aasworld.reachability[1].traveltype, TRAVEL_LADDER);
	assert_int_equal(aasworld.reachability[1].traveltime, 10);
	assert_float_equal(aasworld.reachability[1].start[2], -32.0f, 0.0001f);
	assert_float_equal(aasworld.reachability[1].end[0], -3.0f, 0.0001f);
	assert_float_equal(aasworld.reachability[1].end[2], 32.0f, 0.0001f);
	assert_int_equal(aasworld.reachability[2].areanum, 1);
	assert_int_equal(aasworld.reachability[2].facenum, 2);
	assert_int_equal(aasworld.reachability[2].edgenum, 1);
	assert_int_equal(aasworld.reachability[2].traveltype, TRAVEL_LADDER);
	assert_int_equal(aasworld.reachability[2].traveltime, 10);
	assert_float_equal(aasworld.reachability[2].start[2], 32.0f, 0.0001f);
	assert_float_equal(aasworld.reachability[2].end[0], -3.0f, 0.0001f);
	assert_float_equal(aasworld.reachability[2].end[2], -32.0f, 0.0001f);

	AAS_ShutDownReachabilityHeap();
	AAS_ClearReachabilityData();
	free(aasworld.reachability);
	memset(&aasworld, 0, sizeof(aasworld));
}

/*
=============
test_reachability_retail_teleporter_generation

Parses retail BSP epairs and pins misc_teleporter destination matching, the
24-unit destination lift/drop trace, expanded source hull, and stored cost.
=============
*/
static void test_reachability_retail_teleporter_generation(void **state)
{
	(void)state;
	memset(&aasworld, 0, sizeof(aasworld));
	const char entity_data[] =
		"{\n"
		"\"classname\" \"misc_teleporter\"\n"
		"\"origin\" \"0 0 0\"\n"
		"\"target\" \"teleport_destination\"\n"
		"\"speed\" \"3.5\"\n"
		"\"spawnflags\" \"7\"\n"
		"}\n"
		"{\n"
		"\"classname\" \"misc_teleporter_dest\"\n"
		"\"targetname\" \"teleport_destination\"\n"
		"\"origin\" \"100 0 16\"\n"
		"}\n";
	aas_bspentity_t *entities = AAS_ParseBSPEntities(entity_data,
		sizeof(entity_data) - 1U);
	assert_non_null(entities);
	assert_non_null(entities->next);
	assert_string_equal(AAS_ValueForBSPEpairKey(entities, "classname"),
		"misc_teleporter");
	assert_float_equal(AAS_FloatForBSPEpairKey(entities, "speed"),
		3.5f, 0.0001f);
	assert_int_equal(AAS_IntForBSPEpairKey(entities, "spawnflags"), 7);
	vec3_t parsedorigin;
	assert_true(AAS_VectorForBSPEpairKey(entities->next, "origin", parsedorigin));
	assert_float_equal(parsedorigin[0], 100.0f, 0.0001f);

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
	settings[1].contents = AAS_AREACONTENTS_TELEPORTER;
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
	assert_int_equal(AAS_Reachability_TeleportEntityList(entities), 1);
	assert_true(AAS_ReachabilityExists(1, 2));
	AAS_StoreReachability();
	assert_int_equal(aasworld.numReachability, 2);
	assert_int_equal(settings[1].firstreachablearea, 1);
	assert_int_equal(settings[1].numreachableareas, 1);
	assert_int_equal(aasworld.reachability[1].areanum, 2);
	assert_int_equal(aasworld.reachability[1].facenum, 0);
	assert_int_equal(aasworld.reachability[1].edgenum, 0);
	assert_int_equal(aasworld.reachability[1].traveltype, TRAVEL_TELEPORT);
	assert_int_equal(aasworld.reachability[1].traveltime, 50);
	assert_float_equal(aasworld.reachability[1].start[0], 0.0f, 0.0001f);
	assert_float_equal(aasworld.reachability[1].end[0], 100.0f, 0.0001f);
	assert_float_equal(aasworld.reachability[1].end[2], 0.25f, 0.0001f);

	AAS_FreeBSPEntities(entities);
	AAS_ShutDownReachabilityHeap();
	AAS_ClearReachabilityData();
	free(aasworld.reachability);
	memset(&aasworld, 0, sizeof(aasworld));
}

/*
=============
test_reachability_retail_elevator_generation

Pins retail func_plat edge sampling, model/height metadata, outside-hull start
placement, and the height-times-100-over-speed route cost.
=============
*/
static void test_reachability_retail_elevator_generation(void **state)
{
	(void)state;
	memset(&aasworld, 0, sizeof(aasworld));
	const char entity_data[] =
		"{\n"
		"\"classname\" \"func_plat\"\n"
		"\"model\" \"*1\"\n"
		"\"lip\" \"8\"\n"
		"\"height\" \"32\"\n"
		"\"speed\" \"200\"\n"
		"}\n";
	aas_bspentity_t *entities = AAS_ParseBSPEntities(entity_data,
		sizeof(entity_data) - 1U);
	assert_non_null(entities);

	aas_area_t areas[3] = {0};
	aas_areasettings_t settings[3] = {0};
	aas_bspmodel_t models[2] = {0};
	areas[1].areanum = 1;
	VectorSet(areas[1].mins, -40.0f, -4.0f, 0.0f);
	VectorSet(areas[1].maxs, -16.0f, 4.0f, 20.0f);
	areas[2].areanum = 2;
	VectorSet(areas[2].mins, 20.0f, -4.0f, 30.0f);
	VectorSet(areas[2].maxs, 22.0f, 4.0f, 40.0f);
	settings[1].areaflags = AAS_AREA_GROUNDED;
	settings[1].presencetype = PRESENCE_CROUCH;
	settings[2].areaflags = AAS_AREA_GROUNDED;
	settings[2].presencetype = PRESENCE_CROUCH;
	VectorSet(models[1].mins, -16.0f, -16.0f, 0.0f);
	VectorSet(models[1].maxs, 16.0f, 16.0f, 16.0f);
	VectorClear(models[1].origin);

	aasworld.loaded = qtrue;
	aasworld.numAreas = 3;
	aasworld.areas = areas;
	aasworld.numAreaSettings = 3;
	aasworld.areasettings = settings;
	aasworld.numBspModels = 2;
	aasworld.bspModels = models;

	AAS_InitReachability();
	assert_int_equal(AAS_Reachability_ElevatorEntityList(entities), 1);
	assert_true(AAS_ReachabilityExists(1, 2));
	AAS_StoreReachability();
	assert_int_equal(aasworld.numReachability, 2);
	assert_int_equal(settings[1].firstreachablearea, 1);
	assert_int_equal(settings[1].numreachableareas, 1);
	assert_int_equal(aasworld.reachability[1].areanum, 2);
	assert_int_equal(aasworld.reachability[1].facenum, 1);
	assert_int_equal(aasworld.reachability[1].edgenum, 32);
	assert_int_equal(aasworld.reachability[1].traveltype, TRAVEL_ELEVATOR);
	assert_int_equal(aasworld.reachability[1].traveltime, 16);
	assert_float_equal(aasworld.reachability[1].start[0],
		-34.4765f, 0.001f);
	assert_float_equal(aasworld.reachability[1].start[1], 0.0f, 0.0001f);
	assert_float_equal(aasworld.reachability[1].start[2], 2.0f, 0.0001f);
	assert_float_equal(aasworld.reachability[1].end[0], 21.0f, 0.0001f);
	assert_float_equal(aasworld.reachability[1].end[1], 0.0f, 0.0001f);
	assert_float_equal(aasworld.reachability[1].end[2], 34.0f, 0.0001f);

	AAS_FreeBSPEntities(entities);
	AAS_ShutDownReachabilityHeap();
	AAS_ClearReachabilityData();
	free(aasworld.reachability);
	memset(&aasworld, 0, sizeof(aasworld));
}

/*
=============
test_reachability_retail_grapple_generation

Pins the retail grounded-source grapple scan, higher solid-face selection,
near-wall shot trace, safe landing, metadata, and distance-derived route cost.
=============
*/
static void test_reachability_retail_grapple_generation(void **state)
{
	(void)state;
	memset(&aasworld, 0, sizeof(aasworld));
	Q2Bridge_SetImportTable(&g_test_q2_imports);
	g_test_grapple_trace_enabled = qtrue;
	g_test_gap_trace_enabled = qfalse;
	g_test_point_contents = 0;

	aas_area_t areas[3] = {0};
	aas_areasettings_t settings[3] = {0};
	aas_node_t nodes[4] = {0};
	aas_plane_t planes[4] = {0};
	aas_vertex_t vertexes[4] = {
		{100.0f, -10.0f, 64.0f},
		{100.0f, 10.0f, 64.0f},
		{100.0f, 10.0f, 136.0f},
		{100.0f, -10.0f, 136.0f}
	};
	aas_edge_t edges[5] = {
		{{0, 0}},
		{{0, 1}},
		{{1, 2}},
		{{2, 3}},
		{{3, 0}}
	};
	int edge_index[4] = {1, 2, 3, 4};
	aas_face_t faces[2] = {0};
	int face_index[1] = {1};

	areas[1].areanum = 1;
	VectorSet(areas[1].mins, -32.0f, -32.0f, 0.0f);
	VectorSet(areas[1].maxs, 49.0f, 32.0f, 96.0f);
	VectorSet(areas[1].center, 0.0f, 0.0f, 32.0f);
	areas[2].areanum = 2;
	areas[2].firstface = 0;
	areas[2].numfaces = 1;
	VectorSet(areas[2].mins, 50.0f, -32.0f, 0.0f);
	VectorSet(areas[2].maxs, 160.0f, 32.0f, 160.0f);
	VectorSet(areas[2].center, 100.0f, 0.0f, 80.0f);
	settings[1].areaflags = AAS_AREA_GROUNDED;
	settings[1].presencetype = PRESENCE_NORMAL | PRESENCE_CROUCH;
	settings[2].areaflags = AAS_AREA_GROUNDED;
	settings[2].presencetype = PRESENCE_NORMAL | PRESENCE_CROUCH;

	nodes[1].planenum = 0;
	nodes[1].children[0] = 2;
	nodes[1].children[1] = 3;
	nodes[2].planenum = 2;
	nodes[2].children[0] = -2;
	nodes[2].children[1] = 0;
	nodes[3].planenum = 1;
	nodes[3].children[0] = -1;
	nodes[3].children[1] = 0;
	planes[0].normal[0] = 1.0f;
	planes[0].dist = 50.0f;
	planes[1].normal[2] = 1.0f;
	planes[2].normal[2] = 1.0f;
	planes[3].normal[0] = -1.0f;
	planes[3].dist = -100.0f;
	faces[1].planenum = 3;
	faces[1].faceflags = AAS_FACE_SOLID;
	faces[1].firstedge = 0;
	faces[1].numedges = 4;

	aasworld.loaded = qtrue;
	aasworld.numAreas = 3;
	aasworld.areas = areas;
	aasworld.numAreaSettings = 3;
	aasworld.areasettings = settings;
	aasworld.numNodes = 4;
	aasworld.nodes = nodes;
	aasworld.numPlanes = 4;
	aasworld.planes = planes;
	aasworld.numVertexes = 4;
	aasworld.vertexes = vertexes;
	aasworld.numEdges = 5;
	aasworld.edges = edges;
	aasworld.edgeIndexSize = 4;
	aasworld.edgeIndex = edge_index;
	aasworld.numFaces = 2;
	aasworld.faces = faces;
	aasworld.faceIndexSize = 1;
	aasworld.faceIndex = face_index;

	AAS_InitReachability();
	assert_false(AAS_Reachability_Grapple(1, 2));
	assert_true(AAS_ReachabilityExists(1, 2));
	AAS_StoreReachability();
	assert_int_equal(aasworld.numReachability, 2);
	assert_int_equal(settings[1].firstreachablearea, 1);
	assert_int_equal(settings[1].numreachableareas, 1);
	assert_int_equal(aasworld.reachability[1].areanum, 2);
	assert_int_equal(aasworld.reachability[1].facenum, 1);
	assert_int_equal(aasworld.reachability[1].edgenum, 0);
	assert_int_equal(aasworld.reachability[1].traveltype, TRAVEL_GRAPPLEHOOK);
	assert_int_equal(aasworld.reachability[1].traveltime, 537);
	assert_float_equal(aasworld.reachability[1].start[0], 0.0f, 0.0001f);
	assert_float_equal(aasworld.reachability[1].start[2], 0.25f, 0.0001f);
	assert_float_equal(aasworld.reachability[1].end[0], 110.0f, 0.0001f);
	assert_float_equal(aasworld.reachability[1].end[2], 100.0f, 0.0001f);

	AAS_ShutDownReachabilityHeap();
	AAS_ClearReachabilityData();
	free(aasworld.reachability);
	g_test_grapple_trace_enabled = qfalse;
	Q2Bridge_SetImportTable(NULL);
	memset(&aasworld, 0, sizeof(aasworld));
}

/*
=============
test_reachability_retail_weapon_jump_generation

Pins retail item-area marking, downward rocket knockback, higher ground-face
selection, predicted landing, and the fixed rocket-jump route metadata.
=============
*/
static void test_reachability_retail_weapon_jump_generation(void **state)
{
	(void)state;
	memset(&aasworld, 0, sizeof(aasworld));
	Q2Bridge_SetImportTable(&g_test_q2_imports);
	g_test_gap_trace_enabled = qtrue;
	g_test_grapple_trace_enabled = qfalse;
	g_test_source_ground_max_x = 0.0f;
	g_test_destination_ground_min_x = 50.0f;
	g_test_ground_height = 0.0f;
	g_test_destination_ground_height = 100.0f;
	g_test_point_contents = 0;

	const char entitydata[] =
		"{\n"
		"\"classname\" \"worldspawn\"\n"
		"}\n"
		"{\n"
		"\"classname\" \"item_health\"\n"
		"\"origin\" \"0 0 48\"\n"
		"}\n"
		"{\n"
		"\"classname\" \"item_health_mega\"\n"
		"\"origin\" \"80 0 140\"\n"
		"}\n";
	aas_bspentity_t *entities = AAS_ParseBSPEntities(entitydata,
		sizeof(entitydata) - 1);
	assert_non_null(entities);

	aas_area_t areas[3] = {0};
	aas_areasettings_t settings[3] = {0};
	aas_node_t nodes[4] = {0};
	aas_plane_t planes[3] = {0};
	aas_vertex_t vertexes[4] = {
		{64.0f, -16.0f, 100.0f},
		{96.0f, -16.0f, 100.0f},
		{96.0f, 16.0f, 100.0f},
		{64.0f, 16.0f, 100.0f}
	};
	aas_edge_t edges[5] = {
		{{0, 0}},
		{{0, 1}},
		{{1, 2}},
		{{2, 3}},
		{{3, 0}}
	};
	int edge_index[4] = {1, 2, 3, 4};
	aas_face_t faces[2] = {0};
	int face_index[1] = {1};

	areas[1].areanum = 1;
	VectorSet(areas[1].mins, -64.0f, -32.0f, 24.0f);
	VectorSet(areas[1].maxs, 49.0f, 32.0f, 96.0f);
	VectorSet(areas[1].center, 0.0f, 0.0f, 48.0f);
	areas[2].areanum = 2;
	areas[2].firstface = 0;
	areas[2].numfaces = 1;
	VectorSet(areas[2].mins, 50.0f, -32.0f, 100.0f);
	VectorSet(areas[2].maxs, 160.0f, 32.0f, 180.0f);
	VectorSet(areas[2].center, 80.0f, 0.0f, 132.0f);
	settings[1].areaflags = AAS_AREA_GROUNDED;
	settings[1].presencetype = PRESENCE_NORMAL | PRESENCE_CROUCH;
	settings[2].areaflags = AAS_AREA_GROUNDED;
	settings[2].presencetype = PRESENCE_NORMAL | PRESENCE_CROUCH;

	nodes[1].planenum = 0;
	nodes[1].children[0] = 2;
	nodes[1].children[1] = 3;
	nodes[2].planenum = 2;
	nodes[2].children[0] = -2;
	nodes[2].children[1] = 0;
	nodes[3].planenum = 1;
	nodes[3].children[0] = -1;
	nodes[3].children[1] = 0;
	planes[0].normal[0] = 1.0f;
	planes[0].dist = 50.0f;
	planes[1].normal[2] = 1.0f;
	planes[1].dist = 24.0f;
	planes[2].normal[2] = 1.0f;
	planes[2].dist = 100.0f;
	faces[1].planenum = 2;
	faces[1].faceflags = AAS_FACE_GROUND;
	faces[1].firstedge = 0;
	faces[1].numedges = 4;

	aasworld.loaded = qtrue;
	aasworld.numAreas = 3;
	aasworld.areas = areas;
	aasworld.numAreaSettings = 3;
	aasworld.areasettings = settings;
	aasworld.numNodes = 4;
	aasworld.nodes = nodes;
	aasworld.numPlanes = 3;
	aasworld.planes = planes;
	aasworld.numVertexes = 4;
	aasworld.vertexes = vertexes;
	aasworld.numEdges = 5;
	aasworld.edges = edges;
	aasworld.edgeIndexSize = 4;
	aasworld.edgeIndex = edge_index;
	aasworld.numFaces = 2;
	aasworld.faces = faces;
	aasworld.faceIndexSize = 1;
	aasworld.faceIndex = face_index;

	assert_int_equal(AAS_SetWeaponJumpAreaFlagsEntityList(entities), 1);
	assert_int_equal(settings[1].areaflags & AAS_AREA_WEAPONJUMP, 0);
	assert_int_equal(settings[2].areaflags & AAS_AREA_WEAPONJUMP,
		AAS_AREA_WEAPONJUMP);
	vec3_t rocketorigin = {0.0f, 0.0f, 24.25f};
	assert_true(AAS_RocketJumpZVelocity(rocketorigin) > 600.0f);
	assert_float_equal(AAS_BFGJumpZVelocity(rocketorigin),
		AAS_RocketJumpZVelocity(rocketorigin),
		0.0001f);

	AAS_InitReachability();
	assert_true(AAS_Reachability_WeaponJump(1, 2));
	assert_true(AAS_ReachabilityExists(1, 2));
	AAS_StoreReachability();
	assert_int_equal(aasworld.numReachability, 2);
	assert_int_equal(settings[1].firstreachablearea, 1);
	assert_int_equal(settings[1].numreachableareas, 1);
	assert_int_equal(aasworld.reachability[1].areanum, 2);
	assert_int_equal(aasworld.reachability[1].facenum, 0);
	assert_int_equal(aasworld.reachability[1].edgenum, 0);
	assert_int_equal(aasworld.reachability[1].traveltype, TRAVEL_ROCKETJUMP);
	assert_int_equal(aasworld.reachability[1].traveltime, 500);
	assert_float_equal(aasworld.reachability[1].start[0], 0.0f, 0.0001f);
	assert_float_equal(aasworld.reachability[1].start[2], 24.25f, 0.0001f);
	assert_float_equal(aasworld.reachability[1].end[0], 80.0f, 0.0001f);
	assert_float_equal(aasworld.reachability[1].end[2], 100.0f, 0.0001f);

	AAS_FreeBSPEntities(entities);
	AAS_ShutDownReachabilityHeap();
	AAS_ClearReachabilityData();
	free(aasworld.reachability);
	g_test_gap_trace_enabled = qfalse;
	g_test_destination_ground_height = 0.0f;
	Q2Bridge_SetImportTable(NULL);
	memset(&aasworld, 0, sizeof(aasworld));
}

/*
=============
test_reachability_retail_secondary_walkoff_generation

Pins the separate retail exposed-edge scan and its 100/3000 safe-versus-
damaging-fall route cost split.
=============
*/
static void test_reachability_retail_secondary_walkoff_generation(void **state)
{
	(void)state;
	const float landingheights[2] = {0.0f, -200.0f};
	const unsigned short traveltimes[2] = {100, 3000};

	for (int testcase = 0; testcase < 2; ++testcase)
	{
		memset(&aasworld, 0, sizeof(aasworld));
		aas_area_t areas[4] = {0};
		aas_areasettings_t settings[4] = {0};
		aas_node_t nodes[4] = {0};
		aas_plane_t planes[3] = {0};
		aas_vertex_t vertexes[2] = {
			{0.0f, -10.0f, 100.0f},
			{0.0f, 10.0f, 100.0f}
		};
		aas_edge_t edges[2] = {{{0, 0}}, {{0, 1}}};
		int edge_index[2] = {1, 1};
		aas_face_t faces[3] = {0};
		int face_index[2] = {1, 2};

		areas[1].areanum = 1;
		areas[1].firstface = 0;
		areas[1].numfaces = 2;
		VectorSet(areas[1].mins, 0.0f, -16.0f, 100.0f);
		VectorSet(areas[1].maxs, 64.0f, 16.0f, 180.0f);
		VectorSet(areas[1].center, 8.0f, 0.0f, 132.0f);
		areas[2].areanum = 2;
		VectorSet(areas[2].mins,
			-64.0f,
			-16.0f,
			landingheights[testcase]);
		VectorSet(areas[2].maxs,
			-0.1f,
			16.0f,
			100.0f);
		areas[3].areanum = 3;
		settings[1].areaflags = AAS_AREA_GROUNDED;
		settings[1].presencetype = PRESENCE_NORMAL | PRESENCE_CROUCH;
		settings[2].areaflags = AAS_AREA_GROUNDED;
		settings[2].presencetype = PRESENCE_NORMAL | PRESENCE_CROUCH;
		settings[3].presencetype = PRESENCE_NORMAL | PRESENCE_CROUCH;

		nodes[1].planenum = 0;
		nodes[1].children[0] = 2;
		nodes[1].children[1] = 3;
		nodes[2].planenum = 1;
		nodes[2].children[0] = -1;
		nodes[2].children[1] = 0;
		nodes[3].planenum = 2;
		nodes[3].children[0] = -2;
		nodes[3].children[1] = 0;
		planes[0].normal[0] = 1.0f;
		planes[1].normal[2] = 1.0f;
		planes[1].dist = 100.0f;
		planes[2].normal[2] = 1.0f;
		planes[2].dist = landingheights[testcase];
		faces[1].planenum = 1;
		faces[1].faceflags = AAS_FACE_GROUND;
		faces[1].firstedge = 0;
		faces[1].numedges = 1;
		faces[2].planenum = 0;
		faces[2].faceflags = AAS_FACE_SOLID;
		faces[2].firstedge = 1;
		faces[2].numedges = 1;
		faces[2].frontarea = 1;
		faces[2].backarea = 3;

		aasworld.loaded = qtrue;
		aasworld.numAreas = 4;
		aasworld.areas = areas;
		aasworld.numAreaSettings = 4;
		aasworld.areasettings = settings;
		aasworld.numNodes = 4;
		aasworld.nodes = nodes;
		aasworld.numPlanes = 3;
		aasworld.planes = planes;
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

		AAS_InitReachability();
		AAS_Reachability_WalkOffLedge(1);
		assert_true(AAS_ReachabilityExists(1, 2));
		AAS_StoreReachability();
		assert_int_equal(aasworld.numReachability, 2);
		assert_int_equal(aasworld.reachability[1].areanum, 2);
		assert_int_equal(aasworld.reachability[1].facenum, 0);
		assert_int_equal(aasworld.reachability[1].edgenum, 1);
		assert_int_equal(aasworld.reachability[1].traveltype,
			TRAVEL_WALKOFFLEDGE);
		assert_int_equal(aasworld.reachability[1].traveltime,
			traveltimes[testcase]);
		assert_float_equal(aasworld.reachability[1].start[0],
			-8.0f,
			0.0001f);
		assert_float_equal(aasworld.reachability[1].start[2],
			100.0f,
			0.0001f);
		assert_float_equal(aasworld.reachability[1].end[0],
			-8.0f,
			0.0001f);
		assert_float_equal(aasworld.reachability[1].end[2],
			landingheights[testcase] + 0.25f,
			0.0001f);

		AAS_ShutDownReachabilityHeap();
		AAS_ClearReachabilityData();
		free(aasworld.reachability);
		memset(&aasworld, 0, sizeof(aasworld));
	}
}

/*
=============
test_reachability_retail_incremental_lifecycle

Pins retail one-based generation progress, final secondary/entity passes,
sentinel storage, heap cleanup, and delayed AAS initialization.
=============
*/
static void test_reachability_retail_incremental_lifecycle(void **state)
{
	(void)state;
	memset(&aasworld, 0, sizeof(aasworld));
	aas_area_t areas[2] = {0};
	aas_areasettings_t settings[2] = {0};
	areas[1].areanum = 1;
	settings[1].presencetype = PRESENCE_NORMAL | PRESENCE_CROUCH;
	aasworld.loaded = qtrue;
	aasworld.numAreas = 2;
	aasworld.areas = areas;
	aasworld.numAreaSettings = 2;
	aasworld.areasettings = settings;

	AAS_InitReachability();
	assert_true(aasworld.saveFile);
	assert_int_equal(aasworld.numReachabilityAreas, 1);
	assert_true(AAS_ContinueInitReachability());
	assert_int_equal(aasworld.numReachabilityAreas, 2);
	assert_int_equal(aasworld.numReachability, 1);
	assert_non_null(aasworld.reachability);
	assert_false(AAS_ContinueInitReachability());
	assert_false(aasworld.initialized);

	AAS_ContinueInit(0.0f);
	assert_true(aasworld.initialized);
	assert_int_equal(aasworld.numClusters, 2);
	assert_int_equal(aasworld.clusters[1].numareas, 1);
	AAS_ClearReachabilityData();
	free(aasworld.reachability);
	free(aasworld.portals);
	free(aasworld.portalIndex);
	free(aasworld.clusters);
	memset(&aasworld, 0, sizeof(aasworld));
}

/*
=============
test_retail_cluster_portal_rebuild

Pins the retail 12-byte cluster ABI, geometric two-plane portal recognition,
face flooding of otherwise unreachable areas, and two-sided portal numbering.
=============
*/
static void test_retail_cluster_portal_rebuild(void **state)
{
	(void)state;
	memset(&aasworld, 0, sizeof(aasworld));

	aas_area_t areas[4] = {0};
	aas_areasettings_t settings[4] = {0};
	aas_face_t faces[3] = {0};
	int faceindex[4] = {1, 2, 1, 2};
	aas_edge_t edges[3] = {0};
	int edgeindex[2] = {1, 2};

	areas[1].firstface = 0;
	areas[1].numfaces = 2;
	areas[2].firstface = 2;
	areas[2].numfaces = 1;
	areas[3].firstface = 3;
	areas[3].numfaces = 1;
	settings[1].areaflags = AAS_AREA_GROUNDED;
	settings[1].presencetype = PRESENCE_NORMAL | PRESENCE_CROUCH;
	settings[2].presencetype = PRESENCE_NORMAL | PRESENCE_CROUCH;
	settings[3].presencetype = PRESENCE_NORMAL | PRESENCE_CROUCH;

	faces[1].planenum = 0;
	faces[1].firstedge = 0;
	faces[1].numedges = 1;
	faces[1].frontarea = 1;
	faces[1].backarea = 2;
	faces[2].planenum = 2;
	faces[2].firstedge = 1;
	faces[2].numedges = 1;
	faces[2].frontarea = 1;
	faces[2].backarea = 3;

	aasworld.loaded = qtrue;
	aasworld.numAreas = 4;
	aasworld.areas = areas;
	aasworld.numAreaSettings = 4;
	aasworld.areasettings = settings;
	aasworld.numFaces = 3;
	aasworld.faces = faces;
	aasworld.faceIndexSize = 4;
	aasworld.faceIndex = faceindex;
	aasworld.numEdges = 3;
	aasworld.edges = edges;
	aasworld.edgeIndexSize = 2;
	aasworld.edgeIndex = edgeindex;

	assert_int_equal(sizeof(aas_cluster_t), 12);
	AAS_InitClustering();
	assert_true(aasworld.saveFile);
	assert_int_equal(settings[1].contents & AAS_AREACONTENTS_CLUSTERPORTAL,
		AAS_AREACONTENTS_CLUSTERPORTAL);
	assert_int_equal(settings[1].contents & AAS_AREACONTENTS_ROUTEPORTAL,
		AAS_AREACONTENTS_ROUTEPORTAL);
	assert_int_equal(settings[1].cluster, -1);
	assert_int_equal(settings[2].cluster, 1);
	assert_int_equal(settings[3].cluster, 2);
	assert_int_equal(aasworld.numPortals, 2);
	assert_int_equal(aasworld.portalIndexSize, 2);
	assert_int_equal(aasworld.numClusters, 3);
	assert_int_equal(aasworld.portals[1].areanum, 1);
	assert_int_equal(aasworld.portals[1].frontcluster, 1);
	assert_int_equal(aasworld.portals[1].backcluster, 2);
	assert_int_equal(aasworld.portals[1].clusterareanum[0], 1);
	assert_int_equal(aasworld.portals[1].clusterareanum[1], 1);
	assert_int_equal(aasworld.clusters[1].numareas, 2);
	assert_int_equal(aasworld.clusters[1].numportals, 1);
	assert_int_equal(aasworld.clusters[1].firstportal, 0);
	assert_int_equal(aasworld.clusters[2].numareas, 2);
	assert_int_equal(aasworld.clusters[2].numportals, 1);
	assert_int_equal(aasworld.clusters[2].firstportal, 1);
	assert_int_equal(aasworld.portalIndex[0], 1);
	assert_int_equal(aasworld.portalIndex[1], 1);

	free(aasworld.portals);
	free(aasworld.portalIndex);
	free(aasworld.clusters);
	memset(&aasworld, 0, sizeof(aasworld));
}

/*
=============
test_retail_aas_geometry_optimization

Pins ladder-only face retention, compact signed indexes, non-elevator
reachability remapping, and the retail elevator model-number exception.
=============
*/
static void test_retail_aas_geometry_optimization(void **state)
{
	(void)state;
	memset(&aasworld, 0, sizeof(aasworld));

	aasworld.numVertexes = 4;
	aasworld.vertexes = (aas_vertex_t *)calloc(4, sizeof(aas_vertex_t));
	aasworld.numEdges = 4;
	aasworld.edges = (aas_edge_t *)calloc(4, sizeof(aas_edge_t));
	aasworld.edgeIndexSize = 3;
	aasworld.edgeIndex = (int *)calloc(3, sizeof(int));
	aasworld.numFaces = 3;
	aasworld.faces = (aas_face_t *)calloc(3, sizeof(aas_face_t));
	aasworld.faceIndexSize = 3;
	aasworld.faceIndex = (int *)calloc(3, sizeof(int));
	aasworld.numAreas = 3;
	aasworld.areas = (aas_area_t *)calloc(3, sizeof(aas_area_t));
	aasworld.numReachability = 4;
	aasworld.reachability = (aas_reachability_t *)calloc(4,
		sizeof(aas_reachability_t));
	assert_non_null(aasworld.vertexes);
	assert_non_null(aasworld.edges);
	assert_non_null(aasworld.edgeIndex);
	assert_non_null(aasworld.faces);
	assert_non_null(aasworld.faceIndex);
	assert_non_null(aasworld.areas);
	assert_non_null(aasworld.reachability);

	VectorSet(aasworld.vertexes[0], 1.0f, 0.0f, 0.0f);
	VectorSet(aasworld.vertexes[1], 2.0f, 0.0f, 0.0f);
	VectorSet(aasworld.vertexes[2], 3.0f, 0.0f, 0.0f);
	VectorSet(aasworld.vertexes[3], 4.0f, 0.0f, 0.0f);
	aasworld.edges[1].v[0] = 0;
	aasworld.edges[1].v[1] = 1;
	aasworld.edges[2].v[0] = 1;
	aasworld.edges[2].v[1] = 2;
	aasworld.edges[3].v[0] = 2;
	aasworld.edges[3].v[1] = 3;
	aasworld.edgeIndex[0] = 1;
	aasworld.edgeIndex[1] = -2;
	aasworld.edgeIndex[2] = 3;

	aasworld.faces[1].faceflags = AAS_FACE_LADDER;
	aasworld.faces[1].firstedge = 0;
	aasworld.faces[1].numedges = 2;
	aasworld.faces[2].faceflags = AAS_FACE_GROUND;
	aasworld.faces[2].firstedge = 2;
	aasworld.faces[2].numedges = 1;
	aasworld.faceIndex[0] = 1;
	aasworld.faceIndex[1] = 2;
	aasworld.faceIndex[2] = -1;
	aasworld.areas[1].firstface = 0;
	aasworld.areas[1].numfaces = 2;
	aasworld.areas[2].firstface = 2;
	aasworld.areas[2].numfaces = 1;

	aasworld.reachability[1].facenum = 1;
	aasworld.reachability[1].edgenum = -2;
	aasworld.reachability[1].traveltype = TRAVEL_LADDER;
	aasworld.reachability[2].facenum = 2;
	aasworld.reachability[2].edgenum = 3;
	aasworld.reachability[2].traveltype = TRAVEL_WALK;
	aasworld.reachability[3].facenum = 77;
	aasworld.reachability[3].edgenum = 88;
	aasworld.reachability[3].traveltype = TRAVEL_ELEVATOR;
	aasworld.loaded = qtrue;

	AAS_Optimize();
	assert_int_equal(aasworld.numVertexes, 3);
	assert_int_equal(aasworld.numEdges, 3);
	assert_int_equal(aasworld.edgeIndexSize, 2);
	assert_int_equal(aasworld.numFaces, 2);
	assert_int_equal(aasworld.faceIndexSize, 2);
	assert_int_equal(aasworld.numAreas, 3);
	assert_int_equal(aasworld.faces[1].faceflags, AAS_FACE_LADDER);
	assert_int_equal(aasworld.faces[1].firstedge, 0);
	assert_int_equal(aasworld.faces[1].numedges, 2);
	assert_int_equal(aasworld.edgeIndex[0], 1);
	assert_int_equal(aasworld.edgeIndex[1], -2);
	assert_int_equal(aasworld.areas[1].firstface, 0);
	assert_int_equal(aasworld.areas[1].numfaces, 1);
	assert_int_equal(aasworld.areas[2].firstface, 1);
	assert_int_equal(aasworld.areas[2].numfaces, 1);
	assert_int_equal(aasworld.faceIndex[0], 1);
	assert_int_equal(aasworld.faceIndex[1], -1);
	assert_int_equal(aasworld.reachability[1].facenum, 1);
	assert_int_equal(aasworld.reachability[1].edgenum, -2);
	assert_int_equal(aasworld.reachability[2].facenum, 0);
	assert_int_equal(aasworld.reachability[2].edgenum, 0);
	assert_int_equal(aasworld.reachability[3].facenum, 77);
	assert_int_equal(aasworld.reachability[3].edgenum, 88);

	free(aasworld.vertexes);
	free(aasworld.edges);
	free(aasworld.edgeIndex);
	free(aasworld.faces);
	free(aasworld.faceIndex);
	free(aasworld.areas);
	free(aasworld.reachability);
	memset(&aasworld, 0, sizeof(aasworld));
}

/*
=============
test_retail_routing_intra_area_travel_cost

Pins reverse routing across an intermediate area, including the retail
walk-distance cost between the incoming and outgoing reachabilities.
=============
*/
static void test_retail_routing_intra_area_travel_cost(void **state)
{
	(void)state;
	memset(&aasworld, 0, sizeof(aasworld));

	aas_area_t areas[4] = {0};
	aas_areasettings_t settings[4] = {0};
	aas_reachability_t reachability[3] = {0};
	settings[1].firstreachablearea = 1;
	settings[1].numreachableareas = 1;
	settings[2].firstreachablearea = 2;
	settings[2].numreachableareas = 1;
	settings[1].presencetype = PRESENCE_NORMAL | PRESENCE_CROUCH;
	settings[2].presencetype = PRESENCE_NORMAL | PRESENCE_CROUCH;
	settings[3].presencetype = PRESENCE_NORMAL | PRESENCE_CROUCH;
	reachability[1].areanum = 2;
	VectorSet(reachability[1].start, 0.0f, 0.0f, 0.0f);
	VectorSet(reachability[1].end, 100.0f, 0.0f, 0.0f);
	reachability[1].traveltype = TRAVEL_WALK;
	reachability[1].traveltime = 10;
	reachability[2].areanum = 3;
	VectorSet(reachability[2].start, 200.0f, 0.0f, 0.0f);
	VectorSet(reachability[2].end, 300.0f, 0.0f, 0.0f);
	reachability[2].traveltype = TRAVEL_WALK;
	reachability[2].traveltime = 10;

	aasworld.loaded = qtrue;
	aasworld.initialized = qtrue;
	aasworld.numAreas = 4;
	aasworld.areas = areas;
	aasworld.numAreaSettings = 4;
	aasworld.areasettings = settings;
	aasworld.numReachability = 3;
	aasworld.reachability = reachability;
	AAS_InitTravelFlagFromType();
	AAS_InitAreaContentsTravelFlags();
	assert_int_equal(AAS_PrepareReachability(), BLERR_NOERROR);

	vec3_t origin = {0.0f, 0.0f, 0.0f};
	assert_int_equal(AAS_AreaTravelTimeToGoalArea(1,
		origin,
		3,
		TFL_WALK | TFL_AIR),
		54);
	assert_int_equal(AAS_AreaReachabilityToGoalArea(1,
		origin,
		3,
		TFL_WALK | TFL_AIR),
		1);

	AAS_FreeAllRoutingCaches();
	AAS_ClearReachabilityData();
	free(aasworld.areacontentstravelflags);
	memset(&aasworld, 0, sizeof(aasworld));
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_aas_loads_sample_map,
                                        aas_environment_setup,
                                        aas_environment_teardown),
        cmocka_unit_test_setup_teardown(test_aas_entity_linking_and_reachability,
                                        aas_environment_setup,
                                        aas_environment_teardown),
        cmocka_unit_test_setup_teardown(test_routing_frame_respects_framereachability,
                                        aas_environment_setup,
                                        aas_environment_teardown),
        cmocka_unit_test_setup_teardown(test_routing_frame_forcewrite_toggle,
                                        aas_environment_setup,
                                        aas_environment_teardown),
        cmocka_unit_test_setup_teardown(test_reachability_force_reachability_toggle,
                                        aas_environment_setup,
                                        aas_environment_teardown),
        cmocka_unit_test_setup_teardown(test_reachability_force_clustering_toggle,
                                        aas_environment_setup,
                                        aas_environment_teardown),
		cmocka_unit_test(test_reachability_geometry_helpers),
		cmocka_unit_test(test_reachability_area_and_link_helpers),
		cmocka_unit_test(test_reachability_physics_helpers),
		cmocka_unit_test(test_reachability_swim_generation_and_storage),
		cmocka_unit_test(test_reachability_equal_floor_generation_and_storage),
		cmocka_unit_test(test_reachability_adjacent_edge_travel_branches),
		cmocka_unit_test(test_reachability_jump_generation_and_rejections),
		cmocka_unit_test(test_reachability_ladder_shared_edge_generation),
		cmocka_unit_test(test_reachability_retail_teleporter_generation),
		cmocka_unit_test(test_reachability_retail_elevator_generation),
		cmocka_unit_test(test_reachability_retail_grapple_generation),
		cmocka_unit_test(test_reachability_retail_weapon_jump_generation),
		cmocka_unit_test(test_reachability_retail_secondary_walkoff_generation),
		cmocka_unit_test(test_reachability_retail_incremental_lifecycle),
		cmocka_unit_test(test_retail_cluster_portal_rebuild),
		cmocka_unit_test(test_retail_aas_geometry_optimization),
		cmocka_unit_test(test_retail_routing_intra_area_travel_cost),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
