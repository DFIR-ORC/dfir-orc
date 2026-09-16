import unittest
from orca import diff


class TestRenderRowsTable(unittest.TestCase):
    def test_markers_and_header(self):
        out = diff.render_rows_table(["FRN", "Name"],
                                     added=[{"FRN": "1", "Name": "a"}],
                                     removed=[{"FRN": "2", "Name": "b"}],
                                     total_added=1, total_removed=1)
        lines = out.splitlines()
        self.assertIn("FRN", lines[0])
        self.assertTrue(lines[1].startswith("+ "))
        self.assertTrue(lines[2].startswith("- "))
        self.assertIn("a", lines[1])
        self.assertIn("b", lines[2])

    def test_cell_truncation(self):
        out = diff.render_rows_table(["C"], [{"C": "x" * 100}], [], 1, 0, max_cell=10)
        self.assertIn("x" * 7 + "...", out)
        self.assertNotIn("x" * 11, out)

    def test_column_fitting_notes_hidden(self):
        cols = [f"col{i}" for i in range(10)]
        row = {c: "value" for c in cols}
        out = diff.render_rows_table(cols, [row], [], 1, 0, max_width=20)
        self.assertIn("more columns", out)

    def test_note_when_rows_capped(self):
        out = diff.render_rows_table(["C"], [{"C": "a"}], [], total_added=50, total_removed=0)
        self.assertIn("of +50", out)

    def test_empty_when_no_rows(self):
        self.assertEqual(diff.render_rows_table(["C"], [], [], 0, 0), "")


if __name__ == "__main__":
    unittest.main()
