#!/usr/bin/env python3
"""Headless Quake II parity harness.

Runs the rebuilt Gladiator botlib inside a real dedicated server, with no
window and no interaction, then asserts on what the server logged: the botlib
initialised, bots connected and entered the game, and nothing reported a fatal
or error-level diagnostic.

The harness exits with status code 125 when prerequisites are missing.  CTest
maps that code to a skipped test so developers can configure the environment
without failing the dashboard.

Environment:
  GLADIATOR_Q2_DEDICATED_SERVER  dedicated server executable (required)
  GLADIATOR_Q2_BASEDIR           Quake II install root containing baseq2 (required)
  GLADIATOR_Q2_MODULE_PATH       rebuilt gladiator module (required, set by CTest)
  GLADIATOR_Q2_MOD_DIR           mod directory to stage into (default <basedir>/gladiator)
  GLADIATOR_Q2_MAP               map to load (default q2dm1)
  GLADIATOR_Q2_BOTS              bots to require in game (default 4)
  GLADIATOR_Q2_RUNTIME           seconds to let the server run (default 45)
  GLADIATOR_Q2_PORT              server port (default 27920)
  GLADIATOR_Q2_CAPTURE_DIR       where to copy the captured log
"""
from __future__ import annotations

import os
import re
import shutil
import struct
import subprocess
import sys
import time
from pathlib import Path

SKIP_RC = 125

PE_MACHINE = {0x14C: "x86", 0x8664: "x64", 0x1C0: "arm", 0xAA64: "arm64"}

# Botlib/game diagnostics that mean the run is not healthy.  Anchored so that
# ordinary content (a bot named "Error", a map called "fatal") cannot trip them.
# A Gladiator obituary reads like "Byte was machinegunned by Bill Gates",
# "Steroid Stud was corkscrewed through his head by Demigoddess" or
# "Steroid Stud couldn't avoid death by painless from Trash".  Requiring the
# victim clause as well as " by " keeps the startup banner's "by Mr Elusive."
# out of the count - matching on " by " alone let a run pass on one fewer real
# kill than it claimed.
CHAT_LINE = re.compile(r"^[^:]{1,40}: .")
KILL_LINE = re.compile(r"(\bwas\b.*\bby\b)|(couldn't avoid)")

PROBLEM_PATTERNS = (
    re.compile(r"^Fatal:", re.IGNORECASE),
    re.compile(r"^Error:", re.IGNORECASE),
    re.compile(r"couldn't find .*\.c\b"),
    re.compile(r"couldn't load"),
    re.compile(r"counldn't load"),          # retail's own misspelling
    re.compile(r"out of .* space"),
    re.compile(r"aas not loaded", re.IGNORECASE),
    re.compile(r"no AAS file available"),
)


def _skip(message: str) -> int:
    print(f"[headless-parity] SKIP: {message}", file=sys.stderr)
    return SKIP_RC


def _fail(message: str) -> int:
    print(f"[headless-parity] FAIL: {message}", file=sys.stderr)
    return 1


def _env_path(var: str) -> Path | None:
    value = os.environ.get(var)
    if not value:
        return None
    return Path(value).expanduser()


def _pe_machine(path: Path) -> str | None:
    """Return the PE machine type of a Windows binary, or None."""
    try:
        with path.open("rb") as handle:
            head = handle.read(0x400)
        if head[:2] != b"MZ":
            return None
        offset = struct.unpack_from("<I", head, 0x3C)[0]
        if head[offset : offset + 4] != b"PE\0\0":
            return None
        return PE_MACHINE.get(struct.unpack_from("<H", head, offset + 4)[0])
    except (OSError, struct.error, IndexError):
        return None


def _mod_game_library(mod_dir: Path) -> Path | None:
    for name in ("gamex86.dll", "game.dll", "gamex86_64.dll"):
        candidate = mod_dir / name
        if candidate.is_file():
            return candidate
    return None


def _candidate_logs(basedir: Path, mod_dir: Path) -> list[Path]:
    """Log locations across the engines that ship with a Quake II install."""
    candidates = [
        mod_dir / "qconsole.log",
        basedir / mod_dir.name / "qconsole.log",
    ]
    home = Path.home()
    for root in (
        home / "Documents" / "YamagiQ2",
        home / "OneDrive" / "Documents" / "YamagiQ2",
        home / ".yq2",
        home / "AppData" / "Local" / "q2pro",
    ):
        candidates.append(root / mod_dir.name / "qconsole.log")
        candidates.append(root / "qconsole.log")
    return candidates


def _newest_log(candidates: list[Path], newer_than: float) -> Path | None:
    best: Path | None = None
    best_mtime = newer_than
    for candidate in candidates:
        try:
            mtime = candidate.stat().st_mtime
        except OSError:
            continue
        if mtime >= best_mtime:
            best, best_mtime = candidate, mtime
    return best


def main() -> int:
    dedicated = _env_path("GLADIATOR_Q2_DEDICATED_SERVER")
    basedir = _env_path("GLADIATOR_Q2_BASEDIR")
    # CTest supplies the module it just built, but that build may be the wrong
    # architecture for the installed mod (the retail game library is 32-bit).
    # An explicit GLADIATOR_Q2_MODULE_PATH therefore wins over the build one.
    module = _env_path("GLADIATOR_Q2_MODULE_PATH") or _env_path(
        "GLADIATOR_Q2_MODULE_PATH_BUILD"
    )

    if dedicated is None or basedir is None or module is None:
        return _skip(
            "set GLADIATOR_Q2_DEDICATED_SERVER, GLADIATOR_Q2_BASEDIR and "
            "GLADIATOR_Q2_MODULE_PATH to run the headless parity scenario"
        )
    if not dedicated.is_file():
        return _skip(f"dedicated server is not a file: {dedicated}")
    if not module.is_file():
        return _skip(f"module not built: {module}")
    if not (basedir / "baseq2").is_dir():
        return _skip(f"no baseq2 directory under {basedir}")

    mod_dir = _env_path("GLADIATOR_Q2_MOD_DIR") or (basedir / "gladiator")
    if not mod_dir.is_dir():
        return _skip(f"mod directory not present: {mod_dir}")

    game_library = _mod_game_library(mod_dir)
    if game_library is None:
        return _skip(f"no game library in {mod_dir}")

    # A module of the wrong architecture is silently never loaded by the game
    # library, which would let this harness "pass" while exercising nothing.
    module_arch = _pe_machine(module)
    game_arch = _pe_machine(game_library)
    if module_arch and game_arch and module_arch != game_arch:
        return _skip(
            f"architecture mismatch: {module.name} is {module_arch} but "
            f"{game_library.name} is {game_arch}; the game library cannot load "
            f"it, so the scenario would exercise none of the rebuilt botlib"
        )

    staged = mod_dir / module.name
    backup: Path | None = None
    if staged.exists() and staged.resolve() != module.resolve():
        backup = staged.with_suffix(staged.suffix + ".headless-backup")
        shutil.copy2(staged, backup)
    if staged.resolve() != module.resolve():
        shutil.copy2(module, staged)

    game_map = os.environ.get("GLADIATOR_Q2_MAP", "q2dm1")
    want_bots = int(os.environ.get("GLADIATOR_Q2_BOTS", "4"))
    want_kills = int(os.environ.get("GLADIATOR_Q2_KILLS", "3"))
    runtime = int(os.environ.get("GLADIATOR_Q2_RUNTIME", "45"))
    port = os.environ.get("GLADIATOR_Q2_PORT", "27920")

    command = [
        str(dedicated),
        "+set", "game", mod_dir.name,
        "+set", "dedicated", "1",
        "+set", "deathmatch", "1",
        "+set", "maxclients", str(max(want_bots + 2, 8)),
        # Bots are spawned from the game's frame loop rather than the command
        # buffer: a "map" command flushes anything queued behind it, so
        # "+exec addbots.cfg" after "+map" never runs.
        "+set", "minimumplayers", str(want_bots),
        "+set", "port", port,
        "+set", "logfile", "2",
        "+map", game_map,
    ]

    extra = os.environ.get("GLADIATOR_Q2_EXTRA_ARGS")
    if extra:
        command.extend(extra.split())

    print("[headless-parity] launching:", " ".join(command))
    started = time.time() - 1.0

    # The dedicated console polls console input, which fails outright when
    # stdin is a pipe ("Error getting # of console events").  Give the child a
    # real console of its own; it is never shown and never interacted with.
    popen_kwargs = {}
    if os.name == "nt":
        # Give the child its own console and leave its standard handles bound
        # to it.  Redirecting stdin here would point the console-input poll at
        # NUL and reproduce the very failure this flag exists to avoid.
        popen_kwargs["creationflags"] = getattr(
            subprocess, "CREATE_NEW_CONSOLE", 0
        )
    else:
        popen_kwargs["stdin"] = subprocess.DEVNULL

    try:
        process = subprocess.Popen(
            command,
            cwd=str(basedir),
            **popen_kwargs,
        )
    except OSError as exc:
        return _fail(f"could not launch dedicated server: {exc}")

    # The mod adds bots from its frame loop a few seconds apart, so poll the
    # live log rather than guessing a sleep: finish as soon as the scenario has
    # actually played out, and stop at the deadline either way.
    deadline = time.time() + runtime
    candidates = _candidate_logs(basedir, mod_dir)
    try:
        while time.time() < deadline:
            if process.poll() is not None:
                break
            time.sleep(1.0)
            current = _newest_log(candidates, started)
            if current is None:
                continue
            text = current.read_text(encoding="utf-8", errors="replace")
            seen = text.splitlines()
            joined = sum(1 for l in seen if l.endswith("entered the game"))
            fought = sum(
                1
                for l in seen
                if KILL_LINE.search(l)
                and not CHAT_LINE.match(l)
                and not l.startswith("loaded")
            )
            # Stop once the bots are not just present but actually fighting;
            # otherwise keep playing until the deadline.
            if joined >= want_bots and fought >= want_kills:
                break
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=15)
            except subprocess.TimeoutExpired:
                process.kill()
    finally:
        if backup is not None:
            shutil.copy2(backup, staged)
            backup.unlink(missing_ok=True)

    log_path = _newest_log(_candidate_logs(basedir, mod_dir), started)
    if log_path is None:
        return _fail(
            "no server log produced; expected a qconsole.log written by the "
            "engine (logfile 2)"
        )

    log = log_path.read_text(encoding="utf-8", errors="replace")
    lines = log.splitlines()

    capture_dir = _env_path("GLADIATOR_Q2_CAPTURE_DIR") or (
        Path.cwd() / "headless-parity-captures"
    )
    capture_dir.mkdir(parents=True, exist_ok=True)
    shutil.copy2(log_path, capture_dir / "dedicated.log")
    print(f"[headless-parity] captured {log_path} -> {capture_dir / 'dedicated.log'}")

    failures: list[str] = []

    if "AAS initialized." not in log:
        failures.append("botlib never reported 'AAS initialized.'")

    entered = sum(1 for line in lines if line.endswith("entered the game"))
    if entered < want_bots:
        failures.append(
            f"only {entered} of {want_bots} bots entered the game"
        )

    if not any("loaded" in line and game_map in line for line in lines):
        failures.append(f"no evidence the {game_map} map data was loaded")

    if not any(line.endswith(".aas") or ".aas" in line for line in lines):
        failures.append("no evidence the AAS navigation data was loaded")

    # Bots connecting proves loading, not playing.  An obituary only happens
    # after a bot has navigated to an opponent, selected a weapon and hit it,
    # so it is the cheapest end-to-end proof that navigation, target selection
    # and combat all work against real map data.
    kills = [
        line
        for line in lines
        if KILL_LINE.search(line)
        and not CHAT_LINE.match(line)
        and not line.startswith("loaded")
    ]
    if len(kills) < want_kills:
        failures.append(
            f"only {len(kills)} of {want_kills} kills; bots connected but did "
            f"not fight (navigation, weapon selection or combat)"
        )

    chats = [line for line in lines if CHAT_LINE.match(line)]
    if not chats:
        failures.append("no bot chat was emitted")

    problems = [
        line
        for line in lines
        for pattern in PROBLEM_PATTERNS
        if pattern.search(line)
    ]
    if problems:
        failures.append("server reported %d problem line(s)" % len(problems))

    if failures:
        for failure in failures:
            print(f"[headless-parity]   - {failure}", file=sys.stderr)
        for problem in problems[:20]:
            print(f"[headless-parity]   ! {problem}", file=sys.stderr)
        return _fail(f"scenario unhealthy; log copied to {capture_dir}")

    print(
        f"[headless-parity] OK: {entered} bots played {game_map} for {runtime}s "
        f"with no error-level diagnostics"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
