import os
import tempfile
import unittest
from unittest import mock
from orca import engine
from orca.engine import DuckDbEngine


class TestExtraColumns(unittest.TestCase):
    def test_constants_are_appended_to_the_select(self):
        engine = DuckDbEngine("duckdb.exe")
        with mock.patch.object(engine, "_run") as run:
            engine.csv_to_parquet("in.csv", "out.parquet",
                                  extra_columns={"OrcComputerName": "a.lab"})
        sql = run.call_args[0][0]
        self.assertIn("'a.lab' AS \"OrcComputerName\"", sql)
        self.assertIn("read_csv_auto", sql)

    def test_a_quote_in_a_value_is_escaped(self):
        engine = DuckDbEngine("duckdb.exe")
        with mock.patch.object(engine, "_run") as run:
            engine.csv_to_parquet("in.csv", "out.parquet",
                                  extra_columns={"OrcComputerName": "o'brien.lab"})
        self.assertIn("'o''brien.lab'", run.call_args[0][0])

    def test_a_column_the_csv_already_has_is_not_stamped_twice(self):
        """Review Focus 5: a duplicate column name would make the parquet unreadable."""
        engine = DuckDbEngine("duckdb.exe")
        with mock.patch.object(engine, "columns", return_value=["OrcComputerName", "Path"]), \
                mock.patch.object(engine, "_run") as run:
            engine.csv_to_parquet("in.csv", "out.parquet",
                                  extra_columns={"OrcComputerName": "a.lab"})
        sql = run.call_args[0][0]
        # The select is '*' here, so the only way the name could appear is as a stamp.
        self.assertNotIn('AS "OrcComputerName"', sql)
        self.assertIn("SELECT * FROM read_csv_auto", sql)

DUCKDB = engine.find_duckdb()


class TestSqlPath(unittest.TestCase):
    """Every statement embeds paths as single-quoted literals, so one apostrophe in the path
    ends the literal: ingesting from 'C:\\Users\\O'Brien\\...' failed with
    'Parser Error: syntax error at or near "brien"'."""

    def test_doubles_single_quotes(self):
        self.assertEqual(engine.sql_path("C:\\Users\\O'Brien\\run"), "C:/Users/O''Brien/run")

    def test_forward_slashes(self):
        self.assertEqual(engine.sql_path("C:\\runs\\host"), "C:/runs/host")

    def test_leaves_an_ordinary_path_alone(self):
        self.assertEqual(engine.sql_path("C:/runs/host.parquet"), "C:/runs/host.parquet")

    def test_every_statement_builder_escapes(self):
        quoted = "C:\\O'B\\x.parquet"
        eng = engine.DuckDbEngine("duckdb")
        statements = []
        eng._run = lambda sql: statements.append(sql)

        eng.csv_to_parquet(quoted, quoted)
        eng.parquet_to_csv(quoted, quoted)
        eng.query_to_csv("SELECT 1", quoted)
        for sql in statements:
            self.assertIn("O''B", sql)
            self.assertNotIn("O'B\\", sql)
            self.assertNotIn("'C:/O'B", sql)


class TestMemoryBudget(unittest.TestCase):
    """Each ingest job runs its own duckdb.exe, and each one assumes it owns the machine
    (default limit: 80% of RAM). Without a share-out, six of them materializing multi-GB
    tables at once die with 'Out of Memory Error: Allocation failure'."""

    def test_splits_ram_between_the_workers(self):
        with mock.patch.object(engine, "total_memory_bytes", lambda: 16 << 30):
            # 16GB over 6 workers plus a share of headroom: 16384 / 7 = 2340MB each.
            self.assertEqual(engine.worker_memory_limit(6), "2340MB")

    def test_never_goes_below_a_usable_floor(self):
        with mock.patch.object(engine, "total_memory_bytes", lambda: 1 << 30):
            self.assertEqual(engine.worker_memory_limit(64), "512MB")

    def test_no_limit_when_ram_is_unknown(self):
        with mock.patch.object(engine, "total_memory_bytes", lambda: None):
            self.assertIsNone(engine.worker_memory_limit(4))

    def test_real_machine_reports_some_memory(self):
        self.assertGreater(engine.total_memory_bytes(), 1 << 28)
        self.assertGreater(engine.available_memory_bytes(), 0)

    def test_single_process_budget_comes_from_available_memory(self):
        # Not from total: duckdb's own default is 80% of total, which on a dynamic-memory VM
        # promises memory the host has not granted and fails the allocation outright.
        with mock.patch.object(engine, "available_memory_bytes", lambda: 15 << 30), \
             mock.patch.object(engine, "total_memory_bytes", lambda: 64 << 30):
            self.assertEqual(engine.single_process_memory_limit(), "13312MB")

    def test_single_process_budget_falls_back_to_total(self):
        with mock.patch.object(engine, "available_memory_bytes", lambda: None), \
             mock.patch.object(engine, "total_memory_bytes", lambda: 8 << 30):
            self.assertEqual(engine.single_process_memory_limit(), "6144MB")

    def test_single_process_budget_has_a_floor(self):
        with mock.patch.object(engine, "available_memory_bytes", lambda: 1 << 20), \
             mock.patch.object(engine, "total_memory_bytes", lambda: 1 << 20):
            self.assertEqual(engine.single_process_memory_limit(), "512MB")

    def test_no_single_process_budget_when_memory_is_unknown(self):
        with mock.patch.object(engine, "available_memory_bytes", lambda: None), \
             mock.patch.object(engine, "total_memory_bytes", lambda: None):
            self.assertIsNone(engine.single_process_memory_limit())

    def test_threads_follow_the_budget(self):
        """duckdb runs one thread per core whatever its budget and the threads share that
        budget. The 512MB floor over 16 cores left 32MB a thread and killed a real cross-version
        diff at 'failed to allocate data of size 1.0 MiB'; the same query passes on one thread."""
        with mock.patch.object(engine.os, "cpu_count", lambda: 16):
            self.assertEqual(engine.thread_limit("512MB"), 1)
            self.assertEqual(engine.thread_limit("2048MB"), 4)
            # A budget wider than the machine still only gets the cores there are.
            self.assertEqual(engine.thread_limit("13312MB"), 16)
            # And never zero threads, however small the budget.
            self.assertEqual(engine.thread_limit("100MB"), 1)

    def test_no_thread_cap_without_a_budget(self):
        self.assertIsNone(engine.thread_limit(None))
        self.assertNotIn("threads", engine.DuckDbEngine("duckdb")._settings())

    def test_the_thread_cap_rides_with_the_budget(self):
        with mock.patch.object(engine.os, "cpu_count", lambda: 16):
            settings = engine.DuckDbEngine("duckdb", "2048MB")._settings()

        self.assertIn("memory_limit='2048MB'", settings)
        self.assertIn("SET threads=4;", settings)

    def test_every_statement_gets_a_spill_directory_and_the_budget(self):
        eng = engine.DuckDbEngine("duckdb", "2340MB")
        settings = eng._settings()
        # Spilling must go to local disk: a large spill onto an SMB working directory fails
        # with WinError 1450.
        self.assertIn(eng.temp_directory.replace("\\", "/"), settings)
        self.assertIn("memory_limit='2340MB'", settings)

    def test_no_memory_limit_statement_when_unconstrained(self):
        self.assertNotIn("memory_limit", engine.DuckDbEngine("duckdb")._settings())

    def test_merge_query_pins_no_cap_of_its_own(self):
        """The merge's wide aggregation must get the whole budget the engine was given.

        A 6GB cap tuned on small data used to live here and killed the merge of a real 28M row
        collection on an 18GB machine; uncapped, the same merge takes 168s.
        """
        statements = []
        eng = engine.DuckDbEngine("duckdb")
        eng._run = lambda sql: statements.append(sql)
        eng.query_to_parquet("SELECT 1", "out.parquet")

        self.assertNotIn("memory_limit", statements[0])
        self.assertIn("preserve_insertion_order=false", statements[0])


@unittest.skipUnless(DUCKDB, "duckdb not available")
class TestCsvToParquet(unittest.TestCase):
    def test_malformed_timestamp_beyond_sample_does_not_crash(self):
        # ORC forensic CSVs carry malformed cells (here a truncated timestamp from a corrupt
        # $I30 record). With type auto-detection, >20480 clean rows make DuckDB pick TIMESTAMP,
        # then a bad value deeper in the file aborts the whole ingest. all_varchar reads text
        # and preserves the raw value.
        eng = engine.DuckDbEngine(DUCKDB)
        with tempfile.TemporaryDirectory() as d:
            csv = os.path.join(d, "i30.csv")
            with open(csv, "w", newline="") as f:
                f.write("FRN,FileNameCreationDate\n")
                for i in range(21000):  # past the 20480 auto-detect sample
                    f.write(f"{i},2022-02-01 03:04:05.000\n")
                f.write("999999,2022-02-01 0\n")  # truncated -> not a valid TIMESTAMP

            parquet = os.path.join(d, "i30.parquet")
            eng.csv_to_parquet(csv, parquet, None)  # must not raise
            self.assertEqual(eng.count(parquet), 21001)
            self.assertIn("FileNameCreationDate", eng.columns(parquet))

    def test_query_rows_limits_and_returns_dicts(self):
        eng = engine.DuckDbEngine(DUCKDB)
        rows = eng.query_rows("SELECT * FROM (VALUES (1,'a'),(2,'b'),(3,'c')) t(id,name)", 2)
        self.assertEqual(len(rows), 2)
        self.assertIn("id", rows[0])
        self.assertIn("name", rows[0])

    def test_query_rows_reads_non_ascii(self):
        # chr(129) = U+0081, the byte (0x81) whose cp1252 decode raised UnicodeDecodeError on
        # Windows. Reading duckdb's UTF-8 output must be encoding-explicit, not locale-dependent.
        eng = engine.DuckDbEngine(DUCKDB)
        rows = eng.query_rows("SELECT * FROM (VALUES ('plain'), (chr(129)), (chr(233))) t(name)", 5)
        self.assertEqual(len(rows), 3)


if __name__ == "__main__":
    unittest.main()
