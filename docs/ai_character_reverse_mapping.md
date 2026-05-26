# AI Character Reverse Mapping

This note maps the reconstructed `ai_character` surface against the Gladiator HLIL and the Quake III successor implementation.

## Primary References

| Reference | Role | Reconstruction |
| --- | --- | --- |
| `sub_10029eb0` in `dev_tools/gladiator.dll.bndb_hlil.txt` | Gladiator named `character "name"` loader. Performs a two-pass parse, counts the largest characteristic index and string bytes, then fills one packed allocation. | `AI_LoadCharacterNamed`, `ai_parse_definition`, `ai_character_scan_block`, `ai_character_fill_block` in `src/botlib/ai/ai_character.c`. |
| `sub_1002a5b0` through `sub_1002a810` | Gladiator characteristic validation and accessors. Emits exact diagnostics for missing, uninitialized, and wrong-type slots. | `AI_CharacteristicAs*` plus public `Characteristic_*` wrappers in `src/botlib/ai_character/bot_character.c`. |
| `sub_10029480` setup region | Client setup loads the named profile, then item weights, weapon weights, chat file, chat name, and gender from Gladiator characteristic indices. | `BotSetupClient`, `BotState_AttachCharacter`, and profile resource fields. |
| Q3 `be_ai_char.c` | Successor `skill N` loader, default-character fill, cache fallback, and interpolation. | `AI_LoadCharacterSkillProfileBlock`, `AI_LoadCharacterSkillProfile`, `AI_InterpolateCharacterProfiles`, and Q3 paths in `BotLoadCharacter` / `BotLoadCharacterSkill`. |

## Data Layout

Gladiator stores characteristics in one packed allocation: a count followed by fixed-size typed slots, with string values copied into trailing storage. The reconstruction preserves that shape through `ai_character_definition_t`, including the two-pass size discovery before allocation.

Q3 skill files use a fixed logical address space of 80 characteristics. The reconstructed Q3 path keeps at least 80 slots even when a skill block only initializes a few entries, which preserves missing/default behavior and allows native Q3 indices such as `CHARACTERISTIC_WALKER` at index 48.

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

Gladiator setup remains named-character first: `BotSetupClient` calls `BotLoadNamedCharacter(settings.characterfile, settings.charactername, 1.0f)`, attaches the profile to the client state, and loads the goal-state item weights from Gladiator index 28. The profile also owns weapon weights and chat state so `BotState_AttachCharacter` can expose the same resources to combat, goal, movement, and chat code.

The public export table now guards character operations the same way as adjacent weapon, weight, movement, and chat exports. `BotLoadCharacter`, `BotFreeCharacter`, `BotLoadCharacterSkill`, `BotFreeCharacterStrings`, and all `Characteristic_*` exports route through `BotInterface_EnsureLibraryReady` before touching cache or profile state.

The handle cache now follows the Q3 `bot_reloadcharacters` split. While the libvar is false, cache hits return the existing handle and `BotFreeCharacter` leaves cached profiles alive until `BotShutdownCharacters`. When the libvar is true, direct Gladiator and exact Q3 skill loads bypass cached handles and `BotFreeCharacter` immediately releases the handle. Fractional Q3 `BotLoadCharacter` calls retain the retail exception: the pre-interpolation `BotFindCachedCharacter(charfile, skill)` probe runs before anchor loads and ignores the reload flag, so an already cached interpolated handle can still be returned and then freed by `BotFreeCharacter` under reload mode.

Q3 skill files are detected by the first preprocessed top-level token. Gladiator `character "name"` files stay on the named-character path; missing or non-character files can still route through the Q3 skill loader when `bots/default_c.c` is available, matching successor default-character fallback. `BotLoadCharacterSkill` now mirrors Q3's cache-visible default preload: it loads or reuses `bots/default_c.c` at the requested skill with reload forced off before loading the requested file with the current `bot_reloadcharacters` setting. `BotLoadCharacter` clamps public skill requests to Q3's retail 1..5 range before choosing exact or interpolated paths, but direct `BotLoadCharacterSkill` calls keep the raw requested skill and fall through `BotLoadCachedCharacter` just like Q3. Only exact `1.0`, `4.0`, and `5.0` `BotLoadCharacter` requests bypass interpolation directly; the fractional cache probe still uses Q3's `0.01` skill tolerance and runs even when `bot_reloadcharacters` is enabled. The wrapper now owns the full `BotLoadCachedCharacter` fallback ladder: requested exact cache/load, default exact cache/load, requested any-skill cache/load, and default any-skill cache/load. `AI_LoadCharacterSkillProfileBlock` deliberately parses one block without fallback so those cache probes happen before fallback parsing, as in Q3. Requested Q3 profiles are default-filled from the cached default handle selected by that preload ladder, matching `BotDefaultCharacteristics`; this matters when a prior default skill handle is reused as the any-skill default source. When an exact request falls back to another skill block or to `bots/default_c.c`, the cache records the file and skill that actually loaded and later fallback requests reuse that exact loaded fallback skill. Fractional skills load the retail anchor pairs 1/4 or 4/5 and interpolate float slots, while integer and string slots copy from the lower skill, matching Q3 `BotInterpolateCharacters`; interpolated fallback handles inherit the first anchor's cache filename like Q3's `BotInterpolateCharacters`.

## Regression Coverage

`tests/ai/test_ai_character.c` covers:

- Gladiator named profile parsing and setup resource ownership.
- Direct characteristic diagnostics and conversions.
- `bot_reloadcharacters` cache retention, exact-skill reload bypass, and the Q3 fractional cache-hit exception.
- Q3 skill clamping and fallback cache identity for sparse `skill N` files.
- Q3 missing-character fallback to `bots/default_c.c` through both `BotLoadCharacterSkill` and `BotLoadCharacter`.
- Q3 default-character preloading and cache reuse before requested skill files are loaded.
- Q3 `BotDefaultCharacteristics` filling from the selected cached default handle instead of reparsing defaults.
- Q3 requested any-skill fallback cache reuse before reparsing sparse files.
- Q3 direct `BotLoadCharacterSkill` requests outside 1..5 falling through the retail fallback ladder without clamping.
- Q3 near-anchor interpolation and missing-character fractional fallback through the default skill anchors.
- Export-table readiness guards for character entry points.
- Synthetic Q3 skill blocks using numeric indices.
- Real Q3 asset loading from `dev_tools/Quake-III-Arena-assets/botfiles`, including `#include "chars.h"`, default fills from `bots/default_c.c`, native Q3 indices, and interpolation.
