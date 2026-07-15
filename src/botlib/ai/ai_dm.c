#include "ai_dm.h"

#include <ctype.h>
#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "botlib/aas/aas_map.h"
#include "botlib/ai/goal_move_orchestrator.h"
#include "botlib/common/l_libvar.h"
#include "botlib/ea/ea_local.h"
#include "botlib/interface/bot_state.h"
#include "q2bridge/bridge.h"
#include "q2bridge/bridge_config.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifndef AI_DM_ATTACK_COOLDOWN
#define AI_DM_ATTACK_COOLDOWN 0.2f
#endif

#ifndef AI_DM_AVOID_DURATION
#define AI_DM_AVOID_DURATION 1.5f
#endif

#ifndef AI_DM_MIN_ROCKET_VERTICAL
#define AI_DM_MIN_ROCKET_VERTICAL 48.0f
#endif

#ifndef AI_DM_MAX_CHASE_SPEED
#define AI_DM_MAX_CHASE_SPEED 400.0f
#endif

#ifndef AI_DM_REACTION_DELAY
#define AI_DM_REACTION_DELAY 0.3f
#endif

#ifndef AI_DM_CHASE_DURATION
#define AI_DM_CHASE_DURATION 2.5f
#endif

#ifndef AI_DM_ATTACK_STRAFE_INTERVAL
#define AI_DM_ATTACK_STRAFE_INTERVAL 0.6f
#endif

#ifndef AI_DM_ATTACK_CHASE_DURATION
#define AI_DM_ATTACK_CHASE_DURATION 1.0f
#endif

#define AI_DM_CHARACTERISTIC_ATTACK_SKILL 4
#define AI_DM_CHARACTERISTIC_AIM_SKILL 7
#define AI_DM_CHARACTERISTIC_AIM_ACCURACY 8
#define AI_DM_CHARACTERISTIC_VIEW_FACTOR 9
#define AI_DM_CHARACTERISTIC_VIEW_MAXCHANGE 10
#define AI_DM_CHARACTERISTIC_REACTION_TIME 11
#define AI_DM_CHARACTERISTIC_CROUCHER 24
#define AI_DM_CHARACTERISTIC_JUMPER 25
#define AI_DM_CHARACTERISTIC_PIZZA_PREFERENCE 48

#define AI_DM_MIN_ATTACK_SKILL 0.2f
#define AI_DM_SIMPLE_ATTACK_SKILL 0.4f
#define AI_DM_IDEAL_ATTACK_DISTANCE 140.0f
#define AI_DM_ATTACK_DISTANCE_RANGE 40.0f
#define AI_DM_RETAIL_THINK_TIME 0.1f
#define AI_DM_VISIBILITY_CONTENTS 0x02030003
#define AI_DM_VISIBILITY_FLUIDS (CONTENTS_LAVA | CONTENTS_SLIME | CONTENTS_WATER)
#define AI_DM_TRANSLUCENT_SURFACES 0x30

struct ai_dm_state_s
{
	int client_number;
	libvar_t *dmflags;
	libvar_t *rocketjump;
	libvar_t *usehook;
	int dmflags_value;
	bool allow_rocket_jump;
	bool allow_hook;
	bool config_initialised;
	float last_attack_time;
	float last_jump_time;
	float attack_cooldown;
	float avoid_duration;
	int last_selected_weapon;
	int last_avoid_entity;
	float last_avoid_time;
	int enemy_entity;
	int last_enemy_area;
	int revenge_enemy;
	int revenge_kills;
	bool enemy_visible;
	int strafe_side;
	bool attack_jump_latched;
	pmtype_t last_pm_type;
	float enemyvisible_time;
	float enemysight_time;
	float enemydeath_time;
	float enemyposition_time;
	float chase_time;
	float attackstrafe_timer;
	float attackcrouch_time;
	float attackchase_time;
	float attackjump_time;
	float firethrottlewait_time;
	float firethrottleshoot_time;
	float last_think_time;
	float reaction_delay;
	float chase_duration;
	float attack_strafe_interval;
	float attack_chase_duration;
	vec3_t last_enemy_origin;
	vec3_t last_enemy_velocity;
	bool view_initialised;
	vec3_t viewangles;
	vec3_t ideal_viewangles;
	vec3_t viewanglespeed;
	vec3_t aim_target;
	bool attack_latched;
};

/*
=============
AI_DMVectorNormalise

Normalises a direction vector and returns its original length.
=============
*/
static float AI_DMVectorNormalise(vec3_t vector)
{
	if (vector == NULL)
	{
		return 0.0f;
	}

	float length_sq = vector[0] * vector[0] + vector[1] * vector[1] + vector[2] * vector[2];
	if (length_sq <= 0.0f)
	{
		VectorClear(vector);
		return 0.0f;
	}

	float length = sqrtf(length_sq);
	float inv = 1.0f / length;
	vector[0] *= inv;
	vector[1] *= inv;
	vector[2] *= inv;
	return length;
}

/*
=============
AI_DMStringCompare

Compares weapon names with the retail case-insensitive byte ordering.
=============
*/
static int AI_DMStringCompare(const char *left, const char *right)
{
	if (left == NULL || right == NULL)
	{
		return left == right ? 0 : (left != NULL ? 1 : -1);
	}

	while (*left != '\0' || *right != '\0')
	{
		int left_character = tolower((unsigned char)*left);
		int right_character = tolower((unsigned char)*right);
		if (left_character != right_character)
		{
			return left_character - right_character;
		}
		if (*left != '\0')
		{
			left += 1;
		}
		if (*right != '\0')
		{
			right += 1;
		}
	}
	return 0;
}

/*
=============
AI_DMAngleMod

Quantises an angle through the retail unsigned 16-bit representation.
=============
*/
static float AI_DMAngleMod(float angle)
{
	return (360.0f / 65536.0f) *
		(float)(((int)(angle * (65536.0f / 360.0f))) & 65535);
}

/*
=============
AI_DMAngleDifference

Returns the shortest signed retail difference between two angles.
=============
*/
static float AI_DMAngleDifference(float angle, float ideal_angle)
{
	float difference = angle - ideal_angle;
	if (angle > ideal_angle)
	{
		if (difference > 180.0f)
		{
			difference -= 360.0f;
		}
	}
	else if (difference < -180.0f)
	{
		difference += 360.0f;
	}
	return difference;
}

/*
=============
AI_DMChangeViewAngle

Moves one quantised angle toward its ideal value by at most the given speed.
=============
*/
static float AI_DMChangeViewAngle(float angle, float ideal_angle, float speed)
{
	angle = AI_DMAngleMod(angle);
	ideal_angle = AI_DMAngleMod(ideal_angle);
	if (angle == ideal_angle)
	{
		return angle;
	}

	float move = ideal_angle - angle;
	if (ideal_angle > angle)
	{
		if (move > 180.0f)
		{
			move -= 360.0f;
		}
	}
	else if (move < -180.0f)
	{
		move += 360.0f;
	}

	if (move > speed)
	{
		move = speed;
	}
	else if (move < -speed)
	{
		move = -speed;
	}
	return AI_DMAngleMod(angle + move);
}

/*
=============
AI_DMVectorToAngles

Converts an aim direction to Gladiator's pitch/yaw representation.
=============
*/
static void AI_DMVectorToAngles(const vec3_t direction, vec3_t angles)
{
	VectorClear(angles);
	if (direction[0] == 0.0f && direction[1] == 0.0f)
	{
		if (direction[2] > 0.0f)
		{
			angles[PITCH] = -90.0f;
		}
		else if (direction[2] < 0.0f)
		{
			angles[PITCH] = 90.0f;
		}
		return;
	}

	angles[YAW] = atan2f(direction[1], direction[0]) *
		(180.0f / (float)M_PI);
	if (angles[YAW] < 0.0f)
	{
		angles[YAW] += 360.0f;
	}
	float forward = sqrtf(direction[0] * direction[0] +
		direction[1] * direction[1]);
	angles[PITCH] = atan2f(-direction[2], forward) *
		(180.0f / (float)M_PI);
}

/*
=============
AI_DMRandom

Returns the retail random fraction from the low 15 rand bits.
=============
*/
static float AI_DMRandom(void)
{
	return (float)(rand() & 0x7fff) * 3.05185094e-05f;
}

/*
=============
AI_DMCRandom

Returns the retail signed random fraction used by attack strafing.
=============
*/
static float AI_DMCRandom(void)
{
	return (AI_DMRandom() - 0.5f) * 2.0f;
}

/*
=============
AI_DMCharacteristic

Reads a bounded character value with a safe harness fallback.
=============
*/
static float AI_DMCharacteristic(const bot_client_state_t *client_state, int index, float fallback)
{
	if (client_state == NULL || client_state->character_handle <= 0)
	{
		return fallback;
	}

	return Characteristic_BFloat(client_state->character_handle, index, 0.0f, 1.0f);
}

/*
=============
AI_DMViewCharacteristic

Reads one retail view characteristic over its wider 0.1-to-1800 range.
=============
*/
static float AI_DMViewCharacteristic(const bot_client_state_t *client_state,
	int index,
	float fallback)
{
	if (client_state == NULL || client_state->character_handle <= 0)
	{
		return fallback;
	}

	return Characteristic_BFloat(client_state->character_handle,
		index,
		0.1f,
		1800.0f);
}

/*
=============
AI_DMChangeViewAngles

Applies Gladiator's accelerated/decelerated two-axis view turn and submits it.
=============
*/
static void AI_DMChangeViewAngles(ai_dm_state_t *state,
	const bot_client_state_t *client_state,
	float think_time)
{
	if (state == NULL || client_state == NULL)
	{
		return;
	}

	if (state->ideal_viewangles[PITCH] > 180.0f)
	{
		state->ideal_viewangles[PITCH] -= 360.0f;
	}

	float factor;
	float max_change;
	if (state->enemy_entity < 0)
	{
		factor = 100.0f;
		max_change = 150.0f;
	}
	else
	{
		factor = AI_DMViewCharacteristic(client_state,
			AI_DM_CHARACTERISTIC_VIEW_FACTOR,
			100.0f);
		max_change = AI_DMViewCharacteristic(client_state,
			AI_DM_CHARACTERISTIC_VIEW_MAXCHANGE,
			150.0f);
	}

	factor *= think_time;
	max_change *= think_time;
	for (int axis = PITCH; axis <= YAW; ++axis)
	{
		int signed_difference = (int)AI_DMAngleDifference(
			state->viewangles[axis],
			state->ideal_viewangles[axis]);
		float difference = (float)abs(signed_difference);
		if (difference <= state->viewanglespeed[axis])
		{
			if (difference < state->viewanglespeed[axis])
			{
				state->viewanglespeed[axis] -= max_change;
				if (state->viewanglespeed[axis] < difference)
				{
					state->viewanglespeed[axis] = difference;
				}
			}
		}
		else
		{
			state->viewanglespeed[axis] += factor;
			if (state->viewanglespeed[axis] > difference)
			{
				state->viewanglespeed[axis] = difference;
			}
		}

		state->viewangles[axis] = AI_DMChangeViewAngle(
			state->viewangles[axis],
			state->ideal_viewangles[axis],
			state->viewanglespeed[axis]);
	}

	EA_View(client_state->client_number, state->viewangles);
}

/*
=============
AI_DMAimAtEnemy

Reconstructs the retail weapon-aware aim target, spread, turn, and snap stages.
=============
*/
static void AI_DMAimAtEnemy(ai_dm_state_t *state,
	const bot_client_state_t *client_state,
	const ai_dm_enemy_info_t *enemy,
	const vec3_t eye_position,
	float think_time)
{
	if (state == NULL || client_state == NULL || enemy == NULL)
	{
		return;
	}

	const bot_weapon_info_t *weapon = NULL;
	if (client_state->weapon_state > 0)
	{
		weapon = BotCurrentWeaponInfo(client_state->weapon_state);
	}

	float aim_skill = AI_DMCharacteristic(client_state,
		AI_DM_CHARACTERISTIC_AIM_SKILL,
		0.0f);
	float aim_accuracy = AI_DMCharacteristic(client_state,
		AI_DM_CHARACTERISTIC_AIM_ACCURACY,
		1.0f);
	if (aim_accuracy <= 0.0f)
	{
		aim_accuracy = 0.0001f;
	}
	if (weapon != NULL &&
		AI_DMStringCompare(weapon->name, "Rocket Launcher") == 0)
	{
		aim_accuracy = sqrtf(aim_accuracy);
	}

	vec3_t best_origin;
	VectorCopy(enemy->origin, best_origin);
	best_origin[2] += 8.0f;
	if (weapon != NULL)
	{
		vec3_t start;
		VectorCopy(client_state->last_client_update.origin, start);
		start[2] += client_state->last_client_update.viewoffset[2];
		start[2] += weapon->offset[2];
		vec3_t mins = {-4.0f, -4.0f, -4.0f};
		vec3_t maxs = {4.0f, 4.0f, 4.0f};
		bsp_trace_t trace = Q2_Trace(start,
			mins,
			maxs,
			best_origin,
			client_state->client_number,
			MASK_SHOT);
		if (trace.fraction <= 1.0f && trace.ent != enemy->entity)
		{
			best_origin[2] += 16.0f;
		}

		if (weapon->speed != 0.0f && aim_skill > 0.4f)
		{
			vec3_t distance_vector;
			VectorSubtract(enemy->origin,
				client_state->last_client_update.origin,
				distance_vector);
			float distance = sqrtf(DotProduct(distance_vector,
				distance_vector));
			float lead_time = distance / weapon->speed;
			best_origin[0] = enemy->origin[0] +
				state->last_enemy_velocity[0] * lead_time;
			best_origin[1] = enemy->origin[1] +
				state->last_enemy_velocity[1] * lead_time;
			best_origin[2] = enemy->origin[2];
		}

		const bot_weapon_projectile_t *projectile = weapon->projectileinfo;
		if (aim_skill > 0.6f && projectile != NULL &&
			(projectile->damagetype & BOT_DAMAGETYPE_RADIAL) != 0 &&
			enemy->origin[2] <
				client_state->last_client_update.origin[2] + 16.0f)
		{
			vec3_t trace_enemy_origin;
			VectorCopy(enemy->origin, trace_enemy_origin);
			vec3_t ground_end;
			VectorCopy(trace_enemy_origin, ground_end);
			ground_end[2] -= 64.0f;
			bsp_trace_t ground_trace = Q2_Trace(trace_enemy_origin,
				NULL,
				NULL,
				ground_end,
				enemy->entity,
				MASK_SHOT);

			vec3_t ground_target;
			VectorCopy(best_origin, ground_target);
			if (ground_trace.startsolid)
			{
				ground_target[2] = enemy->origin[2] - 16.0f;
			}
			else
			{
				ground_target[2] = ground_trace.endpos[2] - 8.0f;
			}

			bsp_trace_t impact_trace = Q2_Trace(start,
				NULL,
				NULL,
				ground_target,
				client_state->client_number,
				MASK_SHOT);
			vec3_t impact_delta;
			VectorSubtract(impact_trace.endpos,
				ground_target,
				impact_delta);
			float target_distance = sqrtf(DotProduct(impact_delta,
				impact_delta));
			vec3_t shooter_delta;
			VectorSubtract(impact_trace.endpos, start, shooter_delta);
			float shooter_distance = sqrtf(DotProduct(shooter_delta,
				shooter_delta));
			if (fabsf(impact_trace.endpos[2] - ground_target[2]) < 50.0f &&
				target_distance < 60.0f && shooter_distance > 150.0f)
			{
				bsp_trace_t enemy_trace = Q2_Trace(impact_trace.endpos,
					NULL,
					NULL,
					trace_enemy_origin,
					enemy->entity,
					MASK_SHOT);
				if (enemy_trace.fraction >= 1.0f)
				{
					VectorCopy(ground_target, best_origin);
				}
			}
		}

		float inaccuracy = 1.0f - aim_accuracy;
		best_origin[0] += 20.0f * AI_DMCRandom() * inaccuracy;
		best_origin[1] += 20.0f * AI_DMCRandom() * inaccuracy;
		best_origin[2] += 10.0f * AI_DMCRandom() * inaccuracy;
	}
	VectorCopy(best_origin, state->aim_target);

	vec3_t direction;
	VectorSubtract(best_origin, eye_position, direction);
	if (weapon != NULL && AI_DMStringCompare(weapon->name, "Railgun") == 0)
	{
		AI_DMVectorNormalise(direction);
		float inaccuracy = 1.0f - aim_accuracy;
		for (int axis = 0; axis < 3; ++axis)
		{
			direction[axis] += 0.3f * AI_DMCRandom() * inaccuracy;
		}
	}
	AI_DMVectorToAngles(direction, state->ideal_viewangles);
	if (weapon != NULL)
	{
		float inaccuracy = 1.0f - aim_accuracy;
		state->ideal_viewangles[PITCH] += 6.0f * weapon->vspread *
			AI_DMCRandom() * inaccuracy;
		state->ideal_viewangles[PITCH] = AI_DMAngleMod(
			state->ideal_viewangles[PITCH]);
		state->ideal_viewangles[YAW] += 6.0f * weapon->hspread *
			AI_DMCRandom() * inaccuracy;
		state->ideal_viewangles[YAW] = AI_DMAngleMod(
			state->ideal_viewangles[YAW]);
	}
	AI_DMChangeViewAngles(state, client_state, think_time);

	if (aim_accuracy > 0.8f)
	{
		if (state->ideal_viewangles[PITCH] > 180.0f)
		{
			state->ideal_viewangles[PITCH] -= 360.0f;
		}
		VectorCopy(state->ideal_viewangles, state->viewangles);
		EA_View(client_state->client_number, state->viewangles);
	}
}

/*
=============
AI_DMAngleVectors

Builds the retail forward and right vectors from view angles.
=============
*/
static void AI_DMAngleVectors(const vec3_t angles,
	vec3_t forward,
	vec3_t right)
{
	float pitch = angles[PITCH] * ((float)M_PI / 180.0f);
	float yaw = angles[YAW] * ((float)M_PI / 180.0f);
	float roll = angles[ROLL] * ((float)M_PI / 180.0f);
	float sin_pitch = sinf(pitch);
	float cos_pitch = cosf(pitch);
	float sin_yaw = sinf(yaw);
	float cos_yaw = cosf(yaw);
	float sin_roll = sinf(roll);
	float cos_roll = cosf(roll);

	forward[0] = cos_pitch * cos_yaw;
	forward[1] = cos_pitch * sin_yaw;
	forward[2] = -sin_pitch;
	right[0] = -sin_roll * sin_pitch * cos_yaw + cos_roll * sin_yaw;
	right[1] = -sin_roll * sin_pitch * sin_yaw - cos_roll * cos_yaw;
	right[2] = -sin_roll * cos_pitch;
}

/*
=============
AI_DMInFieldOfVision

Checks both retail pitch and yaw half-FOV bounds against a target direction.
=============
*/
static bool AI_DMInFieldOfVision(const vec3_t viewangles,
	float field_of_view,
	const vec3_t direction)
{
	vec3_t target_angles;
	AI_DMVectorToAngles(direction, target_angles);
	float half_fov = field_of_view * 0.5f;
	float pitch = fabsf(AI_DMAngleDifference(viewangles[PITCH],
		target_angles[PITCH]));
	float yaw = fabsf(AI_DMAngleDifference(viewangles[YAW],
		target_angles[YAW]));
	return pitch <= half_fov && yaw <= half_fov;
}

/*
=============
AI_DMEntityVisible

Reconstructs retail BotEntityVisible's three-point FOV/PVS/medium trace.
=============
*/
static bool AI_DMEntityVisible(int viewer,
	const vec3_t eye,
	const vec3_t viewangles,
	float field_of_view,
	int entity)
{
	aas_entityinfo_t entity_info;
	memset(&entity_info, 0, sizeof(entity_info));
	AAS_EntityInfo(entity, &entity_info);

	vec3_t middle;
	for (int axis = 0; axis < 3; ++axis)
	{
		middle[axis] = (entity_info.mins[axis] +
			entity_info.maxs[axis]) * 0.5f + entity_info.origin[axis];
	}
	vec3_t direction;
	VectorSubtract(middle, eye, direction);
	if (!AI_DMInFieldOfVision(viewangles, field_of_view, direction))
	{
		return false;
	}

	for (int sample = 0; sample < 3; ++sample)
	{
		if (AAS_InPVS(eye, middle))
		{
			int contents_mask = AI_DM_VISIBILITY_CONTENTS;
			int passent = viewer;
			int hit_entity = entity;
			vec3_t start;
			vec3_t end;
			VectorCopy(eye, start);
			VectorCopy(middle, end);

			if ((AAS_PointContents(middle) &
				AI_DM_VISIBILITY_FLUIDS) != 0)
			{
				contents_mask |= AI_DM_VISIBILITY_FLUIDS;
			}
			if ((AAS_PointContents(eye) &
				AI_DM_VISIBILITY_FLUIDS) != 0)
			{
				if ((contents_mask & AI_DM_VISIBILITY_FLUIDS) == 0)
				{
					passent = entity;
					hit_entity = viewer;
					VectorCopy(middle, start);
					VectorCopy(eye, end);
				}
				contents_mask ^= AI_DM_VISIBILITY_FLUIDS;
			}

			bsp_trace_t trace = Q2_Trace(start,
				NULL,
				NULL,
				end,
				passent,
				contents_mask);
			if ((trace.contents & AI_DM_VISIBILITY_FLUIDS) != 0 &&
				(trace.surface.flags & AI_DM_TRANSLUCENT_SURFACES) != 0)
			{
				contents_mask &= ~AI_DM_VISIBILITY_FLUIDS;
				trace = Q2_Trace(trace.endpos,
					NULL,
					NULL,
					end,
					passent,
					contents_mask);
			}
			if (trace.fraction >= 1.0f || trace.ent == hit_entity)
			{
				return true;
			}
		}

		if (sample == 0)
		{
			middle[2] += entity_info.mins[2];
		}
		else if (sample == 1)
		{
			middle[2] += entity_info.maxs[2] -
				entity_info.mins[2];
		}
	}
	return false;
}

/*
=============
AI_DMTraceEntityIsTeammate

Resolves the retail wrong-client firing-sweep guard through live bot states.
=============
*/
static bool AI_DMTraceEntityIsTeammate(const bot_client_state_t *client_state,
	int entity)
{
	if (client_state == NULL || entity <= 0 ||
		entity > BotState_ClientCapacity())
	{
		return false;
	}

	const bot_client_state_t *other = BotState_Get(entity);
	return other != NULL && client_state->team >= 0 && other->team >= 0 &&
		client_state->team == other->team;
}

/*
=============
AI_DMCheckAttack

Reconstructs the retail reaction, visibility, firing sweep, splash-safety,
window follow-up, and fire-on-release latch path.
=============
*/
static bool AI_DMCheckAttack(ai_dm_state_t *state,
	const bot_client_state_t *client_state,
	const ai_dm_enemy_info_t *enemy,
	float now)
{
	if (state == NULL || client_state == NULL || enemy == NULL ||
		enemy->entity == 0 ||
		now - state->reaction_delay < state->enemysight_time)
	{
		return false;
	}

	vec3_t enemy_direction;
	VectorSubtract(enemy->origin,
		client_state->last_client_update.origin,
		enemy_direction);
	float enemy_distance = sqrtf(DotProduct(enemy_direction, enemy_direction));
	float field_of_view = enemy_distance < 100.0f ? 120.0f : 50.0f;
	vec3_t eye_position;
	VectorCopy(client_state->last_client_update.origin, eye_position);
	eye_position[2] += client_state->last_client_update.viewoffset[2];
	if (!AI_DMEntityVisible(client_state->client_number,
		eye_position,
		state->viewangles,
		field_of_view,
		enemy->entity))
	{
		return false;
	}

	const bot_weapon_info_t *weapon = NULL;
	if (client_state->weapon_state > 0)
	{
		weapon = BotCurrentWeaponInfo(client_state->weapon_state);
	}
	if (weapon == NULL)
	{
		return false;
	}

	vec3_t forward;
	vec3_t right;
	AI_DMAngleVectors(state->viewangles, forward, right);
	vec3_t start;
	VectorCopy(client_state->last_client_update.origin, start);
	start[2] += client_state->last_client_update.viewoffset[2];
	for (int axis = 0; axis < 3; ++axis)
	{
		start[axis] += forward[axis] * weapon->offset[0] +
			right[axis] * weapon->offset[1];
	}
	start[2] += weapon->offset[2];
	vec3_t end;
	for (int axis = 0; axis < 3; ++axis)
	{
		end[axis] = start[axis] + forward[axis] * 1000.0f;
		start[axis] -= forward[axis] * 12.0f;
	}
	vec3_t mins = {-8.0f, -8.0f, -8.0f};
	vec3_t maxs = {8.0f, 8.0f, 8.0f};
	bsp_trace_t trace = Q2_Trace(start,
		mins,
		maxs,
		end,
		client_state->client_number,
		MASK_SHOT);

	if (trace.ent != enemy->entity)
	{
		if (AI_DMTraceEntityIsTeammate(client_state, trace.ent))
		{
			return false;
		}

		const bot_weapon_projectile_t *projectile = weapon->projectileinfo;
		if (projectile != NULL &&
			(projectile->damagetype & BOT_DAMAGETYPE_RADIAL) != 0 &&
			trace.fraction * 1000.0f < projectile->radius)
		{
			float points = ((float)projectile->damage -
				trace.fraction * 500.0f) * 0.5f;
			if (points > 0.0f)
			{
				return false;
			}
		}
	}

	if ((trace.contents & CONTENTS_WINDOW) != 0)
	{
		vec3_t target_origin;
		VectorCopy(enemy->origin, target_origin);
		trace = Q2_Trace(trace.endpos,
			NULL,
			NULL,
			target_origin,
			client_state->client_number,
			MASK_SHOT);
		if (trace.ent != enemy->entity)
		{
			return false;
		}
	}

	bool attacked = false;
	if ((weapon->flags & BOT_WEAPON_FIRERELEASED) == 0 ||
		state->attack_latched)
	{
		EA_Attack(client_state->client_number);
		state->last_attack_time = now;
		state->firethrottleshoot_time = now;
		attacked = true;
	}
	state->attack_latched = !state->attack_latched;
	return attacked;
}

/*
=============
AI_DMSetupForMovement

Rebuilds the move-state input used immediately before retail attack-chase
goal movement.
=============
*/
static void AI_DMSetupForMovement(const ai_dm_state_t *state,
	const bot_client_state_t *client_state,
	float think_time)
{
	if (state == NULL || client_state == NULL || client_state->move_handle <= 0)
	{
		return;
	}

	bot_initmove_t initmove;
	memset(&initmove, 0, sizeof(initmove));
	VectorCopy(client_state->last_client_update.origin, initmove.origin);
	VectorCopy(client_state->last_client_update.velocity, initmove.velocity);
	VectorCopy(client_state->last_client_update.viewoffset, initmove.viewoffset);
	initmove.entitynum = client_state->client_number;
	initmove.client = client_state->client_number;
	initmove.thinktime = think_time;
	initmove.presencetype =
		(client_state->last_client_update.pm_flags & PMF_DUCKED) != 0 ?
		PRESENCE_CROUCH : PRESENCE_NORMAL;
	VectorCopy(state->viewangles, initmove.viewangles);

	if ((client_state->last_client_update.pm_flags & PMF_ON_GROUND) != 0)
	{
		initmove.or_moveflags |= MFL_ONGROUND;
	}
	if ((client_state->last_client_update.pm_flags & PMF_TIME_TELEPORT) != 0 &&
		client_state->last_client_update.pm_time > 0)
	{
		initmove.or_moveflags |= MFL_TELEPORTED;
	}
	if ((client_state->last_client_update.pm_flags & PMF_TIME_WATERJUMP) != 0 &&
		client_state->last_client_update.pm_time > 0)
	{
		initmove.or_moveflags |= MFL_WATERJUMP;
	}

	BotInitMoveState(client_state->move_handle, &initmove);
}

/*
=============
AI_DMTryAttackChase

Reconstructs the dormant retail entry branch: a strict future-time gate,
cached enemy goal with an eight-unit box, movement setup, and BotMoveToGoal.
=============
*/
static bool AI_DMTryAttackChase(ai_dm_state_t *state,
	const bot_client_state_t *client_state,
	float now,
	float think_time,
	int travel_flags)
{
	if (state->attackchase_time <= now)
	{
		return false;
	}

	bot_goal_t goal;
	goal.entitynum = state->enemy_entity;
	goal.areanum = state->last_enemy_area;
	VectorCopy(state->last_enemy_origin, goal.origin);
	VectorSet(goal.mins, -8.0f, -8.0f, -8.0f);
	VectorSet(goal.maxs, 8.0f, 8.0f, 8.0f);

	AI_DMSetupForMovement(state, client_state, think_time);
	bot_moveresult_t move_result;
	BotMoveToGoal(&move_result,
		client_state->move_handle,
		&goal,
		travel_flags);
	return true;
}

/*
=============
AI_DMAttackMove

Applies retail attack-skill, preference, distance, and strafe gates.
=============
*/
static void AI_DMAttackMove(ai_dm_state_t *state,
	const bot_client_state_t *client_state,
	const vec3_t forward,
	float distance,
	float now,
	float think_time,
	int travel_flags)
{
	if (state == NULL || client_state == NULL || forward == NULL)
	{
		return;
	}
	if (AI_DMTryAttackChase(state,
		client_state,
		now,
		think_time,
		travel_flags))
	{
		return;
	}

	float preference_roll = AI_DMRandom();
	float pizza_preference = AI_DMCharacteristic(client_state, AI_DM_CHARACTERISTIC_PIZZA_PREFERENCE, 0.0f);
	if (pizza_preference > preference_roll)
	{
		return;
	}

	float attack_skill = AI_DMCharacteristic(client_state, AI_DM_CHARACTERISTIC_ATTACK_SKILL, 1.0f);
	float jumper = AI_DMCharacteristic(client_state, AI_DM_CHARACTERISTIC_JUMPER, 0.0f);
	float croucher = AI_DMCharacteristic(client_state, AI_DM_CHARACTERISTIC_CROUCHER, 0.0f);
	if (attack_skill < AI_DM_MIN_ATTACK_SKILL)
	{
		return;
	}

	int move_type = MOVE_WALK;
	if (state->attackcrouch_time < now - 1.0f)
	{
		if (AI_DMRandom() < jumper)
		{
			move_type = MOVE_JUMP;
		}
		else if (AI_DMRandom() < croucher)
		{
			state->attackcrouch_time = now + croucher * 5.0f;
		}
	}

	if (state->attackcrouch_time > now)
	{
		move_type = MOVE_CROUCH;
	}
	else if (move_type == MOVE_JUMP)
	{
		if (state->attack_jump_latched)
		{
			state->attack_jump_latched = false;
			move_type = MOVE_WALK;
		}
		else
		{
			state->attack_jump_latched = true;
		}
	}

	vec3_t backward;
	VectorNegate(forward, backward);

	if (attack_skill <= AI_DM_SIMPLE_ATTACK_SKILL)
	{
		if (distance > AI_DM_IDEAL_ATTACK_DISTANCE + AI_DM_ATTACK_DISTANCE_RANGE)
		{
			BotMoveInDirection(client_state->move_handle, forward, AI_DM_MAX_CHASE_SPEED, move_type);
		}
		else if (distance < AI_DM_IDEAL_ATTACK_DISTANCE - AI_DM_ATTACK_DISTANCE_RANGE)
		{
			BotMoveInDirection(client_state->move_handle, backward, AI_DM_MAX_CHASE_SPEED, move_type);
		}
		return;
	}

	state->attackstrafe_timer += AI_DM_RETAIL_THINK_TIME;
	state->attack_strafe_interval = 0.4f + (1.0f - attack_skill) * 0.2f;
	if (attack_skill > 0.7f)
	{
		state->attack_strafe_interval += AI_DMCRandom() * 0.1f;
	}

	if (state->attackstrafe_timer > state->attack_strafe_interval && AI_DMRandom() > 0.935f)
	{
		state->strafe_side = -state->strafe_side;
		state->attackstrafe_timer = 0.0f;
	}

	for (int attempt = 0; attempt < 2; ++attempt)
	{
		vec3_t horizontal = {forward[0], forward[1], 0.0f};
		AI_DMVectorNormalise(horizontal);
		vec3_t move_dir = {horizontal[1], -horizontal[0], 0.0f};
		if (state->strafe_side < 0)
		{
			VectorNegate(move_dir, move_dir);
		}

		if (AI_DMRandom() > 0.9f)
		{
			VectorAdd(move_dir, backward, move_dir);
		}
		else if (distance > AI_DM_IDEAL_ATTACK_DISTANCE + AI_DM_ATTACK_DISTANCE_RANGE)
		{
			VectorAdd(move_dir, forward, move_dir);
		}
		else if (distance < AI_DM_IDEAL_ATTACK_DISTANCE - AI_DM_ATTACK_DISTANCE_RANGE)
		{
			VectorAdd(move_dir, backward, move_dir);
		}

		AI_DMVectorNormalise(move_dir);
		if (BotMoveInDirection(client_state->move_handle,
			move_dir,
			AI_DM_MAX_CHASE_SPEED,
			move_type))
		{
			return;
		}

		state->strafe_side = -state->strafe_side;
		state->attackstrafe_timer = 0.0f;
	}
}

/*
=============
AI_DMState_RefreshConfig

Refreshes cached deathmatch libvar values.
=============
*/
static void AI_DMState_RefreshConfig(ai_dm_state_t *state)
{
	if (state == NULL)
	{
		return;
	}

	if (state->dmflags != NULL)
	{
		state->dmflags_value = (int)state->dmflags->value;
		if (state->dmflags->modified && state->dmflags->name != NULL)
		{
			LibVarSetNotModified(state->dmflags->name);
		}
	}
	else
	{
		state->dmflags_value = 0;
	}

	if (state->rocketjump != NULL)
	{
		if (!state->config_initialised || state->rocketjump->modified)
		{
			state->allow_rocket_jump = (state->rocketjump->value != 0.0f);
			if (state->rocketjump->name != NULL)
			{
				LibVarSetNotModified(state->rocketjump->name);
			}
		}
	}
	else
	{
		state->allow_rocket_jump = false;
	}

	if (state->usehook != NULL)
	{
		if (!state->config_initialised || state->usehook->modified)
		{
			state->allow_hook = (state->usehook->value != 0.0f);
			if (state->usehook->name != NULL)
			{
				LibVarSetNotModified(state->usehook->name);
			}
		}
	}
	else
	{
		state->allow_hook = false;
	}

	state->config_initialised = true;
}

/*
=============
AI_DMState_Create

Allocates and initialises per-client deathmatch state.
=============
*/
ai_dm_state_t *AI_DMState_Create(int client_number)
{
	ai_dm_state_t *state = (ai_dm_state_t *)calloc(1, sizeof(*state));
	if (state == NULL)
	{
		return NULL;
	}

	state->client_number = client_number;
	state->attack_cooldown = AI_DM_ATTACK_COOLDOWN;
	state->avoid_duration = AI_DM_AVOID_DURATION;
	state->last_selected_weapon = -1;
	state->last_avoid_entity = -1;
	state->reaction_delay = AI_DM_REACTION_DELAY;
	state->chase_duration = AI_DM_CHASE_DURATION;
	state->attack_strafe_interval = AI_DM_ATTACK_STRAFE_INTERVAL;
	state->attack_chase_duration = AI_DM_ATTACK_CHASE_DURATION;
	state->dmflags = Bridge_DMFlags();
	state->rocketjump = Bridge_RocketJump();
	state->usehook = Bridge_UseHook();

	AI_DMState_Reset(state);
	return state;
}

/*
=============
AI_DMState_Destroy

Releases per-client deathmatch state.
=============
*/
void AI_DMState_Destroy(ai_dm_state_t *state)
{
	if (state == NULL)
	{
		return;
	}

	free(state);
}

/*
=============
AI_DMState_Reset

Restores per-client deathmatch state to its initial values.
=============
*/
void AI_DMState_Reset(ai_dm_state_t *state)
{
	if (state == NULL)
	{
		return;
	}

	state->last_attack_time = -FLT_MAX;
	state->last_jump_time = -FLT_MAX;
	state->last_selected_weapon = -1;
	state->last_avoid_entity = -1;
	state->last_avoid_time = -FLT_MAX;
	state->enemy_entity = -1;
	state->last_enemy_area = 0;
	state->revenge_enemy = -1;
	state->revenge_kills = 0;
	state->enemy_visible = false;
	state->strafe_side = 1;
	state->attack_jump_latched = false;
	state->last_pm_type = PM_NORMAL;
	state->enemyvisible_time = -FLT_MAX;
	state->enemysight_time = -FLT_MAX;
	state->enemydeath_time = -FLT_MAX;
	state->enemyposition_time = -FLT_MAX;
	state->chase_time = -FLT_MAX;
	state->attackstrafe_timer = 0.0f;
	state->attackcrouch_time = -FLT_MAX;
	state->attackchase_time = -FLT_MAX;
	state->attackjump_time = -FLT_MAX;
	state->firethrottlewait_time = -FLT_MAX;
	state->firethrottleshoot_time = -FLT_MAX;
	state->last_think_time = -FLT_MAX;
	state->view_initialised = false;
	VectorClear(state->last_enemy_origin);
	VectorClear(state->last_enemy_velocity);
	VectorClear(state->viewangles);
	VectorClear(state->ideal_viewangles);
	VectorClear(state->viewanglespeed);
	VectorClear(state->aim_target);
	state->attack_latched = false;
	state->config_initialised = false;
	AI_DMState_RefreshConfig(state);
}

/*
=============
AI_DMState_SetClient

Moves a deathmatch state to a different client number.
=============
*/
void AI_DMState_SetClient(ai_dm_state_t *state, int client_number)
{
	if (state == NULL)
	{
		return;
	}

	state->client_number = client_number;
}

/*
=============
AI_DMState_SetAttackChaseTestState

Injects the otherwise unwritten retail chase deadline and cached enemy area
for deterministic coverage of the dormant BotAttackMove branch.
=============
*/
void AI_DMState_SetAttackChaseTestState(ai_dm_state_t *state,
	float attackchase_time,
	int last_enemy_area)
{
	if (state == NULL)
	{
		return;
	}

	state->attackchase_time = attackchase_time;
	state->last_enemy_area = last_enemy_area;
}

/*
=============
AI_DMState_GetMetrics

Copies observable state into the parity-test metrics structure.
=============
*/
void AI_DMState_GetMetrics(const ai_dm_state_t *state, ai_dm_metrics_t *out_metrics)
{
	if (state == NULL || out_metrics == NULL)
	{
		return;
	}

	memset(out_metrics, 0, sizeof(*out_metrics));
	out_metrics->client_number = state->client_number;
	out_metrics->enemy_entity = state->enemy_entity;
	out_metrics->revenge_enemy = state->revenge_enemy;
	out_metrics->revenge_kills = state->revenge_kills;
	out_metrics->enemy_visible = state->enemy_visible;
	out_metrics->last_attack_time = state->last_attack_time;
	out_metrics->last_jump_time = state->last_jump_time;
	out_metrics->enemyvisible_time = state->enemyvisible_time;
	out_metrics->enemysight_time = state->enemysight_time;
	out_metrics->enemydeath_time = state->enemydeath_time;
	out_metrics->enemyposition_time = state->enemyposition_time;
	out_metrics->chase_time = state->chase_time;
	out_metrics->attackstrafe_timer = state->attackstrafe_timer;
	out_metrics->attackcrouch_time = state->attackcrouch_time;
	out_metrics->attackchase_time = state->attackchase_time;
	out_metrics->attackjump_time = state->attackjump_time;
	out_metrics->firethrottlewait_time = state->firethrottlewait_time;
	out_metrics->firethrottleshoot_time = state->firethrottleshoot_time;
	out_metrics->reaction_delay = state->reaction_delay;
	out_metrics->chase_duration = state->chase_duration;
	out_metrics->attack_strafe_interval = state->attack_strafe_interval;
	VectorCopy(state->last_enemy_origin, out_metrics->last_enemy_origin);
	VectorCopy(state->last_enemy_velocity, out_metrics->last_enemy_velocity);
	VectorCopy(state->viewangles, out_metrics->viewangles);
	VectorCopy(state->ideal_viewangles, out_metrics->ideal_viewangles);
	VectorCopy(state->viewanglespeed, out_metrics->viewanglespeed);
	VectorCopy(state->aim_target, out_metrics->aim_target);
	out_metrics->attack_latched = state->attack_latched;
}

/*
=============
AI_DMState_ClampSpeed

Computes the current chase speed for a remembered enemy.
=============
*/
static float AI_DMState_ClampSpeed(const ai_dm_state_t *state, const ai_dm_enemy_info_t *enemy)
{
	if (state == NULL || enemy == NULL || !enemy->valid)
	{
		return 0.0f;
	}

	float base_speed = AI_DM_MAX_CHASE_SPEED;
	if (!state->allow_hook && enemy->distance > 512.0f)
	{
		base_speed *= 0.5f;
	}
	return base_speed;
}

/*
=============
AI_DMState_ShouldRecordAvoid

Determines whether a failed path should refresh an avoid entry.
=============
*/
static bool AI_DMState_ShouldRecordAvoid(const ai_dm_state_t *state, const ai_dm_enemy_info_t *enemy, float now)
{
	if (state == NULL || enemy == NULL || !enemy->valid)
	{
		return false;
	}

	if (enemy->entity < 0)
	{
		return false;
	}

	if (state->last_avoid_entity == enemy->entity)
	{
		if (now - state->last_avoid_time < 0.1f)
		{
			return false;
		}
	}

	return true;
}

/*
=============
AI_DMState_Update

Updates reconstructed per-client combat state for one frame.
=============
*/
void AI_DMState_Update(ai_dm_state_t *state, const bot_client_state_t *client_state,
					   const ai_goal_selection_t *selection, const ai_dm_enemy_info_t *enemy,
					   const bot_input_t *last_move_command, float now)
{
	if (state == NULL || client_state == NULL)
	{
		return;
	}

	float think_time = AI_DM_RETAIL_THINK_TIME;
	if (last_move_command != NULL && last_move_command->thinktime > 0.0f)
	{
		think_time = last_move_command->thinktime;
	}
	else if (state->last_think_time > -FLT_MAX * 0.5f &&
		now > state->last_think_time)
	{
		think_time = now - state->last_think_time;
	}
	state->last_think_time = now;
	AI_DMState_RefreshConfig(state);

	int client = client_state->client_number;
	if (client < 0)
	{
		return;
	}
	if (!state->view_initialised)
	{
		VectorCopy(client_state->last_client_update.viewangles,
			state->viewangles);
		state->view_initialised = true;
	}

	state->reaction_delay = AI_DMCharacteristic(client_state, AI_DM_CHARACTERISTIC_REACTION_TIME, AI_DM_REACTION_DELAY);

	bool movement_controls_weapon =
		client_state->has_move_result && (client_state->last_move_result.flags & MOVERESULT_MOVEMENTWEAPON);

	int desired_weapon = client_state->current_weapon;
	if (movement_controls_weapon)
	{
		desired_weapon = client_state->last_move_result.weapon;
	}

	bool allow_weapon_selection = (!movement_controls_weapon || desired_weapon > 0);
	if (movement_controls_weapon && desired_weapon <= 0)
	{
		state->last_selected_weapon = -1;
	}

	if (allow_weapon_selection && desired_weapon > 0 && desired_weapon != state->last_selected_weapon)
	{
		EA_SelectWeapon(client, desired_weapon);
		state->last_selected_weapon = desired_weapon;
	}

	bool enemy_valid = (enemy != NULL) && enemy->valid;
	vec3_t eye_position = {0.0f, 0.0f, 0.0f};
	VectorCopy(client_state->last_client_update.origin, eye_position);
	eye_position[2] += client_state->last_client_update.viewoffset[2];

	pmtype_t pm_type = client_state->last_client_update.pm_type;
	if (state->last_pm_type != PM_DEAD && pm_type == PM_DEAD)
	{
		if (state->enemy_entity >= 0)
		{
			state->revenge_enemy = state->enemy_entity;
			state->revenge_kills += 1;
		}

		state->enemy_entity = -1;
		state->enemy_visible = false;
		state->chase_time = -FLT_MAX;
	}
	state->last_pm_type = pm_type;

	if (enemy_valid)
	{
		bool enemy_changed = (enemy->entity != state->enemy_entity);
		bool enemy_was_visible = state->enemy_visible;

		state->enemy_entity = enemy->entity;
		state->enemy_visible = enemy->visible;
		state->enemyvisible_time = (enemy->last_seen_time > -FLT_MAX * 0.5f) ? enemy->last_seen_time : now;
		state->chase_time = now + state->chase_duration;

		if (enemy_changed)
		{
			state->enemysight_time = now;
		}
		else if (!enemy_was_visible)
		{
			state->enemysight_time = now;
		}
		else if (state->enemysight_time <= -FLT_MAX * 0.5f)
		{
			state->enemysight_time = now;
		}

		if (state->enemyposition_time > -FLT_MAX * 0.5f)
		{
			float position_delta = now - state->enemyposition_time;
			if (position_delta > 0.0001f)
			{
				vec3_t delta;
				VectorSubtract(enemy->origin, state->last_enemy_origin, delta);
				float scale = 1.0f / position_delta;
				state->last_enemy_velocity[0] = delta[0] * scale;
				state->last_enemy_velocity[1] = delta[1] * scale;
				state->last_enemy_velocity[2] = delta[2] * scale;
			}
		}
		else
		{
			VectorCopy(enemy->velocity, state->last_enemy_velocity);
		}
		VectorCopy(enemy->origin, state->last_enemy_origin);
		state->enemyposition_time = now;

		vec3_t forward;
		VectorSubtract(enemy->origin, eye_position, forward);
		float distance = AI_DMVectorNormalise(forward);
		int travel_flags = 0;
		if (selection != NULL && selection->valid)
		{
			travel_flags = selection->candidate.travel_flags;
		}
		AI_DMAttackMove(state,
			client_state,
			forward,
			distance,
			now,
			think_time,
			travel_flags);

		AI_DMAimAtEnemy(state,
			client_state,
			enemy,
			eye_position,
			think_time);

		if (!AI_DMCheckAttack(state, client_state, enemy, now))
		{
			state->firethrottlewait_time = now;
		}

		float vertical = enemy->origin[2] - eye_position[2];
		if (state->allow_rocket_jump && vertical > AI_DM_MIN_ROCKET_VERTICAL)
		{
			if (now >= state->last_jump_time + state->attack_cooldown)
			{
				EA_Jump(client);
				state->last_jump_time = now;
				state->attackjump_time = now;
			}
		}

	}
	else
	{
		if (state->enemy_visible)
		{
			state->enemy_visible = false;
			state->enemydeath_time = now;
		}

		if (state->chase_time > now && state->enemy_entity >= 0)
		{
			vec3_t predicted;
			VectorCopy(state->last_enemy_origin, predicted);
			if (state->enemyposition_time > -FLT_MAX * 0.5f)
			{
				float lead = now - state->enemyposition_time;
				predicted[0] += state->last_enemy_velocity[0] * lead;
				predicted[1] += state->last_enemy_velocity[1] * lead;
				predicted[2] += state->last_enemy_velocity[2] * lead;
			}

			vec3_t direction;
			VectorSubtract(predicted, eye_position, direction);
			float distance = AI_DMVectorNormalise(direction);

			if (distance > 0.0f)
			{
				ai_dm_enemy_info_t chase_enemy = {
					.valid = true,
					.visible = false,
					.entity = state->enemy_entity,
					.distance = distance,
				};
				float speed = AI_DMState_ClampSpeed(state, &chase_enemy);
				EA_Move(client, direction, speed);
				EA_LookAtPoint(client, eye_position, predicted);
			}
		}
		else
		{
			state->enemy_entity = -1;
			state->attackstrafe_timer = 0.0f;
			state->strafe_side = 1;
		}
	}

	bool path_failed = false;
	if (selection != NULL && selection->valid)
	{
		float selection_score = selection->score;
		float command_speed = (last_move_command != NULL) ? last_move_command->speed : selection_score;
		if (selection_score <= 0.0f || command_speed <= 0.0f)
		{
			path_failed = true;
		}
	}

	if (path_failed && enemy_valid && client_state->goal_state != NULL)
	{
		if (AI_DMState_ShouldRecordAvoid(state, enemy, now))
		{
			ai_avoid_list_t *avoid = AI_GoalState_GetAvoidList(client_state->goal_state);
			if (avoid != NULL)
			{
				float expiry = now + state->avoid_duration;
				if (AI_AvoidList_Add(avoid, enemy->entity, expiry))
				{
					state->last_avoid_entity = enemy->entity;
					state->last_avoid_time = now;
				}
			}
		}
	}
}
