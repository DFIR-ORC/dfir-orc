import os
import tempfile
import unittest
from unittest import mock
from orca import sevenzip

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

    def test_extract_all_tolerates_warning(self):
        with mock.patch("orca.sevenzip.subprocess.run",
                        return_value=mock.Mock(returncode=1, stdout="", stderr="")):
            sevenzip.extract_all("7z", "a.7z", "dest")  # no raise

    def test_extract_all_raises_on_fatal(self):
        with mock.patch("orca.sevenzip.subprocess.run",
                        return_value=mock.Mock(returncode=2, stdout="", stderr="bad")):
            with self.assertRaises(RuntimeError):
                sevenzip.extract_all("7z", "a.7z", "dest")


if __name__ == "__main__":
    unittest.main()
