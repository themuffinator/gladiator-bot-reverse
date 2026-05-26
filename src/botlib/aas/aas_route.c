#include "aas_local.h"

#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "botlib/common/l_log.h"
#include "botlib/common/l_libvar.h"
#include "q2bridge/bridge_config.h"

#define ROUTECACHE_TABLE_SIZE 256U
#define ROUTE_INVALID_TIME 0xFFFFU

typedef struct
{
    int frames_with_work;
    int frames_skipped;
    int last_budget;
    bool forcewrite_active;
} aas_route_frame_state_t;

static aas_route_frame_state_t g_route_frame_state;

typedef struct
{
    int area;
    unsigned int time;
} routing_heap_node_t;

typedef struct
{
    routing_heap_node_t *nodes;
    int size;
    int capacity;
} routing_minheap_t;

typedef struct
{
	qboolean valid;
	qboolean visited;
	unsigned short starttime;
	unsigned short goaltime;
} aas_altroute_midrange_t;

static int Heap_Init(routing_minheap_t *heap, int initialCapacity)
{
    heap->size = 0;
    heap->capacity = initialCapacity > 0 ? initialCapacity : 0;
    if (heap->capacity == 0)
    {
        heap->nodes = NULL;
        return 1;
    }

    heap->nodes = (routing_heap_node_t *)malloc((size_t)heap->capacity * sizeof(routing_heap_node_t));
    if (heap->nodes == NULL)
    {
        heap->capacity = 0;
        return 0;
    }

    return 1;
}

static void Heap_Destroy(routing_minheap_t *heap)
{
    free(heap->nodes);
    heap->nodes = NULL;
    heap->size = 0;
    heap->capacity = 0;
}

static int Heap_Push(routing_minheap_t *heap, int area, unsigned int time)
{
    if (heap->capacity == 0)
    {
        heap->capacity = 16;
        heap->nodes = (routing_heap_node_t *)malloc((size_t)heap->capacity * sizeof(routing_heap_node_t));
        if (heap->nodes == NULL)
        {
            heap->capacity = 0;
            return 0;
        }
    }
    else if (heap->size >= heap->capacity)
    {
        int newCapacity = heap->capacity * 2;
        if (newCapacity <= heap->capacity)
        {
            newCapacity = heap->capacity + 16;
        }

        routing_heap_node_t *grown =
            (routing_heap_node_t *)realloc(heap->nodes, (size_t)newCapacity * sizeof(routing_heap_node_t));
        if (grown == NULL)
        {
            return 0;
        }
        heap->nodes = grown;
        heap->capacity = newCapacity;
    }

    int index = heap->size++;
    while (index > 0)
    {
        int parent = (index - 1) / 2;
        if (heap->nodes[parent].time <= time)
        {
            break;
        }
        heap->nodes[index] = heap->nodes[parent];
        index = parent;
    }

    heap->nodes[index].area = area;
    heap->nodes[index].time = time;
    return 1;
}

static routing_heap_node_t Heap_Pop(routing_minheap_t *heap)
{
    routing_heap_node_t result = {-1, 0};
    if (heap->size <= 0)
    {
        return result;
    }

    result = heap->nodes[0];
    int lastIndex = --heap->size;
    if (heap->size <= 0)
    {
        return result;
    }

    routing_heap_node_t temp = heap->nodes[lastIndex];
    int index = 0;

    while (1)
    {
        int left = 2 * index + 1;
        int right = left + 1;
        if (left >= heap->size)
        {
            break;
        }

        int smallest = left;
        if (right < heap->size && heap->nodes[right].time < heap->nodes[left].time)
        {
            smallest = right;
        }

        if (heap->nodes[smallest].time >= temp.time)
        {
            break;
        }

        heap->nodes[index] = heap->nodes[smallest];
        index = smallest;
    }

    heap->nodes[index] = temp;
    return result;
}

static unsigned int RouteCacheHash(int goalArea, int travelflags)
{
    unsigned int value = (unsigned int)goalArea * 1315423911U;
    value ^= (unsigned int)travelflags * 2654435761U;
    return value;
}

static int RouteCache_EnsureTable(void)
{
    if (aasworld.routingCacheTable != NULL && aasworld.routingCacheTableSize > 0)
    {
        return 1;
    }

    aasworld.routingCacheTable = (aas_routingcache_t **)calloc(ROUTECACHE_TABLE_SIZE,
                                                               sizeof(aas_routingcache_t *));
    if (aasworld.routingCacheTable == NULL)
    {
        return 0;
    }

    aasworld.routingCacheTableSize = ROUTECACHE_TABLE_SIZE;
    aasworld.routingCacheHead = NULL;
    aasworld.routingCacheTail = NULL;
    return 1;
}

static aas_routingcache_t *RouteCache_Find(int goalArea, int travelflags)
{
    if (aasworld.routingCacheTable == NULL || aasworld.routingCacheTableSize == 0)
    {
        return NULL;
    }

    unsigned int hash = RouteCacheHash(goalArea, travelflags) % aasworld.routingCacheTableSize;
    for (aas_routingcache_t *cache = aasworld.routingCacheTable[hash]; cache != NULL; cache = cache->hashNext)
    {
        if (cache->goalArea == goalArea && cache->travelflags == travelflags)
        {
            return cache;
        }
    }

    return NULL;
}

static void RouteCache_Insert(aas_routingcache_t *cache)
{
    if (cache == NULL)
    {
        return;
    }

    if (!RouteCache_EnsureTable())
    {
        return;
    }

    unsigned int hash = RouteCacheHash(cache->goalArea, cache->travelflags) % aasworld.routingCacheTableSize;
    cache->hashNext = aasworld.routingCacheTable[hash];
    aasworld.routingCacheTable[hash] = cache;

    cache->prev = aasworld.routingCacheTail;
    cache->next = NULL;
    if (aasworld.routingCacheTail != NULL)
    {
        aasworld.routingCacheTail->next = cache;
    }
    else
    {
        aasworld.routingCacheHead = cache;
    }
    aasworld.routingCacheTail = cache;
}

static void RouteCache_Unlink(aas_routingcache_t *cache)
{
    if (cache == NULL)
    {
        return;
    }

    if (cache->prev != NULL)
    {
        cache->prev->next = cache->next;
    }
    else
    {
        aasworld.routingCacheHead = cache->next;
    }

    if (cache->next != NULL)
    {
        cache->next->prev = cache->prev;
    }
    else
    {
        aasworld.routingCacheTail = cache->prev;
    }
}

static aas_routingcache_t *RouteCache_Alloc(int goalArea, int travelflags)
{
    size_t numAreas = (aasworld.numAreas > 0) ? (size_t)aasworld.numAreas + 1U : 1U;

    aas_routingcache_t *cache = (aas_routingcache_t *)calloc(1, sizeof(aas_routingcache_t));
    if (cache == NULL)
    {
        return NULL;
    }

    cache->traveltimes = (unsigned short *)malloc(numAreas * sizeof(unsigned short));
    if (cache->traveltimes == NULL)
    {
        free(cache);
        return NULL;
    }

    cache->reachabilities = (int *)malloc(numAreas * sizeof(int));
    if (cache->reachabilities == NULL)
    {
        free(cache->traveltimes);
        free(cache);
        return NULL;
    }

    for (size_t index = 0; index < numAreas; ++index)
    {
        cache->traveltimes[index] = (unsigned short)ROUTE_INVALID_TIME;
        cache->reachabilities[index] = -1;
    }

    cache->goalArea = goalArea;
    cache->travelflags = travelflags;
    cache->hashNext = NULL;
    cache->prev = NULL;
    cache->next = NULL;

    return cache;
}

void AAS_FreeAllRoutingCaches(void)
{
    aas_routingcache_t *cache = aasworld.routingCacheHead;
    while (cache != NULL)
    {
        aas_routingcache_t *next = cache->next;
        free(cache->traveltimes);
        free(cache->reachabilities);
        free(cache);
        cache = next;
    }

    if (aasworld.routingCacheTable != NULL)
    {
        free(aasworld.routingCacheTable);
    }

    aasworld.routingCacheTable = NULL;
    aasworld.routingCacheTableSize = 0;
    aasworld.routingCacheHead = NULL;
    aasworld.routingCacheTail = NULL;
}

void AAS_InvalidateRouteCache(void)
{
    AAS_FreeAllRoutingCaches();
}

void AAS_InitTravelFlagFromType(void)
{
    for (int i = 0; i < MAX_TRAVELTYPES; ++i)
    {
        aasworld.travelflagfortype[i] = TFL_INVALID;
    }

    aasworld.travelflagfortype[TRAVEL_INVALID] = TFL_INVALID;
    aasworld.travelflagfortype[TRAVEL_WALK] = TFL_WALK;
    aasworld.travelflagfortype[TRAVEL_CROUCH] = TFL_CROUCH;
    aasworld.travelflagfortype[TRAVEL_BARRIERJUMP] = TFL_BARRIERJUMP;
    aasworld.travelflagfortype[TRAVEL_JUMP] = TFL_JUMP;
    aasworld.travelflagfortype[TRAVEL_LADDER] = TFL_LADDER;
    aasworld.travelflagfortype[TRAVEL_WALKOFFLEDGE] = TFL_WALKOFFLEDGE;
    aasworld.travelflagfortype[TRAVEL_SWIM] = TFL_SWIM;
    aasworld.travelflagfortype[TRAVEL_WATERJUMP] = TFL_WATERJUMP;
    aasworld.travelflagfortype[TRAVEL_TELEPORT] = TFL_TELEPORT;
    aasworld.travelflagfortype[TRAVEL_ELEVATOR] = TFL_ELEVATOR;
    aasworld.travelflagfortype[TRAVEL_ROCKETJUMP] = TFL_ROCKETJUMP;
    aasworld.travelflagfortype[TRAVEL_BFGJUMP] = TFL_BFGJUMP;
    aasworld.travelflagfortype[TRAVEL_GRAPPLEHOOK] = TFL_GRAPPLEHOOK;
    aasworld.travelflagfortype[TRAVEL_DOUBLEJUMP] = TFL_DOUBLEJUMP;
    aasworld.travelflagfortype[TRAVEL_RAMPJUMP] = TFL_RAMPJUMP;
    aasworld.travelflagfortype[TRAVEL_STRAFEJUMP] = TFL_STRAFEJUMP;
    aasworld.travelflagfortype[TRAVEL_JUMPPAD] = TFL_JUMPPAD;
    aasworld.travelflagfortype[TRAVEL_FUNCBOB] = TFL_FUNCBOB;
}

/*
=============
AAS_TravelFlagForType

Translate a reachability travel type into the travel flags used by routing.
=============
*/
int AAS_TravelFlagForType(int traveltype)
{
    int flags = 0;
    if (traveltype & TRAVELFLAG_NOTTEAM1)
    {
        flags |= TFL_NOTTEAM1;
    }
    if (traveltype & TRAVELFLAG_NOTTEAM2)
    {
        flags |= TFL_NOTTEAM2;
    }

    int type = traveltype & TRAVELTYPE_MASK;
    if (type < 0 || type >= MAX_TRAVELTYPES)
    {
        return TFL_INVALID;
    }

    flags |= aasworld.travelflagfortype[type];
    return flags;
}

/*
=============
AAS_NormalizedTravelFlags

Default zero travel masks to the retail default travel set.
=============
*/
static int AAS_NormalizedTravelFlags(int travelflags)
{
	if (travelflags == 0)
	{
		return TFL_DEFAULT;
	}

	if ((travelflags & (TFL_AIR | TFL_WATER | TFL_SLIME | TFL_LAVA)) == 0)
	{
		travelflags |= TFL_AIR;
	}

	return travelflags;
}

/*
=============
AAS_GetAreaContentsTravelFlags

Translate an area's contents and flags into route-filter travel flags.
=============
*/
int AAS_GetAreaContentsTravelFlags(int areanum)
{
	if (aasworld.areasettings == NULL || areanum < 0 || areanum >= aasworld.numAreaSettings)
	{
		return 0;
	}

	const aas_areasettings_t *settings = &aasworld.areasettings[areanum];
	int contents = settings->contents;
	int flags = 0;

	if ((contents & AAS_AREACONTENTS_WATER) != 0)
	{
		flags |= TFL_WATER;
	}
	else if ((contents & AAS_AREACONTENTS_SLIME) != 0)
	{
		flags |= TFL_SLIME;
	}
	else if ((contents & AAS_AREACONTENTS_LAVA) != 0)
	{
		flags |= TFL_LAVA;
	}
	else
	{
		flags |= TFL_AIR;
	}

	if ((contents & AAS_AREACONTENTS_DONOTENTER) != 0)
	{
		flags |= TFL_DONOTENTER;
	}
	if ((contents & AAS_AREACONTENTS_NOTTEAM1) != 0)
	{
		flags |= TFL_NOTTEAM1;
	}
	if ((contents & AAS_AREACONTENTS_NOTTEAM2) != 0)
	{
		flags |= TFL_NOTTEAM2;
	}
	if ((settings->areaflags & AAS_AREA_BRIDGE) != 0)
	{
		flags |= TFL_BRIDGE;
	}

	return flags;
}

/*
=============
AAS_InitAreaContentsTravelFlags

Build the cached area-content travel mask table after AAS load.
=============
*/
void AAS_InitAreaContentsTravelFlags(void)
{
	free(aasworld.areacontentstravelflags);
	aasworld.areacontentstravelflags = NULL;

	if (aasworld.numAreaSettings <= 0)
	{
		return;
	}

	aasworld.areacontentstravelflags = (int *)calloc((size_t)aasworld.numAreaSettings, sizeof(int));
	if (aasworld.areacontentstravelflags == NULL)
	{
		return;
	}

	for (int areanum = 0; areanum < aasworld.numAreaSettings; ++areanum)
	{
		aasworld.areacontentstravelflags[areanum] = AAS_GetAreaContentsTravelFlags(areanum);
	}
}

/*
=============
AAS_AreaContentsTravelFlags

Return the cached route-filter flags for an AAS area.
=============
*/
int AAS_AreaContentsTravelFlags(int areanum)
{
	if (areanum < 0 || areanum >= aasworld.numAreaSettings)
	{
		return 0;
	}

	if (aasworld.areacontentstravelflags != NULL)
	{
		return aasworld.areacontentstravelflags[areanum];
	}

	return AAS_GetAreaContentsTravelFlags(areanum);
}

/*
=============
AAS_AreaDoNotEnter

Check the retail do-not-enter area content flag.
=============
*/
bool AAS_AreaDoNotEnter(int areanum)
{
	if (aasworld.areasettings == NULL || areanum <= 0 || areanum >= aasworld.numAreaSettings)
	{
		return false;
	}

	return (aasworld.areasettings[areanum].contents & AAS_AREACONTENTS_DONOTENTER) != 0;
}

/*
=============
AAS_AreaDisabled

Check whether routing through the area has been disabled.
=============
*/
bool AAS_AreaDisabled(int areanum)
{
	if (aasworld.areasettings == NULL || areanum <= 0 || areanum >= aasworld.numAreaSettings)
	{
		return true;
	}

	return (aasworld.areasettings[areanum].areaflags & AAS_AREA_DISABLED) != 0;
}

/*
=============
AAS_EnableRoutingArea

Enable, disable, or query routing through an AAS area.
=============
*/
int AAS_EnableRoutingArea(int areanum, int enable)
{
	if (aasworld.areasettings == NULL ||
	    areanum <= 0 ||
	    areanum > aasworld.numAreas ||
	    areanum >= aasworld.numAreaSettings)
	{
		BotLib_Print(PRT_ERROR, "AAS_EnableRoutingArea: areanum %d out of range\n", areanum);
		return 0;
	}

	int oldflags = aasworld.areasettings[areanum].areaflags;
	int wasenabled = (oldflags & AAS_AREA_DISABLED) == 0;
	if (enable < 0)
	{
		return wasenabled;
	}

	if (enable)
	{
		aasworld.areasettings[areanum].areaflags &= ~AAS_AREA_DISABLED;
	}
	else
	{
		aasworld.areasettings[areanum].areaflags |= AAS_AREA_DISABLED;
	}

	if ((oldflags & AAS_AREA_DISABLED) !=
	    (aasworld.areasettings[areanum].areaflags & AAS_AREA_DISABLED))
	{
		AAS_InvalidateRouteCache();
	}

	return wasenabled;
}

/*
=============
AAS_AreaHasLadder

Check ladder state from AAS flags or the Quake II contents bit.
=============
*/
bool AAS_AreaHasLadder(int areanum)
{
	if (aasworld.areasettings == NULL || areanum <= 0 || areanum >= aasworld.numAreaSettings)
	{
		return false;
	}

	const aas_areasettings_t *settings = &aasworld.areasettings[areanum];
	if ((settings->areaflags & AAS_AREA_LADDER) != 0)
	{
		return true;
	}

	return (settings->contents & CONTENTS_LADDER) != 0;
}

/*
=============
AAS_AreaTravelAllowed

Check an area's contents against the caller's travel mask.
=============
*/
bool AAS_AreaTravelAllowed(int areanum, int travelflags)
{
	if (AAS_AreaDisabled(areanum))
	{
		return false;
	}

	int flags = AAS_AreaContentsTravelFlags(areanum);
	int allowed = AAS_NormalizedTravelFlags(travelflags);
	return (flags & ~allowed) == 0;
}

static float VectorDistance(const vec3_t a, const vec3_t b)
{
    float dx = a[0] - b[0];
    float dy = a[1] - b[1];
    float dz = a[2] - b[2];
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

/*
=============
AAS_AreaTravelTime

Compute local travel time inside one area using retail distance factors.
=============
*/
unsigned short AAS_AreaTravelTime(int areanum, const vec3_t start, const vec3_t end)
{
	if (start == NULL || end == NULL)
	{
		return 0;
	}

	float distance = VectorDistance(start, end);
	if (AAS_AreaCrouch(areanum))
	{
		distance *= 1.3f;
	}
	else if (AAS_AreaSwim(areanum))
	{
		distance *= 1.0f;
	}
	else
	{
		distance *= 0.33f;
	}

	if (distance <= 0.0f)
	{
		return 1U;
	}
	if (distance >= ROUTE_INVALID_TIME)
	{
		return (unsigned short)ROUTE_INVALID_TIME;
	}

	return (unsigned short)distance;
}

static unsigned short AAS_LocalTravelTime(int areanum, const vec3_t origin, int reachIndex)
{
    if (origin == NULL)
    {
        return 0;
    }

    if (areanum <= 0 || areanum > aasworld.numAreas)
    {
        return 0;
    }

	if (reachIndex >= 0 && reachIndex < aasworld.numReachability)
	{
		return AAS_AreaTravelTime(areanum, origin, aasworld.reachability[reachIndex].start);
	}

    return AAS_AreaTravelTime(areanum, origin, aasworld.areas[areanum].center);
}

static void AAS_PopulateRouteCache(aas_routingcache_t *cache)
{
    if (cache == NULL)
    {
        return;
    }

    int numAreas = aasworld.numAreas;
    if (numAreas <= 0)
    {
        return;
    }

	if (aasworld.reversedReachability == NULL ||
	    aasworld.reachabilityFromArea == NULL ||
	    aasworld.reachability == NULL)
	{
		return;
	}

    for (int area = 0; area <= numAreas; ++area)
    {
        cache->traveltimes[area] = (unsigned short)ROUTE_INVALID_TIME;
        cache->reachabilities[area] = -1;
    }

    if (cache->goalArea <= 0 || cache->goalArea > numAreas)
    {
        return;
    }

	if (!AAS_AreaTravelAllowed(cache->goalArea, cache->travelflags))
	{
		return;
	}

    routing_minheap_t heap;
    if (!Heap_Init(&heap, 64))
    {
        return;
    }

    if (!Heap_Push(&heap, cache->goalArea, 0))
    {
        Heap_Destroy(&heap);
        return;
    }

    while (heap.size > 0)
    {
        routing_heap_node_t node = Heap_Pop(&heap);
        if (node.area <= 0 || node.area > numAreas)
        {
            continue;
        }

        if (node.time >= cache->traveltimes[node.area])
        {
            continue;
        }

        unsigned int clamped = node.time;
        if (clamped > ROUTE_INVALID_TIME)
        {
            clamped = ROUTE_INVALID_TIME;
        }
        cache->traveltimes[node.area] = (unsigned short)clamped;

        const aas_reversedreachability_t *reverse = &aasworld.reversedReachability[node.area];
        if (reverse->count <= 0 || reverse->reachIndexes == NULL)
        {
            continue;
        }

        for (int index = 0; index < reverse->count; ++index)
        {
            int reachIndex = reverse->reachIndexes[index];
            if (reachIndex < 0 || reachIndex >= aasworld.numReachability)
            {
                continue;
            }

            int startArea = aasworld.reachabilityFromArea[reachIndex];
            if (startArea <= 0 || startArea > numAreas)
            {
                continue;
            }

			if (!AAS_AreaTravelAllowed(startArea, cache->travelflags))
			{
				continue;
			}

            int traveltype = aasworld.reachability[reachIndex].traveltype;
            int required = AAS_TravelFlagForType(traveltype);
            if ((required & cache->travelflags) != required)
            {
                continue;
            }

            unsigned int cost = node.time + aasworld.reachability[reachIndex].traveltime;
            if (cost >= cache->traveltimes[startArea])
            {
                continue;
            }

            if (Heap_Push(&heap, startArea, cost))
            {
                cache->reachabilities[startArea] = reachIndex;
            }
        }
    }

    Heap_Destroy(&heap);
}

static aas_routingcache_t *RouteCache_Get(int goalArea, int travelflags)
{
    aas_routingcache_t *cache = RouteCache_Find(goalArea, travelflags);
    if (cache != NULL)
    {
        return cache;
    }

    cache = RouteCache_Alloc(goalArea, travelflags);
    if (cache == NULL)
    {
        return NULL;
    }

    AAS_PopulateRouteCache(cache);
    RouteCache_Insert(cache);
    return cache;
}

/*
=============
AAS_RouteTravelFlags

Normalize travel flags for a routed area pair.
=============
*/
static int AAS_RouteTravelFlags(int areanum, int goalareanum, int travelflags)
{
	travelflags = AAS_NormalizedTravelFlags(travelflags);
	if (AAS_AreaDoNotEnter(areanum) || AAS_AreaDoNotEnter(goalareanum))
	{
		travelflags |= TFL_DONOTENTER;
	}

	return travelflags;
}

/*
=============
AAS_RouteValidAreaPair

Validate source and goal areas for route queries.
=============
*/
static bool AAS_RouteValidAreaPair(int areanum, int goalareanum, const char *functionName)
{
	if (!aasworld.loaded)
	{
		return false;
	}

	if (areanum <= 0 || areanum > aasworld.numAreas)
	{
		BotLib_Print(PRT_ERROR, "%s: areanum %d out of range\n", functionName, areanum);
		return false;
	}

	if (goalareanum <= 0 || goalareanum > aasworld.numAreas)
	{
		BotLib_Print(PRT_ERROR, "%s: goalareanum %d out of range\n", functionName, goalareanum);
		return false;
	}

	return true;
}

int AAS_AreaTravelTimeToGoalArea(int areanum, vec3_t origin, int goalareanum, int travelflags)
{
	if (!AAS_RouteValidAreaPair(areanum, goalareanum, "AAS_AreaTravelTimeToGoalArea"))
	{
		return 0;
	}

	travelflags = AAS_RouteTravelFlags(areanum, goalareanum, travelflags);
	if (!AAS_AreaTravelAllowed(areanum, travelflags) ||
	    !AAS_AreaTravelAllowed(goalareanum, travelflags))
	{
		return 0;
	}

    if (areanum == goalareanum)
    {
        return 1;
    }

    aas_routingcache_t *cache = RouteCache_Get(goalareanum, travelflags);
    if (cache == NULL)
    {
        return 0;
    }

    unsigned short base = cache->traveltimes[areanum];
    if (base == 0 || base == (unsigned short)ROUTE_INVALID_TIME)
    {
        return 0;
    }

    int firstReachability = -1;
    if (cache->reachabilities != NULL)
    {
        firstReachability = cache->reachabilities[areanum];
    }

    unsigned int total = base + (unsigned int)AAS_LocalTravelTime(areanum, origin, firstReachability);
    if (total > ROUTE_INVALID_TIME)
    {
        total = ROUTE_INVALID_TIME;
    }

    return (int)total;
}

/*
=============
AAS_AreaReachabilityToGoalArea

Return the first reachability used to route from an area to a goal area.
=============
*/
int AAS_AreaReachabilityToGoalArea(int areanum, vec3_t origin, int goalareanum, int travelflags)
{
	(void)origin;

	if (!AAS_RouteValidAreaPair(areanum, goalareanum, "AAS_AreaReachabilityToGoalArea"))
	{
		return 0;
	}

	travelflags = AAS_RouteTravelFlags(areanum, goalareanum, travelflags);
	if (!AAS_AreaTravelAllowed(areanum, travelflags) ||
	    !AAS_AreaTravelAllowed(goalareanum, travelflags))
	{
		return 0;
	}

	if (areanum == goalareanum)
	{
		return 0;
	}

	aas_routingcache_t *cache = RouteCache_Get(goalareanum, travelflags);
	if (cache == NULL || cache->reachabilities == NULL)
	{
		return 0;
	}

	int reachnum = cache->reachabilities[areanum];
	if (reachnum <= 0 || reachnum >= aasworld.numReachability)
	{
		return 0;
	}

	return reachnum;
}

/*
=============
AAS_ReachabilityFromNum

Copy a reachability record by index.
=============
*/
void AAS_ReachabilityFromNum(int num, aas_reachability_t *reach)
{
	if (reach == NULL)
	{
		return;
	}

	if (!aasworld.initialized ||
	    aasworld.reachability == NULL ||
	    num < 0 ||
	    num >= aasworld.numReachability)
	{
		memset(reach, 0, sizeof(*reach));
		return;
	}

	memcpy(reach, &aasworld.reachability[num], sizeof(*reach));
}

/*
=============
AAS_PredictRouteStopArea

Check an intermediate or destination area against route stop events.
=============
*/
static int AAS_PredictRouteStopArea(aas_predictroute_t *route,
                                    int testareanum,
                                    const aas_reachability_t *reach,
                                    int stopevent,
                                    int stopcontents,
                                    int stopareanum,
                                    unsigned int reachTravelTime,
                                    int numareas)
{
	if (route == NULL ||
	    reach == NULL ||
	    testareanum <= 0 ||
	    testareanum >= aasworld.numAreaSettings)
	{
		return qfalse;
	}

	int contents = aasworld.areasettings[testareanum].contents;
	if ((stopevent & RSE_ENTERCONTENTS) != 0 &&
	    (contents & stopcontents) != 0)
	{
		route->stopevent = RSE_ENTERCONTENTS;
		route->endarea = testareanum;
		route->endcontents = contents;
		VectorCopy(reach->end, route->endpos);
		route->time += (int)reachTravelTime;
		route->numareas = numareas;
		return qtrue;
	}

	if ((stopevent & RSE_ENTERAREA) != 0 &&
	    testareanum == stopareanum)
	{
		route->stopevent = RSE_ENTERAREA;
		route->endarea = testareanum;
		route->endcontents = contents;
		VectorCopy(reach->start, route->endpos);
		route->numareas = numareas;
		return qtrue;
	}

	return qfalse;
}

/*
=============
AAS_PredictRoute

Predict the route to a goal area, stopping on the requested route event.
=============
*/
int AAS_PredictRoute(aas_predictroute_t *route,
                     int areanum,
                     vec3_t origin,
                     int goalareanum,
                     int travelflags,
                     int maxareas,
                     int maxtime,
                     int stopevent,
                     int stopcontents,
                     int stoptfl,
                     int stopareanum)
{
	if (route == NULL || origin == NULL)
	{
		return qfalse;
	}

	memset(route, 0, sizeof(*route));
	route->stopevent = RSE_NONE;
	route->endarea = goalareanum;
	VectorCopy(origin, route->endpos);

	if (!AAS_RouteValidAreaPair(areanum, goalareanum, "AAS_PredictRoute"))
	{
		route->stopevent = RSE_NOROUTE;
		return qfalse;
	}

	int curareanum = areanum;
	vec3_t curorigin;
	VectorCopy(origin, curorigin);

	int maxiterations = (aasworld.numAreas > 0) ? aasworld.numAreas : 1;
	for (int index = 0;
	     curareanum != goalareanum &&
	     (maxareas <= 0 || index < maxareas) &&
	     index < maxiterations;
	     ++index)
	{
		int reachnum = AAS_AreaReachabilityToGoalArea(curareanum,
		                                              curorigin,
		                                              goalareanum,
		                                              travelflags);
		if (reachnum <= 0)
		{
			route->stopevent = RSE_NOROUTE;
			return qfalse;
		}

		const aas_reachability_t *reach = &aasworld.reachability[reachnum];
		int travelFlags = AAS_TravelFlagForType(reach->traveltype);
		if ((stopevent & RSE_USETRAVELTYPE) != 0 &&
		    (travelFlags & stoptfl) != 0)
		{
			route->stopevent = RSE_USETRAVELTYPE;
			route->endarea = curareanum;
			route->endcontents = (curareanum > 0 && curareanum < aasworld.numAreaSettings)
			                         ? aasworld.areasettings[curareanum].contents
			                         : 0;
			route->endtravelflags = travelFlags;
			VectorCopy(reach->start, route->endpos);
			return qtrue;
		}

		int destination = reach->areanum;
		int destinationContents = (destination > 0 && destination < aasworld.numAreaSettings)
		                              ? aasworld.areasettings[destination].contents
		                              : 0;
		int destinationFlags = AAS_AreaContentsTravelFlags(destination);
		unsigned short localTravelTime = AAS_AreaTravelTime(areanum, origin, reach->start);
		unsigned int reachTravelTime = (unsigned int)localTravelTime + reach->traveltime;

		if ((stopevent & RSE_USETRAVELTYPE) != 0 &&
		    (destinationFlags & stoptfl) != 0)
		{
			route->stopevent = RSE_USETRAVELTYPE;
			route->endarea = destination;
			route->endcontents = destinationContents;
			route->endtravelflags = destinationFlags;
			VectorCopy(reach->end, route->endpos);
			route->time += (int)reachTravelTime;
			route->numareas = index + 1;
			return qtrue;
		}

		const aas_reachabilityareas_t *reachareas = NULL;
		if (aasworld.reachabilityAreas != NULL &&
		    reachnum >= 0 &&
		    reachnum < aasworld.numReachability)
		{
			reachareas = &aasworld.reachabilityAreas[reachnum];
		}

		int passareas = (reachareas != NULL) ? reachareas->numareas : 0;
		for (int passindex = 0; passindex <= passareas; ++passindex)
		{
			int testareanum = destination;
			if (passindex < passareas)
			{
				int areaindex = reachareas->firstarea + passindex;
				if (areaindex < 0 || areaindex >= aasworld.reachabilityAreaIndexSize)
				{
					continue;
				}
				testareanum = aasworld.reachabilityAreaIndex[areaindex];
			}

			if (AAS_PredictRouteStopArea(route,
			                             testareanum,
			                             reach,
			                             stopevent,
			                             stopcontents,
			                             stopareanum,
			                             reachTravelTime,
			                             index + 1))
			{
				return qtrue;
			}
		}

		route->time += (int)reachTravelTime;
		route->numareas = index + 1;
		route->endarea = destination;
		route->endcontents = destinationContents;
		route->endtravelflags = travelFlags;
		VectorCopy(reach->end, route->endpos);

		curareanum = destination;
		VectorCopy(reach->end, curorigin);

		if (maxtime > 0 && route->time > maxtime)
		{
			break;
		}
	}

	return (curareanum == goalareanum) ? qtrue : qfalse;
}

/*
=============
AAS_AlternativeRouteGoalTypeMatches

Check whether an area is eligible for the requested alternative-goal class.
=============
*/
static qboolean AAS_AlternativeRouteGoalTypeMatches(int areanum, int type)
{
	if ((type & ALTROUTEGOAL_ALL) != 0)
	{
		return qtrue;
	}

	if (aasworld.areasettings == NULL ||
	    areanum <= 0 ||
	    areanum >= aasworld.numAreaSettings)
	{
		return qfalse;
	}

	int contents = aasworld.areasettings[areanum].contents;
	if ((type & ALTROUTEGOAL_CLUSTERPORTALS) != 0 &&
	    (contents & AAS_AREACONTENTS_CLUSTERPORTAL) != 0)
	{
		return qtrue;
	}

	if ((type & ALTROUTEGOAL_VIEWPORTALS) != 0 &&
	    (contents & AAS_AREACONTENTS_VIEWPORTAL) != 0)
	{
		return qtrue;
	}

	return qfalse;
}

/*
=============
AAS_AlternativeRouteFloodCluster

Flood adjacent midrange areas through loaded AAS faces.
=============
*/
static void AAS_AlternativeRouteFloodCluster(int seedareanum,
                                             aas_altroute_midrange_t *midrange,
                                             int *clusterareas,
                                             int *numclusterareas,
                                             int *stack,
                                             int stackcapacity)
{
	if (midrange == NULL ||
	    clusterareas == NULL ||
	    numclusterareas == NULL ||
	    stack == NULL ||
	    seedareanum <= 0 ||
	    seedareanum > aasworld.numAreas ||
	    stackcapacity <= 0)
	{
		return;
	}

	int stackcount = 0;
	stack[stackcount++] = seedareanum;
	midrange[seedareanum].visited = qtrue;

	while (stackcount > 0)
	{
		int areanum = stack[--stackcount];
		clusterareas[*numclusterareas] = areanum;
		*numclusterareas += 1;

		if (aasworld.areas == NULL ||
		    aasworld.faces == NULL ||
		    aasworld.faceIndex == NULL)
		{
			continue;
		}

		const aas_area_t *area = &aasworld.areas[areanum];
		if (area->firstface < 0 || area->numfaces <= 0)
		{
			continue;
		}

		for (int index = 0; index < area->numfaces; ++index)
		{
			int faceindex = area->firstface + index;
			if (faceindex < 0 || faceindex >= aasworld.faceIndexSize)
			{
				continue;
			}

			int facenum = abs(aasworld.faceIndex[faceindex]);
			if (facenum <= 0 || facenum >= aasworld.numFaces)
			{
				continue;
			}

			const aas_face_t *face = &aasworld.faces[facenum];
			int otherareanum = (face->frontarea == areanum) ? face->backarea : face->frontarea;
			if (otherareanum <= 0 ||
			    otherareanum > aasworld.numAreas ||
			    !midrange[otherareanum].valid ||
			    midrange[otherareanum].visited)
			{
				continue;
			}

			if (stackcount >= stackcapacity)
			{
				continue;
			}

			midrange[otherareanum].visited = qtrue;
			stack[stackcount++] = otherareanum;
		}
	}
}

/*
=============
AAS_AlternativeRouteBestClusterArea

Choose the area closest to the flooded cluster center.
=============
*/
static int AAS_AlternativeRouteBestClusterArea(const int *clusterareas, int numclusterareas)
{
	if (clusterareas == NULL ||
	    numclusterareas <= 0 ||
	    aasworld.areas == NULL)
	{
		return 0;
	}

	vec3_t mid;
	VectorClear(mid);
	for (int index = 0; index < numclusterareas; ++index)
	{
		int areanum = clusterareas[index];
		if (areanum <= 0 || areanum > aasworld.numAreas)
		{
			continue;
		}

		VectorAdd(mid, aasworld.areas[areanum].center, mid);
	}
	VectorScale(mid, 1.0f / (float)numclusterareas, mid);

	float bestdist = 999999.0f;
	int bestareanum = 0;
	for (int index = 0; index < numclusterareas; ++index)
	{
		int areanum = clusterareas[index];
		if (areanum <= 0 || areanum > aasworld.numAreas)
		{
			continue;
		}

		float dist = VectorDistance(mid, aasworld.areas[areanum].center);
		if (dist < bestdist)
		{
			bestdist = dist;
			bestareanum = areanum;
		}
	}

	return bestareanum;
}

/*
=============
AAS_ClampRouteTime

Clamp an integer travel time to the ushort fields used by route goal records.
=============
*/
static unsigned short AAS_ClampRouteTime(int traveltime)
{
	if (traveltime <= 0)
	{
		return 0U;
	}
	if (traveltime >= (int)ROUTE_INVALID_TIME)
	{
		return (unsigned short)ROUTE_INVALID_TIME;
	}

	return (unsigned short)traveltime;
}

/*
=============
AAS_AlternativeRouteGoals

Return representative midrange areas for alternative routes to a goal area.
=============
*/
int AAS_AlternativeRouteGoals(vec3_t start,
                              int startareanum,
                              vec3_t goal,
                              int goalareanum,
                              int travelflags,
                              aas_altroutegoal_t *altroutegoals,
                              int maxaltroutegoals,
                              int type)
{
	(void)goal;

	if (start == NULL || altroutegoals == NULL || maxaltroutegoals <= 0)
	{
		return 0;
	}

	if (!AAS_RouteValidAreaPair(startareanum, goalareanum, "AAS_AlternativeRouteGoals"))
	{
		return 0;
	}

	int goaltraveltime = AAS_AreaTravelTimeToGoalArea(startareanum, start, goalareanum, travelflags);
	if (goaltraveltime <= 0)
	{
		return 0;
	}

	size_t numareas = (size_t)aasworld.numAreas + 1U;
	aas_altroute_midrange_t *midrange =
	    (aas_altroute_midrange_t *)calloc(numareas, sizeof(aas_altroute_midrange_t));
	int *clusterareas = (int *)calloc(numareas, sizeof(int));
	int *stack = (int *)calloc(numareas, sizeof(int));
	if (midrange == NULL || clusterareas == NULL || stack == NULL)
	{
		free(stack);
		free(clusterareas);
		free(midrange);
		return 0;
	}

	for (int areanum = 1; areanum <= aasworld.numAreas; ++areanum)
	{
		if (aasworld.areasettings == NULL || areanum >= aasworld.numAreaSettings)
		{
			continue;
		}

		if (!AAS_AlternativeRouteGoalTypeMatches(areanum, type))
		{
			continue;
		}

		if (AAS_AreaReachability(areanum) == 0)
		{
			continue;
		}

		int starttime = AAS_AreaTravelTimeToGoalArea(startareanum, start, areanum, travelflags);
		if (starttime <= 0)
		{
			continue;
		}

		if (starttime > (int)(1.1f * (float)goaltraveltime))
		{
			continue;
		}

		int goaltime = AAS_AreaTravelTimeToGoalArea(areanum, NULL, goalareanum, travelflags);
		if (goaltime <= 0)
		{
			continue;
		}

		if (goaltime > (int)(0.8f * (float)goaltraveltime))
		{
			continue;
		}

		midrange[areanum].valid = qtrue;
		midrange[areanum].starttime = AAS_ClampRouteTime(starttime);
		midrange[areanum].goaltime = AAS_ClampRouteTime(goaltime);
	}

	int numaltroutegoals = 0;
	for (int areanum = 1;
	     areanum <= aasworld.numAreas && numaltroutegoals < maxaltroutegoals;
	     ++areanum)
	{
		if (!midrange[areanum].valid || midrange[areanum].visited)
		{
			continue;
		}

		int numclusterareas = 0;
		AAS_AlternativeRouteFloodCluster(areanum,
		                                 midrange,
		                                 clusterareas,
		                                 &numclusterareas,
		                                 stack,
		                                 (int)numareas);

		int bestareanum = AAS_AlternativeRouteBestClusterArea(clusterareas, numclusterareas);
		if (bestareanum <= 0)
		{
			continue;
		}

		aas_altroutegoal_t *routegoal = &altroutegoals[numaltroutegoals];
		VectorCopy(aasworld.areas[bestareanum].center, routegoal->origin);
		routegoal->areanum = bestareanum;
		routegoal->starttraveltime = midrange[bestareanum].starttime;
		routegoal->goaltraveltime = midrange[bestareanum].goaltime;
		routegoal->extratraveltime = AAS_ClampRouteTime((int)midrange[bestareanum].starttime +
		                                                (int)midrange[bestareanum].goaltime -
		                                                goaltraveltime);

		numaltroutegoals += 1;
	}

	free(stack);
	free(clusterareas);
	free(midrange);
	return numaltroutegoals;
}

/*
=============
AAS_BridgeWalkable

Report whether a bridge area is currently walkable.
=============
*/
int AAS_BridgeWalkable(int areanum)
{
	(void)areanum;

	return qfalse;
}

/*
=============
AAS_RandomGoalStartArea

Choose the first area to try for random goal searches.
=============
*/
static int AAS_RandomGoalStartArea(void)
{
	if (aasworld.numAreas <= 0)
	{
		return 1;
	}

	return 1 + (int)(((double)rand() / ((double)RAND_MAX + 1.0)) * (double)aasworld.numAreas);
}

/*
=============
AAS_RandomGoalArea

Return a random routed goal area and an origin on or inside that area.
=============
*/
int AAS_RandomGoalArea(int areanum, int travelflags, int *goalareanum, vec3_t goalorigin)
{
	if (goalareanum == NULL ||
	    goalorigin == NULL ||
	    aasworld.areas == NULL ||
	    areanum <= 0 ||
	    areanum > aasworld.numAreas)
	{
		return qfalse;
	}

	if (AAS_AreaReachability(areanum) == 0)
	{
		return qfalse;
	}

	int candidate = AAS_RandomGoalStartArea();
	for (int index = 0; index < aasworld.numAreas; ++index)
	{
		if (candidate <= 0 || candidate > aasworld.numAreas)
		{
			candidate = 1;
		}

		if (AAS_AreaReachability(candidate) == 0)
		{
			candidate += 1;
			continue;
		}

		int traveltime = AAS_AreaTravelTimeToGoalArea(areanum,
		                                              aasworld.areas[areanum].center,
		                                              candidate,
		                                              travelflags);
		if (traveltime <= 0)
		{
			candidate += 1;
			continue;
		}

		if (AAS_AreaSwim(candidate))
		{
			*goalareanum = candidate;
			VectorCopy(aasworld.areas[candidate].center, goalorigin);
			return qtrue;
		}

		vec3_t start;
		vec3_t end;
		VectorCopy(aasworld.areas[candidate].center, start);
		VectorCopy(start, end);
		end[2] -= 300.0f;

		aas_trace_t trace = AAS_TraceClientBBox(start, end, PRESENCE_CROUCH, -1);
		if (!trace.startsolid &&
		    trace.fraction < 1.0f &&
		    AAS_PointAreaNum(trace.endpos) == candidate &&
		    AAS_AreaGroundFaceArea(candidate) > 300.0f)
		{
			*goalareanum = candidate;
			VectorCopy(trace.endpos, goalorigin);
			return qtrue;
		}

		candidate += 1;
	}

	return qfalse;
}

/*
=============
AAS_AreaVisible

Return whether the destination area is visible from the source area.
=============
*/
int AAS_AreaVisible(int srcarea, int destarea)
{
	(void)srcarea;
	(void)destarea;

	return qfalse;
}

/*
=============
AAS_ProjectPointOntoVector

Project a point onto the infinite line through a segment.
=============
*/
static void AAS_ProjectPointOntoVector(const vec3_t point, const vec3_t start, const vec3_t end, vec3_t projected)
{
	vec3_t pointvec;
	vec3_t dir;
	VectorSubtract(point, start, pointvec);
	VectorSubtract(end, start, dir);

	float length = sqrtf(DotProduct(dir, dir));
	if (length <= 0.0f)
	{
		VectorCopy(start, projected);
		return;
	}

	VectorScale(dir, 1.0f / length, dir);
	VectorMA(start, DotProduct(pointvec, dir), dir, projected);
}

/*
=============
AAS_NearestHideAreaPenalty

Compute the enemy-distance penalty used while searching for hide areas.
=============
*/
static qboolean AAS_NearestHideAreaPenalty(const vec3_t enemyorigin,
                                           const vec3_t start,
                                           const vec3_t end,
                                           unsigned int *penalty)
{
	vec3_t projected;
	AAS_ProjectPointOntoVector(enemyorigin, start, end, projected);

	int outside = qfalse;
	for (int axis = 0; axis < 3; ++axis)
	{
		if ((projected[axis] > start[axis] && projected[axis] > end[axis]) ||
		    (projected[axis] < start[axis] && projected[axis] < end[axis]))
		{
			outside = qtrue;
			break;
		}
	}

	vec3_t delta;
	if (outside)
	{
		VectorSubtract(enemyorigin, end, delta);
	}
	else
	{
		VectorSubtract(enemyorigin, projected, delta);
	}

	float dist2 = sqrtf(DotProduct(delta, delta));
	if (dist2 < 40.0f)
	{
		return qfalse;
	}

	VectorSubtract(enemyorigin, start, delta);
	float dist1 = sqrtf(DotProduct(delta, delta));
	if (dist2 < dist1 && penalty != NULL)
	{
		*penalty += (unsigned int)((dist1 - dist2) * 10.0f);
	}

	return qtrue;
}

/*
=============
AAS_NearestHideArea

Find the nearest routed area that moves away from an enemy and is not visible.
=============
*/
int AAS_NearestHideArea(int srcnum,
                        vec3_t origin,
                        int areanum,
                        int enemynum,
                        vec3_t enemyorigin,
                        int enemyareanum,
                        int travelflags)
{
	(void)srcnum;
	(void)enemynum;

	if (origin == NULL ||
	    enemyorigin == NULL ||
	    aasworld.areas == NULL ||
	    aasworld.areasettings == NULL ||
	    aasworld.reachability == NULL ||
	    areanum <= 0 ||
	    areanum > aasworld.numAreas ||
	    areanum >= aasworld.numAreaSettings)
	{
		return 0;
	}

	size_t numareas = (size_t)aasworld.numAreas + 1U;
	unsigned short *hidetraveltimes = (unsigned short *)calloc(numareas, sizeof(unsigned short));
	vec3_t *entryorigins = (vec3_t *)calloc(numareas, sizeof(vec3_t));
	if (hidetraveltimes == NULL || entryorigins == NULL)
	{
		free(entryorigins);
		free(hidetraveltimes);
		return 0;
	}
	for (size_t index = 0; index < numareas; ++index)
	{
		hidetraveltimes[index] = (unsigned short)ROUTE_INVALID_TIME;
	}

	routing_minheap_t heap;
	if (!Heap_Init(&heap, 32))
	{
		free(entryorigins);
		free(hidetraveltimes);
		return 0;
	}

	int bestarea = 0;
	unsigned short besttraveltime = 0;
	int startvisible = qtrue;
	int allowedtravelflags = AAS_NormalizedTravelFlags(travelflags);

	VectorCopy(origin, entryorigins[areanum]);
	hidetraveltimes[areanum] = 0U;
	if (!Heap_Push(&heap, areanum, 0U))
	{
		Heap_Destroy(&heap);
		free(entryorigins);
		free(hidetraveltimes);
		return 0;
	}

	while (heap.size > 0)
	{
		routing_heap_node_t node = Heap_Pop(&heap);
		int curareanum = node.area;
		if (curareanum <= 0 ||
		    curareanum > aasworld.numAreas ||
		    curareanum >= aasworld.numAreaSettings ||
		    hidetraveltimes[curareanum] != (unsigned short)node.time)
		{
			continue;
		}

		const aas_areasettings_t *settings = &aasworld.areasettings[curareanum];
		int firstreach = settings->firstreachablearea;
		int lastreach = firstreach + settings->numreachableareas;
		for (int reachnum = firstreach; reachnum < lastreach; ++reachnum)
		{
			if (reachnum <= 0 || reachnum >= aasworld.numReachability)
			{
				continue;
			}

			const aas_reachability_t *reach = &aasworld.reachability[reachnum];
			int nextareanum = reach->areanum;
			if (nextareanum <= 0 ||
			    nextareanum > aasworld.numAreas ||
			    nextareanum >= aasworld.numAreaSettings ||
			    nextareanum == enemyareanum)
			{
				continue;
			}

			int reachtravelflags = AAS_TravelFlagForType(reach->traveltype);
			if ((reachtravelflags & ~allowedtravelflags) != 0 ||
			    !AAS_AreaTravelAllowed(nextareanum, allowedtravelflags))
			{
				continue;
			}

			unsigned int traveltime = node.time +
			                          AAS_AreaTravelTime(curareanum, entryorigins[curareanum], reach->start) +
			                          reach->traveltime;
			if (traveltime >= ROUTE_INVALID_TIME)
			{
				continue;
			}

			if (!AAS_NearestHideAreaPenalty(enemyorigin, entryorigins[curareanum], reach->end, &traveltime))
			{
				continue;
			}

			if (!startvisible && AAS_AreaVisible(enemyareanum, nextareanum))
			{
				continue;
			}

			if (besttraveltime != 0 && traveltime >= besttraveltime)
			{
				continue;
			}

			if (hidetraveltimes[nextareanum] == (unsigned short)ROUTE_INVALID_TIME ||
			    hidetraveltimes[nextareanum] > (unsigned short)traveltime)
			{
				if (!AAS_AreaVisible(enemyareanum, nextareanum))
				{
					besttraveltime = (unsigned short)traveltime;
					bestarea = nextareanum;
				}

				hidetraveltimes[nextareanum] = (unsigned short)traveltime;
				VectorCopy(reach->end, entryorigins[nextareanum]);
				Heap_Push(&heap, nextareanum, traveltime);
			}
		}
	}

	Heap_Destroy(&heap);
	free(entryorigins);
	free(hidetraveltimes);
	return bestarea;
}

/*
=============
AAS_NextAreaReachability

Return the next reachability index for an area, matching the retail iterator.
=============
*/
int AAS_NextAreaReachability(int areanum, int reachnum)
{
	if (aasworld.areasettings == NULL || areanum <= 0 || areanum >= aasworld.numAreaSettings)
	{
		BotLib_Print(PRT_ERROR, "AAS_NextAreaReachability: areanum %d out of range\n", areanum);
		return 0;
	}

	const aas_areasettings_t *settings = &aasworld.areasettings[areanum];
	if (settings->numreachableareas <= 0)
	{
		return 0;
	}

	int first = settings->firstreachablearea;
	int end = first + settings->numreachableareas;
	if (reachnum == 0)
	{
		return first;
	}

	if (reachnum < first)
	{
		BotLib_Print(PRT_FATAL, "AAS_NextAreaReachability: reachnum < settings->firstreachableara");
		return 0;
	}

	int next = reachnum + 1;
	if (next >= end)
	{
		return 0;
	}

	return next;
}

void AAS_RouteFrameResetDiagnostics(void)
{
    memset(&g_route_frame_state, 0, sizeof(g_route_frame_state));
}

static int AAS_ReadIntLibVar(libvar_t *var)
{
    if (var == NULL)
    {
        return 0;
    }

    return (int)var->value;
}

static bool AAS_LibVarEnabled(libvar_t *var)
{
    if (var == NULL)
    {
        return false;
    }

    return var->value != 0.0f;
}

void AAS_RouteFrameUpdate(void)
{
    int budget = AAS_ReadIntLibVar(Bridge_FrameReachability());
    g_route_frame_state.last_budget = budget;
    g_route_frame_state.forcewrite_active = AAS_LibVarEnabled(Bridge_ForceWrite());

    if (budget <= 0)
    {
        g_route_frame_state.frames_skipped += 1;
        return;
    }

    g_route_frame_state.frames_with_work += 1;

    /* Ensure the routing cache table exists as part of the maintenance pass. */
    (void)RouteCache_EnsureTable();
}

int AAS_RouteFrameWorkCounter(void)
{
    return g_route_frame_state.frames_with_work;
}

int AAS_RouteFrameSkipCounter(void)
{
    return g_route_frame_state.frames_skipped;
}

int AAS_RouteFrameLastBudget(void)
{
    return g_route_frame_state.last_budget;
}

bool AAS_RouteFrameForceWriteActive(void)
{
    return g_route_frame_state.forcewrite_active;
}

int AAS_NextModelReachability(int startIndex, int modelnum)
{
    if (aasworld.reachability == NULL || aasworld.numReachability <= 0)
    {
        return 0;
    }

    int index = (startIndex <= 0) ? 1 : startIndex + 1;
    if (index < 1)
    {
        index = 1;
    }

    for (int reachIndex = index; reachIndex < aasworld.numReachability; ++reachIndex)
    {
        const aas_reachability_t *reach = &aasworld.reachability[reachIndex];
        int traveltype = reach->traveltype & TRAVELTYPE_MASK;
        if (traveltype == TRAVEL_ELEVATOR || traveltype == TRAVEL_FUNCBOB)
        {
            int reachModel = reach->facenum & 0x0000FFFF;
            if (reachModel == modelnum)
            {
                return reachIndex;
            }
        }
    }

    return 0;
}

/*
=============
AAS_ModelNumForEntity

Return the brush model number encoded one-based in live entity state.
=============
*/
int AAS_ModelNumForEntity(int entnum)
{
	if (aasworld.entities == NULL || aasworld.maxEntities <= 0)
	{
		return 0;
	}

	if (entnum < 0 || entnum >= aasworld.maxEntities)
	{
		return 0;
	}

	const aas_entity_t *entity = &aasworld.entities[entnum];
	if (entity == NULL || !entity->inuse || entity->modelindex <= 1)
	{
		return 0;
	}

	return entity->modelindex - 1;
}
