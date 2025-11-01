#include "ai_dm.h"

#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "botlib/aas/aas_sound.h"
#include "botlib/ai/goal_move_orchestrator.h"
#include "botlib/common/l_libvar.h"
#include "botlib/ea/ea_local.h"
#include "botlib/interface/bot_state.h"
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
    vec3_t sound_alert_origin;
    float sound_alert_expiry;
    int sound_alert_type;
};

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
    VectorClear(state->sound_alert_origin);
    state->sound_alert_expiry = -FLT_MAX;
    state->sound_alert_type = AAS_SOUNDTYPE_IGNORE;
    state->config_initialised = false;
    AI_DMState_RefreshConfig(state);
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

static bool AI_DMState_IsCombatSound(int sound_type)
{
    switch (sound_type)
    {
        case AAS_SOUNDTYPE_FIRINGWEAPON:
        case AAS_SOUNDTYPE_PLAYER:
        case AAS_SOUNDTYPE_PLAYERSTEPS:
        case AAS_SOUNDTYPE_PLAYERJUMP:
        case AAS_SOUNDTYPE_PLAYERWATERIN:
        case AAS_SOUNDTYPE_PLAYERWATEROUT:
        case AAS_SOUNDTYPE_PLAYERFALL:
            return true;
        default:
            return false;
    }
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

    AI_DMState_RefreshConfig(state);

    int client = client_state->client_number;
    if (client < 0)
    {
        return;
    }

    int desired_weapon = client_state->current_weapon;
    if (desired_weapon > 0 && desired_weapon != state->last_selected_weapon)
    {
        EA_SelectWeapon(client, desired_weapon);
        state->last_selected_weapon = desired_weapon;
    }

    if (now >= state->sound_alert_expiry)
    {
        state->sound_alert_type = AAS_SOUNDTYPE_IGNORE;
    }

    aas_sound_event_summary_t sound_summaries[8];
    size_t sound_count =
        AAS_SoundSubsystem_QuerySoundSummaries(now, sound_summaries, sizeof(sound_summaries) / sizeof(sound_summaries[0]));
    bool alerted_this_frame = false;
    for (size_t i = 0; i < sound_count; ++i)
    {
        const aas_sound_event_summary_t *summary = &sound_summaries[i];
        if (!AI_DMState_IsCombatSound(summary->type))
        {
            continue;
        }

        VectorCopy(summary->origin, state->sound_alert_origin);
        state->sound_alert_expiry = summary->expiry;
        state->sound_alert_type = summary->type;
        alerted_this_frame = true;

        aas_pointlight_event_summary_t light_summaries[4];
        size_t light_count = AAS_SoundSubsystem_QueryPointLightSummaries(
            now, light_summaries, sizeof(light_summaries) / sizeof(light_summaries[0]));
        for (size_t light_index = 0; light_index < light_count; ++light_index)
        {
            const aas_pointlight_event_summary_t *light = &light_summaries[light_index];
            if (light->ent == summary->ent && light->expiry >= now)
            {
                VectorCopy(light->origin, state->sound_alert_origin);
                break;
            }
        }
        break;
    }

    if (alerted_this_frame && state->attack_cooldown > 0.0f)
    {
        state->last_attack_time = now - state->attack_cooldown;
    }

    bool enemy_valid = (enemy != NULL) && enemy->valid;
    vec3_t eye_position = {0.0f, 0.0f, 0.0f};
    VectorCopy(client_state->last_client_update.origin, eye_position);

    if (enemy_valid)
    {
        vec3_t direction;
        VectorSubtract(enemy->origin, eye_position, direction);
        float speed = AI_DMState_ClampSpeed(state, enemy);
        EA_Move(client, direction, speed);
        EA_LookAtPoint(client, eye_position, enemy->origin);

        if (state->attack_cooldown <= 0.0f || now >= state->last_attack_time + state->attack_cooldown)
        {
            EA_Attack(client);
            state->last_attack_time = now;
        }

        float vertical = enemy->origin[2] - eye_position[2];
        if (state->allow_rocket_jump && vertical > AI_DM_MIN_ROCKET_VERTICAL)
        {
            if (now >= state->last_jump_time + state->attack_cooldown)
            {
                EA_Jump(client);
                state->last_jump_time = now;
            }
        }
    }
    else if (state->sound_alert_type != AAS_SOUNDTYPE_IGNORE && state->sound_alert_expiry > now)
    {
        EA_LookAtPoint(client, eye_position, state->sound_alert_origin);
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

