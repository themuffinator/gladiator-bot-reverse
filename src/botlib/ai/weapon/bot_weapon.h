#pragma once

#include <stddef.h>
#include <stdint.h>

#include "../../shared/q_shared.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Gladiator mirrors Quake III's weapon configuration layout: projectiles and
 * weapons are stored in contiguous arrays that live behind a shared
 * configuration handle.  These definitions follow the HLIL offsets so future
 * call sites can port the original access patterns verbatim.
 */

/** Maximum string field length used by the legacy structures. */
#define BOT_WEAPON_MAX_STRINGFIELD 80

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
    bot_weapon_projectile_t *projectile_info;
} bot_weapon_info_t;

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

typedef struct ai_weapon_weights_s ai_weapon_weights_t;

ai_weapon_library_t *AI_LoadWeaponLibrary(const char *filename);
void AI_UnloadWeaponLibrary(ai_weapon_library_t *library);
const bot_weapon_config_t *AI_GetWeaponConfig(const ai_weapon_library_t *library);

ai_weapon_weights_t *AI_LoadWeaponWeights(const char *filename);
void AI_FreeWeaponWeights(ai_weapon_weights_t *weights);
float AI_WeaponWeightForClient(const ai_weapon_weights_t *weights, int weapon_index);

#ifdef __cplusplus
} // extern "C"
#endif

