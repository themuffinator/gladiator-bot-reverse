# Parity Audit Report

## Executive Summary

This audit compares the AI rows currently catalogued in `docs/be_ai_parity_matrix.md`
against the original `gladiator.dll` HLIL reference. It is an AI-function
coverage report, not a measurement of whole-DLL source parity.

## Parity Statistics

Based on the detailed analysis of the parity matrix:

| Module    | Implemented | Divergent | Missing | Total | Parity % |
|-----------|-------------|-----------|---------|-------|----------|
| Goal      | 21          | 0         | 0       | 21    | 100.0%   |
| Weight    | 10          | 0         | 0       | 10    | 100.0%   |
| Move      | 12          | 0         | 0       | 12    | 100.0%   |
| Weapon    | 5           | 0         | 0       | 5     | 100.0%   |
| Character | 4           | 0         | 0       | 4     | 100.0%   |
| Chat      | 13          | 2         | 0       | 15    | 86.7%    |
| **Total** | **65**      | **2**     | **0**   | **67**| **97.0%**|

*Note: "Divergent" items are considered not fully implemented for the purpose of strict parity calculation.*

**Tracked AI-row parity: 97.0%**

The percentage above covers 67 named AI matrix rows only. It excludes the
top-level export wrappers, allocator internals, AAS implementation details,
parser internals, and other DLL code that has not yet been catalogued at the
same granularity. Those areas still contain confirmed parity gaps, so the
number must not be presented as overall `gladiator.dll` parity.

## Remediation Tasks

The previously divergent goal-module items have been closed:

1.  `BotInitLevelItems` now performs item-def loading and BSP entity-lump parsing for level items, map locations, and camp spots.
2.  `BotItemGoalInVisButNotVisible` now follows the retail stale-entity visibility gate logic.
3.  `BotGetMapLocationGoal` now resolves parsed `target_location` records with retail-style bounds.

This round also promoted the chat subsystem into the tracked matrix and closed
the global setup/shutdown gap. The chat work now covers sibling
`rnd.c`/`syn.c`/`match.c` loading, setup-time `synfile`, `rndfile`,
`matchfile`, and `rchatfile` libvars, Gladiator's single-pass random-string
constructor,
match-template registration,
reply-key parsing, captured variable substitution, reply construction, and
HLIL-backed symbol staging. Match metadata now enforces the retail
`(type, subtype);` grammar, including rejection of missing subtypes and extra
trailing tokens. The strict constructor now covers the observed byte-one escape,
length-boundary, variable, and synonym behavior. Reply selection additionally
matches the retail fixed legacy contexts, stale capture leakage, and separation
between pending generated text and inbound console messages. The inbound queue
now uses the shared retail `max_messages` linked heap, exposes the exact
non-destructive node peek and identity-removal path, and retains the older
destructive/type-based calls only as named compatibility wrappers. Its
per-client caller now runs before movement, preserves the strict recent-head
and ten-message flood boundary, removes self messages, applies recovered
synonym/match contexts and probability gates, and converts a successful reply
into a length-timed stationary stand followed by public chat dispatch. The
death-match case updates subtype and enemy death time. The downstream match
dispatcher now reconstructs every retail team-order case from 3 through 21.
Types 3/4 preserve teammate-pronoun and fuzzy-name lookup, live-entity goal
construction, near-item fallback, failure chats, stale goal-tail behavior,
visibility/message timestamps, and the distinct help/accompany deadlines and
formation reset. Type 5 resolves the defend goal and clears only the retail
defend-away timer. Types 6/7 require CTF plus both static flag goals and retain
the rush/get-flag LTG durations and case-6-only away reset. Types 8 through 10
preserve leader changes and the exact unknown-type diagnostic. Types 11 through
18 preserve addressed LTG status, subteam storage/chats, literal formation
replies, exact spacing conversion, the no-op formation command, and LTG 1/2
dismissal. Types 19 through 21 preserve camp goal branches, checkpoint storage,
and patrol-list parsing, including their retail partial-state failure effects.
Status teammate and goal mirrors retain their zero-based/-1 and zero-unset
conventions. Reply suppression is tied to `nochat`, rather than the adjacent
`teamplay` libvar.
Remaining chat gaps are architectural compatibility choices: a state-local
reply list can coexist with retail's single setup-owned global list, a stand
flag proxies the general retail AI-node graph, native pointer widths replace
x86 node pointers, and the literal-template matcher retries normalized source
text when CTF synonym canonicalization would otherwise hide `rush base`.
【F:dev_tools/gladiator.dll.bndb_hlil.txt†L30820-L30952】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L31198-L31758】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L32144-L32145】【F:src/botlib/interface/bot_interface.c】【F:tests/parity/test_bot_interface.c】

Whole-DLL auditing has additionally confirmed mismatches outside this matrix,
including export return conventions, entity/client guards, allocator and
libvar bookkeeping, import-table ownership, and map-refresh behavior. The
original import/export prefixes and `bot_input_t.actionflags` slot are now
enforced by runtime ABI tests. `GetBotAPI` now bounds its read to the exact
ten-callback retail prefix, while `GetBotAPIEx` explicitly owns the optional
tail. Named map loads register their asset tables before discovery, empty map
names follow the retail `maps\.bsp` lookup, and BSP/AAS discovery and header
failures use the recovered fatal messages and `BLERR` codes. Named BSP lump
failures now distinguish odd size, seek, and read paths; AAS lumps preserve
their fixed seek/read diagnostics and retail raw-byte acceptance of odd
structure lengths. Self-contained malformed-header/lump tests also caught and
fixed MSVC being misclassified as big-endian by the loader. Discovery now
checks the four recovered basedir/cddir plus gamedir/baseq2 loose roots, probes
`pak0.pak` through `pak9.pak` after each loose miss, and reads archive-backed
headers, lumps, collision data, and CRCs relative to the selected member's
offset and length. Physical/archive diagnostics and logical archived AAS paths
are preserved. The separate `aas0.zip` through `aas9.zip` fallback now follows
the retail loose-probe precondition, basedir/gamedir then basedir/baseq2 order,
readable-archive logging, and 32-bit `UNZIP32.DLL` callback ABI. Successful
extraction deletes the temporary member and restores the working directory;
the original early failure deliberately leaves both behind. Self-contained
tests pin the exact loose/PAK/ZIP log sequences without committing binary
fixtures. The Quake II BSP collision loader also owns and endian-fixes the
retail 0x4c texinfo records and keeps brush-side texinfo indices. Contrary to
the prior gap estimate, retail's brush-hit writer leaves the inline surface
payload zero after clearing the trace; it writes only side number, plane,
expanded distance, and contents. Synthetic valid and deliberately invalid
texinfo hits pin that boundary. These non-matrix repairs remain independent of
the AI-row percentage.

Higher-level deathmatch combat has moved beyond its earlier generic
approximation. `BotAttackMove` now preserves the retail pizza-preference and
attack-skill characteristic gates, low-skill distance band, fixed-step strafe
clock, threshold, and random direction flips. Its normal movement branch now
also preserves the characteristic-driven crouch/jump choice, crouch timer,
alternating jump latch, direct low-skill movement calls, and the high-skill
two-attempt failed-strafe flip/reset retry. The synthetic enemy-change strafe
reset and attack-chase activation remain absent because retail does not write
those states on this path. The dormant entry branch itself is now reconstructed:
it uses the strict `attackchase_time > AAS_Time()` gate before any random or
characteristic work, builds a cached enemy entity/area/origin goal with `-8`
and `+8` bounds, refreshes the movement state, and forwards the caller's travel
flags to `BotMoveToGoal`. Raw disassembly has exactly one direct access to the
retail `0xb08` deadline field, the read at `0x10022e3e`; the successor's only
prospective writer is commented out, so normal runtime state still cannot enter
the branch. A narrow test-state injector covers it without adding activation.
`BotFindEnemy` stamps sight time at acquisition even when health or shooting
widens the candidate gate, and `BotCheckAttack` uses
the per-character reaction time before issuing attack on every eligible frame,
with no synthetic 0.2-second cooldown. The adjacent view path now quantizes
angles on the retail 16-bit grid, accelerates and slows with characteristics 9
and 10 over the explicit think time, and snaps only when aim accuracy is
strictly greater than 0.8. The turn magnitude also retains the retail
float-to-integer truncation before its absolute-value comparison. Reaction time
no longer doubles as an invented projectile lead. The adjacent weapon-aware aim
path now starts its boxed shot trace at the eye plus weapon Z offset, preserves
the target `+8` and obstructed `+16` adjustments, applies characteristic-7
linear projectile lead, square-roots Rocket Launcher accuracy, and reproduces
the target jitter, Railgun direction perturbation, and vertical/horizontal
spread call order. Radial aim now follows the retail ground-target trace order
and preserves the strict shooter-distance, vertical-impact, and target-impact
boundaries. `BotCheckAttack` uses the recovered bounding-box center/bottom/top
FOV and PVS samples, medium-aware mask and direction adjustment, translucent-
fluid continuation, boxed weapon sweep, teammate and splash checks, window
follow-up, and fire-on-release latch. The PVS path retains and decodes the Quake
II BSP dvis lump, including compressed zero rows and its all-visible fallback.
Thirty focused state/action tests plus a direct dvis regression cover that
slice. The adjacent battle-inventory stage now mirrors `sub_10021020` and
`sub_10021290`: signed health, case-insensitive image-name powerup deadlines,
truncating/clamped countdowns, the strict 0.9-second power-armor window, enemy
horizontal/vertical displacement, all twelve second-skinnum-byte weapon slots,
and the three observed enemy effect flags. Tests also pin retail's deliberate
stale boundaries: timer slots 206/209, enemy slots 242-244/248, and both power-
armor slots when the armor-image stat is zero remain untouched. Ground and
water flags stay on the separate move-state initializer; neither inventory
helper writes an inferred slot for them. Self-preservation, retreat, and exact
enemy-selection integration remain incomplete.
【F:dev_tools/gladiator.dll.bndb_hlil.txt†L10934-L11035】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L28276-L28509】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L28732-L29298】【F:src/botlib/aas/aas_map.c】【F:src/botlib/ai/ai_dm.c】【F:tests/aas/test_aas_map.c】【F:tests/ai/test_ai_dm.c】

The low-level movement predictor now matches several previously observable
physics details: component-wise acceleration and velocity caps, Gladiator's
inverted ground/swim friction selector, start-solid output and red/blue debug
line ordering, AAS-only client-bbox trace dispatch, and the zeroed failure
result on the 21st clip attempt even when that final trace is clear. Collision
and step normals now come from the AAS plane indexed by the trace's
`planenum`, including the exact `normal[2] == 0.0f` step gate. Result velocity
stores the retail frame displacement directly; non-liquid exits use numeric
contents value 4, while ordinary and liquid stop paths retain a zero trace.
The public result now has the exact portable 0x50 layout and embeds only the
retail 0x24 trace prefix; the successor-only `endarea` member is gone. Stop
events are truncated at the original byte boundary, rising motion skips the
feet probe, lava maps to `SE_ENTERLAVA`, and both slime and ordinary water map
to the retail `SE_ENTERSLIME` oddity. Sixty-six focused movement tests pass in
Debug and Release. Predictor visualization now routes through the exact shared
256-line lazy handle pool. Clearing hides every nonzero handle with null
endpoints and color -1, retains those handles for reuse, and resets visibility;
pool exhaustion silently omits a 257th line. The retail 13-frame
`AAS_TestMovementPrediction` wrapper reuses that same lifecycle. An isolated
pool harness and the movement suite pass in both configurations.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L9744-L9812】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L13280-L13999】【F:src/botlib/aas/aas_debug.c】【F:src/botlib/aas/aas_move.c】【F:tests/aas/test_aas_debug_lines.c】【F:tests/ai/test_bot_move.c】

Routing now also carries the retail cache records beside the compatibility
reverse-Dijkstra cache. A fixed-width mirror pins the x86 0x2c header plus two
bytes per travel-time entry, while host-native records retain the same field
order, flags-only lookup, head insertion, and access-time refresh. Routing
initialization allocates one contiguous cluster/area head table plus the
per-global-area portal heads, and the aging pass unlinks only entries strictly
older than 15 seconds. The recovered local reverse-reachability FIFO now seeds
the goal, filters travel type and area contents, applies cluster gates and local
travel matrices, retains equal-cost routes, and preserves unsigned-short wrap.
The portal FIFO composes those local caches in stored portal-index order,
selects the opposite cluster side, seeds portal goals, and populates only newly
created records. Public queries now use the direct area cache for same-cluster
routes and the ordered portal cache at the recovered `0x1001a109` seam for
cross-cluster routes, including both portal-side adjustments, unsigned-short
wrap, and the strict `> 10` frame-update guard. The origin-aware wrapper
recovers the first hop with the strict-first-minimum outgoing scan from
`0x100310e0`; incomplete synthetic worlds retain the compatibility fallback.
Twenty-seven focused cache/query cases pass in Debug and Release.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L21298-L22006】【F:src/botlib/aas/aas_route.c】【F:tests/parity/test_aas_debug.c】

The common-library audit now pins three additional retail-only contracts.
Logging appends CRLF to both ordinary and timestamped records, flushes each
record, keeps the timestamp counter across close/reopen, and formats elapsed
fields without reducing minutes or seconds modulo 60. `Vector2Angles`
truncates pitch and yaw to whole degrees, while the public angle helpers use
the DLL's 16-bit Quake quantization grid. Finally, the structure reader and
writer retain the observed nested-error suppression, fixed-array closing-brace
quirk, and root-base nested-write behavior. The CRC helpers again expose the
retail signed `int` length boundary, including zero-iteration negative lengths,
instead of widening the internal ABI to `size_t`; `AppendPathSeperator` now
keeps the same signed capacity arithmetic. These paths have self-contained
Debug and Release regression tests.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L43738-L43812】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L44070-L44270】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L51000-L51690】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L51720-L51775】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L52539-L52555】【F:src/botlib/common/l_crc.c】【F:src/botlib/common/l_log.c】【F:src/botlib/common/l_struct.c】【F:src/botlib/common/l_utils.c】【F:tests/common/test_bot_common.c】

Elementary actions now dispatch specialized and generic commands through the
retail variadic token/argument/NULL import shape, retain the signed speed cap,
persistent view angles, jump-latch alias, and transient reset rules, and expose
the recovered live-pointer `EA_EndRegular` callback order. The production
`BotAI` path now uses that entry directly, replacing its former snapshot plus
second-stage bridge send. `EA_GetInput` remains an explicit snapshot/reset
compatibility extension for tests and non-retail callers.

Library setup now also follows the recovered non-transactional lifecycle: it
commits the setup flag before AAS/AI/EA, deliberately ignores the AAS setup
return that retail overwrites with zero, retains the flag and initialized AAS
state when later AI setup fails, and allows shutdown to clean that partial
state. The relative retail shutdown order remains AI, AAS, then EA; local
compatibility utilities and sound cleanup are layered around those calls.
