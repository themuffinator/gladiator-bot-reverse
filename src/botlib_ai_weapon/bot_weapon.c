#include "bot_weapon.h"

#include "q2bridge/botlib.h"

#include "botlib_common/l_log.h"
#include "botlib_common/l_memory.h"

#include <string.h>

static bot_weaponstate_t *g_bot_weapon_states[MAX_CLIENTS + 1];

static bool BotWeapon_HandleInRange(int handle)
{
    return handle > 0 && handle <= MAX_CLIENTS;
}

static bot_weaponstate_t *BotWeapon_StateEntry(int handle)
{
    if (!BotWeapon_HandleInRange(handle))
    {
        BotLib_Print(PRT_FATAL, "weapon state handle %d out of range\n", handle);
        return NULL;
    }

    return g_bot_weapon_states[handle];
}

static bot_weaponstate_t *BotWeapon_StateForHandle(int handle)
{
    bot_weaponstate_t *state = BotWeapon_StateEntry(handle);
    if (state == NULL)
    {
        BotLib_Print(PRT_FATAL, "invalid weapon state %d\n", handle);
        return NULL;
    }

    return state;
}

static void BotWeapon_ResetRanking(bot_weaponstate_t *state)
{
    if (state == NULL)
    {
        return;
    }

    state->last_best_weapon = 0;
    state->last_best_weight = 0.0f;
    state->has_last_rank = false;
}

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
            BotLib_Print(PRT_FATAL, "BotAllocWeaponState: allocation failed for handle %d\n", handle);
            return 0;
        }

        g_bot_weapon_states[handle] = state;
        return handle;
    }

    BotLib_Print(PRT_ERROR, "BotAllocWeaponState: no free weapon state slots\n");
    return 0;
}

void BotFreeWeaponState(int handle)
{
    if (!BotWeapon_HandleInRange(handle))
    {
        BotLib_Print(PRT_FATAL, "BotFreeWeaponState: handle %d out of range\n", handle);
        return;
    }

    bot_weaponstate_t *state = g_bot_weapon_states[handle];
    if (state == NULL)
    {
        BotLib_Print(PRT_FATAL, "BotFreeWeaponState: invalid handle %d\n", handle);
        return;
    }

    BotWeapon_ClearWeights(state);
    FreeMemory(state);
    g_bot_weapon_states[handle] = NULL;
}

void BotResetWeaponState(int handle)
{
    bot_weaponstate_t *state = BotWeapon_StateForHandle(handle);
    if (state == NULL)
    {
        return;
    }

    BotWeapon_ResetRanking(state);
}

int BotLoadWeaponWeights(int weaponstate, const char *filename)
{
    bot_weaponstate_t *state = BotWeapon_StateForHandle(weaponstate);
    if (state == NULL)
    {
        return BLERR_CANNOTLOADWEAPONWEIGHTS;
    }

    BotWeapon_ClearWeights(state);

    if (filename == NULL || filename[0] == '\0')
    {
        BotLib_Print(PRT_ERROR, "BotLoadWeaponWeights: filename required\n");
        return BLERR_CANNOTLOADWEAPONWEIGHTS;
    }

    ai_weapon_weights_t *weights = AI_LoadWeaponWeights(filename);
    if (weights == NULL)
    {
        return BLERR_CANNOTLOADWEAPONWEIGHTS;
    }

    state->weights = weights;
    state->config = weights->definitions;
    state->weight_config = weights->config;
    state->owns_weights = true;
    BotWeapon_ResetRanking(state);
    return BLERR_NOERROR;
}

void BotFreeWeaponWeights(int weaponstate)
{
    bot_weaponstate_t *state = BotWeapon_StateForHandle(weaponstate);
    if (state == NULL)
    {
        return;
    }

    BotWeapon_ClearWeights(state);
}

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

    state->weights = weights;
    state->config = weights->definitions;
    state->weight_config = weights->config;
    state->owns_weights = false;
    BotWeapon_ResetRanking(state);
    return BLERR_NOERROR;
}

int BotChooseBestFightWeapon(int weaponstate, const int *inventory)
{
    bot_weaponstate_t *state = BotWeapon_StateForHandle(weaponstate);
    if (state == NULL)
    {
        return 0;
    }

    if (state->weights == NULL || state->weight_config == NULL || state->config == NULL)
    {
        BotWeapon_ResetRanking(state);
        return 0;
    }

    float best_weight = 0.0f;
    int best_weapon = 0;

    const ai_weapon_weights_t *weights = state->weights;
    const bot_weapon_config_t *config = state->config;

    int limit = config->num_weapons;
    if (weights->index_count < limit)
    {
        limit = weights->index_count;
    }

    for (int index = 0; index < limit; ++index)
    {
        const bot_weapon_info_t *weapon = &config->weapons[index];
        if (!weapon->valid)
        {
            continue;
        }

        if (weights->index_by_weapon == NULL)
        {
            continue;
        }

        int weight_index = weights->index_by_weapon[index];
        if (weight_index < 0)
        {
            continue;
        }

        float weight = FuzzyWeight(inventory, state->weight_config, weight_index);
        if (weight > best_weight)
        {
            best_weight = weight;
            best_weapon = index;
        }
    }

    state->last_best_weapon = best_weapon;
    state->last_best_weight = best_weight;
    state->has_last_rank = true;
    return best_weapon;
}

int BotGetTopRankedWeapon(int weaponstate)
{
    bot_weaponstate_t *state = BotWeapon_StateForHandle(weaponstate);
    if (state == NULL || !state->has_last_rank)
    {
        return 0;
    }

    return state->last_best_weapon;
}

void BotGetWeaponInfo(int weaponstate, int weapon, bot_weapon_info_t *weaponinfo)
{
    bot_weaponstate_t *state = BotWeapon_StateForHandle(weaponstate);
    if (state == NULL || weaponinfo == NULL)
    {
        return;
    }

    if (state->config == NULL)
    {
        return;
    }

    if (weapon < 0 || weapon >= state->config->num_weapons)
    {
        BotLib_Print(PRT_ERROR, "BotGetWeaponInfo: weapon %d out of range\n", weapon);
        return;
    }

    memcpy(weaponinfo, &state->config->weapons[weapon], sizeof(*weaponinfo));
}
