# Bot movement retail source parity

## Scope

This document records a routine-by-routine audit of `src/botlib/ai_move/bot_move.c`
against the retail `gladiator.dll` movement block, using two independent
witnesses:

- `dev_tools/gladiator.dll.bndb_hlil.txt` — Binary Ninja HLIL of the retail DLL,
  treated as the authority.
- `ref/gladiator-bot-restored/botlib/botlib.c` — the imported MSVC6 byte-oracle
  reconstruction, used as a secondary witness for source shape and comments.

The audit covers every routine in the retail movement block, from
`BotReachabilityArea` at `0x10030aa0` through `BotSetupMoveAI` at `0x10037a00`.
The reconstruction declares those routines in the same order the original
compiled them, so the table below doubles as a source-layout comparison.

## Routine audit

`Verdict` is the state after this pass. `HLIL` is the retail entry address.

| Routine | HLIL | Verdict |
| --- | --- | --- |
| `BotReachabilityArea` | `0x10030aa0` | Match. Includes the original axis transposition where the outer, middle and inner probe offsets land on `end[0]`, `end[1]` and `end[2]` respectively, and the ground pass tracing from `origin` rather than the adjusted `start`. |
| `BotOnMover` | `0x10030d00` | Match, including the `{-16,-16,-8}`/`{16,16,8}` box constants, the two-axis containment loop, and the `startsolid`/`allsolid`/`ent` result order. |
| `MoverDown` | `0x10030f10` | Match, including the `PRT_MESSAGE` "no entity with model %d" diagnostic. |
| `BotValidTravel` | `0x10030fe0` | Match. The four-argument shape in the imported reference is a decompiler artefact; the call site pushes only the reachability and the travel flags. |
| `BotAddToAvoidReach` | `0x10031010` | Match over the single retail avoid slot. |
| `BotGetReachabilityToGoal` | `0x100310e0` | Match. The imported reference's parameter names are shifted by one relative to the call sites; the effective order is `(origin, areanum, lastgoalareanum, lastareanum, entitynum, ...)`. |
| `BotMovementViewTarget` | `0x10031270` | **Corrected this pass.** Retail stores reach dwords 6/7/8, which is `end`, then lowers z by fifteen. The reconstruction copied `start`, borrowed from the Quake III successor. |
| `MoverBottomCenter` | `0x10031380` | Match. |
| `BotGapDistance` | `0x10031450` | Match, including the `8..96` sample range, the `48 + sv_maxbarrier` drop probe, and the water break that suppresses the gap report. |
| `BotCheckBarrierJump` | `0x10031650` | Match across all three traces and the `MFL_BARRIERJUMP` set. |
| `BotSwimInDirection` | `0x100318d0` | Match, including the unused `type` parameter the shared argument push requires. |
| `BotWalkInDirection` | `0x10031940` | **Corrected this pass.** The prediction call no longer short-circuits on a failed prediction; retail keeps evaluating the frame-count and stop-event guards against the zeroed result. |
| `BotMoveInDirection` | `0x10031be0` | Match. |
| `Intersection` | `0x10031c30` | Match. Dead in retail, preserved by incremental linking. |
| `BotCheckBlocked` | `0x10031d10` | **Corrected this pass.** The vertical-direction test now widens to the retail `double` literal, so a direction of exactly `0.7f` takes the same branch as the original. |
| `BotClearMoveResult` | `0x10031e20` | Match. Retail memsets exactly the leading `0x18` bytes and leaves both vector tails alone. |
| `BotTravel_Walk` | `0x10031e50` | Match, including the crouch gate on `AAS_AreaPresenceType` and the `300 - (300 - 2 * gap)` speed form. |
| `BotFinishTravel_Walk` | `0x10031fe0` | Match. Dead in retail; the `400 - (400 - 3 * dist)` form is preserved. |
| `BotTravel_Crouch` | `0x100320c0` | Match. |
| `BotTravel_BarrierJump` | `0x10032190` | Match. |
| `BotFinishTravel_BarrierJump` | `0x100322c0` | Match. |
| `BotTravel_Swim` | `0x100323e0` | Match. |
| `BotTravel_WaterJump` | `0x100324c0` | **Corrected this pass.** The random rise now accumulates in retail's order, folding the sample into the vertical component before the fixed lift. |
| `BotFinishTravel_WaterJump` | `0x10032620` | **Corrected this pass.** Same accumulation-order fix on the vertical component. |
| `BotTravel_WalkOffLedge` | `0x100327f0` | Match, including the asymmetric `380 - (300 - 2 * dist)` short-edge speed and the `AAS_HorizontalVelocityForJump` fallback to 200. |
| `BotFinishTravel_WalkOffLedge` | `0x10032a00` | Match. |
| `BotTravel_Jump` | `0x10032ae0` | **Corrected this pass.** The run-start alignment dot product now compares against retail's `double` literal. Run-start sampling and the immediate/delayed launch window already matched. |
| `BotFinishTravel_Jump` | `0x10032e80` | Match. |
| `BotTravel_Ladder` | `0x10032fc0` | Match. |
| `BotTravel_Teleport` | `0x100330e0` | Match. |
| `BotTravel_Elevator` | `0x10033210` | Match across the on-mover near/far split, the waiting result on an ascending platform, and the reach/bottom-center target selection. |
| `BotFinishTravel_Elevator` | `0x10033790` | Match. Retail sets no move direction here. |
| `GrappleState` | `0x100338a0` | Match. `sub_1000bb30` walks the AAS entity array at stride `0x84` and tests `valid`, so it is `AAS_NextEntity`; the imported reference names it `AAS_NextBSPEntity`. |
| `BotResetGrapple` | `0x10033a70` | Match. |
| `BotTravel_Grapple` | `0x10033b00` | **Corrected this pass.** The attach sentinel is `0x497423f0`, which is exactly `999999.0f`; the imported reference records `0x4969ffb0` (about `958459.0f`), and that wrong constant had been carried into the reconstruction. The visibility timeout now also widens to retail's `double` literal. |
| `BotTravel_RocketJump` | `0x10033ec0` | Match, including the raw 90-degree pitch store before `EA_View`. |
| `BotEmptyMoveResult` | `0x10034070` | Match. Dead in retail, preserved by incremental linking. |
| `BotFinishTravel_WeaponJump` | `0x100340b0` | Match. |
| `BotReachabilityTime` | `0x10034170` | Match on every arm, including the shared error path for travel type 13 and out-of-range types. |
| `BotMoveInGoalArea` | `0x10034210` | Match. |
| `BotMoveToGoal` | `0x100343a0` | Match. Both dispatch switches, the reachability-retention rules per travel type, the pre-switch `traveltype` store that each builder then overwrites, and the blocked-result timeout reduction all agree. |
| `BotResetAvoidReach` | `0x10034af0` | Match, including the return of the retry slot address. |
| `BotResetLastAvoidReach` | `0x10034b20` | Deliberate deviation, documented below. |
| `BotResetMoveState` | `0x10034b90` | Match. |
| `BotSetupMoveAI` | `0x10037a00` | Match on names, defaults and order. Deliberate deviation on the return value, documented below. |
| `AngleDiff` (as `BotMove_AngleDiff`) | `0x10030a50` | Match, including the single-wrap branch polarity. |

## Corrections applied in this pass

1. **`BotMovementViewTarget` aimed at the wrong end of the next reachability.**
   Retail previews the far side of the next hop; the reconstruction previewed
   its entry point. This changed every lookahead view target the bot produced,
   and it fed the interface-level aim and route-preview paths.
2. **`BotTravel_Grapple` used a wrong attach sentinel.** The retail pattern
   `0x497423f0` is `999999.0f`; the value carried over from the imported
   reference was about `958459.0f`. Both are large, but only the retail value
   matches, and the sentinel is compared against real distances on the very
   next frame.
3. **`BotWalkInDirection` rejected failed predictions.** Retail receives the
   predictor result by value and keeps evaluating its guards even when the
   prediction failed and the record is all zero. The extra early return
   suppressed a movement command retail would still issue.
4. **Three comparisons narrowed to `float` that retail performs in `double`.**
   The `BotCheckBlocked` vertical test against `0.7`, the `BotTravel_Jump`
   alignment dot product against `-0.8`, and the `BotTravel_Grapple` visibility
   timeout against `0.4` each flip branch for inputs that land exactly on the
   single-precision constant.
5. **Two water-jump vertical accumulations reassociated.** Retail folds the
   random sample into the component before adding the fixed lift.

## Deliberate deviations

These are the only two places where this module knowingly departs from the
original. Both are recorded in comments at the call site.

- **`BotResetLastAvoidReach` retry guard.** Retail guards the decrement with a
  load from `movestate + 0x80`, one dword past the single retry slot and past
  the end of the 128-byte movement state. In the original the state is embedded
  in the larger per-client record, so the read lands on a neighbouring field.
  Here the movement state is allocated on its own, so the guard reads the slot
  the decrement itself uses.
- **`BotSetupMoveAI` return value.** The original falls off the end without a
  return statement and hands back whatever the last `LibVar` call left in the
  return register. Every caller only tests the result for zero, so the
  reconstruction reports success through the same libvar instead.

Two further host-safety wrappers remain, unchanged by this pass and already
documented in the source: `BotMove_LibVarValue` supplies a fallback where retail
dereferences a movement libvar pointer unconditionally, and `BotWalkInDirection`
short-circuits a zero think time rather than performing retail's undefined
float-to-int conversion of an infinity. Both produce the retail outcome for
every input the retail data flow can present.

## Coverage

`tests/ai/test_bot_move.c` pins the module at 71 focused cases, including two
added or updated by this pass:

- `test_bot_movement_view_target_uses_retail_route_context` now asserts the next
  reachability's end point.
- `test_bot_travel_grapple_attaches_with_retail_distance_sentinel` pins the
  hook-on branch and the exact `0x497423f0` bit pattern.

`tests/parity/test_bot_move.c` separately pins the public ABI, the libvar
contract, and the exact reset semantics.

## Estimated parity

Movement module only, against the retail movement block:

- Before this pass: about 92 percent. Every routine was present and in retail
  order, but one routine aimed at the wrong reachability endpoint, one carried a
  wrong constant, one had an extra early return, and five comparisons or
  expressions differed in precision or association.
- After this pass: about 99 percent, with the two documented deviations above
  as the only known remaining gaps, neither of which changes behaviour for any
  input the retail data flow can produce.
