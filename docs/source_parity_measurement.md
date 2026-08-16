# Botlib source parity measurement

## Scope

This pass measures what can be measured from the local repository state:

- current repository commit: `ab0e19099c9d88ab7bd76a3e74ea67da659f0119`
- imported reference: `ref/gladiator-bot-restored`
- imported reference commit: `06423488d24d0d415d80c0758a1473c3b4e06a41`
- HLIL source: `dev_tools/gladiator.dll.bndb_hlil.txt`
- contract catalogue: `tests/reference/botlib_contract.json`

This is not a byte-for-byte MSVC6 oracle run for this repository. The current
tree is split into subsystem modules, contains host-safe/native structures, and
does not carry the per-routine object comparison harness needed to assign a
strict byte-identical routine count.

## Results

| Measurement | Result | Meaning |
| --- | ---: | --- |
| Imported reference byte-oracle claim | 591 / 787 routines, 75.1% | Upstream `gladiator-bot-restored` README claim for its own monolithic reconstruction, not this repository. |
| Current export contract coverage | 20 / 20 exports, 100% | `tests/reference/botlib_contract.json` covers all retail export wrappers, plus one bridge diagnostic record. |
| Current helper contract coverage | 5 helpers, 16 libvar defaults | Static contract entries available for tests and documentation. |
| Current AI matrix coverage | 70 implemented / 71 tracked rows, 98.6% | Parsed from `docs/be_ai_parity_matrix.md`; the remaining tracked divergence is `BotConstructChatMessage` unsafe retail malformed-input behavior. |
| Direct parity executable pass rate | 199 passed / 202 non-skipped tests, 98.5% | Direct run of the six built parity executables from `.audit-full-parity`. |
| Direct parity executable total coverage | 199 passed / 208 listed tests, 95.7% | Counts 6 asset-gated skips as uncovered. |
| Reference-to-current exact function-name overlap | 129 / 442 unique parsed reference names, 29.2% | Static name overlap only. This undercounts semantic coverage because this repository uses normalized module-local names. |

## Direct parity run

CTest could not run the full `botlib_parity` suite because the parity asset
fixture gate failed:

- missing `dev_tools/assets/maps/test_mover.bsp`
- missing `dev_tools/assets/maps/test_mover.aas`

The built parity executables were run directly to capture asset-independent
coverage:

| Executable | Passed | Failed | Skipped |
| --- | ---: | ---: | ---: |
| `botlib_parity_bot_interface.exe` | 118 | 1 | 6 |
| `botlib_parity_bot_move.exe` | 5 | 2 | 0 |
| `botlib_parity_precompiler_lexer.exe` | 13 | 0 | 0 |
| `botlib_parity_aas_debug.exe` | 37 | 0 | 0 |
| `botlib_parity_bridge.exe` | 10 | 0 | 0 |
| `botlib_parity_update_translator.exe` | 16 | 0 | 0 |
| **Total** | **199** | **3** | **6** |

Failing direct tests:

1. `test_bot_console_camp_preserves_retail_goal_branches_and_deadlines`
   - Fails at `tests/parity/test_bot_interface.c:4328`.
   - Residual area: camp-here console command goal ownership when the teammate
     position is valid.

2. `test_bot_move_relinks_func_plat_logs_message`
   - Fails at `tests/parity/test_bot_move.c:345`.
   - Residual area: func_plat relink diagnostic bit and retail developer log.

3. `test_bot_move_func_bobbing_without_reachability_logs_warning`
   - Fails at `tests/parity/test_bot_move.c:401`.
   - Residual area: func_bobbing missing-reachability failure and diagnostic.

Skipped direct tests are all asset-gated BSP/AAS or end-to-end map cases.

## Source comparison against imported reference

Lightweight C function extraction produced these static counts:

| Corpus | Parsed definitions | Unique names |
| --- | ---: | ---: |
| `ref/gladiator-bot-restored/botlib` | 453 | 442 |
| `src/botlib` + `src/q2bridge` | 1825 | 1824 |
| Exact-name overlap | - | 129 |

The exact-name overlap is not a semantic parity score. It is useful as a
source-shape signal: the imported reference is monolithic and address-shaped,
while this repository has many split wrappers and compatibility names. The
reference remains valuable for routine-by-routine audits, especially where the
current tree carries normalized helper names.

## Current parity estimate

Best defensible numbers after this pass:

- Strict byte-identical current-repo parity: not measured.
- External byte-oracle benchmark from imported reference: 75.1%.
- Current catalogued behavioral parity: about 98.5% on the runnable direct
  parity subset.
- Current whole-DLL behavioral/source-intent estimate: roughly 80-85%, with
  lower confidence than the scoped test numbers because several DLL regions are
  not measured by a full per-routine oracle.

No source reconstruction was promoted in this pass, so implementation parity is
unchanged. The measurement improved confidence and identified three immediate
residual parity failures plus missing asset prerequisites for full CTest.

## Scoped pass: bot weapon and combat

This subsystem was audited routine-by-routine against the imported reference
reconstruction and the HLIL. Every routine below was read side by side with its
retail counterpart; the result column records whether the reconstruction's
observable behaviour matches for all inputs the retail routine can receive.

| Retail routine | Reconstruction | Result |
| --- | --- | --- |
| `LoadWeaponConfig` / `sub_10034bb0` | `AI_LoadWeaponLibrary` | match |
| `WeaponWeightIndex` / `sub_10035280` | `AI_WeaponWeightsBindConfig` | match |
| `BotFreeWeaponWeights` / `sub_10035300` | `BotWeapon_ClearWeights` | match |
| `BotLoadWeaponWeights` / `sub_10035340` | `BotWeapon_LoadWeightsInternal` | fixed: missing fatal on the cached path |
| `sub_100353c0` | `AI_WeaponNumberForModel` | match |
| `sub_10035430` | `AI_WeaponNameForModel` | match |
| `sub_100354b0` | `BotCurrentWeaponInfo` | fixed: removed an upper bound retail omits |
| `BotChooseBestFightWeapon` / `sub_10035500` | `BotSelectBestFightWeapon` | match |
| `BotResetWeaponState` / `sub_10035640` | `BotResetWeaponState` | match |
| `BotSetupWeaponAI` / `sub_10035680` | `BotSetupWeaponAI` | match |
| `BotShutdownWeaponAI` / `sub_100356d0` | `BotShutdownWeaponAI` | match |
| `sub_10020fe0` | `BotWeaponStateSyncFrame` | match |
| `BotGetWeaponInfo` | `BotGetWeaponInfo` | fixed: silent skip without a config; handle-band diagnostic |
| `BotAimAtEnemy` | `AI_DMAimAtEnemy` | fixed: lead derived from the wrong velocity source |
| `BotCheckAttack` / `sub_10024590` | `AI_DMCheckAttack` | match |
| `BotAttackMove` | `AI_DMAttackMove` | match |
| `BotUpdateInventory` / `sub_10021020` | `BotAI_UpdateBattleInventory` | match |
| `BotUpdateBattleInventory` / `sub_10021290` | `BotAI_UpdateEnemyBattleInventory` | match |
| `BotBattleUseItems` / `sub_10021500` | `BotAI_UseItems` | match |
| `sub_100215e0` | `BotAI_BattleUseItems` | match |
| `BotAggression` | `BotAI_Aggression` | match |
| `BotWantsToRetreat` / `sub_100228c0` | `BotAI_WantsToRetreat` | match |
| `BotWantsToChase` / `sub_10022930` | `BotAI_WantsToChase` | match |
| `BotCanAndWantsToRocketJump` / `sub_10022990` | `BotAI_CanAndWantsToRocketJump` | match |
| `AINode_Battle_Fight` / `sub_1001fd30` | Battle Fight node step and driver | fixed: enemy-location commit ordered after the inventory projection |
| `BotChangeViewAngles` / `sub_10029150` | `AI_DMChangeViewAngles` | match |

Twenty-six routines audited, six defects across five routines, all fixed and
covered by regression tests. No intentional behavioural difference against the
retail reference remains in this subsystem. The guards that survive in the
combat path either match retail's own guards or are unreachable given the
library's initialisation invariants, and the battle travel mask is pinned equal
to retail's literals by compile-time assertion over the fourteen travel types
Gladiator defines.

This is source-level correspondence against the authoritative reference, which
is the same standard every other row in `docs/be_ai_parity_matrix.md` is held
to; it is not a byte-identity measurement. A byte oracle for these routines is
blocked on the repository-wide prerequisite in item 3 below and is not specific
to weapon or combat: this tree is a modular reconstruction that compiles for
64-bit hosts, so per-routine object comparison needs an MSVC6-compatible
monolithic build before any byte-match percentage can be claimed for any
subsystem.

## Defects found outside weapon and combat

Closing the weapon and combat pass surfaced five defects in adjacent code. All
are fixed except where noted.

1. `BotState_Get` returns a stable fixed-stride slot for any in-range client
   while the library is up, because retail keeps its client records in one slab
   allocated at library setup. Four assertions in `tests/ai/test_ai_character.c`
   still tested for a null slot after shutdown, which was the pre-slab
   per-client-allocation contract. They now assert a retained slot whose record
   is cleared.
2. Presentation settings live in a table separate from the client record and
   outlive a client shutdown that clears the record. `BotSetupClient` did not
   re-seed the record's mirror from that table, so a client set up again
   reported a cleared name and skin.
3. `BotState_ResetCombat` was defined but never called, so the combat block's
   reconstruction timestamps stayed at zero instead of their `-FLT_MAX` "never
   happened" value. A freshly set-up client therefore read as having sighted an
   enemy, killed one, and taken damage at time zero. Seeding now happens per
   client in `BotSetupClient`; the record slab itself stays zero-cleared,
   matching retail and the capacity test that pins it.
4. Chat cooldowns sampled a wall clock rather than the frame clock the host
   advances through `BotStartFrame`, so identical frame sequences could take
   different chat paths depending on how long the host took between frames.
   Every other timed decision in that module already used `AAS_Time()`.
5. Not fixed: `BotFindMatch` trims both separators from a capture, while retail
   measures a variable from its start to where the following literal was found.
   The shipped obituary context resolves to the unspaced alternates in
   `bots/match.c`, so a raw span keeps the separator on both sides. Two
   expectations in `tests/parity/test_bot_interface.c` want the victim's
   trailing space preserved. Honouring them requires the name-resolving
   consumers to trim instead, so the span semantics and those consumers need to
   change together rather than one at a time.

## Next measurement improvements

1. Stage the missing mover BSP/AAS assets under `dev_tools/assets/maps/` and
   rerun `ctest --test-dir .audit-full-parity --output-on-failure -R botlib_parity`.
   Still outstanding. The `parity_asset_verification` fixture is a hard
   `FATAL_ERROR`, and every parity, AI, and bspc test declares it as a required
   fixture, so its two missing files stop nine suites from running under CTest
   even though the cases themselves skip cleanly. Until the assets are staged,
   measure those suites by running their executables directly from
   `dev_tools/assets/`.
2. ~~Add a per-routine address map for current source functions so exact-name
   overlap can be replaced by address-backed semantic matching.~~ Done:
   [retail_function_map.md](retail_function_map.md) indexes all 816 retail
   botlib routines by DLL address, HLIL line span, retail name, original
   translation unit, and every citation of that address in `src/`.
3. Import or recreate an MSVC6-compatible oracle harness before claiming a
   strict routine byte-match percentage for this repository.
4. ~~Triage the three direct parity failures before using the direct parity
   pass as a green baseline.~~ Done, together with three more that had appeared
   since: see "Full translation-unit audit" below.

## Full translation-unit audit

A routine-by-routine pass compared **756 retail routines** across all 33
original translation units against `dev_tools/gladiator.dll.bndb_hlil.txt`,
cross-checked against `ref/gladiator-bot-restored/botlib/`. Each candidate
divergence was then re-checked by an independent pass instructed to refute it
against the HLIL; 28 of 109 candidates did not survive that check.

The 80 that did are catalogued with evidence and a fix in
[retail_divergence_backlog.md](retail_divergence_backlog.md). Thirteen are
fixed in the tree; one was implemented and backed out because it moves test
outcomes that need separate triage; the rest are open.

The audit also found that ten of the twelve routines in the original
`be_aas_debug.c` had no counterpart at all — `AAS_ShowArea`, `AAS_ShowFace`,
`AAS_DrawCross`, `AAS_DrawArrow`, `AAS_DrawPermanentCross`,
`AAS_DrawPlaneCross`, `AAS_ShowBoundingBox`, `AAS_PrintTravelType`,
`AAS_ShowReachability` and `AAS_ShowReachableAreas`. All ten are now
reconstructed in `src/botlib/aas/aas_debug.c`.

### Baseline repairs

| Failure | Resolution |
| --- | --- |
| `bot_common` / `bot_common_crc` aborted on `CRC_SourceChecksumCount() == 3` | The catalogue scan at `0x100377e4` discards its result, so registration is unconditional; the test pinned a suppression that does not exist. Registration also now uses `GetClearedMemory`, `_stricmp` ordering and dedup, and the record's real 146-byte name field. |
| `aas_map` failed `test_reachability_jump_generation_and_rejections` | The fixture supplied no AAS node tree, so `AAS_TraceClientBBox` never landed the probe jump. It now models both ledges and the gap, and the case exercises the real generator. |
| `ai_dm` never ran | `ai_dm_tests` did not link `botlib_common`, so it failed on undefined `Vector2Angles`. |
| `botlib_parity_bot_interface` failed `test_ai_seek_ltg_nearby_goal_schedule` | Both selectors arm a 30-second avoid slot for an item whose `respawntime` is zero, so at `t+20` the item is still avoided and retail falls through to its roam goal. The test expected reselection. |
| MSVC builds failed outright | `_Static_assert` in `ai_character.c` and `bot_move.h` is rejected by the default MSVC C dialect. Build with clang + Ninja, as `.audit-full-parity` does. |
