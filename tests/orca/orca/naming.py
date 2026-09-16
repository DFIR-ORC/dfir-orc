"""Pure output-name rules: stem + parent-keyword disambiguation, globally unique."""
import os
import re
from collections import Counter


def sanitize_keyword(text: str) -> str:
    # Keep underscores so ORC command names stay faithful (Browsers_complet, EXE_TMP,
    # NTFSInfo_detail) instead of being mangled to Browserscomplet / EXETMP.
    return re.sub(r"[^A-Za-z0-9_]", "", text)


def _segments(entry_path: str) -> list[str]:
    return [s for s in re.split(r"[\\/]", entry_path) if s]


def base_name(entry_path: str) -> str:
    segs = _segments(entry_path)
    leaf = segs[-1] if segs else entry_path
    return os.path.splitext(leaf)[0]


def path_keyword(entry_path: str) -> str:
    segs = _segments(entry_path)
    if len(segs) < 2:
        return ""
    return sanitize_keyword(segs[-2])


def assign_names(entry_paths: list[str]) -> list[str]:
    bases = [base_name(p) for p in entry_paths]
    base_counts = Counter(bases)

    proposed = []
    for path, base in zip(entry_paths, bases):
        if base_counts[base] == 1:
            proposed.append(base)
            continue
        keyword = path_keyword(path)
        proposed.append(f"{base}_{keyword}" if keyword else base)

    # Guarantee global uniqueness; numeric counter is the last resort.
    used: dict[str, int] = {}
    final = []
    for name in proposed:
        if name not in used:
            used[name] = 1
            final.append(name)
        else:
            used[name] += 1
            final.append(f"{name}_{used[name]}")
    return final
