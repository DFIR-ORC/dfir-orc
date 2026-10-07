import json
import re
import unittest
from orca import logs, report


def host_report() -> dict:
    """A report of a run that was ingested: every view has its input."""
    return {
        "host": "TESTHOST", "directory": "d",
        "outcome": {"available": True, "ok": False,
                    "failures": [{"set": "S", "command": "c", "exit_code": "0x1",
                                  "duration_seconds": 5.0, "start": ""}],
                    "duration_seconds": 1620.0, "memory_peak_bytes": 1503238553,
                    "memory_peak_set": "S", "commands": 35},
        "empty": {"empty_tables": ["E1"], "count": 1},
        "stats": {"tables": 25, "total_rows": 100, "total_bytes": 13207024435,
                  "biggest": [], "slowest": []},
        "log": {"available": True, "logs": []},
        "warn": {"available": True, "messages": []},
        "error": {"available": True, "messages": []},
        "critical": {"available": True, "messages": []},
    }


def full_host_report() -> dict:
    """A run with something in every section, shaped as the view builders produce it."""
    return {
        "host": "DESKTOP-U9N26B0",
        "directory": "E:/runs/DESKTOP-U9N26B0",
        "outcome": {
            "available": True, "ok": False, "commands": 35,
            "duration_seconds": 407.0, "memory_peak_bytes": 1325150208,
            "memory_peak_set": "ORC_General", "failures_ignored": 0,
            "failures": [
                {"set": "ORC_General", "command": f"Command{i}", "tool": "GetThis",
                 "exit_code": "9", "duration_seconds": 51.0, "start": ""}
                for i in range(7)
            ] + [{"set": "ORC_General", "command": "Autoruns", "tool": "",
                  "exit_code": "1", "duration_seconds": 0.0, "start": ""}],
        },
        "empty": {
            "available": True, "files_available": True, "tables_ignored": 0,
            "files_ignored": 0,
            "empty_tables": ["USNInfo_00000000_DiskInterface_0x9e1e94d71e94aa33_", "VSS_list"],
            "count": 2,
            "empty_files": [f"Empty{i}.json" for i in range(9)], "file_count": 9,
        },
        "stats": {
            "available": True, "tables": 26, "total_rows": 1598014, "total_bytes": 67541237,
            "biggest": [], "slowest": [],
            "top": [{"name": "NTFSInfo_00000001_Volume_C_", "bytes": 30408704,
                     "rows": 415398, "seconds": 1.95},
                    {"name": "I30Info_00000001_Volume_C_", "bytes": 21810380,
                     "rows": 798432, "seconds": 1.23},
                    {"name": "SlowButSmall", "bytes": 2048, "rows": 20, "seconds": 9.0}],
        },
        "log": {
            "available": True, "logs_ignored": 0, "skipped_ignored": 0,
            "logs": ([{"log": f"Command{i}.dev.log", "bytes": 100, "error_lines": 2398 - i,
                       "warnings": 10 + i, "errors": 2397 - i, "criticals": 1, "exact": True,
                       "messages": [{"time": "2026-10-03T08:15:03.000Z", "level": "critical",
                                     "message": "m", "before": [], "after": []}]}
                      for i in range(8)]
                     + [{"log": "Quiet.dev.log", "bytes": 100, "error_lines": 0,
                         "warnings": 0, "errors": 0, "criticals": 0, "exact": True}]),
            "skipped": {"autoruns": "duckdb failed: Invalid Input Error: it was not possible "
                                    "to detect the CSV dialect"},
        },
        "warn": {
            "available": True,
            "messages": [
                {"log": "Command0.dev.log", "time": "2026-10-03T08:15:01.000Z",
                 "level": "warning", "message": "volume C: is dirty",
                 "before": [], "after": []},
            ],
        },
        "error": {
            "available": True,
            "messages": [
                {"log": "Command0.dev.log", "time": "2026-10-03T08:15:02.500Z",
                 "level": "error", "message": "CreateFile failed [access denied]",
                 "before": [("2026-10-03T08:15:02.000Z", "debug", "opening volume")],
                 "after": [("2026-10-03T08:15:03.000Z", "info", "giving up on C:")]},
                {"log": "Command1.dev.log", "time": "2026-10-03T08:15:59.000Z",
                 "level": "error", "message": "could not write the archive",
                 "before": [], "after": []},
            ],
        },
        "critical": {
            "available": True,
            "messages": [
                {"log": "Command0.dev.log", "time": "2026-10-03T08:15:03.000Z",
                 "level": "critical",
                 "message": "Failed to open volume \\\\.\\C: [access denied]",
                 "before": [("2026-10-03T08:15:02.000Z", "debug", "opening volume"),
                            ("2026-10-03T08:15:02.500Z", "error", "CreateFile failed")],
                 "after": [("2026-10-03T08:15:03.500Z", "info", "collection aborted")]},
                {"log": "Command1.dev.log", "time": "2026-10-03T08:16:00.000Z",
                 "level": "critical",
                 "message": "Job was authoritatively terminated",
                 "before": [], "after": []},
            ],
        },
    }


def host_report_not_ingested() -> dict:
    """An extracted tree: the Outcome and the logs are real, nothing from '.orca' is."""
    hr = host_report()
    hr["stats"] = {"available": False, "tables": 0, "total_rows": 0, "total_bytes": 0,
                   "biggest": [], "slowest": []}
    hr["empty"] = {"available": False, "files_available": False, "empty_tables": [],
                   "count": 0, "empty_files": [], "file_count": 0}
    return hr


class TestRender(unittest.TestCase):
    def _hr(self):
        return host_report()

    def _hr_not_ingested(self):
        return host_report_not_ingested()

    def _hr_with_skips(self):
        hr = self._hr()
        hr["log"]["skipped"] = {"autoruns": "duckdb said no"}
        return hr

    def test_the_stats_view_names_the_command_that_would_fill_it(self):
        # report never ingests - it is fast and fetching what is missing is not - so it says
        # what to run instead of quietly doing it.
        hr = self._hr_not_ingested()
        hr["directory"] = "E:/dumps/ORC_HOST"

        text = report.render_human(hr, ["stats"])

        self.assertIn("not ingested", text)
        self.assertIn("orca ingest E:/dumps/ORC_HOST", text)
        self.assertNotIn("0 tables", text)

    def test_the_empty_view_does_not_claim_zero_empty_tables(self):
        text = report.render_human(self._hr_not_ingested(), ["empty"])

        self.assertIn("not ingested", text)
        self.assertNotIn("(none)", text)

    def test_the_empty_file_half_says_when_there_is_no_manifest(self):
        hr = self._hr()
        hr["empty"] = {"available": True, "files_available": False, "empty_tables": ["E1"],
                       "count": 1, "empty_files": [], "file_count": 0}

        text = report.render_human(hr, ["empty"])

        self.assertIn("empty tables (1)", text)
        self.assertIn("no manifest", text)

    def test_one_line_prints_a_question_mark_rather_than_a_zero(self):
        line = report.render_one_line(report.summary_of(self._hr_not_ingested()))

        self.assertIn("tables=?", line)
        self.assertIn("size=?", line)
        self.assertNotIn("tables=0", line)

    def test_a_summary_of_an_uningested_run_carries_nulls_not_zeros(self):
        summary = report.summary_of(self._hr_not_ingested())

        self.assertIsNone(summary["tables"])
        self.assertIsNone(summary["total_bytes"])
        self.assertIsNone(summary["empty"])

    def test_summary_of(self):
        s = report.summary_of(self._hr())
        self.assertEqual(s["host"], "TESTHOST")
        self.assertEqual(s["ok"], False)
        self.assertEqual(s["fails"], 1)
        self.assertEqual(s["empty"], 1)
        self.assertEqual(s["tables"], 25)
        self.assertEqual(s["unread"], 0)

    def test_an_unread_table_is_counted(self):
        self.assertEqual(report.summary_of(self._hr_with_skips())["unread"], 1)

    def test_one_line_stays_quiet_when_nothing_was_skipped(self):
        # A field that always reads zero stops being read, and this one says the run is
        # missing a table rather than the collection missing an artifact.
        self.assertNotIn("unread=", report.render_one_line(report.summary_of(self._hr())))

    def test_one_line_says_so_when_something_was(self):
        line = report.render_one_line(report.summary_of(self._hr_with_skips()))
        self.assertIn("unread=1", line)

    def test_the_log_view_names_the_unread_table_and_why(self):
        text = report.render_human(self._hr_with_skips(), ["log"])
        self.assertIn("unread tables (1)", text)
        self.assertIn("autoruns", text)
        self.assertIn("duckdb said no", text)

    def test_the_log_view_says_nothing_when_the_run_is_complete(self):
        self.assertNotIn("unread", report.render_human(self._hr(), ["log"]))

    def test_render_one_line_has_host_and_fields(self):
        line = report.render_one_line(report.summary_of(self._hr()))
        self.assertIn("TESTHOST:", line)
        self.assertIn("ok=False", line)
        self.assertIn("fails=1", line)
        self.assertIn("empty=1", line)

    def test_the_one_line_carries_the_command_count(self):
        # A denominator for 'fails': 12 of 14 is a dead machine, 12 of 300 is noise.
        line = report.render_one_line(report.summary_of(host_report()))

        self.assertIn("cmds=35", line)
        self.assertIn("fails=1", line)

    def test_a_run_with_no_outcome_does_not_invent_a_command_count(self):
        hr = host_report()
        hr["outcome"] = {"available": False}

        line = report.render_one_line(report.summary_of(hr))

        self.assertIn("cmds=?", line)

    def test_the_one_line_names_empty_output_files_when_there_are_some(self):
        # Computed by summary_of all along and never rendered, so the zero-byte ORC output
        # check reached the views and the JSON but never the summary line.
        hr = host_report()
        hr["empty"] = {"available": True, "files_available": True, "empty_tables": [],
                       "count": 0, "empty_files": ["a.csv", "b.log", "c.log"],
                       "file_count": 3}

        self.assertIn("empty_files=3", report.render_one_line(report.summary_of(hr)))

    def test_the_one_line_stays_quiet_when_no_output_file_was_empty(self):
        self.assertNotIn("empty_files=",
                         report.render_one_line(report.summary_of(host_report())))

    def test_an_unread_manifest_does_not_claim_zero_empty_files(self):
        self.assertNotIn("empty_files=",
                         report.render_one_line(report.summary_of(host_report_not_ingested())))

    def test_render_json_is_list_of_summaries(self):
        out = report.render_json([report.summary_of(self._hr())])
        data = json.loads(out)
        self.assertEqual(data[0]["host"], "TESTHOST")
        self.assertEqual(data[0]["fails"], 1)


class TestHostJson(unittest.TestCase):
    """The JSON shape: flat enough to filter in one line, complete enough to be worth having.

    'orca report --root E:/runs --json | ConvertFrom-Json | ? fails -gt 0' has to be a
    one-liner, which it is not if the fields a filter wants sit three levels down.
    """

    def test_the_summary_scalars_stay_at_the_top_level(self):
        obj = report.host_json(host_report())

        self.assertEqual(obj["host"], "TESTHOST")
        self.assertEqual(obj["fails"], 1)
        self.assertEqual(obj["tables"], 25)
        self.assertEqual(obj["directory"], "d")

    def test_every_view_detail_hangs_off_a_key_below(self):
        obj = report.host_json(host_report())

        self.assertEqual([f["command"] for f in obj["failures"]], ["c"])
        self.assertEqual(obj["empty_tables"], ["E1"])
        self.assertEqual(obj["logs"], [])

    def test_it_says_which_inputs_were_readable(self):
        obj = report.host_json(host_report_not_ingested())

        self.assertTrue(obj["outcome_available"])
        self.assertFalse(obj["stats_available"])
        self.assertFalse(obj["empty_available"])
        self.assertFalse(obj["empty_files_available"])

    def test_an_unreadable_input_is_null_rather_than_zero(self):
        obj = report.host_json(host_report_not_ingested())

        self.assertIsNone(obj["tables"])
        self.assertIsNone(obj["total_bytes"])

    def test_the_whole_thing_serialises_as_one_array(self):
        data = json.loads(report.render_json([report.host_json(host_report())]))

        self.assertEqual(data[0]["host"], "TESTHOST")
        self.assertEqual(data[0]["empty_tables"], ["E1"])


def command_row(host: str, command: str, code: str, ok: bool, start: str, tool: str = "",
                seconds: float = 22.0, peak=1325150208) -> dict:
    """One row as outcome.command_rows() builds it, with only the fields --list reads."""
    return {"computer_name": host, "command_set": "ORC_General", "command": command,
            "tool": tool, "duration_seconds": seconds, "set_memory_peak": peak,
            "exit_code": code, "ok": "true" if ok else "false", "start": start}


COMMAND_ROWS = [
    command_row("DESKTOP-U9N26B0", "USNInfo", "18", False, "2026-09-25T14:41:29Z", "USNInfo"),
    command_row("DESKTOP-U9N26B0", "capsule", "0", True, "2026-09-25T14:41:28Z"),
    command_row("TESTHOST", "NTFSInfo", "0", True, "2026-09-25T14:50:00Z", "NTFSInfo",
                seconds=62.0, peak=432013312),
]


class TestRenderCommandLines(unittest.TestCase):
    """One line per command, no per-host headers: the output you scan across N machines."""

    def lines(self, rows=None, failed_only=False) -> list:
        text = report.render_command_lines(rows if rows is not None else COMMAND_ROWS,
                                           failed_only)
        return [line for line in text.splitlines() if line.strip()]

    def test_the_machine_comes_first_and_the_exit_code_last(self):
        line = self.lines()[0]

        self.assertTrue(line.startswith("DESKTOP-U9N26B0"), line)
        self.assertTrue(line.rstrip().endswith("0"), line)

    def test_a_command_running_an_external_binary_reads_as_a_dash(self):
        # 'tool' is null in the Outcome for anything that is not an ORC subtool, which is
        # worth seeing rather than blanking out.
        self.assertIn("-", self.lines()[0])

    def test_lines_run_by_host_then_in_the_order_orc_ran_them(self):
        hosts_and_commands = [(line.split()[0], line.split()[2]) for line in self.lines()[:3]]

        self.assertEqual(hosts_and_commands,
                         [("DESKTOP-U9N26B0", "capsule"), ("DESKTOP-U9N26B0", "USNInfo"),
                          ("TESTHOST", "NTFSInfo")])

    def test_every_command_is_listed_by_default(self):
        self.assertEqual(len(self.lines()), 4)  # three commands plus the summary

    def test_failed_only_keeps_the_nonzero_exits(self):
        kept = self.lines(failed_only=True)

        self.assertEqual(len(kept), 2)  # one failure plus the summary
        self.assertIn("USNInfo", kept[0])

    def test_a_code_that_is_not_a_number_counts_as_a_failure(self):
        rows = [command_row("H", "Weird", "0xdeadbeef", False, "2026-09-25T14:00:00Z")]

        self.assertIn("Weird", self.lines(rows, failed_only=True)[0])

    def test_the_summary_counts_every_command_even_when_filtered(self):
        # The denominator has to survive the filter, or '1 failed' says nothing.
        summary = self.lines(failed_only=True)[-1]

        self.assertIn("3 command(s)", summary)
        self.assertIn("1 failed", summary)
        self.assertIn("2 host(s)", summary)

    def test_a_clean_fleet_filtered_is_just_the_summary(self):
        clean = [command_row("H", "NTFSInfo", "0", True, "2026-09-25T14:00:00Z", "NTFSInfo")]

        kept = self.lines(clean, failed_only=True)

        self.assertEqual(len(kept), 1)
        self.assertIn("0 failed", kept[0])

    def test_the_columns_line_up_across_hosts_of_different_name_lengths(self):
        lines = self.lines()[:3]

        starts = {line.index("ORC_General") for line in lines}
        self.assertEqual(len(starts), 1, lines)

    def test_a_peak_the_outcome_never_recorded_reads_as_a_question_mark(self):
        rows = [command_row("H", "NTFSInfo", "0", True, "2026-09-25T14:00:00Z", "NTFSInfo",
                            peak="")]

        self.assertIn("?", self.lines(rows)[0])

    def test_the_exit_code_is_hex(self):
        rows = [command_row("H", "Processes1", "2147500036", False, "2026-09-25T14:00:00Z")]

        self.assertTrue(self.lines(rows)[0].endswith("0x80004004"), self.lines(rows)[0])

    def test_nothing_to_list_says_so(self):
        self.assertIn("0 command(s)", report.render_command_lines([], False))


class TestRenderSections(unittest.TestCase):
    """The default report: a header, then a counted section per thing worth seeing.

    Lists are capped so that one host stays on a screen - the real run printed 72 lines, 33 of
    them logs - and the count in the heading still says how many there really are.
    """

    def render(self, hr=None, views=None, explicit=False) -> str:
        return report.render_human(hr or full_host_report(),
                                   views or ["outcome", "empty", "stats", "log"],
                                   explicit=explicit)

    def lines(self, **kwargs) -> list:
        return self.render(**kwargs).splitlines()

    def test_the_header_is_the_host_and_where_it_came_from(self):
        first = self.lines()[0]

        self.assertTrue(first.startswith("DESKTOP-U9N26B0"), first)
        self.assertIn("DESKTOP-U9N26B0", first)
        self.assertIn("runs/DESKTOP-U9N26B0", first)

    def test_the_scalars_follow_on_one_line(self):
        second = self.lines()[1]

        for field in ("ok=False", "cmds=35", "fails=8", "dur=6m47s", "mem=1.2GB"):
            self.assertIn(field, second)
        self.assertIn("ORC_General", second)

    def test_a_failure_is_columnised_with_the_code_last(self):
        line = next(l for l in self.lines() if "Autoruns" in l)

        self.assertEqual(line.split(), ["ORC_General", "Autoruns", "-", "0m0s", "cpu=?",
                                        "read=?", "wrote=?", "0x00000001"])

    def test_a_failure_says_what_the_process_did(self):
        # No CPU and nothing read is a command that never started; a full timeout's worth of
        # reading is one that was cut short.
        hr = full_host_report()
        hr["outcome"]["failures"] = [{"set": "ORC_General", "command": "Processes1", "tool": "",
                                      "exit_code": "2147500036", "duration_seconds": 300.0,
                                      "start": "", "cpu_seconds": 3.0, "read_bytes": 2048,
                                      "write_bytes": 1024}]

        line = next(l for l in self.lines(hr=hr) if "Processes1" in l)

        self.assertEqual(line.split()[3:], ["5m0s", "cpu=0m3s", "read=2.0KB", "wrote=1.0KB",
                                            "0x80004004"])

    def test_the_failure_columns_line_up(self):
        failures = [l for l in self.lines() if "Command" in l and "dev.log" not in l]

        starts = {l.index("ORC_General") for l in failures}
        self.assertEqual(len(starts), 1, failures)

    def test_each_section_counts_what_it_holds(self):
        text = self.render()

        self.assertIn("failed commands (8)", text)
        self.assertIn("empty tables (2)", text)
        self.assertIn("empty outputs (9)", text)
        self.assertIn("unread tables (1)", text)

    def test_the_tables_section_shows_size_rows_and_time_together(self):
        line = next(l for l in self.lines() if "NTFSInfo_00000001_Volume_C_" in l)

        self.assertIn("29.0MB", line)
        self.assertIn("415398", line)
        self.assertIn("1.95", line)

    def test_the_tables_heading_carries_the_totals(self):
        self.assertIn("tables (26, 1598014 rows, 64.4MB)", self.render())

    def test_the_logs_heading_says_how_many_of_how_many(self):
        # Eight logs were read, one of them quiet.
        self.assertIn("logs with errors (8 of 9)", self.render())

    def test_a_quiet_log_is_not_listed(self):
        self.assertNotIn("Quiet.dev.log", self.render())

    def test_a_log_is_broken_down_by_level(self):
        line = next(l for l in self.lines() if "Command0.dev.log" in l)

        self.assertEqual(line.split(), ["Command0.dev.log", "10W", "2397E", "1C"])

    def test_a_log_nothing_could_parse_claims_no_level_breakdown(self):
        # Zeros under W and C would assert something the substring fallback cannot know.
        hr = full_host_report()
        hr["log"]["logs"] = [{"log": "crash.txt", "bytes": 10, "error_lines": 3,
                                 "warnings": 0, "errors": 0, "criticals": 0, "exact": False}]

        line = next(l for l in report.render_human(hr, ["log"]).splitlines()
                    if "crash.txt" in l)

        self.assertEqual(line.split(), ["crash.txt", "~3E"])

    def test_a_log_that_could_not_be_read_says_so_rather_than_counting_minus_one(self):
        hr = full_host_report()
        hr["log"]["logs"] = [{"log": "locked.log", "bytes": 10, "error_lines": -1,
                                 "warnings": 0, "errors": 0, "criticals": 0, "exact": False}]

        line = next(l for l in report.render_human(hr, ["log"]).splitlines()
                    if "locked.log" in l)

        self.assertEqual(line.split(), ["locked.log", "unreadable"])

    def test_every_failure_is_listed(self):
        # Eight of them, Autoruns last: a report that hides rows makes you run it again.
        failures = [l for l in self.lines() if "ORC_General" in l and "fails=" not in l]

        self.assertEqual(len(failures), 8)

    def test_every_empty_output_is_listed(self):
        text = self.render()

        for index in range(9):
            self.assertIn(f"Empty{index}.json", text)

    def test_nothing_is_folded_away(self):
        self.assertNotIn("more", self.render())

    def test_every_log_that_carried_something_is_listed(self):
        logs = [l for l in self.lines() if "dev.log" in l]

        self.assertEqual(len(logs), 8)

    def test_a_section_with_nothing_in_it_is_left_out(self):
        hr = full_host_report()
        hr["outcome"]["failures"] = []
        hr["empty"].update({"empty_tables": [], "count": 0, "empty_files": [],
                            "file_count": 0})

        text = self.render(hr)

        self.assertNotIn("failed commands", text)
        self.assertNotIn("empty tables", text)
        self.assertIn("fails=0", text)

    def test_an_explicitly_named_view_shows_its_heading_even_at_zero(self):
        # Asking for a view and getting nothing back cannot be told from a broken report.
        hr = full_host_report()
        hr["empty"].update({"empty_tables": [], "count": 0, "empty_files": [],
                            "file_count": 0})

        text = self.render(hr, views=["empty"], explicit=True)

        self.assertIn("empty tables (0)", text)

    def test_a_view_whose_input_is_missing_still_says_so(self):
        text = self.render(host_report_not_ingested())

        self.assertIn("not ingested", text)

    def test_the_unread_table_names_the_reason(self):
        line = next(l for l in self.lines() if "autoruns" in l)

        self.assertIn("autoruns", line)
        self.assertIn("duckdb failed", line)
        self.assertLessEqual(len(line), 100)

    def test_no_line_runs_past_a_hundred_columns(self):
        for line in self.lines():
            self.assertLessEqual(len(line), 100, line)


def host_report_with_ignores() -> dict:
    """The same run, with some findings passed over."""
    hr = full_host_report()
    hr["outcome"]["failures"] = hr["outcome"]["failures"][:3]
    hr["outcome"]["failures_ignored"] = 5
    hr["empty"]["empty_files"] = hr["empty"]["empty_files"][:5]
    hr["empty"]["file_count"] = 5
    hr["empty"]["files_ignored"] = 4
    hr["log"]["logs_ignored"] = 2
    return hr


class TestRenderIgnored(unittest.TestCase):
    """A heading says what it kept and what it passed over, so the report never silently
    disagrees with the Outcome."""

    def render(self, hr=None) -> str:
        return report.render_human(hr or host_report_with_ignores(),
                                   ["outcome", "empty", "stats", "log"])

    def test_a_section_says_how_many_it_kept_of_how_many(self):
        self.assertIn("failed commands (3 of 8, 5 ignored)", self.render())

    def test_an_untouched_section_keeps_its_plain_count(self):
        self.assertIn("empty tables (2)", self.render())

    def test_the_empty_outputs_say_it_too(self):
        self.assertIn("empty outputs (5 of 9, 4 ignored)", self.render())

    def test_the_logs_heading_keeps_its_own_of_and_adds_the_ignored(self):
        # Eleven logs read: two passed over, one of the nine left was quiet, eight carried
        # something. The 'of' counts what was read, as it did before the ignore list existed.
        self.assertIn("logs with errors (8 of 11, 2 ignored)", self.render())

    def test_a_report_with_no_ignores_reads_exactly_as_before(self):
        self.assertIn("failed commands (8)", self.render(full_host_report()))

    def test_the_one_line_carries_the_total_it_passed_over(self):
        line = report.render_one_line(report.summary_of(host_report_with_ignores()))

        self.assertIn("fails=3", line)
        self.assertIn("ignored=11", line)  # 5 failures + 4 outputs + 2 logs

    def test_the_one_line_stays_quiet_with_nothing_ignored(self):
        self.assertNotIn("ignored=",
                         report.render_one_line(report.summary_of(full_host_report())))

    def test_a_section_wholly_ignored_still_says_so(self):
        # Vanishing because you ignored all of it hides the choice you made; the heading is
        # the only record of it in the human report.
        hr = full_host_report()
        hr["empty"].update({"empty_files": [], "file_count": 0, "files_ignored": 9})

        text = report.render_human(hr, ["empty"])

        self.assertIn("empty outputs (0 of 9, 9 ignored)", text)

    def test_a_section_with_nothing_at_all_is_still_left_out(self):
        hr = full_host_report()
        hr["empty"].update({"empty_files": [], "file_count": 0, "files_ignored": 0})

        self.assertNotIn("empty outputs", report.render_human(hr, ["empty"]))

    def test_the_json_carries_it_as_a_number(self):
        self.assertEqual(report.host_json(host_report_with_ignores())["ignored"], 11)


class TestListIgnored(unittest.TestCase):
    def rows(self) -> list:
        return [command_row("H", "Autoruns", "1", False, "2026-09-25T14:00:00Z"),
                command_row("H", "USNInfo", "18", False, "2026-09-25T14:01:00Z", "USNInfo"),
                command_row("H", "NTFSInfo", "0", True, "2026-09-25T14:02:00Z", "NTFSInfo")]

    def test_an_ignored_command_is_not_listed(self):
        text = report.render_command_lines(self.rows(), False, ["Autoruns"])

        self.assertNotIn("Autoruns", text)
        self.assertIn("USNInfo", text)

    def test_the_summary_says_how_many_were_passed_over(self):
        text = report.render_command_lines(self.rows(), False, ["Autoruns"])

        self.assertIn("2 command(s), 1 failed, 1 ignored", text)

    def test_with_nothing_ignored_the_summary_is_unchanged(self):
        self.assertIn("3 command(s), 2 failed, 1 host(s)",
                      report.render_command_lines(self.rows(), False, []))


class TestRenderCriticals(unittest.TestCase):
    """The message views: what was said, as against where it was said.

    The log view counts the levels per log, which answers 'which log do I open'. These answer
    'what went wrong', which is the question that is usually being asked.
    """

    def render(self, hr=None, views=None, **kwargs) -> str:
        return report.render_human(hr or full_host_report(), views or ["critical"], **kwargs)

    def test_the_section_counts_them(self):
        self.assertIn("critical (2)", self.render())

    def test_a_critical_is_its_log_its_time_and_what_it_said(self):
        line = next(l for l in self.render().splitlines() if "access denied" in l)

        self.assertEqual(line.split()[:2], ["Command0.dev.log", "08:15:03"])
        self.assertIn("Failed to open volume", line)

    def test_the_context_is_not_printed_unless_it_is_asked_for(self):
        # Five lines either side of 61 errors is the whole report on a bad run.
        self.assertNotIn("opening volume", self.render())

    def test_the_context_is_the_lines_either_side_of_the_message(self):
        lines = self.render(context=logs.DEFAULT_CONTEXT).splitlines()
        critical = next(i for i, l in enumerate(lines) if l.strip().startswith("Command0"))

        self.assertIn("[D] opening volume", lines[critical + 2])
        self.assertIn("[E] CreateFile failed", lines[critical + 3])
        self.assertIn("[I] collection aborted", lines[critical + 5])

    def test_the_message_itself_is_marked_in_its_context(self):
        # So the eye finds the line the section is about in the middle of the block, and so the
        # message is there in full rather than cut to the width of the row above it.
        lines = self.render(context=logs.DEFAULT_CONTEXT).splitlines()
        own = next(l for l in lines if l.strip().startswith(">"))

        self.assertIn("[C] Failed to open volume", own)

    def test_a_rule_line_opens_and_closes_each_context(self):
        # Without it the contexts of several messages run into one wall of indented lines.
        lines = self.render(context=logs.DEFAULT_CONTEXT).splitlines()
        critical = next(i for i, l in enumerate(lines) if l.strip().startswith("Command0"))

        self.assertRegex(lines[critical + 1], r"^ +-{4,}$")
        self.assertRegex(lines[critical + 6], r"^ +-{4,}$")
        self.assertIn("Job was authoritatively terminated", lines[critical + 7])

    def test_a_message_with_nothing_around_it_gets_no_rule(self):
        lines = self.render(context=logs.DEFAULT_CONTEXT).splitlines()
        last = next(i for i, l in enumerate(lines) if "authoritatively" in l)

        self.assertEqual(lines[last:], [lines[last]])

    def test_a_run_whose_logs_were_never_read_does_not_read_as_clean(self):
        # Asked for by name, as '--view critical' does: in the default report the log
        # view already says it and twice reads as a bug.
        hr = full_host_report()
        hr["critical"] = {"available": False, "messages": []}

        self.assertIn("(no logs captured)", self.render(hr, explicit=True))

    def test_the_default_report_does_not_say_it_twice(self):
        hr = full_host_report()
        hr["log"]["available"] = False
        hr["critical"] = {"available": False, "messages": []}

        text = report.render_human(hr, ["log", "critical"])

        self.assertEqual(text.count("(no logs captured)"), 1)

    def test_a_clean_run_prints_no_section_at_all(self):
        hr = full_host_report()
        hr["critical"] = {"available": True, "messages": []}

        self.assertEqual(self.render(hr), report.render_human(hr, []))

    def test_naming_the_view_prints_its_heading_even_when_empty(self):
        hr = full_host_report()
        hr["critical"] = {"available": True, "messages": []}

        self.assertIn("critical (0)", self.render(hr, explicit=True))

    def test_the_error_view_prints_what_the_errors_said(self):
        text = self.render(views=["error"])

        self.assertIn("error (2)", text)
        self.assertIn("could not write the archive", text)

    def test_the_warn_view_prints_what_the_warnings_said(self):
        text = self.render(views=["warn"])

        self.assertIn("warn (1)", text)
        self.assertIn("volume C: is dirty", text)

    def test_each_section_is_headed_by_the_name_that_asks_for_it(self):
        # 'warn (1)' rather than 'warning (1)': the heading has to map back to '--view warn'.
        text = self.render(views=["warn", "error", "critical"])
        headings = [re.match(r"^ {2}(\w+) \(\d+\)$", line) for line in text.splitlines()]

        self.assertEqual([m.group(1) for m in headings if m], ["warn", "error", "critical"])

    def test_the_context_is_printed_under_an_error_too(self):
        # The case the flag missed: a log whose trigger was Error has no critical at all, and
        # its errors are the only thing there is to read.
        lines = self.render(views=["error"], context=logs.DEFAULT_CONTEXT).splitlines()
        error = next(i for i, l in enumerate(lines) if l.strip().startswith("Command0"))

        self.assertIn("[D] opening volume", lines[error + 2])
        self.assertIn("[I] giving up on C:", lines[error + 4])

    def test_all_prints_every_view(self):
        text = self.render(views=["all"], explicit=True)

        for heading in ("failed commands", "tables", "empty tables", "logs with errors",
                        "warn (1)", "error (2)", "critical (2)"):
            self.assertIn(heading, text)


class TestCriticalsInTheOtherShapes(unittest.TestCase):
    def test_the_one_line_carries_the_count_when_there_is_one(self):
        self.assertIn("criticals=2", report.render_one_line(report.summary_of(full_host_report())))

    def test_the_one_line_stays_quiet_when_there_are_none(self):
        self.assertNotIn("criticals=", report.render_one_line(report.summary_of(host_report())))

    def test_the_json_carries_the_criticals_at_the_top_level(self):
        doc = report.host_json(full_host_report())

        self.assertEqual([c["message"] for c in doc["critical_messages"]],
                         ["Failed to open volume \\\\.\\C: [access denied]",
                          "Job was authoritatively terminated"])

    def test_the_count_a_filter_reads_is_not_overwritten_by_the_messages(self):
        # Both wanted the name 'criticals' and the detail won, so '? criticals -gt 0' was
        # comparing a number against a list.
        self.assertEqual(report.host_json(full_host_report())["criticals"], 2)

    def test_the_json_does_not_also_repeat_them_under_every_log(self):
        doc = report.host_json(full_host_report())

        self.assertNotIn("messages", doc["logs"][0])

    def test_the_json_carries_the_warnings_and_errors_too(self):
        doc = report.host_json(full_host_report())

        self.assertEqual([m["message"] for m in doc["warn_messages"]], ["volume C: is dirty"])
        self.assertEqual([m["message"] for m in doc["error_messages"]],
                         ["CreateFile failed [access denied]", "could not write the archive"])


if __name__ == "__main__":
    unittest.main()
