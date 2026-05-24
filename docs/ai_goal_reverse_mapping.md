# ai_goal Reverse Mapping

This note captures the current Gladiator HLIL anchors used to reconstruct the goal-state setup and item-selection wiring.

## HLIL anchors

| HLIL address | Reconstructed role | Source mapping |
| --- | --- | --- |
| `sub_10029480` | Client setup. Loads the character, then calls the item-weight loader with characteristic `0x1c` before weapon/chat setup. | `BotSetupClient` now allocates the goal handle and calls `AI_GoalBotlib_LoadItemWeights` with `BOT_CHARACTERISTIC_ITEMWEIGHTS`. |
| `sub_100308d0` | Item-weight loader. Calls the weight parser, stores the config pointer, requires global item config, then builds the item-to-weight index. Returns `0` on success and `0x1c` on failure. | `BotLoadItemWeights` now returns `BLERR_NOERROR` / `BLERR_CANNOTLOADITEMWEIGHTS` and rebuilds `itemweightindex` from registered item infos. |
| `sub_10030950` | Frees the item-weight config and item-weight index pair. | `BotFreeItemWeights` and `BotFreeGoalState`. |
| `sub_100309d0` | Goal AI setup. Loads `itemconfig` with default `items.c`; failure is `0x1d`. | `BotSetupGoalAI` / `BotInitLevelItems` and item definition loading. |
| `sub_10030a20` | Goal AI shutdown. Frees the global item config pointer. | `BotShutdownGoalAI` clears goal states and level-item caches. |

## Q3 successor parity points

The Quake III `be_ai_goal.c` selection flow remains the best readable successor reference for Gladiator's goal item logic:

- `BotChooseLTGItem` and `BotChooseNBGItem` require loaded item weights before selecting.
- Current area resolution uses movement reachability first, with last valid area fallback.
- Item value is divided by travel time scaled by `0.01`, not subtracted from travel time.
- Avoid timers are tested against estimated travel seconds using `avoidtime - t * 0.009`.
- Chosen LTG/NBG items are added back to avoid goals using dropped/default/minimum avoid timings.
- NBG selection checks that returning to the LTG is not more expensive than continuing directly.

## Wiring changes in this round

- Client setup now follows the HLIL order by loading character item weights into the botlib goal state.
- Goal avoid synchronisation now merges orchestrator avoid entries instead of clearing botlib-owned avoid timers each frame.
- Implemented goal helpers are now exposed through `bot_export_t` without shifting existing exported field offsets.
- Focused tests cover item-weight setup, exported goal helper presence, avoid timer preservation, stack emptying, goal naming, and level-item lookup.

