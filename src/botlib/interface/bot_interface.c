#include <assert.h>
#include <stddef.h>

#include <stdarg.h>
#include <string.h>

#include "../../q2bridge/botlib.h"
#include "../../q2bridge/bridge.h"
#include "../../q2bridge/update_translator.h"
#include "../aas/aas_map.h"
#include "../ai/chat/ai_chat.h"
#include "../ai/character/bot_character.h"
#include "../ai/weight/bot_weight.h"
#include "bot_interface.h"
#include "bot_state.h"


static int g_bot_initialized = 0;

static bot_import_t *g_botImport = NULL;
static bot_chatstate_t *g_botInterfaceConsoleChat = NULL;

static void BotInterface_Printf(int priority, const char *fmt, ...)
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
    BotState_ShutdownAll();
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

static int BotSetupClient(int client, bot_settings_t *settings)
{
    if (g_botImport == NULL)
    {
        return BLERR_LIBRARYNOTSETUP;
    }

    if (client < 0 || client >= MAX_CLIENTS)
    {
        BotInterface_Printf(PRT_ERROR, "[bot_interface] BotSetupClient: invalid client %d\n", client);
        return BLERR_INVALIDCLIENTNUMBER;
    }

    if (settings == NULL)
    {
        BotInterface_Printf(PRT_ERROR, "[bot_interface] BotSetupClient: NULL settings pointer for client %d\n", client);
        return BLERR_INVALIDIMPORT;
    }

    if (BotState_Get(client) != NULL)
    {
        BotInterface_Printf(PRT_WARNING, "[bot_interface] BotSetupClient: client %d already active\n", client);
        return BLERR_AICLIENTALREADYSETUP;
    }

    bot_client_state_t *state = BotState_Create(client);
    if (state == NULL)
    {
        BotInterface_Printf(PRT_ERROR, "[bot_interface] BotSetupClient: failed to allocate state for client %d\n", client);
        return BLERR_INVALIDIMPORT;
    }

    memcpy(&state->settings, settings, sizeof(*settings));

    ai_character_profile_t *profile = AI_LoadCharacter(settings->characterfile, 1.0f);
    if (profile == NULL)
    {
        BotInterface_Printf(PRT_ERROR,
                            "[bot_interface] BotSetupClient: failed to load character '%s' for client %d\n",
                            settings->characterfile,
                            client);
        BotState_Destroy(client);
        return BLERR_INVALIDIMPORT;
    }
    state->character = profile;

    const char *display_name = AI_CharacteristicAsString(profile, BOT_CHARACTERISTIC_NAME);
    if (display_name == NULL || *display_name == '\0')
    {
        display_name = settings->charactername;
    }
    if (display_name != NULL)
    {
        strncpy(state->client_settings.netname, display_name, sizeof(state->client_settings.netname) - 1);
        state->client_settings.netname[sizeof(state->client_settings.netname) - 1] = '\0';
    }

    state->client_settings.skin[0] = '\0';

    const char *item_weight_file = AI_CharacteristicAsString(profile, BOT_CHARACTERISTIC_ITEMWEIGHTS);
    if (item_weight_file != NULL && *item_weight_file != '\0')
    {
        bot_weight_config_t *item_weights = ReadWeightConfig(item_weight_file);
        if (item_weights == NULL)
        {
            BotInterface_Printf(PRT_ERROR,
                                "[bot_interface] BotSetupClient: failed to load item weights '%s' for client %d\n",
                                item_weight_file,
                                client);
            BotState_Destroy(client);
            return BLERR_CANNOTLOADITEMWEIGHTS;
        }
        state->item_weights = item_weights;
        profile->item_weights = item_weights;
    }

    const char *weapon_weight_file = AI_CharacteristicAsString(profile, BOT_CHARACTERISTIC_WEAPONWEIGHTS);
    if (weapon_weight_file != NULL && *weapon_weight_file != '\0')
    {
        bot_weight_config_t *weapon_weights = ReadWeightConfig(weapon_weight_file);
        if (weapon_weights == NULL)
        {
            BotInterface_Printf(PRT_ERROR,
                                "[bot_interface] BotSetupClient: failed to load weapon weights '%s' for client %d\n",
                                weapon_weight_file,
                                client);
            BotState_Destroy(client);
            return BLERR_CANNOTLOADWEAPONWEIGHTS;
        }
        state->weapon_weights = weapon_weights;
        profile->weapon_weights = weapon_weights;
    }

    bot_chatstate_t *chat_state = BotAllocChatState();
    if (chat_state == NULL)
    {
        BotInterface_Printf(PRT_ERROR,
                            "[bot_interface] BotSetupClient: failed to allocate chat state for client %d\n",
                            client);
        BotState_Destroy(client);
        return BLERR_CANNOTLOADICHAT;
    }

    state->chat_state = chat_state;
    profile->chat_state = chat_state;

    const char *chat_file = AI_CharacteristicAsString(profile, BOT_CHARACTERISTIC_CHAT_FILE);
    const char *chat_name = AI_CharacteristicAsString(profile, BOT_CHARACTERISTIC_CHAT_NAME);
    if (chat_file != NULL && *chat_file != '\0')
    {
        const char *resolved_chat_name = (chat_name != NULL && *chat_name != '\0') ? chat_name : "default";
        if (!BotLoadChatFile(chat_state, chat_file, resolved_chat_name))
        {
            BotInterface_Printf(PRT_ERROR,
                                "[bot_interface] BotSetupClient: failed to load chat '%s' (%s) for client %d\n",
                                chat_file,
                                resolved_chat_name,
                                client);
            BotState_Destroy(client);
            return BLERR_CANNOTLOADICHAT;
        }
    }

    Bridge_ClearClientSlot(client);
    state->active = true;
    return BLERR_NOERROR;
}

static int BotShutdownClient(int client)
{
    if (g_botImport == NULL)
    {
        return BLERR_LIBRARYNOTSETUP;
    }

    if (client < 0 || client >= MAX_CLIENTS)
    {
        BotInterface_Printf(PRT_ERROR, "[bot_interface] BotShutdownClient: invalid client %d\n", client);
        return BLERR_INVALIDCLIENTNUMBER;
    }

    bot_client_state_t *state = BotState_Get(client);
    if (state == NULL || !state->active)
    {
        BotInterface_Printf(PRT_WARNING, "[bot_interface] BotShutdownClient: client %d not active\n", client);
        return BLERR_AICLIENTALREADYSHUTDOWN;
    }

    BotState_Destroy(client);
    Bridge_ClearClientSlot(client);
    return BLERR_NOERROR;
}

static int BotMoveClient(int oldclnum, int newclnum)
{
    if (g_botImport == NULL)
    {
        return BLERR_LIBRARYNOTSETUP;
    }

    if (oldclnum < 0 || oldclnum >= MAX_CLIENTS)
    {
        BotInterface_Printf(PRT_ERROR, "[bot_interface] BotMoveClient: invalid source client %d\n", oldclnum);
        return BLERR_INVALIDCLIENTNUMBER;
    }

    if (newclnum < 0 || newclnum >= MAX_CLIENTS)
    {
        BotInterface_Printf(PRT_ERROR, "[bot_interface] BotMoveClient: invalid destination client %d\n", newclnum);
        return BLERR_INVALIDCLIENTNUMBER;
    }

    if (oldclnum == newclnum)
    {
        return BLERR_NOERROR;
    }

    bot_client_state_t *state = BotState_Get(oldclnum);
    if (state == NULL || !state->active)
    {
        BotInterface_Printf(PRT_WARNING, "[bot_interface] BotMoveClient: source client %d inactive\n", oldclnum);
        return BLERR_AIMOVEINACTIVECLIENT;
    }

    if (BotState_Get(newclnum) != NULL)
    {
        BotInterface_Printf(PRT_WARNING, "[bot_interface] BotMoveClient: destination client %d already active\n", newclnum);
        return BLERR_AIMOVETOACTIVECLIENT;
    }

    BotState_Move(oldclnum, newclnum);
    int status = Bridge_MoveClientSlot(oldclnum, newclnum);
    if (status != BLERR_NOERROR)
    {
        BotInterface_Printf(PRT_ERROR,
                            "[bot_interface] BotMoveClient: bridge move failed for %d -> %d\n",
                            oldclnum,
                            newclnum);
        BotState_Move(newclnum, oldclnum);
        return status;
    }

    return BLERR_NOERROR;
}

static int BotClientSettings(int client, bot_clientsettings_t *settings)
{
    if (g_botImport == NULL)
    {
        return BLERR_LIBRARYNOTSETUP;
    }

    if (settings == NULL)
    {
        BotInterface_Printf(PRT_ERROR, "[bot_interface] BotClientSettings: NULL output buffer\n");
        return BLERR_INVALIDIMPORT;
    }

    bot_client_state_t *state = BotState_Get(client);
    if (state == NULL || !state->active)
    {
        BotInterface_Printf(PRT_WARNING, "[bot_interface] BotClientSettings: client %d inactive\n", client);
        memset(settings, 0, sizeof(*settings));
        return BLERR_SETTINGSINACTIVECLIENT;
    }

    memcpy(settings, &state->client_settings, sizeof(*settings));
    return BLERR_NOERROR;
}

static int BotSettings(int client, bot_settings_t *settings)
{
    if (g_botImport == NULL)
    {
        return BLERR_LIBRARYNOTSETUP;
    }

    if (settings == NULL)
    {
        BotInterface_Printf(PRT_ERROR, "[bot_interface] BotSettings: NULL output buffer\n");
        return BLERR_INVALIDIMPORT;
    }

    bot_client_state_t *state = BotState_Get(client);
    if (state == NULL || !state->active)
    {
        BotInterface_Printf(PRT_WARNING, "[bot_interface] BotSettings: client %d inactive\n", client);
        memset(settings, 0, sizeof(*settings));
        return BLERR_AICLIENTNOTSETUP;
    }

    memcpy(settings, &state->settings, sizeof(*settings));
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
    exportTable.BotSetupClient = BotSetupClient;
    exportTable.BotShutdownClient = BotShutdownClient;
    exportTable.BotMoveClient = BotMoveClient;
    exportTable.BotClientSettings = BotClientSettings;
    exportTable.BotSettings = BotSettings;
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

