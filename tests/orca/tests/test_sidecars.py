"""Sealed files and what belongs to what (dataset layout spec, section 4.4)."""
import io
import json
import os
import tempfile
import unittest
from contextlib import redirect_stdout
from unittest import mock

from orca import cache, ingest, layout, sidecars


def write_json(path: str, kind: str, body: dict) -> str:
    with open(path, "w", encoding="utf-8") as f:
        json.dump({"dfir-orc": {kind: body}}, f)
    return path


def outcome(run_id="{A}", computer="a.lab", timestamp="20260101_000000",
            archives=("ORC_A_General.7z.p7b",), log="ORC_A_20260101_000000.log",
            outline="ORC_A_20260101_000000_Outline.json", console="") -> dict:
    body = {"id": run_id, "computer_name": computer, "timestamp": timestamp,
            "command_set": [{"name": "ORC_General", "archive": {"name": name}}
                            for name in archives]}
    for key, name in (("log", log), ("outline", outline), ("console", console)):
        if name:
            body[key] = {"file": {"name": name}}
    return body


def touch(path: str, data: bytes = b"x") -> str:
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)
    return path


class Case(unittest.TestCase):
    def setUp(self):
        self.root = tempfile.TemporaryDirectory()
        self.addCleanup(self.root.cleanup)
        self.dir = os.path.join(self.root.name, "collect")
        self.out = os.path.join(self.root.name, "out")
        os.makedirs(self.dir)
        os.makedirs(self.out)

    def group(self, paths=None, from_sealed=()) -> sidecars.Grouped:
        paths = sidecars.plain_sidecars(self.dir) if paths is None else paths
        return sidecars.group(self.dir, sidecars.Resolved(list(paths), list(from_sealed)))


class TestResolve(Case):
    """Rule 1 and rule 5: a plain copy already there is used, and nothing is decrypted."""

    def seal(self, name: str) -> str:
        return touch(os.path.join(self.dir, name + ".7z.p7b"))

    def resolve(self, **kwargs) -> sidecars.Resolved:
        with redirect_stdout(io.StringIO()):
            return sidecars.resolve(self.dir, self.out, "7z", **kwargs)

    def test_a_plain_copy_in_a_dataset_under_the_output_is_used_without_a_key(self):
        self.seal("ORC_A_Outcome.json")
        dataset = os.path.join(self.out, "ORC_A")
        os.makedirs(layout.orca_dir(dataset))
        plain = write_json(os.path.join(dataset, "ORC_A_Outcome.json"), "outcome", outcome())

        with mock.patch.object(ingest, "unseal_one") as unseal:
            resolved = self.resolve()

        unseal.assert_not_called()
        self.assertEqual(resolved.from_sealed, [plain])

    def test_a_plain_copy_beside_the_sealed_one_is_used(self):
        self.seal("ORC_A_Outcome.json")
        plain = write_json(os.path.join(self.dir, "ORC_A_Outcome.json"), "outcome", outcome())

        with mock.patch.object(ingest, "unseal_one") as unseal:
            resolved = self.resolve()

        unseal.assert_not_called()
        self.assertEqual(resolved.paths, [plain])

    def test_otherwise_it_is_decrypted_into_scratch_not_beside_the_collection(self):
        self.seal("ORC_A_Outcome.json")

        def unseal(sealed, target, *args):
            return write_json(target, "outcome", outcome())

        with mock.patch.object(ingest, "unseal_one", side_effect=unseal):
            resolved = self.resolve()

        self.assertEqual(len(resolved.from_sealed), 1)
        self.assertNotEqual(os.path.dirname(resolved.from_sealed[0]), self.dir)
        self.assertEqual(sorted(os.listdir(self.dir)), ["ORC_A_Outcome.json.7z.p7b"])

    def test_refresh_decrypts_even_when_a_copy_is_there(self):
        self.seal("ORC_A_Outcome.json")
        write_json(os.path.join(self.dir, "ORC_A_Outcome.json"), "outcome", outcome())

        with mock.patch.object(ingest, "unseal_one",
                               side_effect=lambda s, t, *a: write_json(t, "outcome",
                                                                       outcome())) as unseal:
            self.resolve(refresh=True)

        unseal.assert_called_once()

    def test_one_that_cannot_be_decrypted_is_named_and_the_rest_resolve(self):
        self.seal("ORC_A_Outcome.json")
        write_json(os.path.join(self.dir, "ORC_B_Outcome.json"), "outcome",
                   outcome("{B}", "b.lab"))

        printed = io.StringIO()
        with mock.patch.object(ingest, "unseal_one", side_effect=RuntimeError("no key")), \
                redirect_stdout(printed):
            resolved = sidecars.resolve(self.dir, self.out, "7z")

        self.assertIn("could not unseal 'ORC_A_Outcome.json.7z.p7b'", printed.getvalue())
        self.assertEqual([os.path.basename(p) for p in resolved.paths], ["ORC_B_Outcome.json"])


class TestGroup(Case):
    """Rule 2 and 3: what belongs to what is read from the files, never from their names."""

    def test_an_outcome_brings_its_archives_log_and_outline_by_what_it_names(self):
        touch(os.path.join(self.dir, "ORC_A_General.7z.p7b"))
        write_json(os.path.join(self.dir, "run-a.json"), "outcome", outcome())
        log = touch(os.path.join(self.dir, "ORC_A_20260101_000000.log"))
        named = write_json(os.path.join(self.dir, "ORC_A_20260101_000000_Outline.json"),
                           "outline", {"id": "{OTHER}"})

        run = self.group().collections[0]

        self.assertEqual([os.path.basename(a) for a in run.archives], ["ORC_A_General.7z.p7b"])
        self.assertEqual(run.sidecars, (log,))
        self.assertIn(named, run.outlines)
        self.assertEqual(run.name, "ORC_A_20260101_000000")

    def test_an_outline_belongs_to_the_run_with_its_id_whatever_its_name(self):
        write_json(os.path.join(self.dir, "o.json"), "outcome", outcome(outline=""))
        renamed = write_json(os.path.join(self.dir, "renamed.json"), "outline", {"id": "{A}"})

        run = self.group().collections[0]

        self.assertEqual(run.outlines, (renamed,))

    def test_another_hosts_log_is_not_attached_by_a_shared_prefix(self):
        write_json(os.path.join(self.dir, "a.json"), "outcome", outcome(log="ORC_A.log"))
        touch(os.path.join(self.dir, "ORC_A.log"))
        touch(os.path.join(self.dir, "ORC_A.log.old.log"))

        run = self.group().collections[0]

        self.assertEqual([os.path.basename(p) for p in run.sidecars], ["ORC_A.log"])

    def test_two_copies_of_one_outcome_are_one_run(self):
        write_json(os.path.join(self.dir, "one.json"), "outcome", outcome())
        write_json(os.path.join(self.dir, "two.json"), "outcome", outcome())

        named = [c for c in self.group().collections if c.outcome_path]

        self.assertEqual(len(named), 1)

    def test_an_outline_with_no_outcome_is_a_run_of_its_own_with_its_archives(self):
        touch(os.path.join(self.dir, "ORC_L_General.7z.p7b"))
        outline = write_json(os.path.join(self.dir, "ORC_L_Outline.json"), "outline", {
            "id": "{L}", "computer_name": "l.lab", "timestamp": "20260102_000000",
            "archives": [{"keyword": "ORC_General", "file": "ORC_L_General.7z"}]})

        runs = self.group().collections

        self.assertEqual(len(runs), 1)
        run = runs[0]
        self.assertIsNone(run.outcome_path)
        self.assertEqual((run.computer_name, run.run_id, run.name),
                         ("l.lab", "{L}", "l.lab_20260102_000000"))
        self.assertEqual(run.outlines, (outline,))
        # The Outline names '<x>.7z'; what arrived is the sealed one.
        self.assertEqual([os.path.basename(a) for a in run.archives], ["ORC_L_General.7z.p7b"])

    def test_an_archive_nothing_names_is_an_unnamed_collection(self):
        write_json(os.path.join(self.dir, "a.json"), "outcome", outcome(archives=()))
        touch(os.path.join(self.dir, "Stray.7z"))

        runs = self.group().collections

        self.assertEqual([os.path.basename(a) for a in runs[-1].archives], ["Stray.7z"])
        self.assertIsNone(runs[-1].outcome_path)
        self.assertEqual(runs[-1].name, "")

    def test_an_archive_the_outcome_names_and_is_missing_is_kept_by_name(self):
        write_json(os.path.join(self.dir, "a.json"), "outcome", outcome())

        run = self.group().collections[0]

        self.assertEqual(run.archives, [os.path.join(self.dir, "ORC_A_General.7z.p7b")])
        self.assertFalse(os.path.exists(run.archives[0]))

    def test_a_sealed_file_no_run_claims_is_unattached(self):
        write_json(os.path.join(self.dir, "a.json"), "outcome", outcome())
        stray = touch(os.path.join(self.root.name, "scratch", "Capsule.txt"))

        grouped = self.group(sidecars.plain_sidecars(self.dir) + [stray], [stray])

        self.assertEqual(grouped.unattached, [stray])

    def test_a_run_without_a_log_name_is_named_by_host_and_time(self):
        write_json(os.path.join(self.dir, "a.json"), "outcome", outcome(log=""))

        self.assertEqual(self.group().collections[0].name, "a.lab_20260101_000000")


class TestPlainArchives(Case):
    """Rule 4: the run is built from the archives that are there."""

    def test_a_missing_archive_is_left_out_with_its_reason(self):
        missing = os.path.join(self.dir, "Gone.7z.p7b")
        with redirect_stdout(io.StringIO()):
            archives, unread, undecrypted = ingest.plain_archives([missing], self.out)

        self.assertEqual((archives, unread, undecrypted),
                         ([], [("Gone.7z.p7b", "archive missing")], []))

    def test_one_that_will_not_decrypt_is_left_out_and_said(self):
        sealed = touch(os.path.join(self.dir, "Locked.7z.p7b"))
        with mock.patch.object(ingest, "decrypt_archive", side_effect=RuntimeError("no key")), \
                redirect_stdout(io.StringIO()):
            archives, unread, undecrypted = ingest.plain_archives([sealed], self.out)

        self.assertEqual(archives, [])
        self.assertEqual(undecrypted, ["Locked.7z.p7b"])
        self.assertIn("could not decrypt", unread[0][1])

    def test_a_plain_copy_beside_the_sealed_one_is_used_as_it_is(self):
        sealed = touch(os.path.join(self.dir, "X.7z.p7b"))
        beside = touch(os.path.join(self.dir, "X.7z"), b"7z\xbc\xaf\x27\x1c")
        with mock.patch.object(ingest, "decrypt_archive") as decrypt:
            archives, _unread, _undecrypted = ingest.plain_archives([sealed], self.out)

        decrypt.assert_not_called()
        self.assertEqual(archives, [beside])


class TestMissingArchiveKey(Case):
    def test_a_missing_archive_is_keyed_by_name_and_its_arrival_changes_the_key(self):
        archive = os.path.join(self.dir, "Late.7z")
        before = cache.key_for(self.dir, cache.ingest_options(), archives=[archive])
        touch(archive)
        self.assertNotEqual(before, cache.key_for(self.dir, cache.ingest_options(),
                                                  archives=[archive]))


if __name__ == "__main__":
    unittest.main()
