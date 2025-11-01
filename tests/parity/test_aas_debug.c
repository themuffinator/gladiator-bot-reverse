#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

#include <cmocka.h>

#include "botlib/aas/aas_debug.h"
#include "botlib/aas/aas_local.h"
#include "botlib/interface/botlib_interface.h"

#define ARRAY_LEN(x) (sizeof(x) / sizeof((x)[0]))

typedef struct captured_print_s
{
    int priority;
    char message[256];
} captured_print_t;

typedef struct aas_debug_test_context_s
{
    botlib_import_table_t imports;
    captured_print_t prints[64];
    size_t print_count;
    aas_area_t *areas;
    aas_reachability_t *reachability;
    aas_areasettings_t *areasettings;
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

static void Mock_Reset(aas_debug_test_context_t *context)
{
    if (context == NULL)
    {
        return;
    }

    context->print_count = 0U;
    memset(context->prints, 0, sizeof(context->prints));
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
    aasworld.numAreas = 3;
    aasworld.numReachability = 2;
    aasworld.numAreaSettings = aasworld.numAreas;

    context->areas = (aas_area_t *)calloc((size_t)aasworld.numAreas + 1U, sizeof(aas_area_t));
    assert_non_null(context->areas);
    aasworld.areas = context->areas;

    for (int areanum = 1; areanum <= aasworld.numAreas; ++areanum)
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

    context->areasettings = (aas_areasettings_t *)calloc((size_t)aasworld.numAreaSettings + 1U,
                                                        sizeof(aas_areasettings_t));
    assert_non_null(context->areasettings);
    aasworld.areasettings = context->areasettings;

    for (int areanum = 1; areanum <= aasworld.numAreaSettings; ++areanum)
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
}

static int setup_aas_debug(void **state)
{
    aas_debug_test_context_t *context = (aas_debug_test_context_t *)calloc(1, sizeof(*context));
    assert_non_null(context);

    context->imports.Print = Mock_Print;
    BotInterface_SetImportTable(&context->imports);

    g_active_context = context;
    BuildMockMap(context);

    *state = context;
    return 0;
}

static int teardown_aas_debug(void **state)
{
    aas_debug_test_context_t *context = (aas_debug_test_context_t *)(*state);
    BotInterface_SetImportTable(NULL);

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

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_bot_test_dumps_area_info, setup_aas_debug, teardown_aas_debug),
        cmocka_unit_test_setup_teardown(test_aas_showpath_reports_path, setup_aas_debug, teardown_aas_debug),
        cmocka_unit_test_setup_teardown(test_aas_showareas_lists_requested_areas, setup_aas_debug, teardown_aas_debug),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
