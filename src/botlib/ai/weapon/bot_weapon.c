#include "bot_weapon.h"

/*
 * TODO: Implement weapon ranking and state management routines backing
 * BotAllocWeaponState, BotFreeWeaponState, BotResetWeaponState,
 * BotLoadWeaponWeights, BotFreeWeaponWeights, BotChooseBestFightWeapon, and
 * BotGetTopRankedWeapon so combat decisions flow through the botlib exports.
 */

size_t BotWeapon_DefinitionCount(void)
{
    return 0;
}

const char *BotWeapon_DefinitionName(size_t index)
{
    (void)index;
    return NULL;
}
