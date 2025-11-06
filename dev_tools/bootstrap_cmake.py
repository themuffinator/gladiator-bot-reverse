"""Utility for configuring and building the Gladiator bot project with CMake.

This helper mirrors the manual shell instructions documented in the README
while ensuring that the CMake cache is generated before invoking
``cmake --build``.  The bootstrap is intended for developers running into the
common "missing CMakeCache.txt" error when calling the build command directly
without a prior configuration pass.
"""

from __future__ import annotations

import argparse
import platform
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Iterable, List


REPO_ROOT = Path(__file__).resolve().parents[1]


def _run_command(argv: Iterable[str]) -> None:
    """Execute *argv* while echoing the command to stdout."""

    cmd = list(argv)
    print("$", " ".join(cmd), flush=True)
    subprocess.check_call(cmd)


def _is_multi_config_generator(generator: str | None) -> bool:
    """Return ``True`` if *generator* exposes multi-configuration builds."""

    if not generator:
        return False

    normalized = generator.lower()
    return (
        "visual studio" in normalized
        or "multi-config" in normalized
        or "xcode" in normalized
    )


def _default_generator() -> List[str]:
    """Best-effort default CMake generator arguments for the host platform."""

    system = platform.system()

    if shutil.which("ninja"):
        return ["-G", "Ninja"]

    if system == "Windows":
        # Assume a Visual Studio 2022 environment when Ninja is unavailable.
        return ["-G", "Visual Studio 17 2022", "-A", "x64"]

    # Fallback to the platform default generator selected by CMake.
    return []


def _generator_from_cache(cache: Path) -> str | None:
    try:
        for line in cache.read_text().splitlines():
            if line.startswith("CMAKE_GENERATOR:INTERNAL="):
                return line.partition("=")[2]
    except OSError as exc:  # pragma: no cover - best-effort cache read
        print(f"warning: failed to read {cache}: {exc}", file=sys.stderr)
    return None


def configure(build_dir: Path, args: argparse.Namespace) -> str | None:
    generator_args: List[str] = []
    multi_config = False

    if args.generator:
        generator_args = ["-G", args.generator]
        multi_config = _is_multi_config_generator(args.generator)
        resolved_generator = args.generator
    else:
        generator_args = _default_generator()
        resolved_generator = generator_args[1] if generator_args else None
        multi_config = _is_multi_config_generator(resolved_generator)

    cache_variables = []
    if args.build_type and not multi_config:
        cache_variables.append(f"-DCMAKE_BUILD_TYPE={args.build_type}")

    configure_cmd = [
        "cmake",
        "-S",
        str(REPO_ROOT),
        "-B",
        str(build_dir),
        *generator_args,
        *cache_variables,
    ]

    _run_command(configure_cmd)

    if args.config and not multi_config:
        print(
            "warning: --config is ignored for single-config generators; "
            "set --build-type instead",
            file=sys.stderr,
        )

    return resolved_generator


def build(build_dir: Path, args: argparse.Namespace, generator: str | None) -> None:
    build_cmd = [
        "cmake",
        "--build",
        str(build_dir),
    ]

    if args.target:
        build_cmd.extend(["--target", args.target])

    multi_config = _is_multi_config_generator(generator)
    if args.config:
        build_cmd.extend(["--config", args.config])
    elif args.build_type and multi_config:
        build_cmd.extend(["--config", args.build_type])

    if args.clean_first:
        build_cmd.append("--clean-first")

    _run_command(build_cmd)


def parse_args(argv: Iterable[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Configure the project if required and invoke cmake --build. "
            "Use this helper when the build directory does not yet contain a "
            "CMakeCache.txt file."
        )
    )

    parser.add_argument(
        "--build-dir",
        default=REPO_ROOT / "build",
        type=Path,
        help="Binary directory to configure (defaults to <repo>/build)",
    )
    parser.add_argument(
        "--build-type",
        default="Release",
        help="CMAKE_BUILD_TYPE for single-config generators (default: Release)",
    )
    parser.add_argument(
        "--config",
        help="Configuration passed to --config when using a multi-config generator",
    )
    parser.add_argument(
        "--generator",
        help="Explicit CMake generator (e.g. Ninja, Visual Studio 17 2022)",
    )
    parser.add_argument(
        "--target",
        default="gladiator",
        help="Target passed to cmake --build (default: gladiator)",
    )
    parser.add_argument(
        "--clean-first",
        action="store_true",
        help="Request a clean build before compiling",
    )
    parser.add_argument(
        "--reconfigure",
        action="store_true",
        help="Force a CMake configuration pass even when the cache exists",
    )
    parser.add_argument(
        "--configure-only",
        action="store_true",
        help="Run the configuration step without invoking cmake --build",
    )

    return parser.parse_args(list(argv))


def main(argv: Iterable[str] | None = None) -> int:
    args = parse_args(argv or sys.argv[1:])
    build_dir = Path(args.build_dir)

    cache = build_dir / "CMakeCache.txt"
    need_configure = args.reconfigure or not cache.exists()
    resolved_generator: str | None = None

    if need_configure:
        build_dir.mkdir(parents=True, exist_ok=True)
        resolved_generator = configure(build_dir, args)
    else:
        print(f"Skipping configure step; found existing cache at {cache}")
        resolved_generator = args.generator or _generator_from_cache(cache)

    if args.configure_only:
        return 0

    build(build_dir, args, resolved_generator)
    return 0


if __name__ == "__main__":
    sys.exit(main())

