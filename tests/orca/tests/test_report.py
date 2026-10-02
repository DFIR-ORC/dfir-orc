import csv
import os
import tempfile
import unittest
from orca import layout, report

TIMING = [
    {"name": "NTFSInfo", "rows": 100, "bytes": 5000, "seconds": 2.0},
    {"name": "Empty1", "rows": 0, "bytes": 10, "seconds": 0.1},
    {"name": "USNInfo", "rows": 50, "bytes": 9000, "seconds": 0.5},
]


class TestReportViews(unittest.TestCase):
    def test_build_empty_view(self):
        v = report.build_empty_view(TIMING)
        self.assertEqual(v["empty_tables"], ["Empty1"])
        self.assertEqual(v["count"], 1)
        self.assertEqual(v["empty_files"], [])

    def test_build_empty_view_reports_empty_output_files(self):
        v = report.build_empty_view(TIMING, ["b.log", "a.json"])
        self.assertEqual(v["empty_files"], ["a.json", "b.log"])  # sorted
        self.assertEqual(v["file_count"], 2)

    def test_empty_output_files_asks_only_for_orc_outputs(self):
        # Parity with Test-OrcExpandedResults: a zero-byte collected sample is ordinary and
        # must not be reported; only ORC's own outputs count.
        class FakeEngine:
            def __init__(self):
                self.sql = None

            def query_rows(self, sql, limit):
                self.sql = sql
                return [{"relpath": "Foo.log"}]

        with tempfile.TemporaryDirectory() as d:
            manifest = os.path.join(d, "manifest.parquet")
            with open(manifest, "wb") as f:
                f.write(b"")

            engine = FakeEngine()
            self.assertEqual(report.empty_output_files(engine, manifest), ["Foo.log"])

        self.assertIn("CAST(size AS BIGINT) = 0", engine.sql)
        for suffix in report.OUTPUT_SUFFIXES:
            self.assertIn(f"'%{suffix}'", engine.sql)

    def test_empty_output_files_without_a_manifest(self):
        self.assertEqual(report.empty_output_files(object(), "nope/manifest.parquet"), [])

    def test_build_stats_view(self):
        v = report.build_stats_view(TIMING)
        self.assertEqual(v["tables"], 3)
        self.assertEqual(v["total_rows"], 150)
        self.assertEqual(v["total_bytes"], 14010)
        self.assertEqual(v["biggest"][0]["name"], "USNInfo")
        self.assertEqual(v["slowest"][0]["name"], "NTFSInfo")

    def test_load_timing_reads_only_parquet_rows(self):
        with tempfile.TemporaryDirectory() as d:
            os.makedirs(layout.orca_dir(d))
            with open(layout.timing_path(d), "w", newline="") as f:
                w = csv.writer(f)
                w.writerow(["name", "kind", "rows", "bytes", "seconds"])
                w.writerow(["T", "parquet", "100", "5000", "2.0"])
                w.writerow(["T", "csv", "100", "8000", "0.3"])
            rows = report.load_timing(d)
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]["rows"], 100)
        self.assertEqual(rows[0]["bytes"], 5000)


class TestViewAvailability(unittest.TestCase):
    """A missing input must not read as a zero.

    'report' takes an ingested run dir, but it is pointed at an extracted tree often enough -
    the Outcome and the logs are both there - and then the two views that read '.orca' have
    nothing to read. 'stats: 0 tables' and 'empty: 0 table(s)' are then indistinguishable from
    a genuine finding, which is the one thing a health report must not get wrong.
    """

    def test_stats_of_a_real_ingest_is_available(self):
        self.assertTrue(report.build_stats_view(TIMING)["available"])

    def test_stats_says_when_there_was_no_timing_to_read(self):
        self.assertFalse(report.build_stats_view([], available=False)["available"])

    def test_the_empty_view_tracks_its_two_inputs_separately(self):
        # A run ingested without duckdb has real timing and no manifest. Calling that "no
        # empty files" would relabel one missing input as the other.
        view = report.build_empty_view(TIMING, None, files_available=False)

        self.assertTrue(view["available"])
        self.assertFalse(view["files_available"])

    def test_a_directory_that_was_never_ingested_reports_neither_half(self):
        view = report.build_empty_view([], None, available=False, files_available=False)

        self.assertFalse(view["available"])
        self.assertFalse(view["files_available"])

    def test_an_extracted_tree_has_an_outcome_but_no_timing(self):
        # Exactly what 'orca report <extracted tree>' hits: ORC's own files are there, the
        # ones ingest writes are not.
        with tempfile.TemporaryDirectory() as directory:
            with open(os.path.join(directory, "ORC_Outcome.json"), "w", encoding="utf-8") as f:
                f.write('{"dfir-orc": {"outcome": {"computer_name": "HOST", '
                        '"command_set": []}}}')

            hr = report.build_host_report(directory)

        self.assertTrue(hr["outcome"]["available"])
        self.assertFalse(hr["stats"]["available"])
        self.assertFalse(hr["empty"]["available"])
        self.assertFalse(hr["empty"]["files_available"])

    def test_an_ingested_run_reports_its_timing_as_available(self):
        with tempfile.TemporaryDirectory() as directory:
            os.makedirs(os.path.join(directory, layout.ORCA_DIR))
            with open(layout.timing_path(directory), "w", newline="", encoding="utf-8") as f:
                writer = csv.writer(f)
                writer.writerow(["kind", "name", "rows", "bytes", "seconds"])
                writer.writerow(["parquet", "NTFSInfo", "100", "5000", "2.0"])

            hr = report.build_host_report(directory)

        self.assertTrue(hr["stats"]["available"])
        self.assertTrue(hr["empty"]["available"])
        self.assertEqual(hr["stats"]["tables"], 1)


class TestRunDirDiscovery(unittest.TestCase):
    def test_a_run_dir_that_does_not_exist_is_an_error(self):
        # It used to report cheerfully on nothing: no Outcome, no tables, all zeros. A typo
        # has to be a typo, which is also what makes a mistyped mode name legible.
        with self.assertRaises(RuntimeError) as caught:
            report.discover_run_dirs(["E:/definitely/not/here"], None)

        self.assertIn("no such run dir", str(caught.exception))

    def test_an_existing_run_dir_is_kept(self):
        with tempfile.TemporaryDirectory() as directory:
            self.assertEqual(report.discover_run_dirs([directory], None), [directory])

    def test_root_discovery_only_takes_ingested_runs(self):
        with tempfile.TemporaryDirectory() as parent:
            ingested = os.path.join(parent, "run")
            os.makedirs(os.path.join(ingested, layout.ORCA_DIR))
            with open(layout.timing_path(ingested), "w", encoding="utf-8") as f:
                f.write("kind,name,rows,bytes,seconds\n")
            os.makedirs(os.path.join(parent, "an-extracted-tree"))

            self.assertEqual(report.discover_run_dirs([], parent), [ingested])


if __name__ == "__main__":
    unittest.main()
