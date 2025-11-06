#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cmocka.h>

#include "q2bridge/bridge.h"
#include "botlib/common/l_libvar.h"

#include "botlib_contract_loader.h"

#define ARRAY_LEN(x) (sizeof(x) / sizeof((x)[0]))

struct cvar_s
{
    const char *name;
    const char *string;
    int flags;
    float value;
    struct cvar_s *next;
};

typedef struct captured_print_s
{
    int severity;
    char message[1024];
} captured_print_t;

typedef struct bridge_import_mock_s
{
    bot_import_t table;
    captured_print_t prints[32];
    size_t print_count;

    int bot_input_calls;
    int bot_input_client;
    bot_input_t last_input;

    int command_calls;
    int command_client;
    char *last_command;

    int trace_calls;

    int memory_alloc_calls;
    int memory_free_calls;
    int last_allocation_size;

    int debug_line_create_calls;
    int debug_line_delete_calls;
    int debug_line_show_calls;
    int next_debug_line_id;

    int error_calls;
    char last_error[1024];

    struct cvar_s mock_cvar;
    int cvar_calls;
} bridge_import_mock_t;

typedef struct bridge_test_context_s
{
    bridge_import_mock_t mock;
    botlib_contract_catalogue_t catalogue;
} bridge_test_context_t;

static bridge_import_mock_t *g_active_bridge_mock = NULL;

static libvar_t g_bridge_mock_maxclients;
static char g_bridge_mock_maxclients_name[32];
static char g_bridge_mock_maxclients_string[32];

static void BridgeMock_Reset(bridge_import_mock_t *mock)
{
    if (mock == NULL)
    {
        return;
    }

    if (mock->last_command != NULL)
    {
        free(mock->last_command);
        mock->last_command = NULL;
    }

    memset(mock->prints, 0, sizeof(mock->prints));
    mock->print_count = 0U;
    mock->bot_input_calls = 0;
    mock->command_calls = 0;
    mock->trace_calls = 0;
    mock->memory_alloc_calls = 0;
    mock->memory_free_calls = 0;
    mock->last_allocation_size = 0;
    mock->debug_line_create_calls = 0;
    mock->debug_line_delete_calls = 0;
    mock->debug_line_show_calls = 0;
    mock->next_debug_line_id = 1;
    mock->error_calls = 0;
    mock->last_error[0] = '\0';
    mock->cvar_calls = 0;
}

static void BridgeMock_RecordPrint(int severity, const char *message)
{
    if (g_active_bridge_mock == NULL || message == NULL)
    {
        return;
    }

    bridge_import_mock_t *mock = g_active_bridge_mock;
    if (mock->print_count < ARRAY_LEN(mock->prints))
    {
        captured_print_t *slot = &mock->prints[mock->print_count++];
        slot->severity = severity;
        strncpy(slot->message, message, sizeof(slot->message) - 1U);
        slot->message[sizeof(slot->message) - 1U] = '\0';
    }
}

static void Mock_Print(int type, char *fmt, ...)
{
    if (fmt == NULL)
    {
        return;
    }

    va_list args;
    va_start(args, fmt);

    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), fmt, args);

    va_end(args);

    BridgeMock_RecordPrint(type, buffer);
}

static void Mock_Error(const char *fmt, ...)
{
    if (g_active_bridge_mock == NULL || fmt == NULL)
    {
        return;
    }

    bridge_import_mock_t *mock = g_active_bridge_mock;

    va_list args;
    va_start(args, fmt);

    vsnprintf(mock->last_error, sizeof(mock->last_error), fmt, args);

    va_end(args);

    mock->error_calls += 1;
}

static cvar_t *Mock_CvarGet(const char *name, const char *default_value, int flags)
{
    (void)default_value;
    (void)flags;

    if (g_active_bridge_mock == NULL)
    {
        return NULL;
    }

    bridge_import_mock_t *mock = g_active_bridge_mock;
    mock->cvar_calls += 1;
    mock->mock_cvar.name = name;
    return &mock->mock_cvar;
}

static void Mock_BotInput(int client, bot_input_t *input)
{
    if (g_active_bridge_mock == NULL || input == NULL)
    {
        return;
    }

    bridge_import_mock_t *mock = g_active_bridge_mock;
    mock->bot_input_calls += 1;
    mock->bot_input_client = client;
    mock->last_input = *input;
}

static void Mock_BotClientCommand(int client, char *fmt, ...)
{
    if (g_active_bridge_mock == NULL || fmt == NULL)
    {
        return;
    }

    bridge_import_mock_t *mock = g_active_bridge_mock;

    va_list args;
    va_start(args, fmt);

    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), fmt, args);

    va_end(args);

    if (mock->last_command != NULL)
    {
        free(mock->last_command);
    }

    size_t length = strlen(buffer);
    mock->last_command = (char *)malloc(length + 1U);
    assert_non_null(mock->last_command);
    memcpy(mock->last_command, buffer, length + 1U);
    mock->command_calls += 1;
    mock->command_client = client;
}

static bsp_trace_t Mock_Trace(vec3_t start,
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

    if (g_active_bridge_mock != NULL)
    {
        g_active_bridge_mock->trace_calls += 1;
    }

    return (bsp_trace_t){0};
}

static int Mock_PointContents(vec3_t point)
{
    (void)point;
    return 0;
}

static void *Mock_GetMemory(int size)
{
    if (g_active_bridge_mock == NULL)
    {
        return NULL;
    }

    bridge_import_mock_t *mock = g_active_bridge_mock;
    mock->memory_alloc_calls += 1;
    mock->last_allocation_size = size;
    return malloc((size_t)((size > 0) ? size : 0));
}

static void Mock_FreeMemory(void *ptr)
{
    if (g_active_bridge_mock == NULL)
    {
        free(ptr);
        return;
    }

    bridge_import_mock_t *mock = g_active_bridge_mock;
    mock->memory_free_calls += 1;
    free(ptr);
}

static int Mock_DebugLineCreate(void)
{
    if (g_active_bridge_mock == NULL)
    {
        return -1;
    }

    bridge_import_mock_t *mock = g_active_bridge_mock;
    mock->debug_line_create_calls += 1;
    return mock->next_debug_line_id++;
}

static void Mock_DebugLineDelete(int line)
{
    if (g_active_bridge_mock == NULL)
    {
        return;
    }

    (void)line;
    g_active_bridge_mock->debug_line_delete_calls += 1;
}

static void Mock_DebugLineShow(int line, vec3_t start, vec3_t end, int color)
{
    (void)line;
    (void)start;
    (void)end;
    (void)color;

    if (g_active_bridge_mock == NULL)
    {
        return;
    }

    g_active_bridge_mock->debug_line_show_calls += 1;
}

static void BridgeTest_SetMaxClients(int value)
{
    if (value < 0)
    {
        value = 0;
    }

    int written = snprintf(g_bridge_mock_maxclients_string,
                           sizeof(g_bridge_mock_maxclients_string),
                           "%d",
                           value);
    if (written < 0)
    {
        g_bridge_mock_maxclients_string[0] = '\0';
    }
    g_bridge_mock_maxclients_string[sizeof(g_bridge_mock_maxclients_string) - 1U] = '\0';

    g_bridge_mock_maxclients.value = (float)value;
    g_bridge_mock_maxclients.modified = false;
}

libvar_t *Bridge_MaxClients(void)
{
    return &g_bridge_mock_maxclients;
}

static int bridge_test_setup(void **state)
{
    bridge_test_context_t *context = (bridge_test_context_t *)calloc(1, sizeof(*context));
    assert_non_null(context);

    g_active_bridge_mock = &context->mock;
    BridgeMock_Reset(&context->mock);

    context->mock.table.BotInput = Mock_BotInput;
    context->mock.table.BotClientCommand = Mock_BotClientCommand;
    context->mock.table.Print = Mock_Print;
    context->mock.table.CvarGet = Mock_CvarGet;
    context->mock.table.Error = Mock_Error;
    context->mock.table.Trace = Mock_Trace;
    context->mock.table.PointContents = Mock_PointContents;
    context->mock.table.GetMemory = Mock_GetMemory;
    context->mock.table.FreeMemory = Mock_FreeMemory;
    context->mock.table.DebugLineCreate = Mock_DebugLineCreate;
    context->mock.table.DebugLineDelete = Mock_DebugLineDelete;
    context->mock.table.DebugLineShow = Mock_DebugLineShow;

    strncpy(g_bridge_mock_maxclients_name,
            "maxclients",
            sizeof(g_bridge_mock_maxclients_name) - 1U);
    g_bridge_mock_maxclients_name[sizeof(g_bridge_mock_maxclients_name) - 1U] = '\0';
    g_bridge_mock_maxclients.name = g_bridge_mock_maxclients_name;
    g_bridge_mock_maxclients.string = g_bridge_mock_maxclients_string;
    g_bridge_mock_maxclients.next = NULL;
    BridgeTest_SetMaxClients(4);

    char contract_path[1024];
    int written = snprintf(contract_path,
                           sizeof(contract_path),
                           "%s/tests/reference/botlib_contract.json",
                           PROJECT_SOURCE_DIR);
    assert_true(written > 0);
    assert_true((size_t)written < sizeof(contract_path));

    int load_status = BotlibContract_Load(contract_path, &context->catalogue);
    assert_int_equal(load_status, 0);

    Q2Bridge_SetImportTable(&context->mock.table);
    Q2Bridge_SetDebugLinesEnabled(false);

    *state = context;
    return 0;
}

static int bridge_test_teardown(void **state)
{
    bridge_test_context_t *context = (bridge_test_context_t *)(state != NULL ? *state : NULL);
    Q2Bridge_ClearImportTable();
    Q2Bridge_SetDebugLinesEnabled(false);
    g_active_bridge_mock = NULL;

    if (context != NULL)
    {
        BridgeMock_Reset(&context->mock);
        BotlibContract_Free(&context->catalogue);
        free(context);
    }

    return 0;
}

static void BridgeTest_AssertInvalidClientLog(const bridge_test_context_t *context,
                                              const char *function_name,
                                              int client,
                                              int max_index)
{
    assert_non_null(context);
    assert_true(context->mock.print_count > 0);

    const captured_print_t *print = &context->mock.prints[0];
    const botlib_contract_export_t *guard =
        BotlibContract_FindExport(&context->catalogue, "GuardClientNumber");
    assert_non_null(guard);

    const botlib_contract_message_t *message =
        BotlibContract_FindMessageWithSeverity(guard, print->severity);
    assert_non_null(message);

    char expected[256];
    int written = snprintf(expected, sizeof(expected), message->text, function_name, client, max_index);
    assert_true(written > 0);
    assert_true((size_t)written < sizeof(expected));
    assert_string_equal(print->message, expected);
}

static void test_bridge_rejects_invalid_bot_input(void **state)
{
    bridge_test_context_t *context = (bridge_test_context_t *)*state;
    BridgeMock_Reset(&context->mock);
    BridgeTest_SetMaxClients(4);

    bot_input_t input = {0};
    input.thinktime = 0.1f;

    Q2_BotInput(4, &input);

    assert_int_equal(context->mock.bot_input_calls, 0);
    BridgeTest_AssertInvalidClientLog(context, "BotInput", 4, 3);
}

static void test_bridge_formats_bot_client_command(void **state)
{
    bridge_test_context_t *context = (bridge_test_context_t *)*state;
    BridgeMock_Reset(&context->mock);
    BridgeTest_SetMaxClients(8);

    size_t long_length = 1500U;
    char *long_text = (char *)malloc(long_length + 1U);
    assert_non_null(long_text);
    memset(long_text, 'a', long_length);
    long_text[long_length] = '\0';

    Q2_BotClientCommand(3, "%s", long_text);

    assert_int_equal(context->mock.command_calls, 1);
    assert_non_null(context->mock.last_command);
    assert_string_equal(context->mock.last_command, long_text);

    free(long_text);

    BridgeMock_Reset(&context->mock);
    BridgeTest_SetMaxClients(4);
    Q2_BotClientCommand(6, "echo test");

    assert_int_equal(context->mock.command_calls, 0);
    BridgeTest_AssertInvalidClientLog(context, "BotClientCommand", 6, 3);
}

static void test_bridge_cvar_get_forwards(void **state)
{
    bridge_test_context_t *context = (bridge_test_context_t *)*state;
    BridgeMock_Reset(&context->mock);

    const char *name = "sv_gravity";
    cvar_t *result = Q2_CvarGet(name, "800", 0);
    assert_ptr_equal(result, &context->mock.mock_cvar);
    assert_int_equal(context->mock.cvar_calls, 1);
    assert_string_equal(context->mock.mock_cvar.name, name);
}

static void test_bridge_error_uses_error_callback(void **state)
{
    bridge_test_context_t *context = (bridge_test_context_t *)*state;
    BridgeMock_Reset(&context->mock);

    Q2_Error("fatal %d", 7);
    assert_int_equal(context->mock.error_calls, 1);
    assert_string_equal(context->mock.last_error, "fatal 7");

    BridgeMock_Reset(&context->mock);
    context->mock.table.Error = NULL;

    Q2_Error("fallback %d", 12);
    assert_int_equal(context->mock.error_calls, 0);
    assert_true(context->mock.print_count > 0);
    assert_int_equal(context->mock.prints[0].severity, PRT_ERROR);
    assert_string_equal(context->mock.prints[0].message, "fallback 12");

    context->mock.table.Error = Mock_Error;
}

static void test_bridge_trace_missing_import_returns_empty(void **state)
{
    bridge_test_context_t *context = (bridge_test_context_t *)*state;
    BridgeMock_Reset(&context->mock);

    context->mock.table.Trace = NULL;

    vec3_t start = {0.0f, 0.0f, 0.0f};
    vec3_t end = {32.0f, 0.0f, 0.0f};
    bsp_trace_t trace = Q2_Trace(start, NULL, NULL, end, 1, 0);

    assert_float_equal(trace.fraction, 1.0f, 0.0001f);
    assert_int_equal(context->mock.trace_calls, 0);
    assert_true(context->mock.print_count > 0);
    assert_string_equal(context->mock.prints[0].message, "[q2bridge] Trace: import not available\n");

    context->mock.table.Trace = Mock_Trace;
}

static void test_bridge_memory_guards(void **state)
{
    bridge_test_context_t *context = (bridge_test_context_t *)*state;
    BridgeMock_Reset(&context->mock);

    void *pointer = Q2_GetMemory(128);
    assert_non_null(pointer);
    assert_int_equal(context->mock.memory_alloc_calls, 1);
    assert_int_equal(context->mock.last_allocation_size, 128);

    Q2_FreeMemory(pointer);
    assert_int_equal(context->mock.memory_free_calls, 1);

    BridgeMock_Reset(&context->mock);
    Q2_GetMemory(0);
    assert_int_equal(context->mock.memory_alloc_calls, 0);

    context->mock.table.GetMemory = NULL;
    BridgeMock_Reset(&context->mock);
    void *missing = Q2_GetMemory(64);
    assert_null(missing);
    assert_int_equal(context->mock.memory_alloc_calls, 0);
    context->mock.table.GetMemory = Mock_GetMemory;
}

static void test_bridge_debug_line_toggle(void **state)
{
    bridge_test_context_t *context = (bridge_test_context_t *)*state;
    BridgeMock_Reset(&context->mock);

    Q2Bridge_SetDebugLinesEnabled(false);
    int disabled_id = Q2_DebugLineCreate();
    assert_int_equal(disabled_id, -1);
    assert_int_equal(context->mock.debug_line_create_calls, 0);

    vec3_t start = {0.0f, 0.0f, 0.0f};
    vec3_t end = {16.0f, 0.0f, 0.0f};
    Q2_DebugLineShow(1, start, end, LINECOLOR_RED);
    assert_int_equal(context->mock.debug_line_show_calls, 0);

    Q2Bridge_SetDebugLinesEnabled(true);
    context->mock.next_debug_line_id = 5;
    int enabled_id = Q2_DebugLineCreate();
    assert_int_equal(enabled_id, 5);
    assert_int_equal(context->mock.debug_line_create_calls, 1);

    Q2_DebugLineShow(enabled_id, start, end, LINECOLOR_GREEN);
    assert_int_equal(context->mock.debug_line_show_calls, 1);

    Q2Bridge_SetDebugLinesEnabled(false);
    Q2_DebugLineDelete(enabled_id);
    assert_int_equal(context->mock.debug_line_delete_calls, 0);
    Q2Bridge_SetDebugLinesEnabled(false);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_bridge_rejects_invalid_bot_input,
                                        bridge_test_setup,
                                        bridge_test_teardown),
        cmocka_unit_test_setup_teardown(test_bridge_formats_bot_client_command,
                                        bridge_test_setup,
                                        bridge_test_teardown),
        cmocka_unit_test_setup_teardown(test_bridge_cvar_get_forwards,
                                        bridge_test_setup,
                                        bridge_test_teardown),
        cmocka_unit_test_setup_teardown(test_bridge_error_uses_error_callback,
                                        bridge_test_setup,
                                        bridge_test_teardown),
        cmocka_unit_test_setup_teardown(test_bridge_trace_missing_import_returns_empty,
                                        bridge_test_setup,
                                        bridge_test_teardown),
        cmocka_unit_test_setup_teardown(test_bridge_memory_guards,
                                        bridge_test_setup,
                                        bridge_test_teardown),
        cmocka_unit_test_setup_teardown(test_bridge_debug_line_toggle,
                                        bridge_test_setup,
                                        bridge_test_teardown),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
