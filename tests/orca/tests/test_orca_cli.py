import contextlib
import io
import unittest
from orca import cli, logs, report


class TestCli(unittest.TestCase):
    def test_parser_exposes_all_subcommands(self):
        parser = cli._build_parser()
        args = parser.parse_args(["ingest", "arch.7z", "--out", "run"])
        self.assertEqual(args.command, "ingest")
        self.assertEqual(args.source, "arch.7z")
        self.assertEqual(args.out, "run")
        # The option, not the positional: a directory of several runs needs a way to pick one.
        self.assertIsNone(args.collection)

    def test_check_takes_the_archive_options_extract_takes(self):
        args = cli._build_parser().parse_args(
            ["check", "ORC.7z.p7b", "--private-key", "k.pem", "--depth", "2",
             "--work-dir", "E:/work"])

        self.assertEqual(args.command, "check")
        self.assertEqual(args.collection, "ORC.7z.p7b")
        self.assertEqual(args.private_key, "k.pem")
        self.assertEqual(args.depth, 2)
        self.assertEqual(args.work_dir, "E:/work")

    def test_check_reads_the_outcome_unless_told_not_to(self):
        parser = cli._build_parser()

        self.assertFalse(parser.parse_args(["check", "a.7z"]).no_outcome)
        self.assertTrue(parser.parse_args(["check", "a.7z", "--no-outcome"]).no_outcome)

    def test_check_has_the_two_summary_shapes_the_other_commands_have(self):
        args = cli._build_parser().parse_args(["check", "a.7z", "--json", "--one-line"])

        self.assertTrue(args.json)
        self.assertTrue(args.one_line)

    def test_outcome_and_outline_are_no_longer_top_level_commands(self):
        # They are modes of 'report' now: everything you ask about a run's metadata is there.
        for argv in (["outcome", "E:/runs/host"], ["outline", "E:/runs/host"]):
            with self.assertRaises(SystemExit):
                cli._build_parser().parse_args(argv)

    def test_report_recognises_its_two_table_modes(self):
        self.assertEqual(cli.report_table_mode(["report", "outcome", "--root", "E:/r"]),
                         "outcome")
        self.assertEqual(cli.report_table_mode(["report", "outline"]), "outline")

    def test_a_run_dir_is_not_mistaken_for_a_mode(self):
        # Why these cannot be argparse subparsers: 'report' already takes the run dirs as an
        # optional positional, and argparse would read the path as a mode name.
        self.assertIsNone(cli.report_table_mode(["report", "E:/runs/host"]))
        self.assertIsNone(cli.report_table_mode(["report"]))
        self.assertIsNone(cli.report_table_mode(["check", "outcome"]))
        self.assertIsNone(cli.report_table_mode([]))

    def test_the_mode_parser_takes_the_table_options(self):
        args = cli._build_mode_parser("outcome").parse_args(
            ["E:/runs/host", "--out", "x.parquet", "--json"])

        self.assertEqual(args.paths, ["E:/runs/host"])
        self.assertEqual(args.out, "x.parquet")
        self.assertTrue(args.json)

    def test_the_mode_parser_names_itself_as_the_real_command(self):
        # So that 'orca report outline --help' does not claim to be something else.
        self.assertEqual(cli._build_mode_parser("outline").prog, "orca report outline")

    def test_report_takes_list_and_failed(self):
        args = cli._build_parser().parse_args(["report", "E:/runs", "--list",
                                               "--failed"])

        self.assertTrue(args.list)
        self.assertTrue(args.failed)

    def test_report_takes_the_critical_view_and_its_backtrace_flag(self):
        args = cli._build_parser().parse_args(["report", "E:/runs", "--view", "critical",
                                               "--backtrace"])

        self.assertEqual(args.view, ["critical"])
        self.assertEqual(args.backtrace, logs.DEFAULT_CONTEXT)

    def test_the_backtrace_flag_takes_an_optional_line_count(self):
        args = cli._build_parser().parse_args(["report", "E:/runs", "--backtrace", "3"])

        self.assertEqual(args.backtrace, 3)

    def test_a_backtrace_count_that_is_not_a_positive_number_is_refused(self):
        for count in ("0", "-5", "x"):
            with self.assertRaises(SystemExit):
                cli._build_parser().parse_args(["report", "E:/runs", "--backtrace", count])

    def test_a_comma_list_of_views_is_the_same_as_repeating_the_flag(self):
        parser = cli._build_parser()
        listed = parser.parse_args(["report", "E:/runs", "--view", "critical,error,warn"])
        repeated = parser.parse_args(["report", "E:/runs", "--view", "critical",
                                      "--view", "error", "--view", "warn"])

        self.assertEqual(listed.view, ["critical", "error", "warn"])
        self.assertEqual(listed.view, repeated.view)

    def test_no_view_named_is_not_an_empty_list_of_views(self):
        # None is 'print the default report'; [] would be 'print none of them'.
        self.assertIsNone(cli._build_parser().parse_args(["report", "E:/runs"]).view)

    def test_every_view_the_report_knows_is_accepted_by_name(self):
        parser = cli._build_parser()
        for view in report.VIEW_NAMES:
            self.assertEqual(parser.parse_args(["report", "E:/runs", "--view", view]).view,
                             [view])

    def test_a_plural_or_unknown_view_is_refused(self):
        # The names were plural once; a silent 'nothing matched that' would read as a clean run.
        for name in ("criticals", "errors", "logs", "nope"):
            with self.assertRaises(SystemExit):
                cli._build_parser().parse_args(["report", "E:/runs", "--view", name])

    def test_the_backtrace_is_off_unless_asked_for(self):
        self.assertIsNone(cli._build_parser().parse_args(["report", "E:/runs"]).backtrace)

    def test_the_logs_inside_the_archives_are_read_unless_declined(self):
        parser = cli._build_parser()

        self.assertTrue(parser.parse_args(["report", "E:/runs"]).archive_logs)
        self.assertFalse(parser.parse_args(["report", "E:/runs",
                                            "--no-archive-logs"]).archive_logs)

    def test_the_report_output_shapes_are_mutually_exclusive(self):
        for argv in (["report", "x", "--list", "--json"],
                     ["report", "x", "--one-line", "--list"],
                     ["report", "x", "--json", "--one-line"]):
            with self.assertRaises(SystemExit):
                cli._build_parser().parse_args(argv)

    def test_the_dataset_is_the_argument_of_merge_and_getthis(self):
        # A command that reads exactly one dataset should not make you name the obvious.
        parser = cli._build_parser()

        for command in ("merge", "getthis"):
            args = parser.parse_args([command, "E:/datasets/host"])
            self.assertEqual(args.dataset, "E:/datasets/host", command)

            with self.assertRaises(SystemExit):
                parser.parse_args([command])

    def test_stopping_at_the_first_error_is_the_thing_you_ask_for(self):
        # Carrying on is what you want nearly every time, so it is the default and --strict is
        # the question you have to ask.
        parser = cli._build_parser()

        for argv in (["ingest", "a.7z"], ["extract", "a.7z"], ["diff", "a.7z", "b.7z"],
                     ["merge", "E:/datasets/host"], ["getthis", "E:/datasets/host"]):
            self.assertFalse(parser.parse_args(argv).strict, argv)
            self.assertTrue(parser.parse_args(argv + ["--strict"]).strict, argv)

        # The flags that used to say it are gone, not kept as aliases.
        for argv in (["ingest", "a.7z", "--skip-errors"],
                     ["ingest", "a.7z", "--ignore-errors"]):
            with self.assertRaises(SystemExit):
                parser.parse_args(argv)

    def test_merge_and_getthis_take_what_an_ingest_needs(self):
        # They ingest on demand, so they need the keys an ingest needs.
        parser = cli._build_parser()

        for command in ("merge", "getthis"):
            args = parser.parse_args([command, "E:/dumps/HOST.7z.p7b", "--private-key",
                                      "k.pem", "--work-dir", "E:/work", "--refresh",
                                      "--strict", "--password", "s3cret",
                                      "--cms-backend", "cng"])
            self.assertEqual(args.private_key, "k.pem", command)
            self.assertEqual(args.work_dir, "E:/work", command)
            self.assertEqual(args.password, "s3cret", command)
            self.assertEqual(args.cms_backend, "cng", command)
            self.assertTrue(args.refresh, command)
            self.assertTrue(args.strict, command)

    def test_report_recurses_only_when_asked(self):
        parser = cli._build_parser()

        self.assertFalse(parser.parse_args(["report", "E:/dumps"]).recurse)
        self.assertTrue(parser.parse_args(["report", "E:/dumps", "--recurse"]).recurse)
        for mode in cli.REPORT_TABLE_MODES:
            self.assertTrue(cli._build_mode_parser(mode)
                            .parse_args(["E:/dumps", "--recurse"]).recurse)

    def test_report_has_no_root_option_any_more(self):
        # A directory means every collection in it, so there is nothing left for --root to do.
        with self.assertRaises(SystemExit):
            cli._build_parser().parse_args(["report", "--root", "E:/runs"])

        for mode in cli.REPORT_TABLE_MODES:
            with self.assertRaises(SystemExit):
                cli._build_mode_parser(mode).parse_args(["--root", "E:/runs"])

    def test_report_takes_an_ignore_file(self):
        args = cli._build_parser().parse_args(["report", "E:/runs", "--ignore",
                                               "ignore.json"])

        self.assertEqual(args.ignore, "ignore.json")

    def test_report_without_a_path_reports_the_current_directory(self):
        # In a fleet's output directory a bare 'orca report' reports the fleet.
        self.assertEqual(cli._build_parser().parse_args(["report"]).paths, ["."])

    def test_report_takes_the_keys_it_needs_to_unseal_a_collection(self):
        # A collection arrives with its Outcome, Outline and log each sealed.
        args = cli._build_parser().parse_args(
            ["report", "E:/dumps/ORC_HOST", "--private-key", "k.pem",
             "--cms-backend", "cng", "--work-dir", "E:/work", "--refresh"])

        self.assertEqual(args.private_key, "k.pem")
        self.assertEqual(args.cms_backend, "cng")
        self.assertEqual(args.work_dir, "E:/work")
        self.assertTrue(args.refresh)

    def test_both_table_modes_take_them_too(self):
        for mode in cli.REPORT_TABLE_MODES:
            args = cli._build_mode_parser(mode).parse_args(
                ["E:/dumps/ORC_HOST", "--private-key", "k.pem", "--work-dir", "E:/work",
                 "--refresh", "--cms-backend", "libcrypto"])  # no --root any more

            self.assertEqual(args.private_key, "k.pem", mode)
            self.assertEqual(args.work_dir, "E:/work", mode)
            self.assertEqual(args.cms_backend, "libcrypto", mode)
            self.assertTrue(args.refresh, mode)

    def test_ingest_without_out_leaves_the_place_to_the_layout(self):
        args = cli._build_parser().parse_args(["ingest", "arch.7z"])
        self.assertIsNone(args.out)
        self.assertIsNone(args.work_dir)
        self.assertFalse(args.out_at_input)

    def test_every_command_that_ingests_takes_out_at_input(self):
        parser = cli._build_parser()
        for argv in (["ingest", "arch.7z"], ["fleet", "dumps"], ["diff", "NEW.7z", "REF.7z"],
                     ["merge", "arch.7z"], ["getthis", "arch.7z"]):
            self.assertTrue(parser.parse_args(argv + ["--out-at-input"]).out_at_input, argv)

    def test_out_at_input_with_another_place_is_refused(self):
        for argv in (["ingest", "arch.7z", "--out-at-input", "--out", "run"],
                     ["ingest", "arch.7z", "--out-at-input", "--work-dir", "E:/work"],
                     ["fleet", "dumps", "--out-at-input", "--out", "E:/runs"],
                     ["diff", "NEW.7z", "REF.7z", "--out-at-input", "--work-dir", "E:/work"]):
            with self.subTest(argv=argv), self.assertRaises(SystemExit), \
                    contextlib.redirect_stderr(io.StringIO()):
                cli.main(argv)

    def test_merge_out_names_its_parquet_and_does_not_conflict(self):
        # merge's --out is the result file, not where the dataset goes.
        parser = cli._build_parser()
        args = parser.parse_args(["merge", "arch.7z", "--out-at-input", "--out", "x.parquet"])
        cli.refuse_conflicting_outputs(parser, args)
        self.assertTrue(args.out_at_input)

    def test_work_dir_is_the_one_override_for_runs_and_decrypted_output(self):
        parser = cli._build_parser()
        for argv in (["ingest", "arch.7z", "--work-dir", "E:/work"],
                     ["diff", "NEW.7z", "REF.7z", "--work-dir", "E:/work"]):
            self.assertEqual(parser.parse_args(argv).work_dir, "E:/work")

    def test_extract_work_dir_is_optional(self):
        # Same flag name, a narrower meaning than on ingest/diff: on extract it only says
        # where the decrypted archive goes while the tree is being written.
        parser = cli._build_parser()
        plain = parser.parse_args(["extract", "arch.p7b", "--out", "tree"])
        self.assertIsNone(plain.work_dir)

        given = parser.parse_args(["extract", "arch.p7b", "--out", "tree", "--work-dir", "E:/w"])
        self.assertEqual(given.work_dir, "E:/w")

    def test_extract_without_out_writes_to_the_current_directory(self):
        # What every other extractor does. 'extract' names each archive's tree after the
        # archive itself, so the cwd gets a directory per archive and the loose sidecars,
        # never a sprayed tree.
        args = cli._build_parser().parse_args(["extract", "arch.p7b"])
        self.assertEqual(args.out, ".")

    def test_strict_is_on_both_commands_that_ingest(self):
        parser = cli._build_parser()
        for argv in (["ingest", "arch.7z"], ["diff", "NEW.7z", "REF.7z"]):
            self.assertFalse(parser.parse_args(argv).strict, argv)
            self.assertTrue(parser.parse_args(argv + ["--strict"]).strict, argv)

    def test_extract_refresh_is_off_unless_asked(self):
        parser = cli._build_parser()
        self.assertFalse(parser.parse_args(["extract", "a.p7b", "--out", "t"]).refresh)
        self.assertTrue(parser.parse_args(["extract", "a.p7b", "--out", "t",
                                           "--refresh"]).refresh)

    def test_extract_depth_is_unlimited_unless_asked(self):
        parser = cli._build_parser()
        plain = parser.parse_args(["extract", "a.p7b", "--out", "tree"])
        self.assertIsNone(plain.depth)

        given = parser.parse_args(["extract", "a.p7b", "--out", "tree", "--depth", "2"])
        self.assertEqual(given.depth, 2)

    def test_diff_out_dir_still_means_the_rows_csv(self):
        args = cli._build_parser().parse_args(["diff", "NEW.7z", "REF.7z", "--rows"])
        self.assertEqual(args.out_dir, "orca-diff")

    def test_parser_merge_requires_run(self):
        parser = cli._build_parser()
        args = parser.parse_args(["merge", "E:/datasets/host"])
        self.assertEqual(args.command, "merge")
        self.assertEqual(args.dataset, "E:/datasets/host")

    def test_main_is_callable(self):
        self.assertTrue(callable(cli.main))


class TestCollectParsing(unittest.TestCase):
    def test_collect_requires_a_target(self):
        with self.assertRaises(SystemExit):
            cli._build_parser().parse_args(["collect", "--orc", "orc.exe", "--out", "r"])

    def test_offline_and_vm_are_exclusive(self):
        with self.assertRaises(SystemExit):
            cli._build_parser().parse_args(["collect", "--orc", "orc.exe", "--out", "r",
                                            "--offline", "d.dd", "--vm", "w10"])

    def test_upload_operation_defaults_to_unset_and_only_takes_known_values(self):
        # Unset, not "copy": collect refuses --upload-operation without --upload, which it
        # could not tell from the default if the default were a value.
        base = ["collect", "--orc", "orc.exe", "--out", "r", "--offline", "d.dd"]
        self.assertIsNone(cli._build_parser().parse_args(base).upload_operation)
        parsed = cli._build_parser().parse_args(base + ["--upload-operation", "move"])
        self.assertEqual(parsed.upload_operation, "move")
        with self.assertRaises(SystemExit):
            cli._build_parser().parse_args(base + ["--upload-operation", "delete"])

    def test_double_dash_arguments_reach_orc_args(self):
        args = cli._build_parser().parse_args(
            ["collect", "--orc", "orc.exe", "--out", "r", "--offline", "d.dd",
             "--", "/key=GetEvt", "/utf8"])
        self.assertEqual(cli.orc_passthrough(args.orc_args), ["/key=GetEvt", "/utf8"])


class TestHelp(unittest.TestCase):
    """What 'orca --help' and 'orca <command> --help' owe a reader.

    The help is the documentation most people read, so every command carries a paragraph of
    its own rather than only the one line the listing shows.
    """

    def commands(self) -> dict:
        action = next(a for a in cli._build_parser()._actions if a.dest == "command")
        return action.choices

    def test_collect_comes_first(self):
        # The pipeline starts there: it is the command that produces what the rest read.
        self.assertEqual(list(self.commands())[0], "collect")

    def test_every_command_has_a_one_line_summary_for_the_listing(self):
        action = next(a for a in cli._build_parser()._actions if a.dest == "command")

        for pseudo in action._choices_actions:
            self.assertTrue((pseudo.help or "").strip(), pseudo.dest)

    def test_every_command_has_a_paragraph_of_its_own(self):
        for name, parser in self.commands().items():
            description = (parser.description or "").strip()
            self.assertTrue(description, f"{name} has no description")
            self.assertGreater(len(description), 120,
                               f"{name}'s description is a line, not a paragraph")

    def test_the_paragraph_is_what_the_command_help_prints(self):
        self.assertIn("without expanding", self.commands()["check"].format_help())

    def test_orca_itself_says_what_it_is_and_shows_the_way_through(self):
        parser = cli._build_parser()

        self.assertGreater(len((parser.description or "").strip()), 120)
        self.assertIn("orca collect", parser.epilog or "")
