# ai_goal Reverse Mapping

This note captures the current Gladiator HLIL anchors used to reconstruct the goal-state setup and item-selection wiring.

## HLIL anchors

| HLIL address | Reconstructed role | Source mapping |
| --- | --- | --- |
| `sub_100293a0` | Client memory diagnostics. Prints the item-weight config byte count and the separate item-index allocation at client offset `0xbc4`. | `BotGoal_ItemWeightIndexByteSize` reports the index allocation and `BotLoadItemWeights` emits the retail `"%6d bytes item index\n"` developer line after index rebuild. |
| `sub_10029480` | Client setup. Loads the character, then calls the item-weight loader with characteristic `0x1c` before weapon/chat setup. | `BotSetupClient` now allocates the goal handle and calls `AI_GoalBotlib_LoadItemWeights` with `BOT_CHARACTERISTIC_ITEMWEIGHTS`. |
| `sub_100308d0` | Item-weight loader. Calls the weight parser, stores the config pointer, requires global item config, then builds the item-to-weight index. Returns `0` on success and `0x1c` on failure. | `BotLoadItemWeights` returns `BLERR_NOERROR` / `BLERR_CANNOTLOADITEMWEIGHTS`, refuses to bind weights before `itemconfig` is loaded, and rebuilds `itemweightindex` from registered item infos. |
| `sub_10030950` | Frees the item-weight config and item-weight index pair. | `BotFreeItemWeights` and `BotFreeGoalState`. |
| `sub_10030990` | Goal-state reset. Clears the goal stack/avoid ranges, writes stack top `0`, and then resets avoid-goal state. | `BotResetGoalState` clears the stack, preserves the retail zero-is-empty stack sentinel, resets avoid goals/reach entries, and leaves loaded item weights intact. |
| `sub_100309d0` | Goal AI setup. Loads `itemconfig` with default `items.c`; failure is `0x1d`. | `BotSetupGoalAI` / `BotInitLevelItems` now preserve the `BLERR_CANNOTLOADITEMCONFIG` startup failure instead of collapsing it into weapon setup. |
| `sub_10030a20` | Goal AI shutdown. Frees the global item config pointer. | `BotShutdownGoalAI` frees goal states and clears level-item/item-definition caches without reloading `itemconfig`. |

## Q3 successor parity points

The Quake III `be_ai_goal.c` selection flow remains the best readable successor reference for Gladiator's goal item logic:

- `BotChooseLTGItem` and `BotChooseNBGItem` require loaded item weights before selecting.
- Current area resolution uses movement reachability first, with last valid area fallback.
- Item value is divided by travel time scaled by `0.01`, not subtracted from travel time.
- Avoid timers are tested against estimated travel seconds using `avoidtime - t * 0.009`.
- Chosen LTG/NBG items are added back to avoid goals using dropped/default/minimum avoid timings.
- `BotSetAvoidGoalTime` treats negative avoid times as a request to derive the timeout from the matching level item's respawn/default/minimum avoid rules.
- NBG selection checks that returning to the LTG is not more expensive than continuing directly.
- `BotInterbreedGoalFuzzyLogic` writes into the child's existing item-weight config; it does not clone either parent. `BotMutateGoalFuzzyLogic` ignores the public `range` parameter and dispatches directly to `EvolveWeightConfig`.
- `BotGoalName` clears the output for unknown goal numbers rather than formatting the numeric id.
- Goal stack slot `0` is a sentinel. Pushes start at slot `1`, `goalstacktop == 0` means empty, and overflow logs `"goal heap overflow"` without discarding older goals.

## Wiring changes in this round

- Client setup now follows the HLIL order by loading character item weights into the botlib goal state.
- Goal setup now returns `BLERR_CANNOTLOADITEMCONFIG` for a missing shared `itemconfig`, and item-weight binding returns `BLERR_CANNOTLOADITEMWEIGHTS` when called before that shared config exists.
- Goal shutdown now clears the reconstructed global goal/item caches instead of invoking the setup loader again.
- Goal stack helpers now use the retail zero-sentinel contract instead of a `-1` empty marker.
- Goal avoid synchronisation now merges orchestrator avoid entries instead of clearing botlib-owned avoid timers each frame.
- Implemented goal helpers are now exposed through `bot_export_t` without shifting existing exported field offsets.
- Focused tests cover item-weight setup, itemconfig failure mapping, item-index byte accounting, exported goal helper presence, child-config fuzzy interbreeding, avoid timer preservation, stack emptying, goal naming, and level-item lookup.
