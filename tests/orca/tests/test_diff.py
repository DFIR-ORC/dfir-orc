import unittest
from orca import diff


class TestDiffPure(unittest.TestCase):
    def test_manifest_reader_escapes_a_quoted_path(self):
        # A run directory under a profile like O'Brien would otherwise break the statement.
        self.assertEqual(diff._manifest_reader("C:\\O'B\\manifest.parquet"),
                         "read_parquet('C:/O''B/manifest.parquet')")

    def test_reader_for(self):
        self.assertEqual(diff.reader_for("x.parquet"), "read_parquet")
        self.assertEqual(diff.reader_for("x.csv"), "read_csv_auto")

    def test_select_columns_default_drops_volatile(self):
        compare, only_a, only_b = diff.select_columns(
            ["FRN", "Name", "CreationDate", "Size"], ["FRN", "Name", "CreationDate", "Reason"])
        self.assertEqual(compare, ["FRN", "Name"])
        self.assertEqual(only_a, ["Size"])
        self.assertEqual(only_b, ["Reason"])

    def test_select_columns_explicit_intersects_both(self):
        compare, _a, _b = diff.select_columns(
            ["FRN", "Name", "Size"], ["FRN", "Name", "Reason"], columns=["FRN", "Reason", "Name"])
        self.assertEqual(compare, ["FRN", "Name"])

    def test_select_columns_extra_exclude(self):
        compare, _a, _b = diff.select_columns(
            ["FRN", "Name", "Size"], ["FRN", "Name", "Size"], exclude_cols=["Size"])
        self.assertEqual(compare, ["FRN", "Name"])

    def test_build_except_count_sql_shape(self):
        sql = diff.build_except_count_sql("read_parquet", "a.parquet",
                                          "read_parquet", "b.parquet", ["FRN"])
        self.assertIn("EXCEPT ALL", sql)
        self.assertIn('CAST("FRN" AS VARCHAR)', sql)
        self.assertIn("read_parquet('a.parquet')", sql)
        self.assertIn("count(*)", sql)


if __name__ == "__main__":
    unittest.main()
