#include "weapon/bot_weapon.h"

#include "../common/l_log.h"
#include "weight/bot_weight.h"

#include <stddef.h>

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

typedef struct ai_weapon_weights_s {
    bot_weight_config_t *weights;
    void *compiled_index;
} ai_weapon_weights_t;

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
    (void)filename;

    BotLib_Print(PRT_WARNING, "[ai_weapon] TODO: implement AI_LoadWeaponWeights.\n");
    return NULL;
}

void AI_FreeWeaponWeights(ai_weapon_weights_t *weights)
{
    (void)weights;

    BotLib_Print(PRT_WARNING, "[ai_weapon] TODO: implement AI_FreeWeaponWeights.\n");
}

/*
 * WeaponWeightForClient will eventually evaluate the fuzzy logic table (see
 * sub_10035500) using the caller's inventory and cached weight state to return a
 * ranking score for the requested slot.
 */
float AI_WeaponWeightForClient(const ai_weapon_weights_t *weights, int weapon_index)
{
    (void)weights;
    (void)weapon_index;

    BotLib_Print(PRT_WARNING, "[ai_weapon] TODO: implement AI_WeaponWeightForClient.\n");
    return 0.0f;
}
