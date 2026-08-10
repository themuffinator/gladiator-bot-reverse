#include "q2bridge/aas_translation.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "q2bridge/bridge.h"
#include "q2bridge/update_translator.h"
#include "botlib/aas/aas_map.h"
#include "botlib/ai_move/mover_catalogue.h"

static qboolean g_aas_loaded = qfalse;
static float g_aas_current_time = 0.0f;

/*
=============
TranslateEntity_SetWorldLoaded
=============
*/
void TranslateEntity_SetWorldLoaded(qboolean loaded)
{
	g_aas_loaded = loaded;
	Bridge_HandleMapStateChange();
}

/*
=============
TranslateEntity_SetCurrentTime
=============
*/
void TranslateEntity_SetCurrentTime(float time)
{
	g_aas_current_time = time;
}

/*
=============
QuantizeComponent
=============
*/
static float QuantizeComponent(float angle)
{
	const double to_short = 182.04444444444445;
	const float to_angle = 0.0054931640625f;
	int quantised = (int)((double)angle * to_short);
	return (float)(unsigned short)quantised * to_angle;
}

/*
=============
CopyVec3
=============
*/
static void CopyVec3(const vec3_t src, vec3_t dst)
{
	if (src == NULL || dst == NULL)
	{
		return;
	}

	dst[0] = src[0];
	dst[1] = src[1];
	dst[2] = src[2];
}

/*
=============
QuantizeEulerDegrees
=============
*/
void QuantizeEulerDegrees(vec3_t angles)
{
	if (angles == NULL)
	{
		return;
	}

	for (int i = 0; i < 3; ++i)
	{
		angles[i] = QuantizeComponent(angles[i]);
	}
}

/*
=============
CopyIfChanged
=============
*/
bool CopyIfChanged(const vec3_t src, vec3_t dst_out)
{
	if (src == NULL || dst_out == NULL)
	{
		return false;
	}

	if (dst_out[0] == src[0] && dst_out[1] == src[1] && dst_out[2] == src[2])
	{
		return false;
	}

	dst_out[0] = src[0];
	dst_out[1] = src[1];
	dst_out[2] = src[2];
	return true;
}

/*
=============
TranslateClientUpdate

Copy the retail 0x4cc BotUpdateClient snapshot without altering its angles.
The private view-angle accumulator lives in AI_DMState_ApplyDeltaAngles.
=============
*/
bot_status_t TranslateClientUpdate(int client_num,
								   const bot_updateclient_t *src,
								   float current_time,
								   AASClientFrame *dst)
{
	(void)client_num;

	if (src == NULL || dst == NULL)
	{
		return BLERR_INVALIDIMPORT;
	}

	float previous_time = dst->last_update_time;

	dst->pm_type = src->pm_type;
	CopyVec3(src->origin, dst->origin);
	CopyVec3(src->velocity, dst->velocity);
	CopyVec3(src->delta_angles, dst->delta_angles);
	dst->pm_flags = src->pm_flags;
	dst->pm_time = src->pm_time;
	dst->gravity = src->gravity;
	CopyVec3(src->viewangles, dst->viewangles);
	CopyVec3(src->viewoffset, dst->viewoffset);
	CopyVec3(src->kick_angles, dst->kick_angles);
	CopyVec3(src->gunangles, dst->gunangles);
	CopyVec3(src->gunoffset, dst->gunoffset);
	dst->gunindex = src->gunindex;
	dst->gunframe = src->gunframe;
	memcpy(dst->blend, src->blend, sizeof(dst->blend));
	dst->fov = src->fov;
	dst->rdflags = src->rdflags;
	memcpy(dst->stats, src->stats, sizeof(dst->stats));
	memcpy(dst->inventory, src->inventory, sizeof(dst->inventory));

	/* Bridge-only diagnostics; retail's copied payload ends before these fields. */
	dst->last_update_time = current_time;
	if (previous_time <= 0.0f)
	{
		dst->frame_delta = 0.0f;
	}
	else
	{
		dst->frame_delta = current_time - previous_time;
		if (dst->frame_delta < 0.0f)
		{
			dst->frame_delta = 0.0f;
		}
	}

	return BLERR_NOERROR;
}

/*
=============
CopyScalarFrameFields

Copy only the scalar fields written by retail AAS_UpdateEntity. The routine
deliberately leaves the cached skinnum, sound and event words untouched.
=============
*/
static void CopyScalarFrameFields(const bot_updateentity_t *src, AASEntityFrame *dst)
{
	dst->solid = src->solid;
	dst->modelindex = src->modelindex;
	dst->modelindex2 = src->modelindex2;
	dst->modelindex3 = src->modelindex3;
	dst->modelindex4 = src->modelindex4;
	dst->frame = src->frame;
	dst->effects = src->effects;
	dst->renderfx = src->renderfx;
}

/*
=============
TranslateEntityUpdate

Mirror retail AAS_UpdateEntity's 0x84-byte update and relink decisions.
=============
*/
bot_status_t TranslateEntityUpdate(int ent_num,
								   const bot_updateentity_t *src,
								   AASEntityFrame *dst)
{
	if (!g_aas_loaded)
	{
		BotlibLog(PRT_MESSAGE, "AAS_UpdateEntity: not loaded\n");
		return BLERR_NOAASFILE;
	}

	if (src == NULL || dst == NULL)
	{
		return BLERR_INVALIDIMPORT;
	}

	float previous_time = dst->last_update_time;
	dst->frame_delta = g_aas_current_time - previous_time;
	dst->last_update_time = g_aas_current_time;
	dst->number = ent_num;

	CopyVec3(dst->origin, dst->previous_origin);
	CopyVec3(src->old_origin, dst->old_origin);
	CopyScalarFrameFields(src, dst);

	dst->angles_dirty = false;
	dst->bounds_dirty = false;
	if (src->solid == SOLID_BSP)
	{
		dst->angles_dirty = CopyIfChanged(src->angles, dst->angles);
		AAS_BSPModelMinsMaxsOrigin(src->modelindex - 1,
								  dst->angles,
								  dst->mins,
								  dst->maxs,
								  NULL);
	}
	else if (src->solid == SOLID_BBOX)
	{
		bool mins_changed = CopyIfChanged(src->mins, dst->mins);
		bool maxs_changed = CopyIfChanged(src->maxs, dst->maxs);
		dst->bounds_dirty = mins_changed || maxs_changed;
	}

	dst->origin_dirty = CopyIfChanged(src->origin, dst->origin);

	/* Reconstruction-only movement metadata; it never drives retail relinking. */
	dst->is_mover = src->modelindex > 0 &&
		BotMove_MoverCatalogueIsModelMover(src->modelindex - 1);

	return BLERR_NOERROR;
}

/*
=============
BotlibLog
=============
*/
void BotlibLog(int level, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    bot_import_extended_t *imports = Q2Bridge_GetImportTable();
    if (imports != NULL && imports->Print != NULL)
    {
        va_list args_copy;
        va_copy(args_copy, args);

        char buffer[1024];
        vsnprintf(buffer, sizeof(buffer), fmt, args_copy);
        va_end(args_copy);

        imports->Print(level, "%s", buffer);
    }
    else
    {
        vfprintf(stderr, fmt, args);
    }

    va_end(args);
}
