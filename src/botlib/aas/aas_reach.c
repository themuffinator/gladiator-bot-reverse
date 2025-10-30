/*
 * Gladiator Bot AAS reachability (placeholder)
 *
 * This module will encapsulate the reachability graph construction, matching
 * the HLIL fragments that enumerate edges, assign travel times, and hook into
 * the routing cache codepath defined in aas_route.c. The structure should be
 * comparable to Quake III Arena's be_aas_reach.c, giving us a known-good
 * baseline when stitching behaviours together.
 *
 * Planned data structures:
 *  - `aas_reachability_t` records mirroring the original binary layout. Fields
 *    include start/end area numbers, travel type, travel time, and auxiliary
 *    vectors.
 *  - Builder work queues for deferred processing uncovered in the HLIL output
 *    (e.g., pending elevator links or crouch optimisations).
 *  - Reachability index tables stored inside `aasworld` to support fast lookups
 *    during routing.
 *
 * Public API surface (to be implemented):
 *  - `void AAS_InitReachability(void);`
 *      Initializes global tables and resets temporary state. Requires the AAS
 *      file loader to have populated world geometry beforehand.
 *  - `bool AAS_BuildReachabilityGraph(void);`
 *      Runs the lift-and-translate pass informed by HLIL comments, producing
 *      `aas_reachability_t` entries and seeding travel times.
 *  - `void AAS_LinkReachability(void);`
 *      Finalizes adjacency lists and registers travel type metadata with the
 *      routing subsystem.
 *  - `void AAS_ClearReachability(void);`
 *      Releases reachability buffers, invoked both on shutdown and on map
 *      reloads triggered by botlib imports.
 *
 * Integration notes:
 *  - Depends on the shared memory allocator (`l_memory`) for both transient and
 *    persistent buffers.
 *  - Must consult libvars controlling optional features (e.g., rocket jump
 *    reachabilities) before generating edges to match runtime expectations.
 *  - Logging via `l_log` will help validate graph completeness against the
 *    HLIL traces.
 *
 * Future commits will populate the scaffolding with the reconstructed logic.
 */

#include "../common/l_memory.h"
#include "../common/l_log.h"
#include "../common/l_libvar.h"

/* TODO: add the real implementations once the reverse engineering work is ready. */
