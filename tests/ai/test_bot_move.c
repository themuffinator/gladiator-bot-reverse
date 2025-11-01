#include <stdarg.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cmocka.h>

#include "botlib/ai/move/bot_move.h"
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

static char g_command_log[MAX_TEST_COMMANDS][128];
static int g_command_count;

static void test_reset_command_log(void)
{
    g_command_count = 0;
    for (int i = 0; i < MAX_TEST_COMMANDS; ++i)
    {
        g_command_log[i][0] = '\0';
    }
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
    (void)start;
    (void)mins;
    (void)maxs;
    (void)end;
    (void)passent;
    (void)contentmask;
    bsp_trace_t trace;
    memset(&trace, 0, sizeof(trace));
    trace.fraction = 1.0f;
    return trace;
}

static int test_bridge_point_contents(vec3_t point)
{
    (void)point;
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

static const botlib_import_table_t *g_active_lib_imports = NULL;

void BotInterface_SetImportTable(const botlib_import_table_t *import_table)
{
    g_active_lib_imports = import_table;
}

const botlib_import_table_t *BotInterface_GetImportTable(void)
{
    return g_active_lib_imports;
}

aas_world_t aasworld = {0};

static int test_setup(void **state)
{
    (void)state;

    test_reset_command_log();
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

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_bot_travel_grapple_hook_toggles,
                                        test_setup,
                                        test_teardown),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
