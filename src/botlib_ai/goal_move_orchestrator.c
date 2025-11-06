#include "goal_move_orchestrator.h"

#include <float.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "botlib_aas/aas_local.h"
#include "botlib_aas/aas_sound.h"
#include "botlib_ai_move/bot_move.h"
#include "botlib_ea/ea_local.h"
#include "botlib_interface/botlib_interface.h"

#define AI_GOAL_SOUND_TAG 0x01000000u
#define AI_GOAL_LIGHT_TAG 0x02000000u
#define AI_GOAL_SOUND_MAX_AGE 1.0f
#define AI_GOAL_SOUND_LIMIT 16
#define AI_GOAL_POINTLIGHT_LIMIT 8
#define AI_GOAL_POINTLIGHT_RADIUS_THRESHOLD 64.0f

enum
{
    AI_SOUND_TYPE_IGNORE = 0,
    AI_SOUND_TYPE_PLAYER = 1,
    AI_SOUND_TYPE_PLAYERSTEPS = 2,
    AI_SOUND_TYPE_PLAYERJUMP = 3,
    AI_SOUND_TYPE_PLAYERWATERIN = 4,
    AI_SOUND_TYPE_PLAYERWATEROUT = 5,
    AI_SOUND_TYPE_PLAYERFALL = 6,
    AI_SOUND_TYPE_FIRINGWEAPON = 7,
};

static void ai_goal_copy_vec(vec3_t dest, const vec3_t src)
{
    if (dest == NULL || src == NULL)
    {
        return;
    }

    dest[0] = src[0];
    dest[1] = src[1];
    dest[2] = src[2];
}

static int ai_goal_point_area_num(const vec3_t origin)
{
    if (origin == NULL || !aasworld.loaded || aasworld.areas == NULL || aasworld.numAreas <= 0)
    {
        return 0;
    }

    for (int areanum = 1; areanum <= aasworld.numAreas; ++areanum)
    {
        const aas_area_t *area = &aasworld.areas[areanum];
        if (origin[0] < area->mins[0] || origin[0] > area->maxs[0])
        {
            continue;
        }
        if (origin[1] < area->mins[1] || origin[1] > area->maxs[1])
        {
            continue;
        }
        if (origin[2] < area->mins[2] || origin[2] > area->maxs[2])
        {
            continue;
        }
        return areanum;
    }

    return 0;
}

static int ai_goal_state_alloc_temp_index(ai_goal_state_t *state, unsigned int tag)
{
    if (state == NULL)
    {
        return -1;
    }

    state->temp_goal_serial = (state->temp_goal_serial + 1U) & 0x00FFFFFFu;
    if (state->temp_goal_serial == 0U)
    {
        state->temp_goal_serial = 1U;
    }

    unsigned int value = tag | state->temp_goal_serial;
    if (value == 0U)
    {
        value = tag | 1U;
    }

    return -(int)value;
}

static bool ai_goal_sound_is_high_priority(const aas_soundinfo_t *info, const aas_sound_event_t *event)
{
    if (info == NULL)
    {
        return false;
    }

    if (info->type <= AI_SOUND_TYPE_IGNORE)
    {
        return false;
    }

    if (info->type <= AI_SOUND_TYPE_FIRINGWEAPON)
    {
        return true;
    }

    if (info->recognition > 0.0f)
    {
        return true;
    }

    if (event != NULL && event->volume >= 0.75f)
    {
        return true;
    }

    return false;
}

static float ai_goal_sound_weight(const aas_soundinfo_t *info,
                                  const aas_sound_event_t *event,
                                  float age)
{
    float base = 250.0f;

    float info_volume = (info != NULL && info->volume > 0.0f) ? info->volume : 75.0f;
    float event_volume = (event != NULL && event->volume > 0.0f) ? event->volume : 1.0f;
    base += info_volume * event_volume;

    if (info != NULL)
    {
        switch (info->type)
        {
            case AI_SOUND_TYPE_FIRINGWEAPON:
                base += 300.0f;
                break;
            case AI_SOUND_TYPE_PLAYER:
            case AI_SOUND_TYPE_PLAYERSTEPS:
            case AI_SOUND_TYPE_PLAYERJUMP:
            case AI_SOUND_TYPE_PLAYERWATERIN:
            case AI_SOUND_TYPE_PLAYERWATEROUT:
            case AI_SOUND_TYPE_PLAYERFALL:
                base += 200.0f;
                break;
            default:
                break;
        }

        if (info->recognition > 0.0f)
        {
            base += info->recognition * 100.0f;
        }
    }

    if (event != NULL)
    {
        float attenuation = (event->attenuation > 0.0f) ? event->attenuation : 1.0f;
        if (attenuation < 1.0f)
        {
            base += (1.0f - attenuation) * 75.0f;
        }
    }

    if (age > 0.0f)
    {
        base -= age * 300.0f;
    }

    if (base < 1.0f)
    {
        base = 1.0f;
    }

    return base;
}

static bool ai_goal_make_sound_candidate(ai_goal_state_t *state,
                                         float now,
                                         const aas_sound_event_t *event,
                                         ai_goal_candidate_t *out_candidate)
{
    if (state == NULL || event == NULL || out_candidate == NULL)
    {
        return false;
    }

    const aas_soundinfo_t *info = NULL;
    if (event->soundindex >= 0)
    {
        info = AAS_SoundSubsystem_InfoForSoundIndex(event->soundindex);
    }

    if (!ai_goal_sound_is_high_priority(info, event))
    {
        return false;
    }

    float age = now - event->timestamp;
    if (age > AI_GOAL_SOUND_MAX_AGE)
    {
        return false;
    }

    if (age < 0.0f)
    {
        age = 0.0f;
    }

    ai_goal_candidate_t candidate;
    memset(&candidate, 0, sizeof(candidate));

    candidate.item_index = ai_goal_state_alloc_temp_index(state, AI_GOAL_SOUND_TAG);
    candidate.travel_flags = TFL_DEFAULT;
    ai_goal_copy_vec(candidate.origin, event->origin);
    candidate.area = ai_goal_point_area_num(candidate.origin);
    if (candidate.area <= 0)
    {
        return false;
    }

    candidate.base_weight = ai_goal_sound_weight(info, event, age);
    if (!isfinite(candidate.base_weight) || candidate.base_weight <= 0.0f)
    {
        return false;
    }

    *out_candidate = candidate;
    return true;
}

static float ai_goal_pointlight_weight(const aas_pointlight_event_t *event)
{
    if (event == NULL)
    {
        return 0.0f;
    }

    float radius = (event->radius > 0.0f) ? event->radius : 0.0f;
    float color_intensity = 0.0f;
    for (int i = 0; i < 3; ++i)
    {
        if (event->color[i] > 0.0f)
        {
            color_intensity += event->color[i];
        }
    }

    float base = 200.0f + radius;
    base += color_intensity * 80.0f;
    if (event->decay > 0.0f)
    {
        base += 50.0f / event->decay;
    }

    return base;
}

static bool ai_goal_make_pointlight_candidate(ai_goal_state_t *state,
                                              const aas_pointlight_event_t *event,
                                              ai_goal_candidate_t *out_candidate)
{
    if (state == NULL || event == NULL || out_candidate == NULL)
    {
        return false;
    }

    if (event->radius < AI_GOAL_POINTLIGHT_RADIUS_THRESHOLD)
    {
        return false;
    }

    ai_goal_candidate_t candidate;
    memset(&candidate, 0, sizeof(candidate));

    candidate.item_index = ai_goal_state_alloc_temp_index(state, AI_GOAL_LIGHT_TAG);
    candidate.travel_flags = TFL_DEFAULT;
    ai_goal_copy_vec(candidate.origin, event->origin);
    candidate.area = ai_goal_point_area_num(candidate.origin);
    if (candidate.area <= 0)
    {
        return false;
    }

    candidate.base_weight = ai_goal_pointlight_weight(event);
    if (!isfinite(candidate.base_weight) || candidate.base_weight <= 0.0f)
    {
        return false;
    }

    *out_candidate = candidate;
    return true;
}

static void ai_goal_consider_candidate(ai_goal_state_t *state,
                                       float now,
                                       const ai_goal_candidate_t *candidate,
                                       ai_goal_selection_t *best)
{
    if (state == NULL || candidate == NULL || best == NULL)
    {
        return;
    }

    if (AI_AvoidList_Contains(&state->avoid_goals, candidate->item_index, now))
    {
        return;
    }

    float weight = candidate->base_weight;
    if (state->services.weight_fn != NULL)
    {
        weight = state->services.weight_fn(state->services.userdata, candidate);
    }

    if (!isfinite(weight) || weight <= -FLT_MAX)
    {
        return;
    }

    float travel_time = 0.0f;
    if (state->services.travel_time_fn != NULL && state->current_area > 0 && candidate->area > 0)
    {
        travel_time = state->services.travel_time_fn(state->services.userdata,
                                                     state->current_area,
                                                     candidate);
        if (!isfinite(travel_time))
        {
            travel_time = 0.0f;
        }
        if (travel_time < 0.0f)
        {
            AI_AvoidList_Add(&state->avoid_goals,
                             candidate->item_index,
                             now + state->services.avoid_duration);
            return;
        }
    }

    float score = weight - travel_time;
    if (!best->valid || score > best->score)
    {
        best->valid = true;
        best->candidate = *candidate;
        best->score = score;
        best->travel_time = travel_time;
    }
}

static void ai_goal_consider_sound_events(ai_goal_state_t *state,
                                          float now,
                                          ai_goal_selection_t *best)
{
    if (state == NULL || best == NULL)
    {
        return;
    }

    size_t total = AAS_SoundSubsystem_SoundEventCount();
    int considered = 0;
    for (size_t index = 0; index < total; ++index)
    {
        if (considered >= AI_GOAL_SOUND_LIMIT)
        {
            break;
        }

        const aas_sound_event_t *event = AAS_SoundSubsystem_SoundEvent(index);
        if (event == NULL)
        {
            continue;
        }

        ai_goal_candidate_t candidate;
        if (!ai_goal_make_sound_candidate(state, now, event, &candidate))
        {
            continue;
        }

        ai_goal_consider_candidate(state, now, &candidate, best);
        ++considered;
    }
}

static void ai_goal_consider_pointlight_events(ai_goal_state_t *state,
                                               float now,
                                               ai_goal_selection_t *best)
{
    if (state == NULL || best == NULL)
    {
        return;
    }

    size_t total = AAS_SoundSubsystem_PointLightCount();
    int considered = 0;
    for (size_t index = 0; index < total; ++index)
    {
        if (considered >= AI_GOAL_POINTLIGHT_LIMIT)
        {
            break;
        }

        const aas_pointlight_event_t *event = AAS_SoundSubsystem_PointLight(index);
        if (event == NULL)
        {
            continue;
        }

        ai_goal_candidate_t candidate;
        if (!ai_goal_make_pointlight_candidate(state, event, &candidate))
        {
            continue;
        }

        ai_goal_consider_candidate(state, now, &candidate, best);
        ++considered;
    }
}

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
    state->temp_goal_serial = 0U;
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
    memset(&state->last_result, 0, sizeof(state->last_result));
    state->has_last_result = false;
}

static void ai_move_apply_result(bot_input_t *input, const bot_moveresult_t *result)
{
    if (input == NULL || result == NULL) {
        return;
    }

    if (result->flags & MOVERESULT_MOVEMENTWEAPON) {
        input->actionflags |= ACTION_ATTACK;
    }

    if (result->traveltype == TRAVEL_JUMP || result->traveltype == TRAVEL_ROCKETJUMP ||
        result->traveltype == TRAVEL_BFGJUMP || result->traveltype == TRAVEL_WATERJUMP) {
        input->actionflags |= ACTION_JUMP;
    }

    if (result->traveltype == TRAVEL_CROUCH) {
        input->actionflags |= ACTION_CROUCH;
    }

    if (result->flags & MOVERESULT_WAITING) {
        input->speed = 0.0f;
    }
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
        ai_goal_consider_candidate(state, now, &state->candidates[i], &best);
    }

    ai_goal_consider_sound_events(state, now, &best);
    ai_goal_consider_pointlight_events(state, now, &best);

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
    state->has_last_result = false;

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
        VectorCopy(selection->candidate.origin, out_input->dir);
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

    bot_input_t enriched = *command;
    if (state->has_last_result) {
        ai_move_apply_result(&enriched, &state->last_result);
    }

    state->last_input = enriched;
    state->has_last_input = true;

    if (state->services.submit_fn != NULL) {
        state->services.submit_fn(state->services.userdata, client, &enriched);
        return BLERR_NOERROR;
    }

    int status = EA_SubmitInput(client, &enriched);
    return status;
}

