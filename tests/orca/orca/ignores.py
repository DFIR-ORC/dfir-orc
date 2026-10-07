"""The ignore list: the names a report is told not to raise.

A collection has findings you already know about and have decided about - a command that
always fails on this fleet, an output ORC always writes empty - and a report that repeats them
every time trains you to skim past the ones that matter. The list says which names to pass
over; it never changes what was collected, and the report still says how many it passed over.
"""
import fnmatch
import json
import os


def load(path: str | None) -> list[str]:
    """The names in an ignore file: one JSON list, each entry a name or an fnmatch pattern.

    No file means no pattern, so every caller can ask unconditionally. A file that is there but
    unreadable is an error naming it: a mistyped ignore file that silently ignored nothing
    would make a report look clean for the wrong reason.
    """
    if not path:
        return []

    try:
        with open(path, encoding="utf-8") as source:
            data = json.load(source)
    except OSError as error:
        raise RuntimeError(f"cannot read the ignore file '{path}': {error}") from error
    except ValueError as error:
        raise RuntimeError(f"'{path}' is not valid JSON: {error}") from error

    if not isinstance(data, list):
        raise RuntimeError(f"'{path}' must hold a JSON list of names, got "
                           f"{type(data).__name__}")

    for entry in data:
        if not isinstance(entry, str):
            raise RuntimeError(f"'{path}' holds {entry!r}, which is not a name")

    return data


def patterns(values: list[str] | None) -> list[str]:
    """The names given on the command line: '--ignore' repeated, each one a comma-separated
    list, so 'Azure*,PersistenceSniper' and two flags say the same thing. Blanks are dropped -
    a trailing comma must not become a pattern."""
    found = []
    for value in values or []:
        found += [name.strip() for name in value.split(",") if name.strip()]
    return found


def matches(name: str, patterns: list[str]) -> bool:
    """True when 'name' is one the patterns cover. '*' and '?' work; case does not matter.

    Case-insensitively on every platform rather than fnmatch's own rule, which follows the
    host: ORC's names mix cases - the 'Autoruns' command writes an 'autoruns' table - and an
    ignore file should behave the same wherever it is read.
    """
    folded = (name or "").lower()
    return any(fnmatch.fnmatchcase(folded, pattern.lower()) for pattern in patterns)


def command_names(command_set: str | None, command: str | None) -> list[str]:
    """The ways of naming one command of one run, as an ignore pattern may name it.

    The command on its own, its key (the command set, as '/key=' names it) to pass over every
    command of that key, and 'set/command' so a name that means one thing in one key can be
    ignored without silencing it everywhere.
    """
    names = [command or ""]
    if command_set:
        names += [command_set, f"{command_set}/{command or ''}"]
    return names


def matches_any(names: list[str], patterns: list[str]) -> bool:
    """True when any of the ways of naming one finding is covered.

    A failed command is named either on its own or as 'set/command', so a name that means one
    thing in one command set can be ignored without silencing it everywhere.
    """
    return any(matches(name, patterns) for name in names)
