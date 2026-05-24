# Parity Audit Report

## Executive Summary

This audit compares the current source state against the original `gladiation.dll` HLIL reference as documented in `docs/be_ai_parity_matrix.md`. The goal is to quantify the current parity level and identify critical tasks to close the gap.

## Parity Statistics

Based on the detailed analysis of the parity matrix:

| Module    | Implemented | Divergent | Missing | Total | Parity % |
|-----------|-------------|-----------|---------|-------|----------|
| Goal      | 21          | 0         | 0       | 21    | 100.0%   |
| Weight    | 10          | 0         | 0       | 10    | 100.0%   |
| Move      | 12          | 0         | 0       | 12    | 100.0%   |
| Weapon    | 5           | 0         | 0       | 5     | 100.0%   |
| Character | 4           | 0         | 0       | 4     | 100.0%   |
| Chat      | 11          | 3         | 0       | 14    | 78.6%    |
| **Total** | **63**      | **3**     | **0**   | **66**| **95.5%**|

*Note: "Divergent" items are considered not fully implemented for the purpose of strict parity calculation.*

**Overall Tracked Parity: 95.5%**

## Remediation Tasks

The previously divergent goal-module items have been closed:

1.  `BotInitLevelItems` now performs item-def loading and BSP entity-lump parsing for level items, map locations, and camp spots.
2.  `BotItemGoalInVisButNotVisible` now follows the retail stale-entity visibility gate logic.
3.  `BotGetMapLocationGoal` now resolves parsed `target_location` records with retail-style bounds.

This round also promoted the chat subsystem into the tracked matrix and closed
the global setup/shutdown gap. The chat work now covers sibling
`rnd.c`/`syn.c`/`match.c` loading, setup-time `synfile`, `rndfile`,
`matchfile`, and `rchatfile` libvars, random-string expansion with repeated
constructor passes,
match-template registration,
reply-key parsing, captured variable substitution, reply construction, and
HLIL-backed symbol staging. Remaining chat gaps are stricter match parser
decomposition, the separate Q3 reply `mcontext`/`vcontext` synonym split, and
retail reply priority/time scoring.
