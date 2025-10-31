# Botlib Parity Test Suite

This suite captures the scenarios required to compare reconstructed botlib behaviours against the high-level intermediate language (HLIL)
contract observed in the original Gladiator binary.  As reverse-engineered subsystems become available, each automated check will verify
that newly implemented code issues the same callbacks, honours the same guard conditions, and emits the same log messages as the stock
module.

## HLIL contract verification goals

* **Import table discipline** &mdash; Every test primes a mocked `bot_import_t` table and asserts that bridge wrappers forward their
  inputs exactly once, in the same slot order detected in the HLIL traces.
* **Lifecycle parity** &mdash; Initialization and shutdown checks ensure that the reconstruction matches the original guard clauses for
  duplicate setup calls, teardown ordering, and error propagation.
* **Diagnostic parity** &mdash; Recording doubles inspect the `Print` callback payloads so that banner messages and failure logs retain the
  strings and severity codes observed in the reference binary.

## Exported interface coverage

Each exported entry point in `src/botlib/interface/botlib_interface.c` receives a dedicated test fixture.  The placeholders below outline
how the cmocka-based harness will interrogate the implementation once the helper shims and mocks are committed.

### `GetBotAPI`
* **Handshake wiring** &mdash; Expect the loader to translate the raw `bot_import_t` into a `botlib_import_table_t`, seeding wrappers for
  `Print`, `DPrint`, `BotLibVarGet`, and `BotLibVarSet`. The handshake then calls `BotInterface_SetImportTable`, `Q2Bridge_SetImportTable`,
  and `Bridge_ResetCachedUpdates` so subsystems exercising memory, libvars, or cached entity state observe the freshly populated pointers.
  Future fixtures should verify this ordering and capture the shimmed callbacks so downstream assertions can intercept log output and
  libvar probes deterministically.

### `BotInterface_SetImportTable`
* **Initialization guard** &mdash; Seed the interface with a valid `bot_import_t` table, then call the setter again with `NULL` and confirm
  that the previously stored pointer remains intact.
* **Logging side effects** &mdash; Verify that setting a table when logging is enabled records the same HLIL-observed message through the
  mocked `Print` slot (if future instrumentation adds such diagnostics).

### `BotInterface_GetImportTable`
* **Round-trip retrieval** &mdash; After stashing a mock table, confirm that the getter returns the identical pointer.
* **Default state** &mdash; When called before initialization, the getter should yield `NULL` without triggering mocked callbacks.

### `BotSetupLibrary`
* **Import validation** &mdash; Provide tables missing `BotLibVarGet`, expect `BLERR_INVALIDIMPORT`, and assert that the failure is logged via
  the mocked `Print` callback.
* **Memory allocator failure** &mdash; Replace `BotMemory_Init` with a stub that returns `false`, expect `BLERR_INVALIDIMPORT`, and confirm
  that no subsystem setup routines run.
* **Repeated initialization** &mdash; Call `BotSetupLibrary` twice; the second invocation should return `BLERR_LIBRARYALREADYSETUP` without
  repeating any mocked callbacks beyond the rejection log.
* **Happy path parity** &mdash; Prime bridge configuration accessors with canned `libvar_t` values, perform setup, and verify that logging
  matches the HLIL banner while each subsystem mock records a single initialization call.

### `BotShutdownLibrary`
* **Guard when uninitialized** &mdash; Invoke shutdown before setup and assert the `BLERR_LIBRARYNOTSETUP` return along with the expected
  diagnostic.
* **Subsystem unwinding** &mdash; After a successful setup, confirm that utilities, AI, EA, and AAS shutdown mocks fire in the reverse order,
  matching HLIL traces.
* **Idempotent shutdown** &mdash; Re-run shutdown and ensure it neither crashes nor double-frees allocator state.

### `BotLibraryInitialized`
* **State transitions** &mdash; Assert that the helper reports `false` before setup, `true` after setup, and `false` following shutdown,
  exercising the complete lifecycle.

### `BotInterface_GetLibraryVariables`
* **Cache population** &mdash; Inject deterministic `libvar_t` records through bridge accessors, trigger setup, and confirm that the returned
  struct mirrors the expected HLIL values.
* **Reset on shutdown** &mdash; After teardown, validate that cached values revert to their defaults while leaving the pointer stable.

The tests above will share a harness that wires recording doubles into the mocked `bot_import_t` table and exposes helper assertions for
sequence ordering, argument capture, and log comparison.

## Implementation roadmap

1. **Harness utilities** &mdash; Publish a reusable mock table generator under `tests/support/` that records import invocations.
2. **cmocka fixtures** &mdash; Define setup and teardown routines that reset the botlib interface between tests, mirroring the HLIL guard
   expectations.
3. **Contract capture** &mdash; Convert the outline above into concrete `cmocka_unit_test()` registrations once the production modules expose
   hooks for dependency injection.

## Diagnostic catalogue integration

The `dev_tools/extract_botlib_contract.py` utility parses the HLIL dump from the
original Gladiator DLL and emits `tests/reference/botlib_contract.json`. The
catalogue records, for every exported botlib function and guard helper,

* the severity and text of each `Print` invocation observed in the binary, and
* the literal error codes returned along each guard path.

Upcoming parity tests will load this JSON file during fixture setup. The cmocka
harness will compare the strings captured by the mocked `Print` callback against
the catalogue entries keyed by function name, and it will assert that the stub
returns reported by the reimplementation match the reference error codes. The
libvar default block in the same JSON document also seeds expectations for the
cached configuration values exercised by `BotSetupLibrary` tests.

