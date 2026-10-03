import os
import unittest
from unittest import mock
from orca import cache, merge


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


class TestIngestingOnDemand(unittest.TestCase):
    """What merge does when handed something that is not a dataset yet.

    merge reads per-table parquet and only 'ingest' writes it, so there is nothing to ask: an
    archive or a collection is ingested first, the same way 'diff' does it, and the cache means
    the second merge of the same collection pays nothing.
    """

    def setUp(self):
        self.asked = []

        def ensure_dataset(source, work_dir=None, **options):
            self.asked.append((source, work_dir, options))
            return "/cache/Little-abc123"

        tables = [{"name": "NTFSInfo_x", "path": "p.parquet",
                   "columns": ["VolumeID", "FRN", "MD5"], "ts_col": None, "mtime": ""}]
        for target, attribute, value in (
                (cache, "ensure_dataset", ensure_dataset),
                (merge, "get_engine", lambda *a, **k: mock.Mock()),
                (merge, "_discover_tables", lambda *a, **k: tables),
                (merge, "build_scalar_sql", lambda *a, **k: "SELECT 1")):
            patcher = mock.patch.object(target, attribute, value)
            patcher.start()
            self.addCleanup(patcher.stop)

    def test_an_archive_is_ingested_before_it_is_merged(self):
        merge.run_merge("E:/dumps/Little.7z")

        self.assertEqual([source for source, _work, _options in self.asked],
                         ["E:/dumps/Little.7z"])

    def test_the_output_lands_in_the_dataset_that_came_back(self):
        out = merge.run_merge("E:/dumps/Little.7z")

        self.assertEqual(out, os.path.join("/cache/Little-abc123", "dfir-orc.parquet"))

    def test_an_ingest_that_fails_says_what_to_do_about_it(self):
        # One unreadable collected file otherwise costs the whole merge, and the reason
        # arrives as a page of duckdb SQL: on a real w11 collection 'autoruns.csv' is UTF-16
        # text of autorunsc's own usage message, saved under a .csv name.
        def refuse(*_args, **_kwargs):
            raise RuntimeError("duckdb failed: It was not possible to automatically detect "
                               "the CSV parsing dialect")

        with mock.patch.object(cache, "ensure_dataset", refuse):
            with self.assertRaises(RuntimeError) as caught:
                merge.run_merge("E:/dumps/Little.7z")

        message = str(caught.exception)
        self.assertIn("could not ingest", message)
        self.assertIn("--skip-errors", message)
        self.assertIn("duckdb failed", message)

    def test_the_keys_and_the_work_dir_are_handed_to_the_ingest(self):
        # Without them a sealed collection could not be ingested at all.
        merge.run_merge("E:/dumps/Little.7z.p7b", private_key="k.pem", work_dir="E:/work",
                        password="s3cret", cms_backend="cng", refresh=True, skip_errors=True)

        source, work_dir, options = self.asked[0]
        self.assertEqual(work_dir, "E:/work")
        self.assertEqual(options["private_key"], "k.pem")
        self.assertEqual(options["password"], "s3cret")
        self.assertEqual(options["cms_backend"], "cng")
        self.assertTrue(options["refresh"])
        self.assertTrue(options["skip_errors"])


if __name__ == "__main__":
    unittest.main()
