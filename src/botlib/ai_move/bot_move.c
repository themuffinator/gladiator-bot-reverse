#include "bot_move.h"

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

#define AVOIDREACH_TIME 6.0f
#define AVOIDREACH_TRIES 4
#define BOT_MOVE_RETAIL_HANDLE_BASE (MAX_CLIENTS + 1)
#define BOT_MOVE_HANDLE_LIMIT (2 * MAX_CLIENTS + 1)

static bot_movestate_t *botmovestates[BOT_MOVE_HANDLE_LIMIT + 1];
static bool botmovestate_owned[BOT_MOVE_HANDLE_LIMIT + 1];

static libvar_t *libvar_sv_friction;
static libvar_t *libvar_sv_stopspeed;
static libvar_t *libvar_sv_gravity;
static libvar_t *libvar_sv_waterfriction;
static libvar_t *libvar_sv_watergravity;
static libvar_t *libvar_sv_maxvelocity;
static libvar_t *libvar_sv_maxwalkvelocity;
static libvar_t *libvar_sv_maxcrouchvelocity;
static libvar_t *libvar_sv_maxswimvelocity;
static libvar_t *libvar_sv_maxaccelerate;
static libvar_t *libvar_sv_airaccelerate;
static libvar_t *libvar_sv_step;
static libvar_t *libvar_sv_maxbarrier;
static libvar_t *libvar_sv_maxsteepness;
static libvar_t *libvar_sv_jumpvel;
static libvar_t *libvar_sv_maxwaterjump;
static libvar_t *libvar_laserhook;
static int grapplemodelindex;

/* Retail model-name lookup, owned by the AAS map asset table. */
extern int IndexFromModel(const char *model);

/*
=============
BotMove_LibVarValue

Read a registered movement libvar while retaining its retail fallback.
=============
*/
static float BotMove_LibVarValue(const libvar_t *libvar, float fallback)
{
	return libvar != NULL ? libvar->value : fallback;
}

/*
=============
BotMove_AngleDiff

Return retail's raw, single-wrap angular difference for grapple alignment.
=============
*/
static double BotMove_AngleDiff(float angle1, float angle2)
{
	double difference = (double)angle1 - (double)angle2;
	if (angle1 > angle2)
	{
		if (difference > 180.0)
		{
			return difference - 360.0;
		}
	}
	else if (difference < -180.0)
	{
		return difference + 360.0;
	}

	return difference;
}

/*
=============
BotMove_VectorNormalize

Normalize a vector and return its original length.
=============
*/
static float BotMove_VectorNormalize(vec3_t vector)
{
	float length = sqrtf(DotProduct(vector, vector));

	if (length != 0.0f)
	{
		float inverse = 1.0f / length;
		vector[0] *= inverse;
		vector[1] *= inverse;
		vector[2] *= inverse;
	}

	return length;
}

/*
=============
BotMove_RandomSigned

Return the retail rand() based value in the range [-1, 1].
=============
*/
static float BotMove_RandomSigned(void)
{
	return 2.0f * ((float)(rand() & 0x7fff) * 0.000030518509f - 0.5f);
}

/*
=============
BotReachabilityArea

Find a reachable area at, around, or below a point.
=============
*/
int BotReachabilityArea(const vec3_t origin, int testground)
{
	vec3_t start;
	vec3_t end;
	aas_trace_t trace;
	int areas[10];
	int fallback = 0;

	for (int pass = 0; pass < 2; ++pass)
	{
		VectorCopy(origin, start);
		if (pass > 0)
		{
			VectorCopy(origin, end);
			end[2] -= 800.0f;
			trace = AAS_TraceClientBBox(origin, end, PRESENCE_CROUCH, -1);
			if (!trace.startsolid)
			{
				VectorCopy(trace.endpos, start);
			}
		}

		fallback = 0;
		int areanum = AAS_PointAreaNum(start);
		if (areanum != 0)
		{
			fallback = areanum;
			if (AAS_AreaReachability(areanum) != 0)
			{
				return areanum;
			}
		}

		for (int dz = 5; dz >= -5; dz -= 5)
		{
			for (int dy = 5; dy >= -5; dy -= 5)
			{
				for (int dx = 5; dx >= -5; dx -= 5)
				{
					end[0] = start[0] + (float)dz;
					end[1] = start[1] + (float)dy;
					end[2] = start[2] + (float)dx;
					int count = AAS_TraceAreas(start, end, areas, NULL, 10);
					if (count <= 0)
					{
						continue;
					}
					if (fallback == 0)
					{
						fallback = areas[0];
					}
					for (int index = 0; index < count; ++index)
					{
						if (AAS_AreaReachability(areas[index]) != 0)
						{
							return areas[index];
						}
					}
				}
			}
		}

		if (!testground)
		{
			break;
		}
	}

	return fallback;
}

/*
=============
BotOnMover

Test whether the bot is standing on the elevator encoded by a reachability.
=============
*/
static int BotOnMover(const vec3_t origin, int entnum, const aas_reachability_t *reach)
{
	vec3_t angles = {0.0f, 0.0f, 0.0f};
	vec3_t mins;
	vec3_t maxs;
	vec3_t modelorigin;
	vec3_t boxmins = {-16.0f, -16.0f, -8.0f};
	vec3_t boxmaxs = {16.0f, 16.0f, 8.0f};
	vec3_t start;
	vec3_t end;

	if (reach->traveltype != TRAVEL_ELEVATOR)
	{
		return 0;
	}

	AAS_BSPModelMinsMaxsOrigin(reach->facenum,
		angles,
		mins,
		maxs,
		modelorigin);
	for (int axis = 0; axis < 2; ++axis)
	{
		if (origin[axis] > maxs[axis] + modelorigin[axis] + 16.0f ||
			origin[axis] < mins[axis] + modelorigin[axis] - 16.0f)
		{
			return 0;
		}
	}

	VectorCopy(origin, start);
	VectorCopy(origin, end);
	start[2] += 24.0f;
	end[2] -= 48.0f;
	bsp_trace_t trace = Q2_Trace(start,
		boxmins,
		boxmaxs,
		end,
		entnum,
		MASK_PLAYERSOLID);
	return !trace.startsolid && !trace.allsolid && trace.ent != 0 &&
		AAS_EntityModelNum(trace.ent) == reach->facenum;
}

/*
=============
MoverDown

Test whether an elevator is below its reachability start.
=============
*/
static int MoverDown(const aas_reachability_t *reach)
{
	vec3_t angles = {0.0f, 0.0f, 0.0f};
	vec3_t mins;
	vec3_t maxs;
	vec3_t origin;

	if (reach->traveltype != TRAVEL_ELEVATOR)
	{
		return 0;
	}

	AAS_BSPModelMinsMaxsOrigin(reach->facenum,
		angles,
		mins,
		maxs,
		origin);
	if (!AAS_OriginOfMoverWithModelNum(reach->facenum, origin))
	{
		BotLib_Print(PRT_MESSAGE, "no entity with model %d\n", reach->facenum);
		return 0;
	}

	return origin[2] + maxs[2] < reach->start[2];
}

/*
=============
BotValidTravel

Test whether the caller enabled a reachability's retail travel flag.
=============
*/
static int BotValidTravel(const aas_reachability_t *reach, int travelflags)
{
	return (AAS_TravelFlagForType(reach->traveltype) & ~travelflags) == 0;
}

/*
=============
BotAddToAvoidReach

Record a failed or selected reachability in the one-entry retail avoid list.
=============
*/
static void BotAddToAvoidReach(bot_movestate_t *ms, int number, float avoidtime)
{
	for (int index = 0; index < MAX_AVOIDREACH; ++index)
	{
		if (ms->avoidreach[index] != number)
		{
			continue;
		}
		if (ms->avoidreachtimes[index] > AAS_Time())
		{
			++ms->avoidreachtries[index];
		}
		else
		{
			ms->avoidreachtries[index] = 1;
		}
		ms->avoidreachtimes[index] = AAS_Time() + avoidtime;
		return;
	}

	for (int index = 0; index < MAX_AVOIDREACH; ++index)
	{
		if (ms->avoidreachtimes[index] >= AAS_Time())
		{
			continue;
		}
		ms->avoidreach[index] = number;
		ms->avoidreachtimes[index] = AAS_Time() + avoidtime;
		ms->avoidreachtries[index] = 1;
		return;
	}
}

/*
=============
BotGetReachabilityToGoal

Select the least-cost allowed outgoing reachability to a goal.
=============
*/
static int BotGetReachabilityToGoal(const vec3_t origin,
	int areanum,
	int lastgoalareanum,
	int lastareanum,
	int entnum,
	const int avoidreach[MAX_AVOIDREACH],
	const float avoidreachtimes[MAX_AVOIDREACH],
	const int avoidreachtries[MAX_AVOIDREACH],
	const bot_goal_t *goal,
	int travelflags)
{
	int besttime = 0;
	int bestreachnum = 0;

	(void)origin;
	(void)entnum;
	for (int reachnum = AAS_NextAreaReachability(areanum, 0);
		reachnum != 0;
		reachnum = AAS_NextAreaReachability(areanum, reachnum))
	{
		int index;
		for (index = 0; index < MAX_AVOIDREACH; ++index)
		{
			if (avoidreach[index] == reachnum &&
				avoidreachtimes[index] >= AAS_Time())
			{
				break;
			}
		}
		if (index != MAX_AVOIDREACH && avoidreachtries[index] > AVOIDREACH_TRIES)
		{
			continue;
		}

		aas_reachability_t reach;
		AAS_ReachabilityFromNum(reachnum, &reach);
		if (lastgoalareanum == goal->areanum && reach.areanum == lastareanum)
		{
			continue;
		}
		if (!BotValidTravel(&reach, travelflags))
		{
			continue;
		}

		int traveltime = (unsigned short)AAS_AreaTravelTimeToGoalArea(
			reach.areanum,
			NULL,
			goal->areanum,
			travelflags);
		if (traveltime == 0)
		{
			continue;
		}
		traveltime += (unsigned short)reach.traveltime;
		if (besttime == 0 || traveltime < besttime)
		{
			besttime = traveltime;
			bestreachnum = reachnum;
		}
	}

	return bestreachnum;
}

/*
=============
BotMovementViewTarget

Return the next reachability end beyond the currently active reach.
=============
*/
int BotMovementViewTarget(bot_movestate_t *movestate,
	const bot_goal_t *goal,
	int travelflags,
	vec3_t target)
{
	if (movestate->lastreachnum == 0 || goal == NULL)
	{
		return 0;
	}

	aas_reachability_t reach;
	AAS_ReachabilityFromNum(movestate->lastreachnum, &reach);
	int reachnum = BotGetReachabilityToGoal(reach.end,
		reach.areanum,
		movestate->lastgoalareanum,
		movestate->lastareanum,
		movestate->entitynum,
		movestate->avoidreach,
		movestate->avoidreachtimes,
		movestate->avoidreachtries,
		goal,
		travelflags);
	if (reachnum == 0)
	{
		return 0;
	}

	AAS_ReachabilityFromNum(reachnum, &reach);
	/*
	 * Retail aims the preview at the far side of the next reachability, not
	 * at its entry point: 0x10031270 stores reach dwords 6, 7 and 8 into the
	 * target, which is `end`, and lowers the vertical component by fifteen
	 * units.  The Quake III successor copies `start` instead, so the shared
	 * lookahead shape must not be borrowed here.
	 */
	target[0] = reach.end[0];
	target[2] = reach.end[2] - 15.0f;
	target[1] = reach.end[1];
	return 1;
}

/*
=============
MoverBottomCenter

Calculate the bottom center of the elevator at its base position.
=============
*/
static void MoverBottomCenter(const aas_reachability_t *reach, vec3_t bottomcenter)
{
	vec3_t angles = {0.0f, 0.0f, 0.0f};
	vec3_t mins;
	vec3_t maxs;
	vec3_t origin;

	if (reach->traveltype != TRAVEL_ELEVATOR)
	{
		return;
	}

	AAS_BSPModelMinsMaxsOrigin(reach->facenum,
		angles,
		mins,
		maxs,
		origin);
	bottomcenter[0] = origin[0] + 0.5f * (mins[0] + maxs[0]);
	bottomcenter[1] = origin[1] + 0.5f * (mins[1] + maxs[1]);
	bottomcenter[2] = reach->start[2];
}

/*
=============
BotGapDistance

Return the first forward sample distance that drops beyond a walkable step.
=============
*/
static float BotGapDistance(bot_movestate_t *ms, const vec3_t dir)
{
	vec3_t start;
	vec3_t end;
	VectorCopy(ms->origin, start);
	VectorCopy(ms->origin, end);
	end[2] -= 60.0f;
	aas_trace_t trace = AAS_TraceClientBBox(start, end, PRESENCE_CROUCH, -1);
	float startz = trace.endpos[2] + 1.0f;
	float maxbarrier = BotMove_LibVarValue(libvar_sv_maxbarrier, 50.0f);
	float step = BotMove_LibVarValue(libvar_sv_step, 18.0f);

	for (float distance = 8.0f; distance <= 100.0f; distance += 8.0f)
	{
		VectorMA(ms->origin, distance, dir, start);
		start[2] = startz + 24.0f;
		VectorCopy(start, end);
		end[2] -= 48.0f + maxbarrier;
		trace = AAS_TraceClientBBox(start, end, PRESENCE_CROUCH, -1);
		if (trace.startsolid)
		{
			continue;
		}
		if (trace.endpos[2] < startz - step - 8.0f)
		{
			VectorCopy(trace.endpos, end);
			end[2] -= 20.0f;
			if ((AAS_PointContents(end) & CONTENTS_WATER) != 0)
			{
				break;
			}
			return distance;
		}
		startz = trace.endpos[2];
	}

	return 0.0f;
}

/*
=============
BotCheckBarrierJump

Trace the retail up-forward-down barrier sequence and submit a jump on success.
=============
*/
static int BotCheckBarrierJump(bot_movestate_t *ms, const vec3_t dir, float speed)
{
	vec3_t start;
	vec3_t end;
	vec3_t hordir;
	VectorCopy(ms->origin, end);
	end[2] += BotMove_LibVarValue(libvar_sv_maxbarrier, 50.0f);
	aas_trace_t trace = AAS_TraceClientBBox(ms->origin,
		end,
		PRESENCE_NORMAL,
		ms->entitynum);
	if (trace.startsolid ||
		trace.endpos[2] - ms->origin[2] < BotMove_LibVarValue(libvar_sv_step, 18.0f))
	{
		return 0;
	}

	hordir[0] = dir[0];
	hordir[1] = dir[1];
	hordir[2] = 0.0f;
	BotMove_VectorNormalize(hordir);
	VectorMA(ms->origin, speed * ms->thinktime * 0.5f, hordir, end);
	VectorCopy(trace.endpos, start);
	end[2] = trace.endpos[2];
	trace = AAS_TraceClientBBox(start, end, PRESENCE_NORMAL, ms->entitynum);
	if (trace.startsolid)
	{
		return 0;
	}

	VectorCopy(trace.endpos, start);
	VectorCopy(trace.endpos, end);
	end[2] = ms->origin[2];
	trace = AAS_TraceClientBBox(start, end, PRESENCE_NORMAL, ms->entitynum);
	if (trace.startsolid || trace.fraction >= 1.0f ||
		trace.endpos[2] - ms->origin[2] < BotMove_LibVarValue(libvar_sv_step, 18.0f))
	{
		return 0;
	}

	EA_Jump(ms->client);
	EA_Move(ms->client, hordir, speed);
	ms->moveflags |= MFL_BARRIERJUMP;
	return 1;
}

/*
=============
BotSwimInDirection

Submit a normalized direct swimming command.
=============
*/
static int BotSwimInDirection(bot_movestate_t *ms,
	const vec3_t dir,
	float speed,
	int type)
{
	vec3_t normdir;
	(void)type;
	VectorCopy(dir, normdir);
	BotMove_VectorNormalize(normdir);
	EA_Move(ms->client, normdir, speed);
	return 1;
}

/*
=============
BotWalkInDirection

Predict and submit the retail direct walking or jumping command.
=============
*/
static int BotWalkInDirection(bot_movestate_t *ms,
	const vec3_t dir,
	float speed,
	int type)
{
	if ((ms->moveflags & MFL_ONGROUND) != 0)
	{
		if (!BotCheckBarrierJump(ms, dir, speed))
		{
			int presencetype = ((type & MOVE_CROUCH) != 0 &&
				(type & MOVE_JUMP) == 0)
				? PRESENCE_CROUCH
				: PRESENCE_NORMAL;
			vec3_t hordir = {dir[0], dir[1], 0.0f};
			BotMove_VectorNormalize(hordir);
			if ((type & MOVE_JUMP) == 0 && BotGapDistance(ms, hordir) > 0.0f)
			{
				type |= MOVE_JUMP;
			}

			vec3_t cmdmove;
			VectorScale(hordir, speed, cmdmove);
			float predictiontime;
			if ((type & MOVE_JUMP) != 0)
			{
				predictiontime = 3.0f;
				cmdmove[2] = BotMove_LibVarValue(libvar_sv_jumpvel, 224.0f);
			}
			else
			{
				predictiontime = 2.0f;
			}

			/*
			 * Retail divides straight into the frame time.  A zero think
			 * time only reaches the predictor through a host that never
			 * filled in the frame input, where the original truncation
			 * yields a negative frame count and the frame-count guard
			 * below rejects the move anyway, so short-circuiting here
			 * keeps the retail outcome without the undefined conversion.
			 */
			if (ms->thinktime <= 0.0f)
			{
				return 0;
			}
			int maxframes = (int)(predictiontime / ms->thinktime);
			/*
			 * The retail predictor returns its result by value and reports
			 * a failed prediction as an all-zero record, which this port
			 * reproduces by clearing the caller's storage.  Retail keeps
			 * evaluating the guards below in that case, so the return code
			 * is deliberately discarded here.
			 */
			aas_clientmove_t move;
			AAS_PredictClientMovement(&move,
				ms->entitynum,
				ms->origin,
				presencetype,
				1,
				ms->velocity,
				cmdmove,
				maxframes,
				maxframes,
				ms->thinktime,
				0x3d,
				0,
				0);
			if (move.frames >= maxframes || (move.stopevent & 0x38) != 0)
			{
				return 0;
			}

			vec3_t movedir;
			movedir[0] = move.endpos[0] - ms->origin[0];
			movedir[1] = move.endpos[1] - ms->origin[1];
			movedir[2] = 0.0f;
			if (sqrtf(DotProduct(movedir, movedir)) < speed * ms->thinktime * 0.5f)
			{
				return 0;
			}

			if ((type & MOVE_JUMP) != 0)
			{
				EA_Jump(ms->client);
			}
			if ((type & MOVE_CROUCH) != 0)
			{
				EA_Crouch(ms->client);
			}
			EA_Move(ms->client, movedir, speed);
		}
	}
	else if ((ms->moveflags & MFL_BARRIERJUMP) != 0 && ms->velocity[2] < 50.0f)
	{
		EA_Move(ms->client, dir, speed);
	}

	return 1;
}

/*
=============
BotMoveInDirection

Dispatch direct movement through the retail swim or walk predictor.
=============
*/
int BotMoveInDirection(bot_movestate_t *movestate,
	const vec3_t dir,
	float speed,
	int type)
{
	if (AAS_Swimming(movestate->origin))
	{
		return BotSwimInDirection(movestate, dir, speed, type);
	}
	return BotWalkInDirection(movestate, dir, speed, type);
}

/*
=============
Intersection

Find the truncated intersection point of two infinite two-dimensional lines.
=============
*/
int Intersection(const vec3_t p1,
	const vec3_t p2,
	const vec3_t p3,
	const vec3_t p4,
	vec3_t out)
{
	float d1x = p2[0] - p1[0];
	float d1y = p2[1] - p1[1];
	float d2x = p4[0] - p3[0];
	float d2y = p4[1] - p3[1];
	float determinant = d2x * d1y - d2y * d1x;

	if (determinant == 0.0f)
	{
		return 0;
	}

	float cross1 = d1x * p1[1] - d1y * p1[0];
	float cross2 = d2x * p3[1] - d2y * p3[0];
	out[0] = (float)(int)((cross2 * d1x - cross1 * d2x) / determinant);
	out[1] = (float)(int)((cross2 * d1y - cross1 * d2y) / determinant);
	return 1;
}

/*
=============
BotCheckBlocked

Trace three units in the requested direction and record a blocking entity.
=============
*/
static int BotCheckBlocked(bot_movestate_t *ms,
	const vec3_t dir,
	bot_moveresult_t *moveresult)
{
	vec3_t mins;
	vec3_t maxs;
	vec3_t end;
	AAS_PresenceTypeBoundingBox(ms->presencetype, mins, maxs);
	/*
	 * Retail compares against a double literal, so the vertical component
	 * widens before the test.  Keeping the float form here would reject a
	 * direction of exactly 0.7f that the original accepts.
	 */
	if (fabs(dir[2]) < 0.7)
	{
		mins[2] += BotMove_LibVarValue(libvar_sv_step, 18.0f);
		maxs[2] -= 10.0f;
	}
	VectorMA(ms->origin, 3.0f, dir, end);
	bsp_trace_t trace = Q2_Trace(ms->origin,
		mins,
		maxs,
		end,
		ms->entitynum,
		MASK_PLAYERSOLID);
	if (trace.startsolid)
	{
		return trace.startsolid;
	}
	if (trace.ent != 0)
	{
		moveresult->blocked = 1;
		moveresult->blockentity = trace.ent;
		return (int)(intptr_t)moveresult;
	}
	return 0;
}

/*
=============
BotClearMoveResult

Clear exactly the six-dword retail status prefix, preserving vector storage.
=============
*/
bot_moveresult_t *BotClearMoveResult(bot_moveresult_t *moveresult)
{
	memset(moveresult, 0, 0x18);
	return moveresult;
}

/*
=============
BotTravel_Walk

Execute an active retail walking reachability.
=============
*/
bot_moveresult_t BotTravel_Walk(bot_movestate_t *ms,
	const aas_reachability_t *reach)
{
	bot_moveresult_t moveresult;
	vec3_t dir;
	BotClearMoveResult(&moveresult);
	dir[0] = reach->start[0] - ms->origin[0];
	dir[1] = reach->start[1] - ms->origin[1];
	dir[2] = 0.0f;
	float distance = BotMove_VectorNormalize(dir);
	BotCheckBlocked(ms, dir, &moveresult);
	if (distance < 10.0f)
	{
		dir[0] = reach->end[0] - ms->origin[0];
		dir[1] = reach->end[1] - ms->origin[1];
		dir[2] = 0.0f;
		distance = BotMove_VectorNormalize(dir);
	}
	if ((AAS_AreaPresenceType(reach->areanum) & PRESENCE_NORMAL) == 0 &&
		distance < 20.0f)
	{
		EA_Crouch(ms->client);
	}

	float gapdistance = BotGapDistance(ms, dir);
	float speed = gapdistance > 0.0f
		? 300.0f - (300.0f - (gapdistance + gapdistance))
		: 400.0f;
	EA_Move(ms->client, dir, speed);
	VectorCopy(dir, moveresult.movedir);
	return moveresult;
}

/*
=============
BotFinishTravel_Walk

Execute the retained but unreachable retail walk finish helper.
=============
*/
bot_moveresult_t BotFinishTravel_Walk(bot_movestate_t *ms,
	const aas_reachability_t *reach)
{
	bot_moveresult_t moveresult;
	vec3_t dir;
	BotClearMoveResult(&moveresult);
	dir[0] = reach->end[0] - ms->origin[0];
	dir[1] = reach->end[1] - ms->origin[1];
	dir[2] = 0.0f;
	float distance = BotMove_VectorNormalize(dir);
	if (distance > 100.0f)
	{
		distance = 100.0f;
	}
	float speed = 400.0f - (400.0f - 3.0f * distance);
	EA_Move(ms->client, dir, speed);
	VectorCopy(dir, moveresult.movedir);
	return moveresult;
}

/*
=============
BotTravel_Crouch

Execute an active retail crouch reachability.
=============
*/
bot_moveresult_t BotTravel_Crouch(bot_movestate_t *ms,
	const aas_reachability_t *reach)
{
	bot_moveresult_t moveresult;
	vec3_t dir;
	BotClearMoveResult(&moveresult);
	dir[0] = reach->end[0] - ms->origin[0];
	dir[1] = reach->end[1] - ms->origin[1];
	dir[2] = 0.0f;
	BotMove_VectorNormalize(dir);
	BotCheckBlocked(ms, dir, &moveresult);
	EA_Crouch(ms->client);
	EA_Move(ms->client, dir, 400.0f);
	VectorCopy(dir, moveresult.movedir);
	return moveresult;
}

/*
=============
BotTravel_BarrierJump

Approach and trigger an active retail barrier-jump reachability.
=============
*/
bot_moveresult_t BotTravel_BarrierJump(bot_movestate_t *ms,
	const aas_reachability_t *reach)
{
	bot_moveresult_t moveresult;
	vec3_t dir;
	BotClearMoveResult(&moveresult);
	dir[0] = reach->start[0] - ms->origin[0];
	dir[1] = reach->start[1] - ms->origin[1];
	dir[2] = 0.0f;
	float distance = BotMove_VectorNormalize(dir);
	BotCheckBlocked(ms, dir, &moveresult);
	if (distance < 7.0f)
	{
		EA_Jump(ms->client);
	}
	else
	{
		if (distance > 60.0f)
		{
			distance = 60.0f;
		}
		float speed = 360.0f - (360.0f - distance * 6.0f);
		EA_Move(ms->client, dir, speed);
	}
	VectorCopy(dir, moveresult.movedir);
	return moveresult;
}

/*
=============
BotFinishTravel_BarrierJump

Apply descending air control after a retail barrier jump.
=============
*/
bot_moveresult_t BotFinishTravel_BarrierJump(bot_movestate_t *ms,
	const aas_reachability_t *reach)
{
	bot_moveresult_t moveresult;
	BotClearMoveResult(&moveresult);
	if (ms->velocity[2] < 250.0f)
	{
		vec3_t dir;
		dir[0] = reach->end[0] - ms->origin[0];
		dir[1] = reach->end[1] - ms->origin[1];
		dir[2] = 0.0f;
		float distance = BotMove_VectorNormalize(dir);
		BotCheckBlocked(ms, dir, &moveresult);
		if (distance > 60.0f)
		{
			distance = 60.0f;
		}
		float speed = 400.0f - (400.0f - distance * 6.0f);
		EA_Move(ms->client, dir, speed);
		VectorCopy(dir, moveresult.movedir);
	}
	return moveresult;
}

/*
=============
BotTravel_Swim

Execute an active retail swimming reachability.
=============
*/
bot_moveresult_t BotTravel_Swim(bot_movestate_t *ms,
	const aas_reachability_t *reach)
{
	bot_moveresult_t moveresult;
	vec3_t dir;
	BotClearMoveResult(&moveresult);
	VectorSubtract(reach->start, ms->origin, dir);
	BotMove_VectorNormalize(dir);
	BotCheckBlocked(ms, dir, &moveresult);
	EA_Move(ms->client, dir, 400.0f);
	VectorCopy(dir, moveresult.movedir);
	Vector2Angles(dir, moveresult.ideal_viewangles);
	moveresult.flags |= MOVERESULT_SWIMVIEW;
	return moveresult;
}

/*
=============
BotTravel_WaterJump

Execute the active retail water-jump view and action sequence.
=============
*/
bot_moveresult_t BotTravel_WaterJump(bot_movestate_t *ms,
	const aas_reachability_t *reach)
{
	bot_moveresult_t moveresult;
	vec3_t dir;
	vec3_t hordir;
	BotClearMoveResult(&moveresult);
	VectorSubtract(reach->end, ms->origin, dir);
	VectorCopy(dir, hordir);
	hordir[2] = 0.0f;
	/*
	 * Retail folds the random rise into the vertical component before the
	 * fixed fifteen-unit lift, so the accumulation order is kept exactly as
	 * the original wrote it.
	 */
	dir[2] = BotMove_RandomSigned() * 40.0f + dir[2] + 15.0f;
	BotMove_VectorNormalize(dir);
	float distance = BotMove_VectorNormalize(hordir);
	EA_MoveForward(ms->client);
	if (distance < 40.0f)
	{
		EA_MoveUp(ms->client);
	}
	Vector2Angles(dir, moveresult.ideal_viewangles);
	moveresult.flags |= MOVERESULT_MOVEMENTVIEW;
	VectorCopy(dir, moveresult.movedir);
	return moveresult;
}

/*
=============
BotFinishTravel_WaterJump

Apply the retail randomized push while leaving water after a water jump.
=============
*/
bot_moveresult_t BotFinishTravel_WaterJump(bot_movestate_t *ms,
	const aas_reachability_t *reach)
{
	bot_moveresult_t moveresult;
	BotClearMoveResult(&moveresult);
	if ((ms->moveflags & MFL_WATERJUMP) == 0)
	{
		vec3_t point;
		VectorCopy(ms->origin, point);
		point[2] -= 32.0f;
		if ((AAS_PointContents(point) & 0x38) != 0)
		{
			vec3_t dir;
			VectorSubtract(reach->end, ms->origin, dir);
			dir[0] = BotMove_RandomSigned() * 10.0f + dir[0];
			dir[1] = BotMove_RandomSigned() * 10.0f + dir[1];
			dir[2] = BotMove_RandomSigned() * 10.0f + dir[2] + 70.0f;
			BotMove_VectorNormalize(dir);
			EA_Move(ms->client, dir, 400.0f);
			Vector2Angles(dir, moveresult.ideal_viewangles);
			moveresult.flags |= MOVERESULT_MOVEMENTVIEW;
			VectorCopy(dir, moveresult.movedir);
		}
	}
	return moveresult;
}

/*
=============
BotTravel_WalkOffLedge

Execute the Gladiator walk-off-ledge approach and horizontal-speed solve.
=============
*/
bot_moveresult_t BotTravel_WalkOffLedge(bot_movestate_t *ms,
	const aas_reachability_t *reach)
{
	bot_moveresult_t moveresult;
	vec3_t dir;
	vec3_t position;
	BotClearMoveResult(&moveresult);
	VectorSubtract(reach->end, ms->origin, position);
	BotCheckBlocked(ms, position, &moveresult);
	dir[0] = position[0];
	dir[1] = position[1];
	dir[2] = 0.0f;
	float distance = BotMove_VectorNormalize(dir);
	float originaldistance = distance;
	float speed;

	if (distance < 60.0f)
	{
		dir[0] = reach->end[0] - ms->origin[0];
		dir[1] = reach->end[1] - ms->origin[1];
		dir[2] = 0.0f;
		BotMove_VectorNormalize(dir);
		position[0] = reach->end[0] - reach->start[0];
		position[1] = reach->end[1] - reach->start[1];
		position[2] = 0.0f;
		if (sqrtf(DotProduct(position, position)) < 15.0f)
		{
			if (originaldistance > 0.0f && originaldistance < 150.0f)
			{
				speed = 380.0f - (300.0f - (originaldistance + originaldistance));
			}
			else
			{
				speed = 400.0f;
			}
		}
		else if (!AAS_HorizontalVelocityForJump(0.0f,
			reach->start,
			reach->end,
			&speed))
		{
			speed = 200.0f;
		}
	}
	else
	{
		speed = 400.0f;
	}

	BotCheckBlocked(ms, dir, &moveresult);
	EA_Move(ms->client, dir, speed);
	VectorCopy(dir, moveresult.movedir);
	return moveresult;
}

/*
=============
BotFinishTravel_WalkOffLedge

Apply fixed-speed horizontal air control after walking off a ledge.
=============
*/
bot_moveresult_t BotFinishTravel_WalkOffLedge(bot_movestate_t *ms,
	const aas_reachability_t *reach)
{
	bot_moveresult_t moveresult;
	vec3_t position;
	vec3_t dir;
	BotClearMoveResult(&moveresult);
	VectorSubtract(reach->end, ms->origin, position);
	BotCheckBlocked(ms, position, &moveresult);
	dir[0] = position[0];
	dir[1] = position[1];
	dir[2] = 0.0f;
	BotMove_VectorNormalize(dir);
	EA_Move(ms->client, dir, 400.0f);
	VectorCopy(dir, moveresult.movedir);
	return moveresult;
}

/*
=============
BotTravel_Jump

Execute the retail run-up and launch state machine for a jump reachability.
=============
*/
bot_moveresult_t BotTravel_Jump(bot_movestate_t *ms,
	const aas_reachability_t *reach)
{
	bot_moveresult_t moveresult;
	vec3_t hordir;
	vec3_t runstart;
	vec3_t dir1;
	vec3_t dir2;
	vec3_t start;
	vec3_t end;
	BotClearMoveResult(&moveresult);

	AAS_JumpReachRunStart(reach, runstart);
	hordir[0] = runstart[0] - reach->start[0];
	hordir[1] = runstart[1] - reach->start[1];
	hordir[2] = 0.0f;
	BotMove_VectorNormalize(hordir);
	VectorCopy(reach->start, start);
	start[2] += 1.0f;
	VectorMA(reach->start, 80.0f, hordir, runstart);

	float sampledistance;
	for (sampledistance = 0.0f;
		sampledistance < 80.0f;
		sampledistance += 10.0f)
	{
		VectorMA(start, sampledistance + 10.0f, hordir, end);
		end[2] += 1.0f;
		if (AAS_PointAreaNum(end) != ms->reachareanum)
		{
			break;
		}
	}
	if (sampledistance < 80.0f)
	{
		VectorMA(reach->start, sampledistance, hordir, runstart);
	}

	VectorSubtract(ms->origin, reach->start, dir1);
	dir1[2] = 0.0f;
	float startdistance = BotMove_VectorNormalize(dir1);
	VectorSubtract(ms->origin, runstart, dir2);
	dir2[2] = 0.0f;
	float runstartdistance = BotMove_VectorNormalize(dir2);
	/* Retail compares the dot product against a double literal. */
	if (DotProduct(dir1, dir2) < -0.8 || runstartdistance < 5.0f)
	{
		hordir[0] = reach->end[0] - ms->origin[0];
		hordir[1] = reach->end[1] - ms->origin[1];
		hordir[2] = 0.0f;
		BotMove_VectorNormalize(hordir);
		if (startdistance < 24.0f)
		{
			EA_Jump(ms->client);
		}
		else if (startdistance < 32.0f)
		{
			EA_DelayedJump(ms->client);
		}
		EA_Move(ms->client, hordir, 600.0f);
		ms->jumpreach = ms->lastreachnum;
	}
	else
	{
		hordir[0] = runstart[0] - ms->origin[0];
		hordir[1] = runstart[1] - ms->origin[1];
		hordir[2] = 0.0f;
		BotMove_VectorNormalize(hordir);
		if (runstartdistance > 80.0f)
		{
			runstartdistance = 80.0f;
		}
		float speed = 400.0f - (400.0f - runstartdistance * 5.0f);
		EA_Move(ms->client, hordir, speed);
	}

	VectorCopy(hordir, moveresult.movedir);
	return moveresult;
}

/*
=============
BotFinishTravel_Jump

Apply retail horizontal air control after the jump action has fired.
=============
*/
bot_moveresult_t BotFinishTravel_Jump(bot_movestate_t *ms,
	const aas_reachability_t *reach)
{
	bot_moveresult_t moveresult;
	BotClearMoveResult(&moveresult);
	if (ms->jumpreach != 0)
	{
		vec3_t dir;
		vec3_t reachdir;
		dir[0] = reach->end[0] - ms->origin[0];
		dir[1] = reach->end[1] - ms->origin[1];
		dir[2] = 0.0f;
		float distance = BotMove_VectorNormalize(dir);
		reachdir[0] = reach->end[0] - reach->start[0];
		reachdir[1] = reach->end[1] - reach->start[1];
		reachdir[2] = 0.0f;
		BotMove_VectorNormalize(reachdir);
		if (DotProduct(reachdir, dir) >= -0.5f || distance >= 24.0f)
		{
			EA_Move(ms->client, dir, 800.0f);
			VectorCopy(dir, moveresult.movedir);
		}
	}
	return moveresult;
}

/*
=============
BotTravel_Ladder

Execute the retail view-driven ladder action.
=============
*/
bot_moveresult_t BotTravel_Ladder(bot_movestate_t *ms,
	const aas_reachability_t *reach)
{
	bot_moveresult_t moveresult;
	vec3_t dir;
	vec3_t viewdir;
	vec3_t origin = {0.0f, 0.0f, 0.0f};
	BotClearMoveResult(&moveresult);
	VectorSubtract(reach->end, ms->origin, dir);
	BotMove_VectorNormalize(dir);
	viewdir[0] = dir[0];
	viewdir[1] = dir[1];
	viewdir[2] = dir[2] * 3.0f;
	Vector2Angles(viewdir, moveresult.ideal_viewangles);
	EA_Move(ms->client, origin, 0.0f);
	EA_MoveForward(ms->client);
	moveresult.flags |= MOVERESULT_MOVEMENTVIEW;
	VectorCopy(dir, moveresult.movedir);
	return moveresult;
}

/*
=============
BotTravel_Teleport

Approach a teleporter unless the frame input marks the bot as teleported.
=============
*/
bot_moveresult_t BotTravel_Teleport(bot_movestate_t *ms,
	const aas_reachability_t *reach)
{
	bot_moveresult_t moveresult;
	BotClearMoveResult(&moveresult);
	if ((ms->moveflags & MFL_TELEPORTED) == 0)
	{
		vec3_t dir;
		VectorSubtract(reach->start, ms->origin, dir);
		if ((ms->moveflags & MFL_SWIMMING) == 0)
		{
			dir[2] = 0.0f;
		}
		float distance = BotMove_VectorNormalize(dir);
		BotCheckBlocked(ms, dir, &moveresult);
		EA_Move(ms->client, dir, distance < 30.0f ? 200.0f : 400.0f);
		if ((ms->moveflags & MFL_SWIMMING) != 0)
		{
			moveresult.flags |= MOVERESULT_SWIMVIEW;
		}
		VectorCopy(dir, moveresult.movedir);
	}
	return moveresult;
}

/*
=============
BotTravel_Elevator

Execute the retail elevator approach, wait, boarding, and exit states.
=============
*/
bot_moveresult_t BotTravel_Elevator(bot_movestate_t *ms,
	const aas_reachability_t *reach)
{
	bot_moveresult_t moveresult;
	vec3_t final;
	vec3_t dir;
	vec3_t reachdir;
	vec3_t telegoaldir;
	vec3_t telegoal;
	BotClearMoveResult(&moveresult);

	if (BotOnMover(ms->origin, ms->entitynum, reach))
	{
		if ((float)abs((int)(ms->origin[2] - reach->end[2])) <
			BotMove_LibVarValue(libvar_sv_maxbarrier, 50.0f))
		{
			dir[0] = reach->end[0] - ms->origin[0];
			dir[1] = reach->end[1] - ms->origin[1];
			dir[2] = 0.0f;
			BotMove_VectorNormalize(dir);
			if (!BotCheckBarrierJump(ms, dir, 100.0f))
			{
				EA_Move(ms->client, dir, 400.0f);
			}
			VectorCopy(dir, moveresult.movedir);
		}
		else
		{
			MoverBottomCenter(reach, telegoal);
			dir[0] = telegoal[0] - ms->origin[0];
			dir[1] = telegoal[1] - ms->origin[1];
			dir[2] = 0.0f;
			float distance = BotMove_VectorNormalize(dir);
			if (distance > 5.0f)
			{
				if (distance > 100.0f)
				{
					distance = 100.0f;
				}
				float speed = 400.0f - (400.0f - distance * 4.0f);
				EA_Move(ms->client, dir, speed);
				VectorCopy(dir, moveresult.movedir);
			}
		}
		return moveresult;
	}

	VectorSubtract(reach->start, ms->origin, reachdir);
	if ((ms->moveflags & MFL_SWIMMING) == 0)
	{
		reachdir[2] = 0.0f;
	}
	float distance1 = BotMove_VectorNormalize(reachdir);
	if (!MoverDown(reach))
	{
		float distance = distance1;
		VectorCopy(reachdir, final);
		BotCheckBlocked(ms, final, &moveresult);
		if (distance > 60.0f)
		{
			distance = 60.0f;
		}
		float speed = 360.0f - (360.0f - distance * 6.0f);
		if ((ms->moveflags & MFL_SWIMMING) == 0 &&
			!BotCheckBarrierJump(ms, final, 50.0f) && speed > 5.0f)
		{
			EA_Move(ms->client, final, speed);
		}
		VectorCopy(final, moveresult.movedir);
		if ((ms->moveflags & MFL_SWIMMING) != 0)
		{
			moveresult.flags |= MOVERESULT_SWIMVIEW;
		}
		moveresult.type = RESULTTYPE_ELEVATORUP;
		moveresult.flags |= MOVERESULT_WAITING;
		return moveresult;
	}

	MoverBottomCenter(reach, telegoal);
	VectorSubtract(telegoal, ms->origin, telegoaldir);
	if ((ms->moveflags & MFL_SWIMMING) == 0)
	{
		telegoaldir[2] = 0.0f;
	}
	float distance2 = BotMove_VectorNormalize(telegoaldir);
	float distance;
	if (distance1 < 20.0f || distance2 < distance1 ||
		DotProduct(telegoaldir, reachdir) < 0.0f)
	{
		distance = distance2;
		VectorCopy(telegoaldir, final);
	}
	else
	{
		distance = distance1;
		VectorCopy(reachdir, final);
	}
	BotCheckBlocked(ms, final, &moveresult);
	if (distance > 60.0f)
	{
		distance = 60.0f;
	}
	if ((ms->moveflags & MFL_SWIMMING) == 0 &&
		!BotCheckBarrierJump(ms, final, 50.0f))
	{
		float speed = 400.0f - (400.0f - distance * 6.0f);
		EA_Move(ms->client, final, speed);
	}
	VectorCopy(final, moveresult.movedir);
	if ((ms->moveflags & MFL_SWIMMING) != 0)
	{
		moveresult.flags |= MOVERESULT_SWIMVIEW;
	}
	return moveresult;
}

/*
=============
BotFinishTravel_Elevator

Move vertically toward the closer elevator-bottom or reach-end target.
=============
*/
bot_moveresult_t BotFinishTravel_Elevator(bot_movestate_t *ms,
	const aas_reachability_t *reach)
{
	bot_moveresult_t moveresult;
	vec3_t reachdir;
	vec3_t telegoaldir;
	vec3_t telegoal;
	BotClearMoveResult(&moveresult);
	MoverBottomCenter(reach, telegoal);
	VectorSubtract(telegoal, ms->origin, telegoaldir);
	VectorSubtract(reach->end, ms->origin, reachdir);
	if (fabsf(telegoaldir[2]) < fabsf(reachdir[2]))
	{
		BotMove_VectorNormalize(telegoaldir);
		EA_Move(ms->client, telegoaldir, 300.0f);
	}
	else
	{
		BotMove_VectorNormalize(reachdir);
		EA_Move(ms->client, reachdir, 300.0f);
	}
	return moveresult;
}

/*
=============
GrappleState

Return whether the retail grapple entity is moving or near the reach end.
=============
*/
int GrappleState(bot_movestate_t *ms, const aas_reachability_t *reach)
{
	(void)ms;
	if (libvar_laserhook == NULL)
	{
		libvar_laserhook = LibVar("laserhook", "0");
	}
	float laserhook = BotMove_LibVarValue(libvar_laserhook, 0.0f);
	if (laserhook == 0.0f && grapplemodelindex == 0)
	{
		grapplemodelindex = IndexFromModel("models/weapons/grapple/hook/tris.md2");
	}

	for (int entnum = AAS_NextEntity(0);
		entnum != 0;
		entnum = AAS_NextEntity(entnum))
	{
		if ((laserhook != 0.0f ||
			AAS_EntityModelindex(entnum) != grapplemodelindex) &&
			(laserhook == 0.0f ||
			(AAS_EntityRenderFX(entnum) & 0x80) == 0))
		{
			continue;
		}

		aas_entityinfo_t info;
		AAS_EntityInfo(entnum, &info);
		if (info.origin[0] != info.lastvisorigin[0] ||
			info.origin[1] != info.lastvisorigin[1] ||
			info.origin[2] != info.lastvisorigin[2])
		{
			return 1;
		}
		vec3_t delta;
		VectorSubtract(info.origin, reach->end, delta);
		if (sqrtf(DotProduct(delta, delta)) < 32.0f)
		{
			return 2;
		}
	}

	return 0;
}

/*
=============
BotResetGrapple

Release a grapple that no longer belongs to the active reachability.
=============
*/
static void BotResetGrapple(bot_movestate_t *ms)
{
	aas_reachability_t reach;
	AAS_ReachabilityFromNum(ms->lastreachnum, &reach);
	if (reach.traveltype != TRAVEL_GRAPPLEHOOK &&
		((ms->moveflags & MFL_GRAPPLEATTACHED) != 0 ||
		ms->grapplevisible_time != 0.0f))
	{
		EA_Command(ms->client, "hookoff", (char *)NULL);
		ms->moveflags &= ~MFL_GRAPPLEATTACHED;
		ms->grapplevisible_time = 0.0f;
	}
}

/*
=============
BotTravel_Grapple

Execute the retail grapple attach, visibility, pull, and reset state machine.
=============
*/
bot_moveresult_t BotTravel_Grapple(bot_movestate_t *ms,
	const aas_reachability_t *reach)
{
	bot_moveresult_t moveresult;
	BotClearMoveResult(&moveresult);
	if ((ms->moveflags & MFL_GRAPPLERESET) != 0)
	{
		EA_Command(ms->client, "hookoff", (char *)NULL);
		ms->moveflags &= ~MFL_GRAPPLEATTACHED;
		return moveresult;
	}

	if ((ms->moveflags & MFL_GRAPPLEATTACHED) != 0)
	{
		int state = GrappleState(ms, reach);
		vec3_t dir;
		dir[0] = reach->end[0] - ms->origin[0];
		dir[1] = reach->end[1] - ms->origin[1];
		dir[2] = 0.0f;
		float distance = sqrtf(DotProduct(dir, dir));
		if (state != 0)
		{
			if (distance < 48.0f)
			{
				if (ms->lastgrappledist - distance < 1.0f)
				{
					EA_Command(ms->client, "hookoff", (char *)NULL);
					ms->reachability_time = 0.0f;
					ms->moveflags =
						(ms->moveflags & ~MFL_GRAPPLEATTACHED) | MFL_GRAPPLERESET;
				}
				ms->lastgrappledist = distance;
				return moveresult;
			}
			if (state != 2 || ms->lastgrappledist - 2.0f >= distance)
			{
				ms->grapplevisible_time = AAS_Time();
				ms->lastgrappledist = distance;
				return moveresult;
			}
		}
		/* Retail subtracts a double literal, so the compare widens too. */
		if (AAS_Time() - 0.4 <= ms->grapplevisible_time)
		{
			ms->lastgrappledist = distance;
			return moveresult;
		}
		EA_Command(ms->client, "hookoff", (char *)NULL);
		ms->moveflags =
			(ms->moveflags & ~MFL_GRAPPLEATTACHED) | MFL_GRAPPLERESET;
		ms->reachability_time = 0.0f;
		return moveresult;
	}

	ms->grapplevisible_time = AAS_Time();
	vec3_t dir;
	VectorSubtract(reach->start, ms->origin, dir);
	if ((ms->moveflags & MFL_SWIMMING) == 0)
	{
		dir[2] = 0.0f;
	}
	vec3_t eye;
	vec3_t viewdir;
	VectorAdd(ms->viewoffset, ms->origin, eye);
	VectorSubtract(reach->end, eye, viewdir);
	float distance = BotMove_VectorNormalize(dir);
	Vector2Angles(viewdir, moveresult.ideal_viewangles);
	moveresult.flags |= MOVERESULT_MOVEMENTVIEW;
	if (distance >= 5.0f ||
		fabs(BotMove_AngleDiff(moveresult.ideal_viewangles[0], ms->viewangles[0])) >= 2.0 ||
		fabs(BotMove_AngleDiff(moveresult.ideal_viewangles[1], ms->viewangles[1])) >= 2.0)
	{
		float speed = distance >= 70.0f
			? 400.0f
			: 300.0f - (300.0f - distance * 4.0f);
		BotCheckBlocked(ms, dir, &moveresult);
		EA_Move(ms->client, dir, speed);
		VectorCopy(dir, moveresult.movedir);
	}
	else
	{
		EA_Command(ms->client, "hookon", (char *)NULL);
		/*
		 * Retail writes the raw pattern 0x497423f0 into lastgrappledist,
		 * which is exactly 999999.0f: a sentinel far enough away that the
		 * first attached frame always registers as closing distance.
		 */
		ms->lastgrappledist = 999999.0f;
		ms->moveflags |= MFL_GRAPPLEATTACHED;
	}

	int areanum = AAS_PointAreaNum(ms->origin);
	if (areanum != 0 && areanum != ms->reachareanum)
	{
		ms->reachability_time = 0.0f;
	}
	return moveresult;
}

/*
=============
BotTravel_RocketJump

Execute the Gladiator distance-gated rocket-jump action and view setup.
=============
*/
bot_moveresult_t BotTravel_RocketJump(bot_movestate_t *ms,
	const aas_reachability_t *reach)
{
	bot_moveresult_t moveresult;
	vec3_t dir;
	BotClearMoveResult(&moveresult);
	dir[0] = reach->start[0] - ms->origin[0];
	dir[1] = reach->start[1] - ms->origin[1];
	dir[2] = 0.0f;
	float distance = BotMove_VectorNormalize(dir);
	if (distance < 5.0f)
	{
		dir[0] = reach->end[0] - ms->origin[0];
		dir[1] = reach->end[1] - ms->origin[1];
		dir[2] = 0.0f;
		BotMove_VectorNormalize(dir);
		EA_Jump(ms->client);
		EA_Attack(ms->client);
		EA_Move(ms->client, dir, 800.0f);
		ms->jumpreach = ms->lastreachnum;
	}
	else
	{
		if (distance > 80.0f)
		{
			distance = 80.0f;
		}
		float speed = 400.0f - (400.0f - distance * 5.0f);
		EA_Move(ms->client, dir, speed);
	}
	Vector2Angles(dir, ms->viewangles);
	ms->viewangles[0] = 90.0f;
	EA_View(ms->client, ms->viewangles);
	moveresult.flags |= MOVERESULT_MOVEMENTVIEWSET;
	EA_UseItem(ms->client, "Rocket Launcher");
	VectorCopy(dir, moveresult.movedir);
	return moveresult;
}

/*
=============
BotEmptyMoveResult

Retain the original incremental-link empty-result helper.
=============
*/
bot_moveresult_t *BotEmptyMoveResult(bot_moveresult_t *result)
{
	bot_moveresult_t moveresult;
	BotClearMoveResult(&moveresult);
	*result = moveresult;
	return result;
}

/*
=============
BotFinishTravel_WeaponJump

Apply Gladiator's fixed-speed horizontal rocket-jump air control.
=============
*/
bot_moveresult_t BotFinishTravel_WeaponJump(bot_movestate_t *ms,
	const aas_reachability_t *reach)
{
	bot_moveresult_t moveresult;
	BotClearMoveResult(&moveresult);
	if (ms->jumpreach != 0)
	{
		vec3_t hordir;
		hordir[0] = reach->end[0] - ms->origin[0];
		hordir[1] = reach->end[1] - ms->origin[1];
		hordir[2] = 0.0f;
		BotMove_VectorNormalize(hordir);
		EA_Move(ms->client, hordir, 800.0f);
		VectorCopy(hordir, moveresult.movedir);
	}
	return moveresult;
}

/*
=============
BotReachabilityTime

Return the retail timeout for a reachability's travel type.
=============
*/
int BotReachabilityTime(const aas_reachability_t *reach)
{
	switch (reach->traveltype)
	{
		case TRAVEL_WALK:
		case TRAVEL_CROUCH:
		case TRAVEL_BARRIERJUMP:
		case TRAVEL_JUMP:
		case TRAVEL_WALKOFFLEDGE:
		case TRAVEL_SWIM:
		case TRAVEL_WATERJUMP:
		case TRAVEL_TELEPORT:
			return 5;
		case TRAVEL_LADDER:
		case TRAVEL_ROCKETJUMP:
			return 6;
		case TRAVEL_ELEVATOR:
			return 10;
		case TRAVEL_GRAPPLEHOOK:
			return 8;
		default:
			BotLib_Print(PRT_ERROR,
				"travel type %d not implemented yet\n",
				reach->traveltype);
			return 8;
	}
}

/*
=============
BotMoveInGoalArea

Move directly toward a goal already inside the bot's current area.
=============
*/
bot_moveresult_t BotMoveInGoalArea(bot_movestate_t *ms,
	const bot_goal_t *goal)
{
	bot_moveresult_t moveresult;
	vec3_t dir;
	BotClearMoveResult(&moveresult);
	dir[0] = goal->origin[0] - ms->origin[0];
	dir[1] = goal->origin[1] - ms->origin[1];
	if ((ms->moveflags & MFL_SWIMMING) != 0)
	{
		dir[2] = goal->origin[2] - ms->origin[2];
		moveresult.traveltype = TRAVEL_SWIM;
	}
	else
	{
		dir[2] = 0.0f;
		moveresult.traveltype = TRAVEL_WALK;
	}
	float distance = BotMove_VectorNormalize(dir);
	if (distance > 100.0f)
	{
		distance = 100.0f;
	}
	float speed = 400.0f - (400.0f - distance * 4.0f);
	if (speed < 10.0f)
	{
		speed = 0.0f;
	}
	BotCheckBlocked(ms, dir, &moveresult);
	EA_Move(ms->client, dir, speed);
	VectorCopy(dir, moveresult.movedir);
	if ((ms->moveflags & MFL_SWIMMING) != 0)
	{
		Vector2Angles(dir, moveresult.ideal_viewangles);
		moveresult.flags |= MOVERESULT_SWIMVIEW;
	}
	ms->lastreachnum = 0;
	ms->lastareanum = 0;
	ms->lastgoalareanum = goal->areanum;
	VectorCopy(ms->origin, ms->lastorigin);
	return moveresult;
}

/*
=============
BotMoveToGoal

Select, retain, and execute the retail reachability for one movement frame.
=============
*/
bot_moveresult_t *BotMoveToGoal(bot_moveresult_t *result,
	bot_movestate_t *movestate,
	const bot_goal_t *goal,
	int travelflags)
{
	bot_moveresult_t moveresult;
	aas_reachability_t reach;
	aas_reachability_t lastreach;
	BotClearMoveResult(&moveresult);
	BotResetGrapple(movestate);
	if (goal == NULL)
	{
		moveresult.failure = 1;
		*result = moveresult;
		return result;
	}

	movestate->moveflags &= ~(MFL_SWIMMING | MFL_AGAINSTLADDER);
	if (AAS_OnGround(movestate->origin,
		movestate->presencetype,
		movestate->entitynum))
	{
		movestate->moveflags |= MFL_ONGROUND;
	}
	if (AAS_Swimming(movestate->origin))
	{
		movestate->moveflags |= MFL_SWIMMING;
	}
	if (AAS_AgainstLadder(movestate->origin))
	{
		movestate->moveflags |= MFL_AGAINSTLADDER;
	}

	if ((movestate->moveflags &
		(MFL_ONGROUND | MFL_SWIMMING | MFL_AGAINSTLADDER)) != 0)
	{
		AAS_ReachabilityFromNum(movestate->lastreachnum, &lastreach);
		movestate->areanum = BotReachabilityArea(movestate->origin,
			lastreach.traveltype != TRAVEL_ELEVATOR);
		if (movestate->areanum == goal->areanum)
		{
			*result = BotMoveInGoalArea(movestate, goal);
			return result;
		}

		int reachnum = movestate->lastreachnum;
		int keepreach = 0;
		if (reachnum != 0)
		{
			AAS_ReachabilityFromNum(reachnum, &reach);
			if ((travelflags & AAS_TravelFlagForType(reach.traveltype)) != 0)
			{
				if (reach.traveltype == TRAVEL_GRAPPLEHOOK)
				{
					keepreach = AAS_Time() <= movestate->reachability_time &&
						(movestate->moveflags & MFL_GRAPPLERESET) == 0;
				}
				else if (reach.traveltype == TRAVEL_ELEVATOR)
				{
					keepreach = movestate->areanum != reach.areanum &&
						AAS_Time() <= movestate->reachability_time;
				}
				else
				{
					keepreach = movestate->lastgoalareanum == goal->areanum &&
						AAS_Time() <= movestate->reachability_time &&
						movestate->lastareanum == movestate->areanum;
				}
			}
		}

		if (!keepreach)
		{
			AAS_AreaReachability(movestate->areanum);
			reachnum = BotGetReachabilityToGoal(movestate->origin,
				movestate->areanum,
				movestate->lastgoalareanum,
				movestate->lastareanum,
				movestate->entitynum,
				movestate->avoidreach,
				movestate->avoidreachtimes,
				movestate->avoidreachtries,
				goal,
				travelflags);
			movestate->reachareanum = movestate->areanum;
			movestate->jumpreach = 0;
			movestate->moveflags &= ~MFL_GRAPPLERESET;
			if (reachnum != 0)
			{
				AAS_ReachabilityFromNum(reachnum, &reach);
				movestate->reachability_time =
					AAS_Time() + (float)BotReachabilityTime(&reach);
				BotAddToAvoidReach(movestate, reachnum, AVOIDREACH_TIME);
			}
		}

		movestate->lastreachnum = reachnum;
		movestate->lastareanum = movestate->areanum;
		movestate->lastgoalareanum = goal->areanum;
		if (reachnum != 0)
		{
			AAS_ReachabilityFromNum(reachnum, &reach);
			moveresult.traveltype = reach.traveltype;
			switch (reach.traveltype)
			{
				case TRAVEL_WALK:
					moveresult = BotTravel_Walk(movestate, &reach);
					break;
				case TRAVEL_CROUCH:
					moveresult = BotTravel_Crouch(movestate, &reach);
					break;
				case TRAVEL_BARRIERJUMP:
					moveresult = BotTravel_BarrierJump(movestate, &reach);
					break;
				case TRAVEL_LADDER:
					moveresult = BotTravel_Ladder(movestate, &reach);
					break;
				case TRAVEL_WALKOFFLEDGE:
					moveresult = BotTravel_WalkOffLedge(movestate, &reach);
					break;
				case TRAVEL_JUMP:
					moveresult = BotTravel_Jump(movestate, &reach);
					break;
				case TRAVEL_SWIM:
					moveresult = BotTravel_Swim(movestate, &reach);
					break;
				case TRAVEL_WATERJUMP:
					moveresult = BotTravel_WaterJump(movestate, &reach);
					break;
				case TRAVEL_TELEPORT:
					moveresult = BotTravel_Teleport(movestate, &reach);
					break;
				case TRAVEL_ELEVATOR:
					moveresult = BotTravel_Elevator(movestate, &reach);
					break;
				case TRAVEL_GRAPPLEHOOK:
					moveresult = BotTravel_Grapple(movestate, &reach);
					break;
				case TRAVEL_ROCKETJUMP:
					moveresult = BotTravel_RocketJump(movestate, &reach);
					break;
				default:
					BotLib_Print(PRT_FATAL,
						"travel type %d not implemented yet\n",
						reach.traveltype);
					break;
			}
		}
		else
		{
			moveresult.failure = 1;
		}
	}
	else if (movestate->lastreachnum != 0)
	{
		AAS_ReachabilityFromNum(movestate->lastreachnum, &reach);
		moveresult.traveltype = reach.traveltype;
		switch (reach.traveltype)
		{
			case TRAVEL_WALK:
				moveresult = BotTravel_Walk(movestate, &reach);
				break;
			case TRAVEL_CROUCH:
			case TRAVEL_TELEPORT:
				break;
			case TRAVEL_BARRIERJUMP:
				moveresult = BotFinishTravel_BarrierJump(movestate, &reach);
				break;
			case TRAVEL_LADDER:
				moveresult = BotTravel_Ladder(movestate, &reach);
				break;
			case TRAVEL_WALKOFFLEDGE:
				moveresult = BotFinishTravel_WalkOffLedge(movestate, &reach);
				break;
			case TRAVEL_JUMP:
				moveresult = BotFinishTravel_Jump(movestate, &reach);
				break;
			case TRAVEL_SWIM:
				moveresult = BotTravel_Swim(movestate, &reach);
				break;
			case TRAVEL_WATERJUMP:
				moveresult = BotFinishTravel_WaterJump(movestate, &reach);
				break;
			case TRAVEL_ELEVATOR:
				moveresult = BotFinishTravel_Elevator(movestate, &reach);
				break;
			case TRAVEL_GRAPPLEHOOK:
				moveresult = BotTravel_Grapple(movestate, &reach);
				break;
			case TRAVEL_ROCKETJUMP:
				moveresult = BotFinishTravel_WeaponJump(movestate, &reach);
				break;
			default:
				BotLib_Print(PRT_FATAL,
					"(last) travel type %d not implemented yet\n",
					reach.traveltype);
				break;
		}
	}

	if (moveresult.blocked)
	{
		movestate->reachability_time -= movestate->thinktime * 10.0f;
	}
	VectorCopy(movestate->origin, movestate->lastorigin);
	*result = moveresult;
	return result;
}

/*
=============
BotResetAvoidReach

Clear the one-entry retail avoid-reach arrays and return the tries slot.
=============
*/
int *BotResetAvoidReach(bot_movestate_t *movestate)
{
	memset(movestate->avoidreach, 0, sizeof(movestate->avoidreach));
	memset(movestate->avoidreachtimes, 0, sizeof(movestate->avoidreachtimes));
	memset(movestate->avoidreachtries, 0, sizeof(movestate->avoidreachtries));
	return movestate->avoidreachtries;
}

/*
=============
BotResetLastAvoidReach

Expire the latest retail avoid-reach entry and reduce its retry count.
=============
*/
void BotResetLastAvoidReach(bot_movestate_t *movestate)
{
	float latesttime = 0.0f;
	int latest = 0;
	for (int index = 0; index < MAX_AVOIDREACH; ++index)
	{
		if (movestate->avoidreachtimes[index] > latesttime)
		{
			latesttime = movestate->avoidreachtimes[index];
			latest = index;
		}
	}
	if (latesttime != 0.0f)
	{
		movestate->avoidreachtimes[latest] = 0.0f;
		/*
		 * Retail guards the decrement with a load from movestate + 0x80,
		 * one dword past the single avoid-reach retry slot and past the end
		 * of the 128-byte movement state itself.  In the original the state
		 * is embedded in the larger per-client record, so that read lands on
		 * an unrelated neighbouring field; here the movement state is
		 * allocated on its own, so the guard reads the slot the original
		 * source clearly meant and that the decrement itself uses.
		 */
		if (movestate->avoidreachtries[latest] > 0)
		{
			--movestate->avoidreachtries[latest];
		}
	}
}

/*
=============
BotResetMoveState

Clear the exact 128-byte retail movement state and return zero.
=============
*/
int BotResetMoveState(bot_movestate_t *movestate)
{
	memset(movestate, 0, 0x80);
	return 0;
}

/*
=============
BotAllocMoveStateHandle

Allocate a reconstruction-only handle around a retail movement state.
=============
*/
int BotAllocMoveStateHandle(void)
{
	for (int handle = 1; handle <= MAX_CLIENTS; ++handle)
	{
		if (botmovestates[handle] == NULL)
		{
			botmovestates[handle] = GetClearedMemory(sizeof(bot_movestate_t));
			if (botmovestates[handle] != NULL)
			{
				botmovestate_owned[handle] = true;
				return handle;
			}
			return 0;
		}
	}
	return 0;
}

/*
=============
BotBindRetailMoveState

Binds the exact 0x80-byte movement core embedded in a retail client slab to a
reserved internal handle without allocating a tracked owner block.
=============
*/
int BotBindRetailMoveState(int client, bot_movestate_t *state)
{
	if (client < 0 || client > MAX_CLIENTS || state == NULL)
	{
		return 0;
	}

	int handle = BOT_MOVE_RETAIL_HANDLE_BASE + client;
	if (botmovestates[handle] == state)
	{
		return handle;
	}
	if (botmovestates[handle] != NULL)
	{
		BotFreeMoveStateHandle(handle);
	}

	memset(state, 0, sizeof(*state));
	botmovestates[handle] = state;
	botmovestate_owned[handle] = false;
	return handle;
}

/*
=============
BotRebindRetailMoveState

Moves reserved movement-handle metadata after a full client record copy.
=============
*/
int BotRebindRetailMoveState(int old_handle,
	int client,
	bot_movestate_t *state)
{
	if (client < 0 || client > MAX_CLIENTS || state == NULL)
	{
		return 0;
	}

	int new_handle = BOT_MOVE_RETAIL_HANDLE_BASE + client;
	if (old_handle == new_handle)
	{
		botmovestates[new_handle] = state;
		botmovestate_owned[new_handle] = false;
		return new_handle;
	}
	if (old_handle <= 0 || old_handle > BOT_MOVE_HANDLE_LIMIT ||
		botmovestates[old_handle] == NULL)
	{
		return BotBindRetailMoveState(client, state);
	}
	if (botmovestates[new_handle] != NULL)
	{
		BotFreeMoveStateHandle(new_handle);
	}

	botmovestates[new_handle] = state;
	botmovestate_owned[new_handle] = false;
	botmovestates[old_handle] = NULL;
	botmovestate_owned[old_handle] = false;
	return new_handle;
}

/*
=============
BotFreeMoveStateHandle

Free a reconstruction-only movement-state handle.
=============
*/
void BotFreeMoveStateHandle(int handle)
{
	if (handle <= 0 || handle > BOT_MOVE_HANDLE_LIMIT ||
		botmovestates[handle] == NULL)
	{
		return;
	}
	if (botmovestate_owned[handle])
	{
		FreeMemory(botmovestates[handle]);
	}
	botmovestates[handle] = NULL;
	botmovestate_owned[handle] = false;
}

/*
=============
BotMoveStateFromHandle

Resolve a reconstruction-only movement-state handle.
=============
*/
bot_movestate_t *BotMoveStateFromHandle(int handle)
{
	if (handle <= 0 || handle > BOT_MOVE_HANDLE_LIMIT)
	{
		return NULL;
	}
	return botmovestates[handle];
}

/*
=============
BotInitMoveStateHandle

Refresh retail frame inputs without disturbing reachability bookkeeping.
=============
*/
void BotInitMoveStateHandle(int handle, const bot_initmove_t *initmove)
{
	bot_movestate_t *movestate = BotMoveStateFromHandle(handle);
	if (movestate == NULL || initmove == NULL)
	{
		return;
	}
	VectorCopy(initmove->origin, movestate->origin);
	VectorCopy(initmove->velocity, movestate->velocity);
	VectorCopy(initmove->viewoffset, movestate->viewoffset);
	movestate->entitynum = initmove->entitynum;
	movestate->client = initmove->client;
	movestate->thinktime = initmove->thinktime;
	movestate->presencetype = initmove->presencetype;
	VectorCopy(initmove->viewangles, movestate->viewangles);
	movestate->moveflags &= ~(MFL_ONGROUND | MFL_TELEPORTED | MFL_WATERJUMP);
	movestate->moveflags |= initmove->or_moveflags &
		(MFL_ONGROUND | MFL_TELEPORTED | MFL_WATERJUMP);
}

/*
=============
BotMoveToGoalHandle

Bridge an extended-API movement handle to the retail pointer core.
=============
*/
void BotMoveToGoalHandle(bot_moveresult_t *result,
	int movestate,
	const bot_goal_t *goal,
	int travelflags)
{
	if (result == NULL)
	{
		return;
	}
	bot_movestate_t *state = BotMoveStateFromHandle(movestate);
	if (state == NULL)
	{
		BotClearMoveResult(result);
		result->failure = 1;
		return;
	}
	BotMoveToGoal(result, state, goal, travelflags);
}

/*
=============
BotMoveInDirectionHandle

Bridge an extended-API movement handle to retail direct movement.
=============
*/
int BotMoveInDirectionHandle(int movestate,
	const vec3_t dir,
	float speed,
	int type)
{
	bot_movestate_t *state = BotMoveStateFromHandle(movestate);
	if (state == NULL || dir == NULL)
	{
		return 0;
	}
	return BotMoveInDirection(state, dir, speed, type);
}

/*
=============
BotResetMoveStateHandle

Bridge an extended-API movement handle to the retail full reset.
=============
*/
void BotResetMoveStateHandle(int movestate)
{
	bot_movestate_t *state = BotMoveStateFromHandle(movestate);
	if (state != NULL)
	{
		BotResetMoveState(state);
	}
}

/*
=============
BotResetAvoidReachHandle

Bridge an extended-API movement handle to the retail avoid reset.
=============
*/
void BotResetAvoidReachHandle(int movestate)
{
	bot_movestate_t *state = BotMoveStateFromHandle(movestate);
	if (state != NULL)
	{
		BotResetAvoidReach(state);
	}
}

/*
=============
BotResetLastAvoidReachHandle

Bridge an extended-API movement handle to the retail latest-avoid reset.
=============
*/
void BotResetLastAvoidReachHandle(int movestate)
{
	bot_movestate_t *state = BotMoveStateFromHandle(movestate);
	if (state != NULL)
	{
		BotResetLastAvoidReach(state);
	}
}

/*
=============
BotMovementViewTargetHandle

Bridge an extended-API handle while ignoring successor-only lookahead.
=============
*/
int BotMovementViewTargetHandle(int movestate,
	const bot_goal_t *goal,
	int travelflags,
	float lookahead,
	vec3_t target)
{
	(void)lookahead;
	bot_movestate_t *state = BotMoveStateFromHandle(movestate);
	if (state == NULL || target == NULL)
	{
		return 0;
	}
	return BotMovementViewTarget(state, goal, travelflags, target);
}

/*
=============
BotPredictVisiblePosition

Retain the successor-only prediction seam without changing retail state.
=============
*/
int BotPredictVisiblePosition(const vec3_t origin,
	int areanum,
	const bot_goal_t *goal,
	int travelflags,
	vec3_t target)
{
	(void)origin;
	(void)areanum;
	(void)goal;
	(void)travelflags;
	(void)target;
	return 0;
}

/*
=============
BotAddAvoidSpot

Ignore successor-only avoid spots without extending the retail movement state.
=============
*/
void BotAddAvoidSpot(int movestate, vec3_t origin, float radius, int type)
{
	(void)movestate;
	(void)origin;
	(void)radius;
	(void)type;
}

/*
=============
BotSetBrushModelTypes

Retain the successor-only map hook as a compatibility no-op.
=============
*/
void BotSetBrushModelTypes(void)
{
}

/*
=============
BotSetupMoveAI

Register all sixteen retail movement libvars with their exact defaults.
=============
*/
int BotSetupMoveAI(void)
{
	libvar_sv_friction = LibVar("sv_friction", "6");
	libvar_sv_stopspeed = LibVar("sv_stopspeed", "100");
	libvar_sv_gravity = LibVar("sv_gravity", "800");
	libvar_sv_waterfriction = LibVar("sv_waterfriction", "1");
	libvar_sv_watergravity = LibVar("sv_watergravity", "400");
	libvar_sv_maxvelocity = LibVar("sv_maxvelocity", "300");
	libvar_sv_maxwalkvelocity = LibVar("sv_maxwalkvelocity", "300");
	libvar_sv_maxcrouchvelocity = LibVar("sv_maxcrouchvelocity", "100");
	libvar_sv_maxswimvelocity = LibVar("sv_maxswimvelocity", "150");
	libvar_sv_maxaccelerate = LibVar("sv_maxacceleration", "2200");
	libvar_sv_airaccelerate = LibVar("sv_airaccelerate", "0");
	libvar_sv_step = LibVar("sv_step", "18");
	libvar_sv_maxbarrier = LibVar("sv_maxbarrier", "50");
	libvar_sv_maxsteepness = LibVar("sv_maxsteepness", "0.7");
	libvar_sv_jumpvel = LibVar("sv_jumpvel", "224");
	libvar_sv_maxwaterjump = LibVar("sv_maxwaterjump", "21");
	/*
	 * The original falls off the end without a return statement, so it hands
	 * back whatever the last LibVar call left in the return register.  Every
	 * caller only tests the result for zero, so report success through the
	 * same libvar instead of reproducing the missing return.
	 */
	return libvar_sv_maxwaterjump != NULL;
}

/*
=============
BotShutdownMoveAI

Invalidate reconstruction-only handle and libvar caches without freeing storage.
=============
*/
void BotShutdownMoveAI(void)
{
	memset(botmovestates, 0, sizeof(botmovestates));
	memset(botmovestate_owned, 0, sizeof(botmovestate_owned));
	libvar_sv_friction = NULL;
	libvar_sv_stopspeed = NULL;
	libvar_sv_gravity = NULL;
	libvar_sv_waterfriction = NULL;
	libvar_sv_watergravity = NULL;
	libvar_sv_maxvelocity = NULL;
	libvar_sv_maxwalkvelocity = NULL;
	libvar_sv_maxcrouchvelocity = NULL;
	libvar_sv_maxswimvelocity = NULL;
	libvar_sv_maxaccelerate = NULL;
	libvar_sv_airaccelerate = NULL;
	libvar_sv_step = NULL;
	libvar_sv_maxbarrier = NULL;
	libvar_sv_maxsteepness = NULL;
	libvar_sv_jumpvel = NULL;
	libvar_sv_maxwaterjump = NULL;
	libvar_laserhook = NULL;
	grapplemodelindex = 0;
}
