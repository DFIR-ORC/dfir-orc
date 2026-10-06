import csv
import io
import json
import os
import tempfile
import unittest
from contextlib import redirect_stdout
from unittest import mock

from orca import cache, fleet, ingest, layout


def setUpModule():
    # orca writes its products under the current directory by default: run this module from
    # an empty one, never from the source tree.
    here = os.getcwd()
    work = tempfile.TemporaryDirectory()
    os.chdir(work.name)
    unittest.addModuleCleanup(work.cleanup)
    unittest.addModuleCleanup(os.chdir, here)


def write_collection(d, stem, computer, archive):
    doc = {"dfir-orc": {"outcome": {"id": "{%s}" % computer, "computer_name": computer,
                                    "timestamp": "20260101_000000",
                                    "command_set": [{"name": "General",
                                                     "archive": {"name": archive}}]}}}
    with open(os.path.join(d, stem + "_Outcome.json"), "w", encoding="utf-8") as f:
        json.dump(doc, f)
    with open(os.path.join(d, archive), "wb") as f:
        f.write(b"7z\xbc\xaf\x27\x1c")


def seven_zip():
    """fleet unseals before it groups, which needs 7z to be findable; none is installed here."""
    return mock.patch.object(fleet.sevenzip, "find_7z", return_value="7z")


class TestRunFleet(unittest.TestCase):
    def test_every_collection_is_ingested_once(self):
        with tempfile.TemporaryDirectory() as d:
            write_collection(d, "ORC_A", "a.lab", "ORC_A_General.7z")
            write_collection(d, "ORC_B", "b.lab", "ORC_B_General.7z")

            with seven_zip(), mock.patch.object(
                    fleet.cache, "ensure_dataset",
                    side_effect=lambda src, out, **kw: os.path.join(d, "ds")) as ens, \
                    redirect_stdout(io.StringIO()):
                code = fleet.run_fleet(d)

            self.assertEqual(code, 0)
            self.assertEqual(ens.call_count, 2)
            got = [call.kwargs["collection"].computer_name for call in ens.call_args_list]
            self.assertEqual(got, ["a.lab", "b.lab"])

    def test_one_failing_host_does_not_stop_the_others(self):
        with tempfile.TemporaryDirectory() as d:
            write_collection(d, "ORC_A", "a.lab", "ORC_A_General.7z")
            write_collection(d, "ORC_B", "b.lab", "ORC_B_General.7z")

            def flaky(src, out, **kw):
                if kw["collection"].computer_name == "a.lab":
                    raise RuntimeError("archive is truncated")
                return os.path.join(d, "ds-b")

            printed = io.StringIO()
            with seven_zip(), mock.patch.object(fleet.cache, "ensure_dataset", side_effect=flaky), \
                    redirect_stdout(printed):
                code = fleet.run_fleet(d)

            self.assertEqual(code, 1)
            self.assertIn("a.lab", printed.getvalue())
            self.assertIn("archive is truncated", printed.getvalue())

    def test_a_directory_with_no_collection_is_an_error(self):
        """Review Focus 3: zero hosts must not read as a clean fleet."""
        with tempfile.TemporaryDirectory() as d:
            with seven_zip(), self.assertRaises(RuntimeError) as caught, \
                    redirect_stdout(io.StringIO()):
                fleet.run_fleet(d)
            self.assertIn("no collection", str(caught.exception))

    def test_a_sealed_fleet_is_unsealed_before_it_is_grouped(self):
        """Review Focus 6: grouping first would find no Outcome and one unnamed collection."""
        with tempfile.TemporaryDirectory() as d:
            for stem in ("ORC_A", "ORC_B"):
                with open(os.path.join(d, stem + "_Outcome.json.7z.p7b"), "wb") as f:
                    f.write(b"sealed")
                with open(os.path.join(d, stem + "_General.7z.p7b"), "wb") as f:
                    f.write(b"x")

            def unseal(sealed, target, seven, private_key=None, cms_backend=None):
                stem = os.path.basename(sealed).split("_Outcome")[0]
                write_collection(d, stem, stem.lower() + ".lab", stem + "_General.7z.p7b")
                return target

            with seven_zip(), mock.patch.object(fleet.ingest, "unseal_one", unseal), \
                    mock.patch.object(fleet.cache, "ensure_dataset",
                                      side_effect=lambda src, out, **kw: os.path.join(d, "ds")) as ens, \
                    redirect_stdout(io.StringIO()):
                fleet.run_fleet(d)

            got = sorted(call.kwargs["collection"].computer_name for call in ens.call_args_list)
            self.assertEqual(got, ["orc_a.lab", "orc_b.lab"])

    def test_a_second_run_ingests_only_what_changed(self):
        """Spec test 8: resume is the cache doing its job, host by host."""
        with tempfile.TemporaryDirectory() as d:
            write_collection(d, "ORC_A", "a.lab", "ORC_A_General.7z")
            write_collection(d, "ORC_B", "b.lab", "ORC_B_General.7z")
            calls = []

            def fake_ingest(source, out_dir, **kwargs):
                calls.append(kwargs["collection"].computer_name)
                os.makedirs(layout.orca_dir(out_dir), exist_ok=True)
                with open(layout.products_path(out_dir), "w", encoding="utf-8") as f:
                    f.write(layout.ORCA_DIR + "\n")

            with seven_zip(), mock.patch.object(cache.ingest, "run_ingest", fake_ingest), \
                    redirect_stdout(io.StringIO()):
                fleet.run_fleet(d)
                fleet.run_fleet(d)
                with open(os.path.join(d, "ORC_B_General.7z"), "ab") as f:
                    f.write(b"more")
                fleet.run_fleet(d)

            self.assertEqual(calls, ["a.lab", "b.lab", "b.lab"])


class TestCoverage(unittest.TestCase):
    def _read(self, path):
        with open(path, newline="", encoding="utf-8") as f:
            return list(csv.DictReader(f))

    def test_a_host_missing_a_table_another_host_has_is_named(self):
        with tempfile.TemporaryDirectory() as d:
            a, b = os.path.join(d, "ds-a"), os.path.join(d, "ds-b")
            os.makedirs(a, exist_ok=True)
            os.makedirs(b, exist_ok=True)
            ca = ingest.Collection(os.path.join(d, "ORC_A_Outcome.json"), [], "a.lab", "{A}", "T")
            cb = ingest.Collection(os.path.join(d, "ORC_B_Outcome.json"), [], "b.lab", "{B}", "T")
            result = fleet.FleetResult([(ca, a), (cb, b)], [])

            tables = {a: ["autoruns", "NTFSInfo"], b: ["NTFSInfo"]}
            where = os.path.join(d, fleet.FLEET_DIR)
            with mock.patch.object(fleet.layout, "table_names", side_effect=lambda p: tables[p]), \
                    mock.patch.object(fleet.layout, "skipped", return_value={}):
                fleet.write_coverage(where, [ca, cb], result)

            rows = self._read(os.path.join(where, "coverage.csv"))
            self.assertEqual(rows, [{"OrcComputerName": "b.lab", "table": "autoruns",
                                     "status": "missing"}])

    def test_a_skipped_table_is_recorded_as_skipped(self):
        with tempfile.TemporaryDirectory() as d:
            a = os.path.join(d, "ds-a")
            os.makedirs(a, exist_ok=True)
            ca = ingest.Collection(os.path.join(d, "ORC_A_Outcome.json"), [], "a.lab", "{A}", "T")
            result = fleet.FleetResult([(ca, a)], [])

            where = os.path.join(d, fleet.FLEET_DIR)
            with mock.patch.object(fleet.layout, "table_names", return_value=["NTFSInfo"]), \
                    mock.patch.object(fleet.layout, "skipped",
                                      return_value={"USNInfo": "7z extract failed"}):
                fleet.write_coverage(where, [ca], result)

            rows = self._read(os.path.join(where, "coverage.csv"))
            self.assertEqual(rows, [{"OrcComputerName": "a.lab", "table": "USNInfo",
                                     "status": "skipped"}])

    def test_hosts_csv_carries_the_denominator_including_failures(self):
        with tempfile.TemporaryDirectory() as d:
            a = os.path.join(d, "ds-a")
            os.makedirs(a, exist_ok=True)
            good = ingest.Collection(os.path.join(d, "ORC_A_Outcome.json"), [],
                                     "a.lab", "{A}", "T")
            bad = ingest.Collection(os.path.join(d, "ORC_B_Outcome.json"), [],
                                    "b.lab", "{B}", "T")
            result = fleet.FleetResult([(good, a)], [(bad, "archive is truncated")])

            where = os.path.join(d, fleet.FLEET_DIR)
            with mock.patch.object(fleet.layout, "table_names", return_value=[]), \
                    mock.patch.object(fleet.layout, "skipped", return_value={}):
                fleet.write_coverage(where, [good, bad], result)

            rows = self._read(os.path.join(where, "hosts.csv"))
            self.assertEqual([r["OrcComputerName"] for r in rows], ["a.lab", "b.lab"])
            self.assertEqual([r["status"] for r in rows], ["ingested", "failed"])
            self.assertEqual(rows[1]["dataset"], "")
            self.assertEqual(rows[1]["error"], "archive is truncated")

    def test_the_same_host_twice_is_two_rows_with_their_own_gaps(self):
        """Review Focus 1: two runs of one host are two denominators, not one overwritten."""
        with tempfile.TemporaryDirectory() as d:
            one, two = os.path.join(d, "ds-1"), os.path.join(d, "ds-2")
            os.makedirs(one, exist_ok=True)
            os.makedirs(two, exist_ok=True)
            c1 = ingest.Collection(os.path.join(d, "ORC_1_Outcome.json"), [], "a.lab", "{1}", "T1")
            c2 = ingest.Collection(os.path.join(d, "ORC_2_Outcome.json"), [], "a.lab", "{2}", "T2")
            result = fleet.FleetResult([(c1, one), (c2, two)], [])

            tables = {one: ["autoruns"], two: []}
            where = os.path.join(d, fleet.FLEET_DIR)
            with mock.patch.object(fleet.layout, "table_names", side_effect=lambda p: tables[p]), \
                    mock.patch.object(fleet.layout, "skipped", return_value={}):
                fleet.write_coverage(where, [c1, c2], result)

            hosts = self._read(os.path.join(where, "hosts.csv"))
            self.assertEqual([r["OrcRunId"] for r in hosts], ["{1}", "{2}"])
            rows = self._read(os.path.join(where, "coverage.csv"))
            self.assertEqual(rows, [{"OrcComputerName": "a.lab", "table": "autoruns",
                                     "status": "missing"}])


class TestCoverageLandsWithTheProducts(unittest.TestCase):
    def test_under_out_when_given(self):
        """Not in the collection directory: that is read-only on a share, and failing there
        after every host was ingested is the worst moment to fail."""
        with tempfile.TemporaryDirectory() as d, tempfile.TemporaryDirectory() as out:
            write_collection(d, "ORC_A", "a.lab", "ORC_A_General.7z")

            with seven_zip(), mock.patch.object(
                    fleet.cache, "ensure_dataset",
                    side_effect=lambda src, o, **kw: os.path.join(out, "ds")), \
                    mock.patch.object(fleet.layout, "table_names", return_value=[]), \
                    redirect_stdout(io.StringIO()):
                fleet.run_fleet(d, out)

            self.assertTrue(os.path.isfile(os.path.join(out, fleet.FLEET_DIR, "hosts.csv")))
            self.assertFalse(os.path.exists(os.path.join(d, fleet.FLEET_DIR)))


if __name__ == "__main__":
    unittest.main()
