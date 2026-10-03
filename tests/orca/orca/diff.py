"""orca diff: compare two parquet/CSV (or two run dirs) on selected columns.
Pure column-selection + SQL builders here; counts/row-export go through the engine."""
import json
import os

from . import layout
from .columns import DEFAULT_VOLATILE_COLUMNS
from .engine import sql_path


def reader_for(path: str) -> str:
    return "read_parquet" if path.lower().endswith(".parquet") else "read_csv_auto"


def select_columns(cols_a, cols_b, columns=None, exclude_cols=None):
    set_a, set_b = set(cols_a), set(cols_b)
    shared = [c for c in cols_a if c in set_b]
    only_a = [c for c in cols_a if c not in set_b]
    only_b = [c for c in cols_b if c not in set_a]
    if columns:
        compare = [c for c in columns if c in set_a and c in set_b]
    else:
        drop = set(DEFAULT_VOLATILE_COLUMNS) | set(exclude_cols or [])
        compare = [c for c in shared if c not in drop]
    return compare, only_a, only_b


def _proj(reader: str, path: str, cols) -> str:
    sel = ", ".join(f'CAST("{c}" AS VARCHAR) AS "{c}"' for c in cols)
    return f"SELECT {sel} FROM {reader}('{path}')"


def build_except_sql(lr, lp, rr, rp, cols) -> str:
    """Rows in (lr,lp) not in (rr,rp), over cols (EXCEPT ALL)."""
    return f"({_proj(lr, lp, cols)}) EXCEPT ALL ({_proj(rr, rp, cols)})"


def build_except_count_sql(lr, lp, rr, rp, cols) -> str:
    return f"SELECT count(*) AS n FROM ({build_except_sql(lr, lp, rr, rp, cols)})"


def build_ordered_sql(inner_sql: str, key_cols) -> str:
    """Order both sides by the pairing key so a row and the row it replaces land in the same
    --show window; an unordered LIMIT would otherwise cut counterparts apart."""
    if not key_cols:
        return inner_sql
    order = ", ".join(f'"{c}"' for c in key_cols)
    return f"SELECT * FROM ({inner_sql}) ORDER BY {order}"


def diff_tables(engine, path_a, path_b, columns=None, exclude_cols=None,
                rows_out_dir=None, name=None) -> dict:
    cols_a = engine.columns(path_a)
    cols_b = engine.columns(path_b)
    compare, only_a, only_b = select_columns(cols_a, cols_b, columns, exclude_cols)
    if not compare:
        return {"compatible": False, "added": 0, "removed": 0, "compare_cols": [],
                "only_in_a": only_a, "only_in_b": only_b, "path_a": path_a, "path_b": path_b}

    ra, rb = reader_for(path_a), reader_for(path_b)
    pa, pb = sql_path(path_a), sql_path(path_b)
    added = int(engine.query_scalar(build_except_count_sql(ra, pa, rb, pb, compare)))
    removed = int(engine.query_scalar(build_except_count_sql(rb, pb, ra, pa, compare)))

    if rows_out_dir:
        os.makedirs(rows_out_dir, exist_ok=True)
        base = name or os.path.splitext(os.path.basename(path_a))[0]
        if added:
            engine.query_to_csv(build_except_sql(ra, pa, rb, pb, compare),
                                os.path.join(rows_out_dir, base + ".added.csv"))
        if removed:
            engine.query_to_csv(build_except_sql(rb, pb, ra, pa, compare),
                                os.path.join(rows_out_dir, base + ".removed.csv"))

    return {"compatible": True, "added": added, "removed": removed, "compare_cols": compare,
            "only_in_a": only_a, "only_in_b": only_b, "path_a": path_a, "path_b": path_b}


def _match_parquet(run_a, run_b):
    """Split the tables into compared, one-sided, and never read at all.

    A table '--skip-errors' could not produce is missing from its run, and one-sided would call
    that added or removed - saying the collection changed, when what changed is that orca could
    not read the file. It is reported as unread instead, on whichever side lost it, and it is
    never compared: there is nothing to compare it against.
    """
    ia, ib = layout.table_paths(run_a), layout.table_paths(run_b)
    unread = set(layout.skipped(run_a)) | set(layout.skipped(run_b))
    common = sorted(set(ia) & set(ib))
    only_a = sorted(set(ia) - set(ib) - unread)
    only_b = sorted(set(ib) - set(ia) - unread)
    return common, only_a, only_b, sorted(unread)


def diff_runs(engine, run_a, run_b, columns=None, exclude_cols=None, rows_out_dir=None) -> dict:
    common, only_a, only_b, unread = _match_parquet(run_a, run_b)
    ia, ib = layout.table_paths(run_a), layout.table_paths(run_b)
    tables = {}
    for n in common:
        tables[n] = diff_tables(engine, ia[n], ib[n], columns, exclude_cols, rows_out_dir, n)
    return {"tables": tables, "added_tables": only_a, "removed_tables": only_b,
            "unread_tables": unread}


def _manifest_reader(path: str, exclude_files=None) -> str:
    """A manifest reader, optionally without the paths a comparison should ignore.

    Comparing two versions of the same collection, every run-local artifact shows up as a
    difference: on a real 10.3.4 vs 10.4.0 pair the 27 "changed" binaries were all logs and
    Statistics.json, and the one added/removed file was the Capsule marker, whose name carries
    a timestamp and a GUID. This is the file-level counterpart of --exclude-cols, and of the
    -Exclude the PowerShell Compare-OrcManifest took.
    """
    reader = f"read_parquet('{sql_path(path)}')"
    if not exclude_files:
        return reader

    # '*' is the glob users think in; SQL LIKE wants '%'.
    clauses = " OR ".join(f"relpath ILIKE '{sql_path(pattern).replace('*', '%')}'"
                          for pattern in exclude_files)
    return f"(SELECT * FROM {reader} WHERE NOT ({clauses}))"


def build_manifest_added_sql(new_reader: str, ref_reader: str) -> str:
    """Count leaf files in NEW whose relpath is absent from REF (anti-join)."""
    return (f"SELECT count(*) AS n FROM {new_reader} a "
            f"LEFT JOIN {ref_reader} b USING (relpath) WHERE b.relpath IS NULL")


def build_manifest_changed_sql(new_reader: str, ref_reader: str, is_csv: bool) -> str:
    """Count files present on both sides whose crc differs, restricted to CSV or binary leaves."""
    csv_op = "=" if is_csv else "<>"
    return (f"SELECT count(*) AS n FROM {new_reader} a JOIN {ref_reader} b USING (relpath) "
            f"WHERE a.crc <> b.crc AND lower(a.isCsv) {csv_op} 'true'")


def diff_manifest(engine, manifest_new: str, manifest_ref: str, exclude_files=None) -> dict:
    """Whole-run file-set + CRC diff, the analysis equivalent of Compare-OrcManifest: files added
    (in NEW only), removed (in REF only), and content-changed split into binary vs CSV leaves."""
    a = _manifest_reader(manifest_new, exclude_files)
    b = _manifest_reader(manifest_ref, exclude_files)
    return {
        "added": int(engine.query_scalar(build_manifest_added_sql(a, b))),
        "removed": int(engine.query_scalar(build_manifest_added_sql(b, a))),
        "changed_binary": int(engine.query_scalar(build_manifest_changed_sql(a, b, is_csv=False))),
        "changed_csv": int(engine.query_scalar(build_manifest_changed_sql(a, b, is_csv=True))),
    }


def render_manifest_line(man: dict) -> str:
    return (f"  files: +{man['added']} / -{man['removed']}  "
            f"changed-binary: {man['changed_binary']}  changed-csv: {man['changed_csv']}")


def table_line(name: str, dt: dict) -> str:
    if not dt["compatible"]:
        return (f'{name}: schema-incompatible '
                f'(only_a={dt["only_in_a"]}, only_b={dt["only_in_b"]})')
    extra = ""
    if dt["only_in_a"] or dt["only_in_b"]:
        extra = f' cols +{dt["only_in_a"]} -{dt["only_in_b"]}'
    return f'{name}: +{dt["added"]} / -{dt["removed"]} rows{extra}'


def render_pair_human(a: str, b: str, dt: dict) -> str:
    name = os.path.basename(a)
    return f"{a} vs {b}\n  {table_line(name, dt)}"


def render_run_human(run_a: str, run_b: str, result: dict, manifest: dict | None = None) -> str:
    lines = [f"{run_a} vs {run_b}"]
    if manifest:
        lines.append(render_manifest_line(manifest))
    for n in sorted(result["tables"]):
        lines.append("  " + table_line(n, result["tables"][n]))
    if result["added_tables"]:
        lines.append(f'  added tables: {", ".join(result["added_tables"])}')
    if result["removed_tables"]:
        lines.append(f'  removed tables: {", ".join(result["removed_tables"])}')
    if result.get("unread_tables"):
        lines.append(f'  unread tables (skipped at ingest, not compared): '
                     f'{", ".join(result["unread_tables"])}')
    return "\n".join(lines)


# Identity columns used to pair a NEW row with the REF row it replaces, per ORC tool. Matched
# against the table-name prefix; columns absent from the compared set are dropped, and an empty
# result disables pairing (rows then render as plain +/- lines).
# Most discriminating column first: it is the one shown next to the changed values, and the one
# the sampling ORDER BY leads with.
KEY_COLUMNS_BY_TOOL: dict = {
    "NTFSInfo": ("FRN", "FilenameID", "DataID", "ADS", "SnapshotID", "VolumeID"),
    "I30Info": ("FRN", "Name", "SnapshotID", "VolumeID"),
    "USNInfo": ("USN", "FRN", "SnapshotID", "VolumeID"),
    "SecDescr": ("ID", "SnapshotID", "VolumeID"),
    "GetThis": ("FRN", "SampleName", "SnapshotID", "VolumeID"),
    "timeline": ("FRN", "FilenameID", "KindOfDate", "SnapshotID", "VolumeID"),
}

# Columns a human reads to recognize a row. Shown next to the changed values because the pairing
# key is mostly numeric: it identifies a row without naming it.
LABEL_COLUMNS: tuple = ("File", "Name", "SampleName", "FullName", "FullPath")


def key_columns_for(table_name: str, compare_cols, override=None) -> list:
    if override:
        return [c for c in override if c in compare_cols]
    for tool, keys in KEY_COLUMNS_BY_TOOL.items():
        if table_name.lower().startswith(tool.lower()):
            return [c for c in keys if c in compare_cols]
    return []


def anchor_columns_for(compare_cols, key_cols, max_labels=2) -> list:
    """Identity columns to display: the readable labels plus the leading key column. Falls back
    to the whole key for tables that carry no name-like column (SecDescr, volstats)."""
    labels = [c for c in LABEL_COLUMNS if c in compare_cols][:max_labels]
    if not labels:
        return list(key_cols)
    return labels + [c for c in key_cols[:1] if c not in labels]


def _key_of(r: dict, key_cols) -> tuple:
    return tuple(r.get(c, "") for c in key_cols)


def pair_rows(added, removed, key_cols):
    """Match each NEW row with the REF row carrying the same key, so the pair can be reported as
    one modification. Rows with no counterpart (and duplicates beyond the first) stay unpaired."""
    if not key_cols:
        return [], list(added), list(removed)

    buckets: dict = {}
    for index, r in enumerate(removed):
        buckets.setdefault(_key_of(r, key_cols), []).append(index)

    pairs, unpaired_added, consumed = [], [], set()
    for r in added:
        bucket = buckets.get(_key_of(r, key_cols))
        if not bucket:
            unpaired_added.append(r)
            continue

        index = bucket.pop(0)
        consumed.add(index)
        pairs.append((r, removed[index]))

    unpaired_removed = [r for i, r in enumerate(removed) if i not in consumed]
    return pairs, unpaired_added, unpaired_removed


def changed_columns(new_row: dict, ref_row: dict, cols) -> tuple:
    return tuple(c for c in cols if new_row.get(c, "") != ref_row.get(c, ""))


def group_pairs_by_change(pairs, cols) -> list:
    """Group modifications by which columns changed. Heterogeneous rows collapse into a handful
    of patterns, and the pattern itself is usually the answer being looked for."""
    groups: dict = {}
    for new_row, ref_row in pairs:
        groups.setdefault(changed_columns(new_row, ref_row, cols), []).append((new_row, ref_row))
    return sorted(groups.items(), key=lambda item: (-len(item[1]), item[0]))


def _cell(value, max_cell: int) -> str:
    text = "" if value is None else str(value)
    return text if len(text) <= max_cell else text[:max_cell - 3] + "..."


def fit_columns(cols, widths, max_width: int) -> list:
    """Keep the leading columns fitting in max_width; the +/- marker takes the first 2 chars."""
    shown, used = [], 2
    for col in cols:
        need = widths[col] + 2  # 2-space gap between columns
        if shown and used + need > max_width:
            break
        shown.append(col)
        used += need
    return shown


def _render_marked(cols, marked, max_cell: int, max_width: int, indent: str = ""):
    """Aligned '+'/'-' block over cols; returns the lines and how many columns did not fit."""
    widths = {c: len(c) for c in cols}
    for col in cols:
        for _, r in marked:
            widths[col] = max(widths[col], len(_cell(r.get(col, ""), max_cell)))

    shown = fit_columns(cols, widths, max_width)
    lines = [indent + "  " + "  ".join(col.ljust(widths[col]) for col in shown)]
    for mark, r in marked:
        lines.append(indent + mark + " "
                     + "  ".join(_cell(r.get(col, ""), max_cell).ljust(widths[col])
                                 for col in shown))
    # Trailing padding on the last column only hurts when the block is copied out of a terminal.
    return [line.rstrip() for line in lines], len(cols) - len(shown)


def render_modification_groups(groups, anchor_cols, max_groups=10, rows_per_group=2,
                               max_cell=40, max_width=200) -> str:
    """One block per change pattern: the columns that differ, then sample rows showing only the
    key plus those columns. This is what makes a +1/-1 pair readable on a 46-column table."""
    lines = []
    for signature, pairs in groups[:max_groups]:
        label = ", ".join(signature) if signature else "(duplicate count only)"
        count = f"{len(pairs)} row" + ("s" if len(pairs) != 1 else "")
        shown_pairs = pairs[:rows_per_group]
        if len(shown_pairs) < len(pairs):
            count += f", showing {len(shown_pairs)}"
        lines.append(f"  changed: {label}   ({count})")

        cols = list(anchor_cols) + [c for c in signature if c not in anchor_cols]
        marked = []
        for new_row, ref_row in shown_pairs:
            marked.append(("+", new_row))
            marked.append(("-", ref_row))
        block, hidden = _render_marked(cols, marked, max_cell, max_width, indent="  ")
        lines.extend(block)
        if hidden:
            lines.append(f"    (+{hidden} more columns, use --rows for full data)")

    if len(groups) > max_groups:
        lines.append(f"  (+{len(groups) - max_groups} more change patterns, "
                     f"use --patterns for the full list)")
    return "\n".join(lines)


def render_rows_table(cols, added, removed, total_added, total_removed,
                      max_cell=40, max_width=200) -> str:
    """Aligned preview of differing rows: '+' = in NEW only, '-' = in REF only. Cells are
    truncated to max_cell, and only the columns that fit within max_width are shown (wide tables
    like NTFSInfo would be unreadable otherwise); the remaining columns are noted."""
    marked = [("+", r) for r in added] + [("-", r) for r in removed]
    if not marked:
        return ""

    lines, hidden = _render_marked(cols, marked, max_cell, max_width)

    shown_added = len(added)
    notes = []
    if shown_added < total_added or (len(marked) - shown_added) < total_removed:
        notes.append(f"showing +{shown_added}/-{len(marked) - shown_added} "
                     f"of +{total_added}/-{total_removed}")
    if hidden:
        notes.append(f"+{hidden} more columns (use --rows for full data)")
    if notes:
        lines.append("  (" + "; ".join(notes) + ")")
    return "\n".join(lines)


def sample_table(engine, dt: dict, limit: int, name=None, key_override=None,
                 max_groups=10, max_cell=40, max_width=200) -> str:
    """Fetch up to 'limit' differing rows per side and render them. When the rows pair up on an
    identity key they are reported as modifications grouped by what changed; anything left over
    (and any table without a key) falls back to the plain +/- row table."""
    if not dt.get("compatible") or not (dt["added"] or dt["removed"]):
        return ""

    compare = dt["compare_cols"]
    ra, rb = reader_for(dt["path_a"]), reader_for(dt["path_b"])
    pa, pb = sql_path(dt["path_a"]), sql_path(dt["path_b"])
    key_cols = key_columns_for(name or os.path.basename(dt["path_a"]), compare, key_override)

    added_sql = build_ordered_sql(build_except_sql(ra, pa, rb, pb, compare), key_cols)
    removed_sql = build_ordered_sql(build_except_sql(rb, pb, ra, pa, compare), key_cols)
    added = engine.query_rows(added_sql, limit) if dt["added"] else []
    removed = engine.query_rows(removed_sql, limit) if dt["removed"] else []

    pairs, added, removed = pair_rows(added, removed, key_cols)
    if not pairs:
        return render_rows_table(compare, added, removed, dt["added"], dt["removed"],
                                 max_cell, max_width)

    header = (f"  paired on {', '.join(key_cols)}: {len(pairs)} modified, "
              f"{len(added)} added-only, {len(removed)} removed-only "
              f'(sampled from +{dt["added"]}/-{dt["removed"]} rows)')
    groups = group_pairs_by_change(pairs, compare)
    anchors = anchor_columns_for(compare, key_cols)
    blocks = [header, render_modification_groups(groups, anchors, max_groups, max_cell=max_cell,
                                                 max_width=max_width)]
    leftovers = render_rows_table(compare, added, removed, len(added), len(removed),
                                  max_cell, max_width)
    if leftovers:
        blocks.append("  unpaired rows:\n" + leftovers)
    return "\n".join(blocks)


def render_one_line(label: str, payload: dict) -> str:
    return f'{label}: {json.dumps(payload, separators=(",", ":"))}'


def render_json(payload: dict) -> str:
    return json.dumps(payload, indent=2)


def _pair_summary(dt: dict) -> dict:
    return {"mode": "pair", "added": dt["added"], "removed": dt["removed"],
            "compatible": dt["compatible"], "only_in_a": dt["only_in_a"],
            "only_in_b": dt["only_in_b"]}


def _run_summary(result: dict) -> dict:
    tables = result["tables"]
    changed = [n for n, d in tables.items()
               if d["added"] or d["removed"] or d["only_in_a"] or d["only_in_b"]]
    return {
        "mode": "run",
        "tables": len(tables),
        "changed": len(changed),
        "added_rows": sum(d["added"] for d in tables.values()),
        "removed_rows": sum(d["removed"] for d in tables.values()),
        "added_tables": result["added_tables"],
        "removed_tables": result["removed_tables"],
        "unread_tables": result.get("unread_tables", []),
        "per_table": tables,
    }


def is_table(path: str) -> bool:
    """A parquet/CSV file diffs as a table; anything else (archive, result dir, run dir) is a
    whole run."""
    return os.path.splitext(path)[1].lower() in (".parquet", ".csv")


def run_diff(path_a=None, path_b=None, run_a=None, run_b=None, columns=None,
             exclude_cols=None, rows=False, out_dir="orca-diff", one_line=False,
             as_json=False, show=None, key=None, patterns=10, engine_name="duckdb",
             work_dir=None, refresh=False, password=None, private_key=None,
             cms_backend=None, exclude_files=None, skip_errors=False) -> None:
    from . import cache
    from .engine import get_engine, single_process_memory_limit
    rows_out = out_dir if rows else None

    # Two positional archives (or result/run dirs) mean a run diff, not a file-pair diff.
    if not (run_a or run_b) and path_a and path_b and not (is_table(path_a) or is_table(path_b)):
        run_a, run_b, path_a, path_b = path_a, path_b, None, None

    if run_a and run_b:
        # Keep the paths the user typed for the report: a cache directory name is a hash.
        source_a, source_b = run_a, run_b
        resolve = dict(work_dir=work_dir, refresh=refresh, password=password,
                       private_key=private_key, cms_backend=cms_backend,
                       exclude_cols=exclude_cols, engine_name=engine_name,
                       skip_errors=skip_errors)
        run_a = cache.ensure_dataset(run_a, **resolve)
        run_b = cache.ensure_dataset(run_b, **resolve)

        # One duckdb at a time, so it gets the machine budget (see engine.single_process_*).
        # Taken here rather than on entry: ensure_dataset may have just spent minutes ingesting two
        # archives with a duckdb per job, and the budget has to describe the machine as it is when
        # the queries below run, not as it was when the command started.
        engine = get_engine(engine_name, single_process_memory_limit())

        result = diff_runs(engine, run_a, run_b, columns, exclude_cols, rows_out)
        summary = _run_summary(result)

        # Whole-run file-set + CRC layer, when both runs carry a manifest (older runs may not).
        man_new = layout.manifest_path(run_a)
        man_ref = layout.manifest_path(run_b)
        manifest = None
        if os.path.exists(man_new) and os.path.exists(man_ref):
            manifest = diff_manifest(engine, man_new, man_ref, exclude_files)
            summary["manifest"] = manifest

        if as_json:
            print(render_json(summary))
        elif one_line:
            payload = {k: summary[k] for k in ("tables", "changed", "added_rows", "removed_rows")}
            # Only when there are any: a one-liner that always carries a zero teaches people
            # to skip it, and this is the field that says the comparison is incomplete.
            if summary["unread_tables"]:
                payload["unread_tables"] = len(summary["unread_tables"])
            if manifest:
                payload.update({"files_added": manifest["added"], "files_removed": manifest["removed"],
                                "changed_binary": manifest["changed_binary"]})
            print(render_one_line(f"{source_a} vs {source_b}", payload))
        else:
            print(render_run_human(source_a, source_b, result, manifest))
            if show:
                for n in sorted(result["tables"]):
                    text = sample_table(engine, result["tables"][n], show, name=n,
                                        key_override=key, max_groups=patterns)
                    if text:
                        print(f"\n{n}:")
                        print(text)
        return

    if not (path_a and path_b):
        raise RuntimeError("provide two files, or --run and --reference")

    # Same budget, same reason, for a plain two-file diff.
    engine = get_engine(engine_name, single_process_memory_limit())
    dt = diff_tables(engine, path_a, path_b, columns, exclude_cols, rows_out)
    summary = _pair_summary(dt)
    if as_json:
        print(render_json(summary))
    elif one_line:
        print(render_one_line(f"{os.path.basename(path_a)} vs {os.path.basename(path_b)}",
                              {"added": dt["added"], "removed": dt["removed"]}))
    else:
        print(render_pair_human(path_a, path_b, dt))
        if show:
            name = os.path.splitext(os.path.basename(path_a))[0]
            text = sample_table(engine, dt, show, name=name, key_override=key,
                                max_groups=patterns)
            if text:
                print(text)
