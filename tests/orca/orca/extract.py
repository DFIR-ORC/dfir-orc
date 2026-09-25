"""orca extract: expand ORC results to a directory tree, decrypting and unnesting on the way.

The successor to the PowerShell Expand-OrcArchive / Expand-OrcResults. Unlike 'ingest', this
writes the whole collected tree, so it is for the times you genuinely want the files on disk;
prefer 'ingest' when you only want to query the data.
"""
import os
import shutil

from . import layout, sevenzip
from .ingest import decrypt_archive, find_outcome_json, resolve_archives


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


def expand_containers(seven_zip: str, root: str, password: str | None) -> int:
    """Expand each nested container beside itself, into a directory named after it."""
    expanded = 0
    for container in nested_containers(root):
        if os.path.getsize(container) == 0:
            print(f"skipped empty container '{container}'")
            continue

        destination = os.path.splitext(container)[0]
        os.makedirs(destination, exist_ok=True)
        sevenzip.extract_all(seven_zip, container, destination,
                             container_password(container, password))
        expanded += 1
    return expanded


def run_extract(source: str, out_dir: str, *, password=None, private_key=None,
                cms_backend=None) -> str:
    seven = sevenzip.find_7z()
    if not seven:
        raise RuntimeError("7z.exe not found")

    os.makedirs(out_dir, exist_ok=True)
    outcome = find_outcome_json(source)
    if outcome:
        shutil.copy(outcome, os.path.join(out_dir, os.path.basename(outcome)))

    # A decrypted '.p7b' is a full-size intermediate: it is kept beside the archive it came
    # from, not dropped into the result tree.
    keep_dir, _ = layout.resolve(source)
    for top in resolve_archives(source):
        if not os.path.exists(top):
            # A partial collection: an archive the Outcome names was never written, or the
            # transfer dropped it. The other archives are independent deliverables, so say
            # which one is gone and keep going rather than losing them to the first gap.
            print(f"skipped missing archive '{top}'")
            continue
        if os.path.getsize(top) == 0:
            print(f"skipped empty archive '{top}'")
            continue

        plain = decrypt_archive(top, keep_dir, private_key, cms_backend)
        destination = os.path.join(out_dir, layout.archive_stem(top))
        os.makedirs(destination, exist_ok=True)
        sevenzip.extract_all(seven, plain, destination, password)
        count = expand_containers(seven, destination, password)
        print(f"extracted '{os.path.basename(top)}' -> {destination} "
              f"({count} nested container(s))")

    return out_dir
