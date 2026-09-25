"""orca extract: expand ORC results to a directory tree, decrypting and unnesting on the way.

The successor to the PowerShell Expand-OrcArchive / Expand-OrcResults. Unlike 'ingest', this
writes the whole collected tree, so it is for the times you genuinely want the files on disk;
prefer 'ingest' when you only want to query the data.
"""
import os
import shutil
from typing import NamedTuple

from . import layout, scratch, sevenzip
from .ingest import (capture_outlines, capture_sidecar_logs, collection_dir, copy_into_run,
                     decrypt_archive, find_outcome_json, has_encryption_layer,
                     resolve_archives, unseal_sidecars)


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


def expand_one_container(seven_zip: str, container: str, root: str, password: str | None,
                         containers_dir: str) -> bool:
    """Expand one container beside itself. True when it was opened at all."""
    if os.path.getsize(container) == 0:
        print(f"skipped empty container '{container}'")
        return False

    destination = os.path.splitext(container)[0]
    os.makedirs(destination, exist_ok=True)
    code = sevenzip.extract_all(seven_zip, container, destination,
                                container_password(container, password))
    if code:
        # 7-Zip's warning code: something did not come out, and this archive is the only copy
        # of it. A container left in the tree beats an entry lost from it.
        print(f"kept '{os.path.basename(container)}' in the tree: 7z reported warnings")
        return True

    retire_container(container, root, containers_dir)
    return True


def expand_containers(seven_zip: str, root: str, password: str | None, containers_dir: str,
                      depth: int | None = None) -> int:
    """Expand nested containers level by level, until none are left or 'depth' levels are done.

    Level by level rather than one pass over the containers found at the start: expanding one
    can reveal another, and a single pass would leave that one sitting there as an archive
    with nothing to say it was never opened.

    A container 7-Zip warned about stays in the tree, so the same paths come back on the next
    level - hence 'tried', without which the loop would never end.
    """
    expanded = 0
    tried: set[str] = set()
    level = 0
    while depth is None or level < depth:
        containers = [path for path in nested_containers(root) if path not in tried]
        if not containers:
            break

        for container in containers:
            tried.add(container)
            if expand_one_container(seven_zip, container, root, password, containers_dir):
                expanded += 1
        level += 1

    return expanded


def deliver_archive(plain: str, out_dir: str, keep: bool) -> str:
    """Put the archive an extract stopped at into the output directory.

    Copied rather than moved when a work dir holds it, so a later run still finds it there and
    skips the decrypt; moved when it is only scratch and would be thrown away anyway.
    """
    target = os.path.join(out_dir, os.path.basename(plain))
    if os.path.exists(target):
        if os.path.samefile(plain, target):
            return target

        # Explicitly, rather than leaving it to move()/copy2(): overwriting an existing file
        # is a rename on posix and a failure-then-fallback on Windows, and a '--refresh' has
        # to behave the same on both.
        os.remove(target)

    if keep:
        shutil.copy2(plain, target)
    else:
        shutil.move(plain, target)

    return target


class Unwrap(NamedTuple):
    """How to open the containers of one collection, and how far down to go."""
    password: str | None = None
    private_key: str | None = None
    cms_backend: str | None = None
    depth: int | None = None
    refresh: bool = False


def unwrap_archive(seven_zip: str, top: str, out_dir: str, work: str, how: Unwrap,
                   keep_work: bool) -> None:
    """Unwrap one top archive into 'out_dir', stopping after 'how.depth' layers (None: all).

    A layer is one container opened: the '.p7b' envelope, then the archive it holds, then each
    level of nested containers. Whatever the last layer produced is the product - the archive
    itself when the extract stops right after the decrypt.
    """
    remaining = how.depth
    plain = top
    if has_encryption_layer(top):
        plain = decrypt_archive(top, work, how.private_key, how.cms_backend, how.refresh)
        if remaining is not None:
            remaining -= 1
            if remaining == 0:
                target = deliver_archive(plain, out_dir, keep_work)
                print(f"decrypted '{os.path.basename(top)}' -> {target}")
                return

    destination = os.path.join(out_dir, layout.archive_stem(top))
    os.makedirs(destination, exist_ok=True)
    sevenzip.extract_all(seven_zip, plain, destination, how.password)
    if remaining is not None:
        remaining -= 1

    count = expand_containers(seven_zip, destination, how.password,
                              os.path.join(work, layout.archive_stem(top)), remaining)
    print(f"extracted '{os.path.basename(top)}' -> {destination} "
          f"({count} nested container(s))")


def run_extract(source: str, out_dir: str, *, password=None, private_key=None,
                cms_backend=None, work_dir=None, depth=None, refresh=False) -> str:
    seven = sevenzip.find_7z()
    if not seven:
        raise RuntimeError("7z.exe not found")

    if depth is not None and depth < 1:
        raise RuntimeError(f"extract: --depth is a number of layers to unwrap, so it starts "
                           f"at 1 (the '.p7b' envelope); got {depth}")

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
    unseal_sidecars(source, seven, out_dir, private_key, cms_backend, refresh)

    # 'work_dir' is a keep dir, as it is for an ingest: the decrypted archive lands in it under
    # a stable name and the next extract of the same collection reuses it rather than paying
    # for the decrypt again. Without one the archive is scratch, gone when the process ends,
    # because the expanded tree supersedes it. Either way nothing is written to the collection:
    # an extract must leave what it reads exactly as it found it.
    work = work_dir or scratch.directory("extract_")
    how = Unwrap(password, private_key, cms_backend, depth, refresh)
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

        unwrap_archive(seven, top, out_dir, work, how, bool(work_dir))

    return out_dir
