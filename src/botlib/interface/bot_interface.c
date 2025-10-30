#include <assert.h>
#include <stddef.h>

#include "../../q2bridge/botlib.h"
#include "../../q2bridge/bridge.h"
#include "../aas/aas_map.h"
#include "../ai/chat/ai_chat.h"
#include "bot_interface.h"


static int g_bot_initialized = 0;

static bot_import_t *g_botImport = NULL;
static bot_chatstate_t *g_botInterfaceConsoleChat = NULL;

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
    BotInterface_Log(PRT_WARNING, __func__);
    return BLERR_NOERROR;
}

static int BotShutdownLibraryStub(void)
{
    assert(g_botImport != NULL);
    BotInterface_Log(PRT_WARNING, __func__);
    if (g_botInterfaceConsoleChat != NULL)
    {
        BotFreeChatState(g_botInterfaceConsoleChat);
        g_botInterfaceConsoleChat = NULL;
    }
    AAS_Shutdown();
    return BLERR_NOERROR;
}

static int BotLibraryInitializedStub(void)
{
    assert(g_botImport != NULL);
    BotInterface_Log(PRT_WARNING, __func__);
    return 0;
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
    (void)client;
    (void)buc;

    assert(g_botImport != NULL);
    BotInterface_Log(PRT_WARNING, __func__);
    return BLERR_NOERROR;
}

static int BotUpdateEntityStub(int ent, bot_updateentity_t *bue)
{
    assert(g_botImport != NULL);
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

