import os
import tempfile
import unittest
from orca import layout


def touch(directory, name):
    with open(os.path.join(directory, name), "wb") as f:
        f.write(b"x")


class TestRunName(unittest.TestCase):
    """A collection ingests as one run, so its directory is named as ORC named the run."""

    def test_from_the_outcome(self):
        with tempfile.TemporaryDirectory() as d:
            touch(d, "ORC_WorkStation_H2ODEV_20260921_172528_Outcome.json")
            touch(d, "ORC_WorkStation_H2ODEV_Offline.7z")
            self.assertEqual(layout.run_name(d), "ORC_WorkStation_H2ODEV_20260921_172528")

    def test_a_sealed_outcome_gives_the_same_name(self):
        # The run directory must not move the moment a collection is unsealed.
        with tempfile.TemporaryDirectory() as d:
            touch(d, "ORC_WorkStation_H2ODEV_20260921_172528_Outcome.json.7z.p7b")
            self.assertEqual(layout.run_name(d), "ORC_WorkStation_H2ODEV_20260921_172528")

    def test_the_outline_answers_when_there_is_no_outcome(self):
        with tempfile.TemporaryDirectory() as d:
            touch(d, "ORC_H_172528_Outline.json")
            self.assertEqual(layout.run_name(d), "ORC_H_172528")

    def test_the_outcome_wins_over_the_outline(self):
        with tempfile.TemporaryDirectory() as d:
            touch(d, "A_Outline.json")
            touch(d, "B_Outcome.json")
            self.assertEqual(layout.run_name(d), "B")

    def test_loose_archives_are_named_after_their_directory(self):
        with tempfile.TemporaryDirectory() as d:
            collection = os.path.join(d, "dumps")
            os.makedirs(collection)
            touch(collection, "SystemInfo.7z")
            self.assertEqual(layout.run_name(collection), "dumps")


class TestArchiveStem(unittest.TestCase):
    """The name rule: keep the encrypted file's name, drop only the archive extensions."""

    def test_plain_archive(self):
        self.assertEqual(layout.archive_stem("ORC_WS_Offline.7z"), "ORC_WS_Offline")

    def test_sealed_archive_drops_both_extensions(self):
        self.assertEqual(layout.archive_stem("ORC_WS_Offline.7z.p7b"), "ORC_WS_Offline")

    def test_sealed_sidecar_keeps_its_own_extension(self):
        # '.json' is not an archive extension, so it survives - that is what makes the
        # unsealed outcome land as 'ORC_Outcome.json' and not as 'ORC_Outcome'.
        self.assertEqual(layout.archive_stem("ORC_172528_Outcome.json.7z.p7b"),
                         "ORC_172528_Outcome.json")
        self.assertEqual(layout.archive_stem("ORC_172528.log.7z.p7b"), "ORC_172528.log")

    def test_journal_stream(self):
        self.assertEqual(layout.archive_stem("Little.7zs"), "Little")

    def test_full_path_keeps_only_the_name(self):
        self.assertEqual(layout.archive_stem(os.path.join("S:", "runs", "A",
                                                          "ORC_WS_Offline.7z.p7b")),
                         "ORC_WS_Offline")

    def test_a_name_that_is_only_an_extension_is_left_alone(self):
        self.assertEqual(layout.archive_stem(".7z"), ".7z")


class TestDerivedDir(unittest.TestCase):
    def test_an_archive_derives_beside_itself(self):
        got = layout.derived_dir(os.path.join("S:", "runs", "A", "ORC_WS_Offline.7z.p7b"))
        self.assertEqual(os.path.basename(got), "ORC_WS_Offline")
        self.assertEqual(os.path.dirname(got),
                         os.path.abspath(os.path.join("S:", "runs", "A")))

    def test_a_collection_directory_derives_into_a_run_of_its_own_name(self):
        with tempfile.TemporaryDirectory() as d:
            touch(d, "ORC_WS_H2ODEV_172528_Outcome.json")
            self.assertEqual(layout.derived_dir(d),
                             os.path.join(d, "ORC_WS_H2ODEV_172528"))

    def test_work_dir_overrides_the_location(self):
        got = layout.derived_dir(os.path.join("S:", "runs", "A", "ORC_WS_Offline.7z"),
                                 work_dir=os.path.join("E:", "work"))
        self.assertEqual(got, os.path.join("E:", "work", "ORC_WS_Offline"))

    def test_work_dir_takes_a_name_that_keeps_two_runs_apart(self):
        # NEW\ORC_WS_Offline.7z and REF\ORC_WS_Offline.7z share a basename; beside the archive
        # they cannot collide, but under one --work-dir they would.
        got = layout.derived_dir(os.path.join("S:", "runs", "A", "ORC_WS_Offline.7z"),
                                 work_dir=os.path.join("E:", "work"), name="ORC_WS_Offline-abc")
        self.assertEqual(got, os.path.join("E:", "work", "ORC_WS_Offline-abc"))


class TestDerivedPaths(unittest.TestCase):
    def test_decrypted_archive_sits_beside_the_encrypted_one(self):
        sealed = os.path.join("S:", "runs", "A", "ORC_WS_Offline.7z.p7b")
        self.assertEqual(layout.decrypted_path(sealed),
                         os.path.join(os.path.abspath(os.path.join("S:", "runs", "A")),
                                      "ORC_WS_Offline.7z"))

    def test_decrypted_archive_can_be_redirected(self):
        sealed = os.path.join("S:", "runs", "A", "ORC_WS_Offline.7z.p7b")
        self.assertEqual(layout.decrypted_path(sealed, os.path.join("E:", "work")),
                         os.path.join("E:", "work", "ORC_WS_Offline.7z"))

    def test_unsealed_sidecar_sits_beside_the_sealed_one(self):
        sealed = os.path.join("S:", "runs", "A", "ORC_172528_Outcome.json.7z.p7b")
        self.assertEqual(layout.unsealed_path(sealed),
                         os.path.join(os.path.abspath(os.path.join("S:", "runs", "A")),
                                      "ORC_172528_Outcome.json"))

    def test_unsealed_sidecar_can_be_redirected(self):
        sealed = os.path.join("S:", "runs", "A", "ORC_172528.log.7z.p7b")
        self.assertEqual(layout.unsealed_path(sealed, os.path.join("E:", "work")),
                         os.path.join("E:", "work", "ORC_172528.log"))


if __name__ == "__main__":
    unittest.main()
