import json
import os
import tempfile
import unittest
from unittest import mock
from orca import powershell


class TestQuote(unittest.TestCase):
    def test_wraps_in_single_quotes(self):
        self.assertEqual(powershell.quote(r"C:\k\contoso.clixml"),
                         r"'C:\k\contoso.clixml'")

    def test_doubles_an_embedded_quote(self):
        # Without this a path like  C:\it's\k.json  ends the string and the rest of it runs
        # as PowerShell.
        self.assertEqual(powershell.quote(r"C:\it's\k.json"), r"'C:\it''s\k.json'")


class TestQuoteArgument(unittest.TestCase):
    def test_a_plain_value_is_just_a_literal(self):
        self.assertEqual(powershell.quote_argument("/utf8"), "'/utf8'")

    def test_whitespace_is_wrapped_so_it_stays_one_argument(self):
        self.assertEqual(powershell.quote_argument("/key=Get This"), "'\"/key=Get This\"'")

    def test_an_embedded_double_quote_is_escaped_for_the_target(self):
        # CommandLineToArgvW reads \" as a literal quote; an unescaped one would end the
        # argument and the rest would arrive as separate ones.
        self.assertEqual(powershell.quote_argument('/key="Get Evt"'),
                         r"""'"/key=\"Get Evt\""'""")

    def test_a_quote_without_whitespace_needs_no_wrapper(self):
        self.assertEqual(powershell.quote_argument('/key="GetEvt"'), r"'/key=\"GetEvt\"'")


class TestCommandArgv(unittest.TestCase):
    def test_captured_run_is_non_interactive(self):
        argv = powershell.command_argv("pwsh.exe", "Get-VM", interactive=False)
        self.assertEqual(argv, ["pwsh.exe", "-NoProfile", "-NonInteractive", "-Command",
                                "Get-VM"])

    def test_interactive_run_keeps_the_console(self):
        argv = powershell.command_argv("pwsh.exe", "Get-Credential", interactive=True)
        self.assertEqual(argv, ["pwsh.exe", "-NoProfile", "-Command", "Get-Credential"])


class TestRun(unittest.TestCase):
    def test_failure_names_the_step_and_carries_stderr(self):
        completed = mock.Mock(returncode=1, stdout="", stderr="Get-VM: not found")
        with mock.patch.object(powershell, "find_powershell", return_value="pwsh"), \
             mock.patch("subprocess.run", return_value=completed):
            with self.assertRaises(RuntimeError) as caught:
                powershell.run("Get-VM x", step="start-vm")
        self.assertIn("start-vm", str(caught.exception))
        self.assertIn("not found", str(caught.exception))

    def test_an_interactive_failure_points_at_the_console(self):
        # Its stdio is inherited so nothing can be captured; the message has to say where the
        # real error went instead of ending in an empty colon.
        completed = mock.Mock(returncode=1)
        del completed.stderr
        del completed.stdout
        with mock.patch.object(powershell, "find_powershell", return_value="pwsh"), \
             mock.patch("subprocess.run", return_value=completed):
            with self.assertRaises(RuntimeError) as caught:
                powershell.run("x", step="guest-run", interactive=True)
        self.assertIn("console", str(caught.exception))

    def test_missing_powershell_is_refused_before_running(self):
        with mock.patch.object(powershell, "find_powershell", return_value=None):
            with self.assertRaises(RuntimeError) as caught:
                powershell.run("Get-VM", step="start-vm")
        self.assertIn("PowerShell", str(caught.exception))

    def test_result_file_is_decoded(self):
        with tempfile.TemporaryDirectory() as d:
            result = os.path.join(d, "result.json")
            with open(result, "w", encoding="utf-8") as f:
                json.dump({"exit_code": 11, "pulled": ["a.7z"]}, f)
            completed = mock.Mock(returncode=0, stdout="", stderr="")
            with mock.patch.object(powershell, "find_powershell", return_value="pwsh"), \
                 mock.patch("subprocess.run", return_value=completed):
                decoded = powershell.run("x", step="guest", interactive=True,
                                         result_path=result)
        self.assertEqual(decoded["exit_code"], 11)

    def test_a_result_file_with_a_bom_is_decoded(self):
        # Windows PowerShell 5.1's 'Set-Content -Encoding utf8' writes a BOM, and that is the
        # shell orca falls back to when pwsh is absent. Reading it as plain utf-8 fails after
        # the collection has already succeeded, which would throw the whole run away.
        with tempfile.TemporaryDirectory() as d:
            result = os.path.join(d, "result.json")
            with open(result, "wb") as f:
                f.write(b"\xef\xbb\xbf" + json.dumps({"exit_code": 0, "pulled": []}).encode())
            completed = mock.Mock(returncode=0, stdout="", stderr="")
            with mock.patch.object(powershell, "find_powershell", return_value="pwsh"), \
                 mock.patch("subprocess.run", return_value=completed):
                decoded = powershell.run("x", step="guest", interactive=True,
                                         result_path=result)
        self.assertEqual(decoded["exit_code"], 0)

    def test_a_missing_result_file_is_an_error(self):
        with tempfile.TemporaryDirectory() as d:
            completed = mock.Mock(returncode=0, stdout="", stderr="")
            with mock.patch.object(powershell, "find_powershell", return_value="pwsh"), \
                 mock.patch("subprocess.run", return_value=completed):
                with self.assertRaises(RuntimeError) as caught:
                    powershell.run("x", step="guest", interactive=True,
                                   result_path=os.path.join(d, "absent.json"))
        self.assertIn("guest", str(caught.exception))


if __name__ == "__main__":
    unittest.main()
