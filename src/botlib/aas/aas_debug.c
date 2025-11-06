#include "aas_debug.h"

#include "botlib/common/l_log.h"
#include "aas_local.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AAS_DEBUG_MAX_PATH_DEPTH 128

/*
 * The original Gladiator binaries mirror Quake III's AAS area contents
 * constants.  Import the subset that drives the diagnostic strings so
 * the debug helpers can reproduce the historical wording when reporting
 * area characteristics.【F:dev_tools/Quake-III-Arena-master/code/bspc/aasfile.h†L67-L89】
 */
enum
{
    AAS_CONTENTS_WATER = 1,
    AAS_CONTENTS_LAVA = 2,
    AAS_CONTENTS_SLIME = 4,
    AAS_CONTENTS_CLUSTERPORTAL = 8,
    AAS_CONTENTS_TELEPORTAL = 16,
    AAS_CONTENTS_ROUTEPORTAL = 32,
    AAS_CONTENTS_TELEPORTER = 64,
    AAS_CONTENTS_JUMPPAD = 128,
    AAS_CONTENTS_DONOTENTER = 256,
    AAS_CONTENTS_VIEWPORTAL = 512,
    AAS_CONTENTS_MOVER = 1024,
    AAS_CONTENTS_NOTTEAM1 = 2048,
    AAS_CONTENTS_NOTTEAM2 = 4096,
};

static bool AAS_DebugWorldLoaded(const char *command)
{
    if (aasworld.loaded && aasworld.areas != NULL && aasworld.numAreas > 0)
    {
        return true;
    }

    BotLib_Print(PRT_WARNING,
                 "[aas_debug] %s: no AAS data is loaded.\n",
                 (command != NULL) ? command : "command");
    return false;
}

static bool AAS_DebugValidArea(int areanum)
{
    if (areanum <= 0 || areanum > aasworld.numAreas || aasworld.areas == NULL)
    {
        return false;
    }

    return true;
}

static const aas_area_t *AAS_DebugGetArea(int areanum)
{
    if (!AAS_DebugValidArea(areanum))
    {
        return NULL;
    }

    return &aasworld.areas[areanum];
}

static int AAS_DebugReachabilityFromArea(const aas_reachability_t *reach)
{
    if (reach == NULL)
    {
        return 0;
    }

    /*
     * The Gladiator HLIL stores the source area index in the reachability
     * struct alongside the target `areanum`. While the reverse engineered
     * loader has not reconstructed the supporting area setting tables yet, the
     * diagnostics extracted from the DLL relied on this convention. Treat the
     * `facenum` field as the originating area until the full graph loader is
     * recovered.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L43703-L43732】
     */
    return reach->facenum;
}

static size_t AAS_DebugListReachabilities(int fromArea)
{
    if (aasworld.reachability == NULL || aasworld.numReachability <= 0)
    {
        return 0U;
    }

    size_t count = 0U;
    for (int index = 0; index < aasworld.numReachability; ++index)
    {
        const aas_reachability_t *reach = &aasworld.reachability[index];
        if (AAS_DebugReachabilityFromArea(reach) != fromArea)
        {
            continue;
        }

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
        count += 1U;
    }

    if (count == 0U)
    {
        BotLib_Print(PRT_MESSAGE,
                     "    (no reachability links from area %d)\n",
                     fromArea);
    }

    return count;
}

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
        area->areanum <= aasworld.numAreaSettings)
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

        if (contents & AAS_CONTENTS_WATER)
        {
            BotLib_Print(PRT_MESSAGE, "water &");
            emitted = true;
        }
        if (contents & AAS_CONTENTS_LAVA)
        {
            BotLib_Print(PRT_MESSAGE, "lava &");
            emitted = true;
        }
        if (contents & AAS_CONTENTS_SLIME)
        {
            BotLib_Print(PRT_MESSAGE, "slime &");
            emitted = true;
        }
        if (contents & AAS_CONTENTS_JUMPPAD)
        {
            BotLib_Print(PRT_MESSAGE, "jump pad &");
            emitted = true;
        }
        if (contents & AAS_CONTENTS_CLUSTERPORTAL)
        {
            BotLib_Print(PRT_MESSAGE, "cluster portal &");
            emitted = true;
        }
        if (contents & AAS_CONTENTS_VIEWPORTAL)
        {
            BotLib_Print(PRT_MESSAGE, "view portal &");
            emitted = true;
        }
        if (contents & AAS_CONTENTS_DONOTENTER)
        {
            BotLib_Print(PRT_MESSAGE, "do not enter &");
            emitted = true;
        }
        if (contents & AAS_CONTENTS_MOVER)
        {
            BotLib_Print(PRT_MESSAGE, "mover &");
            emitted = true;
        }
        if (contents & AAS_CONTENTS_TELEPORTER)
        {
            BotLib_Print(PRT_MESSAGE, "teleporter &");
            emitted = true;
        }
        if (contents & AAS_CONTENTS_ROUTEPORTAL)
        {
            BotLib_Print(PRT_MESSAGE, "route portal &");
            emitted = true;
        }
        if (contents & AAS_CONTENTS_NOTTEAM1)
        {
            BotLib_Print(PRT_MESSAGE, "notteam1 &");
            emitted = true;
        }
        if (contents & AAS_CONTENTS_NOTTEAM2)
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

static int AAS_DebugFindAreaFromPoint(const vec3_t point)
{
    if (aasworld.areas == NULL || aasworld.numAreas <= 0)
    {
        return 0;
    }

    for (int areanum = 1; areanum <= aasworld.numAreas; ++areanum)
    {
        const aas_area_t *area = &aasworld.areas[areanum];
        if (point[0] < area->mins[0] || point[0] > area->maxs[0])
        {
            continue;
        }
        if (point[1] < area->mins[1] || point[1] > area->maxs[1])
        {
            continue;
        }
        if (point[2] < area->mins[2] || point[2] > area->maxs[2])
        {
            continue;
        }

        return area->areanum;
    }

    return 0;
}

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

    if (startArea == goalArea)
    {
        return true;
    }

    if (!AAS_DebugValidArea(startArea) || !AAS_DebugValidArea(goalArea))
    {
        return false;
    }

    int maxAreas = aasworld.numAreas + 1;
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

        for (int reachIndex = 0; reachIndex < aasworld.numReachability; ++reachIndex)
        {
            const aas_reachability_t *reach = &aasworld.reachability[reachIndex];
            if (AAS_DebugReachabilityFromArea(reach) != area)
            {
                continue;
            }

            int nextArea = reach->areanum;
            if (!AAS_DebugValidArea(nextArea))
            {
                continue;
            }

            if (visited[nextArea])
            {
                continue;
            }

            visited[nextArea] = 1U;
            previousArea[nextArea] = area;
            previousReach[nextArea] = reachIndex;
            queue[tail++] = nextArea;
            if (tail >= (size_t)maxAreas)
            {
                tail = (size_t)maxAreas - 1U;
            }
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

    if (pathSteps == 0U)
    {
        free(queue);
        free(previousArea);
        free(previousReach);
        free(visited);
        return false;
    }

    if (pathSteps > AAS_DEBUG_MAX_PATH_DEPTH)
    {
        pathSteps = AAS_DEBUG_MAX_PATH_DEPTH;
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
        int fromArea = AAS_DebugReachabilityFromArea(reach);
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
                     aasworld.numAreas);
        for (int areanum = 1; areanum <= aasworld.numAreas; ++areanum)
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
