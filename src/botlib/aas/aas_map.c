#include "aas_map.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "aas_local.h"

/*
 * Global AAS world state.  The original DLL zeroed the data_100667e0 block
 * during shutdown; the struct layout mirrors that memory region.
 */
aas_world_t aasworld = {0};

static void AAS_ClearWorld(void)
{
    if (aasworld.entities != NULL)
    {
        free(aasworld.entities);
        aasworld.entities = NULL;
    }

    if (aasworld.areas != NULL)
    {
        free(aasworld.areas);
        aasworld.areas = NULL;
    }

    if (aasworld.nodes != NULL)
    {
        free(aasworld.nodes);
        aasworld.nodes = NULL;
    }

    memset(&aasworld, 0, sizeof(aasworld));
}

int AAS_LoadMap(const char *mapname,
                int modelindexes, char *modelindex[],
                int soundindexes, char *soundindex[],
                int imageindexes, char *imageindex[])
{
    (void)modelindexes;
    (void)modelindex;
    (void)soundindexes;
    (void)soundindex;
    (void)imageindexes;
    (void)imageindex;

    AAS_ClearWorld();

    if (mapname != NULL)
    {
        strncpy(aasworld.mapName, mapname, sizeof(aasworld.mapName) - 1U);
        aasworld.mapName[sizeof(aasworld.mapName) - 1U] = '\0';
    }

    /*
     * TODO: Load BSP + AAS headers, populate aasworld.areas/nodes/entities, and
     * propagate the checksum/time fields exactly as the 0x1000ecd0 routine did
     * when it filled the data_100667e0..data_100669a0 globals.
     */

    aasworld.loaded = qtrue;
    aasworld.initialized = qtrue;
    aasworld.entitiesValid = qfalse;
    aasworld.maxEntities = 0;
    return BLERR_NOERROR;
}

void AAS_Shutdown(void)
{
    /*
     * TODO: Mirror the shutdown cascade observed in sub_1000ee30: release the
     * reachability/routing caches and reset every field in aasworld before
     * logging the "AAS shutdown" banner.
     */

    AAS_ClearWorld();
}

static int AAS_EnsureEntityCapacity(int ent)
{
    if (ent < 0)
    {
        return BLERR_INVALIDENTITYNUMBER;
    }

    if (ent < aasworld.maxEntities)
    {
        return BLERR_NOERROR;
    }

    size_t previousCount = (size_t)aasworld.maxEntities;
    size_t requiredCount = (size_t)ent + 1U;
    size_t newSize = requiredCount * sizeof(aas_entity_t);

    aas_entity_t *resized = realloc(aasworld.entities, newSize);
    if (resized == NULL)
    {
        return BLERR_INVALIDENTITYNUMBER;
    }

    /* zero initialise the new tail */
    if (requiredCount > previousCount)
    {
        size_t delta = requiredCount - previousCount;
        memset(resized + previousCount, 0, delta * sizeof(aas_entity_t));
    }

    aasworld.entities = resized;
    aasworld.maxEntities = (int)requiredCount;
    return BLERR_NOERROR;
}

int AAS_UpdateEntity(int ent, bot_updateentity_t *state)
{
    if (!aasworld.loaded)
    {
        return BLERR_NOAASFILE;
    }

    if (state == NULL)
    {
        return BLERR_INVALIDENTITYNUMBER;
    }

    int ensureResult = AAS_EnsureEntityCapacity(ent);
    if (ensureResult != BLERR_NOERROR)
    {
        return ensureResult;
    }

    assert(aasworld.entities != NULL);
    aas_entity_t *entity = &aasworld.entities[ent];

    /* Preserve the historical fields before copying the new state. */
    VectorCopy(entity->origin, entity->previousOrigin);
    VectorCopy(state->angles, entity->angles);

    VectorCopy(state->origin, entity->origin);
    VectorCopy(state->old_origin, entity->old_origin);
    VectorCopy(state->mins, entity->mins);
    VectorCopy(state->maxs, entity->maxs);

    entity->number = ent;
    entity->inuse = qtrue;
    entity->solid = state->solid;
    entity->modelindex = state->modelindex;
    entity->modelindex2 = state->modelindex2;
    entity->modelindex3 = state->modelindex3;
    entity->modelindex4 = state->modelindex4;
    entity->frame = state->frame;
    entity->skinnum = state->skinnum;
    entity->effects = state->effects;
    entity->renderfx = state->renderfx;
    entity->sound = state->sound;
    entity->eventid = state->event;

    /*
     * TODO: Capture timing information (data_10062934/0x84) and route the
     * results through AAS link management once entity areas are implemented.
     */

    aasworld.entitiesValid = qtrue;
    return BLERR_NOERROR;
}
