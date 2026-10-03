import unittest
from orca import cli


class TestCli(unittest.TestCase):
    def test_parser_exposes_all_subcommands(self):
        parser = cli._build_parser()
        args = parser.parse_args(["ingest", "arch.7z", "--out", "run"])
        self.assertEqual(args.command, "ingest")
        self.assertEqual(args.archive, "arch.7z")
        self.assertEqual(args.out, "run")

    def test_check_takes_the_archive_options_extract_takes(self):
        args = cli._build_parser().parse_args(
            ["check", "ORC.7z.p7b", "--private-key", "k.pem", "--depth", "2",
             "--work-dir", "E:/work"])

        self.assertEqual(args.command, "check")
        self.assertEqual(args.archive, "ORC.7z.p7b")
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

    def test_merge_and_getthis_take_what_an_ingest_needs(self):
        # They ingest on demand, so they need the keys an ingest needs.
        parser = cli._build_parser()

        for command in ("merge", "getthis"):
            args = parser.parse_args([command, "E:/dumps/HOST.7z.p7b", "--private-key",
                                      "k.pem", "--work-dir", "E:/work", "--refresh",
                                      "--skip-errors", "--password", "s3cret",
                                      "--cms-backend", "cng"])
            self.assertEqual(args.private_key, "k.pem", command)
            self.assertEqual(args.work_dir, "E:/work", command)
            self.assertEqual(args.password, "s3cret", command)
            self.assertEqual(args.cms_backend, "cng", command)
            self.assertTrue(args.refresh, command)
            self.assertTrue(args.skip_errors, command)

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

    def test_report_needs_something_to_report_on(self):
        with self.assertRaises(SystemExit):
            cli._build_parser().parse_args(["report"])

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

    def test_ingest_without_out_defaults_beside_the_archive(self):
        args = cli._build_parser().parse_args(["ingest", "arch.7z"])
        self.assertIsNone(args.out)
        self.assertIsNone(args.work_dir)

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

    def test_skip_errors_is_on_both_commands_that_ingest(self):
        parser = cli._build_parser()
        for argv in (["ingest", "arch.7z"], ["diff", "NEW.7z", "REF.7z"]):
            self.assertFalse(parser.parse_args(argv).skip_errors, argv)
            self.assertTrue(parser.parse_args(argv + ["--skip-errors"]).skip_errors, argv)

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
