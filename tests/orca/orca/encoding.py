"""Bring a collected CSV to the UTF-8 duckdb reads, whatever language its host spoke.

ORC's own tables are UTF-8, but the tools it runs write what they like: autorunsc writes
UTF-16LE with a BOM, and console tools ('systeminfo /fo csv') write in the host's console
codepage - on a French host cp850, where the thousands separator in '1 024 MB' is byte 0xFF and
not valid UTF-8. The codepage is not guessed: ORC records GetConsoleOutputCP() in the Outline
as 'system.codepage', which is the encoding those tools wrote with on that machine.

A file is only rewritten when it has to be. A UTF-16 BOM is two bytes to look at; anything else
is first handed to duckdb as UTF-8, and only re-read through the host codepage when duckdb says
it is not UTF-8 - validating every multi-GB NTFSInfo table up front would read each one twice.

PowerShell's ConvertTo-Csv and Export-Csv (before 6.0, without -NoTypeInformation) open with a
'#TYPE <class>' line, which duckdb would take for the header: the real header becomes the first
row and the only column is named after the .NET type.
"""
import codecs
import os
import shutil

from . import outline as _outline

UTF16_BOMS = (codecs.BOM_UTF16_LE, codecs.BOM_UTF16_BE)

# Last resort when the Outline is missing or names a codepage Python cannot decode: latin-1
# maps every byte, so the table is still produced; characters above 0x7F may be wrong.
FALLBACK_ENCODING = "latin-1"

# duckdb's own wording for the two failures this module answers.
INVALID_UNICODE = "Invalid unicode"
NOT_A_CSV = "not possible to automatically detect the CSV parsing dialect"

# PowerShell writes exactly this, and nothing else a CSV holds starts with it.
TYPE_LINE = b"#TYPE "

_CHUNK = 1 << 20


def bom_encoding(path: str) -> str | None:
    """'utf-16' when the file starts with a UTF-16 BOM, else None (UTF-8, with a BOM or not,
    is what duckdb reads already)."""
    with open(path, "rb") as f:
        head = f.read(2)
    return "utf-16" if head in UTF16_BOMS else None


def codec_for_codepage(codepage) -> str | None:
    """The Python codec for a Windows codepage number, or None when Python has none."""
    try:
        name = f"cp{int(codepage)}"
        return codecs.lookup(name).name
    except (TypeError, ValueError, LookupError):
        return None


def host_codepage(outline_paths: list[str]) -> str | None:
    """The console codepage the collecting host wrote with, as a Python codec name."""
    for path in outline_paths:
        outline = _outline.read_outline(path)
        if not outline:
            continue

        system = outline.get("system") or {}
        codec = codec_for_codepage(system.get("codepage"))
        if codec:
            return codec

    return None


def transcode(src: str, dst: str, encoding: str) -> str:
    """Rewrite 'src' as UTF-8 at 'dst', streamed. 'replace' rather than fail: one undecodable
    byte should cost a character, not the table. The 'utf-16' codec consumes the BOM."""
    with open(src, "r", encoding=encoding, errors="replace", newline="") as fin, \
            open(dst, "w", encoding="utf-8", newline="") as fout:
        while True:
            chunk = fin.read(_CHUNK)
            if not chunk:
                break
            fout.write(chunk)
    return dst


def starts_with_type_line(path: str) -> bool:
    """True when the file opens with PowerShell's '#TYPE' line, a UTF-8 BOM before it or not."""
    with open(path, "rb") as f:
        head = f.read(len(codecs.BOM_UTF8) + len(TYPE_LINE))
    if head.startswith(codecs.BOM_UTF8):
        head = head[len(codecs.BOM_UTF8):]
    return head.startswith(TYPE_LINE)


def drop_first_line(src: str, dst: str) -> str:
    """Copy 'src' to 'dst' without its first line, byte for byte: the rest may still be in the
    host's codepage, and decoding it here would spoil the codepage retry that follows.

    A whole copy to lose one line; PowerShell CSVs are small. The performant version would pass
    duckdb 'skip=1' instead, but every engine call that reads the CSV (DESCRIBE included) would
    then have to carry it.
    """
    with open(src, "rb") as fin, open(dst, "wb") as fout:
        fin.readline()
        shutil.copyfileobj(fin, fout, _CHUNK)
    return dst


def derived_path(src: str, tag: str) -> str:
    """Where a rewrite of 'src' goes: beside it, in the job's own scratch directory."""
    root, ext = os.path.splitext(src)
    return f"{root}.{tag}{ext}"


def first_line(path: str, limit: int = 120) -> str:
    """The first line of a file, for saying what a non-CSV actually holds."""
    encoding = bom_encoding(path) or "utf-8-sig"
    with open(path, "r", encoding=encoding, errors="replace") as f:
        return f.readline(limit).strip()
