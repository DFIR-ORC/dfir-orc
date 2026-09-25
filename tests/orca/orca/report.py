"""orca report: per-host views over a run dir (its timing, its Outcome and its logs).
Stdlib only. View builders are pure over parsed inputs."""
import csv
import glob
import json
import os
import re
from dataclasses import asdict

from . import layout, outcome as _outcome
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


def build_empty_view(timing: list[dict], empty_files: list[str] | None = None) -> dict:
    empty = sorted(t["name"] for t in timing if t["rows"] == 0)
    files = sorted(empty_files or [])
    return {"empty_tables": empty, "empty_files": files,
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


def build_stats_view(timing: list[dict]) -> dict:
    biggest = sorted(timing, key=lambda t: t["bytes"], reverse=True)[:5]
    slowest = sorted(timing, key=lambda t: t["seconds"], reverse=True)[:5]
    return {
        "tables": len(timing),
        "total_rows": sum(t["rows"] for t in timing),
        "total_bytes": sum(t["bytes"] for t in timing),
        "biggest": [{"name": t["name"], "bytes": t["bytes"]} for t in biggest],
        "slowest": [{"name": t["name"], "seconds": round(t["seconds"], 2)} for t in slowest],
    }


_ERROR_RE = re.compile(r"error|failed|exception", re.IGNORECASE)


def build_outcome_view(outcome_obj) -> dict:
    if outcome_obj is None:
        return {"available": False}
    return {
        "available": True,
        "ok": len(outcome_obj.failures) == 0,
        "failures": [asdict(fl) for fl in outcome_obj.failures],
        "duration_seconds": outcome_obj.duration_seconds,
        "memory_peak_bytes": outcome_obj.memory_peak_bytes,
        "memory_peak_set": outcome_obj.memory_peak_set,
    }


def run_log_files(run_dir: str) -> list[str]:
    """The logs of a run: the ones ingest put in it, plus the expanded 'Errors' container.

    Deliberately not a walk of the whole run directory - it may hold a tree 'orca extract'
    expanded, whose every command log would drown the ones this run is about.
    """
    found = sorted(glob.glob(os.path.join(run_dir, "*.log")))
    errors = os.path.join(run_dir, "Errors")
    for root, _dirs, files in os.walk(errors):
        found += sorted(os.path.join(root, name) for name in files)
    return found


def build_errors_view(run_dir: str) -> dict:
    """What went wrong in this run: the logs carrying error lines, and the tables the ingest
    could not read at all, which are a hole in the run rather than a message inside it."""
    if not os.path.isdir(run_dir):
        return {"available": False, "skipped": {}}
    logs = []
    for path in run_log_files(run_dir):
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
        logs.append({"log": os.path.relpath(path, run_dir).replace("\\", "/"),
                     "bytes": size, "error_lines": count})
    logs.sort(key=lambda x: x["error_lines"], reverse=True)
    return {"available": True, "logs": logs, "skipped": layout.skipped(run_dir)}


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


def host_of(run_dir: str) -> str:
    op = _outcome.outcome_file(run_dir)
    if op:
        try:
            host = _outcome.parse_outcome(op).host
            if host:
                return host
        except (OSError, ValueError):
            pass
    return os.path.basename(os.path.normpath(run_dir))


def build_host_report(run_dir: str, engine=None) -> dict:
    timing = load_timing(run_dir)
    op = _outcome.outcome_file(run_dir)
    oc = _outcome.parse_outcome(op) if op else None
    files = empty_output_files(engine, layout.manifest_path(run_dir)) if engine else []
    return {
        "host": host_of(run_dir),
        "run_dir": run_dir,
        "outcome": build_outcome_view(oc),
        "empty": build_empty_view(timing, files),
        "stats": build_stats_view(timing),
        "errors": build_errors_view(run_dir),
    }


def summary_of(hr: dict) -> dict:
    o, e, s = hr["outcome"], hr["empty"], hr["stats"]
    avail = o.get("available")
    return {
        "host": hr["host"],
        "ok": o.get("ok") if avail else None,
        "fails": len(o.get("failures", [])) if avail else None,
        "empty": e["count"],
        "empty_files": e.get("file_count", 0),
        "tables": s["tables"],
        "unread": len(hr["errors"].get("skipped", {})),
        "total_bytes": s["total_bytes"],
        "duration_seconds": o.get("duration_seconds") if avail else None,
        "memory_peak_bytes": o.get("memory_peak_bytes") if avail else None,
    }


def render_one_line(summary: dict) -> str:
    line = (f'{summary["host"]}: ok={summary["ok"]} fails={summary["fails"]} '
            f'empty={summary["empty"]} tables={summary["tables"]} '
            f'size={_hsize(summary["total_bytes"])} dur={_hdur(summary["duration_seconds"])} '
            f'mem={_hsize(summary["memory_peak_bytes"])}')

    # Only when there is one: a field that always reads zero stops being read, and this is the
    # one saying the run is missing a table rather than the collection missing an artifact.
    if summary.get("unread"):
        line += f' unread={summary["unread"]}'
    return line


def render_json(summaries: list) -> str:
    return json.dumps(summaries, indent=2)


def render_human(hr: dict, views: list) -> str:
    lines = [f'=== {hr["host"]}  ({hr["run_dir"]}) ===']
    if "outcome" in views:
        o = hr["outcome"]
        if not o.get("available"):
            lines.append("outcome: (no Outcome captured)")
        else:
            lines.append(f'outcome: ok={o["ok"]}, duration={_hdur(o["duration_seconds"])}, '
                         f'mem_peak={_hsize(o["memory_peak_bytes"])} ({o["memory_peak_set"]})')
            for fl in o["failures"]:
                lines.append(f'  FAIL {fl["set"]}/{fl["command"]} '
                             f'code={fl["exit_code"]} dur={_hdur(fl["duration_seconds"])}')
    if "empty" in views:
        e = hr["empty"]
        lines.append(f'empty: {e["count"]} table(s) -> '
                     f'{", ".join(e["empty_tables"]) or "(none)"}')
        if e.get("file_count"):
            lines.append(f'  {e["file_count"]} empty output file(s):')
            lines.extend(f"    {name}" for name in e["empty_files"])
    if "stats" in views:
        s = hr["stats"]
        lines.append(f'stats: {s["tables"]} tables, {s["total_rows"]} rows, '
                     f'{_hsize(s["total_bytes"])}')
        for b in s["biggest"]:
            lines.append(f'  big  {b["name"]}: {_hsize(b["bytes"])}')
        for sl in s["slowest"]:
            lines.append(f'  slow {sl["name"]}: {sl["seconds"]}s')
    if "errors" in views:
        er = hr["errors"]
        if not er.get("available"):
            lines.append("errors: (no logs captured)")
        else:
            with_err = [x for x in er["logs"] if x["error_lines"]]
            lines.append(f'errors: {len(with_err)} log(s) with matches')
            for x in with_err:
                lines.append(f'  {x["log"]}: {x["error_lines"]} error-line(s)')

        # A hole in the run rather than a message inside it, so it is said separately and
        # whether or not there were logs to read.
        skipped = er.get("skipped") or {}
        if skipped:
            lines.append(f'unread tables: {len(skipped)} (skipped at ingest)')
            for name, reason in sorted(skipped.items()):
                lines.append(f'  {name}: {reason}')
    return "\n".join(lines)


def discover_run_dirs(run_dirs: list, root) -> list:
    found = list(run_dirs or [])
    if root:
        for path in sorted(glob.glob(os.path.join(root, "*"))):
            if os.path.isfile(layout.timing_path(path)):
                found.append(path)
    seen, result = set(), []
    for d in found:
        key = os.path.normpath(d)
        if key not in seen:
            seen.add(key)
            result.append(d)
    return result


def run_report(run_dirs, root=None, views=None, one_line=False, as_json=False,
               engine_name="duckdb") -> None:
    dirs = discover_run_dirs(run_dirs, root)
    if not dirs:
        raise RuntimeError("no run dirs (need an ingested run dir, or --root)")

    # The manifest needs the engine to read; a report still prints without one, just without
    # the empty-output-file list.
    try:
        engine = get_engine(engine_name, single_process_memory_limit())
    except RuntimeError:
        engine = None
    reports = [build_host_report(d, engine) for d in dirs]

    if as_json:
        print(render_json([summary_of(r) for r in reports]))
        return
    if one_line:
        for r in reports:
            print(render_one_line(summary_of(r)))
        return
    views = views or ["outcome", "empty", "stats", "errors"]
    print("\n\n".join(render_human(r, views) for r in reports))
