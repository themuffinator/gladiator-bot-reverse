#include "aas_local.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "../common/l_log.h"

#define ROUTECACHE_TABLE_SIZE 256U
#define ROUTE_INVALID_TIME 0xFFFFU

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

    for (size_t index = 0; index < numAreas; ++index)
    {
        cache->traveltimes[index] = (unsigned short)ROUTE_INVALID_TIME;
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

static int AAS_TravelFlagForType(int traveltype)
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

static float VectorDistance(const vec3_t a, const vec3_t b)
{
    float dx = a[0] - b[0];
    float dy = a[1] - b[1];
    float dz = a[2] - b[2];
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

static unsigned short AAS_LocalTravelTime(int areanum, const vec3_t origin)
{
    if (origin == NULL)
    {
        return 0;
    }

    if (areanum <= 0 || areanum > aasworld.numAreas)
    {
        return 0;
    }

    const vec3_t center = aasworld.areas[areanum].center;
    float distance = VectorDistance(origin, center);
    float travel = distance * 0.33f;
    if (travel < 0.0f)
    {
        travel = 0.0f;
    }

    if (travel >= ROUTE_INVALID_TIME)
    {
        return (unsigned short)ROUTE_INVALID_TIME;
    }

    return (unsigned short)travel;
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

    for (int area = 0; area <= numAreas; ++area)
    {
        cache->traveltimes[area] = (unsigned short)ROUTE_INVALID_TIME;
    }

    if (cache->goalArea <= 0 || cache->goalArea > numAreas)
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

            Heap_Push(&heap, startArea, cost);
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

int AAS_AreaTravelTimeToGoalArea(int areanum, vec3_t origin, int goalareanum, int travelflags)
{
    if (!aasworld.loaded)
    {
        return 0;
    }

    if (areanum <= 0 || areanum > aasworld.numAreas)
    {
        BotLib_Print(PRT_ERROR, "AAS_AreaTravelTimeToGoalArea: areanum %d out of range\n", areanum);
        return 0;
    }

    if (goalareanum <= 0 || goalareanum > aasworld.numAreas)
    {
        BotLib_Print(PRT_ERROR, "AAS_AreaTravelTimeToGoalArea: goalareanum %d out of range\n", goalareanum);
        return 0;
    }

    if (areanum == goalareanum)
    {
        return (int)AAS_LocalTravelTime(areanum, origin);
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

    unsigned int total = base + (unsigned int)AAS_LocalTravelTime(areanum, origin);
    if (total > ROUTE_INVALID_TIME)
    {
        total = ROUTE_INVALID_TIME;
    }

    return (int)total;
}
