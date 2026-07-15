#include <assert.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include <stdarg.h>
#include <string.h>
#include <float.h>
#include <math.h>

#include "shared/q_platform.h"
#include "q2bridge/aas_translation.h"
#include "q2bridge/botlib.h"
#include "q2bridge/bridge.h"
#include "q2bridge/bridge_config.h"
#include "q2bridge/update_translator.h"
#include "botlib/common/l_libvar.h"
#include "botlib/common/l_log.h"
#include "botlib/common/l_memory.h"
#include "botlib/aas/aas_map.h"
#include "botlib/aas/aas_local.h"
#include "botlib/aas/aas_sound.h"
#include "botlib/ai_chat/ai_chat.h"
#include "botlib/ai_character/bot_character.h"
#include "botlib/ai/ai_dm.h"
#include "botlib/ai_weight/bot_weight.h"
#include "botlib/ai_goal/ai_goal.h"
#include "botlib/ai/goal_move_orchestrator.h"
#include "botlib/ai_move/mover_catalogue.h"
#include "bot_interface_assets.h"
#include "botlib/ea/ea_local.h"
#include "botlib/precomp/l_precomp.h"
#include "botlib_interface.h"
#include "bot_interface.h"
#include "bot_state.h"

static void BotInterface_Printf(int priority, const char *fmt, ...);


static bot_import_t g_botImportStorage;
static bot_import_t *g_botImport = NULL;
static bot_chatstate_t *g_botInterfaceConsoleChat = NULL;
static botlib_import_table_t g_botInterfaceImportTable;

typedef struct botinterface_map_cache_s
{
    char map_name[MAX_FILEPATH];
    botinterface_asset_list_t models;
    botinterface_asset_list_t sounds;
    botinterface_asset_list_t images;
} botinterface_map_cache_t;

typedef struct botinterface_entity_snapshot_s
{
    qboolean valid;
    bot_updateentity_t state;
} botinterface_entity_snapshot_t;

#define BOT_INTERFACE_MAX_ENTITIES 1024

static botinterface_map_cache_t g_botInterfaceMapCache;
static botinterface_entity_snapshot_t g_botInterfaceEntityCache[BOT_INTERFACE_MAX_ENTITIES];
static float g_botInterfaceFrameTime = 0.0f;
static unsigned int g_botInterfaceFrameNumber = 0;
static bool g_botInterfaceDebugDrawEnabled = false;

#define CHARACTERISTIC_EASY_FRAGGER 42
#define CHARACTERISTIC_ALERTNESS 43
#define CHARACTERISTIC_CHAT_CPM 14
#define CHARACTERISTIC_CHAT_REPLY 22

enum bot_battle_inventory_slot_e
{
	BOT_BATTLE_INVENTORY_CELLS = 20,
	BOT_BATTLE_INVENTORY_HEALTH = 41,
	BOT_BATTLE_ENEMY_HORIZONTAL_DIST = 200,
	BOT_BATTLE_ENEMY_HEIGHT = 201,
	BOT_BATTLE_USING_QUAD = 204,
	BOT_BATTLE_USING_INVULNERABILITY = 205,
	BOT_BATTLE_USING_REBREATHER = 207,
	BOT_BATTLE_USING_ENVIRONMENTSUIT = 208,
	BOT_BATTLE_USING_POWERSCREEN = 210,
	BOT_BATTLE_USING_POWERSHIELD = 211,
	BOT_BATTLE_ENEMY_BLASTER = 230,
	BOT_BATTLE_ENEMY_SHOTGUN = 231,
	BOT_BATTLE_ENEMY_SUPERSHOTGUN = 232,
	BOT_BATTLE_ENEMY_MACHINEGUN = 233,
	BOT_BATTLE_ENEMY_CHAINGUN = 234,
	BOT_BATTLE_ENEMY_GRENADELAUNCHER = 235,
	BOT_BATTLE_ENEMY_ROCKETLAUNCHER = 236,
	BOT_BATTLE_ENEMY_HYPERBLASTER = 237,
	BOT_BATTLE_ENEMY_RAILGUN = 238,
	BOT_BATTLE_ENEMY_BFG10K = 239,
	BOT_BATTLE_ENEMY_GRENADES = 240,
	BOT_BATTLE_ENEMY_GRAPPLE = 241,
	BOT_BATTLE_ENEMY_QUAD = 245,
	BOT_BATTLE_ENEMY_INVULNERABILITY = 246,
	BOT_BATTLE_ENEMY_POWERSCREEN = 247,
};

#define BOT_BATTLE_POWER_ARMOR_GRACE 0.9

#define BOT_CONSOLE_MATCH_CONTEXT 7UL
#define BOT_CONSOLE_MATCH_DEATH 1
#define BOT_CONSOLE_MATCH_HELP 3
#define BOT_CONSOLE_MATCH_ACCOMPANY 4
#define BOT_CONSOLE_MATCH_DEFEND_KEY_AREA 5
#define BOT_CONSOLE_MATCH_RUSH_BASE 6
#define BOT_CONSOLE_MATCH_GET_FLAG 7
#define BOT_CONSOLE_MATCH_START_TEAM_LEADERSHIP 8
#define BOT_CONSOLE_MATCH_STOP_TEAM_LEADERSHIP 9
#define BOT_CONSOLE_MATCH_WAIT 10
#define BOT_CONSOLE_MATCH_WHAT_ARE_YOU_DOING 11
#define BOT_CONSOLE_MATCH_JOIN_SUBTEAM 12
#define BOT_CONSOLE_MATCH_LEAVE_SUBTEAM 13
#define BOT_CONSOLE_MATCH_CREATE_FORMATION 14
#define BOT_CONSOLE_MATCH_FORMATION_POSITION 15
#define BOT_CONSOLE_MATCH_FORMATION_SPACE 16
#define BOT_CONSOLE_MATCH_DO_FORMATION 17
#define BOT_CONSOLE_MATCH_DISMISS 18
#define BOT_CONSOLE_MATCH_CAMP 19
#define BOT_CONSOLE_MATCH_CHECKPOINT 20
#define BOT_CONSOLE_MATCH_PATROL 21
#define BOT_CONSOLE_MATCH_VICTIM 0
#define BOT_CONSOLE_MATCH_NETNAME 0
#define BOT_CONSOLE_MATCH_ADDRESSEE 1
#define BOT_CONSOLE_MATCH_TEAMMATE 3
#define BOT_CONSOLE_MATCH_KEYAREA 4
#define BOT_CONSOLE_MATCH_NUMBER 4
#define BOT_CONSOLE_MATCH_POSITION 4
#define BOT_CONSOLE_MATCH_TIME 5
#define BOT_CONSOLE_MATCH_NAME 5
#define BOT_CONSOLE_MATCH_MORE 5
#define BOT_CONSOLE_MATCH_ITEM 2
#define BOT_CONSOLE_MATCH_ME 100
#define BOT_CONSOLE_MATCH_SUBTYPE_NEARITEM 0x01
#define BOT_CONSOLE_MATCH_SUBTYPE_ADDRESSED 0x02
#define BOT_CONSOLE_MATCH_SUBTYPE_FEET 0x08
#define BOT_CONSOLE_MATCH_SUBTYPE_TIME 0x10
#define BOT_CONSOLE_MATCH_SUBTYPE_HERE 0x20
#define BOT_CONSOLE_MATCH_SUBTYPE_THERE 0x40
#define BOT_CONSOLE_MATCH_SUBTYPE_I 0x80
#define BOT_CONSOLE_MATCH_SUBTYPE_MORE 0x100
#define BOT_CONSOLE_MATCH_SUBTYPE_BACK 0x200
#define BOT_CONSOLE_MATCH_SUBTYPE_REVERSE 0x400

#define BOT_CONSOLE_TIME_CONTEXT 8UL
#define BOT_CONSOLE_TEAMMATE_CONTEXT 16UL
#define BOT_CONSOLE_PATROL_CONTEXT 64UL
#define BOT_CONSOLE_MATCH_PATROL_KEYAREA 104
#define BOT_CONSOLE_MATCH_MINUTES 105
#define BOT_CONSOLE_MATCH_SECONDS 106
#define BOT_CONSOLE_PATROL_LOOP 0x01
#define BOT_CONSOLE_PATROL_REVERSE 0x02
#define BOT_CONSOLE_DEFAULT_TEAM_GOAL_DURATION 300.0f
#define BOT_CONSOLE_HELP_DURATION 60.0f
#define BOT_CONSOLE_ACCOMPANY_DURATION 240.0f
#define BOT_CONSOLE_DEFEND_DURATION 120.0f
#define BOT_CONSOLE_RUSH_BASE_DURATION 120.0f
#define BOT_CONSOLE_GET_FLAG_DURATION 180.0f
#define BOT_CONSOLE_ACCOMPANY_DISTANCE 112.0f

#define BOT_CONSOLE_ADDRESSEE_CONTEXT 32UL
#define BOT_CONSOLE_ADDRESSEE_EVERYONE 101
#define BOT_CONSOLE_ADDRESSEE_MULTIPLE_NAMES 102
#define BOT_CONSOLE_CHAT_TEAM 1
#define BOT_CONSOLE_EASY_NAME_CHARS 0x1cU

#define BOT_CONSOLE_TEAM_DMFLAGS 0xc0
#define BOT_CONSOLE_SKIN_TEAMS 0x40
#define BOT_CONSOLE_MODEL_TEAMS 0x80

#define BOT_CONSOLE_SYNONYM_BASE 3UL
#define BOT_CONSOLE_SYNONYM_CTF_RED 7UL
#define BOT_CONSOLE_SYNONYM_CTF_BLUE 11UL

static void BotAI_InitEnemyInfo(ai_dm_enemy_info_t *info)
{
    if (info == NULL)
    {
        return;
    }

    info->valid = false;
    info->visible = false;
    info->entity = -1;
    VectorClear(info->origin);
    VectorClear(info->velocity);
    info->distance = 0.0f;
    info->last_seen_time = -FLT_MAX;
    info->field_of_view = 0.0f;
    info->is_invisible = false;
    info->is_chatting = false;
    info->is_shooting = false;
    info->triggered_by_damage = false;
    info->in_field_of_view = false;
    info->has_line_of_sight = false;
}

static int BotAI_MaxTrackedClients(void)
{
    libvar_t *maxclients = Bridge_MaxClients();
    if (maxclients == NULL)
    {
        return MAX_CLIENTS;
    }

    int value = (int)maxclients->value;
    if (value < 0)
    {
        value = 0;
    }
    if (value > BOT_INTERFACE_MAX_ENTITIES)
    {
        value = BOT_INTERFACE_MAX_ENTITIES;
    }
    return value;
}

static float BotInterface_NormaliseAngle180(float angle)
{
    while (angle > 180.0f)
    {
        angle -= 360.0f;
    }
    while (angle < -180.0f)
    {
        angle += 360.0f;
    }
    return angle;
}

static void BotInterface_VectorToAngles(const vec3_t vector, vec3_t angles)
{
    if (angles == NULL || vector == NULL)
    {
        return;
    }

    float yaw;
    float pitch;

    if (vector[0] == 0.0f && vector[1] == 0.0f)
    {
        yaw = 0.0f;
        pitch = (vector[2] > 0.0f) ? 90.0f : 270.0f;
    }
    else
    {
        yaw = atan2f(vector[1], vector[0]) * (180.0f / (float)M_PI);
        if (yaw < 0.0f)
        {
            yaw += 360.0f;
        }

        float forward = sqrtf(vector[0] * vector[0] + vector[1] * vector[1]);
        pitch = atan2f(vector[2], forward) * (180.0f / (float)M_PI);
    }

    angles[PITCH] = pitch;
    angles[YAW] = yaw;
    angles[ROLL] = 0.0f;
}

static bool BotInterface_InFieldOfVision(const vec3_t viewangles, float fov, const vec3_t target_angles)
{
    if (viewangles == NULL || target_angles == NULL)
    {
        return false;
    }

    if (fov >= 360.0f)
    {
        return true;
    }

    float yaw_delta = BotInterface_NormaliseAngle180(viewangles[YAW] - target_angles[YAW]);
    float pitch_delta = BotInterface_NormaliseAngle180(viewangles[PITCH] - target_angles[PITCH]);
    float half_fov = fov * 0.5f;
    return fabsf(yaw_delta) <= half_fov && fabsf(pitch_delta) <= half_fov;
}

static void BotInterface_ClientEyePosition(const bot_client_state_t *state, vec3_t out)
{
    if (state == NULL || out == NULL)
    {
        return;
    }

    VectorCopy(state->last_client_update.origin, out);
    out[0] += state->last_client_update.viewoffset[0];
    out[1] += state->last_client_update.viewoffset[1];
    out[2] += state->last_client_update.viewoffset[2];
}

static bool BotInterface_HasLineOfSight(const vec3_t from,
                                        const vec3_t to,
                                        int viewer,
                                        int target)
{
    if (from == NULL || to == NULL)
    {
        return false;
    }

    vec3_t start;
    vec3_t end;
    VectorCopy(from, start);
    VectorCopy(to, end);

    vec3_t mins = {0.0f, 0.0f, 0.0f};
    vec3_t maxs = {0.0f, 0.0f, 0.0f};
    bsp_trace_t trace = Q2_Trace(start, mins, maxs, end, viewer, MASK_SHOT);
    return trace.fraction >= 1.0f || trace.ent == target;
}

static bool BotInterface_SameTeam(const bot_client_state_t *lhs, const bot_client_state_t *rhs)
{
    if (lhs == NULL || rhs == NULL)
    {
        return false;
    }

    if (lhs->team < 0 || rhs->team < 0)
    {
        return false;
    }

    return lhs->team == rhs->team;
}

static bool BotInterface_IsChatting(const bot_client_state_t *state)
{
    if (state == NULL)
    {
        return false;
    }

    return (state->last_client_update.stats[STAT_LAYOUTS] & 1) != 0;
}

static bool BotInterface_IsInvisible(const bot_updateentity_t *snapshot)
{
    if (snapshot == NULL)
    {
        return false;
    }

    return (snapshot->renderfx & RF_TRANSLUCENT) != 0;
}

static bool BotInterface_IsShooting(const bot_updateentity_t *snapshot)
{
    if (snapshot == NULL)
    {
        return false;
    }

    const int weapon_fx = EF_BLASTER | EF_ROCKET | EF_GRENADE | EF_HYPERBLASTER | EF_BFG | EF_IONRIPPER |
                          EF_BLUEHYPERBLASTER | EF_TRACKER | EF_PLASMA;
    return (snapshot->effects & weapon_fx) != 0;
}

static float BotInterface_VectorLengthSquared(const vec3_t v)
{
    if (v == NULL)
    {
        return 0.0f;
    }

    return v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
}

static void BotAI_FindEnemy(bot_client_state_t *state, ai_dm_enemy_info_t *enemy)
{
    if (enemy == NULL)
    {
        return;
    }

    BotAI_InitEnemyInfo(enemy);

    if (state == NULL)
    {
        return;
    }

    if (!state->client_update_valid)
    {
        return;
    }

    bot_combat_state_t *combat = &state->combat;

    int current_health = state->last_client_update.stats[STAT_HEALTH];
    bool health_drop = false;
    if (combat->last_health_valid && current_health < combat->last_known_health)
    {
        combat->took_damage = true;
        combat->last_damage_amount = combat->last_known_health - current_health;
        combat->last_damage_time = g_botInterfaceFrameTime;
        health_drop = true;
    }
    else
    {
        combat->took_damage = false;
    }
    combat->last_known_health = current_health;
    combat->last_health_valid = true;

    float alertness = 0.5f;
    float easyfragger = 1.0f;
    if (state->character_handle > 0)
    {
        alertness = Characteristic_BFloat(state->character_handle, CHARACTERISTIC_ALERTNESS, 0.0f, 1.0f);
        easyfragger = Characteristic_BFloat(state->character_handle, CHARACTERISTIC_EASY_FRAGGER, 0.0f, 1.0f);
    }

    vec3_t self_origin;
    VectorCopy(state->last_client_update.origin, self_origin);
    vec3_t eye_position;
    BotInterface_ClientEyePosition(state, eye_position);

    int curenemy = combat->current_enemy;
    const bot_updateentity_t *current_snapshot = NULL;
    float current_enemy_dist_sq = FLT_MAX;
    if (curenemy >= 0 && curenemy < BOT_INTERFACE_MAX_ENTITIES &&
        g_botInterfaceEntityCache[curenemy].valid)
    {
        current_snapshot = &g_botInterfaceEntityCache[curenemy].state;
        vec3_t delta;
        VectorSubtract(current_snapshot->origin, self_origin, delta);
        current_enemy_dist_sq = BotInterface_VectorLengthSquared(delta);
    }
    else
    {
        curenemy = -1;
        combat->current_enemy = -1;
    }

    if (curenemy >= 0 && current_snapshot != NULL)
    {
        bool invisible = BotInterface_IsInvisible(current_snapshot);
        bool shooting = BotInterface_IsShooting(current_snapshot);
        bot_client_state_t *current_state = BotState_Get(curenemy);
        bool chatting = BotInterface_IsChatting(current_state);

        if (!(invisible && !shooting))
        {
            vec3_t to_enemy;
            VectorSubtract(current_snapshot->origin, eye_position, to_enemy);
            vec3_t target_angles;
            BotInterface_VectorToAngles(to_enemy, target_angles);

            float limited = (current_enemy_dist_sq > 810.0f * 810.0f)
                                ? 810.0f * 810.0f
                                : current_enemy_dist_sq;
            float fov = 90.0f + (limited / (810.0f * 9.0f));
            bool in_fov = BotInterface_InFieldOfVision(state->last_client_update.viewangles, fov, target_angles);
            bool has_los = BotInterface_HasLineOfSight(eye_position, current_snapshot->origin, state->client_number, curenemy);

            if (in_fov && has_los)
            {
                enemy->valid = true;
                enemy->visible = true;
                enemy->entity = curenemy;
                VectorCopy(current_snapshot->origin, enemy->origin);
                vec3_t displacement;
                VectorSubtract(current_snapshot->origin, current_snapshot->old_origin, displacement);
                VectorCopy(displacement, enemy->velocity);
                enemy->distance = sqrtf(current_enemy_dist_sq);
                enemy->last_seen_time = g_botInterfaceFrameTime;
                enemy->field_of_view = fov;
                enemy->is_invisible = invisible;
                enemy->is_chatting = chatting;
                enemy->is_shooting = shooting;
                enemy->triggered_by_damage = health_drop;
                enemy->in_field_of_view = in_fov;
                enemy->has_line_of_sight = has_los;

                combat->current_enemy = curenemy;
                combat->enemy_visible = true;
                combat->enemy_visible_time = g_botInterfaceFrameTime;
                combat->enemy_last_seen_time = g_botInterfaceFrameTime;
                VectorCopy(current_snapshot->origin, combat->last_enemy_origin);
                VectorCopy(displacement, combat->last_enemy_velocity);
                combat->enemy_death_time = -FLT_MAX;
                return;
            }
        }
    }

    bool had_visible_enemy = combat->enemy_visible;
    combat->enemy_visible = false;
    if (had_visible_enemy)
    {
        combat->enemy_death_time = g_botInterfaceFrameTime;
    }

    int max_clients = BotAI_MaxTrackedClients();
    float max_range = 900.0f + alertness * 4000.0f;
    float max_range_sq = max_range * max_range;

    for (int ent = 0; ent < max_clients; ++ent)
    {
        if (ent == state->client_number || ent == curenemy)
        {
            continue;
        }

        if (ent < 0 || ent >= BOT_INTERFACE_MAX_ENTITIES)
        {
            continue;
        }

        if (!g_botInterfaceEntityCache[ent].valid)
        {
            continue;
        }

        const bot_updateentity_t *snapshot = &g_botInterfaceEntityCache[ent].state;
        vec3_t delta;
        VectorSubtract(snapshot->origin, self_origin, delta);
        float distance_sq = BotInterface_VectorLengthSquared(delta);
        if (distance_sq > max_range_sq)
        {
            continue;
        }

        if (curenemy >= 0 && distance_sq > current_enemy_dist_sq)
        {
            continue;
        }

        bot_client_state_t *other = BotState_Get(ent);
        if (BotInterface_SameTeam(state, other))
        {
            continue;
        }

        bool invisible = BotInterface_IsInvisible(snapshot);
        bool shooting = BotInterface_IsShooting(snapshot);

        if (invisible && !shooting)
        {
            continue;
        }

        bool chatting = BotInterface_IsChatting(other);
        if (easyfragger < 0.5f && chatting)
        {
            continue;
        }

        float limited = (distance_sq > 810.0f * 810.0f) ? 810.0f * 810.0f : distance_sq;
        float fov = (curenemy < 0 && (health_drop || shooting)) ? 360.0f : 90.0f + (limited / (810.0f * 9.0f));

        vec3_t to_enemy;
        VectorSubtract(snapshot->origin, eye_position, to_enemy);
        vec3_t target_angles;
        BotInterface_VectorToAngles(to_enemy, target_angles);
        bool in_fov = BotInterface_InFieldOfVision(state->last_client_update.viewangles, fov, target_angles);
        if (!in_fov)
        {
            continue;
        }

        bool has_los = BotInterface_HasLineOfSight(eye_position, snapshot->origin, state->client_number, ent);
        if (!has_los)
        {
            continue;
        }

        if (curenemy < 0 && distance_sq > (100.0f * 100.0f) && !health_drop && !shooting)
        {
            bool bot_in_enemy_fov = true;
            if (other != NULL)
            {
                vec3_t to_bot;
                VectorSubtract(self_origin, snapshot->origin, to_bot);
                vec3_t enemy_angles;
                BotInterface_VectorToAngles(to_bot, enemy_angles);
                bot_in_enemy_fov = BotInterface_InFieldOfVision(other->last_client_update.viewangles, 90.0f, enemy_angles);
            }

            if (!bot_in_enemy_fov)
            {
                continue;
            }
        }

        enemy->valid = true;
        enemy->visible = true;
        enemy->entity = ent;
        VectorCopy(snapshot->origin, enemy->origin);
        vec3_t displacement;
        VectorSubtract(snapshot->origin, snapshot->old_origin, displacement);
        VectorCopy(displacement, enemy->velocity);
        enemy->distance = sqrtf(distance_sq);
        enemy->last_seen_time = g_botInterfaceFrameTime;
        enemy->field_of_view = fov;
        enemy->is_invisible = invisible;
        enemy->is_chatting = chatting;
        enemy->is_shooting = shooting;
        enemy->triggered_by_damage = health_drop;
        enemy->in_field_of_view = in_fov;
        enemy->has_line_of_sight = has_los;

        bool enemy_changed = (combat->current_enemy != ent);
        combat->current_enemy = ent;
        combat->enemy_visible = true;
        combat->enemy_visible_time = g_botInterfaceFrameTime;
        combat->enemy_last_seen_time = g_botInterfaceFrameTime;
        combat->enemy_death_time = -FLT_MAX;
        VectorCopy(snapshot->origin, combat->last_enemy_origin);
        VectorCopy(displacement, combat->last_enemy_velocity);
        if (enemy_changed && curenemy >= 0)
        {
            combat->enemy_sight_time = g_botInterfaceFrameTime - 2.0f;
        }
        else
        {
            combat->enemy_sight_time = g_botInterfaceFrameTime;
        }

        return;
    }
}

static void BotInterface_SynchroniseCombatState(bot_client_state_t *state)
{
    if (state == NULL || state->dm_state == NULL)
    {
        return;
    }

    ai_dm_metrics_t metrics = {0};
    AI_DMState_GetMetrics(state->dm_state, &metrics);

    bot_combat_state_t *combat = &state->combat;
    combat->current_enemy = metrics.enemy_entity;
    combat->revenge_enemy = metrics.revenge_enemy;
    combat->revenge_kills = metrics.revenge_kills;
    combat->enemy_visible = metrics.enemy_visible;
    combat->enemy_visible_time = metrics.enemyvisible_time;
    combat->enemy_sight_time = metrics.enemysight_time;
    combat->enemy_death_time = metrics.enemydeath_time;
    combat->enemy_last_seen_time = metrics.enemyposition_time;
    VectorCopy(metrics.last_enemy_origin, combat->last_enemy_origin);
    VectorCopy(metrics.last_enemy_velocity, combat->last_enemy_velocity);
}

static char *BotInterface_CopyString(const char *text);

static void BotInterface_FreeAssetList(botinterface_asset_list_t *list)
{
    if (list == NULL)
    {
        return;
    }

    if (list->entries != NULL)
    {
        for (size_t index = 0; index < list->count; ++index)
        {
            free(list->entries[index]);
        }

        free(list->entries);
    }

    list->entries = NULL;
    list->count = 0;
}

static bool BotInterface_CopyAssetList(botinterface_asset_list_t *target,
                                       int count,
                                       char *source[])
{
    if (target == NULL)
    {
        return false;
    }

    BotInterface_FreeAssetList(target);

    if (count <= 0 || source == NULL)
    {
        target->entries = NULL;
        target->count = 0;
        return true;
    }

    size_t allocation = (size_t)count;
    char **table = (char **)calloc(allocation, sizeof(char *));
    if (table == NULL)
    {
        return false;
    }

    for (size_t index = 0; index < allocation; ++index)
    {
        if (source[index] == NULL)
        {
            continue;
        }

        table[index] = BotInterface_CopyString(source[index]);
        if (table[index] == NULL)
        {
            for (size_t rollback = 0; rollback < index; ++rollback)
            {
                free(table[rollback]);
            }

            free(table);
            return false;
        }
    }

    target->entries = table;
    target->count = allocation;
    return true;
}

static void BotInterface_ResetEntityCache(void)
{
    for (size_t index = 0; index < BOT_INTERFACE_MAX_ENTITIES; ++index)
    {
        g_botInterfaceEntityCache[index].valid = qfalse;
    }
}

static void BotInterface_ResetMapCache(void)
{
    BotMove_MoverCatalogueReset();
    BotInterface_FreeAssetList(&g_botInterfaceMapCache.models);
    BotInterface_FreeAssetList(&g_botInterfaceMapCache.sounds);
    BotInterface_FreeAssetList(&g_botInterfaceMapCache.images);
    g_botInterfaceMapCache.map_name[0] = '\0';
}

/*
=============
BotInterface_RecordMapAssets

Atomically refreshes cached asset tables and preserves the map name for NULL refreshes.
=============
*/
static bool BotInterface_RecordMapAssets(const char *mapname,
	int modelindexes,
	char *modelindex[],
	int soundindexes,
	char *soundindex[],
	int imageindexes,
	char *imageindex[])
{
	botinterface_asset_list_t models = {0};
	botinterface_asset_list_t sounds = {0};
	botinterface_asset_list_t images = {0};

	if (!BotInterface_CopyAssetList(&models, modelindexes, modelindex) ||
		!BotInterface_CopyAssetList(&sounds, soundindexes, soundindex) ||
		!BotInterface_CopyAssetList(&images, imageindexes, imageindex))
	{
		BotInterface_FreeAssetList(&models);
		BotInterface_FreeAssetList(&sounds);
		BotInterface_FreeAssetList(&images);
		return false;
	}

	BotInterface_FreeAssetList(&g_botInterfaceMapCache.models);
	BotInterface_FreeAssetList(&g_botInterfaceMapCache.sounds);
	BotInterface_FreeAssetList(&g_botInterfaceMapCache.images);
	g_botInterfaceMapCache.models = models;
	g_botInterfaceMapCache.sounds = sounds;
	g_botInterfaceMapCache.images = images;

	if (mapname != NULL)
	{
		strncpy(g_botInterfaceMapCache.map_name,
			mapname,
			sizeof(g_botInterfaceMapCache.map_name) - 1);
		g_botInterfaceMapCache.map_name[
			sizeof(g_botInterfaceMapCache.map_name) - 1] = '\0';
	}

	return true;
}

/*
=============
BotInterface_ModelNameForIndex

Resolves a live client gun model index through the map model cache.
=============
*/
static const char *BotInterface_ModelNameForIndex(int modelindex)
{
	if (modelindex < 0 ||
		g_botInterfaceMapCache.models.entries == NULL ||
		(size_t)modelindex >= g_botInterfaceMapCache.models.count)
	{
		return NULL;
	}

	return g_botInterfaceMapCache.models.entries[modelindex];
}

/*
=============
BotInterface_ImageNameForIndex

Resolves a client stat image index and mirrors ImageFromIndex's empty-string
fallback for an unset, unused, or out-of-range slot.
=============
*/
static const char *BotInterface_ImageNameForIndex(int imageindex)
{
	if (imageindex < 0 ||
		g_botInterfaceMapCache.images.entries == NULL ||
		(size_t)imageindex >= g_botInterfaceMapCache.images.count ||
		g_botInterfaceMapCache.images.entries[imageindex] == NULL)
	{
		return "";
	}

	return g_botInterfaceMapCache.images.entries[imageindex];
}

/*
=============
BotAI_UpdateBattleInventory

Reconstructs retail sub_10021020: health, timed powerups, and the short
power-armor activity window are projected into the fuzzy-weight inventory.
=============
*/
static void BotAI_UpdateBattleInventory(bot_client_state_t *state)
{
	if (state == NULL)
	{
		return;
	}

	bot_updateclient_t *update = &state->last_client_update;
	int *inventory = update->inventory;
	const float now = g_botInterfaceFrameTime;

	inventory[BOT_BATTLE_INVENTORY_HEALTH] =
		(int)update->stats[STAT_HEALTH];

	int timer_image = (int)update->stats[STAT_TIMER_ICON];
	if (timer_image != 0)
	{
		const char *image_name = BotInterface_ImageNameForIndex(timer_image);
		float expiry = now + (float)update->stats[STAT_TIMER];
		if (Q_stricmp(image_name, "p_quad") == 0)
		{
			state->quad_time = expiry;
		}
		else if (Q_stricmp(image_name, "p_invulnerability") == 0)
		{
			state->invulnerability_time = expiry;
		}
		else if (Q_stricmp(image_name, "p_rebreather") == 0)
		{
			state->rebreather_time = expiry;
		}
		else if (Q_stricmp(image_name, "p_envirosuit") == 0)
		{
			state->environmentsuit_time = expiry;
		}
	}

	inventory[BOT_BATTLE_USING_QUAD] =
		(int)((double)state->quad_time - (double)now);
	if (inventory[BOT_BATTLE_USING_QUAD] <= 0)
	{
		inventory[BOT_BATTLE_USING_QUAD] = 0;
	}

	inventory[BOT_BATTLE_USING_INVULNERABILITY] =
		(int)((double)state->invulnerability_time - (double)now);
	if (inventory[BOT_BATTLE_USING_INVULNERABILITY] <= 0)
	{
		inventory[BOT_BATTLE_USING_INVULNERABILITY] = 0;
	}

	inventory[BOT_BATTLE_USING_REBREATHER] =
		(int)((double)state->rebreather_time - (double)now);
	if (inventory[BOT_BATTLE_USING_REBREATHER] <= 0)
	{
		inventory[BOT_BATTLE_USING_REBREATHER] = 0;
	}

	inventory[BOT_BATTLE_USING_ENVIRONMENTSUIT] =
		(int)((double)state->environmentsuit_time - (double)now);
	if (inventory[BOT_BATTLE_USING_ENVIRONMENTSUIT] <= 0)
	{
		inventory[BOT_BATTLE_USING_ENVIRONMENTSUIT] = 0;
	}

	int armor_image = (int)update->stats[STAT_ARMOR_ICON];
	if (armor_image != 0)
	{
		const char *image_name = BotInterface_ImageNameForIndex(armor_image);
		if (Q_stricmp(image_name, "i_powershield") == 0)
		{
			state->power_armor_time = now;
		}

		if ((double)state->power_armor_time >
			(double)now - BOT_BATTLE_POWER_ARMOR_GRACE)
		{
			int cells = inventory[BOT_BATTLE_INVENTORY_CELLS];
			inventory[BOT_BATTLE_USING_POWERSCREEN] = cells;
			inventory[BOT_BATTLE_USING_POWERSHIELD] = cells;
		}
		else
		{
			inventory[BOT_BATTLE_USING_POWERSCREEN] = 0;
			inventory[BOT_BATTLE_USING_POWERSHIELD] = 0;
		}
	}
}

/*
=============
BotAI_UpdateEnemyBattleInventory

Reconstructs retail sub_10021290: enemy displacement, Quake II weapon byte,
and the three observed effect flags are projected into battle inventory.
=============
*/
int BotAI_UpdateEnemyBattleInventory(bot_client_state_t *state,
	int enemy_entity)
{
	if (state == NULL)
	{
		return qfalse;
	}

	aas_entityinfo_t entity_info;
	AAS_EntityInfo(enemy_entity, &entity_info);

	vec3_t displacement;
	VectorSubtract(entity_info.origin,
		state->last_client_update.origin,
		displacement);
	int *inventory = state->last_client_update.inventory;
	inventory[BOT_BATTLE_ENEMY_HEIGHT] = (int)displacement[2];
	displacement[2] = 0.0f;
	double horizontal_square =
		(double)displacement[0] * (double)displacement[0] +
		(double)displacement[1] * (double)displacement[1];
	inventory[BOT_BATTLE_ENEMY_HORIZONTAL_DIST] =
		(int)sqrt(horizontal_square);

	memset(&inventory[BOT_BATTLE_ENEMY_BLASTER],
		0,
		12U * sizeof(inventory[0]));

	int weapon = (entity_info.skinnum >> 8) & 0xff;
	switch (weapon)
	{
		case 1:
			inventory[BOT_BATTLE_ENEMY_BLASTER] = 1;
			break;
		case 2:
			inventory[BOT_BATTLE_ENEMY_SHOTGUN] = 1;
			break;
		case 3:
			inventory[BOT_BATTLE_ENEMY_SUPERSHOTGUN] = 1;
			break;
		case 4:
			inventory[BOT_BATTLE_ENEMY_MACHINEGUN] = 1;
			break;
		case 5:
			inventory[BOT_BATTLE_ENEMY_CHAINGUN] = 1;
			break;
		case 6:
			inventory[BOT_BATTLE_ENEMY_GRENADES] = 1;
			break;
		case 7:
			inventory[BOT_BATTLE_ENEMY_GRENADELAUNCHER] = 1;
			break;
		case 8:
			inventory[BOT_BATTLE_ENEMY_ROCKETLAUNCHER] = 1;
			break;
		case 9:
			inventory[BOT_BATTLE_ENEMY_HYPERBLASTER] = 1;
			break;
		case 10:
			inventory[BOT_BATTLE_ENEMY_RAILGUN] = 1;
			break;
		case 11:
			inventory[BOT_BATTLE_ENEMY_BFG10K] = 1;
			break;
		case 12:
			inventory[BOT_BATTLE_ENEMY_GRAPPLE] = 1;
			break;
		default:
			break;
	}

	inventory[BOT_BATTLE_ENEMY_INVULNERABILITY] =
		(entity_info.effects & EF_PENT) != 0;
	inventory[BOT_BATTLE_ENEMY_QUAD] =
		(entity_info.effects & EF_QUAD) != 0;
	inventory[BOT_BATTLE_ENEMY_POWERSCREEN] =
		(entity_info.effects & EF_POWERSCREEN) != 0;
	return qtrue;
}

static void BotInterface_ResetFrameQueues(void)
{
    AAS_SoundSubsystem_ResetFrameEvents();
}

static void BotInterface_ResetGoalSnapshot(bot_client_state_t *state)
{
    if (state == NULL)
    {
        return;
    }

    state->goal_snapshot_count = 0;
    memset(state->goal_snapshot, 0, sizeof(state->goal_snapshot));
}

static void BotInterface_UpdateGoalSnapshot(bot_client_state_t *state)
{
    if (state == NULL)
    {
        return;
    }

    BotInterface_ResetGoalSnapshot(state);

    if (state->goal_handle <= 0)
    {
        return;
    }

    bot_goal_t goal = {0};
    if (AI_GoalBotlib_GetTopGoal(state->goal_handle, &goal))
    {
        state->goal_snapshot[state->goal_snapshot_count++] = goal;
    }

    if (state->goal_snapshot_count < (int)(sizeof(state->goal_snapshot) / sizeof(state->goal_snapshot[0])) &&
        AI_GoalBotlib_GetSecondGoal(state->goal_handle, &goal))
    {
        if (state->goal_snapshot_count == 0 || state->goal_snapshot[0].number != goal.number)
        {
            state->goal_snapshot[state->goal_snapshot_count++] = goal;
        }
    }
}

static const bot_goal_t *BotInterface_FindSnapshotGoal(const bot_client_state_t *state, int number)
{
    if (state == NULL || state->goal_snapshot_count <= 0)
    {
        return NULL;
    }

    for (int i = 0; i < state->goal_snapshot_count; ++i)
    {
        if (state->goal_snapshot[i].number == number)
        {
            return &state->goal_snapshot[i];
        }
    }

    return NULL;
}

static float BotInterface_NormaliseDirection(vec3_t out, const vec3_t in)
{
    if (out == NULL || in == NULL)
    {
        return 0.0f;
    }

    float length = sqrtf(in[0] * in[0] + in[1] * in[1] + in[2] * in[2]);
    if (length <= 0.0001f)
    {
        VectorClear(out);
        return 0.0f;
    }

    float inv = 1.0f / length;
    out[0] = in[0] * inv;
    out[1] = in[1] * inv;
    out[2] = in[2] * inv;
    return length;
}

static void BotInterface_BuildMoveCommand(bot_input_t *out_input,
                                          const vec3_t from,
                                          const vec3_t to)
{
    if (out_input == NULL || from == NULL || to == NULL)
    {
        return;
    }

    vec3_t delta;
    VectorSubtract(to, from, delta);
    float length = BotInterface_NormaliseDirection(out_input->dir, delta);
    out_input->speed = length;
    out_input->actionflags = 0;
}

static int BotInterface_RebuildGoalCandidates(bot_client_state_t *state)
{
	if (state == NULL || state->goal_state == NULL)
	{
		return BLERR_INVALIDIMPORT;
	}

	AI_GoalState_ClearCandidates(state->goal_state);

	for (int index = 0; index < state->goal_snapshot_count; ++index)
	{
		const bot_goal_t *goal = &state->goal_snapshot[index];
		ai_goal_candidate_t candidate = {0};
        candidate.item_index = goal->number;
        candidate.area = goal->areanum;
        candidate.travel_flags = TFL_DEFAULT;
        VectorCopy(goal->origin, candidate.origin);

        int start_area = AI_GoalState_GetCurrentArea(state->goal_state);
        int travel_time = 0;
		float weight = BotGoal_EvaluateStackGoal(state->goal_handle,
												 goal,
												 state->last_client_update.origin,
												 start_area,
												 state->last_client_update.inventory,
												 candidate.travel_flags,
												 &travel_time);
        if (weight <= -FLT_MAX)
        {
            continue;
        }

        candidate.base_weight = weight;
        AI_GoalState_AddCandidate(state->goal_state, &candidate);
    }

    return BLERR_NOERROR;
}

static float BotInterface_GoalWeight(void *ctx, const ai_goal_candidate_t *candidate)
{
    bot_client_state_t *state = (bot_client_state_t *)ctx;
    if (state == NULL || candidate == NULL)
    {
        return 0.0f;
    }

    const bot_goal_t *goal = BotInterface_FindSnapshotGoal(state, candidate->item_index);
    if (goal == NULL)
    {
        return candidate->base_weight;
    }

    int start_area = AI_GoalState_GetCurrentArea(state->goal_state);
    int travel_time = 0;
    float weight = BotGoal_EvaluateStackGoal(state->goal_handle,
                                             goal,
                                             state->last_client_update.origin,
                                             start_area,
                                             state->last_client_update.inventory,
                                             candidate->travel_flags,
                                             &travel_time);
    if (weight <= -FLT_MAX)
    {
        return candidate->base_weight;
    }

    return weight;
}

static float BotInterface_GoalTravelTime(void *ctx, int start_area, const ai_goal_candidate_t *candidate)
{
    bot_client_state_t *state = (bot_client_state_t *)ctx;
    if (state == NULL || candidate == NULL)
    {
        return 0.0f;
    }

    const bot_goal_t *goal = BotInterface_FindSnapshotGoal(state, candidate->item_index);
    if (goal == NULL)
    {
        return -1.0f;
    }

    if (start_area <= 0 || goal->areanum <= 0)
    {
        int travel_time = 0;
        BotGoal_EvaluateStackGoal(state->goal_handle,
                                  goal,
                                  state->last_client_update.origin,
                                  start_area,
                                  state->last_client_update.inventory,
                                  candidate->travel_flags,
                                  &travel_time);
        return (float)travel_time;
    }

    vec3_t origin;
    VectorCopy(state->last_client_update.origin, origin);
    int travel = AAS_AreaTravelTimeToGoalArea(start_area, origin, goal->areanum, candidate->travel_flags);
    return (float)travel;
}

static void BotInterface_GoalNotify(void *ctx, const ai_goal_selection_t *selection)
{
    bot_client_state_t *state = (bot_client_state_t *)ctx;
    if (state == NULL)
    {
        return;
    }

    if (selection == NULL || !selection->valid)
    {
        state->active_goal_number = 0;
        return;
    }

    state->active_goal_number = selection->candidate.item_index;
}

static int BotInterface_PrepareMoveState(bot_client_state_t *state, float thinktime)
{
    if (state == NULL || state->move_handle <= 0)
    {
        return BLERR_INVALIDIMPORT;
    }

    bot_initmove_t init = {0};
    VectorCopy(state->last_client_update.origin, init.origin);
    VectorCopy(state->last_client_update.velocity, init.velocity);
    VectorCopy(state->last_client_update.viewoffset, init.viewoffset);
    init.entitynum = state->client_number;
    init.client = state->client_number;
    init.thinktime = thinktime;
    init.presencetype = (state->last_client_update.pm_flags & PMF_DUCKED) ? PRESENCE_CROUCH : PRESENCE_NORMAL;

    if (state->last_client_update.pm_flags & PMF_ON_GROUND)
    {
        init.or_moveflags |= MFL_ONGROUND;
    }
    if ((state->last_client_update.pm_flags & PMF_TIME_TELEPORT) && state->last_client_update.pm_time > 0)
    {
        init.or_moveflags |= MFL_TELEPORTED;
    }
    if ((state->last_client_update.pm_flags & PMF_TIME_WATERJUMP) && state->last_client_update.pm_time > 0)
    {
        init.or_moveflags |= MFL_WATERJUMP;
    }

    VectorCopy(state->last_client_update.viewangles, init.viewangles);

    BotInitMoveState(state->move_handle, &init);

    bot_movestate_t *ms = BotMoveStateFromHandle(state->move_handle);
    if (ms != NULL && state->goal_state != NULL)
    {
        AI_GoalState_SetCurrentArea(state->goal_state, ms->areanum);
    }

    return BLERR_NOERROR;
}

static void BotInterface_ApplyMoveResult(bot_client_state_t *state,
                                         const bot_moveresult_t *result,
                                         bot_input_t *out_input)
{
    if (state == NULL || result == NULL || out_input == NULL)
    {
        return;
    }

    if (result->failure)
    {
        VectorClear(out_input->dir);
        out_input->speed = 0.0f;
    }
    else
    {
        VectorCopy(result->movedir, out_input->dir);
        out_input->speed = 400.0f;
    }

    out_input->actionflags = 0;
    if (result->flags & MOVERESULT_MOVEMENTWEAPON)
    {
        out_input->actionflags |= ACTION_ATTACK;
        state->current_weapon = result->weapon;
    }

    if (result->traveltype == TRAVEL_JUMP || result->traveltype == TRAVEL_ROCKETJUMP ||
        result->traveltype == TRAVEL_BFGJUMP || result->traveltype == TRAVEL_WATERJUMP)
    {
        out_input->actionflags |= ACTION_JUMP;
    }

    if (result->traveltype == TRAVEL_CROUCH)
    {
        out_input->actionflags |= ACTION_CROUCH;
    }

    if (result->flags & MOVERESULT_WAITING)
    {
        out_input->speed = 0.0f;
    }
}

static int BotInterface_MovePath(void *ctx,
                                 const ai_goal_selection_t *goal,
                                 ai_avoid_list_t *avoid,
                                 bot_input_t *out_input)
{
    bot_client_state_t *state = (bot_client_state_t *)ctx;
    if (state == NULL || out_input == NULL)
    {
        return BLERR_INVALIDIMPORT;
    }

    if (goal == NULL || !goal->valid)
    {
        VectorClear(out_input->dir);
        out_input->speed = 0.0f;
        out_input->actionflags = 0;
        if (state->move_state != NULL)
        {
            state->move_state->has_last_result = false;
        }
        state->has_move_result = false;
        return BLERR_NOERROR;
    }

    const bot_goal_t *target_goal = BotInterface_FindSnapshotGoal(state, goal->candidate.item_index);
    if (target_goal == NULL)
    {
        return BLERR_INVALIDIMPORT;
    }

    bot_moveresult_t result;
    BotClearMoveResult(&result);

    bool attempted_move = false;
    if (state->move_handle > 0 && target_goal->areanum > 0 && aasworld.loaded)
    {
        AI_MoveFrame(&result, state->move_handle, target_goal, goal->candidate.travel_flags);
        attempted_move = true;
    }

    if (!attempted_move)
    {
        BotInterface_BuildMoveCommand(out_input,
                                      state->last_client_update.origin,
                                      target_goal->origin);
        if (state->move_state != NULL)
        {
            state->move_state->has_last_result = false;
        }
        state->has_move_result = false;
        return BLERR_NOERROR;
    }

    if (result.failure)
    {
        if (avoid != NULL && state->goal_avoid_duration > 0.0f)
        {
            AI_AvoidList_Add(avoid,
                             goal->candidate.item_index,
                             g_botInterfaceFrameTime + state->goal_avoid_duration);
        }

        BotInterface_BuildMoveCommand(out_input,
                                      state->last_client_update.origin,
                                      target_goal->origin);
        if (state->move_state != NULL)
        {
            state->move_state->has_last_result = false;
        }
        state->has_move_result = false;
        return BLERR_INVALIDIMPORT;
    }

    BotInterface_ApplyMoveResult(state, &result, out_input);

    if (state->move_state != NULL)
    {
        state->move_state->last_result = result;
        state->move_state->has_last_result = true;
    }

    state->last_move_result = result;
    state->has_move_result = true;
    return BLERR_NOERROR;
}

static void BotInterface_MoveSubmit(void *ctx, int client, const bot_input_t *input)
{
    (void)ctx;
    EA_SubmitInput(client, input);
}

static void BotInterface_BeginFrame(float time)
{
    g_botInterfaceFrameTime = time;
	BotLib_LogSetTime(time);
    g_botInterfaceFrameNumber += 1U;
    Bridge_SetFrameTime(time);
    AAS_SoundSubsystem_SetFrameTime(time);
    BotInterface_ResetFrameQueues();
}

static void BotInterface_EnqueueSound(const vec3_t origin,
                                      int ent,
                                      int channel,
                                      int soundindex,
                                      float volume,
                                      float attenuation,
                                      float timeofs)
{
    if (!AAS_SoundSubsystem_RecordSound(origin,
                                        ent,
                                        channel,
                                        soundindex,
                                        volume,
                                        attenuation,
                                        timeofs))
    {
        BotInterface_Printf(PRT_WARNING,
                             "[bot_interface] BotAddSound: sound queue capacity exceeded\n");
    }
}

static void BotInterface_EnqueuePointLight(const vec3_t origin,
                                           int ent,
                                           float radius,
                                           float r,
                                           float g,
                                           float b,
                                           float time,
                                           float decay)
{
    if (!AAS_SoundSubsystem_RecordPointLight(origin, ent, radius, r, g, b, time, decay))
    {
        BotInterface_Printf(PRT_WARNING,
                             "[bot_interface] BotAddPointLight: point light queue capacity exceeded\n");
    }
}

typedef struct botinterface_import_libvar_s {
    char *name;
    char *value;
    struct botinterface_import_libvar_s *next;
} botinterface_import_libvar_t;

static botinterface_import_libvar_t *g_botInterfaceLibVars = NULL;

static void BotInterface_FreeImportLibVar(botinterface_import_libvar_t *entry)
{
    if (entry == NULL)
    {
        return;
    }

    free(entry->name);
    free(entry->value);
    free(entry);
}

static void BotInterface_ResetImportLibVars(void)
{
    botinterface_import_libvar_t *entry = g_botInterfaceLibVars;
    while (entry != NULL)
    {
        botinterface_import_libvar_t *next = entry->next;
        BotInterface_FreeImportLibVar(entry);
        entry = next;
    }

    g_botInterfaceLibVars = NULL;
}

static botinterface_import_libvar_t *BotInterface_FindImportLibVar(const char *name)
{
    botinterface_import_libvar_t *entry = g_botInterfaceLibVars;
    while (entry != NULL)
    {
        if (entry->name != NULL && name != NULL && strcmp(entry->name, name) == 0)
        {
            return entry;
        }

        entry = entry->next;
    }

    return NULL;
}

static botinterface_import_libvar_t *BotInterface_EnsureImportLibVar(const char *name)
{
    if (name == NULL)
    {
        return NULL;
    }

    botinterface_import_libvar_t *entry = BotInterface_FindImportLibVar(name);
    if (entry != NULL)
    {
        return entry;
    }

    entry = calloc(1, sizeof(*entry));
    if (entry == NULL)
    {
        return NULL;
    }

    entry->name = BotInterface_CopyString(name);
    if (entry->name == NULL)
    {
        free(entry);
        return NULL;
    }

    entry->value = BotInterface_CopyString("");
    if (entry->value == NULL)
    {
        free(entry->name);
        free(entry);
        return NULL;
    }

    entry->next = g_botInterfaceLibVars;
    g_botInterfaceLibVars = entry;
    return entry;
}

/*
=============
BotInterface_PrintWrapper

Formats print output and forwards it through the engine import table.
=============
*/
static void BotInterface_PrintWrapper(int type, const char *fmt, ...)
{
	if (fmt == NULL)
	{
		return;
	}

	if (g_botImport == NULL || g_botImport->Print == NULL)
	{
		return;
	}

	va_list args;
	va_start(args, fmt);

	char buffer[1024];
	vsnprintf(buffer, sizeof(buffer), fmt, args);

	va_end(args);

	buffer[sizeof(buffer) - 1] = '\0';

	const botlib_import_capture_t *capture = BotInterface_GetImportCapture();
	if (capture != NULL && capture->Print != NULL)
	{
		capture->Print(type, buffer);
	}

	g_botImport->Print(type, "%s", buffer);
}

/*
=============
BotInterface_DPrintWrapper

Formats developer output and forwards it through the engine print callback.
=============
*/
static void BotInterface_DPrintWrapper(const char *fmt, ...)
{
	if (fmt == NULL)
	{
		return;
	}

	if (g_botImport == NULL || g_botImport->Print == NULL)
	{
		return;
	}

	va_list args;
	va_start(args, fmt);

	char buffer[1024];
	vsnprintf(buffer, sizeof(buffer), fmt, args);

	va_end(args);

	buffer[sizeof(buffer) - 1] = '\0';

	const botlib_import_capture_t *capture = BotInterface_GetImportCapture();
	if (capture != NULL && capture->DPrint != NULL)
	{
		capture->DPrint(buffer);
	}

	g_botImport->Print(PRT_MESSAGE, "%s", buffer);
}

static void BotInterface_AddCommandWrapper(const char *name, void (*function)(void))
{
    if (g_botImport == NULL || g_botImport->AddCommand == NULL || name == NULL || function == NULL)
    {
        return;
    }

    g_botImport->AddCommand(name, function);
}

static void BotInterface_RemoveCommandWrapper(const char *name)
{
    if (g_botImport == NULL || g_botImport->RemoveCommand == NULL || name == NULL)
    {
        return;
    }

    g_botImport->RemoveCommand(name);
}

static int BotInterface_CmdArgcWrapper(void)
{
    if (g_botImport == NULL || g_botImport->CmdArgc == NULL)
    {
        return 0;
    }

    return g_botImport->CmdArgc();
}

static const char *BotInterface_CmdArgvWrapper(int index)
{
    if (g_botImport == NULL || g_botImport->CmdArgv == NULL)
    {
        return NULL;
    }

    return g_botImport->CmdArgv(index);
}

/*
=============
BotInterface_BotLibVarGetWrapper

Returns cached libvar values from the import-side shim table.
=============
*/
static int BotInterface_BotLibVarGetWrapper(const char *var_name, char *value, size_t size)
{
	int status = -1;

	if (value == NULL || size == 0)
	{
		return -1;
	}

	value[0] = '\0';

	if (var_name == NULL)
	{
		return -1;
	}

	botinterface_import_libvar_t *entry = BotInterface_FindImportLibVar(var_name);
	if (entry != NULL && entry->value != NULL)
	{
		size_t length = strlen(entry->value);
		if (length >= size)
		{
			length = size - 1;
		}

		memcpy(value, entry->value, length);
		value[length] = '\0';
		status = 0;
	}

	const botlib_import_capture_t *capture = BotInterface_GetImportCapture();
	if (capture != NULL && capture->BotLibVarGet != NULL)
	{
		capture->BotLibVarGet(var_name, value, status);
	}

	return status;
}

/*
=============
BotInterface_BotLibVarSetWrapper

Updates cached libvar values supplied through the import-side shim table.
=============
*/
static int BotInterface_BotLibVarSetWrapper(const char *var_name, const char *value)
{
	int status = -1;

	if (var_name == NULL || value == NULL)
	{
		return -1;
	}

	botinterface_import_libvar_t *entry = BotInterface_EnsureImportLibVar(var_name);
	if (entry != NULL)
	{
		char *copy = BotInterface_CopyString(value);
		if (copy != NULL)
		{
			free(entry->value);
			entry->value = copy;
			status = 0;
		}
	}

	const botlib_import_capture_t *capture = BotInterface_GetImportCapture();
	if (capture != NULL && capture->BotLibVarSet != NULL)
	{
		capture->BotLibVarSet(var_name, value, status);
	}

	return status;
}

static void BotInterface_BuildImportTable(bot_import_t *import_table)
{
    (void)import_table;

    BotInterface_ResetImportLibVars();

    g_botInterfaceImportTable.Print = BotInterface_PrintWrapper;
    g_botInterfaceImportTable.DPrint = BotInterface_DPrintWrapper;
    g_botInterfaceImportTable.BotLibVarGet = BotInterface_BotLibVarGetWrapper;
    g_botInterfaceImportTable.BotLibVarSet = BotInterface_BotLibVarSetWrapper;
    g_botInterfaceImportTable.AddCommand = BotInterface_AddCommandWrapper;
    g_botInterfaceImportTable.RemoveCommand = BotInterface_RemoveCommandWrapper;
    g_botInterfaceImportTable.CmdArgc = BotInterface_CmdArgcWrapper;
    g_botInterfaceImportTable.CmdArgv = BotInterface_CmdArgvWrapper;
}

typedef struct botlib_import_cache_entry_s {
    struct botlib_import_cache_entry_s *next;
    char *name;
    char *value;
} botlib_import_cache_entry_t;

static botlib_import_cache_entry_t *g_botImportCache = NULL;
static botlib_import_table_t g_botlibImportTable = {0};

static char *BotInterface_CopyString(const char *text)
{
    if (text == NULL)
    {
        return NULL;
    }

    size_t length = strlen(text);
    char *copy = (char *)malloc(length + 1);
    if (copy == NULL)
    {
        return NULL;
    }

    memcpy(copy, text, length);
    copy[length] = '\0';
    return copy;
}

static void BotInterface_FreeImportCache(void)
{
    botlib_import_cache_entry_t *entry = g_botImportCache;
    while (entry != NULL)
    {
        botlib_import_cache_entry_t *next = entry->next;
        free(entry->name);
        free(entry->value);
        free(entry);
        entry = next;
    }

    g_botImportCache = NULL;
}

static bool BotInterface_UpdateImportCache(const char *name, const char *value)
{
    if (name == NULL || value == NULL)
    {
        return false;
    }

    for (botlib_import_cache_entry_t *entry = g_botImportCache; entry != NULL; entry = entry->next)
    {
        if (strcmp(entry->name, name) == 0)
        {
            char *copy = BotInterface_CopyString(value);
            if (copy == NULL)
            {
                return false;
            }

            free(entry->value);
            entry->value = copy;
            return true;
        }
    }

    botlib_import_cache_entry_t *fresh = (botlib_import_cache_entry_t *)calloc(1, sizeof(*fresh));
    if (fresh == NULL)
    {
        return false;
    }

    fresh->name = BotInterface_CopyString(name);
    fresh->value = BotInterface_CopyString(value);
    if (fresh->name == NULL || fresh->value == NULL)
    {
        free(fresh->name);
        free(fresh->value);
        free(fresh);
        return false;
    }

    fresh->next = g_botImportCache;
    g_botImportCache = fresh;
    return true;
}

/*
=============
BotInterface_BotLibVarGetShim

Fetches cached libvar values for the bootstrapped import table.
=============
*/
static int BotInterface_BotLibVarGetShim(const char *name, char *buffer, size_t buffer_size)
{
	int status = BLERR_INVALIDIMPORT;

	if (name == NULL || buffer == NULL || buffer_size == 0)
	{
		return BLERR_INVALIDIMPORT;
	}

	for (botlib_import_cache_entry_t *entry = g_botImportCache; entry != NULL; entry = entry->next)
	{
		if (strcmp(entry->name, name) == 0)
		{
			strncpy(buffer, entry->value, buffer_size - 1);
			buffer[buffer_size - 1] = '\0';
			status = BLERR_NOERROR;
			break;
		}
	}

	if (status != BLERR_NOERROR)
	{
		buffer[0] = '\0';
	}

	const botlib_import_capture_t *capture = BotInterface_GetImportCapture();
	if (capture != NULL && capture->BotLibVarGet != NULL)
	{
		capture->BotLibVarGet(name, buffer, status);
	}

	return status;
}

/*
=============
BotInterface_BotLibVarSetShim

Stores cached libvar values for the bootstrapped import table.
=============
*/
static int BotInterface_BotLibVarSetShim(const char *name, const char *value)
{
	int status = BLERR_INVALIDIMPORT;

	if (name == NULL || value == NULL)
	{
		return BLERR_INVALIDIMPORT;
	}

	if (BotInterface_UpdateImportCache(name, value))
	{
		status = BLERR_NOERROR;
	}

	const botlib_import_capture_t *capture = BotInterface_GetImportCapture();
	if (capture != NULL && capture->BotLibVarSet != NULL)
	{
		capture->BotLibVarSet(name, value, status);
	}

	return status;
}

/*
=============
BotInterface_PrintShim

Formats and forwards print output during the import table handshake.
=============
*/
static void BotInterface_PrintShim(int priority, const char *fmt, ...)
{
	if (g_botImport == NULL || g_botImport->Print == NULL || fmt == NULL)
	{
		return;
	}

	va_list args;
	va_start(args, fmt);

	char buffer[1024];
	vsnprintf(buffer, sizeof(buffer), fmt, args);

	va_end(args);

	const botlib_import_capture_t *capture = BotInterface_GetImportCapture();
	if (capture != NULL && capture->Print != NULL)
	{
		capture->Print(priority, buffer);
	}

	g_botImport->Print(priority, "%s", buffer);
}

/*
=============
BotInterface_DPrintShim

Formats and forwards developer output during the import table handshake.
=============
*/
static void BotInterface_DPrintShim(const char *fmt, ...)
{
	if (g_botImport == NULL || g_botImport->Print == NULL || fmt == NULL)
	{
		return;
	}

	va_list args;
	va_start(args, fmt);

	char buffer[1024];
	vsnprintf(buffer, sizeof(buffer), fmt, args);

	va_end(args);

	const botlib_import_capture_t *capture = BotInterface_GetImportCapture();
	if (capture != NULL && capture->DPrint != NULL)
	{
		capture->DPrint(buffer);
	}

	g_botImport->Print(PRT_MESSAGE, "%s", buffer);
}

/*
=============
BotInterface_InitialiseImportTable

Prepares the bootstrapped import table used during library setup.
=============
*/
static void BotInterface_InitialiseImportTable(bot_import_t *imports,
	size_t import_size)
{
	memset(&g_botImportStorage, 0, sizeof(g_botImportStorage));
	if (imports != NULL)
	{
		size_t copy_size = import_size;
		if (copy_size > sizeof(g_botImportStorage))
		{
			copy_size = sizeof(g_botImportStorage);
		}
		memcpy(&g_botImportStorage, imports, copy_size);
		g_botImport = &g_botImportStorage;
	}
	else
	{
		g_botImport = NULL;
	}

	memset(&g_botlibImportTable, 0, sizeof(g_botlibImportTable));
	g_botlibImportTable.Print = BotInterface_PrintShim;
	g_botlibImportTable.DPrint = BotInterface_DPrintShim;
	g_botlibImportTable.BotLibVarGet = BotInterface_BotLibVarGetShim;
	g_botlibImportTable.BotLibVarSet = BotInterface_BotLibVarSetShim;
	g_botlibImportTable.AddCommand = BotInterface_AddCommandWrapper;
	g_botlibImportTable.RemoveCommand = BotInterface_RemoveCommandWrapper;
	g_botlibImportTable.CmdArgc = BotInterface_CmdArgcWrapper;
	g_botlibImportTable.CmdArgv = BotInterface_CmdArgvWrapper;
}

static void BotInterface_PrintBanner(int priority, const char *message)
{
    if (message == NULL)
    {
        return;
    }

    if (g_botImport != NULL && g_botImport->Print != NULL)
    {
        g_botImport->Print(priority, "%s", message);
    }
    else
    {
        BotLib_Print(priority, "%s", message);
    }
}

static void BotInterface_Printf(int priority, const char *fmt, ...)
{
    if (g_botImport == NULL || g_botImport->Print == NULL || fmt == NULL)
    {
        return;
    }

    va_list args;
    va_start(args, fmt);

    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), fmt, args);

    va_end(args);

    g_botImport->Print(priority, "%s", buffer);
}

/*
=============
BotInterface_EnsureLibraryReady

Applies the retail shared bot-library setup guard and diagnostic.
=============
*/
static bool BotInterface_EnsureLibraryReady(const char *function_name)
{
	if (g_botImport == NULL)
	{
		return false;
	}

	if (!BotLibraryInitialized())
	{
		if (function_name != NULL)
		{
			BotInterface_Printf(PRT_ERROR,
				"%s: bot library used before being setup\n",
				function_name);
		}
		return false;
	}

	return true;
}

/*
=============
BotInterface_ValidateClientNumber

Applies the retail inclusive maxclients guard and exact diagnostic contract.
=============
*/
static qboolean BotInterface_ValidateClientNumber(int client, const char *function_name)
{
	int max_client_number = BotState_ClientCapacity();
	if (client >= 0 && client <= max_client_number)
	{
		return qtrue;
	}

	BotInterface_Printf(PRT_ERROR,
		"%s: invalid client number %d, [0, %d]\n",
		function_name,
		client,
		max_client_number);
	return qfalse;
}

static bot_chatstate_t *BotInterface_EnsureConsoleChatState(void)
{
    if (g_botInterfaceConsoleChat == NULL) {
        g_botInterfaceConsoleChat = BotAllocChatState();
        if (g_botInterfaceConsoleChat == NULL) {
            if (g_botImport != NULL && g_botImport->Print != NULL) {
                g_botImport->Print(PRT_ERROR, "[bot_interface] failed to allocate console chat state\n");
            }
        }
    }

    return g_botInterfaceConsoleChat;
}

static void BotInterface_Log(int priority, const char *functionName)
{
    if (functionName == NULL)
    {
        return;
    }

    BotLib_Print(priority, "[bot_interface] %s\n", functionName);
}

static char *BotVersion(void)
{
    static char version[] = "BotLib v0.96";

    return version;
}

/*
=============
BotSetupLibraryWrapper

Initialises the botlib bridge and emits the historical startup banners.
=============
*/
static int BotSetupLibraryWrapper(void)
{
	if (BotLibraryInitialized())
	{
		BotInterface_Printf(PRT_ERROR, "bot library already setup\n");
		return BLERR_LIBRARYALREADYSETUP;
	}

	BotInterface_PrintBanner(PRT_MESSAGE, "------- BotLib Initialization -------\n");
	BotInterface_PrintBanner(PRT_MESSAGE, "BotLib v0.96\n");
	BotInterface_SetImportTable(&g_botlibImportTable);

	int result = BotSetupLibrary();
	if (result != BLERR_NOERROR)
	{
		return result;
	}

	BotInterface_PrintBanner(PRT_MESSAGE, "-------------------------------------\n");
	return result;
}

/*
=============
BotShutdownLibraryWrapper

Tears down botlib state while preserving the retail guard and silent wrapper.
=============
*/
static int BotShutdownLibraryWrapper(void)
{
	if (!BotLibraryInitialized())
	{
		BotInterface_Printf(PRT_ERROR, "bot library already shutdown\n");
		return BLERR_LIBRARYNOTSETUP;
	}

	int result = BotShutdownLibrary();

	if (g_botInterfaceConsoleChat != NULL)
	{
		BotFreeChatState(g_botInterfaceConsoleChat);
		g_botInterfaceConsoleChat = NULL;
	}

	BotInterface_ResetMapCache();
	BotInterface_ResetEntityCache();
	BotInterface_ResetFrameQueues();
	g_botInterfaceDebugDrawEnabled = false;
	Q2Bridge_SetDebugLinesEnabled(false);

	return result;
}

static int BotInterface_BotSetupLibrary(void)
{
    assert(g_botImport != NULL);
    return BotSetupLibrary();
}

static int BotInterface_BotShutdownLibrary(void)
{
    assert(g_botImport != NULL);

    int status = BotShutdownLibrary();
    if (g_botInterfaceConsoleChat != NULL)
    {
        BotFreeChatState(g_botInterfaceConsoleChat);
        g_botInterfaceConsoleChat = NULL;
    }

    BotInterface_ResetMapCache();
    BotInterface_ResetEntityCache();
    BotInterface_ResetFrameQueues();
    g_botInterfaceDebugDrawEnabled = false;
    Q2Bridge_SetDebugLinesEnabled(false);

    AAS_Shutdown();
    BotInterface_FreeImportCache();
    BotInterface_SetImportTable(NULL);
    Q2Bridge_ClearImportTable();
    BotLib_LogShutdown();

    return status;
}

/*
=============
BotLibraryInitializedWrapper

Returns the retail AAS continuation-initialization state.
=============
*/
static int BotLibraryInitializedWrapper(void)
{
	return AAS_Initialized();
}

/*
=============
BotLibVarSetWrapper

Applies the retail local libvar update and unconditional zero return contract.
=============
*/
static int BotLibVarSetWrapper(char *var_name, char *value)
{
	(void)BotInterface_UpdateImportCache(var_name, value);
	LibVarSet(var_name, value);
	return BLERR_NOERROR;
}

static int BotInterface_BotLibraryInitialized(void)
{
    assert(g_botImport != NULL);
    return BotLibraryInitialized() ? 1 : 0;
}

static int BotInterface_BotLibVarSet(char *var_name, char *value)
{
    assert(g_botImport != NULL);
    BotInterface_Log(PRT_WARNING, __func__);
    (void)var_name;
    (void)value;
    return BLERR_NOERROR;
}

/*
=============
BotDefineWrapper

Adds a global precompiler define while preserving the retail return contract.
=============
*/
static int BotDefineWrapper(char *string)
{
	if (!PC_AddGlobalDefine(string))
	{
		BotInterface_Printf(PRT_ERROR,
			"couldn't add define %s\n",
			string);
	}

	return BLERR_NOERROR;
}

/*
=============
BotLoadMap

Loads a named map or refreshes retail asset-index tables when the name is NULL.
=============
*/
static int BotLoadMap(char *mapname,
	int modelindexes,
	char *modelindex[],
	int soundindexes,
	char *soundindex[],
	int imageindexes,
	char *imageindex[])
{
	if (g_botImport == NULL)
	{
		return BLERR_LIBRARYNOTSETUP;
	}

	if (!BotLibraryEnsureSetup("BotLoadMap"))
	{
		return BLERR_LIBRARYNOTSETUP;
	}

	if (mapname == NULL)
	{
		int status = AAS_LoadMap(NULL,
			modelindexes,
			modelindex,
			soundindexes,
			soundindex,
			imageindexes,
			imageindex);
		if (status != BLERR_NOERROR)
		{
			BotInterface_Printf(PRT_WARNING,
				"[bot_interface] BotLoadMap: failed to refresh AAS asset lists\n");
		}

		if (BotInterface_RecordMapAssets(NULL,
			modelindexes,
			modelindex,
			soundindexes,
			soundindex,
			imageindexes,
			imageindex))
		{
			if (!BotMove_MoverCatalogueFinalize(&g_botInterfaceMapCache.models))
			{
				BotInterface_Printf(PRT_WARNING,
					"[bot_interface] BotLoadMap: failed to refresh mover model indexes\n");
			}
			BotGoal_SetMapModelIndexes(modelindexes, modelindex);
		}
		else
		{
			BotInterface_Printf(PRT_WARNING,
				"[bot_interface] BotLoadMap: failed to refresh asset lists\n");
		}

		return BLERR_NOERROR;
	}

	BotInterface_PrintBanner(PRT_MESSAGE,
		"------------ Map Loading ------------\n");

	Bridge_ResetCachedUpdates();
	BotInterface_ResetFrameQueues();
	BotInterface_ResetEntityCache();
	BotInterface_ResetMapCache();
	BotGoal_SetMapModelIndexes(0, NULL);
	TranslateEntity_SetWorldLoaded(qfalse);

	if (!BotInterface_RecordMapAssets(mapname,
		modelindexes,
		modelindex,
		soundindexes,
		soundindex,
		imageindexes,
		imageindex))
	{
		BotInterface_Printf(PRT_WARNING,
			"[bot_interface] BotLoadMap: failed to record asset lists for %s\n",
			mapname);
		return BLERR_INVALIDIMPORT;
	}
	BotGoal_SetMapModelIndexes(modelindexes, modelindex);

	int status = AAS_LoadMap(mapname,
							 modelindexes,
							 modelindex,
							 soundindexes,
							 soundindex,
							 imageindexes,
							 imageindex);
	if (status != BLERR_NOERROR)
	{
		return status;
	}

	if (!BotMove_MoverCatalogueFinalize(&g_botInterfaceMapCache.models))
	{
		BotInterface_Printf(PRT_WARNING,
							 "[bot_interface] BotLoadMap: failed to finalize mover catalogue for %s\n",
							 mapname);
		return BLERR_INVALIDIMPORT;
	}

	BotInitLevelItems();
	BotInterface_PrintBanner(PRT_MESSAGE,
		"-------------------------------------\n");
	BotState_ResetAllForNewMap();

	TranslateEntity_SetWorldLoaded(qtrue);
	TranslateEntity_SetCurrentTime(0.0f);

	return BLERR_NOERROR;
}

/*
=============
BotSetupClient

Initializes a bot client while preserving the retail boolean export ABI.
=============
*/
static int BotSetupClient(int client, bot_settings_t *settings)
{
	if (!BotInterface_EnsureLibraryReady("BotSetupClient"))
	{
		return qfalse;
	}

	if (!BotInterface_ValidateClientNumber(client, "BotSetupClient"))
	{
		return qfalse;
	}

	if (BotState_Get(client) != NULL)
	{
		BotInterface_Printf(PRT_FATAL, "client %d already setup\n", client);
		return qfalse;
	}

	if (settings == NULL)
	{
		BotInterface_Printf(PRT_ERROR, "[bot_interface] BotSetupClient: NULL settings pointer for client %d\n", client);
		return qfalse;
	}

	bot_client_state_t *state = BotState_Create(client);
	if (state == NULL)
	{
		BotInterface_Printf(PRT_ERROR, "[bot_interface] BotSetupClient: failed to allocate state for client %d\n", client);
		return qfalse;
	}

	int status = BLERR_NOERROR;

	state->weapon_state = BotAllocWeaponState();
	if (state->weapon_state <= 0)
	{
		BotInterface_Printf(PRT_ERROR,
							"[bot_interface] BotSetupClient: failed to allocate weapon state for client %d\n",
							client);
		BotState_Destroy(client);
		return qfalse;
	}

	memcpy(&state->settings, settings, sizeof(*settings));

	int character_handle = BotLoadNamedCharacter(settings->characterfile,
		settings->charactername,
		1.0f);
	if (character_handle <= 0)
	{
		BotInterface_Printf(PRT_ERROR,
							"[bot_interface] BotSetupClient: failed to load character '%s' for client %d\n",
							settings->characterfile,
							client);
		BotState_Destroy(client);
		return qfalse;
	}

	status = BotState_AttachCharacter(state, character_handle);
	if (status != BLERR_NOERROR)
	{
		BotInterface_Printf(PRT_ERROR,
							"[bot_interface] BotSetupClient: failed to attach character resources for client %d\n",
							client);
		BotState_Destroy(client);
		return qfalse;
	}

	state->goal_state = AI_GoalState_Create();
	if (state->goal_state == NULL)
	{
		BotInterface_Printf(PRT_ERROR,
							"[bot_interface] BotSetupClient: failed to allocate goal state for client %d\n",
							client);
		BotState_Destroy(client);
		return qfalse;
	}

	state->goal_handle = AI_GoalBotlib_AllocState(client);
	if (state->goal_handle <= 0)
	{
		BotInterface_Printf(PRT_ERROR,
							"[bot_interface] BotSetupClient: failed to allocate goal handle for client %d\n",
							client);
		AI_GoalState_Destroy(state->goal_state);
		state->goal_state = NULL;
		BotState_Destroy(client);
		return qfalse;
	}

	AI_GoalBotlib_ResetState(state->goal_handle);

	const char *item_weights_file =
		AI_CharacteristicAsString(state->character, BOT_CHARACTERISTIC_ITEMWEIGHTS);
	status = AI_GoalBotlib_LoadItemWeights(state->goal_handle, item_weights_file);
	if (status != BLERR_NOERROR)
	{
		BotInterface_Printf(PRT_ERROR,
							"[bot_interface] BotSetupClient: failed to load item weights '%s' for client %d\n",
							item_weights_file != NULL ? item_weights_file : "<null>",
							client);
		BotState_Destroy(client);
		return qfalse;
	}

	state->move_state = AI_MoveState_Create();
	if (state->move_state == NULL)
	{
		BotInterface_Printf(PRT_ERROR,
							"[bot_interface] BotSetupClient: failed to allocate move state for client %d\n",
							client);
		AI_GoalState_Destroy(state->goal_state);
		state->goal_state = NULL;
		BotState_Destroy(client);
		return qfalse;
	}

	state->move_handle = BotAllocMoveState();
	if (state->move_handle <= 0)
	{
		BotInterface_Printf(PRT_ERROR,
							"[bot_interface] BotSetupClient: failed to allocate move handle for client %d\n",
							client);
		BotState_Destroy(client);
		return qfalse;
	}

	ai_goal_services_t goal_services = {
		.weight_fn = BotInterface_GoalWeight,
		.travel_time_fn = BotInterface_GoalTravelTime,
		.notify_fn = BotInterface_GoalNotify,
		.area_fn = NULL,
		.userdata = state,
		.avoid_duration = 5.0f,
	};
	AI_GoalState_SetServices(state->goal_state, &goal_services);
	state->goal_avoid_duration = goal_services.avoid_duration;

	ai_move_services_t move_services = {
		.path_fn = BotInterface_MovePath,
		.submit_fn = BotInterface_MoveSubmit,
		.userdata = state,
	};
	AI_MoveState_SetServices(state->move_state, &move_services);

	AI_MoveState_LinkAvoidList(state->move_state, AI_GoalState_GetAvoidList(state->goal_state));

	state->dm_state = AI_DMState_Create(client);
	if (state->dm_state == NULL)
	{
		BotInterface_Printf(PRT_ERROR,
							"[bot_interface] BotSetupClient: failed to allocate DM state for client %d\n",
							client);
		BotState_Destroy(client);
		return qfalse;
	}

	Bridge_ClearClientSlot(client);
	Bridge_SetClientActive(client, qtrue);
	BotState_SetActive(state, true);
	return qtrue;
}

/*
=============
BotShutdownClient

Shuts down a bot client slot and clears the bridge cache.
=============
*/
static int BotShutdownClient(int client)
{
	if (!BotInterface_EnsureLibraryReady("BotShutdownClient"))
	{
		return BLERR_LIBRARYNOTSETUP;
	}

	if (!BotInterface_ValidateClientNumber(client, "BotShutdownClient"))
	{
		return BLERR_INVALIDCLIENTNUMBER;
	}

	bot_client_state_t *state = BotState_Get(client);
	if (state == NULL || !state->active)
	{
		BotInterface_Printf(PRT_ERROR, "client %d already shutdown\n", client);
		Bridge_ClearClientSlot(client);
		return BLERR_AICLIENTALREADYSHUTDOWN;
	}

	BotState_SetActive(state, false);
	BotState_Destroy(client);
	Bridge_SetClientActive(client, qfalse);
	Bridge_ClearClientSlot(client);
	return BLERR_NOERROR;
}

/*
=============
BotMoveClient

Migrates an active bot client to a new slot and preserves bridge state.
=============
*/
static int BotMoveClient(int oldclnum, int newclnum)
{
	if (!BotInterface_EnsureLibraryReady("BotMoveClient"))
	{
		return BLERR_LIBRARYNOTSETUP;
	}

	if (!BotInterface_ValidateClientNumber(oldclnum, "BotMoveClient, parm0"))
	{
		return BLERR_INVALIDCLIENTNUMBER;
	}

	if (!BotInterface_ValidateClientNumber(newclnum, "BotMoveClient, parm1"))
	{
		return BLERR_INVALIDCLIENTNUMBER;
	}

	bot_client_state_t *state = BotState_Get(oldclnum);
	if (state == NULL || !state->active)
	{
		BotInterface_Printf(PRT_FATAL,
			"tried to move inactive bot client\n");
		return BLERR_AIMOVEINACTIVECLIENT;
	}

	if (BotState_Get(newclnum) != NULL)
	{
		BotInterface_Printf(PRT_FATAL,
			"tried to move client to active client\n");
		return BLERR_AIMOVETOACTIVECLIENT;
	}

	BotState_Move(oldclnum, newclnum);
	Bridge_ClearClientSlot(newclnum);
	Bridge_SetClientActive(newclnum, qtrue);
	int status = Bridge_MoveClientSlot(oldclnum, newclnum);
	if (status != BLERR_NOERROR)
	{
		BotInterface_Printf(PRT_ERROR,
		                    "[bot_interface] BotMoveClient: bridge move failed for %d -> %d\n",
		                    oldclnum,
		                    newclnum);
		Bridge_ClearClientSlot(newclnum);
		Bridge_SetClientActive(newclnum, qfalse);
		Bridge_SetClientActive(oldclnum, qtrue);
		BotState_Move(newclnum, oldclnum);
		return status;
	}
	Bridge_SetClientActive(oldclnum, qfalse);
	Bridge_SetClientActive(newclnum, qtrue);

	return BLERR_NOERROR;
}

/*
=============
BotClientSettings

Stores game-provided presentation settings for a client slot.
=============
*/
static int BotClientSettings(int client, bot_clientsettings_t *settings)
{
	if (!BotInterface_EnsureLibraryReady("BotClientSettings"))
	{
		return BLERR_LIBRARYNOTSETUP;
	}

	if (!BotInterface_ValidateClientNumber(client, "BotClientSettings"))
	{
		return BLERR_INVALIDCLIENTNUMBER;
	}

	if (settings == NULL)
	{
		BotInterface_Printf(PRT_ERROR, "[bot_interface] BotClientSettings: NULL output buffer\n");
		return BLERR_INVALIDIMPORT;
	}

	return BotState_SetClientSettings(client, settings);
}

/*
=============
BotSettings

Updates the stored bot setup configuration for an active client.
=============
*/
static int BotSettings(int client, bot_settings_t *settings)
{
	if (!BotInterface_EnsureLibraryReady("BotSettings"))
	{
		return BLERR_LIBRARYNOTSETUP;
	}

	if (!BotInterface_ValidateClientNumber(client, "BotSettings"))
	{
		return BLERR_INVALIDCLIENTNUMBER;
	}

	bot_client_state_t *state = BotState_Get(client);
	if (state == NULL || !state->active)
	{
		BotInterface_Printf(PRT_FATAL,
			"tried to update settings of inactive client\n");
		return BLERR_SETTINGSINACTIVECLIENT;
	}

	if (settings == NULL)
	{
		BotInterface_Printf(PRT_ERROR, "[bot_interface] BotSettings: NULL output buffer\n");
		return BLERR_INVALIDIMPORT;
	}

	memcpy(&state->settings, settings, sizeof(state->settings));
	return BLERR_NOERROR;
}

static int BotStartFrame(float time)
{
	if (!BotInterface_EnsureLibraryReady("BotStartFrame"))
	{
		return BLERR_LIBRARYNOTSETUP;
	}

    AAS_FrameSynchronise(time);
    BotInterface_BeginFrame(time);
    AAS_UnlinkInvalidEntities();
    AAS_InvalidateEntities();
    AAS_ContinueInit(time);
    AAS_RouteFrameUpdate();
    AAS_ReachabilityFrameUpdate();

    aasworld.numFrames += 1;

    return BLERR_NOERROR;
}

static int BotUpdateClient(int client, bot_updateclient_t *buc)
{
	if (!BotInterface_EnsureLibraryReady("BotUpdateClient"))
	{
		return BLERR_LIBRARYNOTSETUP;
	}

	if (!BotInterface_ValidateClientNumber(client, "BotUpdateClient"))
	{
		return BLERR_INVALIDCLIENTNUMBER;
	}

    bot_client_state_t *state = BotState_Get(client);
    if (state == NULL || !state->active)
    {
		BotInterface_Printf(PRT_FATAL,
			"tried to updated inactive bot client\n");
        return BLERR_AIUPDATEINACTIVECLIENT;
    }

    int status = Bridge_UpdateClient(client, buc);
    if (status != BLERR_NOERROR)
    {
        return status;
    }

    AASClientFrame translated;
    if (!Bridge_ReadClientFrame(client, &translated))
    {
        return BLERR_AIUPDATEINACTIVECLIENT;
    }

    bot_updateclient_t quantised = {0};
    quantised.pm_type = translated.pm_type;
    VectorCopy(translated.origin, quantised.origin);
    VectorCopy(translated.velocity, quantised.velocity);
    VectorCopy(translated.delta_angles, quantised.delta_angles);
    quantised.pm_flags = translated.pm_flags;
    quantised.pm_time = translated.pm_time;
    quantised.gravity = translated.gravity;
    VectorCopy(translated.viewangles, quantised.viewangles);
    VectorCopy(translated.viewoffset, quantised.viewoffset);
    VectorCopy(translated.kick_angles, quantised.kick_angles);
    VectorCopy(translated.gunangles, quantised.gunangles);
    VectorCopy(translated.gunoffset, quantised.gunoffset);
    quantised.gunindex = translated.gunindex;
    quantised.gunframe = translated.gunframe;
    memcpy(quantised.blend, translated.blend, sizeof(quantised.blend));
    quantised.fov = translated.fov;
    quantised.rdflags = translated.rdflags;
    memcpy(quantised.stats, translated.stats, sizeof(quantised.stats));
    memcpy(quantised.inventory, translated.inventory, sizeof(quantised.inventory));

    if (buc != NULL)
    {
        *buc = quantised;
    }

    state->last_client_update = quantised;
    state->client_update_valid = true;
    state->last_update_time = translated.last_update_time;

    if (state->goal_state != NULL)
    {
        status = AI_GoalState_RecordClientUpdate(state->goal_state, &quantised);
        if (status != BLERR_NOERROR)
        {
            return status;
        }
    }

    return BLERR_NOERROR;
}

/*
=============
BotInterface_ValidateEntityNumber

Applies the shared retail entity-number guard before exported entity work.
=============
*/
static qboolean BotInterface_ValidateEntityNumber(int ent, const char *function_name)
{
	const botlib_library_variables_t *variables = BotInterface_GetLibraryVariables();
	int max_entity_number = BOT_INTERFACE_MAX_ENTITIES;
	if (variables != NULL)
	{
		max_entity_number = variables->maxentities;
	}

	if (ent >= 0 && ent <= max_entity_number)
	{
		return qtrue;
	}

	BotInterface_Printf(PRT_ERROR,
		"%s: invalid entity number %d, [0, %d]\n",
		function_name,
		ent,
		max_entity_number);
	return qfalse;
}

/*
=============
BotUpdateEntity

Validates and forwards an entity snapshot to the bridge and AAS world.
=============
*/
static int BotUpdateEntity(int ent, bot_updateentity_t *bue)
{
	if (!BotInterface_EnsureLibraryReady("BotUpdateEntity"))
	{
		return BLERR_LIBRARYNOTSETUP;
	}

	if (!BotInterface_ValidateEntityNumber(ent, "BotUpdateEntity"))
	{
		return BLERR_INVALIDENTITYNUMBER;
	}

	if (bue == NULL)
	{
		return AAS_UpdateEntity(ent, NULL);
	}

	int status = Bridge_UpdateEntity(ent, bue);
	if (status != BLERR_NOERROR)
	{
		return status;
	}

	AASEntityFrame translated = {0};
	if (!Bridge_ReadEntityFrame(ent, &translated))
	{
		return BLERR_INVALIDIMPORT;
	}

	status = AAS_UpdateEntity(ent, &translated);
	if (status != BLERR_NOERROR)
	{
		return status;
	}

	if (bue != NULL && ent >= 0 && ent < BOT_INTERFACE_MAX_ENTITIES)
	{
		g_botInterfaceEntityCache[ent].state = *bue;
		g_botInterfaceEntityCache[ent].valid = qtrue;
	}

	aasworld.entitiesValid = qtrue;
	return BLERR_NOERROR;
}

/*
=============
BotAddSound

Validates the source entity before recording a retail sound update.
=============
*/
static int BotAddSound(vec3_t origin,
	int ent,
	int channel,
	int soundindex,
	float volume,
	float attenuation,
	float timeofs)
{
	if (!BotInterface_EnsureLibraryReady("BotUpdateSound"))
	{
		return BLERR_LIBRARYNOTSETUP;
	}

	if (!BotInterface_ValidateEntityNumber(ent, "BotUpdateSound"))
	{
		return BLERR_INVALIDENTITYNUMBER;
	}

	if (soundindex < 0 || (size_t)soundindex >= g_botInterfaceMapCache.sounds.count)
	{
		BotInterface_Printf(PRT_ERROR,
			"[bot_interface] BotAddSound: invalid sound index %d (count %zu)\n",
			soundindex,
			g_botInterfaceMapCache.sounds.count);
		return BLERR_INVALIDSOUNDINDEX;
	}

	BotInterface_EnqueueSound(origin, ent, channel, soundindex, volume, attenuation, timeofs);
	return BLERR_NOERROR;
}

/*
=============
BotAddPointLight

Validates the source entity before recording a retail point light update.
=============
*/
static int BotAddPointLight(vec3_t origin,
	int ent,
	float radius,
	float r,
	float g,
	float b,
	float time,
	float decay)
{
	if (!BotInterface_EnsureLibraryReady("BotAddPointLight"))
	{
		return BLERR_LIBRARYNOTSETUP;
	}

	if (!BotInterface_ValidateEntityNumber(ent, "BotAddPointLight"))
	{
		return BLERR_INVALIDENTITYNUMBER;
	}

	BotInterface_EnqueuePointLight(origin, ent, radius, r, g, b, time, decay);
	return BLERR_NOERROR;
}

/*
=============
BotAI_ConsoleRandom

Returns the retail 15-bit random fraction used by console-message timing and
reply selection.
=============
*/
static float BotAI_ConsoleRandom(void)
{
	return (float)(rand() & 0x7fff) / 32767.0f;
}

/*
=============
BotAI_ConsoleSynonymContext

Builds Gladiator's normal/nearby synonym context and adds the CTF team bit
selected from the retail skin-name convention.
=============
*/
static unsigned long BotAI_ConsoleSynonymContext(const bot_client_state_t *state)
{
	if (state == NULL || LibVarGetValue("ctf") == 0.0f)
	{
		return BOT_CONSOLE_SYNONYM_BASE;
	}

	const char *skin = BotState_ClientSkin(state->client_number);
	if (skin != NULL && strstr(skin, "ctf_r") != NULL)
	{
		return BOT_CONSOLE_SYNONYM_CTF_RED;
	}

	return BOT_CONSOLE_SYNONYM_CTF_BLUE;
}

/*
=============
BotAI_IsOwnConsoleChat

Applies Gladiator's two bounded netname comparisons for plain and parenthesised
chat prefixes.
=============
*/
static bool BotAI_IsOwnConsoleChat(const bot_client_state_t *state,
	const char *message,
	const char *colon)
{
	if (state == NULL || message == NULL || colon == NULL || colon < message)
	{
		return false;
	}

	const char *bot_name = BotState_ClientName(state->client_number);
	if (bot_name == NULL)
	{
		bot_name = "";
	}

	size_t prefix_length = (size_t)(colon - message);
	if (strncmp(message, bot_name, prefix_length) == 0)
	{
		return true;
	}

	if (prefix_length >= 2U &&
		strncmp(message + 1, bot_name, prefix_length - 2U) == 0)
	{
		return true;
	}

	return false;
}

/*
=============
BotAI_ValidChatPosition

Reconstructs Gladiator's dead/ground, hazardous-contents, and world-floor
checks before a bot may stop to answer chat.
=============
*/
static bool BotAI_ValidChatPosition(const bot_client_state_t *state)
{
	if (state == NULL)
	{
		return false;
	}

	if (state->last_client_update.pm_type == PM_DEAD ||
		state->last_client_update.pm_type == PM_GIB)
	{
		return true;
	}

	if ((state->last_client_update.pm_flags & PMF_ON_GROUND) == 0)
	{
		return false;
	}

	vec3_t point;
	VectorCopy(state->last_client_update.origin, point);
	point[2] -= 24.0f;
	if ((AAS_PointContents(point) & (CONTENTS_LAVA | CONTENTS_SLIME)) != 0)
	{
		return false;
	}

	VectorCopy(state->last_client_update.origin, point);
	point[2] += 32.0f;
	if ((AAS_PointContents(point) & MASK_WATER) != 0)
	{
		return false;
	}

	vec3_t start;
	vec3_t end;
	vec3_t mins;
	vec3_t maxs;
	VectorCopy(state->last_client_update.origin, start);
	VectorCopy(state->last_client_update.origin, end);
	start[2] += 1.0f;
	end[2] -= 100.0f;
	AAS_PresenceTypeBoundingBox(PRESENCE_CROUCH, mins, maxs);

	/* Gladiator passes the literal PRESENCE_CROUCH/client pair to Trace. */
	bsp_trace_t trace = AAS_Trace(start,
		mins,
		maxs,
		end,
		PRESENCE_CROUCH,
		state->client_number);
	return trace.ent == 0;
}

/*
=============
BotAI_ConsoleTeamPlayIsOn

Reconstructs Gladiator's team-command gate from the model/skin team dmflags,
CTF, and explicit teamplay libvars.
=============
*/
static bool BotAI_ConsoleTeamPlayIsOn(void)
{
	int dmflags = (int)LibVarGetValue("dmflags");
	return (dmflags & BOT_CONSOLE_TEAM_DMFLAGS) != 0 ||
		LibVarGetValue("ctf") != 0.0f ||
		LibVarGetValue("teamplay") != 0.0f;
}

/*
=============
BotAI_FindExactConsoleClientByName

Finds the exact case-sensitive NETNAME source used by Gladiator's addressed
team-command gate.
=============
*/
static int BotAI_FindExactConsoleClientByName(const char *name)
{
	if (name == NULL || name[0] == '\0')
	{
		return -1;
	}

	for (int client = 0; client < BotState_ClientCapacity(); ++client)
	{
		const char *candidate = BotState_ClientName(client);
		if (candidate != NULL && candidate[0] != '\0' &&
			strcmp(candidate, name) == 0)
		{
			return client;
		}
	}

	return -1;
}

/*
=============
BotAI_ConsoleModelTeamMatches

Compares the model prefix before the Quake II skin separator.
=============
*/
static bool BotAI_ConsoleModelTeamMatches(const char *left, const char *right)
{
	if (left == NULL || right == NULL || left[0] == '\0' || right[0] == '\0')
	{
		return false;
	}

	const char *left_separator = strchr(left, '/');
	const char *right_separator = strchr(right, '/');
	size_t left_length = left_separator != NULL
		? (size_t)(left_separator - left)
		: strlen(left);
	size_t right_length = right_separator != NULL
		? (size_t)(right_separator - right)
		: strlen(right);
	return left_length == right_length &&
		Q_strnicmp(left, right, left_length) == 0;
}

/*
=============
BotAI_ConsoleSkinTeamMatches

Compares the skin suffix at the Quake II model/skin separator.
=============
*/
static bool BotAI_ConsoleSkinTeamMatches(const char *left, const char *right)
{
	if (left == NULL || right == NULL || left[0] == '\0' || right[0] == '\0')
	{
		return false;
	}

	const char *left_separator = strchr(left, '/');
	const char *right_separator = strchr(right, '/');
	return Q_stricmp(left_separator != NULL ? left_separator : left,
		right_separator != NULL ? right_separator : right) == 0;
}

/*
=============
BotAI_ConsoleClientIsTeammate

Uses the reconstructed semantic team slot first, then Gladiator's Quake II
CTF/model/skin presentation conventions when no semantic peer state exists.
=============
*/
static bool BotAI_ConsoleClientIsTeammate(const bot_client_state_t *state,
	int client)
{
	if (state == NULL || !BotState_ClientInRange(client))
	{
		return false;
	}
	if (client == state->client_number)
	{
		return true;
	}

	const bot_client_state_t *other = BotState_Get(client);
	if (other != NULL && state->team >= 0 && other->team >= 0)
	{
		return BotInterface_SameTeam(state, other);
	}

	const char *self_skin = BotState_ClientSkin(state->client_number);
	const char *other_skin = BotState_ClientSkin(client);
	if (LibVarGetValue("ctf") != 0.0f)
	{
		bool self_red = self_skin != NULL && strstr(self_skin, "ctf_r") != NULL;
		bool other_red = other_skin != NULL && strstr(other_skin, "ctf_r") != NULL;
		bool self_blue = self_skin != NULL && strstr(self_skin, "ctf_b") != NULL;
		bool other_blue = other_skin != NULL && strstr(other_skin, "ctf_b") != NULL;
		if (self_red || other_red || self_blue || other_blue)
		{
			return (self_red && other_red) || (self_blue && other_blue);
		}
	}

	int dmflags = (int)LibVarGetValue("dmflags");
	if ((dmflags & BOT_CONSOLE_MODEL_TEAMS) != 0)
	{
		return BotAI_ConsoleModelTeamMatches(self_skin, other_skin);
	}
	if ((dmflags & BOT_CONSOLE_SKIN_TEAMS) != 0 ||
		LibVarGetValue("teamplay") != 0.0f)
	{
		return BotAI_ConsoleSkinTeamMatches(self_skin, other_skin);
	}

	return false;
}

/*
=============
BotAI_ConsoleTeamPlayerCount

Counts named same-team clients for the retail unaddressed-command probability.
=============
*/
static int BotAI_ConsoleTeamPlayerCount(const bot_client_state_t *state)
{
	int count = 0;
	for (int client = 0; client < BotState_ClientCapacity(); ++client)
	{
		const char *name = BotState_ClientName(client);
		if (name != NULL && name[0] != '\0' &&
			BotAI_ConsoleClientIsTeammate(state, client))
		{
			++count;
		}
	}
	return count;
}

/*
=============
BotAI_ConsoleNameAddressesBot

Tests the case-insensitive substring relation against the bot name and its
current 32-byte subteam name.
=============
*/
static bool BotAI_ConsoleNameAddressesBot(const bot_client_state_t *state,
	const char *name)
{
	if (state == NULL || name == NULL || name[0] == '\0')
	{
		return false;
	}

	return StringContains(BotState_ClientName(state->client_number), name, 0) >= 0 ||
		StringContains(state->subteam, name, 0) >= 0;
}

/*
=============
BotAI_ConsoleAddressedToBot

Reconstructs sub_10026be0: validates the NETNAME as a teammate, resolves the
context-32 addressee list against the bot/subteam names, and applies the retail
one-over-other-teammates probability to unaddressed commands.
=============
*/
static bool BotAI_ConsoleAddressedToBot(bot_client_state_t *state,
	const bot_match_t *match)
{
	if (state == NULL || match == NULL)
	{
		return false;
	}

	char netname[BOT_CONSOLE_MESSAGE_STORAGE_CHARS];
	BotMatchVariable(match,
		BOT_CONSOLE_MATCH_NETNAME,
		netname,
		(int)sizeof(netname));
	int source_client = BotAI_FindExactConsoleClientByName(netname);
	if (source_client < 0 ||
		!BotAI_ConsoleClientIsTeammate(state, source_client))
	{
		return false;
	}

	if ((match->subtype & BOT_CONSOLE_MATCH_SUBTYPE_ADDRESSED) == 0)
	{
		int teammate_count = BotAI_ConsoleTeamPlayerCount(state);
		if (teammate_count <= 1)
		{
			return true;
		}
		return BotAI_ConsoleRandom() <= 1.0f / (float)(teammate_count - 1);
	}

	char addressee[BOT_CONSOLE_MESSAGE_STORAGE_CHARS];
	BotMatchVariable(match,
		BOT_CONSOLE_MATCH_ADDRESSEE,
		addressee,
		(int)sizeof(addressee));
	while (addressee[0] != '\0')
	{
		bot_match_t addressee_match;
		memset(&addressee_match, 0, sizeof(addressee_match));
		if (!BotFindMatch(addressee,
			&addressee_match,
			BOT_CONSOLE_ADDRESSEE_CONTEXT))
		{
			break;
		}

		if (addressee_match.type == BOT_CONSOLE_ADDRESSEE_EVERYONE)
		{
			return true;
		}

		char name[BOT_CONSOLE_MESSAGE_STORAGE_CHARS];
		BotMatchVariable(&addressee_match,
			BOT_CONSOLE_MATCH_TEAMMATE,
			name,
			(int)sizeof(name));
		if (BotAI_ConsoleNameAddressesBot(state, name))
		{
			return true;
		}

		if (addressee_match.type != BOT_CONSOLE_ADDRESSEE_MULTIPLE_NAMES)
		{
			break;
		}
		BotMatchVariable(&addressee_match,
			BOT_CONSOLE_MATCH_MORE,
			addressee,
			(int)sizeof(addressee));
		if (addressee[0] == '\0')
		{
			const char *remainder = addressee_match.string + strlen(name);
			if (strncmp(remainder, " and ", 5U) == 0)
			{
				remainder += 5;
			}
			else if (strncmp(remainder, ", ", 2U) == 0)
			{
				remainder += 2;
			}
			else
			{
				break;
			}
			strncpy(addressee, remainder, sizeof(addressee));
			addressee[sizeof(addressee) - 1U] = '\0';
		}
	}

	return false;
}

/*
=============
BotAI_FindConsoleClientByName

Finds the first case-insensitive exact client name, then the first client name
containing the requested text, matching Gladiator's team-command lookup.
=============
*/
static int BotAI_FindConsoleClientByName(const char *name)
{
	if (name == NULL)
	{
		return -1;
	}

	int max_clients = BotState_ClientCapacity();
	for (int client = 0; client < max_clients; ++client)
	{
		if (Q_stricmp(BotState_ClientName(client), name) == 0)
		{
			return client;
		}
	}

	for (int client = 0; client < max_clients; ++client)
	{
		if (StringContains(BotState_ClientName(client), name, 0) >= 0)
		{
			return client;
		}
	}

	return -1;
}

/*
=============
BotAI_UpdateConsoleLeadership

Applies Gladiator's paired start/stop team-leadership match side effects,
including its fuzzy client lookup and ST_I variable-selection behavior.
=============
*/
static void BotAI_UpdateConsoleLeadership(bot_client_state_t *state,
	const bot_match_t *match)
{
	if (state == NULL || match == NULL)
	{
		return;
	}

	char teammate[BOT_CONSOLE_MESSAGE_STORAGE_CHARS];
	BotMatchVariable(match,
		BOT_CONSOLE_MATCH_TEAMMATE,
		teammate,
		(int)sizeof(teammate));

	if (match->type == BOT_CONSOLE_MATCH_START_TEAM_LEADERSHIP)
	{
		if ((match->subtype & BOT_CONSOLE_MATCH_SUBTYPE_I) != 0)
		{
			strncpy(state->team_leader, teammate, sizeof(state->team_leader));
			state->team_leader[sizeof(state->team_leader) - 1U] = '\0';
			return;
		}

		int client = BotAI_FindConsoleClientByName(teammate);
		if (client >= 0)
		{
			strcpy(state->team_leader, BotState_ClientName(client));
		}
		return;
	}

	int client;
	if ((match->subtype & BOT_CONSOLE_MATCH_SUBTYPE_I) != 0)
	{
		char netname[BOT_CONSOLE_MESSAGE_STORAGE_CHARS];
		BotMatchVariable(match,
			BOT_CONSOLE_MATCH_NETNAME,
			netname,
			(int)sizeof(netname));
		client = BotAI_FindConsoleClientByName(netname);
	}
	else
	{
		client = BotAI_FindConsoleClientByName(teammate);
	}

	if (client >= 0 &&
		Q_stricmp(state->team_leader, BotState_ClientName(client)) == 0)
	{
		state->team_leader[0] = '\0';
	}
}

/*
=============
BotAI_ConsoleEnterInitialTeamChat

Constructs a single-variable initial chat and dispatches only a successfully
constructed pending message through Gladiator's team-chat path.
=============
*/
static void BotAI_ConsoleEnterInitialTeamChat(bot_client_state_t *state,
	const char *type,
	const char *variable)
{
	if (state == NULL || state->chat_state == NULL || type == NULL)
	{
		return;
	}

	if (BotInitialChat(state->chat_state, type, 0UL, variable, NULL) != 0)
	{
		BotEnterChat(state->chat_state,
			state->client_number,
			BOT_CONSOLE_CHAT_TEAM);
	}
}

/*
=============
BotAI_ConsoleEnterInitialTeamChat2

Constructs a two-variable retail initial chat and enters the team destination.
=============
*/
static void BotAI_ConsoleEnterInitialTeamChat2(bot_client_state_t *state,
	const char *type,
	const char *first,
	const char *second)
{
	if (state == NULL || state->chat_state == NULL || type == NULL)
	{
		return;
	}

	if (BotInitialChat(state->chat_state,
		type,
		0UL,
		first,
		second,
		NULL) != 0)
	{
		BotEnterChat(state->chat_state,
			state->client_number,
			BOT_CONSOLE_CHAT_TEAM);
	}
}

/*
=============
BotAI_ConsoleCreateWaypoint

Allocates the compact semantic mirror of Gladiator's named waypoint object.
=============
*/
static bot_console_waypoint_t *BotAI_ConsoleCreateWaypoint(const char *name,
	const vec3_t origin,
	int areanum)
{
	bot_console_waypoint_t *waypoint = calloc(1, sizeof(*waypoint));
	if (waypoint == NULL)
	{
		return NULL;
	}

	if (name != NULL)
	{
		strncpy(waypoint->name, name, sizeof(waypoint->name) - 1U);
		waypoint->name[sizeof(waypoint->name) - 1U] = '\0';
	}
	if (origin != NULL)
	{
		VectorCopy(origin, waypoint->goal.origin);
	}
	waypoint->goal.areanum = areanum;
	VectorSet(waypoint->goal.mins, -8.0f, -8.0f, -8.0f);
	VectorSet(waypoint->goal.maxs, 8.0f, 8.0f, 8.0f);
	return waypoint;
}

/*
=============
BotAI_ConsoleFindWaypoint

Finds a named checkpoint with Gladiator's case-insensitive comparison.
=============
*/
static bot_console_waypoint_t *BotAI_ConsoleFindWaypoint(
	bot_console_waypoint_t *waypoints,
	const char *name)
{
	if (name == NULL)
	{
		return NULL;
	}

	for (bot_console_waypoint_t *waypoint = waypoints;
		waypoint != NULL;
		waypoint = waypoint->next)
	{
		if (Q_stricmp(waypoint->name, name) == 0)
		{
			return waypoint;
		}
	}
	return NULL;
}

/*
=============
BotAI_ConsoleUnlinkCheckpoint

Unlinks one checkpoint before retail replaces a duplicate name.
=============
*/
static void BotAI_ConsoleUnlinkCheckpoint(bot_client_state_t *state,
	bot_console_waypoint_t *waypoint)
{
	if (state == NULL || waypoint == NULL)
	{
		return;
	}

	if (waypoint->prev != NULL)
	{
		waypoint->prev->next = waypoint->next;
	}
	else
	{
		state->checkpoints = waypoint->next;
	}
	if (waypoint->next != NULL)
	{
		waypoint->next->prev = waypoint->prev;
	}
	waypoint->next = NULL;
	waypoint->prev = NULL;
}

/*
=============
BotAI_ConsoleResolveTeamGoal

Resolves a key-area name through static level items and then remembered
checkpoints, matching sub_10026770's lookup order.
=============
*/
static bool BotAI_ConsoleResolveTeamGoal(bot_client_state_t *state,
	char *name,
	bot_goal_t *goal)
{
	if (state == NULL || name == NULL || goal == NULL)
	{
		return false;
	}

	int index = 0;
	while (name[0] != '\0')
	{
		bot_goal_t candidate;
		memset(&candidate, 0, sizeof(candidate));
		int next = BotGetLevelItemGoal(index, name, &candidate);
		if (next < 0)
		{
			break;
		}
		if ((candidate.flags & GFL_DROPPED) == 0)
		{
			memcpy(goal, &candidate, sizeof(*goal));
			return true;
		}
		if (next <= index)
		{
			break;
		}
		index = next;
	}

	bot_console_waypoint_t *checkpoint = BotAI_ConsoleFindWaypoint(
		state->checkpoints,
		name);
	if (checkpoint == NULL)
	{
		return false;
	}
	memcpy(goal, &checkpoint->goal, sizeof(*goal));
	return true;
}

/*
=============
BotAI_ConsoleTeamGoalTime

Parses an optional minutes/seconds match and returns its absolute deadline.
=============
*/
static float BotAI_ConsoleTeamGoalTime(const bot_match_t *match)
{
	if (match == NULL ||
		(match->subtype & BOT_CONSOLE_MATCH_SUBTYPE_TIME) == 0)
	{
		return 0.0f;
	}

	char time_text[BOT_CONSOLE_MESSAGE_STORAGE_CHARS];
	BotMatchVariable(match,
		BOT_CONSOLE_MATCH_TIME,
		time_text,
		(int)sizeof(time_text));
	bot_match_t time_match;
	memset(&time_match, 0, sizeof(time_match));
	if (!BotFindMatch(time_text, &time_match, BOT_CONSOLE_TIME_CONTEXT))
	{
		return 0.0f;
	}

	char number[BOT_CONSOLE_MESSAGE_STORAGE_CHARS];
	BotMatchVariable(&time_match,
		BOT_CONSOLE_MATCH_TIME,
		number,
		(int)sizeof(number));
	float duration = (float)atof(number);
	if (time_match.type == BOT_CONSOLE_MATCH_MINUTES)
	{
		duration *= 60.0f;
	}
	else if (time_match.type != BOT_CONSOLE_MATCH_SECONDS)
	{
		return 0.0f;
	}
	return duration > 0.0f ? AAS_Time() + duration : 0.0f;
}

/*
=============
BotAI_ConsoleSetPointGoal

Builds the point-sized team goal used by camp-here and camp-there.
=============
*/
static void BotAI_ConsoleSetPointGoal(bot_goal_t *goal,
	const vec3_t origin,
	int areanum,
	int entitynum)
{
	VectorCopy(origin, goal->origin);
	goal->areanum = areanum;
	VectorSet(goal->mins, -8.0f, -8.0f, -8.0f);
	VectorSet(goal->maxs, 8.0f, 8.0f, 8.0f);
	goal->entitynum = entitynum;
}

/*
=============
BotAI_ConsoleClientOrigin

Reads a team-command source from the live entity cache, with a bot-client
snapshot fallback for reconstructed semantic peers.
=============
*/
static bool BotAI_ConsoleClientOrigin(int client, vec3_t origin)
{
	if (origin == NULL || client < 0 || client >= BOT_INTERFACE_MAX_ENTITIES)
	{
		return false;
	}
	if (g_botInterfaceEntityCache[client].valid)
	{
		VectorCopy(g_botInterfaceEntityCache[client].state.origin, origin);
		return true;
	}

	bot_client_state_t *source = BotState_Get(client);
	if (source == NULL || !source->client_update_valid)
	{
		return false;
	}
	VectorCopy(source->last_client_update.origin, origin);
	return true;
}

/*
=============
BotAI_ConsoleHandleHelpAccompany

Reconstructs cases 3 and 4, including teammate-pronoun parsing, fuzzy target
lookup, the live-target/near-item goal fallback, and their distinct LTG times.
=============
*/
static void BotAI_ConsoleHandleHelpAccompany(bot_client_state_t *state,
	const bot_match_t *match)
{
	char teammate[BOT_CONSOLE_MESSAGE_STORAGE_CHARS];
	BotMatchVariable(match,
		BOT_CONSOLE_MATCH_TEAMMATE,
		teammate,
		(int)sizeof(teammate));

	bot_match_t teammate_match;
	memset(&teammate_match, 0, sizeof(teammate_match));
	bool other = true;
	int target_client;
	char netname[BOT_CONSOLE_MESSAGE_STORAGE_CHARS];
	netname[0] = '\0';
	if (BotFindMatch(teammate,
		&teammate_match,
		BOT_CONSOLE_TEAMMATE_CONTEXT) &&
		teammate_match.type == BOT_CONSOLE_MATCH_ME)
	{
		BotMatchVariable(match,
			BOT_CONSOLE_MATCH_NETNAME,
			netname,
			(int)sizeof(netname));
		target_client = BotAI_FindExactConsoleClientByName(netname);
		other = false;
	}
	else
	{
		target_client = BotAI_FindConsoleClientByName(teammate);
		if (target_client == state->client_number)
		{
			return;
		}
	}

	if (target_client < 0)
	{
		BotAI_ConsoleEnterInitialTeamChat(state,
			"whois",
			other ? teammate : netname);
		return;
	}

	state->team_goal.entitynum = 0;
	vec3_t target_origin;
	if (BotAI_ConsoleClientOrigin(target_client, target_origin))
	{
		int areanum = AAS_PointAreaNum(target_origin);
		if (areanum != 0 && AAS_AreaReachability(areanum) != 0)
		{
			BotAI_ConsoleSetPointGoal(&state->team_goal,
				target_origin,
				areanum,
				target_client);
		}
	}

	if (state->team_goal.entitynum == 0 &&
		(match->subtype & BOT_CONSOLE_MATCH_SUBTYPE_NEARITEM) != 0)
	{
		char item[BOT_CONSOLE_MESSAGE_STORAGE_CHARS];
		BotMatchVariable(match,
			BOT_CONSOLE_MATCH_ITEM,
			item,
			(int)sizeof(item));
		if (!BotAI_ConsoleResolveTeamGoal(state, item, &state->team_goal))
		{
			BotAI_ConsoleEnterInitialTeamChat(state, "cannotfind", item);
			return;
		}
	}

	if (state->team_goal.entitynum == 0)
	{
		BotAI_ConsoleEnterInitialTeamChat(state,
			other ? "whereis" : "whereareyou",
			other ? teammate : netname);
		return;
	}

	state->ltg_teammate = target_client;
	state->team_goal_number = state->team_goal.number;
	state->teammate_visible_time = AAS_Time();
	state->team_message_time = AAS_Time() +
		2.0f * BotAI_ConsoleRandom();
	state->team_goal_time = BotAI_ConsoleTeamGoalTime(match);
	if (match->type == BOT_CONSOLE_MATCH_HELP)
	{
		state->ltg_type = 1;
		if (state->team_goal_time == 0.0f)
		{
			state->team_goal_time = AAS_Time() +
				BOT_CONSOLE_HELP_DURATION;
		}
		return;
	}

	state->ltg_type = 2;
	if (state->team_goal_time == 0.0f)
	{
		state->team_goal_time = AAS_Time() +
			BOT_CONSOLE_ACCOMPANY_DURATION;
	}
	state->formation_dist = BOT_CONSOLE_ACCOMPANY_DISTANCE;
	state->arrive_time = 0.0f;
}

/*
=============
BotAI_ConsoleHandleDefendKeyArea

Reconstructs case 5's key-area resolution, failure chat, LTG deadline, and
defend-away reset.
=============
*/
static void BotAI_ConsoleHandleDefendKeyArea(bot_client_state_t *state,
	const bot_match_t *match)
{
	char keyarea[BOT_CONSOLE_MESSAGE_STORAGE_CHARS];
	BotMatchVariable(match,
		BOT_CONSOLE_MATCH_KEYAREA,
		keyarea,
		(int)sizeof(keyarea));
	if (!BotAI_ConsoleResolveTeamGoal(state, keyarea, &state->team_goal))
	{
		BotAI_ConsoleEnterInitialTeamChat(state, "cannotfind", keyarea);
		return;
	}

	state->team_goal_number = state->team_goal.number;
	state->ltg_type = 3;
	state->team_message_time = AAS_Time() +
		2.0f * BotAI_ConsoleRandom();
	state->team_goal_time = BotAI_ConsoleTeamGoalTime(match);
	if (state->team_goal_time == 0.0f)
	{
		state->team_goal_time = AAS_Time() +
			BOT_CONSOLE_DEFEND_DURATION;
	}
	state->defend_away_time = 0.0f;
}

/*
=============
BotAI_ConsoleCTFFlagsAvailable

Mirrors the case 6/7 gate on ctf plus non-zero cached Red and Blue Flag goal
areas. The reconstruction resolves the static goals on demand from botlib's
level-item table because that table owns the semantic cache.
=============
*/
static bool BotAI_ConsoleCTFFlagsAvailable(void)
{
	if (LibVarGetValue("ctf") == 0.0f)
	{
		return false;
	}

	char red_name[] = "Red Flag";
	char blue_name[] = "Blue Flag";
	bot_goal_t red_flag;
	bot_goal_t blue_flag;
	memset(&red_flag, 0, sizeof(red_flag));
	memset(&blue_flag, 0, sizeof(blue_flag));
	return BotGetLevelItemGoal(-1, red_name, &red_flag) >= 0 &&
		red_flag.areanum != 0 &&
		BotGetLevelItemGoal(-1, blue_name, &blue_flag) >= 0 &&
		blue_flag.areanum != 0;
}

/*
=============
BotAI_ConsoleCommitCTFOrder

Applies the retail case 6 rush-base or case 7 get-flag LTG, message deadline,
fixed goal duration, and rush-away reset boundary.
=============
*/
static void BotAI_ConsoleCommitCTFOrder(bot_client_state_t *state,
	int match_type)
{
	state->team_message_time = AAS_Time() +
		2.0f * BotAI_ConsoleRandom();
	if (match_type == BOT_CONSOLE_MATCH_RUSH_BASE)
	{
		state->ltg_type = 5;
		state->rush_base_away_time = 0.0f;
		state->team_goal_time = AAS_Time() +
			BOT_CONSOLE_RUSH_BASE_DURATION;
		return;
	}

	state->ltg_type = 4;
	state->team_goal_time = AAS_Time() +
		BOT_CONSOLE_GET_FLAG_DURATION;
}

/*
=============
BotAI_ConsoleCommitCampGoal

Commits case 19's resolved goal, teammate, LTG type, and retail deadlines.
=============
*/
static void BotAI_ConsoleCommitCampGoal(bot_client_state_t *state,
	const bot_match_t *match,
	const bot_goal_t *goal,
	int source_client)
{
	memcpy(&state->team_goal, goal, sizeof(state->team_goal));
	state->team_goal_number = goal->number;
	state->team_message_time = AAS_Time() + 2.0f * BotAI_ConsoleRandom();
	state->ltg_type = 6;
	state->ltg_teammate = source_client;
	state->team_goal_time = BotAI_ConsoleTeamGoalTime(match);
	if (state->team_goal_time == 0.0f)
	{
		state->team_goal_time = AAS_Time() +
			BOT_CONSOLE_DEFAULT_TEAM_GOAL_DURATION;
	}
	state->arrive_time = 0.0f;
}

/*
=============
BotAI_ConsoleHandleCamp

Reconstructs match case 19's sender lookup and there/here/key-area branches.
=============
*/
static void BotAI_ConsoleHandleCamp(bot_client_state_t *state,
	const bot_match_t *match)
{
	char netname[BOT_CONSOLE_MESSAGE_STORAGE_CHARS];
	BotMatchVariable(match,
		BOT_CONSOLE_MATCH_NETNAME,
		netname,
		(int)sizeof(netname));
	int source_client = BotAI_FindConsoleClientByName(netname);
	if (source_client < 0)
	{
		BotAI_ConsoleEnterInitialTeamChat(state, "whois", netname);
		return;
	}

	char keyarea[BOT_CONSOLE_MESSAGE_STORAGE_CHARS];
	BotMatchVariable(match,
		BOT_CONSOLE_MATCH_KEYAREA,
		keyarea,
		(int)sizeof(keyarea));
	bot_goal_t goal;
	memcpy(&goal, &state->team_goal, sizeof(goal));
	if ((match->subtype & BOT_CONSOLE_MATCH_SUBTYPE_THERE) != 0)
	{
		BotAI_ConsoleSetPointGoal(&goal,
			state->last_client_update.origin,
			AAS_PointAreaNum(state->last_client_update.origin),
			state->client_number);
	}
	else if ((match->subtype & BOT_CONSOLE_MATCH_SUBTYPE_HERE) != 0)
	{
		if (source_client == state->client_number)
		{
			return;
		}
		state->team_goal.entitynum = 0;
		goal.entitynum = 0;

		vec3_t source_origin;
		int areanum = 0;
		bool visible = false;
		if (BotAI_ConsoleClientOrigin(source_client, source_origin))
		{
			areanum = AAS_PointAreaNum(source_origin);
			vec3_t eye;
			BotInterface_ClientEyePosition(state, eye);
			visible = areanum != 0 &&
				AAS_AreaReachability(areanum) != 0 &&
				BotInterface_HasLineOfSight(eye,
					source_origin,
					state->client_number,
					source_client);
		}
		if (!visible)
		{
			BotAI_ConsoleEnterInitialTeamChat(state,
				"whereareyou",
				netname);
			return;
		}

		BotAI_ConsoleSetPointGoal(&goal,
			source_origin,
			areanum,
			source_client);
	}
	else if (!BotAI_ConsoleResolveTeamGoal(state, keyarea, &goal))
	{
		BotAI_ConsoleEnterInitialTeamChat(state, "cannotfind", keyarea);
		return;
	}

	BotAI_ConsoleCommitCampGoal(state,
		match,
		&goal,
		source_client);
}

/*
=============
BotAI_ConsoleHandleCheckpoint

Stores case 20 checkpoints independently of addressing and emits only the
addressed invalid/confirmation team chats.
=============
*/
static void BotAI_ConsoleHandleCheckpoint(bot_client_state_t *state,
	const bot_match_t *match)
{
	char position[BOT_CONSOLE_MESSAGE_STORAGE_CHARS];
	BotMatchVariable(match,
		BOT_CONSOLE_MATCH_POSITION,
		position,
		(int)sizeof(position));
	vec3_t origin;
	VectorClear(origin);
	(void)sscanf(position,
		"%f %f %f",
		&origin[0],
		&origin[1],
		&origin[2]);
	origin[2] += 0.5f;
	int areanum = AAS_PointAreaNum(origin);
	if (areanum == 0)
	{
		if (BotAI_ConsoleAddressedToBot(state, match))
		{
			BotAI_ConsoleEnterInitialTeamChat(state,
				"checkpoint_invalid",
				NULL);
		}
		return;
	}

	char name[BOT_CONSOLE_MESSAGE_STORAGE_CHARS];
	BotMatchVariable(match,
		BOT_CONSOLE_MATCH_NAME,
		name,
		(int)sizeof(name));
	bot_console_waypoint_t *old = BotAI_ConsoleFindWaypoint(
		state->checkpoints,
		name);
	if (old != NULL)
	{
		BotAI_ConsoleUnlinkCheckpoint(state, old);
		free(old);
	}

	bot_console_waypoint_t *checkpoint = BotAI_ConsoleCreateWaypoint(name,
		origin,
		areanum);
	if (checkpoint == NULL)
	{
		return;
	}
	checkpoint->next = state->checkpoints;
	if (state->checkpoints != NULL)
	{
		state->checkpoints->prev = checkpoint;
	}
	state->checkpoints = checkpoint;

	if (BotAI_ConsoleAddressedToBot(state, match))
	{
		char gps[BOT_CONSOLE_MESSAGE_STORAGE_CHARS];
		snprintf(gps,
			sizeof(gps),
			"%1.0f %1.0f %1.0f",
			checkpoint->goal.origin[0],
			checkpoint->goal.origin[1],
			checkpoint->goal.origin[2]);
		BotAI_ConsoleEnterInitialTeamChat2(state,
			"checkpoint_confirm",
			name,
			gps);
	}
}

/*
=============
BotAI_ConsoleClearPatrolPoints

Mirrors case 21's failure store, which clears only the patrol-list head and
leaves the current-point and flag fields untouched.
=============
*/
static void BotAI_ConsoleClearPatrolPoints(bot_client_state_t *state)
{
	state->patrol_points = NULL;
}

/*
=============
BotAI_ConsoleRecoverPatrolVariables

Recovers patrol variables when an empty optional match alternative leaves the
reconstructed matcher without a KEYAREA or MORE public span.
=============
*/
static int BotAI_ConsoleRecoverPatrolVariables(const char *text,
	char *keyarea,
	size_t keyarea_size,
	char *more,
	size_t more_size)
{
	if (keyarea == NULL || keyarea_size == 0U ||
		more == NULL || more_size == 0U)
	{
		return 0;
	}
	keyarea[0] = '\0';
	more[0] = '\0';
	if (text == NULL)
	{
		return 0;
	}

	const char *cursor = text;
	const char *prefixes[] = {"the ", "checkpoint ", "waypoint "};
	for (size_t index = 0; index < sizeof(prefixes) / sizeof(prefixes[0]); ++index)
	{
		size_t prefix_length = strlen(prefixes[index]);
		if (Q_strnicmp(cursor, prefixes[index], prefix_length) == 0)
		{
			cursor += prefix_length;
			break;
		}
	}

	strncpy(keyarea, cursor, keyarea_size - 1U);
	keyarea[keyarea_size - 1U] = '\0';
	int separator = StringContains(keyarea, " to ", 0);
	int back_to_start = StringContains(keyarea, " and back to the start", 0);
	if (separator >= 0 &&
		(back_to_start < 0 || separator < back_to_start))
	{
		const char *remainder = keyarea + separator + 4;
		strncpy(more, remainder, more_size - 1U);
		more[more_size - 1U] = '\0';
		keyarea[separator] = '\0';
		return BOT_CONSOLE_MATCH_SUBTYPE_MORE;
	}

	const struct
	{
		const char *suffix;
		int subtype;
	} suffixes[] = {
		{" and back to the start", BOT_CONSOLE_MATCH_SUBTYPE_BACK},
		{" and back", BOT_CONSOLE_MATCH_SUBTYPE_BACK},
		{" and reverse", BOT_CONSOLE_MATCH_SUBTYPE_REVERSE},
	};
	for (size_t index = 0; index < sizeof(suffixes) / sizeof(suffixes[0]); ++index)
	{
		size_t text_length = strlen(keyarea);
		size_t suffix_length = strlen(suffixes[index].suffix);
		if (text_length >= suffix_length &&
			Q_stricmp(keyarea + text_length - suffix_length,
				suffixes[index].suffix) == 0)
		{
			keyarea[text_length - suffix_length] = '\0';
			return suffixes[index].subtype;
		}
	}

	return 0;
}

/*
=============
BotAI_ConsoleGetPatrolPoints

Parses and resolves the chained patrol-key-area grammar used by sub_10026990.
=============
*/
static bool BotAI_ConsoleGetPatrolPoints(bot_client_state_t *state,
	const bot_match_t *match)
{
	char remaining[BOT_CONSOLE_MESSAGE_STORAGE_CHARS];
	BotMatchVariable(match,
		BOT_CONSOLE_MATCH_KEYAREA,
		remaining,
		(int)sizeof(remaining));
	bot_console_waypoint_t *points = NULL;
	bot_console_waypoint_t *tail = NULL;
	int point_count = 0;
	int patrol_flags = 0;

	for (;;)
	{
		bot_match_t point_match;
		memset(&point_match, 0, sizeof(point_match));
		if (!BotFindMatch(remaining,
			&point_match,
			BOT_CONSOLE_PATROL_CONTEXT) ||
			point_match.type != BOT_CONSOLE_MATCH_PATROL_KEYAREA)
		{
			EA_SayTeam(state->client_number, "what do you say?");
			BotState_FreeConsoleWaypoints(points);
			BotAI_ConsoleClearPatrolPoints(state);
			return false;
		}

		char keyarea[BOT_CONSOLE_MESSAGE_STORAGE_CHARS];
		BotMatchVariable(&point_match,
			BOT_CONSOLE_MATCH_KEYAREA,
			keyarea,
			(int)sizeof(keyarea));
		int point_subtype = point_match.subtype;
		char recovered_more[BOT_CONSOLE_MESSAGE_STORAGE_CHARS];
		BotMatchVariable(&point_match,
			BOT_CONSOLE_MATCH_MORE,
			recovered_more,
			(int)sizeof(recovered_more));
		if (keyarea[0] == '\0' ||
			((point_subtype & BOT_CONSOLE_MATCH_SUBTYPE_MORE) != 0 &&
				recovered_more[0] == '\0'))
		{
			char fallback_keyarea[BOT_CONSOLE_MESSAGE_STORAGE_CHARS];
			char fallback_more[BOT_CONSOLE_MESSAGE_STORAGE_CHARS];
			int fallback_subtype = BotAI_ConsoleRecoverPatrolVariables(remaining,
				fallback_keyarea,
				sizeof(fallback_keyarea),
				fallback_more,
				sizeof(fallback_more));
			if (keyarea[0] == '\0')
			{
				strncpy(keyarea,
					fallback_keyarea,
					sizeof(keyarea) - 1U);
				keyarea[sizeof(keyarea) - 1U] = '\0';
			}
			if (recovered_more[0] == '\0')
			{
				strncpy(recovered_more,
					fallback_more,
					sizeof(recovered_more) - 1U);
				recovered_more[sizeof(recovered_more) - 1U] = '\0';
			}
			if (point_subtype == 0 && fallback_subtype != 0)
			{
				point_subtype = fallback_subtype;
			}
		}
		if (keyarea[0] == '\0')
		{
			EA_SayTeam(state->client_number, "what do you say?");
			BotState_FreeConsoleWaypoints(points);
			BotAI_ConsoleClearPatrolPoints(state);
			return false;
		}
		bot_goal_t goal;
		memset(&goal, 0, sizeof(goal));
		if (!BotAI_ConsoleResolveTeamGoal(state, keyarea, &goal))
		{
			BotAI_ConsoleEnterInitialTeamChat(state, "cannotfind", keyarea);
			BotState_FreeConsoleWaypoints(points);
			BotAI_ConsoleClearPatrolPoints(state);
			return false;
		}

		bot_console_waypoint_t *point = BotAI_ConsoleCreateWaypoint(keyarea,
			goal.origin,
			goal.areanum);
		if (point == NULL)
		{
			BotState_FreeConsoleWaypoints(points);
			return false;
		}
		if (tail != NULL)
		{
			tail->next = point;
			point->prev = tail;
		}
		else
		{
			points = point;
		}
		tail = point;
		++point_count;

		if ((point_subtype & BOT_CONSOLE_MATCH_SUBTYPE_BACK) != 0)
		{
			patrol_flags = BOT_CONSOLE_PATROL_LOOP;
			break;
		}
		if ((point_subtype & BOT_CONSOLE_MATCH_SUBTYPE_REVERSE) != 0)
		{
			patrol_flags = BOT_CONSOLE_PATROL_REVERSE;
			break;
		}
		if ((point_subtype & BOT_CONSOLE_MATCH_SUBTYPE_MORE) == 0)
		{
			break;
		}
		if (recovered_more[0] != '\0')
		{
			strncpy(remaining, recovered_more, sizeof(remaining) - 1U);
			remaining[sizeof(remaining) - 1U] = '\0';
		}
		else
		{
			remaining[0] = '\0';
		}
	}

	if (point_count < 2)
	{
		EA_SayTeam(state->client_number,
			"I need more key points to patrol\n");
		BotState_FreeConsoleWaypoints(points);
		return false;
	}

	BotState_FreeConsoleWaypoints(state->patrol_points);
	state->patrol_points = points;
	state->current_patrol_point = points;
	state->patrol_flags = patrol_flags;
	return true;
}

/*
=============
BotAI_ConsoleCommitPatrol

Commits case 21's LTG and message/goal deadlines after waypoint parsing.
=============
*/
static void BotAI_ConsoleCommitPatrol(bot_client_state_t *state,
	const bot_match_t *match)
{
	state->team_message_time = AAS_Time() + 2.0f * BotAI_ConsoleRandom();
	state->ltg_type = 7;
	state->team_goal_time = BotAI_ConsoleTeamGoalTime(match);
	if (state->team_goal_time == 0.0f)
	{
		state->team_goal_time = AAS_Time() +
			BOT_CONSOLE_DEFAULT_TEAM_GOAL_DURATION;
	}
}

/*
=============
BotAI_ConsoleEasyClientName

Reconstructs sub_10021860's teammate-name cleanup: high-bit stripping,
space/clan-tag/Mr removal, lower-casing, and alphanumeric/underscore filtering.
=============
*/
static void BotAI_ConsoleEasyClientName(int client,
	char *output,
	size_t output_size)
{
	if (output == NULL || output_size == 0U)
	{
		return;
	}

	char name[128];
	snprintf(name, sizeof(name), "%s", BotState_ClientName(client));
	for (char *character = name; *character != '\0'; ++character)
	{
		*character = (char)((unsigned char)*character & 0x7fU);
	}

	char *space = strstr(name, " ");
	while (space != NULL)
	{
		memmove(space, space + 1, strlen(space + 1) + 1U);
		space = strstr(name, " ");
	}

	char *open_bracket = strstr(name, "[");
	char *close_bracket = strstr(name, "]");
	if (open_bracket != NULL && close_bracket != NULL)
	{
		if (close_bracket > open_bracket)
		{
			memmove(open_bracket,
				close_bracket + 1,
				strlen(close_bracket + 1) + 1U);
		}
		else
		{
			memmove(close_bracket,
				open_bracket + 1,
				strlen(open_bracket + 1) + 1U);
		}
	}

	if ((name[0] == 'm' || name[0] == 'M') &&
		(name[1] == 'r' || name[1] == 'R'))
	{
		memmove(name, name + 2, strlen(name + 2) + 1U);
	}

	char *character = name;
	while (*character != '\0')
	{
		if ((*character >= 'a' && *character <= 'z') ||
			(*character >= '0' && *character <= '9') ||
			*character == '_')
		{
			++character;
		}
		else if (*character >= 'A' && *character <= 'Z')
		{
			*character = (char)(*character + ('a' - 'A'));
			++character;
		}
		else
		{
			memmove(character,
				character + 1,
				strlen(character + 1) + 1U);
		}
	}

	strncpy(output, name, output_size - 1U);
	output[output_size - 1U] = '\0';
}

/*
=============
BotAI_ConsoleReportLongTermGoal

Reports Gladiator long-term-goal types one through seven through the exact
initial-chat types and team-chat destination used by match case 11.
=============
*/
static void BotAI_ConsoleReportLongTermGoal(bot_client_state_t *state)
{
	if (state == NULL)
	{
		return;
	}

	char teammate[BOT_CONSOLE_EASY_NAME_CHARS];
	char goal_name[BOT_CONSOLE_MESSAGE_STORAGE_CHARS];
	switch (state->ltg_type)
	{
	case 1:
		BotAI_ConsoleEasyClientName(state->ltg_teammate,
			teammate,
			sizeof(teammate));
		BotAI_ConsoleEnterInitialTeamChat(state, "helping", teammate);
		return;
	case 2:
		BotAI_ConsoleEasyClientName(state->ltg_teammate,
			teammate,
			sizeof(teammate));
		BotAI_ConsoleEnterInitialTeamChat(state, "accompanying", teammate);
		return;
	case 3:
		BotGoalName(state->team_goal_number,
			goal_name,
			(int)sizeof(goal_name));
		BotAI_ConsoleEnterInitialTeamChat(state, "defending", goal_name);
		return;
	case 4:
		BotAI_ConsoleEnterInitialTeamChat(state, "capturingflag", NULL);
		return;
	case 5:
		BotAI_ConsoleEnterInitialTeamChat(state, "rushingbase", NULL);
		return;
	case 6:
		BotAI_ConsoleEnterInitialTeamChat(state, "camping", NULL);
		return;
	case 7:
		BotAI_ConsoleEnterInitialTeamChat(state, "patrolling", NULL);
		return;
	default:
		return;
	}
}

/*
=============
BotAI_ConsoleJoinSubteam

Copies TEAMNAME into Gladiator's 32-byte subteam slot, pins byte 31 to NUL,
then announces the untruncated captured team name.
=============
*/
static void BotAI_ConsoleJoinSubteam(bot_client_state_t *state,
	const bot_match_t *match)
{
	char teamname[BOT_CONSOLE_MESSAGE_STORAGE_CHARS];
	BotMatchVariable(match,
		BOT_CONSOLE_MATCH_TEAMMATE,
		teamname,
		(int)sizeof(teamname));
	strncpy(state->subteam, teamname, sizeof(state->subteam));
	state->subteam[sizeof(state->subteam) - 1U] = '\0';
	BotAI_ConsoleEnterInitialTeamChat(state, "joinedteam", teamname);
}

/*
=============
BotAI_ConsoleLeaveSubteam

Announces a non-empty current subteam and clears the first retail dword of the
32-byte slot regardless of whether a message was constructed.
=============
*/
static void BotAI_ConsoleLeaveSubteam(bot_client_state_t *state)
{
	if (state->subteam[0] != '\0')
	{
		BotAI_ConsoleEnterInitialTeamChat(state, "leftteam", state->subteam);
	}
	memset(state->subteam, 0, sizeof(int));
}

/*
=============
BotAI_ConsoleSetFormationSpace

Converts NUMBER using the retail feet/metre factors and replaces values outside
the inclusive 48..500 range with the 100-unit default.
=============
*/
static void BotAI_ConsoleSetFormationSpace(bot_client_state_t *state,
	const bot_match_t *match)
{
	char number[BOT_CONSOLE_MESSAGE_STORAGE_CHARS];
	BotMatchVariable(match,
		BOT_CONSOLE_MATCH_NUMBER,
		number,
		(int)sizeof(number));
	double space = atof(number) *
		((match->subtype & BOT_CONSOLE_MATCH_SUBTYPE_FEET) != 0
			? 9.7536000000000005
			: 32.0);
	if (space < 48.0 || space > 500.0)
	{
		space = 100.0;
	}
	state->formation_dist = (float)space;
}

/*
=============
BotAI_MatchConsoleMessage

Classifies normalised console text in Gladiator's combined obituary,
enter-game, and initial-team-chat context, applying the reconstructed death,
team command, subteam, formation, and dismissal effects.
=============
*/
static bool BotAI_MatchConsoleMessage(bot_client_state_t *state, const char *message)
{
	if (state == NULL || message == NULL)
	{
		return false;
	}

	bot_match_t match;
	memset(&match, 0, sizeof(match));
	if (!BotFindMatch(message, &match, BOT_CONSOLE_MATCH_CONTEXT))
	{
		return false;
	}

	if (match.type < 1 || match.type > 21)
	{
		BotInterface_Printf(PRT_MESSAGE, "unknown match type\n");
		return true;
	}

	switch (match.type)
	{
	case BOT_CONSOLE_MATCH_DEATH:
	{
		char victim[BOT_CONSOLE_MESSAGE_STORAGE_CHARS];
		BotMatchVariable(&match,
			BOT_CONSOLE_MATCH_VICTIM,
			victim,
			(int)sizeof(victim));
		int victim_client = BotState_FindClientByName(victim);
		if (victim_client == state->client_number)
		{
			state->bot_death_type = match.subtype;
		}
		else if (victim_client == state->combat.current_enemy)
		{
			state->enemy_death_type = match.subtype;
			state->combat.enemy_death_time = AAS_Time();
		}
		break;
	}
	case BOT_CONSOLE_MATCH_HELP:
	case BOT_CONSOLE_MATCH_ACCOMPANY:
		if (BotAI_ConsoleTeamPlayIsOn() &&
			BotAI_ConsoleAddressedToBot(state, &match))
		{
			BotAI_ConsoleHandleHelpAccompany(state, &match);
		}
		break;
	case BOT_CONSOLE_MATCH_DEFEND_KEY_AREA:
		if (BotAI_ConsoleTeamPlayIsOn() &&
			BotAI_ConsoleAddressedToBot(state, &match))
		{
			BotAI_ConsoleHandleDefendKeyArea(state, &match);
		}
		break;
	case BOT_CONSOLE_MATCH_RUSH_BASE:
	case BOT_CONSOLE_MATCH_GET_FLAG:
		if (BotAI_ConsoleCTFFlagsAvailable() &&
			BotAI_ConsoleAddressedToBot(state, &match))
		{
			BotAI_ConsoleCommitCTFOrder(state, match.type);
		}
		break;
	case BOT_CONSOLE_MATCH_START_TEAM_LEADERSHIP:
	case BOT_CONSOLE_MATCH_STOP_TEAM_LEADERSHIP:
		if (BotAI_ConsoleTeamPlayIsOn())
		{
			BotAI_UpdateConsoleLeadership(state, &match);
		}
		break;
	case BOT_CONSOLE_MATCH_WAIT:
		BotInterface_Printf(PRT_MESSAGE, "unknown match type\n");
		break;
	case BOT_CONSOLE_MATCH_WHAT_ARE_YOU_DOING:
		if (BotAI_ConsoleAddressedToBot(state, &match))
		{
			BotAI_ConsoleReportLongTermGoal(state);
		}
		break;
	case BOT_CONSOLE_MATCH_JOIN_SUBTEAM:
		if (BotAI_ConsoleTeamPlayIsOn() &&
			BotAI_ConsoleAddressedToBot(state, &match))
		{
			BotAI_ConsoleJoinSubteam(state, &match);
		}
		break;
	case BOT_CONSOLE_MATCH_LEAVE_SUBTEAM:
		if (BotAI_ConsoleTeamPlayIsOn() &&
			BotAI_ConsoleAddressedToBot(state, &match))
		{
			BotAI_ConsoleLeaveSubteam(state);
		}
		break;
	case BOT_CONSOLE_MATCH_CREATE_FORMATION:
	case BOT_CONSOLE_MATCH_FORMATION_POSITION:
		EA_SayTeam(state->client_number,
			"the part of my brain to create formations has been damaged");
		break;
	case BOT_CONSOLE_MATCH_FORMATION_SPACE:
		if (BotAI_ConsoleTeamPlayIsOn() &&
			BotAI_ConsoleAddressedToBot(state, &match))
		{
			BotAI_ConsoleSetFormationSpace(state, &match);
		}
		break;
	case BOT_CONSOLE_MATCH_DO_FORMATION:
		break;
	case BOT_CONSOLE_MATCH_DISMISS:
		if (BotAI_ConsoleTeamPlayIsOn() &&
			BotAI_ConsoleAddressedToBot(state, &match) &&
			(state->ltg_type == 1 || state->ltg_type == 2))
		{
			state->ltg_type = 0;
		}
		break;
	case BOT_CONSOLE_MATCH_CAMP:
		if (BotAI_ConsoleTeamPlayIsOn() &&
			BotAI_ConsoleAddressedToBot(state, &match))
		{
			BotAI_ConsoleHandleCamp(state, &match);
		}
		break;
	case BOT_CONSOLE_MATCH_CHECKPOINT:
		if (BotAI_ConsoleTeamPlayIsOn())
		{
			BotAI_ConsoleHandleCheckpoint(state, &match);
		}
		break;
	case BOT_CONSOLE_MATCH_PATROL:
		if (BotAI_ConsoleTeamPlayIsOn() &&
			BotAI_ConsoleAddressedToBot(state, &match) &&
			BotAI_ConsoleGetPatrolPoints(state, &match))
		{
			BotAI_ConsoleCommitPatrol(state, &match);
		}
		break;
	default:
		break;
	}

	return true;
}

/*
=============
BotAI_ChatTime

Returns Gladiator's pending-message length converted through the character's
chat characters-per-minute characteristic.
=============
*/
static float BotAI_ChatTime(const bot_client_state_t *state)
{
	if (state == NULL || state->chat_state == NULL || state->character_handle <= 0)
	{
		return 0.0f;
	}

	int cpm = Characteristic_BInteger(state->character_handle,
		CHARACTERISTIC_CHAT_CPM,
		1,
		4000);
	return (float)BotChatLength(state->chat_state) * 30.0f / (float)cpm;
}

/*
=============
BotAI_SelectConsoleReply

Applies Gladiator's nochat, stand-node, position, population, and character
probability gates before constructing a reply from the text after the colon.
=============
*/
static bool BotAI_SelectConsoleReply(bot_client_state_t *state,
	char *message,
	unsigned long synonym_context)
{
	if (state == NULL || message == NULL || state->chat_state == NULL ||
		state->chat_standing || LibVarGetValue("nochat") != 0.0f ||
		!BotAI_ValidChatPosition(state) || state->character_handle <= 0)
	{
		return false;
	}

	float chat_reply = Characteristic_BFloat(state->character_handle,
		CHARACTERISTIC_CHAT_REPLY,
		0.0f,
		1.0f);
	float population_gate = 1.5f / (float)(BotState_ActiveClientCount() + 1);
	if (BotAI_ConsoleRandom() >= population_gate ||
		BotAI_ConsoleRandom() >= chat_reply)
	{
		return false;
	}

	char *colon = strchr(message, ':');
	if (colon == NULL)
	{
		return false;
	}

	memmove(message, colon + 1, strlen(colon + 1) + 1U);
	UnifyWhiteSpaces(message);
	return BotReplyChat(state->chat_state, message, synonym_context) != 0;
}

/*
=============
BotCheckConsoleMessages

Reads the retail node queue in FIFO order, defers a recent chat head while the
queue is below the flood threshold, classifies normalised text, and removes
each non-deferred node at the same decision points as Gladiator.
=============
*/
static void BotCheckConsoleMessages(bot_client_state_t *state)
{
	if (state == NULL || state->chat_state == NULL)
	{
		return;
	}

	const bot_console_message_node_t *node = BotNextConsoleMessageNode(
		state->chat_state);
	while (node != NULL)
	{
		if (BotNumConsoleMessages(state->chat_state) < 10U &&
			node->type == CMS_CHAT)
		{
			float read_time = 1.0f + BotAI_ConsoleRandom();
			if (node->time > AAS_Time() - read_time)
			{
				break;
			}
		}

		if (node->type == CMS_CHAT)
		{
			const char *colon = strchr(node->message, ':');
			if (colon == NULL || BotAI_IsOwnConsoleChat(state, node->message, colon))
			{
				BotRemoveConsoleMessageNode(state->chat_state, node);
				node = BotNextConsoleMessageNode(state->chat_state);
				continue;
			}
		}

		char message[BOT_CONSOLE_MESSAGE_STORAGE_CHARS];
		memcpy(message, node->message, sizeof(message));
		message[sizeof(message) - 1U] = '\0';
		UnifyWhiteSpaces(message);
		char unsynonymized_message[BOT_CONSOLE_MESSAGE_STORAGE_CHARS];
		memcpy(unsynonymized_message, message, sizeof(unsynonymized_message));
		unsigned long synonym_context = BotAI_ConsoleSynonymContext(state);
		BotReplaceSynonyms(message, synonym_context);

		bool matched = BotAI_MatchConsoleMessage(state, message);
		if (!matched && strcmp(message, unsynonymized_message) != 0)
		{
			/*
			 * The reconstructed matcher stores literal template words rather
			 * than retail's synonym-token form. Retrying the normalized source
			 * keeps commands such as CTF "rush base" reachable after the
			 * canonical synonym pass rewrites base to a flag name.
			 */
			matched = BotAI_MatchConsoleMessage(state,
				unsynonymized_message);
		}

		if (!matched &&
			node->type == CMS_CHAT &&
			BotAI_SelectConsoleReply(state, message, synonym_context))
		{
			BotRemoveConsoleMessageNode(state->chat_state, node);
			state->stand_time = AAS_Time() + BotAI_ChatTime(state);
			state->chat_standing = true;
			return;
		}

		BotRemoveConsoleMessageNode(state->chat_state, node);
		node = BotNextConsoleMessageNode(state->chat_state);
	}
}

/*
=============
BotAI_ReplyStandActive

Dispatches a completed pending reply at the retail stand deadline and reports
whether the bot must remain stationary for this frame.
=============
*/
static bool BotAI_ReplyStandActive(bot_client_state_t *state)
{
	if (state == NULL || !state->chat_standing)
	{
		return false;
	}

	if (AAS_Time() < state->stand_time)
	{
		return true;
	}

	if (state->chat_state != NULL)
	{
		BotEnterChat(state->chat_state, state->client_number, 0);
	}
	state->chat_standing = false;
	return false;
}

/*
=============
BotAI_RunReplyStand

Submits the stationary input produced while Gladiator's stand node waits for a
pending reply's typing time to elapse.
=============
*/
static int BotAI_RunReplyStand(bot_client_state_t *state, float thinktime)
{
	bot_input_t input;
	memset(&input, 0, sizeof(input));
	input.thinktime = thinktime;
	VectorCopy(state->last_client_update.viewangles, input.viewangles);

	int status = EA_SubmitInput(state->client_number, &input);
	if (status != BLERR_NOERROR)
	{
		return status;
	}

	BotState_EmitPendingClientCommands(state);
	status = EA_EndRegular(state->client_number, thinktime);
	if (status == BLERR_NOERROR)
	{
		state->client_update_valid = false;
	}
	return status;
}

/*
=============
BotAI_Think

Runs one reconstructed per-client AI frame, including retail console-message
processing before node-equivalent movement work.
=============
*/
static int BotAI_Think(bot_client_state_t *state, float thinktime)
{
	if (state == NULL)
	{
		return BLERR_AIUPDATEINACTIVECLIENT;
	}

    if (!state->client_update_valid)
    {
        BotInterface_Printf(PRT_WARNING,
                             "[bot_interface] BotAI: no snapshot for client %d\n",
                             state->client_number);
        return BLERR_AIUPDATEINACTIVECLIENT;
    }

    if (state->goal_state == NULL || state->move_state == NULL)
    {
        return BLERR_INVALIDIMPORT;
    }

	BotAI_UpdateBattleInventory(state);
	BotCheckConsoleMessages(state);
	if (BotAI_ReplyStandActive(state))
	{
		return BotAI_RunReplyStand(state, thinktime);
	}

	ai_dm_enemy_info_t enemy_info;
	BotAI_InitEnemyInfo(&enemy_info);
	if (state->dm_state != NULL)
	{
		BotAI_FindEnemy(state, &enemy_info);

		int enemy_entity = enemy_info.valid
			? enemy_info.entity
			: state->combat.current_enemy;
		if (enemy_entity >= 0 &&
			enemy_entity < BOT_INTERFACE_MAX_ENTITIES &&
			g_botInterfaceEntityCache[enemy_entity].valid)
		{
			(void)BotAI_UpdateEnemyBattleInventory(state, enemy_entity);
		}
	}

	if (state->weapon_state > 0)
	{
		BotWeaponStateSyncFrame(state->weapon_state,
			state->client_number,
			state->last_client_update.inventory,
			BotInterface_ModelNameForIndex(state->last_client_update.gunindex));
		state->current_weapon = BotSelectBestFightWeapon(state->client_number,
			state->weapon_state,
			state->last_client_update.inventory,
			g_botInterfaceFrameTime);
	}

	int status = BotInterface_PrepareMoveState(state, thinktime);
	if (status != BLERR_NOERROR)
	{
		return status;
	}

	if (state->goal_handle > 0)
	{
		AI_GoalBotlib_SynchroniseAvoid(state->goal_handle, state->goal_state, g_botInterfaceFrameTime);
		AI_GoalBotlib_Update(state->goal_handle,
							 state->last_client_update.origin,
                             state->last_client_update.inventory,
                             0,
                             g_botInterfaceFrameTime,
                             3.0f);
	}

	BotInterface_UpdateGoalSnapshot(state);
	status = BotInterface_RebuildGoalCandidates(state);
	if (status != BLERR_NOERROR)
	{
		return status;
	}

	ai_goal_selection_t selection = {0};
	status = AI_GoalOrchestrator_Refresh(state->goal_state, g_botInterfaceFrameTime, &selection);
	if (status != BLERR_NOERROR)
	{
		return status;
	}

	bot_input_t input = {0};
	status = AI_MoveOrchestrator_Dispatch(state->move_state, &selection, &input);
	if (status != BLERR_NOERROR)
	{
        return status;
    }

	input.thinktime = thinktime;
	VectorCopy(state->last_client_update.viewangles, input.viewangles);

	status = AI_MoveOrchestrator_Submit(state->move_state, state->client_number, &input);
	if (status != BLERR_NOERROR)
    {
        return status;
    }

    if (state->dm_state != NULL)
    {
        AI_DMState_Update(state->dm_state,
                          state,
                          &selection,
                          &enemy_info,
                          &input,
                          g_botInterfaceFrameTime);
        BotInterface_SynchroniseCombatState(state);
    }

	BotState_EmitPendingClientCommands(state);

	status = EA_EndRegular(state->client_number, thinktime);
	if (status != BLERR_NOERROR)
	{
		return status;
	}

    state->client_update_valid = false;
    return BLERR_NOERROR;
}

static int BotAI(int client, float thinktime)
{
	if (!BotInterface_EnsureLibraryReady("BotAI"))
	{
		return BLERR_LIBRARYNOTSETUP;
	}

	if (!BotInterface_ValidateClientNumber(client, "BotAI"))
	{
		return BLERR_INVALIDCLIENTNUMBER;
	}

    bot_client_state_t *state = BotState_Get(client);
    if (state == NULL || !state->active)
    {
		BotInterface_Printf(PRT_FATAL,
			"client %d hasn't been setup\n",
			client);
        return BLERR_AICLIENTNOTSETUP;
    }

    return BotAI_Think(state, thinktime);
}

/*
=============
BotConsoleMessage

Queues a console message while preserving the retail inactive-client failure.
=============
*/
static int BotConsoleMessage(int client, int type, char *message)
{
	if (!BotInterface_EnsureLibraryReady("BotConsoleMessage"))
	{
		return BLERR_LIBRARYNOTSETUP;
	}

	if (!BotInterface_ValidateClientNumber(client, "BotConsoleMessage"))
	{
		return BLERR_INVALIDCLIENTNUMBER;
	}

	bot_client_state_t *state = BotState_Get(client);
	if (state == NULL || !state->active)
	{
		BotInterface_Printf(PRT_ERROR,
			"recieved console message for inactive bot client\n");
		return BLERR_AICMFORINACTIVECLIENT;
	}

    if (state->chat_state == NULL)
    {
        BotInterface_Printf(PRT_WARNING,
                             "[bot_interface] BotConsoleMessage: client %d missing chat state\n",
                             client);
        return BLERR_CANNOTLOADICHAT;
    }

    if (message != NULL)
    {
        BotQueueConsoleMessage(state->chat_state, type, message);
    }

    return BLERR_NOERROR;
}

/*
=============
BotInterface_Test

Preserves the retail test export as a pure no-op.
=============
*/
static int BotInterface_Test(int parm0, char *parm1, vec3_t parm2, vec3_t parm3)
{
	(void)parm0;
	(void)parm1;
	(void)parm2;
	(void)parm3;

	return BLERR_NOERROR;
}

static int BotInterface_BotWeightIndex(int handle, const char *classname)
{
    if (!BotInterface_EnsureLibraryReady("BotWeightIndex"))
    {
        return -1;
    }

    return BotWeightIndex(handle, classname);
}

/*
=============
BotInterface_BotItemGoalInVisButNotVisible

Checks item goals against AAS visibility and trace results.
=============
*/
static int BotInterface_BotItemGoalInVisButNotVisible(int viewer,
	vec3_t eye,
	vec3_t viewangles,
	bot_goal_t *goal)
{
	if (!BotInterface_EnsureLibraryReady("BotItemGoalInVisButNotVisible"))
	{
		return 0;
	}

	return BotItemGoalInVisButNotVisible(viewer, eye, viewangles, goal);
}

static int BotInterface_BotTouchingGoal(const vec3_t origin, const bot_goal_t *goal)
{
    if (!BotInterface_EnsureLibraryReady("BotTouchingGoal"))
    {
        return 0;
    }

    return BotTouchingGoal(origin, goal);
}

static int BotInterface_BotAllocWeightConfig(void)
{
    if (!BotLibraryEnsureSetup("BotAllocWeightConfig"))
    {
        return 0;
    }

    return BotAllocWeightConfig();
}

static void BotInterface_BotFreeWeightConfig(int handle)
{
    if (!BotInterface_EnsureLibraryReady("BotFreeWeightConfig"))
    {
        return;
    }

    BotFreeWeightConfig(handle);
}

static void BotInterface_BotFreeWeightConfig2(bot_weight_config_t *config)
{
    if (!BotInterface_EnsureLibraryReady("BotFreeWeightConfig2"))
    {
        return;
    }

    BotFreeWeightConfig2(config);
}

static int BotInterface_BotLoadWeights(int handle, const char *filename)
{
    if (!BotInterface_EnsureLibraryReady("BotLoadWeights"))
    {
        return 0;
    }

    return BotLoadWeights(handle, filename);
}

static int BotInterface_BotWriteWeights(int handle, const char *filename)
{
    if (!BotInterface_EnsureLibraryReady("BotWriteWeights"))
    {
        return 0;
    }

    return BotWriteWeights(handle, filename);
}

static int BotInterface_BotSetWeight(int handle, const char *name, float value)
{
    if (!BotInterface_EnsureLibraryReady("BotSetWeight"))
    {
        return 0;
    }

    return BotSetWeight(handle, name, value);
}

/*
=============
BotInterface_BotFindFuzzyWeight

Bridge fuzzy weight name lookup through the weight handle table.
=============
*/
static int BotInterface_BotFindFuzzyWeight(int handle, const char *name)
{
	if (!BotInterface_EnsureLibraryReady("BotFindFuzzyWeight"))
	{
		return -1;
	}

	return BotFindFuzzyWeight(handle, name);
}

/*
=============
BotInterface_BotFuzzyWeightHandle

Bridge fuzzy weight evaluation through the weight handle table.
=============
*/
static float BotInterface_BotFuzzyWeightHandle(int handle,
											   const int *inventory,
											   int weight_index)
{
	if (!BotInterface_EnsureLibraryReady("BotFuzzyWeightHandle"))
	{
		return 0.0f;
	}

	return BotFuzzyWeightHandle(handle, inventory, weight_index);
}

static bot_weight_config_t *BotInterface_BotReadWeightsFile(const char *filename)
{
    if (!BotInterface_EnsureLibraryReady("BotReadWeightsFile"))
    {
        return NULL;
    }

    return BotReadWeightsFile(filename);
}

static int BotInterface_BotAllocMoveState(void)
{
    if (!BotInterface_EnsureLibraryReady("BotAllocMoveState"))
    {
        return 0;
    }

    return BotAllocMoveState();
}

static void BotInterface_BotFreeMoveState(int handle)
{
    if (!BotInterface_EnsureLibraryReady("BotFreeMoveState"))
    {
        return;
    }

    BotFreeMoveState(handle);
}

static void BotInterface_BotResetMoveState(int handle)
{
    if (!BotInterface_EnsureLibraryReady("BotResetMoveState"))
    {
        return;
    }

    BotResetMoveState(handle);
}

static void BotInterface_BotInitMoveState(int handle, const bot_initmove_t *initmove)
{
    if (!BotInterface_EnsureLibraryReady("BotInitMoveState"))
    {
        return;
    }

    BotInitMoveState(handle, initmove);
}

static void BotInterface_BotMoveToGoal(bot_moveresult_t *result,
                                       int movestate,
                                       const bot_goal_t *goal,
                                       int travelflags)
{
    if (!BotInterface_EnsureLibraryReady("BotMoveToGoal"))
    {
        if (result != NULL)
        {
            memset(result, 0, sizeof(*result));
        }
        return;
    }

    BotMoveToGoal(result, movestate, goal, travelflags);
}

static int BotInterface_BotMoveInDirection(int movestate, const vec3_t dir, float speed, int type)
{
    if (!BotInterface_EnsureLibraryReady("BotMoveInDirection"))
    {
        return 0;
    }

    return BotMoveInDirection(movestate, dir, speed, type);
}

static void BotInterface_BotResetAvoidReach(int movestate)
{
    if (!BotInterface_EnsureLibraryReady("BotResetAvoidReach"))
    {
        return;
    }

    BotMove_ResetAvoidReach(movestate);
}

/*
=============
BotInterface_BotResetLastAvoidReach

Bridge reset helper for the most recent avoided reachability.
=============
*/
static void BotInterface_BotResetLastAvoidReach(int movestate)
{
	if (!BotInterface_EnsureLibraryReady("BotResetLastAvoidReach"))
	{
		return;
	}

	BotResetLastAvoidReach(movestate);
}

/*
=============
BotInterface_BotReachabilityArea

Bridge reachability area lookup through the botlib API.
=============
*/
static int BotInterface_BotReachabilityArea(vec3_t origin, int client)
{
	if (!BotInterface_EnsureLibraryReady("BotReachabilityArea"))
	{
		return 0;
	}

	return BotReachabilityArea(origin, client);
}

/*
=============
BotInterface_BotMovementViewTarget

Bridge movement lookahead targeting through the botlib API.
=============
*/
static int BotInterface_BotMovementViewTarget(int movestate,
											  const bot_goal_t *goal,
											  int travelflags,
											  float lookahead,
											  vec3_t target)
{
	if (!BotInterface_EnsureLibraryReady("BotMovementViewTarget"))
	{
		return 0;
	}

	return BotMovementViewTarget(movestate, goal, travelflags, lookahead, target);
}

/*
=============
BotInterface_BotPredictVisiblePosition

Bridge visibility prediction through the botlib API.
=============
*/
static int BotInterface_BotPredictVisiblePosition(vec3_t origin,
												  int areanum,
												  const bot_goal_t *goal,
												  int travelflags,
												  vec3_t target)
{
	if (!BotInterface_EnsureLibraryReady("BotPredictVisiblePosition"))
	{
		return 0;
	}

	return BotPredictVisiblePosition(origin, areanum, goal, travelflags, target);
}

/*
=============
BotInterface_BotAddAvoidSpot

Bridge avoid-spot updates through the botlib movement API.
=============
*/
static void BotInterface_BotAddAvoidSpot(int movestate, vec3_t origin, float radius, int type)
{
	if (!BotInterface_EnsureLibraryReady("BotAddAvoidSpot"))
	{
		return;
	}

	BotAddAvoidSpot(movestate, origin, radius, type);
}

static int BotInterface_BotAllocWeaponState(void)
{
    if (!BotInterface_EnsureLibraryReady("BotAllocWeaponState"))
    {
        return 0;
    }

    return BotAllocWeaponState();
}

static void BotInterface_BotFreeWeaponState(int handle)
{
    if (!BotInterface_EnsureLibraryReady("BotFreeWeaponState"))
    {
        return;
    }

    BotFreeWeaponState(handle);
}

static void BotInterface_BotResetWeaponState(int handle)
{
    if (!BotInterface_EnsureLibraryReady("BotResetWeaponState"))
    {
        return;
    }

    BotResetWeaponState(handle);
}

static int BotInterface_BotLoadWeaponWeights(int weaponstate, const char *filename)
{
    if (!BotInterface_EnsureLibraryReady("BotLoadWeaponWeights"))
    {
        return BLERR_LIBRARYNOTSETUP;
    }

    return BotLoadWeaponWeights(weaponstate, filename);
}

static void BotInterface_BotFreeWeaponWeights(int weaponstate)
{
    if (!BotInterface_EnsureLibraryReady("BotFreeWeaponWeights"))
    {
        return;
    }

    BotFreeWeaponWeights(weaponstate);
}

static int BotInterface_BotChooseBestFightWeapon(int weaponstate, const int *inventory)
{
    if (!BotInterface_EnsureLibraryReady("BotChooseBestFightWeapon"))
    {
        return 0;
    }

    return BotChooseBestFightWeapon(weaponstate, inventory);
}

static int BotInterface_BotGetTopRankedWeapon(int weaponstate)
{
    if (!BotInterface_EnsureLibraryReady("BotGetTopRankedWeapon"))
    {
        return 0;
    }

    return BotGetTopRankedWeapon(weaponstate);
}

static void BotInterface_BotGetWeaponInfo(int weaponstate, int weapon, bot_weapon_info_t *weaponinfo)
{
    if (!BotInterface_EnsureLibraryReady("BotGetWeaponInfo"))
    {
        if (weaponinfo != NULL)
        {
            memset(weaponinfo, 0, sizeof(*weaponinfo));
        }
        return;
    }

    BotGetWeaponInfo(weaponstate, weapon, weaponinfo);
}

/*
=============
BotInterface_BotLoadCharacter

Guards and forwards Q3-style character loading.
=============
*/
static int BotInterface_BotLoadCharacter(const char *character_file, float skill)
{
	if (!BotInterface_EnsureLibraryReady("BotLoadCharacter"))
	{
		return 0;
	}

	return BotLoadCharacter(character_file, skill);
}

/*
=============
BotInterface_BotFreeCharacter

Guards and forwards character handle release.
=============
*/
static void BotInterface_BotFreeCharacter(int handle)
{
	if (!BotInterface_EnsureLibraryReady("BotFreeCharacter"))
	{
		return;
	}

	BotFreeCharacter(handle);
}

/*
=============
BotInterface_BotLoadCharacterSkill

Guards and forwards exact skill-specific character loading.
=============
*/
static int BotInterface_BotLoadCharacterSkill(const char *character_file, float skill)
{
	if (!BotInterface_EnsureLibraryReady("BotLoadCharacterSkill"))
	{
		return 0;
	}

	return BotLoadCharacterSkill(character_file, skill);
}

/*
=============
BotInterface_BotFreeCharacterStrings

Guards and forwards transient character profile cleanup.
=============
*/
static void BotInterface_BotFreeCharacterStrings(ai_character_profile_t *profile)
{
    if (!BotInterface_EnsureLibraryReady("BotFreeCharacterStrings"))
    {
        return;
    }

    BotFreeCharacterStrings(profile);
}

/*
=============
BotInterface_Characteristic_Float

Guards and forwards float characteristic queries.
=============
*/
static float BotInterface_Characteristic_Float(int handle, int index)
{
	if (!BotInterface_EnsureLibraryReady("Characteristic_Float"))
	{
		return 0.0f;
	}

	return Characteristic_Float(handle, index);
}

/*
=============
BotInterface_Characteristic_BFloat

Guards and forwards bounded float characteristic queries.
=============
*/
static float BotInterface_Characteristic_BFloat(int handle,
	int index,
	float minimum,
	float maximum)
{
	if (!BotInterface_EnsureLibraryReady("Characteristic_BFloat"))
	{
		return 0.0f;
	}

	return Characteristic_BFloat(handle, index, minimum, maximum);
}

/*
=============
BotInterface_Characteristic_Integer

Guards and forwards integer characteristic queries.
=============
*/
static int BotInterface_Characteristic_Integer(int handle, int index)
{
	if (!BotInterface_EnsureLibraryReady("Characteristic_Integer"))
	{
		return 0;
	}

	return Characteristic_Integer(handle, index);
}

/*
=============
BotInterface_Characteristic_BInteger

Guards and forwards bounded integer characteristic queries.
=============
*/
static int BotInterface_Characteristic_BInteger(int handle,
	int index,
	int minimum,
	int maximum)
{
	if (!BotInterface_EnsureLibraryReady("Characteristic_BInteger"))
	{
		return 0;
	}

	return Characteristic_BInteger(handle, index, minimum, maximum);
}

/*
=============
BotInterface_Characteristic_String

Guards and forwards string characteristic queries.
=============
*/
static void BotInterface_Characteristic_String(int handle,
	int index,
	char *buffer,
	int buffer_size)
{
	if (!BotInterface_EnsureLibraryReady("Characteristic_String"))
	{
		if (buffer != NULL && buffer_size > 0)
		{
			buffer[0] = '\0';
		}
		return;
	}

	Characteristic_String(handle, index, buffer, buffer_size);
}

static bot_chatstate_t *BotInterface_BotAllocChatState(void)
{
    if (!BotInterface_EnsureLibraryReady("BotAllocChatState"))
    {
        return NULL;
    }

    return BotAllocChatState();
}

static void BotInterface_BotFreeChatState(bot_chatstate_t *state)
{
    if (!BotInterface_EnsureLibraryReady("BotFreeChatState"))
    {
        return;
    }

    BotFreeChatState(state);
}

static int BotInterface_BotLoadChatFile(bot_chatstate_t *state, const char *chatfile, const char *chatname)
{
    if (!BotInterface_EnsureLibraryReady("BotLoadChatFile"))
    {
        return 0;
    }

    return BotLoadChatFile(state, chatfile, chatname);
}

static void BotInterface_BotFreeChatFile(bot_chatstate_t *state)
{
    if (!BotInterface_EnsureLibraryReady("BotFreeChatFile"))
    {
        return;
    }

    BotFreeChatFile(state);
}

static void BotInterface_BotQueueConsoleMessage(bot_chatstate_t *state, int type, const char *message)
{
    if (!BotInterface_EnsureLibraryReady("BotQueueConsoleMessage"))
    {
        return;
    }

    BotQueueConsoleMessage(state, type, message);
}

static int BotInterface_BotRemoveConsoleMessage(bot_chatstate_t *state, int type)
{
    if (!BotInterface_EnsureLibraryReady("BotRemoveConsoleMessage"))
    {
        return 0;
    }

    return BotRemoveConsoleMessage(state, type);
}

static int BotInterface_BotNextConsoleMessage(bot_chatstate_t *state,
                                              int *type,
                                              char *buffer,
                                              size_t buffer_size)
{
    if (!BotInterface_EnsureLibraryReady("BotNextConsoleMessage"))
    {
        if (type != NULL)
        {
            *type = 0;
        }
        if (buffer != NULL && buffer_size > 0)
        {
            buffer[0] = '\0';
        }
        return 0;
    }

    return BotNextConsoleMessage(state, type, buffer, buffer_size);
}

static size_t BotInterface_BotNumConsoleMessages(const bot_chatstate_t *state)
{
    if (!BotInterface_EnsureLibraryReady("BotNumConsoleMessages"))
    {
        return 0U;
    }

    return BotNumConsoleMessages(state);
}

/*
=============
BotInterface_BotNumInitialChats

Guards the reconstructed initial-chat count export.
=============
*/
static int BotInterface_BotNumInitialChats(const bot_chatstate_t *state, const char *type)
{
	if (!BotInterface_EnsureLibraryReady("BotNumInitialChats"))
	{
		return 0;
	}

	return BotNumInitialChats(state, type);
}

static void BotInterface_BotEnterChat(bot_chatstate_t *state, int client, int sendto)
{
    if (!BotInterface_EnsureLibraryReady("BotEnterChat"))
    {
        return;
    }

    BotEnterChat(state, client, sendto);
}

/*
=============
BotInterface_BotReplyChat

Guards and forwards the legacy folded-context reply export.
=============
*/
static int BotInterface_BotReplyChat(bot_chatstate_t *state,
	const char *message,
	unsigned long int context)
{
	if (!BotInterface_EnsureLibraryReady("BotReplyChat"))
	{
		return 0;
	}

	return BotReplyChat(state, message, context);
}

/*
=============
BotInterface_BotReplyChatWithContexts

Guards and forwards the Q3-shaped split-context reply export.
=============
*/
static int BotInterface_BotReplyChatWithContexts(bot_chatstate_t *state,
	const char *message,
	unsigned long int mcontext,
	unsigned long int vcontext,
	const char *var0,
	const char *var1,
	const char *var2,
	const char *var3,
	const char *var4,
	const char *var5,
	const char *var6,
	const char *var7)
{
	if (!BotInterface_EnsureLibraryReady("BotReplyChatWithContexts"))
	{
		return 0;
	}

	return BotReplyChatWithContexts(state,
		message,
		mcontext,
		vcontext,
		var0,
		var1,
		var2,
		var3,
		var4,
		var5,
		var6,
		var7);
}

/*
=============
BotInterface_BotInitialChat

Guards and forwards the reconstructed initial-chat construction export.
=============
*/
static int BotInterface_BotInitialChat(bot_chatstate_t *state,
	const char *type,
	unsigned long context,
	...)
{
	const char *variables[11] = {0};
	if (!BotInterface_EnsureLibraryReady("BotInitialChat"))
	{
		return 0;
	}

	va_list args;
	va_start(args, context);
	for (size_t i = 0; i < sizeof(variables) / sizeof(variables[0]); ++i)
	{
		const char *value = va_arg(args, const char *);
		if (value == NULL)
		{
			break;
		}
		variables[i] = value;
	}
	va_end(args);

	return BotInitialChat(state,
		type,
		context,
		variables[0],
		variables[1],
		variables[2],
		variables[3],
		variables[4],
		variables[5],
		variables[6],
		variables[7],
		variables[8],
		variables[9],
		variables[10],
		NULL);
}

/*
=============
BotInterface_BotChatLength

Returns the constructed chat message length for a chat state.
=============
*/
static int BotInterface_BotChatLength(const bot_chatstate_t *state)
{
	if (!BotInterface_EnsureLibraryReady("BotChatLength"))
	{
		return 0;
	}

	return BotChatLength(state);
}

static void BotInterface_BotGetChatMessage(bot_chatstate_t *state, char *buffer, int buffer_size)
{
	if (!BotInterface_EnsureLibraryReady("BotGetChatMessage"))
	{
		if (buffer != NULL && buffer_size > 0)
		{
			buffer[0] = '\0';
		}
		return;
	}

	BotGetChatMessage(state, buffer, buffer_size);
}

static void BotInterface_BotSetChatGender(bot_chatstate_t *state, int gender)
{
	if (!BotInterface_EnsureLibraryReady("BotSetChatGender"))
	{
		return;
	}

	BotSetChatGender(state, gender);
}

static void BotInterface_BotSetChatName(bot_chatstate_t *state, const char *name, int client)
{
	if (!BotInterface_EnsureLibraryReady("BotSetChatName"))
	{
		return;
	}

	BotSetChatName(state, name, client);
}

/*
=============
BotInterface_StringContains

Guards and forwards the reconstructed chat substring helper.
=============
*/
static int BotInterface_StringContains(const char *str1,
	const char *str2,
	int casesensitive)
{
	if (!BotInterface_EnsureLibraryReady("StringContains"))
	{
		return -1;
	}

	return StringContains(str1, str2, casesensitive);
}

/*
=============
BotInterface_BotFindMatch

Guards and forwards the reconstructed setup match-template export.
=============
*/
static int BotInterface_BotFindMatch(const char *str,
	bot_match_t *match,
	unsigned long int context)
{
	if (!BotInterface_EnsureLibraryReady("BotFindMatch"))
	{
		if (match != NULL)
		{
			memset(match, 0, sizeof(*match));
		}
		return 0;
	}

	return BotFindMatch(str, match, context);
}

/*
=============
BotInterface_BotMatchVariable

Guards and forwards captured match-variable extraction.
=============
*/
static void BotInterface_BotMatchVariable(const bot_match_t *match,
	int variable,
	char *buffer,
	int buffer_size)
{
	if (!BotInterface_EnsureLibraryReady("BotMatchVariable"))
	{
		if (buffer != NULL && buffer_size > 0)
		{
			buffer[0] = '\0';
		}
		return;
	}

	BotMatchVariable(match, variable, buffer, buffer_size);
}

/*
=============
BotInterface_UnifyWhiteSpaces

Guards and forwards retail chat whitespace canonicalization.
=============
*/
static void BotInterface_UnifyWhiteSpaces(char *string)
{
	if (!BotInterface_EnsureLibraryReady("UnifyWhiteSpaces"))
	{
		return;
	}

	UnifyWhiteSpaces(string);
}

/*
=============
BotInterface_BotReplaceSynonyms

Guards and forwards setup-cache synonym replacement.
=============
*/
static void BotInterface_BotReplaceSynonyms(char *string, unsigned long int context)
{
	if (!BotInterface_EnsureLibraryReady("BotReplaceSynonyms"))
	{
		return;
	}

	BotReplaceSynonyms(string, context);
}

/*
=============
BotInterface_GetBotAPI

Builds the export table from an explicitly bounded caller import table.
=============
*/
static bot_export_t *BotInterface_GetBotAPI(bot_import_t *import,
	size_t import_size)
{
	static bot_export_t exportTable;

	memset(&exportTable, 0, sizeof(exportTable));

	BotInterface_FreeImportCache();
	BotInterface_InitialiseImportTable(import, import_size);
	BotInterface_BuildImportTable(import);
	BotMemory_SetAllocatorCallbacks(g_botImport != NULL ? g_botImport->GetMemory : NULL,
		g_botImport != NULL ? g_botImport->FreeMemory : NULL);

	/*
	 * HLIL ordering: capture the allocator imports, translate compatibility
	 * shims, install the botlib import table, feed the bridge import table, and
	 * reset cached update translation state.
	 */
	BotInterface_SetImportTable(&g_botInterfaceImportTable);
	Q2Bridge_SetImportTable(g_botImport);
	Bridge_ResetCachedUpdates();
	Q2Bridge_SetDebugLinesEnabled(g_botInterfaceDebugDrawEnabled);
	assert(g_botImport != NULL);

    exportTable.BotVersion = BotVersion;
    exportTable.BotSetupLibrary = BotSetupLibraryWrapper;
    exportTable.BotShutdownLibrary = BotShutdownLibraryWrapper;
    exportTable.BotLibraryInitialized = BotLibraryInitializedWrapper;
    exportTable.BotLibVarSet = BotLibVarSetWrapper;
    exportTable.BotDefine = BotDefineWrapper;
    exportTable.BotLoadMap = BotLoadMap;
    exportTable.BotSetupClient = BotSetupClient;
    exportTable.BotShutdownClient = BotShutdownClient;
    exportTable.BotMoveClient = BotMoveClient;
    exportTable.BotClientSettings = BotClientSettings;
    exportTable.BotSettings = BotSettings;
    exportTable.BotStartFrame = BotStartFrame;
    exportTable.BotUpdateClient = BotUpdateClient;
    exportTable.BotUpdateEntity = BotUpdateEntity;
    exportTable.BotAddSound = BotAddSound;
    exportTable.BotAddPointLight = BotAddPointLight;
    exportTable.BotAI = BotAI;
    exportTable.BotConsoleMessage = BotConsoleMessage;
    exportTable.Test = BotInterface_Test;
    exportTable.BotAllocGoalState = AI_GoalBotlib_AllocState;
    exportTable.BotFreeGoalState = AI_GoalBotlib_FreeState;
    exportTable.BotResetGoalState = AI_GoalBotlib_ResetState;
    exportTable.BotLoadItemWeights = AI_GoalBotlib_LoadItemWeights;
    exportTable.BotFreeItemWeights = AI_GoalBotlib_FreeItemWeights;
    exportTable.BotWeightIndex = BotInterface_BotWeightIndex;
    exportTable.BotPushGoal = AI_GoalBotlib_PushGoal;
    exportTable.BotPopGoal = AI_GoalBotlib_PopGoal;
    exportTable.BotEmptyGoalStack = AI_GoalBotlib_EmptyGoalStack;
    exportTable.BotGetTopGoal = AI_GoalBotlib_GetTopGoal;
    exportTable.BotGetSecondGoal = AI_GoalBotlib_GetSecondGoal;
    exportTable.BotChooseLTGItem = AI_GoalBotlib_ChooseLTG;
    exportTable.BotChooseNBGItem = AI_GoalBotlib_ChooseNBG;
    exportTable.BotResetAvoidGoals = AI_GoalBotlib_ResetAvoidGoals;
    exportTable.BotAddAvoidGoal = AI_GoalBotlib_AddAvoidGoal;
    exportTable.BotRemoveFromAvoidGoals = AI_GoalBotlib_RemoveFromAvoidGoals;
    exportTable.BotAvoidGoalTime = AI_GoalBotlib_AvoidGoalTime;
    exportTable.BotSetAvoidGoalTime = AI_GoalBotlib_SetAvoidGoalTime;
    exportTable.BotDumpAvoidGoals = AI_GoalBotlib_DumpAvoidGoals;
    exportTable.BotDumpGoalStack = AI_GoalBotlib_DumpGoalStack;
    exportTable.BotGoalName = AI_GoalBotlib_GoalName;
    exportTable.BotGetLevelItemGoal = AI_GoalBotlib_GetLevelItemGoal;
    exportTable.BotGetNextCampSpotGoal = AI_GoalBotlib_GetNextCampSpotGoal;
    exportTable.BotGetMapLocationGoal = AI_GoalBotlib_GetMapLocationGoal;
    exportTable.BotInterbreedGoalFuzzyLogic = AI_GoalBotlib_InterbreedGoalFuzzyLogic;
    exportTable.BotSaveGoalFuzzyLogic = AI_GoalBotlib_SaveGoalFuzzyLogic;
    exportTable.BotMutateGoalFuzzyLogic = AI_GoalBotlib_MutateGoalFuzzyLogic;
    exportTable.BotUpdateGoalState = AI_GoalBotlib_Update;
	exportTable.BotUpdateEntityItems = AI_GoalBotlib_UpdateEntityItems;
    exportTable.BotRegisterLevelItem = AI_GoalBotlib_RegisterLevelItem;
    exportTable.BotUnregisterLevelItem = AI_GoalBotlib_UnregisterLevelItem;
    exportTable.BotMarkLevelItemTaken = AI_GoalBotlib_MarkItemTaken;
	exportTable.BotItemGoalInVisButNotVisible = BotInterface_BotItemGoalInVisButNotVisible;
    exportTable.BotTouchingGoal = BotInterface_BotTouchingGoal;
    exportTable.BotAllocWeightConfig = BotInterface_BotAllocWeightConfig;
    exportTable.BotFreeWeightConfig = BotInterface_BotFreeWeightConfig;
    exportTable.BotFreeWeightConfig2 = BotInterface_BotFreeWeightConfig2;
    exportTable.BotLoadWeights = BotInterface_BotLoadWeights;
    exportTable.BotWriteWeights = BotInterface_BotWriteWeights;
    exportTable.BotSetWeight = BotInterface_BotSetWeight;
    exportTable.BotFindFuzzyWeight = BotInterface_BotFindFuzzyWeight;
    exportTable.BotFuzzyWeightHandle = BotInterface_BotFuzzyWeightHandle;
    exportTable.BotReadWeightsFile = BotInterface_BotReadWeightsFile;
    exportTable.BotAllocMoveState = BotInterface_BotAllocMoveState;
    exportTable.BotFreeMoveState = BotInterface_BotFreeMoveState;
    exportTable.BotResetMoveState = BotInterface_BotResetMoveState;
    exportTable.BotInitMoveState = BotInterface_BotInitMoveState;
	exportTable.BotMoveToGoal = BotInterface_BotMoveToGoal;
    exportTable.BotMoveInDirection = BotInterface_BotMoveInDirection;
    exportTable.BotResetAvoidReach = BotInterface_BotResetAvoidReach;
	exportTable.BotResetLastAvoidReach = BotInterface_BotResetLastAvoidReach;
	exportTable.BotReachabilityArea = BotInterface_BotReachabilityArea;
	exportTable.BotMovementViewTarget = BotInterface_BotMovementViewTarget;
	exportTable.BotPredictVisiblePosition = BotInterface_BotPredictVisiblePosition;
	exportTable.BotAddAvoidSpot = BotInterface_BotAddAvoidSpot;
    exportTable.BotLoadCharacter = BotInterface_BotLoadCharacter;
    exportTable.BotFreeCharacter = BotInterface_BotFreeCharacter;
    exportTable.BotLoadCharacterSkill = BotInterface_BotLoadCharacterSkill;
    exportTable.BotFreeCharacterStrings = BotInterface_BotFreeCharacterStrings;
    exportTable.Characteristic_Float = BotInterface_Characteristic_Float;
    exportTable.Characteristic_BFloat = BotInterface_Characteristic_BFloat;
    exportTable.Characteristic_Integer = BotInterface_Characteristic_Integer;
    exportTable.Characteristic_BInteger = BotInterface_Characteristic_BInteger;
    exportTable.Characteristic_String = BotInterface_Characteristic_String;
    exportTable.BotAllocWeaponState = BotInterface_BotAllocWeaponState;
    exportTable.BotFreeWeaponState = BotInterface_BotFreeWeaponState;
    exportTable.BotResetWeaponState = BotInterface_BotResetWeaponState;
    exportTable.BotLoadWeaponWeights = BotInterface_BotLoadWeaponWeights;
    exportTable.BotFreeWeaponWeights = BotInterface_BotFreeWeaponWeights;
    exportTable.BotChooseBestFightWeapon = BotInterface_BotChooseBestFightWeapon;
    exportTable.BotGetTopRankedWeapon = BotInterface_BotGetTopRankedWeapon;
    exportTable.BotGetWeaponInfo = BotInterface_BotGetWeaponInfo;
    exportTable.BotAllocChatState = BotInterface_BotAllocChatState;
    exportTable.BotFreeChatState = BotInterface_BotFreeChatState;
    exportTable.BotLoadChatFile = BotInterface_BotLoadChatFile;
    exportTable.BotFreeChatFile = BotInterface_BotFreeChatFile;
    exportTable.BotQueueConsoleMessage = BotInterface_BotQueueConsoleMessage;
    exportTable.BotRemoveConsoleMessage = BotInterface_BotRemoveConsoleMessage;
    exportTable.BotNextConsoleMessage = BotInterface_BotNextConsoleMessage;
    exportTable.BotNumConsoleMessages = BotInterface_BotNumConsoleMessages;
    exportTable.BotEnterChat = BotInterface_BotEnterChat;
    exportTable.BotReplyChat = BotInterface_BotReplyChat;
	exportTable.BotReplyChatWithContexts = BotInterface_BotReplyChatWithContexts;
    exportTable.BotChatLength = BotInterface_BotChatLength;
	exportTable.BotNumInitialChats = BotInterface_BotNumInitialChats;
	exportTable.BotInitialChat = BotInterface_BotInitialChat;
    exportTable.BotGetChatMessage = BotInterface_BotGetChatMessage;
    exportTable.BotSetChatGender = BotInterface_BotSetChatGender;
    exportTable.BotSetChatName = BotInterface_BotSetChatName;
    exportTable.StringContains = BotInterface_StringContains;
    exportTable.BotFindMatch = BotInterface_BotFindMatch;
    exportTable.BotMatchVariable = BotInterface_BotMatchVariable;
    exportTable.UnifyWhiteSpaces = BotInterface_UnifyWhiteSpaces;
    exportTable.BotReplaceSynonyms = BotInterface_BotReplaceSynonyms;

	return &exportTable;
}

/*
=============
GetBotAPI

Copies only the immutable ten-callback Gladiator 0.96 import prefix.
=============
*/
GLADIATOR_API bot_export_t *GetBotAPI(bot_import_t *import)
{
	return BotInterface_GetBotAPI(import, BOT_IMPORT_RETAIL_SIZE);
}

/*
=============
GetBotAPIEx

Accepts the reconstruction's optional import tail through an explicit size.
=============
*/
GLADIATOR_API bot_export_t *GetBotAPIEx(bot_import_t *import,
	size_t import_size)
{
	return BotInterface_GetBotAPI(import, import_size);
}
