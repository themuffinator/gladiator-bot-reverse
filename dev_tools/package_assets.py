#!/usr/bin/env python3
"""Package Gladiator assets into Quake II compatible PAK archives.

This script mirrors the classic Gladiator release workflow by bundling the
contents of the ``dist/`` tree into ``pak`` archives. The behaviour is driven
by a JSON manifest (``dist/pak_manifest.json`` by default) so additional
packages can be produced without modifying the script.
"""
from __future__ import annotations

import argparse
import fnmatch
import json
import pathlib
import struct
import sys
from typing import Dict, Iterator, List, Sequence, Tuple

PAK_HEADER_MAGIC = b"PACK"
PAK_DIRECTORY_ENTRY_SIZE = 64
PAK_NAME_FIELD_SIZE = 56
DEFAULT_CHUNK_SIZE = 64 * 1024


class PakBuildError(RuntimeError):
    """Raised when packaging cannot complete."""


def _normalise_path(path: pathlib.Path) -> str:
    return str(path.as_posix())


def _iter_source_files(
    root: pathlib.Path,
    include: Sequence[str],
    exclude: Sequence[str],
) -> Iterator[pathlib.Path]:
    def matches_any(relative: str, patterns: Sequence[str]) -> bool:
        return any(fnmatch.fnmatch(relative, pattern) for pattern in patterns)

    for path in sorted(root.rglob("*")):
        if not path.is_file():
            continue

        relative = _normalise_path(path.relative_to(root))
        if include and not matches_any(relative, include):
            continue
        if exclude and matches_any(relative, exclude):
            continue
        yield path


def _relative_name(root: pathlib.Path, file_path: pathlib.Path) -> str:
    relative = file_path.relative_to(root)
    normalised = relative.as_posix()
    if ".." in pathlib.PurePosixPath(normalised).parts:
        raise PakBuildError(f"Refusing to package path outside root: {normalised}")
    if len(normalised.encode("ascii", "ignore")) != len(normalised):
        raise PakBuildError(f"Asset names must be ASCII: {normalised}")
    if len(normalised) >= PAK_NAME_FIELD_SIZE:
        raise PakBuildError(
            f"Asset name '{normalised}' exceeds {PAK_NAME_FIELD_SIZE - 1} characters"
        )
    return normalised


def _write_pak(output_path: pathlib.Path, entries: Sequence[Tuple[str, pathlib.Path]]) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)

    temp_path = output_path.with_suffix(output_path.suffix + ".tmp")
    directory: List[Tuple[str, int, int]] = []

    with temp_path.open("wb") as stream:
        stream.write(PAK_HEADER_MAGIC)
        stream.write(struct.pack("<ii", 0, 0))

        for name, source_path in entries:
            offset = stream.tell()
            with source_path.open("rb") as source:
                while True:
                    chunk = source.read(DEFAULT_CHUNK_SIZE)
                    if not chunk:
                        break
                    stream.write(chunk)
            length = stream.tell() - offset
            directory.append((name, offset, length))

        directory_offset = stream.tell()
        for name, offset, length in directory:
            encoded = name.encode("ascii")
            padded = encoded + b"\0" * (PAK_NAME_FIELD_SIZE - len(encoded))
            stream.write(padded)
            stream.write(struct.pack("<ii", offset, length))

        stream.seek(4)
        stream.write(struct.pack("<ii", directory_offset, len(directory) * PAK_DIRECTORY_ENTRY_SIZE))

    temp_path.replace(output_path)


def _build_package(manifest_dir: pathlib.Path, package: Dict[str, object]) -> None:
    try:
        name = str(package["name"])
        source = str(package.get("source", ""))
    except KeyError as exc:
        raise PakBuildError(f"Missing required manifest key: {exc}") from exc

    include = package.get("include", ["**/*"])
    exclude = package.get("exclude", [])

    if not isinstance(include, Sequence) or not isinstance(exclude, Sequence):
        raise PakBuildError("'include' and 'exclude' must be sequence types")

    root = (manifest_dir / source).resolve()
    if not root.exists():
        raise PakBuildError(f"Source directory does not exist: {root}")

    files = list(_iter_source_files(root, include, exclude))
    if not files:
        raise PakBuildError(f"No files matched for package '{name}' in {root}")

    entries = [(_relative_name(root, path), path) for path in files]
    output_path = (manifest_dir / name).resolve()

    print(f"[pak] building {output_path} from {len(entries)} assets")
    _write_pak(output_path, entries)


def load_manifest(manifest_path: pathlib.Path) -> Dict[str, object]:
    with manifest_path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def run(manifest_path: pathlib.Path) -> None:
    manifest_data = load_manifest(manifest_path)
    packages = manifest_data.get("packages")
    if not isinstance(packages, Sequence):
        raise PakBuildError("Manifest must define a 'packages' array")

    base_dir = manifest_data.get("base_dir")
    manifest_dir = manifest_path.parent if base_dir is None else (manifest_path.parent / str(base_dir))
    manifest_dir = manifest_dir.resolve()

    for package in packages:
        if not isinstance(package, dict):
            raise PakBuildError("Each package entry must be an object")
        _build_package(manifest_dir, package)


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Build Gladiator PAK archives")
    parser.add_argument(
        "manifest",
        nargs="?",
        default=pathlib.Path("dist/pak_manifest.json"),
        type=pathlib.Path,
        help="Path to the packaging manifest",
    )
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv if argv is not None else sys.argv[1:])
    try:
        run(args.manifest)
    except PakBuildError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    except FileNotFoundError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
