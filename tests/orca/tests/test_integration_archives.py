"""End-to-end: two real archives, one encrypted, compared straight from the dumps.

The only test that exercises the whole chain at once - CMS decryption, journal replay, nested
container discovery, ingest, the run cache and the row diff - so it needs the real tools and
skips cleanly without them.
"""
import io
import os
import struct
import subprocess
import tempfile
import unittest
from contextlib import redirect_stdout
from orca import cli, cms, engine, journal, sevenzip

HERE = os.path.dirname(os.path.abspath(__file__))
CI_DIR = os.path.abspath(os.path.join(HERE, "..", "..", "..", "tools", "ci"))
PUBLIC_KEY = os.path.join(CI_DIR, "contoso.com.pem")
PRIVATE_KEY = os.path.join(CI_DIR, "contoso.com.key")

SEVEN_ZIP = sevenzip.find_7z()
DUCKDB = engine.find_duckdb()
OPENSSL = cms.find_openssl()

NTFSINFO = ("FullName,FRN,SizeInBytes,SampleCollectionDate\n"
            "\\file1,0x01,100,2026-09-20 10:00:00\n"
            "\\file2,0x02,{size},2026-09-20 10:00:01\n")
GETTHIS = "ComputerName,FullName,MD5\nH,\\a.txt,{md5}\n"
SYSTEMINFO = "Name,Value\nOS,Windows 11\n"


def journaled(payload: bytes) -> bytes:
    """Wrap bytes the way ORC's JournalingStream does, including a backward seek and rewrite
    so a replay that merely concatenated the writes would produce the wrong archive."""
    half = len(payload) // 2
    return (struct.pack("<4sI", journal.MAGIC, journal.VERSION)
            + struct.pack("<4sIQ", journal.OP_WRITE, 0, len(payload)) + payload
            + struct.pack("<4sIq", journal.OP_SEEK, journal.SEEK_BEGIN, half)
            + struct.pack("<4sIQ", journal.OP_WRITE, 0, len(payload) - half) + payload[half:]
            + struct.pack("<4sIQ", journal.OP_CLOSE, 0, 0))


@unittest.skipUnless(SEVEN_ZIP and DUCKDB and OPENSSL and os.path.exists(PRIVATE_KEY),
                     "needs 7z, duckdb, openssl and the CI test key")
class TestArchiveDiff(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)
        self.dumps = os.path.join(self.dir.name, "dumps")
        self.cache = os.path.join(self.dir.name, "cache")
        os.makedirs(self.dumps)

    def build_archive(self, name: str, size: str, md5: str) -> str:
        """An ORC-shaped result archive: top-level CSVs plus one nested container."""
        build = os.path.join(self.dir.name, "build", name)
        nested = os.path.join(build, "nested")
        os.makedirs(nested)

        for path, text in ((os.path.join(build, "NTFSInfo.csv"), NTFSINFO.format(size=size)),
                           (os.path.join(build, "SystemInfo.csv"), SYSTEMINFO),
                           (os.path.join(nested, "GetThis.csv"), GETTHIS.format(md5=md5))):
            with open(path, "w", encoding="utf-8") as f:
                f.write(text)

        container = os.path.join(build, "GetThis.7z")
        subprocess.run([SEVEN_ZIP, "a", "-bso0", "-bsp0", container,
                        os.path.join(nested, "GetThis.csv")], check=True, capture_output=True)

        archive = os.path.join(self.dumps, f"DFIR-ORC_{name}.7z")
        subprocess.run([SEVEN_ZIP, "a", "-bso0", "-bsp0", archive,
                        os.path.join(build, "NTFSInfo.csv"),
                        os.path.join(build, "SystemInfo.csv"), container],
                       check=True, capture_output=True)
        return archive

    def encrypt(self, archive: str) -> str:
        """Turn a plain archive into the '.p7b' ORC would have produced."""
        stream = archive + "s"
        with open(archive, "rb") as f:
            payload = f.read()
        with open(stream, "wb") as f:
            f.write(journaled(payload))

        encrypted = os.path.splitext(archive)[0] + ".p7b"
        subprocess.run([OPENSSL, "cms", "-encrypt", "-binary", "-outform", "DER",
                        "-in", stream, "-out", encrypted, PUBLIC_KEY],
                       check=True, capture_output=True)
        os.remove(archive)
        os.remove(stream)
        return encrypted

    def diff(self, new: str, reference: str, *extra) -> str:
        out = io.StringIO()
        with redirect_stdout(out):
            cli.main(["diff", new, reference, "--private-key", PRIVATE_KEY,
                      "--cache-dir", self.cache, *extra])
        return out.getvalue()

    def test_encrypted_reference_diffs_against_a_plain_archive(self):
        new = self.build_archive("NEW", "200", "aaaa")
        reference = self.encrypt(self.build_archive("REF", "999", "bbbb"))

        text = self.diff(new, reference, "--show")

        # The nested container's CSV was found, and both changed tables are reported.
        self.assertIn("GetThis: +1 / -1 rows", text)
        self.assertIn("NTFSInfo: +1 / -1 rows", text)
        self.assertIn("SystemInfo: +0 / -0 rows", text)
        # Same file set on both sides, and no collected binary differs.
        self.assertIn("files: +0 / -0", text)
        # The row preview pairs on FRN and names the column that moved.
        self.assertIn("changed: SizeInBytes", text)
        # The report names what was compared, not the cache directory it ingested into.
        self.assertIn(os.path.basename(reference), text)

    def test_second_run_is_served_from_the_cache(self):
        new = self.build_archive("NEW", "200", "aaaa")
        reference = self.encrypt(self.build_archive("REF", "999", "bbbb"))

        self.assertIn("ingesting", self.diff(new, reference))
        self.assertNotIn("ingesting", self.diff(new, reference))
        self.assertIn("ingesting", self.diff(new, reference, "--refresh"))

    def test_identical_archives_report_no_difference(self):
        new = self.build_archive("NEW", "200", "aaaa")
        reference = self.encrypt(self.build_archive("REF", "200", "aaaa"))

        text = self.diff(new, reference)
        self.assertIn("files: +0 / -0", text)
        self.assertNotIn("+1 / -1", text)

    def test_extract_expands_an_encrypted_result(self):
        reference = self.encrypt(self.build_archive("REF", "999", "bbbb"))
        out = os.path.join(self.dir.name, "expanded")

        with redirect_stdout(io.StringIO()):
            cli.main(["extract", reference, "--out", out, "--private-key", PRIVATE_KEY])

        tree = os.path.join(out, "DFIR-ORC_REF")
        self.assertTrue(os.path.exists(os.path.join(tree, "NTFSInfo.csv")))
        # The nested container is expanded too, not just dropped in place.
        self.assertTrue(os.path.exists(os.path.join(tree, "GetThis", "GetThis.csv")))


if __name__ == "__main__":
    unittest.main()
