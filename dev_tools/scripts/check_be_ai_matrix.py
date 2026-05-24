#!/usr/bin/env python3
"""Validate docs/be_ai_parity_matrix.md for status and schedule coverage."""

from __future__ import annotations

import pathlib
import re
import sys
from typing import Dict, List, Sequence

ALLOWED_STATUSES = {"implemented", "missing", "divergent", "stub"}
DOC_PATH = pathlib.Path(__file__).resolve().parents[2] / "docs" / "be_ai_parity_matrix.md"


def _parse_table(lines: Sequence[str]) -> List[Dict[str, str]]:
    entries: List[Dict[str, str]] = []
    header_re = re.compile(r"^\|\s*Function\s*\|\s*Retail reference\s*\|")
    in_table = False
    for line in lines:
        if header_re.match(line):
            in_table = True
            continue
        if in_table:
            if not line.strip().startswith("|"):
                in_table = False
                continue
            columns = [col.strip() for col in line.strip().split("|")]
            if len(columns) < 5:
                continue
            function, retail_ref, status, acceptance = columns[1:5]
            if function.lower() == "function":
                continue
            dashed = function.replace("-", "").strip()
            if not dashed:
                continue
            entries.append(
                {
                    "function": function,
                    "status": status.lower(),
                    "acceptance": acceptance,
                    "retail": retail_ref,
                }
            )
    return entries


def _parse_schedule(lines: Sequence[str]) -> tuple[bool, List[str]]:
    found_header = False
    schedule: List[str] = []
    capture = False
    for line in lines:
        if line.startswith("## Remediation schedule"):
            capture = True
            found_header = True
            continue
        if capture:
            if line.startswith("## "):
                break
            if line.strip().startswith("- ["):
                schedule.append(line.strip())
    return found_header, schedule


def _normalise_function_names(name: str) -> List[str]:
    parts = re.split(r"[/,()]", name)
    results: List[str] = []
    for part in parts:
        clean = part.strip()
        if not clean:
            continue
        clean = clean.replace("`", "")
        results.append(clean)
    return results


def main() -> int:
    if not DOC_PATH.is_file():
        print(f"Missing matrix document: {DOC_PATH}", file=sys.stderr)
        return 1

    lines = DOC_PATH.read_text(encoding="utf-8").splitlines()
    entries = _parse_table(lines)
    has_schedule_section, schedule = _parse_schedule(lines)

    errors: List[str] = []
    for entry in entries:
        if entry["status"] not in ALLOWED_STATUSES:
            errors.append(
                f"Unsupported status '{entry['status']}' for {entry['function']}"
            )
        if not entry["acceptance"] or entry["acceptance"].lower() == "tbd":
            errors.append(f"Acceptance criteria missing for {entry['function']}")

    if has_schedule_section:
        schedule_text = " ".join(schedule).lower()
        for entry in entries:
            if entry["status"] == "implemented":
                continue
            for func in _normalise_function_names(entry["function"]):
                if func.lower() not in schedule_text:
                    errors.append(
                        f"Remediation schedule missing coverage for '{func}'"
                    )
    else:
        errors.append("Remediation schedule section not found")

    if errors:
        for err in errors:
            print(err, file=sys.stderr)
        return 1

    print(f"Validated {len(entries)} matrix entries with schedule coverage for non-implemented items.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
