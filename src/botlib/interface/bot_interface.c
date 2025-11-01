#include <assert.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <stdarg.h>
#include <string.h>
#include <ctype.h>

#include "../../q2bridge/botlib.h"
#include "../../q2bridge/bridge.h"
#include "../../q2bridge/update_translator.h"
#include "../common/l_libvar.h"
#include "../common/l_log.h"
#include "../aas/aas_map.h"
#include "../aas/aas_local.h"
#include "../aas/aas_debug.h"
#include "../ai/chat/ai_chat.h"
#include "../ai/character/bot_character.h"
#include "../ai/weight/bot_weight.h"
#include "../ai/goal_move_orchestrator.h"
#include "../precomp/l_precomp.h"
#include "botlib_interface.h"
#include "bot_interface.h"
#include "bot_state.h"


static bot_import_t *g_botImport = NULL;
static bot_chatstate_t *g_botInterfaceConsoleChat = NULL;
static botlib_import_table_t g_botInterfaceImportTable;

typedef struct botinterface_asset_list_s
{
    char **entries;
    size_t count;
} botinterface_asset_list_t;

typedef struct botinterface_map_cache_s
{
    char map_name[MAX_FILEPATH];
    botinterface_asset_list_t models;
    botinterface_asset_list_t sounds;
    botinterface_asset_list_t images;
} botinterface_map_cache_t;

typedef struct botinterface_entity_snapshot_s
{
    qboolean valid;
    bot_updateentity_t state;
} botinterface_entity_snapshot_t;

typedef struct botinterface_sound_event_s
{
    vec3_t origin;
    int ent;
    int channel;
    int soundindex;
    float volume;
    float attenuation;
    float timeofs;
} botinterface_sound_event_t;

typedef struct botinterface_pointlight_event_s
{
    vec3_t origin;
    int ent;
    float radius;
    float color[3];
    float time;
    float decay;
} botinterface_pointlight_event_t;

#define BOT_INTERFACE_MAX_SOUND_EVENTS 64
#define BOT_INTERFACE_MAX_POINTLIGHT_EVENTS 32
#define BOT_INTERFACE_MAX_ENTITIES 1024

static botinterface_map_cache_t g_botInterfaceMapCache;
static botinterface_entity_snapshot_t g_botInterfaceEntityCache[BOT_INTERFACE_MAX_ENTITIES];
static botinterface_sound_event_t g_botInterfaceSoundQueue[BOT_INTERFACE_MAX_SOUND_EVENTS];
static size_t g_botInterfaceSoundCount = 0;
static botinterface_pointlight_event_t g_botInterfacePointLightQueue[BOT_INTERFACE_MAX_POINTLIGHT_EVENTS];
static size_t g_botInterfacePointLightCount = 0;
static float g_botInterfaceFrameTime = 0.0f;
static unsigned int g_botInterfaceFrameNumber = 0;
static bool g_botInterfaceDebugDrawEnabled = false;

static int BotInterface_StringCompareIgnoreCase(const char *lhs, const char *rhs)
{
    if (lhs == NULL || rhs == NULL)
    {
        return (lhs == rhs) ? 0 : (lhs != NULL ? 1 : -1);
    }

    while (*lhs != '\0' && *rhs != '\0')
    {
        int diff = tolower((unsigned char)*lhs) - tolower((unsigned char)*rhs);
        if (diff != 0)
        {
            return diff;
        }

        ++lhs;
        ++rhs;
    }

    return tolower((unsigned char)*lhs) - tolower((unsigned char)*rhs);
}

static void BotInterface_FreeAssetList(botinterface_asset_list_t *list)
{
    if (list == NULL)
    {
        return;
    }

    if (list->entries != NULL)
    {
        for (size_t index = 0; index < list->count; ++index)
        {
            free(list->entries[index]);
        }

        free(list->entries);
    }

    list->entries = NULL;
    list->count = 0;
}

static bool BotInterface_CopyAssetList(botinterface_asset_list_t *target,
                                       int count,
                                       char *source[])
{
    if (target == NULL)
    {
        return false;
    }

    BotInterface_FreeAssetList(target);

    if (count <= 0 || source == NULL)
    {
        target->entries = NULL;
        target->count = 0;
        return true;
    }

    size_t allocation = (size_t)count;
    char **table = (char **)calloc(allocation, sizeof(char *));
    if (table == NULL)
    {
        return false;
    }

    for (size_t index = 0; index < allocation; ++index)
    {
        if (source[index] == NULL)
        {
            continue;
        }

        table[index] = strdup(source[index]);
        if (table[index] == NULL)
        {
            for (size_t rollback = 0; rollback < index; ++rollback)
            {
                free(table[rollback]);
            }

            free(table);
            return false;
        }
    }

    target->entries = table;
    target->count = allocation;
    return true;
}

static void BotInterface_ResetEntityCache(void)
{
    for (size_t index = 0; index < BOT_INTERFACE_MAX_ENTITIES; ++index)
    {
        g_botInterfaceEntityCache[index].valid = qfalse;
    }
}

static void BotInterface_ResetMapCache(void)
{
    BotInterface_FreeAssetList(&g_botInterfaceMapCache.models);
    BotInterface_FreeAssetList(&g_botInterfaceMapCache.sounds);
    BotInterface_FreeAssetList(&g_botInterfaceMapCache.images);
    g_botInterfaceMapCache.map_name[0] = '\0';
}

static bool BotInterface_RecordMapAssets(const char *mapname,
                                         int modelindexes,
                                         char *modelindex[],
                                         int soundindexes,
                                         char *soundindex[],
                                         int imageindexes,
                                         char *imageindex[])
{
    if (mapname != NULL)
    {
        strncpy(g_botInterfaceMapCache.map_name,
                mapname,
                sizeof(g_botInterfaceMapCache.map_name) - 1);
        g_botInterfaceMapCache.map_name[sizeof(g_botInterfaceMapCache.map_name) - 1] = '\0';
    }
    else
    {
        g_botInterfaceMapCache.map_name[0] = '\0';
    }

    if (!BotInterface_CopyAssetList(&g_botInterfaceMapCache.models, modelindexes, modelindex))
    {
        return false;
    }

    if (!BotInterface_CopyAssetList(&g_botInterfaceMapCache.sounds, soundindexes, soundindex))
    {
        return false;
    }

    if (!BotInterface_CopyAssetList(&g_botInterfaceMapCache.images, imageindexes, imageindex))
    {
        return false;
    }

    return true;
}

static void BotInterface_ResetFrameQueues(void)
{
    g_botInterfaceSoundCount = 0;
    g_botInterfacePointLightCount = 0;
}

static void BotInterface_BeginFrame(float time)
{
    g_botInterfaceFrameTime = time;
    g_botInterfaceFrameNumber += 1U;
    BotInterface_ResetFrameQueues();
}

static void BotInterface_EnqueueSound(const vec3_t origin,
                                      int ent,
                                      int channel,
                                      int soundindex,
                                      float volume,
                                      float attenuation,
                                      float timeofs)
{
    if (g_botInterfaceSoundCount == BOT_INTERFACE_MAX_SOUND_EVENTS)
    {
        memmove(&g_botInterfaceSoundQueue[0],
                &g_botInterfaceSoundQueue[1],
                (BOT_INTERFACE_MAX_SOUND_EVENTS - 1) * sizeof(g_botInterfaceSoundQueue[0]));
        g_botInterfaceSoundCount -= 1U;
    }

    botinterface_sound_event_t *slot = &g_botInterfaceSoundQueue[g_botInterfaceSoundCount++];
    if (origin != NULL)
    {
        VectorCopy(origin, slot->origin);
    }
    else
    {
        VectorClear(slot->origin);
    }

    slot->ent = ent;
    slot->channel = channel;
    slot->soundindex = soundindex;
    slot->volume = volume;
    slot->attenuation = attenuation;
    slot->timeofs = timeofs;
}

static void BotInterface_EnqueuePointLight(const vec3_t origin,
                                           int ent,
                                           float radius,
                                           float r,
                                           float g,
                                           float b,
                                           float time,
                                           float decay)
{
    if (g_botInterfacePointLightCount == BOT_INTERFACE_MAX_POINTLIGHT_EVENTS)
    {
        memmove(&g_botInterfacePointLightQueue[0],
                &g_botInterfacePointLightQueue[1],
                (BOT_INTERFACE_MAX_POINTLIGHT_EVENTS - 1) * sizeof(g_botInterfacePointLightQueue[0]));
        g_botInterfacePointLightCount -= 1U;
    }

    botinterface_pointlight_event_t *slot =
        &g_botInterfacePointLightQueue[g_botInterfacePointLightCount++];

    if (origin != NULL)
    {
        VectorCopy(origin, slot->origin);
    }
    else
    {
        VectorClear(slot->origin);
    }

    slot->ent = ent;
    slot->radius = radius;
    slot->color[0] = r;
    slot->color[1] = g;
    slot->color[2] = b;
    slot->time = time;
    slot->decay = decay;
}

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
    {
        BotFreeChatState(g_botInterfaceConsoleChat);
        g_botInterfaceConsoleChat = NULL;
    }

    BotInterface_ResetMapCache();
    BotInterface_ResetEntityCache();
    BotInterface_ResetFrameQueues();
    g_botInterfaceDebugDrawEnabled = false;

    return result;
}

static int BotInterface_BotSetupLibrary(void)
{
    assert(g_botImport != NULL);
    return BotSetupLibrary();
}

static int BotInterface_BotShutdownLibrary(void)
{
    assert(g_botImport != NULL);

    int status = BotShutdownLibrary();
    if (g_botInterfaceConsoleChat != NULL)
    {
        BotFreeChatState(g_botInterfaceConsoleChat);
        g_botInterfaceConsoleChat = NULL;
    }

    BotInterface_ResetMapCache();
    BotInterface_ResetEntityCache();
    BotInterface_ResetFrameQueues();
    g_botInterfaceDebugDrawEnabled = false;

    AAS_Shutdown();
    BotInterface_FreeImportCache();
    BotInterface_SetImportTable(NULL);
    Q2Bridge_ClearImportTable();
    BotLib_LogShutdown();

    return status;
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

static int BotLoadMap(char *mapname,
                      int modelindexes,
                      char *modelindex[],
                      int soundindexes,
                      char *soundindex[],
                      int imageindexes,
                      char *imageindex[])
{
    if (g_botImport == NULL)
    {
        return BLERR_LIBRARYNOTSETUP;
    }

    if (!BotLibraryInitialized())
    {
        BotInterface_Printf(PRT_ERROR, "[bot_interface] BotLoadMap: library not initialised\n");
        return BLERR_LIBRARYNOTSETUP;
    }

    if (mapname == NULL || *mapname == '\0')
    {
        BotInterface_Printf(PRT_ERROR, "[bot_interface] BotLoadMap: no map specified\n");
        return BLERR_NOAASFILE;
    }

    Bridge_ResetCachedUpdates();
    BotInterface_ResetFrameQueues();
    BotInterface_ResetEntityCache();
    BotInterface_ResetMapCache();

    int status = AAS_LoadMap(mapname,
                             modelindexes,
                             modelindex,
                             soundindexes,
                             soundindex,
                             imageindexes,
                             imageindex);
    if (status != BLERR_NOERROR)
    {
        return status;
    }

    if (!BotInterface_RecordMapAssets(mapname,
                                      modelindexes,
                                      modelindex,
                                      soundindexes,
                                      soundindex,
                                      imageindexes,
                                      imageindex))
    {
        BotInterface_Printf(PRT_WARNING,
                             "[bot_interface] BotLoadMap: failed to record asset lists for %s\n",
                             mapname);
        return BLERR_INVALIDIMPORT;
    }

    return BLERR_NOERROR;
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

    BotState_AttachCharacter(state, profile);

    state->goal_state = AI_GoalState_Create();
    if (state->goal_state == NULL)
    {
        BotInterface_Printf(PRT_ERROR,
                            "[bot_interface] BotSetupClient: failed to allocate goal state for client %d\n",
                            client);
        BotState_Destroy(client);
        return BLERR_INVALIDIMPORT;
    }

    state->move_state = AI_MoveState_Create();
    if (state->move_state == NULL)
    {
        BotInterface_Printf(PRT_ERROR,
                            "[bot_interface] BotSetupClient: failed to allocate move state for client %d\n",
                            client);
        AI_GoalState_Destroy(state->goal_state);
        state->goal_state = NULL;
        BotState_Destroy(client);
        return BLERR_INVALIDIMPORT;
    }

    AI_MoveState_LinkAvoidList(state->move_state, AI_GoalState_GetAvoidList(state->goal_state));

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

static int BotStartFrame(float time)
{
    if (g_botImport == NULL)
    {
        return BLERR_LIBRARYNOTSETUP;
    }

    if (!BotLibraryInitialized())
    {
        BotInterface_Printf(PRT_ERROR, "[bot_interface] BotStartFrame: library not initialised\n");
        return BLERR_LIBRARYNOTSETUP;
    }

    BotInterface_BeginFrame(time);
    aasworld.time = time;
    aasworld.numFrames += 1;

    return BLERR_NOERROR;
}

static int BotUpdateClient(int client, bot_updateclient_t *buc)
{
    if (g_botImport == NULL)
    {
        return BLERR_LIBRARYNOTSETUP;
    }

    if (!BotLibraryInitialized())
    {
        BotInterface_Printf(PRT_ERROR, "[bot_interface] BotUpdateClient: library not initialised\n");
        return BLERR_LIBRARYNOTSETUP;
    }

    bot_client_state_t *state = BotState_Get(client);
    if (state == NULL || !state->active)
    {
        BotInterface_Printf(PRT_WARNING, "[bot_interface] BotUpdateClient: client %d inactive\n", client);
        return BLERR_AIUPDATEINACTIVECLIENT;
    }

    int status = Bridge_UpdateClient(client, buc);
    if (status != BLERR_NOERROR)
    {
        return status;
    }

    if (buc != NULL)
    {
        memcpy(&state->last_client_update, buc, sizeof(*buc));
        state->client_update_valid = true;
        state->last_update_time = g_botInterfaceFrameTime;

        if (state->goal_state != NULL)
        {
            status = AI_GoalState_RecordClientUpdate(state->goal_state, buc);
            if (status != BLERR_NOERROR)
            {
                return status;
            }
        }
    }

    return BLERR_NOERROR;
}

static int BotUpdateEntity(int ent, bot_updateentity_t *bue)
{
    if (g_botImport == NULL)
    {
        return BLERR_LIBRARYNOTSETUP;
    }

    if (!BotLibraryInitialized())
    {
        BotInterface_Printf(PRT_ERROR, "[bot_interface] BotUpdateEntity: library not initialised\n");
        return BLERR_LIBRARYNOTSETUP;
    }

    int status = Bridge_UpdateEntity(ent, bue);
    if (status != BLERR_NOERROR)
    {
        return status;
    }

    status = AAS_UpdateEntity(ent, bue);
    if (status != BLERR_NOERROR)
    {
        return status;
    }

    if (bue != NULL && ent >= 0 && ent < BOT_INTERFACE_MAX_ENTITIES)
    {
        g_botInterfaceEntityCache[ent].state = *bue;
        g_botInterfaceEntityCache[ent].valid = qtrue;
    }

    aasworld.entitiesValid = qtrue;
    return BLERR_NOERROR;
}

static int BotAddSound(vec3_t origin,
                       int ent,
                       int channel,
                       int soundindex,
                       float volume,
                       float attenuation,
                       float timeofs)
{
    if (g_botImport == NULL)
    {
        return BLERR_LIBRARYNOTSETUP;
    }

    if (!BotLibraryInitialized())
    {
        BotInterface_Printf(PRT_ERROR, "[bot_interface] BotAddSound: library not initialised\n");
        return BLERR_LIBRARYNOTSETUP;
    }

    if (soundindex < 0 || (size_t)soundindex >= g_botInterfaceMapCache.sounds.count)
    {
        BotInterface_Printf(PRT_ERROR,
                             "[bot_interface] BotAddSound: invalid sound index %d (count %zu)\n",
                             soundindex,
                             g_botInterfaceMapCache.sounds.count);
        return BLERR_INVALIDSOUNDINDEX;
    }

    BotInterface_EnqueueSound(origin, ent, channel, soundindex, volume, attenuation, timeofs);
    return BLERR_NOERROR;
}

static int BotAddPointLight(vec3_t origin,
                            int ent,
                            float radius,
                            float r,
                            float g,
                            float b,
                            float time,
                            float decay)
{
    if (g_botImport == NULL)
    {
        return BLERR_LIBRARYNOTSETUP;
    }

    if (!BotLibraryInitialized())
    {
        BotInterface_Printf(PRT_ERROR, "[bot_interface] BotAddPointLight: library not initialised\n");
        return BLERR_LIBRARYNOTSETUP;
    }

    BotInterface_EnqueuePointLight(origin, ent, radius, r, g, b, time, decay);
    return BLERR_NOERROR;
}

static int BotAI_Think(bot_client_state_t *state, float thinktime)
{
    if (state == NULL)
    {
        return BLERR_AIUPDATEINACTIVECLIENT;
    }

    if (!state->client_update_valid)
    {
        BotInterface_Printf(PRT_WARNING,
                             "[bot_interface] BotAI: no snapshot for client %d\n",
                             state->client_number);
        return BLERR_AIUPDATEINACTIVECLIENT;
    }

    if (state->goal_state == NULL || state->move_state == NULL)
    {
        return BLERR_INVALIDIMPORT;
    }

    ai_goal_selection_t selection = {0};
    int status = AI_GoalOrchestrator_Refresh(state->goal_state, g_botInterfaceFrameTime, &selection);
    if (status != BLERR_NOERROR)
    {
        return status;
    }

    bot_input_t input = {0};
    status = AI_MoveOrchestrator_Dispatch(state->move_state, &selection, &input);
    if (status != BLERR_NOERROR)
    {
        return status;
    }

    input.thinktime = thinktime;
    VectorCopy(state->last_client_update.viewangles, input.viewangles);

    status = AI_MoveOrchestrator_Submit(state->move_state, state->client_number, &input);
    if (status != BLERR_NOERROR)
    {
        return status;
    }
    state->client_update_valid = false;
    return BLERR_NOERROR;
}

static int BotAI(int client, float thinktime)
{
    if (g_botImport == NULL)
    {
        return BLERR_LIBRARYNOTSETUP;
    }

    if (!BotLibraryInitialized())
    {
        BotInterface_Printf(PRT_ERROR, "[bot_interface] BotAI: library not initialised\n");
        return BLERR_LIBRARYNOTSETUP;
    }

    bot_client_state_t *state = BotState_Get(client);
    if (state == NULL || !state->active)
    {
        BotInterface_Printf(PRT_WARNING, "[bot_interface] BotAI: client %d inactive\n", client);
        return BLERR_AICLIENTNOTSETUP;
    }

    return BotAI_Think(state, thinktime);
}

static int BotConsoleMessage(int client, int type, char *message)
{
    if (g_botImport == NULL)
    {
        return BLERR_LIBRARYNOTSETUP;
    }

    if (!BotLibraryInitialized())
    {
        BotInterface_Printf(PRT_ERROR, "[bot_interface] BotConsoleMessage: library not initialised\n");
        return BLERR_LIBRARYNOTSETUP;
    }

    bot_client_state_t *state = BotState_Get(client);
    if (state == NULL || !state->active)
    {
        BotInterface_Printf(PRT_WARNING, "[bot_interface] BotConsoleMessage: client %d inactive\n", client);
        return BLERR_AICLIENTNOTSETUP;
    }

    if (state->chat_state == NULL)
    {
        BotInterface_Printf(PRT_WARNING,
                             "[bot_interface] BotConsoleMessage: client %d missing chat state\n",
                             client);
        return BLERR_CANNOTLOADICHAT;
    }

    if (message != NULL)
    {
        BotQueueConsoleMessage(state->chat_state, type, message);
    }

    return BLERR_NOERROR;
}

static int BotInterface_Test(int parm0, char *parm1, vec3_t parm2, vec3_t parm3)
{
    if (g_botImport == NULL)
    {
        return BLERR_LIBRARYNOTSETUP;
    }

    if (!BotLibraryInitialized())
    {
        BotInterface_Printf(PRT_ERROR, "[bot_interface] Test: library not initialised\n");
        return BLERR_LIBRARYNOTSETUP;
    }

    if (parm1 == NULL || *parm1 == '\0')
    {
        BotInterface_Printf(PRT_MESSAGE,
                             "[bot_interface] Test commands: dump_chat, sounds, pointlights, debug_draw, bot_test, aas_showpath, aas_showareas\n");
        return BLERR_INVALIDIMPORT;
    }

    char command_buffer[256];
    strncpy(command_buffer, parm1, sizeof(command_buffer) - 1);
    command_buffer[sizeof(command_buffer) - 1] = '\0';

    char *arguments = command_buffer;
    while (*arguments != '\0' && !isspace((unsigned char)*arguments))
    {
        ++arguments;
    }

    if (*arguments != '\0')
    {
        *arguments++ = '\0';
        while (isspace((unsigned char)*arguments))
        {
            ++arguments;
        }
    }
    else
    {
        arguments = NULL;
    }

    char *command = command_buffer;

    if (BotInterface_StringCompareIgnoreCase(command, "dump_chat") == 0)
    {
        int client = parm0;
        if (arguments != NULL && *arguments != '\0')
        {
            client = (int)strtol(arguments, NULL, 10);
        }

        if (client < 0 || client >= MAX_CLIENTS)
        {
            BotInterface_Printf(PRT_ERROR,
                                 "[bot_interface] Test dump_chat: client %d out of range\n",
                                 client);
            return BLERR_INVALIDCLIENTNUMBER;
        }

        bot_client_state_t *state = BotState_Get(client);
        if (state == NULL || state->chat_state == NULL)
        {
            BotInterface_Printf(PRT_WARNING,
                                 "[bot_interface] Test dump_chat: client %d has no chat state\n",
                                 client);
            return BLERR_AICLIENTNOTSETUP;
        }

        size_t pending = BotNumConsoleMessages(state->chat_state);
        BotInterface_Printf(PRT_MESSAGE,
                             "[bot_interface] Test dump_chat: %zu pending messages for client %d\n",
                             pending,
                             client);

        if (pending == 0)
        {
            return BLERR_NOERROR;
        }

        typedef struct botinterface_test_message_s
        {
            int type;
            char text[256];
        } botinterface_test_message_t;

        botinterface_test_message_t *messages =
            (botinterface_test_message_t *)calloc(pending, sizeof(botinterface_test_message_t));
        if (messages == NULL)
        {
            BotInterface_Printf(PRT_ERROR, "[bot_interface] Test dump_chat: allocation failed\n");
            return BLERR_INVALIDIMPORT;
        }

        size_t captured = 0;
        for (size_t index = 0; index < pending; ++index)
        {
            int type = 0;
            char text[256];
            if (BotNextConsoleMessage(state->chat_state, &type, text, sizeof(text)))
            {
                BotInterface_Printf(PRT_MESSAGE,
                                     "[bot_interface] chat[%d]: (%d) %s\n",
                                     client,
                                     type,
                                     text);
                messages[captured].type = type;
                strncpy(messages[captured].text, text, sizeof(messages[captured].text) - 1);
                messages[captured].text[sizeof(messages[captured].text) - 1] = '\0';
                captured += 1U;
            }
        }

        for (size_t index = 0; index < captured; ++index)
        {
            BotQueueConsoleMessage(state->chat_state,
                                   messages[index].type,
                                   messages[index].text);
        }

        free(messages);
        return BLERR_NOERROR;
    }

    if (BotInterface_StringCompareIgnoreCase(command, "sounds") == 0)
    {
        BotInterface_Printf(PRT_MESSAGE,
                             "[bot_interface] Test sounds: %zu queued (%zu assets)\n",
                             g_botInterfaceSoundCount,
                             g_botInterfaceMapCache.sounds.count);

        for (size_t index = 0; index < g_botInterfaceSoundCount; ++index)
        {
            const botinterface_sound_event_t *event = &g_botInterfaceSoundQueue[index];
            const char *asset = (event->soundindex >= 0 &&
                                 (size_t)event->soundindex < g_botInterfaceMapCache.sounds.count &&
                                 g_botInterfaceMapCache.sounds.entries != NULL)
                                    ? g_botInterfaceMapCache.sounds.entries[event->soundindex]
                                    : "<unknown>";

            BotInterface_Printf(PRT_MESSAGE,
                                 "[bot_interface] sound[%zu]: ent=%d channel=%d index=%d asset=%s\n",
                                 index,
                                 event->ent,
                                 event->channel,
                                 event->soundindex,
                                 asset);
        }

        return BLERR_NOERROR;
    }

    if (BotInterface_StringCompareIgnoreCase(command, "pointlights") == 0)
    {
        BotInterface_Printf(PRT_MESSAGE,
                             "[bot_interface] Test pointlights: %zu queued\n",
                             g_botInterfacePointLightCount);

        for (size_t index = 0; index < g_botInterfacePointLightCount; ++index)
        {
            const botinterface_pointlight_event_t *event = &g_botInterfacePointLightQueue[index];
            BotInterface_Printf(PRT_MESSAGE,
                                 "[bot_interface] light[%zu]: ent=%d origin=(%.1f %.1f %.1f) radius=%.1f color=(%.2f %.2f %.2f)\n",
                                 index,
                                 event->ent,
                                 event->origin[0],
                                 event->origin[1],
                                 event->origin[2],
                                 event->radius,
                                 event->color[0],
                                 event->color[1],
                                 event->color[2]);
        }

        return BLERR_NOERROR;
    }

    if (BotInterface_StringCompareIgnoreCase(command, "bot_test") == 0)
    {
        AAS_DebugBotTest(parm0, arguments, parm2, parm3);
        return BLERR_NOERROR;
    }

    if (BotInterface_StringCompareIgnoreCase(command, "aas_showpath") == 0)
    {
        int startArea = 0;
        int goalArea = 0;

        if (arguments != NULL && *arguments != '\0')
        {
            char *cursor = arguments;
            char *endptr = NULL;
            startArea = (int)strtol(cursor, &endptr, 10);
            if (endptr != cursor)
            {
                cursor = endptr;
                while (*cursor != '\0' && (isspace((unsigned char)*cursor) || *cursor == ','))
                {
                    ++cursor;
                }

                if (*cursor != '\0')
                {
                    goalArea = (int)strtol(cursor, &endptr, 10);
                    if (endptr != cursor)
                    {
                        cursor = endptr;
                    }
                }
            }
        }

        AAS_DebugShowPath(startArea, goalArea, parm2, parm3);
        return BLERR_NOERROR;
    }

    if (BotInterface_StringCompareIgnoreCase(command, "aas_showareas") == 0)
    {
        int areaBuffer[32];
        size_t areaCount = 0U;

        if (arguments != NULL && *arguments != '\0')
        {
            char *cursor = arguments;
            while (*cursor != '\0' && areaCount < (sizeof(areaBuffer) / sizeof(areaBuffer[0])))
            {
                while (*cursor != '\0' && (isspace((unsigned char)*cursor) || *cursor == ','))
                {
                    ++cursor;
                }

                if (*cursor == '\0')
                {
                    break;
                }

                char *endptr = NULL;
                long value = strtol(cursor, &endptr, 10);
                if (endptr == cursor)
                {
                    ++cursor;
                    continue;
                }

                areaBuffer[areaCount++] = (int)value;
                cursor = endptr;
            }
        }

        AAS_DebugShowAreas((areaCount > 0U) ? areaBuffer : NULL, areaCount);
        return BLERR_NOERROR;
    }

    if (BotInterface_StringCompareIgnoreCase(command, "debug_draw") == 0)
    {
        if (arguments != NULL && *arguments != '\0')
        {
            if (BotInterface_StringCompareIgnoreCase(arguments, "on") == 0)
            {
                g_botInterfaceDebugDrawEnabled = true;
            }
            else if (BotInterface_StringCompareIgnoreCase(arguments, "off") == 0)
            {
                g_botInterfaceDebugDrawEnabled = false;
            }
            else
            {
                g_botInterfaceDebugDrawEnabled = (strtol(arguments, NULL, 10) != 0);
            }
        }
        else
        {
            g_botInterfaceDebugDrawEnabled = !g_botInterfaceDebugDrawEnabled;
        }

        BotInterface_Printf(PRT_MESSAGE,
                             "[bot_interface] Test debug_draw: %s\n",
                             g_botInterfaceDebugDrawEnabled ? "enabled" : "disabled");
        return BLERR_NOERROR;
    }

    BotInterface_Printf(PRT_WARNING,
                         "[bot_interface] Test: unknown command '%s'\n",
                         command);
    return BLERR_INVALIDIMPORT;
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
    exportTable.BotLoadMap = BotLoadMap;
    exportTable.BotSetupClient = BotSetupClient;
    exportTable.BotShutdownClient = BotShutdownClient;
    exportTable.BotMoveClient = BotMoveClient;
    exportTable.BotClientSettings = BotClientSettings;
    exportTable.BotSettings = BotSettings;
    exportTable.BotStartFrame = BotStartFrame;
    exportTable.BotUpdateClient = BotUpdateClient;
    exportTable.BotUpdateEntity = BotUpdateEntity;
    exportTable.BotAddSound = BotAddSound;
    exportTable.BotAddPointLight = BotAddPointLight;
    exportTable.BotAI = BotAI;
    exportTable.BotConsoleMessage = BotConsoleMessage;
    exportTable.Test = BotInterface_Test;

    return &exportTable;
}

