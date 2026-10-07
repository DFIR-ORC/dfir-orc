import io
import json
import os
import tempfile
import unittest
from contextlib import redirect_stdout
from unittest import mock
from orca import cache, layout


def make_dataset(path, marker=True):
    os.makedirs(layout.orca_dir(path), exist_ok=True)
    if marker:
        with open(cache.marker_path(path), "w", encoding="utf-8") as f:
            json.dump({"source": "x"}, f)
    return path


class CacheCase(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)
        # The default root: a directory of its own, apart from the one the archive sits in.
        self.cwd = tempfile.TemporaryDirectory()
        self.addCleanup(self.cwd.cleanup)
        self.addCleanup(os.chdir, os.getcwd())
        os.chdir(self.cwd.name)
        self.work_dir = os.path.join(self.dir.name, "work")
        self.archive = os.path.join(self.dir.name, "Little.7z")
        with open(self.archive, "wb") as f:
            f.write(b"7z\xbc\xaf\x27\x1c")

    def fake_ingest(self, products=("NTFSInfo.parquet",)):
        """Stand-in for run_ingest that records its calls and produces a plausible run dir:
        its tables flat, the product list a rebuild goes by, and the marker under its key."""
        calls = []

        def ingest(source, out_dir, **kwargs):
            calls.append((source, out_dir, kwargs))
            os.makedirs(layout.orca_dir(out_dir), exist_ok=True)
            for name in products:
                with open(os.path.join(out_dir, name), "wb") as f:
                    f.write(b"table")
            with open(layout.products_path(out_dir), "w", encoding="utf-8") as f:
                f.write("\n".join([layout.ORCA_DIR, *products]) + "\n")
            cache.write_marker(out_dir, source, kwargs["cache_key"], {})

        return calls, ingest


class TestIsRunDir(CacheCase):
    def test_true_with_the_bookkeeping_an_ingest_writes(self):
        self.assertTrue(cache.is_dataset(make_dataset(os.path.join(self.dir.name, "run"))))

    def test_false_for_a_plain_directory(self):
        self.assertFalse(cache.is_dataset(self.dir.name))

    def test_false_for_a_file(self):
        self.assertFalse(cache.is_dataset(self.archive))


class TestLocationName(CacheCase):
    def test_a_re_collected_archive_keeps_its_name(self):
        before = cache.location_name(self.archive)
        with open(self.archive, "ab") as f:
            f.write(b"more")
        self.assertEqual(cache.location_name(self.archive), before)

    def test_two_archives_of_one_name_in_two_places_are_kept_apart(self):
        other = os.path.join(self.dir.name, "ref", "Little.7z")
        self.assertNotEqual(cache.location_name(self.archive), cache.location_name(other))
        self.assertTrue(cache.location_name(other).startswith("Little-"))


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
        other = cache.key_for(self.archive, cache.ingest_options(engine_name="other"))
        self.assertNotEqual(plain, other)

    def test_password_is_hashed_not_stored(self):
        options = cache.ingest_options(password="S3cret")
        self.assertNotIn("S3cret", json.dumps(options))
        self.assertNotEqual(cache.key_for(self.archive, options),
                            cache.key_for(self.archive, cache.ingest_options(password="other")))


class TestEnsureRun(CacheCase):
    def test_an_existing_run_dir_is_used_in_place(self):
        run = make_dataset(os.path.join(self.dir.name, "run"))
        with mock.patch.object(cache.ingest, "run_ingest") as ingest:
            self.assertEqual(cache.ensure_dataset(run), run)
        ingest.assert_not_called()

    def test_missing_source_fails_closed(self):
        with self.assertRaises(RuntimeError) as caught:
            cache.ensure_dataset(os.path.join(self.dir.name, "absent.7z"))
        self.assertIn("does not exist", str(caught.exception))

    def test_by_default_the_run_lands_in_the_current_directory(self):
        calls, fake = self.fake_ingest()
        with mock.patch.object(cache.ingest, "run_ingest", fake):
            run = cache.ensure_dataset(self.archive)

        self.assertEqual(os.path.dirname(run), os.getcwd())
        self.assertEqual(os.path.basename(run), cache.location_name(self.archive))
        self.assertEqual(os.listdir(self.dir.name), ["Little.7z"])  # the evidence is untouched

    def test_at_input_the_run_lands_beside_the_archive_under_its_name(self):
        calls, fake = self.fake_ingest()
        with mock.patch.object(cache.ingest, "run_ingest", fake):
            run = cache.ensure_dataset(self.archive, at_input=True)

        self.assertEqual(run, os.path.join(self.dir.name, "Little"))
        self.assertEqual(os.listdir(self.cwd.name), [])

    def test_work_dir_moves_the_run_and_keeps_two_archives_apart(self):
        # Under one --work-dir, NEW\Little.7z and REF\Little.7z would collide on their name.
        other_dir = os.path.join(self.dir.name, "ref")
        os.makedirs(other_dir)
        other = os.path.join(other_dir, "Little.7z")
        with open(other, "wb") as f:
            f.write(b"7z\xbc\xaf\x27\x1c\x00")

        calls, fake = self.fake_ingest()
        with mock.patch.object(cache.ingest, "run_ingest", fake):
            one = cache.ensure_dataset(self.archive, self.work_dir)
            two = cache.ensure_dataset(other, self.work_dir)

        self.assertNotEqual(one, two)
        for run in (one, two):
            self.assertEqual(os.path.dirname(run), self.work_dir)
            self.assertTrue(os.path.basename(run).startswith("Little-"))

    def test_ingests_once_then_reuses(self):
        calls, fake = self.fake_ingest()
        with mock.patch.object(cache.ingest, "run_ingest", fake):
            first = cache.ensure_dataset(self.archive)
            second = cache.ensure_dataset(self.archive)

        self.assertEqual(first, second)
        self.assertEqual(len(calls), 1)
        self.assertTrue(os.path.exists(cache.marker_path(first)))

    def test_a_changed_archive_is_re_ingested(self):
        # The key moved out of the directory name into the marker, so this is the check that
        # keeps a re-collected archive from being diffed against its own stale parquet.
        calls, fake = self.fake_ingest()
        with mock.patch.object(cache.ingest, "run_ingest", fake):
            first = cache.ensure_dataset(self.archive)
            with open(self.archive, "ab") as f:
                f.write(b"more")
            os.utime(self.archive, (1, 1))
            second = cache.ensure_dataset(self.archive)

        self.assertEqual(first, second)
        self.assertEqual(len(calls), 2)

    def test_changed_ingest_options_re_ingest(self):
        calls, fake = self.fake_ingest()
        with mock.patch.object(cache.ingest, "run_ingest", fake):
            cache.ensure_dataset(self.archive)
            cache.ensure_dataset(self.archive, engine_name="other")
        self.assertEqual(len(calls), 2)

    def test_a_dataset_missing_a_table_it_recorded_is_ingested_again(self):
        calls, fake = self.fake_ingest()
        with mock.patch.object(cache.ingest, "run_ingest", fake), \
                redirect_stdout(io.StringIO()):
            run = cache.ensure_dataset(self.archive)
            with open(layout.name_map_path(run), "w", encoding="utf-8") as f:
                f.write("name,source_archive,entry_path,source_mtime\nNTFSInfo,a,b,0\n")
            os.remove(os.path.join(run, "NTFSInfo.parquet"))
            cache.ensure_dataset(self.archive, csv=True)
        self.assertEqual(len(calls), 2)

    def test_elsewhere_reuses_a_current_dataset_beside_the_collection(self):
        """Ingested with --out-at-input, then asked for from the current directory: reused."""
        calls, fake = self.fake_ingest()
        with mock.patch.object(cache.ingest, "run_ingest", fake):
            beside = cache.ensure_dataset(self.archive, at_input=True)
            found = cache.ensure_dataset(self.archive, elsewhere=True)
        self.assertEqual(found, beside)
        self.assertEqual(len(calls), 1)

    def test_a_group_puts_the_dataset_under_it(self):
        calls, fake = self.fake_ingest()
        with mock.patch.object(cache.ingest, "run_ingest", fake):
            dataset = cache.ensure_dataset(self.archive, group="hosts")
        self.assertEqual(os.path.basename(os.path.dirname(dataset)), "hosts")

    def test_elsewhere_finds_a_fleets_dataset_under_hosts_beside_the_collection(self):
        """A fleet ran with --out-at-input; a report from anywhere reuses its dataset."""
        calls, fake = self.fake_ingest()
        with mock.patch.object(cache.ingest, "run_ingest", fake):
            fleet = cache.ensure_dataset(self.archive, at_input=True, group="hosts")
            found = cache.ensure_dataset(self.archive, elsewhere=True)
        self.assertEqual(found, fleet)
        self.assertEqual(len(calls), 1)

    def test_elsewhere_finds_a_fleets_dataset_under_hosts_here(self):
        calls, fake = self.fake_ingest()
        with mock.patch.object(cache.ingest, "run_ingest", fake):
            fleet = cache.ensure_dataset(self.archive, group="hosts")
            found = cache.ensure_dataset(self.archive, elsewhere=True)
        self.assertEqual(found, fleet)
        self.assertEqual(len(calls), 1)

    def test_without_elsewhere_the_current_directory_gets_its_own(self):
        calls, fake = self.fake_ingest()
        with mock.patch.object(cache.ingest, "run_ingest", fake):
            beside = cache.ensure_dataset(self.archive, at_input=True)
            here = cache.ensure_dataset(self.archive)
        self.assertNotEqual(here, beside)
        self.assertEqual(len(calls), 2)

    def test_csv_options_do_not_re_ingest(self):
        """The CSVs are written over the tables: asking for them must not cost an ingest."""
        calls, fake = self.fake_ingest()
        with mock.patch.object(cache.ingest, "run_ingest", fake), \
                mock.patch.object(cache.ingest, "csv_over") as csv_over:
            cache.ensure_dataset(self.archive)
            cache.ensure_dataset(self.archive, csv=True, csv_trunc=True, exclude_cols=["MD5"])
        self.assertEqual(len(calls), 1)
        _, _, _, csv, trunc, drop = csv_over.call_args.args
        self.assertEqual((csv, trunc, drop), (True, True, ["MD5"]))

    def test_refresh_re_ingests(self):
        calls, fake = self.fake_ingest()
        with mock.patch.object(cache.ingest, "run_ingest", fake):
            cache.ensure_dataset(self.archive)
            cache.ensure_dataset(self.archive, refresh=True)
        self.assertEqual(len(calls), 2)

    def test_interrupted_ingest_is_rebuilt(self):
        calls, fake = self.fake_ingest()
        with mock.patch.object(cache.ingest, "run_ingest", fake):
            run = cache.ensure_dataset(self.archive)
            os.remove(cache.marker_path(run))  # what a killed ingest leaves behind
            cache.ensure_dataset(self.archive)
        self.assertEqual(len(calls), 2)

    def test_rebuilding_removes_the_previous_products_and_nothing_else(self):
        # The run shares its directory with the collection's own files and with whatever
        # 'orca extract' expanded there: a rebuild goes by the recorded product list.
        calls, fake = self.fake_ingest()
        with mock.patch.object(cache.ingest, "run_ingest", fake):
            run = cache.ensure_dataset(self.archive)

        expanded = os.path.join(run, "GetThis_Default")
        os.makedirs(expanded)
        with open(os.path.join(expanded, "GetThis.csv"), "wb") as f:
            f.write(b"collected")

        stale = os.path.join(run, "NTFSInfo.parquet")
        self.assertTrue(os.path.exists(stale))

        # A second ingest that produces a different table must not leave the first one behind.
        _, other = self.fake_ingest(products=("USNInfo.parquet",))
        with mock.patch.object(cache.ingest, "run_ingest", other):
            cache.ensure_dataset(self.archive, refresh=True)

        self.assertTrue(os.path.exists(os.path.join(expanded, "GetThis.csv")))
        self.assertTrue(os.path.exists(os.path.join(run, "USNInfo.parquet")))
        self.assertFalse(os.path.exists(stale))

    def test_runs_are_ingested_parquet_only(self):
        calls, fake = self.fake_ingest()
        with mock.patch.object(cache.ingest, "run_ingest", fake):
            cache.ensure_dataset(self.archive, private_key="k.pem")

        _, _, kwargs = calls[0]
        self.assertFalse(kwargs["make_csv"])
        self.assertFalse(kwargs["make_trunc"])
        self.assertEqual(kwargs["private_key"], "k.pem")

    def test_the_ingest_is_told_where_to_keep_decrypted_output(self):
        calls, fake = self.fake_ingest()
        with mock.patch.object(cache.ingest, "run_ingest", fake):
            cache.ensure_dataset(self.archive)
            cache.ensure_dataset(self.archive, at_input=True)

        self.assertEqual(calls[0][2]["keep_dir"], os.getcwd())
        self.assertEqual(calls[1][2]["keep_dir"], self.dir.name)

    def test_strict_reaches_the_ingest_and_the_key(self):
        # A dataset that skipped a table is not the dataset a strict ingest would have
        # produced - that one would have failed - so the two must not share a cache entry.
        calls, fake = self.fake_ingest()
        with mock.patch.object(cache.ingest, "run_ingest", fake):
            cache.ensure_dataset(self.archive, strict=True)

        _, _, kwargs = calls[0]
        self.assertTrue(kwargs["strict"])
        self.assertNotEqual(cache.key_for(self.archive, cache.ingest_options(strict=True)),
                            cache.key_for(self.archive, cache.ingest_options()))

    def test_the_ordinary_dataset_keeps_the_key_it_always_had(self):
        # Skipping is the default now, and the default must not be written into the key: every
        # dataset ingested before today would go stale and re-ingest for nothing.
        self.assertEqual(cache.ingest_options(), cache.ingest_options(strict=False))
        self.assertNotIn("strict", cache.ingest_options())

    def test_the_work_dir_is_passed_on_and_not_only_resolved_away(self):
        # It decides where the scratch goes too, and only run_ingest can set that - a diff of
        # two large collections needs the room as much as an ingest of one.
        calls, fake = self.fake_ingest()
        with mock.patch.object(cache.ingest, "run_ingest", fake):
            cache.ensure_dataset(self.archive, work_dir=os.path.join(self.dir.name, "work"))

        _, _, kwargs = calls[0]
        self.assertEqual(kwargs["work_dir"], os.path.join(self.dir.name, "work"))


class TestLayoutBump(CacheCase):
    def test_a_dataset_from_an_older_layout_is_rebuilt(self):
        calls, fake = self.fake_ingest()
        with mock.patch.object(cache.ingest, "run_ingest", fake):
            dataset = cache.ensure_dataset(self.archive)
            with open(cache.marker_path(dataset), encoding="utf-8") as f:
                marker = json.load(f)
            marker["layout"] = cache.LAYOUT_VERSION - 1
            with open(cache.marker_path(dataset), "w", encoding="utf-8") as f:
                json.dump(marker, f)

            cache.ensure_dataset(self.archive)

        self.assertEqual(len(calls), 2)


class TestPerCollectionKey(CacheCase):
    def _collection(self, name, archive, outcome=True):
        path = os.path.join(self.dir.name, archive)
        with open(path, "wb") as f:
            f.write(b"7z\xbc\xaf\x27\x1c")
        if not outcome:
            return cache.ingest.Collection(None, [path], "", "", "")
        return cache.ingest.Collection(
            os.path.join(self.dir.name, f"ORC_{name}_Outcome.json"), [path],
            f"{name}.lab", "{%s}" % name, "20260101_000000", name=f"ORC_{name}")

    def test_two_collections_of_one_directory_get_different_keys(self):
        """Without this every host of a fleet shares one key and one dataset."""
        a = self._collection("A", "ORC_A_General.7z")
        b = self._collection("B", "ORC_B_General.7z")
        options = cache.ingest_options()

        key_a = cache.key_for(self.dir.name, options, archives=a.archives)
        key_b = cache.key_for(self.dir.name, options, archives=b.archives)

        self.assertNotEqual(key_a, key_b)

    def test_the_dataset_is_named_after_the_run(self):
        a = self._collection("A", "ORC_A_General.7z")
        with mock.patch.object(cache.ingest, "run_ingest") as run, \
                mock.patch.object(cache, "write_marker"):
            dataset = cache.ensure_dataset(self.dir.name, collection=a, jobs=3)

        self.assertEqual(os.path.basename(dataset), "ORC_A")
        self.assertEqual(run.call_args.kwargs["collection"], a)
        self.assertEqual(run.call_args.kwargs["jobs"], 3)

    def test_an_unnamed_collection_is_not_named_after_the_directorys_run(self):
        """Review Focus 7: beside a named run, the leftovers would otherwise take that run's
        directory - and clear_dataset would empty it first."""
        with open(os.path.join(self.dir.name, "ORC_A_Outcome.json"), "w", encoding="utf-8") as f:
            f.write('{"dfir-orc": {"outcome": {"id": "{A}", "command_set": []}}}')
        stray = self._collection("", "ORC_Stray_General.7z", outcome=False)

        with mock.patch.object(cache.ingest, "run_ingest"), \
                mock.patch.object(cache, "write_marker"):
            dataset = cache.ensure_dataset(self.dir.name, collection=stray)

        self.assertNotEqual(os.path.basename(dataset), "ORC_A")
        self.assertEqual(os.path.basename(dataset), cache.location_name(self.dir.name))


if __name__ == "__main__":
    unittest.main()


class TestClearKeepsTheDecryptedArchive(unittest.TestCase):
    def test_a_rebuild_removes_the_logs_and_keeps_the_archive_beside_them(self):
        with tempfile.TemporaryDirectory() as run:
            own = os.path.join(run, "ORC_H_General")
            os.makedirs(os.path.join(own, "Errors"))
            for name in ("ORC_H_General.7z", "NTFSInfo.log", os.path.join("Errors", "a.log")):
                with open(os.path.join(own, name), "wb") as f:
                    f.write(b"x")
            layout.write_products(run, [layout.ORCA_DIR,
                                        os.path.join("ORC_H_General", "NTFSInfo.log"),
                                        os.path.join("ORC_H_General", "Errors")])

            cache.clear_dataset(run)

            self.assertEqual(os.listdir(own), ["ORC_H_General.7z"])
