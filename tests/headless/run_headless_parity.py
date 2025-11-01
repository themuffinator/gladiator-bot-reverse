#!/usr/bin/env python3
"""Headless Quake II parity harness launcher.

This script is intended to be driven by CTest so the long-running
headless Quake II parity check can be gated behind an opt-in label.
It ensures the required assets and binaries are available, stages the
rebuilt Gladiator module inside the Quake II install tree, and then
bootstraps the dedicated server with a standard parity configuration.

The harness intentionally exits with status code 125 when prerequisites
are missing.  CTest maps that code to a skipped test so developers can
configure the environment without causing failing dashboards.
"""
from __future__ import annotations

import os
import shutil
import subprocess
import sys
import textwrap
from pathlib import Path

SKIP_RC = 125


def _resolve_required_env(var: str) -> Path:
    value = os.environ.get(var)
    if not value:
        print(f"[headless-parity] missing required environment variable: {var}", file=sys.stderr)
        sys.exit(SKIP_RC)
    path = Path(value).expanduser().resolve()
    if not path.exists():
        print(f"[headless-parity] {var} path does not exist: {path}", file=sys.stderr)
        sys.exit(SKIP_RC)
    return path


def _resolve_optional_env_path(var: str, default: Path) -> Path:
    value = os.environ.get(var)
    if not value:
        return default
    return Path(value).expanduser().resolve()


def _prepare_capture_dir(root: Path) -> Path:
    root.mkdir(parents=True, exist_ok=True)
    return root


def _stage_module(module_path: Path, mod_root: Path) -> Path:
    mod_root.mkdir(parents=True, exist_ok=True)
    destination = mod_root / module_path.name
    if destination.resolve() == module_path:
        return destination
    shutil.copy2(module_path, destination)
    return destination


def _ensure_default_cfg(cfg_path: Path) -> Path:
    if cfg_path.exists():
        return cfg_path
    cfg_path.parent.mkdir(parents=True, exist_ok=True)
    cfg_body = textwrap.dedent(
        """
        set dedicated 1
        set developer 1
        set logfile 2
        set deathmatch 1
        set bot_enable 1
        // Launch a lightweight parity scenario and exit once the map is ready.
        map q2dm1
        wait
        wait
        wait
        quit
        """
    ).strip() + "\n"
    cfg_path.write_text(cfg_body, encoding="utf-8")
    return cfg_path


def main() -> int:
    dedicated_path = _resolve_required_env("GLADIATOR_Q2_DEDICATED_SERVER")
    module_path = _resolve_required_env("GLADIATOR_Q2_MODULE_PATH")
    basedir = _resolve_required_env("GLADIATOR_Q2_BASEDIR")

    if not dedicated_path.is_file():
        print(f"[headless-parity] GLADIATOR_Q2_DEDICATED_SERVER must point to an executable file: {dedicated_path}", file=sys.stderr)
        return SKIP_RC

    if not os.access(dedicated_path, os.X_OK):
        print(f"[headless-parity] dedicated server is not executable: {dedicated_path}", file=sys.stderr)
        return SKIP_RC

    baseq2_dir = basedir / "baseq2"
    if not baseq2_dir.is_dir():
        print(f"[headless-parity] expected baseq2 assets under {basedir} (missing baseq2 directory)", file=sys.stderr)
        return SKIP_RC

    capture_root = _prepare_capture_dir(
        _resolve_optional_env_path(
            "GLADIATOR_Q2_CAPTURE_DIR",
            Path.cwd() / "headless-parity-captures",
        )
    )

    mod_dir = _resolve_optional_env_path(
        "GLADIATOR_Q2_MOD_DIR",
        basedir / "gladiator",
    )
    _stage_module(module_path, mod_dir)

    cfg_path = _ensure_default_cfg(
        _resolve_optional_env_path(
            "GLADIATOR_Q2_PARITY_CFG",
            capture_root / "botlib_parity.cfg",
        )
    )

    log_path = capture_root / "dedicated.log"

    command = [
        str(dedicated_path),
        "+set",
        "basedir",
        str(basedir),
        "+set",
        "game",
        mod_dir.name,
        "+set",
        "dedicated",
        "1",
        "+set",
        "logfile",
        "2",
        "+exec",
        str(cfg_path),
    ]

    extra = os.environ.get("GLADIATOR_Q2_EXTRA_ARGS")
    if extra:
        command.extend(extra.split())

    # Ensure the harness exits even if the config forgets to issue quit.
    if "+quit" not in command:
        command.extend(["+wait", "+wait", "+quit"])

    print("[headless-parity] launching:", " ".join(command))

    try:
        with log_path.open("wb") as log_file:
            result = subprocess.run(
                command,
                check=False,
                stdout=log_file,
                stderr=subprocess.STDOUT,
                cwd=basedir,
                timeout=int(os.environ.get("GLADIATOR_Q2_TIMEOUT", "180")),
            )
    except subprocess.TimeoutExpired:
        print(
            f"[headless-parity] dedicated server timed out; logs captured in {log_path}",
            file=sys.stderr,
        )
        return 1

    if result.returncode != 0:
        print(
            f"[headless-parity] dedicated server exited with {result.returncode}; logs captured in {log_path}",
            file=sys.stderr,
        )
        return result.returncode

    print(f"[headless-parity] completed successfully; logs captured in {log_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
