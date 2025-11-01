#include "bot_move.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "../../aas/aas_local.h"
#include "../../common/l_log.h"
#include "../../common/l_memory.h"
#include "../../ea/ea_local.h"
#include "../../../q2bridge/bridge.h"
#include "../../../q2bridge/bridge_config.h"

static bot_movestate_t *g_botMoveStates[MAX_CLIENTS + 1];

static const char *BotMove_DefaultGrappleModel(void)
{
    return "models/weapons/grapple/hook/tris.md2";
}

static const char *BotMove_GrappleModelPath(void)
{
    const char *path = Bridge_GrappleModelPath();
    if (path == NULL || path[0] == '\0')
    {
        return BotMove_DefaultGrappleModel();
    }

    return path;
}

static bool BotMove_UseHookEnabled(void)
{
    libvar_t *usehook = Bridge_UseHook();
    return usehook != NULL && usehook->value != 0.0f;
}

static bool BotMove_LaserHookEnabled(void)
{
    libvar_t *laserhook = Bridge_LaserHook();
    return laserhook != NULL && laserhook->value != 0.0f;
}

static void BotMove_PrecacheGrappleModel(bot_movestate_t *ms)
{
    (void)ms;

    static bool s_grapple_precached = false;
    if (s_grapple_precached)
    {
        return;
    }

    const char *path = BotMove_GrappleModelPath();
    if (path == NULL || path[0] == '\0')
    {
        return;
    }

    int client = (ms != NULL) ? ms->client : 0;
    Q2_BotClientCommand(client, "%s %s", "precache", path);
    s_grapple_precached = true;
}

static void BotMove_DisengageGrapple(bot_movestate_t *ms)
{
    if (ms == NULL)
    {
        return;
    }

    EA_Command(ms->client, "%s", "hookoff");

    ms->moveflags &= ~(MFL_ACTIVEGRAPPLE | MFL_GRAPPLEPULL);
    ms->grapplevisible_time = 0.0f;
    ms->lastgrappledist = 0.0f;
    ms->reachability_time = 0.0f;
}

bot_moveresult_t BotTravel_Grapple(bot_movestate_t *ms, const aas_reachability_t *reach)
{
    bot_moveresult_t result;
    BotClearMoveResult(&result);
    result.traveltype = TRAVEL_GRAPPLEHOOK;

    if (ms == NULL || reach == NULL)
    {
        result.failure = 1;
        return result;
    }

    bool use_hook = BotMove_UseHookEnabled();
    bool laser_hook = BotMove_LaserHookEnabled();

    if (!use_hook || !laser_hook)
    {
        result.failure = 1;
        return result;
    }

    BotMove_PrecacheGrappleModel(ms);

    vec3_t view_origin;
    VectorAdd(ms->origin, ms->viewoffset, view_origin);

    vec3_t to_goal;
    VectorSubtract(reach->end, view_origin, to_goal);

    vec3_t ideal_viewangles;
    Vector2Angles(to_goal, ideal_viewangles);
    VectorCopy(ideal_viewangles, result.ideal_viewangles);
    result.flags |= MOVERESULT_MOVEMENTVIEW;

    if (ms->moveflags & MFL_ACTIVEGRAPPLE)
    {
        vec3_t pull_dir;
        VectorSubtract(reach->end, ms->origin, pull_dir);
        pull_dir[2] = 0.0f;
        float pull_dist = VectorNormalizeInline(pull_dir);

        bool slack_release = false;
        if (pull_dist < 48.0f)
        {
            if (ms->lastgrappledist - pull_dist < 1.0f)
            {
                slack_release = true;
            }
        }
        else
        {
            if (pull_dist > ms->lastgrappledist - 2.0f)
            {
                if (aasworld.time - ms->grapplevisible_time > 0.4f)
                {
                    slack_release = true;
                }
            }
            else
            {
                ms->grapplevisible_time = aasworld.time;
            }
        }

        if (slack_release)
        {
            BotMove_DisengageGrapple(ms);
            result.flags |= MOVERESULT_MOVEMENTWEAPON;
            return result;
        }

        EA_Attack(ms->client);
        ms->moveflags |= MFL_GRAPPLEPULL;
        result.flags |= MOVERESULT_MOVEMENTWEAPON;
        ms->lastgrappledist = pull_dist;
        ms->reachability_time = aasworld.time + BotMove_TravelTimeout(TRAVEL_GRAPPLEHOOK);
        result.weapon = 0;
        return result;
    }

    vec3_t approach_dir;
    VectorSubtract(reach->start, ms->origin, approach_dir);
    if (!(ms->moveflags & MFL_SWIMMING))
    {
        approach_dir[2] = 0.0f;
    }

    float start_dist = VectorNormalizeInline(approach_dir);
    float yaw_diff = fabsf(AngleDiff(ideal_viewangles[YAW], ms->viewangles[YAW]));
    float pitch_diff = fabsf(AngleDiff(ideal_viewangles[PITCH], ms->viewangles[PITCH]));

    ms->grapplevisible_time = aasworld.time;

    if (start_dist < 5.0f && yaw_diff < 2.0f && pitch_diff < 2.0f)
    {
        EA_Command(ms->client, "%s", "hookon");
        EA_Attack(ms->client);
        ms->moveflags |= (MFL_ACTIVEGRAPPLE | MFL_GRAPPLEPULL);
        vec3_t grapple_delta;
        VectorSubtract(reach->end, ms->origin, grapple_delta);
        grapple_delta[2] = 0.0f;
        ms->lastgrappledist = VectorLength(grapple_delta);
        result.flags |= MOVERESULT_MOVEMENTWEAPON;
        ms->reachability_time = aasworld.time + BotMove_TravelTimeout(TRAVEL_GRAPPLEHOOK);
        result.weapon = 0;
        return result;
    }

    float speed = 400.0f;
    if (start_dist < 70.0f)
    {
        speed = 300.0f - (300.0f - 4.0f * start_dist);
    }

    EA_Move(ms->client, approach_dir, speed);
    VectorCopy(approach_dir, result.movedir);
    if (ms->moveflags & MFL_SWIMMING)
    {
        result.flags |= MOVERESULT_SWIMVIEW;
    }

    ms->reachability_time = aasworld.time + BotMove_TravelTimeout(TRAVEL_GRAPPLEHOOK);
    return result;
}

static float VectorLengthSquared(const vec3_t v)
{
    return v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
}

static float VectorNormalizeInline(vec3_t v)
{
    float length = sqrtf(VectorLengthSquared(v));
    if (length > 1e-6f)
    {
        float scale = 1.0f / length;
        v[0] *= scale;
        v[1] *= scale;
        v[2] *= scale;
    }
    else
    {
        VectorClear(v);
        length = 0.0f;
    }
    return length;
}

static float VectorNormalizeTo(const vec3_t src, vec3_t dst)
{
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
    return VectorNormalizeInline(dst);
}

static int BotMove_FindAreaForPoint(const vec3_t origin)
{
    if (!aasworld.loaded || aasworld.areas == NULL || aasworld.numAreas <= 0)
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

static int BotMove_TravelFlagsForType(int traveltype)
{
    if (traveltype < 0 || traveltype >= MAX_TRAVELTYPES)
    {
        return 0;
    }
    return aasworld.travelflagfortype[traveltype];
}

static bool BotMove_TravelAllowed(int traveltype, int travelflags)
{
    int flags = BotMove_TravelFlagsForType(traveltype);
    int required = (travelflags != 0) ? travelflags : TFL_DEFAULT;
    if (required == 0)
    {
        return true;
    }
    return (flags & required) != 0;
}

static float BotMove_TravelTimeout(int traveltype)
{
    switch (traveltype)
    {
        case TRAVEL_LADDER:
        case TRAVEL_ELEVATOR:
        case TRAVEL_FUNCBOB:
            return 6.0f;
        case TRAVEL_GRAPPLEHOOK:
            return 10.0f;
        default:
            return 5.0f;
    }
}

static void BotMove_SetMovementView(const vec3_t dir, bot_moveresult_t *result, bool swimming)
{
    if (result == NULL)
    {
        return;
    }

    if (swimming)
    {
        result->flags |= MOVERESULT_SWIMVIEW;
    }

    /* ideal view angles remain zeroed until the caller wires in full aiming */
    (void)dir;
}

static void BotMove_DirectToGoal(bot_movestate_t *ms,
                                 const bot_goal_t *goal,
                                 bot_moveresult_t *result)
{
    if (ms == NULL || goal == NULL || result == NULL)
    {
        return;
    }

    vec3_t dir;
    VectorSubtract(goal->origin, ms->origin, dir);

    bool swimming = (ms->moveflags & MFL_SWIMMING) != 0;
    if (!swimming)
    {
        dir[2] = 0.0f;
    }

    VectorNormalizeInline(dir);
    VectorCopy(dir, result->movedir);
    result->traveltype = swimming ? TRAVEL_SWIM : TRAVEL_WALK;
    BotMove_SetMovementView(dir, result, swimming);

    ms->lastreachnum = 0;
    ms->reachareanum = goal->areanum;
    ms->lastgoalareanum = goal->areanum;
    VectorCopy(ms->origin, ms->lastorigin);
}

static bool BotMove_LoadReachability(int reachnum, aas_reachability_t *out)
{
    if (out == NULL || aasworld.reachability == NULL || aasworld.numReachability <= 0)
    {
        return false;
    }

    if (reachnum < 0 || reachnum >= aasworld.numReachability)
    {
        return false;
    }

    *out = aasworld.reachability[reachnum];
    return true;
}

static void BotMove_CopyMoveResult(bot_moveresult_t *dst, const bot_moveresult_t *src)
{
    if (dst == NULL || src == NULL)
    {
        return;
    }

    memcpy(dst, src, sizeof(*dst));
}

static void BotMove_RefreshAvoidReach(bot_movestate_t *ms)
{
    if (ms == NULL)
    {
        return;
    }

    float now = aasworld.time;
    for (int i = 0; i < MAX_AVOIDREACH; ++i)
    {
        if (ms->avoidreach[i] <= 0)
        {
            continue;
        }

        if (ms->avoidreachtimes[i] <= now)
        {
            ms->avoidreach[i] = 0;
            ms->avoidreachtimes[i] = 0.0f;
            ms->avoidreachtries[i] = 0;
        }
    }
}

static bool BotMove_ShouldAvoidReach(const bot_movestate_t *ms, int reachnum)
{
    if (ms == NULL || reachnum <= 0)
    {
        return false;
    }

    float now = aasworld.time;
    for (int i = 0; i < MAX_AVOIDREACH; ++i)
    {
        if (ms->avoidreach[i] == reachnum && ms->avoidreachtimes[i] > now)
        {
            return true;
        }
    }

    return false;
}

static int BotMove_ReconstructFirstReach(const int *parent_area,
                                         const int *parent_reach,
                                         int start_area,
                                         int goal_area)
{
    if (parent_area == NULL || parent_reach == NULL)
    {
        return 0;
    }

    int area = goal_area;
    int reachnum = 0;

    while (area != 0 && area != start_area)
    {
        reachnum = parent_reach[area];
        area = parent_area[area];
    }

    return reachnum;
}

static int BotGetReachabilityToGoal(bot_movestate_t *ms,
                                    const bot_goal_t *goal,
                                    int travelflags,
                                    aas_reachability_t *out,
                                    int *resultFlags)
{
    if (resultFlags != NULL)
    {
        *resultFlags = 0;
    }

    if (ms == NULL || goal == NULL || out == NULL)
    {
        return 0;
    }

    if (!aasworld.loaded ||
        aasworld.areasettings == NULL ||
        aasworld.reachability == NULL ||
        aasworld.numReachability <= 0)
    {
        return 0;
    }

    if (ms->areanum <= 0 || ms->areanum >= aasworld.numAreaSettings)
    {
        return 0;
    }

    int goalArea = goal->areanum;
    if (goalArea <= 0 || goalArea >= aasworld.numAreaSettings)
    {
        return 0;
    }

    int areaCount = aasworld.numAreaSettings;
    int *queue = (int *)malloc((size_t)areaCount * sizeof(int));
    int *visited = (int *)calloc((size_t)areaCount, sizeof(int));
    int *parent_area = (int *)calloc((size_t)areaCount, sizeof(int));
    int *parent_reach = (int *)calloc((size_t)areaCount, sizeof(int));
    if (queue == NULL || visited == NULL || parent_area == NULL || parent_reach == NULL)
    {
        free(queue);
        free(visited);
        free(parent_area);
        free(parent_reach);
        return 0;
    }

    int head = 0;
    int tail = 0;

    queue[tail++] = ms->areanum;
    visited[ms->areanum] = 1;
    parent_area[ms->areanum] = 0;
    parent_reach[ms->areanum] = 0;

    while (head < tail)
    {
        int current = queue[head++];
        if (current == goalArea)
        {
            break;
        }

        if (current <= 0 || current >= aasworld.numAreaSettings)
        {
            continue;
        }

        const aas_areasettings_t *settings = &aasworld.areasettings[current];
        int start = settings->firstreachablearea;
        int count = settings->numreachableareas;
        for (int offset = 0; offset < count; ++offset)
        {
            int reachIndex = start + offset;
            if (reachIndex <= 0 || reachIndex >= aasworld.numReachability)
            {
                continue;
            }

            if (BotMove_ShouldAvoidReach(ms, reachIndex))
            {
                continue;
            }

            const aas_reachability_t *candidate = &aasworld.reachability[reachIndex];
            int traveltype = candidate->traveltype & TRAVELTYPE_MASK;
            if (!BotMove_TravelAllowed(traveltype, travelflags))
            {
                continue;
            }

            int nextArea = candidate->areanum;
            if (nextArea <= 0 || nextArea >= aasworld.numAreaSettings)
            {
                continue;
            }

            if (visited[nextArea])
            {
                continue;
            }

            parent_area[nextArea] = current;
            parent_reach[nextArea] = reachIndex;
            visited[nextArea] = 1;
            queue[tail++] = nextArea;
        }
    }

    int reachnum = 0;
    if (visited[goalArea])
    {
        reachnum = BotMove_ReconstructFirstReach(parent_area, parent_reach, ms->areanum, goalArea);
    }

    free(queue);
    free(visited);
    free(parent_area);
    free(parent_reach);

    if (reachnum <= 0)
    {
        return 0;
    }

    if (!BotMove_LoadReachability(reachnum, out))
    {
        return 0;
    }

    return reachnum;
}

static void BotMove_PrepareResult(bot_moveresult_t *result,
                                  const vec3_t dir,
                                  int traveltype,
                                  bool swimming)
{
    bot_moveresult_t temp;
    BotClearMoveResult(&temp);

    VectorCopy(dir, temp.movedir);
    temp.traveltype = traveltype;
    temp.flags |= MOVERESULT_MOVEMENTVIEW;
    BotMove_SetMovementView(dir, &temp, swimming);

    BotMove_CopyMoveResult(result, &temp);
}

static void BotMove_TravelWalk(bot_movestate_t *ms,
                               const aas_reachability_t *reach,
                               bot_moveresult_t *result)
{
    vec3_t dir;
    VectorSubtract(reach->end, ms->origin, dir);
    dir[2] = 0.0f;
    VectorNormalizeInline(dir);

    ms->moveflags |= MFL_WALK;
    BotMove_PrepareResult(result, dir, TRAVEL_WALK, false);
}

static void BotMove_TravelCrouch(bot_movestate_t *ms,
                                 const aas_reachability_t *reach,
                                 bot_moveresult_t *result)
{
    vec3_t dir;
    VectorSubtract(reach->end, ms->origin, dir);
    dir[2] = 0.0f;
    VectorNormalizeInline(dir);

    BotMove_PrepareResult(result, dir, TRAVEL_CROUCH, false);
}

static void BotMove_TravelBarrierJump(bot_movestate_t *ms,
                                      const aas_reachability_t *reach,
                                      bot_moveresult_t *result)
{
    vec3_t dir;
    VectorSubtract(reach->end, ms->origin, dir);
    VectorNormalizeInline(dir);

    ms->moveflags |= MFL_BARRIERJUMP;
    BotMove_PrepareResult(result, dir, TRAVEL_BARRIERJUMP, false);
}

static void BotMove_TravelLadder(bot_movestate_t *ms,
                                 const aas_reachability_t *reach,
                                 bot_moveresult_t *result)
{
    vec3_t dir;
    VectorSubtract(reach->end, ms->origin, dir);
    VectorNormalizeInline(dir);

    ms->moveflags |= MFL_AGAINSTLADDER;
    BotMove_PrepareResult(result, dir, TRAVEL_LADDER, false);
}

static void BotMove_TravelWalkOffLedge(bot_movestate_t *ms,
                                       const aas_reachability_t *reach,
                                       bot_moveresult_t *result)
{
    vec3_t dir;
    VectorSubtract(reach->end, ms->origin, dir);
    VectorNormalizeInline(dir);

    BotMove_PrepareResult(result, dir, TRAVEL_WALKOFFLEDGE, false);
}

static void BotMove_TravelJump(bot_movestate_t *ms,
                               const aas_reachability_t *reach,
                               bot_moveresult_t *result,
                               int traveltype)
{
    vec3_t dir;
    VectorSubtract(reach->end, ms->origin, dir);
    VectorNormalizeInline(dir);

    ms->jumpreach = 1;
    BotMove_PrepareResult(result, dir, traveltype, false);
}

static void BotMove_TravelSwim(bot_movestate_t *ms,
                               const aas_reachability_t *reach,
                               bot_moveresult_t *result)
{
    vec3_t dir;
    VectorSubtract(reach->end, ms->origin, dir);
    VectorNormalizeInline(dir);

    ms->moveflags |= MFL_SWIMMING;
    BotMove_PrepareResult(result, dir, TRAVEL_SWIM, true);
}

static void BotMove_TravelWaterJump(bot_movestate_t *ms,
                                    const aas_reachability_t *reach,
                                    bot_moveresult_t *result)
{
    vec3_t dir;
    VectorSubtract(reach->end, ms->origin, dir);
    VectorNormalizeInline(dir);

    ms->moveflags |= MFL_WATERJUMP;
    BotMove_PrepareResult(result, dir, TRAVEL_WATERJUMP, true);
}

static void BotMove_TravelTeleport(bot_movestate_t *ms,
                                   const aas_reachability_t *reach,
                                   bot_moveresult_t *result)
{
    (void)ms;

    vec3_t dir;
    VectorSubtract(reach->end, reach->start, dir);
    VectorNormalizeInline(dir);

    BotMove_PrepareResult(result, dir, TRAVEL_TELEPORT, false);
}

static void BotMove_TravelElevator(bot_movestate_t *ms,
                                   const aas_reachability_t *reach,
                                   bot_moveresult_t *result)
{
    vec3_t dir;
    VectorSubtract(reach->end, ms->origin, dir);
    VectorNormalizeInline(dir);

    BotMove_PrepareResult(result, dir, TRAVEL_ELEVATOR, false);
    result->flags |= MOVERESULT_ONTOPOF_ELEVATOR;
}

static void BotMove_TravelGrapple(bot_movestate_t *ms,
                                  const aas_reachability_t *reach,
                                  bot_moveresult_t *result)
{
    vec3_t dir;
    VectorSubtract(reach->end, ms->origin, dir);
    VectorNormalizeInline(dir);

    ms->moveflags |= MFL_ACTIVEGRAPPLE;
    BotMove_PrepareResult(result, dir, TRAVEL_GRAPPLEHOOK, false);
    result->flags |= MOVERESULT_MOVEMENTWEAPON;
}

static void BotMove_TravelJumpPad(bot_movestate_t *ms,
                                  const aas_reachability_t *reach,
                                  bot_moveresult_t *result)
{
    vec3_t dir;
    VectorSubtract(reach->end, ms->origin, dir);
    VectorNormalizeInline(dir);

    BotMove_PrepareResult(result, dir, TRAVEL_JUMPPAD, false);
}

static void BotMove_TravelFuncBob(bot_movestate_t *ms,
                                  const aas_reachability_t *reach,
                                  bot_moveresult_t *result)
{
    vec3_t dir;
    VectorSubtract(reach->end, ms->origin, dir);
    VectorNormalizeInline(dir);

    BotMove_PrepareResult(result, dir, TRAVEL_FUNCBOB, false);
    result->flags |= MOVERESULT_ONTOPOF_FUNCBOB;
}

static void BotMove_DispatchTravel(bot_movestate_t *ms,
                                   const aas_reachability_t *reach,
                                   bot_moveresult_t *result)
{
    if (ms == NULL || reach == NULL || result == NULL)
    {
        return;
    }

    bot_moveresult_t temp;
    BotClearMoveResult(&temp);

    int traveltype = reach->traveltype & TRAVELTYPE_MASK;
    switch (traveltype)
    {
        case TRAVEL_WALK:
            BotMove_TravelWalk(ms, reach, &temp);
            break;
        case TRAVEL_CROUCH:
            BotMove_TravelCrouch(ms, reach, &temp);
            break;
        case TRAVEL_BARRIERJUMP:
            BotMove_TravelBarrierJump(ms, reach, &temp);
            break;
        case TRAVEL_LADDER:
            BotMove_TravelLadder(ms, reach, &temp);
            break;
        case TRAVEL_WALKOFFLEDGE:
            BotMove_TravelWalkOffLedge(ms, reach, &temp);
            break;
        case TRAVEL_JUMP:
            BotMove_TravelJump(ms, reach, &temp, TRAVEL_JUMP);
            break;
        case TRAVEL_SWIM:
            BotMove_TravelSwim(ms, reach, &temp);
            break;
        case TRAVEL_WATERJUMP:
            BotMove_TravelWaterJump(ms, reach, &temp);
            break;
        case TRAVEL_TELEPORT:
            BotMove_TravelTeleport(ms, reach, &temp);
            break;
        case TRAVEL_ELEVATOR:
            BotMove_TravelElevator(ms, reach, &temp);
            break;
        case TRAVEL_GRAPPLEHOOK:
        {
            bot_moveresult_t grapple = BotTravel_Grapple(ms, reach);
            *result = grapple;
            result->traveltype = traveltype;
            return;
        }
        case TRAVEL_ELEVATOR:
            result->flags |= MOVERESULT_ONTOPOF_ELEVATOR;
            BotMove_TravelGrapple(ms, reach, &temp);
            break;
        case TRAVEL_ROCKETJUMP:
            BotMove_TravelJump(ms, reach, &temp, TRAVEL_ROCKETJUMP);
            break;
        case TRAVEL_BFGJUMP:
            BotMove_TravelJump(ms, reach, &temp, TRAVEL_BFGJUMP);
            break;
        case TRAVEL_JUMPPAD:
            BotMove_TravelJumpPad(ms, reach, &temp);
            break;
        case TRAVEL_FUNCBOB:
            BotMove_TravelFuncBob(ms, reach, &temp);
            break;
        default:
            BotLib_Print(PRT_WARNING,
                         "(last) travel type %d not implemented yet\n",
                         traveltype);
            temp.traveltype = traveltype;
            temp.failure = 1;
            break;
    }

    BotMove_CopyMoveResult(result, &temp);
    ms->reachability_time = aasworld.time + BotMove_TravelTimeout(traveltype);
    ms->reachareanum = reach->areanum;
}

int BotAllocMoveState(void)
{
    for (int handle = 1; handle <= MAX_CLIENTS; ++handle)
    {
        if (g_botMoveStates[handle] == NULL)
        {
            g_botMoveStates[handle] = GetClearedMemory(sizeof(bot_movestate_t));
            return handle;
        }
    }

    return 0;
}

void BotFreeMoveState(int handle)
{
    if (handle <= 0 || handle > MAX_CLIENTS)
    {
        BotLib_Print(PRT_WARNING,
                      "BotFreeMoveState: handle %d out of range\n",
                      handle);
        return;
    }

    bot_movestate_t *ms = g_botMoveStates[handle];
    if (ms == NULL)
    {
        BotLib_Print(PRT_WARNING,
                      "BotFreeMoveState: handle %d not allocated\n",
                      handle);
        return;
    }

    FreeMemory(ms);
    g_botMoveStates[handle] = NULL;
}

bot_movestate_t *BotMoveStateFromHandle(int handle)
{
    if (handle <= 0 || handle > MAX_CLIENTS)
    {
        return NULL;
    }

    return g_botMoveStates[handle];
}

void BotResetMoveState(int movestate)
{
    bot_movestate_t *ms = BotMoveStateFromHandle(movestate);
    if (ms == NULL)
    {
        return;
    }

    memset(ms, 0, sizeof(*ms));
}

void BotInitMoveState(int handle, const bot_initmove_t *initmove)
{
    bot_movestate_t *ms = BotMoveStateFromHandle(handle);
    if (ms == NULL)
    {
        return;
    }

    BotResetMoveState(handle);

    if (initmove == NULL)
    {
        return;
    }

    VectorCopy(initmove->origin, ms->origin);
    VectorCopy(initmove->velocity, ms->velocity);
    VectorCopy(initmove->viewoffset, ms->viewoffset);
    ms->entitynum = initmove->entitynum;
    ms->client = initmove->client;
    ms->thinktime = initmove->thinktime;
    ms->presencetype = initmove->presencetype;
    VectorCopy(initmove->viewangles, ms->viewangles);
    ms->moveflags = initmove->or_moveflags;

    BotMoveClassifyEnvironment(ms);
}

void BotClearMoveResult(bot_moveresult_t *moveresult)
{
    if (moveresult == NULL)
    {
        return;
    }

    memset(moveresult, 0, sizeof(*moveresult));
}

void BotMoveClassifyEnvironment(bot_movestate_t *ms)
{
    if (ms == NULL)
    {
        return;
    }

    ms->lastareanum = ms->areanum;
    int areanum = BotMove_FindAreaForPoint(ms->origin);
    if (areanum > 0)
    {
        ms->areanum = areanum;
    }

    ms->moveflags &= ~(MFL_SWIMMING | MFL_AGAINSTLADDER);

    if (aasworld.areasettings != NULL &&
        ms->areanum > 0 && ms->areanum < aasworld.numAreaSettings)
    {
        const aas_areasettings_t *settings = &aasworld.areasettings[ms->areanum];
        int contents = settings->contents;
        if (contents & (CONTENTS_WATER | CONTENTS_LAVA | CONTENTS_SLIME))
        {
            ms->moveflags |= MFL_SWIMMING;
        }
        if (contents & CONTENTS_LADDER)
        {
            ms->moveflags |= MFL_AGAINSTLADDER;
        }
    }
}

void BotMoveToGoal(bot_moveresult_t *result,
                   int movestate,
                   const bot_goal_t *goal,
                   int travelflags)
{
    if (result == NULL)
    {
        return;
    }

    bot_movestate_t *ms = BotMoveStateFromHandle(movestate);
    if (ms == NULL)
    {
        result->failure = 1;
        return;
    }

    if (goal == NULL || goal->areanum <= 0)
    {
        result->failure = 1;
        result->type = RESULTTYPE_INSOLIDAREA;
        return;
    }

    BotMove_RefreshAvoidReach(ms);

    ms->moveflags &= ~(MFL_SWIMMING | MFL_AGAINSTLADDER);

    if (ms->areanum == goal->areanum)
    {
        BotMove_DirectToGoal(ms, goal, result);
        return;
    }

    aas_reachability_t reach;
    int resultFlags = 0;
    int reachIndex = BotGetReachabilityToGoal(ms, goal, travelflags, &reach, &resultFlags);
    if (reachIndex <= 0)
    {
        BotMove_DirectToGoal(ms, goal, result);
        ms->lastreachnum = 0;
        ms->lastgoalareanum = goal->areanum;
        VectorCopy(ms->origin, ms->lastorigin);
        return;
    }

    BotMove_DispatchTravel(ms, &reach, result);
    result->flags |= resultFlags;

    ms->lastreachnum = reachIndex;
    ms->lastgoalareanum = goal->areanum;
    ms->lastareanum = ms->areanum;
    VectorCopy(ms->origin, ms->lastorigin);
}

int BotMoveInDirection(int movestate, const vec3_t dir, float speed, int type)
{
    (void)speed;

    bot_movestate_t *ms = BotMoveStateFromHandle(movestate);
    if (ms == NULL || dir == NULL)
    {
        return 0;
    }

    vec3_t normalized;
    if (VectorNormalizeTo(dir, normalized) <= 0.0f)
    {
        return 0;
    }

    if (type & MOVE_JUMP)
    {
        ms->jumpreach = 1;
    }
    if (type & MOVE_GRAPPLE)
    {
        ms->moveflags |= MFL_ACTIVEGRAPPLE;
    }

    (void)normalized;
    return 1;
}

void BotResetAvoidReach(int movestate)
{
    bot_movestate_t *ms = BotMoveStateFromHandle(movestate);
    if (ms == NULL)
    {
        return;
    }

    memset(ms->avoidreach, 0, sizeof(ms->avoidreach));
    memset(ms->avoidreachtimes, 0, sizeof(ms->avoidreachtimes));
    memset(ms->avoidreachtries, 0, sizeof(ms->avoidreachtries));
}

