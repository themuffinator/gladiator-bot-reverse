/*
 * Gladiator Bot AAS routing (placeholder)
 *
 * This file will host the routing cache management logic reconstructed from
 * HLIL dumps of the original Gladiator bot. The intended design mirrors the
 * Quake III Arena implementation in dev_tools/Quake-III-Arena-master/
 * code/botlib/be_aas_route.c, providing a clear reference while we continue
 * lifting behaviour from the binary.
 *
 * Planned data structures:
 *  - Cluster-local routing caches keyed by goal area numbers.
 *  - Portal routing caches that bridge clusters and expose travel times to
 *    every portal in the AAS world.
 *  - Travel type to travel flag tables (`aasworld.travelflagfortype`) needed to
 *    translate HLIL-observed enums into mask values for filtering reachability
 *    expansions.
 *
 * Public API surface (to be implemented):
 *  - `void AAS_InitTravelFlagFromType(void);`
 *      Seeds the travel type to flag lookup table, similar to Quake III’s
 *      `AAS_InitTravelFlagFromType` routine. Expected to run during botlib
 *      initialization after the world data is loaded.
 *  - `aas_routing_cache_t *AAS_GetAreaRoutingCache(int cluster, int areanum);`
 *      Provides cached travel times for a goal area in a cluster and triggers
 *      cache construction if none exists.
 *  - `aas_routing_cache_t *AAS_GetPortalRoutingCache(int areanum);`
 *      Serves cross-cluster travel time data via portal caches.
 *  - `void AAS_UpdateRoutingCache(unsigned int max_updates_per_frame);`
 *      Performs incremental cache refreshes governed by libvar throttles and
 *      frame budgets observed in the HLIL output.
 *  - `void AAS_FreeAllRoutingCaches(void);`
 *      Releases cache memory during shutdown or when a full rebuild is
 *      required.
 *
 * Integration notes:
 *  - Requires the global `aasworld` descriptor to be populated by the file
 *    loader prior to invocation.
 *  - Depends on the botlib memory subsystem (`l_memory`) for cache allocation
 *    and the logging interface for instrumentation and debugging output.
 *  - Must honour libvar controls such as `saveroutingcache` that gate cache
 *    persistence between runs.
 *
 * Detailed lifting work will align HLIL blocks that manipulate travel flags and
 * update the routing caches with the Quake III reference implementation.
 */

#include "../common/l_memory.h"
#include "../common/l_log.h"
#include "../common/l_libvar.h"
#include "../common/l_utils.h"

/* TODO: add the real implementations once the reverse engineering work is ready. */
