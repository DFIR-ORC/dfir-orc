"""Pure column logic: the volatile-column set, kept-column filtering, SQL quoting."""

DEFAULT_VOLATILE_COLUMNS: tuple[str, ...] = (
    "SampleCollectionDate",
    "CreationDate",
    "LastAccessDate",
    "LastModificationDate",
    "FileNameLastAccessDate",
    "LastAttrChangeDate",
)


# What orca stamps into every table so a row knows which host and which run it came from.
# Prefixed because ORC emits its own optional 'ComputerName' in the file-info families, and
# present on every table - unlike ORC's, which is config-dependent and absent from the CSVs
# other tools produce (autoruns, processes, PersistenceSniper).
IDENTITY_COLUMNS = ("OrcComputerName", "OrcRunId", "OrcTimestamp")


def kept_columns(all_columns: list[str], drop_columns) -> list[str]:
    drop = set(drop_columns)
    return [c for c in all_columns if c not in drop]


def quote_columns(columns: list[str]) -> str:
    return ", ".join('"' + c + '"' for c in columns)
