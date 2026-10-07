"""orca report: per-host views over a run dir (its timing, its Outcome and its logs).
Stdlib only. View builders are pure over parsed inputs."""
import csv
import glob
import hashlib
import json
import os
from concurrent.futures import ThreadPoolExecutor
from dataclasses import asdict
from typing import NamedTuple

from . import fleet as _fleet, fleetdb as _fleetdb, ignores, ingest, layout, logs as _logs
from . import outcome as _outcome, scratch, sevenzip
from .engine import get_engine, single_process_memory_limit, sql_path


def load_timing(directory: str) -> list[dict]:
    path = layout.timing_path(directory)
    rows = []
    if not os.path.exists(path):
        return rows
    with open(path, newline="", encoding="utf-8") as f:
        for r in csv.DictReader(f):
            if r.get("kind") == "parquet":
                rows.append({"name": r["name"], "rows": int(r["rows"]),
                             "bytes": int(r["bytes"]), "seconds": float(r["seconds"])})
    return rows


# The file types ORC writes itself. A zero-byte one means a command produced nothing; a
# zero-byte collected sample is ordinary (the file on disk was empty) and is not reported.
OUTPUT_SUFFIXES = (".log", ".csv", ".xml", ".json", ".txt", ".dmp")


def build_empty_view(timing: list[dict], empty_files: list[str] | None = None,
                     available: bool = True, files_available: bool = True,
                     ignore: list | None = None) -> dict:
    """Empty tables (from the ingest timing) and empty ORC outputs (from the manifest).

    Two inputs, two availability flags, because they go missing independently: a run ingested
    without duckdb has real timing and no manifest, and one flag would relabel one missing
    input as the other. Unlike build_outcome_view, the keys stay present when unavailable -
    a view with two halves cannot collapse to a single {"available": False}.
    """
    patterns = ignore or []
    empty = sorted(t["name"] for t in timing if t["rows"] == 0)
    files = sorted(empty_files or [])
    kept_tables = [name for name in empty if not ignores.matches(name, patterns)]
    kept_files = [name for name in files if not ignores.matches(name, patterns)]
    return {"available": available, "files_available": files_available,
            "empty_tables": kept_tables, "empty_files": kept_files,
            "count": len(kept_tables), "file_count": len(kept_files),
            "tables_ignored": len(empty) - len(kept_tables),
            "files_ignored": len(files) - len(kept_files)}


def empty_output_files(engine, manifest_path: str) -> list[str]:
    """Zero-byte ORC outputs, read from the manifest rather than an expanded tree.

    This is what the PowerShell Test-OrcExpandedResults reports, minus the expansion: the
    manifest already carries every leaf's size, so a 2 GB result set answers the question
    without writing a single file.
    """
    if not os.path.exists(manifest_path):
        return []

    suffixes = " OR ".join(f"lower(relpath) LIKE '%{suffix}'" for suffix in OUTPUT_SUFFIXES)
    sql = (f"SELECT relpath FROM read_parquet('{sql_path(manifest_path)}') "
           f"WHERE CAST(size AS BIGINT) = 0 AND ({suffixes}) ORDER BY relpath")
    try:
        return [row["relpath"] for row in engine.query_rows(sql, 200)]
    except RuntimeError:
        # A run from before manifests existed, or an unreadable one: the rest of the report is
        # still worth printing.
        return []


def top_tables(timing: list[dict]) -> list[dict]:
    """Every table, with its size, rows and time together, ordered by size.

    One list rather than the 'biggest' and 'slowest' pair, which on a real run named the same
    four tables twice. Not a selection: the report folds nothing away, so there is no reason to
    choose which tables the reader is allowed to see.
    """
    return [{"name": t["name"], "bytes": t["bytes"], "rows": t["rows"],
             "seconds": round(t["seconds"], 2)}
            for t in sorted(timing, key=lambda t: t["bytes"], reverse=True)]


def build_stats_view(timing: list[dict], available: bool = True, limit: int = 5) -> dict:
    """Table count, rows and bytes. 'available' is False when there was no timing to read at
    all, which is not the same as an ingest that produced no tables.

    'biggest' and 'slowest' are what the JSON carries, each sorted on its own metric; 'top' is
    the merged list the human report prints as one table.
    """
    biggest = sorted(timing, key=lambda t: t["bytes"], reverse=True)[:limit]
    slowest = sorted(timing, key=lambda t: t["seconds"], reverse=True)[:limit]
    return {
        "available": available,
        "tables": len(timing),
        "total_rows": sum(t["rows"] for t in timing),
        "total_bytes": sum(t["bytes"] for t in timing),
        "biggest": [{"name": t["name"], "bytes": t["bytes"]} for t in biggest],
        "slowest": [{"name": t["name"], "seconds": round(t["seconds"], 2)} for t in slowest],
        "top": top_tables(timing),
    }


def scan_log(path: str, context: int | None = None) -> dict:
    """One log read once, for every view: the level counts, and the messages themselves.

    Read once because a fleet dump holds a log per host and they are not small; the message
    views reshape what this returns rather than opening the files again.
    """
    try:
        with open(path, encoding="utf-8", errors="ignore") as f:
            scan = _logs.scan(f, context)
    except OSError:
        # Unreadable is not clean, and -1 is the count the view has always shown for it.
        return {"error_lines": -1, "warnings": 0, "errors": 0, "criticals": 0,
                "exact": False, "messages": []}

    counts = scan["counts"]
    return {"error_lines": counts["error"] + counts["critical"],
            "warnings": counts["warning"], "errors": counts["error"],
            "criticals": counts["critical"], "exact": scan["exact"],
            "messages": scan["messages"]}


def build_outcome_view(outcome_obj, ignore: list | None = None) -> dict:
    """What the Outcome says. 'ignore' passes over the failures you have already decided about.

    A run whose every failure is expected reads as ok: that is what the ignore list is for, and
    the count of what was passed over goes out with it so nothing is hidden.
    """
    if outcome_obj is None:
        return {"available": False}

    kept = [fl for fl in outcome_obj.failures
            if not ignores.matches_any(ignores.command_names(fl.set, fl.command), ignore or [])]
    return {
        "available": True,
        "ok": len(kept) == 0,
        "commands": outcome_obj.commands,
        "failures": [asdict(fl) for fl in kept],
        "failures_ignored": len(outcome_obj.failures) - len(kept),
        "duration_seconds": outcome_obj.duration_seconds,
        "memory_peak_bytes": outcome_obj.memory_peak_bytes,
        "memory_peak_set": outcome_obj.memory_peak_set,
    }


def run_log_files(directory: str, extra_dirs: list | None = None) -> list[str]:
    """The logs of a run: the ones ingest put in it, plus the expanded 'Errors' container.

    Deliberately not a walk of the whole run directory - it may hold a tree 'orca extract'
    expanded, whose every command log would drown the ones this run is about.

    'extra_dirs' is where a sealed collection's log was unsealed to, and where the logs taken
    out of its archives were put.

    A log seen in two places is one log: 'ingest' leaves the per-command logs beside the
    tables and a report on that dataset also pulls them out of the archives still sitting
    there, which would otherwise count every one of them twice. The place is part of the
    name, so 'Errors/NTFSInfo.dev.log' and 'NTFSInfo.dev.log' stay two logs; the run
    directory comes first, so its copy is the one kept.
    """
    found, seen = [], set()
    for place in [directory] + list(extra_dirs or []):
        here = sorted(glob.glob(os.path.join(place, "*.log")))
        for root, _dirs, files in os.walk(os.path.join(place, "Errors")):
            here += sorted(os.path.join(root, name) for name in files)

        for path in here:
            # Sliced rather than relpath'd: every path here was built from 'place', and
            # relpath raises across Windows mounts, which is exactly where these dirs sit.
            name = os.path.normcase(path[len(place):].lstrip("\\/"))
            if name in seen:
                continue

            seen.add(name)
            found.append(path)

    return found


def run_logs_of(directory: str, extra_dirs: list | None = None,
                stem: str | None = None) -> list:
    """The logs of one run: every log of the directory, or only this run's when a stem says so."""
    found = run_log_files(directory, extra_dirs)
    if not stem:
        return found

    return [path for path in found if os.path.basename(path).startswith(stem)]


def log_label(path: str, directory: str) -> str:
    """How a log is named in the report: its path within the run, else its file name alone.

    An unsealed sidecar sits in the work dir, which is routinely on another volume than the
    collection - a UNC share and a local temp dir, say - and relpath across mounts raises on
    Windows rather than returning a path. Even on one volume it would return a line of '..'
    segments, which names nothing useful.
    """
    try:
        relative = os.path.relpath(path, directory)
    except ValueError:
        return os.path.basename(path)

    if relative.startswith(".."):
        return os.path.basename(path)

    return relative.replace("\\", "/")


OUTCOME_SUFFIX = "_Outcome.json"


def log_stem(outcome_path: str) -> str | None:
    """The name ORC gave this run's files: 'ORC_<host>_<stamp>_Outcome.json' -> the stem.

    What lets a log be attributed to one collection when a directory holds several, which is
    how a merged dump of a fleet arrives.
    """
    name = os.path.basename(outcome_path or "")
    if not name.endswith(OUTCOME_SUFFIX):
        return None

    return name[:-len(OUTCOME_SUFFIX)]


def build_log_view(directory: str, extra_dirs: list | None = None,
                   stem: str | None = None, ignore: list | None = None,
                   context: int | None = None) -> dict:
    """What went wrong in this run: the logs carrying error lines, and the tables the ingest
    could not read at all, which are a hole in the run rather than a message inside it.

    'stem' keeps a run to its own logs where the directory holds more than one collection.
    Without it every log of the directory belongs to the run, which is what a run directory of
    its own means: the collection's log, each command's log out of the expanded Errors
    container, and ORC's console output are all one run's.
    """
    if not os.path.isdir(directory):
        return {"available": False, "skipped": {}, "logs_ignored": 0, "skipped_ignored": 0}

    patterns = ignore or []
    logs = []
    for path in run_logs_of(directory, extra_dirs, stem):
        size = os.path.getsize(path)
        if size == 0:
            continue

        logs.append(dict(scan_log(path, context), log=log_label(path, directory), bytes=size))
    logs.sort(key=lambda x: x["error_lines"], reverse=True)
    kept_logs = [log for log in logs if not ignores.matches(log["log"], patterns)]
    skipped = layout.skipped(directory)
    kept_skipped = {name: why for name, why in skipped.items()
                    if not ignores.matches(name, patterns)}

    # 'available' is whether there was a log to read, not whether the directory exists: a
    # sealed collection holds only '<name>.log.7z.p7b', and '0 log(s) with matches' would then
    # read as reassurance when nothing had been read at all.
    return {"available": bool(run_logs_of(directory, extra_dirs, stem)), "logs": kept_logs,
            "skipped": kept_skipped,
            "logs_ignored": len(logs) - len(kept_logs),
            "skipped_ignored": len(skipped) - len(kept_skipped)}


def build_message_view(log_view: dict, level: str) -> dict:
    """What was said at one level, built from the scan the log view already did.

    Told in time order rather than a log at a time: a host's collection log and its per-command
    logs each hold part of one story, and read one file at a time it does not join up. ORC
    timestamps are fixed-width UTC, so sorting them as text is sorting them as time.
    """
    messages = [dict(message, log=log["log"])
                for log in log_view.get("logs", [])
                for message in log.get("messages", [])
                if message["level"] == level]
    messages.sort(key=lambda m: (m["time"], m["log"]))
    return {"available": log_view.get("available", True), "messages": messages}


def _hsize(n) -> str:
    if n is None:
        return "?"
    size = float(n)
    for unit in ("B", "KB", "MB", "GB", "TB"):
        if size < 1024 or unit == "TB":
            return f"{size:.1f}{unit}"
        size /= 1024


def _hdur(seconds) -> str:
    if seconds is None:
        return "?"
    s = int(seconds)
    return f"{s // 3600}h{(s % 3600) // 60}m" if s >= 3600 else f"{s // 60}m{s % 60}s"


def host_of(directory: str, extra_dirs: list | None = None,
            outcome_path: str | None = None) -> str:
    """The machine the collection came from, by its Outcome; the directory name as a fallback.

    A sealed collection has no readable Outcome until its sidecars are unsealed, and would
    otherwise be reported under the name of the folder it happens to sit in. 'outcome_path' is
    this run's own Outcome, which matters when the directory holds several.
    """
    op = outcome_path or _outcome.outcome_file(directory, extra_dirs)
    if op:
        try:
            host = _outcome.parse_outcome(op).host
            if host:
                return host
        except (OSError, ValueError):
            pass
    return os.path.basename(os.path.normpath(directory))


def build_host_report(directory: str, engine=None, extra_dirs: list | None = None,
                      outcome_path: str | None = None, stem: str | None = None,
                      ignore: list | None = None, context: int | None = None) -> dict:
    """Every view of one run dir, each told whether its own input was there to read.

    Only 'ingest' writes the '.orca' directory, and report is pointed at a directory 'extract'
    produced often enough - ORC's Outcome and logs are both in one - so two of the views
    routinely have nothing to read, and have to say so rather than report a zero.

    The three message views are built from the log view rather than from the directory: they
    are about the same logs, and a fleet's worth of them is not worth reading twice.
    """
    timing = load_timing(directory)
    timing_available = os.path.exists(layout.timing_path(directory))
    manifest_available = bool(engine) and os.path.exists(layout.manifest_path(directory))

    op = outcome_path or _outcome.outcome_file(directory, extra_dirs)
    oc = _outcome.parse_outcome(op) if op else None
    files = empty_output_files(engine, layout.manifest_path(directory)) if engine else []
    log_view = build_log_view(directory, extra_dirs, stem, ignore, context)
    return {
        "host": host_of(directory, extra_dirs, op),
        "directory": directory,
        "outcome": build_outcome_view(oc, ignore),
        "empty": build_empty_view(timing, files, timing_available, manifest_available, ignore),
        "stats": build_stats_view(timing, timing_available),
        "log": log_view,
        "warn": build_message_view(log_view, "warning"),
        "error": build_message_view(log_view, "error"),
        "critical": build_message_view(log_view, "critical"),
    }


def ignored_count(hr: dict) -> int:
    """How many findings the ignore list passed over, across every view of this run."""
    return (hr["outcome"].get("failures_ignored", 0)
            + hr["empty"].get("tables_ignored", 0)
            + hr["empty"].get("files_ignored", 0)
            + hr["log"].get("logs_ignored", 0)
            + hr["log"].get("skipped_ignored", 0))


def summary_of(hr: dict) -> dict:
    """The scalars of one host. None, never 0, for a view whose input was not there: a summary
    that reports an absent input as a zero is worse than one that admits it does not know."""
    o, e, s = hr["outcome"], hr["empty"], hr["stats"]
    avail = o.get("available")
    tables = s.get("available", True)
    tables_empty = e.get("available", True)
    return {
        "host": hr["host"],
        "ok": o.get("ok") if avail else None,
        "commands": o.get("commands") if avail else None,
        "fails": len(o.get("failures", [])) if avail else None,
        "empty": e["count"] if tables_empty else None,
        "empty_files": e.get("file_count", 0) if e.get("files_available", True) else None,
        "tables": s["tables"] if tables else None,
        "unread": len(hr["log"].get("skipped", {})),
        "criticals": len(hr.get("critical", {}).get("messages", [])),
        "ignored": ignored_count(hr),
        "total_bytes": s["total_bytes"] if tables else None,
        "duration_seconds": o.get("duration_seconds") if avail else None,
        "memory_peak_bytes": o.get("memory_peak_bytes") if avail else None,
    }


def _opt(value) -> str:
    """'?' for a field nobody could read, the way _hsize and _hdur already render None."""
    return "?" if value is None else str(value)


def render_one_line(summary: dict) -> str:
    line = (f'{summary["host"]}: ok={_opt(summary["ok"])} cmds={_opt(summary["commands"])} '
            f'fails={_opt(summary["fails"])} '
            f'empty={_opt(summary["empty"])} tables={_opt(summary["tables"])} '
            f'size={_hsize(summary["total_bytes"])} dur={_hdur(summary["duration_seconds"])} '
            f'mem={_hsize(summary["memory_peak_bytes"])}')

    # Only when there is one: a field that always reads zero stops being read, and each of
    # these says something went wrong - the run is missing a table, ORC wrote an output and
    # put nothing in it, a command died - rather than the ordinary state of affairs. None
    # stays quiet too - an unread manifest must not read as 'no empty files'.
    for field in ("criticals", "empty_files", "unread", "ignored"):
        if summary.get(field):
            line += f' {field}={summary[field]}'

    return line


def host_json(hr: dict) -> dict:
    """One flat object for a host: the summary scalars at the top level, each view's detail on
    a key below, and a flag per input saying whether it was there to read.

    Flat on purpose. Filtering a fleet has to stay a one-liner - 'report --json |
    ConvertFrom-Json | ? fails -gt 0' - and it is not one if the fields a filter wants sit
    three levels down. The detail rides along underneath for when the filter has found
    something.
    """
    outcome, empty, stats, log_view = hr["outcome"], hr["empty"], hr["stats"], hr["log"]
    return {
        **summary_of(hr),
        "directory": hr["directory"],
        "outcome_available": outcome.get("available", True),
        "stats_available": stats.get("available", True),
        "empty_available": empty.get("available", True),
        "empty_files_available": empty.get("files_available", True),
        "log_available": log_view.get("available", True),
        "ignored": ignored_count(hr),
        "memory_peak_set": outcome.get("memory_peak_set", ""),
        "failures": outcome.get("failures", []),
        "empty_tables": empty.get("empty_tables", []),
        "empty_files": empty.get("empty_files", []),
        "biggest": stats.get("biggest", []),
        "slowest": stats.get("slowest", []),
        # The messages are kept off the logs: they are the bulky part, and a filter that found
        # a host has one place to read them rather than two that must agree.
        "logs": [{key: value for key, value in log.items() if key != "messages"}
                 for log in log_view.get("logs", [])],
        # Not 'critical': the summary's count is 'criticals', and a detail list under the same
        # name would overwrite the scalar a filter reads.
        "warn_messages": hr.get("warn", {}).get("messages", []),
        "error_messages": hr.get("error", {}).get("messages", []),
        "critical_messages": hr.get("critical", {}).get("messages", []),
        "skipped": log_view.get("skipped", {}),
    }


def render_json(summaries: list) -> str:
    return json.dumps(summaries, indent=2)


MAX_WIDTH = 100


def short_path(path: str, width: int) -> str:
    """A path trimmed from the left, keeping the tail - which is the part that names the run."""
    if len(path) <= width or width < 8:
        return path

    return "..." + path[-(width - 3):]


def columns(rows: list, align: str) -> list:
    """Aligned columns: each column as wide as its widest value, '<' left, '>' right."""
    if not rows:
        return []

    widths = [max(len(row[index]) for row in rows) for index in range(len(rows[0]))]
    return ["  ".join(f"{value:{how}{width}}" for value, how, width
                      in zip(row, align, widths)).rstrip()
            for row in rows]


def section(heading: str, rows: list, align: str) -> list:
    """A counted heading and every one of its rows, indented and in columns.

    Nothing is truncated. A report that hides rows behind a '... N more' is a report you have
    to run again with another flag to finish reading, and the count in the heading is there to
    say how long the list is, not to excuse not printing it.
    """
    return [f"  {heading}"] + [f"    {line}" for line in columns(rows, align)]


def counted(label: str, kept: int, ignored: int = 0) -> str:
    """A section heading. With an ignore list it says what it kept of how many, and how many it
    passed over, so a report never quietly disagrees with the Outcome it read."""
    if not ignored:
        return f"{label} ({kept})"

    return f"{label} ({kept} of {kept + ignored}, {ignored} ignored)"


def failure_rows(outcome: dict) -> list:
    """The failed commands, with the columns a '--list' line uses: set, command, tool,
    duration, then what the process did - CPU time, bytes read and written - and the exit code
    last. Those three tell a command that never started from one cut short at its timeout."""
    failures = outcome.get("failures", [])
    cpu = labelled("cpu", [_hdur(failure.get("cpu_seconds")) for failure in failures])
    read = labelled("read", [_hsize(failure.get("read_bytes")) for failure in failures])
    wrote = labelled("wrote", [_hsize(failure.get("write_bytes")) for failure in failures])
    return [[failure["set"], failure["command"], failure.get("tool") or "-",
             _hdur(failure["duration_seconds"]), cpu[index], read[index], wrote[index],
             _outcome.exit_hex(failure["exit_code"])]
            for index, failure in enumerate(failures)]


def labelled(label: str, values: list) -> list:
    """'label=value' cells padded between the two, so the labels start in one column and the
    values end in one: a '?' must not pull its label to the right of the others."""
    width = max((len(value) for value in values), default=0)
    return [f"{label}={value:>{width}}" for value in values]


def table_rows(stats: dict) -> list:
    """The tables worth looking at, size and rows and time on one line each."""
    return [[table["name"], _hsize(table["bytes"]), f'{table["rows"]} rows',
             f'{table["seconds"]:.2f}s']
            for table in stats.get("top", [])]


def log_level_cells(log: dict) -> list:
    """The counts of one log, or the one thing that can be said when there are none.

    A log nothing could parse gets the substring count alone, marked approximate: zeros under
    warnings and criticals would assert something that heuristic cannot know. Always three
    cells wide, and the one thing it can say goes in the error column, so the numbers line up
    down the table whatever each log turned out to be.
    """
    if log["error_lines"] < 0:
        return ["", "unreadable", ""]

    if not log.get("exact", True):
        return ["", f'~{log["error_lines"]}E', ""]

    return [f'{log["warnings"]}W', f'{log["errors"]}E', f'{log["criticals"]}C']


def log_rows(log_view: dict) -> list:
    """Only the logs that carried something: a log read and found clean needs no line."""
    return [[log["log"]] + log_level_cells(log)
            for log in log_view.get("logs", []) if log["error_lines"]]


def time_of_day(timestamp: str) -> str:
    """'2026-10-03T08:15:03.000Z' -> '08:15:03'. A host report is one run, so the date is the
    same on every line of it and only costs width."""
    return timestamp[11:19] if len(timestamp) >= 19 else timestamp


def message_rows(messages: list, width: int = MAX_WIDTH) -> list:
    """A message as its log, when it happened, and what it said, cut to fit one line."""
    rows = []
    for message in messages:
        room = width - len(message["log"]) - 18
        text = message["message"]
        rows.append([message["log"], time_of_day(message["time"]),
                     text if len(text) <= room else text[:room - 3] + "..."])

    return rows


# The rule around a message's context: several of them otherwise run into one wall of
# indented lines with nothing saying where each one ends.
CONTEXT_RULE = "      " + "-" * 30


def log_line(entry, marker: str = " ") -> str:
    """One context line in the shape the log itself used, under the report's indent."""
    time, level, text = entry
    return f"      {marker} {time_of_day(time)} [{_logs.TAGS[level]}] {text}"


def context_lines(message: dict) -> list:
    """The log lines around the message, ruled off, the message itself marked with '>'.

    Read from the log rather than taken from ORC's backtrace block: the block is a replay the
    sink clears on every dump, so with a trigger of Error it holds the two or three lines since
    the previous error while sitting exactly where this error's own context would be. The
    marked line carries the message in full, which the row above it has had to cut to a width.
    """
    before, after = message.get("before", []), message.get("after", [])
    if not before and not after:
        return []

    own = (message["time"], message["level"], message["message"])
    return ([CONTEXT_RULE]
            + [log_line(entry) for entry in before]
            + [log_line(own, ">")]
            + [log_line(entry) for entry in after]
            + [CONTEXT_RULE])


def skipped_rows(skipped: dict, width: int = MAX_WIDTH) -> list:
    """The tables an ingest could not read, and why, with the reason cut to fit one line."""
    rows = []
    for name, reason in sorted(skipped.items()):
        room = width - len(name) - 6
        rows.append([name, reason if len(reason) <= room else reason[:room - 3] + "..."])

    return rows


def header_lines(hr: dict) -> list:
    """The host, where it came from, and the run's scalars."""
    host = hr["host"]
    path = short_path(hr["directory"], MAX_WIDTH - len(host) - 2)
    return [f"{host}  {path}"]


def outcome_lines(hr: dict, explicit: bool) -> list:
    outcome = hr["outcome"]
    if not outcome.get("available"):
        return ["  (no Outcome captured)"]

    lines = [f'  ok={outcome["ok"]}  cmds={_opt(outcome.get("commands"))}  '
             f'fails={len(outcome.get("failures", []))}  '
             f'dur={_hdur(outcome["duration_seconds"])}  '
             f'mem={_hsize(outcome["memory_peak_bytes"])} ({outcome["memory_peak_set"]})']

    rows = failure_rows(outcome)
    # A heading with no rows under it where something was ignored: vanishing because you
    # ignored all of it would hide the choice, and the heading is its only record here.
    if rows or explicit or outcome.get("failures_ignored", 0):
        lines.append("")
        lines += section(counted("failed commands", len(rows),
                                 outcome.get("failures_ignored", 0)), rows, "<<<>>>>>")

    return lines


def not_ingested(hr: dict, label: str) -> str:
    """What a view says when it needs a dataset and was given a collection.

    'report' never ingests: it is fast, and fetching what is missing is not - a fleet sweep
    would turn into a fleet ingest. So it names the command instead of running it.
    """
    # --out-at-input: a report of the collection only finds a dataset written inside it.
    return f"  {label}: (not ingested; orca ingest {hr['directory']} --out-at-input)"


def stats_lines(hr: dict, explicit: bool) -> list:
    stats = hr["stats"]
    if not stats.get("available", True):
        return [not_ingested(hr, "tables")]

    rows = table_rows(stats)
    if not rows and not explicit:
        return []

    heading = (f'tables ({stats["tables"]}, {stats["total_rows"]} rows, '
               f'{_hsize(stats["total_bytes"])})')
    return section(heading, rows, "<>>>")


def empty_lines(hr: dict, explicit: bool) -> list:
    empty = hr["empty"]
    if not empty.get("available", True):
        return [not_ingested(hr, "empty tables")]

    lines = []
    tables = [[name] for name in empty.get("empty_tables", [])]
    if tables or explicit or empty.get("tables_ignored", 0):
        lines += section(counted("empty tables", len(tables),
                                 empty.get("tables_ignored", 0)), tables, "<")

    if not empty.get("files_available", True):
        return lines + ["  empty outputs: (not read: no manifest or no duckdb)"]

    files = [[name] for name in empty.get("empty_files", [])]
    if files or explicit or empty.get("files_ignored", 0):
        lines += section(counted("empty outputs", len(files),
                                 empty.get("files_ignored", 0)), files, "<")

    return lines


def log_lines(hr: dict, explicit: bool) -> list:
    log_view = hr["log"]
    lines = []
    if not log_view.get("available", True):
        lines.append("  logs: (no logs captured)")
    else:
        rows = log_rows(log_view)
        if rows or explicit or log_view.get("logs_ignored", 0):
            # Its own 'of': how many logs were read, which is not how many were kept.
            read = len(log_view.get("logs", [])) + log_view.get("logs_ignored", 0)
            heading = f"logs with errors ({len(rows)} of {read}"
            passed = log_view.get("logs_ignored", 0)
            heading += f", {passed} ignored)" if passed else ")"
            lines += section(heading, rows, "<>>>")

    # A hole in the run rather than a message inside it, so it is said separately and whether
    # or not there were logs to read.
    skipped = skipped_rows(log_view.get("skipped") or {})
    if skipped or explicit or log_view.get("skipped_ignored", 0):
        lines += section(counted("unread tables", len(skipped),
                                 log_view.get("skipped_ignored", 0)), skipped, "<<")

    return lines


def message_lines(hr: dict, view: str, explicit: bool,
                  context: int | None = None) -> list:
    """What was said at one level. The log view says which log to open; this says why.

    The heading is the view's own name - 'warn', not 'warning' - so a section maps back to the
    flag that asks for it. When no log was read at all the log view already says so, so this
    one repeats it only when it was asked for by name; otherwise the report says it twice.
    """
    messages = hr.get(view, {})
    if not messages.get("available", True):
        return [f"  {view}: (no logs captured)"] if explicit else []

    rows = messages.get("messages", [])
    if not rows and not explicit:
        return []

    lines = [f"  {view} ({len(rows)})"]
    for row, message in zip(columns(message_rows(rows), "<<<"), rows):
        lines.append(f"    {row}")
        if context is not None:
            lines += context_lines(message)

    return lines


# Every view, in the order a report prints them: what ran, what it produced, then what it said
# about it, by rising severity.
VIEWS = ("outcome", "stats", "empty", "log", "warn", "error", "critical")

# What a report prints when no view was named. The message views below critical are left out:
# a run with thousands of warnings would bury the rest of its own report.
DEFAULT_VIEWS = ("outcome", "stats", "empty", "log", "critical")

MESSAGE_VIEWS = ("warn", "error", "critical")

# What '--view' accepts. 'all' is every view rather than one of them.
VIEW_NAMES = VIEWS + ("all",)


def expand_views(views: list | None) -> list:
    """The views to print: the default set when none was named, every one for 'all'.

    Returned in VIEWS order whatever order they were asked for, so '--view critical,log' and
    '--view log,critical' print the same report. None is 'none was named'; an empty list is
    none of them, which is the header alone.
    """
    if views is None:
        return list(DEFAULT_VIEWS)

    if "all" in views:
        return list(VIEWS)

    return [view for view in VIEWS if view in views]


def render_human(hr: dict, views: list, explicit: bool = False,
                 context: int | None = None) -> str:
    """The default report: the host, its scalars, then one counted section per finding.

    Every row of every section is printed. Sections with nothing in them are left out - the
    scalars already say 'fails=0', and a run with no findings should read as a short report
    rather than a page of zeros - but a view named explicitly prints its heading even when it
    is empty, because asking for a view and getting nothing back cannot be told from a broken
    report.
    """
    views = expand_views(views)
    lines = header_lines(hr)
    if "outcome" in views:
        lines += outcome_lines(hr, explicit)
    for name, builder in (("stats", stats_lines), ("empty", empty_lines), ("log", log_lines)):
        if name not in views:
            continue

        built = builder(hr, explicit)
        if built:
            lines += [""] + built

    # Last, and apart, because these are the sections that take a flag of their own.
    for name in MESSAGE_VIEWS:
        if name not in views:
            continue

        built = message_lines(hr, name, explicit, context)
        if built:
            lines += [""] + built

    return "\n".join(lines)


def command_failed(row: dict) -> bool:
    """A command failed when its exit code is not zero, or not a number at all.

    The outcome table's own 'ok' column already reads an unparseable code as a failure, and
    --list has to agree with it: a code ORC wrote that nothing can parse is not a success.
    """
    return row.get("ok") != "true"


def command_fields(row: dict) -> list[str]:
    """One command as the columns of a --list line: machine first, exit code last.

    'peak' is the command SET's peak memory, repeated for each of its commands, because that
    is the only memory an Outcome records; 'tool' is a dash for a command that runs a packaged
    binary rather than an ORC subtool, which the Outcome leaves null.
    """
    return [
        row.get("computer_name") or "?",
        row.get("command_set") or "",
        row.get("command") or "",
        row.get("tool") or "-",
        _hdur(row.get("duration_seconds")),
        _hsize(row.get("set_memory_peak") or None),
        _outcome.exit_hex(row.get("exit_code", "")),
    ]


def sort_command_rows(rows: list[dict]) -> list[dict]:
    """By host, then by the order ORC ran them: a machine's commands stay together and read
    as the timeline they were."""
    return sorted(rows, key=lambda row: (row.get("computer_name") or "",
                                         row.get("start") or ""))


# Left for the names, right for the numbers, so durations, peaks and exit codes line up on
# their last digit across hosts whose names are nothing like the same length.
_COMMAND_ALIGNMENT = ("<", "<", "<", "<", ">", ">", ">")


def render_command_lines(rows: list[dict], failed_only: bool = False,
                         ignore: list | None = None) -> str:
    """One aligned line per command, across every host, with no per-host headers.

    The output to scan when a fleet is expected to be clean: with --failed and nothing wrong,
    all that is left is the summary. The summary counts every command either way - a filtered
    '1 failed' with no denominator says nothing.
    """
    patterns = ignore or []
    kept = [row for row in rows
            if not ignores.matches_any(ignores.command_names(row.get("command_set"),
                                                             row.get("command")), patterns)]
    selected = [row for row in sort_command_rows(kept)
                if not failed_only or command_failed(row)]
    hosts = {row.get("computer_name") or "?" for row in kept}
    failed = sum(1 for row in kept if command_failed(row))
    passed = len(rows) - len(kept)
    summary = f"  {len(kept)} command(s), {failed} failed"
    summary += f", {passed} ignored" if passed else ""
    summary += f", {len(hosts)} host(s)"

    if not selected:
        return summary

    fields = [command_fields(row) for row in selected]
    widths = [max(len(row[column]) for row in fields) for column in range(len(fields[0]))]
    # No indent: a line starts with the machine name, the way a --one-line summary starts with
    # the host, so the output stays greppable and sorts on the field you read first.
    lines = ["  ".join(f"{value:{align}{width}}" for value, align, width
                       in zip(row, _COMMAND_ALIGNMENT, widths)).rstrip()
             for row in fields]
    return "\n".join(lines + ["", summary])


def _extra(sealed: dict, directory: str) -> list | None:
    """The 'extra_dirs' for one run: where its sidecars were unsealed, if any were."""
    found = sealed.get(directory)
    return [found] if found else None


class Run(NamedTuple):
    """One collection to report on.

    The Outcome, not the directory, is what identifies a run: a fleet dumped into a single
    directory is one directory holding one Outcome per machine, and keying off the directory
    reported the first of them and silently dropped the rest. 'outcome_path' is None for an
    ingested run that carries no Outcome at all, which is still worth a report of its tables.
    """
    directory: str
    outcome_path: str | None
    extra_dirs: list


def candidate_dirs(paths: list, recurse: bool = False) -> list:
    """The directories to look in: the ones given, and with 'recurse' everything under them.

    Asked about a directory, a report answers about that directory: a merged dump holds every
    machine's files side by side, so its collections are right there. Looking inside is the
    surprising half - a directory of per-host folders, a tree of dumps by date - so it is
    asked for. The de-duplication by run id is what makes it safe: a collection, the dataset
    'ingest' wrote inside it and an expanded tree all hold a copy of the same Outcome.

    '.orca' is skipped: it holds orca's own bookkeeping, never a collection.
    """
    found = []
    for path in paths:
        if not os.path.isdir(path):
            raise RuntimeError(f"no such directory: '{path}'")

        found.append(path)
        if not recurse:
            continue

        for root, children, _files in os.walk(path):
            children[:] = sorted(name for name in children if name != layout.ORCA_DIR)
            found += [os.path.join(root, name) for name in children]

    return found


def runs_in(directory: str, extra_dirs: list) -> list:
    """Every collection whose Outcome is in 'directory' (or where its sidecars were unsealed).

    An ingested run with no Outcome is one run all the same: its tables are worth reporting
    even when ORC's own record of the collection never arrived.
    """
    outcomes = []
    for place in [directory] + list(extra_dirs):
        outcomes += ingest.find_outcome_jsons(place)

    if not outcomes:
        if os.path.isfile(layout.timing_path(directory)):
            return [Run(directory, None, list(extra_dirs))]

        return []

    return [Run(directory, path, list(extra_dirs)) for path in outcomes]


def archives_named_by(outcome_paths: list) -> list:
    """Every archive name these Outcomes record."""
    named = []
    for path in outcome_paths:
        outcome = _outcome.read_outcome(path) or {}
        for command_set in outcome.get("command_set", []) or []:
            archive = command_set.get("archive") or {}
            if archive.get("name"):
                named.append(archive["name"])

    return named


def orphan_rows(directory: str, extra_dirs: list | None = None,
                accounted: list | None = None) -> list:
    """The archives of 'directory' that no Outcome of it accounts for, with their sizes.

    A collection whose Outcome never arrived leaves exactly this: data sitting in the directory
    that nothing in the report says a word about.

    Every Outcome of the directory has its say, not just the runs that were reported: a
    collection whose ingested run won the de-duplication still named its own archives, and
    calling them orphans because the run directory is where the report came from would be a
    false alarm.

    'accounted' is what the Outcomes found elsewhere in the report name: 'orca fleet' leaves
    the archives it decrypts in its output directory, and their Outcomes sit one level down
    in the datasets - calling those orphans would be the same false alarm.
    """
    named = list(accounted or [])
    for place in [directory] + list(extra_dirs or []):
        named += archives_named_by(ingest.find_outcome_jsons(place))

    rows = []
    for path in ingest.archives_not_named(directory, named):
        rows.append([os.path.basename(path), f"{os.path.getsize(path)} bytes"])

    return rows


def run_identity(run: Run) -> str:
    """What makes two sightings of a run the same run: ORC's run id, else the Outcome's name.

    'ingest' copies the Outcome into the run directory it writes inside the collection, so the
    same collection is visible in both and must be reported once.
    """
    if not run.outcome_path:
        return os.path.abspath(run.directory)

    outcome = _outcome.read_outcome(run.outcome_path) or {}
    return outcome.get("id") or os.path.basename(run.outcome_path)


def ingested_at(directory: str) -> float:
    """When 'directory' was last ingested into, or 0 for one that never was: the time of its
    ingest timing, which every ingest writes last."""
    try:
        return os.path.getmtime(layout.timing_path(directory))
    except OSError:
        return 0.0


def find_runs(dirs: list, sealed: dict) -> list:
    """Every run across 'dirs', one per Outcome, in host order and without duplicates.

    Where the same run is seen twice, the sighting with an ingest timing beside it wins: that
    is the ingested run, and it is the one with tables to report. Of two ingested sightings the
    more recent ingest wins: a dataset left beside a collection by an older orca must not hide
    the one just built in the current directory.
    """
    best = {}
    for directory in dirs:
        for run in runs_in(directory, _extra(sealed, directory) or []):
            identity = run_identity(run)
            current = best.get(identity)
            if current is None or ingested_at(run.directory) > ingested_at(current.directory):
                best[identity] = run

    return sorted(best.values(), key=lambda run: (host_of(run.directory, run.extra_dirs,
                                                          run.outcome_path),
                                                  run.directory))


def sidecar_dir(directory: str, work: str) -> str:
    """Where one run's unsealed sidecars go: a directory of its own under the work dir.

    Named after the collection plus a digest of its path, following the convention the run
    cache uses, because two machines' collections routinely share a folder name under one work
    dir and must not unseal over each other.
    """
    digest = hashlib.sha256(os.path.abspath(directory).encode("utf-8")).hexdigest()[:12]
    return os.path.join(work, f"{os.path.basename(os.path.normpath(directory))}-{digest}")


def unsealed_sidecar_dirs(dirs: list, work: str, private_key=None, cms_backend=None,
                          refresh: bool = False) -> dict:
    """Unseal the sealed sidecars of every collection in 'dirs'; map run dir -> where they went.

    A collection arrives with its Outcome, Outlines and log each sealed into a '.7z.p7b', and
    without them a report can say almost nothing: not the machine's name, not which command
    failed. They are unsealed into the work dir and never beside the collection - 'ingest' keeps
    what it unseals on purpose, to be reused, but a report only reads and must leave what it
    reads as it was. They are a few KB, so paying for the decryption again next time is nothing.

    Collections with nothing sealed are absent from the map, so the common case creates no
    directory at all.
    """
    mapped = {}
    seven = None
    for directory in dirs:
        if not ingest.sealed_sidecars(directory):
            continue

        if seven is None:
            seven = sevenzip.find_7z() or ""
        if not seven:
            # Not fatal: the views that read ORC's own files go quiet, the rest still print.
            print(f"cannot unseal the sidecars of '{directory}': 7z.exe not found")
            continue

        target = sidecar_dir(directory, work)
        os.makedirs(target, exist_ok=True)
        ingest.unseal_sidecars(directory, seven, target, private_key, cms_backend, refresh)
        mapped[directory] = target

    return mapped


def archived_logs_dir(run: Run, work: str) -> str:
    """Where this run's in-archive logs are kept: a directory of its own under the work dir.

    Per run, not per directory: a merged dump of a fleet is one directory holding one Outcome
    per machine, and the logs come out under the command's name ('NTFSInfo.dev.log'), which
    says nothing about which machine they belong to. A directory each is what keeps them
    apart, since 'extra_dirs' attributes by directory rather than by file name.
    """
    return os.path.join(sidecar_dir(run.directory, work), "archived-logs",
                        log_stem(run.outcome_path) or "run")


def archives_of(run: Run) -> tuple[list, list]:
    """This run's archives as (readable paths, names that are sealed).

    Sealed ones are named rather than opened: the envelope is CMS over the whole archive, so
    reading one log out of it costs decrypting all of it, which is an extract wearing a
    report's clothes.
    """
    readable, sealed = [], []
    for name in archives_named_by([run.outcome_path] if run.outcome_path else []):
        path = os.path.join(run.directory, name)
        if ingest.sealed_name(name):
            plain = os.path.join(run.directory, ingest.sealed_name(name) + ".7z")
            if os.path.isfile(plain):
                readable.append(plain)
            elif os.path.isfile(path):
                sealed.append(name)

            continue

        if os.path.isfile(path):
            readable.append(path)

    return readable, sealed


def capture_archived_logs(run: Run, work: str, seven_zip: str,
                          refresh: bool) -> tuple[str | None, list]:
    """Pull this run's in-archive logs into the work dir. (where they went, what stayed sealed)

    Never into the collection: a report only reads, and what it reads has to stay as it was
    found. Kept between runs because the decode is the expensive part - an archive already
    expanded is reused, and '--refresh' is how you ask for it again.

    The sealed ones are returned rather than announced here, so a fleet of them is one line
    instead of one per host.
    """
    target = archived_logs_dir(run, work)
    if os.path.isdir(target) and not refresh:
        return (target if os.listdir(target) else None), []

    readable, sealed = archives_of(run)
    if not readable:
        return None, sealed

    os.makedirs(target, exist_ok=True)
    try:
        # capture_logs names an archive it could not open itself, so only what it does not
        # handle is caught here.
        ingest.capture_logs(seven_zip, readable, target, None)
    except OSError as error:
        print(f"could not keep the logs of '{run.directory}': {error}")

    return (target if os.listdir(target) else None), sealed


def sealed_archive_line(sealed: list, directories: list) -> str:
    """One line for the archives no report can open: there is more in them, and what opens it.

    A signpost, not an explanation. Said at all because an invisible gap is what this whole
    path exists to close - a report that quietly skipped them would read as a clean run - and
    said once rather than per host, the way orphan_lines is, since a fleet's worth of the same
    sentence buries it.
    """
    names = sorted(set(sealed))
    shown = ", ".join(names[:3]) + (", ..." if len(names) > 3 else "")
    plural = "archive" if len(names) == 1 else "archives"
    where = directories[0] if len(set(directories)) == 1 else os.path.dirname(directories[0])
    # --out-at-input: the next report reads the decrypted archives only from beside the sealed.
    return f"more in {len(names)} sealed {plural}: {shown} (orca ingest {where} --out-at-input)"


def with_archived_logs(runs: list, work: str, seven_zip: str, refresh: bool = False,
                       workers: int = 8) -> list:
    """Every run, with the logs out of its archives added to its extra_dirs.

    In parallel across runs because each one is a decode: a fleet of hosts is the case where
    this is worth anything, and they do not wait on each other.
    """
    if not seven_zip:
        print("cannot read the logs inside the archives: 7z.exe not found")
        return runs

    with ThreadPoolExecutor(max_workers=min(workers, len(runs)) or 1) as pool:
        found = list(pool.map(lambda run: capture_archived_logs(run, work, seven_zip, refresh),
                              runs))

    sealed = [name for _directory, names in found for name in names]
    if sealed:
        print(sealed_archive_line(sealed, [run.directory for run in runs]))

    return [run if directory is None else run._replace(extra_dirs=run.extra_dirs + [directory])
            for run, (directory, _sealed) in zip(runs, found)]


def shared_dirs(runs: list) -> set:
    """The directories holding more than one run, where a log has to be attributed."""
    counts: dict = {}
    for run in runs:
        key = os.path.abspath(run.directory)
        counts[key] = counts.get(key, 0) + 1

    return {directory for directory, count in counts.items() if count > 1}


def orphan_lines(dirs: list, sealed: dict, accounted: list | None = None) -> list:
    """One block per directory holding an archive no Outcome accounts for.

    Said once per directory rather than under each of its collections: it is a fact about the
    directory, and a fleet's worth of hosts repeating it would bury it.
    """
    blocks = []
    for directory in dirs:
        rows = orphan_rows(directory, _extra(sealed, directory), accounted)
        if rows:
            blocks.append("\n".join([directory]
                                    + section(f"archives no Outcome names ({len(rows)})",
                                              rows, "<>")))

    return blocks


class FleetRecord(NamedTuple):
    """What 'orca fleet' wrote about one fleet: who answered, and the gaps (fleet.py)."""
    where: str             # the .orca-fleet directory
    hosts: list            # hosts.csv rows
    coverage: list         # coverage.csv rows


def fleet_record(directory: str) -> FleetRecord | None:
    """The fleet whose output directory 'directory' is, or None. Known by '.orca-fleet/hosts.csv',
    which fleet writes beside its datasets and fleet.duckdb."""
    where = os.path.join(directory, _fleet.FLEET_DIR)
    if not os.path.isfile(os.path.join(where, _fleet.HOSTS_FILE)):
        return None
    return FleetRecord(where, _fleet.read_hosts(where), _fleet.read_coverage(where))


def as_directory(path: str) -> str:
    """A path as report reads it: 'fleet.duckdb' stands for the fleet directory holding it - fleet
    writes the database and its .orca-fleet record together, in one place."""
    if os.path.isfile(path) and os.path.basename(path).lower() == _fleetdb.DATABASE_NAME:
        return os.path.dirname(os.path.abspath(path))
    return path


def expand_fleets(dirs: list) -> tuple[list, list]:
    """'dirs' plus the datasets of every fleet among them, and those fleets' records.

    hosts.csv is the authority on a fleet's datasets, failed hosts included: the report reads it
    rather than looking for whatever directories happen to be there.
    """
    seen = {os.path.abspath(d) for d in dirs}
    expanded, records = list(dirs), []
    for directory in dirs:
        record = fleet_record(directory)
        if record is None:
            continue

        records.append(record)
        for row in record.hosts:
            dataset = row.get("dataset") or ""
            if row.get("status") == "ingested" and os.path.isdir(dataset) \
                    and os.path.abspath(dataset) not in seen:
                seen.add(os.path.abspath(dataset))
                expanded.append(dataset)

    return expanded, records


def fleet_problems(records: list, reported: set) -> list[tuple[str, str, str]]:
    """(host, 'failed' or 'missing', what to say) for every host a fleet could not give a dataset
    for. Reported as hosts: a fleet report that drops them reads as a smaller, cleaner fleet.

    'reported' is the identity of every run the report found anyway - a record left by an older
    fleet run, or one written on another machine whose paths do not exist here, must not call a
    host missing while its dataset is reported a few lines below.
    """
    problems = []
    for record in records:
        for row in record.hosts:
            if row.get("OrcRunId") and row["OrcRunId"] in reported:
                continue
            host = row.get("OrcComputerName") or "unnamed collection"
            if row.get("status") == "failed":
                problems.append((host, "failed", f"ingest failed: {row.get('error', '')}"))
            elif not os.path.isdir(row.get("dataset") or ""):
                problems.append((host, "missing", f"dataset missing: {row.get('dataset')} "
                                                  f"(re-run orca fleet)"))
    return problems


def coverage_gaps(records: list, ignore: list | None = None) -> dict[str, list[str]]:
    """host -> its gaps as coverage.csv words them ('missing DNS_records', 'skipped autoruns'),
    less the tables 'ignore' passes over."""
    gaps: dict[str, list[str]] = {}
    for record in records:
        for row in record.coverage:
            if ignores.matches(row.get("table", ""), ignore or []):
                continue
            gaps.setdefault(row.get("OrcComputerName", ""), []).append(
                f"{row.get('status', '')} {row.get('table', '')}")
    return gaps


def ignored_gaps(records: list, ignore: list | None) -> dict[str, int]:
    """host -> how many of its gaps 'ignore' passed over, so the fleet block can say so."""
    counts: dict[str, int] = {}
    for record in records:
        for row in record.coverage:
            if ignores.matches(row.get("table", ""), ignore or []):
                host = row.get("OrcComputerName", "")
                counts[host] = counts.get(host, 0) + 1
    return counts


def _subdirectories(directory: str) -> list[str]:
    try:
        names = sorted(os.listdir(directory))
    except OSError:
        return []
    return [os.path.join(directory, n) for n in names
            if n != layout.ORCA_DIR and os.path.isdir(os.path.join(directory, n))]


def datasets_near(given: list, known: list, identities: set, current: str) -> list:
    """The datasets of the runs found in 'given', where ingest and fleet put them by default.

    One level deep in two places: inside each given directory (--out-at-input) and in the
    current directory (the default). Kept only when one of its runs has the identity of a run
    already found: the run id is the link - 'orca ingest' writes no source marker, and a fleet's
    names the shared directory, not the host - so other collections' datasets add nothing.
    """
    seen = {os.path.abspath(d) for d in known}
    near = []
    for place in list(given) + [current]:
        for candidate in _subdirectories(place):
            key = os.path.abspath(candidate)
            if key in seen or not os.path.isdir(layout.orca_dir(candidate)):
                continue
            seen.add(key)
            if any(run_identity(run) in identities for run in runs_in(candidate, [])):
                near.append(candidate)
    return near


def render_fleet_block(records: list, reported: set, ignore: list | None = None) -> str:
    """The fleet's own say, before the hosts: how many answered, who did not, and the gaps.

    A host whose every gap was ignored keeps its line, with nothing but the count: the line is
    the only record that something was passed over.
    """
    problems = fleet_problems(records, reported)
    gaps = coverage_gaps(records, ignore)
    passed = ignored_gaps(records, ignore)
    lines = []
    for record in records:
        total = len(record.hosts)
        failed = sum(1 for row in record.hosts if row.get("status") == "failed")
        lines.append(f"fleet: {total} collection(s), {total - failed} ingested, {failed} failed  "
                     f"({record.where})")
    for host, _kind, message in problems:
        lines.append(f"  {host}: {message}")
    if gaps or passed:
        lines.append("  coverage gaps:")
        for host in sorted(set(gaps) | set(passed)):
            kept = gaps.get(host, [])
            shown = ", ".join(kept[:6]) + (", ..." if len(kept) > 6 else "")
            count = counted("", len(kept), passed.get(host, 0)).strip()
            lines.append(f"    {host}: {shown} {count}" if shown else f"    {host}: {count}")
    return "\n".join(lines)


def fleet_json(host_objects: list, records: list, reported: set,
               ignore: list | None = None) -> list:
    """The host objects, each told how its fleet saw it, plus one per host with no dataset.

    On the hosts rather than in a top-level object: the JSON is a flat list so that filtering a
    fleet stays a one-liner (host_json), and a wrapper would break every such filter.
    """
    gaps = coverage_gaps(records, ignore)
    for host in host_objects:
        host["ingest"] = "ingested"
        host["coverage_gaps"] = gaps.get(host.get("host", ""), [])
    for host, kind, message in fleet_problems(records, reported):
        host_objects.append({"host": host, "ok": False, "ingest": kind, "error": message,
                             "coverage_gaps": gaps.get(host, [])})
    return host_objects


def fleet_one_lines(records: list, reported: set) -> list[str]:
    """A line per host with no dataset, so a script counting lines counts every host."""
    return [f"{host}: ok=False ingest={kind}"
            for host, kind, _message in fleet_problems(records, reported)]


NOTHING_FOUND = ("give a collection, a dataset, a fleet's output directory (one holding "
                 ".orca-fleet) or its fleet.duckdb")


def run_report(paths, views=None, one_line=False, as_json=False, list_commands=False,
               failed_only=False, private_key=None, cms_backend=None, work_dir=None,
               refresh=False, ignore_file=None, ignore_patterns=None, recurse=False,
               context=None, archive_logs=True, engine_name="duckdb") -> None:
    """Report on every collection in every directory given.

    A directory is read for what it holds rather than taken for one run: a fleet arrives either
    as a directory of per-host folders or as one directory holding every machine's files side
    by side, and the second shape used to report the first Outcome and drop the rest.
    """
    if failed_only and not list_commands:
        raise RuntimeError("--failed filters the --list output; add --list")

    # Read before anything expensive: a mistyped ignore file must not surface after a fleet's
    # worth of decryption, and must never be read as "nothing to ignore".
    ignore = ignores.load(ignore_file) + ignores.patterns(ignore_patterns)
    given = [as_directory(path) for path in (paths or ["."])]
    dirs, fleets = expand_fleets(candidate_dirs(given, recurse))

    scratch.use(work_dir)
    work = work_dir or scratch.directory("report_")
    sealed = unsealed_sidecar_dirs(dirs, work, private_key, cms_backend, refresh)
    runs = find_runs(dirs, sealed)

    # A collection's datasets where fleet and ingest put them by default, matched by run id.
    identities = {run_identity(run) for run in runs if run.outcome_path}
    near = datasets_near(given, dirs, identities, os.getcwd()) if identities else []
    if near:
        dirs += near
        runs = find_runs(dirs, sealed)

    reported = {run_identity(run) for run in runs}
    if not runs and not fleets:
        # Name the flag: a parent of per-host folders is the likeliest reason, and the fix is
        # one word rather than a hunt for which directory to point at.
        inside = "" if recurse else ", or add --recurse to look inside"
        raise RuntimeError(f"no collection in {', '.join(paths or ['.'])}: {NOTHING_FOUND}"
                           f"{inside}")

    # Before the views, and not for '--list', which reads the Outcome alone: the per-command
    # logs are inside the archives, and without this the errors and criticals views read
    # WolfLauncher's own log and nothing else.
    if archive_logs and not list_commands:
        runs = with_archived_logs(runs, work, sevenzip.find_7z() or "", refresh)

    orphans = orphan_lines(dirs, sealed,
                           archives_named_by([run.outcome_path for run in runs
                                              if run.outcome_path]))

    if list_commands:
        if fleets:
            print(render_fleet_block(fleets, reported, ignore))
        # The same rows 'orca report outcome' writes to parquet, so the text and the table can
        # never disagree - and no engine is needed to print them.
        paths = [run.outcome_path for run in runs if run.outcome_path]
        print(render_command_lines(_outcome.command_rows_from(paths), failed_only, ignore))
        without = [run.directory for run in runs if not run.outcome_path]
        if without:
            # Named, not raised: one machine missing an Outcome must not cost you the other
            # eight, and a silent gap in a fleet sweep is worse than a short note.
            print(f'  no Outcome in {len(without)} run dir(s): {", ".join(without)}')
        for block in orphans:
            print("\n" + block)
        return

    # The manifest needs the engine to read; a report still prints without one, just without
    # the empty-output-file list.
    try:
        engine = get_engine(engine_name, single_process_memory_limit())
    except RuntimeError:
        engine = None

    shared = shared_dirs(runs)
    reports = [build_host_report(
        run.directory, engine, run.extra_dirs, run.outcome_path,
        log_stem(run.outcome_path) if os.path.abspath(run.directory) in shared else None,
        ignore, context)
        for run in runs]

    if as_json:
        hosts = [host_json(r) for r in reports]
        print(render_json(fleet_json(hosts, fleets, reported, ignore) if fleets else hosts))
        return
    if one_line:
        for r in reports:
            print(render_one_line(summary_of(r)))
        for line in fleet_one_lines(fleets, reported):
            print(line)
        return

    # 'explicit' is whether a view was named: a named view prints its heading even when it is
    # empty, and the default report leaves the empty ones out.
    explicit = bool(views)
    blocks = [render_fleet_block(fleets, reported, ignore)] if fleets else []
    blocks += [render_human(r, views, explicit, context) for r in reports] + orphans
    print("\n\n".join(blocks))
