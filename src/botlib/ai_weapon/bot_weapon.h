#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "shared/q_shared.h"

#include "botlib/ai_weight/bot_weight.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Gladiator stores weapon definitions in compact file order.  The HLIL field
 * table for `weaponinfo` starts with the generated weapon number at offset 0,
 * stores `name` at offset 4, `model` at offset 0x54, `activate` at offset
 * 0x144, and ends with a 32-bit pointer to the matched projectile definition
 * at offset 0x154.  The retail row stride is therefore 0x158 bytes; 64-bit
 * reconstruction builds keep the same field offsets but use a native pointer
 * at the tail.  This is intentionally different from Quake III's later
 * embedded-projectile `weaponinfo_t`.
 */

/** Maximum string field length used by the legacy structures. */
#define BOT_WEAPON_MAX_STRINGFIELD 80

/** Retail Gladiator weapon/projectile row sizes recovered from the HLIL. */
#define BOT_WEAPON_INFO_RETAIL_SIZE 0x158
#define BOT_PROJECTILE_INFO_RETAIL_SIZE 0x0d0

/** Retail Gladiator weaponinfo offsets used by the parser and parity tests. */
#define BOT_WEAPON_INFO_NUMBER_OFFSET 0x000
#define BOT_WEAPON_INFO_NAME_OFFSET 0x004
#define BOT_WEAPON_INFO_MODEL_OFFSET 0x054
#define BOT_WEAPON_INFO_ACTIVATE_OFFSET 0x144
#define BOT_WEAPON_INFO_PROJECTILEINFO_OFFSET 0x154

/** Maximum length of the recorded weapon configuration path. */
#define AI_WEAPON_MAX_PATH 260

/* Projectile flags */
#define BOT_PROJECTILE_WINDOWDAMAGE 0x0001
#define BOT_PROJECTILE_RETURN 0x0002

/* Weapon flags */
#define BOT_WEAPON_FIRERELEASED 0x0001

/* Damage types */
#define BOT_DAMAGETYPE_IMPACT 0x0001
#define BOT_DAMAGETYPE_RADIAL 0x0002
#define BOT_DAMAGETYPE_VISIBLE 0x0004

typedef struct bot_weapon_projectile_s {
	char name[BOT_WEAPON_MAX_STRINGFIELD];
	char model[BOT_WEAPON_MAX_STRINGFIELD];
	int flags;
	float gravity;
	int damage;
	float radius;
	int visdamage;
	int damagetype;
	int healthinc;
	float push;
	float detonation;
	float bounce;
	float bouncefric;
	float bouncestop;
} bot_weapon_projectile_t;

#pragma pack(push, 4)
typedef struct bot_weapon_info_s {
	int number;
	char name[BOT_WEAPON_MAX_STRINGFIELD];
	char model[BOT_WEAPON_MAX_STRINGFIELD];
	int level;
	int weaponindex;
	int flags;
	char projectile[BOT_WEAPON_MAX_STRINGFIELD];
	int numprojectiles;
	float hspread;
	float vspread;
	float speed;
	float acceleration;
	vec3_t recoil;
	vec3_t offset;
	vec3_t angleoffset;
	float extrazvelocity;
	int ammoamount;
	int ammoindex;
	float activate;
	float reload;
	float spinup;
	float spindown;
	const bot_weapon_projectile_t *projectileinfo;
} bot_weapon_info_t;
#pragma pack(pop)

typedef struct bot_weapon_config_s {
	int num_weapons;
	int num_projectiles;
	bot_weapon_projectile_t *projectiles;
	bot_weapon_info_t *weapons;
} bot_weapon_config_t;

typedef struct ai_weapon_library_s {
	bot_weapon_config_t *config;
	char source_path[AI_WEAPON_MAX_PATH];
} ai_weapon_library_t;

typedef struct ai_weapon_weights_s {
	bot_weight_config_t *config;
	int *index_by_weapon;
	int index_count;
	const bot_weapon_config_t *definitions;
} ai_weapon_weights_t;

typedef struct bot_weaponstate_s {
	int client;
	const int *inventory;
	ai_weapon_weights_t *weights;
	const bot_weapon_config_t *config;
	const bot_weight_config_t *weight_config;
	bool owns_weights;
	bool fresh_weights;
	int current_weapon;
	const char *current_weapon_model;
	float next_weapon_select_time;
	int last_best_weapon;
	float last_best_weight;
	bool has_last_rank;
} bot_weaponstate_t;

ai_weapon_library_t *AI_LoadWeaponLibrary(const char *filename);
void AI_UnloadWeaponLibrary(ai_weapon_library_t *library);
const bot_weapon_config_t *AI_GetWeaponConfig(const ai_weapon_library_t *library);
const bot_weapon_config_t *AI_GetActiveWeaponConfig(void);
const bot_weapon_info_t *AI_GetWeaponInfoByNumber(int weapon);
int AI_WeaponNumberForModel(const char *model);
const char *AI_WeaponNameForModel(const char *model);

ai_weapon_weights_t *AI_LoadWeaponWeights(const char *filename);
ai_weapon_weights_t *AI_LoadWeaponWeightsFresh(const char *filename);
bool AI_WeaponWeightsBindConfig(ai_weapon_weights_t *weights,
								const bot_weapon_config_t *definitions);
size_t AI_WeaponWeightsConfigByteSize(const ai_weapon_weights_t *weights);
size_t AI_WeaponWeightsIndexByteSize(const ai_weapon_weights_t *weights);
void AI_FreeWeaponWeights(ai_weapon_weights_t *weights);
void AI_FreeWeaponWeightsFresh(ai_weapon_weights_t *weights);
float AI_WeaponWeightForClient(const ai_weapon_weights_t *weights, int weapon_index);

int BotSetupWeaponAI(void);
void BotShutdownWeaponAI(void);

int BotAllocWeaponState(void);
int BotBindRetailWeaponState(int client, bot_weaponstate_t *state);
int BotRebindRetailWeaponState(int old_handle,
	int client,
	bot_weaponstate_t *state);
void BotFreeWeaponState(int handle);
void BotResetWeaponState(int handle);
const bot_weaponstate_t *BotWeaponStatePeek(int handle);
void BotForgetWeaponStateAdapters(void);

int BotLoadWeaponWeights(int weaponstate, const char *filename);
int BotLoadWeaponWeightsFresh(int weaponstate, const char *filename);
void BotFreeWeaponWeights(int weaponstate);
int BotWeaponStateAttachWeights(int weaponstate, ai_weapon_weights_t *weights);
void BotWeaponStateSyncFrame(int weaponstate, int client, const int *inventory, const char *model);
void BotWeaponStateSetCurrentModel(int weaponstate, const char *model);

int BotChooseBestFightWeapon(int weaponstate, const int *inventory);
int BotSelectBestFightWeapon(int client, int weaponstate, const int *inventory, float now);
int BotGetTopRankedWeapon(int weaponstate);
const bot_weapon_info_t *BotCurrentWeaponInfo(int weaponstate);
void BotGetWeaponInfo(int weaponstate, int weapon, bot_weapon_info_t *weaponinfo);

#ifdef __cplusplus
} // extern "C"
#endif

