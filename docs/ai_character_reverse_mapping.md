# AI Character Reverse Mapping

This note maps the reconstructed `ai_character` surface against the Gladiator HLIL and the Quake III successor implementation.

## Primary References

| Reference | Role | Reconstruction |
| --- | --- | --- |
| `sub_10029eb0` in `dev_tools/gladiator.dll.bndb_hlil.txt` | Gladiator named `character "name"` loader. Performs a two-pass parse, counts the largest characteristic index and string bytes, then fills one packed allocation. | `AI_LoadCharacterNamed`, `ai_parse_definition`, `ai_character_scan_block`, `ai_character_fill_block` in `src/botlib/ai/ai_character.c`. |
| `sub_1002a5b0` through `sub_1002a810` | Gladiator characteristic validation and accessors. Emits exact diagnostics for missing, uninitialized, and wrong-type slots. | `AI_CharacteristicAs*` plus public `Characteristic_*` wrappers in `src/botlib/ai_character/bot_character.c`. |
| `sub_10029480` setup region | Client setup loads the named profile, then item weights, weapon weights, chat file, chat name, and gender from Gladiator characteristic indices. | `BotSetupClient`, `BotState_AttachCharacter`, and profile resource fields. |
| `sub_10029690`, `sub_100297b0` | Active client shutdown and slot migration over the packed `data_100643a0` bot-state table. Shutdown frees profile-owned wiring and decrements the count; move copies the state block without changing the count. | `BotShutdownClient`, `BotState_Destroy`, `BotMoveClient`, and `BotState_Move`. |
| `sub_10029c10`, `sub_10029a40` | Post-map-load active client reset. Iterates the runtime bot-state table, clears transient goal/move/combat state, and preserves the loaded character pointer, setup block, chat state, item weights, and weapon weights. | `BotState_ResetAllForNewMap`, `BotState_ResetForNewMap`, and the `BotLoadMap` reset call after level-item setup. |
| `sub_10028ea0`, `sub_10028f30`, `sub_10028f80` | Live client presentation table helpers over `data_100643a8` for name lookup, netname access, and skin access. | `BotState_FindClientByName`, `BotState_ClientName`, `BotState_ClientSkin`. |
| `sub_10028fd0` / `data_10064388` | Active bot-client count incremented by setup, decremented by shutdown, and left unchanged by client moves. | `BotState_ActiveClientCount` with setup/shutdown updates through `BotState_SetActive`. |
| `sub_10037bb0`, `sub_10029c90`, `sub_10029da0` | Library setup reads `maxclients` into `data_10064028`, allocates bot-state and live presentation tables to that runtime size, and shutdown frees both tables. | `Botlib_CacheLibraryVariables`, `BotState_ConfigureClientCapacity`, and teardown reset of the state/presentation tables. |
| `sub_10029920`, `sub_100299d0` | Game-to-botlib live presentation setter and active setup-block setter. | `BotClientSettings`, `BotState_SetClientSettings`, and `BotSettings`. |
| `sub_10029960` | Console messages routed into the active client's chat state. | `BotConsoleMessage` and `BotQueueConsoleMessage`. |
| Q3 `be_ai_char.c` | Successor `skill N` loader, default-character fill, cache fallback, and interpolation. | `AI_LoadCharacterSkillProfileBlock`, `AI_LoadCharacterSkillProfile`, `AI_InterpolateCharacterProfiles`, and Q3 paths in `BotLoadCharacter` / `BotLoadCharacterSkill`. |

## Data Layout

Gladiator stores characteristics in one packed allocation: a count followed by fixed-size typed slots, with string values copied into trailing storage. The reconstruction preserves that shape through `ai_character_definition_t`, including the two-pass size discovery before allocation.

Q3 skill files use a fixed logical address space of 80 public characteristics. The reconstructed Q3 path keeps at least 80 slots even when a skill block only initializes a few entries, which preserves missing/default behavior and allows native Q3 indices such as `CHARACTERISTIC_WALKER` at index 48. It also preserves the retail parser quirk where index 80 can be loaded into the allocation, while public accessors, default fill, dump/free-string scans, and interpolation still operate only on indices 0 through 79.

## Index Maps

Gladiator and Q3 use different `chars.h` index assignments. The loader preserves the index map from the file being parsed; callers must use the matching constants.

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

## Wiring

Gladiator setup remains named-character first: `BotSetupClient` calls `BotLoadNamedCharacter(settings.characterfile, settings.charactername, 1.0f)`, attaches the profile to the client state, and loads the goal-state item weights from Gladiator index 28. The profile also owns weapon weights and chat state so `BotState_AttachCharacter` can expose the same resources to combat, goal, movement, and chat code. Character-owned item and weapon weights preserve the characteristic strings as `ReadWeightConfig` inputs, which keeps Q3's filename-keyed retained cache visible through setup instead of converting those paths to one-off absolute filenames. The chat state keeps the script persona from index 13 for reply-name matching and receives the active client number for chat dispatch; this stays distinct from live client presentation state. Live netname/skin data is pushed in by the game through `BotClientSettings`, while `BotSettings` updates the stored bot setup record for active clients. The live presentation table is reset during library setup/shutdown to mirror the HLIL allocation/free pair around `data_100643a8`; its recovered accessors now cover exact slot netname/skin reads and first-match netname lookup, matching the retail helpers used throughout chat and team logic. The state and presentation helpers validate against the runtime `maxclients` value cached during setup, mirroring `data_10064028` rather than treating the compile-time `MAX_CLIENTS` limit as the active table length. The active-bot counter mirrors `data_10064388`: successful setup increments it, shutdown decrements it, library teardown clears it, and `BotMoveClient` leaves it unchanged. A named `BotLoadMap` success sends active clients through the reconstructed `sub_10029c10` reset: volatile snapshots, move results, goal selections, combat metrics, and pending setup commands are cleared, while the character handle, chat state, item weights, weapon weights, setup block, and live presentation mirror stay attached for the new level. The separate retail `BotLoadMap(NULL, ...)` path only refreshes asset-index tables and preserves the live world and every client snapshot. `BotMoveClient` now follows `sub_100297b0`'s whole-record copy: its destination table slot receives the state without rebinding the embedded client/entity numbers, DM client, chat client, or state-local presentation mirror; the standalone live-presentation table remains slot-owned. The `sub_10028a70` one-shot command branch is reconstructed separately: after setup, the next EA command drain queues `gender <index 3>` and, when `altnames` is non-zero, `name <index 1>`.

The public export table now guards character operations the same way as adjacent weapon, weight, movement, and chat exports. `BotLoadCharacter`, `BotFreeCharacter`, `BotLoadCharacterSkill`, `BotFreeCharacterStrings`, and all `Characteristic_*` exports route through `BotInterface_EnsureLibraryReady` before touching cache or profile state.

The handle cache now follows the Q3 `bot_reloadcharacters` split. While the libvar is false, cache hits return the existing handle and `BotFreeCharacter` leaves cached profiles alive until `BotShutdownCharacters`. When the libvar is true, direct Gladiator and exact Q3 skill loads bypass cached handles and `BotFreeCharacter` immediately releases the handle. Fractional Q3 `BotLoadCharacter` calls retain the retail exception: the pre-interpolation `BotFindCachedCharacter(charfile, skill)` probe runs before anchor loads and ignores the reload flag, so an already cached interpolated handle can still be returned and then freed by `BotFreeCharacter` under reload mode.

Q3 skill files are detected by the first preprocessed top-level token. Gladiator `character "name"` files stay on the named-character path; missing or non-character files can still route through the Q3 skill loader when `bots/default_c.c` is available, matching successor default-character fallback. `BotLoadCharacterSkill` now mirrors Q3's cache-visible default preload: it loads or reuses `bots/default_c.c` at the requested skill with reload forced off before loading the requested file with the current `bot_reloadcharacters` setting. `BotLoadCharacter` clamps public skill requests to Q3's retail 1..5 range before choosing exact or interpolated paths, but direct `BotLoadCharacterSkill` calls keep the raw requested skill and fall through `BotLoadCachedCharacter` just like Q3. Only exact `1.0`, `4.0`, and `5.0` `BotLoadCharacter` requests bypass interpolation directly; the fractional cache probe still uses Q3's `0.01` skill tolerance and runs even when `bot_reloadcharacters` is enabled. The wrapper now owns the full `BotLoadCachedCharacter` fallback ladder: requested exact cache/load, default exact cache/load, requested any-skill cache/load, and default any-skill cache/load. `AI_LoadCharacterSkillProfileBlock` deliberately parses one block without fallback so those cache probes happen before fallback parsing, as in Q3. Requested Q3 profiles are default-filled from the cached default handle selected by that preload ladder, matching `BotDefaultCharacteristics`; this matters when a prior default skill handle is reused as the any-skill default source. The Q3 parser accepts characteristic indices through 80, but the exported Q3 accessor surface remains capped at 80 slots, matching `CheckCharacteristicIndex` and the loops in `BotDefaultCharacteristics` and `BotInterpolateCharacters`. When an exact request falls back to another skill block or to `bots/default_c.c`, the cache records the file and skill that actually loaded and later fallback requests reuse that exact loaded fallback skill. Fractional skills load the retail anchor pairs 1/4 or 4/5 and interpolate float slots, while integer and string slots copy from the lower skill, matching Q3 `BotInterpolateCharacters`; interpolated fallback handles inherit the first anchor's cache filename like Q3's `BotInterpolateCharacters`.

## Regression Coverage

`tests/ai/test_ai_character.c` covers:

- Gladiator named profile parsing and setup resource ownership.
- Character-owned item/weapon weight cache retention by loading relative weight filenames, deleting the backing files, and proving both retained configs are reused through a second `AI_LoadCharacter`.
- Direct characteristic diagnostics and conversions.
- `bot_reloadcharacters` cache retention, exact-skill reload bypass, and the Q3 fractional cache-hit exception.
- Q3 skill clamping and fallback cache identity for sparse `skill N` files.
- Q3 missing-character fallback to `bots/default_c.c` through both `BotLoadCharacterSkill` and `BotLoadCharacter`.
- Q3 default-character preloading and cache reuse before requested skill files are loaded.
- Q3 `BotDefaultCharacteristics` filling from the selected cached default handle instead of reparsing defaults.
- Q3 `MAX_CHARACTERISTICS` index-80 parser quirk while keeping public accessors/default fill/interpolation on indices 0 through 79.
- Q3 requested any-skill fallback cache reuse before reparsing sparse files.
- Q3 direct `BotLoadCharacterSkill` requests outside 1..5 falling through the retail fallback ladder without clamping.
- Q3 near-anchor interpolation and missing-character fractional fallback through the default skill anchors.
- Gladiator chat persona/client wiring, live client netname/skin synchronization and lookup through `BotClientSettings`, runtime `maxclients` table bounds, active-bot count updates, map-load active-client reset while preserving character resources, whole-record slot moves without rebinding embedded identities, bot setup mutation through `BotSettings`, plus the first-frame `gender` and optional alternate `name` EA command emission.
- Interface parity coverage pins `BotClientSettings` as the inactive-safe presentation setter and `BotSettings` as the active-client setup setter with the HLIL `BLERR_SETTINGSINACTIVECLIENT` guard.
- Export-table readiness guards for character entry points.
- Synthetic Q3 skill blocks using numeric indices.
- Real Q3 asset loading from `dev_tools/Quake-III-Arena-assets/botfiles`, including `#include "chars.h"`, default fills from `bots/default_c.c`, native Q3 indices, and interpolation.
