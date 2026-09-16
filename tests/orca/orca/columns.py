"""Pure column logic: the volatile-column set, kept-column filtering, SQL quoting."""

DEFAULT_VOLATILE_COLUMNS: tuple[str, ...] = (
    "SampleCollectionDate",
    "CreationDate",
    "LastAccessDate",
    "LastModificationDate",
    "FileNameLastAccessDate",
    "LastAttrChangeDate",
)


def kept_columns(all_columns: list[str], drop_columns) -> list[str]:
    drop = set(drop_columns)
    return [c for c in all_columns if c not in drop]


def quote_columns(columns: list[str]) -> str:
    return ", ".join('"' + c + '"' for c in columns)
