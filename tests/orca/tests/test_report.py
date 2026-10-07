import csv
import io
import os
import shutil
import subprocess
import tempfile
import unittest
from contextlib import redirect_stdout
from unittest import mock
from orca import layout, report, sevenzip

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

        runs = report.find_runs([self.run], {self.run: self.side})
        rows = outcome_module.command_rows_from([r.outcome_path for r in runs])

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

    def test_the_log_view_counts_them(self):
        self.write(self.side, "ORC_HOST.log", "all fine\nan Error here\nFAILED too\n")

        view = report.build_log_view(self.run, [self.side])

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
        self.assertEqual([log["error_lines"] for log in hr["log"]["logs"]], [1])


class TestLogViewAvailability(unittest.TestCase):
    def test_no_logs_at_all_is_not_zero_matches(self):
        # A sealed collection has no plain '*.log', and '0 log(s) with matches' then reads as
        # reassurance when the truth is that nothing was read.
        with tempfile.TemporaryDirectory() as run:
            with open(os.path.join(run, "ORC_HOST.log.7z.p7b"), "wb") as f:
                f.write(b"sealed")

            self.assertFalse(report.build_log_view(run)["available"])

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

            view = report.build_log_view(run, [side])

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
                view = report.build_log_view(run, [side])

        self.assertEqual([log["log"] for log in view["logs"]], ["ORC_HOST.log"])

    def test_a_log_that_matched_nothing_is_still_a_log_that_was_read(self):
        with tempfile.TemporaryDirectory() as run:
            with open(os.path.join(run, "ORC_HOST.log"), "w", encoding="utf-8") as f:
                f.write("a clean run\nnothing to see\n")

            view = report.build_log_view(run)

            self.assertTrue(view["available"])
            self.assertEqual([log["error_lines"] for log in view["logs"]], [0])


ORC_LOG = (
    "2026-10-03T08:15:00.000Z [I] NTFSInfo: no Error in this Info line\n"
    "2026-10-03T08:15:01.000Z [W] volume is dirty\n"
    "2026-10-03T08:15:02.000Z [E] could not open volume\n"
    # The dump comes before the line that triggered it, which is how ORC writes it.
    "2026-10-03T08:15:03.000Z [I] --- BACKTRACE START ---\n"
    "2026-10-03T08:15:02.000Z [E] could not open volume\n"
    "2026-10-03T08:15:03.000Z [I] --- BACKTRACE END ---\n"
    "2026-10-03T08:15:03.000Z [C] aborting\n"
)


class TestLogViewLevels(unittest.TestCase):
    def view(self, payload: str) -> dict:
        with tempfile.TemporaryDirectory() as run:
            with open(os.path.join(run, "ORC_HOST.log"), "w", encoding="utf-8") as f:
                f.write(payload)

            return report.build_log_view(run)

    def test_each_log_is_broken_down_by_level(self):
        log = self.view(ORC_LOG)["logs"][0]

        self.assertEqual((log["warnings"], log["errors"], log["criticals"]), (1, 1, 1))
        self.assertTrue(log["exact"])

    def test_the_headline_count_is_errors_and_criticals_from_the_tags(self):
        # The word 'Error' in the Info line used to count and the untagged '[C]' did not.
        self.assertEqual(self.view(ORC_LOG)["logs"][0]["error_lines"], 2)

    def test_a_log_that_is_not_an_orc_log_says_so(self):
        log = self.view("a tool crashed\nRuntimeError: failed to start\n")["logs"][0]

        self.assertFalse(log["exact"])
        self.assertEqual(log["error_lines"], 1)


class TestMessageViews(unittest.TestCase):
    def view(self, files: dict, level: str = "critical") -> dict:
        with tempfile.TemporaryDirectory() as run:
            for name, payload in files.items():
                with open(os.path.join(run, name), "w", encoding="utf-8") as f:
                    f.write(payload)

            return report.build_message_view(report.build_log_view(
                run, context=5), level)

    def test_a_critical_is_named_with_the_log_it_came_from(self):
        view = self.view({"ORC_HOST.log": ORC_LOG})

        self.assertTrue(view["available"])
        self.assertEqual(view["messages"], [{
            "log": "ORC_HOST.log",
            "time": "2026-10-03T08:15:03.000Z",
            "level": "critical",
            "message": "aborting",
            # The block between them replays the error above, which the context keeps once.
            "before": [("2026-10-03T08:15:00.000Z", "info",
                        "NTFSInfo: no Error in this Info line"),
                       ("2026-10-03T08:15:01.000Z", "warning", "volume is dirty"),
                       ("2026-10-03T08:15:02.000Z", "error", "could not open volume")],
            "after": [],
        }])

    def test_a_view_holds_its_own_level_and_no_other(self):
        warn = self.view({"ORC_HOST.log": ORC_LOG}, "warning")
        error = self.view({"ORC_HOST.log": ORC_LOG}, "error")

        self.assertEqual([m["message"] for m in warn["messages"]], ["volume is dirty"])
        self.assertEqual([m["message"] for m in error["messages"]], ["could not open volume"])

    def test_criticals_from_several_logs_are_told_in_time_order(self):
        # A host's collection log and its per-command logs each hold part of the story; read
        # one log at a time it does not join up.
        view = self.view({
            "ORC_A.log": "2026-10-03T09:00:00.000Z [C] second\n",
            "ORC_B.log": "2026-10-03T08:00:00.000Z [C] first\n",
        })

        self.assertEqual([(c["log"], c["message"]) for c in view["messages"]],
                         [("ORC_B.log", "first"), ("ORC_A.log", "second")])

    def test_a_run_with_no_logs_read_is_not_a_run_with_no_criticals(self):
        self.assertFalse(self.view({})["available"])

    def test_the_host_report_carries_each_level_as_a_view_of_its_own(self):
        with tempfile.TemporaryDirectory() as run:
            with open(os.path.join(run, "ORC_HOST.log"), "w", encoding="utf-8") as f:
                f.write(ORC_LOG)

            hr = report.build_host_report(run)

        self.assertEqual([c["message"] for c in hr["critical"]["messages"]], ["aborting"])
        self.assertEqual([c["message"] for c in hr["error"]["messages"]],
                         ["could not open volume"])
        self.assertEqual([c["message"] for c in hr["warn"]["messages"]], ["volume is dirty"])

    def test_the_host_report_keeps_no_context_unless_it_was_asked_for(self):
        with tempfile.TemporaryDirectory() as run:
            with open(os.path.join(run, "ORC_HOST.log"), "w", encoding="utf-8") as f:
                f.write(ORC_LOG)

            quiet = report.build_host_report(run)
            asked = report.build_host_report(run, context=3)

        self.assertEqual(quiet["critical"]["messages"][0]["before"], [])
        self.assertEqual([text for _, _, text in asked["critical"]["messages"][0]["before"]],
                         ["volume is dirty", "could not open volume"])


SEVEN_ZIP = sevenzip.find_7z()


@unittest.skipUnless(SEVEN_ZIP, "7z.exe not found")
class TestArchivedLogs(unittest.TestCase):
    """The per-command logs, which sit inside the archive until something pulls them out.

    Without this a report on a collection as it arrives reads WolfLauncher's own log, finds it
    clean, and says nothing at all about a command that died with a critical in its own log.
    """

    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)
        self.run = os.path.join(self.dir.name, "collection")
        self.work = os.path.join(self.dir.name, "work")
        os.makedirs(self.run)
        src = os.path.join(self.dir.name, "src")
        os.makedirs(src)
        with open(os.path.join(src, "NTFSInfo.dev.log"), "w", encoding="utf-8") as f:
            f.write("2026-10-03T09:14:22.100Z [C] Failed to open volume\n")
        with open(os.path.join(src, "NTFSInfo.csv"), "w", encoding="utf-8") as f:
            f.write("a,b\n1,2\n")
        subprocess.run([SEVEN_ZIP, "a", "-t7z",
                        os.path.join(self.run, "ORC_HOST_General.7z"),
                        os.path.join(src, "*")], capture_output=True, text=True, check=True)
        with open(os.path.join(self.run, "ORC_HOST_Outcome.json"), "w", encoding="utf-8") as f:
            f.write('{"dfir-orc": {"outcome": {"computer_name": "HOST", "command_set": '
                    '[{"name": "General", "archive": {"name": "ORC_HOST_General.7z"}, '
                    '"commands": [{"name": "NTFSInfo", "exit_code": 1, "start": "", '
                    '"end": ""}]}]}}}')

    def runs(self) -> list:
        return report.find_runs([self.run], {})

    def seal_the_archive(self, keep_plain: bool = False) -> None:
        """What an encrypted collection looks like: the Outcome names the '.p7b' ORC wrote."""
        with open(os.path.join(self.run, "ORC_HOST_General.7z.p7b"), "wb") as f:
            f.write(b"sealed")
        if not keep_plain:
            os.remove(os.path.join(self.run, "ORC_HOST_General.7z"))

        path = os.path.join(self.run, "ORC_HOST_Outcome.json")
        with open(path, encoding="utf-8") as f:
            doc = f.read()
        with open(path, "w", encoding="utf-8") as f:
            f.write(doc.replace("ORC_HOST_General.7z", "ORC_HOST_General.7z.p7b"))

    def test_the_log_inside_the_archive_becomes_one_of_the_runs_extra_dirs(self):
        expanded = report.with_archived_logs(self.runs(), self.work, SEVEN_ZIP)

        found = report.run_log_files(expanded[0].directory, expanded[0].extra_dirs)
        self.assertIn("NTFSInfo.dev.log", [os.path.basename(p) for p in found])

    def test_the_collected_data_is_left_where_it_was(self):
        expanded = report.with_archived_logs(self.runs(), self.work, SEVEN_ZIP)

        pulled = []
        for directory in expanded[0].extra_dirs:
            for _root, _dirs, files in os.walk(directory):
                pulled += files
        self.assertNotIn("NTFSInfo.csv", pulled)

    def test_the_critical_reaches_the_report(self):
        expanded = report.with_archived_logs(self.runs(), self.work, SEVEN_ZIP)
        run = expanded[0]

        hr = report.build_host_report(run.directory, None, run.extra_dirs, run.outcome_path)

        self.assertEqual([c["message"] for c in hr["critical"]["messages"]],
                         ["Failed to open volume"])

    def test_a_second_report_does_not_decode_the_archive_again(self):
        report.with_archived_logs(self.runs(), self.work, SEVEN_ZIP)

        with mock.patch("orca.sevenzip.extract_matching") as extract:
            expanded = report.with_archived_logs(self.runs(), self.work, SEVEN_ZIP)

        extract.assert_not_called()
        self.assertTrue(report.run_log_files(expanded[0].directory, expanded[0].extra_dirs))

    def test_refresh_decodes_it_again(self):
        report.with_archived_logs(self.runs(), self.work, SEVEN_ZIP)

        with mock.patch("orca.sevenzip.extract_matching",
                        wraps=sevenzip.extract_matching) as extract:
            report.with_archived_logs(self.runs(), self.work, SEVEN_ZIP, refresh=True)

        self.assertEqual(extract.call_count, 1)

    def test_a_sealed_archive_is_named_rather_than_passed_over(self):
        # Decrypting a multi-GB envelope is the whole cost of an extract, which is not what a
        # report is for. Saying nothing would leave the gap invisible, which is the state this
        # whole change is about.
        # ORC records the name it wrote, so a sealed collection's Outcome names the '.p7b'.
        self.seal_the_archive()

        printed = io.StringIO()
        with redirect_stdout(printed):
            report.with_archived_logs(self.runs(), self.work, SEVEN_ZIP)

        self.assertIn("ORC_HOST_General.7z.p7b", printed.getvalue())
        self.assertIn("orca ingest", printed.getvalue())

    def test_the_notice_is_one_line_that_says_there_is_more(self):
        # It is a signpost, not an explanation: the archive plain, the command in brackets.
        self.seal_the_archive()

        printed = io.StringIO()
        with redirect_stdout(printed):
            report.with_archived_logs(self.runs(), self.work, SEVEN_ZIP)

        said = printed.getvalue().strip()
        self.assertEqual(len(said.splitlines()), 1, said)
        self.assertIn("more", said)
        self.assertNotIn("(ORC_HOST_General.7z.p7b)", said)
        self.assertRegex(said, r"\(orca ingest .+\)$")

    def test_a_fleet_of_sealed_collections_says_it_once(self):
        # The same reasoning as orphan_lines: a line per host buries the thing it is saying.
        self.seal_the_archive()
        second = os.path.join(self.dir.name, "second")
        shutil.copytree(self.run, second)
        # A copy is the same run - find_runs identifies one by its Outcome's name - so the
        # second has to be another host right down to the file names.
        with open(os.path.join(second, "ORC_HOST_Outcome.json"), encoding="utf-8") as f:
            doc = f.read()
        os.remove(os.path.join(second, "ORC_HOST_Outcome.json"))
        with open(os.path.join(second, "ORC_HOST2_Outcome.json"), "w", encoding="utf-8") as f:
            f.write(doc.replace("HOST", "HOST2"))
        os.rename(os.path.join(second, "ORC_HOST_General.7z.p7b"),
                  os.path.join(second, "ORC_HOST2_General.7z.p7b"))

        printed = io.StringIO()
        with redirect_stdout(printed):
            report.with_archived_logs(report.find_runs([self.run, second], {}),
                                      self.work, SEVEN_ZIP)

        self.assertEqual(printed.getvalue().count("orca ingest"), 1)
        self.assertIn("2 sealed archives", printed.getvalue())

    def test_the_copy_orca_already_decrypted_is_read_rather_than_called_sealed(self):
        # 'ingest' and 'extract' leave the decrypted archive beside the sealed one, and the
        # Outcome goes on naming the sealed one.
        self.seal_the_archive(keep_plain=True)

        printed = io.StringIO()
        with redirect_stdout(printed):
            expanded = report.with_archived_logs(self.runs(), self.work, SEVEN_ZIP)

        found = report.run_log_files(expanded[0].directory, expanded[0].extra_dirs)
        self.assertIn("NTFSInfo.dev.log", [os.path.basename(p) for p in found])
        self.assertEqual(printed.getvalue(), "")


class TestLogsSeenTwice(unittest.TestCase):
    """A log in the run dir and the same log in an extra dir is one log.

    'ingest' leaves the per-command logs beside the tables, and a report on that dataset also
    pulls them out of the archives still sitting there - so the commonest case of all had
    every log counted twice and every critical reported twice.
    """

    def places(self, run_names: list, extra_names: list) -> list:
        root = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, root, True)
        run, extra = os.path.join(root, "run"), os.path.join(root, "extra")
        for directory, names in ((run, run_names), (extra, extra_names)):
            for name in names:
                path = os.path.join(directory, name)
                os.makedirs(os.path.dirname(path), exist_ok=True)
                with open(path, "w", encoding="utf-8") as f:
                    f.write("x\n")

        return report.run_log_files(run, [extra])

    def test_the_same_log_in_both_places_is_read_once(self):
        found = self.places(["NTFSInfo.dev.log"], ["NTFSInfo.dev.log"])

        self.assertEqual(len(found), 1)

    def test_the_run_directorys_copy_is_the_one_kept(self):
        found = self.places(["NTFSInfo.dev.log"], ["NTFSInfo.dev.log"])

        self.assertIn(os.path.join("run", "NTFSInfo.dev.log"), found[0])

    def test_two_logs_of_the_same_name_in_different_places_are_two_logs(self):
        # 'Errors/NTFSInfo.dev.log' is the failing run's copy; the top-level one is not it.
        found = self.places(["NTFSInfo.dev.log"], [os.path.join("Errors", "NTFSInfo.dev.log")])

        self.assertEqual(len(found), 2)


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


def write_outcome(directory: str, host: str, run_id: str = "", code: int = 0) -> str:
    """An Outcome named the way ORC names one, so the sidecars of a run can be told apart."""
    import json

    document = {"dfir-orc": {"outcome": {
        "computer_name": host, "id": run_id or "{" + host + "}",
        "start": "2026-09-25T14:41:27Z", "end": "2026-09-25T14:48:14Z",
        "command_set": [{"name": "ORC_General",
                         "archive": {"name": f"ORC_{host}_General.7z", "size": 1024},
                         "commands": [{"name": "USNInfo", "tool": "USNInfo",
                                       "exit_code": code, "start": "", "end": ""}]}]}}}
    path = os.path.join(directory, f"ORC_{host}_20260925_144127_Outcome.json")
    with open(path, "w", encoding="utf-8") as f:
        json.dump(document, f)

    return path


def mark_ingested(directory: str) -> None:
    os.makedirs(os.path.join(directory, layout.ORCA_DIR), exist_ok=True)
    with open(layout.timing_path(directory), "w", encoding="utf-8") as f:
        f.write("kind,name,rows,bytes,seconds\n")


class TestFindRuns(unittest.TestCase):
    """One directory, several collections.

    A fleet arrives either as a directory of per-host folders or as one directory holding every
    machine's files side by side - a 'merged' dump - and the second shape used to report the
    first Outcome found and silently drop the rest. The Outcome, not the directory, is what
    identifies a run.
    """

    def setUp(self):
        self.root = tempfile.TemporaryDirectory()
        self.addCleanup(self.root.cleanup)
        self.dir = self.root.name

    def hosts(self, *paths) -> list:
        return sorted(run.outcome_path and os.path.basename(run.outcome_path)
                      for run in report.find_runs(report.candidate_dirs(list(paths)), {}))

    def test_every_outcome_in_one_directory_is_its_own_run(self):
        for host in ("WS-0001", "WS-0002", "WS-0003"):
            write_outcome(self.dir, host)

        runs = report.find_runs(report.candidate_dirs([self.dir]), {})

        self.assertEqual(len(runs), 3)
        self.assertEqual(sorted(os.path.basename(r.outcome_path) for r in runs),
                         ["ORC_WS-0001_20260925_144127_Outcome.json",
                          "ORC_WS-0002_20260925_144127_Outcome.json",
                          "ORC_WS-0003_20260925_144127_Outcome.json"])

    def test_a_directory_of_per_host_folders_needs_recurse(self):
        # Looking inside is the surprising half: asked about a directory, report answers about
        # that directory.
        for host in ("WS-0001", "WS-0002"):
            child = os.path.join(self.dir, host)
            os.makedirs(child)
            write_outcome(child, host)

        self.assertEqual(report.find_runs(report.candidate_dirs([self.dir]), {}), [])

        runs = report.find_runs(report.candidate_dirs([self.dir], recurse=True), {})

        self.assertEqual(len(runs), 2)
        self.assertEqual(sorted(os.path.basename(r.directory) for r in runs),
                         ["WS-0001", "WS-0002"])

    def test_recurse_goes_all_the_way_down(self):
        deep = os.path.join(self.dir, "2026", "october", "WS-0007")
        os.makedirs(deep)
        write_outcome(deep, "WS-0007")

        self.assertEqual(report.find_runs(report.candidate_dirs([self.dir]), {}), [])
        self.assertEqual(len(report.find_runs(report.candidate_dirs([self.dir], recurse=True),
                                              {})), 1)

    def test_a_merged_directory_needs_no_recurse(self):
        # The Outcomes are in the directory itself, which is the shape a merged dump arrives in.
        for host in ("WS-0001", "WS-0002"):
            write_outcome(self.dir, host)

        self.assertEqual(len(report.find_runs(report.candidate_dirs([self.dir]), {})), 2)

    def test_a_collection_and_the_run_dir_inside_it_are_one_run(self):
        # 'ingest' copies the Outcome into the run it writes inside the collection, so the same
        # run is visible twice; the ingested copy is the one worth reporting, it has the tables.
        write_outcome(self.dir, "WS-0001")
        run = os.path.join(self.dir, "ORC_WS-0001_Offline")
        os.makedirs(run)
        write_outcome(run, "WS-0001")
        mark_ingested(run)

        runs = report.find_runs(report.candidate_dirs([self.dir], recurse=True), {})

        self.assertEqual(len(runs), 1)
        self.assertEqual(runs[0].directory, run)

    def test_an_ingested_run_with_no_outcome_is_still_a_run(self):
        mark_ingested(self.dir)

        runs = report.find_runs(report.candidate_dirs([self.dir]), {})

        self.assertEqual([r.directory for r in runs], [self.dir])
        self.assertIsNone(runs[0].outcome_path)

    def test_a_sealed_outcome_is_read_from_where_it_was_unsealed(self):
        side = os.path.join(self.root.name, "unsealed")
        os.makedirs(side)
        collection = os.path.join(self.dir, "sealed")
        os.makedirs(collection)
        with open(os.path.join(collection, "ORC_WS-0007_Outcome.json.7z.p7b"), "wb") as f:
            f.write(b"sealed")
        write_outcome(side, "WS-0007")

        runs = report.find_runs([collection], {collection: side})

        self.assertEqual(len(runs), 1)
        self.assertEqual(runs[0].directory, collection)
        self.assertEqual(runs[0].extra_dirs, [side])

    def test_a_directory_with_nothing_in_it_yields_no_run(self):
        self.assertEqual(report.find_runs(report.candidate_dirs([self.dir]), {}), [])

    def test_a_directory_that_is_neither_a_run_nor_a_collection_yields_no_run(self):
        os.makedirs(os.path.join(self.dir, "an-expanded-tree"))
        with open(os.path.join(self.dir, "loose.7z"), "wb") as f:
            f.write(b"not a directory at all")

        self.assertEqual(report.find_runs(report.candidate_dirs([self.dir]), {}), [])

    def test_a_sealed_log_alone_does_not_make_a_collection(self):
        # Only an Outcome says a directory is a collection; a stray sealed log says nothing.
        child = os.path.join(self.dir, "ORC_HOST")
        os.makedirs(child)
        with open(os.path.join(child, "ORC_HOST.log.7z.p7b"), "wb") as f:
            f.write(b"sealed log")

        self.assertEqual(report.find_runs(report.candidate_dirs([self.dir], recurse=True),
                                          {}), [])

    def test_runs_come_back_in_host_order(self):
        for host in ("WS-0003", "WS-0001", "WS-0002"):
            write_outcome(self.dir, host)

        runs = report.find_runs(report.candidate_dirs([self.dir]), {})

        self.assertEqual([os.path.basename(r.outcome_path).split("_")[1] for r in runs],
                         ["WS-0001", "WS-0002", "WS-0003"])

    def test_a_path_that_does_not_exist_is_an_error(self):
        with self.assertRaises(RuntimeError) as caught:
            report.candidate_dirs(["E:/definitely/not/here"])

        self.assertIn("no such", str(caught.exception))

    def test_a_file_is_not_a_directory_of_collections(self):
        path = os.path.join(self.dir, "ORC_HOST.7z")
        with open(path, "wb") as f:
            f.write(b"an archive, not a directory")

        with self.assertRaises(RuntimeError):
            report.candidate_dirs([path])


class TestLogAttribution(unittest.TestCase):
    """Which logs belong to which run, when one directory holds several.

    ORC names a run's files after the run - 'ORC_<host>_<stamp>.log' beside
    'ORC_<host>_<stamp>_Outcome.json' - so in a merged directory a log can be attributed to the
    collection it came from instead of to all of them.
    """

    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)
        self.first = write_outcome(self.dir.name, "WS-0001")
        self.second = write_outcome(self.dir.name, "WS-0002")
        for host in ("WS-0001", "WS-0002"):
            path = os.path.join(self.dir.name, f"ORC_{host}_20260925_144127.log")
            with open(path, "w", encoding="utf-8") as f:
                f.write("an Error here\n")

    def test_the_stem_of_a_run_is_its_outcome_without_the_suffix(self):
        self.assertEqual(report.log_stem(self.first), "ORC_WS-0001_20260925_144127")

    def test_a_run_in_a_shared_directory_reads_only_its_own_log(self):
        view = report.build_log_view(self.dir.name, None, report.log_stem(self.first))

        self.assertEqual([log["log"] for log in view["logs"]],
                         ["ORC_WS-0001_20260925_144127.log"])

    def test_without_a_stem_every_log_of_the_directory_is_read(self):
        # A run directory of its own: the collection's log, each command's log out of the
        # expanded Errors container, and ORC's console output all belong to the one run.
        view = report.build_log_view(self.dir.name)

        self.assertEqual(len(view["logs"]), 2)


class TestOrphanArchives(unittest.TestCase):
    """An archive in the directory that no Outcome accounts for.

    In a merged dump that is the thing worth noticing: a collection whose Outcome never
    arrived, so nothing in the report says anything about it.
    """

    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)

    def archive(self, name: str, size: int = 32) -> str:
        path = os.path.join(self.dir.name, name)
        with open(path, "wb") as f:
            f.write(b"x" * size)
        return path

    def test_an_archive_no_outcome_names_is_reported_with_its_size(self):
        write_outcome(self.dir.name, "WS-0001")
        self.archive("ORC_WS-0001_General.7z", 1024)
        self.archive("ORC_WS-0009_Orphan.7z", 32)

        rows = report.orphan_rows(self.dir.name)

        self.assertEqual(rows, [["ORC_WS-0009_Orphan.7z", "32 bytes"]])

    def test_every_outcome_of_the_directory_has_its_say(self):
        # Two collections side by side: an archive named by either of them is accounted for.
        write_outcome(self.dir.name, "WS-0001")
        write_outcome(self.dir.name, "WS-0002")
        self.archive("ORC_WS-0001_General.7z")
        self.archive("ORC_WS-0002_General.7z")

        self.assertEqual(report.orphan_rows(self.dir.name), [])

    def test_an_archive_an_outcome_elsewhere_names_is_no_orphan(self):
        # 'orca fleet' leaves the archives it decrypts in its output directory, while their
        # Outcomes sit one level down, in each host's dataset.
        self.archive("ORC_WS-0001_General.7z")

        rows = report.orphan_rows(self.dir.name, accounted=["ORC_WS-0001_General.7z.p7b"])

        self.assertEqual(rows, [])

    def test_a_directory_with_no_archive_at_all_has_no_orphan(self):
        write_outcome(self.dir.name, "WS-0001")

        self.assertEqual(report.orphan_rows(self.dir.name), [])


class TestRunReport(unittest.TestCase):
    """The command over a directory: every collection in it, and what nothing accounts for."""

    def setUp(self):
        self.root = tempfile.TemporaryDirectory()
        self.addCleanup(self.root.cleanup)
        self.dir = self.root.name

    def invoke(self, *paths, **kwargs) -> str:
        out = io.StringIO()
        with redirect_stdout(out):
            report.run_report(list(paths) or [self.dir], **kwargs)
        return out.getvalue()

    def test_a_merged_directory_reports_every_collection_in_it(self):
        for host, code in (("WS-0001", 18), ("WS-0002", 9), ("WS-0003", 0)):
            write_outcome(self.dir, host, code=code)

        output = self.invoke(one_line=True)

        self.assertEqual(len(output.strip().splitlines()), 3)
        for host in ("WS-0001", "WS-0002", "WS-0003"):
            self.assertIn(host, output)

    def test_a_directory_of_per_host_folders_reports_each_of_them_with_recurse(self):
        for host in ("WS-0001", "WS-0002"):
            child = os.path.join(self.dir, host)
            os.makedirs(child)
            write_outcome(child, host)

        output = self.invoke(one_line=True, recurse=True)

        self.assertEqual(len(output.strip().splitlines()), 2)

    def test_without_recurse_a_parent_of_collections_says_it_found_nothing(self):
        child = os.path.join(self.dir, "WS-0001")
        os.makedirs(child)
        write_outcome(child, "WS-0001")

        with self.assertRaises(RuntimeError) as caught:
            self.invoke()

        self.assertIn("--recurse", str(caught.exception))

    def test_a_merged_directory_of_two_collections_needs_nothing_extra(self):
        write_outcome(self.dir, "WS-0001")
        write_outcome(self.dir, "WS-0002")

        output = self.invoke(one_line=True)

        self.assertEqual(len(output.strip().splitlines()), 2)

    def test_a_run_reads_only_its_own_log_when_the_directory_is_shared(self):
        write_outcome(self.dir, "WS-0001")
        write_outcome(self.dir, "WS-0002")
        for host, lines in (("WS-0001", 1), ("WS-0002", 3)):
            path = os.path.join(self.dir, f"ORC_{host}_20260925_144127.log")
            with open(path, "w", encoding="utf-8") as f:
                f.write("2026-09-25T14:41:27.000Z [E] an error here\n" * lines)

        output = self.invoke(views=["log"])

        self.assertIn("ORC_WS-0001_20260925_144127.log  0W  1E  0C", output)
        self.assertIn("ORC_WS-0002_20260925_144127.log  0W  3E  0C", output)

    def test_an_archive_nothing_accounts_for_is_named(self):
        write_outcome(self.dir, "WS-0001")
        with open(os.path.join(self.dir, "ORC_WS-0009_Orphan.7z"), "wb") as f:
            f.write(b"y" * 32)

        output = self.invoke()

        self.assertIn("archives no Outcome names (1)", output)
        self.assertIn("ORC_WS-0009_Orphan.7z  32 bytes", output)

    def test_the_orphan_block_names_its_directory_once(self):
        for host in ("WS-0001", "WS-0002"):
            write_outcome(self.dir, host)
        with open(os.path.join(self.dir, "ORC_WS-0009_Orphan.7z"), "wb") as f:
            f.write(b"y")

        output = self.invoke()

        self.assertEqual(output.count("archives no Outcome names"), 1)

    def test_a_directory_whose_archives_are_all_named_says_nothing(self):
        write_outcome(self.dir, "WS-0001")
        with open(os.path.join(self.dir, "ORC_WS-0001_General.7z"), "wb") as f:
            f.write(b"y")

        self.assertNotIn("archives no Outcome", self.invoke())

    def test_nothing_to_report_is_an_error_naming_what_was_looked_at(self):
        with self.assertRaises(RuntimeError) as caught:
            self.invoke()

        self.assertIn("no collection", str(caught.exception))


class TestIgnoringFindings(unittest.TestCase):
    """What an ignore list does to each view.

    Nothing is hidden: the view keeps a count of what it passed over, so a report can say
    '7 of 12, 5 ignored' rather than quietly disagreeing with the Outcome.
    """

    def test_an_ignored_command_leaves_the_failures_and_is_counted(self):
        from orca.outcome import Failure, Outcome

        run = Outcome(host="H", start="", end="", duration_seconds=1.0, commands=5,
                      failures=[Failure("ORC_General", "Autoruns", "1", 0.0, ""),
                                Failure("ORC_General", "USNInfo", "18", 22.0, "")],
                      memory_peak_bytes=1024, memory_peak_set="ORC_General")

        view = report.build_outcome_view(run, ["Autoruns"])

        self.assertEqual([f["command"] for f in view["failures"]], ["USNInfo"])
        self.assertEqual(view["failures_ignored"], 1)

    def test_a_command_can_be_ignored_in_one_command_set_only(self):
        from orca.outcome import Failure, Outcome

        run = Outcome(host="H", start="", end="", duration_seconds=1.0, commands=5,
                      failures=[Failure("ORC_General", "Autoruns", "1", 0.0, ""),
                                Failure("ORC_Little", "Autoruns", "1", 0.0, "")],
                      memory_peak_bytes=0, memory_peak_set="")

        view = report.build_outcome_view(run, ["ORC_Little/Autoruns"])

        self.assertEqual([f["set"] for f in view["failures"]], ["ORC_General"])

    def test_a_key_alone_passes_over_every_failure_of_that_key(self):
        from orca.outcome import Failure, Outcome

        run = Outcome(host="H", start="", end="", duration_seconds=1.0, commands=5,
                      failures=[Failure("ORC_General", "Autoruns", "1", 0.0, ""),
                                Failure("ORC_General", "USNInfo", "1", 0.0, ""),
                                Failure("ORC_FastFind", "FastFind", "1", 0.0, "")],
                      memory_peak_bytes=0, memory_peak_set="")

        view = report.build_outcome_view(run, ["ORC_General"])

        self.assertEqual([f["set"] for f in view["failures"]], ["ORC_FastFind"])
        self.assertEqual(view["failures_ignored"], 2)

    def test_a_run_whose_every_failure_is_expected_reads_as_ok(self):
        # The point of the list: a command that always fails on this fleet is not news.
        from orca.outcome import Failure, Outcome

        run = Outcome(host="H", start="", end="", duration_seconds=1.0, commands=5,
                      failures=[Failure("ORC_General", "Autoruns", "1", 0.0, "")],
                      memory_peak_bytes=0, memory_peak_set="")

        self.assertTrue(report.build_outcome_view(run, ["Autoruns"])["ok"])
        self.assertFalse(report.build_outcome_view(run, [])["ok"])

    def test_an_ignored_empty_output_leaves_the_list_and_is_counted(self):
        timing = [{"name": "VSS_list", "rows": 0, "bytes": 10, "seconds": 0.1},
                  {"name": "NTFSInfo", "rows": 100, "bytes": 5000, "seconds": 1.0}]

        view = report.build_empty_view(timing, ["Azure_Attested.json", "EventConsumer.log"],
                                       ignore=["Azure_*", "VSS_list"])

        self.assertEqual(view["empty_files"], ["EventConsumer.log"])
        self.assertEqual(view["file_count"], 1)
        self.assertEqual(view["files_ignored"], 1)
        self.assertEqual(view["empty_tables"], [])
        self.assertEqual(view["count"], 0)
        self.assertEqual(view["tables_ignored"], 1)

    def test_an_ignored_log_and_an_ignored_unread_table_are_counted(self):
        with tempfile.TemporaryDirectory() as run:
            for name, payload in (("USNInfo.dev.log", "an Error here\n"),
                                  ("Autoruns.dev.log", "an Error here\n")):
                with open(os.path.join(run, name), "w", encoding="utf-8") as f:
                    f.write(payload)
            os.makedirs(os.path.join(run, layout.ORCA_DIR))
            with open(os.path.join(run, layout.ORCA_DIR, "skipped.txt"), "w",
                      encoding="utf-8") as f:
                f.write("autoruns\tduckdb said no\n")

            view = report.build_log_view(run, None, None, ["Autoruns*", "autoruns"])

        self.assertEqual([log["log"] for log in view["logs"]], ["USNInfo.dev.log"])
        self.assertEqual(view["logs_ignored"], 1)
        self.assertEqual(view["skipped"], {})
        self.assertEqual(view["skipped_ignored"], 1)


if __name__ == "__main__":
    unittest.main()
