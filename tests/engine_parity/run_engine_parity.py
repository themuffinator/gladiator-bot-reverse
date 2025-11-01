#!/usr/bin/env python3
"""Harness for validating engine parity using a Quake II dedicated server.

The script launches a Quake II server (Yamagi or another compatible binary),
points it at the Gladiator bot module produced by this repository, and executes
scripted startup commands that exercise the botlib. Console output is captured
and mirrored into the test artifacts directory together with the qconsole log
and optional demo files produced by the engine.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import textwrap
import time
from pathlib import Path
from typing import Iterable, List, Optional

# Substrings that indicate the server failed to load the Gladiator library.
_DLL_FAILURE_TOKENS = (
    "couldn't load",
    "could not load",
    "LoadLibrary failed",
    "failed to load",
    "undefined symbol",
)


def _resolve_server_binary(candidate: str) -> Path:
    """Resolve the server binary from the command line or PATH."""
    path = Path(candidate)
    if path.exists() and os.access(path, os.X_OK):
        return path
    resolved = shutil.which(candidate)
    if resolved:
        return Path(resolved)
    raise FileNotFoundError(
        f"Unable to locate Quake II server executable '{candidate}'. Provide an "
        "absolute path or ensure it is discoverable via PATH/QUAKE2_SERVER."
    )


def _copy_or_symlink(src: Path, dst: Path) -> None:
    """Attempt to create a symlink, falling back to a file copy."""
    if dst.exists() or dst.is_symlink():
        if dst.is_dir():
            raise IsADirectoryError(f"Destination '{dst}' is a directory")
        dst.unlink()
    try:
        dst.symlink_to(src)
    except OSError:
        shutil.copy2(src, dst)


def _read_template(template_path: Path) -> str:
    if not template_path.exists():
        raise FileNotFoundError(f"Config template '{template_path}' is missing")
    return template_path.read_text(encoding="utf-8")


def _render_config(
    template: str,
    *,
    botlib: Path,
    maxclients: int,
    timelimit: int,
    fraglimit: int,
    dmflags: int,
    map_name: str,
    bot_commands: Iterable[str],
    demo_name: Optional[str],
) -> str:
    rendered = template.format(
        botlib=str(botlib),
        maxclients=maxclients,
        timelimit=timelimit,
        fraglimit=fraglimit,
        dmflags=dmflags,
        map_name=map_name,
        bot_commands="\n".join(bot_commands),
        record_command=f"record {demo_name}" if demo_name else "// demo capture disabled",
    )
    return rendered


def _default_bot_commands() -> List[str]:
    # The Gladiator assets shipped with the original mod expose an "addbot"
    # console command that accepts (name, skin, character file, nickname).
    # Using the canonical bot names ensures the resulting demo is deterministic
    # across reference captures.
    return [
        'addbot "Adrenaline Hunk" "male/viper" "bots/hunk_c.c" "hunk"',
        'addbot "Reaper" "cyborg/oni911" "bots/reaper_c.c" "reaper"',
    ]


def _detect_dll_failure(line: str) -> bool:
    lowered = line.lower()
    if "gladiator" not in lowered:
        return False
    return any(token in lowered for token in _DLL_FAILURE_TOKENS)


def _write_console_line(handle, line: str) -> None:
    handle.write(line)
    handle.flush()


def _copy_if_exists(src: Path, dst: Path) -> Optional[Path]:
    if src.exists():
        shutil.copy2(src, dst)
        return dst
    return None


def run_harness(args: argparse.Namespace) -> int:
    repo_root = Path(__file__).resolve().parents[2]
    artifacts_dir = Path(args.artifacts_dir).resolve()
    artifacts_dir.mkdir(parents=True, exist_ok=True)

    runtime_dir = artifacts_dir / "runtime"
    runtime_dir.mkdir(parents=True, exist_ok=True)

    fs_game = args.fs_game
    mod_dir = runtime_dir / fs_game
    mod_dir.mkdir(parents=True, exist_ok=True)

    dll_path = Path(args.dll_path)
    if not dll_path.exists():
        raise FileNotFoundError(
            f"Gladiator bot library '{dll_path}' is missing. Build the project or "
            "point --dll-path to an installed tree."
        )

    staged_botlib = mod_dir / dll_path.name
    _copy_or_symlink(dll_path, staged_botlib)

    template_path = repo_root / "tests" / "engine_parity" / "startup.cfg.in"
    template = _read_template(template_path)

    if args.bot_script:
        raw_lines = Path(args.bot_script).read_text(encoding="utf-8").splitlines()
        bot_commands = [
            line.strip()
            for line in raw_lines
            if line.strip() and not line.strip().startswith("//")
        ]
    else:
        bot_commands = _default_bot_commands()
    rendered_cfg = _render_config(
        template,
        botlib=staged_botlib,
        maxclients=args.maxclients,
        timelimit=args.timelimit,
        fraglimit=args.fraglimit,
        dmflags=args.dmflags,
        map_name=args.map,
        bot_commands=bot_commands,
        demo_name=args.demo_name,
    )

    config_path = runtime_dir / "engine_parity.cfg"
    config_path.write_text(rendered_cfg, encoding="utf-8")

    console_log_path = artifacts_dir / "server_console.log"
    qconsole_path = mod_dir / "qconsole.log"
    demo_path = mod_dir / "demos" / f"{args.demo_name}.dm2" if args.demo_name else None

    server_exe = _resolve_server_binary(args.server)

    cmd = [
        str(server_exe),
        "+set",
        "dedicated",
        "1",
        "+set",
        "fs_basepath",
        str(runtime_dir),
        "+set",
        "fs_game",
        fs_game,
        "+exec",
        str(config_path),
    ]

    failure_detected = False
    timed_out = False

    with console_log_path.open("w", encoding="utf-8") as console_log:
        console_log.write(
            textwrap.dedent(
                f"""\
                # Engine Parity Harness
                # Server binary: {server_exe}
                # Bot library: {dll_path}
                # Working directory: {runtime_dir}
                # Executed command: {' '.join(cmd)}\n\n"""
            )
        )
        console_log.flush()

        process = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            stdin=subprocess.PIPE,
            text=True,
        )

        start_time = time.monotonic()
        quit_deadline = start_time + args.match_duration if args.match_duration else None
        timeout_deadline = start_time + args.timeout if args.timeout else None
        quit_sent = False

        try:
            assert process.stdout is not None
            while True:
                line = process.stdout.readline()
                if line == "":
                    break
                _write_console_line(console_log, line)
                if _detect_dll_failure(line):
                    failure_detected = True

                now = time.monotonic()
                if timeout_deadline and now > timeout_deadline:
                    timed_out = True
                    process.terminate()
                    break

                if quit_deadline and not quit_sent and now >= quit_deadline:
                    try:
                        if process.stdin:
                            process.stdin.write("quit\n")
                            process.stdin.flush()
                    except Exception:
                        pass
                    finally:
                        quit_sent = True
        finally:
            try:
                process.wait(timeout=15)
            except subprocess.TimeoutExpired:
                process.kill()

    exit_code = process.returncode if process.returncode is not None else 1
    if timed_out:
        exit_code = 1

    copied_logs = []
    copied_demo = None
    qconsole_target = artifacts_dir / "qconsole.log"
    copied = _copy_if_exists(qconsole_path, qconsole_target)
    if copied:
        copied_logs.append(copied)

    if demo_path and demo_path.exists():
        artifacts_dir.joinpath("demos").mkdir(exist_ok=True)
        copied_demo = _copy_if_exists(demo_path, artifacts_dir / "demos" / demo_path.name)

    manifest = {
        "console_log": str(console_log_path),
        "qconsole_log": str(copied_logs[0]) if copied_logs else None,
        "demo": str(copied_demo) if copied_demo else None,
        "timed_out": timed_out,
        "dll_failure_detected": failure_detected,
        "exit_code": exit_code,
    }
    with (artifacts_dir / "manifest.json").open("w", encoding="utf-8") as manifest_file:
        json.dump(manifest, manifest_file, indent=2)
        manifest_file.write("\n")

    if failure_detected:
        return 1
    return exit_code


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--server",
        default=os.environ.get("QUAKE2_SERVER", "yamagi-quake2"),
        help="Path or name of the Quake II dedicated server binary.",
    )
    parser.add_argument(
        "--dll-path",
        default="build/gladiator.dll",
        help="Path to the built gladiator.dll (or equivalent shared library).",
    )
    parser.add_argument(
        "--fs-game",
        default="gladiator",
        help="Mod directory to mount via fs_game.",
    )
    parser.add_argument(
        "--artifacts-dir",
        default=Path(__file__).resolve().parent / "artifacts",
        help="Destination directory for logs, demos, and manifest metadata.",
    )
    parser.add_argument(
        "--bot-script",
        help="Optional path to a file containing newline-separated bot commands.",
    )
    parser.add_argument(
        "--map",
        default="q2dm1",
        help="Map to load when the server boots.",
    )
    parser.add_argument(
        "--timelimit",
        type=int,
        default=5,
        help="Match timelimit applied via the server config.",
    )
    parser.add_argument(
        "--fraglimit",
        type=int,
        default=0,
        help="Optional fraglimit applied during the session.",
    )
    parser.add_argument(
        "--maxclients",
        type=int,
        default=8,
        help="Maximum number of clients to allow on the server.",
    )
    parser.add_argument(
        "--dmflags",
        type=int,
        default=0,
        help="Value for the dmflags cvar.",
    )
    parser.add_argument(
        "--match-duration",
        type=float,
        default=30.0,
        help="Time in seconds to wait before issuing a quit command.",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=0.0,
        help="Optional timeout in seconds for the server boot phase.",
    )
    parser.add_argument(
        "--demo-name",
        help="When provided, instructs the server to record a demo with this base name.",
    )
    return parser


def main(argv: Optional[List[str]] = None) -> int:
    parser = build_arg_parser()
    args = parser.parse_args(argv)
    try:
        return run_harness(args)
    except FileNotFoundError as exc:
        print(f"[engine_parity] {exc}", file=sys.stderr)
        return 1
    except Exception as exc:  # pragma: no cover - defensive catch
        print(f"[engine_parity] Unexpected error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
