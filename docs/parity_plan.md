# Botlib Parity Audit Plan

## Baseline audit
- Parity tests are built by default when `BUILD_TESTING=ON`. The remaining
  `cmocka_skip()` calls in `test_bot_interface.c` are fixture/setup gates for
  unavailable assets or an unusable temporary environment; they are not
  placeholder test bodies.【F:docs/parity_testing_guide.md†L26-L47】
- Several parity fixtures skip when required sample assets are missing, including mover coverage that depends on `dev_tools/assets/maps/test_mover.{bsp,aas}` and bot interface setup helpers that halt if the asset environment cannot be staged.【F:tests/README.md†L95-L113】【F:tests/parity/test_bot_interface.c†L546-L670】
- AI weight regression tests also skip when reference weight scripts are unavailable, indicating asset availability is a recurring prerequisite across parity checks.【F:tests/ai/test_ai_weight.c†L18-L36】
- The headless Quake II parity harness remains opt-in and requires external game assets and environment variables before it can validate runtime parity against a dedicated server.【F:docs/testing/headless_quake2_parity_check.md†L1-L105】

## Plan and status
The items below are ordered; "Work on the plan" means executing the next unchecked task.

1. [x] Publish the baseline audit and execution plan (this document).
2. [x] Harden parity harness prerequisites so asset-dependent skips become actionable failures.
   - Documented the minimal asset pack and environment setup in `tests/README.md` with exact paths for mover (`dev_tools/assets/maps/test_mover.{bsp,aas}`), lexer samples, weight scripts, and Quake II assets.
   - Added a `dev_tools/scripts/verify_parity_assets.sh` helper that checks presence/permissions for all required assets and exits non-zero with remediation hints.
   - Wired the verifier into CTest as a pre-step so missing assets surface as failed setup rather than silent skips. CI inherits the same check via `ctest`.
3. [ ] Make asset-gated parity fixtures reproducible and keep their assertions aligned with the HLIL contract.
   - Catalogue each skip with its required asset or setup condition; do not
     classify setup-gated tests as unimplemented behavior.
   - Prefer deterministic generated text fixtures where the retail behavior
     does not require BSP/AAS data. Keep map and mover cases gated on the
     documented binary reference assets.
   - Replace a skip only when the prerequisite can be provided portably; retain
     and document legitimate environment gates.
4. [ ] Expand subsystem parity coverage using expectations from `tests/parity/README.md` and `docs/parity_testing_guide.md`.
   - Game AI scheduler: extend the recovered Observer, Intermission,
     enter-game, battle, dead/gib, random-chat, and direct help/accompany/
     defend/CTF/camp/patrol LTG lifecycle, direct top-stack LTG movement, and
     generic Seek-LTG/NBG nearby-goal handoff, then reconstruct the remaining
     high-level goal bodies.
     Preserve the existing
     50-switch cap, node-owned enemy state, acquisition schedule, retained
     last-seen-goal routing, nearby-goal timeout, node-specific travel masks,
     direct battle result-view/aim/attack branches, and lifecycle timing.
   - Weight config guards: add tests for malformed weights, missing parameters, and boundary handling against `tests/reference/botlib_contract.json`.
   - Movement state exports: extend mover parity to cover crouch/ladder/water states and navigation flags, reusing staged `test_mover` assets.
   - Weapon state exports: add parity assertions for ammo counts, cooldowns, and weapon switching, ensuring fixtures seed predictable inventories.
   - Chat exports: validate chat event propagation and filters; include locale/encoding edge cases if supported by HLIL.
5. [ ] Keep `tests/reference/botlib_contract.json` and supporting documentation in sync with new HLIL findings before enabling stricter assertions.
   - Version the contract updates alongside test changes; require updates in PR checklist when parity behaviours change.
   - Capture rationale for each contract delta directly in the JSON (via comments where allowed) or in an adjacent changelog entry.
   - Add a CI check to diff the contract against expected schemas to prevent accidental drift.
6. [ ] Exercise the headless Quake II parity harness with staged assets, triage divergences, and integrate the run into routine validation once stable.
   - Script a repeatable harness invocation (env setup + command) under `dev_tools/scripts/run_headless_parity.sh` with documented parameters.
   - Record observed deltas between HLIL and dedicated server runs, filing follow-up tasks here with owners and target releases.
     - Owner: parity maintainers; Target: first scheduled headless run after Quake II assets are staged. Capture dedicated server logs from both rebuilt and retail modules and summarize behavioural diffs here.
     - Current block: retail vs. reconstructed comparison deferred until Quake II assets are available in the environment. Use `dev_tools/scripts/run_headless_parity.sh` to export the documented env vars and run `ctest -R headless_quake2_parity` once assets land.
   - Gate merging of major botlib changes on a green headless parity run once flakiness is resolved.
     - CI now runs the headless harness on PRs labelled `headless-parity` in addition to scheduled/workflow-dispatch invocations to enforce review-time coverage for risky botlib changes. Label major botlib PRs to require a fresh headless parity result before merging.

## Maintenance notes
- Update this plan as tasks complete or new parity gaps are discovered, keeping the ordered list accurate so follow-on work can proceed from the top.
- Track asset dependencies alongside each task to minimise future skips and keep parity runs reproducible across environments.
