#pragma once

#include "../weapon/bot_weapon.h"
#include "../weight/bot_weight.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ai_character_profile_s ai_character_profile_t;

ai_character_profile_t *AI_LoadCharacter(const char *filename, float skill);
void AI_FreeCharacter(ai_character_profile_t *profile);

bot_weight_config_t *AI_ItemWeightsForCharacter(const ai_character_profile_t *profile);
ai_weapon_weights_t *AI_WeaponWeightsForCharacter(const ai_character_profile_t *profile);

float AI_CharacteristicAsFloat(const ai_character_profile_t *profile, int index);
int AI_CharacteristicAsInteger(const ai_character_profile_t *profile, int index);
const char *AI_CharacteristicAsString(const ai_character_profile_t *profile, int index);

#ifdef __cplusplus
} // extern "C"
#endif
