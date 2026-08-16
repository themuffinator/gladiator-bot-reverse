#include "aas_local.h"

#include <math.h>
#include <stdbool.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "botlib/common/l_log.h"
#include "botlib/common/l_libvar.h"
#include "botlib/common/l_memory.h"
#include "q2bridge/bridge_config.h"

#define MAX_REACHABILITYPASSAREAS 32
#define AAS_MAX_REACHABILITYSIZE 65536
#define INSIDEUNITS 2.0f
#define INSIDEUNITS_WALKEND 5.0f
#define INSIDEUNITS_WALKSTART 0.1f
#define INSIDEUNITS_WATERJUMP 15.0f
#define AAS_Q2_SURF_SKY 4

typedef struct
{
	qboolean found;
	float dist;
	float length;
	int edgenum;
	vec3_t start;
	vec3_t end;
	vec3_t normal;
} aas_adjacent_reach_candidate_t;

typedef struct
{
    int frames_with_work;
    int frames_skipped;
    bool force_reachability_active;
    bool force_clustering_active;
} aas_reachability_frame_state_t;

static aas_reachability_frame_state_t g_reach_frame_state;
static aas_lreachability_t *reachabilityheap;
static aas_lreachability_t *nextreachability;
static aas_lreachability_t **areareachability;
static int numlreachabilities;
static int reachabilityareacount;
static int reach_swim;
static int reach_equalfloor;
static int reach_step;
static int reach_waterjump;
static int reach_barrier;
static int reach_walk;
static int reach_walkoffledge;
static int reach_jump;
static int reach_ladder;
static int reach_teleport;
static int reach_elevator;
static int reach_grapple;
static int reach_rocketjump;

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

/*
=============
AAS_VectorNormalize

Normalize a reachability geometry vector and return its original length.
=============
*/
static float AAS_VectorNormalize(vec3_t vector)
{
	float length = AAS_VectorLength(vector);
	if (length > 0.000001f)
	{
		VectorScale(vector, 1.0f / length, vector);
	}
	else
	{
		VectorClear(vector);
	}

	return length;
}

/*
=============
AAS_ReachPositiveLibVarValue

Read a reachability physics variable with its retail fallback.
=============
*/
static float AAS_ReachPositiveLibVarValue(const libvar_t *var, float fallback)
{
	if (var == NULL || var->value <= 0.0f)
	{
		return fallback;
	}

	return var->value;
}

/*
=============
AAS_ResetReachabilityGenerator

Release the temporary retail reachability heap and per-area link heads.
=============
*/
static void AAS_ResetReachabilityGenerator(void)
{
	FreeMemory(reachabilityheap);
	reachabilityheap = NULL;
	nextreachability = NULL;
	FreeMemory(areareachability);
	areareachability = NULL;
	numlreachabilities = 0;
	reachabilityareacount = 0;
	reach_swim = 0;
	reach_equalfloor = 0;
	reach_step = 0;
	reach_waterjump = 0;
	reach_barrier = 0;
	reach_walk = 0;
	reach_walkoffledge = 0;
	reach_jump = 0;
	reach_ladder = 0;
	reach_teleport = 0;
	reach_elevator = 0;
	reach_grapple = 0;
	reach_rocketjump = 0;
}

/*
=============
AAS_SetupReachabilityHeap

Allocate and chain the retail 65,536-entry temporary reachability heap.
=============
*/
void AAS_SetupReachabilityHeap(void)
{
	AAS_ResetReachabilityGenerator();
	reachabilityheap = (aas_lreachability_t *)GetClearedMemory(
		AAS_MAX_REACHABILITYSIZE * sizeof(*reachabilityheap));
	if (reachabilityheap == NULL)
	{
		BotLib_Print(PRT_FATAL, "AAS_SetupReachabilityHeap: out of memory\n");
		return;
	}

	for (int index = 0; index < AAS_MAX_REACHABILITYSIZE - 1; ++index)
	{
		reachabilityheap[index].next = &reachabilityheap[index + 1];
	}
	reachabilityheap[AAS_MAX_REACHABILITYSIZE - 1].next = NULL;
	nextreachability = reachabilityheap;
}

/*
=============
AAS_ShutDownReachabilityHeap

Release all temporary reachability-generation storage.
=============
*/
void AAS_ShutDownReachabilityHeap(void)
{
	AAS_ResetReachabilityGenerator();
}

/*
=============
AAS_AllocReachability

Pop one link from the retail temporary reachability free list.
=============
*/
aas_lreachability_t *AAS_AllocReachability(void)
{
	aas_lreachability_t *reachability = nextreachability;
	if (reachability == NULL)
	{
		return NULL;
	}

	if (reachability->next == NULL)
	{
		BotLib_Print(PRT_FATAL, "AAS_MAX_REACHABILITYSIZE\n");
	}

	nextreachability = reachability->next;
	memset(reachability, 0, sizeof(*reachability));
	numlreachabilities++;
	return reachability;
}

/*
=============
AAS_FreeReachability

Return one temporary reachability link to the free list.
=============
*/
void AAS_FreeReachability(aas_lreachability_t *reachability)
{
	if (reachability == NULL || reachabilityheap == NULL ||
		reachability < reachabilityheap ||
		reachability >= reachabilityheap + AAS_MAX_REACHABILITYSIZE)
	{
		return;
	}

	memset(reachability, 0, sizeof(*reachability));
	reachability->next = nextreachability;
	nextreachability = reachability;
	if (numlreachabilities > 0)
	{
		numlreachabilities--;
	}
}

/*
=============
AAS_InitReachability

Begin forced reachability generation with empty per-area temporary lists.
=============
*/
void AAS_InitReachability(void)
{
	AAS_ResetReachabilityGenerator();
	if (!aasworld.loaded || aasworld.areasettings == NULL ||
		aasworld.numAreaSettings <= 0)
	{
		return;
	}

	libvar_t *force_reachability = Bridge_ForceReachability();
	if (aasworld.numReachability > 0 &&
		(force_reachability == NULL || force_reachability->value == 0.0f))
	{
		aasworld.numReachabilityAreas = aasworld.numAreas;
		return;
	}
	aasworld.saveFile = qtrue;
	aasworld.numReachabilityAreas = 1;

	AAS_SetupReachabilityHeap();
	if (reachabilityheap == NULL)
	{
		return;
	}

	reachabilityareacount = aasworld.numAreaSettings;
	areareachability = (aas_lreachability_t **)GetClearedMemory(
		(size_t)reachabilityareacount * sizeof(*areareachability));
	if (areareachability == NULL)
	{
		BotLib_Print(PRT_FATAL, "AAS_InitReachability: out of memory\n");
		AAS_ResetReachabilityGenerator();
		return;
	}
	AAS_SetWeaponJumpAreaFlags();
}

static void AAS_FreeReverseReachability(void)
{
    if (aasworld.reversedReachability != NULL)
    {
        int areaCount = (aasworld.numAreas > 0) ? aasworld.numAreas : 0;
		for (int area = 0; area < areaCount; ++area)
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
AAS_AreaLiquid

Return true when an area contains a swimmable liquid.
=============
*/
int AAS_AreaLiquid(int areanum)
{
	return AAS_AreaSwim(areanum);
}

/*
=============
AAS_AreaLava

Return the retail lava contents flag for an area.
=============
*/
int AAS_AreaLava(int areanum)
{
	if (aasworld.areasettings == NULL || areanum <= 0 || areanum >= aasworld.numAreaSettings)
	{
		return 0;
	}

	return aasworld.areasettings[areanum].contents & AAS_AREACONTENTS_LAVA;
}

/*
=============
AAS_AreaSlime

Return the retail slime contents flag for an area.
=============
*/
int AAS_AreaSlime(int areanum)
{
	if (aasworld.areasettings == NULL || areanum <= 0 || areanum >= aasworld.numAreaSettings)
	{
		return 0;
	}

	return aasworld.areasettings[areanum].contents & AAS_AREACONTENTS_SLIME;
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
AAS_AreaTeleporter

Return the retail teleporter contents flag for an area.
=============
*/
int AAS_AreaTeleporter(int areanum)
{
	if (aasworld.areasettings == NULL || areanum <= 0 || areanum >= aasworld.numAreaSettings)
	{
		return 0;
	}

	return aasworld.areasettings[areanum].contents & AAS_AREACONTENTS_TELEPORTER;
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
			index += aasworld.clusters[cluster].numareas;
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
		index += aasworld.clusters[cursor].numareas;
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
AAS_AreaVolume

Return the convex area volume by forming tetrahedrons from one hull corner.
=============
*/
float AAS_AreaVolume(int areanum)
{
	if (aasworld.areas == NULL || aasworld.faceIndex == NULL ||
		aasworld.faces == NULL || aasworld.edgeIndex == NULL ||
		aasworld.edges == NULL || aasworld.vertexes == NULL ||
		aasworld.planes == NULL || areanum <= 0 || areanum > aasworld.numAreas)
	{
		return 0.0f;
	}

	const aas_area_t *area = &aasworld.areas[areanum];
	if (area->numfaces <= 0 || area->firstface < 0 ||
		area->firstface + area->numfaces > aasworld.faceIndexSize)
	{
		return 0.0f;
	}

	int firstfacenum = abs(aasworld.faceIndex[area->firstface]);
	if (firstfacenum < 0 || firstfacenum >= aasworld.numFaces)
	{
		return 0.0f;
	}

	const aas_face_t *firstface = &aasworld.faces[firstfacenum];
	if (firstface->numedges <= 0 || firstface->firstedge < 0 ||
		firstface->firstedge >= aasworld.edgeIndexSize)
	{
		return 0.0f;
	}

	int firstedgenum = abs(aasworld.edgeIndex[firstface->firstedge]);
	if (firstedgenum < 0 || firstedgenum >= aasworld.numEdges)
	{
		return 0.0f;
	}

	int cornerindex = aasworld.edges[firstedgenum].v[0];
	if (cornerindex < 0 || cornerindex >= aasworld.numVertexes)
	{
		return 0.0f;
	}

	const vec_t *corner = aasworld.vertexes[cornerindex];
	float volume = 0.0f;
	for (int index = 0; index < area->numfaces; ++index)
	{
		int facenum = abs(aasworld.faceIndex[area->firstface + index]);
		if (facenum < 0 || facenum >= aasworld.numFaces)
		{
			continue;
		}

		const aas_face_t *face = &aasworld.faces[facenum];
		int side = face->backarea != areanum;
		int planenum = face->planenum ^ side;
		if (planenum < 0 || planenum >= aasworld.numPlanes)
		{
			continue;
		}

		const aas_plane_t *plane = &aasworld.planes[planenum];
		float distance = -(DotProduct(corner, plane->normal) - plane->dist);
		volume += distance * AAS_FaceArea(face);
	}

	return volume / 3.0f;
}

/*
=============
AAS_FaceCenter

Average both endpoints of every face edge to recover the face center.
=============
*/
void AAS_FaceCenter(int facenum, vec3_t center)
{
	if (center == NULL)
	{
		return;
	}

	VectorClear(center);
	if (aasworld.faces == NULL || aasworld.edgeIndex == NULL ||
		aasworld.edges == NULL || aasworld.vertexes == NULL ||
		facenum < 0 || facenum >= aasworld.numFaces)
	{
		return;
	}

	const aas_face_t *face = &aasworld.faces[facenum];
	if (face->numedges <= 0 || face->firstedge < 0 ||
		face->firstedge + face->numedges > aasworld.edgeIndexSize)
	{
		return;
	}

	for (int index = 0; index < face->numedges; ++index)
	{
		int edgenum = abs(aasworld.edgeIndex[face->firstedge + index]);
		if (edgenum < 0 || edgenum >= aasworld.numEdges)
		{
			VectorClear(center);
			return;
		}

		const aas_edge_t *edge = &aasworld.edges[edgenum];
		if (edge->v[0] < 0 || edge->v[0] >= aasworld.numVertexes ||
			edge->v[1] < 0 || edge->v[1] >= aasworld.numVertexes)
		{
			VectorClear(center);
			return;
		}

		VectorAdd(center, aasworld.vertexes[edge->v[0]], center);
		VectorAdd(center, aasworld.vertexes[edge->v[1]], center);
	}

	VectorScale(center, 0.5f / (float)face->numedges, center);
}

/*
=============
AAS_BestReachableLinkArea

Choose the first grounded or swimming linked area, then the first valid area.
=============
*/
int AAS_BestReachableLinkArea(aas_link_t *areas)
{
	for (aas_link_t *link = areas; link != NULL; link = link->next_area)
	{
		if (AAS_AreaGrounded(link->areanum) || AAS_AreaSwim(link->areanum))
		{
			return link->areanum;
		}
	}

	for (aas_link_t *link = areas; link != NULL; link = link->next_area)
	{
		if (link->areanum != 0)
		{
			return link->areanum;
		}
	}

	return 0;
}

/*
=============
AAS_BestReachableEntityArea

Return the best reachable area from a live entity slot's existing area links.
=============
*/
int AAS_BestReachableEntityArea(int entnum)
{
	if (aasworld.entities == NULL ||
		entnum < 0 ||
		entnum >= aasworld.maxEntities)
	{
		return 0;
	}

	return AAS_BestReachableLinkArea(aasworld.entities[entnum].areas);
}

/*
=============
AAS_BestReachableArea

Find the retail reachable AAS area for an item origin, including the small
point fudge, crouch-bbox floor trace, and overlapping-box fallback.
=============
*/
int AAS_BestReachableArea(const vec3_t origin,
	const vec3_t mins,
	const vec3_t maxs,
	vec3_t goalorigin)
{
	if (!aasworld.loaded)
	{
		BotLib_Print(PRT_ERROR, "AAS_BestReachableArea: aas not loaded\n");
		return 0;
	}
	if (origin == NULL || mins == NULL || maxs == NULL || goalorigin == NULL)
	{
		return 0;
	}

	vec3_t itemorigin;
	VectorCopy(origin, itemorigin);
	vec3_t start;
	VectorCopy(itemorigin, start);
	int areanum = AAS_PointAreaNum(start);
	for (int height = 0; height < 5 && areanum == 0; ++height)
	{
		for (int distance = 0; distance < 5 && areanum == 0; ++distance)
		{
			for (int xdirection = -1;
				xdirection <= 1 && areanum == 0;
				++xdirection)
			{
				for (int ydirection = -1;
					ydirection <= 1 && areanum == 0;
					++ydirection)
				{
					VectorCopy(itemorigin, start);
					start[0] += (float)(distance * 4 * xdirection);
					start[1] += (float)(distance * 4 * ydirection);
					start[2] += (float)(height * 4);
					areanum = AAS_PointAreaNum(start);
				}
			}
		}
	}

	if (areanum != 0)
	{
		vec3_t end;
		VectorCopy(start, end);
		start[2] += 0.25f;
		end[2] -= 50.0f;
		aas_trace_t trace = AAS_TraceClientBBox(start,
			end,
			PRESENCE_CROUCH,
			-1);
		if (!trace.startsolid)
		{
			areanum = AAS_PointAreaNum(trace.endpos);
			VectorCopy(trace.endpos, goalorigin);
			if (areanum != 0)
			{
				return areanum;
			}
		}
		else
		{
			VectorCopy(start, goalorigin);
			return areanum;
		}
	}

	VectorCopy(itemorigin, goalorigin);
	if (aasworld.numAreas <= 0)
	{
		return 0;
	}
	vec3_t absmins;
	vec3_t absmaxs;
	VectorAdd(itemorigin, mins, absmins);
	VectorAdd(itemorigin, maxs, absmaxs);
	int *areas = malloc((size_t)aasworld.numAreas * sizeof(*areas));
	if (areas == NULL)
	{
		return 0;
	}
	int numareas = AAS_BBoxAreas(absmins,
		absmaxs,
		areas,
		aasworld.numAreas);
	areanum = 0;
	/*
	 * sub_1001c460 prepends each temporary leaf link before
	 * sub_1000b130 chooses its first grounded/swimming area.  BBoxAreas
	 * returns leaf visits in traversal order, so consume it backwards to
	 * preserve the temporary link-list order.
	 */
	for (int index = numareas - 1; index >= 0; --index)
	{
		if (AAS_AreaGrounded(areas[index]) || AAS_AreaSwim(areas[index]))
		{
			areanum = areas[index];
			break;
		}
	}
	if (areanum == 0)
	{
		for (int index = numareas - 1; index >= 0; --index)
		{
			if (areas[index] != 0)
			{
				areanum = areas[index];
				break;
			}
		}
	}
	free(areas);
	return areanum;
}

/*
=============
AAS_FallDamageDistance

Return the maximum whole-unit fall distance before the retail damage threshold.
=============
*/
int AAS_FallDamageDistance(void)
{
	float gravity = AAS_ReachPositiveLibVarValue(Bridge_Gravity(), 800.0f);
	float maxzvelocity = sqrtf(30.0f * 10000.0f);
	float time = maxzvelocity / gravity;
	return (int)(0.5f * gravity * time * time);
}

/*
=============
AAS_FallDelta

Convert a fall distance into the retail squared-velocity damage delta.
=============
*/
float AAS_FallDelta(float distance)
{
	float gravity = AAS_ReachPositiveLibVarValue(Bridge_Gravity(), 800.0f);
	float time = sqrtf(fabsf(distance) * 2.0f / gravity);
	float delta = time * gravity;
	return delta * delta * 0.0001f;
}

/*
=============
AAS_MaxJumpHeight

Return the ballistic apex height for a vertical jump velocity.
=============
*/
float AAS_MaxJumpHeight(float phys_jumpvel)
{
	float gravity = AAS_ReachPositiveLibVarValue(Bridge_Gravity(), 800.0f);
	float time = phys_jumpvel / gravity;
	return 0.5f * gravity * time * time;
}

/*
=============
AAS_MaxJumpDistance

Return the maximum horizontal distance across the configured jump-fall height.
=============
*/
float AAS_MaxJumpDistance(float phys_jumpvel)
{
	float gravity = AAS_ReachPositiveLibVarValue(Bridge_Gravity(), 800.0f);
	float maxvelocity = AAS_ReachPositiveLibVarValue(Bridge_MaxVelocity(), 300.0f);
	float maxjumpfallheight = LibVarGetValue("rs_maxjumpfallheight");
	if (maxjumpfallheight <= 0.0f)
	{
		maxjumpfallheight = 450.0f;
	}
	float falltime = sqrtf(maxjumpfallheight / (0.5f * gravity));
	return maxvelocity * (falltime + phys_jumpvel / gravity);
}

/*
=============
AAS_BarrierJumpTravelTime

Return the Q3 decisecond estimate for completing a barrier jump.
=============
*/
unsigned short AAS_BarrierJumpTravelTime(void)
{
	float gravity = AAS_ReachPositiveLibVarValue(Bridge_Gravity(), 800.0f);
	float jumpvelocity = AAS_ReachPositiveLibVarValue(Bridge_JumpVelocity(), 224.0f);
	return (unsigned short)(jumpvelocity / (gravity * 0.1f));
}

/*
=============
AAS_ReachabilityExists

Return true when an existing area reachability already targets the destination.
=============
*/
qboolean AAS_ReachabilityExists(int area1num, int area2num)
{
	if (areareachability != NULL && area1num > 0 &&
		area1num < reachabilityareacount)
	{
		for (aas_lreachability_t *reachability = areareachability[area1num];
			reachability != NULL;
			reachability = reachability->next)
		{
			if (reachability->areanum == area2num)
			{
				return qtrue;
			}
		}
	}

	if (aasworld.areasettings == NULL || aasworld.reachability == NULL ||
		area1num <= 0 || area1num >= aasworld.numAreaSettings ||
		area2num <= 0 || area2num >= aasworld.numAreaSettings)
	{
		return qfalse;
	}

	const aas_areasettings_t *settings = &aasworld.areasettings[area1num];
	if (settings->firstreachablearea < 0 || settings->numreachableareas < 0 ||
		settings->firstreachablearea + settings->numreachableareas > aasworld.numReachability)
	{
		return qfalse;
	}

	for (int index = 0; index < settings->numreachableareas; ++index)
	{
		const aas_reachability_t *reach =
			&aasworld.reachability[settings->firstreachablearea + index];
		if (reach->areanum == area2num)
		{
			return qtrue;
		}
	}

	return qfalse;
}

/*
=============
AAS_NearbySolidOrGap

Return true when the space beyond a reachability ends in solid or an
unsupported non-ground, non-liquid area.
=============
*/
int AAS_NearbySolidOrGap(const vec3_t start, const vec3_t end)
{
	vec3_t direction;
	VectorSubtract(end, start, direction);
	direction[2] = 0.0f;
	AAS_VectorNormalize(direction);

	vec3_t testpoint;
	VectorMA(end, 48.0f, direction, testpoint);
	int areanum = AAS_PointAreaNum(testpoint);
	if (areanum == 0)
	{
		testpoint[2] += 16.0f;
		areanum = AAS_PointAreaNum(testpoint);
		if (areanum == 0)
		{
			return qtrue;
		}
	}

	VectorMA(end, 64.0f, direction, testpoint);
	areanum = AAS_PointAreaNum(testpoint);
	if (areanum != 0 && !AAS_AreaSwim(areanum) && !AAS_AreaGrounded(areanum))
	{
		return qtrue;
	}

	return qfalse;
}

/*
=============
AAS_StoreReachability

Flatten temporary per-area links into the one-based runtime reachability lump.
=============
*/
void AAS_StoreReachability(void)
{
	if (areareachability == NULL || aasworld.areasettings == NULL ||
		reachabilityareacount <= 0)
	{
		return;
	}

	size_t reachability_count = 0U;
	for (int area = 0; area < reachabilityareacount; ++area)
	{
		for (aas_lreachability_t *link = areareachability[area];
			link != NULL;
			link = link->next)
		{
			reachability_count++;
		}
	}

	if (reachability_count >= (size_t)INT_MAX)
	{
		BotLib_Print(PRT_FATAL, "AAS_StoreReachability: too many reachabilities\n");
		return;
	}

	aas_reachability_t *stored = (aas_reachability_t *)GetClearedMemory(
		(reachability_count + 1U) * sizeof(*stored));
	if (stored == NULL)
	{
		BotLib_Print(PRT_FATAL, "AAS_StoreReachability: out of memory\n");
		return;
	}

	int stored_index = 1;
	for (int area = 0; area < aasworld.numAreaSettings; ++area)
	{
		aas_areasettings_t *settings = &aasworld.areasettings[area];
		settings->firstreachablearea = stored_index;
		settings->numreachableareas = 0;
		if (area >= reachabilityareacount)
		{
			continue;
		}

		for (aas_lreachability_t *link = areareachability[area];
			link != NULL;
			link = link->next)
		{
			aas_reachability_t *reachability = &stored[stored_index++];
			reachability->areanum = link->areanum;
			reachability->facenum = link->facenum;
			reachability->edgenum = link->edgenum;
			VectorCopy(link->start, reachability->start);
			VectorCopy(link->end, reachability->end);
			reachability->traveltype = link->traveltype;
			reachability->traveltime = link->traveltime;
			settings->numreachableareas++;
		}
	}

	AAS_ClearReachabilityData();
	FreeMemory(aasworld.reachability);
	aasworld.reachability = stored;
	aasworld.numReachability = stored_index;
	if (AAS_PrepareReachability() != BLERR_NOERROR)
	{
		BotLib_Print(PRT_ERROR,
			"AAS_StoreReachability: failed to prepare generated reachability\n");
	}
	AAS_InvalidateRouteCache();
}

/*
=============
AAS_Reachability_Swim

Create a swim link across a shared liquid face between adjacent swim areas.
=============
*/
int AAS_Reachability_Swim(int area1num, int area2num)
{
	if (areareachability == NULL || aasworld.areas == NULL ||
		aasworld.areasettings == NULL || aasworld.faceIndex == NULL ||
		aasworld.faces == NULL || aasworld.planes == NULL ||
		area1num <= 0 || area2num <= 0 ||
		area1num >= aasworld.numAreaSettings ||
		area2num >= aasworld.numAreaSettings ||
		area1num >= aasworld.numAreas || area2num >= aasworld.numAreas)
	{
		return qfalse;
	}

	if (!AAS_AreaSwim(area1num) || !AAS_AreaSwim(area2num) ||
		(aasworld.areasettings[area2num].presencetype & PRESENCE_NORMAL) == 0)
	{
		return qfalse;
	}

	const aas_area_t *area1 = &aasworld.areas[area1num];
	const aas_area_t *area2 = &aasworld.areas[area2num];
	for (int axis = 0; axis < 3; ++axis)
	{
		if (area1->mins[axis] > area2->maxs[axis] + 10.0f ||
			area1->maxs[axis] < area2->mins[axis] - 10.0f)
		{
			return qfalse;
		}
	}

	if (area1->firstface < 0 || area1->numfaces < 0 ||
		area1->firstface + area1->numfaces > aasworld.faceIndexSize ||
		area2->firstface < 0 || area2->numfaces < 0 ||
		area2->firstface + area2->numfaces > aasworld.faceIndexSize)
	{
		return qfalse;
	}

	for (int index1 = 0; index1 < area1->numfaces; ++index1)
	{
		int signedface1 = aasworld.faceIndex[area1->firstface + index1];
		int side1 = signedface1 < 0;
		int face1num = abs(signedface1);
		if (face1num < 0 || face1num >= aasworld.numFaces)
		{
			continue;
		}

		for (int index2 = 0; index2 < area2->numfaces; ++index2)
		{
			int face2num = abs(aasworld.faceIndex[area2->firstface + index2]);
			if (face1num != face2num)
			{
				continue;
			}

			vec3_t start;
			AAS_FaceCenter(face1num, start);
			if ((AAS_PointContents(start) & MASK_WATER) == 0)
			{
				continue;
			}

			const aas_face_t *face = &aasworld.faces[face1num];
			int planenum = face->planenum ^ side1;
			if (planenum < 0 || planenum >= aasworld.numPlanes)
			{
				continue;
			}

			aas_lreachability_t *reachability = AAS_AllocReachability();
			if (reachability == NULL)
			{
				return qfalse;
			}
			reachability->areanum = area2num;
			reachability->facenum = face1num;
			reachability->edgenum = 0;
			VectorCopy(start, reachability->start);
			VectorMA(reachability->start,
				INSIDEUNITS,
				aasworld.planes[planenum].normal,
				reachability->end);
			reachability->traveltype = TRAVEL_SWIM;
			reachability->traveltime = 1;
			if (AAS_AreaVolume(area2num) < 800.0f)
			{
				reachability->traveltime += 200;
			}
			reachability->next = areareachability[area1num];
			areareachability[area1num] = reachability;
			reach_swim++;
			return qtrue;
		}
	}

	return qfalse;
}

/*
=============
AAS_Reachability_EqualFloorHeight

Create a walk link over the lowest, longest edge shared by two ground faces.
=============
*/
int AAS_Reachability_EqualFloorHeight(int area1num, int area2num)
{
	if (areareachability == NULL || aasworld.areas == NULL ||
		aasworld.areasettings == NULL || aasworld.faceIndex == NULL ||
		aasworld.faces == NULL || aasworld.edgeIndex == NULL ||
		aasworld.edges == NULL || aasworld.vertexes == NULL ||
		aasworld.planes == NULL || area1num <= 0 || area2num <= 0 ||
		area1num >= aasworld.numAreaSettings ||
		area2num >= aasworld.numAreaSettings ||
		area1num >= aasworld.numAreas || area2num >= aasworld.numAreas)
	{
		return qfalse;
	}

	if (!AAS_AreaGrounded(area1num) || !AAS_AreaGrounded(area2num))
	{
		return qfalse;
	}

	const aas_area_t *area1 = &aasworld.areas[area1num];
	const aas_area_t *area2 = &aasworld.areas[area2num];
	for (int axis = 0; axis < 2; ++axis)
	{
		if (area1->mins[axis] > area2->maxs[axis] + 10.0f ||
			area1->maxs[axis] < area2->mins[axis] - 10.0f)
		{
			return qfalse;
		}
	}
	if (area2->mins[2] > area1->maxs[2])
	{
		return qfalse;
	}

	if (area1->firstface < 0 || area1->numfaces < 0 ||
		area1->firstface + area1->numfaces > aasworld.faceIndexSize ||
		area2->firstface < 0 || area2->numfaces < 0 ||
		area2->firstface + area2->numfaces > aasworld.faceIndexSize)
	{
		return qfalse;
	}

	float bestheight = 99999.0f;
	float bestlength = 0.0f;
	qboolean foundreach = qfalse;
	aas_lreachability_t candidate;
	memset(&candidate, 0, sizeof(candidate));
	for (int index1 = 0; index1 < area1->numfaces; ++index1)
	{
		int face1num = abs(aasworld.faceIndex[area1->firstface + index1]);
		if (face1num < 0 || face1num >= aasworld.numFaces)
		{
			continue;
		}
		const aas_face_t *face1 = &aasworld.faces[face1num];
		if ((face1->faceflags & AAS_FACE_GROUND) == 0 ||
			face1->firstedge < 0 || face1->numedges < 0 ||
			face1->firstedge + face1->numedges > aasworld.edgeIndexSize)
		{
			continue;
		}

		for (int index2 = 0; index2 < area2->numfaces; ++index2)
		{
			int face2num = abs(aasworld.faceIndex[area2->firstface + index2]);
			if (face2num < 0 || face2num >= aasworld.numFaces)
			{
				continue;
			}
			const aas_face_t *face2 = &aasworld.faces[face2num];
			if ((face2->faceflags & AAS_FACE_GROUND) == 0 ||
				face2->firstedge < 0 || face2->numedges < 0 ||
				face2->firstedge + face2->numedges > aasworld.edgeIndexSize ||
				face2->planenum < 0 || face2->planenum >= aasworld.numPlanes)
			{
				continue;
			}

			for (int edge1index = 0; edge1index < face1->numedges; ++edge1index)
			{
				int signededge = aasworld.edgeIndex[face1->firstedge + edge1index];
				int edgenum = abs(signededge);
				if (edgenum < 0 || edgenum >= aasworld.numEdges)
				{
					continue;
				}
				for (int edge2index = 0; edge2index < face2->numedges; ++edge2index)
				{
					if (edgenum != abs(aasworld.edgeIndex[face2->firstedge + edge2index]))
					{
						continue;
					}

					const aas_edge_t *edge = &aasworld.edges[edgenum];
					if (edge->v[0] < 0 || edge->v[0] >= aasworld.numVertexes ||
						edge->v[1] < 0 || edge->v[1] >= aasworld.numVertexes)
					{
						continue;
					}

					/*
					 * Retail takes the length of the summed vertices before
					 * halving them (0x10011d09 VectorLength, 0x10011d1c
					 * VectorScale), so the tie-break length is twice the
					 * midpoint's distance from the world origin rather than
					 * the edge length.
					 */
					vec3_t start;
					VectorAdd(aasworld.vertexes[edge->v[0]],
						aasworld.vertexes[edge->v[1]],
						start);
					float length = AAS_VectorLength(start);
					VectorScale(start, 0.5f, start);
					vec3_t end;
					VectorCopy(start, end);

					int side = signededge < 0;
					vec3_t edgevector;
					VectorSubtract(aasworld.vertexes[edge->v[side]],
						aasworld.vertexes[edge->v[!side]],
						edgevector);
					vec3_t normal;
					AAS_CrossProduct(edgevector,
						aasworld.planes[face2->planenum].normal,
						normal);
					if (AAS_VectorNormalize(normal) == 0.0f)
					{
						continue;
					}

					VectorMA(end, INSIDEUNITS_WALKEND, normal, end);
					VectorMA(start, INSIDEUNITS_WALKSTART, normal, start);
					end[2] += 0.125f;
					float height = start[2];
					/*
					 * Retail biases the selection height by 200 when nothing
					 * solid and no gap sits beyond the step edge
					 * (0x10011e48 / 0x10011e54), which pushes such edges to
					 * the back of the height ordering.
					 */
					if (!AAS_NearbySolidOrGap(start, end))
					{
						height += 200.0f;
					}
					if (height < bestheight ||
						(height < bestheight + 1.0f && length > bestlength))
					{
						bestheight = height;
						bestlength = length;
						candidate.areanum = area2num;
						candidate.facenum = 0;
						candidate.edgenum = signededge;
						VectorCopy(start, candidate.start);
						VectorCopy(end, candidate.end);
						candidate.traveltype = TRAVEL_WALK;
						candidate.traveltime = 1;
						foundreach = qtrue;
					}
				}
			}
		}
	}

	if (!foundreach)
	{
		return qfalse;
	}

	aas_lreachability_t *reachability = AAS_AllocReachability();
	if (reachability == NULL)
	{
		return qfalse;
	}
	*reachability = candidate;
	reachability->next = areareachability[area1num];
	areareachability[area1num] = reachability;
	if (!AAS_AreaCrouch(area1num) && AAS_AreaCrouch(area2num))
	{
		float startcrouch = LibVarGetValue("rs_startcrouch");
		if (startcrouch <= 0.0f)
		{
			startcrouch = 300.0f;
		}
		reachability->traveltime =
			(unsigned short)(reachability->traveltime + (unsigned short)startcrouch);
	}
	/*
	 * Retail adds two flat 100 unit penalties after prepending the record:
	 * 0x10012037 when AAS_NearbySolidOrGap(reach->start, reach->end) is false
	 * and 0x10012040 when AAS_AreaGroundFaceArea(reach->areanum) < 500.
	 */
	if (!AAS_NearbySolidOrGap(reachability->start, reachability->end))
	{
		reachability->traveltime = (unsigned short)(reachability->traveltime + 100);
	}
	if (AAS_AreaGroundFaceArea(reachability->areanum) < 500.0f)
	{
		reachability->traveltime = (unsigned short)(reachability->traveltime + 100);
	}
	reach_equalfloor++;
	return qtrue;
}

/*
=============
AAS_ReachLibVarValue

Read a reachability cost variable while retaining the retail default when the
variable has not been configured.
=============
*/
static float AAS_ReachLibVarValue(const char *name, float fallback)
{
	libvar_t *variable = LibVarGet(name);
	return variable != NULL ? variable->value : fallback;
}

/*
=============
AAS_ReachAbsTrunc

Reproduce retail's abs() on a float argument, which MSVC resolves to the
integer abs: the value is truncated to int first, so the magnitude tests in
AAS_Reachability_Ladder effectively compare against 1.0 rather than 0.1 or 0.7.
=============
*/
static float AAS_ReachAbsTrunc(float value)
{
	int truncated = (int)value;
	return (float)(truncated < 0 ? -truncated : truncated);
}

/*
=============
AAS_AdjacentEdgeOverlap

Project two coplanar ground edges into their vertical side plane and return the
closest points over the portion where their horizontal intervals overlap.
=============
*/
static qboolean AAS_AdjacentEdgeOverlap(const vec3_t edge1start,
	const vec3_t edge1end,
	const vec3_t edge2start,
	const vec3_t edge2end,
	const vec3_t normal,
	vec3_t start,
	vec3_t end,
	float *verticaldist,
	float *overlaplength)
{
	vec3_t v1;
	vec3_t v2;
	vec3_t v3;
	vec3_t v4;
	VectorCopy(edge1start, v1);
	VectorCopy(edge1end, v2);
	VectorCopy(edge2start, v3);
	VectorCopy(edge2end, v4);

	vec3_t invgravity = {0.0f, 0.0f, 1.0f};
	vec3_t ort;
	AAS_CrossProduct(invgravity, normal, ort);
	float ortdot = DotProduct(ort, ort);
	if (ortdot <= 0.000001f)
	{
		return qfalse;
	}

	float x1 = DotProduct(v1, ort) / ortdot;
	float x2 = DotProduct(v2, ort) / ortdot;
	float x3 = DotProduct(v3, ort) / ortdot;
	float x4 = DotProduct(v4, ort) / ortdot;
	float y1 = v1[2];
	float y2 = v2[2];
	float y3 = v3[2];
	float y4 = v4[2];
	vec3_t swapvector;
	float swap;
	if (x1 > x2)
	{
		swap = x1;
		x1 = x2;
		x2 = swap;
		swap = y1;
		y1 = y2;
		y2 = swap;
		VectorCopy(v1, swapvector);
		VectorCopy(v2, v1);
		VectorCopy(swapvector, v2);
	}
	if (x3 > x4)
	{
		swap = x3;
		x3 = x4;
		x4 = swap;
		swap = y3;
		y3 = y4;
		y4 = swap;
		VectorCopy(v3, swapvector);
		VectorCopy(v4, v3);
		VectorCopy(swapvector, v4);
	}
	if (x2 <= x3 || x4 <= x1)
	{
		return qfalse;
	}

	vec3_t p1area1;
	vec3_t p1area2;
	vec3_t p2area1;
	vec3_t p2area2;
	float dist1;
	float dist2;
	if ((x1 - 0.5f < x3 && x4 < x2 + 0.5f) &&
		(x3 - 0.5f < x1 && x2 < x4 + 0.5f))
	{
		dist1 = y3 - y1;
		dist2 = y4 - y2;
		VectorCopy(v1, p1area1);
		VectorCopy(v2, p2area1);
		VectorCopy(v3, p1area2);
		VectorCopy(v4, p2area2);
	}
	else
	{
		float y;
		if (x1 > x3 - 0.1f && x1 < x3 + 0.1f)
		{
			dist1 = y3 - y1;
			VectorCopy(v1, p1area1);
			VectorCopy(v3, p1area2);
		}
		else if (x1 < x3)
		{
			y = y1 + (x3 - x1) * (y2 - y1) / (x2 - x1);
			dist1 = y3 - y;
			VectorCopy(v3, p1area1);
			p1area1[2] = y;
			VectorCopy(v3, p1area2);
		}
		else
		{
			y = y3 + (x1 - x3) * (y4 - y3) / (x4 - x3);
			dist1 = y - y1;
			VectorCopy(v1, p1area1);
			VectorCopy(v1, p1area2);
			p1area2[2] = y;
		}

		if (x2 > x4 - 0.1f && x2 < x4 + 0.1f)
		{
			dist2 = y4 - y2;
			VectorCopy(v2, p2area1);
			VectorCopy(v4, p2area2);
		}
		else if (x2 < x4)
		{
			y = y3 + (x2 - x3) * (y4 - y3) / (x4 - x3);
			dist2 = y - y2;
			VectorCopy(v2, p2area1);
			VectorCopy(v2, p2area2);
			p2area2[2] = y;
		}
		else
		{
			y = y1 + (x4 - x1) * (y2 - y1) / (x2 - x1);
			dist2 = y4 - y;
			VectorCopy(v4, p2area1);
			p2area1[2] = y;
			VectorCopy(v4, p2area2);
		}
	}

	if (dist1 > dist2 - 1.0f && dist1 < dist2 + 1.0f)
	{
		*verticaldist = dist1;
		VectorAdd(p1area1, p2area1, start);
		VectorScale(start, 0.5f, start);
		VectorAdd(p1area2, p2area2, end);
		VectorScale(end, 0.5f, end);
	}
	else if (dist1 < dist2)
	{
		*verticaldist = dist1;
		VectorCopy(p1area1, start);
		VectorCopy(p1area2, end);
	}
	else
	{
		*verticaldist = dist2;
		VectorCopy(p2area1, start);
		VectorCopy(p2area2, end);
	}

	vec3_t direction;
	VectorSubtract(p2area2, p1area2, direction);
	*overlaplength = AAS_VectorLength(direction);
	return qtrue;
}

/*
=============
AAS_UpdateAdjacentCandidate

Keep the retail lowest candidate, preferring the longest overlap when two
vertical distances are within one unit.
=============
*/
static void AAS_UpdateAdjacentCandidate(aas_adjacent_reach_candidate_t *candidate,
	float dist,
	float length,
	int edgenum,
	const vec3_t start,
	const vec3_t end,
	const vec3_t normal)
{
	if (candidate->found && dist >= candidate->dist &&
		(dist >= candidate->dist + 1.0f || length <= candidate->length))
	{
		return;
	}

	candidate->found = qtrue;
	candidate->dist = dist;
	candidate->length = length;
	candidate->edgenum = edgenum;
	VectorCopy(start, candidate->start);
	VectorCopy(end, candidate->end);
	VectorCopy(normal, candidate->normal);
}

/*
=============
AAS_LinkAdjacentReachability

Allocate and prepend one temporary reachability produced by the adjacent-edge
generator.
=============
*/
static aas_lreachability_t *AAS_LinkAdjacentReachability(int area1num,
	int area2num,
	int edgenum,
	const vec3_t start,
	const vec3_t end,
	int traveltype,
	unsigned short traveltime)
{
	aas_lreachability_t *reachability = AAS_AllocReachability();
	if (reachability == NULL)
	{
		return NULL;
	}

	reachability->areanum = area2num;
	reachability->facenum = 0;
	reachability->edgenum = edgenum;
	VectorCopy(start, reachability->start);
	VectorCopy(end, reachability->end);
	reachability->traveltype = traveltype;
	reachability->traveltime = traveltime;
	reachability->next = areareachability[area1num];
	areareachability[area1num] = reachability;
	return reachability;
}

/*
=============
AAS_Reachability_Step_Barrier_WaterJump_WalkOffLedge

Create the retail step, water-jump, barrier-jump, downhill-walk, or
walk-off-ledge link between nearby ground or liquid-surface edges.
=============
*/
int AAS_Reachability_Step_Barrier_WaterJump_WalkOffLedge(int area1num,
	int area2num)
{
	if (areareachability == NULL || aasworld.areas == NULL ||
		aasworld.areasettings == NULL || aasworld.faceIndex == NULL ||
		aasworld.faces == NULL || aasworld.edgeIndex == NULL ||
		aasworld.edges == NULL || aasworld.vertexes == NULL ||
		aasworld.planes == NULL || area1num <= 0 || area2num <= 0 ||
		area1num >= reachabilityareacount ||
		area2num >= aasworld.numAreaSettings ||
		area1num >= aasworld.numAreas || area2num >= aasworld.numAreas)
	{
		return qfalse;
	}
	if ((!AAS_AreaGrounded(area1num) && !AAS_AreaSwim(area1num)) ||
		(!AAS_AreaGrounded(area2num) && !AAS_AreaSwim(area2num)))
	{
		return qfalse;
	}

	const aas_area_t *area1 = &aasworld.areas[area1num];
	const aas_area_t *area2 = &aasworld.areas[area2num];
	for (int axis = 0; axis < 2; ++axis)
	{
		if (area1->mins[axis] > area2->maxs[axis] + 10.0f ||
			area1->maxs[axis] < area2->mins[axis] - 10.0f)
		{
			return qfalse;
		}
	}
	if (area1->firstface < 0 || area1->numfaces < 0 ||
		area1->firstface + area1->numfaces > aasworld.faceIndexSize ||
		area2->firstface < 0 || area2->numfaces < 0 ||
		area2->firstface + area2->numfaces > aasworld.faceIndexSize)
	{
		return qfalse;
	}

	aas_adjacent_reach_candidate_t groundcandidate;
	aas_adjacent_reach_candidate_t watercandidate;
	memset(&groundcandidate, 0, sizeof(groundcandidate));
	memset(&watercandidate, 0, sizeof(watercandidate));
	groundcandidate.dist = 99999.0f;
	watercandidate.dist = 99999.0f;
	vec3_t invgravity = {0.0f, 0.0f, 1.0f};
	qboolean area1swim = AAS_AreaSwim(area1num);

	for (int face1index = 0; face1index < area1->numfaces; ++face1index)
	{
		int signedface1 = aasworld.faceIndex[area1->firstface + face1index];
		int faceside1 = signedface1 < 0;
		int face1num = abs(signedface1);
		if (face1num < 0 || face1num >= aasworld.numFaces)
		{
			continue;
		}
		const aas_face_t *face1 = &aasworld.faces[face1num];
		qboolean groundface = (face1->faceflags & AAS_FACE_GROUND) != 0;
		if (!groundface)
		{
			int planenum = face1->planenum ^ (!faceside1);
			if (!area1swim || planenum < 0 || planenum >= aasworld.numPlanes ||
				DotProduct(aasworld.planes[planenum].normal, invgravity) < 0.7f)
			{
				continue;
			}
		}
		if (face1->firstedge < 0 || face1->numedges < 0 ||
			face1->firstedge + face1->numedges > aasworld.edgeIndexSize)
		{
			continue;
		}

		for (int edge1index = 0; edge1index < face1->numedges; ++edge1index)
		{
			int signededge1 = aasworld.edgeIndex[face1->firstedge + edge1index];
			int side1 = signededge1 < 0;
			if (!groundface)
			{
				side1 = side1 == faceside1;
			}
			int edge1num = abs(signededge1);
			if (edge1num < 0 || edge1num >= aasworld.numEdges)
			{
				continue;
			}
			const aas_edge_t *edge1 = &aasworld.edges[edge1num];
			if (edge1->v[0] < 0 || edge1->v[0] >= aasworld.numVertexes ||
				edge1->v[1] < 0 || edge1->v[1] >= aasworld.numVertexes)
			{
				continue;
			}

			vec3_t v1;
			vec3_t v2;
			VectorCopy(aasworld.vertexes[edge1->v[!side1]], v1);
			VectorCopy(aasworld.vertexes[edge1->v[side1]], v2);
			vec3_t edgevector;
			VectorSubtract(v2, v1, edgevector);
			vec3_t normal;
			AAS_CrossProduct(edgevector, invgravity, normal);
			if (AAS_VectorNormalize(normal) == 0.0f)
			{
				continue;
			}
			float planedist = DotProduct(normal, v1);

			for (int face2index = 0; face2index < area2->numfaces; ++face2index)
			{
				int face2num = abs(aasworld.faceIndex[area2->firstface + face2index]);
				if (face2num < 0 || face2num >= aasworld.numFaces)
				{
					continue;
				}
				const aas_face_t *face2 = &aasworld.faces[face2num];
				if ((face2->faceflags & AAS_FACE_GROUND) == 0 ||
					face2->firstedge < 0 || face2->numedges < 0 ||
					face2->firstedge + face2->numedges > aasworld.edgeIndexSize)
				{
					continue;
				}

				for (int edge2index = 0; edge2index < face2->numedges; ++edge2index)
				{
					int edge2num = abs(aasworld.edgeIndex[face2->firstedge + edge2index]);
					if (edge2num < 0 || edge2num >= aasworld.numEdges)
					{
						continue;
					}
					const aas_edge_t *edge2 = &aasworld.edges[edge2num];
					if (edge2->v[0] < 0 || edge2->v[0] >= aasworld.numVertexes ||
						edge2->v[1] < 0 || edge2->v[1] >= aasworld.numVertexes)
					{
						continue;
					}
					const vec_t *v3 = aasworld.vertexes[edge2->v[0]];
					const vec_t *v4 = aasworld.vertexes[edge2->v[1]];
					float diff = DotProduct(normal, v3) - planedist;
					if (diff < -0.1f || diff > 0.1f)
					{
						continue;
					}
					diff = DotProduct(normal, v4) - planedist;
					if (diff < -0.1f || diff > 0.1f)
					{
						continue;
					}

					vec3_t start;
					vec3_t end;
					float dist;
					float length;
					if (!AAS_AdjacentEdgeOverlap(v1, v2, v3, v4, normal,
						start, end, &dist, &length))
					{
						continue;
					}
					if (groundface)
					{
						AAS_UpdateAdjacentCandidate(&groundcandidate, dist, length,
							edge1num, start, end, normal);
					}
					else
					{
						AAS_UpdateAdjacentCandidate(&watercandidate, dist, length,
							edge1num, start, end, normal);
					}
				}
			}
		}
	}

	float maxstep = AAS_ReachPositiveLibVarValue(Bridge_MaxStep(), 18.0f);
	float maxwaterjump = AAS_ReachPositiveLibVarValue(Bridge_MaxWaterJump(), 21.0f);
	float maxbarrier = AAS_ReachPositiveLibVarValue(Bridge_MaxBarrier(), 50.0f);
	if (groundcandidate.found && groundcandidate.dist >= 0.0f &&
		groundcandidate.dist < maxstep)
	{
		vec3_t start;
		vec3_t end;
		VectorMA(groundcandidate.start, INSIDEUNITS_WALKSTART,
			groundcandidate.normal, start);
		VectorMA(groundcandidate.end, INSIDEUNITS_WALKEND,
			groundcandidate.normal, end);
		/*
		 * Retail seeds the step link with traveltime 1 (0x100130c5) and then
		 * adds 300 for a crouch-only destination (0x100130d5).
		 */
		unsigned short traveltime = 1;
		if (!AAS_AreaCrouch(area1num) && AAS_AreaCrouch(area2num))
		{
			traveltime = (unsigned short)(traveltime +
				(unsigned short)AAS_ReachLibVarValue("rs_startcrouch", 300.0f));
		}
		aas_lreachability_t *reachability = AAS_LinkAdjacentReachability(area1num,
			area2num, groundcandidate.edgenum, start, end, TRAVEL_WALK, traveltime);
		if (reachability == NULL)
		{
			return qfalse;
		}
		/*
		 * Retail applies both 400 unit penalties after prepending the record:
		 * 0x10013116 when AAS_NearbySolidOrGap(reach->start, reach->end) is
		 * false and 0x1001311f when AAS_AreaGroundFaceArea(reach->areanum)
		 * is under 500.
		 */
		if (!AAS_NearbySolidOrGap(reachability->start, reachability->end))
		{
			reachability->traveltime =
				(unsigned short)(reachability->traveltime + 400);
		}
		if (AAS_AreaGroundFaceArea(reachability->areanum) < 500.0f)
		{
			reachability->traveltime =
				(unsigned short)(reachability->traveltime + 400);
		}
		reach_step++;
		return qtrue;
	}

	if (watercandidate.found)
	{
		vec3_t testpoint;
		VectorMA(watercandidate.end, -INSIDEUNITS,
			watercandidate.normal, testpoint);
		testpoint[2] -= maxwaterjump;
		int testarea = AAS_PointAreaNum(testpoint);
		if (testarea > 0 && AAS_AreaSwim(testarea) &&
			watercandidate.dist < maxwaterjump + 24.0f &&
			(aasworld.areasettings[area1num].presencetype & PRESENCE_NORMAL) != 0 &&
			(aasworld.areasettings[area2num].presencetype & PRESENCE_NORMAL) != 0)
		{
			vec3_t end;
			VectorMA(watercandidate.end, INSIDEUNITS_WATERJUMP,
				watercandidate.normal, end);
			/* Retail hard-codes 700 for the water jump (0x10013280). */
			if (AAS_LinkAdjacentReachability(area1num, area2num,
				watercandidate.edgenum, watercandidate.start, end,
				TRAVEL_WATERJUMP, 700) == NULL)
			{
				return qfalse;
			}
			reach_waterjump++;
			return qtrue;
		}
	}

	if (groundcandidate.found && groundcandidate.dist > 0.0f &&
		groundcandidate.dist < maxbarrier &&
		(!watercandidate.found ||
			groundcandidate.dist - watercandidate.dist < 16.0f) &&
		!AAS_AreaCrouch(area1num) && !AAS_AreaCrouch(area2num))
	{
		vec3_t start;
		vec3_t end;
		VectorMA(groundcandidate.start, INSIDEUNITS_WALKSTART,
			groundcandidate.normal, start);
		VectorMA(groundcandidate.end, INSIDEUNITS_WALKEND,
			groundcandidate.normal, end);
		/* Retail hard-codes 400 for the barrier jump (0x100133ad). */
		if (AAS_LinkAdjacentReachability(area1num, area2num,
			groundcandidate.edgenum, start, end, TRAVEL_BARRIERJUMP,
			400) == NULL)
		{
			return qfalse;
		}
		reach_barrier++;
		return qtrue;
	}

	if (!groundcandidate.found || groundcandidate.dist >= 0.0f)
	{
		return qfalse;
	}
	if (groundcandidate.dist > -maxstep)
	{
		vec3_t start;
		vec3_t end;
		VectorMA(groundcandidate.start, INSIDEUNITS_WALKSTART,
			groundcandidate.normal, start);
		VectorMA(groundcandidate.end, INSIDEUNITS_WALKEND,
			groundcandidate.normal, end);
		if (AAS_LinkAdjacentReachability(area1num, area2num,
			groundcandidate.edgenum, start, end, TRAVEL_WALK, 1) == NULL)
		{
			return qfalse;
		}
		reach_walk++;
		return qtrue;
	}

	/*
	 * Retail gates the walk-off-ledge branch on
	 * -AAS_FallDamageDistance() < dist or a swimmable destination
	 * (0x100134bc-0x100134d9). It has no rs_maxfallheight limit.
	 */
	if (!(-(float)AAS_FallDamageDistance() < groundcandidate.dist) &&
		!AAS_AreaSwim(area2num))
	{
		return qfalse;
	}
	VectorMA(groundcandidate.end, INSIDEUNITS,
		groundcandidate.normal, groundcandidate.end);
	vec3_t start;
	vec3_t end;
	VectorCopy(groundcandidate.end, start);
	start[2] = groundcandidate.start[2];
	VectorCopy(groundcandidate.end, end);
	end[2] += 4.0f;
	aas_trace_t trace = AAS_TraceClientBBox(start, end, PRESENCE_NORMAL, -1);
	if (trace.startsolid || trace.fraction < 1.0f)
	{
		return qfalse;
	}
	trace.endpos[2] += 1.0f;
	if (AAS_PointAreaNum(trace.endpos) != area2num)
	{
		return qfalse;
	}

	/*
	 * Retail stores a flat travel time of 100 (0x10013638) with no
	 * cluster-portal rejection and no fall-damage penalty; the fall damage
	 * is already accounted for by the gate above.
	 */
	if (AAS_LinkAdjacentReachability(area1num, area2num,
		groundcandidate.edgenum, groundcandidate.start, groundcandidate.end,
		TRAVEL_WALKOFFLEDGE, 100) == NULL)
	{
		return qfalse;
	}
	reach_walkoffledge++;
	return qtrue;
}

/*
=============
AAS_VectorDistance

Return the Euclidean distance between two reachability geometry points.
=============
*/
static float AAS_VectorDistance(const vec3_t first, const vec3_t second)
{
	vec3_t direction;
	VectorSubtract(second, first, direction);
	return AAS_VectorLength(direction);
}

/*
=============
AAS_VectorBetweenVectors

Return true when a point projects between both endpoints of a segment.
=============
*/
static qboolean AAS_VectorBetweenVectors(const vec3_t point,
	const vec3_t first,
	const vec3_t second)
{
	vec3_t direction1;
	vec3_t direction2;
	VectorSubtract(point, first, direction1);
	VectorSubtract(point, second, direction2);
	return DotProduct(direction1, direction2) <= 0.0f;
}

/*
=============
AAS_ExtendClosestRange

Extend one endpoint of an equal-distance point range using the retail
farthest-from-opposite-end comparison.
=============
*/
static void AAS_ExtendClosestRange(vec3_t first,
	vec3_t second,
	const vec3_t point)
{
	float firstdistance = AAS_VectorDistance(first, point);
	float seconddistance = AAS_VectorDistance(second, point);
	float rangedistance = AAS_VectorDistance(first, second);
	if (firstdistance > seconddistance)
	{
		if (firstdistance > rangedistance)
		{
			VectorCopy(point, second);
		}
	}
	else if (seconddistance > rangedistance)
	{
		VectorCopy(point, first);
	}
}

/*
=============
AAS_ConsiderClosestEdgePoint

Merge one projected edge-point pair into the current closest-distance ranges.
=============
*/
static void AAS_ConsiderClosestEdgePoint(const vec3_t start,
	const vec3_t end,
	vec3_t beststart1,
	vec3_t bestend1,
	vec3_t beststart2,
	vec3_t bestend2,
	float *bestdist)
{
	float distance = AAS_VectorDistance(start, end);
	if (distance > *bestdist - 0.5f && distance < *bestdist + 0.5f)
	{
		AAS_ExtendClosestRange(beststart1, beststart2, start);
		AAS_ExtendClosestRange(bestend1, bestend2, end);
	}
	else if (distance < *bestdist)
	{
		*bestdist = distance;
		VectorCopy(start, beststart1);
		VectorCopy(start, beststart2);
		VectorCopy(end, bestend1);
		VectorCopy(end, bestend2);
	}
}

/*
=============
AAS_ReplaceClosestEdgePoint

Replace the closest ranges only when a vertex-pair fallback is strictly
closer, matching the retail non-projecting path.
=============
*/
static void AAS_ReplaceClosestEdgePoint(const vec3_t start,
	const vec3_t end,
	vec3_t beststart1,
	vec3_t bestend1,
	vec3_t beststart2,
	vec3_t bestend2,
	float *bestdist)
{
	float distance = AAS_VectorDistance(start, end);
	if (distance >= *bestdist)
	{
		return;
	}

	*bestdist = distance;
	VectorCopy(start, beststart1);
	VectorCopy(start, beststart2);
	VectorCopy(end, bestend1);
	VectorCopy(end, bestend2);
}

/*
=============
AAS_ClosestEdgePoints

Calculate the closest point ranges on two ground edges after projecting each
edge endpoint onto the other edge's horizontal line and ground plane.
=============
*/
float AAS_ClosestEdgePoints(const vec3_t v1,
	const vec3_t v2,
	const vec3_t v3,
	const vec3_t v4,
	const aas_plane_t *plane1,
	const aas_plane_t *plane2,
	vec3_t beststart1,
	vec3_t bestend1,
	vec3_t beststart2,
	vec3_t bestend2,
	float bestdist)
{
	if (plane1 == NULL || plane2 == NULL ||
		fabsf(plane1->normal[2]) <= 0.000001f ||
		fabsf(plane2->normal[2]) <= 0.000001f)
	{
		return bestdist;
	}

	vec3_t direction1;
	vec3_t direction2;
	VectorSubtract(v2, v1, direction1);
	VectorSubtract(v4, v3, direction2);
	direction1[2] = 0.0f;
	direction2[2] = 0.0f;

	vec3_t p1;
	vec3_t p2;
	vec3_t p3;
	vec3_t p4;
	if (direction2[0] != 0.0f)
	{
		float slope2 = direction2[1] / direction2[0];
		float offset2 = v3[1] - slope2 * v3[0];
		p1[0] = (DotProduct(v1, direction2) -
			(slope2 * direction2[0] + offset2 * direction2[1])) /
			direction2[0];
		p1[1] = slope2 * p1[0] + offset2;
		p2[0] = (DotProduct(v2, direction2) -
			(slope2 * direction2[0] + offset2 * direction2[1])) /
			direction2[0];
		p2[1] = slope2 * p2[0] + offset2;
	}
	else
	{
		p1[0] = v3[0];
		p1[1] = v1[1];
		p2[0] = v3[0];
		p2[1] = v2[1];
	}
	if (direction1[0] != 0.0f)
	{
		float slope1 = direction1[1] / direction1[0];
		float offset1 = v1[1] - slope1 * v1[0];
		p3[0] = (DotProduct(v3, direction1) -
			(slope1 * direction1[0] + offset1 * direction1[1])) /
			direction1[0];
		p3[1] = slope1 * p3[0] + offset1;
		p4[0] = (DotProduct(v4, direction1) -
			(slope1 * direction1[0] + offset1 * direction1[1])) /
			direction1[0];
		p4[1] = slope1 * p4[0] + offset1;
	}
	else
	{
		p3[0] = v1[0];
		p3[1] = v3[1];
		p4[0] = v1[0];
		p4[1] = v4[1];
	}

	p1[2] = 0.0f;
	p2[2] = 0.0f;
	p3[2] = 0.0f;
	p4[2] = 0.0f;
	p1[2] = (plane2->dist - DotProduct(plane2->normal, p1)) /
		plane2->normal[2];
	p2[2] = (plane2->dist - DotProduct(plane2->normal, p2)) /
		plane2->normal[2];
	p3[2] = (plane1->dist - DotProduct(plane1->normal, p3)) /
		plane1->normal[2];
	p4[2] = (plane1->dist - DotProduct(plane1->normal, p4)) /
		plane1->normal[2];

	qboolean founddistance = qfalse;
	if (AAS_VectorBetweenVectors(p1, v3, v4))
	{
		AAS_ConsiderClosestEdgePoint(v1, p1, beststart1, bestend1,
			beststart2, bestend2, &bestdist);
		founddistance = qtrue;
	}
	if (AAS_VectorBetweenVectors(p2, v3, v4))
	{
		AAS_ConsiderClosestEdgePoint(v2, p2, beststart1, bestend1,
			beststart2, bestend2, &bestdist);
		founddistance = qtrue;
	}
	if (AAS_VectorBetweenVectors(p3, v1, v2))
	{
		AAS_ConsiderClosestEdgePoint(p3, v3, beststart1, bestend1,
			beststart2, bestend2, &bestdist);
		founddistance = qtrue;
	}
	if (AAS_VectorBetweenVectors(p4, v1, v2))
	{
		AAS_ConsiderClosestEdgePoint(p4, v4, beststart1, bestend1,
			beststart2, bestend2, &bestdist);
		founddistance = qtrue;
	}
	if (!founddistance)
	{
		AAS_ReplaceClosestEdgePoint(v1, v3, beststart1, bestend1,
			beststart2, bestend2, &bestdist);
		AAS_ReplaceClosestEdgePoint(v1, v4, beststart1, bestend1,
			beststart2, bestend2, &bestdist);
		AAS_ReplaceClosestEdgePoint(v2, v3, beststart1, bestend1,
			beststart2, bestend2, &bestdist);
		AAS_ReplaceClosestEdgePoint(v2, v4, beststart1, bestend1,
			beststart2, bestend2, &bestdist);
	}
	return bestdist;
}

/*
=============
AAS_VectorMiddle

Return the midpoint of two reachability geometry points.
=============
*/
static void AAS_VectorMiddle(const vec3_t first,
	const vec3_t second,
	vec3_t middle)
{
	VectorAdd(first, second, middle);
	VectorScale(middle, 0.5f, middle);
}

/*
=============
AAS_Reachability_Jump

Create a predicted jump or long walk-off-ledge reachability between the
closest ground-edge ranges of two areas.
=============
*/
int AAS_Reachability_Jump(int area1num, int area2num)
{
	if (areareachability == NULL || aasworld.areas == NULL ||
		aasworld.areasettings == NULL || aasworld.faceIndex == NULL ||
		aasworld.faces == NULL || aasworld.edgeIndex == NULL ||
		aasworld.edges == NULL || aasworld.vertexes == NULL ||
		aasworld.planes == NULL || area1num <= 0 || area2num <= 0 ||
		area1num >= reachabilityareacount ||
		area2num >= aasworld.numAreaSettings ||
		area1num >= aasworld.numAreas || area2num >= aasworld.numAreas)
	{
		return qfalse;
	}
	if (!AAS_AreaGrounded(area1num) || !AAS_AreaGrounded(area2num) ||
		AAS_AreaCrouch(area1num) || AAS_AreaCrouch(area2num))
	{
		return qfalse;
	}

	const aas_area_t *area1 = &aasworld.areas[area1num];
	const aas_area_t *area2 = &aasworld.areas[area2num];
	float jumpvelocity = AAS_ReachPositiveLibVarValue(Bridge_JumpVelocity(), 224.0f);
	/*
	 * Retail stores AAS_MaxJumpDistance verbatim: 0x10013d50 calls the helper
	 * and 0x10013d55 stores the result with no intervening multiply.  The
	 * successor doubles it; doubling here makes both the x-y prefilter and the
	 * final bestdist gate twice as permissive and emits links retail never has.
	 */
	float maxjumpdistance = AAS_MaxJumpDistance(jumpvelocity);
	float maxjumpheight = AAS_MaxJumpHeight(jumpvelocity);
	for (int axis = 0; axis < 2; ++axis)
	{
		if (area1->mins[axis] > area2->maxs[axis] + maxjumpdistance ||
			area1->maxs[axis] < area2->mins[axis] - maxjumpdistance)
		{
			return qfalse;
		}
	}
	if (area2->mins[2] > area1->maxs[2] + maxjumpheight ||
		area1->firstface < 0 || area1->numfaces < 0 ||
		area1->firstface + area1->numfaces > aasworld.faceIndexSize ||
		area2->firstface < 0 || area2->numfaces < 0 ||
		area2->firstface + area2->numfaces > aasworld.faceIndexSize)
	{
		return qfalse;
	}

	float bestdistance = 999999.0f;
	vec3_t beststart = {0.0f, 0.0f, 0.0f};
	vec3_t beststart2 = {0.0f, 0.0f, 0.0f};
	vec3_t bestend = {0.0f, 0.0f, 0.0f};
	vec3_t bestend2 = {0.0f, 0.0f, 0.0f};
	for (int face1index = 0; face1index < area1->numfaces; ++face1index)
	{
		int face1num = abs(aasworld.faceIndex[area1->firstface + face1index]);
		if (face1num < 0 || face1num >= aasworld.numFaces)
		{
			continue;
		}
		const aas_face_t *face1 = &aasworld.faces[face1num];
		if ((face1->faceflags & AAS_FACE_GROUND) == 0 ||
			face1->firstedge < 0 || face1->numedges < 0 ||
			face1->firstedge + face1->numedges > aasworld.edgeIndexSize ||
			face1->planenum < 0 || face1->planenum >= aasworld.numPlanes)
		{
			continue;
		}

		for (int face2index = 0; face2index < area2->numfaces; ++face2index)
		{
			int face2num = abs(aasworld.faceIndex[area2->firstface + face2index]);
			if (face2num < 0 || face2num >= aasworld.numFaces)
			{
				continue;
			}
			const aas_face_t *face2 = &aasworld.faces[face2num];
			if ((face2->faceflags & AAS_FACE_GROUND) == 0 ||
				face2->firstedge < 0 || face2->numedges < 0 ||
				face2->firstedge + face2->numedges > aasworld.edgeIndexSize ||
				face2->planenum < 0 || face2->planenum >= aasworld.numPlanes)
			{
				continue;
			}

			for (int edge1index = 0; edge1index < face1->numedges; ++edge1index)
			{
				int edge1num = abs(aasworld.edgeIndex[face1->firstedge + edge1index]);
				if (edge1num < 0 || edge1num >= aasworld.numEdges)
				{
					continue;
				}
				const aas_edge_t *edge1 = &aasworld.edges[edge1num];
				if (edge1->v[0] < 0 || edge1->v[0] >= aasworld.numVertexes ||
					edge1->v[1] < 0 || edge1->v[1] >= aasworld.numVertexes)
				{
					continue;
				}

				for (int edge2index = 0; edge2index < face2->numedges; ++edge2index)
				{
					int edge2num = abs(aasworld.edgeIndex[face2->firstedge + edge2index]);
					if (edge2num < 0 || edge2num >= aasworld.numEdges)
					{
						continue;
					}
					const aas_edge_t *edge2 = &aasworld.edges[edge2num];
					if (edge2->v[0] < 0 || edge2->v[0] >= aasworld.numVertexes ||
						edge2->v[1] < 0 || edge2->v[1] >= aasworld.numVertexes)
					{
						continue;
					}
					bestdistance = AAS_ClosestEdgePoints(
						aasworld.vertexes[edge1->v[0]],
						aasworld.vertexes[edge1->v[1]],
						aasworld.vertexes[edge2->v[0]],
						aasworld.vertexes[edge2->v[1]],
						&aasworld.planes[face1->planenum],
						&aasworld.planes[face2->planenum],
						beststart,
						bestend,
						beststart2,
						bestend2,
						bestdistance);
				}
			}
		}
	}

	AAS_VectorMiddle(beststart, beststart2, beststart);
	AAS_VectorMiddle(bestend, bestend2, bestend);
	if (bestdistance <= 4.0f || bestdistance >= maxjumpdistance)
	{
		return qfalse;
	}

	/*
	 * 0x10014648-0x1001466e rejects the pair before any speed is chosen when
	 * the drop exceeds the fall-damage distance.  Retail continues only while
	 * AAS_FallDamageDistance() >= beststart[2] - bestend[2].
	 */
	if ((float)AAS_FallDamageDistance() < beststart[2] - bestend[2])
	{
		return qfalse;
	}

	/*
	 * Retail has exactly two speed branches, 0x1001468f and 0x100146c4: the
	 * zero-z-velocity solve gives TRAVEL_WALKOFFLEDGE with a 1.2 multiplier
	 * (0x100146a0), and only if that solve fails does it retry with sv_jumpvel
	 * for TRAVEL_JUMP - with no multiplier at all.  The 48-unit "short hop" at
	 * speed 400 and the 1.05 jump multiplier are successor constants that
	 * appear nowhere in 0x10013cc0..0x10014ac6.
	 */
	float speed;
	int traveltype;
	if (AAS_HorizontalVelocityForJump(0.0f, beststart, bestend, &speed))
	{
		speed *= 1.2f;
		traveltype = TRAVEL_WALKOFFLEDGE;
	}
	else
	{
		if (!AAS_HorizontalVelocityForJump(jumpvelocity, beststart, bestend, &speed))
		{
			return qfalse;
		}
		traveltype = TRAVEL_JUMP;
	}

	/*
	 * The 10-unit horizontal-length gate sits on the common join at
	 * label_100146db (0x100146f4-0x10014707), so it applies to both travel
	 * types, not to TRAVEL_JUMP alone.
	 */
	vec3_t horizontaldirection;
	VectorSubtract(bestend, beststart, horizontaldirection);
	horizontaldirection[2] = 0.0f;
	if (AAS_VectorLength(horizontaldirection) < 10.0f)
	{
		return qfalse;
	}

	vec3_t direction;
	VectorSubtract(bestend, beststart, direction);
	if (AAS_VectorNormalize(direction) == 0.0f)
	{
		return qfalse;
	}
	vec3_t teststart;
	vec3_t testend;
	VectorMA(beststart, 1.0f, direction, teststart);
	VectorCopy(teststart, testend);
	testend[2] -= 100.0f;
	aas_trace_t trace = AAS_TraceClientBBox(teststart, testend, PRESENCE_NORMAL, -1);
	if (trace.startsolid)
	{
		return qfalse;
	}
	if (trace.fraction < 1.0f && trace.planenum >= 0 &&
		trace.planenum < aasworld.numPlanes)
	{
		/*
		 * Retail's two barrier probes test only the plane normal and the drop
		 * (0x100147e7/0x1001480d and 0x100148be/0x100148de); AAS_PointContents
		 * is never called anywhere in this routine, so a probe landing in lava
		 * or slime must still reject the candidate.
		 */
		const aas_plane_t *plane = &aasworld.planes[trace.planenum];
		if (plane->normal[2] >= 0.7f &&
			teststart[2] - trace.endpos[2] <=
				AAS_ReachPositiveLibVarValue(Bridge_MaxBarrier(), 50.0f))
		{
			return qfalse;
		}
	}

	VectorMA(bestend, -1.0f, direction, teststart);
	VectorCopy(teststart, testend);
	testend[2] -= 100.0f;
	trace = AAS_TraceClientBBox(teststart, testend, PRESENCE_NORMAL, -1);
	if (trace.startsolid)
	{
		return qfalse;
	}
	if (trace.fraction < 1.0f && trace.planenum >= 0 &&
		trace.planenum < aasworld.numPlanes)
	{
		/*
		 * Retail's two barrier probes test only the plane normal and the drop
		 * (0x100147e7/0x1001480d and 0x100148be/0x100148de); AAS_PointContents
		 * is never called anywhere in this routine, so a probe landing in lava
		 * or slime must still reject the candidate.
		 */
		const aas_plane_t *plane = &aasworld.planes[trace.planenum];
		if (plane->normal[2] >= 0.7f &&
			teststart[2] - trace.endpos[2] <=
				AAS_ReachPositiveLibVarValue(Bridge_MaxBarrier(), 50.0f))
		{
			return qfalse;
		}
	}

	/*
	 * Retail drives the test jump entirely from the command vector: 0x1001497e
	 * passes sub_1000f840 the shared zero vector data_100631cc as the initial
	 * velocity and &var_f4 as cmdmove, where var_f4 is VectorScale(hordir,
	 * speed) with its Z overwritten by sv_jumpvel for TRAVEL_JUMP and 0
	 * otherwise (0x10014927 / 0x1001493d / 0x10014946).  The order matters:
	 * the predictor's on-ground command loop drives frame_test_vel straight at
	 * cmdmove * frametime, so handing it the speed as a starting velocity with
	 * a purely vertical cmdmove - the later Quake III arrangement - zeroes the
	 * horizontal velocity on frame 0 and the probe jump lands back where it
	 * started.  That is why the generator produced no jump links at all.
	 */
	vec3_t direction2d;
	VectorSubtract(bestend, beststart, direction2d);
	direction2d[2] = 0.0f;
	if (AAS_VectorNormalize(direction2d) == 0.0f)
	{
		return qfalse;
	}
	vec3_t cmdmove;
	VectorScale(direction2d, speed, cmdmove);
	cmdmove[2] = ((traveltype & TRAVELTYPE_MASK) == TRAVEL_JUMP)
		? jumpvelocity
		: 0.0f;
	vec3_t velocity = {0.0f, 0.0f, 0.0f};

	/*
	 * Retail predicts exactly once with the literal stop-event mask 0x3d
	 * (0x1001497e).  There is no sidewards retry sweep and no cluster-portal
	 * stop event: sub_1000f840 takes the mask in a char parameter, so
	 * SE_TOUCHCLUSTERPORTAL could never survive the call anyway.
	 */
	aas_clientmove_t move;
	AAS_PredictClientMovement(&move,
		-1,
		beststart,
		PRESENCE_NORMAL,
		qtrue,
		velocity,
		cmdmove,
		3,
		30,
		0.1f,
		SE_HITGROUND | SE_ENTERWATER | SE_ENTERSLIME | SE_ENTERLAVA |
			SE_HITGROUNDDAMAGE,
		0,
		qfalse);
	/* 0x100149ac tests move.frames < 30 and (move.stopevent & 0x38) == 0. */
	if (move.frames >= 30 ||
		(move.stopevent &
			(SE_ENTERSLIME | SE_ENTERLAVA | SE_HITGROUNDDAMAGE)) != 0)
	{
		return qfalse;
	}

	/*
	 * Retail walks back from the landing point in 8 unit steps along the
	 * horizontal direction, a quarter unit above the sample, and accepts the
	 * link as soon as AAS_PointAreaNum reports the destination area
	 * (0x100149c8-0x10014a05).  It gives up once the offset passes -32.
	 */
	int landed = qfalse;
	for (int offset = 0; offset >= -32; offset -= 8)
	{
		vec3_t sample;
		VectorMA(move.endpos, (float)offset, direction2d, sample);
		sample[2] += 0.125f;
		if (AAS_PointAreaNum(sample) == area2num)
		{
			landed = qtrue;
			break;
		}
	}
	if (!landed)
	{
		return qfalse;
	}

	aas_lreachability_t *reachability = AAS_LinkAdjacentReachability(
		area1num,
		area2num,
		0,
		beststart,
		bestend,
		traveltype,
		0);
	if (reachability == NULL)
	{
		return qfalse;
	}
	/*
	 * Retail costs both travel types with one unconditional expression at
	 * 0x10014a8f: (int)(VectorDistance(bestend, beststart) * 240 /
	 * sv_maxwalkvelocity + 600). There is no walk-off-ledge special case and
	 * no fall-damage penalty here.
	 */
	float maxwalkvelocity = AAS_ReachPositiveLibVarValue(
		Bridge_MaxWalkVelocity(), 300.0f);
	reachability->traveltime = (unsigned short)(int)(
		AAS_VectorDistance(bestend, beststart) * 240.0f / maxwalkvelocity +
		600.0f);
	/* Retail bumps the same counter for both travel types (0x10014ab5). */
	reach_jump++;

	return qfalse;
}

/*
=============
AAS_Reachability_Ladder

Create ladder adjacency, ladder-top, or jump-to-ladder links between ladder
areas that share an edge or meet the floor below a ladder face.
=============
*/
int AAS_Reachability_Ladder(int area1num, int area2num)
{
	if (areareachability == NULL || aasworld.areas == NULL ||
		aasworld.areasettings == NULL || aasworld.faceIndex == NULL ||
		aasworld.faces == NULL || aasworld.edgeIndex == NULL ||
		aasworld.edges == NULL || aasworld.vertexes == NULL ||
		aasworld.planes == NULL || area1num <= 0 || area2num <= 0 ||
		area1num >= reachabilityareacount ||
		area2num >= aasworld.numAreaSettings ||
		area1num >= aasworld.numAreas || area2num >= aasworld.numAreas)
	{
		return qfalse;
	}
	if (!AAS_AreaLadder(area1num) || !AAS_AreaLadder(area2num))
	{
		return qfalse;
	}

	float jumpvelocity = AAS_ReachPositiveLibVarValue(Bridge_JumpVelocity(), 224.0f);
	float maxjumpheight = AAS_MaxJumpHeight(jumpvelocity);
	const aas_area_t *area1 = &aasworld.areas[area1num];
	const aas_area_t *area2 = &aasworld.areas[area2num];
	if (area1->firstface < 0 || area1->numfaces < 0 ||
		area1->firstface + area1->numfaces > aasworld.faceIndexSize ||
		area2->firstface < 0 || area2->numfaces < 0 ||
		area2->firstface + area2->numfaces > aasworld.faceIndexSize)
	{
		return qfalse;
	}

	const aas_face_t *ladderface1 = NULL;
	const aas_face_t *ladderface2 = NULL;
	int ladderface1num = 0;
	int ladderface2num = 0;
	int sharededgenum = 0;
	float bestface1area = -9999.0f;
	float bestface2area = -9999.0f;
	for (int face1index = 0; face1index < area1->numfaces; ++face1index)
	{
		int face1num = aasworld.faceIndex[area1->firstface + face1index];
		int face1absolute = abs(face1num);
		if (face1absolute < 0 || face1absolute >= aasworld.numFaces)
		{
			continue;
		}
		const aas_face_t *face1 = &aasworld.faces[face1absolute];
		if ((face1->faceflags & AAS_FACE_LADDER) == 0 ||
			face1->firstedge < 0 || face1->numedges < 0 ||
			face1->firstedge + face1->numedges > aasworld.edgeIndexSize)
		{
			continue;
		}

		for (int face2index = 0; face2index < area2->numfaces; ++face2index)
		{
			int face2num = aasworld.faceIndex[area2->firstface + face2index];
			int face2absolute = abs(face2num);
			if (face2absolute < 0 || face2absolute >= aasworld.numFaces)
			{
				continue;
			}
			const aas_face_t *face2 = &aasworld.faces[face2absolute];
			if ((face2->faceflags & AAS_FACE_LADDER) == 0 ||
				face2->firstedge < 0 || face2->numedges < 0 ||
				face2->firstedge + face2->numedges > aasworld.edgeIndexSize)
			{
				continue;
			}

			qboolean shared = qfalse;
			for (int edge1index = 0; edge1index < face1->numedges && !shared;
				++edge1index)
			{
				int edge1num = aasworld.edgeIndex[face1->firstedge + edge1index];
				for (int edge2index = 0; edge2index < face2->numedges; ++edge2index)
				{
					int edge2num = aasworld.edgeIndex[face2->firstedge + edge2index];
					if (abs(edge1num) != abs(edge2num))
					{
						continue;
					}

					float face1area = AAS_FaceArea(face1);
					float face2area = AAS_FaceArea(face2);
					if (face1area > bestface1area && face2area > bestface2area)
					{
						bestface1area = face1area;
						bestface2area = face2area;
						ladderface1 = face1;
						ladderface2 = face2;
						ladderface1num = face1num;
						ladderface2num = face2num;
						sharededgenum = edge1num;
					}
					shared = qtrue;
					break;
				}
			}
		}
	}
	if (ladderface1 == NULL || ladderface2 == NULL ||
		abs(sharededgenum) >= aasworld.numEdges)
	{
		return qfalse;
	}

	const aas_edge_t *sharededge = &aasworld.edges[abs(sharededgenum)];
	if (sharededge->v[0] < 0 || sharededge->v[0] >= aasworld.numVertexes ||
		sharededge->v[1] < 0 || sharededge->v[1] >= aasworld.numVertexes)
	{
		return qfalse;
	}
	int firstvertex = sharededgenum < 0;
	vec3_t vertex1;
	vec3_t vertex2;
	VectorCopy(aasworld.vertexes[sharededge->v[firstvertex]], vertex1);
	VectorCopy(aasworld.vertexes[sharededge->v[!firstvertex]], vertex2);
	vec3_t area1point;
	vec3_t area2point;
	VectorAdd(vertex1, vertex2, area1point);
	VectorScale(area1point, 0.5f, area1point);
	VectorCopy(area1point, area2point);

	int plane1num = ladderface1->planenum ^ (ladderface1num < 0);
	int plane2num = ladderface2->planenum ^ (ladderface2num < 0);
	if (plane1num < 0 || plane1num >= aasworld.numPlanes ||
		plane2num < 0 || plane2num >= aasworld.numPlanes)
	{
		return qfalse;
	}
	const aas_plane_t *plane1 = &aasworld.planes[plane1num];
	const aas_plane_t *plane2 = &aasworld.planes[plane2num];
	vec3_t sharededgevector;
	VectorSubtract(vertex2, vertex1, sharededgevector);
	vec3_t direction;
	AAS_CrossProduct(plane1->normal, sharededgevector, direction);
	if (AAS_VectorNormalize(direction) == 0.0f)
	{
		return qfalse;
	}
	VectorMA(area1point, -32.0f, direction, area1point);
	VectorMA(area2point, 32.0f, direction, area2point);

	/*
	 * Retail truncates each float to int before taking its magnitude
	 * (0x1001524b and 0x1001527d call __ftol then the integer abs), so any
	 * normal whose z component is inside (-1, 1) counts as vertical.
	 */
	qboolean face1vertical = AAS_ReachAbsTrunc(plane1->normal[2]) < 0.1f;
	qboolean face2vertical = AAS_ReachAbsTrunc(plane2->normal[2]) < 0.1f;
	if (!face1vertical && !face2vertical)
	{
		return qfalse;
	}
	if (face1vertical && face2vertical &&
		DotProduct(plane1->normal, plane2->normal) > 0.7f &&
		/* Retail truncates before the magnitude here too (0x100152e5). */
		AAS_ReachAbsTrunc(sharededgevector[2]) < 0.7f)
	{
		vec3_t end;
		VectorMA(area2point, -3.0f, plane1->normal, end);
		aas_lreachability_t *forward = AAS_LinkAdjacentReachability(
			area1num,
			area2num,
			abs(sharededgenum),
			area1point,
			end,
			TRAVEL_LADDER,
			10);
		if (forward == NULL)
		{
			return qfalse;
		}
		forward->facenum = ladderface1num;
		reach_ladder++;

		VectorMA(area1point, -3.0f, plane1->normal, end);
		aas_lreachability_t *reverse = AAS_LinkAdjacentReachability(
			area2num,
			area1num,
			abs(sharededgenum),
			area2point,
			end,
			TRAVEL_LADDER,
			10);
		if (reverse == NULL)
		{
			return qfalse;
		}
		reverse->facenum = ladderface2num;
		reach_ladder++;
		return qtrue;
	}

	if (face1vertical && (ladderface2->faceflags & AAS_FACE_GROUND) != 0)
	{
		vec3_t end;
		VectorCopy(area2point, end);
		end[2] += 16.0f;
		VectorMA(end, -15.0f, plane1->normal, end);
		aas_lreachability_t *forward = AAS_LinkAdjacentReachability(
			area1num,
			area2num,
			abs(sharededgenum),
			area1point,
			end,
			TRAVEL_LADDER,
			10);
		if (forward == NULL)
		{
			return qfalse;
		}
		forward->facenum = ladderface1num;
		reach_ladder++;

		aas_lreachability_t *reverse = AAS_LinkAdjacentReachability(
			area2num,
			area1num,
			abs(sharededgenum),
			area2point,
			area1point,
			TRAVEL_WALKOFFLEDGE,
			10);
		if (reverse == NULL)
		{
			return qfalse;
		}
		reverse->facenum = ladderface2num;
		reach_walkoffledge++;
		return qtrue;
	}

	if (!face1vertical || ladderface1->firstedge < 0 ||
		ladderface1->numedges <= 0 ||
		ladderface1->firstedge + ladderface1->numedges > aasworld.edgeIndexSize)
	{
		return qfalse;
	}
	vec3_t lowestpoint = {0.0f, 0.0f, 99999.0f};
	int lowestedgenum = 0;
	for (int edgeindex = 0; edgeindex < ladderface1->numedges; ++edgeindex)
	{
		int edgenum = abs(aasworld.edgeIndex[ladderface1->firstedge + edgeindex]);
		if (edgenum < 0 || edgenum >= aasworld.numEdges)
		{
			continue;
		}
		const aas_edge_t *edge = &aasworld.edges[edgenum];
		if (edge->v[0] < 0 || edge->v[0] >= aasworld.numVertexes ||
			edge->v[1] < 0 || edge->v[1] >= aasworld.numVertexes)
		{
			continue;
		}
		vec3_t midpoint;
		VectorAdd(aasworld.vertexes[edge->v[0]],
			aasworld.vertexes[edge->v[1]], midpoint);
		VectorScale(midpoint, 0.5f, midpoint);
		if (midpoint[2] < lowestpoint[2])
		{
			VectorCopy(midpoint, lowestpoint);
			lowestedgenum = edgenum;
		}
	}
	if (lowestpoint[2] >= 99999.0f || ladderface1->planenum < 0 ||
		ladderface1->planenum >= aasworld.numPlanes)
	{
		return qfalse;
	}
	plane1 = &aasworld.planes[ladderface1->planenum];
	vec3_t start;
	vec3_t end;
	VectorMA(lowestpoint, 5.0f, plane1->normal, start);
	VectorCopy(start, end);
	start[2] += 5.0f;
	end[2] -= 100.0f;
	aas_trace_t trace = AAS_TraceClientBBox(start, end, PRESENCE_NORMAL, -1);
	trace.endpos[2] += 1.0f;
	int floorarea = AAS_PointAreaNum(trace.endpos);
	if (floorarea <= 0 || floorarea >= aasworld.numAreas ||
		floorarea >= aasworld.numAreaSettings)
	{
		return qfalse;
	}
	const aas_area_t *floor = &aasworld.areas[floorarea];
	if (floor->firstface < 0 || floor->numfaces < 0 ||
		floor->firstface + floor->numfaces > aasworld.faceIndexSize)
	{
		return qfalse;
	}
	qboolean floorhasladder = qfalse;
	for (int faceindex = 0; faceindex < floor->numfaces; ++faceindex)
	{
		int facenum = abs(aasworld.faceIndex[floor->firstface + faceindex]);
		if (facenum < 0 || facenum >= aasworld.numFaces)
		{
			continue;
		}
		const aas_face_t *face = &aasworld.faces[facenum];
		if ((face->faceflags & AAS_FACE_LADDER) == 0 ||
			face->planenum < 0 || face->planenum >= aasworld.numPlanes)
		{
			continue;
		}
		/* Retail truncates before the magnitude here too (0x1001574c). */
		if (AAS_ReachAbsTrunc(aasworld.planes[face->planenum].normal[2]) < 0.1f)
		{
			floorhasladder = qtrue;
			break;
		}
	}
	if (floorhasladder || floorarea == area1num ||
		AAS_ReachabilityExists(area1num, floorarea) ||
		AAS_ReachabilityExists(floorarea, area1num) ||
		start[2] - trace.endpos[2] >= maxjumpheight)
	{
		return qfalse;
	}

	aas_lreachability_t *down = AAS_LinkAdjacentReachability(
		area1num,
		floorarea,
		lowestedgenum,
		lowestpoint,
		trace.endpos,
		TRAVEL_LADDER,
		10);
	if (down == NULL)
	{
		return qfalse;
	}
	down->facenum = ladderface1num;
	reach_ladder++;

	VectorMA(lowestpoint, -5.0f, plane1->normal, end);
	end[2] += 10.0f;
	aas_lreachability_t *upreach = AAS_LinkAdjacentReachability(
		floorarea,
		area1num,
		lowestedgenum,
		trace.endpos,
		end,
		TRAVEL_JUMP,
		10);
	if (upreach == NULL)
	{
		return qfalse;
	}
	upreach->facenum = ladderface1num;
	reach_jump++;
	return qtrue;
}

/*
=============
AAS_Reachability_TeleportEntityList

Create the retail Quake II misc_teleporter reachabilities from a parsed BSP
entity list and return the number of links created.
=============
*/
int AAS_Reachability_TeleportEntityList(const aas_bspentity_t *entities)
{
	if (entities == NULL || areareachability == NULL || aasworld.areas == NULL ||
		aasworld.areasettings == NULL || aasworld.numAreas <= 1)
	{
		return 0;
	}

	int created = 0;
	for (const aas_bspentity_t *teleporter = entities;
		teleporter != NULL;
		teleporter = teleporter->next)
	{
		const char *classname = AAS_ValueForBSPEpairKey(teleporter, "classname");
		if (classname == NULL || strcmp(classname, "misc_teleporter") != 0)
		{
			continue;
		}

		vec3_t origin;
		if (!AAS_VectorForBSPEpairKey(teleporter, "origin", origin))
		{
			BotLib_Print(PRT_ERROR, "teleporter (%s) without origin\n", classname);
			continue;
		}
		const char *target = AAS_ValueForBSPEpairKey(teleporter, "target");
		if (target == NULL)
		{
			BotLib_Print(PRT_ERROR,
				"teleporter at %1.0f %1.0f %1.0f without target\n",
				origin[0], origin[1], origin[2]);
			continue;
		}

		const aas_bspentity_t *destination = NULL;
		for (const aas_bspentity_t *candidate = entities;
			candidate != NULL;
			candidate = candidate->next)
		{
			const char *destinationclass = AAS_ValueForBSPEpairKey(candidate,
				"classname");
			const char *targetname = AAS_ValueForBSPEpairKey(candidate, "targetname");
			if (destinationclass != NULL && targetname != NULL &&
				strcmp(destinationclass, "misc_teleporter_dest") == 0 &&
				strcmp(targetname, target) == 0)
			{
				destination = candidate;
				break;
			}
		}
		if (destination == NULL)
		{
			BotLib_Print(PRT_ERROR, "teleporter without destination (%s)\n", target);
			continue;
		}

		vec3_t destinationorigin;
		if (!AAS_VectorForBSPEpairKey(destination, "origin", destinationorigin))
		{
			BotLib_Print(PRT_ERROR,
				"teleporter destination (%s) without origin\n", target);
			continue;
		}
		destinationorigin[2] += 24.0f;
		vec3_t end;
		VectorCopy(destinationorigin, end);
		end[2] -= 100.0f;
		aas_trace_t trace = AAS_TraceClientBBox(destinationorigin,
			end,
			PRESENCE_CROUCH,
			-1);
		if (trace.startsolid)
		{
			BotLib_Print(PRT_ERROR,
				"teleporter destination (%s) in solid\n", target);
			continue;
		}
		VectorCopy(trace.endpos, destinationorigin);
		int destinationarea = AAS_PointAreaNum(destinationorigin);
		if (destinationarea <= 0 || destinationarea >= aasworld.numAreas ||
			destinationarea >= aasworld.numAreaSettings)
		{
			continue;
		}

		vec3_t triggermins = {-8.0f, -8.0f, 8.0f};
		vec3_t triggermaxs = {8.0f, 8.0f, 24.0f};
		vec3_t boxmins;
		vec3_t boxmaxs;
		AAS_PresenceTypeBoundingBox(PRESENCE_CROUCH, boxmins, boxmaxs);
		vec3_t absmins;
		vec3_t absmaxs;
		for (int axis = 0; axis < 3; ++axis)
		{
			absmins[axis] = triggermins[axis] + origin[axis] - boxmaxs[axis];
			absmaxs[axis] = triggermaxs[axis] + origin[axis] - boxmins[axis];
		}

		int *areas = (int *)malloc((size_t)aasworld.numAreas * sizeof(*areas));
		if (areas == NULL)
		{
			break;
		}
		int numareas = AAS_BBoxAreas(absmins, absmaxs, areas, aasworld.numAreas);
		for (int index = 0; index < numareas; ++index)
		{
			int sourcearea = areas[index];
			/*
			 * Retail admits every area overlapping the synthesised trigger box
			 * whose areaflags say it is grounded: 0x10015f21 calls
			 * sub_10011670 (AAS_AreaGrounded) on each linked area. Quake II has
			 * no teleporter brush contents, so an AAS_AreaTeleporter test here
			 * would reject every area on every shipped map.
			 */
			if (sourcearea <= 0 || sourcearea >= reachabilityareacount ||
				!AAS_AreaGrounded(sourcearea))
			{
				continue;
			}
			aas_lreachability_t *reachability = AAS_LinkAdjacentReachability(
				sourcearea,
				destinationarea,
				0,
				origin,
				destinationorigin,
				TRAVEL_TELEPORT,
				50);
			if (reachability == NULL)
			{
				break;
			}
			reach_teleport++;
			created++;
		}
		free(areas);
	}
	return created;
}

/*
=============
AAS_Reachability_Teleport

Load the current map's BSP epairs, generate retail teleporter links, and free
the temporary entity list.
=============
*/
void AAS_Reachability_Teleport(void)
{
	aas_bspentity_t *entities = AAS_LoadBSPEntities();
	AAS_Reachability_TeleportEntityList(entities);
	AAS_FreeBSPEntities(entities);
}

/*
=============
AAS_Reachability_ElevatorEntityList

Create the retail Quake II func_plat links from a parsed BSP entity list and
return the number of elevator reachabilities created.
=============
*/
int AAS_Reachability_ElevatorEntityList(const aas_bspentity_t *entities)
{
	if (entities == NULL || areareachability == NULL || aasworld.areas == NULL ||
		aasworld.areasettings == NULL || aasworld.bspModels == NULL)
	{
		return 0;
	}

	int created = 0;
	for (const aas_bspentity_t *entity = entities;
		entity != NULL;
		entity = entity->next)
	{
		const char *classname = AAS_ValueForBSPEpairKey(entity, "classname");
		if (classname == NULL || strcmp(classname, "func_plat") != 0)
		{
			continue;
		}
		const char *model = AAS_ValueForBSPEpairKey(entity, "model");
		if (model == NULL)
		{
			BotLib_Print(PRT_ERROR, "func_plat without model\n");
			continue;
		}
		int modelnum = (int)strtol(model + (model[0] == '*'), NULL, 10);
		if (modelnum <= 0 || modelnum >= aasworld.numBspModels)
		{
			BotLib_Print(PRT_ERROR, "func_plat with invalid model number\n");
			continue;
		}

		vec3_t angles = {0.0f, 0.0f, 0.0f};
		vec3_t mins;
		vec3_t maxs;
		vec3_t origin;
		AAS_BSPModelMinsMaxsOrigin(modelnum, angles, mins, maxs, origin);
		float lip = AAS_FloatForBSPEpairKey(entity, "lip");
		if (lip == 0.0f)
		{
			lip = 8.0f;
		}
		float height = AAS_FloatForBSPEpairKey(entity, "height");
		if (height == 0.0f)
		{
			height = maxs[2] - mins[2] - lip;
		}
		float speed = AAS_FloatForBSPEpairKey(entity, "speed");
		if (speed == 0.0f)
		{
			speed = 200.0f;
		}

		vec3_t pos1;
		vec3_t pos2;
		VectorCopy(origin, pos1);
		VectorCopy(origin, pos2);
		pos2[2] -= height;
		vec3_t mids;
		VectorAdd(mins, maxs, mids);
		vec3_t platbottom;
		VectorMA(pos2, 0.5f, mids, platbottom);
		platbottom[2] = maxs[2] - (pos1[2] - pos2[2]) + 2.0f;
		VectorAdd(mins, maxs, mids);
		vec3_t plattop;
		VectorMA(pos2, 0.5f, mids, plattop);
		plattop[2] = maxs[2] + 2.0f;
		for (int axis = 0; axis < 3; ++axis)
		{
			mins[axis] -= 1.0f;
			maxs[axis] += 1.0f;
		}
		VectorAdd(mins, maxs, mids);
		VectorScale(mids, 0.5f, mids);

		float xvals[8] = {
			mins[0], mids[0], maxs[0], mids[0],
			mins[0], maxs[0], maxs[0], mins[0]
		};
		float yvals[8] = {
			mids[1], maxs[1], mids[1], mins[1],
			maxs[1], maxs[1], mins[1], mins[1]
		};

		for (int sourceindex = 0; sourceindex < 9; ++sourceindex)
		{
			vec3_t bottomorigin;
			int sourcearea;
			if (sourceindex < 8)
			{
				bottomorigin[0] = origin[0] + xvals[sourceindex];
				bottomorigin[1] = origin[1] + yvals[sourceindex];
				bottomorigin[2] = platbottom[2] + 16.0f;
				for (int offset = 0;; ++offset)
				{
					sourcearea = AAS_PointAreaNum(bottomorigin);
					if (sourcearea > 0 &&
						(AAS_AreaGrounded(sourcearea) || AAS_AreaSwim(sourcearea)))
					{
						break;
					}
					if (offset >= 15)
					{
						sourcearea = 0;
						break;
					}
					bottomorigin[2] += 4.0f;
				}
				if (sourcearea == 0)
				{
					continue;
				}
			}
			else
			{
				VectorCopy(plattop, bottomorigin);
				bottomorigin[2] += 24.0f;
				sourcearea = AAS_PointAreaNum(bottomorigin);
				if (sourcearea <= 0)
				{
					continue;
				}
				VectorCopy(platbottom, bottomorigin);
				bottomorigin[2] += 24.0f;
			}

			for (int expansion = 0; expansion < 3; ++expansion)
			{
				for (int axis = 0; axis < 3; ++axis)
				{
					mins[axis] -= 4.0f;
					maxs[axis] += 4.0f;
				}
				float topx[8] = {
					mins[0], mids[0], maxs[0], mids[0],
					mins[0], maxs[0], maxs[0], mins[0]
				};
				float topy[8] = {
					mids[1], maxs[1], mids[1], mins[1],
					maxs[1], maxs[1], mins[1], mins[1]
				};

				for (int destinationindex = 0;
					destinationindex < 8;
					++destinationindex)
				{
					vec3_t toporigin;
					toporigin[0] = origin[0] + topx[destinationindex];
					toporigin[1] = origin[1] + topy[destinationindex];
					toporigin[2] = plattop[2] + 16.0f;
					int destinationarea = 0;
					int offset;
					for (offset = 0; offset < 16; ++offset)
					{
						destinationarea = AAS_PointAreaNum(toporigin);
						if (destinationarea > 0 &&
							(AAS_AreaGrounded(destinationarea) ||
								AAS_AreaSwim(destinationarea)))
						{
							vec3_t tracestart;
							vec3_t traceend;
							VectorCopy(plattop, tracestart);
							tracestart[2] += 32.0f;
							VectorCopy(toporigin, traceend);
							traceend[2] += 1.0f;
							aas_trace_t trace = AAS_TraceClientBBox(tracestart,
								traceend,
								PRESENCE_CROUCH,
								-1);
							if (trace.fraction >= 1.0f)
							{
								break;
							}
						}
						toporigin[2] += 4.0f;
					}
					if (offset >= 16 || destinationarea == sourcearea ||
						!AAS_AreaGrounded(destinationarea) ||
						AAS_ReachabilityExists(sourcearea, destinationarea))
					{
						continue;
					}

					vec3_t direction;
					VectorSubtract(bottomorigin, platbottom, direction);
					AAS_VectorNormalize(direction);
					vec3_t reachstart;
					reachstart[0] = bottomorigin[0] + 24.0f * direction[0];
					reachstart[1] = bottomorigin[1] + 24.0f * direction[1];
					reachstart[2] = bottomorigin[2];
					int axis;
					for (axis = 0; axis < 3; ++axis)
					{
						if (reachstart[axis] < origin[axis] + mins[axis] ||
							reachstart[axis] > origin[axis] + maxs[axis])
						{
							break;
						}
					}
					if (axis >= 3)
					{
						continue;
					}

					int traveltime = (int)(height * 100.0f / speed);
					if (traveltime == 0)
					{
						traveltime = 50;
					}
					aas_lreachability_t *reachability = AAS_LinkAdjacentReachability(
						sourcearea,
						destinationarea,
						(int)height,
						reachstart,
						toporigin,
						TRAVEL_ELEVATOR,
						(unsigned short)traveltime);
					if (reachability == NULL)
					{
						continue;
					}
					reachability->facenum = modelnum;
					reach_elevator++;
					created++;
					expansion = 9999;
				}
			}
		}
	}
	return created;
}

/*
=============
AAS_Reachability_Elevator

Load the current map's BSP epairs, generate retail func_plat links, and free
the temporary entity list.
=============
*/
void AAS_Reachability_Elevator(void)
{
	aas_bspentity_t *entities = AAS_LoadBSPEntities();
	AAS_Reachability_ElevatorEntityList(entities);
	AAS_FreeBSPEntities(entities);
}

/*
=============
AAS_Reachability_Grapple

Create retail grapple-hook links from a grounded source area towards usable
solid faces in a higher destination area.
=============
*/
int AAS_Reachability_Grapple(int area1num, int area2num)
{
	if (areareachability == NULL || aasworld.areas == NULL ||
		aasworld.areasettings == NULL || aasworld.faceIndex == NULL ||
		aasworld.faces == NULL || aasworld.edgeIndex == NULL ||
		aasworld.edges == NULL || aasworld.vertexes == NULL ||
		aasworld.planes == NULL || area1num <= 0 || area2num <= 0 ||
		area1num >= reachabilityareacount ||
		area2num >= aasworld.numAreaSettings ||
		area1num >= aasworld.numAreas || area2num >= aasworld.numAreas)
	{
		return qfalse;
	}
	if ((!AAS_AreaGrounded(area1num) && !AAS_AreaSwim(area1num)) ||
		(AAS_AreaPresenceType(area1num) & PRESENCE_NORMAL) == 0 ||
		AAS_AreaSwim(area1num))
	{
		return qfalse;
	}

	const aas_area_t *area1 = &aasworld.areas[area1num];
	const aas_area_t *area2 = &aasworld.areas[area2num];
	if (area2->maxs[2] < area1->mins[2] || area2->firstface < 0 ||
		area2->numfaces < 0 ||
		area2->firstface + area2->numfaces > aasworld.faceIndexSize)
	{
		return qfalse;
	}

	vec3_t areastart;
	vec3_t start;
	vec3_t end;
	VectorCopy(area1->center, start);
	if (AAS_PointAreaNum(start) == 0)
	{
		BotLib_LogWrite("area %d center %f %f %f in solid?\r\n",
			area1num, start[0], start[1], start[2]);
	}
	VectorCopy(start, end);
	end[2] -= 1000.0f;
	aas_trace_t trace = AAS_TraceClientBBox(start, end, PRESENCE_CROUCH, -1);
	if (trace.startsolid)
	{
		return qfalse;
	}
	VectorCopy(trace.endpos, areastart);

	for (int faceindex = 0; faceindex < area2->numfaces; ++faceindex)
	{
		int face2num = aasworld.faceIndex[area2->firstface + faceindex];
		int absface2num = abs(face2num);
		if (absface2num <= 0 || absface2num >= aasworld.numFaces)
		{
			continue;
		}
		const aas_face_t *face2 = &aasworld.faces[absface2num];
		if ((face2->faceflags & AAS_FACE_SOLID) == 0 ||
			face2->planenum < 0 || face2->planenum >= aasworld.numPlanes ||
			face2->firstedge < 0 || face2->numedges <= 0 ||
			face2->firstedge + face2->numedges > aasworld.edgeIndexSize)
		{
			continue;
		}

		int edgenum = abs(aasworld.edgeIndex[face2->firstedge]);
		if (edgenum <= 0 || edgenum >= aasworld.numEdges)
		{
			continue;
		}
		const aas_edge_t *edge = &aasworld.edges[edgenum];
		if (edge->v[0] < 0 || edge->v[0] >= aasworld.numVertexes)
		{
			continue;
		}

		const aas_plane_t *faceplane = &aasworld.planes[face2->planenum];
		vec3_t direction;
		VectorSubtract(aasworld.vertexes[edge->v[0]], areastart, direction);
		if (DotProduct(faceplane->normal, direction) > 0.0f)
		{
			continue;
		}

		vec3_t facecenter;
		AAS_FaceCenter(absface2num, facecenter);
		if (facecenter[2] < areastart[2] + 64.0f ||
			-faceplane->normal[2] < 0.0f)
		{
			continue;
		}
		VectorSubtract(facecenter, areastart, direction);
		float verticaldistance = direction[2];
		direction[2] = 0.0f;
		float horizontaldistance = AAS_VectorLength(direction);
		if (horizontaldistance == 0.0f || horizontaldistance > 2000.0f ||
			verticaldistance / horizontaldistance < tanf(0.2617993877991494f))
		{
			continue;
		}

		VectorCopy(facecenter, start);
		VectorMA(facecenter, -500.0f, faceplane->normal, end);
		bsp_trace_t bsptrace = AAS_Trace(start, NULL, NULL, end, 0, MASK_SHOT);
		if ((bsptrace.surface.flags & AAS_Q2_SURF_SKY) != 0 ||
			bsptrace.fraction * 500.0f > 32.0f)
		{
			continue;
		}

		VectorSubtract(facecenter, areastart, direction);
		AAS_VectorNormalize(direction);
		VectorMA(areastart, 4.0f, direction, start);
		VectorCopy(bsptrace.endpos, end);
		trace = AAS_TraceClientBBox(start, end, PRESENCE_NORMAL, -1);
		VectorSubtract(trace.endpos, facecenter, direction);
		if (AAS_VectorLength(direction) > 24.0f)
		{
			continue;
		}

		VectorCopy(trace.endpos, start);
		VectorCopy(trace.endpos, end);
		end[2] -= (float)AAS_FallDamageDistance();
		trace = AAS_TraceClientBBox(start, end, PRESENCE_NORMAL, -1);
		if (trace.fraction >= 1.0f)
		{
			continue;
		}
		int destinationarea = AAS_PointAreaNum(trace.endpos);
		if (destinationarea <= 0 || destinationarea >= aasworld.numAreas ||
			destinationarea >= aasworld.numAreaSettings ||
			(aasworld.areasettings[destinationarea].contents &
				(AAS_AREACONTENTS_SLIME | AAS_AREACONTENTS_LAVA)) != 0 ||
			destinationarea == area1num ||
			AAS_ReachabilityExists(area1num, destinationarea) ||
			!AAS_AreaGrounded(destinationarea))
		{
			continue;
		}

		int tracedareas[20];
		int numareas = AAS_TraceAreas(areastart,
			bsptrace.endpos,
			tracedareas,
			NULL,
			20);
		if (numareas >= 20)
		{
			continue;
		}
		int areaindex;
		for (areaindex = 0; areaindex < numareas; ++areaindex)
		{
			if (AAS_AreaClusterPortal(tracedareas[areaindex]))
			{
				break;
			}
		}
		if (areaindex < numareas)
		{
			continue;
		}

		VectorSubtract(bsptrace.endpos, areastart, direction);
		aas_lreachability_t *reachability = AAS_LinkAdjacentReachability(
			area1num,
			destinationarea,
			0,
			areastart,
			bsptrace.endpos,
			TRAVEL_GRAPPLEHOOK,
			(unsigned short)(500.0f + AAS_VectorLength(direction) * 0.25f));
		if (reachability == NULL)
		{
			return qfalse;
		}
		reachability->facenum = face2num;
		reach_grapple++;
	}

	return qfalse;
}

/*
=============
AAS_SetWeaponJumpAreaFlagsEntityList

Mark areas containing the retail Quake II high-value item and weapon classes
as weapon-jump destinations.
=============
*/
int AAS_SetWeaponJumpAreaFlagsEntityList(const aas_bspentity_t *entities)
{
	static const char *const weaponjumpclasses[] = {
		"item_armor_body",
		"item_armor_combat",
		"item_power_screen",
		"item_power_shield",
		"weapon_grenadelauncher",
		"weapon_rocketlauncher",
		"weapon_hyperblaster",
		"weapon_railgun",
		"weapon_bfg",
		"weapon_boomer",
		"weapon_phalanx",
		"item_quadfire",
		"weapon_etf_rifle",
		"weapon_proxlauncher",
		"weapon_plasmabeam",
		"weapon_chainfist",
		"weapon_disintegrator",
		"item_ir_goggles",
		"item_double",
		"item_compass",
		"item_sphere_vengeance",
		"item_sphere_hunter",
		"item_sphere_defender",
		"item_doppleganger",
		"dm_tag_token",
		"dm_tag_token",
		"item_health_mega",
		"item_quad",
		"item_invulnerability"
	};
	const vec3_t mins = {-15.0f, -15.0f, -15.0f};
	const vec3_t maxs = {15.0f, 15.0f, 15.0f};

	if (entities == NULL || aasworld.areasettings == NULL ||
		aasworld.numAreaSettings <= 1)
	{
		return 0;
	}

	int marked = 0;
	for (const aas_bspentity_t *entity = entities;
		entity != NULL;
		entity = entity->next)
	{
		const char *classname = AAS_ValueForBSPEpairKey(entity, "classname");
		if (classname == NULL)
		{
			continue;
		}
		qboolean match = qfalse;
		for (size_t index = 0;
			index < sizeof(weaponjumpclasses) / sizeof(weaponjumpclasses[0]);
			++index)
		{
			if (strcmp(classname, weaponjumpclasses[index]) == 0)
			{
				match = qtrue;
				break;
			}
		}
		if (!match)
		{
			continue;
		}

		vec3_t origin;
		if (!AAS_VectorForBSPEpairKey(entity, "origin", origin))
		{
			continue;
		}
		if (!AAS_DropToFloor(origin, mins, maxs))
		{
			BotLib_Print(PRT_MESSAGE,
				"%s in solid at (%1.1f %1.1f %1.1f)\n",
				classname,
				origin[0],
				origin[1],
				origin[2]);
		}
		int areanum = AAS_BestReachableArea(origin, mins, maxs, origin);
		if (areanum <= 0 || areanum >= aasworld.numAreaSettings)
		{
			continue;
		}
		aasworld.areasettings[areanum].areaflags |= AAS_AREA_WEAPONJUMP;
		marked++;
	}
	return marked;
}

/*
=============
AAS_SetWeaponJumpAreaFlags

Load the current BSP entity epairs, mark retail weapon-jump item areas, and
release the temporary entity list.
=============
*/
void AAS_SetWeaponJumpAreaFlags(void)
{
	aas_bspentity_t *entities = AAS_LoadBSPEntities();
	AAS_SetWeaponJumpAreaFlagsEntityList(entities);
	AAS_FreeBSPEntities(entities);
}

/*
=============
AAS_Reachability_WeaponJump

Create the retail rocket-jump link to a higher ground face in an item-marked
destination area.
=============
*/
int AAS_Reachability_WeaponJump(int area1num, int area2num)
{
	if (areareachability == NULL || aasworld.areas == NULL ||
		aasworld.areasettings == NULL || aasworld.faceIndex == NULL ||
		aasworld.faces == NULL || aasworld.edgeIndex == NULL ||
		aasworld.edges == NULL || aasworld.vertexes == NULL ||
		area1num <= 0 || area2num <= 0 ||
		area1num >= reachabilityareacount || area1num >= aasworld.numAreas ||
		area2num >= aasworld.numAreas || area1num >= aasworld.numAreaSettings ||
		area2num >= aasworld.numAreaSettings)
	{
		return qfalse;
	}
	if (!AAS_AreaGrounded(area1num) || AAS_AreaSwim(area1num) ||
		!AAS_AreaGrounded(area2num) ||
		(aasworld.areasettings[area2num].areaflags &
			AAS_AREA_WEAPONJUMP) == 0)
	{
		return qfalse;
	}

	const aas_area_t *area1 = &aasworld.areas[area1num];
	const aas_area_t *area2 = &aasworld.areas[area2num];
	if (area2->maxs[2] < area1->mins[2] || area2->firstface < 0 ||
		area2->numfaces < 0 ||
		area2->firstface + area2->numfaces > aasworld.faceIndexSize)
	{
		return qfalse;
	}

	vec3_t start;
	VectorCopy(area1->center, start);
	if (AAS_PointAreaNum(start) == 0)
	{
		BotLib_LogWrite("area %d center %f %f %f in solid?\r\n",
			area1num, start[0], start[1], start[2]);
	}
	vec3_t end;
	VectorCopy(start, end);
	end[2] -= 1000.0f;
	aas_trace_t trace = AAS_TraceClientBBox(start, end, PRESENCE_CROUCH, -1);
	if (trace.startsolid)
	{
		return qfalse;
	}
	vec3_t areastart;
	VectorCopy(trace.endpos, areastart);

	for (int faceindex = 0; faceindex < area2->numfaces; ++faceindex)
	{
		int face2num = aasworld.faceIndex[area2->firstface + faceindex];
		int absface2num = abs(face2num);
		if (absface2num <= 0 || absface2num >= aasworld.numFaces)
		{
			continue;
		}
		const aas_face_t *face2 = &aasworld.faces[absface2num];
		if ((face2->faceflags & AAS_FACE_GROUND) == 0)
		{
			continue;
		}

		vec3_t facecenter;
		AAS_FaceCenter(absface2num, facecenter);
		if (facecenter[2] < areastart[2] + 64.0f)
		{
			continue;
		}
		for (int weapon = 0; weapon < 1; ++weapon)
		{
			float zvelocity = weapon != 0 ?
				AAS_BFGJumpZVelocity(areastart) :
				AAS_RocketJumpZVelocity(areastart);
			float speed;
			if (!AAS_HorizontalVelocityForJump(zvelocity,
				areastart,
				facecenter,
				&speed) || speed >= 270.0f)
			{
				continue;
			}

			vec3_t direction;
			VectorSubtract(facecenter, areastart, direction);
			direction[2] = 0.0f;
			float horizontaldistance = AAS_VectorNormalize(direction);
			if (horizontaldistance >=
				1.6f * facecenter[2] - areastart[2])
			{
				continue;
			}

			vec3_t cmdmove;
			VectorScale(direction, speed, cmdmove);
			vec3_t velocity = {0.0f, 0.0f, 0.0f};
			velocity[2] = zvelocity;
			aas_clientmove_t move;
			AAS_PredictClientMovement(&move,
				-1,
				areastart,
				PRESENCE_NORMAL,
				qtrue,
				velocity,
				cmdmove,
				3,
				30,
				0.1f,
				SE_HITGROUND | SE_ENTERWATER | SE_ENTERSLIME |
					SE_ENTERLAVA | SE_HITGROUNDDAMAGE,
				0,
				qfalse);
			if (move.frames >= 30 ||
				(move.stopevent & (SE_ENTERSLIME | SE_ENTERLAVA |
					SE_HITGROUNDDAMAGE)) != 0)
			{
				continue;
			}

			int offset;
			for (offset = 0; offset >= -32; offset -= 8)
			{
				vec3_t sample;
				VectorMA(move.endpos, (float)offset, direction, sample);
				sample[2] += 0.125f;
				if (AAS_PointAreaNum(sample) == area2num)
				{
					break;
				}
			}
			if (offset < -32)
			{
				continue;
			}

			aas_lreachability_t *reachability = AAS_LinkAdjacentReachability(
				area1num,
				area2num,
				0,
				areastart,
				facecenter,
				weapon != 0 ? TRAVEL_BFGJUMP : TRAVEL_ROCKETJUMP,
				500);
			if (reachability == NULL)
			{
				return qfalse;
			}
			reach_rocketjump++;
			return qtrue;
		}
	}
	return qfalse;
}

/*
=============
AAS_Reachability_WalkOffLedge

Scan a grounded area's exposed ground edges and add the retail secondary
walk-off-ledge links to safe grounded or swimming landing areas below.
=============
*/
void AAS_Reachability_WalkOffLedge(int areanum)
{
	if (areareachability == NULL || aasworld.areas == NULL ||
		aasworld.areasettings == NULL || aasworld.faceIndex == NULL ||
		aasworld.faces == NULL || aasworld.edgeIndex == NULL ||
		aasworld.edges == NULL || aasworld.vertexes == NULL ||
		aasworld.planes == NULL || areanum <= 0 ||
		areanum >= reachabilityareacount || areanum >= aasworld.numAreas ||
		areanum >= aasworld.numAreaSettings || !AAS_AreaGrounded(areanum) ||
		AAS_AreaSwim(areanum))
	{
		return;
	}

	const aas_area_t *area = &aasworld.areas[areanum];
	if (area->firstface < 0 || area->numfaces < 0 ||
		area->firstface + area->numfaces > aasworld.faceIndexSize)
	{
		return;
	}

	for (int face1index = 0; face1index < area->numfaces; ++face1index)
	{
		int face1num = aasworld.faceIndex[area->firstface + face1index];
		int absface1num = abs(face1num);
		if (absface1num <= 0 || absface1num >= aasworld.numFaces)
		{
			continue;
		}
		const aas_face_t *face1 = &aasworld.faces[absface1num];
		if ((face1->faceflags & AAS_FACE_GROUND) == 0 ||
			face1->firstedge < 0 || face1->numedges < 0 ||
			face1->firstedge + face1->numedges > aasworld.edgeIndexSize ||
			face1->planenum < 0 || face1->planenum >= aasworld.numPlanes)
		{
			continue;
		}

		for (int edge1index = 0; edge1index < face1->numedges; ++edge1index)
		{
			int edge1num = aasworld.edgeIndex[face1->firstedge + edge1index];
			int absedge1num = abs(edge1num);
			if (absedge1num <= 0 || absedge1num >= aasworld.numEdges)
			{
				continue;
			}

			for (int face2index = 0; face2index < area->numfaces; ++face2index)
			{
				int face2num = aasworld.faceIndex[
					area->firstface + face2index];
				int absface2num = abs(face2num);
				if (absface2num <= 0 || absface2num >= aasworld.numFaces)
				{
					continue;
				}
				const aas_face_t *face2 = &aasworld.faces[absface2num];
				if ((face2->faceflags & AAS_FACE_GROUND) != 0 ||
					face2->firstedge < 0 || face2->numedges < 0 ||
					face2->firstedge + face2->numedges >
						aasworld.edgeIndexSize)
				{
					continue;
				}

				for (int edge2index = 0;
					edge2index < face2->numedges;
					++edge2index)
				{
					int edge2num = aasworld.edgeIndex[
						face2->firstedge + edge2index];
					if (abs(edge2num) != absedge1num)
					{
						continue;
					}

					int otherareanum = face2->frontarea == areanum ?
						face2->backarea : face2->frontarea;
					if (otherareanum > 0 && otherareanum < aasworld.numAreas &&
						otherareanum < aasworld.numAreaSettings &&
						AAS_AreaGrounded(otherareanum))
					{
						const aas_area_t *otherarea =
							&aasworld.areas[otherareanum];
						qboolean gap = qfalse;
						qboolean sharedfacefound = qfalse;
						if (otherarea->firstface >= 0 &&
							otherarea->numfaces >= 0 &&
							otherarea->firstface + otherarea->numfaces <=
								aasworld.faceIndexSize)
						{
							for (int face3index = 0;
								face3index < otherarea->numfaces;
								++face3index)
							{
								int face3num = aasworld.faceIndex[
									otherarea->firstface + face3index];
								int absface3num = abs(face3num);
								if (absface3num == absface2num ||
									absface3num <= 0 ||
									absface3num >= aasworld.numFaces)
								{
									continue;
								}
								const aas_face_t *face3 =
									&aasworld.faces[absface3num];
								if (face3->firstedge < 0 ||
									face3->numedges < 0 ||
									face3->firstedge + face3->numedges >
										aasworld.edgeIndexSize)
								{
									continue;
								}
								for (int edge3index = 0;
									edge3index < face3->numedges;
									++edge3index)
								{
									int edge3num = aasworld.edgeIndex[
										face3->firstedge + edge3index];
									if (abs(edge3num) != absedge1num)
									{
										continue;
									}
									sharedfacefound = qtrue;
									if ((face3->faceflags & AAS_FACE_SOLID) == 0)
									{
										gap = qtrue;
									}
									else if ((face3->faceflags &
										AAS_FACE_GROUND) != 0)
									{
										gap = qfalse;
									}
									else
									{
										gap = qtrue;
									}
									break;
								}
								if (sharedfacefound)
								{
									break;
								}
							}
						}
						if (!gap)
						{
							break;
						}
					}

					const aas_edge_t *edge = &aasworld.edges[absedge1num];
					int side = edge1num < 0;
					if (edge->v[side] < 0 ||
						edge->v[side] >= aasworld.numVertexes ||
						edge->v[!side] < 0 ||
						edge->v[!side] >= aasworld.numVertexes)
					{
						break;
					}
					const vec3_t *vertex1 =
						&aasworld.vertexes[edge->v[side]];
					const vec3_t *vertex2 =
						&aasworld.vertexes[edge->v[!side]];
					vec3_t edgevector;
					VectorSubtract(*vertex2, *vertex1, edgevector);
					vec3_t direction;
					AAS_CrossProduct(aasworld.planes[face1->planenum].normal,
						edgevector,
						direction);
					if (AAS_VectorNormalize(direction) == 0.0f)
					{
						break;
					}
					vec3_t midpoint;
					VectorAdd(*vertex1, *vertex2, midpoint);
					VectorScale(midpoint, 0.5f, midpoint);
					VectorMA(midpoint, 8.0f, direction, midpoint);
					vec3_t testend;
					VectorCopy(midpoint, testend);
					testend[2] -= 1000.0f;
					aas_trace_t trace = AAS_TraceClientBBox(midpoint,
						testend,
						PRESENCE_CROUCH,
						-1);
					if (trace.startsolid)
					{
						break;
					}
					int reachareanum = AAS_PointAreaNum(trace.endpos);
					if (reachareanum <= 0 ||
						reachareanum >= aasworld.numAreas ||
						reachareanum >= aasworld.numAreaSettings ||
						reachareanum == areanum ||
						AAS_ReachabilityExists(areanum, reachareanum) ||
						(!AAS_AreaGrounded(reachareanum) &&
							!AAS_AreaSwim(reachareanum)) ||
						(aasworld.areasettings[reachareanum].contents &
							(AAS_AREACONTENTS_SLIME |
								AAS_AREACONTENTS_LAVA)) != 0)
					{
						break;
					}

					float fallheight = fabsf(midpoint[2] - trace.endpos[2]);
					unsigned short traveltime = 100;
					if (!AAS_AreaSwim(reachareanum) &&
						fallheight > (float)AAS_FallDamageDistance())
					{
						traveltime = 3000;
					}
					aas_lreachability_t *reachability =
						AAS_LinkAdjacentReachability(areanum,
							reachareanum,
							edge1num,
							midpoint,
							trace.endpos,
							TRAVEL_WALKOFFLEDGE,
							traveltime);
					if (reachability == NULL)
					{
						break;
					}
					reach_walkoffledge++;
				}
			}
		}
	}
}

/*
=============
AAS_ReachabilityMilliseconds

Return the retail process-clock millisecond value used to bound generation.
=============
*/
static int AAS_ReachabilityMilliseconds(void)
{
	return (int)(clock() * 1000 / CLOCKS_PER_SEC);
}

/*
=============
AAS_ContinueInitReachability

Advance the retail frame-budgeted reachability generator and run its final
walk-off, teleporter, elevator, storage, and temporary-heap cleanup passes.
=============
*/
int AAS_ContinueInitReachability(void)
{
	if (!aasworld.loaded || aasworld.numReachabilityAreas >= aasworld.numAreas)
	{
		return qfalse;
	}
	if (areareachability == NULL)
	{
		return qfalse;
	}

	float framebudget = 0.0f;
	libvar_t *framevar = Bridge_FrameReachability();
	if (framevar != NULL)
	{
		framebudget = framevar->value;
	}
	if (framebudget <= 0.0f)
	{
		framebudget = 15.0f;
	}
	/*
	 * Retail creates reachability_delay from its own "100" default string
	 * (0x100189ad passes data_1005bddc = "100" to LibVar) and only substitutes
	 * 200 (0x43480000 at 0x100189c8) when the parsed value is not positive.
	 * A non-creating lookup would always fall through to 200 because no
	 * Quake II cvar of that name exists.
	 */
	float delay = LibVarValue("reachability_delay", "100");
	if (delay <= 0.0f)
	{
		delay = 200.0f;
	}

	int todo = aasworld.numReachabilityAreas + (int)framebudget;
	int starttime = AAS_ReachabilityMilliseconds();
	int sourcearea = aasworld.numReachabilityAreas;
	while (sourcearea < aasworld.numAreas && sourcearea < todo)
	{
		aasworld.numReachabilityAreas++;
		for (int destinationarea = 1;
			destinationarea < aasworld.numAreas;
			++destinationarea)
		{
			if (sourcearea == destinationarea ||
				AAS_ReachabilityExists(sourcearea, destinationarea))
			{
				continue;
			}
			if (AAS_Reachability_Swim(sourcearea, destinationarea))
			{
				continue;
			}
			if (AAS_Reachability_EqualFloorHeight(sourcearea,
				destinationarea))
			{
				continue;
			}
			if (AAS_Reachability_Step_Barrier_WaterJump_WalkOffLedge(
				sourcearea,
				destinationarea))
			{
				continue;
			}
			if (AAS_Reachability_Ladder(sourcearea, destinationarea))
			{
				continue;
			}
			AAS_Reachability_Jump(sourcearea, destinationarea);
		}

		for (int destinationarea = 1;
			destinationarea < aasworld.numAreas;
			++destinationarea)
		{
			if (sourcearea == destinationarea ||
				AAS_ReachabilityExists(sourcearea, destinationarea))
			{
				continue;
			}
			AAS_Reachability_Grapple(sourcearea, destinationarea);
			AAS_Reachability_WeaponJump(sourcearea, destinationarea);
		}

		if (AAS_ReachabilityMilliseconds() - starttime > (int)delay)
		{
			break;
		}
		sourcearea++;
	}

	if (aasworld.numReachabilityAreas >= aasworld.numAreas)
	{
		for (int areanum = 1; areanum < aasworld.numAreas; ++areanum)
		{
			AAS_Reachability_WalkOffLedge(areanum);
		}
		AAS_Reachability_Teleport();
		AAS_Reachability_Elevator();
		AAS_StoreReachability();
		AAS_ShutDownReachabilityHeap();
		BotLib_Print(PRT_MESSAGE, "calculating clusters...\n");
		return qtrue;
	}

	if (aasworld.numReachabilityAreas - (int)framebudget <= 1)
	{
		BotLib_Print(PRT_MESSAGE, "calculating reachability...\n");
	}
	if (aasworld.numReachabilityAreas + (int)framebudget >= aasworld.numAreas)
	{
		BotLib_Print(PRT_MESSAGE, "\r%6d%%", 100);
		BotLib_Print(PRT_MESSAGE,
			"\nplease wait while storing reachability...\n");
	}
	else
	{
		BotLib_Print(PRT_MESSAGE,
			"\r%6d%%",
			aasworld.numReachabilityAreas * 100 / aasworld.numAreas);
	}
	return qtrue;
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

		/*
		 * Retail has no reachability pass-area expansion stage.  The successor's
		 * added table also leaves trajectory-based travel types empty.
		 */
		case TRAVEL_JUMP:
		case TRAVEL_ROCKETJUMP:
		case TRAVEL_BFGJUMP:
		case TRAVEL_JUMPPAD:
			return 0;

		/* The successor likewise records no sampled model path for movers. */
		case TRAVEL_ELEVATOR:
		case TRAVEL_FUNCBOB:
			return 0;

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
		(aas_reversedreachability_t *)calloc((size_t)numAreas,
			sizeof(aas_reversedreachability_t));
    if (aasworld.reversedReachability == NULL)
    {
        AAS_ClearReachabilityData();
        return BLERR_INVALIDIMPORT;
    }

	int *reverseCounts = (int *)calloc((size_t)numAreas, sizeof(int));
    if (reverseCounts == NULL)
    {
        AAS_ClearReachabilityData();
        return BLERR_INVALIDIMPORT;
    }

	for (int area = 1; area < numAreas && area < aasworld.numAreaSettings; ++area)
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
			if (destination < 0 || destination >= numAreas)
            {
                continue;
            }

            reverseCounts[destination] += 1;
        }
    }

	for (int area = 0; area < numAreas; ++area)
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

	int *reverseOffsets = (int *)calloc((size_t)numAreas, sizeof(int));
    if (reverseOffsets == NULL)
    {
        free(reverseCounts);
        AAS_ClearReachabilityData();
        return BLERR_INVALIDIMPORT;
    }

	for (int area = 1; area < numAreas && area < aasworld.numAreaSettings; ++area)
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
			if (destination < 0 || destination >= numAreas)
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
