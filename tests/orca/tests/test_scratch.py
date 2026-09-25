import os
import tempfile
import unittest
from unittest import mock
from orca import scratch


class ScratchCase(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.addCleanup(scratch.cleanup)
        patcher = mock.patch.object(scratch.tempfile, "gettempdir", lambda: self.temp.name)
        patcher.start()
        self.addCleanup(patcher.stop)
        scratch.cleanup()  # no root carried over from another test


class TestRoot(ScratchCase):
    def test_is_orca_pid_under_temp(self):
        self.assertEqual(scratch.root(),
                         os.path.join(self.temp.name, "orca", str(os.getpid())))

    def test_is_created_on_demand_and_reused(self):
        self.assertEqual(scratch.root(), scratch.root())
        self.assertTrue(os.path.isdir(scratch.root()))


class TestDirectory(ScratchCase):
    def test_lands_inside_the_root(self):
        made = scratch.directory("stage_")
        self.assertTrue(os.path.isdir(made))
        self.assertEqual(os.path.dirname(made), scratch.root())
        self.assertTrue(os.path.basename(made).startswith("stage_"))

    def test_two_calls_do_not_collide(self):
        self.assertNotEqual(scratch.directory("stage_"), scratch.directory("stage_"))


class TestCleanup(ScratchCase):
    def test_removes_everything_the_process_wrote(self):
        made = scratch.directory("stage_")
        with open(os.path.join(made, "big.7z"), "wb") as f:
            f.write(b"x")

        root = scratch.root()
        scratch.cleanup()
        self.assertFalse(os.path.exists(root))

    def test_is_safe_to_call_twice(self):
        scratch.root()
        scratch.cleanup()
        scratch.cleanup()

    def test_a_later_call_starts_a_fresh_root(self):
        scratch.root()
        scratch.cleanup()
        self.assertTrue(os.path.isdir(scratch.root()))


if __name__ == "__main__":
    unittest.main()
