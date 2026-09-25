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
            common, only_a, only_b = diff._match_parquet(a, b)

        self.assertEqual(sorted(common), ["NTFSInfo"])
        self.assertEqual(only_a, ["USNInfo"])
        self.assertEqual(only_b, ["NewTool"])

    def test_derived_parquet_is_not_a_collected_table(self):
        # 'merge', 'outcome', 'outline' and 'getthis' write their parquet into the run dir,
        # where the tables now live too; diffing them as tables would be nonsense.
        with tempfile.TemporaryDirectory() as a, tempfile.TemporaryDirectory() as b:
            make_run(a, ("NTFSInfo",), extra=("dfir-orc", "getthis"))
            make_run(b, ("NTFSInfo",))
            common, only_a, only_b = diff._match_parquet(a, b)

        self.assertEqual(common, ["NTFSInfo"])
        self.assertEqual(only_a, [])
        self.assertEqual(only_b, [])


if __name__ == "__main__":
    unittest.main()
