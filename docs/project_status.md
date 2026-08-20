# Project status

**Functionally complete, under continued testing.**

Last reviewed: 2026-08-20, at commit `f0ae4b4`.

The short version: the reconstruction works. `gladiator.dll` loads in a real
Quake II dedicated server, the bots navigate, fight and chat, and every
confirmed behavioural difference from the retail module has been resolved.

The longer version is that "works as far as we have tested it" and "is
finished" are different claims, and this page is careful about which one is
being made. Continued testing is expected to surface issues. That is the normal
condition of a reconstruction, not a sign something has gone wrong.

## What has been verified

| | |
| --- | --- |
| Retail divergence backlog | 80 of 80 confirmed entries applied ([details](retail_divergence_backlog.md)) |
| Tracked AI-row parity | 67 of 68 catalogued rows, 98.5% ([details](parity_audit_report.md)) |
| Export contract | 20 of 20 retail export wrappers covered |
| Export table of a built module | Exactly one symbol, `GetBotAPI`, matching retail |
| AAS file format | Version 3 read and written; version 2 accepted, as retail |
| Builds | win32 (clang-cl), win64 (clang-cl), linux64 (GCC 13) |
| Game module | Builds and links on all three targets, exporting only `GetGameAPI` |
| Runtime | Boots a Quake II dedicated server; bots play, not merely connect |

### Test suite

A local `ctest` run at the commit above, configured with
`-DBUILD_TESTING=ON`, no optional assets staged:

| | |
| --- | ---: |
| Registered CTest entries | 35 |
| Passed | 31 |
| Skipped (missing optional assets or environment) | 3 |
| Failed | 1 |

The three skips are `headless_quake2_parity` (needs a licensed Quake II
install), `bspc_filesystem`, and `bspc_cli_modes`. They skip by design rather
than fail, so that a developer without game assets can still run the suite.

## Known issues

### 1. `aas_map` — zip fallback extraction pass

One case fails: `test_aas_loader_runs_retail_zip_fallback_as_a_separate_extraction_pass`
(`tests/aas/test_aas_map.c:2256`). After the loose `.aas` file is removed, the
loader is expected to probe three zip archives; it probes zero. The rest of the
binary is healthy — 43 of 51 cases pass, 7 skip on missing map fixtures.

This is a pre-existing failure, confirmed by running the same case against a
clean checkout of `f0ae4b4`. It affects the zip-archive fallback path of AAS
discovery, not the normal loose-file or `pak` paths, so it is unlikely to be
hit by an ordinary install — but it is a genuine open bug.

### 2. `BotConstructChatMessage` malformed-input behaviour

The one open row in the AI parity matrix. The reconstruction does not reproduce
retail's unsafe behaviour when handed a malformed chat message.

### 3. Asset-gated coverage

Several fixtures skip without binary map assets that are not in the repository:

- `dev_tools/assets/maps/test_mover.{bsp,aas}` — mover parity
- `dev_tools/assets/test_nav.bsp` — the AAS regression harness (7 cases)

Until these are staged, the movement and AAS-loading paths they cover are
untested locally. See [parity_testing_guide.md](parity_testing_guide.md).

### 4. Deliberate divergences

Five retail behaviours are **not** reproduced, on purpose, because reproducing
them means shipping a double free, two memory leaks, an out-of-bounds read and
a nondeterministic stale-stack read. Each is recorded in the divergence backlog
with a comment at its call site. They are listed here so a later audit does not
re-open them as gaps.

### 5. Game module: observer mode is absent

`p_observer.c` is not in Mr Elusive's source release. Nothing breaks — observer
mode is gated on `OBSERVER`, which he left commented out, and every call site
is inside `#ifdef OBSERVER`, so the default configuration never compiled or
linked it. The module builds and links cleanly without it on all three targets.

The consequence is simply that observer mode is unavailable. Enabling it would
mean reconstructing the file from the retail `gamex86.dll`. See
[game_source_integration.md](game_source_integration.md).

The game module is otherwise untested at runtime by this project: it builds and
exports the right entry point, but it has not been exercised in a server the
way the botlib has.

### 6. No LICENSE file

The repository ships no `LICENSE`. This matters more now that releases carry
Mr Elusive's game source, which itself incorporates id Software, Xatrix, Rogue
and Rocket Arena 2 code. See
[game_source_integration.md](game_source_integration.md) for the provenance
chain; choosing a licence is a decision for the project owner.

### 7. Documentation drift

Some documents under `docs/` are working notes from the reverse-engineering
effort rather than descriptions of current state, and a few describe a `src2/`
staging tree that was never created. [docs/README.md](README.md) marks which is
which.

## Where coverage is thin

Being explicit about this is more useful than a single parity percentage:

- **Breadth of the parity matrix.** It catalogues 68 AI rows. The retail module
  has roughly 756 routines. The uncatalogued remainder — allocator internals,
  AAS implementation detail, parser internals — is reconstructed and exercised
  by the wider suite, but not tracked at the same granularity. The 98.5% figure
  is *of catalogued rows*, and must not be read as whole-module parity.
- **Map breadth.** The headless harness has been exercised on a small number of
  maps. Unusual geometry, large maps and unusual entity setups are where AAS
  differences tend to hide.
- **Mod and source-port breadth.** Testing has focused on the original Gladiator
  mod. Other mods and source ports are largely untried.
- **Long-running behaviour.** Leaks, cache growth and routing-table churn over
  hours of play are not covered by the current suite.

## Reporting a problem

If a bot behaves unlike the original, that is worth reporting. The
documentation is written so a report can be traced to a specific routine:

1. [retail_function_map.md](retail_function_map.md) maps retail addresses to
   translation units.
2. [retail_divergence_backlog.md](retail_divergence_backlog.md) records what was
   already found and how each entry was resolved — a new symptom may be a
   regression of a closed entry.
3. `dev_tools/gladiator.dll.bndb_hlil.txt` is the HLIL export the analysis is
   anchored to.

Include the map, the mod, the source port, and the botlib console output —
raising the `log` libvar produces `botlib.log` next to the game directory.

## Version

This page describes reconstruction version **1.0.0**. The module continues to
report `BotLib v0.96` to the host; see
[reconstruction_versioning.md](reconstruction_versioning.md) for why the two
numbers differ.
