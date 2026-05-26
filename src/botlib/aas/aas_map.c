#include "aas_map.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aas_local.h"
#include "aas_sound.h"
#include "botlib/ai_move/mover_catalogue.h"
#include "botlib/common/l_log.h"
#include "botlib/interface/botlib_interface.h"
#include "q2bridge/bridge.h"

static void AAS_UnlinkEntityFromAreas(aas_entity_t *entity);
static int AAS_LinkEntityToComputedAreas(aas_entity_t *entity, const vec3_t absmins, const vec3_t absmaxs);
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
static void AAS_ClearWorld(void);
static void AAS_ParseEntityLump(const char *data, size_t length);

#define AAS_AREA_STACK_SIZE 128
#define AAS_TRACEPLANE_EPSILON 0.125f
#define AAS_TRACE_ON_EPSILON 0.0f
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

typedef struct aas_bsptrace_stack_s
{
	vec3_t start;
	vec3_t end;
	float startfraction;
	float endfraction;
	int nodenum;
} aas_bsptrace_stack_t;

/*
 * Global AAS world state.  The original DLL zeroed the data_100667e0 block
 * during shutdown; the struct layout mirrors that memory region.
 */
aas_world_t aasworld = {0};
static qboolean g_aasLibraryInitialized = qfalse;

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

Return the BSP contents at a point through the Quake II collision import.
=============
*/
int AAS_PointContents(const vec3_t point)
{
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
AAS_EntityModelNum

Return the raw model index through the Q3-compatible helper name.
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

	return aasworld.entities[entnum].modelindex;
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

Find a live mover entity by brush-model number and copy its origin.
=============
*/
int AAS_OriginOfMoverWithModelNum(int modelnum, vec3_t origin)
{
	if (origin == NULL || modelnum <= 0 || aasworld.entities == NULL)
	{
		return qfalse;
	}

	for (int entnum = 0; entnum < aasworld.maxEntities; ++entnum)
	{
		const aas_entity_t *entity = &aasworld.entities[entnum];
		if (!AAS_EntityIsMover(entity))
		{
			continue;
		}

		if (AAS_ModelNumForEntity(entnum) == modelnum)
		{
			VectorCopy(entity->origin, origin);
			return qtrue;
		}
	}

	return qfalse;
}

/*
=============
AAS_PresenceTypeBoundingBox

Return the retail AAS bounding box for a presence type.
=============
*/
void AAS_PresenceTypeBoundingBox(int presencetype, vec3_t mins, vec3_t maxs)
{
	if (mins == NULL || maxs == NULL)
	{
		return;
	}

	if (presencetype == PRESENCE_NORMAL)
	{
		VectorSet(mins, -15.0f, -15.0f, -24.0f);
		VectorSet(maxs, 15.0f, 15.0f, 32.0f);
		return;
	}

	if (presencetype == PRESENCE_CROUCH)
	{
		VectorSet(mins, -15.0f, -15.0f, -24.0f);
		VectorSet(maxs, 15.0f, 15.0f, 8.0f);
		return;
	}

	BotLib_Print(PRT_FATAL, "AAS_PresenceTypeBoundingBox: unknown presence type\n");
	VectorSet(mins, -15.0f, -15.0f, -24.0f);
	VectorSet(maxs, 15.0f, 15.0f, 8.0f);
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

	for (int areanum = 1; areanum <= aasworld.numAreas; ++areanum)
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
	if (presencetype == PRESENCE_NONE)
	{
		return qtrue;
	}

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
AAS_TraceContentMaskBlocksEntity

Check whether the trace mask can collide with a botlib solid entity.
=============
*/
static qboolean AAS_TraceContentMaskBlocksEntity(int contentmask)
{
	return (contentmask & (CONTENTS_SOLID | CONTENTS_PLAYERCLIP |
	                       CONTENTS_MONSTER | CONTENTS_MONSTERCLIP)) != 0;
}

/*
=============
AAS_TraceEntitySolid

Check whether an entity should participate in reconstructed AAS entity traces.
=============
*/
static qboolean AAS_TraceEntitySolid(const aas_entity_t *entity, int contentmask)
{
	if (entity == NULL || !entity->inuse)
	{
		return qfalse;
	}
	if (!AAS_TraceContentMaskBlocksEntity(contentmask))
	{
		return qfalse;
	}

	return entity->solid == SOLID_BBOX || entity->solid == SOLID_BSP;
}

/*
=============
AAS_PointInsideBounds

Return true when a point is inside or on a bounding box.
=============
*/
static qboolean AAS_PointInsideBounds(const vec3_t point, const vec3_t mins, const vec3_t maxs)
{
	if (point == NULL || mins == NULL || maxs == NULL)
	{
		return qfalse;
	}

	for (int axis = 0; axis < 3; ++axis)
	{
		if (point[axis] < mins[axis] || point[axis] > maxs[axis])
		{
			return qfalse;
		}
	}

	return qtrue;
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
	if (!AAS_TraceEntitySolid(entity, contentmask) ||
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
	local.contents = CONTENTS_SOLID;

	if (AAS_PointInsideBounds(start, expandedmins, expandedmaxs))
	{
		local.startsolid = qtrue;
		local.fraction = 0.0f;
		local.allsolid = AAS_PointInsideBounds(end, expandedmins, expandedmaxs);
		VectorCopy(start, local.endpos);
		if (local.fraction < trace->fraction || !trace->startsolid)
		{
			*trace = local;
			return qtrue;
		}
		return qfalse;
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
static void AAS_RotateLocalVector(const vec3_t local, vec3_t axis[3], vec3_t world)
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
		if (aasworld.bspModels != NULL)
		{
			BotLib_Print(PRT_FATAL,
			             "AAS_BSPModelMinsMaxs: modelnum %d out of range [0-%d]",
			             modelnum,
			             aasworld.numBspModels);
		}
		return;
	}

	const aas_bspmodel_t *model = &aasworld.bspModels[modelnum];
	if (origin != NULL)
	{
		VectorCopy(model->origin, origin);
	}

	if (mins == NULL || maxs == NULL)
	{
		return;
	}

	if (angles == NULL || AAS_VectorIsZero(angles))
	{
		VectorCopy(model->mins, mins);
		VectorCopy(model->maxs, maxs);
		return;
	}

	vec3_t axis[3];
	AAS_AnglesToAxis(angles, axis);

	for (int component = 0; component < 3; ++component)
	{
		mins[component] = 999999.0f;
		maxs[component] = -999999.0f;
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
			if (rotated[component] < mins[component])
			{
				mins[component] = rotated[component];
			}
			if (rotated[component] > maxs[component])
			{
				maxs[component] = rotated[component];
			}
		}
	}
}

/*
=============
AAS_BSPTracePlaneOffset

Calculate the plane expansion offset for tracing an axial box through a brush.
=============
*/
static float AAS_BSPTracePlaneOffset(const aas_plane_t *plane, const vec3_t mins, const vec3_t maxs)
{
	if (plane == NULL)
	{
		return 0.0f;
	}

	float offset = 0.0f;
	for (int component = 0; component < 3; ++component)
	{
		if (plane->normal[component] < 0.0f)
		{
			offset += plane->normal[component] * ((maxs != NULL) ? maxs[component] : 0.0f);
		}
		else
		{
			offset += plane->normal[component] * ((mins != NULL) ? mins[component] : 0.0f);
		}
	}

	return offset;
}

/*
=============
AAS_CopyBSPTracePlane

Copy a BSP plane into a trace result.
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
	trace->plane.signbits = AAS_CPlaneSignBits(plane->normal);
}

/*
=============
AAS_TraceThroughBSPBrush

Clip a local-space box sweep through one convex BSP brush.
=============
*/
static qboolean AAS_TraceThroughBSPBrush(const aas_bspbrush_t *brush,
                                         const vec3_t start,
                                         const vec3_t mins,
                                         const vec3_t maxs,
                                         const vec3_t end,
                                         int contentmask,
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
	int clipside = -1;
	qboolean startsout = qfalse;
	qboolean endsout = qfalse;

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
		float expandedDist = plane->dist + AAS_BSPTracePlaneOffset(plane, mins, maxs);
		float startdist = DotProduct(start, plane->normal) - expandedDist;
		float enddist = DotProduct(end, plane->normal) - expandedDist;

		if (startdist > 0.0f)
		{
			startsout = qtrue;
		}
		if (enddist > 0.0f)
		{
			endsout = qtrue;
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
		if (!endsout)
		{
			trace->allsolid = qtrue;
		}
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
		float expandedDist = plane->dist + AAS_BSPTracePlaneOffset(plane, mins, maxs);
		float front = DotProduct(current.start, plane->normal) - expandedDist;
		float back = DotProduct(current.end, plane->normal) - expandedDist;
		if (front >= 0.0f && back >= 0.0f)
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
		if (front < 0.0f && back < 0.0f)
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

	qboolean rotated = angles != NULL && !AAS_VectorIsZero(angles);
	vec3_t axis[3];
	if (rotated)
	{
		AAS_AnglesToAxis(angles, axis);
	}

	vec3_t localStart;
	vec3_t localEnd;
	VectorSubtract(start, totalOrigin, localStart);
	VectorSubtract(end, totalOrigin, localEnd);
	if (rotated)
	{
		vec3_t delta;
		VectorCopy(localStart, delta);
		AAS_WorldToLocalVector(delta, axis, localStart);
		VectorCopy(localEnd, delta);
		AAS_WorldToLocalVector(delta, axis, localEnd);
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
	                      localStart,
	                      traceMins,
	                      traceMaxs,
	                      localEnd,
	                      contentmask,
	                      &trace);

	vec3_t localTraceEnd;
	if (trace.startsolid)
	{
		VectorCopy(localStart, localTraceEnd);
		trace.fraction = 0.0f;
	}
	else
	{
		VectorSubtract(localEnd, localStart, localTraceEnd);
		VectorMA(localStart, trace.fraction, localTraceEnd, localTraceEnd);
	}

	if (rotated)
	{
		vec3_t worldEnd;
		AAS_RotateLocalVector(localTraceEnd, axis, worldEnd);
		VectorAdd(worldEnd, totalOrigin, trace.endpos);

		vec3_t worldNormal;
		AAS_RotateLocalVector(trace.plane.normal, axis, worldNormal);
		VectorCopy(worldNormal, trace.plane.normal);
		trace.plane.dist = DotProduct(trace.plane.normal, trace.endpos);
		trace.plane.signbits = AAS_CPlaneSignBits(trace.plane.normal);
	}
	else
	{
		VectorAdd(localTraceEnd, totalOrigin, trace.endpos);
		trace.plane.dist += DotProduct(trace.plane.normal, totalOrigin);
	}

	return trace;
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
			if (modeltrace.startsolid || modeltrace.fraction < trace->fraction)
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

Return the paired plane when the loaded plane pair faces the trace start.
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

	int paired = planenum ^ 1;
	if (paired < 0 || paired >= aasworld.numPlanes)
	{
		return planenum;
	}

	const aas_plane_t *opposite = &aasworld.planes[paired];
	if (DotProduct(plane->normal, opposite->normal) > -0.999f)
	{
		return planenum;
	}

	return paired;
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

Push a client-bbox trace segment onto the traversal stack.
=============
*/
static qboolean AAS_PushClientTraceSegment(aas_clienttrace_stack_t *stack,
                                           int *stacktop,
                                           int nodenum,
                                           int planenum,
                                           const vec3_t start,
                                           const vec3_t end)
{
	if (stack == NULL || stacktop == NULL || *stacktop >= AAS_AREA_STACK_SIZE)
	{
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
	trace->fraction = bsptrace.fraction;
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

	aas_clienttrace_stack_t stack[AAS_AREA_STACK_SIZE];
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
		if (front >= -AAS_TRACE_ON_EPSILON && back >= -AAS_TRACE_ON_EPSILON)
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

		if (front == back)
		{
			front -= 0.001f;
		}

		float frac = (front < 0.0f)
		                 ? (front + AAS_TRACEPLANE_EPSILON) / (front - back)
		                 : (front - AAS_TRACEPLANE_EPSILON) / (front - back);
		if (frac < 0.0f)
		{
			frac = 0.001f;
		}
		else if (frac > 1.0f)
		{
			frac = 0.999f;
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
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
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
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
    return value;
#else
    return (uint32_t)AAS_LittleLong((int32_t)value);
#endif
}

static uint16_t AAS_LittleShort(uint16_t value)
{
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
    return value;
#else
    return (uint16_t)((value >> 8) | (value << 8));
#endif
}

static float AAS_LittleFloat(float value)
{
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
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

static qboolean AAS_ParseQuotedToken(const char **cursor, const char *end, char **outToken)
{
    if (cursor == NULL || *cursor == NULL || outToken == NULL)
    {
        return qfalse;
    }

    const char *position = *cursor;
    while (position < end && isspace((unsigned char)*position))
    {
        ++position;
    }

    if (position >= end || *position != '"')
    {
        *cursor = position;
        return qfalse;
    }

    ++position;
    const char *start = position;
    qboolean escape = qfalse;
    while (position < end)
    {
        char ch = *position;
        if (!escape && ch == '\\')
        {
            escape = qtrue;
            ++position;
            continue;
        }

        if (!escape && ch == '"')
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

            const char *reader = start;
            char *writer = token;
            escape = qfalse;
            while (reader < position)
            {
                char rc = *reader++;
                if (!escape && rc == '\\')
                {
                    escape = qtrue;
                    continue;
                }

                *writer++ = rc;
                escape = qfalse;
            }
            *writer = '\0';

            ++position;
            *cursor = position;
            *outToken = token;
            return qtrue;
        }

        escape = qfalse;
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
        while (cursor < end && isspace((unsigned char)*cursor))
        {
            ++cursor;
        }

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
            while (cursor < end && isspace((unsigned char)*cursor))
            {
                ++cursor;
            }

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

            while (cursor < end && isspace((unsigned char)*cursor))
            {
                ++cursor;
            }

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
		cluster->numreachabilityareas = AAS_LittleLong(cluster->numreachabilityareas);
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

static qboolean AAS_ComputeFileChecksum(const char *path, uint32_t *checksum)
{
    if (path == NULL || checksum == NULL)
    {
        return qfalse;
    }

    FILE *file = fopen(path, "rb");
    if (file == NULL)
    {
        return qfalse;
    }

    uint8_t buffer[8192];
    size_t bytesRead;
    uint32_t crc = 0U;

    while ((bytesRead = fread(buffer, 1U, sizeof(buffer), file)) > 0U)
    {
        crc = AAS_CRC32Update(crc, buffer, bytesRead);
    }

    if (ferror(file))
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

    if (mapname == NULL || *mapname == '\0')
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
        written = snprintf(buffer, bufferSize, "maps/%s", mapname);
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

static int AAS_ReadLump(FILE *file,
                        const q2_lump_t *lump,
                        size_t elementSize,
                        void **outBuffer,
                        int *outCount,
                        long fileSize,
                        int seekError,
                        int readError)
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

    if (lump == NULL || file == NULL)
    {
        return readError;
    }

    if (lump->length == 0)
    {
        return BLERR_NOERROR;
    }

    if (lump->offset < 0 || lump->length < 0)
    {
        return readError;
    }

    long end = (long)lump->offset + (long)lump->length;
    if (fileSize >= 0 && (lump->offset > fileSize || end > fileSize))
    {
        return readError;
    }

    if (elementSize == 0U || (size_t)lump->length % elementSize != 0U)
    {
        return readError;
    }

    size_t count = (size_t)lump->length / elementSize;
    if (count > (size_t)INT_MAX)
    {
        return readError;
    }

    if (fseek(file, lump->offset, SEEK_SET) != 0)
    {
        return seekError;
    }

    void *buffer = malloc(count * elementSize);
    if (buffer == NULL)
    {
        return readError;
    }

    size_t read = fread(buffer, elementSize, count, file);
    if (read != count)
    {
        free(buffer);
        return readError;
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
                                           aas_bspbrushside_t *brushsides,
                                           aas_bspbrush_t *brushes)
{
	free(models);
	free(nodes);
	free(leaves);
	free(leafbrushes);
	free(planes);
	free(brushsides);
	free(brushes);
}

/*
=============
AAS_LoadBSPCollisionData

Load the Quake II BSP collision lumps needed by inline brush-model tracing.
=============
*/
static int AAS_LoadBSPCollisionData(const char *bspPath,
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
                                    aas_bspbrushside_t **outBrushSides,
                                    int *outNumBrushSides,
                                    aas_bspbrush_t **outBrushes,
                                    int *outNumBrushes)
{
	if (outModels == NULL ||
	    outNumModels == NULL ||
	    outNodes == NULL ||
	    outNumNodes == NULL ||
	    outLeaves == NULL ||
	    outNumLeaves == NULL ||
	    outLeafBrushes == NULL ||
	    outLeafBrushIndexSize == NULL ||
	    outPlanes == NULL ||
	    outNumPlanes == NULL ||
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
	*outBrushSides = NULL;
	*outNumBrushSides = 0;
	*outBrushes = NULL;
	*outNumBrushes = 0;

	FILE *file = fopen(bspPath, "rb");
	if (file == NULL)
	{
		BotLib_Print(PRT_ERROR, "AAS_LoadMap: cannot reopen BSP %s (%s)\n", bspPath, strerror(errno));
		return BLERR_CANNOTOPENBSPFILE;
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

	long fileSize = AAS_GetFileSize(file);
	if (fileSize < 0)
	{
		fclose(file);
		return BLERR_CANNOTREADBSPHEADER;
	}

	aas_bspmodel_t *models = NULL;
	int numModels = 0;
	int result = AAS_ReadLump(file,
	                          &header.lumps[Q2_BSP_LUMP_MODELS],
	                          sizeof(aas_bspmodel_t),
	                          (void **)&models,
	                          &numModels,
	                          fileSize,
	                          BLERR_CANNOTSEEKTOBSPFILE,
	                          BLERR_CANNOTREADBSPLUMP);
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
	                      fileSize,
	                      BLERR_CANNOTSEEKTOBSPFILE,
	                      BLERR_CANNOTREADBSPLUMP);
	if (result != BLERR_NOERROR)
	{
		free(models);
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
	                      fileSize,
	                      BLERR_CANNOTSEEKTOBSPFILE,
	                      BLERR_CANNOTREADBSPLUMP);
	if (result != BLERR_NOERROR)
	{
		free(models);
		free(nodes);
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
	                      fileSize,
	                      BLERR_CANNOTSEEKTOBSPFILE,
	                      BLERR_CANNOTREADBSPLUMP);
	if (result != BLERR_NOERROR)
	{
		free(models);
		free(nodes);
		free(leaves);
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
	                      fileSize,
	                      BLERR_CANNOTSEEKTOBSPFILE,
	                      BLERR_CANNOTREADBSPLUMP);
	if (result != BLERR_NOERROR)
	{
		free(models);
		free(nodes);
		free(leaves);
		free(leafbrushes);
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
	                      fileSize,
	                      BLERR_CANNOTSEEKTOBSPFILE,
	                      BLERR_CANNOTREADBSPLUMP);
	if (result != BLERR_NOERROR)
	{
		free(models);
		free(nodes);
		free(leaves);
		free(leafbrushes);
		free(planes);
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
	                      fileSize,
	                      BLERR_CANNOTSEEKTOBSPFILE,
	                      BLERR_CANNOTREADBSPLUMP);
	fclose(file);
	if (result != BLERR_NOERROR)
	{
		AAS_FreeLoadedBSPCollisionData(models,
		                               nodes,
		                               leaves,
		                               leafbrushes,
		                               planes,
		                               brushsides,
		                               NULL);
		return result;
	}

	AAS_FixupBSPModels(models, numModels);
	AAS_FixupBSPNodes(nodes, numNodes);
	AAS_FixupBSPLeaves(leaves, numLeaves);
	AAS_FixupBSPLeafBrushes(leafbrushes, leafBrushIndexSize);
	AAS_FixupPlanes(planes, numPlanes);
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
	*outBrushSides = brushsides;
	*outNumBrushSides = numBrushSides;
	*outBrushes = brushes;
	*outNumBrushes = numBrushes;
	return BLERR_NOERROR;
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

static void AAS_ClearWorld(void)
{
    BotMove_MoverCatalogueReset();
    AAS_RouteFrameResetDiagnostics();
    AAS_ReachabilityFrameResetDiagnostics();
    AAS_FreeAllRoutingCaches();
    AAS_ClearReachabilityData();
    free(aasworld.areacontentstravelflags);
    aasworld.areacontentstravelflags = NULL;

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

	if (aasworld.bspModels != NULL)
	{
		free(aasworld.bspModels);
		aasworld.bspModels = NULL;
	}

	if (aasworld.bspNodes != NULL)
	{
		free(aasworld.bspNodes);
		aasworld.bspNodes = NULL;
	}

	if (aasworld.bspLeaves != NULL)
	{
		free(aasworld.bspLeaves);
		aasworld.bspLeaves = NULL;
	}

	if (aasworld.bspLeafBrushes != NULL)
	{
		free(aasworld.bspLeafBrushes);
		aasworld.bspLeafBrushes = NULL;
	}

	if (aasworld.bspPlanes != NULL)
	{
		free(aasworld.bspPlanes);
		aasworld.bspPlanes = NULL;
	}

	if (aasworld.bspBrushSides != NULL)
	{
		free(aasworld.bspBrushSides);
		aasworld.bspBrushSides = NULL;
	}

	if (aasworld.bspBrushes != NULL)
	{
		free(aasworld.bspBrushes);
		aasworld.bspBrushes = NULL;
	}

    if (aasworld.areas != NULL)
    {
        free(aasworld.areas);
        aasworld.areas = NULL;
    }

	if (aasworld.bboxes != NULL)
	{
		free(aasworld.bboxes);
		aasworld.bboxes = NULL;
	}

	if (aasworld.vertexes != NULL)
	{
		free(aasworld.vertexes);
		aasworld.vertexes = NULL;
	}

	if (aasworld.edges != NULL)
	{
		free(aasworld.edges);
		aasworld.edges = NULL;
	}

	if (aasworld.edgeIndex != NULL)
	{
		free(aasworld.edgeIndex);
		aasworld.edgeIndex = NULL;
	}

	if (aasworld.faces != NULL)
	{
		free(aasworld.faces);
		aasworld.faces = NULL;
	}

	if (aasworld.faceIndex != NULL)
	{
		free(aasworld.faceIndex);
		aasworld.faceIndex = NULL;
	}

    if (aasworld.areasettings != NULL)
    {
        free(aasworld.areasettings);
        aasworld.areasettings = NULL;
    }

    if (aasworld.reachability != NULL)
    {
        free(aasworld.reachability);
        aasworld.reachability = NULL;
    }

    if (aasworld.nodes != NULL)
    {
        free(aasworld.nodes);
        aasworld.nodes = NULL;
    }

	if (aasworld.planes != NULL)
	{
		free(aasworld.planes);
		aasworld.planes = NULL;
	}

	if (aasworld.portals != NULL)
	{
		free(aasworld.portals);
		aasworld.portals = NULL;
	}

	if (aasworld.portalIndex != NULL)
	{
		free(aasworld.portalIndex);
		aasworld.portalIndex = NULL;
	}

	if (aasworld.clusters != NULL)
	{
		free(aasworld.clusters);
		aasworld.clusters = NULL;
	}

    AAS_SoundSubsystem_ClearMapAssets();
    BotMove_MoverCatalogueReset();
    memset(&aasworld, 0, sizeof(aasworld));

    TranslateEntity_SetCurrentTime(0.0f);
    TranslateEntity_SetWorldLoaded(qfalse);
}

int AAS_LoadMap(const char *mapname,
                int modelindexes, char *modelindex[],
                int soundindexes, char *soundindex[],
                int imageindexes, char *imageindex[])
{
    TranslateEntity_SetWorldLoaded(qfalse);

    (void)modelindexes;
    (void)modelindex;
    (void)imageindexes;
    (void)imageindex;

    if (mapname == NULL || *mapname == '\0')
    {
        BotLib_Print(PRT_ERROR, "AAS_LoadMap: map name not specified\n");
        return BLERR_NOAASFILE;
    }

    AAS_ClearWorld();

    strncpy(aasworld.mapName, mapname, sizeof(aasworld.mapName) - 1U);
    aasworld.mapName[sizeof(aasworld.mapName) - 1U] = '\0';

    char bspPath[MAX_FILEPATH];
    char aasPath[MAX_FILEPATH];

    if (!AAS_BuildPath(bspPath, sizeof(bspPath), mapname, ".bsp"))
    {
        BotLib_Print(PRT_ERROR, "AAS_LoadMap: BSP path too long for %s\n", mapname);
        return BLERR_NOAASFILE;
    }

    if (!AAS_BuildPath(aasPath, sizeof(aasPath), mapname, ".aas"))
    {
        BotLib_Print(PRT_ERROR, "AAS_LoadMap: AAS path too long for %s\n", mapname);
        return BLERR_NOAASFILE;
    }

    FILE *bspFile = fopen(bspPath, "rb");
    if (bspFile == NULL)
    {
        BotLib_Print(PRT_ERROR, "AAS_LoadMap: cannot open BSP %s (%s)\n", bspPath, strerror(errno));
        return BLERR_CANNOTOPENBSPFILE;
    }

    q2_bsp_header_t bspHeader;
    if (fread(&bspHeader, sizeof(bspHeader), 1U, bspFile) != 1U)
    {
        BotLib_Print(PRT_ERROR, "AAS_LoadMap: failed to read BSP header from %s\n", bspPath);
        fclose(bspFile);
        return BLERR_CANNOTREADBSPHEADER;
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
        BotLib_Print(PRT_ERROR, "AAS_LoadMap: %s is not a Quake II BSP\n", bspPath);
        fclose(bspFile);
        return BLERR_WRONGBSPFILEID;
    }

    if (bspHeader.version != Q2_BSP_VERSION)
    {
        BotLib_Print(PRT_ERROR,
                     "AAS_LoadMap: BSP %s has version %d (expected %d)\n",
                     bspPath,
                     bspHeader.version,
                     Q2_BSP_VERSION);
        fclose(bspFile);
        return BLERR_WRONGBSPFILEVERSION;
    }

    const q2_lump_t *entitiesLump = &bspHeader.lumps[Q2_BSP_LUMP_ENTITIES];
    if (entitiesLump->length < 0)
    {
        BotLib_Print(PRT_WARNING,
                     "AAS_LoadMap: BSP %s has invalid entity lump length %d\n",
                     bspPath,
                     entitiesLump->length);
    }
    else if (entitiesLump->length > 0)
    {
        if (entitiesLump->offset < 0)
        {
            BotLib_Print(PRT_WARNING,
                         "AAS_LoadMap: BSP %s has invalid entity lump offset %d\n",
                         bspPath,
                         entitiesLump->offset);
        }
        else if (fseek(bspFile, entitiesLump->offset, SEEK_SET) != 0)
        {
            BotLib_Print(PRT_WARNING,
                         "AAS_LoadMap: failed to seek to entity lump in %s (%s)\n",
                         bspPath,
                         strerror(errno));
        }
        else
        {
            size_t lumpLength = (size_t)entitiesLump->length;
            char *entityData = (char *)malloc(lumpLength + 1U);
            if (entityData == NULL)
            {
                BotLib_Print(PRT_WARNING,
                             "AAS_LoadMap: out of memory reading entity lump from %s\n",
                             bspPath);
            }
            else
            {
                size_t readLength = fread(entityData, 1U, lumpLength, bspFile);
                if (readLength != lumpLength)
                {
                    BotLib_Print(PRT_WARNING,
                                 "AAS_LoadMap: failed to read entity lump from %s\n",
                                 bspPath);
                }
                else
                {
                    entityData[lumpLength] = '\0';
                    AAS_ParseEntityLump(entityData, lumpLength);
                }
                free(entityData);
            }
        }
    }

    fclose(bspFile);

    uint32_t bspChecksum = 0U;
    if (!AAS_ComputeFileChecksum(bspPath, &bspChecksum))
    {
        BotLib_Print(PRT_ERROR, "AAS_LoadMap: failed to compute BSP checksum for %s\n", bspPath);
        return BLERR_CANNOTREADBSPHEADER;
    }

    FILE *aasFile = fopen(aasPath, "rb");
    if (aasFile == NULL)
    {
        BotLib_Print(PRT_ERROR, "AAS_LoadMap: cannot open AAS %s (%s)\n", aasPath, strerror(errno));
        return BLERR_CANNOTOPENAASFILE;
    }

    q2_aas_header_t aasHeader;
    if (fread(&aasHeader, sizeof(aasHeader), 1U, aasFile) != 1U)
    {
        BotLib_Print(PRT_ERROR, "AAS_LoadMap: failed to read AAS header from %s\n", aasPath);
        fclose(aasFile);
        return BLERR_CANNOTREADAASHEADER;
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
        BotLib_Print(PRT_ERROR, "AAS_LoadMap: %s is not an AAS file\n", aasPath);
        fclose(aasFile);
        return BLERR_WRONGAASFILEID;
    }

    if (aasHeader.version == Q2_AAS_VERSION_OLD)
    {
        BotLib_Print(PRT_WARNING, "AAS_LoadMap: %s uses the deprecated AAS version 2\n", aasPath);
    }
    else if (aasHeader.version != Q2_AAS_VERSION)
    {
        BotLib_Print(PRT_ERROR,
                     "AAS_LoadMap: %s has version %d (expected %d)\n",
                     aasPath,
                     aasHeader.version,
                     Q2_AAS_VERSION);
        fclose(aasFile);
        return BLERR_WRONGAASFILEVERSION;
    }

    long aasFileSize = AAS_GetFileSize(aasFile);
    if (aasFileSize < 0)
    {
        BotLib_Print(PRT_ERROR, "AAS_LoadMap: failed to determine size of %s\n", aasPath);
        fclose(aasFile);
        return BLERR_CANNOTREADAASHEADER;
    }

	aas_bbox_t *bboxes = NULL;
	int numBBoxes = 0;
	int result = AAS_ReadLump(aasFile,
	                          &aasHeader.lumps[Q2_AAS_LUMP_BBOXES],
	                          sizeof(aas_bbox_t),
	                          (void **)&bboxes,
	                          &numBBoxes,
	                          aasFileSize,
	                          BLERR_CANNOTSEEKTOAASFILE,
	                          BLERR_CANNOTREADAASLUMP);
	if (result != BLERR_NOERROR)
	{
		fclose(aasFile);
		return result;
	}

	aas_vertex_t *vertexes = NULL;
	int numVertexes = 0;
	result = AAS_ReadLump(aasFile,
	                      &aasHeader.lumps[Q2_AAS_LUMP_VERTEXES],
	                      sizeof(aas_vertex_t),
	                      (void **)&vertexes,
	                      &numVertexes,
	                      aasFileSize,
	                      BLERR_CANNOTSEEKTOAASFILE,
	                      BLERR_CANNOTREADAASLUMP);
	if (result != BLERR_NOERROR)
	{
		free(bboxes);
		fclose(aasFile);
		return result;
	}

	aas_edge_t *edges = NULL;
	int numEdges = 0;
	result = AAS_ReadLump(aasFile,
	                      &aasHeader.lumps[Q2_AAS_LUMP_EDGES],
	                      sizeof(aas_edge_t),
	                      (void **)&edges,
	                      &numEdges,
	                      aasFileSize,
	                      BLERR_CANNOTSEEKTOAASFILE,
	                      BLERR_CANNOTREADAASLUMP);
	if (result != BLERR_NOERROR)
	{
		free(bboxes);
		free(vertexes);
		fclose(aasFile);
		return result;
	}

	int *edgeIndex = NULL;
	int edgeIndexSize = 0;
	result = AAS_ReadLump(aasFile,
	                      &aasHeader.lumps[Q2_AAS_LUMP_EDGEINDEX],
	                      sizeof(int),
	                      (void **)&edgeIndex,
	                      &edgeIndexSize,
	                      aasFileSize,
	                      BLERR_CANNOTSEEKTOAASFILE,
	                      BLERR_CANNOTREADAASLUMP);
	if (result != BLERR_NOERROR)
	{
		free(bboxes);
		free(vertexes);
		free(edges);
		fclose(aasFile);
		return result;
	}

	aas_face_t *faces = NULL;
	int numFaces = 0;
	result = AAS_ReadLump(aasFile,
	                      &aasHeader.lumps[Q2_AAS_LUMP_FACES],
	                      sizeof(aas_face_t),
	                      (void **)&faces,
	                      &numFaces,
	                      aasFileSize,
	                      BLERR_CANNOTSEEKTOAASFILE,
	                      BLERR_CANNOTREADAASLUMP);
	if (result != BLERR_NOERROR)
	{
		free(bboxes);
		free(vertexes);
		free(edges);
		free(edgeIndex);
		fclose(aasFile);
		return result;
	}

	int *faceIndex = NULL;
	int faceIndexSize = 0;
	result = AAS_ReadLump(aasFile,
	                      &aasHeader.lumps[Q2_AAS_LUMP_FACEINDEX],
	                      sizeof(int),
	                      (void **)&faceIndex,
	                      &faceIndexSize,
	                      aasFileSize,
	                      BLERR_CANNOTSEEKTOAASFILE,
	                      BLERR_CANNOTREADAASLUMP);
	if (result != BLERR_NOERROR)
	{
		free(bboxes);
		free(vertexes);
		free(edges);
		free(edgeIndex);
		free(faces);
		fclose(aasFile);
		return result;
	}

    aas_area_t *areas = NULL;
    int numAreas = 0;
    result = AAS_ReadLump(aasFile,
                              &aasHeader.lumps[Q2_AAS_LUMP_AREAS],
                              sizeof(aas_area_t),
                              (void **)&areas,
                              &numAreas,
                              aasFileSize,
                              BLERR_CANNOTSEEKTOAASFILE,
                              BLERR_CANNOTREADAASLUMP);
    if (result != BLERR_NOERROR)
    {
		free(bboxes);
		free(vertexes);
		free(edges);
		free(edgeIndex);
		free(faces);
		free(faceIndex);
        fclose(aasFile);
        return result;
    }

    aas_areasettings_t *areasettings = NULL;
    int numAreaSettings = 0;
    result = AAS_ReadLump(aasFile,
                          &aasHeader.lumps[Q2_AAS_LUMP_AREASETTINGS],
                          sizeof(aas_areasettings_t),
                          (void **)&areasettings,
                          &numAreaSettings,
                          aasFileSize,
                          BLERR_CANNOTSEEKTOAASFILE,
                          BLERR_CANNOTREADAASLUMP);
    if (result != BLERR_NOERROR)
    {
		free(bboxes);
		free(vertexes);
		free(edges);
		free(edgeIndex);
		free(faces);
		free(faceIndex);
        free(areas);
        fclose(aasFile);
        return result;
    }

    aas_reachability_t *reachability = NULL;
    int numReachability = 0;
    result = AAS_ReadLump(aasFile,
                          &aasHeader.lumps[Q2_AAS_LUMP_REACHABILITY],
                          sizeof(aas_reachability_t),
                          (void **)&reachability,
                          &numReachability,
                          aasFileSize,
                          BLERR_CANNOTSEEKTOAASFILE,
                          BLERR_CANNOTREADAASLUMP);
    if (result != BLERR_NOERROR)
    {
		free(bboxes);
		free(vertexes);
		free(edges);
		free(edgeIndex);
		free(faces);
		free(faceIndex);
        free(areas);
        free(areasettings);
        fclose(aasFile);
        return result;
    }

	aas_plane_t *planes = NULL;
	int numPlanes = 0;
	result = AAS_ReadLump(aasFile,
	                      &aasHeader.lumps[Q2_AAS_LUMP_PLANES],
	                      sizeof(aas_plane_t),
	                      (void **)&planes,
	                      &numPlanes,
	                      aasFileSize,
	                      BLERR_CANNOTSEEKTOAASFILE,
	                      BLERR_CANNOTREADAASLUMP);
	if (result != BLERR_NOERROR)
	{
		free(bboxes);
		free(vertexes);
		free(edges);
		free(edgeIndex);
		free(faces);
		free(faceIndex);
		free(areas);
		free(areasettings);
		free(reachability);
		fclose(aasFile);
		return result;
	}

    aas_node_t *nodes = NULL;
    int numNodes = 0;
    result = AAS_ReadLump(aasFile,
                          &aasHeader.lumps[Q2_AAS_LUMP_NODES],
                          sizeof(aas_node_t),
                          (void **)&nodes,
                          &numNodes,
                          aasFileSize,
                          BLERR_CANNOTSEEKTOAASFILE,
                          BLERR_CANNOTREADAASLUMP);
    if (result != BLERR_NOERROR)
    {
		free(bboxes);
		free(vertexes);
		free(edges);
		free(edgeIndex);
		free(faces);
		free(faceIndex);
        free(areas);
        free(areasettings);
        free(reachability);
		free(planes);
        fclose(aasFile);
        return result;
    }

	aas_portal_t *portals = NULL;
	int numPortals = 0;
	result = AAS_ReadLump(aasFile,
	                      &aasHeader.lumps[Q2_AAS_LUMP_PORTALS],
	                      sizeof(aas_portal_t),
	                      (void **)&portals,
	                      &numPortals,
	                      aasFileSize,
	                      BLERR_CANNOTSEEKTOAASFILE,
	                      BLERR_CANNOTREADAASLUMP);
	if (result != BLERR_NOERROR)
	{
		free(bboxes);
		free(vertexes);
		free(edges);
		free(edgeIndex);
		free(faces);
		free(faceIndex);
		free(areas);
		free(areasettings);
		free(reachability);
		free(planes);
		free(nodes);
		fclose(aasFile);
		return result;
	}

	int *portalIndex = NULL;
	int portalIndexSize = 0;
	result = AAS_ReadLump(aasFile,
	                      &aasHeader.lumps[Q2_AAS_LUMP_PORTALINDEX],
	                      sizeof(int),
	                      (void **)&portalIndex,
	                      &portalIndexSize,
	                      aasFileSize,
	                      BLERR_CANNOTSEEKTOAASFILE,
	                      BLERR_CANNOTREADAASLUMP);
	if (result != BLERR_NOERROR)
	{
		free(bboxes);
		free(vertexes);
		free(edges);
		free(edgeIndex);
		free(faces);
		free(faceIndex);
		free(areas);
		free(areasettings);
		free(reachability);
		free(planes);
		free(nodes);
		free(portals);
		fclose(aasFile);
		return result;
	}

	aas_cluster_t *clusters = NULL;
	int numClusters = 0;
	result = AAS_ReadLump(aasFile,
	                      &aasHeader.lumps[Q2_AAS_LUMP_CLUSTERS],
	                      sizeof(aas_cluster_t),
	                      (void **)&clusters,
	                      &numClusters,
	                      aasFileSize,
	                      BLERR_CANNOTSEEKTOAASFILE,
	                      BLERR_CANNOTREADAASLUMP);
	if (result != BLERR_NOERROR)
	{
		free(bboxes);
		free(vertexes);
		free(edges);
		free(edgeIndex);
		free(faces);
		free(faceIndex);
		free(areas);
		free(areasettings);
		free(reachability);
		free(planes);
		free(nodes);
		free(portals);
		free(portalIndex);
		fclose(aasFile);
		return result;
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
    if (!AAS_ComputeFileChecksum(aasPath, &aasChecksum))
    {
        BotLib_Print(PRT_ERROR, "AAS_LoadMap: failed to compute checksum for %s\n", aasPath);
		free(bboxes);
		free(vertexes);
		free(edges);
		free(edgeIndex);
		free(faces);
		free(faceIndex);
        free(areas);
        free(areasettings);
        free(reachability);
		free(planes);
        free(nodes);
		free(portals);
		free(portalIndex);
		free(clusters);
        return BLERR_CANNOTREADAASHEADER;
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
	aas_bspbrushside_t *bspBrushSides = NULL;
	int numBspBrushSides = 0;
	aas_bspbrush_t *bspBrushes = NULL;
	int numBspBrushes = 0;
	result = AAS_LoadBSPCollisionData(bspPath,
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
	                                  &bspBrushSides,
	                                  &numBspBrushSides,
	                                  &bspBrushes,
	                                  &numBspBrushes);
	if (result != BLERR_NOERROR)
	{
		free(bboxes);
		free(vertexes);
		free(edges);
		free(edgeIndex);
		free(faces);
		free(faceIndex);
		free(areas);
		free(areasettings);
		free(reachability);
		free(planes);
		free(nodes);
		free(portals);
		free(portalIndex);
		free(clusters);
		return result;
	}

    strncpy(aasworld.aasFilePath, aasPath, sizeof(aasworld.aasFilePath) - 1U);
    aasworld.aasFilePath[sizeof(aasworld.aasFilePath) - 1U] = '\0';

    aasworld.bspChecksum = (int)bspChecksum;
    aasworld.aasChecksum = (int)aasChecksum;
	aasworld.numBspModels = numBspModels;
	aasworld.bspModels = bspModels;
	aasworld.numBspNodes = numBspNodes;
	aasworld.bspNodes = bspNodes;
	aasworld.numBspLeaves = numBspLeaves;
	aasworld.bspLeaves = bspLeaves;
	aasworld.bspLeafBrushIndexSize = bspLeafBrushIndexSize;
	aasworld.bspLeafBrushes = bspLeafBrushes;
	aasworld.numBspPlanes = numBspPlanes;
	aasworld.bspPlanes = bspPlanes;
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
    aasworld.maxEntities = 0;
    aasworld.entities = NULL;
    aasworld.entitiesValid = qfalse;
    aasworld.numFrames = 0;
    aasworld.loaded = qtrue;
    aasworld.initialized = qfalse;

    if (!AAS_SoundSubsystem_RegisterMapAssets(soundindexes, soundindex))
    {
        BotLib_Print(PRT_ERROR,
                     "AAS_LoadMap: failed to register sound assets for %s\n",
                     mapname);
        AAS_ClearWorld();
        return BLERR_INVALIDIMPORT;
    }

    AAS_InitTravelFlagFromType();
    AAS_InitAreaContentsTravelFlags();
    int reachStatus = AAS_PrepareReachability();
    if (reachStatus != BLERR_NOERROR)
    {
        AAS_ClearWorld();
        return reachStatus;
    }

    int areaStatus = AAS_EnsureAreaListArray();
    if (areaStatus != BLERR_NOERROR)
    {
        AAS_ClearWorld();
        return areaStatus;
    }

    AAS_InvalidateRouteCache();

    AAS_FrameSynchronise(0.0f);
    TranslateEntity_SetWorldLoaded(qtrue);
    return BLERR_NOERROR;
}

void AAS_Shutdown(void)
{
    if (aasworld.loaded || aasworld.initialized)
    {
        BotLib_Print(PRT_MESSAGE, "AAS shutdown.\n");
    }

    TranslateEntity_SetCurrentTime(0.0f);
    TranslateEntity_SetWorldLoaded(qfalse);
    AAS_ClearWorld();
    g_aasLibraryInitialized = qfalse;
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

	int maxareas = aasworld.numAreas;
	int *areas = (int *)malloc((size_t)maxareas * sizeof(int));
	if (areas == NULL)
	{
		return BLERR_INVALIDENTITYNUMBER;
	}

	int occupied = AAS_BBoxAreas(absmins, absmaxs, areas, maxareas);
	for (int index = 0; index < occupied; ++index)
	{
		int areanum = areas[index];
		status = AAS_LinkEntityToArea(entity, areanum);
		if (status != BLERR_NOERROR)
		{
			free(areas);
			return status;
		}

		AAS_SetEntityAreaBit(entity, areanum);
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

    if (state->bounds_dirty || state->origin_dirty)
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
    }

    if (state->origin_dirty && entity->solid == SOLID_BSP)
    {
        float dx = fabsf(state->origin[0] - state->previous_origin[0]);
        float dy = fabsf(state->origin[1] - state->previous_origin[1]);
        float dz = fabsf(state->origin[2] - state->previous_origin[2]);
        if (dx > 0.125f || dy > 0.125f || dz > 0.125f)
        {
            AAS_InvalidateRouteCache();
        }
    }

    aasworld.entitiesValid = qtrue;
    return BLERR_NOERROR;
}
