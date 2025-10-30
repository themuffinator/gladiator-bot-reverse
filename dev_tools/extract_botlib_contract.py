#!/usr/bin/env python3
"""Extract the botlib diagnostic contract from the HLIL export.

The generated JSON catalogue captures the user-visible messages, guard return
codes, and configuration defaults observed in the original Gladiator botlib
binary.  Parity tests consume the catalogue to assert that the reconstructed
implementation emits the same diagnostics.
"""

from __future__ import annotations

import argparse
import json
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, Iterable, List, Optional


HLIL_PATH = Path("dev_tools/gladiator.dll.bndb_hlil.txt")
DEFAULT_OUTPUT = Path("tests/reference/botlib_contract.json")


@dataclass
class MessageRecord:
    address: str
    severity: int
    text: str
    source: str


@dataclass
class ReturnRecord:
    address: str
    value: int
    source: str


@dataclass
class FunctionRecord:
    name: str
    address: str
    category: str
    messages: List[MessageRecord] = field(default_factory=list)
    return_codes: List[ReturnRecord] = field(default_factory=list)


@dataclass
class LibVarDefault:
    address: str
    name: str
    fallback: str
    fallback_source: str
    source: str


def decode_c_string(literal: str) -> str:
    """Convert a C string literal body into the corresponding Python string."""

    # Binary Ninja emits escape sequences that match C syntax.  Using the
    # unicode_escape codec mirrors how the binary would interpret them.  Some
    # literals contain UTF-8 sequences (such as ellipses) that are split into
    # extended ASCII bytes by the intermediate decode, so round trip through
    # latin-1 to rebuild the original characters when possible.
    intermediate = bytes(literal, "utf-8").decode("unicode_escape")
    try:
        return intermediate.encode("latin-1").decode("utf-8")
    except UnicodeEncodeError:
        return intermediate


def parse_data_strings(lines: List[str]) -> Dict[str, str]:
    """Extract string contents from the data section of the HLIL dump."""

    data_strings: Dict[str, str] = {}

    char_re = re.compile(
        r"^[0-9a-f]{8}\s+char\s+(data_[0-9a-f]+)\[[^\]]*\]\s*=\s+\"((?:\\.|[^\"])*)\""
    )
    label_re = re.compile(r"^[0-9a-f]{8}\s+(data_[0-9a-f]+):")
    hex_re = re.compile(r"^[0-9a-f]{8}\s+((?:[0-9a-f]{2} ){1,16})(?:\s+.*)?$")

    for line in lines:
        match = char_re.match(line)
        if match:
            label, literal = match.groups()
            data_strings[label] = decode_c_string(literal)

    for idx, line in enumerate(lines):
        match = label_re.match(line)
        if not match:
            continue

        label = match.group(1)
        if label in data_strings:
            continue

        byte_values: List[int] = []
        cursor = idx + 1
        while cursor < len(lines):
            next_line = lines[cursor]
            hex_match = hex_re.match(next_line)
            if not hex_match:
                break

            byte_values.extend(int(token, 16) for token in hex_match.group(1).split())
            cursor += 1

        # Convert to ASCII if the bytes resemble a null-terminated string.
        chars: List[str] = []
        for value in byte_values:
            if value == 0:
                break
            if value in (0x09, 0x0a) or 0x20 <= value <= 0x7E:
                chars.append(chr(value))
            else:
                chars = []
                break

        if chars:
            data_strings[label] = "".join(chars)

    return data_strings


def resolve_message_text(raw_literal: str, data_strings: Dict[str, str]) -> str:
    """Expand truncated literals using the data-section catalogue when possible."""

    text = decode_c_string(raw_literal)
    if "…" not in text:
        return text

    prefix = text.split("…", 1)[0]
    candidates = [value for value in data_strings.values() if value.startswith(prefix)]
    if len(candidates) == 1:
        return candidates[0]
    return text


def build_function_index() -> Dict[str, FunctionRecord]:
    """Define the botlib exports and helper guards that emit diagnostics."""

    exports = {
        "100379e0": ("BotVersion", "export"),
        "10037bb0": ("BotLibSetup", "export"),
        "10037cf0": ("BotLibShutdown", "export"),
        "1000dee0": ("BotLibraryInitialized", "export"),
        "10037da0": ("BotLibVarSet", "export"),
        "10037dd0": ("BotLibAddGlobalDefine", "export"),
        "10037e10": ("BotLibLoadMap", "export"),
        "10037f00": ("BotLibSetupClient", "export"),
        "10037f70": ("BotLibShutdownClient", "export"),
        "10037fe0": ("BotLibMoveClient", "export"),
        "10038070": ("BotLibClientSettings", "export"),
        "100380e0": ("BotLibSettings", "export"),
        "10038150": ("BotLibStartFrame", "export"),
        "10038190": ("BotLibUpdateClient", "export"),
        "10038200": ("BotLibUpdateEntity", "export"),
        "10038270": ("BotLibUpdateSound", "export"),
        "100382f0": ("BotLibAddPointLight", "export"),
        "10038380": ("BotLibAI", "export"),
        "100383f0": ("BotLibConsoleMessage", "export"),
        "10038460": ("BotLibTestHook", "export"),
    }

    helpers = {
        "100379a0": ("GuardLibrarySetup", "helper"),
        "10037900": ("GuardClientNumber", "helper"),
        "10037950": ("GuardEntityNumber", "helper"),
        "1000df00": ("AASInitialisedLog", "helper"),
        "1000df30": ("AASWriteWorldLog", "helper"),
    }

    index: Dict[str, FunctionRecord] = {}
    for address, (name, category) in {**exports, **helpers}.items():
        index[address] = FunctionRecord(name=name, address=f"0x{address}", category=category)
    return index


def parse_functions(
    lines: List[str],
    data_strings: Dict[str, str],
    index: Dict[str, FunctionRecord],
) -> None:
    """Populate message and return metadata for the indexed functions."""

    function_start_re = re.compile(
        r"^([0-9a-f]{8})\s+[A-Za-z_][^=]*\s+(?:sub|j_sub|GetBotAPI)_([0-9a-f]+)\([^)]*\)\s*$"
    )
    message_re = re.compile(
        r"^([0-9a-f]{8})\s+(?:return\s+)?data_10063fe8\((\d+),\s+\"((?:\\.|[^\"])*)\""
    )
    return_re = re.compile(r"^([0-9a-f]{8})\s+return\s+(-?\d+)\b")
    string_assign_re = re.compile(
        r"^([0-9a-f]{8})\s+([A-Za-z_][\w]*)\s*=\s+\"((?:\\.|[^\"])*)\""
    )
    value_assign_re = re.compile(r"^([0-9a-f]{8})\s+([A-Za-z_][\w]*)\s*=\s+(\d+)\b")
    var_call_re = re.compile(
        r"^([0-9a-f]{8})\s+data_10063fe8\(([A-Za-z_][\w]*),\s+([A-Za-z_][\w]*)"
    )

    address_lookup = {addr: record for addr, record in index.items()}

    current: Optional[FunctionRecord] = None
    current_address: Optional[str] = None
    local_strings: Dict[str, List[MessageRecord]] = {}
    local_values: Dict[str, List[ReturnRecord]] = {}

    for line in lines:
        start_match = function_start_re.match(line)
        if start_match:
            tokens = line.split()
            if len(tokens) > 1 and tokens[1] in {"return", "if", "while", "for", "goto", "do", "else"}:
                continue
            address, impl_address = start_match.groups()
            target = address_lookup.get(address)
            if target is None:
                # Tail-call wrappers (j_sub_*) reference the implementation
                # address in the suffix; consult that before giving up.
                target = address_lookup.get(impl_address)
                current_address = impl_address if target else None
            else:
                current_address = address
            current = target
            local_strings = {}
            local_values = {}
            continue

        if current is None or current_address is None:
            continue

        message_match = message_re.match(line)
        if message_match:
            address, severity_text, literal = message_match.groups()
            message = MessageRecord(
                address=f"0x{address}",
                severity=int(severity_text),
                text=resolve_message_text(literal, data_strings),
                source=line.strip(),
            )
            current.messages.append(message)
            continue

        string_match = string_assign_re.match(line)
        if string_match:
            address, var_name, literal = string_match.groups()
            local_strings.setdefault(var_name, []).append(
                MessageRecord(
                    address=f"0x{address}",
                    severity=0,
                    text=resolve_message_text(literal, data_strings),
                    source=line.strip(),
                )
            )
            continue

        value_match = value_assign_re.match(line)
        if value_match:
            address, var_name, value_text = value_match.groups()
            local_values.setdefault(var_name, []).append(
                ReturnRecord(
                    address=f"0x{address}", value=int(value_text), source=line.strip()
                )
            )
            continue

        var_call_match = var_call_re.match(line)
        if var_call_match:
            address, severity_var, text_var = var_call_match.groups()
            templates = local_strings.get(text_var) or []
            severities = local_values.get(severity_var) or []
            for template, severity in zip(templates, severities):
                current.messages.append(
                    MessageRecord(
                        address=f"0x{address}",
                        severity=severity.value,
                        text=template.text,
                        source=line.strip(),
                    )
                )
            continue

        return_match = return_re.match(line)
        if return_match:
            address, value_text = return_match.groups()
            record = ReturnRecord(
                address=f"0x{address}", value=int(value_text), source=line.strip()
            )
            current.return_codes.append(record)


def parse_libvar_defaults(
    lines: List[str], data_strings: Dict[str, str]
) -> List[LibVarDefault]:
    """Capture the cached libvar defaults seeded during BotLibSetup."""

    target_address = "10037a00"
    defaults: List[LibVarDefault] = []

    assignment_re = re.compile(
        r"^([0-9a-f]{8})\s+data_100640[0-9a-f]{2} = j_sub_100389c0\(\"([^\"]+)\",\s+([^\)]+)\)"
    )

    try:
        start_index = next(
            idx
            for idx, line in enumerate(lines)
            if line.startswith(f"{target_address}    ")
        )
    except StopIteration:
        return defaults

    started = False
    for line in lines[start_index + 1 :]:
        if line.strip() == "":
            if started:
                break
            continue
        started = True

        match = assignment_re.match(line)
        if not match:
            alt_match = re.match(
                r"^([0-9a-f]{8})\s+int32_t\*\s+\w+ = j_sub_100389c0\(\"([^\"]+)\",\s+([^\)]+)\)",
                line,
            )
            if not alt_match:
                continue
            match = alt_match

        address, name, fallback_expr = match.groups()
        fallback_expr = fallback_expr.strip()
        fallback_value: Optional[str] = None

        literal_match = re.match(r'\"((?:\\.|[^\"])*)\"', fallback_expr)
        if literal_match:
            fallback_value = decode_c_string(literal_match.group(1))
        else:
            ref_match = re.match(r"&?(data_[0-9a-f]+)", fallback_expr)
            if ref_match:
                fallback_value = data_strings.get(ref_match.group(1))

        defaults.append(
            LibVarDefault(
                address=f"0x{address}",
                name=name,
                fallback=fallback_value or fallback_expr,
                fallback_source=fallback_expr,
                source=line.strip(),
            )
        )

    return defaults


def serialise(records: Iterable[FunctionRecord]) -> List[Dict[str, object]]:
    payload: List[Dict[str, object]] = []
    for record in records:
        payload.append(
            {
                "name": record.name,
                "address": record.address,
                "category": record.category,
                "messages": [
                    {
                        "address": message.address,
                        "severity": message.severity,
                        "text": message.text,
                        "source": message.source,
                    }
                    for message in record.messages
                ],
                "return_codes": [
                    {
                        "address": ret.address,
                        "value": ret.value,
                        "source": ret.source,
                    }
                    for ret in record.return_codes
                ],
            }
        )
    return payload


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--hlil",
        type=Path,
        default=HLIL_PATH,
        help="Path to dev_tools/gladiator.dll.bndb_hlil.txt",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=DEFAULT_OUTPUT,
        help="Destination JSON file",
    )
    args = parser.parse_args()

    lines = args.hlil.read_text(encoding="utf-8").splitlines()
    data_strings = parse_data_strings(lines)
    index = build_function_index()
    parse_functions(lines, data_strings, index)
    libvar_defaults = parse_libvar_defaults(lines, data_strings)

    exports = [record for record in index.values() if record.category == "export"]
    helpers = [record for record in index.values() if record.category == "helper"]

    exports.sort(key=lambda record: record.name)
    helpers.sort(key=lambda record: record.name)

    payload = {
        "metadata": {
            "hlil_source": str(args.hlil),
            "description": "Botlib diagnostic contract extracted from the Gladiator DLL",
        },
        "exports": serialise(exports),
        "helpers": serialise(helpers),
        "libvar_defaults": [
            {
                "address": default.address,
                "name": default.name,
                "fallback": default.fallback,
                "fallback_source": default.fallback_source,
                "source": default.source,
            }
            for default in libvar_defaults
        ],
    }

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(payload, indent=2, sort_keys=True), encoding="utf-8")


if __name__ == "__main__":
    main()
