#include "aas_map.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <direct.h>
#include <io.h>
#define chdir _chdir
#define getcwd _getcwd
#define unlink _unlink
#else
#include <unistd.h>
#endif

#include "aas_local.h"
#include "aas_sound.h"
#include "botlib/ai_move/mover_catalogue.h"
#include "botlib/common/l_crc.h"
#include "botlib/common/l_libvar.h"
#include "botlib/common/l_log.h"
#include "botlib/common/l_memory.h"
#include "botlib/common/l_utils.h"
#include "botlib/interface/botlib_interface.h"
#include "botlib/precomp/l_script.h"
#include "q2bridge/bridge.h"
#include "shared/q_platform.h"

/*
 * Implemented in src/botlib/precomp/l_script.c but not yet exported by
 * l_script.h. Retail sub_100069a0 routes every entity-lump parse failure
 * through it (j_sub_1003e2c0 at 10006bda / 10006bed / 10006c34).
 */
void ScriptError(pc_script_t *script, char *str, ...);

static void AAS_UnlinkEntityFromAreas(aas_entity_t *entity);
static int AAS_LinkEntityToComputedAreas(aas_entity_t *entity, const vec3_t absmins, const vec3_t absmaxs);
static int AAS_LinkEntityToBSPLeaves(aas_entity_t *entity, const vec3_t absmins, const vec3_t absmaxs);
static void AAS_ResetEntityBitset(aas_entity_t *entity);
static int AAS_PrepareEntityBitset(aas_entity_t *entity);
static int AAS_EnsureAreaListArray(void);
static size_t AAS_AreaBitWordCount(void);
static qboolean AAS_BoxIntersectsArea(const vec3_t absmins, const vec3_t absmaxs, const aas_area_t *area);
static qboolean AAS_AreaEntityCollision(int areanum,
                                        const vec3_t start,
                                        const vec3_t end,
                                        int presencetype,
                                        int passent,
                                        aas_trace_t *trace);
static void AAS_ClampMinsMaxs(vec3_t mins, vec3_t maxs);
static void AAS_ClearBSPData(void);
static void AAS_ClearAASData(void);
static void AAS_ClearWorld(void);
static void AAS_FreeEntityArray(void);
static int AAS_AllocateConfiguredEntityArray(void);
static void AAS_ParseEntityLump(const char *data, size_t length);

static aas_link_t *g_aasLinkHeap;
static aas_link_t *g_aasLinkFreeList;
static int g_aasLinkHeapSize;
static bsp_link_t *g_bspLinkHeap;
static bsp_link_t *g_bspLinkFreeList;
static int g_bspLinkHeapSize;

typedef struct aas_indexlist_s
{
	int numindexes;
	char **indexes;
} aas_indexlist_t;

static aas_indexlist_t *g_aasModelIndexes;
static aas_indexlist_t *g_aasSoundIndexes;
static aas_indexlist_t *g_aasImageIndexes;
static qboolean g_aasIndexesLoaded;

#define AAS_AREA_STACK_SIZE 128
#define AAS_CLIENT_TRACE_STACK_SIZE 64
#define AAS_TRACEPLANE_EPSILON 0.125f
#define AAS_TRACE_ON_EPSILON 0.0005f
#define AAS_BSP_TRACE_EPSILON 0.005f
#define AAS_DEG2RAD 0.01745329251994329577f

typedef struct aas_tracearea_stack_s
{
	vec3_t start;
	vec3_t end;
	int nodenum;
} aas_tracearea_stack_t;

typedef struct aas_bboxarea_stack_s
{
	int nodenum;
} aas_bboxarea_stack_t;

typedef struct aas_clienttrace_stack_s
{
	vec3_t start;
	vec3_t end;
	int planenum;
	int nodenum;
} aas_clienttrace_stack_t;

typedef char aas_clienttrace_stack_entry_size[
	(sizeof(aas_clienttrace_stack_t) == 0x20U) ? 1 : -1];
typedef char aas_clienttrace_stack_storage_size[
	(sizeof(aas_clienttrace_stack_t) * AAS_CLIENT_TRACE_STACK_SIZE == 0x800U) ?
		1 : -1];

typedef struct aas_bsptrace_stack_s
{
	vec3_t start;
	vec3_t end;
	float startfraction;
	float endfraction;
	int nodenum;
} aas_bsptrace_stack_t;

typedef struct aas_bsptrace_transform_s
{
	vec3_t origin;
	vec3_t axis[3];
	qboolean rotated;
} aas_bsptrace_transform_t;

/*
 * Global AAS world state.  The original DLL zeroed the data_100667e0 block
 * during shutdown; the struct layout mirrors that memory region.
 */
aas_world_t aasworld = {0};
static qboolean g_aasLibraryInitialized = qfalse;
static qboolean g_aasEntityLimitsConfigured = qfalse;
static int g_aasConfiguredMaxEntities = 0;
static int g_aasConfiguredMaxClients = 0;

qboolean AAS_WorldLoaded(void)
{
    return aasworld.loaded;
}

/*
=============
AAS_Loaded

Return whether an AAS file is currently loaded.
=============
*/
int AAS_Loaded(void)
{
	return aasworld.loaded;
}

/*
=============
AAS_Initialized

Return whether AAS continuation setup has completed.
=============
*/
int AAS_Initialized(void)
{
	return aasworld.initialized;
}

/*
=============
AAS_Time

Return the current AAS frame time.
=============
*/
float AAS_Time(void)
{
	return aasworld.time;
}

/*
=============
AAS_Trace

Trace an axial box through the Quake II collision import.
=============
*/
bsp_trace_t AAS_Trace(const vec3_t start,
                      const vec3_t mins,
                      const vec3_t maxs,
                      const vec3_t end,
                      int passent,
                      int contentmask)
{
	return Q2_Trace((vec_t *)start,
	                (vec_t *)mins,
	                (vec_t *)maxs,
	                (vec_t *)end,
	                passent,
	                contentmask);
}

/*
=============
AAS_PointContents

Return the engine's contents at a point.
=============
*/
int AAS_PointContents(const vec3_t point)
{
	/*
	 * Retail sub_10003080 is a single tail call into the bot_import
	 * PointContents slot ("1000308e  return data_10063ff0(arg1)"), which the
	 * host wires to gi.pointcontents. The DLL's own BSP walk (sub_10005a10 /
	 * sub_100057a0) has no reachable callers. AAS_BSPModelPointContents is
	 * kept as that reconstruction but is no longer on this path.
	 */
	return Q2_PointContents((vec_t *)point);
}

/*
=============
AAS_EntitySlotInRange

Check whether an entity slot can be read from the AAS entity array.
=============
*/
static qboolean AAS_EntitySlotInRange(int entnum)
{
	return aasworld.entities != NULL &&
	       entnum >= 0 &&
	       entnum < aasworld.maxEntities;
}

/*
=============
AAS_EntityIsMover

Check whether a live entity represents a mover brush model.
=============
*/
static qboolean AAS_EntityIsMover(const aas_entity_t *entity)
{
	if (entity == NULL || !entity->inuse)
	{
		return qfalse;
	}

	if (entity->isMover)
	{
		return qtrue;
	}

	if (entity->solid != SOLID_BSP)
	{
		return qfalse;
	}

	int modelnum = AAS_ModelNumForEntity(entity->number);
	return modelnum > 0 && BotMove_MoverCatalogueFindByModel(modelnum) != NULL;
}

/*
=============
AAS_EntityTypeForState

Derive the Q3-style AAS entity type from the reconstructed Quake II state.
=============
*/
static int AAS_EntityTypeForState(const aas_entity_t *entity)
{
	if (AAS_EntityIsMover(entity))
	{
		return AAS_ENTITYTYPE_MOVER;
	}

	return AAS_ENTITYTYPE_GENERAL;
}

/*
=============
AAS_CopyEntityInfo

Copy a live entity into the public AAS entity info record.
=============
*/
static void AAS_CopyEntityInfo(const aas_entity_t *entity, aas_entityinfo_t *info)
{
	memset(info, 0, sizeof(*info));

	if (entity == NULL)
	{
		return;
	}

	info->valid = entity->inuse;
	info->type = AAS_EntityTypeForState(entity);
	info->ltime = entity->lastUpdateTime;
	info->update_time = entity->deltaTime;
	info->number = entity->number;
	VectorCopy(entity->origin, info->origin);
	VectorCopy(entity->angles, info->angles);
	VectorCopy(entity->old_origin, info->old_origin);
	VectorCopy(entity->previousOrigin, info->lastvisorigin);
	VectorCopy(entity->mins, info->mins);
	VectorCopy(entity->maxs, info->maxs);
	info->solid = entity->solid;
	info->modelindex = entity->modelindex;
	info->modelindex2 = entity->modelindex2;
	info->modelindex3 = entity->modelindex3;
	info->modelindex4 = entity->modelindex4;
	info->frame = entity->frame;
	info->skinnum = entity->skinnum;
	info->eventid = entity->eventid;
	info->effects = entity->effects;
	info->renderfx = entity->renderfx;
	info->sound = entity->sound;
}

/*
=============
AAS_EntityInfo

Copy the current AAS entity info record.
=============
*/
void AAS_EntityInfo(int entnum, aas_entityinfo_t *info)
{
	if (info == NULL)
	{
		return;
	}

	if (!aasworld.initialized)
	{
		BotLib_Print(PRT_FATAL, "AAS_EntityInfo: aasworld not initialized\n");
		memset(info, 0, sizeof(*info));
		return;
	}

	if (!AAS_EntitySlotInRange(entnum))
	{
		BotLib_Print(PRT_FATAL, "AAS_EntityInfo: entnum %d out of range\n", entnum);
		memset(info, 0, sizeof(*info));
		return;
	}

	AAS_CopyEntityInfo(&aasworld.entities[entnum], info);
}

/*
=============
AAS_EntityOrigin

Copy the current origin for an entity slot.
=============
*/
void AAS_EntityOrigin(int entnum, vec3_t origin)
{
	if (origin == NULL)
	{
		return;
	}

	if (!AAS_EntitySlotInRange(entnum))
	{
		BotLib_Print(PRT_FATAL, "AAS_EntityOrigin: entnum %d out of range\n", entnum);
		VectorClear(origin);
		return;
	}

	VectorCopy(aasworld.entities[entnum].origin, origin);
}

/*
=============
AAS_EntityModelindex

Return the raw model index stored for an entity slot.
=============
*/
int AAS_EntityModelindex(int entnum)
{
	if (!AAS_EntitySlotInRange(entnum))
	{
		BotLib_Print(PRT_FATAL, "AAS_EntityModelindex: entnum %d out of range\n", entnum);
		return 0;
	}

	return aasworld.entities[entnum].modelindex;
}

/*
=============
AAS_EntityRenderFX

Return the render effects stored for an entity slot.
=============
*/
int AAS_EntityRenderFX(int entnum)
{
	if (!aasworld.initialized)
	{
		return 0;
	}

	if (!AAS_EntitySlotInRange(entnum))
	{
		BotLib_Print(PRT_FATAL, "AAS_EntityRenderFX: entnum %d out of range\n", entnum);
		return 0;
	}

	return aasworld.entities[entnum].renderfx;
}

/*
=============
AAS_EntityModelNum

Return the zero-based model number derived from the stored model index.
=============
*/
int AAS_EntityModelNum(int entnum)
{
	if (!aasworld.initialized)
	{
		return 0;
	}

	if (!AAS_EntitySlotInRange(entnum))
	{
		BotLib_Print(PRT_FATAL, "AAS_EntityModelNum: entnum %d out of range\n", entnum);
		return 0;
	}

	return aasworld.entities[entnum].modelindex - 1;
}

/*
=============
AAS_EntitySize

Copy an entity's local bounding box.
=============
*/
void AAS_EntitySize(int entnum, vec3_t mins, vec3_t maxs)
{
	if (mins == NULL || maxs == NULL)
	{
		return;
	}

	if (!aasworld.initialized)
	{
		return;
	}

	if (!AAS_EntitySlotInRange(entnum))
	{
		BotLib_Print(PRT_FATAL, "AAS_EntitySize: entnum %d out of range\n", entnum);
		return;
	}

	const aas_entity_t *entity = &aasworld.entities[entnum];
	VectorCopy(entity->mins, mins);
	VectorCopy(entity->maxs, maxs);
}

/*
=============
AAS_OriginOfMoverWithModelNum

Find the first entity slot with the requested brush-model number and copy its
origin.
=============
*/
int AAS_OriginOfMoverWithModelNum(int modelnum, vec3_t origin)
{
	if (origin == NULL || aasworld.entities == NULL)
	{
		return qfalse;
	}

	for (int entnum = 0; entnum < aasworld.maxEntities; ++entnum)
	{
		const aas_entity_t *entity = &aasworld.entities[entnum];
		if (entity->modelindex - 1 == modelnum)
		{
			VectorCopy(entity->origin, origin);
			return qtrue;
		}
	}

	return qfalse;
}

/*
=============
AAS_NearestEntity

Return the closest entity slot with the requested raw model index.
=============
*/
int AAS_NearestEntity(const vec3_t origin, int modelindex)
{
	if (origin == NULL || aasworld.entities == NULL)
	{
		return 0;
	}

	int bestentnum = 0;
	float bestdistance = 99999.0f;
	for (int entnum = 0; entnum < aasworld.maxEntities; ++entnum)
	{
		const aas_entity_t *entity = &aasworld.entities[entnum];
		if (entity->modelindex != modelindex)
		{
			continue;
		}

		vec3_t direction;
		VectorSubtract(entity->origin, origin, direction);
		if (abs((int)direction[0]) >= 40 || abs((int)direction[1]) >= 40)
		{
			continue;
		}

		float distance = sqrtf(direction[0] * direction[0] +
			direction[1] * direction[1] +
			direction[2] * direction[2]);
		if (distance < bestdistance)
		{
			bestdistance = distance;
			bestentnum = entnum;
		}
	}

	return bestentnum;
}

/*
=============
AAS_PresenceTypeBoundingBox

Return the retail AAS bounding box for a presence type.
=============
*/
void AAS_PresenceTypeBoundingBox(int presencetype, vec3_t mins, vec3_t maxs)
{
	/*
	 * Retail sub_1000dda0 builds two 3-entry stack tables and indexes them by
	 * a selector that is 1 for presence type 4 and 2 for everything else:
	 *   1000ddc2  mins[1] = mins[2] = {-16,-16,-24}   (0xC1800000/0xC1C00000)
	 *   1000de0a  maxs[1] = {16,16,32}, maxs[2] = {16,16,8}
	 *   1000de3a  if (arg1 != 4) { if (arg1 != 2) PRT_FATAL; index = 2; }
	 *   1000de3c  else index = 1
	 * The half extents are +/-16 (Q2's 32x32 player), not Q3's +/-15, and the
	 * literal 4 selects the TALL box while 2 selects the crouch box - the
	 * opposite of the Q3 be_aas_sample.c mapping. The bare literals are used
	 * on purpose: PRESENCE_NORMAL/PRESENCE_CROUCH keep the values retail's
	 * areasettings presence mask needs (sub_100115d0), and retail is simply
	 * inconsistent between the mask and this table.
	 */
	static const vec3_t boxmins[3] = {
		{ 0.0f, 0.0f, 0.0f },
		{ -16.0f, -16.0f, -24.0f },
		{ -16.0f, -16.0f, -24.0f }
	};
	static const vec3_t boxmaxs[3] = {
		{ 0.0f, 0.0f, 0.0f },
		{ 16.0f, 16.0f, 32.0f },
		{ 16.0f, 16.0f, 8.0f }
	};
	int index;

	if (mins == NULL || maxs == NULL)
	{
		return;
	}

	if (presencetype == 4)
	{
		index = 1;
	}
	else
	{
		if (presencetype != 2)
		{
			BotLib_Print(PRT_FATAL,
				"AAS_PresenceTypeBoundingBox: unknown presence type\n");
		}
		index = 2;
	}

	VectorCopy(boxmins[index], mins);
	VectorCopy(boxmaxs[index], maxs);
}

/*
=============
AAS_PointAreaNum

Resolve a point to the loaded area bounds currently reconstructed from AAS.
=============
*/
int AAS_PointAreaNum(const vec3_t point)
{
	if (!aasworld.loaded)
	{
		BotLib_Print(PRT_ERROR, "AAS_PointAreaNum: aas not loaded\n");
		return 0;
	}

	if (point == NULL || aasworld.areas == NULL || aasworld.numAreas <= 0)
	{
		return 0;
	}

	if (aasworld.nodes != NULL && aasworld.numNodes > 1 &&
	    aasworld.planes != NULL && aasworld.numPlanes > 0)
	{
		int nodenum = 1;
		while (nodenum > 0)
		{
			if (nodenum >= aasworld.numNodes)
			{
				return 0;
			}

			const aas_node_t *node = &aasworld.nodes[nodenum];
			if (node->planenum < 0 || node->planenum >= aasworld.numPlanes)
			{
				return 0;
			}

			const aas_plane_t *plane = &aasworld.planes[node->planenum];
			float dist = DotProduct(point, plane->normal) - plane->dist;
			if (dist > 0.0f)
			{
				nodenum = node->children[0];
			}
			else
			{
				nodenum = node->children[1];
			}
		}

		return (nodenum < 0) ? -nodenum : 0;
	}

	for (int areanum = 1; areanum < aasworld.numAreas; ++areanum)
	{
		const aas_area_t *area = &aasworld.areas[areanum];
		if (point[0] < area->mins[0] || point[0] > area->maxs[0])
		{
			continue;
		}
		if (point[1] < area->mins[1] || point[1] > area->maxs[1])
		{
			continue;
		}
		if (point[2] < area->mins[2] || point[2] > area->maxs[2])
		{
			continue;
		}

		return areanum;
	}

	return 0;
}

/*
=============
AAS_PointPresenceType

Return the presence mask for the AAS area containing a point.
=============
*/
int AAS_PointPresenceType(const vec3_t point)
{
	if (!aasworld.loaded)
	{
		return 0;
	}

	int areanum = AAS_PointAreaNum(point);
	if (areanum == 0)
	{
		return PRESENCE_NONE;
	}

	return AAS_AreaPresenceType(areanum);
}

/*
=============
AAS_VectorLength

Return the length of a 3D vector.
=============
*/
static float AAS_VectorLength(const vec3_t v)
{
	if (v == NULL)
	{
		return 0.0f;
	}

	return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

/*
=============
AAS_NormalizeVector

Normalize a vector in place and return its previous length.
=============
*/
static float AAS_NormalizeVector(vec3_t v)
{
	float length = AAS_VectorLength(v);
	if (length <= 1e-6f)
	{
		VectorClear(v);
		return 0.0f;
	}

	float scale = 1.0f / length;
	VectorScale(v, scale, v);
	return length;
}

/*
=============
AAS_AreaAllowsPresence

Check whether an area accepts the requested presence type.
=============
*/
static qboolean AAS_AreaAllowsPresence(int areanum, int presencetype)
{
	if (aasworld.areasettings == NULL ||
	    areanum <= 0 ||
	    areanum >= aasworld.numAreaSettings)
	{
		return qfalse;
	}

	return (aasworld.areasettings[areanum].presencetype & presencetype) != 0;
}

/*
=============
AAS_TraceEntitySolid

Check whether an entity should participate in reconstructed AAS entity traces.
=============
*/
static qboolean AAS_TraceEntitySolid(const aas_entity_t *entity)
{
	if (entity == NULL || !entity->inuse)
	{
		return qfalse;
	}

	return entity->solid == SOLID_BBOX || entity->solid == SOLID_BSP;
}

/*
=============
AAS_CPlaneSignBits

Calculate cplane sign bits for a normal.
=============
*/
static byte AAS_CPlaneSignBits(const vec3_t normal)
{
	byte signbits = 0;
	for (int axis = 0; axis < 3; ++axis)
	{
		if (normal[axis] < 0.0f)
		{
			signbits |= (byte)(1U << axis);
		}
	}

	return signbits;
}

/*
=============
AAS_SetTraceCPlane

Fill a collision plane from an axis hit normal and point.
=============
*/
static void AAS_SetTraceCPlane(cplane_t *plane, const vec3_t normal, const vec3_t point, int axis)
{
	if (plane == NULL || normal == NULL || point == NULL)
	{
		return;
	}

	memset(plane, 0, sizeof(*plane));
	VectorCopy(normal, plane->normal);
	plane->dist = DotProduct(normal, point);
	plane->type = (byte)((axis >= 0 && axis < 3) ? axis : 3);
	plane->signbits = AAS_CPlaneSignBits(normal);
}

/*
=============
AAS_TraceEntityBBox

Sweep a box origin through an entity's expanded axial bounds.
=============
*/
static qboolean AAS_TraceEntityBBox(const aas_entity_t *entity,
                                    int entnum,
                                    const vec3_t start,
                                    const vec3_t boxmins,
                                    const vec3_t boxmaxs,
                                    const vec3_t end,
                                    int contentmask,
                                    bsp_trace_t *trace)
{
	if (!AAS_TraceEntitySolid(entity) ||
	    start == NULL ||
	    end == NULL ||
	    trace == NULL)
	{
		return qfalse;
	}

	vec3_t sweepmins;
	vec3_t sweepmaxs;
	if (boxmins != NULL)
	{
		VectorCopy(boxmins, sweepmins);
	}
	else
	{
		VectorClear(sweepmins);
	}
	if (boxmaxs != NULL)
	{
		VectorCopy(boxmaxs, sweepmaxs);
	}
	else
	{
		VectorClear(sweepmaxs);
	}
	AAS_ClampMinsMaxs(sweepmins, sweepmaxs);

	vec3_t absmins;
	vec3_t absmaxs;
	VectorAdd(entity->origin, entity->mins, absmins);
	VectorAdd(entity->origin, entity->maxs, absmaxs);
	AAS_ClampMinsMaxs(absmins, absmaxs);

	vec3_t expandedmins;
	vec3_t expandedmaxs;
	for (int axis = 0; axis < 3; ++axis)
	{
		expandedmins[axis] = absmins[axis] - sweepmaxs[axis];
		expandedmaxs[axis] = absmaxs[axis] - sweepmins[axis];
	}
	AAS_ClampMinsMaxs(expandedmins, expandedmaxs);

	bsp_trace_t local;
	memset(&local, 0, sizeof(local));
	local.fraction = 1.0f;
	local.ent = entnum;
	/* Retail BBOX traces use the sentinel side and leave contents clear. */
	local.sidenum = -1;

	qboolean startsinside = qtrue;
	float startsolidmargin = (entity->solid == SOLID_BBOX) ? 0.5f : 0.0f;
	for (int axis = 0; axis < 3; ++axis)
	{
		if (start[axis] < expandedmins[axis] - startsolidmargin ||
			start[axis] > expandedmaxs[axis] + startsolidmargin)
		{
			startsinside = qfalse;
			break;
		}
	}

	if (startsinside)
	{
		local.startsolid = qtrue;
		local.fraction = 0.0f;
		/*
		 * Retail sub_10003680 marks both fields as soon as a BBOX sweep
		 * starts inside the expanded bounds, even if its endpoint escapes.
		 */
		local.allsolid = qtrue;
		VectorCopy(start, local.endpos);
		/*
		 * sub_10003680 writes this start-inside BBOX result straight to its
		 * caller record.  Unlike an ordinary sweep hit, it does not retain an
		 * already-zero fraction from an earlier linked entity.
		 */
		*trace = local;
		return qtrue;
	}

	vec3_t direction;
	VectorSubtract(end, start, direction);

	float enter = 0.0f;
	float leave = 1.0f;
	int hitaxis = -1;
	float hitnormal = 0.0f;

	for (int axis = 0; axis < 3; ++axis)
	{
		float move = direction[axis];
		if (fabsf(move) <= 1e-6f)
		{
			if (start[axis] < expandedmins[axis] || start[axis] > expandedmaxs[axis])
			{
				return qfalse;
			}
			continue;
		}

		float nearfrac = (expandedmins[axis] - start[axis]) / move;
		float farfrac = (expandedmaxs[axis] - start[axis]) / move;
		float normal = -1.0f;
		if (nearfrac > farfrac)
		{
			float tmp = nearfrac;
			nearfrac = farfrac;
			farfrac = tmp;
			normal = 1.0f;
		}

		if (nearfrac > enter)
		{
			enter = nearfrac;
			hitaxis = axis;
			hitnormal = normal;
		}
		if (farfrac < leave)
		{
			leave = farfrac;
		}
		if (enter > leave)
		{
			return qfalse;
		}
	}

	if (hitaxis < 0 || enter < 0.0f || enter > 1.0f)
	{
		return qfalse;
	}
	if (enter >= trace->fraction)
	{
		return qfalse;
	}

	local.fraction = enter;
	VectorMA(start, enter, direction, local.endpos);

	vec3_t normal;
	VectorClear(normal);
	normal[hitaxis] = hitnormal;
	AAS_SetTraceCPlane(&local.plane, normal, local.endpos, hitaxis);
	/*
	 * sub_10003680 leaves cplane.signbits clear for BBOX hits, then adjusts
	 * the face distance back out to the entity boundary with the swept extent.
	 */
	local.plane.signbits = 0;
	local.exp_dist = (hitnormal < 0.0f) ?
		sweepmaxs[hitaxis] : -sweepmins[hitaxis];
	local.plane.dist -= local.exp_dist;

	*trace = local;
	return qtrue;
}

/*
=============
AAS_VectorIsZero

Return true when all vector components are zero.
=============
*/
static qboolean AAS_VectorIsZero(const vec3_t value)
{
	return value == NULL ||
	       (value[0] == 0.0f && value[1] == 0.0f && value[2] == 0.0f);
}

/*
=============
AAS_AnglesToAxis

Build the Quake-style orientation axes used by transformed BSP model traces.
=============
*/
static void AAS_AnglesToAxis(const vec3_t angles, vec3_t axis[3])
{
	float yaw = angles[YAW] * AAS_DEG2RAD;
	float pitch = angles[PITCH] * AAS_DEG2RAD;
	float roll = angles[ROLL] * AAS_DEG2RAD;

	float sy = sinf(yaw);
	float cy = cosf(yaw);
	float sp = sinf(pitch);
	float cp = cosf(pitch);
	float sr = sinf(roll);
	float cr = cosf(roll);

	VectorSet(axis[0], cp * cy, cp * sy, -sp);

	vec3_t right;
	VectorSet(right,
	          -sr * sp * cy + -cr * -sy,
	          -sr * sp * sy + -cr * cy,
	          -sr * cp);
	VectorNegate(right, axis[1]);

	VectorSet(axis[2],
	          cr * sp * cy + -sr * -sy,
	          cr * sp * sy + -sr * cy,
	          cr * cp);
}

/*
=============
AAS_RotateLocalVector

Rotate a local model-space vector into world orientation.
=============
*/
static void AAS_RotateLocalVector(const vec3_t local, const vec3_t axis[3], vec3_t world)
{
	for (int component = 0; component < 3; ++component)
	{
		world[component] = local[0] * axis[0][component] +
		                   local[1] * axis[1][component] +
		                   local[2] * axis[2][component];
	}
}

/*
=============
AAS_WorldToLocalVector

Rotate a world-oriented vector into model local space.
=============
*/
static void AAS_WorldToLocalVector(const vec3_t world, vec3_t axis[3], vec3_t local)
{
	local[0] = DotProduct(world, axis[0]);
	local[1] = DotProduct(world, axis[1]);
	local[2] = DotProduct(world, axis[2]);
}

/*
=============
AAS_BSPModelValid

Check whether a BSP inline model number can be read.
=============
*/
static qboolean AAS_BSPModelValid(int modelnum)
{
	return aasworld.bspModels != NULL &&
	       modelnum >= 0 &&
	       modelnum < aasworld.numBspModels;
}

/*
=============
AAS_BSPModelMinsMaxsOrigin

Return rotated local bounds and the model origin for a Quake II BSP inline model.
=============
*/
void AAS_BSPModelMinsMaxsOrigin(int modelnum,
                                const vec3_t angles,
                                vec3_t mins,
                                vec3_t maxs,
                                vec3_t origin)
{
	/* sub_10005e60 returns before touching any caller output with no model table. */
	if (aasworld.bspModels == NULL)
	{
		return;
	}

	if (mins != NULL)
	{
		VectorClear(mins);
	}
	if (maxs != NULL)
	{
		VectorClear(maxs);
	}
	if (origin != NULL)
	{
		VectorClear(origin);
	}

	if (!AAS_BSPModelValid(modelnum))
	{
		BotLib_Print(PRT_FATAL,
		             "AAS_BSPModelMinsMaxs: modelnum %d out of range [0-%d]",
		             modelnum,
		             aasworld.numBspModels);
		return;
	}

	const aas_bspmodel_t *model = &aasworld.bspModels[modelnum];
	if (origin != NULL)
	{
		VectorCopy(model->origin, origin);
	}

	if (mins == NULL && maxs == NULL)
	{
		return;
	}

	if (angles == NULL || AAS_VectorIsZero(angles))
	{
		if (mins != NULL)
		{
			VectorCopy(model->mins, mins);
		}
		if (maxs != NULL)
		{
			VectorCopy(model->maxs, maxs);
		}
		return;
	}

	vec3_t axis[3];
	vec3_t rotatedmins;
	vec3_t rotatedmaxs;
	AAS_AnglesToAxis(angles, axis);

	for (int component = 0; component < 3; ++component)
	{
		rotatedmins[component] = 999999.0f;
		rotatedmaxs[component] = -999999.0f;
	}

	for (int corner = 0; corner < 8; ++corner)
	{
		vec3_t local;
		local[0] = (corner & 1) ? model->maxs[0] : model->mins[0];
		local[1] = (corner & 2) ? model->maxs[1] : model->mins[1];
		local[2] = (corner & 4) ? model->maxs[2] : model->mins[2];

		vec3_t rotated;
		AAS_RotateLocalVector(local, axis, rotated);
		for (int component = 0; component < 3; ++component)
		{
			if (rotated[component] < rotatedmins[component])
			{
				rotatedmins[component] = rotated[component];
			}
			if (rotated[component] > rotatedmaxs[component])
			{
				rotatedmaxs[component] = rotated[component];
			}
		}
	}

	if (mins != NULL)
	{
		VectorCopy(rotatedmins, mins);
	}
	if (maxs != NULL)
	{
		VectorCopy(rotatedmaxs, maxs);
	}
}

/*
=============
AAS_BSPTracePlaneOffset

Calculate the plane expansion offset for tracing a world-axis-aligned box.
=============
*/
static float AAS_BSPTracePlaneOffset(const vec3_t normal, const vec3_t mins, const vec3_t maxs)
{
	if (normal == NULL)
	{
		return 0.0f;
	}

	float offset = 0.0f;
	for (int component = 0; component < 3; ++component)
	{
		if (normal[component] < 0.0f)
		{
			offset += normal[component] * ((maxs != NULL) ? maxs[component] : 0.0f);
		}
		else
		{
			offset += normal[component] * ((mins != NULL) ? mins[component] : 0.0f);
		}
	}

	return offset;
}

/*
=============
AAS_BSPTraceWorldPlane

Convert one model-local BSP plane into the world-space plane used by the
retail transformed trace walker.
=============
*/
static void AAS_BSPTraceWorldPlane(const aas_plane_t *plane,
	                                  const aas_bsptrace_transform_t *transform,
	                                  vec3_t normal,
	                                  float *dist)
{
	if (normal == NULL || dist == NULL)
	{
		return;
	}

	VectorClear(normal);
	*dist = 0.0f;
	if (plane == NULL)
	{
		return;
	}

	if (transform != NULL && transform->rotated)
	{
		AAS_RotateLocalVector(plane->normal, transform->axis, normal);
	}
	else
	{
		VectorCopy(plane->normal, normal);
	}

	*dist = plane->dist;
	if (transform != NULL)
	{
		*dist += DotProduct(normal, transform->origin);
	}
}

/*
=============
AAS_CopyBSPTracePlane

Copy the raw loaded BSP cplane into a trace result.  The transformed collision
math stays in world space, but sub_10004310 writes this model-local payload.
=============
*/
static void AAS_CopyBSPTracePlane(bsp_trace_t *trace, const aas_plane_t *plane)
{
	if (trace == NULL || plane == NULL)
	{
		return;
	}

	VectorCopy(plane->normal, trace->plane.normal);
	trace->plane.dist = plane->dist;
	trace->plane.type = (byte)plane->type;
	/* The raw 0x54-byte writer leaves this byte from its zeroed trace record. */
	trace->plane.signbits = 0;
}

/*
=============
AAS_TraceThroughBSPBrush

Clip a world-axis-aligned box sweep through one transformed convex BSP brush.
=============
*/
static qboolean AAS_TraceThroughBSPBrush(const aas_bspbrush_t *brush,
                                         const vec3_t start,
                                         const vec3_t mins,
                                         const vec3_t maxs,
                                         const vec3_t end,
                                         int contentmask,
	                                         const aas_bsptrace_transform_t *transform,
                                         bsp_trace_t *trace)
{
	if (brush == NULL ||
	    trace == NULL ||
	    (brush->contents & contentmask) == 0 ||
	    brush->numsides <= 0 ||
	    brush->firstside < 0 ||
	    aasworld.bspBrushSides == NULL ||
	    aasworld.bspPlanes == NULL)
	{
		return qfalse;
	}

	float enterfrac = -1.0f;
	float leavefrac = 1.0f;
	const aas_plane_t *clipplane = NULL;
	float clipdist = 0.0f;
	int clipside = -1;
	qboolean startsout = qfalse;

	for (int sideindex = 0; sideindex < brush->numsides; ++sideindex)
	{
		int brushsideindex = brush->firstside + sideindex;
		if (brushsideindex < 0 || brushsideindex >= aasworld.numBspBrushSides)
		{
			continue;
		}

		const aas_bspbrushside_t *side = &aasworld.bspBrushSides[brushsideindex];
		int planenum = (int)side->planenum;
		if (planenum < 0 || planenum >= aasworld.numBspPlanes)
		{
			continue;
		}

		const aas_plane_t *plane = &aasworld.bspPlanes[planenum];
		vec3_t normal;
		float planedist;
		AAS_BSPTraceWorldPlane(plane, transform, normal, &planedist);
		float expandedDist = planedist + AAS_BSPTracePlaneOffset(normal, mins, maxs);
		float startdist = DotProduct(start, normal) - expandedDist;
		float enddist = DotProduct(end, normal) - expandedDist;

		if (startdist > 0.0f)
		{
			startsout = qtrue;
		}
		if (startdist > 0.0f && enddist > 0.0f)
		{
			return qfalse;
		}
		if (startdist <= 0.0f && enddist <= 0.0f)
		{
			continue;
		}

		if (startdist > enddist)
		{
			float fraction = (startdist - AAS_BSP_TRACE_EPSILON) / (startdist - enddist);
			if (fraction > enterfrac)
			{
				enterfrac = fraction;
				clipplane = plane;
				clipdist = expandedDist;
				clipside = brushsideindex;
			}
		}
		else
		{
			float fraction = (startdist + AAS_BSP_TRACE_EPSILON) / (startdist - enddist);
			if (fraction < leavefrac)
			{
				leavefrac = fraction;
			}
		}
	}

	if (!startsout)
	{
		trace->startsolid = qtrue;
		trace->contents = brush->contents;
		/*
		 * The retail BSP brush writer carries the same start-inside allsolid
		 * quirk as its BBOX branch, regardless of whether the trace exits.
		 */
		trace->allsolid = qtrue;
		return qtrue;
	}

	if (enterfrac < leavefrac && enterfrac > -1.0f && enterfrac < trace->fraction)
	{
		if (enterfrac < 0.0f)
		{
			enterfrac = 0.0f;
		}
		trace->fraction = enterfrac;
		trace->contents = brush->contents;
		trace->sidenum = clipside;
		trace->exp_dist = clipdist;
		/*
		 * Retail sub_10004310 stores the global brush-side index at 0x10004432,
		 * then skips the trace surface payload before storing contents at
		 * 0x10004474.  Keep surface zeroed even though the side retains texinfo.
		 */
		AAS_CopyBSPTracePlane(trace, clipplane);
		return qtrue;
	}

	return qfalse;
}

/*
=============
AAS_TraceThroughBSPLeaf

Trace through every brush referenced by a BSP leaf.
=============
*/
static void AAS_TraceThroughBSPLeaf(int leafnum,
                                    const vec3_t start,
                                    const vec3_t mins,
                                    const vec3_t maxs,
                                    const vec3_t end,
                                    int contentmask,
	                                const aas_bsptrace_transform_t *transform,
                                    bsp_trace_t *trace)
{
	if (leafnum < 0 ||
	    leafnum >= aasworld.numBspLeaves ||
	    aasworld.bspLeaves == NULL ||
	    aasworld.bspLeafBrushes == NULL ||
	    aasworld.bspBrushes == NULL)
	{
		return;
	}

	const aas_bspleaf_t *leaf = &aasworld.bspLeaves[leafnum];
	for (int index = 0; index < leaf->numleafbrushes; ++index)
	{
		int leafbrushindex = (int)leaf->firstleafbrush + index;
		if (leafbrushindex < 0 || leafbrushindex >= aasworld.bspLeafBrushIndexSize)
		{
			continue;
		}

		int brushnum = (int)aasworld.bspLeafBrushes[leafbrushindex];
		if (brushnum < 0 || brushnum >= aasworld.numBspBrushes)
		{
			continue;
		}

		AAS_TraceThroughBSPBrush(&aasworld.bspBrushes[brushnum],
		                         start,
		                         mins,
		                         maxs,
		                         end,
		                         contentmask,
		                         transform,
		                         trace);
		if (trace->allsolid)
		{
			return;
		}
	}
}

/*
=============
AAS_PushBSPTraceNode

Push a BSP trace segment onto the local traversal stack.
=============
*/
static qboolean AAS_PushBSPTraceNode(aas_bsptrace_stack_t *stack,
                                     int *stacktop,
                                     int nodenum,
                                     float startfraction,
                                     float endfraction,
                                     const vec3_t start,
                                     const vec3_t end)
{
	if (stack == NULL || stacktop == NULL || *stacktop >= AAS_AREA_STACK_SIZE)
	{
		BotLib_Print(PRT_ERROR, "AAS_TraceBSPModel: out of trace lines\n");
		return qfalse;
	}

	aas_bsptrace_stack_t *entry = &stack[*stacktop];
	VectorCopy(start, entry->start);
	VectorCopy(end, entry->end);
	entry->startfraction = startfraction;
	entry->endfraction = endfraction;
	entry->nodenum = nodenum;
	*stacktop += 1;
	return qtrue;
}

/*
=============
AAS_TraceBSPModelTree

Walk a BSP inline model tree and trace through touched leaves.
=============
*/
static void AAS_TraceBSPModelTree(int headnode,
                                  const vec3_t start,
                                  const vec3_t mins,
                                  const vec3_t maxs,
                                  const vec3_t end,
                                  int contentmask,
	                                  const aas_bsptrace_transform_t *transform,
                                  bsp_trace_t *trace)
{
	aas_bsptrace_stack_t stack[AAS_AREA_STACK_SIZE];
	int stacktop = 0;
	if (!AAS_PushBSPTraceNode(stack, &stacktop, headnode, 0.0f, 1.0f, start, end))
	{
		return;
	}

	while (stacktop > 0)
	{
		aas_bsptrace_stack_t current = stack[--stacktop];
		if (trace->fraction <= current.startfraction)
		{
			continue;
		}

		if (current.nodenum < 0)
		{
			int leafnum = -1 - current.nodenum;
			AAS_TraceThroughBSPLeaf(leafnum,
			                        start,
			                        mins,
			                        maxs,
			                        end,
			                        contentmask,
			                        transform,
			                        trace);
			continue;
		}

		if (current.nodenum >= aasworld.numBspNodes || aasworld.bspNodes == NULL)
		{
			continue;
		}

		const aas_bspnode_t *node = &aasworld.bspNodes[current.nodenum];
		if (node->planenum < 0 || node->planenum >= aasworld.numBspPlanes)
		{
			continue;
		}

		const aas_plane_t *plane = &aasworld.bspPlanes[node->planenum];
		vec3_t normal;
		float planedist;
		AAS_BSPTraceWorldPlane(plane, transform, normal, &planedist);
		float expandedDist = planedist + AAS_BSPTracePlaneOffset(normal, mins, maxs);
		float front = DotProduct(current.start, normal) - expandedDist;
		float back = DotProduct(current.end, normal) - expandedDist;
		/*
		 * sub_100044f0 classifies the expanded box against BSP split planes
		 * through a ±0.005 band before deciding that a segment is wholly in
		 * one child.  This is distinct from the brush clip epsilon below:
		 * a segment whose front endpoint is just ahead of the plane can be
		 * routed straight to the back child instead of visiting both leaves.
		 */
		if (front > -AAS_BSP_TRACE_EPSILON &&
			back > -AAS_BSP_TRACE_EPSILON)
		{
			if (!AAS_PushBSPTraceNode(stack,
			                          &stacktop,
			                          node->children[0],
			                          current.startfraction,
			                          current.endfraction,
			                          current.start,
			                          current.end))
			{
				return;
			}
			continue;
		}
		if (front < AAS_BSP_TRACE_EPSILON &&
			back < AAS_BSP_TRACE_EPSILON)
		{
			if (!AAS_PushBSPTraceNode(stack,
			                          &stacktop,
			                          node->children[1],
			                          current.startfraction,
			                          current.endfraction,
			                          current.start,
			                          current.end))
			{
				return;
			}
			continue;
		}

		float fraction = front / (front - back);
		if (fraction < 0.0f)
		{
			fraction = 0.0f;
		}
		else if (fraction > 1.0f)
		{
			fraction = 1.0f;
		}

		vec3_t middle;
		for (int component = 0; component < 3; ++component)
		{
			middle[component] = current.start[component] +
			                    (current.end[component] - current.start[component]) * fraction;
		}

		float middlefraction = current.startfraction +
		                       (current.endfraction - current.startfraction) * fraction;
		int side = (front < 0.0f) ? 1 : 0;
		if (!AAS_PushBSPTraceNode(stack,
		                          &stacktop,
		                          node->children[side ^ 1],
		                          middlefraction,
		                          current.endfraction,
		                          middle,
		                          current.end))
		{
			return;
		}
		if (!AAS_PushBSPTraceNode(stack,
		                          &stacktop,
		                          node->children[side],
		                          current.startfraction,
		                          middlefraction,
		                          current.start,
		                          middle))
		{
			return;
		}
	}
}

/*
=============
AAS_TraceBSPModel

Trace an axial box through a loaded Quake II BSP inline model.
=============
*/
bsp_trace_t AAS_TraceBSPModel(int modelnum,
                              const vec3_t angles,
                              const vec3_t origin,
                              const vec3_t start,
                              const vec3_t mins,
                              const vec3_t maxs,
                              const vec3_t end,
                              int contentmask)
{
	bsp_trace_t trace;
	memset(&trace, 0, sizeof(trace));
	trace.fraction = 1.0f;
	if (end != NULL)
	{
		VectorCopy(end, trace.endpos);
	}

	if (!AAS_BSPModelValid(modelnum) ||
	    start == NULL ||
	    end == NULL ||
	    contentmask == 0)
	{
		return trace;
	}

	const aas_bspmodel_t *model = &aasworld.bspModels[modelnum];
	vec3_t totalOrigin;
	VectorCopy(model->origin, totalOrigin);
	if (origin != NULL)
	{
		VectorAdd(totalOrigin, origin, totalOrigin);
	}

	aas_bsptrace_transform_t transform;
	memset(&transform, 0, sizeof(transform));
	VectorCopy(totalOrigin, transform.origin);
	transform.rotated = angles != NULL && !AAS_VectorIsZero(angles);
	if (transform.rotated)
	{
		AAS_AnglesToAxis(angles, transform.axis);
	}

	vec3_t traceMins;
	vec3_t traceMaxs;
	if (mins != NULL)
	{
		VectorCopy(mins, traceMins);
	}
	else
	{
		VectorClear(traceMins);
	}
	if (maxs != NULL)
	{
		VectorCopy(maxs, traceMaxs);
	}
	else
	{
		VectorClear(traceMaxs);
	}
	AAS_ClampMinsMaxs(traceMins, traceMaxs);

	AAS_TraceBSPModelTree(model->headnode,
	                      start,
	                      traceMins,
	                      traceMaxs,
	                      end,
	                      contentmask,
	                      &transform,
	                      &trace);

	if (trace.startsolid)
	{
		VectorCopy(start, trace.endpos);
		trace.fraction = 0.0f;
	}
	else
	{
		vec3_t direction;
		VectorSubtract(end, start, direction);
		VectorMA(start, trace.fraction, direction, trace.endpos);
	}

	return trace;
}

/*
=============
AAS_BSPModelPointLeafNum

Resolve a local-space point to one Quake II BSP leaf using the retail
front-on-positive, back-on-zero node rule.
=============
*/
static int AAS_BSPModelPointLeafNum(int modelnum, const vec3_t point)
{
	if (!AAS_BSPModelValid(modelnum) || point == NULL)
	{
		return -1;
	}

	int nodenum = aasworld.bspModels[modelnum].headnode;
	while (nodenum >= 0)
	{
		if (aasworld.bspNodes == NULL || nodenum >= aasworld.numBspNodes)
		{
			return -1;
		}

		const aas_bspnode_t *node = &aasworld.bspNodes[nodenum];
		if (aasworld.bspPlanes == NULL ||
			node->planenum < 0 || node->planenum >= aasworld.numBspPlanes)
		{
			return -1;
		}

		const aas_plane_t *plane = &aasworld.bspPlanes[node->planenum];
		float distance = DotProduct(point, plane->normal) - plane->dist;
		nodenum = node->children[(distance > 0.0f) ? 0 : 1];
	}

	int leafnum = -1 - nodenum;
	if (aasworld.bspLeaves == NULL || leafnum < 0 || leafnum >= aasworld.numBspLeaves)
	{
		return -1;
	}

	return leafnum;
}

/*
=============
AAS_PointInsideBSPBrush

Test a point against a convex brush with the DLL's 0.005 outside epsilon.
=============
*/
static qboolean AAS_PointInsideBSPBrush(const aas_bspbrush_t *brush, const vec3_t point)
{
	if (brush == NULL || point == NULL || brush->firstside < 0 ||
		brush->numsides <= 0 || aasworld.bspBrushSides == NULL ||
		aasworld.bspPlanes == NULL)
	{
		return qfalse;
	}

	for (int sideindex = 0; sideindex < brush->numsides; ++sideindex)
	{
		int brushsideindex = brush->firstside + sideindex;
		if (brushsideindex < 0 || brushsideindex >= aasworld.numBspBrushSides)
		{
			return qfalse;
		}

		int planenum = (int)aasworld.bspBrushSides[brushsideindex].planenum;
		if (planenum < 0 || planenum >= aasworld.numBspPlanes)
		{
			return qfalse;
		}

		const aas_plane_t *plane = &aasworld.bspPlanes[planenum];
		if (DotProduct(point, plane->normal) - plane->dist > 0.005f)
		{
			return qfalse;
		}
	}

	return qtrue;
}

/*
=============
AAS_BSPLeafPointContents

Return the first loaded brush contents containing a local-space point.
=============
*/
static int AAS_BSPLeafPointContents(int leafnum, const vec3_t point)
{
	if (leafnum < 0 || leafnum >= aasworld.numBspLeaves ||
		aasworld.bspLeaves == NULL || aasworld.bspLeafBrushes == NULL ||
		aasworld.bspBrushes == NULL)
	{
		return 0;
	}

	const aas_bspleaf_t *leaf = &aasworld.bspLeaves[leafnum];
	for (int index = 0; index < leaf->numleafbrushes; ++index)
	{
		int leafbrushindex = (int)leaf->firstleafbrush + index;
		if (leafbrushindex < 0 || leafbrushindex >= aasworld.bspLeafBrushIndexSize)
		{
			continue;
		}

		int brushnum = (int)aasworld.bspLeafBrushes[leafbrushindex];
		if (brushnum < 0 || brushnum >= aasworld.numBspBrushes)
		{
			continue;
		}

		const aas_bspbrush_t *brush = &aasworld.bspBrushes[brushnum];
		if (AAS_PointInsideBSPBrush(brush, point))
		{
			return brush->contents;
		}
	}

	return 0;
}

/*
=============
AAS_PointInsideEntityBounds

Check a world-space point against the inclusive bounds kept for one entity.
=============
*/
static qboolean AAS_PointInsideEntityBounds(const aas_entity_t *entity, const vec3_t point)
{
	if (entity == NULL || point == NULL)
	{
		return qfalse;
	}

	for (int axis = 0; axis < 3; ++axis)
	{
		float min = entity->origin[axis] + entity->mins[axis];
		float max = entity->origin[axis] + entity->maxs[axis];
		if (min > max)
		{
			float temporary = min;
			min = max;
			max = temporary;
		}
		if (point[axis] < min || point[axis] > max)
		{
			return qfalse;
		}
	}

	return qtrue;
}

/*
=============
AAS_BSPModelPointContents

Mirror sub_100057a0 for static model brushes and world-leaf linked entities.
=============
*/
int AAS_BSPModelPointContents(const vec3_t point,
                              int modelnum,
                              const vec3_t origin,
                              const vec3_t angles,
                              qboolean includeentities)
{
	if (point == NULL || !AAS_BSPModelValid(modelnum))
	{
		return 0;
	}

	vec3_t totalorigin;
	VectorCopy(aasworld.bspModels[modelnum].origin, totalorigin);
	if (origin != NULL)
	{
		VectorAdd(totalorigin, origin, totalorigin);
	}

	vec3_t localpoint;
	VectorSubtract(point, totalorigin, localpoint);
	if (angles != NULL && !AAS_VectorIsZero(angles))
	{
		vec3_t axis[3];
		vec3_t transformed;
		vec3_t inverseangles;
		VectorNegate(angles, inverseangles);
		AAS_AnglesToAxis(inverseangles, axis);
		VectorCopy(localpoint, transformed);
		/* sub_100057a0 builds -angles, then multiplies the point by that matrix. */
		AAS_RotateLocalVector(transformed, axis, localpoint);
	}

	int leafnum = AAS_BSPModelPointLeafNum(modelnum, localpoint);
	if (leafnum < 0)
	{
		return 0;
	}

	int contents = AAS_BSPLeafPointContents(leafnum, localpoint);
	if (contents != 0 || !includeentities ||
		aasworld.bspLeafEntityLists == NULL ||
		(size_t)leafnum >= aasworld.bspLeafEntityListCount ||
		aasworld.entities == NULL)
	{
		return contents;
	}

	for (bsp_link_t *link = aasworld.bspLeafEntityLists[leafnum];
		link != NULL;
		link = link->next_ent)
	{
		if (link->entnum <= 0 || link->entnum >= aasworld.maxEntities)
		{
			continue;
		}

		const aas_entity_t *entity = &aasworld.entities[link->entnum];
		if (!entity->inuse || !AAS_PointInsideEntityBounds(entity, point))
		{
			continue;
		}

		if (entity->solid == SOLID_BBOX)
		{
			contents |= CONTENTS_MONSTER;
		}
		else if (entity->solid == SOLID_BSP)
		{
			int entitymodel = AAS_ModelNumForEntity(link->entnum);
			contents |= AAS_BSPModelPointContents(point,
			                                      entitymodel,
			                                      entity->origin,
			                                      entity->angles,
			                                      qfalse);
		}
	}

	return contents;
}

/*
=============
AAS_EntityCollision

Trace an axial box against a linked AAS entity.
=============
*/
qboolean AAS_EntityCollision(int entnum,
                             const vec3_t start,
                             const vec3_t boxmins,
                             const vec3_t boxmaxs,
                             const vec3_t end,
                             int contentmask,
                             bsp_trace_t *trace)
{
	if (trace == NULL ||
	    aasworld.entities == NULL ||
	    entnum <= 0 ||
	    entnum >= aasworld.maxEntities)
	{
		return qfalse;
	}

	const aas_entity_t *entity = &aasworld.entities[entnum];
	if (entity->solid == SOLID_BSP)
	{
		int modelnum = AAS_ModelNumForEntity(entnum);
		if (AAS_BSPModelValid(modelnum))
		{
			bsp_trace_t modeltrace = AAS_TraceBSPModel(modelnum,
			                                           entity->angles,
			                                           entity->origin,
			                                           start,
			                                           boxmins,
			                                           boxmaxs,
			                                           end,
			                                           contentmask);
			/*
			 * The SOLID_BSP path is deliberately different from the BBOX
			 * start-inside writer above: sub_10003680 accepts its nested BSP
			 * trace only on a strict fraction improvement.
			 */
			if (modeltrace.fraction < trace->fraction)
			{
				modeltrace.ent = entnum;
				*trace = modeltrace;
				return qtrue;
			}

			return qfalse;
		}
	}

	return AAS_TraceEntityBBox(&aasworld.entities[entnum],
	                           entnum,
	                           start,
	                           boxmins,
	                           boxmaxs,
	                           end,
	                           contentmask,
	                           trace);
}

/*
=============
AAS_SamePoint

Compare two trace points exactly, matching the retail stack checks.
=============
*/
static qboolean AAS_SamePoint(const vec3_t a, const vec3_t b)
{
	return a != NULL &&
	       b != NULL &&
	       a[0] == b[0] &&
	       a[1] == b[1] &&
	       a[2] == b[2];
}

/*
=============
AAS_TraceFacingPlanenum

Return the paired plane when the loaded split normal points with the trace.
=============
*/
static int AAS_TraceFacingPlanenum(int planenum, const vec3_t direction)
{
	if (planenum < 0 || planenum >= aasworld.numPlanes || aasworld.planes == NULL)
	{
		return 0;
	}

	const aas_plane_t *plane = &aasworld.planes[planenum];
	if (direction == NULL || DotProduct(direction, plane->normal) <= 0.0f)
	{
		return planenum;
	}

	return planenum ^ 1;
}

/*
=============
AAS_SetTraceBlocked

Fill an AAS trace result for a solid or presence-blocking hit.
=============
*/
static void AAS_SetTraceBlocked(aas_trace_t *trace,
                                const vec3_t trace_start,
                                const vec3_t trace_end,
                                const vec3_t hit_start,
                                int area,
                                int planenum)
{
	if (trace == NULL)
	{
		return;
	}

	vec3_t direction;
	VectorSubtract(trace_end, trace_start, direction);
	float total_length = AAS_NormalizeVector(direction);

	if (AAS_SamePoint(hit_start, trace_start) || total_length <= 0.0f)
	{
		trace->startsolid = qtrue;
		trace->fraction = 0.0f;
		VectorCopy(trace_start, trace->endpos);
		VectorClear(direction);
	}
	else
	{
		vec3_t delta;
		VectorSubtract(hit_start, trace_start, delta);
		trace->startsolid = qfalse;
		trace->fraction = AAS_VectorLength(delta) / total_length;
		VectorMA(hit_start, -AAS_TRACEPLANE_EPSILON, direction, trace->endpos);
	}

	trace->ent = 0;
	trace->area = area;
	trace->planenum = AAS_TraceFacingPlanenum(planenum, direction);
	if (trace->planenum >= 0 &&
	    trace->planenum < aasworld.numPlanes &&
	    aasworld.planes != NULL)
	{
		const aas_plane_t *plane = &aasworld.planes[trace->planenum];
		VectorCopy(plane->normal, trace->plane.normal);
		trace->plane.dist = plane->dist;
		trace->plane.type = (byte)plane->type;
	}
}

/*
=============
AAS_HasAreaBSPTree

Check whether the loaded AAS has a usable node/plane area tree.
=============
*/
static qboolean AAS_HasAreaBSPTree(void)
{
	return aasworld.nodes != NULL &&
	       aasworld.numNodes > 1 &&
	       aasworld.planes != NULL &&
	       aasworld.numPlanes > 0;
}

/*
=============
AAS_AddUniqueArea

Append an area number if it is not already present in the result buffer.
=============
*/
static qboolean AAS_AddUniqueArea(int areanum, int *areas, int maxareas, int *numareas)
{
	if (areas == NULL || numareas == NULL || maxareas <= 0 || areanum <= 0)
	{
		return qfalse;
	}

	for (int index = 0; index < *numareas; ++index)
	{
		if (areas[index] == areanum)
		{
			return qtrue;
		}
	}

	if (*numareas >= maxareas)
	{
		return qfalse;
	}

	areas[*numareas] = areanum;
	*numareas += 1;
	return qtrue;
}

/*
=============
AAS_BoxOnPlaneSide

Return the sides of a plane touched by an axis-aligned bounding box.
=============
*/
static int AAS_BoxOnPlaneSide(const vec3_t absmins, const vec3_t absmaxs, const aas_plane_t *plane)
{
	vec3_t corners[2];
	for (int axis = 0; axis < 3; ++axis)
	{
		if (plane->normal[axis] < 0.0f)
		{
			corners[0][axis] = absmins[axis];
			corners[1][axis] = absmaxs[axis];
		}
		else
		{
			corners[0][axis] = absmaxs[axis];
			corners[1][axis] = absmins[axis];
		}
	}

	float dist1 = DotProduct(plane->normal, corners[0]) - plane->dist;
	float dist2 = DotProduct(plane->normal, corners[1]) - plane->dist;
	int sides = 0;
	if (dist1 >= 0.0f)
	{
		sides |= 1;
	}
	if (dist2 < 0.0f)
	{
		sides |= 2;
	}

	return sides;
}

/*
=============
AAS_CollectBBoxAreasFromBounds

Fallback bbox-to-area collection for partial test worlds without nodes.
=============
*/
static int AAS_CollectBBoxAreasFromBounds(const vec3_t absmins,
                                          const vec3_t absmaxs,
                                          int *areas,
                                          int maxareas)
{
	int numareas = 0;
	if (aasworld.areas == NULL || aasworld.numAreas <= 0)
	{
		return 0;
	}

	for (int areanum = 1; areanum <= aasworld.numAreas; ++areanum)
	{
		if (!AAS_BoxIntersectsArea(absmins, absmaxs, &aasworld.areas[areanum]))
		{
			continue;
		}

		if (!AAS_AddUniqueArea(areanum, areas, maxareas, &numareas))
		{
			break;
		}
	}

	return numareas;
}

/*
=============
AAS_CollectBBoxAreasFromTree

Collect areas touched by a bbox by walking the loaded AAS node tree.
=============
*/
static int AAS_CollectBBoxAreasFromTree(const vec3_t absmins,
                                        const vec3_t absmaxs,
                                        int *areas,
                                        int maxareas)
{
	aas_bboxarea_stack_t stack[AAS_AREA_STACK_SIZE];
	int stacktop = 0;
	int numareas = 0;

	stack[stacktop++].nodenum = 1;
	while (stacktop > 0)
	{
		int nodenum = stack[--stacktop].nodenum;
		if (nodenum < 0)
		{
			if (!AAS_AddUniqueArea(-nodenum, areas, maxareas, &numareas))
			{
				return numareas;
			}
			continue;
		}

		if (nodenum == 0)
		{
			continue;
		}

		if (nodenum >= aasworld.numNodes)
		{
			BotLib_Print(PRT_ERROR, "AAS_LinkEntity: nodenum out of range\n");
			return numareas;
		}

		const aas_node_t *node = &aasworld.nodes[nodenum];
		if (node->planenum < 0 || node->planenum >= aasworld.numPlanes)
		{
			BotLib_Print(PRT_ERROR, "AAS_LinkEntity: planenum out of range\n");
			return numareas;
		}

		int side = AAS_BoxOnPlaneSide(absmins, absmaxs, &aasworld.planes[node->planenum]);
		if ((side & 2) != 0)
		{
			if (stacktop >= AAS_AREA_STACK_SIZE)
			{
				BotLib_Print(PRT_ERROR, "AAS_LinkEntity: stack overflow\n");
				return numareas;
			}
			stack[stacktop++].nodenum = node->children[1];
		}
		if ((side & 1) != 0)
		{
			if (stacktop >= AAS_AREA_STACK_SIZE)
			{
				BotLib_Print(PRT_ERROR, "AAS_LinkEntity: stack overflow\n");
				return numareas;
			}
			stack[stacktop++].nodenum = node->children[0];
		}
	}

	return numareas;
}

/*
=============
AAS_BBoxAreas

Return the AAS areas touched by an absolute bounding box.
=============
*/
int AAS_BBoxAreas(const vec3_t absmins, const vec3_t absmaxs, int *areas, int maxareas)
{
	if (areas != NULL && maxareas > 0)
	{
		areas[0] = 0;
	}

	if (!aasworld.loaded ||
	    absmins == NULL ||
	    absmaxs == NULL ||
	    areas == NULL ||
	    maxareas <= 0)
	{
		return 0;
	}

	vec3_t mins;
	vec3_t maxs;
	VectorCopy(absmins, mins);
	VectorCopy(absmaxs, maxs);
	AAS_ClampMinsMaxs(mins, maxs);

	if (!AAS_HasAreaBSPTree())
	{
		return AAS_CollectBBoxAreasFromBounds(mins, maxs, areas, maxareas);
	}

	return AAS_CollectBBoxAreasFromTree(mins, maxs, areas, maxareas);
}

/*
=============
AAS_PushClientTraceSegment

Push a segment within retail's 64-entry geometry, guarding its unchecked edge.
=============
*/
static qboolean AAS_PushClientTraceSegment(aas_clienttrace_stack_t *stack,
                                           int *stacktop,
                                           int nodenum,
                                           int planenum,
                                           const vec3_t start,
                                           const vec3_t end)
{
	if (stack == NULL ||
		stacktop == NULL ||
		*stacktop >= AAS_CLIENT_TRACE_STACK_SIZE)
	{
		/* Retail has no overflow branch; keep its geometry without corruption. */
		BotLib_Print(PRT_ERROR, "AAS_TraceBoundingBox: stack overflow\n");
		return qfalse;
	}

	aas_clienttrace_stack_t *entry = &stack[*stacktop];
	VectorCopy(start, entry->start);
	VectorCopy(end, entry->end);
	entry->nodenum = nodenum;
	entry->planenum = planenum;
	*stacktop += 1;
	return qtrue;
}

/*
=============
AAS_AreaEntityCollision

Trace a client bbox segment against entities linked to an AAS area.
=============
*/
static qboolean AAS_AreaEntityCollision(int areanum,
                                        const vec3_t start,
                                        const vec3_t end,
                                        int presencetype,
                                        int passent,
                                        aas_trace_t *trace)
{
	if (trace == NULL ||
	    areanum <= 0 ||
	    aasworld.areaEntityLists == NULL ||
	    (size_t)areanum >= aasworld.areaEntityListCount)
	{
		return qfalse;
	}

	vec3_t boxmins;
	vec3_t boxmaxs;
	AAS_PresenceTypeBoundingBox(presencetype, boxmins, boxmaxs);

	bsp_trace_t bsptrace;
	memset(&bsptrace, 0, sizeof(bsptrace));
	bsptrace.fraction = 1.0f;

	qboolean collision = qfalse;
	for (aas_link_t *link = aasworld.areaEntityLists[areanum]; link != NULL; link = link->next_ent)
	{
		if (link->entnum == passent)
		{
			continue;
		}
		if (AAS_EntityCollision(link->entnum,
		                        start,
		                        boxmins,
		                        boxmaxs,
		                        end,
		                        CONTENTS_SOLID | CONTENTS_PLAYERCLIP,
		                        &bsptrace))
		{
			collision = qtrue;
		}
	}

	if (!collision)
	{
		return qfalse;
	}

	trace->startsolid = bsptrace.startsolid;
	trace->ent = bsptrace.ent;
	VectorCopy(bsptrace.endpos, trace->endpos);
	trace->area = 0;
	trace->planenum = 0;
	trace->plane = bsptrace.plane;
	return qtrue;
}

/*
=============
AAS_TraceClientBBoxFallback

Fallback client-bbox trace for partial test worlds without nodes.
=============
*/
static aas_trace_t AAS_TraceClientBBoxFallback(const vec3_t start,
                                               const vec3_t end,
                                               int presencetype)
{
	aas_trace_t trace;
	memset(&trace, 0, sizeof(trace));

	int startarea = AAS_PointAreaNum(start);
	if (startarea > 0 && !AAS_AreaAllowsPresence(startarea, presencetype))
	{
		trace.startsolid = qtrue;
		trace.fraction = 0.0f;
		VectorCopy(start, trace.endpos);
		trace.area = startarea;
		trace.lastarea = 0;
		return trace;
	}

	trace.startsolid = qfalse;
	trace.fraction = 1.0f;
	VectorCopy(end, trace.endpos);
	trace.ent = 0;
	trace.area = 0;
	trace.lastarea = AAS_PointAreaNum(end);
	if (trace.lastarea <= 0)
	{
		trace.lastarea = startarea;
	}
	trace.planenum = 0;
	return trace;
}

/*
=============
AAS_TraceClientBBox

Trace a client presence box through the loaded AAS area BSP tree.
=============
*/
aas_trace_t AAS_TraceClientBBox(const vec3_t start, const vec3_t end, int presencetype, int passent)
{
	aas_trace_t trace;
	memset(&trace, 0, sizeof(trace));

	if (!aasworld.loaded || start == NULL || end == NULL)
	{
		return trace;
	}

	if (!AAS_HasAreaBSPTree())
	{
		return AAS_TraceClientBBoxFallback(start, end, presencetype);
	}

	aas_clienttrace_stack_t stack[AAS_CLIENT_TRACE_STACK_SIZE];
	int stacktop = 0;
	if (!AAS_PushClientTraceSegment(stack, &stacktop, 1, 0, start, end))
	{
		return trace;
	}

	while (stacktop > 0)
	{
		aas_clienttrace_stack_t current = stack[--stacktop];
		int nodenum = current.nodenum;
		if (nodenum < 0)
		{
			int areanum = -nodenum;
			if (!AAS_AreaAllowsPresence(areanum, presencetype))
			{
				AAS_SetTraceBlocked(&trace,
				                    start,
				                    end,
				                    current.start,
				                    areanum,
				                    current.planenum);
				return trace;
			}

			if (passent >= 0 &&
			    AAS_AreaEntityCollision(areanum,
			                            current.start,
			                            current.end,
			                            presencetype,
			                            passent,
			                            &trace))
			{
				if (!trace.startsolid)
				{
					vec3_t full_delta;
					vec3_t hit_delta;
					VectorSubtract(end, start, full_delta);
					VectorSubtract(trace.endpos, start, hit_delta);
					float full_length = AAS_VectorLength(full_delta);
					if (full_length > 0.0f)
					{
						trace.fraction = AAS_VectorLength(hit_delta) / full_length;
					}
				}
				return trace;
			}
			trace.lastarea = areanum;
			continue;
		}

		if (nodenum == 0)
		{
			AAS_SetTraceBlocked(&trace,
			                    start,
			                    end,
			                    current.start,
			                    0,
			                    current.planenum);
			return trace;
		}

		if (nodenum >= aasworld.numNodes)
		{
			BotLib_Print(PRT_ERROR, "AAS_TraceBoundingBox: nodenum out of range\n");
			return trace;
		}

		const aas_node_t *node = &aasworld.nodes[nodenum];
		if (node->planenum < 0 || node->planenum >= aasworld.numPlanes)
		{
			BotLib_Print(PRT_ERROR, "AAS_TraceBoundingBox: planenum out of range\n");
			return trace;
		}

		const aas_plane_t *plane = &aasworld.planes[node->planenum];
		float front = DotProduct(current.start, plane->normal) - plane->dist;
		float back = DotProduct(current.end, plane->normal) - plane->dist;
		if (front > -AAS_TRACE_ON_EPSILON && back > -AAS_TRACE_ON_EPSILON)
		{
			if (!AAS_PushClientTraceSegment(stack,
			                                &stacktop,
			                                node->children[0],
			                                current.planenum,
			                                current.start,
			                                current.end))
			{
				return trace;
			}
			continue;
		}

		if (front < AAS_TRACE_ON_EPSILON && back < AAS_TRACE_ON_EPSILON)
		{
			if (!AAS_PushClientTraceSegment(stack,
			                                &stacktop,
			                                node->children[1],
			                                current.planenum,
			                                current.start,
			                                current.end))
			{
				return trace;
			}
			continue;
		}

		float frac = (front < 0.0f)
		                 ? (front + AAS_TRACEPLANE_EPSILON) / (front - back)
		                 : (front - AAS_TRACEPLANE_EPSILON) / (front - back);
		if (frac < 0.0f)
		{
			frac = 0.0f;
		}
		else if (frac > 1.0f)
		{
			frac = 1.0f;
		}

		vec3_t middle;
		for (int axis = 0; axis < 3; ++axis)
		{
			middle[axis] = current.start[axis] + (current.end[axis] - current.start[axis]) * frac;
		}

		int startside = (front < 0.0f) ? 1 : 0;
		if (!AAS_PushClientTraceSegment(stack,
		                                &stacktop,
		                                node->children[!startside],
		                                node->planenum,
		                                middle,
		                                current.end))
		{
			return trace;
		}
		if (!AAS_PushClientTraceSegment(stack,
		                                &stacktop,
		                                node->children[startside],
		                                current.planenum,
		                                current.start,
		                                middle))
		{
			return trace;
		}
	}

	trace.startsolid = qfalse;
	trace.fraction = 1.0f;
	VectorCopy(end, trace.endpos);
	trace.ent = 0;
	trace.area = 0;
	trace.planenum = 0;
	return trace;
}

/*
=============
AAS_PushTraceAreaSegment

Push a trace-area node segment onto the traversal stack.
=============
*/
static qboolean AAS_PushTraceAreaSegment(aas_tracearea_stack_t *stack,
                                         int *stacktop,
                                         int nodenum,
                                         const vec3_t start,
                                         const vec3_t end)
{
	if (stack == NULL || stacktop == NULL || *stacktop >= AAS_AREA_STACK_SIZE)
	{
		BotLib_Print(PRT_ERROR, "AAS_TraceAreas: stack overflow\n");
		return qfalse;
	}

	aas_tracearea_stack_t *entry = &stack[*stacktop];
	VectorCopy(start, entry->start);
	VectorCopy(end, entry->end);
	entry->nodenum = nodenum;
	*stacktop += 1;
	return qtrue;
}

/*
=============
AAS_TraceAreasFallback

Fallback line area sampling for partial test worlds without nodes.
=============
*/
static int AAS_TraceAreasFallback(const vec3_t start,
                                  const vec3_t end,
                                  int *areas,
                                  vec3_t *points,
                                  int maxareas)
{
	int numareas = 0;
	int startarea = AAS_PointAreaNum(start);
	if (startarea > 0 && AAS_AddUniqueArea(startarea, areas, maxareas, &numareas))
	{
		if (points != NULL)
		{
			VectorCopy(start, points[numareas - 1]);
		}
	}

	if (numareas >= maxareas)
	{
		return numareas;
	}

	int endarea = AAS_PointAreaNum(end);
	if (endarea > 0 && AAS_AddUniqueArea(endarea, areas, maxareas, &numareas))
	{
		if (points != NULL)
		{
			VectorCopy(end, points[numareas - 1]);
		}
	}

	return numareas;
}

/*
=============
AAS_TraceAreas

Store the areas passed through by a line segment.
=============
*/
int AAS_TraceAreas(const vec3_t start, const vec3_t end, int *areas, vec3_t *points, int maxareas)
{
	if (areas != NULL && maxareas > 0)
	{
		areas[0] = 0;
	}

	if (!aasworld.loaded ||
	    start == NULL ||
	    end == NULL ||
	    areas == NULL ||
	    maxareas <= 0)
	{
		return 0;
	}

	if (!AAS_HasAreaBSPTree())
	{
		return AAS_TraceAreasFallback(start, end, areas, points, maxareas);
	}

	aas_tracearea_stack_t stack[AAS_AREA_STACK_SIZE];
	int stacktop = 0;
	int numareas = 0;
	if (!AAS_PushTraceAreaSegment(stack, &stacktop, 1, start, end))
	{
		return 0;
	}

	while (stacktop > 0)
	{
		aas_tracearea_stack_t current = stack[--stacktop];
		int nodenum = current.nodenum;
		if (nodenum < 0)
		{
			areas[numareas] = -nodenum;
			if (points != NULL)
			{
				VectorCopy(current.start, points[numareas]);
			}
			++numareas;
			if (numareas >= maxareas)
			{
				return numareas;
			}
			continue;
		}

		if (nodenum == 0)
		{
			continue;
		}

		if (nodenum >= aasworld.numNodes)
		{
			BotLib_Print(PRT_ERROR, "AAS_TraceAreas: nodenum out of range\n");
			return numareas;
		}

		const aas_node_t *node = &aasworld.nodes[nodenum];
		if (node->planenum < 0 || node->planenum >= aasworld.numPlanes)
		{
			BotLib_Print(PRT_ERROR, "AAS_TraceAreas: planenum out of range\n");
			return numareas;
		}

		const aas_plane_t *plane = &aasworld.planes[node->planenum];
		float front = DotProduct(current.start, plane->normal) - plane->dist;
		float back = DotProduct(current.end, plane->normal) - plane->dist;
		if (front > 0.0f && back > 0.0f)
		{
			if (!AAS_PushTraceAreaSegment(stack, &stacktop, node->children[0], current.start, current.end))
			{
				return numareas;
			}
			continue;
		}

		if (front <= 0.0f && back <= 0.0f)
		{
			if (!AAS_PushTraceAreaSegment(stack, &stacktop, node->children[1], current.start, current.end))
			{
				return numareas;
			}
			continue;
		}

		float frac = front / (front - back);
		if (frac < 0.0f)
		{
			frac = 0.0f;
		}
		else if (frac > 1.0f)
		{
			frac = 1.0f;
		}

		vec3_t middle;
		for (int axis = 0; axis < 3; ++axis)
		{
			middle[axis] = current.start[axis] + (current.end[axis] - current.start[axis]) * frac;
		}

		int startside = (front < 0.0f) ? 1 : 0;
		if (!AAS_PushTraceAreaSegment(stack, &stacktop, node->children[!startside], middle, current.end))
		{
			return numareas;
		}
		if (!AAS_PushTraceAreaSegment(stack, &stacktop, node->children[startside], current.start, middle))
		{
			return numareas;
		}
	}

	return numareas;
}

/*
=============
AAS_PlaneFromNum

Return a loaded AAS split plane by index.
=============
*/
aas_plane_t *AAS_PlaneFromNum(int planenum)
{
	if (aasworld.planes == NULL || planenum < 0 || planenum >= aasworld.numPlanes)
	{
		return NULL;
	}

	return &aasworld.planes[planenum];
}

/*
=============
AAS_TraceEndFace

Return the face containing a trace end position in the last traversed area.
=============
*/
aas_face_t *AAS_TraceEndFace(const aas_trace_t *trace)
{
	if (!aasworld.loaded ||
	    trace == NULL ||
	    trace->startsolid ||
	    trace->lastarea <= 0 ||
	    trace->lastarea > aasworld.numAreas ||
	    aasworld.areas == NULL ||
	    aasworld.faces == NULL ||
	    aasworld.faceIndex == NULL)
	{
		return NULL;
	}

	const aas_area_t *area = &aasworld.areas[trace->lastarea];
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
		if ((face->planenum & ~1) != (trace->planenum & ~1))
		{
			continue;
		}

		if (AAS_PointInsideFace(facenum, trace->endpos, 0.01f))
		{
			return face;
		}
	}

	return NULL;
}

/*
=============
AAS_FreeIndexList

Release one copied retail asset-index table.
=============
*/
static void AAS_FreeIndexList(aas_indexlist_t *list)
{
	if (list == NULL)
	{
		return;
	}

	for (int index = 0; index < list->numindexes; ++index)
	{
		if (list->indexes[index] != NULL)
		{
			FreeMemory(list->indexes[index]);
		}
	}
	FreeMemory(list);
}

/*
=============
AAS_CreateIndexList

Copy an engine asset-index table into botlib-owned storage.
=============
*/
static aas_indexlist_t *AAS_CreateIndexList(int numindexes, char *names[])
{
	if (numindexes < 0 ||
		(size_t)numindexes > (SIZE_MAX - sizeof(aas_indexlist_t)) / sizeof(char *))
	{
		return NULL;
	}

	size_t allocation = sizeof(aas_indexlist_t) +
		(size_t)numindexes * sizeof(char *);
	aas_indexlist_t *list = GetClearedMemory(allocation);
	if (list == NULL)
	{
		return NULL;
	}

	list->numindexes = numindexes;
	list->indexes = (char **)(list + 1);
	for (int index = 0; index < numindexes; ++index)
	{
		const char *name = names != NULL ? names[index] : NULL;
		if (name == NULL)
		{
			continue;
		}

		size_t length = strlen(name) + 1U;
		list->indexes[index] = GetMemory(length);
		if (list->indexes[index] == NULL)
		{
			AAS_FreeIndexList(list);
			return NULL;
		}
		memcpy(list->indexes[index], name, length);
	}
	return list;
}

/*
=============
AAS_ClearIndexTables

Release the three AAS-owned retail asset-index tables.
=============
*/
static void AAS_ClearIndexTables(void)
{
	AAS_FreeIndexList(g_aasModelIndexes);
	AAS_FreeIndexList(g_aasSoundIndexes);
	AAS_FreeIndexList(g_aasImageIndexes);
	g_aasModelIndexes = NULL;
	g_aasSoundIndexes = NULL;
	g_aasImageIndexes = NULL;
	g_aasIndexesLoaded = qfalse;
}

/*
=============
AAS_ReplaceIndexTables

Replace all retail asset-index tables for a named map load.
=============
*/
static qboolean AAS_ReplaceIndexTables(int modelindexes,
	char *modelindex[],
	int soundindexes,
	char *soundindex[],
	int imageindexes,
	char *imageindex[])
{
	AAS_ClearIndexTables();
	g_aasModelIndexes = AAS_CreateIndexList(modelindexes, modelindex);
	g_aasSoundIndexes = AAS_CreateIndexList(soundindexes, soundindex);
	g_aasImageIndexes = AAS_CreateIndexList(imageindexes, imageindex);
	if (g_aasModelIndexes == NULL || g_aasSoundIndexes == NULL ||
		g_aasImageIndexes == NULL)
	{
		AAS_ClearIndexTables();
		return qfalse;
	}
	g_aasIndexesLoaded = qtrue;
	return qtrue;
}

/*
=============
AAS_RefreshIndexList

Fill previously unused entries during a NULL-map asset refresh.
=============
*/
static qboolean AAS_RefreshIndexList(aas_indexlist_t **destination,
	int numindexes,
	char *names[])
{
	if (destination == NULL || numindexes < 0)
	{
		return qfalse;
	}
	if (*destination == NULL || numindexes > (*destination)->numindexes)
	{
		aas_indexlist_t *replacement = AAS_CreateIndexList(numindexes, names);
		if (replacement == NULL)
		{
			return qfalse;
		}
		AAS_FreeIndexList(*destination);
		*destination = replacement;
		return qtrue;
	}

	for (int index = 0; index < numindexes; ++index)
	{
		const char *name = names != NULL ? names[index] : NULL;
		if ((*destination)->indexes[index] != NULL || name == NULL)
		{
			continue;
		}

		size_t length = strlen(name) + 1U;
		char *copy = GetMemory(length);
		if (copy == NULL)
		{
			return qfalse;
		}
		memcpy(copy, name, length);
		(*destination)->indexes[index] = copy;
	}
	return qtrue;
}

/*
=============
AAS_RefreshIndexTables

Apply retail's fill-only update for late engine asset registrations.
=============
*/
static qboolean AAS_RefreshIndexTables(int modelindexes,
	char *modelindex[],
	int soundindexes,
	char *soundindex[],
	int imageindexes,
	char *imageindex[])
{
	if (!AAS_RefreshIndexList(&g_aasModelIndexes,
		modelindexes,
		modelindex) ||
		!AAS_RefreshIndexList(&g_aasSoundIndexes,
			soundindexes,
			soundindex) ||
		!AAS_RefreshIndexList(&g_aasImageIndexes,
			imageindexes,
			imageindex))
	{
		return qfalse;
	}
	g_aasIndexesLoaded = qtrue;
	return qtrue;
}

/*
=============
AAS_StringFromIndex

Resolve a retail asset name while preserving index-zero empty semantics.
=============
*/
static const char *AAS_StringFromIndex(const char *indexname,
	const aas_indexlist_t *list,
	int index)
{
	static const char empty[] = "";
	if (!g_aasIndexesLoaded)
	{
		BotLib_Print(PRT_ERROR, "%s: index %d not setup\n", indexname, index);
		return empty;
	}
	if (list == NULL || index < 0 || index >= list->numindexes)
	{
		BotLib_Print(PRT_ERROR, "%s: index %d out of range\n", indexname, index);
		return empty;
	}
	if (list->indexes[index] != NULL)
	{
		return list->indexes[index];
	}
	if (index != 0)
	{
		BotLib_Print(PRT_ERROR,
			"%s: reference to unused index %d\n",
			indexname,
			index);
	}
	return empty;
}

/*
=============
AAS_IndexFromString

Find a copied retail asset name using the original case-insensitive match.
=============
*/
static int AAS_IndexFromString(const char *indexname,
	const aas_indexlist_t *list,
	const char *name)
{
	if (!g_aasIndexesLoaded)
	{
		BotLib_Print(PRT_ERROR,
			"%s: index not setup \"%s\"\n",
			indexname,
			name != NULL ? name : "");
		return 0;
	}
	if (list == NULL || name == NULL)
	{
		return 0;
	}
	for (int index = 0; index < list->numindexes; ++index)
	{
		if (list->indexes[index] != NULL &&
			Q_stricmp(list->indexes[index], name) == 0)
		{
			return index;
		}
	}
	return 0;
}

/*
=============
AAS_ModelFromIndex

Resolve a model name from the AAS-owned retail index table.
=============
*/
const char *AAS_ModelFromIndex(int index)
{
	return AAS_StringFromIndex("ModelFromIndex", g_aasModelIndexes, index);
}

/*
=============
IndexFromModel

Resolve a model index from the AAS-owned retail index table.
=============
*/
int IndexFromModel(const char *model)
{
	return AAS_IndexFromString("IndexFromModel", g_aasModelIndexes, model);
}

/*
=============
AAS_SoundFromIndex

Resolve a sound name from the AAS-owned retail index table.
=============
*/
const char *AAS_SoundFromIndex(int index)
{
	return AAS_StringFromIndex("SoundFromIndex", g_aasSoundIndexes, index);
}

/*
=============
AAS_IndexFromSound

Resolve a sound index from the AAS-owned retail index table.
=============
*/
int AAS_IndexFromSound(const char *sound)
{
	return AAS_IndexFromString("IndexFromSound", g_aasSoundIndexes, sound);
}

/*
=============
AAS_ImageFromIndex

Resolve an image name from the AAS-owned retail index table.
=============
*/
const char *AAS_ImageFromIndex(int index)
{
	return AAS_StringFromIndex("ImageFromIndex", g_aasImageIndexes, index);
}

/*
=============
AAS_IndexFromImage

Resolve an image index from the AAS-owned retail index table.
=============
*/
int AAS_IndexFromImage(const char *image)
{
	return AAS_IndexFromString("IndexFromImage", g_aasImageIndexes, image);
}

/*
=============
AAS_Init

Initialize the retained AAS world.
=============
*/
int AAS_Init(void)
{
    if (g_aasLibraryInitialized)
    {
        return BLERR_NOERROR;
    }

    AAS_ClearWorld();
    aasworld.initialized = qtrue;
    g_aasLibraryInitialized = qtrue;
	BotLib_Print(PRT_MESSAGE, "AAS initialized.\n");
    return BLERR_NOERROR;
}

static int32_t AAS_LittleLong(int32_t value)
{
#if defined(_WIN32) || \
	(defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__))
    return value;
#else
    uint32_t u = (uint32_t)value;
    return (int32_t)((u >> 24)
                     | ((u >> 8) & 0x0000FF00U)
                     | ((u << 8) & 0x00FF0000U)
                     | (u << 24));
#endif
}

static uint32_t AAS_LittleUnsigned(uint32_t value)
{
#if defined(_WIN32) || \
	(defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__))
    return value;
#else
    return (uint32_t)AAS_LittleLong((int32_t)value);
#endif
}

static uint16_t AAS_LittleShort(uint16_t value)
{
#if defined(_WIN32) || \
	(defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__))
    return value;
#else
    return (uint16_t)((value >> 8) | (value << 8));
#endif
}

static float AAS_LittleFloat(float value)
{
#if defined(_WIN32) || \
	(defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__))
    return value;
#else
    union
    {
        float f;
        uint32_t u;
    } swapper;
    swapper.f = value;
    swapper.u = AAS_LittleUnsigned(swapper.u);
    return swapper.f;
#endif
}

/*
=============
AAS_BSPPointLeafNumber

Find the Quake II BSP leaf containing a point for visibility queries.
=============
*/
static int AAS_BSPPointLeafNumber(const vec3_t point)
{
	if (point == NULL || aasworld.bspNodes == NULL ||
		aasworld.bspPlanes == NULL || aasworld.bspLeaves == NULL)
	{
		return -1;
	}

	int nodenum = 0;
	while (nodenum >= 0)
	{
		if (nodenum >= aasworld.numBspNodes)
		{
			return -1;
		}
		const aas_bspnode_t *node = &aasworld.bspNodes[nodenum];
		if (node->planenum < 0 || node->planenum >= aasworld.numBspPlanes)
		{
			return -1;
		}
		const aas_plane_t *plane = &aasworld.bspPlanes[node->planenum];
		float distance = DotProduct(point, plane->normal) - plane->dist;
		nodenum = node->children[distance < 0.0f ? 1 : 0];
	}

	int leafnum = -1 - nodenum;
	return leafnum >= 0 && leafnum < aasworld.numBspLeaves
		? leafnum
		: -1;
}

/*
=============
AAS_BSPVisibilityOffset

Read one host-endian Quake II dvis offset from the retained visibility lump.
=============
*/
static qboolean AAS_BSPVisibilityOffset(int cluster,
	int type,
	int *out_offset)
{
	if (out_offset == NULL || cluster < 0 ||
		cluster >= aasworld.numBspVisibilityClusters ||
		type < 0 || type > 1 || aasworld.bspVisibility == NULL)
	{
		return qfalse;
	}

	size_t offset = sizeof(int32_t) +
		((size_t)cluster * 2U + (size_t)type) * sizeof(int32_t);
	if (offset > aasworld.bspVisibilitySize ||
		aasworld.bspVisibilitySize - offset < sizeof(int32_t))
	{
		return qfalse;
	}
	int32_t value;
	memcpy(&value, aasworld.bspVisibility + offset, sizeof(value));
	*out_offset = value;
	return qtrue;
}

/*
=============
AAS_InPVS

Test the Quake II compressed PVS row for the BSP leaves containing two points.
=============
*/
qboolean AAS_InPVS(const vec3_t point1, const vec3_t point2)
{
	if (aasworld.bspVisibility == NULL ||
		aasworld.bspVisibilitySize == 0U ||
		aasworld.bspNodes == NULL)
	{
		return qtrue;
	}

	int leaf1 = AAS_BSPPointLeafNumber(point1);
	int leaf2 = AAS_BSPPointLeafNumber(point2);
	if (leaf1 < 0 || leaf2 < 0)
	{
		return qfalse;
	}
	int cluster1 = aasworld.bspLeaves[leaf1].cluster;
	int cluster2 = aasworld.bspLeaves[leaf2].cluster;
	if (cluster1 < 0 || cluster2 < 0 ||
		cluster1 >= aasworld.numBspVisibilityClusters ||
		cluster2 >= aasworld.numBspVisibilityClusters)
	{
		return qfalse;
	}

	int input_offset;
	if (!AAS_BSPVisibilityOffset(cluster1, 0, &input_offset))
	{
		return qfalse;
	}
	if (input_offset < 0)
	{
		return qtrue;
	}

	size_t input = (size_t)input_offset;
	size_t output = 0U;
	size_t target_byte = (size_t)cluster2 >> 3;
	size_t row_bytes = ((size_t)aasworld.numBspVisibilityClusters + 7U) >> 3;
	while (output < row_bytes && input < aasworld.bspVisibilitySize)
	{
		unsigned char value = aasworld.bspVisibility[input++];
		if (value != 0U)
		{
			if (output == target_byte)
			{
				return (value & (1U << ((unsigned int)cluster2 & 7U))) != 0U;
			}
			output += 1U;
			continue;
		}

		if (input >= aasworld.bspVisibilitySize)
		{
			return qfalse;
		}
		unsigned char repeat = aasworld.bspVisibility[input++];
		if (repeat == 0U)
		{
			BotLib_Print(PRT_ERROR, "AAS_DecompressVis: 0 repeat\n");
			return qfalse;
		}
		if (target_byte >= output &&
			target_byte < output + (size_t)repeat)
		{
			return qfalse;
		}
		output += (size_t)repeat;
	}
	return qfalse;
}

#define AAS_VISIBILITY_CONTENTS 0x02030003
#define AAS_VISIBILITY_FLUIDS 0x00000038
#define AAS_VISIBILITY_TRANSLUCENT_SURFACES 0x00000030

/*
=============
AAS_InFieldOfVision

Quantize retail pitch and yaw to the 16-bit angle grid and apply inclusive
half-FOV bounds using the shortest signed difference.
=============
*/
static qboolean AAS_InFieldOfVision(const vec3_t viewangles,
	float fieldofview,
	vec3_t targetangles)
{
	for (int axis = 0; axis < 2; ++axis)
	{
		float viewangle = AngleMod(viewangles[axis]);
		float targetangle = AngleMod(targetangles[axis]);
		targetangles[axis] = targetangle;

		float difference = targetangle - viewangle;
		if (targetangle < viewangle)
		{
			if (difference < -180.0f)
			{
				difference += 360.0f;
			}
		}
		else if (difference > 180.0f)
		{
			difference -= 360.0f;
		}

		if (difference < 0.0f)
		{
			if (difference < fieldofview * -0.5f)
			{
				return qfalse;
			}
		}
		else if (difference > fieldofview * 0.5f)
		{
			return qfalse;
		}
	}

	return qtrue;
}

/*
=============
AAS_EntityVisible

Test the target midpoint, bottom, and top in retail FOV/PVS/trace order,
including liquid-boundary reversal and translucent-liquid continuation.
=============
*/
int AAS_EntityVisible(int viewer,
	const vec3_t eye,
	const vec3_t viewangles,
	float fieldofview,
	int entnum)
{
	const aas_entity_t *entity = &aasworld.entities[entnum];
	vec3_t sample;
	for (int axis = 0; axis < 3; ++axis)
	{
		sample[axis] = (entity->mins[axis] + entity->maxs[axis]) *
			0.5f + entity->origin[axis];
	}

	vec3_t direction;
	vec3_t targetangles;
	VectorSubtract(sample, eye, direction);
	Vector2Angles(direction, targetangles);
	if (!AAS_InFieldOfVision(viewangles, fieldofview, targetangles))
	{
		return qfalse;
	}

	for (int sampleindex = 0; sampleindex < 3; ++sampleindex)
	{
		if (AAS_InPVS(eye, sample))
		{
			int contentmask = AAS_VISIBILITY_CONTENTS;
			int passent = viewer;
			int hitent = entnum;
			vec3_t start;
			vec3_t end;
			VectorCopy(eye, start);
			VectorCopy(sample, end);

			if ((Q2_PointContents(sample) & AAS_VISIBILITY_FLUIDS) != 0)
			{
				contentmask |= AAS_VISIBILITY_FLUIDS;
			}
			if ((Q2_PointContents((vec_t *)eye) & AAS_VISIBILITY_FLUIDS) != 0)
			{
				if ((contentmask & AAS_VISIBILITY_FLUIDS) == 0)
				{
					VectorCopy(sample, start);
					VectorCopy(eye, end);
					passent = entnum;
					hitent = viewer;
				}
				contentmask ^= AAS_VISIBILITY_FLUIDS;
			}

			bsp_trace_t trace = AAS_Trace(start,
				NULL,
				NULL,
				end,
				passent,
				contentmask);
			if ((trace.contents & AAS_VISIBILITY_FLUIDS) != 0 &&
				(trace.surface.flags &
					AAS_VISIBILITY_TRANSLUCENT_SURFACES) != 0)
			{
				contentmask &= ~AAS_VISIBILITY_FLUIDS;
				trace = AAS_Trace(trace.endpos,
					NULL,
					NULL,
					end,
					passent,
					contentmask);
			}
			if (trace.fraction >= 1.0f || trace.ent == hitent)
			{
				return qtrue;
			}

			/*
			 * Retail advances the sample point INSIDE the PVS-gated block:
			 * the z-adjust at 1000b98d-1000b9af sits at the same HLIL
			 * indentation as the trace and the success return, while only the
			 * loop counter update at 1000b9b8 is one level out. So when the
			 * PVS test fails, retail re-tests the same bbox midpoint on
			 * iterations 1 and 2 and never reaches the feet/head samples.
			 */
			if (sampleindex == 0)
			{
				sample[2] += entity->mins[2];
			}
			else if (sampleindex == 1)
			{
				sample[2] += entity->maxs[2] - entity->mins[2];
			}
		}
	}

	return qfalse;
}

/*
=============
AAS_VisibleEntities

Enumerate in-use client entities in ascending numeric order through the
inclusive setup-time maxclients slot and stop at the caller's result cap.
=============
*/
int AAS_VisibleEntities(int viewer,
	const vec3_t eye,
	const vec3_t viewangles,
	float fieldofview,
	int maxentities,
	int *entitynums)
{
	int count = 0;
	for (int entnum = 1; entnum <= aasworld.maxClients; ++entnum)
	{
		if (aasworld.entities[entnum].inuse &&
			AAS_EntityVisible(viewer,
				eye,
				viewangles,
				fieldofview,
				entnum))
		{
			entitynums[count++] = entnum;
			if (count >= maxentities)
			{
				break;
			}
		}
	}
	return count;
}

/*
=============
AAS_NextEntity

Return the next in-use entity slot, with negative cursors beginning at zero.
=============
*/
int AAS_NextEntity(int entnum)
{
	if (!aasworld.loaded)
	{
		return 0;
	}
	if (entnum < 0)
	{
		entnum = -1;
	}

	for (int next = entnum + 1; next < aasworld.maxEntities; ++next)
	{
		if (aasworld.entities[next].inuse)
		{
			return next;
		}
	}
	return 0;
}

typedef struct aas_parsed_entity_s
{
    char classname[64];
    qboolean hasClassname;
    char model[64];
    qboolean hasModel;
    float lip;
    qboolean hasLip;
    float height;
    qboolean hasHeight;
    float speed;
    qboolean hasSpeed;
    int spawnflags;
    qboolean hasSpawnflags;
} aas_parsed_entity_t;

enum
{
    AAS_MOVER_DOORTYPE_NONE = 0,
    AAS_MOVER_DOORTYPE_STANDARD = 1,
    AAS_MOVER_DOORTYPE_ROTATING = 2,
    AAS_MOVER_DOORTYPE_SECRET = 3
};

static qboolean AAS_ParseFloatValue(const char *value, float *outValue);
static qboolean AAS_ParseIntValue(const char *value, int *outValue);
static void AAS_SkipEntityWhitespace(const char **cursor, const char *end);
static qboolean AAS_ParseQuotedToken(const char **cursor, const char *end, char **outToken);
static void AAS_SkipMalformedEntity(const char **cursor, const char *end);
static void AAS_ParseEntityKeyValue(aas_parsed_entity_t *entity, const char *key, const char *value);
static void AAS_RegisterMoverEntity(const aas_parsed_entity_t *entity);

static qboolean AAS_ParseFloatValue(const char *value, float *outValue)
{
    if (value == NULL || outValue == NULL)
    {
        return qfalse;
    }

    errno = 0;
    char *endPtr = NULL;
    float parsed = strtof(value, &endPtr);
    if (endPtr == value || errno == ERANGE)
    {
        return qfalse;
    }

    while (endPtr != NULL && *endPtr != '\0' && isspace((unsigned char)*endPtr))
    {
        ++endPtr;
    }

    if (endPtr != NULL && *endPtr != '\0')
    {
        return qfalse;
    }

    *outValue = parsed;
    return qtrue;
}

static qboolean AAS_ParseIntValue(const char *value, int *outValue)
{
    if (value == NULL || outValue == NULL)
    {
        return qfalse;
    }

    errno = 0;
    char *endPtr = NULL;
    long parsed = strtol(value, &endPtr, 10);
    if (endPtr == value || errno == ERANGE)
    {
        return qfalse;
    }

    while (endPtr != NULL && *endPtr != '\0' && isspace((unsigned char)*endPtr))
    {
        ++endPtr;
    }

    if (endPtr != NULL && *endPtr != '\0')
    {
        return qfalse;
    }

    if (parsed > INT_MAX || parsed < INT_MIN)
    {
        return qfalse;
    }

    *outValue = (int)parsed;
    return qtrue;
}

static void AAS_CopyStringField(char *destination,
                                size_t destinationSize,
                                const char *source,
                                const char *fieldName)
{
    if (destination == NULL || destinationSize == 0U || source == NULL)
    {
        return;
    }

    size_t length = strlen(source);
    if (length >= destinationSize)
    {
        BotLib_Print(PRT_WARNING,
                     "AAS_ParseEntityLump: %s value truncated from %zu characters\n",
                     fieldName,
                     length);
        length = destinationSize - 1U;
    }

    memcpy(destination, source, length);
    destination[length] = '\0';
}

/*
=============
AAS_SkipEntityWhitespace

Skip the whitespace and C/C++ comments accepted by the retail script lexer.
=============
*/
static void AAS_SkipEntityWhitespace(const char **cursor, const char *end)
{
	if (cursor == NULL || *cursor == NULL)
	{
		return;
	}

	const char *position = *cursor;
	for (;;)
	{
		while (position < end && isspace((unsigned char)*position))
		{
			++position;
		}
		if ((size_t)(end - position) < 2U || position[0] != '/')
		{
			break;
		}
		if (position[1] == '/')
		{
			position += 2;
			while (position < end && *position != '\n')
			{
				++position;
			}
			continue;
		}
		if (position[1] == '*')
		{
			position += 2;
			while ((size_t)(end - position) >= 2U &&
				!(position[0] == '*' && position[1] == '/'))
			{
				++position;
			}
			if ((size_t)(end - position) >= 2U)
			{
				position += 2;
			}
			else
			{
				position = end;
			}
			continue;
		}
		break;
	}

	*cursor = position;
}

/*
=============
AAS_ParseQuotedToken

Read one quoted mover-catalogue token with retail's literal-backslash flag.
=============
*/
static qboolean AAS_ParseQuotedToken(const char **cursor, const char *end, char **outToken)
{
    if (cursor == NULL || *cursor == NULL || outToken == NULL)
    {
        return qfalse;
    }

	AAS_SkipEntityWhitespace(cursor, end);
	const char *position = *cursor;

    if (position >= end || *position != '"')
    {
        *cursor = position;
        return qfalse;
    }

	++position;
	const char *start = position;
	while (position < end)
	{
		if (*position == '\n')
		{
			BotLib_Print(PRT_WARNING,
				"AAS_ParseEntityLump: newline inside quoted string\n");
			*cursor = position;
			return qfalse;
		}
		if (*position == '"')
		{
			size_t rawLength = (size_t)(position - start);
            char *token = (char *)malloc(rawLength + 1U);
            if (token == NULL)
            {
                BotLib_Print(PRT_WARNING,
                             "AAS_ParseEntityLump: out of memory parsing entity token\n");
                *cursor = position;
                return qfalse;
            }

			memcpy(token, start, rawLength);
			token[rawLength] = '\0';

            ++position;
            *cursor = position;
            *outToken = token;
            return qtrue;
        }

		++position;
    }

    BotLib_Print(PRT_WARNING,
                 "AAS_ParseEntityLump: unterminated quoted string in entity lump\n");
    *cursor = position;
    return qfalse;
}

static void AAS_SkipMalformedEntity(const char **cursor, const char *end)
{
    if (cursor == NULL || *cursor == NULL)
    {
        return;
    }

    const char *position = *cursor;
    while (position < end)
    {
        if (*position == '}')
        {
            ++position;
            break;
        }
        ++position;
    }

    *cursor = position;
}

static qboolean AAS_IsMoverClassname(const char *classname,
                                     float *defaultLip,
                                     float *defaultHeight,
                                     float *defaultSpeed,
                                     int *doorType,
                                     bot_mover_kind_t *moverKind)
{
    if (classname == NULL)
    {
        return qfalse;
    }

    if (strcmp(classname, "func_plat") == 0 || strcmp(classname, "func_plat2") == 0)
    {
        if (defaultLip != NULL)
        {
            *defaultLip = 8.0f;
        }
        if (defaultHeight != NULL)
        {
            *defaultHeight = 0.0f;
        }
        if (defaultSpeed != NULL)
        {
            *defaultSpeed = 200.0f;
        }
        if (doorType != NULL)
        {
            *doorType = AAS_MOVER_DOORTYPE_NONE;
        }
        if (moverKind != NULL)
        {
            *moverKind = BOT_MOVER_KIND_FUNC_PLAT;
        }
        return qtrue;
    }

    if (strcmp(classname, "func_bobbing") == 0)
    {
        if (defaultLip != NULL)
        {
            *defaultLip = 0.0f;
        }
        if (defaultHeight != NULL)
        {
            *defaultHeight = 32.0f;
        }
        if (defaultSpeed != NULL)
        {
            *defaultSpeed = 4.0f;
        }
        if (doorType != NULL)
        {
            *doorType = AAS_MOVER_DOORTYPE_NONE;
        }
        if (moverKind != NULL)
        {
            *moverKind = BOT_MOVER_KIND_FUNC_BOB;
        }
        return qtrue;
    }

    if (strcmp(classname, "func_door") == 0)
    {
        if (defaultLip != NULL)
        {
            *defaultLip = 8.0f;
        }
        if (defaultHeight != NULL)
        {
            *defaultHeight = 0.0f;
        }
        if (defaultSpeed != NULL)
        {
            *defaultSpeed = 100.0f;
        }
        if (doorType != NULL)
        {
            *doorType = AAS_MOVER_DOORTYPE_STANDARD;
        }
        if (moverKind != NULL)
        {
            *moverKind = BOT_MOVER_KIND_FUNC_DOOR;
        }
        return qtrue;
    }

    if (strcmp(classname, "func_door_rotating") == 0)
    {
        if (defaultLip != NULL)
        {
            *defaultLip = 0.0f;
        }
        if (defaultHeight != NULL)
        {
            *defaultHeight = 0.0f;
        }
        if (defaultSpeed != NULL)
        {
            *defaultSpeed = 100.0f;
        }
        if (doorType != NULL)
        {
            *doorType = AAS_MOVER_DOORTYPE_ROTATING;
        }
        if (moverKind != NULL)
        {
            *moverKind = BOT_MOVER_KIND_FUNC_DOOR_ROTATING;
        }
        return qtrue;
    }

    if (strcmp(classname, "func_door_secret") == 0
        || strcmp(classname, "func_door_secret2") == 0)
    {
        if (defaultLip != NULL)
        {
            *defaultLip = 8.0f;
        }
        if (defaultHeight != NULL)
        {
            *defaultHeight = 0.0f;
        }
        if (defaultSpeed != NULL)
        {
            *defaultSpeed = 50.0f;
        }
        if (doorType != NULL)
        {
            *doorType = AAS_MOVER_DOORTYPE_SECRET;
        }
        if (moverKind != NULL)
        {
            *moverKind = BOT_MOVER_KIND_FUNC_DOOR_SECRET;
        }
        return qtrue;
    }

    return qfalse;
}

static void AAS_ParseEntityKeyValue(aas_parsed_entity_t *entity, const char *key, const char *value)
{
    if (entity == NULL || key == NULL || value == NULL)
    {
        return;
    }

    if (strcmp(key, "classname") == 0)
    {
        AAS_CopyStringField(entity->classname, sizeof(entity->classname), value, key);
        entity->hasClassname = qtrue;
    }
    else if (strcmp(key, "model") == 0)
    {
        AAS_CopyStringField(entity->model, sizeof(entity->model), value, key);
        entity->hasModel = qtrue;
    }
    else if (strcmp(key, "lip") == 0)
    {
        float parsed = 0.0f;
        if (AAS_ParseFloatValue(value, &parsed))
        {
            entity->lip = parsed;
            entity->hasLip = qtrue;
        }
        else
        {
            BotLib_Print(PRT_WARNING,
                         "AAS_ParseEntityLump: failed to parse lip value '%s'\n",
                         value);
        }
    }
    else if (strcmp(key, "height") == 0)
    {
        float parsed = 0.0f;
        if (AAS_ParseFloatValue(value, &parsed))
        {
            entity->height = parsed;
            entity->hasHeight = qtrue;
        }
        else
        {
            BotLib_Print(PRT_WARNING,
                         "AAS_ParseEntityLump: failed to parse height value '%s'\n",
                         value);
        }
    }
    else if (strcmp(key, "speed") == 0)
    {
        float parsed = 0.0f;
        if (AAS_ParseFloatValue(value, &parsed))
        {
            entity->speed = parsed;
            entity->hasSpeed = qtrue;
        }
        else
        {
            BotLib_Print(PRT_WARNING,
                         "AAS_ParseEntityLump: failed to parse speed value '%s'\n",
                         value);
        }
    }
    else if (strcmp(key, "spawnflags") == 0)
    {
        int parsed = 0;
        if (AAS_ParseIntValue(value, &parsed))
        {
            entity->spawnflags = parsed;
            entity->hasSpawnflags = qtrue;
        }
        else
        {
            BotLib_Print(PRT_WARNING,
                         "AAS_ParseEntityLump: failed to parse spawnflags value '%s'\n",
                         value);
        }
    }
}

static void AAS_RegisterMoverEntity(const aas_parsed_entity_t *entity)
{
    if (entity == NULL || !entity->hasClassname)
    {
        return;
    }

    float defaultLip = 0.0f;
    float defaultHeight = 0.0f;
    float defaultSpeed = 0.0f;
    int doorType = AAS_MOVER_DOORTYPE_NONE;
    bot_mover_kind_t moverKind = BOT_MOVER_KIND_UNKNOWN;
    if (!AAS_IsMoverClassname(entity->classname,
                              &defaultLip,
                              &defaultHeight,
                              &defaultSpeed,
                              &doorType,
                              &moverKind))
    {
        return;
    }

    if (!entity->hasModel)
    {
        BotLib_Print(PRT_WARNING,
                     "AAS_ParseEntityLump: mover '%s' missing model key\n",
                     entity->classname);
        return;
    }

    if (entity->model[0] != '*')
    {
        BotLib_Print(PRT_WARNING,
                     "AAS_ParseEntityLump: mover '%s' has non-brush model '%s'\n",
                     entity->classname,
                     entity->model);
        return;
    }

    int modelnum = 0;
    if (!AAS_ParseIntValue(entity->model + 1, &modelnum))
    {
        BotLib_Print(PRT_WARNING,
                     "AAS_ParseEntityLump: mover '%s' has invalid model '%s'\n",
                     entity->classname,
                     entity->model);
        return;
    }

    float lip = entity->hasLip ? entity->lip : defaultLip;
    float height = entity->hasHeight ? entity->height : defaultHeight;
    float speed = entity->hasSpeed ? entity->speed : defaultSpeed;
    int spawnflags = entity->hasSpawnflags ? entity->spawnflags : 0;

    bot_mover_catalogue_entry_t entry = {
        .modelnum = modelnum,
        .modelindex = -1,
        .lip = lip,
        .height = height,
        .speed = speed,
        .spawnflags = spawnflags,
        .doortype = doorType,
        .kind = moverKind,
        .ready = false,
    };

    if (!BotMove_MoverCatalogueInsert(&entry))
    {
        BotLib_Print(PRT_WARNING,
                     "AAS_ParseEntityLump: failed to register mover model %d\n",
                     modelnum);
    }
}

static void AAS_ParseEntityLump(const char *data, size_t length)
{
    if (data == NULL || length == 0U)
    {
        return;
    }

    const char *cursor = data;
    const char *end = data + length;

	while (cursor < end)
	{
		AAS_SkipEntityWhitespace(&cursor, end);

        if (cursor >= end)
        {
            break;
        }

        if (*cursor != '{')
        {
            ++cursor;
            continue;
        }

        ++cursor;
        aas_parsed_entity_t entity = {0};
        qboolean malformed = qfalse;

		while (cursor < end)
		{
			AAS_SkipEntityWhitespace(&cursor, end);

            if (cursor >= end)
            {
                BotLib_Print(PRT_WARNING,
                             "AAS_ParseEntityLump: unterminated entity definition in BSP\n");
                malformed = qtrue;
                break;
            }

            if (*cursor == '}')
            {
                ++cursor;
                break;
            }

            char *key = NULL;
            if (!AAS_ParseQuotedToken(&cursor, end, &key))
            {
                BotLib_Print(PRT_WARNING,
                             "AAS_ParseEntityLump: expected quoted key in entity definition\n");
                if (key != NULL)
                {
                    free(key);
                }
                malformed = qtrue;
                AAS_SkipMalformedEntity(&cursor, end);
                break;
            }

			AAS_SkipEntityWhitespace(&cursor, end);

            char *value = NULL;
            if (!AAS_ParseQuotedToken(&cursor, end, &value))
            {
                BotLib_Print(PRT_WARNING,
                             "AAS_ParseEntityLump: expected quoted value for key '%s'\n",
                             (key != NULL) ? key : "");
                if (key != NULL)
                {
                    free(key);
                }
                if (value != NULL)
                {
                    free(value);
                }
                malformed = qtrue;
                AAS_SkipMalformedEntity(&cursor, end);
                break;
            }

            if (key != NULL && value != NULL)
            {
                AAS_ParseEntityKeyValue(&entity, key, value);
            }

            if (key != NULL)
            {
                free(key);
            }
            if (value != NULL)
            {
                free(value);
            }
        }

        if (!malformed)
        {
            AAS_RegisterMoverEntity(&entity);
        }
    }
}

static void AAS_FixupAreas(aas_area_t *areas, int count)
{
    if (areas == NULL || count <= 0)
    {
        return;
    }

    for (int index = 0; index < count; ++index)
    {
        aas_area_t *area = &areas[index];
        area->areanum = AAS_LittleLong(area->areanum);
        area->numfaces = AAS_LittleLong(area->numfaces);
        area->firstface = AAS_LittleLong(area->firstface);
        for (int axis = 0; axis < 3; ++axis)
        {
            area->mins[axis] = AAS_LittleFloat(area->mins[axis]);
            area->maxs[axis] = AAS_LittleFloat(area->maxs[axis]);
            area->center[axis] = AAS_LittleFloat(area->center[axis]);
        }
    }
}

static void AAS_FixupAreaSettings(aas_areasettings_t *settings, int count)
{
    if (settings == NULL || count <= 0)
    {
        return;
    }

    for (int index = 0; index < count; ++index)
    {
        aas_areasettings_t *entry = &settings[index];
        entry->contents = AAS_LittleLong(entry->contents);
        entry->areaflags = AAS_LittleLong(entry->areaflags);
        entry->presencetype = AAS_LittleLong(entry->presencetype);
        entry->cluster = AAS_LittleLong(entry->cluster);
        entry->clusterareanum = AAS_LittleLong(entry->clusterareanum);
        entry->numreachableareas = AAS_LittleLong(entry->numreachableareas);
        entry->firstreachablearea = AAS_LittleLong(entry->firstreachablearea);
    }
}

static void AAS_FixupReachability(aas_reachability_t *reachability, int count)
{
    if (reachability == NULL || count <= 0)
    {
        return;
    }

    for (int index = 0; index < count; ++index)
    {
        aas_reachability_t *reach = &reachability[index];
        reach->areanum = AAS_LittleLong(reach->areanum);
        reach->facenum = AAS_LittleLong(reach->facenum);
        reach->edgenum = AAS_LittleLong(reach->edgenum);
        for (int axis = 0; axis < 3; ++axis)
        {
            reach->start[axis] = AAS_LittleFloat(reach->start[axis]);
            reach->end[axis] = AAS_LittleFloat(reach->end[axis]);
        }
        reach->traveltype = AAS_LittleLong(reach->traveltype);
        reach->traveltime = AAS_LittleShort(reach->traveltime);
    }
}

/*
=============
AAS_FixupBBoxes

Convert loaded AAS bounding-box records from little endian.
=============
*/
static void AAS_FixupBBoxes(aas_bbox_t *bboxes, int count)
{
	if (bboxes == NULL || count <= 0)
	{
		return;
	}

	for (int index = 0; index < count; ++index)
	{
		aas_bbox_t *bbox = &bboxes[index];
		bbox->presencetype = AAS_LittleLong(bbox->presencetype);
		bbox->flags = AAS_LittleLong(bbox->flags);
		for (int axis = 0; axis < 3; ++axis)
		{
			bbox->mins[axis] = AAS_LittleFloat(bbox->mins[axis]);
			bbox->maxs[axis] = AAS_LittleFloat(bbox->maxs[axis]);
		}
	}
}

/*
=============
AAS_FixupVertexes

Convert loaded AAS vertex records from little endian.
=============
*/
static void AAS_FixupVertexes(aas_vertex_t *vertexes, int count)
{
	if (vertexes == NULL || count <= 0)
	{
		return;
	}

	for (int index = 0; index < count; ++index)
	{
		for (int axis = 0; axis < 3; ++axis)
		{
			vertexes[index][axis] = AAS_LittleFloat(vertexes[index][axis]);
		}
	}
}

/*
=============
AAS_FixupEdges

Convert loaded AAS edge records from little endian.
=============
*/
static void AAS_FixupEdges(aas_edge_t *edges, int count)
{
	if (edges == NULL || count <= 0)
	{
		return;
	}

	for (int index = 0; index < count; ++index)
	{
		edges[index].v[0] = AAS_LittleLong(edges[index].v[0]);
		edges[index].v[1] = AAS_LittleLong(edges[index].v[1]);
	}
}

/*
=============
AAS_FixupPlanes

Convert loaded AAS plane records from little endian.
=============
*/
static void AAS_FixupPlanes(aas_plane_t *planes, int count)
{
	if (planes == NULL || count <= 0)
	{
		return;
	}

	for (int index = 0; index < count; ++index)
	{
		aas_plane_t *plane = &planes[index];
		for (int axis = 0; axis < 3; ++axis)
		{
			plane->normal[axis] = AAS_LittleFloat(plane->normal[axis]);
		}
		plane->dist = AAS_LittleFloat(plane->dist);
		plane->type = AAS_LittleLong(plane->type);
	}
}

static void AAS_FixupNodes(aas_node_t *nodes, int count)
{
    if (nodes == NULL || count <= 0)
    {
        return;
    }

    for (int index = 0; index < count; ++index)
    {
        nodes[index].planenum = AAS_LittleLong(nodes[index].planenum);
        nodes[index].children[0] = AAS_LittleLong(nodes[index].children[0]);
        nodes[index].children[1] = AAS_LittleLong(nodes[index].children[1]);
    }
}

/*
=============
AAS_FixupFaces

Convert loaded AAS face records from little endian.
=============
*/
static void AAS_FixupFaces(aas_face_t *faces, int count)
{
	if (faces == NULL || count <= 0)
	{
		return;
	}

	for (int index = 0; index < count; ++index)
	{
		aas_face_t *face = &faces[index];
		face->planenum = AAS_LittleLong(face->planenum);
		face->faceflags = AAS_LittleLong(face->faceflags);
		face->numedges = AAS_LittleLong(face->numedges);
		face->firstedge = AAS_LittleLong(face->firstedge);
		face->frontarea = AAS_LittleLong(face->frontarea);
		face->backarea = AAS_LittleLong(face->backarea);
	}
}

/*
=============
AAS_FixupPortals

Convert loaded AAS portal records from little endian.
=============
*/
static void AAS_FixupPortals(aas_portal_t *portals, int count)
{
	if (portals == NULL || count <= 0)
	{
		return;
	}

	for (int index = 0; index < count; ++index)
	{
		aas_portal_t *portal = &portals[index];
		portal->areanum = AAS_LittleLong(portal->areanum);
		portal->frontcluster = AAS_LittleLong(portal->frontcluster);
		portal->backcluster = AAS_LittleLong(portal->backcluster);
		portal->clusterareanum[0] = AAS_LittleLong(portal->clusterareanum[0]);
		portal->clusterareanum[1] = AAS_LittleLong(portal->clusterareanum[1]);
	}
}

/*
=============
AAS_FixupClusters

Convert loaded AAS cluster records from little endian.
=============
*/
static void AAS_FixupClusters(aas_cluster_t *clusters, int count)
{
	if (clusters == NULL || count <= 0)
	{
		return;
	}

	for (int index = 0; index < count; ++index)
	{
		aas_cluster_t *cluster = &clusters[index];
		cluster->numareas = AAS_LittleLong(cluster->numareas);
		cluster->numportals = AAS_LittleLong(cluster->numportals);
		cluster->firstportal = AAS_LittleLong(cluster->firstportal);
	}
}

/*
=============
AAS_FixupIntArray

Convert a loaded AAS integer lump from little endian.
=============
*/
static void AAS_FixupIntArray(int *values, int count)
{
	if (values == NULL || count <= 0)
	{
		return;
	}

	for (int index = 0; index < count; ++index)
	{
		values[index] = AAS_LittleLong(values[index]);
	}
}

/*
=============
AAS_FixupBSPModels

Convert loaded Quake II BSP model records from little endian.
=============
*/
static void AAS_FixupBSPModels(aas_bspmodel_t *models, int count)
{
	if (models == NULL || count <= 0)
	{
		return;
	}

	for (int index = 0; index < count; ++index)
	{
		aas_bspmodel_t *model = &models[index];
		for (int component = 0; component < 3; ++component)
		{
			model->mins[component] = AAS_LittleFloat(model->mins[component]);
			model->maxs[component] = AAS_LittleFloat(model->maxs[component]);
			model->origin[component] = AAS_LittleFloat(model->origin[component]);
		}
		model->headnode = AAS_LittleLong(model->headnode);
		model->firstface = AAS_LittleLong(model->firstface);
		model->numfaces = AAS_LittleLong(model->numfaces);
	}
}

/*
=============
AAS_FixupBSPNodes

Convert loaded Quake II BSP node records from little endian.
=============
*/
static void AAS_FixupBSPNodes(aas_bspnode_t *nodes, int count)
{
	if (nodes == NULL || count <= 0)
	{
		return;
	}

	for (int index = 0; index < count; ++index)
	{
		aas_bspnode_t *node = &nodes[index];
		node->planenum = AAS_LittleLong(node->planenum);
		node->children[0] = AAS_LittleLong(node->children[0]);
		node->children[1] = AAS_LittleLong(node->children[1]);
		for (int component = 0; component < 3; ++component)
		{
			node->mins[component] = (short)AAS_LittleShort((uint16_t)node->mins[component]);
			node->maxs[component] = (short)AAS_LittleShort((uint16_t)node->maxs[component]);
		}
		node->firstface = AAS_LittleShort(node->firstface);
		node->numfaces = AAS_LittleShort(node->numfaces);
	}
}

/*
=============
AAS_FixupBSPLeaves

Convert loaded Quake II BSP leaf records from little endian.
=============
*/
static void AAS_FixupBSPLeaves(aas_bspleaf_t *leaves, int count)
{
	if (leaves == NULL || count <= 0)
	{
		return;
	}

	for (int index = 0; index < count; ++index)
	{
		aas_bspleaf_t *leaf = &leaves[index];
		leaf->contents = AAS_LittleLong(leaf->contents);
		leaf->cluster = (short)AAS_LittleShort((uint16_t)leaf->cluster);
		leaf->area = (short)AAS_LittleShort((uint16_t)leaf->area);
		for (int component = 0; component < 3; ++component)
		{
			leaf->mins[component] = (short)AAS_LittleShort((uint16_t)leaf->mins[component]);
			leaf->maxs[component] = (short)AAS_LittleShort((uint16_t)leaf->maxs[component]);
		}
		leaf->firstleafface = AAS_LittleShort(leaf->firstleafface);
		leaf->numleaffaces = AAS_LittleShort(leaf->numleaffaces);
		leaf->firstleafbrush = AAS_LittleShort(leaf->firstleafbrush);
		leaf->numleafbrushes = AAS_LittleShort(leaf->numleafbrushes);
	}
}

/*
=============
AAS_FixupBSPLeafBrushes

Convert loaded Quake II BSP leaf-brush indexes from little endian.
=============
*/
static void AAS_FixupBSPLeafBrushes(unsigned short *leafbrushes, int count)
{
	if (leafbrushes == NULL || count <= 0)
	{
		return;
	}

	for (int index = 0; index < count; ++index)
	{
		leafbrushes[index] = AAS_LittleShort(leafbrushes[index]);
	}
}

/*
=============
AAS_FixupBSPTexInfo

Convert loaded Quake II BSP texture-info records from little endian.
=============
*/
static void AAS_FixupBSPTexInfo(aas_bsptexinfo_t *texinfo, int count)
{
	if (texinfo == NULL || count <= 0)
	{
		return;
	}

	for (int index = 0; index < count; ++index)
	{
		aas_bsptexinfo_t *entry = &texinfo[index];
		for (int axis = 0; axis < 2; ++axis)
		{
			for (int component = 0; component < 4; ++component)
			{
				entry->vecs[axis][component] =
					AAS_LittleFloat(entry->vecs[axis][component]);
			}
		}
		entry->flags = AAS_LittleLong(entry->flags);
		entry->value = AAS_LittleLong(entry->value);
		entry->nexttexinfo = AAS_LittleLong(entry->nexttexinfo);
	}
}

/*
=============
AAS_FixupBSPEdges

Convert loaded Quake II BSP edge records from little endian.
=============
*/
static void AAS_FixupBSPEdges(aas_bspedge_t *edges, int count)
{
	if (edges == NULL || count <= 0)
	{
		return;
	}

	for (int index = 0; index < count; ++index)
	{
		edges[index].v[0] = AAS_LittleShort(edges[index].v[0]);
		edges[index].v[1] = AAS_LittleShort(edges[index].v[1]);
	}
}

/*
=============
AAS_FixupBSPFaces

Convert loaded Quake II BSP face records from little endian.
=============
*/
static void AAS_FixupBSPFaces(aas_bspface_t *faces, int count)
{
	if (faces == NULL || count <= 0)
	{
		return;
	}

	for (int index = 0; index < count; ++index)
	{
		aas_bspface_t *face = &faces[index];
		face->planenum = AAS_LittleShort(face->planenum);
		face->side = (short)AAS_LittleShort((uint16_t)face->side);
		face->firstedge = AAS_LittleLong(face->firstedge);
		face->numedges = (short)AAS_LittleShort((uint16_t)face->numedges);
		face->texinfo = (short)AAS_LittleShort((uint16_t)face->texinfo);
		face->lightofs = AAS_LittleLong(face->lightofs);
	}
}

/*
=============
AAS_FixupBSPBrushSides

Convert loaded Quake II BSP brush-side records from little endian.
=============
*/
static void AAS_FixupBSPBrushSides(aas_bspbrushside_t *brushsides, int count)
{
	if (brushsides == NULL || count <= 0)
	{
		return;
	}

	for (int index = 0; index < count; ++index)
	{
		brushsides[index].planenum = AAS_LittleShort(brushsides[index].planenum);
		brushsides[index].texinfo = (short)AAS_LittleShort((uint16_t)brushsides[index].texinfo);
	}
}

/*
=============
AAS_FixupBSPBrushes

Convert loaded Quake II BSP brush records from little endian.
=============
*/
static void AAS_FixupBSPBrushes(aas_bspbrush_t *brushes, int count)
{
	if (brushes == NULL || count <= 0)
	{
		return;
	}

	for (int index = 0; index < count; ++index)
	{
		brushes[index].firstside = AAS_LittleLong(brushes[index].firstside);
		brushes[index].numsides = AAS_LittleLong(brushes[index].numsides);
		brushes[index].contents = AAS_LittleLong(brushes[index].contents);
	}
}

static uint32_t AAS_CRC32Update(uint32_t crc, const void *data, size_t length)
{
    static uint32_t table[256];
    static int tableInitialised = 0;

    if (!tableInitialised)
    {
        for (uint32_t index = 0; index < 256U; ++index)
        {
            uint32_t value = index;
            for (int bit = 0; bit < 8; ++bit)
            {
                if (value & 1U)
                {
                    value = (value >> 1) ^ 0xEDB88320U;
                }
                else
                {
                    value >>= 1;
                }
            }

            table[index] = value;
        }

        tableInitialised = 1;
    }

    const uint8_t *bytes = (const uint8_t *)data;
    crc = ~crc;
    for (size_t i = 0; i < length; ++i)
    {
        crc = table[(crc ^ bytes[i]) & 0xFFU] ^ (crc >> 8);
    }

    return ~crc;
}

/*
=============
AAS_ComputeFileChecksumRange

Computes the checksum of one loose file or one bounded archive entry.
=============
*/
static qboolean AAS_ComputeFileChecksumRange(const char *path,
	long offset,
	long length,
	uint32_t *checksum)
{
	if (path == NULL || offset < 0L || length < -1L || checksum == NULL)
	{
		return qfalse;
	}

	FILE *file = fopen(path, "rb");
	if (file == NULL)
	{
		return qfalse;
	}
	if (fseek(file, offset, SEEK_SET) != 0)
	{
		fclose(file);
		return qfalse;
	}

	uint8_t buffer[8192];
	uint32_t crc = 0U;
	long remaining = length;

	while (remaining != 0L)
	{
		size_t requested = sizeof(buffer);
		if (remaining > 0L && (long)requested > remaining)
		{
			requested = (size_t)remaining;
		}
		size_t bytesRead = fread(buffer, 1U, requested, file);
		if (bytesRead == 0U)
		{
			break;
		}
		crc = AAS_CRC32Update(crc, buffer, bytesRead);
		if (remaining > 0L)
		{
			remaining -= (long)bytesRead;
		}
	}

	if (ferror(file) || remaining > 0L)
	{
		fclose(file);
		return qfalse;
	}

	fclose(file);
	*checksum = crc;
	return qtrue;
}

static int AAS_StringEndsWithIgnoreCase(const char *value, const char *suffix)
{
    if (value == NULL || suffix == NULL)
    {
        return 0;
    }

    size_t valueLength = strlen(value);
    size_t suffixLength = strlen(suffix);
    if (suffixLength == 0)
    {
        return 1;
    }

    if (suffixLength > valueLength)
    {
        return 0;
    }

    const char *valueSuffix = value + valueLength - suffixLength;
    for (size_t index = 0; index < suffixLength; ++index)
    {
        char lhs = (char)tolower((unsigned char)valueSuffix[index]);
        char rhs = (char)tolower((unsigned char)suffix[index]);
        if (lhs != rhs)
        {
            return 0;
        }
    }

    return 1;
}

static qboolean AAS_BuildPath(char *buffer,
                              size_t bufferSize,
                              const char *mapname,
                              const char *extension)
{
    if (buffer == NULL || bufferSize == 0U)
    {
        return qfalse;
    }

    buffer[0] = '\0';

    if (mapname == NULL)
    {
        return qfalse;
    }

    int prefixNeeded = 1;
    if (strncmp(mapname, "maps/", 5) == 0 || strncmp(mapname, "maps\\", 5) == 0)
    {
        prefixNeeded = 0;
    }

    int written;
    if (prefixNeeded)
    {
#ifdef _WIN32
		written = snprintf(buffer, bufferSize, "maps\\%s", mapname);
#else
		written = snprintf(buffer, bufferSize, "maps/%s", mapname);
#endif
    }
    else
    {
        written = snprintf(buffer, bufferSize, "%s", mapname);
    }

    if (written < 0 || (size_t)written >= bufferSize)
    {
        buffer[0] = '\0';
        return qfalse;
    }

    if (extension != NULL && *extension != '\0'
        && !AAS_StringEndsWithIgnoreCase(buffer, extension))
    {
        size_t currentLength = (size_t)written;
        if (currentLength + strlen(extension) + 1U > bufferSize)
        {
            buffer[0] = '\0';
            return qfalse;
        }

        strncat(buffer, extension, bufferSize - currentLength - 1U);
    }

    return qtrue;
}

/*
=============
AAS_BuildAASCandidatePath

Builds one of the retail AAS discovery candidates in probe order.
=============
*/
static qboolean AAS_BuildAASCandidatePath(char *buffer,
	size_t bufferSize,
	const char *mapname,
	int candidate)
{
	if (buffer == NULL || bufferSize == 0U || mapname == NULL ||
		(candidate != 0 && candidate != 1))
	{
		return qfalse;
	}

	const char *prefix = "";
	if (candidate == 1)
	{
#ifdef _WIN32
		prefix = "maps\\";
#else
		prefix = "maps/";
#endif
	}

	int written = snprintf(buffer, bufferSize, "%s%s.aas", prefix, mapname);
	if (written < 0 || (size_t)written >= bufferSize)
	{
		buffer[0] = '\0';
		return qfalse;
	}

	return qtrue;
}

#define AAS_PAK_IDENT 0x4B434150
#define AAS_PAK_ENTRY_NAME_SIZE 56
#define AAS_PAK_ENTRY_SIZE 64

typedef struct aas_pak_header_s
{
	int32_t ident;
	int32_t directoryOffset;
	int32_t directoryLength;
} aas_pak_header_t;

typedef struct aas_pak_entry_s
{
	char name[AAS_PAK_ENTRY_NAME_SIZE];
	int32_t offset;
	int32_t length;
} aas_pak_entry_t;

typedef struct aas_map_file_source_s
{
	char physicalPath[MAX_FILEPATH];
	char logicalPath[MAX_FILEPATH];
	char archivePath[MAX_FILEPATH];
	char originalDirectory[MAX_FILEPATH];
	long offset;
	long length;
	qboolean archived;
	qboolean zipped;
} aas_map_file_source_t;

typedef qboolean (*aas_zip_extract_callback_t)(const char *archivePath,
	const char *memberName);

static aas_zip_extract_callback_t g_aasZipExtractCallback;

#if defined(_WIN32) && UINTPTR_MAX == UINT32_MAX
typedef struct aas_unzip_options_s
{
	int extractOnlyNewer;
	int spaceToUnderscore;
	int promptToOverwrite;
	int quiet;
	int writeToStdout;
	int testArchive;
	int verbose;
	int update;
	int displayComment;
	int restoreDirectories;
	int overwriteAll;
	int convertText;
	int zipInfoMode;
	int caseInsensitive;
	int restorePrivileges;
	char *archiveName;
	char *extractDirectory;
} aas_unzip_options_t;

typedef struct aas_unzip_callbacks_s
{
	FARPROC print;
	FARPROC sound;
	FARPROC replace;
	FARPROC password;
	FARPROC applicationMessage;
	FARPROC service;
	uint32_t totalCompressedSize;
	uint32_t totalSize;
	uint32_t compressionFactor;
	uint32_t memberCount;
} aas_unzip_callbacks_t;

typedef int (WINAPI *aas_windll_unzip_t)(int includeCount,
	char **includeNames,
	int excludeCount,
	char **excludeNames,
	aas_unzip_options_t *options,
	aas_unzip_callbacks_t *callbacks);

typedef char aas_unzip_options_size_must_be_68_bytes[
	sizeof(aas_unzip_options_t) == 0x44 ? 1 : -1];
typedef char aas_unzip_callbacks_size_must_be_40_bytes[
	sizeof(aas_unzip_callbacks_t) == 0x28 ? 1 : -1];

/*
=============
AAS_ZipPrintCallback

Forward the retail UNZIP32 text callback through the botlib print channel.
=============
*/
static int WINAPI AAS_ZipPrintCallback(char *message, unsigned long length)
{
	if (message != NULL && length > 0UL)
	{
		int printableLength = length > (unsigned long)INT_MAX
			? INT_MAX
			: (int)length;
		BotLib_Print(PRT_MESSAGE, "%.*s", printableLength, message);
	}
	return (int)length;
}

/*
=============
AAS_ZipReplaceCallback

Select the retail replace-existing-member behavior during extraction.
=============
*/
static int WINAPI AAS_ZipReplaceCallback(char *filename,
	unsigned filenameSize)
{
	(void)filename;
	(void)filenameSize;
	return 1;
}

/*
=============
AAS_ZipPasswordCallback

Reject password prompting while retaining the retail callback table shape.
=============
*/
static int WINAPI AAS_ZipPasswordCallback(char *password,
	int passwordSize,
	const char *promptMessage,
	const char *entryName)
{
	(void)password;
	(void)passwordSize;
	(void)promptMessage;
	(void)entryName;
	return 1;
}

/*
=============
AAS_ZipApplicationMessageCallback

Retain the retail 13-argument callback ABI and compression-text formatting.
=============
*/
static void WINAPI AAS_ZipApplicationMessageCallback(
	unsigned long uncompressedSize,
	unsigned long compressedSize,
	unsigned compressionFactor,
	unsigned month,
	unsigned day,
	unsigned year,
	unsigned hour,
	unsigned minute,
	char indicator,
	const char *filename,
	const char *method,
	unsigned long crc,
	char encrypted)
{
	char compressionText[16];
	if (compressionFactor == 100U)
	{
		lstrcpyA(compressionText, "100%%");
	}
	else
	{
		wsprintfA(compressionText,
			"%c%d%%",
			uncompressedSize < compressedSize ? '-' : ' ',
			(int)compressionFactor);
	}

	(void)month;
	(void)day;
	(void)year;
	(void)hour;
	(void)minute;
	(void)indicator;
	(void)filename;
	(void)method;
	(void)crc;
	(void)encrypted;
}
#endif

/*
=============
AAS_SetZipExtractorForTests

Install a transient extractor used only by deterministic filesystem tests.
=============
*/
void AAS_SetZipExtractorForTests(aas_zip_extract_callback_t extractor)
{
	g_aasZipExtractCallback = extractor;
}

/*
=============
AAS_ExtractZipMember

Invoke the retail Info-ZIP DLL ABI, or a deterministic test extractor.
=============
*/
static qboolean AAS_ExtractZipMember(const char *archivePath,
	const char *memberName)
{
	if (archivePath == NULL || memberName == NULL)
	{
		return qfalse;
	}
	if (g_aasZipExtractCallback != NULL)
	{
		return g_aasZipExtractCallback(archivePath, memberName);
	}

#if defined(_WIN32) && UINTPTR_MAX == UINT32_MAX
	char searchBuffer[0x80];
	char *filePart = NULL;
	if (SearchPathA(NULL,
		"UNZIP32.DLL",
		NULL,
		(DWORD)sizeof(searchBuffer),
		searchBuffer,
		&filePart) == 0U)
	{
		return qfalse;
	}

	HMODULE unzipModule = LoadLibraryA("UNZIP32.DLL");
	if (unzipModule == NULL)
	{
		return qfalse;
	}

	aas_windll_unzip_t unzip =
		(aas_windll_unzip_t)GetProcAddress(unzipModule, "windll_unzip");
	if (unzip == NULL)
	{
		FreeLibrary(unzipModule);
		return qfalse;
	}

	aas_unzip_options_t options;
	memset(&options, 0, sizeof(options));
	options.quiet = 2;
	options.overwriteAll = 1;
	options.caseInsensitive = 1;
	options.archiveName = (char *)archivePath;

	aas_unzip_callbacks_t callbacks;
	memset(&callbacks, 0, sizeof(callbacks));
	callbacks.print = (FARPROC)AAS_ZipPrintCallback;
	callbacks.replace = (FARPROC)AAS_ZipReplaceCallback;
	callbacks.password = (FARPROC)AAS_ZipPasswordCallback;
	callbacks.applicationMessage =
		(FARPROC)AAS_ZipApplicationMessageCallback;

	char *includeNames[] = {(char *)memberName};
	int result = unzip(1,
		includeNames,
		0,
		NULL,
		&options,
		&callbacks);
	FreeLibrary(unzipModule);
	return result == 0 ? qtrue : qfalse;
#else
	return qfalse;
#endif
}

/*
=============
AAS_NormalizeSearchPath

Normalizes both loose and PAK paths to the platform's retail separator form.
=============
*/
static void AAS_NormalizeSearchPath(char *path)
{
	if (path == NULL)
	{
		return;
	}

	const char separator =
#ifdef _WIN32
		'\\';
#else
		'/';
#endif
	for (char *cursor = path; *cursor != '\0'; ++cursor)
	{
		if (*cursor == '/' || *cursor == '\\')
		{
			*cursor = separator;
		}
	}
}

/*
=============
AAS_SearchPathEqual

Compares normalized PAK entry names with retail ASCII case folding.
=============
*/
static qboolean AAS_SearchPathEqual(const char *lhs, const char *rhs)
{
	if (lhs == NULL || rhs == NULL)
	{
		return qfalse;
	}

	while (*lhs != '\0' || *rhs != '\0')
	{
		int left = (unsigned char)*lhs;
		int right = (unsigned char)*rhs;
		if (left >= 'a' && left <= 'z')
		{
			left -= 'a' - 'A';
		}
		if (right >= 'a' && right <= 'z')
		{
			right -= 'a' - 'A';
		}
		if (left != right)
		{
			return qfalse;
		}
		if (*lhs != '\0')
		{
			++lhs;
		}
		if (*rhs != '\0')
		{
			++rhs;
		}
	}

	return qtrue;
}

/*
=============
AAS_BuildLoosePath

Builds one retail loose-file path from a filesystem root and game directory.
=============
*/
static qboolean AAS_BuildLoosePath(char *buffer,
	size_t bufferSize,
	const char *root,
	const char *directory,
	const char *requested)
{
	if (buffer == NULL || bufferSize == 0U || requested == NULL)
	{
		return qfalse;
	}

	buffer[0] = '\0';
	const char separator =
#ifdef _WIN32
		'\\';
#else
		'/';
#endif

	const char *rootPart = root != NULL ? root : "";
	int written = snprintf(buffer, bufferSize, "%s", rootPart);
	if (written < 0 || (size_t)written >= bufferSize)
	{
		buffer[0] = '\0';
		return qfalse;
	}

	size_t length = (size_t)written;
	if (length > 0U && buffer[length - 1U] != '/' &&
		buffer[length - 1U] != '\\')
	{
		if (length + 2U > bufferSize)
		{
			buffer[0] = '\0';
			return qfalse;
		}
		buffer[length++] = separator;
		buffer[length] = '\0';
	}

	const char *directoryPart = directory != NULL ? directory : "";
	if (*directoryPart != '\0')
	{
		written = snprintf(buffer + length,
			bufferSize - length,
			"%s",
			directoryPart);
		if (written < 0 || (size_t)written >= bufferSize - length)
		{
			buffer[0] = '\0';
			return qfalse;
		}
		length += (size_t)written;
		if (length > 0U && buffer[length - 1U] != '/' &&
			buffer[length - 1U] != '\\')
		{
			if (length + 2U > bufferSize)
			{
				buffer[0] = '\0';
				return qfalse;
			}
			buffer[length++] = separator;
			buffer[length] = '\0';
		}
	}

	written = snprintf(buffer + length,
		bufferSize - length,
		"%s",
		requested);
	if (written < 0 || (size_t)written >= bufferSize - length)
	{
		buffer[0] = '\0';
		return qfalse;
	}

	AAS_NormalizeSearchPath(buffer);

	return qtrue;
}

/*
=============
AAS_LooseFileReadable

Tests the same read-access condition used by the retail filesystem discovery.
=============
*/
static qboolean AAS_LooseFileReadable(const char *path)
{
	if (path == NULL || *path == '\0')
	{
		return qfalse;
	}

#ifdef _WIN32
	return _access(path, 4) == 0 ? qtrue : qfalse;
#else
	return access(path, 4) == 0 ? qtrue : qfalse;
#endif
}

/*
=============
AAS_FindPakEntry

Finds one normalized logical filename in a Quake II PAK directory.
=============
*/
static qboolean AAS_FindPakEntry(const char *pakPath,
	const char *requested,
	long *entryOffset,
	long *entryLength)
{
	if (pakPath == NULL || requested == NULL || entryOffset == NULL ||
		entryLength == NULL)
	{
		return qfalse;
	}

	FILE *pak = fopen(pakPath, "rb");
	if (pak == NULL)
	{
		return qfalse;
	}

	aas_pak_header_t header;
	if (fread(&header, 1U, sizeof(header), pak) != sizeof(header))
	{
		fclose(pak);
		return qfalse;
	}
	header.ident = AAS_LittleLong(header.ident);
	header.directoryOffset = AAS_LittleLong(header.directoryOffset);
	header.directoryLength = AAS_LittleLong(header.directoryLength);
	if (header.ident != AAS_PAK_IDENT || header.directoryOffset < 0 ||
		header.directoryLength < 0)
	{
		fclose(pak);
		return qfalse;
	}

	size_t entryCount =
		(size_t)header.directoryLength / AAS_PAK_ENTRY_SIZE;
	if (entryCount == 0U ||
		entryCount > SIZE_MAX / sizeof(aas_pak_entry_t) ||
		fseek(pak, header.directoryOffset, SEEK_SET) != 0)
	{
		fclose(pak);
		return qfalse;
	}

	aas_pak_entry_t *entries =
		(aas_pak_entry_t *)malloc(entryCount * sizeof(*entries));
	if (entries == NULL)
	{
		fclose(pak);
		return qfalse;
	}
	if (fread(entries, sizeof(*entries), entryCount, pak) != entryCount)
	{
		free(entries);
		fclose(pak);
		return qfalse;
	}
	fclose(pak);

	char normalizedRequested[MAX_FILEPATH];
	snprintf(normalizedRequested,
		sizeof(normalizedRequested),
		"%s",
		requested);
	AAS_NormalizeSearchPath(normalizedRequested);
	for (size_t index = 0; index < entryCount; ++index)
	{
		char normalizedName[AAS_PAK_ENTRY_NAME_SIZE + 1U];
		memcpy(normalizedName, entries[index].name, AAS_PAK_ENTRY_NAME_SIZE);
		normalizedName[AAS_PAK_ENTRY_NAME_SIZE] = '\0';
		AAS_NormalizeSearchPath(normalizedName);
		if (!AAS_SearchPathEqual(normalizedName, normalizedRequested))
		{
			continue;
		}

		int32_t offset = AAS_LittleLong(entries[index].offset);
		int32_t length = AAS_LittleLong(entries[index].length);
		free(entries);
		if (offset < 0 || length < 0)
		{
			return qfalse;
		}
		*entryOffset = (long)offset;
		*entryLength = (long)length;
		return qtrue;
	}

	free(entries);
	return qfalse;
}

/*
=============
AAS_DiscoverMapFile

Discovers a loose file or PAK entry in exact retail checkpoint order.
=============
*/
static qboolean AAS_DiscoverMapFile(const char *requested,
	aas_map_file_source_t *source)
{
	if (requested == NULL || source == NULL)
	{
		return qfalse;
	}

	memset(source, 0, sizeof(*source));
	source->length = -1L;
	snprintf(source->logicalPath,
		sizeof(source->logicalPath),
		"%s",
		requested);

	const char *rootVariables[] = {"basedir", "cddir"};
	for (size_t rootIndex = 0;
		rootIndex < sizeof(rootVariables) / sizeof(rootVariables[0]);
		++rootIndex)
	{
		const char *gameDirectory = LibVarGetString("gamedir");
		const char *root = LibVarGetString(rootVariables[rootIndex]);
		const char *directories[] = {gameDirectory, "baseq2"};
		for (size_t directoryIndex = 0;
			directoryIndex < sizeof(directories) / sizeof(directories[0]);
			++directoryIndex)
		{
			char path[MAX_FILEPATH];
			if (AAS_BuildLoosePath(path,
				sizeof(path),
				root,
				directories[directoryIndex],
				requested))
			{
				BotLib_LogWrite("accessing %s", path);
				if (AAS_LooseFileReadable(path))
				{
					snprintf(source->physicalPath,
						sizeof(source->physicalPath),
						"%s",
						path);
					source->offset = 0L;
					source->length = -1L;
					source->archived = qfalse;
					return qtrue;
				}
			}

			for (int pakIndex = 0; pakIndex < 10; ++pakIndex)
			{
				char pakName[16];
				snprintf(pakName, sizeof(pakName), "pak%d.pak", pakIndex);
				char pakPath[MAX_FILEPATH];
				if (!AAS_BuildLoosePath(pakPath,
					sizeof(pakPath),
					root,
					directories[directoryIndex],
					pakName) ||
					!AAS_LooseFileReadable(pakPath))
				{
					continue;
				}

				BotLib_LogWrite("searching %s in %s",
					requested,
					pakPath);
				long entryOffset = 0L;
				long entryLength = 0L;
				if (!AAS_FindPakEntry(pakPath,
					requested,
					&entryOffset,
					&entryLength))
				{
					continue;
				}

				snprintf(source->physicalPath,
					sizeof(source->physicalPath),
					"%s",
					pakPath);
				source->offset = entryOffset;
				source->length = entryLength;
				source->archived = qtrue;
				return qtrue;
			}
		}
	}

	return qfalse;
}

/*
=============
AAS_RestoreZipDirectory

Restore the process directory saved around the retail ZIP extraction pass.
=============
*/
static void AAS_RestoreZipDirectory(const aas_map_file_source_t *source)
{
	if (source == NULL || !source->zipped ||
		source->originalDirectory[0] == '\0')
	{
		return;
	}
	(void)chdir(source->originalDirectory);
}

/*
=============
AAS_DiscoverZipAAS

Run the separate retail aas0.zip through aas9.zip extraction fallback.
=============
*/
static qboolean AAS_DiscoverZipAAS(const char *mapname,
	aas_map_file_source_t *source)
{
	if (mapname == NULL || source == NULL)
	{
		return qfalse;
	}

	char originalDirectory[MAX_FILEPATH];
	if (getcwd(originalDirectory, sizeof(originalDirectory)) == NULL)
	{
		return qfalse;
	}

	const char *basedir = LibVarGetString("basedir");
	const char *gamedir = LibVarGetString("gamedir");
	char workingDirectory[MAX_FILEPATH];
	if (!AAS_BuildLoosePath(workingDirectory,
		sizeof(workingDirectory),
		basedir,
		gamedir,
		"") ||
		chdir(workingDirectory) != 0)
	{
		return qfalse;
	}

	char memberName[MAX_FILEPATH];
	int memberLength = snprintf(memberName,
		sizeof(memberName),
		"%s.aas",
		mapname);
	if (memberLength < 0 || (size_t)memberLength >= sizeof(memberName))
	{
		(void)chdir(originalDirectory);
		return qfalse;
	}

	const char *directories[] = {gamedir, "baseq2"};
	for (size_t directoryIndex = 0;
		directoryIndex < sizeof(directories) / sizeof(directories[0]);
		++directoryIndex)
	{
		for (int zipIndex = 0; zipIndex < 10; ++zipIndex)
		{
			char zipName[16];
			snprintf(zipName, sizeof(zipName), "aas%d.zip", zipIndex);
			char archivePath[MAX_FILEPATH];
			if (!AAS_BuildLoosePath(archivePath,
				sizeof(archivePath),
				"..",
				directories[directoryIndex],
				zipName) ||
				!AAS_LooseFileReadable(archivePath))
			{
				continue;
			}

			BotLib_LogWrite("searching %s in %s",
				memberName,
				archivePath);
			if (!AAS_ExtractZipMember(archivePath, memberName))
			{
				BotLib_LogWrite("could not find %s in %s",
					memberName,
					archivePath);
				continue;
			}

			memset(source, 0, sizeof(*source));
			source->length = -1L;
			source->zipped = qtrue;
			snprintf(source->physicalPath,
				sizeof(source->physicalPath),
				"%s",
				memberName);
			snprintf(source->logicalPath,
				sizeof(source->logicalPath),
				"%s",
				memberName);
			snprintf(source->archivePath,
				sizeof(source->archivePath),
				"%s",
				archivePath);
			snprintf(source->originalDirectory,
				sizeof(source->originalDirectory),
				"%s",
				originalDirectory);
			return qtrue;
		}
	}

	(void)chdir(originalDirectory);
	return qfalse;
}

/*
=============
AAS_ReturnMapLoadFailure

Discard mover metadata parsed for a map that did not reach a successful load.
=============
*/
static int AAS_ReturnMapLoadFailure(int errorCode)
{
	BotMove_MoverCatalogueReset();
	return errorCode;
}

/*
=============
AAS_ReturnAASLoadFailure

Preserve the retail ZIP helper's final no-AAS status after member load errors.
=============
*/
static int AAS_ReturnAASLoadFailure(const aas_map_file_source_t *source,
	int errorCode)
{
	if (source == NULL || !source->zipped)
	{
		return AAS_ReturnMapLoadFailure(errorCode);
	}

	BotLib_Print(PRT_FATAL, "no AAS file available\n");
	return AAS_ReturnMapLoadFailure(BLERR_NOAASFILE);
}

/*
=============
AAS_PrintLoadedMapFile

Reports an archive/logical path pair, or for a loose file either the logical
"maps\<map>.<ext>" name or the resolved physical path, like retail.
=============
*/
static void AAS_PrintLoadedMapFile(const aas_map_file_source_t *source,
	qboolean looseUsesLogical)
{
	if (source == NULL)
	{
		return;
	}

	if (source->zipped)
	{
#ifdef _WIN32
		BotLib_Print(PRT_MESSAGE,
			"loaded %s\\%s\n",
			source->archivePath,
			source->logicalPath);
#else
		BotLib_Print(PRT_MESSAGE,
			"loaded %s/%s\n",
			source->archivePath,
			source->logicalPath);
#endif
		return;
	}

	if (source->archived)
	{
#ifdef _WIN32
		BotLib_Print(PRT_MESSAGE,
			"loaded %s\\%s\n",
			source->physicalPath,
			source->logicalPath);
#else
		BotLib_Print(PRT_MESSAGE,
			"loaded %s/%s\n",
			source->physicalPath,
			source->logicalPath);
#endif
		return;
	}

	/*
	 * Retail sub_1000e880 deliberately reports different buffers for the two
	 * loose cases. The BSP prints the logical name it assembled at
	 * 1000e8d6/1000e8fd/1000e928 ("maps\" + map + ".bsp"):
	 *   1000e98b  if (var_1b4 == 0)
	 *   1000e9bb      Print(1, "loaded %s\n", &var_120)
	 * while the AAS file prints the resolved physical path:
	 *   1000eae2  if (s == 0)
	 *   1000eb0f      Print(1, "loaded %s\n", &var_1b0)
	 */
	BotLib_Print(PRT_MESSAGE,
		"loaded %s\n",
		looseUsesLogical ? source->logicalPath : source->physicalPath);
}

static long AAS_GetFileSize(FILE *file)
{
    if (file == NULL)
    {
        return -1L;
    }

    long current = ftell(file);
    if (current < 0)
    {
        return -1L;
    }

    if (fseek(file, 0L, SEEK_END) != 0)
    {
        return -1L;
    }

    long size = ftell(file);
    if (size < 0)
    {
        return -1L;
    }

    if (fseek(file, current, SEEK_SET) != 0)
    {
        return -1L;
    }

	return size;
}

/*
=============
AAS_FreeBSPEntities

Free a retail-style linked BSP entity and epair list.
=============
*/
void AAS_FreeBSPEntities(aas_bspentity_t *entities)
{
	while (entities != NULL)
	{
		aas_bspentity_t *nextentity = entities->next;
		aas_bspepair_t *epair = entities->epairs;
		while (epair != NULL)
		{
			aas_bspepair_t *nextepair = epair->next;
			if (epair->key != NULL)
			{
				FreeMemory(epair->key);
			}
			if (epair->value != NULL)
			{
				FreeMemory(epair->value);
			}
			FreeMemory(epair);
			epair = nextepair;
		}
		FreeMemory(entities);
		entities = nextentity;
	}
}

/*
=============
AAS_ParseBSPEntities

Parse a Quake II entity lump into the linked entity/epair representation used
by the retail reachability generators.
=============
*/
aas_bspentity_t *AAS_ParseBSPEntities(const char *data, size_t length)
{
	if (data == NULL || length == 0U || length > (size_t)INT_MAX)
	{
		return NULL;
	}

	pc_script_t *script = LoadScriptMemory((char *)data,
		(int)length,
		"entdata");
	if (script == NULL)
	{
		return NULL;
	}
	/* Retail calls SetScriptFlags(script, 12) before reading dentdata. */
	script->flags = SCFL_NOSTRINGWHITESPACES |
		SCFL_NOSTRINGESCAPECHARS;

	aas_bspentity_t *entities = NULL;
	pc_token_t token;
	while (PS_ReadToken(script, &token))
	{
		if (strcmp(token.string, "{") != 0)
		{
			/* Retail 10006bda: j_sub_1003e2c0(script, "invalid %s\n"). */
			ScriptError(script, "invalid %s\n", token.string);
			AAS_FreeBSPEntities(entities);
			FreeScript(script);
			return NULL;
		}

		aas_bspentity_t *entity = GetClearedMemory(sizeof(*entity));
		if (entity == NULL)
		{
			AAS_FreeBSPEntities(entities);
			FreeScript(script);
			return NULL;
		}
		entity->next = entities;
		entities = entity;
		qboolean closed = qfalse;
		while (PS_ReadToken(script, &token))
		{
			if (strcmp(token.string, "}") == 0)
			{
				closed = qtrue;
				break;
			}

			aas_bspepair_t *epair = GetClearedMemory(sizeof(*epair));
			if (epair == NULL)
			{
				AAS_FreeBSPEntities(entities);
				FreeScript(script);
				return NULL;
			}
			epair->next = entity->epairs;
			entity->epairs = epair;

			if (token.type != TT_STRING)
			{
				/* Retail 10006bed: j_sub_1003e2c0(script, "invalid %s\n"). */
				ScriptError(script, "invalid %s\n", token.string);
				AAS_FreeBSPEntities(entities);
				FreeScript(script);
				return NULL;
			}

			size_t tokenlength = strlen(token.string);
			if (tokenlength < 2U)
			{
				/* 64-bit-safety guard retail lacks; keep it diagnosed. */
				ScriptError(script, "invalid %s\n", token.string);
				AAS_FreeBSPEntities(entities);
				FreeScript(script);
				return NULL;
			}
			token.string[tokenlength - 1U] = '\0';
			epair->key = GetMemory(tokenlength - 1U);
			if (epair->key == NULL)
			{
				AAS_FreeBSPEntities(entities);
				FreeScript(script);
				return NULL;
			}
			memcpy(epair->key, token.string + 1, tokenlength - 1U);

			if (!PS_ExpectTokenType(script, TT_STRING, 0, &token))
			{
				AAS_FreeBSPEntities(entities);
				FreeScript(script);
				return NULL;
			}
			tokenlength = strlen(token.string);
			if (tokenlength < 2U)
			{
				/* 64-bit-safety guard retail lacks; keep it diagnosed. */
				ScriptError(script, "invalid %s\n", token.string);
				AAS_FreeBSPEntities(entities);
				FreeScript(script);
				return NULL;
			}
			token.string[tokenlength - 1U] = '\0';
			epair->value = GetMemory(tokenlength - 1U);
			if (epair->value == NULL)
			{
				AAS_FreeBSPEntities(entities);
				FreeScript(script);
				return NULL;
			}
			memcpy(epair->value, token.string + 1, tokenlength - 1U);
		}

		if (!closed)
		{
			/* Retail 10006c34: j_sub_1003e2c0(script, "missing }\n"). */
			ScriptError(script, "missing }\n");
			AAS_FreeBSPEntities(entities);
			FreeScript(script);
			return NULL;
		}
	}
	FreeScript(script);
	return entities;
}

/*
=============
AAS_ValueForBSPEpairKey

Return the value for an exact BSP entity key, or NULL when absent.
=============
*/
const char *AAS_ValueForBSPEpairKey(const aas_bspentity_t *entity, const char *key)
{
	if (entity == NULL || key == NULL)
	{
		return NULL;
	}
	for (const aas_bspepair_t *epair = entity->epairs;
		epair != NULL;
		epair = epair->next)
	{
		if (epair->key != NULL && strcmp(epair->key, key) == 0)
		{
			return epair->value;
		}
	}
	return NULL;
}

/*
=============
AAS_VectorForBSPEpairKey

Parse a three-component vector from a BSP entity epair.
=============
*/
qboolean AAS_VectorForBSPEpairKey(const aas_bspentity_t *entity,
	const char *key,
	vec3_t value)
{
	if (value == NULL)
	{
		return qfalse;
	}
	const char *text = AAS_ValueForBSPEpairKey(entity, key);
	if (text == NULL)
	{
		return qfalse;
	}
	double parsed[3] = {0.0, 0.0, 0.0};
	(void)sscanf(text, "%lf %lf %lf", &parsed[0], &parsed[1], &parsed[2]);
	value[0] = (float)parsed[0];
	value[1] = (float)parsed[1];
	value[2] = (float)parsed[2];
	return qtrue;
}

/*
=============
AAS_FloatForBSPEpairKey

Return a BSP epair floating-point value, defaulting to zero.
=============
*/
float AAS_FloatForBSPEpairKey(const aas_bspentity_t *entity, const char *key)
{
	const char *text = AAS_ValueForBSPEpairKey(entity, key);
	return text != NULL ? strtof(text, NULL) : 0.0f;
}

/*
=============
AAS_IntForBSPEpairKey

Return a BSP epair integer value, defaulting to zero.
=============
*/
int AAS_IntForBSPEpairKey(const aas_bspentity_t *entity, const char *key)
{
	const char *text = AAS_ValueForBSPEpairKey(entity, key);
	return text != NULL ? (int)strtol(text, NULL, 10) : 0;
}

static int AAS_ReadLump(FILE *file,
	const q2_lump_t *lump,
	size_t elementSize,
	void **outBuffer,
	int *outCount,
	long baseOffset,
	long fileSize,
	int readError,
	const char *lumpName);

/*
=============
AAS_LoadBSPEntities

Read and parse the current Quake II map's entity lump for generator passes.
=============
*/
aas_bspentity_t *AAS_LoadBSPEntities(void)
{
	if (aasworld.bspEntityData != NULL && aasworld.bspEntityDataSize > 0)
	{
		return AAS_ParseBSPEntities(aasworld.bspEntityData,
			(size_t)aasworld.bspEntityDataSize);
	}

	char bspcandidate[MAX_FILEPATH];
	if (!AAS_BuildPath(bspcandidate,
		sizeof(bspcandidate),
		aasworld.mapName,
		".bsp"))
	{
		return NULL;
	}

	aas_map_file_source_t source;
	if (!AAS_DiscoverMapFile(bspcandidate, &source))
	{
		return NULL;
	}

	FILE *file = fopen(source.physicalPath, "rb");
	if (file == NULL)
	{
		return NULL;
	}
	if (fseek(file, source.offset, SEEK_SET) != 0)
	{
		fclose(file);
		return NULL;
	}

	long filesize = source.length;
	if (!source.archived)
	{
		filesize = AAS_GetFileSize(file);
	}

	q2_bsp_header_t header;
	if (filesize < (long)sizeof(header) ||
		fread(&header, sizeof(header), 1U, file) != 1U)
	{
		fclose(file);
		return NULL;
	}
	header.ident = AAS_LittleLong(header.ident);
	header.version = AAS_LittleLong(header.version);
	for (int index = 0; index < Q2_BSP_LUMP_MAX; ++index)
	{
		header.lumps[index].offset = AAS_LittleLong(header.lumps[index].offset);
		header.lumps[index].length = AAS_LittleLong(header.lumps[index].length);
	}
	if (header.ident != Q2_BSP_IDENT || header.version != Q2_BSP_VERSION)
	{
		fclose(file);
		return NULL;
	}

	char *data = NULL;
	int length = 0;
	int status = AAS_ReadLump(file,
		&header.lumps[Q2_BSP_LUMP_ENTITIES],
		1U,
		(void **)&data,
		&length,
		source.offset,
		filesize,
		BLERR_CANNOTREADBSPLUMP,
		"entity");
	fclose(file);
	if (status != BLERR_NOERROR || data == NULL || length <= 0)
	{
		FreeMemory(data);
		return NULL;
	}

	aas_bspentity_t *entities = AAS_ParseBSPEntities(data, (size_t)length);
	FreeMemory(data);
	return entities;
}

/*
=============
AAS_LumpFailure

Prints the retail BSP/AAS lump diagnostic and returns its shared load status.
=============
*/
static int AAS_LumpFailure(int errorCode,
	const char *lumpName,
	qboolean seekFailure)
{
	if (errorCode == BLERR_CANNOTREADBSPLUMP)
	{
		BotLib_Print(PRT_FATAL,
			seekFailure ? "can't seek to bsp lump %s\n" :
				"can't read bsp lump %s\n",
			lumpName != NULL ? lumpName : "");
	}
	else
	{
		BotLib_Print(PRT_FATAL,
			seekFailure ? "can't seek to aas lump\n" :
				"can't read aas lump\n");
	}

	return errorCode;
}

/*
=============
AAS_ReadLump

Reads one retail lump while preserving BSP alignment and exact failure output.
=============
*/
static int AAS_ReadLump(FILE *file,
	const q2_lump_t *lump,
	size_t elementSize,
	void **outBuffer,
	int *outCount,
	long baseOffset,
	long fileSize,
	int readError,
	const char *lumpName)
{
	if (outBuffer == NULL)
	{
		return readError;
	}

	*outBuffer = NULL;
	if (outCount != NULL)
	{
		*outCount = 0;
	}

	if (lump == NULL || file == NULL || baseOffset < 0L)
	{
		return readError;
	}

	if (lump->length == 0)
	{
		return BLERR_NOERROR;
	}

	if (readError == BLERR_CANNOTREADBSPLUMP &&
		(elementSize == 0U || (size_t)lump->length % elementSize != 0U))
	{
		BotLib_Print(PRT_FATAL, "odd %s bsp lump size\n",
			lumpName != NULL ? lumpName : "");
		return readError;
	}

	if (elementSize == 0U || lump->length < 0)
	{
		return AAS_LumpFailure(readError, lumpName, qfalse);
	}

	if (lump->offset < 0)
	{
		return AAS_LumpFailure(readError, lumpName, qtrue);
	}

	size_t length = (size_t)lump->length;
	size_t count = length / elementSize;
	if (count > (size_t)INT_MAX)
	{
		return AAS_LumpFailure(readError, lumpName, qfalse);
	}

	int64_t end = (int64_t)lump->offset + (int64_t)lump->length;
	if (fileSize >= 0 &&
		((int64_t)lump->offset > (int64_t)fileSize ||
		end > (int64_t)fileSize))
	{
		return AAS_LumpFailure(readError, lumpName, qfalse);
	}

	int64_t absoluteOffset = (int64_t)baseOffset + (int64_t)lump->offset;
	if (absoluteOffset < 0 || absoluteOffset > LONG_MAX ||
		fseek(file, (long)absoluteOffset, SEEK_SET) != 0)
	{
		return AAS_LumpFailure(readError, lumpName, qtrue);
	}

	void *buffer = GetClearedMemory(length);
	if (buffer == NULL)
	{
		return AAS_LumpFailure(readError, lumpName, qfalse);
	}

	if (fread(buffer, 1U, length, file) != length)
	{
		FreeMemory(buffer);
		return AAS_LumpFailure(readError, lumpName, qfalse);
	}

	*outBuffer = buffer;
	if (outCount != NULL)
	{
		*outCount = (int)count;
	}

	return BLERR_NOERROR;
}

/*
=============
AAS_FreeLoadedBSPCollisionData

Release pending BSP collision lump buffers during map-load failure.
=============
*/
static void AAS_FreeLoadedBSPCollisionData(aas_bspmodel_t *models,
                                           aas_bspnode_t *nodes,
                                           aas_bspleaf_t *leaves,
                                           unsigned short *leafbrushes,
                                           aas_plane_t *planes,
	                                       aas_bsptexinfo_t *texinfo,
                                           aas_bspbrushside_t *brushsides,
                                           aas_bspbrush_t *brushes)
{
	FreeMemory(models);
	FreeMemory(nodes);
	FreeMemory(leaves);
	FreeMemory(leafbrushes);
	FreeMemory(planes);
	FreeMemory(texinfo);
	FreeMemory(brushsides);
	FreeMemory(brushes);
}

/*
=============
AAS_LoadBSPCollisionData

Load Quake II BSP records retained beside the inline brush-model collision
graph. Retail owns texinfo here even though its internal trace never resolves
a brush-side texinfo index into the trace surface payload.
=============
*/
static int AAS_LoadBSPCollisionData(const aas_map_file_source_t *bspSource,
                                    aas_bspmodel_t **outModels,
                                    int *outNumModels,
                                    aas_bspnode_t **outNodes,
                                    int *outNumNodes,
                                    aas_bspleaf_t **outLeaves,
                                    int *outNumLeaves,
                                    unsigned short **outLeafBrushes,
                                    int *outLeafBrushIndexSize,
                                    aas_plane_t **outPlanes,
                                    int *outNumPlanes,
	                                aas_bsptexinfo_t **outTexInfo,
	                                int *outNumTexInfo,
                                    aas_bspbrushside_t **outBrushSides,
                                    int *outNumBrushSides,
                                    aas_bspbrush_t **outBrushes,
                                    int *outNumBrushes)
{
	if (bspSource == NULL ||
	    outModels == NULL ||
	    outNumModels == NULL ||
	    outNodes == NULL ||
	    outNumNodes == NULL ||
	    outLeaves == NULL ||
	    outNumLeaves == NULL ||
	    outLeafBrushes == NULL ||
	    outLeafBrushIndexSize == NULL ||
	    outPlanes == NULL ||
	    outNumPlanes == NULL ||
	    outTexInfo == NULL ||
	    outNumTexInfo == NULL ||
	    outBrushSides == NULL ||
	    outNumBrushSides == NULL ||
	    outBrushes == NULL ||
	    outNumBrushes == NULL)
	{
		return BLERR_CANNOTREADBSPLUMP;
	}

	*outModels = NULL;
	*outNumModels = 0;
	*outNodes = NULL;
	*outNumNodes = 0;
	*outLeaves = NULL;
	*outNumLeaves = 0;
	*outLeafBrushes = NULL;
	*outLeafBrushIndexSize = 0;
	*outPlanes = NULL;
	*outNumPlanes = 0;
	*outTexInfo = NULL;
	*outNumTexInfo = 0;
	*outBrushSides = NULL;
	*outNumBrushSides = 0;
	*outBrushes = NULL;
	*outNumBrushes = 0;

	FILE *file = fopen(bspSource->physicalPath, "rb");
	if (file == NULL)
	{
		BotLib_Print(PRT_ERROR,
			"AAS_LoadMap: cannot reopen BSP %s (%s)\n",
			bspSource->physicalPath,
			strerror(errno));
		return BLERR_CANNOTOPENBSPFILE;
	}
	if (fseek(file, bspSource->offset, SEEK_SET) != 0)
	{
		fclose(file);
		return BLERR_CANNOTSEEKTOBSPFILE;
	}
	long fileSize = bspSource->length;
	if (!bspSource->archived)
	{
		fileSize = AAS_GetFileSize(file);
	}
	if (fileSize < 0L || fileSize < (long)sizeof(q2_bsp_header_t))
	{
		fclose(file);
		return BLERR_CANNOTREADBSPHEADER;
	}

	q2_bsp_header_t header;
	if (fread(&header, sizeof(header), 1U, file) != 1U)
	{
		fclose(file);
		return BLERR_CANNOTREADBSPHEADER;
	}

	header.ident = AAS_LittleLong(header.ident);
	header.version = AAS_LittleLong(header.version);
	for (int index = 0; index < Q2_BSP_LUMP_MAX; ++index)
	{
		header.lumps[index].offset = AAS_LittleLong(header.lumps[index].offset);
		header.lumps[index].length = AAS_LittleLong(header.lumps[index].length);
	}

	if (header.ident != Q2_BSP_IDENT)
	{
		fclose(file);
		return BLERR_WRONGBSPFILEID;
	}
	if (header.version != Q2_BSP_VERSION)
	{
		fclose(file);
		return BLERR_WRONGBSPFILEVERSION;
	}

	aas_bspmodel_t *models = NULL;
	int numModels = 0;
	int result = AAS_ReadLump(file,
	                          &header.lumps[Q2_BSP_LUMP_MODELS],
	                          sizeof(aas_bspmodel_t),
	                          (void **)&models,
	                          &numModels,
	                          bspSource->offset,
	                          fileSize,
	                          BLERR_CANNOTREADBSPLUMP,
	                          "models");
	if (result != BLERR_NOERROR)
	{
		fclose(file);
		return result;
	}

	aas_bspnode_t *nodes = NULL;
	int numNodes = 0;
	result = AAS_ReadLump(file,
	                      &header.lumps[Q2_BSP_LUMP_NODES],
	                      sizeof(aas_bspnode_t),
	                      (void **)&nodes,
	                      &numNodes,
	                      bspSource->offset,
	                      fileSize,
	                      BLERR_CANNOTREADBSPLUMP,
	                      "nodes");
	if (result != BLERR_NOERROR)
	{
		FreeMemory(models);
		fclose(file);
		return result;
	}

	aas_bspleaf_t *leaves = NULL;
	int numLeaves = 0;
	result = AAS_ReadLump(file,
	                      &header.lumps[Q2_BSP_LUMP_LEAFS],
	                      sizeof(aas_bspleaf_t),
	                      (void **)&leaves,
	                      &numLeaves,
	                      bspSource->offset,
	                      fileSize,
	                      BLERR_CANNOTREADBSPLUMP,
	                      "leafs");
	if (result != BLERR_NOERROR)
	{
		FreeMemory(models);
		FreeMemory(nodes);
		fclose(file);
		return result;
	}

	unsigned short *leafbrushes = NULL;
	int leafBrushIndexSize = 0;
	result = AAS_ReadLump(file,
	                      &header.lumps[Q2_BSP_LUMP_LEAFBRUSHES],
	                      sizeof(unsigned short),
	                      (void **)&leafbrushes,
	                      &leafBrushIndexSize,
	                      bspSource->offset,
	                      fileSize,
	                      BLERR_CANNOTREADBSPLUMP,
	                      "leaf brushes");
	if (result != BLERR_NOERROR)
	{
		FreeMemory(models);
		FreeMemory(nodes);
		FreeMemory(leaves);
		fclose(file);
		return result;
	}

	aas_plane_t *planes = NULL;
	int numPlanes = 0;
	result = AAS_ReadLump(file,
	                      &header.lumps[Q2_BSP_LUMP_PLANES],
	                      sizeof(aas_plane_t),
	                      (void **)&planes,
	                      &numPlanes,
	                      bspSource->offset,
	                      fileSize,
	                      BLERR_CANNOTREADBSPLUMP,
	                      "planes");
	if (result != BLERR_NOERROR)
	{
		FreeMemory(models);
		FreeMemory(nodes);
		FreeMemory(leaves);
		FreeMemory(leafbrushes);
		fclose(file);
		return result;
	}

	aas_bsptexinfo_t *texinfo = NULL;
	int numTexInfo = 0;
	result = AAS_ReadLump(file,
	                      &header.lumps[Q2_BSP_LUMP_TEXINFO],
	                      sizeof(aas_bsptexinfo_t),
	                      (void **)&texinfo,
	                      &numTexInfo,
	                      bspSource->offset,
	                      fileSize,
	                      BLERR_CANNOTREADBSPLUMP,
	                      "texinfo");
	if (result != BLERR_NOERROR)
	{
		FreeMemory(models);
		FreeMemory(nodes);
		FreeMemory(leaves);
		FreeMemory(leafbrushes);
		FreeMemory(planes);
		fclose(file);
		return result;
	}

	aas_bspbrushside_t *brushsides = NULL;
	int numBrushSides = 0;
	result = AAS_ReadLump(file,
	                      &header.lumps[Q2_BSP_LUMP_BRUSHSIDES],
	                      sizeof(aas_bspbrushside_t),
	                      (void **)&brushsides,
	                      &numBrushSides,
	                      bspSource->offset,
	                      fileSize,
	                      BLERR_CANNOTREADBSPLUMP,
	                      "brush sides");
	if (result != BLERR_NOERROR)
	{
		FreeMemory(models);
		FreeMemory(nodes);
		FreeMemory(leaves);
		FreeMemory(leafbrushes);
		FreeMemory(planes);
		FreeMemory(texinfo);
		fclose(file);
		return result;
	}

	aas_bspbrush_t *brushes = NULL;
	int numBrushes = 0;
	result = AAS_ReadLump(file,
	                      &header.lumps[Q2_BSP_LUMP_BRUSHES],
	                      sizeof(aas_bspbrush_t),
	                      (void **)&brushes,
	                      &numBrushes,
	                      bspSource->offset,
	                      fileSize,
	                      BLERR_CANNOTREADBSPLUMP,
	                      "brushes");
	fclose(file);
	if (result != BLERR_NOERROR)
	{
		AAS_FreeLoadedBSPCollisionData(models,
		                               nodes,
		                               leaves,
		                               leafbrushes,
		                               planes,
		                               texinfo,
		                               brushsides,
		                               NULL);
		return result;
	}

	AAS_FixupBSPModels(models, numModels);
	AAS_FixupBSPNodes(nodes, numNodes);
	AAS_FixupBSPLeaves(leaves, numLeaves);
	AAS_FixupBSPLeafBrushes(leafbrushes, leafBrushIndexSize);
	AAS_FixupPlanes(planes, numPlanes);
	AAS_FixupBSPTexInfo(texinfo, numTexInfo);
	AAS_FixupBSPBrushSides(brushsides, numBrushSides);
	AAS_FixupBSPBrushes(brushes, numBrushes);

	*outModels = models;
	*outNumModels = numModels;
	*outNodes = nodes;
	*outNumNodes = numNodes;
	*outLeaves = leaves;
	*outNumLeaves = numLeaves;
	*outLeafBrushes = leafbrushes;
	*outLeafBrushIndexSize = leafBrushIndexSize;
	*outPlanes = planes;
	*outNumPlanes = numPlanes;
	*outTexInfo = texinfo;
	*outNumTexInfo = numTexInfo;
	*outBrushSides = brushsides;
	*outNumBrushSides = numBrushSides;
	*outBrushes = brushes;
	*outNumBrushes = numBrushes;
	return BLERR_NOERROR;
}

/*
=============
AAS_FreeLoadedBSPPointLightData

Release pending BSP light-sampling buffers during map-load failure.
=============
*/
static void AAS_FreeLoadedBSPPointLightData(vec3_t *vertexes,
	aas_bspedge_t *edges,
	int *surfedges,
	aas_bspface_t *faces,
	aas_bspsurfaceextent_t *surface_extents,
	unsigned char *lightdata)
{
	FreeMemory(vertexes);
	FreeMemory(edges);
	FreeMemory(surfedges);
	FreeMemory(faces);
	free(surface_extents);
	FreeMemory(lightdata);
}

/*
=============
AAS_BSPPointLightDataFailure

Report a deterministic guard rejection for malformed point-light BSP data.
=============
*/
static int AAS_BSPPointLightDataFailure(const char *field, int index)
{
	BotLib_Print(PRT_ERROR,
		"AAS_LoadMap: invalid BSP point-light %s at %d\n",
		field != NULL ? field : "data",
		index);
	return BLERR_CANNOTREADBSPLUMP;
}

/*
=============
AAS_BSPPointLightSpanValid

Check a signed first/count pair without allowing integer wraparound.
=============
*/
static qboolean AAS_BSPPointLightSpanValid(int first, int count, int total)
{
	return first >= 0 && count >= 0 && total >= 0 &&
		first <= total && count <= total - first;
}

/*
=============
AAS_BuildBSPPointLightExtents

Validate retail BSP sampling references and build each face's eight-byte
texture-minimum/extent cache.
=============
*/
static int AAS_BuildBSPPointLightExtents(const aas_bspmodel_t *models,
	int num_models,
	const aas_bspnode_t *nodes,
	int num_nodes,
	const aas_plane_t *planes,
	int num_planes,
	const aas_bsptexinfo_t *texinfo,
	int num_texinfo,
	const vec3_t *vertexes,
	int num_vertexes,
	const aas_bspedge_t *edges,
	int num_edges,
	const int *surfedges,
	int num_surfedges,
	const aas_bspface_t *faces,
	int num_faces,
	const unsigned char *lightdata,
	int lightdata_size,
	aas_bspsurfaceextent_t **out_surface_extents)
{
	if (out_surface_extents == NULL)
	{
		return BLERR_CANNOTREADBSPLUMP;
	}
	*out_surface_extents = NULL;
	if ((num_models > 0 && models == NULL) ||
		(num_nodes > 0 && nodes == NULL) ||
		(num_planes > 0 && planes == NULL) ||
		(num_texinfo > 0 && texinfo == NULL) ||
		(num_vertexes > 0 && vertexes == NULL) ||
		(num_edges > 0 && edges == NULL) ||
		(num_surfedges > 0 && surfedges == NULL) ||
		(num_faces > 0 && faces == NULL))
	{
		return AAS_BSPPointLightDataFailure("missing lump", 0);
	}

	for (int index = 0; index < num_models; ++index)
	{
		const aas_bspmodel_t *model = &models[index];
		if ((model->headnode >= num_nodes && model->headnode >= 0) ||
			!AAS_BSPPointLightSpanValid(model->firstface,
				model->numfaces,
				num_faces))
		{
			return AAS_BSPPointLightDataFailure("model", index);
		}
	}

	for (int index = 0; index < num_nodes; ++index)
	{
		const aas_bspnode_t *node = &nodes[index];
		if (node->planenum < 0 || node->planenum >= num_planes ||
			!AAS_BSPPointLightSpanValid((int)node->firstface,
				(int)node->numfaces,
				num_faces))
		{
			return AAS_BSPPointLightDataFailure("node", index);
		}
		for (int side = 0; side < 2; ++side)
		{
			if (node->children[side] >= num_nodes)
			{
				return AAS_BSPPointLightDataFailure("node child", index);
			}
		}
	}

	for (int index = 0; index < num_edges; ++index)
	{
		if ((int)edges[index].v[0] >= num_vertexes ||
			(int)edges[index].v[1] >= num_vertexes)
		{
			return AAS_BSPPointLightDataFailure("edge vertex", index);
		}
	}

	if (num_faces <= 0)
	{
		return BLERR_NOERROR;
	}
	if (faces == NULL || texinfo == NULL || vertexes == NULL ||
		edges == NULL || surfedges == NULL)
	{
		return AAS_BSPPointLightDataFailure("missing face dependency", 0);
	}

	aas_bspsurfaceextent_t *surface_extents =
		(aas_bspsurfaceextent_t *)calloc((size_t)num_faces,
			sizeof(aas_bspsurfaceextent_t));
	if (surface_extents == NULL)
	{
		return AAS_BSPPointLightDataFailure("extent allocation", 0);
	}

	for (int face_index = 0; face_index < num_faces; ++face_index)
	{
		const aas_bspface_t *face = &faces[face_index];
		if ((int)face->planenum >= num_planes ||
			face->texinfo < 0 || face->texinfo >= num_texinfo ||
			!AAS_BSPPointLightSpanValid(face->firstedge,
				(int)face->numedges,
				num_surfedges) ||
			face->numedges <= 0)
		{
			free(surface_extents);
			return AAS_BSPPointLightDataFailure("face", face_index);
		}

		float mins[2] = {FLT_MAX, FLT_MAX};
		float maxs[2] = {-FLT_MAX, -FLT_MAX};
		for (int edge_offset = 0;
			edge_offset < (int)face->numedges;
			++edge_offset)
		{
			int surfedge = surfedges[face->firstedge + edge_offset];
			if (surfedge == INT_MIN)
			{
				free(surface_extents);
				return AAS_BSPPointLightDataFailure("surfedge", face_index);
			}
			int edge_index = surfedge >= 0 ? surfedge : -surfedge;
			if (edge_index < 0 || edge_index >= num_edges)
			{
				free(surface_extents);
				return AAS_BSPPointLightDataFailure("surfedge", face_index);
			}

			const aas_bspedge_t *edge = &edges[edge_index];
			int vertex_index = surfedge >= 0 ?
				(int)edge->v[0] : (int)edge->v[1];
			if (vertex_index < 0 || vertex_index >= num_vertexes)
			{
				free(surface_extents);
				return AAS_BSPPointLightDataFailure("face vertex", face_index);
			}

			const aas_bsptexinfo_t *face_texinfo = &texinfo[face->texinfo];
			for (int axis = 0; axis < 2; ++axis)
			{
				float value = DotProduct(vertexes[vertex_index],
					face_texinfo->vecs[axis]) +
					face_texinfo->vecs[axis][3];
				if (value < mins[axis])
				{
					mins[axis] = value;
				}
				if (value > maxs[axis])
				{
					maxs[axis] = value;
				}
			}
		}

		for (int axis = 0; axis < 2; ++axis)
		{
			double texture_minimum =
				floor((double)mins[axis] / 16.0) * 16.0;
			double texture_maximum =
				ceil((double)maxs[axis] / 16.0) * 16.0;
			double extent = texture_maximum - texture_minimum;
			if (!isfinite(texture_minimum) || !isfinite(extent) ||
				texture_minimum < (double)SHRT_MIN ||
				texture_minimum > (double)SHRT_MAX ||
				extent < 0.0 || extent > (double)SHRT_MAX)
			{
				free(surface_extents);
				return AAS_BSPPointLightDataFailure("face extent", face_index);
			}
			surface_extents[face_index].texturemins[axis] =
				(short)texture_minimum;
			surface_extents[face_index].extents[axis] = (short)extent;
		}

		if (face->lightofs < 0)
		{
			continue;
		}

		int style_count = 0;
		while (style_count < 4 && face->styles[style_count] != 0xffU)
		{
			style_count += 1;
		}
		int width = (surface_extents[face_index].extents[0] >> 4) + 1;
		int height = (surface_extents[face_index].extents[1] >> 4) + 1;
		size_t sample_count = (size_t)width * (size_t)height;
		if (width <= 0 || height <= 0 ||
			sample_count > SIZE_MAX / 3U ||
			(size_t)style_count > SIZE_MAX / (sample_count * 3U))
		{
			free(surface_extents);
			return AAS_BSPPointLightDataFailure("lightmap dimensions", face_index);
		}
		size_t light_bytes = sample_count * 3U * (size_t)style_count;
		if (lightdata == NULL || lightdata_size < 0 ||
			face->lightofs > lightdata_size ||
			light_bytes > (size_t)(lightdata_size - face->lightofs))
		{
			free(surface_extents);
			return AAS_BSPPointLightDataFailure("lightmap span", face_index);
		}
	}

	*out_surface_extents = surface_extents;
	return BLERR_NOERROR;
}

/*
=============
AAS_LoadBSPPointLightData

Load and endian-fix the BSP geometry/lightmap lumps retained by retail's
point-light sampler, then calculate the per-face extent cache.
=============
*/
static int AAS_LoadBSPPointLightData(const aas_map_file_source_t *bsp_source,
	const aas_bspmodel_t *models,
	int num_models,
	const aas_bspnode_t *nodes,
	int num_nodes,
	const aas_plane_t *planes,
	int num_planes,
	const aas_bsptexinfo_t *texinfo,
	int num_texinfo,
	vec3_t **out_vertexes,
	int *out_num_vertexes,
	aas_bspedge_t **out_edges,
	int *out_num_edges,
	int **out_surfedges,
	int *out_num_surfedges,
	aas_bspface_t **out_faces,
	int *out_num_faces,
	aas_bspsurfaceextent_t **out_surface_extents,
	unsigned char **out_lightdata,
	int *out_lightdata_size)
{
	if (bsp_source == NULL || out_vertexes == NULL ||
		out_num_vertexes == NULL || out_edges == NULL ||
		out_num_edges == NULL || out_surfedges == NULL ||
		out_num_surfedges == NULL || out_faces == NULL ||
		out_num_faces == NULL || out_surface_extents == NULL ||
		out_lightdata == NULL || out_lightdata_size == NULL)
	{
		return BLERR_CANNOTREADBSPLUMP;
	}

	*out_vertexes = NULL;
	*out_num_vertexes = 0;
	*out_edges = NULL;
	*out_num_edges = 0;
	*out_surfedges = NULL;
	*out_num_surfedges = 0;
	*out_faces = NULL;
	*out_num_faces = 0;
	*out_surface_extents = NULL;
	*out_lightdata = NULL;
	*out_lightdata_size = 0;

	FILE *file = fopen(bsp_source->physicalPath, "rb");
	if (file == NULL)
	{
		BotLib_Print(PRT_ERROR,
			"AAS_LoadMap: cannot reopen BSP %s (%s)\n",
			bsp_source->physicalPath,
			strerror(errno));
		return BLERR_CANNOTOPENBSPFILE;
	}
	if (fseek(file, bsp_source->offset, SEEK_SET) != 0)
	{
		fclose(file);
		return BLERR_CANNOTSEEKTOBSPFILE;
	}

	long file_size = bsp_source->archived ?
		bsp_source->length : AAS_GetFileSize(file);
	if (file_size < 0L || file_size < (long)sizeof(q2_bsp_header_t))
	{
		fclose(file);
		return BLERR_CANNOTREADBSPHEADER;
	}
	if (fseek(file, bsp_source->offset, SEEK_SET) != 0)
	{
		fclose(file);
		return BLERR_CANNOTSEEKTOBSPFILE;
	}

	q2_bsp_header_t header;
	if (fread(&header, sizeof(header), 1U, file) != 1U)
	{
		fclose(file);
		return BLERR_CANNOTREADBSPHEADER;
	}
	header.ident = AAS_LittleLong(header.ident);
	header.version = AAS_LittleLong(header.version);
	for (int index = 0; index < Q2_BSP_LUMP_MAX; ++index)
	{
		header.lumps[index].offset =
			AAS_LittleLong(header.lumps[index].offset);
		header.lumps[index].length =
			AAS_LittleLong(header.lumps[index].length);
	}
	if (header.ident != Q2_BSP_IDENT)
	{
		fclose(file);
		return BLERR_WRONGBSPFILEID;
	}
	if (header.version != Q2_BSP_VERSION)
	{
		fclose(file);
		return BLERR_WRONGBSPFILEVERSION;
	}

	vec3_t *vertexes = NULL;
	int num_vertexes = 0;
	aas_bspedge_t *edges = NULL;
	int num_edges = 0;
	int *surfedges = NULL;
	int num_surfedges = 0;
	aas_bspface_t *faces = NULL;
	int num_faces = 0;
	unsigned char *lightdata = NULL;
	int lightdata_size = 0;
	aas_bspsurfaceextent_t *surface_extents = NULL;

	int result = AAS_ReadLump(file,
		&header.lumps[Q2_BSP_LUMP_VERTICES],
		sizeof(vec3_t),
		(void **)&vertexes,
		&num_vertexes,
		bsp_source->offset,
		file_size,
		BLERR_CANNOTREADBSPLUMP,
		"vertexes");
	if (result != BLERR_NOERROR)
	{
		goto load_failure;
	}
	result = AAS_ReadLump(file,
		&header.lumps[Q2_BSP_LUMP_EDGES],
		sizeof(aas_bspedge_t),
		(void **)&edges,
		&num_edges,
		bsp_source->offset,
		file_size,
		BLERR_CANNOTREADBSPLUMP,
		"edges");
	if (result != BLERR_NOERROR)
	{
		goto load_failure;
	}
	result = AAS_ReadLump(file,
		&header.lumps[Q2_BSP_LUMP_SURFEDGES],
		sizeof(int),
		(void **)&surfedges,
		&num_surfedges,
		bsp_source->offset,
		file_size,
		BLERR_CANNOTREADBSPLUMP,
		"surface edges");
	if (result != BLERR_NOERROR)
	{
		goto load_failure;
	}
	result = AAS_ReadLump(file,
		&header.lumps[Q2_BSP_LUMP_FACES],
		sizeof(aas_bspface_t),
		(void **)&faces,
		&num_faces,
		bsp_source->offset,
		file_size,
		BLERR_CANNOTREADBSPLUMP,
		"faces");
	if (result != BLERR_NOERROR)
	{
		goto load_failure;
	}
	result = AAS_ReadLump(file,
		&header.lumps[Q2_BSP_LUMP_LIGHTING],
		1U,
		(void **)&lightdata,
		&lightdata_size,
		bsp_source->offset,
		file_size,
		BLERR_CANNOTREADBSPLUMP,
		"lighting");
	if (result != BLERR_NOERROR)
	{
		goto load_failure;
	}
	fclose(file);
	file = NULL;

	AAS_FixupVertexes((aas_vertex_t *)vertexes, num_vertexes);
	AAS_FixupBSPEdges(edges, num_edges);
	AAS_FixupIntArray(surfedges, num_surfedges);
	AAS_FixupBSPFaces(faces, num_faces);
	result = AAS_BuildBSPPointLightExtents(models,
		num_models,
		nodes,
		num_nodes,
		planes,
		num_planes,
		texinfo,
		num_texinfo,
		(const vec3_t *)vertexes,
		num_vertexes,
		edges,
		num_edges,
		surfedges,
		num_surfedges,
		faces,
		num_faces,
		lightdata,
		lightdata_size,
		&surface_extents);
	if (result != BLERR_NOERROR)
	{
		goto load_failure;
	}

	*out_vertexes = vertexes;
	*out_num_vertexes = num_vertexes;
	*out_edges = edges;
	*out_num_edges = num_edges;
	*out_surfedges = surfedges;
	*out_num_surfedges = num_surfedges;
	*out_faces = faces;
	*out_num_faces = num_faces;
	*out_surface_extents = surface_extents;
	*out_lightdata = lightdata;
	*out_lightdata_size = lightdata_size;
	return BLERR_NOERROR;

load_failure:
	if (file != NULL)
	{
		fclose(file);
	}
	AAS_FreeLoadedBSPPointLightData(vertexes,
		edges,
		surfedges,
		faces,
		surface_extents,
		lightdata);
	return result;
}

/*
=============
AAS_SampleBSPPointLightFace

Sample one face at a BSP split point using retail's s-major lightmap address
calculation and fixed 0x108 per-channel scale.
=============
*/
static qboolean AAS_SampleBSPPointLightFace(int face_index,
	const vec3_t point,
	int *red,
	int *green,
	int *blue)
{
	if (face_index < 0 || face_index >= aasworld.numBspFaces ||
		aasworld.bspFaces == NULL || aasworld.bspSurfaceExtents == NULL ||
		aasworld.bspTexInfo == NULL || point == NULL)
	{
		return qfalse;
	}

	const aas_bspface_t *face = &aasworld.bspFaces[face_index];
	if (face->texinfo < 0 || face->texinfo >= aasworld.numBspTexInfo)
	{
		return qfalse;
	}
	const aas_bsptexinfo_t *texinfo = &aasworld.bspTexInfo[face->texinfo];
	const aas_bspsurfaceextent_t *surface_extent =
		&aasworld.bspSurfaceExtents[face_index];

	float light_s_value = DotProduct(point, texinfo->vecs[0]) +
		texinfo->vecs[0][3];
	float light_t_value = DotProduct(point, texinfo->vecs[1]) +
		texinfo->vecs[1][3];
	if (!isfinite(light_s_value) || !isfinite(light_t_value) ||
		light_s_value < (float)INT_MIN || light_s_value > (float)INT_MAX ||
		light_t_value < (float)INT_MIN || light_t_value > (float)INT_MAX)
	{
		return qfalse;
	}
	int light_s = (int)light_s_value;
	int light_t = (int)light_t_value;
	int delta_s = light_s - (int)surface_extent->texturemins[0];
	int delta_t = light_t - (int)surface_extent->texturemins[1];
	if (delta_s < 0 || delta_t < 0 ||
		delta_s > (int)surface_extent->extents[0] ||
		delta_t > (int)surface_extent->extents[1])
	{
		return qfalse;
	}

	int sample_red = 0;
	int sample_green = 0;
	int sample_blue = 0;
	if (face->lightofs >= 0)
	{
		int width = ((int)surface_extent->extents[0] >> 4) + 1;
		int height = ((int)surface_extent->extents[1] >> 4) + 1;
		if (width <= 0 || height <= 0 || aasworld.bspLightData == NULL ||
			aasworld.bspLightDataSize < 0 ||
			face->lightofs > aasworld.bspLightDataSize)
		{
			return qfalse;
		}
		size_t sample_count = (size_t)width * (size_t)height;
		size_t sample_index =
			(size_t)(delta_s >> 4) * (size_t)width +
			(size_t)(delta_t >> 4);
		if (sample_count > SIZE_MAX / 3U)
		{
			return qfalse;
		}
		size_t style_bytes = sample_count * 3U;

		for (int style = 0;
			style < 4 && face->styles[style] != 0xffU;
			++style)
		{
			size_t style_offset = (size_t)style * style_bytes;
			size_t pixel_offset = sample_index * 3U;
			if (style_offset > SIZE_MAX - pixel_offset ||
				(size_t)face->lightofs > SIZE_MAX - style_offset - pixel_offset)
			{
				return qfalse;
			}
			size_t offset = (size_t)face->lightofs +
				style_offset + pixel_offset;
			if (offset > (size_t)aasworld.bspLightDataSize ||
				(size_t)aasworld.bspLightDataSize - offset < 3U)
			{
				return qfalse;
			}
			const unsigned char *sample = &aasworld.bspLightData[offset];
			sample_red += (int)sample[0] * 0x108;
			sample_green += (int)sample[1] * 0x108;
			sample_blue += (int)sample[2] * 0x108;
		}
		sample_red >>= 8;
		sample_green >>= 8;
		sample_blue >>= 8;
	}

	if (red != NULL)
	{
		*red = sample_red;
	}
	if (green != NULL)
	{
		*green = sample_green;
	}
	if (blue != NULL)
	{
		*blue = sample_blue;
	}
	return qtrue;
}

/*
=============
AAS_RecursiveBSPPointLight

Walk the retail BSP split recursion, sampling node faces between the near and
far child traversals.
=============
*/
static qboolean AAS_RecursiveBSPPointLight(int nodenum,
	const vec3_t start,
	const vec3_t end,
	vec3_t hit,
	int *red,
	int *green,
	int *blue,
	int depth)
{
	if (nodenum < 0)
	{
		return qfalse;
	}
	if (nodenum >= aasworld.numBspNodes || depth >= aasworld.numBspNodes ||
		aasworld.bspNodes == NULL || aasworld.bspPlanes == NULL)
	{
		return qfalse;
	}

	const aas_bspnode_t *node = &aasworld.bspNodes[nodenum];
	if (node->planenum < 0 || node->planenum >= aasworld.numBspPlanes)
	{
		return qfalse;
	}
	const aas_plane_t *plane = &aasworld.bspPlanes[node->planenum];
	float front;
	float back;
	if (plane->type >= 0 && plane->type < 3)
	{
		front = start[plane->type] - plane->dist;
		back = end[plane->type] - plane->dist;
	}
	else
	{
		front = DotProduct(start, plane->normal) - plane->dist;
		back = DotProduct(end, plane->normal) - plane->dist;
	}

	int side = front < 0.0f;
	if ((back < 0.0f) == side)
	{
		return AAS_RecursiveBSPPointLight(node->children[side],
			start,
			end,
			hit,
			red,
			green,
			blue,
			depth + 1);
	}

	float denominator = front - back;
	if (denominator == 0.0f)
	{
		return qfalse;
	}
	float fraction = front / denominator;
	vec3_t middle;
	for (int component = 0; component < 3; ++component)
	{
		middle[component] = start[component] +
			fraction * (end[component] - start[component]);
	}

	if (AAS_RecursiveBSPPointLight(node->children[side],
		start,
		middle,
		hit,
		red,
		green,
		blue,
		depth + 1))
	{
		return qtrue;
	}

	int first_face = (int)node->firstface;
	int num_faces = (int)node->numfaces;
	if (!AAS_BSPPointLightSpanValid(first_face,
		num_faces,
		aasworld.numBspFaces))
	{
		return qfalse;
	}
	for (int offset = 0; offset < num_faces; ++offset)
	{
		int sample_red;
		int sample_green;
		int sample_blue;
		if (!AAS_SampleBSPPointLightFace(first_face + offset,
			middle,
			&sample_red,
			&sample_green,
			&sample_blue))
		{
			continue;
		}
		if (hit != NULL)
		{
			VectorCopy(middle, hit);
		}
		if (red != NULL)
		{
			*red = sample_red;
		}
		if (green != NULL)
		{
			*green = sample_green;
		}
		if (blue != NULL)
		{
			*blue = sample_blue;
		}
		return qtrue;
	}

	return AAS_RecursiveBSPPointLight(node->children[side ^ 1],
		middle,
		end,
		hit,
		red,
		green,
		blue,
		depth + 1);
}

/*
=============
AAS_BSPTracePointLight

Run retail's static point-light wrapper from the world model headnode.
=============
*/
qboolean AAS_BSPTracePointLight(const vec3_t start,
	const vec3_t end,
	vec3_t hit,
	int *red,
	int *green,
	int *blue)
{
	if (!aasworld.loaded || start == NULL || end == NULL ||
		aasworld.bspLightData == NULL || aasworld.bspLightDataSize <= 0 ||
		aasworld.bspModels == NULL || aasworld.numBspModels <= 0 ||
		aasworld.bspNodes == NULL || aasworld.numBspNodes <= 0)
	{
		return qfalse;
	}

	return AAS_RecursiveBSPPointLight(aasworld.bspModels[0].headnode,
		start,
		end,
		hit,
		red,
		green,
		blue,
		0);
}

/*
=============
AAS_LoadBSPVisibilityData

Retain and endian-fix the Quake II dvis lump used by retail AAS_InPVS.
=============
*/
static qboolean AAS_LoadBSPVisibilityData(
	const aas_map_file_source_t *bsp_source,
	unsigned char **out_visibility,
	size_t *out_visibility_size,
	int *out_num_clusters)
{
	if (bsp_source == NULL || out_visibility == NULL ||
		out_visibility_size == NULL || out_num_clusters == NULL)
	{
		return qfalse;
	}
	*out_visibility = NULL;
	*out_visibility_size = 0U;
	*out_num_clusters = 0;

	FILE *file = fopen(bsp_source->physicalPath, "rb");
	if (file == NULL || fseek(file, bsp_source->offset, SEEK_SET) != 0)
	{
		if (file != NULL)
		{
			fclose(file);
		}
		return qfalse;
	}
	long file_size = bsp_source->archived
		? bsp_source->length
		: AAS_GetFileSize(file);
	if (file_size < (long)sizeof(q2_bsp_header_t))
	{
		fclose(file);
		return qfalse;
	}

	q2_bsp_header_t header;
	if (fread(&header, sizeof(header), 1U, file) != 1U)
	{
		fclose(file);
		return qfalse;
	}
	header.ident = AAS_LittleLong(header.ident);
	header.version = AAS_LittleLong(header.version);
	for (int index = 0; index < Q2_BSP_LUMP_MAX; ++index)
	{
		header.lumps[index].offset = AAS_LittleLong(
			header.lumps[index].offset);
		header.lumps[index].length = AAS_LittleLong(
			header.lumps[index].length);
	}
	if (header.ident != Q2_BSP_IDENT || header.version != Q2_BSP_VERSION)
	{
		fclose(file);
		return qfalse;
	}

	unsigned char *visibility = NULL;
	int visibility_size = 0;
	int result = AAS_ReadLump(file,
		&header.lumps[Q2_BSP_LUMP_VISIBILITY],
		1U,
		(void **)&visibility,
		&visibility_size,
		bsp_source->offset,
		file_size,
		BLERR_CANNOTREADBSPLUMP,
		"visibility");
	fclose(file);
	if (result != BLERR_NOERROR)
	{
		FreeMemory(visibility);
		return qfalse;
	}
	if (visibility == NULL || visibility_size == 0)
	{
		return qtrue;
	}
	if (visibility_size < (int)sizeof(int32_t))
	{
		FreeMemory(visibility);
		return qfalse;
	}

	int32_t num_clusters;
	memcpy(&num_clusters, visibility, sizeof(num_clusters));
	num_clusters = AAS_LittleLong(num_clusters);
	if (num_clusters < 0 ||
		(size_t)num_clusters >
		((size_t)visibility_size - sizeof(int32_t)) /
			(2U * sizeof(int32_t)))
	{
		FreeMemory(visibility);
		return qfalse;
	}
	memcpy(visibility, &num_clusters, sizeof(num_clusters));
	for (int index = 0; index < num_clusters * 2; ++index)
	{
		size_t offset = sizeof(int32_t) +
			(size_t)index * sizeof(int32_t);
		int32_t bit_offset;
		memcpy(&bit_offset, visibility + offset, sizeof(bit_offset));
		bit_offset = AAS_LittleLong(bit_offset);
		memcpy(visibility + offset, &bit_offset, sizeof(bit_offset));
	}

	*out_visibility = visibility;
	*out_visibility_size = (size_t)visibility_size;
	*out_num_clusters = num_clusters;
	return qtrue;
}

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

/*
=============
AAS_FreeEntityArray

Release the host-native entity table and all per-entity area bookkeeping.
=============
*/
static void AAS_FreeEntityArray(void)
{
	if (aasworld.entities != NULL)
	{
		for (int entnum = 0; entnum < aasworld.maxEntities; ++entnum)
		{
			aas_entity_t *entity = &aasworld.entities[entnum];
			AAS_UnlinkEntityFromAreas(entity);
			AAS_UnlinkEntityFromBSPLeaves(entity);
			free(entity->areaOccupancyBits);
			entity->areaOccupancyBits = NULL;
			entity->areaOccupancyWords = 0U;
			entity->areaOccupancyCount = 0;
		}
		FreeMemory(aasworld.entities);
	}

	aasworld.entities = NULL;
	aasworld.maxEntities = 0;
	aasworld.maxClients = 0;
}

/*
=============
AAS_InitAASLinkHeap

Initialise the retail-sized fixed AAS entity-link heap from max_aaslinks.
=============
*/
void AAS_InitAASLinkHeap(void)
{
	int maxlinks = g_aasLinkHeapSize;
	if (g_aasLinkHeap == NULL)
	{
		maxlinks = (int)LibVarValue("max_aaslinks", "4096");
		if (maxlinks < 0)
		{
			maxlinks = 0;
		}

		if ((size_t)maxlinks > SIZE_MAX / sizeof(*g_aasLinkHeap))
		{
			maxlinks = 0;
		}

		g_aasLinkHeapSize = maxlinks;
		if (maxlinks > 0)
		{
			g_aasLinkHeap = (aas_link_t *)GetMemory(
				(size_t)maxlinks * sizeof(*g_aasLinkHeap));
		}
	}

	for (int index = 0; index < maxlinks && g_aasLinkHeap != NULL; ++index)
	{
		aas_link_t *link = &g_aasLinkHeap[index];
		link->prev_ent = index > 0 ? &g_aasLinkHeap[index - 1] : NULL;
		link->next_ent = index + 1 < maxlinks ? &g_aasLinkHeap[index + 1] : NULL;
	}

	g_aasLinkFreeList = g_aasLinkHeap;
}

/*
=============
AAS_FreeAASLinkHeap

Release the retail AAS entity-link heap during full AAS shutdown.
=============
*/
void AAS_FreeAASLinkHeap(void)
{
	if (g_aasLinkHeap != NULL)
	{
		FreeMemory(g_aasLinkHeap);
	}

	g_aasLinkHeap = NULL;
	g_aasLinkFreeList = NULL;
	g_aasLinkHeapSize = 0;
}

/*
=============
AAS_AllocAASLink

Pop one entity-to-area link from the retail fixed free list.
=============
*/
aas_link_t *AAS_AllocAASLink(void)
{
	aas_link_t *link = g_aasLinkFreeList;
	if (link == NULL)
	{
		BotLib_Print(PRT_FATAL, "empty aas link heap\n");
		return NULL;
	}

	g_aasLinkFreeList = link->next_ent;
	if (g_aasLinkFreeList != NULL)
	{
		g_aasLinkFreeList->prev_ent = NULL;
	}

	return link;
}

/*
=============
AAS_FreeAASLink

Return one entity-to-area link to the head of the retail free list.
=============
*/
void AAS_FreeAASLink(aas_link_t *link)
{
	if (link == NULL)
	{
		return;
	}

	if (g_aasLinkFreeList != NULL)
	{
		g_aasLinkFreeList->prev_ent = link;
	}

	link->prev_ent = NULL;
	link->next_ent = g_aasLinkFreeList;
	link->next_area = NULL;
	link->prev_area = NULL;
	g_aasLinkFreeList = link;
}

/*
=============
AAS_InitBSPLinkHeap

Initialise the retail-sized fixed BSP leaf/entity-link heap from max_bsplinks.
=============
*/
void AAS_InitBSPLinkHeap(void)
{
	int maxlinks = g_bspLinkHeapSize;
	if (g_bspLinkHeap == NULL)
	{
		maxlinks = (int)LibVarValue("max_bsplinks", "4096");
		if (maxlinks < 0)
		{
			maxlinks = 0;
		}

		if ((size_t)maxlinks > SIZE_MAX / sizeof(*g_bspLinkHeap))
		{
			maxlinks = 0;
		}

		g_bspLinkHeapSize = maxlinks;
		if (maxlinks > 0)
		{
			g_bspLinkHeap = (bsp_link_t *)GetMemory(
				(size_t)maxlinks * sizeof(*g_bspLinkHeap));
		}
	}

	for (int index = 0; index < maxlinks && g_bspLinkHeap != NULL; ++index)
	{
		bsp_link_t *link = &g_bspLinkHeap[index];
		link->prev_ent = index > 0 ? &g_bspLinkHeap[index - 1] : NULL;
		link->next_ent = index + 1 < maxlinks ? &g_bspLinkHeap[index + 1] : NULL;
	}

	g_bspLinkFreeList = g_bspLinkHeap;
}

/*
=============
AAS_FreeBSPLinkHeap

Release the retained BSP leaf/entity-link heap for isolated teardown.
=============
*/
void AAS_FreeBSPLinkHeap(void)
{
	if (g_bspLinkHeap != NULL)
	{
		FreeMemory(g_bspLinkHeap);
	}

	g_bspLinkHeap = NULL;
	g_bspLinkFreeList = NULL;
	g_bspLinkHeapSize = 0;
}

/*
=============
AAS_AllocBSPLink

Pop one entity-to-BSP-leaf link from the retail fixed free list.
=============
*/
bsp_link_t *AAS_AllocBSPLink(void)
{
	bsp_link_t *link = g_bspLinkFreeList;
	if (link == NULL)
	{
		BotLib_Print(PRT_FATAL, "empty bsp link heap\n");
		return NULL;
	}

	g_bspLinkFreeList = link->next_ent;
	if (g_bspLinkFreeList != NULL)
	{
		g_bspLinkFreeList->prev_ent = NULL;
	}

	return link;
}

/*
=============
AAS_FreeBSPLink

Return one entity-to-BSP-leaf link to the head of the retail free list.
=============
*/
void AAS_FreeBSPLink(bsp_link_t *link)
{
	if (link == NULL)
	{
		return;
	}

	if (g_bspLinkFreeList != NULL)
	{
		g_bspLinkFreeList->prev_ent = link;
	}

	link->prev_ent = NULL;
	link->next_ent = g_bspLinkFreeList;
	link->next_leaf = NULL;
	link->prev_leaf = NULL;
	g_bspLinkFreeList = link;
}

/*
=============
AAS_AllocateConfiguredEntityArray

Allocate the setup-time retail entity table.
=============
*/
static int AAS_AllocateConfiguredEntityArray(void)
{
	if (!g_aasEntityLimitsConfigured)
	{
		return BLERR_NOERROR;
	}

	aasworld.maxEntities = g_aasConfiguredMaxEntities;
	aasworld.maxClients = g_aasConfiguredMaxClients;
	if (g_aasConfiguredMaxEntities <= 0)
	{
		return BLERR_NOERROR;
	}
	if ((size_t)g_aasConfiguredMaxEntities >
		SIZE_MAX / sizeof(aas_entity_t))
	{
		return BLERR_INVALIDENTITYNUMBER;
	}

	aasworld.entities = (aas_entity_t *)GetClearedMemory(
		(size_t)g_aasConfiguredMaxEntities * sizeof(aas_entity_t));
	if (aasworld.entities == NULL)
	{
		return BLERR_INVALIDENTITYNUMBER;
	}

	return BLERR_NOERROR;
}

/*
=============
AAS_ClearBSPData

Release the Quake II BSP data at the same point retail's BSP loader starts.
=============
*/
static void AAS_ClearBSPData(void)
{
	if (aasworld.bspEntityData != NULL)
	{
		FreeMemory(aasworld.bspEntityData);
		aasworld.bspEntityData = NULL;
	}
	aasworld.bspEntityDataSize = 0;

	if (aasworld.bspLeafEntityLists != NULL)
	{
		free(aasworld.bspLeafEntityLists);
		aasworld.bspLeafEntityLists = NULL;
		aasworld.bspLeafEntityListCount = 0U;
	}

	if (aasworld.bspModels != NULL)
	{
		FreeMemory(aasworld.bspModels);
		aasworld.bspModels = NULL;
	}

	if (aasworld.bspNodes != NULL)
	{
		FreeMemory(aasworld.bspNodes);
		aasworld.bspNodes = NULL;
	}

	if (aasworld.bspLeaves != NULL)
	{
		FreeMemory(aasworld.bspLeaves);
		aasworld.bspLeaves = NULL;
	}

	if (aasworld.bspVisibility != NULL)
	{
		FreeMemory(aasworld.bspVisibility);
		aasworld.bspVisibility = NULL;
	}

	if (aasworld.bspLeafBrushes != NULL)
	{
		FreeMemory(aasworld.bspLeafBrushes);
		aasworld.bspLeafBrushes = NULL;
	}

	if (aasworld.bspPlanes != NULL)
	{
		FreeMemory(aasworld.bspPlanes);
		aasworld.bspPlanes = NULL;
	}

	if (aasworld.bspTexInfo != NULL)
	{
		FreeMemory(aasworld.bspTexInfo);
		aasworld.bspTexInfo = NULL;
	}

	if (aasworld.bspVertexes != NULL)
	{
		FreeMemory(aasworld.bspVertexes);
		aasworld.bspVertexes = NULL;
	}

	if (aasworld.bspEdges != NULL)
	{
		FreeMemory(aasworld.bspEdges);
		aasworld.bspEdges = NULL;
	}

	if (aasworld.bspSurfEdges != NULL)
	{
		FreeMemory(aasworld.bspSurfEdges);
		aasworld.bspSurfEdges = NULL;
	}

	if (aasworld.bspFaces != NULL)
	{
		FreeMemory(aasworld.bspFaces);
		aasworld.bspFaces = NULL;
	}

	if (aasworld.bspSurfaceExtents != NULL)
	{
		free(aasworld.bspSurfaceExtents);
		aasworld.bspSurfaceExtents = NULL;
	}

	if (aasworld.bspLightData != NULL)
	{
		FreeMemory(aasworld.bspLightData);
		aasworld.bspLightData = NULL;
	}

	if (aasworld.bspBrushSides != NULL)
	{
		FreeMemory(aasworld.bspBrushSides);
		aasworld.bspBrushSides = NULL;
	}

	if (aasworld.bspBrushes != NULL)
	{
		FreeMemory(aasworld.bspBrushes);
		aasworld.bspBrushes = NULL;
	}

	aasworld.numBspModels = 0;
	aasworld.numBspNodes = 0;
	aasworld.numBspLeaves = 0;
	aasworld.numBspVisibilityClusters = 0;
	aasworld.bspVisibilitySize = 0U;
	aasworld.bspLeafBrushIndexSize = 0;
	aasworld.numBspPlanes = 0;
	aasworld.numBspTexInfo = 0;
	aasworld.numBspVertexes = 0;
	aasworld.numBspEdges = 0;
	aasworld.bspSurfEdgeIndexSize = 0;
	aasworld.numBspFaces = 0;
	aasworld.bspLightDataSize = 0;
	aasworld.numBspBrushSides = 0;
	aasworld.numBspBrushes = 0;
	aasworld.bspLeafEntityListCount = 0U;
	aasworld.bspChecksum = 0;
	aasworld.bspEntityChecksum = 0U;
}

/*
=============
AAS_ClearAASData

Release navigation data when retail's AAS loader begins after a candidate has
been found.
=============
*/
static void AAS_ClearAASData(void)
{
	AAS_RouteFrameResetDiagnostics();
	AAS_ReachabilityFrameResetDiagnostics();
	AAS_ShutDownReachabilityHeap();
	AAS_FreeAllRoutingCaches();
	AAS_ClearReachabilityData();
	free(aasworld.areacontentstravelflags);
	aasworld.areacontentstravelflags = NULL;

	if (aasworld.areaEntityLists != NULL)
	{
		FreeMemory(aasworld.areaEntityLists);
		aasworld.areaEntityLists = NULL;
	}

	if (aasworld.areas != NULL)
	{
		FreeMemory(aasworld.areas);
		aasworld.areas = NULL;
	}

	if (aasworld.bboxes != NULL)
	{
		FreeMemory(aasworld.bboxes);
		aasworld.bboxes = NULL;
	}

	if (aasworld.vertexes != NULL)
	{
		FreeMemory(aasworld.vertexes);
		aasworld.vertexes = NULL;
	}

	if (aasworld.edges != NULL)
	{
		FreeMemory(aasworld.edges);
		aasworld.edges = NULL;
	}

	if (aasworld.edgeIndex != NULL)
	{
		FreeMemory(aasworld.edgeIndex);
		aasworld.edgeIndex = NULL;
	}

	if (aasworld.faces != NULL)
	{
		FreeMemory(aasworld.faces);
		aasworld.faces = NULL;
	}

	if (aasworld.faceIndex != NULL)
	{
		FreeMemory(aasworld.faceIndex);
		aasworld.faceIndex = NULL;
	}

	if (aasworld.areasettings != NULL)
	{
		FreeMemory(aasworld.areasettings);
		aasworld.areasettings = NULL;
	}

	if (aasworld.reachability != NULL)
	{
		FreeMemory(aasworld.reachability);
		aasworld.reachability = NULL;
	}

	if (aasworld.nodes != NULL)
	{
		FreeMemory(aasworld.nodes);
		aasworld.nodes = NULL;
	}

	if (aasworld.planes != NULL)
	{
		FreeMemory(aasworld.planes);
		aasworld.planes = NULL;
	}

	if (aasworld.portals != NULL)
	{
		FreeMemory(aasworld.portals);
		aasworld.portals = NULL;
	}

	if (aasworld.portalIndex != NULL)
	{
		FreeMemory(aasworld.portalIndex);
		aasworld.portalIndex = NULL;
	}

	if (aasworld.clusters != NULL)
	{
		FreeMemory(aasworld.clusters);
		aasworld.clusters = NULL;
	}

	aasworld.numReachabilityAreas = 0;
	aasworld.aasChecksum = 0;
	aasworld.saveFile = qfalse;
	aasworld.aasFilePath[0] = '\0';
	aasworld.numAreas = 0;
	aasworld.numBBoxes = 0;
	aasworld.numVertexes = 0;
	aasworld.numEdges = 0;
	aasworld.edgeIndexSize = 0;
	aasworld.numFaces = 0;
	aasworld.faceIndexSize = 0;
	aasworld.numReachability = 0;
	aasworld.numAreaSettings = 0;
	aasworld.numNodes = 0;
	aasworld.numPlanes = 0;
	aasworld.numPortals = 0;
	aasworld.portalIndexSize = 0;
	aasworld.numClusters = 0;
	aasworld.areaEntityListCount = 0U;
	memset(aasworld.travelflagfortype, 0, sizeof(aasworld.travelflagfortype));
}

/*
=============
AAS_ClearWorld

Release every retained AAS allocation during full AAS shutdown.
=============
*/
static void AAS_ClearWorld(void)
{
	AAS_FreeEntityArray();
	AAS_ClearAASData();
	AAS_ClearBSPData();
	AAS_ClearIndexTables();
	BotMove_MoverCatalogueReset();
	AAS_SoundSubsystem_ClearMapAssets();
	memset(&aasworld, 0, sizeof(aasworld));
	TranslateEntity_SetCurrentTime(0.0f);
	TranslateEntity_SetWorldLoaded(qfalse);
}

/*
=============
AAS_LoadMap

Loads navigation data or refreshes asset indexes without disturbing a live world.
=============
*/
int AAS_LoadMap(const char *mapname,
	int modelindexes, char *modelindex[],
	int soundindexes, char *soundindex[],
	int imageindexes, char *imageindex[])
{
	if (mapname == NULL)
	{
		if (!AAS_RefreshIndexTables(modelindexes,
			modelindex,
			soundindexes,
			soundindex,
			imageindexes,
			imageindex))
		{
			BotLib_Print(PRT_WARNING,
				"AAS_LoadMap: failed to refresh asset indexes\n");
			return BLERR_INVALIDIMPORT;
		}
		if (!AAS_SoundSubsystem_RegisterMapAssets(soundindexes, soundindex))
		{
			BotLib_Print(PRT_WARNING,
				"AAS_LoadMap: failed to refresh sound asset indexes\n");
		}
		return BLERR_NOERROR;
	}

	BotMove_MoverCatalogueReset();
	if (!AAS_ReplaceIndexTables(modelindexes,
		modelindex,
		soundindexes,
		soundindex,
		imageindexes,
		imageindex))
	{
		BotLib_Print(PRT_FATAL,
			"AAS_LoadMap: failed to register asset indexes for %s\n",
			mapname);
		return AAS_ReturnMapLoadFailure(BLERR_INVALIDIMPORT);
	}

	aasworld.initialized = qfalse;
	aasworld.loaded = qfalse;
	TranslateEntity_SetWorldLoaded(qfalse);
	AAS_ResetEntityLinks();

	if (!AAS_SoundSubsystem_RegisterMapAssets(soundindexes, soundindex))
	{
		BotLib_Print(PRT_FATAL,
			"AAS_LoadMap: failed to register sound assets for %s\n",
			mapname);
		return AAS_ReturnMapLoadFailure(BLERR_INVALIDIMPORT);
	}

    strncpy(aasworld.mapName, mapname, sizeof(aasworld.mapName) - 1U);
    aasworld.mapName[sizeof(aasworld.mapName) - 1U] = '\0';

    char bspCandidate[MAX_FILEPATH];
    char aasCandidate[MAX_FILEPATH];
	aas_map_file_source_t bspSource;
	aas_map_file_source_t aasSource;

    if (!AAS_BuildPath(bspCandidate,
		sizeof(bspCandidate),
		mapname,
		".bsp"))
	{
        BotLib_Print(PRT_ERROR, "AAS_LoadMap: BSP path too long for %s\n", mapname);
		return AAS_ReturnMapLoadFailure(BLERR_NOAASFILE);
    }

	if (!AAS_DiscoverMapFile(bspCandidate, &bspSource))
    {
		BotLib_Print(PRT_FATAL,
			"couldn't find the bsp file %s\n",
			bspCandidate);
		return AAS_ReturnMapLoadFailure(BLERR_NOBSPFILE);
	}
	AAS_ClearBSPData();

	FILE *bspFile = fopen(bspSource.physicalPath, "rb");
    if (bspFile == NULL)
    {
		BotLib_Print(PRT_FATAL,
			"can't open bsp file %s\n",
			bspSource.physicalPath);
		return AAS_ReturnMapLoadFailure(BLERR_CANNOTOPENBSPFILE);
    }
	if (fseek(bspFile, bspSource.offset, SEEK_SET) != 0)
	{
		BotLib_Print(PRT_FATAL,
			"can't seek to bsp file %s\n",
			bspSource.physicalPath);
		fclose(bspFile);
		return AAS_ReturnMapLoadFailure(BLERR_CANNOTSEEKTOBSPFILE);
	}
	long bspFileSize = bspSource.length;
	if (!bspSource.archived)
	{
		bspFileSize = AAS_GetFileSize(bspFile);
	}

    q2_bsp_header_t bspHeader;
	if (bspFileSize < (long)sizeof(bspHeader) ||
		fread(&bspHeader, sizeof(bspHeader), 1U, bspFile) != 1U)
    {
		BotLib_Print(PRT_FATAL,
			"can't read header of bsp file %s\n",
			bspSource.physicalPath);
		fclose(bspFile);
		return AAS_ReturnMapLoadFailure(BLERR_CANNOTREADBSPHEADER);
    }

    bspHeader.ident = AAS_LittleLong(bspHeader.ident);
    bspHeader.version = AAS_LittleLong(bspHeader.version);
    for (int index = 0; index < Q2_BSP_LUMP_MAX; ++index)
    {
        bspHeader.lumps[index].offset = AAS_LittleLong(bspHeader.lumps[index].offset);
        bspHeader.lumps[index].length = AAS_LittleLong(bspHeader.lumps[index].length);
    }

    if (bspHeader.ident != Q2_BSP_IDENT)
    {
		BotLib_Print(PRT_FATAL,
			"%s is not an BSP file\n",
			bspSource.physicalPath);
		fclose(bspFile);
		return AAS_ReturnMapLoadFailure(BLERR_WRONGBSPFILEID);
    }

    if (bspHeader.version != Q2_BSP_VERSION)
    {
		BotLib_Print(PRT_FATAL,
			"bsp file %s is version %i, not %i\n",
			bspSource.physicalPath,
			bspHeader.version,
			Q2_BSP_VERSION);
		fclose(bspFile);
		return AAS_ReturnMapLoadFailure(BLERR_WRONGBSPFILEVERSION);
    }

    if (bspFileSize < 0L)
    {
		BotLib_Print(PRT_FATAL,
			"can't seek to bsp file %s\n",
			bspSource.physicalPath);
		fclose(bspFile);
		return AAS_ReturnMapLoadFailure(BLERR_CANNOTSEEKTOBSPFILE);
	}

	/*
	 * Retail sub_10007d30 special-cases exactly two lumps whose length may
	 * legitimately be zero and reports each at PRT_MESSAGE:
	 *   10007f7d  if (visibility filelen == 0) Print(1, "WARNGING: bsp has no
	 *             visibility data\n")      (typo is literal, string 0x1005ac70)
	 *   100080fe  if (lighting filelen == 0) Print(1, "WARNING: bsp has no
	 *             light data\n")           (string 0x1005ac20)
	 * No other lump gets zero-length handling.
	 */
	if (bspHeader.lumps[Q2_BSP_LUMP_VISIBILITY].length == 0)
	{
		BotLib_Print(PRT_MESSAGE, "WARNGING: bsp has no visibility data\n");
	}
	if (bspHeader.lumps[Q2_BSP_LUMP_LIGHTING].length == 0)
	{
		BotLib_Print(PRT_MESSAGE, "WARNING: bsp has no light data\n");
	}

	char *entityData = NULL;
	int entityLength = 0;
	int entityStatus = AAS_ReadLump(bspFile,
		&bspHeader.lumps[Q2_BSP_LUMP_ENTITIES],
		1U,
		(void **)&entityData,
		&entityLength,
		bspSource.offset,
		bspFileSize,
		BLERR_CANNOTREADBSPLUMP,
		"entity");
	if (entityStatus != BLERR_NOERROR)
	{
		fclose(bspFile);
		return AAS_ReturnMapLoadFailure(entityStatus);
	}

	aasworld.bspEntityChecksum = CRC_ProcessString((uint8_t *)entityData,
		entityLength);
	if (entityData != NULL)
	{
		AAS_ParseEntityLump(entityData, (size_t)entityLength);
		aasworld.bspEntityData = entityData;
		aasworld.bspEntityDataSize = entityLength;
	}

    fclose(bspFile);

    uint32_t bspChecksum = 0U;
    if (!AAS_ComputeFileChecksumRange(bspSource.physicalPath,
		bspSource.offset,
		bspSource.length,
		&bspChecksum))
    {
		BotLib_Print(PRT_ERROR,
			"AAS_LoadMap: failed to compute BSP checksum for %s\n",
			bspSource.physicalPath);
		return AAS_ReturnMapLoadFailure(BLERR_CANNOTREADBSPHEADER);
    }
	/* Retail 1000e9bb prints the logical "maps\<map>.bsp" name here. */
	AAS_PrintLoadedMapFile(&bspSource, qtrue);

	FILE *aasFile = NULL;
	qboolean aasDiscovered = qfalse;
	for (int candidate = 0; candidate < 2; ++candidate)
	{
		if (!AAS_BuildAASCandidatePath(aasCandidate,
			sizeof(aasCandidate),
			mapname,
			candidate))
		{
			continue;
		}

		if (!AAS_DiscoverMapFile(aasCandidate, &aasSource))
		{
			continue;
		}

		aasDiscovered = qtrue;
		break;
	}

	if (!aasDiscovered)
	{
		aasDiscovered = AAS_DiscoverZipAAS(mapname, &aasSource);
	}
	if (!aasDiscovered)
	{
		BotLib_Print(PRT_FATAL, "no AAS file available\n");
		return AAS_ReturnMapLoadFailure(BLERR_NOAASFILE);
	}
	AAS_ClearAASData();

	aasFile = fopen(aasSource.physicalPath, "rb");
	if (aasFile == NULL)
	{
		BotLib_Print(PRT_FATAL,
			"can't open %s\n",
			aasSource.physicalPath);
		return AAS_ReturnAASLoadFailure(&aasSource,
			BLERR_CANNOTOPENAASFILE);
	}
	if (fseek(aasFile, aasSource.offset, SEEK_SET) != 0)
	{
		BotLib_Print(PRT_FATAL,
			"can't seek to file %s\n",
			aasSource.physicalPath);
		fclose(aasFile);
		return AAS_ReturnAASLoadFailure(&aasSource,
			BLERR_CANNOTSEEKTOAASFILE);
	}

	long aasFileSize = aasSource.length;
	if (!aasSource.archived)
	{
		aasFileSize = AAS_GetFileSize(aasFile);
	}

    q2_aas_header_t aasHeader;
	if (aasFileSize < (long)sizeof(aasHeader) ||
		fread(&aasHeader, sizeof(aasHeader), 1U, aasFile) != 1U)
    {
		BotLib_Print(PRT_FATAL,
			"can't read header of file %s\n",
			aasSource.physicalPath);
		fclose(aasFile);
		return AAS_ReturnAASLoadFailure(&aasSource,
			BLERR_CANNOTREADAASHEADER);
    }

    aasHeader.ident = AAS_LittleLong(aasHeader.ident);
    aasHeader.version = AAS_LittleLong(aasHeader.version);
    for (int index = 0; index < Q2_AAS_LUMP_MAX; ++index)
    {
        aasHeader.lumps[index].offset = AAS_LittleLong(aasHeader.lumps[index].offset);
        aasHeader.lumps[index].length = AAS_LittleLong(aasHeader.lumps[index].length);
    }

    if (aasHeader.ident != Q2_AAS_IDENT)
    {
		BotLib_Print(PRT_FATAL,
			"%s is not an AAS file\n",
			aasSource.physicalPath);
		fclose(aasFile);
		return AAS_ReturnAASLoadFailure(&aasSource,
			BLERR_WRONGAASFILEID);
    }

    if (aasHeader.version == Q2_AAS_VERSION_OLD)
    {
		BotLib_Print(PRT_WARNING,
			"found an old AAS file, create a new AAS file\n");
    }
    else if (aasHeader.version != Q2_AAS_VERSION)
    {
		BotLib_Print(PRT_FATAL,
			"aas file %s is version %i, not %i\n",
			aasSource.physicalPath,
			aasHeader.version,
			Q2_AAS_VERSION);
		fclose(aasFile);
		return AAS_ReturnAASLoadFailure(&aasSource,
			BLERR_WRONGAASFILEVERSION);
    }

    if (aasFileSize < 0L)
    {
		BotLib_Print(PRT_FATAL,
			"can't seek to file %s\n",
			aasSource.physicalPath);
		fclose(aasFile);
		return AAS_ReturnAASLoadFailure(&aasSource,
			BLERR_CANNOTSEEKTOAASFILE);
    }

	aas_bbox_t *bboxes = NULL;
	int numBBoxes = 0;
	int result = AAS_ReadLump(aasFile,
	                          &aasHeader.lumps[Q2_AAS_LUMP_BBOXES],
	                          sizeof(aas_bbox_t),
	                          (void **)&bboxes,
	                          &numBBoxes,
	                          aasSource.offset,
	                          aasFileSize,
	                          BLERR_CANNOTREADAASLUMP,
	                          NULL);
	if (result != BLERR_NOERROR)
	{
		fclose(aasFile);
		return AAS_ReturnAASLoadFailure(&aasSource, result);
	}

	aas_vertex_t *vertexes = NULL;
	int numVertexes = 0;
	result = AAS_ReadLump(aasFile,
	                      &aasHeader.lumps[Q2_AAS_LUMP_VERTEXES],
	                      sizeof(aas_vertex_t),
	                      (void **)&vertexes,
	                      &numVertexes,
	                      aasSource.offset,
	                      aasFileSize,
	                      BLERR_CANNOTREADAASLUMP,
	                      NULL);
	if (result != BLERR_NOERROR)
	{
		FreeMemory(bboxes);
		fclose(aasFile);
		return AAS_ReturnAASLoadFailure(&aasSource, result);
	}

	aas_edge_t *edges = NULL;
	int numEdges = 0;
	result = AAS_ReadLump(aasFile,
	                      &aasHeader.lumps[Q2_AAS_LUMP_EDGES],
	                      sizeof(aas_edge_t),
	                      (void **)&edges,
	                      &numEdges,
	                      aasSource.offset,
	                      aasFileSize,
	                      BLERR_CANNOTREADAASLUMP,
	                      NULL);
	if (result != BLERR_NOERROR)
	{
		FreeMemory(bboxes);
		FreeMemory(vertexes);
		fclose(aasFile);
		return AAS_ReturnAASLoadFailure(&aasSource, result);
	}

	int *edgeIndex = NULL;
	int edgeIndexSize = 0;
	result = AAS_ReadLump(aasFile,
	                      &aasHeader.lumps[Q2_AAS_LUMP_EDGEINDEX],
	                      sizeof(int),
	                      (void **)&edgeIndex,
	                      &edgeIndexSize,
	                      aasSource.offset,
	                      aasFileSize,
	                      BLERR_CANNOTREADAASLUMP,
	                      NULL);
	if (result != BLERR_NOERROR)
	{
		FreeMemory(bboxes);
		FreeMemory(vertexes);
		FreeMemory(edges);
		fclose(aasFile);
		return AAS_ReturnAASLoadFailure(&aasSource, result);
	}

	aas_face_t *faces = NULL;
	int numFaces = 0;
	result = AAS_ReadLump(aasFile,
	                      &aasHeader.lumps[Q2_AAS_LUMP_FACES],
	                      sizeof(aas_face_t),
	                      (void **)&faces,
	                      &numFaces,
	                      aasSource.offset,
	                      aasFileSize,
	                      BLERR_CANNOTREADAASLUMP,
	                      NULL);
	if (result != BLERR_NOERROR)
	{
		FreeMemory(bboxes);
		FreeMemory(vertexes);
		FreeMemory(edges);
		FreeMemory(edgeIndex);
		fclose(aasFile);
		return AAS_ReturnAASLoadFailure(&aasSource, result);
	}

	int *faceIndex = NULL;
	int faceIndexSize = 0;
	result = AAS_ReadLump(aasFile,
	                      &aasHeader.lumps[Q2_AAS_LUMP_FACEINDEX],
	                      sizeof(int),
	                      (void **)&faceIndex,
	                      &faceIndexSize,
	                      aasSource.offset,
	                      aasFileSize,
	                      BLERR_CANNOTREADAASLUMP,
	                      NULL);
	if (result != BLERR_NOERROR)
	{
		FreeMemory(bboxes);
		FreeMemory(vertexes);
		FreeMemory(edges);
		FreeMemory(edgeIndex);
		FreeMemory(faces);
		fclose(aasFile);
		return AAS_ReturnAASLoadFailure(&aasSource, result);
	}

    aas_area_t *areas = NULL;
    int numAreas = 0;
    result = AAS_ReadLump(aasFile,
                              &aasHeader.lumps[Q2_AAS_LUMP_AREAS],
                              sizeof(aas_area_t),
                              (void **)&areas,
                              &numAreas,
	                          aasSource.offset,
                              aasFileSize,
                              BLERR_CANNOTREADAASLUMP,
                              NULL);
	if (result != BLERR_NOERROR)
	{
		FreeMemory(bboxes);
		FreeMemory(vertexes);
		FreeMemory(edges);
		FreeMemory(edgeIndex);
		FreeMemory(faces);
		FreeMemory(faceIndex);
		fclose(aasFile);
		return AAS_ReturnAASLoadFailure(&aasSource, result);
    }

    aas_areasettings_t *areasettings = NULL;
    int numAreaSettings = 0;
    result = AAS_ReadLump(aasFile,
                          &aasHeader.lumps[Q2_AAS_LUMP_AREASETTINGS],
                          sizeof(aas_areasettings_t),
                          (void **)&areasettings,
                          &numAreaSettings,
	                      aasSource.offset,
                          aasFileSize,
                          BLERR_CANNOTREADAASLUMP,
                          NULL);
    if (result != BLERR_NOERROR)
    {
		FreeMemory(bboxes);
		FreeMemory(vertexes);
		FreeMemory(edges);
		FreeMemory(edgeIndex);
		FreeMemory(faces);
		FreeMemory(faceIndex);
        FreeMemory(areas);
		fclose(aasFile);
		return AAS_ReturnAASLoadFailure(&aasSource, result);
    }

    aas_reachability_t *reachability = NULL;
    int numReachability = 0;
    result = AAS_ReadLump(aasFile,
                          &aasHeader.lumps[Q2_AAS_LUMP_REACHABILITY],
                          sizeof(aas_reachability_t),
                          (void **)&reachability,
                          &numReachability,
	                      aasSource.offset,
                          aasFileSize,
                          BLERR_CANNOTREADAASLUMP,
                          NULL);
    if (result != BLERR_NOERROR)
    {
		FreeMemory(bboxes);
		FreeMemory(vertexes);
		FreeMemory(edges);
		FreeMemory(edgeIndex);
		FreeMemory(faces);
		FreeMemory(faceIndex);
        FreeMemory(areas);
        FreeMemory(areasettings);
		fclose(aasFile);
		return AAS_ReturnAASLoadFailure(&aasSource, result);
    }

	aas_plane_t *planes = NULL;
	int numPlanes = 0;
	result = AAS_ReadLump(aasFile,
	                      &aasHeader.lumps[Q2_AAS_LUMP_PLANES],
	                      sizeof(aas_plane_t),
	                      (void **)&planes,
	                      &numPlanes,
	                      aasSource.offset,
	                      aasFileSize,
	                      BLERR_CANNOTREADAASLUMP,
	                      NULL);
    if (result != BLERR_NOERROR)
    {
		FreeMemory(bboxes);
		FreeMemory(vertexes);
		FreeMemory(edges);
		FreeMemory(edgeIndex);
		FreeMemory(faces);
		FreeMemory(faceIndex);
        FreeMemory(areas);
        FreeMemory(areasettings);
        FreeMemory(reachability);
		fclose(aasFile);
		return AAS_ReturnAASLoadFailure(&aasSource, result);
	}

    aas_node_t *nodes = NULL;
    int numNodes = 0;
    result = AAS_ReadLump(aasFile,
                          &aasHeader.lumps[Q2_AAS_LUMP_NODES],
                          sizeof(aas_node_t),
                          (void **)&nodes,
                          &numNodes,
	                      aasSource.offset,
                          aasFileSize,
                          BLERR_CANNOTREADAASLUMP,
                          NULL);
	if (result != BLERR_NOERROR)
	{
		FreeMemory(bboxes);
		FreeMemory(vertexes);
		FreeMemory(edges);
		FreeMemory(edgeIndex);
		FreeMemory(faces);
		FreeMemory(faceIndex);
        FreeMemory(areas);
        FreeMemory(areasettings);
        FreeMemory(reachability);
		FreeMemory(planes);
		fclose(aasFile);
		return AAS_ReturnAASLoadFailure(&aasSource, result);
    }

	aas_portal_t *portals = NULL;
	int numPortals = 0;
	result = AAS_ReadLump(aasFile,
	                      &aasHeader.lumps[Q2_AAS_LUMP_PORTALS],
	                      sizeof(aas_portal_t),
	                      (void **)&portals,
	                      &numPortals,
	                      aasSource.offset,
	                      aasFileSize,
	                      BLERR_CANNOTREADAASLUMP,
	                      NULL);
    if (result != BLERR_NOERROR)
    {
		FreeMemory(bboxes);
		FreeMemory(vertexes);
		FreeMemory(edges);
		FreeMemory(edgeIndex);
		FreeMemory(faces);
		FreeMemory(faceIndex);
        FreeMemory(areas);
        FreeMemory(areasettings);
        FreeMemory(reachability);
		FreeMemory(planes);
		FreeMemory(nodes);
		fclose(aasFile);
		return AAS_ReturnAASLoadFailure(&aasSource, result);
	}

	int *portalIndex = NULL;
	int portalIndexSize = 0;
	result = AAS_ReadLump(aasFile,
	                      &aasHeader.lumps[Q2_AAS_LUMP_PORTALINDEX],
	                      sizeof(int),
	                      (void **)&portalIndex,
	                      &portalIndexSize,
	                      aasSource.offset,
	                      aasFileSize,
	                      BLERR_CANNOTREADAASLUMP,
	                      NULL);
	if (result != BLERR_NOERROR)
	{
		FreeMemory(bboxes);
		FreeMemory(vertexes);
		FreeMemory(edges);
		FreeMemory(edgeIndex);
		FreeMemory(faces);
		FreeMemory(faceIndex);
		FreeMemory(areas);
		FreeMemory(areasettings);
		FreeMemory(reachability);
		FreeMemory(planes);
		FreeMemory(nodes);
		FreeMemory(portals);
		fclose(aasFile);
		return AAS_ReturnAASLoadFailure(&aasSource, result);
	}

	aas_cluster_t *clusters = NULL;
	int numClusters = 0;
	result = AAS_ReadLump(aasFile,
	                      &aasHeader.lumps[Q2_AAS_LUMP_CLUSTERS],
	                      sizeof(aas_cluster_t),
	                      (void **)&clusters,
	                      &numClusters,
	                      aasSource.offset,
	                      aasFileSize,
	                      BLERR_CANNOTREADAASLUMP,
	                      NULL);
	if (result != BLERR_NOERROR)
	{
		FreeMemory(bboxes);
		FreeMemory(vertexes);
		FreeMemory(edges);
		FreeMemory(edgeIndex);
		FreeMemory(faces);
		FreeMemory(faceIndex);
		FreeMemory(areas);
		FreeMemory(areasettings);
		FreeMemory(reachability);
		FreeMemory(planes);
		FreeMemory(nodes);
		FreeMemory(portals);
		FreeMemory(portalIndex);
		fclose(aasFile);
		return AAS_ReturnAASLoadFailure(&aasSource, result);
	}

    fclose(aasFile);

	AAS_FixupBBoxes(bboxes, numBBoxes);
	AAS_FixupVertexes(vertexes, numVertexes);
	AAS_FixupEdges(edges, numEdges);
	AAS_FixupIntArray(edgeIndex, edgeIndexSize);
	AAS_FixupFaces(faces, numFaces);
	AAS_FixupIntArray(faceIndex, faceIndexSize);
    AAS_FixupAreas(areas, numAreas);
    AAS_FixupAreaSettings(areasettings, numAreaSettings);
    AAS_FixupReachability(reachability, numReachability);
	AAS_FixupPlanes(planes, numPlanes);
    AAS_FixupNodes(nodes, numNodes);
	AAS_FixupPortals(portals, numPortals);
	AAS_FixupIntArray(portalIndex, portalIndexSize);
	AAS_FixupClusters(clusters, numClusters);

	uint32_t aasChecksum = 0U;
	if (!AAS_ComputeFileChecksumRange(aasSource.physicalPath,
		aasSource.offset,
		aasSource.length,
		&aasChecksum))
	{
		BotLib_Print(PRT_ERROR,
			"AAS_LoadMap: failed to compute checksum for %s\n",
			aasSource.physicalPath);
		FreeMemory(bboxes);
		FreeMemory(vertexes);
		FreeMemory(edges);
		FreeMemory(edgeIndex);
		FreeMemory(faces);
		FreeMemory(faceIndex);
        FreeMemory(areas);
        FreeMemory(areasettings);
        FreeMemory(reachability);
		FreeMemory(planes);
        FreeMemory(nodes);
		FreeMemory(portals);
		FreeMemory(portalIndex);
		FreeMemory(clusters);
		return AAS_ReturnAASLoadFailure(&aasSource,
			BLERR_CANNOTREADAASHEADER);
    }

	qboolean aasSuccessReported = qfalse;
	if (aasSource.zipped)
	{
		(void)unlink(aasSource.physicalPath);
		AAS_PrintLoadedMapFile(&aasSource, qfalse);
		BotLib_LogWrite("found %s in %s",
			aasSource.logicalPath,
			aasSource.archivePath);
		AAS_RestoreZipDirectory(&aasSource);
		aasSuccessReported = qtrue;
	}

	aas_bspmodel_t *bspModels = NULL;
	int numBspModels = 0;
	aas_bspnode_t *bspNodes = NULL;
	int numBspNodes = 0;
	aas_bspleaf_t *bspLeaves = NULL;
	int numBspLeaves = 0;
	unsigned short *bspLeafBrushes = NULL;
	int bspLeafBrushIndexSize = 0;
	aas_plane_t *bspPlanes = NULL;
	int numBspPlanes = 0;
	aas_bsptexinfo_t *bspTexInfo = NULL;
	int numBspTexInfo = 0;
	aas_bspbrushside_t *bspBrushSides = NULL;
	int numBspBrushSides = 0;
	aas_bspbrush_t *bspBrushes = NULL;
	int numBspBrushes = 0;
	result = AAS_LoadBSPCollisionData(&bspSource,
	                                  &bspModels,
	                                  &numBspModels,
	                                  &bspNodes,
	                                  &numBspNodes,
	                                  &bspLeaves,
	                                  &numBspLeaves,
	                                  &bspLeafBrushes,
	                                  &bspLeafBrushIndexSize,
	                                  &bspPlanes,
	                                  &numBspPlanes,
	                                  &bspTexInfo,
	                                  &numBspTexInfo,
	                                  &bspBrushSides,
	                                  &numBspBrushSides,
	                                  &bspBrushes,
	                                  &numBspBrushes);
	if (result != BLERR_NOERROR)
	{
		FreeMemory(bboxes);
		FreeMemory(vertexes);
		FreeMemory(edges);
		FreeMemory(edgeIndex);
		FreeMemory(faces);
		FreeMemory(faceIndex);
		FreeMemory(areas);
		FreeMemory(areasettings);
		FreeMemory(reachability);
		FreeMemory(planes);
		FreeMemory(nodes);
		FreeMemory(portals);
		FreeMemory(portalIndex);
		FreeMemory(clusters);
		return AAS_ReturnMapLoadFailure(result);
	}

	vec3_t *bspVertexes = NULL;
	int numBspVertexes = 0;
	aas_bspedge_t *bspEdges = NULL;
	int numBspEdges = 0;
	int *bspSurfEdges = NULL;
	int bspSurfEdgeIndexSize = 0;
	aas_bspface_t *bspFaces = NULL;
	int numBspFaces = 0;
	aas_bspsurfaceextent_t *bspSurfaceExtents = NULL;
	unsigned char *bspLightData = NULL;
	int bspLightDataSize = 0;
	result = AAS_LoadBSPPointLightData(&bspSource,
		bspModels,
		numBspModels,
		bspNodes,
		numBspNodes,
		bspPlanes,
		numBspPlanes,
		bspTexInfo,
		numBspTexInfo,
		&bspVertexes,
		&numBspVertexes,
		&bspEdges,
		&numBspEdges,
		&bspSurfEdges,
		&bspSurfEdgeIndexSize,
		&bspFaces,
		&numBspFaces,
		&bspSurfaceExtents,
		&bspLightData,
		&bspLightDataSize);
	if (result != BLERR_NOERROR)
	{
		AAS_FreeLoadedBSPCollisionData(bspModels,
			bspNodes,
			bspLeaves,
			bspLeafBrushes,
			bspPlanes,
			bspTexInfo,
			bspBrushSides,
			bspBrushes);
		FreeMemory(bboxes);
		FreeMemory(vertexes);
		FreeMemory(edges);
		FreeMemory(edgeIndex);
		FreeMemory(faces);
		FreeMemory(faceIndex);
		FreeMemory(areas);
		FreeMemory(areasettings);
		FreeMemory(reachability);
		FreeMemory(planes);
		FreeMemory(nodes);
		FreeMemory(portals);
		FreeMemory(portalIndex);
		FreeMemory(clusters);
		return AAS_ReturnMapLoadFailure(result);
	}
	unsigned char *bspVisibility = NULL;
	size_t bspVisibilitySize = 0U;
	int numBspVisibilityClusters = 0;
	(void)AAS_LoadBSPVisibilityData(&bspSource,
		&bspVisibility,
		&bspVisibilitySize,
		&numBspVisibilityClusters);
	if (!aasSuccessReported)
	{
		/* Retail 1000eb0f prints the resolved physical path for the AAS file. */
		AAS_PrintLoadedMapFile(&aasSource, qfalse);
	}

	if (aasSource.zipped)
	{
		aasworld.aasFilePath[0] = '\0';
	}
	else
	{
		const char *storedAASPath = aasSource.archived
			? aasSource.logicalPath
			: aasSource.physicalPath;
		strncpy(aasworld.aasFilePath,
			storedAASPath,
			sizeof(aasworld.aasFilePath) - 1U);
		aasworld.aasFilePath[sizeof(aasworld.aasFilePath) - 1U] = '\0';
	}

    aasworld.bspChecksum = (int)bspChecksum;
    aasworld.aasChecksum = (int)aasChecksum;
	aasworld.numBspModels = numBspModels;
	aasworld.bspModels = bspModels;
	aasworld.numBspNodes = numBspNodes;
	aasworld.bspNodes = bspNodes;
	aasworld.numBspLeaves = numBspLeaves;
	aasworld.bspLeaves = bspLeaves;
	aasworld.numBspVisibilityClusters = numBspVisibilityClusters;
	aasworld.bspVisibilitySize = bspVisibilitySize;
	aasworld.bspVisibility = bspVisibility;
	aasworld.bspLeafBrushIndexSize = bspLeafBrushIndexSize;
	aasworld.bspLeafBrushes = bspLeafBrushes;
	aasworld.numBspPlanes = numBspPlanes;
	aasworld.bspPlanes = bspPlanes;
	aasworld.numBspTexInfo = numBspTexInfo;
	aasworld.bspTexInfo = bspTexInfo;
	aasworld.numBspVertexes = numBspVertexes;
	aasworld.bspVertexes = bspVertexes;
	aasworld.numBspEdges = numBspEdges;
	aasworld.bspEdges = bspEdges;
	aasworld.bspSurfEdgeIndexSize = bspSurfEdgeIndexSize;
	aasworld.bspSurfEdges = bspSurfEdges;
	aasworld.numBspFaces = numBspFaces;
	aasworld.bspFaces = bspFaces;
	aasworld.bspSurfaceExtents = bspSurfaceExtents;
	aasworld.bspLightDataSize = bspLightDataSize;
	aasworld.bspLightData = bspLightData;
	aasworld.numBspBrushSides = numBspBrushSides;
	aasworld.bspBrushSides = bspBrushSides;
	aasworld.numBspBrushes = numBspBrushes;
	aasworld.bspBrushes = bspBrushes;
    aasworld.numAreas = numAreas;
    aasworld.areas = areas;
	aasworld.numBBoxes = numBBoxes;
	aasworld.bboxes = bboxes;
	aasworld.numVertexes = numVertexes;
	aasworld.vertexes = vertexes;
	aasworld.numEdges = numEdges;
	aasworld.edges = edges;
	aasworld.edgeIndexSize = edgeIndexSize;
	aasworld.edgeIndex = edgeIndex;
	aasworld.numFaces = numFaces;
	aasworld.faces = faces;
	aasworld.faceIndexSize = faceIndexSize;
	aasworld.faceIndex = faceIndex;
    aasworld.numReachability = numReachability;
    aasworld.reachability = reachability;
    aasworld.numAreaSettings = numAreaSettings;
    aasworld.areasettings = areasettings;
    aasworld.numNodes = numNodes;
    aasworld.nodes = nodes;
	aasworld.numPlanes = numPlanes;
	aasworld.planes = planes;
	aasworld.numPortals = numPortals;
	aasworld.portals = portals;
	aasworld.portalIndexSize = portalIndexSize;
	aasworld.portalIndex = portalIndex;
	aasworld.numClusters = numClusters;
	aasworld.clusters = clusters;
    aasworld.entitiesValid = qfalse;
	aasworld.loaded = qtrue;
    aasworld.initialized = qfalse;

	AAS_InitTravelFlagFromType();
	AAS_InitAreaContentsTravelFlags();
	AAS_InitBSPLinkHeap();
	AAS_InitAASLinkHeap();
	int reachStatus = AAS_PrepareReachability();
	if (reachStatus != BLERR_NOERROR)
	{
		AAS_ClearAASData();
		return AAS_ReturnMapLoadFailure(reachStatus);
    }

    int areaStatus = AAS_EnsureAreaListArray();
	if (areaStatus != BLERR_NOERROR)
	{
		AAS_ClearAASData();
		return AAS_ReturnMapLoadFailure(areaStatus);
    }

    AAS_InvalidateRouteCache();

    AAS_FrameSynchronise(0.0f);
    TranslateEntity_SetWorldLoaded(qtrue);
    return BLERR_NOERROR;
}

/*
=============
AAS_ConfigureEntityLimits

Store retail entity limits, initialise sound state, then invalidate the fixed
entity table in the same order as sub_1000edc0.
=============
*/
int AAS_ConfigureEntityLimits(int maxentities, int maxclients)
{
	g_aasConfiguredMaxEntities = maxentities;
	g_aasConfiguredMaxClients = maxclients;
	g_aasEntityLimitsConfigured = qtrue;

	AAS_FreeEntityArray();
	int status = AAS_AllocateConfiguredEntityArray();
	if (status != BLERR_NOERROR)
	{
		return status;
	}

	status = AAS_SoundSubsystem_Init();
	AAS_InvalidateEntities();
	return status;
}

/*
=============
AAS_SwapAASFileData

Swap every persisted AAS lump between host and little-endian representation.
=============
*/
static void AAS_SwapAASFileData(void)
{
	AAS_FixupBBoxes(aasworld.bboxes, aasworld.numBBoxes);
	AAS_FixupVertexes(aasworld.vertexes, aasworld.numVertexes);
	AAS_FixupPlanes(aasworld.planes, aasworld.numPlanes);
	AAS_FixupEdges(aasworld.edges, aasworld.numEdges);
	AAS_FixupIntArray(aasworld.edgeIndex, aasworld.edgeIndexSize);
	AAS_FixupFaces(aasworld.faces, aasworld.numFaces);
	AAS_FixupIntArray(aasworld.faceIndex, aasworld.faceIndexSize);
	AAS_FixupAreas(aasworld.areas, aasworld.numAreas);
	AAS_FixupAreaSettings(aasworld.areasettings, aasworld.numAreaSettings);
	AAS_FixupReachability(aasworld.reachability, aasworld.numReachability);
	AAS_FixupNodes(aasworld.nodes, aasworld.numNodes);
	AAS_FixupPortals(aasworld.portals, aasworld.numPortals);
	AAS_FixupIntArray(aasworld.portalIndex, aasworld.portalIndexSize);
	AAS_FixupClusters(aasworld.clusters, aasworld.numClusters);
}

/*
=============
AAS_WriteAASLump

Append one retail AAS lump and record its little-endian offset and length.
=============
*/
static int AAS_WriteAASLump(FILE *file,
	q2_aas_header_t *header,
	q2_aas_lump_id_t lumpnum,
	const void *data,
	size_t length)
{
	long offset = ftell(file);
	if (offset < 0 || offset > INT32_MAX || length > INT32_MAX)
	{
		BotLib_Print(PRT_ERROR, "AAS_WriteAASLump: invalid lump bounds\n");
		return qfalse;
	}

	header->lumps[lumpnum].offset = AAS_LittleLong((int32_t)offset);
	header->lumps[lumpnum].length = AAS_LittleLong((int32_t)length);
	if (length > 0U &&
		(data == NULL || fwrite(data, length, 1U, file) != 1U))
	{
		BotLib_Print(PRT_ERROR,
			"AAS_WriteAASLump: error writing lump %d\n",
			(int)lumpnum);
		return qfalse;
	}
	return qtrue;
}

/*
=============
AAS_WriteAASFile

Write the 14 retail version-3 AAS lumps and then rewrite the completed header.
=============
*/
int AAS_WriteAASFile(const char *filename)
{
	if (filename == NULL || *filename == '\0')
	{
		return qfalse;
	}

	BotLib_Print(PRT_MESSAGE, "writing %s\n", filename);
	FILE *file = fopen(filename, "wb");
	if (file == NULL)
	{
		BotLib_Print(PRT_ERROR, "error opening %s\n", filename);
		return qfalse;
	}

	q2_aas_header_t header;
	memset(&header, 0, sizeof(header));
	header.ident = AAS_LittleLong(Q2_AAS_IDENT);
	header.version = AAS_LittleLong(Q2_AAS_VERSION);
	if (fwrite(&header, sizeof(header), 1U, file) != 1U)
	{
		fclose(file);
		return qfalse;
	}

	AAS_SwapAASFileData();
	int success =
		AAS_WriteAASLump(file,
			&header,
			Q2_AAS_LUMP_BBOXES,
			aasworld.bboxes,
			(size_t)aasworld.numBBoxes * sizeof(aas_bbox_t)) &&
		AAS_WriteAASLump(file,
			&header,
			Q2_AAS_LUMP_VERTEXES,
			aasworld.vertexes,
			(size_t)aasworld.numVertexes * sizeof(aas_vertex_t)) &&
		AAS_WriteAASLump(file,
			&header,
			Q2_AAS_LUMP_PLANES,
			aasworld.planes,
			(size_t)aasworld.numPlanes * sizeof(aas_plane_t)) &&
		AAS_WriteAASLump(file,
			&header,
			Q2_AAS_LUMP_EDGES,
			aasworld.edges,
			(size_t)aasworld.numEdges * sizeof(aas_edge_t)) &&
		AAS_WriteAASLump(file,
			&header,
			Q2_AAS_LUMP_EDGEINDEX,
			aasworld.edgeIndex,
			(size_t)aasworld.edgeIndexSize * sizeof(int)) &&
		AAS_WriteAASLump(file,
			&header,
			Q2_AAS_LUMP_FACES,
			aasworld.faces,
			(size_t)aasworld.numFaces * sizeof(aas_face_t)) &&
		AAS_WriteAASLump(file,
			&header,
			Q2_AAS_LUMP_FACEINDEX,
			aasworld.faceIndex,
			(size_t)aasworld.faceIndexSize * sizeof(int)) &&
		AAS_WriteAASLump(file,
			&header,
			Q2_AAS_LUMP_AREAS,
			aasworld.areas,
			(size_t)aasworld.numAreas * sizeof(aas_area_t)) &&
		AAS_WriteAASLump(file,
			&header,
			Q2_AAS_LUMP_AREASETTINGS,
			aasworld.areasettings,
			(size_t)aasworld.numAreaSettings * sizeof(aas_areasettings_t)) &&
		AAS_WriteAASLump(file,
			&header,
			Q2_AAS_LUMP_REACHABILITY,
			aasworld.reachability,
			(size_t)aasworld.numReachability * sizeof(aas_reachability_t)) &&
		AAS_WriteAASLump(file,
			&header,
			Q2_AAS_LUMP_NODES,
			aasworld.nodes,
			(size_t)aasworld.numNodes * sizeof(aas_node_t)) &&
		AAS_WriteAASLump(file,
			&header,
			Q2_AAS_LUMP_PORTALS,
			aasworld.portals,
			(size_t)aasworld.numPortals * sizeof(aas_portal_t)) &&
		AAS_WriteAASLump(file,
			&header,
			Q2_AAS_LUMP_PORTALINDEX,
			aasworld.portalIndex,
			(size_t)aasworld.portalIndexSize * sizeof(int)) &&
		AAS_WriteAASLump(file,
			&header,
			Q2_AAS_LUMP_CLUSTERS,
			aasworld.clusters,
			(size_t)aasworld.numClusters * sizeof(aas_cluster_t));

	if (success && fseek(file, 0L, SEEK_SET) == 0)
	{
		success = fwrite(&header, sizeof(header), 1U, file) == 1U;
	}
	else
	{
		success = qfalse;
	}

	AAS_SwapAASFileData();
	if (fclose(file) != 0)
	{
		success = qfalse;
	}
	return success;
}

/*
=============
AAS_Shutdown

Tear the AAS world down and report the shutdown unconditionally.
=============
*/
void AAS_Shutdown(void)
{
	TranslateEntity_SetCurrentTime(0.0f);
	TranslateEntity_SetWorldLoaded(qfalse);
	AAS_ClearWorld();
	AAS_FreeAASLinkHeap();
	AAS_SoundSubsystem_ResetState();
	g_aasEntityLimitsConfigured = qfalse;
	g_aasConfiguredMaxEntities = 0;
	g_aasConfiguredMaxClients = 0;
	g_aasLibraryInitialized = qfalse;

	/*
	 * Retail sub_1000ee30 prints last and with no predicate at all:
	 * "1000ee80  return data_10063fe8(1, "AAS shutdown.\n")", reached after the
	 * memset at 1000ee6f has already zeroed both the loaded and initialized
	 * flags, so no guard could have referenced them. A shutdown without a map,
	 * or a second shutdown in a row, still prints.
	 */
	BotLib_Print(PRT_MESSAGE, "AAS shutdown.\n");
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
	if (g_aasEntityLimitsConfigured)
	{
		return BLERR_INVALIDENTITYNUMBER;
	}

	size_t previousCount = (size_t)aasworld.maxEntities;
	size_t requiredCount = (size_t)ent + 1U;
	if (requiredCount > (size_t)INT_MAX ||
		requiredCount > SIZE_MAX / sizeof(aas_entity_t))
	{
		return BLERR_INVALIDENTITYNUMBER;
	}

	aas_entity_t *resized = (aas_entity_t *)GetClearedMemory(
		requiredCount * sizeof(aas_entity_t));
	if (resized == NULL)
	{
		return BLERR_INVALIDENTITYNUMBER;
	}

	if (aasworld.entities != NULL && previousCount > 0U)
	{
		memcpy(resized,
			aasworld.entities,
			previousCount * sizeof(aas_entity_t));
	}
	FreeMemory(aasworld.entities);
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
    if (aasworld.numAreas < 0)
    {
        return BLERR_INVALIDENTITYNUMBER;
    }

    size_t desired = (size_t)aasworld.numAreas;

    if (aasworld.areaEntityLists != NULL && aasworld.areaEntityListCount == desired)
    {
        return BLERR_NOERROR;
    }

    if (aasworld.areaEntityLists != NULL)
    {
        FreeMemory(aasworld.areaEntityLists);
        aasworld.areaEntityLists = NULL;
        aasworld.areaEntityListCount = 0U;
    }

    if (desired > SIZE_MAX / sizeof(*aasworld.areaEntityLists))
    {
        return BLERR_INVALIDENTITYNUMBER;
    }

    aasworld.areaEntityLists = (aas_link_t **)GetClearedMemory(
        desired * sizeof(*aasworld.areaEntityLists));
    if (aasworld.areaEntityLists == NULL && desired > 0U)
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
		AAS_FreeAASLink(link);
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

	aas_link_t *link = AAS_AllocAASLink();
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

/*
=============
AAS_EnsureBSPLeafEntityListArray

Allocate the head table used by the retail BSP leaf/entity link lists.
=============
*/
static int AAS_EnsureBSPLeafEntityListArray(void)
{
	if (aasworld.numBspLeaves <= 0 || aasworld.bspLeaves == NULL)
	{
		return BLERR_NOERROR;
	}

	size_t desired = (size_t)aasworld.numBspLeaves;
	if (aasworld.bspLeafEntityLists != NULL &&
		aasworld.bspLeafEntityListCount == desired)
	{
		return BLERR_NOERROR;
	}

	if (aasworld.entities != NULL)
	{
		for (int entnum = 0; entnum < aasworld.maxEntities; ++entnum)
		{
			AAS_UnlinkEntityFromBSPLeaves(&aasworld.entities[entnum]);
		}
	}
	free(aasworld.bspLeafEntityLists);
	aasworld.bspLeafEntityLists =
		(bsp_link_t **)calloc(desired, sizeof(*aasworld.bspLeafEntityLists));
	if (aasworld.bspLeafEntityLists == NULL)
	{
		aasworld.bspLeafEntityListCount = 0U;
		return BLERR_INVALIDENTITYNUMBER;
	}

	aasworld.bspLeafEntityListCount = desired;
	return BLERR_NOERROR;
}

/*
=============
AAS_UnlinkEntityFromBSPLeaves

Remove and release every BSP leaf link owned by one entity.
=============
*/
void AAS_UnlinkEntityFromBSPLeaves(aas_entity_t *entity)
{
	if (entity == NULL)
	{
		return;
	}

	bsp_link_t *link = entity->leaves;
	while (link != NULL)
	{
		bsp_link_t *next = link->next_leaf;
		if (link->leafnum >= 0 && aasworld.bspLeafEntityLists != NULL &&
			(size_t)link->leafnum < aasworld.bspLeafEntityListCount)
		{
			if (link->prev_ent != NULL)
			{
				link->prev_ent->next_ent = link->next_ent;
			}
			else
			{
				aasworld.bspLeafEntityLists[link->leafnum] = link->next_ent;
			}
			if (link->next_ent != NULL)
			{
				link->next_ent->prev_ent = link->prev_ent;
			}
		}
		AAS_FreeBSPLink(link);
		link = next;
	}

	entity->leaves = NULL;
}

/*
=============
AAS_LinkEntityToBSPLeaf

Insert one entity into both the specified leaf's and entity's link chains.
=============
*/
static int AAS_LinkEntityToBSPLeaf(aas_entity_t *entity, int leafnum)
{
	if (entity == NULL || leafnum < 0 || aasworld.bspLeafEntityLists == NULL ||
		(size_t)leafnum >= aasworld.bspLeafEntityListCount)
	{
		return BLERR_INVALIDENTITYNUMBER;
	}

	bsp_link_t *link = AAS_AllocBSPLink();
	if (link == NULL)
	{
		return BLERR_INVALIDENTITYNUMBER;
	}

	link->entnum = entity->number;
	link->leafnum = leafnum;

	link->prev_leaf = NULL;
	link->next_leaf = entity->leaves;
	if (entity->leaves != NULL)
	{
		entity->leaves->prev_leaf = link;
	}
	entity->leaves = link;

	link->prev_ent = NULL;
	link->next_ent = aasworld.bspLeafEntityLists[leafnum];
	if (link->next_ent != NULL)
	{
		link->next_ent->prev_ent = link;
	}
	aasworld.bspLeafEntityLists[leafnum] = link;

	return BLERR_NOERROR;
}

/*
=============
AAS_LinkEntityToBSPLeaves

Walk model zero's BSP tree and attach an entity to every touched leaf.
=============
*/
static int AAS_LinkEntityToBSPLeaves(aas_entity_t *entity,
	                                  const vec3_t absmins,
	                                  const vec3_t absmaxs)
{
	AAS_UnlinkEntityFromBSPLeaves(entity);
	if (entity == NULL || absmins == NULL || absmaxs == NULL ||
		!AAS_BSPModelValid(0))
	{
		return BLERR_NOERROR;
	}

	int status = AAS_EnsureBSPLeafEntityListArray();
	if (status != BLERR_NOERROR || aasworld.bspLeafEntityLists == NULL)
	{
		return status;
	}

	int stack[AAS_AREA_STACK_SIZE];
	int stacktop = 0;
	stack[stacktop++] = aasworld.bspModels[0].headnode;
	while (stacktop > 0)
	{
		int nodenum = stack[--stacktop];
		if (nodenum < 0)
		{
			int leafnum = -1 - nodenum;
			if (leafnum >= 0 && leafnum < aasworld.numBspLeaves)
			{
				status = AAS_LinkEntityToBSPLeaf(entity, leafnum);
				if (status != BLERR_NOERROR)
				{
					return BLERR_NOERROR;
				}
			}
			continue;
		}

		if (aasworld.bspNodes == NULL || nodenum >= aasworld.numBspNodes)
		{
			continue;
		}

		const aas_bspnode_t *node = &aasworld.bspNodes[nodenum];
		if (aasworld.bspPlanes == NULL || node->planenum < 0 ||
			node->planenum >= aasworld.numBspPlanes)
		{
			continue;
		}

		int sides = AAS_BoxOnPlaneSide(absmins,
		                               absmaxs,
		                               &aasworld.bspPlanes[node->planenum]);
		if ((sides & 2) != 0)
		{
			if (stacktop >= AAS_AREA_STACK_SIZE)
			{
				AAS_UnlinkEntityFromBSPLeaves(entity);
				return BLERR_INVALIDENTITYNUMBER;
			}
			stack[stacktop++] = node->children[1];
		}
		if ((sides & 1) != 0)
		{
			if (stacktop >= AAS_AREA_STACK_SIZE)
			{
				AAS_UnlinkEntityFromBSPLeaves(entity);
				return BLERR_INVALIDENTITYNUMBER;
			}
			stack[stacktop++] = node->children[0];
		}
	}

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

/*
=============
AAS_LinkEntityToComputedAreas

Link a changed entity to every AAS leaf touched by its absolute bounds.
=============
*/
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

	int maxareas = aasworld.numAreas;
	int *areas = (int *)malloc((size_t)maxareas * sizeof(int));
	if (areas == NULL)
	{
		return BLERR_INVALIDENTITYNUMBER;
	}

	int occupied = AAS_BBoxAreas(absmins, absmaxs, areas, maxareas);
	int linked = 0;
	for (int index = 0; index < occupied; ++index)
	{
		int areanum = areas[index];
		status = AAS_LinkEntityToArea(entity, areanum);
		if (status != BLERR_NOERROR)
		{
			occupied = linked;
			break;
		}

		AAS_SetEntityAreaBit(entity, areanum);
		linked++;
	}
	free(areas);

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

/*
=============
AAS_UpdateEntity
=============
*/
int AAS_UpdateEntity(int ent, const AASEntityFrame *state)
{
    if (!aasworld.loaded)
    {
        BotlibLog(PRT_MESSAGE, "AAS_UpdateEntity: not loaded\n");
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
		AAS_UnlinkEntityFromBSPLeaves(entity);
        AAS_ResetEntityBitset(entity);
        entity->inuse = qfalse;
        entity->outsideAllAreas = qtrue;
        entity->lastOutsideUpdate = aasworld.time;
        entity->lastUpdateTime = 0.0f;
        entity->deltaTime = 0.0f;
        entity->isMover = qfalse;
        return BLERR_NOERROR;
    }

    entity->inuse = qtrue;
    entity->solid = state->solid;
    entity->isMover = state->is_mover ? qtrue : qfalse;
    entity->modelindex = state->modelindex;
    entity->modelindex2 = state->modelindex2;
    entity->modelindex3 = state->modelindex3;
    entity->modelindex4 = state->modelindex4;
    entity->frame = state->frame;
    entity->skinnum = state->skinnum;
    entity->effects = state->effects;
    entity->renderfx = state->renderfx;
    entity->sound = state->sound;
    entity->eventid = state->event_id;

    VectorCopy(state->angles, entity->angles);
    VectorCopy(state->origin, entity->origin);
    VectorCopy(state->old_origin, entity->old_origin);
    VectorCopy(state->previous_origin, entity->previousOrigin);
    VectorCopy(state->mins, entity->mins);
    VectorCopy(state->maxs, entity->maxs);

    entity->lastUpdateTime = state->last_update_time;
    entity->deltaTime = state->frame_delta;

	if (ent > 0 &&
		(state->angles_dirty || state->bounds_dirty || state->origin_dirty))
    {
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

		linkStatus = AAS_LinkEntityToBSPLeaves(entity, absmins, absmaxs);
		if (linkStatus != BLERR_NOERROR)
		{
			return linkStatus;
		}
    }

	/*
	 * Retail sub_1000a920 does no routing work at all. Its whole mover path is
	 * relinking - "1000aafd j_sub_1001c3f0(esi[0x1f])", "1000ab14 esi[0x1f] =
	 * j_sub_1001c620(...)", "1000ab1e j_sub_10006090(esi[0x20])", "1000ab38
	 * esi[0x20] = j_sub_10006210(...)", "1000ab47 return 0" - so a door, plat
	 * or elevator moving never touches the routing caches. The only routine
	 * that tears the cache head tables down is sub_10019550, and its only two
	 * call sites in the image are 1000ed30 (AAS_LoadMap) and 1000ee30
	 * (AAS_Shutdown). Retail keeps both cache families alive for the whole map.
	 */

    aasworld.entitiesValid = qtrue;
    return BLERR_NOERROR;
}
