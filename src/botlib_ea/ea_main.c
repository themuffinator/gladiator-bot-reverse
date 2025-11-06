#include "botlib_ea/ea_local.h"

#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "botlib_common/l_log.h"
#include "botlib_interface/botlib_interface.h"
#include "q2bridge/bridge.h"

#define EA_MAX_COMMANDS 8
#define EA_MAX_COMMAND_LENGTH 256

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct ea_client_state_s
{
    bot_input_t pending_input;
    bot_input_t last_output;
    bool has_pending_input;
    bool has_last_output;
    vec3_t viewangles;
    bool has_viewangles;
    char command_buffer[EA_MAX_COMMANDS][EA_MAX_COMMAND_LENGTH];
    size_t command_count;
} ea_client_state_t;

static ea_client_state_t *g_ea_clients = NULL;
static int g_ea_max_clients = 0;
static bool g_ea_initialised = false;

static void EA_ClearClientState(ea_client_state_t *state)
{
    if (state == NULL)
    {
        return;
    }

    memset(&state->pending_input, 0, sizeof(state->pending_input));
    memset(&state->last_output, 0, sizeof(state->last_output));
    state->has_pending_input = false;
    state->has_last_output = false;
    VectorClear(state->viewangles);
    state->has_viewangles = false;
    state->command_count = 0;
    for (size_t index = 0; index < EA_MAX_COMMANDS; ++index)
    {
        state->command_buffer[index][0] = '\0';
    }
}

static bool EA_ClientIndexValid(int client)
{
    return g_ea_initialised && client >= 0 && client < g_ea_max_clients;
}

static ea_client_state_t *EA_ClientState(int client)
{
    if (!EA_ClientIndexValid(client))
    {
        return NULL;
    }

    return &g_ea_clients[client];
}

static bot_input_t *EA_AccessPendingInput(ea_client_state_t *state)
{
    if (state == NULL)
    {
        return NULL;
    }

    if (!state->has_pending_input)
    {
        memset(&state->pending_input, 0, sizeof(state->pending_input));
        state->has_pending_input = true;
    }

    return &state->pending_input;
}

static void EA_NormaliseDirection(vec3_t out, const vec3_t direction)
{
    if (out == NULL || direction == NULL)
    {
        return;
    }

    float length = sqrtf(direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2]);
    if (length <= 0.0001f)
    {
        VectorClear(out);
        return;
    }

    float inv = 1.0f / length;
    out[0] = direction[0] * inv;
    out[1] = direction[1] * inv;
    out[2] = direction[2] * inv;
}

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
        else
        {
            result[PITCH] = 0.0f;
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

    result[ROLL] = 0.0f;
    VectorCopy(result, out);
}

int EA_Init(int max_clients)
{
    if (g_ea_initialised)
    {
        return BLERR_NOERROR;
    }

    if (max_clients <= 0)
    {
        BotLib_Print(PRT_ERROR, "EA_Init: invalid max_clients %d\n", max_clients);
        return BLERR_INVALIDIMPORT;
    }

    size_t allocation = (size_t)max_clients * sizeof(ea_client_state_t);
    g_ea_clients = (ea_client_state_t *)calloc((size_t)max_clients, sizeof(ea_client_state_t));
    if (g_ea_clients == NULL)
    {
        BotLib_Print(PRT_ERROR,
                     "EA_Init: failed to allocate %zu bytes for client contexts\n",
                     allocation);
        return BLERR_INVALIDIMPORT;
    }

    g_ea_max_clients = max_clients;
    for (int index = 0; index < g_ea_max_clients; ++index)
    {
        EA_ClearClientState(&g_ea_clients[index]);
    }

    g_ea_initialised = true;
    return BLERR_NOERROR;
}

void EA_Shutdown(void)
{
    if (!g_ea_initialised)
    {
        return;
    }

    free(g_ea_clients);
    g_ea_clients = NULL;
    g_ea_max_clients = 0;
    g_ea_initialised = false;
}

bool EA_IsInitialised(void)
{
    return g_ea_initialised;
}

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

    bot_input_t *pending = EA_AccessPendingInput(state);
    if (input != NULL)
    {
        *pending = *input;
    }
    else
    {
        memset(pending, 0, sizeof(*pending));
    }

    state->has_pending_input = true;
    return BLERR_NOERROR;
}

static void EA_DispatchCommandBuffer(int client, ea_client_state_t *state)
{
    if (state == NULL)
    {
        return;
    }

    for (size_t index = 0; index < state->command_count; ++index)
    {
        const char *command = state->command_buffer[index];
        if (command[0] != '\0')
        {
            Q2_BotClientCommand(client, "%s", command);
        }
        state->command_buffer[index][0] = '\0';
    }

    state->command_count = 0;
}

int EA_GetInput(int client, float thinktime, bot_input_t *out_input)
{
    if (!g_ea_initialised)
    {
        return BLERR_INVALIDIMPORT;
    }

    if (out_input == NULL)
    {
        return BLERR_INVALIDIMPORT;
    }

    ea_client_state_t *state = EA_ClientState(client);
    if (state == NULL)
    {
        return BLERR_INVALIDCLIENTNUMBER;
    }

    bot_input_t command = {0};
    if (state->has_pending_input)
    {
        command = state->pending_input;
    }

    if (state->has_viewangles)
    {
        VectorCopy(state->viewangles, command.viewangles);
    }

    command.thinktime = thinktime;
    *out_input = command;

    EA_DispatchCommandBuffer(client, state);

    state->last_output = *out_input;
    state->has_last_output = true;
    memset(&state->pending_input, 0, sizeof(state->pending_input));
    state->has_pending_input = false;
    state->has_viewangles = false;
    VectorClear(state->viewangles);
    return BLERR_NOERROR;
}

void EA_Command(int client, const char *fmt, ...)
{
    if (!g_ea_initialised || fmt == NULL)
    {
        return;
    }

    ea_client_state_t *state = EA_ClientState(client);
    if (state == NULL)
    {
        return;
    }

    if (state->command_count >= EA_MAX_COMMANDS)
    {
        return;
    }

    va_list args;
    va_start(args, fmt);

    char buffer[EA_MAX_COMMAND_LENGTH];
    int written = vsnprintf(buffer, sizeof(buffer), fmt, args);

    va_end(args);

    if (written < 0)
    {
        return;
    }

    buffer[EA_MAX_COMMAND_LENGTH - 1] = '\0';
    strncpy(state->command_buffer[state->command_count], buffer, EA_MAX_COMMAND_LENGTH);
    state->command_buffer[state->command_count][EA_MAX_COMMAND_LENGTH - 1] = '\0';
    state->command_count += 1U;
}

void EA_ClearCommandBuffer(int client)
{
    if (!g_ea_initialised)
    {
        return;
    }

    ea_client_state_t *state = EA_ClientState(client);
    if (state == NULL)
    {
        return;
    }

    for (size_t index = 0; index < EA_MAX_COMMANDS; ++index)
    {
        state->command_buffer[index][0] = '\0';
    }

    state->command_count = 0;
}

void EA_SetViewAngles(int client, const vec3_t viewangles)
{
    if (!g_ea_initialised || viewangles == NULL)
    {
        return;
    }

    ea_client_state_t *state = EA_ClientState(client);
    if (state == NULL)
    {
        return;
    }

    VectorCopy(viewangles, state->viewangles);
    state->has_viewangles = true;
}

void EA_ClearViewAngles(int client)
{
    if (!g_ea_initialised)
    {
        return;
    }

    ea_client_state_t *state = EA_ClientState(client);
    if (state == NULL)
    {
        return;
    }

    VectorClear(state->viewangles);
    state->has_viewangles = false;
}

void EA_LookAtPoint(int client, const vec3_t eye_position, const vec3_t target_position)
{
    if (!g_ea_initialised || eye_position == NULL || target_position == NULL)
    {
        return;
    }

    vec3_t direction;
    VectorSubtract(target_position, eye_position, direction);
    EA_VectorToAngles(direction, direction);
    EA_SetViewAngles(client, direction);
}

void EA_Move(int client, const vec3_t direction, float speed)
{
    if (!g_ea_initialised)
    {
        return;
    }

    ea_client_state_t *state = EA_ClientState(client);
    if (state == NULL)
    {
        return;
    }

    bot_input_t *input = EA_AccessPendingInput(state);
    if (input == NULL)
    {
        return;
    }

    vec3_t normalised;
    EA_NormaliseDirection(normalised, direction);
    VectorCopy(normalised, input->dir);
    input->speed = (speed > 0.0f) ? speed : 0.0f;
}

void EA_ClearMovement(int client)
{
    if (!g_ea_initialised)
    {
        return;
    }

    ea_client_state_t *state = EA_ClientState(client);
    if (state == NULL)
    {
        return;
    }

    bot_input_t *input = EA_AccessPendingInput(state);
    if (input == NULL)
    {
        return;
    }

    VectorClear(input->dir);
    input->speed = 0.0f;
}

void EA_SelectWeapon(int client, int weapon)
{
    if (!g_ea_initialised)
    {
        return;
    }

    ea_client_state_t *state = EA_ClientState(client);
    if (state == NULL)
    {
        return;
    }

    bot_input_t *input = EA_AccessPendingInput(state);
    if (input == NULL)
    {
        return;
    }

    input->weapon = weapon;
}

static void EA_SetActionFlag(int client, int action_flag)
{
    if (!g_ea_initialised)
    {
        return;
    }

    ea_client_state_t *state = EA_ClientState(client);
    if (state == NULL)
    {
        return;
    }

    bot_input_t *input = EA_AccessPendingInput(state);
    if (input == NULL)
    {
        return;
    }

    input->actionflags |= action_flag;
}

void EA_Attack(int client)
{
    EA_SetActionFlag(client, ACTION_ATTACK);
}

void EA_Use(int client)
{
    EA_SetActionFlag(client, ACTION_USE);
}

void EA_Respawn(int client)
{
    EA_SetActionFlag(client, ACTION_RESPAWN);
}

void EA_Jump(int client)
{
    EA_SetActionFlag(client, ACTION_JUMP);
}

void EA_DelayedJump(int client)
{
    EA_SetActionFlag(client, ACTION_DELAYEDJUMP);
}

void EA_Crouch(int client)
{
    EA_SetActionFlag(client, ACTION_CROUCH);
}
