"""Compute/IO engine seam. DuckDbEngine shells to duckdb.exe; results never via -json."""
import csv as _csv
import os
import shutil
import subprocess
import tempfile

from . import columns as _columns


def find_duckdb() -> str | None:
    found = shutil.which("duckdb")
    if found:
        return found
    for cand in (os.path.expanduser(r"~\.home\bin\duckdb.exe"),
                 r"C:\Program Files\DuckDB\duckdb.exe",
                 r"C:\tools\duckdb\duckdb.exe"):
        if os.path.exists(cand):
            return cand
    return None


def _memory_bytes(field: str) -> int | None:
    """Physical memory, total or available, or None when it cannot be determined."""
    if os.name == "nt":
        import ctypes
        from ctypes import wintypes

        class MemoryStatus(ctypes.Structure):
            _fields_ = [("dwLength", wintypes.DWORD), ("dwMemoryLoad", wintypes.DWORD),
                        ("ullTotalPhys", ctypes.c_ulonglong),
                        ("ullAvailPhys", ctypes.c_ulonglong),
                        ("ullTotalPageFile", ctypes.c_ulonglong),
                        ("ullAvailPageFile", ctypes.c_ulonglong),
                        ("ullTotalVirtual", ctypes.c_ulonglong),
                        ("ullAvailVirtual", ctypes.c_ulonglong),
                        ("ullAvailExtendedVirtual", ctypes.c_ulonglong)]

        status = MemoryStatus()
        status.dwLength = ctypes.sizeof(MemoryStatus)
        if not ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(status)):
            return None
        return int(getattr(status, "ullTotalPhys" if field == "total" else "ullAvailPhys"))

    try:
        pages = "SC_PHYS_PAGES" if field == "total" else "SC_AVPHYS_PAGES"
        return os.sysconf(pages) * os.sysconf("SC_PAGE_SIZE")
    except (ValueError, OSError, AttributeError):
        return None


def total_memory_bytes() -> int | None:
    return _memory_bytes("total")


def available_memory_bytes() -> int | None:
    return _memory_bytes("available")


# Left for the rest of the machine when a command has duckdb to itself.
SINGLE_PROCESS_RESERVE = 2 << 30


def single_process_memory_limit() -> str | None:
    """Budget for a command running one duckdb at a time (merge, diff, report).

    Derived from AVAILABLE memory, not total, and never left to duckdb's own default. That
    default is 80% of total RAM, which promises memory the host may not actually grant: on a
    dynamic-memory VM the reported total moves, and the merge of a real 28M row collection died
    with a bare 'Out of Memory Error: Allocation failure' - the OS refusing - rather than a
    clean limit. The same merge completes in about 3 minutes inside a budget taken from what is
    actually free.
    """
    available = available_memory_bytes() or total_memory_bytes()
    if not available:
        return None

    return f"{max((available - SINGLE_PROCESS_RESERVE) // (1 << 20), 512)}MB"


def worker_memory_limit(workers: int) -> str | None:
    """A per-process duckdb memory limit for 'workers' concurrent duckdb.exe, or None when the
    machine's RAM is unknown.

    Each duckdb process otherwise assumes it owns the machine - its default limit is 80% of
    physical RAM - so several of them materializing multi-GB tables at once exhaust memory and
    fail with 'Out of Memory Error: Allocation failure' even though any one of them would have
    succeeded. Splitting the budget, with a temp directory to spill into, keeps the parallel
    path working on real collections.
    """
    total = total_memory_bytes()
    if not total or workers < 1:
        return None

    # The extra share leaves headroom for the rest of the machine (and for 7z).
    per_worker_mb = total // (workers + 1) // (1 << 20)
    return f"{max(per_worker_mb, 512)}MB"


# What one duckdb thread needs to get a diff-sized EXCEPT ALL through. DuckDB documents 125MB
# a thread as the bare minimum and 1-4GB as the working range; measured against duckdb 1.5.5 on
# a 666MB, 16 column parquet pair, 512MB a thread passed at every width tried (512/1, 1024/2,
# 2048/4, 3072/6) and 384 a thread still died, so the low end of the documented range it is.
MEMORY_PER_THREAD_MB = 512


def _megabytes(memory_limit: str | None) -> int | None:
    """The 'NNNMB' budgets this module produces, as a number."""
    if not memory_limit or not memory_limit.endswith("MB"):
        return None

    try:
        return int(memory_limit[:-2])
    except ValueError:
        return None


def thread_limit(memory_limit: str | None) -> int | None:
    """Threads a duckdb process may use under 'memory_limit', or None when it is unconstrained.

    DuckDB runs one thread per core whatever its budget is, and the threads share that budget:
    the 512MB floor spread over 16 cores leaves 32MB a thread, nowhere near enough to spill, and
    a cross-version run diff dies on 'failed to allocate data of size 1.0 MiB (487.7 MiB/488.2
    MiB used)' with a spill directory sitting right there. Fewer, fatter threads turn that query
    from fatal into merely slow: the same failing query ran in 17s on one thread.
    """
    megabytes = _megabytes(memory_limit)
    if megabytes is None:
        return None

    return max(1, min(megabytes // MEMORY_PER_THREAD_MB, os.cpu_count() or 1))


def sql_path(path: str) -> str:
    """A path ready to sit inside a single-quoted SQL literal.

    Forward slashes for DuckDB, and single quotes doubled: every statement embeds paths as
    string literals, so one apostrophe in the path ends the literal and breaks the statement.
    A Windows profile called 'O'Brien' is enough to do it, and the error that comes back names
    the fragment after the quote, not the path.
    """
    return path.replace("\\", "/").replace("'", "''")


# Short alias: this appears inside almost every f-string in the module.
_fwd = sql_path


def _reader(path: str) -> str:
    return "read_parquet" if path.lower().endswith(".parquet") else "read_csv_auto"


class Engine:
    def columns(self, path: str) -> list[str]:
        raise NotImplementedError

    def count(self, path: str) -> int:
        raise NotImplementedError

    def csv_to_parquet(self, csv_path: str, parquet_path: str, drop_columns=None,
                       extra_columns=None) -> None:
        raise NotImplementedError

    def parquet_to_csv(self, parquet_path: str, csv_path: str, drop_columns=None) -> None:
        raise NotImplementedError

    def query_to_parquet(self, select_sql: str, parquet_path: str) -> None:
        raise NotImplementedError

    def query_scalar(self, select_sql: str, column: str = "n") -> str:
        raise NotImplementedError

    def query_to_csv(self, select_sql: str, csv_path: str) -> None:
        raise NotImplementedError

    def query_rows(self, select_sql: str, limit: int) -> list:
        raise NotImplementedError


class DuckDbEngine(Engine):
    def __init__(self, exe: str, memory_limit: str | None = None):
        self.exe = exe
        self.memory_limit = memory_limit
        # LOCAL disk, never the (SMB) working directory: a large spill onto a share fails with
        # WinError 1450, insufficient system resources.
        self.temp_directory = os.path.join(tempfile.gettempdir(), "orca_duckdb_tmp")

    def _settings(self) -> str:
        """Prologue every statement runs with: somewhere to spill, a memory budget that accounts
        for the other duckdb processes orca may be running alongside this one, and a thread count
        that budget can feed."""
        settings = f"SET temp_directory='{_fwd(self.temp_directory)}'; "
        if self.memory_limit:
            settings += f"SET memory_limit='{self.memory_limit}'; "
            threads = thread_limit(self.memory_limit)
            if threads:
                settings += f"SET threads={threads}; "
        return settings

    def _run(self, sql: str) -> None:
        # Feed SQL via stdin, not `-c`: the generated merge SQL can exceed the Windows
        # command-line length limit (WinError 206). Stdin still yields proper exit codes.
        proc = subprocess.run([self.exe], input=self._settings() + sql,
                              capture_output=True, text=True)
        if proc.returncode != 0:
            raise RuntimeError(f"duckdb failed: {proc.stderr.strip()}\nSQL: {sql[:500]}")

    def _scalar_csv(self, sql_select: str, column: str) -> list[str]:
        # Run a SELECT, capture rows via COPY TO a temp CSV (no -json), read column.
        fd, tmp = tempfile.mkstemp(suffix=".csv")
        os.close(fd)
        try:
            self._run(f"COPY ({sql_select}) TO '{_fwd(tmp)}' (HEADER, DELIMITER ',');")
            with open(tmp, newline="", encoding="utf-8", errors="replace") as f:
                return [row[column] for row in _csv.DictReader(f)]
        finally:
            os.remove(tmp)

    def columns(self, path: str) -> list[str]:
        sql = f"SELECT column_name FROM (DESCRIBE SELECT * FROM {_reader(path)}('{_fwd(path)}'))"
        return self._scalar_csv(sql, "column_name")

    def count(self, path: str) -> int:
        vals = self._scalar_csv(f"SELECT count(*) AS n FROM {_reader(path)}('{_fwd(path)}')", "n")
        return int(vals[0]) if vals else 0

    def _select_list(self, path: str, drop_columns) -> str:
        if not drop_columns:
            return "*"
        kept = _columns.kept_columns(self.columns(path), drop_columns)
        return _columns.quote_columns(kept)

    def csv_to_parquet(self, csv_path: str, parquet_path: str, drop_columns=None,
                       extra_columns=None) -> None:
        sel = self._select_list(csv_path, drop_columns)
        extra = self._extra_terms(csv_path, extra_columns)
        # all_varchar: read every column as text. ORC's forensic CSVs contain malformed cells
        # (e.g. a truncated timestamp from a forged $I30 record) that abort type auto-detection
        # deep into a multi-million-row file. Text is also what merge/diff compare on, and it
        # keeps the parquet schema stable across runs (auto-detected types otherwise flip between
        # TIMESTAMP and VARCHAR depending on the sample, producing spurious schema diffs).
        self._run(f"COPY (SELECT {sel}{extra} FROM read_csv_auto('{_fwd(csv_path)}', "
                  f"all_varchar=true)) TO '{_fwd(parquet_path)}' (FORMAT parquet);")

    def _extra_terms(self, csv_path: str, extra_columns) -> str:
        """Constant columns appended to a select, as ', <value> AS <name>' terms.

        A column the file already carries is skipped rather than added: duckdb would write a
        parquet with two columns of that name, which nothing can read back. ORC emits its own
        unprefixed 'ComputerName' in some configurations, so the collision is not theoretical.
        The look-up is one more duckdb DESCRIBE per table; accepted, the spec rules performance
        out of scope for this change.
        """
        if not extra_columns:
            return ""

        have = {c.lower() for c in self.columns(csv_path)}
        terms = []
        for name, value in extra_columns.items():
            if name.lower() in have:
                continue
            quoted = str(value).replace("'", "''")
            terms.append(f", '{quoted}' AS \"{name}\"")
        return "".join(terms)

    def parquet_to_csv(self, parquet_path: str, csv_path: str, drop_columns=None) -> None:
        sel = self._select_list(parquet_path, drop_columns)
        self._run(f"COPY (SELECT {sel} FROM read_parquet('{_fwd(parquet_path)}')) "
                  f"TO '{_fwd(csv_path)}' (HEADER, DELIMITER ',');")

    def query_to_parquet(self, select_sql: str, parquet_path: str) -> None:
        # Dropping insertion order cuts peak memory for the merge's grouped aggregation.
        #
        # No memory cap of its own: this path used to pin 6GB, tuned on small data, and that
        # killed the merge of a real 28M row collection ("could not allocate block of size
        # 4.0 KiB (5.5 GiB/5.5 GiB used)") on a machine with 18GB free to use. The same merge
        # succeeds in 168s with no cap. Whatever budget applies comes from _settings(), which
        # is set when several duckdb processes share the machine and left to duckdb's own
        # default (80% of RAM) when one command has it to itself.
        self._run("SET preserve_insertion_order=false; "
                  f"COPY ({select_sql}) TO '{_fwd(parquet_path)}' (FORMAT parquet);")

    def query_scalar(self, select_sql: str, column: str = "n") -> str:
        vals = self._scalar_csv(select_sql, column)
        return vals[0] if vals else ""

    def query_to_csv(self, select_sql: str, csv_path: str) -> None:
        self._run(f"COPY ({select_sql}) TO '{_fwd(csv_path)}' (HEADER, DELIMITER ',');")

    def query_rows(self, select_sql: str, limit: int) -> list:
        # Materialize up to 'limit' rows as ordered dicts (via a temp CSV, no -json) for inline
        # preview of a diff.
        fd, tmp = tempfile.mkstemp(suffix=".csv")
        os.close(fd)
        try:
            self._run(f"COPY (SELECT * FROM ({select_sql}) LIMIT {int(limit)}) "
                      f"TO '{_fwd(tmp)}' (HEADER, DELIMITER ',');")
            with open(tmp, newline="", encoding="utf-8", errors="replace") as f:
                return list(_csv.DictReader(f))
        finally:
            os.remove(tmp)


def rows_to_parquet(rows: list[dict], out_path: str, engine) -> None:
    """Materialize plain dict rows as parquet, via a scratch CSV so every column lands as text.

    The same route the manifest takes. Text keeps schemas stable across ORC versions and leaves
    embedded JSON intact; cast in the query when you need a number.
    """
    if not rows:
        raise RuntimeError("no rows to write")

    csv_path = os.path.splitext(out_path)[0] + ".rows.csv"
    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    with open(csv_path, "w", newline="", encoding="utf-8") as f:
        writer = _csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)

    try:
        engine.csv_to_parquet(csv_path, out_path)
    finally:
        os.remove(csv_path)


def get_engine(name: str, memory_limit: str | None = None) -> Engine:
    if name == "duckdb":
        exe = find_duckdb()
        if not exe:
            raise RuntimeError("duckdb.exe not found (PATH, ~/.home/bin, Program Files, c:\\tools)")
        return DuckDbEngine(exe, memory_limit)
    raise ValueError(f"unknown engine: {name}")
