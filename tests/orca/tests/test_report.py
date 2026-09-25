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


if __name__ == "__main__":
    unittest.main()
