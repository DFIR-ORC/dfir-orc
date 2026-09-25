import io
import os
import tempfile
import unittest
from contextlib import redirect_stdout
from unittest import mock
from orca import extract, ingest
from orca.sevenzip import ArchiveEntry


class ExtractCase(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)

    def touch(self, *parts, content=b"x"):
        path = os.path.join(self.dir.name, *parts)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as f:
            f.write(content)
        return path


class TestNestedContainers(ExtractCase):
    def test_finds_containers_at_every_depth(self):
        self.touch("tree", "GetThis.7z")
        self.touch("tree", "sub", "GetSamples.7z")
        self.touch("tree", "NTFSInfo.csv")

        found = extract.nested_containers(os.path.join(self.dir.name, "tree"))
        self.assertEqual([os.path.basename(p) for p in found], ["GetThis.7z", "GetSamples.7z"])

    def test_empty_tree(self):
        os.makedirs(os.path.join(self.dir.name, "tree"))
        self.assertEqual(extract.nested_containers(os.path.join(self.dir.name, "tree")), [])


class TestContainerPassword(ExtractCase):
    def test_explicit_password_wins(self):
        container = self.touch("tree", "GetThis.7z")
        self.touch("tree", "GetThis_cli_config.xml",
                   content=b"<getthis><output password='fromxml'/></getthis>")
        self.assertEqual(extract.container_password(container, "typed"), "typed")

    def test_falls_back_to_the_cli_config(self):
        container = self.touch("tree", "GetThis.7z")
        self.touch("tree", "GetThis_cli_config.xml",
                   content=b"<getthis><output password='fromxml'/></getthis>")
        self.assertEqual(extract.container_password(container, None), "fromxml")

    def test_none_when_there_is_no_config(self):
        container = self.touch("tree", "GetThis.7z")
        self.assertIsNone(extract.container_password(container, None))


class TestExpandContainers(ExtractCase):
    def setUp(self):
        super().setUp()
        self.root = os.path.join(self.dir.name, "tree")
        self.containers = os.path.join(self.dir.name, "work", "DFIR-ORC_HOST")

    def expand(self, code=0, calls=None):
        def fake(seven, archive, dest, pw):
            if calls is not None:
                calls.append((archive, dest))
            return code

        printed = io.StringIO()
        with mock.patch.object(extract.sevenzip, "extract_all", fake), \
             redirect_stdout(printed):
            count = extract.expand_containers("7z", self.root, None, self.containers)
        return count, printed.getvalue()

    def test_expands_each_container_beside_itself(self):
        self.touch("tree", "GetThis.7z")
        self.touch("tree", "sub", "GetSamples.7z")

        calls = []
        count, _ = self.expand(calls=calls)

        self.assertEqual(count, 2)
        self.assertEqual([dest for _, dest in calls],
                         [os.path.join(self.root, "GetThis"),
                          os.path.join(self.root, "sub", "GetSamples")])

    def test_a_container_that_expanded_cleanly_leaves_the_tree(self):
        self.touch("tree", "GetThis.7z")
        self.touch("tree", "sub", "GetSamples.7z")

        self.expand()

        # Everything they held is in the tree now, so the archives are intermediates.
        self.assertEqual(extract.nested_containers(self.root), [])
        self.assertTrue(os.path.isfile(os.path.join(self.containers, "GetThis.7z")))
        # The path within the tree is kept, so two command sets' containers cannot collide.
        self.assertTrue(os.path.isfile(os.path.join(self.containers, "sub", "GetSamples.7z")))

    def test_the_expanded_directory_stays_where_it_is(self):
        self.touch("tree", "GetThis.7z")

        self.expand()

        self.assertTrue(os.path.isdir(os.path.join(self.root, "GetThis")))

    def test_a_container_7z_warned_about_stays_in_the_tree(self):
        # Exit 1 on an extract means entries did not come out, and this archive is the only
        # copy of them: a messy tree beats a lost entry.
        container = self.touch("tree", "GetThis.7z")

        count, printed = self.expand(code=1)

        self.assertEqual(count, 1)
        self.assertTrue(os.path.isfile(container))
        self.assertFalse(os.path.exists(os.path.join(self.containers, "GetThis.7z")))
        self.assertIn("warnings", printed)

    def test_empty_container_is_skipped(self):
        self.touch("tree", "Empty.7z", content=b"")

        with mock.patch.object(extract.sevenzip, "extract_all") as extract_all:
            self.assertEqual(extract.expand_containers("7z", self.root, None,
                                                       self.containers), 0)
        extract_all.assert_not_called()


class TestRunExtract(ExtractCase):
    # An Outcome naming two command sets, reduced to what orca reads out of it.
    OUTCOME_TWO = (b'{"dfir-orc": {"outcome": {"command_set": ['
                   b'{"archive": {"name": "ORC_H_General.7z"}}, '
                   b'{"archive": {"name": "ORC_H_Little.7z"}}]}}}')

    def extract_calls(self, source, out):
        """(the (archive, destination) pairs 7z was asked for, what the extract printed)."""
        calls = []
        printed = io.StringIO()
        with mock.patch.object(extract.sevenzip, "find_7z", lambda: "7z"), \
             mock.patch.object(extract.sevenzip, "extract_all",
                               lambda seven, a, dest, pw: calls.append((a, dest))), \
             redirect_stdout(printed):
            extract.run_extract(source, out)
        return calls, printed.getvalue()

    def test_extracts_a_top_archive_into_its_own_directory(self):
        archive = self.touch("results", "DFIR-ORC_HOST.7z", content=b"7z\xbc\xaf")
        out = os.path.join(self.dir.name, "out")
        calls = []

        with mock.patch.object(extract.sevenzip, "find_7z", lambda: "7z"), \
             mock.patch.object(extract.sevenzip, "extract_all",
                               lambda seven, a, dest, pw: calls.append((a, dest))):
            extract.run_extract(archive, out)

        self.assertEqual(calls, [(archive, os.path.join(out, "DFIR-ORC_HOST"))])

    def test_every_archive_of_a_collection_is_extracted(self):
        """ORC writes one archive per command set, so a collection directory holds several and
        an extract owes the caller all of them, each in a directory of its own."""
        source = os.path.dirname(self.touch("results", "ORC_H_General.7z",
                                            content=b"7z\xbc\xaf"))
        self.touch("results", "ORC_H_Little.7z", content=b"7z\xbc\xaf")
        out = os.path.join(self.dir.name, "out")

        calls, _ = self.extract_calls(source, out)

        self.assertEqual(calls, [(os.path.join(source, "ORC_H_General.7z"),
                                  os.path.join(out, "ORC_H_General")),
                                 (os.path.join(source, "ORC_H_Little.7z"),
                                  os.path.join(out, "ORC_H_Little"))])

    def test_every_archive_the_outcome_names_is_extracted(self):
        source = os.path.dirname(self.touch("results", "ORC_H_Outcome.json",
                                            content=self.OUTCOME_TWO))
        for name in ("ORC_H_General.7z", "ORC_H_Little.7z"):
            self.touch("results", name, content=b"7z\xbc\xaf")
        out = os.path.join(self.dir.name, "out")

        calls, _ = self.extract_calls(source, out)

        self.assertEqual([destination for _, destination in calls],
                         [os.path.join(out, "ORC_H_General"),
                          os.path.join(out, "ORC_H_Little")])

    def test_a_missing_archive_is_skipped_and_the_others_still_extract(self):
        """A partial collection - an archive the Outcome names that nobody kept - is named out
        loud and the rest still comes out, rather than the first gap aborting the extract."""
        outcome = self.OUTCOME_TWO.replace(b"ORC_H_General.7z", b"ORC_H_Missing.7z")
        source = os.path.dirname(self.touch("results", "ORC_H_Outcome.json", content=outcome))
        self.touch("results", "ORC_H_Little.7z", content=b"7z\xbc\xaf")
        out = os.path.join(self.dir.name, "out")

        calls, printed = self.extract_calls(source, out)

        self.assertEqual([destination for _, destination in calls],
                         [os.path.join(out, "ORC_H_Little")])
        self.assertIn("ORC_H_Missing.7z", printed)

    def test_empty_archive_is_skipped(self):
        archive = self.touch("results", "Empty.7z", content=b"")
        out = os.path.join(self.dir.name, "out")

        with mock.patch.object(extract.sevenzip, "find_7z", lambda: "7z"), \
             mock.patch.object(extract.sevenzip, "extract_all") as extract_all:
            extract.run_extract(archive, out)
        extract_all.assert_not_called()

    def test_p7b_is_decrypted_before_extraction(self):
        archive = self.touch("results", "DFIR-ORC_HOST.p7b", content=b"ciphertext")
        out = os.path.join(self.dir.name, "out")
        decrypted = self.touch("plain", "DFIR-ORC_HOST.7z", content=b"7z\xbc\xaf")
        calls = []

        with mock.patch.object(extract.sevenzip, "find_7z", lambda: "7z"), \
             mock.patch.object(extract, "decrypt_archive",
                               lambda path, work, key, backend: decrypted), \
             mock.patch.object(extract.sevenzip, "extract_all",
                               lambda seven, a, dest, pw: calls.append((a, dest))):
            extract.run_extract(archive, out, private_key="k.pem")

        # Extraction reads the decrypted archive, but the output keeps the original name.
        self.assertEqual(calls, [(decrypted, os.path.join(out, "DFIR-ORC_HOST"))])

    def test_missing_7z_fails_closed(self):
        with mock.patch.object(extract.sevenzip, "find_7z", lambda: None):
            with self.assertRaises(RuntimeError):
                extract.run_extract(self.dir.name, os.path.join(self.dir.name, "out"))


class TestRunExtractSidecars(ExtractCase):
    """The collection's own files - the Outcome, the Outlines, WolfLauncher's log - are in no
    archive, and an encrypted one seals them into '<name>.7z.p7b' beside the archives. They
    have to be put into the extracted tree explicitly, and the collection itself has to come
    out of an extract byte for byte as it went in."""

    # What a real Outcome holds, reduced to the two things orca reads out of it: that it is an
    # Outcome at all, and the archive it names.
    OUTCOME = (b'{"dfir-orc": {"outcome": {"command_set": '
               b'[{"archive": {"name": "ORC_H_1441_General.7z.p7b"}}]}}}')

    SIDECARS = ("ORC_H_1441_Outcome.json", "ORC_H_1441_Outline.json", "ORC_H_1441.log")

    def collection(self, *sealed, plain=None):
        """A directory holding one sealed archive plus the named sealed (and plain) sidecars."""
        source = os.path.dirname(self.touch("results", "ORC_H_1441_General.7z.p7b",
                                            content=b"ciphertext"))
        for name in sealed:
            self.touch("results", name + ".7z.p7b", content=b"ciphertext")
        for name, content in (plain or {}).items():
            self.touch("results", name, content=content)
        return source

    def extract(self, source, out, payload=b"{}"):
        """Run an extract over 'source' with the CMS and 7z layers faked out.

        decrypt_archive is patched in both modules: extract imported the name, so the copy it
        calls and the one unseal_one() calls are different bindings. Both record where they
        were asked to write, which is what says whether the collection was left alone.
        """
        self.decrypted_into = []
        self.expanded = []

        def fake_decrypt(path, dest, private_key=None, backend=None, refresh=False):
            self.decrypted_into.append(dest)
            os.makedirs(dest, exist_ok=True)
            plain = os.path.join(dest, extract.layout.archive_stem(path) + ".7z")
            with open(plain, "wb") as f:
                f.write(b"7z\xbc\xaf")
            return plain

        def fake_extract_entry(seven, archive, entry_path, dest, password=None):
            produced = os.path.join(dest, entry_path)
            with open(produced, "wb") as f:
                f.write(payload)
            return produced

        entry = ArchiveEntry(path="inner", size=1, crc="x", attributes="A")
        printed = io.StringIO()
        with mock.patch.object(extract.sevenzip, "find_7z", lambda: "7z"), \
             mock.patch.object(extract, "decrypt_archive", fake_decrypt), \
             mock.patch.object(ingest, "decrypt_archive", fake_decrypt), \
             mock.patch.object(extract.sevenzip, "list_entries", lambda *a, **k: [entry]), \
             mock.patch.object(extract.sevenzip, "extract_entry", fake_extract_entry), \
             mock.patch.object(extract.sevenzip, "extract_all",
                               lambda seven, a, dest, pw: self.expanded.append(dest)), \
             redirect_stdout(printed):
            extract.run_extract(source, out, private_key="k.pem")
        return printed.getvalue()

    def test_the_sidecars_land_in_the_out_directory(self):
        source = self.collection(*self.SIDECARS)
        out = os.path.join(self.dir.name, "out")

        self.extract(source, out, payload=self.OUTCOME)

        # The extracted tree is what gets handed on, so it carries the collection's own files
        # as well as the archive contents.
        for name in self.SIDECARS:
            self.assertTrue(os.path.exists(os.path.join(out, name)), name)

    def test_the_collection_is_left_exactly_as_it_was(self):
        source = self.collection(*self.SIDECARS)
        out = os.path.join(self.dir.name, "out")
        before = sorted(os.listdir(source))

        self.extract(source, out, payload=self.OUTCOME)

        # Not one unsealed sidecar, not one decrypted '.7z', nothing: '--out' elsewhere means
        # the collection is read, never written.
        self.assertEqual(sorted(os.listdir(source)), before)

    def test_the_decrypted_archive_goes_to_scratch_not_to_the_tree(self):
        source = self.collection()
        out = os.path.join(self.dir.name, "out")

        self.extract(source, out)

        # A full-size intermediate the expanded tree supersedes: it has no business in the
        # deliverable, and none in the collection either.
        self.assertNotIn(out, self.decrypted_into)
        self.assertNotIn(source, self.decrypted_into)
        self.assertEqual([n for n in os.listdir(out) if n.lower().endswith(".7z")], [])

    def test_a_cleartext_sidecar_is_copied_rather_than_decrypted_again(self):
        # An unencrypted collection, or one an earlier run already unsealed in place.
        source = self.collection("ORC_H_1441.log", plain={"ORC_H_1441.log": b"cleartext"})
        out = os.path.join(self.dir.name, "out")

        self.extract(source, out, payload=b"decrypted again")

        with open(os.path.join(out, "ORC_H_1441.log"), "rb") as f:
            self.assertEqual(f.read(), b"cleartext")

    def test_out_pointing_at_the_collection_is_not_a_copy_onto_itself(self):
        # 'orca extract <dir> --out <same dir>': the sidecars unseal into that very directory,
        # so copying them in is a no-op, not an error.
        source = self.collection("ORC_H_1441_Outline.json", "ORC_H_1441.log")

        self.extract(source, source)

        for name in ("ORC_H_1441_Outline.json", "ORC_H_1441.log"):
            path = os.path.join(source, name)
            self.assertTrue(os.path.exists(path), name)
            self.assertEqual(os.path.getsize(path), len(b"{}"))

    def test_the_archive_is_named_by_the_outcome_unsealed_into_the_tree(self):
        source = self.collection("ORC_H_1441_Outcome.json")
        out = os.path.join(self.dir.name, "out")

        self.extract(source, out, payload=self.OUTCOME)

        # The Outcome is only readable in 'out', so resolve_archives() has to look there for
        # it while still resolving the names it holds against the collection.
        self.assertTrue(os.path.exists(os.path.join(out, "ORC_H_1441_Outcome.json")))
        self.assertEqual(self.expanded, [os.path.join(out, "ORC_H_1441_General")])

    def test_a_sidecar_is_never_mistaken_for_an_archive(self):
        source = self.collection("ORC_H_1441.log")
        out = os.path.join(self.dir.name, "out")

        self.extract(source, out)

        self.assertEqual(self.expanded, [os.path.join(out, "ORC_H_1441_General")])

    def test_an_unopenable_sidecar_does_not_abort_the_extract(self):
        source = self.collection("ORC_H_1441.log")
        out = os.path.join(self.dir.name, "out")

        def refuse_sidecar(path, dest, private_key=None, backend=None, refresh=False):
            raise RuntimeError("no backend could decrypt")

        printed = io.StringIO()
        with mock.patch.object(extract.sevenzip, "find_7z", lambda: "7z"), \
             mock.patch.object(ingest, "decrypt_archive", refuse_sidecar), \
             mock.patch.object(extract, "decrypt_archive",
                               lambda path, dest, key, backend: self.touch("plain", "a.7z")), \
             mock.patch.object(extract.sevenzip, "extract_all") as extract_all, \
             redirect_stdout(printed):
            extract.run_extract(source, out, private_key="k.pem")

        self.assertIn("could not unseal", printed.getvalue())
        extract_all.assert_called_once()


if __name__ == "__main__":
    unittest.main()
