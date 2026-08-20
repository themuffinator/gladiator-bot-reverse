# Archive of the original Gladiator Bot releases

Mr Elusive's own distribution files, mirrored here exactly as he published
them on [his project page](https://mrelusive.com/oldprojects/gladiator/download.shtml.htm).

Nothing here is built by this repository, and nothing here is modified. These
are the 1999 binaries and source archives, byte for byte — the ground truth
that everything under `src/` is reconstructed against, and a hedge against
the day that site stops answering.

Every file is verified against [`SHA256SUMS.txt`](SHA256SUMS.txt):

```bash
cd archive && sha256sum -c SHA256SUMS.txt
```

`.gitattributes` marks this tree `binary`, so git stores and checks it out
byte for byte and never applies line-ending normalisation to it.

## Gladiator Bot

| File | Size | Released | Description |
| --- | ---: | --- | --- |
| [`gladq2096_win32-x86.exe`](gladiator-bot/gladq2096_win32-x86.exe) | 1,138,009 B | 1999-07-20 | v0.96 for Win32 x86 — the retail installer |
| [`gladq2096_linux-x86-libc5.tar.gz`](gladiator-bot/gladq2096_linux-x86-libc5.tar.gz) | 906,801 B | 1999-08-02 | v0.96 for Linux x86, libc5 |
| [`gladq2096_linux-x86-glibc.tar.gz`](gladiator-bot/gladq2096_linux-x86-glibc.tar.gz) | 907,051 B | 1999-08-02 | v0.96 for Linux x86, glibc |
| [`gladq2096gamesrc.zip`](gladiator-bot/gladq2096gamesrc.zip) | 551,076 B | 1999-08-02 | v0.96 game source |

Upstream: `https://mrelusive.com/oldprojects/gladiator/ftp/`

## Bot characters

| File | Size | Released | Description |
| --- | ---: | --- | --- |
| [`gbc092_epilepsy.zip`](bot-characters/gbc092_epilepsy.zip) | 4,239 B | 1999-04-01 | EPiLePSy — by Zindahsh |
| [`gbc092_ernie.zip`](bot-characters/gbc092_ernie.zip) | 2,864 B | 1999-04-05 | Evil Ernie — by Felix |
| [`gbc092_fuel.zip`](bot-characters/gbc092_fuel.zip) | 3,288 B | 1999-04-05 | Fuel — by Felix |
| [`gbc092_garf.zip`](bot-characters/gbc092_garf.zip) | 4,685 B | 1999-04-10 | garF — by Blair Williams |
| [`gbc092_morphias.zip`](bot-characters/gbc092_morphias.zip) | 2,595 B | 1999-04-10 | MorpHias — by Blair Williams |
| [`gbc092_keyboy.zip`](bot-characters/gbc092_keyboy.zip) | 3,951 B | 1999-04-15 | KeyBoy — by Zindahsh |
| [`gbc092_garf9.zip`](bot-characters/gbc092_garf9.zip) | 14,094 B | 1999-05-02 | nine characters — by Blair Williams |
| [`gbc092_swarm.zip`](bot-characters/gbc092_swarm.zip) | 3,237 B | 1999-05-10 | Swarm — by Jason |

Upstream: `https://mrelusive.com/oldprojects/gladiator/ftp/characters/`

## BSPC and WinBSPC

| File | Size | Released | Description |
| --- | ---: | --- | --- |
| [`winbspc12.zip`](bspc/winbspc12.zip) | 119,746 B | 1999-05-20 | WinBSPC v1.2, Win32 x86 — the GUI front end |
| [`bspc12_win32-x86.zip`](bspc/bspc12_win32-x86.zip) | 104,990 B | 1999-05-20 | BSPC v1.2, Win32 x86 |
| [`bspc12_linux-x86-libc5.tar.gz`](bspc/bspc12_linux-x86-libc5.tar.gz) | 110,248 B | 1999-05-20 | BSPC v1.2, Linux x86 libc5 |
| [`bspc12_linux-x86-glibc.tar.gz`](bspc/bspc12_linux-x86-glibc.tar.gz) | 110,277 B | 1999-05-20 | BSPC v1.2, Linux x86 glibc |

Upstream: `https://mrelusive.com/oldprojects/gladiator/ftp/`

## Notes

- **`gladq2096gamesrc.zip` is the origin of [`src/game/`](../src/game/).** The
  same 141 files are also vendored unpacked at `dev_tools/game_source/`. See
  [game_source_integration.md](../docs/game_source_integration.md) for what
  changed on the way into the build.
- **The Linux archives settle the module naming.** They contain
  `gladi386.so`, `gamei386.so` and `bspci386` — every binary arch-suffixed
  with no `lib` prefix, which is the convention this project extends to
  64-bit builds.
- **`gladq2096_win32-x86.exe` is a 1999 self-extracting installer.** It is
  archived as a historical artifact. Nothing in this repository runs it, and
  you should not need to.
- **The bot characters are third-party work**, by the authors credited above,
  distributed from Mr Elusive's site. They are character configs and weight
  scripts (`bots/*_c.c`, `*_w.c`, `*_t.c`), not code.
- The download page also lists Omicron Bot and MeQCC. Neither is part of the
  Gladiator Bot, so neither is mirrored here.
