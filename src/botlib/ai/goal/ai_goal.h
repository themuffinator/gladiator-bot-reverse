#pragma once

#include "bot_goal.h"
#include "../goal_move_orchestrator.h"

#ifdef __cplusplus
extern "C" {
#endif

int AI_GoalBotlib_AllocState(int client);
void AI_GoalBotlib_FreeState(int handle);
void AI_GoalBotlib_ResetState(int handle);
int AI_GoalBotlib_LoadItemWeights(int handle, const char *filename);
void AI_GoalBotlib_FreeItemWeights(int handle);
void AI_GoalBotlib_SetTime(float now);
void AI_GoalBotlib_SynchroniseAvoid(int handle, const ai_goal_state_t *state, float now);
int AI_GoalBotlib_Update(int handle,
                         vec3_t origin,
                         int *inventory,
                         int travelflags,
                         float now,
                         float nearby_time);
int AI_GoalBotlib_GetTopGoal(int handle, bot_goal_t *goal);
int AI_GoalBotlib_GetSecondGoal(int handle, bot_goal_t *goal);
int AI_GoalBotlib_PushGoal(int handle, const bot_goal_t *goal);
int AI_GoalBotlib_PopGoal(int handle);
void AI_GoalBotlib_ResetAvoidGoals(int handle);
void AI_GoalBotlib_AddAvoidGoal(int handle, int number, float avoidtime);
int AI_GoalBotlib_RegisterLevelItem(const bot_levelitem_setup_t *setup);
void AI_GoalBotlib_UnregisterLevelItem(int number);
void AI_GoalBotlib_MarkItemTaken(int number, float respawn_delay);
int AI_GoalBotlib_ChooseLTG(int handle, vec3_t origin, int *inventory, int travelflags);
int AI_GoalBotlib_ChooseNBG(int handle, vec3_t origin, int *inventory, int travelflags, bot_goal_t *ltg, float maxtime);
const bot_goalstate_t *AI_GoalBotlib_DebugPeek(int handle);

#ifdef __cplusplus
}
#endif

