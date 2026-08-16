#ifndef GLADIATOR_BOTLIB_AAS_AAS_DEBUG_H
#define GLADIATOR_BOTLIB_AAS_AAS_DEBUG_H

#include <stddef.h>

#include "shared/q_shared.h"

#ifdef __cplusplus
extern "C" {
#endif

struct aas_reachability_s;

void AAS_ClearShownDebugLines(void);

void AAS_DebugLine(vec3_t start, vec3_t end, int color);

/*
 * The retail geometry visualizers from be_aas_debug.c.  Gladiator reaches them
 * through a string-keyed console dispatcher rather than direct calls, so most
 * carry no static caller in the DLL; AAS_ShowArea is the exception, called by
 * AAS_ShowReachability at 0x1000a5f2.
 */
void AAS_DrawPermanentCross(const vec3_t origin, float size, int color);

void AAS_DrawCross(const vec3_t origin, float size, int color);

void AAS_DrawPlaneCross(const vec3_t point,
	const vec3_t normal,
	float dist,
	int type,
	int color);

void AAS_ShowBoundingBox(const vec3_t origin, const vec3_t maxs, const vec3_t mins);

void AAS_ShowArea(int areanum, int groundfacesonly);

void AAS_ShowFace(int facenum);

void AAS_PrintTravelType(int traveltype);

void AAS_DrawArrow(const vec3_t start,
	const vec3_t end,
	int linecolor,
	int arrowcolor);

void AAS_ShowReachability(const struct aas_reachability_s *reach);

void AAS_ShowReachableAreas(int areanum);

void AAS_DebugBotTest(int entnum, const char *arguments, const vec3_t origin, const vec3_t angles);

void AAS_DebugShowPath(int startArea, int goalArea, const vec3_t start, const vec3_t goal);

void AAS_DebugShowAreas(const int *areas, size_t areaCount);

void AAS_DebugRegisterConsoleCommands(void);

void AAS_DebugUnregisterConsoleCommands(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* GLADIATOR_BOTLIB_AAS_AAS_DEBUG_H */
