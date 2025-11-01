#pragma once

#include <stdbool.h>
#include "q2bridge/botlib.h"
#include "shared/q_shared.h"
#include "move/bot_move.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AI_GOAL_MAX_CANDIDATES 32
#define AI_GOAL_AVOID_LIST_CAPACITY 32

struct bot_updateclient_s;
typedef struct ai_goal_state_s ai_goal_state_t;
typedef struct ai_move_state_s ai_move_state_t;

typedef struct ai_goal_candidate_s {
    int item_index;
    int area;
    int travel_flags;
    float base_weight;
    vec3_t origin;
} ai_goal_candidate_t;

typedef struct ai_goal_selection_s {
    bool valid;
    ai_goal_candidate_t candidate;
    float score;
    float travel_time;
} ai_goal_selection_t;

typedef struct ai_avoid_entry_s {
    int id;
    float expiry;
} ai_avoid_entry_t;

typedef struct ai_avoid_list_s {
    ai_avoid_entry_t entries[AI_GOAL_AVOID_LIST_CAPACITY];
    int count;
} ai_avoid_list_t;

typedef float (*ai_goal_weight_fn)(void *ctx, const ai_goal_candidate_t *candidate);
typedef float (*ai_goal_travel_time_fn)(void *ctx, int start_area, const ai_goal_candidate_t *candidate);
typedef void (*ai_goal_notify_fn)(void *ctx, const ai_goal_selection_t *selection);
typedef int (*ai_goal_area_fn)(void *ctx, const vec3_t origin);

typedef struct ai_goal_services_s {
    ai_goal_weight_fn weight_fn;
    ai_goal_travel_time_fn travel_time_fn;
    ai_goal_notify_fn notify_fn;
    ai_goal_area_fn area_fn;
    void *userdata;
    float avoid_duration;
} ai_goal_services_t;

typedef int (*ai_move_path_fn)(void *ctx,
                               const ai_goal_selection_t *goal,
                               ai_avoid_list_t *avoid,
                               bot_input_t *out_input);
typedef void (*ai_move_submit_fn)(void *ctx, int client, const bot_input_t *input);

typedef struct ai_move_services_s {
    ai_move_path_fn path_fn;
    ai_move_submit_fn submit_fn;
    void *userdata;
} ai_move_services_t;

struct ai_goal_state_s {
    ai_goal_services_t services;
    ai_goal_candidate_t candidates[AI_GOAL_MAX_CANDIDATES];
    int candidate_count;
    ai_goal_selection_t active_goal;
    ai_avoid_list_t avoid_goals;
    int current_area;
};

struct ai_move_state_s {
    ai_move_services_t services;
    ai_avoid_list_t *shared_avoid;
    ai_goal_selection_t last_goal;
    bot_input_t last_input;
    bool has_last_input;
    bot_moveresult_t last_result;
    bool has_last_result;
};

ai_goal_state_t *AI_GoalState_Create(void);
void AI_GoalState_Destroy(ai_goal_state_t *state);
void AI_GoalState_SetServices(ai_goal_state_t *state, const ai_goal_services_t *services);
void AI_GoalState_ClearCandidates(ai_goal_state_t *state);
bool AI_GoalState_AddCandidate(ai_goal_state_t *state, const ai_goal_candidate_t *candidate);
int AI_GoalState_RecordClientUpdate(ai_goal_state_t *state, const struct bot_updateclient_s *update);
int AI_GoalOrchestrator_Refresh(ai_goal_state_t *state, float now, ai_goal_selection_t *selection);
void AI_GoalState_SetCurrentArea(ai_goal_state_t *state, int area);
int AI_GoalState_GetCurrentArea(const ai_goal_state_t *state);
const ai_goal_selection_t *AI_GoalState_GetActiveSelection(const ai_goal_state_t *state);
ai_avoid_list_t *AI_GoalState_GetAvoidList(ai_goal_state_t *state);

ai_move_state_t *AI_MoveState_Create(void);
void AI_MoveState_Destroy(ai_move_state_t *state);
void AI_MoveState_SetServices(ai_move_state_t *state, const ai_move_services_t *services);
void AI_MoveState_LinkAvoidList(ai_move_state_t *state, ai_avoid_list_t *avoid);
int AI_MoveOrchestrator_Dispatch(ai_move_state_t *state,
                                 const ai_goal_selection_t *selection,
                                 bot_input_t *out_input);
int AI_MoveOrchestrator_Submit(ai_move_state_t *state, int client, const bot_input_t *input);
ai_avoid_list_t *AI_MoveState_GetAvoidList(ai_move_state_t *state);

void AI_AvoidList_Prune(ai_avoid_list_t *list, float now);
bool AI_AvoidList_Contains(const ai_avoid_list_t *list, int id, float now);
bool AI_AvoidList_Add(ai_avoid_list_t *list, int id, float expiry);

#ifdef __cplusplus
}
#endif

