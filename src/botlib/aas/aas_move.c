#include "aas_local.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "aas_debug.h"
#include "botlib/common/l_utils.h"
#include "q2bridge/bridge.h"
#include "q2bridge/bridge_config.h"

#define AAS_RETAIL_DEFAULT_ENDCONTENTS 4

/*
=============
AAS_MoveLibVarValue

Read a movement tuning variable with a retail fallback.
=============
*/
static float AAS_MoveLibVarValue(const libvar_t *var, float fallback)
{
	if (var == NULL)
	{
		return fallback;
	}

	return var->value;
}

/*
=============
AAS_MoveVectorNormalize

Normalize a vector in place and return its original length.
=============
*/
static float AAS_MoveVectorNormalize(vec3_t v)
{
	float length = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
	if (length > 0.0f)
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

/*
=============
AAS_DropToFloor

Drop an item-sized bounding box at most 100 units onto retail solid geometry.
=============
*/
int AAS_DropToFloor(vec3_t origin, const vec3_t mins, const vec3_t maxs)
{
	if (origin == NULL)
	{
		return qfalse;
	}

	vec3_t end;
	VectorCopy(origin, end);
	end[2] -= 100.0f;
	bsp_trace_t trace = AAS_Trace(origin, mins, maxs, end, 0, MASK_SOLID);
	if (trace.startsolid)
	{
		return qfalse;
	}

	VectorCopy(trace.endpos, origin);
	return qtrue;
}

/*
=============
AAS_WeaponJumpZVelocity

Calculate the retail vertical knockback velocity from a downward weapon shot.
=============
*/
float AAS_WeaponJumpZVelocity(const vec3_t origin, float radiusdamage)
{
	if (origin == NULL)
	{
		return AAS_MoveLibVarValue(Bridge_JumpVelocity(), 224.0f);
	}

	const vec3_t rocketoffset = {8.0f, 8.0f, -8.0f};
	const vec3_t botmins = {-16.0f, -16.0f, -24.0f};
	const vec3_t botmaxs = {16.0f, 16.0f, 32.0f};
	const vec3_t forward = {0.0f, 0.0f, -1.0f};
	const vec3_t right = {0.0f, -1.0f, 0.0f};

	vec3_t start;
	VectorCopy(origin, start);
	start[2] += 8.0f;
	start[0] += forward[0] * rocketoffset[0] +
		right[0] * rocketoffset[1];
	start[1] += forward[1] * rocketoffset[0] +
		right[1] * rocketoffset[1];
	start[2] += forward[2] * rocketoffset[0] +
		right[2] * rocketoffset[1] + rocketoffset[2];

	vec3_t end;
	VectorMA(start, 500.0f, forward, end);
	bsp_trace_t trace = AAS_Trace(start, NULL, NULL, end, 1, MASK_SOLID);

	vec3_t center;
	VectorAdd(botmins, botmaxs, center);
	VectorMA(origin, 0.5f, center, center);
	vec3_t offset;
	VectorSubtract(trace.endpos, center, offset);
	float points = radiusdamage - 0.5f *
		sqrtf(DotProduct(offset, offset));
	if (points < 0.0f)
	{
		points = 0.0f;
	}
	points *= 0.5f;

	vec3_t direction;
	VectorSubtract(origin, trace.endpos, direction);
	AAS_MoveVectorNormalize(direction);
	vec3_t knockbackvelocity;
	VectorScale(direction, 1600.0f * points / 200.0f,
		knockbackvelocity);
	return knockbackvelocity[2] +
		AAS_MoveLibVarValue(Bridge_JumpVelocity(), 224.0f);
}

/*
=============
AAS_RocketJumpZVelocity

Return the retail rocket-jump vertical velocity for a 120-damage blast.
=============
*/
float AAS_RocketJumpZVelocity(const vec3_t origin)
{
	return AAS_WeaponJumpZVelocity(origin, 120.0f);
}

/*
=============
AAS_BFGJumpZVelocity

Preserve the retail BFG wrapper's original 120-damage argument.
=============
*/
float AAS_BFGJumpZVelocity(const vec3_t origin)
{
	return AAS_WeaponJumpZVelocity(origin, 120.0f);
}

/*
=============
AAS_MoveApplyFriction

Apply Q3-style horizontal friction to a velocity vector in units per second.
=============
*/
static void AAS_MoveApplyFriction(vec3_t velocity, float friction, float stopspeed, float frametime)
{
	float speed = sqrtf(velocity[0] * velocity[0] + velocity[1] * velocity[1]);
	if (speed <= 0.0f)
	{
		return;
	}

	float control = (speed < stopspeed) ? stopspeed : speed;
	float newspeed = speed - frametime * control * friction;
	if (newspeed < 0.0f)
	{
		newspeed = 0.0f;
	}
	newspeed /= speed;

	velocity[0] *= newspeed;
	velocity[1] *= newspeed;
}

/*
=============
AAS_MoveApplyCommandVelocity

Apply Gladiator's per-axis command acceleration and mode velocity clamps.
=============
*/
static void AAS_MoveApplyCommandVelocity(vec3_t velocity,
	const vec3_t cmdmove,
	int axes,
	float frametime,
	float maxacceleration,
	float maxvelocity)
{
	float frame_maxacceleration = maxacceleration * frametime;
	float frame_maxvelocity = maxvelocity * frametime;

	for (int i = 0; i < axes; ++i)
	{
		float velocity_change = cmdmove[i] * frametime - velocity[i];
		if (velocity_change > frame_maxacceleration)
		{
			velocity_change = frame_maxacceleration;
		}
		else if (velocity_change < -frame_maxacceleration)
		{
			velocity_change = -frame_maxacceleration;
		}

		float new_velocity = velocity[i] + velocity_change;
		if (new_velocity > frame_maxvelocity)
		{
			velocity[i] = frame_maxvelocity;
		}
		else if (new_velocity < -frame_maxvelocity)
		{
			velocity[i] = -frame_maxvelocity;
		}
		else
		{
			velocity[i] = new_velocity;
		}
	}
}

/*
=============
AAS_MoveTraceClientBBox

Trace the AAS presence bounds through the retail AAS client trace path.
=============
*/
static aas_trace_t AAS_MoveTraceClientBBox(const vec3_t start,
                                           const vec3_t end,
                                           int presencetype,
                                           int passent)
{
	return AAS_TraceClientBBox(start, end, presencetype, passent);
}

/*
=============
AAS_MoveFeetContentsEvents

Translate the retail feet probe's low contents byte into stop events.  The
original maps both slime and water to SE_ENTERSLIME.
=============
*/
static int AAS_MoveFeetContentsEvents(const vec3_t point, int *outcontents)
{
	int contents = 0;
	if (point != NULL)
	{
		vec3_t sample;
		VectorCopy(point, sample);
		contents = Q2_PointContents(sample);
	}
	if (outcontents != NULL)
	{
		*outcontents = contents;
	}

	int events = SE_NONE;
	if ((contents & CONTENTS_LAVA) != 0)
	{
		events |= SE_ENTERLAVA;
	}
	if ((contents & (CONTENTS_SLIME | CONTENTS_WATER)) != 0)
	{
		events |= SE_ENTERSLIME;
	}

	return events;
}

/*
=============
AAS_MoveStoreTrace

Copy the internal trace prefix into the retail 0x24-byte result trace.
=============
*/
static void AAS_MoveStoreTrace(aas_clientmove_trace_t *destination,
	const aas_trace_t *source)
{
	if (destination == NULL || source == NULL)
	{
		return;
	}

	destination->startsolid = source->startsolid;
	destination->fraction = source->fraction;
	VectorCopy(source->endpos, destination->endpos);
	destination->ent = source->ent;
	destination->lastarea = source->lastarea;
	destination->area = source->area;
	destination->planenum = source->planenum;
}

/*
=============
AAS_MoveStoreClientMove

Populate the public prediction result using the retail frame displacement.
=============
*/
static void AAS_MoveStoreClientMove(aas_clientmove_t *move,
                                    const vec3_t endpos,
                                    const vec3_t frame_velocity,
                                    const aas_trace_t *trace,
                                    int presencetype,
                                    int stopevent,
                                    int endcontents,
                                    float frametime,
                                    int frame)
{
	if (move == NULL)
	{
		return;
	}

	VectorCopy(endpos, move->endpos);
	VectorCopy(frame_velocity, move->velocity);
	if (trace != NULL)
	{
		AAS_MoveStoreTrace(&move->trace, trace);
	}
	move->presencetype = presencetype;
	move->stopevent = stopevent;
	move->endcontents = endcontents;
	move->time = (float)frame * frametime;
	move->frames = frame;
}

/*
=============
AAS_Swimming

Return true if the origin is in water, slime, or lava.
=============
*/
int AAS_Swimming(const vec3_t origin)
{
	if (origin == NULL)
	{
		return qfalse;
	}

	vec3_t sample;
	VectorCopy(origin, sample);
	sample[2] -= 2.0f;

	return (Q2_PointContents(sample) &
		(CONTENTS_LAVA | CONTENTS_SLIME | CONTENTS_WATER)) != 0;
}

/*
=============
AAS_OnGround

Trace down from the origin and test the impact plane steepness.
=============
*/
int AAS_OnGround(const vec3_t origin, int presencetype, int passent)
{
	if (origin == NULL)
	{
		return qfalse;
	}

	vec3_t end;
	VectorCopy(origin, end);
	end[2] -= 4.0f;

	aas_trace_t trace = AAS_MoveTraceClientBBox(origin, end, presencetype, passent);
	if (trace.startsolid || trace.fraction >= 1.0f)
	{
		return qfalse;
	}
	if (origin[2] - trace.endpos[2] > 2.0f)
	{
		return qfalse;
	}

	const aas_plane_t *plane = AAS_PlaneFromNum(trace.planenum);
	if (plane == NULL)
	{
		return qfalse;
	}

	float maxsteepness = AAS_MoveLibVarValue(Bridge_MaxSteepness(), 0.7f);
	return plane->normal[2] >= maxsteepness;
}

/*
=============
AAS_JumpReachRunStart

Return the run-up point used before a jump reachability.
=============
*/
void AAS_JumpReachRunStart(const aas_reachability_t *reach, vec3_t runstart)
{
	if (runstart == NULL)
	{
		return;
	}
	if (reach == NULL)
	{
		VectorClear(runstart);
		return;
	}

	vec3_t hordir;
	hordir[0] = reach->start[0] - reach->end[0];
	hordir[1] = reach->start[1] - reach->end[1];
	hordir[2] = 0.0f;
	/* Retail (sub_1000f010 @0x1000f03a) calls VectorNormalize unconditionally
	   and proceeds with a zero cmdmove on a degenerate direction; there is no
	   zero-length early-out. */
	AAS_MoveVectorNormalize(hordir);

	vec3_t start;
	VectorCopy(reach->start, start);
	start[2] += 1.0f;

	vec3_t velocity;
	VectorClear(velocity);
	vec3_t cmdmove;
	VectorScale(hordir, 400.0f, cmdmove);

	/* Retail runs two 0.1s frames of real client movement prediction away from
	   the jump start (0x1000f097) rather than extrapolating a fixed distance,
	   so walls, ledges and steps clip the run-up point.  The return value is
	   deliberately ignored: retail has no boolean and copies the scratch move
	   on the clip-overflow path, which AAS_PredictClientMovement reproduces by
	   clearing *move before returning qfalse. */
	aas_clientmove_t move;
	AAS_PredictClientMovement(&move,
		-1,
		start,
		PRESENCE_NORMAL,
		qtrue,
		velocity,
		cmdmove,
		1,
		2,
		0.1f,
		SE_ENTERWATER | SE_ENTERSLIME | SE_ENTERLAVA |
			SE_HITGROUNDDAMAGE | SE_GAP,
		0,
		qfalse);

	VectorCopy(move.endpos, runstart);
	/* 0x1000f0d0 tests move.stopevent (move+0x40) against 0x38, so only slime,
	   lava and landing damage fall back to the jump start itself. */
	if ((move.stopevent &
		(SE_ENTERSLIME | SE_ENTERLAVA | SE_HITGROUNDDAMAGE)) != 0)
	{
		VectorCopy(start, runstart);
	}
}

/*
=============
AAS_AgainstLadder

Check whether the origin touches a ladder face in a normal-presence ladder
area, including the retail one-unit area-boundary probes.
=============
*/
int AAS_AgainstLadder(const vec3_t origin)
{
	if (origin == NULL)
	{
		return qfalse;
	}

	vec3_t area_origin;
	VectorCopy(origin, area_origin);
	int areanum = AAS_PointAreaNum(area_origin);
	if (areanum == 0)
	{
		area_origin[0] += 1.0f;
		areanum = AAS_PointAreaNum(area_origin);
		if (areanum == 0)
		{
			area_origin[1] += 1.0f;
			areanum = AAS_PointAreaNum(area_origin);
			if (areanum == 0)
			{
				area_origin[0] -= 2.0f;
				areanum = AAS_PointAreaNum(area_origin);
				if (areanum == 0)
				{
					area_origin[1] -= 2.0f;
					areanum = AAS_PointAreaNum(area_origin);
				}
			}
		}
	}

	if (areanum <= 0 ||
		aasworld.areas == NULL ||
		aasworld.areasettings == NULL ||
		aasworld.faceIndex == NULL ||
		aasworld.faces == NULL ||
		aasworld.planes == NULL ||
		areanum >= aasworld.numAreas ||
		areanum >= aasworld.numAreaSettings)
	{
		return qfalse;
	}

	const aas_areasettings_t *settings = &aasworld.areasettings[areanum];
	if ((settings->areaflags & AAS_AREA_LADDER) == 0 ||
		(settings->presencetype & PRESENCE_NORMAL) == 0)
	{
		return qfalse;
	}

	const aas_area_t *area = &aasworld.areas[areanum];
	if (area->firstface < 0 ||
		area->numfaces < 0 ||
		area->numfaces > aasworld.faceIndexSize ||
		area->firstface > aasworld.faceIndexSize - area->numfaces)
	{
		return qfalse;
	}

	for (int index = 0; index < area->numfaces; ++index)
	{
		int signed_facenum = aasworld.faceIndex[area->firstface + index];
		if (signed_facenum == INT_MIN)
		{
			continue;
		}

		int side = signed_facenum < 0;
		int facenum = abs(signed_facenum);
		if (facenum < 0 || facenum >= aasworld.numFaces)
		{
			continue;
		}

		const aas_face_t *face = &aasworld.faces[facenum];
		if ((face->faceflags & AAS_FACE_LADDER) == 0)
		{
			continue;
		}

		int planenum = face->planenum ^ side;
		if (planenum < 0 || planenum >= aasworld.numPlanes)
		{
			continue;
		}

		const aas_plane_t *plane = &aasworld.planes[planenum];
		if (fabsf(DotProduct(plane->normal, origin) - plane->dist) < 3.0f &&
			AAS_PointInsideFace(facenum, origin, 0.1f))
		{
			return qtrue;
		}
	}

	return qfalse;
}

/*
=============
AAS_HorizontalVelocityForJump

Calculate the horizontal speed needed to fall from start to end.
=============
*/
int AAS_HorizontalVelocityForJump(float zvel, const vec3_t start, const vec3_t end, float *velocity)
{
	if (start == NULL || end == NULL || velocity == NULL)
	{
		return qfalse;
	}

	float gravity = AAS_MoveLibVarValue(Bridge_Gravity(), 800.0f);
	float maxvelocity = AAS_MoveLibVarValue(Bridge_MaxVelocity(), 300.0f);

	/*
	 * Retail divides by sv_gravity unguarded, so a zero or negative value
	 * propagates infinities and NaNs rather than short-circuiting.
	 */
	float jump_time = zvel / gravity;
	float maxjump = 0.5f * gravity * jump_time * jump_time;
	float top = start[2] + maxjump;
	float height2fall = top - end[2];
	/*
	 * 0x10010837 tests C0 (`test ah,1`), which the x87 also sets when the
	 * compare is unordered, so a NaN height2fall takes this branch too - the
	 * exact case sv_gravity 0 produces.  Plain `< 0.0f` would fall through.
	 */
	if (!(height2fall >= 0.0f))
	{
		*velocity = maxvelocity;
		return qfalse;
	}

	float fall_time = sqrtf(height2fall / (0.5f * gravity));
	float total_time = fall_time + jump_time;

	vec3_t dir;
	VectorSubtract(end, start, dir);
	*velocity = sqrtf(dir[0] * dir[0] + dir[1] * dir[1]) / total_time;
	if (*velocity > maxvelocity)
	{
		*velocity = maxvelocity;
		return qfalse;
	}

	return qtrue;
}

/*
=============
AAS_PredictClientMovement

Predict client movement through the Gladiator retail stop-event state machine.
=============
*/
int AAS_PredictClientMovement(aas_clientmove_t *move,
                              int entnum,
                              const vec3_t origin,
                              int presencetype,
                              int onground,
                              const vec3_t velocity,
                              const vec3_t cmdmove,
                              int cmdframes,
                              int maxframes,
                              float frametime,
                              int stopevent,
                              int stopareanum,
                              int visualize)
{
	(void)stopareanum;

	if (move == NULL || origin == NULL || velocity == NULL || cmdmove == NULL)
	{
		return qfalse;
	}

	memset(move, 0, sizeof(*move));
	unsigned char retail_stopevent = (unsigned char)stopevent;

	float phys_friction = AAS_MoveLibVarValue(Bridge_Friction(), 6.0f);
	float phys_stopspeed = AAS_MoveLibVarValue(Bridge_StopSpeed(), 100.0f);
	float phys_gravity = AAS_MoveLibVarValue(Bridge_Gravity(), 800.0f);
	float phys_watergravity = AAS_MoveLibVarValue(Bridge_WaterGravity(), 400.0f);
	float phys_waterfriction = AAS_MoveLibVarValue(Bridge_WaterFriction(), 1.0f);
	float phys_maxwalkvelocity = AAS_MoveLibVarValue(Bridge_MaxWalkVelocity(), 300.0f);
	float phys_maxcrouchvelocity = AAS_MoveLibVarValue(Bridge_MaxCrouchVelocity(), 100.0f);
	float phys_maxswimvelocity = AAS_MoveLibVarValue(Bridge_MaxSwimVelocity(), 150.0f);
	float phys_maxacceleration = AAS_MoveLibVarValue(Bridge_MaxAcceleration(), 2200.0f);
	float phys_maxstep = AAS_MoveLibVarValue(Bridge_MaxStep(), 18.0f);
	float phys_maxbarrier = AAS_MoveLibVarValue(Bridge_MaxBarrier(), 50.0f);
	float phys_maxsteepness = AAS_MoveLibVarValue(Bridge_MaxSteepness(), 0.7f);
	float phys_jumpvel = AAS_MoveLibVarValue(Bridge_JumpVelocity(), 224.0f) * frametime;

	vec3_t org;
	VectorCopy(origin, org);
	org[2] += 0.25f;

	vec3_t frame_test_vel;
	VectorScale(velocity, frametime, frame_test_vel);

	aas_trace_t trace;
	memset(&trace, 0, sizeof(trace));
	trace.fraction = 1.0f;

	int jump_frame = -1;
	int frame = 0;
	for (; frame < maxframes; ++frame)
	{
		int swimming = AAS_Swimming(org);
		float gravity = swimming ? phys_watergravity : phys_gravity;
		frame_test_vel[2] -= gravity * 0.1f * frametime;

		if (onground || swimming)
		{
			float friction = swimming ? phys_friction : phys_waterfriction;
			VectorScale(frame_test_vel, 10.0f, frame_test_vel);
			AAS_MoveApplyFriction(frame_test_vel, friction, phys_stopspeed, frametime);
			VectorScale(frame_test_vel, 0.1f, frame_test_vel);
		}

		int crouch = qfalse;
		if (frame < cmdframes)
		{
			float maxvel = phys_maxwalkvelocity;
			int command_axes = 0;

			if (onground)
			{
				if (cmdmove[2] < -300.0f)
				{
					crouch = qtrue;
					maxvel = phys_maxcrouchvelocity;
				}
				if (!swimming && cmdmove[2] > 1.0f)
				{
					frame_test_vel[2] = phys_jumpvel - gravity * 0.1f * frametime + 5.0f;
					jump_frame = frame;
				}
				command_axes = 2;
			}
			if (swimming)
			{
				maxvel = phys_maxswimvelocity;
				command_axes = 3;
			}

			if (command_axes > 0)
			{
				AAS_MoveApplyCommandVelocity(frame_test_vel,
					cmdmove,
					command_axes,
					frametime,
					phys_maxacceleration,
					maxvel);
			}
		}

		if (crouch)
		{
			presencetype = PRESENCE_CROUCH;
		}
		else if (presencetype == PRESENCE_CROUCH &&
		         (AAS_PointPresenceType(org) & PRESENCE_NORMAL) != 0)
		{
			presencetype = PRESENCE_NORMAL;
		}

		vec3_t lastorg;
		VectorCopy(org, lastorg);

		vec3_t left_test_vel;
		VectorCopy(frame_test_vel, left_test_vel);

		for (int clip = 0; clip <= 20; ++clip)
		{
			vec3_t end;
			VectorAdd(org, left_test_vel, end);
			trace = AAS_MoveTraceClientBBox(org, end, presencetype, entnum);
			if (visualize)
			{
				if (trace.startsolid)
				{
					Q2_Print(PRT_MESSAGE, "PredictMovement: start solid\n");
				}
				AAS_DebugLine(org, trace.endpos, LINECOLOR_RED);
			}

			VectorCopy(trace.endpos, org);
			if (trace.fraction >= 1.0f)
			{
				if (clip >= 20)
				{
					memset(move, 0, sizeof(*move));
					return qfalse;
				}
				break;
			}

			const aas_plane_t *plane = AAS_PlaneFromNum(trace.planenum);
			if (plane == NULL)
			{
				/* Retail (sub_1000f840) indexes aasworld.planes unguarded, so
				   this can only happen before the plane lump is loaded.  Stop
				   clipping rather than dereference nothing; with planes loaded
				   the branch is unreachable and parity is unaffected. */
				break;
			}

			int step = qfalse;
			if (plane->normal[2] == 0.0f && (jump_frame < 0 || frame - jump_frame > 2))
			{
				vec3_t start;
				vec3_t stepend;
				VectorMA(org, -0.25f, plane->normal, start);
				VectorCopy(start, stepend);
				start[2] += phys_maxstep;

				aas_trace_t steptrace = AAS_MoveTraceClientBBox(start, stepend, presencetype, entnum);
				if (!steptrace.startsolid)
				{
					const aas_plane_t *stepplane = AAS_PlaneFromNum(steptrace.planenum);
					if (stepplane != NULL && stepplane->normal[2] > phys_maxsteepness)
					{
						VectorSubtract(end, steptrace.endpos, left_test_vel);
						left_test_vel[2] = 0.0f;
						frame_test_vel[2] = 0.0f;
						if (visualize && steptrace.endpos[2] - org[2] > 0.125f)
						{
							vec3_t step_line_end;
							VectorCopy(org, step_line_end);
							step_line_end[2] = steptrace.endpos[2];
							AAS_DebugLine(org, step_line_end, LINECOLOR_BLUE);
						}
						org[2] = steptrace.endpos[2];
						step = qtrue;
					}
				}
			}

			if (!step)
			{
				vec3_t old_frame_test_vel;
				VectorCopy(frame_test_vel, old_frame_test_vel);

				VectorMA(left_test_vel,
					-DotProduct(left_test_vel, plane->normal),
					plane->normal,
					left_test_vel);
				VectorMA(frame_test_vel,
					-DotProduct(frame_test_vel, plane->normal),
					plane->normal,
					frame_test_vel);
				if (plane->normal[2] > phys_maxsteepness)
				{
					onground = qtrue;
				}

				if ((retail_stopevent & SE_HITGROUNDDAMAGE) != 0)
				{
					float delta = 0.0f;
					if (old_frame_test_vel[2] < 0.0f &&
					    frame_test_vel[2] > old_frame_test_vel[2] &&
					    !onground)
					{
						delta = old_frame_test_vel[2];
					}
					else if (onground)
					{
						delta = frame_test_vel[2] - old_frame_test_vel[2];
					}
					if (delta != 0.0f)
					{
						delta = delta * 10.0f;
						delta = delta * delta * 0.0001f;
						if (swimming)
						{
							delta = 0.0f;
						}
						if (delta > 30.0f)
						{
							AAS_MoveStoreClientMove(move,
							                        org,
							                        frame_test_vel,
							                        &trace,
							                        presencetype,
							                        SE_HITGROUNDDAMAGE,
							                        AAS_RETAIL_DEFAULT_ENDCONTENTS,
							                        frametime,
							                        frame);
							return qtrue;
						}
					}
				}
			}

			if (clip >= 20)
			{
				memset(move, 0, sizeof(*move));
				return qfalse;
			}
		}

		if (frame_test_vel[2] <= 0.0f)
		{
			vec3_t feet;
			VectorCopy(org, feet);
			feet[2] -= 22.0f;

			int pointcontents = 0;
			int event = AAS_MoveFeetContentsEvents(feet, &pointcontents);
			if ((event & retail_stopevent) != 0)
			{
				AAS_MoveStoreClientMove(move,
				                        org,
				                        frame_test_vel,
				                        NULL,
				                        presencetype,
				                        event & retail_stopevent,
				                        pointcontents,
				                        frametime,
				                        frame);
				return qtrue;
			}
		}

		onground = AAS_OnGround(org, presencetype, entnum);
		if (onground)
		{
			if ((retail_stopevent & SE_HITGROUND) != 0)
			{
				AAS_MoveStoreClientMove(move,
				                        org,
				                        frame_test_vel,
				                        &trace,
				                        presencetype,
				                        SE_HITGROUND,
				                        AAS_RETAIL_DEFAULT_ENDCONTENTS,
				                        frametime,
				                        frame);
				return qtrue;
			}
		}
		else if ((retail_stopevent & SE_LEAVEGROUND) != 0)
		{
			AAS_MoveStoreClientMove(move,
			                        org,
			                        frame_test_vel,
			                        &trace,
			                        presencetype,
			                        SE_LEAVEGROUND,
			                        AAS_RETAIL_DEFAULT_ENDCONTENTS,
			                        frametime,
			                        frame);
			return qtrue;
		}
		else if ((retail_stopevent & SE_GAP) != 0)
		{
			vec3_t end;
			VectorCopy(org, end);
			end[2] -= 48.0f + phys_maxbarrier;
			aas_trace_t gaptrace = AAS_MoveTraceClientBBox(org, end, PRESENCE_CROUCH, -1);
			if (!gaptrace.startsolid &&
			    gaptrace.endpos[2] < org[2] - phys_maxstep - 1.0f)
			{
				vec3_t watercheck;
				VectorCopy(end, watercheck);
				if ((Q2_PointContents(watercheck) & CONTENTS_WATER) == 0)
				{
					AAS_MoveStoreClientMove(move,
					                        lastorg,
					                        frame_test_vel,
					                        &trace,
					                        presencetype,
					                        SE_GAP,
					                        AAS_RETAIL_DEFAULT_ENDCONTENTS,
					                        frametime,
					                        frame);
					return qtrue;
				}
			}
		}
	}

	AAS_MoveStoreClientMove(move,
	                        org,
	                        frame_test_vel,
	                        NULL,
	                        presencetype,
	                        SE_NONE,
	                        AAS_RETAIL_DEFAULT_ENDCONTENTS,
	                        frametime,
	                        frame);
	return qtrue;
}

/*
=============
AAS_TestMovementPrediction

Run the retail visual movement-prediction probe after hiding the prior frame's
shared debug lines.
=============
*/
void AAS_TestMovementPrediction(int entnum,
	vec3_t origin,
	vec3_t direction)
{
	vec3_t velocity;
	vec3_t cmdmove;
	aas_clientmove_t move;

	VectorClear(velocity);
	if (!AAS_Swimming(origin))
	{
		direction[2] = 0.0f;
	}
	AAS_MoveVectorNormalize(direction);
	VectorScale(direction, 400.0f, cmdmove);
	cmdmove[2] = 224.0f;

	AAS_ClearShownDebugLines();
	AAS_PredictClientMovement(&move,
		entnum,
		origin,
		PRESENCE_NORMAL,
		qtrue,
		velocity,
		cmdmove,
		13,
		13,
		0.1f,
		SE_HITGROUND,
		0,
		qtrue);
	if ((move.stopevent & SE_LEAVEGROUND) != 0)
	{
		Q2_Print(PRT_MESSAGE, "leave ground\n");
	}
}
