#pragma once

#include <stdbool.h>

#include "../../precomp/l_precomp.h"
#include "../../common/l_log.h"
#include "../../common/l_memory.h"
#include "../weight/bot_weight.h"
#include "../../shared/q_shared.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BOT_GOAL_MAX_AVOID       256
#define BOT_GOAL_MAX_STACK       8
#define BOT_GOAL_MAX_AVOIDREACH  32

#define GFL_NONE    0
#define GFL_ITEM    1
#define GFL_ROAM    2
#define GFL_DROPPED 4

struct bot_goalstate_s;

typedef struct bot_goal_s
{
    vec3_t origin;
    int areanum;
    vec3_t mins;
    vec3_t maxs;
    int entitynum;
    int number;
    int flags;
    int iteminfo;
} bot_goal_t;

typedef struct bot_avoidgoal_s
{
    int number;
    float timeout;
} bot_avoidgoal_t;

typedef struct bot_goalstate_s
{
    bot_weight_config_t *itemweightconfig;
    int *itemweightindex;
    int itemweightcount;

    int client;
    int lastreachabilityarea;

    bot_goal_t goalstack[BOT_GOAL_MAX_STACK];
    int goalstacktop;

    bot_avoidgoal_t avoidgoals[BOT_GOAL_MAX_AVOID];
    int numavoidgoals;

    int avoidreach[BOT_GOAL_MAX_AVOIDREACH];
    float avoidreachtimes[BOT_GOAL_MAX_AVOIDREACH];
    int numavoidreach;
} bot_goalstate_t;

typedef struct bot_levelitem_setup_s
{
    const char *classname;
    bot_goal_t goal;
    float respawntime;
    float weight;
    int flags;
} bot_levelitem_setup_t;

int BotAllocGoalState(int client);
void BotFreeGoalState(int handle);
void BotResetGoalState(int handle);

int BotLoadItemWeights(int handle, const char *filename);
void BotFreeItemWeights(int handle);
int BotWeightIndex(int handle, const char *classname);

void BotResetAvoidGoals(int handle);
void BotAddToAvoidGoals(int handle, int number, float avoidtime);
void BotRemoveFromAvoidGoals(int handle, int number);
float BotAvoidGoalTime(int handle, int number);
void BotSetAvoidGoalTime(int handle, int number, float avoidtime);
void BotResetAvoidReach(int handle);
void BotAddToAvoidReach(int handle, int number, float avoidtime);

int BotPushGoal(int handle, const bot_goal_t *goal);
int BotPopGoal(int handle);
void BotEmptyGoalStack(int handle);
int BotGetTopGoal(int handle, bot_goal_t *goal);
int BotGetSecondGoal(int handle, bot_goal_t *goal);

int BotChooseLTGItem(int handle, const vec3_t origin, const int *inventory, int travelflags);
int BotChooseNBGItem(int handle,
                     const vec3_t origin,
                     const int *inventory,
                     int travelflags,
                     const bot_goal_t *ltg,
                     float maxtime);

float BotGoal_EvaluateStackGoal(int handle,
                                const bot_goal_t *goal,
                                const vec3_t origin,
                                int start_area,
                                const int *inventory,
                                int travelflags,
                                int *travel_time);

int BotTouchingGoal(const vec3_t origin, const bot_goal_t *goal);
void BotGoalName(int number, char *name, int size);
void BotDumpAvoidGoals(int handle);
void BotDumpGoalStack(int handle);

int BotGoal_RegisterLevelItem(const bot_levelitem_setup_t *setup);
void BotGoal_UnregisterLevelItem(int number);
void BotGoal_MarkItemTaken(int number, float respawn_delay);

void BotGoal_SetCurrentTime(float now);
float BotGoal_CurrentTime(void);

const bot_goalstate_t *BotGoalStatePeek(int handle);

#ifdef __cplusplus
}
#endif

