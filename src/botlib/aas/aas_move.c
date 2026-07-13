#include "aas_local.h"

#include <math.h>
#include <string.h>

#include "botlib/common/l_utils.h"
#include "q2bridge/bridge.h"
#include "q2bridge/bridge_config.h"

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
AAS_MovePositiveLibVarValue

Read a movement variable and preserve the Q3 physics fallback for zero values.
=============
*/
static float AAS_MovePositiveLibVarValue(const libvar_t *var, float fallback)
{
	float value = AAS_MoveLibVarValue(var, fallback);
	if (value <= 0.0f)
	{
		return fallback;
	}

	return value;
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
		return AAS_MovePositiveLibVarValue(Bridge_JumpVelocity(), 224.0f);
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
		AAS_MovePositiveLibVarValue(Bridge_JumpVelocity(), 224.0f);
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
AAS_MoveVectorLengthSquared

Return squared vector length without pulling in higher-level utility linkage.
=============
*/
static float AAS_MoveVectorLengthSquared(const vec3_t v)
{
	if (v == NULL)
	{
		return 0.0f;
	}

	return v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
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
AAS_MoveAccelerate

Accelerate a velocity vector toward a desired movement direction.
=============
*/
static void AAS_MoveAccelerate(vec3_t velocity,
                               float frametime,
                               const vec3_t wishdir,
                               float wishspeed,
                               float accel)
{
	float currentspeed = DotProduct(velocity, wishdir);
	float addspeed = wishspeed - currentspeed;
	if (addspeed <= 0.0f)
	{
		return;
	}

	float accelspeed = accel * frametime * wishspeed;
	if (accelspeed > addspeed)
	{
		accelspeed = addspeed;
	}

	for (int i = 0; i < 3; ++i)
	{
		velocity[i] += accelspeed * wishdir[i];
	}
}

/*
=============
AAS_MoveTraceClientBBox

Trace the AAS presence bounds through the loaded AAS tree or partial-world fallback.
=============
*/
static aas_trace_t AAS_MoveTraceClientBBox(const vec3_t start,
                                           const vec3_t end,
                                           int presencetype,
                                           int passent)
{
	aas_trace_t trace;
	memset(&trace, 0, sizeof(trace));
	trace.fraction = 1.0f;

	if (start == NULL || end == NULL)
	{
		return trace;
	}

	if (aasworld.nodes != NULL &&
	    aasworld.numNodes > 1 &&
	    aasworld.planes != NULL &&
	    aasworld.numPlanes > 0)
	{
		return AAS_TraceClientBBox(start, end, presencetype, passent);
	}

	vec3_t mins;
	vec3_t maxs;
	AAS_PresenceTypeBoundingBox(presencetype, mins, maxs);

	bsp_trace_t bsptrace = AAS_Trace(start,
	                                 mins,
	                                 maxs,
	                                 end,
	                                 passent,
	                                 CONTENTS_SOLID | CONTENTS_PLAYERCLIP);
	trace.startsolid = bsptrace.startsolid;
	trace.fraction = bsptrace.fraction;
	trace.ent = bsptrace.ent;
	trace.plane = bsptrace.plane;
	if (trace.fraction >= 1.0f && !trace.startsolid)
	{
		VectorCopy(end, trace.endpos);
	}
	else
	{
		VectorCopy(bsptrace.endpos, trace.endpos);
	}
	trace.area = AAS_PointAreaNum(trace.endpos);

	return trace;
}

/*
=============
AAS_MoveAreaContentsEvents

Translate AAS area contents at a point into prediction stop events.
=============
*/
static int AAS_MoveAreaContentsEvents(const vec3_t point)
{
	if (!aasworld.loaded || aasworld.areasettings == NULL || point == NULL)
	{
		return SE_NONE;
	}

	int areanum = AAS_PointAreaNum(point);
	if (areanum <= 0 || areanum >= aasworld.numAreaSettings)
	{
		return SE_NONE;
	}

	int events = SE_NONE;
	int contents = aasworld.areasettings[areanum].contents;
	if ((contents & AAS_AREACONTENTS_WATER) != 0)
	{
		events |= SE_ENTERWATER;
	}
	if ((contents & AAS_AREACONTENTS_SLIME) != 0)
	{
		events |= SE_ENTERSLIME;
	}
	if ((contents & AAS_AREACONTENTS_LAVA) != 0)
	{
		events |= SE_ENTERLAVA;
	}

	return events;
}

/*
=============
AAS_MovePointContentsEvents

Translate BSP contents at a point into prediction stop events.
=============
*/
static int AAS_MovePointContentsEvents(const vec3_t point, int *outcontents)
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
	if ((contents & CONTENTS_WATER) != 0)
	{
		events |= SE_ENTERWATER;
	}
	if ((contents & CONTENTS_SLIME) != 0)
	{
		events |= SE_ENTERSLIME;
	}
	if ((contents & CONTENTS_LAVA) != 0)
	{
		events |= SE_ENTERLAVA;
	}

	return events;
}

/*
=============
AAS_MoveStoreClientMove

Populate the public prediction result using the current frame velocity.
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
	move->endarea = AAS_PointAreaNum(endpos);
	if (frametime > 0.0f)
	{
		VectorScale(frame_velocity, 1.0f / frametime, move->velocity);
	}
	else
	{
		VectorClear(move->velocity);
	}
	if (trace != NULL)
	{
		move->trace = *trace;
	}
	move->presencetype = presencetype;
	move->stopevent = stopevent;
	move->endcontents = endcontents;
	move->time = (float)frame * frametime;
	move->frames = frame;
}

/*
=============
AAS_MovePointHasHazard

Check whether a sampled point has lava or slime contents.
=============
*/
static int AAS_MovePointHasHazard(const vec3_t point)
{
	if (point == NULL)
	{
		return qfalse;
	}

	int contents = 0;
	bot_import_t *imports = Q2Bridge_GetImportTable();
	if (imports != NULL && imports->PointContents != NULL)
	{
		vec3_t sample;
		VectorCopy(point, sample);
		contents = Q2_PointContents(sample);
	}
	if ((contents & (CONTENTS_LAVA | CONTENTS_SLIME)) != 0)
	{
		return qtrue;
	}

	if (!aasworld.loaded || aasworld.areasettings == NULL)
	{
		return qfalse;
	}

	int areanum = AAS_PointAreaNum(point);
	if (areanum <= 0 || areanum >= aasworld.numAreaSettings)
	{
		return qfalse;
	}

	contents = aasworld.areasettings[areanum].contents;
	return (contents & (AAS_AREACONTENTS_LAVA | AAS_AREACONTENTS_SLIME)) != 0;
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

	int contents = 0;
	int events = AAS_MovePointContentsEvents(sample, &contents);
	if ((events & (SE_ENTERWATER | SE_ENTERSLIME | SE_ENTERLAVA)) != 0)
	{
		return qtrue;
	}

	events = AAS_MoveAreaContentsEvents(sample);
	return (events & (SE_ENTERWATER | SE_ENTERSLIME | SE_ENTERLAVA)) != 0;
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
	end[2] -= 10.0f;

	aas_trace_t trace = AAS_MoveTraceClientBBox(origin, end, presencetype, passent);
	if (trace.startsolid || trace.fraction >= 1.0f)
	{
		return qfalse;
	}
	if (origin[2] - trace.endpos[2] > 10.0f)
	{
		return qfalse;
	}

	float maxsteepness = AAS_MoveLibVarValue(Bridge_MaxSteepness(), 0.7f);
	return trace.plane.normal[2] >= maxsteepness;
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
	if (AAS_MoveVectorNormalize(hordir) <= 0.0f)
	{
		VectorCopy(reach->start, runstart);
		return;
	}

	vec3_t start;
	VectorCopy(reach->start, start);
	start[2] += 1.0f;

	for (int frame = 1; frame <= 2; ++frame)
	{
		vec3_t sample;
		VectorMA(start, 40.0f * (float)frame, hordir, sample);
		if (AAS_MovePointHasHazard(sample))
		{
			VectorCopy(start, runstart);
			return;
		}
	}

	VectorMA(start, 80.0f, hordir, runstart);
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
	if (gravity <= 0.0f)
	{
		*velocity = maxvelocity;
		return qfalse;
	}

	float jump_time = zvel / gravity;
	float maxjump = 0.5f * gravity * jump_time * jump_time;
	float top = start[2] + maxjump;
	float height2fall = top - end[2];
	if (height2fall < 0.0f)
	{
		*velocity = maxvelocity;
		return qfalse;
	}

	float fall_time = sqrtf(height2fall / (0.5f * gravity));
	float total_time = fall_time + jump_time;
	if (fabsf(total_time) <= 1e-6f)
	{
		*velocity = maxvelocity;
		return qfalse;
	}

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

Predict client movement through the Q3 AAS stop-event state machine.
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
	(void)visualize;

	if (move == NULL || origin == NULL || velocity == NULL || cmdmove == NULL)
	{
		return qfalse;
	}

	memset(move, 0, sizeof(*move));

	if (frametime <= 0.0f)
	{
		frametime = 0.1f;
	}
	if (maxframes <= 0)
	{
		maxframes = 1;
	}
	if (cmdframes < 0)
	{
		cmdframes = 0;
	}

	float phys_friction = AAS_MovePositiveLibVarValue(Bridge_Friction(), 6.0f);
	float phys_stopspeed = AAS_MovePositiveLibVarValue(Bridge_StopSpeed(), 100.0f);
	float phys_gravity = AAS_MovePositiveLibVarValue(Bridge_Gravity(), 800.0f);
	float phys_watergravity = AAS_MovePositiveLibVarValue(Bridge_WaterGravity(), 400.0f);
	float phys_waterfriction = AAS_MovePositiveLibVarValue(Bridge_WaterFriction(), 1.0f);
	float phys_maxwalkvelocity = AAS_MovePositiveLibVarValue(Bridge_MaxWalkVelocity(), 320.0f);
	float phys_maxcrouchvelocity = AAS_MovePositiveLibVarValue(Bridge_MaxCrouchVelocity(), 100.0f);
	float phys_maxswimvelocity = AAS_MovePositiveLibVarValue(Bridge_MaxSwimVelocity(), 150.0f);
	float phys_walkaccelerate = 10.0f;
	float phys_airaccelerate = AAS_MovePositiveLibVarValue(Bridge_AirAccelerate(), 1.0f);
	float phys_swimaccelerate = 4.0f;
	float phys_maxstep = AAS_MovePositiveLibVarValue(Bridge_MaxStep(), 18.0f);
	float phys_maxbarrier = AAS_MovePositiveLibVarValue(Bridge_MaxBarrier(), 50.0f);
	float phys_maxsteepness = AAS_MovePositiveLibVarValue(Bridge_MaxSteepness(), 0.7f);
	float phys_jumpvel = AAS_MovePositiveLibVarValue(Bridge_JumpVelocity(), 224.0f) * frametime;

	vec3_t org;
	VectorCopy(origin, org);
	org[2] += 0.25f;

	vec3_t frame_test_vel;
	VectorScale(velocity, frametime, frame_test_vel);

	aas_trace_t trace;
	memset(&trace, 0, sizeof(trace));
	trace.fraction = 1.0f;

	int jump_frame = -1;
	for (int frame = 0; frame < maxframes; ++frame)
	{
		int swimming = AAS_Swimming(org);
		float gravity = swimming ? phys_watergravity : phys_gravity;
		frame_test_vel[2] -= gravity * 0.1f * frametime;

		if (onground || swimming)
		{
			float friction = swimming ? phys_waterfriction : phys_friction;
			VectorScale(frame_test_vel, 1.0f / frametime, frame_test_vel);
			AAS_MoveApplyFriction(frame_test_vel, friction, phys_stopspeed, frametime);
			VectorScale(frame_test_vel, frametime, frame_test_vel);
		}

		int crouch = qfalse;
		if (frame < cmdframes)
		{
			float maxvel = phys_maxwalkvelocity;
			float accelerate = phys_airaccelerate;
			vec3_t wishdir;
			VectorCopy(cmdmove, wishdir);

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
					accelerate = phys_airaccelerate;
				}
				else
				{
					accelerate = phys_walkaccelerate;
				}
			}
			if (swimming)
			{
				maxvel = phys_maxswimvelocity;
				accelerate = phys_swimaccelerate;
			}
			else
			{
				wishdir[2] = 0.0f;
			}

			float wishspeed = AAS_MoveVectorNormalize(wishdir);
			if (wishspeed > maxvel)
			{
				wishspeed = maxvel;
			}
			VectorScale(frame_test_vel, 1.0f / frametime, frame_test_vel);
			AAS_MoveAccelerate(frame_test_vel, frametime, wishdir, wishspeed, accelerate);
			VectorScale(frame_test_vel, frametime, frame_test_vel);
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

			if ((stopevent & SE_ENTERAREA) != 0 && stopareanum > 0)
			{
				int areas[20];
				vec3_t points[20];
				int count = AAS_TraceAreas(org, trace.endpos, areas, points, 20);
				for (int i = 0; i < count; ++i)
				{
					if (areas[i] == stopareanum)
					{
						AAS_MoveStoreClientMove(move,
						                        points[i],
						                        frame_test_vel,
						                        &trace,
						                        presencetype,
						                        SE_ENTERAREA,
						                        0,
						                        frametime,
						                        frame);
						return qtrue;
					}
				}
			}

			VectorCopy(trace.endpos, org);
			if (trace.fraction >= 1.0f)
			{
				break;
			}

			vec3_t normal;
			VectorCopy(trace.plane.normal, normal);
			if (AAS_MoveVectorLengthSquared(normal) <= 0.0001f)
			{
				VectorSet(normal, 0.0f, 0.0f, 1.0f);
			}

			int step = qfalse;
			if (fabsf(normal[2]) <= 0.0001f && (jump_frame < 0 || frame - jump_frame > 2))
			{
				vec3_t start;
				vec3_t stepend;
				VectorMA(org, -0.25f, normal, start);
				VectorCopy(start, stepend);
				start[2] += phys_maxstep;

				aas_trace_t steptrace = AAS_MoveTraceClientBBox(start, stepend, presencetype, entnum);
				if (!steptrace.startsolid && steptrace.plane.normal[2] > phys_maxsteepness)
				{
					VectorSubtract(end, steptrace.endpos, left_test_vel);
					left_test_vel[2] = 0.0f;
					frame_test_vel[2] = 0.0f;
					org[2] = steptrace.endpos[2];
					step = qtrue;
				}
			}

			if (!step)
			{
				vec3_t old_frame_test_vel;
				VectorCopy(frame_test_vel, old_frame_test_vel);

				VectorMA(left_test_vel, -DotProduct(left_test_vel, normal), normal, left_test_vel);
				VectorMA(frame_test_vel, -DotProduct(frame_test_vel, normal), normal, frame_test_vel);
				if (normal[2] > phys_maxsteepness)
				{
					onground = qtrue;
				}

				if ((stopevent & SE_HITGROUNDDAMAGE) != 0)
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
						if (delta > 40.0f)
						{
							AAS_MoveStoreClientMove(move,
							                        org,
							                        frame_test_vel,
							                        &trace,
							                        presencetype,
							                        SE_HITGROUNDDAMAGE,
							                        0,
							                        frametime,
							                        frame);
							return qtrue;
						}
					}
				}
			}
		}

		if (frame_test_vel[2] <= 10.0f)
		{
			vec3_t feet;
			VectorCopy(org, feet);
			feet[2] -= 22.0f;

			int pointcontents = 0;
			int event = AAS_MovePointContentsEvents(feet, &pointcontents);
			event |= AAS_MoveAreaContentsEvents(org);
			if ((event & stopevent) != 0)
			{
				AAS_MoveStoreClientMove(move,
				                        org,
				                        frame_test_vel,
				                        &trace,
				                        presencetype,
				                        event & stopevent,
				                        pointcontents,
				                        frametime,
				                        frame);
				return qtrue;
			}
		}

		onground = AAS_OnGround(org, presencetype, entnum);
		if (onground)
		{
			if ((stopevent & SE_HITGROUND) != 0)
			{
				AAS_MoveStoreClientMove(move,
				                        org,
				                        frame_test_vel,
				                        &trace,
				                        presencetype,
				                        SE_HITGROUND,
				                        0,
				                        frametime,
				                        frame);
				return qtrue;
			}
		}
		else if ((stopevent & SE_LEAVEGROUND) != 0)
		{
			AAS_MoveStoreClientMove(move,
			                        org,
			                        frame_test_vel,
			                        &trace,
			                        presencetype,
			                        SE_LEAVEGROUND,
			                        0,
			                        frametime,
			                        frame);
			return qtrue;
		}
		else if ((stopevent & SE_GAP) != 0)
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
					                        0,
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
	                        &trace,
	                        presencetype,
	                        SE_NONE,
	                        0,
	                        frametime,
	                        maxframes);
	return qtrue;
}
