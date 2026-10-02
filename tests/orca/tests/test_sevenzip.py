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


if __name__ == "__main__":
    unittest.main()
