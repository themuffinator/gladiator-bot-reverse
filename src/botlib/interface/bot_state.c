#include "bot_state.h"

#include <float.h>
#include <stdlib.h>
#include <string.h>

#include "q2bridge/botlib.h"
#include "botlib/ai_goal/ai_goal.h"

static bot_client_state_t *g_bot_state_table[MAX_CLIENTS];

static void BotState_ResetCombat(bot_combat_state_t *combat)
{
    if (combat == NULL)
    {
        return;
    }

    memset(combat, 0, sizeof(*combat));
    combat->current_enemy = -1;
    combat->enemy_visible = false;
    combat->enemy_visible_time = -FLT_MAX;
    combat->enemy_sight_time = -FLT_MAX;
    combat->enemy_death_time = -FLT_MAX;
    combat->enemy_last_seen_time = -FLT_MAX;
    combat->revenge_enemy = -1;
    combat->revenge_kills = 0;
    combat->last_known_health = 0;
    combat->last_damage_amount = 0;
    combat->last_damage_time = -FLT_MAX;
    combat->last_health_valid = false;
    combat->took_damage = false;
    VectorClear(combat->last_enemy_origin);
    VectorClear(combat->last_enemy_velocity);
}

static void BotState_FreeResources(bot_client_state_t *state)
{
    if (state == NULL) {
        return;
    }

    if (state->weapon_state > 0) {
        BotFreeWeaponState(state->weapon_state);
        state->weapon_state = 0;
    }

    if (state->move_state != NULL) {
        AI_MoveState_Destroy(state->move_state);
    }

    if (state->move_handle > 0) {
        BotFreeMoveState(state->move_handle);
        state->move_handle = 0;
    }
    if (state->dm_state != NULL) {
        AI_DMState_Destroy(state->dm_state);
    }

    if (state->goal_state != NULL) {
        AI_GoalState_Destroy(state->goal_state);
    }
    if (state->goal_handle > 0) {
        AI_GoalBotlib_FreeState(state->goal_handle);
    }

    if (state->character_handle > 0) {
        BotFreeCharacter(state->character_handle);
        state->character_handle = 0;
    } else if (state->character != NULL) {
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
    state->character_handle = 0;
    state->chat_state = NULL;
    state->weapon_weights = NULL;
    state->item_weights = NULL;
    state->weapon_state = 0;
    state->current_weapon = 0;

    state->goal_state = NULL;
    state->move_state = NULL;
    state->dm_state = NULL;
    state->goal_handle = 0;
    state->goal_snapshot_count = 0;
    memset(state->goal_snapshot, 0, sizeof(state->goal_snapshot));
    memset(&state->last_move_result, 0, sizeof(state->last_move_result));
    state->has_move_result = false;
    state->goal_avoid_duration = 0.0f;
    state->active_goal_number = 0;
    BotState_ResetCombat(&state->combat);
    state->team = -1;
    memset(&state->last_client_update, 0, sizeof(state->last_client_update));
    state->client_update_valid = false;
    state->last_update_time = 0.0f;
    state->active = false;
    memset(&state->client_settings, 0, sizeof(state->client_settings));
}

void BotState_AttachCharacter(bot_client_state_t *state, int character_handle)
{
    if (state == NULL) {
        return;
    }

    state->character_handle = character_handle;
    state->character = BotCharacterFromHandle(character_handle);
    state->item_weights = NULL;
    state->weapon_weights = NULL;
    state->chat_state = NULL;

    state->client_settings.netname[0] = '\0';
    state->client_settings.skin[0] = '\0';

    if (state->character == NULL) {
        return;
    }

    state->item_weights = state->character->item_weights;
    state->weapon_weights = state->character->weapon_weights;
    state->chat_state = state->character->chat_state;
    state->current_weapon = 0;

    const char *display_name = AI_CharacteristicAsString(state->character, BOT_CHARACTERISTIC_NAME);
    if (display_name == NULL || *display_name == '\0') {
        display_name = AI_CharacteristicAsString(state->character, BOT_CHARACTERISTIC_ALT_NAME);
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

    if (state->weapon_state > 0) {
        if (state->weapon_weights != NULL) {
            if (BotWeaponStateAttachWeights(state->weapon_state, state->weapon_weights) != BLERR_NOERROR) {
                BotResetWeaponState(state->weapon_state);
            }
        } else {
            BotFreeWeaponWeights(state->weapon_state);
        }
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
    state->team = -1;
    BotState_ResetCombat(&state->combat);
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
        if (state->dm_state != NULL) {
            AI_DMState_SetClient(state->dm_state, new_client);
        }
    }
}

void BotState_ShutdownAll(void)
{
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        BotState_Destroy(i);
    }
}

