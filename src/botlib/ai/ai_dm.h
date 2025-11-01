#pragma once

#include <stdbool.h>

#include "shared/q_shared.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ai_dm_state_s ai_dm_state_t;

typedef struct ai_dm_enemy_info_s {
    bool valid;
    bool visible;
    int entity;
    vec3_t origin;
    vec3_t velocity;
    float distance;
    float last_seen_time;
} ai_dm_enemy_info_t;

struct bot_client_state_s;
typedef struct bot_client_state_s bot_client_state_t;

typedef struct ai_goal_selection_s ai_goal_selection_t;

typedef struct bot_input_s bot_input_t;

typedef struct ai_dm_metrics_s {
    int client_number;
    int enemy_entity;
    int revenge_enemy;
    int revenge_kills;
    bool enemy_visible;
    float last_attack_time;
    float last_jump_time;
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
    float reaction_delay;
    float chase_duration;
    float attack_strafe_interval;
    vec3_t last_enemy_origin;
    vec3_t last_enemy_velocity;
} ai_dm_metrics_t;

ai_dm_state_t *AI_DMState_Create(int client_number);
void AI_DMState_Destroy(ai_dm_state_t *state);
void AI_DMState_Reset(ai_dm_state_t *state);
void AI_DMState_SetClient(ai_dm_state_t *state, int client_number);
void AI_DMState_GetMetrics(const ai_dm_state_t *state, ai_dm_metrics_t *out_metrics);
void AI_DMState_Update(ai_dm_state_t *state,
                       const bot_client_state_t *client_state,
                       const ai_goal_selection_t *selection,
                       const ai_dm_enemy_info_t *enemy,
                       const bot_input_t *last_move_command,
                       float now);

#ifdef __cplusplus
}
#endif

