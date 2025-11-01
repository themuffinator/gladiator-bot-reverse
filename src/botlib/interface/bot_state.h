#pragma once

#include <stdbool.h>

#include "q2bridge/botlib.h"
#include "../ai/character/bot_character.h"
#include "../ai/chat/ai_chat.h"
#include "../ai/weapon/bot_weapon.h"
#include "../ai/weight/bot_weight.h"
#include "../ai/goal_move_orchestrator.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bot_client_state_s bot_client_state_t;

/**
 * Characteristic indices required during client setup. These values mirror the
 * macros defined in the Gladiator assets (chars.h) and describe where the
 * character, chat, and weight filenames are stored within the parsed profile.
 */
enum bot_characteristic_index_e {
    BOT_CHARACTERISTIC_NAME = 0,
    BOT_CHARACTERISTIC_ALT_NAME = 1,
    BOT_CHARACTERISTIC_WEAPONWEIGHTS = 5,
    BOT_CHARACTERISTIC_CHAT_FILE = 12,
    BOT_CHARACTERISTIC_CHAT_NAME = 13,
    BOT_CHARACTERISTIC_ITEMWEIGHTS = 28,
};

struct bot_client_state_s {
    int client_number;
    bool active;
    bot_settings_t settings;
    bot_clientsettings_t client_settings;
    ai_character_profile_t *character;
    bot_weight_config_t *item_weights;
    ai_weapon_weights_t *weapon_weights;
    bot_chatstate_t *chat_state;
    ai_goal_state_t *goal_state;
    ai_move_state_t *move_state;
    int weapon_state;
    int current_weapon;
    int goal_handle;
    bot_updateclient_t last_client_update;
    bool client_update_valid;
    float last_update_time;
};

bot_client_state_t *BotState_Get(int client);
bot_client_state_t *BotState_Create(int client);
void BotState_Destroy(int client);
void BotState_Move(int old_client, int new_client);
void BotState_ShutdownAll(void);
void BotState_AttachCharacter(bot_client_state_t *state, ai_character_profile_t *profile);

#ifdef __cplusplus
} // extern "C"
#endif

