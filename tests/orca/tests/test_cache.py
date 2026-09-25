import json
import os
import tempfile
import unittest
from unittest import mock
from orca import cache, layout


def make_run_dir(path, marker=True):
    os.makedirs(layout.orca_dir(path), exist_ok=True)
    if marker:
        with open(cache.marker_path(path), "w", encoding="utf-8") as f:
            json.dump({"source": "x"}, f)
    return path


class CacheCase(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)
        self.work_dir = os.path.join(self.dir.name, "work")
        self.archive = os.path.join(self.dir.name, "Little.7z")
        with open(self.archive, "wb") as f:
            f.write(b"7z\xbc\xaf\x27\x1c")

    def fake_ingest(self, products=("NTFSInfo.parquet",)):
        """Stand-in for run_ingest that records its calls and produces a plausible run dir:
        its tables flat, and the product list a rebuild goes by."""
        calls = []

        def ingest(source, out_dir, **kwargs):
            calls.append((source, out_dir, kwargs))
            os.makedirs(layout.orca_dir(out_dir), exist_ok=True)
            for name in products:
                with open(os.path.join(out_dir, name), "wb") as f:
                    f.write(b"table")
            with open(layout.products_path(out_dir), "w", encoding="utf-8") as f:
                f.write("\n".join([layout.ORCA_DIR, *products]) + "\n")

        return calls, ingest


class TestIsRunDir(CacheCase):
    def test_true_with_the_bookkeeping_an_ingest_writes(self):
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
            self.assertEqual(cache.ensure_run(run), run)
        ingest.assert_not_called()

    def test_missing_source_fails_closed(self):
        with self.assertRaises(RuntimeError) as caught:
            cache.ensure_run(os.path.join(self.dir.name, "absent.7z"))
        self.assertIn("does not exist", str(caught.exception))

    def test_the_run_lands_beside_the_archive_under_its_name(self):
        calls, fake = self.fake_ingest()
        with mock.patch.object(cache.ingest, "run_ingest", fake):
            run = cache.ensure_run(self.archive)

        self.assertEqual(run, os.path.join(self.dir.name, "Little"))

    def test_work_dir_moves_the_run_and_keeps_two_archives_apart(self):
        # Under one --work-dir, NEW\Little.7z and REF\Little.7z would collide on their name.
        other_dir = os.path.join(self.dir.name, "ref")
        os.makedirs(other_dir)
        other = os.path.join(other_dir, "Little.7z")
        with open(other, "wb") as f:
            f.write(b"7z\xbc\xaf\x27\x1c\x00")

        calls, fake = self.fake_ingest()
        with mock.patch.object(cache.ingest, "run_ingest", fake):
            one = cache.ensure_run(self.archive, self.work_dir)
            two = cache.ensure_run(other, self.work_dir)

        self.assertNotEqual(one, two)
        for run in (one, two):
            self.assertEqual(os.path.dirname(run), self.work_dir)
            self.assertTrue(os.path.basename(run).startswith("Little-"))

    def test_ingests_once_then_reuses(self):
        calls, fake = self.fake_ingest()
        with mock.patch.object(cache.ingest, "run_ingest", fake):
            first = cache.ensure_run(self.archive)
            second = cache.ensure_run(self.archive)

        self.assertEqual(first, second)
        self.assertEqual(len(calls), 1)
        self.assertTrue(os.path.exists(cache.marker_path(first)))

    def test_a_changed_archive_is_re_ingested(self):
        # The key moved out of the directory name into the marker, so this is the check that
        # keeps a re-collected archive from being diffed against its own stale parquet.
        calls, fake = self.fake_ingest()
        with mock.patch.object(cache.ingest, "run_ingest", fake):
            first = cache.ensure_run(self.archive)
            with open(self.archive, "ab") as f:
                f.write(b"more")
            os.utime(self.archive, (1, 1))
            second = cache.ensure_run(self.archive)

        self.assertEqual(first, second)
        self.assertEqual(len(calls), 2)

    def test_changed_ingest_options_re_ingest(self):
        calls, fake = self.fake_ingest()
        with mock.patch.object(cache.ingest, "run_ingest", fake):
            cache.ensure_run(self.archive)
            cache.ensure_run(self.archive, exclude_cols=["MD5"])
        self.assertEqual(len(calls), 2)

    def test_refresh_re_ingests(self):
        calls, fake = self.fake_ingest()
        with mock.patch.object(cache.ingest, "run_ingest", fake):
            cache.ensure_run(self.archive)
            cache.ensure_run(self.archive, refresh=True)
        self.assertEqual(len(calls), 2)

    def test_interrupted_ingest_is_rebuilt(self):
        calls, fake = self.fake_ingest()
        with mock.patch.object(cache.ingest, "run_ingest", fake):
            run = cache.ensure_run(self.archive)
            os.remove(cache.marker_path(run))  # what a killed ingest leaves behind
            cache.ensure_run(self.archive)
        self.assertEqual(len(calls), 2)

    def test_rebuilding_removes_the_previous_products_and_nothing_else(self):
        # The run shares its directory with the collection's own files and with whatever
        # 'orca extract' expanded there: a rebuild goes by the recorded product list.
        calls, fake = self.fake_ingest()
        with mock.patch.object(cache.ingest, "run_ingest", fake):
            run = cache.ensure_run(self.archive)

        expanded = os.path.join(run, "GetThis_Default")
        os.makedirs(expanded)
        with open(os.path.join(expanded, "GetThis.csv"), "wb") as f:
            f.write(b"collected")

        stale = os.path.join(run, "NTFSInfo.parquet")
        self.assertTrue(os.path.exists(stale))

        # A second ingest that produces a different table must not leave the first one behind.
        _, other = self.fake_ingest(products=("USNInfo.parquet",))
        with mock.patch.object(cache.ingest, "run_ingest", other):
            cache.ensure_run(self.archive, refresh=True)

        self.assertTrue(os.path.exists(os.path.join(expanded, "GetThis.csv")))
        self.assertTrue(os.path.exists(os.path.join(run, "USNInfo.parquet")))
        self.assertFalse(os.path.exists(stale))

    def test_runs_are_ingested_parquet_only(self):
        calls, fake = self.fake_ingest()
        with mock.patch.object(cache.ingest, "run_ingest", fake):
            cache.ensure_run(self.archive, private_key="k.pem")

        _, _, kwargs = calls[0]
        self.assertFalse(kwargs["make_csv"])
        self.assertFalse(kwargs["make_trunc"])
        self.assertEqual(kwargs["private_key"], "k.pem")

    def test_the_ingest_is_told_where_to_keep_decrypted_output(self):
        calls, fake = self.fake_ingest()
        with mock.patch.object(cache.ingest, "run_ingest", fake):
            cache.ensure_run(self.archive)

        _, _, kwargs = calls[0]
        self.assertEqual(kwargs["keep_dir"], self.dir.name)


if __name__ == "__main__":
    unittest.main()
