#pragma once

#include <stddef.h>
#include <stdint.h>
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

#define BOT_STATE_RETAIL_RECORD_SIZE 0x11d0U
#define BOT_STATE_RETAIL_CLIENT_SETTINGS_SIZE 0x90U
#define BOT_STATE_RETAIL_MOVE_OFFSET 0x0b40U
#define BOT_STATE_RETAIL_GOAL_OFFSET 0x0bc0U
#define BOT_STATE_RETAIL_CHAT_OFFSET 0x0f8cU
#define BOT_STATE_RETAIL_WEAPON_OFFSET 0x1048U

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
	char *name;
	bot_goal_t goal;
	struct bot_console_waypoint_s *next;
	struct bot_console_waypoint_s *prev;
	char name_storage[];
} bot_console_waypoint_t;

#if defined(__cplusplus)
#define BOT_STATE_STATIC_ASSERT(condition, message) static_assert(condition, message)
#else
#define BOT_STATE_STATIC_ASSERT(condition, message) _Static_assert(condition, message)
#endif

BOT_STATE_STATIC_ASSERT(sizeof(bot_goal_t) == 0x38,
	"retail bot_goal_t must be 56 bytes");
BOT_STATE_STATIC_ASSERT(offsetof(bot_console_waypoint_t, name) == 0,
	"retail waypoint name pointer must lead the object");
BOT_STATE_STATIC_ASSERT(offsetof(bot_console_waypoint_t, goal) == sizeof(void *),
	"retail waypoint goal must follow the name pointer");
BOT_STATE_STATIC_ASSERT(offsetof(bot_console_waypoint_t, next) ==
	sizeof(void *) + sizeof(bot_goal_t),
	"retail waypoint next link offset mismatch");
BOT_STATE_STATIC_ASSERT(offsetof(bot_console_waypoint_t, prev) ==
	2U * sizeof(void *) + sizeof(bot_goal_t),
	"retail waypoint previous link offset mismatch");
BOT_STATE_STATIC_ASSERT(offsetof(bot_console_waypoint_t, name_storage) ==
	3U * sizeof(void *) + sizeof(bot_goal_t),
	"retail waypoint trailing name offset mismatch");

#if UINTPTR_MAX == UINT32_MAX
BOT_STATE_STATIC_ASSERT(sizeof(bot_console_waypoint_t) == 0x44,
	"32-bit retail waypoint header must be 68 bytes");
#endif

struct bot_client_state_s {
	union
	{
		struct
		{
    int client_number;
	int entity_number;
    int team;
    bool active;
	bool active_counted;
    bot_settings_t settings;
    bot_clientsettings_t client_settings;
	bool client_commands_pending;
	bot_character_t *character;
    bot_weight_config_t *item_weights;
    ai_weapon_weights_t *weapon_weights;
    bot_chatstate_t *chat_state;
    ai_goal_state_t *goal_state;
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
	float enter_game_time;
	bool respawn_requested;
	bool respawn_action_sent;
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
		struct
		{
			unsigned char retail_prefix[BOT_STATE_RETAIL_MOVE_OFFSET];
			bot_movestate_t retail_move_core;
			bot_goalstate_t retail_goal_core;
			unsigned char retail_chat_padding[
				BOT_STATE_RETAIL_WEAPON_OFFSET -
				(BOT_STATE_RETAIL_GOAL_OFFSET + sizeof(bot_goalstate_t))];
			bot_weaponstate_t retail_weapon_core;
		};
	};
};

BOT_STATE_STATIC_ASSERT(sizeof(bot_clientsettings_t) ==
	BOT_STATE_RETAIL_CLIENT_SETTINGS_SIZE,
	"retail client settings record must be 144 bytes");
BOT_STATE_STATIC_ASSERT(sizeof(bot_client_state_t) <= BOT_STATE_RETAIL_RECORD_SIZE,
	"native bot state adapter must fit the retail 0x11d0-byte slot");
BOT_STATE_STATIC_ASSERT(offsetof(bot_client_state_t, retail_move_core) ==
	BOT_STATE_RETAIL_MOVE_OFFSET,
	"retail move core offset mismatch");
BOT_STATE_STATIC_ASSERT(offsetof(bot_client_state_t, retail_goal_core) ==
	BOT_STATE_RETAIL_GOAL_OFFSET,
	"retail goal core offset mismatch");
BOT_STATE_STATIC_ASSERT(offsetof(bot_client_state_t, retail_weapon_core) ==
	BOT_STATE_RETAIL_WEAPON_OFFSET,
	"retail weapon core offset mismatch");
BOT_STATE_STATIC_ASSERT(offsetof(bot_client_state_t, patrol_flags) <
	BOT_STATE_RETAIL_MOVE_OFFSET,
	"successor semantic adapter overlaps the retail move core");

#undef BOT_STATE_STATIC_ASSERT

bot_client_state_t *BotState_Get(int client);
bot_client_state_t *BotState_Create(int client);
void BotState_Destroy(int client);
void BotState_Move(int old_client, int new_client);
void BotState_ShutdownAll(void);
void BotState_ReleaseTables(void);
void BotState_ResetForNewMap(bot_client_state_t *state);
void BotState_ResetAllForNewMap(void);
void BotState_ConfigureClientCapacity(int max_clients);
int BotState_ClientCapacity(void);
bool BotState_ClientInRange(int client);
void BotState_ResetClientSettings(void);
void BotState_SetActive(bot_client_state_t *state, bool active);
void BotState_InitCombatSentinels(bot_client_state_t *state);
void BotState_SetLongTermGoal(bot_client_state_t *state,
	int type,
	int teammate,
	int goal_number);
void BotState_FreeConsoleWaypoints(bot_console_waypoint_t *points);
int BotState_ActiveClientCount(void);
int BotState_AttachCharacter(bot_client_state_t *state, bot_character_t *character);
void BotState_EmitPendingClientCommands(bot_client_state_t *state);
int BotState_SetClientSettings(int client, const bot_clientsettings_t *settings);
const bot_clientsettings_t *BotState_ClientSettings(int client);
const char *BotState_ClientName(int client);
const char *BotState_ClientSkin(int client);
int ClientFromName(const char *name);
int FindClientByName(char *name);
int BotState_FindClientByName(const char *name);

#ifdef __cplusplus
} // extern "C"
#endif

