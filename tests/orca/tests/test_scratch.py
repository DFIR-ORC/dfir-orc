import os
import tempfile
import time
import unittest
from unittest import mock
from orca import scratch


class ScratchCase(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.addCleanup(scratch.cleanup)
        self.addCleanup(scratch.use, None)
        patcher = mock.patch.object(scratch.tempfile, "gettempdir", lambda: self.temp.name)
        patcher.start()
        self.addCleanup(patcher.stop)
        scratch.use(None)  # no base carried over from another test
        scratch.cleanup()  # nor a root


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


class TestUse(ScratchCase):
    """'--work-dir': the whole scratch tree moves, so every intermediate follows it - the CMS
    output and the journal replay, not only the archive a caller happened to name."""

    def setUp(self):
        super().setUp()
        self.work = tempfile.TemporaryDirectory()
        self.addCleanup(self.work.cleanup)

    def test_the_root_moves_under_the_given_directory(self):
        scratch.use(self.work.name)
        self.assertEqual(scratch.root(),
                         os.path.join(self.work.name, "orca", str(os.getpid())))

    def test_every_scratch_directory_follows_it(self):
        scratch.use(self.work.name)
        self.assertEqual(os.path.dirname(scratch.directory("cms_")), scratch.root())

    def test_nothing_but_the_index_is_left_on_the_temp_dir(self):
        # The point of the flag: nothing of any size lands on the temp volume. The index
        # still does, deliberately - a few hundred bytes, and a fixed rendezvous is what lets
        # a later run find the scratch of a run that was killed under --work-dir.
        scratch.use(self.work.name)
        scratch.directory("cms_")

        base = os.path.join(self.temp.name, "orca")
        self.assertEqual(os.listdir(base), [os.path.basename(scratch._index)])

    def test_cleanup_leaves_the_directory_the_user_named(self):
        scratch.use(self.work.name)
        made = scratch.directory("cms_")
        with open(os.path.join(made, "big.7zs"), "wb") as f:
            f.write(b"x")

        scratch.cleanup()

        self.assertFalse(os.path.exists(made))
        self.assertTrue(os.path.isdir(self.work.name))

    def test_none_means_the_temp_dir(self):
        scratch.use(None)
        self.assertEqual(os.path.dirname(scratch.root()),
                         os.path.join(self.temp.name, "orca"))

    def test_a_root_already_taken_elsewhere_is_dropped_and_re_taken(self):
        # Whether something touched scratch first is not the caller's to reason about: the
        # flag has to hold either way, or a huge intermediate quietly goes to the wrong volume.
        stale = scratch.root()

        scratch.use(self.work.name)

        self.assertFalse(os.path.exists(stale))
        self.assertEqual(os.path.dirname(scratch.root()),
                         os.path.join(self.work.name, "orca"))

    def test_setting_the_same_base_again_keeps_the_root(self):
        scratch.use(self.work.name)
        root = scratch.root()

        scratch.use(self.work.name)

        self.assertEqual(scratch.root(), root)
        self.assertTrue(os.path.isdir(root))


class IndexCase(ScratchCase):
    """Removal is driven by an index orca writes as it creates things, so that it can only
    ever remove its own working files - it reaches into directories the user chose and keeps."""

    def plant(self, root, names=(), magic=scratch.INDEX_MAGIC, age=None, make=True):
        """An index file naming 'root', as a killed run would have left behind."""
        base = scratch.index_dir()
        os.makedirs(base, exist_ok=True)
        if make:
            for name in names:
                os.makedirs(os.path.join(root, name), exist_ok=True)
            os.makedirs(root, exist_ok=True)

        path = os.path.join(base, "9999-planted" + scratch.INDEX_SUFFIX)
        with open(path, "w", encoding="utf-8") as f:
            f.write("\n".join([magic, root, *names]) + "\n")

        if age is not None:
            old = time.time() - age
            os.utime(path, (old, old))
        return path


class TestIndexContents(IndexCase):
    def test_it_names_the_root_and_every_directory_made(self):
        made = [os.path.basename(scratch.directory("cms_")),
                os.path.basename(scratch.directory("stage_"))]

        root, names = scratch.read_index(scratch._index)

        self.assertEqual(root, scratch.root())
        self.assertEqual(names, made)

    def test_it_lives_in_the_temp_dir_even_when_the_scratch_moved(self):
        # The fixed rendezvous: a run killed under --work-dir is cleaned by a later run that
        # knows nothing about that directory.
        work = tempfile.TemporaryDirectory()
        self.addCleanup(work.cleanup)
        scratch.use(work.name)

        scratch.directory("cms_")

        self.assertEqual(os.path.dirname(scratch._index),
                         os.path.join(self.temp.name, "orca"))
        self.assertEqual(scratch.read_index(scratch._index)[0], scratch.root())

    def test_a_directory_is_recorded_only_once_it_exists(self):
        made = scratch.directory("cms_")
        _, names = scratch.read_index(scratch._index)
        for name in names:
            self.assertTrue(os.path.isdir(os.path.join(scratch.root(), name)))
        self.assertIn(os.path.basename(made), names)


class TestReadIndex(IndexCase):
    def test_a_foreign_file_is_not_an_index(self):
        path = self.plant(os.path.join(self.temp.name, "orca", "4242"), magic="something else")
        self.assertIsNone(scratch.read_index(path))

    def test_a_root_that_is_not_shaped_like_ours_is_refused(self):
        # The second lock: an index is a plain file in a shared temp dir, and it drives rmtree.
        for root in (os.path.join(self.temp.name, "precious"),
                     os.path.join(self.temp.name, "orca", "notapid"),
                     "/"):
            path = self.plant(root, make=False)
            self.assertIsNone(scratch.read_index(path), root)

    def test_a_missing_index_is_not_an_error(self):
        self.assertIsNone(scratch.read_index(os.path.join(self.temp.name, "nope.index")))


class TestSweep(IndexCase):
    def test_a_stale_index_takes_its_scratch_with_it(self):
        root = os.path.join(self.temp.name, "orca", "4242")
        path = self.plant(root, ["cms_ab"], age=scratch.STALE_SECONDS + 60)

        scratch.root()

        self.assertFalse(os.path.exists(root))
        self.assertFalse(os.path.exists(path))

    def test_a_fresh_index_is_left_alone(self):
        root = os.path.join(self.temp.name, "orca", "4243")
        path = self.plant(root, ["cms_ab"], age=60)

        scratch.root()

        self.assertTrue(os.path.isdir(root))
        self.assertTrue(os.path.exists(path))

    def test_a_directory_no_index_names_is_never_touched(self):
        # Even named exactly like a scratch root: the name is not what decides any more.
        base = os.path.join(self.temp.name, "orca")
        orphan = os.path.join(base, "4242")
        os.makedirs(orphan)
        old = time.time() - scratch.STALE_SECONDS - 60
        os.utime(orphan, (old, old))

        scratch.root()

        self.assertTrue(os.path.isdir(orphan))

    def test_the_users_own_files_are_never_touched(self):
        base = os.path.join(self.temp.name, "orca")
        os.makedirs(os.path.join(base, "my-notes"))
        note = os.path.join(base, "keep.txt")
        with open(note, "wb") as f:
            f.write(b"x")
        old = time.time() - scratch.STALE_SECONDS - 60
        os.utime(os.path.join(base, "my-notes"), (old, old))
        os.utime(note, (old, old))

        scratch.root()

        self.assertTrue(os.path.isdir(os.path.join(base, "my-notes")))
        self.assertTrue(os.path.exists(note))

    def test_an_unparsable_stale_index_loses_only_itself(self):
        root = os.path.join(self.temp.name, "orca", "4242")
        path = self.plant(root, ["cms_ab"], magic="corrupt",
                          age=scratch.STALE_SECONDS + 60)

        scratch.root()

        self.assertFalse(os.path.exists(path))
        self.assertTrue(os.path.isdir(root), "it no longer says what it owns, so it gets no say")

    def test_this_run_does_not_sweep_its_own_index(self):
        root = scratch.root()
        scratch.sweep()
        self.assertTrue(os.path.isdir(root))
        self.assertTrue(os.path.exists(scratch._index))


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
