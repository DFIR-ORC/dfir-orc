"""Build the wide dfir-orc.parquet from a run's file-keyed per-table parquet: one row per
(VolumeID, SnapshotID, FRN).

Two modes:
- scalar (default): keep the most-recent value per column (arg_max by recency). Fast, one pass.
- lossless: keep every distinct value as a most-recent-first JSON array. Built memory-frugally
  (materialize unified -> UNPIVOT -> collapse per (key,col) -> PIVOT back), since a single
  array_agg over all columns at once exhausts memory. Noticeably slower.

Cells are VARCHAR: a plain scalar, or (lossless) a JSON-array string when values vary."""
import csv as _csv
import os
import shutil
import tempfile
import time

from . import layout
from .engine import get_engine, single_process_memory_limit, sql_path

KEY_COLUMNS = ("VolumeID", "SnapshotID", "FRN")

DEFAULT_TS_COLUMNS: dict[str, str] = {
    "NTFSInfo": "LastModificationDate",
    "USNInfo": "TimeStamp",
    "GetThis": "SampleCollectionDate",
    "I30Info": "FileNameLastModificationDate",
}


def tool_timestamp_column(name: str, overrides: dict | None = None) -> str | None:
    mapping = dict(DEFAULT_TS_COLUMNS)
    if overrides:
        mapping.update(overrides)
    for prefix, column in mapping.items():
        if name.startswith(prefix):
            return column
    return None


def master_columns(tables: list[dict]) -> list[str]:
    cols: set[str] = set()
    for table in tables:
        cols.update(table["columns"])
    return sorted(c for c in cols if c not in KEY_COLUMNS)


def _table_select(table: dict, master_cols: list[str]) -> str:
    """Per-table projection: the three key columns, a __recency timestamp, and every master
    column cast to VARCHAR (NULL where the table lacks it). Same-named columns align by name."""
    cols = set(table["columns"])
    vol = 'CAST("VolumeID" AS VARCHAR)'
    snap = 'CAST("SnapshotID" AS VARCHAR)' if "SnapshotID" in cols else "CAST(NULL AS VARCHAR)"
    frn = 'CAST("FRN" AS VARCHAR)'

    recency_terms = []
    ts_col = table.get("ts_col")
    if ts_col and ts_col in cols:
        recency_terms.append(f'try_cast("{ts_col}" AS TIMESTAMP)')
    mtime = table.get("mtime")
    if mtime:
        recency_terms.append(f"try_cast('{mtime}' AS TIMESTAMP)")
    recency = f"COALESCE({', '.join(recency_terms)})" if recency_terms else "CAST(NULL AS TIMESTAMP)"

    value_exprs = []
    for c in master_cols:
        if c in cols:
            value_exprs.append(f'CAST("{c}" AS VARCHAR) AS "{c}"')
        else:
            value_exprs.append(f'CAST(NULL AS VARCHAR) AS "{c}"')

    return (f"SELECT {vol} AS k_vol, {snap} AS k_snap, {frn} AS k_frn, "
            f"{recency} AS __recency, " + ", ".join(value_exprs) +
            f" FROM read_parquet('{sql_path(table['path'])}')")


def build_unified(tables: list[dict], master_cols: list[str]) -> str:
    return " UNION ALL ".join(f"({_table_select(t, master_cols)})" for t in tables)


def build_scalar_sql(tables: list[dict], master_cols: list[str]) -> str:
    """One value per column: the most recent by __recency (arg_max). Spillable, single pass."""
    unified = build_unified(tables, master_cols)
    exprs = ", ".join(f'arg_max("{c}", __recency) AS "{c}"' for c in master_cols)
    return (f"SELECT COALESCE(k_vol,'') AS VolumeID, COALESCE(k_snap,'') AS SnapshotID, "
            f"k_frn AS FRN, {exprs} FROM ({unified}) "
            "GROUP BY COALESCE(k_vol,''), COALESCE(k_snap,''), k_frn")


def build_lossless_sql(unified_path: str, master_cols: list[str]) -> str:
    """Lossless arrays, memory-frugal: UNPIVOT the (already materialized) unified parquet to a
    tall (key, col, val) form, collapse each (key, col) to scalar-or-JSON-array (recency
    ordered), then PIVOT back to wide. master_cols is unused in the SQL (the UNPIVOT discovers
    columns) but kept for signature symmetry with build_scalar_sql."""
    excl = "k_vol, k_snap, k_frn, __recency"
    collapse = (
        "SELECT k_vol, k_snap, k_frn, col, "
        "CASE WHEN count(DISTINCT val) <= 1 THEN max(val) "
        "ELSE CAST(to_json(list_distinct(array_agg(val ORDER BY __recency DESC) "
        "FILTER (WHERE val IS NOT NULL))) AS VARCHAR) END AS merged "
        f"FROM (UNPIVOT read_parquet('{sql_path(unified_path)}') ON COLUMNS(* EXCLUDE ({excl})) "
        "INTO NAME col VALUE val) WHERE val IS NOT NULL "
        "GROUP BY k_vol, k_snap, k_frn, col")
    pivot = f"PIVOT ({collapse}) ON col USING first(merged) GROUP BY k_vol, k_snap, k_frn"
    return (f"SELECT COALESCE(k_vol,'') AS VolumeID, COALESCE(k_snap,'') AS SnapshotID, "
            f"k_frn AS FRN, * EXCLUDE (k_vol, k_snap, k_frn) FROM ({pivot})")


def read_name_map_mtimes(name_map_path: str) -> dict[str, str]:
    mtimes: dict[str, str] = {}
    if not os.path.exists(name_map_path):
        return mtimes
    with open(name_map_path, newline="", encoding="utf-8", errors="replace") as f:
        for row in _csv.DictReader(f):
            mtimes[row["name"]] = row.get("source_mtime", "") or ""
    return mtimes


def _discover_tables(engine, run_dir: str, ts_overrides: dict | None) -> list[dict]:
    mtimes = read_name_map_mtimes(layout.name_map_path(run_dir))
    tables = []
    for name, path in sorted(layout.table_paths(run_dir).items()):
        columns = engine.columns(path)
        if "VolumeID" not in columns or "FRN" not in columns:
            continue
        tables.append({
            "name": name,
            "path": path.replace("\\", "/"),
            "columns": columns,
            "ts_col": tool_timestamp_column(name, ts_overrides),
            "mtime": mtimes.get(name, ""),
        })
    return tables


def run_merge(run_dir: str, out: str | None = None, lossless: bool = False,
              ts_overrides: dict | None = None, engine_name: str = "duckdb") -> str:
    # The merge has duckdb to itself and is the heaviest query orca runs.
    engine = get_engine(engine_name, single_process_memory_limit())
    tables = _discover_tables(engine, run_dir, ts_overrides)
    if not tables:
        raise RuntimeError(f"no file-keyed (VolumeID+FRN) parquet found in '{run_dir}'")

    cols = master_columns(tables)
    out = out or os.path.join(run_dir, "dfir-orc.parquet")

    t0 = time.perf_counter()
    if lossless:
        # Materialize unified once, then the memory-frugal UNPIVOT/PIVOT build.
        staging = tempfile.mkdtemp(prefix="orca_merge_")
        try:
            unified_pq = os.path.join(staging, "unified.parquet").replace("\\", "/")
            engine.query_to_parquet(build_unified(tables, cols), unified_pq)
            engine.query_to_parquet(build_lossless_sql(unified_pq, cols), out)
        finally:
            shutil.rmtree(staging, ignore_errors=True)
    else:
        engine.query_to_parquet(build_scalar_sql(tables, cols), out)
    elapsed = time.perf_counter() - t0

    rows = engine.count(out)
    mode = "lossless" if lossless else "scalar"
    print(f"dfir-orc.parquet ({mode}): {rows} rows, {len(cols)} columns, "
          f"{len(tables)} tables, {round(elapsed, 1)}s")
    return out
