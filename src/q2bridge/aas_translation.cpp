#include "q2bridge/aas_translation.h"

#include <cstdarg>
#include <cstdio>

#include "botlib/aas/aas_local.h"
#include "q2bridge/bridge.h"

namespace q2bridge {

bool CopyIfChanged(const vec3_t src, vec3_t dst_out)
{
    bool dirty = false;
    for (int axis = 0; axis < 3; ++axis)
    {
        if (src[axis] != dst_out[axis])
        {
            dirty = true;
        }
    }

    if (dirty)
    {
        VectorCopy(src, dst_out);
    }

    return dirty;
}

int TranslateEntityUpdate(int ent_num,
                          const bot_updateentity_t &src,
                          float current_time,
                          AASEntityFrame &dst)
{
    if (!aasworld.loaded)
    {
        BotlibLog(PRT_WARNING, "AAS_UpdateEntity: not loaded\n");
        return BLERR_NOAASFILE;
    }

    vec3_t prior_origin;
    VectorCopy(dst.origin, prior_origin);
    VectorCopy(prior_origin, dst.previous_origin);

    float previous_time = dst.last_update_time;
    dst.last_update_time = current_time;
    dst.frame_delta = (previous_time > 0.0f) ? (current_time - previous_time) : 0.0f;

    dst.number = ent_num;
    dst.solid = src.solid;

    bool angles_dirty = CopyIfChanged(src.angles, dst.angles);
    bool mins_dirty = CopyIfChanged(src.mins, dst.mins);
    bool maxs_dirty = CopyIfChanged(src.maxs, dst.maxs);
    bool bounds_changed = mins_dirty || maxs_dirty;

    if (src.solid == SOLID_BSP && bounds_changed && !angles_dirty)
    {
        VectorCopy(src.angles, dst.angles);
    }

    dst.angles_dirty = angles_dirty;
    dst.bounds_dirty = (src.solid == SOLID_BBOX) ? bounds_changed : false;

    bool origin_dirty = CopyIfChanged(src.origin, dst.origin);
    dst.origin_dirty = origin_dirty;

    VectorCopy(src.old_origin, dst.old_origin);

    dst.modelindex = src.modelindex;
    dst.modelindex2 = src.modelindex2;
    dst.modelindex3 = src.modelindex3;
    dst.modelindex4 = src.modelindex4;
    dst.frame = src.frame;
    dst.skinnum = src.skinnum;
    dst.effects = src.effects;
    dst.renderfx = src.renderfx;
    dst.sound = src.sound;
    dst.event_id = src.event;

    return BLERR_NOERROR;
}

int TranslateClientUpdate(int client_num,
                          const bot_updateclient_t &src,
                          AASClientFrame &dst)
{
    (void)client_num;
    (void)src;
    (void)dst;
    return BLERR_NOERROR;
}

void QuantizeEulerDegrees(vec3_t angles)
{
    (void)angles;
}

} // namespace q2bridge

extern "C" void BotlibLog(int level, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    bot_import_t *imports = Q2Bridge_GetImportTable();
    if (imports != nullptr && imports->Print != nullptr)
    {
        char buffer[1024];
        vsnprintf(buffer, sizeof(buffer), fmt, args);
        imports->Print(level, buffer);
    }
    else
    {
        vfprintf(stderr, fmt, args);
    }

    va_end(args);
}
