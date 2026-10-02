"""Parse an ORC Outcome JSON into a small dataclass: host, duration, failed commands, peak
memory. Port of the PowerShell Get-OrcOutcome logic."""
import json
import os
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
    # How many commands ran at all. The denominator of 'failures': one failure out of two
    # commands and one out of three hundred are not the same collection.
    commands: int = 0


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
    commands = 0
    peak = 0
    peak_set = ""
    for cmd_set in o.get("command_set", []):
        mp = cmd_set.get("statistics", {}).get("process_memory_peak", 0) or 0
        if mp > peak:
            peak, peak_set = mp, cmd_set.get("name", "")
        for cmd in cmd_set.get("commands", []):
            commands += 1
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
                   memory_peak_bytes=peak, memory_peak_set=peak_set, commands=commands)


def read_outcome(path: str) -> dict | None:
    """The 'dfir-orc.outcome' object of a JSON file, or None if this is not an Outcome."""
    try:
        with open(path, encoding="utf-8") as f:
            data = json.load(f)
    except (OSError, ValueError):
        return None

    outcome = data.get("dfir-orc", {}).get("outcome")
    return outcome if isinstance(outcome, dict) else None


def _io(counters: dict, name: str):
    return (counters or {}).get(name, "")


def command_rows(outcome: dict, source: str) -> list[dict]:
    """One row per command executed, across every command set of one run.

    This is the table that answers fleet questions - which command fails, on which machine,
    under which build - by joining to the Outline table on run_id, which the two share.
    """
    run = {
        "run_id": outcome.get("id", ""),
        "computer_name": outcome.get("computer_name", ""),
        "system_type": outcome.get("system_type", ""),
        "timestamp": outcome.get("timestamp", ""),
        "run_start": outcome.get("start", ""),
    }

    rows = []
    for cmd_set in outcome.get("command_set", []) or []:
        statistics = cmd_set.get("statistics") or {}
        archive = cmd_set.get("archive") or {}
        for command in cmd_set.get("commands", []) or []:
            code = _exit_int(command.get("exit_code"))
            counters = command.get("io_counters") or {}
            origin = command.get("origin") or {}
            start, end = command.get("start", ""), command.get("end", "")

            rows.append({
                **run,
                "command_set": cmd_set.get("name", ""),
                "set_start": cmd_set.get("start", ""),
                "set_memory_peak": statistics.get("process_memory_peak", ""),
                "set_process_count": statistics.get("process", ""),
                "archive_name": archive.get("name", ""),
                "archive_size": archive.get("size", ""),
                "archive_sha1": archive.get("sha1", ""),
                "archive_input_type": archive.get("input_type", ""),

                "command": command.get("name", ""),
                "tool": command.get("tool", ""),
                "exit_code": str(command.get("exit_code", "")),
                # Both: the raw value is what ORC reported, the int is what you filter on.
                "exit_code_int": "" if code is None else code,
                "ok": "true" if code == 0 else "false",
                "start": start,
                "end": end,
                "duration_seconds": _seconds(start, end),
                "pid": command.get("pid", ""),
                "sha1": command.get("sha1", ""),
                "origin_name": origin.get("friendly_name", ""),
                "origin_type": origin.get("type", ""),
                "user_time": command.get("user_time", ""),
                "kernel_time": command.get("kernel_time", ""),
                "read_operation": _io(counters, "read_operation"),
                "read_transfer": _io(counters, "read_transfer"),
                "write_operation": _io(counters, "write_operation"),
                "write_transfer": _io(counters, "write_transfer"),
                "output_count": len(command.get("output") or []),
                "command_line": command.get("command_line", ""),

                "source": source.replace("\\", "/"),
                "json": json.dumps(command, separators=(",", ":")),
            })
    return rows


def outcome_file(run_dir: str) -> str | None:
    """The Outcome ingest captured for a run, if it had one.

    Found by content, not by name: it sits in the run under the name ORC gave it, beside the
    Outline, which is also a '*.json' carrying the collection's name.
    """
    from .ingest import find_outcome_json
    return find_outcome_json(run_dir)


def collect_command_rows(run_dirs) -> list[dict]:
    rows = []
    for run_dir in run_dirs:
        path = outcome_file(run_dir)
        outcome = read_outcome(path) if path else None
        if outcome is not None:
            rows.extend(command_rows(outcome, path))
    return rows


def run_outcome(run_dirs, root=None, out=None, engine_name="duckdb", as_json=False) -> None:
    from .engine import get_engine, rows_to_parquet, single_process_memory_limit
    from .report import discover_run_dirs

    dirs = discover_run_dirs(list(run_dirs or []), root)
    if not dirs:
        raise RuntimeError("no run dirs (need an ingested run dir, or --root)")

    rows = collect_command_rows(dirs)
    if not rows:
        raise RuntimeError(f"no Outcome captured in {len(dirs)} run dir(s)")

    if as_json:
        print(json.dumps([{k: v for k, v in row.items() if k != "json"} for row in rows],
                         indent=2))
        return

    out = out or os.path.join(dirs[0], "outcome.parquet")
    rows_to_parquet(rows, out, get_engine(engine_name, single_process_memory_limit()))

    failed = [row for row in rows if row["ok"] == "false"]
    print(f"{out}: {len(rows)} command(s) from {len(dirs)} run(s), {len(failed)} failed")
    for row in failed[:10]:
        print(f"  {row['computer_name'] or '?':16s} {row['command_set']}/{row['command']} "
              f"(code: {row['exit_code']})")
