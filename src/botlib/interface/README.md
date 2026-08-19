# Botlib interface

The module's outward face: the export table the Quake II host receives, and the
per-client state behind it.

| File | Contents |
| --- | --- |
| `bot_interface.c` | The retail export wrappers. `GetBotAPI` assembles the 20-pointer table the Gladiator mod expects, and each wrapper reproduces the retail entry point's validation, diagnostics and return codes |
| `bot_state.c` | Per-client bot state: settings slots, names, skins, lifecycle |
| `botlib_interface.c` | `BotSetupLibrary` / `BotShutdownLibrary` and subsystem init ordering |

## Constraints

Two things in here are load-bearing for compatibility and must not drift:

- **The export table.** A built module exports exactly one symbol,
  `GetBotAPI`, matching retail. `WINDOWS_EXPORT_ALL_SYMBOLS` is off and
  non-Windows builds hide everything else; new public functions need
  `GLADIATOR_API` to be exported at all.
- **The version string.** `BotVersion()` returns `"BotLib v0.96"` and
  `BotSetupLibrary` prints it in the startup banner. Both are pinned by address
  in `tests/reference/botlib_contract.json` and must keep their exact bytes.
  The reconstruction's own version lives alongside it as
  `g_gladiatorReconstructionVersion`, deliberately off every retail code path.

`GetBotAPIEx` is an in-repo extension for the bridge and test harness. It is
not exported and is not part of the retail surface.
