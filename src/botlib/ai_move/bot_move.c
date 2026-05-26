#include "bot_move.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "botlib/aas/aas_local.h"
#include "botlib/common/l_libvar.h"
#include "botlib/common/l_log.h"
#include "botlib/common/l_memory.h"
#include "botlib/common/l_utils.h"
#include "botlib/ea/ea_local.h"
#include "q2bridge/bridge.h"
#include "q2bridge/botlib.h"
#include "q2bridge/bridge_config.h"

static bot_movestate_t *g_botMoveStates[MAX_CLIENTS + 1];

#define AVOIDREACH_TIME 6.0f
#define AVOIDREACH_TRIES 4

static float VectorNormalizeInline(vec3_t v);
static float VectorNormalizeTo(const vec3_t src, vec3_t dst);
static float BotMove_CRandom(void);
static int BotMove_FindAreaForPoint(const vec3_t origin);
static bool BotMove_AreaHasReachability(int areanum);
static int BotMove_FuzzyPointReachabilityArea(const vec3_t origin);
static void BotMove_GetClientBounds(int client, vec3_t mins, vec3_t maxs);
static void BotMove_CrouchPresenceBounds(vec3_t mins, vec3_t maxs);
static float BotMove_TravelTimeout(int traveltype);
static bool BotMove_AreaContentsHasLiquid(int contents);
static bool BotMove_AreaHasLadder(int areanum);
static int BotMove_AreaPresenceType(int areanum);
static bool BotMove_PointInLiquid(const vec3_t origin, int areanum);
static bool BotMove_OnGround(bot_movestate_t *ms);
static float BotMove_GapDistance(const vec3_t origin, const vec3_t hordir, int entnum);
static bool BotMove_CheckBarrierJump(bot_movestate_t *ms, const vec3_t dir, float speed);
static void BotMove_JumpRunStart(const bot_movestate_t *ms, const aas_reachability_t *reach, vec3_t runstart);
static int BotMove_SwimInDirection(bot_movestate_t *ms, const vec3_t dir, float speed, int type);
static int BotMove_WalkInDirection(bot_movestate_t *ms, const vec3_t dir, float speed, int type);
static void BotMove_AddToAvoidReach(bot_movestate_t *ms, int number, float avoidtime);
static bool BotMove_HasMovementSurface(const bot_movestate_t *ms);

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
    float yaw_diff = fabsf(AngleDelta(ideal_viewangles[YAW], ms->viewangles[YAW]));
    float pitch_diff = fabsf(AngleDelta(ideal_viewangles[PITCH], ms->viewangles[PITCH]));

    ms->grapplevisible_time = aasworld.time;

    if (start_dist < 5.0f && yaw_diff < 2.0f && pitch_diff < 2.0f)
    {
        EA_Command(ms->client, "%s", "hookon");
        EA_Attack(ms->client);
        ms->moveflags |= (MFL_ACTIVEGRAPPLE | MFL_GRAPPLEPULL);
        vec3_t grapple_delta;
        VectorSubtract(reach->end, ms->origin, grapple_delta);
        grapple_delta[2] = 0.0f;
        ms->lastgrappledist = sqrtf(VectorLengthSquared(grapple_delta));
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

typedef struct bot_move_mover_support_s
{
    const aas_entity_t *entity;
    int entnum;
    int modelnum;
    const bot_mover_catalogue_entry_t *catalogue;
} bot_move_mover_support_t;

static bool BotMove_DeveloperWarningsEnabled(void)
{
    if (LibVarValue("bot_developer", "0") != 0.0f)
    {
        return true;
    }

    return LibVarValue("developer", "0") != 0.0f;
}

static const char *BotMove_MoverKindName(bot_mover_kind_t kind)
{
    switch (kind)
    {
        case BOT_MOVER_KIND_FUNC_PLAT:
            return "func_plat";
        case BOT_MOVER_KIND_FUNC_BOB:
            return "func_bobbing";
        default:
            return NULL;
    }
}

/*
=============
BotMove_ReachMoverModelNum

Extract the brush model number encoded in mover reachabilities.
=============
*/
static int BotMove_ReachMoverModelNum(const aas_reachability_t *reach)
{
	if (reach == NULL)
	{
		return 0;
	}

	return reach->facenum & 0x0000FFFF;
}

/*
=============
BotMove_EntityMatchesModelNum

Match a live entity against a reconstructed brush model number.
=============
*/
static bool BotMove_EntityMatchesModelNum(int entnum, int modelnum)
{
	if (modelnum <= 0)
	{
		return false;
	}

	return AAS_ModelNumForEntity(entnum) == modelnum;
}

/*
=============
BotMove_MoverEntityForModel

Find the live AAS entity that owns a mover brush model.
=============
*/
static const aas_entity_t *BotMove_MoverEntityForModel(int modelnum)
{
	if (modelnum <= 0 || aasworld.entities == NULL || aasworld.maxEntities <= 0)
	{
		return NULL;
	}

	for (int entnum = 0; entnum < aasworld.maxEntities; ++entnum)
	{
		const aas_entity_t *entity = &aasworld.entities[entnum];
		if (entity == NULL || !entity->inuse || entity->solid != SOLID_BSP)
		{
			continue;
		}

		if (BotMove_EntityMatchesModelNum(entnum, modelnum))
		{
			return entity;
		}
	}

	return NULL;
}

/*
=============
BotMove_MoverBoundsForModel

Copy the current mover origin and model bounds.
=============
*/
static bool BotMove_MoverBoundsForModel(int modelnum, vec3_t origin, vec3_t mins, vec3_t maxs)
{
	const aas_entity_t *entity = BotMove_MoverEntityForModel(modelnum);
	if (entity == NULL)
	{
		BotLib_Print(PRT_MESSAGE, "no entity with model %d\n", modelnum);
		if (origin != NULL)
		{
			VectorClear(origin);
		}
		if (mins != NULL)
		{
			VectorClear(mins);
		}
		if (maxs != NULL)
		{
			VectorClear(maxs);
		}
		return false;
	}

	if (origin != NULL)
	{
		VectorCopy(entity->origin, origin);
	}
	if (mins != NULL)
	{
		VectorCopy(entity->mins, mins);
	}
	if (maxs != NULL)
	{
		VectorCopy(entity->maxs, maxs);
	}
	return true;
}

/*
=============
BotMove_OnMover

Check whether the bot is standing on the mover for this reachability.
=============
*/
static bool BotMove_OnMover(const bot_movestate_t *ms, const aas_reachability_t *reach)
{
	if (ms == NULL || reach == NULL)
	{
		return false;
	}

	int modelnum = BotMove_ReachMoverModelNum(reach);
	vec3_t mins;
	vec3_t maxs;
	vec3_t modelorigin;
	if (!BotMove_MoverBoundsForModel(modelnum, modelorigin, mins, maxs))
	{
		return false;
	}

	for (int axis = 0; axis < 2; ++axis)
	{
		if (ms->origin[axis] > modelorigin[axis] + maxs[axis] + 16.0f)
		{
			return false;
		}
		if (ms->origin[axis] < modelorigin[axis] + mins[axis] - 16.0f)
		{
			return false;
		}
	}

	vec3_t boxmins;
	vec3_t boxmaxs;
	vec3_t start;
	vec3_t end;
	VectorSet(boxmins, -16.0f, -16.0f, -8.0f);
	VectorSet(boxmaxs, 16.0f, 16.0f, 8.0f);
	VectorCopy(ms->origin, start);
	start[2] += 24.0f;
	VectorCopy(ms->origin, end);
	end[2] -= 48.0f;

	bsp_trace_t trace = Q2_Trace(start,
	                             boxmins,
	                             boxmaxs,
	                             end,
	                             ms->entitynum,
	                             CONTENTS_SOLID | CONTENTS_PLAYERCLIP);
	if (!trace.startsolid && !trace.allsolid &&
	    trace.ent >= 0 &&
	    BotMove_EntityMatchesModelNum(trace.ent, modelnum))
	{
		return true;
	}

	return false;
}

/*
=============
BotMove_MoverDown

Check whether a platform mover is below the reachability start point.
=============
*/
static bool BotMove_MoverDown(const aas_reachability_t *reach)
{
	int modelnum = BotMove_ReachMoverModelNum(reach);
	vec3_t origin;
	vec3_t maxs;
	if (!BotMove_MoverBoundsForModel(modelnum, origin, NULL, maxs))
	{
		return false;
	}

	return origin[2] + maxs[2] < reach->start[2];
}

/*
=============
BotMove_MoverBottomCenter

Calculate the bottom-position center used by elevator and func_bobbing travel.
=============
*/
static void BotMove_MoverBottomCenter(const aas_reachability_t *reach, vec3_t bottomcenter)
{
	if (bottomcenter == NULL)
	{
		return;
	}

	int modelnum = BotMove_ReachMoverModelNum(reach);
	vec3_t origin;
	vec3_t mins;
	vec3_t maxs;
	if (!BotMove_MoverBoundsForModel(modelnum, origin, mins, maxs))
	{
		VectorClear(bottomcenter);
		return;
	}

	vec3_t mids;
	VectorAdd(mins, maxs, mids);
	VectorMA(origin, 0.5f, mids, bottomcenter);
	bottomcenter[2] = reach->start[2];
}

/*
=============
BotMove_SignedShort

Sign-extend a 16-bit value packed into a reachability field.
=============
*/
static int BotMove_SignedShort(int value)
{
	value &= 0x0000FFFF;
	if (value > 0x00007FFF)
	{
		value |= ~0x0000FFFF;
	}

	return value;
}

/*
=============
BotMove_FuncBobStartEnd

Decode the moving start/end points for a func_bobbing reachability.
=============
*/
static bool BotMove_FuncBobStartEnd(const aas_reachability_t *reach, vec3_t start, vec3_t end, vec3_t origin)
{
	if (reach == NULL || start == NULL || end == NULL || origin == NULL)
	{
		return false;
	}

	int modelnum = BotMove_ReachMoverModelNum(reach);
	vec3_t mins;
	vec3_t maxs;
	if (!BotMove_MoverBoundsForModel(modelnum, origin, mins, maxs))
	{
		BotLib_Print(PRT_MESSAGE, "BotFuncBobStartEnd: no entity with model %d\n", modelnum);
		VectorClear(start);
		VectorClear(end);
		VectorClear(origin);
		return false;
	}

	vec3_t mid;
	VectorAdd(mins, maxs, mid);
	VectorScale(mid, 0.5f, mid);
	VectorCopy(mid, start);
	VectorCopy(mid, end);

	int spawnflags = reach->facenum >> 16;
	int num0 = BotMove_SignedShort(reach->edgenum >> 16);
	int num1 = BotMove_SignedShort(reach->edgenum);

	if ((spawnflags & 1) != 0)
	{
		start[0] = (float)num0;
		end[0] = (float)num1;
		origin[0] += mid[0];
		origin[1] = mid[1];
		origin[2] = mid[2];
	}
	else if ((spawnflags & 2) != 0)
	{
		start[1] = (float)num0;
		end[1] = (float)num1;
		origin[0] = mid[0];
		origin[1] += mid[1];
		origin[2] = mid[2];
	}
	else
	{
		start[2] = (float)num0;
		end[2] = (float)num1;
		origin[0] = mid[0];
		origin[1] = mid[1];
		origin[2] += mid[2];
	}

	return true;
}

static int BotMove_DetermineTravelTypeForMover(const bot_mover_catalogue_entry_t *entry)
{
    if (entry == NULL)
    {
        return TRAVEL_INVALID;
    }

    switch (entry->kind)
    {
        case BOT_MOVER_KIND_FUNC_PLAT:
            return TRAVEL_ELEVATOR;
        case BOT_MOVER_KIND_FUNC_BOB:
            return TRAVEL_FUNCBOB;
        default:
            return TRAVEL_INVALID;
    }
}

static unsigned int BotMove_DiagnosticForMover(bot_mover_kind_t kind, bool success)
{
    switch (kind)
    {
        case BOT_MOVER_KIND_FUNC_PLAT:
            return success ? BOT_MOVE_DIAG_FUNCPLAT_RELINKED : BOT_MOVE_DIAG_FUNCPLAT_FAILED;
        case BOT_MOVER_KIND_FUNC_BOB:
            return success ? BOT_MOVE_DIAG_FUNCBOB_RELINKED : BOT_MOVE_DIAG_FUNCBOB_FAILED;
        default:
            return BOT_MOVE_DIAG_NONE;
    }
}

static int BotMove_FindReachabilityForMover(int traveltype, int modelnum)
{
    if (aasworld.reachability == NULL || aasworld.numReachability <= 0)
    {
        return -1;
    }

    for (int index = 0; index < aasworld.numReachability; ++index)
    {
        const aas_reachability_t *candidate = &aasworld.reachability[index];
        if ((candidate->traveltype & TRAVELTYPE_MASK) != traveltype)
        {
            continue;
        }

        if ((candidate->facenum & 0x0000FFFF) != modelnum)
        {
            continue;
        }

        return index;
    }

    return -1;
}

static bool BotMove_IsUsingMoverReach(const bot_movestate_t *ms, int traveltype, int modelnum)
{
    if (ms == NULL || aasworld.reachability == NULL)
    {
        return false;
    }

    if (ms->lastreachnum < 0 || ms->lastreachnum >= aasworld.numReachability)
    {
        return false;
    }

    const aas_reachability_t *current = &aasworld.reachability[ms->lastreachnum];
    if ((current->traveltype & TRAVELTYPE_MASK) != traveltype)
    {
        return false;
    }

    return (current->facenum & 0x0000FFFF) == modelnum;
}

/*
=============
BotMove_FindSupportingMover

Find a linked brush-model mover under the current move state.
=============
*/
static bool BotMove_FindSupportingMover(bot_movestate_t *ms, bot_move_mover_support_t *support)
{
    if (ms == NULL || support == NULL)
    {
        return false;
    }

    if (!aasworld.loaded || !aasworld.entitiesValid || aasworld.entities == NULL)
    {
        return false;
    }

    memset(support, 0, sizeof(*support));

    int area = ms->areanum;
    if (area <= 0)
    {
        area = BotMove_FindAreaForPoint(ms->origin);
        if (area <= 0)
        {
            return false;
        }
        ms->areanum = area;
    }

    if (aasworld.areaEntityLists == NULL)
    {
        return false;
    }

    size_t listCount = aasworld.areaEntityListCount;
    if (listCount == 0U || (size_t)area >= listCount)
    {
        return false;
    }

    const aas_link_t *link = aasworld.areaEntityLists[area];
    const float lateralTolerance = 1.0f;
    const float belowTolerance = 64.0f;
    const float aboveTolerance = 32.0f;

    for (; link != NULL; link = link->next_ent)
    {
        int entnum = link->entnum;
        if (entnum < 0 || entnum >= aasworld.maxEntities)
        {
            continue;
        }

        const aas_entity_t *entity = &aasworld.entities[entnum];
        if (!entity->inuse || entity->solid != SOLID_BSP || entity->modelindex <= 0)
        {
            continue;
        }

        int modelnum = AAS_ModelNumForEntity(entnum);
        if (modelnum <= 0)
        {
            continue;
        }

		const bot_mover_catalogue_entry_t *catalogue = BotMove_MoverCatalogueFindByModel(modelnum);
		if (catalogue == NULL)
		{
			continue;
		}

        vec3_t absmins;
        vec3_t absmaxs;
        VectorAdd(entity->origin, entity->mins, absmins);
        VectorAdd(entity->origin, entity->maxs, absmaxs);

        if (ms->origin[0] + lateralTolerance < absmins[0] ||
            ms->origin[0] - lateralTolerance > absmaxs[0] ||
            ms->origin[1] + lateralTolerance < absmins[1] ||
            ms->origin[1] - lateralTolerance > absmaxs[1])
        {
            continue;
        }

        if (ms->origin[2] + belowTolerance < absmaxs[2])
        {
            continue;
        }

        if (ms->origin[2] > absmaxs[2] + aboveTolerance)
        {
            continue;
        }

        support->entity = entity;
        support->entnum = entnum;
        support->modelnum = modelnum;
        support->catalogue = catalogue;
        return true;
    }

    return false;
}

static bool BotMove_HandleGroundMover(bot_movestate_t *ms, bot_moveresult_t *result)
{
    if (ms == NULL || result == NULL)
    {
        return false;
    }

    bot_move_mover_support_t support;
    if (!BotMove_FindSupportingMover(ms, &support))
    {
        return false;
    }

    const bot_mover_catalogue_entry_t *catalogue = support.catalogue;
    if (catalogue == NULL)
    {
        return false;
    }

    int traveltype = BotMove_DetermineTravelTypeForMover(catalogue);
    if (traveltype == TRAVEL_INVALID)
    {
        return false;
    }

    if (catalogue->kind == BOT_MOVER_KIND_FUNC_PLAT)
    {
        result->flags |= MOVERESULT_ONTOPOF_ELEVATOR;
    }
    else if (catalogue->kind == BOT_MOVER_KIND_FUNC_BOB)
    {
        result->flags |= MOVERESULT_ONTOPOF_FUNCBOB;
    }

    if (BotMove_IsUsingMoverReach(ms, traveltype, support.modelnum))
    {
        return false;
    }

    int reachnum = BotMove_FindReachabilityForMover(traveltype, support.modelnum);
    if (reachnum >= 0)
    {
        const aas_reachability_t *reach = &aasworld.reachability[reachnum];
        ms->lastreachnum = reachnum;
        ms->reachability_time = aasworld.time + BotMove_TravelTimeout(traveltype);
        ms->reachareanum = reach->areanum;
        result->diagnostics |= BotMove_DiagnosticForMover(catalogue->kind, true);
        BotLib_Print(PRT_MESSAGE,
                     "client %d: relinking brush model ent %d\n",
                     ms->client,
                     support.entnum);
        return false;
    }

    if (BotMove_DeveloperWarningsEnabled())
    {
        const char *mover_name = BotMove_MoverKindName(catalogue->kind);
        if (mover_name != NULL)
        {
            BotLib_Print(PRT_MESSAGE,
                         "client %d: on %s without reachability\n",
                         ms->client,
                         mover_name);
        }
    }

    result->blocked = 1;
    result->blockentity = support.entnum;
    result->flags |= MOVERESULT_ONTOPOFOBSTACLE;
    result->failure = 1;
    result->diagnostics |= BotMove_DiagnosticForMover(catalogue->kind, false);
    return true;
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

/*
=============
BotMove_CRandom

Return a Quake-style random float in the range [-1, 1).
=============
*/
static float BotMove_CRandom(void)
{
	return ((float)rand() / ((float)RAND_MAX + 1.0f)) * 2.0f - 1.0f;
}

/*
=============
BotMove_LibVarValue

Read a movement tuning variable with a retail fallback.
=============
*/
static float BotMove_LibVarValue(const libvar_t *var, float fallback)
{
	if (var == NULL)
	{
		return fallback;
	}

	return var->value;
}

/*
=============
BotMove_AreaContentsHasLiquid

Check reconstructed AAS area liquid contents.
=============
*/
static bool BotMove_AreaContentsHasLiquid(int contents)
{
	return (contents & (AAS_AREACONTENTS_WATER |
	                    AAS_AREACONTENTS_LAVA |
	                    AAS_AREACONTENTS_SLIME)) != 0;
}

/*
=============
BotMove_AreaHasLadder

Check ladder state from AAS flags or the Quake II contents bit.
=============
*/
static bool BotMove_AreaHasLadder(int areanum)
{
	return AAS_AreaHasLadder(areanum);
}

/*
=============
BotMove_AreaPresenceType

Read the reconstructed AAS presence mask for an area.
=============
*/
static int BotMove_AreaPresenceType(int areanum)
{
	int presencetype = AAS_AreaPresenceType(areanum);
	if (presencetype == 0)
	{
		return PRESENCE_NORMAL;
	}

	return presencetype;
}

/*
=============
BotMove_PointInLiquid

Resolve swimming state from point contents first, then AAS area contents.
=============
*/
static bool BotMove_PointInLiquid(const vec3_t origin, int areanum)
{
	if (origin != NULL && (Q2_PointContents((vec_t *)origin) & MASK_WATER) != 0)
	{
		return true;
	}

	if (aasworld.areasettings == NULL ||
	    areanum <= 0 ||
	    areanum >= aasworld.numAreaSettings)
	{
		return false;
	}

	return BotMove_AreaContentsHasLiquid(aasworld.areasettings[areanum].contents);
}

/*
=============
BotMove_ClientBoundsForPresence

Provide a conservative Quake II client box when entity bounds are unavailable.
=============
*/
static void BotMove_ClientBoundsForPresence(const bot_movestate_t *ms, vec3_t mins, vec3_t maxs)
{
	if (mins == NULL || maxs == NULL)
	{
		return;
	}

	if (ms != NULL)
	{
		BotMove_GetClientBounds(ms->entitynum, mins, maxs);
		if (VectorLengthSquared(mins) > 0.0f || VectorLengthSquared(maxs) > 0.0f)
		{
			return;
		}
	}

	VectorSet(mins, -16.0f, -16.0f, -24.0f);
	if (ms != NULL && ms->presencetype == PRESENCE_CROUCH)
	{
		VectorSet(maxs, 16.0f, 16.0f, 16.0f);
	}
	else
	{
		VectorSet(maxs, 16.0f, 16.0f, 32.0f);
	}
}

/*
=============
BotMove_OnGround

Trace slightly below the bot to refresh the on-ground movement flag.
=============
*/
static bool BotMove_OnGround(bot_movestate_t *ms)
{
	if (ms == NULL)
	{
		return false;
	}

	if (aasworld.areasettings != NULL &&
	    ms->areanum > 0 &&
	    ms->areanum < aasworld.numAreaSettings &&
	    (aasworld.areasettings[ms->areanum].areaflags & AAS_AREA_GROUNDED) != 0)
	{
		return true;
	}

	vec3_t mins;
	vec3_t maxs;
	vec3_t end;
	BotMove_ClientBoundsForPresence(ms, mins, maxs);
	VectorCopy(ms->origin, end);
	end[2] -= 3.0f;

	bsp_trace_t trace = Q2_Trace(ms->origin,
	                             mins,
	                             maxs,
	                             end,
	                             ms->entitynum,
	                             CONTENTS_SOLID | CONTENTS_PLAYERCLIP);
	return !trace.startsolid && trace.fraction < 1.0f;
}

/*
=============
BotMove_HasMovementSurface

Check whether normal reachability selection may run this frame.
=============
*/
static bool BotMove_HasMovementSurface(const bot_movestate_t *ms)
{
	if (ms == NULL)
	{
		return false;
	}

	return (ms->moveflags & (MFL_ONGROUND | MFL_SWIMMING | MFL_AGAINSTLADDER)) != 0;
}

static int BotMove_FindAreaForPoint(const vec3_t origin)
{
	return AAS_PointAreaNum(origin);
}

/*
=============
BotMove_AreaHasReachability

Check whether an area has reachability data attached.
=============
*/
static bool BotMove_AreaHasReachability(int areanum)
{
	if (areanum <= 0)
	{
		return false;
	}

	if (aasworld.areasettings != NULL &&
	    areanum < aasworld.numAreaSettings)
	{
		if (AAS_AreaReachability(areanum) > 0)
		{
			return true;
		}
	}

	if (aasworld.reachability == NULL || aasworld.numReachability <= 0)
	{
		return false;
	}

	if (aasworld.reachabilityFromArea != NULL)
	{
		for (int index = 0; index < aasworld.numReachability; ++index)
		{
			if (aasworld.reachabilityFromArea[index] == areanum)
			{
				return true;
			}
		}

		return false;
	}

	for (int index = 0; index < aasworld.numReachability; ++index)
	{
		if (aasworld.reachability[index].facenum == areanum)
		{
			return true;
		}
	}

	return false;
}

/*
=============
BotMove_FuzzyPointReachabilityArea

Resolve an area near the origin using small offsets when needed.
=============
*/
static int BotMove_FuzzyPointReachabilityArea(const vec3_t origin)
{
	if (origin == NULL)
	{
		return 0;
	}

	int area = BotMove_FindAreaForPoint(origin);
	int firstArea = area;
	if (area > 0)
	{
		if (BotMove_AreaHasReachability(area))
		{
			return area;
		}
	}

	int bestArea = 0;
	float bestDist = FLT_MAX;

	for (int z = 1; z >= -1; --z)
	{
		for (int x = 1; x >= -1; --x)
		{
			for (int y = 1; y >= -1; --y)
			{
				vec3_t offset;
				VectorCopy(origin, offset);
				offset[0] += (float)(x * 8);
				offset[1] += (float)(y * 8);
				offset[2] += (float)(z * 12);

				area = BotMove_FindAreaForPoint(offset);
				if (area <= 0)
				{
					continue;
				}

				if (firstArea == 0)
				{
					firstArea = area;
				}

				if (!BotMove_AreaHasReachability(area))
				{
					continue;
				}

				vec3_t delta;
				VectorSubtract(offset, origin, delta);
				float dist = sqrtf(VectorLengthSquared(delta));
				if (dist < bestDist)
				{
					bestDist = dist;
					bestArea = area;
				}
			}
		}

		if (bestArea > 0)
		{
			return bestArea;
		}
	}

	return firstArea;
}

/*
=============
BotMove_GetClientBounds

Gather the client bounding box for traces.
=============
*/
static void BotMove_GetClientBounds(int client, vec3_t mins, vec3_t maxs)
{
	if (mins == NULL || maxs == NULL)
	{
		return;
	}

	VectorClear(mins);
	VectorClear(maxs);

	if (aasworld.entities == NULL || aasworld.maxEntities <= 0)
	{
		return;
	}

	if (client < 0 || client >= aasworld.maxEntities)
	{
		return;
	}

	const aas_entity_t *entity = &aasworld.entities[client];
	if (entity == NULL || !entity->inuse)
	{
		return;
	}

	VectorCopy(entity->mins, mins);
	VectorCopy(entity->maxs, maxs);
}

/*
=============
BotMove_CrouchPresenceBounds

Return the retail crouch presence box used by reachability area probes.
=============
*/
static void BotMove_CrouchPresenceBounds(vec3_t mins, vec3_t maxs)
{
	if (mins == NULL || maxs == NULL)
	{
		return;
	}

	VectorSet(mins, -15.0f, -15.0f, -24.0f);
	VectorSet(maxs, 15.0f, 15.0f, 8.0f);
}

static int BotMove_TravelFlagsForType(int traveltype)
{
	int flags = AAS_TravelFlagForType(traveltype);
	if (flags == 0)
	{
		flags = TFL_INVALID;
	}

	return flags;
}

static bool BotMove_TravelAllowed(int traveltype, int travelflags)
{
	int flags = BotMove_TravelFlagsForType(traveltype);
	int allowed = (travelflags != 0) ? travelflags : TFL_DEFAULT;
	if (allowed == 0)
	{
		return true;
	}

	return (flags & ~allowed) == 0;
}

/*
=============
BotMove_AreaContentsTravelFlags

Translate AAS area contents to routing travel masks.
=============
*/
static int BotMove_AreaContentsTravelFlags(int areanum)
{
	return AAS_AreaContentsTravelFlags(areanum);
}

/*
=============
BotMove_AreaTravelAllowed

Check destination area contents against allowed travel flags.
=============
*/
static bool BotMove_AreaTravelAllowed(int areanum, int travelflags)
{
	return AAS_AreaTravelAllowed(areanum, travelflags);
}

/*
=============
BotMove_AreaDoNotEnter

Check whether the AAS area carries the do-not-enter content flag.
=============
*/
static bool BotMove_AreaDoNotEnter(int areanum)
{
	return AAS_AreaDoNotEnter(areanum);
}

/*
=============
BotMove_TravelTimeout

Return the retained-reach timeout for a travel type.
=============
*/
static float BotMove_TravelTimeout(int traveltype)
{
	switch (traveltype)
	{
		case TRAVEL_WALK:
		case TRAVEL_CROUCH:
		case TRAVEL_BARRIERJUMP:
		case TRAVEL_JUMP:
		case TRAVEL_WALKOFFLEDGE:
		case TRAVEL_SWIM:
		case TRAVEL_WATERJUMP:
		case TRAVEL_TELEPORT:
			return 5.0f;
		case TRAVEL_LADDER:
		case TRAVEL_ROCKETJUMP:
			return 6.0f;
		case TRAVEL_ELEVATOR:
		case TRAVEL_JUMPPAD:
		case TRAVEL_FUNCBOB:
			return 10.0f;
		case TRAVEL_BFGJUMP:
		case TRAVEL_GRAPPLEHOOK:
			return 8.0f;
		default:
			return 8.0f;
	}
}

/*
=============
BotMove_AddToTarget

Accumulate distance towards the movement lookahead target.
=============
*/
static bool BotMove_AddToTarget(const vec3_t start,
								const vec3_t end,
								float maxdist,
								float *dist,
								vec3_t target)
{
	vec3_t dir;
	float curdist;

	if (dist == NULL || target == NULL)
	{
		return false;
	}

	VectorSubtract(end, start, dir);
	curdist = VectorNormalizeInline(dir);
	if (*dist + curdist < maxdist)
	{
		VectorCopy(end, target);
		*dist += curdist;
		return false;
	}

	target[0] = start[0] + (maxdist - *dist) * dir[0];
	target[1] = start[1] + (maxdist - *dist) * dir[1];
	target[2] = start[2] + (maxdist - *dist) * dir[2];
	*dist = maxdist;
	return true;
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

	if (dir != NULL && VectorLengthSquared(dir) > 0.0001f)
	{
		Vector2Angles(dir, result->ideal_viewangles);
	}
}

/*
=============
BotMove_CheckBlocked

Detect an entity blocking a short movement probe.
=============
*/
static void BotMove_CheckBlocked(bot_movestate_t *ms,
                                 const vec3_t dir,
                                 bool checkbottom,
                                 bot_moveresult_t *result)
{
	if (ms == NULL || dir == NULL || result == NULL)
	{
		return;
	}

	vec3_t mins;
	vec3_t maxs;
	vec3_t end;
	BotMove_ClientBoundsForPresence(ms, mins, maxs);

	VectorCopy(ms->origin, end);
	end[0] += dir[0] * 3.0f;
	end[1] += dir[1] * 3.0f;
	end[2] += dir[2] * 3.0f;

	bsp_trace_t trace = Q2_Trace(ms->origin,
	                             mins,
	                             maxs,
	                             end,
	                             ms->entitynum,
	                             CONTENTS_SOLID | CONTENTS_PLAYERCLIP | CONTENTS_MONSTER);
	if (!trace.startsolid && trace.ent > 0)
	{
		result->blocked = 1;
		result->blockentity = trace.ent;
		return;
	}

	if (!checkbottom || BotMove_AreaHasReachability(ms->areanum))
	{
		return;
	}

	VectorCopy(ms->origin, end);
	end[2] -= 3.0f;
	trace = Q2_Trace(ms->origin,
	                 mins,
	                 maxs,
	                 end,
	                 ms->entitynum,
	                 CONTENTS_SOLID | CONTENTS_PLAYERCLIP);
	if (!trace.startsolid && trace.ent > 0)
	{
		result->blocked = 1;
		result->blockentity = trace.ent;
		result->flags |= MOVERESULT_ONTOPOFOBSTACLE;
	}
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
	BotMove_CheckBlocked(ms, dir, true, result);

	vec3_t delta;
	VectorSubtract(goal->origin, ms->origin, delta);
	float dist = sqrtf(VectorLengthSquared(delta));
	if (!swimming)
	{
		delta[2] = 0.0f;
		dist = sqrtf(VectorLengthSquared(delta));
	}
	if (dist > 100.0f)
	{
		dist = 100.0f;
	}
	float speed = 4.0f * dist;
	if (speed < 10.0f)
	{
		speed = 0.0f;
	}
	EA_Move(ms->client, dir, speed);

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

/*
=============
BotMove_ShouldAvoidReach

Check whether a reachability should be avoided while updating last-avoid tracking.
=============
*/
static bool BotMove_ShouldAvoidReach(bot_movestate_t *ms, int reachnum)
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
			ms->lastavoidreach = reachnum;
			ms->lastavoidreachtime = ms->avoidreachtimes[i];
			ms->lastavoidreachtries = ms->avoidreachtries[i];
			return ms->avoidreachtries[i] > AVOIDREACH_TRIES;
		}
	}

	return false;
}

/*
=============
BotMove_AddToAvoidReach

Track recently selected reachabilities using the retail retry counter.
=============
*/
static void BotMove_AddToAvoidReach(bot_movestate_t *ms, int number, float avoidtime)
{
	if (ms == NULL || number <= 0)
	{
		return;
	}

	float now = aasworld.time;
	for (int i = 0; i < MAX_AVOIDREACH; ++i)
	{
		if (ms->avoidreach[i] == number)
		{
			if (ms->avoidreachtimes[i] > now)
			{
				ms->avoidreachtries[i]++;
			}
			else
			{
				ms->avoidreachtries[i] = 1;
			}
			ms->avoidreachtimes[i] = now + avoidtime;
			return;
		}
	}

	for (int i = 0; i < MAX_AVOIDREACH; ++i)
	{
		if (ms->avoidreachtimes[i] <= now)
		{
			ms->avoidreach[i] = number;
			ms->avoidreachtimes[i] = now + avoidtime;
			ms->avoidreachtries[i] = 1;
			return;
		}
	}
}

/*
=============
BotMove_VectorDistanceSquared

Squared point distance helper used by avoid-spot checks.
=============
*/
static float BotMove_VectorDistanceSquared(const vec3_t a, const vec3_t b)
{
	vec3_t delta;
	VectorSubtract(a, b, delta);
	return VectorLengthSquared(delta);
}

/*
=============
BotMove_DistanceFromLineSquared

Compute squared distance from a point to a clamped segment.
=============
*/
static float BotMove_DistanceFromLineSquared(const vec3_t point, const vec3_t start, const vec3_t end)
{
	vec3_t segment;
	VectorSubtract(end, start, segment);
	float lengthSquared = VectorLengthSquared(segment);
	if (lengthSquared <= 0.0001f)
	{
		return BotMove_VectorDistanceSquared(point, start);
	}

	vec3_t toPoint;
	VectorSubtract(point, start, toPoint);
	float t = DotProduct(toPoint, segment) / lengthSquared;
	if (t < 0.0f)
	{
		t = 0.0f;
	}
	else if (t > 1.0f)
	{
		t = 1.0f;
	}

	vec3_t projected;
	projected[0] = start[0] + t * segment[0];
	projected[1] = start[1] + t * segment[1];
	projected[2] = start[2] + t * segment[2];
	return BotMove_VectorDistanceSquared(point, projected);
}

/*
=============
BotMove_AvoidSpots

Return the strongest avoid-spot type blocking a reachability.
=============
*/
static int BotMove_AvoidSpots(const vec3_t origin,
                              const aas_reachability_t *reach,
                              const bot_avoidspot_t *avoidspots,
                              int numavoidspots)
{
	if (origin == NULL || reach == NULL || avoidspots == NULL || numavoidspots <= 0)
	{
		return AVOID_CLEAR;
	}

	bool checkbetween = true;
	switch (reach->traveltype & TRAVELTYPE_MASK)
	{
		case TRAVEL_WALKOFFLEDGE:
		case TRAVEL_JUMP:
		case TRAVEL_TELEPORT:
		case TRAVEL_ELEVATOR:
		case TRAVEL_GRAPPLEHOOK:
		case TRAVEL_ROCKETJUMP:
		case TRAVEL_BFGJUMP:
		case TRAVEL_JUMPPAD:
		case TRAVEL_FUNCBOB:
			checkbetween = false;
			break;
		default:
			checkbetween = true;
			break;
	}

	int type = AVOID_CLEAR;
	for (int i = 0; i < numavoidspots; ++i)
	{
		const bot_avoidspot_t *spot = &avoidspots[i];
		float squaredRadius = spot->radius * spot->radius;
		float squaredDist = BotMove_DistanceFromLineSquared(spot->origin, origin, reach->start);
		if (squaredDist < squaredRadius &&
		    BotMove_VectorDistanceSquared(spot->origin, origin) > squaredDist)
		{
			type = spot->type;
		}
		else if (checkbetween)
		{
			squaredDist = BotMove_DistanceFromLineSquared(spot->origin, reach->start, reach->end);
			if (squaredDist < squaredRadius &&
			    BotMove_VectorDistanceSquared(spot->origin, reach->start) > squaredDist)
			{
				type = spot->type;
			}
		}
		else
		{
			squaredDist = BotMove_VectorDistanceSquared(spot->origin, reach->end);
			if (squaredDist < squaredRadius &&
			    BotMove_VectorDistanceSquared(spot->origin, reach->start) > squaredDist)
			{
				type = spot->type;
			}
		}

		if (type == AVOID_ALWAYS)
		{
			return type;
		}
	}

	return type;
}

/*
=============
BotMove_FallbackAreaTravelTime

Forward Dijkstra fallback used when reconstructed route caches are unavailable.
=============
*/
static int BotMove_FallbackAreaTravelTime(int startArea, int goalArea, int travelflags)
{
	if (startArea <= 0 || goalArea <= 0 || startArea >= aasworld.numAreaSettings ||
	    goalArea >= aasworld.numAreaSettings || aasworld.areasettings == NULL ||
	    aasworld.reachability == NULL)
	{
		return 0;
	}

	if (startArea == goalArea)
	{
		return 1;
	}

	int areaCount = aasworld.numAreaSettings;
	int *best = (int *)malloc((size_t)areaCount * sizeof(int));
	bool *visited = (bool *)calloc((size_t)areaCount, sizeof(bool));
	if (best == NULL || visited == NULL)
	{
		free(best);
		free(visited);
		return 0;
	}

	for (int i = 0; i < areaCount; ++i)
	{
		best[i] = INT_MAX;
	}
	best[startArea] = 0;

	while (true)
	{
		int current = 0;
		int currentCost = INT_MAX;
		for (int area = 1; area < areaCount; ++area)
		{
			if (!visited[area] && best[area] < currentCost)
			{
				current = area;
				currentCost = best[area];
			}
		}

		if (current == 0 || current == goalArea)
		{
			break;
		}

		visited[current] = true;
		const aas_areasettings_t *settings = &aasworld.areasettings[current];
		for (int offset = 0; offset < settings->numreachableareas; ++offset)
		{
			int reachIndex = settings->firstreachablearea + offset;
			if (reachIndex <= 0 || reachIndex >= aasworld.numReachability)
			{
				continue;
			}

			const aas_reachability_t *reach = &aasworld.reachability[reachIndex];
			int nextArea = reach->areanum;
			if (nextArea <= 0 || nextArea >= areaCount)
			{
				continue;
			}
			if (!BotMove_TravelAllowed(reach->traveltype, travelflags) ||
			    !BotMove_AreaTravelAllowed(nextArea, travelflags))
			{
				continue;
			}

			int step = (reach->traveltime > 0) ? reach->traveltime : 1;
			if (currentCost <= INT_MAX - step && currentCost + step < best[nextArea])
			{
				best[nextArea] = currentCost + step;
			}
		}
	}

	int result = (best[goalArea] != INT_MAX) ? best[goalArea] : 0;
	free(best);
	free(visited);
	return result;
}

/*
=============
BotMove_AreaTravelTimeToGoal

Use reconstructed routing caches when present, otherwise fall back locally.
=============
*/
static int BotMove_AreaTravelTimeToGoal(int areanum,
                                        vec3_t origin,
                                        int goalareanum,
                                        int travelflags)
{
	if (aasworld.reversedReachability != NULL &&
	    aasworld.reachabilityFromArea != NULL &&
	    aasworld.numAreas > 0)
	{
		int routed = AAS_AreaTravelTimeToGoalArea(areanum, origin, goalareanum, travelflags);
		if (routed > 0)
		{
			return routed;
		}
	}

	return BotMove_FallbackAreaTravelTime(areanum, goalareanum, travelflags);
}

/*
=============
BotGetReachabilityToGoal

Choose the best first reachability using retail travel-time and avoidance rules.
=============
*/
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

	int routeFlags = (travelflags != 0) ? travelflags : TFL_DEFAULT;
	int moveTravelFlags = routeFlags;
	if (BotMove_AreaDoNotEnter(ms->areanum) || BotMove_AreaDoNotEnter(goalArea))
	{
		routeFlags |= TFL_DONOTENTER;
		moveTravelFlags |= TFL_DONOTENTER;
	}

	const aas_areasettings_t *settings = &aasworld.areasettings[ms->areanum];
	int bestTime = 0;
	int bestReachNum = 0;
	aas_reachability_t bestReach;
	memset(&bestReach, 0, sizeof(bestReach));

	for (int offset = 0; offset < settings->numreachableareas; ++offset)
	{
		int reachnum = settings->firstreachablearea + offset;
		if (reachnum <= 0 || reachnum >= aasworld.numReachability)
		{
			continue;
		}

		if (BotMove_ShouldAvoidReach(ms, reachnum))
		{
			continue;
		}

		aas_reachability_t reach = aasworld.reachability[reachnum];
		if (ms->lastgoalareanum == goalArea && reach.areanum == ms->lastareanum)
		{
			continue;
		}

		if (!BotMove_TravelAllowed(reach.traveltype, moveTravelFlags) ||
		    !BotMove_AreaTravelAllowed(reach.areanum, moveTravelFlags))
		{
			continue;
		}

		int travelTime = BotMove_AreaTravelTimeToGoal(reach.areanum,
		                                             reach.end,
		                                             goalArea,
		                                             routeFlags);
		if (travelTime <= 0)
		{
			continue;
		}

		if (BotMove_AvoidSpots(ms->origin, &reach, ms->avoidspots, ms->numavoidspots) != AVOID_CLEAR)
		{
			if (resultFlags != NULL)
			{
				*resultFlags |= MOVERESULT_BLOCKEDBYAVOIDSPOT;
			}
			continue;
		}

		travelTime += (reach.traveltime > 0) ? reach.traveltime : 1;
		if (bestTime == 0 || travelTime < bestTime)
		{
			bestTime = travelTime;
			bestReachNum = reachnum;
			bestReach = reach;
		}
	}

	if (bestReachNum <= 0)
	{
		return 0;
	}

	*out = bestReach;
	return bestReachNum;
}

/*
=============
BotMove_GetReachabilityFromPoint

Build the temporary route context used by view and prediction helpers.
=============
*/
static int BotMove_GetReachabilityFromPoint(const bot_movestate_t *base,
                                            const vec3_t origin,
                                            int areanum,
                                            int lastgoalareanum,
                                            int lastareanum,
                                            const bot_goal_t *goal,
                                            int travelflags,
                                            aas_reachability_t *out)
{
	if (origin == NULL || goal == NULL || out == NULL)
	{
		return 0;
	}

	bot_movestate_t temp;
	if (base != NULL)
	{
		temp = *base;
	}
	else
	{
		memset(&temp, 0, sizeof(temp));
	}

	VectorCopy(origin, temp.origin);
	temp.areanum = areanum;
	temp.lastgoalareanum = lastgoalareanum;
	temp.lastareanum = lastareanum;
	temp.numavoidspots = 0;

	return BotGetReachabilityToGoal(&temp, goal, travelflags, out, NULL);
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
	if (ms == NULL || reach == NULL || result == NULL)
	{
		return;
	}

	BotClearMoveResult(result);
	result->traveltype = TRAVEL_WALK;

	vec3_t hordir;
	VectorSubtract(reach->start, ms->origin, hordir);
	hordir[2] = 0.0f;
	float dist = VectorNormalizeInline(hordir);

	BotMove_CheckBlocked(ms, hordir, true, result);

	if (dist < 10.0f)
	{
		VectorSubtract(reach->end, ms->origin, hordir);
		hordir[2] = 0.0f;
		dist = VectorNormalizeInline(hordir);
	}

	if ((BotMove_AreaPresenceType(reach->areanum) & PRESENCE_NORMAL) == 0 && dist < 20.0f)
	{
		EA_Crouch(ms->client);
	}

	float gapdist = BotMove_GapDistance(ms->origin, hordir, ms->entitynum);
	float speed;
	if ((ms->moveflags & MFL_WALK) != 0)
	{
		if (gapdist > 0.0f)
		{
			speed = 200.0f - (180.0f - gapdist);
		}
		else
		{
			speed = 200.0f;
		}
	}
	else
	{
		if (gapdist > 0.0f)
		{
			speed = 400.0f - (360.0f - 2.0f * gapdist);
		}
		else
		{
			speed = 400.0f;
		}
	}

	VectorCopy(hordir, result->movedir);
	result->flags |= MOVERESULT_MOVEMENTVIEW;
	BotMove_SetMovementView(hordir, result, false);
	EA_Move(ms->client, hordir, speed);
}

/*
=============
BotMove_TravelCrouch

Travel to the reachability end while crouching.
=============
*/
static void BotMove_TravelCrouch(bot_movestate_t *ms,
                                 const aas_reachability_t *reach,
                                 bot_moveresult_t *result)
{
	if (ms == NULL || reach == NULL || result == NULL)
	{
		return;
	}

	vec3_t dir;
	VectorSubtract(reach->end, ms->origin, dir);
	dir[2] = 0.0f;
	VectorNormalizeInline(dir);

	BotMove_PrepareResult(result, dir, TRAVEL_CROUCH, false);
	BotMove_CheckBlocked(ms, dir, true, result);
	EA_Crouch(ms->client);
	EA_Move(ms->client, dir, 400.0f);
}

/*
=============
BotMove_TravelBarrierJump

Approach the barrier jump start, then trigger the jump at close range.
=============
*/
static void BotMove_TravelBarrierJump(bot_movestate_t *ms,
                                      const aas_reachability_t *reach,
                                      bot_moveresult_t *result)
{
	if (ms == NULL || reach == NULL || result == NULL)
	{
		return;
	}

	BotClearMoveResult(result);
	result->traveltype = TRAVEL_BARRIERJUMP;

	vec3_t hordir;
	VectorSubtract(reach->start, ms->origin, hordir);
	hordir[2] = 0.0f;
	float dist = VectorNormalizeInline(hordir);

	BotMove_CheckBlocked(ms, hordir, true, result);
	if (dist < 9.0f)
	{
		EA_Jump(ms->client);
		ms->moveflags |= MFL_BARRIERJUMP;
		ms->jumpreach = 1;
	}
	else
	{
		if (dist > 60.0f)
		{
			dist = 60.0f;
		}
		EA_Move(ms->client, hordir, 6.0f * dist);
	}

	VectorCopy(hordir, result->movedir);
	result->flags |= MOVERESULT_MOVEMENTVIEW;
	BotMove_SetMovementView(hordir, result, false);
}

/*
=============
BotMove_TravelLadder

Face the ladder and submit the retail move-forward action.
=============
*/
static void BotMove_TravelLadder(bot_movestate_t *ms,
                                 const aas_reachability_t *reach,
                                 bot_moveresult_t *result)
{
	if (ms == NULL || reach == NULL || result == NULL)
	{
		return;
	}

	BotClearMoveResult(result);
	result->traveltype = TRAVEL_LADDER;

	vec3_t dir;
	VectorSubtract(reach->end, ms->origin, dir);
	VectorNormalizeInline(dir);

	vec3_t viewdir;
	viewdir[0] = dir[0];
	viewdir[1] = dir[1];
	viewdir[2] = 3.0f * dir[2];
	Vector2Angles(viewdir, result->ideal_viewangles);

	vec3_t origin;
	VectorClear(origin);
	EA_Move(ms->client, origin, 0.0f);
	EA_MoveForward(ms->client);

	VectorCopy(dir, result->movedir);
	result->flags |= MOVERESULT_MOVEMENTVIEW;
}

/*
=============
BotMove_TravelWalkOffLedge

Walk toward the ledge endpoint using the Gladiator walk-off speed rules.
=============
*/
static void BotMove_TravelWalkOffLedge(bot_movestate_t *ms,
                                       const aas_reachability_t *reach,
                                       bot_moveresult_t *result)
{
	if (ms == NULL || reach == NULL || result == NULL)
	{
		return;
	}

	BotClearMoveResult(result);
	result->traveltype = TRAVEL_WALKOFFLEDGE;

	vec3_t dir;
	VectorSubtract(reach->end, ms->origin, dir);
	VectorNormalizeInline(dir);
	BotMove_CheckBlocked(ms, dir, true, result);

	vec3_t reachdir;
	VectorSubtract(reach->end, reach->start, reachdir);
	reachdir[2] = 0.0f;
	float reachhordist = sqrtf(VectorLengthSquared(reachdir));

	vec3_t hordir;
	VectorSubtract(reach->end, ms->origin, hordir);
	hordir[2] = 0.0f;
	float dist = VectorNormalizeInline(hordir);

	float speed = 400.0f;
	if (dist < 60.0f)
	{
		if (reachhordist >= 15.0f)
		{
			if (!AAS_HorizontalVelocityForJump(0.0f, reach->start, reach->end, &speed))
			{
				speed = 200.0f;
			}
		}
		else if (dist > 0.0f && dist < 150.0f)
		{
			speed = 380.0f - (300.0f - 2.0f * dist);
		}
	}

	BotMove_CheckBlocked(ms, hordir, true, result);
	VectorCopy(hordir, result->movedir);
	result->flags |= MOVERESULT_MOVEMENTVIEW;
	BotMove_SetMovementView(hordir, result, false);
	EA_Move(ms->client, hordir, speed);
}

/*
=============
BotMove_JumpRunStart

Find the run-up point for a jump reachability.
=============
*/
static void BotMove_JumpRunStart(const bot_movestate_t *ms, const aas_reachability_t *reach, vec3_t runstart)
{
	if (reach == NULL || runstart == NULL)
	{
		return;
	}

	vec3_t aasrunstart;
	AAS_JumpReachRunStart(reach, aasrunstart);

	vec3_t hordir;
	hordir[0] = aasrunstart[0] - reach->start[0];
	hordir[1] = aasrunstart[1] - reach->start[1];
	hordir[2] = 0.0f;
	if (VectorNormalizeInline(hordir) <= 0.0f)
	{
		VectorCopy(reach->start, runstart);
		return;
	}

	vec3_t start;
	VectorCopy(reach->start, start);
	start[2] += 1.0f;
	VectorMA(reach->start, 80.0f, hordir, runstart);

	float dist;
	for (dist = 0.0f; dist < 80.0f; dist += 10.0f)
	{
		vec3_t end;
		VectorMA(start, dist + 10.0f, hordir, end);
		end[2] += 1.0f;
		if (ms != NULL && BotMove_FindAreaForPoint(end) != ms->reachareanum)
		{
			break;
		}
	}

	if (dist < 80.0f)
	{
		VectorMA(reach->start, dist, hordir, runstart);
	}
}

/*
=============
BotMove_TravelJump

Run up to a jump reachability and trigger the jump near the launch point.
=============
*/
static void BotMove_TravelJump(bot_movestate_t *ms,
                               const aas_reachability_t *reach,
                               bot_moveresult_t *result)
{
	if (ms == NULL || reach == NULL || result == NULL)
	{
		return;
	}

	BotClearMoveResult(result);
	result->traveltype = TRAVEL_JUMP;

	vec3_t runstart;
	BotMove_JumpRunStart(ms, reach, runstart);

	vec3_t dir1;
	VectorSubtract(ms->origin, reach->start, dir1);
	dir1[2] = 0.0f;
	float dist1 = VectorNormalizeInline(dir1);

	vec3_t dir2;
	VectorSubtract(ms->origin, runstart, dir2);
	dir2[2] = 0.0f;
	float dist2 = VectorNormalizeInline(dir2);

	vec3_t hordir;
	if (DotProduct(dir1, dir2) < -0.8f || dist2 < 5.0f)
	{
		hordir[0] = reach->end[0] - ms->origin[0];
		hordir[1] = reach->end[1] - ms->origin[1];
		hordir[2] = 0.0f;
		VectorNormalizeInline(hordir);

		if (dist1 < 24.0f)
		{
			EA_Jump(ms->client);
		}
		else if (dist1 < 32.0f)
		{
			EA_DelayedJump(ms->client);
		}
		EA_Move(ms->client, hordir, 600.0f);
		ms->jumpreach = (ms->lastreachnum > 0) ? ms->lastreachnum : 1;
	}
	else
	{
		hordir[0] = runstart[0] - ms->origin[0];
		hordir[1] = runstart[1] - ms->origin[1];
		hordir[2] = 0.0f;
		VectorNormalizeInline(hordir);

		if (dist2 > 80.0f)
		{
			dist2 = 80.0f;
		}
		EA_Move(ms->client, hordir, 5.0f * dist2);
	}

	VectorCopy(hordir, result->movedir);
}

/*
=============
BotMove_TravelRocketJump

Approach the rocket-jump launch point, then fire while looking straight down.
=============
*/
static void BotMove_TravelRocketJump(bot_movestate_t *ms,
                                     const aas_reachability_t *reach,
                                     bot_moveresult_t *result)
{
	if (ms == NULL || reach == NULL || result == NULL)
	{
		return;
	}

	BotClearMoveResult(result);
	result->traveltype = TRAVEL_ROCKETJUMP;

	vec3_t hordir;
	VectorSubtract(reach->start, ms->origin, hordir);
	hordir[2] = 0.0f;
	float dist = VectorNormalizeInline(hordir);

	if (dist < 5.0f)
	{
		VectorSubtract(reach->end, ms->origin, hordir);
		hordir[2] = 0.0f;
		VectorNormalizeInline(hordir);
		EA_Jump(ms->client);
		EA_Attack(ms->client);
		EA_Move(ms->client, hordir, 800.0f);
		ms->jumpreach = (ms->lastreachnum > 0) ? ms->lastreachnum : 1;
	}
	else
	{
		if (dist > 80.0f)
		{
			dist = 80.0f;
		}
		EA_Move(ms->client, hordir, 5.0f * dist);
	}

	Vector2Angles(hordir, result->ideal_viewangles);
	result->ideal_viewangles[PITCH] = 90.0f;
	EA_SetViewAngles(ms->client, result->ideal_viewangles);

	int weapon = (int)LibVarValue("weapindex_rocketlauncher", "5");
	EA_SelectWeapon(ms->client, weapon);
	result->weapon = weapon;
	result->flags |= MOVERESULT_MOVEMENTVIEWSET | MOVERESULT_MOVEMENTWEAPON;
	VectorCopy(hordir, result->movedir);
}

/*
=============
BotMove_TravelSwim

Swim toward the reachability start at the retail fixed speed.
=============
*/
static void BotMove_TravelSwim(bot_movestate_t *ms,
                               const aas_reachability_t *reach,
                               bot_moveresult_t *result)
{
	if (ms == NULL || reach == NULL || result == NULL)
	{
		return;
	}

	vec3_t dir;
	VectorSubtract(reach->start, ms->origin, dir);
	VectorNormalizeInline(dir);

	BotMove_PrepareResult(result, dir, TRAVEL_SWIM, true);
	BotMove_CheckBlocked(ms, dir, true, result);
	EA_Move(ms->client, dir, 400.0f);
}

/*
=============
BotMove_TravelWaterJump

Swim forward and upward near the water-jump exit point.
=============
*/
static void BotMove_TravelWaterJump(bot_movestate_t *ms,
                                    const aas_reachability_t *reach,
                                    bot_moveresult_t *result)
{
	if (ms == NULL || reach == NULL || result == NULL)
	{
		return;
	}

	BotClearMoveResult(result);
	result->traveltype = TRAVEL_WATERJUMP;

	vec3_t dir;
	VectorSubtract(reach->end, ms->origin, dir);
	vec3_t hordir;
	VectorCopy(dir, hordir);
	hordir[2] = 0.0f;
	dir[2] += 15.0f + BotMove_CRandom() * 40.0f;
	VectorNormalizeInline(dir);
	float dist = VectorNormalizeInline(hordir);

	EA_MoveForward(ms->client);
	if (dist < 40.0f)
	{
		EA_MoveUp(ms->client);
	}

	VectorCopy(dir, result->movedir);
	result->flags |= MOVERESULT_MOVEMENTVIEW;
	BotMove_SetMovementView(dir, result, false);
}

/*
=============
BotMove_TravelTeleport

Move toward the teleporter start until the caller reports teleportation.
=============
*/
static void BotMove_TravelTeleport(bot_movestate_t *ms,
                                   const aas_reachability_t *reach,
                                   bot_moveresult_t *result)
{
	if (ms == NULL || reach == NULL || result == NULL)
	{
		return;
	}

	BotClearMoveResult(result);
	if ((ms->moveflags & MFL_TELEPORTED) != 0)
	{
		return;
	}

	result->traveltype = TRAVEL_TELEPORT;

	vec3_t hordir;
	VectorSubtract(reach->start, ms->origin, hordir);
	if ((ms->moveflags & MFL_SWIMMING) == 0)
	{
		hordir[2] = 0.0f;
	}
	float dist = VectorNormalizeInline(hordir);

	BotMove_CheckBlocked(ms, hordir, true, result);
	EA_Move(ms->client, hordir, (dist < 30.0f) ? 200.0f : 400.0f);

	if ((ms->moveflags & MFL_SWIMMING) != 0)
	{
		result->flags |= MOVERESULT_SWIMVIEW;
	}

	VectorCopy(hordir, result->movedir);
}

/*
=============
BotMove_TravelElevator

Drive active elevator reachability using the retail mover state machine.
=============
*/
static void BotMove_TravelElevator(bot_movestate_t *ms,
                                   const aas_reachability_t *reach,
                                   bot_moveresult_t *result)
{
	if (ms == NULL || reach == NULL || result == NULL)
	{
		return;
	}

	BotClearMoveResult(result);
	result->traveltype = TRAVEL_ELEVATOR;

	if (BotMove_OnMover(ms, reach))
	{
		result->flags |= MOVERESULT_ONTOPOF_ELEVATOR;
		if (fabsf(ms->origin[2] - reach->end[2]) < BotMove_LibVarValue(Bridge_MaxBarrier(), 50.0f))
		{
			vec3_t hordir;
			VectorSubtract(reach->end, ms->origin, hordir);
			hordir[2] = 0.0f;
			VectorNormalizeInline(hordir);
			if (!BotMove_CheckBarrierJump(ms, hordir, 100.0f))
			{
				EA_Move(ms->client, hordir, 400.0f);
			}
			VectorCopy(hordir, result->movedir);
			return;
		}

		vec3_t bottomcenter;
		vec3_t hordir;
		BotMove_MoverBottomCenter(reach, bottomcenter);
		VectorSubtract(bottomcenter, ms->origin, hordir);
		hordir[2] = 0.0f;
		float dist = VectorNormalizeInline(hordir);
		if (dist > 10.0f)
		{
			if (dist > 100.0f)
			{
				dist = 100.0f;
			}
			EA_Move(ms->client, hordir, 4.0f * dist);
			VectorCopy(hordir, result->movedir);
		}
		return;
	}

	vec3_t dir;
	VectorSubtract(reach->end, ms->origin, dir);
	float dist = VectorNormalizeInline(dir);
	if (dist < 64.0f)
	{
		if (dist > 60.0f)
		{
			dist = 60.0f;
		}
		float speed = 6.0f * dist;
		if ((ms->moveflags & MFL_SWIMMING) != 0 || !BotMove_CheckBarrierJump(ms, dir, 50.0f))
		{
			if (speed > 5.0f)
			{
				EA_Move(ms->client, dir, speed);
			}
		}
		VectorCopy(dir, result->movedir);
		if ((ms->moveflags & MFL_SWIMMING) != 0)
		{
			result->flags |= MOVERESULT_SWIMVIEW;
		}
		ms->reachability_time = 0.0f;
		return;
	}

	vec3_t dir1;
	VectorSubtract(reach->start, ms->origin, dir1);
	if ((ms->moveflags & MFL_SWIMMING) == 0)
	{
		dir1[2] = 0.0f;
	}
	float dist1 = VectorNormalizeInline(dir1);

	if (!BotMove_MoverDown(reach))
	{
		VectorCopy(dir1, dir);
		dist = dist1;
		BotMove_CheckBlocked(ms, dir, false, result);
		if (dist > 60.0f)
		{
			dist = 60.0f;
		}
		float speed = 6.0f * dist;
		if ((ms->moveflags & MFL_SWIMMING) == 0 && !BotMove_CheckBarrierJump(ms, dir, 50.0f))
		{
			if (speed > 5.0f)
			{
				EA_Move(ms->client, dir, speed);
			}
		}
		VectorCopy(dir, result->movedir);
		if ((ms->moveflags & MFL_SWIMMING) != 0)
		{
			result->flags |= MOVERESULT_SWIMVIEW;
		}
		result->type = RESULTTYPE_ELEVATORUP;
		result->flags |= MOVERESULT_WAITING;
		return;
	}

	vec3_t bottomcenter;
	vec3_t dir2;
	BotMove_MoverBottomCenter(reach, bottomcenter);
	VectorSubtract(bottomcenter, ms->origin, dir2);
	if ((ms->moveflags & MFL_SWIMMING) == 0)
	{
		dir2[2] = 0.0f;
	}
	float dist2 = VectorNormalizeInline(dir2);

	if (dist1 < 20.0f || dist2 < dist1 || DotProduct(dir1, dir2) < 0.0f)
	{
		dist = dist2;
		VectorCopy(dir2, dir);
	}
	else
	{
		dist = dist1;
		VectorCopy(dir1, dir);
	}

	BotMove_CheckBlocked(ms, dir, false, result);
	if (dist > 60.0f)
	{
		dist = 60.0f;
	}
	if ((ms->moveflags & MFL_SWIMMING) == 0 && !BotMove_CheckBarrierJump(ms, dir, 50.0f))
	{
		EA_Move(ms->client, dir, 6.0f * dist);
	}
	VectorCopy(dir, result->movedir);
	if ((ms->moveflags & MFL_SWIMMING) != 0)
	{
		result->flags |= MOVERESULT_SWIMVIEW;
	}
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

/*
=============
BotMove_TravelJumpPad

Walk straight to the jump-pad reachability start before launch.
=============
*/
static void BotMove_TravelJumpPad(bot_movestate_t *ms,
                                  const aas_reachability_t *reach,
                                  bot_moveresult_t *result)
{
	if (ms == NULL || reach == NULL || result == NULL)
	{
		return;
	}

	BotClearMoveResult(result);
	result->traveltype = TRAVEL_JUMPPAD;

	vec3_t hordir;
	VectorSubtract(reach->start, ms->origin, hordir);
	hordir[2] = 0.0f;
	VectorNormalizeInline(hordir);

	BotMove_CheckBlocked(ms, hordir, true, result);
	EA_Move(ms->client, hordir, 400.0f);
	VectorCopy(hordir, result->movedir);
}

/*
=============
BotMove_TravelFuncBob

Drive active func_bobbing reachability using the Q3 mover state machine.
=============
*/
static void BotMove_TravelFuncBob(bot_movestate_t *ms,
                                  const aas_reachability_t *reach,
                                  bot_moveresult_t *result)
{
	if (ms == NULL || reach == NULL || result == NULL)
	{
		return;
	}

	BotClearMoveResult(result);
	result->traveltype = TRAVEL_FUNCBOB;

	vec3_t bob_start;
	vec3_t bob_end;
	vec3_t bob_origin;
	(void)BotMove_FuncBobStartEnd(reach, bob_start, bob_end, bob_origin);

	if (BotMove_OnMover(ms, reach))
	{
		result->flags |= MOVERESULT_ONTOPOF_FUNCBOB;
		vec3_t dir;
		VectorSubtract(bob_origin, bob_end, dir);
		if (sqrtf(VectorLengthSquared(dir)) < 24.0f)
		{
			vec3_t hordir;
			VectorSubtract(reach->end, ms->origin, hordir);
			hordir[2] = 0.0f;
			VectorNormalizeInline(hordir);
			if (!BotMove_CheckBarrierJump(ms, hordir, 100.0f))
			{
				EA_Move(ms->client, hordir, 400.0f);
			}
			VectorCopy(hordir, result->movedir);
			return;
		}

		vec3_t bottomcenter;
		vec3_t hordir;
		BotMove_MoverBottomCenter(reach, bottomcenter);
		VectorSubtract(bottomcenter, ms->origin, hordir);
		hordir[2] = 0.0f;
		float dist = VectorNormalizeInline(hordir);
		if (dist > 10.0f)
		{
			if (dist > 100.0f)
			{
				dist = 100.0f;
			}
			EA_Move(ms->client, hordir, 4.0f * dist);
			VectorCopy(hordir, result->movedir);
		}
		return;
	}

	vec3_t dir;
	VectorSubtract(reach->end, ms->origin, dir);
	float dist = VectorNormalizeInline(dir);
	if (dist < 64.0f)
	{
		if (dist > 60.0f)
		{
			dist = 60.0f;
		}
		float speed = 6.0f * dist;
		if ((ms->moveflags & MFL_SWIMMING) != 0 || !BotMove_CheckBarrierJump(ms, dir, 50.0f))
		{
			if (speed > 5.0f)
			{
				EA_Move(ms->client, dir, speed);
			}
		}
		VectorCopy(dir, result->movedir);
		if ((ms->moveflags & MFL_SWIMMING) != 0)
		{
			result->flags |= MOVERESULT_SWIMVIEW;
		}
		ms->reachability_time = 0.0f;
		return;
	}

	vec3_t dir1;
	VectorSubtract(reach->start, ms->origin, dir1);
	if ((ms->moveflags & MFL_SWIMMING) == 0)
	{
		dir1[2] = 0.0f;
	}
	float dist1 = VectorNormalizeInline(dir1);

	VectorSubtract(bob_origin, bob_start, dir);
	if (sqrtf(VectorLengthSquared(dir)) > 16.0f)
	{
		VectorCopy(dir1, dir);
		dist = dist1;
		BotMove_CheckBlocked(ms, dir, false, result);
		if (dist > 60.0f)
		{
			dist = 60.0f;
		}
		float speed = 6.0f * dist;
		if ((ms->moveflags & MFL_SWIMMING) == 0 && !BotMove_CheckBarrierJump(ms, dir, 50.0f))
		{
			if (speed > 5.0f)
			{
				EA_Move(ms->client, dir, speed);
			}
		}
		VectorCopy(dir, result->movedir);
		if ((ms->moveflags & MFL_SWIMMING) != 0)
		{
			result->flags |= MOVERESULT_SWIMVIEW;
		}
		result->type = RESULTTYPE_WAITFORFUNCBOBBING;
		result->flags |= MOVERESULT_WAITING;
		return;
	}

	vec3_t bottomcenter;
	vec3_t dir2;
	BotMove_MoverBottomCenter(reach, bottomcenter);
	VectorSubtract(bottomcenter, ms->origin, dir2);
	if ((ms->moveflags & MFL_SWIMMING) == 0)
	{
		dir2[2] = 0.0f;
	}
	float dist2 = VectorNormalizeInline(dir2);

	if (dist1 < 20.0f || dist2 < dist1 || DotProduct(dir1, dir2) < 0.0f)
	{
		dist = dist2;
		VectorCopy(dir2, dir);
	}
	else
	{
		dist = dist1;
		VectorCopy(dir1, dir);
	}

	BotMove_CheckBlocked(ms, dir, false, result);
	if (dist > 60.0f)
	{
		dist = 60.0f;
	}
	if ((ms->moveflags & MFL_SWIMMING) == 0 && !BotMove_CheckBarrierJump(ms, dir, 50.0f))
	{
		EA_Move(ms->client, dir, 6.0f * dist);
	}
	VectorCopy(dir, result->movedir);
	if ((ms->moveflags & MFL_SWIMMING) != 0)
	{
		result->flags |= MOVERESULT_SWIMVIEW;
	}
}

/*
=============
BotMove_AirControl

Estimate the horizontal correction needed while finishing airborne travel.
=============
*/
static bool BotMove_AirControl(const vec3_t origin,
                               const vec3_t velocity,
                               const vec3_t goal,
                               vec3_t dir,
                               float *speed)
{
	if (origin == NULL || velocity == NULL || goal == NULL || dir == NULL || speed == NULL)
	{
		return false;
	}

	vec3_t org;
	vec3_t vel;
	VectorCopy(origin, org);
	vel[0] = velocity[0] * 0.1f;
	vel[1] = velocity[1] * 0.1f;
	vel[2] = velocity[2] * 0.1f;

	float gravity = BotMove_LibVarValue(Bridge_Gravity(), 800.0f);
	for (int i = 0; i < 50; ++i)
	{
		vel[2] -= gravity * 0.01f;
		if (vel[2] < 0.0f && org[2] + vel[2] < goal[2])
		{
			if (fabsf(vel[2]) > 0.0001f)
			{
				float scale = (goal[2] - org[2]) / vel[2];
				vel[0] *= scale;
				vel[1] *= scale;
				vel[2] *= scale;
			}
			VectorAdd(org, vel, org);
			VectorSubtract(goal, org, dir);
			float dist = VectorNormalizeInline(dir);
			if (dist > 32.0f)
			{
				dist = 32.0f;
			}
			*speed = 13.0f * dist;
			return true;
		}

		VectorAdd(org, vel, org);
	}

	VectorClear(dir);
	*speed = BotMove_LibVarValue(Bridge_MaxWalkVelocity(), 400.0f);
	return false;
}

/*
=============
BotMove_FinishTravelBarrierJump

Continue a barrier jump while airborne.
=============
*/
static void BotMove_FinishTravelBarrierJump(bot_movestate_t *ms,
                                            const aas_reachability_t *reach,
                                            bot_moveresult_t *result)
{
	BotClearMoveResult(result);
	result->traveltype = TRAVEL_BARRIERJUMP;

	if (ms->velocity[2] >= 250.0f)
	{
		return;
	}

	vec3_t dir;
	VectorSubtract(reach->end, ms->origin, dir);
	dir[2] = 0.0f;
	VectorNormalizeInline(dir);
	BotMove_CheckBlocked(ms, dir, true, result);
	EA_Move(ms->client, dir, BotMove_LibVarValue(Bridge_MaxWalkVelocity(), 400.0f));
	VectorCopy(dir, result->movedir);
}

/*
=============
BotMove_FinishTravelWalkOffLedge

Continue steering toward a ledge reachability endpoint after leaving ground.
=============
*/
static void BotMove_FinishTravelWalkOffLedge(bot_movestate_t *ms,
                                             const aas_reachability_t *reach,
                                             bot_moveresult_t *result)
{
	BotClearMoveResult(result);
	result->traveltype = TRAVEL_WALKOFFLEDGE;

	vec3_t goal;
	vec3_t forward;
	VectorSubtract(reach->end, ms->origin, forward);
	forward[2] = 0.0f;
	float dist = VectorNormalizeInline(forward);
	if (dist > 16.0f)
	{
		goal[0] = reach->end[0] + 16.0f * forward[0];
		goal[1] = reach->end[1] + 16.0f * forward[1];
		goal[2] = reach->end[2] + 16.0f * forward[2];
	}
	else
	{
		VectorCopy(reach->end, goal);
	}

	vec3_t dir;
	float speed;
	if (!BotMove_AirControl(ms->origin, ms->velocity, goal, dir, &speed))
	{
		VectorSubtract(reach->end, ms->origin, dir);
		dir[2] = 0.0f;
		VectorNormalizeInline(dir);
		speed = BotMove_LibVarValue(Bridge_MaxWalkVelocity(), 400.0f);
	}

	BotMove_CheckBlocked(ms, dir, true, result);
	EA_Move(ms->client, dir, speed);
	VectorCopy(dir, result->movedir);
}

/*
=============
BotMove_FinishTravelJump

Continue jump travel after the launch action has happened.
=============
*/
static void BotMove_FinishTravelJump(bot_movestate_t *ms,
                                     const aas_reachability_t *reach,
                                     bot_moveresult_t *result,
                                     int traveltype)
{
	BotClearMoveResult(result);
	result->traveltype = traveltype;

	if (!ms->jumpreach)
	{
		return;
	}

	vec3_t dir;
	VectorSubtract(reach->end, ms->origin, dir);
	dir[2] = 0.0f;
	float dist = VectorNormalizeInline(dir);

	vec3_t reachdir;
	VectorSubtract(reach->end, reach->start, reachdir);
	reachdir[2] = 0.0f;
	VectorNormalizeInline(reachdir);
	if (DotProduct(dir, reachdir) < -0.5f && dist < 24.0f)
	{
		return;
	}

	EA_Move(ms->client, dir, 800.0f);
	VectorCopy(dir, result->movedir);
}

/*
=============
BotMove_FinishTravelWaterJump

Continue a water jump only while still touching liquid below the bot.
=============
*/
static void BotMove_FinishTravelWaterJump(bot_movestate_t *ms,
                                          const aas_reachability_t *reach,
                                          bot_moveresult_t *result)
{
	BotClearMoveResult(result);
	result->traveltype = TRAVEL_WATERJUMP;

	if ((ms->moveflags & MFL_WATERJUMP) != 0)
	{
		return;
	}

	vec3_t probe;
	VectorCopy(ms->origin, probe);
	probe[2] -= 32.0f;
	if (!BotMove_PointInLiquid(probe, ms->areanum))
	{
		return;
	}

	vec3_t dir;
	VectorSubtract(reach->end, ms->origin, dir);
	dir[2] += 70.0f;
	VectorNormalizeInline(dir);
	BotMove_SetMovementView(dir, result, false);
	EA_Move(ms->client, dir, BotMove_LibVarValue(Bridge_MaxSwimVelocity(), 400.0f));
	VectorCopy(dir, result->movedir);
	result->flags |= MOVERESULT_MOVEMENTVIEW;
}

/*
=============
BotMove_FinishTravelAirControl

Shared Q3 air-control finish path for jump pads.
=============
*/
static void BotMove_FinishTravelAirControl(bot_movestate_t *ms,
                                           const aas_reachability_t *reach,
                                           bot_moveresult_t *result,
                                           int traveltype,
                                           bool checkblocked)
{
	BotClearMoveResult(result);
	result->traveltype = traveltype;

	vec3_t dir;
	float speed;
	if (!BotMove_AirControl(ms->origin, ms->velocity, reach->end, dir, &speed))
	{
		VectorSubtract(reach->end, ms->origin, dir);
		dir[2] = 0.0f;
		VectorNormalizeInline(dir);
		speed = BotMove_LibVarValue(Bridge_MaxWalkVelocity(), 400.0f);
	}

	if (checkblocked)
	{
		BotMove_CheckBlocked(ms, dir, true, result);
	}
	EA_Move(ms->client, dir, speed);
	VectorCopy(dir, result->movedir);
}

/*
=============
BotMove_FinishTravelRocketJump

Continue a Gladiator rocket jump after the launch action has happened.
=============
*/
static void BotMove_FinishTravelRocketJump(bot_movestate_t *ms,
                                           const aas_reachability_t *reach,
                                           bot_moveresult_t *result)
{
	if (ms == NULL || reach == NULL || result == NULL)
	{
		return;
	}

	BotClearMoveResult(result);
	result->traveltype = TRAVEL_ROCKETJUMP;

	if (!ms->jumpreach)
	{
		return;
	}

	vec3_t hordir;
	VectorSubtract(reach->end, ms->origin, hordir);
	hordir[2] = 0.0f;
	VectorNormalizeInline(hordir);
	EA_Move(ms->client, hordir, 800.0f);
	VectorCopy(hordir, result->movedir);
}

/*
=============
BotMove_FinishTravelElevator

Steer toward the closer elevator endpoint while airborne.
=============
*/
static void BotMove_FinishTravelElevator(bot_movestate_t *ms,
                                         const aas_reachability_t *reach,
                                         bot_moveresult_t *result)
{
	if (ms == NULL || reach == NULL || result == NULL)
	{
		return;
	}

	BotClearMoveResult(result);
	result->traveltype = TRAVEL_ELEVATOR;

	vec3_t bottomcenter;
	vec3_t bottomdir;
	vec3_t topdir;
	BotMove_MoverBottomCenter(reach, bottomcenter);
	VectorSubtract(bottomcenter, ms->origin, bottomdir);
	VectorSubtract(reach->end, ms->origin, topdir);

	vec3_t dir;
	if (fabsf(bottomdir[2]) < fabsf(topdir[2]))
	{
		VectorNormalizeTo(bottomdir, dir);
	}
	else
	{
		VectorNormalizeTo(topdir, dir);
	}

	EA_Move(ms->client, dir, 300.0f);
	VectorCopy(dir, result->movedir);
}

/*
=============
BotMove_FinishTravelFuncBob

Finish a func_bobbing reachability using the Q3 mover timing rules.
=============
*/
static void BotMove_FinishTravelFuncBob(bot_movestate_t *ms,
                                        const aas_reachability_t *reach,
                                        bot_moveresult_t *result)
{
	if (ms == NULL || reach == NULL || result == NULL)
	{
		return;
	}

	BotClearMoveResult(result);
	result->traveltype = TRAVEL_FUNCBOB;

	vec3_t bob_origin;
	vec3_t bob_start;
	vec3_t bob_end;
	vec3_t dir;
	(void)BotMove_FuncBobStartEnd(reach, bob_start, bob_end, bob_origin);

	VectorSubtract(bob_origin, bob_end, dir);
	float dist = sqrtf(VectorLengthSquared(dir));
	if (dist < 16.0f)
	{
		vec3_t hordir;
		VectorSubtract(reach->end, ms->origin, hordir);
		if ((ms->moveflags & MFL_SWIMMING) == 0)
		{
			hordir[2] = 0.0f;
		}
		dist = VectorNormalizeInline(hordir);
		if (dist > 60.0f)
		{
			dist = 60.0f;
		}
		float speed = 6.0f * dist;
		if (speed > 5.0f)
		{
			EA_Move(ms->client, dir, speed);
		}
		VectorCopy(dir, result->movedir);
		if ((ms->moveflags & MFL_SWIMMING) != 0)
		{
			result->flags |= MOVERESULT_SWIMVIEW;
		}
		return;
	}

	vec3_t bottomcenter;
	vec3_t hordir;
	BotMove_MoverBottomCenter(reach, bottomcenter);
	VectorSubtract(bottomcenter, ms->origin, hordir);
	if ((ms->moveflags & MFL_SWIMMING) == 0)
	{
		hordir[2] = 0.0f;
	}
	dist = VectorNormalizeInline(hordir);
	if (dist > 5.0f)
	{
		if (dist > 100.0f)
		{
			dist = 100.0f;
		}
		EA_Move(ms->client, hordir, 4.0f * dist);
		VectorCopy(hordir, result->movedir);
	}
}

/*
=============
BotMove_ShouldKeepReachability

Validate the last reachability before reusing it this frame.
=============
*/
static bool BotMove_ShouldKeepReachability(bot_movestate_t *ms,
                                           const bot_goal_t *goal,
                                           int travelflags,
                                           const aas_reachability_t *reach,
                                           const bot_moveresult_t *result)
{
	if (ms == NULL || goal == NULL || reach == NULL)
	{
		return false;
	}

	int allowed = (travelflags != 0) ? travelflags : TFL_DEFAULT;
	if (!BotMove_TravelAllowed(reach->traveltype, allowed))
	{
		return false;
	}

	int traveltype = reach->traveltype & TRAVELTYPE_MASK;
	if (traveltype == TRAVEL_GRAPPLEHOOK)
	{
		return ms->reachability_time >= aasworld.time &&
		       (ms->moveflags & MFL_GRAPPLERESET) == 0;
	}

	if (traveltype == TRAVEL_ELEVATOR || traveltype == TRAVEL_FUNCBOB)
	{
		if (result != NULL &&
		    (result->flags & (MOVERESULT_ONTOPOF_ELEVATOR | MOVERESULT_ONTOPOF_FUNCBOB)) != 0)
		{
			ms->reachability_time = aasworld.time + 5.0f;
		}

		return ms->areanum != reach->areanum &&
		       ms->reachability_time >= aasworld.time;
	}

	return ms->lastgoalareanum == goal->areanum &&
	       ms->reachability_time >= aasworld.time &&
	       ms->lastareanum == ms->areanum;
}

/*
=============
BotMove_FindMoverEntry

Resolve a brush-model mover catalogue entry for a live AAS entity.
=============
*/
static const bot_mover_catalogue_entry_t *BotMove_FindMoverEntry(int entnum, int *outModelnum)
{
    if (aasworld.entities == NULL || aasworld.maxEntities <= 0)
    {
        return NULL;
    }

    if (entnum < 0 || entnum >= aasworld.maxEntities)
    {
        return NULL;
    }

    const aas_entity_t *entity = &aasworld.entities[entnum];
    if (entity == NULL || !entity->inuse || entity->solid != SOLID_BSP)
    {
        return NULL;
    }

    int modelnum = AAS_ModelNumForEntity(entnum);
    if (modelnum <= 0)
    {
        return NULL;
    }

	const bot_mover_catalogue_entry_t *entry = BotMove_MoverCatalogueFindByModel(modelnum);
	if (entry == NULL)
	{
		return NULL;
	}

    if (outModelnum != NULL)
    {
        *outModelnum = modelnum;
    }

    return entry;
}

static void BotMove_HandleMoverLanding(bot_movestate_t *ms,
                                       int dispatchedReachIndex,
                                       const aas_reachability_t *dispatchedReach,
                                       bot_moveresult_t *result,
                                       int *ioReachIndex,
                                       int *ioReachArea,
                                       float *ioReachabilityTime)
{
    if (ms == NULL || result == NULL || dispatchedReach == NULL ||
        ioReachIndex == NULL || ioReachArea == NULL || ioReachabilityTime == NULL)
    {
        return;
    }

    if (aasworld.entities == NULL || aasworld.areaEntityLists == NULL ||
        aasworld.areaEntityListCount == 0U)
    {
        return;
    }

    if (ms->entitynum < 0 || ms->entitynum >= aasworld.maxEntities)
    {
        return;
    }

    const aas_entity_t *botEntity = &aasworld.entities[ms->entitynum];
    if (botEntity == NULL || !botEntity->inuse)
    {
        return;
    }

    for (aas_link_t *areaLink = botEntity->areas; areaLink != NULL; areaLink = areaLink->next_area)
    {
        int area = areaLink->areanum;
        if (area < 0)
        {
            continue;
        }

        size_t listIndex = (size_t)area;
        if (listIndex >= aasworld.areaEntityListCount)
        {
            continue;
        }

        for (aas_link_t *entLink = aasworld.areaEntityLists[listIndex]; entLink != NULL; entLink = entLink->next_ent)
        {
            int entnum = entLink->entnum;
            if (entnum == ms->entitynum)
            {
                continue;
            }

            int modelnum = 0;
            const bot_mover_catalogue_entry_t *entry = BotMove_FindMoverEntry(entnum, &modelnum);
            if (entry == NULL)
            {
                continue;
            }

            aas_reachability_t moverReach;
            int reachnum = AAS_NextModelReachability(0, modelnum);
            if (reachnum <= 0 || !BotMove_LoadReachability(reachnum, &moverReach))
            {
                bool assumeFuncBob = (entry->height > 0.0f);
                result->blocked = 1;
                result->blockentity = entnum;
                if (assumeFuncBob)
                {
                    result->flags |= MOVERESULT_ONTOPOF_FUNCBOB;
                    result->type = RESULTTYPE_WAITFORFUNCBOBBING;
                }
                else
                {
                    result->flags |= MOVERESULT_ONTOPOF_ELEVATOR;
                    result->type = RESULTTYPE_ELEVATORUP;
                }
                result->flags |= MOVERESULT_ONTOPOFOBSTACLE;
                return;
            }

            int traveltype = moverReach.traveltype & TRAVELTYPE_MASK;
            if (traveltype != TRAVEL_ELEVATOR && traveltype != TRAVEL_FUNCBOB)
            {
                continue;
            }

            int moverModel = moverReach.facenum & 0x0000FFFF;
            int dispatchedModel = dispatchedReach->facenum & 0x0000FFFF;
            bool sameModel = (dispatchedReachIndex > 0) &&
                             ((dispatchedReach->traveltype & TRAVELTYPE_MASK) == traveltype) &&
                             (dispatchedModel == moverModel);

            if (traveltype == TRAVEL_ELEVATOR)
            {
                result->flags |= MOVERESULT_ONTOPOF_ELEVATOR;
                result->type = RESULTTYPE_ELEVATORUP;
            }
            else
            {
                result->flags |= MOVERESULT_ONTOPOF_FUNCBOB;
                result->type = RESULTTYPE_WAITFORFUNCBOBBING;
            }

            if (!sameModel)
            {
                *ioReachIndex = reachnum;
                *ioReachArea = moverReach.areanum;
            }

            *ioReachabilityTime = aasworld.time + 5.0f;
            return;
        }
    }
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
            BotMove_TravelJump(ms, reach, &temp);
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
        case TRAVEL_ROCKETJUMP:
            BotMove_TravelRocketJump(ms, reach, &temp);
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
}

/*
=============
BotMove_DispatchFinishTravel

Finish the previous reachability while airborne, matching the retail split.
=============
*/
static void BotMove_DispatchFinishTravel(bot_movestate_t *ms,
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
		case TRAVEL_TELEPORT:
			temp.traveltype = traveltype;
			break;
		case TRAVEL_BARRIERJUMP:
			BotMove_FinishTravelBarrierJump(ms, reach, &temp);
			break;
		case TRAVEL_LADDER:
			BotMove_TravelLadder(ms, reach, &temp);
			break;
		case TRAVEL_WALKOFFLEDGE:
			BotMove_FinishTravelWalkOffLedge(ms, reach, &temp);
			break;
		case TRAVEL_JUMP:
			BotMove_FinishTravelJump(ms, reach, &temp, TRAVEL_JUMP);
			break;
		case TRAVEL_SWIM:
			BotMove_TravelSwim(ms, reach, &temp);
			break;
		case TRAVEL_WATERJUMP:
			BotMove_FinishTravelWaterJump(ms, reach, &temp);
			break;
		case TRAVEL_ELEVATOR:
			BotMove_FinishTravelElevator(ms, reach, &temp);
			break;
		case TRAVEL_GRAPPLEHOOK:
		{
			bot_moveresult_t grapple = BotTravel_Grapple(ms, reach);
			*result = grapple;
			result->traveltype = traveltype;
			return;
		}
		case TRAVEL_ROCKETJUMP:
			BotMove_FinishTravelRocketJump(ms, reach, &temp);
			break;
		case TRAVEL_JUMPPAD:
			BotMove_FinishTravelAirControl(ms, reach, &temp, TRAVEL_JUMPPAD, true);
			break;
		case TRAVEL_FUNCBOB:
			BotMove_FinishTravelFuncBob(ms, reach, &temp);
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

	ms->moveflags &= ~(MFL_ONGROUND | MFL_TELEPORTED | MFL_WATERJUMP | MFL_WALK | MFL_GRAPPLEPULL);
	if ((initmove->or_moveflags & MFL_ONGROUND) != 0)
	{
		ms->moveflags |= MFL_ONGROUND;
	}
	if ((initmove->or_moveflags & MFL_TELEPORTED) != 0)
	{
		ms->moveflags |= MFL_TELEPORTED;
	}
	if ((initmove->or_moveflags & MFL_WATERJUMP) != 0)
	{
		ms->moveflags |= MFL_WATERJUMP;
	}
	if ((initmove->or_moveflags & MFL_WALK) != 0)
	{
		ms->moveflags |= MFL_WALK;
	}
	if ((initmove->or_moveflags & MFL_GRAPPLEPULL) != 0)
	{
		ms->moveflags |= MFL_GRAPPLEPULL;
	}

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
        if (BotMove_AreaContentsHasLiquid(contents))
        {
            ms->moveflags |= MFL_SWIMMING;
        }
        if (BotMove_AreaHasLadder(ms->areanum))
        {
            ms->moveflags |= MFL_AGAINSTLADDER;
        }
    }

	if (BotMove_PointInLiquid(ms->origin, ms->areanum))
	{
		ms->moveflags |= MFL_SWIMMING;
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

	BotClearMoveResult(result);

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
	BotMoveClassifyEnvironment(ms);
	if (BotMove_OnGround(ms))
	{
		ms->moveflags |= MFL_ONGROUND;
	}

	if (ms->areanum <= 0)
	{
		ms->areanum = BotMove_FuzzyPointReachabilityArea(ms->origin);
		if (ms->areanum <= 0)
		{
			result->failure = 1;
			result->blocked = 1;
			result->type = RESULTTYPE_INSOLIDAREA;
			return;
		}
	}

	bool hasMovementSurface = BotMove_HasMovementSurface(ms);

    if ((ms->moveflags & MFL_ONGROUND) != 0 && BotMove_HandleGroundMover(ms, result))
    {
        ms->lastgoalareanum = goal->areanum;
        ms->lastareanum = ms->areanum;
        VectorCopy(ms->origin, ms->lastorigin);
        return;
    }

	if (!hasMovementSurface && ms->lastreachnum > 0)
	{
		aas_reachability_t lastreach;
		if (BotMove_LoadReachability(ms->lastreachnum, &lastreach))
		{
			BotMove_DispatchFinishTravel(ms, &lastreach, result);
			result->traveltype = lastreach.traveltype;
			if (result->blocked)
			{
				ms->reachability_time -= 10.0f * ms->thinktime;
			}

			VectorCopy(ms->origin, ms->lastorigin);
			return;
		}
	}

    if (ms->areanum == goal->areanum)
    {
        BotMove_DirectToGoal(ms, goal, result);
        return;
    }

    aas_reachability_t reach;
    int resultFlags = 0;
	int reachIndex = 0;
	if (ms->lastreachnum > 0 && BotMove_LoadReachability(ms->lastreachnum, &reach) &&
	    BotMove_ShouldKeepReachability(ms, goal, travelflags, &reach, result))
	{
		reachIndex = ms->lastreachnum;
	}

	if (reachIndex <= 0)
	{
		reachIndex = BotGetReachabilityToGoal(ms, goal, travelflags, &reach, &resultFlags);
		if (reachIndex > 0)
		{
			ms->reachareanum = ms->areanum;
			ms->jumpreach = 0;
			ms->moveflags &= ~MFL_GRAPPLERESET;
		}
	}
    if (reachIndex <= 0)
    {
		if ((resultFlags & MOVERESULT_BLOCKEDBYAVOIDSPOT) != 0)
		{
			result->failure = 1;
			result->flags |= resultFlags;
			return;
		}

        BotMove_DirectToGoal(ms, goal, result);
        ms->lastreachnum = 0;
        ms->lastgoalareanum = goal->areanum;
        VectorCopy(ms->origin, ms->lastorigin);
        return;
    }

    unsigned int preDispatchDiagnostics = result->diagnostics;

    BotMove_DispatchTravel(ms, &reach, result);
    result->diagnostics |= preDispatchDiagnostics;

    int traveltype = reach.traveltype & TRAVELTYPE_MASK;
    int finalReachIndex = reachIndex;
    int finalReachArea = reach.areanum;
    float finalReachabilityTime = aasworld.time + BotMove_TravelTimeout(traveltype);

    BotMove_HandleMoverLanding(ms,
                               reachIndex,
                               &reach,
                               result,
                               &finalReachIndex,
                               &finalReachArea,
                               &finalReachabilityTime);

    result->flags |= resultFlags;

    if (result->blocked)
    {
		ms->reachability_time -= 10.0f * ms->thinktime;
        return;
    }

	BotMove_AddToAvoidReach(ms, finalReachIndex, AVOIDREACH_TIME);
    ms->reachability_time = finalReachabilityTime;
    ms->lastreachnum = finalReachIndex;
    ms->reachareanum = finalReachArea;
    ms->lastgoalareanum = goal->areanum;
    ms->lastareanum = ms->areanum;
    VectorCopy(ms->origin, ms->lastorigin);
}

/*
=============
BotMove_GapDistance

Trace forward with crouch bounds to detect a gap before direct walking.
=============
*/
static float BotMove_GapDistance(const vec3_t origin, const vec3_t hordir, int entnum)
{
	if (origin == NULL || hordir == NULL)
	{
		return 0.0f;
	}

	vec3_t mins;
	vec3_t maxs;
	BotMove_CrouchPresenceBounds(mins, maxs);

	vec3_t start;
	vec3_t end;
	float startz = origin[2];

	VectorCopy(origin, start);
	VectorCopy(origin, end);
	end[2] -= 60.0f;
	bsp_trace_t trace = Q2_Trace(start,
	                             mins,
	                             maxs,
	                             end,
	                             entnum,
	                             CONTENTS_SOLID | CONTENTS_PLAYERCLIP);
	if (trace.fraction >= 1.0f)
	{
		return 1.0f;
	}
	startz = trace.endpos[2] + 1.0f;

	float maxbarrier = BotMove_LibVarValue(Bridge_MaxBarrier(), 50.0f);
	float maxstep = BotMove_LibVarValue(Bridge_MaxStep(), 18.0f);
	for (float dist = 8.0f; dist <= 100.0f; dist += 8.0f)
	{
		VectorMA(origin, dist, hordir, start);
		start[2] = startz + 24.0f;
		VectorCopy(start, end);
		end[2] -= 48.0f + maxbarrier;

		trace = Q2_Trace(start,
		                 mins,
		                 maxs,
		                 end,
		                 entnum,
		                 CONTENTS_SOLID | CONTENTS_PLAYERCLIP);
		if (trace.startsolid)
		{
			continue;
		}

		if (trace.endpos[2] < startz - maxstep - 8.0f)
		{
			VectorCopy(trace.endpos, end);
			end[2] -= 20.0f;
			if ((Q2_PointContents(end) & CONTENTS_WATER) != 0)
			{
				break;
			}

			return dist;
		}
		startz = trace.endpos[2];
	}

	return 0.0f;
}

/*
=============
BotMove_CheckBarrierJump

Probe the retail vertical-forward-down barrier jump path before walking.
=============
*/
static bool BotMove_CheckBarrierJump(bot_movestate_t *ms, const vec3_t dir, float speed)
{
	if (ms == NULL || dir == NULL)
	{
		return false;
	}

	vec3_t hordir;
	VectorSet(hordir, dir[0], dir[1], 0.0f);
	if (VectorNormalizeInline(hordir) <= 0.0f)
	{
		return false;
	}

	vec3_t mins;
	vec3_t maxs;
	bot_movestate_t trace_state = *ms;
	trace_state.presencetype = PRESENCE_NORMAL;
	BotMove_ClientBoundsForPresence(&trace_state, mins, maxs);

	float maxbarrier = BotMove_LibVarValue(Bridge_MaxBarrier(), 50.0f);
	float maxstep = BotMove_LibVarValue(Bridge_MaxStep(), 18.0f);

	vec3_t start;
	vec3_t end;
	VectorCopy(ms->origin, end);
	end[2] += maxbarrier;

	bsp_trace_t trace = Q2_Trace(ms->origin,
	                             mins,
	                             maxs,
	                             end,
	                             ms->entitynum,
	                             CONTENTS_SOLID | CONTENTS_PLAYERCLIP);
	if (trace.startsolid)
	{
		return false;
	}
	if (trace.endpos[2] - ms->origin[2] < maxstep)
	{
		return false;
	}

	VectorMA(ms->origin, ms->thinktime * speed * 0.5f, hordir, end);
	VectorCopy(trace.endpos, start);
	end[2] = trace.endpos[2];
	trace = Q2_Trace(start,
	                 mins,
	                 maxs,
	                 end,
	                 ms->entitynum,
	                 CONTENTS_SOLID | CONTENTS_PLAYERCLIP);
	if (trace.startsolid)
	{
		return false;
	}

	VectorCopy(trace.endpos, start);
	VectorCopy(trace.endpos, end);
	end[2] = ms->origin[2];
	trace = Q2_Trace(start,
	                 mins,
	                 maxs,
	                 end,
	                 ms->entitynum,
	                 CONTENTS_SOLID | CONTENTS_PLAYERCLIP);
	if (trace.startsolid)
	{
		return false;
	}
	if (trace.fraction >= 1.0f)
	{
		return false;
	}
	if (trace.endpos[2] - ms->origin[2] < maxstep)
	{
		return false;
	}

	EA_Jump(ms->client);
	EA_Move(ms->client, hordir, speed);
	ms->moveflags |= MFL_BARRIERJUMP;
	ms->jumpreach = 1;
	return true;
}

/*
=============
BotMove_SwimInDirection

Submit direct swim movement without ground prediction.
=============
*/
static int BotMove_SwimInDirection(bot_movestate_t *ms, const vec3_t dir, float speed, int type)
{
	(void)type;

	if (ms == NULL || dir == NULL)
	{
		return 0;
	}

	vec3_t normalized;
	if (VectorNormalizeTo(dir, normalized) <= 0.0f)
	{
		return 0;
	}

	EA_Move(ms->client, normalized, speed);
	return 1;
}

/*
=============
BotMove_WalkInDirection

Submit direct walk movement, including retail barrier jump probing.
=============
*/
static int BotMove_WalkInDirection(bot_movestate_t *ms, const vec3_t dir, float speed, int type)
{
	if (ms == NULL || dir == NULL)
	{
		return 0;
	}

	vec3_t normalized;
	if (VectorNormalizeTo(dir, normalized) <= 0.0f)
	{
		return 0;
	}

	vec3_t hordir;
	VectorSet(hordir, normalized[0], normalized[1], 0.0f);
	if (VectorNormalizeInline(hordir) <= 0.0f)
	{
		return 0;
	}

	if ((type & MOVE_GRAPPLE) != 0)
	{
		ms->moveflags |= MFL_ACTIVEGRAPPLE;
	}

	if ((ms->moveflags & MFL_ONGROUND) != 0 || BotMove_OnGround(ms))
	{
		ms->moveflags |= MFL_ONGROUND;
		if (BotMove_CheckBarrierJump(ms, hordir, speed))
		{
			return 1;
		}

		ms->moveflags &= ~MFL_BARRIERJUMP;
		int presencetype = ((type & MOVE_CROUCH) != 0 && (type & MOVE_JUMP) == 0)
			? PRESENCE_CROUCH
			: PRESENCE_NORMAL;
		if ((type & MOVE_JUMP) == 0 &&
		    BotMove_GapDistance(ms->origin, hordir, ms->entitynum) > 0.0f)
		{
			type |= MOVE_JUMP;
		}

		vec3_t cmdmove;
		vec3_t velocity;
		vec3_t origin;
		VectorScale(hordir, speed, cmdmove);
		VectorCopy(ms->velocity, velocity);

		int maxframes;
		int cmdframes;
		int stopevent;
		if ((type & MOVE_JUMP) != 0)
		{
			cmdmove[2] = 400.0f;
			maxframes = 30;
			cmdframes = 1;
			stopevent = SE_HITGROUND | SE_HITGROUNDDAMAGE |
			            SE_ENTERWATER | SE_ENTERSLIME | SE_ENTERLAVA;
		}
		else
		{
			maxframes = 2;
			cmdframes = 2;
			stopevent = SE_HITGROUNDDAMAGE |
			            SE_ENTERWATER | SE_ENTERSLIME | SE_ENTERLAVA;
		}

		VectorCopy(ms->origin, origin);
		origin[2] += 0.5f;

		aas_clientmove_t move;
		if (!AAS_PredictClientMovement(&move,
		                               ms->entitynum,
		                               origin,
		                               presencetype,
		                               qtrue,
		                               velocity,
		                               cmdmove,
		                               cmdframes,
		                               maxframes,
		                               0.1f,
		                               stopevent,
		                               0,
		                               qfalse))
		{
			return 0;
		}

		if ((type & MOVE_JUMP) != 0 && move.frames >= maxframes)
		{
			return 0;
		}

		if ((move.stopevent & (SE_ENTERSLIME | SE_ENTERLAVA | SE_HITGROUNDDAMAGE)) != 0)
		{
			return 0;
		}

		if ((move.stopevent & SE_HITGROUND) != 0)
		{
			vec3_t tmpdir;
			if (VectorNormalizeTo(move.velocity, tmpdir) > 0.0f &&
			    BotMove_GapDistance(move.endpos, tmpdir, ms->entitynum) > 0.0f)
			{
				return 0;
			}
			if (BotMove_GapDistance(move.endpos, hordir, ms->entitynum) > 0.0f)
			{
				return 0;
			}
		}

		vec3_t moved;
		moved[0] = move.endpos[0] - ms->origin[0];
		moved[1] = move.endpos[1] - ms->origin[1];
		moved[2] = 0.0f;
		if (sqrtf(VectorLengthSquared(moved)) < speed * ms->thinktime * 0.5f)
		{
			return 0;
		}

		if ((type & MOVE_JUMP) != 0)
		{
			EA_Jump(ms->client);
			ms->jumpreach = 1;
		}
		if ((type & MOVE_CROUCH) != 0)
		{
			EA_Crouch(ms->client);
		}
		EA_Move(ms->client, hordir, speed);
		return 1;
	}

	if ((ms->moveflags & MFL_BARRIERJUMP) != 0)
	{
		if (ms->velocity[2] < 50.0f)
		{
			EA_Move(ms->client, normalized, speed);
		}
		return 1;
	}

	return 1;
}

/*
=============
BotMoveInDirection

Dispatch direct movement through the reconstructed swim/walk helpers.
=============
*/
int BotMoveInDirection(int movestate, const vec3_t dir, float speed, int type)
{
	bot_movestate_t *ms = BotMoveStateFromHandle(movestate);
	if (ms == NULL)
	{
		return 0;
	}

	if (BotMove_PointInLiquid(ms->origin, ms->areanum) ||
	    (ms->moveflags & MFL_SWIMMING) != 0)
	{
		ms->moveflags |= MFL_SWIMMING;
		return BotMove_SwimInDirection(ms, dir, speed, type);
	}

	return BotMove_WalkInDirection(ms, dir, speed, type);
}

/*
=============
BotMove_ResetAvoidReach

Clear movement state avoid reachability tracking.
=============
*/
void BotMove_ResetAvoidReach(int movestate)
{
	bot_movestate_t *ms = BotMoveStateFromHandle(movestate);
	if (ms == NULL)
	{
		return;
	}

	memset(ms->avoidreach, 0, sizeof(ms->avoidreach));
	memset(ms->avoidreachtimes, 0, sizeof(ms->avoidreachtimes));
	memset(ms->avoidreachtries, 0, sizeof(ms->avoidreachtries));
	ms->lastavoidreach = 0;
	ms->lastavoidreachtime = 0.0f;
	ms->lastavoidreachtries = 0;
}

/*
=============
BotResetLastAvoidReach

Clear the most recent avoid reach entry.
=============
*/
void BotResetLastAvoidReach(int movestate)
{
	bot_movestate_t *ms;
	int latest;
	float latesttime;

	ms = BotMoveStateFromHandle(movestate);
	if (ms == NULL)
	{
		return;
	}

	latest = -1;
	latesttime = 0.0f;
	for (int i = 0; i < MAX_AVOIDREACH; i++)
	{
		if (ms->avoidreachtimes[i] > latesttime)
		{
			latesttime = ms->avoidreachtimes[i];
			latest = i;
		}
	}

	if (latest >= 0 && latesttime > 0.0f)
	{
		ms->avoidreachtimes[latest] = 0.0f;
		if (ms->avoidreachtries[latest] > 0)
		{
			ms->avoidreachtries[latest]--;
		}
	}

	ms->lastavoidreach = 0;
	ms->lastavoidreachtime = 0.0f;
	ms->lastavoidreachtries = 0;
}

/*
=============
BotReachabilityArea

Resolve the reachability area for an origin with mover/solid handling.
=============
*/
int BotReachabilityArea(const vec3_t origin, int client)
{
	if (origin == NULL)
	{
		return 0;
	}

	vec3_t mins;
	vec3_t maxs;
	BotMove_CrouchPresenceBounds(mins, maxs);

	vec3_t start;
	vec3_t end;
	VectorCopy(origin, start);
	VectorCopy(origin, end);
	end[2] -= 3.0f;

	bsp_trace_t trace = Q2_Trace(start, mins, maxs, end, client, CONTENTS_SOLID | CONTENTS_PLAYERCLIP);
	if (!trace.startsolid && trace.fraction < 1.0f)
	{
		int entnum = trace.ent;
		if (entnum == 0)
		{
			return BotMove_FuzzyPointReachabilityArea(start);
		}

		if (entnum > 0 && aasworld.entities != NULL && entnum < aasworld.maxEntities)
		{
			const aas_entity_t *entity = &aasworld.entities[entnum];
			if (entity != NULL && entity->inuse)
			{
				int modelnum = AAS_ModelNumForEntity(entnum);
				if (modelnum > 0)
				{
					const bot_mover_catalogue_entry_t *entry = BotMove_MoverCatalogueFindByModel(modelnum);
					if (entry != NULL &&
						(entry->kind == BOT_MOVER_KIND_FUNC_PLAT ||
						 entry->kind == BOT_MOVER_KIND_FUNC_BOB))
					{
						aas_reachability_t reach;
						int reachnum = AAS_NextModelReachability(0, modelnum);
						if (reachnum > 0 && BotMove_LoadReachability(reachnum, &reach))
						{
							return reach.areanum;
						}
					}
				}
			}
		}

		if (Q2_PointContents(start) & MASK_WATER)
		{
			return BotMove_FuzzyPointReachabilityArea(start);
		}

		int areanum = BotMove_FuzzyPointReachabilityArea(start);
		if (areanum > 0 && BotMove_AreaHasReachability(areanum))
		{
			return areanum;
		}

		VectorCopy(origin, end);
		end[2] -= 800.0f;
		trace = Q2_Trace(start, mins, maxs, end, -1, CONTENTS_SOLID | CONTENTS_PLAYERCLIP);
		if (!trace.startsolid)
		{
			return BotMove_FuzzyPointReachabilityArea(trace.endpos);
		}
	}

	return BotMove_FuzzyPointReachabilityArea(start);
}

/*
=============
BotMovementViewTarget

Compute a lookahead target point along the current movement path.
=============
*/
int BotMovementViewTarget(int movestate,
						  const bot_goal_t *goal,
						  int travelflags,
						  float lookahead,
						  vec3_t target)
{
	aas_reachability_t reach;
	aas_reachability_t next_reach;
	int next_reachnum;
	int reachnum;
	int lastareanum;
	bot_movestate_t *ms;
	vec3_t end;
	float dist;

	if (target == NULL)
	{
		return 0;
	}

	ms = BotMoveStateFromHandle(movestate);
	if (ms == NULL)
	{
		return 0;
	}

	if (goal == NULL || ms->lastreachnum <= 0)
	{
		return 0;
	}

	reachnum = ms->lastreachnum;
	VectorCopy(ms->origin, end);
	lastareanum = ms->lastareanum;
	dist = 0.0f;

	while (reachnum > 0 && dist < lookahead)
	{
		int traveltype;

		if (!BotMove_LoadReachability(reachnum, &reach))
		{
			return 0;
		}

		if (BotMove_AddToTarget(end, reach.start, lookahead, &dist, target))
		{
			return 1;
		}

		traveltype = reach.traveltype & TRAVELTYPE_MASK;
		if (traveltype == TRAVEL_TELEPORT ||
			traveltype == TRAVEL_ROCKETJUMP ||
			traveltype == TRAVEL_BFGJUMP)
		{
			return 1;
		}

		if (traveltype != TRAVEL_JUMPPAD &&
			traveltype != TRAVEL_ELEVATOR &&
			traveltype != TRAVEL_FUNCBOB)
		{
			if (BotMove_AddToTarget(reach.start, reach.end, lookahead, &dist, target))
			{
				return 1;
			}
		}

		next_reachnum = BotMove_GetReachabilityFromPoint(ms,
		                                                 reach.end,
		                                                 reach.areanum,
		                                                 ms->lastgoalareanum,
		                                                 lastareanum,
		                                                 goal,
		                                                 travelflags,
		                                                 &next_reach);

		VectorCopy(reach.end, end);
		lastareanum = reach.areanum;
		if (lastareanum == goal->areanum)
		{
			BotMove_AddToTarget(reach.end, goal->origin, lookahead, &dist, target);
			return 1;
		}
		reachnum = next_reachnum;
	}

	return 0;
}

/*
=============
BotMove_Visible

Check line of sight between two points for movement prediction.
=============
*/
static bool BotMove_Visible(int ent, const vec3_t start, const vec3_t end)
{
	if (start == NULL || end == NULL)
	{
		return false;
	}

	vec3_t trace_start;
	vec3_t trace_end;
	VectorCopy(start, trace_start);
	VectorCopy(end, trace_end);

	vec3_t mins = {0.0f, 0.0f, 0.0f};
	vec3_t maxs = {0.0f, 0.0f, 0.0f};
	bsp_trace_t trace = Q2_Trace(trace_start,
								 mins,
								 maxs,
								 trace_end,
								 ent,
								 CONTENTS_SOLID | CONTENTS_PLAYERCLIP);
	return trace.fraction >= 1.0f;
}

/*
=============
BotPredictVisiblePosition

Predict a reachable point that is visible from the goal.
=============
*/
int BotPredictVisiblePosition(const vec3_t origin,
							  int areanum,
							  const bot_goal_t *goal,
							  int travelflags,
							  vec3_t target)
{
	if (origin == NULL || target == NULL || goal == NULL)
	{
		return 0;
	}

	if (areanum <= 0 || goal->areanum <= 0)
	{
		return 0;
	}

	bot_movestate_t temp;
	memset(&temp, 0, sizeof(temp));
	VectorCopy(origin, temp.origin);
	temp.areanum = areanum;

	vec3_t end;
	VectorCopy(origin, end);
	int lastgoalareanum = goal->areanum;
	int lastareanum = areanum;

	for (int i = 0; i < 20 && areanum != goal->areanum; ++i)
	{
		aas_reachability_t reach;

		int reachnum = BotMove_GetReachabilityFromPoint(&temp,
		                                                end,
		                                                areanum,
		                                                lastgoalareanum,
		                                                lastareanum,
		                                                goal,
		                                                travelflags,
		                                                &reach);
		if (reachnum <= 0)
		{
			return 0;
		}

		if (BotMove_Visible(goal->entitynum, goal->origin, reach.start))
		{
			VectorCopy(reach.start, target);
			return 1;
		}

		if (BotMove_Visible(goal->entitynum, goal->origin, reach.end))
		{
			VectorCopy(reach.end, target);
			return 1;
		}

		if (reach.areanum == goal->areanum)
		{
			VectorCopy(reach.end, target);
			return 1;
		}

		lastareanum = areanum;
		areanum = reach.areanum;
		VectorCopy(reach.end, end);
	}

	return 0;
}

/*
=============
BotAddAvoidSpot

Adds, updates, or clears avoidance spots for a move state.
=============
*/
void BotAddAvoidSpot(int movestate, vec3_t origin, float radius, int type)
{
	bot_movestate_t *ms;

	ms = BotMoveStateFromHandle(movestate);
	if (ms == NULL)
	{
		return;
	}

	if (type == AVOID_CLEAR)
	{
		ms->numavoidspots = 0;
		return;
	}

	if (origin == NULL)
	{
		return;
	}

	if (ms->numavoidspots >= MAX_AVOIDSPOTS)
	{
		return;
	}

	int index = ms->numavoidspots;
	VectorCopy(origin, ms->avoidspots[index].origin);
	ms->avoidspots[index].radius = radius;
	ms->avoidspots[index].type = type;
	ms->numavoidspots++;
}

/*
=============
BotSetBrushModelTypes

Resets mover model metadata; rebuilt on map load.
=============
*/
void BotSetBrushModelTypes(void)
{
	BotMove_MoverCatalogueReset();
}

/*
=============
BotSetupMoveAI
=============
*/
int BotSetupMoveAI(void)
{
	BotSetBrushModelTypes();
	return BLERR_NOERROR;
}

/*
=============
BotShutdownMoveAI
=============
*/
void BotShutdownMoveAI(void)
{
	for (int handle = 1; handle <= MAX_CLIENTS; ++handle)
	{
		if (g_botMoveStates[handle] != NULL)
		{
			BotFreeMoveState(handle);
		}
	}

	BotMove_MoverCatalogueReset();
}
