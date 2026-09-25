"""orca extract: expand ORC results to a directory tree, decrypting and unnesting on the way.

The successor to the PowerShell Expand-OrcArchive / Expand-OrcResults. Unlike 'ingest', this
writes the whole collected tree, so it is for the times you genuinely want the files on disk;
prefer 'ingest' when you only want to query the data.
"""
import os
import shutil

from . import layout, scratch, sevenzip
from .ingest import (capture_outlines, capture_sidecar_logs, collection_dir, copy_into_run,
                     decrypt_archive, find_outcome_json, resolve_archives, unseal_sidecars)


def nested_containers(root: str) -> list[str]:
    """Every '.7z' found under an expanded tree, innermost paths included.

    ORC nests one container per command set, and those containers are what hold the collected
    samples, so a tree is not usable until they are expanded too.
    """
    found = []
    for directory, _, files in os.walk(root):
        for name in files:
            if name.lower().endswith(".7z"):
                found.append(os.path.join(directory, name))
    return sorted(found)


def container_password(container: str, password: str | None) -> str | None:
    """An explicit password wins; otherwise read the one ORC recorded next to the container."""
    if password:
        return password

    config = os.path.splitext(container)[0] + "_cli_config.xml"
    if not os.path.exists(config):
        return None
    return sevenzip.password_from_cli_config(config)


def retire_container(container: str, root: str, containers_dir: str) -> None:
    """Take a container that expanded cleanly out of the tree, into the working directory.

    Everything it held is in the tree now, so the archive itself is an intermediate and has no
    business in the deliverable. It keeps the path it had within the tree, so two command sets
    that both ship a 'GetSamples.7z' cannot land on each other.
    """
    target = os.path.join(containers_dir, os.path.relpath(container, root))
    os.makedirs(os.path.dirname(target), exist_ok=True)
    if os.path.exists(target):
        os.remove(target)

    shutil.move(container, target)


def expand_containers(seven_zip: str, root: str, password: str | None,
                      containers_dir: str) -> int:
    """Expand each nested container beside itself, into a directory named after it."""
    expanded = 0
    for container in nested_containers(root):
        if os.path.getsize(container) == 0:
            print(f"skipped empty container '{container}'")
            continue

        destination = os.path.splitext(container)[0]
        os.makedirs(destination, exist_ok=True)
        code = sevenzip.extract_all(seven_zip, container, destination,
                                    container_password(container, password))
        expanded += 1
        if code:
            # 7-Zip's warning code: something did not come out, and this archive is the only
            # copy of it. A container left in the tree beats an entry lost from it.
            print(f"kept '{os.path.basename(container)}' in the tree: 7z reported warnings")
            continue

        retire_container(container, root, containers_dir)
    return expanded


def run_extract(source: str, out_dir: str, *, password=None, private_key=None,
                cms_backend=None, work_dir=None) -> str:
    seven = sevenzip.find_7z()
    if not seven:
        raise RuntimeError("7z.exe not found")

    # The CMS output is archive-sized and lives only until it is replayed, so it stays scratch
    # - pointed at 'work_dir', because the temp volume is where the room runs out. Set before
    # anything takes a scratch directory.
    scratch.use(work_dir)

    os.makedirs(out_dir, exist_ok=True)
    collection = collection_dir(source)

    # The collection's own files - the Outcome, the Outlines, WolfLauncher's log - are in no
    # archive, so an extracted tree only holds them if they are put there: copied when ORC
    # left them in the clear, decrypted straight into the tree when it sealed them. Copying
    # first means unseal_sidecars() finds the file already in place and does not decrypt a
    # second time what is already readable.
    outcome = find_outcome_json(collection)
    copy_into_run([outcome] if outcome else [], out_dir)
    capture_outlines([collection], out_dir)
    capture_sidecar_logs([collection], out_dir)
    unseal_sidecars(source, seven, out_dir, private_key, cms_backend)

    # 'work_dir' is a keep dir, as it is for an ingest: the decrypted archive lands in it under
    # a stable name and the next extract of the same collection reuses it rather than paying
    # for the decrypt again. Without one the archive is scratch, gone when the process ends,
    # because the expanded tree supersedes it. Either way nothing is written to the collection:
    # an extract must leave what it reads exactly as it found it.
    work = work_dir or scratch.directory("extract_")
    for top in resolve_archives(source, out_dir):
        if not os.path.exists(top):
            # A partial collection: an archive the Outcome names was never written, or the
            # transfer dropped it. The other archives are independent deliverables, so say
            # which one is gone and keep going rather than losing them to the first gap.
            print(f"skipped missing archive '{top}'")
            continue
        if os.path.getsize(top) == 0:
            print(f"skipped empty archive '{top}'")
            continue

        plain = decrypt_archive(top, work, private_key, cms_backend)
        destination = os.path.join(out_dir, layout.archive_stem(top))
        os.makedirs(destination, exist_ok=True)
        sevenzip.extract_all(seven, plain, destination, password)
        count = expand_containers(seven, destination, password,
                                  os.path.join(work, layout.archive_stem(top)))
        print(f"extracted '{os.path.basename(top)}' -> {destination} "
              f"({count} nested container(s))")

    return out_dir
