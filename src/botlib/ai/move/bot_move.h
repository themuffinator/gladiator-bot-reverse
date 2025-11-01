#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "../../shared/q_shared.h"
#include "mover_catalogue.h"

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

/* move state flags */
#define MFL_BARRIERJUMP   1
#define MFL_ONGROUND      2
#define MFL_SWIMMING      4
#define MFL_AGAINSTLADDER 8
#define MFL_WATERJUMP     16
#define MFL_TELEPORTED    32
#define MFL_GRAPPLEPULL   64
#define MFL_ACTIVEGRAPPLE 128
#define MFL_GRAPPLERESET  256
#define MFL_WALK          512

/* move result flags */
#define MOVERESULT_MOVEMENTVIEW    1
#define MOVERESULT_SWIMVIEW        2
#define MOVERESULT_WAITING         4
#define MOVERESULT_MOVEMENTVIEWSET 8
#define MOVERESULT_MOVEMENTWEAPON  16
#define MOVERESULT_ONTOPOFOBSTACLE 32
#define MOVERESULT_ONTOPOF_FUNCBOB 64
#define MOVERESULT_ONTOPOF_ELEVATOR 128
#define MOVERESULT_BLOCKEDBYAVOIDSPOT 256

#define MAX_AVOIDREACH  1
#define MAX_AVOIDSPOTS  32

/* avoid spot types */
#define AVOID_CLEAR     0
#define AVOID_ALWAYS    1
#define AVOID_DONTBLOCK 2

/* presence types */
#define PRESENCE_NONE    1
#define PRESENCE_NORMAL  2
#define PRESENCE_CROUCH  4

/* moveresult type indicators */
#define RESULTTYPE_ELEVATORUP         1
#define RESULTTYPE_WAITFORFUNCBOBBING 2
#define RESULTTYPE_BADGRAPPLEPATH     4
#define RESULTTYPE_INSOLIDAREA        8

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
    int weapon;
    vec3_t movedir;
    vec3_t ideal_viewangles;
} bot_moveresult_t;

typedef struct bot_avoidspot_s
{
    vec3_t origin;
    float radius;
    int type;
} bot_avoidspot_t;

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

    bot_avoidspot_t avoidspots[MAX_AVOIDSPOTS];
    int numavoidspots;
} bot_movestate_t;

int BotAllocMoveState(void);
void BotFreeMoveState(int handle);
bot_movestate_t *BotMoveStateFromHandle(int handle);
void BotResetMoveState(int movestate);
void BotInitMoveState(int handle, const bot_initmove_t *initmove);

void BotClearMoveResult(bot_moveresult_t *moveresult);
void BotMoveClassifyEnvironment(bot_movestate_t *ms);

void BotMoveToGoal(bot_moveresult_t *result,
                   int movestate,
                   const bot_goal_t *goal,
                   int travelflags);

int BotMoveInDirection(int movestate, const vec3_t dir, float speed, int type);

void BotResetAvoidReach(int movestate);

void AI_MoveFrame(bot_moveresult_t *result,
                  int movestate,
                  const bot_goal_t *goal,
                  int travelflags);

bot_moveresult_t BotTravel_Grapple(bot_movestate_t *ms, const struct aas_reachability_s *reach);

#ifdef __cplusplus
} /* extern "C" */
#endif

