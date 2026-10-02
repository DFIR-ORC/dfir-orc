"""orca check: the Outcome cross-check, the container walk, and the verdict reporting."""
import hashlib
import io
import json
import os
import subprocess
import tempfile
import unittest
from contextlib import redirect_stdout

from orca import check, cli, scratch, sevenzip

SEVEN_ZIP = sevenzip.find_7z()


def write(path: str, payload: bytes) -> str:
    with open(path, "wb") as f:
        f.write(payload)
    return path


def sha1(payload: bytes) -> str:
    return hashlib.sha1(payload).hexdigest()


def outcome_with(*archives: dict) -> dict:
    """An Outcome holding one command set per archive, shaped as ORC writes it."""
    return {"command_set": [{"name": f"Set{i}", "archive": archive}
                            for i, archive in enumerate(archives)]}


class TestCheckAgainstOutcome(unittest.TestCase):
    """What the Outcome says ORC wrote, against what is on disk.

    ORC hashes the file it actually wrote - for an encrypted run the hash stream sits below the
    CMS encoder - so this layer needs no key and no decryption, just a read.
    """

    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)

    def path(self, name: str) -> str:
        return os.path.join(self.dir.name, name)

    def test_an_archive_matching_its_recorded_size_and_sha1_passes(self):
        payload = b"a collected archive" * 10
        write(self.path("Set.7z"), payload)
        outcome = outcome_with({"name": "Set.7z", "size": len(payload), "sha1": sha1(payload)})

        findings = check.check_against_outcome(outcome, self.dir.name)

        self.assertEqual([f.verdict for f in findings], [check.OK])
        self.assertEqual(findings[0].target, "Set.7z")

    def test_an_archive_the_outcome_names_but_the_directory_lacks_is_missing(self):
        outcome = outcome_with({"name": "Gone.7z", "size": 1024, "sha1": "ab" * 20})

        findings = check.check_against_outcome(outcome, self.dir.name)

        self.assertEqual([f.verdict for f in findings], [check.MISSING])
        self.assertTrue(findings[0].failed)

    def test_a_truncated_archive_is_reported_with_both_sizes(self):
        # The one corruption '7z t' cannot see on its own: a short archive whose headers are
        # gone is unopenable, but a short archive can also be internally consistent.
        payload = b"only the first half"
        write(self.path("Set.7z"), payload)
        outcome = outcome_with({"name": "Set.7z", "size": 4096, "sha1": "ab" * 20})

        findings = check.check_against_outcome(outcome, self.dir.name)

        self.assertEqual([f.verdict for f in findings], [check.SHORT])
        self.assertIn("19", findings[0].detail)
        self.assertIn("4096", findings[0].detail)

    def test_content_that_differs_at_the_same_size_is_caught_by_the_sha1(self):
        payload = b"the bytes ORC wrote"
        write(self.path("Set.7z"), b"X" * len(payload))
        outcome = outcome_with({"name": "Set.7z", "size": len(payload), "sha1": sha1(payload)})

        findings = check.check_against_outcome(outcome, self.dir.name)

        self.assertEqual([f.verdict for f in findings], [check.SHA1])
        self.assertTrue(findings[0].failed)

    def test_an_outcome_that_records_no_sha1_still_has_its_size_checked(self):
        # ORC logs an error and leaves the field empty when it cannot hash the archive
        # (WolfExecution_Execute.cpp:316), so an absent sha1 is a real case, not a broken file.
        payload = b"unhashed but whole"
        write(self.path("Set.7z"), payload)
        outcome = outcome_with({"name": "Set.7z", "size": len(payload), "sha1": ""})

        findings = check.check_against_outcome(outcome, self.dir.name)

        self.assertEqual([f.verdict for f in findings], [check.OK])
        self.assertIn("no sha1", findings[0].detail)

    def test_an_empty_archive_the_outcome_agrees_is_empty_does_not_fail(self):
        # ORC does write 0-byte archives, which is why extract.py skips them rather than
        # failing; an Outcome that records 0 too means nothing was lost.
        write(self.path("Set.7z"), b"")
        outcome = outcome_with({"name": "Set.7z", "size": 0, "sha1": ""})

        findings = check.check_against_outcome(outcome, self.dir.name)

        self.assertEqual([f.verdict for f in findings], [check.EMPTY])
        self.assertFalse(findings[0].failed)

    def test_an_empty_archive_the_outcome_says_should_hold_data_fails(self):
        write(self.path("Set.7z"), b"")
        outcome = outcome_with({"name": "Set.7z", "size": 151003136, "sha1": "ab" * 20})

        findings = check.check_against_outcome(outcome, self.dir.name)

        self.assertEqual([f.verdict for f in findings], [check.SHORT])
        self.assertTrue(findings[0].failed)

    def test_every_archive_of_every_command_set_is_checked(self):
        payload = b"one of several"
        write(self.path("A.7z"), payload)
        write(self.path("B.7z"), payload)
        outcome = outcome_with({"name": "A.7z", "size": len(payload), "sha1": sha1(payload)},
                               {"name": "B.7z", "size": len(payload), "sha1": "ab" * 20},
                               {"name": "C.7z", "size": 10, "sha1": ""})

        findings = check.check_against_outcome(outcome, self.dir.name)

        self.assertEqual([(f.target, f.verdict) for f in findings],
                         [("A.7z", check.OK), ("B.7z", check.SHA1), ("C.7z", check.MISSING)])

    def test_only_the_decrypted_copy_on_disk_is_not_a_sha1_mismatch(self):
        # The Outcome records the file ORC wrote, '<name>.7z.p7b', and its sha1 is of that
        # ciphertext (the hash stream sits below the CMS encoder). A collection reaching us
        # already decrypted - 'orca extract --depth 1' leaves exactly that - has nothing to
        # compare, and calling it a mismatch would be a false alarm, the worst kind here.
        payload = b"the decrypted archive"
        write(self.path("Set.7z"), payload)
        outcome = outcome_with({"name": "Set.7z.p7b", "size": 4096, "sha1": "ab" * 20})

        findings = check.check_against_outcome(outcome, self.dir.name)

        self.assertEqual([f.verdict for f in findings], [check.UNSEALED])
        self.assertFalse(findings[0].failed)
        self.assertIn("Set.7z", findings[0].detail)

    def test_the_sealed_file_is_preferred_when_both_copies_are_there(self):
        # What an 'orca ingest' leaves behind: the '.p7b' and the '.7z' it decrypted and kept.
        sealed = b"the sealed bytes ORC wrote"
        write(self.path("Set.7z.p7b"), sealed)
        write(self.path("Set.7z"), b"the decrypted copy")
        outcome = outcome_with({"name": "Set.7z.p7b", "size": len(sealed),
                                "sha1": sha1(sealed)})

        findings = check.check_against_outcome(outcome, self.dir.name)

        self.assertEqual([f.verdict for f in findings], [check.OK])

    def test_a_command_set_without_an_archive_is_not_a_finding(self):
        # A set whose commands all failed before anything was archived has no 'archive' object.
        findings = check.check_against_outcome({"command_set": [{"name": "Set"}]}, self.dir.name)

        self.assertEqual(findings, [])


@unittest.skipUnless(SEVEN_ZIP, "7z.exe not found")
class TestCheckContainer(unittest.TestCase):
    """The container walk, on real nested archives.

    "stage" is what goes into an archive, "dir" is where the archives themselves land, and
    "work" is the scratch the walk is allowed to use - the whole point being that it holds one
    container at a time and nothing at the end.
    """

    def setUp(self):
        self.root = tempfile.TemporaryDirectory()
        self.addCleanup(self.root.cleanup)
        self.stage = os.path.join(self.root.name, "stage")
        self.dir = os.path.join(self.root.name, "archives")
        self.work = os.path.join(self.root.name, "work")
        for directory in (self.stage, self.dir, self.work):
            os.makedirs(directory)

    def text(self, name: str, payload: str) -> str:
        path = os.path.join(self.stage, name)
        with open(path, "w", encoding="utf-8") as f:
            f.write(payload)
        return path

    def archive(self, name: str, *members: str, into: str = "", password: str = "") -> str:
        """A real 7z holding exactly "members", built from the staging dir."""
        path = os.path.join(into or self.dir, name)
        args = [SEVEN_ZIP, "a", "-t7z", path, *members]
        if password:
            args.append("-p" + password)
        subprocess.run(args, cwd=self.stage, capture_output=True, text=True, check=True)
        return path

    def corrupt(self, path: str, offset: int = 60) -> str:
        """Flip a byte of the packed stream, past the 32-byte signature header: the headers
        still parse, so only a CRC check over the payload catches it."""
        with open(path, "r+b") as f:
            f.seek(offset)
            byte = f.read(1)
            f.seek(offset)
            f.write(bytes([byte[0] ^ 0xFF]))
        return path

    def nested(self, corrupt: bool = False, password: str = "") -> str:
        """A Top.7z holding a CSV and a GetSamples.7z container, the way ORC nests them."""
        self.text("a.txt", "a nested sample, long enough to pack into a real stream " * 40)
        inner = self.archive("GetSamples.7z", "a.txt", into=self.stage, password=password)
        if corrupt:
            # Corrupted BEFORE nesting, so Top.7z carries the CRC of the damaged bytes and
            # tests clean: the damage exists only inside the container.
            self.corrupt(inner)

        self.text("NTFSInfo.csv", "FullName,FRN\n\\a,0x01\n")
        return self.archive("Top.7z", "GetSamples.7z", "NTFSInfo.csv")

    def seen(self, findings) -> list:
        return [(f.target, f.verdict, f.level) for f in findings]

    def left_behind(self) -> list:
        return [name for _, _, files in os.walk(self.work) for name in files]

    def test_an_archive_with_no_container_is_one_ok_finding(self):
        self.text("NTFSInfo.csv", "FullName,FRN\n\\a,0x01\n")
        self.text("SystemInfo.csv", "Name,Value\nOS,11\n")
        archive = self.archive("Top.7z", "NTFSInfo.csv", "SystemInfo.csv")

        findings = check.check_container(SEVEN_ZIP, archive, self.work)

        self.assertEqual(self.seen(findings), [("Top.7z", check.OK, 0)])
        self.assertIn("2 entries", findings[0].detail)

    def test_a_nested_container_is_tested_at_the_next_level(self):
        findings = check.check_container(SEVEN_ZIP, self.nested(), self.work)

        self.assertEqual(self.seen(findings),
                         [("Top.7z", check.OK, 0), ("GetSamples.7z", check.OK, 1)])

    def test_the_walk_leaves_nothing_in_the_work_dir(self):
        # The promise of the command: one container on disk at a time, none at the end.
        check.check_container(SEVEN_ZIP, self.nested(), self.work)

        self.assertEqual(self.left_behind(), [])
        self.assertEqual(os.listdir(self.work), [])

    def test_damage_inside_a_container_fails_it_while_the_top_stays_ok(self):
        findings = check.check_container(SEVEN_ZIP, self.nested(corrupt=True), self.work)

        self.assertEqual(self.seen(findings),
                         [("Top.7z", check.OK, 0), ("GetSamples.7z", check.FAILED, 1)])
        self.assertIn("a.txt", findings[1].detail)
        self.assertEqual(self.left_behind(), [])

    def test_depth_one_tests_the_archive_without_opening_its_containers(self):
        findings = check.check_container(SEVEN_ZIP, self.nested(), self.work, depth=1)

        self.assertEqual(self.seen(findings), [("Top.7z", check.OK, 0)])

    def test_a_sealed_container_is_opened_with_the_password_orc_recorded_beside_it(self):
        self.nested(password="S3cret")
        # ORC ships the password in a sibling "<name>_cli_config.xml" entry, so the walk has to
        # carry that file out of the archive along with the container itself.
        self.text("GetSamples_cli_config.xml", "<cli><output password=\"S3cret\"/></cli>")
        top = self.archive("Top.7z", "GetSamples.7z", "NTFSInfo.csv",
                           "GetSamples_cli_config.xml")

        findings = check.check_container(SEVEN_ZIP, top, self.work)

        self.assertEqual(self.seen(findings),
                         [("Top.7z", check.OK, 0), ("GetSamples.7z", check.OK, 1)])

    def test_a_sealed_container_with_no_recorded_password_is_reported_failed(self):
        findings = check.check_container(SEVEN_ZIP, self.nested(password="S3cret"), self.work)

        self.assertEqual(self.seen(findings),
                         [("Top.7z", check.OK, 0), ("GetSamples.7z", check.FAILED, 1)])

    def test_an_explicit_password_opens_a_sealed_container(self):
        findings = check.check_container(SEVEN_ZIP, self.nested(password="S3cret"), self.work,
                                         password="S3cret")

        self.assertEqual(self.seen(findings),
                         [("Top.7z", check.OK, 0), ("GetSamples.7z", check.OK, 1)])

    def test_an_unopenable_archive_is_failed_and_not_recursed_into(self):
        path = os.path.join(self.dir, "Top.7z")
        with open(path, "wb") as f:
            f.write(b"this was never an archive" * 10)

        findings = check.check_container(SEVEN_ZIP, path, self.work)

        self.assertEqual(self.seen(findings), [("Top.7z", check.FAILED, 0)])
        self.assertEqual(self.left_behind(), [])

    def test_a_staging_directory_that_cannot_be_made_is_a_finding_not_a_crash(self):
        # A verification pass that dies with a traceback has reported nothing at all, so a work
        # dir it cannot use has to come back as a failed container like any other.
        blocked = os.path.join(self.root.name, "blocked")
        with open(blocked, "wb"):
            pass

        findings = check.check_container(SEVEN_ZIP, self.nested(), blocked)

        self.assertEqual([(f.target, f.verdict) for f in findings],
                         [("Top.7z", check.OK), ("GetSamples.7z", check.FAILED)])

    def test_an_empty_container_is_reported_without_failing_the_run(self):
        # ORC writes these; 7-Zip cannot open one, so it needs saying before "7z t" is asked.
        self.text("NTFSInfo.csv", "FullName\n\\a\n")
        with open(os.path.join(self.stage, "Empty.7z"), "wb"):
            pass
        top = self.archive("Top.7z", "Empty.7z", "NTFSInfo.csv")

        findings = check.check_container(SEVEN_ZIP, top, self.work)

        self.assertEqual(self.seen(findings),
                         [("Top.7z", check.OK, 0), ("Empty.7z", check.EMPTY, 1)])
        self.assertFalse(findings[1].failed)


def findings_of(*rows) -> list:
    return [check.Finding(*row) for row in rows]


SAMPLE = findings_of(
    ("ORC_Offline.7z.p7b", check.OK, "sha1 ok, 158340608 bytes", 0),
    ("ORC_Offline.7z", check.OK, "312 entries, 151003136 bytes", 0),
    ("GetSamples.7z", check.OK, "1204 entries, 44220416 bytes", 1),
    ("BootCode.7z", check.FAILED, "Data Error : BootCode/sector0.bin", 1),
    ("Empty.7z", check.EMPTY, "0 bytes", 1),
)


class TestSummarize(unittest.TestCase):
    def test_it_counts_what_was_checked_and_what_failed(self):
        summary = check.summarize(SAMPLE, "E:/dumps/ORC_HOST")

        self.assertEqual(summary["source"], "E:/dumps/ORC_HOST")
        self.assertEqual(summary["checked"], 5)
        self.assertEqual(summary["ok"], 3)
        self.assertEqual(summary["failed"], 1)

    def test_it_carries_the_failures_so_a_summary_names_them(self):
        summary = check.summarize(SAMPLE, "x")

        self.assertEqual([f["target"] for f in summary["failures"]], ["BootCode.7z"])
        self.assertEqual(summary["failures"][0]["detail"], "Data Error : BootCode/sector0.bin")

    def test_an_empty_check_is_not_a_pass(self):
        # Nothing checked means nothing verified; a 'checked=0 failed=0' summary that read as
        # success would be the one lie this command must not tell.
        summary = check.summarize([], "x")

        self.assertEqual(summary["checked"], 0)
        self.assertEqual(summary["failed"], 0)


class TestRenderHuman(unittest.TestCase):
    def test_the_verdict_column_aligns_and_nesting_is_indented(self):
        lines = check.render_human(SAMPLE).splitlines()

        self.assertTrue(lines[0].startswith("  ok "), lines[0])
        column = [line.index("ORC_Offline.7z ") for line in lines[1:2]]
        self.assertEqual([line.find("GetSamples.7z") for line in lines[2:3]],
                         [column[0] + 2])

    def test_every_finding_gets_a_line_with_its_detail(self):
        text = check.render_human(SAMPLE)

        self.assertIn("Data Error : BootCode/sector0.bin", text)
        self.assertEqual(len(text.splitlines()), len(SAMPLE) + 2)

    def test_the_last_line_counts_the_findings(self):
        last = check.render_human(SAMPLE).splitlines()[-1]

        self.assertIn("5 checked", last)
        self.assertIn("1 failed", last)

    def test_a_clean_check_says_so_without_listing_failures(self):
        clean = findings_of(("ORC_Offline.7z", check.OK, "312 entries", 0))

        last = check.render_human(clean).splitlines()[-1]

        self.assertIn("1 checked", last)
        self.assertIn("0 failed", last)


class TestRenderOneLine(unittest.TestCase):
    def test_it_carries_the_counts_and_the_source(self):
        line = check.render_one_line(check.summarize(SAMPLE, "E:/dumps/ORC_HOST"))

        self.assertTrue(line.startswith("E:/dumps/ORC_HOST:"), line)
        self.assertIn("checked=5", line)
        self.assertIn("failed=1", line)

    def test_a_verdict_nobody_hit_is_left_out(self):
        # The report-style rule: a field that always reads zero stops being read.
        clean = findings_of(("ORC_Offline.7z", check.OK, "312 entries", 0))

        line = check.render_one_line(check.summarize(clean, "x"))

        self.assertNotIn("empty=", line)
        self.assertIn("failed=0", line)

    def test_an_empty_verdict_is_named_when_there_is_one(self):
        line = check.render_one_line(check.summarize(SAMPLE, "x"))

        self.assertIn("empty=1", line)


class TestRenderJson(unittest.TestCase):
    def test_it_is_json_carrying_every_finding(self):
        import json

        parsed = json.loads(check.render_json(check.summarize(SAMPLE, "x")))

        self.assertEqual(parsed["checked"], 5)
        self.assertEqual(len(parsed["findings"]), 5)
        self.assertEqual(parsed["findings"][3]["verdict"], check.FAILED)
        self.assertEqual(parsed["findings"][2]["level"], 1)


@unittest.skipUnless(SEVEN_ZIP, "7z.exe not found")
class TestRunCheck(unittest.TestCase):
    """A whole collection: the Outcome layer, the container walk, and the exit code.

    The collection directory is built the way ORC leaves one - an Outcome JSON naming the
    archives it wrote, beside those archives.
    """

    def setUp(self):
        self.root = tempfile.TemporaryDirectory()
        self.addCleanup(self.root.cleanup)
        # The work dir lives in that temp dir and the scratch base is module state: left
        # pointing at a deleted directory it breaks the next test that takes a scratch dir.
        self.addCleanup(scratch.use, None)
        self.stage = os.path.join(self.root.name, "stage")
        self.dumps = os.path.join(self.root.name, "dumps")
        self.work = os.path.join(self.root.name, "work")
        for directory in (self.stage, self.dumps, self.work):
            os.makedirs(directory)

    def text(self, name: str, payload: str) -> str:
        path = os.path.join(self.stage, name)
        with open(path, "w", encoding="utf-8") as f:
            f.write(payload)
        return path

    def archive(self, name: str, *members: str, into: str = "") -> str:
        path = os.path.join(into or self.dumps, name)
        subprocess.run([SEVEN_ZIP, "a", "-t7z", path, *members],
                       cwd=self.stage, capture_output=True, text=True, check=True)
        return path

    def collection(self, corrupt: bool = False, missing: bool = False) -> str:
        """An ORC-shaped collection: Top.7z with a nested container, plus its Outcome."""
        self.text("a.txt", "a collected sample, long enough to pack into a real stream " * 40)
        inner = self.archive("GetThis.7z", "a.txt", into=self.stage)
        if corrupt:
            with open(inner, "r+b") as f:
                f.seek(60)
                byte = f.read(1)
                f.seek(60)
                f.write(bytes([byte[0] ^ 0xFF]))

        self.text("NTFSInfo.csv", "FullName,FRN\n\\a,0x01\n")
        top = self.archive("DFIR-ORC_HOST.7z", "GetThis.7z", "NTFSInfo.csv")

        sets = [{"name": "General", "archive": {"name": os.path.basename(top),
                                                "size": os.path.getsize(top),
                                                "sha1": check.file_sha1(top)}}]
        if missing:
            sets.append({"name": "Little", "archive": {"name": "DFIR-ORC_HOST_Little.7z",
                                                       "size": 4096, "sha1": "ab" * 20}})

        outcome = os.path.join(self.dumps, "ORC_HOST_Outcome.json")
        with open(outcome, "w", encoding="utf-8") as f:
            json.dump({"dfir-orc": {"outcome": {"computer_name": "HOST", "command_set": sets}}}, f)
        return self.dumps

    def invoke(self, source: str, **kwargs) -> tuple:
        """Named 'invoke' rather than 'run': TestCase.run is the method unittest itself calls."""
        out = io.StringIO()
        with redirect_stdout(out):
            code = check.run_check(source, work_dir=self.work, **kwargs)
        return code, out.getvalue()

    def test_a_sound_collection_passes_with_a_row_per_layer(self):
        code, output = self.invoke(self.collection())

        self.assertEqual(code, 0)
        self.assertIn("sha1 ok", output)
        self.assertIn("DFIR-ORC_HOST.7z", output)
        self.assertIn("GetThis.7z", output)
        self.assertIn("3 checked", output)
        self.assertIn("0 failed", output)

    def test_damage_inside_a_container_makes_the_check_fail(self):
        code, output = self.invoke(self.collection(corrupt=True))

        self.assertEqual(code, 1)
        self.assertIn(check.FAILED, output)
        self.assertIn("a.txt", output)

    def test_an_archive_the_outcome_names_but_is_gone_fails_the_check(self):
        code, output = self.invoke(self.collection(missing=True))

        self.assertEqual(code, 1)
        self.assertIn(check.MISSING, output)
        self.assertIn("DFIR-ORC_HOST_Little.7z", output)

    def test_no_outcome_layer_leaves_only_the_container_rows(self):
        code, output = self.invoke(self.collection(), outcome=False)

        self.assertEqual(code, 0)
        self.assertNotIn("sha1 ok", output)
        self.assertIn("2 checked", output)

    def test_an_archive_with_no_outcome_beside_it_is_still_checked(self):
        self.text("NTFSInfo.csv", "FullName,FRN\n\\a,0x01\n")
        top = self.archive("DFIR-ORC_HOST.7z", "NTFSInfo.csv")

        code, output = self.invoke(top)

        self.assertEqual(code, 0)
        self.assertIn("1 checked", output)

    def test_depth_one_tests_the_archive_and_leaves_its_containers_packed(self):
        code, output = self.invoke(self.collection(), depth=1)

        self.assertEqual(code, 0)
        self.assertNotIn("GetThis.7z", output)

    def test_the_walk_leaves_no_container_in_the_work_dir(self):
        self.invoke(self.collection())

        left = [name for _, _, files in os.walk(self.work) for name in files]
        self.assertEqual(left, [])

    def test_the_collection_is_not_written_to(self):
        # A verification pass must leave the evidence exactly as it found it.
        source = self.collection()
        before = sorted(os.listdir(source))

        self.invoke(source)

        self.assertEqual(sorted(os.listdir(source)), before)

    def test_one_line_is_one_line(self):
        code, output = self.invoke(self.collection(), one_line=True)

        self.assertEqual(code, 0)
        self.assertEqual(len(output.strip().splitlines()), 1)
        self.assertIn("checked=3", output)

    def test_json_is_one_object_carrying_the_findings(self):
        code, output = self.invoke(self.collection(), as_json=True)

        parsed = json.loads(output)
        self.assertEqual(code, 0)
        self.assertEqual(parsed["checked"], 3)
        self.assertEqual(parsed["failed"], 0)

    def test_the_cli_hands_back_the_exit_code(self):
        # The point of the command in a pipeline: a damaged collection has to be a non-zero
        # exit, not a line of output somebody has to read.
        out = io.StringIO()
        with redirect_stdout(out):
            code = cli.main(["check", self.collection(corrupt=True), "--work-dir", self.work])

        self.assertEqual(code, 1)
        self.assertIn(check.FAILED, out.getvalue())

    def test_the_cli_passes_a_clean_collection(self):
        out = io.StringIO()
        with redirect_stdout(out):
            code = cli.main(["check", self.collection(), "--work-dir", self.work, "--one-line"])

        self.assertEqual(code, 0)
        self.assertIn("failed=0", out.getvalue())

    def test_a_depth_below_one_is_refused_rather_than_silently_checking_nothing(self):
        with self.assertRaises(RuntimeError):
            self.invoke(self.collection(), depth=0)


if __name__ == "__main__":
    unittest.main()
