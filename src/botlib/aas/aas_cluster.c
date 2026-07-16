#include "aas_local.h"

#include <stdlib.h>
#include <string.h>

#include "botlib/common/l_log.h"
#include "botlib/common/l_memory.h"
#include "q2bridge/bridge_config.h"

#define AAS_MAX_PORTALS 65536
#define AAS_MAX_PORTALINDEXSIZE 65536
#define AAS_MAX_CLUSTERS 65536
#define MAX_PORTALAREAS 128

/*
=============
AAS_RemoveClusterAreas

Clear the cluster assignment of every real AAS area.
=============
*/
static void AAS_RemoveClusterAreas(void)
{
	if (aasworld.areasettings == NULL)
	{
		return;
	}

	for (int areanum = 1;
		areanum < aasworld.numAreas && areanum < aasworld.numAreaSettings;
		++areanum)
	{
		aasworld.areasettings[areanum].cluster = 0;
	}
}

/*
=============
AAS_UpdatePortal

Attach a portal area to one side of a cluster and append its portal index.
=============
*/
static int AAS_UpdatePortal(int areanum, int clusternum)
{
	int portalnum;

	for (portalnum = 1; portalnum < aasworld.numPortals; ++portalnum)
	{
		if (aasworld.portals[portalnum].areanum == areanum)
		{
			break;
		}
	}

	if (portalnum == aasworld.numPortals)
	{
		BotLib_Print(PRT_ERROR, "no portal of area %d\n", areanum);
		return qtrue;
	}

	aas_portal_t *portal = &aasworld.portals[portalnum];
	if (portal->frontcluster == clusternum || portal->backcluster == clusternum)
	{
		return qtrue;
	}

	if (portal->frontcluster == 0)
	{
		portal->frontcluster = clusternum;
	}
	else if (portal->backcluster == 0)
	{
		portal->backcluster = clusternum;
	}
	else
	{
		BotLib_LogWrite("portal using area %d is seperating more than two clusters\r\n",
			areanum);
		aasworld.areasettings[areanum].contents &= ~AAS_AREACONTENTS_CLUSTERPORTAL;
		return qfalse;
	}

	if (aasworld.portalIndexSize >= AAS_MAX_PORTALINDEXSIZE)
	{
		BotLib_Print(PRT_ERROR, "AAS_MAX_PORTALINDEXSIZE\n");
		return qtrue;
	}

	if (clusternum <= 0 || clusternum >= AAS_MAX_CLUSTERS)
	{
		BotLib_Print(PRT_ERROR, "AAS_UpdatePortal: cluster number out of range\n");
		return qfalse;
	}

	aas_cluster_t *cluster = &aasworld.clusters[clusternum];
	int portalindex = cluster->firstportal + cluster->numportals;
	if (portalindex < 0 || portalindex >= AAS_MAX_PORTALINDEXSIZE)
	{
		BotLib_Print(PRT_ERROR, "AAS_UpdatePortal: portal index out of range\n");
		return qfalse;
	}

	aasworld.areasettings[areanum].cluster = -portalnum;
	aasworld.portalIndex[portalindex] = portalnum;
	aasworld.portalIndexSize += 1;
	cluster->numportals += 1;
	return qtrue;
}

/*
=============
AAS_FloodClusterAreas

Flood a retail cluster through shared area faces and outgoing reachabilities.
=============
*/
static int AAS_FloodClusterAreas(int areanum, int clusternum)
{
	if (areanum <= 0 || areanum >= aasworld.numAreas)
	{
		BotLib_Print(PRT_ERROR,
			"AAS_FloodClusterAreas_r: areanum out of range\n");
		return qfalse;
	}

	aas_areasettings_t *settings = &aasworld.areasettings[areanum];
	if (settings->cluster > 0)
	{
		if (settings->cluster == clusternum)
		{
			return qtrue;
		}

		BotLib_LogWrite("cluster %d touched cluster %d at area %d\r\n",
			clusternum,
			settings->cluster,
			areanum);
		return qfalse;
	}

	if ((settings->contents & AAS_AREACONTENTS_CLUSTERPORTAL) != 0)
	{
		return AAS_UpdatePortal(areanum, clusternum);
	}

	settings->cluster = clusternum;
	settings->clusterareanum = aasworld.clusters[clusternum].numareas;
	aasworld.clusters[clusternum].numareas += 1;

	aas_area_t *area = &aasworld.areas[areanum];
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

		aas_face_t *face = &aasworld.faces[facenum];
		int otherareanum;
		if (face->frontarea == areanum)
		{
			otherareanum = face->backarea;
		}
		else
		{
			otherareanum = face->frontarea;
		}

		if (otherareanum != 0 &&
			!AAS_FloodClusterAreas(otherareanum, clusternum))
		{
			return qfalse;
		}
	}

	for (int index = 0; index < settings->numreachableareas; ++index)
	{
		int reachnum = settings->firstreachablearea + index;
		if (reachnum < 0 || reachnum >= aasworld.numReachability)
		{
			continue;
		}

		int otherareanum = aasworld.reachability[reachnum].areanum;
		if (otherareanum != 0 &&
			!AAS_FloodClusterAreas(otherareanum, clusternum))
		{
			return qfalse;
		}
	}

	return qtrue;
}

/*
=============
AAS_FloodClusterAreasUsingReachabilities

Backfill areas whose one-way reachabilities lead into the current cluster.
=============
*/
static int AAS_FloodClusterAreasUsingReachabilities(int clusternum)
{
	for (int areanum = 1;
		areanum < aasworld.numAreas && areanum < aasworld.numAreaSettings;
		++areanum)
	{
		aas_areasettings_t *settings = &aasworld.areasettings[areanum];
		if (settings->cluster != 0 ||
			(settings->contents & AAS_AREACONTENTS_CLUSTERPORTAL) != 0)
		{
			continue;
		}

		for (int index = 0; index < settings->numreachableareas; ++index)
		{
			int reachnum = settings->firstreachablearea + index;
			if (reachnum < 0 || reachnum >= aasworld.numReachability)
			{
				continue;
			}

			int destination = aasworld.reachability[reachnum].areanum;
			if (destination <= 0 || destination >= aasworld.numAreaSettings)
			{
				continue;
			}
			if ((aasworld.areasettings[destination].contents &
				AAS_AREACONTENTS_CLUSTERPORTAL) != 0)
			{
				continue;
			}
			if (aasworld.areasettings[destination].cluster == 0)
			{
				continue;
			}

			if (!AAS_FloodClusterAreas(areanum, clusternum))
			{
				return qfalse;
			}
			areanum = 0;
			break;
		}
	}

	return qtrue;
}

/*
=============
AAS_NumberClusterPortals

Number each portal after the regular areas in its two adjacent clusters.
=============
*/
static void AAS_NumberClusterPortals(int clusternum)
{
	aas_cluster_t *cluster = &aasworld.clusters[clusternum];
	for (int index = 0; index < cluster->numportals; ++index)
	{
		int portalindex = cluster->firstportal + index;
		if (portalindex < 0 || portalindex >= aasworld.portalIndexSize)
		{
			continue;
		}

		int portalnum = aasworld.portalIndex[portalindex];
		if (portalnum <= 0 || portalnum >= aasworld.numPortals)
		{
			continue;
		}

		aas_portal_t *portal = &aasworld.portals[portalnum];
		if (portal->frontcluster == clusternum)
		{
			portal->clusterareanum[0] = cluster->numareas;
		}
		else
		{
			portal->clusterareanum[1] = cluster->numareas;
		}
		cluster->numareas += 1;
	}
}

/*
=============
AAS_FindClusters

Build all clusters and their portal lists from the current portal markings.
=============
*/
static int AAS_FindClusters(void)
{
	AAS_RemoveClusterAreas();

	for (int areanum = 1;
		areanum < aasworld.numAreas && areanum < aasworld.numAreaSettings;
		++areanum)
	{
		aas_areasettings_t *settings = &aasworld.areasettings[areanum];
		if (settings->cluster != 0 ||
			(settings->contents & AAS_AREACONTENTS_CLUSTERPORTAL) != 0)
		{
			continue;
		}

		if (aasworld.numClusters >= AAS_MAX_CLUSTERS)
		{
			BotLib_Print(PRT_ERROR, "AAS_MAX_CLUSTERS\n");
			return qfalse;
		}

		aas_cluster_t *cluster = &aasworld.clusters[aasworld.numClusters];
		cluster->numareas = 0;
		cluster->firstportal = aasworld.portalIndexSize;
		cluster->numportals = 0;

		if (!AAS_FloodClusterAreas(areanum, aasworld.numClusters) ||
			!AAS_FloodClusterAreasUsingReachabilities(aasworld.numClusters))
		{
			return qfalse;
		}

		AAS_NumberClusterPortals(aasworld.numClusters);
		BotLib_LogWrite("cluster %d has %d areas\r\n",
			aasworld.numClusters,
			cluster->numareas);
		aasworld.numClusters += 1;
	}

	return qtrue;
}

/*
=============
AAS_CreatePortals

Create one portal record for every area marked as a cluster portal.
=============
*/
static void AAS_CreatePortals(void)
{
	for (int areanum = 1;
		areanum < aasworld.numAreas && areanum < aasworld.numAreaSettings;
		++areanum)
	{
		if ((aasworld.areasettings[areanum].contents &
			AAS_AREACONTENTS_CLUSTERPORTAL) == 0)
		{
			continue;
		}

		if (aasworld.numPortals >= AAS_MAX_PORTALS)
		{
			BotLib_Print(PRT_ERROR, "AAS_MAX_PORTALS\n");
			return;
		}

		aas_portal_t *portal = &aasworld.portals[aasworld.numPortals];
		portal->areanum = areanum;
		portal->frontcluster = 0;
		portal->backcluster = 0;
		portal->clusterareanum[0] = 0;
		portal->clusterareanum[1] = 0;
		BotLib_LogWrite("portal %d: area %d\r\n", aasworld.numPortals, areanum);
		aasworld.numPortals += 1;
	}
}

/*
=============
AAS_ConnectedAreasRecursive

Mark members of an area set that are connected through non-solid faces.
=============
*/
static void AAS_ConnectedAreasRecursive(const int *areanums,
	int numareas,
	int *connectedareas,
	int current)
{
	connectedareas[current] = qtrue;
	int areanum = areanums[current];
	if (areanum <= 0 || areanum >= aasworld.numAreas)
	{
		return;
	}

	aas_area_t *area = &aasworld.areas[areanum];
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

		aas_face_t *face = &aasworld.faces[facenum];
		if ((face->faceflags & AAS_FACE_SOLID) != 0)
		{
			continue;
		}

		int otherareanum = face->frontarea != areanum
			? face->frontarea
			: face->backarea;
		int otherindex;
		for (otherindex = 0; otherindex < numareas; ++otherindex)
		{
			if (areanums[otherindex] == otherareanum)
			{
				break;
			}
		}

		if (otherindex != numareas && connectedareas[otherindex] == 0)
		{
			AAS_ConnectedAreasRecursive(areanums,
				numareas,
				connectedareas,
				otherindex);
		}
	}
}

/*
=============
AAS_ConnectedAreas

Return true when every area in a bounded portal-side set is connected.
=============
*/
static int AAS_ConnectedAreas(const int *areanums, int numareas)
{
	int connectedareas[MAX_PORTALAREAS];
	memset(connectedareas, 0, sizeof(connectedareas));

	if (numareas < 1)
	{
		return qfalse;
	}
	if (numareas == 1)
	{
		return qtrue;
	}

	AAS_ConnectedAreasRecursive(areanums, numareas, connectedareas, 0);
	for (int index = 0; index < numareas; ++index)
	{
		if (connectedareas[index] == 0)
		{
			return qfalse;
		}
	}
	return qtrue;
}

/*
=============
AAS_GetAdjacentAreasWithLessPresenceTypes

Collect adjoining areas whose presence types are a strict subset of the source.
=============
*/
static int AAS_GetAdjacentAreasWithLessPresenceTypes(int *areanums,
	int numareas,
	int currentareanum)
{
	if (numareas >= MAX_PORTALAREAS)
	{
		BotLib_Print(PRT_ERROR, "MAX_PORTALAREAS\n");
		return numareas;
	}

	areanums[numareas++] = currentareanum;
	aas_area_t *area = &aasworld.areas[currentareanum];
	int presencetype = aasworld.areasettings[currentareanum].presencetype;

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

		aas_face_t *face = &aasworld.faces[facenum];
		if ((face->faceflags & AAS_FACE_SOLID) != 0)
		{
			continue;
		}

		int otherareanum = face->frontarea != currentareanum
			? face->frontarea
			: face->backarea;
		if (otherareanum <= 0 || otherareanum >= aasworld.numAreas ||
			otherareanum >= aasworld.numAreaSettings)
		{
			continue;
		}

		int otherpresencetype = aasworld.areasettings[otherareanum].presencetype;
		if ((presencetype & ~otherpresencetype) == 0 ||
			(otherpresencetype & ~presencetype) != 0)
		{
			continue;
		}

		int found;
		for (found = 0; found < numareas; ++found)
		{
			if (areanums[found] == otherareanum)
			{
				break;
			}
		}
		if (found != numareas)
		{
			continue;
		}

		if (numareas >= MAX_PORTALAREAS)
		{
			BotLib_Print(PRT_ERROR, "MAX_PORTALAREAS\n");
			return numareas;
		}
		numareas = AAS_GetAdjacentAreasWithLessPresenceTypes(areanums,
			numareas,
			otherareanum);
	}

	return numareas;
}

/*
=============
AAS_CheckAreaForPossiblePortal

Recognize the retail two-plane grounded choke geometry and mark its area set.
=============
*/
static int AAS_CheckAreaForPossiblePortal(int areanum)
{
	if ((aasworld.areasettings[areanum].contents &
		AAS_AREACONTENTS_CLUSTERPORTAL) != 0)
	{
		return 0;
	}
	if ((aasworld.areasettings[areanum].areaflags & AAS_AREA_GROUNDED) == 0)
	{
		return 0;
	}

	int areanums[MAX_PORTALAREAS];
	int numareafrontfaces[MAX_PORTALAREAS];
	int numareabackfaces[MAX_PORTALAREAS];
	int frontfacenums[MAX_PORTALAREAS];
	int backfacenums[MAX_PORTALAREAS];
	int frontareanums[MAX_PORTALAREAS];
	int backareanums[MAX_PORTALAREAS];
	memset(numareafrontfaces, 0, sizeof(numareafrontfaces));
	memset(numareabackfaces, 0, sizeof(numareabackfaces));

	int numareas = AAS_GetAdjacentAreasWithLessPresenceTypes(areanums,
		0,
		areanum);
	int numfrontfaces = 0;
	int numbackfaces = 0;
	int numfrontareas = 0;
	int numbackareas = 0;
	int frontplanenum = -1;
	int backplanenum = -1;

	for (int areaindex = 0; areaindex < numareas; ++areaindex)
	{
		aas_area_t *area = &aasworld.areas[areanums[areaindex]];
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

			aas_face_t *face = &aasworld.faces[facenum];
			if ((face->faceflags & AAS_FACE_SOLID) != 0)
			{
				continue;
			}

			int shared;
			for (shared = 0; shared < numareas; ++shared)
			{
				if (shared == areaindex)
				{
					continue;
				}
				if (face->frontarea == areanums[shared] ||
					face->backarea == areanums[shared])
				{
					break;
				}
			}
			if (shared != numareas)
			{
				continue;
			}

			int otherareanum = face->frontarea == areanums[areaindex]
				? face->backarea
				: face->frontarea;
			if (otherareanum < 0 || otherareanum >= aasworld.numAreaSettings)
			{
				return 0;
			}
			if ((aasworld.areasettings[otherareanum].contents &
				AAS_AREACONTENTS_CLUSTERPORTAL) != 0)
			{
				return 0;
			}

			int faceplanenum = face->planenum & ~1;
			if (frontplanenum < 0 || faceplanenum == frontplanenum)
			{
				if (numfrontfaces >= MAX_PORTALAREAS)
				{
					return 0;
				}
				frontplanenum = faceplanenum;
				frontfacenums[numfrontfaces++] = facenum;
				int found;
				for (found = 0; found < numfrontareas; ++found)
				{
					if (frontareanums[found] == otherareanum)
					{
						break;
					}
				}
				if (found == numfrontareas)
				{
					if (numfrontareas >= MAX_PORTALAREAS)
					{
						return 0;
					}
					frontareanums[numfrontareas++] = otherareanum;
				}
				numareafrontfaces[areaindex] += 1;
			}
			else if (backplanenum < 0 || faceplanenum == backplanenum)
			{
				if (numbackfaces >= MAX_PORTALAREAS)
				{
					return 0;
				}
				backplanenum = faceplanenum;
				backfacenums[numbackfaces++] = facenum;
				int found;
				for (found = 0; found < numbackareas; ++found)
				{
					if (backareanums[found] == otherareanum)
					{
						break;
					}
				}
				if (found == numbackareas)
				{
					if (numbackareas >= MAX_PORTALAREAS)
					{
						return 0;
					}
					backareanums[numbackareas++] = otherareanum;
				}
				numareabackfaces[areaindex] += 1;
			}
			else
			{
				return 0;
			}
		}
	}

	for (int areaindex = 0; areaindex < numareas; ++areaindex)
	{
		if (numareafrontfaces[areaindex] == 0 ||
			numareabackfaces[areaindex] == 0)
		{
			return 0;
		}
	}
	if (!AAS_ConnectedAreas(frontareanums, numfrontareas) ||
		!AAS_ConnectedAreas(backareanums, numbackareas))
	{
		return 0;
	}

	for (int frontindex = 0; frontindex < numfrontfaces; ++frontindex)
	{
		aas_face_t *frontface = &aasworld.faces[frontfacenums[frontindex]];
		for (int frontedge = 0; frontedge < frontface->numedges; ++frontedge)
		{
			int edgeindex = frontface->firstedge + frontedge;
			if (edgeindex < 0 || edgeindex >= aasworld.edgeIndexSize)
			{
				continue;
			}
			int frontedgenum = abs(aasworld.edgeIndex[edgeindex]);

			for (int backindex = 0; backindex < numbackfaces; ++backindex)
			{
				aas_face_t *backface = &aasworld.faces[backfacenums[backindex]];
				for (int backedge = 0; backedge < backface->numedges; ++backedge)
				{
					int backedgeindex = backface->firstedge + backedge;
					if (backedgeindex < 0 ||
						backedgeindex >= aasworld.edgeIndexSize)
					{
						continue;
					}
					if (frontedgenum ==
						abs(aasworld.edgeIndex[backedgeindex]))
					{
						return 0;
					}
				}
			}
		}
	}

	for (int areaindex = 0; areaindex < numareas; ++areaindex)
	{
		int portalarea = areanums[areaindex];
		aasworld.areasettings[portalarea].contents |=
			AAS_AREACONTENTS_CLUSTERPORTAL;
		aasworld.areasettings[portalarea].contents |=
			AAS_AREACONTENTS_ROUTEPORTAL;
		BotLib_LogWrite("possible portal: %d\r\n", portalarea);
	}

	return numareas;
}

/*
=============
AAS_FindPossiblePortals

Scan all areas for retail geometric cluster-portal candidates.
=============
*/
static void AAS_FindPossiblePortals(void)
{
	int possibleportals = 0;
	for (int areanum = 1;
		areanum < aasworld.numAreas && areanum < aasworld.numAreaSettings;
		++areanum)
	{
		possibleportals += AAS_CheckAreaForPossiblePortal(areanum);
	}
	BotLib_Print(PRT_MESSAGE, "\r%6d possible portals\n", possibleportals);
}

/*
=============
AAS_RemoveAllPortals

Remove every existing cluster-portal mark before geometric regeneration.
=============
*/
static void AAS_RemoveAllPortals(void)
{
	for (int areanum = 1;
		areanum < aasworld.numAreas && areanum < aasworld.numAreaSettings;
		++areanum)
	{
		aasworld.areasettings[areanum].contents &=
			~AAS_AREACONTENTS_CLUSTERPORTAL;
	}
}

/*
=============
AAS_TestPortals

Reject the first portal that does not separate exactly two clusters.
=============
*/
static int AAS_TestPortals(void)
{
	for (int portalnum = 1; portalnum < aasworld.numPortals; ++portalnum)
	{
		aas_portal_t *portal = &aasworld.portals[portalnum];
		if (portal->frontcluster == 0)
		{
			aasworld.areasettings[portal->areanum].contents &=
				~AAS_AREACONTENTS_CLUSTERPORTAL;
			BotLib_LogWrite("portal area %d has no front cluster\r\n",
				portal->areanum);
			return qfalse;
		}
		if (portal->backcluster == 0)
		{
			aasworld.areasettings[portal->areanum].contents &=
				~AAS_AREACONTENTS_CLUSTERPORTAL;
			BotLib_LogWrite("portal area %d has no back cluster\r\n",
				portal->areanum);
			return qfalse;
		}
	}
	return qtrue;
}

/*
=============
AAS_InitClustering

Rebuild the retail geometric portals, clusters, and cluster-local numbering.
=============
*/
void AAS_InitClustering(void)
{
	if (!aasworld.loaded || aasworld.areas == NULL ||
		aasworld.areasettings == NULL)
	{
		return;
	}

	libvar_t *forceclustering = Bridge_ForceClustering();
	libvar_t *forcereachability = Bridge_ForceReachability();
	int forcecluster = forceclustering != NULL && forceclustering->value != 0.0f;
	int forcereach = forcereachability != NULL &&
		forcereachability->value != 0.0f;
	if (aasworld.numClusters >= 1 && !forcecluster && !forcereach)
	{
		return;
	}

	AAS_RemoveAllPortals();
	AAS_RemoveClusterAreas();
	AAS_FindPossiblePortals();

	FreeMemory(aasworld.portals);
	FreeMemory(aasworld.portalIndex);
	FreeMemory(aasworld.clusters);
	aasworld.portals = (aas_portal_t *)GetClearedMemory(
		AAS_MAX_PORTALS * sizeof(aas_portal_t));
	aasworld.portalIndex = (int *)GetClearedMemory(
		AAS_MAX_PORTALINDEXSIZE * sizeof(int));
	aasworld.clusters = (aas_cluster_t *)GetClearedMemory(
		AAS_MAX_CLUSTERS * sizeof(aas_cluster_t));
	if (aasworld.portals == NULL || aasworld.portalIndex == NULL ||
		aasworld.clusters == NULL)
	{
		BotLib_Print(PRT_ERROR,
			"AAS_InitClustering: unable to allocate cluster data\n");
		FreeMemory(aasworld.portals);
		FreeMemory(aasworld.portalIndex);
		FreeMemory(aasworld.clusters);
		aasworld.portals = NULL;
		aasworld.portalIndex = NULL;
		aasworld.clusters = NULL;
		aasworld.numPortals = 0;
		aasworld.portalIndexSize = 0;
		aasworld.numClusters = 0;
		return;
	}

	int attempts = 0;
	for (;;)
	{
		aasworld.numPortals = 1;
		aasworld.portalIndexSize = 0;
		aasworld.numClusters = 1;
		AAS_CreatePortals();

		if (AAS_FindClusters() && AAS_TestPortals())
		{
			break;
		}

		attempts += 1;
		if (attempts > aasworld.numAreas)
		{
			BotLib_Print(PRT_ERROR,
				"AAS_InitClustering: cluster rebuild did not converge\n");
			return;
		}
	}

	aasworld.saveFile = qtrue;
	BotLib_Print(PRT_MESSAGE, "\r%6d portals created\n", aasworld.numPortals);
	BotLib_Print(PRT_MESSAGE, "\r%6d clusters created\n", aasworld.numClusters);
}
