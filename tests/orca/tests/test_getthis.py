import os
import tempfile
import unittest
from orca import getthis, layout


class FakeCountEngine:
    def __init__(self, counts):
        self._counts = counts

    def count(self, path):
        return self._counts[os.path.splitext(os.path.basename(path))[0]]


def _make_run(dir_, tables, mapping):
    os.makedirs(layout.orca_dir(dir_), exist_ok=True)
    for name in tables:
        open(os.path.join(dir_, name + ".parquet"), "w").close()
    with open(layout.name_map_path(dir_), "w", newline="") as f:
        f.write("name,source_archive,entry_path,source_mtime\n")
        for name in tables:
            f.write(f"{name},{mapping.get(name, '')},GetThis.csv,\n")


class TestGetThis(unittest.TestCase):
    def test_glob_reader_unions_every_getthis_table(self):
        with tempfile.TemporaryDirectory() as d:
            _make_run(d, ["GetThis", "GetThis_ADS", "NTFSInfo"], {})
            reader = getthis.glob_reader(d)

        self.assertIn("read_parquet(", reader)
        self.assertIn("union_by_name=true", reader)
        self.assertIn("GetThis.parquet", reader)
        self.assertIn("GetThis_ADS.parquet", reader)
        self.assertNotIn("NTFSInfo", reader)

    def test_the_commands_own_output_is_not_a_getthis_table(self):
        # 'getthis.parquet' is what this command writes; on Windows a 'GetThis*' glob over the
        # run directory would match it and the summary would count its own last result.
        with tempfile.TemporaryDirectory() as d:
            _make_run(d, ["GetThis"], {})
            open(os.path.join(d, "getthis.parquet"), "w").close()
            found = [os.path.basename(p) for p in getthis.discover_getthis(d)]

        self.assertEqual(found, ["GetThis.parquet"])

    def test_read_source_archives_uses_basename(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "name-map.csv")
            with open(path, "w", newline="") as f:
                f.write("name,source_archive,entry_path,source_mtime\n")
                f.write("GetThis_ADS,/tmp/orca_stage_x/ADS.7z,GetThis.csv,\n")
            self.assertEqual(getthis.read_source_archives(path), {"GetThis_ADS": "ADS.7z"})

    def test_counts_by_archive_maps_and_sorts_desc(self):
        with tempfile.TemporaryDirectory() as d:
            _make_run(d, ["GetThis_ADS", "GetThis_Browsers"],
                      {"GetThis_ADS": "/tmp/x/ADS.7z",
                       "GetThis_Browsers": "/tmp/x/Browsers_complet.7z"})
            engine = FakeCountEngine({"GetThis_ADS": 1234, "GetThis_Browsers": 412873})
            self.assertEqual(getthis.counts_by_archive(engine, d),
                             [("Browsers_complet.7z", 412873), ("ADS.7z", 1234)])

    def test_counts_fall_back_to_table_name_when_unmapped(self):
        with tempfile.TemporaryDirectory() as d:
            _make_run(d, ["GetThis_ADS"], {})  # empty name-map rows
            engine = FakeCountEngine({"GetThis_ADS": 5})
            self.assertEqual(getthis.counts_by_archive(engine, d), [("GetThis_ADS", 5)])

    def test_render_summary_per_archive_no_hash(self):
        out = getthis.render_summary(611534, [("Browsers_complet.7z", 412873), ("ADS.7z", 1234)])
        self.assertIn("611534 files collected", out)
        self.assertIn("Browsers_complet.7z", out)
        self.assertIn("412873", out)
        self.assertNotIn("SHA1", out)
        self.assertNotIn("distinct", out)


if __name__ == "__main__":
    unittest.main()
