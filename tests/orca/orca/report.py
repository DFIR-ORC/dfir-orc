"""orca report: per-host views over a run dir (its timing, its Outcome and its logs).
Stdlib only. View builders are pure over parsed inputs."""
import csv
import glob
import hashlib
import json
import os
import re
from dataclasses import asdict

from . import ingest, layout, outcome as _outcome, scratch, sevenzip
from .engine import get_engine, single_process_memory_limit, sql_path


def load_timing(run_dir: str) -> list[dict]:
    path = layout.timing_path(run_dir)
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
                     available: bool = True, files_available: bool = True) -> dict:
    """Empty tables (from the ingest timing) and empty ORC outputs (from the manifest).

    Two inputs, two availability flags, because they go missing independently: a run ingested
    without duckdb has real timing and no manifest, and one flag would relabel one missing
    input as the other. Unlike build_outcome_view, the keys stay present when unavailable -
    a view with two halves cannot collapse to a single {"available": False}.
    """
    empty = sorted(t["name"] for t in timing if t["rows"] == 0)
    files = sorted(empty_files or [])
    return {"available": available, "files_available": files_available,
            "empty_tables": empty, "empty_files": files,
            "count": len(empty), "file_count": len(files)}


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


_ERROR_RE = re.compile(r"error|failed|exception", re.IGNORECASE)


def build_outcome_view(outcome_obj) -> dict:
    if outcome_obj is None:
        return {"available": False}
    return {
        "available": True,
        "ok": len(outcome_obj.failures) == 0,
        "commands": outcome_obj.commands,
        "failures": [asdict(fl) for fl in outcome_obj.failures],
        "duration_seconds": outcome_obj.duration_seconds,
        "memory_peak_bytes": outcome_obj.memory_peak_bytes,
        "memory_peak_set": outcome_obj.memory_peak_set,
    }


def run_log_files(run_dir: str, extra_dirs: list | None = None) -> list[str]:
    """The logs of a run: the ones ingest put in it, plus the expanded 'Errors' container.

    Deliberately not a walk of the whole run directory - it may hold a tree 'orca extract'
    expanded, whose every command log would drown the ones this run is about.

    'extra_dirs' is where a sealed collection's log was unsealed to.
    """
    found = []
    for directory in [run_dir] + list(extra_dirs or []):
        found += sorted(glob.glob(os.path.join(directory, "*.log")))
        for root, _dirs, files in os.walk(os.path.join(directory, "Errors")):
            found += sorted(os.path.join(root, name) for name in files)

    return found


def log_label(path: str, run_dir: str) -> str:
    """How a log is named in the report: its path within the run, else its file name alone.

    An unsealed sidecar sits in the work dir, which is routinely on another volume than the
    collection - a UNC share and a local temp dir, say - and relpath across mounts raises on
    Windows rather than returning a path. Even on one volume it would return a line of '..'
    segments, which names nothing useful.
    """
    try:
        relative = os.path.relpath(path, run_dir)
    except ValueError:
        return os.path.basename(path)

    if relative.startswith(".."):
        return os.path.basename(path)

    return relative.replace("\\", "/")


def build_errors_view(run_dir: str, extra_dirs: list | None = None) -> dict:
    """What went wrong in this run: the logs carrying error lines, and the tables the ingest
    could not read at all, which are a hole in the run rather than a message inside it."""
    if not os.path.isdir(run_dir):
        return {"available": False, "skipped": {}}
    logs = []
    for path in run_log_files(run_dir, extra_dirs):
        size = os.path.getsize(path)
        if size == 0:
            continue

        count = 0
        try:
            with open(path, encoding="utf-8", errors="ignore") as f:
                for line in f:
                    if _ERROR_RE.search(line):
                        count += 1
        except OSError:
            count = -1
        logs.append({"log": log_label(path, run_dir), "bytes": size, "error_lines": count})
    logs.sort(key=lambda x: x["error_lines"], reverse=True)

    # 'available' is whether there was a log to read, not whether the directory exists: a
    # sealed collection holds only '<name>.log.7z.p7b', and '0 log(s) with matches' would then
    # read as reassurance when nothing had been read at all.
    return {"available": bool(run_log_files(run_dir, extra_dirs)), "logs": logs,
            "skipped": layout.skipped(run_dir)}


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


def host_of(run_dir: str, extra_dirs: list | None = None) -> str:
    """The machine the collection came from, by its Outcome; the directory name as a fallback.

    A sealed collection has no readable Outcome until its sidecars are unsealed, and would
    otherwise be reported under the name of the folder it happens to sit in.
    """
    op = _outcome.outcome_file(run_dir, extra_dirs)
    if op:
        try:
            host = _outcome.parse_outcome(op).host
            if host:
                return host
        except (OSError, ValueError):
            pass
    return os.path.basename(os.path.normpath(run_dir))


def build_host_report(run_dir: str, engine=None, extra_dirs: list | None = None) -> dict:
    """The four views of one run dir, each told whether its own input was there to read.

    Only 'ingest' writes the '.orca' directory, and report is pointed at a directory 'extract'
    produced often enough - ORC's Outcome and logs are both in one - so two of the four views
    routinely have nothing to read, and have to say so rather than report a zero.
    """
    timing = load_timing(run_dir)
    timing_available = os.path.exists(layout.timing_path(run_dir))
    manifest_available = bool(engine) and os.path.exists(layout.manifest_path(run_dir))

    op = _outcome.outcome_file(run_dir, extra_dirs)
    oc = _outcome.parse_outcome(op) if op else None
    files = empty_output_files(engine, layout.manifest_path(run_dir)) if engine else []
    return {
        "host": host_of(run_dir, extra_dirs),
        "run_dir": run_dir,
        "outcome": build_outcome_view(oc),
        "empty": build_empty_view(timing, files, timing_available, manifest_available),
        "stats": build_stats_view(timing, timing_available),
        "errors": build_errors_view(run_dir, extra_dirs),
    }


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
        "unread": len(hr["errors"].get("skipped", {})),
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

    # Only when there is one: a field that always reads zero stops being read, and these two
    # say the run is missing a table, or ORC wrote an output and put nothing in it, rather
    # than the ordinary state of affairs. None stays quiet too - an unread manifest must not
    # read as 'no empty files'.
    for field in ("empty_files", "unread"):
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
    outcome, empty, stats, errors = hr["outcome"], hr["empty"], hr["stats"], hr["errors"]
    return {
        **summary_of(hr),
        "run_dir": hr["run_dir"],
        "outcome_available": outcome.get("available", True),
        "stats_available": stats.get("available", True),
        "empty_available": empty.get("available", True),
        "empty_files_available": empty.get("files_available", True),
        "errors_available": errors.get("available", True),
        "memory_peak_set": outcome.get("memory_peak_set", ""),
        "failures": outcome.get("failures", []),
        "empty_tables": empty.get("empty_tables", []),
        "empty_files": empty.get("empty_files", []),
        "biggest": stats.get("biggest", []),
        "slowest": stats.get("slowest", []),
        "logs": errors.get("logs", []),
        "skipped": errors.get("skipped", {}),
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


def failure_rows(outcome: dict) -> list:
    """The failed commands, with the columns a '--list' line uses: set, command, tool,
    duration, and the exit code last."""
    return [[failure["set"], failure["command"], failure.get("tool") or "-",
             _hdur(failure["duration_seconds"]), str(failure["exit_code"])]
            for failure in outcome.get("failures", [])]


def table_rows(stats: dict) -> list:
    """The tables worth looking at, size and rows and time on one line each."""
    return [[table["name"], _hsize(table["bytes"]), f'{table["rows"]} rows',
             f'{table["seconds"]:.2f}s']
            for table in stats.get("top", [])]


def log_rows(errors: dict) -> list:
    """Only the logs that carried something: a log read and found clean needs no line."""
    return [[log["log"], str(log["error_lines"])]
            for log in errors.get("logs", []) if log["error_lines"]]


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
    path = short_path(hr["run_dir"], MAX_WIDTH - len(host) - 2)
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
    if rows or explicit:
        lines.append("")
        lines += section(f"failed commands ({len(rows)})", rows, "<<<>>")

    return lines


def stats_lines(hr: dict, explicit: bool) -> list:
    stats = hr["stats"]
    if not stats.get("available", True):
        return ["  tables: (no ingest timing: not an ingested run)"]

    rows = table_rows(stats)
    if not rows and not explicit:
        return []

    heading = (f'tables ({stats["tables"]}, {stats["total_rows"]} rows, '
               f'{_hsize(stats["total_bytes"])})')
    return section(heading, rows, "<>>>")


def empty_lines(hr: dict, explicit: bool) -> list:
    empty = hr["empty"]
    if not empty.get("available", True):
        return ["  empty tables: (no ingest timing: not an ingested run)"]

    lines = []
    tables = [[name] for name in empty.get("empty_tables", [])]
    if tables or explicit:
        lines += section(f"empty tables ({len(tables)})", tables, "<")

    if not empty.get("files_available", True):
        return lines + ["  empty outputs: (not read: no manifest or no duckdb)"]

    files = [[name] for name in empty.get("empty_files", [])]
    if files or explicit:
        lines += section(f"empty outputs ({len(files)})", files, "<")

    return lines


def errors_lines(hr: dict, explicit: bool) -> list:
    errors = hr["errors"]
    lines = []
    if not errors.get("available", True):
        lines.append("  logs: (no logs captured)")
    else:
        rows = log_rows(errors)
        if rows or explicit:
            read = len(errors.get("logs", []))
            lines += section(f"logs with errors ({len(rows)} of {read})", rows, "<>")

    # A hole in the run rather than a message inside it, so it is said separately and whether
    # or not there were logs to read.
    skipped = skipped_rows(errors.get("skipped") or {})
    if skipped or explicit:
        lines += section(f"unread tables ({len(skipped)})", skipped, "<<")

    return lines


def render_human(hr: dict, views: list, explicit: bool = False) -> str:
    """The default report: the host, its scalars, then one counted section per finding.

    Every row of every section is printed. Sections with nothing in them are left out - the
    scalars already say 'fails=0', and a run with no findings should read as a short report
    rather than a page of zeros - but a view named explicitly prints its heading even when it
    is empty, because asking for a view and getting nothing back cannot be told from a broken
    report.
    """
    lines = header_lines(hr)
    if "outcome" in views:
        lines += outcome_lines(hr, explicit)
    for name, builder in (("stats", stats_lines), ("empty", empty_lines),
                          ("errors", errors_lines)):
        if name not in views:
            continue

        built = builder(hr, explicit)
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
        str(row.get("exit_code", "")),
    ]


def sort_command_rows(rows: list[dict]) -> list[dict]:
    """By host, then by the order ORC ran them: a machine's commands stay together and read
    as the timeline they were."""
    return sorted(rows, key=lambda row: (row.get("computer_name") or "",
                                         row.get("start") or ""))


# Left for the names, right for the numbers, so durations, peaks and exit codes line up on
# their last digit across hosts whose names are nothing like the same length.
_COMMAND_ALIGNMENT = ("<", "<", "<", "<", ">", ">", ">")


def render_command_lines(rows: list[dict], failed_only: bool = False) -> str:
    """One aligned line per command, across every host, with no per-host headers.

    The output to scan when a fleet is expected to be clean: with --failed and nothing wrong,
    all that is left is the summary. The summary counts every command either way - a filtered
    '1 failed' with no denominator says nothing.
    """
    selected = [row for row in sort_command_rows(rows)
                if not failed_only or command_failed(row)]
    hosts = {row.get("computer_name") or "?" for row in rows}
    failed = sum(1 for row in rows if command_failed(row))
    summary = (f"  {len(rows)} command(s), {failed} failed, {len(hosts)} host(s)")

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


def holds_a_collection(directory: str) -> bool:
    """True when 'directory' holds an ORC Outcome, plain or sealed.

    What makes a directory worth reporting on before anything is ingested, which is how a
    folder of collections as they arrive can be swept. The sealed case is recognised by the
    name ORC gives it ('<name>_Outcome.json.7z.p7b') and never by decrypting: discovery has to
    stay cheap and must not need a key. A sealed log on its own says nothing - only an Outcome
    says this is a collection.
    """
    if not os.path.isdir(directory):
        return False

    if _outcome.outcome_file(directory):
        return True

    return any((ingest.sealed_name(path) or "").lower().endswith("outcome.json")
               for path in ingest.sealed_sidecars(directory))


def _extra(sealed: dict, run_dir: str) -> list | None:
    """The 'extra_dirs' for one run: where its sidecars were unsealed, if any were."""
    found = sealed.get(run_dir)
    return [found] if found else None


def sidecar_dir(run_dir: str, work: str) -> str:
    """Where one run's unsealed sidecars go: a directory of its own under the work dir.

    Named after the collection plus a digest of its path, following the convention the run
    cache uses, because two machines' collections routinely share a folder name under one work
    dir and must not unseal over each other.
    """
    digest = hashlib.sha256(os.path.abspath(run_dir).encode("utf-8")).hexdigest()[:12]
    return os.path.join(work, f"{os.path.basename(os.path.normpath(run_dir))}-{digest}")


def unsealed_sidecar_dirs(dirs: list, work: str, private_key=None, cms_backend=None,
                          refresh: bool = False) -> dict:
    """Unseal the sealed sidecars of every collection in 'dirs'; map run dir -> where they went.

    A collection arrives with its Outcome, Outlines and log each sealed into a '.7z.p7b', and
    without them a report can say almost nothing: not the machine's name, not which command
    failed. They are unsealed into the work dir and never beside the collection - 'ingest' and
    'extract' do that on purpose, to be reused, but a report only reads and must leave what it
    reads as it was. They are a few KB, so paying for the decryption again next time is nothing.

    Collections with nothing sealed are absent from the map, so the common case creates no
    directory at all.
    """
    mapped = {}
    seven = None
    for run_dir in dirs:
        if not ingest.sealed_sidecars(run_dir):
            continue

        if seven is None:
            seven = sevenzip.find_7z() or ""
        if not seven:
            # Not fatal: the views that read ORC's own files go quiet, the rest still print.
            print(f"cannot unseal the sidecars of '{run_dir}': 7z.exe not found")
            continue

        target = sidecar_dir(run_dir, work)
        os.makedirs(target, exist_ok=True)
        ingest.unseal_sidecars(run_dir, seven, target, private_key, cms_backend, refresh)
        mapped[run_dir] = target

    return mapped


def discover_run_dirs(run_dirs: list, root) -> list:
    """The run dirs to report on: the ones named, plus every ingested run under 'root'.

    A named directory that is not there is an error rather than a report about nothing - it
    used to come back as a host with no Outcome and no tables, which reads like a finding.
    """
    for path in (run_dirs or []):
        if not os.path.isdir(path):
            raise RuntimeError(f"no such run dir: '{path}'")

    found = list(run_dirs or [])
    if root:
        for path in sorted(glob.glob(os.path.join(root, "*"))):
            if os.path.isfile(layout.timing_path(path)) or holds_a_collection(path):
                found.append(path)
    seen, result = set(), []
    for d in found:
        key = os.path.normpath(d)
        if key not in seen:
            seen.add(key)
            result.append(d)
    return result


def run_report(run_dirs, root=None, views=None, one_line=False, as_json=False,
               list_commands=False, failed_only=False, private_key=None, cms_backend=None,
               work_dir=None, refresh=False, engine_name="duckdb") -> None:
    dirs = discover_run_dirs(run_dirs, root)
    if not dirs:
        raise RuntimeError("no run dirs (need an ingested run dir, or --root)")

    if failed_only and not list_commands:
        raise RuntimeError("--failed filters the --list output; add --list")

    scratch.use(work_dir)
    sealed = unsealed_sidecar_dirs(dirs, work_dir or scratch.directory("report_"),
                                   private_key, cms_backend, refresh)

    if list_commands:
        # The same rows 'orca report outcome' writes to parquet, so the text and the table can
        # never disagree - and no engine is needed to print them.
        print(render_command_lines(_outcome.collect_command_rows(dirs, sealed), failed_only))
        without = [d for d in dirs if not _outcome.outcome_file(d, _extra(sealed, d))]
        if without:
            # Named, not raised: one machine missing an Outcome must not cost you the other
            # eight, and a silent gap in a fleet sweep is worse than a short note.
            print(f'  no Outcome in {len(without)} run dir(s): {", ".join(without)}')
        return

    # The manifest needs the engine to read; a report still prints without one, just without
    # the empty-output-file list.
    try:
        engine = get_engine(engine_name, single_process_memory_limit())
    except RuntimeError:
        engine = None
    reports = [build_host_report(d, engine, _extra(sealed, d)) for d in dirs]

    if as_json:
        print(render_json([host_json(r) for r in reports]))
        return
    if one_line:
        for r in reports:
            print(render_one_line(summary_of(r)))
        return
    explicit = bool(views)
    views = views or ["outcome", "empty", "stats", "errors"]
    print("\n\n".join(render_human(r, views, explicit) for r in reports))
