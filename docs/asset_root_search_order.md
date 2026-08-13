# Asset Root Search Order

`BotLib_LocateAssetRoot` and `BotLib_ResolveAssetPath` reconstruct the lookup
sequence used by the original Gladiator botlib when resolving assets such as
`chars.h` or per-bot configuration files.

## What retail actually probes

Retail's resolver is `sub_10041f60`. It reads the `gamedir` libvar once and
calls `sub_10041ba0` twice: with the `basedir` libvar at `0x10041f92` and with
the `cddir` libvar at `0x10041fba`.

`sub_10041ba0(dir, gamedir, filename, out)` fills exactly two 0x90-byte
game-directory slots before it starts probing:

| slot | source | address |
| ---- | ------ | ------- |
| 0 | the `gamedir` argument, copied only when it is non-NULL | `0x10041c2b` |
| 1 | the literal `"baseq2"` | `0x10041c3f` |

It then loops over those two slots and no others - the loop bound at
`0x10041e24` is `var_244 + 1 s< 2`. For each slot it builds `<dir>/<slot>/` and:

1. Probes the loose file `<dir>/<slot>/<filename>` with `access(path, 4)`
   (`0x10041d2d`), logging `accessing %s` first (`0x10041d17`).
2. Failing that, walks `pak0.pak` through `pak9.pak` in that directory
   (`0x10041e13` bounds the loop at 10, `0x10041dbc` formats `"pak%d.pak"`),
   and searches each package that exists for the member (`0x10041e0d`),
   logging `searching %s in %s` (`0x10041df0`).

Two consequences follow directly from the code:

* The loose probe and the package scan are **interleaved per directory**, not
  run as two separate passes over every directory.
* The slot append at `0x10041ca8` is skipped when the slot string has zero
  length. An empty `gamedir` libvar therefore collapses slot 0 onto the bare
  directory, which is the only way `<basedir>/<filename>` or
  `<cddir>/<filename>` is ever tried. When `gamedir` is set, retail never
  probes a bare `basedir` or `cddir`, and it never probes `gamedir` on its own
  at all.

So the full retail order, for a non-empty `gamedir`, is:

1. `<basedir>/<gamedir>/<file>`, then `<basedir>/<gamedir>/pak0-9.pak`
2. `<basedir>/baseq2/<file>`, then `<basedir>/baseq2/pak0-9.pak`
3. `<cddir>/<gamedir>/<file>`, then `<cddir>/<gamedir>/pak0-9.pak`
4. `<cddir>/baseq2/<file>`, then `<cddir>/baseq2/pak0-9.pak`

and for an empty `gamedir` steps 1 and 3 degrade to `<basedir>/<file>` and
`<cddir>/<file>` respectively.

## What this reconstruction adds

Retail has no environment variable and no host-side override; only the
`basedir`, `gamedir` and `cddir` libvars exist. The reconstruction keeps the
retail roots above and appends host-side conveniences after them:

5. The `gladiator_asset_dir` libvar, when it is set. Because an explicitly set
   libvar is a deliberate host decision, it wins over the ambient environment.
6. The `GLADIATOR_ASSET_DIR` environment variable, consulted only when the
   `gladiator_asset_dir` libvar is empty.
7. Repository fallbacks (`dev_tools/assets`, `../dev_tools/assets`,
   `../../dev_tools/assets`).

Roots from steps 5 and 6 are flagged as overrides. `BotLib_ResolveAssetPath`
gives those roots one loose-file pass ahead of everything else so a staged test
or development asset tree beats an installed game directory; every root is then
walked again in the retail order with the loose probe and the package scan
interleaved exactly as `sub_10041ba0` does.

Duplicates are filtered as the list is constructed, so aliased paths (for
example when `gladiator_asset_dir` already points at the chosen legacy
directory) do not trigger redundant filesystem probes; an alias that was first
registered as a legacy root is upgraded in place to an override.

Unit and parity tests under `tests/common/` and `tests/parity/` exercise
representative combinations of these libvars to guard the reconstructed
ordering against regressions, and `tests/aas/test_aas_map.c` pins the
per-directory loose/package interleave and the `baseq2` fallback with staged
on-disk fixtures.
