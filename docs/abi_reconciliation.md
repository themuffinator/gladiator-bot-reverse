# ABI Reconciliation Notes

The fresh Ghidra export should adjudicate the binary-exact external ABI before
new code is promoted into production modules. Current repository headers are
useful reconstruction scaffolding, but they do not all describe the same ABI
surface.

## Source Priority

1. Fixed addresses and diagnostics from `tests/reference/botlib_contract.json`
   and `dev_tools/extract_botlib_contract.py`.
2. Original public structures and exports from `dev_tools/game_source/botlib.h`.
3. Current bridge-facing declarations in `src/q2bridge/botlib.h`.
4. Internal shim declarations in `src/botlib/interface/botlib_interface.h`.
5. Prose docs and Quake III references as naming aids only.

## Initial Drift Inventory

`src2/scripts/generate_abi_inventory.py` produces the current machine-readable
inventory at `src2/maps/abi_inventory.json`. The first generated pass found:

- `bot_export_t`: 20 original entries in common, 0 original-only entries, 67
  bridge-only extension entries.
- `bot_import_t`: 10 original entries in common, 0 original-only entries, 6
  bridge-only extension callbacks: `CvarGet`, `Error`, `AddCommand`,
  `RemoveCommand`, `CmdArgc`, and `CmdArgv`.
- Internal shim import table: only `Print` overlaps the original binary import
  table; the shim-only callbacks are `DPrint`, `BotLibVarGet`, `BotLibVarSet`,
  `AddCommand`, `RemoveCommand`, `CmdArgc`, and `CmdArgv`.
- `bot_input_t`: 5 original fields in common and 1 bridge-only field,
  `weapon`.
- Public macro values in `src/q2bridge/botlib.h` match the original header for
  the checked action, message, max-length, print, line-color, and BLERR names,
  except `BLERR_INVALIDIMPORT`, which is a current reconstruction addition.
- `src2/maps/export_aliases.csv` records the 20-slot relationship between
  HLIL/implementation names and public export-table field names, such as
  `BotLibLoadMap` -> `BotLoadMap` and `BotLibTestHook` -> `Test`. Regenerate
  it with `src2/scripts/generate_export_aliases.py`.

The production header now preserves those retail prefixes explicitly. The
first twenty `bot_export_t` callbacks and first ten `bot_import_t` callbacks
are contiguous in original order, and runtime parity tests fail on any slot
offset drift. `GetBotAPI` copies exactly the ten-callback retail prefix into
library-owned storage, so it neither over-reads a 0.96 caller nor follows later
caller-table mutation. The explicit-size `GetBotAPIEx` compatibility entry
point admits the six bridge-only trailing callbacks for in-repo integrations.
Likewise, `bot_input_t.actionflags` is back in its original prefix slot;
the successor-only `weapon` field follows it as a trailing extension.

| Area | Original 0.96 header | Current reconstructed headers | Intake action |
| --- | --- | --- | --- |
| Export table | Classic lifecycle/client/map/test functions | Original 20-slot prefix followed by goal, movement, weight, weapon, character, and chat extensions | Keep extensions after the tested retail prefix. |
| `bot_input_t` | Five-field command input layout | Original fields retain their offsets; trailing `weapon` supports successor code | Keep the trailing extension outside the retail prefix. |
| Import table | Ten callbacks from `BotInput` through `DebugLineShow` | `GetBotAPI` reads only the original 10-slot prefix; `GetBotAPIEx` accepts the six-callback compatibility tail; a separate internal shim adapts libvars and commands | Keep all three boundaries explicit and prevent prefix drift or caller over-read. |
| Error codes | Botlib error vocabulary from the original interface | Mirrored in multiple headers with local additions | Keep values address-backed and de-duplicate once ABI is stable. |
| Libvar cache | Defaults observed in HLIL contract | Bridge cache in `src/q2bridge/bridge_config.c` extends current behavior | Use `src2/maps/address_to_name.csv` `DAT_` mappings as evidence for original cache slots. |

The symbol naming rule for staged Ghidra output is: use implementation names
from `address_to_name.csv` inside recovered code, and use
`export_aliases.csv` only when reasoning about the public `bot_export_t` table.
`symbol_catalog.json` is the merged source for split planning, so public
20-slot exports remain owned by the `interface` module even when their names
contain domain words such as `Move` or `ConsoleMessage`.

For the next Ghidra export, seed the decompiler with the wrapper groups listed
in `src2/staging/include/ghidra_type_seed_manifest.json`. Treat
`original_public_abi` as the binary-facing authority and the current bridge and
internal module groups as secondary naming/type context until Ghidra confirms
the layouts.

## Recording Type Decisions

Use `src2/maps/type_overrides.yml` for reviewed staging decisions. Each promoted
type should eventually record:

- The binary-facing source of truth.
- Whether the type crosses the DLL boundary.
- Known size/alignment assumptions.
- Fields that are inferred but not yet proven.
- The production header that owns the final definition.

## Promotion Rule

When Ghidra output disagrees with an existing reconstructed header, preserve the
raw staged form until the address-backed export, original public header, and
call-site behavior agree. The production bridge can adapt to a binary-exact ABI;
the binary ABI should not be reshaped to fit current convenience wrappers.
