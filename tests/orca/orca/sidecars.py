"""A collection's own files - Outcome, Outlines, log, console output - and what belongs to what.

Two rules (docs/superpowers/specs/2026-10-07-orca-dataset-layout-design.md, section 4.4):

- One rule for every sealed file: '<name>.7z.p7b' holds '<name>', and when a plain '<name>' is
  already there - in the collection, or in a dataset under the output - it is used and nothing
  is decrypted, so a fleet whose every host is current needs no key. Otherwise the sealed file
  is decrypted into scratch; ingest copies what it needs into the dataset, which then holds the
  only plain copy.
- What belongs to what is read from the files, never from their names: an Outcome names its
  archives, its log, its Outline and its console output, and an Outline belongs to the run
  whose Outcome carries the same id.
"""
import json
import os
from typing import NamedTuple

from . import ingest, layout, scratch

SIDECAR_EXTENSIONS = ingest.SEALED_OUTPUT_EXTENSIONS


def dataset_dirs(root: str | None) -> list[str]:
    """The datasets under 'root': directly under it, where ingest puts one, and under its
    'hosts', where a fleet puts them - the directories an ingest wrote its bookkeeping in."""
    if not root:
        return []
    found = []
    for place in (root, os.path.join(root, layout.HOSTS_DIR)):
        try:
            names = sorted(os.listdir(place))
        except OSError:
            continue
        found += [os.path.join(place, name) for name in names
                  if os.path.isdir(layout.orca_dir(os.path.join(place, name)))]
    return found


def look_for(name: str, places: list[str]) -> str | None:
    """The first plain file called 'name' in 'places', or None."""
    for place in places:
        candidate = os.path.join(place, name)
        if os.path.isfile(candidate):
            return candidate
    return None


def plain_sidecars(directory: str) -> list[str]:
    """The plain json/log/txt/xml files beside a collection's archives."""
    try:
        entries = sorted(os.listdir(directory))
    except OSError:
        return []
    return [os.path.join(directory, entry) for entry in entries
            if entry.lower().endswith(SIDECAR_EXTENSIONS)
            and os.path.isfile(os.path.join(directory, entry))]


class Resolved(NamedTuple):
    """A collection's sidecars as plain files: every one, and those that were sealed."""
    paths: list
    from_sealed: list


def resolve(directory: str, root: str | None, seven_zip: str, private_key=None,
            cms_backend=None, refresh: bool = False, places=()) -> Resolved:
    """Every sidecar of the collection in 'directory', as a plain file wherever it is.

    A sealed one is looked for under its plain name in the collection, in 'places', and in
    every dataset under 'root' (the output: the current directory, --out or --work-dir, or the
    collection with --out-at-input) before anything is decrypted. '--refresh' decrypts anyway.
    One that cannot be decrypted is named and left out; the rest of the collection still
    ingests.
    """
    found = plain_sidecars(directory)
    from_sealed = []
    lookup = [directory] + list(places) + dataset_dirs(root)
    work = None
    for sealed in ingest.sealed_sidecars(directory):
        name = ingest.sealed_name(sealed)
        existing = None if refresh else look_for(name, lookup)
        if existing:
            from_sealed.append(existing)
            continue

        work = work or scratch.directory("unseal_")
        try:
            from_sealed.append(ingest.unseal_one(sealed, os.path.join(work, name), seven_zip,
                                                 private_key, cms_backend))
            print(f"unsealed '{name}'")
        except (RuntimeError, OSError) as error:
            print(f"could not unseal '{os.path.basename(sealed)}': {error}")

    seen, paths = set(), []
    for path in found + from_sealed:
        key = os.path.normcase(os.path.abspath(path))
        if key not in seen:
            seen.add(key)
            paths.append(path)
    return Resolved(paths, from_sealed)


def _document(path: str) -> dict:
    """The 'dfir-orc' object of a JSON file; empty when it is not one."""
    if not path.lower().endswith(".json"):
        return {}
    try:
        with open(path, encoding="utf-8-sig") as f:
            data = json.load(f)
    except (ValueError, OSError):
        return {}
    document = data.get("dfir-orc") if isinstance(data, dict) else None
    return document if isinstance(document, dict) else {}


def kind_of(path: str) -> tuple[str, dict]:
    """('outcome' | 'outline' | 'other', the object) for one sidecar, by its content."""
    document = _document(path)
    for kind in ("outcome", "outline"):
        if isinstance(document.get(kind), dict):
            return kind, document[kind]
    return "other", {}


def _file_name(outcome: dict, key: str) -> str:
    """The file an Outcome names under 'key' ('log', 'outline', 'console'), or ''."""
    entry = outcome.get(key) or {}
    return ((entry.get("file") or {}).get("name") or "") if isinstance(entry, dict) else ""


def archive_named(directory: str, name: str) -> str:
    """The archive 'name' is on disk, sealed or plain: an Outcome names '<x>.7z.p7b', an Outline
    '<x>.7z', and either may be what arrived. The name itself when neither is there - that is
    the thing missing."""
    stem = layout.archive_stem(name)
    for candidate in (name, stem + ".7z.p7b", stem + ".7z"):
        if os.path.exists(os.path.join(directory, candidate)):
            return os.path.join(directory, candidate)
    return os.path.join(directory, name)


def outcome_archives(outcome: dict) -> list[str]:
    names = []
    for command_set in outcome.get("command_set", []) or []:
        archive = command_set.get("archive")
        for one in (archive if isinstance(archive, list) else [archive] if archive else []):
            if one.get("name"):
                names.append(one["name"])
    return names


def outline_archives(outline: dict) -> list[str]:
    return [entry.get("file") for entry in outline.get("archives", []) or []
            if isinstance(entry, dict) and entry.get("file")]


def run_name(log_name: str, computer: str, timestamp: str) -> str:
    """The dataset's name: the log the Outcome lists, less '.log' - the name ORC gave the run.
    A run with no such log (one known by its Outline alone) is '<computer>_<timestamp>'."""
    if log_name.lower().endswith(".log"):
        return log_name[:-len(".log")]
    return f"{computer or 'unknown'}_{timestamp or 'unknown'}"


class Grouped(NamedTuple):
    collections: list
    unattached: list     # sealed sidecars no run claims


def group(directory: str, resolved: Resolved) -> Grouped:
    """The runs of the collection in 'directory', from what its sidecars say.

    One per Outcome (two copies of one Outcome are one run, by id); one per Outline whose run
    has no Outcome, with the archives that Outline lists; and one more for the archives
    nothing names, as an unnamed collection. A sealed sidecar no run claims is returned as
    unattached, to be named rather than dropped.
    """
    kinds = {path: kind_of(path) for path in resolved.paths}
    by_name = {os.path.basename(path).lower(): path for path in resolved.paths}
    outlines = {path: obj for path, (kind, obj) in kinds.items() if kind == "outline"}

    found, claimed, used, seen = [], [], set(), set()
    for path, (kind, outcome) in kinds.items():
        if kind != "outcome":
            continue

        used.add(path)
        run_id = outcome.get("id") or ""
        if run_id and run_id in seen:
            continue
        seen.add(run_id)

        archives = [archive_named(directory, name) for name in outcome_archives(outcome)]
        own_outlines = [p for p, obj in outlines.items() if run_id and obj.get("id") == run_id]
        named_outline = by_name.get(_file_name(outcome, "outline").lower())
        if named_outline and named_outline not in own_outlines:
            own_outlines.append(named_outline)
        sidecars = [by_name[name.lower()] for name in (_file_name(outcome, "log"),
                                                       _file_name(outcome, "console"))
                    if name and name.lower() in by_name]

        computer, timestamp = outcome.get("computer_name") or "", outcome.get("timestamp") or ""
        found.append(ingest.Collection(path, archives, computer, run_id, timestamp,
                                       tuple(own_outlines), tuple(sidecars),
                                       run_name(_file_name(outcome, "log"), computer,
                                                timestamp)))
        claimed += archives
        used.update(own_outlines, sidecars)

    found += outline_runs(directory, outlines, seen, claimed, used)

    leftover = ingest.archives_not_named(directory, claimed)
    if leftover:
        found.append(ingest.Collection(None, leftover, "", "", ""))

    unattached = [path for path in resolved.from_sealed if path not in used]
    return Grouped(found, unattached)


def outline_runs(directory: str, outlines: dict, seen: set, claimed: list, used: set) -> list:
    """A run for every Outline id no Outcome carries: the Outline is all there is of it.

    Updates 'claimed' and 'used' with what these runs take, as group does for the Outcomes'.
    """
    runs = []
    by_id: dict = {}
    for path, obj in outlines.items():
        if (obj.get("id") or "") not in seen:
            by_id.setdefault(obj.get("id") or path, []).append(path)

    for _run_id, paths in sorted(by_id.items()):
        outline = outlines[paths[0]]
        taken = {os.path.normcase(path) for path in claimed}
        archives = [archive_named(directory, name) for name in outline_archives(outline)]
        archives = [path for path in archives if os.path.normcase(path) not in taken]
        computer, timestamp = outline.get("computer_name") or "", outline.get("timestamp") or ""
        runs.append(ingest.Collection(None, archives, computer, outline.get("id") or "",
                                      timestamp, tuple(paths), (),
                                      run_name("", computer, timestamp)))
        claimed += archives
        used.update(paths)
    return runs
