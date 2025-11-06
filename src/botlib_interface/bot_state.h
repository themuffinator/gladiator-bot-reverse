#pragma once

#include <stdbool.h>

#include "q2bridge/botlib.h"
#include "botlib_ai_character/bot_character.h"
#include "botlib_ai_chat/ai_chat.h"
#include "botlib_ai_weapon/bot_weapon.h"
#include "botlib_ai_weight/bot_weight.h"
#include "botlib_ai/goal_move_orchestrator.h"
#include "botlib_ai_move/bot_move.h"
#include "botlib_ai_goal/bot_goal.h"
#include "botlib_ai/ai_dm.h"

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

typedef struct bot_combat_state_s
{
    int current_enemy;
    bool enemy_visible;
    float enemy_visible_time;
    float enemy_sight_time;
    float enemy_death_time;
    float enemy_last_seen_time;
    vec3_t last_enemy_origin;
    vec3_t last_enemy_velocity;
    int revenge_enemy;
    int revenge_kills;
    int last_known_health;
    int last_damage_amount;
    float last_damage_time;
    bool last_health_valid;
    bool took_damage;
} bot_combat_state_t;

struct bot_client_state_s {
    int client_number;
    int team;
    bool active;
    bot_settings_t settings;
    bot_clientsettings_t client_settings;
    int character_handle;
    ai_character_profile_t *character;
    bot_weight_config_t *item_weights;
    ai_weapon_weights_t *weapon_weights;
    bot_chatstate_t *chat_state;
    ai_goal_state_t *goal_state;
    ai_move_state_t *move_state;
    int move_handle;
    ai_dm_state_t *dm_state;
    int weapon_state;
    int current_weapon;
    int goal_handle;
    bot_goal_t goal_snapshot[2];
    int goal_snapshot_count;
    bot_updateclient_t last_client_update;
    bool client_update_valid;
    float last_update_time;
    bot_moveresult_t last_move_result;
    bool has_move_result;
    float goal_avoid_duration;
    int active_goal_number;
    bot_combat_state_t combat;
};

bot_client_state_t *BotState_Get(int client);
bot_client_state_t *BotState_Create(int client);
void BotState_Destroy(int client);
void BotState_Move(int old_client, int new_client);
void BotState_ShutdownAll(void);
void BotState_AttachCharacter(bot_client_state_t *state, int character_handle);

#ifdef __cplusplus
} // extern "C"
#endif

