/*
 * Gladiator movement design skeleton.
 *
 * Per-frame updates must mirror the Quake III `BotMoveToGoal` pipeline:
 *   1. Clear the `bot_moveresult_t` buffer and refresh `bot_movestate_t` flags
 *      (`MFL_SWIMMING`, `MFL_AGAINSTLADDER`, grapple bits) before any routing
 *      logic. The HLIL shows offsets `0x18`/`0x1c` being rewritten prior to the
 *      travel-type dispatch, matching the order in `be_ai_move.c`.【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_move.c†L3055-L3315】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L40697-L40740】
 *   2. Resolve platform edge cases while the bot is grounded. Both the Quake
 *      sources and `sub_100160e0` classify `func_plat`, `func_bobbing`, and
 *      related movers so accidental landings can either retarget a reachability
 *      or raise `MOVERESULT_ONTOPOFOBSTACLE`. Replicate the entity scanning and
 *      `modeltypes` bookkeeping before AAS queries run. 【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_move.c†L3088-L3180】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L18002-L18267】
 *   3. Request or reuse reachabilities via `BotGetReachabilityToGoal`, updating
 *      `lastreachnum`, `reachability_time`, and avoid lists. The HLIL copies a
 *      0x30-byte mover result out of each travel helper and stores the bot’s
 *      last origin (`0x14`-`0x16`) immediately afterwards, so we should do the
 *      same when wiring up the C wrappers. 【F:dev_tools/gladiator.dll.bndb_hlil.txt†L40712-L40739】
 *   4. Dispatch to travel-specific handlers (walk, ladder, grapple, etc.) and
 *      emit the `(last) travel type %%d not implemented` diagnostic when an
 *      enum is missing. Gladiator’s switch table maps the same travel codes as
 *      Quake III and expects identical hook-ups. 【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_move.c†L3213-L3315】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L40712-L40790】
 *
 * Inputs:
 *   - `bot_movestate_t` snapshot with current origin, velocity, and routing
 *     context (offsets `0x10`-`0x1f` in the HLIL layout).
 *   - Active goal blob containing reachability area, target origin, and travel
 *     flags (passed as `arg3` through `sub_100343a0`).
 *
 * Outputs:
 *   - `bot_moveresult_t` populated with direction, speed, result flags, and the
 *     travel type that completed this frame.
 *
 * Dependencies:
 *   - AAS queries for ground tests, reachability lookups, and travel time
 *     estimates (`AAS_OnGround`, `AAS_ReachabilityFromNum`, `AAS_Time`).
 *   - Grapple state machine that pulls `laserhook`/`hookon`/`hookoff` commands
 *     through the botlib import table, obeying the same timeout and slack rules
 *     as the original DLL. 【F:dev_tools/gladiator.dll.bndb_hlil.txt†L40193-L40460】
 */
