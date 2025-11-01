#ifndef Q2BRIDGE_AAS_TRANSLATION_H
#define Q2BRIDGE_AAS_TRANSLATION_H

#include <stdbool.h>

#include "botlib.h"

/**
 * \brief Runtime mirror of the Gladiator botlib per-client state.
 */
typedef struct q2bridge_aas_client_frame_s {
    pmtype_t pm_type;
    vec3_t origin;
    vec3_t velocity;
    float delta_angles[3];
    byte pm_flags;
    byte pm_time;
    float gravity;
    vec3_t viewangles;
    vec3_t viewoffset;
    vec3_t kick_angles;
    vec3_t gunangles;
    vec3_t gunoffset;
    int gunindex;
    int gunframe;
    float blend[4];
    float fov;
    int rdflags;
    short stats[MAX_STATS];
    int inventory[MAX_ITEMS];
    float last_update_time;
    float frame_delta;
} q2bridge_aas_client_frame_t;

/**
 * \brief Snapshot of an entity as expected by the botlib navigation layer.
 */
typedef struct q2bridge_aas_entity_frame_s {
    int number;
    int solid;
    vec3_t origin;
    vec3_t angles;
    vec3_t mins;
    vec3_t maxs;
    vec3_t old_origin;
    vec3_t previous_origin;
    int modelindex;
    int modelindex2;
    int modelindex3;
    int modelindex4;
    int frame;
    int skinnum;
    int effects;
    int renderfx;
    int sound;
    int event_id;
    float last_update_time;
    float frame_delta;
    bool angles_dirty;
    bool bounds_dirty;
    bool origin_dirty;
} q2bridge_aas_entity_frame_t;

/**
 * \brief Emit botlib-style diagnostics.
 */
void BotlibLog(int level, const char *fmt, ...);

bool Q2Bridge_CopyIfChanged(const vec3_t src, vec3_t dst_out);

int Q2Bridge_TranslateClientUpdate(int client_num,
                                   const bot_updateclient_t *src,
                                   q2bridge_aas_client_frame_t *dst);

int Q2Bridge_TranslateEntityUpdate(int ent_num,
                                   const bot_updateentity_t *src,
                                   float current_time,
                                   q2bridge_aas_entity_frame_t *dst);

void Q2Bridge_QuantizeEulerDegrees(vec3_t angles);

#endif // Q2BRIDGE_AAS_TRANSLATION_H
