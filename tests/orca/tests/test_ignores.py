"""The ignore list: names a report is told not to raise, and how they are matched."""
import json
import os
import tempfile
import unittest

from orca import ignores


class TestLoad(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)

    def write(self, payload) -> str:
        path = os.path.join(self.dir.name, "ignore.json")
        with open(path, "w", encoding="utf-8") as f:
            if isinstance(payload, str):
                f.write(payload)
            else:
                json.dump(payload, f)
        return path

    def test_a_list_of_names_is_read_in_order(self):
        path = self.write(["Azure_*", "Autoruns"])

        self.assertEqual(ignores.load(path), ["Azure_*", "Autoruns"])

    def test_no_file_means_no_pattern(self):
        self.assertEqual(ignores.load(None), [])

    def test_an_empty_list_is_allowed(self):
        self.assertEqual(ignores.load(self.write([])), [])

    def test_a_file_that_is_not_there_is_an_error_naming_it(self):
        with self.assertRaises(RuntimeError) as caught:
            ignores.load(os.path.join(self.dir.name, "absent.json"))

        self.assertIn("absent.json", str(caught.exception))

    def test_malformed_json_is_an_error_naming_the_file(self):
        path = self.write("{nope")

        with self.assertRaises(RuntimeError) as caught:
            ignores.load(path)

        self.assertIn("ignore.json", str(caught.exception))

    def test_an_object_instead_of_a_list_says_what_was_expected(self):
        # Worth a clear error: the obvious wrong guess at the format.
        path = self.write({"failed_commands": ["Autoruns"]})

        with self.assertRaises(RuntimeError) as caught:
            ignores.load(path)

        self.assertIn("list of names", str(caught.exception))

    def test_an_entry_that_is_not_a_name_is_an_error(self):
        path = self.write(["Autoruns", 7])

        with self.assertRaises(RuntimeError) as caught:
            ignores.load(path)

        self.assertIn("7", str(caught.exception))


class TestMatches(unittest.TestCase):
    def test_a_plain_name_matches_itself(self):
        self.assertTrue(ignores.matches("Autoruns", ["Autoruns"]))
        self.assertFalse(ignores.matches("Autoruns", ["AutorunsX"]))

    def test_a_star_covers_a_family(self):
        self.assertTrue(ignores.matches("Azure_Identity", ["Azure_*"]))
        self.assertTrue(ignores.matches("Azure_ScheduledEvents", ["Azure_*"]))
        self.assertFalse(ignores.matches("GetThis_Default", ["Azure_*"]))

    def test_a_question_mark_covers_one_character(self):
        self.assertTrue(ignores.matches("processes1.log", ["processes?.log"]))
        self.assertFalse(ignores.matches("processes12.log", ["processes?.log"]))

    def test_case_does_not_matter(self):
        # ORC's own names mix cases - the 'Autoruns' command writes an 'autoruns' table - and
        # an ignore file should not have to know which is which.
        self.assertTrue(ignores.matches("autoruns", ["Autoruns"]))
        self.assertTrue(ignores.matches("Azure_Identity", ["azure_*"]))

    def test_a_name_matching_nothing_is_kept(self):
        self.assertFalse(ignores.matches("USNInfo", ["Azure_*", "Autoruns"]))

    def test_no_patterns_matches_nothing(self):
        self.assertFalse(ignores.matches("Autoruns", []))

    def test_any_of_several_names_may_match(self):
        # A failure can be given as the bare command or as 'set/command', so a name that is
        # only used in one command set can be told apart from the same name in another.
        self.assertTrue(ignores.matches_any(["Autoruns", "ORC_General/Autoruns"],
                                            ["ORC_General/*"]))
        self.assertFalse(ignores.matches_any(["Autoruns", "ORC_Little/Autoruns"],
                                            ["ORC_General/*"]))


if __name__ == "__main__":
    unittest.main()
