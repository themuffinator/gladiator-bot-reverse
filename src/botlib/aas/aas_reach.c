#include "aas_local.h"

#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "botlib/common/l_log.h"
#include "botlib/common/l_libvar.h"
#include "q2bridge/bridge_config.h"

#define MAX_REACHABILITYPASSAREAS 32

typedef struct
{
    int frames_with_work;
    int frames_skipped;
    bool force_reachability_active;
    bool force_clustering_active;
} aas_reachability_frame_state_t;

static aas_reachability_frame_state_t g_reach_frame_state;

/*
=============
AAS_CrossProduct

Compute the vector cross product used by AAS face geometry helpers.
=============
*/
static void AAS_CrossProduct(const vec3_t v1, const vec3_t v2, vec3_t result)
{
	result[0] = v1[1] * v2[2] - v1[2] * v2[1];
	result[1] = v1[2] * v2[0] - v1[0] * v2[2];
	result[2] = v1[0] * v2[1] - v1[1] * v2[0];
}

/*
=============
AAS_VectorLength

Return the Euclidean length of a vector.
=============
*/
static float AAS_VectorLength(const vec3_t vector)
{
	return sqrtf(DotProduct(vector, vector));
}

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

/*
=============
AAS_FreeReachabilityAreas

Release generated reachability pass-area lookup tables.
=============
*/
static void AAS_FreeReachabilityAreas(void)
{
	free(aasworld.reachabilityAreas);
	aasworld.reachabilityAreas = NULL;

	free(aasworld.reachabilityAreaIndex);
	aasworld.reachabilityAreaIndex = NULL;
	aasworld.reachabilityAreaIndexSize = 0;
}

void AAS_ClearReachabilityData(void)
{
    AAS_FreeReverseReachability();
    AAS_FreeReachabilityAreas();
    free(aasworld.reachabilityFromArea);
    aasworld.reachabilityFromArea = NULL;
}

/*
=============
AAS_AreaReachability

Return the number of reachability links attached to an area.
=============
*/
int AAS_AreaReachability(int areanum)
{
	if (aasworld.areasettings == NULL || areanum < 0 || areanum >= aasworld.numAreaSettings)
	{
		BotLib_Print(PRT_ERROR, "AAS_AreaReachability: areanum %d out of range\n", areanum);
		return 0;
	}

	return aasworld.areasettings[areanum].numreachableareas;
}

/*
=============
AAS_AreaCluster

Return the cluster assigned to an area.
=============
*/
int AAS_AreaCluster(int areanum)
{
	if (aasworld.areasettings == NULL || areanum <= 0 || areanum >= aasworld.numAreaSettings)
	{
		BotLib_Print(PRT_ERROR, "AAS_AreaCluster: invalid area number\n");
		return 0;
	}

	return aasworld.areasettings[areanum].cluster;
}

/*
=============
AAS_AreaPresenceType

Return the presence mask stored in an area setting.
=============
*/
int AAS_AreaPresenceType(int areanum)
{
	if (!aasworld.loaded)
	{
		return 0;
	}

	if (aasworld.areasettings == NULL || areanum <= 0 || areanum >= aasworld.numAreaSettings)
	{
		BotLib_Print(PRT_ERROR, "AAS_AreaPresenceType: invalid area number\n");
		return 0;
	}

	return aasworld.areasettings[areanum].presencetype;
}

/*
=============
AAS_AreaInfo

Copy the reconstructed public area information record.
=============
*/
int AAS_AreaInfo(int areanum, aas_areainfo_t *info)
{
	if (info == NULL)
	{
		return 0;
	}

	if (aasworld.areas == NULL ||
	    aasworld.areasettings == NULL ||
	    areanum <= 0 ||
	    areanum > aasworld.numAreas ||
	    areanum >= aasworld.numAreaSettings)
	{
		BotLib_Print(PRT_ERROR, "AAS_AreaInfo: areanum %d out of range\n", areanum);
		return 0;
	}

	const aas_area_t *area = &aasworld.areas[areanum];
	const aas_areasettings_t *settings = &aasworld.areasettings[areanum];
	info->cluster = settings->cluster;
	info->contents = settings->contents;
	info->flags = settings->areaflags;
	info->presencetype = settings->presencetype;
	VectorCopy(area->mins, info->mins);
	VectorCopy(area->maxs, info->maxs);
	VectorCopy(area->center, info->center);
	return (int)sizeof(aas_areainfo_t);
}

/*
=============
AAS_AreaCrouch

Return true when an area lacks normal-standing presence.
=============
*/
int AAS_AreaCrouch(int areanum)
{
	if (aasworld.areasettings == NULL || areanum <= 0 || areanum >= aasworld.numAreaSettings)
	{
		return qfalse;
	}

	return (aasworld.areasettings[areanum].presencetype & PRESENCE_NORMAL) == 0;
}

/*
=============
AAS_AreaSwim

Return true when an area is flagged as liquid.
=============
*/
int AAS_AreaSwim(int areanum)
{
	if (aasworld.areasettings == NULL || areanum <= 0 || areanum >= aasworld.numAreaSettings)
	{
		return qfalse;
	}

	return (aasworld.areasettings[areanum].areaflags & AAS_AREA_LIQUID) != 0;
}

/*
=============
AAS_AreaGrounded

Return true when an area is flagged as grounded.
=============
*/
int AAS_AreaGrounded(int areanum)
{
	if (aasworld.areasettings == NULL || areanum <= 0 || areanum >= aasworld.numAreaSettings)
	{
		return qfalse;
	}

	return (aasworld.areasettings[areanum].areaflags & AAS_AREA_GROUNDED) != 0;
}

/*
=============
AAS_AreaLadder

Return true when an area is flagged as ladder movement.
=============
*/
int AAS_AreaLadder(int areanum)
{
	return AAS_AreaHasLadder(areanum) ? qtrue : qfalse;
}

/*
=============
AAS_AreaJumpPad

Return true when an area carries jump-pad contents.
=============
*/
int AAS_AreaJumpPad(int areanum)
{
	if (aasworld.areasettings == NULL || areanum <= 0 || areanum >= aasworld.numAreaSettings)
	{
		return qfalse;
	}

	return (aasworld.areasettings[areanum].contents & AAS_AREACONTENTS_JUMPPAD) != 0;
}

/*
=============
AAS_AreaClusterPortal

Return true when an area carries cluster-portal contents.
=============
*/
int AAS_AreaClusterPortal(int areanum)
{
	if (aasworld.areasettings == NULL || areanum <= 0 || areanum >= aasworld.numAreaSettings)
	{
		return qfalse;
	}

	return (aasworld.areasettings[areanum].contents & AAS_AREACONTENTS_CLUSTERPORTAL) != 0;
}

/*
=============
AAS_PointReachabilityAreaIndex

Return the cluster-local reachability area index for a point.
=============
*/
int AAS_PointReachabilityAreaIndex(const vec3_t origin)
{
	if (!aasworld.initialized)
	{
		return 0;
	}

	if (origin == NULL)
	{
		int index = 0;
		if (aasworld.clusters == NULL)
		{
			return 0;
		}

		for (int cluster = 0; cluster < aasworld.numClusters; ++cluster)
		{
			index += aasworld.clusters[cluster].numreachabilityareas;
		}
		return index;
	}

	int areanum = AAS_PointAreaNum(origin);
	if (areanum == 0 || AAS_AreaReachability(areanum) == 0)
	{
		return 0;
	}

	if (aasworld.areasettings == NULL || areanum >= aasworld.numAreaSettings)
	{
		return 0;
	}

	int cluster = aasworld.areasettings[areanum].cluster;
	int clusterareanum = aasworld.areasettings[areanum].clusterareanum;
	if (cluster < 0)
	{
		int portalnum = -cluster;
		if (aasworld.portals == NULL || portalnum <= 0 || portalnum >= aasworld.numPortals)
		{
			return 0;
		}

		cluster = aasworld.portals[portalnum].frontcluster;
		clusterareanum = aasworld.portals[portalnum].clusterareanum[0];
	}

	if (aasworld.clusters == NULL || cluster < 0 || cluster >= aasworld.numClusters)
	{
		return 0;
	}

	int index = 0;
	for (int cursor = 0; cursor < cluster; ++cursor)
	{
		index += aasworld.clusters[cursor].numreachabilityareas;
	}

	return index + clusterareanum;
}

/*
=============
AAS_InsideFace

Check whether a point lies inside a face winding using the supplied normal.
=============
*/
static qboolean AAS_InsideFace(const aas_face_t *face,
                               const vec3_t pnormal,
                               const vec3_t point,
                               float epsilon)
{
	if (!aasworld.loaded ||
	    face == NULL ||
	    pnormal == NULL ||
	    point == NULL ||
	    aasworld.edgeIndex == NULL ||
	    aasworld.edges == NULL ||
	    aasworld.vertexes == NULL)
	{
		return qfalse;
	}

	if (face->firstedge < 0 || face->numedges < 0 ||
	    face->firstedge + face->numedges > aasworld.edgeIndexSize)
	{
		return qfalse;
	}

	for (int index = 0; index < face->numedges; ++index)
	{
		int edgenum = aasworld.edgeIndex[face->firstedge + index];
		int edgeindex = abs(edgenum);
		if (edgeindex < 0 || edgeindex >= aasworld.numEdges)
		{
			return qfalse;
		}

		const aas_edge_t *edge = &aasworld.edges[edgeindex];
		int firstvertex = (edgenum < 0) ? 1 : 0;
		int v0index = edge->v[firstvertex];
		int v1index = edge->v[!firstvertex];
		if (v0index < 0 || v0index >= aasworld.numVertexes ||
		    v1index < 0 || v1index >= aasworld.numVertexes)
		{
			return qfalse;
		}

		const vec_t *v0 = aasworld.vertexes[v0index];
		const vec_t *v1 = aasworld.vertexes[v1index];
		vec3_t edgevec;
		vec3_t pointvec;
		vec3_t sepnormal;
		VectorSubtract(v1, v0, edgevec);
		VectorSubtract(point, v0, pointvec);
		AAS_CrossProduct(edgevec, pnormal, sepnormal);
		if (DotProduct(pointvec, sepnormal) < -epsilon)
		{
			return qfalse;
		}
	}

	return qtrue;
}

/*
=============
AAS_PointInsideFace

Check whether a point is inside the bounds of a loaded face.
=============
*/
qboolean AAS_PointInsideFace(int facenum, const vec3_t point, float epsilon)
{
	if (!aasworld.loaded ||
	    point == NULL ||
	    aasworld.faces == NULL ||
	    aasworld.planes == NULL ||
	    facenum < 0 ||
	    facenum >= aasworld.numFaces)
	{
		return qfalse;
	}

	const aas_face_t *face = &aasworld.faces[facenum];
	if (face->planenum < 0 || face->planenum >= aasworld.numPlanes)
	{
		return qfalse;
	}

	return AAS_InsideFace(face, aasworld.planes[face->planenum].normal, point, epsilon);
}

/*
=============
AAS_AreaGroundFace

Return the ground face underneath a point in an area.
=============
*/
aas_face_t *AAS_AreaGroundFace(int areanum, const vec3_t point)
{
	if (!aasworld.loaded ||
	    point == NULL ||
	    aasworld.areas == NULL ||
	    aasworld.faceIndex == NULL ||
	    aasworld.faces == NULL ||
	    aasworld.planes == NULL ||
	    areanum <= 0 ||
	    areanum > aasworld.numAreas)
	{
		return NULL;
	}

	const aas_area_t *area = &aasworld.areas[areanum];
	if (area->firstface < 0 || area->numfaces < 0 ||
	    area->firstface + area->numfaces > aasworld.faceIndexSize)
	{
		return NULL;
	}

	vec3_t up = {0.0f, 0.0f, 1.0f};
	for (int index = 0; index < area->numfaces; ++index)
	{
		int facenum = abs(aasworld.faceIndex[area->firstface + index]);
		if (facenum < 0 || facenum >= aasworld.numFaces)
		{
			continue;
		}

		aas_face_t *face = &aasworld.faces[facenum];
		if ((face->faceflags & AAS_FACE_GROUND) == 0 ||
		    face->planenum < 0 ||
		    face->planenum >= aasworld.numPlanes)
		{
			continue;
		}

		vec3_t normal;
		if (aasworld.planes[face->planenum].normal[2] < 0.0f)
		{
			VectorNegate(up, normal);
		}
		else
		{
			VectorCopy(up, normal);
		}

		if (AAS_InsideFace(face, normal, point, 0.01f))
		{
			return face;
		}
	}

	return NULL;
}

/*
=============
AAS_FacePlane

Copy the plane normal and distance for a face.
=============
*/
void AAS_FacePlane(int facenum, vec3_t normal, float *dist)
{
	if (normal != NULL)
	{
		VectorClear(normal);
	}
	if (dist != NULL)
	{
		*dist = 0.0f;
	}

	if (aasworld.faces == NULL ||
	    aasworld.planes == NULL ||
	    facenum < 0 ||
	    facenum >= aasworld.numFaces)
	{
		return;
	}

	const aas_face_t *face = &aasworld.faces[facenum];
	if (face->planenum < 0 || face->planenum >= aasworld.numPlanes)
	{
		return;
	}

	const aas_plane_t *plane = &aasworld.planes[face->planenum];
	if (normal != NULL)
	{
		VectorCopy(plane->normal, normal);
	}
	if (dist != NULL)
	{
		*dist = plane->dist;
	}
}

/*
=============
AAS_FaceArea

Return the triangulated surface area of a face.
=============
*/
float AAS_FaceArea(const aas_face_t *face)
{
	if (face == NULL ||
	    aasworld.edgeIndex == NULL ||
	    aasworld.edges == NULL ||
	    aasworld.vertexes == NULL ||
	    face->numedges < 3 ||
	    face->firstedge < 0 ||
	    face->firstedge + face->numedges > aasworld.edgeIndexSize)
	{
		return 0.0f;
	}

	int baseEdgeNum = aasworld.edgeIndex[face->firstedge];
	int baseEdgeIndex = abs(baseEdgeNum);
	if (baseEdgeIndex < 0 || baseEdgeIndex >= aasworld.numEdges)
	{
		return 0.0f;
	}

	int side = (baseEdgeNum < 0) ? 1 : 0;
	int baseVertex = aasworld.edges[baseEdgeIndex].v[side];
	if (baseVertex < 0 || baseVertex >= aasworld.numVertexes)
	{
		return 0.0f;
	}

	const vec_t *origin = aasworld.vertexes[baseVertex];
	float total = 0.0f;
	for (int index = 1; index < face->numedges - 1; ++index)
	{
		int edgenum = aasworld.edgeIndex[face->firstedge + index];
		int edgeindex = abs(edgenum);
		if (edgeindex < 0 || edgeindex >= aasworld.numEdges)
		{
			return total;
		}

		side = (edgenum < 0) ? 1 : 0;
		int v1index = aasworld.edges[edgeindex].v[side];
		int v2index = aasworld.edges[edgeindex].v[!side];
		if (v1index < 0 || v1index >= aasworld.numVertexes ||
		    v2index < 0 || v2index >= aasworld.numVertexes)
		{
			return total;
		}

		vec3_t d1;
		vec3_t d2;
		vec3_t cross;
		VectorSubtract(aasworld.vertexes[v1index], origin, d1);
		VectorSubtract(aasworld.vertexes[v2index], origin, d2);
		AAS_CrossProduct(d1, d2, cross);
		total += 0.5f * AAS_VectorLength(cross);
	}

	return total;
}

/*
=============
AAS_AreaGroundFaceArea

Return the total surface area of all ground faces in an area.
=============
*/
float AAS_AreaGroundFaceArea(int areanum)
{
	if (aasworld.areas == NULL ||
	    aasworld.faceIndex == NULL ||
	    aasworld.faces == NULL ||
	    areanum <= 0 ||
	    areanum > aasworld.numAreas)
	{
		return 0.0f;
	}

	const aas_area_t *area = &aasworld.areas[areanum];
	if (area->firstface < 0 || area->numfaces < 0 ||
	    area->firstface + area->numfaces > aasworld.faceIndexSize)
	{
		return 0.0f;
	}

	float total = 0.0f;
	for (int index = 0; index < area->numfaces; ++index)
	{
		int facenum = abs(aasworld.faceIndex[area->firstface + index]);
		if (facenum < 0 || facenum >= aasworld.numFaces)
		{
			continue;
		}

		const aas_face_t *face = &aasworld.faces[facenum];
		if ((face->faceflags & AAS_FACE_GROUND) != 0)
		{
			total += AAS_FaceArea(face);
		}
	}

	return total;
}

/*
=============
AAS_TraceReachabilityPassAreas

Collect the AAS areas crossed by reachabilities whose travel type can span areas.
=============
*/
static int AAS_TraceReachabilityPassAreas(const aas_reachability_t *reach, int *areas, int maxareas)
{
	if (reach == NULL || areas == NULL || maxareas <= 0)
	{
		return 0;
	}

	vec3_t start;
	vec3_t end;
	switch (reach->traveltype & TRAVELTYPE_MASK)
	{
		case TRAVEL_BARRIERJUMP:
		case TRAVEL_WATERJUMP:
			VectorCopy(reach->start, end);
			end[2] = reach->end[2];
			return AAS_TraceAreas(reach->start, end, areas, NULL, maxareas);

		case TRAVEL_WALKOFFLEDGE:
			VectorCopy(reach->end, start);
			start[2] = reach->start[2];
			return AAS_TraceAreas(start, reach->end, areas, NULL, maxareas);

		case TRAVEL_GRAPPLEHOOK:
			return AAS_TraceAreas(reach->start, reach->end, areas, NULL, maxareas);

		default:
			return 0;
	}
}

/*
=============
AAS_InitReachabilityAreas

Build Q3-style pass-area tables for reachabilities that traverse intermediate areas.
=============
*/
static int AAS_InitReachabilityAreas(void)
{
	AAS_FreeReachabilityAreas();

	if (aasworld.numReachability <= 0)
	{
		return BLERR_NOERROR;
	}

	size_t reachCount = (size_t)aasworld.numReachability;
	aasworld.reachabilityAreas =
	    (aas_reachabilityareas_t *)calloc(reachCount, sizeof(aas_reachabilityareas_t));
	if (aasworld.reachabilityAreas == NULL)
	{
		return BLERR_INVALIDIMPORT;
	}

	size_t maxIndexCount = reachCount * (size_t)MAX_REACHABILITYPASSAREAS;
	aasworld.reachabilityAreaIndex = (int *)calloc(maxIndexCount, sizeof(int));
	if (aasworld.reachabilityAreaIndex == NULL)
	{
		AAS_FreeReachabilityAreas();
		return BLERR_INVALIDIMPORT;
	}

	int numReachAreas = 0;
	int areas[MAX_REACHABILITYPASSAREAS];
	for (int reachIndex = 0; reachIndex < aasworld.numReachability; ++reachIndex)
	{
		int numAreas = AAS_TraceReachabilityPassAreas(&aasworld.reachability[reachIndex],
		                                              areas,
		                                              MAX_REACHABILITYPASSAREAS);
		if (numAreas < 0)
		{
			numAreas = 0;
		}
		if (numAreas > MAX_REACHABILITYPASSAREAS)
		{
			numAreas = MAX_REACHABILITYPASSAREAS;
		}

		if (numReachAreas + numAreas > (int)maxIndexCount)
		{
			AAS_FreeReachabilityAreas();
			return BLERR_INVALIDIMPORT;
		}

		aasworld.reachabilityAreas[reachIndex].firstarea = numReachAreas;
		aasworld.reachabilityAreas[reachIndex].numareas = numAreas;
		for (int index = 0; index < numAreas; ++index)
		{
			aasworld.reachabilityAreaIndex[numReachAreas++] = areas[index];
		}
	}

	aasworld.reachabilityAreaIndexSize = numReachAreas;
	return BLERR_NOERROR;
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
    int reachAreaStatus = AAS_InitReachabilityAreas();
    if (reachAreaStatus != BLERR_NOERROR)
    {
        AAS_ClearReachabilityData();
        return reachAreaStatus;
    }

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
