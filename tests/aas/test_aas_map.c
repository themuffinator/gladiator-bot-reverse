#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <setjmp.h>
#include <cmocka.h>

#include <stdbool.h>

#ifdef _WIN32
#include <direct.h>
#define chdir _chdir
#define getcwd _getcwd
#define unlink _unlink
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#include "botlib_aas/aas_map.h"
#include "botlib_aas/aas_local.h"
#include "botlib_common/l_libvar.h"
#include "botlib_common/l_memory.h"
#include "botlib_interface/botlib_interface.h"
#include "q2bridge/aas_translation.h"
#include "q2bridge/botlib.h"
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

static const botlib_import_table_t g_test_imports = {
    .Print = test_capture_print,
    .DPrint = test_capture_dprint,
    .BotLibVarGet = test_libvar_get,
    .BotLibVarSet = test_libvar_set,
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
    status = TranslateEntityUpdate(1, &fixtures[0], aasworld.time, &translated);
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
    status = TranslateEntityUpdate(1, &fixtures[1], aasworld.time, &translated);
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
    status = TranslateEntityUpdate(1, &fixtures[2], aasworld.time, &translated);
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
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
