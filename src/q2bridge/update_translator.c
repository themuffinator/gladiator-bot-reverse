#include "q2bridge/update_translator.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "q2bridge/aas_translation.h"
#include "q2bridge/bridge.h"
#include "q2bridge/bridge_config.h"

#include "botlib_common/l_libvar.h"

static void Bridge_LogMessage(int priority, const char *fmt, ...);

#define BRIDGE_DEFAULT_MAX_ENTITIES 1024
#define Q2_ABSOLUTE_MAX_ENTITIES 2048

typedef struct bridge_client_slot_s
{
    qboolean active;
    qboolean seen;
    qboolean logged;
    bot_updateclient_t snapshot;
    qboolean frame_valid;
    AASClientFrame frame;
} bridge_client_slot_t;

typedef struct bridge_entity_slot_s
{
    qboolean seen;
    qboolean logged;
    bot_updateentity_t snapshot;
    qboolean frame_valid;
    AASEntityFrame frame;
} bridge_entity_slot_t;

static bridge_client_slot_t g_bridge_clients[MAX_CLIENTS];
static bridge_entity_slot_t *g_bridge_entities = NULL;
static int g_bridge_entity_capacity = 0;
static int g_bridge_max_client_index = MAX_CLIENTS - 1;
static int g_bridge_max_entity_index = -1;
static float g_bridge_frame_time = 0.0f;

static const char *Bridge_GetMaxClientsVarName(const libvar_t *var)
{
    if (var != NULL && var->name != NULL && var->name[0] != '\0')
    {
        return var->name;
    }

    return "maxclients";
}

static void Bridge_UpdateMaxClientIndex(qboolean force_refresh)
{
    const int fallback_clients = MAX_CLIENTS;
    int max_clients = fallback_clients;

    libvar_t *maxclients = Bridge_MaxClients();
    if (maxclients != NULL)
    {
        const char *var_name = Bridge_GetMaxClientsVarName(maxclients);

        if (force_refresh || maxclients->modified)
        {
            max_clients = (int)LibVarValue(var_name, "0");
            LibVarSetNotModified(var_name);
        }
        else
        {
            max_clients = (int)maxclients->value;
        }

        if (max_clients <= 0)
        {
            max_clients = fallback_clients;
        }
    }
    else
    {
        int fetched = (int)LibVarValue(Bridge_GetMaxClientsVarName(NULL), "0");
        if (fetched > 0)
        {
            max_clients = fetched;
        }
    }

    if (max_clients > fallback_clients)
    {
        max_clients = fallback_clients;
    }

    g_bridge_max_client_index = max_clients - 1;
}

static int Bridge_ReadConfiguredMaxEntities(void)
{
    int configured = 0;

    const libvar_t *maxentities = Bridge_MaxEntities();
    if (maxentities != NULL && maxentities->name != NULL)
    {
        configured = (int)LibVarValue(maxentities->name, "0");
    }
    else
    {
        configured = (int)LibVarValue("maxentities", "0");
    }

    if (configured <= 0)
    {
        configured = BRIDGE_DEFAULT_MAX_ENTITIES;
    }
    if (configured > Q2_ABSOLUTE_MAX_ENTITIES)
    {
        configured = Q2_ABSOLUTE_MAX_ENTITIES;
    }

    return configured;
}

static qboolean Bridge_GrowEntityTable(int new_capacity)
{
    if (new_capacity <= g_bridge_entity_capacity)
    {
        return qtrue;
    }

    size_t new_size = (size_t)new_capacity * sizeof(*g_bridge_entities);
    bridge_entity_slot_t *resized =
        (bridge_entity_slot_t *)realloc(g_bridge_entities, new_size);
    if (resized == NULL)
    {
        Bridge_LogMessage(PRT_ERROR,
                          "[q2bridge] failed to grow entity cache to %d slots\n",
                          new_capacity);
        return qfalse;
    }

    size_t old_size = (size_t)g_bridge_entity_capacity * sizeof(*resized);
    if (new_size > old_size)
    {
        memset((unsigned char *)resized + old_size, 0, new_size - old_size);
    }

    g_bridge_entities = resized;
    g_bridge_entity_capacity = new_capacity;
    return qtrue;
}

static void Bridge_ResetEntityTable(void)
{
    int configured = Bridge_ReadConfiguredMaxEntities();

    if (configured != g_bridge_entity_capacity)
    {
        bridge_entity_slot_t *fresh = NULL;
        if (configured > 0)
        {
            fresh = (bridge_entity_slot_t *)calloc((size_t)configured, sizeof(*fresh));
            if (fresh == NULL)
            {
                Bridge_LogMessage(PRT_ERROR,
                                  "[q2bridge] failed to allocate entity cache for %d slots\n",
                                  configured);
                configured = g_bridge_entity_capacity;
            }
            else
            {
                free(g_bridge_entities);
                g_bridge_entities = fresh;
                g_bridge_entity_capacity = configured;
            }
        }
        else
        {
            free(g_bridge_entities);
            g_bridge_entities = NULL;
            g_bridge_entity_capacity = 0;
        }
    }
    else if (g_bridge_entities != NULL)
    {
        memset(g_bridge_entities, 0, (size_t)g_bridge_entity_capacity * sizeof(*g_bridge_entities));
    }

    if (configured > g_bridge_entity_capacity)
    {
        configured = g_bridge_entity_capacity;
    }

    g_bridge_max_entity_index = (configured > 0) ? configured - 1 : -1;
}

static void Bridge_SynchroniseEntityLimit(void)
{
    int configured = Bridge_ReadConfiguredMaxEntities();
    if (configured > g_bridge_entity_capacity)
    {
        if (!Bridge_GrowEntityTable(configured))
        {
            configured = g_bridge_entity_capacity;
        }
        else
        {
            g_bridge_entity_capacity = configured;
        }
    }

    if (configured > g_bridge_entity_capacity)
    {
        configured = g_bridge_entity_capacity;
    }

    g_bridge_max_entity_index = (configured > 0) ? configured - 1 : -1;
}

static void Bridge_LogMessage(int priority, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    bot_import_t *imports = Q2Bridge_GetImportTable();
    if (imports != NULL && imports->Print != NULL)
    {
        va_list args_copy;
        va_copy(args_copy, args);

        char buffer[1024];
        vsnprintf(buffer, sizeof(buffer), fmt, args_copy);
        va_end(args_copy);

        imports->Print(priority, "%s", buffer);
    }
    else
    {
        vfprintf(stderr, fmt, args);
    }

    va_end(args);
}

static qboolean Bridge_CheckLibraryReady(const char *caller)
{
    if (Q2Bridge_GetImportTable() == NULL)
    {
        Bridge_LogMessage(PRT_ERROR, "%s: bot library used before being setup\n", caller);
        return qfalse;
    }

    return qtrue;
}

static qboolean Bridge_CheckClientNumber(int client, const char *caller)
{
    Bridge_UpdateMaxClientIndex(qfalse);

    if (client < 0 || client > g_bridge_max_client_index)
    {
        Bridge_LogMessage(PRT_ERROR,
                          "%s: invalid client number %d, [0, %d]\n",
                          caller,
                          client,
                          g_bridge_max_client_index);
        return qfalse;
    }

    return qtrue;
}

static qboolean Bridge_CheckEntityNumber(int ent, const char *caller)
{
    Bridge_SynchroniseEntityLimit();

    if (ent < 0 || ent > g_bridge_max_entity_index)
    {
        Bridge_LogMessage(PRT_ERROR,
                          "%s: invalid entity number %d, [0, %d]\n",
                          caller,
                          ent,
                          g_bridge_max_entity_index);
        return qfalse;
    }

    return qtrue;
}

static void Bridge_LogFirstCapture(qboolean *logged, const char *fmt, ...)
{
    if (*logged)
    {
        return;
    }

    va_list args;
    va_start(args, fmt);

    bot_import_t *imports = Q2Bridge_GetImportTable();
    if (imports != NULL && imports->Print != NULL)
    {
        char buffer[512];
        vsnprintf(buffer, sizeof(buffer), fmt, args);
        imports->Print(PRT_MESSAGE, "%s", buffer);
    }
    else
    {
        vfprintf(stderr, fmt, args);
    }

    va_end(args);

    *logged = qtrue;
}

int Bridge_UpdateClient(int client, const bot_updateclient_t *update)
{
    if (!Bridge_CheckLibraryReady("BotUpdateClient"))
    {
        return BLERR_LIBRARYNOTSETUP;
    }

    if (!Bridge_CheckClientNumber(client, "BotUpdateClient"))
    {
        return BLERR_INVALIDCLIENTNUMBER;
    }

    if (update == NULL)
    {
        Bridge_LogMessage(PRT_ERROR, "BotUpdateClient: NULL update payload provided\n");
        return BLERR_AIUPDATEINACTIVECLIENT;
    }

    bridge_client_slot_t *slot = &g_bridge_clients[client];
    if (!slot->active)
    {
        BotlibLog(PRT_WARNING, "tried to updated inactive bot client\n");
        return BLERR_AIUPDATEINACTIVECLIENT;
    }

    memcpy(&slot->snapshot, update, sizeof(*update));

    bot_status_t status = TranslateClientUpdate(client, update, g_bridge_frame_time, &slot->frame);
    if (status != BLERR_NOERROR)
    {
        return status;
    }

    slot->frame_valid = qtrue;
    slot->seen = qtrue;

    Bridge_LogFirstCapture(&slot->logged,
                           "[q2bridge] captured BotUpdateClient frame for client %d\n",
                           client);

    return BLERR_NOERROR;
}

int Bridge_UpdateEntity(int ent, const bot_updateentity_t *update)
{
    if (!Bridge_CheckLibraryReady("BotUpdateEntity"))
    {
        return BLERR_LIBRARYNOTSETUP;
    }

    if (!Bridge_CheckEntityNumber(ent, "BotUpdateEntity"))
    {
        return BLERR_INVALIDENTITYNUMBER;
    }

    if (update == NULL)
    {
        Bridge_LogMessage(PRT_ERROR, "BotUpdateEntity: NULL update payload provided\n");
        return BLERR_INVALIDENTITYNUMBER;
    }

    if (g_bridge_entities == NULL)
    {
        Bridge_LogMessage(PRT_ERROR,
                          "BotUpdateEntity: entity cache unavailable for index %d\n",
                          ent);
        return BLERR_INVALIDENTITYNUMBER;
    }

    bridge_entity_slot_t *slot = &g_bridge_entities[ent];
    memcpy(&slot->snapshot, update, sizeof(*update));

    slot->frame_valid = qfalse;
    bot_status_t status = TranslateEntityUpdate(ent, update, &slot->frame);
    if (status != BLERR_NOERROR)
    {
        return status;
    }

    slot->frame_valid = qtrue;
    slot->seen = qtrue;

    Bridge_LogFirstCapture(&slot->logged,
                           "[q2bridge] captured BotUpdateEntity frame for ent %d\n",
                           ent);

    return BLERR_NOERROR;
}

void Bridge_ResetCachedUpdates(void)
{
    memset(g_bridge_clients, 0, sizeof(g_bridge_clients));
    Bridge_ResetEntityTable();
    g_bridge_frame_time = 0.0f;

    Bridge_UpdateMaxClientIndex(qtrue);
}

void Bridge_ClearClientSlot(int client)
{
    if (!Bridge_CheckClientNumber(client, "Bridge_ClearClientSlot"))
    {
        return;
    }

    memset(&g_bridge_clients[client], 0, sizeof(g_bridge_clients[client]));
}

int Bridge_MoveClientSlot(int old_client, int new_client)
{
    if (!Bridge_CheckLibraryReady("BotMoveClient"))
    {
        return BLERR_LIBRARYNOTSETUP;
    }

    if (!Bridge_CheckClientNumber(old_client, "BotMoveClient"))
    {
        return BLERR_INVALIDCLIENTNUMBER;
    }

    if (!Bridge_CheckClientNumber(new_client, "BotMoveClient"))
    {
        return BLERR_INVALIDCLIENTNUMBER;
    }

    if (old_client == new_client)
    {
        return BLERR_NOERROR;
    }

    g_bridge_clients[new_client] = g_bridge_clients[old_client];
    memset(&g_bridge_clients[old_client], 0, sizeof(g_bridge_clients[old_client]));

    return BLERR_NOERROR;
}

void Bridge_SetFrameTime(float time)
{
    g_bridge_frame_time = time;
    TranslateEntity_SetCurrentTime(time);
}

void Bridge_SetClientActive(int client, qboolean active)
{
    Bridge_UpdateMaxClientIndex(qfalse);

    if (!Bridge_CheckClientNumber(client, "Bridge_SetClientActive"))
    {
        return;
    }

    g_bridge_clients[client].active = active;
    if (!active)
    {
        g_bridge_clients[client].frame_valid = qfalse;
        memset(&g_bridge_clients[client].frame, 0, sizeof(g_bridge_clients[client].frame));
    }
}

void Bridge_HandleMapStateChange(void)
{
    Bridge_UpdateMaxClientIndex(qtrue);
}

qboolean Bridge_ReadClientFrame(int client, AASClientFrame *frame_out)
{
    if (!Bridge_CheckClientNumber(client, "Bridge_ReadClientFrame"))
    {
        return qfalse;
    }

    bridge_client_slot_t *slot = &g_bridge_clients[client];
    if (!slot->frame_valid || frame_out == NULL)
    {
        return qfalse;
    }

    *frame_out = slot->frame;
    return qtrue;
}

qboolean Bridge_ReadEntityFrame(int ent, AASEntityFrame *frame_out)
{
    if (!Bridge_CheckEntityNumber(ent, "Bridge_ReadEntityFrame"))
    {
        return qfalse;
    }

    if (g_bridge_entities == NULL)
    {
        return qfalse;
    }

    bridge_entity_slot_t *slot = &g_bridge_entities[ent];
    if (!slot->frame_valid || frame_out == NULL)
    {
        return qfalse;
    }

    *frame_out = slot->frame;
    return qtrue;
}
