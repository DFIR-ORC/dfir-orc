import io
import json
import os
import types
import struct
import subprocess
import tempfile
import unittest
from contextlib import redirect_stdout
from unittest import mock
from orca import ingest, journal, layout, sevenzip
from orca.sevenzip import ArchiveEntry


def setUpModule():
    # orca writes its products under the current directory by default: run this module from
    # an empty one, never from the source tree.
    here = os.getcwd()
    work = tempfile.TemporaryDirectory()
    os.chdir(work.name)
    unittest.addModuleCleanup(work.cleanup)
    unittest.addModuleCleanup(os.chdir, here)


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

    def test_a_decrypted_collection_is_found_under_the_name_it_now_has(self):
        # ORC records the name it wrote. A collection can reach us decrypted and the envelope
        # gone - 'orca extract --depth 1' produces exactly that - with the Outcome still
        # naming the sealed one.
        with tempfile.TemporaryDirectory() as d:
            self._write_outcome(d, [("SystemInfo", "SystemInfo.7z.p7b")])
            with open(os.path.join(d, "SystemInfo.7z"), "wb") as f:
                f.write(b"7z")

            self.assertEqual(ingest.resolve_archives(d), [os.path.join(d, "SystemInfo.7z")])

    def test_the_sealed_archive_wins_when_both_are_there(self):
        # The usual case: orca keeps the decrypted copy beside the original.
        with tempfile.TemporaryDirectory() as d:
            self._write_outcome(d, [("SystemInfo", "SystemInfo.7z.p7b")])
            for name in ("SystemInfo.7z.p7b", "SystemInfo.7z"):
                with open(os.path.join(d, name), "wb") as f:
                    f.write(b"x")

            self.assertEqual(ingest.resolve_archives(d),
                             [os.path.join(d, "SystemInfo.7z.p7b")])

    def test_a_missing_archive_is_named_as_the_outcome_asked_for_it(self):
        # What is missing is the file ORC said it wrote, not a decrypted copy nobody made.
        with tempfile.TemporaryDirectory() as d:
            self._write_outcome(d, [("SystemInfo", "SystemInfo.7z.p7b")])

            self.assertEqual(ingest.resolve_archives(d),
                             [os.path.join(d, "SystemInfo.7z.p7b")])

    def test_an_archive_no_outcome_names_is_returned_too(self):
        """A share where two ORC runs landed side by side holds one Outcome per run, and only
        one of them is read. The other run's archive is collected data sitting right there, so
        it comes out too, with a word about why it was not in the Outcome."""
        with tempfile.TemporaryDirectory() as d:
            self._write_outcome(d, [("General", "ORC_B2_General.7z.p7b")])
            for name in ("ORC_B2_General.7z.p7b", "ORC_B0_General.7z.p7b"):
                with open(os.path.join(d, name), "wb") as f:
                    f.write(b"x")

            printed = io.StringIO()
            with redirect_stdout(printed):
                got = ingest.resolve_archives(d)

            self.assertEqual(got, [os.path.join(d, "ORC_B2_General.7z.p7b"),
                                   os.path.join(d, "ORC_B0_General.7z.p7b")])
            self.assertIn("ORC_B0_General.7z.p7b", printed.getvalue())

    def test_the_outcome_read_is_the_one_the_tree_gets(self):
        """Which Outcome is read has to be the same every time, and the same one
        find_outcome_json() copies into the tree - it reads them sorted."""
        with tempfile.TemporaryDirectory() as d:
            for stem, archive in (("B_Outcome", "B_General.7z"), ("A_Outcome", "A_General.7z")):
                outcome = {"dfir-orc": {"outcome": {"command_set": [
                    {"name": "General", "archive": {"name": archive}}]}}}
                with open(os.path.join(d, stem + ".json"), "w", encoding="utf-8") as f:
                    json.dump(outcome, f)
                with open(os.path.join(d, archive), "wb") as f:
                    f.write(b"x")

            with redirect_stdout(io.StringIO()):
                got = ingest.resolve_archives(d)

            self.assertEqual(os.path.basename(got[0]), "A_General.7z")
            self.assertEqual(os.path.basename(ingest.find_outcome_json(d)), "A_Outcome.json")

    def test_the_decrypted_copy_of_a_named_archive_is_not_taken_for_an_unnamed_one(self):
        # 'X.7z' beside 'X.7z.p7b' is what orca kept, and the Outcome names the sealed one.
        with tempfile.TemporaryDirectory() as d:
            self._write_outcome(d, [("SystemInfo", "SystemInfo.7z.p7b")])
            for name in ("SystemInfo.7z.p7b", "SystemInfo.7z"):
                with open(os.path.join(d, name), "wb") as f:
                    f.write(b"x")

            printed = io.StringIO()
            with redirect_stdout(printed):
                got = ingest.resolve_archives(d)

            self.assertEqual(got, [os.path.join(d, "SystemInfo.7z.p7b")])
            self.assertEqual(printed.getvalue(), "")

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

    def test_resolve_archives_skips_the_decrypted_copy_of_a_sealed_archive(self):
        # Once orca keeps 'X.7z' beside 'X.7z.p7b', the loose-archive fallback sees both;
        # ingesting the pair would ingest the same collection twice.
        with tempfile.TemporaryDirectory() as d:
            for name in ("SystemInfo.7z.p7b", "SystemInfo.7z", "Other.7z"):
                with open(os.path.join(d, name), "wb") as f:
                    f.write(b"x")

            self.assertEqual(ingest.resolve_archives(d),
                             [os.path.join(d, "Other.7z"),
                              os.path.join(d, "SystemInfo.7z.p7b")])


class UnsealCase(unittest.TestCase):
    """Sealed sidecars unseal in place, beside the file they came from."""

    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)
        self.decrypted = []

    def seal(self, name, directory=None):
        full = os.path.join(directory or self.dir.name, name)
        with open(full, "wb") as f:
            f.write(b"ciphertext")
        return full

    def fakes(self, payload=b"{}"):
        """decrypt_archive/list_entries/extract_entry that produce a real file to move."""
        def fake_decrypt(path, dest, private_key=None, backend=None, refresh=False):
            self.decrypted.append(os.path.basename(path))
            return os.path.join(dest, "sealed.7z")

        def fake_extract(seven, archive, entry_path, dest, password=None):
            produced = os.path.join(dest, entry_path)
            with open(produced, "wb") as f:
                f.write(payload)
            return produced

        entry = ArchiveEntry(path="inner", size=1, crc="x", attributes="A")
        return (mock.patch.object(ingest, "decrypt_archive", fake_decrypt),
                mock.patch.object(ingest.sevenzip, "list_entries", lambda *a, **k: [entry]),
                mock.patch.object(ingest.sevenzip, "extract_entry", fake_extract))

    def unseal(self, source=None, keep_dir=None, refresh=False, payload=b"{}"):
        out = io.StringIO()
        decrypt, listing, extract = self.fakes(payload)
        with decrypt, listing, extract, redirect_stdout(out):
            return ingest.unseal_sidecars(source or self.dir.name, "7z",
                                          keep_dir or self.dir.name, refresh=refresh)


class TestUnsealSidecars(UnsealCase):
    def test_each_sidecar_lands_beside_its_sealed_file_named_after_it(self):
        self.seal("ORC_H_172528_Outcome.json.7z.p7b")
        self.seal("ORC_H_172528_Outline.json.7z.p7b")
        self.seal("ORC_H_172528.log.7z.p7b")

        unsealed = self.unseal()

        self.assertEqual(sorted(os.path.basename(p) for p in unsealed),
                         ["ORC_H_172528.log", "ORC_H_172528_Outcome.json",
                          "ORC_H_172528_Outline.json"])
        for path in unsealed:
            self.assertTrue(os.path.exists(path), path)
            self.assertEqual(os.path.dirname(path), self.dir.name)

    def test_a_sealed_archive_is_left_alone(self):
        # 'SystemInfo.7z.p7b' seals an archive, not an output: it is ingested, not unsealed.
        self.seal("SystemInfo.7z.p7b")
        self.assertEqual(self.unseal(), [])
        self.assertEqual(self.decrypted, [])

    def test_an_already_unsealed_sidecar_is_reused(self):
        self.seal("ORC_Outcome.json.7z.p7b")
        plain = os.path.join(self.dir.name, "ORC_Outcome.json")
        with open(plain, "wb") as f:
            f.write(b"{}")

        self.assertEqual(self.unseal(), [plain])
        self.assertEqual(self.decrypted, [])

    def test_refresh_unseals_again(self):
        self.seal("ORC_Outcome.json.7z.p7b")
        with open(os.path.join(self.dir.name, "ORC_Outcome.json"), "wb") as f:
            f.write(b"stale")

        self.unseal(refresh=True, payload=b"fresh")

        with open(os.path.join(self.dir.name, "ORC_Outcome.json"), "rb") as f:
            self.assertEqual(f.read(), b"fresh")
        self.assertEqual(self.decrypted, ["ORC_Outcome.json.7z.p7b"])

    def test_keep_dir_redirects_the_unsealed_file(self):
        # The read-only-collection path: the sealed file cannot be unsealed where it lives.
        self.seal("ORC_Outcome.json.7z.p7b")
        keep = os.path.join(self.dir.name, "elsewhere")
        os.makedirs(keep)

        self.assertEqual(self.unseal(keep_dir=keep),
                         [os.path.join(keep, "ORC_Outcome.json")])

    def test_an_unopenable_sidecar_does_not_abort_the_ingest(self):
        self.seal("ORC_Outcome.json.7z.p7b")

        def refuse(*args, **kwargs):
            raise RuntimeError("no backend could decrypt")

        out = io.StringIO()
        with mock.patch.object(ingest, "decrypt_archive", refuse), redirect_stdout(out):
            self.assertEqual(ingest.unseal_sidecars(self.dir.name, "7z", self.dir.name), [])
        self.assertIn("could not unseal", out.getvalue())

    def test_a_file_source_unseals_the_sidecars_of_its_collection(self):
        archive = os.path.join(self.dir.name, "ORC_Offline.7z")
        with open(archive, "wb") as f:
            f.write(b"7z")
        self.seal("ORC_Outcome.json.7z.p7b")

        self.assertEqual(self.unseal(source=archive),
                         [os.path.join(self.dir.name, "ORC_Outcome.json")])


class Done:
    """A finished future: the result loop only ever calls .result()."""

    def __init__(self, value=None, error=None):
        self.value, self.error = value, error

    def result(self):
        if self.error:
            raise self.error
        return self.value


class Job:
    def __init__(self, name):
        self.output_name = name


class TestCollectResults(unittest.TestCase):
    """One malformed collected file once cost every other table in the collection: the first
    failing job's exception came straight out of the result loop. It costs only itself now,
    and '--strict' is how you ask for the old behaviour."""

    def collect(self, futures, jobs, strict=False):
        out = io.StringIO()
        with redirect_stdout(out):
            records, skipped = ingest.collect_results(futures, jobs, strict)
        return records, skipped, out.getvalue()

    def test_strict_aborts_at_the_first_failure(self):
        futures = [Done(["a"]), Done(error=RuntimeError("duckdb said no")), Done(["c"])]
        jobs = [Job("first"), Job("autoruns"), Job("third")]

        with self.assertRaises(RuntimeError):
            self.collect(futures, jobs, strict=True)

    def test_a_failure_costs_only_that_table(self):
        futures = [Done(["a"]), Done(error=RuntimeError("duckdb said no")), Done(["c"])]
        jobs = [Job("first"), Job("autoruns"), Job("third")]

        records, skipped, printed = self.collect(futures, jobs)

        self.assertEqual(records, ["a", "c"])
        self.assertEqual([name for name, _ in skipped], ["autoruns"])
        self.assertIn("autoruns", printed)

    def test_the_reason_is_kept_on_one_line(self):
        # A duckdb error carries the whole failing statement, newlines and all.
        futures = [Done(error=RuntimeError("Invalid Input\nSQL: COPY (SELECT\n *)"))]

        _, skipped, _ = self.collect(futures, [Job("autoruns")])

        self.assertNotIn("\n", skipped[0][1])
        self.assertIn("Invalid Input", skipped[0][1])

    def test_an_os_error_is_skipped_too(self):
        futures = [Done(error=OSError("no such file"))]

        _, skipped, _ = self.collect(futures, [Job("autoruns")])

        self.assertEqual(len(skipped), 1)

    def test_nothing_skipped_when_every_job_worked(self):
        records, skipped, _ = self.collect([Done(["a"])], [Job("first")])

        self.assertEqual(records, ["a"])
        self.assertEqual(skipped, [])


class TestWriteSkipped(unittest.TestCase):
    """A run missing a table does not say so anywhere else - a diff calls it a removed table,
    as though the collection had stopped producing it - so the run has to carry the caveat."""

    def test_it_records_each_table_and_its_reason(self):
        with tempfile.TemporaryDirectory() as d:
            ingest.write_skipped([("autoruns", "duckdb said no")], d)

            with open(ingest.layout.skipped_path(d), encoding="utf-8") as f:
                self.assertEqual(f.read(), "autoruns\tduckdb said no\n")

    def test_no_file_when_nothing_was_skipped(self):
        with tempfile.TemporaryDirectory() as d:
            ingest.write_skipped([], d)
            self.assertFalse(os.path.exists(ingest.layout.skipped_path(d)))

    def test_an_older_run_s_record_is_not_left_behind(self):
        # Re-ingesting into the same directory must not leave it claiming a table is missing.
        with tempfile.TemporaryDirectory() as d:
            ingest.write_skipped([("autoruns", "duckdb said no")], d)

            ingest.write_skipped([], d)

            self.assertFalse(os.path.exists(ingest.layout.skipped_path(d)))


class TestIngestScratch(unittest.TestCase):
    """The CMS intermediate is as big as the collection and goes to scratch, so '--work-dir'
    has to move that too - otherwise a temp volume smaller than the collection stops an ingest
    dead, and the error recommends the flag that was already given."""

    def test_work_dir_moves_the_scratch(self):
        from orca import scratch

        with tempfile.TemporaryDirectory() as d:
            work = os.path.join(d, "work")
            self.addCleanup(scratch.use, None)
            self.addCleanup(scratch.cleanup)

            # Set before anything can take a scratch directory, so even a run that stops on
            # the very next line has already moved it.
            with mock.patch.object(ingest, "get_engine", lambda *a, **k: None), \
                 mock.patch.object(ingest.sevenzip, "find_7z", lambda: None):
                with self.assertRaises(RuntimeError):
                    ingest.run_ingest("x.7z", os.path.join(d, "out"), work_dir=work)

            self.assertEqual(os.path.dirname(scratch.root()), os.path.join(work, "orca"))


class TestCopyIntoRun(unittest.TestCase):
    def test_a_file_already_in_the_run_is_left_where_it_is(self):
        # 'orca extract --out <the collection>' asks to copy a file onto itself.
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "ORC_Outcome.json")
            with open(path, "wb") as f:
                f.write(b"{}")

            self.assertEqual(ingest.copy_into_run([path], d), ["ORC_Outcome.json"])
            with open(path, "rb") as f:
                self.assertEqual(f.read(), b"{}")


class TestCaptureOutlines(unittest.TestCase):
    def test_copies_every_attempt_under_its_own_name(self):
        with tempfile.TemporaryDirectory() as d:
            source, run = os.path.join(d, "src"), os.path.join(d, "run")
            os.makedirs(source)
            for name in ("A_Outline.json", "B_Outline.json", "A_Outcome.json", "notes.txt"):
                with open(os.path.join(source, name), "wb") as f:
                    f.write(b"{}")

            written = ingest.capture_outlines([source], run)

            self.assertEqual(written, ["A_Outline.json", "B_Outline.json"])
            self.assertEqual(sorted(os.listdir(run)), ["A_Outline.json", "B_Outline.json"])

    def test_when_there_are_none(self):
        with tempfile.TemporaryDirectory() as d:
            run = os.path.join(d, "run")
            self.assertEqual(ingest.capture_outlines([d], run), [])
            self.assertFalse(os.path.exists(run))


class TestCaptureSidecarLogs(unittest.TestCase):
    def test_collection_level_logs_reach_the_run(self):
        # WolfLauncher's own log is in no archive: it only ever exists beside them.
        with tempfile.TemporaryDirectory() as d:
            source, run = os.path.join(d, "src"), os.path.join(d, "run")
            os.makedirs(source)
            for name in ("ORC_H_172528.log", "ORC_H_172528_Outcome.json"):
                with open(os.path.join(source, name), "wb") as f:
                    f.write(b"x")

            self.assertEqual(ingest.capture_sidecar_logs([source], run),
                             ["ORC_H_172528.log"])
            self.assertEqual(os.listdir(run), ["ORC_H_172528.log"])

    def test_no_logs_writes_no_directory(self):
        with tempfile.TemporaryDirectory() as d:
            run = os.path.join(d, "run")
            self.assertEqual(ingest.capture_sidecar_logs([d], run), [])
            self.assertFalse(os.path.exists(run))


SEVEN_ZIP = sevenzip.find_7z()


@unittest.skipUnless(SEVEN_ZIP, "7z.exe not found")
class TestCaptureLogs(unittest.TestCase):
    """The per-command logs, which live inside the archive rather than beside it."""

    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)
        src = os.path.join(self.dir.name, "src")
        os.makedirs(os.path.join(src, "Errors"))
        for name in ("NTFSInfo.dev.log", "USNInfo.dev.log", "NTFSInfo.csv"):
            with open(os.path.join(src, name), "w", encoding="utf-8") as f:
                f.write("x")
        with open(os.path.join(src, "Errors", "Autoruns.dev.log"), "w", encoding="utf-8") as f:
            f.write("x")

        self.archive = os.path.join(self.dir.name, "ORC_H_General.7z")
        subprocess.run([SEVEN_ZIP, "a", "-t7z", self.archive, os.path.join(src, "*")],
                       capture_output=True, text=True, check=True)
        self.run = os.path.join(self.dir.name, "run")

    def test_every_log_in_the_archive_reaches_the_run(self):
        products = ingest.capture_logs(SEVEN_ZIP, [self.archive], self.run, None)

        # The names are relative to the run dir and separated the way this OS writes paths:
        # 'products' is what cache.clear_dataset joins onto it to know what it may remove.
        self.assertEqual(sorted(products),
                         [os.path.join("Errors", "Autoruns.dev.log"),
                          "NTFSInfo.dev.log", "USNInfo.dev.log"])
        self.assertTrue(os.path.isfile(os.path.join(self.run, "Errors", "Autoruns.dev.log")))

    def test_the_collected_data_is_left_in_the_archive(self):
        ingest.capture_logs(SEVEN_ZIP, [self.archive], self.run, None)

        self.assertFalse(os.path.exists(os.path.join(self.run, "NTFSInfo.csv")))

    def test_the_password_recorded_beside_the_archive_is_used_without_being_asked_for(self):
        locked = os.path.join(self.dir.name, "GetThis.7z")
        subprocess.run([SEVEN_ZIP, "a", "-t7z", "-pS3cret", locked,
                        os.path.join(self.dir.name, "src", "*")],
                       capture_output=True, text=True, check=True)
        with open(os.path.join(self.dir.name, "GetThis_cli_config.xml"), "w",
                  encoding="utf-8") as f:
            f.write('<getthis><output password="S3cret"/></getthis>')

        products = ingest.capture_logs(SEVEN_ZIP, [locked], self.run, None)

        self.assertIn("NTFSInfo.dev.log", products)

    def test_an_archive_that_will_not_open_is_named_rather_than_coming_back_empty(self):
        # 'report' has no --password, so this is reachable there; silently returning no logs
        # would look exactly like an archive that holds none.
        sealed = os.path.join(self.dir.name, "ORC_H_Locked.7z")
        subprocess.run([SEVEN_ZIP, "a", "-t7z", "-pS3cret", sealed,
                        os.path.join(self.dir.name, "src", "*")],
                       capture_output=True, text=True, check=True)

        printed = io.StringIO()
        with redirect_stdout(printed):
            products = ingest.capture_logs(SEVEN_ZIP, [sealed], self.run, None)

        self.assertEqual(products, [])
        self.assertIn("ORC_H_Locked.7z", printed.getvalue())

    def test_an_archive_is_decoded_once_however_many_logs_it_holds(self):
        # Not a detail: ORC writes a single solid block, so a call per log re-decodes
        # everything in front of it. Counted rather than timed, which would be flaky.
        with mock.patch("orca.sevenzip.extract_matching",
                        wraps=sevenzip.extract_matching) as extract:
            ingest.capture_logs(SEVEN_ZIP, [self.archive], self.run, None)

        self.assertEqual(extract.call_count, 1)


class TestSidecarDirs(unittest.TestCase):
    def test_one_directory_when_the_collection_is_writable(self):
        with tempfile.TemporaryDirectory() as d:
            self.assertEqual(ingest.sidecar_dirs(d, d), [d])

    def test_the_fallback_directory_is_searched_too(self):
        with tempfile.TemporaryDirectory() as d:
            keep = os.path.join(d, "scratch")
            self.assertEqual(ingest.sidecar_dirs(d, keep), [d, keep])


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

    def test_an_already_decrypted_archive_is_reused(self):
        # The whole point of keeping it: decrypt + replay of a multi-GB collection is the
        # expensive part of an ingest, and it is deterministic.
        encrypted = self.path("Little.7z.p7b", b"ciphertext")
        os.makedirs(self.work, exist_ok=True)
        plain = os.path.join(self.work, "Little.7z")
        with open(plain, "wb") as f:
            f.write(b"7z\xbc\xaf\x27\x1c")

        out = io.StringIO()
        with mock.patch.object(ingest.cms, "decrypt") as decrypt, redirect_stdout(out):
            self.assertEqual(ingest.decrypt_archive(encrypted, self.work, "key.pem"), plain)
        decrypt.assert_not_called()

    def test_refresh_decrypts_again(self):
        encrypted = self.path("Little.7z.p7b", b"ciphertext")
        os.makedirs(self.work, exist_ok=True)
        with open(os.path.join(self.work, "Little.7z"), "wb") as f:
            f.write(b"7z\xbc\xaf\x27\x1c")

        def fake_decrypt(src, dst, private_key, backend):
            with open(dst, "wb") as f:
                f.write(self.journal_of(b"fresh-archive-bytes"))
            return "openssl"

        out = io.StringIO()
        with mock.patch.object(ingest.cms, "decrypt", fake_decrypt), redirect_stdout(out):
            plain = ingest.decrypt_archive(encrypted, self.work, "key.pem", refresh=True)

        with open(plain, "rb") as f:
            self.assertEqual(f.read(), b"fresh-archive-bytes")

    def test_a_truncated_leftover_is_not_reused(self):
        # Only a file that actually starts with the 7z signature counts as decrypted; a killed
        # run can leave a partial one behind.
        encrypted = self.path("Little.7z.p7b", b"ciphertext")
        os.makedirs(self.work, exist_ok=True)
        with open(os.path.join(self.work, "Little.7z"), "wb") as f:
            f.write(b"not-an-archive")

        def fake_decrypt(src, dst, private_key, backend):
            with open(dst, "wb") as f:
                f.write(self.journal_of(b"7z-archive-bytes"))
            return "openssl"

        out = io.StringIO()
        with mock.patch.object(ingest.cms, "decrypt", fake_decrypt), redirect_stdout(out):
            plain = ingest.decrypt_archive(encrypted, self.work, "key.pem")

        with open(plain, "rb") as f:
            self.assertEqual(f.read(), b"7z-archive-bytes")

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


class TestArchivesNotNamed(unittest.TestCase):
    """The orphan computation on its own, without the line unnamed_archives prints."""

    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)

    def touch(self, name: str, payload: bytes = b"x") -> str:
        path = os.path.join(self.dir.name, name)
        with open(path, "wb") as f:
            f.write(payload)
        return path

    def test_an_archive_no_outcome_names_is_found(self):
        self.touch("ORC_A_General.7z")
        orphan = self.touch("ORC_B_General.7z")

        self.assertEqual(ingest.archives_not_named(self.dir.name, ["ORC_A_General.7z"]),
                         [orphan])

    def test_the_decrypted_copy_of_a_named_archive_is_not_an_orphan(self):
        # The Outcome names the sealed spelling; the '.7z' beside it is orca's own decryption.
        self.touch("ORC_A_General.7z.p7b")
        self.touch("ORC_A_General.7z")

        self.assertEqual(ingest.archives_not_named(self.dir.name, ["ORC_A_General.7z.p7b"]), [])

    def test_a_sealed_sidecar_is_not_an_archive(self):
        self.touch("ORC_A_Outcome.json.7z.p7b")

        self.assertEqual(ingest.archives_not_named(self.dir.name, []), [])


class TestSkippedTablesAreVisible(unittest.TestCase):
    """Skipping is the default, so saying so is not optional.

    A dataset that skipped a table is incomplete and nothing else on disk says so, so every
    failure is printed as it happens AND the end of the ingest names the tables, because the
    per-table lines scroll away behind the timings of the 26 that worked.
    """

    def jobs(self, *names):
        return [types.SimpleNamespace(output_name=name) for name in names]

    def futures(self, *errors):
        class Future:
            def __init__(self, error):
                self.error = error

            def result(self):
                if self.error:
                    raise self.error
                return []

        return [Future(error) for error in errors]

    def test_each_failure_is_printed_as_it_happens(self):
        out = io.StringIO()
        with redirect_stdout(out):
            records, skipped = ingest.collect_results(
                self.futures(RuntimeError("duckdb failed: sniffing autoruns.csv"), None),
                self.jobs("autoruns", "NTFSInfo"), strict=False)

        self.assertEqual(len(skipped), 1)
        self.assertIn("autoruns", out.getvalue())
        self.assertIn("duckdb failed", out.getvalue())

    def test_the_end_of_the_ingest_names_what_it_skipped(self):
        # A count alone sends you looking for a file; the names are what you act on.
        out = io.StringIO()
        with tempfile.TemporaryDirectory() as directory:
            with redirect_stdout(out):
                ingest.report_skipped([("autoruns", "duckdb failed: sniffing"),
                                       ("VSS_list", "7z said no")], directory)

        text = out.getvalue()
        self.assertIn("2 table(s) skipped", text)
        self.assertIn("autoruns", text)
        self.assertIn("VSS_list", text)
        self.assertIn("skipped.txt", text)

    def test_nothing_skipped_says_nothing(self):
        out = io.StringIO()
        with tempfile.TemporaryDirectory() as directory:
            with redirect_stdout(out):
                ingest.report_skipped([], directory)

        self.assertEqual(out.getvalue(), "")

    def test_by_default_a_table_that_will_not_read_costs_only_itself(self):
        # The common case by far: one malformed collected file should not cost the other 26
        # tables, and typing a flag every time to say so is typing it every time.
        out = io.StringIO()
        with redirect_stdout(out):
            records, skipped = ingest.collect_results(
                self.futures(RuntimeError("duckdb failed"), None),
                self.jobs("autoruns", "NTFSInfo"), strict=False)

        self.assertEqual([name for name, _reason in skipped], ["autoruns"])

    def test_strict_stops_at_the_first_failure(self):
        with self.assertRaises(RuntimeError):
            ingest.collect_results(self.futures(RuntimeError("duckdb failed")),
                                   self.jobs("autoruns"), strict=True)


class TestDiscoverJobsStaging(unittest.TestCase):
    """Two archives holding a same-named container must not overwrite each other's copy."""

    def _fakes(self, tops, nested_name):
        def list_entries(seven, archive, password=None):
            if os.path.basename(archive) in tops:
                return [ArchiveEntry(path="Inner.7z", size=1, crc="", attributes="")]
            return [ArchiveEntry(path=nested_name, size=1, crc="", attributes="")]

        def extract_entry(seven, archive, entry_path, dest_dir, password=None):
            out = os.path.join(dest_dir, entry_path)
            os.makedirs(os.path.dirname(out) or dest_dir, exist_ok=True)
            with open(out, "w", encoding="utf-8") as f:
                f.write(os.path.basename(archive))
            return out

        return list_entries, extract_entry

    def test_each_container_keeps_its_own_copy(self):
        tops = {"ORC_A_General.7z", "ORC_B_General.7z"}
        list_entries, extract_entry = self._fakes(tops, "NTFSInfo.csv")

        with tempfile.TemporaryDirectory() as staging, \
                mock.patch.object(sevenzip, "list_entries", list_entries), \
                mock.patch.object(sevenzip, "extract_entry", extract_entry):
            jobs, manifest = ingest.discover_jobs(
                [os.path.join("D:\\out", t) for t in sorted(tops)], staging, "7z", None)

            sources = set()
            for job in jobs:
                with open(job.source_archive, encoding="utf-8") as f:
                    sources.add(f.read())

            # Two jobs, each reading the container that came out of its own top archive.
            self.assertEqual(len(jobs), 2)
            self.assertEqual(sources, tops)

            # Review Focus 8: the manifest is unchanged by the fix. Both archives hold the
            # same leaf at the same relative path and that is what the manifest records;
            # which archive it came from is not part of a relpath, because diff joins two
            # runs' manifests on it and the archive stem carries the run's timestamp.
            self.assertEqual({m["relpath"] for m in manifest}, {"Inner/NTFSInfo.csv"})


class TestCollectionsIn(unittest.TestCase):
    """A directory can hold one collection per host; each Outcome names its own archives."""

    def _write(self, d, stem, computer, archives, run_id="{ID}", ts="20260101_000000",
               outcome_dir=None):
        cmd_sets = [{"name": "General", "archive": {"name": a}} for a in archives]
        doc = {"dfir-orc": {"outcome": {"id": run_id, "computer_name": computer,
                                        "timestamp": ts, "command_set": cmd_sets}}}
        with open(os.path.join(outcome_dir or d, stem + "_Outcome.json"), "w",
                  encoding="utf-8") as f:
            json.dump(doc, f)
        for name in archives:
            with open(os.path.join(d, name), "wb") as f:
                f.write(b"x")

    def test_one_collection_per_outcome(self):
        with tempfile.TemporaryDirectory() as d:
            self._write(d, "ORC_A", "a.lab", ["ORC_A_General.7z"], run_id="{A}")
            self._write(d, "ORC_B", "b.lab", ["ORC_B_General.7z"], run_id="{B}")

            with redirect_stdout(io.StringIO()):
                found = ingest.collections_in(d)

            self.assertEqual([c.computer_name for c in found], ["a.lab", "b.lab"])
            self.assertEqual([c.run_id for c in found], ["{A}", "{B}"])
            self.assertEqual([c.timestamp for c in found],
                             ["20260101_000000", "20260101_000000"])
            self.assertEqual([os.path.basename(p) for p in found[0].archives],
                             ["ORC_A_General.7z"])
            self.assertEqual([os.path.basename(p) for p in found[1].archives],
                             ["ORC_B_General.7z"])

    def test_an_archive_no_outcome_names_becomes_an_unnamed_collection(self):
        """A stray archive is still collected data; it ingests as a collection of its own."""
        with tempfile.TemporaryDirectory() as d:
            self._write(d, "ORC_A", "a.lab", ["ORC_A_General.7z"])
            with open(os.path.join(d, "ORC_Stray_General.7z"), "wb") as f:
                f.write(b"x")

            with redirect_stdout(io.StringIO()):
                found = ingest.collections_in(d)

            self.assertEqual(len(found), 2)
            self.assertIsNone(found[1].outcome_path)
            self.assertEqual(found[1].computer_name, "")
            self.assertEqual([os.path.basename(p) for p in found[1].archives],
                             ["ORC_Stray_General.7z"])

    def test_loose_archives_with_no_outcome_are_one_collection(self):
        with tempfile.TemporaryDirectory() as d:
            for name in ("A.7z", "B.7z"):
                with open(os.path.join(d, name), "wb") as f:
                    f.write(b"x")

            with redirect_stdout(io.StringIO()):
                found = ingest.collections_in(d)

            self.assertEqual(len(found), 1)
            self.assertIsNone(found[0].outcome_path)
            self.assertEqual(len(found[0].archives), 2)

    def test_an_unreadable_outcome_does_not_lose_the_others(self):
        """Review Focus 2: one corrupt sidecar must not cost the rest of the directory."""
        with tempfile.TemporaryDirectory() as d:
            self._write(d, "ORC_A", "a.lab", ["ORC_A_General.7z"])
            with open(os.path.join(d, "ORC_Bad_Outcome.json"), "w", encoding="utf-8") as f:
                f.write("{ this is not json")

            with redirect_stdout(io.StringIO()):
                found = ingest.collections_in(d)

            self.assertEqual([c.computer_name for c in found], ["a.lab"])

    def test_an_outcome_unsealed_elsewhere_is_found_and_its_archives_are_here(self):
        """Review Focus 6: under --work-dir (or on a read-only share) the plain Outcome sits
        in another directory; the archives it names are still beside the sealed one."""
        with tempfile.TemporaryDirectory() as d, tempfile.TemporaryDirectory() as work:
            self._write(d, "ORC_A", "a.lab", ["ORC_A_General.7z"], outcome_dir=work)
            with open(os.path.join(d, "ORC_A_Outcome.json.7z.p7b"), "wb") as f:
                f.write(b"sealed")

            with redirect_stdout(io.StringIO()):
                alone = ingest.collections_in(d)
                together = ingest.collections_in(d, [work])

            self.assertIsNone(alone[0].outcome_path)
            self.assertEqual([c.computer_name for c in together], ["a.lab"])
            self.assertEqual(together[0].archives, [os.path.join(d, "ORC_A_General.7z")])

    def test_the_same_run_in_two_places_is_one_collection(self):
        """A plain Outcome beside the archives and the unsealed copy under --work-dir carry the
        same id, and must not be read as two hosts."""
        with tempfile.TemporaryDirectory() as d, tempfile.TemporaryDirectory() as work:
            self._write(d, "ORC_A", "a.lab", ["ORC_A_General.7z"], run_id="{A}")
            self._write(d, "ORC_A", "a.lab", ["ORC_A_General.7z"], run_id="{A}",
                        outcome_dir=work)

            with redirect_stdout(io.StringIO()):
                found = ingest.collections_in(d, [work])

            self.assertEqual([c.run_id for c in found], ["{A}"])


def write_collection(d, stem, computer, archive, run_id=None, ts="20260101_000000"):
    doc = {"dfir-orc": {"outcome": {"id": run_id or "{%s}" % computer,
                                    "computer_name": computer, "timestamp": ts,
                                    "command_set": [{"name": "General",
                                                     "archive": {"name": archive}}]}}}
    with open(os.path.join(d, stem + "_Outcome.json"), "w", encoding="utf-8") as f:
        json.dump(doc, f)
    with open(os.path.join(d, archive), "wb") as f:
        f.write(b"x")


def no_tools():
    """run_ingest looks duckdb and 7z up before it reads anything; neither is installed here."""
    return (mock.patch.object(ingest, "get_engine", return_value=mock.Mock()),
            mock.patch.object(ingest.sevenzip, "find_7z", return_value="7z"))


class TestIngestRefusesAFleet(unittest.TestCase):
    def test_two_collections_are_refused_by_name(self):
        with tempfile.TemporaryDirectory() as d:
            write_collection(d, "ORC_A", "a.lab", "ORC_A_General.7z")
            write_collection(d, "ORC_B", "b.lab", "ORC_B_General.7z")
            engine, seven = no_tools()

            with engine, seven, self.assertRaises(RuntimeError) as caught, \
                    redirect_stdout(io.StringIO()):
                ingest.run_ingest(d)

            message = str(caught.exception)
            self.assertIn("2 collections", message)
            self.assertIn("a.lab", message)
            self.assertIn("b.lab", message)
            self.assertIn("orca fleet", message)

    def test_a_sealed_fleet_is_refused_once_its_outcomes_are_unsealed(self):
        """Review Focus 6: the Outcomes of an encrypted fleet only become readable inside
        run_ingest, so the refusal has to come after unsealing, not before."""
        with tempfile.TemporaryDirectory() as d:
            for stem, computer in (("ORC_A", "a.lab"), ("ORC_B", "b.lab")):
                with open(os.path.join(d, stem + "_Outcome.json.7z.p7b"), "wb") as f:
                    f.write(b"sealed")
                with open(os.path.join(d, stem + "_General.7z.p7b"), "wb") as f:
                    f.write(b"x")

            def unseal(sealed, target, seven, private_key=None, cms_backend=None):
                stem = os.path.basename(sealed).split("_Outcome")[0]
                computer = "a.lab" if stem == "ORC_A" else "b.lab"
                write_collection(d, stem, computer, stem + "_General.7z.p7b")
                return target

            engine, seven = no_tools()
            with engine, seven, mock.patch.object(ingest, "unseal_one", unseal), \
                    self.assertRaises(RuntimeError) as caught, redirect_stdout(io.StringIO()):
                ingest.run_ingest(d)

            self.assertIn("2 collections", str(caught.exception))

    def test_one_outcome_plus_a_stray_archive_is_one_collection(self):
        """Regression guard on test_an_archive_no_outcome_names_is_returned_too: the stray
        archive is folded into the named collection, with a word about why."""
        with tempfile.TemporaryDirectory() as d:
            write_collection(d, "ORC_A", "a.lab", "ORC_A_General.7z")
            with open(os.path.join(d, "ORC_Stray_General.7z"), "wb") as f:
                f.write(b"x")

            printed = io.StringIO()
            with redirect_stdout(printed):
                got = ingest.collection_of(d, [d])

            self.assertEqual(got.computer_name, "a.lab")
            self.assertEqual([os.path.basename(p) for p in got.archives],
                             ["ORC_A_General.7z", "ORC_Stray_General.7z"])
            self.assertIn("ORC_Stray_General.7z", printed.getvalue())

    def test_a_file_source_is_its_own_collection_with_the_outcome_beside_it(self):
        with tempfile.TemporaryDirectory() as d:
            write_collection(d, "ORC_A", "a.lab", "ORC_A_General.7z")
            archive = os.path.join(d, "ORC_A_General.7z")

            got = ingest.collection_of(archive, [d])

            self.assertEqual(got.archives, [archive])
            self.assertEqual(got.computer_name, "a.lab")
            self.assertEqual(os.path.basename(got.outcome_path), "ORC_A_Outcome.json")

    def test_an_empty_directory_is_refused(self):
        with tempfile.TemporaryDirectory() as d:
            with self.assertRaises(RuntimeError):
                ingest.collection_of(d, [d])


class TestSelectCollection(unittest.TestCase):
    def _found(self):
        return [
            ingest.Collection("/d/ORC_A_20260101_000000_Outcome.json", ["/d/a.7z"],
                              "a.lab", "{A}", "T1"),
            ingest.Collection("/d/ORC_B_20260101_000000_Outcome.json", ["/d/b.7z"],
                              "b.lab", "{B}", "T2"),
        ]

    def test_matches_computer_name_case_insensitively(self):
        got = ingest.select_collection(self._found(), "A.LAB")
        self.assertEqual(got.run_id, "{A}")

    def test_matches_the_run_name_the_dataset_directory_carries(self):
        got = ingest.select_collection(self._found(), "ORC_B_20260101_000000")
        self.assertEqual(got.run_id, "{B}")

    def test_falls_back_to_a_prefix_of_the_outcome_filename(self):
        got = ingest.select_collection(self._found(), "ORC_B")
        self.assertEqual(got.run_id, "{B}")

    def test_no_match_names_the_candidates(self):
        """Review Focus 4: a typo must not ingest nothing quietly."""
        with self.assertRaises(RuntimeError) as caught:
            ingest.select_collection(self._found(), "c.lab")
        self.assertIn("a.lab", str(caught.exception))
        self.assertIn("b.lab", str(caught.exception))

    def test_an_ambiguous_match_is_refused(self):
        """Review Focus 4: the same host collected twice cannot be picked by name alone."""
        twice = [
            ingest.Collection("/d/ORC_1_Outcome.json", ["/d/1.7z"], "a.lab", "{1}", "T1"),
            ingest.Collection("/d/ORC_2_Outcome.json", ["/d/2.7z"], "a.lab", "{2}", "T2"),
        ]
        with self.assertRaises(RuntimeError) as caught:
            ingest.select_collection(twice, "a.lab")
        self.assertIn("ORC_1_Outcome.json", str(caught.exception))
        self.assertIn("ORC_2_Outcome.json", str(caught.exception))


class TestIngestOneCollectionOfSeveral(unittest.TestCase):
    """With --collection, everything the dataset gets besides its tables is that run's."""

    def _fleet(self, d):
        write_collection(d, "ORC_A_20260101_000000", "a.lab", "ORC_A_General.7z")
        write_collection(d, "ORC_B_20260102_000000", "b.lab", "ORC_B_General.7z")
        for name in ("ORC_A_20260101_000000_Outline.json", "ORC_B_20260102_000000_Outline.json",
                     "ORC_A_20260101_000000.log", "ORC_B_20260102_000000.log"):
            with open(os.path.join(d, name), "w", encoding="utf-8") as f:
                f.write("{}")

    def test_the_dataset_the_outcome_the_outlines_and_the_logs_are_the_chosen_runs(self):
        with tempfile.TemporaryDirectory() as d:
            self._fleet(d)
            engine, seven = no_tools()
            with engine, seven, \
                    mock.patch.object(ingest, "discover_jobs", return_value=([], [])), \
                    mock.patch.object(ingest, "capture_logs", return_value=[]), \
                    redirect_stdout(io.StringIO()):
                ingest.run_ingest(d, collection_name="b.lab")

            dataset = os.path.join(os.getcwd(), "ORC_B_20260102_000000")
            self.assertTrue(os.path.isdir(dataset))
            self.assertFalse(os.path.isdir(os.path.join(os.getcwd(), "ORC_A_20260101_000000")))
            copied = sorted(n for n in os.listdir(dataset) if n != layout.ORCA_DIR)
            self.assertEqual(copied, ["ORC_B_20260102_000000.log",
                                      "ORC_B_20260102_000000_Outcome.json",
                                      "ORC_B_20260102_000000_Outline.json"])


class TestSidecarsOfOneRun(unittest.TestCase):
    def test_outlines_and_logs_are_filtered_by_run_name(self):
        with tempfile.TemporaryDirectory() as d, tempfile.TemporaryDirectory() as out:
            for name in ("ORC_A_Outline.json", "ORC_B_Outline.json", "ORC_A.log", "ORC_B.log"):
                with open(os.path.join(d, name), "w", encoding="utf-8") as f:
                    f.write("{}")

            outlines = ingest.capture_outlines([d], out, "ORC_A")
            logs = ingest.capture_sidecar_logs([d], out, "ORC_A")

            self.assertEqual(outlines, ["ORC_A_Outline.json"])
            self.assertEqual(logs, ["ORC_A.log"])
            self.assertEqual(sorted(os.listdir(out)), ["ORC_A.log", "ORC_A_Outline.json"])

    def test_no_run_name_keeps_everything_as_today(self):
        with tempfile.TemporaryDirectory() as d, tempfile.TemporaryDirectory() as out:
            for name in ("ORC_A_Outline.json", "ORC_B_Outline.json"):
                with open(os.path.join(d, name), "w", encoding="utf-8") as f:
                    f.write("{}")

            self.assertEqual(len(ingest.capture_outlines([d], out)), 2)


class TestIdentityColumns(unittest.TestCase):
    def test_values_come_from_the_outcome(self):
        collection = ingest.Collection("/d/ORC_A_Outcome.json", ["/d/a.7z"],
                                       "a.lab", "{A}", "20260101_000000")
        self.assertEqual(ingest.identity_columns(collection),
                         {"OrcComputerName": "a.lab", "OrcRunId": "{A}",
                          "OrcTimestamp": "20260101_000000"})

    def test_a_collection_with_no_outcome_stamps_empty_values(self):
        collection = ingest.Collection(None, ["/d/a.7z"], "", "", "")
        self.assertEqual(ingest.identity_columns(collection),
                         {"OrcComputerName": "", "OrcRunId": "", "OrcTimestamp": ""})


class TestPlainIngestStampsIdentity(unittest.TestCase):
    def test_every_job_of_a_plain_ingest_gets_the_outcomes_identity(self):
        """Not only under fleet: a dataset ingested by hand must answer the same glob."""
        with tempfile.TemporaryDirectory() as d:
            write_collection(d, "ORC_A", "a.lab", "ORC_A_General.7z", run_id="{A}")
            job = ingest.CsvJob("autoruns.csv", os.path.join(d, "ORC_A_General.7z"), "autoruns")
            seen = []

            def ingest_one(job, engine, out_dir, drop, make_csv, make_trunc, seven, password,
                           extra_columns=None, codepage=None):
                seen.append(extra_columns)
                return []

            engine, seven = no_tools()
            with engine, seven, \
                    mock.patch.object(ingest, "discover_jobs", return_value=([job], [])), \
                    mock.patch.object(ingest, "capture_logs", return_value=[]), \
                    mock.patch.object(ingest, "ingest_one", ingest_one), \
                    redirect_stdout(io.StringIO()):
                ingest.run_ingest(d)

            self.assertEqual(seen, [{"OrcComputerName": "a.lab", "OrcRunId": "{A}",
                                     "OrcTimestamp": "20260101_000000"}])


class TestIngestReadsTheHostCodepage(unittest.TestCase):
    def test_the_outlines_console_codepage_reaches_every_job(self):
        """A console tool's CSV is in the host's codepage; only the Outline says which one."""
        with tempfile.TemporaryDirectory() as d:
            write_collection(d, "ORC_A", "a.lab", "ORC_A_General.7z", run_id="{A}")
            with open(os.path.join(d, "ORC_A_Outline.json"), "w", encoding="utf-8") as f:
                json.dump({"dfir-orc": {"outline": {"system": {"codepage": 850}}}}, f)
            job = ingest.CsvJob("Systeminfo.csv", os.path.join(d, "ORC_A_General.7z"),
                                "Systeminfo")
            seen = []

            def ingest_one(job, engine, out_dir, drop, make_csv, make_trunc, seven, password,
                           extra_columns=None, codepage=None):
                seen.append(codepage)
                return []

            engine, seven = no_tools()
            with engine, seven,                     mock.patch.object(ingest, "discover_jobs", return_value=([job], [])),                     mock.patch.object(ingest, "capture_logs", return_value=[]),                     mock.patch.object(ingest, "ingest_one", ingest_one),                     redirect_stdout(io.StringIO()):
                ingest.run_ingest(d)

            self.assertEqual(seen, ["cp850"])


class TestOutcomeIdentity(unittest.TestCase):
    def test_reads_the_three_fields(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "X_Outcome.json")
            with open(path, "w", encoding="utf-8") as f:
                json.dump({"dfir-orc": {"outcome": {"id": "{A}", "computer_name": "a.lab",
                                                    "timestamp": "T"}}}, f)
            self.assertEqual(ingest.outcome_identity(path), ("a.lab", "{A}", "T"))

    def test_no_outcome_is_an_empty_identity(self):
        self.assertEqual(ingest.outcome_identity(None), ("", "", ""))


if __name__ == "__main__":
    unittest.main()
