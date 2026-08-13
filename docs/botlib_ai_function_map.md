# Botlib AI Module Function Map

The Quake III Arena botlib exports rely on a collection of AI subsystems that
mirror the upstream `be_ai_*` modules. The table below captures the key entry
points expected by the interface export table so reconstructed implementations
can be checked against the retail module boundaries.

| Module | Source Reference | Key Functions Expected by Exports | Notes |
| --- | --- | --- | --- |
| Goal Management | `code/botlib/be_ai_goal.c` | `BotAllocGoalState`, `BotFreeGoalState`, `BotResetGoalState`, `BotLoadItemWeights`, `BotFreeItemWeights`, `BotWeightIndex`, `BotPushGoal`, `BotPopGoal`, `BotGetTopGoal`, `BotGetSecondGoal`, `BotChooseLTGItem`, `BotChooseNBGItem`, `BotTouchingGoal`, `BotGetLevelItemGoal`, `BotUpdateEntityItems` | Provides the recovered item-weight, fixed avoid-table, stack, map-item, selector, and dynamic-entity paths behind the Q3-style handle adapters. Retail client setup directly calls `AI_GoalBotlib_LoadItemWeights`, which reaches the canonical `BotLoadItemWeights` core; the extended export currently uses the same adapter, but that alias is not the basis for setup/cache behavior. The core sends the caller filename to `ReadWeightConfigUncached`, so each successful goal-state load receives a distinct owned config and preserves the retail immediate-overwrite/failure semantics outside the general filename cache. The index covers every `iteminfo` entry in declaration order. `BotSetupGoalAI` loads only the global item config; `BotShutdownGoalAI` frees only that config and nulls its pointer, while client teardown owns goal states and item weights. Raw LTG/NBG selection has no last-valid-area fallback, admits raw unlinked records, ignores the compatibility-only `next_respawn_time`, scores timed items with `+20`, pushes only `GFL_ITEM`, arms avoidance before stack push, keeps the strict NBG ceiling/return-route check, and retains timed items in jump-pad areas. Per-map setup loads/parses the BSP entity lump, samples `notspawnflags`, resolves model indices, and scans the prepended entity list. Dynamic refresh expires timed records before AAS availability checks, enumerates each entity as `AAS_NextEntity` → model index → entity info, and lets the first matching active-list record handle it. Intentional safety adaptations bound the retail N-1 pool defect, check failed dropped-item allocations, and free the temporary BSP list. |
| Weight Configurations | `code/botlib/be_ai_weight.c` | `BotAllocWeightConfig`, `BotFreeWeightConfig`, `BotLoadWeights`, `BotWriteWeights`, `BotFreeWeightConfig2`, `BotReadWeightsFile`, `BotSetWeight`, `BotFindFuzzyWeight`, `BotFuzzyWeightHandle`, `FuzzyWeightUndecided`, `ScaleWeight`, `ScaleBalanceRange`, `EvolveWeightConfig`, `MergeWeightConfigs`, `InterbreedWeightConfigs` | Parses and caches `*.w` files, including `$evalfloat` / `$evalint` macro-expanded defaults, implicit switch defaults, the 128-weight cap, Q3's retained filename-keyed cache lookup before filesystem resolution, Gladiator's `"couldn't find %s\n"` / `"counldn't load %s\n"` load diagnostics, and the broad `FreeWeightConfig` reload gate that retains even direct loads while `bot_reloadcharacters` is false; exposes indexed fuzzy lookup/evaluation through the handle and q2bridge surfaces; reconstructs Gladiator's two-config merge helper, bounded mutation helper, Q3 genetic helpers, the undecided balance sampler with the retail masked random scale, and the HLIL-mapped scale helpers. Goal item scoring consumes `FuzzyWeightUndecided`, goal fuzzy mutation routes straight to `EvolveWeightConfig`, and goal interbreeding writes into the child state's existing config, matching the Q3 wiring. The low-level weight helpers remain callable by the botlib setup path, while the public interface wrappers enforce setup guards. |
| Movement | `code/botlib/be_ai_move.c` | `BotAllocMoveState`, `BotFreeMoveState`, `BotInitMoveState`, `BotResetMoveState`, `BotMoveToGoal`, `BotMoveInDirection`, `BotPredictVisiblePosition`, `BotResetAvoidReach`, `BotResetLastAvoidReach`, `BotReachabilityArea`, `BotMovementViewTarget`, `BotAddAvoidSpot` | Handles reachability analysis, path advancement, and avoidance heuristics for navigation. Dispatch preserves Gladiator's unsupported successor travel warnings for type 13 and types 15-17, merges pre-dispatch mover diagnostics back into the copied move result, keeps view/prediction route queries on Q3's synthetic reach-end context, reconstructs the exported reachability-area probe ordering, restores direct-move barrier/gap probing plus `AAS_PredictClientMovement` rejection, active walk/jump/elevator/rocket-jump/jump-pad/func_bobbing steering, and wires the retail avoid-spot export through the bridge/interface table. The raw elementary-action ABI writes the byte-1 half of the actionflags dword for the last two flags: `EA_MoveRight` sets bit `0x100` (`sub_100374f0`, `flags:1.b |= 1` at `0x10037503`) and `EA_DelayedJump` bit `0x200` (`sub_10037390`, `flags:1.b |= 2` at `0x100373ae`), matching `ACTION_MOVERIGHT` 256 / `ACTION_DELAYEDJUMP` 512 in the host's `botlib.h`. Only jump/moveup (`0x08`), crouch/movedown (`0x10`) and the jump latch riding on move-left (`0x80`, `sub_100374c0`) are genuinely aliased bits. |
| Game-side Combat | Gladiator `sub_10021500`, `sub_100215e0`, `sub_10021650`, `sub_100226c0`, `sub_100228c0`, `sub_10022930`, `sub_10022990`, `sub_10022e10`, `sub_10023970`, `sub_10024590` / Q3 `code/game/ai_dmq3.c` | `BotAI_UseItems`, `BotAI_BattleUseItems`, `BotAI_CarryingFlag`, `BotAI_Aggression`, `BotAI_WantsToRetreat`, `BotAI_WantsToChase`, `BotAI_CanAndWantsToRocketJump`, `BotAttackMove`, `BotFindEnemy`, `BotCheckAttack` | The reconstructed combat caller now uses Gladiator's 15-bit random scale, characteristic-48 pizza preference, characteristic-4 attack-skill floor and 100-180-unit low-skill hold band, fixed `+0.1` strafe clock, skill-derived direction threshold, probabilistic strafe flips, characteristic-driven crouch/jump handling, the crouch timer and alternating jump latch, direct low-skill movement calls, and the high-skill two-attempt failed-strafe retry. It does not synthesize the enemy-change strafe reset or attack-chase activation that retail omits. The otherwise dormant entry branch now preserves the strict future-time deadline before random and characteristic work, constructs the cached enemy entity/area/origin goal with retail's -8/+8 bounds, rebuilds movement state, and passes the caller's travel flags to `BotMoveToGoal`; raw DLL inspection finds only the deadline read at `0x10022e3e`, while Q3 leaves its sole prospective writer commented out, so a narrow test injector covers the branch without activating it in production. `BotFindEnemy` now scans at most sixteen visible one-based client entities in numeric order and preserves the retail live-player, characteristic-45 range, private-view FOV, team-precedence, damage, 300-unit, shooting-frame, candidate-facing, retreat, and state-write rules. Its high-level callers now retain the recovered node-specific acquisition schedule. Attack eligibility reads characteristic 11 in `[0,1]`, waits for the exact reaction-time boundary, then submits `EA_Attack` every eligible frame without an invented generic cooldown. Aim submission preserves the retail 16-bit angle grid, shortest-angle turn, characteristic-9/10 additive acceleration and asymmetric slowdown, explicit frame think time, and strict aim-accuracy `> 0.8` direct snap. Its weapon-aware stages reproduce the eye-plus-weapon-offset boxed trace, target `+8/+16` quirk, characteristic-7 linear projectile lead, Rocket Launcher square-root accuracy, target jitter, Railgun normalized perturbation, pitch/yaw spread call order, and radial ground targeting with the strict retail `> 150`, `< 50`, and `< 60` gates. `BotCheckAttack` delegates center/bottom/top FOV/PVS samples, dry/fluid mask and direction adjustment, and translucent-fluid continuation to retail `AAS_EntityVisible`, then applies the weapon sweep, teammate and splash safety, window follow-up, and fire-on-release latch. The retained Quake II dvis data supplies this PVS query. The earlier reaction-time-as-projectile-lead approximation is gone; reaction time only gates firing. Battle inventory now runs before fuzzy weapon scoring: `sub_10021020` resolves the retail image table for four powerup deadlines and the power-shield grace state, while `sub_10021290` projects enemy distance, height, all twelve weapon-byte one-hot slots, and QUAD/PENT/powerscreen effects. Exact stale gaps are preserved, including the armor-icon-zero branch and enemy powershield slot; ground/water flags remain movement inputs rather than invented inventory fields. General and battle item-use ordering, flag carry, aggression, retreat, chase, and the raw rocket-jump eligibility decision now match retail.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L10934-L11035】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L26925-L27034】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L27037-L27104】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L27133-L27165】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L28100-L28129】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L28276-L28509】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L28732-L29298】【F:src/botlib/interface/bot_interface.c】【F:src/botlib/ai/ai_dm.c】【F:tests/parity/test_bot_interface.c】【F:tests/ai/test_ai_dm.c】 |
| Weapon Selection | Gladiator `ai_weapon` HLIL / Q3 `code/botlib/be_ai_weap.c` | `AI_LoadWeaponLibrary`, `AI_WeaponNumberForModel`, `AI_WeaponNameForModel`, `AI_WeaponWeightsBindConfig`, `AI_WeaponWeightsConfigByteSize`, `AI_WeaponWeightsIndexByteSize`, `BotAllocWeaponState`, `BotFreeWeaponState`, `BotResetWeaponState`, `BotLoadWeaponWeights`, `BotLoadWeaponWeightsFresh`, `BotFreeWeaponWeights`, `BotWeaponStateSyncFrame`, `BotWeaponStateSetCurrentModel`, `BotChooseBestFightWeapon`, `BotSelectBestFightWeapon`, `BotGetWeaponInfo`, `BotGetTopRankedWeapon` | Parses Gladiator's compact weapon records, pins the recovered `0x158` weapon / `0xd0` projectile retail rows and key field offsets, links projectile pointers, passes fuzzy weapon-weight filenames directly into the selected cached or fresh load path before binding to the active weaponconfig, proves retained filename-keyed reuse through deleted-fixture `AI_LoadWeaponWeights` and `BotLoadWeaponWeights` paths, and gives every retail client a distinct weapon-state-owned config through `BotLoadWeaponWeightsFresh`. It builds and refreshes fuzzy-weight index tables, resolves weapon models with Gladiator's case-insensitive compare helper, syncs the client, inventory pointer, and current model from the live frame before selection, keeps the public chooser side-effect-free like Q3, tracks selected weapon state through the HLIL model-change gate using the dedicated `EA_UseItem` token/argument/terminator call shape, preserves the cached weapon on a no-winner scoring pass, clears selector timing only through the explicit reset helper, and releases the state-owned weights when the client weapon state is torn down. Game-side selection is node-local: Fight, active Retreat, and Battle NBG synchronise and select; Battle NBG refreshes enemy inventory after movement before selecting, while Stand, goal nodes, Chase, and idle Retreat do not invoke the selector.【F:src/botlib/ai_weapon/bot_weapon.c】【F:src/botlib/interface/bot_interface.c】【F:tests/ai/test_ai_weapon.c】【F:tests/ai/test_ai_character.c】 |
| Character Profiles | Gladiator `sub_10029eb0` / Q3 `code/botlib/be_ai_char.c` | `AI_LoadCharacterDefinition`, `AI_LoadCharacterNamed`, `BotLoadCharacter`, `BotFreeCharacter`, `BotLoadCharacterHandle`, `BotLoadNamedCharacterHandle`, `BotLoadCharacterSkillHandle`, `BotFreeCharacterHandle`, `BotFreeCharacterStringsHandle`, `BotShutdownCharacterHandles`, `Characteristic_*`, `Characteristic_*Handle` | Splits the two ABIs explicitly. The unsuffixed Gladiator family loads a fresh named `bot_character_t *`, returns a mutable raw packed `char *`, and never enters the handle cache. The returned x86 allocation is exactly a four-byte highest-index header, `highest + 1` eight-byte slots, then strings; native x64 keeps the order with pointer-width alignment. Internal counts/skill/name/string size live in host-allocated sidecars, so bot-memory counters see one tracked definition. Direct free removes both; subsystem shutdown drains intentionally abandoned second-open/pass-two/setup-retry pairs before arena teardown. The two-pass parser merges matches through EOF, accepts matching-block EOF while retaining the retail expected-token diagnostics from both passes, prefixes malformed-definition errors with source file/line context, rejects duplicate/trailing invalid definitions, safely includes the hidden highest slot, and logs loose logical paths versus PAK container/entry provenance exactly. Float-to-integer access uses signed-64 truncation plus low-dword return, including x87 wrap and invalid-zero behavior. Production setup uses the canonical uncached goal-item load, fresh weapon weights, and per-client chat loading regardless of `nochat`; the definition owns none. It does not blanket-reset or centrally destroy normal partial failures, instead reusing retained owner handles/objects on retry, and missing raw weight strings do not fall back. Shutdown runs any allowed `exit_game` chat before chat → weapon → goal/item → character → console waypoints → successor transient teardown; moves retain the original bound client field. The explicit Q3 handle family supplies the `MAX_CLIENTS` handle table, buffer-copy string facade, `bot_reloadcharacters` cache/free split, default preload/fill/fallback ladder, clamping/interpolation, and the 80-public-slot/index-80-allocation quirk. See `docs/ai_character_reverse_mapping.md` for full parser, lifetime, and retry details.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L32494-L32570】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L32810-L33312】【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_char.c†L129-L610】【F:src/botlib/ai/ai_character.c】【F:src/botlib/ai_character/bot_character.c】【F:src/botlib/interface/bot_interface.c】【F:tests/ai/test_ai_character.c】 |
| Chat System | `code/botlib/be_ai_chat.c`, `code/game/ai_chat.c` | `BotAllocChatState`, `BotFreeChatState`, `BotLoadChatFile`, `BotFreeChatFile`, `BotQueueConsoleMessage`, `BotRemoveConsoleMessage`, `BotNextConsoleMessage`, `BotEnterChat`, `BotNumInitialChats`, `BotInitialChat`, `BotGetChatMessage`, `BotSetChatGender`, `BotSetChatName`, `BotChatName`, `BotChatClient`, `BotReplyChat`, `BotChatLength`, `BotNumConsoleMessages`, `StringContains`, `BotFindMatch`, `BotMatchVariable`, `UnifyWhiteSpaces`, `BotReplaceSynonyms` | Manages per-bot chat states, script selection, initial-chat buckets, pending message handoff, reply metadata, setup match/synonym utilities, and diagnostic access to the retained reply persona/client metadata. Incoming console text uses the shared retail `max_messages` node heap with AAS timestamps, FIFO state lists, non-destructive node peek, and identity removal; legacy destructive/type wrappers remain separate. The per-client dispatcher now pins the retail side effects for match types 3–21: help/accompany/defend and CTF orders, leadership/status/subteam/formation/dismissal, plus camp/checkpoint/patrol. Remaining differences are architectural adapters around node ownership, native pointers, semantic client IDs, the absent general AI-node graph, and synonym/literal-template interoperation. |

## Retail goal/interface wiring boundary

- `BotSetupGoalAI` loads only the global raw `itemconfig`; it does not allocate
  the level-item pool or scan BSP entities. Whole-library setup loads weapons
  first, then the goal config, then invokes chat setup while ignoring its
  result; goal failure does not roll back the weapon load. The raw item parser
  uses the direct requested path, strict `iteminfo` grammar, signed-short
  `type`/`index` fields, structure-parser vector edge cases, and atomic failure.
  Asset lookup accepts a loose file or probes only `pak0.pak` through
  `pak9.pak` in ascending order; arbitrary package names and `pak10.pak` or
  later are outside retail.
- After the ignored chat result, AI setup allocates the two cleared retail
  client tables: `maxclients * 0x11d0` bytes for fixed-stride bot records and
  `maxclients * 0x90` bytes for presentation settings. Slot addresses remain
  stable for that setup lifetime. The retail inclusive `client <= maxclients`
  guard is represented by separate aligned host sentinels at the endpoint, so
  it remains observable without indexing beyond either exact allocation.
- `BotMoveClient` copies all `0x11d0` bytes into the inactive destination and
  zeros all `0x11d0` bytes at the source; embedded client/entity identities
  move unchanged. The separate settings table remains host-slot-indexed and is
  not copied. Shutdown releases slot owners, then frees the settings table
  before the state table. Native x64 pointers are confined to a state adapter
  that must fit the retail slot; the external `0x11d0`/`0x90` strides and
  allocation sizes do not widen.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L32577-L32595】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L32727-L32760】【F:src/botlib/interface/bot_state.c】【F:src/botlib/interface/bot_state.h】【F:src/botlib/interface/botlib_interface.c】【F:tests/parity/test_bot_interface.c】
- A named `BotLoadMap` calls `AAS_LoadMap` before mutating host caches. Only a
  successful AAS load resets bridge caches, records model/asset tables, resets
  existing bot states, and invokes `BotInitLevelItems`. A null name returns the
  AAS status directly and performs no goal/map adapter rebuild.
- With AAS uninitialized, `BotAI` is a successful no-op before inactive-client
  validation. Otherwise the client think runs first and the global throttled
  `BotUpdateEntityItems` pass runs afterward; selectors do not refresh dynamic
  items on demand.
- Seek NBG and ordinary item LTG completion use touch plus the recovered
  item-visibility/validity predicate. Battle NBG is deliberately touch-only,
  so disappearance of a traced item cannot end the battle detour by itself.
- The x86 raw item config/item info, level item, goal, goal state, and weight
  config layouts remain size/offset exact. Native x64 pointer widening and Q3
  ownership/metadata live in sidecars and adapters rather than changing the
  retail records.
- The recovered 20-function `bot_export_t` is the complete retail prefix.
  Successor goal APIs live only in `bot_export_extended_t`. Library shutdown
  clears imports and both export blocks after teardown; callers must reacquire
  `GetBotAPI` before setting up again, which reconstructs each table without
  inserting extension entries into the retail prefix.

The long table rows above predate the current scheduler pass where they describe a
monolithic think loop or an absent AI-node graph. The active reconstruction now
uses the retail 50-switch scheduler, node-owned enemy/sight state, and the
Stand, Seek LTG, Activate, Seek NBG, Fight, Chase, Retreat, and Battle NBG
node identities. Its shared pmove preamble enters reset Observer and
Intermission nodes, sends the gated `end_level` and `start_level` events at
their distinct handoffs, and retains the two-second no-chat intermission wait.
Stand always tests for an enemy before its pending-chat typing wait, retaining
that pending message if the visible-enemy transition interrupts it; on each
no-enemy tick it advances the retained private view turn before evaluating that
strict post-deadline handoff; the expiry then preserves that turn into Seek
LTG's same-frame work. A non-zero private `__squatt` guard instead preserves
Stand and its pending chat while sending the retail warning say and `removebot`
command.
It also latches one valid-position, characteristic-gated `enter_game` event
during the first eight seconds after setup, while dead/gib clients retain the
retail reset, death-chat, strict post-typing-time one-shot-respawn, and normal Seek-LTG
re-entry sequence, including the first-alive-frame gate before regular movement. Seek LTG now executes its random-chat handoff before
enemy acquisition and goal selection: help/accompany/rush-base orders veto
it, its first trial is `0.1 * thinktime`, fast chat bypasses the two
probability gates, and a valid position selects `random_misc` or
`random_insult` before Stand. For five seconds after a recorded enemy death,
Seek LTG also performs the retail per-frame trial that sends only wave gesture
0 or 2 immediately before its enemy scan. Battle Fight now turns a retained dead enemy into
the gated characteristic-19 kill chat (telefrag before insult/praise), holding
Stand for the constructed message's typing time before returning to Seek LTG.
Its active combat body selects a weapon, then applies the Battle
Quad/Invulnerability pass and the general item-use pass before its attack move.
Battle Chase receives its own strict ten-second entry deadline and now routes directly
to a retained eight-unit last-seen goal, zeros that deadline on contact or
after movement reaches the remembered enemy area, and performs the
once-per-second nearby-item search before entering Battle NBG.
Its active mover refreshes the retained enemy battle inventory and runs the
general item-use pass before move-state setup; active Battle NBG and Retreat
run that same general item-use pass immediately before their own mover setup.
Battle NBG now holds its own nearby goal, verifies the retained enemy, refreshes
the reachable last-enemy area and origin, applies the strict timeout/contact
pop, and returns to Fight or Retreat before using
the ordinary LTG routing again. Retreat now validates the enemy, exits to a
fresh Chase when safe, promotes a carried CTF flag to Rush Base only when that
LTG changes (clearing its away clock and giving it a 120-second deadline), then
resolves the home-flag goal directly rather than fabricating a stack entry, and
shares the nearby-item handoff while preserving its no-rocket-jump travel mask. All three
direct battle paths now retain mover-provided movement/swim views and preserve
an explicitly mover-set EA view without a private turn. Immediately after each
direct `BotMoveToGoal`, they also run the recovered `BotAIBlocked` activation /
perpendicular-retry handoff; Chase and Retreat clear the retained LTG
deadline on mover failure, while NBG clears the shared nearby-goal lease.
Chase's fixed 300-unit route-lookahead fallback feeds the private accelerated
turn otherwise. Fight deliberately leaves its mover untouched at node entry;
BotAttackMove initializes it only after the pizza-preference and minimum
attack-skill gates, plus in its dormant future attack-chase branch. Its move
vector originates at the player body, whereas weapon-aware aim originates at
the eye position. The 100-180-unit hold branch is strictly below attack skill
0.4; the exact boundary uses strafing. The rocket-jump setting is a travel-mask
permission, not a Fight-tail autonomous jump action.
Battle NBG invokes weapon-aware aim only when movement does not own the view,
then checks attack and applies its separate non-view-set turn. Retreat preserves
its own movement-view, low attack-skill lookahead, and enemy-aim alternatives
before the same recovered attack gate. Weapon selection is likewise node-local:
Fight, active Retreat, and Battle NBG select, with Battle NBG moving, syncing
its weapon frame, then updating enemy inventory before selection; Stand, goal
nodes, Chase, and idle Retreat do
not. Before resolving a team or ordinary item goal, Seek LTG also replays the
retail autonomous CTF selection: a carrier receives Rush Base; otherwise an
unassigned aggressive bot uses the 0.33/0.66 choice to get the enemy flag,
defend its home flag for 120 seconds, or wait through the 60-second CTF roam
lease. Ordinary Seek LTG now probes nearby
items on the retail strict half-second cadence, immediately re-enters
Seek-NBG to move a selected item through a separately counted scheduler switch, uses the defend-only 1500
travel-unit budget (700 otherwise), and pushes a five-second Seek-NBG goal
that pops back to the underlying LTG on contact, missing-visible-item, failure,
or expiry through the same-frame node loop without a synthetic probe delay. It refreshes the move state only
immediately before a direct `BotMoveToGoal`, leaving idle and nearby-goal
handoff paths untouched. Once a direct team branch declines, Seek LTG passes its top stack
goal straight to `BotMoveToGoal` with the node's retail travel mask; failed
moves reset only the mover's last avoid reach rather than the compatibility
goal-avoid list. The ordinary item branch now retains a chosen LTG for twenty
seconds, uses the complete item reach/absence/vertical-overlap predicate, and
clears both avoid layers when no replacement remains selectable. A failed
direct move also immediately expires that lease, matching the retail retry
transition. Remaining
scheduler work is concentrated in other high-level goal bodies. Help, accompany, defend, CTF get-flag/
rush-base, camp, and patrol now return their retail direct long-term goals
before ordinary item selection. Help and accompany use the teammate entity's
reachable current AAS goal with their distinct near-distance holds and strict
expiry/visibility clears. The accompany hold shares battle's crouch timer,
reconstructs retail arrival/crouch/gesture input, faces the companion for the
first two seconds, and otherwise selects a safe ten-try roam target as its idle
view. Get-flag selects the opposing flag and rush-base selects the home flag
only while carrying one, retaining its post-touch away timer. The direct
branches preserve delayed team acknowledgements where retail emits them, strict
expiry, movement failure avoid reset, defend/camp near-goal behavior, and
patrol's forward-bit ping-pong traversal.

These names reflect the interfaces invoked by `GetBotLibAPI` when the engine
binds botlib exports (see `botlib_export_t` in the Quake III Arena source). As
reconstruction progresses, ensure the same signatures exist under the
`src/botlib/ai*` directories so downstream modules can link without
modification.

## Current Combat Decision Reconstruction Notes

- `BotAI_UseItems` maps `sub_10021500` as four independent ordered readers:
  owned Silencer, then the eye-position `PointContents & 0x38` Rebreather gate,
  then inactive Power Shield and Power Screen. Every owned/active test uses the
  raw slots and strict signed comparisons, so one call may submit all four
  exact item names without normalising stale inventory.
- `BotAI_BattleUseItems` maps `sub_100215e0`: an inactive owned Quad submits
  `"Quad Damage"` and returns immediately; only the fallback can submit owned,
  inactive `"Invulnerability"`.
- `BotAI_CarryingFlag` maps `sub_10021650` exactly: ordered nonzero CTF values
  enable the raw flag slots, flag one takes precedence with result 1, and flag
  two returns 2. Zero and unordered values disable both checks.
- `BotAI_Aggression` maps `sub_100226c0` over the battle inventory without
  refreshing or normalising it. Own invulnerability overrides the later gates;
  enemy powerups, the inclusive height/health/armor boundaries, and all eight
  strict weapon/ammunition pairs preserve their retail order and raw slots.
- `BotAI_WantsToRetreat` maps `sub_100228c0` as flag carry, LTG type 4, then
  aggression `< 50`. `BotAI_WantsToChase` maps `sub_10022930` as aggression
  `> 50` only, deliberately omitting the extra flag/LTG cases present in Quake
  III.
- Seek LTG/NBG and Battle Chase/Retreat/NBG share the retail nearby-goal
  deadline/probe pair at `0xaec`/`0xaf8`. Failed Chase and Retreat movement
  clear the separate retained LTG deadline at `0xae8`, while failed Battle NBG
  movement clears only the shared nearby-goal lease.
- `BotAI_CanAndWantsToRocketJump` maps `sub_10022990` over raw slots 14, 21,
  204, and 205 before the health and armor gates. Own invulnerability bypasses
  survivability and characteristic 26; otherwise health must be at least 60,
  sub-90 health needs one exact 40/50/60 armor threshold, and bounded weapon
  jumping accepts at `>= 0.5`. The global `rocketjump` variable remains a
  caller-side gate, as in retail, and this helper neither refreshes nor mutates
  stale inventory.
  These helpers feed the exact `BotFindEnemy` retreat fallback. Enemy selection
  scans up to sixteen visible one-based client entities in numeric order and
  preserves the retail live-player, 900/810/300-unit, team-precedence,
  shooting-frame, candidate-facing, and retreat gates. It updates only the
  current enemy and sight time on success while always retaining the current
  health baseline. The reconstructed 50-transition node loop now limits
  acquisition to the retail node phases, retains Fight/Chase enemy ownership,
  keeps Activate committed to its standalone activation goal through movement,
  then scans for an enemy before its private view turn; its reached/expired
  handoff remains before movement. Seek-NBG likewise exits missing, reached,
  and expired goals before movement through the same-frame successor loop. Its
  runes-enabled reached-goal handoff also
  compares CTF tech models and sends `drop tech` for a conflicting held tech,
  retaining the raw tech-four Haste exception. Both live Activate and Seek-NBG
  acquisition passes run after movement and precede their private view turn;
  Chase retains its entry deadline. The lifecycle now resets and holds Observer and
  Intermission, dispatches its level-transition events, and gates the one-shot
  enter-game chat before the dead/gib death-chat and delayed-respawn path.
  Help, defend, camp, and patrol now run through the direct LTG mover with
  their retail message/expiry and near-goal state transitions. Help resolves
  a reachable current teammate goal and clears on the strict stale-visibility
  boundary. Accompany formation behavior and CTF get-flag/rush-base now run
through their recovered direct LTG branches. Generic Seek LTG/NBG nearby-item
selection also retains its strict probe and expiry handoff, and the ordinary
top-stack LTG now uses the same direct mover/failure-reset path plus its
twenty-second item lease, retail view selection (explicit mover view, waiting
roam glance, or 300-unit route look-ahead), and exhausted-candidate recovery
that empties the goal stack before clearing both avoidance layers. Its direct
team and ordinary movement exits now preserve their ideal target and take the
private accelerated view turn unless the mover has explicitly set its view;
general item use follows the nearby-goal probe and directly precedes only an
active goal mover, so a no-goal idle turn or successful nearby handoff emits
no premature item use;
remaining
scheduler work is in other high-level goal bodies. Camp arrival now keeps the
retail random idle look, crouch clock, swimming reset, and water/lava/slime
cancellation. Battle Retreat now retains its no-goal idle view turn instead of
redirecting through the Stand node without general item use, and its later Chase handoff uses the
separate strict chase predicate even while get-flag initially requests retreat.
Battle Chase, NBG, and Retreat also keep a
mover-set view intact rather than applying `sub_10029150` afterwards, and each
calls `sub_10025560` after its direct mover result. Blocked direct movement now
matches `sub_10025560` and its `sub_10024a10` resolver: a blocked model first
walks reverse `target` links through up to ten `trigger_counter` / `trigger_relay`
nodes to locate a button or trigger; shootable doors and buttons select Blaster,
aim, and attack; reachable resolved buttons and trigger brushes store a single
ten-second activation goal; all other blocks use the retained alternating
perpendicular retry and expire the active Seek-NBG/LTG lease.
- The private `sub_10029150` view turn uses its fixed 100/150 acceleration
  values precisely when the enemy entity is zero; active enemy turns alone
  query characteristics 9 and 10.
- Bot state now keeps retail's distinct zero-based client slot and one-based
  world entity number. AAS visibility, traces, movement initialization,
  teammate tests, attack checks, console enemy deaths, and client-backed goals
  use the entity identity, while EA/chat/input calls continue to use the client
  slot. `BotMoveClient` moves the complete record without rewriting either
  identity; the zero-based `ltg_teammate` storage remains an explicit boundary
  adapter.
  【F:dev_tools/gladiator.dll.bndb_hlil.txt†L27133-L27165】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L27171-L27189】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L28010-L28088】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L28100-L28129】【F:src/botlib/interface/bot_interface.c】【F:tests/parity/test_bot_interface.c】

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
  reachability timeout bookkeeping, including the `sub_10034170` timeout table
  for rocket, grapple, and unsupported travel. It also preserves the retail
  airborne finish branch, continuing `lastreachnum` through travel-specific
  finish handlers instead of repathing while the bot is midair. Unsupported
  successor travel types still use the retail warning path.
- Active walk travel now mirrors Q3's reach-start-first steering, near-start
  switch to the reach end, crouch-only area handling via reconstructed area
  presence settings, and `BotGapDistance` speed scaling. Active crouch travel
  now uses the retail fixed 400 speed, and active barrier-jump travel approaches
  `reach->start` before firing the close-range jump state.
- Active walk-off-ledge travel now follows Gladiator's `sub_100327f0` constants:
  far endpoints move at 400, solvable drops use the reconstructed horizontal
  fall velocity, and near-vertical short edges use the binary's `80 + 2 * dist`
  speed curve. The shared `AAS_HorizontalVelocityForJump` helper now owns the
  Q3 jump/fall formula used by that branch.
- Active ladder, swim, water-jump, and teleport travel now mirror the compact
  Q3/Gladiator helpers: ladder and water-jump use elementary forward/up action
  bits, swim targets `reach->start` at fixed 400 speed, and teleport approaches
  `reach->start` until `MFL_TELEPORTED` is reported.
- Active elevator travel now maps Gladiator's `sub_10033210`, including
  on-mover detection, down/up waiting, bottom-center approach, near-end exit,
  and the airborne `sub_10033790` finish helper. Successor func_bobbing travel
  now uses Q3's packed start/end decoding and dedicated active/finish mover
  paths. Mover lookup accepts both raw reachability model numbers and the
  one-based model indexes produced by entity updates, preserving accidental
  landing diagnostics.
- Active normal jump travel now follows the Q3/Gladiator run-start state
  machine: `AAS_JumpReachRunStart` seeds the run-up point and preserves the
  slime/lava hazard fallback before `bot_move` applies the retail area-gap
  shortening, then the launch branch fires immediate or delayed jump actions
  near `reach->start` while moving toward `reach->end` at speed 600.
- Active rocket jump travel now follows Gladiator's `sub_10033ec0` path:
  approach `reach->start` at `5 * min(dist, 80)`, launch within five units with
  `EA_Jump` + `EA_Attack` at speed 800, set the view directly to pitch 90, and
  select `weapindex_rocketlauncher`. The airborne finish path now follows
  `sub_100340b0` rather than Q3 weapon-jump air control, applying a flat
  endpoint move at speed 800 only after `jumpreach` is set. Type-13
  `TRAVEL_BFGJUMP` remains the original unsupported diagnostic branch.
- Active jump-pad travel now targets `reach->start`, runs the shared blocked
  probe, and moves at fixed speed 400 without setting movement-view flags.
- `BotMoveInDirection` now mirrors Q3's direct swim/walk helper split. The walk
  helper probes barrier jumps with the retail vertical-forward-down trace
  sequence, reads Gladiator's `sv_step`/`sv_maxbarrier` libvars, scans for
  forward gaps with crouch bounds and the water exception, preserves late-airborne
  barrier continuation, predicts direct movement through `AAS_PredictClientMovement`
  before EA submission, and rejects hazardous or blocked predictions before
  emitting EA jump/crouch/grapple input for accepted direct movement.
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
| `"bot_testichat"`, `"bot_testrchat"` | Successor-only diagnostic cvars retained behind extended adapters.| `BotNumInitialChatsWithAliases`, `BotInitialChatWithContext`, `BotReplyChatWithContexts`. | These Q3 diagnostics do not affect the retail `BotInitialChat`, `BotReplyChat`, or `BotEnterChat` paths.【F:dev_tools/Quake-III-Arena-master/code/game/ai_main.c†L1225-L1227】【F:dev_tools/Quake-III-Arena-master/code/game/ai_dmq3.c†L4661-L4676】 |
| `"no rchats\n"` console print | Emitted when no reply chats are present after loading the reply chat tables.| `BotLoadReplyChat`, ultimately used by `BotReplyChat`. | HLIL loader around `sub_1002d6a2`; Quake III prints the same message in `BotLoadReplyChat`.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L35770-L35780】【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_chat.c†L1970-L1980】 |
| `"couldn't find chat %s in %s\n"` | Occurs when a chat block cannot be located inside the requested file.| `BotLoadInitialChat` / `BotLoadChatFile`. | HLIL branch `sub_1002d8a0`; Quake III raises the same fatal in `BotLoadInitialChat`.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L36194-L36214】【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_chat.c†L2013-L2078】 |
| `"couldn't load chat %s from %s\n"` | Final failure path after attempting to load and cache a chat file.| `BotLoadChatFile` export, propagating loader errors up to the engine. | HLIL `sub_1002dff0` reports the error; Quake III signals it from `BotLoadChatFile`.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L36258-L36268】【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_chat.c†L2226-L2239】 |
| `"BotConstructChat: message ..."` family | String assembly helper validates message length, random tables, weighted synonym replacement, and variable expansion.| `BotConstructChatMessage`, used by in-game events such as `BotChat_EnterGame`, `BotChat_Kill`, and other response helpers.| Gladiator HLIL `sub_1002e060` performs one pass over byte-one `r`/`v` references and then unconditionally calls the weighted-synonym helper. Its 0x98 storage uses an asymmetric 0x96 boundary: replacement overflow returns before copy, but literal byte 150 is copied and logged before construction ends. Quake III later added the outer ten-pass expansion loop; that successor behavior is intentionally not used for the Gladiator target.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L36290-L36460】【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_chat.c†L2258-L2399】 |
| `"enter_game"`, `"exit_game"`, `"start_level"`, `"end_level"` | Event helper strings are passed into the initial-chat constructor from Gladiator game-side chat triggers.| `BotNumInitialChats`, `BotInitialChat`, and the raw initial type storage used by event wrappers. | Gladiator uses these pre-Q3 names in HLIL (`sub_10021e90`, `sub_10021f80`, `sub_10022070`), while Quake III probes successor aliases such as `game_exit` and `level_start` through `BotNumInitialChats`.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L27648-L27767】【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_chat.c†L2407-L2566】 |
| Pending chat message buffer | `BotInitialChat` and `BotReplyChat` construct pending text; `BotChatLength` reports it and `BotEnterChat` sends it.| `BotInitialChat`, `BotReplyChat`, `BotChatLength`, `BotEnterChat`. | Empty pending text is a no-op. `sendto == 1` uses `EA_SayTeam(clientto, text)` and every other value uses `EA_Say(clientto, text)`, then clears the buffer. Retail has no tell mode, tilde stripping, owner-client override, or synthetic fallback chat.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L36701-L36921】 |
| Incoming console-message heap and caller | Console messages are global-pool nodes timestamped with `AAS_Time`, linked FIFO per chat state, peeked without removal, and removed by exact node identity. | `BotQueueConsoleMessage`, `BotNextConsoleMessage`, `BotRemoveConsoleMessage`, `BotNumConsoleMessages`, `BotCheckConsoleMessages`. | The 0xa8 x86 node copies exactly 150 bytes and leaves the final two message bytes stale. The consumer performs the retail delay/count gate, normalization, synonym pass, one match attempt, reply gates, exact-node removal, and delayed `BotEnterChat`; copy-pop and type-removal adapters are separately named.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L30820-L30952】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L33326-L33450】 |
| Recent chat-message timers | Initial and reply chat messages carry per-line timestamps; the reply selector preserves retail's raw-node ordinal bug.| `BotChooseInitialChatMessage`, `BotInitialChat`, `BotReplyChat`. | The reply path first counts non-recent responses, then decrements the ordinal before testing each node's timestamp. This can select a recent node and selects the traversal head when all responses are recent. Selected lines are still marked for `CHATMESSAGE_RECENTTIME`.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L36790-L36830】【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_chat.c†L2700-L2764】 |
| Reply priority scan | Reply table entries compare every matching key set and keep the highest numeric priority before constructing a response.| `BotLoadReplyChat`, `BotReplyChat`, `BotChat_ParseReplyKeys`. | Gladiator HLIL `sub_1002e7d0` iterates the global reply list, compares the stored priority against the current best, and only then constructs the selected chat message; Q3's `BotReplyChat` performs the same best-priority scan.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L36703-L36830】【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_chat.c†L2614-L2764】 |
| Reply key-list grammar | Retail keys are quoted strings, parenthesized match pieces, or bare `name` / `female` / `male` / `it`; optional `&` and `!` prefixes preserve their original selection rules. | `BotReplyChat`. | There is no retail `<botname-list>` grammar. The shipped evaluator intentionally never activates bare `name`; gender and parenthesized keys remain active. Rules, keys, and responses traverse in reverse file order.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L35497-L36852】 |
| Plain reply-key matching | Quoted reply keys use raw case-insensitive substring matching.| `StringContains`, `BotReplyChat`. | Retail does not apply word-boundary matching to quoted reply keys. `StringContainsWord` is used by synonym replacement and retains its separate literal-space pointer-skip quirks.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L33458-L33685】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L36701-L36852】 |
| Parenthesized reply keys such as `("i am ", 0)` | Reply keys capture the span matched by integer numeric variables and response messages substitute that span through retail `ESCAPE_CHAR` variable construction, including Q3 string-piece alternatives such as `"hello " | "hi "`.| `BotChat_ParseReplyKeys`, `BotChat_ReplyRuleMatches`, `BotConstructChatMessage`. | Quake III's `BotLoadMatchPieces` rejects non-integer, out-of-range, adjacent, empty, and comma-less match-piece sequences, reads `|`-separated string alternatives into one match piece, and then `StringsMatch` fills `bot_match_t.variables[]`; Gladiator HLIL passes the match table into `BotConstructChatMessage` before dispatch. The outer reply-key list still mirrors Q3's optional commas between sibling keys.【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_chat.c†L1139-L1234】【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_chat.c†L1357-L1490】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L36701-L36968】 |
| Reply construction contexts | Retail `BotReplyChat(state, message)` always constructs with message context 0 and variable context 16.| `BotReplyChat`; `BotReplyChatWithContexts` compatibility adapter. | Q3 split contexts and explicit var0–var7 overlays remain available only through the extended adapter and do not alter the retail entry point.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L36701-L36852】 |
| Match utility exports | Setup-loaded `match.c` templates retain their `MTCONTEXT_*` filter, message subtype, Q3-valid comma-delimited variable pieces, and `|`-separated string alternatives so game code can classify console messages and pull captured variables. | `BotFindMatch`, `BotMatchVariable`, `StringContains`, `UnifyWhiteSpaces`, `BotReplaceSynonyms`. | Q3 exposes these helpers next to the chat exports; Gladiator HLIL carries the same `BotMatchVariable: variable out of range` diagnostic and match-template loader region.【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_chat.c†L440-L464】【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_chat.c†L761-L761】【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_chat.c†L1139-L1234】【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_chat.c†L1434-L1490】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L35081-L35081】 |
| Match template metadata | Numeric `match.c` context labels and `(message type, subtype)` tuple fields are accepted only as integer tokens after preprocessing. | `BotChat_ParseMatchScript`, `BotChat_ParseMatchTemplate`, `BotFindMatch`. | Q3 `BotLoadMatchTemplates` reads the context, message type, and subtype through integer token expectations before registering the template; the reconstruction keeps identifier compatibility for direct, non-preprocessed input but normal script loads consume expanded integer metadata and reject floats rather than truncating them.【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_chat.c†L1258-L1332】 |
| `<"bot", ...>` reply keys, tilde stripping, and tell mode | Successor-only behavior isolated from the retail core.| `BotReplyChatWithContexts`, `BotGetChatMessage`. | Gladiator has no bot-name-list flag, performs no tilde cleanup in `BotEnterChat`, and maps every non-team destination to public say. Extended Q3 helpers remain explicitly named adapters.【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_chat.c†L1828-L1981】 |
| `synfile`, `rndfile`, `matchfile`, `rchatfile` | Setup reads the same libvar-selected asset names as Quake III and skips `rchatfile` when `nochat` is non-zero.| `BotSetupChatAI` / `BotShutdownChatAI`. | HLIL `sub_1002ebb0` loads the three shared assets first and only then gates reply chat loading on `nochat`; `sub_1002ec80` frees the cached lists.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L36922-L36948】【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_chat.c†L2960-L3015】 |
| `j_sub_1002ebb0()` in setup and `j_sub_1002ec80()` in shutdown | Library setup invokes chat setup after weapon/item setup and shutdown tears the shared chat cache down before item/weapon teardown.| `Botlib_SetupAISubsystem`, `Botlib_ShutdownAISubsystem`, `BotSetupChatAI`, `BotShutdownChatAI`. | Gladiator HLIL `sub_10029c90` calls weapon setup, item setup, then chat setup before allocating bot-state storage; `sub_10029da0` invokes the chat shutdown bridge before item and weapon shutdown.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L32727-L32748】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L32754-L32760】 |
| `j_sub_100356d0()` in shutdown | Library shutdown reaches the weapon AI teardown after chat/item cleanup, and the reconstructed interface must clear both active client attachments and exported handle-table slots before shared config memory is released.| `Botlib_ShutdownAISubsystem`, `BotState_ShutdownAll`, `BotShutdownWeaponAI`, `BotAllocWeaponState`, `BotFreeWeaponState`. | Gladiator embeds weapon state inside each bot client, but the Q3-style export surface exposes handle-indexed states; the reconstruction now destroys client states first and drains remaining exported handles during `BotShutdownLibrary` so a later setup cycle does not observe stale client or weapon-state slots.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L32754-L32760】 |
| `rnd.c`, `syn.c`, `match.c`, `rchat.c` setup loads | These four tables are global setup ownership, independent of per-client initial chat files.| `BotSetupChatAI`, `BotShutdownChatAI`, `BotLoadChatFile`. | Setup loads them in synonym → random → match → optional reply order and initializes the console heap last. `BotLoadChatFile` owns only the selected initial-chat tree.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L36922-L36948】 |
| Preprocessed `MSG_*` / match variables | Script identifiers expand before the retail match parser records numeric contexts, types, subtypes, and variable positions.| `PC_ReadToken`, `BotFindMatch`, `BotMatchVariable`. | Commas delimit pieces but contribute no text. Fixed pieces remain verbatim, so captures deliberately retain any spaces not present in those pieces; downstream retail name lookups consume the raw spans without trimming.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L34756-L35081】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L48200-L48234】 |

These correlations keep `src/botlib/ai_chat` aligned with the actual chat event
flow (`BotChat_EnterGame`, `BotChat_Kill`, etc.) observed in both the HLIL dump
and id Software's GPL source.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L35770-L36374】【F:dev_tools/Quake-III-Arena-master/code/game/ai_chat.c†L415-L898】

## Current Chat Reconstruction Notes

- `PS_CreateScriptFromSource` now materializes precompiled source tokens into a
  rewindable script snapshot. This matches the multi-pass loader shape used by
  chat parsing: random strings, synonyms, match templates, reply tables, and
  named initial chat blocks can all scan the same precompiled asset view.
- The precompiler applies ordinary define lookup to every `TT_NAME`, including
  `MSG_*`, context labels, and match placeholders such as `VICTIM`, `KILLER`,
  and `NETNAME`. The chat layer maps the resulting numeric metadata back to
  message and variable roles instead of changing public preprocessing rules.
  Its flattened template form treats numeric variable markers as complete
  match pieces, retaining separator spaces outside captured names and mapping
  the special flag/team slots 7-9 before pattern construction.
- Loading `rchat.c` now pulls sibling retail assets (`rnd.c`, `syn.c`, and
  `match.c`) when present. This reflects the original split between reply
  tables, random-string tables, synonym contexts, and obituary match templates.
- Parsed chat-message random and variable components use Gladiator's internal
  `ESCAPE_CHAR` byte (`0x01`) for construction. Earlier readable reconstruction
  markers are preserved as ordinary literal text.
- Chat construction follows Gladiator's single expansion pass. Weighted output
  synonyms run unconditionally after that pass; the successor's ten-pass outer
  loop is not applied to the retail Gladiator target.
- Named initial-chat blocks are retained in raw type buckets as well as mapped
  event contexts. This recovers the Q3-style `BotNumInitialChats`/`BotInitialChat`
  path and lets Gladiator names (`exit_game`, `start_level`, `end_level`) answer
  successor probes (`game_exit`, `level_start`, `level_end`).
- The bridge export table now exposes `BotNumInitialChats` and `BotInitialChat`
  as appended reconstruction extension slots, leaving the original public ABI
  ordering isolated while making the recovered initial-chat path callable.
- Incoming console messages no longer live in a fixed per-state ring. A shared
  `max_messages` pool supplies AAS-timestamped nodes to state-local FIFO lists;
  raw-node peek/removal exposes the retail identity contract, while the older
  destructive copy-out and type-removal calls remain compatibility wrappers.
- The per-client AI path now consumes that FIFO before weapon and movement
  logic, preserving the strict recent-head/flood boundary, raw self-chat
  removal, synonym contexts, context-7 classification, reply gates, exact-node
  removal, length-derived stand deadline, stationary stand input, and delayed
  public-chat dispatch from Gladiator `sub_10028650`.
- Its recovered match dispatcher covers types 3 through 21. Types 3/4 preserve
  teammate lookup, entity/item fallback, failure chats, LTG deadlines, and
  accompany formation state; type 5 resolves and times defend goals; types 6/7
  enforce the two-flag CTF boundary. Types 8 through 18 retain leadership,
  status, subteam, formation, and dismissal behavior. Types 19 through 21 retain
  camp branches, checkpoint replacement, patrol chains, and partial-failure
  stores.
- Chat construction mirrors Gladiator's single byte-one expansion pass and
  applies the post-expansion weighted synonym pass for every message context.
- `BotInitialChat` now stores the constructed text in the chat state, so
  `BotChatLength`, `BotGetChatMessage`, and `BotEnterChat` expose the same
  pending-message handoff used by the retail game timing and send paths.
- The retail chat test libvars are now wired: `bot_testichat` prints
  `BotNumInitialChats` diagnostics and causes `BotEnterChat` to print pending
  text instead of sending it, while `bot_testrchat` dumps every constructed
  response from the winning reply rule without marking recent timers or
  dispatching through the bridge.
- Chat states now retain the bot's chat name, client, and gender metadata.
  Character/profile loading seeds the reply persona from characteristic 13 while
  client attachment refreshes the owner client, and reply-chat keys such as
  unquoted `name`, `female`, `male`, and `it` use that metadata during matching.
- Reply chat now scans all matching reply rules in retail's setup-owned global
  rchat table and selects the highest priority response, matching the HLIL/Q3
  interpretation of the numeric value after the key list. State-local parsing
  remains available only to direct callers that bypass setup; it cannot affect
  a setup-owned reply selection. The parser also handles Q3 `<"bot", ...>`
  bot-name lists.
- `BotReplyChatWithContexts` now exposes the Q3 split reply-construction path:
  `mcontext` drives weighted output synonyms, `vcontext` drives reply-variable
  synonym canonicalization, and fixed var0-var7 slots cover the game-side
  bot-name/player-name wiring.
- Pending chat text now strips retail `~` markers when callers fetch or send it
  through `BotGetChatMessage` or `BotEnterChat`; reply construction leaves the
  text pending for that same handoff instead of dispatching immediately.
- Initial chat type buckets and reply response lists retain per-message
  recent-use timestamps. Reply selection reproduces the retail two-pass quirk:
  it counts non-recent lines, then decrements the chosen ordinal across raw
  response nodes before checking time. When every line is recent this selects
  the traversal head (the last response in file order) instead of failing.
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
- Reply chat keys are retained instead of skipped. Parenthesized keys with
  numeric captures feed retail byte-one variable references. Readable brace and
  backslash placeholder syntax is treated as literal text by the strict
  Gladiator constructor.
- Match pieces now preserve Q3's `|`-separated string alternatives for both
  setup-loaded `match.c` templates and parenthesized reply keys, so the same
  capture engine handles `"a" | "b"` string pieces before filling variable
  spans. Single empty strings are retained as real string pieces as well, which
  keeps `0, ""` from becoming a trailing catch-all variable.
- `BotLoadChatMessage` and `BotLoadMatchPieces` now follow Q3's delimiter
  contract: message components require commas before the final semicolon, and
  match pieces require commas before `=` or `)`. This is intentionally narrower
  than reply-key list parsing, where Q3 still accepts omitted commas between
  adjacent sibling keys.
- Match-template metadata now follows the exact retail tail grammar: every
  entry requires `(type, subtype);`. Missing subtypes and arbitrary tokens
  between the subtype and semicolon are rejected instead of being skipped.
- Synonym replacement now shares Q3's `StringContainsWord` separator set with
  plain reply-key matching. Space, period, comma, and exclamation mark terminate
  synonym words; punctuation such as `?`, `_`, and `-` remains part of the word
  for both public `BotReplaceSynonyms` and reply-variable `vcontext`
  canonicalization.
