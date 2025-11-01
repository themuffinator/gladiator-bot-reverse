#include "aas_local.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "../common/l_log.h"
#include "../common/l_libvar.h"
#include "../../q2bridge/bridge_config.h"

typedef struct
{
    int frames_with_work;
    int frames_skipped;
    bool force_reachability_active;
    bool force_clustering_active;
} aas_reachability_frame_state_t;

static aas_reachability_frame_state_t g_reach_frame_state;

static void AAS_FreeReverseReachability(void)
{
    if (aasworld.reversedReachability != NULL)
    {
        int areaCount = (aasworld.numAreas > 0) ? aasworld.numAreas : 0;
        for (int area = 0; area <= areaCount; ++area)
        {
            free(aasworld.reversedReachability[area].reachIndexes);
            aasworld.reversedReachability[area].reachIndexes = NULL;
            aasworld.reversedReachability[area].count = 0;
        }
        free(aasworld.reversedReachability);
        aasworld.reversedReachability = NULL;
    }
}

void AAS_ClearReachabilityData(void)
{
    AAS_FreeReverseReachability();
    free(aasworld.reachabilityFromArea);
    aasworld.reachabilityFromArea = NULL;
}

int AAS_PrepareReachability(void)
{
    AAS_ClearReachabilityData();

    if (aasworld.reachability == NULL || aasworld.numReachability <= 0)
    {
        return BLERR_NOERROR;
    }

    if (aasworld.areasettings == NULL || aasworld.numAreaSettings <= 0)
    {
        BotLib_Print(PRT_ERROR, "AAS_PrepareReachability: missing area settings\n");
        return BLERR_INVALIDIMPORT;
    }

    int numAreas = aasworld.numAreas;
    if (numAreas < 0)
    {
        numAreas = 0;
    }

    int numReach = aasworld.numReachability;
    aasworld.reachabilityFromArea = (int *)calloc((size_t)numReach, sizeof(int));
    if (aasworld.reachabilityFromArea == NULL)
    {
        return BLERR_INVALIDIMPORT;
    }

    aasworld.reversedReachability =
        (aas_reversedreachability_t *)calloc((size_t)numAreas + 1U, sizeof(aas_reversedreachability_t));
    if (aasworld.reversedReachability == NULL)
    {
        AAS_ClearReachabilityData();
        return BLERR_INVALIDIMPORT;
    }

    int *reverseCounts = (int *)calloc((size_t)numAreas + 1U, sizeof(int));
    if (reverseCounts == NULL)
    {
        AAS_ClearReachabilityData();
        return BLERR_INVALIDIMPORT;
    }

    for (int area = 1; area <= numAreas && area < aasworld.numAreaSettings; ++area)
    {
        const aas_areasettings_t *settings = &aasworld.areasettings[area];
        if (settings->numreachableareas <= 0)
        {
            continue;
        }

        int first = settings->firstreachablearea;
        int count = settings->numreachableareas;
        if (first < 0 || count < 0)
        {
            BotLib_Print(PRT_WARNING,
                         "AAS_PrepareReachability: area %d has negative reachability metadata\n",
                         area);
            continue;
        }

        if (first + count > numReach)
        {
            BotLib_Print(PRT_ERROR,
                         "AAS_PrepareReachability: area %d references reachabilities beyond file bounds\n",
                         area);
            free(reverseCounts);
            AAS_ClearReachabilityData();
            return BLERR_INVALIDIMPORT;
        }

        for (int offset = 0; offset < count; ++offset)
        {
            int reachIndex = first + offset;
            aasworld.reachabilityFromArea[reachIndex] = area;

            int destination = aasworld.reachability[reachIndex].areanum;
            if (destination < 0 || destination > numAreas)
            {
                continue;
            }

            reverseCounts[destination] += 1;
        }
    }

    for (int area = 0; area <= numAreas; ++area)
    {
        int count = reverseCounts[area];
        if (count <= 0)
        {
            continue;
        }

        aasworld.reversedReachability[area].reachIndexes = (int *)malloc((size_t)count * sizeof(int));
        if (aasworld.reversedReachability[area].reachIndexes == NULL)
        {
            free(reverseCounts);
            AAS_ClearReachabilityData();
            return BLERR_INVALIDIMPORT;
        }
        aasworld.reversedReachability[area].count = count;
    }

    int *reverseOffsets = (int *)calloc((size_t)numAreas + 1U, sizeof(int));
    if (reverseOffsets == NULL)
    {
        free(reverseCounts);
        AAS_ClearReachabilityData();
        return BLERR_INVALIDIMPORT;
    }

    for (int area = 1; area <= numAreas && area < aasworld.numAreaSettings; ++area)
    {
        const aas_areasettings_t *settings = &aasworld.areasettings[area];
        if (settings->numreachableareas <= 0)
        {
            continue;
        }

        int first = settings->firstreachablearea;
        int count = settings->numreachableareas;
        if (first < 0)
        {
            continue;
        }

        for (int offset = 0; offset < count; ++offset)
        {
            int reachIndex = first + offset;
            int destination = aasworld.reachability[reachIndex].areanum;
            if (destination < 0 || destination > numAreas)
            {
                continue;
            }

            int insert = reverseOffsets[destination];
            if (insert < aasworld.reversedReachability[destination].count)
            {
                aasworld.reversedReachability[destination].reachIndexes[insert] = reachIndex;
                reverseOffsets[destination] = insert + 1;
            }
        }
    }

    free(reverseOffsets);
    free(reverseCounts);
    return BLERR_NOERROR;
}

void AAS_ReachabilityFrameResetDiagnostics(void)
{
    memset(&g_reach_frame_state, 0, sizeof(g_reach_frame_state));
}

static bool AAS_ReachabilityLibVarEnabled(libvar_t *var)
{
    if (var == NULL)
    {
        return false;
    }

    return var->value != 0.0f;
}

void AAS_ReachabilityFrameUpdate(void)
{
    bool force_reach = AAS_ReachabilityLibVarEnabled(Bridge_ForceReachability());
    bool force_cluster = AAS_ReachabilityLibVarEnabled(Bridge_ForceClustering());

    g_reach_frame_state.force_reachability_active = force_reach;
    g_reach_frame_state.force_clustering_active = force_cluster;

    if (force_reach || force_cluster)
    {
        g_reach_frame_state.frames_with_work += 1;
    }
    else
    {
        g_reach_frame_state.frames_skipped += 1;
    }
}

int AAS_ReachabilityFrameWorkCounter(void)
{
    return g_reach_frame_state.frames_with_work;
}

int AAS_ReachabilityFrameSkipCounter(void)
{
    return g_reach_frame_state.frames_skipped;
}

bool AAS_ReachabilityForceReachabilityActive(void)
{
    return g_reach_frame_state.force_reachability_active;
}

bool AAS_ReachabilityForceClusteringActive(void)
{
    return g_reach_frame_state.force_clustering_active;
}
