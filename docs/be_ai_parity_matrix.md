# be_ai Parity Matrix

This matrix tracks the Quake III / Gladiator `be_ai_*` exports against the in-repo `src/botlib/ai*` implementations to surface missing or divergent behaviour.

## Goal module
| Function | Retail reference | Gladiator status | Acceptance criteria |
| --- | --- | --- | --- |
| BotResetGoalState | `be_ai_goal.h` export set.【F:dev_tools/Quake-III-Arena-master/code/game/be_ai_goal.h†L52-L118】 | implemented | Reset stack/avoidance and last reachability in place; matches `bot_goal.c` handle guard path.【F:src/botlib/ai_goal/bot_goal.c†L141-L168】 |
| BotResetAvoidGoals | `be_ai_goal.h` export set.【F:dev_tools/Quake-III-Arena-master/code/game/be_ai_goal.h†L52-L118】 | implemented | Clears avoid-goal list and count for a valid state.【F:src/botlib/ai_goal/bot_goal.c†L300-L322】 |
| BotRemoveFromAvoidGoals | `be_ai_goal.h` export set.【F:dev_tools/Quake-III-Arena-master/code/game/be_ai_goal.h†L52-L118】 | implemented | Removes matching avoid goal and compacts entries.【F:src/botlib/ai_goal/bot_goal.c†L364-L384】 |
| BotPushGoal / BotPopGoal / BotEmptyGoalStack | Stack exports in retail header.【F:dev_tools/Quake-III-Arena-master/code/game/be_ai_goal.h†L58-L76】 | implemented | Push/pop preserve LIFO semantics with overflow trimming per `bot_goal.c`.【F:src/botlib/ai_goal/bot_goal.c†L450-L506】 |
| BotDumpAvoidGoals / BotDumpGoalStack | Diagnostics expected by exports.【F:dev_tools/Quake-III-Arena-master/code/game/be_ai_goal.h†L64-L68】 | implemented | Emit formatted state contents to BotLib logging for valid handles.【F:src/botlib/ai_goal/bot_goal.c†L1042-L1107】 |
| BotGoalName | Retail helper for debug output.【F:dev_tools/Quake-III-Arena-master/code/game/be_ai_goal.h†L68-L82】 | implemented | Resolves level item names or falls back to numeric identifier.【F:src/botlib/ai_goal/bot_goal.c†L1000-L1021】 |
| BotGetTopGoal / BotGetSecondGoal | Stack peek helpers from retail.【F:dev_tools/Quake-III-Arena-master/code/game/be_ai_goal.h†L70-L82】 | implemented | Return highest entries when available; handle guards match `bot_goal.c`.【F:src/botlib/ai_goal/bot_goal.c†L508-L551】 |
| BotChooseLTGItem / BotChooseNBGItem | Retail item selection exports.【F:dev_tools/Quake-III-Arena-master/code/game/be_ai_goal.h†L74-L83】 | implemented | Score items using travel time and weights, respecting avoid lists and respawn timers.【F:src/botlib/ai_goal/bot_goal.c†L760-L906】 |
| BotTouchingGoal | Retail proximity predicate.【F:dev_tools/Quake-III-Arena-master/code/game/be_ai_goal.h†L82-L94】 | implemented | Bounding-box containment check around goal origin.【F:src/botlib/ai_goal/bot_goal.c†L942-L972】 |
| BotAvoidGoalTime / BotSetAvoidGoalTime | Retail avoidance timers.【F:dev_tools/Quake-III-Arena-master/code/game/be_ai_goal.h†L93-L100】 | implemented | Lookup/update avoid goal expiry using global time cache.【F:src/botlib/ai_goal/bot_goal.c†L386-L415】 |
| BotLoadItemWeights / BotFreeItemWeights | Retail weight IO.【F:dev_tools/Quake-III-Arena-master/code/game/be_ai_goal.h†L106-L114】 | implemented | Resolve asset path, load weight config, and allocate indices or free allocations.【F:src/botlib/ai_goal/bot_goal.c†L220-L287】 |
| BotAllocGoalState / BotFreeGoalState | Retail lifecycle entry points.【F:dev_tools/Quake-III-Arena-master/code/game/be_ai_goal.h†L111-L118】 | implemented | Handle-indexed allocations with per-client metadata and cleanup guards.【F:src/botlib/ai_goal/bot_goal.c†L76-L134】 |
| BotItemGoalInVisButNotVisible | Vision parity hook.【F:dev_tools/Quake-III-Arena-master/code/game/be_ai_goal.h†L84-L92】【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_goal.c†L1637-L1667】 | implemented | Uses AAS-solid visibility trace plus stale-entity timing gate (`ltime` parity analogue) to preserve retail "in vis but not visible" behavior. |
| BotGetLevelItemGoal | Retail classname search.【F:dev_tools/Quake-III-Arena-master/code/game/be_ai_goal.h†L86-L92】 | implemented | Provides classname-indexed resumable search over registered level items and fills `bot_goal_t` for matches. |
| BotGetNextCampSpotGoal | Retail camp navigation.【F:dev_tools/Quake-III-Arena-master/code/game/be_ai_goal.h†L88-L94】 | implemented | Iterates registered camp/roam level items and returns next valid goal with resumable index semantics. |
| BotGetMapLocationGoal | Retail location lookup.【F:dev_tools/Quake-III-Arena-master/code/game/be_ai_goal.h†L89-L94】 | implemented | Resolves `target_location` records parsed from the BSP entity lump using case-insensitive name matching and retail-sized goal bounds. |
| BotInitLevelItems | Retail level scan.【F:dev_tools/Quake-III-Arena-master/code/game/be_ai_goal.h†L97-L104】【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_goal.c†L537-L672】 | implemented | Rebuilds item/map-location/camp caches by loading item defs and parsing the BSP entity lump, including spawn filter flags and camp metadata. |
| BotUpdateEntityItems | Retail dynamic updates.【F:dev_tools/Quake-III-Arena-master/code/game/be_ai_goal.h†L97-L104】【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_goal.c†L1009-L1175】 | implemented | Refreshes dropped/temporary entity goals each frame, handling expiration and origin/area updates. |
| BotInterbreedGoalFuzzyLogic / BotMutateGoalFuzzyLogic | Retail genetic utilities.【F:dev_tools/Quake-III-Arena-master/code/game/be_ai_goal.h†L101-L106】 | implemented | Interbreeds/mutates goal fuzzy logic using weight-config helpers with state-handle guards. |
| BotSaveGoalFuzzyLogic | Retail persistence hook.【F:dev_tools/Quake-III-Arena-master/code/game/be_ai_goal.h†L103-L106】 | implemented | Serializes current goal fuzzy logic through `WriteWeightConfig`. |
| BotSetupGoalAI / BotShutdownGoalAI | Retail init/teardown.【F:dev_tools/Quake-III-Arena-master/code/game/be_ai_goal.h†L115-L118】 | implemented | Wires init/shutdown lifecycle for goal states and level-item caches with idempotent guards. |

## Weight module
| Function | Retail reference | Gladiator status | Acceptance criteria |
| --- | --- | --- | --- |
| ReadWeightConfig / FreeWeightConfig | Weight config exports.【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_weight.h†L63-L70】 | implemented | Parse weight scripts (with global defines) and free fuzzy separators when `bot_reloadcharacters` permits reloading.【F:src/botlib/ai_weight/ai_weight.c†L166-L274】【F:src/botlib/ai_weight/bot_weight.c†L31-L74】 |
| FuzzyWeight / FindFuzzyWeight | Retail evaluators.【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_weight.h†L69-L74】 | implemented | Expose fuzzy weight lookup through handle table; return 0 or -1 on invalid handle/weight number.【F:src/botlib/ai_weight/bot_weight.c†L174-L252】【F:src/botlib/ai_weight/bot_weight.c†L344-L382】 |
| BotLoadWeights / BotWriteWeights | Retail IO equivalents to load/write weight configs.【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_weight.h†L63-L74】 | implemented | Handle-based load/write with asset resolution and fatal logging on missing files; preserves cached configs.【F:src/botlib/ai_weight/bot_weight.c†L118-L213】【F:src/botlib/ai_weight/bot_weight.c†L264-L319】 |
| BotSetWeight / BotFuzzyWeightHandle | Retail runtime adjustors.【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_weight.h†L69-L74】 | implemented | Allow runtime reassignment of fuzzy leaf values and evaluation over inventories via stored configs.【F:src/botlib/ai_weight/bot_weight.c†L322-L382】 |
| WriteWeightConfig | Standalone writer expected by retail.【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_weight.h†L67-L74】 | implemented | Non-handle writer now mirrors retail signature and reuses the shared writer path. |
| FuzzyWeightUndecided | Retail undecided branch evaluator.【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_weight.h†L71-L74】 | implemented | Provides undecided-branch evaluation path through recursive fuzzy traversal. |
| ScaleWeight / ScaleBalanceRange | Retail scaling utilities.【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_weight.h†L74-L78】 | implemented | Scales named weights and balance ranges without breaking fuzzy tree links. |
| EvolveWeightConfig | Retail evolution helper.【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_weight.h†L78-L82】 | implemented | Evolves weight configurations via mutation helpers used by goal fuzzy logic APIs. |
| InterbreedWeightConfigs | Retail interbreeding helper.【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_weight.h†L79-L82】 | implemented | Interbreeds parent configs into child outputs using shared crossover helpers. |
| BotShutdownWeights | Retail cache cleanup.【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_weight.h†L81-L83】 | implemented | Frees cached handles/configs and resets global weight state on shutdown. |

## Move module
| Function | Retail reference | Gladiator status | Acceptance criteria |
| --- | --- | --- | --- |
| BotResetMoveState / BotInitMoveState | Retail lifecycle entry points.【F:dev_tools/Quake-III-Arena-master/code/game/be_ai_move.h†L112-L134】 | implemented | Clear state, set presencetype and origin data, and honour or_moveflags; errors flagged on invalid handles.【F:src/botlib/ai_move/bot_move.c†L608-L706】 |
| BotMoveToGoal / BotMoveInDirection | Retail movement primitives.【F:dev_tools/Quake-III-Arena-master/code/game/be_ai_move.h†L115-L118】 | implemented | Drive AAS reachability and direction-based motion with moveresult flags and grapple support.【F:src/botlib/ai_move/bot_move.c†L754-L1151】 |
| BotResetAvoidReach | Retail avoid reach reset.【F:dev_tools/Quake-III-Arena-master/code/game/be_ai_move.h†L118-L122】 | implemented | Clears avoid reach arrays and times on the move state.【F:src/botlib/ai_move/bot_move.c†L707-L727】 |
| BotAllocMoveState / BotFreeMoveState | Retail allocation APIs.【F:dev_tools/Quake-III-Arena-master/code/game/be_ai_move.h†L128-L134】 | implemented | Handle-indexed allocations with fatal logging on exhaustion or misuse.【F:src/botlib/ai_move/bot_move.c†L94-L189】 |
| BotTravel_Grapple | Retail travel variant (inline in be_ai_move).【F:dev_tools/Quake-III-Arena-master/code/game/be_ai_move.h†L112-L134】 | implemented | Implements grapple reachability including hook precache, view alignment, and slack release.【F:src/botlib/ai_move/bot_move.c†L24-L203】 |
| BotResetLastAvoidReach | Retail reachability reset helper.【F:dev_tools/Quake-III-Arena-master/code/game/be_ai_move.h†L118-L123】 | implemented | Tracks and clears last avoided reachability metadata and timers on demand. |
| BotReachabilityArea | Retail area probe.【F:dev_tools/Quake-III-Arena-master/code/game/be_ai_move.h†L122-L125】 | implemented | Resolves area index via trace + AAS fallback handling for solids, movers, and water transitions. |
| BotMovementViewTarget | Retail lookahead target helper.【F:dev_tools/Quake-III-Arena-master/code/game/be_ai_move.h†L124-L126】【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_move.c†L843-L891】 | implemented | Computes lookahead target along reachability chains and fills target vector on success. |
| BotPredictVisiblePosition | Retail prediction helper.【F:dev_tools/Quake-III-Arena-master/code/game/be_ai_move.h†L125-L127】【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_move.c†L912-L968】 | implemented | Predicts a reachable position visible from goal by traversing reachability and LOS checks. |
| BotAddAvoidSpot | Retail avoid-spot adder.【F:dev_tools/Quake-III-Arena-master/code/game/be_ai_move.h†L134-L137】 | implemented | Adds/updates/clears avoid spots with `AVOID_CLEAR` semantics and capped slot management. |
| BotSetBrushModelTypes | Retail map-init hook.【F:dev_tools/Quake-III-Arena-master/code/game/be_ai_move.h†L135-L138】 | implemented | Integrates mover model-type reset path used during map lifecycle initialisation. |
| BotSetupMoveAI / BotShutdownMoveAI | Retail init/teardown.【F:dev_tools/Quake-III-Arena-master/code/game/be_ai_move.h†L138-L141】 | implemented | Wires movement subsystem setup/shutdown and handle cleanup through botlib lifecycle. |

## Weapon module
| Function | Retail reference | Gladiator status | Acceptance criteria |
| --- | --- | --- | --- |
| BotAllocWeaponState / BotFreeWeaponState / BotResetWeaponState | Retail lifecycle exports.【F:dev_tools/Quake-III-Arena-master/code/game/be_ai_weap.h†L89-L104】 | implemented | Handle-indexed weapon states clear cached weights/config and log on invalid handles.【F:src/botlib/ai_weapon/bot_weapon.c†L16-L120】 |
| BotLoadWeaponWeights | Retail load hook.【F:dev_tools/Quake-III-Arena-master/code/game/be_ai_weap.h†L95-L100】 | implemented | Loads weight + config pair, attaches to state, and reports `BLERR_CANNOTLOADWEAPONWEIGHTS` on failure.【F:src/botlib/ai_weapon/bot_weapon.c†L122-L182】 |
| BotChooseBestFightWeapon / BotGetTopRankedWeapon | Retail selection helpers.【F:dev_tools/Quake-III-Arena-master/code/game/be_ai_weap.h†L93-L99】 | implemented | Evaluate fuzzy weights over inventory and cache last best weapon/weight for subsequent queries.【F:src/botlib/ai_weapon/bot_weapon.c†L184-L245】 |
| BotGetWeaponInfo | Retail info fetch.【F:dev_tools/Quake-III-Arena-master/code/game/be_ai_weap.h†L94-L99】 | implemented | Copies weapon info for indexed weapon, guarding bounds and null configs.【F:src/botlib/ai_weapon/bot_weapon.c†L247-L280】 |
| BotSetupWeaponAI / BotShutdownWeaponAI | Retail init/teardown.【F:dev_tools/Quake-III-Arena-master/code/game/be_ai_weap.h†L89-L92】 | implemented | Loads weapon library during setup and frees global weapon state/config handles during shutdown. |

## Character module
| Function | Retail reference | Gladiator status | Acceptance criteria |
| --- | --- | --- | --- |
| BotLoadCharacter / BotLoadCharacterSkill | Retail character loaders.【F:dev_tools/Quake-III-Arena-master/code/game/be_ai_char.h†L33-L48】 | implemented | Case-insensitive handle caching keyed by filename+skill with refcounts and asset load logging.【F:src/botlib/ai_character/bot_character.c†L59-L151】 |
| BotFreeCharacter / BotFreeCharacterStrings | Retail free paths.【F:dev_tools/Quake-III-Arena-master/code/game/be_ai_char.h†L33-L47】 | implemented | Decrement refcount, free profile when zero, or free provided profile strings for transient callers.【F:src/botlib/ai_character/bot_character.c†L153-L210】 |
| Characteristic_Float / BFloat / Integer / BInteger / String | Retail characteristic accessors.【F:dev_tools/Quake-III-Arena-master/code/game/be_ai_char.h†L37-L47】 | implemented | Route through loaded profile with clamping/bounds and safe buffer handling for strings.【F:src/botlib/ai_character/bot_character.c†L220-L305】 |
| BotShutdownCharacters | Retail cache shutdown.【F:dev_tools/Quake-III-Arena-master/code/game/be_ai_char.h†L33-L48】 | implemented | Releases cached character handles and clears the cache table on shutdown. |

## Chat module
| Function / HLIL symbol | Retail reference | Gladiator status | Acceptance criteria |
| --- | --- | --- | --- |
| BotLoadSynonyms / `sub_1002b110` | Chat setup loads `syn.c` before matching.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L33833-L34223】 | implemented | `BotLoadChatFile` loads sibling `syn.c`, preserves `CONTEXT_*` names or numeric fallbacks, and exposes `CONTEXT_NEARBYITEM` phrases to reply matching. |
| BotLoadRandomStrings / RandomString | Retail random table loader and selector.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L34224-L34480】 | implemented | Parses sibling or inline `name = { ... }` random tables and expands `\rname\` references during construction. |
| BotLoadMatchTemplates | Retail `match.c` parser.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L34756-L35496】 | divergent | Loads sibling `match.c` and registers key obituary templates; parser is intentionally tolerant of unsupported late-file constructs pending full static-function split. |
| BotLoadReplyChat / BotFreeReplyChat | Retail reply table lifecycle.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L35497-L35780】 | implemented | Parses `[ ... ] = context { ... }` reply blocks, preserves `no rchats` diagnostics, and frees parsed tables with chat state cleanup. |
| BotLoadInitialChat / BotLoadChatFile / BotFreeChatFile | Retail named chat-file lifecycle.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L35815-L36268】 | implemented | Loads named `chat "name"` blocks, handles missing-chat diagnostics, and scans shared `rnd.c`, `syn.c`, and `match.c` assets in the same load path. |
| BotNumInitialChats / BotInitialChat | Retail initial-chat selector and constructor.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L36578-L36594】【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_chat.c†L2407-L2566】 | implemented | Retains raw initial-chat type buckets, resolves Gladiator/Q3 type aliases, exposes bridge extension slots, and constructs queued messages with supplied variable replacements. |
| BotConstructChatMessage | Retail chat string constructor.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L36273-L36700】 | divergent | Expands random refs plus `\vN\` and readable `{PLACEHOLDER}` captures with length/error checks, repeats random expansion for up to ten passes, and applies weighted synonym replacement for initial-chat `CONTEXT_*` masks; reply construction still lacks the separate Q3 `mcontext`/`vcontext` split. |
| BotReplyChat | Retail reply selector.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L36701-L36852】 | divergent | Parses reply keys, applies `!`/`&` key semantics, captures parenthesized `0` variables and match-template placeholders, dispatches through the bridge, and preserves construction failures; full retail priority/time scoring remains approximate. |
| BotChatLength | Retail length helper.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L36853-L36921】 | implemented | Returns constructed string length through the public helper path. |
| BotSetupChatAI / BotShutdownChatAI | Retail global setup/teardown.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L36922-L36948】 | implemented | Exposes the global setup/shutdown exports, reads `synfile`, `rndfile`, `matchfile`, and conditionally `rchatfile`, returns `BLERR_NOERROR`, and frees cached setup assets on shutdown. |

## Remediation schedule (prioritised by gameplay impact)
- [x] High: finish retail map-entity parity in `BotInitLevelItems` (entity lump parsing, game-type filters, respawn metadata seeding).
- [x] Medium: align `BotItemGoalInVisButNotVisible` with explicit AAS visibility semantics before trace fallback decisions.
- [x] Medium: complete `BotGetMapLocationGoal` parity for `target_location`-style location metadata and naming rules.

