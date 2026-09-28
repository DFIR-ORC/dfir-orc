import unittest
from orca import cli


class TestCli(unittest.TestCase):
    def test_parser_exposes_all_subcommands(self):
        parser = cli._build_parser()
        args = parser.parse_args(["ingest", "arch.7z", "--out", "run"])
        self.assertEqual(args.command, "ingest")
        self.assertEqual(args.archive, "arch.7z")
        self.assertEqual(args.out, "run")

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
        args = parser.parse_args(["merge", "--run", "run"])
        self.assertEqual(args.command, "merge")
        self.assertEqual(args.run, "run")

    def test_main_is_callable(self):
        self.assertTrue(callable(cli.main))
