#include "../common/l_log.h"
#include "../../shared/q_shared.h"

#include <stddef.h>

/**
 * Logs a stub message for the legacy bot_test console command. The Gladiator
 * HLIL export table exposes the Test slot (sub_10038460) but our reverse
 * engineering has not restored the full behaviour yet.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L43703-L43732】
 * The Quake III implementation prints detailed area diagnostics, highlights
 * the current reachability, and emits debug lines via the engine callbacks.【F:dev_tools/Quake-III-Arena-master/code/botlib/be_interface.c†L318-L569】
 */
void AAS_DebugBotTest(int parm0, const char *parm1, const vec3_t origin, const vec3_t angles)
{
    (void)parm0;
    (void)parm1;
    (void)origin;
    (void)angles;

    BotLib_Print(PRT_MESSAGE, "[aas_debug] bot_test is not implemented yet.\n");
    // TODO: Query the cached AAS world data, mirror the BotExportTest
    // overlay pathing diagnostics, and use the import DebugLine* callbacks to
    // render the same area, reachability, and ladder information the original
    // engine exposed.【F:dev_tools/Quake-III-Arena-master/code/botlib/be_interface.c†L356-L553】【F:dev_tools/Quake-III-Arena-master/code/botlib/be_aas_debug.c†L394-L518】
}

/**
 * Logs a stub message for aas_showpath. The real game draws a sequence of
 * reachabilities between two areas using the botlib debug line helpers.【F:dev_tools/Quake-III-Arena-master/code/botlib/be_aas_debug.c†L605-L711】
 */
void AAS_DebugShowPath(int startArea, int goalArea, const vec3_t start, const vec3_t goal)
{
    (void)startArea;
    (void)goalArea;
    (void)start;
    (void)goal;

    BotLib_Print(PRT_MESSAGE, "[aas_debug] aas_showpath is not implemented yet.\n");
    // TODO: Invoke the eventual AAS path query once the navigation data is
    // decoded, then iterate each reachability step to draw arrows and
    // annotate travel types with BotLib_Print so the engine console mirrors
    // the legacy tooling.【F:dev_tools/Quake-III-Arena-master/code/botlib/be_aas_debug.c†L605-L711】
}
