"""Ingest orchestration: discover top-level CSVs, build per-table parquet + CSVs, timed."""
import json
import os
import shutil
import time
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from typing import NamedTuple

from . import cms, csvout, encoding, journal, layout, naming, scratch, sevenzip
from .columns import DEFAULT_VOLATILE_COLUMNS, IDENTITY_COLUMNS
from .engine import get_engine, worker_memory_limit
from .timing import TimingRecord, write_timing_csv, summarize


@dataclass
class CsvJob:
    entry_path: str
    source_archive: str
    output_name: str
    password: str | None = None
    source_mtime: str = ""
    archive: str = ""        # the top archive's stem: its directory in the dataset


def _jobs_from_logical(logical: list[tuple]) -> list[CsvJob]:
    # logical item = (logical_path, source_archive, entry_path, password, modified[, top]).
    names = naming.assign_names([item[0] for item in logical])
    return [CsvJob(entry_path=item[2], source_archive=item[1], output_name=name,
                   password=item[3], source_mtime=item[4],
                   archive=layout.archive_stem(item[5]) if len(item) > 5 else "")
            for item, name in zip(logical, names)]


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
        # One staging subdirectory per top archive. Two archives of one collection can hold
        # containers of the same name - an Outcome naming two archives, plus a stray archive
        # from an earlier run of the same host - and a flat staging directory let the second
        # overwrite the first, silently, before any job read it. Only the place on disk is
        # namespaced: the logical paths and manifest relpaths stay as they are, because diff
        # joins two runs' manifests on relpath and the archive stem carries the run's stamp.
        top_stage = os.path.join(staging_dir, layout.archive_stem(top))
        os.makedirs(top_stage, exist_ok=True)

        for entry in sevenzip.list_entries(seven_zip, top, password_override):
            low = entry.path.lower()
            if low.endswith(".7z"):
                # password_override is the TOP archive's password here: it is what listing 'top'
                # above used, and pulling an entry out of it needs the same one. The nested
                # container's own password is resolved separately, just below.
                container = sevenzip.extract_entry(seven_zip, top, entry.path, top_stage,
                                                   password_override)
                if not os.path.exists(container):
                    continue
                stem = os.path.splitext(entry.path)[0]
                pw = password_override
                if not pw:
                    cfg_rel = stem + "_cli_config.xml"
                    try:
                        sevenzip.extract_entry(seven_zip, top, cfg_rel, top_stage,
                                               password_override)
                        pw = sevenzip.password_from_cli_config(os.path.join(top_stage, cfg_rel))
                    except Exception:
                        pw = None
                for nested in sevenzip.list_entries(seven_zip, container, pw):
                    relpath = f"{stem}/{nested.path}"
                    manifest.append(_manifest_row(relpath, nested))
                    if nested.path.lower().endswith(".csv"):
                        logical.append((relpath, container, nested.path, pw, nested.modified,
                                        top))
            else:
                manifest.append(_manifest_row(entry.path, entry))
                if low.endswith(".csv"):
                    logical.append((entry.path, top, entry.path, password_override, entry.modified,
                                    top))
    return _jobs_from_logical(logical), manifest


def archive_as_found(directory: str, name: str) -> str:
    """The archive an Outcome names, as it actually is on disk.

    ORC records the name it wrote, '<name>.7z.p7b'. A collection can reach us with that already
    decrypted and the envelope gone - 'orca extract --depth 1' produces exactly that - and the
    Outcome still names the sealed one. Fall back to the decrypted name rather than insisting
    on a file nobody kept; when neither is there, name the one the Outcome asked for, because
    that is the thing that is missing.
    """
    sealed = os.path.join(directory, name)
    if os.path.exists(sealed):
        return sealed

    plain = os.path.join(directory, layout.archive_stem(name) + ".7z")
    return plain if os.path.exists(plain) else sealed


def loose_archives(directory: str) -> list[str]:
    """Every result archive sitting in 'directory', by what is on disk rather than by name in
    an Outcome, the way Expand-OrcArchive accepted a plain folder of results.

    A sealed output ('outcome.json.7z.p7b') is not an archive, and 'X.7z' beside 'X.7z.p7b' is
    the decrypted copy orca kept, not a second collection: taking both would read it twice.
    """
    entries = os.listdir(directory)
    sealed = {entry.lower() for entry in entries if entry.lower().endswith(SEALED_SUFFIX)}
    return sorted(os.path.join(directory, entry) for entry in entries
                  if entry.lower().endswith((".7z", ".p7b")) and not sealed_output(entry)
                  and entry.lower() + ".p7b" not in sealed)


def archives_not_named(directory: str, named: list[str]) -> list[str]:
    """The archives of 'directory' that none of 'named' accounts for.

    Matched by archive stem, so the decrypted copy of a named archive is not taken for a new
    one just because the Outcome names the sealed spelling. Pure: 'orca report' asks the same
    question to say which archives nobody's Outcome accounts for, and must not print as it
    goes.
    """
    stems = {layout.archive_stem(path).lower() for path in named}
    return [path for path in loose_archives(directory)
            if layout.archive_stem(path).lower() not in stems]


def unnamed_archives(directory: str, named: list[str]) -> list[str]:
    """The archives of 'directory' no Outcome named, each reported as it is picked up.

    One directory can hold several ORC runs, each with its own Outcome, and an ingest reads
    one - nothing says which run a caller meant. The other runs' archives are collected data
    sitting right there, so they are opened too rather than dropped without a word. An archive
    ORC wrote after the Outcome it belongs to, or one dropped in by hand, lands here as well.
    """
    found = archives_not_named(directory, named)
    for path in found:
        print(f"'{os.path.basename(path)}' is not in the Outcome that was read; "
              "opening it anyway")
    return found


def resolve_archives(path: str, outcome_dir: str | None = None) -> list[str]:
    """The archives of the collection at 'path', named by its Outcome where there is one, plus
    any other archive of the directory that no Outcome named.

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
                    archives.append(archive_as_found(path, name))
        if archives:
            return archives + unnamed_archives(path, archives)

    # No usable Outcome at all: everything the directory holds is unnamed, and there is nothing
    # to warn about - a folder of loose results is a supported input, not a surprise.
    loose = loose_archives(path)
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

    Each lands in 'keep_dir' - the current directory, or next to the file it came from with
    '--out-at-input' - under the sealed file's own name with only the extension dropped ('ORC_Outcome.json.7z.p7b' -> 'ORC_Outcome.json'). Doing this first means
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


def find_outlines(directories: list[str], run: str | None = None) -> list[str]:
    """Plain Outline JSONs beside the collection. ORC writes one per run attempt.

    'run' narrows them to one run's - 'ORC_<set>_<host>_<stamp>_Outline.json' starts with the
    run name - for a directory that holds several hosts' sidecars side by side.
    """
    found = []
    for directory in directories:
        try:
            entries = sorted(os.listdir(directory))
        except OSError:
            continue

        found += [os.path.join(directory, entry) for entry in entries
                  if entry.lower().endswith("outline.json")
                  and (run is None or entry.lower().startswith(run.lower()))]
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


def capture_outlines(directories: list[str], out_dir: str, run: str | None = None) -> list[str]:
    """Copy every Outline of a collection - of one run, when 'run' names it - into the run.

    Kept as files rather than parsed here: 'orca outline' turns them into a queryable table,
    and keeping the documents means a later ORC version can add fields without a re-ingest.
    """
    return copy_into_run(find_outlines(directories, run), out_dir)


def has_encryption_layer(path: str) -> bool:
    """True when 'path' is wrapped in a layer of its own - a CMS envelope ('.p7b') or a bare
    journal - rather than being an openable archive already.

    An extract counts that wrapping as one level of depth like any other, so it has to be able
    to ask the question without decrypting to find out.
    """
    return journal.is_journaled(path) or os.path.splitext(path)[1].lower() == ".p7b"


def decrypted_copy_is_current(plain: str, sealed: str) -> bool:
    """True when 'plain' is an archive decrypted from 'sealed' as it is now: one, and not older.

    The name alone said nothing about which collection it came from: a host collected again at
    the same path brings a new '.p7b' under the old name, and its old decrypted copy would be
    ingested in its place. Not older than the sealed file is the cheap answer - a copy made
    from this '.p7b' is written after it - and '--refresh' is the escape hatch when a copy of
    the collection did not keep its times.
    """
    if not sevenzip.is_archive(plain):
        return False
    try:
        return os.path.getmtime(plain) >= os.path.getmtime(sealed)
    except OSError:
        return False


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
    if not has_encryption_layer(path):
        return path

    journaled = journal.is_journaled(path)

    # ORC names an encrypted archive '<name>.7z.p7b', so the plain name is the sealed one with
    # only the extension changed - never 'SystemInfo.7z.7z'.
    plain = layout.decrypted_path(path, dest_dir)
    if not refresh and decrypted_copy_is_current(plain, path):
        print(f"reusing '{os.path.basename(plain)}'")
        return plain
    if os.path.exists(plain) and not refresh:
        print(f"'{os.path.basename(plain)}' is older than '{os.path.basename(path)}'; "
              "decrypting again")

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


def csv_to_parquet_any_encoding(engine, src: str, parquet: str, extra_columns,
                                codepage: str | None) -> None:
    """Convert one collected CSV whatever it was written in (see encoding.py).

    A UTF-16 file is rewritten before duckdb sees it, and PowerShell's '#TYPE' line dropped
    (encoding.py says why). The file is then tried as UTF-8, and re-read through the host's
    console codepage only when duckdb refuses it as not UTF-8. A file whose rows are shorter
    than its header is read once more with the short rows padded (see collapsed_to_one_column).
    A file that is not a CSV at all - a tool's error message saved under '.csv' - is reported
    by what it holds instead of duckdb's dump of the dialects it tried.
    """
    bom = encoding.bom_encoding(src)
    if bom:
        src = encoding.transcode(src, encoding.derived_path(src, "utf8"), bom)

    if encoding.starts_with_type_line(src):
        src = encoding.drop_first_line(src, encoding.derived_path(src, "untyped"))

    read = convert_any_encoding(engine, src, parquet, extra_columns, codepage, bool(bom))
    if collapsed_to_one_column(engine, read, parquet, extra_columns):
        engine.csv_to_parquet(read, parquet, None, extra_columns, null_padding=True)


def convert_any_encoding(engine, src: str, parquet: str, extra_columns, codepage: str | None,
                         transcoded: bool) -> str:
    """csv_to_parquet, retried through the host's codepage when duckdb says the file is not
    UTF-8. Returns the file that was read: 'src', or its UTF-8 rewrite."""
    try:
        engine.csv_to_parquet(src, parquet, None, extra_columns)
        return src
    except RuntimeError as error:
        message = str(error)
        if encoding.NOT_A_CSV in message:
            raise RuntimeError(f"not a CSV, it starts with: "
                               f"'{encoding.first_line(src)}'") from error

        # Already transcoded from UTF-16, or refused for another reason: no codepage helps.
        if transcoded or encoding.INVALID_UNICODE not in message:
            raise

    codec = codepage
    if not codec:
        codec = encoding.FALLBACK_ENCODING
        print(f"warning: '{os.path.basename(src)}' is not UTF-8 and the Outline gives no "
              f"usable codepage; reading it as {codec}, characters above 0x7F may be wrong")

    utf8 = encoding.transcode(src, encoding.derived_path(src, "utf8"), codec)
    engine.csv_to_parquet(utf8, parquet, None, extra_columns)
    return utf8


def collapsed_to_one_column(engine, csv_path: str, parquet: str, extra_columns) -> bool:
    """True when a comma-separated header came out as a single column.

    No error says so: when some rows are shorter than the header (autorunsc omits the last
    field of some rows), duckdb's sniffer rejects ',' and settles on a delimiter the file never
    uses, so every line becomes one value under a column named after the whole header line. A
    table that is one column by design has no comma in its header, so it is never re-read.
    """
    if "," not in encoding.first_line(csv_path):
        return False

    stamped = {name.lower() for name in (extra_columns or {})}
    collected = [c for c in engine.columns(parquet) if c.lower() not in stamped]
    return len(collected) == 1


def ingest_one(job: CsvJob, engine, out_dir: str, seven_zip: str, password,
               extra_columns=None, codepage: str | None = None) -> list[TimingRecord]:
    records: list[TimingRecord] = []
    work = scratch.directory("csv_")
    try:
        src = sevenzip.extract_entry(seven_zip, job.source_archive, job.entry_path, work, password)

        os.makedirs(out_dir, exist_ok=True)
        parquet = os.path.join(out_dir, job.output_name + ".parquet")

        t0 = time.perf_counter()
        csv_to_parquet_any_encoding(engine, src, parquet, extra_columns, codepage)
        rows = engine.count(parquet)
        records.append(TimingRecord(job.output_name, "parquet", rows,
                                    os.path.getsize(parquet), time.perf_counter() - t0))

        return records
    finally:
        shutil.rmtree(work, ignore_errors=True)


def _write_name_map(jobs: list[CsvJob], out_dir: str) -> None:
    import csv as _csv
    path = layout.name_map_path(out_dir)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", newline="", encoding="utf-8") as f:
        w = _csv.writer(f)
        w.writerow(["name", "source_archive", "entry_path", "source_mtime", "archive"])
        for j in jobs:
            w.writerow([j.output_name, j.source_archive, j.entry_path, j.source_mtime,
                        j.archive])


def _write_products(names: list[str], out_dir: str) -> None:
    """Record what this ingest put in the run directory, one name per line."""
    layout.write_products(out_dir, names)


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


def run_ingest(archive: str, out_dir: str | None = None, *, jobs=None, make_csv=False,
               make_trunc=False, exclude_cols=None, password=None, private_key=None,
               cms_backend=None, engine_name="duckdb", work_dir=None, keep_dir=None,
               refresh=False, strict=False, collection=None,
               collection_name=None, at_input=False, cache_key=None) -> list[TimingRecord]:
    """Ingest a collection into a run directory, unless the one there is already current.

    'out_dir' and 'keep_dir' default to the current directory (see layout.resolve): the run,
    the decrypted archives and the unsealed sidecars land there. 'at_input' puts them beside
    the collection instead; '--work-dir' moves them, and the scratch with them.

    'collection' is the one Collection to ingest when the caller (fleet, cache) already knows
    it; 'collection_name' is '--collection', resolved here rather than by the caller because
    a sealed directory's Outcomes are only readable once this function has unsealed them.

    'make_csv' / 'make_trunc' write csv\\ / csv-trunc\\ after the parquet (csvout), whether the
    tables were just built or already there; 'archive' may then be a dataset, and the
    collection is not needed. 'cache_key' is the caller's key for the run (cache.ensure_dataset),
    so that both agree on what current means.
    """
    # Here rather than at the top: cache imports this module.
    from . import cache

    if cache.is_dataset(archive):
        csv_over(archive, engine_name, jobs, make_csv, make_trunc, exclude_cols)
        return []
    # Before anything takes a scratch directory, and not only where the products go: the CMS
    # intermediate is as big as the collection, so a machine whose temp volume is smaller than
    # that cannot ingest it at all unless '--work-dir' moves the scratch as well. The failure
    # if it does not is a decryption error that names no disk, so it is not one to debug twice.
    scratch.use(work_dir)

    # The worker count decides the engine's memory budget: every job runs its own duckdb, and
    # a table large enough to matter is large enough for six of them to exhaust the machine.
    workers = jobs or os.cpu_count() or 4
    engine = get_engine(engine_name, worker_memory_limit(workers))
    seven = sevenzip.find_7z()
    if not seven:
        raise RuntimeError("7z.exe not found")

    # keep_dir first and on its own: unsealing needs it, and which collection this is - and
    # so what the dataset is called - is only known once the sidecars are plain.
    if keep_dir is None or out_dir is None:
        resolved_keep, resolved_out = layout.resolve(archive, work_dir, at_input=at_input)
        keep_dir = keep_dir or resolved_keep
    else:
        resolved_out = out_dir

    # Nested containers are extracted once into staging and must outlive ingestion
    # (each job extracts its CSV from its container), so staging is cleaned only at the end.
    staging = scratch.directory("stage_")
    try:
        # First, so everything below reads plain files whether or not the collection was
        # encrypted - collection_of included, which reads the Outcome to name the archives. A
        # plain copy already in a dataset under keep_dir (or in the one --out names) is used
        # without decrypting (sidecars.resolve).
        if collection is None:
            from . import sidecars
            resolved = sidecars.resolve(collection_dir(archive), keep_dir, seven, private_key,
                                        cms_backend, refresh,
                                        places=[out_dir] if out_dir else [])
            collection = collection_of(archive, resolved.paths, collection_name)
        run = collection.name or None

        # A directory source is named after the run it holds. With several in it that has to
        # be this collection's run, not the directory's first sidecar (layout.run_name).
        if out_dir is None:
            out_dir = resolved_out
            if run and os.path.isdir(archive):
                out_dir = os.path.join(os.path.dirname(resolved_out), run)
        os.makedirs(out_dir, exist_ok=True)

        # Before decrypting: a current run costs nothing to ask for again, which is what makes
        # '--csv' on a collection already ingested a conversion and not a second ingest.
        options = cache.ingest_options(password, engine_name, strict)
        key = cache_key or cache.key_for(archive, options, archives=collection.archives)
        if not refresh and cache.is_current(out_dir, key):
            print(f"'{out_dir}' is current; not ingesting again (--refresh to force)")
            csv_over(out_dir, engine_name, jobs, make_csv, make_trunc, exclude_cols)
            return []

        archives, unread, undecrypted = plain_archives(collection.archives, out_dir,
                                                       private_key, cms_backend, refresh)

        # Everything this ingest writes into the run, by name: a run shares its directory with
        # the collection's own files and with an expanded tree, so a rebuild has to know what
        # it may remove (see cache.clear_dataset). The Outcome, Outlines and logs are this
        # collection's own: a directory of several hosts holds everyone's side by side.
        products = [layout.ORCA_DIR]
        products += copy_into_run([collection.outcome_path] if collection.outcome_path else [],
                                  out_dir)
        outlines = copy_into_run(list(collection.outlines), out_dir)
        products += outlines
        products += copy_into_run(list(collection.sidecars), out_dir)
        products += capture_logs(seven, archives, out_dir, password)

        work_jobs, manifest_rows = discover_jobs(archives, staging, seven, password)
        products += [job.output_name + ".parquet" for job in work_jobs]
        _write_products(products, out_dir)

        _write_name_map(work_jobs, out_dir)
        _write_manifest(manifest_rows, out_dir, engine)

        identity = identity_columns(collection)
        codepage = encoding.host_codepage([os.path.join(out_dir, name) for name in outlines])
        with ThreadPoolExecutor(max_workers=workers) as pool:
            futures = [pool.submit(ingest_one, j, engine, out_dir, seven, j.password,
                                   identity, codepage)
                       for j in work_jobs]
            records, skipped = collect_results(futures, work_jobs, strict)
    finally:
        shutil.rmtree(staging, ignore_errors=True)

    # An archive that is missing, or would not decrypt, is a hole in the run as a table orca
    # could not read is: recorded the same way, and said by the report the same way.
    skipped = unread + skipped

    write_skipped(skipped, out_dir)
    report_skipped(skipped, out_dir)

    write_timing_csv(records, layout.timing_path(out_dir))
    print(summarize(records))
    csv_over(out_dir, engine_name, jobs, make_csv, make_trunc, exclude_cols)

    # Last, so that its absence marks an interrupted ingest (cache.is_current). Not at all when
    # an archive would not decrypt: a missing key is fixed by giving it, and the next run has to
    # try again rather than find this run current.
    if undecrypted:
        print(f"'{out_dir}' is not marked current: {len(undecrypted)} archive(s) could not be "
              f"decrypted ({', '.join(undecrypted)})")
    else:
        cache.write_marker(out_dir, archive, key, options)
    return records


def plain_archives(tops: list, out_dir: str, private_key=None, cms_backend=None,
                   refresh: bool = False) -> tuple[list, list, list]:
    """(openable archives, [(name, why)] left out, names that would not decrypt).

    The run is built from the archives that are there: one missing, or one that will not
    decrypt, costs itself and is said, as an unreadable table does. A decrypted '.7z' goes
    into the archive's own directory in the run and is kept for the next ingest; a plain copy
    already beside the sealed one in the collection is used as it is.
    """
    archives, unread, undecrypted = [], [], []
    for top in tops:
        name = os.path.basename(top)
        if not os.path.exists(top):
            print(f"'{name}' is named by the collection and is not there")
            unread.append((name, "archive missing"))
            continue

        beside = os.path.join(os.path.dirname(top), layout.archive_stem(top) + ".7z")
        if not refresh and top != beside and decrypted_copy_is_current(beside, top):
            archives.append(beside)
            continue

        try:
            archives.append(decrypt_archive(top, os.path.join(out_dir, layout.archive_stem(top)),
                                            private_key, cms_backend, refresh))
        except (RuntimeError, OSError) as error:
            print(f"could not decrypt '{name}': {error}")
            unread.append((name, f"could not decrypt: {error}"))
            undecrypted.append(name)

    return archives, unread, undecrypted


def csv_over(dataset: str, engine_name: str, jobs, make_csv: bool, make_trunc: bool,
             exclude_cols=None) -> None:
    """The CSVs asked for, over the tables 'dataset' holds (csvout). Nothing when none was."""
    if not (make_csv or make_trunc):
        return

    workers = jobs or os.cpu_count() or 4
    drop = list(exclude_cols) if exclude_cols else list(DEFAULT_VOLATILE_COLUMNS)
    csvout.write_csv(get_engine(engine_name, worker_memory_limit(workers)), dataset,
                     make_csv, make_trunc, drop, workers)


def collect_results(futures, jobs: list, strict: bool = False):
    """Gather every job's timings, and the tables that could not be produced.

    A failed job costs its own table and nothing else, and says so: the reason is printed here
    and recorded in '.orca/skipped.txt', which is the only thing that says the dataset is
    incomplete. That is the default because it is what you want nearly every time - one
    malformed collected file should not cost the other 26 tables, and having to say so with a
    flag means saying it every time. 'strict' is the other answer: the first failure aborts the
    ingest, for a collection you are about to rely on.

    The narrow catch is deliberate: a duckdb refusal and a 7-Zip extraction failure both arrive
    as RuntimeError, and a bug in orca should still stop the run rather than be filed as one
    more unreadable table.
    """
    records: list[TimingRecord] = []
    skipped: list[tuple[str, str]] = []
    for job, future in zip(jobs, futures):
        try:
            records.extend(future.result())
        except (RuntimeError, OSError) as error:
            if strict:
                raise

            # One line: a duckdb error carries the whole failing statement.
            reason = " ".join(str(error).split())[:300]
            print(f"skipped '{job.output_name}': {reason}")
            skipped.append((job.output_name, reason))

    return records, skipped


def report_skipped(skipped: list, out_dir: str) -> None:
    """Name the tables an ingest could not read, at the end, after the timings.

    Skipping one is the default, so saying so is not optional: the dataset it produces is
    incomplete and nothing else on disk says it. Each failure is printed as it happens, but
    those lines scroll away behind the tables that worked, so the names are repeated here where
    the eye lands - a count alone only sends you looking for the file.
    """
    if not skipped:
        return

    names = ", ".join(name for name, _reason in skipped)
    print(f"{len(skipped)} table(s) skipped, recorded in "
          f"{os.path.join(layout.ORCA_DIR, 'skipped.txt')}: {names}")


def write_skipped(skipped: list, out_dir: str) -> None:
    """Record the tables an ingest could not produce, or remove the record when there are none.

    A run missing a table does not say so anywhere else. Diffed against a complete run it shows
    up as a removed table, which reads as the collection having stopped producing it rather
    than orca having failed to read it, and '--one-line' does not mention it at all. Rewritten
    on every ingest, so it never describes an older one.
    """
    path = layout.skipped_path(out_dir)
    if not skipped:
        if os.path.exists(path):
            os.remove(path)
        return

    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        for name, reason in skipped:
            f.write(f"{name}\t{reason}\n")


def _outcome_object(path: str) -> dict | None:
    """The 'dfir-orc.outcome' object of a JSON file, or None when it is not an Outcome.

    Read here rather than imported from outcome.py: that module imports this one back, and
    discovery must not depend on the reporting layer.
    """
    try:
        with open(path, encoding="utf-8") as f:
            data = json.load(f)
    except (ValueError, OSError):
        return None

    outcome = (data.get("dfir-orc") or {}).get("outcome")
    return outcome if isinstance(outcome, dict) else None


def find_outcome_jsons(directory: str) -> list[str]:
    """Every ORC Outcome JSON in 'directory', by content rather than by name.

    A directory can hold several: a fleet is often dumped into one directory, one machine's
    files beside the next, and then each Outcome is a collection of its own.
    """
    try:
        entries = sorted(os.listdir(directory))
    except OSError:
        return []

    return [os.path.join(directory, entry) for entry in entries
            if entry.lower().endswith(".json") and _outcome_object(os.path.join(directory, entry))]


def outcome_identity(outcome_path: str | None) -> tuple[str, str, str]:
    """(computer_name, run id, timestamp) of an Outcome; three empty strings for none."""
    outcome = _outcome_object(outcome_path) if outcome_path else None
    if not outcome:
        return "", "", ""

    return (outcome.get("computer_name") or "", outcome.get("id") or "",
            outcome.get("timestamp") or "")


class Collection(NamedTuple):
    """One ORC run in a directory: its Outcome, its archives, and who it came from.

    A directory often holds several - a fleet uploads one machine's files beside the next -
    and each is a run of its own. 'outcome_path' is None for archives no Outcome named, which
    are collected data all the same and ingest as an unnamed collection.
    """
    outcome_path: str | None
    archives: list[str]
    computer_name: str
    run_id: str
    timestamp: str
    outlines: tuple = ()     # the run's Outlines: the one its Outcome names, and those of its id
    sidecars: tuple = ()     # the log and console output its Outcome names
    name: str = ""           # the dataset's name: the run's, from its Outcome (sidecars.run_name)


def collections_in(directory: str, extra_dirs=()) -> list[Collection]:
    """Every ORC run in 'directory' (sidecars.group), from the plain sidecars of 'directory'
    and of 'extra_dirs': one per Outcome, one per Outline whose run has no Outcome, and one for
    the archives nothing names.

    This is what separates a fleet directory from a single collection. A sealed collection's
    sidecars are invisible here until they are plain: sidecars.resolve does that, and callers
    holding its result go to sidecars.group directly.
    """
    from . import sidecars
    paths = []
    for place in [directory, *extra_dirs]:
        paths += sidecars.plain_sidecars(place)
    return sidecars.group(directory, sidecars.Resolved(paths, [])).collections




def identity_columns(collection: Collection) -> dict[str, str]:
    """The identity to stamp into every table of 'collection'.

    A collection with no Outcome has no identity to claim, and gets the columns with empty
    values rather than not at all: a fleet query reading a glob must find the column on every
    file or the union fails.
    """
    return {"OrcComputerName": collection.computer_name,
            "OrcRunId": collection.run_id,
            "OrcTimestamp": collection.timestamp}


def select_collection(found: list[Collection], wanted: str) -> Collection:
    """The one collection of 'found' that 'wanted' names.

    Matched on the Outcome's computer_name first, the run name second (what the dataset
    directory is called, so what an operator sees on disk), and as a prefix of the Outcome's
    filename last. An ambiguous name is refused rather than resolved arbitrarily: a lab
    collects the same host twice often enough that picking one silently would ingest the
    wrong run.
    """
    needle = wanted.lower()
    named = [c for c in found if c.outcome_path]
    matches = [c for c in named if c.computer_name.lower() == needle]
    if not matches:
        matches = [c for c in named if c.name.lower() == needle]
    if not matches:
        matches = [c for c in named
                   if os.path.basename(c.outcome_path).lower().startswith(needle)]

    if len(matches) == 1:
        return matches[0]

    if not matches:
        names = ", ".join(c.computer_name or os.path.basename(c.outcome_path) for c in named)
        raise RuntimeError(f"no collection named '{wanted}'; this directory holds {names}")

    spelled = ", ".join(os.path.basename(c.outcome_path) for c in matches)
    raise RuntimeError(f"'{wanted}' names {len(matches)} collections ({spelled}); "
                       "name one of their Outcome files instead")


def collection_of(source: str, sidecar_paths: list, wanted: str | None = None) -> Collection:
    """The one collection 'source' is, from its sidecars as plain files (sidecars.resolve).

    A file is a collection of one archive with the run whose Outcome names it - or the first
    Outcome beside it, as before. A directory holding several runs is not one
    collection: ingesting it as one merges hosts that have nothing to do with each other into
    a dataset named after whichever Outcome sorted first, so it is refused unless 'wanted'
    names one of them. 'fleet' is the command that reads all of them.
    """
    from . import sidecars
    if os.path.isfile(source):
        runs = sidecars.group(collection_dir(source),
                              sidecars.Resolved(list(sidecar_paths), [])).collections
        stem = layout.archive_stem(source).lower()
        named = [c for c in runs if c.outcome_path]
        own = next((c for c in named
                    if stem in {layout.archive_stem(a).lower() for a in c.archives}),
                   named[0] if named else None)
        if own is None:
            return Collection(None, [source], "", "", "")
        return own._replace(archives=[source])

    found = sidecars.group(source, sidecars.Resolved(list(sidecar_paths), [])).collections
    if not found:
        raise RuntimeError(f"no Orc Outcome json and no archive found in '{source}'")

    named = [c for c in found if c.outcome_path]
    if wanted:
        return select_collection(found, wanted)

    if len(named) > 1:
        who = ", ".join(c.computer_name or os.path.basename(c.outcome_path) for c in named)
        raise RuntimeError(
            f"{len(named)} collections in '{source}' ({who}). 'orca ingest' reads one "
            f"collection; use 'orca fleet' for all of them, or "
            f"'--collection {named[0].computer_name}' to pick one.")

    # One Outcome plus archives it does not name is one collection, as it always was: an
    # archive ORC wrote after its Outcome, or one dropped in by hand, is collected data and
    # nothing says which run it belongs to. Said out loud, as unnamed_archives() does.
    head = named[0] if named else found[0]
    strays = [path for c in found if c is not head for path in c.archives]
    for path in strays:
        print(f"'{os.path.basename(path)}' is not in the Outcome that was read; "
              "opening it anyway")
    return head._replace(archives=head.archives + strays)


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


def capture_sidecar_logs(directories: list[str], out_dir: str,
                         run: str | None = None) -> list[str]:
    """Copy the collection-level logs into the run.

    WolfLauncher's own log and console output are not in any archive: ORC writes them beside
    the archives and, when encrypted, seals them there - so they only exist as plain files once
    unseal_sidecars has run. 'run' keeps only one run's ('ORC_<set>_<host>_<stamp>.log') out
    of a directory holding several hosts'.
    """
    found = []
    for directory in directories:
        try:
            entries = sorted(os.listdir(directory))
        except OSError:
            continue

        found += [os.path.join(directory, entry) for entry in entries
                  if entry.lower().endswith(".log")
                  and (run is None or entry.lower().startswith(run.lower()))]
    return copy_into_run(found, out_dir)


def error_containers(entries: list) -> list[str]:
    """The 'Errors' sub-archives among an archive's entries: the failing commands' output."""
    return [entry.path for entry in entries
            if entry.path.lower().endswith(".7z") and "error" in entry.path.lower()]


def expand_error_container(seven_zip: str, top: str, container_path: str, stage: str,
                           out_dir: str, password) -> bool:
    """Expand one 'Errors' sub-archive into 'out_dir/Errors'. True when it came out.

    Its password is its own: ORC writes it into the '_cli_config.xml' beside it in the
    archive, which is why that file is fetched in the same breath.
    """
    cfg = os.path.splitext(container_path)[0] + "_cli_config.xml"
    try:
        sevenzip.extract_matching(seven_zip, top, [container_path, cfg], stage, password)
    except RuntimeError:
        return False

    container = os.path.join(stage, container_path)
    if not os.path.isfile(container):
        return False

    pw = password or sevenzip.password_from_cli_config(os.path.join(stage, cfg))
    dest = os.path.join(out_dir, ERRORS_DIR)
    os.makedirs(dest, exist_ok=True)
    try:
        sevenzip.extract_all(seven_zip, container, dest, pw)
    except RuntimeError:
        return False

    return True


def capture_logs(seven_zip: str, archives: list, out_dir: str, password) -> list[str]:
    """Extract every *.log entry and any 'Errors' sub-archive of each archive.

    Each archive's land in a directory of its own named after it ('<out_dir>/<archive stem>/'),
    under the names they carry in the archive: two archives' 'Errors' containers no longer
    expand over each other, and a log says which archive it came from. Returns them relative
    to 'out_dir' ('<stem>/NTFSInfo.log', '<stem>/Errors'), as products.txt records them.

    One extraction call per archive, not one per log: ORC writes a single solid block
    (Archive7z.cpp), so reaching an entry costs decoding everything in front of it, and asking
    for each log separately pays that again for each. Listing first is free by comparison - it
    only reads the header - and is what names the 'Errors' container to ask for in the same
    call.
    """
    written = []
    for top in archives:
        stem = layout.archive_stem(top)
        place = os.path.join(out_dir, stem)
        # Resolved per archive, not once: a collection holds several, each with its own
        # '_cli_config.xml' beside it, and only an explicit --password overrides them all.
        pw = sevenzip.password_for(top, password)
        entries = sevenzip.list_entries(seven_zip, top, pw)
        logs = [entry.path for entry in entries if entry.path.lower().endswith(".log")]
        if logs:
            os.makedirs(place, exist_ok=True)
            try:
                sevenzip.extract_matching(seven_zip, top, "*.log", place, pw)
            except RuntimeError as error:
                # Named, not swallowed: an archive that will not open (a password nobody
                # passed, most often) returning no logs is indistinguishable from one that
                # holds none, and the logs are what explains the failure being reported.
                print(f"could not read the logs of '{os.path.basename(top)}': {error}")

            # What came out, not what was asked for: an entry 7-Zip warned about is missing
            # from the run, and naming it a product would have the cache delete a file that
            # is not there and claim the log was captured.
            written += [os.path.join(stem, path) for path in logs
                        if os.path.isfile(os.path.join(place, path))]

        for container_path in error_containers(entries):
            stage = scratch.directory("errors_")
            try:
                if expand_error_container(seven_zip, top, container_path, stage, place, pw):
                    written.append(os.path.join(stem, ERRORS_DIR))
            finally:
                shutil.rmtree(stage, ignore_errors=True)

    return written
