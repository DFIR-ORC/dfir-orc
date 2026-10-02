import contextlib
import datetime
import json
import os
import tempfile
import unittest
import unittest.mock
from orca import collect, hyperv, vhd


class TestClassifyTarget(unittest.TestCase):
    def test_directory_is_used_as_is(self):
        with tempfile.TemporaryDirectory() as d:
            self.assertEqual(collect.classify_offline_target(d),
                             collect.Target("directory", d))

    def test_vhd_and_vhdx_are_attached(self):
        with tempfile.TemporaryDirectory() as d:
            for name in ("w10.vhd", "w10.VHDX"):
                path = os.path.join(d, name)
                open(path, "wb").close()
                self.assertEqual(collect.classify_offline_target(path).kind, "vhd")

    def test_anything_else_is_a_disk_image_for_orc(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "disk.dd")
            open(path, "wb").close()
            self.assertEqual(collect.classify_offline_target(path).kind, "image")

    def test_missing_target_is_refused(self):
        with self.assertRaises(RuntimeError) as caught:
            collect.classify_offline_target(os.path.join("no", "such", "disk.dd"))
        self.assertIn("disk.dd", str(caught.exception))


class TestOrcArgv(unittest.TestCase):
    def test_offline_order_matches_invoke_orcoffline(self):
        argv = collect.orc_argv(r"C:\o\orc.exe", out=r"C:\r", temp=r"C:\t",
                                local_config=r"C:\r\local.xml", extra=["/key=GetEvt"],
                                overwrite=True, offline="F:")
        self.assertEqual(argv, [r"C:\o\orc.exe", "/key=GetEvt", "/overwrite", "/Offline=F:",
                                r"/TempDir=C:\t", r"/Out=C:\r", r"/local=C:\r\local.xml"])

    def test_without_overwrite_and_without_offline(self):
        argv = collect.orc_argv(r"C:\o\orc.exe", out=r"C:\r", temp=r"C:\t",
                                local_config=r"C:\r\local.xml", extra=[], overwrite=False)
        self.assertEqual(argv, [r"C:\o\orc.exe", r"/TempDir=C:\t", r"/Out=C:\r",
                                r"/local=C:\r\local.xml"])

    def test_extra_arguments_are_passed_through_untouched(self):
        argv = collect.orc_argv("orc.exe", out="o", temp="t", local_config="l",
                                extra=["/key=Get This", "/utf8"], overwrite=False)
        self.assertIn("/key=Get This", argv)
        self.assertIn("/utf8", argv)


class TestPrepareOutDir(unittest.TestCase):
    def test_creates_a_missing_directory_and_returns_it_absolute(self):
        with tempfile.TemporaryDirectory() as d:
            target = os.path.join(d, "results")
            returned = collect.prepare_out_dir(target, force=False)
            self.assertTrue(os.path.isdir(returned))
            self.assertTrue(os.path.isabs(returned))

    def test_an_empty_directory_is_accepted(self):
        with tempfile.TemporaryDirectory() as d:
            self.assertTrue(os.path.isdir(collect.prepare_out_dir(d, force=False)))

    def test_a_non_empty_directory_needs_force(self):
        with tempfile.TemporaryDirectory() as d:
            open(os.path.join(d, "DFIR-ORC_HOST.7z"), "wb").close()
            with self.assertRaises(RuntimeError) as caught:
                collect.prepare_out_dir(d, force=False)
            self.assertIn("--force", str(caught.exception))
            self.assertTrue(collect.prepare_out_dir(d, force=True))

    def test_relative_paths_are_made_absolute(self):
        # ORC resolves a relative path against its own working directory, not orca's, so a
        # relative '--out' would otherwise write somewhere nobody asked for. Compared by
        # shape, not against the temp dir itself, which can be a symlink on some platforms.
        with tempfile.TemporaryDirectory() as d:
            here = os.getcwd()
            os.chdir(d)
            try:
                returned = collect.prepare_out_dir("results", force=False)
                self.assertTrue(os.path.isabs(returned))
                self.assertEqual(os.path.basename(returned), "results")
                self.assertTrue(os.path.isdir(returned))
            finally:
                os.chdir(here)


class TestPrepareOutDirRejections(unittest.TestCase):
    def test_an_existing_file_is_refused_by_name(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "results")
            open(path, "wb").close()
            with self.assertRaises(RuntimeError) as caught:
                collect.prepare_out_dir(path, force=False)
            self.assertIn("results", str(caught.exception))
            self.assertIn("file", str(caught.exception))


class TestCopyOrcInto(unittest.TestCase):
    def test_the_copy_keeps_its_name_and_contents(self):
        with tempfile.TemporaryDirectory() as d:
            orc = os.path.join(d, "DFIR-ORC_x64.exe")
            with open(orc, "wb") as f:
                f.write(b"MZbinary")
            out = os.path.join(d, "results")
            os.makedirs(out)
            copied = collect.copy_orc_into(out, orc)
        self.assertEqual(copied, os.path.join(out, "DFIR-ORC_x64.exe"))

    def test_a_destination_that_cannot_be_written_says_why(self):
        # On Windows a running exe cannot be overwritten, which is what a --force rerun into
        # an --out whose copy is still collecting hits. A bare PermissionError would not say
        # that, and the copy happens before ORC starts.
        with tempfile.TemporaryDirectory() as d:
            orc = os.path.join(d, "orc.exe")
            open(orc, "wb").close()
            with unittest.mock.patch("shutil.copy2", side_effect=PermissionError("in use")):
                with self.assertRaises(RuntimeError) as caught:
                    collect.copy_orc_into(d, orc)
        message = str(caught.exception)
        self.assertIn("orc.exe", message)
        self.assertIn("may still be running", message)


class TestCommandLine(unittest.TestCase):
    def test_the_printed_command_line_is_quoted(self):
        # It reads as something to copy and re-run, so an unquoted path with a space would
        # print a command line that does not work.
        printed = collect.printable_argv(["orc.exe", "/key=Get This"])
        self.assertIn('"/key=Get This"', printed)


class TestRecord(unittest.TestCase):
    def test_record_holds_what_ran(self):
        with tempfile.TemporaryDirectory() as d:
            orc = os.path.join(d, "orc.exe")
            with open(orc, "wb") as f:
                f.write(b"MZfake")
            record = collect.collect_record(
                target=collect.Target("image", r"C:\dumps\w10.dd"), orc=orc,
                argv=[orc, "/Out=x"], out_dir=d, temp_dir=d, started=1.0, ended=3.5,
                exit_code=0, pulled=[])
            # Read inside the temp dir's lifetime: the file is gone once the block exits.
            expected_sha256 = collect.file_sha256(orc)
        self.assertEqual(record["target"], {"kind": "image", "value": r"C:\dumps\w10.dd"})
        self.assertEqual(record["orc"]["sha256"], expected_sha256)
        self.assertEqual(record["orc"]["size"], 6)
        self.assertEqual(record["exit_code"], 0)
        self.assertEqual(record["duration_s"], 2.5)
        self.assertEqual(record["argv"], [orc, "/Out=x"])

    def test_timestamps_carry_their_offset(self):
        # Two sides of a comparison collected on different hosts, or across a DST change, are
        # not orderable from a naive local timestamp.
        with tempfile.TemporaryDirectory() as d:
            orc = os.path.join(d, "orc.exe")
            open(orc, "wb").close()
            record = collect.collect_record(
                target=collect.Target("image", "x"), orc=orc, argv=[], out_dir=d, temp_dir=d,
                started=1.0, ended=2.0, exit_code=0, pulled=[])
        parsed = datetime.datetime.fromisoformat(record["started"])
        self.assertIsNotNone(parsed.tzinfo)

    def test_write_record_is_readable_json(self):
        with tempfile.TemporaryDirectory() as d:
            path = collect.write_record(d, {"exit_code": 11})
            self.assertEqual(os.path.basename(path), "collect.json")
            with open(path, "r", encoding="utf-8") as f:
                self.assertEqual(json.load(f)["exit_code"], 11)


class FakeProcess:
    """Stands in for subprocess.run: records the argv and returns a chosen exit code."""
    def __init__(self, code=0):
        self.code = code
        self.argv = None

    def __call__(self, argv, **kwargs):
        self.argv = argv
        return unittest.mock.Mock(returncode=self.code)


class TestRunCollectOffline(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.orc = os.path.join(self.dir.name, "orc.exe")
        with open(self.orc, "wb") as f:
            f.write(b"MZ")
        self.image = os.path.join(self.dir.name, "w10.dd")
        open(self.image, "wb").close()
        self.out = os.path.join(self.dir.name, "results")

    def tearDown(self):
        self.dir.cleanup()

    def test_runs_orc_on_the_image_and_writes_the_record(self):
        process = FakeProcess()
        code = collect.run_collect(orc=self.orc, out=self.out, offline=self.image,
                                   temp=os.path.join(self.dir.name, "t"),
                                   run_process=process)
        self.assertEqual(code, 0)
        self.assertIn(f"/Offline={os.path.abspath(self.image)}", process.argv)
        self.assertTrue(os.path.exists(os.path.join(self.out, "local.xml")))
        with open(os.path.join(self.out, "collect.json"), "r", encoding="utf-8") as f:
            record = json.load(f)
        self.assertEqual(record["target"]["kind"], "image")
        self.assertEqual(record["exit_code"], 0)

    def test_local_xml_is_utf8_and_carries_the_temporary(self):
        temp = os.path.join(self.dir.name, "t")
        collect.run_collect(orc=self.orc, out=self.out, offline=self.image, temp=temp,
                            run_process=FakeProcess())
        with open(os.path.join(self.out, "local.xml"), "rb") as f:
            raw = f.read()
        self.assertFalse(raw.startswith(b"\xff\xfe"))   # not UTF-16LE
        self.assertIn(os.path.abspath(temp).encode("utf-8"), raw)

    def test_a_failing_orc_keeps_results_and_propagates_the_code(self):
        code = collect.run_collect(orc=self.orc, out=self.out, offline=self.image,
                                   temp=os.path.join(self.dir.name, "t"),
                                   run_process=FakeProcess(code=11))
        self.assertEqual(code, 11)
        with open(os.path.join(self.out, "collect.json"), "r", encoding="utf-8") as f:
            self.assertEqual(json.load(f)["exit_code"], 11)

    def test_a_missing_orc_binary_is_refused(self):
        with self.assertRaises(RuntimeError) as caught:
            collect.run_collect(orc=os.path.join(self.dir.name, "absent.exe"), out=self.out,
                                offline=self.image, run_process=FakeProcess())
        self.assertIn("absent.exe", str(caught.exception))

    def test_a_directory_target_is_passed_straight_through(self):
        process = FakeProcess()
        collect.run_collect(orc=self.orc, out=self.out, offline=self.dir.name,
                            run_process=process)
        self.assertIn(f"/Offline={os.path.abspath(self.dir.name)}", process.argv)

    def test_by_default_the_copy_in_out_is_what_runs(self):
        process = FakeProcess()
        collect.run_collect(orc=self.orc, out=self.out, offline=self.image,
                            run_process=process)
        copied = os.path.join(self.out, "orc.exe")
        self.assertTrue(os.path.exists(copied), "the executable is kept with the results")
        self.assertEqual(process.argv[0], copied, "the copy is what ran, not the original")
        with open(os.path.join(self.out, "collect.json"), "r", encoding="utf-8") as f:
            record = json.load(f)
        self.assertEqual(record["orc"]["copied_to"], copied)
        self.assertEqual(record["orc"]["path"], os.path.abspath(self.orc))

    def test_no_orc_copy_runs_the_original_and_copies_nothing(self):
        process = FakeProcess()
        collect.run_collect(orc=self.orc, out=self.out, offline=self.image, copy_orc=False,
                            run_process=process)
        self.assertFalse(os.path.exists(os.path.join(self.out, "orc.exe")))
        self.assertEqual(process.argv[0], os.path.abspath(self.orc))
        with open(os.path.join(self.out, "collect.json"), "r", encoding="utf-8") as f:
            self.assertIsNone(json.load(f)["orc"].get("copied_to"))

    def test_a_rejected_run_leaves_no_output_directory_behind(self):
        # The target is checked before --out is created, so a run that never started does not
        # leave a local.xml that makes the corrected retry demand --force.
        out = os.path.join(self.dir.name, "fresh")
        with self.assertRaises(RuntimeError):
            collect.run_collect(orc=self.orc, out=out,
                                offline=os.path.join(self.dir.name, "absent.dd"),
                                run_process=FakeProcess())
        self.assertFalse(os.path.exists(out))

    def test_neither_offline_nor_vm_is_refused(self):
        with self.assertRaises(RuntimeError):
            collect.run_collect(orc=self.orc, out=self.out, run_process=FakeProcess())

    def test_a_vhd_run_records_the_volume_and_who_attached_it(self):
        # collect.json exists to remove exactly this ambiguity when two runs are compared:
        # which volume was read, and whether the disk was already mounted by someone else.
        vhdx = os.path.join(self.dir.name, "w10.vhdx")
        open(vhdx, "wb").close()
        attached = vhd.Attached(path=vhdx, physical_path=r"\\.\PhysicalDrive4", number=4,
                                drive_letter="G:", attached_by_orca=False)

        @contextlib.contextmanager
        def fake_attach(path):
            yield attached

        process = FakeProcess()
        with unittest.mock.patch.object(collect.vhd, "attached", fake_attach):
            collect.run_collect(orc=self.orc, out=self.out, offline=vhdx,
                                run_process=process)
        self.assertIn("/Offline=G:", process.argv)
        with open(os.path.join(self.out, "collect.json"), "r", encoding="utf-8") as f:
            target = json.load(f)["target"]
        self.assertEqual(target["kind"], "vhd")
        self.assertEqual(target["drive_letter"], "G:")
        self.assertFalse(target["attached_by_orca"])

    def test_local_config_is_merged_not_replaced(self):
        base = os.path.join(self.dir.name, "base.xml")
        with open(base, "w", encoding="utf-8") as f:
            f.write("<dfir-orc altitude='Highest'><console><file>c</file></console></dfir-orc>")
        collect.run_collect(orc=self.orc, out=self.out, offline=self.image, local_config=base,
                            run_process=FakeProcess())
        with open(os.path.join(self.out, "local.xml"), "r", encoding="utf-8") as f:
            written = f.read()
        self.assertIn("altitude", written)
        self.assertIn("<console>", written)


class TestRunCollectVm(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.orc = os.path.join(self.dir.name, "orc.exe")
        with open(self.orc, "wb") as f:
            f.write(b"MZ")
        self.out = os.path.join(self.dir.name, "results")
        self.calls = []

    def tearDown(self):
        self.dir.cleanup()

    def runner(self, result=None):
        def fake(**kwargs):
            self.calls.append(kwargs)
            return result or {"exit_code": 0, "pulled": ["DFIR-ORC_H.7z"]}
        return fake

    def test_pulls_by_default_and_records_what_came_back(self):
        code = collect.run_collect(orc=self.orc, out=self.out, vm="w10", checkpoint="clean",
                                   credential=os.path.join(self.dir.name, "k.clixml"),
                                   vm_runner=self.runner())
        self.assertEqual(code, 0)
        self.assertTrue(self.calls[0]["pull"])
        self.assertEqual(self.calls[0]["vm"], "w10")
        self.assertEqual(self.calls[0]["checkpoint"], "clean")
        with open(os.path.join(self.out, "collect.json"), "r", encoding="utf-8") as f:
            record = json.load(f)
        self.assertEqual(record["target"]["kind"], "vm")
        self.assertEqual(record["target"]["value"], "w10")
        self.assertEqual(record["pulled"], ["DFIR-ORC_H.7z"])

    def test_the_generated_config_points_output_into_the_guest(self):
        collect.run_collect(orc=self.orc, out=self.out, vm="w10",
                            credential=os.path.join(self.dir.name, "k.clixml"),
                            vm_runner=self.runner())
        with open(os.path.join(self.out, "local.xml"), "r", encoding="utf-8") as f:
            written = f.read()
        self.assertIn(hyperv.GuestPaths().out, written)

    def test_no_pull_is_forwarded(self):
        collect.run_collect(orc=self.orc, out=self.out, vm="w10", pull=False,
                            credential=os.path.join(self.dir.name, "k.clixml"),
                            vm_runner=self.runner())
        self.assertFalse(self.calls[0]["pull"])

    def test_a_failing_guest_run_propagates_its_code(self):
        code = collect.run_collect(orc=self.orc, out=self.out, vm="w10",
                                   credential=os.path.join(self.dir.name, "k.clixml"),
                                   vm_runner=self.runner({"exit_code": 11, "pulled": []}))
        self.assertEqual(code, 11)

    def test_no_credential_and_no_console_fails_fast(self):
        # Get-Credential would hang forever where nobody can answer it (CI).
        with unittest.mock.patch("sys.stdin") as stdin:
            stdin.isatty.return_value = False
            with self.assertRaises(RuntimeError) as caught:
                collect.run_collect(orc=self.orc, out=self.out, vm="w10",
                                    vm_runner=self.runner())
        self.assertIn("--credential", str(caught.exception))

    def test_an_unsupported_credential_format_is_refused_before_the_vm_is_touched(self):
        with self.assertRaises(RuntimeError) as caught:
            collect.run_collect(orc=self.orc, out=self.out, vm="w10",
                                credential=os.path.join(self.dir.name, "k.pem"),
                                vm_runner=self.runner())
        self.assertIn(".clixml", str(caught.exception))
        self.assertEqual(self.calls, [])

    def test_the_record_names_the_checkpoint(self):
        collect.run_collect(orc=self.orc, out=self.out, vm="w10", checkpoint="clean",
                            credential=os.path.join(self.dir.name, "k.clixml"),
                            vm_runner=self.runner())
        with open(os.path.join(self.out, "collect.json"), "r", encoding="utf-8") as f:
            target = json.load(f)["target"]
        self.assertEqual(target["checkpoint"], "clean")

    def test_temp_sets_the_guest_temp_directory(self):
        collect.run_collect(orc=self.orc, out=self.out, vm="w10", temp=r"D:\orc-temp",
                            credential=os.path.join(self.dir.name, "k.clixml"),
                            vm_runner=self.runner())
        self.assertIn(r"/TempDir=D:\orc-temp", self.calls[0]["orc_args"])
        self.assertEqual(self.calls[0]["guest"].temp, r"D:\orc-temp")

    def test_vm_only_options_are_refused_with_offline(self):
        image = os.path.join(self.dir.name, "w10.dd")
        open(image, "wb").close()
        with self.assertRaises(RuntimeError) as caught:
            collect.run_collect(orc=self.orc, out=self.out, offline=image, checkpoint="clean",
                                run_process=FakeProcess())
        self.assertIn("--checkpoint", str(caught.exception))

    def test_the_guest_is_fed_from_the_copy_in_out(self):
        # Same guarantee as the offline target: the result directory holds the binary that
        # was used, and the build output is read once for the copy instead of for the run.
        collect.run_collect(orc=self.orc, out=self.out, vm="w10",
                            credential=os.path.join(self.dir.name, "k.clixml"),
                            vm_runner=self.runner())
        copied = os.path.join(self.out, "orc.exe")
        self.assertTrue(os.path.exists(copied))
        self.assertEqual(self.calls[0]["local_exe"], copied)

    def test_no_orc_copy_pushes_the_build_binary_straight_to_the_guest(self):
        collect.run_collect(orc=self.orc, out=self.out, vm="w10", copy_orc=False,
                            credential=os.path.join(self.dir.name, "k.clixml"),
                            vm_runner=self.runner())
        self.assertFalse(os.path.exists(os.path.join(self.out, "orc.exe")))
        self.assertEqual(self.calls[0]["local_exe"], os.path.abspath(self.orc))

    def test_the_guest_argv_uses_guest_paths_only(self):
        collect.run_collect(orc=self.orc, out=self.out, vm="w10",
                            credential=os.path.join(self.dir.name, "k.clixml"),
                            vm_runner=self.runner())
        guest = hyperv.GuestPaths()
        self.assertEqual(self.calls[0]["orc_args"],
                         [f"/TempDir={guest.temp}", f"/Out={guest.out}",
                          f"/local={guest.config}"])
        with open(os.path.join(self.out, "collect.json"), "r", encoding="utf-8") as f:
            self.assertEqual(json.load(f)["argv"][0], guest.exe)


if __name__ == "__main__":
    unittest.main()
