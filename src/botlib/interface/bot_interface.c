#include "bot_interface.h"

#include <stddef.h>

static bot_import_t *g_bot_import = NULL;
static int g_bot_initialized = 0;
static const int kBotLibApiVersion = BOTLIB_API_VERSION;

static bot_status_t BotLibSetupStub(void)
{
    (void)g_bot_import;
    g_bot_initialized = 1;
    return BOT_STATUS_NOT_IMPLEMENTED;
}

static bot_status_t BotLibShutdownStub(void)
{
    g_bot_initialized = 0;
    return BOT_STATUS_NOT_IMPLEMENTED;
}

static bot_status_t BotLibLoadMapStub(const char *mapname)
{
    (void)mapname;
    if (!g_bot_initialized) {
        return BOT_STATUS_NOT_INITIALIZED;
    }
    return BOT_STATUS_NOT_IMPLEMENTED;
}

static bot_status_t BotLibFreeMapStub(void)
{
    if (!g_bot_initialized) {
        return BOT_STATUS_NOT_INITIALIZED;
    }
    return BOT_STATUS_NOT_IMPLEMENTED;
}

bot_export_t *GetBotAPI(int api_version, bot_import_t *import_table)
{
    static bot_export_t exports;

    if (!import_table) {
        return NULL;
    }

    if (api_version != kBotLibApiVersion) {
        return NULL;
    }

    g_bot_import = import_table;
    g_bot_initialized = 0;

    exports.api_version = kBotLibApiVersion;
    exports.BotLibSetup = BotLibSetupStub;
    exports.BotLibShutdown = BotLibShutdownStub;
    exports.BotLibLoadMap = BotLibLoadMapStub;
    exports.BotLibFreeMap = BotLibFreeMapStub;

    return &exports;
}
