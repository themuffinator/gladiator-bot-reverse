#pragma once

#include "botlib/ai_weapon/bot_weapon.h"
#include "botlib/ai_weight/bot_weight.h"

#ifdef __cplusplus
extern "C" {
#endif

struct ai_character_definition_s;

typedef enum ai_character_value_type_e {
	AI_CHARACTER_VALUE_NONE = 0,
	AI_CHARACTER_VALUE_INTEGER = 1,
	AI_CHARACTER_VALUE_FLOAT = 2,
	AI_CHARACTER_VALUE_STRING = 3,
} ai_character_value_type_t;

typedef struct ai_character_profile_s {
	char character_filename[128];
	char character_name[128];
	float requested_skill;
	bot_weight_config_t *item_weights;
	ai_weapon_weights_t *weapon_weights;
	void *chat_state;
	struct ai_character_definition_s *definition_blob;
} ai_character_profile_t;

ai_character_profile_t *AI_LoadCharacter(const char *filename, float skill);
ai_character_profile_t *AI_LoadCharacterNamed(const char *filename,
	const char *character_name,
	float skill);
void AI_FreeCharacter(ai_character_profile_t *profile);

bot_weight_config_t *AI_ItemWeightsForCharacter(const ai_character_profile_t *profile);
ai_weapon_weights_t *AI_WeaponWeightsForCharacter(const ai_character_profile_t *profile);

int AI_CharacteristicCount(const ai_character_profile_t *profile);
ai_character_value_type_t AI_CharacteristicType(const ai_character_profile_t *profile, int index);
float AI_CharacteristicAsFloat(const ai_character_profile_t *profile, int index);
int AI_CharacteristicAsInteger(const ai_character_profile_t *profile, int index);
const char *AI_CharacteristicAsString(const ai_character_profile_t *profile, int index);

int BotLoadCharacter(const char *character_file, float skill);
int BotLoadNamedCharacter(const char *character_file, const char *character_name, float skill);
int BotLoadCharacterSkill(const char *character_file, float skill);
void BotFreeCharacter(int handle);
void BotFreeCharacterStrings(ai_character_profile_t *profile);
ai_character_profile_t *BotCharacterFromHandle(int handle);
void BotShutdownCharacters(void);

float Characteristic_Float(int handle, int index);
float Characteristic_BFloat(int handle, int index, float minimum, float maximum);
int Characteristic_Integer(int handle, int index);
int Characteristic_BInteger(int handle, int index, int minimum, int maximum);
void Characteristic_String(int handle, int index, char *buffer, int buffer_size);


#ifdef __cplusplus
} // extern "C"
#endif
