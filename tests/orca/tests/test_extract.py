import io
import os
import tempfile
import unittest
from contextlib import redirect_stdout
from unittest import mock
from orca import extract


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
    def test_expands_each_container_beside_itself(self):
        root = os.path.join(self.dir.name, "tree")
        self.touch("tree", "GetThis.7z")
        self.touch("tree", "sub", "GetSamples.7z")

        calls = []
        with mock.patch.object(extract.sevenzip, "extract_all",
                               lambda seven, archive, dest, pw: calls.append((archive, dest))):
            self.assertEqual(extract.expand_containers("7z", root, None), 2)

        self.assertEqual([dest for _, dest in calls],
                         [os.path.join(root, "GetThis"), os.path.join(root, "sub", "GetSamples")])

    def test_empty_container_is_skipped(self):
        root = os.path.join(self.dir.name, "tree")
        self.touch("tree", "Empty.7z", content=b"")

        with mock.patch.object(extract.sevenzip, "extract_all") as extract_all:
            self.assertEqual(extract.expand_containers("7z", root, None), 0)
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


if __name__ == "__main__":
    unittest.main()
