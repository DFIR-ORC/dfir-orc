"""End-to-end: two real archives, one encrypted, compared straight from the dumps.

The only test that exercises the whole chain at once - CMS decryption, journal replay, nested
container discovery, ingest, the run cache and the row diff - so it needs the real tools and
skips cleanly without them.
"""
import io
import json
import os
import struct
import subprocess
import tempfile
import unittest
from contextlib import redirect_stdout
from orca import (check, cli, cms, engine, journal, layout, report, scratch,
                  sevenzip)

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


class OrcFixtures:
    """Builds the collections these tests read: ORC-shaped archives, the '.p7b' envelope over a
    journaled stream, and the sealed sidecars WolfLauncher leaves beside them."""

    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)
        # These tests pass '--work-dir' inside that temp dir, and the scratch base is module
        # state: left pointing at a directory that has since been deleted, the next test's
        # unsealing fails on a path it cannot create.
        self.addCleanup(scratch.use, None)
        self.dumps = os.path.join(self.dir.name, "dumps")
        self.work = os.path.join(self.dir.name, "work")
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

    def seal(self, name: str, text: str) -> str:
        """A sealed ORC sidecar: '<name>.7z.p7b' over a journaled 7z holding one file.

        This is what WolfLauncher's SealClosedFile leaves beside the archives for the Outcome,
        the Outline and its own log when the collection is encrypted.
        """
        build = os.path.join(self.dir.name, "seal")
        os.makedirs(build, exist_ok=True)
        payload = os.path.join(build, name)
        with open(payload, "w", encoding="utf-8") as f:
            f.write(text)

        archive = os.path.join(build, name + ".7z")
        subprocess.run([SEVEN_ZIP, "a", "-bso0", "-bsp0", archive, payload],
                       check=True, capture_output=True)
        with open(archive, "rb") as f:
            stream = journaled(f.read())

        journal_path = archive + "s"
        with open(journal_path, "wb") as f:
            f.write(stream)

        sealed = os.path.join(self.dumps, name + ".7z.p7b")
        subprocess.run([OPENSSL, "cms", "-encrypt", "-binary", "-outform", "DER",
                        "-in", journal_path, "-out", sealed, PUBLIC_KEY],
                       check=True, capture_output=True)
        return sealed


@unittest.skipUnless(SEVEN_ZIP and OPENSSL and os.path.exists(PRIVATE_KEY),
                     "needs 7z, openssl and the CI test key")
class TestCheckCollection(OrcFixtures, unittest.TestCase):
    """'orca check' over a real encrypted collection.

    No duckdb in the skip list: a check reads archives rather than tables, so it must not skip
    on an engine it never loads."""

    def sealed_collection(self) -> str:
        """An encrypted collection as ORC leaves it: a '.p7b' archive and a sealed Outcome
        naming it, with the size and sha1 of the sealed file - which is what ORC hashes."""
        sealed = self.encrypt(self.build_archive("HOST", "200", "aaaa"))
        outcome = {"dfir-orc": {"outcome": {"computer_name": "HOST", "command_set": [
            {"name": "General", "archive": {"name": os.path.basename(sealed),
                                            "size": os.path.getsize(sealed),
                                            "sha1": check.file_sha1(sealed)}}]}}}
        self.seal("ORC_HOST_Outcome.json", json.dumps(outcome))
        return self.dumps

    def check(self, source: str, *extra) -> tuple:
        out = io.StringIO()
        with redirect_stdout(out):
            code = cli.main(["check", source, "--private-key", PRIVATE_KEY,
                             "--work-dir", self.work, *extra])
        return code, out.getvalue()

    def test_check_verifies_an_encrypted_collection_end_to_end(self):
        code, output = self.check(self.sealed_collection())

        self.assertEqual(code, 0, output)
        # The sealed Outcome had to be unsealed to be read, the '.p7b' hashed, the CMS envelope
        # and the journal replayed, and the nested container tested inside the archive.
        self.assertIn("sha1 ok", output)
        self.assertIn("GetThis.7z", output)
        self.assertIn("0 failed", output)

    def test_check_writes_nothing_into_the_collection(self):
        source = self.sealed_collection()
        before = sorted(os.listdir(source))

        self.check(source)

        self.assertEqual(sorted(os.listdir(source)), before)

    def test_check_depth_one_opens_the_envelope_and_stops(self):
        code, output = self.check(self.sealed_collection(), "--depth", "1")

        self.assertEqual(code, 0, output)
        self.assertIn("envelope opened", output)
        self.assertNotIn("GetThis.7z", output)

    def test_check_fails_when_the_envelope_cannot_be_opened(self):
        source = self.sealed_collection()
        sealed = os.path.join(source, "DFIR-ORC_HOST.p7b")
        with open(sealed, "rb") as f:
            whole = f.read()
        with open(sealed, "wb") as f:
            f.write(whole[:len(whole) // 2])

        code, output = self.check(source, "--no-outcome")

        self.assertEqual(code, 1)
        self.assertIn(check.FAILED, output)


@unittest.skipUnless(SEVEN_ZIP and OPENSSL and os.path.exists(PRIVATE_KEY),
                     "needs 7z, openssl and the CI test key")
class TestReportSealedCollection(OrcFixtures, unittest.TestCase):
    """'orca report' over a collection whose Outcome, Outline and log are all sealed.

    The state a collection arrives in. Nothing is ingested and nothing is extracted: report
    unseals what it needs into its work dir and reads it from there.
    """

    OUTCOME = {"dfir-orc": {"outcome": {
        "computer_name": "SEALEDHOST",
        "start": "2026-09-25T14:41:27Z", "end": "2026-09-25T14:48:14Z",
        "command_set": [{"name": "ORC_General",
                         "statistics": {"process_memory_peak": 1325150208},
                         "commands": [
                             {"name": "USNInfo", "tool": "USNInfo", "exit_code": 18,
                              "start": "2026-09-25T14:41:29Z", "end": "2026-09-25T14:41:51Z"},
                             {"name": "NTFSInfo", "tool": "NTFSInfo", "exit_code": 0,
                              "start": "2026-09-25T14:41:29Z", "end": "2026-09-25T14:42:31Z"}]}]}}}

    OUTLINE = {"dfir-orc": {"outline": {"version": "v10.3.4-62-g48af8529a-x64-xp",
                                        "computer_name": "SEALEDHOST", "id": "{SEALED}"}}}

    def sealed_collection(self) -> str:
        """Archive, Outcome, Outline and log, every one of them sealed, as ORC leaves them."""
        self.encrypt(self.build_archive("SEALED", "200", "aaaa"))
        self.seal("ORC_SEALED_Outcome.json", json.dumps(self.OUTCOME))
        self.seal("ORC_SEALED_Outline.json", json.dumps(self.OUTLINE))
        self.seal("ORC_SEALED.log", "a clean start\nFAILED to read the journal\nan Error here\n")
        return self.dumps

    def report(self, *extra) -> str:
        out = io.StringIO()
        with redirect_stdout(out):
            cli.main(["report", self.sealed_collection(), "--private-key", PRIVATE_KEY,
                      "--work-dir", self.work, *extra])
        return out.getvalue()

    def test_the_outcome_is_unsealed_and_read(self):
        output = self.report()

        self.assertIn("SEALEDHOST", output)
        self.assertIn("failed commands (1)", output)
        # set, command, tool, duration, exit code - the columns a --list line uses.
        line = next(l for l in output.splitlines() if "USNInfo" in l and "ORC_General" in l)
        self.assertEqual(line.split(), ["ORC_General", "USNInfo", "USNInfo", "0m22s", "18"])

    def test_the_log_is_unsealed_so_the_errors_view_works(self):
        output = self.report()

        self.assertIn("logs with errors (1 of 1)", output)
        # The section row, not the 'decrypted ...' progress line that names the same file.
        rows = [l.split() for l in output.splitlines() if l.startswith("    ")]
        self.assertIn(["ORC_SEALED.log", "2"], rows)

    def test_the_host_is_the_machine_not_the_folder_name(self):
        # Without the Outcome, a sealed collection is reported under its directory name.
        self.assertIn("SEALEDHOST:", self.report("--one-line"))

    def test_list_works_on_a_sealed_collection(self):
        output = self.report("--list", "--failed")

        self.assertIn("SEALEDHOST", output)
        self.assertIn("USNInfo", output)
        self.assertIn("2 command(s), 1 failed", output)

    def test_the_outline_mode_reads_the_sealed_outline(self):
        out = io.StringIO()
        with redirect_stdout(out):
            cli.main(["report", "outline", self.sealed_collection(), "--json",
                      "--private-key", PRIVATE_KEY, "--work-dir", self.work])

        self.assertIn("v10.3.4-62-g48af8529a-x64-xp", out.getvalue())

    def test_the_outcome_mode_reads_the_sealed_outcome(self):
        out = io.StringIO()
        with redirect_stdout(out):
            cli.main(["report", "outcome", self.sealed_collection(), "--json",
                      "--private-key", PRIVATE_KEY, "--work-dir", self.work])

        self.assertIn("SEALEDHOST", out.getvalue())

    def test_nothing_is_written_into_the_collection(self):
        source = self.sealed_collection()
        before = sorted(os.listdir(source))

        out = io.StringIO()
        with redirect_stdout(out):
            cli.main(["report", source, "--private-key", PRIVATE_KEY, "--work-dir", self.work])

        self.assertEqual(sorted(os.listdir(source)), before)

    def test_a_parent_directory_finds_the_sealed_collection(self):
        # The collection is a child of what was asked about, and its Outcome is sealed: it is
        # found by unsealing, which is why the key is passed here and not at discovery.
        self.sealed_collection()

        scratch.use(self.work)
        dirs = report.candidate_dirs([self.dir.name])
        sealed = report.unsealed_sidecar_dirs(dirs, self.work, PRIVATE_KEY)
        runs = report.find_runs(dirs, sealed)

        self.assertIn(self.dumps, [run.run_dir for run in runs])
        self.assertIn("SEALEDHOST", [report.host_of(r.run_dir, r.extra_dirs, r.outcome_path)
                                     for r in runs])


@unittest.skipUnless(SEVEN_ZIP and DUCKDB and OPENSSL and os.path.exists(PRIVATE_KEY),
                     "needs 7z, duckdb, openssl and the CI test key")
class TestArchiveDiff(OrcFixtures, unittest.TestCase):
    def diff(self, new: str, reference: str, *extra) -> str:
        out = io.StringIO()
        with redirect_stdout(out):
            cli.main(["diff", new, reference, "--private-key", PRIVATE_KEY,
                      "--work-dir", self.work, *extra])
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

    def test_second_run_reuses_the_ingested_run(self):
        new = self.build_archive("NEW", "200", "aaaa")
        reference = self.encrypt(self.build_archive("REF", "999", "bbbb"))

        self.assertIn("ingesting", self.diff(new, reference))
        self.assertNotIn("ingesting", self.diff(new, reference))
        self.assertIn("ingesting", self.diff(new, reference, "--refresh"))

    def test_by_default_everything_lands_beside_the_archive(self):
        new = self.build_archive("NEW", "200", "aaaa")
        reference = self.encrypt(self.build_archive("REF", "999", "bbbb"))

        out = io.StringIO()
        with redirect_stdout(out):
            cli.main(["diff", new, reference, "--private-key", PRIVATE_KEY])

        # One run directory per archive, named after it, in the directory it came from, with
        # its tables flat inside it and orca's own bookkeeping tucked into '.orca'.
        for run in (os.path.join(self.dumps, "DFIR-ORC_NEW"),
                    os.path.join(self.dumps, "DFIR-ORC_REF")):
            self.assertTrue(os.path.exists(os.path.join(run, "NTFSInfo.parquet")), run)
            self.assertTrue(os.path.exists(layout.name_map_path(run)), run)
            self.assertTrue(os.path.exists(layout.manifest_path(run)), run)

        # The decrypted archive is kept beside the encrypted one, under its own name.
        plain = os.path.join(self.dumps, "DFIR-ORC_REF.7z")
        self.assertTrue(os.path.exists(plain))
        self.assertTrue(sevenzip.is_archive(plain))

        # A later ingest of the same collection - here forced by changing an ingest option -
        # reuses that decrypted archive instead of paying for the decrypt again, and does not
        # ingest it as a second collection alongside the '.p7b' it came from.
        again = io.StringIO()
        with redirect_stdout(again):
            cli.main(["diff", new, reference, "--private-key", PRIVATE_KEY,
                      "--exclude-cols", "MD5"])

        text = again.getvalue()
        self.assertIn("ingesting", text)
        self.assertIn("reusing 'DFIR-ORC_REF.7z'", text)
        self.assertNotIn("decrypted", text)

    def test_a_collection_directory_ingests_as_one_run_named_after_the_run(self):
        # One ORC run writes one Outcome and an archive per command set; they ingest together,
        # so the run directory is named as ORC named the run, not after any one archive.
        self.build_archive("Detail", "200", "aaaa")
        self.build_archive("General", "999", "bbbb")
        command_set = [{"name": n, "archive": {"name": f"DFIR-ORC_{n}.7z"}}
                       for n in ("Detail", "General")]
        with open(os.path.join(self.dumps, "ORC_H_172528_Outcome.json"), "w",
                  encoding="utf-8") as f:
            json.dump({"dfir-orc": {"outcome": {"computer_name": "H",
                                                "command_set": command_set}}}, f)

        with redirect_stdout(io.StringIO()):
            cli.main(["ingest", self.dumps, "--no-csv"])

        run = os.path.join(self.dumps, "ORC_H_172528")
        # Both command sets' tables in the one run: 3 CSVs each, names deduped across them.
        self.assertEqual(len(layout.table_paths(run)), 6)
        # The Outcome is in the run under the name ORC gave it, not renamed to 'outcome.json'.
        self.assertTrue(os.path.exists(os.path.join(run, "ORC_H_172528_Outcome.json")))

    def test_sealed_sidecars_are_unsealed_beside_the_collection(self):
        new = self.build_archive("NEW", "200", "aaaa")
        reference = self.encrypt(self.build_archive("REF", "999", "bbbb"))
        outcome = self.seal("ORC_H_172528_Outcome.json",
                            '{"dfir-orc":{"outcome":{"computer_name":"H","command_set":[]}}}')
        self.seal("ORC_H_172528_Outline.json", '{"dfir-orc":{"outline":{"computer_name":"H"}}}')
        self.seal("ORC_H_172528.log", "the collection log\n")

        out = io.StringIO()
        with redirect_stdout(out):
            cli.main(["diff", new, reference, "--private-key", PRIVATE_KEY])

        # Each sidecar is now a plain file beside the sealed one it came from, named after it.
        for name in ("ORC_H_172528_Outcome.json", "ORC_H_172528_Outline.json",
                     "ORC_H_172528.log"):
            self.assertTrue(os.path.exists(os.path.join(self.dumps, name)), name)
        self.assertTrue(os.path.exists(outcome))  # the sealed original is left alone

        # ... and the run picked them up, flat and under the same names: outcome, outline and
        # the collection-level log, mixed in with the tables.
        run = os.path.join(self.dumps, "DFIR-ORC_REF")
        for name in ("ORC_H_172528_Outcome.json", "ORC_H_172528_Outline.json",
                     "ORC_H_172528.log", "NTFSInfo.parquet"):
            self.assertIn(name, os.listdir(run))

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
