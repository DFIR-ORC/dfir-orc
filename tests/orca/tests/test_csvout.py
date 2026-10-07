"""CSV on demand over a dataset (docs/superpowers/specs/2026-10-07-orca-csv-on-demand-design.md)."""
import io
import os
import tempfile
import threading
import unittest
from contextlib import redirect_stdout

from orca import csvout, layout


class FakeEngine:
    """Writes the CSV as '<parquet>|<dropped columns>' and records each conversion."""

    def __init__(self):
        self.calls = []
        self.lock = threading.Lock()

    def parquet_to_csv(self, parquet, target, drop=None):
        with self.lock:
            self.calls.append((os.path.basename(target), drop))
        with open(target, "w", encoding="utf-8") as f:
            f.write(f"{parquet}|{drop}")


def make_dataset(directory: str, tables: list[str]) -> str:
    os.makedirs(layout.orca_dir(directory), exist_ok=True)
    with open(layout.name_map_path(directory), "w", newline="", encoding="utf-8") as f:
        f.write("name,source_archive,entry_path,source_mtime\n")
        for name in tables:
            f.write(f"{name},a.7z,{name}.csv,0\n")
            with open(os.path.join(directory, name + ".parquet"), "wb") as table:
                table.write(b"PAR1")
    layout.write_products(directory, [layout.ORCA_DIR] + [n + ".parquet" for n in tables])
    return directory


class CsvCase(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)
        self.dataset = make_dataset(os.path.join(self.dir.name, "run"), ["autoruns", "NTFSInfo"])
        self.engine = FakeEngine()

    def write(self, **kwargs) -> int:
        with redirect_stdout(io.StringIO()):
            return csvout.write_csv(self.engine, self.dataset, **kwargs)


class TestWriteCsv(CsvCase):
    def test_nothing_asked_writes_nothing(self):
        self.assertEqual(self.write(), 0)
        self.assertFalse(os.path.exists(os.path.join(self.dataset, csvout.CSV_DIR)))

    def test_every_table_gets_its_csv(self):
        self.assertEqual(self.write(csv=True), 2)
        self.assertEqual(sorted(os.listdir(os.path.join(self.dataset, csvout.CSV_DIR))),
                         ["NTFSInfo.csv", "autoruns.csv"])

    def test_asking_twice_writes_nothing_the_second_time(self):
        self.write(csv=True)
        self.assertEqual(self.write(csv=True), 0)

    def test_a_table_newer_than_its_csv_is_written_again(self):
        self.write(csv=True)
        csv = os.path.join(self.dataset, csvout.CSV_DIR, "autoruns.csv")
        os.utime(csv, (1, 1))
        self.assertEqual(self.write(csv=True), 1)

    def test_trunc_drops_the_columns_it_is_given(self):
        self.write(trunc=True, drop=["MD5"])
        self.assertEqual(sorted(self.engine.calls),
                         [("NTFSInfo.csv", ["MD5"]), ("autoruns.csv", ["MD5"])])
        self.assertFalse(os.path.exists(os.path.join(self.dataset, csvout.CSV_DIR)))

    def test_other_dropped_columns_rewrite_every_trunc_csv(self):
        self.write(trunc=True, drop=["MD5"])
        self.assertEqual(self.write(trunc=True, drop=["MD5"]), 0)
        self.assertEqual(self.write(trunc=True, drop=["SHA1"]), 2)

    def test_what_it_writes_is_a_product_a_rebuild_removes(self):
        self.write(csv=True, trunc=True, drop=[])
        products = layout.products(self.dataset)
        self.assertIn(os.path.join(csvout.CSV_DIR, "autoruns.csv"), products)
        self.assertIn(csvout.CSV_TRUNC_DIR, products)
        self.assertIn("autoruns.parquet", products)

    def test_a_parquet_the_ingest_did_not_name_is_not_converted(self):
        """A tree 'orca extract' wrote inside the dataset is not one of its tables."""
        with open(os.path.join(self.dataset, "stray.parquet"), "wb") as f:
            f.write(b"PAR1")
        self.write(csv=True)
        self.assertNotIn("stray.csv", os.listdir(os.path.join(self.dataset, csvout.CSV_DIR)))


class TestEveryTableHasItsParquet(CsvCase):
    """The CSVs are written over the parquet: a table without one is not quietly left out."""

    def test_a_table_whose_parquet_is_gone_is_refused_by_name(self):
        os.remove(os.path.join(self.dataset, "NTFSInfo.parquet"))
        with self.assertRaises(RuntimeError) as caught:
            self.write(csv=True)
        self.assertIn("NTFSInfo", str(caught.exception))
        self.assertEqual(self.engine.calls, [])

    def test_a_table_the_ingest_skipped_is_named_not_refused(self):
        os.remove(os.path.join(self.dataset, "NTFSInfo.parquet"))
        with open(layout.skipped_path(self.dataset), "w", encoding="utf-8") as f:
            f.write("NTFSInfo\t7z extract failed\n")
        out = io.StringIO()
        with redirect_stdout(out):
            self.assertEqual(csvout.write_csv(self.engine, self.dataset, csv=True), 1)
        self.assertIn("skipped: NTFSInfo", out.getvalue())


class TestCsvInTheArchiveDirectory(unittest.TestCase):
    """A table's CSV beside the logs of the archive it came from; csv-trunc at the top."""

    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)
        self.dataset = os.path.join(self.dir.name, "run")
        os.makedirs(layout.orca_dir(self.dataset))
        with open(layout.name_map_path(self.dataset), "w", newline="", encoding="utf-8") as f:
            f.write("name,source_archive,entry_path,source_mtime,archive\n")
            f.write("autoruns,x,autoruns.csv,0,ORC_H_General\n")
            f.write("FastFind,x,FastFind.csv,0,ORC_H_FastFind\n")
        for name in ("autoruns", "FastFind"):
            with open(os.path.join(self.dataset, name + ".parquet"), "wb") as f:
                f.write(b"PAR1")
        layout.write_products(self.dataset, [layout.ORCA_DIR])
        self.engine = FakeEngine()

    def write(self, **kwargs):
        with redirect_stdout(io.StringIO()):
            return csvout.write_csv(self.engine, self.dataset, **kwargs)

    def test_each_tables_csv_is_directly_in_its_archives_directory(self):
        self.write(csv=True)
        for archive, name in (("ORC_H_General", "autoruns"), ("ORC_H_FastFind", "FastFind")):
            self.assertEqual(os.listdir(os.path.join(self.dataset, archive)), [name + ".csv"])
        self.assertFalse(os.path.exists(os.path.join(self.dataset, "csv")))

    def test_csv_trunc_stays_at_the_datasets_top(self):
        self.write(trunc=True, drop=["MD5"])
        self.assertEqual(sorted(os.listdir(os.path.join(self.dataset, "csv-trunc"))),
                         ["FastFind.csv", "autoruns.csv"])

    def test_a_rebuild_removes_the_csvs_and_keeps_the_archive(self):
        from orca import cache
        kept = os.path.join(self.dataset, "ORC_H_General", "ORC_H_General.7z")
        os.makedirs(os.path.dirname(kept), exist_ok=True)
        open(kept, "wb").close()
        self.write(csv=True, trunc=True, drop=[])
        self.assertIn(os.path.join("ORC_H_General", "autoruns.csv"),
                      layout.products(self.dataset))

        cache.clear_dataset(self.dataset)

        self.assertEqual(os.listdir(os.path.join(self.dataset, "ORC_H_General")),
                         ["ORC_H_General.7z"])
        self.assertFalse(os.path.exists(os.path.join(self.dataset, "csv-trunc")))


if __name__ == "__main__":
    unittest.main()
