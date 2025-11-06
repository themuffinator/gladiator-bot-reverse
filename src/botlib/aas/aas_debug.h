#ifndef GLADIATOR_BOTLIB_AAS_AAS_DEBUG_H
#define GLADIATOR_BOTLIB_AAS_AAS_DEBUG_H

#include <stddef.h>

#include "shared/q_shared.h"

#ifdef __cplusplus
extern "C" {
#endif

void AAS_DebugBotTest(int entnum, const char *arguments, const vec3_t origin, const vec3_t angles);

void AAS_DebugShowPath(int startArea, int goalArea, const vec3_t start, const vec3_t goal);

void AAS_DebugShowAreas(const int *areas, size_t areaCount);

void AAS_DebugRegisterConsoleCommands(void);

void AAS_DebugUnregisterConsoleCommands(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* GLADIATOR_BOTLIB_AAS_AAS_DEBUG_H */
