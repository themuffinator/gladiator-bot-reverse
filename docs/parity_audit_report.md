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
| Chat      | 15          | 1         | 0       | 16    | 93.8%    |
| **Total** | **67**      | **1**     | **0**   | **68**| **98.5%**|

*Note: "Divergent" items are considered not fully implemented for the purpose of strict parity calculation.*

**Tracked AI-row parity: 98.5%**

The percentage above covers 67 named AI matrix rows only. It excludes the
top-level export wrappers, allocator internals, AAS implementation details,
parser internals, and other DLL code that has not yet been catalogued at the
same granularity. Those areas still contain confirmed parity gaps, so the
number must not be presented as overall `gladiator.dll` parity.

## Remediation Tasks

The previously divergent goal-module items have been closed:

1.  `BotInitLevelItems` now performs item-def loading and BSP entity-lump parsing for level items, map locations, and camp spots. Its item loader now owns the retail dynamic `max_iteminfo` table (default 256), resets negative values, and rejects overflow atomically rather than accepting a truncated item config; its level-item pool also follows `max_levelitems` (default 512), its retail exhaustion diagnostic, non-clearing free-list lifecycle, and the retail head-inserted active-list order used by `BotGetLevelItemGoal`. The primary itemconfig pass now canonicalises its quoted classname/name/model values and only falls back to the raw reader after a parse failure, so it does not duplicate definitions.
2.  `BotLoadItemWeights` now creates its fuzzy-weight index table over the complete `iteminfo` config in source order, as in `sub_1002f100`; a missing config entry retains the exact index/classname warning even when that item has no level instance.
3.  `BotItemGoalInVisButNotVisible` now follows the retail stale-entity visibility gate logic.
4.  `BotGetMapLocationGoal` now resolves parsed `target_location` records with retail-style bounds.

The raw LTG/NBG selector was also re-audited against `sub_1002feb0` and
`sub_10030260`: it rejects a missing or unreachable start area without Q3's
last-area fallback, admits raw unlinked records, ignores the bridge-only
respawn marker, adds the recovered `+20` timed-item score after travel scaling,
pushes an item-only goal, preserves nonzero negative respawn values, and arms
avoidance before a possibly overflowing goal-stack push. Its start-area query
also derives ground-test mode from the two-units-below fluid mask rather than
from the active client number.

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
Reply selection now uses retail's single setup-owned global list whenever chat
setup exists; state-local reply parsing remains only for direct callers that
bypass setup. The constructor additionally preserves the raw signed-byte
arithmetic for malformed variable references when it reaches a diagnosable
out-of-range index. Remaining chat gaps are restricted to retail's
non-deterministic unchecked-memory paths: null/foreign constructor pointers,
an overlong random identifier overflowing its 0x98-byte stack buffer, and
negative or wrapped variable indices dereferenced without a lower-bound check.
Initial-chat, reply-chat, and setup-owned reply loading now also run the raw
post-parse integrity scans: escaped random references are checked against the
setup `rnd.c` tables, each missing identifier is logged once per load pass, and
invalid escape bytes retain their non-fatal diagnostic.
Native pointer widths replace
x86 node pointers, and the literal-template matcher retrying normalized source
text when CTF synonym canonicalization would otherwise hide `rush base`. The
reconstructed node scheduler now owns the stand-state transition rather than
using it as a general AI-node proxy.

Battle Chase, Retreat, and Battle NBG now share Seek LTG/NBG's recovered
nearby-goal deadline and probe clocks (`0xaec` and `0xaf8`). Their movement
failure branches instead clear the retained 20-second LTG clock at `0xae8`,
which prevents a failed Battle Chase or Retreat from cancelling the active
nearby-item lease.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L25197-L25231】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L26472-L26498】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L26632-L26657】【F:src/botlib/interface/bot_interface.c】【F:tests/parity/test_bot_interface.c】
【F:dev_tools/gladiator.dll.bndb_hlil.txt†L30820-L30952】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L31198-L31758】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L32144-L32145】【F:src/botlib/interface/bot_interface.c】【F:tests/parity/test_bot_interface.c】

Whole-DLL auditing has additionally confirmed mismatches outside this matrix,
including export return conventions, entity/client guards, allocator and
libvar bookkeeping, import-table ownership, and map-refresh behavior. The
allocator now distinguishes the two retail paths: `GetMemory` mirrors
`sub_10038f90`'s tracked non-clearing allocation, while `GetClearedMemory`
mirrors `sub_10039000` by zeroing exactly the requested payload after the
tracked header is built. The exported setup path now also opens `botlib.log` before its banner
when `log` is enabled, while core shutdown closes that same log before the
library state is cleared.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L43434-L43480】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L44091-L44156】【F:src/botlib/interface/bot_interface.c】【F:src/botlib/interface/botlib_interface.c】【F:tests/parity/test_bot_interface.c】
Client setup also preserves the retail fatal character-load diagnostic,
including its character-name/file ordering, rather than emitting an adapter
error.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L32494-L32507】【F:src/botlib/interface/bot_interface.c】【F:tests/parity/test_bot_interface.c】
The common CRC subsystem now restores the retail ordered source-checksum list:
raw parser file loads record their uncompressed bytes, and each client setup
records the loaded BSP entity lump under the current map name. The retained
list uses the DLL's 16-bit CRC, the embedded 92-value checksum suppression
catalogue, name-only duplicate suppression, and diagnostic-log dump
form.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L8881-L8884】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L43234-L43352】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L50651-L50656】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L75453-L75477】【F:src/botlib/common/l_crc.c】【F:src/botlib/precomp/l_script.c】【F:src/botlib/aas/aas_map.c】【F:tests/common/test_bot_common.c】
The AAS loader and generated-world paths now use that same retail allocator:
every retained AAS/BSP lump, the optimizer's compact geometry and index maps,
the 65,536-entry temporary reachability heap, flattened reachability data, and
the 65,536-entry cluster tables are acquired with `GetClearedMemory` and released
with `FreeMemory`. This matches `sub_1000c670`, `sub_10010c10`,
`sub_10010f60`, and `sub_100096e0`, making map-lifetime allocations visible to
the retail `showmemoryusage`/`memorydump` bookkeeping. The synthetic BSP
texinfo fixture now asserts both allocation tracking during load and exact
release on shutdown. The fixed setup-time entity table likewise now follows
`sub_1000edc0`'s `maxentities × 0x84` tracked allocation and release contract;
the host-only dynamic fallback copies through a new tracked block instead of
using `realloc`. A self-contained entity-link regression proves the fallback
returns its allocation to the tracker. The persistent sound metadata pool now
uses the raw 0xb0-byte `name[80]`/scalar/`string[80]` record layout and the
cleared path, while the raw 0x34-byte sound free-list pool uses tracked
non-clearing storage before its link fields are explicitly initialised. The
independent point-light free list uses the same 0x34-byte allocation shape,
but only if its otherwise-unconnected raw initializer is invoked. The
sound-index map is likewise now the DLL's cleared tracked
pointer table: it borrows engine-owned asset names and maps only exact
`strcmp` matches to sound-info records, rather than retaining compatibility
copies or normalized lookup strings.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L11382-L11500】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L9693-L9710】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L13042-L13054】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L14213-L14318】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L23970-L24100】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L24437-L24481】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L44261-L44281】【F:src/botlib/aas/aas_map.c】【F:src/botlib/aas/aas_cluster.c】【F:src/botlib/aas/aas_optimize.c】【F:src/botlib/aas/aas_reach.c】【F:src/botlib/aas/aas_sound.c】【F:tests/aas/test_aas_map.c】【F:tests/parity/test_aas_debug.c】
The
original import/export prefixes and `bot_input_t.actionflags` slot are now
enforced by runtime ABI tests. `GetBotAPI` now bounds its read to the exact
ten-callback retail prefix; the DLL export directory now exposes that one
retail entry only, while non-exported `GetBotAPIEx` explicitly owns the
optional tail for in-repo integration tests. Named map loads register their asset tables before discovery, empty map
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
attack-skill characteristic gates, strict 0.4 low-skill cutoff and distance band, fixed-step strafe
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
`BotFindEnemy` now follows the retail ascending, at-most-16 visible-client
scan over one-based entity numbers. It preserves the exact live-player model,
effects, frame, self, and number gates; characteristic-45 900-unit range cap;
private 16-bit view-angle FOV widening; team-mode precedence; recent-health,
300-unit, shooting-frame, candidate-facing, and retreat fallback order; and
the narrow success/failure state-write boundary. The private deathmatch view
also accumulates each client update's delta angles on that same 16-bit grid.
Damage- and shooting-assisted acquisitions stamp sight time exactly like every
other accepted candidate. `BotAI_Think` now uses the reconstructed node loop:
Stand/Seek LTG acquire before Fight. Activate remains committed to its
activation goal until it completes movement, preserves its independent deadline
after a failed move while clearing only the ordinary nearby-goal clock, then
performs the same delayed enemy scan as Seek NBG. Reached or expired activation
goals hand off to Seek-NBG before movement, and Seek NBG likewise hands missing,
reached, or expired nearby goals to Seek-LTG through the same-frame loop. Reached goals
also replay the CTF/runes tech handoff: a conflicting held tech issues `drop
tech`, including retail's anomalous tech-four Haste exception. Only a live
Seek-NBG goal acquires after its movement step, immediately before its private
view turn. Stand performs its enemy scan before honoring a pending chat's
typing wait, advances its private view turn on the no-enemy path, performs the
chat handoff strictly after its deadline while preserving the pre-handoff turn
into Seek LTG's same-frame work, and retains
that pending chat when it enters Fight. Its private non-zero `__squatt` guard
instead retains Stand and the pending chat while issuing the retail warning
say and `removebot` command. Fight retains
its node-owned enemy without scanning, except that a dead
retained enemy takes the retail kill-chat/Stand handoff; Chase only scans after
direct visibility fails. That kill path observes characteristic 19, uses
telefrag before insult/praise, waits for the constructed typing time, then
resumes Seek LTG. Its 50-transition cap and zero enemy
sentinel match the retail control boundary. Its active combat path selects a
weapon, runs Battle item use, then general item use before the attack move.
Fight does not initialize movement at node entry. Instead, BotAttackMove
refreshes the mover only after its pizza-preference and minimum attack-skill
gates, as well as in its dormant future attack-chase branch. Its movement
vector and distance use the player body origin, while the adjacent aim path
continues to use eye position. Its rocket-jump setting contributes to the
attack-chase travel mask only; Fight does not append an autonomous vertical
jump after aim and attack checking.
Battle Chase now starts and
strictly expires its independent ten-second deadline, builds and routes to the
retained reachable enemy area/origin through its eight-unit goal, zeros that
deadline on goal contact or post-move arrival in the remembered enemy area,
and runs the one-second, 500-travel-unit nearby-item
search with the retail hook/rocket-jump travel flags. Its active path now
refreshes the retained enemy inventory and runs general item use before direct
movement setup; active Battle NBG and Retreat run that general item-use pass
immediately before their own movement setup, while Retreat's no-goal idle
turn emits no general item use. It then
clears the retained LTG deadline on failure and runs the recovered
post-result `BotAIBlocked` handoff before using result movement/swim view flags
or the fixed 300-unit route lookahead,
but preserves an explicitly mover-set EA view and skips the private accelerated
turn in that case. Battle NBG now validates its
retained enemy, refreshes the reachable last-enemy area and origin, moves to
the stack's nearby goal directly, applies the strict
timeout/contact pop before returning to Fight or Retreat, clears the shared
nearby-goal deadline on failure, invokes that same blocked
handoff, and follows its result-driven aim/check-attack/tail-view sequence.
Weapon selection is now
restricted to Fight, active Retreat, and Battle NBG; its latter call follows
the post-movement weapon-frame sync and enemy-inventory refresh. Retreat now validates the
enemy, exits to a fresh Chase only through the separate strict chase predicate
(even while a get-flag LTG asks to retreat), uses its
distinct no-rocket-jump travel mask, promotes a flag carrier to LTG 5 (Rush
Base) by clearing its away clock and setting its 120-second team-goal deadline
before resolving and directly moving to the home flag, and otherwise
selects/directly moves its retained LTG,
clears the retained LTG deadline on mover failure, runs the same
blocked handoff, and preserves its movement-view versus
low-skill-lookahead versus weapon-aware-aim branch before checking attack.
Seek LTG now also runs the retail autonomous CTF selector before resolving a
team or ordinary item goal: a carrier receives the Rush Base lease, while an
unassigned aggressive bot uses the fixed 0.33/0.66 random thresholds to seek
the enemy flag, defend its own flag for 120 seconds, or defer a new choice for
60 seconds when no applicable static flag goal exists.
Seek LTG now probes and transfers a nearby item before running the general
item-use pass; activation, active Seek NBG, and retained direct LTG movers run
that pass immediately before their own movement setup, while no-goal and
successful nearby-goal handoffs do not fabricate it.
The common pmove preamble enters a reset observer node for spectator snapshots
and a reset intermission node for freeze snapshots. The latter immediately
emits a gated `end_level` chat, then, on return to normal play, schedules a
gated `start_level` chat or the retail two-second silent stand; observer exit
also re-enters Stand. A setup-time latch permits one valid-position,
characteristic-gated `enter_game` chat during the first eight seconds. On the
first dead/gib snapshot it resets the goal/move/weapon mirrors, applies the
`nochat` / `fastchat` death-chat gates, preserves the killer through the
computed typing deadline, then emits exactly one respawn action strictly after
that deadline. Its first normal snapshot re-enters Seek LTG but ends the frame
without goal movement; ordinary Seek-LTG work resumes on the following frame.
Before enemy acquisition or goal selection, Seek LTG now executes
`sub_10022470`'s random-chat handoff: help, accompany, and rush-base orders
veto it; the trial is bounded by `0.1 * thinktime`; `fastchat` bypasses the
character and 0.25 gates; and a valid chat position selects either
`random_misc` or `random_insult` before entering Stand for the message's
typing time.
Within five seconds of a recorded enemy death, it also performs the retail
per-frame probability trial and emits only wave gesture 0 or 2 before its
enemy scan; the five-second boundary itself does not wave.
Seek LTG also now routes the retail direct long-term-goal branches for help,
accompany, defend, CTF get-flag/rush-base, camp, and patrol before ordinary
item selection. Help and accompany resolve their one-based live teammate into a
reachable AAS goal, consume their delayed acknowledgements, refresh visibility,
hold at their distinct near distances, and retain the retail strict
deadline/stale-visibility clears. Get-flag selects the opposing static flag and
clears on touch or strict expiry; rush-base selects the home static flag only
while carrying one, preserves its strict expiry clear, and sets the recovered
five-to-fifteen-second post-touch away timer. The other direct branches retain
the LTG hook/rocket-jump travel mask, failed-move avoid reset, the 70-unit
defend-away timer, 40-unit camp arrival, and patrol's internal forward-bit
ping-pong transition. Accompany's close formation hold now shares the retail
crouch timer, performs its arrival acknowledgement and gestures, faces the
companion during the initial hold, and otherwise uses the recovered ten-try
safe `BotRoamGoal` idle-view candidate. Ordinary item-goal routing now probes
NBG items strictly every 0.5 seconds, enters and moves a selected Seek-NBG in
that same frame while consuming the scheduler's separate node-switch slot, uses a five-second Seek-NBG lifetime,
and restores the previous LTG on contact, missing visible static item, failure,
or expiry without an invented probe delay; direct defend orders alone use its 1500-unit travel limit rather
than the normal 700. After a direct team branch declines, the ordinary top
stack goal goes directly to `BotMoveToGoal` with the same travel mask; a failed
move clears the mover's complete avoid-reach record and leaves the BotAI tick
successful. Its view follows the retail priority of explicit mover view,
waiting-time roam glance, then 300-unit route look-ahead (with move-direction
fallback), which feeds the private accelerated turn except for an explicitly
mover-set view; no-goal and completed direct-team branches take that same
retained private turn without refreshing the move state. The normal item branch now retains LTGs for twenty seconds, uses
the complete item reach/absence/vertical-overlap predicate, and empties the
goal stack before clearing both avoid layers if every replacement remains
avoided; failed movement immediately expires that item lease. Direct blocked
movement now follows retail `BotAIBlocked`: its `BotEntityToActivate` helper
first resolves the blocked inline model and walks reverse `target` links through
up to ten `trigger_counter` / `trigger_relay` entities to find the actual
button or trigger. Shootable doors and buttons use Blaster while aiming at the
resolved brush face, and reachable activators enter the single ten-second
Activate goal. Other blocks retry perpendicularly, toggling their direction
only after the first direct move fails and expiring the active Seek-NBG/LTG
lease. Camp arrival retains retail's random idle view, crouch-time hold,
swimming reset, and hazardous-medium cancellation. Battle Chase, NBG, and
Retreat preserve a mover-set EA view without a subsequent private turn; Battle
Retreat also retains its private idle view turn when no retreat LTG is
available. The recovered scheduler now covers the catalogued high-level goal
bodies; remaining work is in whole-DLL areas that are not yet tracked at this
matrix granularity.
`BotCheckAttack` uses
the per-character reaction time before issuing attack on every eligible frame,
with no synthetic 0.2-second cooldown. The adjacent view path now quantizes
angles on the retail 16-bit grid, accelerates and slows with characteristics 9
and 10 over the explicit think time, and snaps only when aim accuracy is
strictly greater than 0.8. The turn magnitude also retains the retail
float-to-integer truncation before its absolute-value comparison. Its exact
zero-enemy sentinel selects fixed 100/150 turn parameters, while active enemies
use characteristics 9 and 10. Reaction time
no longer doubles as an invented projectile lead. The adjacent weapon-aware aim
path now starts its boxed shot trace at the eye plus weapon Z offset, preserves
the target `+8` and obstructed `+16` adjustments, applies characteristic-7
linear projectile lead, square-roots Rocket Launcher accuracy, and reproduces
the target jitter, Railgun direction perturbation, and vertical/horizontal
spread call order. Radial aim now follows the retail ground-target trace order
and preserves the strict shooter-distance, vertical-impact, and target-impact
boundaries. `BotCheckAttack` delegates the recovered bounding-box center/bottom/
top FOV/PVS samples, medium-aware mask and direction adjustment, and translucent-
fluid continuation to retail `AAS_EntityVisible`, then performs its boxed weapon
sweep, teammate and splash checks, window
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
helper writes an inferred slot for them. The adjacent item-use helpers now
mirror `sub_10021500` and `sub_100215e0`: general use independently submits
Silencer, eye-liquid-gated Rebreather, Power Shield, and Power Screen in retail
order, while battle use gives an eligible Quad the exact early-return priority
over Invulnerability. The adjacent decision helpers mirror `sub_10021650`,
`sub_100226c0`, `sub_100228c0`, `sub_10022930`, and `sub_10022990`:
flag carry returns the retail red/blue values 1/2 behind the ordered-nonzero
CTF gate; aggression retains the exact powerup, height, health, armor, weapon,
and ammunition boundaries; retreat prioritises flag carry and LTG type 4 before
the strict `< 50` test; chase uses only the strict `> 50` test; and rocket-jump
eligibility preserves the positive launcher, three-rocket minimum, no-QUAD,
invulnerability bypass, health/armor, and inclusive characteristic-26 gates.
The global `rocketjump` variable remains caller-owned. Direct tests also prove
these readers do not normalise or mutate stale inventory. Seven fixture-free
enemy-selection groups pin the candidate predicates, enumeration cap and order,
all distance/FOV boundaries, complete team precedence, shooting/facing/retreat
fallbacks, entity-to-client lookup, and exact state writes in Debug and Release.

The surrounding game-side identity paths now preserve retail's split between
the zero-based client slot at bot-state offset `+4` and the one-based AAS entity
number at `+8`. World-facing visibility, trace `passent`, attack sweeps,
movement-state initialization, same-team tests, console-death enemy matching,
and client-backed help/camp goals use the entity number; EA, chat, settings,
and input APIs retain the client slot. `BotMoveClient` now mirrors the raw
whole-record copy, so moving the backing slot preserves those embedded client
and entity values (and nested DM/chat client fields) even when the external
slot changes; `ltg_teammate` remains a zero-based internal adapter before
conversion at AAS boundaries.
【F:dev_tools/gladiator.dll.bndb_hlil.txt†L10934-L11035】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L27133-L27165】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L27171-L27189】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L28010-L28088】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L28100-L28129】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L28276-L28509】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L28732-L29298】【F:src/botlib/aas/aas_map.c】【F:src/botlib/interface/bot_interface.c】【F:tests/parity/test_bot_interface.c】【F:src/botlib/ai/ai_dm.c】【F:tests/aas/test_aas_map.c】【F:tests/ai/test_ai_dm.c】

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
compatibility utilities are layered around those calls. The raw sound teardown
helper is a no-op: AAS's final global-state clear discards sound references,
then the common tracked allocator releases their storage at library shutdown.

The sensory sound path now mirrors the retail fixed 0x34-byte heap instead of
using a frame-local FIFO. `sub_1001ce20` writes delayed start/end times from the
mapped soundinfo duration; `sub_1001cfa0` expires active sounds at `end <=
frame`, then promotes only queued sounds whose `start < frame`, replacing the
active matching entity/sound pair. Pool exhaustion emits the raw `empty sound
heap` diagnostic and retains queued records. `BotUpdateSound` now delegates its
sound-index range check to that leaf, preserving fatal `sound index ... out of
range` output and code 32 instead of a host wrapper warning; it also no longer
mutates the AAS entity sound field. Focused fixture-free coverage pins raw field
offsets, end-time ordering, equal-boundary behavior, negative-offset
replacement, and the wrapper diagnostic/status.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L24089-L24408】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L43650-L43658】【F:src/botlib/aas/aas_sound.c】【F:src/botlib/interface/bot_interface.c】【F:tests/aas/test_aas_pointlight.c】【F:tests/parity/test_bot_interface.c】

Sound setup now has the recovered no-argument retail shape: it reads
`max_aassounds` and `max_soundinfo` directly from libvars, validates their
respective `0..65536` and `0..65535` ranges, writes the raw `256` fallback on
failure, and supplies `LibVarString("soundconfig", "sounds.c")` to the
metadata loader. A zero sound-info capacity therefore remains a real,
zero-sized metadata pool and config parse rather than taking a host-only
disabled shortcut. The separate `max_aaslights` heap is recovered at
`sub_1000d340`, but has no retail `BotSetup` caller: normal library setup
therefore leaves `BotAddPointLight` on its raw empty-heap warning path. Its
explicit reconstruction-only initializer remains available for isolated heap
coverage.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L11861-L11917】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L24487-L24497】【F:src/botlib/aas/aas_sound.c】【F:src/botlib/interface/botlib_interface.c】【F:tests/aas/test_aas_pointlight.c】【F:tests/parity/test_bot_interface.c】

That metadata loader now opens `soundconfig` through the retail precompiler
and accepts only top-level `soundinfo` names. Each record is cleared/defaulted
then read through the shared generic `ReadStructure` field table, preserving
macro expansion, source diagnostics, and the raw zero-capacity `more than 0
sound infos defined` failure. Missing sources and parse failures leave the
already-created sensory pools intact, matching the raw zero-result path rather
than rolling the subsystem back.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L23970-L24080】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L51056-L51145】【F:src/botlib/aas/aas_sound.c】【F:src/botlib/common/l_struct.c】【F:tests/aas/test_aas_pointlight.c】

`BotStartFrame` now follows the retail `sub_1000e010` order. It marks entity
snapshots invalid without an invented stale-link sweep, advances sound and
light queues, continues AAS initialization, resets only the per-frame routing
cache-population counter, and consumes the one-shot `showcacheupdates`,
`showmemoryusage`, and `memorydump` libvars. The latter two emit the raw
tracked-byte/block summaries through the botlib print import. Fixture-free
coverage verifies the retained entity link, absent compatibility route/reach
passes, counter reset, output, libvar reset, and the same shared AAS clock
used by exported avoid-goal expiry queries.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L12448-L12483】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L22117-L22122】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L37497-L37535】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L44331-L44346】【F:src/botlib/aas/aas_main.c】【F:src/botlib/aas/aas_route.c】【F:src/botlib/interface/bot_interface.c】【F:tests/parity/test_bot_interface.c】
