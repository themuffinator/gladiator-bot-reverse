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

## Next measurement improvements

1. Stage the missing mover BSP/AAS assets under `dev_tools/assets/maps/` and
   rerun `ctest --test-dir .audit-full-parity --output-on-failure -R botlib_parity`.
2. Add a per-routine address map for current source functions so exact-name
   overlap can be replaced by address-backed semantic matching.
3. Import or recreate an MSVC6-compatible oracle harness before claiming a
   strict routine byte-match percentage for this repository.
4. Triage the three direct parity failures before using the direct parity pass
   as a green baseline.
