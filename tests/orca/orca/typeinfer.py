"""Column types read from the values themselves: what '--schema auto' falls back to for what
ORC's schemas do not type, and all of '--schema infer'.

Not DuckDB's sniffer: that guesses from a sample, so the same column comes out TIMESTAMP on one
fleet and VARCHAR on the next (engine.csv_to_parquet says why ingest keeps text). Here every
non-empty value of a column, across the whole fleet, is checked against a strict candidate, and
the first candidate they all fit wins. One value that fits none keeps the column text: a type is
a claim about every value, not most of them.

The price is a scan of every column it reads, and an answer that depends on the data: an empty
column stays text, and a fleet with one malformed cell types less than one without. Hence second
to ORC's schemas (orcschema), which type ORC's tables - the large ones - without reading them.
"""
from typing import NamedTuple


class Candidate(NamedTuple):
    name: str              # recorded in orca_tables.schema
    fits: str              # SQL true when one non-empty value '{c}' parses as this type
    cast: str              # SQL reading '{c}' as this type


def _timestamp(name: str, fmt: str, shape: str) -> Candidate:
    """A timestamp in one exact format, every field at its full width.

    Neither a cast nor strptime alone is strict: try_cast reads a truncated '2013-08-22 11:4' as
    11:04:00, and strptime does not hold a field to its width - '11:4:22.390' parses as 11:04:22
    and '20261006-12055' as 12:05:05 (both tested). 'shape' is the regex of the full-width text,
    and only text of that shape is parsed.
    """
    parse = f"try_strptime({{c}}, '{fmt}')"
    return Candidate(name, f"regexp_full_match({{c}}, '{shape}') AND {parse} IS NOT NULL",
                     f"CASE WHEN regexp_full_match({{c}}, '{shape}') THEN {parse} END")


# ORC's CSV writer: '{YYYY}-{MM}-{DD} {hh}:{mm}:{ss}.{mmm}' (CsvFileWriter.cpp). Also what a
# schema 'timestamp' column is read as (fleetdb.CASTS).
ORC_TIMESTAMP = _timestamp("timestamp (ORC)", "%Y-%m-%d %H:%M:%S.%g",
                           r"\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3}")

_GUID = "[{]?[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}[}]?"

# In order: the first one every value fits wins. Booleans before integers so that 0/1 stay
# integers (only Y/N/true/false are booleans); unsigned before signed. No locale-dependent date
# (systeminfo's '02/10/2019, 15:39:21'): day and month cannot be told apart from the data.
CANDIDATES = (
    Candidate("boolean", "upper({c}) IN ('Y', 'N', 'TRUE', 'FALSE')", "try_cast({c} AS BOOLEAN)"),
    Candidate("ubigint", "regexp_full_match({c}, '[0-9]+|0[xX][0-9a-fA-F]+') "
                         "AND try_cast({c} AS UBIGINT) IS NOT NULL", "try_cast({c} AS UBIGINT)"),
    Candidate("bigint", "regexp_full_match({c}, '-?[0-9]+') "
                        "AND try_cast({c} AS BIGINT) IS NOT NULL", "try_cast({c} AS BIGINT)"),
    ORC_TIMESTAMP,
    _timestamp("timestamp", "%Y-%m-%d %H:%M:%S", r"\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}"),
    _timestamp("timestamp (ISO 8601)", "%Y-%m-%dT%H:%M:%S",
               r"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}"),
    _timestamp("timestamp (autorunsc)", "%Y%m%d-%H%M%S", r"\d{8}-\d{6}"),
    Candidate("uuid", f"regexp_full_match({{c}}, '{_GUID}') "
                      "AND try_cast({c} AS UUID) IS NOT NULL", "try_cast({c} AS UUID)"),
)


def fill(template: str, column_sql: str) -> str:
    # replace(), not format(): the templates hold regex braces.
    return template.replace("{c}", column_sql)


def census_sql(source: str, columns_sql: list[str]) -> str:
    """One pass over 'source' counting, per column, its non-empty values ('n<i>') and those that
    do not fit each candidate ('f<i>_<j>')."""
    terms = []
    for i, column in enumerate(columns_sql):
        present = f"{column} IS NOT NULL AND {column} <> ''"
        terms.append(f"count(*) FILTER (WHERE {present}) AS n{i}")
        for j, candidate in enumerate(CANDIDATES):
            fits = fill(candidate.fits, column)
            terms.append(f"count(*) FILTER (WHERE {present} AND NOT coalesce({fits}, false)) "
                         f"AS f{i}_{j}")
    return f"SELECT {', '.join(terms)} FROM {source}"


def choose(census: dict, count: int) -> list[Candidate | None]:
    """Per column, the first candidate no non-empty value failed; None for an empty column or one
    no candidate fits."""
    chosen = []
    for i in range(count):
        if int(census[f"n{i}"]) == 0:
            chosen.append(None)
            continue
        chosen.append(next((candidate for j, candidate in enumerate(CANDIDATES)
                            if int(census[f"f{i}_{j}"]) == 0), None))
    return chosen


def cast_sql(candidate: Candidate, column_sql: str) -> str:
    return fill(candidate.cast, column_sql)
