# Botlib Parity Testing Guide

This guide outlines how to configure a development environment for the botlib parity suite, run the available checks, and keep the HLIL-derived expectations catalogue aligned with new reverse-engineering findings. The parity plan captures the behaviours described in the existing parity README and placeholder cmocka harness.

## 1. Environment configuration

1. **Install required toolchain**
   - CMake 3.16 or newer and a C11/C++17-compatible compiler (the top-level build requires both languages). 【F:CMakeLists.txt†L1-L15】
   - Internet access for the first configuration so CMake's `FetchContent` block can clone the cmocka test dependency when the parity suite is enabled. 【F:tests/CMakeLists.txt†L1-L23】
   - Platform-specific prerequisites that mirror CI:
     - **Linux** – Install `ninja-build`, a GCC or Clang toolchain, and development headers for pthreads/zlib through your distribution packages (e.g., `sudo apt-get install build-essential ninja-build`). The parity workflow runs against `ubuntu-latest` with these packages available.
     - **Windows** – Install Visual Studio 2022 Build Tools (C++ workload) and Ninja. Launch the "x64 Native Tools Command Prompt" before configuring so MSVC is on `PATH`, or use `vcvarsall.bat`/`ilammy/msvc-dev-cmd` as in CI. CMake will use MSVC with the Ninja generator once this environment is active.
   - The cmocka harness is fetched automatically into `build/_deps/` on the first configure; delete the folder to force a refresh if the checkout becomes corrupted. 【F:tests/CMakeLists.txt†L10-L23】

2. **Configure the build tree**
   ```bash
   cmake -S . -B build -DBUILD_TESTING=ON
   ```
   - `BUILD_TESTING=ON` activates the `tests/` subtree. 【F:CMakeLists.txt†L17-L25】
   - Parity sources are now enabled by default; pass `-DBOTLIB_PARITY_ENABLE_SOURCES=OFF` if you temporarily need the lightweight placeholder target instead of the cmocka fixtures. 【F:tests/parity/CMakeLists.txt†L1-L20】

3. **Reconfigure when dependencies change**
   - If the FetchContent checkout becomes corrupted or cmocka fails to update, remove `build/_deps/cmocka-*` and rerun the configure command above. 【F:tests/CMakeLists.txt†L10-L23】
   - Changing compiler flags or toggling the parity option requires re-running the same configure command so CMake regenerates the build files.

## 2. Running the parity suite

1. **Build the tests**
   ```bash
   cmake --build build --target botlib_parity_tests
   ```
   The parity executable links against cmocka and pulls headers from `src/` so wrapper and interface updates are available to the harness. 【F:tests/parity/CMakeLists.txt†L7-L17】

2. **Execute the suite**
   ```bash
   ctest --test-dir build --output-on-failure -R botlib_parity
   ```
   - Use `-V` for verbose cmocka output when diagnosing guard or logging mismatches.
   - Individual cmocka tests map directly to the scenarios catalogued in `tests/parity/README.md`, such as import-table discipline, lifecycle parity, and diagnostic parity checks. 【F:tests/parity/README.md†L1-L64】
   - The translator fixtures introduced in `test_update_translator.c` rely on the bridge helpers `TranslateEntity_SetWorldLoaded`
     and `TranslateEntity_SetCurrentTime` to emulate the AAS runtime. Ensure `tests/reference/botlib_contract.json` ships with
     the `BridgeDiagnostics` catalogue entry so mocked `Print` captures can be compared against the HLIL-derived strings. 【F:tests/parity/test_update_translator.c†L1-L324】【F:tests/reference/botlib_contract.json†L1-L230】

3. **Interpreting results**
   - **Pass** – All expectations drawn from the HLIL trace matched (import-call order, guard behaviour, diagnostic strings). A clean run will show `100% tests passed` in CTest.
   - **Skip** – Remaining `cmocka_skip()` paths in `test_bot_interface.c` are fixture/setup gates for unavailable BSP/AAS assets. Binary-independent interface contracts continue to run; stage the documented map fixtures to enable the gated cases. 【F:tests/parity/test_bot_interface.c†L1-L68】
   - **Fail** – Review the cmocka failure summary to identify which expectation broke. Most assertions will surface mismatched bridge callbacks, incorrect guard return codes, or missing diagnostics described in the parity README.
   - **Import parity deviations** – The `test_import_table_matches_retail_symbol_list` check compares the current `bot_import_t` layout against the retail import names lifted from `dev_tools/game_source/botlib.h`. When it fails, the error message will cite the unexpected slot count or the first field whose name/offset diverges so you can reconcile the struct with the retail reference before rerunning CTest. 【F:tests/parity/test_bot_interface.c†L69-L136】【F:dev_tools/game_source/botlib.h†L230-L260】

4. **Run the self-contained common-library checks**
   ```bash
   ctest --test-dir build -C Release -R "^bot_common_(memory|crc|libvar|log|utils|struct)$" --output-on-failure
   ctest --test-dir build -C Debug -R "^bot_common_(memory|crc|libvar|log|utils|struct)$" --output-on-failure
   ```
   These focused tests do not need packaged game assets. They pin allocator and
   libvar bookkeeping, the signed-length CRC contract, raw CRLF log bytes and
   persistent timestamp counters, whole-degree/vector-angle quantization, and
   the retail nested-structure and fixed-array parser/writer quirks.【F:tests/common/CMakeLists.txt】【F:tests/common/test_bot_common.c】【F:tests/common/test_libvar.c】

## 3. Updating the expectations catalogue

The expectations catalogue is split between documentation and executable checks:

- `tests/parity/README.md` narrates the HLIL-derived scenarios for each exported botlib entry point.
- `tests/parity/test_bot_interface.c` encodes those expectations as cmocka fixtures.

When HLIL review reveals new behaviour:

1. Capture the evidence: annotate the relevant snippet (e.g., offsets and strings) from `dev_tools/gladiator.dll.bndb_hlil.txt` in your working notes so the provenance is clear. 【F:dev_tools/gladiator.dll.bndb_hlil.txt†L33055-L51227】
2. Update the README:
   - Add or revise bullets under the affected entry point so the catalogue reflects the new guard, logging, or call-order insight. 【F:tests/parity/README.md†L14-L63】
   - Cross-reference any new helper utilities or harness hooks that the test will need.
3. Align the cmocka suite:
   - Introduce new assertions or helper structures in `test_bot_interface.c` that mirror the updated expectations. Keep the test names and structure in sync with the README to preserve traceability. 【F:tests/parity/test_bot_interface.c†L10-L68】
   - Commit both changes together so reviewers can confirm the documentation and automated checks agree.
4. Regenerate build files if new source helpers or headers were added so the parity target includes them (`cmake -S . -B build ...`).

## 4. Troubleshooting checklist

- **cmocka fetch issues** – Delete the cached `_deps/cmocka-*` directories inside the build tree and rerun CMake to re-fetch the dependency. 【F:tests/CMakeLists.txt†L10-L23】
- **Stale mock import tables** – After modifying the recording doubles or helper APIs described in `tests/README.md`, clean the parity target (`cmake --build build --target clean` or delete the corresponding `CMakeFiles` directory) before rebuilding so the updated structures are linked. 【F:tests/README.md†L5-L136】【F:tests/parity/test_bot_interface.c†L14-L25】
- **Mismatched expectations after HLIL updates** – Re-run the catalogue update process above and ensure both the README and test fixtures reflect the latest findings before rerunning `ctest`.
- **CTest finds no tests** – Confirm that `BUILD_TESTING=ON` was supplied and that `BOTLIB_PARITY_ENABLE_SOURCES` was not explicitly forced to `OFF`; otherwise the parity target falls back to the placeholder custom target and no executable is built. 【F:tests/parity/CMakeLists.txt†L1-L20】

Keeping this workflow up to date ensures contributors can quickly validate reconstructed functionality against the original Gladiator botlib contract while iterating on new discoveries from the HLIL traces.

## 5. Weapon configuration parity

`BotSetupLibrary` now caches the `weaponconfig`, `max_weaponinfo`, and `max_projectileinfo` libvars before attempting to load the global weapon library. The cache is populated through the bridge helpers so tests can override the libvar values without rebuilding the AI loader, and a missing or malformed weapon configuration will cause setup to fail with `BLERR_CANNOTLOADWEAPONCONFIG`. Parity fixtures that stub `BotLibVarGet` should therefore provide consistent responses for these names when exercising the startup path.【F:src/botlib/interface/botlib_interface.c†L36-L124】【F:src/q2bridge/bridge_config.c†L1-L214】

## 6. Character and client-resource parity

- Layout fixtures should inspect the returned `bot_character_t *` itself. On
  x86 assert a four-byte highest-index header, eight-byte slots beginning at
  offset four, and trailing strings; on x64 assert the native offset-eight,
  16-byte-slot adaptation. Bot-memory accounting must see one definition block,
  because internal count/string-size/skill/name metadata is held in a
  host-`calloc` sidecar. Direct free must remove both pieces. A deliberate
  second-open/pass-two failure should leave the retail packed block abandoned
  until `AI_ShutdownCharacterDefinitions` drains the definition/sidecar list
  before arena teardown.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L32810-L33192】【F:src/botlib/ai/ai_character.c】【F:src/botlib/interface/botlib_interface.c】【F:tests/ai/test_ai_character.c】
- Direct-string tests should prove `Characteristic_String` is a mutable raw
  pointer into trailing packed storage, while the Q3 handle facade copies into
  a caller buffer. Float-to-integer fixtures must cover signed-64 truncation and
  low-dword return: `2147483648.0f` becomes `INT_MIN`, `4294967296.0f` wraps to
  zero, negative low-dword wrap is preserved, and x87-invalid/out-of-64-bit
  values return zero. Successful loose loads must log the requested name plus
  logical filename without the resolved physical path; non-empty PAK loads must
  log `<pak-path>\<logical-entry>`.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L33197-L33312】【F:src/botlib/ai/ai_character.c】【F:src/botlib/ai_character/bot_character.c】【F:tests/ai/test_ai_character.c】
- Fixed-client-table fixtures should set a small `maxclients` and verify setup
  creates two cleared payloads of exactly `maxclients * 0x11d0` and
  `maxclients * 0x90`. Adjacent real state/settings addresses must differ by
  `0x11d0`/`0x90`, stay stable across create/destroy reuse, and begin cleared.
  The inclusive `client == maxclients` endpoint must resolve to separate host
  sentinels, not to either base plus its allocation size; `client > maxclients`
  must fail. Move a populated record and assert a full-record destination copy
  plus a fully zeroed `0x11d0` source, while the separate presentation-settings
  records stay indexed by their host slots. On shutdown, instrument the
  allocator if needed to pin settings-base free before state-base free, then
  assert capacity returns to zero. On x64, retain these
  stride/allocation/clear assertions and the
  `sizeof(bot_client_state_t) <= 0x11d0` boundary instead of asserting raw x86
  pointer offsets inside the native adapter.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L32577-L32595】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L32727-L32760】【F:src/botlib/interface/bot_state.c】【F:src/botlib/interface/bot_state.h】【F:src/botlib/interface/botlib_interface.c】【F:tests/parity/test_bot_interface.c】
- Setup-failure fixtures must not expect central cleanup or an entry reset.
  Missing/empty item and weapon characteristics are passed as raw empty paths
  and fail without item/weapon defaults. The inactive partial state and owner
  handles remain: item failure retains the goal handle; weapon failure retains
  goal and weapon handles after the branch-specific item free; chat failure
  retains the chat object and both handles after its item/weapon frees. Repeated
  attempts reuse those owners, and a later valid setup can succeed. Set
  `nochat=1` separately and confirm that per-client `BotLoadChatFile` still runs;
  `nochat` gates later chat construction, not client resource setup.
  【F:dev_tools/gladiator.dll.bndb_hlil.txt†L32494-L32540】【F:src/botlib/interface/bot_interface.c】【F:tests/ai/test_ai_character.c】
- Shutdown fixtures should observe `exit_game` construction/dispatch before
  resource destruction, followed by chat → weapon/weights → goal/item → packed
  character → console waypoints → successor goal/move/DM. A whole-record move changes the table slot
  but leaves `state->client_number` and the chat binding untouched, so shutdown
  of the destination slot must still emit to the retained original client.
  【F:dev_tools/gladiator.dll.bndb_hlil.txt†L32548-L32595】【F:src/botlib/interface/bot_interface.c】【F:src/botlib/interface/bot_state.c】【F:tests/ai/test_ai_character.c】

## 7. Weight configuration parity

Handle-based helpers now wrap the weight parser so callers mirror Gladiator’s lifecycle:

- `BotAllocWeightConfig` returns a 1-based identifier backed by the botlib heap; the guard path emits `"BotAllocWeightConfig: no free handles"` when the small table is exhausted so parity tests can assert the legacy failure mode.【F:src/botlib/ai_weight/bot_weight.c†L37-L83】
- `ReadWeightConfig` cache tests should prove retained configs are returned before filesystem lookup. Load a temporary filename with `bot_reloadcharacters` off, delete the backing file, then load the same caller filename again and assert pointer identity; Q3 compares against `config->filename` before `LoadSourceFile`. Keep that assertion on the low-level and explicitly cached weapon paths. Test the Gladiator client path separately: `BotSetupClient` directly invokes the canonical goal-state adapter/core, whose `BotLoadItemWeights` uses `ReadWeightConfigUncached`; do not infer setup caching from the extended export's historical name or its current alias to that adapter. Weapon weights use the fresh weapon-state path, and two clients using the same character must receive distinct packed definitions, item configs, weapon configs, and chat states. The character definition itself owns none of those resources.【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_weight.c†L292-L433】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L32494-L32570】【F:src/botlib/ai_goal/bot_goal.c】【F:src/botlib/ai_weapon/bot_weapon.c】【F:src/botlib/interface/bot_interface.c】【F:tests/ai/test_ai_weight.c】【F:tests/ai/test_ai_weapon.c】【F:tests/ai/test_ai_character.c】
- `FreeWeightConfig` should be tested with a direct non-cacheable load while `bot_reloadcharacters` is false. Q3 returns before freeing any config in that state, so assert the memory total is unchanged after `FreeWeightConfig`, then flip the flag and force the same pointer through the normal free path.【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_weight.c†L139-L146】【F:tests/ai/test_ai_weight.c】
- `BotLoadWeights` validates both the handle and filename before dispatching to `ReadWeightConfig`. Parser failures continue to raise the historical `"couldn't load weights\n"` diagnostic captured in the HLIL trace, while lower-level file failures preserve Gladiator's `"couldn't find %s\n"` and `"counldn't load %s\n"` strings from the raw loader.【F:src/botlib/ai_weight/bot_weight.c†L95-L123】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L42072-L42080】【F:docs/weight_config_analysis.md†L6-L18】
- `ReadWeightConfig` should pin the retail non-fatal parser edges: a `switch` without `default` appends a zero-valued separator at the max inventory sentinel, and the `"too many fuzzy weights"` path keeps the first 128 weights before stopping.【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_weight.c†L153-L209】【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_weight.c†L332-L340】【F:tests/ai/test_ai_weight.c】
- `BotReadWeightsFile` should be covered beside the handle loader because it exposes the same parser/cache path directly through the q2bridge table. Keep `bot_reloadcharacters` enabled for direct-read fixture tests when ownership needs to stay local to the test.【F:src/botlib/ai_weight/bot_weight.c】【F:src/q2bridge/botlib.h】
- `BotSetWeight` leverages the exported `BotWeight_FindIndex` helper so tests can probe guard clauses (missing handle/config, unknown names) without poking the private tree representation, then confirm the assigned weight evaluates through `BotFuzzyWeightHandle`.【F:src/botlib/ai_weight/bot_weight.c】【F:src/botlib/ai_weight/bot_weight.h†L53-L67】
- `BotFindFuzzyWeight` / `BotFuzzyWeightHandle` expose read-only queries through both the local handle API and q2bridge export table, falling back to sentinel values when the handle is invalid. This mirrors the Quake III access pattern while keeping the tree management internal.【F:src/botlib/ai_weight/bot_weight.c】【F:src/botlib/interface/bot_interface.c】【F:src/q2bridge/botlib.h】
- `FuzzyWeightUndecided` needs direct range assertions because its recursive path samples balance leaves while its exhausted-final-separator path returns the stored weight unchanged. Include a raw-random regression for Q3's `(rand() & 0x7fff) / 0x7fff` scale; that helper is shared with mutation and is easy to accidentally rewrite as a different C runtime normalization.【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_weight.c†L610-L637】【F:dev_tools/Quake-III-Arena-master/code/game/q_shared.h†L750-L751】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L42649-L42660】【F:tests/ai/test_ai_weight.c】
- `MergeWeightConfigs` covers the Gladiator-only two-config merge path. Regression fixtures should use simple `balance(...)` leaves when asserting averages and include a nested `switch` branch, because retail child recursion self-crosses the second config and the sibling walk stops when the second config has a `next` separator.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L42884-L42938】【F:src/botlib/ai_weight/ai_weight.c】
- `InterbreedWeightConfigs` should be tested with both top-level and nested balance leaves. The top-level child output averages both parents, while the nested branch follows the Q3 child self-cross and inherits the second parent value.【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_weight.c†L823-L876】【F:tests/ai/test_ai_weight.c】
- `EvolveWeightConfig` should assert that mutation changes remain inside the original balance bounds and uses the same masked `random()` scale as undecided sampling. Gladiator clamps out-of-range mutations, while the Quake III reference expands the bounds around the new weight.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L42721-L42775】【F:dev_tools/Quake-III-Arena-master/code/game/q_shared.h†L750-L751】【F:tests/ai/test_ai_weight.c】
- `ScaleWeight` and `ScaleBalanceRange` deserve direct struct-level coverage because they are low-level source-parity helpers with little public traffic. Assert name misses are no-ops, scale inputs clamp to the retail ranges, and balance leaves remain bounded after scaling.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L42797-L42878】【F:tests/ai/test_ai_weight.c】
- Goal fuzzy wiring should stay thin: LTG/NBG item scoring consumes `FuzzyWeightUndecided` under Q3's `UNDECIDEDFUZZY` path, `BotInterbreedGoalFuzzyLogic` writes into the child state's existing config, and `BotMutateGoalFuzzyLogic` ignores the `range` argument before calling `EvolveWeightConfig`. Keep exported goal tests around the full register-load-score path so the weight module and goal index table stay wired together.【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_goal.c†L222-L260】【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_goal.c†L1349-L1353】【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_goal.c†L1520-L1524】【F:src/botlib/ai_goal/bot_goal.c】【F:tests/ai/test_ai_goal_move.c】
- Goal setup has a distinct `itemconfig` failure code: missing shared item definitions should return `BLERR_CANNOTLOADITEMCONFIG` from setup and `BLERR_CANNOTLOADITEMWEIGHTS` from later per-state item-weight loads, matching Gladiator's `sub_100309d0`/`sub_100308d0` split.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L38348-L38415】【F:src/botlib/ai_goal/bot_goal.c】【F:tests/ai/test_ai_goal_move.c】
- Goal avoid fixtures should pin the Gladiator fixed-slot table separately from Q3's successor capacity. HLIL clears 64 goal-number slots at `0x1cc` and 64 expiry slots at `0x2cc`; insertion scans for the first expired time slot and does not compact, refresh active duplicates, or evict an active entry when all slots are live. Tests should set the botlib goal time above zero before adding avoids so the retail strict-expired comparison has reusable reset slots.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L37436-L37519】【F:src/botlib/ai_goal/bot_goal.c】【F:tests/ai/test_ai_goal_move.c】
- Raw map-item selection fixtures should mark a selected item as taken through the bridge compatibility helper and confirm that the raw LTG/NBG list still selects it once its avoid entry is cleared: Gladiator has no `next_respawn_time` field or corresponding selector gate. That gate belongs only to direct registrations.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L37857-L38233】【F:src/botlib/ai_goal/bot_goal.c】【F:tests/ai/test_ai_goal_move.c】
- Raw selector overflow fixtures should fill the seven usable retail stack slots, then assert that LTG/NBG still return success and arm the chosen item's avoid timeout before `BotPushGoal` logs overflow and preserves the pre-existing stack top.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L37776-L37802】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L37857-L38233】【F:src/botlib/ai_goal/bot_goal.c】【F:tests/ai/test_ai_goal_move.c】
- Goal item fixtures should include model-index wiring and BSP entity filtering, not only classnames. `items.c` supplies model paths, `BotLoadMap` supplies the Quake II model table, `BotInitLevelItems` applies the retail `notspawnflags` gate while reading little-endian BSP entity lumps on Windows, and every known BSP item—including `spawnflags & 1`—should trace down to the floor before its goal origin is registered. `BotUpdateEntityItems` should link settled live entities to nearby static level items or create dropped goals with the retail 30-second timeout, retain dropped items whose goal area is a jump-pad area, and leave an old static link intact if the live entity subsequently changes model. Level-item lookup fixtures should assert the retail cursor contract: `BotGetLevelItemGoal` returns the first matching goal with `goal.number > index`, returns `-1` when exhausted, and writes the raw goal prefix while preserving the caller's `flags` and `iteminfo` tail. Selection fixtures should cover an unlinked raw map item (`entitynum == 0`), because retail selection still admits it; the unlinked-static/roam guard belongs only to direct compatibility registrations. Timed raw candidates receive a fixed `+20` score bonus after travel scaling, but their selected goal remains `GFL_ITEM` and uses the item respawn/default-30 avoid duration. They should also distinguish current-area point resolution from reachability acceptance: a missing or unreachable start area must reject selection even after a prior valid selection; Gladiator has no Q3 last-area fallback. NBG fixtures should assert the strict `travel_time < maxtime` check, including the zero/equal-time rejection path and the raw LTG return-route comparison, and should rely on avoid timers rather than adding a same-number LTG filter. Public avoid fixtures should cover negative `BotSetAvoidGoalTime` on dropped goals, because that API derives respawn/default/minimum time rather than the selected-dropped-item timeout. BotAI-path fixtures should also cover the one-second `sub_100292e0` refresh gate after client think: the current think sees the previous global item state, the refresh becomes available to later ticks, and goal queries never refresh independently.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L32442-L32476】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L37140-L37409】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L37420-L38300】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L37470-L37550】【F:src/botlib/ai_goal/bot_goal.c】【F:tests/ai/test_ai_goal_move.c】
- Goal info-entity fixtures should include multiple `target_location` and `info_camp` records in one BSP entity lump. Q3 pushes each parsed record to the head of a linked list, so duplicate map-location names should resolve to the last parsed entity and camp cursor iteration should run in reverse entity-lump order.【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_goal.c†L491-L520】【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_goal.c†L912-L958】【F:src/botlib/ai_goal/bot_goal.c】【F:tests/ai/test_ai_goal_move.c】
- Goal-touching fixtures should use points outside the raw item bounds but inside the goal bounds expanded by `AAS_PresenceTypeBoundingBox(PRESENCE_NORMAL)`. Gladiator `sub_10030600` and Q3 both test player-bbox overlap, so point-containment-only tests miss the exported helper's real contract.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L38234-L38292】【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_goal.c†L1608-L1630】【F:src/botlib/ai_goal/bot_goal.c】【F:tests/ai/test_ai_goal_move.c】
- Item-visibility fixtures should assert the actual trace endpoint for `BotItemGoalInVisButNotVisible`: the retail code traces to `goal.origin + goal.mins`, not to `(mins + maxs) / 2`. This catches accidental "center of bbox" cleanups that erase the original Q3/Gladiator quirk.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L38299-L38341】【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_goal.c†L1643-L1649】【F:src/botlib/ai_goal/bot_goal.c】【F:tests/ai/test_ai_goal_move.c】
- Goal-stack fixtures should assert both empty and depth-one peek behavior. Slot zero is a sentinel: `BotGetTopGoal` fails at depth zero, and `BotGetSecondGoal` must also fail at depth one rather than copying the sentinel slot.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L37835-L37853】【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_goal.c†L1251-L1276】【F:src/botlib/ai_goal/bot_goal.c】【F:tests/ai/test_ai_goal_move.c】
- Default asset fixtures should exercise `defaul_i.c` and small `$evalfloat` macro cases, because `fw_items.c` uses macro-generated `balance(...)` expressions for pickups such as `weapon_shotgun` and `item_quad`. These checks depend on the Q3 `NUMBERVALUE` lexer path so expanded numeric tokens carry `intvalue` / `floatvalue` into dollar evaluation.【F:dev_tools/assets/fw_items.c†L18-L21】【F:dev_tools/Quake-III-Arena-master/code/botlib/l_script.h†L34-L35】【F:tests/ai/test_ai_weight.c】
- `ReadWeightConfigWithDefines` fixtures should load once with a scoped `DMFLAGS` define and then load again without it. This catches stale global precompiler defines leaking into later weight files and protects the conditional `DF_WEAPONS_STAY` branches in default pickup weights.【F:src/botlib/precomp/l_precomp.c】【F:dev_tools/assets/fw_items.c†L41-L45】【F:tests/ai/test_ai_weight.c】

Document these behaviours in parity fixtures whenever a new guard clause or diagnostic is asserted so the automated checks stay aligned with the reconstructed loader.

## 8. Goal loader and interface-lifecycle parity

- Exercise the raw item loader with the requested filename itself, not an
  `itemconfig/` fallback. Cover a loose file, conflicting entries in
  `pak0.pak` and `pak9.pak`, and decoys in an arbitrary package and
  `pak10.pak`: loose wins, otherwise the lowest numbered package from the exact
  `pak0`–`pak9` probe set wins. Verify logical loose-load logging and packaged
  container-plus-entry logging.【F:src/botlib/common/l_assets.c】【F:src/botlib/ai_goal/bot_goal.c】【F:tests/ai/test_ai_goal_move.c】
- Pin the strict grammar separately from asset resolution. Classnames must be
  quoted, only the recovered seven fields are legal, `type`/`index` are signed
  16-bit integers, partial vectors and the third-component trailing comma are
  accepted, and malformed/over-capacity/truncated input invalidates the whole
  config. The truncated-block diagnostic is exactly
  `couldn't read expected token`. Zero definitions still produce a successful
  config and warning.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L36978-L37139】【F:src/botlib/ai_goal/bot_goal.c】【F:tests/ai/test_ai_goal_move.c】
- On x86, assert the exact raw sizes and important offsets for the `0x08` item
  config, `0x11c` item info, `0x34` level item, `0x38` goal, `0x3cc` goal
  state, and `0x404` weight config. On x64, test the native-pointer adapters
  and sidecar ownership instead of expecting retail pointer offsets to widen
  the exported x86 records. Q3 flags, source filenames, cache keys, and handle
  metadata must never alter the raw prefix.【F:src/botlib/ai_goal/bot_goal.c】【F:src/botlib/ai_weight/ai_weight.c】
- Keep setup and map fixtures separate. `BotSetupGoalAI` loads only global
  item definitions. At whole-library setup, assert weapon → goal → chat order,
  no weapon rollback on goal failure, and success despite a chat-setup failure.
  A named `BotLoadMap` must call AAS before host cache work, return immediately
  on AAS failure, and after success reset clients before `BotInitLevelItems`.
  A null map name forwards the AAS status without cache, model-table,
  client-reset, or level-item side effects.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L32671-L32748】【F:src/botlib/interface/botlib_interface.c】【F:src/botlib/interface/bot_interface.c】【F:tests/parity/test_bot_interface.c】
- Drive `BotAI` with AAS uninitialized and an inactive valid client: it must be
  a successful no-op. With AAS initialized, record call order and assert client
  think precedes the throttled global item refresh.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L32442-L32476】【F:src/botlib/interface/bot_interface.c】【F:tests/parity/test_bot_interface.c】
- Give Seek NBG and Battle NBG the same unobstructed but invalid/absent item
  entity. Seek must pop through the touch-plus-visibility rule; Battle must
  retain the detour until contact or its independent timeout. Also cover true
  contact so both retain the shared item-touch side effect.【F:src/botlib/interface/bot_interface.c】【F:tests/parity/test_bot_interface.c】
- Shutdown tests must not call through a saved export table. Assert that the
  retail 20-function table, the separate extended table, and imports are
  cleared after teardown, then call `GetBotAPI` again and use the fresh table
  for the next setup cycle. Verify extension entries never occupy, resize, or
  reorder the retail prefix.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L43482-L43485】【F:src/botlib/interface/bot_interface.c】【F:src/q2bridge/botlib.h】【F:tests/parity/test_bot_interface.c】
