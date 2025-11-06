# Tests Guide

## Bridge wrapper validation roadmap

Once the mocked `bot_import_t` callbacks are wired into automated suites they
will exercise the bridge wrappers, translation shims, and the eventual libvar
cache in three complementary passes:

1. **Wrapper verification** &mdash; Recording doubles will assert that each wrapper
   forwards arguments to the correct import slot, preserves constness, and
   performs any marshalling required by the Quake II bridge before releasing the
   call.  Mismatch alarms will flag regressions in `src/q2bridge/` helpers as new
   wrapper implementations come online.
2. **Translator synchronization** &mdash; Planned harness hooks will compare the
   data passed to the import table against translator outputs (e.g.,
   `bot_input_t` assembly, movement command translators).  Divergences will be
   surfaced through focused assertions so translator upgrades can be validated
   without running a full game loop.
3. **Libvar caching checks** &mdash; Once the configuration cache lands, the mocks
   will simulate cache invalidation and confirm that updates propagate back
   through the bridge.  This allows us to pin down stale-cache edge cases before
   the caching layer ships.

The upcoming helpers will be shared by the **AAS navigation stack** and the
broader **AI module orchestration** so that integration tests can cover
cross-module behaviors (navigation planning, decision loops, and command
issuance) from the outset.

## AI weapon regression tests

The AI weapon suite under `tests/ai/` loads the Gladiator weapon definitions
and weight scripts directly from the repository. When running the tests outside
of CTest, make sure the following libvars are seeded before calling into the
botlib so that asset discovery mirrors the in-game configuration:

- `weaponconfig` &mdash; absolute path to `dev_tools/assets/weapons.c`.
- `max_weaponinfo` &mdash; increased to at least `64` to cover the full Quake II
  arsenal captured in the regression data.
- `max_projectileinfo` &mdash; likewise bumped to `64` so projectile definitions
  are not clipped during parsing.

The regression harness sets these values explicitly and skips the assertions if
the assets are unavailable (for example, when the repository is cloned without
`dev_tools/`).

## AI character regression tests

The AI character tests rely on the Gladiator assets mirroring their original
Quake III directory layout. The harness automatically points
`GLADIATOR_ASSET_DIR` at `${PROJECT_SOURCE_DIR}/dev_tools/assets`, copies
`syn.c`, `match.c`, and `rchat.c` into `dev_tools/assets/bots/` for the duration
of the run, and restores the tree afterwards. The integration exercise that
drives `BotSetupClient` also overrides `weaponconfig`, `max_weaponinfo`, and
`max_projectileinfo` through the exported `BotLibVarSet` hook so the weapon
library and weight tables resolve to the repository assets.

## AI goal/move orchestration tests

The goal and movement orchestration suite under `tests/ai/test_ai_goal_move.c`
shares the same asset expectations as the character regression tests. The
fixtures reuse the bot setup pipeline to allocate goal and move state, so make
sure `${PROJECT_SOURCE_DIR}/dev_tools/assets` contains the Gladiator bot
profiles (`bots/babe_c.c`) and chat scripts. The harness automatically bridges
`syn.c`, `match.c`, and `rchat.c` into `dev_tools/assets/bots/`, seeds the
`GLADIATOR_ASSET_DIR` environment variable, and initialises the botlib memory
heap before running each scenario. When executing the tests manually, ensure the
asset directory is writable so the temporary chat copies can be created and
deleted.

## AAS regression tests

The navigation harness under `tests/aas/` boots the botlib memory system and
loads a miniature BSP/AAS pair to exercise `AAS_LoadMap` and
`AAS_UpdateEntity`. To keep the tests deterministic:

- Download `test_nav.bsp` and `test_nav.aas` separately (for example, from the
  original Gladiator bot asset distribution) and place them under a directory
  referenced by `GLADIATOR_AAS_TEST_ASSET_DIR`. When the environment variable is
  unset the harness falls back to `${PROJECT_SOURCE_DIR}/dev_tools/assets`, so
  copying the files into `dev_tools/assets/maps/` also satisfies the
  requirement.
- Set `GLADIATOR_ASSET_DIR` (or run from the harness root) so `AAS_LoadMap`
  resolves paths relative to the asset directory. The tests automatically set
  the variable to the chosen asset root when it is not already configured.
- Provide a writable working directory: the fixture temporarily changes into
  the asset root so the loader can open `maps/test_nav.*` using the same
  relative paths as the game.

With those prerequisites satisfied the harness will verify that `aasworld` is
initialised correctly, that entity updates populate the expected area links, and
that reachability records expose the travel times baked into the sample AAS
file. If the files are missing the harness prints a skip reason so CI jobs that
do not stage the optional assets continue to pass.

### Bot interface parity mover fixtures

The parity suite (`tests/parity/test_bot_interface.c`) includes
`test_bot_bridge_tracks_mover_entity_updates`, which exercises the mover
catalogue against a miniature Quake III map. The test expects the following
assets to be present under the active asset root:

- `${PROJECT_SOURCE_DIR}/dev_tools/assets/maps/test_mover.bsp`
- `${PROJECT_SOURCE_DIR}/dev_tools/assets/maps/test_mover.aas`

`asset_env_initialise` (from `tests/support/asset_env.c`) stages
`${PROJECT_SOURCE_DIR}/dev_tools/assets` as `GLADIATOR_ASSET_DIR`, switches the
working directory to that location, and records the path so fixtures can probe
`maps/test_mover.*`. When either file is missing `ensure_map_fixture` prints a
diagnostic such as `bot interface parity skipped: missing
${PROJECT_SOURCE_DIR}/dev_tools/assets/maps/test_mover.aas`, and the test calls
`cmocka_skip()` to report a skipped case rather than a failure. CI operators
should install the BSP/AAS pair in the listed directory so the mover parity
checks remain active.

### Precompiler lexer parity fixtures

`tests/parity/test_precompiler_lexer.c` lexes `${PROJECT_SOURCE_DIR}/dev_tools/assets/fw_items.c`
and `${PROJECT_SOURCE_DIR}/dev_tools/assets/syn.c` and compares the output token
stream against the catalogue recorded from the Gladiator HLIL traces. With the
lexer implementation in place the regression now asserts that
`PC_LoadSourceFile`, `PC_ReadToken`, `PC_PeekToken`, and `PC_UnreadToken`
reproduce the same sequence. The fixture only reports a skip when either asset
file is missing; otherwise divergences surface as descriptive assertion
failures so regressions are caught immediately.

To streamline build-system integration, we anticipate driving these tests via
`CTest` invoking a lightweight **GoogleTest** harness.  The harness will provide
fixtures for seeding the mocked `bot_import_t` table, helpers for table diffing,
and adapters so future Lua- or Python-based smoke tests can reuse the same
recording doubles.

## Mocking `bot_import_t`

The Quake II bridge exposes `bot_import_t` as a table of callbacks that the game
passes into the bot library.  When testing modules that consume this table you
can swap each slot with a lightweight test double that records interactions or
returns canned values.  The table defined in [`src/q2bridge/botlib.h`](../src/q2bridge/botlib.h)
contains the following slots:

| Slot | Signature | Typical responsibility |
| --- | --- | --- |
| `BotInput` | `void (*)(int client, bot_input_t *bi)` | Push controller commands collected by the bot into the engine. |
| `BotClientCommand` | `void (*)(int client, char *str, ...)` | Issue console commands to the client. |
| `Print` | `void (*)(int type, char *fmt, ...)` | Send formatted diagnostics to the engine log. |
| `CvarGet` | `cvar_t *(*)(const char *name, const char *default_value, int flags)` | Query or create console variables within the engine. |
| `Error` | `void (*)(const char *fmt, ...)` | Emit fatal diagnostics routed through the engine's error handler. |
| `Trace` | `bsp_trace_t (*)(vec3_t start, vec3_t mins, vec3_t maxs, vec3_t end, int passent, int contentmask)` | Query collision geometry for visibility or movement checks. |
| `PointContents` | `int (*)(vec3_t point)` | Determine the material or contents at a point in space. |
| `GetMemory` | `void *(*)(int size)` | Allocate memory through the engine. |
| `FreeMemory` | `void (*)(void *ptr)` | Release memory obtained from `GetMemory`. |
| `DebugLineCreate` | `int (*)(void)` | Reserve an identifier for a debug line. |
| `DebugLineDelete` | `void (*)(int line)` | Remove a previously created debug line. |
| `DebugLineShow` | `void (*)(int line, vec3_t start, vec3_t end, int color)` | Draw or update a debug line segment. |

### Replacing slots with recording doubles

You can wrap each callback with a lambda or struct that increments counters and
captures arguments for later assertions.  The following pseudocode shows one way
to build a reusable harness:

```c
struct BotImportRecorder {
    std::atomic<int> bot_input_calls{0};
    std::vector<BotInputArgs> bot_input_invocations;
    std::vector<std::string> client_commands;
    std::vector<std::tuple<int, std::string>> print_events;
    // ... additional collections for trace, point contents, etc.

    bot_import_t make_table() {
        bot_import_t table{};
        table.BotInput = [this](int client, bot_input_t *bi) {
            ++bot_input_calls;
            bot_input_invocations.push_back({client, *bi});
        };
        table.BotClientCommand = [this](int client, char *str, ...) {
            std::va_list args;
            va_start(args, str);
            client_commands.push_back(vformat(str, args));
            va_end(args);
        };
        table.Print = [this](int type, char *fmt, ...) {
            std::va_list args;
            va_start(args, fmt);
            print_events.emplace_back(type, vformat(fmt, args));
            va_end(args);
        };
        // Replace the remaining slots with lambdas that push arguments
        // into the recorder's vectors or update dedicated counters.
        return table;
    }
};
```

When the system under test invokes a callback, the recorder increments the
corresponding counter or appends the call data.  Assertions can then verify both
how many times a slot was used and the parameters provided.

For tests that require configurable responses (such as `Trace` or
`PointContents`), provide a `std::queue` of precomputed return values:

```c
struct TracePlan {
    bsp_trace_t result;
    InvocationArgs args;
};

std::queue<TracePlan> planned_traces;

bot_import_t import_table{};
import_table.Trace = [&](vec3_t start, vec3_t mins, vec3_t maxs, vec3_t end,
                         int passent, int contentmask) {
    auto plan = planned_traces.front();
    planned_traces.pop();
    recorded_traces.push_back({start, mins, maxs, end, passent, contentmask});
    return plan.result;
};
```

A simple structure diagram can also clarify the relationships:

```
BotImportRecorder
├── counters (e.g., bot_input_calls, debug_line_show_calls)
├── captured args
│   ├── bot_input_invocations : list of (client, bot_input_t)
│   ├── client_commands : list of strings
│   └── traces : list of (start, mins, maxs, end, passent, contentmask)
└── response queues (optional)
    ├── planned_traces : queue<TracePlan>
    └── point_contents_values : queue<int>
```

By pre-populating the response queues and verifying the recorded collections,
implementers can quickly assemble deterministic mocks tailored to each unit test.
# GetBotAPI Scenario Plan

This document outlines planned test scenarios for `GetBotAPI`. Each section describes the expected behavior so future contributors can convert these plans into automated test cases.

## Successful module loading
- Provide a fully populated module implementing the required exported entry point.
- Expect `GetBotAPI` to load the module, resolve the entry point, and return a valid interface pointer.
- Verify that the interface exposes the expected function table without triggering any guards or error paths.

## Initialization guard failures

### Missing exported symbol
- Supply a module that lacks the expected `CreateBotAPI` (or equivalent) export.
- `GetBotAPI` should detect the absence, refuse to initialize the module, and return `nullptr` while reporting an appropriate error.

### Setup routine failure
- Use a module whose exported entry point simulates a setup failure (e.g., returns an error code or leaves the interface pointer unset).
- `GetBotAPI` must treat the failure as fatal, unload the module if needed, and propagate a failure result to the caller.

### Double unload protection
- Trigger a scenario where module unloading is requested twice (e.g., by calling the unload path after a guard failure and again during teardown).
- Ensure `GetBotAPI` protects against double-unload issues, avoiding crashes or double-free errors.

## Placeholder-return expectations
- For modules under `src/shared/`, `src/q2bridge/`, and `src/botlib/interface/`, confirm that unimplemented interfaces return safe placeholder objects when `GetBotAPI` is invoked.
- Each placeholder should provide deterministic stub behavior (e.g., no-ops, default values) so downstream components can continue operating in tests.
- Verify that placeholder returns are clearly distinguishable from fully initialized interfaces, allowing tests to assert the correct path was taken.
