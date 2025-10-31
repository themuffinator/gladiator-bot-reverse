#include "bot_state.h"

#include <stdlib.h>
#include <string.h>

static bot_client_state_t *g_bot_state_table[MAX_CLIENTS];

static void BotState_FreeResources(bot_client_state_t *state)
{
    if (state == NULL) {
        return;
    }

    if (state->chat_state != NULL) {
        BotFreeChatState(state->chat_state);
        state->chat_state = NULL;
    }

    if (state->weapon_weights != NULL) {
        FreeWeightConfig(state->weapon_weights);
        state->weapon_weights = NULL;
    }

    if (state->item_weights != NULL) {
        FreeWeightConfig(state->item_weights);
        state->item_weights = NULL;
    }

    if (state->character != NULL) {
        AI_FreeCharacter(state->character);
        state->character = NULL;
    }

    state->goal_state = NULL;
    state->move_state = NULL;
    state->active = false;
    memset(&state->client_settings, 0, sizeof(state->client_settings));
}

bot_client_state_t *BotState_Get(int client)
{
    if (client < 0 || client >= MAX_CLIENTS) {
        return NULL;
    }

    return g_bot_state_table[client];
}

bot_client_state_t *BotState_Create(int client)
{
    if (client < 0 || client >= MAX_CLIENTS) {
        return NULL;
    }

    if (g_bot_state_table[client] != NULL) {
        return NULL;
    }

    bot_client_state_t *state = calloc(1, sizeof(*state));
    if (state == NULL) {
        return NULL;
    }

    state->client_number = client;
    g_bot_state_table[client] = state;
    return state;
}

void BotState_Destroy(int client)
{
    if (client < 0 || client >= MAX_CLIENTS) {
        return;
    }

    bot_client_state_t *state = g_bot_state_table[client];
    if (state == NULL) {
        return;
    }

    BotState_FreeResources(state);
    free(state);
    g_bot_state_table[client] = NULL;
}

void BotState_Move(int old_client, int new_client)
{
    if (old_client < 0 || old_client >= MAX_CLIENTS ||
        new_client < 0 || new_client >= MAX_CLIENTS) {
        return;
    }

    if (old_client == new_client) {
        return;
    }

    bot_client_state_t *state = g_bot_state_table[old_client];
    g_bot_state_table[new_client] = state;
    g_bot_state_table[old_client] = NULL;

    if (state != NULL) {
        state->client_number = new_client;
    }
}

void BotState_ShutdownAll(void)
{
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        BotState_Destroy(i);
    }
}

