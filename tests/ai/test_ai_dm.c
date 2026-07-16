#include <stdarg.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "botlib/aas/aas_map.h"
#include "botlib/ai/ai_dm.h"
#include "botlib/ai_move/bot_move.h"
#include "botlib/ea/ea_local.h"
#include "botlib/common/l_libvar.h"
#include "botlib/ai/goal_move_orchestrator.h"
#include "botlib/interface/bot_state.h"
#include "q2bridge/botlib.h"

#include "shared/q_shared.h"

#define DM_ASSERT(expr)                                                                                                \
	do                                                                                                                 \
	{                                                                                                                  \
		if (!(expr))                                                                                                   \
		{                                                                                                              \
			fprintf(stderr, "[dm_test] Assertion failed: %s (%s:%d)\n", #expr, __FILE__, __LINE__);                    \
			exit(EXIT_FAILURE);                                                                                        \
		}                                                                                                              \
	} while (0)

#define DM_ASSERT_FLOAT_CLOSE(actual, expected, tolerance)                                                             \
	do                                                                                                                 \
	{                                                                                                                  \
		float dm_diff = fabsf((float)(actual) - (float)(expected));                                                    \
		if (dm_diff > (float)(tolerance))                                                                              \
		{                                                                                                              \
			fprintf(stderr, "[dm_test] Float assertion failed: %s ≈ %s (diff=%f tol=%f) (%s:%d)\n", #actual,           \
					#expected, dm_diff, (float)(tolerance), __FILE__, __LINE__);                                       \
			exit(EXIT_FAILURE);                                                                                        \
		}                                                                                                              \
	} while (0)

typedef struct dm_test_capture_s
{
	bool pending;
	int client;
	bot_input_t command;
} dm_test_capture_t;

typedef struct dm_ea_state_s
{
	bool initialised;
	bot_input_t base;
	vec3_t move_dir;
	float move_speed;
	bool has_move;
	vec3_t view_origin;
	vec3_t view_target;
	bool has_view;
	vec3_t viewangles;
	bool has_viewangles;
	int actionflags;
	int weapon;
} dm_ea_state_t;

#define DM_MAX_TEST_CLIENTS 8
#define DM_CHARACTERISTIC_COUNT 54
#define DM_CHARACTERISTIC_ATTACK_SKILL 4
#define DM_CHARACTERISTIC_AIM_SKILL 7
#define DM_CHARACTERISTIC_AIM_ACCURACY 8
#define DM_CHARACTERISTIC_VIEW_FACTOR 9
#define DM_CHARACTERISTIC_VIEW_MAXCHANGE 10
#define DM_CHARACTERISTIC_REACTION_TIME 11
#define DM_CHARACTERISTIC_CROUCHER 24
#define DM_CHARACTERISTIC_JUMPER 25
#define DM_CHARACTERISTIC_PIZZA_PREFERENCE 48
#define DM_MAX_MOVE_CALLS 8
#define DM_MAX_TRACE_CALLS 32

typedef struct dm_move_call_s
{
	int move_handle;
	vec3_t direction;
	float speed;
	int type;
} dm_move_call_t;

typedef struct dm_init_move_call_s
{
	int move_handle;
	bot_initmove_t initmove;
} dm_init_move_call_t;

typedef struct dm_goal_move_call_s
{
	int move_handle;
	vec3_t origin;
	int areanum;
	vec3_t mins;
	vec3_t maxs;
	int entitynum;
	int travel_flags;
} dm_goal_move_call_t;

typedef struct dm_trace_call_s
{
	vec3_t start;
	vec3_t mins;
	vec3_t maxs;
	vec3_t end;
	bool has_mins;
	bool has_maxs;
	int passent;
	int mask;
} dm_trace_call_t;

static dm_test_capture_t g_dm_capture;
static dm_ea_state_t g_dm_ea_state[DM_MAX_TEST_CLIENTS];
static float g_dm_characteristics[DM_CHARACTERISTIC_COUNT];
static int g_dm_characteristic_call_count[DM_CHARACTERISTIC_COUNT];
static float g_dm_characteristic_minimum[DM_CHARACTERISTIC_COUNT];
static float g_dm_characteristic_maximum[DM_CHARACTERISTIC_COUNT];
static int g_dm_characteristic_last_handle;
static dm_move_call_t g_dm_move_calls[DM_MAX_MOVE_CALLS];
static int g_dm_move_call_count;
static int g_dm_move_results[DM_MAX_MOVE_CALLS];
static int g_dm_move_result_count;
static dm_init_move_call_t g_dm_init_move_call;
static dm_goal_move_call_t g_dm_goal_move_call;
static int g_dm_init_move_call_count;
static int g_dm_goal_move_call_count;
static int g_dm_move_sequence;
static int g_dm_init_move_order;
static int g_dm_goal_move_order;
static bot_weapon_info_t g_dm_weapon_info;
static bool g_dm_weapon_available;
static int g_dm_weapon_handle;
static bsp_trace_t g_dm_trace_result;
static int g_dm_trace_count;
static vec3_t g_dm_trace_start;
static vec3_t g_dm_trace_mins;
static vec3_t g_dm_trace_maxs;
static vec3_t g_dm_trace_end;
static int g_dm_trace_passent;
static int g_dm_trace_mask;
static bsp_trace_t g_dm_trace_results[DM_MAX_TRACE_CALLS];
static int g_dm_trace_result_count;
static dm_trace_call_t g_dm_trace_calls[DM_MAX_TRACE_CALLS];
static int g_dm_bot_state_capacity = DM_MAX_TEST_CLIENTS - 1;
static int g_dm_same_team_result;
static int g_dm_same_team_entity;
static const bot_client_state_t *g_dm_same_team_state;
static qboolean g_dm_entity_visible_result;
static int g_dm_entity_visible_call_count;
static int g_dm_entity_visible_viewer;
static vec3_t g_dm_entity_visible_eye;
static vec3_t g_dm_entity_visible_viewangles;
static float g_dm_entity_visible_field_of_view;
static int g_dm_entity_visible_target;

/*
=============
dm_ea_get_state
=============
*/
static dm_ea_state_t *dm_ea_get_state(int client)
{
	if (client < 0 || client >= DM_MAX_TEST_CLIENTS)
	{
		return NULL;
	}

	return &g_dm_ea_state[client];
}

/*
=============
dm_ea_reset_state
=============
*/
static void dm_ea_reset_state(dm_ea_state_t *state)
{
	if (state == NULL)
	{
		return;
	}

	memset(state, 0, sizeof(*state));
}

/*
=============
dm_compute_viewangles
=============
*/
static void dm_compute_viewangles(const vec3_t eye, const vec3_t target, vec3_t out)
{
	vec3_t direction;
	VectorSubtract(target, eye, direction);

	float yaw = atan2f(direction[1], direction[0]) * (180.0f / (float)M_PI);
	if (yaw < 0.0f)
	{
		yaw += 360.0f;
	}

	float forward = sqrtf(direction[0] * direction[0] + direction[1] * direction[1]);
	float pitch = 0.0f;
	if (forward > 0.0001f || fabsf(direction[2]) > 0.0001f)
	{
		pitch = atan2f(-direction[2], forward) * (180.0f / (float)M_PI);
	}

	out[PITCH] = pitch;
	out[YAW] = yaw;
	out[ROLL] = 0.0f;
}

/*
=============
dm_retail_random

Mirrors the low-15-bit retail random fraction for deterministic expectations.
=============
*/
static float dm_retail_random(void)
{
	return (float)(rand() & 0x7fff) * 3.05185094e-05f;
}

/*
=============
dm_retail_crandom

Mirrors the signed retail random fraction used by aim perturbations.
=============
*/
static float dm_retail_crandom(void)
{
	return (dm_retail_random() - 0.5f) * 2.0f;
}

/*
=============
dm_retail_angle_mod

Quantises an expected angle to Gladiator's unsigned 16-bit grid.
=============
*/
static float dm_retail_angle_mod(float angle)
{
	return (360.0f / 65536.0f) *
		(float)(((int)(angle * (65536.0f / 360.0f))) & 65535);
}

/*
=============
EA_ResetClient
=============
*/
int EA_ResetClient(int client)
{
	dm_ea_state_t *state = dm_ea_get_state(client);
	if (state == NULL)
	{
		return BLERR_INVALIDCLIENTNUMBER;
	}

	dm_ea_reset_state(state);
	state->initialised = true;
	return BLERR_NOERROR;
}

/*
=============
EA_SubmitInput
=============
*/
int EA_SubmitInput(int client, const bot_input_t *input)
{
	dm_ea_state_t *state = dm_ea_get_state(client);
	if (state == NULL || input == NULL)
	{
		return BLERR_INVALIDCLIENTNUMBER;
	}

	state->initialised = true;
	state->base = *input;
	state->weapon = input->weapon;
	return BLERR_NOERROR;
}

/*
=============
EA_Move
=============
*/
void EA_Move(int client, const vec3_t direction, float speed)
{
	dm_ea_state_t *state = dm_ea_get_state(client);
	if (state == NULL || !state->initialised)
	{
		return;
	}

	VectorCopy(direction, state->move_dir);
	state->move_speed = speed;
	state->has_move = true;
}

/*
=============
EA_LookAtPoint
=============
*/
void EA_LookAtPoint(int client, const vec3_t eye_position, const vec3_t target_position)
{
	dm_ea_state_t *state = dm_ea_get_state(client);
	if (state == NULL || !state->initialised)
	{
		return;
	}

	VectorCopy(eye_position, state->view_origin);
	VectorCopy(target_position, state->view_target);
	state->has_view = true;
}

/*
=============
EA_View

Captures the exact view angles submitted by retail combat smoothing.
=============
*/
void EA_View(int client, const vec3_t viewangles)
{
	dm_ea_state_t *state = dm_ea_get_state(client);
	if (state == NULL || !state->initialised || viewangles == NULL)
	{
		return;
	}

	VectorCopy(viewangles, state->viewangles);
	state->has_viewangles = true;
}

/*
=============
EA_SelectWeapon
=============
*/
void EA_SelectWeapon(int client, int weapon)
{
	dm_ea_state_t *state = dm_ea_get_state(client);
	if (state == NULL || !state->initialised)
	{
		return;
	}

	state->weapon = weapon;
}

/*
=============
EA_Attack
=============
*/
void EA_Attack(int client)
{
	dm_ea_state_t *state = dm_ea_get_state(client);
	if (state == NULL || !state->initialised)
	{
		return;
	}

	state->actionflags |= ACTION_ATTACK;
}

/*
=============
EA_Jump
=============
*/
void EA_Jump(int client)
{
	dm_ea_state_t *state = dm_ea_get_state(client);
	if (state == NULL || !state->initialised)
	{
		return;
	}

	state->actionflags |= ACTION_JUMP;
}

/*
=============
EA_GetInput
=============
*/
int EA_GetInput(int client, float thinktime, bot_input_t *out_input)
{
	dm_ea_state_t *state = dm_ea_get_state(client);
	if (state == NULL || !state->initialised || out_input == NULL)
	{
		return BLERR_INVALIDCLIENTNUMBER;
	}

	bot_input_t command = state->base;
	command.thinktime = thinktime;

	if (state->has_move)
	{
		VectorCopy(state->move_dir, command.dir);
		command.speed = state->move_speed;
	}

	if (state->has_view)
	{
		dm_compute_viewangles(state->view_origin, state->view_target, command.viewangles);
	}
	if (state->has_viewangles)
	{
		VectorCopy(state->viewangles, command.viewangles);
	}

	command.weapon = state->weapon;
	command.actionflags |= state->actionflags;

	*out_input = command;

	g_dm_capture.pending = true;
	g_dm_capture.client = client;
	g_dm_capture.command = command;

	state->has_move = false;
	state->has_view = false;
	state->has_viewangles = false;
	state->actionflags = 0;
	VectorClear(state->move_dir);
	state->move_speed = 0.0f;

	return BLERR_NOERROR;
}

/*
=============
BotMoveInDirection

Captures direct-movement retries and emits EA state only on success.
=============
*/
int BotMoveInDirection(int move_handle, const vec3_t direction, float speed, int type)
{
	int call_index = g_dm_move_call_count;
	if (call_index < DM_MAX_MOVE_CALLS)
	{
		g_dm_move_calls[call_index].move_handle = move_handle;
		VectorCopy(direction, g_dm_move_calls[call_index].direction);
		g_dm_move_calls[call_index].speed = speed;
		g_dm_move_calls[call_index].type = type;
	}
	g_dm_move_call_count += 1;

	int result = 1;
	if (call_index < g_dm_move_result_count)
	{
		result = g_dm_move_results[call_index];
	}
	if (!result)
	{
		return 0;
	}

	int client = move_handle - 1;
	dm_ea_state_t *state = dm_ea_get_state(client);
	if (state == NULL)
	{
		return 0;
	}

	EA_Move(client, direction, speed);
	if ((type & MOVE_JUMP) != 0)
	{
		state->actionflags |= ACTION_JUMP;
	}
	if ((type & MOVE_CROUCH) != 0)
	{
		state->actionflags |= ACTION_CROUCH;
	}
	return 1;
}

/*
=============
BotInitMoveState

Captures the retail attack-chase movement setup without requiring move AI.
=============
*/
void BotInitMoveState(int move_handle, const bot_initmove_t *initmove)
{
	g_dm_init_move_call_count += 1;
	g_dm_init_move_order = ++g_dm_move_sequence;
	g_dm_init_move_call.move_handle = move_handle;
	if (initmove != NULL)
	{
		g_dm_init_move_call.initmove = *initmove;
	}
}

/*
=============
BotMoveToGoal

Captures only the goal fields written by retail's dormant attack-chase branch.
=============
*/
void BotMoveToGoal(bot_moveresult_t *result,
	int move_handle,
	const bot_goal_t *goal,
	int travel_flags)
{
	if (result != NULL)
	{
		memset(result, 0, sizeof(*result));
	}

	g_dm_goal_move_call_count += 1;
	g_dm_goal_move_order = ++g_dm_move_sequence;
	g_dm_goal_move_call.move_handle = move_handle;
	g_dm_goal_move_call.travel_flags = travel_flags;
	if (goal == NULL)
	{
		return;
	}

	VectorCopy(goal->origin, g_dm_goal_move_call.origin);
	g_dm_goal_move_call.areanum = goal->areanum;
	VectorCopy(goal->mins, g_dm_goal_move_call.mins);
	VectorCopy(goal->maxs, g_dm_goal_move_call.maxs);
	g_dm_goal_move_call.entitynum = goal->entitynum;
}

/*
=============
BotCurrentWeaponInfo

Returns the deterministic current weapon record used by aim-stage tests.
=============
*/
const bot_weapon_info_t *BotCurrentWeaponInfo(int weaponstate)
{
	g_dm_weapon_handle = weaponstate;
	return g_dm_weapon_available ? &g_dm_weapon_info : NULL;
}

/*
=============
BotState_ClientCapacity

Returns the configured inclusive client bound used by the retail sweep guard.
=============
*/
int BotState_ClientCapacity(void)
{
	return g_dm_bot_state_capacity;
}

/*
=============
BotAI_SameTeam

Captures the entity-number handoff to retail's shared team predicate.
=============
*/
int BotAI_SameTeam(const bot_client_state_t *state, int entity)
{
	g_dm_same_team_state = state;
	g_dm_same_team_entity = entity;
	return g_dm_same_team_result;
}

/*
=============
Q2_Trace

Captures the retail muzzle trace and returns the configured BSP result.
=============
*/
bsp_trace_t Q2_Trace(vec3_t start,
	vec3_t mins,
	vec3_t maxs,
	vec3_t end,
	int passent,
	int contentmask)
{
	int call_index = g_dm_trace_count;
	g_dm_trace_count += 1;
	VectorCopy(start, g_dm_trace_start);
	if (mins != NULL)
	{
		VectorCopy(mins, g_dm_trace_mins);
	}
	else
	{
		VectorClear(g_dm_trace_mins);
	}
	if (maxs != NULL)
	{
		VectorCopy(maxs, g_dm_trace_maxs);
	}
	else
	{
		VectorClear(g_dm_trace_maxs);
	}
	VectorCopy(end, g_dm_trace_end);
	g_dm_trace_passent = passent;
	g_dm_trace_mask = contentmask;
	if (call_index >= 0 && call_index < DM_MAX_TRACE_CALLS)
	{
		dm_trace_call_t *call = &g_dm_trace_calls[call_index];
		memset(call, 0, sizeof(*call));
		VectorCopy(start, call->start);
		VectorCopy(end, call->end);
		call->has_mins = mins != NULL;
		call->has_maxs = maxs != NULL;
		if (mins != NULL)
		{
			VectorCopy(mins, call->mins);
		}
		if (maxs != NULL)
		{
			VectorCopy(maxs, call->maxs);
		}
		call->passent = passent;
		call->mask = contentmask;
	}
	if (call_index >= 0 && call_index < g_dm_trace_result_count)
	{
		return g_dm_trace_results[call_index];
	}
	return g_dm_trace_result;
}

/*
=============
AAS_EntityVisible

Captures the direct retail visibility adapter used by the attack gate.
=============
*/
int AAS_EntityVisible(int viewer,
	const vec3_t eye,
	const vec3_t viewangles,
	float fieldofview,
	int entnum)
{
	g_dm_entity_visible_call_count += 1;
	g_dm_entity_visible_viewer = viewer;
	VectorCopy(eye, g_dm_entity_visible_eye);
	VectorCopy(viewangles, g_dm_entity_visible_viewangles);
	g_dm_entity_visible_field_of_view = fieldofview;
	g_dm_entity_visible_target = entnum;
	return g_dm_entity_visible_result;
}

/*
=============
dm_trace_result_at

Returns one cleared scripted trace result and extends the active sequence.
=============
*/
static bsp_trace_t *dm_trace_result_at(int index)
{
	DM_ASSERT(index >= 0);
	DM_ASSERT(index < DM_MAX_TRACE_CALLS);
	memset(&g_dm_trace_results[index], 0, sizeof(g_dm_trace_results[index]));
	if (g_dm_trace_result_count <= index)
	{
		g_dm_trace_result_count = index + 1;
	}
	return &g_dm_trace_results[index];
}

/*
=============
Bridge_DMFlags
=============
*/
libvar_t *Bridge_DMFlags(void)
{
	return NULL;
}

/*
=============
Bridge_RocketJump
=============
*/
libvar_t *Bridge_RocketJump(void)
{
	return NULL;
}

/*
=============
Bridge_UseHook
=============
*/
libvar_t *Bridge_UseHook(void)
{
	return NULL;
}

/*
=============
Characteristic_BFloat

Provides deterministic bounded character values to the focused DM harness.
=============
*/
float Characteristic_BFloat(int handle, int index, float minimum, float maximum)
{
	g_dm_characteristic_last_handle = handle;

	if (index < 0 || index >= DM_CHARACTERISTIC_COUNT)
	{
		return minimum;
	}

	g_dm_characteristic_call_count[index] += 1;
	g_dm_characteristic_minimum[index] = minimum;
	g_dm_characteristic_maximum[index] = maximum;
	float value = g_dm_characteristics[index];
	if (value < minimum)
	{
		value = minimum;
	}
	if (value > maximum)
	{
		value = maximum;
	}
	return value;
}

/*
=============
LibVarSetNotModified
=============
*/
void LibVarSetNotModified(const char *var_name)
{
	(void)var_name;
}

/*
=============
AI_GoalState_GetAvoidList
=============
*/
ai_avoid_list_t *AI_GoalState_GetAvoidList(ai_goal_state_t *state)
{
	(void)state;
	return NULL;
}

/*
=============
AI_AvoidList_Add
=============
*/
bool AI_AvoidList_Add(ai_avoid_list_t *list, int id, float expiry)
{
	(void)list;
	(void)id;
	(void)expiry;
	return false;
}

/*
=============
dm_capture_reset
=============
*/
static void dm_capture_reset(void)
{
	g_dm_capture.pending = false;
	g_dm_capture.client = -1;
	memset(&g_dm_capture.command, 0, sizeof(g_dm_capture.command));
}

/*
=============
dm_move_capture_reset

Clears direct-movement calls and restores default-success behavior.
=============
*/
static void dm_move_capture_reset(void)
{
	memset(g_dm_move_calls, 0, sizeof(g_dm_move_calls));
	memset(g_dm_move_results, 0, sizeof(g_dm_move_results));
	g_dm_move_call_count = 0;
	g_dm_move_result_count = 0;
	memset(&g_dm_init_move_call, 0, sizeof(g_dm_init_move_call));
	memset(&g_dm_goal_move_call, 0, sizeof(g_dm_goal_move_call));
	g_dm_init_move_call_count = 0;
	g_dm_goal_move_call_count = 0;
	g_dm_move_sequence = 0;
	g_dm_init_move_order = 0;
	g_dm_goal_move_order = 0;
}

/*
=============
dm_mock_bot_input
=============
*/
static void dm_mock_bot_input(int client, bot_input_t *input)
{
	(void)client;
	(void)input;
}

/*
=============
dm_mock_bot_client_command
=============
*/
static void dm_mock_bot_client_command(int client, char *fmt, ...)
{
	(void)client;
	(void)fmt;
}

/*
=============
dm_test_setup

Resets deterministic DM and character test state before each focused case.
=============
*/
static int dm_test_setup(void)
{
	dm_capture_reset();
	for (int i = 0; i < DM_MAX_TEST_CLIENTS; ++i)
	{
		memset(&g_dm_ea_state[i], 0, sizeof(g_dm_ea_state[i]));
	}
	memset(g_dm_characteristics, 0, sizeof(g_dm_characteristics));
	memset(g_dm_characteristic_call_count, 0, sizeof(g_dm_characteristic_call_count));
	memset(g_dm_characteristic_minimum, 0, sizeof(g_dm_characteristic_minimum));
	memset(g_dm_characteristic_maximum, 0, sizeof(g_dm_characteristic_maximum));
	g_dm_characteristics[DM_CHARACTERISTIC_ATTACK_SKILL] = 1.0f;
	g_dm_characteristics[DM_CHARACTERISTIC_REACTION_TIME] = 0.3f;
	g_dm_characteristic_last_handle = -1;
	dm_move_capture_reset();
	memset(&g_dm_weapon_info, 0, sizeof(g_dm_weapon_info));
	g_dm_weapon_available = false;
	g_dm_weapon_handle = -1;
	memset(&g_dm_trace_result, 0, sizeof(g_dm_trace_result));
	g_dm_trace_result.fraction = 1.0f;
	g_dm_trace_count = 0;
	VectorClear(g_dm_trace_start);
	VectorClear(g_dm_trace_mins);
	VectorClear(g_dm_trace_maxs);
	VectorClear(g_dm_trace_end);
	g_dm_trace_passent = -1;
	g_dm_trace_mask = 0;
	memset(g_dm_trace_results, 0, sizeof(g_dm_trace_results));
	g_dm_trace_result_count = 0;
	memset(g_dm_trace_calls, 0, sizeof(g_dm_trace_calls));
	g_dm_bot_state_capacity = DM_MAX_TEST_CLIENTS - 1;
	g_dm_same_team_result = 0;
	g_dm_same_team_entity = -1;
	g_dm_same_team_state = NULL;
	g_dm_entity_visible_result = qtrue;
	g_dm_entity_visible_call_count = 0;
	g_dm_entity_visible_viewer = -1;
	VectorClear(g_dm_entity_visible_eye);
	VectorClear(g_dm_entity_visible_viewangles);
	g_dm_entity_visible_field_of_view = 0.0f;
	g_dm_entity_visible_target = -1;
	srand(1);
	return 0;
}

/*
=============
dm_test_teardown
=============
*/
static void dm_test_teardown(void)
{
	dm_capture_reset();
}

/*
=============
dm_prepare_client_state
=============
*/
static void dm_prepare_client_state(bot_client_state_t *client_state, int client)
{
	memset(client_state, 0, sizeof(*client_state));
	client_state->client_number = client;
	client_state->entity_number = client + 1;
	client_state->character_handle = 7;
	client_state->move_handle = client + 1;
	client_state->current_weapon = 3;
	client_state->last_client_update.pm_type = PM_NORMAL;
	VectorClear(client_state->last_client_update.origin);
}

/*
=============
dm_enable_attack_weapon

Installs the valid ordinary weapon state required by retail BotCheckAttack.
=============
*/
static void dm_enable_attack_weapon(bot_client_state_t *client_state)
{
	DM_ASSERT(client_state != NULL);
	client_state->weapon_state = 1;
	g_dm_weapon_available = true;
	strcpy(g_dm_weapon_info.name, "Blaster");
	g_dm_characteristics[DM_CHARACTERISTIC_AIM_ACCURACY] = 1.0f;
}

/*
=============
dm_prepare_enemy
=============
*/
static void dm_prepare_enemy(ai_dm_enemy_info_t *enemy, int entity, const vec3_t origin, float distance,
							 float last_seen)
{
	memset(enemy, 0, sizeof(*enemy));
	enemy->valid = true;
	enemy->visible = true;
	enemy->entity = entity;
	VectorCopy(origin, enemy->origin);
	enemy->distance = distance;
	enemy->last_seen_time = last_seen;
}

/*
=============
dm_consume_input
=============
*/
static bot_input_t dm_consume_input(int client, float thinktime)
{
	bot_input_t command;
	memset(&command, 0, sizeof(command));

	dm_capture_reset();
	DM_ASSERT(EA_GetInput(client, thinktime, &command) == BLERR_NOERROR);
	DM_ASSERT(g_dm_capture.pending);
	DM_ASSERT(g_dm_capture.client == client);
	return g_dm_capture.command;
}

/*
=============
test_dm_reaction_delay_gates_attack

Pins the retail characteristic-11 reaction gate and its inclusive boundary.
=============
*/
static void test_dm_reaction_delay_gates_attack(void)
{
	const int client = 0;
	g_dm_characteristics[DM_CHARACTERISTIC_REACTION_TIME] = 0.4f;
	ai_dm_state_t *dm_state = AI_DMState_Create(client);
	DM_ASSERT(dm_state != NULL);

	bot_client_state_t client_state;
	dm_prepare_client_state(&client_state, client);
	dm_enable_attack_weapon(&client_state);

	ai_dm_enemy_info_t enemy;
	vec3_t enemy_origin = {128.0f, 0.0f, 0.0f};
	dm_prepare_enemy(&enemy, 2, enemy_origin, 128.0f, 1.0f);

	DM_ASSERT(EA_ResetClient(client) == BLERR_NOERROR);
	bot_input_t base = {0};
	DM_ASSERT(EA_SubmitInput(client, &base) == BLERR_NOERROR);

	AI_DMState_Update(dm_state, &client_state, NULL, &enemy, NULL, 1.0f);
	bot_input_t command = dm_consume_input(client, 0.1f);
	DM_ASSERT((command.actionflags & ACTION_ATTACK) == 0);

	ai_dm_metrics_t metrics;
	AI_DMState_GetMetrics(dm_state, &metrics);
	DM_ASSERT(metrics.enemy_entity == 2);
	DM_ASSERT(metrics.enemy_visible);
	DM_ASSERT_FLOAT_CLOSE(metrics.enemysight_time, 1.0f, 0.0001f);
	DM_ASSERT_FLOAT_CLOSE(metrics.reaction_delay, 0.4f, 0.0001f);
	DM_ASSERT(g_dm_characteristic_call_count[DM_CHARACTERISTIC_REACTION_TIME] == 1);
	DM_ASSERT(g_dm_characteristic_last_handle == client_state.character_handle);
	DM_ASSERT_FLOAT_CLOSE(g_dm_characteristic_minimum[DM_CHARACTERISTIC_REACTION_TIME],
		0.0f,
		0.0001f);
	DM_ASSERT_FLOAT_CLOSE(g_dm_characteristic_maximum[DM_CHARACTERISTIC_REACTION_TIME],
		1.0f,
		0.0001f);

	enemy.last_seen_time = 1.2f;
	AI_DMState_Update(dm_state, &client_state, NULL, &enemy, NULL, 1.2f);
	command = dm_consume_input(client, 0.1f);
	DM_ASSERT((command.actionflags & ACTION_ATTACK) == 0);

	enemy.last_seen_time = 1.4f;
	AI_DMState_Update(dm_state, &client_state, NULL, &enemy, NULL, 1.4f);
	command = dm_consume_input(client, 0.1f);
	DM_ASSERT((command.actionflags & ACTION_ATTACK) != 0);

	AI_DMState_GetMetrics(dm_state, &metrics);
	DM_ASSERT_FLOAT_CLOSE(metrics.last_attack_time, 1.4f, 0.0001f);
	DM_ASSERT_FLOAT_CLOSE(metrics.enemyvisible_time, 1.4f, 0.0001f);

	AI_DMState_Destroy(dm_state);
}

/*
=============
test_dm_damage_exposure_keeps_reaction_delay

Verifies that damage and firing widen acquisition without backdating sight time.
=============
*/
static void test_dm_damage_exposure_keeps_reaction_delay(void)
{
	const int client = 4;
	g_dm_characteristics[DM_CHARACTERISTIC_REACTION_TIME] = 0.5f;
	ai_dm_state_t *dm_state = AI_DMState_Create(client);
	DM_ASSERT(dm_state != NULL);

	bot_client_state_t client_state;
	dm_prepare_client_state(&client_state, client);
	dm_enable_attack_weapon(&client_state);

	ai_dm_enemy_info_t enemy;
	vec3_t enemy_origin = {128.0f, 0.0f, 0.0f};
	dm_prepare_enemy(&enemy, 6, enemy_origin, 128.0f, 1.0f);
	enemy.triggered_by_damage = true;
	enemy.is_shooting = true;

	DM_ASSERT(EA_ResetClient(client) == BLERR_NOERROR);
	bot_input_t base = {0};
	DM_ASSERT(EA_SubmitInput(client, &base) == BLERR_NOERROR);

	AI_DMState_Update(dm_state, &client_state, NULL, &enemy, NULL, 1.0f);
	bot_input_t command = dm_consume_input(client, 0.1f);
	DM_ASSERT((command.actionflags & ACTION_ATTACK) == 0);

	ai_dm_metrics_t metrics;
	AI_DMState_GetMetrics(dm_state, &metrics);
	DM_ASSERT_FLOAT_CLOSE(metrics.enemysight_time, 1.0f, 0.0001f);
	DM_ASSERT_FLOAT_CLOSE(metrics.reaction_delay, 0.5f, 0.0001f);

	AI_DMState_Update(dm_state, &client_state, NULL, &enemy, NULL, 1.49f);
	command = dm_consume_input(client, 0.1f);
	DM_ASSERT((command.actionflags & ACTION_ATTACK) == 0);

	AI_DMState_Update(dm_state, &client_state, NULL, &enemy, NULL, 1.5f);
	command = dm_consume_input(client, 0.1f);
	DM_ASSERT((command.actionflags & ACTION_ATTACK) != 0);

	AI_DMState_Destroy(dm_state);
}

/*
=============
test_dm_ready_attack_has_no_generic_cooldown

Pins retail BotCheckAttack's per-frame attack action once reaction is ready.
=============
*/
static void test_dm_ready_attack_has_no_generic_cooldown(void)
{
	const int client = 5;
	g_dm_characteristics[DM_CHARACTERISTIC_REACTION_TIME] = 0.0f;
	ai_dm_state_t *dm_state = AI_DMState_Create(client);
	DM_ASSERT(dm_state != NULL);

	bot_client_state_t client_state;
	dm_prepare_client_state(&client_state, client);
	dm_enable_attack_weapon(&client_state);

	ai_dm_enemy_info_t enemy;
	vec3_t enemy_origin = {128.0f, 0.0f, 0.0f};
	dm_prepare_enemy(&enemy, 7, enemy_origin, 128.0f, 3.0f);

	DM_ASSERT(EA_ResetClient(client) == BLERR_NOERROR);
	bot_input_t base = {0};
	DM_ASSERT(EA_SubmitInput(client, &base) == BLERR_NOERROR);

	AI_DMState_Update(dm_state, &client_state, NULL, &enemy, NULL, 3.0f);
	bot_input_t command = dm_consume_input(client, 0.05f);
	DM_ASSERT((command.actionflags & ACTION_ATTACK) != 0);

	AI_DMState_Update(dm_state, &client_state, NULL, &enemy, NULL, 3.05f);
	command = dm_consume_input(client, 0.05f);
	DM_ASSERT((command.actionflags & ACTION_ATTACK) != 0);

	ai_dm_metrics_t metrics;
	AI_DMState_GetMetrics(dm_state, &metrics);
	DM_ASSERT_FLOAT_CLOSE(metrics.last_attack_time, 3.05f, 0.0001f);

	AI_DMState_Destroy(dm_state);
}

/*
=============
test_dm_low_skill_attack_distance_bands

Pins the retail 0.2 skill floor and 100-to-180-unit low-skill hold band.
=============
*/
static void test_dm_low_skill_attack_distance_bands(void)
{
	const int client = 6;
	g_dm_characteristics[DM_CHARACTERISTIC_REACTION_TIME] = 0.0f;
	g_dm_characteristics[DM_CHARACTERISTIC_ATTACK_SKILL] = 0.19f;
	ai_dm_state_t *dm_state = AI_DMState_Create(client);
	DM_ASSERT(dm_state != NULL);

	bot_client_state_t client_state;
	dm_prepare_client_state(&client_state, client);

	DM_ASSERT(EA_ResetClient(client) == BLERR_NOERROR);
	bot_input_t base = {0};
	base.dir[0] = 0.25f;
	base.dir[1] = 0.5f;
	base.speed = 77.0f;
	DM_ASSERT(EA_SubmitInput(client, &base) == BLERR_NOERROR);

	ai_dm_enemy_info_t enemy;
	vec3_t enemy_origin = {256.0f, 0.0f, 0.0f};
	dm_prepare_enemy(&enemy, 8, enemy_origin, 256.0f, 4.0f);
	AI_DMState_Update(dm_state, &client_state, NULL, &enemy, NULL, 4.0f);
	bot_input_t command = dm_consume_input(client, 0.1f);
	DM_ASSERT_FLOAT_CLOSE(command.speed, 77.0f, 0.0001f);
	DM_ASSERT_FLOAT_CLOSE(command.dir[0], 0.25f, 0.0001f);

	g_dm_characteristics[DM_CHARACTERISTIC_ATTACK_SKILL] = 0.2f;
	enemy_origin[0] = 140.0f;
	dm_prepare_enemy(&enemy, 8, enemy_origin, 140.0f, 4.1f);
	AI_DMState_Update(dm_state, &client_state, NULL, &enemy, NULL, 4.1f);
	command = dm_consume_input(client, 0.1f);
	DM_ASSERT_FLOAT_CLOSE(command.speed, 77.0f, 0.0001f);
	DM_ASSERT_FLOAT_CLOSE(command.dir[0], 0.25f, 0.0001f);

	enemy_origin[0] = 181.0f;
	dm_prepare_enemy(&enemy, 8, enemy_origin, 181.0f, 4.2f);
	AI_DMState_Update(dm_state, &client_state, NULL, &enemy, NULL, 4.2f);
	command = dm_consume_input(client, 0.1f);
	DM_ASSERT_FLOAT_CLOSE(command.speed, 400.0f, 0.0001f);
	DM_ASSERT_FLOAT_CLOSE(command.dir[0], 1.0f, 0.0001f);

	enemy_origin[0] = 99.0f;
	dm_prepare_enemy(&enemy, 8, enemy_origin, 99.0f, 4.3f);
	AI_DMState_Update(dm_state, &client_state, NULL, &enemy, NULL, 4.3f);
	command = dm_consume_input(client, 0.1f);
	DM_ASSERT_FLOAT_CLOSE(command.speed, 400.0f, 0.0001f);
	DM_ASSERT_FLOAT_CLOSE(command.dir[0], -1.0f, 0.0001f);

	AI_DMState_Destroy(dm_state);
}

/*
=============
test_dm_attack_move_uses_body_origin_not_eye_position

Pins BotAttackMove's body-origin vector separately from the eye position that
the aim path uses. A tall view offset must not turn the 140-unit hold band into
a forward move.
=============
*/
static void test_dm_attack_move_uses_body_origin_not_eye_position(void)
{
	const int client = 6;
	g_dm_characteristics[DM_CHARACTERISTIC_REACTION_TIME] = 0.0f;
	g_dm_characteristics[DM_CHARACTERISTIC_ATTACK_SKILL] = 0.2f;
	ai_dm_state_t *dm_state = AI_DMState_Create(client);
	DM_ASSERT(dm_state != NULL);

	bot_client_state_t client_state;
	dm_prepare_client_state(&client_state, client);
	VectorSet(client_state.last_client_update.viewoffset, 0.0f, 0.0f, 200.0f);

	ai_dm_enemy_info_t enemy;
	vec3_t enemy_origin = {140.0f, 0.0f, 0.0f};
	dm_prepare_enemy(&enemy, 8, enemy_origin, 140.0f, 4.0f);
	AI_DMState_Update(dm_state, &client_state, NULL, &enemy, NULL, 4.0f);

	DM_ASSERT(g_dm_init_move_call_count == 1);
	DM_ASSERT(g_dm_move_call_count == 0);
	AI_DMState_Destroy(dm_state);
}

/*
=============
test_dm_attack_skill_four_tenths_enters_strafe

Pins BotAttackMove's strict 0.4 low-skill cutoff. At exactly 0.4 retail enters
the strafe path instead of holding position in the 100-to-180-unit band.
=============
*/
static void test_dm_attack_skill_four_tenths_enters_strafe(void)
{
	const int client = 5;
	g_dm_characteristics[DM_CHARACTERISTIC_REACTION_TIME] = 0.0f;
	g_dm_characteristics[DM_CHARACTERISTIC_ATTACK_SKILL] = 0.4f;
	ai_dm_state_t *dm_state = AI_DMState_Create(client);
	DM_ASSERT(dm_state != NULL);

	bot_client_state_t client_state;
	dm_prepare_client_state(&client_state, client);

	ai_dm_enemy_info_t enemy;
	vec3_t enemy_origin = {140.0f, 0.0f, 0.0f};
	dm_prepare_enemy(&enemy, 8, enemy_origin, 140.0f, 4.0f);
	AI_DMState_Update(dm_state, &client_state, NULL, &enemy, NULL, 4.0f);

	ai_dm_metrics_t metrics;
	AI_DMState_GetMetrics(dm_state, &metrics);
	DM_ASSERT_FLOAT_CLOSE(metrics.attackstrafe_timer, 0.1f, 0.0001f);
	DM_ASSERT(g_dm_move_call_count == 1);
	AI_DMState_Destroy(dm_state);
}

/*
=============
test_dm_attack_chase_builds_cached_goal_before_characteristic_work

Pins the dormant strict-time branch, cached goal fields, movement setup/order,
travel flags, and the exact expiry boundary without adding a live writer.
=============
*/
static void test_dm_attack_chase_builds_cached_goal_before_characteristic_work(void)
{
	const int client = 3;
	const float now = 40.0f;
	g_dm_characteristics[DM_CHARACTERISTIC_REACTION_TIME] = 0.0f;
	ai_dm_state_t *dm_state = AI_DMState_Create(client);
	DM_ASSERT(dm_state != NULL);

	ai_dm_metrics_t metrics;
	AI_DMState_GetMetrics(dm_state, &metrics);
	DM_ASSERT(metrics.attackchase_time < -1.0e30f);

	bot_client_state_t client_state;
	dm_prepare_client_state(&client_state, client);
	VectorSet(client_state.last_client_update.origin, 10.0f, 20.0f, 30.0f);
	VectorSet(client_state.last_client_update.velocity, 1.0f, 2.0f, 3.0f);
	VectorSet(client_state.last_client_update.viewoffset, 4.0f, 5.0f, 6.0f);
	VectorSet(client_state.last_client_update.viewangles, 70.0f, 80.0f, 90.0f);
	vec3_t private_view_delta = {7.0f, 8.0f, 9.0f};
	AI_DMState_ApplyDeltaAngles(dm_state, private_view_delta);
	client_state.last_client_update.pm_flags = PMF_ON_GROUND |
		PMF_TIME_TELEPORT |
		PMF_TIME_WATERJUMP |
		PMF_DUCKED;
	client_state.last_client_update.pm_time = 25;

	ai_goal_selection_t selection;
	memset(&selection, 0, sizeof(selection));
	selection.valid = true;
	selection.candidate.travel_flags = 0x13579;

	ai_dm_enemy_info_t enemy;
	vec3_t enemy_origin = {111.0f, 222.0f, 333.0f};
	dm_prepare_enemy(&enemy, 5, enemy_origin, 372.0f, now);

	bot_input_t last_move_command;
	memset(&last_move_command, 0, sizeof(last_move_command));
	last_move_command.thinktime = 0.125f;
	AI_DMState_SetAttackChaseTestState(dm_state, now + 6.0f, 37);
	AI_DMState_Update(dm_state,
		&client_state,
		&selection,
		&enemy,
		&last_move_command,
		now);

	DM_ASSERT(g_dm_init_move_call_count == 1);
	DM_ASSERT(g_dm_goal_move_call_count == 1);
	DM_ASSERT(g_dm_init_move_order == 1);
	DM_ASSERT(g_dm_goal_move_order == 2);
	DM_ASSERT(g_dm_move_call_count == 0);
	DM_ASSERT(g_dm_init_move_call.move_handle == client_state.move_handle);
	DM_ASSERT_FLOAT_CLOSE(g_dm_init_move_call.initmove.origin[0], 10.0f, 0.0001f);
	DM_ASSERT_FLOAT_CLOSE(g_dm_init_move_call.initmove.origin[1], 20.0f, 0.0001f);
	DM_ASSERT_FLOAT_CLOSE(g_dm_init_move_call.initmove.origin[2], 30.0f, 0.0001f);
	DM_ASSERT_FLOAT_CLOSE(g_dm_init_move_call.initmove.velocity[0], 1.0f, 0.0001f);
	DM_ASSERT_FLOAT_CLOSE(g_dm_init_move_call.initmove.velocity[1], 2.0f, 0.0001f);
	DM_ASSERT_FLOAT_CLOSE(g_dm_init_move_call.initmove.velocity[2], 3.0f, 0.0001f);
	DM_ASSERT_FLOAT_CLOSE(g_dm_init_move_call.initmove.viewoffset[0], 4.0f, 0.0001f);
	DM_ASSERT_FLOAT_CLOSE(g_dm_init_move_call.initmove.viewoffset[1], 5.0f, 0.0001f);
	DM_ASSERT_FLOAT_CLOSE(g_dm_init_move_call.initmove.viewoffset[2], 6.0f, 0.0001f);
	DM_ASSERT(g_dm_init_move_call.initmove.entitynum == client + 1);
	DM_ASSERT(g_dm_init_move_call.initmove.client == client);
	DM_ASSERT_FLOAT_CLOSE(g_dm_init_move_call.initmove.thinktime, 0.125f, 0.0001f);
	DM_ASSERT(g_dm_init_move_call.initmove.presencetype == PRESENCE_CROUCH);
	DM_ASSERT(g_dm_init_move_call.initmove.or_moveflags ==
		(MFL_ONGROUND | MFL_TELEPORTED | MFL_WATERJUMP));
	DM_ASSERT_FLOAT_CLOSE(g_dm_init_move_call.initmove.viewangles[0],
		dm_retail_angle_mod(7.0f),
		0.0001f);
	DM_ASSERT_FLOAT_CLOSE(g_dm_init_move_call.initmove.viewangles[1],
		dm_retail_angle_mod(8.0f),
		0.0001f);
	DM_ASSERT_FLOAT_CLOSE(g_dm_init_move_call.initmove.viewangles[2],
		dm_retail_angle_mod(9.0f),
		0.0001f);

	DM_ASSERT(g_dm_goal_move_call.move_handle == client_state.move_handle);
	DM_ASSERT(g_dm_goal_move_call.areanum == 37);
	DM_ASSERT(g_dm_goal_move_call.entitynum == enemy.entity);
	DM_ASSERT(g_dm_goal_move_call.travel_flags == 0x13579);
	for (int axis = 0; axis < 3; ++axis)
	{
		DM_ASSERT_FLOAT_CLOSE(g_dm_goal_move_call.origin[axis],
			enemy_origin[axis],
			0.0001f);
		DM_ASSERT_FLOAT_CLOSE(g_dm_goal_move_call.mins[axis], -8.0f, 0.0001f);
		DM_ASSERT_FLOAT_CLOSE(g_dm_goal_move_call.maxs[axis], 8.0f, 0.0001f);
	}
	DM_ASSERT(g_dm_characteristic_call_count[DM_CHARACTERISTIC_PIZZA_PREFERENCE] == 0);
	DM_ASSERT(g_dm_characteristic_call_count[DM_CHARACTERISTIC_ATTACK_SKILL] == 0);
	DM_ASSERT(g_dm_characteristic_call_count[DM_CHARACTERISTIC_JUMPER] == 0);
	DM_ASSERT(g_dm_characteristic_call_count[DM_CHARACTERISTIC_CROUCHER] == 0);

	dm_move_capture_reset();
	enemy.last_seen_time = now + 6.0f;
	AI_DMState_SetAttackChaseTestState(dm_state, now + 6.0f, 37);
	AI_DMState_Update(dm_state,
		&client_state,
		&selection,
		&enemy,
		&last_move_command,
		now + 6.0f);
	DM_ASSERT(g_dm_init_move_call_count == 1);
	DM_ASSERT(g_dm_goal_move_call_count == 0);
	DM_ASSERT(g_dm_characteristic_call_count[DM_CHARACTERISTIC_PIZZA_PREFERENCE] == 1);
	DM_ASSERT(g_dm_characteristic_call_count[DM_CHARACTERISTIC_ATTACK_SKILL] == 1);
	DM_ASSERT(g_dm_characteristic_call_count[DM_CHARACTERISTIC_JUMPER] == 1);
	DM_ASSERT(g_dm_characteristic_call_count[DM_CHARACTERISTIC_CROUCHER] == 1);

	AI_DMState_Destroy(dm_state);
}

/*
=============
test_dm_pizza_preference_skips_attack_move

Pins Gladiator's characteristic-48 preference roll ahead of attack movement.
=============
*/
static void test_dm_pizza_preference_skips_attack_move(void)
{
	const int client = 7;
	g_dm_characteristics[DM_CHARACTERISTIC_REACTION_TIME] = 0.0f;
	g_dm_characteristics[DM_CHARACTERISTIC_ATTACK_SKILL] = 1.0f;
	g_dm_characteristics[DM_CHARACTERISTIC_PIZZA_PREFERENCE] = 1.0f;
	ai_dm_state_t *dm_state = AI_DMState_Create(client);
	DM_ASSERT(dm_state != NULL);

	bot_client_state_t client_state;
	dm_prepare_client_state(&client_state, client);

	DM_ASSERT(EA_ResetClient(client) == BLERR_NOERROR);
	bot_input_t base = {0};
	base.speed = 77.0f;
	DM_ASSERT(EA_SubmitInput(client, &base) == BLERR_NOERROR);

	ai_dm_enemy_info_t enemy;
	vec3_t enemy_origin = {256.0f, 0.0f, 0.0f};
	dm_prepare_enemy(&enemy, 9, enemy_origin, 256.0f, 5.0f);
	AI_DMState_Update(dm_state, &client_state, NULL, &enemy, NULL, 5.0f);
	bot_input_t command = dm_consume_input(client, 0.1f);
	DM_ASSERT_FLOAT_CLOSE(command.speed, 77.0f, 0.0001f);

	ai_dm_metrics_t metrics;
	AI_DMState_GetMetrics(dm_state, &metrics);
	DM_ASSERT_FLOAT_CLOSE(metrics.attackstrafe_timer, 0.0f, 0.0001f);
	DM_ASSERT(g_dm_characteristic_call_count[DM_CHARACTERISTIC_PIZZA_PREFERENCE] == 1);
	DM_ASSERT(g_dm_characteristic_call_count[DM_CHARACTERISTIC_ATTACK_SKILL] == 0);

	g_dm_characteristics[DM_CHARACTERISTIC_PIZZA_PREFERENCE] = 0.0f;
	AI_DMState_Update(dm_state, &client_state, NULL, &enemy, NULL, 5.1f);
	command = dm_consume_input(client, 0.1f);
	DM_ASSERT_FLOAT_CLOSE(command.speed, 400.0f, 0.0001f);

	AI_DMState_Destroy(dm_state);
}

/*
=============
test_dm_strafe_clock_uses_retail_fixed_step

Verifies that skilled attack movement advances by 0.1, not wall-clock delta.
=============
*/
static void test_dm_strafe_clock_uses_retail_fixed_step(void)
{
	const int client = 0;
	g_dm_characteristics[DM_CHARACTERISTIC_REACTION_TIME] = 0.0f;
	g_dm_characteristics[DM_CHARACTERISTIC_ATTACK_SKILL] = 0.5f;
	ai_dm_state_t *dm_state = AI_DMState_Create(client);
	DM_ASSERT(dm_state != NULL);

	bot_client_state_t client_state;
	dm_prepare_client_state(&client_state, client);

	DM_ASSERT(EA_ResetClient(client) == BLERR_NOERROR);
	bot_input_t base = {0};
	DM_ASSERT(EA_SubmitInput(client, &base) == BLERR_NOERROR);

	ai_dm_enemy_info_t enemy;
	vec3_t enemy_origin = {200.0f, 0.0f, 0.0f};
	dm_prepare_enemy(&enemy, 10, enemy_origin, 200.0f, 6.0f);
	AI_DMState_Update(dm_state, &client_state, NULL, &enemy, NULL, 6.0f);
	dm_consume_input(client, 0.1f);

	ai_dm_metrics_t metrics;
	AI_DMState_GetMetrics(dm_state, &metrics);
	DM_ASSERT_FLOAT_CLOSE(metrics.attackstrafe_timer, 0.1f, 0.0001f);
	DM_ASSERT_FLOAT_CLOSE(metrics.attack_strafe_interval, 0.5f, 0.0001f);
	DM_ASSERT(metrics.attackchase_time < -1.0e30f);

	enemy.entity = 11;
	AI_DMState_Update(dm_state, &client_state, NULL, &enemy, NULL, 50.0f);
	dm_consume_input(client, 0.1f);
	AI_DMState_GetMetrics(dm_state, &metrics);
	DM_ASSERT_FLOAT_CLOSE(metrics.attackstrafe_timer, 0.2f, 0.0001f);
	DM_ASSERT_FLOAT_CLOSE(metrics.attack_strafe_interval, 0.5f, 0.0001f);

	AI_DMState_Destroy(dm_state);
}

/*
=============
test_dm_jump_requests_use_retail_latch

Pins Gladiator's alternating jump-request latch rather than a time cooldown.
=============
*/
static void test_dm_jump_requests_use_retail_latch(void)
{
	const int client = 4;
	g_dm_characteristics[DM_CHARACTERISTIC_REACTION_TIME] = 0.0f;
	g_dm_characteristics[DM_CHARACTERISTIC_ATTACK_SKILL] = 0.2f;
	g_dm_characteristics[DM_CHARACTERISTIC_JUMPER] = 1.0f;
	g_dm_characteristics[DM_CHARACTERISTIC_CROUCHER] = 0.0f;
	ai_dm_state_t *dm_state = AI_DMState_Create(client);
	DM_ASSERT(dm_state != NULL);

	bot_client_state_t client_state;
	dm_prepare_client_state(&client_state, client);

	DM_ASSERT(EA_ResetClient(client) == BLERR_NOERROR);
	bot_input_t base = {0};
	DM_ASSERT(EA_SubmitInput(client, &base) == BLERR_NOERROR);

	ai_dm_enemy_info_t enemy;
	vec3_t enemy_origin = {181.0f, 0.0f, 0.0f};
	dm_prepare_enemy(&enemy, 12, enemy_origin, 181.0f, 10.0f);

	AI_DMState_Update(dm_state, &client_state, NULL, &enemy, NULL, 10.0f);
	bot_input_t command = dm_consume_input(client, 0.1f);
	DM_ASSERT(g_dm_move_call_count == 1);
	DM_ASSERT(g_dm_move_calls[0].type == MOVE_JUMP);
	DM_ASSERT((command.actionflags & ACTION_JUMP) != 0);

	dm_move_capture_reset();
	AI_DMState_Update(dm_state, &client_state, NULL, &enemy, NULL, 10.1f);
	command = dm_consume_input(client, 0.1f);
	DM_ASSERT(g_dm_move_call_count == 1);
	DM_ASSERT(g_dm_move_calls[0].type == MOVE_WALK);
	DM_ASSERT((command.actionflags & ACTION_JUMP) == 0);

	dm_move_capture_reset();
	AI_DMState_Update(dm_state, &client_state, NULL, &enemy, NULL, 10.2f);
	command = dm_consume_input(client, 0.1f);
	DM_ASSERT(g_dm_move_call_count == 1);
	DM_ASSERT(g_dm_move_calls[0].type == MOVE_JUMP);
	DM_ASSERT((command.actionflags & ACTION_JUMP) != 0);

	AI_DMState_Destroy(dm_state);
}

/*
=============
test_dm_crouch_timer_uses_characteristic_window

Pins the now-plus-five-times-croucher window and its strict expiry boundary.
=============
*/
static void test_dm_crouch_timer_uses_characteristic_window(void)
{
	const int client = 5;
	g_dm_characteristics[DM_CHARACTERISTIC_REACTION_TIME] = 0.0f;
	g_dm_characteristics[DM_CHARACTERISTIC_ATTACK_SKILL] = 0.2f;
	g_dm_characteristics[DM_CHARACTERISTIC_JUMPER] = 0.0f;
	g_dm_characteristics[DM_CHARACTERISTIC_CROUCHER] = 1.0f;
	ai_dm_state_t *dm_state = AI_DMState_Create(client);
	DM_ASSERT(dm_state != NULL);

	bot_client_state_t client_state;
	dm_prepare_client_state(&client_state, client);

	DM_ASSERT(EA_ResetClient(client) == BLERR_NOERROR);
	bot_input_t base = {0};
	DM_ASSERT(EA_SubmitInput(client, &base) == BLERR_NOERROR);

	ai_dm_enemy_info_t enemy;
	vec3_t enemy_origin = {181.0f, 0.0f, 0.0f};
	dm_prepare_enemy(&enemy, 13, enemy_origin, 181.0f, 20.0f);

	AI_DMState_Update(dm_state, &client_state, NULL, &enemy, NULL, 20.0f);
	bot_input_t command = dm_consume_input(client, 0.1f);
	DM_ASSERT(g_dm_move_call_count == 1);
	DM_ASSERT(g_dm_move_calls[0].type == MOVE_CROUCH);
	DM_ASSERT((command.actionflags & ACTION_CROUCH) != 0);

	ai_dm_metrics_t metrics;
	AI_DMState_GetMetrics(dm_state, &metrics);
	DM_ASSERT_FLOAT_CLOSE(metrics.attackcrouch_time, 25.0f, 0.0001f);

	dm_move_capture_reset();
	AI_DMState_Update(dm_state, &client_state, NULL, &enemy, NULL, 24.9f);
	command = dm_consume_input(client, 0.1f);
	DM_ASSERT(g_dm_move_calls[0].type == MOVE_CROUCH);
	DM_ASSERT((command.actionflags & ACTION_CROUCH) != 0);

	dm_move_capture_reset();
	AI_DMState_Update(dm_state, &client_state, NULL, &enemy, NULL, 25.0f);
	command = dm_consume_input(client, 0.1f);
	DM_ASSERT(g_dm_move_calls[0].type == MOVE_WALK);
	DM_ASSERT((command.actionflags & ACTION_CROUCH) == 0);

	AI_DMState_Destroy(dm_state);
}

/*
=============
test_dm_failed_strafe_retries_other_side

Verifies the two direct-move attempts, side flip, and strafe-clock reset.
=============
*/
static void test_dm_failed_strafe_retries_other_side(void)
{
	const int client = 6;
	g_dm_characteristics[DM_CHARACTERISTIC_REACTION_TIME] = 0.0f;
	g_dm_characteristics[DM_CHARACTERISTIC_ATTACK_SKILL] = 0.5f;
	g_dm_characteristics[DM_CHARACTERISTIC_JUMPER] = 0.0f;
	g_dm_characteristics[DM_CHARACTERISTIC_CROUCHER] = 0.0f;
	ai_dm_state_t *dm_state = AI_DMState_Create(client);
	DM_ASSERT(dm_state != NULL);

	bot_client_state_t client_state;
	dm_prepare_client_state(&client_state, client);

	DM_ASSERT(EA_ResetClient(client) == BLERR_NOERROR);
	bot_input_t base = {0};
	base.speed = 77.0f;
	DM_ASSERT(EA_SubmitInput(client, &base) == BLERR_NOERROR);

	g_dm_move_result_count = 2;
	g_dm_move_results[0] = 0;
	g_dm_move_results[1] = 1;

	ai_dm_enemy_info_t enemy;
	vec3_t enemy_origin = {140.0f, 0.0f, 0.0f};
	dm_prepare_enemy(&enemy, 14, enemy_origin, 140.0f, 30.0f);
	AI_DMState_Update(dm_state, &client_state, NULL, &enemy, NULL, 30.0f);
	bot_input_t command = dm_consume_input(client, 0.1f);

	DM_ASSERT(g_dm_move_call_count == 2);
	DM_ASSERT(g_dm_move_calls[0].move_handle == client_state.move_handle);
	DM_ASSERT(g_dm_move_calls[1].move_handle == client_state.move_handle);
	DM_ASSERT(g_dm_move_calls[0].type == MOVE_WALK);
	DM_ASSERT(g_dm_move_calls[1].type == MOVE_WALK);
	DM_ASSERT(g_dm_move_calls[0].direction[1] * g_dm_move_calls[1].direction[1] < 0.0f);
	DM_ASSERT_FLOAT_CLOSE(command.speed, 400.0f, 0.0001f);
	DM_ASSERT_FLOAT_CLOSE(command.dir[0], g_dm_move_calls[1].direction[0], 0.0001f);
	DM_ASSERT_FLOAT_CLOSE(command.dir[1], g_dm_move_calls[1].direction[1], 0.0001f);

	ai_dm_metrics_t metrics;
	AI_DMState_GetMetrics(dm_state, &metrics);
	DM_ASSERT_FLOAT_CLOSE(metrics.attackstrafe_timer, 0.0f, 0.0001f);

	dm_move_capture_reset();
	g_dm_move_result_count = 2;
	g_dm_move_results[0] = 0;
	g_dm_move_results[1] = 0;
	AI_DMState_Update(dm_state, &client_state, NULL, &enemy, NULL, 30.1f);
	command = dm_consume_input(client, 0.1f);
	DM_ASSERT(g_dm_move_call_count == 2);
	DM_ASSERT_FLOAT_CLOSE(command.speed, 77.0f, 0.0001f);
	AI_DMState_GetMetrics(dm_state, &metrics);
	DM_ASSERT_FLOAT_CLOSE(metrics.attackstrafe_timer, 0.0f, 0.0001f);

	AI_DMState_Destroy(dm_state);
}

/*
=============
test_dm_chase_timer_decays
=============
*/
static void test_dm_chase_timer_decays(void)
{
	const int client = 1;
	ai_dm_state_t *dm_state = AI_DMState_Create(client);
	DM_ASSERT(dm_state != NULL);

	bot_client_state_t client_state;
	dm_prepare_client_state(&client_state, client);

	ai_dm_enemy_info_t enemy;
	vec3_t origin = {64.0f, 64.0f, 0.0f};
	dm_prepare_enemy(&enemy, 3, origin, 90.5f, 2.0f);

	DM_ASSERT(EA_ResetClient(client) == BLERR_NOERROR);
	bot_input_t base = {0};
	DM_ASSERT(EA_SubmitInput(client, &base) == BLERR_NOERROR);

	AI_DMState_Update(dm_state, &client_state, NULL, &enemy, NULL, 2.0f);
	dm_consume_input(client, 0.05f);

	ai_dm_metrics_t metrics;
	AI_DMState_GetMetrics(dm_state, &metrics);
	DM_ASSERT(metrics.enemy_visible);
	DM_ASSERT(metrics.chase_time > 2.0f);

	AI_DMState_Update(dm_state, &client_state, NULL, NULL, NULL, 2.1f);
	dm_consume_input(client, 0.05f);
	AI_DMState_GetMetrics(dm_state, &metrics);
	DM_ASSERT(!metrics.enemy_visible);
	DM_ASSERT(metrics.enemy_entity == 3);

	float expiry_time = metrics.chase_time + 0.2f;
	AI_DMState_Update(dm_state, &client_state, NULL, NULL, NULL, expiry_time);
	dm_consume_input(client, 0.05f);
	AI_DMState_GetMetrics(dm_state, &metrics);
	DM_ASSERT(metrics.enemy_entity == 0);

	float reacquire_time = expiry_time + 0.3f;
	dm_prepare_enemy(&enemy, 3, origin, 90.5f, reacquire_time);
	AI_DMState_Update(dm_state, &client_state, NULL, &enemy, NULL, reacquire_time);
	dm_consume_input(client, 0.05f);
	AI_DMState_GetMetrics(dm_state, &metrics);
	DM_ASSERT(metrics.enemy_visible);
	DM_ASSERT_FLOAT_CLOSE(metrics.enemysight_time, reacquire_time, 0.0001f);

	AI_DMState_Destroy(dm_state);
}

/*
=============
test_dm_revenge_counters_update_on_death
=============
*/
static void test_dm_revenge_counters_update_on_death(void)
{
	const int client = 2;
	ai_dm_state_t *dm_state = AI_DMState_Create(client);
	DM_ASSERT(dm_state != NULL);

	bot_client_state_t client_state;
	dm_prepare_client_state(&client_state, client);

	ai_dm_enemy_info_t enemy;
	vec3_t origin = {32.0f, -32.0f, 0.0f};
	dm_prepare_enemy(&enemy, 4, origin, 45.3f, 5.0f);

	DM_ASSERT(EA_ResetClient(client) == BLERR_NOERROR);
	bot_input_t base = {0};
	DM_ASSERT(EA_SubmitInput(client, &base) == BLERR_NOERROR);

	AI_DMState_Update(dm_state, &client_state, NULL, &enemy, NULL, 5.0f);
	dm_consume_input(client, 0.05f);

	client_state.last_client_update.pm_type = PM_DEAD;
	AI_DMState_Update(dm_state, &client_state, NULL, NULL, NULL, 5.5f);
	dm_consume_input(client, 0.05f);

	ai_dm_metrics_t metrics;
	AI_DMState_GetMetrics(dm_state, &metrics);
	DM_ASSERT(metrics.revenge_enemy == 4);
	DM_ASSERT(metrics.revenge_kills == 1);
	DM_ASSERT(metrics.enemy_entity == 0);
	DM_ASSERT(!metrics.enemy_visible);

	AI_DMState_Destroy(dm_state);
}

/*
=============
test_dm_velocity_tracks_enemy_motion
=============
*/
static void test_dm_velocity_tracks_enemy_motion(void)
{
	const int client = 3;
	ai_dm_state_t *dm_state = AI_DMState_Create(client);
	DM_ASSERT(dm_state != NULL);

	bot_client_state_t client_state;
	dm_prepare_client_state(&client_state, client);

	ai_dm_enemy_info_t enemy;
	vec3_t origin = {0.0f, 0.0f, 0.0f};
	dm_prepare_enemy(&enemy, 5, origin, 0.0f, 10.0f);

	DM_ASSERT(EA_ResetClient(client) == BLERR_NOERROR);
	bot_input_t base = {0};
	DM_ASSERT(EA_SubmitInput(client, &base) == BLERR_NOERROR);

	AI_DMState_Update(dm_state, &client_state, NULL, &enemy, NULL, 10.0f);
	dm_consume_input(client, 0.05f);

	vec3_t moved_origin = {5.0f, 0.0f, 0.0f};
	dm_prepare_enemy(&enemy, 5, moved_origin, 5.0f, 10.5f);
	AI_DMState_Update(dm_state, &client_state, NULL, &enemy, NULL, 10.5f);
	dm_consume_input(client, 0.05f);

	ai_dm_metrics_t metrics;
	AI_DMState_GetMetrics(dm_state, &metrics);
	DM_ASSERT_FLOAT_CLOSE(metrics.last_enemy_origin[0], 5.0f, 0.0001f);
	DM_ASSERT_FLOAT_CLOSE(metrics.last_enemy_velocity[0], 10.0f, 0.0001f);
	DM_ASSERT_FLOAT_CLOSE(metrics.enemyposition_time, 10.5f, 0.0001f);

	AI_DMState_Destroy(dm_state);
}

/*
=============
test_dm_view_turn_uses_retail_acceleration_and_deceleration

Pins sub_10029150's additive turn speed and asymmetric slowdown clamp.
=============
*/
static void test_dm_view_turn_uses_retail_acceleration_and_deceleration(void)
{
	const int client = 0;
	g_dm_characteristics[DM_CHARACTERISTIC_AIM_ACCURACY] = 0.0f;
	g_dm_characteristics[DM_CHARACTERISTIC_VIEW_FACTOR] = 10.0f;
	g_dm_characteristics[DM_CHARACTERISTIC_VIEW_MAXCHANGE] = 20.0f;
	ai_dm_state_t *dm_state = AI_DMState_Create(client);
	DM_ASSERT(dm_state != NULL);

	bot_client_state_t client_state;
	dm_prepare_client_state(&client_state, client);
	ai_dm_enemy_info_t enemy;
	vec3_t enemy_origin = {0.0f, 100.0f, 0.0f};
	dm_prepare_enemy(&enemy, 2, enemy_origin, 100.0f, 1.0f);
	bot_input_t move_command = {0};
	move_command.thinktime = 0.1f;

	DM_ASSERT(EA_ResetClient(client) == BLERR_NOERROR);
	bot_input_t base = {0};
	DM_ASSERT(EA_SubmitInput(client, &base) == BLERR_NOERROR);
	AI_DMState_Update(dm_state,
		&client_state,
		NULL,
		&enemy,
		&move_command,
		1.0f);
	bot_input_t command = dm_consume_input(client, 0.1f);
	ai_dm_metrics_t metrics;
	AI_DMState_GetMetrics(dm_state, &metrics);
	DM_ASSERT_FLOAT_CLOSE(metrics.ideal_viewangles[YAW], 90.0f, 0.01f);
	DM_ASSERT_FLOAT_CLOSE(metrics.viewanglespeed[YAW], 1.0f, 0.0001f);
	DM_ASSERT_FLOAT_CLOSE(metrics.viewangles[YAW], 0.99976f, 0.01f);
	DM_ASSERT_FLOAT_CLOSE(command.viewangles[YAW], metrics.viewangles[YAW], 0.0001f);

	AI_DMState_Update(dm_state,
		&client_state,
		NULL,
		&enemy,
		&move_command,
		1.1f);
	dm_consume_input(client, 0.1f);
	AI_DMState_GetMetrics(dm_state, &metrics);
	DM_ASSERT_FLOAT_CLOSE(metrics.viewanglespeed[YAW], 2.0f, 0.0001f);
	DM_ASSERT_FLOAT_CLOSE(metrics.viewangles[YAW], 2.9993f, 0.02f);
	DM_ASSERT(g_dm_characteristic_call_count[DM_CHARACTERISTIC_VIEW_FACTOR] == 2);
	DM_ASSERT(g_dm_characteristic_call_count[DM_CHARACTERISTIC_VIEW_MAXCHANGE] == 2);
	AI_DMState_Destroy(dm_state);

	dm_test_setup();
	g_dm_characteristics[DM_CHARACTERISTIC_AIM_ACCURACY] = 0.0f;
	g_dm_characteristics[DM_CHARACTERISTIC_VIEW_FACTOR] = 100.0f;
	g_dm_characteristics[DM_CHARACTERISTIC_VIEW_MAXCHANGE] = 10.0f;
	dm_state = AI_DMState_Create(client);
	DM_ASSERT(dm_state != NULL);
	dm_prepare_client_state(&client_state, client);
	dm_prepare_enemy(&enemy, 2, enemy_origin, 100.0f, 2.0f);
	DM_ASSERT(EA_ResetClient(client) == BLERR_NOERROR);
	DM_ASSERT(EA_SubmitInput(client, &base) == BLERR_NOERROR);
	AI_DMState_Update(dm_state,
		&client_state,
		NULL,
		&enemy,
		&move_command,
		2.0f);
	dm_consume_input(client, 0.1f);

	const float fifteen_degrees = 0.26794919f;
	enemy_origin[0] = 100.0f;
	enemy_origin[1] = 100.0f * fifteen_degrees;
	dm_prepare_enemy(&enemy, 2, enemy_origin, 103.53f, 2.1f);
	AI_DMState_Update(dm_state,
		&client_state,
		NULL,
		&enemy,
		&move_command,
		2.1f);
	dm_consume_input(client, 0.1f);
	AI_DMState_GetMetrics(dm_state, &metrics);
	DM_ASSERT_FLOAT_CLOSE(metrics.ideal_viewangles[YAW], 15.0f, 0.02f);
	DM_ASSERT_FLOAT_CLOSE(metrics.viewanglespeed[YAW], 9.0f, 0.02f);
	DM_ASSERT_FLOAT_CLOSE(metrics.viewangles[YAW], 15.0f, 0.02f);
	AI_DMState_Destroy(dm_state);
}

/*
=============
test_dm_view_turn_uses_retail_no_enemy_defaults

Pins sub_10029150's exact zero enemy sentinel: non-combat turns use its
fixed 100/150 acceleration parameters without querying bot characteristics.
=============
*/
static void test_dm_view_turn_uses_retail_no_enemy_defaults(void)
{
	const int client = 0;
	g_dm_characteristics[DM_CHARACTERISTIC_VIEW_FACTOR] = 1.0f;
	g_dm_characteristics[DM_CHARACTERISTIC_VIEW_MAXCHANGE] = 1.0f;
	ai_dm_state_t *dm_state = AI_DMState_Create(client);
	DM_ASSERT(dm_state != NULL);

	bot_client_state_t client_state;
	dm_prepare_client_state(&client_state, client);
	bot_input_t base = {0};
	vec3_t ideal_viewangles = {0.0f, 90.0f, 0.0f};
	DM_ASSERT(EA_ResetClient(client) == BLERR_NOERROR);
	DM_ASSERT(EA_SubmitInput(client, &base) == BLERR_NOERROR);
	AI_DMState_SetEnemyContext(dm_state, 0, 0.0f, 0, NULL);
	AI_DMState_SetIdealViewAngles(dm_state, ideal_viewangles);
	AI_DMState_ChangeViewAngles(dm_state, &client_state, 0.1f);

	ai_dm_metrics_t metrics;
	AI_DMState_GetMetrics(dm_state, &metrics);
	DM_ASSERT_FLOAT_CLOSE(metrics.viewanglespeed[YAW], 10.0f, 0.0001f);
	DM_ASSERT(g_dm_characteristic_call_count[DM_CHARACTERISTIC_VIEW_FACTOR] == 0);
	DM_ASSERT(g_dm_characteristic_call_count[DM_CHARACTERISTIC_VIEW_MAXCHANGE] == 0);
	bot_input_t command = dm_consume_input(client, 0.1f);
	DM_ASSERT_FLOAT_CLOSE(command.viewangles[YAW], metrics.viewangles[YAW], 0.0001f);
	AI_DMState_Destroy(dm_state);
}

/*
=============
test_dm_view_turn_preserves_retail_accuracy_snap_threshold

Pins sub_10024376's strict greater-than-0.8 direct-view branch.
=============
*/
static void test_dm_view_turn_preserves_retail_accuracy_snap_threshold(void)
{
	const int client = 0;
	g_dm_characteristics[DM_CHARACTERISTIC_AIM_ACCURACY] = 0.8f;
	g_dm_characteristics[DM_CHARACTERISTIC_VIEW_FACTOR] = 10.0f;
	g_dm_characteristics[DM_CHARACTERISTIC_VIEW_MAXCHANGE] = 20.0f;
	ai_dm_state_t *dm_state = AI_DMState_Create(client);
	DM_ASSERT(dm_state != NULL);

	bot_client_state_t client_state;
	dm_prepare_client_state(&client_state, client);
	ai_dm_enemy_info_t enemy;
	vec3_t enemy_origin = {0.0f, 100.0f, 0.0f};
	dm_prepare_enemy(&enemy, 2, enemy_origin, 100.0f, 3.0f);
	bot_input_t move_command = {0};
	move_command.thinktime = 0.1f;
	bot_input_t base = {0};
	DM_ASSERT(EA_ResetClient(client) == BLERR_NOERROR);
	DM_ASSERT(EA_SubmitInput(client, &base) == BLERR_NOERROR);
	AI_DMState_Update(dm_state,
		&client_state,
		NULL,
		&enemy,
		&move_command,
		3.0f);
	dm_consume_input(client, 0.1f);
	ai_dm_metrics_t metrics;
	AI_DMState_GetMetrics(dm_state, &metrics);
	DM_ASSERT(metrics.viewangles[YAW] < 2.0f);
	AI_DMState_Destroy(dm_state);

	dm_test_setup();
	g_dm_characteristics[DM_CHARACTERISTIC_AIM_ACCURACY] = 0.8001f;
	g_dm_characteristics[DM_CHARACTERISTIC_VIEW_FACTOR] = 10.0f;
	g_dm_characteristics[DM_CHARACTERISTIC_VIEW_MAXCHANGE] = 20.0f;
	dm_state = AI_DMState_Create(client);
	DM_ASSERT(dm_state != NULL);
	dm_prepare_client_state(&client_state, client);
	dm_prepare_enemy(&enemy, 2, enemy_origin, 100.0f, 4.0f);
	DM_ASSERT(EA_ResetClient(client) == BLERR_NOERROR);
	DM_ASSERT(EA_SubmitInput(client, &base) == BLERR_NOERROR);
	AI_DMState_Update(dm_state,
		&client_state,
		NULL,
		&enemy,
		&move_command,
		4.0f);
	bot_input_t command = dm_consume_input(client, 0.1f);
	AI_DMState_GetMetrics(dm_state, &metrics);
	DM_ASSERT_FLOAT_CLOSE(metrics.viewangles[YAW], 90.0f, 0.01f);
	DM_ASSERT_FLOAT_CLOSE(command.viewangles[YAW], 90.0f, 0.01f);
	AI_DMState_Destroy(dm_state);
}

/*
=============
test_dm_view_turn_truncates_fractional_angle_difference

Pins the retail abs(int(AngleDifference)) conversion before speed updates.
=============
*/
static void test_dm_view_turn_truncates_fractional_angle_difference(void)
{
	const int client = 0;
	g_dm_characteristics[DM_CHARACTERISTIC_AIM_ACCURACY] = 0.0f;
	g_dm_characteristics[DM_CHARACTERISTIC_VIEW_FACTOR] = 1800.0f;
	g_dm_characteristics[DM_CHARACTERISTIC_VIEW_MAXCHANGE] = 1800.0f;
	ai_dm_state_t *dm_state = AI_DMState_Create(client);
	DM_ASSERT(dm_state != NULL);

	bot_client_state_t client_state;
	dm_prepare_client_state(&client_state, client);
	ai_dm_enemy_info_t enemy;
	vec3_t enemy_origin = {100.0f, 1.570925f, 0.0f};
	dm_prepare_enemy(&enemy, 2, enemy_origin, 100.01f, 5.0f);
	bot_input_t move_command = {0};
	move_command.thinktime = 0.1f;
	bot_input_t base = {0};
	DM_ASSERT(EA_ResetClient(client) == BLERR_NOERROR);
	DM_ASSERT(EA_SubmitInput(client, &base) == BLERR_NOERROR);
	AI_DMState_Update(dm_state,
		&client_state,
		NULL,
		&enemy,
		&move_command,
		5.0f);
	bot_input_t command = dm_consume_input(client, 0.1f);
	ai_dm_metrics_t metrics;
	AI_DMState_GetMetrics(dm_state, &metrics);
	DM_ASSERT(metrics.ideal_viewangles[YAW] > 0.8f);
	DM_ASSERT(metrics.ideal_viewangles[YAW] < 1.0f);
	DM_ASSERT_FLOAT_CLOSE(metrics.viewanglespeed[YAW], 0.0f, 0.0001f);
	DM_ASSERT_FLOAT_CLOSE(metrics.viewangles[YAW], 0.0f, 0.0001f);
	DM_ASSERT_FLOAT_CLOSE(command.viewangles[YAW], 0.0f, 0.0001f);
	AI_DMState_Destroy(dm_state);
}

/*
=============
test_dm_aim_uses_retail_muzzle_trace_and_linear_lead

Pins weapon offset tracing and the strict aim-skill projectile prediction path.
=============
*/
static void test_dm_aim_uses_retail_muzzle_trace_and_linear_lead(void)
{
	const int client = 0;
	g_dm_characteristics[DM_CHARACTERISTIC_PIZZA_PREFERENCE] = 1.0f;
	g_dm_characteristics[DM_CHARACTERISTIC_AIM_SKILL] = 0.5f;
	g_dm_characteristics[DM_CHARACTERISTIC_AIM_ACCURACY] = 1.0f;
	g_dm_weapon_available = true;
	strcpy(g_dm_weapon_info.name, "Blaster");
	g_dm_weapon_info.speed = 100.0f;
	g_dm_weapon_info.offset[2] = 12.0f;
	g_dm_trace_result.fraction = 1.0f;
	g_dm_trace_result.ent = 2;

	ai_dm_state_t *dm_state = AI_DMState_Create(client);
	DM_ASSERT(dm_state != NULL);
	bot_client_state_t client_state;
	dm_prepare_client_state(&client_state, client);
	client_state.weapon_state = 9;
	client_state.last_client_update.viewoffset[2] = 20.0f;
	ai_dm_enemy_info_t enemy;
	vec3_t enemy_origin = {100.0f, 0.0f, 0.0f};
	dm_prepare_enemy(&enemy, 2, enemy_origin, 100.0f, 1.0f);
	enemy.velocity[0] = 10.0f;
	enemy.velocity[2] = 50.0f;
	bot_input_t move_command = {0};
	move_command.thinktime = 0.1f;
	bot_input_t base = {0};
	DM_ASSERT(EA_ResetClient(client) == BLERR_NOERROR);
	DM_ASSERT(EA_SubmitInput(client, &base) == BLERR_NOERROR);
	AI_DMState_Update(dm_state,
		&client_state,
		NULL,
		&enemy,
		&move_command,
		1.0f);
	dm_consume_input(client, 0.1f);

	ai_dm_metrics_t metrics;
	AI_DMState_GetMetrics(dm_state, &metrics);
	DM_ASSERT(g_dm_weapon_handle == 9);
	DM_ASSERT(g_dm_trace_count == 1);
	DM_ASSERT_FLOAT_CLOSE(g_dm_trace_start[2], 32.0f, 0.0001f);
	DM_ASSERT_FLOAT_CLOSE(g_dm_trace_end[0], 100.0f, 0.0001f);
	DM_ASSERT_FLOAT_CLOSE(g_dm_trace_end[2], 8.0f, 0.0001f);
	DM_ASSERT_FLOAT_CLOSE(g_dm_trace_mins[0], -4.0f, 0.0001f);
	DM_ASSERT_FLOAT_CLOSE(g_dm_trace_maxs[2], 4.0f, 0.0001f);
	DM_ASSERT(g_dm_trace_passent == client + 1);
	DM_ASSERT(g_dm_trace_mask == MASK_SHOT);
	DM_ASSERT_FLOAT_CLOSE(metrics.aim_target[0], 110.0f, 0.0001f);
	DM_ASSERT_FLOAT_CLOSE(metrics.aim_target[1], 0.0f, 0.0001f);
	DM_ASSERT_FLOAT_CLOSE(metrics.aim_target[2], 0.0f, 0.0001f);
	AI_DMState_Destroy(dm_state);
}

/*
=============
test_dm_aim_preserves_trace_raise_and_skill_threshold

Pins the always-true fraction<=1 obstruction quirk and strict skill > 0.4 gate.
=============
*/
static void test_dm_aim_preserves_trace_raise_and_skill_threshold(void)
{
	const int client = 0;
	g_dm_characteristics[DM_CHARACTERISTIC_PIZZA_PREFERENCE] = 1.0f;
	g_dm_characteristics[DM_CHARACTERISTIC_AIM_SKILL] = 0.4f;
	g_dm_characteristics[DM_CHARACTERISTIC_AIM_ACCURACY] = 1.0f;
	g_dm_weapon_available = true;
	strcpy(g_dm_weapon_info.name, "HyperBlaster");
	g_dm_weapon_info.speed = 100.0f;
	g_dm_trace_result.fraction = 1.0f;
	g_dm_trace_result.ent = 0;

	ai_dm_state_t *dm_state = AI_DMState_Create(client);
	DM_ASSERT(dm_state != NULL);
	bot_client_state_t client_state;
	dm_prepare_client_state(&client_state, client);
	client_state.weapon_state = 3;
	ai_dm_enemy_info_t enemy;
	vec3_t enemy_origin = {100.0f, 0.0f, 0.0f};
	dm_prepare_enemy(&enemy, 2, enemy_origin, 100.0f, 2.0f);
	enemy.velocity[0] = 50.0f;
	bot_input_t move_command = {0};
	move_command.thinktime = 0.1f;
	bot_input_t base = {0};
	DM_ASSERT(EA_ResetClient(client) == BLERR_NOERROR);
	DM_ASSERT(EA_SubmitInput(client, &base) == BLERR_NOERROR);
	AI_DMState_Update(dm_state,
		&client_state,
		NULL,
		&enemy,
		&move_command,
		2.0f);
	dm_consume_input(client, 0.1f);
	ai_dm_metrics_t metrics;
	AI_DMState_GetMetrics(dm_state, &metrics);
	DM_ASSERT_FLOAT_CLOSE(metrics.aim_target[0], 100.0f, 0.0001f);
	DM_ASSERT_FLOAT_CLOSE(metrics.aim_target[2], 24.0f, 0.0001f);
	AI_DMState_Destroy(dm_state);
}

/*
=============
test_dm_aim_applies_rocket_accuracy_square_root

Pins the Rocket Launcher name gate and its sqrt accuracy before target noise.
=============
*/
static void test_dm_aim_applies_rocket_accuracy_square_root(void)
{
	const int client = 0;
	g_dm_characteristics[DM_CHARACTERISTIC_PIZZA_PREFERENCE] = 1.0f;
	g_dm_characteristics[DM_CHARACTERISTIC_AIM_SKILL] = 0.0f;
	g_dm_characteristics[DM_CHARACTERISTIC_AIM_ACCURACY] = 0.25f;
	g_dm_weapon_available = true;
	strcpy(g_dm_weapon_info.name, "rOcKeT lAuNcHeR");
	g_dm_trace_result.fraction = 1.0f;
	g_dm_trace_result.ent = 2;

	const unsigned int seed = 23U;
	srand(seed);
	(void)dm_retail_random();
	float expected_x = 100.0f + 20.0f * dm_retail_crandom() * 0.5f;
	float expected_y = 20.0f * dm_retail_crandom() * 0.5f;
	float expected_z = 8.0f + 10.0f * dm_retail_crandom() * 0.5f;
	srand(seed);

	ai_dm_state_t *dm_state = AI_DMState_Create(client);
	DM_ASSERT(dm_state != NULL);
	bot_client_state_t client_state;
	dm_prepare_client_state(&client_state, client);
	client_state.weapon_state = 4;
	ai_dm_enemy_info_t enemy;
	vec3_t enemy_origin = {100.0f, 0.0f, 0.0f};
	dm_prepare_enemy(&enemy, 2, enemy_origin, 100.0f, 3.0f);
	bot_input_t move_command = {0};
	move_command.thinktime = 0.1f;
	bot_input_t base = {0};
	DM_ASSERT(EA_ResetClient(client) == BLERR_NOERROR);
	DM_ASSERT(EA_SubmitInput(client, &base) == BLERR_NOERROR);
	AI_DMState_Update(dm_state,
		&client_state,
		NULL,
		&enemy,
		&move_command,
		3.0f);
	dm_consume_input(client, 0.1f);
	ai_dm_metrics_t metrics;
	AI_DMState_GetMetrics(dm_state, &metrics);
	DM_ASSERT_FLOAT_CLOSE(metrics.aim_target[0], expected_x, 0.0001f);
	DM_ASSERT_FLOAT_CLOSE(metrics.aim_target[1], expected_y, 0.0001f);
	DM_ASSERT_FLOAT_CLOSE(metrics.aim_target[2], expected_z, 0.0001f);
	AI_DMState_Destroy(dm_state);
}

/*
=============
test_dm_aim_applies_railgun_direction_noise_and_spread

Pins the Railgun-only normalized perturbation and pitch/yaw spread call order.
=============
*/
static void test_dm_aim_applies_railgun_direction_noise_and_spread(void)
{
	const int client = 0;
	const float accuracy = 0.5f;
	const float inaccuracy = 1.0f - accuracy;
	g_dm_characteristics[DM_CHARACTERISTIC_PIZZA_PREFERENCE] = 1.0f;
	g_dm_characteristics[DM_CHARACTERISTIC_AIM_SKILL] = 0.0f;
	g_dm_characteristics[DM_CHARACTERISTIC_AIM_ACCURACY] = accuracy;
	g_dm_weapon_available = true;
	strcpy(g_dm_weapon_info.name, "Railgun");
	g_dm_weapon_info.hspread = 0.2f;
	g_dm_weapon_info.vspread = 0.3f;
	g_dm_trace_result.fraction = 1.0f;
	g_dm_trace_result.ent = 2;

	const unsigned int seed = 31U;
	srand(seed);
	(void)dm_retail_random();
	vec3_t expected_target = {
		100.0f + 20.0f * dm_retail_crandom() * inaccuracy,
		20.0f * dm_retail_crandom() * inaccuracy,
		8.0f + 10.0f * dm_retail_crandom() * inaccuracy
	};
	vec3_t expected_direction;
	VectorCopy(expected_target, expected_direction);
	float length = sqrtf(DotProduct(expected_direction, expected_direction));
	for (int axis = 0; axis < 3; ++axis)
	{
		expected_direction[axis] /= length;
		expected_direction[axis] += 0.3f * dm_retail_crandom() * inaccuracy;
	}
	float expected_yaw = atan2f(expected_direction[1], expected_direction[0]) *
		(180.0f / (float)M_PI);
	if (expected_yaw < 0.0f)
	{
		expected_yaw += 360.0f;
	}
	float horizontal = sqrtf(expected_direction[0] * expected_direction[0] +
		expected_direction[1] * expected_direction[1]);
	float expected_pitch = atan2f(-expected_direction[2], horizontal) *
		(180.0f / (float)M_PI);
	expected_pitch = dm_retail_angle_mod(expected_pitch +
		6.0f * g_dm_weapon_info.vspread * dm_retail_crandom() * inaccuracy);
	if (expected_pitch > 180.0f)
	{
		expected_pitch -= 360.0f;
	}
	expected_yaw = dm_retail_angle_mod(expected_yaw +
		6.0f * g_dm_weapon_info.hspread * dm_retail_crandom() * inaccuracy);
	srand(seed);

	ai_dm_state_t *dm_state = AI_DMState_Create(client);
	DM_ASSERT(dm_state != NULL);
	bot_client_state_t client_state;
	dm_prepare_client_state(&client_state, client);
	client_state.weapon_state = 5;
	ai_dm_enemy_info_t enemy;
	vec3_t enemy_origin = {100.0f, 0.0f, 0.0f};
	dm_prepare_enemy(&enemy, 2, enemy_origin, 100.0f, 4.0f);
	bot_input_t move_command = {0};
	move_command.thinktime = 0.1f;
	bot_input_t base = {0};
	DM_ASSERT(EA_ResetClient(client) == BLERR_NOERROR);
	DM_ASSERT(EA_SubmitInput(client, &base) == BLERR_NOERROR);
	AI_DMState_Update(dm_state,
		&client_state,
		NULL,
		&enemy,
		&move_command,
		4.0f);
	dm_consume_input(client, 0.1f);
	ai_dm_metrics_t metrics;
	AI_DMState_GetMetrics(dm_state, &metrics);
	DM_ASSERT_FLOAT_CLOSE(metrics.aim_target[0], expected_target[0], 0.0001f);
	DM_ASSERT_FLOAT_CLOSE(metrics.aim_target[1], expected_target[1], 0.0001f);
	DM_ASSERT_FLOAT_CLOSE(metrics.aim_target[2], expected_target[2], 0.0001f);
	DM_ASSERT_FLOAT_CLOSE(metrics.ideal_viewangles[PITCH], expected_pitch, 0.01f);
	DM_ASSERT_FLOAT_CLOSE(metrics.ideal_viewangles[YAW], expected_yaw, 0.01f);
	AI_DMState_Destroy(dm_state);
}

/*
=============
test_dm_aim_radial_ground_target_uses_retail_trace_order

Pins all three radial-ground traces, retained predicted X/Y, and the absent Q3
impact Z nudge before visible-target noise.
=============
*/
static void test_dm_aim_radial_ground_target_uses_retail_trace_order(void)
{
	const int client = 0;
	g_dm_characteristics[DM_CHARACTERISTIC_PIZZA_PREFERENCE] = 1.0f;
	g_dm_characteristics[DM_CHARACTERISTIC_AIM_SKILL] = 0.61f;
	g_dm_characteristics[DM_CHARACTERISTIC_AIM_ACCURACY] = 1.0f;
	g_dm_weapon_available = true;
	strcpy(g_dm_weapon_info.name, "Blaster");
	g_dm_weapon_info.offset[2] = 2.0f;
	bot_weapon_projectile_t projectile;
	memset(&projectile, 0, sizeof(projectile));
	projectile.damagetype = BOT_DAMAGETYPE_RADIAL;
	g_dm_weapon_info.projectileinfo = &projectile;

	bsp_trace_t *trace = dm_trace_result_at(0);
	trace->fraction = 1.0f;
	trace->ent = 2;
	trace = dm_trace_result_at(1);
	trace->fraction = 0.5f;
	trace->endpos[0] = 200.0f;
	trace->endpos[2] = -32.0f;
	trace = dm_trace_result_at(2);
	trace->fraction = 1.0f;
	trace->endpos[0] = 200.0f;
	trace->endpos[2] = -40.0f;
	trace = dm_trace_result_at(3);
	trace->fraction = 1.0f;

	ai_dm_state_t *dm_state = AI_DMState_Create(client);
	DM_ASSERT(dm_state != NULL);
	bot_client_state_t client_state;
	dm_prepare_client_state(&client_state, client);
	client_state.weapon_state = 6;
	client_state.last_client_update.viewoffset[2] = 10.0f;
	ai_dm_enemy_info_t enemy;
	vec3_t enemy_origin = {200.0f, 0.0f, 0.0f};
	dm_prepare_enemy(&enemy, 2, enemy_origin, 200.0f, 6.0f);
	bot_input_t move_command = {0};
	move_command.thinktime = 0.1f;
	bot_input_t base = {0};
	DM_ASSERT(EA_ResetClient(client) == BLERR_NOERROR);
	DM_ASSERT(EA_SubmitInput(client, &base) == BLERR_NOERROR);
	AI_DMState_Update(dm_state,
		&client_state,
		NULL,
		&enemy,
		&move_command,
		6.0f);
	dm_consume_input(client, 0.1f);

	ai_dm_metrics_t metrics;
	AI_DMState_GetMetrics(dm_state, &metrics);
	DM_ASSERT(g_dm_trace_count == 4);
	DM_ASSERT(g_dm_trace_calls[0].has_mins);
	DM_ASSERT_FLOAT_CLOSE(g_dm_trace_calls[0].mins[0], -4.0f, 0.0001f);
	DM_ASSERT(!g_dm_trace_calls[1].has_mins);
	DM_ASSERT_FLOAT_CLOSE(g_dm_trace_calls[1].start[0], 200.0f, 0.0001f);
	DM_ASSERT_FLOAT_CLOSE(g_dm_trace_calls[1].end[2], -64.0f, 0.0001f);
	DM_ASSERT(g_dm_trace_calls[1].passent == enemy.entity);
	DM_ASSERT_FLOAT_CLOSE(g_dm_trace_calls[2].start[2], 12.0f, 0.0001f);
	DM_ASSERT_FLOAT_CLOSE(g_dm_trace_calls[2].end[0], 200.0f, 0.0001f);
	DM_ASSERT_FLOAT_CLOSE(g_dm_trace_calls[2].end[2], -40.0f, 0.0001f);
	DM_ASSERT(g_dm_trace_calls[2].passent == client + 1);
	DM_ASSERT_FLOAT_CLOSE(g_dm_trace_calls[3].start[2], -40.0f, 0.0001f);
	DM_ASSERT_FLOAT_CLOSE(g_dm_trace_calls[3].end[2], 0.0f, 0.0001f);
	DM_ASSERT(g_dm_trace_calls[3].passent == enemy.entity);
	for (int index = 1; index <= 3; ++index)
	{
		DM_ASSERT(g_dm_trace_calls[index].mask == MASK_SHOT);
	}
	DM_ASSERT_FLOAT_CLOSE(metrics.aim_target[0], 200.0f, 0.0001f);
	DM_ASSERT_FLOAT_CLOSE(metrics.aim_target[1], 0.0f, 0.0001f);
	DM_ASSERT_FLOAT_CLOSE(metrics.aim_target[2], -40.0f, 0.0001f);
	AI_DMState_Destroy(dm_state);
}

/*
=============
test_dm_aim_radial_ground_target_preserves_strict_gates

Pins the strict skill, enemy-height, 150-unit shooter-distance, and final-clear
boundaries that distinguish the retail branch from its successor.
=============
*/
static void test_dm_aim_radial_ground_target_preserves_strict_gates(void)
{
	const int client = 0;
	bot_weapon_projectile_t projectile;
	memset(&projectile, 0, sizeof(projectile));
	projectile.damagetype = BOT_DAMAGETYPE_RADIAL;
	bsp_trace_t *trace;

	g_dm_characteristics[DM_CHARACTERISTIC_PIZZA_PREFERENCE] = 1.0f;
	g_dm_characteristics[DM_CHARACTERISTIC_AIM_SKILL] = 0.6f;
	g_dm_characteristics[DM_CHARACTERISTIC_AIM_ACCURACY] = 1.0f;
	g_dm_weapon_available = true;
	g_dm_weapon_info.projectileinfo = &projectile;
	g_dm_trace_result.fraction = 1.0f;
	g_dm_trace_result.ent = 2;
	ai_dm_state_t *dm_state = AI_DMState_Create(client);
	DM_ASSERT(dm_state != NULL);
	bot_client_state_t client_state;
	dm_prepare_client_state(&client_state, client);
	client_state.weapon_state = 1;
	ai_dm_enemy_info_t enemy;
	vec3_t enemy_origin = {200.0f, 0.0f, 0.0f};
	dm_prepare_enemy(&enemy, 2, enemy_origin, 200.0f, 7.0f);
	bot_input_t base = {0};
	DM_ASSERT(EA_ResetClient(client) == BLERR_NOERROR);
	DM_ASSERT(EA_SubmitInput(client, &base) == BLERR_NOERROR);
	AI_DMState_Update(dm_state, &client_state, NULL, &enemy, NULL, 7.0f);
	dm_consume_input(client, 0.1f);
	DM_ASSERT(g_dm_trace_count == 1);
	AI_DMState_Destroy(dm_state);

	dm_test_setup();
	g_dm_characteristics[DM_CHARACTERISTIC_PIZZA_PREFERENCE] = 1.0f;
	g_dm_characteristics[DM_CHARACTERISTIC_AIM_SKILL] = 0.61f;
	g_dm_characteristics[DM_CHARACTERISTIC_AIM_ACCURACY] = 1.0f;
	g_dm_weapon_available = true;
	g_dm_weapon_info.projectileinfo = &projectile;
	g_dm_trace_result.fraction = 1.0f;
	g_dm_trace_result.ent = 2;
	dm_state = AI_DMState_Create(client);
	DM_ASSERT(dm_state != NULL);
	dm_prepare_client_state(&client_state, client);
	client_state.weapon_state = 1;
	enemy_origin[2] = 16.0f;
	dm_prepare_enemy(&enemy, 2, enemy_origin, 200.0f, 8.0f);
	DM_ASSERT(EA_ResetClient(client) == BLERR_NOERROR);
	DM_ASSERT(EA_SubmitInput(client, &base) == BLERR_NOERROR);
	AI_DMState_Update(dm_state, &client_state, NULL, &enemy, NULL, 8.0f);
	dm_consume_input(client, 0.1f);
	DM_ASSERT(g_dm_trace_count == 1);
	AI_DMState_Destroy(dm_state);

	dm_test_setup();
	g_dm_characteristics[DM_CHARACTERISTIC_PIZZA_PREFERENCE] = 1.0f;
	g_dm_characteristics[DM_CHARACTERISTIC_AIM_SKILL] = 0.61f;
	g_dm_characteristics[DM_CHARACTERISTIC_AIM_ACCURACY] = 1.0f;
	g_dm_weapon_available = true;
	g_dm_weapon_info.projectileinfo = &projectile;
	trace = dm_trace_result_at(0);
	trace->fraction = 1.0f;
	trace->ent = 2;
	trace = dm_trace_result_at(1);
	trace->startsolid = qtrue;
	trace = dm_trace_result_at(2);
	trace->endpos[0] = 150.0f;
	trace->endpos[2] = 0.0f;
	dm_state = AI_DMState_Create(client);
	DM_ASSERT(dm_state != NULL);
	dm_prepare_client_state(&client_state, client);
	client_state.weapon_state = 1;
	VectorSet(enemy_origin, 150.0f, 0.0f, 0.0f);
	dm_prepare_enemy(&enemy, 2, enemy_origin, 150.0f, 9.0f);
	DM_ASSERT(EA_ResetClient(client) == BLERR_NOERROR);
	DM_ASSERT(EA_SubmitInput(client, &base) == BLERR_NOERROR);
	AI_DMState_Update(dm_state, &client_state, NULL, &enemy, NULL, 9.0f);
	dm_consume_input(client, 0.1f);
	ai_dm_metrics_t metrics;
	AI_DMState_GetMetrics(dm_state, &metrics);
	DM_ASSERT(g_dm_trace_count == 3);
	DM_ASSERT_FLOAT_CLOSE(metrics.aim_target[2], 8.0f, 0.0001f);
	AI_DMState_Destroy(dm_state);

	dm_test_setup();
	g_dm_characteristics[DM_CHARACTERISTIC_PIZZA_PREFERENCE] = 1.0f;
	g_dm_characteristics[DM_CHARACTERISTIC_AIM_SKILL] = 0.61f;
	g_dm_characteristics[DM_CHARACTERISTIC_AIM_ACCURACY] = 1.0f;
	g_dm_weapon_available = true;
	g_dm_weapon_info.projectileinfo = &projectile;
	trace = dm_trace_result_at(0);
	trace->fraction = 1.0f;
	trace->ent = 2;
	trace = dm_trace_result_at(1);
	trace->endpos[0] = 200.0f;
	trace->endpos[2] = -32.0f;
	trace = dm_trace_result_at(2);
	trace->endpos[0] = 200.0f;
	trace->endpos[2] = -40.0f;
	trace = dm_trace_result_at(3);
	trace->fraction = 0.999f;
	dm_state = AI_DMState_Create(client);
	DM_ASSERT(dm_state != NULL);
	dm_prepare_client_state(&client_state, client);
	client_state.weapon_state = 1;
	VectorSet(enemy_origin, 200.0f, 0.0f, 0.0f);
	dm_prepare_enemy(&enemy, 2, enemy_origin, 200.0f, 10.0f);
	DM_ASSERT(EA_ResetClient(client) == BLERR_NOERROR);
	DM_ASSERT(EA_SubmitInput(client, &base) == BLERR_NOERROR);
	AI_DMState_Update(dm_state, &client_state, NULL, &enemy, NULL, 10.0f);
	dm_consume_input(client, 0.1f);
	AI_DMState_GetMetrics(dm_state, &metrics);
	DM_ASSERT(g_dm_trace_count == 4);
	DM_ASSERT_FLOAT_CLOSE(metrics.aim_target[2], 8.0f, 0.0001f);
	AI_DMState_Destroy(dm_state);
}

/*
=============
test_dm_aim_radial_ground_target_rejects_exact_impact_boundaries

Pins the strict less-than 50 vertical and less-than 60 distance checks.
=============
*/
static void test_dm_aim_radial_ground_target_rejects_exact_impact_boundaries(void)
{
	const int client = 0;
	bot_weapon_projectile_t projectile;
	memset(&projectile, 0, sizeof(projectile));
	projectile.damagetype = BOT_DAMAGETYPE_RADIAL;
	bot_input_t base = {0};
	bot_client_state_t client_state;
	ai_dm_enemy_info_t enemy;
	vec3_t enemy_origin = {200.0f, 0.0f, 0.0f};

	g_dm_characteristics[DM_CHARACTERISTIC_PIZZA_PREFERENCE] = 1.0f;
	g_dm_characteristics[DM_CHARACTERISTIC_AIM_SKILL] = 0.61f;
	g_dm_characteristics[DM_CHARACTERISTIC_AIM_ACCURACY] = 1.0f;
	g_dm_weapon_available = true;
	g_dm_weapon_info.projectileinfo = &projectile;
	bsp_trace_t *trace = dm_trace_result_at(0);
	trace->fraction = 1.0f;
	trace->ent = 2;
	trace = dm_trace_result_at(1);
	trace->startsolid = qtrue;
	trace = dm_trace_result_at(2);
	VectorSet(trace->endpos, 200.0f, 0.0f, 34.0f);
	ai_dm_state_t *dm_state = AI_DMState_Create(client);
	DM_ASSERT(dm_state != NULL);
	dm_prepare_client_state(&client_state, client);
	client_state.weapon_state = 1;
	dm_prepare_enemy(&enemy, 2, enemy_origin, 200.0f, 10.5f);
	DM_ASSERT(EA_ResetClient(client) == BLERR_NOERROR);
	DM_ASSERT(EA_SubmitInput(client, &base) == BLERR_NOERROR);
	AI_DMState_Update(dm_state, &client_state, NULL, &enemy, NULL, 10.5f);
	dm_consume_input(client, 0.1f);
	ai_dm_metrics_t metrics;
	AI_DMState_GetMetrics(dm_state, &metrics);
	DM_ASSERT(g_dm_trace_count == 3);
	DM_ASSERT_FLOAT_CLOSE(metrics.aim_target[2], 8.0f, 0.0001f);
	AI_DMState_Destroy(dm_state);

	dm_test_setup();
	g_dm_characteristics[DM_CHARACTERISTIC_PIZZA_PREFERENCE] = 1.0f;
	g_dm_characteristics[DM_CHARACTERISTIC_AIM_SKILL] = 0.61f;
	g_dm_characteristics[DM_CHARACTERISTIC_AIM_ACCURACY] = 1.0f;
	g_dm_weapon_available = true;
	g_dm_weapon_info.projectileinfo = &projectile;
	trace = dm_trace_result_at(0);
	trace->fraction = 1.0f;
	trace->ent = 2;
	trace = dm_trace_result_at(1);
	trace->startsolid = qtrue;
	trace = dm_trace_result_at(2);
	VectorSet(trace->endpos, 260.0f, 0.0f, -16.0f);
	dm_state = AI_DMState_Create(client);
	DM_ASSERT(dm_state != NULL);
	dm_prepare_client_state(&client_state, client);
	client_state.weapon_state = 1;
	dm_prepare_enemy(&enemy, 2, enemy_origin, 200.0f, 10.6f);
	DM_ASSERT(EA_ResetClient(client) == BLERR_NOERROR);
	DM_ASSERT(EA_SubmitInput(client, &base) == BLERR_NOERROR);
	AI_DMState_Update(dm_state, &client_state, NULL, &enemy, NULL, 10.6f);
	dm_consume_input(client, 0.1f);
	AI_DMState_GetMetrics(dm_state, &metrics);
	DM_ASSERT(g_dm_trace_count == 3);
	DM_ASSERT_FLOAT_CLOSE(metrics.aim_target[2], 8.0f, 0.0001f);
	AI_DMState_Destroy(dm_state);
}

/*
=============
test_dm_check_attack_uses_aas_entity_visible

Pins BotCheckAttack's direct call to the retail AAS visibility service.
=============
*/
static void test_dm_check_attack_uses_aas_entity_visible(void)
{
	const int client = 0;
	bot_input_t base = {0};
	bot_client_state_t client_state;
	ai_dm_enemy_info_t enemy;
	vec3_t enemy_origin = {200.0f, 0.0f, 0.0f};

	g_dm_characteristics[DM_CHARACTERISTIC_PIZZA_PREFERENCE] = 1.0f;
	g_dm_characteristics[DM_CHARACTERISTIC_REACTION_TIME] = 0.0f;
	g_dm_characteristics[DM_CHARACTERISTIC_AIM_ACCURACY] = 1.0f;
	g_dm_weapon_available = true;
	bsp_trace_t *trace = dm_trace_result_at(0);
	trace->fraction = 1.0f;
	trace->ent = 2;
	trace = dm_trace_result_at(1);
	trace->fraction = 0.5f;
	trace->ent = 2;

	ai_dm_state_t *dm_state = AI_DMState_Create(client);
	DM_ASSERT(dm_state != NULL);
	dm_prepare_client_state(&client_state, client);
	client_state.weapon_state = 1;
	client_state.last_client_update.viewoffset[2] = 20.0f;
	dm_prepare_enemy(&enemy, 2, enemy_origin, 200.0f, 20.0f);
	DM_ASSERT(EA_ResetClient(client) == BLERR_NOERROR);
	DM_ASSERT(EA_SubmitInput(client, &base) == BLERR_NOERROR);
	AI_DMState_Update(dm_state, &client_state, NULL, &enemy, NULL, 20.0f);
	bot_input_t command = dm_consume_input(client, 0.1f);

	DM_ASSERT((command.actionflags & ACTION_ATTACK) != 0);
	DM_ASSERT(g_dm_entity_visible_call_count == 1);
	DM_ASSERT(g_dm_entity_visible_viewer == client + 1);
	DM_ASSERT_FLOAT_CLOSE(g_dm_entity_visible_eye[2], 20.0f, 0.0001f);
	DM_ASSERT_FLOAT_CLOSE(g_dm_entity_visible_viewangles[YAW], 0.0f, 0.0001f);
	DM_ASSERT_FLOAT_CLOSE(g_dm_entity_visible_field_of_view, 50.0f, 0.0001f);
	DM_ASSERT(g_dm_entity_visible_target == enemy.entity);
	DM_ASSERT(g_dm_trace_count == 2);
	AI_DMState_Destroy(dm_state);

	dm_test_setup();
	g_dm_characteristics[DM_CHARACTERISTIC_PIZZA_PREFERENCE] = 1.0f;
	g_dm_characteristics[DM_CHARACTERISTIC_REACTION_TIME] = 0.0f;
	g_dm_characteristics[DM_CHARACTERISTIC_AIM_ACCURACY] = 1.0f;
	g_dm_weapon_available = true;
	g_dm_entity_visible_result = qfalse;
	trace = dm_trace_result_at(0);
	trace->fraction = 1.0f;
	trace->ent = 2;
	dm_state = AI_DMState_Create(client);
	DM_ASSERT(dm_state != NULL);
	dm_prepare_client_state(&client_state, client);
	client_state.weapon_state = 1;
	dm_prepare_enemy(&enemy, 2, enemy_origin, 200.0f, 20.1f);
	DM_ASSERT(EA_ResetClient(client) == BLERR_NOERROR);
	DM_ASSERT(EA_SubmitInput(client, &base) == BLERR_NOERROR);
	AI_DMState_Update(dm_state, &client_state, NULL, &enemy, NULL, 20.1f);
	command = dm_consume_input(client, 0.1f);
	DM_ASSERT((command.actionflags & ACTION_ATTACK) == 0);
	DM_ASSERT(g_dm_entity_visible_call_count == 1);
	DM_ASSERT(g_dm_trace_count == 1);
	AI_DMState_Destroy(dm_state);
}

/*
=============
test_dm_check_attack_builds_retail_weapon_sweep

Pins full forward/right weapon offsets, the backed start, 1000-unit end,
eight-unit box, direct hit, and ordinary-weapon latch toggle.
=============
*/
static void test_dm_check_attack_builds_retail_weapon_sweep(void)
{
	const int client = 0;
	g_dm_characteristics[DM_CHARACTERISTIC_PIZZA_PREFERENCE] = 1.0f;
	g_dm_characteristics[DM_CHARACTERISTIC_REACTION_TIME] = 0.0f;
	g_dm_characteristics[DM_CHARACTERISTIC_AIM_ACCURACY] = 1.0f;
	g_dm_weapon_available = true;
	strcpy(g_dm_weapon_info.name, "Blaster");
	g_dm_weapon_info.offset[0] = 10.0f;
	g_dm_weapon_info.offset[1] = 5.0f;
	g_dm_weapon_info.offset[2] = 3.0f;
	bsp_trace_t *trace = dm_trace_result_at(0);
	trace->fraction = 1.0f;
	trace->ent = 2;
	trace = dm_trace_result_at(1);
	trace->fraction = 1.0f;
	trace->ent = 2;
	trace = dm_trace_result_at(2);
	trace->fraction = 0.5f;
	trace->ent = 2;

	ai_dm_state_t *dm_state = AI_DMState_Create(client);
	DM_ASSERT(dm_state != NULL);
	bot_client_state_t client_state;
	dm_prepare_client_state(&client_state, client);
	client_state.weapon_state = 9;
	VectorSet(client_state.last_client_update.origin, 1.0f, 2.0f, 3.0f);
	client_state.last_client_update.viewoffset[2] = 20.0f;
	ai_dm_enemy_info_t enemy;
	vec3_t enemy_origin = {1.0f, 202.0f, 15.0f};
	dm_prepare_enemy(&enemy, 2, enemy_origin, 200.0f, 11.0f);
	bot_input_t base = {0};
	DM_ASSERT(EA_ResetClient(client) == BLERR_NOERROR);
	DM_ASSERT(EA_SubmitInput(client, &base) == BLERR_NOERROR);
	AI_DMState_Update(dm_state, &client_state, NULL, &enemy, NULL, 11.0f);
	bot_input_t command = dm_consume_input(client, 0.1f);

	DM_ASSERT((command.actionflags & ACTION_ATTACK) != 0);
	DM_ASSERT(g_dm_trace_count == 2);
	DM_ASSERT_FLOAT_CLOSE(g_dm_trace_calls[1].start[0], 6.0f, 0.01f);
	DM_ASSERT_FLOAT_CLOSE(g_dm_trace_calls[1].start[1], 0.0f, 0.01f);
	DM_ASSERT_FLOAT_CLOSE(g_dm_trace_calls[1].start[2], 26.0f, 0.01f);
	DM_ASSERT_FLOAT_CLOSE(g_dm_trace_calls[1].end[0], 6.0f, 0.01f);
	DM_ASSERT_FLOAT_CLOSE(g_dm_trace_calls[1].end[1], 1012.0f, 0.02f);
	DM_ASSERT_FLOAT_CLOSE(g_dm_trace_calls[1].end[2], 26.0f, 0.01f);
	DM_ASSERT_FLOAT_CLOSE(g_dm_trace_calls[1].mins[0], -8.0f, 0.0001f);
	DM_ASSERT_FLOAT_CLOSE(g_dm_trace_calls[1].maxs[2], 8.0f, 0.0001f);
	DM_ASSERT(g_dm_trace_calls[1].passent == client + 1);
	DM_ASSERT(g_dm_trace_calls[1].mask == MASK_SHOT);
	ai_dm_metrics_t metrics;
	AI_DMState_GetMetrics(dm_state, &metrics);
	DM_ASSERT(metrics.attack_latched);
	AI_DMState_Destroy(dm_state);
}

/*
=============
test_dm_check_attack_preserves_distance_fov_boundary

Pins distance below 100 selecting 120 degrees and exactly 100 selecting 50.
=============
*/
static void test_dm_check_attack_preserves_distance_fov_boundary(void)
{
	const int client = 0;
	const float cosine_thirty = 0.8660254038f;
	const float sine_thirty = 0.5f;
	g_dm_characteristics[DM_CHARACTERISTIC_PIZZA_PREFERENCE] = 1.0f;
	g_dm_characteristics[DM_CHARACTERISTIC_REACTION_TIME] = 0.0f;
	g_dm_characteristics[DM_CHARACTERISTIC_AIM_ACCURACY] = 0.8f;
	g_dm_characteristics[DM_CHARACTERISTIC_VIEW_FACTOR] = 0.1f;
	g_dm_characteristics[DM_CHARACTERISTIC_VIEW_MAXCHANGE] = 0.1f;
	g_dm_weapon_available = true;
	g_dm_trace_result.fraction = 1.0f;
	g_dm_trace_result.ent = 2;
	ai_dm_state_t *dm_state = AI_DMState_Create(client);
	DM_ASSERT(dm_state != NULL);
	bot_client_state_t client_state;
	dm_prepare_client_state(&client_state, client);
	client_state.weapon_state = 1;
	ai_dm_enemy_info_t enemy;
	vec3_t enemy_origin = {
		99.999f * cosine_thirty,
		99.999f * sine_thirty,
		0.0f
	};
	dm_prepare_enemy(&enemy, 2, enemy_origin, 99.999f, 12.0f);
	bot_input_t base = {0};
	DM_ASSERT(EA_ResetClient(client) == BLERR_NOERROR);
	DM_ASSERT(EA_SubmitInput(client, &base) == BLERR_NOERROR);
	AI_DMState_Update(dm_state, &client_state, NULL, &enemy, NULL, 12.0f);
	bot_input_t command = dm_consume_input(client, 0.1f);
	DM_ASSERT((command.actionflags & ACTION_ATTACK) != 0);
	DM_ASSERT(g_dm_trace_count == 2);
	DM_ASSERT(g_dm_entity_visible_call_count == 1);
	DM_ASSERT_FLOAT_CLOSE(g_dm_entity_visible_field_of_view, 120.0f, 0.0001f);
	AI_DMState_Destroy(dm_state);

	dm_test_setup();
	g_dm_characteristics[DM_CHARACTERISTIC_PIZZA_PREFERENCE] = 1.0f;
	g_dm_characteristics[DM_CHARACTERISTIC_REACTION_TIME] = 0.0f;
	g_dm_characteristics[DM_CHARACTERISTIC_AIM_ACCURACY] = 0.8f;
	g_dm_characteristics[DM_CHARACTERISTIC_VIEW_FACTOR] = 0.1f;
	g_dm_characteristics[DM_CHARACTERISTIC_VIEW_MAXCHANGE] = 0.1f;
	g_dm_weapon_available = true;
	g_dm_trace_result.fraction = 1.0f;
	g_dm_trace_result.ent = 2;
	dm_state = AI_DMState_Create(client);
	DM_ASSERT(dm_state != NULL);
	dm_prepare_client_state(&client_state, client);
	client_state.weapon_state = 1;
	VectorSet(enemy_origin,
		100.0f * cosine_thirty,
		100.0f * sine_thirty,
		0.0f);
	dm_prepare_enemy(&enemy, 2, enemy_origin, 100.0f, 13.0f);
	DM_ASSERT(EA_ResetClient(client) == BLERR_NOERROR);
	DM_ASSERT(EA_SubmitInput(client, &base) == BLERR_NOERROR);
	AI_DMState_Update(dm_state, &client_state, NULL, &enemy, NULL, 13.0f);
	command = dm_consume_input(client, 0.1f);
	DM_ASSERT((command.actionflags & ACTION_ATTACK) != 0);
	DM_ASSERT(g_dm_trace_count == 2);
	DM_ASSERT(g_dm_entity_visible_call_count == 1);
	DM_ASSERT_FLOAT_CLOSE(g_dm_entity_visible_field_of_view, 50.0f, 0.0001f);
	AI_DMState_Destroy(dm_state);
}

/*
=============
test_dm_check_attack_suppresses_teammate_and_radial_self_damage

Pins the wrong-client team guard, strict splash radius, and zero-points edge.
=============
*/
static void test_dm_check_attack_suppresses_teammate_and_radial_self_damage(void)
{
	const int client = 0;
	g_dm_characteristics[DM_CHARACTERISTIC_PIZZA_PREFERENCE] = 1.0f;
	g_dm_characteristics[DM_CHARACTERISTIC_REACTION_TIME] = 0.0f;
	g_dm_characteristics[DM_CHARACTERISTIC_AIM_ACCURACY] = 1.0f;
	g_dm_weapon_available = true;
	bsp_trace_t *trace = dm_trace_result_at(0);
	trace->fraction = 1.0f;
	trace->ent = 2;
	trace = dm_trace_result_at(1);
	trace->fraction = 0.2f;
	trace->ent = 3;
	ai_dm_state_t *dm_state = AI_DMState_Create(client);
	DM_ASSERT(dm_state != NULL);
	bot_client_state_t client_state;
	dm_prepare_client_state(&client_state, client);
	client_state.weapon_state = 1;
	g_dm_same_team_result = 1;
	ai_dm_enemy_info_t enemy;
	vec3_t enemy_origin = {200.0f, 0.0f, 0.0f};
	dm_prepare_enemy(&enemy, 2, enemy_origin, 200.0f, 14.0f);
	bot_input_t base = {0};
	DM_ASSERT(EA_ResetClient(client) == BLERR_NOERROR);
	DM_ASSERT(EA_SubmitInput(client, &base) == BLERR_NOERROR);
	AI_DMState_Update(dm_state, &client_state, NULL, &enemy, NULL, 14.0f);
	bot_input_t command = dm_consume_input(client, 0.1f);
	ai_dm_metrics_t metrics;
	AI_DMState_GetMetrics(dm_state, &metrics);
	DM_ASSERT((command.actionflags & ACTION_ATTACK) == 0);
	DM_ASSERT(!metrics.attack_latched);
	DM_ASSERT(g_dm_same_team_state == &client_state);
	DM_ASSERT(g_dm_same_team_entity == 3);
	AI_DMState_Destroy(dm_state);

	dm_test_setup();
	g_dm_characteristics[DM_CHARACTERISTIC_PIZZA_PREFERENCE] = 1.0f;
	g_dm_characteristics[DM_CHARACTERISTIC_REACTION_TIME] = 0.0f;
	g_dm_characteristics[DM_CHARACTERISTIC_AIM_ACCURACY] = 1.0f;
	g_dm_weapon_available = true;
	bot_weapon_projectile_t projectile;
	memset(&projectile, 0, sizeof(projectile));
	projectile.damagetype = BOT_DAMAGETYPE_RADIAL;
	projectile.damage = 100;
	projectile.radius = 100.0f;
	g_dm_weapon_info.projectileinfo = &projectile;
	trace = dm_trace_result_at(0);
	trace->fraction = 1.0f;
	trace->ent = 2;
	trace = dm_trace_result_at(1);
	trace->fraction = 0.05f;
	trace->ent = -1;
	dm_state = AI_DMState_Create(client);
	DM_ASSERT(dm_state != NULL);
	dm_prepare_client_state(&client_state, client);
	client_state.weapon_state = 1;
	dm_prepare_enemy(&enemy, 2, enemy_origin, 200.0f, 15.0f);
	DM_ASSERT(EA_ResetClient(client) == BLERR_NOERROR);
	DM_ASSERT(EA_SubmitInput(client, &base) == BLERR_NOERROR);
	AI_DMState_Update(dm_state, &client_state, NULL, &enemy, NULL, 15.0f);
	command = dm_consume_input(client, 0.1f);
	AI_DMState_GetMetrics(dm_state, &metrics);
	DM_ASSERT((command.actionflags & ACTION_ATTACK) == 0);
	DM_ASSERT(!metrics.attack_latched);
	AI_DMState_Destroy(dm_state);

	dm_test_setup();
	g_dm_characteristics[DM_CHARACTERISTIC_PIZZA_PREFERENCE] = 1.0f;
	g_dm_characteristics[DM_CHARACTERISTIC_REACTION_TIME] = 0.0f;
	g_dm_characteristics[DM_CHARACTERISTIC_AIM_ACCURACY] = 1.0f;
	g_dm_weapon_available = true;
	projectile.damagetype = BOT_DAMAGETYPE_RADIAL;
	projectile.damage = 100;
	projectile.radius = 100.0f;
	g_dm_weapon_info.projectileinfo = &projectile;
	trace = dm_trace_result_at(0);
	trace->fraction = 1.0f;
	trace->ent = 2;
	trace = dm_trace_result_at(1);
	trace->fraction = 0.1f;
	trace->ent = -1;
	dm_state = AI_DMState_Create(client);
	DM_ASSERT(dm_state != NULL);
	dm_prepare_client_state(&client_state, client);
	client_state.weapon_state = 1;
	dm_prepare_enemy(&enemy, 2, enemy_origin, 200.0f, 16.0f);
	DM_ASSERT(EA_ResetClient(client) == BLERR_NOERROR);
	DM_ASSERT(EA_SubmitInput(client, &base) == BLERR_NOERROR);
	AI_DMState_Update(dm_state, &client_state, NULL, &enemy, NULL, 16.0f);
	command = dm_consume_input(client, 0.1f);
	DM_ASSERT((command.actionflags & ACTION_ATTACK) != 0);
	AI_DMState_Destroy(dm_state);

	dm_test_setup();
	g_dm_characteristics[DM_CHARACTERISTIC_PIZZA_PREFERENCE] = 1.0f;
	g_dm_characteristics[DM_CHARACTERISTIC_REACTION_TIME] = 0.0f;
	g_dm_characteristics[DM_CHARACTERISTIC_AIM_ACCURACY] = 1.0f;
	g_dm_weapon_available = true;
	projectile.damagetype = BOT_DAMAGETYPE_RADIAL;
	projectile.damage = 100;
	projectile.radius = 300.0f;
	g_dm_weapon_info.projectileinfo = &projectile;
	trace = dm_trace_result_at(0);
	trace->fraction = 1.0f;
	trace->ent = 2;
	trace = dm_trace_result_at(1);
	trace->fraction = 0.2f;
	trace->ent = -1;
	dm_state = AI_DMState_Create(client);
	DM_ASSERT(dm_state != NULL);
	dm_prepare_client_state(&client_state, client);
	client_state.weapon_state = 1;
	dm_prepare_enemy(&enemy, 2, enemy_origin, 200.0f, 17.0f);
	DM_ASSERT(EA_ResetClient(client) == BLERR_NOERROR);
	DM_ASSERT(EA_SubmitInput(client, &base) == BLERR_NOERROR);
	AI_DMState_Update(dm_state, &client_state, NULL, &enemy, NULL, 17.0f);
	command = dm_consume_input(client, 0.1f);
	DM_ASSERT((command.actionflags & ACTION_ATTACK) != 0);
	AI_DMState_Destroy(dm_state);
}

/*
=============
test_dm_check_attack_verifies_window_and_release_latch

Pins the retail-only window follow-up and alternating fire-on-release action.
=============
*/
static void test_dm_check_attack_verifies_window_and_release_latch(void)
{
	const int client = 0;
	g_dm_characteristics[DM_CHARACTERISTIC_PIZZA_PREFERENCE] = 1.0f;
	g_dm_characteristics[DM_CHARACTERISTIC_REACTION_TIME] = 0.0f;
	g_dm_characteristics[DM_CHARACTERISTIC_AIM_ACCURACY] = 1.0f;
	g_dm_weapon_available = true;
	bsp_trace_t *trace = dm_trace_result_at(0);
	trace->fraction = 1.0f;
	trace->ent = 2;
	trace = dm_trace_result_at(1);
	trace->fraction = 0.5f;
	trace->ent = -1;
	trace->contents = CONTENTS_WINDOW;
	trace->endpos[0] = 50.0f;
	trace = dm_trace_result_at(2);
	trace->fraction = 0.25f;
	trace->ent = 2;
	ai_dm_state_t *dm_state = AI_DMState_Create(client);
	DM_ASSERT(dm_state != NULL);
	bot_client_state_t client_state;
	dm_prepare_client_state(&client_state, client);
	client_state.weapon_state = 1;
	ai_dm_enemy_info_t enemy;
	vec3_t enemy_origin = {200.0f, 0.0f, 0.0f};
	dm_prepare_enemy(&enemy, 2, enemy_origin, 200.0f, 18.0f);
	bot_input_t base = {0};
	DM_ASSERT(EA_ResetClient(client) == BLERR_NOERROR);
	DM_ASSERT(EA_SubmitInput(client, &base) == BLERR_NOERROR);
	AI_DMState_Update(dm_state, &client_state, NULL, &enemy, NULL, 18.0f);
	bot_input_t command = dm_consume_input(client, 0.1f);
	DM_ASSERT((command.actionflags & ACTION_ATTACK) != 0);
	DM_ASSERT(g_dm_trace_count == 3);
	DM_ASSERT(!g_dm_trace_calls[2].has_mins);
	DM_ASSERT_FLOAT_CLOSE(g_dm_trace_calls[2].start[0], 50.0f, 0.0001f);
	DM_ASSERT_FLOAT_CLOSE(g_dm_trace_calls[2].end[0], 200.0f, 0.0001f);
	DM_ASSERT(g_dm_trace_calls[2].passent == client + 1);
	AI_DMState_Destroy(dm_state);

	dm_test_setup();
	g_dm_characteristics[DM_CHARACTERISTIC_PIZZA_PREFERENCE] = 1.0f;
	g_dm_characteristics[DM_CHARACTERISTIC_REACTION_TIME] = 0.0f;
	g_dm_characteristics[DM_CHARACTERISTIC_AIM_ACCURACY] = 1.0f;
	g_dm_weapon_available = true;
	trace = dm_trace_result_at(0);
	trace->fraction = 1.0f;
	trace->ent = 2;
	trace = dm_trace_result_at(1);
	trace->fraction = 0.5f;
	trace->ent = -1;
	trace->contents = CONTENTS_WINDOW;
	trace->endpos[0] = 50.0f;
	trace = dm_trace_result_at(2);
	trace->fraction = 1.0f;
	trace->ent = -1;
	dm_state = AI_DMState_Create(client);
	DM_ASSERT(dm_state != NULL);
	dm_prepare_client_state(&client_state, client);
	client_state.weapon_state = 1;
	dm_prepare_enemy(&enemy, 2, enemy_origin, 200.0f, 18.1f);
	DM_ASSERT(EA_ResetClient(client) == BLERR_NOERROR);
	DM_ASSERT(EA_SubmitInput(client, &base) == BLERR_NOERROR);
	AI_DMState_Update(dm_state, &client_state, NULL, &enemy, NULL, 18.1f);
	command = dm_consume_input(client, 0.1f);
	ai_dm_metrics_t metrics;
	AI_DMState_GetMetrics(dm_state, &metrics);
	DM_ASSERT((command.actionflags & ACTION_ATTACK) == 0);
	DM_ASSERT(!metrics.attack_latched);
	DM_ASSERT(g_dm_trace_count == 3);
	AI_DMState_Destroy(dm_state);

	dm_test_setup();
	g_dm_characteristics[DM_CHARACTERISTIC_PIZZA_PREFERENCE] = 1.0f;
	g_dm_characteristics[DM_CHARACTERISTIC_REACTION_TIME] = 0.0f;
	g_dm_characteristics[DM_CHARACTERISTIC_AIM_ACCURACY] = 1.0f;
	g_dm_weapon_available = true;
	g_dm_weapon_info.flags = BOT_WEAPON_FIRERELEASED;
	g_dm_trace_result.fraction = 1.0f;
	g_dm_trace_result.ent = 2;
	dm_state = AI_DMState_Create(client);
	DM_ASSERT(dm_state != NULL);
	dm_prepare_client_state(&client_state, client);
	client_state.weapon_state = 1;
	dm_prepare_enemy(&enemy, 2, enemy_origin, 200.0f, 19.0f);
	DM_ASSERT(EA_ResetClient(client) == BLERR_NOERROR);
	DM_ASSERT(EA_SubmitInput(client, &base) == BLERR_NOERROR);
	AI_DMState_Update(dm_state, &client_state, NULL, &enemy, NULL, 19.0f);
	command = dm_consume_input(client, 0.1f);
	AI_DMState_GetMetrics(dm_state, &metrics);
	DM_ASSERT((command.actionflags & ACTION_ATTACK) == 0);
	DM_ASSERT(metrics.attack_latched);
	AI_DMState_Update(dm_state, &client_state, NULL, &enemy, NULL, 19.1f);
	command = dm_consume_input(client, 0.1f);
	AI_DMState_GetMetrics(dm_state, &metrics);
	DM_ASSERT((command.actionflags & ACTION_ATTACK) != 0);
	DM_ASSERT(!metrics.attack_latched);
	DM_ASSERT(g_dm_trace_count == 4);
	AI_DMState_Destroy(dm_state);
}

/*
=============
test_dm_private_view_accumulates_client_delta_angles

Pins BotUpdateClient's retail private-view accumulation and 16-bit wrapping.
=============
*/
static void test_dm_private_view_accumulates_client_delta_angles(void)
{
	ai_dm_state_t *dm_state = AI_DMState_Create(0);
	DM_ASSERT(dm_state != NULL);

	vec3_t viewangles;
	DM_ASSERT(AI_DMState_GetViewAngles(dm_state, viewangles));
	DM_ASSERT_FLOAT_CLOSE(viewangles[PITCH], 0.0f, 0.0f);
	DM_ASSERT_FLOAT_CLOSE(viewangles[YAW], 0.0f, 0.0f);
	DM_ASSERT_FLOAT_CLOSE(viewangles[ROLL], 0.0f, 0.0f);

	vec3_t first_delta = {370.0f, -10.0f, 720.5f};
	AI_DMState_ApplyDeltaAngles(dm_state, first_delta);
	DM_ASSERT(AI_DMState_GetViewAngles(dm_state, viewangles));
	for (int axis = 0; axis < 3; ++axis)
	{
		DM_ASSERT_FLOAT_CLOSE(viewangles[axis],
			dm_retail_angle_mod(first_delta[axis]),
			0.0001f);
	}

	vec3_t second_delta = {5.0f, 15.0f, -0.5f};
	AI_DMState_ApplyDeltaAngles(dm_state, second_delta);
	DM_ASSERT(AI_DMState_GetViewAngles(dm_state, viewangles));
	for (int axis = 0; axis < 3; ++axis)
	{
		float expected = dm_retail_angle_mod(
			dm_retail_angle_mod(first_delta[axis]) +
			second_delta[axis]);
		DM_ASSERT_FLOAT_CLOSE(viewangles[axis], expected, 0.0001f);
	}

	AI_DMState_Reset(dm_state);
	DM_ASSERT(AI_DMState_GetViewAngles(dm_state, viewangles));
	DM_ASSERT_FLOAT_CLOSE(viewangles[PITCH], 0.0f, 0.0f);
	DM_ASSERT_FLOAT_CLOSE(viewangles[YAW], 0.0f, 0.0f);
	DM_ASSERT_FLOAT_CLOSE(viewangles[ROLL], 0.0f, 0.0f);
	AI_DMState_Destroy(dm_state);
}

/*
=============
test_dm_direct_battle_primitives_preserve_view_aim_attack

Pins the public seams consumed by Battle Chase, Battle NBG, and Battle Retreat:
direct movement ideal views, weapon-aware enemy aim, and attack eligibility.
=============
*/
static void test_dm_direct_battle_primitives_preserve_view_aim_attack(void)
{
	const int client = 0;
	g_dm_characteristics[DM_CHARACTERISTIC_VIEW_FACTOR] = 1800.0f;
	g_dm_characteristics[DM_CHARACTERISTIC_VIEW_MAXCHANGE] = 1800.0f;
	ai_dm_state_t *dm_state = AI_DMState_Create(client);
	DM_ASSERT(dm_state != NULL);

	bot_client_state_t client_state;
	dm_prepare_client_state(&client_state, client);
	bot_input_t base = {0};
	vec3_t movement_view = {0.0f, 90.0f, 0.0f};
	DM_ASSERT(EA_ResetClient(client) == BLERR_NOERROR);
	DM_ASSERT(EA_SubmitInput(client, &base) == BLERR_NOERROR);
	AI_DMState_SetEnemyContext(dm_state, 1, 0.0f, 0, NULL);
	AI_DMState_SetIdealViewAngles(dm_state, movement_view);
	AI_DMState_ChangeViewAngles(dm_state, &client_state, 0.1f);
	bot_input_t command = dm_consume_input(client, 0.1f);
	DM_ASSERT_FLOAT_CLOSE(command.viewangles[YAW], 90.0f, 0.01f);

	AI_DMState_Destroy(dm_state);
	dm_test_setup();
	g_dm_trace_result.fraction = 1.0f;
	g_dm_trace_result.ent = 2;
	dm_state = AI_DMState_Create(client);
	DM_ASSERT(dm_state != NULL);
	dm_prepare_client_state(&client_state, client);
	dm_enable_attack_weapon(&client_state);
	ai_dm_enemy_info_t enemy;
	vec3_t enemy_origin = {200.0f, 0.0f, 0.0f};
	dm_prepare_enemy(&enemy, 2, enemy_origin, 200.0f, 0.0f);
	DM_ASSERT(EA_ResetClient(client) == BLERR_NOERROR);
	DM_ASSERT(EA_SubmitInput(client, &base) == BLERR_NOERROR);
	AI_DMState_SetEnemyContext(dm_state, enemy.entity, 0.0f, 0, enemy.origin);
	AI_DMState_AimAtEnemy(dm_state, &client_state, &enemy, 0.1f);
	DM_ASSERT(AI_DMState_CheckAttack(dm_state, &client_state, &enemy, 1.0f));
	command = dm_consume_input(client, 0.1f);
	DM_ASSERT((command.actionflags & ACTION_ATTACK) != 0);
	DM_ASSERT_FLOAT_CLOSE(command.viewangles[YAW], 0.0f, 0.01f);
	AI_DMState_Destroy(dm_state);
}

/*
=============
main
=============
*/
int main(void)
{
	struct dm_test_case
	{
		const char *name;
		int (*setup)(void);
		void (*test)(void);
		void (*teardown)(void);
	} tests[] = {
		{"private_view_delta", dm_test_setup, test_dm_private_view_accumulates_client_delta_angles, dm_test_teardown},
		{"direct_battle_primitives", dm_test_setup, test_dm_direct_battle_primitives_preserve_view_aim_attack, dm_test_teardown},
		{"reaction_delay", dm_test_setup, test_dm_reaction_delay_gates_attack, dm_test_teardown},
		{"damage_reaction_delay", dm_test_setup, test_dm_damage_exposure_keeps_reaction_delay, dm_test_teardown},
		{"attack_no_cooldown", dm_test_setup, test_dm_ready_attack_has_no_generic_cooldown, dm_test_teardown},
		{"attack_skill_distance", dm_test_setup, test_dm_low_skill_attack_distance_bands, dm_test_teardown},
		{"attack_body_origin", dm_test_setup, test_dm_attack_move_uses_body_origin_not_eye_position, dm_test_teardown},
		{"attack_skill_four_tenths", dm_test_setup, test_dm_attack_skill_four_tenths_enters_strafe, dm_test_teardown},
		{"attack_chase_goal", dm_test_setup, test_dm_attack_chase_builds_cached_goal_before_characteristic_work, dm_test_teardown},
		{"pizza_preference", dm_test_setup, test_dm_pizza_preference_skips_attack_move, dm_test_teardown},
		{"strafe_fixed_step", dm_test_setup, test_dm_strafe_clock_uses_retail_fixed_step, dm_test_teardown},
		{"jump_latch", dm_test_setup, test_dm_jump_requests_use_retail_latch, dm_test_teardown},
		{"crouch_timer", dm_test_setup, test_dm_crouch_timer_uses_characteristic_window, dm_test_teardown},
		{"strafe_retry", dm_test_setup, test_dm_failed_strafe_retries_other_side, dm_test_teardown},
		{"chase_timer", dm_test_setup, test_dm_chase_timer_decays, dm_test_teardown},
		{"revenge_counters", dm_test_setup, test_dm_revenge_counters_update_on_death, dm_test_teardown},
		{"velocity_tracking", dm_test_setup, test_dm_velocity_tracks_enemy_motion, dm_test_teardown},
		{"view_turn", dm_test_setup, test_dm_view_turn_uses_retail_acceleration_and_deceleration, dm_test_teardown},
		{"view_no_enemy_defaults", dm_test_setup, test_dm_view_turn_uses_retail_no_enemy_defaults, dm_test_teardown},
		{"view_snap", dm_test_setup, test_dm_view_turn_preserves_retail_accuracy_snap_threshold, dm_test_teardown},
		{"view_fraction", dm_test_setup, test_dm_view_turn_truncates_fractional_angle_difference, dm_test_teardown},
		{"aim_lead", dm_test_setup, test_dm_aim_uses_retail_muzzle_trace_and_linear_lead, dm_test_teardown},
		{"aim_trace_raise", dm_test_setup, test_dm_aim_preserves_trace_raise_and_skill_threshold, dm_test_teardown},
		{"aim_rocket_accuracy", dm_test_setup, test_dm_aim_applies_rocket_accuracy_square_root, dm_test_teardown},
		{"aim_rail_spread", dm_test_setup, test_dm_aim_applies_railgun_direction_noise_and_spread, dm_test_teardown},
		{"aim_radial_ground", dm_test_setup, test_dm_aim_radial_ground_target_uses_retail_trace_order, dm_test_teardown},
		{"aim_radial_gates", dm_test_setup, test_dm_aim_radial_ground_target_preserves_strict_gates, dm_test_teardown},
		{"aim_radial_exact_impacts", dm_test_setup, test_dm_aim_radial_ground_target_rejects_exact_impact_boundaries, dm_test_teardown},
		{"attack_aas_visibility", dm_test_setup, test_dm_check_attack_uses_aas_entity_visible, dm_test_teardown},
		{"attack_sweep", dm_test_setup, test_dm_check_attack_builds_retail_weapon_sweep, dm_test_teardown},
		{"attack_fov", dm_test_setup, test_dm_check_attack_preserves_distance_fov_boundary, dm_test_teardown},
		{"attack_safety", dm_test_setup, test_dm_check_attack_suppresses_teammate_and_radial_self_damage, dm_test_teardown},
		{"attack_window_release", dm_test_setup, test_dm_check_attack_verifies_window_and_release_latch, dm_test_teardown},
	};

	size_t count = sizeof(tests) / sizeof(tests[0]);
	for (size_t index = 0; index < count; ++index)
	{
		if (tests[index].setup != NULL)
		{
			DM_ASSERT(tests[index].setup() == 0);
		}

		if (tests[index].test != NULL)
		{
			tests[index].test();
		}

		if (tests[index].teardown != NULL)
		{
			tests[index].teardown();
		}
	}

	return EXIT_SUCCESS;
}
