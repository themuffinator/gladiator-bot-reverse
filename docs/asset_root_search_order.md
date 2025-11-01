# Asset Root Search Order

`BotLib_LocateAssetRoot` and `BotLib_ResolveAssetPath` reconstruct the lookup
sequence used by the original Gladiator botlib when resolving assets such as
`chars.h` or per-bot configuration files. The search proceeds in the following
order and stops at the first candidate that contains the requested file:

1. `basedir` + `gamedir` (combined as `<basedir>/<gamedir>` when both values are
   non-empty).
2. `basedir` alone.
3. `cddir` + `gamedir`.
4. `cddir` alone.
5. `gamedir` alone (useful when the engine passes an absolute mod directory).
6. The modern `gladiator_asset_dir` libvar.
7. The `GLADIATOR_ASSET_DIR` environment variable.
8. Repository fallbacks (`dev_tools/assets`, `../dev_tools/assets`,
   `../../dev_tools/assets`).

Duplicates are filtered as the list is constructed so aliased paths (for
example when `gladiator_asset_dir` already points at the chosen legacy
directory) do not trigger redundant filesystem probes. This ordering mirrors the
behaviour extracted from the HLIL traces: legacy `basedir`/`cddir` libvars are
consulted before the newer override, ensuring mod-era launch scripts and
mission-pack CD installs retain precedence when both locations are present.

Unit and parity tests under `tests/common/` and `tests/parity/` exercise
representative combinations of these libvars to guard the reconstructed
ordering against regressions.
