#include "q2bridge/update_translator.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "q2bridge/aas_translation.h"
#include "q2bridge/bridge.h"

#define BRIDGE_MAX_ENTITIES 1024

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
} bridge_entity_slot_t;

static bridge_client_slot_t g_bridge_clients[MAX_CLIENTS];
static bridge_entity_slot_t g_bridge_entities[BRIDGE_MAX_ENTITIES];
static int g_bridge_max_client_index = MAX_CLIENTS - 1;
static int g_bridge_max_entity_index = BRIDGE_MAX_ENTITIES - 1;
static float g_bridge_frame_time = 0.0f;

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

    bridge_entity_slot_t *slot = &g_bridge_entities[ent];
    memcpy(&slot->snapshot, update, sizeof(*update));
    slot->seen = qtrue;

    Bridge_LogFirstCapture(&slot->logged,
                           "[q2bridge] captured BotUpdateEntity frame for ent %d\n",
                           ent);

    return BLERR_NOERROR;
}

void Bridge_ResetCachedUpdates(void)
{
    memset(g_bridge_clients, 0, sizeof(g_bridge_clients));
    memset(g_bridge_entities, 0, sizeof(g_bridge_entities));
    g_bridge_frame_time = 0.0f;
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
}

void Bridge_SetClientActive(int client, qboolean active)
{
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
