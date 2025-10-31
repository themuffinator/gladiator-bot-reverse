#include <assert.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../q2bridge/botlib.h"
#include "../../q2bridge/bridge.h"
#include "../../q2bridge/update_translator.h"
#include "../common/l_libvar.h"
#include "../common/l_log.h"
#include "../aas/aas_map.h"
#include "../ai/chat/ai_chat.h"
#include "../precomp/l_precomp.h"
#include "botlib_interface.h"
#include "bot_interface.h"


static bot_import_t *g_botImport = NULL;
static bot_chatstate_t *g_botInterfaceConsoleChat = NULL;
static botlib_import_table_t g_botInterfaceImportTable;

typedef struct botinterface_import_libvar_s {
    char *name;
    char *value;
    struct botinterface_import_libvar_s *next;
} botinterface_import_libvar_t;

static botinterface_import_libvar_t *g_botInterfaceLibVars = NULL;

static void BotInterface_FreeImportLibVar(botinterface_import_libvar_t *entry)
{
    if (entry == NULL)
    {
        return;
    }

    free(entry->name);
    free(entry->value);
    free(entry);
}

static void BotInterface_ResetImportLibVars(void)
{
    botinterface_import_libvar_t *entry = g_botInterfaceLibVars;
    while (entry != NULL)
    {
        botinterface_import_libvar_t *next = entry->next;
        BotInterface_FreeImportLibVar(entry);
        entry = next;
    }

    g_botInterfaceLibVars = NULL;
}

static botinterface_import_libvar_t *BotInterface_FindImportLibVar(const char *name)
{
    botinterface_import_libvar_t *entry = g_botInterfaceLibVars;
    while (entry != NULL)
    {
        if (entry->name != NULL && name != NULL && strcmp(entry->name, name) == 0)
        {
            return entry;
        }

        entry = entry->next;
    }

    return NULL;
}

static botinterface_import_libvar_t *BotInterface_EnsureImportLibVar(const char *name)
{
    if (name == NULL)
    {
        return NULL;
    }

    botinterface_import_libvar_t *entry = BotInterface_FindImportLibVar(name);
    if (entry != NULL)
    {
        return entry;
    }

    entry = calloc(1, sizeof(*entry));
    if (entry == NULL)
    {
        return NULL;
    }

    entry->name = strdup(name);
    if (entry->name == NULL)
    {
        free(entry);
        return NULL;
    }

    entry->value = strdup("");
    if (entry->value == NULL)
    {
        free(entry->name);
        free(entry);
        return NULL;
    }

    entry->next = g_botInterfaceLibVars;
    g_botInterfaceLibVars = entry;
    return entry;
}

static void BotInterface_PrintWrapper(int type, const char *fmt, ...)
{
    if (fmt == NULL)
    {
        return;
    }

    if (g_botImport == NULL || g_botImport->Print == NULL)
    {
        return;
    }

    va_list args;
    va_start(args, fmt);

    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), fmt, args);

    va_end(args);

    buffer[sizeof(buffer) - 1] = '\0';
    g_botImport->Print(type, "%s", buffer);
}

static void BotInterface_DPrintWrapper(const char *fmt, ...)
{
    if (fmt == NULL)
    {
        return;
    }

    if (g_botImport == NULL || g_botImport->Print == NULL)
    {
        return;
    }

    va_list args;
    va_start(args, fmt);

    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), fmt, args);

    va_end(args);

    buffer[sizeof(buffer) - 1] = '\0';
    g_botImport->Print(PRT_MESSAGE, "%s", buffer);
}

static int BotInterface_BotLibVarGetWrapper(const char *var_name, char *value, size_t size)
{
    if (value == NULL || size == 0)
    {
        return -1;
    }

    value[0] = '\0';

    if (var_name == NULL)
    {
        return -1;
    }

    botinterface_import_libvar_t *entry = BotInterface_FindImportLibVar(var_name);
    if (entry == NULL || entry->value == NULL)
    {
        return -1;
    }

    size_t length = strlen(entry->value);
    if (length >= size)
    {
        length = size - 1;
    }

    memcpy(value, entry->value, length);
    value[length] = '\0';
    return 0;
}

static int BotInterface_BotLibVarSetWrapper(const char *var_name, const char *value)
{
    if (var_name == NULL || value == NULL)
    {
        return -1;
    }

    botinterface_import_libvar_t *entry = BotInterface_EnsureImportLibVar(var_name);
    if (entry == NULL)
    {
        return -1;
    }

    char *copy = strdup(value);
    if (copy == NULL)
    {
        return -1;
    }

    free(entry->value);
    entry->value = copy;
    return 0;
}

static void BotInterface_BuildImportTable(bot_import_t *import_table)
{
    (void)import_table;

    BotInterface_ResetImportLibVars();

    g_botInterfaceImportTable.Print = BotInterface_PrintWrapper;
    g_botInterfaceImportTable.DPrint = BotInterface_DPrintWrapper;
    g_botInterfaceImportTable.BotLibVarGet = BotInterface_BotLibVarGetWrapper;
    g_botInterfaceImportTable.BotLibVarSet = BotInterface_BotLibVarSetWrapper;
}

typedef struct botlib_import_cache_entry_s {
    struct botlib_import_cache_entry_s *next;
    char *name;
    char *value;
} botlib_import_cache_entry_t;

static botlib_import_cache_entry_t *g_botImportCache = NULL;
static botlib_import_table_t g_botlibImportTable = {0};

static char *BotInterface_CopyString(const char *text)
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

static void BotInterface_FreeImportCache(void)
{
    botlib_import_cache_entry_t *entry = g_botImportCache;
    while (entry != NULL)
    {
        botlib_import_cache_entry_t *next = entry->next;
        free(entry->name);
        free(entry->value);
        free(entry);
        entry = next;
    }

    g_botImportCache = NULL;
}

static bool BotInterface_UpdateImportCache(const char *name, const char *value)
{
    if (name == NULL || value == NULL)
    {
        return false;
    }

    for (botlib_import_cache_entry_t *entry = g_botImportCache; entry != NULL; entry = entry->next)
    {
        if (strcmp(entry->name, name) == 0)
        {
            char *copy = BotInterface_CopyString(value);
            if (copy == NULL)
            {
                return false;
            }

            free(entry->value);
            entry->value = copy;
            return true;
        }
    }

    botlib_import_cache_entry_t *fresh = (botlib_import_cache_entry_t *)calloc(1, sizeof(*fresh));
    if (fresh == NULL)
    {
        return false;
    }

    fresh->name = BotInterface_CopyString(name);
    fresh->value = BotInterface_CopyString(value);
    if (fresh->name == NULL || fresh->value == NULL)
    {
        free(fresh->name);
        free(fresh->value);
        free(fresh);
        return false;
    }

    fresh->next = g_botImportCache;
    g_botImportCache = fresh;
    return true;
}

static int BotInterface_BotLibVarGetShim(const char *name, char *buffer, size_t buffer_size)
{
    if (name == NULL || buffer == NULL || buffer_size == 0)
    {
        return BLERR_INVALIDIMPORT;
    }

    for (botlib_import_cache_entry_t *entry = g_botImportCache; entry != NULL; entry = entry->next)
    {
        if (strcmp(entry->name, name) == 0)
        {
            strncpy(buffer, entry->value, buffer_size - 1);
            buffer[buffer_size - 1] = '\0';
            return BLERR_NOERROR;
        }
    }

    buffer[0] = '\0';
    return BLERR_INVALIDIMPORT;
}

static void BotInterface_PrintShim(int priority, const char *fmt, ...)
{
    if (g_botImport == NULL || g_botImport->Print == NULL || fmt == NULL)
    {
        return;
    }

    va_list args;
    va_start(args, fmt);

    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), fmt, args);

    va_end(args);

    g_botImport->Print(priority, "%s", buffer);
}

static void BotInterface_InitialiseImportTable(bot_import_t *imports)
{
    g_botImport = imports;

    memset(&g_botlibImportTable, 0, sizeof(g_botlibImportTable));
    g_botlibImportTable.Print = BotInterface_PrintShim;
    g_botlibImportTable.BotLibVarGet = BotInterface_BotLibVarGetShim;
    g_botlibImportTable.BotLibVarSet = NULL;

    BotInterface_SetImportTable(&g_botlibImportTable);
}

static void BotInterface_PrintBanner(int priority, const char *message)
{
    if (message == NULL)
    {
        return;
    }

    if (g_botImport != NULL && g_botImport->Print != NULL)
    {
        g_botImport->Print(priority, "%s", message);
    }
    else
    {
        BotLib_Print(priority, "%s", message);
    }
}

static bot_chatstate_t *BotInterface_EnsureConsoleChatState(void)
{
    if (g_botInterfaceConsoleChat == NULL) {
        g_botInterfaceConsoleChat = BotAllocChatState();
        if (g_botInterfaceConsoleChat == NULL) {
            if (g_botImport != NULL && g_botImport->Print != NULL) {
                g_botImport->Print(PRT_ERROR, "[bot_interface] failed to allocate console chat state\n");
            }
        }
    }

    return g_botInterfaceConsoleChat;
}

static void BotInterface_Log(int priority, const char *functionName)
{
    if (g_botImport != NULL && g_botImport->Print != NULL)
    {
        g_botImport->Print(priority, "[bot_interface] %s stub invoked\n", functionName);
    }
}

static char *BotVersion(void)
{
    static char version[] = "gladiator-bot-interface-stub";

    return version;
}

static int BotSetupLibraryWrapper(void)
{
    BotInterface_PrintBanner(PRT_MESSAGE, "------- BotLib Initialization -------\n");
    BotInterface_SetImportTable(&g_botlibImportTable);

    int result = BotSetupLibrary();
    if (result != BLERR_NOERROR)
    {
        return result;
    }

    return result;
}

static int BotShutdownLibraryWrapper(void)
{
    int result = BotShutdownLibrary();

    BotInterface_PrintBanner(PRT_MESSAGE, "------- BotLib Shutdown -------\n");

    if (g_botInterfaceConsoleChat != NULL)
static int BotInterface_BotSetupLibrary(void)
{
    assert(g_botImport != NULL);
    return BotSetupLibrary();
}

static int BotInterface_BotShutdownLibrary(void)
{
    assert(g_botImport != NULL);

    int status = BotShutdownLibrary();
    if (status == BLERR_NOERROR)
    {
        if (g_botInterfaceConsoleChat != NULL)
        {
            BotFreeChatState(g_botInterfaceConsoleChat);
            g_botInterfaceConsoleChat = NULL;
        }
        AAS_Shutdown();
    }

    AAS_Shutdown();
    BotInterface_FreeImportCache();
    BotInterface_SetImportTable(NULL);
    Q2Bridge_ClearImportTable();
    BotLib_LogShutdown();

    return result;
}

static int BotLibraryInitializedWrapper(void)
{
    return BotLibraryInitialized() ? 1 : 0;
}

static int BotLibVarSetWrapper(char *var_name, char *value)
{
    if (var_name == NULL || value == NULL)
    {
        return BLERR_INVALIDIMPORT;
    }

    if (!BotInterface_UpdateImportCache(var_name, value))
    {
        return BLERR_INVALIDIMPORT;
    }

    LibVarSet(var_name, value);
    return status;
}

static int BotInterface_BotLibraryInitialized(void)
{
    assert(g_botImport != NULL);
    return BotLibraryInitialized() ? 1 : 0;
}

static int BotInterface_BotLibVarSet(char *var_name, char *value)
{
    assert(g_botImport != NULL);
    BotInterface_Log(PRT_WARNING, __func__);
    (void)var_name;
    (void)value;
    return BLERR_NOERROR;
}

static int BotDefineWrapper(char *string)
{
    if (string == NULL)
    {
        return BLERR_INVALIDIMPORT;
    }

    if (!PC_AddGlobalDefine(string))
    {
        return BLERR_INVALIDIMPORT;
    }

    return BLERR_NOERROR;
}

static int BotLoadMapStub(char *mapname, int modelindexes, char *modelindex[], int soundindexes,
                          char *soundindex[], int imageindexes, char *imageindex[])
{
    assert(g_botImport != NULL);
    BotInterface_Log(PRT_WARNING, __func__);
    return AAS_LoadMap(mapname, modelindexes, modelindex, soundindexes, soundindex, imageindexes, imageindex);
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
    (void)time;

    assert(g_botImport != NULL);
    BotInterface_Log(PRT_WARNING, __func__);
    return BLERR_NOERROR;
}

static int BotUpdateClientStub(int client, bot_updateclient_t *buc)
{
    assert(g_botImport != NULL);

    int status = Bridge_UpdateClient(client, buc);
    if (status != BLERR_NOERROR)
    {
        return status;
    }

    BotInterface_Log(PRT_WARNING, __func__);
    return BLERR_NOERROR;
}

static int BotUpdateEntityStub(int ent, bot_updateentity_t *bue)
{
    assert(g_botImport != NULL);

    int status = Bridge_UpdateEntity(ent, bue);
    if (status != BLERR_NOERROR)
    {
        return status;
    }

    BotInterface_Log(PRT_WARNING, __func__);
    return AAS_UpdateEntity(ent, bue);
}

static int BotAddSoundStub(vec3_t origin, int ent, int channel, int soundindex, float volume, float attenuation,
                           float timeofs)
{
    (void)origin;
    (void)ent;
    (void)channel;
    (void)soundindex;
    (void)volume;
    (void)attenuation;
    (void)timeofs;

    assert(g_botImport != NULL);
    BotInterface_Log(PRT_WARNING, __func__);
    return BLERR_NOERROR;
}

static int BotAddPointLightStub(vec3_t origin, int ent, float radius, float r, float g, float b, float time,
                                float decay)
{
    (void)origin;
    (void)ent;
    (void)radius;
    (void)r;
    (void)g;
    (void)b;
    (void)time;
    (void)decay;

    assert(g_botImport != NULL);
    BotInterface_Log(PRT_WARNING, __func__);
    return BLERR_NOERROR;
}

static int BotAIStub(int client, float thinktime)
{
    (void)client;
    (void)thinktime;

    assert(g_botImport != NULL);
    BotInterface_Log(PRT_WARNING, __func__);
    return BLERR_NOERROR;
}

static int BotConsoleMessageStub(int client, int type, char *message)
{
    (void)client;

    assert(g_botImport != NULL);
    BotInterface_Log(PRT_WARNING, __func__);
    bot_chatstate_t *chat_state = BotInterface_EnsureConsoleChatState();
    if (chat_state != NULL && message != NULL)
    {
        BotQueueConsoleMessage(chat_state, type, message);
    }
    return BLERR_NOERROR;
}

static int TestStub(int parm0, char *parm1, vec3_t parm2, vec3_t parm3)
{
    (void)parm0;
    (void)parm1;
    (void)parm2;
    (void)parm3;

    assert(g_botImport != NULL);
    BotInterface_Log(PRT_WARNING, __func__);
    bot_chatstate_t *chat_state = BotInterface_EnsureConsoleChatState();
    if (chat_state != NULL && g_botImport->Print != NULL)
    {
        int message_type = 0;
        char buffer[256];
        if (BotNextConsoleMessage(chat_state, &message_type, buffer, sizeof(buffer)))
        {
            g_botImport->Print(PRT_MESSAGE,
                               "[bot_interface] console chat (%d): %s\n",
                               message_type,
                               buffer);
        }
        else
        {
            g_botImport->Print(PRT_MESSAGE,
                               "[bot_interface] console chat queue empty (%zu pending)\n",
                               BotNumConsoleMessages(chat_state));
        }
    }
    return 0;
}

bot_export_t *GetBotAPI(bot_import_t *import)
{
    static bot_export_t exportTable;

    BotInterface_FreeImportCache();
    BotInterface_InitialiseImportTable(import);
    g_botImport = import;
    BotInterface_BuildImportTable(import);
    BotInterface_SetImportTable(&g_botInterfaceImportTable);
    Q2Bridge_SetImportTable(import);
    Bridge_ResetCachedUpdates();
    assert(g_botImport != NULL);

    exportTable.BotVersion = BotVersion;
    exportTable.BotSetupLibrary = BotSetupLibraryWrapper;
    exportTable.BotShutdownLibrary = BotShutdownLibraryWrapper;
    exportTable.BotLibraryInitialized = BotLibraryInitializedWrapper;
    exportTable.BotLibVarSet = BotLibVarSetWrapper;
    exportTable.BotDefine = BotDefineWrapper;
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

