import os
import tempfile
import unittest
from orca import layout, report
from orca.outcome import Outcome, Failure


class TestOutcomeErrorsViews(unittest.TestCase):
    def test_build_outcome_view_none(self):
        self.assertEqual(report.build_outcome_view(None), {"available": False})

    def test_build_outcome_view_with_failures(self):
        o = Outcome(host="H", start="", end="", duration_seconds=60.0,
                    failures=[Failure("S", "c", "0x1", 5.0, "")],
                    memory_peak_bytes=2048, memory_peak_set="S")
        v = report.build_outcome_view(o)
        self.assertTrue(v["available"])
        self.assertFalse(v["ok"])
        self.assertEqual(len(v["failures"]), 1)
        self.assertEqual(v["memory_peak_bytes"], 2048)

    def test_build_outcome_view_carries_the_command_count(self):
        o = Outcome(host="H", start="", end="", duration_seconds=60.0,
                    failures=[Failure("S", "c", "0x1", 5.0, "")],
                    memory_peak_bytes=2048, memory_peak_set="S", commands=35)

        self.assertEqual(report.build_outcome_view(o)["commands"], 35)

    def test_build_errors_view_counts_pattern_lines(self):
        with tempfile.TemporaryDirectory() as run:
            with open(os.path.join(run, "a.log"), "w", encoding="utf-8") as f:
                f.write("all good\nFAILED to open\nan Error here\nfine\n")
            with open(os.path.join(run, "empty.log"), "w") as f:
                pass

            # The expanded 'Errors' container counts too; an expanded tree does not.
            errors = os.path.join(run, "Errors")
            os.makedirs(os.path.join(run, "GetThis_Default"))
            os.makedirs(errors)
            with open(os.path.join(errors, "failed.log"), "w", encoding="utf-8") as f:
                f.write("exception thrown\n")
            with open(os.path.join(run, "GetThis_Default", "buried.log"), "w",
                      encoding="utf-8") as f:
                f.write("an Error here\n")

            v = report.build_errors_view(run)

        self.assertTrue(v["available"])
        names = {x["log"]: x for x in v["logs"]}
        self.assertEqual(names["a.log"]["error_lines"], 2)
        self.assertEqual(names["Errors/failed.log"]["error_lines"], 1)
        self.assertNotIn("empty.log", names)
        self.assertNotIn("GetThis_Default/buried.log", names)

    def test_build_errors_view_missing_dir(self):
        self.assertEqual(report.build_errors_view("nope"),
                         {"available": False, "skipped": {}, "logs_ignored": 0,
                          "skipped_ignored": 0})

    def test_build_errors_view_carries_the_tables_the_ingest_could_not_read(self):
        # A hole in the run rather than a message inside it, but the same question answered:
        # what went wrong here.
        with tempfile.TemporaryDirectory() as run:
            os.makedirs(layout.orca_dir(run))
            with open(layout.skipped_path(run), "w", encoding="utf-8") as f:
                f.write("autoruns\tduckdb said no\n")

            v = report.build_errors_view(run)

        self.assertEqual(v["skipped"], {"autoruns": "duckdb said no"})

    def test_build_errors_view_of_a_complete_run_has_no_skips(self):
        with tempfile.TemporaryDirectory() as run:
            self.assertEqual(report.build_errors_view(run)["skipped"], {})


if __name__ == "__main__":
    unittest.main()
