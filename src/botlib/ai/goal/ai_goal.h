#pragma once

#include <stdbool.h>

#include "bot_goal.h"

#ifdef __cplusplus
extern "C" {
#endif

bool AI_Goal_Init(void);
void AI_Goal_Shutdown(void);
void AI_Goal_BeginFrame(float time);

int AI_Goal_AllocState(int client);
void AI_Goal_FreeState(int handle);
int AI_Goal_LoadItemWeights(int goalstate, const char *filename);
void AI_Goal_SetClient(int goalstate, int client);

#ifdef __cplusplus
} /* extern "C" */
#endif

