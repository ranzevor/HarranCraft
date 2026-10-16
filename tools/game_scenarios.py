"""Required client-world scenario evidence and explicit LAN-test selection."""

from __future__ import annotations

import re

CLIENT_SCENARIOS = frozenset(
    {
        "block-roundtrip",
        "reconnect",
        "block-entity-preservation",
        "thread-ownership",
        "world-stop",
        "world-reopen",
        "world-replacement",
    }
)
LAN_SCENARIOS = frozenset({"lan-revocation", "lan-recovery"})


def lan_requested(properties: dict[str, str], *, enabled: bool, client: bool) -> bool:
    value = properties.get("runLanTests", "false")
    if value not in {"true", "false"}:
        raise ValueError("lan_option_invalid")
    selected = value == "true"
    if selected and not (enabled and client):
        raise ValueError("lan_client_opt_in_required")
    return selected


def client_results(output: str, *, lan: bool) -> dict:
    records = re.findall(r"(?m)(?:^|[ \t])HARRANCRAFT_SCENARIO ([a-z-]+)[ \t\r]*$", output)
    if len(records) > 64:
        raise ValueError("client_scenario_limit_exceeded")
    required = CLIENT_SCENARIOS | LAN_SCENARIOS if lan else CLIENT_SCENARIOS
    completed = set(records)
    missing = sorted(required - completed)
    unexpected = sorted(completed - required)
    duplicates = sorted(name for name in completed if records.count(name) != 1)
    return {
        "status": "failed" if missing or unexpected or duplicates else "passed",
        "passed": sorted(completed & required),
        "missing": missing,
        "unexpected": unexpected,
        "duplicates": duplicates,
        "lan": "requested" if lan else "not_requested",
    }
