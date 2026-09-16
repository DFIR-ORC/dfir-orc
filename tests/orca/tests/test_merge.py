import unittest
from orca import merge


class TestMerge(unittest.TestCase):
    def test_tool_timestamp_column_prefix_map(self):
        self.assertEqual(merge.tool_timestamp_column("NTFSInfo_0xabc"), "LastModificationDate")
        self.assertEqual(merge.tool_timestamp_column("USNInfo_0xabc"), "TimeStamp")
        self.assertEqual(merge.tool_timestamp_column("GetThis_ADS"), "SampleCollectionDate")
        self.assertEqual(merge.tool_timestamp_column("I30Info_x"), "FileNameLastModificationDate")
        self.assertIsNone(merge.tool_timestamp_column("volstats_x"))

    def test_tool_timestamp_override(self):
        self.assertEqual(merge.tool_timestamp_column("GetThis_ADS", {"GetThis": "CreationDate"}),
                         "CreationDate")

    def test_master_columns_union_minus_key(self):
        tables = [
            {"columns": ["VolumeID", "SnapshotID", "FRN", "MD5", "FullName"]},
            {"columns": ["VolumeID", "FRN", "MD5", "Reason"]},
        ]
        self.assertEqual(merge.master_columns(tables), ["FullName", "MD5", "Reason"])

    def test_build_scalar_sql_shapes(self):
        tables = [{"name": "NTFSInfo_x", "path": "p1.parquet",
                   "columns": ["VolumeID", "SnapshotID", "FRN", "MD5"],
                   "ts_col": "LastModificationDate", "mtime": "2026-06-25 00:00:00"},
                  {"name": "USNInfo_x", "path": "p2.parquet",
                   "columns": ["VolumeID", "FRN", "MD5"],
                   "ts_col": "TimeStamp", "mtime": "2026-06-26 00:00:00"}]
        sql = merge.build_scalar_sql(tables, ["MD5"])
        self.assertIn("read_parquet('p1.parquet')", sql)
        self.assertIn("UNION ALL", sql)
        self.assertIn('arg_max("MD5", __recency)', sql)
        self.assertIn("AS VolumeID", sql)
        self.assertIn("GROUP BY", sql)

    def test_build_lossless_sql_shapes(self):
        sql = merge.build_lossless_sql("u.parquet", ["MD5"])
        self.assertIn("read_parquet('u.parquet')", sql)
        self.assertIn("UNPIVOT", sql)
        self.assertIn("PIVOT", sql)
        self.assertIn("count(DISTINCT val)", sql)
        self.assertIn("ORDER BY __recency DESC", sql)
        self.assertIn("AS VolumeID", sql)


if __name__ == "__main__":
    unittest.main()
