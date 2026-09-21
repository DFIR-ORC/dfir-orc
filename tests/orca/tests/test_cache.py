import json
import os
import tempfile
import unittest
from unittest import mock
from orca import cache


def make_run_dir(path, marker=True):
    os.makedirs(os.path.join(path, "parquet"), exist_ok=True)
    if marker:
        with open(os.path.join(path, cache.MARKER), "w", encoding="utf-8") as f:
            json.dump({"source": "x"}, f)
    return path


class CacheCase(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)
        self.cache_dir = os.path.join(self.dir.name, "cache")
        self.archive = os.path.join(self.dir.name, "Little.7z")
        with open(self.archive, "wb") as f:
            f.write(b"7z\xbc\xaf\x27\x1c")

    def fake_ingest(self):
        """Stand-in for run_ingest that records its calls and produces a plausible run dir."""
        calls = []

        def ingest(source, out_dir, **kwargs):
            calls.append((source, out_dir, kwargs))
            os.makedirs(os.path.join(out_dir, "parquet"), exist_ok=True)

        return calls, ingest


class TestIsRunDir(CacheCase):
    def test_true_with_a_parquet_store(self):
        self.assertTrue(cache.is_run_dir(make_run_dir(os.path.join(self.dir.name, "run"))))

    def test_false_for_a_plain_directory(self):
        self.assertFalse(cache.is_run_dir(self.dir.name))

    def test_false_for_a_file(self):
        self.assertFalse(cache.is_run_dir(self.archive))


class TestKey(CacheCase):
    def test_is_stable_for_the_same_source(self):
        options = cache.ingest_options()
        self.assertEqual(cache.key_for(self.archive, options),
                         cache.key_for(self.archive, options))

    def test_carries_the_archive_name(self):
        self.assertTrue(cache.key_for(self.archive, cache.ingest_options()).startswith("Little-"))

    def test_changes_when_the_archive_changes(self):
        options = cache.ingest_options()
        before = cache.key_for(self.archive, options)
        with open(self.archive, "ab") as f:
            f.write(b"more")
        os.utime(self.archive, (1, 1))
        self.assertNotEqual(before, cache.key_for(self.archive, options))

    def test_changes_with_the_ingest_options(self):
        plain = cache.key_for(self.archive, cache.ingest_options())
        excluded = cache.key_for(self.archive, cache.ingest_options(exclude_cols=["MD5"]))
        self.assertNotEqual(plain, excluded)

    def test_column_order_does_not_change_the_key(self):
        one = cache.key_for(self.archive, cache.ingest_options(exclude_cols=["MD5", "SHA1"]))
        other = cache.key_for(self.archive, cache.ingest_options(exclude_cols=["SHA1", "MD5"]))
        self.assertEqual(one, other)

    def test_password_is_hashed_not_stored(self):
        options = cache.ingest_options(password="S3cret")
        self.assertNotIn("S3cret", json.dumps(options))
        self.assertNotEqual(cache.key_for(self.archive, options),
                            cache.key_for(self.archive, cache.ingest_options(password="other")))


class TestEnsureRun(CacheCase):
    def test_an_existing_run_dir_is_used_in_place(self):
        run = make_run_dir(os.path.join(self.dir.name, "run"))
        with mock.patch.object(cache.ingest, "run_ingest") as ingest:
            self.assertEqual(cache.ensure_run(run, self.cache_dir), run)
        ingest.assert_not_called()

    def test_missing_source_fails_closed(self):
        with self.assertRaises(RuntimeError) as caught:
            cache.ensure_run(os.path.join(self.dir.name, "absent.7z"), self.cache_dir)
        self.assertIn("does not exist", str(caught.exception))

    def test_ingests_once_then_reuses(self):
        calls, fake = self.fake_ingest()
        with mock.patch.object(cache.ingest, "run_ingest", fake):
            first = cache.ensure_run(self.archive, self.cache_dir)
            second = cache.ensure_run(self.archive, self.cache_dir)

        self.assertEqual(first, second)
        self.assertEqual(len(calls), 1)
        self.assertTrue(os.path.exists(os.path.join(first, cache.MARKER)))

    def test_refresh_re_ingests(self):
        calls, fake = self.fake_ingest()
        with mock.patch.object(cache.ingest, "run_ingest", fake):
            cache.ensure_run(self.archive, self.cache_dir)
            cache.ensure_run(self.archive, self.cache_dir, refresh=True)
        self.assertEqual(len(calls), 2)

    def test_interrupted_ingest_is_rebuilt(self):
        calls, fake = self.fake_ingest()
        with mock.patch.object(cache.ingest, "run_ingest", fake):
            run = cache.ensure_run(self.archive, self.cache_dir)
            os.remove(os.path.join(run, cache.MARKER))  # what a killed ingest leaves behind
            cache.ensure_run(self.archive, self.cache_dir)
        self.assertEqual(len(calls), 2)

    def test_cached_runs_are_ingested_parquet_only(self):
        calls, fake = self.fake_ingest()
        with mock.patch.object(cache.ingest, "run_ingest", fake):
            cache.ensure_run(self.archive, self.cache_dir, private_key="k.pem")

        _, _, kwargs = calls[0]
        self.assertFalse(kwargs["make_csv"])
        self.assertFalse(kwargs["make_trunc"])
        self.assertEqual(kwargs["private_key"], "k.pem")


class TestDefaultCacheDir(unittest.TestCase):
    def test_windows_uses_localappdata(self):
        with mock.patch.object(os, "name", "nt"), \
             mock.patch.dict(os.environ, {"LOCALAPPDATA": r"C:\Users\u\AppData\Local"}):
            self.assertEqual(cache.default_cache_dir(),
                             os.path.join(r"C:\Users\u\AppData\Local", "orca", "cache"))

    def test_posix_uses_xdg_cache_home(self):
        with mock.patch.object(os, "name", "posix"), \
             mock.patch.dict(os.environ, {"XDG_CACHE_HOME": "/home/u/.cache"}):
            self.assertEqual(cache.default_cache_dir(), os.path.join("/home/u/.cache", "orca"))


if __name__ == "__main__":
    unittest.main()
