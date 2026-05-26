#include "bot_weapon.h"

#include "q2bridge/botlib.h"

#include "botlib/ea/ea_local.h"
#include "botlib/common/l_libvar.h"
#include "botlib/common/l_log.h"
#include "botlib/common/l_memory.h"
#include "shared/q_platform.h"

#include <string.h>

#define BOT_WEAPON_DEFAULT_WEIGHTS "default/defaul_w.c"

static bot_weaponstate_t *g_bot_weapon_states[MAX_CLIENTS + 1];
static ai_weapon_library_t *g_weapon_library = NULL;

/*
=============
BotWeapon_HandleInRange

Checks whether a weapon-state handle maps to the one-based client table.
=============
*/
static bool BotWeapon_HandleInRange(int handle)
{
	return handle > 0 && handle <= MAX_CLIENTS;
}

/*
=============
BotWeapon_StateForHandle

Resolves a handle and emits the fatal diagnostics used by the retail helpers.
=============
*/
static bot_weaponstate_t *BotWeapon_StateForHandle(int handle)
{
	if (!BotWeapon_HandleInRange(handle))
	{
		BotLib_Print(PRT_FATAL, "weapon state handle %d out of range\n", handle);
		return NULL;
	}

	bot_weaponstate_t *state = g_bot_weapon_states[handle];
	if (state == NULL)
	{
		BotLib_Print(PRT_FATAL, "invalid weapon state %d\n", handle);
		return NULL;
	}

	return state;
}

/*
=============
BotWeapon_ResetRanking

Clears the cached best-weapon bookkeeping while preserving loaded weights.
=============
*/
static void BotWeapon_ResetRanking(bot_weaponstate_t *state)
{
	if (state == NULL)
	{
		return;
	}

	state->client = 0;
	state->inventory = NULL;
	state->current_weapon = 0;
	state->current_weapon_model = NULL;
	state->next_weapon_select_time = 0.0f;
	state->last_best_weapon = 0;
	state->last_best_weight = 0.0f;
	state->has_last_rank = false;
}

/*
=============
BotWeapon_ClearWeights

Frees or detaches a state's weapon weight table.
=============
*/
static void BotWeapon_ClearWeights(bot_weaponstate_t *state)
{
	if (state == NULL)
	{
		return;
	}

	if (state->weights != NULL && state->owns_weights)
	{
		AI_FreeWeaponWeights(state->weights);
	}

	state->weights = NULL;
	state->config = NULL;
	state->weight_config = NULL;
	state->owns_weights = false;

	BotWeapon_ResetRanking(state);
}

/*
=============
BotWeapon_ConfigForState

Returns the current global weapon definitions used by the retail helpers.
=============
*/
static const bot_weapon_config_t *BotWeapon_ConfigForState(const bot_weaponstate_t *state)
{
	(void)state;
	return AI_GetActiveWeaponConfig();
}

/*
=============
BotWeapon_EnsureActiveBinding

Refreshes a state's fuzzy-weight index against the active weaponconfig.
=============
*/
static bool BotWeapon_EnsureActiveBinding(bot_weaponstate_t *state)
{
	if (state == NULL || state->weights == NULL || state->weights->config == NULL)
	{
		return false;
	}

	const bot_weapon_config_t *config = AI_GetActiveWeaponConfig();
	if (config == NULL)
	{
		return false;
	}

	if (state->weights->definitions != config ||
		state->weights->index_count != config->num_weapons ||
		(config->num_weapons > 0 && state->weights->index_by_weapon == NULL))
	{
		if (!AI_WeaponWeightsBindConfig(state->weights, config))
		{
			state->config = NULL;
			return false;
		}
	}

	state->config = state->weights->definitions;
	state->weight_config = state->weights->config;
	return true;
}

/*
=============
BotWeapon_FindBestFightWeapon

Scores every weapon with a compiled fuzzy-weight index and returns the winner.
=============
*/
static int BotWeapon_FindBestFightWeapon(bot_weaponstate_t *state,
										 const int *inventory,
										 const bot_weapon_config_t **config_out,
										 const bot_weapon_info_t **weapon_out,
										 float *weight_out)
{
	if (config_out != NULL)
	{
		*config_out = NULL;
	}
	if (weapon_out != NULL)
	{
		*weapon_out = NULL;
	}
	if (weight_out != NULL)
	{
		*weight_out = 0.0f;
	}

	if (state == NULL ||
		inventory == NULL ||
		state->weights == NULL ||
		state->weights->config == NULL)
	{
		return 0;
	}

	if (!BotWeapon_EnsureActiveBinding(state) ||
		state->weights->index_by_weapon == NULL)
	{
		return 0;
	}

	const bot_weapon_config_t *config = BotWeapon_ConfigForState(state);
	if (config == NULL)
	{
		return 0;
	}

	float best_weight = 0.0f;
	int best_weapon = 0;
	const bot_weapon_info_t *best_info = NULL;

	int limit = config->num_weapons;
	if (state->weights->index_count < limit)
	{
		limit = state->weights->index_count;
	}

	for (int index = 0; index < limit; ++index)
	{
		int weight_index = state->weights->index_by_weapon[index];
		if (weight_index < 0)
		{
			continue;
		}

		float weight = FuzzyWeight(inventory, state->weight_config, weight_index);
		if (weight > best_weight)
		{
			best_weight = weight;
			best_info = &config->weapons[index];
			best_weapon = best_info->number;
		}
	}

	if (config_out != NULL)
	{
		*config_out = config;
	}
	if (weapon_out != NULL)
	{
		*weapon_out = best_info;
	}
	if (weight_out != NULL)
	{
		*weight_out = best_weight;
	}
	return best_weapon;
}

/*
=============
BotWeapon_RecordSelection

Stores the selected weapon/model for later ranking and command gating.
=============
*/
static void BotWeapon_RecordSelection(bot_weaponstate_t *state,
									  const bot_weapon_info_t *weapon,
									  int weapon_number,
									  float weight)
{
	if (state == NULL)
	{
		return;
	}

	state->current_weapon = weapon_number;
	state->current_weapon_model = (weapon != NULL) ? weapon->model : NULL;
	state->last_best_weapon = weapon_number;
	state->last_best_weight = weight;
	state->has_last_rank = true;
}

/*
=============
BotSetupWeaponAI
=============
*/
int BotSetupWeaponAI(void)
{
	if (g_weapon_library != NULL)
	{
		AI_UnloadWeaponLibrary(g_weapon_library);
		g_weapon_library = NULL;
	}

	const char *file = LibVarString("weaponconfig", "weapons.c");
	g_weapon_library = AI_LoadWeaponLibrary(file);
	if (g_weapon_library == NULL)
	{
		BotLib_Print(PRT_FATAL, "couldn't load the weapon config\n");
		return BLERR_CANNOTLOADWEAPONCONFIG;
	}

	return BLERR_NOERROR;
}

/*
=============
BotShutdownWeaponAI
=============
*/
void BotShutdownWeaponAI(void)
{
	if (g_weapon_library != NULL)
	{
		AI_UnloadWeaponLibrary(g_weapon_library);
		g_weapon_library = NULL;
	}

	for (int handle = 1; handle <= MAX_CLIENTS; ++handle)
	{
		if (g_bot_weapon_states[handle] != NULL)
		{
			BotFreeWeaponState(handle);
		}
	}
}

/*
=============
BotAllocWeaponState
=============
*/
int BotAllocWeaponState(void)
{
	for (int handle = 1; handle <= MAX_CLIENTS; ++handle)
	{
		if (g_bot_weapon_states[handle] != NULL)
		{
			continue;
		}

		bot_weaponstate_t *state = (bot_weaponstate_t *)GetClearedMemory(sizeof(*state));
		if (state == NULL)
		{
			BotLib_Print(PRT_FATAL,
						 "BotAllocWeaponState: allocation failed for handle %d\n",
						 handle);
			return 0;
		}

		state->config = AI_GetActiveWeaponConfig();
		g_bot_weapon_states[handle] = state;
		return handle;
	}

	BotLib_Print(PRT_ERROR, "BotAllocWeaponState: no free weapon state slots\n");
	return 0;
}

/*
=============
BotFreeWeaponState
=============
*/
void BotFreeWeaponState(int handle)
{
	bot_weaponstate_t *state = BotWeapon_StateForHandle(handle);
	if (state == NULL)
	{
		return;
	}

	BotWeapon_ClearWeights(state);
	FreeMemory(state);
	g_bot_weapon_states[handle] = NULL;
}

/*
=============
BotResetWeaponState

Preserves loaded weights while clearing selector state like Gladiator's reset helper.
=============
*/
void BotResetWeaponState(int handle)
{
	bot_weaponstate_t *state = BotWeapon_StateForHandle(handle);
	if (state == NULL)
	{
		return;
	}

	BotWeapon_ResetRanking(state);
}

/*
=============
BotLoadWeaponWeights
=============
*/
int BotLoadWeaponWeights(int weaponstate, const char *filename)
{
	bot_weaponstate_t *state = BotWeapon_StateForHandle(weaponstate);
	if (state == NULL)
	{
		return BLERR_CANNOTLOADWEAPONWEIGHTS;
	}

	BotWeapon_ClearWeights(state);

	const char *resolved = filename;
	if (resolved == NULL || resolved[0] == '\0')
	{
		resolved = BOT_WEAPON_DEFAULT_WEIGHTS;
	}

	ai_weapon_weights_t *weights = AI_LoadWeaponWeights(resolved);
	if (weights == NULL)
	{
		return BLERR_CANNOTLOADWEAPONWEIGHTS;
	}

	const bot_weapon_config_t *config = AI_GetActiveWeaponConfig();
	if (config == NULL)
	{
		state->weights = weights;
		state->config = NULL;
		state->weight_config = weights->config;
		state->owns_weights = true;
		BotWeapon_ResetRanking(state);
		return BLERR_CANNOTLOADWEAPONCONFIG;
	}

	if ((weights->definitions != config ||
		 weights->index_count != config->num_weapons ||
		 (config->num_weapons > 0 && weights->index_by_weapon == NULL)) &&
		!AI_WeaponWeightsBindConfig(weights, config))
	{
		AI_FreeWeaponWeights(weights);
		return BLERR_CANNOTLOADWEAPONWEIGHTS;
	}

	state->weights = weights;
	state->config = weights->definitions;
	state->weight_config = weights->config;
	state->owns_weights = true;
	BotWeapon_ResetRanking(state);
	return BLERR_NOERROR;
}

/*
=============
BotFreeWeaponWeights
=============
*/
void BotFreeWeaponWeights(int weaponstate)
{
	bot_weaponstate_t *state = BotWeapon_StateForHandle(weaponstate);
	if (state == NULL)
	{
		return;
	}

	BotWeapon_ClearWeights(state);
}

/*
=============
BotWeaponStateAttachWeights
=============
*/
int BotWeaponStateAttachWeights(int weaponstate, ai_weapon_weights_t *weights)
{
	bot_weaponstate_t *state = BotWeapon_StateForHandle(weaponstate);
	if (state == NULL)
	{
		return BLERR_CANNOTLOADWEAPONWEIGHTS;
	}

	BotWeapon_ClearWeights(state);

	if (weights == NULL)
	{
		return BLERR_CANNOTLOADWEAPONWEIGHTS;
	}

	const bot_weapon_config_t *config = AI_GetActiveWeaponConfig();
	if (config == NULL)
	{
		return BLERR_CANNOTLOADWEAPONCONFIG;
	}

	if (weights->definitions != config ||
		weights->index_count != config->num_weapons ||
		(config->num_weapons > 0 && weights->index_by_weapon == NULL))
	{
		if (!AI_WeaponWeightsBindConfig(weights, config))
		{
			return BLERR_CANNOTLOADWEAPONWEIGHTS;
		}
	}

	state->weights = weights;
	state->config = weights->definitions;
	state->weight_config = weights->config;
	state->owns_weights = false;
	BotWeapon_ResetRanking(state);
	return BLERR_NOERROR;
}

/*
=============
BotWeaponStateSetCurrentModel

Synchronises the cached live weapon model before combat selection.
=============
*/
void BotWeaponStateSetCurrentModel(int weaponstate, const char *model)
{
	bot_weaponstate_t *state = BotWeapon_StateForHandle(weaponstate);
	if (state == NULL)
	{
		return;
	}

	state->current_weapon_model = (model != NULL && model[0] != '\0') ? model : NULL;
}

/*
=============
BotWeaponStateSyncFrame

Reconstructs Gladiator's per-frame weapon state sync: client, inventory, model.
=============
*/
void BotWeaponStateSyncFrame(int weaponstate,
							 int client,
							 const int *inventory,
							 const char *model)
{
	bot_weaponstate_t *state = BotWeapon_StateForHandle(weaponstate);
	if (state == NULL)
	{
		return;
	}

	state->client = client;
	state->inventory = inventory;
	state->current_weapon_model = (model != NULL && model[0] != '\0') ? model : NULL;
}

/*
=============
BotChooseBestFightWeapon
=============
*/
int BotChooseBestFightWeapon(int weaponstate, const int *inventory)
{
	bot_weaponstate_t *state = BotWeapon_StateForHandle(weaponstate);
	if (state == NULL)
	{
		return 0;
	}

	const int *score_inventory = (inventory != NULL) ? inventory : state->inventory;
	const bot_weapon_config_t *config = NULL;
	const bot_weapon_info_t *weapon = NULL;
	float best_weight = 0.0f;
	int best_weapon = BotWeapon_FindBestFightWeapon(state,
													score_inventory,
													&config,
													&weapon,
													&best_weight);
	(void)config;
	(void)weapon;
	(void)best_weight;
	return best_weapon;
}

/*
=============
BotSelectBestFightWeapon

Gladiator combat wiring: choose, issue a use command when the model changes,
and defer the next use command by the weapon activation time plus three seconds.
=============
*/
int BotSelectBestFightWeapon(int client, int weaponstate, const int *inventory, float now)
{
	bot_weaponstate_t *state = BotWeapon_StateForHandle(weaponstate);
	if (state == NULL)
	{
		return 0;
	}

	if (state->has_last_rank && now < state->next_weapon_select_time)
	{
		return state->current_weapon;
	}

	int command_client = (client >= 0) ? client : state->client;
	const int *score_inventory = (inventory != NULL) ? inventory : state->inventory;
	const bot_weapon_config_t *config = NULL;
	const bot_weapon_info_t *weapon = NULL;
	float best_weight = 0.0f;
	int best_weapon = BotWeapon_FindBestFightWeapon(state,
													score_inventory,
													&config,
													&weapon,
													&best_weight);
	if (config == NULL || weapon == NULL)
	{
		return state->current_weapon;
	}

	if (state->current_weapon_model == NULL ||
		Q_stricmp(weapon->model, state->current_weapon_model) != 0)
	{
		EA_Command(command_client, "use %s", weapon->name);
		state->next_weapon_select_time = now + weapon->activate + 3.0f;
	}

	BotWeapon_RecordSelection(state, weapon, best_weapon, best_weight);
	return best_weapon;
}

/*
=============
BotGetTopRankedWeapon
=============
*/
int BotGetTopRankedWeapon(int weaponstate)
{
	bot_weaponstate_t *state = BotWeapon_StateForHandle(weaponstate);
	if (state == NULL || !state->has_last_rank)
	{
		return 0;
	}

	return state->last_best_weapon;
}

/*
=============
BotCurrentWeaponInfo
=============
*/
const bot_weapon_info_t *BotCurrentWeaponInfo(int weaponstate)
{
	bot_weaponstate_t *state = BotWeapon_StateForHandle(weaponstate);
	if (state == NULL)
	{
		return NULL;
	}

	const bot_weapon_config_t *config = BotWeapon_ConfigForState(state);
	if (config == NULL ||
		state->current_weapon < 0 ||
		state->current_weapon >= config->num_weapons)
	{
		return NULL;
	}

	return &config->weapons[state->current_weapon];
}

/*
=============
BotGetWeaponInfo
=============
*/
void BotGetWeaponInfo(int weaponstate, int weapon, bot_weapon_info_t *weaponinfo)
{
	if (weaponinfo == NULL)
	{
		return;
	}

	memset(weaponinfo, 0, sizeof(*weaponinfo));

	bot_weaponstate_t *state = BotWeapon_StateForHandle(weaponstate);
	if (state == NULL)
	{
		return;
	}

	const bot_weapon_config_t *config = BotWeapon_ConfigForState(state);
	if (config == NULL)
	{
		return;
	}

	if (weapon < 0 || weapon >= config->num_weapons)
	{
		BotLib_Print(PRT_ERROR, "BotGetWeaponInfo: weapon %d out of range\n", weapon);
		return;
	}

	memcpy(weaponinfo, &config->weapons[weapon], sizeof(*weaponinfo));
}
