#pragma once

#include <stdbool.h>

#include "shared/q_shared.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ai_dm_state_s ai_dm_state_t;

typedef struct ai_dm_enemy_info_s {
    bool valid;
    int entity;
    vec3_t origin;
    vec3_t velocity;
    float distance;
    bool visible;
    bool recently_damaged;
    bool enemy_invisible;
    bool enemy_shooting;
    bool enemy_chatting;
    float fov_used;
    float sight_time;
    float last_visible_time;
} ai_dm_enemy_info_t;

struct bot_client_state_s;
typedef struct bot_client_state_s bot_client_state_t;

typedef struct ai_goal_selection_s ai_goal_selection_t;

typedef struct bot_input_s bot_input_t;

ai_dm_state_t *AI_DMState_Create(int client_number);
void AI_DMState_Destroy(ai_dm_state_t *state);
void AI_DMState_Reset(ai_dm_state_t *state);
void AI_DMState_Update(ai_dm_state_t *state,
                       const bot_client_state_t *client_state,
                       const ai_goal_selection_t *selection,
                       const ai_dm_enemy_info_t *enemy,
                       const bot_input_t *last_move_command,
                       float now);

#ifdef __cplusplus
}
#endif

