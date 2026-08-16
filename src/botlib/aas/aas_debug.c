#include "aas_debug.h"

#include "botlib/common/l_libvar.h"
#include "botlib/common/l_log.h"
#include "aas_local.h"
#include "q2bridge/bridge.h"
#include "q2bridge/bridge_config.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AAS_DEBUG_MAX_PATH_DEPTH 128
#define AAS_DEBUG_MAX_LINES 256

/*
 * The shared LINECOLOR_* values are written as `L` suffixed literals, so on a
 * host with 64-bit long they widen to positive values that can never compare
 * equal to the sign-extended int a colour variable holds.  Retail keeps every
 * one of these in a 32-bit register, so fold them back to int here.
 */
#define AAS_DEBUG_COLOR(value) ((int)(int32_t)(value))

static int g_aasDebugLines[AAS_DEBUG_MAX_LINES];
static int g_aasDebugLineVisible[AAS_DEBUG_MAX_LINES];
static int g_aasNumDebugLines;

/*
=============
AAS_DebugCrossProduct

Compute the vector cross product used by the arrow head offset.
=============
*/
static void AAS_DebugCrossProduct(const vec3_t v1, const vec3_t v2, vec3_t result)
{
	result[0] = v1[1] * v2[2] - v1[2] * v2[1];
	result[1] = v1[2] * v2[0] - v1[0] * v2[2];
	result[2] = v1[0] * v2[1] - v1[1] * v2[0];
}

/*
=============
AAS_DebugVectorNormalize

Normalize a vector in place and return its original length.
=============
*/
static float AAS_DebugVectorNormalize(vec3_t vector)
{
	float length = sqrtf(DotProduct(vector, vector));
	if (length > 0.000001f)
	{
		VectorScale(vector, 1.0f / length, vector);
	}
	else
	{
		VectorClear(vector);
	}

	return length;
}

/*
=============
AAS_ClearShownDebugLines

Hide every lazily-created retail debug line and make its slot reusable.
=============
*/
void AAS_ClearShownDebugLines(void)
{
	for (int line = 0; line < AAS_DEBUG_MAX_LINES; ++line)
	{
		if (g_aasDebugLines[line] != 0)
		{
			Q2_DebugLineShow(g_aasDebugLines[line], NULL, NULL, LINECOLOR_NONE);
			g_aasDebugLineVisible[line] = qfalse;
		}
	}
}

/*
=============
AAS_DebugLine

Show one line through the retail shared 256-slot lazy handle pool.
=============
*/
void AAS_DebugLine(vec3_t start, vec3_t end, int color)
{
	for (int line = 0; line < AAS_DEBUG_MAX_LINES; ++line)
	{
		if (g_aasDebugLines[line] == 0)
		{
			g_aasDebugLines[line] = Q2_DebugLineCreate();
			g_aasDebugLineVisible[line] = qfalse;
			g_aasNumDebugLines += 1;
		}

		if (!g_aasDebugLineVisible[line])
		{
			Q2_DebugLineShow(g_aasDebugLines[line], start, end, color);
			g_aasDebugLineVisible[line] = qtrue;
			return;
		}
	}
}

/*
=============
AAS_DebugReserveLines

Reserve handles from the shared 256-slot pool without drawing yet.

AAS_DrawPlaneCross (0x10009a10) and AAS_ShowBoundingBox (0x10009cb0) collect
several handles up front and mark each visible at reservation time rather than
at show time, so they cannot go through AAS_DebugLine.  Both stop scanning at
the 256th slot and then show through whatever the array happens to hold, which
is why a short reservation is not an error here.
=============
*/
static int AAS_DebugReserveLines(int *lines, int count)
{
	int reserved = 0;
	for (int line = 0; reserved < count && line < AAS_DEBUG_MAX_LINES; ++line)
	{
		if (g_aasDebugLines[line] == 0)
		{
			g_aasDebugLines[line] = Q2_DebugLineCreate();
			lines[reserved++] = g_aasDebugLines[line];
			g_aasDebugLineVisible[line] = qtrue;
			g_aasNumDebugLines += 1;
		}
		else if (!g_aasDebugLineVisible[line])
		{
			lines[reserved++] = g_aasDebugLines[line];
			g_aasDebugLineVisible[line] = qtrue;
		}
	}

	return reserved;
}

/*
=============
AAS_DrawPermanentCross

Draw a three-axis cross and leak one extra handle per axis.

Retail 0x10009950 draws each arm through AAS_DebugLine and then creates a
second, never-tracked line for the same endpoints, so the cross survives the
next AAS_ClearShownDebugLines.  Retail returns the last DebugLineShow result
straight from the engine import; the reconstructed import is void, and the DLL
has no caller that reads the value, so this returns nothing.
=============
*/
void AAS_DrawPermanentCross(const vec3_t origin, float size, int color)
{
	for (int i = 0; i < 3; ++i)
	{
		vec3_t start;
		vec3_t end;
		VectorCopy(origin, start);
		start[i] += size;
		VectorCopy(origin, end);
		end[i] -= size;
		AAS_DebugLine(start, end, color);
		int debugline = Q2_DebugLineCreate();
		Q2_DebugLineShow(debugline, start, end, color);
	}
}

/*
=============
AAS_DrawCross

Draw a three-axis cross through the ordinary reusable line pool.
=============
*/
void AAS_DrawCross(const vec3_t origin, float size, int color)
{
	for (int i = 0; i < 3; ++i)
	{
		vec3_t start;
		vec3_t end;
		VectorCopy(origin, start);
		start[i] += size;
		VectorCopy(origin, end);
		end[i] -= size;
		AAS_DebugLine(start, end, color);
	}
}

/*
=============
AAS_DrawPlaneCross

Draw the retail plane-projected cross at a trace impact point.

Retail 0x10009a10 builds a twelve unit square around the point in the two
transverse axes, solves each corner back onto the plane along axis type % 3,
and draws the two diagonals.  It has no caller in the shipped DLL.
=============
*/
void AAS_DrawPlaneCross(const vec3_t point,
	const vec3_t normal,
	float dist,
	int type,
	int color)
{
	vec3_t start1;
	vec3_t end1;
	vec3_t start2;
	vec3_t end2;
	VectorCopy(point, start1);
	VectorCopy(point, end1);
	VectorCopy(point, start2);
	VectorCopy(point, end2);

	int n0 = type % 3;
	int n1 = (type + 1) % 3;
	int n2 = (type + 2) % 3;
	start1[n1] -= 6.0f;
	start1[n2] -= 6.0f;
	end1[n1] += 6.0f;
	end1[n2] += 6.0f;
	start2[n1] += 6.0f;
	start2[n2] -= 6.0f;
	end2[n1] -= 6.0f;
	end2[n2] += 6.0f;

	start1[n0] = (dist - (start1[n1] * normal[n1] + start1[n2] * normal[n2])) /
		normal[n0];
	end1[n0] = (dist - (end1[n1] * normal[n1] + end1[n2] * normal[n2])) /
		normal[n0];
	start2[n0] = (dist - (start2[n1] * normal[n1] + start2[n2] * normal[n2])) /
		normal[n0];
	end2[n0] = (dist - (end2[n1] * normal[n1] + end2[n2] * normal[n2])) /
		normal[n0];

	int lines[2] = {0, 0};
	(void)AAS_DebugReserveLines(lines, 2);
	Q2_DebugLineShow(lines[0], start1, end1, color);
	Q2_DebugLineShow(lines[1], start2, end2, color);
}

/*
=============
AAS_ShowBoundingBox

Draw a bounding box as a flickering wireframe cube.

Retail 0x10009cb0 takes the top corner offsets second and the bottom offsets
third - the reverse of the successor's order - and reserves only three handles
per face iteration, so the four passes overwrite each other and only the last
three edges stay on screen.  Both quirks are preserved.
=============
*/
void AAS_ShowBoundingBox(const vec3_t origin, const vec3_t maxs, const vec3_t mins)
{
	vec3_t corners[8];
	VectorAdd(origin, maxs, corners[0]);
	corners[1][0] = origin[0] + mins[0];
	corners[1][1] = origin[1] + maxs[1];
	corners[1][2] = origin[2] + maxs[2];
	corners[2][0] = origin[0] + mins[0];
	corners[2][1] = origin[1] + mins[1];
	corners[2][2] = origin[2] + maxs[2];
	corners[3][0] = origin[0] + maxs[0];
	corners[3][1] = origin[1] + mins[1];
	corners[3][2] = origin[2] + maxs[2];
	memcpy(corners[4], corners[0], sizeof(vec3_t) * 4U);
	for (int i = 0; i < 4; ++i)
	{
		corners[4 + i][2] = origin[2] + mins[2];
	}

	for (int i = 0; i < 4; ++i)
	{
		int lines[3] = {0, 0, 0};
		(void)AAS_DebugReserveLines(lines, 3);
		Q2_DebugLineShow(lines[0], corners[i], corners[(i + 1) & 3],
			AAS_DEBUG_COLOR(LINECOLOR_RED));
		Q2_DebugLineShow(lines[1], corners[4 + i], corners[4 + ((i + 1) & 3)],
			AAS_DEBUG_COLOR(LINECOLOR_RED));
		Q2_DebugLineShow(lines[2], corners[i], corners[4 + i], AAS_DEBUG_COLOR(LINECOLOR_RED));
	}
}

/*
=============
AAS_DebugNextEdgeColor

Advance the retail four-colour debug palette.

Retail spells the tail of this cycle as a neg/sbb/and/add sequence at
0x1000a264: neg leaves carry set unless the input was green, and sbb eax, eax
turns that into 0 or -1, selecting yellow for green and red for everything
else, including the zero the callers start from.
=============
*/
static int AAS_DebugNextEdgeColor(int color)
{
	if (color == AAS_DEBUG_COLOR(LINECOLOR_RED))
	{
		return AAS_DEBUG_COLOR(LINECOLOR_BLUE);
	}
	if (color == AAS_DEBUG_COLOR(LINECOLOR_BLUE))
	{
		return AAS_DEBUG_COLOR(LINECOLOR_GREEN);
	}
	if (color == AAS_DEBUG_COLOR(LINECOLOR_GREEN))
	{
		return AAS_DEBUG_COLOR(LINECOLOR_YELLOW);
	}

	return AAS_DEBUG_COLOR(LINECOLOR_RED);
}

/*
=============
AAS_ShowArea

Draw one area's edges, de-duplicated, cycling the retail debug palette.

Retail 0x1000a0a0 collects up to 256 distinct edge numbers over the area's
faces - restricted to ground and liquid faces when groundfacesonly is set -
and then draws each once.  groundfacesonly keeps only faces carrying the ground
or ladder bit (retail tests faceflags & 6).  Its range diagnostics for the face
and edge lumps are warnings only: retail prints and keeps indexing.
=============
*/
void AAS_ShowArea(int areanum, int groundfacesonly)
{
	int areaedges[AAS_DEBUG_MAX_LINES];
	int numareaedges = 0;
	int color = 0;

	if (areanum < 0 || areanum >= aasworld.numAreas)
	{
		BotLib_Print(PRT_ERROR, "area %d out of range [0, %d]\n",
			areanum, aasworld.numAreas);
		return;
	}

	const aas_area_t *area = &aasworld.areas[areanum];
	for (int i = 0; i < area->numfaces; ++i)
	{
		int facenum = abs(aasworld.faceIndex[area->firstface + i]);
		if (facenum >= aasworld.numFaces)
		{
			BotLib_Print(PRT_ERROR, "facenum %d out of range\n", facenum);
		}

		const aas_face_t *face = &aasworld.faces[facenum];
		if (groundfacesonly &&
			(face->faceflags & (AAS_FACE_GROUND | AAS_FACE_LADDER)) == 0)
		{
			continue;
		}

		for (int j = 0; j < face->numedges; ++j)
		{
			int edgenum = abs(aasworld.edgeIndex[face->firstedge + j]);
			if (edgenum >= aasworld.numEdges)
			{
				BotLib_Print(PRT_ERROR, "edgenum %d out of range\n", edgenum);
			}

			int n = 0;
			while (n < numareaedges && areaedges[n] != edgenum)
			{
				n += 1;
			}
			if (n == numareaedges && numareaedges < AAS_DEBUG_MAX_LINES)
			{
				areaedges[numareaedges++] = edgenum;
			}
		}
	}

	for (int n = 0; n < numareaedges; ++n)
	{
		const aas_edge_t *edge = &aasworld.edges[areaedges[n]];
		color = AAS_DebugNextEdgeColor(color);
		AAS_DebugLine(aasworld.vertexes[edge->v[0]],
			aasworld.vertexes[edge->v[1]],
			color);
	}
}

/*
=============
AAS_ShowFace

Draw one face's edges plus a stub of its plane normal.

Sibling of AAS_ShowArea that takes a face index; it starts the palette on
yellow and finishes with a twenty unit red normal from the first vertex.  It
has no caller in the shipped DLL.
=============
*/
void AAS_ShowFace(int facenum)
{
	int color = AAS_DEBUG_COLOR(LINECOLOR_YELLOW);

	if (facenum >= aasworld.numFaces)
	{
		BotLib_Print(PRT_ERROR, "facenum %d out of range\n", facenum);
	}

	const aas_face_t *face = &aasworld.faces[facenum];
	for (int i = 0; i < face->numedges; ++i)
	{
		int edgenum = abs(aasworld.edgeIndex[face->firstedge + i]);
		if (edgenum >= aasworld.numEdges)
		{
			BotLib_Print(PRT_ERROR, "edgenum %d out of range\n", edgenum);
		}

		const aas_edge_t *edge = &aasworld.edges[edgenum];
		if (color == AAS_DEBUG_COLOR(LINECOLOR_RED))
		{
			color = AAS_DEBUG_COLOR(LINECOLOR_GREEN);
		}
		else if (color == AAS_DEBUG_COLOR(LINECOLOR_GREEN))
		{
			color = AAS_DEBUG_COLOR(LINECOLOR_BLUE);
		}
		else if (color == AAS_DEBUG_COLOR(LINECOLOR_BLUE))
		{
			color = AAS_DEBUG_COLOR(LINECOLOR_YELLOW);
		}
		else
		{
			color = AAS_DEBUG_COLOR(LINECOLOR_RED);
		}

		AAS_DebugLine(aasworld.vertexes[edge->v[0]],
			aasworld.vertexes[edge->v[1]],
			color);
	}

	const aas_plane_t *plane = &aasworld.planes[face->planenum];
	int edgenum = abs(aasworld.edgeIndex[face->firstedge]);
	const aas_edge_t *edge = &aasworld.edges[edgenum];
	vec3_t start;
	vec3_t end;
	VectorCopy(aasworld.vertexes[edge->v[0]], start);
	VectorMA(start, 20.0f, plane->normal, end);
	AAS_DebugLine(start, end, AAS_DEBUG_COLOR(LINECOLOR_RED));
}

/*
=============
AAS_PrintTravelType

Retail 0x1000a400 returns immediately; the travel-type names never shipped.
=============
*/
void AAS_PrintTravelType(int traveltype)
{
	(void)traveltype;
}

/*
=============
AAS_DrawArrow

Draw a shaft plus two head strokes from start to end.

The head offset is normalize(end - start) crossed with world up, falling back
to the x axis once the two are within 0.99 of parallel.
=============
*/
void AAS_DrawArrow(const vec3_t start, const vec3_t end, int linecolor, int arrowcolor)
{
	vec3_t up = {0.0f, 0.0f, 1.0f};
	vec3_t dir;
	vec3_t cross;
	vec3_t p1;
	vec3_t p2;
	vec3_t shaftstart;
	vec3_t shaftend;

	VectorCopy(start, shaftstart);
	VectorCopy(end, shaftend);
	VectorSubtract(end, start, dir);
	AAS_DebugVectorNormalize(dir);
	float dot = DotProduct(dir, up);
	if (dot > 0.99f || dot < -0.99f)
	{
		VectorSet(cross, 1.0f, 0.0f, 0.0f);
	}
	else
	{
		AAS_DebugCrossProduct(dir, up, cross);
	}

	VectorMA(end, -6.0f, dir, p1);
	VectorCopy(p1, p2);
	VectorMA(p1, 6.0f, cross, p1);
	VectorMA(p2, -6.0f, cross, p2);
	AAS_DebugLine(shaftstart, shaftend, linecolor);
	AAS_DebugLine(p1, shaftend, arrowcolor);
	AAS_DebugLine(p2, shaftend, arrowcolor);
}

/*
=============
AAS_ShowReachability

Visualise one reachability: its area, a start-to-end arrow, and for the
airborne travel types the predicted client movement.

Retail 0x1000a5e0 reads sv_jumpvel straight out of the libvar without the
positive-value fallback the generator applies, drives the jump prediction from
a zero velocity plus a command vector, and adds the run-up cross for
TRAVEL_JUMP alone.  Its prediction passes visualize as one, which is what
draws the arc.
=============
*/
void AAS_ShowReachability(const aas_reachability_t *reach)
{
	if (reach == NULL)
	{
		return;
	}

	AAS_ShowArea(reach->areanum, 1);
	AAS_DrawArrow(reach->start, reach->end, AAS_DEBUG_COLOR(LINECOLOR_BLUE), AAS_DEBUG_COLOR(LINECOLOR_YELLOW));

	const libvar_t *jumpvel = Bridge_JumpVelocity();
	float speed = 0.0f;
	vec3_t dir;
	vec3_t cmdmove;
	aas_clientmove_t move;

	if (reach->traveltype == TRAVEL_JUMP ||
		reach->traveltype == TRAVEL_WALKOFFLEDGE)
	{
		float jumpvelocity = (jumpvel != NULL) ? jumpvel->value : 0.0f;
		AAS_HorizontalVelocityForJump(jumpvelocity,
			reach->start,
			reach->end,
			&speed);
		dir[0] = reach->end[0] - reach->start[0];
		dir[1] = reach->end[1] - reach->start[1];
		dir[2] = 0.0f;
		AAS_DebugVectorNormalize(dir);
		VectorScale(dir, speed, cmdmove);
		cmdmove[2] = jumpvelocity;

		vec3_t velocity = {0.0f, 0.0f, 0.0f};
		AAS_PredictClientMovement(&move,
			-1,
			reach->start,
			PRESENCE_NORMAL,
			qtrue,
			velocity,
			cmdmove,
			3,
			30,
			0.1f,
			SE_HITGROUND | SE_ENTERWATER | SE_ENTERSLIME | SE_ENTERLAVA |
				SE_HITGROUNDDAMAGE,
			0,
			qtrue);

		if (reach->traveltype == TRAVEL_JUMP)
		{
			AAS_JumpReachRunStart(reach, dir);
			AAS_DrawCross(dir, 4.0f, AAS_DEBUG_COLOR(LINECOLOR_BLUE));
		}
	}
	else if (reach->traveltype == TRAVEL_ROCKETJUMP)
	{
		float zvel = AAS_RocketJumpZVelocity(reach->start);
		AAS_HorizontalVelocityForJump(zvel, reach->start, reach->end, &speed);
		dir[0] = reach->end[0] - reach->start[0];
		dir[1] = reach->end[1] - reach->start[1];
		dir[2] = 0.0f;
		AAS_DebugVectorNormalize(dir);
		VectorScale(dir, speed, cmdmove);

		vec3_t velocity = {0.0f, 0.0f, zvel};
		AAS_PredictClientMovement(&move,
			-1,
			reach->start,
			PRESENCE_NORMAL,
			qtrue,
			velocity,
			cmdmove,
			3,
			30,
			0.1f,
			SE_HITGROUND | SE_ENTERWATER | SE_ENTERSLIME | SE_ENTERLAVA |
				SE_HITGROUNDDAMAGE,
			0,
			qtrue);
	}
}

/*
=============
AAS_ShowReachableAreas

Step through one area's reachabilities, one entry every 1.5 seconds.

Retail 0x1000a810 keeps the copied record, the last area, the cursor, and the
clock in file statics, restarts the cursor when the area changes, and shows
the retained copy on every call even between advances.
=============
*/
void AAS_ShowReachableAreas(int areanum)
{
	static aas_reachability_t showreach_reach;
	static int showreach_lastareanum;
	static int showreach_index;
	static float showreach_lasttime;

	if (areanum != showreach_lastareanum)
	{
		showreach_index = 0;
		showreach_lastareanum = areanum;
	}

	const aas_areasettings_t *settings = &aasworld.areasettings[areanum];
	int numreach = settings->numreachableareas;
	if (numreach == 0)
	{
		return;
	}
	if (showreach_index >= numreach)
	{
		showreach_index = 0;
	}

	if (AAS_Time() - showreach_lasttime > 1.5f)
	{
		showreach_reach =
			aasworld.reachability[settings->firstreachablearea + showreach_index];
		showreach_index += 1;
		showreach_lasttime = AAS_Time();
		AAS_PrintTravelType(showreach_reach.traveltype);
		BotLib_Print(PRT_MESSAGE, "\n");
	}

	AAS_ShowReachability(&showreach_reach);
}

/*
=============
AAS_DebugWorldLoaded

Return whether debug commands have a loaded world with at least one real area.
=============
*/
static bool AAS_DebugWorldLoaded(const char *command)
{
	if (aasworld.loaded && aasworld.areas != NULL && aasworld.numAreas > 1)
    {
        return true;
    }

    BotLib_Print(PRT_WARNING,
                 "[aas_debug] %s: no AAS data is loaded.\n",
                 (command != NULL) ? command : "command");
    return false;
}

/*
=============
AAS_DebugValidArea

Validate a one-based real area index against the area lump count.
=============
*/
static bool AAS_DebugValidArea(int areanum)
{
	return aasworld.areas != NULL &&
		   areanum > 0 &&
		   areanum < aasworld.numAreas;
}

/*
=============
AAS_DebugGetArea

Return a valid area record from the loaded one-based area table.
=============
*/
static const aas_area_t *AAS_DebugGetArea(int areanum)
{
    if (!AAS_DebugValidArea(areanum))
    {
        return NULL;
    }

    return &aasworld.areas[areanum];
}

/*
=============
AAS_DebugReachabilityRange

Resolve the contiguous outgoing reachability span stored in an area's settings.
=============
*/
static bool AAS_DebugReachabilityRange(int fromArea, int *first, int *count)
{
	if (first == NULL || count == NULL)
	{
		return false;
	}

	*first = 0;
	*count = 0;
	if (!AAS_DebugValidArea(fromArea) ||
		aasworld.areasettings == NULL ||
		fromArea >= aasworld.numAreaSettings)
	{
		return false;
	}

	const aas_areasettings_t *settings = &aasworld.areasettings[fromArea];
	if (settings->firstreachablearea < 0 || settings->numreachableareas < 0)
	{
		return false;
	}

	if (settings->numreachableareas > 0 &&
		(settings->firstreachablearea == 0 ||
		 settings->firstreachablearea >= aasworld.numReachability ||
		 settings->numreachableareas >
			 aasworld.numReachability - settings->firstreachablearea))
	{
		return false;
	}

	*first = settings->firstreachablearea;
	*count = settings->numreachableareas;
	return true;
}

/*
=============
AAS_DebugReachabilityFromArea

Resolve a reachability's source from prepared metadata or area-setting spans.
=============
*/
static int AAS_DebugReachabilityFromArea(int reachIndex)
{
	if (reachIndex < 0 || reachIndex >= aasworld.numReachability)
	{
		return 0;
	}

	if (aasworld.reachabilityFromArea != NULL)
	{
		int fromArea = aasworld.reachabilityFromArea[reachIndex];
		if (AAS_DebugValidArea(fromArea))
		{
			return fromArea;
		}
	}

	int maxArea = aasworld.numAreas;
	if (aasworld.numAreaSettings < maxArea)
	{
		maxArea = aasworld.numAreaSettings;
	}
	for (int fromArea = 1; fromArea < maxArea; ++fromArea)
	{
		int first = 0;
		int count = 0;
		if (!AAS_DebugReachabilityRange(fromArea, &first, &count))
		{
			continue;
		}
		if (reachIndex >= first && reachIndex < first + count)
		{
			return fromArea;
		}
	}

	return 0;
}

/*
=============
AAS_DebugNextReachabilityFromArea

Iterate one area's outgoing reachabilities using retail area-setting ranges.
=============
*/
static int AAS_DebugNextReachabilityFromArea(int fromArea, int previousIndex)
{
	int first = 0;
	int count = 0;
	if (!AAS_DebugReachabilityRange(fromArea, &first, &count))
	{
		return -1;
	}

	int nextIndex = previousIndex < first ? first : previousIndex + 1;
	if (nextIndex >= first && nextIndex < first + count)
	{
		return nextIndex;
	}

	return -1;
}

/*
=============
AAS_DebugListReachabilities

Print every outgoing reachability stored for a source area.
=============
*/
static size_t AAS_DebugListReachabilities(int fromArea)
{
	size_t listed = 0U;
	if (aasworld.reachability != NULL && aasworld.numReachability > 0)
	{
		for (int index = AAS_DebugNextReachabilityFromArea(fromArea, -1);
			 index >= 0;
			 index = AAS_DebugNextReachabilityFromArea(fromArea, index))
		{
			const aas_reachability_t *reach = &aasworld.reachability[index];
			BotLib_Print(PRT_MESSAGE,
						 "    reach[%d]: %d -> %d travel=%d time=%u start=(%.2f %.2f %.2f) end=(%.2f %.2f %.2f)\n",
						 index,
						 fromArea,
						 reach->areanum,
						 reach->traveltype,
						 (unsigned int)reach->traveltime,
						 reach->start[0],
						 reach->start[1],
						 reach->start[2],
						 reach->end[0],
						 reach->end[1],
						 reach->end[2]);
			listed += 1U;
		}
	}

	if (listed == 0U)
	{
		BotLib_Print(PRT_MESSAGE,
					 "    (no reachability links from area %d)\n",
					 fromArea);
	}

	return listed;
}

/*
=============
AAS_DebugDescribeArea

Print geometry, routing metadata, and content flags for one loaded area.
=============
*/
static void AAS_DebugDescribeArea(const aas_area_t *area)
{
	if (area == NULL)
	{
        return;
    }

    BotLib_Print(PRT_MESSAGE,
                 "area %d: faces=%d firstface=%d center=(%.2f %.2f %.2f) mins=(%.2f %.2f %.2f) maxs=(%.2f %.2f %.2f)\n",
                 area->areanum,
                 area->numfaces,
                 area->firstface,
                 area->center[0],
                 area->center[1],
                 area->center[2],
                 area->mins[0],
                 area->mins[1],
                 area->mins[2],
                 area->maxs[0],
                 area->maxs[1],
                 area->maxs[2]);

    if (aasworld.areasettings != NULL &&
        area->areanum > 0 &&
		area->areanum < aasworld.numAreaSettings)
    {
        const aas_areasettings_t *settings = &aasworld.areasettings[area->areanum];
        BotLib_Print(PRT_MESSAGE,
                     "  cluster=%d presencetype=%d reachable=%d firstreachable=%d\n",
                     settings->cluster,
                     settings->presencetype,
                     settings->numreachableareas,
                     settings->firstreachablearea);

        BotLib_Print(PRT_MESSAGE, "  area contents: ");
        int contents = settings->contents;
        bool emitted = false;

        if (contents & AAS_AREACONTENTS_WATER)
        {
            BotLib_Print(PRT_MESSAGE, "water &");
            emitted = true;
        }
        if (contents & AAS_AREACONTENTS_LAVA)
        {
            BotLib_Print(PRT_MESSAGE, "lava &");
            emitted = true;
        }
        if (contents & AAS_AREACONTENTS_SLIME)
        {
            BotLib_Print(PRT_MESSAGE, "slime &");
            emitted = true;
        }
        if (contents & AAS_AREACONTENTS_JUMPPAD)
        {
            BotLib_Print(PRT_MESSAGE, "jump pad &");
            emitted = true;
        }
        if (contents & AAS_AREACONTENTS_CLUSTERPORTAL)
        {
            BotLib_Print(PRT_MESSAGE, "cluster portal &");
            emitted = true;
        }
        if (contents & AAS_AREACONTENTS_VIEWPORTAL)
        {
            BotLib_Print(PRT_MESSAGE, "view portal &");
            emitted = true;
        }
        if (contents & AAS_AREACONTENTS_DONOTENTER)
        {
            BotLib_Print(PRT_MESSAGE, "do not enter &");
            emitted = true;
        }
        if (contents & AAS_AREACONTENTS_MOVER)
        {
            BotLib_Print(PRT_MESSAGE, "mover &");
            emitted = true;
        }
        if (contents & AAS_AREACONTENTS_TELEPORTER)
        {
            BotLib_Print(PRT_MESSAGE, "teleporter &");
            emitted = true;
        }
        if (contents & AAS_AREACONTENTS_ROUTEPORTAL)
        {
            BotLib_Print(PRT_MESSAGE, "route portal &");
            emitted = true;
        }
        if (contents & AAS_AREACONTENTS_NOTTEAM1)
        {
            BotLib_Print(PRT_MESSAGE, "notteam1 &");
            emitted = true;
        }
        if (contents & AAS_AREACONTENTS_NOTTEAM2)
        {
            BotLib_Print(PRT_MESSAGE, "notteam2 &");
            emitted = true;
        }

        if (!emitted)
        {
            BotLib_Print(PRT_MESSAGE, "empty");
        }

        BotLib_Print(PRT_MESSAGE, "\n");
    }
}

/*
=============
AAS_DebugFindAreaFromPoint

Resolve a debug point through the same AAS node walk used by runtime queries.
=============
*/
static int AAS_DebugFindAreaFromPoint(const vec3_t point)
{
	if (point == NULL)
    {
        return 0;
    }

	return AAS_PointAreaNum(point);
}

/*
=============
AAS_DebugBuildPath

Build a breadth-first debug path over each area's outgoing reachability span.
=============
*/
static bool AAS_DebugBuildPath(int startArea,
                               int goalArea,
                               int **outReachIndices,
                               size_t *outCount)
{
    if (outReachIndices == NULL || outCount == NULL)
    {
        return false;
    }

    *outReachIndices = NULL;
    *outCount = 0U;

	if (!AAS_DebugValidArea(startArea) || !AAS_DebugValidArea(goalArea))
	{
		return false;
	}

    if (startArea == goalArea)
    {
        return true;
    }

	if (aasworld.reachability == NULL || aasworld.numReachability <= 0)
    {
        return false;
    }

	int maxAreas = aasworld.numAreas;
    int *queue = (int *)calloc((size_t)maxAreas, sizeof(int));
    int *previousArea = (int *)calloc((size_t)maxAreas, sizeof(int));
    int *previousReach = (int *)calloc((size_t)maxAreas, sizeof(int));
    unsigned char *visited = (unsigned char *)calloc((size_t)maxAreas, sizeof(unsigned char));
    if (queue == NULL || previousArea == NULL || previousReach == NULL || visited == NULL)
    {
        free(queue);
        free(previousArea);
        free(previousReach);
        free(visited);
        return false;
    }

    for (int index = 0; index < maxAreas; ++index)
    {
        previousArea[index] = -1;
        previousReach[index] = -1;
    }

    size_t head = 0U;
    size_t tail = 0U;
    queue[tail++] = startArea;
    visited[startArea] = 1U;
    previousArea[startArea] = startArea;

    bool found = false;
    while (head < tail)
    {
        int area = queue[head++];
        if (area == goalArea)
        {
            found = true;
            break;
        }

		for (int reachIndex = AAS_DebugNextReachabilityFromArea(area, -1);
			 reachIndex >= 0;
			 reachIndex = AAS_DebugNextReachabilityFromArea(area, reachIndex))
        {
            const aas_reachability_t *reach = &aasworld.reachability[reachIndex];
            int nextArea = reach->areanum;
			if (!AAS_DebugValidArea(nextArea) || visited[nextArea])
            {
                continue;
            }

			if (tail >= (size_t)maxAreas)
            {
                continue;
            }

            visited[nextArea] = 1U;
            previousArea[nextArea] = area;
            previousReach[nextArea] = reachIndex;
            queue[tail++] = nextArea;
        }
    }

    if (!found)
    {
        free(queue);
        free(previousArea);
        free(previousReach);
        free(visited);
        return false;
    }

    size_t pathSteps = 0U;
    for (int area = goalArea; area != startArea; area = previousArea[area])
    {
        if (area <= 0 || area >= maxAreas || previousArea[area] < 0)
        {
            pathSteps = 0U;
            break;
        }
        pathSteps += 1U;
    }

	if (pathSteps == 0U || pathSteps > AAS_DEBUG_MAX_PATH_DEPTH)
    {
        free(queue);
        free(previousArea);
        free(previousReach);
        free(visited);
        return false;
    }

    int *indices = (int *)calloc(pathSteps, sizeof(int));
    if (indices == NULL)
    {
        free(queue);
        free(previousArea);
        free(previousReach);
        free(visited);
        return false;
    }

    size_t cursor = pathSteps;
    int current = goalArea;
    while (current != startArea && cursor > 0U)
    {
        int reachIndex = previousReach[current];
        if (reachIndex < 0)
        {
            break;
        }

        indices[--cursor] = reachIndex;
        current = previousArea[current];
        if (current <= 0 || current >= maxAreas)
        {
            break;
        }
    }

    if (cursor != 0U)
    {
        free(indices);
        free(queue);
        free(previousArea);
        free(previousReach);
        free(visited);
        return false;
    }

    *outReachIndices = indices;
    *outCount = pathSteps;

    free(queue);
    free(previousArea);
    free(previousReach);
    free(visited);
    return true;
}

/*
=============
AAS_DebugBotTest

Describe the requested area or the area containing the supplied origin.
=============
*/
void AAS_DebugBotTest(int entnum, const char *arguments, const vec3_t origin, const vec3_t angles)
{
    if (!AAS_DebugWorldLoaded("bot_test"))
    {
        return;
    }

    BotLib_Print(PRT_MESSAGE,
                 "bot_test entity %d origin=(%.2f %.2f %.2f) angles=(%.2f %.2f %.2f)\n",
                 entnum,
                 origin[0],
                 origin[1],
                 origin[2],
                 angles[0],
                 angles[1],
                 angles[2]);

    int requestedArea = 0;
    if (arguments != NULL && *arguments != '\0')
    {
        requestedArea = (int)strtol(arguments, NULL, 10);
    }

    if (!AAS_DebugValidArea(requestedArea))
    {
        requestedArea = AAS_DebugFindAreaFromPoint(origin);
        if (!AAS_DebugValidArea(requestedArea))
        {
            BotLib_Print(PRT_WARNING,
                         "bot_test: origin is outside all areas\n");
            return;
        }
    }

    const aas_area_t *area = AAS_DebugGetArea(requestedArea);
    if (area == NULL)
    {
        BotLib_Print(PRT_WARNING,
                     "bot_test: area %d is invalid\n",
                     requestedArea);
        return;
    }

    AAS_DebugDescribeArea(area);
    AAS_DebugListReachabilities(area->areanum);
}

/*
=============
AAS_DebugShowPath

Print a reachability path between two requested or point-resolved areas.
=============
*/
void AAS_DebugShowPath(int startArea, int goalArea, const vec3_t start, const vec3_t goal)
{
    if (!AAS_DebugWorldLoaded("aas_showpath"))
    {
        return;
    }

    if (!AAS_DebugValidArea(startArea))
    {
        startArea = AAS_DebugFindAreaFromPoint(start);
    }

    if (!AAS_DebugValidArea(goalArea))
    {
        goalArea = AAS_DebugFindAreaFromPoint(goal);
    }

    BotLib_Print(PRT_MESSAGE,
                 "aas_showpath start=%d goal=%d\n",
                 startArea,
                 goalArea);

    if (!AAS_DebugValidArea(startArea) || !AAS_DebugValidArea(goalArea))
    {
        BotLib_Print(PRT_WARNING,
                     "aas_showpath: invalid start (%d) or goal (%d) area\n",
                     startArea,
                     goalArea);
        return;
    }

    if (startArea == goalArea)
    {
        BotLib_Print(PRT_MESSAGE,
                     "  start and goal refer to the same area; no traversal required.\n");
        return;
    }

    int *pathIndices = NULL;
    size_t pathCount = 0U;
    if (!AAS_DebugBuildPath(startArea, goalArea, &pathIndices, &pathCount) ||
        pathIndices == NULL || pathCount == 0U)
    {
        BotLib_Print(PRT_WARNING,
                     "[aas_debug] aas_showpath: no path found from %d to %d\n",
                     startArea,
                     goalArea);
        free(pathIndices);
        return;
    }

    unsigned int totalTime = 0U;
    for (size_t step = 0; step < pathCount; ++step)
    {
        int reachIndex = pathIndices[step];
        if (reachIndex < 0 || reachIndex >= aasworld.numReachability)
        {
            continue;
        }

        const aas_reachability_t *reach = &aasworld.reachability[reachIndex];
        totalTime += (unsigned int)reach->traveltime;
		int fromArea = AAS_DebugReachabilityFromArea(reachIndex);
        BotLib_Print(PRT_MESSAGE,
                     "  step %zu: %d -> %d travel=%d time=%u start=(%.2f %.2f %.2f) end=(%.2f %.2f %.2f)\n",
                     step,
                     fromArea,
                     reach->areanum,
                     reach->traveltype,
                     (unsigned int)reach->traveltime,
                     reach->start[0],
                     reach->start[1],
                     reach->start[2],
                     reach->end[0],
                     reach->end[1],
                     reach->end[2]);
    }

    BotLib_Print(PRT_MESSAGE,
                 "  total steps=%zu total_traveltime=%u\n",
                 pathCount,
                 totalTime);

    free(pathIndices);
}

/*
=============
AAS_DebugShowAreas

Describe selected areas or every real area in the loaded AAS world.
=============
*/
void AAS_DebugShowAreas(const int *areas, size_t areaCount)
{
    if (!AAS_DebugWorldLoaded("aas_showareas"))
    {
        return;
    }

    if (areas == NULL || areaCount == 0U)
    {
        BotLib_Print(PRT_MESSAGE,
                     "aas_showareas: dumping all %d areas\n",
					 aasworld.numAreas - 1);
		for (int areanum = 1; areanum < aasworld.numAreas; ++areanum)
        {
            const aas_area_t *area = AAS_DebugGetArea(areanum);
            AAS_DebugDescribeArea(area);
            AAS_DebugListReachabilities(areanum);
        }
        return;
    }

    BotLib_Print(PRT_MESSAGE,
                 "aas_showareas: listing %zu areas\n",
                 areaCount);

    for (size_t index = 0; index < areaCount; ++index)
    {
        int areanum = areas[index];
        if (!AAS_DebugValidArea(areanum))
        {
            BotLib_Print(PRT_WARNING,
                         "  area %d is outside the loaded set\n",
                         areanum);
            continue;
        }

        const aas_area_t *area = AAS_DebugGetArea(areanum);
        AAS_DebugDescribeArea(area);
        AAS_DebugListReachabilities(areanum);
    }
}
