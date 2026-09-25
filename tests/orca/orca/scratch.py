"""Per-process scratch space: '%TEMP%/orca/<pid>', removed when the process ends.

Everything orca writes that it does not intend to keep goes here - nested containers pulled out
of a top archive, the CSV a job is converting, the CMS/journal intermediates. One root per pid
so two orca runs never share a directory, and so a killed run leaves one obvious directory to
delete rather than a scatter of 'orca_*' temporaries.
"""
import atexit
import os
import shutil
import tempfile

_root: str | None = None


def root() -> str:
    """The process's scratch root, created on first use and removed at exit."""
    global _root
    if _root is None:
        _root = os.path.join(tempfile.gettempdir(), "orca", str(os.getpid()))
        os.makedirs(_root, exist_ok=True)
        atexit.register(cleanup)
    return _root


def directory(prefix: str) -> str:
    """A fresh scratch directory inside the root."""
    return tempfile.mkdtemp(prefix=prefix, dir=root())


def cleanup() -> None:
    """Remove the whole scratch root. Safe to call more than once."""
    global _root
    if _root is not None:
        shutil.rmtree(_root, ignore_errors=True)
        _root = None
