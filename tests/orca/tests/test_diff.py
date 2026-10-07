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

    def test_orcas_identity_columns_are_not_compared(self):
        """Two runs differ on every row in OrcRunId and OrcTimestamp: compared, every row of
        every table was a change (4,172,563 of an I30Info that had none)."""
        cols = ["FRN", "Name", "OrcComputerName", "OrcRunId", "OrcTimestamp"]
        compare, _a, _b = diff.select_columns(cols, cols)
        self.assertEqual(compare, ["FRN", "Name"])

    def test_identity_columns_named_explicitly_are_compared(self):
        cols = ["FRN", "OrcComputerName"]
        compare, _a, _b = diff.select_columns(cols, cols, columns=["OrcComputerName"])
        self.assertEqual(compare, ["OrcComputerName"])

    def test_build_except_count_sql_shape(self):
        sql = diff.build_except_count_sql("read_parquet", "a.parquet",
                                          "read_parquet", "b.parquet", ["FRN"])
        self.assertIn("EXCEPT ALL", sql)
        self.assertIn('CAST("FRN" AS VARCHAR)', sql)
        self.assertIn("read_parquet('a.parquet')", sql)
        self.assertIn("count(*)", sql)


if __name__ == "__main__":
    unittest.main()


class TestEachSideIsComputedOnce(unittest.TestCase):
    """The differing rows of each side are found once and read back for --rows and --show:
    each EXCEPT ALL is a full pass over two tables of millions of rows."""

    def test_count_rows_and_show_share_one_pass_per_side(self):
        import tempfile
        from unittest import mock

        engine = mock.Mock()
        engine.columns.return_value = ["FRN", "Name"]
        engine.count.return_value = 3
        engine.query_rows.return_value = []
        with tempfile.TemporaryDirectory() as out:
            dt = diff.diff_tables(engine, "a.parquet", "b.parquet", rows_out_dir=out, name="T")
            diff.sample_table(engine, dt, 5, name="T")

        passes = [call.args[0] for call in engine.query_to_parquet.call_args_list]
        self.assertEqual(len(passes), 2)
        self.assertTrue(all("EXCEPT ALL" in sql for sql in passes))
        later = [call.args[0] for call in engine.query_to_csv.call_args_list] + \
                [call.args[0] for call in engine.query_rows.call_args_list]
        self.assertEqual(len(later), 4)
        self.assertFalse(any("EXCEPT ALL" in sql for sql in later))
