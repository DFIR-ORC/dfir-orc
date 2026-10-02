"""orca check: verify a collection is intact, without expanding it.

Three layers, cheapest first. The Outcome layer asks ORC's own record what it wrote and
re-reads the files to confirm it - no key and no decryption, since the sha1 ORC stores is of
the file it put on disk (the '.p7b' itself for an encrypted run). The envelope layer decrypts
and replays the journal. The container layer runs '7z t', which decompresses every entry to
nothing and recomputes its CRC, recursing into nested containers one at a time so peak disk
stays at one container per level instead of the whole expanded tree.
"""
import hashlib
import json
import os
import shutil
from typing import NamedTuple

from . import extract, ingest, scratch, sevenzip
from .outcome import read_outcome

OK = "ok"
FAILED = "FAILED"
MISSING = "MISSING"
SHORT = "SHORT"
SIZE = "size differs"
SHA1 = "sha1 differs"
EMPTY = "empty"
WARNED = "warnings"
UNSEALED = "unsealed"

# The verdicts that make 'check' exit non-zero. 'empty' and 'warnings' are reported and still
# pass: ORC does write 0-byte archives (extract.py skips them for the same reason), and an
# archive the Outcome agrees is empty has lost nothing. A wrongly empty one is SHORT instead.
FAILING = frozenset({FAILED, MISSING, SHORT, SIZE, SHA1})

_HASH_CHUNK = 1 << 20


class Finding(NamedTuple):
    """One line of a check report: what was looked at, the verdict, and what says so.

    'level' is how deep in the containers it sits, which is only ever used to indent the
    report - the verdicts themselves do not care how nested a container was.
    """
    target: str
    verdict: str
    detail: str = ""
    level: int = 0

    @property
    def failed(self) -> bool:
        return self.verdict in FAILING


def file_sha1(path: str) -> str:
    """Streamed, so hashing a multi-GB archive costs a read rather than that much memory."""
    digest = hashlib.sha1()
    with open(path, "rb") as source:
        for block in iter(lambda: source.read(_HASH_CHUNK), b""):
            digest.update(block)

    return digest.hexdigest()


def recorded_size(archive: dict) -> int | None:
    """The size an Outcome records for an archive, or None when it records none usably.

    ORC writes it as a number, but an Outcome is read from disk like any other parsed input:
    a value that is not a number means 'unknown', never a comparison against a guess.
    """
    value = archive.get("size")
    if isinstance(value, bool):
        return None
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        try:
            return int(value, 0)
        except ValueError:
            return None

    return None


def check_recorded_archive(directory: str, name: str, size: int | None, sha1: str) -> Finding:
    """One archive the Outcome names, against the file of that name in 'directory'."""
    path = ingest.archive_as_found(directory, name)
    if not os.path.exists(path):
        return Finding(name, MISSING, "named by the Outcome, not on disk")

    found = os.path.basename(path)
    if found != name:
        # Only the decrypted copy is here. ORC's size and sha1 are of the file it wrote - the
        # sealed one - so there is nothing to compare, and reporting a mismatch would be a
        # false alarm. The container layer still verifies what the archive holds.
        return Finding(name, UNSEALED,
                       f"only '{found}' is on disk; the Outcome's size and sha1 are of the "
                       f"sealed file")

    on_disk = os.path.getsize(path)
    if size is not None and on_disk != size:
        # Reported before the hash: a size that disagrees is the finding, and a sha1 over the
        # wrong number of bytes would only restate it.
        verdict = SHORT if on_disk < size else SIZE
        return Finding(name, verdict, f"on disk {on_disk}, Outcome {size}")

    if on_disk == 0:
        return Finding(name, EMPTY, "0 bytes, as the Outcome records")

    if not sha1:
        return Finding(name, OK, f"{on_disk} bytes, no sha1 recorded")

    actual = file_sha1(path)
    if actual.lower() != sha1.lower():
        return Finding(name, SHA1, f"on disk {actual[:12]}, Outcome {sha1[:12].lower()}")

    return Finding(name, OK, f"sha1 ok, {on_disk} bytes")


def check_against_outcome(outcome: dict, directory: str) -> list[Finding]:
    """Every archive the Outcome's command sets name, checked against 'directory'.

    A command set whose commands all failed before anything was archived carries no 'archive'
    object at all, and that is not a finding of this layer - 'orca report' is where a failed
    command belongs.
    """
    findings = []
    for command_set in outcome.get("command_set", []) or []:
        archive = command_set.get("archive") or {}
        name = archive.get("name", "")
        if not name:
            continue

        findings.append(check_recorded_archive(directory, name, recorded_size(archive),
                                               archive.get("sha1", "") or ""))

    return findings


def container_entries(entries: list) -> list[str]:
    """The nested containers an archive holds, as entry paths, in listing order.

    ORC nests one container per command set, and those are what hold the collected samples, so
    an archive is only as sound as the containers inside it.
    """
    return [entry.path for entry in entries if entry.path.lower().endswith(".7z")]


def nested_config_entry(entries: list, container: str) -> str | None:
    """The "<name>_cli_config.xml" ORC ships beside a container, when the archive holds one.

    It carries the container's password, so it has to come out of the archive along with the
    container or a sealed container cannot be opened at all.
    """
    config = os.path.splitext(container)[0] + "_cli_config.xml"
    return config if any(entry.path == config for entry in entries) else None


def discard(paths: list[str]) -> None:
    """Delete what a container test needed on disk, so only one container is ever extracted."""
    for path in paths:
        try:
            os.remove(path)
        except OSError as error:
            # Not fatal, but worth saying: the work dir grows by one container per failure.
            print(f"could not remove '{path}': {error}")


def check_nested(seven_zip: str, archive: str, entry: str, config: str | None, staging: str,
                 password: str | None, depth: int | None,
                 level: int) -> list[Finding]:
    """Carry one nested container out of 'archive' into 'staging', check it, then delete it.

    One container at a time, deleted before the next is taken out: this is what keeps the peak
    disk cost at one container per level of nesting rather than the whole expanded tree.
    """
    extracted = []
    try:
        os.makedirs(staging, exist_ok=True)
        container = sevenzip.extract_entry(seven_zip, archive, entry, staging, password)
        extracted.append(container)
        if config:
            extracted.append(sevenzip.extract_entry(seven_zip, archive, config, staging,
                                                    password))

        return check_container(seven_zip, container, staging,
                               extract.container_password(container, password), depth, level)
    except (RuntimeError, OSError) as error:
        # The container could not be produced, so there is nothing to test: either 7-Zip would
        # not give it up or the work dir cannot hold it. Reported as a failed container rather
        # than raised, because a verification pass that dies has reported nothing at all.
        return [Finding(os.path.basename(entry), FAILED, str(error), level)]
    finally:
        discard(extracted)


def check_container(seven_zip: str, archive: str, work_dir: str, password: str | None = None,
                    depth: int | None = None, level: int = 0) -> list[Finding]:
    """Verify 'archive' and, unless 'depth' runs out, every container nested inside it.

    'depth' counts the layers still allowed to be opened, this archive included, so 1 tests the
    archive and leaves its containers packed. 'work_dir' is scratch the walk empties again - it
    must not be the directory holding a kept decrypted archive.
    """
    name = os.path.basename(archive)
    size = os.path.getsize(archive)
    if size == 0:
        # ORC does write these and 7-Zip cannot open one, so it needs saying first: an empty
        # container holds nothing to verify, which is not the same as a broken one.
        return [Finding(name, EMPTY, "0 bytes", level)]

    result = sevenzip.test_archive(seven_zip, archive, password)
    if not result.ok:
        # Stop here either way: the entries of an archive 7-Zip could not read through are not
        # worth extracting, and recursing would report damage that is already reported.
        verdict = WARNED if result.code == 1 else FAILED
        return [Finding(name, verdict, "; ".join(result.problems[:3]), level)]

    entries = sevenzip.list_entries(seven_zip, archive, password)
    findings = [Finding(name, OK, f"{len(entries)} entries, {size} bytes", level)]
    if depth is not None and depth <= 1:
        return findings

    containers = container_entries(entries)
    if not containers:
        return findings

    staging = os.path.join(work_dir, os.path.splitext(name)[0])
    for entry in containers:
        findings.extend(check_nested(seven_zip, archive, entry,
                                     nested_config_entry(entries, entry), staging, password,
                                     None if depth is None else depth - 1, level + 1))

    # Every container that went in there has been deleted, so this only takes the empty
    # directories with it, leaving the work dir as it was found.
    shutil.rmtree(staging, ignore_errors=True)
    return findings


def summarize(findings: list[Finding], source: str) -> dict:
    """The counts a summary needs, plus the findings themselves for the JSON rendering.

    'checked' is deliberately reported even when it is 0: nothing checked is nothing verified,
    and a 'checked=0 failed=0' line that read as success would be the one lie this command
    cannot afford.
    """
    counts: dict = {}
    for finding in findings:
        counts[finding.verdict] = counts.get(finding.verdict, 0) + 1

    return {
        "source": source,
        "checked": len(findings),
        "ok": counts.get(OK, 0),
        "failed": sum(1 for finding in findings if finding.failed),
        "verdicts": counts,
        "failures": [{"target": f.target, "verdict": f.verdict, "detail": f.detail}
                     for f in findings if f.failed],
        "findings": [finding._asdict() for finding in findings],
    }


def render_human(findings: list[Finding]) -> str:
    """One line per finding: the verdict in a column of its own, the target indented by how
    deep its container sits, then what says so. Verdicts align so failures are scannable in a
    report that is otherwise mostly 'ok'."""
    if not findings:
        return "  nothing to check"

    labels = ["  " * finding.level + finding.target for finding in findings]
    verdict_width = max(len(finding.verdict) for finding in findings)
    label_width = max(len(label) for label in labels)

    lines = [f"  {finding.verdict:<{verdict_width}}  {label:<{label_width}}  {finding.detail}"
             .rstrip() for finding, label in zip(findings, labels)]

    summary = summarize(findings, "")
    lines.append("")
    lines.append(f"  {summary['checked']} checked, {summary['ok']} ok, "
                 f"{summary['failed']} failed")
    return "\n".join(lines)


def render_one_line(summary: dict) -> str:
    line = (f'{summary["source"]}: checked={summary["checked"]} ok={summary["ok"]} '
            f'failed={summary["failed"]}')

    # Only when there is one, the way 'orca report' handles unread tables: a field that always
    # reads zero stops being read, and these three say the collection is unusual without being
    # broken, which is exactly what a summary should not bury.
    for verdict in (EMPTY, WARNED, UNSEALED):
        count = summary["verdicts"].get(verdict, 0)
        if count:
            line += f" {verdict}={count}"

    return line


def render_json(summary: dict) -> str:
    return json.dumps(summary, indent=2)


def check_one_collection_archive(seven_zip: str, top: str, work: str, containers: str,
                                 password: str | None, private_key: str | None,
                                 cms_backend: str | None, depth: int | None,
                                 refresh: bool) -> list[Finding]:
    """One top-level archive: open its envelope if it has one, then walk its containers.

    A sound envelope gets no line of its own - the container line that follows could not exist
    if the CMS layer and the journal had not replayed correctly, so saying it twice adds noise.
    A broken one is the finding.
    """
    name = os.path.basename(top)
    remaining = depth
    plain = top
    if ingest.has_encryption_layer(top):
        try:
            plain = ingest.decrypt_archive(top, work, private_key, cms_backend, refresh)
        except (RuntimeError, OSError) as error:
            return [Finding(name, FAILED, f"could not be opened: {error}")]

        if remaining is not None:
            remaining -= 1
            if remaining == 0:
                # '--depth 1' on an encrypted collection asks for the envelope and nothing
                # else, and it opened - which is a result, not an empty report.
                return [Finding(name, OK, "envelope opened, containers left packed")]

    return check_container(seven_zip, plain, containers, password, remaining)


def run_check(source: str, *, password=None, private_key=None, cms_backend=None, work_dir=None,
              depth=None, refresh=False, outcome=True, one_line=False, as_json=False) -> int:
    """Verify the collection at 'source' and return the exit code: 1 if anything failed.

    Nothing is written to the collection. 'ingest' and 'extract' unseal sidecars beside the
    archives on purpose, to be reused; a check must leave the evidence as it found it, so the
    sidecars it needs are unsealed into the work dir instead.
    """
    seven = sevenzip.find_7z()
    if not seven:
        raise RuntimeError("7z.exe not found")

    if depth is not None and depth < 1:
        raise RuntimeError(f"check: --depth is a number of layers to open, so it starts at 1 "
                           f"(the '.p7b' envelope); got {depth}")

    # The CMS plaintext is archive-sized and the temp volume is where the room runs out. Set
    # before anything takes a scratch directory.
    scratch.use(work_dir)
    work = work_dir or scratch.directory("check_")
    os.makedirs(work, exist_ok=True)

    ingest.unseal_sidecars(source, seven, work, private_key, cms_backend, refresh)
    outcome_path = next((found for found in map(ingest.find_outcome_json,
                                                ingest.sidecar_dirs(source, work)) if found),
                       None)

    findings = []
    if outcome:
        record = read_outcome(outcome_path) if outcome_path else None
        if record:
            findings += check_against_outcome(record, ingest.collection_dir(source))
        else:
            # Worth saying rather than quietly dropping a layer: without the Outcome there is
            # no record of what ORC wrote, so truncation can no longer be seen at all.
            print("no Outcome json for this collection: checking the archives only")

    containers = os.path.join(work, "containers")
    for top in ingest.resolve_archives(source, os.path.dirname(outcome_path)
                                       if outcome_path else None):
        if not os.path.exists(top):
            # An archive the Outcome names and the collection lacks: already reported as
            # MISSING by the layer that knows it was expected, so do not say it twice.
            if not outcome:
                findings.append(Finding(os.path.basename(top), MISSING, "not on disk"))
            continue

        findings.extend(check_one_collection_archive(seven, top, work, containers, password,
                                                    private_key, cms_backend, depth, refresh))

    summary = summarize(findings, source)
    if as_json:
        print(render_json(summary))
    elif one_line:
        print(render_one_line(summary))
    else:
        print(render_human(findings))

    return 1 if summary["failed"] else 0
