#include "bot_state.h"

#include <stdlib.h>
#include <string.h>

static bot_client_state_t *g_bot_state_table[MAX_CLIENTS];

static void BotState_FreeResources(bot_client_state_t *state)
{
    if (state == NULL) {
        return;
    }

    if (state->character != NULL) {
        AI_FreeCharacter(state->character);
    } else {
        if (state->chat_state != NULL) {
            BotFreeChatState(state->chat_state);
        }

        if (state->weapon_weights != NULL) {
            AI_FreeWeaponWeights(state->weapon_weights);
        }

        if (state->item_weights != NULL) {
            FreeWeightConfig(state->item_weights);
        }
    }

    state->character = NULL;
    state->chat_state = NULL;
    state->weapon_weights = NULL;
    state->item_weights = NULL;

    state->goal_state = NULL;
    state->move_state = NULL;
    memset(&state->last_client_update, 0, sizeof(state->last_client_update));
    state->client_update_valid = false;
    state->last_update_time = 0.0f;
    state->active = false;
    memset(&state->client_settings, 0, sizeof(state->client_settings));
}

void BotState_AttachCharacter(bot_client_state_t *state, ai_character_profile_t *profile)
{
    if (state == NULL) {
        return;
    }

    state->character = profile;
    state->item_weights = NULL;
    state->weapon_weights = NULL;
    state->chat_state = NULL;

    state->client_settings.netname[0] = '\0';
    state->client_settings.skin[0] = '\0';

    if (profile == NULL) {
        return;
    }

    state->item_weights = profile->item_weights;
    state->weapon_weights = profile->weapon_weights;
    state->chat_state = profile->chat_state;

    const char *display_name = AI_CharacteristicAsString(profile, BOT_CHARACTERISTIC_NAME);
    if (display_name == NULL || *display_name == '\0') {
        display_name = AI_CharacteristicAsString(profile, BOT_CHARACTERISTIC_ALT_NAME);
    }

    const char *fallback_name = NULL;
    if (state->settings.charactername[0] != '\0') {
        fallback_name = state->settings.charactername;
    }

    const char *netname = display_name;
    if (netname == NULL || *netname == '\0') {
        netname = fallback_name;
    }

    if (netname != NULL && *netname != '\0') {
        strncpy(state->client_settings.netname, netname, sizeof(state->client_settings.netname) - 1);
        state->client_settings.netname[sizeof(state->client_settings.netname) - 1] = '\0';
    }
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

