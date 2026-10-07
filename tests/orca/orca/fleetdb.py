"""The fleet database: one DuckDB file of views over every host's parquet.

'orca fleet' leaves one dataset per host; this is what makes them one thing to query.
'duckdb fleet.duckdb', then 'SELECT ... FROM NTFSInfo' reads every host of the fleet, from any
working directory. Views, never copies: a host re-ingested in place is current at once, which
is the staleness a materialised fleet table could not avoid (fleet spec 5.3).

A view is per table *family*, not per table name: ORC names a per-volume output after the
volume ('NTFSInfo_00000000_DiskInterface_0x4e106ff4106fe187_'), so no two hosts share one
and a view per name would stack nothing. See docs/superpowers/specs/
2026-10-06-orca-fleet-database-design.md.
"""
import os
import re
from typing import NamedTuple

from . import layout, naming, typeinfer
from .engine import sql_path

# ORC's per-volume output name, '<prefix>_<8-digit index>_<location identifier>_' (the Archive
# output in UtilitiesMain.h), read backwards. The identifier varies - 'DiskInterface_0x...',
# 'Volume_C', a shadow copy's GUID - so only the frame is matched.
_PER_VOLUME = re.compile(r"^(.+?)_\d{8}_.+_$")

# orca's own tables in the database; a collected family may not take these names.
RESERVED_PREFIX = "orca_"

PARQUET_FILE_COLUMN = "OrcParquetFile"


def table_family(base: str) -> str:
    """'NTFSInfo_00000000_Volume_C_' -> 'NTFSInfo'; any other name is its own family."""
    match = _PER_VOLUME.match(base)
    return match.group(1) if match else base


def family_of(name: str, entry_path: str) -> str:
    """The family of a table, from the CSV ORC collected rather than from the table name.

    naming.assign_names may extend a table name with a command keyword on collision
    ('NTFSInfo_00000000_..._NTFSInfo_quick'); the per-volume frame is then no longer at the
    end of the name. That keyword is kept on the family: two command sets collecting with
    different options are two tables, and stacking them together would be wrong.
    """
    base = naming.base_name(entry_path) if entry_path else ""
    if not base or not name.startswith(base):
        return table_family(name)

    return table_family(base) + name[len(base):]


def dataset_families(dataset: str) -> dict[str, list[str]]:
    """family -> the parquet files of one dataset, for the tables that exist on disk."""
    families: dict[str, list[str]] = {}
    for name, entry in layout.table_entries(dataset):
        path = os.path.join(dataset, name + ".parquet")
        if os.path.exists(path):
            families.setdefault(family_of(name, entry), []).append(os.path.abspath(path))
    return families


def fleet_families(datasets: list[str]) -> dict[str, list[str]]:
    """family -> every parquet file of that family across 'datasets', in a stable order."""
    merged: dict[str, list[str]] = {}
    for dataset in datasets:
        for family, paths in dataset_families(dataset).items():
            merged.setdefault(family, []).extend(paths)
    return {family: sorted(merged[family]) for family in sorted(merged, key=str.lower)}


def quote_identifier(name: str) -> str:
    return '"' + name.replace('"', '""') + '"'


def _literal(value) -> str:
    return "'" + str(value).replace("'", "''") + "'"


def view_sql(family: str, paths: list[str]) -> str:
    """The view of one family: its files listed absolutely, so it reads from any directory and
    reads this fleet's hosts only. A glob would be resolved against DuckDB's working directory."""
    files = ", ".join(f"'{sql_path(path)}'" for path in paths)
    return (f"CREATE VIEW {quote_identifier(family)} AS SELECT * FROM read_parquet([{files}], "
            f"union_by_name = true, filename = '{PARQUET_FILE_COLUMN}');\n")


def table_sql(name: str, columns: list[str], rows: list[list]) -> str:
    """A small text table holding 'rows'. For orca's own record, which is copied rather than
    viewed: it describes the fleet run that wrote this database."""
    definition = ", ".join(f"{quote_identifier(column)} VARCHAR" for column in columns)
    sql = f"CREATE TABLE {quote_identifier(name)} ({definition});\n"
    if rows:
        values = ",\n".join("(" + ", ".join(_literal(value) for value in row) + ")"
                            for row in rows)
        sql += f"INSERT INTO {quote_identifier(name)} VALUES\n{values};\n"
    return sql


# How each schema type is read back from the CSV writer's rendering (spec 9.3). try_*, so a
# malformed cell is NULL here and still exact in the text view. The timestamp is held to the
# writer's exact, full-width format (typeinfer._timestamp says why neither a cast nor strptime
# alone is enough).
CASTS = {
    "timestamp": typeinfer.ORC_TIMESTAMP.cast,
    "uint64": "try_cast({c} AS UBIGINT)",
    "uint32": "try_cast({c} AS UINTEGER)",
    "bool": "try_cast({c} AS BOOLEAN)",
    "guid": "try_cast({c} AS UUID)",
}

TYPED_SUFFIX = "_typed"

# A family is matched to a schema table only when the table names at least this share of its
# columns: below it, a few common names (ComputerName, FullPath) would bind unrelated tables.
MIN_MATCH = 0.5


def match_table(family: str, columns: list[str], tables: list):
    """The schema table a family's columns come from, or None (spec 9.2).

    By columns, because nothing in a schema says which collected file it describes: a CSV header
    is the table's column names, minus what the configuration left out. Most shared columns
    wins; a tie goes to the table whose tool name is in the family name.
    """
    own = [c for c in columns if not c.startswith("Orc")]
    if not own:
        return None

    def score(table):
        shared = sum(1 for c in own if c in table.columns)
        return shared, table.tool.lower() in family.lower()

    best = max(tables, key=score, default=None)
    if best is None or score(best)[0] < MIN_MATCH * len(own):
        return None
    return best


def schema_casts(columns: list[str], table) -> dict[str, str]:
    """column -> its cast, for the columns the schema table gives a type to apply."""
    casts = {}
    for column in columns:
        cast = CASTS.get(table.columns.get(column, ""))
        if cast:
            casts[column] = typeinfer.fill(cast, quote_identifier(column))
    return casts


def typed_view_sql(family: str, casts: dict[str, str]) -> str | None:
    """'<family>_typed' over the text view, or None when there is no column to type."""
    if not casts:
        return None

    terms = [f"{cast} AS {quote_identifier(column)}" for column, cast in casts.items()]
    return (f"CREATE VIEW {quote_identifier(family + TYPED_SUFFIX)} AS SELECT * REPLACE ("
            + ", ".join(terms) + f") FROM {quote_identifier(family)};\n")


def unknown_columns(columns: list[str], table) -> list[str]:
    """The columns of a family its schema table does not describe: they stay text.

    The sign of a collection made by a newer ORC than the schema: between versions columns are
    added, types do not change. orca's own Orc* columns are not ORC's and never count.
    """
    return [c for c in columns if not c.startswith("Orc") and c not in table.columns]


def inferred_casts(engine, staging: str, family: str, columns: list[str]) -> dict[str, str]:
    """column -> its cast, for the columns every value of which fits one type (typeinfer)."""
    own = [c for c in columns if not c.startswith("Orc")]
    if not own:
        return {}

    quoted = [quote_identifier(c) for c in own]
    rows = engine.query_rows_in_database(
        staging, typeinfer.census_sql(quote_identifier(family), quoted))
    chosen = typeinfer.choose(rows[0], len(own)) if rows else []
    return {column: typeinfer.cast_sql(candidate, sql)
            for column, sql, candidate in zip(own, quoted, chosen) if candidate}


class Typed(NamedTuple):
    schema: str            # what it was read as: 'NTFSInfo.fileinfo', '... + inferred', 'inferred'
    created: bool          # False when no column of it had a type to apply
    unknown: list          # columns its schema table does not know: a newer ORC's


INFERRED_LABEL = "inferred"


def _typed_view(engine, staging: str, family: str, typing) -> Typed | None:
    """Create the typed companion of a family view; None when nothing describes it.

    'typing' (orcschema.Typing): the schema table a family matches types the columns it knows -
    text included, so a hash is never re-read as a number - and the values type the rest, when
    asked to: every column of a family no schema describes, or the columns a newer ORC added.
    """
    columns = engine.query_column_in_database(
        staging, f"SELECT column_name FROM (DESCRIBE {quote_identifier(family)})", "column_name")

    table = match_table(family, columns, typing.schemas.tables) if typing.schemas else None
    if table is None and not typing.infer:
        return None

    casts, labels, unknown = {}, [], []
    if table is not None:
        casts = schema_casts(columns, table)
        labels.append(f"{table.tool}.{table.key}")
        unknown = unknown_columns(columns, table)

    rest = unknown if table is not None else columns
    inferred = inferred_casts(engine, staging, family, rest) if typing.infer and rest else {}
    if inferred:
        casts.update(inferred)
        labels.append(INFERRED_LABEL)

    sql = typed_view_sql(family, casts)
    if sql:
        engine.run_in_database(staging, sql)
    return Typed(" + ".join(labels) or INFERRED_LABEL, bool(sql), unknown)


DATABASE_NAME = "fleet.duckdb"


def database_path(where: str) -> str:
    """'<where>/fleet.duckdb', beside .orca-fleet/: one fleet per place, as its bookkeeping is.
    Two fleets in one working directory overwrite each other; --out keeps them apart."""
    return os.path.join(where, DATABASE_NAME)


def _one_line(error: Exception) -> str:
    # A duckdb error carries the whole failing statement, here hundreds of file names.
    return " ".join(str(error).split("\nSQL:")[0].split())[:300]


def _remove(path: str) -> None:
    for leftover in (path, path + ".wal"):
        if os.path.exists(leftover):
            os.remove(leftover)


# orca_tables: one row per family. 'not_in_schema' rather than 'untyped': under '--schema auto'
# a column no schema describes is still typed from its values, so what this records is that no
# schema named the column - not that it stayed text.
CATALOG_COLUMNS = ["family", "view", "hosts", "files", "status", "error", "schema",
                   "not_in_schema"]


class BuildResult(NamedTuple):
    skipped: list          # (view, reason) of the views that could not be created
    typed: int             # typed views created
    outdated: list         # (family, [columns]) the schema does not know: a newer ORC's tables


def _family_views(engine, staging: str, family: str, paths: list[str], typing) -> list[list]:
    """Create a family's text view, and its typed companion when a schema describes it.

    Returns its catalog rows. Each is its own statement: CREATE VIEW reads every listed file's
    schema, and one unreadable parquet must cost its family's view, not the database.
    """
    hosts, files = len({os.path.dirname(path) for path in paths}), len(paths)
    if family.lower().startswith(RESERVED_PREFIX):
        reason = f"the '{RESERVED_PREFIX}' prefix is reserved for orca's own tables"
        return [[family, family, hosts, files, "skipped", reason, "", ""]]

    try:
        engine.run_in_database(staging, view_sql(family, paths))
    except RuntimeError as error:
        return [[family, family, hosts, files, "skipped", _one_line(error), "", ""]]

    rows = [[family, family, hosts, files, "view", "", "", ""]]
    if typing is None:
        return rows

    view = family + TYPED_SUFFIX
    try:
        typed = _typed_view(engine, staging, family, typing)
    except RuntimeError as error:
        return rows + [[family, view, hosts, files, "skipped", _one_line(error), "", ""]]

    if typed is None:
        return rows

    schema = typed.schema
    untyped = ", ".join(typed.unknown)
    if typed.created:
        rows.append([family, view, hosts, files, "typed", "", schema, untyped])
    elif untyped:
        # Nothing got a type, yet the schema is missing columns: still worth saying.
        rows[0][6:8] = [schema, untyped]
    return rows


def build_database(engine, database: str, families: dict[str, list[str]],
                   tables: dict[str, tuple[list[str], list[list]]], typing=None) -> BuildResult:
    """Write 'database' from scratch: the views of every family, then orca's own 'tables'.

    Built beside it and moved over it at the end, so a failed build leaves the previous
    database whole. 'typing' (orcschema.Typing) adds the typed views; None leaves text only.
    """
    staging = database + ".tmp"
    _remove(staging)

    catalog: list[list] = []
    for family, paths in families.items():
        catalog += _family_views(engine, staging, family, paths, typing)

    own = dict(tables)
    own["orca_tables"] = (CATALOG_COLUMNS, catalog)
    own["orca_schema"] = (["source"], [[typing.source]] if typing else [])
    engine.run_in_database(staging, "".join(table_sql(name, columns, rows)
                                            for name, (columns, rows) in own.items()))

    try:
        os.replace(staging, database)
    except OSError as error:
        raise RuntimeError(f"cannot replace '{database}' ({error}); if it is open in a DuckDB "
                           f"shell, close it and re-run") from error

    skipped = [(row[1], row[5]) for row in catalog if row[4] == "skipped"]
    typed = sum(1 for row in catalog if row[4] == "typed")
    outdated = [(row[0], row[7].split(", ")) for row in catalog if row[7]]
    return BuildResult(skipped, typed, outdated)
