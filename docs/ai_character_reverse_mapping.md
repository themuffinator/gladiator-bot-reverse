# AI Character Reverse Mapping

This note maps the reconstructed character subsystem against Gladiator's packed
named-character ABI and the Quake III successor's skill/profile handle API.

## Primary References

| Reference | Retail behavior | Reconstruction |
| --- | --- | --- |
| Gladiator `sub_10029eb0` | Opens the source twice, scans every top-level definition, accumulates the highest characteristic index and string bytes, allocates one packed block, then fills it on the second pass.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L32810-L33175】 | `AI_LoadCharacterDefinition`, `AI_LoadCharacterNamed`, and the scan/fill helpers in `src/botlib/ai/ai_character.c`. |
| Gladiator `sub_1002a590` and `sub_1002a5b0` through `sub_1002a810` | Frees the single allocation and validates the packed header before typed access, preserving the recovered missing, uninitialized, wrong-type, and invalid-bound diagnostics.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L33190-L33312】 | Direct `BotFreeCharacter` and pointer-based `Characteristic_*` functions in `src/botlib/ai_character/bot_character.c`. |
| Gladiator `sub_10029480` | Loads the named definition, copies settings only after that succeeds, then loads item weights, weapon weights, chat file/name, and gender from indices 28, 5, 12, 13, and 3.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L32494-L32540】 | `BotSetupClient`, `BotState_AttachCharacter`, the per-client goal/weapon owners, and the per-client chat state. The chat loader uses retail error-code semantics (`0` success, `BLERR_CANNOTLOADICHAT` failure) and always emits its final fatal load diagnostic after an inner load failure. |
| Gladiator `sub_10029690` | Tears down chat, weapon weights, item weights, and the packed character in that order before clearing the state slot.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L32548-L32570】 | `BotState_FreeResources` in `src/botlib/interface/bot_state.c`. |
| Gladiator `sub_100297b0`, `sub_10029a40`, and `sub_10029c10` | Moves the complete state record without changing the active count, and resets transient map state while preserving setup-owned client resources.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L32577-L32595】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L32661-L32720】 | `BotState_Move`, `BotState_ResetForNewMap`, and `BotState_ResetAllForNewMap`. |
| Q3 `be_ai_char.c` | Adds `skill N` blocks, an 80-characteristic public surface, default filling, cache lookup, reload behavior, and interpolation.【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_char.c†L129-L610】【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_char.c†L778-L789】 | Explicit `*Handle` entry points and metadata profiles in `src/botlib/ai_character/bot_character.c`. |

## Retail Parser and Packed Layout

The named parser follows the retail two-pass contract. Top-level token text is
compared with `"character"` without adding a `TT_NAME` restriction. Nonmatching
character blocks are skipped with balanced braces, matching blocks can be split
across the file and are merged, and both passes continue to EOF after a match.
Consequently, a duplicate index in two matching blocks fails during the fill
pass, and an unknown trailing top-level definition still fails even after a
valid match. A matching block that reaches clean EOF without a closing `}` is
accepted after `PC_ExpectAnyToken` emits `couldn't read expected token` once in
each pass; EOF while reading the required value for an index still fails.
Malformed-definition diagnostics use `SourceError`, preserving the retail file
and line prefix. These edges follow the retail token loop and its EOF exits.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L32842-L33146】【F:src/botlib/ai/ai_character.c】【F:tests/ai/test_ai_character.c】

The first pass records the highest index, not one-past-the-highest. Retail then
allocates enough room for that index and writes the highest index into the
public header, while the accessor rejects `index >= header`. The reconstruction
therefore allocates the highest slot safely but deliberately exposes only
`0 .. highest-1`; the highest slot is the retail hidden sentinel. Strings live
in trailing storage inside the same allocation. Negative indices are rejected
safely and the reconstruction does not impose the former artificial positive
1024 limit; only unrepresentable allocation/index arithmetic is rejected.
【F:dev_tools/gladiator.dll.bndb_hlil.txt†L33050-L33160】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L33197-L33207】【F:src/botlib/ai/ai_character.c】

On x86 the returned allocation is exactly the four-byte highest-index header,
`highest + 1` eight-byte `{ type, value }` slots beginning at offset four, and
the NUL-terminated string bytes immediately after the final slot. Native x64
builds preserve that order but adapt to pointer width/alignment: the slot table
begins at offset eight and each pointer-bearing slot is 16 bytes. Internal slot
count, string-byte count, skill, and identifier metadata live in a private
sidecar keyed by the definition; none of that metadata precedes or follows the
retail payload. Sidecars use host `calloc`/`free`, so bot-memory block and byte
counters observe exactly one tracked allocation per packed character.
`AI_FreeCharacterDefinition` unlinks the sidecar before freeing the definition,
and `AI_ShutdownCharacterDefinitions` drains any definition/sidecar pairs
orphaned by retail-compatible parser/setup failures before the bot-memory arena
is destroyed, leaving the sidecar list reset. In particular, retail allocates
after pass one and deliberately returns `NULL` without freeing that block when
the second source open or any pass-two parse fails. The reconstruction preserves
that leak during ordinary execution; the sidecar shutdown drain makes it safe
at subsystem teardown without changing the observed failure path.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L33050-L33175】【F:src/botlib/ai/ai_character.c】【F:src/botlib/interface/botlib_interface.c】【F:tests/ai/test_ai_character.c】

A successful loose-file load reports the caller's requested character and
logical filename as `loaded <name> from <filename>`; it deliberately does not
leak the resolved physical loose path. A non-empty PAK entry instead reports
`loaded <name> from <pak-path>\<logical-filename>`, preserving both container
and logical-entry provenance. Asset lookup follows the retail numbered-container
contract: only `pak0.pak` through `pak9.pak` are considered, in ascending numeric
order; arbitrary names and `pak10.pak` are ignored. These are success diagnostics
in addition to the retail parser/accessor errors.【F:src/botlib/ai/ai_character.c】【F:src/botlib/common/l_assets.c】【F:tests/ai/test_ai_character.c】【F:tests/parity/test_precompiler_lexer.c】

The raw loader first pins the logical filename through the retail 0x104-byte
local. Empty filenames therefore reach resolution and emit `couldn't find `;
overlong inputs resolve and log through the first 259 characters. The native
reconstruction guarantees the final NUL instead of reproducing retail's
unterminated-input fault surface. NULL filename/name and foreign-pointer guards
are likewise explicit host-safety adaptations.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L32810-L32833】【F:src/botlib/ai/ai_character.c】【F:tests/ai/test_ai_character.c】

Q3 profiles retain 80 public characteristics, indices 0 through 79. The
allocation still has room for parser index 80, preserving the successor's
index-80 quirk, but access, default fill, string invalidation, and interpolation
do not expose that sentinel.【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_char.c†L180-L610】【F:src/botlib/ai/ai_character.c】

## API and Ownership Split

The unsuffixed internal ABI is the Gladiator ABI:

- `BotLoadCharacter(file, name)` returns a fresh opaque `bot_character_t *`
  pointing at the one packed definition allocation.
- `BotFreeCharacter(character)` unlinks/frees its private sidecar and frees the
  one bot-memory-tracked definition; it owns no setup resources.
- `Characteristic_Float`, `Characteristic_BFloat`,
  `Characteristic_Integer`, and `Characteristic_BInteger` take the pointer;
  `Characteristic_String` returns the mutable raw in-allocation `char *`, not a
  copy or a const facade.

The successor compatibility ABI is explicit:

- `BotLoadCharacterHandle`, `BotLoadNamedCharacterHandle`, and
  `BotLoadCharacterSkillHandle` return 1-based handles.
- `BotFreeCharacterHandle`, `Characteristic_*Handle`, and
  `BotShutdownCharacterHandles` operate on the handle table.
- `Characteristic_StringHandle` copies into the caller's buffer.
- `BotFreeCharacterStringsHandle` receives the resolved metadata profile,
  invalidates its public string slots, and leaves the profile, packed
  allocation, numeric slots, and handle alive.

`AI_LoadCharacterNamed` and the named path through `AI_LoadCharacter` are
definition-only metadata wrappers: they do not load or own item weights, weapon
weights, or chat state. `AI_FreeCharacter` frees only the wrapper and packed
definition/sidecar pair. The bridge's Q3-shaped export fields retain their
historical names, but their guarded wrappers call the explicit handle family;
production retail client code calls the direct pointer family.【F:src/botlib/ai/ai_character.c】【F:src/botlib/ai_character/bot_character.c】【F:src/botlib/ai_character/bot_character.h】【F:src/botlib/interface/bot_interface.c】

Float-to-integer access reproduces the retail x87 `__ftol` path rather than a
32-bit saturating cast: truncate to a signed 64-bit result, return its low 32
bits, and therefore wrap values outside the signed-32 range. NaN and values
outside the signed-64 conversion range model masked x87 integer-indefinite,
whose low 32 bits are zero. Bounded integer access clamps only after this
conversion.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L33246-L33312】【F:src/botlib/ai/ai_character.c】【F:tests/ai/test_ai_character.c】

Parsed numeric slots consume the precompiler token caches directly, as retail
does: floats use `token.floatvalue`, retaining its bit-level rounding, while
integers keep the token's low 32 bits (`0xffffffff` becomes `-1` and
`4294967296` becomes `0`). The separate index guard still rejects negative or
unrepresentable allocations instead of reproducing retail memory corruption.
`Characteristic_BFloat` also mirrors x87 unordered comparisons: a NaN value or
minimum returns the minimum, while a NaN maximum leaves an otherwise in-range
value unchanged.【F:dev_tools/gladiator.dll.bndb_hlil.txt†L33050-L33136】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L33238-L33260】【F:src/botlib/ai/ai_character.c】【F:src/botlib/ai_character/bot_character.c】【F:tests/ai/test_ai_character.c】

## Cache and Skill Semantics

Retail pointer loads never enter the handle cache: each setup receives a fresh
packed definition and `BotFreeCharacter` always releases it. Named Gladiator
handle loads are also fresh entries and are released immediately by
`BotFreeCharacterHandle`, independent of `bot_reloadcharacters`.

Q3 skill handles retain the successor policy. With
`bot_reloadcharacters == 0`, exact file/skill cache hits within the strict
`0.01` tolerance reuse the existing handle and free calls retain Q3 profiles
until `BotShutdownCharacterHandles`. Reload mode bypasses ordinary exact-cache
reuse and frees Q3 handles immediately. The fractional public load preserves
Q3's earlier cache probe even in reload mode, so an existing interpolated
handle can still be returned and then released. The table has
`MAX_CLIENTS + 1` slots, with valid public handles 1 through `MAX_CLIENTS`.
【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_char.c†L129-L173】【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_char.c†L541-L610】【F:src/botlib/ai_character/bot_character.c】

The handle facade preserves the successor's observable edge behavior: it
distinguishes an out-of-range handle from an empty slot with the exact fatal
diagnostic, validates the handle before bounded-value arguments, and leaves a
string destination unchanged on failure. Default-character preload is
unconditional, including reload mode and requests for the default file itself;
an explicit empty filename proceeds through that fallback ladder. Cache-hit and
fresh-load success messages retain the four retail branches and their original
filename/skill arguments rather than adding a generic low-level success line.
【F:src/botlib/ai_character/bot_character.c】【F:tests/ai/test_ai_character.c】

`BotLoadCharacterHandle` clamps public Q3 skills to 1..5, uses exact anchors
1, 4, and 5 directly, and interpolates fractional requests between 1/4 or 4/5.
`BotLoadCharacterSkillHandle` keeps its requested skill unclamped. The Q3
fallback ladder remains requested exact, default exact, requested any-skill,
then default any-skill, with `bots/default_c.c` preloaded through the retained
cache and used to fill missing characteristics. Float slots interpolate;
integer and string slots come from the lower anchor.【F:dev_tools/Quake-III-Arena-master/code/botlib/be_ai_char.c†L180-L610】【F:src/botlib/ai_character/bot_character.c】

## Client Wiring and Lifetime

`BotSetupClient` now follows the retail ownership sequence:

1. Load and attach a fresh packed definition with
   `BotLoadCharacter(settings.characterfile, settings.charactername)`.
2. Copy the setup block only after the character load succeeds.
3. Reuse an existing goal handle after a partial failure, or allocate one, then
   call the canonical goal-state item loader on characteristic 28. Its core
   uses `ReadWeightConfigUncached`; the goal handle owns the distinct config.
4. Reuse an existing weapon state, or allocate one, then load distinct fresh
   weapon weights from characteristic 5; the weapon state owns them.
5. Reuse an existing chat state, or allocate one, load characteristics 12 and
   13, and apply gender from characteristic 3. Retail setup does not call
   `BotSetChatName`: the compatibility name/client metadata remains empty until
   an explicit API call. `nochat` does not skip this per-client file load; it
   gates later chat construction/events.
6. Allocate the successor-only transient goal, movement, and DM states.

The bot state keeps borrowed convenience pointers to the goal-owned item config
and weapon-state-owned weapon weights; the packed definition owns neither.
Two clients using the same file/name therefore receive distinct character,
item-weight, weapon-weight, and chat allocations. Setup performs no blanket
reset at entry and normal loader failures do not centrally destroy the partial
state. An item-load failure retains the inactive state, new definition, and
goal handle. A weapon-load failure also retains the weapon-state handle after
freeing that attempt's item weights; a chat-load failure retains the chat object
and both owner handles after freeing that attempt's item and weapon weights.
Later attempts reuse those handles/objects and overwrite the state character
pointer without freeing a definition abandoned by the prior failed attempt;
the definition shutdown drain reclaims those orphans. Structural allocation or
owner validation failures still take the explicit destroy path.

Missing or empty item/weapon characteristics are passed to their raw loaders as
the empty path. They emit the accessor and underlying loader diagnostics and
fail without substituting `bots/babe_i.c`, `items.c`, or
`default/defaul_w.c`; setup adds no extra normal-path item/weapon failure log.
【F:dev_tools/gladiator.dll.bndb_hlil.txt†L32481-L32540】【F:src/botlib/interface/bot_interface.c】【F:src/botlib/interface/bot_state.c】【F:tests/ai/test_ai_character.c】

`BotShutdownClient` runs the `exit_game` construction path and dispatches any
allowed chat before an owned resource is torn down. Teardown then releases chat,
the weapon state and its weights, the goal handle and its item config, and the
retail character and console waypoints, in that order; only then does it destroy
successor transient goal/move/DM state. Lifecycle variables use retail
`EasyClientName` normalization in a 32-byte buffer; an empty presentation name
stays empty and never falls back to the chat persona.
Map reset frees waypoint chains, clears the complete record, restores only
identity/settings and owner fields, then invokes the retail move → goal → weapon
→ goal-avoid → move-avoid reset sequence before host-only adapter resets.
Whole-record client moves preserve ownership and active count but deliberately
retain `state->client_number`; shutting down a moved table slot therefore
dispatches its exit chat through the original client. Waypoint nodes use the
tracked allocator and retail flexible layout—0x44 fixed bytes plus the trailing
NUL-terminated name on x86, with native pointer-width adaptation on x64. The
one-shot setup command path still emits `gender <index 3>` and, when `altnames`
is enabled, `name <index 1>`.
【F:dev_tools/gladiator.dll.bndb_hlil.txt†L32548-L32595】【F:dev_tools/gladiator.dll.bndb_hlil.txt†L32661-L32720】【F:src/botlib/interface/bot_interface.c】【F:src/botlib/interface/bot_state.c】

## Characteristic Index Map

| Meaning | Gladiator index | Q3 index |
| --- | ---: | ---: |
| Name | 0 | 0 |
| Gender | 3 | 1 |
| Attack skill | 4 | 2 |
| Weapon weights | 5 | 3 |
| Aim skill | 7 | 16 |
| Chat file | 12 | 21 |
| Chat name | 13 | 22 |
| Chat CPM | 14 | 23 |
| Item weights | 28 | 40 |
| Aggression | 32 | 41 |

## Regression Coverage

`tests/ai/test_ai_character.c` pins:

- definition-only named profiles, exact packed x86/native layout, one-block
  bot-memory accounting, sidecar cleanup, mutable raw string pointers, x87
  low-dword integer conversion, bounds, and diagnostics;
- exact loose logical-path and PAK container/entry success diagnostics;
- matching-block merging, duplicate rejection, unknown trailing definitions,
  long names, clean block EOF, and failure to find an exact character name;
- fresh named-handle entries and handle string-copy behavior;
- Q3 index 80 allocation with an 80-slot public surface, exact-cache tolerance,
  reload behavior, string-only invalidation, default filling/fallback identity,
  clamping, unclamped skill loads, and interpolation using synthetic and real
  Q3 assets;
- guarded Q3 export wrappers;
- setup order and definition/resource ownership, raw missing-weight failures
  without fallback, retained partial owners and retry reuse, isolation between
  two clients, pre-teardown exit chat, teardown independence, map-reset
  preservation, live presentation data, active-count and retained-client
  whole-record move behavior, and one-shot setup commands.

These assertions cover the retail/Q3 split directly rather than treating
weight or chat resources as character-profile fields.【F:tests/ai/test_ai_character.c】
