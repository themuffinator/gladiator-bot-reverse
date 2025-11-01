"""Headless engine log harness utilities for parity testing.

This module parses console output captured from headless parity runs and
compares the extracted events against the HLIL-derived catalogue under
``tests/parity/engine_logs/``.  The helper is designed to run as a standalone
CLI so that CI pipelines (or developers refreshing the reference data) can
perform diffs without pulling additional dependencies.
"""

from __future__ import annotations

from dataclasses import dataclass, field
import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any, Dict, Iterable, List, Optional, Tuple

DEFAULT_CATALOG = Path("tests/parity/engine_logs/catalog.json")
TIMESTAMP_RE = re.compile(
    r"^\s*(?:\[(?P<bracket>[0-9:.]+)\]|(?P<plain>[0-9]+:[0-9:.]+))\s*(?P<message>.*)$"
)
BOT_JOIN_RE = re.compile(
    r"(?i)\bbot\s+(?P<name>[A-Za-z0-9_\-]+)\s+(connected|entered the game|joined)"
)
MAP_LOAD_RE = re.compile(r"(?i)\bmap\s*:\s*(?P<map>[A-Za-z0-9_]+)")
SCORE_RE = re.compile(r"(?i)^score[:\s]+(?P<detail>.*)$")
CHAT_RE = re.compile(r"^(?P<speaker>[^:]+):\s*(?P<text>.+)$")
DIAGNOSTIC_HINT_RE = re.compile(r"(?i)(warning|error|aas_|botlib|bridge|console message)")


@dataclass
class LogEvent:
    """Represents a single parsed log or metadata event."""

    type: str
    text: str
    timestamp: Optional[float] = None
    source: str = "console"
    metadata: Dict[str, Any] = field(default_factory=dict)

    def serialise(self) -> Dict[str, Any]:
        payload: Dict[str, Any] = {
            "type": self.type,
            "text": self.text,
            "source": self.source,
        }
        if self.timestamp is not None:
            payload["timestamp"] = self.timestamp
        if self.metadata:
            payload["metadata"] = self.metadata
        return payload


@dataclass
class ExpectedEvent:
    """Structured expectation from the catalogue."""

    type: str
    match_type: str
    match_value: str
    severity: Optional[str] = None
    timestamp_hint: Optional[float] = None
    raw: Dict[str, Any] = field(default_factory=dict)

    @classmethod
    def from_dict(cls, payload: Dict[str, Any]) -> "ExpectedEvent":
        match = payload.get("match", {})
        if not match:
            raise ValueError("Expected event is missing a match block")
        if len(match) != 1:
            raise ValueError("Match block must contain exactly one selector")
        (match_type, match_value), = match.items()
        return cls(
            type=payload.get("type", "diagnostic"),
            match_type=match_type,
            match_value=str(match_value),
            severity=payload.get("severity"),
            timestamp_hint=payload.get("timestamp_hint"),
            raw=payload,
        )

    def matches(self, event: LogEvent) -> bool:
        if event.type != self.type:
            return False
        text = event.text
        if self.match_type == "exact":
            return text == self.match_value
        if self.match_type == "contains":
            return self.match_value in text
        if self.match_type == "regex":
            return re.search(self.match_value, text) is not None
        raise ValueError(f"Unsupported match type: {self.match_type}")

    def describe(self) -> str:
        return f"{self.type} [{self.match_type}={self.match_value!r}]"


@dataclass
class ComparisonResult:
    matched: List[Tuple[ExpectedEvent, LogEvent]]
    missing: List[ExpectedEvent]
    unexpected: List[LogEvent]
    timestamp_mismatches: List[Tuple[ExpectedEvent, LogEvent, float]]

    @property
    def ok(self) -> bool:
        return not (self.missing or self.unexpected or self.timestamp_mismatches)


def parse_timestamp(token: Optional[str]) -> Optional[float]:
    if not token:
        return None
    token = token.strip()
    if not token:
        return None
    # Support H:MM:SS.mmm, MM:SS.mmm, or SS.mmm
    if ":" in token:
        parts = token.split(":")
        seconds = 0.0
        for part in parts:
            if not part:
                continue
            seconds = seconds * 60 + float(part)
        return seconds
    try:
        return float(token)
    except ValueError:
        return None


def classify_message(message: str) -> str:
    if "BotLib" in message or "Gladiator" in message or re.fullmatch(r"-+", message):
        return "banner"
    if MAP_LOAD_RE.search(message):
        return "map-load"
    if BOT_JOIN_RE.search(message):
        return "bot-join"
    if SCORE_RE.search(message):
        return "score"
    if DIAGNOSTIC_HINT_RE.search(message):
        return "diagnostic"
    if CHAT_RE.search(message):
        return "chat"
    # Fall back to diagnostic so unexpected lines are surfaced.
    return "diagnostic"


def parse_console_output(text: str, source: str = "console") -> List[LogEvent]:
    events: List[LogEvent] = []
    for line_no, raw_line in enumerate(text.splitlines(), start=1):
        line = raw_line.rstrip()
        if not line:
            continue
        timestamp = None
        message = line
        match = TIMESTAMP_RE.match(line)
        if match:
            token = match.group("bracket") or match.group("plain")
            message = match.group("message").strip()
            timestamp = parse_timestamp(token)
        event_type = classify_message(message)
        metadata: Dict[str, Any] = {"line": line_no}
        if event_type == "bot-join":
            join_match = BOT_JOIN_RE.search(message)
            if join_match:
                metadata["bot"] = join_match.group("name")
        elif event_type == "map-load":
            load_match = MAP_LOAD_RE.search(message)
            if load_match:
                metadata["map"] = load_match.group("map")
        elif event_type == "score":
            score_match = SCORE_RE.search(message)
            if score_match:
                metadata["detail"] = score_match.group("detail").strip()
        elif event_type == "chat":
            chat_match = CHAT_RE.search(message)
            if chat_match:
                metadata["speaker"] = chat_match.group("speaker").strip()
                metadata["chat"] = chat_match.group("text").strip()
        events.append(
            LogEvent(
                type=event_type,
                text=message,
                timestamp=timestamp,
                source=source,
                metadata=metadata,
            )
        )
    return events


def parse_demo_metadata(path: Path) -> Tuple[List[LogEvent], Dict[str, Any]]:
    with path.open("r", encoding="utf-8") as handle:
        payload = json.load(handle)
    events: List[LogEvent] = []
    map_name = payload.get("map")
    for tick in payload.get("score_ticks", []):
        text = f"Score: {tick.get('player', 'unknown')} {tick.get('score', '?')}"
        events.append(
            LogEvent(
                type="score-tick",
                text=text,
                timestamp=tick.get("time"),
                source="metadata",
                metadata=tick,
            )
        )
    return events, payload


def load_catalog(path: Path) -> Dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def load_expected_events(catalog: Dict[str, Any], scenario: str) -> List[ExpectedEvent]:
    scenario_block = catalog.get("scenarios", {}).get(scenario)
    if not scenario_block:
        raise KeyError(f"Scenario '{scenario}' not found in catalogue")
    return [ExpectedEvent.from_dict(event) for event in scenario_block.get("events", [])]


def diff_events(
    expected: Iterable[ExpectedEvent],
    actual: Iterable[LogEvent],
    timestamp_tolerance: float = 0.5,
) -> ComparisonResult:
    expected_list = list(expected)
    actual_list = list(actual)
    matched: List[Tuple[ExpectedEvent, LogEvent]] = []
    missing: List[ExpectedEvent] = []
    unexpected: List[LogEvent] = []
    timestamp_mismatches: List[Tuple[ExpectedEvent, LogEvent, float]] = []

    actual_index = 0
    for expected_event in expected_list:
        found = False
        while actual_index < len(actual_list):
            candidate = actual_list[actual_index]
            actual_index += 1
            if expected_event.matches(candidate):
                found = True
                matched.append((expected_event, candidate))
                if (
                    expected_event.timestamp_hint is not None
                    and candidate.timestamp is not None
                ):
                    delta = abs(expected_event.timestamp_hint - candidate.timestamp)
                    if delta > timestamp_tolerance:
                        timestamp_mismatches.append((expected_event, candidate, delta))
                break
            else:
                unexpected.append(candidate)
        if not found:
            missing.append(expected_event)
    # Remaining actual events (if any) are unexpected extras.
    unexpected.extend(actual_list[actual_index:])
    return ComparisonResult(matched, missing, unexpected, timestamp_mismatches)


def render_report(result: ComparisonResult) -> str:
    lines: List[str] = []
    if result.missing:
        lines.append("Missing expected events:")
        for event in result.missing:
            lines.append(f"  - {event.describe()}")
    if result.unexpected:
        lines.append("Unexpected events:")
        for event in result.unexpected:
            timestamp = f"@{event.timestamp:.3f}s" if event.timestamp is not None else "@?"
            lines.append(f"  - {timestamp} {event.type}: {event.text}")
    if result.timestamp_mismatches:
        lines.append("Timestamp mismatches beyond tolerance:")
        for expected, actual, delta in result.timestamp_mismatches:
            hint = expected.timestamp_hint if expected.timestamp_hint is not None else "?"
            actual_ts = actual.timestamp if actual.timestamp is not None else "?"
            lines.append(
                f"  - {expected.describe()} expected @ {hint}, observed {actual_ts} (delta {delta:.3f}s)"
            )
    if not lines:
        lines.append("All events matched the catalogue expectations.")
    return "\n".join(lines)


def build_expected_block(events: Iterable[LogEvent]) -> List[Dict[str, Any]]:
    catalogue_events: List[Dict[str, Any]] = []
    for event in events:
        match_value = event.text
        match_type = "exact"
        if event.type in {"chat", "score", "score-tick"}:
            pattern = match_value
            pattern = re.sub(r"\d+\.\d+", "{FLOAT}", pattern)
            pattern = re.sub(r"\d+", "{INT}", pattern)
            escaped = re.escape(pattern)
            escaped = escaped.replace("\\{FLOAT\\}", r"\\d+(?:\\.\\d+)?")
            escaped = escaped.replace("\\{INT\\}", r"\\d+")
            match_value = escaped
            match_type = "regex"
        catalogue_events.append(
            {
                "type": event.type,
                "match": {match_type: match_value},
                "severity": event.metadata.get("severity"),
                "timestamp_hint": event.timestamp,
                "source": f"refreshed from {event.source}",
            }
        )
    return catalogue_events


def command_compare(args: argparse.Namespace) -> int:
    catalog_path: Path = args.catalog
    scenario: str = args.scenario
    console_path: Path = args.console_log
    metadata_path: Path = args.metadata
    tolerance: float = args.timestamp_tolerance

    catalog = load_catalog(catalog_path)
    expected_events = load_expected_events(catalog, scenario)

    with console_path.open("r", encoding="utf-8") as handle:
        console_text = handle.read()
    console_events = parse_console_output(console_text, source="console")

    metadata_events, metadata_payload = parse_demo_metadata(metadata_path)
    actual_events = sorted(
        console_events + metadata_events,
        key=lambda event: (event.timestamp if event.timestamp is not None else float("inf")),
    )

    result = diff_events(expected_events, actual_events, tolerance)
    report = render_report(result)
    if args.verbose:
        print(report)
    else:
        # Only echo the negative cases unless verbose mode was requested.
        if not result.ok:
            print(report, file=sys.stderr)
    return 0 if result.ok else 1


def command_refresh(args: argparse.Namespace) -> int:
    catalog_path: Path = args.catalog
    scenario: str = args.scenario
    console_path: Path = args.console_log
    metadata_path: Path = args.metadata

    catalog = load_catalog(catalog_path)
    scenarios = catalog.setdefault("scenarios", {})
    scenario_block = scenarios.setdefault(scenario, {})

    with console_path.open("r", encoding="utf-8") as handle:
        console_text = handle.read()
    console_events = parse_console_output(console_text, source="console")
    metadata_events, metadata_payload = parse_demo_metadata(metadata_path)

    # Preserve metadata hints for ease of manual review.
    if metadata_payload:
        scenario_block.setdefault("map", metadata_payload.get("map"))
        scenario_block.setdefault("description", f"Refreshed from {metadata_payload.get('demo', 'capture')}")
        scenario_block.setdefault("build", metadata_payload.get("build"))

    actual_events = sorted(
        console_events + metadata_events,
        key=lambda event: (event.timestamp if event.timestamp is not None else float("inf")),
    )
    scenario_block["events"] = build_expected_block(actual_events)

    with catalog_path.open("w", encoding="utf-8") as handle:
        json.dump(catalog, handle, indent=2, sort_keys=True)
        handle.write("\n")
    if args.verbose:
        print(f"Scenario '{scenario}' refreshed with {len(actual_events)} events.")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--catalog",
        type=Path,
        default=DEFAULT_CATALOG,
        help="Path to the engine log expectations catalogue (default: %(default)s)",
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    compare_parser = subparsers.add_parser(
        "compare", help="Diff captured logs against the expectation catalogue"
    )
    compare_parser.add_argument("scenario", help="Scenario identifier to load from the catalogue")
    compare_parser.add_argument("--console-log", type=Path, required=True, help="Captured console log path")
    compare_parser.add_argument("--metadata", type=Path, required=True, help="Demo metadata JSON path")
    compare_parser.add_argument(
        "--timestamp-tolerance",
        type=float,
        default=0.75,
        help="Tolerance (seconds) applied when comparing timestamps",
    )
    compare_parser.add_argument("--verbose", action="store_true", help="Print the full comparison report")
    compare_parser.set_defaults(func=command_compare)

    refresh_parser = subparsers.add_parser(
        "refresh", help="Regenerate a scenario block from captured logs"
    )
    refresh_parser.add_argument("scenario", help="Scenario identifier to write back into the catalogue")
    refresh_parser.add_argument("--console-log", type=Path, required=True, help="Captured console log path")
    refresh_parser.add_argument("--metadata", type=Path, required=True, help="Demo metadata JSON path")
    refresh_parser.add_argument("--verbose", action="store_true", help="Print a completion message")
    refresh_parser.set_defaults(func=command_refresh)

    return parser


def main(argv: Optional[List[str]] = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
