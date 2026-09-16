import json
import os
import tempfile
import unittest
from orca import ingest


class TestFindOutcome(unittest.TestCase):
    def test_finds_outcome_json_in_dir(self):
        with tempfile.TemporaryDirectory() as d:
            good = os.path.join(d, "X_Outcome.json")
            with open(good, "w", encoding="utf-8") as f:
                json.dump({"dfir-orc": {"outcome": {"computer_name": "H"}}}, f)
            with open(os.path.join(d, "other.json"), "w", encoding="utf-8") as f:
                json.dump({"something": 1}, f)
            self.assertEqual(ingest.find_outcome_json(d), good)

    def test_none_when_absent(self):
        with tempfile.TemporaryDirectory() as d:
            self.assertIsNone(ingest.find_outcome_json(d))


if __name__ == "__main__":
    unittest.main()
