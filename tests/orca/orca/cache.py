"""Run directories: hand any archive to a command and get back an ingested run.

'orca diff NEW.7z REF.p7b' ingests each side once and reuses those runs on the next call, so
comparing a new build against the same reference repeatedly pays for the reference once. A run
is an ordinary directory - in the current directory, or beside the archive it came from with
'--out-at-input': 'orca report', 'getthis' and 'merge' all work on it, and deleting it is the
only cleanup there is.
"""
import hashlib
import json
import os
import shutil
import time

from . import ingest, layout

# Bump when a run directory's layout changes in a way that makes older entries unusable.
# 2: ingest captures the collection's Outlines into '<run>/outlines/'; entries from before that
#    look complete but answer 'orca outline' with nothing.
# 3: runs live beside the archive under its own name, so the key that used to be the directory
#    name moved into the marker.
# 4: the run is flat - tables, outcome, outline and logs sit in it directly, and orca's own
#    bookkeeping moved into '<run>/.orca/'.
# 5: every table carries OrcComputerName/OrcRunId/OrcTimestamp. Without the bump a dataset
#    ingested before this looks current and answers a fleet query with a missing column.
LAYOUT_VERSION = 5

MARKER = "cache-source.json"


def marker_path(dataset: str) -> str:
    return os.path.join(layout.orca_dir(dataset), MARKER)


def is_dataset(path: str) -> bool:
    """True for a directory an ingest has already written its bookkeeping into."""
    return os.path.isdir(path) and os.path.isdir(layout.orca_dir(path))


def source_files(source: str, archives: list | None = None) -> list[str]:
    """The archives whose identity a run depends on: the file itself, every top archive an
    Outcome directory points at, or - for one collection of a directory holding several - just
    that collection's own archives, so each host keys separately."""
    if archives is not None:
        return sorted(archives)
    if os.path.isdir(source):
        return sorted(ingest.resolve_archives(source))
    return [source]


def key_for(source: str, options: dict, archives: list | None = None) -> str:
    """Identity of an ingested run: each source archive's name, size and mtime, plus the ingest
    options that change what is produced.

    Deliberately not a content hash: reading a multi-GB archive over a share to decide whether
    to skip work defeats the purpose. Same size+mtime heuristic make and mypy use; '--refresh'
    is the escape hatch when it guesses wrong.
    """
    members = []
    for path in source_files(source, archives):
        stat = os.stat(path)
        members.append({"name": os.path.basename(path), "size": stat.st_size,
                        "mtime_ns": stat.st_mtime_ns})

    material = json.dumps({"layout": LAYOUT_VERSION, "members": members, "options": options},
                          sort_keys=True)
    digest = hashlib.sha256(material.encode("utf-8")).hexdigest()[:12]
    stem = layout.archive_stem(os.path.abspath(source)) or "run"
    return f"{stem}-{digest}"


def location_name(source: str) -> str:
    """The dataset directory name for 'source' under a shared root: its name plus a digest of
    where it is.

    The place, not the content: a re-collected archive has a new key but is the same input,
    and has to rebuild its own dataset rather than leave the stale one behind in the current
    directory. And the place, not only the name: NEW\\Little.7z and REF\\Little.7z share one.
    """
    where = os.path.normcase(os.path.abspath(source))
    digest = hashlib.sha256(where.encode("utf-8")).hexdigest()[:12]
    stem = layout.archive_stem(os.path.abspath(source)) or "run"
    return f"{stem}-{digest}"


def ingest_options(password=None, engine_name="duckdb", strict=False) -> dict:
    """The part of an ingest that changes its tables, as it goes into the key.

    The private key is left out on purpose: two different keys decrypt the same archive to the
    same bytes. The password is hashed rather than stored, so correcting a wrong one rebuilds.
    The CSVs and '--exclude-cols' are left out too: they are written over the tables on request
    (csvout) and never change them, so asking for them must not cost an ingest.
    """
    options = {
        "password": hashlib.sha256(password.encode("utf-8")).hexdigest()[:8] if password else None,
        "engine": engine_name,
    }

    # A dataset that skipped a table is not the dataset a strict ingest would have produced -
    # that one would have failed - so the two must not share a key. Only '--strict' is written
    # in: it is the exception, and recording the default would change the key of every dataset
    # ingested before today and re-ingest it for nothing.
    if strict:
        options["strict"] = True

    return options


def is_current(dataset: str, key: str) -> bool:
    """True when 'dataset' holds a complete run ingested from this archive with these options.

    The marker is written last, so its absence marks an interrupted ingest and the run is
    rebuilt rather than half-diffed.
    """
    if not is_dataset(dataset):
        return False

    try:
        with open(marker_path(dataset), encoding="utf-8") as f:
            marker = json.load(f)
    except (ValueError, OSError):
        return False

    if marker.get("key") != key or marker.get("layout") != LAYOUT_VERSION:
        return False

    # A table it recorded and no longer holds: the CSVs, a report or a diff would quietly go
    # without it, so it is rebuilt rather than reused.
    missing = layout.missing_tables(dataset)
    if missing:
        print(f"'{dataset}' is missing the parquet of {len(missing)} table(s) "
              f"({', '.join(missing[:5])}{', ...' if len(missing) > 5 else ''}); ingesting again")
    return not missing


def clear_dataset(dataset: str) -> None:
    """Remove what the previous ingest wrote, leaving everything else in the directory alone.

    A run directory holds the collection's own files and whatever 'orca extract' expanded
    there, so this follows the recorded product list rather than the directory listing.
    """
    for product in layout.products(dataset) + list(layout.DERIVED_PARQUET):
        path = os.path.join(dataset, product)
        if os.path.isdir(path):
            shutil.rmtree(path, ignore_errors=True)
        elif os.path.exists(path):
            os.remove(path)


def write_marker(dataset: str, source: str, key: str, options: dict) -> None:
    os.makedirs(layout.orca_dir(dataset), exist_ok=True)
    with open(marker_path(dataset), "w", encoding="utf-8") as f:
        json.dump({"source": os.path.abspath(source), "key": key, "layout": LAYOUT_VERSION,
                   "options": options, "ingested": time.strftime("%Y-%m-%d %H:%M:%S")},
                  f, indent=2)


def ensure_dataset(source: str, work_dir: str | None = None, *, password=None, private_key=None,
               cms_backend=None, exclude_cols=None, engine_name="duckdb",
               refresh: bool = False, strict: bool = False, collection=None,
               jobs=None, at_input: bool = False, csv: bool = False,
               csv_trunc: bool = False) -> str:
    """Return a dataset for 'source', ingesting it when there is not a current one.

    A path that is already a dataset is used in place and never copied. 'collection' narrows a
    directory holding several runs to one of them (fleet); 'jobs' is the worker count, and not
    part of the key - it changes how fast a dataset is built, not what it contains.

    'csv' / 'csv_trunc' write the CSVs over the tables, whether just built or already current
    (csvout); 'exclude_cols' is what csv-trunc drops, and only that.
    """
    if is_dataset(source):
        ingest.csv_over(source, engine_name, jobs, csv, csv_trunc, exclude_cols)
        return source

    if not os.path.exists(source):
        raise RuntimeError(f"cache: '{source}' does not exist")

    options = ingest_options(password, engine_name, strict)
    key = key_for(source, options, archives=collection.archives if collection else None)

    # One collection of a fleet directory is named after its own run: the directory's name is
    # shared by every host in it. One with no Outcome has no run name and takes its location -
    # never layout.run_name, which would hand it the first host's directory and have
    # clear_dataset empty that host's dataset before writing over it.
    if collection and collection.outcome_path:
        name = layout.run_name_for(collection.outcome_path)
    elif collection or layout.output_root(work_dir, at_input):
        # Beside the archive the directory name is the archive's; under a shared root - the
        # current directory, or --work-dir - two collections' archives often share a basename.
        name = location_name(source)
    else:
        name = None
    keep_dir, dataset = layout.resolve(source, work_dir, name=name, at_input=at_input)

    if not refresh and is_current(dataset, key):
        ingest.csv_over(dataset, engine_name, jobs, csv, csv_trunc, exclude_cols)
        return dataset

    clear_dataset(dataset)
    os.makedirs(dataset, exist_ok=True)
    print(f"ingesting '{source}' into '{dataset}'")
    # 'work_dir' as well as the two directories resolved from it: those say where the products
    # go, and run_ingest also puts its scratch there - a diff of two large collections has the
    # same no-room-on-the-temp-volume problem as anything else.
    ingest.run_ingest(source, dataset, make_csv=csv, make_trunc=csv_trunc,
                      collection=collection, jobs=jobs,
                      exclude_cols=exclude_cols, password=password, private_key=private_key,
                      cms_backend=cms_backend, engine_name=engine_name, keep_dir=keep_dir,
                      work_dir=work_dir, refresh=refresh, strict=strict, cache_key=key)
    return dataset
