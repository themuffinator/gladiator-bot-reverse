#!/usr/bin/env python3
"""Run the reconstructed bspc CLI across every supported pipeline.

This script exercises the placeholder pipelines so that regression suites can
quickly detect argument parsing or filesystem regressions. Each invocation
verifies the generated artifacts and checks for the legacy timing message that
looks like ``"%5.0f seconds elapsed"`` in the reference binary.
"""

from __future__ import annotations

import argparse
import base64
import binascii
import difflib
import json
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Mapping

RE_TIMING = re.compile(r"\b\d+\s+seconds\selapsed\b")


@dataclass(frozen=True)
class ModeConfig:
    flag: str
    input_path: Path
    expected_files: Mapping[Path, Path]
    optional_files: Mapping[Path, Path] | None = None
    requires_timing: bool = True


def normalize_text(text: str, repo_root: Path | None = None) -> str:
    """Canonicalise separators and optionally strip repository prefixes."""

    normalized = text.replace("\r\n", "\n").replace("\\", "/")
    if repo_root is not None:
        root = repo_root.as_posix().rstrip("/")
        if root:
            normalized = normalized.replace(f"{root}/", "")
            normalized = normalized.replace(root, "")
    return normalized


def decode_text(data: bytes) -> str | None:
    try:
        return data.decode("utf-8")
    except UnicodeDecodeError:
        return None


def first_difference(expected: bytes, actual: bytes) -> int | None:
    for index, (lhs, rhs) in enumerate(zip(expected, actual)):
        if lhs != rhs:
            return index
    if len(expected) != len(actual):
        return min(len(expected), len(actual))
    return None


def write_text_diff(relative_path: Path, expected: str, actual: str, output_dir: Path) -> Path:
    diff_lines = difflib.unified_diff(
        expected.splitlines(keepends=True),
        actual.splitlines(keepends=True),
        fromfile=f"expected/{relative_path.as_posix()}",
        tofile=f"actual/{relative_path.as_posix()}",
        lineterm="",
    )
    diff_text = "\n".join(diff_lines)
    diff_path = output_dir / f"{relative_path.name}.diff"
    diff_path.write_text(diff_text + ("\n" if diff_text and not diff_text.endswith("\n") else ""), encoding="utf-8")
    return diff_path


def read_golden_bytes(path: Path) -> bytes:
    data = path.read_bytes()
    if path.suffix == ".base64":
        try:
            text = data.decode("ascii")
        except UnicodeDecodeError as exc:  # pragma: no cover - defensive guard
            raise ValueError(f"Golden artifact is not ASCII: {path}") from exc
        payload = "".join(line.strip() for line in text.splitlines())
        try:
            return base64.b64decode(payload, validate=True)
        except binascii.Error as exc:  # pragma: no cover - defensive guard
            raise ValueError(f"Golden artifact has invalid base64 encoding: {path}") from exc
    return data


def compare_artifact(
    relative_path: Path,
    actual_path: Path,
    golden_path: Path,
    output_dir: Path,
    repo_root: Path,
) -> tuple[bool, str | None]:
    expected_bytes = read_golden_bytes(golden_path)
    actual_bytes = actual_path.read_bytes()
    if actual_bytes == expected_bytes:
        return True, None

    expected_text = decode_text(expected_bytes)
    actual_text = decode_text(actual_bytes)
    if expected_text is not None and actual_text is not None:
        normalized_expected = normalize_text(expected_text, repo_root)
        normalized_actual = normalize_text(actual_text, repo_root)
        if normalized_expected == normalized_actual:
            return True, None

        diff_path = write_text_diff(relative_path, normalized_expected, normalized_actual, output_dir)
        return False, f"text mismatch for {relative_path.as_posix()} (diff written to {diff_path})"

    index = first_difference(expected_bytes, actual_bytes)
    if index is None:
        index = min(len(expected_bytes), len(actual_bytes))
    expected_slice = expected_bytes[index : index + 8]
    actual_slice = actual_bytes[index : index + 8]
    return (
        False,
        "binary mismatch for {path} at offset {offset}: expected {expected_len} bytes, got {actual_len} bytes. "
        "expected={expected_slice!r} actual={actual_slice!r}".format(
            path=relative_path.as_posix(),
            offset=index,
            expected_len=len(expected_bytes),
            actual_len=len(actual_bytes),
            expected_slice=expected_slice,
            actual_slice=actual_slice,
        ),
    )


class CommandFailure(RuntimeError):
    """Raised when the bspc subprocess exits with a failure code."""


class TimingMissing(AssertionError):
    """Raised when the stdout stream is missing the timing summary."""


class OutputMismatch(AssertionError):
    """Raised when generated files diverge from the golden baselines."""


def run_mode(
    bspc_path: Path,
    work_dir: Path,
    repo_root: Path,
    golden_root: Path,
    mode: ModeConfig,
) -> dict[str, object]:
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

    if mode.requires_timing and not RE_TIMING.search(result.stdout):
        raise TimingMissing(f"No timing summary found in {mode.flag} output:\n{result.stdout}")

    missing: list[str] = []
    mismatched: list[str] = []
    validated: list[str] = []
    for relative_path, golden_relative in mode.expected_files.items():
        file_path = output_dir / relative_path
        golden_path = golden_root / golden_relative
        if not golden_path.exists():
            raise FileNotFoundError(f"Golden asset missing: {golden_path}")
        if not file_path.exists():
            missing.append(relative_path.as_posix())
            continue
        matches, message = compare_artifact(relative_path, file_path, golden_path, output_dir, repo_root)
        if matches:
            validated.append(relative_path.as_posix())
        else:
            mismatched.append(message or relative_path.as_posix())
    if missing or mismatched:
        raise OutputMismatch(
            "Output validation failed for {flag}: missing={missing}, mismatched={mismatched}".format(
                flag=mode.flag, missing=missing, mismatched=mismatched
            )
        )

    optional_present: list[str] = []
    optional_mismatches: list[str] = []
    for relative_path, golden_relative in (mode.optional_files or {}).items():
        file_path = output_dir / relative_path
        if not file_path.exists():
            continue
        golden_path = golden_root / golden_relative
        if not golden_path.exists():
            raise FileNotFoundError(f"Golden asset missing: {golden_path}")
        matches, message = compare_artifact(relative_path, file_path, golden_path, output_dir, repo_root)
        if matches:
            optional_present.append(relative_path.as_posix())
        else:
            optional_mismatches.append(message or relative_path.as_posix())
    if optional_mismatches:
        raise OutputMismatch(
            "Output validation failed for {flag}: mismatched={optional}".format(
                flag=mode.flag, optional=optional_mismatches
            )
        )

    return {
        "flag": mode.flag,
        "command": cmd,
        "timing_summary": RE_TIMING.findall(result.stdout),
        "artifacts": sorted(validated),
        "optional_artifacts": sorted(optional_present),
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
    golden_root = repo_root / "tests" / "support" / "assets" / "bspc" / "golden"
    if not golden_root.exists():
        raise FileNotFoundError(f"Golden asset directory missing: {golden_root}")

    modes = [
        ModeConfig(
            flag="map2bsp",
            input_path=map_asset,
            expected_files={
                Path("simple_room.bsp"): Path("map2bsp/simple_room.bsp.base64"),
                Path("simple_room.prt"): Path("map2bsp/simple_room.prt"),
                Path("simple_room.lin"): Path("map2bsp/simple_room.lin"),
            },
        ),
        ModeConfig(
            flag="map2aas",
            input_path=map_asset,
            expected_files={
                Path("simple_room.aas"): Path("map2aas/simple_room.aas.base64"),
                Path("simple_room.bsp"): Path("map2aas/simple_room.bsp.base64"),
                Path("simple_room.prt"): Path("map2aas/simple_room.prt"),
                Path("simple_room.lin"): Path("map2aas/simple_room.lin"),
            },
        ),
        ModeConfig(
            flag="bsp2map",
            input_path=bsp_asset,
            expected_files={},
            requires_timing=False,
        ),
        ModeConfig(
            flag="bsp2bsp",
            input_path=bsp_asset,
            expected_files={
                Path("2box4.bsp"): Path("bsp2bsp/2box4.bsp.base64"),
                Path("2box4.prt"): Path("bsp2bsp/2box4.prt"),
                Path("2box4.lin"): Path("bsp2bsp/2box4.lin"),
            },
        ),
        ModeConfig(
            flag="bsp2aas",
            input_path=bsp_asset,
            expected_files={
                Path("2box4.aas"): Path("bsp2aas/2box4.aas.base64"),
            },
        ),
    ]

    workspace = args.workspace
    workspace = workspace if workspace.is_absolute() else repo_root / workspace
    workspace.mkdir(parents=True, exist_ok=True)

    summary: list[dict[str, object]] = []
    for mode in modes:
        summary.append(run_mode(bspc_path, workspace, repo_root, golden_root, mode))

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
