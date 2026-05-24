#include "botlib/ai_weapon/bot_weapon.h"

#include "botlib/ai_weight/bot_weight.h"
#include "botlib/common/l_assets.h"
#include "botlib/common/l_libvar.h"
#include "botlib/common/l_log.h"
#include "botlib/common/l_memory.h"
#include "botlib/common/l_struct.h"
#include "botlib/precomp/l_precomp.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define AI_WEAPON_DEFAULT_CONFIG "weapons.c"
#define AI_WEAPON_UNKNOWN_NAME "unknown weapon"

#define AI_WEAPON_OFS(x) ((int)offsetof(bot_weapon_info_t, x))
#define AI_PROJECTILE_OFS(x) ((int)offsetof(bot_weapon_projectile_t, x))

static const bot_weapon_config_t *g_active_weapon_config = NULL;

static const fielddef_t g_weaponinfo_fields[] = {
	{"name", AI_WEAPON_OFS(name), FT_STRING},
	{"level", AI_WEAPON_OFS(level), FT_INT},
	{"model", AI_WEAPON_OFS(model), FT_STRING},
	{"weaponindex", AI_WEAPON_OFS(weaponindex), FT_INT},
	{"flags", AI_WEAPON_OFS(flags), FT_INT},
	{"projectile", AI_WEAPON_OFS(projectile), FT_STRING},
	{"numprojectiles", AI_WEAPON_OFS(numprojectiles), FT_INT},
	{"hspread", AI_WEAPON_OFS(hspread), FT_FLOAT},
	{"vspread", AI_WEAPON_OFS(vspread), FT_FLOAT},
	{"speed", AI_WEAPON_OFS(speed), FT_FLOAT},
	{"acceleration", AI_WEAPON_OFS(acceleration), FT_FLOAT},
	{"recoil", AI_WEAPON_OFS(recoil), FT_FLOAT | FT_ARRAY, 3},
	{"offset", AI_WEAPON_OFS(offset), FT_FLOAT | FT_ARRAY, 3},
	{"angleoffset", AI_WEAPON_OFS(angleoffset), FT_FLOAT | FT_ARRAY, 3},
	{"extrazvelocity", AI_WEAPON_OFS(extrazvelocity), FT_FLOAT},
	{"ammoamount", AI_WEAPON_OFS(ammoamount), FT_INT},
	{"ammoindex", AI_WEAPON_OFS(ammoindex), FT_INT},
	{"activate", AI_WEAPON_OFS(activate), FT_FLOAT},
	{"reload", AI_WEAPON_OFS(reload), FT_FLOAT},
	{"spinup", AI_WEAPON_OFS(spinup), FT_FLOAT},
	{"spindown", AI_WEAPON_OFS(spindown), FT_FLOAT},
	{NULL, 0, 0, 0},
};

static const fielddef_t g_projectileinfo_fields[] = {
	{"name", AI_PROJECTILE_OFS(name), FT_STRING},
	{"model", AI_PROJECTILE_OFS(model), FT_STRING},
	{"flags", AI_PROJECTILE_OFS(flags), FT_INT},
	{"gravity", AI_PROJECTILE_OFS(gravity), FT_FLOAT},
	{"damage", AI_PROJECTILE_OFS(damage), FT_INT},
	{"radius", AI_PROJECTILE_OFS(radius), FT_FLOAT},
	{"visdamage", AI_PROJECTILE_OFS(visdamage), FT_INT},
	{"damagetype", AI_PROJECTILE_OFS(damagetype), FT_INT},
	{"healthinc", AI_PROJECTILE_OFS(healthinc), FT_INT},
	{"push", AI_PROJECTILE_OFS(push), FT_FLOAT},
	{"detonation", AI_PROJECTILE_OFS(detonation), FT_FLOAT},
	{"bounce", AI_PROJECTILE_OFS(bounce), FT_FLOAT},
	{"bouncefric", AI_PROJECTILE_OFS(bouncefric), FT_FLOAT},
	{"bouncestop", AI_PROJECTILE_OFS(bouncestop), FT_FLOAT},
	{NULL, 0, 0, 0},
};

static const structdef_t g_weaponinfo_struct = {
	(int)sizeof(bot_weapon_info_t),
	g_weaponinfo_fields,
};

static const structdef_t g_projectileinfo_struct = {
	(int)sizeof(bot_weapon_projectile_t),
	g_projectileinfo_fields,
};

/*
=============
AI_Weapon_LogPath

Returns the path used in diagnostics, falling back to the retail default.
=============
*/
static const char *AI_Weapon_LogPath(const char *path)
{
	return (path != NULL && path[0] != '\0') ? path : AI_WEAPON_DEFAULT_CONFIG;
}

/*
=============
AI_Weapon_OpenSource

Resolves and opens a weapon configuration through the shared asset search path.
=============
*/
static pc_source_t *AI_Weapon_OpenSource(const char *requested,
										 char *resolved_path,
										 size_t resolved_size)
{
	if (resolved_path != NULL && resolved_size > 0)
	{
		resolved_path[0] = '\0';
	}

	if (requested == NULL || requested[0] == '\0')
	{
		requested = AI_WEAPON_DEFAULT_CONFIG;
	}

	char candidate[AI_WEAPON_MAX_PATH];
	if (!BotLib_ResolveAssetPath(requested, NULL, candidate, sizeof(candidate)))
	{
		if (resolved_path != NULL && resolved_size > 0)
		{
			strncpy(resolved_path, candidate, resolved_size - 1);
			resolved_path[resolved_size - 1] = '\0';
		}
		return NULL;
	}

	pc_source_t *source = PC_LoadSourceFile(candidate);
	if (resolved_path != NULL && resolved_size > 0)
	{
		strncpy(resolved_path, candidate, resolved_size - 1);
		resolved_path[resolved_size - 1] = '\0';
	}

	return source;
}

/*
=============
AI_Weapon_AllocateConfig

Allocates one Gladiator weaponconfig block with weapon and projectile arrays.
=============
*/
static bot_weapon_config_t *AI_Weapon_AllocateConfig(int max_weaponinfo,
													 int max_projectileinfo)
{
	size_t weapon_bytes = (size_t)max_weaponinfo * sizeof(bot_weapon_info_t);
	size_t projectile_bytes = (size_t)max_projectileinfo * sizeof(bot_weapon_projectile_t);
	size_t allocation_size = sizeof(bot_weapon_config_t) + weapon_bytes + projectile_bytes;

	bot_weapon_config_t *config = (bot_weapon_config_t *)GetClearedMemory(allocation_size);
	if (config == NULL)
	{
		BotLib_Print(PRT_ERROR,
					 "[ai_weapon] failed to allocate weapon configuration (%zu bytes)\n",
					 allocation_size);
		return NULL;
	}

	bot_weapon_info_t *weapon_array = (bot_weapon_info_t *)(config + 1);
	bot_weapon_projectile_t *projectile_array =
		(bot_weapon_projectile_t *)(weapon_array + max_weaponinfo);

	config->num_weapons = 0;
	config->num_projectiles = 0;
	config->projectiles = projectile_array;
	config->weapons = weapon_array;
	return config;
}

/*
=============
AI_Weapon_FreeLoadArtifacts

Releases parser state and the partially built weapon config after a load error.
=============
*/
static void AI_Weapon_FreeLoadArtifacts(pc_source_t *source, bot_weapon_config_t *config)
{
	if (source != NULL)
	{
		PC_FreeSource(source);
	}
	if (config != NULL)
	{
		FreeMemory(config);
	}
}

/*
=============
AI_Weapon_LoadFailed

Common failure path that matches the retail cascaded load diagnostic.
=============
*/
static ai_weapon_library_t *AI_Weapon_LoadFailed(pc_source_t *source,
												 bot_weapon_config_t *config,
												 const char *requested)
{
	AI_Weapon_FreeLoadArtifacts(source, config);
	BotLib_Print(PRT_ERROR,
				 "couldn't load weapon config %s\n",
				 AI_Weapon_LogPath(requested));
	return NULL;
}

/*
=============
AI_Weapon_NormalizeMaxLibvar

Reads a max_* libvar and applies the original negative-value fallback.
=============
*/
static int AI_Weapon_NormalizeMaxLibvar(const char *name)
{
	int value = (int)LibVarValue(name, "32");
	if (value < 0)
	{
		BotLib_Print(PRT_ERROR, "%s = %d\n", name, value);
		LibVarSet(name, "32");
		value = 32;
	}
	return value;
}

/*
=============
AI_Weapon_ReadConfigDefinitions

Parses top-level weaponinfo and projectileinfo blocks in Gladiator order.
=============
*/
static bool AI_Weapon_ReadConfigDefinitions(pc_source_t *source,
											bot_weapon_config_t *config,
											int max_weaponinfo,
											int max_projectileinfo,
											const char *log_path)
{
	pc_token_t token;
	while (PC_ReadToken(source, &token))
	{
		if (token.type != TT_NAME)
		{
			BotLib_Print(PRT_ERROR,
						 "unknown definition %s in %s\n",
						 token.string,
						 log_path);
			return false;
		}

		if (strcmp(token.string, "weaponinfo") == 0)
		{
			if (config->num_weapons >= max_weaponinfo)
			{
				BotLib_Print(PRT_ERROR,
							 "more than %d weapons defined in %s\n",
							 max_weaponinfo,
							 log_path);
				return false;
			}

			bot_weapon_info_t *weapon = &config->weapons[config->num_weapons];
			memset(weapon, 0, sizeof(*weapon));
			if (!ReadStructure(source, &g_weaponinfo_struct, weapon))
			{
				return false;
			}
			config->num_weapons += 1;
			continue;
		}

		if (strcmp(token.string, "projectileinfo") == 0)
		{
			if (config->num_projectiles >= max_projectileinfo)
			{
				BotLib_Print(PRT_ERROR,
							 "more than %d projectiles defined in %s\n",
							 max_projectileinfo,
							 log_path);
				return false;
			}

			bot_weapon_projectile_t *projectile = &config->projectiles[config->num_projectiles];
			memset(projectile, 0, sizeof(*projectile));
			if (!ReadStructure(source, &g_projectileinfo_struct, projectile))
			{
				return false;
			}
			config->num_projectiles += 1;
			continue;
		}

		BotLib_Print(PRT_ERROR,
					 "unknown definition %s in %s\n",
					 token.string,
					 log_path);
		return false;
	}

	return true;
}

/*
=============
AI_Weapon_FindProjectile

Finds a projectile definition by name in the parsed configuration.
=============
*/
static const bot_weapon_projectile_t *AI_Weapon_FindProjectile(const bot_weapon_config_t *config,
															   const char *name)
{
	if (config == NULL || name == NULL || name[0] == '\0')
	{
		return NULL;
	}

	for (int index = 0; index < config->num_projectiles; ++index)
	{
		const bot_weapon_projectile_t *projectile = &config->projectiles[index];
		if (strcmp(projectile->name, name) == 0)
		{
			return projectile;
		}
	}

	return NULL;
}

/*
=============
AI_Weapon_FixupDefinitions

Generates weapon numbers and links each weapon to its projectile definition.
=============
*/
static bool AI_Weapon_FixupDefinitions(bot_weapon_config_t *config, const char *log_path)
{
	for (int index = 0; index < config->num_weapons; ++index)
	{
		bot_weapon_info_t *weapon = &config->weapons[index];

		if (weapon->name[0] == '\0')
		{
			BotLib_Print(PRT_ERROR,
						 "weapon %d has no name in %s\n",
						 index,
						 log_path);
			return false;
		}

		if (weapon->projectile[0] == '\0')
		{
			BotLib_Print(PRT_ERROR,
						 "weapon %s has no projectile in %s\n",
						 weapon->name,
						 log_path);
			return false;
		}

		const bot_weapon_projectile_t *projectile =
			AI_Weapon_FindProjectile(config, weapon->projectile);
		if (projectile == NULL)
		{
			BotLib_Print(PRT_ERROR,
						 "weapon %s uses undefined projectile in %s\n",
						 weapon->name,
						 log_path);
			return false;
		}

		weapon->number = index;
		weapon->projectileinfo = projectile;
	}

	if (config->num_weapons == 0)
	{
		BotLib_Print(PRT_WARNING, "no weapon info loaded\n");
	}

	return true;
}

/*
=============
AI_LoadWeaponLibrary
=============
*/
ai_weapon_library_t *AI_LoadWeaponLibrary(const char *filename)
{
	const char *config_name = LibVarString("weaponconfig", AI_WEAPON_DEFAULT_CONFIG);
	const char *requested = (filename != NULL && filename[0] != '\0') ? filename : config_name;
	if (requested == NULL || requested[0] == '\0')
	{
		requested = AI_WEAPON_DEFAULT_CONFIG;
	}

	int max_weaponinfo = AI_Weapon_NormalizeMaxLibvar("max_weaponinfo");
	int max_projectileinfo = AI_Weapon_NormalizeMaxLibvar("max_projectileinfo");

	bot_weapon_config_t *config =
		AI_Weapon_AllocateConfig(max_weaponinfo, max_projectileinfo);
	if (config == NULL)
	{
		BotLib_Print(PRT_ERROR,
					 "couldn't load weapon config %s\n",
					 AI_Weapon_LogPath(requested));
		return NULL;
	}

	char resolved_path[AI_WEAPON_MAX_PATH];
	pc_source_t *source = AI_Weapon_OpenSource(requested,
											   resolved_path,
											   sizeof(resolved_path));
	if (source == NULL)
	{
		const char *failed_path = (resolved_path[0] != '\0') ? resolved_path : requested;
		BotLib_Print(PRT_ERROR, "couldn't load %s\n", AI_Weapon_LogPath(failed_path));
		return AI_Weapon_LoadFailed(NULL, config, requested);
	}

	const char *log_path = AI_Weapon_LogPath(resolved_path);
	if (!AI_Weapon_ReadConfigDefinitions(source,
										 config,
										 max_weaponinfo,
										 max_projectileinfo,
										 log_path))
	{
		return AI_Weapon_LoadFailed(source, config, requested);
	}

	PC_FreeSource(source);
	source = NULL;

	if (!AI_Weapon_FixupDefinitions(config, log_path))
	{
		return AI_Weapon_LoadFailed(NULL, config, requested);
	}

	ai_weapon_library_t *library = (ai_weapon_library_t *)GetClearedMemory(sizeof(*library));
	if (library == NULL)
	{
		BotLib_Print(PRT_ERROR, "[ai_weapon] failed to allocate library wrapper\n");
		return AI_Weapon_LoadFailed(NULL, config, requested);
	}

	library->config = config;
	strncpy(library->source_path, log_path, sizeof(library->source_path) - 1);
	library->source_path[sizeof(library->source_path) - 1] = '\0';
	g_active_weapon_config = config;

	BotLib_Print(PRT_MESSAGE, "loaded %s\n", library->source_path);
	return library;
}

/*
=============
AI_UnloadWeaponLibrary
=============
*/
void AI_UnloadWeaponLibrary(ai_weapon_library_t *library)
{
	if (library == NULL)
	{
		return;
	}

	if (library->config != NULL)
	{
		if (g_active_weapon_config == library->config)
		{
			g_active_weapon_config = NULL;
		}
		FreeMemory(library->config);
		library->config = NULL;
	}

	FreeMemory(library);
}

/*
=============
AI_GetWeaponConfig
=============
*/
const bot_weapon_config_t *AI_GetWeaponConfig(const ai_weapon_library_t *library)
{
	return (library != NULL) ? library->config : NULL;
}

/*
=============
AI_GetActiveWeaponConfig
=============
*/
const bot_weapon_config_t *AI_GetActiveWeaponConfig(void)
{
	return g_active_weapon_config;
}

/*
=============
AI_GetWeaponInfoByNumber
=============
*/
const bot_weapon_info_t *AI_GetWeaponInfoByNumber(int weapon)
{
	const bot_weapon_config_t *config = AI_GetActiveWeaponConfig();
	if (config == NULL || weapon < 0 || weapon >= config->num_weapons)
	{
		return NULL;
	}

	return &config->weapons[weapon];
}

/*
=============
AI_WeaponNumberForModel
=============
*/
int AI_WeaponNumberForModel(const char *model)
{
	const bot_weapon_config_t *config = AI_GetActiveWeaponConfig();
	if (config == NULL || model == NULL)
	{
		return -1;
	}

	for (int index = 0; index < config->num_weapons; ++index)
	{
		const bot_weapon_info_t *weapon = &config->weapons[index];
		if (strcmp(weapon->model, model) == 0)
		{
			return weapon->number;
		}
	}

	return -1;
}

/*
=============
AI_WeaponNameForModel
=============
*/
const char *AI_WeaponNameForModel(const char *model)
{
	const bot_weapon_config_t *config = AI_GetActiveWeaponConfig();
	if (config == NULL || model == NULL)
	{
		return AI_WEAPON_UNKNOWN_NAME;
	}

	for (int index = 0; index < config->num_weapons; ++index)
	{
		const bot_weapon_info_t *weapon = &config->weapons[index];
		if (strcmp(weapon->model, model) == 0)
		{
			return weapon->name;
		}
	}

	return AI_WEAPON_UNKNOWN_NAME;
}

/*
=============
AI_Weapon_FindWeightIndex
=============
*/
static int AI_Weapon_FindWeightIndex(const bot_weight_config_t *config,
									 const bot_weapon_info_t *weapon)
{
	if (config == NULL || weapon == NULL)
	{
		return -1;
	}

	return BotWeight_FindIndex(config, weapon->name);
}

/*
=============
AI_LoadWeaponWeights
=============
*/
ai_weapon_weights_t *AI_LoadWeaponWeights(const char *filename)
{
	char resolved_path[AI_WEAPON_MAX_PATH];
	if (!BotLib_ResolveAssetPath(filename, NULL, resolved_path, sizeof(resolved_path)))
	{
		BotLib_Print(PRT_ERROR,
					 "[ai_weapon] failed to locate weapon weights %s\n",
					 filename != NULL ? filename : "<null>");
		return NULL;
	}

	bot_weight_config_t *config = ReadWeightConfig(resolved_path);
	if (config == NULL)
	{
		return NULL;
	}

	const bot_weapon_config_t *definitions = AI_GetActiveWeaponConfig();
	if (definitions == NULL)
	{
		BotLib_Print(PRT_ERROR,
					 "[ai_weapon] unable to compile weapon weights without an active weapon config (%s)\n",
					 filename != NULL ? filename : "<null>");
		FreeWeightConfig(config);
		return NULL;
	}

	ai_weapon_weights_t *weights = (ai_weapon_weights_t *)GetClearedMemory(sizeof(*weights));
	if (weights == NULL)
	{
		FreeWeightConfig(config);
		return NULL;
	}

	weights->config = config;
	weights->definitions = definitions;
	weights->index_count = definitions->num_weapons;

	if (weights->index_count > 0)
	{
		weights->index_by_weapon =
			(int *)GetClearedMemory(sizeof(int) * (size_t)weights->index_count);
		if (weights->index_by_weapon == NULL)
		{
			AI_FreeWeaponWeights(weights);
			return NULL;
		}

		for (int index = 0; index < weights->index_count; ++index)
		{
			const bot_weapon_info_t *weapon = &definitions->weapons[index];
			weights->index_by_weapon[index] =
				AI_Weapon_FindWeightIndex(config, weapon);
		}
	}

	return weights;
}

/*
=============
AI_FreeWeaponWeights
=============
*/
void AI_FreeWeaponWeights(ai_weapon_weights_t *weights)
{
	if (weights == NULL)
	{
		return;
	}

	if (weights->index_by_weapon != NULL)
	{
		FreeMemory(weights->index_by_weapon);
		weights->index_by_weapon = NULL;
	}

	if (weights->config != NULL)
	{
		FreeWeightConfig(weights->config);
		weights->config = NULL;
	}

	weights->definitions = NULL;
	weights->index_count = 0;
	FreeMemory(weights);
}

/*
=============
AI_Weapon_BuildReferenceInventory
=============
*/
static void AI_Weapon_BuildReferenceInventory(const bot_weapon_info_t *weapon,
											  int *inventory,
											  size_t inventory_count)
{
	if (weapon == NULL || inventory == NULL)
	{
		return;
	}

	memset(inventory, 0, sizeof(int) * inventory_count);

	if (weapon->weaponindex > 0 && (size_t)weapon->weaponindex < inventory_count)
	{
		inventory[weapon->weaponindex] = 1;
	}

	if (weapon->ammoindex > 0 && (size_t)weapon->ammoindex < inventory_count)
	{
		inventory[weapon->ammoindex] = (weapon->ammoamount > 0) ? weapon->ammoamount : 1;
	}
}

/*
=============
AI_WeaponWeightForClient
=============
*/
float AI_WeaponWeightForClient(const ai_weapon_weights_t *weights, int weapon_index)
{
	if (weights == NULL || weights->config == NULL)
	{
		return 0.0f;
	}

	if (weapon_index < 0 ||
		weapon_index >= weights->index_count ||
		weights->index_by_weapon == NULL)
	{
		return 0.0f;
	}

	int weight_index = weights->index_by_weapon[weapon_index];
	if (weight_index < 0)
	{
		return 0.0f;
	}

	if (weights->definitions != NULL && weapon_index < weights->definitions->num_weapons)
	{
		int reference_inventory[MAX_ITEMS];
		const bot_weapon_info_t *weapon = &weights->definitions->weapons[weapon_index];
		AI_Weapon_BuildReferenceInventory(weapon, reference_inventory, MAX_ITEMS);
		return FuzzyWeight(reference_inventory, weights->config, weight_index);
	}

	return FuzzyWeight(NULL, weights->config, weight_index);
}
