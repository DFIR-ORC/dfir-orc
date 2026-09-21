import io
import json
import os
import struct
import tempfile
import unittest
from contextlib import redirect_stdout
from unittest import mock
from orca import ingest, journal
from orca.sevenzip import ArchiveEntry


def _e(path):
    return ArchiveEntry(path=path, size=1, crc="x", attributes="A")


class TestPlanJobs(unittest.TestCase):
    def test_filters_csv_and_names_uniquely(self):
        entries = [_e("C\\NTFSInfo.csv"), _e("D\\NTFSInfo.csv"),
                   _e("SystemInfo.csv"), _e("GetThis.7z")]
        jobs = ingest.plan_jobs([("arc.7z", entries)])
        self.assertEqual([j.output_name for j in jobs],
                         ["NTFSInfo_C", "NTFSInfo_D", "SystemInfo"])
        self.assertTrue(all(j.source_archive == "arc.7z" for j in jobs))
        self.assertTrue(all(j.entry_path.lower().endswith(".csv") for j in jobs))


class TestResolveArchives(unittest.TestCase):
    # The real Outcome json uses lowercase keys and a single 'archive' object per
    # command_set; a capitalized/list lookup returned nothing on real data.
    def _write_outcome(self, dir_, sets):
        cmd_sets = [{"name": n, "archive": {"name": a}} for n, a in sets]
        outcome = {"dfir-orc": {"outcome": {"computer_name": "H", "command_set": cmd_sets}}}
        with open(os.path.join(dir_, "OrcOutcome.json"), "w", encoding="utf-8") as f:
            json.dump(outcome, f)

    def test_dir_reads_lowercase_archive_names(self):
        with tempfile.TemporaryDirectory() as d:
            self._write_outcome(d, [("SystemInfo", "SystemInfo.7z"),
                                    ("GetThis", "GetThis.7z")])
            got = ingest.resolve_archives(d)
            self.assertEqual(got, [os.path.join(d, "SystemInfo.7z"),
                                   os.path.join(d, "GetThis.7z")])

    def test_file_passthrough(self):
        self.assertEqual(ingest.resolve_archives(__file__), [__file__])

    def test_directory_without_an_outcome_falls_back_to_its_archives(self):
        with tempfile.TemporaryDirectory() as d:
            for name in ("B.p7b", "A.7z", "notes.txt"):
                with open(os.path.join(d, name), "wb") as f:
                    f.write(b"x")
            self.assertEqual(ingest.resolve_archives(d),
                             [os.path.join(d, "A.7z"), os.path.join(d, "B.p7b")])

    def test_directory_with_nothing_usable(self):
        with tempfile.TemporaryDirectory() as d:
            with self.assertRaises(RuntimeError):
                ingest.resolve_archives(d)


class TestSealedOutputs(unittest.TestCase):
    """ORC seals closed files into '<name>.7z.p7b', so an encrypted collection holds
    'outcome.json.7z.p7b' beside the real archives. Telling them apart matters twice: the
    outcome must be read, and it must not be ingested as if it were a result archive."""

    def test_sealed_name_strips_the_suffix(self):
        self.assertEqual(ingest.sealed_name("outcome.json.7z.p7b"), "outcome.json")
        self.assertEqual(ingest.sealed_name("SystemInfo.7z.p7b"), "SystemInfo")

    def test_sealed_name_of_something_else(self):
        self.assertIsNone(ingest.sealed_name("GetThis.7z"))
        self.assertIsNone(ingest.sealed_name("archive.p7b"))

    def test_sealed_outputs_are_not_archives(self):
        self.assertTrue(ingest.sealed_output("outcome.json.7z.p7b"))
        self.assertTrue(ingest.sealed_output("GetThis.log.7z.p7b"))
        self.assertFalse(ingest.sealed_output("SystemInfo.7z.p7b"))
        self.assertFalse(ingest.sealed_output("DFIR-ORC_HOST.7z"))

    def test_resolve_archives_skips_a_sealed_outcome(self):
        with tempfile.TemporaryDirectory() as d:
            for name in ("SystemInfo.7z.p7b", "outcome.json.7z.p7b", "GetThis.log.7z.p7b"):
                with open(os.path.join(d, name), "wb") as f:
                    f.write(b"x")

            self.assertEqual(ingest.resolve_archives(d),
                             [os.path.join(d, "SystemInfo.7z.p7b")])

    def test_open_sealed_outcome_extracts_the_json(self):
        with tempfile.TemporaryDirectory() as d:
            source = os.path.join(d, "out")
            os.makedirs(source)
            with open(os.path.join(source, "outcome.json.7z.p7b"), "wb") as f:
                f.write(b"ciphertext")

            entry = ArchiveEntry(path="Outcome.json", size=10, crc="x", attributes="A")
            with mock.patch.object(ingest, "decrypt_archive",
                                   lambda p, w, k, b: os.path.join(w, "outcome.json.7z")), \
                 mock.patch.object(ingest.sevenzip, "list_entries", lambda *a, **k: [entry]), \
                 mock.patch.object(ingest.sevenzip, "extract_entry",
                                   lambda z, a, p, dest, *r: os.path.join(dest, p)):
                found = ingest.open_sealed_outcome(source, d, "7z")

            self.assertEqual(found, os.path.join(d, "Outcome.json"))

    def test_open_sealed_outcome_when_there_is_none(self):
        with tempfile.TemporaryDirectory() as d:
            self.assertIsNone(ingest.open_sealed_outcome(d, d, "7z"))

    def test_unopenable_sealed_outcome_does_not_abort_the_ingest(self):
        with tempfile.TemporaryDirectory() as d:
            with open(os.path.join(d, "outcome.json.7z.p7b"), "wb") as f:
                f.write(b"ciphertext")

            def refuse(*args, **kwargs):
                raise RuntimeError("no backend could decrypt")

            with mock.patch.object(ingest, "decrypt_archive", refuse):
                self.assertIsNone(ingest.open_sealed_outcome(d, d, "7z"))


class TestDecryptArchive(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)
        self.work = os.path.join(self.dir.name, "staging")

    def path(self, name, blob):
        full = os.path.join(self.dir.name, name)
        with open(full, "wb") as f:
            f.write(blob)
        return full

    def journal_of(self, payload):
        return (struct.pack("<4sI", journal.MAGIC, journal.VERSION)
                + struct.pack("<4sIQ", journal.OP_WRITE, 0, len(payload)) + payload
                + struct.pack("<4sIQ", journal.OP_CLOSE, 0, 0))

    def test_plain_archive_is_returned_untouched(self):
        archive = self.path("Little.7z", b"7z\xbc\xaf\x27\x1c")
        self.assertEqual(ingest.decrypt_archive(archive, self.work), archive)
        self.assertFalse(os.path.exists(self.work))

    def test_bare_journal_is_replayed_without_decryption(self):
        stream = self.path("Little.7zs", self.journal_of(b"7z-archive-bytes"))
        plain = ingest.decrypt_archive(stream, self.work)
        self.assertEqual(plain, os.path.join(self.work, "Little.7z"))
        with open(plain, "rb") as f:
            self.assertEqual(f.read(), b"7z-archive-bytes")

    def test_p7b_is_decrypted_then_replayed(self):
        encrypted = self.path("Little.p7b", b"ciphertext")
        payload = b"7z-archive-bytes"

        def fake_decrypt(src, dst, private_key, backend):
            with open(dst, "wb") as f:
                f.write(self.journal_of(payload))
            return "openssl"

        with mock.patch.object(ingest.cms, "decrypt", fake_decrypt):
            plain = ingest.decrypt_archive(encrypted, self.work, "key.pem")

        self.assertEqual(plain, os.path.join(self.work, "Little.7z"))
        with open(plain, "rb") as f:
            self.assertEqual(f.read(), payload)
        # The journal intermediate is GB-scale; it must not be left behind.
        self.assertFalse(os.path.exists(os.path.join(self.work, "Little.7zs")))

    def test_orc_named_archive_does_not_gain_a_second_extension(self):
        # ORC writes '<name>.7z.p7b', so the stem already carries '.7z'.
        encrypted = self.path("SystemInfo.7z.p7b", b"ciphertext")

        def fake_decrypt(src, dst, private_key, backend):
            with open(dst, "wb") as f:
                f.write(self.journal_of(b"7z-archive-bytes"))
            return "cng"

        with mock.patch.object(ingest.cms, "decrypt", fake_decrypt):
            plain = ingest.decrypt_archive(encrypted, self.work, "key.pem")

        self.assertEqual(plain, os.path.join(self.work, "SystemInfo.7z"))

    def test_real_orc_journal_shape_replays_to_the_right_size(self):
        """The operation sequence ORC actually writes, from a real SystemInfo.7z.p7b:
        it seeks back to 0 at the end to rewrite the 7z header, so the payload bytes sum to
        more than the archive. Concatenating the writes would corrupt it."""
        header, body, tail = b"H" * 32, b"B" * 100, b"T" * 32
        stream = (struct.pack("<4sI", journal.MAGIC, journal.VERSION)
                  + struct.pack("<4sIq", journal.OP_SEEK, journal.SEEK_BEGIN, 0)
                  + struct.pack("<4sIQ", journal.OP_WRITE, 0, len(header)) + header
                  + struct.pack("<4sIQ", journal.OP_WRITE, 0, len(body)) + body
                  + struct.pack("<4sIq", journal.OP_SEEK, journal.SEEK_BEGIN, 0)
                  + struct.pack("<4sIQ", journal.OP_WRITE, 0, len(tail)) + tail
                  + struct.pack("<4sIQ", journal.OP_CLOSE, 0, 0))

        source = self.path("Stream.7zs", stream)
        plain = ingest.decrypt_archive(source, self.work)
        with open(plain, "rb") as f:
            written = f.read()

        self.assertEqual(len(written), 132)  # not 164: the header was overwritten
        self.assertEqual(written, tail + body)

    def _decrypt_to(self, content: bytes):
        """Run decrypt_archive over a '.p7b' whose CMS content is 'content'; return (path, log)."""
        encrypted = self.path("Little.p7b", b"ciphertext")

        def fake_decrypt(src, dst, private_key, backend):
            with open(dst, "wb") as f:
                f.write(content)
            return "openssl"

        out = io.StringIO()
        with mock.patch.object(ingest.cms, "decrypt", fake_decrypt), redirect_stdout(out):
            plain = ingest.decrypt_archive(encrypted, self.work, "key.pem")
        return plain, out.getvalue()

    def test_unjournaled_p7b_keeps_the_decrypted_content(self):
        # '/no_journaling': the CMS content is the archive itself.
        plain, _ = self._decrypt_to(b"7z\xbc\xaf\x27\x1c")

        self.assertEqual(plain, os.path.join(self.work, "Little.7z"))
        with open(plain, "rb") as f:
            self.assertEqual(f.read(), b"7z\xbc\xaf\x27\x1c")

    def test_unjournaled_p7b_is_named_as_no_journaling_not_as_a_problem(self):
        # A missing JRNL header is normal under '/no_journaling', so it must not be reported
        # as a failure - that would cry wolf on every archive collected that way.
        _, log = self._decrypt_to(b"7z\xbc\xaf\x27\x1c")

        self.assertIn("no_journaling", log)
        self.assertNotIn("warning", log.lower())

    def test_content_that_is_neither_journal_nor_archive_is_reported(self):
        # The wrong-key case. CMS over AES-CBC has no integrity check and OpenSSL answers a
        # bad RSA padding with a random session key, so cms.decrypt can return garbage and
        # call it a success; only the content tells us. Absent JRNL, absent 7z signature.
        plain, log = self._decrypt_to(b"\xb0f5\x06\xd1\xbc@\x9e\xe7\xa9s]\xbc\x88\xff")

        self.assertIn("warning", log.lower())
        self.assertIn("key", log)
        # 'continue as if it worked': the caller still gets the path, garbage and all.
        self.assertEqual(plain, os.path.join(self.work, "Little.7z"))
        self.assertTrue(os.path.exists(plain))


if __name__ == "__main__":
    unittest.main()
