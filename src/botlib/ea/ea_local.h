#ifndef GLADIATOR_BOTLIB_EA_LOCAL_H
#define GLADIATOR_BOTLIB_EA_LOCAL_H

#include <stdbool.h>

#include "shared/q_shared.h"
#include "q2bridge/botlib.h"

int EA_Init(int max_clients);
void EA_Shutdown(void);
bool EA_IsInitialised(void);

int EA_ResetClient(int client);
int EA_SubmitInput(int client, const bot_input_t *input);
int EA_GetInput(int client, float thinktime, bot_input_t *out_input);

void EA_Command(int client, const char *fmt, ...);
void EA_ClearCommandBuffer(int client);

void EA_SetViewAngles(int client, const vec3_t viewangles);
void EA_ClearViewAngles(int client);
void EA_LookAtPoint(int client, const vec3_t eye_position, const vec3_t target_position);

void EA_Move(int client, const vec3_t direction, float speed);
void EA_ClearMovement(int client);
void EA_SelectWeapon(int client, int weapon);

void EA_Attack(int client);
void EA_Use(int client);
void EA_Respawn(int client);
void EA_Jump(int client);
void EA_DelayedJump(int client);
void EA_Crouch(int client);

#endif /* GLADIATOR_BOTLIB_EA_LOCAL_H */
