# Tests Guide

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
