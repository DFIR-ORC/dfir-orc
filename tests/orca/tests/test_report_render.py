import json
import unittest
from orca import report


class TestRender(unittest.TestCase):
    def _hr(self):
        return {
            "host": "TESTHOST", "run_dir": "d",
            "outcome": {"available": True, "ok": False,
                        "failures": [{"set": "S", "command": "c", "exit_code": "0x1",
                                      "duration_seconds": 5.0, "start": ""}],
                        "duration_seconds": 1620.0, "memory_peak_bytes": 1503238553,
                        "memory_peak_set": "S"},
            "empty": {"empty_tables": ["E1"], "count": 1},
            "stats": {"tables": 25, "total_rows": 100, "total_bytes": 13207024435,
                      "biggest": [], "slowest": []},
            "errors": {"available": True, "logs": []},
        }

    def _hr_with_skips(self):
        hr = self._hr()
        hr["errors"]["skipped"] = {"autoruns": "duckdb said no"}
        return hr

    def test_summary_of(self):
        s = report.summary_of(self._hr())
        self.assertEqual(s["host"], "TESTHOST")
        self.assertEqual(s["ok"], False)
        self.assertEqual(s["fails"], 1)
        self.assertEqual(s["empty"], 1)
        self.assertEqual(s["tables"], 25)
        self.assertEqual(s["unread"], 0)

    def test_an_unread_table_is_counted(self):
        self.assertEqual(report.summary_of(self._hr_with_skips())["unread"], 1)

    def test_one_line_stays_quiet_when_nothing_was_skipped(self):
        # A field that always reads zero stops being read, and this one says the run is
        # missing a table rather than the collection missing an artifact.
        self.assertNotIn("unread=", report.render_one_line(report.summary_of(self._hr())))

    def test_one_line_says_so_when_something_was(self):
        line = report.render_one_line(report.summary_of(self._hr_with_skips()))
        self.assertIn("unread=1", line)

    def test_the_errors_view_names_the_unread_table_and_why(self):
        text = report.render_human(self._hr_with_skips(), ["errors"])
        self.assertIn("unread tables: 1", text)
        self.assertIn("autoruns: duckdb said no", text)

    def test_the_errors_view_says_nothing_when_the_run_is_complete(self):
        self.assertNotIn("unread", report.render_human(self._hr(), ["errors"]))

    def test_render_one_line_has_host_and_fields(self):
        line = report.render_one_line(report.summary_of(self._hr()))
        self.assertIn("TESTHOST:", line)
        self.assertIn("ok=False", line)
        self.assertIn("fails=1", line)
        self.assertIn("empty=1", line)

    def test_render_json_is_list_of_summaries(self):
        out = report.render_json([report.summary_of(self._hr())])
        data = json.loads(out)
        self.assertEqual(data[0]["host"], "TESTHOST")
        self.assertEqual(data[0]["fails"], 1)


if __name__ == "__main__":
    unittest.main()
