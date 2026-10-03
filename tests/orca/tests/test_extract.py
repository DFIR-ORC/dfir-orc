import io
import os
import tempfile
import unittest
from contextlib import redirect_stdout
from unittest import mock
from orca import extract, ingest, scratch
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

    def nesting(self, code=0):
        """An 'Outer.7z' whose contents include an 'Inner.7z'."""
        def fake(seven, archive, dest, pw):
            os.makedirs(dest, exist_ok=True)
            if os.path.basename(archive) == "Outer.7z":
                with open(os.path.join(dest, "Inner.7z"), "wb") as f:
                    f.write(b"x")
            return code

        return fake

    def test_it_keeps_going_until_no_container_is_left(self):
        # Expanding a container can reveal another; one pass over the list found at the start
        # would leave that one sitting there as an archive.
        self.touch("tree", "Outer.7z")

        with mock.patch.object(extract.sevenzip, "extract_all", self.nesting()), \
             redirect_stdout(io.StringIO()):
            count = extract.expand_containers("7z", self.root, None, self.containers)

        self.assertEqual(count, 2)
        self.assertEqual(extract.nested_containers(self.root), [])
        self.assertTrue(os.path.isfile(os.path.join(self.containers, "Outer.7z")))
        self.assertTrue(os.path.isfile(os.path.join(self.containers, "Outer", "Inner.7z")))

    def test_depth_stops_after_that_many_levels(self):
        self.touch("tree", "Outer.7z")

        with mock.patch.object(extract.sevenzip, "extract_all", self.nesting()), \
             redirect_stdout(io.StringIO()):
            count = extract.expand_containers("7z", self.root, None, self.containers, 1)

        self.assertEqual(count, 1)
        self.assertTrue(os.path.isfile(os.path.join(self.root, "Outer", "Inner.7z")))

    def test_depth_zero_opens_nothing(self):
        self.touch("tree", "GetThis.7z")

        with mock.patch.object(extract.sevenzip, "extract_all") as extract_all:
            self.assertEqual(extract.expand_containers("7z", self.root, None,
                                                       self.containers, 0), 0)
        extract_all.assert_not_called()

    def test_a_container_left_in_the_tree_is_not_tried_again(self):
        # It stays where nested_containers() finds it, so without a memory of what has been
        # tried the level loop would never end.
        self.touch("tree", "GetThis.7z")
        tried = []

        def fake(seven, archive, dest, pw):
            tried.append(archive)
            return 1

        with mock.patch.object(extract.sevenzip, "extract_all", fake), \
             redirect_stdout(io.StringIO()):
            extract.expand_containers("7z", self.root, None, self.containers)

        self.assertEqual(len(tried), 1)


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

    def test_a_second_run_in_the_same_directory_is_extracted_too(self):
        """Two ORC runs dropped on the same share: one Outcome per run, only one of them read.
        The other run's archive still has to come out, and be named as unexpected."""
        for host in ("B0", "B2"):
            outcome = (b'{"dfir-orc": {"outcome": {"command_set": ['
                       b'{"archive": {"name": "ORC_' + host.encode() + b'_General.7z"}}]}}}')
            self.touch("results", f"ORC_{host}_Outcome.json", content=outcome)
            self.touch("results", f"ORC_{host}_General.7z", content=b"7z\xbc\xaf")
        source = os.path.join(self.dir.name, "results")
        out = os.path.join(self.dir.name, "out")

        calls, printed = self.extract_calls(source, out)

        self.assertEqual([destination for _, destination in calls],
                         [os.path.join(out, "ORC_B0_General"),
                          os.path.join(out, "ORC_B2_General")])
        self.assertIn("ORC_B2_General.7z", printed)

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
                               lambda path, work, key, backend, refresh=False: decrypted), \
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

    def extract(self, source, out, payload=b"{}", work_dir=None, refresh=False):
        """Run an extract over 'source' with the CMS and 7z layers faked out.

        decrypt_archive is patched in both modules: extract imported the name, so the copy it
        calls and the one unseal_one() calls are different bindings. Both record where they
        were asked to write, which is what says whether the collection was left alone.
        """
        self.decrypted_into = []
        self.refreshed = []
        self.expanded = []

        def fake_decrypt(path, dest, private_key=None, backend=None, refresh=False):
            self.decrypted_into.append(dest)
            self.refreshed.append(refresh)
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
            extract.run_extract(source, out, private_key="k.pem", work_dir=work_dir,
                                refresh=refresh)
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

    def test_work_dir_keeps_the_decrypted_archive_under_a_stable_name(self):
        # '--work-dir' is a keep dir, as it is for an ingest: the next extract of the same
        # collection finds the archive there instead of paying for the decrypt again.
        source = self.collection("ORC_H_1441.log")
        out = os.path.join(self.dir.name, "out")
        work = os.path.join(self.dir.name, "work")
        self.addCleanup(scratch.use, None)

        self.extract(source, out, work_dir=work)

        # The archive lands in the keep dir itself. The sidecar's CMS work is transient and
        # stays scratch - moved under the keep dir only because that is where the room is.
        self.assertIn(work, self.decrypted_into)
        for dest in self.decrypted_into:
            self.assertTrue(dest == work or dest.startswith(os.path.join(work, "orca")), dest)

        scratch.cleanup()
        self.assertTrue(os.path.isfile(os.path.join(work, "ORC_H_1441_General.7z")))
        self.assertEqual([n for n in os.listdir(out) if n.lower().endswith(".7z")], [])

    def test_refresh_decrypts_the_archive_again(self):
        # The reuse guard is cheap by design - a file that exists and looks like a 7z - so a
        # run interrupted mid-copy leaves something it accepts. --refresh says no to it.
        source = self.collection("ORC_H_1441.log")
        out = os.path.join(self.dir.name, "out")

        self.extract(source, out, refresh=True)

        # The sidecar's own decrypt goes to a fresh scratch directory every time, so it has
        # nothing to reuse and nothing to refresh; the archive is the one with a kept copy.
        self.assertTrue(self.refreshed[-1])

    def test_refresh_unseals_a_sidecar_that_is_already_there(self):
        source = self.collection("ORC_H_1441.log")
        out = os.path.join(self.dir.name, "out")
        self.extract(source, out, payload=b"first")

        self.extract(source, out, payload=b"second", refresh=True)

        with open(os.path.join(out, "ORC_H_1441.log"), "rb") as f:
            self.assertEqual(f.read(), b"second")

    def test_without_refresh_a_sidecar_already_there_is_left_alone(self):
        source = self.collection("ORC_H_1441.log")
        out = os.path.join(self.dir.name, "out")
        self.extract(source, out, payload=b"first")

        self.extract(source, out, payload=b"second")

        with open(os.path.join(out, "ORC_H_1441.log"), "rb") as f:
            self.assertEqual(f.read(), b"first")
        self.assertEqual(self.refreshed, [False])

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
                               lambda path, dest, key, backend, refresh=False: self.touch("plain", "a.7z")), \
             mock.patch.object(extract.sevenzip, "extract_all") as extract_all, \
             redirect_stdout(printed):
            extract.run_extract(source, out, private_key="k.pem")

        self.assertIn("could not unseal", printed.getvalue())
        extract_all.assert_called_once()


class TestRunExtractDepth(ExtractCase):
    """'--depth' counts layers of container unwrapped, the '.p7b' envelope included, so
    '--depth 1' on an encrypted collection means 'just decrypt it'."""

    def extract(self, depth=None, work_dir=None, sealed=True):
        name = "ORC_G.7z.p7b" if sealed else "ORC_G.7z"
        source = os.path.dirname(self.touch("results", name, content=b"ciphertext"))
        out = os.path.join(self.dir.name, "out")
        self.addCleanup(scratch.use, None)

        def fake_decrypt(path, dest, private_key=None, backend=None, refresh=False):
            os.makedirs(dest, exist_ok=True)
            plain = os.path.join(dest, extract.layout.archive_stem(path) + ".7z")
            with open(plain, "wb") as f:
                f.write(b"7z\xbc\xaf")
            return plain

        def fake_extract_all(seven, archive, dest, pw):
            # Three layers to unwrap: the envelope, the archive, and the container it holds.
            os.makedirs(dest, exist_ok=True)
            leaf = "GetSamples.7z" if os.path.basename(archive) == "ORC_G.7z" else "GetThis.csv"
            with open(os.path.join(dest, leaf), "wb") as f:
                f.write(b"x")
            return 0

        with mock.patch.object(extract.sevenzip, "find_7z", lambda: "7z"), \
             mock.patch.object(extract, "decrypt_archive", fake_decrypt), \
             mock.patch.object(ingest, "decrypt_archive", fake_decrypt), \
             mock.patch.object(extract.sevenzip, "extract_all", fake_extract_all), \
             redirect_stdout(io.StringIO()):
            extract.run_extract(source, out, private_key="k.pem", depth=depth,
                                work_dir=work_dir)
        return out

    def test_one_layer_stops_after_the_decrypt(self):
        out = self.extract(depth=1)

        # The archive is what that layer produced, so it is the product and belongs in --out.
        self.assertTrue(os.path.isfile(os.path.join(out, "ORC_G.7z")))
        self.assertFalse(os.path.exists(os.path.join(out, "ORC_G")))

    def test_two_layers_expand_the_archive_and_leave_its_containers(self):
        out = self.extract(depth=2)

        self.assertTrue(os.path.isfile(os.path.join(out, "ORC_G", "GetSamples.7z")))
        self.assertFalse(os.path.exists(os.path.join(out, "ORC_G", "GetSamples")))
        self.assertFalse(os.path.exists(os.path.join(out, "ORC_G.7z")))

    def test_three_layers_expand_the_containers_too(self):
        out = self.extract(depth=3)

        self.assertTrue(os.path.isfile(os.path.join(out, "ORC_G", "GetSamples", "GetThis.csv")))
        self.assertFalse(os.path.exists(os.path.join(out, "ORC_G", "GetSamples.7z")))

    def test_unlimited_is_the_default(self):
        out = self.extract()

        self.assertTrue(os.path.isfile(os.path.join(out, "ORC_G", "GetSamples", "GetThis.csv")))
        self.assertEqual(extract.nested_containers(out), [])

    def test_a_depth_below_one_is_refused(self):
        # There is no such thing as unwrapping zero layers: that is not running the command.
        with self.assertRaises(RuntimeError):
            self.extract(depth=0)

    def test_a_plain_collection_has_no_envelope_to_open_first(self):
        # The count is of layers actually present, so the same number means different things:
        # depth 1 is the decrypt when there is one, and the archive itself when there is not.
        out = self.extract(depth=1, sealed=False)

        self.assertTrue(os.path.isfile(os.path.join(out, "ORC_G", "GetSamples.7z")))
        self.assertFalse(os.path.exists(os.path.join(out, "ORC_G", "GetSamples")))

    def test_a_plain_collection_expands_its_containers_one_layer_later(self):
        out = self.extract(depth=2, sealed=False)

        self.assertTrue(os.path.isfile(os.path.join(out, "ORC_G", "GetSamples", "GetThis.csv")))
        self.assertFalse(os.path.exists(os.path.join(out, "ORC_G", "GetSamples.7z")))

    def test_unlimited_on_a_plain_collection_leaves_no_container(self):
        out = self.extract(sealed=False)

        self.assertTrue(os.path.isfile(os.path.join(out, "ORC_G", "GetSamples", "GetThis.csv")))
        self.assertEqual(extract.nested_containers(out), [])

    def test_a_plain_collection_is_never_delivered_out_of_itself(self):
        # decrypt_archive() returns a plain archive untouched, so the path an unwrap holds IS
        # the collection's own file. Delivering that would move the evidence into the tree.
        out = self.extract(depth=1, sealed=False)

        self.assertTrue(os.path.isfile(os.path.join(self.dir.name, "results", "ORC_G.7z")))
        self.assertFalse(os.path.exists(os.path.join(out, "ORC_G.7z")))

    def test_the_stopped_at_archive_is_copied_out_of_a_work_dir_not_moved(self):
        # The work dir's copy is what a later run reuses, so it has to survive delivery.
        work = os.path.join(self.dir.name, "work")

        out = self.extract(depth=1, work_dir=work)

        self.assertTrue(os.path.isfile(os.path.join(out, "ORC_G.7z")))
        self.assertTrue(os.path.isfile(os.path.join(work, "ORC_G.7z")))


class TestStrictExtract(unittest.TestCase):
    """What 'extract --strict' refuses to carry on past.

    Carrying on is right by default - the other archives of a collection are independent
    deliverables, and one missing file should not cost them - but a collection you are about to
    rely on is worth a run that stops at the first thing it could not open.
    """

    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)
        self.out = os.path.join(self.dir.name, "out")
        os.makedirs(self.out)

    def test_a_missing_archive_stops_a_strict_extract(self):
        with self.assertRaises(RuntimeError) as caught:
            extract.check_archive_readable(os.path.join(self.dir.name, "absent.7z"),
                                           strict=True)

        self.assertIn("absent.7z", str(caught.exception))

    def test_an_empty_archive_stops_a_strict_extract(self):
        empty = os.path.join(self.dir.name, "Empty.7z")
        with open(empty, "wb"):
            pass

        with self.assertRaises(RuntimeError) as caught:
            extract.check_archive_readable(empty, strict=True)

        self.assertIn("Empty.7z", str(caught.exception))

    def test_by_default_both_are_only_reported(self):
        empty = os.path.join(self.dir.name, "Empty.7z")
        with open(empty, "wb"):
            pass

        out = io.StringIO()
        with redirect_stdout(out):
            missing = extract.check_archive_readable(
                os.path.join(self.dir.name, "absent.7z"), strict=False)
            blank = extract.check_archive_readable(empty, strict=False)

        self.assertFalse(missing)
        self.assertFalse(blank)
        self.assertIn("absent.7z", out.getvalue())
        self.assertIn("Empty.7z", out.getvalue())

    def test_a_sound_archive_is_readable_either_way(self):
        archive = os.path.join(self.dir.name, "Good.7z")
        with open(archive, "wb") as f:
            f.write(b"7z\xbc\xaf\x27\x1c" + b"x" * 64)

        self.assertTrue(extract.check_archive_readable(archive, strict=True))
        self.assertTrue(extract.check_archive_readable(archive, strict=False))

    def test_a_container_7zip_warned_about_stops_a_strict_extract(self):
        with self.assertRaises(RuntimeError) as caught:
            extract.handle_container_warning("GetSamples.7z", strict=True)

        self.assertIn("GetSamples.7z", str(caught.exception))

    def test_by_default_such_a_container_is_kept_in_the_tree(self):
        out = io.StringIO()
        with redirect_stdout(out):
            extract.handle_container_warning("GetSamples.7z", strict=False)

        self.assertIn("GetSamples.7z", out.getvalue())


if __name__ == "__main__":
    unittest.main()
