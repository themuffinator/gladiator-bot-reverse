#include "aas_local.h"

#include <stdlib.h>
#include <string.h>

#include "botlib/common/l_log.h"
#include "botlib/common/l_libvar.h"
#include "botlib/common/l_memory.h"
#include "q2bridge/bridge_config.h"
#include "q2bridge/update_translator.h"

static void AAS_FrameRemoveLink(aas_link_t *link)
{
    if (link == NULL)
    {
        return;
    }

    if (aasworld.areaEntityLists == NULL)
    {
        return;
    }

    int areanum = link->areanum;
    if (areanum < 0)
    {
        return;
    }

    size_t index = (size_t)areanum;
    if (index >= aasworld.areaEntityListCount)
    {
        return;
    }

    if (link->prev_ent != NULL)
    {
        link->prev_ent->next_ent = link->next_ent;
    }
    else
    {
        aasworld.areaEntityLists[index] = link->next_ent;
    }

    if (link->next_ent != NULL)
    {
        link->next_ent->prev_ent = link->prev_ent;
    }
}

/*
=============
AAS_FrameUnlinkEntity

Release the invalid frame's pooled AAS entity links and occupancy state.
=============
*/
static void AAS_FrameUnlinkEntity(aas_entity_t *entity)
{
    if (entity == NULL)
    {
        return;
    }

    aas_link_t *link = entity->areas;
    while (link != NULL)
    {
        aas_link_t *next = link->next_area;
        AAS_FrameRemoveLink(link);
		AAS_FreeAASLink(link);
        link = next;
    }

    entity->areas = NULL;

    if (entity->areaOccupancyBits != NULL && entity->areaOccupancyWords > 0U)
    {
        memset(entity->areaOccupancyBits, 0, entity->areaOccupancyWords * sizeof(unsigned int));
    }

    entity->areaOccupancyCount = 0;
    entity->outsideAllAreas = qtrue;
    entity->lastOutsideUpdate = aasworld.time;
}

void AAS_UnlinkInvalidEntities(void)
{
    if (aasworld.entities == NULL || aasworld.maxEntities <= 0)
    {
        return;
    }

    for (int entnum = 0; entnum < aasworld.maxEntities; ++entnum)
    {
        aas_entity_t *entity = &aasworld.entities[entnum];
        if (!entity->inuse && (entity->areas != NULL || entity->leaves != NULL))
        {
            AAS_FrameUnlinkEntity(entity);
			AAS_UnlinkEntityFromBSPLeaves(entity);
        }
	}
}

/*
=============
AAS_ResetEntityLinks

Mirror retail's raw entity-table link reset without reclaiming either heap.
=============
*/
void AAS_ResetEntityLinks(void)
{
	if (aasworld.entities == NULL || aasworld.maxEntities <= 0)
	{
		return;
	}

	for (int entnum = 0; entnum < aasworld.maxEntities; ++entnum)
	{
		aas_entity_t *entity = &aasworld.entities[entnum];
		entity->areas = NULL;
		entity->leaves = NULL;
	}
}

void AAS_InvalidateEntities(void)
{
    if (aasworld.entities != NULL && aasworld.maxEntities > 0)
    {
        for (int entnum = 0; entnum < aasworld.maxEntities; ++entnum)
        {
            aas_entity_t *entity = &aasworld.entities[entnum];
            entity->number = entnum;
            entity->inuse = qfalse;
        }
    }

    aasworld.entitiesValid = qfalse;
}

/*
=============
AAS_ContinueInit

Finish reachability and clustering, then create retail routing-cache tables.
=============
*/
void AAS_ContinueInit(float time)
{
    (void)time;

    if (!aasworld.loaded)
    {
        return;
    }

    if (aasworld.initialized)
    {
        return;
    }

	if (aasworld.numReachabilityAreas == 0)
	{
		AAS_InitReachability();
	}
	if (AAS_ContinueInitReachability())
	{
		return;
	}

	AAS_InitClustering();
	libvar_t *forcewrite = Bridge_ForceWrite();
	int shouldwrite = aasworld.saveFile ||
		(forcewrite != NULL && forcewrite->value != 0.0f);
	/*
	 * 0x1000df7d gates the optimize+write block on savefile || forcewrite
	 * alone, with no test on the filename.  Retail never clears
	 * aasworld.filename - data_100667f0 has only the two writes in
	 * BotLibLoadMap's loose/pak branch - so a zip-sourced AAS reaches here
	 * with a stale or zero-filled name and AAS_WriteAASFile fails on it,
	 * printing "couldn't write %s".  Skipping the block would also skip
	 * AAS_Optimize, which swaps compacted vertex/edge/face arrays into
	 * aasworld and changes every later sample and route.
	 */
	if (shouldwrite)
	{
		if (LibVarValue("nooptimize", "0") == 0.0f)
		{
			AAS_Optimize();
		}

		if (AAS_WriteAASFile(aasworld.aasFilePath))
		{
			BotLib_Print(PRT_MESSAGE,
				"%s written succesfully\n",
				aasworld.aasFilePath);
		}
		else
		{
			BotLib_Print(PRT_ERROR,
				"couldn't write %s\n",
				aasworld.aasFilePath);
		}
	}

	/*
	 * 0x1000dfc9 and 0x1000dfce are unconditional: AAS_InitRouting then a tail
	 * call to AAS_SetInitialized.  The routine's only early exit is the
	 * AAS_ContinueInitReachability guard above, and retail references no error
	 * string here at all.
	 */
	(void)AAS_PrepareReachability();
	(void)AAS_InitRetailRoutingCaches();
	aasworld.initialized = qtrue;
	BotLib_Print(PRT_MESSAGE, "AAS initialized.\n");
}

void AAS_FrameSynchronise(float time)
{
    aasworld.time = time;
    TranslateEntity_SetCurrentTime(time);
}

/*
=============
AAS_RunFrameDiagnostics

Execute the retail one-shot AAS and memory diagnostic libvars.

Retail sub_1000e010 reads all three with the non-creating getter sub_10038990
(0x1000e044, 0x1000e076, 0x1000e0a7), which returns 0.0f without allocating
when the name is absent (0x1003899f). Only the write-back sub_10038ac0 on the
taken branch may create a record, so the diagnostics must never inflate the
block and byte counters they themselves print.
=============
*/
void AAS_RunFrameDiagnostics(void)
{
	if (LibVarGetValue("showcacheupdates") != 0.0f)
	{
		BotLib_Print(PRT_MESSAGE,
			"%d area cache updates\n",
			AAS_RetailAreaCacheUpdateCount());
		BotLib_Print(PRT_MESSAGE,
			"%d portal cache updates\n",
			AAS_RetailPortalCacheUpdateCount());
		LibVarSet("showcacheupdates", "0");
	}

	if (LibVarGetValue("showmemoryusage") != 0.0f)
	{
		BotLib_Print(PRT_MESSAGE,
			"total botlib memory: %d KB\n",
			(int)(BotMemory_TotalAllocated() >> 10));
		BotLib_Print(PRT_MESSAGE,
			"total memory blocks: %d\n",
			(int)BotMemory_BlockCount());
		LibVarSet("showmemoryusage", "0");
	}

	if (LibVarGetValue("memorydump") != 0.0f)
	{
		BotLib_Print(PRT_MESSAGE,
			"total botlib memory: %d KB\n",
			(int)(BotMemory_TotalAllocated() >> 10));
		BotLib_Print(PRT_MESSAGE,
			"total memory blocks: %d\n",
			(int)BotMemory_BlockCount());
		LibVarSet("memorydump", "0");
	}
}
