import csv
import os
import tempfile
import unittest
from unittest import mock
from orca import layout, report

TIMING = [
    {"name": "NTFSInfo", "rows": 100, "bytes": 5000, "seconds": 2.0},
    {"name": "Empty1", "rows": 0, "bytes": 10, "seconds": 0.1},
    {"name": "USNInfo", "rows": 50, "bytes": 9000, "seconds": 0.5},
]


class TestReportViews(unittest.TestCase):
    def test_build_empty_view(self):
        v = report.build_empty_view(TIMING)
        self.assertEqual(v["empty_tables"], ["Empty1"])
        self.assertEqual(v["count"], 1)
        self.assertEqual(v["empty_files"], [])

    def test_build_empty_view_reports_empty_output_files(self):
        v = report.build_empty_view(TIMING, ["b.log", "a.json"])
        self.assertEqual(v["empty_files"], ["a.json", "b.log"])  # sorted
        self.assertEqual(v["file_count"], 2)

    def test_empty_output_files_asks_only_for_orc_outputs(self):
        # Parity with Test-OrcExpandedResults: a zero-byte collected sample is ordinary and
        # must not be reported; only ORC's own outputs count.
        class FakeEngine:
            def __init__(self):
                self.sql = None

            def query_rows(self, sql, limit):
                self.sql = sql
                return [{"relpath": "Foo.log"}]

        with tempfile.TemporaryDirectory() as d:
            manifest = os.path.join(d, "manifest.parquet")
            with open(manifest, "wb") as f:
                f.write(b"")

            engine = FakeEngine()
            self.assertEqual(report.empty_output_files(engine, manifest), ["Foo.log"])

        self.assertIn("CAST(size AS BIGINT) = 0", engine.sql)
        for suffix in report.OUTPUT_SUFFIXES:
            self.assertIn(f"'%{suffix}'", engine.sql)

    def test_empty_output_files_without_a_manifest(self):
        self.assertEqual(report.empty_output_files(object(), "nope/manifest.parquet"), [])

    def test_build_stats_view(self):
        v = report.build_stats_view(TIMING)
        self.assertEqual(v["tables"], 3)
        self.assertEqual(v["total_rows"], 150)
        self.assertEqual(v["total_bytes"], 14010)
        self.assertEqual(v["biggest"][0]["name"], "USNInfo")
        self.assertEqual(v["slowest"][0]["name"], "NTFSInfo")

    def test_load_timing_reads_only_parquet_rows(self):
        with tempfile.TemporaryDirectory() as d:
            os.makedirs(layout.orca_dir(d))
            with open(layout.timing_path(d), "w", newline="") as f:
                w = csv.writer(f)
                w.writerow(["name", "kind", "rows", "bytes", "seconds"])
                w.writerow(["T", "parquet", "100", "5000", "2.0"])
                w.writerow(["T", "csv", "100", "8000", "0.3"])
            rows = report.load_timing(d)
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]["rows"], 100)
        self.assertEqual(rows[0]["bytes"], 5000)


class TestStatsTableList(unittest.TestCase):
    """One list of every table, with size, rows and time together.

    'biggest' and 'slowest' said the same thing twice - on a real run four of the five biggest
    were also the five slowest - and since the report folds nothing away there is no reason to
    choose which tables to show at all.
    """

    TIMING = [
        {"name": "Big", "rows": 400, "bytes": 9000, "seconds": 0.2},
        {"name": "Middling", "rows": 300, "bytes": 5000, "seconds": 0.1},
        {"name": "Small", "rows": 10, "bytes": 100, "seconds": 0.05},
        {"name": "SlowButSmall", "rows": 20, "bytes": 200, "seconds": 9.0},
    ]

    def test_the_list_carries_size_rows_and_time_together(self):
        top = report.build_stats_view(self.TIMING)["top"]

        self.assertEqual(top[0]["name"], "Big")
        self.assertEqual(top[0]["bytes"], 9000)
        self.assertEqual(top[0]["rows"], 400)
        self.assertEqual(top[0]["seconds"], 0.2)

    def test_every_table_is_there(self):
        names = [t["name"] for t in report.build_stats_view(self.TIMING)["top"]]

        self.assertEqual(sorted(names), ["Big", "Middling", "SlowButSmall", "Small"])

    def test_no_table_is_listed_twice(self):
        names = [t["name"] for t in report.build_stats_view(self.TIMING)["top"]]

        self.assertEqual(len(names), len(set(names)))

    def test_the_list_is_ordered_by_size(self):
        top = report.build_stats_view(self.TIMING)["top"]

        self.assertEqual([t["bytes"] for t in top], [9000, 5000, 200, 100])


class TestViewAvailability(unittest.TestCase):
    """A missing input must not read as a zero.

    'report' takes an ingested run dir, but it is pointed at an extracted tree often enough -
    the Outcome and the logs are both there - and then the two views that read '.orca' have
    nothing to read. 'stats: 0 tables' and 'empty: 0 table(s)' are then indistinguishable from
    a genuine finding, which is the one thing a health report must not get wrong.
    """

    def test_stats_of_a_real_ingest_is_available(self):
        self.assertTrue(report.build_stats_view(TIMING)["available"])

    def test_stats_says_when_there_was_no_timing_to_read(self):
        self.assertFalse(report.build_stats_view([], available=False)["available"])

    def test_the_empty_view_tracks_its_two_inputs_separately(self):
        # A run ingested without duckdb has real timing and no manifest. Calling that "no
        # empty files" would relabel one missing input as the other.
        view = report.build_empty_view(TIMING, None, files_available=False)

        self.assertTrue(view["available"])
        self.assertFalse(view["files_available"])

    def test_a_directory_that_was_never_ingested_reports_neither_half(self):
        view = report.build_empty_view([], None, available=False, files_available=False)

        self.assertFalse(view["available"])
        self.assertFalse(view["files_available"])

    def test_an_extracted_tree_has_an_outcome_but_no_timing(self):
        # Exactly what 'orca report <extracted tree>' hits: ORC's own files are there, the
        # ones ingest writes are not.
        with tempfile.TemporaryDirectory() as directory:
            with open(os.path.join(directory, "ORC_Outcome.json"), "w", encoding="utf-8") as f:
                f.write('{"dfir-orc": {"outcome": {"computer_name": "HOST", '
                        '"command_set": []}}}')

            hr = report.build_host_report(directory)

        self.assertTrue(hr["outcome"]["available"])
        self.assertFalse(hr["stats"]["available"])
        self.assertFalse(hr["empty"]["available"])
        self.assertFalse(hr["empty"]["files_available"])

    def test_an_ingested_run_reports_its_timing_as_available(self):
        with tempfile.TemporaryDirectory() as directory:
            os.makedirs(os.path.join(directory, layout.ORCA_DIR))
            with open(layout.timing_path(directory), "w", newline="", encoding="utf-8") as f:
                writer = csv.writer(f)
                writer.writerow(["kind", "name", "rows", "bytes", "seconds"])
                writer.writerow(["parquet", "NTFSInfo", "100", "5000", "2.0"])

            hr = report.build_host_report(directory)

        self.assertTrue(hr["stats"]["available"])
        self.assertTrue(hr["empty"]["available"])
        self.assertEqual(hr["stats"]["tables"], 1)


class TestRunDirDiscovery(unittest.TestCase):
    def test_a_run_dir_that_does_not_exist_is_an_error(self):
        # It used to report cheerfully on nothing: no Outcome, no tables, all zeros. A typo
        # has to be a typo, which is also what makes a mistyped mode name legible.
        with self.assertRaises(RuntimeError) as caught:
            report.discover_run_dirs(["E:/definitely/not/here"], None)

        self.assertIn("no such run dir", str(caught.exception))

    def test_an_existing_run_dir_is_kept(self):
        with tempfile.TemporaryDirectory() as directory:
            self.assertEqual(report.discover_run_dirs([directory], None), [directory])

    def test_root_discovery_only_takes_ingested_runs(self):
        with tempfile.TemporaryDirectory() as parent:
            ingested = os.path.join(parent, "run")
            os.makedirs(os.path.join(ingested, layout.ORCA_DIR))
            with open(layout.timing_path(ingested), "w", encoding="utf-8") as f:
                f.write("kind,name,rows,bytes,seconds\n")
            os.makedirs(os.path.join(parent, "an-extracted-tree"))

            self.assertEqual(report.discover_run_dirs([], parent), [ingested])


OUTCOME_DOC = ('{"dfir-orc": {"outcome": {"computer_name": "HOST", "command_set": '
               '[{"name": "ORC_General", "statistics": {"process_memory_peak": 2048}, '
               '"commands": [{"name": "USNInfo", "exit_code": 18, "start": "", "end": ""}, '
               '{"name": "NTFSInfo", "exit_code": 0, "start": "", "end": ""}]}]}}}')


class TestReadingFromASecondDirectory(unittest.TestCase):
    """A sealed collection has its Outcome, Outline and log unsealed somewhere else.

    Nothing may be written into the collection, so the views are told where else to look rather
    than everything being copied into the run dir first.
    """

    def setUp(self):
        self.root = tempfile.TemporaryDirectory()
        self.addCleanup(self.root.cleanup)
        self.run = os.path.join(self.root.name, "collection")
        self.side = os.path.join(self.root.name, "unsealed")
        os.makedirs(self.run)
        os.makedirs(self.side)

    def write(self, directory: str, name: str, payload: str) -> str:
        path = os.path.join(directory, name)
        with open(path, "w", encoding="utf-8") as f:
            f.write(payload)
        return path

    def test_the_outcome_is_found_in_the_second_directory(self):
        self.write(self.side, "ORC_Outcome.json", OUTCOME_DOC)

        from orca import outcome as outcome_module

        self.assertIsNone(outcome_module.outcome_file(self.run))
        self.assertIsNotNone(outcome_module.outcome_file(self.run, [self.side]))

    def test_the_run_dir_wins_when_both_hold_one(self):
        # The run dir is the collection as it is; an unsealed copy is only a stand-in.
        mine = self.write(self.run, "ORC_Outcome.json", OUTCOME_DOC)
        self.write(self.side, "ORC_Outcome.json", OUTCOME_DOC)

        from orca import outcome as outcome_module

        self.assertEqual(outcome_module.outcome_file(self.run, [self.side]), mine)

    def test_the_command_rows_come_from_the_second_directory_too(self):
        self.write(self.side, "ORC_Outcome.json", OUTCOME_DOC)

        from orca import outcome as outcome_module

        rows = outcome_module.collect_command_rows([self.run], {self.run: self.side})

        self.assertEqual([r["command"] for r in rows], ["USNInfo", "NTFSInfo"])

    def test_the_outlines_are_found_in_the_second_directory(self):
        self.write(self.side, "ORC_Outline.json", '{"dfir-orc": {"outline": {}}}')

        from orca import outline as outline_module

        self.assertEqual(outline_module.outline_files(self.run), [])
        self.assertEqual(len(outline_module.outline_files(self.run, [self.side])), 1)

    def test_the_logs_of_the_second_directory_are_read(self):
        self.write(self.side, "ORC_HOST.log", "all fine\nan Error here\n")

        self.assertEqual(report.run_log_files(self.run), [])
        self.assertEqual(len(report.run_log_files(self.run, [self.side])), 1)

    def test_the_errors_view_counts_them(self):
        self.write(self.side, "ORC_HOST.log", "all fine\nan Error here\nFAILED too\n")

        view = report.build_errors_view(self.run, [self.side])

        self.assertEqual([log["error_lines"] for log in view["logs"]], [2])

    def test_the_host_comes_from_the_unsealed_outcome_not_the_directory_name(self):
        # Without this a sealed collection is reported under its folder name.
        self.write(self.side, "ORC_Outcome.json", OUTCOME_DOC)

        self.assertEqual(report.host_of(self.run), "collection")
        self.assertEqual(report.host_of(self.run, [self.side]), "HOST")

    def test_the_whole_report_is_built_from_both_directories(self):
        self.write(self.side, "ORC_Outcome.json", OUTCOME_DOC)
        self.write(self.side, "ORC_HOST.log", "an Error here\n")

        hr = report.build_host_report(self.run, None, [self.side])

        self.assertEqual(hr["host"], "HOST")
        self.assertTrue(hr["outcome"]["available"])
        self.assertEqual(hr["outcome"]["commands"], 2)
        self.assertEqual([log["error_lines"] for log in hr["errors"]["logs"]], [1])

    def test_root_also_finds_a_collection_that_was_never_ingested(self):
        # A folder of collections as they arrive is what a fleet sweep is pointed at, and
        # nothing in it has an '.orca' yet.
        with tempfile.TemporaryDirectory() as parent:
            raw = os.path.join(parent, "ORC_HOST")
            os.makedirs(raw)
            with open(os.path.join(raw, "ORC_Outcome.json"), "w", encoding="utf-8") as f:
                f.write(OUTCOME_DOC)

            self.assertEqual(report.discover_run_dirs([], parent), [raw])

    def test_root_finds_a_collection_whose_outcome_is_still_sealed(self):
        # By name, never by decrypting: discovery has to stay cheap and must not need a key.
        with tempfile.TemporaryDirectory() as parent:
            raw = os.path.join(parent, "ORC_HOST")
            os.makedirs(raw)
            with open(os.path.join(raw, "ORC_HOST_Outcome.json.7z.p7b"), "wb") as f:
                f.write(b"sealed, and not opened to answer this")

            self.assertEqual(report.discover_run_dirs([], parent), [raw])

    def test_root_still_ignores_a_directory_that_is_neither(self):
        with tempfile.TemporaryDirectory() as parent:
            os.makedirs(os.path.join(parent, "an-expanded-tree"))
            with open(os.path.join(parent, "loose.7z"), "wb") as f:
                f.write(b"not a directory at all")

            self.assertEqual(report.discover_run_dirs([], parent), [])

    def test_a_sealed_log_alone_does_not_make_a_directory_reportable(self):
        # Only an Outcome says "this is a collection"; a stray sealed log says nothing.
        with tempfile.TemporaryDirectory() as parent:
            raw = os.path.join(parent, "ORC_HOST")
            os.makedirs(raw)
            with open(os.path.join(raw, "ORC_HOST.log.7z.p7b"), "wb") as f:
                f.write(b"sealed log")

            self.assertEqual(report.discover_run_dirs([], parent), [])


class TestErrorsViewAvailability(unittest.TestCase):
    def test_no_logs_at_all_is_not_zero_matches(self):
        # A sealed collection has no plain '*.log', and '0 log(s) with matches' then reads as
        # reassurance when the truth is that nothing was read.
        with tempfile.TemporaryDirectory() as run:
            with open(os.path.join(run, "ORC_HOST.log.7z.p7b"), "wb") as f:
                f.write(b"sealed")

            self.assertFalse(report.build_errors_view(run)["available"])

    def test_a_log_from_outside_the_run_is_named_by_its_file_name(self):
        # The unsealed sidecars live in the work dir, which is routinely on another volume
        # than the collection: naming the log relative to the run dir gives either a path full
        # of '..' or, across drives on Windows, a ValueError out of relpath.
        with tempfile.TemporaryDirectory() as root:
            run = os.path.join(root, "collection")
            side = os.path.join(root, "unsealed")
            os.makedirs(run)
            os.makedirs(side)
            with open(os.path.join(side, "ORC_HOST.log"), "w", encoding="utf-8") as f:
                f.write("an Error here\n")

            view = report.build_errors_view(run, [side])

        self.assertEqual([log["log"] for log in view["logs"]], ["ORC_HOST.log"])

    def test_a_log_on_another_mount_does_not_blow_up(self):
        # What a UNC collection and a C: work dir actually do to os.path.relpath.
        with tempfile.TemporaryDirectory() as root:
            run = os.path.join(root, "collection")
            side = os.path.join(root, "unsealed")
            os.makedirs(run)
            os.makedirs(side)
            with open(os.path.join(side, "ORC_HOST.log"), "w", encoding="utf-8") as f:
                f.write("an Error here\n")

            def across_mounts(*_args, **_kwargs):
                raise ValueError("path is on mount C:, start on mount UNC")

            with mock.patch("os.path.relpath", across_mounts):
                view = report.build_errors_view(run, [side])

        self.assertEqual([log["log"] for log in view["logs"]], ["ORC_HOST.log"])

    def test_a_log_that_matched_nothing_is_still_a_log_that_was_read(self):
        with tempfile.TemporaryDirectory() as run:
            with open(os.path.join(run, "ORC_HOST.log"), "w", encoding="utf-8") as f:
                f.write("a clean run\nnothing to see\n")

            view = report.build_errors_view(run)

            self.assertTrue(view["available"])
            self.assertEqual([log["error_lines"] for log in view["logs"]], [0])


class TestUnsealedSidecarDirs(unittest.TestCase):
    """Where a sealed collection's Outcome, Outline and log are unsealed to.

    The work dir, never the collection: 'ingest' and 'extract' unseal in place on purpose so
    the next command finds plain files, but a report only reads, and what it reads has to stay
    as it was found.
    """

    def setUp(self):
        self.root = tempfile.TemporaryDirectory()
        self.addCleanup(self.root.cleanup)
        self.work = os.path.join(self.root.name, "work")
        os.makedirs(self.work)

    def sealed_collection(self, name: str) -> str:
        directory = os.path.join(self.root.name, name)
        os.makedirs(directory)
        with open(os.path.join(directory, "ORC_HOST_Outcome.json.7z.p7b"), "wb") as f:
            f.write(b"sealed")
        return directory

    def test_a_plain_collection_needs_nothing_unsealed(self):
        directory = os.path.join(self.root.name, "plain")
        os.makedirs(directory)
        with open(os.path.join(directory, "ORC_Outcome.json"), "w", encoding="utf-8") as f:
            f.write(OUTCOME_DOC)

        self.assertEqual(report.unsealed_sidecar_dirs([directory], self.work), {})
        self.assertEqual(os.listdir(self.work), [])

    def test_two_collections_of_the_same_name_do_not_unseal_over_each_other(self):
        # Two machines' collections routinely share a folder name under one work dir.
        first = report.sidecar_dir("/dumps/a/ORC_HOST", self.work)
        second = report.sidecar_dir("/dumps/b/ORC_HOST", self.work)

        self.assertNotEqual(first, second)
        self.assertTrue(os.path.basename(first).startswith("ORC_HOST-"), first)

    def test_without_7z_it_says_so_and_leaves_the_rest_of_the_report_alone(self):
        directory = self.sealed_collection("ORC_SEALED")

        with mock.patch.object(report.sevenzip, "find_7z", return_value=None):
            mapped = report.unsealed_sidecar_dirs([directory], self.work)

        self.assertEqual(mapped, {})


if __name__ == "__main__":
    unittest.main()
