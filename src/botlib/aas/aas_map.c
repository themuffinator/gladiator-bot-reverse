#include "aas_map.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "aas_local.h"

static void AAS_UnlinkEntityFromAreas(aas_entity_t *entity);
static int AAS_LinkEntityToComputedAreas(aas_entity_t *entity, const vec3_t absmins, const vec3_t absmaxs);
static void AAS_ResetEntityBitset(aas_entity_t *entity);
static int AAS_PrepareEntityBitset(aas_entity_t *entity);
static int AAS_EnsureAreaListArray(void);
static size_t AAS_AreaBitWordCount(void);
static void AAS_ClampMinsMaxs(vec3_t mins, vec3_t maxs);

/*
 * Global AAS world state.  The original DLL zeroed the data_100667e0 block
 * during shutdown; the struct layout mirrors that memory region.
 */
aas_world_t aasworld = {0};

static size_t AAS_AreaBitWordCount(void)
{
    int numAreas = aasworld.numAreas;
    if (numAreas < 0)
    {
        numAreas = 0;
    }

    size_t totalBits = (size_t)numAreas + 1U;
    if (totalBits == 0U)
    {
        return 0U;
    }

    return (totalBits + 31U) / 32U;
}

static void AAS_ClearWorld(void)
{
    if (aasworld.entities != NULL)
    {
        for (int i = 0; i < aasworld.maxEntities; ++i)
        {
            AAS_UnlinkEntityFromAreas(&aasworld.entities[i]);
            if (aasworld.entities[i].areaOccupancyBits != NULL)
            {
                free(aasworld.entities[i].areaOccupancyBits);
                aasworld.entities[i].areaOccupancyBits = NULL;
                aasworld.entities[i].areaOccupancyWords = 0U;
            }
            aasworld.entities[i].areaOccupancyCount = 0;
        }

        free(aasworld.entities);
        aasworld.entities = NULL;
    }

    if (aasworld.areaEntityLists != NULL)
    {
        free(aasworld.areaEntityLists);
        aasworld.areaEntityLists = NULL;
        aasworld.areaEntityListCount = 0U;
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
    int areaStatus = AAS_EnsureAreaListArray();
    if (areaStatus != BLERR_NOERROR)
    {
        return areaStatus;
    }
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

static void AAS_ResetEntityBitset(aas_entity_t *entity)
{
    if (entity->areaOccupancyBits != NULL && entity->areaOccupancyWords > 0U)
    {
        memset(entity->areaOccupancyBits, 0, entity->areaOccupancyWords * sizeof(unsigned int));
    }

    entity->areaOccupancyCount = 0;
}

static int AAS_PrepareEntityBitset(aas_entity_t *entity)
{
    size_t requiredWords = AAS_AreaBitWordCount();
    if (requiredWords == 0U)
    {
        if (entity->areaOccupancyBits != NULL)
        {
            free(entity->areaOccupancyBits);
            entity->areaOccupancyBits = NULL;
        }

        entity->areaOccupancyWords = 0U;
        AAS_ResetEntityBitset(entity);
        return BLERR_NOERROR;
    }

    if (entity->areaOccupancyWords != requiredWords)
    {
        unsigned int *bits = (unsigned int *)realloc(entity->areaOccupancyBits,
                                                     requiredWords * sizeof(unsigned int));
        if (bits == NULL)
        {
            return BLERR_INVALIDENTITYNUMBER;
        }

        entity->areaOccupancyBits = bits;
        entity->areaOccupancyWords = requiredWords;
    }

    memset(entity->areaOccupancyBits, 0, requiredWords * sizeof(unsigned int));
    entity->areaOccupancyCount = 0;
    return BLERR_NOERROR;
}

static void AAS_SetEntityAreaBit(aas_entity_t *entity, int areanum)
{
    if (entity->areaOccupancyBits == NULL || entity->areaOccupancyWords == 0U)
    {
        return;
    }

    if (areanum < 0)
    {
        return;
    }

    size_t bitIndex = (size_t)areanum;
    size_t wordIndex = bitIndex / 32U;
    size_t bitOffset = bitIndex % 32U;

    if (wordIndex < entity->areaOccupancyWords)
    {
        entity->areaOccupancyBits[wordIndex] |= (1U << bitOffset);
    }
}

static int AAS_EnsureAreaListArray(void)
{
    size_t desired = (size_t)aasworld.numAreas + 1U;
    if (desired == 0U)
    {
        desired = 1U;
    }

    if (aasworld.areaEntityLists != NULL && aasworld.areaEntityListCount == desired)
    {
        return BLERR_NOERROR;
    }

    if (aasworld.areaEntityLists != NULL)
    {
        free(aasworld.areaEntityLists);
        aasworld.areaEntityLists = NULL;
        aasworld.areaEntityListCount = 0U;
    }

    aasworld.areaEntityLists = (aas_link_t **)calloc(desired, sizeof(aas_link_t *));
    if (aasworld.areaEntityLists == NULL)
    {
        return BLERR_INVALIDENTITYNUMBER;
    }

    aasworld.areaEntityListCount = desired;
    return BLERR_NOERROR;
}

static void AAS_RemoveLinkFromAreaList(aas_link_t *link)
{
    if (link == NULL)
    {
        return;
    }

    int areanum = link->areanum;
    if (areanum < 0 || aasworld.areaEntityLists == NULL)
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

static void AAS_UnlinkEntityFromAreas(aas_entity_t *entity)
{
    if (entity == NULL)
    {
        return;
    }

    aas_link_t *link = entity->areas;
    while (link != NULL)
    {
        aas_link_t *next = link->next_area;
        AAS_RemoveLinkFromAreaList(link);
        free(link);
        link = next;
    }

    entity->areas = NULL;
}

static int AAS_LinkEntityToArea(aas_entity_t *entity, int areanum)
{
    if (areanum < 0)
    {
        return BLERR_INVALIDENTITYNUMBER;
    }

    if (aasworld.areaEntityLists == NULL ||
        (size_t)areanum >= aasworld.areaEntityListCount)
    {
        return BLERR_INVALIDENTITYNUMBER;
    }

    aas_link_t *link = (aas_link_t *)malloc(sizeof(aas_link_t));
    if (link == NULL)
    {
        return BLERR_INVALIDENTITYNUMBER;
    }

    link->entnum = entity->number;
    link->areanum = areanum;

    link->prev_area = NULL;
    link->next_area = entity->areas;
    if (entity->areas != NULL)
    {
        entity->areas->prev_area = link;
    }
    entity->areas = link;

    link->prev_ent = NULL;
    link->next_ent = aasworld.areaEntityLists[areanum];
    if (link->next_ent != NULL)
    {
        link->next_ent->prev_ent = link;
    }
    aasworld.areaEntityLists[areanum] = link;

    return BLERR_NOERROR;
}

static qboolean AAS_BoxIntersectsArea(const vec3_t absmins, const vec3_t absmaxs, const aas_area_t *area)
{
    if (area == NULL)
    {
        return qfalse;
    }

    for (int axis = 0; axis < 3; ++axis)
    {
        if (absmaxs[axis] < area->mins[axis] || absmins[axis] > area->maxs[axis])
        {
            return qfalse;
        }
    }

    return qtrue;
}

static void AAS_ClampMinsMaxs(vec3_t mins, vec3_t maxs)
{
    for (int axis = 0; axis < 3; ++axis)
    {
        if (mins[axis] > maxs[axis])
        {
            float tmp = mins[axis];
            mins[axis] = maxs[axis];
            maxs[axis] = tmp;
        }
    }
}

static int AAS_LinkEntityToComputedAreas(aas_entity_t *entity, const vec3_t absmins, const vec3_t absmaxs)
{
    if (entity == NULL)
    {
        return BLERR_INVALIDENTITYNUMBER;
    }

    AAS_UnlinkEntityFromAreas(entity);

    int status = AAS_PrepareEntityBitset(entity);
    if (status != BLERR_NOERROR)
    {
        return status;
    }

    if (aasworld.areas == NULL || aasworld.numAreas <= 0)
    {
        entity->outsideAllAreas = qtrue;
        entity->lastOutsideUpdate = aasworld.time;
        entity->areaOccupancyCount = 0;
        return BLERR_NOERROR;
    }

    status = AAS_EnsureAreaListArray();
    if (status != BLERR_NOERROR)
    {
        return status;
    }

    int occupied = 0;
    for (int areanum = 1; areanum <= aasworld.numAreas; ++areanum)
    {
        const aas_area_t *area = &aasworld.areas[areanum];
        if (!AAS_BoxIntersectsArea(absmins, absmaxs, area))
        {
            continue;
        }

        status = AAS_LinkEntityToArea(entity, areanum);
        if (status != BLERR_NOERROR)
        {
            return status;
        }

        AAS_SetEntityAreaBit(entity, areanum);
        ++occupied;
    }

    entity->areaOccupancyCount = occupied;

    if (occupied == 0)
    {
        entity->outsideAllAreas = qtrue;
        entity->lastOutsideUpdate = aasworld.time;
    }
    else
    {
        entity->outsideAllAreas = qfalse;
        entity->lastOutsideUpdate = 0.0f;
    }

    return BLERR_NOERROR;
}

int AAS_UpdateEntity(int ent, bot_updateentity_t *state)
{
    if (!aasworld.loaded)
    {
        return BLERR_NOAASFILE;
    }

    int ensureResult = AAS_EnsureEntityCapacity(ent);
    if (ensureResult != BLERR_NOERROR)
    {
        return ensureResult;
    }

    assert(aasworld.entities != NULL);
    aas_entity_t *entity = &aasworld.entities[ent];

    entity->number = ent;

    if (state == NULL)
    {
        AAS_UnlinkEntityFromAreas(entity);
        AAS_ResetEntityBitset(entity);
        entity->inuse = qfalse;
        entity->outsideAllAreas = qtrue;
        entity->lastOutsideUpdate = aasworld.time;
        return BLERR_NOERROR;
    }

    /* Preserve the historical fields before copying the new state. */
    VectorCopy(entity->origin, entity->previousOrigin);
    VectorCopy(state->angles, entity->angles);

    VectorCopy(state->origin, entity->origin);
    VectorCopy(state->old_origin, entity->old_origin);
    VectorCopy(state->mins, entity->mins);
    VectorCopy(state->maxs, entity->maxs);

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

    float previousUpdate = entity->lastUpdateTime;
    entity->lastUpdateTime = aasworld.time;
    entity->deltaTime = (previousUpdate > 0.0f) ? (aasworld.time - previousUpdate) : 0.0f;

    vec3_t absmins;
    vec3_t absmaxs;
    VectorAdd(entity->origin, entity->mins, absmins);
    VectorAdd(entity->origin, entity->maxs, absmaxs);
    AAS_ClampMinsMaxs(absmins, absmaxs);

    int linkStatus = AAS_LinkEntityToComputedAreas(entity, absmins, absmaxs);
    if (linkStatus != BLERR_NOERROR)
    {
        return linkStatus;
    }

    aasworld.entitiesValid = qtrue;
    return BLERR_NOERROR;
}
