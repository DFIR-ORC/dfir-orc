import json
import os
import tempfile
import unittest
from orca import outcome

DOC = {
    "dfir-orc": {"outcome": {
        "computer_name": "TESTHOST",
        "start": "2026-06-28T10:00:00.000Z",
        "end": "2026-06-28T10:30:00.000Z",
        "command_set": [
            {"name": "SetA", "statistics": {"process_memory_peak": 1000},
             "commands": [{"name": "cmd1", "exit_code": 0,
                           "start": "2026-06-28T10:00:00.000Z", "end": "2026-06-28T10:01:00.000Z"}]},
            {"name": "SetB", "statistics": {"process_memory_peak": 5000},
             "commands": [{"name": "cmd2", "exit_code": "0xFFFFFFFF",
                           "start": "2026-06-28T10:02:00.000Z", "end": "2026-06-28T10:05:00.000Z"}]},
        ]}}}


class TestOutcome(unittest.TestCase):
    def _write(self, doc):
        fd, p = tempfile.mkstemp(suffix=".json")
        os.close(fd)
        with open(p, "w", encoding="utf-8") as f:
            json.dump(doc, f)
        return p

    def test_parses_host_and_duration(self):
        o = outcome.parse_outcome(self._write(DOC))
        self.assertEqual(o.host, "TESTHOST")
        self.assertEqual(o.duration_seconds, 1800.0)

    def test_collects_only_failures(self):
        o = outcome.parse_outcome(self._write(DOC))
        self.assertEqual(len(o.failures), 1)
        self.assertEqual(o.failures[0].command, "cmd2")
        self.assertEqual(o.failures[0].set, "SetB")
        self.assertEqual(o.failures[0].duration_seconds, 180.0)

    def test_memory_peak(self):
        o = outcome.parse_outcome(self._write(DOC))
        self.assertEqual(o.memory_peak_bytes, 5000)
        self.assertEqual(o.memory_peak_set, "SetB")


if __name__ == "__main__":
    unittest.main()
