#include "bot_state.h"

#include <float.h>
#include <stdlib.h>
#include <string.h>

#include "q2bridge/botlib.h"
#include "botlib/ai_goal/ai_goal.h"
#include "botlib/common/l_libvar.h"
#include "botlib/ea/ea_local.h"

/*
 * Retail validates client numbers through maxclients inclusively even though
 * its tables contain maxclients entries.  Keep one compatibility sentinel so
 * the observable off-by-one contract does not reproduce the retail OOB.
 */
static bot_client_state_t *g_bot_state_table[MAX_CLIENTS + 1];
static bot_clientsettings_t g_bot_client_settings[MAX_CLIENTS + 1];
static int g_bot_client_capacity;
static int g_bot_active_client_count;

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
BotState_ResetCombat

Resets the combat tracking fields to their default values.
=============
*/
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
		free(points);
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
		if (state->chat_state != NULL) {
			BotFreeChatState(state->chat_state);
			state->character->chat_state = NULL;
			state->chat_state = NULL;
		}

		if (state->weapon_weights != NULL) {
			AI_FreeWeaponWeights(state->weapon_weights);
			state->character->weapon_weights = NULL;
			state->weapon_weights = NULL;
		}

		if (state->item_weights != NULL) {
			FreeWeightConfig(state->item_weights);
			state->character->item_weights = NULL;
			state->item_weights = NULL;
		}

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
	state->power_armor_time = 0.0f;
	state->quad_time = 0.0f;
	state->invulnerability_time = 0.0f;
	state->rebreather_time = 0.0f;
	state->environmentsuit_time = 0.0f;
	state->stand_time = 0.0f;
	state->chat_standing = false;
	state->bot_death_type = 0;
	state->enemy_death_type = 0;
	memset(state->team_leader, 0, sizeof(state->team_leader));
	memset(state->subteam, 0, sizeof(state->subteam));
	state->formation_dist = 0.0f;
	BotState_FreeConsoleWaypoints(state->checkpoints);
	BotState_FreeConsoleWaypoints(state->patrol_points);
	state->checkpoints = NULL;
	state->patrol_points = NULL;
	state->current_patrol_point = NULL;
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
int BotState_AttachCharacter(bot_client_state_t *state, int character_handle)
{
	if (state == NULL) {
		return BLERR_INVALIDIMPORT;
	}

	state->character_handle = character_handle;
	state->character = BotCharacterFromHandle(character_handle);
	state->item_weights = NULL;
	state->weapon_weights = NULL;
	state->chat_state = NULL;

	if (state->character == NULL) {
		return BLERR_INVALIDIMPORT;
	}

	state->item_weights = state->character->item_weights;
	state->weapon_weights = state->character->weapon_weights;
	state->chat_state = state->character->chat_state;
	state->current_weapon = 0;
	state->client_commands_pending = true;

	const char *display_name = NULL;
	if (LibVarGetValue("altnames") != 0.0f) {
		display_name = AI_CharacteristicAsString(state->character, BOT_CHARACTERISTIC_ALT_NAME);
	}
	if (display_name == NULL || *display_name == '\0') {
		display_name = AI_CharacteristicAsString(state->character, BOT_CHARACTERISTIC_NAME);
	}
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

	if (state->chat_state != NULL) {
		const char *chat_name = AI_CharacteristicAsString(state->character, BOT_CHARACTERISTIC_CHAT_NAME);
		if (chat_name == NULL || *chat_name == '\0') {
			chat_name = (netname != NULL && *netname != '\0') ? netname : fallback_name;
		}
		BotSetChatName(state->chat_state, chat_name, state->client_number);

		const char *gender = AI_CharacteristicAsString(state->character, BOT_CHARACTERISTIC_GENDER);
		if (gender != NULL && (gender[0] == 'f' || gender[0] == 'F')) {
			BotSetChatGender(state->chat_state, CHAT_GENDERFEMALE);
		} else if (gender != NULL && (gender[0] == 'm' || gender[0] == 'M')) {
			BotSetChatGender(state->chat_state, CHAT_GENDERMALE);
		} else {
			BotSetChatGender(state->chat_state, CHAT_GENDERLESS);
		}
	}

	if (state->weapon_state > 0) {
		if (state->weapon_weights != NULL) {
			int status = BotWeaponStateAttachWeights(state->weapon_state, state->weapon_weights);
			if (status != BLERR_NOERROR) {
				BotResetWeaponState(state->weapon_state);
				return status;
			}
		} else {
			BotFreeWeaponWeights(state->weapon_state);
		}
	}

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

	const char *gender = AI_CharacteristicAsString(state->character, BOT_CHARACTERISTIC_GENDER);
	if (gender == NULL)
	{
		gender = "";
	}
	EA_Command(state->client_number, "gender %s", gender);

	if (LibVarGetValue("altnames") != 0.0f)
	{
		const char *name = AI_CharacteristicAsString(state->character, BOT_CHARACTERISTIC_ALT_NAME);
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

Returns the active bot state pointer for a client slot.
=============
*/
bot_client_state_t *BotState_Get(int client)
{
	if (!BotState_ClientInRange(client)) {
		return NULL;
	}

	return g_bot_state_table[client];
}

/*
=============
BotState_Create

Allocates and registers a new bot state for the specified client.
=============
*/
bot_client_state_t *BotState_Create(int client)
{
	if (!BotState_ClientInRange(client)) {
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
	state->ltg_teammate = -1;
	memcpy(&state->client_settings, &g_bot_client_settings[client], sizeof(state->client_settings));
	BotState_ResetCombat(&state->combat);
	g_bot_state_table[client] = state;
	return state;
}

/*
=============
BotState_Destroy

Destroys a bot state and releases its resources.
=============
*/
void BotState_Destroy(int client)
{
	if (!BotState_PhysicalIndexInRange(client)) {
		return;
	}

	bot_client_state_t *state = g_bot_state_table[client];
	if (state == NULL) {
		return;
	}

	BotState_SetActive(state, false);
	BotState_FreeResources(state);
	free(state);
	g_bot_state_table[client] = NULL;
}

/*
=============
BotState_Move

Transfers a bot state from one client slot to another.
=============
*/
void BotState_Move(int old_client, int new_client)
{
	if (!BotState_ClientInRange(old_client) || !BotState_ClientInRange(new_client)) {
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
		memcpy(&state->client_settings,
			&g_bot_client_settings[new_client],
			sizeof(state->client_settings));
		if (state->chat_state != NULL) {
			BotSetChatName(state->chat_state, BotChatName(state->chat_state), new_client);
		}
		if (state->dm_state != NULL) {
			AI_DMState_SetClient(state->dm_state, new_client);
		}
	}
}

/*
=============
BotState_ShutdownAll

Destroys all bot state entries.
=============
*/
void BotState_ShutdownAll(void)
{
	for (int i = 0; i <= MAX_CLIENTS; ++i) {
		BotState_Destroy(i);
	}

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

	memset(&state->last_client_update, 0, sizeof(state->last_client_update));
	state->client_update_valid = false;
	state->last_update_time = 0.0f;
	state->goal_snapshot_count = 0;
	memset(state->goal_snapshot, 0, sizeof(state->goal_snapshot));
	memset(&state->last_move_result, 0, sizeof(state->last_move_result));
	state->has_move_result = false;
	state->active_goal_number = 0;
	state->current_weapon = 0;
	state->client_commands_pending = false;
	BotState_ResetCombat(&state->combat);
	state->power_armor_time = 0.0f;
	state->quad_time = 0.0f;
	state->invulnerability_time = 0.0f;
	state->rebreather_time = 0.0f;
	state->environmentsuit_time = 0.0f;
	state->stand_time = 0.0f;
	state->chat_standing = false;
	state->bot_death_type = 0;
	state->enemy_death_type = 0;
	memset(state->team_leader, 0, sizeof(state->team_leader));
	memset(state->subteam, 0, sizeof(state->subteam));
	state->formation_dist = 0.0f;
	BotState_FreeConsoleWaypoints(state->checkpoints);
	BotState_FreeConsoleWaypoints(state->patrol_points);
	state->checkpoints = NULL;
	state->patrol_points = NULL;
	state->current_patrol_point = NULL;
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

	if (state->goal_handle > 0)
	{
		AI_GoalBotlib_ResetState(state->goal_handle);
	}
	if (state->goal_state != NULL)
	{
		AI_GoalState_Reset(state->goal_state);
	}
	if (state->move_state != NULL)
	{
		AI_MoveState_Reset(state->move_state);
		if (state->goal_state != NULL)
		{
			AI_MoveState_LinkAvoidList(state->move_state,
				AI_GoalState_GetAvoidList(state->goal_state));
		}
	}
	if (state->move_handle > 0)
	{
		BotResetMoveState(state->move_handle);
	}
	if (state->dm_state != NULL)
	{
		AI_DMState_Reset(state->dm_state);
	}
	if (state->weapon_state > 0)
	{
		BotResetWeaponState(state->weapon_state);
	}
}

/*
=============
BotState_ResetAllForNewMap

Runs the reconstructed map-load reset over each active runtime client slot.
=============
*/
void BotState_ResetAllForNewMap(void)
{
	for (int client = 0; client < g_bot_client_capacity; ++client)
	{
		bot_client_state_t *state = g_bot_state_table[client];
		if (state != NULL && state->active)
		{
			BotState_ResetForNewMap(state);
		}
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
	if (max_clients < 0) {
		max_clients = 0;
	}
	if (max_clients > MAX_CLIENTS) {
		max_clients = MAX_CLIENTS;
	}

	for (int client = max_clients + 1; client <= MAX_CLIENTS; ++client) {
		BotState_Destroy(client);
		memset(&g_bot_client_settings[client], 0, sizeof(g_bot_client_settings[client]));
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
	memset(g_bot_client_settings, 0, sizeof(g_bot_client_settings));
	for (int i = 0; i <= MAX_CLIENTS; ++i) {
		bot_client_state_t *state = g_bot_state_table[i];
		if (state != NULL) {
			memset(&state->client_settings, 0, sizeof(state->client_settings));
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
	if (state == NULL || state->active == active) {
		return;
	}

	state->active = active;
	if (active) {
		if (!state->active_counted) {
			++g_bot_active_client_count;
			state->active_counted = true;
		}
	} else if (state->active_counted && g_bot_active_client_count > 0) {
		--g_bot_active_client_count;
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
	if (!BotState_ClientInRange(client) || settings == NULL) {
		return BLERR_INVALIDCLIENTNUMBER;
	}

	memcpy(&g_bot_client_settings[client], settings, sizeof(g_bot_client_settings[client]));

	bot_client_state_t *state = g_bot_state_table[client];
	if (state != NULL) {
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
	if (!BotState_ClientInRange(client)) {
		return NULL;
	}

	return &g_bot_client_settings[client];
}

/*
=============
BotState_ClientName

Returns the live presentation name stored for a client slot.
=============
*/
const char *BotState_ClientName(int client)
{
	const bot_clientsettings_t *settings = BotState_ClientSettings(client);
	if (settings == NULL) {
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
	const bot_clientsettings_t *settings = BotState_ClientSettings(client);
	if (settings == NULL) {
		return "";
	}

	return settings->skin;
}

/*
=============
BotState_FindClientByName

Finds the first live client presentation slot with a matching netname.
=============
*/
int BotState_FindClientByName(const char *name)
{
	if (name == NULL) {
		return 0;
	}

	for (int client = 0; client < g_bot_client_capacity; ++client) {
		if (strcmp(g_bot_client_settings[client].netname, name) == 0) {
			return client;
		}
	}

	return 0;
}
