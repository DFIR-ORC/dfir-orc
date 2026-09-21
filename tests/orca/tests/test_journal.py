import os
import struct
import tempfile
import unittest
from orca import journal


def header(magic=journal.MAGIC, version=journal.VERSION):
    return struct.pack("<4sI", magic, version)


def write_op(payload):
    return struct.pack("<4sIQ", journal.OP_WRITE, 0, len(payload)) + payload


def seek_op(method, distance):
    return struct.pack("<4sIq", journal.OP_SEEK, method, distance)


def close_op():
    return struct.pack("<4sIQ", journal.OP_CLOSE, 0, 0)


class JournalCase(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)
        self.src = os.path.join(self.dir.name, "stream.7zs")
        self.dst = os.path.join(self.dir.name, "stream.7z")

    def source(self, blob):
        with open(self.src, "wb") as f:
            f.write(blob)
        return self.src

    def replayed(self, blob):
        journal.replay(self.source(blob), self.dst)
        with open(self.dst, "rb") as f:
            return f.read()

    def rejects(self, blob, message):
        with self.assertRaises(RuntimeError) as caught:
            journal.replay(self.source(blob), self.dst)
        self.assertIn(message, str(caught.exception))


class TestReplay(JournalCase):
    def test_sequential_writes(self):
        blob = header() + write_op(b"7z\xbc\xaf") + write_op(b"payload") + close_op()
        self.assertEqual(self.replayed(blob), b"7z\xbc\xafpayload")

    def test_backward_seek_overwrites(self):
        # ORC seeks back to patch the archive header once the final size is known.
        blob = (header() + write_op(b"AAAABBBB")
                + seek_op(journal.SEEK_BEGIN, 4) + write_op(b"CCCC") + close_op())
        self.assertEqual(self.replayed(blob), b"AAAACCCC")

    def test_seek_current_and_end(self):
        blob = (header() + write_op(b"0123")
                + seek_op(journal.SEEK_CURRENT, -2) + write_op(b"xy")
                + seek_op(journal.SEEK_END, 0) + write_op(b"z") + close_op())
        self.assertEqual(self.replayed(blob), b"01xyz")

    def test_returns_bytes_written(self):
        blob = header() + write_op(b"abcdef") + close_op()
        self.assertEqual(journal.replay(self.source(blob), self.dst), 6)

    def test_payload_larger_than_the_copy_chunk(self):
        payload = os.urandom(journal._COPY_CHUNK + 5)
        self.assertEqual(self.replayed(header() + write_op(payload) + close_op()), payload)

    def test_missing_close_record_is_accepted(self):
        # A crashed collection leaves an unclosed journal; its data is still replayable.
        self.assertEqual(self.replayed(header() + write_op(b"data")), b"data")


class TestRejects(JournalCase):
    def test_bad_magic(self):
        self.rejects(header(magic=b"XXXX") + close_op(), "not a JRNL")

    def test_bad_version(self):
        self.rejects(header(version=99) + close_op(), "not a JRNL")

    def test_empty_source(self):
        self.rejects(b"", "not a JRNL")

    def test_truncated_record_header(self):
        self.rejects(header() + b"WRIT\x00\x00\x00\x00", "ends inside a record header")

    def test_write_length_past_end_of_source(self):
        blob = header() + struct.pack("<4sIQ", journal.OP_WRITE, 0, 1 << 40) + b"short"
        self.rejects(blob, "bytes left")

    def test_write_length_just_past_the_last_byte(self):
        # Off-by-a-few rather than absurd: the length check is a range check, not a sanity check.
        blob = header() + struct.pack("<4sIQ", journal.OP_WRITE, 0, 32) + b"only-ten-b"
        self.rejects(blob, "bytes left")

    def test_seek_to_a_negative_position(self):
        self.rejects(header() + seek_op(journal.SEEK_BEGIN, -1) + close_op(), "outside")

    def test_seek_past_the_source_size(self):
        self.rejects(header() + seek_op(journal.SEEK_BEGIN, 1 << 40) + close_op(), "outside")

    def test_unknown_seek_method(self):
        self.rejects(header() + seek_op(7, 0) + close_op(), "unknown seek method")

    def test_unknown_record(self):
        blob = header() + struct.pack("<4sIQ", b"HACK", 0, 0) + close_op()
        self.rejects(blob, "unknown record")

    def test_data_after_the_close_record(self):
        self.rejects(header() + close_op() + write_op(b"trailing"), "after its close record")


class TestIsJournaled(JournalCase):
    def test_true_for_a_journal(self):
        self.assertTrue(journal.is_journaled(self.source(header() + close_op())))

    def test_false_for_a_plain_archive(self):
        self.assertFalse(journal.is_journaled(self.source(b"7z\xbc\xaf\x27\x1c\x00\x04")))

    def test_false_for_a_short_file(self):
        self.assertFalse(journal.is_journaled(self.source(b"JRNL")))

    def test_false_for_a_missing_file(self):
        self.assertFalse(journal.is_journaled(os.path.join(self.dir.name, "absent")))


if __name__ == "__main__":
    unittest.main()
