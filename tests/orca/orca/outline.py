"""Parse ORC Outline JSON into one flat row per run, for querying a fleet of collections.

The Outline says what ran where: the exact ORC build, the machine, its OS, CPU and memory, the
command line, the configured archives. Across many collections that is the table you want when
asking "which build ran on which OS", or when a result looks odd and the directory name is not
to be trusted - on a real pair, two directories named 1034_vss and 1040_vss turned out to hold
builds 23 and 62 commits past the same tag.

The row keeps the fields worth grouping by as real columns and the whole document in 'json', so
anything not promoted is still reachable with DuckDB's json_extract and no schema decision has
to be made in advance.
"""
import json
import os


def _get(data: dict, *path, default=""):
    """Nested lookup that tolerates a missing branch: Outlines differ between ORC versions."""
    current = data
    for key in path:
        if not isinstance(current, dict):
            return default
        current = current.get(key)
        if current is None:
            return default
    return current


def _count(data: dict, *path) -> int:
    value = _get(data, *path, default=[])
    return len(value) if isinstance(value, (list, dict)) else 0


def read_outline(path: str) -> dict | None:
    """The 'dfir-orc.outline' object of a JSON file, or None if this is not an Outline."""
    try:
        with open(path, encoding="utf-8") as f:
            data = json.load(f)
    except (OSError, ValueError):
        return None

    outline = data.get("dfir-orc", {}).get("outline")
    return outline if isinstance(outline, dict) else None


def outline_row(outline: dict, source: str) -> dict:
    """One run as a flat row. Field names follow the Outline's own vocabulary."""
    system = outline.get("system") or {}
    cpus = system.get("cpu") or [{}]
    first_cpu = cpus[0] if isinstance(cpus, list) and cpus else {}
    tags = _get(system, "operating_system", "tag", default=[])

    return {
        "run_id": outline.get("id", ""),
        "computer_name": outline.get("computer_name", ""),
        "system_type": outline.get("system_type", ""),
        "version": outline.get("version", ""),
        "timestamp": outline.get("timestamp", ""),
        "start": outline.get("start", ""),

        "architecture": system.get("architecture", ""),
        "hypervisor": system.get("hypervisor", ""),
        "os_description": _get(system, "operating_system", "description"),
        "os_version": _get(system, "operating_system", "version"),
        "os_locale": _get(system, "operating_system", "locale"),
        "os_install_date": _get(system, "operating_system", "install_date"),
        "os_shutdown_time": _get(system, "operating_system", "shutdown_time"),
        # Joined rather than nested: 'tags LIKE %Windows11%' is the query people write.
        "os_tags": ",".join(tags) if isinstance(tags, list) else "",

        "cpu_name": first_cpu.get("name", ""),
        "cpu_manufacturer": first_cpu.get("manufacturer", ""),
        "cpu_physical": first_cpu.get("physical_processors", ""),
        "cpu_logical": first_cpu.get("logical_processors", ""),
        "memory_physical": _get(system, "physical_memory", "physical"),
        "memory_available": _get(system, "physical_memory", "available_physical"),
        "memory_load": _get(system, "physical_memory", "current_load"),

        "volume_count": _count(system, "mounted_volumes"),
        "drive_count": _count(system, "physical_drives"),
        "adapter_count": _count(system, "network", "adapter"),
        "profile_count": _count(outline, "profile_list", "profile"),
        "archive_count": _count(outline, "archives"),

        "user_name": _get(outline, "process", "user", "username"),
        "user_sid": _get(outline, "process", "user", "SID"),
        "elevated": _get(outline, "process", "user", "elevated"),
        "syswow64": _get(outline, "process", "syswow64"),
        "command_line": _get(outline, "process", "command_line"),
        "binary": _get(outline, "process", "binary"),
        "capsule_sha1": _get(outline, "capsule", "sha1"),

        "output": outline.get("output", ""),
        "temp": outline.get("temp", ""),
        "source": source.replace("\\", "/"),
        "json": json.dumps(outline, separators=(",", ":")),
    }


def collect_rows(paths: list[str]) -> list[dict]:
    """A row per Outline. Files that are not Outlines are skipped, not fatal: a run directory
    holds whatever the collection held."""
    rows = []
    for path in paths:
        outline = read_outline(path)
        if outline is not None:
            rows.append(outline_row(outline, path))
    return rows


def outline_files(run_dir: str, extra_dirs: list | None = None) -> list[str]:
    """The Outlines ingest captured for a run. A collection can hold several - a run that was
    restarted leaves one per attempt, and only the last may have an Outcome.

    'extra_dirs' is where a sealed collection's Outlines were unsealed to.
    """
    from .ingest import find_outlines
    return find_outlines([run_dir] + list(extra_dirs or []))


def run_outline(run_dirs, root=None, out=None, engine_name="duckdb", as_json=False,
               private_key=None, cms_backend=None, work_dir=None, refresh=False) -> None:
    from . import scratch
    from .engine import get_engine, rows_to_parquet, single_process_memory_limit
    from .report import discover_run_dirs, unsealed_sidecar_dirs

    dirs = discover_run_dirs(list(run_dirs or []), root)
    if not dirs:
        raise RuntimeError("no run dirs (need an ingested run dir, or --root)")

    # A collection arrives with its sidecars sealed, and this table is read from one of them.
    scratch.use(work_dir)
    sealed = unsealed_sidecar_dirs(dirs, work_dir or scratch.directory("report_"),
                                   private_key, cms_backend, refresh)

    paths = [path for run_dir in dirs
             for path in outline_files(run_dir,
                                       [sealed[run_dir]] if run_dir in sealed else None)]
    rows = collect_rows(paths)
    if not rows:
        raise RuntimeError(f"no Outline captured in {len(dirs)} run dir(s); "
                           f"ingest a collection that carries one")

    if as_json:
        print(json.dumps([{k: v for k, v in row.items() if k != "json"} for row in rows],
                         indent=2))
        return

    out = out or os.path.join(dirs[0], "outline.parquet")
    rows_to_parquet(rows, out, get_engine(engine_name, single_process_memory_limit()))
    print(f"{out}: {len(rows)} run(s) from {len(dirs)} collection(s)")
    for row in rows:
        print(f"  {row['computer_name'] or '?':20s} {row['version']:32s} "
              f"{row['os_description'][:48]}")
