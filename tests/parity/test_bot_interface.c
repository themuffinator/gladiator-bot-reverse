#include <stdarg.h>
#include <stddef.h>
#include <stdbool.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>

#ifndef _WIN32
#include <unistd.h>
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

#include <cmocka.h>

#include "botlib/interface/bot_interface.h"
#include "botlib/interface/bot_state.h"
#include "botlib/ai/chat/ai_chat.h"
#include "botlib/aas/aas_map.h"
#include "botlib/common/l_log.h"
#include "botlib/common/l_memory.h"
#include "botlib/aas/aas_local.h"

#define ARRAY_LEN(x) (sizeof(x) / sizeof((x)[0]))

#ifndef PROJECT_SOURCE_DIR
#error "PROJECT_SOURCE_DIR must be defined so regression tests can resolve asset paths."
#endif

typedef struct bot_interface_asset_environment_s
{
    char asset_root[PATH_MAX];
    char contract_path[PATH_MAX];
    char previous_cwd[PATH_MAX];
    char map_name[64];
    bool have_previous_cwd;
    bool created_syn;
    bool created_match;
    bool created_rchat;
    bool created_map_bsp;
    bool created_map_aas;
} bot_interface_asset_environment_t;

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
    bot_interface_asset_environment_t assets;
    mock_bot_import_t mock;
    bot_export_t *api;
} bot_interface_test_context_t;

static mock_bot_import_t *g_active_mock = NULL;

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

static bool copy_file(const char *source, const char *destination)
{
    if (source == NULL || destination == NULL)
    {
        return false;
    }

    FILE *input = fopen(source, "rb");
    if (input == NULL)
    {
        return false;
    }

    FILE *output = fopen(destination, "wb");
    if (output == NULL)
    {
        fclose(input);
        return false;
    }

    char buffer[4096];
    size_t read_bytes;
    bool ok = true;
    while ((read_bytes = fread(buffer, 1U, sizeof(buffer), input)) > 0U)
    {
        if (fwrite(buffer, 1U, read_bytes, output) != read_bytes)
        {
            ok = false;
            break;
        }
    }

    fclose(output);
    fclose(input);

    if (!ok)
    {
        unlink(destination);
    }

    return ok;
}

static bool ensure_asset_bridge(bot_interface_asset_environment_t *env, const char *filename, bool *created_flag)
{
    if (env == NULL || filename == NULL)
    {
        return false;
    }

    char destination[PATH_MAX];
    snprintf(destination, sizeof(destination), "%s/bots/%s", env->asset_root, filename);

    struct stat info;
    if (stat(destination, &info) == 0)
    {
        return true;
    }

    char source[PATH_MAX];
    snprintf(source, sizeof(source), "%s/%s", env->asset_root, filename);
    if (stat(source, &info) != 0)
    {
        return false;
    }

    if (!copy_file(source, destination))
    {
        return false;
    }

    if (created_flag != NULL)
    {
        *created_flag = true;
    }

    return true;
}

static bool write_minimal_bsp(const char *path)
{
    if (path == NULL)
    {
        return false;
    }

    FILE *file = fopen(path, "wb");
    if (file == NULL)
    {
        return false;
    }

    q2_bsp_header_t header;
    memset(&header, 0, sizeof(header));
    header.ident = Q2_BSP_IDENT;
    header.version = Q2_BSP_VERSION;

    bool ok = fwrite(&header, sizeof(header), 1U, file) == 1U;
    fclose(file);

    if (!ok)
    {
        unlink(path);
    }

    return ok;
}

static bool write_minimal_aas(const char *path)
{
    if (path == NULL)
    {
        return false;
    }

    FILE *file = fopen(path, "wb");
    if (file == NULL)
    {
        return false;
    }

    q2_aas_header_t header;
    memset(&header, 0, sizeof(header));
    header.ident = Q2_AAS_IDENT;
    header.version = Q2_AAS_VERSION;

    bool ok = fwrite(&header, sizeof(header), 1U, file) == 1U;
    fclose(file);

    if (!ok)
    {
        unlink(path);
    }

    return ok;
}

static bool ensure_map_assets(bot_interface_asset_environment_t *env)
{
    if (env == NULL)
    {
        return false;
    }

    if (env->map_name[0] == '\0')
    {
        snprintf(env->map_name, sizeof(env->map_name), "test_nav");
    }

    char bsp_path[PATH_MAX];
    char aas_path[PATH_MAX];
    snprintf(bsp_path, sizeof(bsp_path), "%s/maps/%s.bsp", env->asset_root, env->map_name);
    snprintf(aas_path, sizeof(aas_path), "%s/maps/%s.aas", env->asset_root, env->map_name);

    struct stat info;
    if (stat(bsp_path, &info) != 0)
    {
        if (!write_minimal_bsp(bsp_path))
        {
            return false;
        }
        env->created_map_bsp = true;
    }

    if (stat(aas_path, &info) != 0)
    {
        if (!write_minimal_aas(aas_path))
        {
            if (env->created_map_bsp)
            {
                unlink(bsp_path);
                env->created_map_bsp = false;
            }
            return false;
        }
        env->created_map_aas = true;
    }

    return true;
}

static bool bot_interface_environment_initialise(bot_interface_asset_environment_t *env)
{
    if (env == NULL)
    {
        return false;
    }

    memset(env, 0, sizeof(*env));

    int written = snprintf(env->asset_root,
                           sizeof(env->asset_root),
                           "%s/dev_tools/assets",
                           PROJECT_SOURCE_DIR);
    if (written <= 0 || (size_t)written >= sizeof(env->asset_root))
    {
        return false;
    }

    written = snprintf(env->contract_path,
                       sizeof(env->contract_path),
                       "%s/tests/reference/botlib_contract.json",
                       PROJECT_SOURCE_DIR);
    if (written <= 0 || (size_t)written >= sizeof(env->contract_path))
    {
        return false;
    }

    char probe_path[PATH_MAX];
    snprintf(probe_path, sizeof(probe_path), "%s/bots/babe_c.c", env->asset_root);
    FILE *probe = fopen(probe_path, "rb");
    if (probe == NULL)
    {
        return false;
    }
    fclose(probe);

    if (!ensure_map_assets(env))
    {
        return false;
    }

    if (getcwd(env->previous_cwd, sizeof(env->previous_cwd)) != NULL)
    {
        env->have_previous_cwd = true;
    }

    if (chdir(env->asset_root) != 0)
    {
        return false;
    }

    if (test_setenv("GLADIATOR_ASSET_DIR", env->asset_root) != 0)
    {
        return false;
    }

    if (!ensure_asset_bridge(env, "syn.c", &env->created_syn))
    {
        return false;
    }
    if (!ensure_asset_bridge(env, "match.c", &env->created_match))
    {
        return false;
    }
    if (!ensure_asset_bridge(env, "rchat.c", &env->created_rchat))
    {
        return false;
    }

    return true;
}

static void bot_interface_environment_cleanup(bot_interface_asset_environment_t *env)
{
    if (env == NULL)
    {
        return;
    }

    if (env->created_syn)
    {
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/bots/syn.c", env->asset_root);
        unlink(path);
        env->created_syn = false;
    }

    if (env->created_match)
    {
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/bots/match.c", env->asset_root);
        unlink(path);
        env->created_match = false;
    }

    if (env->created_rchat)
    {
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/bots/rchat.c", env->asset_root);
        unlink(path);
        env->created_rchat = false;
    }

    if (env->created_map_bsp)
    {
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/maps/%s.bsp", env->asset_root, env->map_name);
        unlink(path);
        env->created_map_bsp = false;
    }

    if (env->created_map_aas)
    {
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/maps/%s.aas", env->asset_root, env->map_name);
        unlink(path);
        env->created_map_aas = false;
    }

    test_unsetenv("GLADIATOR_ASSET_DIR");

    if (env->have_previous_cwd)
    {
        chdir(env->previous_cwd);
        env->have_previous_cwd = false;
    }
}

static bool botlib_contract_allows_return_code(const bot_interface_asset_environment_t *env,
                                               const char *function_name,
                                               int code)
{
    if (env == NULL || function_name == NULL)
    {
        return false;
    }

    FILE *file = fopen(env->contract_path, "rb");
    if (file == NULL)
    {
        return false;
    }

    if (fseek(file, 0L, SEEK_END) != 0)
    {
        fclose(file);
        return false;
    }

    long size = ftell(file);
    if (size <= 0)
    {
        fclose(file);
        return false;
    }

    if (fseek(file, 0L, SEEK_SET) != 0)
    {
        fclose(file);
        return false;
    }

    char *buffer = (char *)malloc((size_t)size + 1U);
    if (buffer == NULL)
    {
        fclose(file);
        return false;
    }

    size_t read_bytes = fread(buffer, 1U, (size_t)size, file);
    fclose(file);
    buffer[read_bytes] = '\0';

    char needle[128];
    snprintf(needle, sizeof(needle), "\"name\": \"%s\"", function_name);
    char *entry = strstr(buffer, needle);
    if (entry == NULL)
    {
        free(buffer);
        return false;
    }

    char *return_codes = strstr(entry, "\"return_codes\"");
    if (return_codes == NULL)
    {
        free(buffer);
        return false;
    }

    char *open_bracket = strchr(return_codes, '[');
    char *close_bracket = strchr(return_codes, ']');
    if (open_bracket == NULL || close_bracket == NULL || close_bracket <= open_bracket)
    {
        free(buffer);
        return false;
    }

    char saved = close_bracket[1];
    close_bracket[1] = '\0';

    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"value\": %d", code);
    bool found = strstr(open_bracket, pattern) != NULL;

    close_bracket[1] = saved;
    free(buffer);
    return found;
}

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

    if (!bot_interface_environment_initialise(&context->assets))
    {
        bot_interface_environment_cleanup(&context->assets);
        free(context);
        cmocka_skip("bot interface parity tests require dev_tools/assets");
        return -1;
    }

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

    g_active_mock = &context->mock;
    context->api = GetBotAPI(&context->mock.table);
    assert_non_null(context->api);

    char weaponconfig_path[PATH_MAX];
    snprintf(weaponconfig_path,
             sizeof(weaponconfig_path),
             "%s/weapons.c",
             context->assets.asset_root);

    char weaponconfig_var[] = "weaponconfig";
    char max_weaponinfo_var[] = "max_weaponinfo";
    char max_weaponinfo_value[] = "64";
    char max_projectileinfo_var[] = "max_projectileinfo";
    char max_projectileinfo_value[] = "64";
    char gladiator_asset_var[] = "GLADIATOR_ASSET_DIR";

    context->api->BotLibVarSet(weaponconfig_var, weaponconfig_path);
    context->api->BotLibVarSet(max_weaponinfo_var, max_weaponinfo_value);
    context->api->BotLibVarSet(max_projectileinfo_var, max_projectileinfo_value);
    context->api->BotLibVarSet(gladiator_asset_var, context->assets.asset_root);

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

    BotState_ShutdownAll();
    g_active_mock = NULL;
    if (context != NULL)
    {
        bot_interface_environment_cleanup(&context->assets);
    }
    free(context);
    return 0;
}

static void test_bot_load_map_requires_library(void **state)
{
    bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;

    Mock_Reset(&context->mock);

    int status = context->api->BotLoadMap(context->assets.map_name, 0, NULL, 0, NULL, 0, NULL);
    assert_int_equal(status, BLERR_LIBRARYNOTSETUP);
}

static void test_bot_load_map_and_sensory_queues(void **state)
{
    bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;

    Mock_Reset(&context->mock);

    int status = context->api->BotSetupLibrary();
    assert_int_equal(status, BLERR_NOERROR);

    char *sounds[] = {"world/ambient.wav", "weapons/impact.wav"};
    status = context->api->BotLoadMap(context->assets.map_name, 0, NULL, 2, sounds, 0, NULL);
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
}

static void test_bot_console_message_and_ai_pipeline(void **state)
{
    bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;

    Mock_Reset(&context->mock);

    int status = context->api->BotSetupLibrary();
    assert_int_equal(status, BLERR_NOERROR);
    assert_true(botlib_contract_allows_return_code(&context->assets, "BotLibSetup", status));
    assert_non_null(Mock_FindPrint(&context->mock, "------- BotLib Initialization -------"));

    status = context->api->BotLoadMap(context->assets.map_name, 0, NULL, 0, NULL, 0, NULL);
    assert_int_equal(status, BLERR_NOERROR);
    assert_true(botlib_contract_allows_return_code(&context->assets, "BotLibLoadMap", status));

    bot_settings_t settings;
    memset(&settings, 0, sizeof(settings));
    snprintf(settings.characterfile, sizeof(settings.characterfile), "bots/babe_c.c");
    snprintf(settings.charactername, sizeof(settings.charactername), "Babe");

    status = context->api->BotSetupClient(1, &settings);
    assert_int_equal(status, BLERR_NOERROR);

    status = context->api->BotConsoleMessage(1, CMS_CHAT, "hello gladiator");
    assert_int_equal(status, BLERR_NOERROR);
    assert_true(botlib_contract_allows_return_code(&context->assets, "BotLibConsoleMessage", status));

    context->api->Test(1, "dump_chat", NULL, NULL);
    assert_non_null(Mock_FindPrint(&context->mock, "hello gladiator"));

    status = context->api->BotStartFrame(0.1f);
    assert_int_equal(status, BLERR_NOERROR);
    assert_true(botlib_contract_allows_return_code(&context->assets, "BotLibStartFrame", status));

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

    status = context->api->BotShutdownClient(1);
    assert_int_equal(status, BLERR_NOERROR);

    status = context->api->BotShutdownLibrary();
    assert_int_equal(status, BLERR_NOERROR);
}

static void test_bot_update_entity_populates_aas(void **state)
{
    bot_interface_test_context_t *context = (bot_interface_test_context_t *)*state;

    Mock_Reset(&context->mock);

    int status = context->api->BotSetupLibrary();
    assert_int_equal(status, BLERR_NOERROR);

    status = context->api->BotLoadMap(context->assets.map_name, 0, NULL, 0, NULL, 0, NULL);
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
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}

