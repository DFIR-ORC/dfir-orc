import os
import tempfile
import unittest
from orca import diff, layout


def make_run(directory, names, extra=()):
    """A run directory: its tables flat, and whatever else shares the directory."""
    os.makedirs(layout.orca_dir(directory), exist_ok=True)
    with open(layout.name_map_path(directory), "w", encoding="utf-8") as f:
        f.write("name,source_archive,entry_path,source_mtime\n")
        for name in names:
            f.write(f"{name},arc.7z,{name}.csv,\n")
    for name in list(names) + list(extra):
        open(os.path.join(directory, name + ".parquet"), "w").close()
    return directory


class TestMatch(unittest.TestCase):
    def test_match_parquet_pairs_and_singletons(self):
        with tempfile.TemporaryDirectory() as a, tempfile.TemporaryDirectory() as b:
            make_run(a, ("NTFSInfo", "USNInfo"))
            make_run(b, ("NTFSInfo", "NewTool"))
            common, only_a, only_b, unread = diff._match_parquet(a, b)

        self.assertEqual(sorted(common), ["NTFSInfo"])
        self.assertEqual(only_a, ["USNInfo"])
        self.assertEqual(only_b, ["NewTool"])

    def test_derived_parquet_is_not_a_collected_table(self):
        # 'merge', 'outcome', 'outline' and 'getthis' write their parquet into the run dir,
        # where the tables now live too; diffing them as tables would be nonsense.
        with tempfile.TemporaryDirectory() as a, tempfile.TemporaryDirectory() as b:
            make_run(a, ("NTFSInfo",), extra=("dfir-orc", "getthis"))
            make_run(b, ("NTFSInfo",))
            common, only_a, only_b, unread = diff._match_parquet(a, b)

        self.assertEqual(common, ["NTFSInfo"])
        self.assertEqual(only_a, [])
        self.assertEqual(only_b, [])


class TestUnreadTables(unittest.TestCase):
    """A table '--skip-errors' could not produce is missing from its run. One-sided would call
    that removed, which says the collection changed when what changed is that orca could not
    read the file."""

    def skip(self, run_dir, name):
        with open(layout.skipped_path(run_dir), "w", encoding="utf-8") as f:
            f.write(f"{name}\tduckdb said no\n")

    def test_a_table_skipped_in_a_is_unread_not_removed(self):
        with tempfile.TemporaryDirectory() as a, tempfile.TemporaryDirectory() as b:
            make_run(a, ("NTFSInfo",))
            make_run(b, ("NTFSInfo", "autoruns"))
            self.skip(a, "autoruns")

            common, only_a, only_b, unread = diff._match_parquet(a, b)

        self.assertEqual(common, ["NTFSInfo"])
        self.assertEqual(only_b, [])
        self.assertEqual(unread, ["autoruns"])

    def test_a_table_skipped_in_b_is_unread_not_added(self):
        with tempfile.TemporaryDirectory() as a, tempfile.TemporaryDirectory() as b:
            make_run(a, ("NTFSInfo", "autoruns"))
            make_run(b, ("NTFSInfo",))
            self.skip(b, "autoruns")

            _, only_a, _, unread = diff._match_parquet(a, b)

        self.assertEqual(only_a, [])
        self.assertEqual(unread, ["autoruns"])

    def test_a_table_skipped_in_both_is_still_named(self):
        # It is in neither run, so nothing else would ever mention that it was not compared.
        with tempfile.TemporaryDirectory() as a, tempfile.TemporaryDirectory() as b:
            make_run(a, ("NTFSInfo",))
            make_run(b, ("NTFSInfo",))
            self.skip(a, "autoruns")
            self.skip(b, "autoruns")

            _, only_a, only_b, unread = diff._match_parquet(a, b)

        self.assertEqual((only_a, only_b), ([], []))
        self.assertEqual(unread, ["autoruns"])

    def test_a_genuinely_removed_table_is_still_removed(self):
        with tempfile.TemporaryDirectory() as a, tempfile.TemporaryDirectory() as b:
            make_run(a, ("NTFSInfo",))
            make_run(b, ("NTFSInfo", "USNInfo"))

            _, _, only_b, unread = diff._match_parquet(a, b)

        self.assertEqual(only_b, ["USNInfo"])
        self.assertEqual(unread, [])


class TestSkippedReader(unittest.TestCase):
    def test_it_reads_the_name_and_the_reason(self):
        with tempfile.TemporaryDirectory() as d:
            os.makedirs(layout.orca_dir(d))
            with open(layout.skipped_path(d), "w", encoding="utf-8") as f:
                f.write("autoruns\tduckdb said no\nbits\t\n")

            self.assertEqual(layout.skipped(d),
                             {"autoruns": "duckdb said no", "bits": ""})

    def test_a_full_run_has_no_file_and_no_skips(self):
        with tempfile.TemporaryDirectory() as d:
            self.assertEqual(layout.skipped(d), {})


if __name__ == "__main__":
    unittest.main()
