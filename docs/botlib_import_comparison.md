# Botlib import callback comparison

## Callbacks expected by the original Gladiator DLL

The disassembly of `gladiator.dll` shows that the bot library copies a 0x28-byte import block and treats it as ten function pointers. During `GetBotAPI`, the DLL performs:

- A `memcpy` of 0x28 bytes from the engine-provided table into `data_10063fe0`.
- Subsequent code calls the imported slots as follows:
  - `data_10063fe0(arg1, s)` – first slot, used to feed a client-specific structure into the botlib.
  - `data_10063fe4(...)` – second slot, invoked when issuing console commands (e.g., `EA_Command`).
  - `data_10063fe8(type, fmt, ...)` – third slot, used for formatted logging across the DLL.
  - `data_10063fec(&trace, start, mins, maxs, end, passent, mask)` – fourth slot, returns a 0x54-byte trace result.
  - `data_10063ff0(point)` – fifth slot, queried with a single vector argument (point contents check).
  - `data_10063ff4(ptr)` and `data_10063ff8(ptr)` – sixth and seventh slots, used to allocate and free heap blocks.
  - `data_10063ffc()` – eighth slot, repeatedly used to obtain integer handles.

These uses align with the original ten-entry `bot_import_t`: `BotInput`,
`BotClientCommand`, `Print`, `Trace`, `PointContents`, `GetMemory`,
`FreeMemory`, and the three debug-line callbacks.

### Expected slot order and signatures inferred from HLIL

| Slot | Observed use | Inferred prototype |
| --- | --- | --- |
| 0 | Client data feed | `void (*BotInput)(int client, bot_input_t *bi);` |
| 1 | Console command emission | `void (*BotClientCommand)(int client, char *str, ...);` |
| 2 | Logging | `void (*Print)(int type, const char *fmt, ...);` |
| 3 | BSP trace | `bsp_trace_t (*Trace)(vec3_t start, vec3_t mins, vec3_t maxs, vec3_t end, int passent, int contentmask);` |
| 4 | Point contents | `int (*PointContents)(vec3_t point);` |
| 5 | Allocator | `void *(*GetMemory)(int size);` |
| 6 | Free | `void (*FreeMemory)(void *ptr);` |
| 7 | Handle generator | `int (*DebugLineCreate)(void);` |
| 8 | Delete debug line | `void (*DebugLineDelete)(int line);` |
| 9 | Draw debug line | `void (*DebugLineShow)(int line, vec3_t start, vec3_t end, int color);` |

The reconstructed elementary-action layer now exercises both leading slots
with their recovered call shapes. `EA_EndRegular` passes the live internal
`bot_input_t` to `BotInput`, samples the jump bit after the callback, and only
then performs the transient reset/latch restoration. Specialized commands pass
one token, one argument, and `NULL`; generic `EA_Command` captures nine
argument slots, emits the retail `EA_Command: too many arguments` error when
the ninth is non-NULL, still dispatches that slot, and appends a final explicit
`NULL`. The production `BotAI` path dispatches through `EA_EndRegular`;
`EA_GetInput` remains a bridge-only snapshot/reset adapter and must not be
mistaken for the retail slot-0 callback boundary.

## Reconstructed compatibility surface

The reconstruction keeps the ten retail callbacks first and appends six
bridge-only helpers. The retail `GetBotAPI` entry reads only the prefix;
`GetBotAPIEx(imports, size)` is the explicit opt-in path for this tail:

| Order | Callback | Signature |
| --- | --- | --- |
| 0 | BotInput | `void (*BotInput)(int client, bot_input_t *bi);` |
| 1 | BotClientCommand | `void (*BotClientCommand)(int client, char *str, ...);` |
| 2 | Print | `void (*Print)(int type, char *fmt, ...);` |
| 3 | Trace | `bsp_trace_t (*Trace)(vec3_t start, vec3_t mins, vec3_t maxs, vec3_t end, int passent, int contentmask);` |
| 4 | PointContents | `int (*PointContents)(vec3_t point);` |
| 5 | GetMemory | `void *(*GetMemory)(int size);` |
| 6 | FreeMemory | `void (*FreeMemory)(void *ptr);` |
| 7 | DebugLineCreate | `int (*DebugLineCreate)(void);` |
| 8 | DebugLineDelete | `void (*DebugLineDelete)(int line);` |
| 9 | DebugLineShow | `void (*DebugLineShow)(int line, vec3_t start, vec3_t end, int color);` |
| 10 | CvarGet | `cvar_t *(*CvarGet)(const char *name, const char *default_value, int flags);` |
| 11 | Error | `void (*Error)(const char *fmt, ...);` |
| 12 | AddCommand | `void (*AddCommand)(const char *name, void (*function)(void));` |
| 13 | RemoveCommand | `void (*RemoveCommand)(const char *name);` |
| 14 | CmdArgc | `int (*CmdArgc)(void);` |
| 15 | CmdArgv | `const char *(*CmdArgv)(int index);` |

## Current `botlib_import_table_t`

The in-repo mirror only exposes eight callbacks:

- `Print(int type, const char *fmt, ...)`
- `DPrint(const char *fmt, ...)`
- `BotLibVarGet(const char *var_name, char *value, size_t size)`
- `BotLibVarSet(const char *var_name, const char *value)`
- `AddCommand(const char *name, void (*function)(void))`
- `RemoveCommand(const char *name)`
- `CmdArgc(void)`
- `CmdArgv(int index)`

This eight-callback table is an internal adapter, not the DLL-facing ABI. It
routes printing, cached libvars, and console commands while engine-facing
input, trace, allocator, and debug-line calls continue through the copied
public `bot_import_t`.

## Gap summary

| Source | Present callbacks | Missing callbacks | Notes |
| --- | --- | --- | --- |
| Gladiator DLL HLIL (10 slots) | All callbacks from `BotInput` through `DebugLineShow` | None | `GetBotAPI` copies the 0x28-byte 32-bit table. |
| Repo `bot_import_t` (10 + 6) | Exact retail prefix plus `CvarGet`, `Error`, and console-command helpers | None from retail | Tests enforce prefix offsets, exact bounded `GetBotAPI` copying, copied-callback ownership, and size-aware compatibility-tail opt-in. |
| Repo `botlib_import_table_t` (8) | Print, DPrint, BotLibVarGet/BotLibVarSet, AddCommand, RemoveCommand, CmdArgc, CmdArgv | Not applicable | Internal adapter rather than a binary ABI replacement. |
