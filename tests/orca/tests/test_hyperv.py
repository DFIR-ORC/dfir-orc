import json
import os
import tempfile
import unittest
from orca import hyperv


class TestCredentialExpression(unittest.TestCase):
    def test_no_file_prompts(self):
        self.assertIn("Get-Credential", hyperv.credential_expression(None))

    def test_clixml_is_imported(self):
        expression = hyperv.credential_expression(r"C:\k\vm.clixml")
        self.assertIn("Import-Clixml", expression)
        self.assertIn(r"'C:\k\vm.clixml'", expression)

    def test_xml_is_imported_too(self):
        self.assertIn("Import-Clixml", hyperv.credential_expression(r"C:\k\vm.xml"))

    def test_json_builds_a_pscredential(self):
        expression = hyperv.credential_expression(r"C:\k\vm.json")
        self.assertIn("ConvertFrom-Json", expression)
        self.assertIn("PSCredential", expression)
        self.assertIn("ConvertTo-SecureString", expression)

    def test_unknown_extension_names_the_known_ones(self):
        with self.assertRaises(RuntimeError) as caught:
            hyperv.credential_expression(r"C:\k\vm.pem")
        message = str(caught.exception)
        self.assertIn(".clixml", message)
        self.assertIn(".json", message)

    def test_a_quote_in_the_path_is_escaped(self):
        expression = hyperv.credential_expression(r"C:\it's\vm.json")
        self.assertIn("'C:\\it''s\\vm.json'", expression)


class TestScripts(unittest.TestCase):
    def test_restore_stops_then_restores(self):
        script = hyperv.restore_checkpoint_script("w10", "clean")
        self.assertLess(script.index("Stop-VM"), script.index("Restore-VMCheckpoint"))
        self.assertIn("'clean'", script)
        self.assertIn("-Confirm:$false", script)

    def test_start_waits_for_the_guest(self):
        script = hyperv.start_script("w10")
        self.assertIn("Start-VM", script)
        self.assertIn("Wait-VM", script)

    def test_copy_exe_pushes_from_the_host(self):
        script = hyperv.copy_exe_script("w10", r"C:\b\orc.exe", r"C:\Windows\Temp\orc.exe")
        self.assertIn("Copy-VMFile", script)
        self.assertIn("-FileSource Host", script)
        self.assertIn(r"'C:\b\orc.exe'", script)

    def test_guest_script_runs_waits_pulls_and_reports(self):
        script = hyperv.guest_script(
            "w10", credential_expr="Get-Credential", guest=hyperv.GuestPaths(), local_config=r"C:\r\local.xml",
            orc_args=["/overwrite", r"/Out=C:\Windows\Temp\orc-out"], out_dir=r"C:\r",
            result_path=r"C:\r\res.json", pull=True)
        self.assertIn("New-PSSession", script)
        self.assertIn("-ToSession", script)
        self.assertIn("-FromSession", script)
        self.assertIn("Remove-PSSession", script)
        self.assertIn("exit_code", script)
        self.assertIn("/overwrite", script)
        # The exit code of the process we started, not a Wait-Process by name.
        self.assertIn("-PassThru", script)
        self.assertNotIn("Wait-Process", script)

    def test_guest_script_copies_the_generated_config_in(self):
        script = hyperv.guest_script(
            "w10", credential_expr="Get-Credential", guest=hyperv.GuestPaths(), local_config=r"C:\r\local.xml",
            orc_args=[], out_dir=r"C:\r", result_path=r"C:\r\res.json", pull=True)
        self.assertIn(r"'C:\r\local.xml'", script)
        self.assertIn(r"'C:\Windows\Temp\local.xml'", script)

    def test_an_argument_with_a_space_stays_one_argument(self):
        # PowerShell joins -ArgumentList with spaces and does NOT re-quote, so '/key=Get This'
        # would reach ORC as two arguments. The offline target keeps it whole (subprocess
        # quotes it), and the two targets must not disagree.
        script = hyperv.guest_script(
            "w10", credential_expr="Get-Credential", guest=hyperv.GuestPaths(), local_config=r"C:\r\local.xml",
            orc_args=["/key=Get This", "/utf8"], out_dir=r"C:\r",
            result_path=r"C:\r\res.json", pull=True)
        self.assertIn("'\"/key=Get This\"'", script)
        self.assertIn("'/utf8'", script)

    def test_the_pulled_list_comes_from_the_guest_not_the_destination(self):
        # Enumerating the host --out counts local.xml, and with --force a previous run's
        # archives too, so collect.json would claim files that were never pulled.
        script = hyperv.guest_script(
            "w10", credential_expr="Get-Credential", guest=hyperv.GuestPaths(), local_config=r"C:\r\local.xml",
            orc_args=[], out_dir=r"C:\r", result_path=r"C:\r\res.json", pull=True)
        enumerate_at = script.index("Get-ChildItem")
        self.assertIn(r"'C:\Windows\Temp\orc-out'", script[enumerate_at:])
        # The host directory appears only as the copy destination, never as what is listed.
        self.assertNotIn(r"Get-ChildItem -Recurse -File 'C:\r'", script)

    def test_no_pull_does_not_copy_results_back(self):
        script = hyperv.guest_script(
            "w10", credential_expr="Get-Credential", guest=hyperv.GuestPaths(), local_config=r"C:\r\local.xml",
            orc_args=[], out_dir=r"C:\r", result_path=r"C:\r\res.json", pull=False)
        self.assertNotIn("-FromSession", script)


class TestTheSecretNeverLeaves(unittest.TestCase):
    def test_a_json_credential_puts_the_path_in_the_script_not_the_password(self):
        # The script is passed to PowerShell through '-Command', so it IS the command line:
        # anything embedded in it is readable by every other process on the box. PowerShell
        # reads the file itself, which is the whole reason orca never parses it.
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "vm.json")
            with open(path, "w", encoding="utf-8") as f:
                json.dump({"user": "LAB\\tester", "password": "SuperSecret1"}, f)
            script = hyperv.guest_script(
                "w10", credential_expr=hyperv.credential_expression(path),
                guest=hyperv.GuestPaths(), local_config=r"C:\r\local.xml", orc_args=[],
                out_dir=r"C:\r", result_path=r"C:\r\res.json", pull=True)
        self.assertNotIn("SuperSecret1", script)
        self.assertIn(path, script)

    def test_no_credential_file_means_no_credential_text_at_all(self):
        script = hyperv.guest_script(
            "w10", credential_expr=hyperv.credential_expression(None),
            guest=hyperv.GuestPaths(), local_config=r"C:\r\local.xml", orc_args=[],
            out_dir=r"C:\r", result_path=r"C:\r\res.json", pull=True)
        self.assertIn("Get-Credential", script)


class FakeRunner:
    def __init__(self, result=None):
        self.steps = []
        self.result = result or {"exit_code": 0, "pulled": []}

    def __call__(self, script, *, step, interactive=False, result_path=None):
        self.steps.append(step)
        return self.result if result_path else None


class TestSequence(unittest.TestCase):
    def kwargs(self, **overrides):
        base = dict(vm="w10", checkpoint=None, local_exe=r"C:\b\orc.exe",
                    local_config=r"C:\r\local.xml", credential=None, orc_args=[],
                    out_dir=r"C:\r", result_path=r"C:\r\res.json", pull=True)
        base.update(overrides)
        return base

    def test_without_a_checkpoint(self):
        runner = FakeRunner()
        hyperv.run_vm_collect(run_step=runner, **self.kwargs())
        self.assertEqual(runner.steps, ["start-vm", "copy-exe", "guest-run"])

    def test_with_a_checkpoint_restore_comes_first(self):
        runner = FakeRunner()
        hyperv.run_vm_collect(run_step=runner, **self.kwargs(checkpoint="clean"))
        self.assertEqual(runner.steps,
                         ["restore-checkpoint", "start-vm", "copy-exe", "guest-run"])

    def test_returns_the_guest_result(self):
        runner = FakeRunner(result={"exit_code": 11, "pulled": ["DFIR-ORC_H.7z"]})
        result = hyperv.run_vm_collect(run_step=runner, **self.kwargs())
        self.assertEqual(result["exit_code"], 11)
        self.assertEqual(result["pulled"], ["DFIR-ORC_H.7z"])


if __name__ == "__main__":
    unittest.main()
