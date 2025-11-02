#!/usr/bin/env python3
"""Run the reconstructed bspc CLI across every supported pipeline.

This script exercises the placeholder pipelines so that regression suites can
quickly detect argument parsing or filesystem regressions. Each invocation
verifies the generated artifacts and checks for the legacy timing message that
looks like ``"%5.0f seconds elapsed"`` in the reference binary.
"""

from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Mapping

RE_TIMING = re.compile(r"\b\d+\s+seconds\selapsed\b")


@dataclass(frozen=True)
class ModeConfig:
    flag: str
    input_path: Path
    expected_files: Mapping[Path, str]
    optional_files: Iterable[Path] = ()


class CommandFailure(RuntimeError):
    """Raised when the bspc subprocess exits with a failure code."""


class TimingMissing(AssertionError):
    """Raised when the stdout stream is missing the timing summary."""


class OutputMismatch(AssertionError):
    """Raised when generated files differ from the expected placeholders."""


def run_mode(bspc_path: Path, work_dir: Path, mode: ModeConfig) -> dict[str, object]:
    output_dir = work_dir / mode.flag
    if output_dir.exists():
        shutil.rmtree(output_dir)
    output_dir.mkdir(parents=True)

    cmd = [str(bspc_path), f"-{mode.flag}", str(mode.input_path), "-output", str(output_dir)]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        raise CommandFailure(
            f"{mode.flag} failed with exit code {result.returncode}:\n{result.stdout}\n{result.stderr}"
        )

    if not RE_TIMING.search(result.stdout):
        raise TimingMissing(f"No timing summary found in {mode.flag} output:\n{result.stdout}")

    missing: list[str] = []
    mismatched: list[str] = []
    for relative_path, expected_text in mode.expected_files.items():
        file_path = output_dir / relative_path
        if not file_path.exists():
            missing.append(str(relative_path))
            continue
        contents = file_path.read_text(encoding="utf-8")
        if contents != expected_text:
            mismatched.append(str(relative_path))
    if missing or mismatched:
        raise OutputMismatch(
            "Output validation failed for {flag}: missing={missing}, mismatched={mismatched}".format(
                flag=mode.flag, missing=missing, mismatched=mismatched
            )
        )

    optional_present = sorted(
        str(path)
        for path in mode.optional_files
        if (output_dir / path).exists()
    )

    return {
        "flag": mode.flag,
        "command": cmd,
        "timing_summary": RE_TIMING.findall(result.stdout),
        "artifacts": sorted(str(path) for path in mode.expected_files),
        "optional_artifacts": optional_present,
    }


def detect_repository_root(start: Path) -> Path:
    current = start
    for parent in [current, *current.parents]:
        if (parent / ".git").is_dir():
            return parent
    raise FileNotFoundError(f"Unable to locate repository root from {start}")


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--bspc",
        dest="bspc_path",
        type=Path,
        help="Path to the bspc executable (defaults to build/tools/bspc/bspc)",
    )
    parser.add_argument(
        "--workspace",
        type=Path,
        default=Path("build/test-output/bspc_cli"),
        help="Directory used for generated files",
    )
    parser.add_argument(
        "--json",
        dest="json_path",
        type=Path,
        help="Write the run summary to this JSON file",
    )
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    repo_root = detect_repository_root(Path.cwd())

    bspc_path = args.bspc_path
    if bspc_path is None:
        bspc_path = repo_root / "build" / "tools" / "bspc" / "bspc"
    if not bspc_path.exists():
        raise FileNotFoundError(
            f"bspc executable not found at {bspc_path}. Build the tool before running this script."
        )

    map_asset = repo_root / "tests" / "support" / "assets" / "bspc" / "simple_room.map"
    if not map_asset.exists():
        raise FileNotFoundError(f"MAP asset missing: {map_asset}")
    bsp_asset = repo_root / "dev_tools" / "assets" / "maps" / "2box4.bsp"
    if not bsp_asset.exists():
        raise FileNotFoundError(f"BSP asset missing: {bsp_asset}")

    expected_map_placeholder = f"placeholder BSP generated from {map_asset.as_posix()}\n"
    expected_bsp_placeholder = f"placeholder BSP generated from {bsp_asset.as_posix()}\n"
    expected_map_aas_placeholder = f"placeholder AAS generated from {map_asset.as_posix()}\n"
    expected_bsp_aas_placeholder = f"placeholder AAS generated from {bsp_asset.as_posix()}\n"

    modes = [
        ModeConfig(
            flag="map2bsp",
            input_path=map_asset,
            expected_files={
                Path("simple_room.bsp"): expected_map_placeholder,
                Path("simple_room.prt"): "",
                Path("simple_room.lin"): "",
            },
        ),
        ModeConfig(
            flag="map2aas",
            input_path=map_asset,
            expected_files={
                Path("simple_room.aas"): expected_map_aas_placeholder,
                Path("simple_room.bsp"): expected_map_placeholder,
                Path("simple_room.prt"): "",
                Path("simple_room.lin"): "",
            },
        ),
        ModeConfig(
            flag="bsp2map",
            input_path=bsp_asset,
            expected_files={},
        ),
        ModeConfig(
            flag="bsp2bsp",
            input_path=bsp_asset,
            expected_files={
                Path("2box4.bsp"): expected_bsp_placeholder,
                Path("2box4.prt"): "",
                Path("2box4.lin"): "",
            },
        ),
        ModeConfig(
            flag="bsp2aas",
            input_path=bsp_asset,
            expected_files={
                Path("2box4.aas"): expected_bsp_aas_placeholder,
            },
        ),
    ]

    workspace = args.workspace
    workspace = workspace if workspace.is_absolute() else repo_root / workspace
    workspace.mkdir(parents=True, exist_ok=True)

    summary: list[dict[str, object]] = []
    for mode in modes:
        summary.append(run_mode(bspc_path, workspace, mode))

    if args.json_path:
        json_path = args.json_path if args.json_path.is_absolute() else repo_root / args.json_path
        json_path.parent.mkdir(parents=True, exist_ok=True)
        json_path.write_text(json.dumps(summary, indent=2), encoding="utf-8")

    print(json.dumps(summary, indent=2))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except (CommandFailure, TimingMissing, OutputMismatch) as exc:
        print(str(exc), file=sys.stderr)
        raise SystemExit(1)
