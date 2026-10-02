import json
import os
import tempfile
import unittest
from orca import outcome

OUTCOME = {"dfir-orc": {"outcome": {
    "id": "{22546F36-A4F0-4FDF-BD32-74D93D13AD97}",
    "computer_name": "TESTHOST",
    "system_type": "WorkStation",
    "timestamp": "20260918_101548",
    "start": "2026-09-18T10:15:48Z",
    "command_set": [{
        "name": "ORC_Offline",
        "start": "2026-09-18T10:15:49Z",
        "statistics": {"process_memory_peak": 3600400384, "process": 24},
        "archive": {"name": "ORC_TESTHOST_Offline.7z", "sha1": "492A0A", "size": 2011382199,
                    "input_type": "offline"},
        "commands": [
            {"name": "VSS_list_offline", "tool": "NTFSUtil", "exit_code": 0, "pid": 7428,
             "start": "2026-09-18T10:15:49Z", "end": "2026-09-18T10:15:59Z",
             "command_line": "NTFSUtil /VSS", "user_time": 1, "kernel_time": 2,
             "origin": {"friendly_name": "<self>", "type": "file"},
             "io_counters": {"read_operation": 10, "read_transfer": 20,
                             "write_operation": 30, "write_transfer": 40},
             "output": [{"name": "VSS_list.csv"}]},
            {"name": "USNInfo_offline", "tool": "USNInfo", "exit_code": 11, "pid": 91,
             "start": "2026-09-18T10:20:00Z", "end": "2026-09-18T10:21:59Z"},
        ],
    }],
}}}


class OutcomeTableCase(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)

    def write(self, name, payload):
        path = os.path.join(self.dir.name, name)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as f:
            json.dump(payload, f)
        return path


class TestCommandRows(OutcomeTableCase):
    def rows(self):
        return outcome.command_rows(OUTCOME["dfir-orc"]["outcome"], "C:\\runs\\a\\outcome.json")

    def test_one_row_per_command(self):
        self.assertEqual([r["command"] for r in self.rows()],
                         ["VSS_list_offline", "USNInfo_offline"])

    def test_run_identity_is_on_every_row(self):
        # run_id is what joins this table to the Outline table.
        for row in self.rows():
            self.assertEqual(row["run_id"], "{22546F36-A4F0-4FDF-BD32-74D93D13AD97}")
            self.assertEqual(row["computer_name"], "TESTHOST")

    def test_exit_code_is_kept_raw_and_as_an_int(self):
        failed = self.rows()[1]
        self.assertEqual(failed["exit_code"], "11")
        self.assertEqual(failed["exit_code_int"], 11)
        self.assertEqual(failed["ok"], "false")
        self.assertEqual(self.rows()[0]["ok"], "true")

    def test_timing_and_counters_are_promoted(self):
        row = self.rows()[0]
        self.assertEqual(row["duration_seconds"], 10.0)
        self.assertEqual((row["read_operation"], row["write_transfer"]), (10, 40))
        self.assertEqual((row["user_time"], row["kernel_time"]), (1, 2))
        self.assertEqual(row["origin_name"], "<self>")
        self.assertEqual(row["output_count"], 1)

    def test_set_and_archive_context_is_carried_down(self):
        row = self.rows()[0]
        self.assertEqual(row["command_set"], "ORC_Offline")
        self.assertEqual(row["set_memory_peak"], 3600400384)
        self.assertEqual(row["archive_size"], 2011382199)
        self.assertEqual(row["archive_input_type"], "offline")

    def test_a_command_missing_optional_fields_still_yields_a_row(self):
        row = self.rows()[1]
        self.assertEqual(row["sha1"], "")
        self.assertEqual(row["read_operation"], "")
        self.assertEqual(row["output_count"], 0)

    def test_the_command_document_is_kept(self):
        self.assertEqual(json.loads(self.rows()[0]["json"])["tool"], "NTFSUtil")

    def test_an_outcome_with_no_command_set(self):
        self.assertEqual(outcome.command_rows({"id": "x"}, "s.json"), [])


class TestCollecting(OutcomeTableCase):
    def test_reads_the_outcome_of_each_run(self):
        first = self.write(os.path.join("a", "outcome.json"), OUTCOME)
        # A run whose collection had no Outcome contributes no path and so no rows.

        rows = outcome.command_rows_from([first])
        self.assertEqual(len(rows), 2)

    def test_an_outline_is_not_an_outcome(self):
        path = self.write("outcome.json", {"dfir-orc": {"outline": {"id": "x"}}})
        self.assertIsNone(outcome.read_outcome(path))

    def test_unreadable_json_is_skipped(self):
        path = os.path.join(self.dir.name, "outcome.json")
        with open(path, "w", encoding="utf-8") as f:
            f.write("{nope")
        self.assertIsNone(outcome.read_outcome(path))
        self.assertEqual(outcome.command_rows_from([path]), [])

    def test_outcome_file_of_a_run_without_one(self):
        self.assertIsNone(outcome.outcome_file(self.dir.name))


if __name__ == "__main__":
    unittest.main()
