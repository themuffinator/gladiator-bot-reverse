# HLIL Movement Helper Notes vs. Quake III Botlib

## Overview
The Gladiator HLIL exposes the same high-level movement flow that Quake III Arena implements in `be_ai_move.c` and `be_ai_goal.c`. The snippets below capture the state machines and special cases that must be recreated when we lift the decompiled helpers into C. The Quake III sources are used as a reference for naming and structure; the HLIL confirms which branches are still present in Gladiator and where behaviour diverges.

## Core Move State Tracking
* Quake III stores per-bot movement state in `bot_movestate_t`, including fields for positional history, reachability bookkeeping, and bitflags such as `MFL_SWIMMING`, `MFL_AGAINSTLADDER`, and grapple state. 【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_move.c†L56-L119】
* The Gladiator HLIL clears the same flag mask (`&= 0xfffffff3`) and immediately repopulates bits by calling helpers equivalent to `BotSwimInWater`, `BotOnLadder`, and `BotJumpOrFall`, matching the Quake logic that rebuilds `ms->moveflags` each frame. 【F:dev_tools/gladiator.dll.bndb_hlil.txt†L40697-L40720】
* HLIL offsets `0x13` (last reachability), `0x18` (move flag byte), and `0x1c` (reachability timeout) mirror Quake’s `lastreachnum`, `moveflags`, and `reachability_time`. Updating those slots happens just before or after dispatching per-travel-type handlers, preserving the same temporal ordering. 【F:dev_tools/gladiator.dll.bndb_hlil.txt†L40697-L40740】

## Travel-Type Dispatch Tables
* Quake III maps reachability travel types to specialised finish routines inside `BotMoveToGoal`, logging when an unrecognised type appears. 【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_move.c†L3213-L3315】
* Gladiator’s `sub_100343a0` uses the same switch, falling back to `data_10063fe8("(last) travel type %d not implemented yet")` for unsupported branches, and copies a 0x30-byte movement result structure from each helper before returning. 【F:dev_tools/gladiator.dll.bndb_hlil.txt†L40712-L40790】
* `sub_10034170` computes a per-travel-type timeout (5s for standard walking, 6s for ladders and bobbing, 10s for grapple) identical to the Quake heuristics that seed `ms->reachability_time`. Unsupported types emit a diagnostic before defaulting to 8 seconds. 【F:dev_tools/gladiator.dll.bndb_hlil.txt†L40563-L40582】

## Grapple / Hook State Machine
* Quake III caches cvars such as `laserhook`, loads the grapple model on demand, and toggles the `hookon` / `hookoff` commands based on reachability distance inside `BotTravel_Grapple`. 【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_move.c†L2534-L2737】
* Gladiator mirrors that flow: `sub_100338a0` grabs the `laserhook` libvar and precaches `models/weapons/grapple/hook/tris.md2` the first time it detects an enabled hook. 【F:dev_tools/gladiator.dll.bndb_hlil.txt†L40193-L40257】
* The grapple handler sets bit `0x40` in the move flags when `hookon` is issued, caches the reel distance in offsets `0x1a`/`0x1b`, and forcibly toggles `hookoff` when the slack exceeds one unit or the bot drifts more than 48 units from the last recorded distance—behaviour that aligns with Quake’s tension checks. 【F:dev_tools/gladiator.dll.bndb_hlil.txt†L40385-L40460】
* Timeouts use `j_sub_1000e120()` (the HLIL wrapper for `AAS_Time()`) to gate reactivation, matching the Quake guard that waits before firing the hook again. 【F:dev_tools/gladiator.dll.bndb_hlil.txt†L40393-L40445】

## Obstacle and Platform Handling
* Quake’s ground branch in `BotMoveToGoal` inspects `modeltypes` to detect when bots land atop elevators, bobbing platforms, doors, or static brushes, then rewires reachabilities or reports blockage. 【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_move.c†L3088-L3180】
* Gladiator’s `sub_100160e0` walks the entity list, matching `classname` strings such as `func_plat`, `func_bobbing`, and related movers, extracting lip, height, and speed fields to populate the same lookup table. That preparation is required so the movement loop can resolve accidental landings just like Quake III. 【F:dev_tools/gladiator.dll.bndb_hlil.txt†L18002-L18267】
* Later in the frame, the HLIL copies the bot’s origin into `0x14`/`0x16` after finishing travel dispatch, mirroring the Quake behaviour of caching the last grounded location for platform correction and stuck detection. 【F:dev_tools/gladiator.dll.bndb_hlil.txt†L40725-L40739】

## Goal Selection and Per-Frame Sequencing
* Quake III maintains a goal stack (`bot_goalstate_t`) and computes long-term and nearby goals each frame via `BotChooseLTGItem` / `BotChooseNBGItem`, combining fuzzy item weights with AAS travel times. 【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_goal.c†L1241-L1519】
* The Gladiator HLIL shows the movement helper taking both the active goal blob (`arg3`) and the current move state (`arg2`), exiting early when `arg2[0x10]` (current reachability) matches the goal’s area—exactly where Quake stops to push or pop goals. 【F:dev_tools/gladiator.dll.bndb_hlil.txt†L40791-L40806】
* After pathing, Gladiator calls a routine equivalent to `BotGetReachabilityToGoal`, storing new avoid lists in offsets `0x1d`–`0x1f`, and clears the hook state when the path changes—matching Quake’s per-frame order of operations. 【F:dev_tools/gladiator.dll.bndb_hlil.txt†L40810-L40820】

## Notable Edge Cases to Preserve
* **Accidental Platform Landings:** HLIL validates mover model numbers and logs when an elevator/bobbing platform lacks a reachability, implying the need to replicate Quake’s fallback that marks the bot as blocked in that scenario. 【F:dev_tools/gladiator.dll.bndb_hlil.txt†L18023-L18210】【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_move.c†L3092-L3176】
* **Grapple Slack Management:** The hook is disengaged if the reel distance shrinks below 0.4 seconds of travel time or grows by more than a unit, ensuring the bot does not remain tethered to dead geometry. 【F:dev_tools/gladiator.dll.bndb_hlil.txt†L40405-L40460】
* **Travel Type 0xE Gating:** Before accepting a grapple reachability (type `0xE`), Gladiator checks the remaining timeout and whether bit `0x80` is already set in the move flags, mirroring Quake’s rule that avoids re-firing the hook too soon. 【F:dev_tools/gladiator.dll.bndb_hlil.txt†L40638-L40655】
* **Reachability Resets on Goal Change:** When `arg2[0x10]` equals the goal’s area, the HLIL returns immediately; otherwise it frees the old reachability handle (`j_sub_10011040`) and re-queries via `j_sub_100310e0`, just like Quake’s `BotMoveToGoal` re-pathing after area changes. 【F:dev_tools/gladiator.dll.bndb_hlil.txt†L40791-L40820】

These notes should guide the future C translation so Gladiator’s movement logic retains the quirks captured by the original binary.
