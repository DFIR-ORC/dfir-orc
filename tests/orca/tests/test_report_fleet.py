"""orca report adapts to a fleet (docs/superpowers/specs/2026-10-07-orca-report-fleet-design.md)."""
import csv
import io
import json
import os
import tempfile
import unittest
from contextlib import redirect_stdout

from orca import fleet, fleetdb, layout, report


def write_outcome(directory: str, host: str, run_id: str) -> None:
    os.makedirs(directory, exist_ok=True)
    document = {"dfir-orc": {"outcome": {
        "computer_name": host, "id": run_id, "start": "2026-09-25T14:41:27Z",
        "end": "2026-09-25T14:48:14Z", "command_set": []}}}
    with open(os.path.join(directory, f"ORC_{host}_20260925_144127_Outcome.json"), "w",
              encoding="utf-8") as f:
        json.dump(document, f)


def write_dataset(directory: str, host: str, run_id: str, tables: int = 0) -> str:
    """A dataset as ingest leaves it, as far as report reads it: the Outcome copy and the timing."""
    write_outcome(directory, host, run_id)
    os.makedirs(layout.orca_dir(directory), exist_ok=True)
    with open(layout.timing_path(directory), "w", encoding="utf-8") as f:
        f.write("kind,name,rows,bytes,seconds\n")
        for i in range(tables):
            f.write(f"parquet,Table{i},10,100,0.1\n")
    return directory


def write_fleet(root: str, rows: list, coverage: list = ()) -> None:
    where = os.path.join(root, fleet.FLEET_DIR)
    os.makedirs(where, exist_ok=True)
    for name, columns, content in ((fleet.HOSTS_FILE, fleet.HOST_COLUMNS, rows),
                                   (fleet.COVERAGE_FILE, fleet.COVERAGE_COLUMNS, coverage)):
        with open(os.path.join(where, name), "w", newline="", encoding="utf-8") as f:
            writer = csv.writer(f)
            writer.writerow(columns)
            writer.writerows(content)


class FleetCase(unittest.TestCase):
    def setUp(self):
        self.root = tempfile.TemporaryDirectory()
        self.addCleanup(self.root.cleanup)
        self.dir = self.root.name
        # report looks in the current directory for a collection's datasets: give it its own.
        self.cwd = tempfile.TemporaryDirectory()
        self.addCleanup(self.cwd.cleanup)
        self.addCleanup(os.chdir, os.getcwd())
        os.chdir(self.cwd.name)

    def invoke(self, paths, **kwargs) -> str:
        out = io.StringIO()
        with redirect_stdout(out):
            report.run_report(paths, **kwargs)
        return out.getvalue()

    def fleet(self, failed: bool = True) -> None:
        """Two hosts ingested into the fleet directory, a third that failed."""
        a = write_dataset(os.path.join(self.dir, "ORC_A"), "a.lab", "{A}", tables=2)
        b = write_dataset(os.path.join(self.dir, "ORC_B"), "b.lab", "{B}", tables=1)
        rows = [["a.lab", "{A}", a, "ingested", ""], ["b.lab", "{B}", b, "ingested", ""]]
        if failed:
            rows.append(["c.lab", "{C}", "", "failed", "archive is truncated"])
        write_fleet(self.dir, rows, [["b.lab", "autoruns", "missing"]])


class TestFleetDirectory(FleetCase):
    def test_its_datasets_are_reported_without_recurse(self):
        self.fleet(failed=False)
        lines = self.invoke([self.dir], one_line=True).strip().splitlines()
        self.assertEqual([line.split(":")[0] for line in lines], ["a.lab", "b.lab"])
        self.assertIn("tables=2", lines[0])

    def test_a_failed_host_is_a_host_in_every_shape(self):
        self.fleet()
        lines = self.invoke([self.dir], one_line=True).strip().splitlines()
        self.assertEqual(lines[-1], "c.lab: ok=False ingest=failed")

        hosts = json.loads(self.invoke([self.dir], as_json=True))
        failed = [h for h in hosts if h["host"] == "c.lab"][0]
        self.assertEqual((failed["ok"], failed["ingest"]), (False, "failed"))
        self.assertIn("archive is truncated", failed["error"])
        self.assertEqual([h for h in hosts if h["host"] == "b.lab"][0]["coverage_gaps"],
                         ["missing autoruns"])

        text = self.invoke([self.dir])
        self.assertIn("fleet: 3 collection(s), 2 ingested, 1 failed", text)
        self.assertIn("c.lab: ingest failed: archive is truncated", text)
        self.assertIn("b.lab: missing autoruns (1)", text)

    def test_a_dataset_that_is_gone_is_named(self):
        self.fleet(failed=False)
        write_fleet(self.dir, [["a.lab", "{A}", os.path.join(self.dir, "ORC_A"), "ingested", ""],
                               ["z.lab", "{Z}", os.path.join(self.dir, "gone"), "ingested", ""]])
        self.assertIn("z.lab: ok=False ingest=missing",
                      self.invoke([self.dir], one_line=True))

    def test_a_host_reported_anyway_is_not_also_missing(self):
        """A record from another machine names paths that do not exist here."""
        self.fleet(failed=False)
        write_fleet(self.dir, [["a.lab", "{A}", "C:/elsewhere/ORC_A", "ingested", ""]])
        output = self.invoke([self.dir], recurse=True, one_line=True)
        self.assertNotIn("ingest=missing", output)
        self.assertIn("a.lab:", output)

    def test_no_path_is_the_current_directory(self):
        self.fleet(failed=False)
        os.chdir(self.dir)
        self.assertEqual(len(self.invoke([], one_line=True).strip().splitlines()), 2)


class TestFleetDatabase(FleetCase):
    def test_fleet_duckdb_names_its_fleet(self):
        self.fleet(failed=False)
        database = os.path.join(self.dir, fleetdb.DATABASE_NAME)
        open(database, "wb").close()
        self.assertEqual(len(self.invoke([database], one_line=True).strip().splitlines()), 2)

    def test_another_file_is_refused(self):
        other = os.path.join(self.dir, "notes.txt")
        open(other, "wb").close()
        with self.assertRaisesRegex(RuntimeError, "no such directory"):
            self.invoke([other])


class TestCollectionFindsItsDatasets(FleetCase):
    def collection(self) -> str:
        directory = os.path.join(self.dir, "collect")
        write_outcome(directory, "a.lab", "{A}")
        return directory

    def test_in_the_current_directory(self):
        collection = self.collection()
        write_dataset(os.path.join(self.cwd.name, "ORC_A"), "a.lab", "{A}", tables=3)
        self.assertIn("tables=3", self.invoke([collection], one_line=True))

    def test_inside_the_collection(self):
        collection = self.collection()
        write_dataset(os.path.join(collection, "ORC_A"), "a.lab", "{A}", tables=4)
        self.assertIn("tables=4", self.invoke([collection], one_line=True))

    def test_another_collections_dataset_is_not_added(self):
        collection = self.collection()
        write_dataset(os.path.join(self.cwd.name, "ORC_B"), "b.lab", "{B}", tables=1)
        lines = self.invoke([collection], one_line=True).strip().splitlines()
        self.assertEqual(len(lines), 1)
        self.assertIn("tables=?", lines[0])

    def test_the_newest_ingest_wins(self):
        """A dataset left beside the collection by an older orca must not hide the new one."""
        collection = self.collection()
        old = write_dataset(os.path.join(collection, "ORC_A"), "a.lab", "{A}", tables=1)
        os.utime(layout.timing_path(old), (1_000_000_000, 1_000_000_000))
        write_dataset(os.path.join(self.cwd.name, "ORC_A"), "a.lab", "{A}", tables=5)
        self.assertIn("tables=5", self.invoke([collection], one_line=True))

    def test_a_plain_collection_report_has_no_fleet_block(self):
        collection = self.collection()
        self.assertNotIn("fleet:", self.invoke([collection]))


class TestNothingFound(FleetCase):
    def test_the_message_says_what_would_have_worked(self):
        with self.assertRaisesRegex(RuntimeError,
                                    "fleet's output directory.*fleet.duckdb.*--recurse"):
            self.invoke([self.dir])


if __name__ == "__main__":
    unittest.main()
