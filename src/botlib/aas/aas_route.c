#include "aas_local.h"

#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "botlib/common/l_log.h"
#include "botlib/common/l_libvar.h"
#include "botlib/common/l_memory.h"
#include "q2bridge/bridge_config.h"

#define ROUTECACHE_TABLE_SIZE 256U
#define ROUTE_INVALID_TIME 0xFFFFU
#define RETAIL_ROUTECACHE_BASE_SIZE 0x2cU
#define RETAIL_ROUTECACHE_REFRESH_TIME 15.0f
#define RETAIL_TFL_AIR 0x00008000
#define RETAIL_TFL_WATER 0x00010000
#define RETAIL_TFL_SLIME 0x00020000
#define RETAIL_TFL_LAVA 0x00040000
#define RETAIL_TFL_TRAVEL_MASK 0x00007fff

typedef struct aas_retailroutingupdate_s
{
	int areanum;
	unsigned short tmptraveltime;
	int outgoingreach;
	qboolean inlist;
	struct aas_retailroutingupdate_s *next;
	struct aas_retailroutingupdate_s *prev;
} aas_retailroutingupdate_t;

typedef struct aas_retailportalupdate_s
{
	int cluster;
	int areanum;
	unsigned short tmptraveltime;
	qboolean inlist;
	struct aas_retailportalupdate_s *next;
	struct aas_retailportalupdate_s *prev;
} aas_retailportalupdate_t;

static const int g_retail_travel_flags[32] = {
	0,
	0x00000001,
	0x00000002,
	0x00000004,
	0x00000008,
	0x00000010,
	0x00000020,
	0x00000080,
	0x00000100,
	0x00000200,
	0x00000400,
	0x00000800,
	0x00001000,
	0x00002000,
	0x00004000,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0
};

static int g_retail_area_cache_updates;
static int g_retail_portal_cache_updates;
static int g_retail_frame_routing_updates;

static qboolean AAS_InitAlternativeRoutingScratch(void);
static void AAS_FreeAlternativeRoutingScratch(void);

#define AAS_ROUTE_STATIC_ASSERT(name, expression) \
	typedef char name[(expression) ? 1 : -1]
AAS_ROUTE_STATIC_ASSERT(aas_route_time_offset,
	offsetof(aas_retailroutingcache32_t, time) == 0x00);
AAS_ROUTE_STATIC_ASSERT(aas_route_cluster_offset,
	offsetof(aas_retailroutingcache32_t, cluster) == 0x04);
AAS_ROUTE_STATIC_ASSERT(aas_route_area_offset,
	offsetof(aas_retailroutingcache32_t, areanum) == 0x08);
AAS_ROUTE_STATIC_ASSERT(aas_route_origin_offset,
	offsetof(aas_retailroutingcache32_t, origin) == 0x0c);
AAS_ROUTE_STATIC_ASSERT(aas_route_start_time_offset,
	offsetof(aas_retailroutingcache32_t, starttraveltime) == 0x18);
AAS_ROUTE_STATIC_ASSERT(aas_route_travel_flags_offset,
	offsetof(aas_retailroutingcache32_t, travelflags) == 0x1c);
AAS_ROUTE_STATIC_ASSERT(aas_route_previous_offset,
	offsetof(aas_retailroutingcache32_t, prev) == 0x20);
AAS_ROUTE_STATIC_ASSERT(aas_route_next_offset,
	offsetof(aas_retailroutingcache32_t, next) == 0x24);
AAS_ROUTE_STATIC_ASSERT(aas_route_travel_times_offset,
	offsetof(aas_retailroutingcache32_t, traveltimes) == 0x28);
AAS_ROUTE_STATIC_ASSERT(aas_route_base_size,
	sizeof(aas_retailroutingcache32_t) == RETAIL_ROUTECACHE_BASE_SIZE);
#undef AAS_ROUTE_STATIC_ASSERT

/*
=============
AAS_RetailRoutingCacheSize

Return the exact x86 allocation size used by retail for a cache record.
=============
*/
size_t AAS_RetailRoutingCacheSize(int numtraveltimes)
{
	if (numtraveltimes < 0 ||
		(size_t)numtraveltimes >
		(SIZE_MAX - RETAIL_ROUTECACHE_BASE_SIZE) / sizeof(unsigned short))
	{
		return 0U;
	}

	return RETAIL_ROUTECACHE_BASE_SIZE +
		(size_t)numtraveltimes * sizeof(unsigned short);
}

/*
=============
AAS_RetailRoutingCacheHostSize

Size the host-native counterpart of retail's variable-length cache record.
=============
*/
static size_t AAS_RetailRoutingCacheHostSize(int numtraveltimes)
{
	if (numtraveltimes < 0 ||
		(size_t)numtraveltimes >
		(SIZE_MAX - sizeof(aas_retailroutingcache_t)) / sizeof(unsigned short))
	{
		return 0U;
	}

	return sizeof(aas_retailroutingcache_t) +
		(size_t)numtraveltimes * sizeof(unsigned short);
}

/*
=============
AAS_AllocRetailRoutingCache

Allocate a cleared retail cache header followed by its travel-time table.
=============
*/
static aas_retailroutingcache_t *AAS_AllocRetailRoutingCache(int numtraveltimes)
{
	size_t size = AAS_RetailRoutingCacheHostSize(numtraveltimes);
	if (size == 0U)
	{
		return NULL;
	}

	return (aas_retailroutingcache_t *)GetClearedMemory(size);
}

/*
=============
AAS_FreeRetailRoutingCache

Release one variable-sized retail routing-cache record.
=============
*/
static void AAS_FreeRetailRoutingCache(aas_retailroutingcache_t *cache)
{
	FreeMemory(cache);
}

/*
=============
AAS_FreeRetailClusterAreaCaches

Free every cache list and the contiguous per-cluster area-head table.
=============
*/
static void AAS_FreeRetailClusterAreaCaches(void)
{
	if (aasworld.retailClusterAreaCache == NULL)
	{
		return;
	}

	for (int clusternum = 0; clusternum < aasworld.numClusters; ++clusternum)
	{
		int numareas = aasworld.clusters != NULL ?
			aasworld.clusters[clusternum].numareas : 0;
		for (int clusterareanum = 0; clusterareanum < numareas; ++clusterareanum)
		{
			aas_retailroutingcache_t *cache =
				aasworld.retailClusterAreaCache[clusternum][clusterareanum];
			while (cache != NULL)
			{
				aas_retailroutingcache_t *next = cache->next;
				AAS_FreeRetailRoutingCache(cache);
				cache = next;
			}
			aasworld.retailClusterAreaCache[clusternum][clusterareanum] = NULL;
		}
	}

	FreeMemory(aasworld.retailClusterAreaCache);
	aasworld.retailClusterAreaCache = NULL;
}

/*
=============
AAS_InitRetailClusterAreaCaches

Allocate retail's contiguous row-pointer and per-cluster area-head table.
=============
*/
static qboolean AAS_InitRetailClusterAreaCaches(void)
{
	if (aasworld.numClusters <= 0)
	{
		aasworld.retailClusterAreaCache = NULL;
		return qtrue;
	}
	if (aasworld.clusters == NULL)
	{
		return qfalse;
	}

	size_t numheads = 0U;
	for (int clusternum = 0; clusternum < aasworld.numClusters; ++clusternum)
	{
		int numareas = aasworld.clusters[clusternum].numareas;
		if (numareas < 0 || (size_t)numareas > SIZE_MAX - numheads)
		{
			return qfalse;
		}
		numheads += (size_t)numareas;
	}

	if ((size_t)aasworld.numClusters > SIZE_MAX / sizeof(aas_retailroutingcache_t **) ||
		numheads > SIZE_MAX / sizeof(aas_retailroutingcache_t *))
	{
		return qfalse;
	}

	size_t rowsize =
		(size_t)aasworld.numClusters * sizeof(aas_retailroutingcache_t **);
	size_t headsize = numheads * sizeof(aas_retailroutingcache_t *);
	if (headsize > SIZE_MAX - rowsize)
	{
		return qfalse;
	}

	unsigned char *block = (unsigned char *)GetClearedMemory(rowsize + headsize);
	if (block == NULL)
	{
		return qfalse;
	}

	aasworld.retailClusterAreaCache = (aas_retailroutingcache_t ***)block;
	aas_retailroutingcache_t **heads =
		(aas_retailroutingcache_t **)(block + rowsize);
	for (int clusternum = 0; clusternum < aasworld.numClusters; ++clusternum)
	{
		aasworld.retailClusterAreaCache[clusternum] = heads;
		heads += aasworld.clusters[clusternum].numareas;
	}

	return qtrue;
}

/*
=============
AAS_FreeRetailPortalCaches

Free every portal-route list and the per-area portal-cache head table.
=============
*/
static void AAS_FreeRetailPortalCaches(void)
{
	if (aasworld.retailPortalCache == NULL)
	{
		return;
	}

	for (int areanum = 0; areanum < aasworld.numAreas; ++areanum)
	{
		aas_retailroutingcache_t *cache = aasworld.retailPortalCache[areanum];
		while (cache != NULL)
		{
			aas_retailroutingcache_t *next = cache->next;
			AAS_FreeRetailRoutingCache(cache);
			cache = next;
		}
		aasworld.retailPortalCache[areanum] = NULL;
	}

	FreeMemory(aasworld.retailPortalCache);
	aasworld.retailPortalCache = NULL;
}

/*
=============
AAS_InitRetailPortalCaches

Allocate one cleared portal-cache list head for every global AAS area.
=============
*/
static qboolean AAS_InitRetailPortalCaches(void)
{
	if (aasworld.numAreas <= 0)
	{
		aasworld.retailPortalCache = NULL;
		return qtrue;
	}
	if ((size_t)aasworld.numAreas >
		SIZE_MAX / sizeof(aas_retailroutingcache_t *))
	{
		return qfalse;
	}

	aasworld.retailPortalCache = (aas_retailroutingcache_t **)GetClearedMemory(
		(size_t)aasworld.numAreas * sizeof(aas_retailroutingcache_t *));
	return aasworld.retailPortalCache != NULL ? qtrue : qfalse;
}

/*
=============
AAS_InitRetailRoutingCaches

Recreate both retail cache-head table families for the current AAS world.
=============
*/
qboolean AAS_InitRetailRoutingCaches(void)
{
	AAS_FreeRetailRoutingCaches();
	if (!AAS_InitRetailClusterAreaCaches())
	{
		return qfalse;
	}
	if (!AAS_InitRetailPortalCaches())
	{
		AAS_FreeRetailClusterAreaCaches();
		return qfalse;
	}
	if (!AAS_InitAlternativeRoutingScratch())
	{
		AAS_FreeRetailRoutingCaches();
		return qfalse;
	}

	return qtrue;
}

/*
=============
AAS_FreeRetailRoutingCaches

Release retail cluster-area caches first, then the portal cache family.
=============
*/
void AAS_FreeRetailRoutingCaches(void)
{
	AAS_FreeRetailClusterAreaCaches();
	AAS_FreeRetailPortalCaches();
}

/*
=============
AAS_RetailClusterAreaNum

Resolve an area's table slot, including the requested side of a portal area.
=============
*/
static int AAS_RetailClusterAreaNum(int clusternum, int areanum)
{
	if (aasworld.areasettings == NULL || areanum < 0 ||
		areanum >= aasworld.numAreaSettings)
	{
		return -1;
	}

	int areacluster = aasworld.areasettings[areanum].cluster;
	if (areacluster > 0)
	{
		return aasworld.areasettings[areanum].clusterareanum;
	}

	int portalnum = -areacluster;
	if (aasworld.portals == NULL || portalnum < 0 ||
		portalnum >= aasworld.numPortals)
	{
		return -1;
	}

	int side = aasworld.portals[portalnum].frontcluster != clusternum;
	return aasworld.portals[portalnum].clusterareanum[side];
}

/*
=============
AAS_RetailTravelFlagForType

Translate Gladiator's unmasked reachability type through the retail table.
=============
*/
static int AAS_RetailTravelFlagForType(int traveltype)
{
	if (traveltype < 0 ||
		traveltype >= (int)(sizeof(g_retail_travel_flags) /
			sizeof(g_retail_travel_flags[0])))
	{
		return 0;
	}

	return g_retail_travel_flags[traveltype];
}

/*
=============
AAS_RetailAreaContentTravelFlag

Select retail's single water, slime, lava, or air flag for an area.
=============
*/
static int AAS_RetailAreaContentTravelFlag(int areanum)
{
	if (aasworld.areasettings == NULL || areanum < 0 ||
		areanum >= aasworld.numAreaSettings)
	{
		return 0;
	}

	int contents = aasworld.areasettings[areanum].contents;
	if ((contents & AAS_AREACONTENTS_WATER) != 0)
	{
		return RETAIL_TFL_WATER;
	}
	if ((contents & AAS_AREACONTENTS_SLIME) != 0)
	{
		return RETAIL_TFL_SLIME;
	}
	if ((contents & AAS_AREACONTENTS_LAVA) != 0)
	{
		return RETAIL_TFL_LAVA;
	}

	return RETAIL_TFL_AIR;
}

/*
=============
AAS_RetailLocalTravelTime

Reproduce one entry from retail's per-area local travel-time matrix.
=============
*/
static unsigned short AAS_RetailLocalTravelTime(int areanum,
	int outgoingreach,
	int incomingreach)
{
	if (aasworld.reachability == NULL || outgoingreach < 0 ||
		outgoingreach >= aasworld.numReachability || incomingreach < 0 ||
		incomingreach >= aasworld.numReachability)
	{
		return 0;
	}

	vec3_t delta;
	VectorSubtract(aasworld.reachability[outgoingreach].start,
		aasworld.reachability[incomingreach].end,
		delta);
	float distance = sqrtf(delta[0] * delta[0] +
		delta[1] * delta[1] + delta[2] * delta[2]);
	double scaled = (double)distance;
	if (AAS_AreaCrouch(areanum))
	{
		scaled *= 1.3;
	}
	else if (!AAS_AreaSwim(areanum))
	{
		scaled *= 0.33;
	}

	int traveltime = (int)scaled;
	if (traveltime <= 0)
	{
		traveltime = 1;
	}

	return (unsigned short)traveltime;
}

/*
=============
AAS_UpdateRetailAreaRoutingCache

Propagate one cluster-area cache with retail's reversed-link FIFO update pass.
=============
*/
static void AAS_UpdateRetailAreaRoutingCache(aas_retailroutingcache_t *cache)
{
	g_retail_area_cache_updates += 1;
	g_retail_frame_routing_updates += 1;

	if (cache == NULL || aasworld.areasettings == NULL ||
		aasworld.reachability == NULL || aasworld.reachabilityFromArea == NULL ||
		aasworld.reversedReachability == NULL || aasworld.clusters == NULL ||
		aasworld.numAreas <= 0 || cache->areanum < 0 ||
		cache->areanum >= aasworld.numAreas || cache->cluster < 0 ||
		cache->cluster >= aasworld.numClusters)
	{
		return;
	}

	int goalclusterarea = AAS_RetailClusterAreaNum(cache->cluster,
		cache->areanum);
	if (goalclusterarea < 0 ||
		goalclusterarea >= aasworld.clusters[cache->cluster].numareas)
	{
		return;
	}

	aas_retailroutingupdate_t *updates =
		(aas_retailroutingupdate_t *)GetClearedMemory(
			(size_t)aasworld.numAreas * sizeof(aas_retailroutingupdate_t));
	if (updates == NULL)
	{
		return;
	}

	int badtravelflags = ~cache->travelflags;
	aas_retailroutingupdate_t *head = &updates[cache->areanum];
	aas_retailroutingupdate_t *tail = head;
	head->areanum = cache->areanum;
	head->tmptraveltime = (unsigned short)(int)cache->starttraveltime;
	head->outgoingreach = -1;
	if (cache->areanum < aasworld.numAreaSettings)
	{
		const aas_areasettings_t *goalsettings =
			&aasworld.areasettings[cache->areanum];
		if (goalsettings->numreachableareas > 0 &&
			goalsettings->firstreachablearea >= 0 &&
			goalsettings->firstreachablearea < aasworld.numReachability)
		{
			head->outgoingreach = goalsettings->firstreachablearea;
		}
	}
	head->inlist = qtrue;
	cache->traveltimes[goalclusterarea] = head->tmptraveltime;

	while (head != NULL)
	{
		aas_retailroutingupdate_t *current = head;
		head = current->next;
		if (head != NULL)
		{
			head->prev = NULL;
		}
		else
		{
			tail = NULL;
		}
		current->next = NULL;
		current->prev = NULL;
		current->inlist = qfalse;

		if (current->areanum < 0 || current->areanum >= aasworld.numAreas)
		{
			continue;
		}

		const aas_reversedreachability_t *reversed =
			&aasworld.reversedReachability[current->areanum];
		for (int reverseindex = reversed->count - 1;
			reverseindex >= 0;
			--reverseindex)
		{
			if (reversed->reachIndexes == NULL)
			{
				break;
			}

			int reachnum = reversed->reachIndexes[reverseindex];
			if (reachnum < 0 || reachnum >= aasworld.numReachability)
			{
				continue;
			}

			const aas_reachability_t *reach = &aasworld.reachability[reachnum];
			int required = AAS_RetailTravelFlagForType(reach->traveltype);
			if ((required & badtravelflags) != 0 ||
				(AAS_RetailAreaContentTravelFlag(reach->areanum) &
					badtravelflags) != 0)
			{
				continue;
			}

			int sourcearea = aasworld.reachabilityFromArea[reachnum];
			if (sourcearea < 0 || sourcearea >= aasworld.numAreas ||
				sourcearea >= aasworld.numAreaSettings)
			{
				continue;
			}

			int sourcecluster = aasworld.areasettings[sourcearea].cluster;
			if (sourcecluster > 0 && sourcecluster != cache->cluster)
			{
				continue;
			}

			int sourceclusterarea = AAS_RetailClusterAreaNum(cache->cluster,
				sourcearea);
			if (sourceclusterarea < 0 ||
				sourceclusterarea >= aasworld.clusters[cache->cluster].numareas)
			{
				continue;
			}

			unsigned short localtraveltime = 0;
			if (current->outgoingreach >= 0)
			{
				localtraveltime = AAS_RetailLocalTravelTime(current->areanum,
					current->outgoingreach,
					reachnum);
			}
			unsigned short candidate = (unsigned short)(
				(unsigned int)localtraveltime +
				(unsigned int)reach->traveltime +
				(unsigned int)current->tmptraveltime);
			unsigned short existing = cache->traveltimes[sourceclusterarea];
			if (existing != 0 && existing <= candidate)
			{
				continue;
			}

			cache->traveltimes[sourceclusterarea] = candidate;
			aas_retailroutingupdate_t *nextupdate = &updates[sourcearea];
			nextupdate->areanum = sourcearea;
			nextupdate->tmptraveltime = candidate;
			nextupdate->outgoingreach = reachnum;
			if (!nextupdate->inlist)
			{
				nextupdate->prev = tail;
				nextupdate->next = NULL;
				if (tail != NULL)
				{
					tail->next = nextupdate;
				}
				else
				{
					head = nextupdate;
				}
				tail = nextupdate;
				nextupdate->inlist = qtrue;
			}
		}
	}

	FreeMemory(updates);
}

/*
=============
AAS_RetailAreaCacheUpdateCount

Return the total number of retail cluster-area population passes.
=============
*/
int AAS_RetailAreaCacheUpdateCount(void)
{
	return g_retail_area_cache_updates;
}

/*
=============
AAS_RetailFrameRoutingUpdateCount

Return the retail cluster-area population passes performed this frame.
=============
*/
int AAS_RetailFrameRoutingUpdateCount(void)
{
	return g_retail_frame_routing_updates;
}

/*
=============
AAS_GetRetailAreaRoutingCache

Find or insert the travel-flags cache in one cluster-area table slot.
=============
*/
aas_retailroutingcache_t *AAS_GetRetailAreaRoutingCache(int clusternum,
	int areanum,
	int travelflags)
{
	if (aasworld.retailClusterAreaCache == NULL || aasworld.clusters == NULL ||
		aasworld.areas == NULL || clusternum < 0 ||
		clusternum >= aasworld.numClusters || areanum < 0 ||
		areanum >= aasworld.numAreas)
	{
		return NULL;
	}

	int clusterareanum = AAS_RetailClusterAreaNum(clusternum, areanum);
	if (clusterareanum < 0 ||
		clusterareanum >= aasworld.clusters[clusternum].numareas)
	{
		return NULL;
	}

	aas_retailroutingcache_t **head =
		&aasworld.retailClusterAreaCache[clusternum][clusterareanum];
	aas_retailroutingcache_t *cache = *head;
	while (cache != NULL && cache->travelflags != travelflags)
	{
		cache = cache->next;
	}

	if (cache == NULL)
	{
		cache = AAS_AllocRetailRoutingCache(
			aasworld.clusters[clusternum].numareas);
		if (cache == NULL)
		{
			return NULL;
		}

		cache->cluster = clusternum;
		cache->areanum = areanum;
		VectorCopy(aasworld.areas[areanum].center, cache->origin);
		cache->starttraveltime = 1.0f;
		cache->travelflags = travelflags;
		cache->prev = NULL;
		cache->next = *head;
		if (*head != NULL)
		{
			(*head)->prev = cache;
		}
		*head = cache;
		AAS_UpdateRetailAreaRoutingCache(cache);
	}

	cache->time = AAS_Time();
	return cache;
}

/*
=============
AAS_UpdateRetailPortalRoutingCache

Propagate portal travel times through each cluster's ordered portal list.
=============
*/
static void AAS_UpdateRetailPortalRoutingCache(aas_retailroutingcache_t *cache)
{
	g_retail_portal_cache_updates += 1;

	if (cache == NULL || aasworld.areasettings == NULL ||
		aasworld.clusters == NULL || aasworld.portals == NULL ||
		aasworld.numAreas <= 0 || cache->areanum < 0 ||
		cache->areanum >= aasworld.numAreas ||
		cache->areanum >= aasworld.numAreaSettings || cache->cluster < 0 ||
		cache->cluster >= aasworld.numClusters)
	{
		return;
	}

	aas_retailportalupdate_t *updates =
		(aas_retailportalupdate_t *)GetClearedMemory(
			(size_t)aasworld.numAreas * sizeof(aas_retailportalupdate_t));
	if (updates == NULL)
	{
		return;
	}

	aas_retailportalupdate_t *head = &updates[cache->areanum];
	aas_retailportalupdate_t *tail = head;
	head->cluster = cache->cluster;
	head->areanum = cache->areanum;
	head->tmptraveltime = (unsigned short)(int)cache->starttraveltime;
	head->inlist = qtrue;

	int areacluster = aasworld.areasettings[cache->areanum].cluster;
	if (areacluster < 0)
	{
		int portalnum = -areacluster;
		if (portalnum >= 0 && portalnum < aasworld.numPortals)
		{
			cache->traveltimes[portalnum] = head->tmptraveltime;
		}
	}

	while (head != NULL)
	{
		aas_retailportalupdate_t *current = head;
		head = current->next;
		if (head != NULL)
		{
			head->prev = NULL;
		}
		else
		{
			tail = NULL;
		}
		current->next = NULL;
		current->prev = NULL;
		current->inlist = qfalse;

		if (current->cluster < 0 || current->cluster >= aasworld.numClusters ||
			current->areanum < 0 || current->areanum >= aasworld.numAreas)
		{
			continue;
		}

		aas_retailroutingcache_t *areacache =
			AAS_GetRetailAreaRoutingCache(current->cluster,
				current->areanum,
				cache->travelflags);
		if (areacache == NULL)
		{
			continue;
		}

		const aas_cluster_t *cluster = &aasworld.clusters[current->cluster];
		for (int index = 0; index < cluster->numportals; ++index)
		{
			int portalindex = cluster->firstportal + index;
			if (aasworld.portalIndex == NULL || portalindex < 0 ||
				portalindex >= aasworld.portalIndexSize)
			{
				continue;
			}

			int portalnum = aasworld.portalIndex[portalindex];
			if (portalnum < 0 || portalnum >= aasworld.numPortals)
			{
				continue;
			}

			const aas_portal_t *portal = &aasworld.portals[portalnum];
			if (portal->areanum == current->areanum)
			{
				continue;
			}

			int clusterareanum = AAS_RetailClusterAreaNum(current->cluster,
				portal->areanum);
			if (clusterareanum < 0 || clusterareanum >= cluster->numareas)
			{
				continue;
			}

			unsigned short areatraveltime =
				areacache->traveltimes[clusterareanum];
			if (areatraveltime == 0)
			{
				continue;
			}

			unsigned short candidate = (unsigned short)(
				(unsigned int)areatraveltime +
				(unsigned int)current->tmptraveltime);
			unsigned short existing = cache->traveltimes[portalnum];
			if (existing != 0 && existing <= candidate)
			{
				continue;
			}

			cache->traveltimes[portalnum] = candidate;
			if (portal->areanum < 0 || portal->areanum >= aasworld.numAreas)
			{
				continue;
			}

			aas_retailportalupdate_t *nextupdate = &updates[portal->areanum];
			nextupdate->cluster = portal->frontcluster;
			if (nextupdate->cluster == current->cluster)
			{
				nextupdate->cluster = portal->backcluster;
			}
			nextupdate->areanum = portal->areanum;
			nextupdate->tmptraveltime = candidate;
			if (!nextupdate->inlist)
			{
				nextupdate->next = NULL;
				nextupdate->prev = tail;
				if (tail != NULL)
				{
					tail->next = nextupdate;
				}
				else
				{
					head = nextupdate;
				}
				tail = nextupdate;
				nextupdate->inlist = qtrue;
			}
		}
	}

	FreeMemory(updates);
}

/*
=============
AAS_RetailPortalCacheUpdateCount

Return the total number of retail portal-cache population passes.
=============
*/
int AAS_RetailPortalCacheUpdateCount(void)
{
	return g_retail_portal_cache_updates;
}

/*
=============
AAS_GetRetailPortalRoutingCache

Find or insert the travel-flags cache in an area's portal-cache list.
=============
*/
aas_retailroutingcache_t *AAS_GetRetailPortalRoutingCache(int clusternum,
	int areanum,
	int travelflags)
{
	if (aasworld.retailPortalCache == NULL || aasworld.areas == NULL ||
		areanum < 0 || areanum >= aasworld.numAreas)
	{
		return NULL;
	}

	aas_retailroutingcache_t **head = &aasworld.retailPortalCache[areanum];
	aas_retailroutingcache_t *cache = *head;
	while (cache != NULL && cache->travelflags != travelflags)
	{
		cache = cache->next;
	}

	if (cache == NULL)
	{
		cache = AAS_AllocRetailRoutingCache(aasworld.numPortals);
		if (cache == NULL)
		{
			return NULL;
		}

		cache->cluster = clusternum;
		cache->areanum = areanum;
		VectorCopy(aasworld.areas[areanum].center, cache->origin);
		cache->starttraveltime = 1.0f;
		cache->travelflags = travelflags;
		cache->prev = NULL;
		cache->next = *head;
		if (*head != NULL)
		{
			(*head)->prev = cache;
		}
		*head = cache;
		AAS_UpdateRetailPortalRoutingCache(cache);
	}

	cache->time = AAS_Time();
	return cache;
}

/*
=============
AAS_AgeRetailRoutingCacheList

Unlink and release entries whose last access is strictly beyond 15 seconds.
=============
*/
static void AAS_AgeRetailRoutingCacheList(aas_retailroutingcache_t **head,
	float expiration)
{
	if (head == NULL)
	{
		return;
	}

	aas_retailroutingcache_t *cache = *head;
	while (cache != NULL)
	{
		aas_retailroutingcache_t *next = cache->next;
		if (cache->time < expiration)
		{
			if (cache->prev == NULL)
			{
				*head = cache->next;
			}
			else
			{
				cache->prev->next = cache->next;
			}
			if (cache->next != NULL)
			{
				cache->next->prev = cache->prev;
			}
			AAS_FreeRetailRoutingCache(cache);
		}
		cache = next;
	}
}

/*
=============
AAS_AgeRetailRoutingCaches

Apply retail's 15-second aging pass to both cache-head table families.
=============
*/
void AAS_AgeRetailRoutingCaches(void)
{
	float expiration = AAS_Time() - RETAIL_ROUTECACHE_REFRESH_TIME;
	if (aasworld.retailClusterAreaCache != NULL && aasworld.clusters != NULL)
	{
		for (int clusternum = 0; clusternum < aasworld.numClusters; ++clusternum)
		{
			for (int clusterareanum = 0;
				clusterareanum < aasworld.clusters[clusternum].numareas;
				++clusterareanum)
			{
				AAS_AgeRetailRoutingCacheList(
					&aasworld.retailClusterAreaCache[clusternum][clusterareanum],
					expiration);
			}
		}
	}

	if (aasworld.retailPortalCache != NULL)
	{
		for (int areanum = 0; areanum < aasworld.numAreas; ++areanum)
		{
			AAS_AgeRetailRoutingCacheList(
				&aasworld.retailPortalCache[areanum],
				expiration);
		}
	}
}

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
	unsigned short starttime;
	unsigned short goaltime;
} aas_altroute_midrange_t;

typedef char aas_altroute_midrange_valid_offset[
	(offsetof(aas_altroute_midrange_t, valid) == 0U) ? 1 : -1];
typedef char aas_altroute_midrange_starttime_offset[
	(offsetof(aas_altroute_midrange_t, starttime) == 4U) ? 1 : -1];
typedef char aas_altroute_midrange_goaltime_offset[
	(offsetof(aas_altroute_midrange_t, goaltime) == 6U) ? 1 : -1];
typedef char aas_altroute_midrange_size[
	(sizeof(aas_altroute_midrange_t) == 8U) ? 1 : -1];

static aas_altroute_midrange_t *g_alternative_route_midrange;
static int *g_alternative_route_cluster_areas;
static int g_alternative_route_cluster_count;
static int g_alternative_route_scratch_capacity;

/*
=============
AAS_FreeAlternativeRoutingScratch

Release the two map-lifetime scratch arrays reconstructed at 0x1001ab80.
=============
*/
static void AAS_FreeAlternativeRoutingScratch(void)
{
	if (g_alternative_route_midrange != NULL)
	{
		FreeMemory(g_alternative_route_midrange);
	}
	if (g_alternative_route_cluster_areas != NULL)
	{
		FreeMemory(g_alternative_route_cluster_areas);
	}

	g_alternative_route_midrange = NULL;
	g_alternative_route_cluster_areas = NULL;
	g_alternative_route_cluster_count = 0;
	g_alternative_route_scratch_capacity = 0;
}

/*
=============
AAS_InitAlternativeRoutingScratch

Reallocate retail's 8-byte midrange records and 4-byte connected-area list.
=============
*/
static qboolean AAS_InitAlternativeRoutingScratch(void)
{
	AAS_FreeAlternativeRoutingScratch();

	if (aasworld.numAreas <= 0)
	{
		return qtrue;
	}

	size_t numareas = (size_t)aasworld.numAreas;
	if (numareas > SIZE_MAX / sizeof(*g_alternative_route_midrange) ||
		numareas > SIZE_MAX / sizeof(*g_alternative_route_cluster_areas))
	{
		return qfalse;
	}

	g_alternative_route_midrange =
		(aas_altroute_midrange_t *)GetClearedMemory(
			numareas * sizeof(*g_alternative_route_midrange));
	if (g_alternative_route_midrange == NULL)
	{
		return qfalse;
	}

	g_alternative_route_cluster_areas =
		(int *)GetClearedMemory(
			numareas * sizeof(*g_alternative_route_cluster_areas));
	if (g_alternative_route_cluster_areas == NULL)
	{
		AAS_FreeAlternativeRoutingScratch();
		return qfalse;
	}

	g_alternative_route_scratch_capacity = aasworld.numAreas;
	return qtrue;
}

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
    size_t numAreas = (aasworld.numAreas > 0) ? (size_t)aasworld.numAreas : 1U;

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

/*
=============
AAS_FreeRoutingQueryCaches

Release retail and compatibility query caches without map-lifetime scratch.
=============
*/
static void AAS_FreeRoutingQueryCaches(void)
{
	AAS_FreeRetailRoutingCaches();

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

/*
=============
AAS_FreeAllRoutingCaches

Release query caches and the alternative-routing arrays owned by the map.
=============
*/
void AAS_FreeAllRoutingCaches(void)
{
	AAS_FreeRoutingQueryCaches();
	AAS_FreeAlternativeRoutingScratch();
}

/*
=============
AAS_InvalidateRouteCache

Invalidate computed route results while retaining map-lifetime scratch.
=============
*/
void AAS_InvalidateRouteCache(void)
{
	AAS_FreeRoutingQueryCaches();
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
	    areanum >= aasworld.numAreas ||
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

    if (areanum <= 0 || areanum >= aasworld.numAreas)
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

    for (int area = 0; area < numAreas; ++area)
    {
        cache->traveltimes[area] = (unsigned short)ROUTE_INVALID_TIME;
        cache->reachabilities[area] = -1;
    }

    if (cache->goalArea <= 0 || cache->goalArea >= numAreas)
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

	cache->traveltimes[cache->goalArea] = 0;
	if (!Heap_Push(&heap, cache->goalArea, 0))
    {
        Heap_Destroy(&heap);
        return;
    }

    while (heap.size > 0)
    {
        routing_heap_node_t node = Heap_Pop(&heap);
        if (node.area <= 0 || node.area >= numAreas)
        {
            continue;
        }

        if (node.time != cache->traveltimes[node.area])
        {
            continue;
        }

		if (!AAS_AreaTravelAllowed(node.area, cache->travelflags))
		{
			continue;
		}

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
            if (startArea <= 0 || startArea >= numAreas)
            {
                continue;
            }

            int traveltype = aasworld.reachability[reachIndex].traveltype;
            int required = AAS_TravelFlagForType(traveltype);
            if ((required & cache->travelflags) != required)
            {
                continue;
            }

			unsigned int cost = node.time +
				aasworld.reachability[reachIndex].traveltime;
			int outgoingreach = cache->reachabilities[node.area];
			if (outgoingreach > 0 && outgoingreach < aasworld.numReachability)
			{
				cost += AAS_AreaTravelTime(node.area,
					aasworld.reachability[reachIndex].end,
					aasworld.reachability[outgoingreach].start);
			}
            if (cost >= cache->traveltimes[startArea])
            {
                continue;
            }

			cache->traveltimes[startArea] = (unsigned short)cost;
			cache->reachabilities[startArea] = reachIndex;
			if (!Heap_Push(&heap, startArea, cost))
            {
				cache->traveltimes[startArea] = (unsigned short)ROUTE_INVALID_TIME;
				cache->reachabilities[startArea] = -1;
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
	if (!aasworld.initialized)
	{
		return false;
	}

	if (areanum <= 0 || areanum >= aasworld.numAreas)
	{
		BotLib_Print(PRT_ERROR, "%s: areanum %d out of range\n", functionName, areanum);
		return false;
	}

	if (goalareanum <= 0 || goalareanum >= aasworld.numAreas)
	{
		BotLib_Print(PRT_ERROR, "%s: goalareanum %d out of range\n", functionName, goalareanum);
		return false;
	}

	return true;
}

/*
=============
AAS_RetailRoutingQueryAvailable

Require the complete retail cache graph before replacing the compatibility
route cache used by small synthetic worlds.
=============
*/
static qboolean AAS_RetailRoutingQueryAvailable(void)
{
	return aasworld.retailClusterAreaCache != NULL &&
		aasworld.retailPortalCache != NULL &&
		aasworld.areas != NULL &&
		aasworld.areasettings != NULL &&
		aasworld.clusters != NULL &&
		aasworld.reachability != NULL &&
		aasworld.reachabilityFromArea != NULL &&
		aasworld.reversedReachability != NULL &&
		aasworld.numAreas > 0 &&
		aasworld.numAreaSettings >= aasworld.numAreas &&
		aasworld.numClusters > 0;
}

/*
=============
AAS_RetailPublicTravelFlags

Project the successor API's shifted area-content flags onto Gladiator's
retail flag positions while leaving an explicit retail mask unchanged.
=============
*/
static int AAS_RetailPublicTravelFlags(int travelflags)
{
	if (travelflags == 0)
	{
		travelflags = TFL_DEFAULT;
	}

	int q3contents = travelflags &
		(TFL_AIR | TFL_WATER | TFL_SLIME | TFL_LAVA);
	if (q3contents == 0)
	{
		return travelflags;
	}

	int retailflags = travelflags & RETAIL_TFL_TRAVEL_MASK;
	if ((q3contents & TFL_AIR) != 0)
	{
		retailflags |= RETAIL_TFL_AIR;
	}
	if ((q3contents & TFL_WATER) != 0)
	{
		retailflags |= RETAIL_TFL_WATER;
	}
	if ((q3contents & TFL_SLIME) != 0)
	{
		retailflags |= RETAIL_TFL_SLIME;
	}
	if ((q3contents & TFL_LAVA) != 0)
	{
		retailflags |= RETAIL_TFL_LAVA;
	}

	return retailflags;
}

/*
=============
AAS_RetailAreaTravelTimeToGoalArea

Consume retail's cluster-area and portal caches using the exact branch order
and unsigned-short comparisons reconstructed at 0x10019fa0.
=============
*/
static unsigned short AAS_RetailAreaTravelTimeToGoalArea(int areanum,
	int goalareanum,
	int travelflags)
{
	if (!aasworld.initialized)
	{
		return 0;
	}

	if (areanum == goalareanum)
	{
		return 1;
	}

	if (!AAS_RouteValidAreaPair(areanum,
		goalareanum,
		"AAS_AreaTravelTimeToGoalArea"))
	{
		return 0;
	}

	if (g_retail_frame_routing_updates > 10)
	{
		return 0;
	}

	int areacluster = aasworld.areasettings[areanum].cluster;
	int goalcluster = aasworld.areasettings[goalareanum].cluster;
	int adjustedareacluster = areacluster;
	int adjustedgoalcluster = goalcluster;

	if (adjustedareacluster < 0 && adjustedgoalcluster > 0)
	{
		int portalnum = -adjustedareacluster;
		if (aasworld.portals != NULL && portalnum >= 0 &&
			portalnum < aasworld.numPortals)
		{
			const aas_portal_t *portal = &aasworld.portals[portalnum];
			if (portal->frontcluster == adjustedgoalcluster ||
				portal->backcluster == adjustedgoalcluster)
			{
				adjustedareacluster = adjustedgoalcluster;
			}
		}
	}
	else if (adjustedareacluster > 0 && adjustedgoalcluster < 0)
	{
		int portalnum = -adjustedgoalcluster;
		if (aasworld.portals != NULL && portalnum >= 0 &&
			portalnum < aasworld.numPortals)
		{
			const aas_portal_t *portal = &aasworld.portals[portalnum];
			if (portal->frontcluster == adjustedareacluster ||
				portal->backcluster == adjustedareacluster)
			{
				adjustedgoalcluster = adjustedareacluster;
			}
		}
	}

	if (adjustedareacluster > 0 && adjustedgoalcluster > 0 &&
		adjustedareacluster == adjustedgoalcluster &&
		adjustedareacluster < aasworld.numClusters)
	{
		aas_retailroutingcache_t *areacache =
			AAS_GetRetailAreaRoutingCache(adjustedareacluster,
				goalareanum,
				travelflags);
		int clusterareanum = AAS_RetailClusterAreaNum(adjustedareacluster,
			areanum);
		if (areacache != NULL && clusterareanum >= 0 &&
			clusterareanum <
				aasworld.clusters[adjustedareacluster].numareas)
		{
			unsigned short traveltime =
				areacache->traveltimes[clusterareanum];
			if (traveltime != 0)
			{
				return traveltime;
			}
		}
	}

	if (goalcluster < 0)
	{
		int portalnum = -goalcluster;
		if (aasworld.portals == NULL || portalnum < 0 ||
			portalnum >= aasworld.numPortals)
		{
			return 0;
		}
		goalcluster = aasworld.portals[portalnum].frontcluster;
	}
	if (goalcluster < 0 || goalcluster >= aasworld.numClusters)
	{
		return 0;
	}

	aas_retailroutingcache_t *portalcache =
		AAS_GetRetailPortalRoutingCache(goalcluster,
			goalareanum,
			travelflags);
	if (portalcache == NULL)
	{
		return 0;
	}

	if (areacluster < 0)
	{
		int portalnum = -areacluster;
		if (portalnum < 0 || portalnum >= aasworld.numPortals)
		{
			return 0;
		}
		return portalcache->traveltimes[portalnum];
	}
	if (areacluster < 0 || areacluster >= aasworld.numClusters)
	{
		return 0;
	}

	unsigned short besttime = 0;
	const aas_cluster_t *cluster = &aasworld.clusters[areacluster];
	for (int index = 0; index < cluster->numportals; ++index)
	{
		int portalindex = cluster->firstportal + index;
		if (aasworld.portalIndex == NULL || portalindex < 0 ||
			portalindex >= aasworld.portalIndexSize)
		{
			continue;
		}

		int portalnum = aasworld.portalIndex[portalindex];
		if (aasworld.portals == NULL || portalnum < 0 ||
			portalnum >= aasworld.numPortals ||
			portalcache->traveltimes[portalnum] == 0)
		{
			continue;
		}

		const aas_portal_t *portal = &aasworld.portals[portalnum];
		aas_retailroutingcache_t *areacache =
			AAS_GetRetailAreaRoutingCache(areacluster,
				portal->areanum,
				travelflags);
		int clusterareanum = AAS_RetailClusterAreaNum(areacluster,
			areanum);
		if (areacache == NULL || clusterareanum < 0 ||
			clusterareanum >= cluster->numareas ||
			areacache->traveltimes[clusterareanum] == 0)
		{
			continue;
		}

		unsigned short traveltime = (unsigned short)(
			(unsigned int)portalcache->traveltimes[portalnum] +
			(unsigned int)areacache->traveltimes[clusterareanum]);
		if (besttime == 0 || traveltime < besttime)
		{
			besttime = traveltime;
		}
	}

	return besttime;
}

/*
=============
AAS_RetailFirstReachabilityToGoalArea

Recover retail's first hop with the outgoing-order, strict-minimum scan used
by the bot movement caller around 0x100310e0.
=============
*/
static int AAS_RetailFirstReachabilityToGoalArea(int areanum,
	int goalareanum,
	int travelflags)
{
	unsigned int besttime = 0;
	int bestreachnum = 0;

	for (int reachnum = AAS_NextAreaReachability(areanum, 0);
		reachnum != 0;
		reachnum = AAS_NextAreaReachability(areanum, reachnum))
	{
		if (reachnum < 0 || reachnum >= aasworld.numReachability)
		{
			continue;
		}

		const aas_reachability_t *reach = &aasworld.reachability[reachnum];
		int required = AAS_RetailTravelFlagForType(reach->traveltype);
		if ((required & ~travelflags) != 0)
		{
			continue;
		}

		unsigned short routed = AAS_RetailAreaTravelTimeToGoalArea(
			reach->areanum,
			goalareanum,
			travelflags);
		if (routed == 0)
		{
			continue;
		}

		unsigned int traveltime = (unsigned int)routed +
			(unsigned int)reach->traveltime;
		if (besttime == 0 || traveltime < besttime)
		{
			besttime = traveltime;
			bestreachnum = reachnum;
		}
	}

	return bestreachnum;
}

/*
=============
AAS_AreaTravelTimeToGoalArea

Return the travel time from an area towards a goal area. Retail resolves a
query where start and goal match with the minimum travel time of one before
any range validation or travel flag filtering happens.
=============
*/
int AAS_AreaTravelTimeToGoalArea(int areanum, vec3_t origin, int goalareanum, int travelflags)
{
	if (!aasworld.initialized)
	{
		return 0;
	}

	if (areanum == goalareanum)
	{
		return 1;
	}

	if (!AAS_RouteValidAreaPair(areanum, goalareanum, "AAS_AreaTravelTimeToGoalArea"))
	{
		return 0;
	}

	if (AAS_RetailRoutingQueryAvailable())
	{
		int retailflags = AAS_RetailPublicTravelFlags(travelflags);
		unsigned short traveltime = AAS_RetailAreaTravelTimeToGoalArea(
			areanum,
			goalareanum,
			retailflags);
		if (traveltime == 0)
		{
			return 0;
		}
		if (origin == NULL)
		{
			return (int)traveltime;
		}

		int reachnum = AAS_RetailFirstReachabilityToGoalArea(areanum,
			goalareanum,
			retailflags);
		if (reachnum > 0 && reachnum < aasworld.numReachability)
		{
			return (int)traveltime + (int)AAS_AreaTravelTime(areanum,
				origin,
				aasworld.reachability[reachnum].start);
		}
		return (int)traveltime;
	}

	travelflags = AAS_RouteTravelFlags(areanum, goalareanum, travelflags);
	if (!AAS_AreaTravelAllowed(areanum, travelflags) ||
	    !AAS_AreaTravelAllowed(goalareanum, travelflags))
	{
		return 0;
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
Retail answers a query where start and goal match with reachability zero
before any range validation or travel flag filtering happens.
=============
*/
int AAS_AreaReachabilityToGoalArea(int areanum, vec3_t origin, int goalareanum, int travelflags)
{
	(void)origin;

	if (!aasworld.initialized)
	{
		return 0;
	}

	if (areanum == goalareanum)
	{
		return 0;
	}

	if (!AAS_RouteValidAreaPair(areanum, goalareanum, "AAS_AreaReachabilityToGoalArea"))
	{
		return 0;
	}

	if (AAS_RetailRoutingQueryAvailable())
	{
		int retailflags = AAS_RetailPublicTravelFlags(travelflags);
		if (AAS_RetailAreaTravelTimeToGoalArea(areanum,
			goalareanum,
			retailflags) == 0)
		{
			return 0;
		}
		return AAS_RetailFirstReachabilityToGoalArea(areanum,
			goalareanum,
			retailflags);
	}

	travelflags = AAS_RouteTravelFlags(areanum, goalareanum, travelflags);
	if (!AAS_AreaTravelAllowed(areanum, travelflags) ||
	    !AAS_AreaTravelAllowed(goalareanum, travelflags))
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
AAS_AlternativeRouteFloodCluster

Append one connected component in retail face order and consume its records.
=============
*/
static void AAS_AlternativeRouteFloodCluster(int areanum)
{
	if (areanum <= 0 ||
		areanum >= aasworld.numAreas ||
		g_alternative_route_midrange == NULL ||
		g_alternative_route_cluster_areas == NULL ||
		g_alternative_route_cluster_count >=
			g_alternative_route_scratch_capacity)
	{
		return;
	}

	g_alternative_route_cluster_areas[
		g_alternative_route_cluster_count++] = areanum;
	g_alternative_route_midrange[areanum].valid = qfalse;

	if (aasworld.areas == NULL ||
		aasworld.faces == NULL ||
		aasworld.faceIndex == NULL)
	{
		return;
	}

	const aas_area_t *area = &aasworld.areas[areanum];
	if (area->firstface < 0 || area->numfaces <= 0)
	{
		return;
	}

	for (int index = 0; index < area->numfaces; ++index)
	{
		int faceindex = area->firstface + index;
		if (faceindex < 0 || faceindex >= aasworld.faceIndexSize)
		{
			continue;
		}

		int signedfacenum = aasworld.faceIndex[faceindex];
		if (signedfacenum == INT_MIN)
		{
			continue;
		}

		int facenum = abs(signedfacenum);
		if (facenum <= 0 || facenum >= aasworld.numFaces)
		{
			continue;
		}

		const aas_face_t *face = &aasworld.faces[facenum];
		int otherareanum = (face->frontarea == areanum) ?
			face->backarea : face->frontarea;
		if (otherareanum <= 0 ||
			otherareanum >= aasworld.numAreas ||
			!g_alternative_route_midrange[otherareanum].valid)
		{
			continue;
		}

		AAS_AlternativeRouteFloodCluster(otherareanum);
	}
}

/*
=============
AAS_AlternativeRouteBestClusterArea

Choose the first flooded area strictly closest to the component mean.
=============
*/
static int AAS_AlternativeRouteBestClusterArea(void)
{
	if (g_alternative_route_cluster_areas == NULL ||
		g_alternative_route_cluster_count <= 0 ||
		aasworld.areas == NULL)
	{
		return 0;
	}

	vec3_t mid;
	VectorClear(mid);
	for (int index = 0;
		index < g_alternative_route_cluster_count;
		++index)
	{
		int areanum = g_alternative_route_cluster_areas[index];
		if (areanum <= 0 || areanum >= aasworld.numAreas)
		{
			continue;
		}

		VectorAdd(mid, aasworld.areas[areanum].center, mid);
	}
	VectorScale(mid,
		1.0f / (float)g_alternative_route_cluster_count,
		mid);

	float bestdist = 999999.0f;
	int bestareanum = 0;
	for (int index = 0;
		index < g_alternative_route_cluster_count;
		++index)
	{
		int areanum = g_alternative_route_cluster_areas[index];
		if (areanum <= 0 || areanum >= aasworld.numAreas)
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
AAS_AlternativeRouteGoals

Return retail route-portal representatives through the successor-shaped API.
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
	(void)startareanum;
	(void)goalareanum;
	(void)type;

	if (start == NULL ||
		goal == NULL ||
		altroutegoals == NULL ||
		maxaltroutegoals <= 0)
	{
		return 0;
	}

	int retailstartarea = AAS_PointAreaNum(start);
	if (retailstartarea == 0)
	{
		return 0;
	}

	int retailgoalarea = AAS_PointAreaNum(goal);
	if (retailgoalarea == 0)
	{
		return 0;
	}

	if (g_alternative_route_midrange == NULL ||
		g_alternative_route_cluster_areas == NULL ||
		g_alternative_route_scratch_capacity != aasworld.numAreas)
	{
		return 0;
	}

	unsigned short goaltraveltime = (unsigned short)
		AAS_AreaTravelTimeToGoalArea(retailstartarea,
			NULL,
			retailgoalarea,
			travelflags);
	double maxtraveltime = (double)goaltraveltime * 1.5;

	memset(g_alternative_route_midrange,
		0,
		(size_t)aasworld.numAreas *
			sizeof(*g_alternative_route_midrange));

	for (int areanum = 1; areanum < aasworld.numAreas; ++areanum)
	{
		if (aasworld.areasettings == NULL || areanum >= aasworld.numAreaSettings)
		{
			continue;
		}

		if ((aasworld.areasettings[areanum].contents &
			AAS_AREACONTENTS_ROUTEPORTAL) == 0)
		{
			continue;
		}

		if (AAS_AreaReachability(areanum) == 0)
		{
			continue;
		}

		unsigned short starttime = (unsigned short)
			AAS_AreaTravelTimeToGoalArea(retailstartarea,
				NULL,
				areanum,
				travelflags);
		if (starttime == 0)
		{
			continue;
		}

		if ((double)starttime > maxtraveltime)
		{
			continue;
		}

		unsigned short goaltime = (unsigned short)
			AAS_AreaTravelTimeToGoalArea(areanum,
				NULL,
				retailgoalarea,
				travelflags);
		if (goaltime == 0)
		{
			continue;
		}

		if ((double)goaltime > maxtraveltime)
		{
			continue;
		}

		g_alternative_route_midrange[areanum].valid = qtrue;
		g_alternative_route_midrange[areanum].starttime = starttime;
		g_alternative_route_midrange[areanum].goaltime = goaltime;
	}

	int numaltroutegoals = 0;
	for (int areanum = 1;
	     areanum < aasworld.numAreas && numaltroutegoals < maxaltroutegoals;
	     ++areanum)
	{
		if (!g_alternative_route_midrange[areanum].valid)
		{
			continue;
		}

		g_alternative_route_cluster_count = 0;
		AAS_AlternativeRouteFloodCluster(areanum);

		int bestareanum = AAS_AlternativeRouteBestClusterArea();
		if (bestareanum <= 0)
		{
			continue;
		}

		aas_altroutegoal_t *routegoal = &altroutegoals[numaltroutegoals];
		VectorCopy(aasworld.areas[bestareanum].center, routegoal->origin);
		routegoal->areanum = bestareanum;
		routegoal->starttraveltime =
			g_alternative_route_midrange[bestareanum].starttime;
		routegoal->goaltraveltime =
			g_alternative_route_midrange[bestareanum].goaltime;
		unsigned short combinedtime = (unsigned short)(
			(unsigned int)routegoal->starttraveltime +
			(unsigned int)routegoal->goaltraveltime);
		routegoal->extratraveltime = (unsigned short)(
			(unsigned int)combinedtime - (unsigned int)goaltraveltime);

		numaltroutegoals += 1;
	}

	BotLib_Print(PRT_MESSAGE,
		"%d alternative route goals\n",
		numaltroutegoals);
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

	return (int)(((double)rand() / ((double)RAND_MAX + 1.0)) *
		(double)aasworld.numAreas);
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
	    areanum >= aasworld.numAreas)
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
		if (candidate <= 0 || candidate >= aasworld.numAreas)
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
	    areanum >= aasworld.numAreas ||
	    areanum >= aasworld.numAreaSettings)
	{
		return 0;
	}

	size_t numareas = (size_t)aasworld.numAreas;
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
		    curareanum >= aasworld.numAreas ||
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
			    nextareanum >= aasworld.numAreas ||
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

/*
=============
AAS_RouteFrameResetDiagnostics

Reset routing-frame diagnostics and retail update accounting.
=============
*/
void AAS_RouteFrameResetDiagnostics(void)
{
	memset(&g_route_frame_state, 0, sizeof(g_route_frame_state));
	g_retail_area_cache_updates = 0;
	g_retail_portal_cache_updates = 0;
	g_retail_frame_routing_updates = 0;
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

/*
=============
AAS_RouteFrameUpdate

Begin a routing frame and perform the compatibility cache maintenance pass.
=============
*/
void AAS_RouteFrameUpdate(void)
{
	g_retail_frame_routing_updates = 0;
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
