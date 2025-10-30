# Gladiator Bot AAS Reachability & Routing Notes

## HLIL to Gameplay Mapping

| HLIL Symbol / Comment | Planned Implementation | Cross-Reference |
| --- | --- | --- |
| `travelflagfortype` initialization block | `AAS_InitTravelFlagFromType` seeds the travel-type mask table once `aasworld` is ready. | Mirrors `AAS_InitTravelFlagFromType` in Quake III’s `be_aas_route.c`, which maps each travel enum to a bitmask used by the routing caches.【F:dev_tools/Quake-III-Arena-master/code/botlib/be_aas_route.c†L62-L101】 |
| `routing cache refresh` loop with frame budget guard | `AAS_UpdateRoutingCache` will throttle cache maintenance based on a per-frame quota sourced from a libvar. HLIL traces show the same guard surrounding area/portal cache rebuilds. | Quake III enforces `MAX_FRAMEROUTINGUPDATES` to limit cache work per frame; we expect identical behaviour to emerge from the Gladiator binary.【F:dev_tools/Quake-III-Arena-master/code/botlib/be_aas_route.c†L44-L60】 |
| `GetAreaRoutingCache` callsites in HLIL | `AAS_GetAreaRoutingCache` populates or retrieves cached area travel times. | Quake III uses the function to lazily allocate caches per cluster goal area before computing travel costs.【F:dev_tools/Quake-III-Arena-master/code/botlib/be_aas_route.c†L36-L60】 |
| Reachability builder storing start/end area indices and travel type | `AAS_BuildReachabilityGraph` will emit `aas_reachability_t` records matching the HLIL layout, enabling traversal queries. | `be_aas_reach.c` in Quake III constructs identical records from BSP/AAS data ahead of routing.【F:dev_tools/Quake-III-Arena-master/code/botlib/be_aas_reach.c†L1-L120】 |

## Source Placeholders

The following placeholder files capture the structure and integration points before we port the full logic:

- `src/botlib/aas/aas_route.c` — documents routing cache data, the API surface, and integration with memory, logging, and libvars.【F:src/botlib/aas/aas_route.c†L1-L58】
- `src/botlib/aas/aas_reach.c` — outlines reachability graph responsibilities and their touchpoints with the routing layer.【F:src/botlib/aas/aas_reach.c†L1-L51】

Both files will expand as we translate HLIL output into C code.

## Module Dependencies & Initialization Order

1. **Libvars (`src/botlib/common/l_libvar.c`)** — Provide runtime configuration toggles such as `saveroutingcache`. Routing setup must query or define these before cache creation so that persistence settings are honoured.【F:src/botlib/common/l_libvar.c†L201-L291】
2. **Memory allocator (`src/botlib/common/l_memory.c`)** — Supplies the arena allocator used across botlib. Reachability and routing caches should only allocate after `l_memory` has been initialized by the botlib interface layer.【F:src/botlib/common/l_memory.c†L1-L120】
3. **Logging (`src/botlib/common/l_log.c`)** — Required for diagnostics while validating reconstructed behaviour against HLIL traces. Initialization should occur prior to reachability/routing setup so debugging output is available.【F:src/botlib/common/l_log.c†L1-L120】
4. **AAS world loader (`src/botlib/precomp` future work)** — Must populate `aasworld` with area, portal, and cluster metadata before reachability or routing routines execute, matching the sequence observed in both HLIL and Quake III.

Ensuring this initialization order prevents invalid memory access and aligns the reverse-engineered modules with their Quake III counterparts.
