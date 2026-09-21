"""Ingest orchestration: discover top-level CSVs, build per-table parquet + CSVs, timed."""
import json
import os
import shutil
import tempfile
import time
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass

from . import cms, journal, naming, sevenzip
from .columns import DEFAULT_VOLATILE_COLUMNS
from .engine import get_engine, worker_memory_limit
from .timing import TimingRecord, write_timing_csv, summarize


@dataclass
class CsvJob:
    entry_path: str
    source_archive: str
    output_name: str
    password: str | None = None
    source_mtime: str = ""


def _jobs_from_logical(logical: list[tuple]) -> list[CsvJob]:
    # logical item = (logical_path, source_archive, entry_path, password, modified).
    names = naming.assign_names([item[0] for item in logical])
    return [CsvJob(entry_path=ep, source_archive=sa, output_name=name, password=pw,
                   source_mtime=mt)
            for (lp, sa, ep, pw, mt), name in zip(logical, names)]


def plan_jobs(entries_by_archive: list[tuple[str, list]]) -> list[CsvJob]:
    # Top-level CSV entries only (kept for unit testing the naming path).
    logical = []
    for archive, entries in entries_by_archive:
        for entry in entries:
            if entry.path.lower().endswith(".csv"):
                logical.append((entry.path, archive, entry.path, None, entry.modified))
    return _jobs_from_logical(logical)


def _manifest_row(relpath: str, entry) -> dict:
    is_csv = relpath.lower().endswith(".csv")
    return {"relpath": relpath.replace("\\", "/"), "size": entry.size, "crc": entry.crc,
            "isCsv": "true" if is_csv else "false"}


def discover_jobs(top_archives: list[str], staging_dir: str, seven_zip: str,
                  password_override: str | None) -> tuple[list[CsvJob], list[dict]]:
    """Find every CSV across the archives: top-level entries plus those one level down
    inside each nested '.7z' container (the common ORC layout). Nested containers are
    extracted once into staging_dir; their password comes from --password or the sibling
    '*_cli_config.xml'.

    Also returns a manifest row (relpath, size, crc, isCsv) for EVERY leaf entry - not just the
    CSVs - so a run's whole file set and binary-content CRCs can be compared later. The '.7z'
    containers themselves are not recorded (their leaves are)."""
    logical: list[tuple] = []
    manifest: list[dict] = []
    for top in top_archives:
        for entry in sevenzip.list_entries(seven_zip, top, password_override):
            low = entry.path.lower()
            if low.endswith(".7z"):
                # password_override is the TOP archive's password here: it is what listing 'top' above
                # used, and pulling an entry out of it needs the same one. The nested container's own
                # password is resolved separately, just below.
                container = sevenzip.extract_entry(seven_zip, top, entry.path, staging_dir,
                                                   password_override)
                if not os.path.exists(container):
                    continue
                stem = os.path.splitext(entry.path)[0]
                pw = password_override
                if not pw:
                    cfg_rel = stem + "_cli_config.xml"
                    try:
                        sevenzip.extract_entry(seven_zip, top, cfg_rel, staging_dir, password_override)
                        pw = sevenzip.password_from_cli_config(os.path.join(staging_dir, cfg_rel))
                    except Exception:
                        pw = None
                for nested in sevenzip.list_entries(seven_zip, container, pw):
                    relpath = f"{stem}/{nested.path}"
                    manifest.append(_manifest_row(relpath, nested))
                    if nested.path.lower().endswith(".csv"):
                        logical.append((relpath, container, nested.path, pw, nested.modified))
            else:
                manifest.append(_manifest_row(entry.path, entry))
                if low.endswith(".csv"):
                    logical.append((entry.path, top, entry.path, password_override, entry.modified))
    return _jobs_from_logical(logical), manifest


def resolve_archives(path: str) -> list[str]:
    if os.path.isfile(path):
        return [path]
    # Directory: find an Outcome json and read its command_set archive names. Sorted, so which
    # Outcome is read is the same every time AND is the same one find_outcome_json() picks -
    # otherwise a directory holding two runs could read one run's archives under the other run's
    # Outcome, differently on each listing.
    for entry in sorted(os.listdir(path)):
        if not entry.lower().endswith(".json"):
            continue
        full = os.path.join(path, entry)
        try:
            with open(full, encoding="utf-8") as f:
                data = json.load(f)
        except (ValueError, OSError):
            continue
        outcome = data.get("dfir-orc", {}).get("outcome")
        if not outcome:
            continue
        archives = []
        for cmd_set in outcome.get("command_set", []):
            # A command_set holds a single 'archive' object; keys in the real Outcome json
            # are lowercase ('archive'/'name'). PowerShell's case-insensitive JSON access
            # masked a capitalized lookup here that returned nothing on real data. Tolerate a
            # list too, defensively.
            archive = cmd_set.get("archive")
            if not archive:
                continue

            for one in (archive if isinstance(archive, list) else [archive]):
                name = one.get("name")
                if name:
                    archives.append(os.path.join(path, name))
        if archives:
            return archives

    # No usable Outcome: fall back to the archives sitting in the directory, the way
    # Expand-OrcArchive accepted a plain folder of results.
    loose = sorted(os.path.join(path, entry) for entry in os.listdir(path)
                   if entry.lower().endswith((".7z", ".p7b")) and not sealed_output(entry))
    if loose:
        return loose

    raise RuntimeError(f"no Orc Outcome json and no archive found in '{path}'")


# ORC seals a closed file into '<name>.7z.p7b' (WolfLauncher_Run.cpp), so an encrypted
# collection holds 'outcome.json.7z.p7b' and '<command>.log.7z.p7b' beside the real archives.
SEALED_SUFFIX = ".7z.p7b"
SEALED_OUTPUT_EXTENSIONS = (".json", ".log", ".txt", ".xml")


def sealed_name(path: str) -> str | None:
    """The name ORC sealed, for a '<name>.7z.p7b'; None when this is not a sealed file."""
    base = os.path.basename(path)
    if not base.lower().endswith(SEALED_SUFFIX):
        return None
    return base[:-len(SEALED_SUFFIX)]


def sealed_output(path: str) -> bool:
    """True for a sealed ORC *output* (an outcome, a log) rather than a sealed archive.

    Both are '<name>.7z.p7b'; what separates them is the extension underneath, because
    'SystemInfo.7z.p7b' seals an archive while 'outcome.json.7z.p7b' seals a JSON file. Without
    this the outcome would be ingested as if it were a result archive.
    """
    name = sealed_name(path)
    return bool(name) and name.lower().endswith(SEALED_OUTPUT_EXTENSIONS)


def open_sealed_outcome(source: str, work_dir: str, seven_zip: str, private_key=None,
                        cms_backend=None) -> str | None:
    """Path to the Outcome JSON inside a sealed '<name>.json.7z.p7b', or None if there is none.

    An encrypted collection seals its Outcome by default (bEncryptOutcome), so without this a
    run's whole outcome view - duration, failures, peak memory - silently disappears exactly
    when encryption is in use.
    """
    directory = source if os.path.isdir(source) else os.path.dirname(source) or "."
    try:
        entries = sorted(os.listdir(directory))
    except OSError:
        return None

    for entry in entries:
        name = sealed_name(entry)
        if not name or not name.lower().endswith(".json"):
            continue

        try:
            archive = decrypt_archive(os.path.join(directory, entry), work_dir, private_key,
                                      cms_backend)
            for inner in sevenzip.list_entries(seven_zip, archive):
                if inner.path.lower().endswith(".json"):
                    return sevenzip.extract_entry(seven_zip, archive, inner.path, work_dir)
        except (RuntimeError, OSError) as error:
            # A sealed outcome we cannot open is worth saying out loud: the rest of the run
            # still ingests, but its health view will be empty.
            print(f"could not open the sealed outcome '{entry}': {error}")

    return None


def decrypt_archive(path: str, work_dir: str, private_key: str | None = None,
                    cms_backend: str | None = None) -> str:
    """Return a path to an openable '.7z' for 'path', decrypting into 'work_dir' if needed.

    An already-plain archive is returned untouched. A '.p7b' is CMS-decrypted and then, when
    ORC journaled the stream (the default - '/no_journaling' does not), replayed into the
    archive it describes. A bare '.7zs' journal is replayed without any decryption.

    Kept separate from resolve_archives() so discovery stays free of side effects on disk.
    """
    stem = os.path.splitext(os.path.basename(path))[0]
    # ORC names an encrypted archive '<name>.7z.p7b', so dropping '.p7b' already leaves the
    # '.7z'; appending another would produce 'SystemInfo.7z.7z'.
    if stem.lower().endswith(".7z"):
        stem = stem[:-3]

    if journal.is_journaled(path):
        os.makedirs(work_dir, exist_ok=True)
        plain = os.path.join(work_dir, stem + ".7z")
        journal.replay(path, plain)
        return plain

    if os.path.splitext(path)[1].lower() != ".p7b":
        return path

    os.makedirs(work_dir, exist_ok=True)
    decrypted = os.path.join(work_dir, stem + ".7zs")
    backend = cms.decrypt(path, decrypted, private_key, cms_backend)

    if not journal.is_journaled(decrypted):
        # No JRNL header. Two unrelated things look identical here, so name which one it is.
        # '/no_journaling' puts the archive straight into the CMS content and is normal; a
        # wrong key also lands here, because CMS over AES-CBC carries no integrity check and
        # OpenSSL answers a bad RSA padding with a random session key rather than an error -
        # so cms.decrypt can return garbage and call it a success. Only the content tells them
        # apart. Either way the caller still gets a path: 7z reports the real damage, and an
        # archive is not refused just because this heuristic did not recognize it.
        plain = os.path.join(work_dir, stem + ".7z")
        os.replace(decrypted, plain)
        name = os.path.basename(path)
        if sevenzip.is_archive(plain):
            print(f"decrypted '{name}' with the {backend} backend (no_journaling)")
        else:
            print(f"warning: '{name}' decrypted with the {backend} backend to neither a JRNL "
                  f"journal nor a 7z archive - most likely the wrong private key; continuing")
        return plain

    plain = os.path.join(work_dir, stem + ".7z")
    journal.replay(decrypted, plain)
    # The journal is a GB-scale intermediate the replayed archive supersedes.
    os.remove(decrypted)
    print(f"decrypted '{os.path.basename(path)}' with the {backend} backend")
    return plain


def ingest_one(job: CsvJob, engine, out_dir: str, drop_columns, make_csv: bool,
               make_trunc: bool, seven_zip: str, password) -> list[TimingRecord]:
    records: list[TimingRecord] = []
    work = tempfile.mkdtemp(prefix="orca_")
    try:
        src = sevenzip.extract_entry(seven_zip, job.source_archive, job.entry_path, work, password)

        parquet_dir = os.path.join(out_dir, "parquet")
        os.makedirs(parquet_dir, exist_ok=True)
        parquet = os.path.join(parquet_dir, job.output_name + ".parquet")

        t0 = time.perf_counter()
        engine.csv_to_parquet(src, parquet, None)
        rows = engine.count(parquet)
        records.append(TimingRecord(job.output_name, "parquet", rows,
                                    os.path.getsize(parquet), time.perf_counter() - t0))

        if make_csv:
            csv_dir = os.path.join(out_dir, "csv")
            os.makedirs(csv_dir, exist_ok=True)

            full = os.path.join(csv_dir, job.output_name + ".csv")
            t0 = time.perf_counter()
            engine.parquet_to_csv(parquet, full, None)
            records.append(TimingRecord(job.output_name, "csv", rows,
                                        os.path.getsize(full), time.perf_counter() - t0))

            if make_trunc:
                trunc_dir = os.path.join(out_dir, "csv-trunc")
                os.makedirs(trunc_dir, exist_ok=True)
                trunc = os.path.join(trunc_dir, job.output_name + ".csv")
                t0 = time.perf_counter()
                engine.parquet_to_csv(parquet, trunc, drop_columns)
                records.append(TimingRecord(job.output_name, "csv.trunc", rows,
                                            os.path.getsize(trunc), time.perf_counter() - t0))
        return records
    finally:
        shutil.rmtree(work, ignore_errors=True)


def _write_name_map(jobs: list[CsvJob], out_dir: str) -> None:
    import csv as _csv
    with open(os.path.join(out_dir, "name-map.csv"), "w", newline="", encoding="utf-8") as f:
        w = _csv.writer(f)
        w.writerow(["name", "source_archive", "entry_path", "source_mtime"])
        for j in jobs:
            w.writerow([j.output_name, j.source_archive, j.entry_path, j.source_mtime])


def _write_manifest(rows: list[dict], out_dir: str, engine) -> None:
    """Persist the every-leaf manifest as manifest.parquet (via a scratch CSV, so the columns
    land as text like every other ingested table). Enables whole-run file-set + CRC diffs."""
    if not rows:
        return

    import csv as _csv
    csv_path = os.path.join(out_dir, "manifest.csv")
    with open(csv_path, "w", newline="", encoding="utf-8") as f:
        w = _csv.writer(f)
        w.writerow(["relpath", "size", "crc", "isCsv"])
        for r in rows:
            w.writerow([r["relpath"], r["size"], r["crc"], r["isCsv"]])

    engine.csv_to_parquet(csv_path, os.path.join(out_dir, "manifest.parquet"))
    os.remove(csv_path)


def run_ingest(archive: str, out_dir: str, *, jobs=None, make_csv=True, make_trunc=True,
               exclude_cols=None, password=None, private_key=None, cms_backend=None,
               engine_name="duckdb") -> list[TimingRecord]:
    # The worker count decides the engine's memory budget: every job runs its own duckdb, and
    # a table large enough to matter is large enough for six of them to exhaust the machine.
    workers = jobs or os.cpu_count() or 4
    engine = get_engine(engine_name, worker_memory_limit(workers))
    seven = sevenzip.find_7z()
    if not seven:
        raise RuntimeError("7z.exe not found")
    drop = list(exclude_cols) if exclude_cols else list(DEFAULT_VOLATILE_COLUMNS)

    os.makedirs(out_dir, exist_ok=True)

    # Nested containers are extracted once into staging and must outlive ingestion
    # (each job extracts its CSV from its container), so staging is cleaned only at the end.
    # A decrypted '.p7b' lands here too, for the same lifetime.
    staging = tempfile.mkdtemp(prefix="orca_stage_")
    try:
        archives = [decrypt_archive(top, staging, private_key, cms_backend)
                    for top in resolve_archives(archive)]

        # A sealed outcome needs the staging directory, so this runs inside it rather than
        # before, as the plain-JSON case used to.
        outcome_path = find_outcome_json(archive) or open_sealed_outcome(
            archive, staging, seven, private_key, cms_backend)
        if outcome_path:
            shutil.copy(outcome_path, os.path.join(out_dir, "outcome.json"))

        capture_logs(seven, archives, out_dir, password)

        work_jobs, manifest_rows = discover_jobs(archives, staging, seven, password)
        _write_name_map(work_jobs, out_dir)
        _write_manifest(manifest_rows, out_dir, engine)

        records: list[TimingRecord] = []
        with ThreadPoolExecutor(max_workers=workers) as pool:
            futures = [pool.submit(ingest_one, j, engine, out_dir, drop, make_csv,
                                   make_trunc, seven, j.password) for j in work_jobs]
            for fut in futures:
                records.extend(fut.result())
    finally:
        shutil.rmtree(staging, ignore_errors=True)

    write_timing_csv(records, os.path.join(out_dir, "ingest-timing.csv"))
    print(summarize(records))
    return records


def find_outcome_json(path: str) -> str | None:
    """Find an ORC Outcome JSON in 'path' (a dir) or next to it (if 'path' is a file)."""
    base = path if os.path.isdir(path) else os.path.dirname(path) or "."
    for entry in sorted(os.listdir(base)):
        if not entry.lower().endswith(".json"):
            continue
        full = os.path.join(base, entry)
        try:
            with open(full, encoding="utf-8") as f:
                data = json.load(f)
        except (ValueError, OSError):
            continue
        if data.get("dfir-orc", {}).get("outcome"):
            return full
    return None


def capture_logs(seven_zip: str, archives: list, out_dir: str, password) -> None:
    """Extract top-level *.log entries and any 'Errors' sub-archive into <out_dir>/logs/."""
    logs_dir = os.path.join(out_dir, "logs")
    for top in archives:
        for entry in sevenzip.list_entries(seven_zip, top, password):
            low = entry.path.lower()
            if low.endswith(".log"):
                os.makedirs(logs_dir, exist_ok=True)
                try:
                    sevenzip.extract_entry(seven_zip, top, entry.path, logs_dir, password)
                except Exception:
                    pass
            elif low.endswith(".7z") and "error" in low:
                stage = tempfile.mkdtemp(prefix="orca_log_")
                try:
                    container = sevenzip.extract_entry(seven_zip, top, entry.path, stage, password)
                    pw = password
                    if not pw:
                        cfg = os.path.splitext(entry.path)[0] + "_cli_config.xml"
                        try:
                            sevenzip.extract_entry(seven_zip, top, cfg, stage, password)
                            pw = sevenzip.password_from_cli_config(os.path.join(stage, cfg))
                        except Exception:
                            pw = None
                    dest = os.path.join(logs_dir, "Errors")
                    os.makedirs(dest, exist_ok=True)
                    sevenzip.extract_all(seven_zip, container, dest, pw)
                except Exception:
                    pass
                finally:
                    shutil.rmtree(stage, ignore_errors=True)
