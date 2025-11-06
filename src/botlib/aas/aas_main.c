#include "aas_local.h"

#include <stdlib.h>
#include <string.h>

#include "../common/l_log.h"
#include "../../q2bridge/update_translator.h"

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
        free(link);
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
        if (!entity->inuse && entity->areas != NULL)
        {
            AAS_FrameUnlinkEntity(entity);
        }
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

    aasworld.initialized = qtrue;
    BotLib_Print(PRT_MESSAGE, "AAS initialized.\n");
}

void AAS_FrameSynchronise(float time)
{
    aasworld.time = time;
    TranslateEntity_SetCurrentTime(time);
}
