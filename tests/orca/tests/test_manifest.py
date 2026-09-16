import os
import tempfile
import unittest
from orca import diff, engine

DUCKDB = engine.find_duckdb()


class TestManifestSql(unittest.TestCase):
    def test_added_sql_is_anti_join_on_relpath(self):
        sql = diff.build_manifest_added_sql("A", "B")
        self.assertIn("LEFT JOIN B", sql)
        self.assertIn("USING (relpath)", sql)
        self.assertIn("b.relpath IS NULL", sql)

    def test_changed_sql_splits_csv_vs_binary(self):
        csv_sql = diff.build_manifest_changed_sql("A", "B", is_csv=True)
        bin_sql = diff.build_manifest_changed_sql("A", "B", is_csv=False)
        self.assertIn("a.crc <> b.crc", csv_sql)
        self.assertIn("lower(a.isCsv) = 'true'", csv_sql)
        self.assertIn("lower(a.isCsv) <> 'true'", bin_sql)

    def test_render_manifest_line(self):
        line = diff.render_manifest_line({"added": 12, "removed": 3,
                                          "changed_binary": 4, "changed_csv": 7})
        self.assertIn("files: +12 / -3", line)
        self.assertIn("changed-binary: 4", line)
        self.assertIn("changed-csv: 7", line)

    def test_render_run_human_includes_manifest_line(self):
        result = {"tables": {}, "added_tables": [], "removed_tables": []}
        out = diff.render_run_human("A", "B", result,
                                    {"added": 1, "removed": 0, "changed_binary": 2, "changed_csv": 0})
        self.assertIn("files: +1 / -0", out)


def _write_manifest_parquet(eng, path, rows):
    csv_path = path + ".csv"
    with open(csv_path, "w", newline="", encoding="utf-8") as f:
        f.write("relpath,size,crc,isCsv\n")
        for relpath, crc, is_csv in rows:
            f.write(f"{relpath},1,{crc},{'true' if is_csv else 'false'}\n")
    eng.csv_to_parquet(csv_path, path)
    os.remove(csv_path)


@unittest.skipUnless(DUCKDB, "duckdb not available")
class TestDiffManifest(unittest.TestCase):
    def test_counts_added_removed_changed(self):
        eng = engine.DuckDbEngine(DUCKDB)
        with tempfile.TemporaryDirectory() as d:
            new = os.path.join(d, "new.parquet")
            ref = os.path.join(d, "ref.parquet")
            _write_manifest_parquet(eng, new, [
                ("a.csv", "CRC_A_NEW", True),   # crc differs from ref -> changed_csv
                ("b.bin", "CRC_B", False),      # same -> unchanged
                ("c.bin", "CRC_C", False),      # new only -> added
            ])
            _write_manifest_parquet(eng, ref, [
                ("a.csv", "CRC_A_OLD", True),
                ("b.bin", "CRC_B", False),
                ("d.bin", "CRC_D", False),      # ref only -> removed
            ])
            got = diff.diff_manifest(eng, new, ref)
            self.assertEqual(got, {"added": 1, "removed": 1,
                                   "changed_binary": 0, "changed_csv": 1})


if __name__ == "__main__":
    unittest.main()
