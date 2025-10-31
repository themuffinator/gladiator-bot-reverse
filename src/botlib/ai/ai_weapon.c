#include "weapon/bot_weapon.h"

#include "../common/l_libvar.h"
#include "../common/l_log.h"
#include "../common/l_memory.h"
#include "../precomp/l_precomp.h"
#include "weight/bot_weight.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define AI_WEAPON_DEFAULT_CONFIG "weapons.c"
#define AI_WEAPON_ASSET_PREFIX "dev_tools/assets/"

typedef struct ai_weapon_weights_s {
    bot_weight_config_t *config;
    int *index_by_weapon;
    int index_count;
    const bot_weapon_config_t *definitions;
} ai_weapon_weights_t;

static const bot_weapon_config_t *g_active_weapon_config = NULL;

static const char *AI_Weapon_LogPath(const char *path)
{
    return (path != NULL && path[0] != '\0') ? path : AI_WEAPON_DEFAULT_CONFIG;
}

static float AI_Weapon_TokenToFloat(const pc_token_t *token)
{
    if (token == NULL)
    {
        return 0.0f;
    }

    if (token->type == TT_NUMBER && (token->subtype & TT_FLOAT))
    {
        return (float)token->floatvalue;
    }

    return (float)token->intvalue;
}

static int AI_Weapon_TokenToInt(const pc_token_t *token)
{
    if (token == NULL)
    {
        return 0;
    }

    if (token->type == TT_NUMBER && (token->subtype & TT_FLOAT))
    {
        return (int)token->floatvalue;
    }

    return (int)token->intvalue;
}

static void AI_Weapon_CopyTokenString(char *dest, size_t dest_size, const pc_token_t *token)
{
    if (dest == NULL || dest_size == 0 || token == NULL)
    {
        return;
    }

    dest[0] = '\0';
    strncpy(dest, token->string, dest_size - 1);
    dest[dest_size - 1] = '\0';
}

static bool AI_Weapon_ReadVector(pc_source_t *source, vec3_t out)
{
    if (source == NULL || out == NULL)
    {
        return false;
    }

    pc_token_t punctuation;
    if (!PC_ExpectTokenType(source, TT_PUNCTUATION, P_BRACEOPEN, &punctuation))
    {
        return false;
    }

    for (int i = 0; i < 3; ++i)
    {
        pc_token_t value;
        if (!PC_ExpectTokenType(source, TT_NUMBER, 0, &value))
        {
            return false;
        }
        out[i] = AI_Weapon_TokenToFloat(&value);

        if (i < 2)
        {
            if (!PC_ExpectTokenType(source, TT_PUNCTUATION, P_COMMA, &punctuation))
            {
                return false;
            }
        }
    }

    if (!PC_ExpectTokenType(source, TT_PUNCTUATION, P_BRACECLOSE, &punctuation))
    {
        return false;
    }

    return true;
}

static bool AI_Weapon_ParseProjectile(pc_source_t *source,
                                      bot_weapon_projectile_t *projectile,
                                      const char *source_path)
{
    const char *log_path = AI_Weapon_LogPath(source_path);

    pc_token_t punctuation;
    if (!PC_ExpectTokenType(source, TT_PUNCTUATION, P_BRACEOPEN, &punctuation))
    {
        BotLib_Print(PRT_ERROR, "projectileinfo missing opening brace in %s\n", log_path);
        return false;
    }

    pc_token_t token;
    while (PC_ReadToken(source, &token))
    {
        if (token.type == TT_PUNCTUATION && token.subtype == P_BRACECLOSE)
        {
            return true;
        }

        if (token.type != TT_NAME)
        {
            BotLib_Print(PRT_ERROR, "unknown projectile field token %s in %s\n", token.string, log_path);
            return false;
        }

        if (strcmp(token.string, "name") == 0)
        {
            pc_token_t value;
            if (!PC_ExpectTokenType(source, TT_STRING, 0, &value))
            {
                return false;
            }
            AI_Weapon_CopyTokenString(projectile->name, sizeof(projectile->name), &value);
        }
        else if (strcmp(token.string, "model") == 0)
        {
            pc_token_t value;
            if (!PC_ExpectTokenType(source, TT_STRING, 0, &value))
            {
                return false;
            }
            AI_Weapon_CopyTokenString(projectile->model, sizeof(projectile->model), &value);
        }
        else if (strcmp(token.string, "flags") == 0)
        {
            pc_token_t value;
            if (!PC_ExpectTokenType(source, TT_NUMBER, 0, &value))
            {
                return false;
            }
            projectile->flags = AI_Weapon_TokenToInt(&value);
        }
        else if (strcmp(token.string, "gravity") == 0)
        {
            pc_token_t value;
            if (!PC_ExpectTokenType(source, TT_NUMBER, 0, &value))
            {
                return false;
            }
            projectile->gravity = AI_Weapon_TokenToFloat(&value);
        }
        else if (strcmp(token.string, "damage") == 0)
        {
            pc_token_t value;
            if (!PC_ExpectTokenType(source, TT_NUMBER, 0, &value))
            {
                return false;
            }
            projectile->damage = AI_Weapon_TokenToInt(&value);
        }
        else if (strcmp(token.string, "radius") == 0)
        {
            pc_token_t value;
            if (!PC_ExpectTokenType(source, TT_NUMBER, 0, &value))
            {
                return false;
            }
            projectile->radius = AI_Weapon_TokenToFloat(&value);
        }
        else if (strcmp(token.string, "visdamage") == 0)
        {
            pc_token_t value;
            if (!PC_ExpectTokenType(source, TT_NUMBER, 0, &value))
            {
                return false;
            }
            projectile->visdamage = AI_Weapon_TokenToInt(&value);
        }
        else if (strcmp(token.string, "damagetype") == 0)
        {
            pc_token_t value;
            if (!PC_ExpectTokenType(source, TT_NUMBER, 0, &value))
            {
                return false;
            }
            projectile->damagetype = AI_Weapon_TokenToInt(&value);
        }
        else if (strcmp(token.string, "healthinc") == 0)
        {
            pc_token_t value;
            if (!PC_ExpectTokenType(source, TT_NUMBER, 0, &value))
            {
                return false;
            }
            projectile->healthinc = AI_Weapon_TokenToInt(&value);
        }
        else if (strcmp(token.string, "push") == 0)
        {
            pc_token_t value;
            if (!PC_ExpectTokenType(source, TT_NUMBER, 0, &value))
            {
                return false;
            }
            projectile->push = AI_Weapon_TokenToFloat(&value);
        }
        else if (strcmp(token.string, "detonation") == 0)
        {
            pc_token_t value;
            if (!PC_ExpectTokenType(source, TT_NUMBER, 0, &value))
            {
                return false;
            }
            projectile->detonation = AI_Weapon_TokenToFloat(&value);
        }
        else if (strcmp(token.string, "bounce") == 0)
        {
            pc_token_t value;
            if (!PC_ExpectTokenType(source, TT_NUMBER, 0, &value))
            {
                return false;
            }
            projectile->bounce = AI_Weapon_TokenToFloat(&value);
        }
        else if (strcmp(token.string, "bouncefric") == 0)
        {
            pc_token_t value;
            if (!PC_ExpectTokenType(source, TT_NUMBER, 0, &value))
            {
                return false;
            }
            projectile->bouncefric = AI_Weapon_TokenToFloat(&value);
        }
        else if (strcmp(token.string, "bouncestop") == 0)
        {
            pc_token_t value;
            if (!PC_ExpectTokenType(source, TT_NUMBER, 0, &value))
            {
                return false;
            }
            projectile->bouncestop = AI_Weapon_TokenToFloat(&value);
        }
        else
        {
            BotLib_Print(PRT_ERROR, "unknown projectile field %s in %s\n", token.string, log_path);
            return false;
        }
    }

    BotLib_Print(PRT_ERROR, "projectileinfo missing closing brace in %s\n", log_path);
    return false;
}

static bool AI_Weapon_ParseWeapon(pc_source_t *source,
                                  bot_weapon_info_t *weapon,
                                  const char *source_path)
{
    const char *log_path = AI_Weapon_LogPath(source_path);

    pc_token_t punctuation;
    if (!PC_ExpectTokenType(source, TT_PUNCTUATION, P_BRACEOPEN, &punctuation))
    {
        BotLib_Print(PRT_ERROR, "weaponinfo missing opening brace in %s\n", log_path);
        return false;
    }

    pc_token_t token;
    while (PC_ReadToken(source, &token))
    {
        if (token.type == TT_PUNCTUATION && token.subtype == P_BRACECLOSE)
        {
            return true;
        }

        if (token.type != TT_NAME)
        {
            BotLib_Print(PRT_ERROR, "unknown weapon field token %s in %s\n", token.string, log_path);
            return false;
        }

        if (strcmp(token.string, "name") == 0)
        {
            pc_token_t value;
            if (!PC_ExpectTokenType(source, TT_STRING, 0, &value))
            {
                return false;
            }
            AI_Weapon_CopyTokenString(weapon->name, sizeof(weapon->name), &value);
        }
        else if (strcmp(token.string, "model") == 0)
        {
            pc_token_t value;
            if (!PC_ExpectTokenType(source, TT_STRING, 0, &value))
            {
                return false;
            }
            AI_Weapon_CopyTokenString(weapon->model, sizeof(weapon->model), &value);
        }
        else if (strcmp(token.string, "level") == 0)
        {
            pc_token_t value;
            if (!PC_ExpectTokenType(source, TT_NUMBER, 0, &value))
            {
                return false;
            }
            weapon->level = AI_Weapon_TokenToInt(&value);
        }
        else if (strcmp(token.string, "weaponindex") == 0)
        {
            pc_token_t value;
            if (!PC_ExpectTokenType(source, TT_NUMBER, 0, &value))
            {
                return false;
            }
            weapon->weaponindex = AI_Weapon_TokenToInt(&value);
        }
        else if (strcmp(token.string, "flags") == 0)
        {
            pc_token_t value;
            if (!PC_ExpectTokenType(source, TT_NUMBER, 0, &value))
            {
                return false;
            }
            weapon->flags = AI_Weapon_TokenToInt(&value);
        }
        else if (strcmp(token.string, "projectile") == 0)
        {
            pc_token_t value;
            if (!PC_ExpectTokenType(source, TT_STRING, 0, &value))
            {
                return false;
            }
            AI_Weapon_CopyTokenString(weapon->projectile, sizeof(weapon->projectile), &value);
        }
        else if (strcmp(token.string, "numprojectiles") == 0)
        {
            pc_token_t value;
            if (!PC_ExpectTokenType(source, TT_NUMBER, 0, &value))
            {
                return false;
            }
            weapon->numprojectiles = AI_Weapon_TokenToInt(&value);
        }
        else if (strcmp(token.string, "hspread") == 0)
        {
            pc_token_t value;
            if (!PC_ExpectTokenType(source, TT_NUMBER, 0, &value))
            {
                return false;
            }
            weapon->hspread = AI_Weapon_TokenToFloat(&value);
        }
        else if (strcmp(token.string, "vspread") == 0)
        {
            pc_token_t value;
            if (!PC_ExpectTokenType(source, TT_NUMBER, 0, &value))
            {
                return false;
            }
            weapon->vspread = AI_Weapon_TokenToFloat(&value);
        }
        else if (strcmp(token.string, "speed") == 0)
        {
            pc_token_t value;
            if (!PC_ExpectTokenType(source, TT_NUMBER, 0, &value))
            {
                return false;
            }
            weapon->speed = AI_Weapon_TokenToFloat(&value);
        }
        else if (strcmp(token.string, "acceleration") == 0)
        {
            pc_token_t value;
            if (!PC_ExpectTokenType(source, TT_NUMBER, 0, &value))
            {
                return false;
            }
            weapon->acceleration = AI_Weapon_TokenToFloat(&value);
        }
        else if (strcmp(token.string, "recoil") == 0)
        {
            if (!AI_Weapon_ReadVector(source, weapon->recoil))
            {
                return false;
            }
        }
        else if (strcmp(token.string, "offset") == 0)
        {
            if (!AI_Weapon_ReadVector(source, weapon->offset))
            {
                return false;
            }
        }
        else if (strcmp(token.string, "angleoffset") == 0)
        {
            if (!AI_Weapon_ReadVector(source, weapon->angleoffset))
            {
                return false;
            }
        }
        else if (strcmp(token.string, "extrazvelocity") == 0)
        {
            pc_token_t value;
            if (!PC_ExpectTokenType(source, TT_NUMBER, 0, &value))
            {
                return false;
            }
            weapon->extrazvelocity = AI_Weapon_TokenToFloat(&value);
        }
        else if (strcmp(token.string, "ammoamount") == 0)
        {
            pc_token_t value;
            if (!PC_ExpectTokenType(source, TT_NUMBER, 0, &value))
            {
                return false;
            }
            weapon->ammoamount = AI_Weapon_TokenToInt(&value);
        }
        else if (strcmp(token.string, "ammoindex") == 0)
        {
            pc_token_t value;
            if (!PC_ExpectTokenType(source, TT_NUMBER, 0, &value))
            {
                return false;
            }
            weapon->ammoindex = AI_Weapon_TokenToInt(&value);
        }
        else if (strcmp(token.string, "activate") == 0)
        {
            pc_token_t value;
            if (!PC_ExpectTokenType(source, TT_NUMBER, 0, &value))
            {
                return false;
            }
            weapon->activate = AI_Weapon_TokenToFloat(&value);
        }
        else if (strcmp(token.string, "reload") == 0)
        {
            pc_token_t value;
            if (!PC_ExpectTokenType(source, TT_NUMBER, 0, &value))
            {
                return false;
            }
            weapon->reload = AI_Weapon_TokenToFloat(&value);
        }
        else if (strcmp(token.string, "spinup") == 0)
        {
            pc_token_t value;
            if (!PC_ExpectTokenType(source, TT_NUMBER, 0, &value))
            {
                return false;
            }
            weapon->spinup = AI_Weapon_TokenToFloat(&value);
        }
        else if (strcmp(token.string, "spindown") == 0)
        {
            pc_token_t value;
            if (!PC_ExpectTokenType(source, TT_NUMBER, 0, &value))
            {
                return false;
            }
            weapon->spindown = AI_Weapon_TokenToFloat(&value);
        }
        else
        {
            BotLib_Print(PRT_ERROR, "unknown weapon field %s in %s\n", token.string, log_path);
            return false;
        }
    }

    BotLib_Print(PRT_ERROR, "weaponinfo missing closing brace in %s\n", log_path);
    return false;
}

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

    pc_source_t *source = PC_LoadSourceFile(requested);
    if (source != NULL)
    {
        if (resolved_path != NULL && resolved_size > 0)
        {
            strncpy(resolved_path, requested, resolved_size - 1);
            resolved_path[resolved_size - 1] = '\0';
        }
        return source;
    }

    bool has_directory = strchr(requested, '/') != NULL || strchr(requested, '\\') != NULL;
    if (!has_directory)
    {
        char fallback[AI_WEAPON_MAX_PATH];
        int written = snprintf(fallback, sizeof(fallback), AI_WEAPON_ASSET_PREFIX "%s", requested);
        if (written < 0 || (size_t)written >= sizeof(fallback))
        {
            fallback[sizeof(fallback) - 1] = '\0';
        }

        source = PC_LoadSourceFile(fallback);
        if (source != NULL)
        {
            if (resolved_path != NULL && resolved_size > 0)
            {
                strncpy(resolved_path, fallback, resolved_size - 1);
                resolved_path[resolved_size - 1] = '\0';
            }
            return source;
        }

        if (resolved_path != NULL && resolved_size > 0)
        {
            strncpy(resolved_path, fallback, resolved_size - 1);
            resolved_path[resolved_size - 1] = '\0';
        }
    }
    else if (resolved_path != NULL && resolved_size > 0)
    {
        strncpy(resolved_path, requested, resolved_size - 1);
        resolved_path[resolved_size - 1] = '\0';
    }

    return NULL;
}

ai_weapon_library_t *AI_LoadWeaponLibrary(const char *filename)
{
    const char *config_name = LibVarString("weaponconfig", AI_WEAPON_DEFAULT_CONFIG);
    const char *requested = (filename != NULL && filename[0] != '\0') ? filename : config_name;
    if (requested == NULL || requested[0] == '\0')
    {
        requested = AI_WEAPON_DEFAULT_CONFIG;
    }

    int max_weaponinfo = (int)LibVarValue("max_weaponinfo", "32");
    if (max_weaponinfo < 0)
    {
        BotLib_Print(PRT_ERROR, "max_weaponinfo = %d\n", max_weaponinfo);
        LibVarSet("max_weaponinfo", "32");
        max_weaponinfo = 32;
    }

    int max_projectileinfo = (int)LibVarValue("max_projectileinfo", "32");
    if (max_projectileinfo < 0)
    {
        BotLib_Print(PRT_ERROR, "max_projectileinfo = %d\n", max_projectileinfo);
        LibVarSet("max_projectileinfo", "32");
        max_projectileinfo = 32;
    }

    size_t weapon_bytes = (size_t)max_weaponinfo * sizeof(bot_weapon_info_t);
    size_t projectile_bytes = (size_t)max_projectileinfo * sizeof(bot_weapon_projectile_t);
    size_t allocation_size = sizeof(bot_weapon_config_t) + weapon_bytes + projectile_bytes;

    bot_weapon_config_t *config = (bot_weapon_config_t *)GetClearedMemory(allocation_size);
    if (config == NULL)
    {
        BotLib_Print(PRT_ERROR, "[ai_weapon] failed to allocate weapon configuration (%zu bytes)\n", allocation_size);
        BotLib_Print(PRT_ERROR, "couldn't load weapon config %s\n", AI_Weapon_LogPath(requested));
        return NULL;
    }

    bot_weapon_info_t *weapon_array = (bot_weapon_info_t *)(config + 1);
    bot_weapon_projectile_t *projectile_array = (bot_weapon_projectile_t *)(weapon_array + max_weaponinfo);

    config->weapons = weapon_array;
    config->projectiles = projectile_array;
    config->num_weapons = 0;
    config->num_projectiles = 0;

    char resolved_path[AI_WEAPON_MAX_PATH];
    pc_source_t *source = AI_Weapon_OpenSource(requested, resolved_path, sizeof(resolved_path));
    if (source == NULL)
    {
        BotLib_Print(PRT_ERROR, "couldn't load %s\n", AI_Weapon_LogPath(resolved_path[0] != '\0' ? resolved_path : requested));
        BotLib_Print(PRT_ERROR, "couldn't load weapon config %s\n", AI_Weapon_LogPath(requested));
        FreeMemory(config);
        return NULL;
    }

    const char *log_path = AI_Weapon_LogPath(resolved_path);

    pc_token_t token;
    while (PC_ReadToken(source, &token))
    {
        if (token.type != TT_NAME)
        {
            BotLib_Print(PRT_ERROR, "unknown definition %s in %s\n", token.string, log_path);
            PC_FreeSource(source);
            FreeMemory(config);
            BotLib_Print(PRT_ERROR, "couldn't load weapon config %s\n", AI_Weapon_LogPath(requested));
            return NULL;
        }

        if (strcmp(token.string, "weaponinfo") == 0)
        {
            if (config->num_weapons >= max_weaponinfo)
            {
                BotLib_Print(PRT_ERROR, "more than %d weapons defined in %s\n", max_weaponinfo, log_path);
                PC_FreeSource(source);
                FreeMemory(config);
                BotLib_Print(PRT_ERROR, "couldn't load weapon config %s\n", AI_Weapon_LogPath(requested));
                return NULL;
            }

            bot_weapon_info_t *weapon = &config->weapons[config->num_weapons];
            memset(weapon, 0, sizeof(*weapon));
            if (!AI_Weapon_ParseWeapon(source, weapon, log_path))
            {
                PC_FreeSource(source);
                FreeMemory(config);
                BotLib_Print(PRT_ERROR, "couldn't load weapon config %s\n", AI_Weapon_LogPath(requested));
                return NULL;
            }

            weapon->number = (int)config->num_weapons;
            config->num_weapons += 1;
        }
        else if (strcmp(token.string, "projectileinfo") == 0)
        {
            if (config->num_projectiles >= max_projectileinfo)
            {
                BotLib_Print(PRT_ERROR, "more than %d projectiles defined in %s\n", max_projectileinfo, log_path);
                PC_FreeSource(source);
                FreeMemory(config);
                BotLib_Print(PRT_ERROR, "couldn't load weapon config %s\n", AI_Weapon_LogPath(requested));
                return NULL;
            }

            bot_weapon_projectile_t *projectile = &config->projectiles[config->num_projectiles];
            memset(projectile, 0, sizeof(*projectile));
            if (!AI_Weapon_ParseProjectile(source, projectile, log_path))
            {
                PC_FreeSource(source);
                FreeMemory(config);
                BotLib_Print(PRT_ERROR, "couldn't load weapon config %s\n", AI_Weapon_LogPath(requested));
                return NULL;
            }

            config->num_projectiles += 1;
        }
        else
        {
            BotLib_Print(PRT_ERROR, "unknown definition %s in %s\n", token.string, log_path);
            PC_FreeSource(source);
            FreeMemory(config);
            BotLib_Print(PRT_ERROR, "couldn't load weapon config %s\n", AI_Weapon_LogPath(requested));
            return NULL;
        }
    }

    PC_FreeSource(source);

    for (int i = 0; i < config->num_weapons; ++i)
    {
        bot_weapon_info_t *weapon = &config->weapons[i];
        weapon->number = i;

        if (weapon->name[0] == '\0')
        {
            BotLib_Print(PRT_ERROR, "weapon %d has no name in %s\n", i, log_path);
            FreeMemory(config);
            BotLib_Print(PRT_ERROR, "couldn't load weapon config %s\n", AI_Weapon_LogPath(requested));
            return NULL;
        }

        if (weapon->projectile[0] == '\0')
        {
            BotLib_Print(PRT_ERROR, "weapon %s has no projectile in %s\n", weapon->name, log_path);
            FreeMemory(config);
            BotLib_Print(PRT_ERROR, "couldn't load weapon config %s\n", AI_Weapon_LogPath(requested));
            return NULL;
        }

        bool found_projectile = false;
        for (int j = 0; j < config->num_projectiles; ++j)
        {
            bot_weapon_projectile_t *projectile = &config->projectiles[j];
            if (strcmp(projectile->name, weapon->projectile) == 0)
            {
                weapon->projectile_info = projectile;
                found_projectile = true;
                break;
            }
        }

        if (!found_projectile)
        {
            BotLib_Print(PRT_ERROR, "weapon %s uses undefined projectile in %s\n", weapon->name, log_path);
            FreeMemory(config);
            BotLib_Print(PRT_ERROR, "couldn't load weapon config %s\n", AI_Weapon_LogPath(requested));
            return NULL;
        }
    }

    if (config->num_weapons == 0)
    {
        BotLib_Print(PRT_WARNING, "no weapon info loaded\n");
    }

    ai_weapon_library_t *library = (ai_weapon_library_t *)GetClearedMemory(sizeof(ai_weapon_library_t));
    if (library == NULL)
    {
        BotLib_Print(PRT_ERROR, "[ai_weapon] failed to allocate library wrapper\n");
        FreeMemory(config);
        BotLib_Print(PRT_ERROR, "couldn't load weapon config %s\n", AI_Weapon_LogPath(requested));
        return NULL;
    }

    library->config = config;
    g_active_weapon_config = config;
    strncpy(library->source_path, log_path, sizeof(library->source_path) - 1);
    library->source_path[sizeof(library->source_path) - 1] = '\0';

    BotLib_Print(PRT_MESSAGE, "loaded %s\n", library->source_path);
    return library;
}

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

const bot_weapon_config_t *AI_GetWeaponConfig(const ai_weapon_library_t *library)
{
    return (library != NULL) ? library->config : NULL;
}

ai_weapon_weights_t *AI_LoadWeaponWeights(const char *filename)
{
    bot_weight_config_t *config = ReadWeightConfig(filename);
    if (config == NULL)
    {
        return NULL;
    }

    const bot_weapon_config_t *definitions = g_active_weapon_config;
    if (definitions == NULL)
    {
        BotLib_Print(PRT_ERROR,
                     "[ai_weapon] unable to compile weapon weights without an active weapon config (%s)\n",
                     (filename != NULL) ? filename : "<null>");
        FreeWeightConfig(config);
        return NULL;
    }

    ai_weapon_weights_t *weights = (ai_weapon_weights_t *)GetClearedMemory(sizeof(ai_weapon_weights_t));
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
        weights->index_by_weapon = (int *)GetClearedMemory(sizeof(int) * (size_t)weights->index_count);
        if (weights->index_by_weapon == NULL)
        {
            AI_FreeWeaponWeights(weights);
            return NULL;
        }

        for (int i = 0; i < weights->index_count; ++i)
        {
            weights->index_by_weapon[i] = -1;
        }

        bool mismatch = false;
        for (int i = 0; i < weights->index_count; ++i)
        {
            const bot_weapon_info_t *weapon = &definitions->weapons[i];
            int weight_index = BotWeight_FindIndex(config, weapon->name);
            if (weight_index < 0)
            {
                BotLib_Print(PRT_WARNING,
                             "item info %d \"%s\" has no fuzzy weight\n",
                             weapon->number,
                             weapon->name);
                mismatch = true;
                break;
            }

            weights->index_by_weapon[i] = weight_index;
        }

        if (mismatch)
        {
            AI_FreeWeaponWeights(weights);
            return NULL;
        }
    }

    return weights;
}

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

float AI_WeaponWeightForClient(const ai_weapon_weights_t *weights, int weapon_index)
{
    if (weights == NULL || weights->config == NULL)
    {
        return 0.0f;
    }

    if (weapon_index < 0 || weapon_index >= weights->index_count || weights->index_by_weapon == NULL)
    {
        return 0.0f;
    }

    int weight_index = weights->index_by_weapon[weapon_index];
    if (weight_index < 0)
    {
        if (weights->definitions != NULL && weapon_index < weights->definitions->num_weapons)
        {
            const bot_weapon_info_t *weapon = &weights->definitions->weapons[weapon_index];
            BotLib_Print(PRT_WARNING,
                         "item info %d \"%s\" has no fuzzy weight\n",
                         weapon->number,
                         weapon->name);
        }
        else
        {
            BotLib_Print(PRT_WARNING,
                         "[ai_weapon] weapon slot %d missing fuzzy weight binding\n",
                         weapon_index);
        }
        return 0.0f;
    }

    return FuzzyWeight(NULL, weights->config, weight_index);
}

