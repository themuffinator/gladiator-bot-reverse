#pragma once

#include "botlib/ai_weapon/bot_weapon.h"
#include "botlib/ai_weight/bot_weight.h"

#ifdef __cplusplus
extern "C" {
#endif

struct ai_character_definition_s;

typedef struct ai_character_profile_s {
    char character_filename[128];
    float requested_skill;
    bot_weight_config_t *item_weights;
    ai_weapon_weights_t *weapon_weights;
    void *chat_state;
    struct ai_character_definition_s *definition_blob;
} ai_character_profile_t;

ai_character_profile_t *AI_LoadCharacter(const char *filename, float skill);
void AI_FreeCharacter(ai_character_profile_t *profile);

bot_weight_config_t *AI_ItemWeightsForCharacter(const ai_character_profile_t *profile);
ai_weapon_weights_t *AI_WeaponWeightsForCharacter(const ai_character_profile_t *profile);

float AI_CharacteristicAsFloat(const ai_character_profile_t *profile, int index);
int AI_CharacteristicAsInteger(const ai_character_profile_t *profile, int index);
const char *AI_CharacteristicAsString(const ai_character_profile_t *profile, int index);

int BotLoadCharacter(const char *character_file, float skill);
int BotLoadCharacterSkill(const char *character_file, float skill);
void BotFreeCharacter(int handle);
void BotFreeCharacterStrings(ai_character_profile_t *profile);
ai_character_profile_t *BotCharacterFromHandle(int handle);

float Characteristic_Float(int handle, int index);
float Characteristic_BFloat(int handle, int index, float minimum, float maximum);
int Characteristic_Integer(int handle, int index);
int Characteristic_BInteger(int handle, int index, int minimum, int maximum);
void Characteristic_String(int handle, int index, char *buffer, int buffer_size);


#ifdef __cplusplus
} // extern "C"
#endif
