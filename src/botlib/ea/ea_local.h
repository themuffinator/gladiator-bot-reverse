#ifndef GLADIATOR_BOTLIB_EA_LOCAL_H
#define GLADIATOR_BOTLIB_EA_LOCAL_H

#include <stdbool.h>

#include "q2bridge/botlib.h"
#include "shared/q_shared.h"

int EA_Init(int max_clients);
void EA_Shutdown(void);
bool EA_IsInitialised(void);

int EA_ResetClient(int client);
void EA_ResetInput(int client);
int EA_SubmitInput(int client, const bot_input_t *input);
int EA_GetInput(int client, float thinktime, bot_input_t *out_input);
int EA_EndRegular(int client, float thinktime);

void EA_Say(int client, const char *text);
void EA_SayTeam(int client, const char *text);
void EA_UseItem(int client, const char *item);
void EA_DropItem(int client, const char *item);
void EA_UseInv(int client, const char *inventory);
void EA_DropInv(int client, const char *inventory);
void EA_Gesture(int client, int gesture);
void EA_Command(int client, const char *command, ...);
void EA_ClearCommandBuffer(int client);

void EA_View(int client, const vec3_t viewangles);
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
void EA_MoveUp(int client);
void EA_MoveDown(int client);
void EA_MoveForward(int client);
void EA_MoveBack(int client);
void EA_MoveLeft(int client);
void EA_MoveRight(int client);

#endif /* GLADIATOR_BOTLIB_EA_LOCAL_H */
