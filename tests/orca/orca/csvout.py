r"""CSV over a dataset's tables, written on request after the parquet.

The CSVs are a by-product: no orca command reads them back, every one reads parquet. So an
ingest writes parquet only, and '--csv' / '--csv-trunc' derive the text from a dataset that is
already there - a second ingest is not needed to get it, and nor is the collection.

  <archive>\   each table as normalized text - UTF-8, one quoting style, one file per table -
               directly in the directory of the archive it was collected in, beside that
               archive's logs and '.7z': the converted form of a file that archive holds
  csv-trunc\  the same less the volatile columns, for a readable text diff of two runs, at
               the dataset's top: it is orca's derivation, not a file of any one archive

A table no archive is recorded for (a dataset written by hand) has its CSV in the dataset's
own 'csv\'.
"""
import json
import os
from concurrent.futures import ThreadPoolExecutor

from . import layout

# A directory of their own: a top-level CSV of the collection (VSS_list.csv) carries the same
# name as the table ingested from it, and flat they would overwrite the collected file.
CSV_DIR = "csv"
CSV_TRUNC_DIR = "csv-trunc"
TRUNC_COLUMNS_FILE = "csv-trunc-columns.json"


def is_stale(parquet: str, target: str) -> bool:
    """True when 'target' has to be (re)written: missing, or older than the table it renders."""
    if not os.path.exists(target):
        return True
    return os.path.getmtime(target) < os.path.getmtime(parquet)


def trunc_columns_path(dataset: str) -> str:
    return os.path.join(layout.orca_dir(dataset), TRUNC_COLUMNS_FILE)


def read_trunc_columns(dataset: str) -> list | None:
    """The columns the existing csv-trunc\\ dropped, or None when that is not recorded."""
    try:
        with open(trunc_columns_path(dataset), encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return None


def write_trunc_columns(dataset: str, drop: list) -> None:
    with open(trunc_columns_path(dataset), "w", encoding="utf-8") as f:
        json.dump(sorted(drop), f)


def csv_dir(dataset: str, archive: str) -> str:
    """Where a table's full CSV goes, relative to the dataset: its archive's directory, else
    'csv'."""
    return archive or CSV_DIR


def write_csv(engine, dataset: str, csv: bool = False, trunc: bool = False,
              drop: list | None = None, workers: int | None = None) -> int:
    r"""Write csv\ and/or csv-trunc\ for the tables of 'dataset'. Returns how many files it wrote.

    Incremental: a table's CSV is rewritten only when missing or older than its parquet, so asking
    twice costs nothing the second time. csv-trunc\ is rewritten whole when the dropped columns
    changed, since its files would otherwise be newer than their parquet and still wrong.

    The tables come from the dataset's own record (name-map), not a walk: a tree 'orca extract'
    wrote inside the dataset must not be converted. Both directories go into products.txt, so a
    rebuild of the tables removes them rather than leaving stale text beside new parquet.

    'workers' tables at a time, as the ingest converts them: 'engine' has to be sized for that.
    """
    wanted = ([(CSV_DIR, None)] if csv else []) + ([(CSV_TRUNC_DIR, drop or [])] if trunc else [])
    if not wanted:
        return 0

    # Fail closed: a CSV directory short of a table reads as a complete export. Reached with a
    # dataset given as the source, where there is no collection to build the table again from.
    missing = layout.missing_tables(dataset)
    if missing:
        raise RuntimeError(f"'{dataset}' has no parquet for {', '.join(missing)}: ingest its "
                           f"collection again to rebuild them, then ask for the CSVs")

    passed = layout.skipped(dataset)
    if passed:
        print(f"no CSV for the {len(passed)} table(s) the ingest skipped: {', '.join(passed)}")

    retrunc = trunc and read_trunc_columns(dataset) != sorted(drop or [])
    archives = layout.table_archives(dataset)
    tasks, directories, written = [], set(), []
    for kind, dropped in wanted:
        for name in layout.table_names(dataset):
            parquet = os.path.join(dataset, name + ".parquet")
            if not os.path.exists(parquet):     # skipped at ingest, said above
                continue

            directory = (csv_dir(dataset, archives.get(name, "")) if kind == CSV_DIR
                         else CSV_TRUNC_DIR)
            directories.add(directory)
            os.makedirs(os.path.join(dataset, directory), exist_ok=True)
            target = os.path.join(dataset, directory, name + ".csv")
            if kind == CSV_DIR:
                written.append(os.path.join(directory, name + ".csv"))
            if is_stale(parquet, target) or (dropped is not None and retrunc):
                tasks.append((parquet, target, dropped))

    with ThreadPoolExecutor(max_workers=workers or os.cpu_count() or 4) as pool:
        # list(): a conversion that failed raises here rather than vanishing in the pool.
        list(pool.map(lambda task: engine.parquet_to_csv(*task), tasks))

    if trunc:
        write_trunc_columns(dataset, drop or [])
    # Each CSV in an archive's directory by name, never the directory: a rebuild removes the
    # CSVs and keeps the logs and the decrypted archive beside them. csv-trunc is orca's whole.
    layout.add_products(dataset, written + ([CSV_TRUNC_DIR] if trunc else []))
    print(f"csv: {len(tasks)} file(s) written in '{dataset}' "
          f"({', '.join(sorted(directories))})")
    return len(tasks)
