#!/usr/bin/env python3
"""Package a built Gladiator botlib module together with its documentation.

Produces the archive the release workflow uploads, e.g.

    q2-gladiator-botlib-reconstruction-1.0.0-win32.zip
      q2-gladiator-botlib-reconstruction-1.0.0-win32/
        gladiator.dll
        VERSION.txt
        README.md
        CHANGELOG.md
        LICENSE            (when present)
        docs/...

The same script runs locally and in CI so a hand-built archive matches a
released one:

    python tools/package_release.py --binary build-x86/gladiator.dll --platform win32
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
)


def read_reconstruction_version() -> str:
    """Return GLADIATOR_RECON_VERSION as declared in the CMake module."""
    text = VERSION_CMAKE.read_text(encoding="utf-8")
    match = re.search(r'set\(GLADIATOR_RECON_VERSION\s+"([^"]+)"', text)
    if not match:
        raise SystemExit(f"could not find GLADIATOR_RECON_VERSION in {VERSION_CMAKE}")
    return match.group(1)


def read_legacy_botlib_version() -> str:
    text = VERSION_CMAKE.read_text(encoding="utf-8")
    match = re.search(r'set\(GLADIATOR_LEGACY_BOTLIB_VERSION\s+"([^"]+)"', text)
    if not match:
        raise SystemExit(f"could not find GLADIATOR_LEGACY_BOTLIB_VERSION in {VERSION_CMAKE}")
    return match.group(1)


def git_commit() -> str:
    try:
        out = subprocess.run(
            ["git", "rev-parse", "--short=12", "HEAD"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=True,
        )
        return out.stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return "unknown"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def build_version_manifest(
    version: str, platform: str, binary: Path, commit: str
) -> str:
    legacy = read_legacy_botlib_version()
    built = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%S UTC")
    return "\n".join(
        (
            "Q2 Gladiator Bot Botlib Reconstruction",
            "=" * 38,
            "",
            f"Reconstruction version : {version}",
            f"Legacy botlib version  : {legacy}  (reported to the host as \"BotLib v{legacy}\")",
            f"Platform               : {platform}",
            f"Module                 : {binary.name}",
            f"SHA-256                : {sha256(binary)}",
            f"Source commit          : {commit}",
            f"Built                  : {built}",
            "",
            "The two version numbers are independent.  The reconstruction version",
            "describes this project; the legacy botlib version describes the retail",
            "module being reconstructed and is what the Gladiator mod sees at",
            "runtime.  See docs/reconstruction_versioning.md.",
            "",
            "This reconstruction is dedicated to Mr Elusive, who wrote the original",
            "Gladiator Bot and the Quake III Arena bot library it grew into.",
            "",
        )
    )


def collect_docs() -> list[tuple[Path, str]]:
    """Return (source path, archive-relative path) pairs for the docs tree."""
    entries: list[tuple[Path, str]] = []
    docs_root = REPO_ROOT / "docs"
    if not docs_root.is_dir():
        raise SystemExit(f"documentation directory not found: {docs_root}")

    for path in sorted(docs_root.rglob("*.md")):
        entries.append((path, path.relative_to(REPO_ROOT).as_posix()))

    if not entries:
        raise SystemExit(f"no documentation found under {docs_root}")
    return entries


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--binary", required=True, type=Path,
                        help="built module (gladiator.dll or libgladiator.so)")
    parser.add_argument("--platform", required=True,
                        help="platform tag used in the archive name, e.g. win32, win64, linux64")
    parser.add_argument("--version", default=None,
                        help="reconstruction version; defaults to the value in "
                             "cmake/ReconstructionVersion.cmake")
    parser.add_argument("--output-dir", type=Path, default=REPO_ROOT / "dist" / "release",
                        help="directory to write the archive into")
    args = parser.parse_args()

    binary: Path = args.binary
    if not binary.is_file():
        raise SystemExit(f"module not found: {binary}")

    version = args.version or read_reconstruction_version()
    commit = git_commit()
    stem = f"{ARCHIVE_STEM}-{version}-{args.platform}"

    args.output_dir.mkdir(parents=True, exist_ok=True)
    archive_path = args.output_dir / f"{stem}.zip"

    with zipfile.ZipFile(archive_path, "w", zipfile.ZIP_DEFLATED) as archive:
        archive.write(binary, f"{stem}/{binary.name}")
        archive.writestr(
            f"{stem}/VERSION.txt",
            build_version_manifest(version, args.platform, binary, commit),
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
    print(f"  module   : {binary.name} ({binary.stat().st_size:,} bytes)")
    print(f"  sha256   : {sha256(archive_path)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
