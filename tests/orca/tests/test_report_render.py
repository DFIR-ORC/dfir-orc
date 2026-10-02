import json
import unittest
from orca import report


def host_report() -> dict:
    """A report of a run that was ingested: every view has its input."""
    return {
        "host": "TESTHOST", "run_dir": "d",
        "outcome": {"available": True, "ok": False,
                    "failures": [{"set": "S", "command": "c", "exit_code": "0x1",
                                  "duration_seconds": 5.0, "start": ""}],
                    "duration_seconds": 1620.0, "memory_peak_bytes": 1503238553,
                    "memory_peak_set": "S", "commands": 35},
        "empty": {"empty_tables": ["E1"], "count": 1},
        "stats": {"tables": 25, "total_rows": 100, "total_bytes": 13207024435,
                  "biggest": [], "slowest": []},
        "errors": {"available": True, "logs": []},
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
        hr["errors"]["skipped"] = {"autoruns": "duckdb said no"}
        return hr

    def test_the_stats_view_says_the_dir_was_never_ingested(self):
        text = report.render_human(self._hr_not_ingested(), ["stats"])

        self.assertIn("no ingest timing", text)
        self.assertNotIn("0 tables", text)

    def test_the_empty_view_does_not_claim_zero_empty_tables(self):
        text = report.render_human(self._hr_not_ingested(), ["empty"])

        self.assertIn("no ingest timing", text)
        self.assertNotIn("(none)", text)

    def test_the_empty_file_half_says_when_there_is_no_manifest(self):
        hr = self._hr()
        hr["empty"] = {"available": True, "files_available": False, "empty_tables": ["E1"],
                       "count": 1, "empty_files": [], "file_count": 0}

        text = report.render_human(hr, ["empty"])

        self.assertIn("1 table(s)", text)
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

    def test_the_errors_view_names_the_unread_table_and_why(self):
        text = report.render_human(self._hr_with_skips(), ["errors"])
        self.assertIn("unread tables: 1", text)
        self.assertIn("autoruns: duckdb said no", text)

    def test_the_errors_view_says_nothing_when_the_run_is_complete(self):
        self.assertNotIn("unread", report.render_human(self._hr(), ["errors"]))

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
        self.assertEqual(obj["run_dir"], "d")

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

    def test_nothing_to_list_says_so(self):
        self.assertIn("0 command(s)", report.render_command_lines([], False))


if __name__ == "__main__":
    unittest.main()
