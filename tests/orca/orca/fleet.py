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

from . import cache, ingest, layout, sevenzip

# The fleet's own bookkeeping, beside the per-host datasets rather than inside any one of them.
FLEET_DIR = ".orca-fleet"


class FleetResult(NamedTuple):
    ingested: list[tuple]        # (Collection, dataset path)
    failed: list[tuple]          # (Collection, error message)


def run_fleet(directory: str, out: str | None = None, *, password=None, private_key=None,
              cms_backend=None, engine_name="duckdb", jobs=None, refresh: bool = False,
              strict: bool = False, at_input: bool = False) -> int:
    """Ingest every collection in 'directory'. Returns a process exit code.

    A host that cannot be ingested costs that host and nothing else: on a hundred machines the
    one truncated archive must not take the other ninety-nine with it, which is what '--strict'
    would do and what the default - skipping a single table - does not cover.
    """
    seven = sevenzip.find_7z()
    if not seven:
        raise RuntimeError("7z.exe not found")

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

    # Under keep_dir, the same place the decrypted archives went: the current directory,
    # --out, the collection directory, or the scratch fallback - so a directory orca could not write to does not fail
    # here, after every host was ingested.
    write_coverage(os.path.join(keep_dir, FLEET_DIR), collections, result)

    print(f"{len(result.ingested)} of {len(collections)} collection(s) ingested")
    if result.failed:
        names = ", ".join(c.computer_name or "unnamed" for c, _ in result.failed)
        print(f"{len(result.failed)} failed: {names}")

    return 1 if result.failed else 0


def write_coverage(where: str, collections: list, result: FleetResult) -> None:
    """Record which hosts answered, and which tables they could not produce, under 'where'.

    A stacking query over a glob reads the hosts that have the file and says nothing about the
    rest: if fourteen hosts produced no autoruns because the command failed, the answer
    describes eighty-six while reading as if it described a hundred. In hunting, 'absent'
    reading as 'clean' is the dangerous failure, so the denominator is written down.

    CSV because the fleet view is SQL: duckdb reads these beside the parquet with no extra step.
    """
    os.makedirs(where, exist_ok=True)

    # By identity, not equality: 'result' is built from the very objects in 'collections'.
    datasets = {id(collection): dataset for collection, dataset in result.ingested}
    failures = {id(collection): message for collection, message in result.failed}

    with open(os.path.join(where, "hosts.csv"), "w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(["OrcComputerName", "OrcRunId", "dataset", "status", "error"])
        for collection in collections:
            if id(collection) in failures:
                writer.writerow([collection.computer_name, collection.run_id, "", "failed",
                                 failures[id(collection)]])
            else:
                writer.writerow([collection.computer_name, collection.run_id,
                                 datasets[id(collection)], "ingested", ""])

    # Only the gaps are written: absent means present, so a healthy fleet leaves a short file
    # in which every line is something to look at. Keyed by dataset, not by computer name: the
    # same host collected twice is two runs with two sets of gaps.
    produced = {dataset: set(layout.table_names(dataset)) for _, dataset in result.ingested}
    everywhere = set().union(*produced.values()) if produced else set()

    with open(os.path.join(where, "coverage.csv"), "w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(["OrcComputerName", "table", "status"])
        for collection, dataset in result.ingested:
            for name in sorted(layout.skipped(dataset)):
                writer.writerow([collection.computer_name, name, "skipped"])
            for name in sorted(everywhere - produced[dataset]):
                writer.writerow([collection.computer_name, name, "missing"])
