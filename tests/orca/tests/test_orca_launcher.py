import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))  # the orca root
LAUNCHER = os.path.join(HERE, "orca.py")


class TestLauncher(unittest.TestCase):
    def test_help_runs_and_relocates_bytecode(self):
        with tempfile.TemporaryDirectory() as tmp:
            env = dict(os.environ)
            # Pin every var tempfile.gettempdir() consults (TMPDIR, TEMP, TMP,
            # in that order) so the child's temp root is deterministically `tmp`
            # on any host - otherwise an inherited TMPDIR (common on Linux) wins.
            env["TMPDIR"] = tmp
            env["TMP"] = tmp
            env["TEMP"] = tmp
            # Ensure the child does not inherit an override that would defeat the test.
            env.pop("PYTHONPYCACHEPREFIX", None)
            proc = subprocess.run(
                [sys.executable, LAUNCHER, "--help"],
                cwd=tmp, env=env, capture_output=True, text=True,
            )
            self.assertEqual(proc.returncode, 0, proc.stderr)
            self.assertIn("ingest", proc.stdout)

            prefix = os.path.join(tmp, "orca-pycache")
            pyc = []
            for root, _dirs, files in os.walk(prefix):
                pyc += [f for f in files if f.endswith(".pyc")]
            self.assertTrue(pyc, "expected relocated .pyc under %TEMP%/orca-pycache")

    def test_launcher_sets_prefix_before_importing_package(self):
        # The prefix assignment must appear before the first `from orca` import.
        with open(LAUNCHER, "r", encoding="utf-8") as handle:
            text = handle.read()
        self.assertLess(
            text.index("sys.pycache_prefix"),
            text.index("from orca"),
            "sys.pycache_prefix must be set before importing orca",
        )
