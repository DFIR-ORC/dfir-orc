"""Parse an ORC Outcome JSON into a small dataclass: host, duration, failed commands, peak
memory. Port of the PowerShell Get-OrcOutcome logic."""
import json
from dataclasses import dataclass
from datetime import datetime


@dataclass
class Failure:
    set: str
    command: str
    exit_code: str
    duration_seconds: float
    start: str


@dataclass
class Outcome:
    host: str
    start: str
    end: str
    duration_seconds: float
    failures: list
    memory_peak_bytes: int
    memory_peak_set: str


def _seconds(start: str, end: str) -> float:
    try:
        a = datetime.fromisoformat(start.replace("Z", "+00:00"))
        b = datetime.fromisoformat(end.replace("Z", "+00:00"))
        return (b - a).total_seconds()
    except (ValueError, AttributeError):
        return 0.0


def _exit_int(code) -> int | None:
    if isinstance(code, int):
        return code
    if isinstance(code, str):
        try:
            return int(code, 16) if code.lower().startswith("0x") else int(code)
        except ValueError:
            return None
    return None


def parse_outcome(path: str) -> Outcome:
    with open(path, encoding="utf-8") as f:
        data = json.load(f)
    o = data.get("dfir-orc", {}).get("outcome", {})

    failures = []
    peak = 0
    peak_set = ""
    for cmd_set in o.get("command_set", []):
        mp = cmd_set.get("statistics", {}).get("process_memory_peak", 0) or 0
        if mp > peak:
            peak, peak_set = mp, cmd_set.get("name", "")
        for cmd in cmd_set.get("commands", []):
            if "exit_code" not in cmd:
                continue
            code = _exit_int(cmd["exit_code"])
            if code is None or code != 0:  # unparseable or non-zero = failure
                failures.append(Failure(
                    set=cmd_set.get("name", ""),
                    command=cmd.get("name", ""),
                    exit_code=str(cmd["exit_code"]),
                    duration_seconds=_seconds(cmd.get("start", ""), cmd.get("end", "")),
                    start=cmd.get("start", ""),
                ))

    start, end = o.get("start", ""), o.get("end", "")
    return Outcome(host=o.get("computer_name", ""), start=start, end=end,
                   duration_seconds=_seconds(start, end), failures=failures,
                   memory_peak_bytes=peak, memory_peak_set=peak_set)
