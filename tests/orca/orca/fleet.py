"""orca fleet: ingest every collection of a directory, one dataset per host.

A directory of results from several machines is several runs, not one. 'ingest' reads one and
refuses the rest; this reads all of them, each into its own dataset named after its own run.

One collection at a time: each ingest already parallelises its own tables and sizes duckdb's
memory for that, and running several collections at once would multiply a budget that is
already correct. A fleet therefore costs the sum of its hosts, and a second run costs almost
nothing - cache.ensure_dataset skips the hosts that are already current.
"""
import csv
import os
from typing import NamedTuple

from . import cache, fleetdb, ingest, layout, orcschema, sevenzip
from .engine import get_engine

# The fleet's own bookkeeping, beside the per-host datasets rather than inside any one of them.
FLEET_DIR = ".orca-fleet"


class FleetResult(NamedTuple):
    ingested: list[tuple]        # (Collection, dataset path)
    failed: list[tuple]          # (Collection, error message)


def run_fleet(directory: str, out: str | None = None, *, password=None, private_key=None,
              cms_backend=None, engine_name="duckdb", jobs=None, refresh: bool = False,
              strict: bool = False, at_input: bool = False,
              schema: str = orcschema.AUTO_OPTION) -> int:
    """Ingest every collection in 'directory'. Returns a process exit code.

    A host that cannot be ingested costs that host and nothing else: on a hundred machines the
    one truncated archive must not take the other ninety-nine with it, which is what '--strict'
    would do and what the default - skipping a single table - does not cover.
    """
    seven = sevenzip.find_7z()
    if not seven:
        raise RuntimeError("7z.exe not found")

    # Before any host, not after: a mistyped --schema should not cost the length of an ingest.
    typing = orcschema.resolve(schema)

    # Unseal before grouping, never after: an encrypted fleet's Outcomes are
    # '<run>_Outcome.json.7z.p7b' and collections_in reads plain JSON, so grouping first finds
    # no Outcome, one unnamed collection, and ingests every host into one dataset. keep_dir is
    # where the plain sidecars land - the current directory, --out, beside the archives with
    # --out-at-input, or scratch when that is read-only - and is where collections_in has to
    # look as well.
    keep_dir, _ = layout.resolve(directory, out, at_input=at_input)
    ingest.unseal_sidecars(directory, seven, keep_dir, private_key, cms_backend, refresh)
    collections = ingest.collections_in(directory, ingest.sidecar_dirs(directory, keep_dir))
    if not collections:
        raise RuntimeError(f"no collection found in '{directory}': no Orc Outcome json "
                           "and no archive")

    result = FleetResult([], [])
    for collection in collections:
        who = collection.computer_name or "unnamed collection"
        try:
            dataset = cache.ensure_dataset(
                directory, out, collection=collection, jobs=jobs, password=password,
                private_key=private_key, cms_backend=cms_backend, engine_name=engine_name,
                refresh=refresh, strict=strict, at_input=at_input)
            result.ingested.append((collection, dataset))
        except (RuntimeError, OSError) as error:
            print(f"'{who}' failed: {error}")
            result.failed.append((collection, str(error)))

    # Under keep_dir, the same place the decrypted archives went: the current directory, --out,
    # the collection directory, or the scratch fallback - so a directory orca could not write
    # to does not fail here, after every host was ingested.
    write_coverage(os.path.join(keep_dir, FLEET_DIR), collections, result)

    print(f"{len(result.ingested)} of {len(collections)} collection(s) ingested")
    if result.failed:
        names = ", ".join(c.computer_name or "unnamed" for c, _ in result.failed)
        print(f"{len(result.failed)} failed: {names}")

    if not write_database(keep_dir, collections, result, engine_name, typing):
        return 1

    return 1 if result.failed else 0


HOST_COLUMNS = ["OrcComputerName", "OrcRunId", "dataset", "status", "error"]
COVERAGE_COLUMNS = ["OrcComputerName", "table", "status"]


def host_rows(collections: list, result: FleetResult, root: str | None = None) -> list[list[str]]:
    """One row per collection, failed ones included: the denominator of any fleet answer.

    With 'root', the fleet's directory, each dataset is written relative to it (stored_dataset).
    """
    # By identity, not equality: 'result' is built from the very objects in 'collections'.
    datasets = {id(collection): dataset for collection, dataset in result.ingested}
    failures = {id(collection): message for collection, message in result.failed}

    rows = []
    for collection in collections:
        if id(collection) in failures:
            rows.append([collection.computer_name, collection.run_id, "", "failed",
                         failures[id(collection)]])
        else:
            rows.append([collection.computer_name, collection.run_id,
                         stored_dataset(datasets[id(collection)], root), "ingested", ""])
    return rows


def stored_dataset(dataset: str, root: str | None) -> str:
    """'dataset' as the fleet's records hold it: relative to 'root', the fleet's directory.

    Relative so that a fleet copied off the analysis machine, or moved to another disk, still
    names its own datasets. Absolute where no relative path exists (another drive), and that
    record breaks on a move the way every record did before.
    """
    if not root:
        return dataset
    try:
        return os.path.relpath(dataset, root)
    except ValueError:
        return os.path.abspath(dataset)


def dataset_of(row: dict, where: str) -> str:
    """The dataset a hosts.csv row names, as a path: relative ones are under the fleet's directory,
    the parent of 'where' (.orca-fleet). Absolute ones, as older fleets wrote, are kept. Empty
    for a host that failed."""
    dataset = row.get("dataset") or ""
    if not dataset:
        return ""
    return os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(where)), dataset))


def coverage_rows(result: FleetResult) -> list[list[str]]:
    """The gaps: a table a host could not read, and a table family other hosts produced and this
    one did not.

    Families, not table names: a per-volume table is named after its volume, and by name every
    host would be 'missing' every other host's volumes. Only the gaps are listed - absent means
    present - so a healthy fleet gives a short list in which every line is something to look
    at. Keyed by dataset, not computer name: the same host collected twice is two runs with two
    sets of gaps.
    """
    produced = {dataset: set(fleetdb.dataset_families(dataset))
                for _, dataset in result.ingested}
    everywhere = set().union(*produced.values()) if produced else set()

    rows = []
    for collection, dataset in result.ingested:
        skipped = sorted(layout.skipped(dataset))
        for name in skipped:
            rows.append([collection.computer_name, name, "skipped"])

        # A skipped table was collected, so it is not missing as well: one line, the true one.
        entries = dict(layout.table_entries(dataset))
        unread = {fleetdb.family_of(name, entries.get(name, "")) for name in skipped}
        for family in sorted(everywhere - produced[dataset] - unread):
            rows.append([collection.computer_name, family, "missing"])
    return rows


HOSTS_FILE = "hosts.csv"
COVERAGE_FILE = "coverage.csv"


def _read_csv(path: str) -> list[dict]:
    """The rows of one of the fleet's own CSVs; empty when it is not there."""
    try:
        with open(path, newline="", encoding="utf-8") as f:
            return list(csv.DictReader(f))
    except OSError:
        return []


def read_hosts(where: str) -> list[dict]:
    """hosts.csv as write_coverage wrote it under 'where' (the .orca-fleet directory)."""
    return _read_csv(os.path.join(where, HOSTS_FILE))


def read_coverage(where: str) -> list[dict]:
    """coverage.csv as write_coverage wrote it under 'where'."""
    return _read_csv(os.path.join(where, COVERAGE_FILE))


def _write_csv(path: str, columns: list[str], rows: list[list[str]]) -> None:
    with open(path, "w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(columns)
        writer.writerows(rows)


def write_coverage(where: str, collections: list, result: FleetResult) -> None:
    """Record which hosts answered, and which tables they could not produce, under 'where'.

    A stacking query over a glob reads the hosts that have the file and says nothing about the
    rest: if fourteen hosts produced no autoruns because the command failed, the answer
    describes eighty-six while reading as if it described a hundred. In hunting, 'absent'
    reading as 'clean' is the dangerous failure, so the denominator is written down.
    """
    os.makedirs(where, exist_ok=True)
    _write_csv(os.path.join(where, HOSTS_FILE), HOST_COLUMNS,
               host_rows(collections, result, os.path.dirname(os.path.abspath(where))))
    _write_csv(os.path.join(where, COVERAGE_FILE), COVERAGE_COLUMNS, coverage_rows(result))


def write_database(where: str, collections: list, result: FleetResult,
                   engine_name: str = "duckdb", typing=None) -> bool:
    """Write the fleet database beside the datasets (see fleetdb). False when it could not be.

    The datasets do not depend on it, so a database that cannot be written is reported rather
    than raised, after every host was ingested; the exit code still says so.
    """
    if not result.ingested:
        return True

    database = fleetdb.database_path(where)
    families = fleetdb.fleet_families([dataset for _, dataset in result.ingested])
    tables = {"orca_hosts": (HOST_COLUMNS, host_rows(collections, result, where)),
              "orca_coverage": (COVERAGE_COLUMNS, coverage_rows(result))}
    try:
        built = fleetdb.build_database(get_engine(engine_name), database, families, tables,
                                       typing)
    except RuntimeError as error:
        print(f"could not write the fleet database '{database}': {error}")
        return False

    views = len(families) - sum(1 for view, _ in built.skipped if view in families)
    print(f"fleet database: {database} ({views} view(s), {built.typed} typed; "
          f"open with: duckdb \"{database}\")")
    print(f"  typed views: {typing.source}" if typing else
          "  no ORC schema found: no typed views (pass --schema)")
    for view, reason in built.skipped:
        print(f"  view skipped: {view}: {reason}")
    report_outdated(built.outdated, typing)
    return True


def report_outdated(outdated: list, typing) -> None:
    """Name the tables carrying columns their schema does not know, and what to do about it.

    With the bundled schema that means the fleet was collected by a newer ORC than this orca
    knows, and that ORC's binary describes them. Read from their values in the meantime when
    the typing infers, left text when it does not.
    """
    if not outdated:
        return

    fate = "typed from their values" if typing is not None and typing.infer else "they stay text"
    print(f"  {len(outdated)} table(s) have columns the schema does not know; {fate}:")
    for family, columns in outdated:
        print(f"    {family}: {', '.join(columns)}")
    schemas = typing.schemas if typing is not None else None
    if schemas is not None and schemas.source == orcschema.BUNDLED_SOURCE:
        print("  the collection was likely made by a newer ORC than the bundled schema; for its "
              "own types, pass --schema with the path of the DFIR-ORC.exe that collected it")
