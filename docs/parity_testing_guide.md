# Botlib Parity Testing Guide

This guide outlines how to configure a development environment for the botlib parity suite, run the available checks, and keep the HLIL-derived expectations catalogue aligned with new reverse-engineering findings. The parity plan captures the behaviours described in the existing parity README and placeholder cmocka harness.

## 1. Environment configuration

1. **Install required toolchain**
   - CMake 3.16 or newer and a C11/C++17-compatible compiler (the top-level build requires both languages). 【F:CMakeLists.txt†L1-L15】
   - Internet access for the first configuration so CMake's `FetchContent` block can clone the cmocka test dependency when the parity suite is enabled. 【F:tests/CMakeLists.txt†L1-L23】

2. **Configure the build tree**
   ```bash
   cmake -S . -B build -DBUILD_TESTING=ON -DBOTLIB_PARITY_ENABLE_SOURCES=ON
   ```
   - `BUILD_TESTING=ON` activates the `tests/` subtree. 【F:CMakeLists.txt†L17-L25】
   - `BOTLIB_PARITY_ENABLE_SOURCES=ON` switches the parity target from the placeholder custom target to an executable that compiles `tests/parity/test_bot_interface.c`. Leave this option off if the suite should remain a stub during feature work. 【F:tests/parity/CMakeLists.txt†L1-L20】

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
- **CTest finds no tests** – Confirm that both `BUILD_TESTING=ON` and `BOTLIB_PARITY_ENABLE_SOURCES=ON` were set during configuration; otherwise the parity target remains a placeholder custom target and no executable is built. 【F:tests/parity/CMakeLists.txt†L1-L20】

Keeping this workflow up to date ensures contributors can quickly validate reconstructed functionality against the original Gladiator botlib contract while iterating on new discoveries from the HLIL traces.
