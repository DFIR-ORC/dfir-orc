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

    def test_summary_of(self):
        s = report.summary_of(self._hr())
        self.assertEqual(s["host"], "TESTHOST")
        self.assertEqual(s["ok"], False)
        self.assertEqual(s["fails"], 1)
        self.assertEqual(s["empty"], 1)
        self.assertEqual(s["tables"], 25)

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
