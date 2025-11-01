#ifndef Q2BRIDGE_AAS_TRANSLATION_H
#define Q2BRIDGE_AAS_TRANSLATION_H

#include <stdbool.h>

#include "botlib.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \brief Runtime mirror of the Gladiator botlib per-client state.
 *
 * The legacy DLL stores each client in a 0x11d0-byte record that begins with
 * an "active" flag and the owning slot index. Immediately after those words it
 * memcpy's the full bot_updateclient_t payload and then normalises the angle
 * fields before AI runs for the frame.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L32615-L32623】【F:dev_tools/gladiator.dll.bndb
_hlil.txt†L32598-L32613】
 *
 * The helper functions declared in this header provide a typed representation
 * for the reconstructed data and describe the conversions that must take place
 * when shuttling Quake II game state into the internal AAS-aware structures.
 * Maintaining the fidelity of origins and angles is essential because the game
 * AI reuses those values for steering and visibility tests in routines such as
 * ai_stand and ai_walk.【F:dev_tools/game_source/g_ai.c†L52-L137】
 * The Quake II game DLL packages the bot_updateclient_t from the client ps
 * fields inside BotLib_BotUpdateClient, which is the upstream of the data we
 * receive here.【F:dev_tools/game_source/bl_main.c†L482-L541】
 */
typedef struct AASClientFrame_s {
    pmtype_t pm_type;
    vec3_t origin;
    vec3_t velocity;
    vec3_t delta_angles; ///< Normalised via QuantizeEulerDegrees.
    byte pm_flags;
    byte pm_time;
    float gravity;
    vec3_t viewangles;  ///< Stored as short->angle converted floats.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L32615-L32623】
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
    float last_update_time; ///< Absolute timestamp pulled from the botlib heap.
    float frame_delta;      ///< Computed as now - last_update_time.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L32603-L32611】
} AASClientFrame;

/**
 * \brief Snapshot of an entity as expected by the botlib navigation layer.
 *
 * The Gladiator DLL allocates a 0x84-byte record per entity. Each call to
 * BotUpdateEntity performs the following high-level steps:
 *
 *  - Refuses to run if the AAS world is not loaded and logs
 *    "AAS_UpdateEntity: not loaded" (severity 1).【F:dev_tools/gladiator.dll.bndb_hlil.txt†L10344-L10354】
 *  - Updates frame timing metadata (current time and delta).【F:dev_tools/gladiator.dll.bndb_hlil.txt†L10358-L10386】
 *  - Copies old origins before writing the new ones so velocity calculations can
 *    compare positions across frames.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L10368-L10386】
 *  - Tracks solid type and model indexes, and mirrors mins/maxs or angle data
 *    only when they change by using a vector equality guard. This behaviour is
 *    implemented by a helper that compares the three components before copying
 *    (j_sub_10043240).【F:dev_tools/gladiator.dll.bndb_hlil.txt†L10386-L10428】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L52700
-L52720】
 *  - When bounds move, calls into the spatial update routine responsible for
 *    relinking the entity in the AAS world (j_sub_10005e60 / j_sub_1001c620).
 *
 * The structure below models the data layout after those operations. The
 * incoming values originate from BotLib_BotUpdateEntity inside the game DLL,
 * which forwards the edict_t presentation state to the bot library.【F:dev_tools/game_source/bl_main.c†L570-L620】
 */
typedef struct AASEntityFrame_s {
    int number;      ///< Entity slot written to the 0x84-byte table.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L10358-L10390】
    int solid;       ///< bot_updateentity_t::solid.
    vec3_t origin;   ///< Current origin.
    vec3_t angles;   ///< Orientation (quantised when solid == 3).
    vec3_t mins;     ///< Bounding box mins.
    vec3_t maxs;     ///< Bounding box maxs.
    vec3_t old_origin; ///< Last networked origin.
    vec3_t previous_origin; ///< Cached copy from before the latest write.
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
    float last_update_time; ///< Absolute timestamp.
    float frame_delta;      ///< Time since the previous update.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L10358-L10368】
    bool angles_dirty;      ///< true when angles changed this frame.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L10386-L10409】
    bool bounds_dirty;      ///< true when mins/maxs changed (solid == 2 branch).【F:dev_tools/gladiator.dll.bndb_hlil.txt†L10390-L10421】
    bool origin_dirty;      ///< true when origin changed.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L10421-L10433】
} AASEntityFrame;

/**
 * \brief Converts Quake II short-based Euler angles into the quantised float
 * representation used by the botlib (short2angle/angle2short round-trip).
 *
 * Mirrors j_sub_10042d40 which multiplies by 65536/360, rounds to int and then
 * scales back to degrees, matching the pmove delta-angle behaviour recorded in
 * the DLL.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L52520-L52605】
 */
void QuantizeEulerDegrees(vec3_t angles);

/**
 * \brief Guard copy used by BotUpdateEntity to avoid redundant writes.
 *
 * Returns true when the destination differs and should be updated, replicating
 * j_sub_10043240's component-wise comparison. The caller uses the boolean to
 * set the *_dirty flags and trigger spatial relinks.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L10386-L10428】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L52700-L52720】
 */
bool CopyIfChanged(const vec3_t src, vec3_t dst_out);

/**
 * \brief Configure whether the AAS world is available for entity translation.
 */
void TranslateEntity_SetWorldLoaded(qboolean loaded);

/**
 * \brief Update the current frame time used for entity delta calculations.
 */
void TranslateEntity_SetCurrentTime(float time);

/**
 * \brief Translate BotUpdateClient payload into the local AASClientFrame.
 *
 * The function is responsible for:
 *  - Verifying the target slot is active; otherwise emit the
 *    "tried to updated inactive bot client" log and return
 *    BLERR_AIUPDATEINACTIVECLIENT (0x18).【F:dev_tools/gladiator.dll.bndb_hlil.txt†L32598-L32607】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L74440-L74441】
 *  - memcpy'ing the raw state, then running QuantizeEulerDegrees over delta
 *    angles, view angles and gun/view kick vectors so they honour the original
 *    16-bit precision limits.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L32603-L32623】
 *  - Updating the cached timestamps to keep BotAI's thinktime consistent with
 *    the DLL implementation.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L32603-L32611】
 */
bot_status_t TranslateClientUpdate(int client_num,
                                   const bot_updateclient_t *src,
                                   float current_time,
                                   AASClientFrame *dst);

/**
 * \brief Translate BotUpdateEntity payload into the local AASEntityFrame.
 *
 * Responsibilities gathered from the reversed DLL:
 *  - Abort with BLERR_NOAASFILE when the AAS world is not loaded and log the
 *    historic warning message.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L10344-L10354】
 *  - Preserve prior origins before copying new data so downstream code can
 *    reconstruct instantaneous velocity (fields previous_origin vs. origin).
 *  - Mark angles/bounds/origin dirty when CopyIfChanged returns true and update
 *    the appropriate *_dirty flag so callers know when to call into
 *    j_sub_10005e60 / j_sub_1001c620 equivalents for spatial maintenance.
 *  - For brush models (solid == 3) ensure the orientation vector is copied when
 *    mins/maxs differ, matching the DLL's arg1 flag semantics.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L10386-L10409】
 *  - Track the bridge-provided timestamps so downstream code can compute
 *    consistent delta times with the historic DLL implementation.
 */
bot_status_t TranslateEntityUpdate(int ent_num,
                                   const bot_updateentity_t *src,
                                   AASEntityFrame *dst);

/**
 * \brief Emit botlib-style diagnostics.
 *
 * The Gladiator binary funnels all validation failures through
 * data_10063fe8(level, message, ...). The replacement layer should provide a
 * drop-in logger so Translate* helpers can surface identical errors.
 */
void BotlibLog(int level, const char *fmt, ...);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // Q2BRIDGE_AAS_TRANSLATION_H
