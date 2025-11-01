#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cmocka.h>

#include "botlib/interface/bot_interface.h"
#include "botlib/interface/bot_state.h"
#include "botlib/ai/chat/ai_chat.h"
#include "botlib/ea/ea_local.h"
#include "botlib/common/l_log.h"
#include "botlib/common/l_memory.h"
#include "botlib/common/l_libvar.h"
#include "botlib/common/l_struct.h"
#include "botlib/common/l_utils.h"
#include "botlib/aas/aas_local.h"
#include "botlib_contract_loader.h"
#include "../support/asset_env.h"
#include "q2bridge/bridge_config.h"

#define ARRAY_LEN(x) (sizeof(x) / sizeof((x)[0]))

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

    LibVarSet("weaponconfig", weapon_config_path);
    LibVarSet("max_weaponinfo", "64");
    LibVarSet("max_projectileinfo", "64");
    LibVarSet("GLADIATOR_ASSET_DIR", context->assets.asset_root);

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

    int status = context->api->BotSetupLibrary();
    assert_int_equal(status, BLERR_NOERROR);

    assert_non_null(Mock_FindPrint(&context->mock, "AAS initialized."));
    assert_true(aasworld.initialized);
    assert_true(EA_IsInitialised());
    assert_true(L_Utils_IsInitialised());
    assert_true(L_Struct_IsInitialised());
    assert_non_null(Bridge_MaxClients());

    char *sounds[] = {"world/ambient.wav", "weapons/impact.wav"};
    status = context->api->BotLoadMap("maps/test1.bsp", 0, NULL, 2, sounds, 0, NULL);
    assert_int_equal(status, BLERR_NOERROR);

    vec3_t origin = {0.0f, 32.0f, 16.0f};
    status = context->api->BotAddSound(origin, 3, 1, 1, 0.5f, 0.7f, 0.0f);
    assert_int_equal(status, BLERR_NOERROR);

    status = context->api->BotAddSound(origin, 4, 2, 5, 0.3f, 1.0f, 0.1f);
    assert_int_equal(status, BLERR_INVALIDSOUNDINDEX);

    status = context->api->BotAddPointLight(origin, 5, 128.0f, 1.0f, 0.3f, 0.2f, 0.0f, 0.25f);
    assert_int_equal(status, BLERR_NOERROR);

    context->api->Test(0, "sounds", origin, origin);
    assert_non_null(Mock_FindPrint(&context->mock, "sound[0]"));

    context->api->Test(0, "pointlights", origin, origin);
    assert_non_null(Mock_FindPrint(&context->mock, "pointlights"));

    context->api->BotShutdownLibrary();

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

static void test_bot_update_entity_populates_aas(void **state)
{
    bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;

    Mock_Reset(&context->mock);

    int status = context->api->BotSetupLibrary();
    assert_int_equal(status, BLERR_NOERROR);

    status = context->api->BotLoadMap("maps/test2.bsp", 0, NULL, 0, NULL, 0, NULL);
    assert_int_equal(status, BLERR_NOERROR);

    bot_updateentity_t entity;
    memset(&entity, 0, sizeof(entity));
    entity.origin[0] = 16.0f;
    entity.origin[1] = 24.0f;
    entity.origin[2] = 48.0f;
    entity.solid = 31;
    entity.modelindex = 2;

    status = context->api->BotUpdateEntity(5, &entity);
    assert_int_equal(status, BLERR_NOERROR);
    assert_true(aasworld.entitiesValid);
    assert_non_null(aasworld.entities);
    assert_int_equal(aasworld.entities[5].number, 5);

    context->api->BotShutdownLibrary();
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

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_bot_load_map_requires_library,
                                        setup_bot_interface,
                                        teardown_bot_interface),
        cmocka_unit_test_setup_teardown(test_bot_load_map_and_sensory_queues,
                                        setup_bot_interface,
                                        teardown_bot_interface),
        cmocka_unit_test_setup_teardown(test_bot_console_message_and_ai_pipeline,
                                        setup_bot_interface,
                                        teardown_bot_interface),
        cmocka_unit_test_setup_teardown(test_bot_update_entity_populates_aas,
                                        setup_bot_interface,
                                        teardown_bot_interface),
        cmocka_unit_test_setup_teardown(test_bot_end_to_end_pipeline_with_assets,
                                        setup_bot_interface,
                                        teardown_bot_interface),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}

