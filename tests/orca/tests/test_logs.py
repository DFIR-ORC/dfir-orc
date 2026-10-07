import unittest
from orca import logs


class TestScanCounts(unittest.TestCase):
    def test_levels_come_from_the_tag_not_from_the_words_in_the_message(self):
        # 'Error' in an Info message is the case the old substring heuristic got wrong: ORC
        # logs "Failed to ..." at Debug all over the place while reporting the failure once.
        scan = logs.scan([
            "2026-10-03T08:15:00.123Z [I] NTFSInfo: no Error here despite the word",
            "2026-10-03T08:15:01.000Z [W] volume is dirty",
            "2026-10-03T08:15:02.000Z [E] could not open \\\\.\\C:",
            "2026-10-03T08:15:03.000Z [C] aborting",
        ])
        self.assertEqual(scan["counts"], {"warning": 1, "error": 1, "critical": 1})

    def test_the_replayed_backtrace_does_not_inflate_the_counts(self):
        # BacktraceSink::dump_to replays the ring with each entry's ORIGINAL level and
        # timestamp, so counting the block would report every error inside it a second time.
        # The block is counted nowhere.
        scan = logs.scan([
            "2026-10-03T08:15:02.000Z [E] could not open \\\\.\\C:",
            "2026-10-03T08:15:03.000Z [I] --- BACKTRACE START ---",
            "2026-10-03T08:15:01.900Z [D] opening volume",
            "2026-10-03T08:15:02.000Z [E] could not open \\\\.\\C:",
            "2026-10-03T08:15:03.000Z [I] --- BACKTRACE END ---",
            "2026-10-03T08:15:03.000Z [C] aborting",
        ])
        self.assertEqual(scan["counts"], {"warning": 0, "error": 1, "critical": 1})


class TestScanMessages(unittest.TestCase):
    def test_every_warning_error_and_critical_is_a_message_in_the_order_logged(self):
        scan = logs.scan([
            "2026-10-03T08:15:00.000Z [I] starting",
            "2026-10-03T08:15:01.000Z [W] volume is dirty",
            "2026-10-03T08:15:02.000Z [E] could not open \\.\\C:",
            "2026-10-03T08:15:03.000Z [C] aborting",
        ])
        self.assertEqual([(m["level"], m["message"]) for m in scan["messages"]],
                         [("warning", "volume is dirty"),
                          ("error", "could not open \\.\\C:"),
                          ("critical", "aborting")])

    def test_no_context_is_kept_unless_it_is_asked_for(self):
        # Five lines either side of every message, across a fleet of logs, is the bulky part
        # of a scan, and only '--backtrace' prints them.
        scan = logs.scan([
            "2026-10-03T08:15:00.000Z [I] starting",
            "2026-10-03T08:15:02.000Z [E] could not open volume",
            "2026-10-03T08:15:03.000Z [I] carrying on",
        ])
        self.assertEqual(scan["messages"][0]["before"], [])
        self.assertEqual(scan["messages"][0]["after"], [])


class TestScanContext(unittest.TestCase):
    """The lines around a message, read from the log itself.

    Not ORC's backtrace block: dump_to clears the ring on every dump, so with a trigger of
    Error the block holds the two or three lines since the previous error, and the lines that
    actually surround this one are above it in the file.
    """

    def test_a_message_carries_the_lines_either_side_of_it(self):
        scan = logs.scan([
            "2026-10-03T08:15:00.000Z [I] first",
            "2026-10-03T08:15:01.000Z [D] second",
            "2026-10-03T08:15:02.000Z [E] could not open volume",
            "2026-10-03T08:15:03.000Z [D] third",
            "2026-10-03T08:15:04.000Z [I] fourth",
        ], context=1)
        message = scan["messages"][0]

        self.assertEqual([text for _, _, text in message["before"]], ["second"])
        self.assertEqual([text for _, _, text in message["after"]], ["third"])

    def test_the_count_bounds_each_side(self):
        scan = logs.scan([
            "2026-10-03T08:15:00.000Z [I] first",
            "2026-10-03T08:15:01.000Z [D] second",
            "2026-10-03T08:15:02.000Z [E] could not open volume",
            "2026-10-03T08:15:03.000Z [D] third",
            "2026-10-03T08:15:04.000Z [I] fourth",
        ], context=5)
        message = scan["messages"][0]

        self.assertEqual([text for _, _, text in message["before"]], ["first", "second"])
        self.assertEqual([text for _, _, text in message["after"]], ["third", "fourth"])

    def test_a_message_is_not_in_its_own_context(self):
        scan = logs.scan([
            "2026-10-03T08:15:02.000Z [E] could not open volume",
        ], context=5)
        message = scan["messages"][0]

        self.assertEqual(message["before"], [])
        self.assertEqual(message["after"], [])

    def test_the_next_message_is_part_of_the_context_of_the_one_before_it(self):
        # Two errors in a row are each other's context, which is how a cascade reads.
        scan = logs.scan([
            "2026-10-03T08:15:02.000Z [E] first",
            "2026-10-03T08:15:03.000Z [E] second",
        ], context=5)

        self.assertEqual([text for _, _, text in scan["messages"][0]["after"]], ["second"])
        self.assertEqual([text for _, _, text in scan["messages"][1]["before"]], ["first"])

    def test_the_replayed_block_is_part_of_the_context(self):
        # It has to be: the file sink sits at Info, and dump_to drops every sink to trace for
        # the replay, so on a real log the ring is the ONLY place the debug lines that say why
        # an error happened are written at all. The count bounds it, so a 64-line ring cannot
        # flood the report.
        scan = logs.scan([
            "2026-10-03T08:15:00.000Z [D] opening the volume",
            "2026-10-03T08:15:02.000Z [I] --- BACKTRACE START ---",
            "2026-10-03T08:15:01.000Z [D] CreateFile returned 32",
            "2026-10-03T08:15:02.000Z [I] --- BACKTRACE END ---",
            "2026-10-03T08:15:02.000Z [E] could not open volume",
        ], context=5)
        message = scan["messages"][0]

        self.assertEqual([text for _, _, text in message["before"]],
                         ["opening the volume", "CreateFile returned 32"])

    def test_a_replay_of_the_message_itself_is_not_its_own_context(self):
        # ORC dumps the ring before the NEXT error, and by then this one is in it, so its own
        # line would otherwise be printed again right under its own marker.
        scan = logs.scan([
            "2026-10-03T08:15:02.000Z [E] could not open volume",
            "2026-10-03T08:15:03.000Z [I] --- BACKTRACE START ---",
            "2026-10-03T08:15:02.000Z [E] could not open volume",
            "2026-10-03T08:15:02.500Z [D] CreateFile returned 32",
            "2026-10-03T08:15:03.000Z [I] --- BACKTRACE END ---",
            "2026-10-03T08:15:03.000Z [C] aborting",
        ], context=5)

        self.assertEqual([text for _, _, text in scan["messages"][0]["after"]],
                         ["CreateFile returned 32", "aborting"])

    def test_the_lines_a_block_replays_are_still_counted_only_once(self):
        scan = logs.scan([
            "2026-10-03T08:15:02.000Z [E] could not open volume",
            "2026-10-03T08:15:03.000Z [I] --- BACKTRACE START ---",
            "2026-10-03T08:15:02.000Z [E] could not open volume",
            "2026-10-03T08:15:03.000Z [I] --- BACKTRACE END ---",
            "2026-10-03T08:15:03.000Z [C] aborting",
        ], context=5)

        self.assertEqual(scan["counts"], {"warning": 0, "error": 1, "critical": 1})
        self.assertEqual(len(scan["messages"]), 2)

    def test_a_line_repeated_back_to_back_is_kept_once(self):
        # The block replays the line logged just before it, so the error above this one lands
        # twice in a row. ORC also logs the same line twice of its own accord here and there.
        scan = logs.scan([
            "2026-10-03T08:15:01.000Z [E] could not open volume",
            "2026-10-03T08:15:02.000Z [I] --- BACKTRACE START ---",
            "2026-10-03T08:15:01.000Z [E] could not open volume",
            "2026-10-03T08:15:01.500Z [D] CreateFile returned 32",
            "2026-10-03T08:15:02.000Z [I] --- BACKTRACE END ---",
            "2026-10-03T08:15:02.000Z [C] aborting",
        ], context=5)

        self.assertEqual([text for _, _, text in scan["messages"][1]["before"]],
                         ["could not open volume", "CreateFile returned 32"])

    def test_the_delimiters_are_no_part_of_it_either(self):
        scan = logs.scan([
            "2026-10-03T08:15:02.000Z [E] could not open volume",
            "2026-10-03T08:15:03.000Z [I] --- BACKTRACE START ---",
            "2026-10-03T08:15:02.000Z [E] could not open volume",
            "2026-10-03T08:15:03.000Z [I] --- BACKTRACE END ---",
            "2026-10-03T08:15:03.000Z [C] aborting",
        ], context=5)

        self.assertEqual([text for _, _, text in scan["messages"][0]["after"]], ["aborting"])


class TestScanFallback(unittest.TestCase):
    def test_a_log_with_no_orc_lines_falls_back_to_matching_the_words(self):
        # Reporting a clean 0 for a log nothing could parse would read as reassurance. The
        # old substring heuristic is wrong but not silent, so it is what an unparseable log
        # gets, flagged as inexact.
        scan = logs.scan(["Traceback (most recent call last):",
                          "RuntimeError: the tool failed to start",
                          "all quiet"])
        self.assertFalse(scan["exact"])
        self.assertEqual(scan["counts"]["error"], 1)

    def test_an_orc_log_is_exact_even_with_unparseable_lines_in_it(self):
        # A message with an embedded newline lands as a line of its own; one is not a reason
        # to throw the level tags away and start counting words.
        scan = logs.scan(["2026-10-03T08:15:02.000Z [E] could not open:",
                          "    the volume was failed and in error",
                          "2026-10-03T08:15:03.000Z [I] done"])
        self.assertTrue(scan["exact"])
        self.assertEqual(scan["counts"]["error"], 1)


if __name__ == "__main__":
    unittest.main()
