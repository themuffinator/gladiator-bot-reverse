#include "aas_debug.h"

#include "botlib/common/l_log.h"
#include "aas_local.h"
#include "q2bridge/bridge.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AAS_DEBUG_MAX_PATH_DEPTH 128
#define AAS_DEBUG_MAX_LINES 256

static int g_aasDebugLines[AAS_DEBUG_MAX_LINES];
static int g_aasDebugLineVisible[AAS_DEBUG_MAX_LINES];
static int g_aasNumDebugLines;

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
