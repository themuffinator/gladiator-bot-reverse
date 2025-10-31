#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "../../q2bridge/botlib.h"
#include "../../q2bridge/bridge.h"
#include "../../q2bridge/update_translator.h"
#include "../../shared/q_shared.h"
#include "../aas/aas_map.h"
#include "../ai/bot_ai.h"
#include "../ai/chat/ai_chat.h"
#include "bot_interface.h"

#define BOT_INTERFACE_DEFAULT_MAX_ENTITIES 1024


static int g_bot_initialized = 0;

static bot_import_t *g_botImport = NULL;
typedef struct bot_interface_string_table_s
{
    char **entries;
    int count;
} bot_interface_string_table_t;

typedef struct bot_interface_map_cache_s
{
    bot_interface_string_table_t items;
    bot_interface_string_table_t sounds;
    bot_interface_string_table_t images;
} bot_interface_map_cache_t;

static bot_interface_map_cache_t g_bot_map_cache;

static void BotInterface_Log(int priority, const char *functionName)
{
    if (g_botImport != NULL && g_botImport->Print != NULL)
    {
        g_botImport->Print(priority, "[bot_interface] %s stub invoked\n", functionName);
    }
}

static void BotInterface_FreeStringTable(bot_interface_string_table_t *table)
{
    if (table == NULL || table->entries == NULL)
    {
        return;
    }

    for (int i = 0; i < table->count; ++i)
    {
        free(table->entries[i]);
    }

    free(table->entries);
    table->entries = NULL;
    table->count = 0;
}

static char *BotInterface_DuplicateString(const char *text)
{
    if (text == NULL)
    {
        return NULL;
    }

    size_t length = strlen(text) + 1U;
    char *copy = malloc(length);
    if (copy != NULL)
    {
        memcpy(copy, text, length);
    }

    return copy;
}

static void BotInterface_SeedStringTable(bot_interface_string_table_t *table, int count, char *source[])
{
    BotInterface_FreeStringTable(table);

    if (table == NULL || count <= 0 || source == NULL)
    {
        return;
    }

    table->entries = calloc((size_t)count, sizeof(char *));
    if (table->entries == NULL)
    {
        table->count = 0;
        return;
    }

    table->count = count;
    for (int i = 0; i < count; ++i)
    {
        table->entries[i] = BotInterface_DuplicateString(source[i]);
    }
}

static void BotInterface_ResetMapCache(void)
{
    BotInterface_FreeStringTable(&g_bot_map_cache.items);
    BotInterface_FreeStringTable(&g_bot_map_cache.sounds);
    BotInterface_FreeStringTable(&g_bot_map_cache.images);
}

static bool BotInterface_IsSoundIndexValid(int soundindex)
{
    return soundindex >= 0 && soundindex < g_bot_map_cache.sounds.count;
}

static char *BotVersionStub(void)
{
    static char version[] = "gladiator-bot-interface-stub";

    assert(g_botImport != NULL);
    BotInterface_Log(PRT_MESSAGE, __func__);
    return version;
}

static int BotSetupLibraryStub(void)
{
    assert(g_botImport != NULL);
    if (g_bot_initialized)
    {
        return BLERR_LIBRARYALREADYSETUP;
    }

    BotAI_ResetForNewMap();
    BotInterface_ResetMapCache();
    Bridge_ResetCachedUpdates();

    g_bot_initialized = 1;
    return BLERR_NOERROR;
}

static int BotShutdownLibraryStub(void)
{
    assert(g_botImport != NULL);
    if (!g_bot_initialized)
    {
        return BLERR_LIBRARYNOTSETUP;
    }

    BotAI_ResetForNewMap();
    BotInterface_ResetMapCache();
    Bridge_ResetCachedUpdates();
    AAS_Shutdown();

    g_bot_initialized = 0;
    return BLERR_NOERROR;
}

static int BotLibraryInitializedStub(void)
{
    assert(g_botImport != NULL);
    return g_bot_initialized;
}

static int BotLibVarSetStub(char *var_name, char *value)
{
    (void)var_name;
    (void)value;

    assert(g_botImport != NULL);
    BotInterface_Log(PRT_WARNING, __func__);
    return BLERR_NOERROR;
}

static int BotDefineStub(char *string)
{
    (void)string;

    assert(g_botImport != NULL);
    BotInterface_Log(PRT_WARNING, __func__);
    return BLERR_NOERROR;
}

static int BotLoadMapStub(char *mapname, int modelindexes, char *modelindex[], int soundindexes,
                          char *soundindex[], int imageindexes, char *imageindex[])
{
    assert(g_botImport != NULL);
    if (!g_bot_initialized)
    {
        return BLERR_LIBRARYNOTSETUP;
    }

    if (mapname == NULL || mapname[0] == '\0')
    {
        return BLERR_NOAASFILE;
    }

    BotAI_ResetForNewMap();
    BotInterface_ResetMapCache();
    Bridge_ResetCachedUpdates();
    Bridge_SetEntityLimits(MAX_CLIENTS, BOT_INTERFACE_DEFAULT_MAX_ENTITIES);

    BotInterface_SeedStringTable(&g_bot_map_cache.items, modelindexes, modelindex);
    BotInterface_SeedStringTable(&g_bot_map_cache.sounds, soundindexes, soundindex);
    BotInterface_SeedStringTable(&g_bot_map_cache.images, imageindexes, imageindex);

    int status = AAS_LoadMap(mapname, modelindexes, modelindex, soundindexes, soundindex, imageindexes, imageindex);
    if (status != BLERR_NOERROR)
    {
        return status;
    }

    return BLERR_NOERROR;
}

static int BotSetupClientStub(int client, bot_settings_t *settings)
{
    (void)client;
    (void)settings;

    assert(g_botImport != NULL);
    BotInterface_Log(PRT_WARNING, __func__);
    return BLERR_NOERROR;
}

static int BotShutdownClientStub(int client)
{
    (void)client;

    assert(g_botImport != NULL);
    BotInterface_Log(PRT_WARNING, __func__);
    return BLERR_NOERROR;
}

static int BotMoveClientStub(int oldclnum, int newclnum)
{
    (void)oldclnum;
    (void)newclnum;

    assert(g_botImport != NULL);
    BotInterface_Log(PRT_WARNING, __func__);
    return BLERR_NOERROR;
}

static int BotClientSettingsStub(int client, bot_clientsettings_t *settings)
{
    (void)client;
    (void)settings;

    assert(g_botImport != NULL);
    BotInterface_Log(PRT_WARNING, __func__);
    return BLERR_NOERROR;
}

static int BotSettingsStub(int client, bot_settings_t *settings)
{
    (void)client;
    (void)settings;

    assert(g_botImport != NULL);
    BotInterface_Log(PRT_WARNING, __func__);
    return BLERR_NOERROR;
}

static int BotStartFrameStub(float time)
{
    assert(g_botImport != NULL);
    if (!g_bot_initialized)
    {
        return BLERR_LIBRARYNOTSETUP;
    }

    BotAI_BeginFrame(time);
    return BLERR_NOERROR;
}

static int BotUpdateClientStub(int client, bot_updateclient_t *buc)
{
    assert(g_botImport != NULL);

    if (!g_bot_initialized)
    {
        return BLERR_LIBRARYNOTSETUP;
    }

    int status = Bridge_UpdateClient(client, buc);
    if (status != BLERR_NOERROR)
    {
        return status;
    }

    return BLERR_NOERROR;
}

static int BotUpdateEntityStub(int ent, bot_updateentity_t *bue)
{
    assert(g_botImport != NULL);

    if (!g_bot_initialized)
    {
        return BLERR_LIBRARYNOTSETUP;
    }

    int status = Bridge_UpdateEntity(ent, bue);
    if (status != BLERR_NOERROR)
    {
        return status;
    }

    return AAS_UpdateEntity(ent, bue);
}

static int BotAddSoundStub(vec3_t origin, int ent, int channel, int soundindex, float volume, float attenuation,
                           float timeofs)
{
    assert(g_botImport != NULL);

    if (!g_bot_initialized)
    {
        return BLERR_LIBRARYNOTSETUP;
    }

    if (!BotInterface_IsSoundIndexValid(soundindex))
    {
        return BLERR_INVALIDSOUNDINDEX;
    }

    if (ent >= 0)
    {
        int validation = Bridge_ValidateEntityNumber(ent, "BotAddSound");
        if (validation != BLERR_NOERROR)
        {
            return validation;
        }
    }

    return BotAI_AddSoundEvent(origin, ent, channel, soundindex, volume, attenuation, timeofs);
}

static int BotAddPointLightStub(vec3_t origin, int ent, float radius, float r, float g, float b, float time,
                                float decay)
{
    assert(g_botImport != NULL);

    if (!g_bot_initialized)
    {
        return BLERR_LIBRARYNOTSETUP;
    }

    if (ent >= 0)
    {
        int validation = Bridge_ValidateEntityNumber(ent, "BotAddPointLight");
        if (validation != BLERR_NOERROR)
        {
            return validation;
        }
    }

    return BotAI_AddPointLightEvent(origin, ent, radius, r, g, b, time, decay);
}

static int BotAIStub(int client, float thinktime)
{
    assert(g_botImport != NULL);

    if (!g_bot_initialized)
    {
        return BLERR_LIBRARYNOTSETUP;
    }

    return BotAI_Think(client, thinktime);
}

static int BotConsoleMessageStub(int client, int type, char *message)
{
    assert(g_botImport != NULL);

    if (!g_bot_initialized)
    {
        return BLERR_LIBRARYNOTSETUP;
    }

    if (message != NULL)
    {
        BotAI_QueueConsoleMessage(client, type, message);
    }
    return BLERR_NOERROR;
}

static void BotInterface_TestUsage(void)
{
    if (g_botImport != NULL && g_botImport->Print != NULL)
    {
        g_botImport->Print(PRT_MESSAGE,
                           "[bot_interface] Test <client> dump_console|toggle_debug\n");
    }
}

static int TestStub(int parm0, char *parm1, vec3_t parm2, vec3_t parm3)
{
    (void)parm2;
    (void)parm3;

    assert(g_botImport != NULL);

    if (!g_bot_initialized)
    {
        return BLERR_LIBRARYNOTSETUP;
    }

    const char *command = (parm1 != NULL) ? parm1 : "dump_console";

    if (strcmp(command, "dump_console") == 0)
    {
        if (parm0 >= 0)
        {
            BotAI_DebugDumpConsoleMessages(g_botImport, parm0);
        }
        else
        {
            BotAI_DebugDumpConsoleMessagesAll(g_botImport);
        }

        return BLERR_NOERROR;
    }

    if (strcmp(command, "toggle_debug") == 0)
    {
        if (parm0 < 0)
        {
            BotInterface_TestUsage();
            return BLERR_INVALIDCLIENTNUMBER;
        }

        BotAI_DebugToggleDraw(g_botImport, parm0);
        return BLERR_NOERROR;
    }

    BotInterface_TestUsage();
    return BLERR_NOERROR;
}

bot_export_t *GetBotAPI(bot_import_t *import)
{
    static bot_export_t exportTable;

    g_botImport = import;
    Q2Bridge_SetImportTable(import);
    assert(g_botImport != NULL);

    exportTable.BotVersion = BotVersionStub;
    exportTable.BotSetupLibrary = BotSetupLibraryStub;
    exportTable.BotShutdownLibrary = BotShutdownLibraryStub;
    exportTable.BotLibraryInitialized = BotLibraryInitializedStub;
    exportTable.BotLibVarSet = BotLibVarSetStub;
    exportTable.BotDefine = BotDefineStub;
    exportTable.BotLoadMap = BotLoadMapStub;
    exportTable.BotSetupClient = BotSetupClientStub;
    exportTable.BotShutdownClient = BotShutdownClientStub;
    exportTable.BotMoveClient = BotMoveClientStub;
    exportTable.BotClientSettings = BotClientSettingsStub;
    exportTable.BotSettings = BotSettingsStub;
    exportTable.BotStartFrame = BotStartFrameStub;
    exportTable.BotUpdateClient = BotUpdateClientStub;
    exportTable.BotUpdateEntity = BotUpdateEntityStub;
    exportTable.BotAddSound = BotAddSoundStub;
    exportTable.BotAddPointLight = BotAddPointLightStub;
    exportTable.BotAI = BotAIStub;
    exportTable.BotConsoleMessage = BotConsoleMessageStub;
    exportTable.Test = TestStub;

    return &exportTable;
}

static bot_status_t BotLibSetupStub(void)
{
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

