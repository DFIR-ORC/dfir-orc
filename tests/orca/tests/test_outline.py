import json
import os
import tempfile
import unittest
from orca import outline

# Shaped like a real Outline, trimmed to the parts the row promotes. Every value is invented:
# an Outline describes the machine it was collected on - name, account, CPU, memory, install and
# shutdown times - so a fixture copied from a real run would commit that machine's fingerprint.
OUTLINE = {
    "dfir-orc": {"outline": {
        "id": "{71E4A690-0F9D-4CC5-9778-5CDAD8BEF933}",
        "computer_name": "TESTHOST",
        "system_type": "WorkStation",
        "version": "v10.3.4-62-g48af8529a-x64-xp",
        "timestamp": "20260921_172528",
        "start": "2026-09-21T17:25:28Z",
        "output": "d:\\temp\\Archive\\",
        "temp": "d:\\temp\\Temp\\",
        "system": {
            "architecture": "x64",
            "hypervisor": "none",
            "operating_system": {"description": "Microsoft Windows 10 Enterprise", "version": "10.0",
                                 "locale": "en-US", "install_date": "2026-01-01T00:00:00Z",
                                 "shutdown_time": "2026-09-20T12:00:00Z",
                                 "tag": ["OSBuild#19041", "Windows10", "x64"]},
            "cpu": [{"manufacturer": "ExampleCorp", "name": "Example CPU 8-Core",
                     "physical_processors": 8, "logical_processors": 16}],
            "physical_memory": {"physical": 34359738368, "available_physical": 8589934592,
                                "current_load": 75},
            "mounted_volumes": [{"path": "D:\\"}, {"path": "C:\\"}],
            "physical_drives": [{"id": 0}, {"id": 1}],
            "network": {"adapter": [{"name": "a"}, {"name": "b"}, {"name": "c"}]},
        },
        "process": {"command_line": " WolfLauncher /overwrite", "binary": "DFIR-ORC.exe",
                    "syswow64": False,
                    "user": {"username": "TESTHOST\\admin", "SID": "S-1-5-21-1", "elevated": True}},
        "profile_list": {"profile": [{"p": 1}, {"p": 2}, {"p": 3}, {"p": 4}]},
        "archives": [{"keyword": "ORC_Offline", "commands": [{"name": "capsule"}]}],
        "capsule": {"sha1": "56864BE802F4618ADACFD5F686AF3245C9990E8F"},
    }}
}


class OutlineCase(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)

    def write(self, name, payload):
        path = os.path.join(self.dir.name, name)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as f:
            json.dump(payload, f)
        return path


class TestRow(OutlineCase):
    def row(self):
        return outline.outline_row(OUTLINE["dfir-orc"]["outline"], "C:\\runs\\a\\Outline.json")

    def test_identity_and_build(self):
        row = self.row()
        self.assertEqual(row["computer_name"], "TESTHOST")
        self.assertEqual(row["version"], "v10.3.4-62-g48af8529a-x64-xp")
        self.assertEqual(row["run_id"], "{71E4A690-0F9D-4CC5-9778-5CDAD8BEF933}")

    def test_nested_scalars_are_promoted(self):
        row = self.row()
        self.assertEqual(row["os_description"], "Microsoft Windows 10 Enterprise")
        self.assertEqual(row["cpu_name"], "Example CPU 8-Core")
        self.assertEqual(row["memory_physical"], 34359738368)
        self.assertEqual(row["user_name"], "TESTHOST\\admin")
        self.assertEqual(row["capsule_sha1"], "56864BE802F4618ADACFD5F686AF3245C9990E8F")

    def test_tags_are_joined_for_like_queries(self):
        self.assertEqual(self.row()["os_tags"], "OSBuild#19041,Windows10,x64")

    def test_repeating_structures_become_counts(self):
        row = self.row()
        self.assertEqual((row["volume_count"], row["drive_count"], row["adapter_count"]),
                         (2, 2, 3))
        self.assertEqual((row["profile_count"], row["archive_count"]), (4, 1))

    def test_the_whole_document_is_kept(self):
        # The escape hatch: anything not promoted stays reachable with json_extract.
        parsed = json.loads(self.row()["json"])
        self.assertEqual(parsed["archives"][0]["keyword"], "ORC_Offline")

    def test_a_sparse_outline_does_not_raise(self):
        # Outlines differ between ORC versions; a missing branch must not break the row.
        row = outline.outline_row({"computer_name": "X"}, "x.json")
        self.assertEqual(row["computer_name"], "X")
        self.assertEqual(row["os_description"], "")
        self.assertEqual(row["volume_count"], 0)


class TestReading(OutlineCase):
    def test_reads_an_outline(self):
        path = self.write("Outline.json", OUTLINE)
        self.assertEqual(outline.read_outline(path)["computer_name"], "TESTHOST")

    def test_an_outcome_is_not_an_outline(self):
        path = self.write("Outcome.json", {"dfir-orc": {"outcome": {"computer_name": "H"}}})
        self.assertIsNone(outline.read_outline(path))

    def test_unreadable_json_is_skipped_not_fatal(self):
        path = os.path.join(self.dir.name, "broken.json")
        with open(path, "w", encoding="utf-8") as f:
            f.write("{not json")
        self.assertIsNone(outline.read_outline(path))
        self.assertEqual(outline.collect_rows([path]), [])

    def test_collect_rows_skips_non_outlines(self):
        good = self.write("Outline.json", OUTLINE)
        other = self.write("Outcome.json", {"dfir-orc": {"outcome": {}}})
        self.assertEqual(len(outline.collect_rows([good, other])), 1)


class TestOutlineFiles(OutlineCase):
    def test_lists_every_captured_outline(self):
        run = os.path.join(self.dir.name, "run")
        self.write(os.path.join("run", "outlines", "A_Outline.json"), OUTLINE)
        self.write(os.path.join("run", "outlines", "B_Outline.json"), OUTLINE)
        # A restarted collection leaves one Outline per attempt.
        self.assertEqual([os.path.basename(p) for p in outline.outline_files(run)],
                         ["A_Outline.json", "B_Outline.json"])

    def test_a_run_without_outlines(self):
        self.assertEqual(outline.outline_files(self.dir.name), [])


if __name__ == "__main__":
    unittest.main()
