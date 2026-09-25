"""Per-process scratch space: '<base>/orca/<pid>', removed when the process ends.

Everything orca writes that it does not intend to keep goes here - nested containers pulled out
of a top archive, the CSV a job is converting, the CMS/journal intermediates. One root per pid
so two orca runs never share a directory, and so a killed run leaves one obvious directory to
delete rather than a scatter of 'orca_*' temporaries.

The base is the system temp dir unless use() moves it. Everything here is derived from the
root, so moving it moves every intermediate at once: threading a directory through each call
site instead means the next one added quietly keeps writing to the temp volume.

What may be removed is decided by an index, never by what a directory is called. Each run
writes '<temp>/orca/<pid>-<unique>.index' naming the root it created and every directory it
creates inside it, and removal - its own at exit, another run's leftovers later - only ever
touches what an index names. The index stays in the system temp dir even when use() puts the
scratch somewhere else, so a run killed under '--work-dir E:/big' is cleaned up by a later run
that has never heard of E:/big.
"""
import atexit
import os
import shutil
import tempfile
import time

INDEX_MAGIC = "orca-scratch 1"
INDEX_SUFFIX = ".index"

# How long another run's index may sit there before it is taken for the leavings of a killed
# run. Long enough that no live run matches, short enough that a corpse is not kept for weeks.
STALE_SECONDS = 24 * 60 * 60

_base: str | None = None
_root: str | None = None
_index: str | None = None


def use(directory: str | None) -> None:
    """Put this process's scratch under 'directory' instead of the system temp dir.

    What '--work-dir' sets: a decrypted collection runs to hundreds of GB and a temp volume is
    often far smaller.

    A root already taken under another base is dropped and re-taken here, rather than left
    where it was: whether some earlier call happened to touch scratch first is not something a
    caller can reason about, and the quiet failure - a collection-sized intermediate still
    going to the volume with no room on it - is the one this flag exists to prevent. Scratch is
    disposable by definition, so no path is expected to survive a change of base.
    """
    global _base
    if directory == _base:
        return

    _base = directory
    cleanup()


def index_dir() -> str:
    """Where every run's index lives, whatever use() has been told."""
    return os.path.join(tempfile.gettempdir(), "orca")


def is_scratch_root(path: str) -> bool:
    """True for a path shaped like a root orca makes: '<base>/orca/<pid>'.

    A second lock on top of the index. Only orca writes an index, but it is a plain file in a
    shared temp directory and what it drives is an rmtree of a directory the user chose.
    """
    parent, name = os.path.split(os.path.normpath(path))
    return bool(name) and name.isdigit() and os.path.basename(parent) == "orca"


def read_index(path: str) -> tuple[str, list[str]] | None:
    """The root and the directory names an index holds, or None when it is not one of ours."""
    try:
        with open(path, encoding="utf-8") as f:
            lines = f.read().splitlines()
    except OSError:
        return None

    if len(lines) < 2 or lines[0] != INDEX_MAGIC or not is_scratch_root(lines[1]):
        return None

    return lines[1], [name for name in lines[2:] if name]


def remove_indexed(root: str, names: list[str]) -> None:
    """Remove what an index names: each directory it recorded, then the root itself.

    The per-name pass is what makes the index load-bearing rather than decorative - the root
    is removed too, but only because orca created that as well.
    """
    for name in names:
        shutil.rmtree(os.path.join(root, name), ignore_errors=True)

    shutil.rmtree(root, ignore_errors=True)


def sweep() -> None:
    """Remove the scratch of runs that ended without cleaning up after themselves.

    Driven by stale indexes, so a directory no index names is never touched however it is
    named. A stale index orca cannot parse has its own file removed and nothing else: it is
    orca's own litter, but it no longer says what it owns, so it gets no say.
    """
    directory = index_dir()
    cutoff = time.time() - STALE_SECONDS
    try:
        entries = sorted(os.listdir(directory))
    except OSError:
        return

    for entry in entries:
        path = os.path.join(directory, entry)
        if not entry.endswith(INDEX_SUFFIX) or path == _index:
            continue

        try:
            if os.path.getmtime(path) >= cutoff:
                continue
        except OSError:
            continue

        found = read_index(path)
        if found:
            remove_indexed(*found)

        try:
            os.remove(path)
        except OSError:
            print(f"could not remove the stale scratch index '{path}'")


def _start_index(root: str) -> str | None:
    """Open this run's index on the root it has just created."""
    directory = index_dir()
    try:
        os.makedirs(directory, exist_ok=True)
        handle, path = tempfile.mkstemp(prefix=f"{os.getpid()}-", suffix=INDEX_SUFFIX,
                                        dir=directory)
        with os.fdopen(handle, "w", encoding="utf-8") as f:
            f.write(f"{INDEX_MAGIC}\n{root}\n")
        return path
    except OSError as error:
        # Not fatal: this run still cleans up after itself from memory. What is lost is the
        # cleanup of this scratch by a LATER run, should this one be killed - so say it.
        print(f"could not write the scratch index in '{directory}': {error}")
        return None


def _record(name: str) -> None:
    """Add a directory to this run's index, once it exists.

    Created first, recorded second, so the index never names something that was not made.
    """
    if _index is None:
        return

    try:
        with open(_index, "a", encoding="utf-8") as f:
            f.write(name + "\n")
    except OSError as error:
        print(f"could not record '{name}' in the scratch index: {error}")


def root() -> str:
    """The process's scratch root, created on first use and removed at exit."""
    global _root, _index
    if _root is None:
        base = os.path.join(_base or tempfile.gettempdir(), "orca")
        _root = os.path.join(base, str(os.getpid()))
        os.makedirs(_root, exist_ok=True)
        _index = _start_index(_root)
        sweep()
        atexit.register(cleanup)
    return _root


def directory(prefix: str) -> str:
    """A fresh scratch directory inside the root."""
    made = tempfile.mkdtemp(prefix=prefix, dir=root())
    _record(os.path.basename(made))
    return made


def cleanup() -> None:
    """Remove what this run's index names, and the index. Safe to call more than once."""
    global _root, _index
    if _root is not None:
        found = read_index(_index) if _index else None
        remove_indexed(*(found or (_root, [])))
        _root = None

    if _index is not None:
        try:
            os.remove(_index)
        except OSError:
            pass
        _index = None
