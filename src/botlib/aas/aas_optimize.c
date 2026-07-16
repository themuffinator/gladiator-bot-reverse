#include "aas_local.h"

#include <stdlib.h>
#include <string.h>

#include "botlib/common/l_log.h"
#include "botlib/common/l_memory.h"

typedef struct aas_optimized_s
{
	int numvertexes;
	aas_vertex_t *vertexes;
	int numedges;
	aas_edge_t *edges;
	int edgeindexsize;
	int *edgeindex;
	int numfaces;
	aas_face_t *faces;
	int faceindexsize;
	int *faceindex;
	int numareas;
	aas_area_t *areas;
	int *vertexoptimizeindex;
	int *edgeoptimizeindex;
	int *faceoptimizeindex;
} aas_optimized_t;

/*
=============
AAS_KeepEdge

The retail optimizer retains every edge referenced by a retained face.
=============
*/
static int AAS_KeepEdge(const aas_edge_t *edge)
{
	(void)edge;
	return qtrue;
}

/*
=============
AAS_OptimizeEdge

Copy an edge and its vertices into the compact geometry arrays once.
=============
*/
static int AAS_OptimizeEdge(aas_optimized_t *optimized, int edgenum)
{
	int absoluteedge = abs(edgenum);
	if (absoluteedge <= 0 || absoluteedge >= aasworld.numEdges)
	{
		return 0;
	}

	aas_edge_t *edge = &aasworld.edges[absoluteedge];
	if (!AAS_KeepEdge(edge))
	{
		return 0;
	}

	int optimizededge = optimized->edgeoptimizeindex[absoluteedge];
	if (optimizededge != 0)
	{
		return edgenum > 0 ? optimizededge : -optimizededge;
	}

	if (optimized->numedges >= aasworld.numEdges)
	{
		return 0;
	}

	aas_edge_t *destination = &optimized->edges[optimized->numedges];
	for (int endpoint = 0; endpoint < 2; ++endpoint)
	{
		int vertexnum = edge->v[endpoint];
		if (vertexnum < 0 || vertexnum >= aasworld.numVertexes)
		{
			return 0;
		}

		if (optimized->vertexoptimizeindex[vertexnum] != 0)
		{
			destination->v[endpoint] =
				optimized->vertexoptimizeindex[vertexnum];
		}
		else
		{
			if (optimized->numvertexes >= aasworld.numVertexes)
			{
				return 0;
			}
			VectorCopy(aasworld.vertexes[vertexnum],
				optimized->vertexes[optimized->numvertexes]);
			destination->v[endpoint] = optimized->numvertexes;
			optimized->vertexoptimizeindex[vertexnum] =
				optimized->numvertexes;
			optimized->numvertexes += 1;
		}
	}

	optimized->edgeoptimizeindex[absoluteedge] = optimized->numedges;
	optimizededge = optimized->numedges;
	optimized->numedges += 1;
	return edgenum > 0 ? optimizededge : -optimizededge;
}

/*
=============
AAS_KeepFace

Retain only ladder faces after reachability generation, as in gladiator.dll.
=============
*/
static int AAS_KeepFace(const aas_face_t *face)
{
	return (face->faceflags & AAS_FACE_LADDER) != 0;
}

/*
=============
AAS_OptimizeFace

Copy a retained face and compact its signed edge-index list.
=============
*/
static int AAS_OptimizeFace(aas_optimized_t *optimized, int facenum)
{
	int absoluteface = abs(facenum);
	if (absoluteface <= 0 || absoluteface >= aasworld.numFaces)
	{
		return 0;
	}

	aas_face_t *face = &aasworld.faces[absoluteface];
	if (!AAS_KeepFace(face))
	{
		return 0;
	}

	int optimizedface = optimized->faceoptimizeindex[absoluteface];
	if (optimizedface != 0)
	{
		return facenum > 0 ? optimizedface : -optimizedface;
	}
	if (optimized->numfaces >= aasworld.numFaces)
	{
		return 0;
	}

	aas_face_t *destination = &optimized->faces[optimized->numfaces];
	memcpy(destination, face, sizeof(*destination));
	destination->numedges = 0;
	destination->firstedge = optimized->edgeindexsize;
	for (int index = 0; index < face->numedges; ++index)
	{
		int sourceindex = face->firstedge + index;
		if (sourceindex < 0 || sourceindex >= aasworld.edgeIndexSize)
		{
			continue;
		}

		int optimizededge = AAS_OptimizeEdge(optimized,
			aasworld.edgeIndex[sourceindex]);
		if (optimizededge == 0 ||
			optimized->edgeindexsize >= aasworld.edgeIndexSize)
		{
			continue;
		}

		optimized->edgeindex[destination->firstedge + destination->numedges] =
			optimizededge;
		destination->numedges += 1;
		optimized->edgeindexsize += 1;
	}

	optimized->faceoptimizeindex[absoluteface] = optimized->numfaces;
	optimizedface = optimized->numfaces;
	optimized->numfaces += 1;
	return facenum > 0 ? optimizedface : -optimizedface;
}

/*
=============
AAS_OptimizeArea

Copy an area and compact its signed face-index list to retained faces.
=============
*/
static void AAS_OptimizeArea(aas_optimized_t *optimized, int areanum)
{
	aas_area_t *source = &aasworld.areas[areanum];
	aas_area_t *destination = &optimized->areas[areanum];
	memcpy(destination, source, sizeof(*destination));
	destination->numfaces = 0;
	destination->firstface = optimized->faceindexsize;

	for (int index = 0; index < source->numfaces; ++index)
	{
		int sourceindex = source->firstface + index;
		if (sourceindex < 0 || sourceindex >= aasworld.faceIndexSize)
		{
			continue;
		}

		int optimizedface = AAS_OptimizeFace(optimized,
			aasworld.faceIndex[sourceindex]);
		if (optimizedface == 0 ||
			optimized->faceindexsize >= aasworld.faceIndexSize)
		{
			continue;
		}

		optimized->faceindex[destination->firstface + destination->numfaces] =
			optimizedface;
		destination->numfaces += 1;
		optimized->faceindexsize += 1;
	}
}

/*
=============
AAS_FreeOptimized

Release any compact geometry buffers that have not been transferred.
=============
*/
static void AAS_FreeOptimized(aas_optimized_t *optimized)
{
	FreeMemory(optimized->vertexes);
	FreeMemory(optimized->edges);
	FreeMemory(optimized->edgeindex);
	FreeMemory(optimized->faces);
	FreeMemory(optimized->faceindex);
	FreeMemory(optimized->areas);
	FreeMemory(optimized->vertexoptimizeindex);
	FreeMemory(optimized->edgeoptimizeindex);
	FreeMemory(optimized->faceoptimizeindex);
	memset(optimized, 0, sizeof(*optimized));
}

/*
=============
AAS_OptimizeAlloc

Allocate retail-sized compact arrays and old-to-new index tables.
=============
*/
static int AAS_OptimizeAlloc(aas_optimized_t *optimized)
{
	memset(optimized, 0, sizeof(*optimized));
	optimized->vertexes = (aas_vertex_t *)GetClearedMemory(
		(size_t)aasworld.numVertexes * sizeof(aas_vertex_t));
	optimized->edges = (aas_edge_t *)GetClearedMemory(
		(size_t)aasworld.numEdges * sizeof(aas_edge_t));
	optimized->edgeindex = (int *)GetClearedMemory(
		(size_t)aasworld.edgeIndexSize * sizeof(int));
	optimized->faces = (aas_face_t *)GetClearedMemory(
		(size_t)aasworld.numFaces * sizeof(aas_face_t));
	optimized->faceindex = (int *)GetClearedMemory(
		(size_t)aasworld.faceIndexSize * sizeof(int));
	optimized->areas = (aas_area_t *)GetClearedMemory(
		(size_t)aasworld.numAreas * sizeof(aas_area_t));
	optimized->vertexoptimizeindex = (int *)GetClearedMemory(
		(size_t)aasworld.numVertexes * sizeof(int));
	optimized->edgeoptimizeindex = (int *)GetClearedMemory(
		(size_t)aasworld.numEdges * sizeof(int));
	optimized->faceoptimizeindex = (int *)GetClearedMemory(
		(size_t)aasworld.numFaces * sizeof(int));

	if ((aasworld.numVertexes > 0 &&
		optimized->vertexes == NULL) ||
		(aasworld.numEdges > 0 && optimized->edges == NULL) ||
		(aasworld.edgeIndexSize > 0 && optimized->edgeindex == NULL) ||
		(aasworld.numFaces > 0 && optimized->faces == NULL) ||
		(aasworld.faceIndexSize > 0 && optimized->faceindex == NULL) ||
		(aasworld.numAreas > 0 && optimized->areas == NULL) ||
		(aasworld.numVertexes > 0 &&
		optimized->vertexoptimizeindex == NULL) ||
		(aasworld.numEdges > 0 && optimized->edgeoptimizeindex == NULL) ||
		(aasworld.numFaces > 0 && optimized->faceoptimizeindex == NULL))
	{
		AAS_FreeOptimized(optimized);
		return qfalse;
	}

	optimized->numvertexes = 0;
	optimized->numedges = 1;
	optimized->edgeindexsize = 0;
	optimized->numfaces = 1;
	optimized->faceindexsize = 0;
	optimized->numareas = aasworld.numAreas;
	return qtrue;
}

/*
=============
AAS_OptimizeStore

Replace world geometry with the compact retail geometry and free index maps.
=============
*/
static void AAS_OptimizeStore(aas_optimized_t *optimized)
{
	FreeMemory(aasworld.vertexes);
	aasworld.vertexes = optimized->vertexes;
	aasworld.numVertexes = optimized->numvertexes;
	optimized->vertexes = NULL;

	FreeMemory(aasworld.edges);
	aasworld.edges = optimized->edges;
	aasworld.numEdges = optimized->numedges;
	optimized->edges = NULL;

	FreeMemory(aasworld.edgeIndex);
	aasworld.edgeIndex = optimized->edgeindex;
	aasworld.edgeIndexSize = optimized->edgeindexsize;
	optimized->edgeindex = NULL;

	FreeMemory(aasworld.faces);
	aasworld.faces = optimized->faces;
	aasworld.numFaces = optimized->numfaces;
	optimized->faces = NULL;

	FreeMemory(aasworld.faceIndex);
	aasworld.faceIndex = optimized->faceindex;
	aasworld.faceIndexSize = optimized->faceindexsize;
	optimized->faceindex = NULL;

	FreeMemory(aasworld.areas);
	aasworld.areas = optimized->areas;
	aasworld.numAreas = optimized->numareas;
	optimized->areas = NULL;

	FreeMemory(optimized->vertexoptimizeindex);
	FreeMemory(optimized->edgeoptimizeindex);
	FreeMemory(optimized->faceoptimizeindex);
	optimized->vertexoptimizeindex = NULL;
	optimized->edgeoptimizeindex = NULL;
	optimized->faceoptimizeindex = NULL;
}

/*
=============
AAS_Optimize

Discard non-ladder geometry and remap non-elevator reachability references.
=============
*/
void AAS_Optimize(void)
{
	if (!aasworld.loaded || aasworld.areas == NULL ||
		aasworld.faces == NULL || aasworld.edges == NULL)
	{
		return;
	}

	aas_optimized_t optimized;
	if (!AAS_OptimizeAlloc(&optimized))
	{
		BotLib_Print(PRT_ERROR, "AAS_Optimize: unable to allocate compact data\n");
		return;
	}

	for (int areanum = 1; areanum < aasworld.numAreas; ++areanum)
	{
		AAS_OptimizeArea(&optimized, areanum);
	}

	for (int reachnum = 0; reachnum < aasworld.numReachability; ++reachnum)
	{
		aas_reachability_t *reachability = &aasworld.reachability[reachnum];
		if (reachability->traveltype == TRAVEL_ELEVATOR)
		{
			continue;
		}

		int sign = reachability->facenum;
		int facenum = abs(sign);
		reachability->facenum = facenum < aasworld.numFaces
			? optimized.faceoptimizeindex[facenum]
			: 0;
		if (sign < 0)
		{
			reachability->facenum = -reachability->facenum;
		}

		sign = reachability->edgenum;
		int edgenum = abs(sign);
		reachability->edgenum = edgenum < aasworld.numEdges
			? optimized.edgeoptimizeindex[edgenum]
			: 0;
		if (sign < 0)
		{
			reachability->edgenum = -reachability->edgenum;
		}
	}

	AAS_OptimizeStore(&optimized);
	AAS_FreeOptimized(&optimized);
	BotLib_Print(PRT_MESSAGE, "AAS data optimized.\n");
}
