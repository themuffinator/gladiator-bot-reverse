# Game source integration

The Gladiator Bot is two binaries. This repository now builds both.

| Module | What it is | Where it comes from |
| --- | --- | --- |
| `gladiator.dll` / `gladx86_64.so` | The botlib — the bot AI | **Reconstructed** from the retail binary by this project |
| `gamex86.dll` / `gamex86_64.so` | The game module — Quake II mod logic | **Mr Elusive's own source**, released publicly |

They are separate DLLs that meet only at one boundary: the game module locates
the botlib at runtime and calls `GetBotAPI`. Neither shares a header with the
other — each carries its own `q_shared.h`, `game.h` and `botlib.h`, exactly as
the two shipped in 1999.

## Provenance

`src/game/` is `gladq2096gamesrc.zip` (v0.96, dated 1999-08-02) from
[Mr Elusive's project page](https://mrelusive.com/oldprojects/gladiator/), the
same archive vendored read-only at `dev_tools/game_source/`. His own
`readme.txt` is preserved in the tree as
[`src/game/ORIGINAL_README.txt`](../src/game/ORIGINAL_README.txt); it is the
authoritative statement of what the source contains, and it records that the
release incorporates:

1. Quake II v3.20 game source — **id Software**
2. Mission Pack 1, *The Reckoning* — **Xatrix**
3. Mission Pack 2, *Ground Zero* — **Rogue**
4. Quake II CTF game source — **id Software / Zoid**
5. Parts of the Rocket Arena 2 bot support routines — **David Wright**
6. The Gladiator Bot game code — **Mr Elusive**

Everything in `src/game/` is therefore third-party work being *preserved*, not
reconstructed. It is not covered by this project's parity rules; it is
untouched apart from the portability changes listed below.

> **Licensing.** id Software released the Quake II game source under the GPLv2
> in 2001, which covers items 1–4. Items 5 and 6 have no separate published
> licence. This repository has no `LICENSE` file yet, and adding one is a
> decision for the project owner rather than something to infer — see
> [project_status.md](project_status.md).

## Why `src/game/`, not a top-level `game/`

The repository already keeps every shipped module under `src/`
(`src/botlib/`, `src/q2bridge/`, `src/shared/`). The game module is a shipped
module, so it belongs there too. The root `CMakeLists.txt` source audit was
widened to cover it: every `.c` under `src/` must belong to exactly one of the
`gladiator`, `q2bridge` or `game` targets, so a file cannot be added and
silently left out of the build.

Building it is optional — `-DBUILD_GAME_MODULE=OFF` skips it, and the audit
excludes `src/game/` in that case rather than reporting 82 orphaned files.

## Portability changes

The source is 1999 C compiled by MSVC6. Eight changes make it build and run
under current GCC and Clang for 32- and 64-bit targets. Every one carries a
`PORT(...)` comment at the site naming what it fixes, so they are greppable:

```bash
grep -rn "PORT(" src/game/
```

That returns **eleven** markers across seven files for the eight changes: the
`CTFDrop_Flag` fix touches three sites (its definition, its `return` statement,
and its declaration in the header) and the 64-bit botlib name touches two, so
no site looks unexplained on its own.

| File | Change | Why |
| --- | --- | --- |
| `g_local.h` | Removed dead `extern` for `jacket_armor_index`, `combat_armor_index`, `body_armor_index` | Declared `extern`, defined `static` in `g_items.c`, used in no other TU. A static definition after a non-static declaration is a constraint violation. The definitions keep internal linkage. |
| `g_local.h` | Removed dead `extern qboolean is_quad` | Same defect, `p_weapon.c`. |
| `q_shared.h` | Added POSIX equivalents for `_isnan`, `min`, `max`, and includes for `strcasecmp`/`access` | MSVC CRT spellings the source uses directly. Each substitute is the exact standard equivalent. See the note on `min`/`max` below. |
| `g_ctf.c`, `g_ctf.h` | `CTFDrop_Flag` returns `void`, not `qboolean` | It is only ever stored in `gitem_t::drop`, which is `void (*)(edict_t *, gitem_t *)`. Every other drop handler returns void and this one's value is never read. |
| `m_flyer.c` | `flyer_blocked` returns `qboolean`, not `int` | `monsterinfo.blocked` is `qboolean (*)(edict_t *, float)`; every other `*_blocked` handler already returns `qboolean`. |
| `m_boss31.c` | `extern void SP_monster_makron(...)` | Forward declaration with implicit `int`; the definition in `m_boss32.c` returns `void`. |
| `g_ctf.h` | Declared `stuffcmd` | Defined in `g_ctf.c`, called from `p_client.c` under `CTF_HOOK`, never declared. Implicit declarations are now errors. |
| `bl_spawn.c` | 64-bit branch for the default botlib filename | The two `botlib` cvar defaults were `gladiator.dll` / `gladi386.so`. The 32-bit name is exactly what retail shipped and is untouched; a 64-bit build now asks for `gladx86_64.so`, which is what it actually produces. Without this the Linux archive shipped two modules that could not find each other. |

No behaviour changes. The two function-pointer fixes remove real undefined
behaviour: calling through a mismatched pointer type worked by accident on
1999 x86 and is not guaranteed anywhere.

### The `min` / `max` shim is deliberately unsafe

The substitutes in `q_shared.h` are the MSVC CRT's macros character for
character, taken from `ucrt/stdlib.h`:

```c
#define max(a,b) (((a) > (b)) ? (a) : (b))
#define min(a,b) (((a) < (b)) ? (a) : (b))
```

They double-evaluate their arguments, and two call sites pass arguments with
side effects — `m_widow2_rogue.c:1358` passes `random()`, `m_widow_rogue.c:1611`
passes `CountPlayers()`. Retail and both Windows builds get MSVC's macros and
therefore evaluate those twice.

Replacing them with inline functions would look like a cleanup and would
actually be a divergence: the Linux build would evaluate once where Windows and
retail evaluate twice, changing the PRNG stream. The shim keeps all three
builds identical, and `q_shared.h` carries the same warning at the definition.

## Build configuration

Set in [`src/game/CMakeLists.txt`](../src/game/CMakeLists.txt) rather than by
editing source:

- **`C_ONLY`** — the source's own switch for "portable C, no hand-written
  assembly". It forces `id386` to 0, which selects the C `BoxOnPlaneSide` over
  the MSVC `__declspec(naked)` version and drops the inline-asm `Q_ftol`
  (which nothing in the tree calls). Modern compilers reject MSVC naked
  functions containing inline asm. The 64-bit builds already took the C path
  because `id386` is 0 there; defining it everywhere keeps all three release
  builds behaviourally identical instead of only the 64-bit ones.
- **`WIN32`** on Windows — `q_shared.c` tests bare `WIN32` (not `_WIN32`) to
  pick `_stricmp` over `strcasecmp`. MSVC6 supplied it from project settings;
  today it arrives only through CMake's default flags, which a build that
  overrides `CMAKE_C_FLAGS` — as the 32-bit release build does — silently
  drops.
- **`stricmp=strcasecmp`** elsewhere, matching the original `linux-i386.mak`.
- **`C_EXTENSIONS ON`** — stated explicitly, not left to the default: the
  source calls `strdup`, `strcasecmp` and `access`, which strict `-std=c99`
  hides.

### Module names

Both halves are named per platform and word size, and the two must agree: the
game module `dlopen`s the botlib by a hard-coded filename and
`BotLoadLibrary` has no fallback, so a mismatch means no bots and a
`couldn't load` line in the console.

| Build | Game module | Botlib |
| --- | --- | --- |
| Windows 32-bit | `gamex86.dll` — what retail Quake II loads | `gladiator.dll` |
| Windows 64-bit | `gamex86_64.dll` | `gladiator.dll` |
| Linux 32-bit | `gamei386.so` | `gladi386.so` |
| Linux 64-bit | `gamex86_64.so` | `gladx86_64.so` |

The 32-bit row is exactly what the 1999 release shipped: the retail Linux
archives (`gladq2096_linux-x86-glibc.tar.gz` and the libc5 build) contain
`gamei386.so`, `gladi386.so` and `bspci386` — every binary arch-suffixed, no
`lib` prefix. The 64-bit row applies that same rule to a new architecture
rather than inventing a scheme, and `bl_spawn.c` carries a `PORT(arch)` change
so its two hard-coded defaults select the right one.

This is why the botlib target overrides `PREFIX` on Linux: CMake's default
`libgladiator.so` is a name nothing would ever look for.

### Exports

A built game module exports exactly one symbol, `GetGameAPI`, on every
platform — via the original `game.def` on Windows and a linker version script
(`game.exports`) on ELF. This mirrors the botlib, which exports only
`GetBotAPI`.

## The `p_observer.c` question

`p_observer.c` is **not** in Mr Elusive's source release, although
`p_observer.h` is and `linux-i386.mak` still lists `p_observer.o`.

This does not break anything. Observer mode is gated on `OBSERVER`, which is
commented out at `g_local.h:7`, and every call into that file — `DoObserver`
in `p_client.c`, `ClientObserverCmd` in `g_cmds.c` — sits inside
`#ifdef OBSERVER`. The default configuration, the one he shipped, neither
compiles nor links it. The module builds and links cleanly without it on all
three targets; the stale makefile entry is the only loose end.

So the file is a *disabled feature*, not a missing dependency. Enabling
observer mode would require reconstructing it from the retail `gamex86.dll`
(`dev_tools/assets/gamex86.dll`) the same way the botlib was reconstructed.
That is optional work, not a prerequisite, and it has not been done.

## What was not copied

The 1999 build files stay in `dev_tools/game_source/` and are not part of the
build: `game.dsp`, `game.plg`, `msvc60.mak`, `lcc.mak`, `linux-i386.mak`.
CMake supersedes them. `game.def` *is* copied, because the Windows build still
uses it verbatim.
