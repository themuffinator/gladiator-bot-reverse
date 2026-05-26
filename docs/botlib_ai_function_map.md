# Botlib AI Module Function Map

The Quake III Arena botlib exports rely on a collection of AI subsystems that
mirror the upstream `be_ai_*` modules. The table below captures the key entry
points expected by the interface export table so reconstructed implementations
can be checked against the retail module boundaries.

| Module | Source Reference | Key Functions Expected by Exports | Notes |
| --- | --- | --- | --- |
| Goal Management | `code/botlib/be_ai_goal.c` | `BotAllocGoalState`, `BotFreeGoalState`, `BotResetGoalState`, `BotLoadItemWeights`, `BotFreeItemWeights`, `BotWeightIndex`, `BotPushGoal`, `BotPopGoal`, `BotGetTopGoal`, `BotChooseLTGItem`, `BotChooseNBGItem`, `BotTouchingGoal` | Provides item weight driven long-term and nearby goal selection along with avoid-goal bookkeeping. Item scoring now follows Q3's `UNDECIDEDFUZZY` path through `FuzzyWeightUndecided`, so registered level items consume the same sampled balance ranges as the retail botlib. |
| Weight Configurations | `code/botlib/be_ai_weight.c` | `BotAllocWeightConfig`, `BotFreeWeightConfig`, `BotLoadWeights`, `BotWriteWeights`, `BotFreeWeightConfig2`, `BotReadWeightsFile`, `BotSetWeight`, `BotFindFuzzyWeight`, `BotFuzzyWeightHandle`, `FuzzyWeightUndecided`, `ScaleWeight`, `ScaleBalanceRange`, `EvolveWeightConfig`, `MergeWeightConfigs`, `InterbreedWeightConfigs` | Parses and caches `*.w` files, including `$evalfloat` / `$evalint` macro-expanded defaults, implicit switch defaults, the 128-weight cap, and Q3's retained filename-keyed cache lookup before filesystem resolution; exposes indexed fuzzy lookup/evaluation through the handle and q2bridge surfaces; reconstructs Gladiator's two-config merge helper, bounded mutation helper, Q3 genetic helpers, the undecided balance sampler with the retail masked random scale, and the HLIL-mapped scale helpers. Goal item scoring consumes `FuzzyWeightUndecided`, goal fuzzy mutation routes straight to `EvolveWeightConfig`, and goal interbreeding writes into the child state's existing config, matching the Q3 wiring. The low-level weight helpers remain callable by the botlib setup path, while the public interface wrappers enforce setup guards. |
| Movement | `code/botlib/be_ai_move.c` | `BotAllocMoveState`, `BotFreeMoveState`, `BotInitMoveState`, `BotResetMoveState`, `BotMoveToGoal`, `BotMoveInDirection`, `BotPredictVisiblePosition`, `BotResetAvoidReach`, `BotResetLastAvoidReach`, `BotReachabilityArea`, `BotMovementViewTarget`, `BotAddAvoidSpot` | Handles reachability analysis, path advancement, and avoidance heuristics for navigation. Dispatch preserves Gladiator's unsupported successor travel warnings for types 15-17, merges pre-dispatch mover diagnostics back into the copied move result, keeps view/prediction route queries on Q3's synthetic reach-end context, reconstructs the exported reachability-area probe ordering, restores direct-move barrier/gap probing and active walk-travel steering, and wires the retail avoid-spot export through the bridge/interface table. |
| Weapon Selection | Gladiator `ai_weapon` HLIL / Q3 `code/botlib/be_ai_weap.c` | `AI_LoadWeaponLibrary`, `AI_WeaponNumberForModel`, `AI_WeaponNameForModel`, `AI_WeaponWeightsBindConfig`, `AI_WeaponWeightsConfigByteSize`, `AI_WeaponWeightsIndexByteSize`, `BotAllocWeaponState`, `BotFreeWeaponState`, `BotResetWeaponState`, `BotLoadWeaponWeights`, `BotFreeWeaponWeights`, `BotWeaponStateSyncFrame`, `BotWeaponStateSetCurrentModel`, `BotChooseBestFightWeapon`, `BotSelectBestFightWeapon`, `BotGetWeaponInfo`, `BotGetTopRankedWeapon` | Parses Gladiator's compact weapon records, pins the recovered `0x158` weapon / `0xd0` projectile retail rows and key field offsets, links projectile pointers, loads fuzzy weapon weights before binding to the active weaponconfig, builds and refreshes fuzzy-weight index tables, reports the separate weight/index allocations used by character setup diagnostics, resolves weapon models with Gladiator's case-insensitive compare helper, syncs the client, inventory pointer, and current model from the live frame before selection, keeps the public chooser side-effect-free like Q3, tracks selected weapon state through the HLIL-style model-change `use` command gate, preserves the cached weapon on a no-winner scoring pass, clears selector timing only through the explicit reset helper, and drains reconstructed weapon-state handles during botlib shutdown. |
| Character Profiles | Gladiator `sub_10029eb0` / Q3 `code/botlib/be_ai_char.c` | `AI_LoadCharacterNamed`, `BotLoadNamedCharacter`, `BotLoadCharacter`, `BotFreeCharacter`, `BotLoadCharacterSkill`, `BotFreeCharacterStrings`, `Characteristic_Float` | Loads named Gladiator `character "name"` definitions through the precompiler, wires item/weapon/chat resources plus their setup indices, and also supports Q3 `skill N` files with included `chars.h` constants, default-character preloading/filling from the cached default handle, interpolation, `BotLoadCharacter` skill clamping, unclamped direct `BotLoadCharacterSkill` fallback, missing-file default fallback, fallback cache identity, the retail exact/default/any-skill fallback cache order, and `bot_reloadcharacters` cache/free semantics including the retail fractional cache-hit exception. Characteristic lookups preserve Gladiator diagnostics, and the export table now guards character entry points like the adjacent AI domains. See `docs/ai_character_reverse_mapping.md` for the Gladiator/Q3 index map. |
| Chat System | `code/botlib/be_ai_chat.c`, `code/game/ai_chat.c` | `BotAllocChatState`, `BotFreeChatState`, `BotLoadChatFile`, `BotFreeChatFile`, `BotQueueConsoleMessage`, `BotRemoveConsoleMessage`, `BotNextConsoleMessage`, `BotEnterChat`, `BotNumInitialChats`, `BotInitialChat`, `BotGetChatMessage`, `BotSetChatGender`, `BotSetChatName`, `BotReplyChat`, `BotChatLength`, `BotNumConsoleMessages`, `StringContains`, `BotFindMatch`, `BotMatchVariable`, `UnifyWhiteSpaces`, `BotReplaceSynonyms` | Manages per-bot chat states, script selection, initial-chat buckets, pending message handoff, reply metadata, setup match/synonym utilities, and queued console messages for in-game dialogue. |

These names reflect the interfaces invoked by `GetBotLibAPI` when the engine
binds botlib exports (see `botlib_export_t` in the Quake III Arena source). As
reconstruction progresses, ensure the same signatures exist under the
`src/botlib/ai*` directories so downstream modules can link without
modification.

## Current Movement Reconstruction Notes

- `BotInitMoveState` is treated as the retail per-frame input refresh, not as a
  full reset. It updates origin, velocity, view state, entity/client fields, and
  the selected `or_moveflags` bits while preserving current reachability and
  retry timers.
- `BotGetReachabilityToGoal` follows the Q3/Gladiator routing shape: enumerate
  outgoing area reachabilities, suppress stale backtracking when the goal did
  not change, validate travel flags and area contents, apply avoid-reach retry
  state, reject avoid-spot collisions, then score by route travel time plus the
  reachability's local travel time.
- `BotMoveToGoal` now performs the HLIL-observed travel dispatch after
  environment classification, mover diagnostics, route reselection, and
  reachability timeout bookkeeping. It also preserves the retail airborne
  finish branch, continuing `lastreachnum` through travel-specific finish
  handlers instead of repathing while the bot is midair. Unsupported successor
  travel types still use the retail warning path.
- Active walk travel now mirrors Q3's reach-start-first steering, near-start
  switch to the reach end, crouch-only area handling via reconstructed area
  presence settings, and `BotGapDistance` speed scaling. Active crouch travel
  now uses the retail fixed 400 speed, and active barrier-jump travel approaches
  `reach->start` before firing the close-range jump state.
- Active walk-off-ledge travel now follows Gladiator's `sub_100327f0` constants:
  far endpoints move at 400, solvable drops use the reconstructed horizontal
  fall velocity, and near-vertical short edges use the binary's `80 + 2 * dist`
  speed curve.
- Active ladder, swim, water-jump, and teleport travel now mirror the compact
  Q3/Gladiator helpers: ladder and water-jump use elementary forward/up action
  bits, swim targets `reach->start` at fixed 400 speed, and teleport approaches
  `reach->start` until `MFL_TELEPORTED` is reported.
- Active normal jump travel now follows the Q3/Gladiator run-start state
  machine: run toward the sampled launch point, then fire immediate or delayed
  jump actions near `reach->start` while moving toward `reach->end` at speed 600.
- `BotMoveInDirection` now mirrors Q3's direct swim/walk helper split. The walk
  helper probes barrier jumps with the retail vertical-forward-down trace
  sequence, reads Gladiator's `sv_step`/`sv_maxbarrier` libvars, scans for
  forward gaps with crouch bounds and the water exception, preserves late-airborne
  barrier continuation, and still emits EA jump/crouch/grapple input for
  accepted direct movement.
- `BotMovementViewTarget` and `BotPredictVisiblePosition` now share the retail
  synthetic route-query setup: each follow-up hop is queried from the previous
  reach endpoint with local last-area bookkeeping, and view lookahead ignores
  live avoid spots instead of treating them as path blockers.
- `BotReachabilityArea` now uses the retail crouch presence box for its bridge
  traces, returns immediately on world hits, and performs the non-mover
  fall-down probe with no pass entity before fuzzy area lookup.

## Chat HLIL String Mapping

HLIL traces of `gladiator.dll` surface a collection of diagnostic strings that
match Quake III Arena's chat loader and response routines. Mapping those
strings to the expected functions allows us to stage stub implementations ahead
of the full translation.

| HLIL String | Observed Context | Expected Function(s) | Upstream Reference |
| --- | --- | --- | --- |
| `"fastchat"`, `"nochat"` libvar probes | Chat initialisation queries a block of libvars (`dmflags`, `fastchat`, `nochat`, etc.) before doing any file work, mirroring the botlib variable cache.| `BotLibVarSet`, `LibVarValue`-style gating around chat triggers. | Gladiator HLIL `sub_10028c30` initialises libvars; Quake III reads `bot_nochat` and toggles chat exports accordingly.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L32137-L32174】【F:dev_tools/Quake-III-Arena-master/code/game/ai_chat.c†L415-L898】 |
| `"bot_testichat"`, `"bot_testrchat"` | Chat test cvars are mirrored into botlib libvars to print candidate initial and reply chats without emitting normal bot commands.| `BotNumInitialChats`, `BotEnterChat`, `BotReplyChat`, `BotReplyChatWithContexts`. | Q3 sets `bot_testichat` during bot setup to run `BotChatTest`, and `bot_testrchat` before calling `BotReplyChat`; botlib prints initial counts, suppresses `BotEnterChat` sends, and dumps all responses in the selected reply block.【F:dev_tools/Quake-III-Arena-master/code/game/ai_main.c†L1225-L1227】【F:dev_tools/Quake-III-Arena-master/code/game/ai_dmq3.c†L4661-L4676】【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_chat.c†L2465-L2485】【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_chat.c†L2747-L2764】【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_chat.c†L2785-L2814】 |
| `"no rchats\n"` console print | Emitted when no reply chats are present after loading the reply chat tables.| `BotLoadReplyChat`, ultimately used by `BotReplyChat`. | HLIL loader around `sub_1002d6a2`; Quake III prints the same message in `BotLoadReplyChat`.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L35770-L35780】【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_chat.c†L1970-L1980】 |
| `"couldn't find chat %s in %s\n"` | Occurs when a chat block cannot be located inside the requested file.| `BotLoadInitialChat` / `BotLoadChatFile`. | HLIL branch `sub_1002d8a0`; Quake III raises the same fatal in `BotLoadInitialChat`.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L36194-L36214】【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_chat.c†L2013-L2078】 |
| `"couldn't load chat %s from %s\n"` | Final failure path after attempting to load and cache a chat file.| `BotLoadChatFile` export, propagating loader errors up to the engine. | HLIL `sub_1002dff0` reports the error; Quake III signals it from `BotLoadChatFile`.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L36258-L36268】【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_chat.c†L2226-L2239】 |
| `"BotConstructChat: message ..."` family | String assembly helper validates message length, random string tables, repeated random expansion, weighted synonym replacement, and variable expansion.| `BotConstructChatMessage`, used by in-game events such as `BotChat_EnterGame`, `BotChat_Kill`, and other response helpers.| HLIL `sub_1002e060` enforces length/random string checks; Quake III's `BotLoadChatMessage` accepts only integer numeric message components as `\vN\` variables before `BotConstructChatMessage` repeats expansion for up to ten passes.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L36290-L36374】【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_chat.c†L862-L914】【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_chat.c†L2289-L2399】 |
| `"enter_game"`, `"exit_game"`, `"start_level"`, `"end_level"` | Event helper strings are passed into the initial-chat constructor from Gladiator game-side chat triggers.| `BotNumInitialChats`, `BotInitialChat`, and the raw initial type storage used by event wrappers. | Gladiator uses these pre-Q3 names in HLIL (`sub_10021e90`, `sub_10021f80`, `sub_10022070`), while Quake III probes successor aliases such as `game_exit` and `level_start` through `BotNumInitialChats`.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L27648-L27767】【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_chat.c†L2407-L2566】 |
| Pending chat message buffer | The game computes chat timing from the constructed message length before entering it into chat output; `BotInitialChat` and `BotReplyChat` only construct pending text, `BotSetChatName` stores the command-source client, and `BotEnterChat` drains the text into `say`, `say_team`, or `tell` with its `clientto` argument reserved for the tell target.| `BotInitialChat`, `BotReplyChat`, `BotChatLength`, `BotGetChatMessage`, `BotEnterChat`, `BotSetChatName`. | Gladiator game-side code calls the message-length helper after `sub_1002e510`, while the botlib path matches Quake III's split between `BotInitialChat`, `BotReplyChat`, `BotChatLength`, `BotEnterChat`, `BotGetChatMessage`, and the owner-client metadata set by `BotSetChatName`.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L36701-L36968】【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_chat.c†L2407-L2566】【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_chat.c†L2614-L2869】 |
| Recent chat-message timers | Initial and reply chat messages carry per-line timestamps so recently used lines are skipped while alternatives are available.| `BotChooseInitialChatMessage`, `BotInitialChat`, `BotReplyChat`. | Q3 initializes chat-message recency, filters candidates by `AAS_Time()`, marks selected messages for `CHATMESSAGE_RECENTTIME`, and falls back when every line is still recent.【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_chat.c†L71-L71】【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_chat.c†L1962-L1962】【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_chat.c†L2407-L2454】【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_chat.c†L2700-L2764】 |
| Reply priority scan | Reply table entries compare every matching key set and keep the highest numeric priority before constructing a response.| `BotLoadReplyChat`, `BotReplyChat`, `BotChat_ParseReplyKeys`. | Gladiator HLIL `sub_1002e7d0` iterates the global reply list, compares the stored priority against the current best, and only then constructs the selected chat message; Q3's `BotReplyChat` performs the same best-priority scan.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L36703-L36830】【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_chat.c†L2614-L2764】 |
| Reply key-list grammar | Reply blocks require at least one key, allow a comma to be omitted between adjacent keys, and reserve bare names for Q3's special `name` / gender keys while normal keys and `<...>` bot-name entries remain quoted strings. | `BotChat_ParseReplyKeys`, `BotChat_ParseReplyKeyText`. | Q3 allocates and parses one key before it will accept `]`, consumes a comma opportunistically after each key, checks special bare tokens before falling back to `TT_STRING`, and reads `<...>` names through `TT_STRING` tokens only.【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_chat.c†L1862-L1930】 |
| Plain reply-key word matching | String reply keys use Q3's `StringContainsWord` boundaries: only spaces, periods, commas, and exclamation marks terminate a word.| `BotChat_ReplyKeyMatches`, `BotReplyChat`. | Q3 reply selection calls `StringContainsWord` for `RCKFL_STRING`, so punctuation such as `?`, `:`, `_`, and `-` stays inside the word rather than acting as a boundary.【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_chat.c†L495-L530】【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_chat.c†L2638-L2645】 |
| Parenthesized reply keys such as `("i am ", 0)` | Reply keys capture the span matched by integer numeric variables and response messages substitute that span through `\vN\` construction.| `BotChat_ParseReplyKeys`, `BotChat_ReplyRuleMatches`, `BotConstructChatMessage`. | Quake III's `BotLoadMatchPieces` rejects non-integer, out-of-range, and adjacent variable tokens before `StringsMatch` fills `bot_match_t.variables[]`; Gladiator HLIL passes the match table into `BotConstructChatMessage` before dispatch.【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_chat.c†L1139-L1167】【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_chat.c†L1434-L1490】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L36701-L36968】 |
| Reply `mcontext` / `vcontext` split | Q3 reply construction uses the message context for weighted output synonyms and a separate variable context for captured reply variables, then appends fixed var0-var7 replacements.| `BotReplyChatWithContexts`, `BotConstructChatMessage`, `BotReplaceReplySynonyms`. | Q3 callers pass `context, CONTEXT_REPLY, NULL... botname, netname`; the botlib constructor canonicalizes reply variables with `BotReplaceReplySynonyms(temp, vcontext)` and applies weighted output synonyms with `mcontext`.【F:dev_tools/Quake-III-Arena-master/code/game/ai_dmq3.c†L4666-L4687】【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_chat.c†L2289-L2399】【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_chat.c†L2614-L2764】 |
| Match utility exports | Setup-loaded `match.c` templates retain their `MTCONTEXT_*` filter, message subtype, and Q3-valid variable pieces so game code can classify console messages and pull captured variables. | `BotFindMatch`, `BotMatchVariable`, `StringContains`, `UnifyWhiteSpaces`, `BotReplaceSynonyms`. | Q3 exposes these helpers next to the chat exports; Gladiator HLIL carries the same `BotMatchVariable: variable out of range` diagnostic and match-template loader region.【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_chat.c†L440-L464】【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_chat.c†L761-L761】【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_chat.c†L1139-L1167】【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_chat.c†L1434-L1490】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L35081-L35081】 |
| Match template metadata | Numeric `match.c` context labels and `(message type, subtype)` tuple fields are accepted only as integer tokens after preprocessing. | `BotChat_ParseMatchScript`, `BotChat_ParseMatchTemplate`, `BotFindMatch`. | Q3 `BotLoadMatchTemplates` reads the context, message type, and subtype through integer token expectations before registering the template; the reconstruction keeps identifier support for preserved macro names but rejects float numeric metadata rather than truncating it.【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_chat.c†L1258-L1332】 |
| `<"bot", ...>` reply keys and tilde stripping | Bot-name list keys are parsed as special reply-key metadata, and constructed chat removes `~` markers when text is fetched or sent.| `BotChat_ParseReplyKeys`, `BotReplyChat`, `BotGetChatMessage`, `BotEnterChat`. | Q3 parses `<...>` into `RCKFL_BOTNAMES` and strips tildes at `BotEnterChat` / `BotGetChatMessage`; Gladiator HLIL exposes the same reply-key flag bits and pending-message handoff offsets.【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_chat.c†L1828-L1981】【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_chat.c†L421-L433】 |
| `synfile`, `rndfile`, `matchfile`, `rchatfile` | Setup reads the same libvar-selected asset names as Quake III and skips `rchatfile` when `nochat` is non-zero.| `BotSetupChatAI` / `BotShutdownChatAI`. | HLIL `sub_1002ebb0` loads the three shared assets first and only then gates reply chat loading on `nochat`; `sub_1002ec80` frees the cached lists.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L36922-L36948】【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_chat.c†L2960-L3015】 |
| `j_sub_1002ebb0()` in setup and `j_sub_1002ec80()` in shutdown | Library setup invokes chat setup after weapon/item setup and shutdown tears the shared chat cache down before item/weapon teardown.| `Botlib_SetupAISubsystem`, `Botlib_ShutdownAISubsystem`, `BotSetupChatAI`, `BotShutdownChatAI`. | Gladiator HLIL `sub_10029c90` calls weapon setup, item setup, then chat setup before allocating bot-state storage; `sub_10029da0` invokes the chat shutdown bridge before item and weapon shutdown.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L32727-L32748】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L32754-L32760】 |
| `j_sub_100356d0()` in shutdown | Library shutdown reaches the weapon AI teardown after chat/item cleanup, which must also clean the reconstructed exported handle table before the shared config memory is released.| `Botlib_ShutdownAISubsystem`, `BotShutdownWeaponAI`, `BotAllocWeaponState`, `BotFreeWeaponState`. | Gladiator embeds weapon state inside each bot client, but the Q3-style export surface exposes handle-indexed states; the reconstruction now drains those handles during `BotShutdownLibrary` so a later setup cycle does not observe stale weapon-state slots.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L32754-L32760】 |
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
- `BotInitialChat` now stores the constructed text in the chat state, so
  `BotChatLength`, `BotGetChatMessage`, and `BotEnterChat` expose the same
  pending-message handoff used by the retail game timing and send paths.
- The retail chat test libvars are now wired: `bot_testichat` prints
  `BotNumInitialChats` diagnostics and causes `BotEnterChat` to print pending
  text instead of sending it, while `bot_testrchat` dumps every constructed
  response from the winning reply rule without marking recent timers or
  dispatching through the bridge.
- Chat states now retain the bot's chat name, client, and gender metadata.
  Character/profile loading seeds those fields, and reply-chat keys such as
  unquoted `name`, `female`, `male`, and `it` use that metadata during matching.
- Reply chat now scans all matching reply rules and selects the highest priority
  response, matching the HLIL/Q3 interpretation of the numeric value after the
  key list. The parser also handles Q3 `<"bot", ...>` bot-name lists.
- `BotReplyChatWithContexts` now exposes the Q3 split reply-construction path:
  `mcontext` drives weighted output synonyms, `vcontext` drives reply-variable
  synonym canonicalization, and fixed var0-var7 slots cover the game-side
  bot-name/player-name wiring.
- Pending chat text now strips retail `~` markers when callers fetch or send it
  through `BotGetChatMessage` or `BotEnterChat`; reply construction leaves the
  text pending for that same handoff instead of dispatching immediately.
- Initial chat type buckets and reply response lists now retain per-message
  recent-use timestamps. Selection skips recent lines when alternatives are
  available and falls back to a retail-compatible candidate when every line is
  still within the recent window.
- Setup-loaded match templates now preserve `MTCONTEXT_*` masks and subtypes.
  The bridge exports `StringContains`, `UnifyWhiteSpaces`, `BotReplaceSynonyms`,
  `BotFindMatch`, and `BotMatchVariable`, giving game-side chat command and
  obituary parsing access to the recovered setup cache.
- `BotSetupChatAI` now follows the address-backed setup sequence by reading
  `synfile`, `rndfile`, `matchfile`, and conditionally `rchatfile`, while
  `BotShutdownChatAI` frees the shared setup cache.
- The interface AI setup path now calls `BotSetupChatAI` in the HLIL-observed
  sequence and `BotShutdownChatAI` during AI teardown, so setup-loaded random,
  synonym, match, and reply assets become the shared fallback for per-bot
  personality chat states.
- Reply chat keys are now retained instead of skipped. Parenthesized keys with
  numeric captures feed `\vN\` response variables, and readable match
  placeholders such as `{VICTIM}` / `{KILLER}` are captured from incoming
  obituary-style messages before construction.
