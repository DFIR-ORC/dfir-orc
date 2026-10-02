"""Real 'orca collect' runs, against a real ORC binary and a real disk.

Everything here skips unless the host supplies the inputs through the environment, the way the
duckdb integration tests skip when duckdb is absent:

  ORCA_IT_ORC     a configured ORC executable (DFIR-ORC.exe)
  ORCA_IT_IMAGE   a raw disk image (.dd) for the offline target
  ORCA_IT_VHDX    a .vhd/.vhdx; the attach needs an elevated shell
  ORCA_IT_KEYWORD the ORC keyword to collect, default SystemInfo (keep it small)
  ORCA_IT_ARGS    the arguments to pass ORC instead of just '/key=<keyword>'

A packaged DFIR-ORC.exe needs no ORCA_IT_ARGS. A RAW build-tree binary does: it has no
embedded WolfLauncher configuration and no capsule to supply a safe temporary directory, so
'/TempDir=' alone fails with "Invalid safe temporary directory" (exit 87). Give it:

  ORCA_IT_ARGS = WolfLauncher /config=<...>\wolf-main.example.xml /SafeTemp=<an existing dir>

These are the only tests that exercise the argv for real, the local.xml ORC actually accepts,
and (for the VHDX) the ctypes attach and the drive-letter matching.
"""
import json
import os
import shlex
import tempfile
import unittest
from orca import collect

ORC = os.environ.get("ORCA_IT_ORC")
IMAGE = os.environ.get("ORCA_IT_IMAGE")
VHDX = os.environ.get("ORCA_IT_VHDX")
KEYWORD = os.environ.get("ORCA_IT_KEYWORD", "SystemInfo")


def split_args(value: str | None, keyword: str) -> list[str]:
    """ORCA_IT_ARGS into an argv list, or the default '/key=<keyword>'.

    posix=False so a Windows path keeps its backslashes; the quotes shlex leaves on a token
    are stripped here, which is what lets a '/config=' path contain spaces.
    """
    if not value:
        return [f"/key={keyword}"]

    return [token[1:-1] if len(token) > 1 and token[0] == token[-1] == '"' else token
            for token in shlex.split(value, posix=False)]


ARGS = split_args(os.environ.get("ORCA_IT_ARGS"), KEYWORD)


def read_record(out_dir: str) -> dict:
    with open(os.path.join(out_dir, "collect.json"), "r", encoding="utf-8") as source:
        return json.load(source)


def archives(out_dir: str) -> list[str]:
    return [name for name in os.listdir(out_dir) if name.endswith((".7z", ".p7b"))]


class TestSplitArgs(unittest.TestCase):
    """Always runs: the parsing is pure, and a mistake here would show up as an unexplained
    ORC usage error in whichever of the skipped tests the host can run."""

    def test_the_default_is_just_the_keyword(self):
        self.assertEqual(split_args(None, "SystemInfo"), ["/key=SystemInfo"])

    def test_a_raw_build_invocation_keeps_its_backslashes(self):
        args = split_args(r"WolfLauncher /config=S:\orc\wolf.xml /SafeTemp=C:\t", "X")
        self.assertEqual(args, ["WolfLauncher", r"/config=S:\orc\wolf.xml", r"/SafeTemp=C:\t"])

    def test_a_quoted_path_with_spaces_stays_one_argument(self):
        args = split_args(r'WolfLauncher "/config=C:\my configs\wolf.xml"', "X")
        self.assertEqual(args, ["WolfLauncher", r"/config=C:\my configs\wolf.xml"])


@unittest.skipUnless(ORC and IMAGE, "set ORCA_IT_ORC and ORCA_IT_IMAGE to run this")
class TestCollectFromADiskImage(unittest.TestCase):
    def test_it_leaves_archives_a_record_and_the_configuration(self):
        with tempfile.TemporaryDirectory() as d:
            out = os.path.join(d, "results")
            code = collect.run_collect(orc=ORC, out=out, offline=IMAGE,
                                       temp=os.path.join(d, "temp"),
                                       orc_args=ARGS)
            record = read_record(out)
            self.assertEqual(record["exit_code"], code, "the record must agree with the run")
            self.assertEqual(record["target"]["kind"], "image")
            self.assertTrue(os.path.exists(os.path.join(out, "local.xml")))
            # ORC accepting the UTF-8 local.xml is the thing this proves: a rejected
            # configuration shows up as a non-zero exit with no archive.
            self.assertEqual(code, 0, f"ORC exited {code}; see {out}")
            self.assertTrue(archives(out), f"no archive in {out}: {sorted(os.listdir(out))}")


@unittest.skipUnless(ORC and VHDX, "set ORCA_IT_ORC and ORCA_IT_VHDX to run this (elevated)")
class TestCollectFromAVhd(unittest.TestCase):
    def test_it_attaches_picks_a_volume_and_records_which(self):
        with tempfile.TemporaryDirectory() as d:
            out = os.path.join(d, "results")
            code = collect.run_collect(orc=ORC, out=out, offline=VHDX,
                                       temp=os.path.join(d, "temp"),
                                       orc_args=ARGS)
            record = read_record(out)
            self.assertEqual(record["target"]["kind"], "vhd")
            self.assertRegex(record["target"]["drive_letter"], r"^[A-Za-z]:$")
            self.assertIn("PhysicalDrive", record["target"]["physical_path"])
            self.assertEqual(code, 0, f"ORC exited {code}; see {out}")
            self.assertTrue(archives(out), f"no archive in {out}: {sorted(os.listdir(out))}")

    def test_the_disk_is_detached_afterwards(self):
        from orca import vhd

        with tempfile.TemporaryDirectory() as d:
            collect.run_collect(orc=ORC, out=os.path.join(d, "results"), offline=VHDX,
                                temp=os.path.join(d, "temp"), orc_args=ARGS)

        # Attaching again must succeed and report that orca is the one attaching it: if the
        # previous run had leaked the attachment, this would come back False.
        with vhd.attached(VHDX) as disk:
            self.assertTrue(disk.attached_by_orca,
                            "the previous collect left the disk attached")


if __name__ == "__main__":
    unittest.main()
