"""orca report adapts to a fleet (docs/superpowers/specs/2026-10-07-orca-report-fleet-design.md)."""
import csv
import io
import json
import os
import tempfile
import unittest
from contextlib import redirect_stdout
from unittest import mock

from orca import fleet, fleetdb, ingest, layout, report


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

    def test_a_relative_dataset_is_read_from_the_fleet_directory(self):
        """What a moved fleet looks like: hosts.csv names its datasets relative to itself."""
        write_dataset(os.path.join(self.dir, "ORC_A"), "a.lab", "{A}", tables=2)
        write_fleet(self.dir, [["a.lab", "{A}", "ORC_A", "ingested", ""]])
        lines = self.invoke([self.dir], one_line=True).strip().splitlines()
        self.assertEqual(lines, [line for line in lines if "tables=2" in line])
        self.assertEqual([line.split(":")[0] for line in lines], ["a.lab"])

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

    def test_an_ignored_gap_is_passed_over_and_counted(self):
        self.fleet(failed=False)
        write_fleet(self.dir, [["a.lab", "{A}", os.path.join(self.dir, "ORC_A"), "ingested", ""],
                               ["b.lab", "{B}", os.path.join(self.dir, "ORC_B"), "ingested", ""]],
                    [["a.lab", "DNS_records", "missing"], ["b.lab", "autoruns", "missing"],
                     ["b.lab", "AD_computers", "skipped"]])

        text = self.invoke([self.dir], ignore_patterns=["DNS_records,AD_*"])

        # a.lab keeps its line: it is the only record that something was passed over.
        self.assertIn("a.lab: (0 of 1, 1 ignored)", text)
        self.assertIn("b.lab: missing autoruns (1 of 2, 1 ignored)", text)
        self.assertNotIn("DNS_records", text)
        hosts = json.loads(self.invoke([self.dir], as_json=True,
                                       ignore_patterns=["DNS_records,AD_*"]))
        self.assertEqual([h["coverage_gaps"] for h in hosts], [[], ["missing autoruns"]])

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


class TestCollectionIsIngested(FleetCase):
    """A collection is handed to ingest - or its current dataset reused - and the report is
    read from the dataset (§8 of the dataset layout spec)."""

    def setUp(self):
        super().setUp()
        self.calls = []
        self.failures = []

        def ingest_collections(directory, out=None, **options):
            self.calls.append((directory, options))
            ingested = []
            for path in ingest.find_outcome_jsons(directory):
                host, run_id, _stamp = ingest.outcome_identity(path)
                dataset = write_dataset(os.path.join(self.cwd.name, "ds-" + host), host, run_id,
                                        tables=3)
                ingested.append((ingest.Collection(path, [], host, run_id, ""), dataset))
            failed = [(ingest.Collection(None, [], who, "", ""), why)
                      for who, why in self.failures]
            return [], fleet.FleetResult(ingested, failed), directory

        patch = mock.patch.object(report._fleet, "ingest_collections", ingest_collections)
        patch.start()
        self.addCleanup(patch.stop)

    def collection(self) -> str:
        directory = os.path.join(self.dir, "collect")
        write_outcome(directory, "a.lab", "{A}")
        return directory

    def test_a_collection_is_reported_from_the_dataset_ingest_gives(self):
        self.assertIn("tables=3", self.invoke([self.collection()], one_line=True))
        self.assertEqual(len(self.calls), 1)

    def test_a_dataset_elsewhere_is_reused_rather_than_ingested_again(self):
        self.invoke([self.collection()], one_line=True)
        self.assertTrue(self.calls[0][1]["elsewhere"])

    def test_the_ingest_options_reach_the_ingest(self):
        self.invoke([self.collection()], one_line=True, private_key="k.pem", password="S3cret",
                    at_input=True, strict=True, refresh=True, jobs=3)
        options = self.calls[0][1]
        self.assertEqual((options["private_key"], options["password"], options["at_input"],
                          options["strict"], options["refresh"], options["jobs"]),
                         ("k.pem", "S3cret", True, True, True, 3))

    def test_a_dataset_is_read_as_it_is(self):
        dataset = write_dataset(os.path.join(self.dir, "ORC_A"), "a.lab", "{A}", tables=2)
        self.assertIn("tables=2", self.invoke([dataset], one_line=True))
        self.assertEqual(self.calls, [])

    def test_list_on_a_plain_outcome_ingests_nothing(self):
        self.invoke([self.collection()], list_commands=True)
        self.assertEqual(self.calls, [])

    def test_list_on_a_sealed_outcome_unseals_it_and_ingests_nothing(self):
        directory = os.path.join(self.dir, "sealed")
        os.makedirs(directory)
        open(os.path.join(directory, "ORC_A_Outcome.json.7z.p7b"), "wb").close()

        def unseal(dirs, work, *args):
            write_outcome(work, "a.lab", "{A}")
            return {directory: work}

        with mock.patch.object(report, "unsealed_sidecar_dirs", side_effect=unseal) as unsealed:
            self.invoke([directory], list_commands=True)

        self.assertEqual(self.calls, [])
        self.assertEqual(unsealed.call_args.args[0], [directory])

    def test_the_outcome_view_alone_ingests_nothing(self):
        self.invoke([self.collection()], views=["outcome"])
        self.assertEqual(self.calls, [])

    def test_a_view_that_reads_the_logs_ingests(self):
        self.invoke([self.collection()], views=["outcome", "log"])
        self.assertEqual(len(self.calls), 1)

    def test_loose_archives_are_ingested_only_where_named(self):
        """Under --recurse they could be a dataset's kept archives or an expanded tree's."""
        loose = os.path.join(self.dir, "loose")
        os.makedirs(loose)
        open(os.path.join(loose, "ORC_X_General.7z"), "wb").close()
        self.collection()

        self.invoke([self.dir], recurse=True, one_line=True)
        self.assertNotIn(loose, [directory for directory, _ in self.calls])

        self.calls.clear()
        with self.assertRaises(RuntimeError):     # the fake ingests nothing from it
            self.invoke([loose], one_line=True)
        self.assertEqual([directory for directory, _ in self.calls], [loose])

    def test_a_collection_that_could_not_be_ingested_is_a_host_in_every_shape(self):
        self.failures.append(("b.lab", "archive is truncated"))
        collection = self.collection()

        self.assertIn("b.lab: ok=False ingest=failed", self.invoke([collection], one_line=True))
        hosts = json.loads(self.invoke([collection], as_json=True))
        self.assertIn({"host": "b.lab", "ok": False, "ingest": "failed",
                       "error": "archive is truncated"}, hosts)
        self.assertIn("b.lab: archive is truncated", self.invoke([collection]))

    def test_what_ingest_prints_stays_off_the_report(self):
        def chatty(directory, out=None, **options):
            print("ingesting something")
            return [], fleet.FleetResult([], []), directory

        with mock.patch.object(report._fleet, "ingest_collections", chatty), \
                mock.patch("sys.stderr", io.StringIO()) as stderr, \
                self.assertRaises(RuntimeError):
            self.invoke([self.collection()], as_json=True)
        self.assertIn("ingesting something", stderr.getvalue())

    def test_a_plain_collection_report_has_no_fleet_block(self):
        self.assertNotIn("fleet:", self.invoke([self.collection()]))


class TestNothingFound(FleetCase):
    def test_the_message_says_what_would_have_worked(self):
        with self.assertRaisesRegex(RuntimeError,
                                    "fleet's output directory.*fleet.duckdb.*--recurse"):
            self.invoke([self.dir])


if __name__ == "__main__":
    unittest.main()
