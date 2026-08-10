#pragma once

#include <stddef.h>

#include "shared/q_shared.h"

#ifdef __cplusplus
extern "C" {
#endif

struct aas_reachability_s;

/* movement command bitmasks */
#define MOVE_WALK        1
#define MOVE_CROUCH      2
#define MOVE_JUMP        4
#define MOVE_GRAPPLE     8
#define MOVE_ROCKETJUMP  16
#define MOVE_BFGJUMP     32

/* retail move-state flags */
#define MFL_BARRIERJUMP      0x01
#define MFL_ONGROUND         0x02
#define MFL_SWIMMING         0x04
#define MFL_AGAINSTLADDER    0x08
#define MFL_WATERJUMP        0x10
#define MFL_TELEPORTED       0x20
#define MFL_GRAPPLEATTACHED  0x40
#define MFL_GRAPPLERESET     0x80

/* retail move-result flags */
#define MOVERESULT_MOVEMENTVIEW    1
#define MOVERESULT_SWIMVIEW        2
#define MOVERESULT_WAITING         4
#define MOVERESULT_MOVEMENTVIEWSET 8

#define MAX_AVOIDREACH 1

/* compatibility-only avoid-spot types */
#define AVOID_CLEAR     0
#define AVOID_ALWAYS    1
#define AVOID_DONTBLOCK 2

/* presence types */
#define PRESENCE_NONE   1
#define PRESENCE_NORMAL 2
#define PRESENCE_CROUCH 4

/* retail moveresult type indicators */
#define RESULTTYPE_ELEVATORUP 1

#ifndef BOT_GOAL_STRUCT_DEFINED
#define BOT_GOAL_STRUCT_DEFINED
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
#endif

typedef struct bot_initmove_s
{
	vec3_t origin;
	vec3_t velocity;
	vec3_t viewoffset;
	int entitynum;
	int client;
	float thinktime;
	int presencetype;
	vec3_t viewangles;
	int or_moveflags;
} bot_initmove_t;

typedef struct bot_moveresult_s
{
	int failure;
	int type;
	int blocked;
	int blockentity;
	int traveltype;
	int flags;
	vec3_t movedir;
	vec3_t ideal_viewangles;
} bot_moveresult_t;

typedef struct bot_movestate_s
{
	vec3_t origin;
	vec3_t velocity;
	vec3_t viewoffset;
	int entitynum;
	int client;
	float thinktime;
	int presencetype;
	vec3_t viewangles;
	int areanum;
	int lastareanum;
	int lastgoalareanum;
	int lastreachnum;
	vec3_t lastorigin;
	int reachareanum;
	int moveflags;
	int jumpreach;
	float grapplevisible_time;
	float lastgrappledist;
	float reachability_time;
	int avoidreach[MAX_AVOIDREACH];
	float avoidreachtimes[MAX_AVOIDREACH];
	int avoidreachtries[MAX_AVOIDREACH];
} bot_movestate_t;

#if defined(__cplusplus)
#define BOT_MOVE_STATIC_ASSERT(condition, message) static_assert(condition, message)
#else
#define BOT_MOVE_STATIC_ASSERT(condition, message) _Static_assert(condition, message)
#endif

BOT_MOVE_STATIC_ASSERT(sizeof(bot_moveresult_t) == 0x30,
	"retail bot_moveresult_t must be 48 bytes");
BOT_MOVE_STATIC_ASSERT(offsetof(bot_moveresult_t, movedir) == 0x18,
	"retail bot_moveresult_t movedir offset mismatch");
BOT_MOVE_STATIC_ASSERT(offsetof(bot_moveresult_t, ideal_viewangles) == 0x24,
	"retail bot_moveresult_t ideal_viewangles offset mismatch");
BOT_MOVE_STATIC_ASSERT(sizeof(bot_movestate_t) == 0x80,
	"retail bot_movestate_t must be 128 bytes");
BOT_MOVE_STATIC_ASSERT(offsetof(bot_movestate_t, areanum) == 0x40,
	"retail bot_movestate_t areanum offset mismatch");
BOT_MOVE_STATIC_ASSERT(offsetof(bot_movestate_t, lastreachnum) == 0x4c,
	"retail bot_movestate_t lastreachnum offset mismatch");
BOT_MOVE_STATIC_ASSERT(offsetof(bot_movestate_t, moveflags) == 0x60,
	"retail bot_movestate_t moveflags offset mismatch");
BOT_MOVE_STATIC_ASSERT(offsetof(bot_movestate_t, reachability_time) == 0x70,
	"retail bot_movestate_t reachability_time offset mismatch");
BOT_MOVE_STATIC_ASSERT(offsetof(bot_movestate_t, avoidreach) == 0x74,
	"retail bot_movestate_t avoidreach offset mismatch");
BOT_MOVE_STATIC_ASSERT(offsetof(bot_movestate_t, avoidreachtimes) == 0x78,
	"retail bot_movestate_t avoidreachtimes offset mismatch");
BOT_MOVE_STATIC_ASSERT(offsetof(bot_movestate_t, avoidreachtries) == 0x7c,
	"retail bot_movestate_t avoidreachtries offset mismatch");

#undef BOT_MOVE_STATIC_ASSERT

bot_moveresult_t *BotClearMoveResult(bot_moveresult_t *moveresult);
bot_moveresult_t *BotMoveToGoal(bot_moveresult_t *result,
	bot_movestate_t *movestate,
	const bot_goal_t *goal,
	int travelflags);
int BotMoveInDirection(bot_movestate_t *movestate,
	const vec3_t dir,
	float speed,
	int type);
int BotResetMoveState(bot_movestate_t *movestate);
int *BotResetAvoidReach(bot_movestate_t *movestate);
void BotResetLastAvoidReach(bot_movestate_t *movestate);
int BotReachabilityArea(const vec3_t origin, int testground);
int BotMovementViewTarget(bot_movestate_t *movestate,
	const bot_goal_t *goal,
	int travelflags,
	vec3_t target);
bot_moveresult_t BotTravel_Grapple(bot_movestate_t *movestate,
	const struct aas_reachability_s *reach);

int BotAllocMoveStateHandle(void);
int BotBindRetailMoveState(int client, bot_movestate_t *state);
int BotRebindRetailMoveState(int old_handle,
	int client,
	bot_movestate_t *state);
void BotFreeMoveStateHandle(int handle);
bot_movestate_t *BotMoveStateFromHandle(int handle);
void BotInitMoveStateHandle(int handle, const bot_initmove_t *initmove);
void BotMoveToGoalHandle(bot_moveresult_t *result,
	int movestate,
	const bot_goal_t *goal,
	int travelflags);
int BotMoveInDirectionHandle(int movestate,
	const vec3_t dir,
	float speed,
	int type);
void BotResetMoveStateHandle(int movestate);
void BotResetAvoidReachHandle(int movestate);
void BotResetLastAvoidReachHandle(int movestate);
int BotMovementViewTargetHandle(int movestate,
	const bot_goal_t *goal,
	int travelflags,
	float lookahead,
	vec3_t target);

int BotPredictVisiblePosition(const vec3_t origin,
	int areanum,
	const bot_goal_t *goal,
	int travelflags,
	vec3_t target);
void BotAddAvoidSpot(int movestate, vec3_t origin, float radius, int type);
void BotSetBrushModelTypes(void);
int BotSetupMoveAI(void);
void BotShutdownMoveAI(void);

#ifdef __cplusplus
} /* extern "C" */
#endif
