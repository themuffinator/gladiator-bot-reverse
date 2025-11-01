#pragma once

#include <stdbool.h>

#include "../../../shared/q_shared.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum number of goals tracked on the internal stack. */
#define MAX_GOALSTACK 8

/** Maximum number of goals tracked in the avoidance table. */
#define MAX_AVOIDGOALS 256

/** Goal is a generic placeholder. */
#define GFL_NONE 0x0000
/** Goal represents a map item. */
#define GFL_ITEM 0x0001
/** Goal is a roam/sandbox navigation target. */
#define GFL_ROAM 0x0002
/** Goal originates from a dropped entity (e.g. weapon). */
#define GFL_DROPPED 0x0004

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

typedef struct bot_goalstate_s bot_goalstate_t;

int BotAllocGoalState(int client);
void BotFreeGoalState(int handle);

void BotResetGoalState(int goalstate);

int BotLoadItemWeights(int goalstate, const char *filename);
void BotFreeItemWeights(int goalstate);

void BotPushGoal(int goalstate, const bot_goal_t *goal);
void BotPopGoal(int goalstate);
void BotEmptyGoalStack(int goalstate);

int BotGetTopGoal(int goalstate, bot_goal_t *goal);
int BotGetSecondGoal(int goalstate, bot_goal_t *goal);

void BotResetAvoidGoals(int goalstate);
void BotRemoveFromAvoidGoals(int goalstate, int number);
float BotAvoidGoalTime(int goalstate, int number);
void BotSetAvoidGoalTime(int goalstate, int number, float avoidtime);

int BotChooseLTGItem(int goalstate, vec3_t origin, const int *inventory, int travelflags);
int BotChooseNBGItem(int goalstate,
                     vec3_t origin,
                     const int *inventory,
                     int travelflags,
                     const bot_goal_t *ltg,
                     float maxtime);

void BotGoal_Shutdown(void);
void BotGoal_SetClient(int goalstate, int client);

#ifdef __cplusplus
} /* extern "C" */
#endif

