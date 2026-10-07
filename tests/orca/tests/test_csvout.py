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

    def test_both_directories_are_products_a_rebuild_removes(self):
        self.write(csv=True, trunc=True, drop=[])
        products = layout.products(self.dataset)
        self.assertIn(csvout.CSV_DIR, products)
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


if __name__ == "__main__":
    unittest.main()
