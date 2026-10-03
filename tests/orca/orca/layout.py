"""Where orca's derived files go: the rules that turn an input path into an output path.

Everything an ingest derives from an archive lands beside it, in a directory named after the
archive with its archive extensions dropped, so one place holds everything that came out of one
archive. A decrypted archive and an unsealed sidecar (outcome, outline, log) keep the encrypted
file's name with only the extension changed, and sit next to the encrypted original.

An extract is the exception and does not come through here: it writes under its own '--out',
the current directory by default, and leaves the collection untouched.

Pure path arithmetic, apart from resolve(), which has to ask whether the collection can be
written to at all.
"""
import csv
import glob
import os

from . import scratch

# Extensions ORC wraps a payload in. Stripped repeatedly, so '<name>.7z.p7b' collapses in one
# call; anything else ('.json', '.log') is the payload's own extension and stays.
ARCHIVE_EXTENSIONS = (".p7b", ".7z", ".7zs")

# Sidecars whose name carries ORC's own identity for a run, most authoritative first. A
# collection ingests as a single run whatever the number of archives in it, so its run
# directory is named after the run rather than after any one of them.
RUN_NAME_SUFFIXES = ("_Outcome.json", "_Outline.json")

# orca's own bookkeeping inside a run: files nobody reads by hand, kept out of the way of the
# collection's own files, which now share the directory.
ORCA_DIR = ".orca"

# Tables the other commands derive from a run and write into it. They are parquet sitting next
# to the ingested tables, so whatever answers "which parquet is a collected table" has to know
# them - name-map.csv does; a bare glob does not.
DERIVED_PARQUET = ("dfir-orc.parquet", "outcome.parquet", "outline.parquet", "getthis.parquet")


def archive_stem(path: str) -> str:
    """'ORC_Offline.7z.p7b' -> 'ORC_Offline'; 'ORC_Outcome.json.7z.p7b' -> 'ORC_Outcome.json'."""
    name = os.path.basename(path)
    stripping = True
    while stripping:
        stripping = False
        for extension in ARCHIVE_EXTENSIONS:
            if len(name) > len(extension) and name.lower().endswith(extension):
                name = name[:-len(extension)]
                stripping = True
    return name


def _beside(path: str, work_dir: str | None) -> str:
    return work_dir if work_dir else os.path.dirname(os.path.abspath(path))


def run_name(directory: str) -> str:
    """The name ORC gave the run in 'directory', read from the sidecars it wrote there.

    'ORC_WS_H2ODEV_20260921_172528_Outcome.json' -> 'ORC_WS_H2ODEV_20260921_172528', sealed or
    not - the name is the same either way, so a collection does not change dataset the
    moment it is unsealed. A folder of loose archives carries no such sidecar and is named
    after itself.
    """
    try:
        entries = sorted(os.listdir(directory))
    except OSError:
        entries = []

    for suffix in RUN_NAME_SUFFIXES:
        for entry in entries:
            stem = archive_stem(entry)
            if len(stem) > len(suffix) and stem.lower().endswith(suffix.lower()):
                return stem[:-len(suffix)]

    return os.path.basename(os.path.abspath(directory))


def derived_dir(source: str, work_dir: str | None = None, name: str | None = None) -> str:
    """The directory holding everything orca derives from 'source'.

    A collection directory holds every archive of one ORC run, and they ingest into a single
    run, so its dataset goes inside it under the run's own name rather than beside any
    one archive. 'name' overrides that, which matters under --work-dir: two collections' runs
    often share a name there.
    """
    if os.path.isdir(source):
        return os.path.join(work_dir or source, name or run_name(source))

    return os.path.join(_beside(source, work_dir), name or archive_stem(source))


def decrypted_path(archive: str, work_dir: str | None = None) -> str:
    """Where the plain '.7z' of an encrypted or journaled archive is written."""
    return os.path.join(_beside(archive, work_dir), archive_stem(archive) + ".7z")


def unsealed_path(sealed: str, work_dir: str | None = None) -> str:
    """Where the plain payload of a sealed sidecar ('<name>.7z.p7b') is written."""
    return os.path.join(_beside(sealed, work_dir), archive_stem(sealed))


def is_writable(directory: str) -> bool:
    """True when orca can create a file in 'directory', creating the directory if needed."""
    probe = os.path.join(directory, ".orca-write-probe")
    try:
        os.makedirs(directory, exist_ok=True)
        with open(probe, "w"):
            pass
        os.remove(probe)
        return True
    except OSError:
        return False


def resolve(source: str, work_dir: str | None = None,
            name: str | None = None) -> tuple[str, str]:
    """(keep_dir, dataset) for 'source': where the decrypted archives and unsealed sidecars are
    kept, and where the dataset goes.

    Both fall back to scratch when the collection cannot be written to - a share mounted
    read-only, a mounted image, read-only media. What orca keeps beside a collection is a
    convenience for the next run, so losing it beats refusing to run.
    """
    keep = work_dir or (source if os.path.isdir(source) else
                        os.path.dirname(os.path.abspath(source)))
    run = derived_dir(source, work_dir, name)
    if is_writable(keep):
        return keep, run

    fallback = scratch.directory("readonly_")
    print(f"'{keep}' is read-only; keeping orca's products in '{fallback}' for this run")
    return fallback, os.path.join(fallback, os.path.basename(run))


def orca_dir(dataset: str) -> str:
    return os.path.join(dataset, ORCA_DIR)


def name_map_path(dataset: str) -> str:
    return os.path.join(orca_dir(dataset), "name-map.csv")


def timing_path(dataset: str) -> str:
    return os.path.join(orca_dir(dataset), "ingest-timing.csv")


def skipped_path(dataset: str) -> str:
    """Where an ingest records the tables it could not produce. Absent means none."""
    return os.path.join(orca_dir(dataset), "skipped.txt")


def skipped(dataset: str) -> dict[str, str]:
    """name -> why, for the tables '--skip-errors' could not produce. Empty for a full run."""
    try:
        with open(skipped_path(dataset), encoding="utf-8", errors="replace") as f:
            lines = f.read().splitlines()
    except OSError:
        return {}

    found = {}
    for line in lines:
        name, _, reason = line.partition("\t")
        if name:
            found[name] = reason
    return found


def manifest_path(dataset: str) -> str:
    return os.path.join(orca_dir(dataset), "manifest.parquet")


def products_path(dataset: str) -> str:
    return os.path.join(orca_dir(dataset), "products.txt")


def products(dataset: str) -> list[str]:
    """What the last ingest wrote into 'dataset', by name.

    A run shares its directory with the collection's own files and with whatever 'orca extract'
    expanded there, so this list - not the directory listing - is what a rebuild may remove.
    """
    path = products_path(dataset)
    try:
        with open(path, encoding="utf-8") as f:
            return [line.strip() for line in f if line.strip()]
    except OSError:
        return [ORCA_DIR]


def table_names(dataset: str) -> list[str]:
    """The tables an ingest produced, in the order it named them.

    Read from name-map.csv rather than globbed: the run directory holds the collection's own
    files too, and a glob would offer 'dfir-orc.parquet' and friends as if a tool had collected
    them. The glob is the fallback for a run directory written by hand.
    """
    path = name_map_path(dataset)
    if not os.path.exists(path):
        return [os.path.basename(p)[:-len(".parquet")]
                for p in sorted(glob.glob(os.path.join(dataset, "*.parquet")))
                if os.path.basename(p) not in DERIVED_PARQUET]

    with open(path, newline="", encoding="utf-8", errors="replace") as f:
        return [row["name"] for row in csv.DictReader(f) if row.get("name")]


def table_paths(dataset: str) -> dict[str, str]:
    """name -> parquet path, for the tables of a run that actually exist on disk."""
    found = {}
    for name in table_names(dataset):
        path = os.path.join(dataset, name + ".parquet")
        if os.path.exists(path):
            found[name] = path
    return found
