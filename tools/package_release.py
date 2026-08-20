#!/usr/bin/env python3
"""Package the built Gladiator modules together with their documentation.

Produces the archive the release workflow uploads, e.g.

    q2-gladiator-botlib-reconstruction-1.0.0-win32.zip
      q2-gladiator-botlib-reconstruction-1.0.0-win32/
        gladiator.dll          the reconstructed botlib
        gamex86.dll            Mr Elusive's game module
        INSTALL.txt
        VERSION.txt
        README.md
        CHANGELOG.md
        LICENSE                (when present)
        docs/...

The same script runs locally and in CI so a hand-built archive matches a
released one:

    python tools/package_release.py --platform win32
        --binary build-x86/gladiator.dll
        --binary build-x86/src/game/gamex86.dll
"""

from __future__ import annotations

import argparse
import hashlib
import re
import subprocess
import sys
import zipfile
from datetime import datetime, timezone
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
VERSION_CMAKE = REPO_ROOT / "cmake" / "ReconstructionVersion.cmake"
ARCHIVE_STEM = "q2-gladiator-botlib-reconstruction"

# Repository-relative files copied into every archive.  Missing optional
# entries are skipped; missing required ones abort the run.
DOC_FILES = (
    ("README.md", True),
    ("CHANGELOG.md", True),
    ("LICENSE", False),
    ("NOTICE", False),
)


def _read_cmake_string(name: str) -> str:
    text = VERSION_CMAKE.read_text(encoding="utf-8")
    match = re.search(r"set\(" + re.escape(name) + r'\s+"([^"]+)"', text)
    if not match:
        raise SystemExit(f"could not find {name} in {VERSION_CMAKE}")
    return match.group(1)


def read_reconstruction_version() -> str:
    return _read_cmake_string("GLADIATOR_RECON_VERSION")


def read_legacy_botlib_version() -> str:
    return _read_cmake_string("GLADIATOR_LEGACY_BOTLIB_VERSION")


def git_commit() -> str:
    """Return the source commit, suffixed "-dirty" for an unclean tree.

    This must match what cmake/ReconstructionVersion.cmake bakes into the
    module, or VERSION.txt would report a clean commit for an archive whose
    binaries are stamped dirty.
    """
    try:
        out = subprocess.run(
            ["git", "rev-parse", "--short=12", "HEAD"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=True,
        )
        commit = out.stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return "unknown"

    try:
        dirty = subprocess.run(
            ["git", "diff", "--quiet", "HEAD"],
            cwd=REPO_ROOT,
            capture_output=True,
        )
        if dirty.returncode != 0:
            commit += "-dirty"
    except OSError:
        pass

    return commit


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def describe_module(path: Path) -> str:
    """Say what a module is, from its filename.

    Covers every name the build can emit: gladiator.dll on Windows, and the
    arch-suffixed gladi386.so / gladx86_64.so on Linux (retail's convention),
    against gamex86.dll / gamei386.so / gamex86_64.so for the game module.
    """
    stem = path.stem.lower()
    if stem.startswith(("glad", "libglad")):
        return "reconstructed botlib (bot AI)"
    if stem.startswith(("game", "libgame")):
        return "game module (Quake II mod logic)"
    return "module"


def build_version_manifest(
    version: str, platform: str, modules: list[Path], commit: str
) -> str:
    legacy = read_legacy_botlib_version()
    built = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%S UTC")

    lines = [
        "Q2 Gladiator Bot Botlib Reconstruction",
        "=" * 38,
        "",
        f"Reconstruction version : {version}",
        f"Legacy botlib version  : {legacy}",
        f'                         (reported to the host as "BotLib v{legacy}")',
        f"Platform               : {platform}",
        f"Source commit          : {commit}",
        f"Built                  : {built}",
        "",
        "Modules",
        "-------",
    ]
    for module in modules:
        lines += [
            f"  {module.name}  --  {describe_module(module)}",
            f"    size    : {module.stat().st_size:,} bytes",
            f"    SHA-256 : {sha256(module)}",
        ]
    lines += [
        "",
        "The two version numbers are independent.  The reconstruction version",
        "describes this project; the legacy botlib version describes the retail",
        "module being reconstructed and is what the Gladiator mod sees at",
        "runtime.  See docs/reconstruction_versioning.md.",
        "",
        "The game module is Mr Elusive's own v0.96 game source, built unmodified",
        "apart from the portability fixes marked PORT(...) in the source.  See",
        "docs/game_source_integration.md.",
        "",
        "This reconstruction is dedicated to Mr Elusive, who wrote the original",
        "Gladiator Bot and the Quake III Arena bot library it grew into.",
        "",
    ]
    return "\n".join(lines)


def build_install_notes(platform: str, modules: list[Path]) -> str:
    lines = [
        "Installing the Gladiator Bot",
        "============================",
        "",
        "1. Create a 'gladiator' directory inside your Quake II installation,",
        "   alongside 'baseq2'.",
        "",
        "2. Copy these files into it:",
        "",
    ]
    lines += [f"     {module.name}" for module in modules]
    lines += [
        "",
        "3. Copy the Gladiator asset paks (pak*.pak) into the same directory.",
        "   They are not included here: this archive ships code, not assets.",
        "",
        "4. Launch Quake II with the mod:",
        "",
        "     quake2 +set game gladiator",
        "",
        "The game module loads the botlib at runtime, so both files must be",
        "present and must come from the same build.",
        "",
    ]
    if platform == "win32":
        lines += [
            "This is the 32-bit build, the one retail Quake II can load.",
            "",
        ]
    else:
        lines += [
            "This is a 64-bit build. Retail Quake II cannot load it; use a 64-bit",
            "source port such as Q2PRO or yamagi Quake II.",
            "",
        ]
    return "\n".join(lines)


def collect_docs() -> list[tuple[Path, str]]:
    """Return (source path, archive-relative path) pairs for the docs tree."""
    docs_root = REPO_ROOT / "docs"
    if not docs_root.is_dir():
        raise SystemExit(f"documentation directory not found: {docs_root}")

    entries = [
        (path, path.relative_to(REPO_ROOT).as_posix())
        for path in sorted(docs_root.rglob("*.md"))
    ]
    if not entries:
        raise SystemExit(f"no documentation found under {docs_root}")
    return entries


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument(
        "--binary",
        required=True,
        type=Path,
        action="append",
        dest="binaries",
        help="a built module to ship; repeat the flag for each (botlib and game)",
    )
    parser.add_argument(
        "--platform",
        required=True,
        help="platform tag used in the archive name, e.g. win32, win64, linux64",
    )
    parser.add_argument(
        "--version",
        default=None,
        help="reconstruction version; defaults to the value in "
        "cmake/ReconstructionVersion.cmake",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=REPO_ROOT / "dist" / "release",
        help="directory to write the archive into",
    )
    args = parser.parse_args()

    modules: list[Path] = args.binaries
    for module in modules:
        if not module.is_file():
            raise SystemExit(f"module not found: {module}")

    names = [module.name for module in modules]
    duplicates = sorted({name for name in names if names.count(name) > 1})
    if duplicates:
        raise SystemExit(f"two modules share a filename: {', '.join(duplicates)}")

    version = args.version or read_reconstruction_version()
    commit = git_commit()
    stem = f"{ARCHIVE_STEM}-{version}-{args.platform}"

    args.output_dir.mkdir(parents=True, exist_ok=True)
    archive_path = args.output_dir / f"{stem}.zip"

    with zipfile.ZipFile(archive_path, "w", zipfile.ZIP_DEFLATED) as archive:
        for module in modules:
            archive.write(module, f"{stem}/{module.name}")

        archive.writestr(
            f"{stem}/VERSION.txt",
            build_version_manifest(version, args.platform, modules, commit),
        )
        archive.writestr(
            f"{stem}/INSTALL.txt",
            build_install_notes(args.platform, modules),
        )

        for name, required in DOC_FILES:
            source = REPO_ROOT / name
            if source.is_file():
                archive.write(source, f"{stem}/{name}")
            elif required:
                raise SystemExit(f"required file missing from the repository: {name}")

        for source, relative in collect_docs():
            archive.write(source, f"{stem}/{relative}")

    print(f"{archive_path}")
    print(f"  version  : {version}")
    print(f"  platform : {args.platform}")
    for module in modules:
        print(f"  module   : {module.name} ({module.stat().st_size:,} bytes)")
    print(f"  sha256   : {sha256(archive_path)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
