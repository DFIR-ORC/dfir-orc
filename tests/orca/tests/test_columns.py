import unittest
from orca import columns


class TestColumns(unittest.TestCase):
    def test_kept_columns_drops_volatile_preserving_order(self):
        allc = ["FRN", "Name", "CreationDate", "Size", "LastAccessDate"]
        self.assertEqual(columns.kept_columns(allc, ["CreationDate", "LastAccessDate"]),
                         ["FRN", "Name", "Size"])

    def test_kept_columns_is_case_sensitive(self):
        self.assertEqual(columns.kept_columns(["Name", "name"], ["name"]), ["Name"])

    def test_kept_columns_empty_drop_keeps_all(self):
        self.assertEqual(columns.kept_columns(["a", "b"], []), ["a", "b"])

    def test_quote_columns(self):
        self.assertEqual(columns.quote_columns(["FRN", "Name"]), '"FRN", "Name"')

    def test_default_volatile_includes_sample_collection_date(self):
        self.assertIn("SampleCollectionDate", columns.DEFAULT_VOLATILE_COLUMNS)


if __name__ == "__main__":
    unittest.main()
