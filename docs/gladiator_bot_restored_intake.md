# gladiator-bot-restored intake

## Imported reference

- Source: `https://github.com/Niehztog/gladiator-bot-restored`
- Local path: `ref/gladiator-bot-restored`
- Imported commit: `06423488d24d0d415d80c0758a1473c3b4e06a41`
- README claim at import time: 591 of 787 original Windows DLL routines byte-match the MSVC6 oracle, with no original routines left unpaired.

The local checkout is for reference intake only. It is configured as a sparse
source-only checkout that excludes the upstream binary payloads
(`assets/pak7.pak`, `tools/vendor/bspc/bspc.exe`,
`tools/vendor/bspc/winbspc.exe`, and `tools/vendor/bspc/bspc-linux-x86`) to
preserve this repository's no-new-binary rule.

## Initial comparison

The imported project is useful, but not as a raw replacement. Its reconstructed
botlib is mostly a monolithic `botlib.c` plus exact export wrappers and layout
headers. This repository has already split botlib into subsystem modules and
has substantial HLIL-backed parity fixtures, compatibility helpers, and docs.

Lightweight function-name extraction found:

| Area | Count |
| --- | ---: |
| Imported `ref/gladiator-bot-restored/botlib` C files | 4 |
| Imported parsed function definitions | 446 |
| Current `src/botlib` + `src/q2bridge` C files | 41 |
| Current parsed function definitions | 1823 |
| Exact-name overlap from the scan | 137 |

These numbers are approximate because the reference file contains decompiler
comments, calling-convention annotations, and some declarations that defeat a
simple parser. They are still strong enough to show the shape of the intake:
many routines in the reference are already represented here under normalized or
module-local names, while some reference routines are genuine candidates for
source-shape improvement.

## High-value reconstruction lanes

1. **AI scheduler and deathmatch node names**
   - Reference anchors: `AIEnter_*`, `AINode_*`, `BotDeathmatchAI`,
     `BotCheckConsoleMessages`, `BotAIBlocked`, `BotRoamGoal`,
     `BotAttackMove`, `BotFindEnemy`, `BotAimAtEnemy`, and `BotCheckAttack`.
   - Current tree already has retail-shaped logic in `src/botlib/interface`,
     but the reference preserves original node names, call order, and comments
     against exact addresses. This is the best candidate for replacing remaining
     high-level wrappers with source-shaped retail routines.

2. **Export wrapper exactness**
   - Reference anchors: `botlib_exports.c`, `Export_Bot*`, `Export_Test`, and
     `GetBotAPI`.
   - Current tree already enforces the retail import/export prefix. The
     reference still gives a compact wrapper oracle for return-code order,
     shutdown clearing, log setup, and slot assignment.

3. **AAS BSP/load/debug helper naming and edge cases**
   - Reference anchors: `AAS_LoadBSPFile`, `AAS_LoadAASFile`,
     `AAS_DumpBSPData`, `AAS_EntityBSPData`, `AAS_NextBSPEntity`,
     `AAS_BSPTraceLight`, `AAS_Show*`, `AAS_Draw*`, and index-list helpers.
   - Current AAS code is already heavily reconstructed, but the imported names
     and comments can close remaining naming gaps and confirm whether some
     compatibility helpers should be promoted to retail-shaped functions.

4. **Struct and offset audit**
   - Reference anchors: `botlib_structs.h`, `bot_state.h`, `aas_world.h`,
     `ea_state.h`, `chat_state.h`, and `struct_sizes_asserts.h`.
   - This is useful as a second layout witness for the current native structs,
     especially where this repo deliberately separates retail ABI layouts from
     host-safe side structures.

5. **Unrenamed `sub_100*` triage**
   - The scan found dozens of imported `sub_100*` routines absent by name in
     this tree. Some are likely already represented by named helpers, but the
     remaining set is worth triaging against `tests/reference/botlib_contract.json`
     and the Binary Ninja HLIL before any code is copied or renamed.

## Recommended workflow

Use the imported source as a secondary reconstruction oracle, not as the
authority. For each candidate routine:

1. Match reference address/name to the Binary Ninja HLIL and the existing
   contract JSON.
2. Locate the current subsystem implementation and tests.
3. Diff behavior at the level of guard order, diagnostics, return codes,
   struct offsets, random calls, and side effects.
4. Promote only verified source-shape improvements into `src/`.
5. Add or adjust focused parity tests before broad rewrites.

## Assessment

Yes, the import can support another meaningful reconstruction round. The best
near-term payoff is not wholesale replacement; it is a targeted pass over AI
scheduler/deathmatch glue, export wrappers, AAS naming/edge cases, and layout
assertions where the reference has address-backed source comments and this repo
still carries normalized compatibility abstractions.

Estimated parity impact of the import alone: no source parity change. It adds a
reference corpus and triage plan, but the reconstructed source remains at the
same parity level until verified changes are promoted into `src/`.
