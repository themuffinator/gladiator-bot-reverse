#include "bot_state.h"

#include <float.h>
#include <string.h>

#include "q2bridge/botlib.h"
#include "botlib/ai_goal/ai_goal.h"
#include "botlib/common/l_libvar.h"
#include "botlib/common/l_memory.h"
#include "botlib/ea/ea_local.h"
#include "shared/q_platform.h"

typedef union bot_state_sentinel_storage_u
{
	void *pointer_alignment;
	double double_alignment;
	unsigned char bytes[BOT_STATE_RETAIL_RECORD_SIZE];
} bot_state_sentinel_storage_t;

/* Retail owns two cleared, fixed-stride tables for the whole AI lifetime. */
static unsigned char *g_bot_state_pool;
static bot_clientsettings_t *g_bot_client_settings_pool;
/*
 * Retail validates through maxclients inclusively despite allocating exactly
 * maxclients records.  These untracked host sentinels preserve that observable
 * edge without reproducing the retail out-of-bounds access.
 */
static bot_state_sentinel_storage_t g_bot_state_sentinel;
static bot_clientsettings_t g_bot_client_settings_sentinel;
static int g_bot_client_capacity;
static int g_bot_active_client_count;

/*
=============
BotState_ResetCombat

Resets compatibility combat tracking to its established inactive sentinels.
=============
*/
static void BotState_ResetCombat(bot_combat_state_t *combat)
{
	if (combat == NULL)
	{
		return;
	}

	memset(combat, 0, sizeof(*combat));
	combat->enemy_visible_time = -FLT_MAX;
	combat->enemy_sight_time = -FLT_MAX;
	combat->enemy_death_time = -FLT_MAX;
	combat->enemy_last_seen_time = -FLT_MAX;
	combat->chase_time = -FLT_MAX;
	combat->revenge_enemy = -1;
	combat->last_damage_time = -FLT_MAX;
}

/*
=============
BotState_PhysicalIndexInRange

Validates an index against the fixed storage backing the reconstructed tables.
=============
*/
static bool BotState_PhysicalIndexInRange(int client)
{
	return client >= 0 && client <= MAX_CLIENTS;
}

/*
=============
BotState_StateSlot

Returns a stable retail-stride slot or the untracked inclusive-range sentinel.
=============
*/
static bot_client_state_t *BotState_StateSlot(int client)
{
	if (g_bot_client_capacity <= 0 || !BotState_PhysicalIndexInRange(client) ||
		client > g_bot_client_capacity)
	{
		return NULL;
	}
	if (client == g_bot_client_capacity)
	{
		return (bot_client_state_t *)(void *)g_bot_state_sentinel.bytes;
	}
	if (g_bot_state_pool == NULL)
	{
		return NULL;
	}
	return (bot_client_state_t *)(void *)(g_bot_state_pool +
		(size_t)client * BOT_STATE_RETAIL_RECORD_SIZE);
}

/*
=============
BotState_SettingsSlot

Returns the fixed presentation record or its inclusive-range host sentinel.
=============
*/
static bot_clientsettings_t *BotState_SettingsSlot(int client)
{
	if (g_bot_client_capacity <= 0 || !BotState_PhysicalIndexInRange(client) ||
		client > g_bot_client_capacity)
	{
		return NULL;
	}
	if (client == g_bot_client_capacity)
	{
		return &g_bot_client_settings_sentinel;
	}
	if (g_bot_client_settings_pool == NULL)
	{
		return NULL;
	}
	return &g_bot_client_settings_pool[client];
}

/*
=============
BotState_ClearRecord

Clears one complete 0x11d0-byte retail slot, including unused adapter padding.
=============
*/
static void BotState_ClearRecord(bot_client_state_t *state)
{
	if (state != NULL)
	{
		memset(state, 0, BOT_STATE_RETAIL_RECORD_SIZE);
	}
}

/*
=============
BotState_ReleaseStorage

Frees the presentation table before the state base in retail shutdown order.
=============
*/
static void BotState_ReleaseStorage(void)
{
	if (g_bot_client_settings_pool != NULL)
	{
		FreeMemory(g_bot_client_settings_pool);
		g_bot_client_settings_pool = NULL;
	}
	if (g_bot_state_pool != NULL)
	{
		FreeMemory(g_bot_state_pool);
		g_bot_state_pool = NULL;
	}
	memset(&g_bot_client_settings_sentinel,
		0,
		sizeof(g_bot_client_settings_sentinel));
	memset(&g_bot_state_sentinel, 0, sizeof(g_bot_state_sentinel));
}

/*
=============
BotState_FreeConsoleWaypoints

Releases a retail checkpoint or patrol-point chain owned by a bot state.
=============
*/
void BotState_FreeConsoleWaypoints(bot_console_waypoint_t *points)
{
	while (points != NULL)
	{
		bot_console_waypoint_t *next = points->next;
		FreeMemory(points);
		points = next;
	}
}

/*
=============
BotState_FreeResources

Releases the resources owned by a bot state in HLIL teardown order.
=============
*/
static void BotState_FreeResources(bot_client_state_t *state)
{
	if (state == NULL)
	{
		return;
	}

	if (state->chat_state != NULL)
	{
		BotDestroyChatState(state->chat_state);
		state->chat_state = NULL;
	}

	if (state->weapon_state > 0)
	{
		BotFreeWeaponState(state->weapon_state);
		state->weapon_state = 0;
	}
	state->weapon_weights = NULL;

	if (state->goal_handle > 0)
	{
		AI_GoalBotlib_FreeState(state->goal_handle);
		state->goal_handle = 0;
	}
	state->item_weights = NULL;

	if (state->character != NULL)
	{
		BotFreeCharacter(state->character);
		state->character = NULL;
	}

	BotState_FreeConsoleWaypoints(state->checkpoints);
	BotState_FreeConsoleWaypoints(state->patrol_points);
	state->checkpoints = NULL;
	state->patrol_points = NULL;
	state->current_patrol_point = NULL;

	if (state->goal_state != NULL)
	{
		AI_GoalState_Destroy(state->goal_state);
		state->goal_state = NULL;
	}

	if (state->move_handle > 0)
	{
		BotFreeMoveStateHandle(state->move_handle);
		state->move_handle = 0;
	}

	if (state->dm_state != NULL)
	{
		AI_DMState_Destroy(state->dm_state);
		state->dm_state = NULL;
	}

	state->chat_state = NULL;
	state->weapon_weights = NULL;
	state->item_weights = NULL;
	state->weapon_state = 0;
	state->current_weapon = 0;

	state->goal_state = NULL;
	state->dm_state = NULL;
	state->goal_handle = 0;
	state->goal_snapshot_count = 0;
	memset(state->goal_snapshot, 0, sizeof(state->goal_snapshot));
	memset(&state->last_move_result, 0, sizeof(state->last_move_result));
	state->has_move_result = false;
	state->goal_avoid_duration = 0.0f;
	state->active_goal_number = 0;
	state->nearby_goal_time = 0.0f;
	state->nearby_goal_check_time = 0.0f;
	state->long_term_goal_time = 0.0f;
	memset(&state->activation_goal, 0, sizeof(state->activation_goal));
	state->activation_goal_time = 0.0f;
	state->blocked_avoid_right = false;
	state->ai_node = BOT_AI_NODE_SEEK_LTG;
	state->ai_node_switches = 0;
	state->ai_node_overflow = false;
	state->power_armor_time = 0.0f;
	state->quad_time = 0.0f;
	state->invulnerability_time = 0.0f;
	state->rebreather_time = 0.0f;
	state->environmentsuit_time = 0.0f;
	state->stand_time = 0.0f;
	state->chat_standing = false;
	state->enter_game_time = 0.0f;
	state->respawn_requested = false;
	state->respawn_action_sent = false;
	state->respawn_time = 0.0f;
	state->bot_death_type = 0;
	state->enemy_death_type = 0;
	memset(state->team_leader, 0, sizeof(state->team_leader));
	memset(state->subteam, 0, sizeof(state->subteam));
	state->formation_dist = 0.0f;
	state->patrol_flags = 0;
	state->ltg_type = 0;
	state->ltg_teammate = -1;
	state->team_goal_number = 0;
	memset(&state->team_goal, 0, sizeof(state->team_goal));
	state->team_message_time = 0.0f;
	state->team_goal_time = 0.0f;
	state->teammate_visible_time = 0.0f;
	state->arrive_time = 0.0f;
	state->defend_away_time = 0.0f;
	state->rush_base_away_time = 0.0f;
	state->team = -1;
	memset(&state->last_client_update, 0, sizeof(state->last_client_update));
	state->client_update_valid = false;
	state->last_update_time = 0.0f;
	state->active = false;
	state->active_counted = false;
	state->client_commands_pending = false;
	memset(&state->client_settings, 0, sizeof(state->client_settings));
}

/*
=============
BotState_AttachCharacter

Binds character resources to a bot client state.
=============
*/
int BotState_AttachCharacter(bot_client_state_t *state, bot_character_t *character)
{
	if (state == NULL || character == NULL)
	{
		return BLERR_INVALIDIMPORT;
	}

	state->character = character;
	state->item_weights = NULL;
	state->weapon_weights = NULL;
	state->chat_state = NULL;
	state->current_weapon = 0;

	return BLERR_NOERROR;
}

/*
=============
BotState_EmitPendingClientCommands

Queues the one-shot setup commands sourced from the attached character.
=============
*/
void BotState_EmitPendingClientCommands(bot_client_state_t *state)
{
	if (state == NULL || !state->client_commands_pending)
	{
		return;
	}

	state->client_commands_pending = false;
	if (state->character == NULL)
	{
		return;
	}

	const char *gender = Characteristic_String(state->character, BOT_CHARACTERISTIC_GENDER);
	if (gender == NULL)
	{
		gender = "";
	}
	EA_Command(state->client_number, "gender %s", gender);

	if (LibVarGetValue("altnames") != 0.0f)
	{
		const char *name = Characteristic_String(state->character, BOT_CHARACTERISTIC_ALT_NAME);
		if (name == NULL)
		{
			name = "";
		}
		EA_Command(state->client_number, "name %s", name);
	}
}

/*
=============
BotState_Get

Returns the stable fixed-table address for a configured client slot.
=============
*/
bot_client_state_t *BotState_Get(int client)
{
	if (!BotState_ClientInRange(client))
	{
		return NULL;
	}
	return BotState_StateSlot(client);
}

/*
=============
BotState_Create

Returns an inactive cleared fixed-table slot without allocating per client.
=============
*/
bot_client_state_t *BotState_Create(int client)
{
	bot_client_state_t *state = BotState_Get(client);
	if (state == NULL || state->active)
	{
		return NULL;
	}
	return state;
}

/*
=============
BotState_Destroy

Releases a bot state's owners and clears its stable retail slot in place.
=============
*/
void BotState_Destroy(int client)
{
	bot_client_state_t *state = BotState_Get(client);
	if (state == NULL)
	{
		return;
	}

	bool active_counted = state->active_counted;
	BotState_FreeResources(state);
	if (active_counted && g_bot_active_client_count > 0)
	{
		--g_bot_active_client_count;
	}
	BotState_ClearRecord(state);
}

/*
=============
BotState_Move

Copies the complete retail record to a stable destination and clears the source.
=============
*/
void BotState_Move(int old_client, int new_client)
{
	if (!BotState_ClientInRange(old_client) || !BotState_ClientInRange(new_client))
	{
		return;
	}

	if (old_client == new_client)
	{
		return;
	}

	bot_client_state_t *state = BotState_StateSlot(old_client);
	bot_client_state_t *destination = BotState_StateSlot(new_client);
	if (state == NULL || destination == NULL)
	{
		return;
	}

	/* Retail overwrites an inactive destination without releasing its owners. */
	memcpy(destination, state, BOT_STATE_RETAIL_RECORD_SIZE);
	BotState_ClearRecord(state);
	if (destination->goal_state != NULL)
	{
		destination->goal_state->services.userdata = destination;
	}
}

/*
=============
BotState_ShutdownAll

Releases every retail slot owner while retaining the table bases for shutdown.
=============
*/
void BotState_ShutdownAll(void)
{
	for (int client = 0; client < g_bot_client_capacity; ++client)
	{
		BotState_Destroy(client);
	}
	if (g_bot_client_capacity > 0)
	{
		BotState_Destroy(g_bot_client_capacity);
	}

	g_bot_active_client_count = 0;
}

/*
=============
BotState_ReleaseTables

Releases only the two retail AI tables, settings first, without per-client
teardown. The retail arena/subsystem shutdown owns any remaining allocations.
=============
*/
void BotState_ReleaseTables(void)
{
	BotState_ReleaseStorage();
	g_bot_client_capacity = 0;
	g_bot_active_client_count = 0;
}

/*
=============
BotState_ResetForNewMap

Clears level-local AI state while preserving the attached character resources.
=============
*/
void BotState_ResetForNewMap(bot_client_state_t *state)
{
	if (state == NULL)
	{
		return;
	}

	bot_client_state_t preserved = *state;
	BotState_FreeConsoleWaypoints(state->checkpoints);
	BotState_FreeConsoleWaypoints(state->patrol_points);

	BotState_ClearRecord(state);
	state->active = preserved.active;
	state->active_counted = preserved.active_counted;
	state->client_number = preserved.client_number;
	state->entity_number = preserved.entity_number;
	state->settings = preserved.settings;
	state->client_settings = preserved.client_settings;
	state->character = preserved.character;
	state->item_weights = preserved.item_weights;
	state->weapon_weights = preserved.weapon_weights;
	state->chat_state = preserved.chat_state;
	state->goal_handle = preserved.goal_handle;
	state->weapon_state = preserved.weapon_state;
	state->move_handle = preserved.move_handle;
	state->goal_state = preserved.goal_state;
	state->dm_state = preserved.dm_state;
	state->retail_move_core = preserved.retail_move_core;
	state->retail_goal_core = preserved.retail_goal_core;
	state->retail_weapon_core = preserved.retail_weapon_core;
	if (state->move_handle > 0)
	{
		BotResetMoveStateHandle(state->move_handle);
	}
	if (state->goal_handle > 0)
	{
		AI_GoalBotlib_ResetState(state->goal_handle);
	}
	if (state->weapon_state > 0)
	{
		BotResetWeaponState(state->weapon_state);
	}
	if (state->goal_handle > 0)
	{
		AI_GoalBotlib_ResetAvoidGoals(state->goal_handle);
	}
	if (state->move_handle > 0)
	{
		BotResetAvoidReachHandle(state->move_handle);
	}

	/* Reset compatibility-only owners after the exact retail owner sequence. */
	if (state->goal_state != NULL)
	{
		AI_GoalState_Reset(state->goal_state);
	}
	if (state->dm_state != NULL)
	{
		AI_DMState_Reset(state->dm_state);
	}
}

/*
=============
BotState_ResetAllForNewMap

Runs the retail map-load reset over every allocated runtime client slot.
=============
*/
void BotState_ResetAllForNewMap(void)
{
	for (int client = 0; client < g_bot_client_capacity; ++client)
	{
		BotState_ResetForNewMap(BotState_StateSlot(client));
	}
}

/*
=============
BotState_ConfigureClientCapacity

Sets the runtime client range mirrored from the retail maxclients allocation.
=============
*/
void BotState_ConfigureClientCapacity(int max_clients)
{
	if (max_clients < 0)
	{
		max_clients = 0;
	}
	if (max_clients > MAX_CLIENTS)
	{
		max_clients = MAX_CLIENTS;
	}

	if (max_clients == g_bot_client_capacity
		&& (max_clients == 0
			|| (g_bot_state_pool != NULL
				&& g_bot_client_settings_pool != NULL)))
	{
		return;
	}

	BotState_ReleaseTables();
	if (max_clients == 0)
	{
		return;
	}

	g_bot_state_pool = GetClearedMemory((size_t)max_clients
		* BOT_STATE_RETAIL_RECORD_SIZE);
	g_bot_client_settings_pool = GetClearedMemory((size_t)max_clients
		* sizeof(*g_bot_client_settings_pool));
	if (g_bot_state_pool == NULL || g_bot_client_settings_pool == NULL)
	{
		BotState_ReleaseTables();
		return;
	}
	g_bot_client_capacity = max_clients;
}

/*
=============
BotState_ClientCapacity

Returns the configured runtime client range.
=============
*/
int BotState_ClientCapacity(void)
{
	return g_bot_client_capacity;
}

/*
=============
BotState_ClientInRange

Checks the inclusive retail range backed by the compatibility sentinel.
=============
*/
bool BotState_ClientInRange(int client)
{
	return BotState_PhysicalIndexInRange(client) && client <= g_bot_client_capacity;
}

/*
=============
BotState_ResetClientSettings

Clears the game-provided presentation settings table.
=============
*/
void BotState_ResetClientSettings(void)
{
	if (g_bot_client_settings_pool != NULL)
	{
		memset(g_bot_client_settings_pool,
			0,
			(size_t)g_bot_client_capacity
				* sizeof(*g_bot_client_settings_pool));
	}
	memset(&g_bot_client_settings_sentinel,
		0,
		sizeof(g_bot_client_settings_sentinel));
	for (int client = 0; client <= g_bot_client_capacity; ++client)
	{
		bot_client_state_t *state = BotState_StateSlot(client);
		if (state != NULL)
		{
			memset(&state->client_settings,
				0,
				sizeof(state->client_settings));
		}
	}
}

/*
=============
BotState_SetActive

Updates a bot state's active flag and mirrors Gladiator's active bot count.
=============
*/
void BotState_SetActive(bot_client_state_t *state, bool active)
{
	if (state == NULL)
	{
		return;
	}

	state->active = active;
	if (active)
	{
		if (!state->active_counted)
		{
			++g_bot_active_client_count;
			state->active_counted = true;
		}
	}
	else if (state->active_counted)
	{
		if (g_bot_active_client_count > 0)
		{
			--g_bot_active_client_count;
		}
		state->active_counted = false;
	}
}

/*
=============
BotState_SetLongTermGoal

Mirrors Gladiator's long-term-goal type and team-goal number. Retail +0x10a8
stores teammate + 1; this host boundary accepts the zero-based client instead.
=============
*/
void BotState_SetLongTermGoal(bot_client_state_t *state,
	int type,
	int teammate,
	int goal_number)
{
	if (state == NULL)
	{
		return;
	}

	state->ltg_type = type;
	state->ltg_teammate = teammate;
	state->team_goal_number = goal_number;
	state->team_goal.number = goal_number;
}

/*
=============
BotState_ActiveClientCount

Returns the number of active bot clients registered through setup.
=============
*/
int BotState_ActiveClientCount(void)
{
	return g_bot_active_client_count;
}

/*
=============
BotState_SetClientSettings

Stores the game-provided presentation settings for a client slot.
=============
*/
int BotState_SetClientSettings(int client, const bot_clientsettings_t *settings)
{
	bot_clientsettings_t *destination = BotState_SettingsSlot(client);
	if (destination == NULL || settings == NULL)
	{
		return BLERR_INVALIDCLIENTNUMBER;
	}

	memcpy(destination, settings, sizeof(*destination));

	bot_client_state_t *state = BotState_StateSlot(client);
	if (state != NULL)
	{
		memcpy(&state->client_settings, settings, sizeof(state->client_settings));
	}

	return BLERR_NOERROR;
}

/*
=============
BotState_ClientSettings

Returns the last game-provided presentation settings for a client slot.
=============
*/
const bot_clientsettings_t *BotState_ClientSettings(int client)
{
	return BotState_SettingsSlot(client);
}

/*
=============
BotState_ClientName

Returns the live presentation name stored for a client slot.
=============
*/
const char *BotState_ClientName(int client)
{
	const bot_clientsettings_t *settings = BotState_SettingsSlot(client);
	if (settings == NULL)
	{
		BotLib_Print(PRT_WARNING,
			"ClientName: client %d out of range\n",
			client);
		return "";
	}

	return settings->netname;
}

/*
=============
BotState_ClientSkin

Returns the live presentation skin stored for a client slot.
=============
*/
const char *BotState_ClientSkin(int client)
{
	const bot_clientsettings_t *settings = BotState_SettingsSlot(client);
	if (settings == NULL)
	{
		BotLib_Print(PRT_WARNING,
			"ClientSkin: client %d out of range\n",
			client);
		return "";
	}

	return settings->skin;
}

/*
=============
ClientFromName

Returns the first case-sensitive exact client name. Retail aliases every miss
to client zero.
=============
*/
int ClientFromName(const char *name)
{
	if (name == NULL || g_bot_client_capacity <= 0)
	{
		return 0;
	}

	for (int client = 0; client < g_bot_client_capacity; ++client)
	{
		const bot_clientsettings_t *settings = BotState_SettingsSlot(client);
		if (settings != NULL && strcmp(name, settings->netname) == 0)
		{
			return client;
		}
	}

	return 0;
}

/*
=============
FindClientByName

Finds the first case-insensitive exact client name, then the first name that
contains the requested text.
=============
*/
int FindClientByName(char *name)
{
	if (name == NULL)
	{
		return -1;
	}

	for (int client = 0; client < g_bot_client_capacity; ++client)
	{
		const bot_clientsettings_t *settings = BotState_SettingsSlot(client);
		if (settings != NULL && Q_stricmp(settings->netname, name) == 0)
		{
			return client;
		}
	}

	for (int client = 0; client < g_bot_client_capacity; ++client)
	{
		const bot_clientsettings_t *settings = BotState_SettingsSlot(client);
		if (settings != NULL
			&& StringContains(settings->netname, name, 0) != NULL)
		{
			return client;
		}
	}

	return -1;
}

/*
=============
BotState_FindClientByName

Const-safe host adapter for the retail roster lookup.
=============
*/
int BotState_FindClientByName(const char *name)
{
	return FindClientByName((char *)name);
}
