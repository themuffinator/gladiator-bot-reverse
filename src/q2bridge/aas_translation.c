#include "q2bridge/aas_translation.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "q2bridge/bridge.h"
#include "q2bridge/update_translator.h"
#include "botlib/aas/aas_map.h"
#include "botlib/ai/move/mover_catalogue.h"

static qboolean g_aas_loaded = qfalse;
static float g_aas_current_time = 0.0f;

void TranslateEntity_SetWorldLoaded(qboolean loaded)
{
    g_aas_loaded = loaded;
    Bridge_HandleMapStateChange();
}

void TranslateEntity_SetCurrentTime(float time)
{
    g_aas_current_time = time;
}

static float QuantizeComponent(float angle)
{
    const float to_short = 65536.0f / 360.0f;
    const float to_angle = 360.0f / 65536.0f;

    float scaled = angle * to_short;
    long quantised = lroundf(scaled);
    quantised &= 0xFFFF;
    if (quantised & 0x8000)
    {
        quantised -= 0x10000;
    }

    return (float)quantised * to_angle;
}

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

    QuantizeEulerDegrees(dst->delta_angles);
    QuantizeEulerDegrees(dst->viewangles);
    QuantizeEulerDegrees(dst->kick_angles);
    QuantizeEulerDegrees(dst->gunangles);

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

static void CopyScalarFrameFields(const bot_updateentity_t *src, AASEntityFrame *dst)
{
    dst->solid = src->solid;
    dst->modelindex = src->modelindex;
    dst->modelindex2 = src->modelindex2;
    dst->modelindex3 = src->modelindex3;
    dst->modelindex4 = src->modelindex4;
    dst->frame = src->frame;
    dst->skinnum = src->skinnum;
    dst->effects = src->effects;
    dst->renderfx = src->renderfx;
    dst->sound = src->sound;
    dst->event_id = src->event;
}

bot_status_t TranslateEntityUpdate(int ent_num,
                                   const bot_updateentity_t *src,
                                   AASEntityFrame *dst)
{
    if (src == NULL || dst == NULL)
    {
        return BLERR_INVALIDIMPORT;
    }

    if (!g_aas_loaded)
    {
        BotlibLog(PRT_MESSAGE, "AAS_UpdateEntity: not loaded\n");
        return BLERR_NOAASFILE;
    }

    float previous_time = dst->last_update_time;
    dst->last_update_time = g_aas_current_time;
    if (previous_time <= 0.0f)
    {
        dst->frame_delta = 0.0f;
    }
    else
    {
        dst->frame_delta = g_aas_current_time - previous_time;
        if (dst->frame_delta < 0.0f)
        {
            dst->frame_delta = 0.0f;
        }
    }

    dst->number = ent_num;

    vec3_t previous_origin;
    previous_origin[0] = dst->origin[0];
    previous_origin[1] = dst->origin[1];
    previous_origin[2] = dst->origin[2];

    CopyVec3(src->old_origin, dst->old_origin);

    bool origin_changed = CopyIfChanged(src->origin, dst->origin);
    if (origin_changed)
    {
        CopyVec3(previous_origin, dst->previous_origin);
    }

    bool mins_changed = CopyIfChanged(src->mins, dst->mins);
    bool maxs_changed = CopyIfChanged(src->maxs, dst->maxs);
    bool bounds_changed = mins_changed || maxs_changed;

    vec3_t quantized_angles;
    CopyVec3(src->angles, quantized_angles);
    if (src->solid == 3)
    {
        QuantizeEulerDegrees(quantized_angles);
    }

    bool angles_changed = CopyIfChanged(quantized_angles, dst->angles);
    if (!angles_changed && src->solid == 3 && bounds_changed)
    {
        /* Brush models relay their orientation through mins/maxs flags. */
        CopyVec3(quantized_angles, dst->angles);
        dst->angles_dirty = true;
    }
    else
    {
        dst->angles_dirty = angles_changed;
    }

    dst->bounds_dirty = bounds_changed;
    dst->origin_dirty = origin_changed;

    bool was_mover = dst->is_mover;
    bool is_mover = BotMove_MoverCatalogueIsModelMover(src->modelindex);
    dst->is_mover = is_mover;
    bool mover_changed = was_mover != is_mover;

    if ((bounds_changed && src->solid == 3) || mover_changed)
    {
        BotlibLog(PRT_MESSAGE, "[q2bridge] relinking brush model ent %d\n", ent_num);
    }

    CopyScalarFrameFields(src, dst);

    return BLERR_NOERROR;
}

void BotlibLog(int level, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    bot_import_t *imports = Q2Bridge_GetImportTable();
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
