#include "bot_ai.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../shared/q_shared.h"
#include "../common/l_log.h"
#include "chat/ai_chat.h"
#include "goal/bot_goal.h"
#include "move/bot_move.h"
#include "weapon/bot_weapon.h"
#include "weight/bot_weight.h"
#include "q2bridge/bridge.h"
#include "q2bridge/update_translator.h"

#define BOT_AI_MAX_SOUND_EVENTS 64
#define BOT_AI_MAX_POINTLIGHT_EVENTS 32

typedef struct bot_ai_sound_event_s {
    vec3_t origin;
    int ent;
    int channel;
    int soundindex;
    float volume;
    float attenuation;
    float timeofs;
} bot_ai_sound_event_t;

typedef struct bot_ai_pointlight_event_s {
    vec3_t origin;
    int ent;
    float radius;
    float r;
    float g;
    float b;
    float time;
    float decay;
} bot_ai_pointlight_event_t;

typedef struct bot_ai_client_state_s {
    bool active;
    bool debug_draw_enabled;
    int client_num;
    float last_think_time;
    bot_input_t last_input;
    bot_chatstate_t *console_chat;
} bot_ai_client_state_t;

static bot_ai_client_state_t g_bot_ai_clients[MAX_CLIENTS];
static float g_bot_ai_frame_time = 0.0f;
static bot_ai_sound_event_t g_bot_ai_sounds[BOT_AI_MAX_SOUND_EVENTS];
static size_t g_bot_ai_sound_count = 0;
static bot_ai_pointlight_event_t g_bot_ai_pointlights[BOT_AI_MAX_POINTLIGHT_EVENTS];
static size_t g_bot_ai_pointlight_count = 0;

static bool BotAI_ClientIndexValid(int client)
{
    return client >= 0 && client < MAX_CLIENTS;
}

static bot_ai_client_state_t *BotAI_FindClientState(int client)
{
    if (!BotAI_ClientIndexValid(client)) {
        return NULL;
    }

    return &g_bot_ai_clients[client];
}

static bot_ai_client_state_t *BotAI_GetClientState(int client, bool create)
{
    bot_ai_client_state_t *state = BotAI_FindClientState(client);
    if (state == NULL) {
        return NULL;
    }

    if (!state->active && create) {
        memset(state, 0, sizeof(*state));
        state->active = true;
        state->client_num = client;
    }

    return (state->active || create) ? state : NULL;
}

static void BotAI_ClearClientState(bot_ai_client_state_t *state)
{
    if (state == NULL) {
        return;
    }

    if (state->console_chat != NULL) {
        BotFreeChatState(state->console_chat);
        state->console_chat = NULL;
    }

    memset(state, 0, sizeof(*state));
}

void BotAI_ResetForNewMap(void)
{
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        BotAI_ClearClientState(&g_bot_ai_clients[i]);
    }

    g_bot_ai_sound_count = 0;
    g_bot_ai_pointlight_count = 0;
    g_bot_ai_frame_time = 0.0f;
}

void BotAI_BeginFrame(float time)
{
    g_bot_ai_frame_time = time;
    g_bot_ai_sound_count = 0;
    g_bot_ai_pointlight_count = 0;
}

bot_chatstate_t *BotAI_ConsoleState(int client)
{
    bot_ai_client_state_t *state = BotAI_GetClientState(client, true);
    if (state == NULL) {
        return NULL;
    }

    if (state->console_chat == NULL) {
        state->console_chat = BotAllocChatState();
        if (state->console_chat == NULL) {
            BotLib_Print(PRT_ERROR,
                         "[bot_ai] failed to allocate console chat state for client %d\n",
                         client);
            state->active = false;
            return NULL;
        }
    }

    return state->console_chat;
}

void BotAI_QueueConsoleMessage(int client, int type, const char *message)
{
    bot_chatstate_t *chat_state = BotAI_ConsoleState(client);
    if (chat_state == NULL) {
        return;
    }

    BotQueueConsoleMessage(chat_state, type, message);
}

static void BotAI_LogDroppedEvent(const char *event_name)
{
    BotLib_Print(PRT_WARNING,
                 "[bot_ai] dropping %s event due to full queue\n",
                 event_name);
}

int BotAI_AddSoundEvent(const vec3_t origin,
                        int ent,
                        int channel,
                        int soundindex,
                        float volume,
                        float attenuation,
                        float timeofs)
{
    if (g_bot_ai_sound_count >= BOT_AI_MAX_SOUND_EVENTS) {
        BotAI_LogDroppedEvent("sound");
        return BLERR_NOERROR;
    }

    bot_ai_sound_event_t *slot = &g_bot_ai_sounds[g_bot_ai_sound_count++];
    VectorCopy(origin, slot->origin);
    slot->ent = ent;
    slot->channel = channel;
    slot->soundindex = soundindex;
    slot->volume = volume;
    slot->attenuation = attenuation;
    slot->timeofs = timeofs;
    return BLERR_NOERROR;
}

int BotAI_AddPointLightEvent(const vec3_t origin,
                             int ent,
                             float radius,
                             float r,
                             float g,
                             float b,
                             float time,
                             float decay)
{
    if (g_bot_ai_pointlight_count >= BOT_AI_MAX_POINTLIGHT_EVENTS) {
        BotAI_LogDroppedEvent("point light");
        return BLERR_NOERROR;
    }

    bot_ai_pointlight_event_t *slot = &g_bot_ai_pointlights[g_bot_ai_pointlight_count++];
    VectorCopy(origin, slot->origin);
    slot->ent = ent;
    slot->radius = radius;
    slot->r = r;
    slot->g = g;
    slot->b = b;
    slot->time = time;
    slot->decay = decay;
    return BLERR_NOERROR;
}

static void BotAI_UpdateGoals(bot_ai_client_state_t *state, const bot_updateclient_t *client)
{
    (void)state;
    (void)client;

    BotLib_Print(PRT_DEBUG, "[bot_ai] TODO: update goals for client %d\n", state->client_num);
}

static void BotAI_UpdateMovement(bot_ai_client_state_t *state, const bot_updateclient_t *client)
{
    (void)state;
    (void)client;

    BotLib_Print(PRT_DEBUG, "[bot_ai] TODO: update movement for client %d\n", state->client_num);
}

static void BotAI_UpdateWeapons(bot_ai_client_state_t *state, const bot_updateclient_t *client)
{
    (void)state;
    (void)client;

    BotLib_Print(PRT_DEBUG, "[bot_ai] TODO: update weapons for client %d\n", state->client_num);
}

int BotAI_Think(int client, float thinktime)
{
    bot_ai_client_state_t *state = BotAI_GetClientState(client, true);
    if (state == NULL) {
        return BLERR_INVALIDCLIENTNUMBER;
    }

    bool snapshot_seen = false;
    const bot_updateclient_t *snapshot = Bridge_GetClientUpdate(client, &snapshot_seen);
    if (!snapshot_seen || snapshot == NULL) {
        return BLERR_AIUPDATEINACTIVECLIENT;
    }

    BotAI_UpdateGoals(state, snapshot);
    BotAI_UpdateMovement(state, snapshot);
    BotAI_UpdateWeapons(state, snapshot);

    bot_input_t input;
    memset(&input, 0, sizeof(input));
    input.thinktime = thinktime;
    for (int i = 0; i < 3; ++i) {
        input.viewangles[i] = snapshot->viewangles[i];
    }

    Q2_BotInput(client, &input);

    state->last_think_time = thinktime;
    state->last_input = input;
    return BLERR_NOERROR;
}

static void BotAI_PrintMessage(bot_import_t *imports, int priority, const char *fmt, ...)
{
    if (imports == NULL || imports->Print == NULL || fmt == NULL) {
        return;
    }

    va_list args;
    va_start(args, fmt);

    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    imports->Print(priority, "%s", buffer);
}

void BotAI_DebugDumpConsoleMessages(bot_import_t *imports, int client)
{
    bot_ai_client_state_t *state = BotAI_FindClientState(client);
    if (state == NULL || !state->active) {
        BotAI_PrintMessage(imports,
                           PRT_WARNING,
                           "[bot_ai] client %d has no active bot state\n",
                           client);
        return;
    }

    if (state->console_chat == NULL) {
        BotAI_PrintMessage(imports,
                           PRT_MESSAGE,
                           "[bot_ai] client %d console queue empty\n",
                           client);
        return;
    }

    int message_type = 0;
    char buffer[256];
    while (BotNextConsoleMessage(state->console_chat, &message_type, buffer, sizeof(buffer))) {
        BotAI_PrintMessage(imports,
                           PRT_MESSAGE,
                           "[bot_ai] client %d console(%d): %s\n",
                           client,
                           message_type,
                           buffer);
    }

    size_t pending = BotNumConsoleMessages(state->console_chat);
    BotAI_PrintMessage(imports,
                       PRT_MESSAGE,
                       "[bot_ai] client %d console queue drained (%zu pending)\n",
                       client,
                       pending);
}

void BotAI_DebugDumpConsoleMessagesAll(bot_import_t *imports)
{
    for (int client = 0; client < MAX_CLIENTS; ++client) {
        bot_ai_client_state_t *state = BotAI_FindClientState(client);
        if (state != NULL && state->active) {
            BotAI_DebugDumpConsoleMessages(imports, client);
        }
    }
}

void BotAI_DebugToggleDraw(bot_import_t *imports, int client)
{
    bot_ai_client_state_t *state = BotAI_GetClientState(client, true);
    if (state == NULL) {
        BotAI_PrintMessage(imports,
                           PRT_ERROR,
                           "[bot_ai] invalid client index %d for debug toggle\n",
                           client);
        return;
    }

    state->debug_draw_enabled = !state->debug_draw_enabled;
    BotAI_PrintMessage(imports,
                       PRT_MESSAGE,
                       "[bot_ai] debug draw for client %d %s\n",
                       client,
                       state->debug_draw_enabled ? "enabled" : "disabled");
}
