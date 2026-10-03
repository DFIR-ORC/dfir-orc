"""GetThis collection summary: how many files each GetThis container archive collected, plus the
grand total. Also emits getthis.parquet (all collected samples, one row per file)."""
import csv
import json
import os

from . import cache, layout
from .engine import get_engine, sql_path as _fwd


def discover_getthis(dataset: str) -> list[str]:
    """The GetThis tables of a run, taken by name rather than by glob: the run directory also
    holds this command's own 'getthis.parquet', which a case-insensitive glob would match."""
    return [path for name, path in sorted(layout.table_paths(dataset).items())
            if name.startswith("GetThis")]


def glob_reader(dataset: str) -> str:
    """A single read_parquet over every GetThis table. Schemas are uniform, but union_by_name
    tolerates any column-order drift between command sets."""
    paths = ", ".join(f"'{_fwd(path)}'" for path in discover_getthis(dataset))
    return f"read_parquet([{paths}], union_by_name=true)"


def read_source_archives(name_map_path: str) -> dict[str, str]:
    """table name -> containing archive basename (e.g. 'ADS.7z'), from a run's name-map.csv."""
    mapping: dict[str, str] = {}
    if not os.path.exists(name_map_path):
        return mapping

    with open(name_map_path, newline="", encoding="utf-8", errors="replace") as f:
        for row in csv.DictReader(f):
            source = row.get("source_archive", "") or ""
            mapping[row["name"]] = os.path.basename(source) if source else ""
    return mapping


def counts_by_archive(engine, dataset: str) -> list[tuple[str, int]]:
    """(container archive name, collected-file count) per GetThis container, files summed per
    archive and ordered by count descending."""
    archives = read_source_archives(layout.name_map_path(dataset))
    totals: dict[str, int] = {}
    for path in discover_getthis(dataset):
        name = os.path.splitext(os.path.basename(path))[0]
        archive = archives.get(name) or name  # fall back to the table name if unmapped
        totals[archive] = totals.get(archive, 0) + engine.count(path)
    return sorted(totals.items(), key=lambda kv: (-kv[1], kv[0]))


def render_summary(total: int, by_archive: list[tuple[str, int]]) -> str:
    lines = [f"GetThis: {total} files collected"]
    width = max((len(archive) for archive, _ in by_archive), default=0)
    for archive, count in by_archive:
        lines.append(f"  {archive.ljust(width)}  {count}")
    return "\n".join(lines)


def run_getthis(source: str, out: str | None = None, engine_name: str = "duckdb",
                as_json: bool = False, password=None, private_key=None, cms_backend=None,
                work_dir=None, refresh: bool = False, strict: bool = False) -> str | None:
    """Summarise what GetThis collected, ingesting 'source' first when it is not a dataset yet.

    Same reasoning as merge: this reads the GetThis tables of a dataset, and only 'ingest'
    writes them.
    """
    try:
        dataset = cache.ensure_dataset(source, work_dir, password=password,
                                       private_key=private_key, cms_backend=cms_backend,
                                       engine_name=engine_name, refresh=refresh,
                                       strict=strict)
    except RuntimeError as error:
        # The reason arrives as a page of duckdb SQL, so the last line says what to do about
        # it - but only when '--strict' is what refused: an ingest can also fail because the
        # key is wrong or there is no archive there, and 'drop --strict' would be nonsense.
        hint = ("\n  that is what --strict asks for; without it the tables that do read are "
                "ingested" if strict else "")
        raise RuntimeError(f"could not ingest '{source}': {error}{hint}") from error
    engine = get_engine(engine_name)
    paths = discover_getthis(dataset)
    if not paths:
        print("GetThis: no GetThis tables in the dataset")
        return None

    by_archive = counts_by_archive(engine, dataset)
    total = sum(count for _, count in by_archive)

    out = out or os.path.join(dataset, "getthis.parquet")
    engine.query_to_parquet(
        f"SELECT FullName, * EXCLUDE (FullName) FROM {glob_reader(dataset)}", out)

    if as_json:
        print(json.dumps({"files": total, "parquet": out,
                          "by_archive": [{"archive": a, "files": c} for a, c in by_archive]}))
    else:
        print(render_summary(total, by_archive))

    return out
