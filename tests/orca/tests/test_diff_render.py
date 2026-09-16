import json
import unittest
from orca import diff

DT = {"compatible": True, "added": 12, "removed": 3,
      "compare_cols": ["FRN", "Name"], "only_in_a": ["Size"], "only_in_b": []}


class TestDiffRender(unittest.TestCase):
    def test_table_line_counts_and_cols(self):
        line = diff.table_line("NTFSInfo", DT)
        self.assertIn("NTFSInfo:", line)
        self.assertIn("+12", line)
        self.assertIn("-3", line)
        self.assertIn("Size", line)

    def test_table_line_incompatible(self):
        line = diff.table_line("X", {"compatible": False, "added": 0, "removed": 0,
                                     "compare_cols": [], "only_in_a": ["a"], "only_in_b": ["b"]})
        self.assertIn("incompatible", line.lower())

    def test_render_json_roundtrips(self):
        out = diff.render_json({"mode": "pair", "added": 12, "removed": 3})
        self.assertEqual(json.loads(out)["added"], 12)


if __name__ == "__main__":
    unittest.main()
