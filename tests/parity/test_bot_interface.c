#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>

#include <cmocka.h>

#include "botlib/interface/bot_interface.h"
#include "botlib/interface/bot_state.h"
#include "botlib/ai/chat/ai_chat.h"
#include "botlib/ea/ea_local.h"
#include "botlib/ai/move/mover_catalogue.h"
#include "botlib/ai/goal_move_orchestrator.h"
#include "botlib/ai/goal/bot_goal.h"
#include "botlib/common/l_log.h"
#include "botlib/common/l_memory.h"
#include "botlib/common/l_libvar.h"
#include "botlib/common/l_struct.h"
#include "botlib/common/l_utils.h"
#include "botlib/aas/aas_sound.h"
#include "botlib/aas/aas_local.h"
#include "botlib/aas/aas_debug.h"
#include "botlib_contract_loader.h"
#include "../support/asset_env.h"
#include "q2bridge/bridge_config.h"
#include "q2bridge/update_translator.h"

#define ARRAY_LEN(x) (sizeof(x) / sizeof((x)[0]))

#ifndef AREACONTENTS_MOVER
#define AREACONTENTS_MOVER 1024
#endif

typedef struct captured_print_s
{
    int type;
    char message[1024];
} captured_print_t;

typedef struct mock_bot_import_s
{
    bot_import_t table;
    captured_print_t prints[128];
    size_t print_count;
    bot_input_t inputs[64];
    int input_clients[64];
    size_t bot_input_count;
    struct
    {
        char name[64];
        void (*function)(void);
    } commands[16];
    size_t command_count;
    char *command_args[16];
    int command_argc;
} mock_bot_import_t;

typedef struct bot_interface_test_context_s
{
    asset_env_t assets;
    mock_bot_import_t mock;
    bot_export_t *api;
    botlib_contract_catalogue_t catalogue;
    bool libvar_initialised;
} bot_interface_test_context_t;

static mock_bot_import_t *g_active_mock = NULL;
static int g_mock_import_libvar_set_status = BLERR_NOERROR;

static void Mock_Print(int type, char *fmt, ...)
{
    if (g_active_mock == NULL || fmt == NULL)
    {
        return;
    }

    va_list args;
    va_start(args, fmt);

    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), fmt, args);

    va_end(args);

    if (g_active_mock->print_count < ARRAY_LEN(g_active_mock->prints))
    {
        captured_print_t *slot = &g_active_mock->prints[g_active_mock->print_count++];
        slot->type = type;
        strncpy(slot->message, buffer, sizeof(slot->message) - 1);
        slot->message[sizeof(slot->message) - 1] = '\0';
    }
    else if (!g_active_mock->print_count)
    {
        return;
    }
    else
    {
        memmove(&g_active_mock->prints[0],
                &g_active_mock->prints[1],
                (ARRAY_LEN(g_active_mock->prints) - 1) * sizeof(g_active_mock->prints[0]));

        captured_print_t *slot = &g_active_mock->prints[ARRAY_LEN(g_active_mock->prints) - 1];
        slot->type = type;
        strncpy(slot->message, buffer, sizeof(slot->message) - 1);
        slot->message[sizeof(slot->message) - 1] = '\0';
    }
}

static void Mock_BotInput(int client, bot_input_t *input)
{
    if (g_active_mock == NULL || input == NULL)
    {
        return;
    }

    size_t index = g_active_mock->bot_input_count;
    if (index >= ARRAY_LEN(g_active_mock->inputs))
    {
        return;
    }

    g_active_mock->inputs[index] = *input;
    g_active_mock->input_clients[index] = client;
    g_active_mock->bot_input_count += 1U;
}

static int Mock_ImportBotLibVarSet(const char *var_name, const char *value)
{
    (void)var_name;
    (void)value;
    return g_mock_import_libvar_set_status;
}

static void Mock_BotClientCommand(int client, char *fmt, ...)
{
    (void)client;
    (void)fmt;
}

static bsp_trace_t Mock_Trace(vec3_t start, vec3_t mins, vec3_t maxs, vec3_t end, int passent, int contentmask)
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

static int Mock_PointContents(vec3_t point)
{
    (void)point;
    return 0;
}

static void *Mock_GetMemory(int size)
{
    return malloc((size_t)size);
}

static void Mock_FreeMemory(void *ptr)
{
    free(ptr);
}

static int Mock_DebugLineCreate(void)
{
    return 1;
}

static void Mock_DebugLineDelete(int line)
{
    (void)line;
}

static void Mock_DebugLineShow(int line, vec3_t start, vec3_t end, int color)
{
    (void)line;
    (void)start;
    (void)end;
    (void)color;
}

static void Mock_ClearCommandArgs(mock_bot_import_t *mock)
{
    if (mock == NULL)
    {
        return;
    }

    for (int index = 0; index < mock->command_argc; ++index)
    {
        free(mock->command_args[index]);
        mock->command_args[index] = NULL;
    }

    mock->command_argc = 0;
}

static char *Mock_DuplicateString(const char *text)
{
    if (text == NULL)
    {
        return NULL;
    }

    size_t length = strlen(text);
    char *copy = (char *)malloc(length + 1);
    if (copy == NULL)
    {
        return NULL;
    }

    memcpy(copy, text, length);
    copy[length] = '\0';
    return copy;
}

static void Mock_SetCommandArgs(mock_bot_import_t *mock, const char *command, const char *args_line)
{
    if (mock == NULL)
    {
        return;
    }

    Mock_ClearCommandArgs(mock);

    if (command != NULL)
    {
        mock->command_args[mock->command_argc++] = Mock_DuplicateString(command);
    }

    if (args_line == NULL)
    {
        return;
    }

    const char *cursor = args_line;
    while (*cursor != '\0' && mock->command_argc < (int)ARRAY_LEN(mock->command_args))
    {
        while (*cursor != '\0' && isspace((unsigned char)*cursor))
        {
            ++cursor;
        }

        if (*cursor == '\0')
        {
            break;
        }

        const char *start = cursor;
        while (*cursor != '\0' && !isspace((unsigned char)*cursor))
        {
            ++cursor;
        }

        size_t length = (size_t)(cursor - start);
        char *token = (char *)malloc(length + 1);
        if (token == NULL)
        {
            break;
        }

        memcpy(token, start, length);
        token[length] = '\0';
        mock->command_args[mock->command_argc++] = token;
    }
}

static void Mock_AddCommand(const char *name, void (*function)(void))
{
    if (g_active_mock == NULL || name == NULL || function == NULL)
    {
        return;
    }

    if (g_active_mock->command_count >= ARRAY_LEN(g_active_mock->commands))
    {
        return;
    }

    size_t index = g_active_mock->command_count++;
    strncpy(g_active_mock->commands[index].name, name, sizeof(g_active_mock->commands[index].name) - 1);
    g_active_mock->commands[index].name[sizeof(g_active_mock->commands[index].name) - 1] = '\0';
    g_active_mock->commands[index].function = function;
}

static void Mock_RemoveCommand(const char *name)
{
    if (g_active_mock == NULL || name == NULL)
    {
        return;
    }

    for (size_t index = 0; index < g_active_mock->command_count; ++index)
    {
        if (strcmp(g_active_mock->commands[index].name, name) == 0)
        {
            for (size_t move = index; move + 1 < g_active_mock->command_count; ++move)
            {
                g_active_mock->commands[move] = g_active_mock->commands[move + 1];
            }
            g_active_mock->command_count -= 1U;
            break;
        }
    }
}

static int Mock_CmdArgc(void)
{
    if (g_active_mock == NULL)
    {
        return 0;
    }

    return g_active_mock->command_argc;
}

static const char *Mock_CmdArgv(int index)
{
    if (g_active_mock == NULL || index < 0 || index >= g_active_mock->command_argc)
    {
        return NULL;
    }

    return g_active_mock->command_args[index];
}

static void Mock_InvokeCommand(mock_bot_import_t *mock, const char *name, const char *args)
{
    if (mock == NULL || name == NULL)
    {
        return;
    }

    for (size_t index = 0; index < mock->command_count; ++index)
    {
        if (strcmp(mock->commands[index].name, name) == 0)
        {
            Mock_SetCommandArgs(mock, name, args);
            if (mock->commands[index].function != NULL)
            {
                mock->commands[index].function();
            }
            Mock_ClearCommandArgs(mock);
            return;
        }
    }
}

static void Mock_Reset(mock_bot_import_t *mock)
{
    if (mock == NULL)
    {
        return;
    }

    memset(mock->prints, 0, sizeof(mock->prints));
    memset(mock->inputs, 0, sizeof(mock->inputs));
    memset(mock->input_clients, 0, sizeof(mock->input_clients));
    mock->print_count = 0;
    mock->bot_input_count = 0;
    mock->command_count = 0;
    Mock_ClearCommandArgs(mock);
}

static void Mock_ClearPrints(mock_bot_import_t *mock)
{
    if (mock == NULL)
    {
        return;
    }

    memset(mock->prints, 0, sizeof(mock->prints));
    mock->print_count = 0;
}

static void Mock_CopyPrints(const mock_bot_import_t *mock,
                            captured_print_t *dest,
                            size_t dest_capacity,
                            size_t *out_count)
{
    if (out_count == NULL)
    {
        return;
    }

    *out_count = 0;

    if (mock == NULL || dest == NULL || dest_capacity == 0)
    {
        return;
    }

    size_t count = mock->print_count;
    if (count > dest_capacity)
    {
        count = dest_capacity;
    }

    for (size_t index = 0; index < count; ++index)
    {
        dest[index] = mock->prints[index];
    }

    *out_count = count;
}

static void Mock_AssertPrintsMatch(const captured_print_t *expected,
                                   size_t expected_count,
                                   const mock_bot_import_t *actual)
{
    assert_non_null(expected);
    assert_non_null(actual);

    assert_int_equal(actual->print_count, expected_count);
    for (size_t index = 0; index < expected_count; ++index)
    {
        assert_int_equal(actual->prints[index].type, expected[index].type);
        assert_string_equal(actual->prints[index].message, expected[index].message);
    }
}

static const char *Mock_FindPrint(const mock_bot_import_t *mock, const char *needle)
{
    if (mock == NULL || needle == NULL)
    {
        return NULL;
    }

    for (size_t index = 0; index < mock->print_count; ++index)
    {
        if (strstr(mock->prints[index].message, needle) != NULL)
        {
            return mock->prints[index].message;
        }
    }

    return NULL;
}

static const captured_print_t *Mock_FindPrintEntry(const mock_bot_import_t *mock,
                                                   const char *needle)
{
    if (mock == NULL || needle == NULL)
    {
        return NULL;
    }

    for (size_t index = 0; index < mock->print_count; ++index)
    {
        if (strstr(mock->prints[index].message, needle) != NULL)
        {
            return &mock->prints[index];
        }
    }

    return NULL;
}

static void Mock_AssertPrintContains(const mock_bot_import_t *mock,
                                     const char *needle,
                                     int expected_type)
{
    const captured_print_t *entry = Mock_FindPrintEntry(mock, needle);
    assert_non_null(entry);
    if (expected_type >= 0)
    {
    assert_int_equal(entry->type, expected_type);
    }
}

static void test_console_commands_register(void **state)
{
    bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;

    Mock_Reset(&context->mock);

    int status = context->api->BotSetupLibrary();
    assert_int_equal(status, BLERR_NOERROR);

    assert_int_equal(context->mock.command_count, 3);
    assert_string_equal(context->mock.commands[0].name, "bot_test");
    assert_non_null(context->mock.commands[0].function);
    assert_string_equal(context->mock.commands[1].name, "aas_showpath");
    assert_non_null(context->mock.commands[1].function);
    assert_string_equal(context->mock.commands[2].name, "aas_showareas");
    assert_non_null(context->mock.commands[2].function);

    context->api->BotShutdownLibrary();

    assert_int_equal(context->mock.command_count, 0);
}

static void test_console_commands_invoke(void **state)
{
    bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;

    if (!ensure_map_fixture(&context->assets, "test1"))
    {
        cmocka_skip();
    }

    Mock_Reset(&context->mock);

    int status = context->api->BotSetupLibrary();
    assert_int_equal(status, BLERR_NOERROR);
    assert_int_equal(context->mock.command_count, 3);

    char *sounds[] = {"player/step1.wav"};
    status = context->api->BotLoadMap("maps/test1.bsp", 0, NULL, 1, sounds, 0, NULL);
    assert_int_equal(status, BLERR_NOERROR);
    assert_true(aasworld.loaded);
    assert_true(aasworld.numAreas > 0);

    vec3_t zero = {0.0f, 0.0f, 0.0f};

    captured_print_t expected_bot_test[ARRAY_LEN(context->mock.prints)];
    size_t expected_bot_test_count = 0U;

    Mock_ClearPrints(&context->mock);
    AAS_DebugBotTest(0, "1", zero, zero);
    Mock_CopyPrints(&context->mock,
                    expected_bot_test,
                    ARRAY_LEN(expected_bot_test),
                    &expected_bot_test_count);

    Mock_ClearPrints(&context->mock);
    Mock_InvokeCommand(&context->mock, "bot_test", "1");
    Mock_AssertPrintsMatch(expected_bot_test, expected_bot_test_count, &context->mock);

    captured_print_t expected_showpath[ARRAY_LEN(context->mock.prints)];
    size_t expected_showpath_count = 0U;

    Mock_ClearPrints(&context->mock);
    AAS_DebugShowPath(1, 2, zero, zero);
    Mock_CopyPrints(&context->mock,
                    expected_showpath,
                    ARRAY_LEN(expected_showpath),
                    &expected_showpath_count);

    Mock_ClearPrints(&context->mock);
    Mock_InvokeCommand(&context->mock, "aas_showpath", "1 2");
    Mock_AssertPrintsMatch(expected_showpath, expected_showpath_count, &context->mock);

    int invalid_area = aasworld.numAreas + 5;
    int area_list[] = {1, invalid_area};
    captured_print_t expected_showareas[ARRAY_LEN(context->mock.prints)];
    size_t expected_showareas_count = 0U;

    Mock_ClearPrints(&context->mock);
    AAS_DebugShowAreas(area_list, ARRAY_LEN(area_list));
    Mock_CopyPrints(&context->mock,
                    expected_showareas,
                    ARRAY_LEN(expected_showareas),
                    &expected_showareas_count);

    char area_args[64];
    snprintf(area_args, sizeof(area_args), "%d %d", area_list[0], area_list[1]);

    Mock_ClearPrints(&context->mock);
    Mock_InvokeCommand(&context->mock, "aas_showareas", area_args);
    Mock_AssertPrintsMatch(expected_showareas, expected_showareas_count, &context->mock);

    context->api->BotShutdownLibrary();
}

static int setup_bot_interface(void **state)
{
    bot_interface_test_context_t *context =
        (bot_interface_test_context_t *)calloc(1, sizeof(*context));
    assert_non_null(context);

    char contract_path[1024];
    snprintf(contract_path,
             sizeof(contract_path),
             "%s/tests/reference/botlib_contract.json",
             PROJECT_SOURCE_DIR);
    int load_status = BotlibContract_Load(contract_path, &context->catalogue);
    assert_int_equal(load_status, 0);

    context->mock.table.BotInput = Mock_BotInput;
    context->mock.table.BotClientCommand = Mock_BotClientCommand;
    context->mock.table.Print = Mock_Print;
    context->mock.table.Trace = Mock_Trace;
    context->mock.table.PointContents = Mock_PointContents;
    context->mock.table.GetMemory = Mock_GetMemory;
    context->mock.table.FreeMemory = Mock_FreeMemory;
    context->mock.table.DebugLineCreate = Mock_DebugLineCreate;
    context->mock.table.DebugLineDelete = Mock_DebugLineDelete;
    context->mock.table.DebugLineShow = Mock_DebugLineShow;
    context->mock.table.AddCommand = Mock_AddCommand;
    context->mock.table.RemoveCommand = Mock_RemoveCommand;
    context->mock.table.CmdArgc = Mock_CmdArgc;
    context->mock.table.CmdArgv = Mock_CmdArgv;

    LibVar_Init();
    context->libvar_initialised = true;

    if (!asset_env_initialise(&context->assets))
    {
        asset_env_cleanup(&context->assets);
        free(context);
        cmocka_skip();
    }

    char weapon_config_path[PATH_MAX];
    int written = snprintf(weapon_config_path,
                           sizeof(weapon_config_path),
                           "%s/weapons.c",
                           context->assets.asset_root);
    if (written <= 0 || (size_t)written >= sizeof(weapon_config_path))
    {
        asset_env_cleanup(&context->assets);
        free(context);
        cmocka_skip();
    }

    LibVarSet("basedir", context->assets.asset_root);
    LibVarSet("gamedir", "");
    LibVarSet("cddir", "");
    LibVarSet("gladiator_asset_dir", "");
    LibVarSet("weaponconfig", "weapons.c");
    LibVarSet("max_weaponinfo", "64");
    LibVarSet("max_projectileinfo", "64");
    LibVarSet("itemconfig", "items.c");

    g_active_mock = &context->mock;
    context->api = GetBotAPI(&context->mock.table);
    assert_non_null(context->api);

    *state = context;
    return 0;
}

static int teardown_bot_interface(void **state)
{
    bot_interface_test_context_t *context =
        (bot_interface_test_context_t *)(state != NULL ? *state : NULL);

    if (context != NULL && context->api != NULL)
    {
        context->api->BotShutdownLibrary();
    }

    if (context != NULL)
    {
        asset_env_cleanup(&context->assets);
    }

    if (context != NULL && context->libvar_initialised)
    {
        LibVar_Shutdown();
        context->libvar_initialised = false;
    }

    BotState_ShutdownAll();
    g_active_mock = NULL;
    BotInterface_SetImportTable(NULL);
    if (context != NULL)
    {
        BotlibContract_Free(&context->catalogue);
    }
    free(context);
    return 0;
}

static void test_bot_load_map_requires_library(void **state)
{
    bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;

    Mock_Reset(&context->mock);

    int status = context->api->BotLoadMap("maps/test.bsp", 0, NULL, 0, NULL, 0, NULL);
    assert_true(context->mock.print_count > 0);

    const botlib_contract_export_t *guard =
        BotlibContract_FindExport(&context->catalogue, "GuardLibrarySetup");
    assert_non_null(guard);
    const botlib_contract_scenario_t *failure = BotlibContract_FindScenario(guard, "failure");
    assert_non_null(failure);
    const botlib_contract_message_t *message =
        BotlibContract_FindMessageWithSeverity(failure, context->mock.prints[0].type);
    assert_non_null(message);

    char expected_message[1024];
    snprintf(expected_message, sizeof(expected_message), message->text, "BotLoadMap");
    assert_string_equal(context->mock.prints[0].message, expected_message);

    const botlib_contract_return_code_t *expected_status =
        BotlibContract_FindReturnCode(failure, BLERR_LIBRARYNOTSETUP);
    assert_non_null(expected_status);
    assert_int_equal(status, expected_status->value);
}

static void test_bot_load_map_and_sensory_queues(void **state)
{
    bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;

    Mock_Reset(&context->mock);

    LibVarSet("max_soundinfo", "64");
    LibVarSet("max_aassounds", "4");

    assert_string_equal(context->api->BotVersion(), "BotLib v0.96");

    int status = context->api->BotSetupLibrary();
    assert_int_equal(status, BLERR_NOERROR);

    assert_non_null(Mock_FindPrint(&context->mock, "------- BotLib Initialization -------"));
    assert_non_null(Mock_FindPrint(&context->mock, "BotLib v0.96"));
    assert_null(Mock_FindPrint(&context->mock, "stub invoked"));

    assert_non_null(Mock_FindPrint(&context->mock, "AAS initialized."));
    assert_true(aasworld.initialized);
    assert_true(EA_IsInitialised());
    assert_true(L_Utils_IsInitialised());
    assert_true(L_Struct_IsInitialised());
    assert_non_null(Bridge_MaxClients());

    size_t info_count = AAS_SoundSubsystem_InfoCount();
    assert_true(info_count > 0);

    int step_index = AAS_SoundSubsystem_FindInfoIndex("player/step1.wav");
    assert_true(step_index >= 0);
    const aas_soundinfo_t *step_info = AAS_SoundSubsystem_Info((size_t)step_index);
    assert_non_null(step_info);
    assert_string_equal(step_info->name, "player/step1.wav");
    assert_float_equal(step_info->volume, 80.0f, 0.01f);
    assert_int_equal(step_info->type, 1);

    char *sounds[] = {"player/step1.wav", "weapons/blastf1a.wav"};
    status = context->api->BotLoadMap("maps/test1.bsp", 0, NULL, 2, sounds, 0, NULL);
    assert_int_equal(status, BLERR_NOERROR);

    vec3_t origin = {0.0f, 32.0f, 16.0f};
    status = context->api->BotAddSound(origin, 3, 1, 1, 0.5f, 0.7f, 0.0f);
    assert_int_equal(status, BLERR_NOERROR);

    assert_int_equal((int)AAS_SoundSubsystem_SoundEventCount(), 1);
    const aas_sound_event_t *event = AAS_SoundSubsystem_SoundEvent(0);
    assert_non_null(event);
    assert_int_equal(event->ent, 3);
    assert_int_equal(event->soundindex, 1);
    assert_float_equal(event->volume, 0.5f, 0.0001f);
    const aas_soundinfo_t *event_info = AAS_SoundSubsystem_InfoForSoundIndex(event->soundindex);
    assert_non_null(event_info);
    assert_string_equal(event_info->name, "weapons/blastf1a.wav");
    assert_int_equal(AAS_SoundSubsystem_SoundTypeForIndex(event->soundindex), event_info->type);

    const aas_sound_event_summary_t *sound_summaries = NULL;
    size_t summary_count = AAS_SoundSubsystem_SoundSummaries(&sound_summaries);
    assert_int_equal(summary_count, 1);
    assert_non_null(sound_summaries);
    const aas_sound_event_summary_t *summary = &sound_summaries[0];
    assert_non_null(summary);
    assert_non_null(summary->event);
    assert_ptr_equal(summary->event, event);
    assert_true(summary->has_info);
    assert_ptr_equal(summary->info, event_info);
    assert_int_equal(summary->sound_type, event_info->type);
    assert_false(summary->expired);

    status = context->api->BotAddSound(origin, 4, 2, 5, 0.3f, 1.0f, 0.1f);
    assert_int_equal(status, BLERR_INVALIDSOUNDINDEX);
    assert_int_equal((int)AAS_SoundSubsystem_SoundEventCount(), 1);

    status = context->api->BotAddPointLight(origin, 5, 128.0f, 1.0f, 0.3f, 0.2f, 0.0f, 0.25f);
    assert_int_equal(status, BLERR_NOERROR);
    assert_int_equal((int)AAS_SoundSubsystem_PointLightCount(), 1);

    const aas_pointlight_event_summary_t *light_summaries = NULL;
    size_t light_summary_count = AAS_SoundSubsystem_PointLightSummaries(&light_summaries);
    assert_int_equal(light_summary_count, 1);
    assert_non_null(light_summaries);
    const aas_pointlight_event_summary_t *light_summary = &light_summaries[0];
    assert_non_null(light_summary);
    assert_non_null(light_summary->event);
    assert_ptr_equal(light_summary->event, AAS_SoundSubsystem_PointLight(0));
    assert_true(light_summary->has_expiry);
    assert_float_equal(light_summary->expiry_time, 0.25f, 0.0001f);
    assert_false(light_summary->expired);

    context->api->Test(0, "sounds", origin, origin);
    assert_non_null(Mock_FindPrint(&context->mock, "Test sounds: 1 queued"));
    assert_non_null(Mock_FindPrint(&context->mock, "sound[0]: ent=3"));

    context->api->Test(0, "pointlights", origin, origin);
    assert_non_null(Mock_FindPrint(&context->mock, "Test pointlights: 1 queued"));
    assert_non_null(Mock_FindPrint(&context->mock, "light[0]: ent=5"));

    bot_settings_t settings;
    memset(&settings, 0, sizeof(settings));
    snprintf(settings.characterfile, sizeof(settings.characterfile), "bots/babe_c.c");
    snprintf(settings.charactername, sizeof(settings.charactername), "Babe");

    status = context->api->BotSetupClient(1, &settings);
    assert_int_equal(status, BLERR_NOERROR);

    bot_client_state_t *self_state = BotState_Get(1);
    assert_non_null(self_state);
    self_state->team = 1;

    bot_goal_t chase_goal;
    memset(&chase_goal, 0, sizeof(chase_goal));
    chase_goal.number = 0;
    chase_goal.areanum = 1;
    VectorSet(chase_goal.origin, 256.0f, 0.0f, 24.0f);
    status = context->api->BotPushGoal(self_state->goal_handle, &chase_goal);
    assert_int_equal(status, BLERR_NOERROR);

    bot_client_state_t *enemy_state = BotState_Create(2);
    assert_non_null(enemy_state);
    enemy_state->active = true;
    enemy_state->team = 2;

    bot_updateclient_t self_update;
    memset(&self_update, 0, sizeof(self_update));
    VectorSet(self_update.origin, 0.0f, 0.0f, 24.0f);
    VectorSet(self_update.viewangles, 0.0f, 0.0f, 0.0f);
    self_update.stats[STAT_HEALTH] = 100;
    self_update.pm_flags = PMF_ON_GROUND;
    for (int i = 0; i < MAX_ITEMS; ++i)
    {
        self_update.inventory[i] = 1;
    }

    bot_updateclient_t enemy_update;
    memset(&enemy_update, 0, sizeof(enemy_update));
    VectorSet(enemy_update.origin, 256.0f, 0.0f, 24.0f);
    VectorSet(enemy_update.viewangles, 0.0f, 180.0f, 0.0f);
    enemy_update.stats[STAT_HEALTH] = 100;
    enemy_state->last_client_update = enemy_update;
    enemy_state->client_update_valid = true;

    bot_updateentity_t enemy_entity;
    memset(&enemy_entity, 0, sizeof(enemy_entity));
    VectorCopy(enemy_update.origin, enemy_entity.origin);
    VectorCopy(enemy_update.origin, enemy_entity.old_origin);

    status = context->api->BotStartFrame(0.2f);
    assert_int_equal(status, BLERR_NOERROR);

    status = context->api->BotUpdateClient(1, &self_update);
    assert_int_equal(status, BLERR_NOERROR);

    status = context->api->BotUpdateEntity(2, &enemy_entity);
    assert_int_equal(status, BLERR_NOERROR);

    status = context->api->BotAI(1, 0.05f);
    assert_int_equal(status, BLERR_NOERROR);

    assert_true(context->mock.bot_input_count > 0);
    const bot_input_t *final_input = &context->mock.inputs[context->mock.bot_input_count - 1];
    assert_float_equal(final_input->thinktime, 0.05f, 0.0001f);
    assert_true(final_input->dir[0] > 0.99f);
    assert_float_equal(final_input->dir[1], 0.0f, 0.0001f);
    assert_float_equal(final_input->dir[2], 0.0f, 0.0001f);
    assert_true(final_input->speed >= 256.0f);
    assert_true(final_input->speed <= 400.0f);
    assert_int_equal(final_input->actionflags, 0);

    assert_int_equal(self_state->combat.current_enemy, 2);
    assert_true(self_state->combat.enemy_visible);
    assert_int_equal(self_state->active_goal_number, chase_goal.number);

    context->api->BotShutdownClient(1);
    BotState_Destroy(2);

    Mock_Reset(&context->mock);

    context->api->BotShutdownLibrary();

    LibVarSet("max_soundinfo", "1");
    LibVarSet("max_aassounds", "2");

    status = context->api->BotSetupLibrary();
    assert_int_equal(status, BLERR_NOERROR);
    assert_non_null(Mock_FindPrint(&context->mock, "AAS_Sound: discarding soundinfo"));

    Mock_Reset(&context->mock);

    status = context->api->BotLoadMap("maps/test1.bsp", 0, NULL, 2, sounds, 0, NULL);
    assert_int_equal(status, BLERR_NOERROR);
    assert_int_equal((int)AAS_SoundSubsystem_InfoCount(), 1);

    context->api->BotShutdownLibrary();

    Mock_Reset(&context->mock);

    LibVarSet("max_soundinfo", "0");
    LibVarSet("max_aassounds", "0");

    status = context->api->BotSetupLibrary();
    assert_int_equal(status, BLERR_NOERROR);
    assert_non_null(Mock_FindPrint(&context->mock, "AAS_Sound: max_soundinfo disabled"));

    Mock_Reset(&context->mock);

    status = context->api->BotLoadMap("maps/test1.bsp", 0, NULL, 2, sounds, 0, NULL);
    assert_int_equal(status, BLERR_NOERROR);

    status = context->api->BotAddSound(origin, 6, 2, 0, 1.0f, 1.0f, 0.0f);
    assert_int_equal(status, BLERR_NOERROR);
    assert_non_null(Mock_FindPrint(&context->mock, "BotAddSound: sound queue capacity exceeded"));

    status = context->api->BotAddPointLight(origin, 7, 64.0f, 0.1f, 0.2f, 0.3f, 0.0f, 0.5f);
    assert_int_equal(status, BLERR_NOERROR);
    assert_non_null(Mock_FindPrint(&context->mock, "BotAddPointLight: point light queue capacity exceeded"));

    context->api->BotShutdownLibrary();

    assert_non_null(Mock_FindPrint(&context->mock, "------- BotLib Shutdown -------"));
    assert_null(Mock_FindPrint(&context->mock, "stub invoked"));

    assert_false(aasworld.initialized);
    assert_false(EA_IsInitialised());
    assert_false(L_Utils_IsInitialised());
    assert_false(L_Struct_IsInitialised());
    assert_null(Bridge_MaxClients());
}

static void test_bot_console_message_and_ai_pipeline(void **state)
{
    bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;

    Mock_Reset(&context->mock);

    int status = context->api->BotSetupLibrary();
    assert_int_equal(status, BLERR_NOERROR);

    bot_client_state_t *client_state = BotState_Create(1);
    assert_non_null(client_state);
    client_state->active = true;
    client_state->chat_state = BotAllocChatState();
    assert_non_null(client_state->chat_state);

    status = context->api->BotConsoleMessage(1, CMS_CHAT, "hello gladiator");
    assert_int_equal(status, BLERR_NOERROR);

    context->api->Test(1, "dump_chat", NULL, NULL);
    assert_non_null(Mock_FindPrint(&context->mock, "hello gladiator"));

    context->api->BotStartFrame(0.1f);

    bot_updateclient_t update;
    memset(&update, 0, sizeof(update));
    update.viewangles[1] = 45.0f;

    status = context->api->BotUpdateClient(1, &update);
    assert_int_equal(status, BLERR_NOERROR);

    status = context->api->BotAI(1, 0.05f);
    assert_int_equal(status, BLERR_NOERROR);
    assert_int_equal(context->mock.bot_input_count, 1);
    assert_int_equal(context->mock.input_clients[0], 1);
    assert_float_equal(context->mock.inputs[0].thinktime, 0.05f, 0.0001f);
    assert_float_equal(context->mock.inputs[0].viewangles[1], 45.0f, 0.0001f);

    status = context->api->BotAI(1, 0.05f);
    assert_int_equal(status, BLERR_AIUPDATEINACTIVECLIENT);

    context->api->BotShutdownLibrary();
}

static void test_bot_lib_var_set_propagates_import_status(void **state)
{
    bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;

    Mock_Reset(&context->mock);

    const botlib_import_table_t *original_imports = BotInterface_GetImportTable();
    assert_non_null(original_imports);

    botlib_import_table_t override_imports = *original_imports;
    override_imports.BotLibVarSet = Mock_ImportBotLibVarSet;

    BotInterface_SetImportTable(&override_imports);

    g_mock_import_libvar_set_status = BLERR_NOERROR;
    int status = context->api->BotLibVarSet("test_override", "42");
    assert_int_equal(status, BLERR_NOERROR);

    g_mock_import_libvar_set_status = BLERR_LIBRARYNOTSETUP;
    status = context->api->BotLibVarSet("test_override", "84");
    assert_int_equal(status, BLERR_LIBRARYNOTSETUP);

    g_mock_import_libvar_set_status = BLERR_INVALIDIMPORT;
    status = context->api->BotLibVarSet("test_override", "168");
    assert_int_equal(status, BLERR_INVALIDIMPORT);

    BotInterface_SetImportTable(original_imports);
}

static void test_bot_lib_var_cache_tracks_updates(void **state)
{
    bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;

    Mock_Reset(&context->mock);

    int status = context->api->BotSetupLibrary();
    assert_int_equal(status, BLERR_NOERROR);

    const botlib_import_table_t *imports = BotInterface_GetImportTable();
    assert_non_null(imports);
    assert_non_null(imports->BotLibVarGet);

    char buffer[128];

    memset(buffer, 0, sizeof(buffer));
    status = imports->BotLibVarGet("cache_probe", buffer, sizeof(buffer));
    assert_int_equal(status, BLERR_INVALIDIMPORT);

    status = context->api->BotLibVarSet("cache_probe", "alpha");
    assert_int_equal(status, BLERR_NOERROR);

    memset(buffer, 0, sizeof(buffer));
    status = imports->BotLibVarGet("cache_probe", buffer, sizeof(buffer));
    assert_int_equal(status, BLERR_NOERROR);
    assert_string_equal(buffer, "alpha");

    status = context->api->BotLibVarSet("cache_probe", "beta");
    assert_int_equal(status, BLERR_NOERROR);

    memset(buffer, 0, sizeof(buffer));
    status = imports->BotLibVarGet("cache_probe", buffer, sizeof(buffer));
    assert_int_equal(status, BLERR_NOERROR);
    assert_string_equal(buffer, "beta");

    status = context->api->BotLibVarSet("cache_secondary", "gamma");
    assert_int_equal(status, BLERR_NOERROR);

    memset(buffer, 0, sizeof(buffer));
    status = imports->BotLibVarGet("cache_secondary", buffer, sizeof(buffer));
    assert_int_equal(status, BLERR_NOERROR);
    assert_string_equal(buffer, "gamma");
}

static void test_bot_update_entity_populates_aas(void **state)
{
    bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;

    Mock_Reset(&context->mock);

    int status = context->api->BotSetupLibrary();
    assert_int_equal(status, BLERR_NOERROR);

    status = context->api->BotLoadMap("maps/test2.bsp", 0, NULL, 0, NULL, 0, NULL);
    assert_int_equal(status, BLERR_NOERROR);

    status = context->api->BotStartFrame(0.0f);
    assert_int_equal(status, BLERR_NOERROR);

    bot_updateentity_t entity;
    memset(&entity, 0, sizeof(entity));
    entity.origin[0] = 16.0f;
    entity.origin[1] = 24.0f;
    entity.origin[2] = 48.0f;
    entity.old_origin[0] = 16.0f;
    entity.old_origin[1] = 24.0f;
    entity.old_origin[2] = 48.0f;
    entity.solid = 31;
    entity.modelindex = 2;

    status = context->api->BotUpdateEntity(5, &entity);
    assert_int_equal(status, BLERR_NOERROR);
    assert_true(aasworld.entitiesValid);
    assert_non_null(aasworld.entities);
    assert_int_equal(aasworld.entities[5].number, 5);

    AASEntityFrame translated;
    assert_true(Bridge_ReadEntityFrame(5, &translated));
    assert_float_equal(translated.last_update_time, 0.0f, 0.0001f);
    assert_float_equal(translated.frame_delta, 0.0f, 0.0001f);
    assert_false(translated.is_mover);

    status = context->api->BotStartFrame(0.25f);
    assert_int_equal(status, BLERR_NOERROR);

    bot_updateentity_t follow_up = entity;
    follow_up.origin[0] = 24.0f;
    follow_up.origin[1] = 32.0f;
    follow_up.origin[2] = 52.0f;
    follow_up.old_origin[0] = entity.origin[0];
    follow_up.old_origin[1] = entity.origin[1];
    follow_up.old_origin[2] = entity.origin[2];

    status = context->api->BotUpdateEntity(5, &follow_up);
    assert_int_equal(status, BLERR_NOERROR);

    assert_true(Bridge_ReadEntityFrame(5, &translated));
    assert_float_equal(translated.last_update_time, 0.25f, 0.0001f);
    assert_float_equal(translated.frame_delta, 0.25f, 0.0001f);
    assert_true(translated.frame_delta > 0.0f);
    assert_false(translated.is_mover);

    context->api->BotShutdownLibrary();
}

typedef struct bot_mover_fixture_s
{
    aas_area_t *areas;
    aas_areasettings_t *area_settings;
    aas_reachability_t *reachability;
    aas_entity_t *entities;
} bot_mover_fixture_t;

static void bot_mover_fixture_init(bot_mover_fixture_t *fixture)
{
    memset(&aasworld, 0, sizeof(aasworld));
    aasworld.loaded = qtrue;
    aasworld.initialized = qtrue;
    aasworld.time = 0.0f;
    aasworld.numAreas = 3;
    aasworld.numAreaSettings = 4;
    aasworld.numReachability = 3;
    aasworld.maxEntities = 8;

    fixture->areas = (aas_area_t *)calloc((size_t)(aasworld.numAreas + 1), sizeof(aas_area_t));
    fixture->area_settings =
        (aas_areasettings_t *)calloc((size_t)aasworld.numAreaSettings, sizeof(aas_areasettings_t));
    fixture->reachability =
        (aas_reachability_t *)calloc((size_t)aasworld.numReachability, sizeof(aas_reachability_t));
    fixture->entities = (aas_entity_t *)calloc((size_t)aasworld.maxEntities, sizeof(aas_entity_t));

    assert_non_null(fixture->areas);
    assert_non_null(fixture->area_settings);
    assert_non_null(fixture->reachability);
    assert_non_null(fixture->entities);

    aasworld.areas = fixture->areas;
    aasworld.areasettings = fixture->area_settings;
    aasworld.reachability = fixture->reachability;
    aasworld.entities = fixture->entities;
    TranslateEntity_SetWorldLoaded(qtrue);

    VectorSet(aasworld.areas[1].mins, -64.0f, -64.0f, 0.0f);
    VectorSet(aasworld.areas[1].maxs, 64.0f, 64.0f, 64.0f);
    VectorSet(aasworld.areas[2].mins, -64.0f, -64.0f, 0.0f);
    VectorSet(aasworld.areas[2].maxs, 192.0f, 64.0f, 64.0f);
    VectorSet(aasworld.areas[3].mins, 192.0f, -64.0f, 0.0f);
    VectorSet(aasworld.areas[3].maxs, 320.0f, 64.0f, 64.0f);

    fixture->area_settings[1].firstreachablearea = 1;
    fixture->area_settings[1].numreachableareas = 1;
    fixture->area_settings[2].firstreachablearea = 2;
    fixture->area_settings[2].numreachableareas = 1;

    fixture->reachability[1].areanum = 2;
    fixture->reachability[1].traveltype = TRAVEL_WALK;
    VectorClear(fixture->reachability[1].start);
    VectorSet(fixture->reachability[1].end, 128.0f, 0.0f, 32.0f);
    fixture->reachability[1].facenum = 0; /* ensure no mover reach is registered */

    fixture->reachability[2].areanum = 3;
    fixture->reachability[2].traveltype = TRAVEL_WALK;
    VectorSet(fixture->reachability[2].start, 128.0f, 0.0f, 32.0f);
    VectorSet(fixture->reachability[2].end, 256.0f, 0.0f, 32.0f);

    memset(aasworld.travelflagfortype, 0, sizeof(aasworld.travelflagfortype));
    aasworld.travelflagfortype[TRAVEL_WALK] = TFL_WALK;
}

static void bot_mover_fixture_shutdown(bot_mover_fixture_t *fixture)
{
    if (fixture == NULL)
    {
        return;
    }

    free(fixture->areas);
    free(fixture->area_settings);
    free(fixture->reachability);
    free(fixture->entities);
    memset(fixture, 0, sizeof(*fixture));

    AAS_Shutdown();
    BotMove_MoverCatalogueReset();
}

static void test_bot_interface_mover_parity(void **state)
{
    bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;

    Mock_Reset(&context->mock);

    int status = context->api->BotSetupLibrary();
    assert_int_equal(status, BLERR_NOERROR);

    bot_mover_fixture_t fixture;
    memset(&fixture, 0, sizeof(fixture));
    bot_mover_fixture_init(&fixture);

    LibVarSet("bot_developer", "1");

    BotMove_MoverCatalogueReset();
    bot_mover_catalogue_entry_t mover_entry = {
        .modelnum = 8,
        .lip = 0.0f,
        .height = 0.0f,
        .speed = 0.0f,
        .spawnflags = 0,
        .doortype = 0,
        .kind = BOT_MOVER_KIND_FUNC_PLAT,
    };
    assert_true(BotMove_MoverCatalogueInsert(&mover_entry));

    char model_name[] = "*8";
    char *model_entries[] = {model_name};
    botinterface_asset_list_t asset_models = {
        .entries = model_entries,
        .count = ARRAY_LEN(model_entries),
    };
    assert_true(BotMove_MoverCatalogueFinalize(&asset_models));

    bot_updateentity_t mover_update;
    memset(&mover_update, 0, sizeof(mover_update));
    VectorSet(mover_update.origin, 64.0f, 0.0f, 16.0f);
    VectorSet(mover_update.old_origin, 64.0f, 0.0f, 16.0f);
    VectorSet(mover_update.mins, -32.0f, -32.0f, -16.0f);
    VectorSet(mover_update.maxs, 32.0f, 32.0f, 16.0f);
    mover_update.solid = SOLID_BSP;
    mover_update.modelindex = mover_entry.modelnum;

    status = context->api->BotUpdateEntity(3, &mover_update);
    assert_int_equal(status, BLERR_NOERROR);

    Mock_AssertPrintContains(&context->mock, "relinking brush model", PRT_MESSAGE);
    Mock_Reset(&context->mock);

    bot_settings_t settings;
    memset(&settings, 0, sizeof(settings));
    snprintf(settings.characterfile, sizeof(settings.characterfile), "bots/babe_c.c");
    snprintf(settings.charactername, sizeof(settings.charactername), "Babe");
    settings.skill = 1;

    status = context->api->BotSetupClient(1, &settings);
    assert_int_equal(status, BLERR_NOERROR);

    bot_client_state_t *client_state = BotState_Get(1);
    assert_non_null(client_state);

    bot_goal_t mover_goal;
    memset(&mover_goal, 0, sizeof(mover_goal));
    mover_goal.number = 42;
    mover_goal.areanum = 3;
    VectorSet(mover_goal.origin, 256.0f, 0.0f, 32.0f);

    status = context->api->BotPushGoal(client_state->goal_handle, &mover_goal);
    assert_int_equal(status, BLERR_NOERROR);

    status = context->api->BotStartFrame(0.1f);
    assert_int_equal(status, BLERR_NOERROR);

    bot_updateclient_t update;
    memset(&update, 0, sizeof(update));
    VectorSet(update.origin, 64.0f, 0.0f, 32.0f);
    update.pm_flags = PMF_ON_GROUND;
    update.viewangles[1] = 90.0f;
    update.stats[STAT_HEALTH] = 100;
    for (int i = 0; i < MAX_ITEMS; ++i)
    {
        update.inventory[i] = 1;
    }

    status = context->api->BotUpdateClient(1, &update);
    assert_int_equal(status, BLERR_NOERROR);

    status = context->api->BotAI(1, 0.05f);
    assert_int_equal(status, BLERR_INVALIDIMPORT);

    Mock_AssertPrintContains(&context->mock, "without reachability", PRT_MESSAGE);

    BotDumpAvoidGoals(client_state->goal_handle);
    Mock_AssertPrintContains(&context->mock, "BotDumpAvoidGoals", PRT_MESSAGE);

    ai_avoid_list_t *avoid = AI_GoalState_GetAvoidList(client_state->goal_state);
    assert_non_null(avoid);
    assert_true(AI_AvoidList_Contains(avoid, mover_goal.number, 0.1f));

    Mock_Reset(&context->mock);

    fixture.reachability[1].traveltype = TRAVEL_ELEVATOR;
    fixture.reachability[1].facenum = mover_entry.modelnum;

    status = context->api->BotStartFrame(0.2f);
    assert_int_equal(status, BLERR_NOERROR);

    status = context->api->BotUpdateClient(1, &update);
    assert_int_equal(status, BLERR_NOERROR);

    status = context->api->BotAI(1, 0.05f);
    assert_int_equal(status, BLERR_NOERROR);

    assert_true(context->mock.bot_input_count > 0U);
    const bot_input_t *final_input =
        &context->mock.inputs[context->mock.bot_input_count - 1U];
    assert_float_equal(final_input->thinktime, 0.05f, 0.0001f);

    vec3_t goal_delta;
    VectorSubtract(mover_goal.origin, update.origin, goal_delta);
    float expected_speed = sqrtf(goal_delta[0] * goal_delta[0] +
                                 goal_delta[1] * goal_delta[1] +
                                 goal_delta[2] * goal_delta[2]);
    assert_true(expected_speed > 0.0f);

    vec3_t expected_dir = {goal_delta[0] / expected_speed,
                           goal_delta[1] / expected_speed,
                           goal_delta[2] / expected_speed};

    assert_float_equal(final_input->speed, expected_speed, 0.0001f);
    assert_float_equal(final_input->dir[0], expected_dir[0], 0.0001f);
    assert_float_equal(final_input->dir[1], expected_dir[1], 0.0001f);
    assert_float_equal(final_input->dir[2], expected_dir[2], 0.0001f);
    assert_int_equal(final_input->actionflags, 0);

    context->api->BotShutdownClient(1);
    context->api->BotShutdownLibrary();

    bot_mover_fixture_shutdown(&fixture);
    LibVarSet("bot_developer", "0");
}

static bool ensure_map_fixture(const asset_env_t *assets, const char *stem)
{
    if (assets == NULL || stem == NULL)
    {
        return false;
    }

    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/maps/%s.bsp", assets->asset_root, stem);
    FILE *file = fopen(path, "rb");
    if (file == NULL)
    {
        print_message("bot interface parity skipped: missing %s\n", path);
        return false;
    }
    fclose(file);

    snprintf(path, sizeof(path), "%s/maps/%s.aas", assets->asset_root, stem);
    file = fopen(path, "rb");
    if (file == NULL)
    {
        print_message("bot interface parity skipped: missing %s\n", path);
        return false;
    }
    fclose(file);

    return true;
}

static bool ensure_mover_fixture(const asset_env_t *assets)
{
    return ensure_map_fixture(assets, "test_mover");
}

static bool find_mover_area_center(vec3_t center_out, int *area_out)
{
    if (!aasworld.loaded || aasworld.areasettings == NULL || aasworld.areas == NULL)
    {
        return false;
    }

    for (int area = 1; area <= aasworld.numAreas; ++area)
    {
        if ((aasworld.areasettings[area].contents & AREACONTENTS_MOVER) == 0)
        {
            continue;
        }

        if (center_out != NULL)
        {
            VectorCopy(aasworld.areas[area].center, center_out);
        }

        if (area_out != NULL)
        {
            *area_out = area;
        }

        return true;
    }

    return false;
}

static void assert_success_status(const botlib_contract_catalogue_t *catalogue,
                                  const char *export_name,
                                  int status)
{
    const botlib_contract_export_t *export_entry =
        BotlibContract_FindExport(catalogue, export_name);
    assert_non_null(export_entry);

    const botlib_contract_scenario_t *scenario =
        BotlibContract_FindScenario(export_entry, "success");
    if (scenario == NULL)
    {
        scenario = BotlibContract_FindScenario(export_entry, NULL);
    }
    assert_non_null(scenario);

    const botlib_contract_return_code_t *expected =
        BotlibContract_FindReturnCode(scenario, status);

    if (expected != NULL)
    {
        assert_int_equal(status, expected->value);
    }
    else
    {
        assert_int_equal(status, BLERR_NOERROR);
    }
}

static void test_bot_end_to_end_pipeline_with_assets(void **state)
{
    bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;

    if (!ensure_map_fixture(&context->assets, "2box4"))
    {
        cmocka_skip();
    }

    Mock_Reset(&context->mock);

    int status = context->api->BotSetupLibrary();
    assert_success_status(&context->catalogue, "BotLibSetup", status);
    assert_non_null(Mock_FindPrint(&context->mock, "------- BotLib Initialization -------"));

    status = context->api->BotLoadMap("maps/2box4.bsp", 0, NULL, 0, NULL, 0, NULL);
    assert_success_status(&context->catalogue, "BotLibLoadMap", status);
    assert_true(aasworld.loaded);

    bot_settings_t settings;
    memset(&settings, 0, sizeof(settings));
    snprintf(settings.characterfile, sizeof(settings.characterfile), "bots/babe_c.c");
    snprintf(settings.charactername, sizeof(settings.charactername), "Babe");
    settings.ailibrary[0] = '\0';

    status = context->api->BotSetupClient(1, &settings);
    assert_int_equal(status, BLERR_NOERROR);

    status = context->api->BotConsoleMessage(1, CMS_CHAT, "hello gladiator");
    assert_success_status(&context->catalogue, "BotLibConsoleMessage", status);

    context->api->Test(1, "dump_chat", NULL, NULL);
    assert_non_null(Mock_FindPrint(&context->mock, "hello gladiator"));

    status = context->api->BotStartFrame(0.1f);
    assert_success_status(&context->catalogue, "BotLibStartFrame", status);

    bot_updateclient_t update;
    memset(&update, 0, sizeof(update));
    update.viewangles[1] = 45.0f;

    status = context->api->BotUpdateClient(1, &update);
    assert_int_equal(status, BLERR_NOERROR);

    status = context->api->BotAI(1, 0.05f);
    assert_int_equal(status, BLERR_NOERROR);
    assert_true(context->mock.bot_input_count > 0);
    assert_int_equal(context->mock.input_clients[0], 1);
    assert_float_equal(context->mock.inputs[0].thinktime, 0.05f, 0.0001f);

    context->api->BotShutdownClient(1);
}

static void test_bot_bridge_tracks_mover_entity_updates(void **state)
{
    bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;

    if (!ensure_mover_fixture(&context->assets))
    {
        cmocka_skip();
    }

    Mock_Reset(&context->mock);

    int status = context->api->BotSetupLibrary();
    assert_int_equal(status, BLERR_NOERROR);

    status = context->api->BotLoadMap("maps/test_mover.bsp", 0, NULL, 0, NULL, 0, NULL);
    assert_int_equal(status, BLERR_NOERROR);
    assert_true(aasworld.loaded);

    vec3_t mover_center;
    int mover_areanum = 0;
    assert_true(find_mover_area_center(mover_center, &mover_areanum));
    assert_true(mover_areanum > 0);

    const bot_mover_catalogue_entry_t *mover_entry = NULL;
    int mover_modelnum = 0;
    for (int candidate = 1; candidate < 64; ++candidate)
    {
        mover_entry = BotMove_MoverCatalogueFindByModel(candidate);
        if (mover_entry != NULL)
        {
            mover_modelnum = candidate;
            break;
        }
    }
    assert_non_null(mover_entry);
    assert_true(BotMove_MoverCatalogueIsModelMover(mover_modelnum));

    bot_settings_t settings;
    memset(&settings, 0, sizeof(settings));
    snprintf(settings.characterfile, sizeof(settings.characterfile), "bots/babe_c.c");
    snprintf(settings.charactername, sizeof(settings.charactername), "Babe");
    status = context->api->BotSetupClient(1, &settings);
    assert_int_equal(status, BLERR_NOERROR);

    bot_updateclient_t client_update;
    memset(&client_update, 0, sizeof(client_update));
    client_update.pm_type = PM_NORMAL;
    VectorCopy(mover_center, client_update.origin);
    client_update.origin[2] = aasworld.areas[mover_areanum].maxs[2] + 24.0f;
    client_update.pm_flags = 0;
    client_update.stats[STAT_HEALTH] = 100;
    for (int item = 0; item < MAX_ITEMS; ++item)
    {
        client_update.inventory[item] = 1;
    }

    status = context->api->BotStartFrame(0.0f);
    assert_int_equal(status, BLERR_NOERROR);

    bot_updateentity_t mover_update;
    memset(&mover_update, 0, sizeof(mover_update));
    VectorCopy(mover_center, mover_update.origin);
    mover_update.origin[2] = aasworld.areas[mover_areanum].mins[2];
    VectorCopy(mover_update.origin, mover_update.old_origin);
    VectorSet(mover_update.mins, -16.0f, -16.0f, -8.0f);
    VectorSet(mover_update.maxs, 16.0f, 16.0f, 8.0f);
    mover_update.solid = 3;
    mover_update.modelindex = mover_modelnum;

    const int mover_entity_num = 32;
    status = context->api->BotUpdateEntity(mover_entity_num, &mover_update);
    assert_int_equal(status, BLERR_NOERROR);

    status = context->api->BotUpdateClient(1, &client_update);
    assert_int_equal(status, BLERR_NOERROR);

    status = context->api->BotStartFrame(0.2f);
    assert_int_equal(status, BLERR_NOERROR);

    bot_updateentity_t mover_raised = mover_update;
    mover_raised.old_origin[2] = mover_update.origin[2];
    mover_raised.origin[2] = aasworld.areas[mover_areanum].maxs[2];
    status = context->api->BotUpdateEntity(mover_entity_num, &mover_raised);
    assert_int_equal(status, BLERR_NOERROR);

    bot_updateclient_t client_on_mover = client_update;
    client_on_mover.origin[2] = mover_raised.origin[2];
    client_on_mover.pm_flags = PMF_ON_GROUND;
    status = context->api->BotUpdateClient(1, &client_on_mover);
    assert_int_equal(status, BLERR_NOERROR);

    status = context->api->BotAI(1, 0.05f);
    assert_int_equal(status, BLERR_NOERROR);
    assert_true(context->mock.bot_input_count > 0);

    AASEntityFrame mover_frame;
    memset(&mover_frame, 0, sizeof(mover_frame));
    assert_true(Bridge_ReadEntityFrame(mover_entity_num, &mover_frame));
    assert_true(mover_frame.is_mover);
    assert_true(mover_frame.origin_dirty);
    assert_float_equal(mover_frame.origin[2], mover_raised.origin[2], 0.0001f);
    assert_float_equal(mover_frame.previous_origin[2], mover_update.origin[2], 0.0001f);
    assert_float_equal(mover_frame.frame_delta, 0.2f, 0.0001f);

    AASClientFrame client_frame;
    memset(&client_frame, 0, sizeof(client_frame));
    assert_true(Bridge_ReadClientFrame(1, &client_frame));
    assert_float_equal(client_frame.origin[2], client_on_mover.origin[2], 0.0001f);
    assert_float_equal(client_frame.last_update_time, 0.2f, 0.0001f);
    assert_float_equal(client_frame.frame_delta, 0.2f, 0.0001f);

    assert_int_equal(AAS_ModelNumForEntity(mover_entity_num), mover_modelnum);

    context->api->BotShutdownClient(1);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_console_commands_register,
                                        setup_bot_interface,
                                        teardown_bot_interface),
        cmocka_unit_test_setup_teardown(test_console_commands_invoke,
                                        setup_bot_interface,
                                        teardown_bot_interface),
        cmocka_unit_test_setup_teardown(test_bot_load_map_requires_library,
                                        setup_bot_interface,
                                        teardown_bot_interface),
        cmocka_unit_test_setup_teardown(test_bot_load_map_and_sensory_queues,
                                        setup_bot_interface,
                                        teardown_bot_interface),
        cmocka_unit_test_setup_teardown(test_bot_console_message_and_ai_pipeline,
                                        setup_bot_interface,
                                        teardown_bot_interface),
        cmocka_unit_test_setup_teardown(test_bot_lib_var_set_propagates_import_status,
                                        setup_bot_interface,
                                        teardown_bot_interface),
        cmocka_unit_test_setup_teardown(test_bot_lib_var_cache_tracks_updates,
                                        setup_bot_interface,
                                        teardown_bot_interface),
        cmocka_unit_test_setup_teardown(test_bot_update_entity_populates_aas,
                                        setup_bot_interface,
                                        teardown_bot_interface),
        cmocka_unit_test_setup_teardown(test_bot_interface_mover_parity,
                                        setup_bot_interface,
                                        teardown_bot_interface),
        cmocka_unit_test_setup_teardown(test_bot_end_to_end_pipeline_with_assets,
                                        setup_bot_interface,
                                        teardown_bot_interface),
        cmocka_unit_test_setup_teardown(test_bot_bridge_tracks_mover_entity_updates,
                                        setup_bot_interface,
                                        teardown_bot_interface),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}

