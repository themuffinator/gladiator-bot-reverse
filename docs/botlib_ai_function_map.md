# Botlib AI Module Function Map

The Quake III Arena botlib exports rely on a collection of AI subsystems that
mirror the upstream `be_ai_*` modules. The table below captures the key entry
points expected by the interface export table so we can plan stub/proxy
implementations while the reverse-engineered logic is reconstructed.

| Module | Source Reference | Key Functions Expected by Exports | Notes |
| --- | --- | --- | --- |
| Goal Management | `code/botlib/be_ai_goal.c` | `BotAllocGoalState`, `BotFreeGoalState`, `BotResetGoalState`, `BotLoadItemWeights`, `BotFreeItemWeights`, `BotWeightIndex`, `BotPushGoal`, `BotPopGoal`, `BotGetTopGoal`, `BotChooseLTGItem`, `BotChooseNBGItem`, `BotTouchingGoal` | Provides item weight driven long-term and nearby goal selection along with avoid-goal bookkeeping. |
| Weight Configurations | `code/botlib/be_ai_weight.c` | `BotAllocWeightConfig`, `BotFreeWeightConfig`, `BotLoadWeights`, `BotWriteWeights`, `BotFreeWeightConfig2`, `BotReadWeightsFile`, `BotSetWeight` | Parses `*.w` files, exposes indexed weight lookups, and serialises adjustments back to disk. |
| Movement | `code/botlib/be_ai_move.c` | `BotAllocMoveState`, `BotFreeMoveState`, `BotInitMoveState`, `BotResetMoveState`, `BotMoveToGoal`, `BotMoveInDirection`, `BotPredictVisiblePosition`, `BotResetAvoidReach`, `BotResetLastAvoidReach`, `BotReachabilityArea`, `BotMovementViewTarget` | Handles reachability analysis, path advancement, and avoidance heuristics for navigation. |
| Weapon Selection | `code/botlib/be_ai_weapon.c` | `BotAllocWeaponState`, `BotFreeWeaponState`, `BotResetWeaponState`, `BotLoadWeaponWeights`, `BotFreeWeaponWeights`, `BotChooseBestFightWeapon`, `BotGetWeaponInfo`, `BotGetTopRankedWeapon` | Evaluates weapon weightings per opponent context and tracks per-client weapon state. |
| Character Profiles | `code/botlib/be_ai_char.c` | `BotLoadCharacter`, `BotFreeCharacter`, `BotLoadCharacterSkill`, `BotFreeCharacterStrings`, `BotInterbreedCharacters`, `BotDefaultCharacteristic`, `Characteristic_Float` | Loads `.chr` definitions, exposes characteristic lookups, and supports bot evolution utilities. |
| Chat System | `code/botlib/be_ai_chat.c`, `code/game/ai_chat.c` | `BotAllocChatState`, `BotFreeChatState`, `BotLoadChatFile`, `BotFreeChatFile`, `BotQueueConsoleMessage`, `BotRemoveConsoleMessage`, `BotNextConsoleMessage`, `BotEnterChat`, `BotReplyChat`, `BotChatLength`, `BotNumConsoleMessages` | Manages per-bot chat states, script selection, and queued console messages for in-game dialogue. |

These names reflect the interfaces invoked by `GetBotLibAPI` when the engine
binds botlib exports (see `botlib_export_t` in the Quake III Arena source). As
reconstruction progresses, ensure the same signatures exist under
`src/botlib/ai/` so downstream modules can link without modification.
