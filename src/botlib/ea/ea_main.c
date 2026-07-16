#include "botlib/ea/ea_local.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "botlib/common/l_log.h"
#include "botlib/common/l_memory.h"
#include "q2bridge/bridge.h"

#define EA_MAX_COMMAND_LENGTH 256
#define EA_MAX_COMMAND_ARGUMENTS 9
#define EA_MAX_USERMOVE 565.0f
#define ACTION_JUMPEDLASTFRAME ACTION_MOVELEFT

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct ea_client_state_s
{
	bot_input_t input;
} ea_client_state_t;

static ea_client_state_t *g_ea_clients = NULL;
static int g_ea_client_slots = 0;
static bool g_ea_initialised = false;

/*
=============
EA_ClearClientState

Clears all persistent elementary-action state for one client.
=============
*/
static void EA_ClearClientState(ea_client_state_t *state)
{
	if (state == NULL)
	{
		return;
	}

	memset(state, 0, sizeof(*state));
}

/*
=============
EA_ClientIndexValid

Checks the UB-safe client range reserved by setup.
=============
*/
static bool EA_ClientIndexValid(int client)
{
	return g_ea_initialised && client >= 0 && client < g_ea_client_slots;
}

/*
=============
EA_ClientState

Returns a client input record when the client index is valid.
=============
*/
static ea_client_state_t *EA_ClientState(int client)
{
	if (!EA_ClientIndexValid(client))
	{
		return NULL;
	}

	return &g_ea_clients[client];
}

/*
=============
EA_VectorToAngles

Converts a look direction into the elementary-action view representation.
=============
*/
static void EA_VectorToAngles(const vec3_t value, vec3_t out)
{
	if (value == NULL || out == NULL)
	{
		return;
	}

	vec3_t result = {0.0f, 0.0f, 0.0f};
	if (value[0] == 0.0f && value[1] == 0.0f)
	{
		result[YAW] = 0.0f;
		if (value[2] > 0.0f)
		{
			result[PITCH] = -90.0f;
		}
		else if (value[2] < 0.0f)
		{
			result[PITCH] = 90.0f;
		}
	}
	else
	{
		result[YAW] = atan2f(value[1], value[0]) * (180.0f / (float)M_PI);
		if (result[YAW] < 0.0f)
		{
			result[YAW] += 360.0f;
		}

		float forward = sqrtf(value[0] * value[0] + value[1] * value[1]);
		result[PITCH] = atan2f(-value[2], forward) * (180.0f / (float)M_PI);
	}

	VectorCopy(result, out);
}

/*
=============
EA_ResetInputState

Applies Gladiator's post-output reset while retaining view and weapon state.
=============
*/
static void EA_ResetInputState(ea_client_state_t *state)
{
	if (state == NULL)
	{
		return;
	}

	int jumped = state->input.actionflags & ACTION_JUMP;
	state->input.thinktime = 0.0f;
	VectorClear(state->input.dir);
	state->input.speed = 0.0f;
	state->input.actionflags = 0;
	if (jumped != 0)
	{
		state->input.actionflags = ACTION_JUMPEDLASTFRAME;
	}
}

/*
=============
EA_PrepareInputState

Clears the hidden previous-jump latch and applies this frame's think time.
=============
*/
static void EA_PrepareInputState(ea_client_state_t *state, float thinktime)
{
	state->input.actionflags &= ~ACTION_JUMPEDLASTFRAME;
	state->input.thinktime = thinktime;
}

/*
=============
EA_Init

Allocates cleared elementary-action records through the botlib allocator.
=============
*/
int EA_Init(int max_clients)
{
	if (g_ea_initialised)
	{
		return BLERR_NOERROR;
	}

	if (max_clients < 0)
	{
		return BLERR_INVALIDIMPORT;
	}
	if (max_clients > MAX_CLIENTS)
	{
		max_clients = MAX_CLIENTS;
	}

	/* Preserve the retail inclusive endpoint without reproducing its OOB access. */
	size_t client_slots = (size_t)max_clients + 1U;
	g_ea_clients = (ea_client_state_t *)GetClearedMemory(
		client_slots * sizeof(*g_ea_clients));
	if (g_ea_clients == NULL)
	{
		return BLERR_INVALIDIMPORT;
	}

	g_ea_client_slots = (int)client_slots;
	g_ea_initialised = true;
	return BLERR_NOERROR;
}

/*
=============
EA_Shutdown

Releases the elementary-action records and clears lifecycle state.
=============
*/
void EA_Shutdown(void)
{
	FreeMemory(g_ea_clients);
	g_ea_clients = NULL;
	g_ea_client_slots = 0;
	g_ea_initialised = false;
}

/*
=============
EA_IsInitialised

Reports whether elementary-action storage is available.
=============
*/
bool EA_IsInitialised(void)
{
	return g_ea_initialised;
}

/*
=============
EA_ResetClient

Clears every elementary-action field for a client lifecycle reset.
=============
*/
int EA_ResetClient(int client)
{
	if (!g_ea_initialised)
	{
		return BLERR_INVALIDIMPORT;
	}

	ea_client_state_t *state = EA_ClientState(client);
	if (state == NULL)
	{
		return BLERR_INVALIDCLIENTNUMBER;
	}

	EA_ClearClientState(state);
	return BLERR_NOERROR;
}

/*
=============
EA_ResetInput

Applies the retail per-frame reset to one client's pending input.
=============
*/
void EA_ResetInput(int client)
{
	EA_ResetInputState(EA_ClientState(client));
}

/*
=============
EA_SubmitInput

Replaces a client's pending input for the reconstruction orchestrator.
=============
*/
int EA_SubmitInput(int client, const bot_input_t *input)
{
	if (!g_ea_initialised)
	{
		return BLERR_INVALIDIMPORT;
	}

	ea_client_state_t *state = EA_ClientState(client);
	if (state == NULL)
	{
		return BLERR_INVALIDCLIENTNUMBER;
	}

	if (input != NULL)
	{
		state->input = *input;
	}
	else
	{
		EA_ClearClientState(state);
	}

	return BLERR_NOERROR;
}

/*
=============
EA_GetInput

Finalises one frame of input and applies Gladiator's post-output reset.
=============
*/
int EA_GetInput(int client, float thinktime, bot_input_t *out_input)
{
	if (!g_ea_initialised || out_input == NULL)
	{
		return BLERR_INVALIDIMPORT;
	}

	ea_client_state_t *state = EA_ClientState(client);
	if (state == NULL)
	{
		return BLERR_INVALIDCLIENTNUMBER;
	}

	EA_PrepareInputState(state, thinktime);
	*out_input = state->input;
	EA_ResetInputState(state);
	return BLERR_NOERROR;
}

/*
=============
EA_EndRegular

Dispatches the live retail input record, then applies its post-import reset.
=============
*/
int EA_EndRegular(int client, float thinktime)
{
	if (!g_ea_initialised)
	{
		return BLERR_INVALIDIMPORT;
	}

	ea_client_state_t *state = EA_ClientState(client);
	if (state == NULL)
	{
		return BLERR_INVALIDCLIENTNUMBER;
	}

	bot_import_extended_t *imports = Q2Bridge_GetImportTable();
	if (imports == NULL || imports->BotInput == NULL)
	{
		return BLERR_INVALIDIMPORT;
	}

	EA_PrepareInputState(state, thinktime);
	imports->BotInput(client, &state->input);
	EA_ResetInputState(state);
	return BLERR_NOERROR;
}

/*
=============
EA_ImportedCommand

Calls the retail BotClientCommand import with one argument and a NULL sentinel.
=============
*/
static void EA_ImportedCommand(int client, const char *command, const char *argument)
{
	if (!EA_ClientIndexValid(client) || command == NULL)
	{
		return;
	}

	bot_import_extended_t *imports = Q2Bridge_GetImportTable();
	if (imports == NULL || imports->BotClientCommand == NULL)
	{
		return;
	}

	imports->BotClientCommand(client, (char *)command, (char *)argument, NULL);
}

/*
=============
EA_Say

Dispatches the retail say command and its single text argument.
=============
*/
void EA_Say(int client, const char *text)
{
	EA_ImportedCommand(client, "say", text);
}

/*
=============
EA_SayTeam

Dispatches the retail say_team command and its single text argument.
=============
*/
void EA_SayTeam(int client, const char *text)
{
	EA_ImportedCommand(client, "say_team", text);
}

/*
=============
EA_UseItem

Dispatches the retail use command and its single item argument.
=============
*/
void EA_UseItem(int client, const char *item)
{
	EA_ImportedCommand(client, "use", item);
}

/*
=============
EA_DropItem

Dispatches the retail drop command and its single item argument.
=============
*/
void EA_DropItem(int client, const char *item)
{
	EA_ImportedCommand(client, "drop", item);
}

/*
=============
EA_UseInv

Dispatches the retail invuse command and its single inventory argument.
=============
*/
void EA_UseInv(int client, const char *inventory)
{
	EA_ImportedCommand(client, "invuse", inventory);
}

/*
=============
EA_DropInv

Dispatches the retail invdrop command and its single inventory argument.
=============
*/
void EA_DropInv(int client, const char *inventory)
{
	EA_ImportedCommand(client, "invdrop", inventory);
}

/*
=============
EA_Gesture

Dispatches the retail wave command with a signed decimal gesture argument.
=============
*/
void EA_Gesture(int client, int gesture)
{
	char argument[128];
	int written = snprintf(argument, sizeof(argument), "%d", gesture);
	if (written < 0)
	{
		return;
	}

	argument[sizeof(argument) - 1U] = '\0';
	EA_ImportedCommand(client, "wave", argument);
}

/*
=============
EA_DispatchGenericCommand

Forwards the nine captured argument slots and the retail trailing NULL.
=============
*/
static void EA_DispatchGenericCommand(int client,
	const char *command,
	char *arguments[EA_MAX_COMMAND_ARGUMENTS])
{
	bot_import_extended_t *imports = Q2Bridge_GetImportTable();
	if (imports == NULL || imports->BotClientCommand == NULL)
	{
		return;
	}

	imports->BotClientCommand(client,
		(char *)command,
		arguments[0],
		arguments[1],
		arguments[2],
		arguments[3],
		arguments[4],
		arguments[5],
		arguments[6],
		arguments[7],
		arguments[8],
		NULL);
}

/*
=============
EA_CompatibilityStringFormat

Recognises the reconstruction's proven single trailing %s call convention.
=============
*/
static bool EA_CompatibilityStringFormat(const char *format, size_t *token_length)
{
	const char *conversion = strstr(format, "%s");
	if (conversion == NULL || conversion[2] != '\0')
	{
		return false;
	}

	for (const char *cursor = format; cursor < conversion; ++cursor)
	{
		if (*cursor == '%')
		{
			return false;
		}
	}

	size_t length = (size_t)(conversion - format);
	while (length > 0U && isspace((unsigned char)format[length - 1U]))
	{
		length -= 1U;
	}
	*token_length = length;
	return true;
}

/*
=============
EA_Command

Dispatches retail token arguments while adapting proven single-%s callers.
=============
*/
void EA_Command(int client, const char *command, ...)
{
	if (!EA_ClientIndexValid(client) || command == NULL)
	{
		return;
	}

	char *arguments[EA_MAX_COMMAND_ARGUMENTS] = {0};
	va_list args;
	va_start(args, command);

	size_t token_length = 0U;
	if (EA_CompatibilityStringFormat(command, &token_length))
	{
		const char *value = va_arg(args, const char *);
		va_end(args);
		if (value == NULL)
		{
			return;
		}

		if (token_length == 0U)
		{
			EA_DispatchGenericCommand(client, value, arguments);
			return;
		}
		if (token_length >= EA_MAX_COMMAND_LENGTH)
		{
			return;
		}

		char compatibility_token[EA_MAX_COMMAND_LENGTH];
		memcpy(compatibility_token, command, token_length);
		compatibility_token[token_length] = '\0';
		arguments[0] = (char *)value;
		EA_DispatchGenericCommand(client, compatibility_token, arguments);
		return;
	}

	for (size_t index = 0U; index < EA_MAX_COMMAND_ARGUMENTS; ++index)
	{
		arguments[index] = va_arg(args, char *);
		if (arguments[index] == NULL)
		{
			break;
		}
		if (index + 1U == EA_MAX_COMMAND_ARGUMENTS)
		{
			BotLib_Print(PRT_ERROR, "EA_Command: too many arguments");
		}
	}
	va_end(args);

	EA_DispatchGenericCommand(client, command, arguments);
}

/*
=============
EA_ClearCommandBuffer

Retains the compatibility no-op now that retail commands dispatch immediately.
=============
*/
void EA_ClearCommandBuffer(int client)
{
	(void)client;
}

/*
=============
EA_View

Copies view angles into the persistent retail input record.
=============
*/
void EA_View(int client, const vec3_t viewangles)
{
	ea_client_state_t *state = EA_ClientState(client);
	if (state == NULL || viewangles == NULL)
	{
		return;
	}

	VectorCopy(viewangles, state->input.viewangles);
}

/*
=============
EA_SetViewAngles

Provides the reconstruction alias for the retail EA_View action.
=============
*/
void EA_SetViewAngles(int client, const vec3_t viewangles)
{
	EA_View(client, viewangles);
}

/*
=============
EA_ClearViewAngles

Clears a client's persistent view angles.
=============
*/
void EA_ClearViewAngles(int client)
{
	ea_client_state_t *state = EA_ClientState(client);
	if (state == NULL)
	{
		return;
	}

	VectorClear(state->input.viewangles);
}

/*
=============
EA_LookAtPoint

Builds persistent view angles aimed from an eye point to a target point.
=============
*/
void EA_LookAtPoint(int client, const vec3_t eye_position, const vec3_t target_position)
{
	if (eye_position == NULL || target_position == NULL)
	{
		return;
	}

	vec3_t direction;
	VectorSubtract(target_position, eye_position, direction);
	EA_VectorToAngles(direction, direction);
	EA_View(client, direction);
}

/*
=============
EA_Move

Copies the requested direction and clamps speed to Gladiator's 565-unit limit.
=============
*/
void EA_Move(int client, const vec3_t direction, float speed)
{
	ea_client_state_t *state = EA_ClientState(client);
	if (state == NULL || direction == NULL)
	{
		return;
	}

	VectorCopy(direction, state->input.dir);
	if (speed > EA_MAX_USERMOVE)
	{
		speed = EA_MAX_USERMOVE;
	}
	else if (speed < -EA_MAX_USERMOVE)
	{
		speed = -EA_MAX_USERMOVE;
	}
	state->input.speed = speed;
}

/*
=============
EA_ClearMovement

Clears the directional portion of a client's pending input.
=============
*/
void EA_ClearMovement(int client)
{
	ea_client_state_t *state = EA_ClientState(client);
	if (state == NULL)
	{
		return;
	}

	VectorClear(state->input.dir);
	state->input.speed = 0.0f;
}

/*
=============
EA_SelectWeapon

Stores the successor weapon extension without disturbing the retail prefix.
=============
*/
void EA_SelectWeapon(int client, int weapon)
{
	ea_client_state_t *state = EA_ClientState(client);
	if (state == NULL)
	{
		return;
	}

	state->input.weapon = weapon;
}

/*
=============
EA_SetActionFlag

Adds a simple action bit to one client's pending input.
=============
*/
static void EA_SetActionFlag(int client, int action_flag)
{
	ea_client_state_t *state = EA_ClientState(client);
	if (state == NULL)
	{
		return;
	}

	state->input.actionflags |= action_flag;
}

/*
=============
EA_Attack

Submits the retail attack action.
=============
*/
void EA_Attack(int client)
{
	EA_SetActionFlag(client, ACTION_ATTACK);
}

/*
=============
EA_Use

Submits the retail use action.
=============
*/
void EA_Use(int client)
{
	EA_SetActionFlag(client, ACTION_USE);
}

/*
=============
EA_Respawn

Submits the retail respawn action.
=============
*/
void EA_Respawn(int client)
{
	EA_SetActionFlag(client, ACTION_RESPAWN);
}

/*
=============
EA_Jump

Submits jump on alternating frames using Gladiator's hidden latch bit.
=============
*/
void EA_Jump(int client)
{
	ea_client_state_t *state = EA_ClientState(client);
	if (state == NULL)
	{
		return;
	}

	if ((state->input.actionflags & ACTION_JUMPEDLASTFRAME) != 0)
	{
		state->input.actionflags &= ~ACTION_JUMP;
	}
	else
	{
		state->input.actionflags |= ACTION_JUMP;
	}
}

/*
=============
EA_DelayedJump

Submits delayed jump unless the immediate-jump latch is active.
=============
*/
void EA_DelayedJump(int client)
{
	ea_client_state_t *state = EA_ClientState(client);
	if (state == NULL)
	{
		return;
	}

	if ((state->input.actionflags & ACTION_JUMPEDLASTFRAME) != 0)
	{
		state->input.actionflags &= ~ACTION_DELAYEDJUMP;
	}
	else
	{
		state->input.actionflags |= ACTION_DELAYEDJUMP;
	}
}

/*
=============
EA_Crouch

Submits the retail crouch action.
=============
*/
void EA_Crouch(int client)
{
	EA_SetActionFlag(client, ACTION_CROUCH);
}

/*
=============
EA_MoveUp

Submits the retail upward movement action.
=============
*/
void EA_MoveUp(int client)
{
	EA_SetActionFlag(client, ACTION_MOVEUP);
}

/*
=============
EA_MoveDown

Submits the retail downward movement action.
=============
*/
void EA_MoveDown(int client)
{
	EA_SetActionFlag(client, ACTION_MOVEDOWN);
}

/*
=============
EA_MoveForward

Submits the retail forward movement action.
=============
*/
void EA_MoveForward(int client)
{
	EA_SetActionFlag(client, ACTION_MOVEFORWARD);
}

/*
=============
EA_MoveBack

Submits the retail backward movement action.
=============
*/
void EA_MoveBack(int client)
{
	EA_SetActionFlag(client, ACTION_MOVEBACK);
}

/*
=============
EA_MoveLeft

Submits the retail left movement action bit.
=============
*/
void EA_MoveLeft(int client)
{
	EA_SetActionFlag(client, ACTION_MOVELEFT);
}

/*
=============
EA_MoveRight

Submits the retail right movement action.
=============
*/
void EA_MoveRight(int client)
{
	EA_SetActionFlag(client, ACTION_MOVERIGHT);
}
