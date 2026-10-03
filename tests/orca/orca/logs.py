"""Reading an ORC log: what level each line carried, what went wrong, and what was around it.

ORC writes every line through one spdlog pattern, '%^%Y-%m-%dT%T.%eZ [%L] %v%$', so the level
is a tag at a fixed place rather than something to be guessed from the words in the message.
That distinction is the point of this module: ORC logs "Failed to ..." at Debug all over the
place while reporting the failure once at Error, so counting lines that contain "error" or
"failed" both over- and under-states what went wrong.
"""
import re
from collections import deque

LEVELS = {"T": "trace", "D": "debug", "I": "info", "W": "warning", "E": "error", "C": "critical"}

TAGS = {level: tag for tag, level in LEVELS.items()}

LINE_RE = re.compile(r"^(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d+Z) \[([TDIWEC])\] (.*)$")

# The delimiters BacktraceSink::dump_to writes around the replayed ring, at Info level.
BACKTRACE_START = "--- BACKTRACE START ---"
BACKTRACE_END = "--- BACKTRACE END ---"

# How many lines either side of a message '--backtrace' prints when no count is given.
DEFAULT_CONTEXT = 5

# What a log with no ORC line in it is counted by. Wrong in both directions - ORC logs
# "Failed to ..." at Debug, and reports plenty of errors without any of these words - but a
# log nothing could parse reporting a clean 0 would read as reassurance.
WORDS_RE = re.compile(r"error|failed|exception", re.IGNORECASE)


def parse_line(line: str):
    """One log line as (timestamp, level, message), or None when it is not an ORC line."""
    match = LINE_RE.match(line.rstrip("\r\n"))
    if not match:
        return None

    timestamp, tag, message = match.groups()
    return timestamp, LEVELS[tag], message


def kept_context(entries, own) -> list:
    """Context lines worth keeping: not the message itself, and not a line repeated back to
    back. A block replays the line logged just before it, so the message above this one lands
    twice in a row; ORC also logs the same line twice of its own accord here and there."""
    kept = []
    for entry in entries:
        if entry != own and (not kept or kept[-1] != entry):
            kept.append(entry)

    return kept


def scan(lines, context: int | None = None) -> dict:
    """What a log carried: the count of each level, and every warning, error and critical with
    the lines around it.

    A backtrace block is a replay of lines already logged, with their original levels and
    timestamps, so counting it would report every error inside the ring a second time: the
    block is counted nowhere, and nothing in it is read as a message.

    It is part of the context, though. The file facility sits at Info on a real collection and
    dump_to drops every sink to trace for the replay, so the ring is the only place the debug
    lines that say WHY an error happened are written at all - on one real log, 216 of its 217
    debug lines. What is dropped is a replay of the message itself: ORC dumps the ring before
    the NEXT error and this one is in it by then, so its own line would come back as its own
    context.

    'context' is how many lines either side of a message to keep, None for none of them. Five
    lines either side of every message, over a fleet of logs, is the bulky part of a scan, and
    only '--backtrace' prints them.

    The lines around the message in the log, and not ORC's ring on its own, by decision: dump_to
    CLEARS the ring at every dump, so with the trigger at Error it holds only what was logged
    since the previous error, and it is not the context of anything. On one real log, 16 of its
    32 blocks are two lines whose first is the previous error repeated, and the three 64-line
    blocks are whatever happened to be logged during a quiet stretch rather than anything to do
    with the error below them. Printing the ring answered no question; the block's lines are kept
    as part of the surrounding context instead, for the reason above.
    """
    counts = {"warning": 0, "error": 0, "critical": 0}
    messages = []
    in_block = False
    recent = deque(maxlen=context or 0)  # the lines before, as far back as asked for
    pending = []  # the messages still short of their lines after
    parsed_lines = 0
    word_matches = 0

    for line in lines:
        parsed = parse_line(line)
        if not parsed:
            # Counted as we go rather than by keeping the lines: a log nothing can parse is
            # exactly the one that may be arbitrarily large.
            word_matches += bool(WORDS_RE.search(line))
            continue

        parsed_lines += 1

        timestamp, level, message = parsed
        if message == BACKTRACE_START:
            in_block = True
            continue

        if message == BACKTRACE_END:
            in_block = False
            continue

        if context:
            for entry in pending:
                entry["after"] = kept_context(
                    entry["after"] + [parsed],
                    (entry["time"], entry["level"], entry["message"]))

            pending = [entry for entry in pending if len(entry["after"]) < context]

        if not in_block and level in counts:
            counts[level] += 1
            entry = {"time": timestamp, "level": level, "message": message,
                     "before": kept_context(recent, (timestamp, level, message)),
                     "after": []}
            messages.append(entry)
            # Registered after its own line was handed to the messages before it, so a message
            # is never in its own context and is always in theirs.
            if context:
                pending.append(entry)

        recent.append(parsed)

    if not parsed_lines:
        return {"counts": {"warning": 0, "error": word_matches, "critical": 0},
                "messages": [], "exact": False}

    return {"counts": counts, "messages": messages, "exact": True}
