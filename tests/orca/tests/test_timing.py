import csv
import os
import tempfile
import unittest
from orca import timing


class TestTiming(unittest.TestCase):
    def test_write_and_read_back(self):
        recs = [timing.TimingRecord("NTFSInfo", "parquet", 10, 2048, 1.5),
                timing.TimingRecord("NTFSInfo", "csv", 10, 4096, 0.5)]
        with tempfile.TemporaryDirectory() as d:
            out = os.path.join(d, "t.csv")
            timing.write_timing_csv(recs, out)
            with open(out, newline="") as f:
                rows = list(csv.DictReader(f))
        self.assertEqual(rows[0]["name"], "NTFSInfo")
        self.assertEqual(rows[0]["kind"], "parquet")
        self.assertEqual(rows[0]["seconds"], "1.5")

    def test_summarize_mentions_totals(self):
        recs = [timing.TimingRecord("a", "parquet", 1, 1, 2.0)]
        s = timing.summarize(recs)
        self.assertIn("1", s)        # file count
        self.assertIn("2.0", s)      # total seconds


if __name__ == "__main__":
    unittest.main()
