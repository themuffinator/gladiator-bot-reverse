# ai_goal Reverse Mapping

This note captures the current Gladiator HLIL anchors used to reconstruct the goal-state setup and item-selection wiring.

## HLIL anchors

| HLIL address | Reconstructed role | Source mapping |
| --- | --- | --- |
| `sub_100293a0` | Client memory diagnostics. Prints the item-weight config byte count and the separate item-index allocation at client offset `0xbc4`. | `BotGoal_ItemWeightIndexByteSize` reports the index allocation and `BotLoadItemWeights` emits the retail `"%6d bytes item index\n"` developer line after index rebuild. |
| `sub_10029480` | Client setup. Loads the character, then calls the item-weight loader with characteristic `0x1c` before weapon/chat setup. | `BotSetupClient` now allocates the goal handle and calls `AI_GoalBotlib_LoadItemWeights` with `BOT_CHARACTERISTIC_ITEMWEIGHTS`. |
| `sub_100308d0` | Item-weight loader. Calls the weight parser with the caller filename, stores the config pointer, requires global item config, then builds the item-to-weight index. Returns `0` on success and `0x1c` on failure. | `BotLoadItemWeights` returns `BLERR_NOERROR` / `BLERR_CANNOTLOADITEMWEIGHTS`, refuses to bind weights before `itemconfig` is loaded, passes the original filename through to `ReadWeightConfig` so retained cache entries are found before filesystem resolution, emits the retail generic `"couldn't load weights\n"` wrapper diagnostic on parser failure, and rebuilds `itemweightindex` from registered item infos. |
| `sub_10030950` | Frees the item-weight config and item-weight index pair. | `BotFreeItemWeights` and `BotFreeGoalState`. |
| `sub_10030990` | Goal-state reset. Clears the goal stack/avoid ranges, writes stack top `0`, and then resets avoid-goal state. | `BotResetGoalState` clears the stack, preserves the retail zero-is-empty stack sentinel, resets avoid goals/reach entries, and leaves loaded item weights intact. |
| `sub_1002f6f0` | Avoid-goal reset. Clears the 64 goal-number slots at goal-state offset `0x1cc` and the 64 expiry-time slots at `0x2cc`. | `bot_goalstate_t` now stores separate `avoidgoals[64]` / `avoidgoaltimes[64]` arrays and `BotResetAvoidGoals` clears both without maintaining a compact count. |
| `sub_1002f730` / `sub_1002f7b0` / `sub_1002f820` | Avoid-goal dump/add/query helpers. The add helper scans the 64 time slots and writes only the first expired slot; it does not compact, refresh active duplicates, or evict an active entry when the table is full. | `BotAddToAvoidGoals`, `BotAvoidGoalTime`, `BotRemoveFromAvoidGoals`, and `BotDumpAvoidGoals` now operate over the fixed Gladiator table. Removal clears an active timeout in place, queries scan the whole table, and full active tables reject new entries. |
| `sub_100309d0` | Goal AI setup. Loads `itemconfig` with default `items.c`; failure is `0x1d`. | `BotSetupGoalAI` / `BotInitLevelItems` now preserve the `BLERR_CANNOTLOADITEMCONFIG` startup failure instead of collapsing it into weapon setup. |
| `sub_10030a20` | Goal AI shutdown. Frees the global item config pointer. | `BotShutdownGoalAI` frees goal states and clears level-item/item-definition caches without reloading `itemconfig`. |
| `sub_100292e0` | BotAI-side dynamic item refresh throttle. Compares current botlib time against the next refresh deadline, calls `sub_1002fa20`, then advances the deadline by one second. | `BotUpdateEntityItemsThrottled` mirrors the one-second gate and `AI_GoalBotlib_Update` invokes it before stack touch/selection so live entity items are linked before LTG/NBG scoring. |
| `sub_1002ed20` | Item config loader. Parses `iteminfo` blocks, including classname, display name, model path, model index, respawn time, and bounds. | `BotGoal_LoadItemDefs` now preserves model/modelindex fields and has a raw `iteminfo` fallback when the standalone precompiler path yields an empty or partial table. |
| `sub_1002f360` | Level item initialization. Reads `notspawnflags` with default `2048`, resolves each item model through `IndexFromModel`, logs modelindex zero, scans BSP entities, skips filtered spawnflag entities, drops stationary items to the floor, and seeds level item goals. | `BotGoal_SetMapModelIndexes` caches the `BotLoadMap` model table before `BotInitLevelItems`; item definitions are resolved against that table before BSP item registration; the goal-side BSP reader now preserves little-endian headers on Windows, item entity registration applies the retail `spawnflags & notspawnflags` gate, and stationary BSP items trace down before their origin/area is registered. |
| `sub_1002fe50` / `sub_1002fe80` | Goal-stack peek helpers. Top goal returns null at depth zero; second goal returns null unless stack depth is greater than one. | `BotGetTopGoal` preserves the zero-is-empty sentinel, and `BotGetSecondGoal` now refuses depth-one stacks instead of exposing slot zero. |
| `sub_1002f890` | Level item goal lookup. Walks level items until it finds the first valid matching item whose goal number is greater than the caller's cursor, then writes the public goal fields from the level-item record and iteminfo bounds. | `BotGetLevelItemGoal` now treats `index` as a lower-bound cursor, rebuilds the returned goal from the level item plus item definition bounds, reports public flags as `GFL_ITEM` plus `GFL_DROPPED` only for timed dropped items, returns that goal number, and returns `-1` when the classname search is exhausted. |
| `sub_1002fa20` | Dynamic entity item update. Expires timed goals, matches settled live entities by model index, links nearby static level items, and creates timed dropped goals. | `BotUpdateEntityItems` now performs model-index reconciliation, uses the Gladiator 20-unit static-link radius, updates linked origins/areas, refuses dropped goals whose resolved goal area is a jump-pad area, and creates `GFL_DROPPED` goals with 30-second timeouts. |
| `sub_10030600` | Goal-touching predicate. Gets the normal presence bounding box, expands the goal bounds by the player extents, then checks all three axes. | `BotTouchingGoal` now mirrors the overlap-style retail test instead of requiring the bot origin to be inside the raw item bounds. |
| `sub_10030770` | Item-goal visibility stale-entity test. For item goals, traces from the viewer eye to `goal.origin + goal.mins`, then treats stale entity data as "in vis but not visible". | `BotItemGoalInVisButNotVisible` now preserves the original `mins + mins` / `scale 0.5` target-point quirk instead of tracing to the geometric item center. |

## Q3 successor parity points

The Quake III `be_ai_goal.c` selection flow remains the best readable successor reference for Gladiator's goal item logic:

- `BotChooseLTGItem` and `BotChooseNBGItem` require loaded item weights before selecting.
- Current area resolution uses movement reachability first; areas with no reachability links fall back to the last valid area and reject selection when no last area exists.
- Item value is divided by travel time scaled by `0.01`, not subtracted from travel time.
- Avoid timers are tested against estimated travel seconds using `avoidtime - t * 0.009`.
- Chosen LTG/NBG items are added back to avoid goals using timeout-backed dropped/default/minimum avoid timings.
- Gladiator's avoid-goal table is smaller and less "managed" than the Q3 successor: it is a fixed 64-slot number/time table, inserts use only expired slots, active duplicate inserts do not refresh earlier slots, and a full active table does not evict an older avoid goal.
- Static item records with `entitynum == 0` are ignored by LTG/NBG item selection unless flagged as roam goals; the dynamic entity refresh is expected to link real pickups before they become selectable.
- Dynamic entity item refresh is not run on every goal query; the BotAI path gates it to roughly once per second before goal selection.
- Dropped dynamic items are discarded when their resolved goal area is a jump-pad area, matching the Q3 successor guard after `AAS_BestReachableArea`.
- `BotGetLevelItemGoal` uses a strict `goal.number > index` cursor, returns `-1` when no later classname match exists, and rebuilds public lookup flags instead of exposing internal roam bookkeeping.
- `BotTouchingGoal` treats touching as normal player bbox overlap with the goal bounds, not point containment inside the item bounds.
- `BotItemGoalInVisButNotVisible` traces to `goal.origin + goal.mins`; this is inherited from the Q3 source sequence and confirmed in Gladiator HLIL.
- `BotSetAvoidGoalTime` treats negative avoid times as a request to derive the timeout from the matching level item's respawn/default/minimum avoid rules.
- The public negative `BotSetAvoidGoalTime` path does not use the chosen-dropped-item avoid shortcut; even dropped goals derive from respawn/default/minimum rules on that API. The chosen-item shortcut itself is keyed off the temporary level-item timeout, matching the retail `if (bestitem->timeout)` test rather than a copied flag bit.
- Stationary BSP items are traced down to the floor during level-item initialization before goal origin/area registration.
- Parsed `target_location` and `info_camp` metadata are pushed to the head of their lists in the Q3 successor, so duplicate map locations resolve to the last parsed BSP entity and camp spot iteration runs in reverse entity-lump order.
- NBG selection requires `travel_time < maxtime`; zero or equal max-time values reject candidates, and repeated LTG/NBG picks are controlled by avoid timers rather than an extra same-goal-number veto.
- NBG selection checks that returning to the LTG is not more expensive than continuing directly.
- `BotInterbreedGoalFuzzyLogic` writes into the child's existing item-weight config; it does not clone either parent. `BotMutateGoalFuzzyLogic` ignores the public `range` parameter and dispatches directly to `EvolveWeightConfig`.
- `BotGoalName` clears the output for unknown goal numbers rather than formatting the numeric id.
- Goal stack slot `0` is a sentinel. Pushes start at slot `1`, `goalstacktop == 0` means empty, second-goal lookup requires depth greater than one, and overflow logs `"goal heap overflow"` without discarding older goals.

## Wiring changes in this round

- Client setup now follows the HLIL order by loading character item weights into the botlib goal state.
- Goal setup now returns `BLERR_CANNOTLOADITEMCONFIG` for a missing shared `itemconfig`, and item-weight binding returns `BLERR_CANNOTLOADITEMWEIGHTS` when called before that shared config exists.
- Goal shutdown now clears the reconstructed global goal/item caches instead of invoking the setup loader again.
- Goal stack helpers now use the retail zero-sentinel contract instead of a `-1` empty marker.
- Goal avoid synchronisation now merges orchestrator avoid entries instead of clearing botlib-owned avoid timers each frame.
- Avoid-goal storage now follows the HLIL fixed-slot layout and insertion semantics instead of the earlier compact counted list.
- Implemented goal helpers are now exposed through `bot_export_t` without shifting existing exported field offsets.
- Item definitions now carry `model` / `modelindex`, `BotLoadMap` feeds the goal module the Quake II model table, and dynamic entity updates reconstruct the HLIL static-link and dropped-item paths, including the dropped-item jump-pad-area rejection.
- BotAI goal updating now routes through the recovered once-per-second entity item refresh gate before selection, matching `sub_100292e0`.
- Nearby goal selection now preserves the retail strict `maxtime` ceiling and relies on avoid timers, not an extra same-number filter, to prevent immediate LTG reselection.
- LTG/NBG start-area resolution now rejects no-reachability current areas unless a previous valid area is available as fallback.
- Public negative avoid-time derivation now uses a separate respawn/default/minimum helper so dropped items only use the dropped avoid timeout when they are actually selected as LTG/NBG goals.
- LTG/NBG selection now rebuilds pushed item goals from the selected level item and derives `GFL_DROPPED` / dropped avoid timing from the live timeout field, while preserving `GFL_ROAM` for selected roam goals.
- NBG selection keeps the raw travel time from the bot to the LTG for the return-route comparison, including the retail zero/unreachable result, instead of substituting a permissive sentinel.
- Stationary BSP level items now follow the retail floor-drop trace before registration.
- Level item initialization now mirrors the retail `notspawnflags` BSP entity filter and keeps normal little-endian Quake II BSP headers readable on Windows builds.
- Parsed map locations and camp spots now preserve successor head-insertion order rather than append order.
- Public level-item lookup now reconstructs the exported goal record instead of copying the stored selector record wholesale, preserving the retail item/dropped flag contract for roam and temporary dropped goals.
- Focused tests cover item-weight setup, itemconfig failure mapping, item-index byte accounting, exported goal helper presence, child-config fuzzy interbreeding, avoid timer preservation, fixed-slot avoid table behavior, negative avoid-time derivation for dropped items, selected-item timeout-backed dropped semantics, stack emptying and second-goal sentinel handling, goal naming, goal touching bounds, item visibility trace targeting, level-item cursor lookup and public flag reconstruction, BSP spawnflag filtering, stationary BSP item floor dropping, info-entity ordering, strict NBG max-time handling, start-area reachability rejection, static live-entity linking, throttled AI-side entity refresh, unlinked-static item selection guards, dropped-item jump-pad rejection, and dropped-item timeout handling.
