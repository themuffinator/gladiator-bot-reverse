# Botlib AI Module Function Map

The Quake III Arena botlib exports rely on a collection of AI subsystems that
mirror the upstream `be_ai_*` modules. The table below captures the key entry
points expected by the interface export table so reconstructed implementations
can be checked against the retail module boundaries.

| Module | Source Reference | Key Functions Expected by Exports | Notes |
| --- | --- | --- | --- |
| Goal Management | `code/botlib/be_ai_goal.c` | `BotAllocGoalState`, `BotFreeGoalState`, `BotResetGoalState`, `BotLoadItemWeights`, `BotFreeItemWeights`, `BotWeightIndex`, `BotPushGoal`, `BotPopGoal`, `BotGetTopGoal`, `BotChooseLTGItem`, `BotChooseNBGItem`, `BotTouchingGoal` | Provides item weight driven long-term and nearby goal selection along with avoid-goal bookkeeping. |
| Weight Configurations | `code/botlib/be_ai_weight.c` | `BotAllocWeightConfig`, `BotFreeWeightConfig`, `BotLoadWeights`, `BotWriteWeights`, `BotFreeWeightConfig2`, `BotReadWeightsFile`, `BotSetWeight` | Parses `*.w` files, exposes indexed weight lookups, and serialises adjustments back to disk. The low-level weight helpers remain callable by the botlib setup path, while the public interface wrappers enforce setup guards. |
| Movement | `code/botlib/be_ai_move.c` | `BotAllocMoveState`, `BotFreeMoveState`, `BotInitMoveState`, `BotResetMoveState`, `BotMoveToGoal`, `BotMoveInDirection`, `BotPredictVisiblePosition`, `BotResetAvoidReach`, `BotResetLastAvoidReach`, `BotReachabilityArea`, `BotMovementViewTarget` | Handles reachability analysis, path advancement, and avoidance heuristics for navigation. Dispatch preserves Gladiator's unsupported successor travel warnings for types 15-17 and merges pre-dispatch mover diagnostics back into the copied move result. |
| Weapon Selection | `code/botlib/be_ai_weapon.c` | `BotAllocWeaponState`, `BotFreeWeaponState`, `BotResetWeaponState`, `BotLoadWeaponWeights`, `BotFreeWeaponWeights`, `BotChooseBestFightWeapon`, `BotGetWeaponInfo`, `BotGetTopRankedWeapon` | Evaluates weapon weightings per opponent context and tracks per-client weapon state. |
| Character Profiles | `code/botlib/be_ai_char.c` | `BotLoadCharacter`, `BotFreeCharacter`, `BotLoadCharacterSkill`, `BotFreeCharacterStrings`, `BotInterbreedCharacters`, `BotDefaultCharacteristic`, `Characteristic_Float` | Loads `.chr` definitions, exposes characteristic lookups, and supports bot evolution utilities. |
| Chat System | `code/botlib/be_ai_chat.c`, `code/game/ai_chat.c` | `BotAllocChatState`, `BotFreeChatState`, `BotLoadChatFile`, `BotFreeChatFile`, `BotQueueConsoleMessage`, `BotRemoveConsoleMessage`, `BotNextConsoleMessage`, `BotEnterChat`, `BotNumInitialChats`, `BotInitialChat`, `BotReplyChat`, `BotChatLength`, `BotNumConsoleMessages` | Manages per-bot chat states, script selection, initial-chat buckets, and queued console messages for in-game dialogue. |

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
| `"BotConstructChat: message ..."` family | String assembly helper validates message length, random string tables, repeated random expansion, weighted synonym replacement, and variable expansion.| `BotConstructChatMessage`, used by in-game events such as `BotChat_EnterGame`, `BotChat_Kill`, and other response helpers.| HLIL `sub_1002e060` enforces length/random string checks; Quake III's `BotConstructChatMessage` performs the same validations and repeats expansion for up to ten passes before dispatching event-specific chats.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L36290-L36374】【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_chat.c†L2289-L2399】 |
| `"enter_game"`, `"exit_game"`, `"start_level"`, `"end_level"` | Event helper strings are passed into the initial-chat constructor from Gladiator game-side chat triggers.| `BotNumInitialChats`, `BotInitialChat`, and the raw initial type storage used by event wrappers. | Gladiator uses these pre-Q3 names in HLIL (`sub_10021e90`, `sub_10021f80`, `sub_10022070`), while Quake III probes successor aliases such as `game_exit` and `level_start` through `BotNumInitialChats`.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L27648-L27767】【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_chat.c†L2407-L2566】 |
| Parenthesized reply keys such as `("i am ", 0)` | Reply keys capture the span matched by numeric variables and response messages substitute that span through `\vN\` construction.| `BotChat_ParseReplyKeys`, `BotChat_ReplyRuleMatches`, `BotConstructChatMessage`. | Quake III's `StringsMatch` fills `bot_match_t.variables[]`; Gladiator HLIL passes the match table into `BotConstructChatMessage` before dispatch.【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_chat.c†L1434-L1490】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L36701-L36968】 |
| `synfile`, `rndfile`, `matchfile`, `rchatfile` | Setup reads the same libvar-selected asset names as Quake III and skips `rchatfile` when `nochat` is non-zero.| `BotSetupChatAI` / `BotShutdownChatAI`. | HLIL `sub_1002ebb0` loads the three shared assets first and only then gates reply chat loading on `nochat`; `sub_1002ec80` frees the cached lists.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L36922-L36948】【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_chat.c†L2960-L3015】 |
| `rnd.c`, `syn.c`, `match.c` sibling loads | Retail reply chat depends on shared random tables, synonym contexts, and obituary match templates that live beside `rchat.c` rather than inside it.| `BotLoadChatFile`, `BotChat_ParseRandomStringTables`, `BotChat_ParseSynonymContextsFromScript`, `BotChat_ParseMatchScript`. | The loader now mirrors the multi-asset setup implied by the HLIL chat-load stages and Quake III's separate initial, reply, and match table construction paths. |
| Macro-preserved `MSG_*` / match variables | Gladiator chat assets rely on precompiler macros such as `MSG_DEATH`, `VICTIM`, `NETNAME`, and `GENDER_HIM`; preserving those tokens keeps reconstructed templates readable and context-aware.| `PS_CreateScriptFromSource`, `PC_ShouldPreserveChatDefine`, `BotChat_MessageTypeFromToken`, `BotChat_RewriteVariablesForMessageType`. | This supports retail `match.c` registrations such as `{VICTIM} commits suicide` while still allowing numeric macro fallback when the token stream has already expanded a define. |

These correlations keep `src/botlib/ai_chat` aligned with the actual chat event
flow (`BotChat_EnterGame`, `BotChat_Kill`, etc.) observed in both the HLIL dump
and id Software's GPL source.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L35770-L36374】【F:dev_tools/Quake-III-Arena-master/code/game/ai_chat.c†L415-L898】

## Current Chat Reconstruction Notes

- `PS_CreateScriptFromSource` now materializes precompiled source tokens into a
  rewindable script snapshot. This matches the multi-pass loader shape used by
  chat parsing: random strings, synonyms, match templates, reply tables, and
  named initial chat blocks can all scan the same precompiled asset view.
- The precompiler keeps chat symbols such as `MSG_*` and match placeholders
  (`VICTIM`, `KILLER`, `NETNAME`, etc.) symbolic so the reconstructed chat
  layer can map them back to readable template text while still accepting
  numeric-expanded legacy input for context constants.
- Loading `rchat.c` now pulls sibling retail assets (`rnd.c`, `syn.c`, and
  `match.c`) when present. This reflects the original split between reply
  tables, random-string tables, synonym contexts, and obituary match templates.
- Named initial-chat blocks are retained in raw type buckets as well as mapped
  event contexts. This recovers the Q3-style `BotNumInitialChats`/`BotInitialChat`
  path and lets Gladiator names (`exit_game`, `start_level`, `end_level`) answer
  successor probes (`game_exit`, `level_start`, `level_end`).
- The bridge export table now exposes `BotNumInitialChats` and `BotInitialChat`
  as appended reconstruction extension slots, leaving the original public ABI
  ordering isolated while making the recovered initial-chat path callable.
- Chat construction now mirrors the Q3 repeated random-expansion loop and applies
  the post-expansion weighted synonym pass when `BotInitialChat` receives a
  `CONTEXT_*` mask from the game wrapper.
- `BotSetupChatAI` now follows the address-backed setup sequence by reading
  `synfile`, `rndfile`, `matchfile`, and conditionally `rchatfile`, while
  `BotShutdownChatAI` frees the shared setup cache.
- Reply chat keys are now retained instead of skipped. Parenthesized keys with
  numeric captures feed `\vN\` response variables, and readable match
  placeholders such as `{VICTIM}` / `{KILLER}` are captured from incoming
  obituary-style messages before construction.
