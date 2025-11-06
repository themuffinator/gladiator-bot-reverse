#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cmocka.h>

#include "botlib/aas/aas_local.h"
#include "botlib/aas/aas_map.h"
#include "botlib/ai_move/bot_move.h"
#include "botlib/ai_move/mover_catalogue.h"
#include "botlib/common/bot_common.h"
#include "botlib/common/l_libvar.h"
#include "botlib/interface/botlib_interface.h"

#define TEST_LOG_CAPACITY 8

typedef struct test_log_entry_s
{
    int priority;
    char message[256];
} test_log_entry_t;

static struct
{
    test_log_entry_t entries[TEST_LOG_CAPACITY];
    int count;
} g_test_log;

static void test_log_reset(void)
{
    g_test_log.count = 0;
    for (int i = 0; i < TEST_LOG_CAPACITY; ++i)
    {
        g_test_log.entries[i].priority = 0;
        g_test_log.entries[i].message[0] = '\0';
    }
}

static void test_log_print(int priority, const char *fmt, ...)
{
    if (g_test_log.count >= TEST_LOG_CAPACITY)
    {
        return;
    }

    va_list args;
    va_start(args, fmt);

    test_log_entry_t *entry = &g_test_log.entries[g_test_log.count++];
    entry->priority = priority;
    vsnprintf(entry->message, sizeof(entry->message), fmt != NULL ? fmt : "", args);

    va_end(args);
}

static void test_register_mover(int modelnum, bot_mover_kind_t kind)
{
    bot_mover_catalogue_entry_t entry = {
        .modelnum = modelnum,
        .lip = 0.0f,
        .height = 0.0f,
        .speed = 0.0f,
        .spawnflags = 0,
        .doortype = 0,
        .kind = kind,
    };

    assert_true(BotMove_MoverCatalogueInsert(&entry));
}

static void test_spawn_brush_entity(int entnum,
                                    const vec3_t origin,
                                    const vec3_t mins,
                                    const vec3_t maxs,
                                    int modelnum)
{
    AASEntityFrame frame;
    memset(&frame, 0, sizeof(frame));

    VectorCopy(origin, frame.origin);
    VectorCopy(origin, frame.old_origin);
    VectorCopy(origin, frame.previous_origin);
    VectorCopy(mins, frame.mins);
    VectorCopy(maxs, frame.maxs);

    frame.origin_dirty = qtrue;
    frame.bounds_dirty = qtrue;
    frame.solid = SOLID_BSP;
    frame.modelindex = modelnum + 1;

    int status = AAS_UpdateEntity(entnum, &frame);
    assert_int_equal(status, BLERR_NOERROR);
}

typedef struct test_map_fixture_s
{
    aas_area_t *areas;
    aas_areasettings_t *settings;
    aas_reachability_t *reachability;
} test_map_fixture_t;

static int test_setup(void **state)
{
    test_log_reset();
    BotMove_MoverCatalogueReset();
    LibVarSet("bot_developer", "0");

    memset(&aasworld, 0, sizeof(aasworld));
    aasworld.loaded = qtrue;
    aasworld.numAreas = 3;
    aasworld.numAreaSettings = 4;
    aasworld.numReachability = 3;
    aasworld.time = 0.0f;

    test_map_fixture_t *fixture = calloc(1, sizeof(*fixture));
    if (fixture == NULL)
    {
        return -1;
    }

    fixture->areas = calloc((size_t)(aasworld.numAreas + 1), sizeof(aas_area_t));
    fixture->settings = calloc((size_t)aasworld.numAreaSettings, sizeof(aas_areasettings_t));
    fixture->reachability = calloc((size_t)aasworld.numReachability, sizeof(aas_reachability_t));
    if (fixture->areas == NULL || fixture->settings == NULL || fixture->reachability == NULL)
    {
        free(fixture->areas);
        free(fixture->settings);
        free(fixture->reachability);
        free(fixture);
        return -1;
    }

    aasworld.areas = fixture->areas;
    aasworld.areasettings = fixture->settings;
    aasworld.reachability = fixture->reachability;

    aasworld.areasettings[1].firstreachablearea = 1;
    aasworld.areasettings[1].numreachableareas = 1;
    aasworld.areasettings[2].firstreachablearea = 2;
    aasworld.areasettings[2].numreachableareas = 1;

    aasworld.reachability[1].areanum = 2;
    aasworld.reachability[1].traveltype = TRAVEL_WALK;
    VectorClear(aasworld.reachability[1].start);
    VectorSet(aasworld.reachability[1].end, 128.0f, 0.0f, 0.0f);

    aasworld.reachability[2].areanum = 3;
    aasworld.reachability[2].traveltype = TRAVEL_LADDER;
    VectorSet(aasworld.reachability[2].start, 128.0f, 0.0f, 0.0f);
    VectorSet(aasworld.reachability[2].end, 128.0f, 0.0f, 64.0f);

    memset(aasworld.travelflagfortype, 0, sizeof(aasworld.travelflagfortype));
    aasworld.travelflagfortype[TRAVEL_WALK] = TFL_WALK;
    aasworld.travelflagfortype[TRAVEL_LADDER] = TFL_LADDER;

    static const botlib_import_table_t imports = {
        .Print = test_log_print,
        .DPrint = NULL,
        .BotLibVarGet = NULL,
        .BotLibVarSet = NULL,
    };
    BotInterface_SetImportTable(&imports);

    *state = fixture;
    return 0;
}

static int test_teardown(void **state)
{
    BotInterface_SetImportTable(NULL);

    test_map_fixture_t *fixture = (test_map_fixture_t *)(*state);
    if (fixture != NULL)
    {
        free(fixture->areas);
        free(fixture->settings);
        free(fixture->reachability);
        free(fixture);
    }

    memset(&aasworld, 0, sizeof(aasworld));
    BotMove_MoverCatalogueReset();
    LibVarSet("bot_developer", "0");
    return 0;
}

static void test_bot_move_multi_hop_path(void **state)
{
    (void)state;

    int movestate = BotAllocMoveState();
    assert_true(movestate > 0);

    bot_movestate_t *ms = BotMoveStateFromHandle(movestate);
    assert_non_null(ms);

    BotResetMoveState(movestate);
    ms->areanum = 1;
    VectorClear(ms->origin);

    bot_goal_t goal;
    memset(&goal, 0, sizeof(goal));
    goal.areanum = 3;
    VectorSet(goal.origin, 256.0f, 0.0f, 64.0f);

    bot_moveresult_t result;
    BotClearMoveResult(&result);

    BotMoveToGoal(&result, movestate, &goal, TFL_DEFAULT);

    assert_int_equal(result.traveltype, TRAVEL_WALK);
    assert_int_equal(ms->lastreachnum, 1);
    assert_int_equal(ms->lastgoalareanum, 3);
    assert_int_equal(ms->reachareanum, 2);

    BotFreeMoveState(movestate);
}

static void test_bot_move_unknown_travel_logs_warning(void **state)
{
    (void)state;

    test_log_reset();
    int movestate = BotAllocMoveState();
    assert_true(movestate > 0);

    bot_movestate_t *ms = BotMoveStateFromHandle(movestate);
    assert_non_null(ms);

    BotResetMoveState(movestate);
    ms->areanum = 1;
    VectorClear(ms->origin);

    int original_type = aasworld.reachability[1].traveltype;
    aasworld.reachability[1].traveltype = TRAVEL_DOUBLEJUMP;

    bot_goal_t goal;
    memset(&goal, 0, sizeof(goal));
    goal.areanum = 2;
    VectorSet(goal.origin, 128.0f, 0.0f, 0.0f);

    bot_moveresult_t result;
    BotClearMoveResult(&result);

    BotMoveToGoal(&result, movestate, &goal, TFL_DEFAULT);

    assert_true(result.failure);
    assert_int_equal(g_test_log.count, 1);
    assert_string_equal(g_test_log.entries[0].message, "(last) travel type 15 not implemented yet\n");

    aasworld.reachability[1].traveltype = original_type;
    BotFreeMoveState(movestate);
}

static void test_bot_move_relinks_func_plat_logs_message(void **state)
{
    test_map_fixture_t *fixture = (test_map_fixture_t *)(*state);
    (void)fixture;

    test_log_reset();
    BotMove_MoverCatalogueReset();
    LibVarSet("bot_developer", "0");

    test_register_mover(7, BOT_MOVER_KIND_FUNC_PLAT);

    vec3_t origin = {0.0f, 0.0f, 0.0f};
    vec3_t mins = {-16.0f, -16.0f, 0.0f};
    vec3_t maxs = {16.0f, 16.0f, 32.0f};
    test_spawn_brush_entity(3, origin, mins, maxs, 7);

    aasworld.time = 1.0f;

    fixture->reachability[1].traveltype = TRAVEL_ELEVATOR;
    fixture->reachability[1].facenum = (fixture->reachability[1].facenum & 0xFFFF0000) | 7;
    fixture->reachability[1].areanum = 2;

    int movestate = BotAllocMoveState();
    assert_true(movestate > 0);

    bot_movestate_t *ms = BotMoveStateFromHandle(movestate);
    assert_non_null(ms);
    BotResetMoveState(movestate);
    ms->areanum = 1;
    VectorClear(ms->origin);
    ms->origin[2] = 32.0f;
    ms->moveflags = MFL_ONGROUND;
    ms->client = 0;

    bot_goal_t goal;
    memset(&goal, 0, sizeof(goal));
    goal.areanum = 3;
    VectorSet(goal.origin, 256.0f, 0.0f, 64.0f);

    bot_moveresult_t result;
    BotClearMoveResult(&result);

    BotMoveToGoal(&result, movestate, &goal, TFL_DEFAULT);

    assert_int_equal(ms->lastreachnum, 1);
    assert_true((result.diagnostics & BOT_MOVE_DIAG_FUNCPLAT_RELINKED) != 0U);
    assert_int_equal(g_test_log.count, 1);
    assert_int_equal(g_test_log.entries[0].priority, PRT_MESSAGE);
    assert_string_equal(g_test_log.entries[0].message,
                       "client 0: relinking brush model ent 3\n");

    int status = AAS_UpdateEntity(3, NULL);
    assert_int_equal(status, BLERR_NOERROR);
    BotFreeMoveState(movestate);
}

static void test_bot_move_func_bobbing_without_reachability_logs_warning(void **state)
{
    test_map_fixture_t *fixture = (test_map_fixture_t *)(*state);
    (void)fixture;

    test_log_reset();
    BotMove_MoverCatalogueReset();
    LibVarSet("bot_developer", "1");

    test_register_mover(8, BOT_MOVER_KIND_FUNC_BOB);

    vec3_t origin = {0.0f, 0.0f, 0.0f};
    vec3_t mins = {-16.0f, -16.0f, 0.0f};
    vec3_t maxs = {16.0f, 16.0f, 32.0f};
    test_spawn_brush_entity(4, origin, mins, maxs, 8);

    aasworld.time = 2.0f;

    fixture->reachability[1].traveltype = TRAVEL_WALK;
    fixture->reachability[1].facenum = 0;
    fixture->reachability[2].traveltype = TRAVEL_LADDER;
    fixture->reachability[2].facenum = 0;

    int movestate = BotAllocMoveState();
    assert_true(movestate > 0);

    bot_movestate_t *ms = BotMoveStateFromHandle(movestate);
    assert_non_null(ms);
    BotResetMoveState(movestate);
    ms->areanum = 1;
    VectorClear(ms->origin);
    ms->origin[2] = 32.0f;
    ms->moveflags = MFL_ONGROUND;
    ms->client = 0;

    bot_goal_t goal;
    memset(&goal, 0, sizeof(goal));
    goal.areanum = 3;
    VectorSet(goal.origin, 64.0f, 0.0f, 0.0f);

    bot_moveresult_t result;
    BotClearMoveResult(&result);

    BotMoveToGoal(&result, movestate, &goal, TFL_DEFAULT);

    assert_true(result.failure);
    assert_true(result.blocked);
    assert_int_equal(result.blockentity, 4);
    assert_true((result.flags & MOVERESULT_ONTOPOFOBSTACLE) != 0);
    assert_true((result.diagnostics & BOT_MOVE_DIAG_FUNCBOB_FAILED) != 0U);
    assert_int_equal(g_test_log.count, 1);
    assert_int_equal(g_test_log.entries[0].priority, PRT_MESSAGE);
    assert_string_equal(g_test_log.entries[0].message,
                       "client 0: on func_bobbing without reachability\n");

    int status = AAS_UpdateEntity(4, NULL);
    assert_int_equal(status, BLERR_NOERROR);
    BotFreeMoveState(movestate);
    LibVarSet("bot_developer", "0");
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_bot_move_multi_hop_path, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_bot_move_unknown_travel_logs_warning, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_bot_move_relinks_func_plat_logs_message,
                                        test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_bot_move_func_bobbing_without_reachability_logs_warning,
                                        test_setup,
                                        test_teardown),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
