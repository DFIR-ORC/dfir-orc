"""Ingest orchestration: discover top-level CSVs, build per-table parquet + CSVs, timed."""
import json
import os
import shutil
import time
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass

from . import cms, journal, layout, naming, scratch, sevenzip
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


def resolve_archives(path: str, outcome_dir: str | None = None) -> list[str]:
    """The archives of the collection at 'path', named by its Outcome where there is one.

    'outcome_dir' is where to read that Outcome from when it does not sit beside the archives:
    'orca extract' unseals it into the output tree so the collection stays untouched. The
    archive names it holds are still resolved against 'path', where the archives are.
    """
    if os.path.isfile(path):
        return [path]
    # Directory: find an Outcome json and read its command_set archive names. Sorted, so which
    # Outcome is read is the same every time AND is the same one find_outcome_json() picks -
    # otherwise a directory holding two runs could read one run's archives under the other run's
    # Outcome, differently on each listing.
    reading = outcome_dir or path
    for entry in sorted(os.listdir(reading)):
        if not entry.lower().endswith(".json"):
            continue
        full = os.path.join(reading, entry)
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
    entries = os.listdir(path)
    sealed = {entry.lower() for entry in entries if entry.lower().endswith(SEALED_SUFFIX)}
    loose = sorted(os.path.join(path, entry) for entry in entries
                   if entry.lower().endswith((".7z", ".p7b")) and not sealed_output(entry)
                   # 'X.7z' beside 'X.7z.p7b' is the decrypted copy orca kept, not a second
                   # collection: ingesting both would ingest the same data twice.
                   and entry.lower() + ".p7b" not in sealed)
    if loose:
        return loose

    raise RuntimeError(f"no Orc Outcome json and no archive found in '{path}'")


# ORC seals a closed file into '<name>.7z.p7b' (WolfLauncher_Run.cpp), so an encrypted
# collection holds 'outcome.json.7z.p7b' and '<command>.log.7z.p7b' beside the real archives.
SEALED_SUFFIX = ".7z.p7b"
SEALED_OUTPUT_EXTENSIONS = (".json", ".log", ".txt", ".xml")

# ORC collects the failing commands' output into an 'Errors' sub-archive; it is expanded into a
# directory of that name, the way 'orca extract' expands any container.
ERRORS_DIR = "Errors"

# The CSV renderings of the tables keep a directory of their own: a top-level CSV of the
# collection (VSS_list.csv) carries the same name as the table ingested from it, and flat they
# would overwrite the collected file.
CSV_DIR = "csv"
CSV_TRUNC_DIR = "csv-trunc"


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


def collection_dir(source: str) -> str:
    """The directory holding a collection: 'source' itself, or the one its archive sits in."""
    return source if os.path.isdir(source) else os.path.dirname(os.path.abspath(source))


def sidecar_dirs(source: str, keep_dir: str) -> list[str]:
    """Where a collection's plain sidecars can be found: beside the archives, plus - when that
    directory is read-only - the scratch directory they had to be unsealed into."""
    directory = collection_dir(source)
    if os.path.abspath(keep_dir) == os.path.abspath(directory):
        return [directory]
    return [directory, keep_dir]


def sealed_sidecars(directory: str) -> list[str]:
    """Every sealed ORC output ('<name>.7z.p7b' over a json/log/txt/xml) in 'directory'."""
    try:
        entries = sorted(os.listdir(directory))
    except OSError:
        return []

    return [os.path.join(directory, entry) for entry in entries if sealed_output(entry)]


def unseal_one(sealed: str, target: str, seven_zip: str, private_key=None,
               cms_backend=None) -> str:
    """Decrypt one sealed sidecar and put the file it holds at 'target'."""
    work = scratch.directory("unseal_")
    archive = decrypt_archive(sealed, work, private_key, cms_backend)
    entries = sevenzip.list_entries(seven_zip, archive)
    if not entries:
        raise RuntimeError("the sealed archive holds no file")

    extracted = sevenzip.extract_entry(seven_zip, archive, entries[0].path, work)
    if os.path.exists(target):
        os.remove(target)

    # move, not replace: the scratch directory and the collection are rarely on the same volume.
    shutil.move(extracted, target)
    return target


def unseal_sidecars(source: str, seven_zip: str, keep_dir: str, private_key=None,
                    cms_backend=None, refresh: bool = False) -> list[str]:
    """Unseal every Outcome, Outline, log and console output sealed beside the collection.

    Each lands next to the file it came from, under the sealed file's own name with only the
    extension dropped ('ORC_Outcome.json.7z.p7b' -> 'ORC_Outcome.json'). Doing this first means
    everything downstream reads plain files and never has to care whether the collection was
    encrypted; an already-unsealed file is reused rather than decrypted again.
    """
    unsealed = []
    for sealed in sealed_sidecars(collection_dir(source)):
        target = layout.unsealed_path(sealed, keep_dir)
        if not refresh and os.path.exists(target):
            unsealed.append(target)
            continue

        try:
            unsealed.append(unseal_one(sealed, target, seven_zip, private_key, cms_backend))
            print(f"unsealed '{os.path.basename(target)}'")
        except (RuntimeError, OSError) as error:
            # Worth saying out loud: the rest of the run still ingests, but a view built on
            # this file will be empty.
            print(f"could not unseal '{os.path.basename(sealed)}': {error}")

    return unsealed


def find_outlines(directories: list[str]) -> list[str]:
    """Plain Outline JSONs beside the collection. ORC writes one per run attempt."""
    found = []
    for directory in directories:
        try:
            entries = sorted(os.listdir(directory))
        except OSError:
            continue

        found += [os.path.join(directory, entry) for entry in entries
                  if entry.lower().endswith("outline.json")]
    return found


def copy_into_run(paths: list[str], out_dir: str) -> list[str]:
    """Copy a collection's own files into the run under the names ORC gave them, and say which
    names were written so a later rebuild can remove exactly those."""
    written = []
    for path in paths:
        os.makedirs(out_dir, exist_ok=True)
        name = os.path.basename(path)
        target = os.path.join(out_dir, name)
        # 'orca extract --out' may point at the collection itself, and then the file already
        # is where it belongs; shutil.copy refuses a copy onto itself.
        if not os.path.exists(target) or not os.path.samefile(path, target):
            shutil.copy(path, target)
        written.append(name)
    return written


def capture_outlines(directories: list[str], out_dir: str) -> list[str]:
    """Copy every Outline of a collection into the run.

    Kept as files rather than parsed here: 'orca outline' turns them into a queryable table,
    and keeping the documents means a later ORC version can add fields without a re-ingest.
    """
    return copy_into_run(find_outlines(directories), out_dir)


def decrypt_archive(path: str, dest_dir: str, private_key: str | None = None,
                    cms_backend: str | None = None, refresh: bool = False) -> str:
    """Return a path to an openable '.7z' for 'path', decrypting into 'dest_dir' if needed.

    An already-plain archive is returned untouched. A '.p7b' is CMS-decrypted and then, when
    ORC journaled the stream (the default - '/no_journaling' does not), replayed into the
    archive it describes. A bare '.7zs' journal is replayed without any decryption.

    The plain archive is kept rather than thrown away: decrypting and replaying a multi-GB
    collection is the expensive part of an ingest, and the next run reuses it. The journal
    intermediate is not - it is superseded by the archive it replays into.

    Kept separate from resolve_archives() so discovery stays free of side effects on disk.
    """
    journaled = journal.is_journaled(path)
    if not journaled and os.path.splitext(path)[1].lower() != ".p7b":
        return path

    # ORC names an encrypted archive '<name>.7z.p7b', so the plain name is the sealed one with
    # only the extension changed - never 'SystemInfo.7z.7z'.
    plain = layout.decrypted_path(path, dest_dir)
    if not refresh and os.path.exists(plain) and sevenzip.is_archive(plain):
        print(f"reusing '{os.path.basename(plain)}'")
        return plain

    os.makedirs(dest_dir, exist_ok=True)
    if journaled:
        journal.replay(path, plain)
        return plain

    # The journal is a GB-scale intermediate: scratch, removed when the process ends.
    decrypted = os.path.join(scratch.directory("cms_"), layout.archive_stem(path) + ".7zs")
    backend = cms.decrypt(path, decrypted, private_key, cms_backend)

    if not journal.is_journaled(decrypted):
        # No JRNL header. Two unrelated things look identical here, so name which one it is.
        # '/no_journaling' puts the archive straight into the CMS content and is normal; a
        # wrong key also lands here, because CMS over AES-CBC carries no integrity check and
        # OpenSSL answers a bad RSA padding with a random session key rather than an error -
        # so cms.decrypt can return garbage and call it a success. Only the content tells them
        # apart. Either way the caller still gets a path: 7z reports the real damage, and an
        # archive is not refused just because this heuristic did not recognize it.
        shutil.move(decrypted, plain)
        name = os.path.basename(path)
        if sevenzip.is_archive(plain):
            print(f"decrypted '{name}' with the {backend} backend (no_journaling)")
        else:
            print(f"warning: '{name}' decrypted with the {backend} backend to neither a JRNL "
                  f"journal nor a 7z archive - most likely the wrong private key; continuing")
        return plain

    journal.replay(decrypted, plain)
    os.remove(decrypted)
    print(f"decrypted '{os.path.basename(path)}' with the {backend} backend")
    return plain


def ingest_one(job: CsvJob, engine, out_dir: str, drop_columns, make_csv: bool,
               make_trunc: bool, seven_zip: str, password) -> list[TimingRecord]:
    records: list[TimingRecord] = []
    work = scratch.directory("csv_")
    try:
        src = sevenzip.extract_entry(seven_zip, job.source_archive, job.entry_path, work, password)

        os.makedirs(out_dir, exist_ok=True)
        parquet = os.path.join(out_dir, job.output_name + ".parquet")

        t0 = time.perf_counter()
        engine.csv_to_parquet(src, parquet, None)
        rows = engine.count(parquet)
        records.append(TimingRecord(job.output_name, "parquet", rows,
                                    os.path.getsize(parquet), time.perf_counter() - t0))

        if make_csv:
            csv_dir = os.path.join(out_dir, CSV_DIR)
            os.makedirs(csv_dir, exist_ok=True)

            full = os.path.join(csv_dir, job.output_name + ".csv")
            t0 = time.perf_counter()
            engine.parquet_to_csv(parquet, full, None)
            records.append(TimingRecord(job.output_name, "csv", rows,
                                        os.path.getsize(full), time.perf_counter() - t0))

            if make_trunc:
                trunc_dir = os.path.join(out_dir, CSV_TRUNC_DIR)
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
    path = layout.name_map_path(out_dir)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", newline="", encoding="utf-8") as f:
        w = _csv.writer(f)
        w.writerow(["name", "source_archive", "entry_path", "source_mtime"])
        for j in jobs:
            w.writerow([j.output_name, j.source_archive, j.entry_path, j.source_mtime])


def _write_products(names: list[str], out_dir: str) -> None:
    """Record what this ingest put in the run directory, one name per line."""
    path = layout.products_path(out_dir)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(sorted(set(names))) + "\n")


def _write_manifest(rows: list[dict], out_dir: str, engine) -> None:
    """Persist the every-leaf manifest as manifest.parquet (via a scratch CSV, so the columns
    land as text like every other ingested table). Enables whole-run file-set + CRC diffs."""
    if not rows:
        return

    import csv as _csv
    csv_path = os.path.join(scratch.directory("manifest_"), "manifest.csv")
    with open(csv_path, "w", newline="", encoding="utf-8") as f:
        w = _csv.writer(f)
        w.writerow(["relpath", "size", "crc", "isCsv"])
        for r in rows:
            w.writerow([r["relpath"], r["size"], r["crc"], r["isCsv"]])

    manifest = layout.manifest_path(out_dir)
    os.makedirs(os.path.dirname(manifest), exist_ok=True)
    engine.csv_to_parquet(csv_path, manifest)
    os.remove(csv_path)


def run_ingest(archive: str, out_dir: str | None = None, *, jobs=None, make_csv=True,
               make_trunc=True, exclude_cols=None, password=None, private_key=None,
               cms_backend=None, engine_name="duckdb", work_dir=None, keep_dir=None,
               refresh=False) -> list[TimingRecord]:
    """Ingest a collection into a run directory.

    'out_dir' and 'keep_dir' default to the collection's own directory (see layout.resolve):
    the run lands beside the archive it came from, and so do the decrypted archives and
    unsealed sidecars. '--work-dir' moves both somewhere else.
    """
    # The worker count decides the engine's memory budget: every job runs its own duckdb, and
    # a table large enough to matter is large enough for six of them to exhaust the machine.
    workers = jobs or os.cpu_count() or 4
    engine = get_engine(engine_name, worker_memory_limit(workers))
    seven = sevenzip.find_7z()
    if not seven:
        raise RuntimeError("7z.exe not found")
    drop = list(exclude_cols) if exclude_cols else list(DEFAULT_VOLATILE_COLUMNS)

    if out_dir is None or keep_dir is None:
        resolved_keep, resolved_out = layout.resolve(archive, work_dir)
        keep_dir = keep_dir or resolved_keep
        out_dir = out_dir or resolved_out
    os.makedirs(out_dir, exist_ok=True)

    # Nested containers are extracted once into staging and must outlive ingestion
    # (each job extracts its CSV from its container), so staging is cleaned only at the end.
    staging = scratch.directory("stage_")
    try:
        # First, so everything below reads plain files whether or not the collection was
        # encrypted - resolve_archives included, which reads the Outcome to name the archives.
        unseal_sidecars(archive, seven, keep_dir, private_key, cms_backend, refresh)

        archives = [decrypt_archive(top, keep_dir, private_key, cms_backend, refresh)
                    for top in resolve_archives(archive)]

        directories = sidecar_dirs(archive, keep_dir)
        outcome_path = next((found for found in map(find_outcome_json, directories) if found),
                            None)

        # Everything this ingest writes into the run, by name: a run shares its directory with
        # the collection's own files and with an expanded tree, so a rebuild has to know what
        # it may remove (see cache.clear_run).
        products = [layout.ORCA_DIR]
        products += copy_into_run([outcome_path] if outcome_path else [], out_dir)
        products += capture_outlines(directories, out_dir)
        products += capture_sidecar_logs(directories, out_dir)
        products += capture_logs(seven, archives, out_dir, password)

        work_jobs, manifest_rows = discover_jobs(archives, staging, seven, password)
        products += [job.output_name + ".parquet" for job in work_jobs]
        if make_csv:
            products.append(CSV_DIR)
            if make_trunc:
                products.append(CSV_TRUNC_DIR)
        _write_products(products, out_dir)

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

    write_timing_csv(records, layout.timing_path(out_dir))
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


def capture_sidecar_logs(directories: list[str], out_dir: str) -> list[str]:
    """Copy the collection-level logs into the run.

    WolfLauncher's own log and console output are not in any archive: ORC writes them beside
    the archives and, when encrypted, seals them there - so they only exist as plain files once
    unseal_sidecars has run.
    """
    found = []
    for directory in directories:
        try:
            entries = sorted(os.listdir(directory))
        except OSError:
            continue

        found += [os.path.join(directory, entry) for entry in entries
                  if entry.lower().endswith(".log")]
    return copy_into_run(found, out_dir)


def capture_logs(seven_zip: str, archives: list, out_dir: str, password) -> list[str]:
    """Extract the top-level *.log entries and any 'Errors' sub-archive of each archive.

    They land in the run beside the tables, under the names they carry in the archive - the
    same place and names 'orca extract' would give them.
    """
    written = []
    for top in archives:
        for entry in sevenzip.list_entries(seven_zip, top, password):
            low = entry.path.lower()
            if low.endswith(".log"):
                os.makedirs(out_dir, exist_ok=True)
                try:
                    sevenzip.extract_entry(seven_zip, top, entry.path, out_dir, password)
                    written.append(entry.path)
                except Exception:
                    pass
            elif low.endswith(".7z") and "error" in low:
                stage = scratch.directory("errors_")
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
                    dest = os.path.join(out_dir, ERRORS_DIR)
                    os.makedirs(dest, exist_ok=True)
                    sevenzip.extract_all(seven_zip, container, dest, pw)
                    written.append(ERRORS_DIR)
                except Exception:
                    pass
                finally:
                    shutil.rmtree(stage, ignore_errors=True)
    return written
