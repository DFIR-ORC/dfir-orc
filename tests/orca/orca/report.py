"""orca report: per-host views over a run dir (its timing, its Outcome and its logs).
Stdlib only. View builders are pure over parsed inputs."""
import csv
import glob
import hashlib
import json
import os
import shutil
import sys
from contextlib import redirect_stdout
from dataclasses import asdict
from typing import NamedTuple

from . import cache, fleet as _fleet, fleetdb as _fleetdb, ignores, ingest, layout, logs as _logs
from . import outcome as _outcome, scratch, sevenzip, sidecars as _sidecars
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


def archive_dirs(directory: str, outcome_path: str | None = None) -> list[str]:
    """The names of the run's archive directories, where ingest put each archive's logs.

    Known from the run's own record rather than found by looking: the archives its Outcome
    names (their stems), and for a run with no Outcome the directories its products.txt
    names. A directory found by walking could be a tree 'orca extract' expanded there.
    """
    names = [layout.archive_stem(name)
             for name in archives_named_by([outcome_path] if outcome_path else [])]
    names += [product.replace("\\", "/").split("/")[0] for product in layout.products(directory)
              if "/" in product.replace("\\", "/")]
    return sorted(set(name for name in names if name))


def archive_keys(directory: str, outcome_path: str | None = None) -> dict[str, str]:
    """archive directory name -> the key its archive was collected under ('ORC_General').

    From the run's own records: the Outcome pairs each command set's name with its archive,
    and for a run known only by its Outline, the Outline pairs each 'keyword' with its 'file'.
    What labels a log in the report: the directory repeats the host and the system type the
    host header already shows, and in a message view that pushes the message off the line.
    """
    keys = {}
    outcome = _outcome.read_outcome(outcome_path) if outcome_path else None
    for command_set in (outcome or {}).get("command_set", []) or []:
        archive = command_set.get("archive")
        for one in (archive if isinstance(archive, list) else [archive] if archive else []):
            if one.get("name") and command_set.get("name"):
                keys[layout.archive_stem(one["name"])] = command_set["name"]

    if not keys:
        for path in glob.glob(os.path.join(directory, "*.json")):
            kind, outline = _sidecars.kind_of(path)
            if kind != "outline":
                continue
            for entry in outline.get("archives", []) or []:
                if isinstance(entry, dict) and entry.get("file") and entry.get("keyword"):
                    keys[layout.archive_stem(entry["file"])] = entry["keyword"]

    return keys


def keyed_label(label: str, keys: dict | None) -> str:
    """'label' with its archive directory replaced by that archive's key, when one is known:
    'ORC_..._General/GetSamples.log' -> 'ORC_General/GetSamples.log'. Anything else as it is."""
    head, sep, rest = label.partition("/")
    if not sep or not keys:
        return label

    key = {name.lower(): value for name, value in keys.items()}.get(head.lower())
    return f"{key}/{rest}" if key else label


def run_log_files(directory: str, extra_dirs: list | None = None,
                  archives: list | None = None) -> list[str]:
    """The logs of a run: the ones at its top level and in each of its archive directories
    ('archives', see archive_dirs), each with its expanded 'Errors' container.

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
        for where in [os.path.join(place, name) for name in archives or []]:
            here += sorted(glob.glob(os.path.join(where, "*.log")))
            for root, _dirs, files in os.walk(os.path.join(where, "Errors")):
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
                stem: str | None = None, archives: list | None = None) -> list:
    """The logs of one run: every log of the directory, or only this run's when a stem says so.

    A log in one of the run's own archive directories is the run's whatever its name: the
    directory was named after an archive this run's Outcome lists.
    """
    found = run_log_files(directory, extra_dirs, archives)
    if not stem:
        return found

    own = {os.path.normcase(name) for name in archives or []}
    return [path for path in found
            if os.path.basename(path).startswith(stem)
            or os.path.normcase(os.path.basename(os.path.dirname(path))) in own
            or in_archive_errors(path, own)]


def in_archive_errors(path: str, archives: set) -> bool:
    """True for a file under '<archive dir>/Errors/', at any depth below it."""
    parts = [os.path.normcase(part) for part in os.path.normpath(path).split(os.sep)]
    for index, part in enumerate(parts[:-1]):
        if part == os.path.normcase("Errors") and index and parts[index - 1] in archives:
            return True
    return False


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
                   context: int | None = None, archives: list | None = None,
                   keys: dict | None = None) -> dict:
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
    for path in run_logs_of(directory, extra_dirs, stem, archives):
        size = os.path.getsize(path)
        if size == 0:
            continue

        logs.append(dict(scan_log(path, context),
                         log=keyed_label(log_label(path, directory), keys), bytes=size))
    logs.sort(key=lambda x: x["error_lines"], reverse=True)
    # The file name too: a log is listed under its archive's directory now, and an ignore
    # pattern written for 'GetSamples.dev.log' has to keep passing it over.
    kept_logs = [log for log in logs
                 if not ignores.matches_any([log["log"], os.path.basename(log["log"])],
                                            patterns)]
    skipped = layout.skipped(directory)
    kept_skipped = {name: why for name, why in skipped.items()
                    if not ignores.matches(name, patterns)}

    # 'available' is whether there was a log to read, not whether the directory exists: a
    # sealed collection holds only '<name>.log.7z.p7b', and '0 log(s) with matches' would then
    # read as reassurance when nothing had been read at all.
    return {"available": bool(run_logs_of(directory, extra_dirs, stem, archives)),
            "logs": kept_logs,
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
    log_view = build_log_view(directory, extra_dirs, stem, ignore, context,
                              archive_dirs(directory, op), archive_keys(directory, op))
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
    """The plain sidecars of every sealed collection in 'dirs'; map run dir -> where they are.

    A collection arrives with its Outcome, Outlines and log each sealed into a '.7z.p7b', and
    without them a report can say almost nothing: not the machine's name, not which command
    failed. Each sealed one is put in the work dir, never beside the collection: a report only
    reads, and must leave what it reads as it was.

    The plain file is taken where it already is before anything is decrypted (sidecars, rule
    1): beside the sealed one, or in a dataset ingest made of the collection - under the
    current directory, inside the collection (--out-at-input) or under 'work'. A collection
    whose datasets are there is read with no key and no 7-Zip; only what is still missing is
    unsealed.

    Collections with nothing sealed are absent from the map, so the common case creates no
    directory at all.
    """
    mapped = {}
    seven = None
    for directory in dirs:
        sealed = ingest.sealed_sidecars(directory)
        if not sealed:
            continue

        target = sidecar_dir(directory, work)
        os.makedirs(target, exist_ok=True)
        places = [directory] + [dataset for root in (os.getcwd(), directory, work)
                                for dataset in _sidecars.dataset_dirs(root)]
        for path in sealed:
            name = ingest.sealed_name(path)
            plain = None if refresh else _sidecars.look_for(name, places)
            if plain:
                if not os.path.exists(os.path.join(target, name)):
                    shutil.copy(plain, os.path.join(target, name))
                continue

            if seven is None:
                seven = sevenzip.find_7z() or ""
            if not seven:
                # Not fatal: the views that read ORC's own files go quiet, the rest still print.
                print(f"cannot unseal '{os.path.basename(path)}': 7z.exe not found")
                continue

            try:
                ingest.unseal_one(path, os.path.join(target, name), seven, private_key,
                                  cms_backend)
                print(f"unsealed '{name}'")
            except (RuntimeError, OSError) as error:
                print(f"could not unseal '{os.path.basename(path)}': {error}")

        if os.listdir(target):
            mapped[directory] = target

    return mapped


def shared_dirs(runs: list) -> set:
    """The directories holding more than one run, where a log has to be attributed."""
    counts: dict = {}
    for run in runs:
        key = os.path.abspath(run.directory)
        counts[key] = counts.get(key, 0) + 1

    return {directory for directory, count in counts.items() if count > 1}


class FleetRecord(NamedTuple):
    """What 'orca fleet' wrote about one fleet: who answered, and the gaps (fleet.py)."""
    where: str             # the fleet's record: fleet\.orca (fleet.FLEET_DIR)
    hosts: list            # hosts.csv rows
    coverage: list         # coverage.csv rows


def fleet_record(directory: str) -> FleetRecord | None:
    """The fleet whose output directory 'directory' is, or None. Known by 'fleet/.orca/hosts.csv',
    which fleet writes beside its datasets and fleet.duckdb."""
    where = os.path.join(directory, _fleet.FLEET_DIR)
    if not os.path.isfile(os.path.join(where, _fleet.HOSTS_FILE)):
        return None
    return FleetRecord(where, _fleet.read_hosts(where), _fleet.read_coverage(where))


def as_directory(path: str) -> str:
    """A path as report reads it: 'fleet.duckdb' stands for the fleet directory holding it - fleet
    writes the database and its fleet\.orca record together, in one place."""
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
            dataset = _fleet.dataset_of(row, record.where)
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
            dataset = _fleet.dataset_of(row, record.where)
            if row.get("status") == "failed":
                problems.append((host, "failed", f"ingest failed: {row.get('error', '')}"))
            elif not os.path.isdir(dataset):
                problems.append((host, "missing", f"dataset missing: {dataset} "
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
                 "fleet\\.orca) or its fleet.duckdb")


def holds_outcome(directory: str) -> bool:
    """True when 'directory' has an ORC Outcome, plain or sealed: what makes it a collection."""
    if ingest.find_outcome_jsons(directory):
        return True

    suffix = OUTCOME_SUFFIX.lower() + ingest.SEALED_SUFFIX
    return any(os.path.basename(path).lower().endswith(suffix)
               for path in ingest.sealed_sidecars(directory))


def outcome_only(views: list | None, list_commands: bool, one_line: bool,
                 as_json: bool) -> bool:
    """True when nothing asked for needs a dataset: '--list', or the outcome view alone.

    Those read the Outcome and nothing else, and a sealed Outcome is a few KB to unseal - an
    ingest would decode every archive of the collection to answer it. '--one-line' and '--json'
    summarise every view, tables included, so they need the dataset.
    """
    if list_commands:
        return True
    if one_line or as_json:
        return False
    return set(expand_views(views)) <= {"outcome"}


def sort_dirs(dirs: list, given: list, outcome_only_asked: bool) -> tuple[list, list]:
    """(directories read as they are, directories whose collections are ingested first).

    A dataset and a fleet's output directory are read as they are. So is every collection when
    only its Outcome is asked for (outcome_only): a sealed Outcome is unsealed rather than the
    collection ingested. Everything else that is a collection goes through ingest.

    A directory counts as a collection when it has an Outcome. Loose archives with none count
    only in a directory named on the command line: under --recurse they would include every
    nested container of an expanded tree, and the archives kept in a dataset's own directories.
    """
    named = {os.path.normcase(os.path.abspath(directory)) for directory in given}
    as_they_are, to_ingest = [], []
    for directory in dirs:
        # A fleet's output directory holds datasets, not archives - unless the fleet ran with
        # --out-at-input, when it is the collection directory as well, and a collection.
        output_only = fleet_record(directory) and not ingest.loose_archives(directory)
        if cache.is_dataset(directory) or output_only:
            as_they_are.append(directory)
            continue

        if outcome_only_asked:
            if holds_outcome(directory):
                as_they_are.append(directory)
            continue

        explicit = os.path.normcase(os.path.abspath(directory)) in named
        if holds_outcome(directory) or (explicit and ingest.loose_archives(directory)):
            to_ingest.append(directory)

    return as_they_are, to_ingest


def ingest_dirs(directories: list, work_dir: str | None, options: dict) -> tuple[list, list]:
    """A dataset for every collection in 'directories': (datasets, [(who, why)] that failed).

    Ingested or reused (cache.ensure_dataset), and a current dataset in the other default place
    is used rather than ingested again. Ingest's progress goes to stderr: what report prints on
    stdout is the report, and '--json' or '--one-line' must stay that alone.
    """
    if directories:
        print(f"{len(directories)} collection director{'y' if len(directories) == 1 else 'ies'}"
              f": ingesting what has no current dataset", file=sys.stderr)

    datasets, failed = [], []
    with redirect_stdout(sys.stderr):
        for directory in directories:
            _collections, result, _keep = _fleet.ingest_collections(
                directory, work_dir, elsewhere=True, **options)
            datasets += [dataset for _collection, dataset in result.ingested]
            failed += [(collection.computer_name or os.path.basename(directory), message)
                       for collection, message in result.failed]

    return datasets, failed


def failure_lines(failed: list) -> list:
    """The collections that could not be ingested, as hosts: a report that drops them reads as
    a smaller, cleaner set of hosts."""
    if not failed:
        return []

    return [f"ingest failed ({len(failed)})"] + [f"  {who}: {why}" for who, why in failed]


def run_report(paths, views=None, one_line=False, as_json=False, list_commands=False,
               failed_only=False, private_key=None, cms_backend=None, work_dir=None,
               refresh=False, ignore_file=None, ignore_patterns=None, recurse=False,
               context=None, engine_name="duckdb", password=None, at_input=False,
               strict=False, jobs=None) -> None:
    """Report on every collection in every directory given.

    A report reads datasets. A collection is handed to ingest first - or its current dataset
    reused - as diff, merge and getthis do, so the logs inside its archives and the Outcome it
    sealed are read the one way ingest reads them.

    A directory is read for what it holds rather than taken for one run: a fleet arrives either
    as a directory of per-host folders or as one directory holding every machine's files side
    by side, and each of its collections is a run of its own.
    """
    if failed_only and not list_commands:
        raise RuntimeError("--failed filters the --list output; add --list")

    # Read before anything expensive: a mistyped ignore file must not surface after a fleet's
    # worth of ingest, and must never be read as "nothing to ignore".
    ignore = ignores.load(ignore_file) + ignores.patterns(ignore_patterns)
    given = [as_directory(path) for path in (paths or ["."])]
    dirs, fleets = expand_fleets(candidate_dirs(given, recurse))

    scratch.use(work_dir)
    reading_outcome = outcome_only(views, list_commands, one_line, as_json)
    as_they_are, to_ingest = sort_dirs(dirs, given, reading_outcome)
    options = dict(password=password, private_key=private_key, cms_backend=cms_backend,
                   engine_name=engine_name, jobs=jobs, refresh=refresh, strict=strict,
                   at_input=at_input)
    datasets, failed = ingest_dirs(to_ingest, work_dir, options)

    # Only the Outcome is asked for: a sealed one is unsealed into the work dir, a few KB,
    # rather than its collection ingested.
    sealed = {}
    if reading_outcome:
        sealed = unsealed_sidecar_dirs(as_they_are, work_dir or scratch.directory("report_"),
                                       private_key, cms_backend, refresh)
    runs = find_runs(as_they_are + datasets, sealed)

    reported = {run_identity(run) for run in runs}
    if not runs and not fleets and not failed:
        # Name the flag: a parent of per-host folders is the likeliest reason, and the fix is
        # one word rather than a hunt for which directory to point at.
        inside = "" if recurse else ", or add --recurse to look inside"
        raise RuntimeError(f"no collection in {', '.join(paths or ['.'])}: {NOTHING_FOUND}"
                           f"{inside}")

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
        for line in failure_lines(failed):
            print(line)
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
        hosts = fleet_json(hosts, fleets, reported, ignore) if fleets else hosts
        hosts += [{"host": who, "ok": False, "ingest": "failed", "error": why}
                  for who, why in failed]
        print(render_json(hosts))
        return
    if one_line:
        for r in reports:
            print(render_one_line(summary_of(r)))
        for line in fleet_one_lines(fleets, reported):
            print(line)
        for who, _why in failed:
            print(f"{who}: ok=False ingest=failed")
        return

    # 'explicit' is whether a view was named: a named view prints its heading even when it is
    # empty, and the default report leaves the empty ones out.
    explicit = bool(views)
    blocks = [render_fleet_block(fleets, reported, ignore)] if fleets else []
    blocks += [render_human(r, views, explicit, context) for r in reports]
    if failed:
        blocks.append("\n".join(failure_lines(failed)))
    print("\n\n".join(blocks))
