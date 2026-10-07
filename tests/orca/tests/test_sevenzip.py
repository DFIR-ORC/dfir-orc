import os
import shutil
import subprocess
import tempfile
import unittest
from unittest import mock
from orca import sevenzip

SEVEN_ZIP = sevenzip.find_7z()

LISTING = """
7-Zip 24.09
Listing archive: x.7z
--
Path = x.7z
Type = 7z
----------
Path = NTFSInfo.csv
Size = 1234
Modified = 2026-06-25 00:43:03.1250000
CRC = ABCD1234
Attributes = A

Path = sub\\dir
Size =
Attributes = D

Path = SystemInfo.csv
Size = 50
CRC = 0000FFFF
Attributes = A
"""


class TestParse(unittest.TestCase):
    def test_parses_file_entries_only(self):
        entries = sevenzip.parse_listing(LISTING.splitlines())
        paths = [e.path for e in entries]
        self.assertEqual(paths, ["NTFSInfo.csv", "SystemInfo.csv"])  # directory skipped

    def test_fields(self):
        e = sevenzip.parse_listing(LISTING.splitlines())[0]
        self.assertEqual(e.size, 1234)
        self.assertEqual(e.crc, "ABCD1234")

    def test_empty_input_is_empty(self):
        self.assertEqual(sevenzip.parse_listing([]), [])

    def test_captures_modified(self):
        e = sevenzip.parse_listing(LISTING.splitlines())[0]
        self.assertEqual(e.modified, "2026-06-25 00:43:03.1250000")


class TestExtractTolerance(unittest.TestCase):
    # 7-Zip on Windows returns code 1 (warning) where p7zip returns 0; the extract must
    # succeed as long as the file lands, and fail closed with 7z's message otherwise.
    def test_warning_code_ok_when_file_present(self):
        with tempfile.TemporaryDirectory() as d:
            open(os.path.join(d, "x.csv"), "w").close()
            with mock.patch("orca.sevenzip.subprocess.run",
                            return_value=mock.Mock(returncode=1, stdout="", stderr="")):
                out = sevenzip.extract_entry("7z", "a.7z", "x.csv", d)
            self.assertEqual(out, os.path.join(d, "x.csv"))

    def test_missing_file_raises_with_message(self):
        with tempfile.TemporaryDirectory() as d:
            with mock.patch("orca.sevenzip.subprocess.run",
                            return_value=mock.Mock(returncode=2, stdout="", stderr="Wrong password?")):
                with self.assertRaises(RuntimeError) as ctx:
                    sevenzip.extract_entry("7z", "a.7z", "x.csv", d)
            self.assertIn("Wrong password?", str(ctx.exception))

    def test_extract_all_tolerates_warning_but_reports_it(self):
        # The caller has to be able to tell 'everything came out' from 'most of it did'.
        with mock.patch("orca.sevenzip.subprocess.run",
                        return_value=mock.Mock(returncode=1, stdout="", stderr="")):
            self.assertEqual(sevenzip.extract_all("7z", "a.7z", "dest"), 1)  # no raise

    def test_extract_all_reports_a_clean_extract(self):
        with mock.patch("orca.sevenzip.subprocess.run",
                        return_value=mock.Mock(returncode=0, stdout="", stderr="")):
            self.assertEqual(sevenzip.extract_all("7z", "a.7z", "dest"), 0)

    def test_extract_all_raises_on_fatal(self):
        with mock.patch("orca.sevenzip.subprocess.run",
                        return_value=mock.Mock(returncode=2, stdout="", stderr="bad")):
            with self.assertRaises(RuntimeError):
                sevenzip.extract_all("7z", "a.7z", "dest")


class TestNeverPrompts(unittest.TestCase):
    """Without '-p' and with stdin open, 7-Zip prompts for an encrypted archive's password and
    waits - the suite hung on 'ORC_H_Locked.7z', and so would a report of a protected archive."""

    def test_every_call_closes_stdin_and_passes_a_password(self):
        calls = (lambda: sevenzip.list_entries("7z", "a.7z"),
                 lambda: sevenzip.test_archive("7z", "a.7z"),
                 lambda: sevenzip.extract_all("7z", "a.7z", "dest"),
                 lambda: sevenzip.extract_matching("7z", "a.7z", "*.log", "dest"))
        for call in calls:
            with mock.patch("orca.sevenzip.subprocess.run",
                            return_value=mock.Mock(returncode=0, stdout="", stderr="")) as run:
                call()
            self.assertIs(run.call_args.kwargs["stdin"], subprocess.DEVNULL)
            self.assertIn("-p", run.call_args.args[0])

    def test_the_given_password_is_the_one_passed(self):
        with mock.patch("orca.sevenzip.subprocess.run",
                        return_value=mock.Mock(returncode=0, stdout="", stderr="")) as run:
            sevenzip.extract_all("7z", "a.7z", "dest", "S3cret")
        self.assertIn("-pS3cret", run.call_args.args[0])


class TestIsArchive(unittest.TestCase):
    def blob(self, content: bytes) -> str:
        path = os.path.join(self.dir.name, "blob")
        with open(path, "wb") as f:
            f.write(content)
        return path

    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)

    def test_recognizes_the_signature(self):
        self.assertTrue(sevenzip.is_archive(self.blob(b"7z\xbc\xaf\x27\x1c\x00\x04rest")))

    def test_rejects_a_journal(self):
        # The other thing a decrypted '.p7b' can legitimately be.
        self.assertFalse(sevenzip.is_archive(self.blob(b"JRNL\x02\x00\x00\x00")))

    def test_rejects_garbage(self):
        self.assertFalse(sevenzip.is_archive(self.blob(b"\xb0f5\x06\xd1\xbc@\x9e")))

    def test_rejects_a_truncated_signature(self):
        self.assertFalse(sevenzip.is_archive(self.blob(b"7z\xbc")))

    def test_a_missing_file_is_not_an_archive(self):
        self.assertFalse(sevenzip.is_archive(os.path.join(self.dir.name, "absent")))


# '7z t' output, verbatim from 7-Zip 26.03: a data error names every entry it could not
# decode, while an archive that cannot be opened at all says so in a trailing 'ERRORS:' block.
TEST_DATA_ERROR = """
7-Zip (r) 26.03 (x86) : Igor Pavlov : Public domain : 2026-09-03

Testing archive: crc.7z
--
Path = crc.7z
Type = 7z

ERROR: Data Error : src\\a.txt
ERROR: Data Error : src\\b.txt

Sub items Errors: 2

Archives with Errors: 1

Sub items Errors: 2
"""

TEST_CANNOT_OPEN = """
7-Zip (r) 26.03 (x86) : Igor Pavlov : Public domain : 2026-09-03

Testing archive: trunc.7z
ERROR: trunc.7z
trunc.7z
Open ERROR: Cannot open the file as [7z] archive


ERRORS:
Unexpected end of archive

Can't open as archive: 1
Files: 0
"""

TEST_OK = """
7-Zip (r) 26.03 (x86) : Igor Pavlov : Public domain : 2026-09-03

Testing archive: ok.7z
--
Path = ok.7z
Type = 7z

Everything is Ok

Folders: 1
Files: 2
"""


class TestParseTestOutput(unittest.TestCase):
    def test_a_clean_test_reports_no_problem(self):
        self.assertEqual(sevenzip.parse_test_output(TEST_OK.splitlines()), [])

    def test_a_data_error_names_every_entry_that_failed(self):
        self.assertEqual(sevenzip.parse_test_output(TEST_DATA_ERROR.splitlines()),
                         ["Data Error : src\\a.txt", "Data Error : src\\b.txt"])

    def test_an_unopenable_archive_reports_the_reason_not_its_own_name(self):
        # 'ERROR: trunc.7z' is 7-Zip restating the file it is on; the finding is in the two
        # lines after it, and a report that quoted the name instead would say nothing.
        self.assertEqual(sevenzip.parse_test_output(TEST_CANNOT_OPEN.splitlines()),
                         ["Cannot open the file as [7z] archive", "Unexpected end of archive"])


@unittest.skipUnless(SEVEN_ZIP, "7z.exe not found")
class TestTestArchive(unittest.TestCase):
    """'7z t' on real archives: the only layer that reads the payload rather than the headers."""

    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)
        self.source = os.path.join(self.dir.name, "a.txt")
        with open(self.source, "w", encoding="utf-8") as f:
            # Long enough that the packed stream spans the offset the corruption tests hit.
            f.write("hello world, a reasonably long line to actually compress " * 40)

    def archive(self, name: str, password: str | None = None) -> str:
        path = os.path.join(self.dir.name, name)
        args = [SEVEN_ZIP, "a", "-t7z", path, self.source]
        if password:
            args.append("-p" + password)
        subprocess.run(args, capture_output=True, text=True, check=True)
        return path

    def corrupt(self, path: str, offset: int) -> str:
        with open(path, "r+b") as f:
            f.seek(offset)
            byte = f.read(1)
            f.seek(offset)
            f.write(bytes([byte[0] ^ 0xFF]))
        return path

    def test_a_sound_archive_tests_clean(self):
        result = sevenzip.test_archive(SEVEN_ZIP, self.archive("ok.7z"))
        self.assertTrue(result.ok)
        self.assertEqual(result.code, 0)
        self.assertEqual(result.problems, [])

    def test_a_flipped_byte_in_the_payload_names_the_entry_that_failed(self):
        # Past the 32-byte signature header, so the headers still parse and the CRC check is
        # what catches it - the corruption a listing alone cannot see.
        result = sevenzip.test_archive(SEVEN_ZIP, self.corrupt(self.archive("crc.7z"), 60))
        self.assertFalse(result.ok)
        self.assertEqual(result.code, 2)
        self.assertTrue(any("a.txt" in problem for problem in result.problems), result.problems)

    def test_a_truncated_archive_reports_the_unexpected_end(self):
        path = self.archive("trunc.7z")
        with open(path, "rb") as f:
            whole = f.read()
        with open(path, "wb") as f:
            f.write(whole[:len(whole) // 2])

        result = sevenzip.test_archive(SEVEN_ZIP, path)
        self.assertFalse(result.ok)
        self.assertTrue(any("Unexpected end of archive" in problem for problem in result.problems),
                        result.problems)

    def test_a_password_protected_archive_passes_only_with_its_password(self):
        path = self.archive("sealed.7z", password="S3cret")
        self.assertFalse(sevenzip.test_archive(SEVEN_ZIP, path).ok)
        self.assertTrue(sevenzip.test_archive(SEVEN_ZIP, path, password="S3cret").ok)

    def test_a_missing_file_is_a_problem_not_an_exception(self):
        result = sevenzip.test_archive(SEVEN_ZIP, os.path.join(self.dir.name, "absent.7z"))
        self.assertFalse(result.ok)


class TestPasswordFor(unittest.TestCase):
    """ORC records a protected archive's password in the '<name>_cli_config.xml' beside it.

    It is the '/config' the sub-command was run with, archived next to that command's output,
    so an archive that needs a password usually arrives with it - no '--password' needed.
    """

    def config(self, directory: str, name: str, password: str) -> None:
        with open(os.path.join(directory, name), "w", encoding="utf-8") as f:
            f.write(f'<getthis><output password="{password}"/></getthis>')

    def test_the_password_recorded_beside_the_archive_is_found(self):
        with tempfile.TemporaryDirectory() as d:
            self.config(d, "GetThis_cli_config.xml", "S3cret")

            self.assertEqual(sevenzip.password_for(os.path.join(d, "GetThis.7z")), "S3cret")

    def test_an_explicit_password_wins(self):
        with tempfile.TemporaryDirectory() as d:
            self.config(d, "GetThis_cli_config.xml", "S3cret")

            self.assertEqual(sevenzip.password_for(os.path.join(d, "GetThis.7z"), "mine"),
                             "mine")

    def test_no_config_beside_it_is_no_password(self):
        with tempfile.TemporaryDirectory() as d:
            self.assertIsNone(sevenzip.password_for(os.path.join(d, "GetThis.7z")))


@unittest.skipUnless(SEVEN_ZIP, "7z.exe not found")
class TestExtractMatching(unittest.TestCase):
    """Pulling every match out in one 7z call.

    ORC writes a single solid block, so the cost of reaching an entry is decoding everything
    before it. One call stops at the last match; a call per entry re-decodes that prefix for
    each one.
    """

    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)
        self.src = os.path.join(self.dir.name, "src")
        os.makedirs(os.path.join(self.src, "Errors"))
        for name in ("NTFSInfo.dev.log", "USNInfo.dev.log", "NTFSInfo.csv"):
            with open(os.path.join(self.src, name), "w", encoding="utf-8") as f:
                f.write("payload " * 20)
        with open(os.path.join(self.src, "Errors", "Autoruns.dev.log"), "w",
                  encoding="utf-8") as f:
            f.write("nested")

        self.archive = os.path.join(self.dir.name, "a.7z")
        subprocess.run([SEVEN_ZIP, "a", "-t7z", self.archive, os.path.join(self.src, "*")],
                       capture_output=True, text=True, check=True)
        self.out = os.path.join(self.dir.name, "out")

    def test_every_match_comes_out_and_nothing_else(self):
        written = sevenzip.extract_matching(SEVEN_ZIP, self.archive, "*.log", self.out)

        self.assertEqual(sorted(os.path.basename(p) for p in written),
                         ["Autoruns.dev.log", "NTFSInfo.dev.log", "USNInfo.dev.log"])
        self.assertFalse(os.path.exists(os.path.join(self.out, "NTFSInfo.csv")))

    def test_a_nested_match_keeps_the_path_it_had_in_the_archive(self):
        # 'Errors/Autoruns.dev.log' has to stay under 'Errors': run_log_files walks that
        # directory by name, and flattened the per-command logs would collide.
        sevenzip.extract_matching(SEVEN_ZIP, self.archive, "*.log", self.out)

        self.assertTrue(os.path.isfile(os.path.join(self.out, "Errors", "Autoruns.dev.log")))

    def test_an_archive_with_no_match_is_not_an_error(self):
        self.assertEqual(sevenzip.extract_matching(SEVEN_ZIP, self.archive, "*.nope", self.out),
                         [])

    def test_several_patterns_are_one_call(self):
        # ORC's failing-command logs are inside a nested 'Errors' container, which is not a
        # '*.log'. Fetching it in a second call would decode the solid prefix twice.
        written = sevenzip.extract_matching(SEVEN_ZIP, self.archive,
                                            ["*.log", "NTFSInfo.csv"], self.out)

        self.assertIn("NTFSInfo.csv", [os.path.basename(p) for p in written])
        self.assertIn("USNInfo.dev.log", [os.path.basename(p) for p in written])

    def test_a_missing_archive_raises_rather_than_reporting_nothing_found(self):
        with self.assertRaises(RuntimeError):
            sevenzip.extract_matching(SEVEN_ZIP, os.path.join(self.dir.name, "absent.7z"),
                                      "*.log", self.out)


if __name__ == "__main__":
    unittest.main()


@unittest.skipUnless(SEVEN_ZIP, "7z.exe not found")
class TestNamesOutsideAscii(unittest.TestCase):
    """7-Zip prints in the console's code page unless told otherwise, and Python read it in the
    locale's: an entry named 'Résumé.csv' came back garbled, and no longer matched the file
    an extract writes under its real name."""

    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)
        source = os.path.join(self.dir.name, "Résumé.csv")
        with open(source, "w", encoding="utf-8") as f:
            f.write("a,b\n1,2\n")
        self.archive = os.path.join(self.dir.name, "a.7z")
        subprocess.run([SEVEN_ZIP, "a", "-t7z", self.archive, source], capture_output=True,
                       check=True)

    def test_a_listing_names_the_entry_as_it_is(self):
        entries = sevenzip.list_entries(SEVEN_ZIP, self.archive)

        self.assertEqual([entry.path for entry in entries], ["Résumé.csv"])

    def test_an_entry_listed_is_an_entry_extracted(self):
        entry = sevenzip.list_entries(SEVEN_ZIP, self.archive)[0]
        dest = os.path.join(self.dir.name, "out")

        sevenzip.extract_entry(SEVEN_ZIP, self.archive, entry.path, dest)

        self.assertTrue(os.path.isfile(os.path.join(dest, "Résumé.csv")))
