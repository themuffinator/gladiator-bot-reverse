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
   - **Skip** – Any remaining `cmocka_skip()` markers in `test_bot_interface.c` indicate the scenario has not yet been implemented; replace these with assertions as reverse-engineering work firms up the behaviour. 【F:tests/parity/test_bot_interface.c†L1-L68】
   - **Fail** – Review the cmocka failure summary to identify which expectation broke. Most assertions will surface mismatched bridge callbacks, incorrect guard return codes, or missing diagnostics described in the parity README.

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

## 6. Weight configuration parity

Handle-based helpers now wrap the weight parser so callers mirror Gladiator’s lifecycle:

- `BotAllocWeightConfig` returns a 1-based identifier backed by the botlib heap; the guard path emits `"BotAllocWeightConfig: no free handles"` when the small table is exhausted so parity tests can assert the legacy failure mode.【F:src/botlib/ai/weight/bot_weight.c†L37-L83】
- `BotLoadWeights` validates both the handle and filename before dispatching to `ReadWeightConfig`. Parser failures continue to raise the historical `"couldn't load weights\n"` diagnostic captured in the HLIL trace.【F:src/botlib/ai/weight/bot_weight.c†L95-L123】【F:docs/weight_config_analysis.md†L6-L18】
- `BotSetWeight` leverages the exported `BotWeight_FindIndex` helper so tests can probe guard clauses (missing handle/config, unknown names) without poking the private tree representation.【F:src/botlib/ai/weight/bot_weight.c†L143-L174】【F:src/botlib/ai/weight/bot_weight.h†L53-L67】
- `BotFindFuzzyWeight` / `BotFuzzyWeightHandle` expose read-only queries for upcoming goal and weapon tests, falling back to sentinel values when the handle is invalid. This mirrors the Quake III access pattern while keeping the tree management internal.【F:src/botlib/ai/weight/bot_weight.c†L176-L197】

Document these behaviours in parity fixtures whenever a new guard clause or diagnostic is asserted so the automated checks stay aligned with the reconstructed loader.
