#include "goal_move_orchestrator.h"

#include <float.h>
#include <stdbool.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "botlib/ea/ea_local.h"
#include "botlib/interface/botlib_interface.h"

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
};

static void ai_goal_state_reset(ai_goal_state_t *state)
{
    if (state == NULL) {
        return;
    }

    state->candidate_count = 0;
    state->active_goal.valid = false;
    state->active_goal.score = 0.0f;
    state->active_goal.travel_time = 0.0f;
    state->current_area = 0;
    memset(state->candidates, 0, sizeof(state->candidates));
    memset(&state->avoid_goals, 0, sizeof(state->avoid_goals));
}

static void ai_move_state_reset(ai_move_state_t *state)
{
    if (state == NULL) {
        return;
    }

    state->shared_avoid = NULL;
    state->has_last_input = false;
    memset(&state->services, 0, sizeof(state->services));
    memset(&state->last_goal, 0, sizeof(state->last_goal));
    memset(&state->last_input, 0, sizeof(state->last_input));
}

static float ai_goal_default_weight(void *ctx, const ai_goal_candidate_t *candidate)
{
    (void)ctx;
    if (candidate == NULL) {
        return 0.0f;
    }
    return candidate->base_weight;
}

static float ai_goal_default_travel(void *ctx, int start_area, const ai_goal_candidate_t *candidate)
{
    (void)ctx;
    (void)start_area;
    (void)candidate;
    return 0.0f;
}

static void ai_move_default_submit(void *ctx, int client, const bot_input_t *input)
{
    (void)ctx;
    if (input == NULL) {
        return;
    }

    EA_SubmitInput(client, input);
}

ai_goal_state_t *AI_GoalState_Create(void)
{
    ai_goal_state_t *state = (ai_goal_state_t *)calloc(1, sizeof(*state));
    if (state == NULL) {
        return NULL;
    }

    state->services.weight_fn = ai_goal_default_weight;
    state->services.travel_time_fn = ai_goal_default_travel;
    state->services.notify_fn = NULL;
    state->services.area_fn = NULL;
    state->services.userdata = NULL;
    state->services.avoid_duration = 5.0f;
    ai_goal_state_reset(state);
    return state;
}

void AI_GoalState_Destroy(ai_goal_state_t *state)
{
    if (state == NULL) {
        return;
    }

    free(state);
}

void AI_GoalState_SetServices(ai_goal_state_t *state, const ai_goal_services_t *services)
{
    if (state == NULL) {
        return;
    }

    if (services != NULL) {
        state->services = *services;
        if (state->services.weight_fn == NULL) {
            state->services.weight_fn = ai_goal_default_weight;
        }
        if (state->services.travel_time_fn == NULL) {
            state->services.travel_time_fn = ai_goal_default_travel;
        }
        if (state->services.avoid_duration <= 0.0f) {
            state->services.avoid_duration = 5.0f;
        }
    } else {
        state->services.weight_fn = ai_goal_default_weight;
        state->services.travel_time_fn = ai_goal_default_travel;
        state->services.notify_fn = NULL;
        state->services.area_fn = NULL;
        state->services.userdata = NULL;
        state->services.avoid_duration = 5.0f;
    }
}

void AI_GoalState_ClearCandidates(ai_goal_state_t *state)
{
    if (state == NULL) {
        return;
    }

    state->candidate_count = 0;
    memset(state->candidates, 0, sizeof(state->candidates));
}

bool AI_GoalState_AddCandidate(ai_goal_state_t *state, const ai_goal_candidate_t *candidate)
{
    if (state == NULL || candidate == NULL) {
        return false;
    }

    if (state->candidate_count >= AI_GOAL_MAX_CANDIDATES) {
        return false;
    }

    state->candidates[state->candidate_count++] = *candidate;
    return true;
}

void AI_GoalState_SetCurrentArea(ai_goal_state_t *state, int area)
{
    if (state == NULL) {
        return;
    }

    state->current_area = area;
}

int AI_GoalState_GetCurrentArea(const ai_goal_state_t *state)
{
    if (state == NULL) {
        return 0;
    }

    return state->current_area;
}

const ai_goal_selection_t *AI_GoalState_GetActiveSelection(const ai_goal_state_t *state)
{
    if (state == NULL) {
        return NULL;
    }

    return &state->active_goal;
}

ai_avoid_list_t *AI_GoalState_GetAvoidList(ai_goal_state_t *state)
{
    if (state == NULL) {
        return NULL;
    }

    return &state->avoid_goals;
}

static void ai_avoid_list_remove_index(ai_avoid_list_t *list, int index)
{
    if (list == NULL || index < 0 || index >= list->count) {
        return;
    }

    for (int i = index; i < list->count - 1; ++i) {
        list->entries[i] = list->entries[i + 1];
    }

    if (list->count > 0) {
        --list->count;
    }
}

void AI_AvoidList_Prune(ai_avoid_list_t *list, float now)
{
    if (list == NULL) {
        return;
    }

    for (int i = 0; i < list->count; ++i) {
        if (list->entries[i].expiry <= now) {
            ai_avoid_list_remove_index(list, i);
            --i;
        }
    }
}

bool AI_AvoidList_Contains(const ai_avoid_list_t *list, int id, float now)
{
    if (list == NULL) {
        return false;
    }

    for (int i = 0; i < list->count; ++i) {
        if (list->entries[i].id == id) {
            if (list->entries[i].expiry > now) {
                return true;
            }
            break;
        }
    }

    return false;
}

bool AI_AvoidList_Add(ai_avoid_list_t *list, int id, float expiry)
{
    if (list == NULL) {
        return false;
    }

    for (int i = 0; i < list->count; ++i) {
        if (list->entries[i].id == id) {
            list->entries[i].expiry = expiry;
            return true;
        }
    }

    if (list->count < AI_GOAL_AVOID_LIST_CAPACITY) {
        list->entries[list->count].id = id;
        list->entries[list->count].expiry = expiry;
        ++list->count;
        return true;
    }

    int oldest_index = 0;
    float oldest_expiry = list->entries[0].expiry;
    for (int i = 1; i < list->count; ++i) {
        if (list->entries[i].expiry < oldest_expiry) {
            oldest_index = i;
            oldest_expiry = list->entries[i].expiry;
        }
    }

    list->entries[oldest_index].id = id;
    list->entries[oldest_index].expiry = expiry;
    return true;
}

int AI_GoalState_RecordClientUpdate(ai_goal_state_t *state, const struct bot_updateclient_s *update)
{
    if (state == NULL || update == NULL) {
        return BLERR_INVALIDIMPORT;
    }

    if (state->services.area_fn != NULL) {
        int area = state->services.area_fn(state->services.userdata, update->origin);
        if (area < 0) {
            return BLERR_INVALIDIMPORT;
        }
        state->current_area = area;
    }

    return BLERR_NOERROR;
}

static bool ai_goal_selection_equals(const ai_goal_selection_t *lhs, const ai_goal_selection_t *rhs)
{
    if (lhs == NULL || rhs == NULL) {
        return false;
    }

    if (!lhs->valid && !rhs->valid) {
        return true;
    }

    if (lhs->valid != rhs->valid) {
        return false;
    }

    return lhs->candidate.item_index == rhs->candidate.item_index;
}

int AI_GoalOrchestrator_Refresh(ai_goal_state_t *state, float now, ai_goal_selection_t *selection)
{
    if (state == NULL) {
        return BLERR_INVALIDIMPORT;
    }

    AI_AvoidList_Prune(&state->avoid_goals, now);

    ai_goal_selection_t best = {0};
    best.valid = false;
    best.score = -FLT_MAX;

    for (int i = 0; i < state->candidate_count; ++i) {
        ai_goal_candidate_t *candidate = &state->candidates[i];
        if (AI_AvoidList_Contains(&state->avoid_goals, candidate->item_index, now)) {
            continue;
        }

        float weight = 0.0f;
        if (state->services.weight_fn != NULL) {
            weight = state->services.weight_fn(state->services.userdata, candidate);
        }

        float travel_time = 0.0f;
        if (state->services.travel_time_fn != NULL && state->current_area > 0 && candidate->area > 0) {
            travel_time = state->services.travel_time_fn(state->services.userdata, state->current_area, candidate);
            if (travel_time < 0.0f) {
                AI_AvoidList_Add(&state->avoid_goals,
                                 candidate->item_index,
                                 now + state->services.avoid_duration);
                continue;
            }
        }

        float score = weight - travel_time;
        if (!best.valid || score > best.score) {
            best.valid = true;
            best.candidate = *candidate;
            best.score = score;
            best.travel_time = travel_time;
        }
    }

    bool changed = !ai_goal_selection_equals(&state->active_goal, &best);
    state->active_goal = best;

    if (selection != NULL) {
        *selection = best;
    }

    if (changed && state->services.notify_fn != NULL) {
        state->services.notify_fn(state->services.userdata, &state->active_goal);
    }

    return BLERR_NOERROR;
}

ai_move_state_t *AI_MoveState_Create(void)
{
    ai_move_state_t *state = (ai_move_state_t *)calloc(1, sizeof(*state));
    if (state == NULL) {
        return NULL;
    }

    ai_move_state_reset(state);
    state->services.submit_fn = ai_move_default_submit;
    return state;
}

void AI_MoveState_Destroy(ai_move_state_t *state)
{
    if (state == NULL) {
        return;
    }

    free(state);
}

void AI_MoveState_SetServices(ai_move_state_t *state, const ai_move_services_t *services)
{
    if (state == NULL) {
        return;
    }

    if (services != NULL) {
        state->services = *services;
        if (state->services.submit_fn == NULL) {
            state->services.submit_fn = ai_move_default_submit;
        }
    } else {
        memset(&state->services, 0, sizeof(state->services));
        state->services.submit_fn = ai_move_default_submit;
    }
}

void AI_MoveState_LinkAvoidList(ai_move_state_t *state, ai_avoid_list_t *avoid)
{
    if (state == NULL) {
        return;
    }

    state->shared_avoid = avoid;
}

ai_avoid_list_t *AI_MoveState_GetAvoidList(ai_move_state_t *state)
{
    if (state == NULL) {
        return NULL;
    }

    return state->shared_avoid;
}

int AI_MoveOrchestrator_Dispatch(ai_move_state_t *state,
                                 const ai_goal_selection_t *selection,
                                 bot_input_t *out_input)
{
    if (state == NULL || out_input == NULL) {
        return BLERR_INVALIDIMPORT;
    }

    memset(out_input, 0, sizeof(*out_input));
    state->has_last_input = false;

    if (selection == NULL || !selection->valid) {
        state->last_goal = (ai_goal_selection_t){0};
        state->last_goal.valid = false;
        return BLERR_NOERROR;
    }

    state->last_goal = *selection;

    if (state->services.path_fn != NULL) {
        int status = state->services.path_fn(state->services.userdata,
                                             selection,
                                             state->shared_avoid,
                                             out_input);
        if (status != BLERR_NOERROR) {
            return status;
        }
    } else {
        VectorSubtract(selection->candidate.origin, vec3_origin, out_input->dir);
        out_input->speed = selection->score;
    }

    state->last_input = *out_input;
    state->has_last_input = true;
    return BLERR_NOERROR;
}

int AI_MoveOrchestrator_Submit(ai_move_state_t *state, int client, const bot_input_t *input)
{
    if (state == NULL) {
        return BLERR_INVALIDIMPORT;
    }

    const bot_input_t *command = input;
    if (command == NULL) {
        if (!state->has_last_input) {
            return BLERR_INVALIDIMPORT;
        }
        command = &state->last_input;
    }

    if (state->services.submit_fn != NULL) {
        state->services.submit_fn(state->services.userdata, client, command);
        return BLERR_NOERROR;
    }

    int status = EA_SubmitInput(client, command);
    return status;
}

