import json
import os
import tempfile
import unittest
from orca import ingest
from orca.sevenzip import ArchiveEntry


def _e(path):
    return ArchiveEntry(path=path, size=1, crc="x", attributes="A")


class TestPlanJobs(unittest.TestCase):
    def test_filters_csv_and_names_uniquely(self):
        entries = [_e("C\\NTFSInfo.csv"), _e("D\\NTFSInfo.csv"),
                   _e("SystemInfo.csv"), _e("GetThis.7z")]
        jobs = ingest.plan_jobs([("arc.7z", entries)])
        self.assertEqual([j.output_name for j in jobs],
                         ["NTFSInfo_C", "NTFSInfo_D", "SystemInfo"])
        self.assertTrue(all(j.source_archive == "arc.7z" for j in jobs))
        self.assertTrue(all(j.entry_path.lower().endswith(".csv") for j in jobs))


class TestResolveArchives(unittest.TestCase):
    # The real Outcome json uses lowercase keys and a single 'archive' object per
    # command_set; a capitalized/list lookup returned nothing on real data.
    def _write_outcome(self, dir_, sets):
        cmd_sets = [{"name": n, "archive": {"name": a}} for n, a in sets]
        outcome = {"dfir-orc": {"outcome": {"computer_name": "H", "command_set": cmd_sets}}}
        with open(os.path.join(dir_, "OrcOutcome.json"), "w", encoding="utf-8") as f:
            json.dump(outcome, f)

    def test_dir_reads_lowercase_archive_names(self):
        with tempfile.TemporaryDirectory() as d:
            self._write_outcome(d, [("SystemInfo", "SystemInfo.7z"),
                                    ("GetThis", "GetThis.7z")])
            got = ingest.resolve_archives(d)
            self.assertEqual(got, [os.path.join(d, "SystemInfo.7z"),
                                   os.path.join(d, "GetThis.7z")])

    def test_file_passthrough(self):
        self.assertEqual(ingest.resolve_archives(__file__), [__file__])


if __name__ == "__main__":
    unittest.main()
