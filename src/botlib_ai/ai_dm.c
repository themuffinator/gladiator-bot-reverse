#include "ai_dm.h"

#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "botlib_ai/goal_move_orchestrator.h"
#include "botlib_common/l_libvar.h"
#include "botlib_ea/ea_local.h"
#include "botlib_interface/bot_state.h"
#include "q2bridge/bridge_config.h"

#ifndef AI_DM_ATTACK_COOLDOWN
#define AI_DM_ATTACK_COOLDOWN 0.2f
#endif

#ifndef AI_DM_AVOID_DURATION
#define AI_DM_AVOID_DURATION 1.5f
#endif

#ifndef AI_DM_MIN_ROCKET_VERTICAL
#define AI_DM_MIN_ROCKET_VERTICAL 48.0f
#endif

#ifndef AI_DM_MAX_CHASE_SPEED
#define AI_DM_MAX_CHASE_SPEED 400.0f
#endif

#ifndef AI_DM_REACTION_DELAY
#define AI_DM_REACTION_DELAY 0.3f
#endif

#ifndef AI_DM_CHASE_DURATION
#define AI_DM_CHASE_DURATION 2.5f
#endif

#ifndef AI_DM_ATTACK_STRAFE_INTERVAL
#define AI_DM_ATTACK_STRAFE_INTERVAL 0.6f
#endif

#ifndef AI_DM_ATTACK_CHASE_DURATION
#define AI_DM_ATTACK_CHASE_DURATION 1.0f
#endif

struct ai_dm_state_s
{
    int client_number;
    libvar_t *dmflags;
    libvar_t *rocketjump;
    libvar_t *usehook;
    int dmflags_value;
    bool allow_rocket_jump;
    bool allow_hook;
    bool config_initialised;
    float last_attack_time;
    float last_jump_time;
    float attack_cooldown;
    float avoid_duration;
    int last_selected_weapon;
    int last_avoid_entity;
    float last_avoid_time;
    int enemy_entity;
    int revenge_enemy;
    int revenge_kills;
    bool enemy_visible;
    int strafe_side;
    pmtype_t last_pm_type;
    float enemyvisible_time;
    float enemysight_time;
    float enemydeath_time;
    float enemyposition_time;
    float chase_time;
    float attackstrafe_timer;
    float attackcrouch_time;
    float attackchase_time;
    float attackjump_time;
    float firethrottlewait_time;
    float firethrottleshoot_time;
    float last_think_time;
    float reaction_delay;
    float chase_duration;
    float attack_strafe_interval;
    float attack_chase_duration;
    vec3_t last_enemy_origin;
    vec3_t last_enemy_velocity;
};

static float AI_DMVectorNormalise(vec3_t vector)
{
    if (vector == NULL)
    {
        return 0.0f;
    }

    float length_sq = vector[0] * vector[0] + vector[1] * vector[1] + vector[2] * vector[2];
    if (length_sq <= 0.0f)
    {
        VectorClear(vector);
        return 0.0f;
    }

    float length = sqrtf(length_sq);
    float inv = 1.0f / length;
    vector[0] *= inv;
    vector[1] *= inv;
    vector[2] *= inv;
    return length;
}

static void AI_DMState_RefreshConfig(ai_dm_state_t *state)
{
    if (state == NULL)
    {
        return;
    }

    if (state->dmflags != NULL)
    {
        state->dmflags_value = (int)state->dmflags->value;
        if (state->dmflags->modified && state->dmflags->name != NULL)
        {
            LibVarSetNotModified(state->dmflags->name);
        }
    }
    else
    {
        state->dmflags_value = 0;
    }

    if (state->rocketjump != NULL)
    {
        if (!state->config_initialised || state->rocketjump->modified)
        {
            state->allow_rocket_jump = (state->rocketjump->value != 0.0f);
            if (state->rocketjump->name != NULL)
            {
                LibVarSetNotModified(state->rocketjump->name);
            }
        }
    }
    else
    {
        state->allow_rocket_jump = false;
    }

    if (state->usehook != NULL)
    {
        if (!state->config_initialised || state->usehook->modified)
        {
            state->allow_hook = (state->usehook->value != 0.0f);
            if (state->usehook->name != NULL)
            {
                LibVarSetNotModified(state->usehook->name);
            }
        }
    }
    else
    {
        state->allow_hook = false;
    }

    state->config_initialised = true;
}

ai_dm_state_t *AI_DMState_Create(int client_number)
{
    ai_dm_state_t *state = (ai_dm_state_t *)calloc(1, sizeof(*state));
    if (state == NULL)
    {
        return NULL;
    }

    state->client_number = client_number;
    state->attack_cooldown = AI_DM_ATTACK_COOLDOWN;
    state->avoid_duration = AI_DM_AVOID_DURATION;
    state->last_selected_weapon = -1;
    state->last_avoid_entity = -1;
    state->reaction_delay = AI_DM_REACTION_DELAY;
    state->chase_duration = AI_DM_CHASE_DURATION;
    state->attack_strafe_interval = AI_DM_ATTACK_STRAFE_INTERVAL;
    state->attack_chase_duration = AI_DM_ATTACK_CHASE_DURATION;
    state->dmflags = Bridge_DMFlags();
    state->rocketjump = Bridge_RocketJump();
    state->usehook = Bridge_UseHook();

    AI_DMState_Reset(state);
    return state;
}

void AI_DMState_Destroy(ai_dm_state_t *state)
{
    if (state == NULL)
    {
        return;
    }

    free(state);
}

void AI_DMState_Reset(ai_dm_state_t *state)
{
    if (state == NULL)
    {
        return;
    }

    state->last_attack_time = -FLT_MAX;
    state->last_jump_time = -FLT_MAX;
    state->last_selected_weapon = -1;
    state->last_avoid_entity = -1;
    state->last_avoid_time = -FLT_MAX;
    state->enemy_entity = -1;
    state->revenge_enemy = -1;
    state->revenge_kills = 0;
    state->enemy_visible = false;
    state->strafe_side = 1;
    state->last_pm_type = PM_NORMAL;
    state->enemyvisible_time = -FLT_MAX;
    state->enemysight_time = -FLT_MAX;
    state->enemydeath_time = -FLT_MAX;
    state->enemyposition_time = -FLT_MAX;
    state->chase_time = -FLT_MAX;
    state->attackstrafe_timer = 0.0f;
    state->attackcrouch_time = -FLT_MAX;
    state->attackchase_time = -FLT_MAX;
    state->attackjump_time = -FLT_MAX;
    state->firethrottlewait_time = -FLT_MAX;
    state->firethrottleshoot_time = -FLT_MAX;
    state->last_think_time = -FLT_MAX;
    VectorClear(state->last_enemy_origin);
    VectorClear(state->last_enemy_velocity);
    state->config_initialised = false;
    AI_DMState_RefreshConfig(state);
}

void AI_DMState_SetClient(ai_dm_state_t *state, int client_number)
{
    if (state == NULL)
    {
        return;
    }

    state->client_number = client_number;
}

void AI_DMState_GetMetrics(const ai_dm_state_t *state, ai_dm_metrics_t *out_metrics)
{
    if (state == NULL || out_metrics == NULL)
    {
        return;
    }

    memset(out_metrics, 0, sizeof(*out_metrics));
    out_metrics->client_number = state->client_number;
    out_metrics->enemy_entity = state->enemy_entity;
    out_metrics->revenge_enemy = state->revenge_enemy;
    out_metrics->revenge_kills = state->revenge_kills;
    out_metrics->enemy_visible = state->enemy_visible;
    out_metrics->last_attack_time = state->last_attack_time;
    out_metrics->last_jump_time = state->last_jump_time;
    out_metrics->enemyvisible_time = state->enemyvisible_time;
    out_metrics->enemysight_time = state->enemysight_time;
    out_metrics->enemydeath_time = state->enemydeath_time;
    out_metrics->enemyposition_time = state->enemyposition_time;
    out_metrics->chase_time = state->chase_time;
    out_metrics->attackstrafe_timer = state->attackstrafe_timer;
    out_metrics->attackcrouch_time = state->attackcrouch_time;
    out_metrics->attackchase_time = state->attackchase_time;
    out_metrics->attackjump_time = state->attackjump_time;
    out_metrics->firethrottlewait_time = state->firethrottlewait_time;
    out_metrics->firethrottleshoot_time = state->firethrottleshoot_time;
    out_metrics->reaction_delay = state->reaction_delay;
    out_metrics->chase_duration = state->chase_duration;
    out_metrics->attack_strafe_interval = state->attack_strafe_interval;
    VectorCopy(state->last_enemy_origin, out_metrics->last_enemy_origin);
    VectorCopy(state->last_enemy_velocity, out_metrics->last_enemy_velocity);
}

static float AI_DMState_ClampSpeed(const ai_dm_state_t *state, const ai_dm_enemy_info_t *enemy)
{
    if (state == NULL || enemy == NULL || !enemy->valid)
    {
        return 0.0f;
    }

    float base_speed = AI_DM_MAX_CHASE_SPEED;
    if (!state->allow_hook && enemy->distance > 512.0f)
    {
        base_speed *= 0.5f;
    }
    return base_speed;
}

static bool AI_DMState_ShouldRecordAvoid(const ai_dm_state_t *state,
                                         const ai_dm_enemy_info_t *enemy,
                                         float now)
{
    if (state == NULL || enemy == NULL || !enemy->valid)
    {
        return false;
    }

    if (enemy->entity < 0)
    {
        return false;
    }

    if (state->last_avoid_entity == enemy->entity)
    {
        if (now - state->last_avoid_time < 0.1f)
        {
            return false;
        }
    }

    return true;
}

void AI_DMState_Update(ai_dm_state_t *state,
                       const bot_client_state_t *client_state,
                       const ai_goal_selection_t *selection,
                       const ai_dm_enemy_info_t *enemy,
                       const bot_input_t *last_move_command,
                       float now)
{
    if (state == NULL || client_state == NULL)
    {
        return;
    }

    float think_delta = 0.0f;
    if (state->last_think_time > -FLT_MAX * 0.5f)
    {
        think_delta = now - state->last_think_time;
        if (think_delta < 0.0f)
        {
            think_delta = 0.0f;
        }
    }
    state->last_think_time = now;

    AI_DMState_RefreshConfig(state);

    int client = client_state->client_number;
    if (client < 0)
    {
        return;
    }

    bool movement_controls_weapon =
        client_state->has_move_result &&
        (client_state->last_move_result.flags & MOVERESULT_MOVEMENTWEAPON);

    int desired_weapon = client_state->current_weapon;
    if (movement_controls_weapon)
    {
        desired_weapon = client_state->last_move_result.weapon;
    }

    bool allow_weapon_selection = (!movement_controls_weapon || desired_weapon > 0);
    if (movement_controls_weapon && desired_weapon <= 0)
    {
        state->last_selected_weapon = -1;
    }

    if (allow_weapon_selection && desired_weapon > 0 && desired_weapon != state->last_selected_weapon)
    {
        EA_SelectWeapon(client, desired_weapon);
        state->last_selected_weapon = desired_weapon;
    }

    bool enemy_valid = (enemy != NULL) && enemy->valid;
    vec3_t eye_position = {0.0f, 0.0f, 0.0f};
    VectorCopy(client_state->last_client_update.origin, eye_position);

    pmtype_t pm_type = client_state->last_client_update.pm_type;
    if (state->last_pm_type != PM_DEAD && pm_type == PM_DEAD)
    {
        if (state->enemy_entity >= 0)
        {
            state->revenge_enemy = state->enemy_entity;
            state->revenge_kills += 1;
        }

        state->enemy_entity = -1;
        state->enemy_visible = false;
        state->chase_time = -FLT_MAX;
    }
    state->last_pm_type = pm_type;

    if (enemy_valid)
    {
        bool enemy_changed = (enemy->entity != state->enemy_entity);
        bool enemy_was_visible = state->enemy_visible;

        state->enemy_entity = enemy->entity;
        state->enemy_visible = enemy->visible;
        state->enemyvisible_time = (enemy->last_seen_time > -FLT_MAX * 0.5f) ? enemy->last_seen_time : now;
        state->chase_time = now + state->chase_duration;

        if (enemy_changed)
        {
            state->enemysight_time = now;
            state->attackstrafe_timer = 0.0f;
            state->strafe_side = 1;
        }
        else if (!enemy_was_visible)
        {
            state->enemysight_time = now;
            state->attackstrafe_timer = 0.0f;
            state->strafe_side = 1;
        }
        else if (state->enemysight_time <= -FLT_MAX * 0.5f)
        {
            state->enemysight_time = now;
        }

        if (enemy->triggered_by_damage || enemy->is_shooting)
        {
            float instant = now - state->reaction_delay;
            if (state->enemysight_time > instant)
            {
                state->enemysight_time = instant;
            }
        }

        if (state->enemyposition_time > -FLT_MAX * 0.5f)
        {
            float position_delta = now - state->enemyposition_time;
            if (position_delta > 0.0001f)
            {
                vec3_t delta;
                VectorSubtract(enemy->origin, state->last_enemy_origin, delta);
                float scale = 1.0f / position_delta;
                state->last_enemy_velocity[0] = delta[0] * scale;
                state->last_enemy_velocity[1] = delta[1] * scale;
                state->last_enemy_velocity[2] = delta[2] * scale;
            }
        }
        else
        {
            VectorCopy(enemy->velocity, state->last_enemy_velocity);
        }
        VectorCopy(enemy->origin, state->last_enemy_origin);
        state->enemyposition_time = now;

        state->attackstrafe_timer += think_delta;
        if (state->attackstrafe_timer >= state->attack_strafe_interval)
        {
            state->attackstrafe_timer = 0.0f;
            state->strafe_side = -state->strafe_side;
        }

        state->attackchase_time = now + state->attack_chase_duration;

        vec3_t forward;
        VectorSubtract(enemy->origin, eye_position, forward);
        float distance = AI_DMVectorNormalise(forward);
        vec3_t move_dir;
        VectorCopy(forward, move_dir);

        vec3_t strafe = {forward[1], -forward[0], 0.0f};
        if (AI_DMVectorNormalise(strafe) > 0.0f)
        {
            move_dir[0] += (float)state->strafe_side * strafe[0];
            move_dir[1] += (float)state->strafe_side * strafe[1];
            move_dir[2] += (float)state->strafe_side * strafe[2];
            AI_DMVectorNormalise(move_dir);
        }

        float speed = AI_DMState_ClampSpeed(state, enemy);
        EA_Move(client, move_dir, speed);

        vec3_t look_target;
        VectorCopy(enemy->origin, look_target);
        if (state->reaction_delay > 0.0f && distance > 0.0f)
        {
            float lead = state->reaction_delay;
            look_target[0] += state->last_enemy_velocity[0] * lead;
            look_target[1] += state->last_enemy_velocity[1] * lead;
            look_target[2] += state->last_enemy_velocity[2] * lead;
        }
        EA_LookAtPoint(client, eye_position, look_target);

        bool cooled_down = (state->attack_cooldown <= 0.0f) || (now >= state->last_attack_time + state->attack_cooldown);
        bool reaction_ready = (state->enemysight_time > -FLT_MAX * 0.5f) &&
                              (now >= state->enemysight_time + state->reaction_delay);

        if (reaction_ready && cooled_down)
        {
            EA_Attack(client);
            state->last_attack_time = now;
            state->firethrottleshoot_time = now;
        }
        else
        {
            state->firethrottlewait_time = now;
        }

        float vertical = enemy->origin[2] - eye_position[2];
        if (state->allow_rocket_jump && vertical > AI_DM_MIN_ROCKET_VERTICAL)
        {
            if (now >= state->last_jump_time + state->attack_cooldown)
            {
                EA_Jump(client);
                state->last_jump_time = now;
                state->attackjump_time = now;
            }
        }

        state->attackcrouch_time = now;
    }
    else
    {
        if (state->enemy_visible)
        {
            state->enemy_visible = false;
            state->enemydeath_time = now;
        }

        if (state->chase_time > now && state->enemy_entity >= 0)
        {
            vec3_t predicted;
            VectorCopy(state->last_enemy_origin, predicted);
            if (state->enemyposition_time > -FLT_MAX * 0.5f)
            {
                float lead = now - state->enemyposition_time;
                predicted[0] += state->last_enemy_velocity[0] * lead;
                predicted[1] += state->last_enemy_velocity[1] * lead;
                predicted[2] += state->last_enemy_velocity[2] * lead;
            }

            vec3_t direction;
            VectorSubtract(predicted, eye_position, direction);
            float distance = AI_DMVectorNormalise(direction);

            if (distance > 0.0f)
            {
                ai_dm_enemy_info_t chase_enemy = {
                    .valid = true,
                    .visible = false,
                    .entity = state->enemy_entity,
                    .distance = distance,
                };
                float speed = AI_DMState_ClampSpeed(state, &chase_enemy);
                EA_Move(client, direction, speed);
                EA_LookAtPoint(client, eye_position, predicted);
            }
        }
        else
        {
            state->enemy_entity = -1;
            state->attackstrafe_timer = 0.0f;
            state->strafe_side = 1;
        }
    }

    bool path_failed = false;
    if (selection != NULL && selection->valid)
    {
        float selection_score = selection->score;
        float command_speed = (last_move_command != NULL) ? last_move_command->speed : selection_score;
        if (selection_score <= 0.0f || command_speed <= 0.0f)
        {
            path_failed = true;
        }
    }

    if (path_failed && enemy_valid && client_state->goal_state != NULL)
    {
        if (AI_DMState_ShouldRecordAvoid(state, enemy, now))
        {
            ai_avoid_list_t *avoid = AI_GoalState_GetAvoidList(client_state->goal_state);
            if (avoid != NULL)
            {
                float expiry = now + state->avoid_duration;
                if (AI_AvoidList_Add(avoid, enemy->entity, expiry))
                {
                    state->last_avoid_entity = enemy->entity;
                    state->last_avoid_time = now;
                }
            }
        }
    }
}

