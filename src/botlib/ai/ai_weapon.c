#include "weapon/bot_weapon.h"

#include "../common/l_log.h"
#include "weight/bot_weight.h"

#include <stddef.h>
#include <stdlib.h>

struct ai_weapon_weights_s {
    bot_weight_config_t *config;
    int *weight_index;
    size_t weapon_count;
};

/*
 * Weapon preference logic in Gladiator pulls from two layers of configuration:
 *   1. A global weapon library (data_10064080) loaded during library setup via
 *      sub_10035680 after resolving "weaponconfig". Each entry spans 0x158 bytes
 *      and carries names, projectiles, and ranking metadata accessed by helper
 *      routines such as sub_100353c0/sub_10035430.
 *   2. Per-character weapon weight sets acquired from the character profile via
 *      sub_10035340, which in turn consumes the global library when translating
 *      fuzzy weights into runtime lookup tables. Freeing the per-bot handle
 *      routes through sub_10035300 and ultimately BotFreeWeaponWeights.
 * The combat evaluator (sub_10035500) iterates the configured weapons, queries
 * the fuzzy weights, and caches the top-ranked selection for each bot.
 *【F:dev_tools/gladiator.dll.bndb_hlil.txt†L41360-L41427】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L41430-L41505】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L41536-L41615】
 */
typedef struct ai_weapon_library_s {
    bot_weight_config_t *weapon_config_table;
    void *weapon_definition_blob;
    size_t weapon_count;
} ai_weapon_library_t;

/*
 * Library initialisation will eventually parse the weapon configuration file
 * (mirroring sub_10035680) and populate ai_weapon_library_t so BotSetupLibrary
 * can expose the definitions to character loaders. Stubbed until the HLIL logic
 * is ported.
 */
ai_weapon_library_t *AI_LoadWeaponLibrary(const char *filename)
{
    (void)filename;

    BotLib_Print(PRT_WARNING, "[ai_weapon] TODO: implement AI_LoadWeaponLibrary.\n");
    return NULL;
}

void AI_UnloadWeaponLibrary(ai_weapon_library_t *library)
{
    (void)library;

    BotLib_Print(PRT_WARNING, "[ai_weapon] TODO: implement AI_UnloadWeaponLibrary.\n");
}

/*
 * Character-specific weapon weights refer back to the library to resolve
 * symbolic weapon names. The loader mirrors sub_10035340 while the free helper
 * wraps sub_10035300.
 */
ai_weapon_weights_t *AI_LoadWeaponWeights(const char *filename)
{
    bot_weight_config_t *config = ReadWeightConfig(filename);
    if (config == NULL) {
        return NULL;
    }

    size_t weapon_count = BotWeapon_DefinitionCount();
    int *index_table = NULL;
    if (weapon_count > 0) {
        index_table = (int *)malloc(sizeof(*index_table) * weapon_count);
        if (index_table == NULL) {
            FreeWeightConfig(config);
            return NULL;
        }

        for (size_t i = 0; i < weapon_count; ++i) {
            index_table[i] = -1;
        }

        for (size_t i = 0; i < weapon_count; ++i) {
            const char *weapon_name = BotWeapon_DefinitionName(i);
            const char *safe_name = (weapon_name != NULL) ? weapon_name : "<unknown weapon>";
            int weight_index = BotWeight_FindIndex(config, weapon_name);
            if (weight_index < 0) {
                BotLib_Print(PRT_WARNING,
                             "item info %d \"%s\" has no fuzzy weight\n",
                             (int)i,
                             safe_name);
                free(index_table);
                FreeWeightConfig(config);
                return NULL;
            }

            index_table[i] = weight_index;
        }
    }

    ai_weapon_weights_t *weights = (ai_weapon_weights_t *)calloc(1, sizeof(*weights));
    if (weights == NULL) {
        if (index_table != NULL) {
            free(index_table);
        }
        FreeWeightConfig(config);
        return NULL;
    }

    weights->config = config;
    weights->weight_index = index_table;
    weights->weapon_count = weapon_count;
    return weights;
}

void AI_FreeWeaponWeights(ai_weapon_weights_t *weights)
{
    if (weights == NULL) {
        return;
    }

    if (weights->weight_index != NULL) {
        free(weights->weight_index);
        weights->weight_index = NULL;
    }

    if (weights->config != NULL) {
        FreeWeightConfig(weights->config);
        weights->config = NULL;
    }

    weights->weapon_count = 0;
    free(weights);
}

static const char *ai_weapon_definition_name(size_t index)
{
    const char *name = BotWeapon_DefinitionName(index);
    return name != NULL ? name : "<unknown weapon>";
}

float AI_WeaponWeightForClient(const ai_weapon_weights_t *weights,
                               const int *inventory,
                               int weapon_index)
{
    if (weights == NULL) {
        return 0.0f;
    }

    if (weapon_index < 0) {
        BotLib_Print(PRT_WARNING,
                     "item info %d \"%s\" has no fuzzy weight\n",
                     weapon_index,
                     "<unknown weapon>");
        return 0.0f;
    }

    size_t slot = (size_t)weapon_index;
    if (slot >= weights->weapon_count || weights->weight_index == NULL) {
        BotLib_Print(PRT_WARNING,
                     "item info %d \"%s\" has no fuzzy weight\n",
                     weapon_index,
                     ai_weapon_definition_name(slot));
        return 0.0f;
    }

    int fuzzy_index = weights->weight_index[slot];
    if (fuzzy_index < 0) {
        BotLib_Print(PRT_WARNING,
                     "item info %d \"%s\" has no fuzzy weight\n",
                     weapon_index,
                     ai_weapon_definition_name(slot));
        return 0.0f;
    }

    return FuzzyWeight(inventory, weights->config, fuzzy_index);
}
