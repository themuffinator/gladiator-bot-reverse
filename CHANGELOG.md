# Changelog

All notable changes to the **Q2 Gladiator Bot Botlib Reconstruction** are
recorded here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

Versions on this page are *reconstruction* versions and follow
[Semantic Versioning](https://semver.org/spec/v2.0.0.html). They are
independent of the legacy botlib version the module reports to Quake II, which
is frozen at **0.96** because it is part of the ABI the original Gladiator mod
links against. See [docs/reconstruction_versioning.md](docs/reconstruction_versioning.md).

## [Unreleased]

Nothing yet.

## [1.0.0] — 2026-08-20

First release of the reconstruction. `gladiator.dll` loads in a real Quake II
dedicated server, the bots navigate, fight and chat, and every confirmed
behavioural difference from the retail module has been resolved.

Roughly 492 commits between 2025-10-30 and 2026-08-19 went into getting here.
The summary below is by subsystem rather than by commit.

### Added

- **Complete botlib reconstruction** of Mr Elusive's Gladiator Bot botlib for
  Quake II, rebuilt against the retail module and cross-checked against his
  own Quake III Arena bot library:
  - **AAS** — map loading, clustering, reachability, routing, movement
    prediction, sound and light sampling, and the debug/draw commands.
    The original AAS file format (version 3, with version 2 accepted) is read
    and written unchanged, so existing Gladiator `.aas` files still work.
  - **AI** — long- and short-term goal selection, the movement state machines,
    weapon and item weighting, the fuzzy weight-config evaluator, character
    loading, and the chat system.
  - **Elementary actions**, the **precompiler/lexer**, and the **common**
    layer (memory, libvars, CRC, structured logging, asset resolution).
  - **Q2 bridge** translating between the Quake II game module and the botlib.
- **`bspc`** — reconstruction of the AAS compiler, covering the `map2bsp`,
  `bsp2bsp`, `map2aas` and `bsp2aas` pipelines, with golden-file tests.
- **Parity test suite** — cmocka fixtures asserting behaviour against
  `tests/reference/botlib_contract.json`, an address-anchored catalogue of the
  retail module's diagnostics and return codes.
- **Headless Quake II harness** — boots a dedicated server against the rebuilt
  module and asserts the bots actually play, not merely connect.
- **Asset packaging** — reproduces the numbered `pak` layout the original
  release shipped.
- **Reconstruction versioning** — a version for this project, kept separate
  from the legacy 0.96 botlib version. It is embedded in the module as a
  recoverable marker string, published as a Windows `VERSIONINFO` record, and
  used to name release archives.
- **Manual release workflow** — `.github/workflows/release.yml` builds win32,
  win64 and linux64 modules, packages each with the documentation, and
  publishes a GitHub release.

### Fixed

- **All 80 confirmed entries** in
  [docs/retail_divergence_backlog.md](docs/retail_divergence_backlog.md),
  recovered by a routine-by-routine audit of the 33 original translation units
  against the retail module. 13 high, 38 medium, 29 low severity; a further 28
  candidate findings were refuted on adversarial re-check and are not counted.
- Two memory bugs in the AAS entity-link path, and an intermittent crash in
  the AAS map suite.
- `INT_MIN` was used in `aas_route.c` without `<limits.h>`, which built under
  MSVC and clang-cl through transitive includes but broke the GCC/Linux build.

### Notes on deliberate divergences

Five retail behaviours are **not** reproduced, because reproducing them would
mean shipping a double free, two memory leaks, an out-of-bounds read and a
nondeterministic stale-stack read. Each is recorded in the divergence backlog
and carries a comment at the call site so a later audit does not re-open it as
a gap.

[Unreleased]: https://github.com/themuffinator/GladiatorBot-reverse/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/themuffinator/GladiatorBot-reverse/releases/tag/v1.0.0
