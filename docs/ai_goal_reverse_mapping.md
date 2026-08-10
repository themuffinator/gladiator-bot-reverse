# `ai_goal` Retail Reverse Mapping

This document records the Gladiator retail behavior recovered from
`dev_tools/gladiator.dll.bndb_hlil.txt`. The HLIL is authoritative for the
retail path. Quake III Arena `be_ai_goal.c` is useful for names and intent, but
later Q3 behavior is called out explicitly and must remain behind a
compatibility boundary.

## Primary HLIL anchors

| Address | Retail role |
| --- | --- |
| `sub_100292e0` | One-second dynamic entity-item update gate. |
| `sub_100293a0` | Per-client memory diagnostics. |
| `sub_10029480` / `sub_10029690` | Client setup and shutdown wiring. |
| `sub_100297b0` | Complete `0x11d0` client-state move and source clear. |
| `sub_10029c10` (`0x10029c41`) | Map-load call to level-item initialization. |
| `sub_10029c90` / `sub_10029da0` | AI setup/shutdown and fixed client-table allocation/free. |
| `sub_1002ed20` | Strict `iteminfo` config loader. |
| `sub_1002f100` | Item-to-fuzzy-weight index builder. |
| `sub_1002f1a0` through `sub_1002f320` | Level-item heap and active-list helpers. |
| `sub_1002f360` | Per-map level-item initialization. |
| `sub_1002f6a0` | Direct goal-number-to-display-name lookup. |
| `sub_1002f6f0` through `sub_1002f820` | Fixed avoid-goal table operations. |
| `sub_1002f890` | Public level-item goal lookup. |
| `sub_1002fa20` | Dynamic entity-item reconciliation. |
| `sub_1002fd40` through `sub_1002fe80` | Goal-stack operations. |
| `sub_1002feb0` / `sub_10030260` | LTG and NBG item selection. |
| `sub_10030600` | Goal-touching predicate. |
| `sub_10030770` | Item-goal visibility/validity predicate. |
| `sub_100308d0` / `sub_10030950` | Item-weight load/free leaves. |
| `sub_10030990` | Goal-state reset. |
| `sub_100309d0` / `sub_10030a20` | Goal-AI setup/shutdown. |

## Exact retail data layouts

Gladiator is a 32-bit DLL. These offsets describe its raw retail records; a
64-bit compatibility layer should keep its own bookkeeping outside these
logical core records.

### `itemconfig_t` and `iteminfo_t`

The item config begins with an item count and an item-array pointer. The loader
allocates one cleared block of `8 + max_iteminfo * 0x11c` bytes and points the
array at the bytes immediately following the header.

Each `iteminfo_t` is exactly `0x11c` bytes:

| Offset | Field |
| ---: | --- |
| `0x000` | `char name[80]` -- configured display name |
| `0x050` | `char classname[80]` -- token following `iteminfo` |
| `0x0a0` | `char model[80]` |
| `0x0f0` | runtime `modelindex` |
| `0x0f4` | integer `type` |
| `0x0f8` | integer inventory `index` |
| `0x0fc` | float `respawntime` |
| `0x100` | `mins[3]` |
| `0x10c` | `maxs[3]` |
| `0x118` | zero-based item-info number |

`modelindex` is not an item-config field. It is overwritten from
`IndexFromModel(iteminfo.model)` on every map load.

### `levelitem_t`

The raw level-item record is exactly `0x34` bytes:

| Offset | Field |
| ---: | --- |
| `0x00` | goal number |
| `0x04` | item-info index |
| `0x08` | physical/entity origin `vec3_t` |
| `0x14` | best-reachable area number |
| `0x18` | best-reachable goal origin `vec3_t` |
| `0x24` | linked entity number |
| `0x28` | absolute timeout; zero for a permanent item |
| `0x2c` | active-list `prev` |
| `0x30` | active/free-list `next` |

Retail does not store a classname, validity flag, base weight, next-respawn
time, Q3 item eligibility flags, or `GFL_DROPPED` bit in this record. Those are
compatibility metadata and should not change raw heap size or raw list order.

### Goal and goal-state records

`bot_goal_t` is `0x38` bytes. The retail goal state is exactly `0x3cc` bytes:

| Offset | Field |
| ---: | --- |
| `0x000` | item-weight config pointer |
| `0x004` | item-weight index pointer |
| `0x008` | eight `0x38` goal-stack slots |
| `0x1c8` | stack top/depth |
| `0x1cc` | 64 avoid-goal numbers |
| `0x2cc` | 64 avoid-goal expiry times |

There is no item-weight count, client number, last-reachability area, or
avoid-reach table in the retail goal-state leaf. Gladiator embeds this record
inside each retail client state; allocating it through a Q3-style handle table
is adapter behavior.

### Host-width boundary

The raw `itemconfig_t` (`0x08`), `iteminfo_t` (`0x11c`), `levelitem_t`
(`0x34`), `bot_goal_t` (`0x38`), goal state (`0x3cc`), and fuzzy-weight config
(`0x404`) are asserted at their retail sizes on x86. On x64, native pointers
necessarily widen in the parser/adapter-facing records. Cache filenames,
handle ownership, compatibility item flags, and other host metadata therefore
live in sidecars or adapter tables; none is appended to a raw x86 record or
used to change retail list, selection, or ownership semantics.

The client-state tables have a related but distinct host-width boundary. Their
public storage stride remains the retail `0x11d0` bytes on every build, and the
separate client-settings stride remains exactly `0x90`; the native
`bot_client_state_t` is required to fit within the leading portion of its
fixed slot. On x64, its embedded pointers are native-width adapter fields, so
tests pin the allocation sizes, fixed strides, full-slot clears, and stable
addresses rather than claiming that every internal x86 pointer offset is
unchanged.【F:src/botlib/interface/bot_state.h】【F:src/botlib/interface/bot_state.c】【F:tests/parity/test_bot_interface.c】

## Strict `LoadItemConfig` behavior

`sub_1002ed20` implements a strict, atomic load:

1. Read `max_iteminfo`, default `256`. A negative value logs
   `max_iteminfo = %d`, resets the libvar to `256`, and continues with `256`.
2. Resolve and load the requested source. Failures log the recovered
   `couldn't find %s` or `counldn't load %s` diagnostics and return null.
3. Allocate the cleared header/item block.
4. Require every top-level token to be exactly `iteminfo`. An unknown
   definition is a fatal source error; it is not skipped.
5. Require the classname following `iteminfo` to be a string token and copy it
   to offset `0x50` with the retail 80-byte limit.
6. Zero the complete destination `iteminfo_t`, then parse only `name`, `model`,
   `type`, `index`, `respawntime`, `mins`, and `maxs` with the structure
   parser's declared types. Unknown fields, malformed values, missing braces,
   or over-capacity input fail the whole load and free both source and config.
7. Preserve duplicate classnames as separate entries and assign each entry its
   declaration-order number.
8. A zero-entry config warns `no item info loaded` but still succeeds.
9. Log the loaded source path on success.

There are no implicit display-name, bounds, respawn-time, or model-index
defaults. A permissive raw scanner, case-insensitive deduplication, accepting
`TT_NAME` classnames, or accepting a `modelindex` key is non-retail behavior.

The structure parser details are observable too. `type` and `index` accept a
separate leading minus token but require an integer in the signed 16-bit range;
floats and out-of-range values are fatal. Float fields accept signed numeric
tokens. Vector fields are brace-delimited and comma-separated, but the retail
reader accepts an empty or one-/two-component vector (the cleared remainder
stays zero) and accepts the comma after a third component. End-of-file inside
an item block emits `couldn't read expected token` and fails the complete load.

The requested `itemconfig` name is resolved directly: the raw loader does not
prepend an `itemconfig/` directory. Within each normal asset root, a loose file
wins before package lookup. Package lookup probes only `pak0.pak` through
`pak9.pak`, in ascending numeric order, so the first matching entry wins;
arbitrarily named packages and `pak10.pak` or later are ignored. A loose-file
success logs the logical request, while a packaged success logs the winning
container plus logical entry.

## BSP entity list and accessor contract

`AAS_LoadBSPEntities` locates the current Quake II BSP through the shared map
discovery path, reads its entity lump, and passes that text to
`AAS_ParseBSPEntities`. The parser preserves the recovered retail linked-list
semantics:

- each parsed entity is prepended, so the returned entity list is in reverse
  textual order;
- each epair is also prepended within its entity;
- key lookup uses an exact case-sensitive comparison and returns the first
  linked epair, so the last textual occurrence of a duplicate key wins.

`AAS_VectorForBSPEpairKey` leaves the caller's vector unchanged and returns
`qfalse` when the key is absent. When the key is present, it zero-initializes
three double temporaries, attempts to scan all three components, copies all
three temporaries to the output, and returns `qtrue` without testing the scan
count. A partial value therefore supplies the parsed prefix and zeroes the
missing components; a present but wholly malformed value writes three zeroes.

## Setup, map, client, and shutdown lifecycle

The retail lifetimes are deliberately separate:

- `BotSetupGoalAI` (`sub_100309d0`) only loads the global item config from the
  `itemconfig` libvar, default `items.c`. It returns `0` on success or `0x1d`
  after logging `couldn't load item config` on failure. It does not scan map
  entities or initialize the level-item heap.
- Whole-library AI setup loads the weapon library first, then calls
  `BotSetupGoalAI`, then invokes chat setup and discards the chat result. A goal
  setup failure returns without rolling back the already loaded weapon
  library; a chat setup failure does not fail library setup. None of these
  setup calls substitutes for the later map-level item initialization. After
  the ignored chat result, setup allocates one cleared
  `maxclients * 0x11d0` client-state block and one cleared
  `maxclients * 0x90` client-settings block.
- Map load calls `sub_1002f360` at `0x10029c41`. That leaf recreates the
  map-scoped level-item heap/list, refreshes all item model indices, and scans
  the current BSP entity data. The global item config persists across maps.
- `BotShutdownGoalAI` (`sub_10030a20`) frees only the global item config and
  sets its pointer to null. Client item weights are owned by client setup and
  shutdown, not this leaf.
- The dynamic-update deadline is zero-initialized once in BSS. Map init and
  goal-AI shutdown do not reset it.

Those two client allocations remain fixed for the AI subsystem lifetime.
State slot `n` is always `state_base + n * 0x11d0`, settings slot `n` is always
`settings_base + n * 0x90`, and client creation reuses those addresses rather
than allocating individual records; setup allocation and client destruction
clear complete state slots. Retail validates client indices
through `maxclients` inclusively even though it allocates exactly `maxclients`
records. The reconstruction preserves that observable endpoint with separate,
aligned state/settings host sentinels for `client == maxclients`; it never
indexes either physical allocation one record past its end.

`BotMoveClient`/`sub_100297b0` copies the complete `0x11d0` source record over
the inactive destination and then zeros the complete source record. Embedded
client/entity identities therefore move unchanged with the record; the host
adapter only repairs destination-relative service userdata. The separate
`0x90` presentation-settings table remains indexed by host slot and is not
part of this state-record copy. Shutdown first
releases and clears every real slot plus the inclusive sentinel, then frees the
`0x90` settings base before the `0x11d0` state base, matching the recovered
retail allocation/free order.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L32577-L32595】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L32727-L32760】【F:src/botlib/interface/bot_state.c】【F:src/botlib/interface/botlib_interface.c】【F:tests/parity/test_bot_interface.c】

The public `BotLoadMap` wiring preserves that boundary and order. A null map
name returns the `AAS_LoadMap` status directly without resetting adapter
caches or rebuilding goal items. For a named map, `AAS_LoadMap` runs first and
any failure returns before adapter mutation. After success, the bridge/frame/
entity/map caches are reset, host asset tables and model indices are recorded,
all existing client states are reset for the new map, and only then
`BotInitLevelItems` rebuilds the map-scoped raw item list. Host asset-catalogue
failures are warnings and do not replace the successful retail map status.

Client setup (`sub_10029480`) loads character data, then item weights, weapon
weights, and chat data. An item-weight failure returns immediately. A later
weapon failure frees item weights; a chat failure frees item and weapon
weights. Client shutdown (`sub_10029690`) frees those owned resources and then
zeros the complete client state.

## Item-weight ownership and indexing

`sub_100308d0` calls `ReadWeightConfig(filename)` directly and immediately
stores its return value in the caller's config field. The goal leaf does not
perform an asset-path rewrite, cache lookup, preflight item-config test, or
two-phase replacement.

- Parser failure leaves the just-written config field null, logs
  `couldn't load weights`, and returns `0x1c`.
- If parsing succeeds while global itemconfig is null, the leaf returns
  `0x1c` without freeing the newly installed config and without rebuilding the
  index.
- With itemconfig present, it unconditionally overwrites the index field with
  a new `numiteminfo * 4` cleared allocation and returns `0`.
- Reloading does not free either prior pointer first; the retail leaf can leak
  replaced config/index allocations.

`sub_1002f100` looks up a fuzzy-weight index for every iteminfo classname at
offset `0x50`. Missing fuzzy weights are reported through `Log_Write` as
`item info %d "%s" has no fuzzy weight`.

`sub_10030950` frees non-null config and index pointers but does not null them.
The normal client-shutdown path subsequently clears the containing client
state. Nulling fields or making reload failure transactional is a safety
improvement, not exact retail leaf behavior.

Memory diagnostics are emitted by `sub_100293a0`, not by the weight loader. In
order, it reports character, item weights, item index, weapon weights, weapon
index, and chat-file allocation sizes.

## Level-item heap and map initialization

`sub_1002f1a0` uses `max_levelitems`, default `512`, and allocates
`count * 0x34` bytes with non-cleared `GetMemory`. Its retail loop contains an
off-by-one defect for counts of at least two: it links records `0` through
`count - 3`, making `count - 2` reachable but leaving that record's `next`
uninitialized, while it writes null only to detached record `count - 1`.
Count one works; count zero writes before the returned block.

`AllocLevelItem` only pops the free-list head and never clears it.
`FreeLevelItem` only writes the free-list `next`. Active-list removal repairs
neighbors but does not clear the removed record. A corrected, fully linked,
cleared heap is observably different from retail. The reconstruction preserves
the observable capacity defect without retaining undefined memory access: one
configured slot remains one usable slot, a count of at least two exposes a
terminated `count - 1`-slot free list, and a nonpositive count exposes no
slots.

Per-map initialization performs these operations in order:

1. Recreate the heap, clear the active head, and reset the static item count.
2. Load and parse the current BSP entity lump into the temporary prepended
   entity/epair lists described above.
3. Read `notspawnflags`, default `2048`.
4. Overwrite every iteminfo model index with `IndexFromModel(model)`, even if a
   previous or parsed value was positive. Log model-index-zero entries through
   `Log_Write`.
5. Walk the parsed BSP entities in their resulting reverse-textual list order.
   After retrieving classname, apply
   `spawnflags & notspawnflags` before item lookup or origin validation.
6. Match entity classname to iteminfo classname case-sensitively.
7. Require an origin, allocate a level item, assign the next number only after
   successful allocation, drop it to the floor, compute its best reachable
   area/origin, and insert it at the active-list head.
8. Log `found %d level items` after the complete scan.

Static heap exhaustion logs `out of level items` and immediately abandons the
map scan without the final found-count log. Unknown unfiltered entities are
written to the log as `entity %s unkown item`. `target_location` and
`info_camp` have no special retail handling here.

### Floor drop

The reused retail `AAS_DropToFloor` path traces from the entity origin to
`origin.z - 100` using item bounds, pass entity zero, and mask `3`
(`MASK_SOLID`, including windows). It fails only when `startsolid` is set.
Otherwise it copies the trace end position even when the fraction is `1`, so a
no-hit trace still moves the item down 100 units. A start-solid item is logged
but remains eligible for registration.

## Dynamic entity-item reconciliation

The retail BotAI path invokes `sub_1002fa20` only when current AAS time is
strictly greater than the persistent next-update deadline, then sets the
deadline to a fresh `AAS_Time() + 1`.

At the export boundary an uninitialized AAS world is a successful no-op,
including before inactive-client validation. With AAS initialized, `BotAI`
runs the selected client's think first and only then evaluates the global
one-second entity-item refresh gate. Goal queries do not independently trigger
that refresh.

The update leaf:

- first sweeps the active list and removes a record only when
  `timeout != 0 && AAS_Time() > timeout`; this sweep occurs before the
  item-definition availability check and before AAS entity enumeration, so an
  expired record is still removed when no live AAS entity can be returned;
- enumerates with `AAS_NextEntity`, reads `AAS_EntityModelindex` first, skips a
  zero model index, and only then calls `AAS_EntityInfo`; current and previous
  origins must be exactly equal;
- performs one active-list walk per entity. The first same-model record that
  can handle the entity wins, preserving list precedence: a nearby unlinked
  head record can bind before a later record already linked to that entity;
- updates only physical origin for an existing same-model/same-entity link;
- binds a same-model unlinked static item only when distance is strictly less
  than 20, then recomputes best-reachable area/origin;
- otherwise allocates a fresh dropped record with number
  `static_item_count + entitynum`, timeout `AAS_Time() + 30`, and inserts it at
  the list head;
- does not reject jump-pad areas.

Repeated entity model changes can leave multiple active dropped records with
the same public number. Number-based replacement or deduplication changes
retail list order and later fuzzy-RNG selection order.

## Intentional reconstruction safety boundaries

The normal retail ordering and successful-path results above are preserved,
but the reconstruction does not reproduce three undefined or leaking failure
paths:

- the level-item pool uses the bounded, terminated one-or-`count - 1` usable
  slot emulation described above instead of following an uninitialized final
  link or writing before a zero-count allocation;
- the dynamic dropped-item path checks `AllocLevelItem` and adapter results and
  returns on exhaustion instead of dereferencing a null pointer;
- map initialization frees the temporary BSP entity/epair list after both the
  raw scan and compatibility metadata pass, whereas the recovered retail leaf
  leaks that temporary list.

## LTG and NBG selection

Both selectors use the recovered current-area path: sample two units below the
bot, derive the ground-test mode from the `0x38` contents mask, call the
reachability-area helper, and reject a missing or unreachable start area. They
do not use a Q3 last-valid-area fallback.

For each raw level item, retail checks `BotAvoidGoalTime` first. Any strictly
positive remaining avoid time rejects the item directly. It does not subtract
estimated arrival time. This check occurs before area tests, fuzzy evaluation,
random-number consumption, and routing, so reordering it changes deterministic
selection results.

For an eligible item:

- evaluate `FuzzyWeightUndecided`; require a positive weight;
- require a positive travel time;
- compute `weight / (travel_time * 0.01)` using the retail double/x87
  intermediate path;
- add `20` when the level-item timeout is nonzero;
- replace the best candidate only on a strict score increase.

After choosing a raw item, add its number to the avoid table using its exact
nonzero configured respawn time, or 30 seconds when that value is exactly zero,
then push the goal. The pushed raw item goal has `GFL_ITEM`; timeout does not
add a Q3 `GFL_DROPPED` goal flag. Push overflow does not change selector
success: the avoid entry remains and the selector returns `1`.

### LTG roam fallback

If LTG selection finds no item, retail calls `AAS_RandomGoalArea` from the
current area with the caller travel flags. On success it constructs and pushes
a roam goal with:

- returned area and origin;
- mins `{-15, -15, -15}` and maxs `{15, 15, 15}`;
- entity number, goal number, and iteminfo all zero;
- flags exactly `GFL_ROAM` (`2`).

It then returns `1`. Only failure to find a random goal returns `0`.

### NBG limits and LTG detour

NBG requires `travel_time < maxtime`; equality is rejected. When an LTG is
provided, retail computes current-to-LTG travel time and, after a candidate has
already beaten the current best score, unconditionally computes
candidate-to-LTG travel time. This detour check applies to permanent and timed
dropped items alike. A candidate is rejected when its return time is greater
than the direct current-to-LTG time. Timed items are not exempt.

## Avoid-goal table

The table contains exactly 64 numbers and 64 absolute expiry times.

- Reset zeroes both arrays.
- Add uses the first slot whose expiry is strictly less than current AAS time.
- Add does not reject number zero, clamp duration, refresh an active duplicate,
  compact entries, or evict an active entry when full.
- Query returns `expiry - now` for the first matching slot whose expiry is
  greater than or equal to now; otherwise it returns zero.
- Dump includes entries whose expiry equals now and uses `Log_Write` with
  `avoid goal %s, number %d for %f seconds`, with no heading.

`BotRemoveFromAvoidGoals`, `BotSetAvoidGoalTime`, avoid-reach storage, and
negative-time respawn/minimum derivation are later Q3 compatibility APIs, not
Gladiator retail goal leaves.

## Goal stack, names, and lookup returns

Stack slot zero is a sentinel. Usable goals are slots one through seven;
`goalstacktop == 0` is empty.

- Push rejects `top >= 7`, logs `goal heap overflow`, dumps the stack, and
  otherwise increments top and copies exactly `0x38` bytes. Its observed
  machine return on success is the new depth.
- Pop decrements a positive top and returns the resulting depth at machine
  level. Popping the only goal therefore yields zero.
- Empty writes top zero.
- Top and second return direct pointers to the internal stack records or null;
  second requires depth greater than one.
- The likely original C declarations for push, pop, and empty are `void`; the
  depth/pointer values from mutating leaves are compiler artifacts unless an
  adapter intentionally exposes them.
- Stack dump walks from slot one upward to top and writes only `%d: %s` through
  `Log_Write`, with no heading or area/number fields.

`sub_1002f6a0` returns a direct pointer to the configured display name for a
goal number, or a shared empty string when no level item matches. An empty
configured display name remains empty; retail does not fall back to classname.

`sub_1002f890` walks the active list and requires both:

- `levelitem.number > index`; and
- a case-insensitive comparison between the caller string and iteminfo display
  `name` at offset zero (`sub_10045cb0`).

The caller string is not optional and an empty string is not a wildcard. On a
match, retail writes area, origin, entity number, item bounds, and goal number,
while deliberately leaving caller `flags` and `iteminfo` untouched. It returns
the matching goal number or `-1` when exhausted.

## Touching and visibility predicates

### `BotTouchingGoal`

Retail obtains the normal presence-type-4 player bounds (X/Y ±16, Z -24/+32),
forms the Minkowski-expanded goal interval, and then applies the active safety
shrink:

- lower X/Y increase by 4; lower Z is unchanged;
- upper X/Y decrease by 4; upper Z decreases by 10.

The bot origin must remain inside that adjusted interval on all three axes.
Using ±15 compatibility bounds or omitting the safety vectors is not retail.

### `BotItemGoalInVisButNotVisible`

For an item goal, retail traces a point from the eye to
`goal.origin + goal.mins` using pass entity `viewer` and mask `3`
(`MASK_SOLID`). View angles are unused.

If the trace is unobstructed:

- a nonpositive goal entity number returns `1`;
- otherwise `AAS_EntityInfo` is queried and invalid entity info returns `1`;
- valid entity info returns `0`.

An obstructed trace returns `0`. There is no half-second `lastUpdateTime`
staleness test.

### High-level nearby-goal wiring

The game-side nodes intentionally do not share one completion predicate.
Seek NBG completes an item when the bot touches it, or when the retail
item-visibility predicate says that a traced, unobstructed item is no longer a
valid visible entity. Battle NBG uses contact only. Thus an absent-visible item
can pop Seek NBG, but it must not prematurely pop Battle NBG. True item contact
still performs the shared runes-drop side effect.

Long-term item replacement uses the same touch-plus-visibility predicate as
Seek NBG. A failed LTG reselection does not empty the complete stack; lower
goals remain available beneath the expired or popped item goal.

## Return and logging contract

Stable retail status results are:

- item-config load: pointer or null;
- level-item allocation: pointer or null;
- avoid-time query: remaining time or zero;
- level-item goal lookup: goal number or `-1`;
- LTG/NBG, touching, and visibility: `1` or `0`;
- item-weight load: `0` or `0x1c`;
- goal-AI setup: `0` or `0x1d`.

Several HLIL helpers have inferred non-void return types only because the
compiler left a pointer, depth, zero, comparison flags, iterator result, or
logging return in `EAX`. Free/list mutators, resets, dumps, entity updating,
goal-state reset, and shutdown should be treated as source-level `void` unless
machine-register parity is explicitly required.

Retail log ordering that affects diagnostics includes:

- model-index warnings before the BSP entity scan;
- `notspawnflags` filtering before unknown-item or missing-origin diagnostics;
- immediate termination, with no found-count log, on static heap exhaustion;
- `Log_Write` rather than `BotLib_Print` for fuzzy-index misses, model-index
  misses, unknown items, avoid dumps, and stack dumps.

## Q3-only compatibility surface

The following are useful successor/bridge features but are not retail
Gladiator `ai_goal` leaves and must be clearly isolated from the raw path:

- separately allocated goal-state handles and handle validation;
- `BotRemoveFromAvoidGoals`, `BotSetAvoidGoalTime`, avoid-reach APIs, and their
  negative-time/default/minimum semantics;
- direct register/unregister/mark-taken item APIs, `next_respawn_time`, base
  weights, successor item eligibility bits, and public `GFL_DROPPED` goals;
- `target_location` / `info_camp` parsing and the map-location/camp-goal APIs;
- goal fuzzy-logic interbreed, mutate, and save APIs;
- high-level goal/move orchestration and external avoid-list synchronization;
- last-reachability-area fallback, same-goal vetoes, arrival-adjusted avoid
  tests, and jump-pad rejection;
- buffer-copy/boolean adapters for goal names and stack peeks in place of the
  retail direct-pointer leaves.

Gladiator's base `bot_export_t` remains the recovered 20-function table. Q3
goal APIs may be exposed through an extended in-repository table, but they must
not change base export size, retail core layouts, retail ownership, selection
order, logging, or map/setup lifetimes.

The retail library boundary follows the same rule. `BotShutdownLibrary`
finishes teardown while imports are still callable, then clears the retained
state, import block, 20-function retail export block, and the separate extended
block. A host must call `GetBotAPI` again before another setup cycle; using a
previously returned table after shutdown is intentionally invalid. The Q3
extension is rebuilt only by that fresh API acquisition and never occupies or
reorders the retail export prefix.
