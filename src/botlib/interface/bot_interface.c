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
#include <ctype.h>

#include "../../q2bridge/aas_translation.h"
#include "../../q2bridge/botlib.h"
#include "../../q2bridge/bridge.h"
#include "../../q2bridge/bridge_config.h"
#include "../../q2bridge/update_translator.h"
#include "../common/l_libvar.h"
#include "../common/l_log.h"
#include "../aas/aas_map.h"
#include "../aas/aas_local.h"
#include "../aas/aas_sound.h"
#include "../aas/aas_debug.h"
#include "../ai/chat/ai_chat.h"
#include "../ai/character/bot_character.h"
#include "../ai/ai_dm.h"
#include "../ai/weight/bot_weight.h"
#include "../ai/goal/ai_goal.h"
#include "../ai/goal_move_orchestrator.h"
#include "../ai/move/mover_catalogue.h"
#include "bot_interface_assets.h"
#include "../ea/ea_local.h"
#include "../precomp/l_precomp.h"
#include "botlib_interface.h"
#include "bot_interface.h"
#include "bot_state.h"

static void BotInterface_Printf(int priority, const char *fmt, ...);


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

static int BotInterface_StringCompareIgnoreCase(const char *lhs, const char *rhs)
{
    if (lhs == NULL || rhs == NULL)
    {
        return (lhs == rhs) ? 0 : (lhs != NULL ? 1 : -1);
    }

    while (*lhs != '\0' && *rhs != '\0')
    {
        int diff = tolower((unsigned char)*lhs) - tolower((unsigned char)*rhs);
        if (diff != 0)
        {
            return diff;
        }

        ++lhs;
        ++rhs;
    }

    return tolower((unsigned char)*lhs) - tolower((unsigned char)*rhs);
}

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

static bool BotInterface_RecordMapAssets(const char *mapname,
                                         int modelindexes,
                                         char *modelindex[],
                                         int soundindexes,
                                         char *soundindex[],
                                         int imageindexes,
                                         char *imageindex[])
{
    if (mapname != NULL)
    {
        strncpy(g_botInterfaceMapCache.map_name,
                mapname,
                sizeof(g_botInterfaceMapCache.map_name) - 1);
        g_botInterfaceMapCache.map_name[sizeof(g_botInterfaceMapCache.map_name) - 1] = '\0';
    }
    else
    {
        g_botInterfaceMapCache.map_name[0] = '\0';
    }

    if (!BotInterface_CopyAssetList(&g_botInterfaceMapCache.models, modelindexes, modelindex))
    {
        return false;
    }

    if (!BotInterface_CopyAssetList(&g_botInterfaceMapCache.sounds, soundindexes, soundindex))
    {
        return false;
    }

    if (!BotInterface_CopyAssetList(&g_botInterfaceMapCache.images, imageindexes, imageindex))
    {
        return false;
    }

    return true;
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
    g_botInterfaceFrameNumber += 1U;
    Bridge_SetFrameTime(time);
    AAS_SoundSubsystem_SetFrameTime(time);
    AAS_RouteFrameUpdate();
    AAS_ReachabilityFrameUpdate();
    BotInterface_ResetFrameQueues();

    for (int client = 0; client < MAX_CLIENTS; ++client)
    {
        bot_client_state_t *state = BotState_Get(client);
        if (state != NULL && state->active && state->weapon_state > 0)
        {
            BotResetWeaponState(state->weapon_state);
        }
    }
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
    g_botImport->Print(type, "%s", buffer);
}

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

static int BotInterface_BotLibVarGetWrapper(const char *var_name, char *value, size_t size)
{
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
    if (entry == NULL || entry->value == NULL)
    {
        return -1;
    }

    size_t length = strlen(entry->value);
    if (length >= size)
    {
        length = size - 1;
    }

    memcpy(value, entry->value, length);
    value[length] = '\0';
    return 0;
}

static int BotInterface_BotLibVarSetWrapper(const char *var_name, const char *value)
{
    if (var_name == NULL || value == NULL)
    {
        return -1;
    }

    botinterface_import_libvar_t *entry = BotInterface_EnsureImportLibVar(var_name);
    if (entry == NULL)
    {
        return -1;
    }

    char *copy = BotInterface_CopyString(value);
    if (copy == NULL)
    {
        return -1;
    }

    free(entry->value);
    entry->value = copy;
    return 0;
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

static int BotInterface_BotLibVarGetShim(const char *name, char *buffer, size_t buffer_size)
{
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
            return BLERR_NOERROR;
        }
    }

    buffer[0] = '\0';
    return BLERR_INVALIDIMPORT;
}

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

    g_botImport->Print(priority, "%s", buffer);
}

static void BotInterface_InitialiseImportTable(bot_import_t *imports)
{
    g_botImport = imports;

    memset(&g_botlibImportTable, 0, sizeof(g_botlibImportTable));
    g_botlibImportTable.Print = BotInterface_PrintShim;
    g_botlibImportTable.BotLibVarGet = BotInterface_BotLibVarGetShim;
    g_botlibImportTable.BotLibVarSet = NULL;
    g_botlibImportTable.AddCommand = NULL;
    g_botlibImportTable.RemoveCommand = NULL;
    g_botlibImportTable.CmdArgc = NULL;
    g_botlibImportTable.CmdArgv = NULL;

    BotInterface_SetImportTable(&g_botlibImportTable);
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
                                 "[bot_interface] %s: library not initialised\\n",
                                 function_name);
        }
        return false;
    }

    return true;
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

static int BotSetupLibraryWrapper(void)
{
    BotInterface_PrintBanner(PRT_MESSAGE, "------- BotLib Initialization -------\n");
    BotInterface_PrintBanner(PRT_MESSAGE, "BotLib v0.96\n");
    BotInterface_SetImportTable(&g_botlibImportTable);

    int result = BotSetupLibrary();
    if (result != BLERR_NOERROR)
    {
        return result;
    }

    return result;
}

static int BotShutdownLibraryWrapper(void)
{
    int result = BotShutdownLibrary();

    BotInterface_PrintBanner(PRT_MESSAGE, "------- BotLib Shutdown -------\n");

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

static int BotLibraryInitializedWrapper(void)
{
    return BotLibraryInitialized() ? 1 : 0;
}

static int BotLibVarSetWrapper(char *var_name, char *value)
{
    if (var_name == NULL || value == NULL)
    {
        return BLERR_INVALIDIMPORT;
    }

    if (!BotInterface_UpdateImportCache(var_name, value))
    {
        return BLERR_INVALIDIMPORT;
    }

    int status = LibVarSetStatus(var_name, value);
    if (status != BLERR_NOERROR)
    {
        return status;
    }

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

static int BotDefineWrapper(char *string)
{
    if (string == NULL)
    {
        return BLERR_INVALIDIMPORT;
    }

    if (!PC_AddGlobalDefine(string))
    {
        return BLERR_INVALIDIMPORT;
    }

    return BLERR_NOERROR;
}

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

    if (!BotLibraryInitialized())
    {
        BotInterface_Printf(PRT_ERROR, "[bot_interface] BotLoadMap: library not initialised\n");
        return BLERR_LIBRARYNOTSETUP;
    }

    if (mapname == NULL || *mapname == '\0')
    {
        BotInterface_Printf(PRT_ERROR, "[bot_interface] BotLoadMap: no map specified\n");
        return BLERR_NOAASFILE;
    }

    Bridge_ResetCachedUpdates();
    BotInterface_ResetFrameQueues();
    BotInterface_ResetEntityCache();
    BotInterface_ResetMapCache();
    TranslateEntity_SetWorldLoaded(qfalse);

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

    if (!BotMove_MoverCatalogueFinalize(&g_botInterfaceMapCache.models))
    {
        BotInterface_Printf(PRT_WARNING,
                             "[bot_interface] BotLoadMap: failed to finalize mover catalogue for %s\n",
                             mapname);
        return BLERR_INVALIDIMPORT;
    }

    TranslateEntity_SetWorldLoaded(qtrue);
    TranslateEntity_SetCurrentTime(0.0f);

    return BLERR_NOERROR;
}

static int BotSetupClient(int client, bot_settings_t *settings)
{
    if (g_botImport == NULL)
    {
        return BLERR_LIBRARYNOTSETUP;
    }

    if (client < 0 || client >= MAX_CLIENTS)
    {
        BotInterface_Printf(PRT_ERROR, "[bot_interface] BotSetupClient: invalid client %d\n", client);
        return BLERR_INVALIDCLIENTNUMBER;
    }

    if (settings == NULL)
    {
        BotInterface_Printf(PRT_ERROR, "[bot_interface] BotSetupClient: NULL settings pointer for client %d\n", client);
        return BLERR_INVALIDIMPORT;
    }

    if (BotState_Get(client) != NULL)
    {
        BotInterface_Printf(PRT_WARNING, "[bot_interface] BotSetupClient: client %d already active\n", client);
        return BLERR_AICLIENTALREADYSETUP;
    }

    bot_client_state_t *state = BotState_Create(client);
    if (state == NULL)
    {
        BotInterface_Printf(PRT_ERROR, "[bot_interface] BotSetupClient: failed to allocate state for client %d\n", client);
        return BLERR_INVALIDIMPORT;
    }

    state->weapon_state = BotAllocWeaponState();
    if (state->weapon_state <= 0)
    {
        BotInterface_Printf(PRT_ERROR,
                            "[bot_interface] BotSetupClient: failed to allocate weapon state for client %d\n",
                            client);
        BotState_Destroy(client);
        return BLERR_INVALIDIMPORT;
    }

    memcpy(&state->settings, settings, sizeof(*settings));

    int character_handle = BotLoadCharacter(settings->characterfile, 1.0f);
    if (character_handle <= 0)
    {
        BotInterface_Printf(PRT_ERROR,
                            "[bot_interface] BotSetupClient: failed to load character '%s' for client %d\n",
                            settings->characterfile,
                            client);
        BotState_Destroy(client);
        return BLERR_INVALIDIMPORT;
    }

    BotState_AttachCharacter(state, character_handle);

    state->goal_state = AI_GoalState_Create();
    if (state->goal_state == NULL)
    {
        BotInterface_Printf(PRT_ERROR,
                            "[bot_interface] BotSetupClient: failed to allocate goal state for client %d\n",
                            client);
        BotState_Destroy(client);
        return BLERR_INVALIDIMPORT;
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
        return BLERR_INVALIDIMPORT;
    }

    AI_GoalBotlib_ResetState(state->goal_handle);

    state->move_state = AI_MoveState_Create();
    if (state->move_state == NULL)
    {
        BotInterface_Printf(PRT_ERROR,
                            "[bot_interface] BotSetupClient: failed to allocate move state for client %d\n",
                            client);
        AI_GoalState_Destroy(state->goal_state);
        state->goal_state = NULL;
        BotState_Destroy(client);
        return BLERR_INVALIDIMPORT;
    }

    state->move_handle = BotAllocMoveState();
    if (state->move_handle <= 0)
    {
        BotInterface_Printf(PRT_ERROR,
                            "[bot_interface] BotSetupClient: failed to allocate move handle for client %d\n",
                            client);
        BotState_Destroy(client);
        return BLERR_INVALIDIMPORT;
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
        return BLERR_INVALIDIMPORT;
    }

    Bridge_ClearClientSlot(client);
    Bridge_SetClientActive(client, qtrue);
    state->active = true;
    return BLERR_NOERROR;
}

static int BotShutdownClient(int client)
{
    if (g_botImport == NULL)
    {
        return BLERR_LIBRARYNOTSETUP;
    }

    if (client < 0 || client >= MAX_CLIENTS)
    {
        BotInterface_Printf(PRT_ERROR, "[bot_interface] BotShutdownClient: invalid client %d\n", client);
        return BLERR_INVALIDCLIENTNUMBER;
    }

    bot_client_state_t *state = BotState_Get(client);
    if (state == NULL || !state->active)
    {
        BotInterface_Printf(PRT_WARNING, "[bot_interface] BotShutdownClient: client %d not active\n", client);
        return BLERR_AICLIENTALREADYSHUTDOWN;
    }

    BotState_Destroy(client);
    Bridge_SetClientActive(client, qfalse);
    Bridge_ClearClientSlot(client);
    return BLERR_NOERROR;
}

static int BotMoveClient(int oldclnum, int newclnum)
{
    if (g_botImport == NULL)
    {
        return BLERR_LIBRARYNOTSETUP;
    }

    if (oldclnum < 0 || oldclnum >= MAX_CLIENTS)
    {
        BotInterface_Printf(PRT_ERROR, "[bot_interface] BotMoveClient: invalid source client %d\n", oldclnum);
        return BLERR_INVALIDCLIENTNUMBER;
    }

    if (newclnum < 0 || newclnum >= MAX_CLIENTS)
    {
        BotInterface_Printf(PRT_ERROR, "[bot_interface] BotMoveClient: invalid destination client %d\n", newclnum);
        return BLERR_INVALIDCLIENTNUMBER;
    }

    if (oldclnum == newclnum)
    {
        return BLERR_NOERROR;
    }

    bot_client_state_t *state = BotState_Get(oldclnum);
    if (state == NULL || !state->active)
    {
        BotInterface_Printf(PRT_WARNING, "[bot_interface] BotMoveClient: source client %d inactive\n", oldclnum);
        return BLERR_AIMOVEINACTIVECLIENT;
    }

    if (BotState_Get(newclnum) != NULL)
    {
        BotInterface_Printf(PRT_WARNING, "[bot_interface] BotMoveClient: destination client %d already active\n", newclnum);
        return BLERR_AIMOVETOACTIVECLIENT;
    }

    BotState_Move(oldclnum, newclnum);
    int status = Bridge_MoveClientSlot(oldclnum, newclnum);
    if (status != BLERR_NOERROR)
    {
        BotInterface_Printf(PRT_ERROR,
                            "[bot_interface] BotMoveClient: bridge move failed for %d -> %d\n",
                            oldclnum,
                            newclnum);
        BotState_Move(newclnum, oldclnum);
        return status;
    }
    Bridge_SetClientActive(oldclnum, qfalse);
    Bridge_SetClientActive(newclnum, qtrue);

    return BLERR_NOERROR;
}

static int BotClientSettings(int client, bot_clientsettings_t *settings)
{
    if (g_botImport == NULL)
    {
        return BLERR_LIBRARYNOTSETUP;
    }

    if (settings == NULL)
    {
        BotInterface_Printf(PRT_ERROR, "[bot_interface] BotClientSettings: NULL output buffer\n");
        return BLERR_INVALIDIMPORT;
    }

    bot_client_state_t *state = BotState_Get(client);
    if (state == NULL || !state->active)
    {
        BotInterface_Printf(PRT_WARNING, "[bot_interface] BotClientSettings: client %d inactive\n", client);
        memset(settings, 0, sizeof(*settings));
        return BLERR_SETTINGSINACTIVECLIENT;
    }

    memcpy(settings, &state->client_settings, sizeof(*settings));
    return BLERR_NOERROR;
}

static int BotSettings(int client, bot_settings_t *settings)
{
    if (g_botImport == NULL)
    {
        return BLERR_LIBRARYNOTSETUP;
    }

    if (settings == NULL)
    {
        BotInterface_Printf(PRT_ERROR, "[bot_interface] BotSettings: NULL output buffer\n");
        return BLERR_INVALIDIMPORT;
    }

    bot_client_state_t *state = BotState_Get(client);
    if (state == NULL || !state->active)
    {
        BotInterface_Printf(PRT_WARNING, "[bot_interface] BotSettings: client %d inactive\n", client);
        memset(settings, 0, sizeof(*settings));
        return BLERR_AICLIENTNOTSETUP;
    }

    memcpy(settings, &state->settings, sizeof(*settings));
    return BLERR_NOERROR;
}

static int BotStartFrame(float time)
{
    if (g_botImport == NULL)
    {
        return BLERR_LIBRARYNOTSETUP;
    }

    if (!BotLibraryInitialized())
    {
        BotInterface_Printf(PRT_ERROR, "[bot_interface] BotStartFrame: library not initialised\n");
        return BLERR_LIBRARYNOTSETUP;
    }

    BotInterface_BeginFrame(time);
    AAS_FrameSynchronise(time);
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
    if (g_botImport == NULL)
    {
        return BLERR_LIBRARYNOTSETUP;
    }

    if (!BotLibraryInitialized())
    {
        BotInterface_Printf(PRT_ERROR, "[bot_interface] BotUpdateClient: library not initialised\n");
        return BLERR_LIBRARYNOTSETUP;
    }

    bot_client_state_t *state = BotState_Get(client);
    if (state == NULL || !state->active)
    {
        BotInterface_Printf(PRT_WARNING, "[bot_interface] BotUpdateClient: client %d inactive\n", client);
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

static int BotUpdateEntity(int ent, bot_updateentity_t *bue)
{
    if (g_botImport == NULL)
    {
        return BLERR_LIBRARYNOTSETUP;
    }

    if (!BotLibraryInitialized())
    {
        BotInterface_Printf(PRT_ERROR, "[bot_interface] BotUpdateEntity: library not initialised\n");
        return BLERR_LIBRARYNOTSETUP;
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

static int BotAddSound(vec3_t origin,
                       int ent,
                       int channel,
                       int soundindex,
                       float volume,
                       float attenuation,
                       float timeofs)
{
    if (g_botImport == NULL)
    {
        return BLERR_LIBRARYNOTSETUP;
    }

    if (!BotLibraryInitialized())
    {
        BotInterface_Printf(PRT_ERROR, "[bot_interface] BotAddSound: library not initialised\n");
        return BLERR_LIBRARYNOTSETUP;
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

static int BotAddPointLight(vec3_t origin,
                            int ent,
                            float radius,
                            float r,
                            float g,
                            float b,
                            float time,
                            float decay)
{
    if (g_botImport == NULL)
    {
        return BLERR_LIBRARYNOTSETUP;
    }

    if (!BotLibraryInitialized())
    {
        BotInterface_Printf(PRT_ERROR, "[bot_interface] BotAddPointLight: library not initialised\n");
        return BLERR_LIBRARYNOTSETUP;
    }

    BotInterface_EnqueuePointLight(origin, ent, radius, r, g, b, time, decay);
    return BLERR_NOERROR;
}

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

    if (state->weapon_state > 0)
    {
        state->current_weapon = BotChooseBestFightWeapon(state->weapon_state,
                                                         state->last_client_update.inventory);
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

    ai_dm_enemy_info_t enemy_info;
    BotAI_InitEnemyInfo(&enemy_info);
    if (state->dm_state != NULL)
    {
        BotAI_FindEnemy(state, &enemy_info);
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

    bot_input_t final_input = {0};
    status = EA_GetInput(state->client_number, thinktime, &final_input);
    if (status != BLERR_NOERROR)
    {
        return status;
    }

    Q2_BotInput(state->client_number, &final_input);

    state->client_update_valid = false;
    return BLERR_NOERROR;
}

static int BotAI(int client, float thinktime)
{
    if (g_botImport == NULL)
    {
        return BLERR_LIBRARYNOTSETUP;
    }

    if (!BotLibraryInitialized())
    {
        BotInterface_Printf(PRT_ERROR, "[bot_interface] BotAI: library not initialised\n");
        return BLERR_LIBRARYNOTSETUP;
    }

    bot_client_state_t *state = BotState_Get(client);
    if (state == NULL || !state->active)
    {
        BotInterface_Printf(PRT_WARNING, "[bot_interface] BotAI: client %d inactive\n", client);
        return BLERR_AICLIENTNOTSETUP;
    }

    return BotAI_Think(state, thinktime);
}

static int BotConsoleMessage(int client, int type, char *message)
{
    if (g_botImport == NULL)
    {
        return BLERR_LIBRARYNOTSETUP;
    }

    if (!BotLibraryInitialized())
    {
        BotInterface_Printf(PRT_ERROR, "[bot_interface] BotConsoleMessage: library not initialised\n");
        return BLERR_LIBRARYNOTSETUP;
    }

    bot_client_state_t *state = BotState_Get(client);
    if (state == NULL || !state->active)
    {
        BotInterface_Printf(PRT_WARNING, "[bot_interface] BotConsoleMessage: client %d inactive\n", client);
        return BLERR_AICLIENTNOTSETUP;
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

static int BotInterface_Test(int parm0, char *parm1, vec3_t parm2, vec3_t parm3)
{
    if (g_botImport == NULL)
    {
        return BLERR_LIBRARYNOTSETUP;
    }

    if (!BotLibraryInitialized())
    {
        BotInterface_Printf(PRT_ERROR, "[bot_interface] Test: library not initialised\n");
        return BLERR_LIBRARYNOTSETUP;
    }

    if (parm1 == NULL || *parm1 == '\0')
    {
        BotInterface_Printf(PRT_MESSAGE,
                             "[bot_interface] Test commands: dump_chat, sounds, pointlights, debug_draw, bot_test, aas_showpath, aas_showareas, teleport_mover\n");
        return BLERR_INVALIDIMPORT;
    }

    char command_buffer[256];
    strncpy(command_buffer, parm1, sizeof(command_buffer) - 1);
    command_buffer[sizeof(command_buffer) - 1] = '\0';

    char *arguments = command_buffer;
    while (*arguments != '\0' && !isspace((unsigned char)*arguments))
    {
        ++arguments;
    }

    if (*arguments != '\0')
    {
        *arguments++ = '\0';
        while (isspace((unsigned char)*arguments))
        {
            ++arguments;
        }
    }
    else
    {
        arguments = NULL;
    }

    char *command = command_buffer;

    if (BotInterface_StringCompareIgnoreCase(command, "dump_chat") == 0)
    {
        int client = parm0;
        if (arguments != NULL && *arguments != '\0')
        {
            client = (int)strtol(arguments, NULL, 10);
        }

        if (client < 0 || client >= MAX_CLIENTS)
        {
            BotInterface_Printf(PRT_ERROR,
                                 "[bot_interface] Test dump_chat: client %d out of range\n",
                                 client);
            return BLERR_INVALIDCLIENTNUMBER;
        }

        bot_client_state_t *state = BotState_Get(client);
        if (state == NULL || state->chat_state == NULL)
        {
            BotInterface_Printf(PRT_WARNING,
                                 "[bot_interface] Test dump_chat: client %d has no chat state\n",
                                 client);
            return BLERR_AICLIENTNOTSETUP;
        }

        size_t pending = BotNumConsoleMessages(state->chat_state);
        BotInterface_Printf(PRT_MESSAGE,
                             "[bot_interface] Test dump_chat: %zu pending messages for client %d\n",
                             pending,
                             client);

        if (pending == 0)
        {
            return BLERR_NOERROR;
        }

        typedef struct botinterface_test_message_s
        {
            int type;
            char text[256];
        } botinterface_test_message_t;

        botinterface_test_message_t *messages =
            (botinterface_test_message_t *)calloc(pending, sizeof(botinterface_test_message_t));
        if (messages == NULL)
        {
            BotInterface_Printf(PRT_ERROR, "[bot_interface] Test dump_chat: allocation failed\n");
            return BLERR_INVALIDIMPORT;
        }

        size_t captured = 0;
        for (size_t index = 0; index < pending; ++index)
        {
            int type = 0;
            char text[256];
            if (BotNextConsoleMessage(state->chat_state, &type, text, sizeof(text)))
            {
                BotInterface_Printf(PRT_MESSAGE,
                                     "[bot_interface] chat[%d]: (%d) %s\n",
                                     client,
                                     type,
                                     text);
                messages[captured].type = type;
                strncpy(messages[captured].text, text, sizeof(messages[captured].text) - 1);
                messages[captured].text[sizeof(messages[captured].text) - 1] = '\0';
                captured += 1U;
            }
        }

        for (size_t index = 0; index < captured; ++index)
        {
            BotQueueConsoleMessage(state->chat_state,
                                   messages[index].type,
                                   messages[index].text);
        }

        free(messages);
        return BLERR_NOERROR;
    }

    if (BotInterface_StringCompareIgnoreCase(command, "teleport_mover") == 0)
    {
        if (arguments == NULL || *arguments == '\0')
        {
            BotInterface_Printf(PRT_ERROR,
                                 "[bot_interface] Test teleport_mover: expected <client> <target>\n");
            return BLERR_INVALIDIMPORT;
        }

        char *cursor = arguments;
        char *endptr = NULL;
        long client_value = strtol(cursor, &endptr, 10);
        if (endptr == cursor)
        {
            BotInterface_Printf(PRT_ERROR,
                                 "[bot_interface] Test teleport_mover: invalid client argument '%s'\n",
                                 arguments);
            return BLERR_INVALIDIMPORT;
        }

        while (*endptr != '\0' && isspace((unsigned char)*endptr))
        {
            ++endptr;
        }

        if (*endptr == '\0')
        {
            BotInterface_Printf(PRT_ERROR,
                                 "[bot_interface] Test teleport_mover: expected mover entity after client\n");
            return BLERR_INVALIDIMPORT;
        }

        cursor = endptr;
        long mover_value = strtol(cursor, &endptr, 10);
        if (endptr == cursor)
        {
            BotInterface_Printf(PRT_ERROR,
                                 "[bot_interface] Test teleport_mover: invalid mover argument '%s'\n",
                                 cursor);
            return BLERR_INVALIDIMPORT;
        }

        int client = (int)client_value;
        int mover_ent = (int)mover_value;

        if (client < 0 || client >= MAX_CLIENTS)
        {
            BotInterface_Printf(PRT_ERROR,
                                 "[bot_interface] Test teleport_mover: client %d out of range\n",
                                 client);
            return BLERR_INVALIDCLIENTNUMBER;
        }

        bot_client_state_t *state = BotState_Get(client);
        if (state == NULL || !state->active)
        {
            BotInterface_Printf(PRT_WARNING,
                                 "[bot_interface] Test teleport_mover: client %d not active\n",
                                 client);
            return BLERR_AICLIENTNOTSETUP;
        }

        if (!state->client_update_valid)
        {
            BotInterface_Printf(PRT_WARNING,
                                 "[bot_interface] Test teleport_mover: client %d has no snapshot\n",
                                 client);
            return BLERR_AIUPDATEINACTIVECLIENT;
        }

        if (mover_ent < 0 || mover_ent >= BOT_INTERFACE_MAX_ENTITIES)
        {
            BotInterface_Printf(PRT_ERROR,
                                 "[bot_interface] Test teleport_mover: mover %d out of range\n",
                                 mover_ent);
            return BLERR_INVALIDENTITYNUMBER;
        }

        if (!g_botInterfaceEntityCache[mover_ent].valid)
        {
            BotInterface_Printf(PRT_WARNING,
                                 "[bot_interface] Test teleport_mover: mover entity %d has no snapshot\n",
                                 mover_ent);
            return BLERR_INVALIDENTITYNUMBER;
        }

        const bot_updateentity_t *snapshot = &g_botInterfaceEntityCache[mover_ent].state;
        const bot_mover_catalogue_entry_t *entry = BotMove_MoverCatalogueFindByModel(snapshot->modelindex);
        if (entry == NULL)
        {
            BotInterface_Printf(PRT_WARNING,
                                 "[bot_interface] Test teleport_mover: entity %d (modelindex %d) is not a mover\n",
                                 mover_ent,
                                 snapshot->modelindex);
            return BLERR_INVALIDIMPORT;
        }

        bot_updateclient_t update = state->last_client_update;
        VectorCopy(snapshot->origin, update.origin);
        VectorClear(update.velocity);
        update.pm_flags |= PMF_TIME_TELEPORT;
        if (update.pm_time <= 0)
        {
            update.pm_time = 1;
        }
        VectorCopy(snapshot->angles, update.viewangles);

        int status = BotUpdateClient(client, &update);
        if (status != BLERR_NOERROR)
        {
            BotInterface_Printf(PRT_ERROR,
                                 "[bot_interface] Test teleport_mover: failed to update client %d (%d)\n",
                                 client,
                                 status);
            return status;
        }

        BotInterface_Printf(PRT_MESSAGE,
                             "[bot_interface] Test teleport_mover: moved client %d to mover %d origin (%.1f %.1f %.1f)\n",
                             client,
                             mover_ent,
                             snapshot->origin[0],
                             snapshot->origin[1],
                             snapshot->origin[2]);

        return BLERR_NOERROR;
    }

    if (BotInterface_StringCompareIgnoreCase(command, "sounds") == 0)
    {
        const aas_sound_event_summary_t *summaries = NULL;
        size_t sound_count = AAS_SoundSubsystem_SoundSummaries(&summaries);
        BotInterface_Printf(PRT_MESSAGE,
                             "[bot_interface] Test sounds: %zu queued (%zu assets)\n",
                             sound_count,
                             g_botInterfaceMapCache.sounds.count);

        for (size_t index = 0; index < sound_count; ++index)
        {
            const aas_sound_event_summary_t *summary = &summaries[index];
            if (summary == NULL || summary->event == NULL)
            {
                continue;
            }

            const aas_sound_event_t *event = summary->event;
            const char *asset = AAS_SoundSubsystem_AssetName(event->soundindex);
            if (asset == NULL && event->soundindex >= 0
                && (size_t)event->soundindex < g_botInterfaceMapCache.sounds.count
                && g_botInterfaceMapCache.sounds.entries != NULL)
            {
                asset = g_botInterfaceMapCache.sounds.entries[event->soundindex];
            }

            const aas_soundinfo_t *info = summary->info;
            const char *info_name =
                (info != NULL && info->name[0] != '\0') ? info->name : "<unknown>";

            const char *expired = summary->expired ? "true" : "false";
            const char *expired_frame = summary->expired_this_frame ? "true" : "false";
            char expiry_buffer[64];
            if (summary->has_expiry)
            {
                snprintf(expiry_buffer,
                         sizeof(expiry_buffer),
                         "%.3f",
                         summary->expiry_time);
            }
            else
            {
                strncpy(expiry_buffer, "n/a", sizeof(expiry_buffer) - 1U);
                expiry_buffer[sizeof(expiry_buffer) - 1U] = '\0';
            }

            BotInterface_Printf(PRT_MESSAGE,
                                 "[bot_interface] sound[%zu]: ent=%d channel=%d index=%d asset=%s info=%s type=%d timestamp=%.3f expiry=%s expired=%s expired_this_frame=%s\n",
                                 index,
                                 event->ent,
                                 event->channel,
                                 event->soundindex,
                                 (asset != NULL) ? asset : "<unknown>",
                                 info_name,
                                 summary->sound_type,
                                 summary->timestamp,
                                 expiry_buffer,
                                 expired,
                                 expired_frame);
        }

        return BLERR_NOERROR;
    }

    if (BotInterface_StringCompareIgnoreCase(command, "pointlights") == 0)
    {
        const aas_pointlight_event_summary_t *summaries = NULL;
        size_t light_count = AAS_SoundSubsystem_PointLightSummaries(&summaries);
        BotInterface_Printf(PRT_MESSAGE,
                             "[bot_interface] Test pointlights: %zu queued\n",
                             light_count);

        for (size_t index = 0; index < light_count; ++index)
        {
            const aas_pointlight_event_summary_t *summary = &summaries[index];
            if (summary == NULL || summary->event == NULL)
            {
                continue;
            }

            const aas_pointlight_event_t *event = summary->event;
            const char *expired = summary->expired ? "true" : "false";
            const char *expired_frame = summary->expired_this_frame ? "true" : "false";
            char expiry_buffer[64];
            if (summary->has_expiry)
            {
                snprintf(expiry_buffer,
                         sizeof(expiry_buffer),
                         "%.3f",
                         summary->expiry_time);
            }
            else
            {
                strncpy(expiry_buffer, "n/a", sizeof(expiry_buffer) - 1U);
                expiry_buffer[sizeof(expiry_buffer) - 1U] = '\0';
            }

            BotInterface_Printf(PRT_MESSAGE,
                                 "[bot_interface] light[%zu]: ent=%d origin=(%.1f %.1f %.1f) radius=%.1f color=(%.2f %.2f %.2f) timestamp=%.3f expiry=%s expired=%s expired_this_frame=%s\n",
                                 index,
                                 event->ent,
                                 event->origin[0],
                                 event->origin[1],
                                 event->origin[2],
                                 event->radius,
                                 event->color[0],
                                 event->color[1],
                                 event->color[2],
                                 summary->timestamp,
                                 expiry_buffer,
                                 expired,
                                 expired_frame);
        }

        return BLERR_NOERROR;
    }

    if (BotInterface_StringCompareIgnoreCase(command, "bot_test") == 0)
    {
        AAS_DebugBotTest(parm0, arguments, parm2, parm3);
        return BLERR_NOERROR;
    }

    if (BotInterface_StringCompareIgnoreCase(command, "aas_showpath") == 0)
    {
        int startArea = 0;
        int goalArea = 0;

        if (arguments != NULL && *arguments != '\0')
        {
            char *cursor = arguments;
            char *endptr = NULL;
            startArea = (int)strtol(cursor, &endptr, 10);
            if (endptr != cursor)
            {
                cursor = endptr;
                while (*cursor != '\0' && (isspace((unsigned char)*cursor) || *cursor == ','))
                {
                    ++cursor;
                }

                if (*cursor != '\0')
                {
                    goalArea = (int)strtol(cursor, &endptr, 10);
                    if (endptr != cursor)
                    {
                        cursor = endptr;
                    }
                }
            }
        }

        AAS_DebugShowPath(startArea, goalArea, parm2, parm3);
        return BLERR_NOERROR;
    }

    if (BotInterface_StringCompareIgnoreCase(command, "aas_showareas") == 0)
    {
        int areaBuffer[32];
        size_t areaCount = 0U;

        if (arguments != NULL && *arguments != '\0')
        {
            char *cursor = arguments;
            while (*cursor != '\0' && areaCount < (sizeof(areaBuffer) / sizeof(areaBuffer[0])))
            {
                while (*cursor != '\0' && (isspace((unsigned char)*cursor) || *cursor == ','))
                {
                    ++cursor;
                }

                if (*cursor == '\0')
                {
                    break;
                }

                char *endptr = NULL;
                long value = strtol(cursor, &endptr, 10);
                if (endptr == cursor)
                {
                    ++cursor;
                    continue;
                }

                areaBuffer[areaCount++] = (int)value;
                cursor = endptr;
            }
        }

        AAS_DebugShowAreas((areaCount > 0U) ? areaBuffer : NULL, areaCount);
        return BLERR_NOERROR;
    }

    if (BotInterface_StringCompareIgnoreCase(command, "debug_draw") == 0)
    {
        if (arguments != NULL && *arguments != '\0')
        {
            if (BotInterface_StringCompareIgnoreCase(arguments, "on") == 0)
            {
                g_botInterfaceDebugDrawEnabled = true;
                Q2Bridge_SetDebugLinesEnabled(g_botInterfaceDebugDrawEnabled);
            }
            else if (BotInterface_StringCompareIgnoreCase(arguments, "off") == 0)
            {
                g_botInterfaceDebugDrawEnabled = false;
                Q2Bridge_SetDebugLinesEnabled(g_botInterfaceDebugDrawEnabled);
            }
            else
            {
                g_botInterfaceDebugDrawEnabled = (strtol(arguments, NULL, 10) != 0);
                Q2Bridge_SetDebugLinesEnabled(g_botInterfaceDebugDrawEnabled);
            }
        }
        else
        {
            g_botInterfaceDebugDrawEnabled = !g_botInterfaceDebugDrawEnabled;
            Q2Bridge_SetDebugLinesEnabled(g_botInterfaceDebugDrawEnabled);
        }

        BotInterface_Printf(PRT_MESSAGE,
                             "[bot_interface] Test debug_draw: %s\n",
                             g_botInterfaceDebugDrawEnabled ? "enabled" : "disabled");
        return BLERR_NOERROR;
    }

    BotInterface_Printf(PRT_WARNING,
                         "[bot_interface] Test: unknown command '%s'\n",
                         command);
    return BLERR_INVALIDIMPORT;
}

static int BotInterface_BotWeightIndex(int handle, const char *classname)
{
    if (!BotInterface_EnsureLibraryReady("BotWeightIndex"))
    {
        return -1;
    }

    return BotWeightIndex(handle, classname);
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
    if (!BotInterface_EnsureLibraryReady("BotAllocWeightConfig"))
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

static void BotInterface_BotFreeCharacterStrings(ai_character_profile_t *profile)
{
    if (!BotInterface_EnsureLibraryReady("BotFreeCharacterStrings"))
    {
        return;
    }

    BotFreeCharacterStrings(profile);
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

static void BotInterface_BotEnterChat(bot_chatstate_t *state, int client, int sendto)
{
    if (!BotInterface_EnsureLibraryReady("BotEnterChat"))
    {
        return;
    }

    BotEnterChat(state, client, sendto);
}

static int BotInterface_BotReplyChat(bot_chatstate_t *state, const char *message, unsigned long int context)
{
    if (!BotInterface_EnsureLibraryReady("BotReplyChat"))
    {
        return 0;
    }

    return BotReplyChat(state, message, context);
}

static int BotInterface_BotChatLength(const char *message)
{
    if (!BotInterface_EnsureLibraryReady("BotChatLength"))
    {
        return 0;
    }

    return BotChatLength(message);
}

GLADIATOR_API bot_export_t *GetBotAPI(bot_import_t *import)
{
    static bot_export_t exportTable;

    memset(&exportTable, 0, sizeof(exportTable));

    BotInterface_FreeImportCache();
    BotInterface_InitialiseImportTable(import);
    g_botImport = import;
    BotInterface_BuildImportTable(import);
    BotInterface_SetImportTable(&g_botInterfaceImportTable);
    Q2Bridge_SetImportTable(import);
    Q2Bridge_SetDebugLinesEnabled(g_botInterfaceDebugDrawEnabled);
    Bridge_ResetCachedUpdates();
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
    exportTable.BotGetTopGoal = AI_GoalBotlib_GetTopGoal;
    exportTable.BotGetSecondGoal = AI_GoalBotlib_GetSecondGoal;
    exportTable.BotChooseLTGItem = AI_GoalBotlib_ChooseLTG;
    exportTable.BotChooseNBGItem = AI_GoalBotlib_ChooseNBG;
    exportTable.BotResetAvoidGoals = AI_GoalBotlib_ResetAvoidGoals;
    exportTable.BotAddAvoidGoal = AI_GoalBotlib_AddAvoidGoal;
    exportTable.BotUpdateGoalState = AI_GoalBotlib_Update;
    exportTable.BotRegisterLevelItem = AI_GoalBotlib_RegisterLevelItem;
    exportTable.BotUnregisterLevelItem = AI_GoalBotlib_UnregisterLevelItem;
    exportTable.BotMarkLevelItemTaken = AI_GoalBotlib_MarkItemTaken;
    exportTable.BotTouchingGoal = BotInterface_BotTouchingGoal;
    exportTable.BotAllocWeightConfig = BotInterface_BotAllocWeightConfig;
    exportTable.BotFreeWeightConfig = BotInterface_BotFreeWeightConfig;
    exportTable.BotFreeWeightConfig2 = BotInterface_BotFreeWeightConfig2;
    exportTable.BotLoadWeights = BotInterface_BotLoadWeights;
    exportTable.BotWriteWeights = BotInterface_BotWriteWeights;
    exportTable.BotSetWeight = BotInterface_BotSetWeight;
    exportTable.BotReadWeightsFile = BotInterface_BotReadWeightsFile;
    exportTable.BotAllocMoveState = BotInterface_BotAllocMoveState;
    exportTable.BotFreeMoveState = BotInterface_BotFreeMoveState;
    exportTable.BotResetMoveState = BotInterface_BotResetMoveState;
    exportTable.BotInitMoveState = BotInterface_BotInitMoveState;
    exportTable.BotMoveToGoal = BotInterface_BotMoveToGoal;
    exportTable.BotMoveInDirection = BotInterface_BotMoveInDirection;
    exportTable.BotResetAvoidReach = BotInterface_BotResetAvoidReach;
    exportTable.BotLoadCharacter = BotLoadCharacter;
    exportTable.BotFreeCharacter = BotFreeCharacter;
    exportTable.BotLoadCharacterSkill = BotLoadCharacterSkill;
    exportTable.BotFreeCharacterStrings = BotInterface_BotFreeCharacterStrings;
    exportTable.Characteristic_Float = Characteristic_Float;
    exportTable.Characteristic_BFloat = Characteristic_BFloat;
    exportTable.Characteristic_Integer = Characteristic_Integer;
    exportTable.Characteristic_BInteger = Characteristic_BInteger;
    exportTable.Characteristic_String = Characteristic_String;
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
    exportTable.BotChatLength = BotInterface_BotChatLength;

    return &exportTable;
}

