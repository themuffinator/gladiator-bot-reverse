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
reconstruction progresses, ensure the same signatures exist under the
`src/botlib/ai*` directories so downstream modules can link without
modification.

## Chat HLIL String Mapping

HLIL traces of `gladiator.dll` surface a collection of diagnostic strings that
match Quake III Arena's chat loader and response routines. Mapping those
strings to the expected functions allows us to stage stub implementations ahead
of the full translation.

| HLIL String | Observed Context | Expected Function(s) | Upstream Reference |
| --- | --- | --- | --- |
| `"fastchat"`, `"nochat"` libvar probes | Chat initialisation queries a block of libvars (`dmflags`, `fastchat`, `nochat`, etc.) before doing any file work, mirroring the botlib variable cache.| `BotLibVarSet`, `LibVarValue`-style gating around chat triggers. | Gladiator HLIL `sub_10028c30` initialises libvars; Quake III reads `bot_nochat` and toggles chat exports accordingly.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L32137-L32174】【F:dev_tools/Quake-III-Arena-master/code/game/ai_chat.c†L415-L898】 |
| `"no rchats\n"` console print | Emitted when no reply chats are present after loading the reply chat tables.| `BotLoadReplyChat`, ultimately used by `BotReplyChat`. | HLIL loader around `sub_1002d6a2`; Quake III prints the same message in `BotLoadReplyChat`.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L35770-L35780】【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_chat.c†L1970-L1980】 |
| `"couldn't find chat %s in %s\n"` | Occurs when a chat block cannot be located inside the requested file.| `BotLoadInitialChat` / `BotLoadChatFile`. | HLIL branch `sub_1002d8a0`; Quake III raises the same fatal in `BotLoadInitialChat`.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L36194-L36214】【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_chat.c†L2013-L2078】 |
| `"couldn't load chat %s from %s\n"` | Final failure path after attempting to load and cache a chat file.| `BotLoadChatFile` export, propagating loader errors up to the engine. | HLIL `sub_1002dff0` reports the error; Quake III signals it from `BotLoadChatFile`.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L36258-L36268】【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_chat.c†L2226-L2239】 |
| `"BotConstructChat: message ..."` family | String assembly helper validates message length, random string tables, and variable expansion.| `BotConstructChatMessage`, used by in-game events such as `BotChat_EnterGame`, `BotChat_Kill`, and other response helpers.| HLIL `sub_1002e060` enforces length/random string checks; Quake III's `BotConstructChatMessage` performs the same validations before dispatching event-specific chats.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L36290-L36374】【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_chat.c†L2289-L2399】 |

These correlations ensure the new stubs in `src/botlib/ai_chat` can call into
the script precompiler and structure their TODOs around the actual chat event
flow (`BotChat_EnterGame`, `BotChat_Kill`, etc.) observed in both the HLIL dump
and id Software's GPL source.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L35770-L36374】【F:dev_tools/Quake-III-Arena-master/code/game/ai_chat.c†L415-L898】
