#pragma once

#include <stdbool.h>

#include "botlib/ai_weapon/bot_weapon.h"
#include "botlib/ai_weight/bot_weight.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ai_character_definition_s bot_character_t;

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
	bot_character_t *definition_blob;
} ai_character_profile_t;

bot_character_t *AI_LoadCharacterDefinition(const char *filename,
	const char *character_name);
void AI_FreeCharacterDefinition(bot_character_t *character);
void AI_ShutdownCharacterDefinitions(void);
int AI_CharacteristicDefinitionCount(const bot_character_t *character);
ai_character_value_type_t AI_CharacteristicDefinitionType(const bot_character_t *character,
	int index);
float AI_CharacteristicDefinitionAsFloat(const bot_character_t *character, int index);
int AI_CharacteristicDefinitionAsInteger(const bot_character_t *character, int index);
const char *AI_CharacteristicDefinitionAsString(const bot_character_t *character, int index);

ai_character_profile_t *AI_LoadCharacter(const char *filename, float skill);
ai_character_profile_t *AI_LoadCharacterNamed(const char *filename,
	const char *character_name,
	float skill);
bool AI_CharacterFileUsesSkillBlocks(const char *filename);
bool AI_CharacterFileUsesNamedBlocks(const char *filename);
bool AI_CharacterDefaultFileUsesSkillBlocks(void);
ai_character_profile_t *AI_LoadCharacterSkillProfileBlock(const char *filename,
	float requested_skill,
	int block_skill,
	bool merge_defaults);
ai_character_profile_t *AI_LoadCharacterSkillProfile(const char *filename, float skill);
bool AI_ApplyCharacterDefaults(ai_character_profile_t *profile,
	const ai_character_profile_t *defaults);
ai_character_profile_t *AI_InterpolateCharacterProfiles(const ai_character_profile_t *first,
	const ai_character_profile_t *second,
	float skill);
void AI_FreeCharacter(ai_character_profile_t *profile);
void AI_FreeCharacterStrings(ai_character_profile_t *profile);

bot_weight_config_t *AI_ItemWeightsForCharacter(const ai_character_profile_t *profile);
ai_weapon_weights_t *AI_WeaponWeightsForCharacter(const ai_character_profile_t *profile);
float AI_CharacterProfileSkill(const ai_character_profile_t *profile);
const char *AI_CharacterProfileFilename(const ai_character_profile_t *profile);

int AI_CharacteristicCount(const ai_character_profile_t *profile);
ai_character_value_type_t AI_CharacteristicType(const ai_character_profile_t *profile, int index);
float AI_CharacteristicAsFloat(const ai_character_profile_t *profile, int index);
int AI_CharacteristicAsInteger(const ai_character_profile_t *profile, int index);
const char *AI_CharacteristicAsString(const ai_character_profile_t *profile, int index);

bot_character_t *BotLoadCharacter(const char *character_file,
	const char *character_name);
void BotFreeCharacter(bot_character_t *character);

float Characteristic_Float(const bot_character_t *character, int index);
float Characteristic_BFloat(const bot_character_t *character,
	int index,
	float minimum,
	float maximum);
int Characteristic_Integer(const bot_character_t *character, int index);
int Characteristic_BInteger(const bot_character_t *character,
	int index,
	int minimum,
	int maximum);
char *Characteristic_String(const bot_character_t *character, int index);

int BotLoadCharacterHandle(const char *character_file, float skill);
int BotLoadNamedCharacterHandle(const char *character_file,
	const char *character_name,
	float skill);
int BotLoadCharacterSkillHandle(const char *character_file, float skill);
void BotFreeCharacterHandle(int handle);
void BotFreeCharacterStringsHandle(ai_character_profile_t *profile);
ai_character_profile_t *BotCharacterFromHandle(int handle);
void BotShutdownCharacterHandles(void);

float Characteristic_FloatHandle(int handle, int index);
float Characteristic_BFloatHandle(int handle,
	int index,
	float minimum,
	float maximum);
int Characteristic_IntegerHandle(int handle, int index);
int Characteristic_BIntegerHandle(int handle,
	int index,
	int minimum,
	int maximum);
void Characteristic_StringHandle(int handle,
	int index,
	char *buffer,
	int buffer_size);


#ifdef __cplusplus
} // extern "C"
#endif
