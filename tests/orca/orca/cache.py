"""Run directories: hand any archive to a command and get back an ingested run.

'orca diff NEW.7z REF.p7b' ingests each side once and reuses those runs on the next call, so
comparing a new build against the same reference repeatedly pays for the reference once. A run
is an ordinary directory beside the archive it came from: 'orca report', 'getthis' and 'merge'
all work on it, and deleting it is the only cleanup there is.
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
LAYOUT_VERSION = 4

MARKER = "cache-source.json"


def marker_path(run_dir: str) -> str:
    return os.path.join(layout.orca_dir(run_dir), MARKER)


def is_run_dir(path: str) -> bool:
    """True for a directory an ingest has already written its bookkeeping into."""
    return os.path.isdir(path) and os.path.isdir(layout.orca_dir(path))


def source_files(source: str) -> list[str]:
    """The archives whose identity a run depends on: the file itself, or every top archive an
    Outcome directory points at."""
    if os.path.isdir(source):
        return sorted(ingest.resolve_archives(source))
    return [source]


def key_for(source: str, options: dict) -> str:
    """Identity of an ingested run: each source archive's name, size and mtime, plus the ingest
    options that change what is produced.

    Deliberately not a content hash: reading a multi-GB archive over a share to decide whether
    to skip work defeats the purpose. Same size+mtime heuristic make and mypy use; '--refresh'
    is the escape hatch when it guesses wrong.
    """
    members = []
    for path in source_files(source):
        stat = os.stat(path)
        members.append({"name": os.path.basename(path), "size": stat.st_size,
                        "mtime_ns": stat.st_mtime_ns})

    material = json.dumps({"layout": LAYOUT_VERSION, "members": members, "options": options},
                          sort_keys=True)
    digest = hashlib.sha256(material.encode("utf-8")).hexdigest()[:12]
    stem = layout.archive_stem(os.path.abspath(source)) or "run"
    return f"{stem}-{digest}"


def ingest_options(exclude_cols=None, password=None, engine_name="duckdb") -> dict:
    """The part of an ingest that changes its output, as it goes into the key.

    The private key is left out on purpose: two different keys decrypt the same archive to the
    same bytes. The password is hashed rather than stored, so correcting a wrong one rebuilds.
    """
    return {
        "exclude_cols": sorted(exclude_cols) if exclude_cols else None,
        "password": hashlib.sha256(password.encode("utf-8")).hexdigest()[:8] if password else None,
        "engine": engine_name,
        "csv": False,
    }


def is_current(run_dir: str, key: str) -> bool:
    """True when 'run_dir' holds a complete run ingested from this archive with these options.

    The marker is written last, so its absence marks an interrupted ingest and the run is
    rebuilt rather than half-diffed.
    """
    if not is_run_dir(run_dir):
        return False

    try:
        with open(marker_path(run_dir), encoding="utf-8") as f:
            marker = json.load(f)
    except (ValueError, OSError):
        return False

    return marker.get("key") == key and marker.get("layout") == LAYOUT_VERSION


def clear_run(run_dir: str) -> None:
    """Remove what the previous ingest wrote, leaving everything else in the directory alone.

    A run directory holds the collection's own files and whatever 'orca extract' expanded
    there, so this follows the recorded product list rather than the directory listing.
    """
    for product in layout.products(run_dir) + list(layout.DERIVED_PARQUET):
        path = os.path.join(run_dir, product)
        if os.path.isdir(path):
            shutil.rmtree(path, ignore_errors=True)
        elif os.path.exists(path):
            os.remove(path)


def write_marker(run_dir: str, source: str, key: str, options: dict) -> None:
    os.makedirs(layout.orca_dir(run_dir), exist_ok=True)
    with open(marker_path(run_dir), "w", encoding="utf-8") as f:
        json.dump({"source": os.path.abspath(source), "key": key, "layout": LAYOUT_VERSION,
                   "options": options, "ingested": time.strftime("%Y-%m-%d %H:%M:%S")},
                  f, indent=2)


def ensure_run(source: str, work_dir: str | None = None, *, password=None, private_key=None,
               cms_backend=None, exclude_cols=None, engine_name="duckdb",
               refresh: bool = False) -> str:
    """Return a run directory for 'source', ingesting it when there is not a current one.

    A path that is already a run directory is used in place and never copied. Runs are ingested
    parquet-only: every consumer of one reads parquet, and skipping the CSV products makes the
    ingest substantially cheaper.
    """
    if is_run_dir(source):
        return source

    if not os.path.exists(source):
        raise RuntimeError(f"cache: '{source}' does not exist")

    options = ingest_options(exclude_cols, password, engine_name)
    key = key_for(source, options)
    # Beside the archive the directory name is the archive's; under one --work-dir two
    # collections' archives often share a basename, so the key names the directory there.
    keep_dir, run_dir = layout.resolve(source, work_dir, name=key if work_dir else None)

    if not refresh and is_current(run_dir, key):
        return run_dir

    clear_run(run_dir)
    os.makedirs(run_dir, exist_ok=True)
    print(f"ingesting '{source}' into '{run_dir}'")
    ingest.run_ingest(source, run_dir, make_csv=False, make_trunc=False,
                      exclude_cols=exclude_cols, password=password, private_key=private_key,
                      cms_backend=cms_backend, engine_name=engine_name, keep_dir=keep_dir,
                      refresh=refresh)

    write_marker(run_dir, source, key, options)
    return run_dir
