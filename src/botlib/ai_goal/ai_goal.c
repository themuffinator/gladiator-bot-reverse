#include "ai_goal.h"

#include <stddef.h>

#include "bot_goal.h"
#include "botlib/ai/goal_move_orchestrator.h"
#include "botlib/common/l_log.h"

static int AI_GoalBotlib_ValidateHandle(int handle)
{
    if (handle <= 0 || handle > MAX_CLIENTS)
    {
        BotLib_Print(PRT_ERROR, "AI_GoalBotlib: invalid goal handle %d\n", handle);
        return 0;
    }
    if (BotGoalStatePeek(handle) == NULL)
    {
        BotLib_Print(PRT_ERROR, "AI_GoalBotlib: goal handle %d not allocated\n", handle);
        return 0;
    }
    return 1;
}

int AI_GoalBotlib_AllocState(int client)
{
    return BotAllocGoalState(client);
}

void AI_GoalBotlib_FreeState(int handle)
{
    BotFreeGoalState(handle);
}

void AI_GoalBotlib_ResetState(int handle)
{
    if (!AI_GoalBotlib_ValidateHandle(handle))
    {
        return;
    }
    BotResetGoalState(handle);
}

int AI_GoalBotlib_LoadItemWeights(int handle, const char *filename)
{
    if (!AI_GoalBotlib_ValidateHandle(handle))
    {
        return 0;
    }
    return BotLoadItemWeights(handle, filename);
}

void AI_GoalBotlib_FreeItemWeights(int handle)
{
    if (!AI_GoalBotlib_ValidateHandle(handle))
    {
        return;
    }
    BotFreeItemWeights(handle);
}

void AI_GoalBotlib_SetTime(float now)
{
    BotGoal_SetCurrentTime(now);
}

static void AI_GoalBotlib_UpdateAvoidGoals(int handle, const ai_avoid_list_t *list, float now)
{
    BotResetAvoidGoals(handle);
    if (list == NULL)
    {
        return;
    }

    for (int i = 0; i < list->count; ++i)
    {
        float remaining = list->entries[i].expiry - now;
        if (remaining <= 0.0f)
        {
            continue;
        }
        BotAddToAvoidGoals(handle, list->entries[i].id, remaining);
    }
}

void AI_GoalBotlib_SynchroniseAvoid(int handle, const ai_goal_state_t *state, float now)
{
    if (!AI_GoalBotlib_ValidateHandle(handle) || state == NULL)
    {
        return;
    }

    AI_GoalBotlib_SetTime(now);
    const ai_avoid_list_t *avoid = AI_GoalState_GetAvoidList((ai_goal_state_t *)state);
    AI_GoalBotlib_UpdateAvoidGoals(handle, avoid, now);
}

int AI_GoalBotlib_PushGoal(int handle, const bot_goal_t *goal)
{
    if (!AI_GoalBotlib_ValidateHandle(handle) || goal == NULL)
    {
        return 0;
    }
    return BotPushGoal(handle, goal);
}

int AI_GoalBotlib_PopGoal(int handle)
{
    if (!AI_GoalBotlib_ValidateHandle(handle))
    {
        return 0;
    }
    return BotPopGoal(handle);
}

int AI_GoalBotlib_GetTopGoal(int handle, bot_goal_t *goal)
{
    if (!AI_GoalBotlib_ValidateHandle(handle))
    {
        return 0;
    }
    return BotGetTopGoal(handle, goal);
}

int AI_GoalBotlib_GetSecondGoal(int handle, bot_goal_t *goal)
{
    if (!AI_GoalBotlib_ValidateHandle(handle))
    {
        return 0;
    }
    return BotGetSecondGoal(handle, goal);
}

int AI_GoalBotlib_Update(int handle,
                         vec3_t origin,
                         int *inventory,
                         int travelflags,
                         float now,
                         float nearby_time)
{
    if (!AI_GoalBotlib_ValidateHandle(handle))
    {
        return BLERR_INVALIDIMPORT;
    }

    AI_GoalBotlib_SetTime(now);

    bot_goal_t top_goal;
    if (BotGetTopGoal(handle, &top_goal))
    {
        if (BotTouchingGoal(origin, &top_goal))
        {
            BotPopGoal(handle);
        }
    }

    if (!BotGetTopGoal(handle, &top_goal))
    {
        if (!BotChooseLTGItem(handle, origin, inventory, travelflags))
        {
            return BLERR_INVALIDIMPORT;
        }
        BotGetTopGoal(handle, &top_goal);
    }

    if (nearby_time > 0.0f)
    {
        BotChooseNBGItem(handle, origin, inventory, travelflags, &top_goal, nearby_time);
    }

    return BLERR_NOERROR;
}

void AI_GoalBotlib_ResetAvoidGoals(int handle)
{
    if (!AI_GoalBotlib_ValidateHandle(handle))
    {
        return;
    }
    BotResetAvoidGoals(handle);
}

void AI_GoalBotlib_AddAvoidGoal(int handle, int number, float avoidtime)
{
    if (!AI_GoalBotlib_ValidateHandle(handle))
    {
        return;
    }
    BotAddToAvoidGoals(handle, number, avoidtime);
}

int AI_GoalBotlib_RegisterLevelItem(const bot_levelitem_setup_t *setup)
{
    return BotGoal_RegisterLevelItem(setup);
}

void AI_GoalBotlib_UnregisterLevelItem(int number)
{
    BotGoal_UnregisterLevelItem(number);
}

void AI_GoalBotlib_MarkItemTaken(int number, float respawn_delay)
{
    BotGoal_MarkItemTaken(number, respawn_delay);
}

int AI_GoalBotlib_ChooseLTG(int handle, vec3_t origin, int *inventory, int travelflags)
{
    if (!AI_GoalBotlib_ValidateHandle(handle))
    {
        return 0;
    }
    return BotChooseLTGItem(handle, origin, inventory, travelflags);
}

int AI_GoalBotlib_ChooseNBG(int handle, vec3_t origin, int *inventory, int travelflags, bot_goal_t *ltg, float maxtime)
{
    if (!AI_GoalBotlib_ValidateHandle(handle))
    {
        return 0;
    }
    return BotChooseNBGItem(handle, origin, inventory, travelflags, ltg, maxtime);
}

const bot_goalstate_t *AI_GoalBotlib_DebugPeek(int handle)
{
    return BotGoalStatePeek(handle);
}
