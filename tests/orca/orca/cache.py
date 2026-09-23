"""Run-directory cache: hand any archive to a command and get back an ingested run.

'orca diff NEW.7z REF.p7b' ingests each side once and reuses those runs on the next call, so
comparing a new build against the same reference repeatedly pays for the reference once. A
cached entry is an ordinary run directory: 'orca report', 'getthis' and 'merge' all work on it.
"""
import hashlib
import json
import os
import shutil
import time

from . import ingest

# Bump when a run directory's layout changes in a way that makes older entries unusable.
# 2: ingest captures the collection's Outlines into '<run>/outlines/'; entries from before that
#    look complete but answer 'orca outline' with nothing.
LAYOUT_VERSION = 2

MARKER = "cache-source.json"


def default_cache_dir() -> str:
    """Where cached runs live: %LOCALAPPDATA%\\orca\\cache, or $XDG_CACHE_HOME/orca."""
    if os.name == "nt":
        base = os.environ.get("LOCALAPPDATA") or os.path.expanduser("~")
        return os.path.join(base, "orca", "cache")

    base = os.environ.get("XDG_CACHE_HOME") or os.path.join(os.path.expanduser("~"), ".cache")
    return os.path.join(base, "orca")


def is_run_dir(path: str) -> bool:
    """True for a directory ingest has already written a parquet store into."""
    return os.path.isdir(path) and os.path.isdir(os.path.join(path, "parquet"))


def source_files(source: str) -> list[str]:
    """The archives whose identity a cached run depends on: the file itself, or every top
    archive an Outcome directory points at."""
    if os.path.isdir(source):
        return sorted(ingest.resolve_archives(source))
    return [source]


def key_for(source: str, options: dict) -> str:
    """Cache key: each source archive's name, size and mtime, plus the ingest options that
    change what is produced.

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
    stem = os.path.splitext(os.path.basename(os.path.abspath(source)))[0] or "run"
    return f"{stem}-{digest}"


def ingest_options(exclude_cols=None, password=None, engine_name="duckdb") -> dict:
    """The part of an ingest that changes its output, as it goes into the cache key.

    The private key is left out on purpose: two different keys decrypt the same archive to the
    same bytes. The password is hashed rather than stored, so correcting a wrong one rebuilds.
    """
    return {
        "exclude_cols": sorted(exclude_cols) if exclude_cols else None,
        "password": hashlib.sha256(password.encode("utf-8")).hexdigest()[:8] if password else None,
        "engine": engine_name,
        "csv": False,
    }


def ensure_run(source: str, cache_dir: str | None = None, *, password=None, private_key=None,
               cms_backend=None, exclude_cols=None, engine_name="duckdb",
               refresh: bool = False) -> str:
    """Return a run directory for 'source', ingesting it into the cache when needed.

    A path that is already a run directory is used in place and never copied. Cached runs are
    ingested parquet-only: every consumer of a cached run reads parquet, and skipping the CSV
    products makes the ingest substantially cheaper.
    """
    if is_run_dir(source):
        return source

    if not os.path.exists(source):
        raise RuntimeError(f"cache: '{source}' does not exist")

    cache_dir = cache_dir or default_cache_dir()
    options = ingest_options(exclude_cols, password, engine_name)
    run_dir = os.path.join(cache_dir, key_for(source, options))

    # The marker is written last, so its absence marks an interrupted ingest and the entry is
    # rebuilt rather than half-diffed.
    if not refresh and is_run_dir(run_dir) and os.path.exists(os.path.join(run_dir, MARKER)):
        return run_dir

    shutil.rmtree(run_dir, ignore_errors=True)
    os.makedirs(run_dir, exist_ok=True)
    print(f"ingesting '{source}' into the cache ({run_dir})")
    ingest.run_ingest(source, run_dir, make_csv=False, make_trunc=False,
                      exclude_cols=exclude_cols, password=password, private_key=private_key,
                      cms_backend=cms_backend, engine_name=engine_name)

    with open(os.path.join(run_dir, MARKER), "w", encoding="utf-8") as f:
        json.dump({"source": os.path.abspath(source), "layout": LAYOUT_VERSION,
                   "options": options, "ingested": time.strftime("%Y-%m-%d %H:%M:%S")},
                  f, indent=2)
    return run_dir
