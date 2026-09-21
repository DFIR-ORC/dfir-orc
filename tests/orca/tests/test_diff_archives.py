import io
import os
import tempfile
import unittest
from contextlib import redirect_stdout
from unittest import mock
from orca import cache, diff, engine

EMPTY_RUN_DIFF = {"tables": {}, "added_tables": [], "removed_tables": []}


class TestIsTable(unittest.TestCase):
    def test_parquet_and_csv_are_tables(self):
        self.assertTrue(diff.is_table("getthis.parquet"))
        self.assertTrue(diff.is_table(r"E:\runs\a\csv\NTFSInfo.CSV"))

    def test_archives_and_directories_are_not(self):
        self.assertFalse(diff.is_table("DFIR-ORC_HOST.7z"))
        self.assertFalse(diff.is_table("DFIR-ORC_HOST.p7b"))
        self.assertFalse(diff.is_table(r"E:\runs\host"))


class DispatchCase(unittest.TestCase):
    """run_diff's routing, with ingestion and the engine stubbed out."""

    def setUp(self):
        self.resolved = []

        def ensure_run(source, cache_dir=None, **kwargs):
            self.resolved.append((source, cache_dir, kwargs))
            return f"/cache/{os.path.basename(source)}-hash"

        for target, attribute, value in (
                (cache, "ensure_run", ensure_run),
                (engine, "get_engine", lambda name, memory_limit=None: mock.Mock()),
                (diff, "diff_runs", lambda *a, **k: EMPTY_RUN_DIFF),
                (diff, "diff_tables", lambda *a, **k: {
                    "compatible": True, "added": 0, "removed": 0, "compare_cols": ["a"],
                    "only_in_a": [], "only_in_b": [], "path_a": "a", "path_b": "b"})):
            patcher = mock.patch.object(target, attribute, value)
            patcher.start()
            self.addCleanup(patcher.stop)

    def run_diff(self, **kwargs):
        out = io.StringIO()
        with redirect_stdout(out):
            diff.run_diff(**kwargs)
        return out.getvalue()


class TestDispatch(DispatchCase):
    def test_two_positional_archives_become_a_run_diff(self):
        self.run_diff(path_a="NEW.7z", path_b="REF.p7b")
        self.assertEqual([source for source, _, _ in self.resolved], ["NEW.7z", "REF.p7b"])

    def test_two_positional_tables_stay_a_file_pair_diff(self):
        self.run_diff(path_a="a.parquet", path_b="b.parquet")
        self.assertEqual(self.resolved, [])

    def test_run_and_reference_accept_archives(self):
        self.run_diff(run_a="NEW.7z", run_b="REF.7z")
        self.assertEqual([source for source, _, _ in self.resolved], ["NEW.7z", "REF.7z"])

    def test_decryption_options_reach_the_cache(self):
        self.run_diff(run_a="NEW.p7b", run_b="REF.p7b", private_key="k.pem",
                      cms_backend="openssl", password="S3cret", cache_dir="C:/cache",
                      refresh=True, exclude_cols=["MD5"])

        _, cache_dir, kwargs = self.resolved[0]
        self.assertEqual(cache_dir, "C:/cache")
        self.assertEqual(kwargs["private_key"], "k.pem")
        self.assertEqual(kwargs["cms_backend"], "openssl")
        self.assertEqual(kwargs["password"], "S3cret")
        self.assertEqual(kwargs["exclude_cols"], ["MD5"])
        self.assertTrue(kwargs["refresh"])

    def test_report_names_the_archives_not_the_cache(self):
        # A cache directory is a hash; the user needs to read back what they compared.
        text = self.run_diff(path_a="NEW.7z", path_b="REF.7z")
        self.assertIn("NEW.7z vs REF.7z", text)
        self.assertNotIn("hash", text)

    def test_one_line_also_names_the_archives(self):
        text = self.run_diff(path_a="NEW.7z", path_b="REF.7z", one_line=True)
        self.assertIn("NEW.7z vs REF.7z", text)

    def test_a_single_positional_is_still_an_error(self):
        with self.assertRaises(RuntimeError):
            self.run_diff(path_a="NEW.7z")


class TestRunDirsSkipTheCache(unittest.TestCase):
    def test_an_ingested_run_dir_is_not_re_ingested(self):
        with tempfile.TemporaryDirectory() as d:
            run = os.path.join(d, "run")
            os.makedirs(os.path.join(run, "parquet"))
            with mock.patch.object(cache.ingest, "run_ingest") as ingest:
                self.assertEqual(cache.ensure_run(run, os.path.join(d, "cache")), run)
            ingest.assert_not_called()


if __name__ == "__main__":
    unittest.main()
