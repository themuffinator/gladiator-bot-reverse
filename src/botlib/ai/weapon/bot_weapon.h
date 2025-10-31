#pragma once

#include <stddef.h>

#include "../weight/bot_weight.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ai_weapon_weights_s ai_weapon_weights_t;

size_t BotWeapon_DefinitionCount(void);
const char *BotWeapon_DefinitionName(size_t index);

ai_weapon_weights_t *AI_LoadWeaponWeights(const char *filename);
void AI_FreeWeaponWeights(ai_weapon_weights_t *weights);
float AI_WeaponWeightForClient(const ai_weapon_weights_t *weights,
                               const int *inventory,
                               int weapon_index);

#ifdef __cplusplus
} // extern "C"
#endif
