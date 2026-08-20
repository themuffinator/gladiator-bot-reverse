# Documentation index

Documentation for the **Q2 Gladiator Bot Botlib Reconstruction**.

These documents were written over the course of the reverse-engineering effort
and they are not all the same *kind* of document. Some describe what the code
does now; some are address-level notes used while recovering a subsystem; some
are plans that were superseded. The table below says which is which, so a
reader does not mistake an old plan for current state.

## Start here

| Document | What it is |
| --- | --- |
| [../README.md](../README.md) | Project overview, build instructions, and current status |
| [project_status.md](project_status.md) | Where the reconstruction actually stands, what is verified, and every known issue |
| [game_source_integration.md](game_source_integration.md) | The other half of the mod: Mr Elusive's game source, its provenance, and the seven portability changes made to it |
| [../CHANGELOG.md](../CHANGELOG.md) | Release history |
| [reconstruction_versioning.md](reconstruction_versioning.md) | Why there are two version numbers and how releases are cut |

## Current state of the reconstruction

Accurate descriptions of the tree as it stands.

| Document | What it covers |
| --- | --- |
| [retail_divergence_backlog.md](retail_divergence_backlog.md) | The routine-by-routine audit against the retail module. All 80 confirmed entries applied; the primary record of *why* the code looks the way it does |
| [parity_audit_report.md](parity_audit_report.md) | AI-function coverage: 67 of 68 catalogued rows implemented |
| [be_ai_parity_matrix.md](be_ai_parity_matrix.md) | The per-row matrix the audit report summarises |
| [source_parity_measurement.md](source_parity_measurement.md) | A measurement snapshot. Numbers are as of the commit named inside it, not necessarily HEAD |
| [retail_function_map.md](retail_function_map.md) | Address-to-translation-unit index for the retail module |
| [botlib_ai_function_map.md](botlib_ai_function_map.md) | Address map for the AI subsystems |

## The game module

`src/game/` is Mr Elusive's own released game source rather than a
reconstruction, so this project's parity rules do not apply to it.

| Document | What it covers |
| --- | --- |
| [game_source_integration.md](game_source_integration.md) | Provenance, licensing, structure, the seven `PORT(...)` changes, build configuration, and the `p_observer.c` question |
| [`../src/game/ORIGINAL_README.txt`](../src/game/ORIGINAL_README.txt) | Mr Elusive's own readme from the source release — the authoritative statement of what it contains |

## Subsystem reference

How individual subsystems work, and where they came from.

| Document | Subsystem |
| --- | --- |
| [aas_reachability_mapping.md](aas_reachability_mapping.md) | AAS reachability generation |
| [ai_movement_state_machines.md](ai_movement_state_machines.md) | Movement state machines |
| [ai_goal_reverse_mapping.md](ai_goal_reverse_mapping.md) | Goal selection |
| [ai_character_reverse_mapping.md](ai_character_reverse_mapping.md) | Character loading |
| [bot_move_retail_parity.md](bot_move_retail_parity.md) | Movement parity notes |
| [weight_config_analysis.md](weight_config_analysis.md) | The fuzzy weight-config format |
| [asset_root_search_order.md](asset_root_search_order.md) | How the runtime resolves asset paths |
| [botlib_import_comparison.md](botlib_import_comparison.md) | Retail import table versus this reconstruction |
| [bot_debug_commands.md](bot_debug_commands.md) | Console debug commands |

## Testing

| Document | What it covers |
| --- | --- |
| [parity_testing_guide.md](parity_testing_guide.md) | Setting up and running the parity suite, and staging the assets some fixtures need |
| [testing/headless_quake2_parity_check.md](testing/headless_quake2_parity_check.md) | Booting a real Quake II dedicated server against the rebuilt module |
| [testing/quake2_source_port_testing.md](testing/quake2_source_port_testing.md) | Testing against source ports |
| [testing/bspc_cli.md](testing/bspc_cli.md) | The reconstructed AAS compiler's command line |
| [parity_plan.md](parity_plan.md) | The ordered parity work plan. Items 3–6 remain open |

## Historical and superseded

Kept for provenance. **Do not treat these as instructions for current work.**
The first three describe an intake approach built around a `src2/` staging tree
that was never created, so paths inside them do not resolve; the fourth is
simply a record of work already completed.

| Document | Status |
| --- | --- |
| [src2_intake_workflow.md](src2_intake_workflow.md) | Superseded. Describes a `src2/` staging tree that does not exist |
| [plans/ghidra-new-plan.md](plans/ghidra-new-plan.md) | Superseded. A plan for a Ghidra-based re-intake that was not taken up |
| [abi_reconciliation.md](abi_reconciliation.md) | Partly superseded. The source-priority list at the top is still the right ordering; the `src2/` inventory paths below it do not exist |
| [gladiator_bot_restored_intake.md](gladiator_bot_restored_intake.md) | How the `gladiator-bot-restored` cross-check reference was brought in |

## A note on references

Two external sources are used throughout and are cited by path:

- `dev_tools/gladiator.dll.bndb_hlil.txt` — the Binary Ninja HLIL export of the
  retail module. Line spans quoted in the divergence backlog index into this
  file.
- `ref/gladiator-bot-restored/` — an independent reconstruction, used as a
  cross-check rather than a source. Not vendored; clone it separately.

Both are reference material. Where they disagree with the HLIL, the HLIL wins.
