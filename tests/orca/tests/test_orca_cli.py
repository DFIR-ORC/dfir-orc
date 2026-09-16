import unittest
from orca import cli


class TestCli(unittest.TestCase):
    def test_parser_exposes_all_subcommands(self):
        parser = cli._build_parser()
        args = parser.parse_args(["ingest", "arch.7z", "--out", "run"])
        self.assertEqual(args.command, "ingest")
        self.assertEqual(args.archive, "arch.7z")
        self.assertEqual(args.out, "run")

    def test_parser_merge_requires_run(self):
        parser = cli._build_parser()
        args = parser.parse_args(["merge", "--run", "run"])
        self.assertEqual(args.command, "merge")
        self.assertEqual(args.run, "run")

    def test_main_is_callable(self):
        self.assertTrue(callable(cli.main))
