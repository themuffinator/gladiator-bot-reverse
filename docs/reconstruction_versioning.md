# Reconstruction versioning

This project carries **two** version numbers. Conflating them would break either
the original Gladiator mod or the ability to tell one release from another, so
they are kept deliberately separate.

| | Legacy botlib version | Reconstruction version |
| --- | --- | --- |
| Value | `0.96` (frozen) | `1.0.0` and upward |
| Describes | the retail module being reconstructed | this project |
| Declared in | `GLADIATOR_LEGACY_BOTLIB_VERSION` | `GLADIATOR_RECON_VERSION` |
| Visible to the host at runtime | **yes** — `BotVersion()` and the startup banner | no |
| Visible in the shipped file | `strings` | file properties, `VERSION.txt`, archive name, `strings` |
| Changes when | never, unless a different retail build is targeted | every release |

Both are declared in [`cmake/ReconstructionVersion.cmake`](../cmake/ReconstructionVersion.cmake),
which is the single source of truth for the build, the packaging script and the
release workflow.

## Why the legacy version is frozen

The retail `gladiator.dll` reports `BotLib v0.96` in two places, and Quake II
hosts and the Gladiator mod were built against exactly that:

- `BotVersion()` returns the literal string `"BotLib v0.96"`.
- `BotSetupLibrary` prints `BotLib v0.96\n` as the second line of its startup
  banner.

Both are pinned by address in the parity contract —
`tests/reference/botlib_contract.json` records the banner print at
`0x10037bef` — and asserted by
`tests/parity/test_bot_interface.c`. The headless log catalogue
(`tests/parity/engine_logs/catalog.json`) matches the banner line exactly.

So `BotVersion()` must keep returning `"BotLib v0.96"` even when the
reconstruction reaches 2.0. Bumping it to advertise reconstruction progress
would be reporting a botlib version that never existed.

## How the reconstruction version reaches a shipped module

Neither mechanism touches a retail code path, so neither can perturb parity.

1. **Embedded marker string.** `src/botlib/interface/bot_interface.c` defines
   `g_gladiatorReconstructionVersion`, an ordinary non-static `const char[]`
   that lands in `.rodata` and survives into the shipped binary:

   ```text
   @(#) Q2 Gladiator Bot Botlib Reconstruction 1.0.0 (f0ae4b4799c6, 2026-08-20) [botlib ABI 0.96]
   ```

   The `@(#)` prefix is the classic SCCS marker, so `what(1)` finds it as well
   as `strings`. It is reachable in-process through
   `BotReconstructionVersion()`, but it is *not* exported: the module marks only
   `GetBotAPI` with `GLADIATOR_API`, builds with `WINDOWS_EXPORT_ALL_SYMBOLS`
   off, and hides everything else on ELF and Mach-O. The export table of a
   built module is still exactly one symbol.

2. **Windows `VERSIONINFO` resource.** Generated from
   `src/shared/gladiator_version.rc.in` when a resource compiler is available.
   `FILEVERSION` carries the reconstruction version and `PRODUCTVERSION`
   carries `0.96`, so the file properties dialog states both facts without
   either being mistaken for the other. If no resource compiler is found the
   build says so and continues — the marker string still identifies the module.

The reconstruction version is deliberately **not** printed from the startup
banner. That banner reproduces retail's four lines verbatim and a fifth line
would be a divergence.

## Numbering policy

Semantic versioning, read against *reconstruction fidelity* rather than an API:

- **Major** — a change to what the module fundamentally is: targeting a
  different retail build, or a deliberate break in compatibility with the
  original mod's data or ABI.
- **Minor** — new reconstructed behaviour, newly covered subsystems, or a batch
  of resolved divergences.
- **Patch** — fixes to already-reconstructed behaviour, build and packaging
  fixes, documentation.

Pre-release suffixes (`1.1.0-rc1`) are accepted by the build and the release
workflow.

## Cutting a release

1. Bump `GLADIATOR_RECON_VERSION` in `cmake/ReconstructionVersion.cmake`.
2. Add the matching section to [`CHANGELOG.md`](../CHANGELOG.md).
3. Commit, then run the **Release** workflow from the Actions tab
   (`.github/workflows/release.yml`). Leave the version input empty so it uses
   the value committed in step 1.

The workflow refuses to run if the requested version is malformed, equals the
legacy `0.96`, or already has a tag. Each built module is checked for both the
reconstruction marker *and* the legacy `BotLib v0.96` string before it is
packaged, so a release cannot ship a module that lost either.

To produce the same archive locally — both modules, as the release ships them:

```bash
python tools/package_release.py --platform win32 --binary build-x86/gladiator.dll --binary build-x86/src/game/gamex86.dll
```

## Overriding the version for a build

```bash
cmake -S . -B build -DGLADIATOR_RECON_VERSION=1.1.0-rc1
```

The release workflow warns when a dispatch input disagrees with the committed
value, because the repository should record what was released.
