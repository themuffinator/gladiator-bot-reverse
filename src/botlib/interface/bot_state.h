#pragma once

#include <stdbool.h>

#include "q2bridge/botlib.h"
#include "botlib/ai_character/bot_character.h"
#include "botlib/ai_chat/ai_chat.h"
#include "botlib/ai_weapon/bot_weapon.h"
#include "botlib/ai_weight/bot_weight.h"
#include "botlib/ai/goal_move_orchestrator.h"
#include "botlib/ai_move/bot_move.h"
#include "botlib/ai_goal/bot_goal.h"
#include "botlib/ai/ai_dm.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bot_client_state_s bot_client_state_t;

typedef enum bot_ai_node_e
{
	BOT_AI_NODE_SEEK_LTG = 0,
	BOT_AI_NODE_STAND,
	BOT_AI_NODE_ACTIVATE_ENTITY,
	BOT_AI_NODE_SEEK_NBG,
	BOT_AI_NODE_BATTLE_FIGHT,
	BOT_AI_NODE_BATTLE_CHASE,
	BOT_AI_NODE_BATTLE_RETREAT,
	BOT_AI_NODE_BATTLE_NBG,
	BOT_AI_NODE_OBSERVER,
	BOT_AI_NODE_INTERMISSION,
} bot_ai_node_t;

/**
 * Characteristic indices required during client setup. These values mirror the
 * macros defined in the Gladiator assets (chars.h) and describe where the
 * character, chat, and weight filenames are stored within the parsed profile.
 */
enum bot_characteristic_index_e {
    BOT_CHARACTERISTIC_NAME = 0,
    BOT_CHARACTERISTIC_ALT_NAME = 1,
    BOT_CHARACTERISTIC_GENDER = 3,
    BOT_CHARACTERISTIC_WEAPONWEIGHTS = 5,
    BOT_CHARACTERISTIC_CHAT_FILE = 12,
    BOT_CHARACTERISTIC_CHAT_NAME = 13,
    BOT_CHARACTERISTIC_ITEMWEIGHTS = 28,
};

typedef struct bot_combat_state_s
{
	int current_enemy;
	int last_enemy_area;
    bool enemy_visible;
    float enemy_visible_time;
    float enemy_sight_time;
	float enemy_death_time;
	float enemy_last_seen_time;
	float chase_time;
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

typedef struct bot_console_waypoint_s
{
	char name[BOT_CONSOLE_MESSAGE_STORAGE_CHARS];
	bot_goal_t goal;
	struct bot_console_waypoint_s *next;
	struct bot_console_waypoint_s *prev;
} bot_console_waypoint_t;

struct bot_client_state_s {
    int client_number;
	int entity_number;
    int team;
    bool active;
	bool active_counted;
    bot_settings_t settings;
    bot_clientsettings_t client_settings;
	bool client_commands_pending;
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
	float nearby_goal_time;
	float nearby_goal_check_time;
	float long_term_goal_time;
	bot_goal_t activation_goal;
	float activation_goal_time;
	bool blocked_avoid_right;
	bot_combat_state_t combat;
	bot_ai_node_t ai_node;
	int ai_node_switches;
	bool ai_node_overflow;
	float power_armor_time;
	float quad_time;
	float invulnerability_time;
	float rebreather_time;
	float environmentsuit_time;
	float stand_time;
	bool chat_standing;
	bool stand_chat_pending;
	float enter_game_time;
	bool enter_game_chat_attempted;
	bool respawn_requested;
	bool respawn_action_sent;
	bool respawn_chat_pending;
	float respawn_time;
	int bot_death_type;
	int enemy_death_type;
	char team_leader[MAX_NETNAME];
	char subteam[32];
	float formation_dist;
	int ltg_type;
	int ltg_teammate;
	int team_goal_number;
	bot_goal_t team_goal;
	float team_message_time;
	float team_goal_time;
	float teammate_visible_time;
	float arrive_time;
	float defend_away_time;
	float rush_base_away_time;
	float get_flag_away_time;
	bot_console_waypoint_t *checkpoints;
	bot_console_waypoint_t *patrol_points;
	bot_console_waypoint_t *current_patrol_point;
	int patrol_flags;
};

bot_client_state_t *BotState_Get(int client);
bot_client_state_t *BotState_Create(int client);
void BotState_Destroy(int client);
void BotState_Move(int old_client, int new_client);
void BotState_ShutdownAll(void);
void BotState_ResetForNewMap(bot_client_state_t *state);
void BotState_ResetAllForNewMap(void);
void BotState_ConfigureClientCapacity(int max_clients);
int BotState_ClientCapacity(void);
bool BotState_ClientInRange(int client);
void BotState_ResetClientSettings(void);
void BotState_SetActive(bot_client_state_t *state, bool active);
void BotState_SetLongTermGoal(bot_client_state_t *state,
	int type,
	int teammate,
	int goal_number);
void BotState_FreeConsoleWaypoints(bot_console_waypoint_t *points);
int BotState_ActiveClientCount(void);
int BotState_AttachCharacter(bot_client_state_t *state, int character_handle);
void BotState_EmitPendingClientCommands(bot_client_state_t *state);
int BotState_SetClientSettings(int client, const bot_clientsettings_t *settings);
const bot_clientsettings_t *BotState_ClientSettings(int client);
const char *BotState_ClientName(int client);
const char *BotState_ClientSkin(int client);
int BotState_FindClientByName(const char *name);

#ifdef __cplusplus
} // extern "C"
#endif

