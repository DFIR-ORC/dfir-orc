"""ORC's table schemas - the '*SqlSchema.xml' each tool describes its CSV columns with.

Bundled with orca (orca/schemas, copied from src/OrcCommand/Command), or read from the ORC binary
'--schema <path>' names when a collection was made by a version whose tables the bundled copy does
not know. Used by fleetdb for its typed views. See
docs/superpowers/specs/2026-10-06-orca-fleet-database-design.md, section 9.

A binary is never run: it comes from a collection, and is evidence. Its resources are read with
the Windows loader in data-file mode, which maps them and executes nothing.
"""
import ctypes
import glob
import os
import sys
import xml.etree.ElementTree as ET
from typing import NamedTuple

from . import scratch

HERE = os.path.dirname(os.path.abspath(__file__))

# The copy orca carries, so it needs neither the source tree nor a binary to type a fleet.
BUNDLED_SCHEMAS = os.path.join(HERE, "schemas")
BUNDLED_SOURCE = "bundled with orca"

# Where the bundled copy comes from: tests/orca/orca -> src/OrcCommand/Command/<Tool>/*Schema.xml.
SOURCE_SCHEMAS = os.path.normpath(os.path.join(HERE, "..", "..", "..", "src", "OrcCommand",
                                               "Command"))

SCHEMA_RESOURCE_TYPE = "BINARY"
SCHEMA_RESOURCE_SUFFIX = "_SQLSCHEMA"

# A capsule's per-architecture runners (OrcCapsule Runner.cpp), any of which carries the schemas.
RUNNER_RESOURCES = ("RUN64", "RUN32", "RUN64XP", "RUN32XP", "RUNARM64")
RT_RCDATA = 10
ZSTD_MAGIC = b"\x28\xb5\x2f\xfd"


class SchemaTable(NamedTuple):
    tool: str
    key: str
    columns: dict          # column name -> schema type ('timestamp', 'uint64', ...)


class Schemas(NamedTuple):
    source: str            # where they came from, recorded in the database
    tables: list           # SchemaTable


def parse_schema(document: bytes) -> list[SchemaTable]:
    """The tables of one '<sqlschema tool=...>' document."""
    root = ET.fromstring(document)
    tool = root.get("tool", "")
    tables = []
    for table in root.findall("table"):
        columns = {column.get("name"): column.tag for column in table if column.get("name")}
        tables.append(SchemaTable(tool, table.get("key", ""), columns))
    return tables


def parse_all(documents: list[bytes], source: str) -> Schemas:
    tables = []
    for document in documents:
        tables += parse_schema(document)
    return Schemas(source, tables)


def schema_files(root: str, nested: bool) -> list[str]:
    """The '*Schema.xml' under 'root': one per tool directory in the source tree, flat in orca."""
    parts = (root, "*", "*Schema.xml") if nested else (root, "*Schema.xml")
    return sorted(glob.glob(os.path.join(*parts)))


def _read_all(paths: list[str]) -> list[bytes]:
    documents = []
    for path in paths:
        with open(path, "rb") as f:
            documents.append(f.read())
    return documents


# --- Reading resources from a PE without running it (Windows) -------------------------------

LOAD_LIBRARY_AS_DATAFILE = 0x00000002
LOAD_LIBRARY_AS_IMAGE_RESOURCE = 0x00000020


def _kernel32():
    from ctypes import wintypes

    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    k32.LoadLibraryExW.restype = wintypes.HMODULE
    k32.LoadLibraryExW.argtypes = [wintypes.LPCWSTR, wintypes.HANDLE, wintypes.DWORD]
    k32.FreeLibrary.argtypes = [wintypes.HMODULE]
    k32.FindResourceW.restype = wintypes.HANDLE
    k32.FindResourceW.argtypes = [wintypes.HMODULE, ctypes.c_void_p, ctypes.c_void_p]
    k32.SizeofResource.restype = wintypes.DWORD
    k32.SizeofResource.argtypes = [wintypes.HMODULE, wintypes.HANDLE]
    k32.LoadResource.restype = wintypes.HANDLE
    k32.LoadResource.argtypes = [wintypes.HMODULE, wintypes.HANDLE]
    k32.LockResource.restype = ctypes.c_void_p
    k32.LockResource.argtypes = [wintypes.HANDLE]
    # Raw pointers in the callback: a type or name may be an integer id, not a string.
    k32.EnumProc = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HMODULE, ctypes.c_void_p,
                                      ctypes.c_void_p, ctypes.c_void_p)
    k32.EnumResourceNamesW.argtypes = [wintypes.HMODULE, ctypes.c_void_p, k32.EnumProc,
                                       ctypes.c_void_p]
    return k32


def _resource_id(value):
    """A resource type or name as the Win32 API takes it: a string, or an integer id."""
    return ctypes.c_wchar_p(value) if isinstance(value, str) else ctypes.c_void_p(value)


class _DataFile:
    """A PE mapped for its resources only, freed on exit."""

    def __init__(self, path: str):
        self.k32 = _kernel32()
        flags = LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE
        self.module = self.k32.LoadLibraryExW(path, None, flags)
        if not self.module:
            raise RuntimeError(f"cannot read '{path}' as an executable "
                               f"[{ctypes.WinError(ctypes.get_last_error())}]")

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.k32.FreeLibrary(self.module)

    def names(self, resource_type) -> list[str]:
        found = []

        def collect(_module, _type, name, _param):
            # An id below 0x10000 is an integer resource id (IS_INTRESOURCE), not a string.
            found.append(ctypes.wstring_at(name) if name and name >> 16 else f"#{name or 0}")
            return True

        self.k32.EnumResourceNamesW(self.module, _resource_id(resource_type),
                                    self.k32.EnumProc(collect), None)
        return found

    def read(self, resource_type, name: str) -> bytes | None:
        handle = self.k32.FindResourceW(self.module, _resource_id(name),
                                        _resource_id(resource_type))
        if not handle:
            return None
        size = self.k32.SizeofResource(self.module, handle)
        data = self.k32.LockResource(self.k32.LoadResource(self.module, handle))
        return ctypes.string_at(data, size) if data and size else None


def _zstd_decompress(data: bytes) -> bytes:
    try:
        from compression import zstd
    except ImportError as error:
        raise RuntimeError(f"a capsule's runners are zstd-compressed, which needs Python 3.14 "
                           f"(this is {sys.version.split()[0]}); give the per-architecture "
                           f"binary (DFIR-ORC_x64.exe) instead") from error
    return zstd.decompress(data)


def executable_documents(path: str) -> list[bytes]:
    """The schema documents of an ORC binary: its own, or a capsule's first runner's."""
    if sys.platform != "win32":
        raise RuntimeError("reading an ORC binary's resources needs Windows")

    with _DataFile(path) as image:
        names = [n for n in image.names(SCHEMA_RESOURCE_TYPE) if n.endswith(SCHEMA_RESOURCE_SUFFIX)]
        if names:
            return [image.read(SCHEMA_RESOURCE_TYPE, name) for name in sorted(names)]

        runner = next(((name, blob) for name in RUNNER_RESOURCES
                       if (blob := image.read(RT_RCDATA, name))), None)

    if runner is None:
        raise RuntimeError(f"'{path}' holds neither ORC schemas nor a capsule runner")

    name, blob = runner
    plain = _zstd_decompress(blob) if blob.startswith(ZSTD_MAGIC) else blob
    unpacked = os.path.join(scratch.directory("runner_"), name + ".exe")
    with open(unpacked, "wb") as f:
        f.write(plain)

    with _DataFile(unpacked) as image:
        names = [n for n in image.names(SCHEMA_RESOURCE_TYPE) if n.endswith(SCHEMA_RESOURCE_SUFFIX)]
        documents = [image.read(SCHEMA_RESOURCE_TYPE, n) for n in sorted(names)]
    if not documents:
        raise RuntimeError(f"the {name} runner of '{path}' holds no ORC schema")
    return documents


# --- Choosing the source ----------------------------------------------------------------------

AUTO_OPTION = "auto"
BUNDLED_OPTION = "bundled"
INFER_OPTION = "infer"

INFERRED_SOURCE = "inferred from the values"


class Typing(NamedTuple):
    """How a fleet database's typed views are typed: ORC's schemas first, then the values.

    'schemas' types what ORC describes; 'infer' then reads a type from the values of what no
    schema typed - the tables ORC does not describe, and the columns a newer ORC added
    (typeinfer). Either may be absent.
    """
    schemas: Schemas | None
    infer: bool

    @property
    def source(self) -> str:
        parts = [self.schemas.source] if self.schemas else []
        if self.infer:
            parts.append(INFERRED_SOURCE)
        return ", then ".join(parts)


def resolve(option: str = AUTO_OPTION) -> Typing:
    """What '--schema' names.

    auto     the bundled schemas, then the values for what they do not type (the default)
    bundled  the bundled schemas only
    infer    the values only
    <path>   that ORC binary's schemas, then the values, as auto does with the bundled ones

    'auto' is the default, by decision (2026-10-07), over the cheaper 'bundled': it types the
    tables ORC does not describe (autoruns, processes, DNS_records, ...) and the columns a newer
    ORC added, and a family whose every column a schema knows is never scanned, so the cost of
    reading the values falls on the small tables. Measured on the three-DC sample: auto 22 typed
    views in 10-14 s, bundled 17 in 3 s, infer 21 in 30 s. 'bundled' and 'infer' are kept as the
    two ends of that trade - the schemas alone, or the values alone (what 'auto' first meant).

    The binary is read here, before any host is ingested: a wrong path should not cost the
    length of a fleet ingest.
    """
    if option == AUTO_OPTION:
        return Typing(load(), True)
    if option == BUNDLED_OPTION:
        return Typing(load(), False)
    if option == INFER_OPTION:
        return Typing(None, True)
    if not os.path.isfile(option):
        raise RuntimeError(f"--schema '{option}': neither '{AUTO_OPTION}', '{BUNDLED_OPTION}', "
                           f"'{INFER_OPTION}', nor an existing ORC binary")
    return Typing(load(orc=option), True)


def load(orc: str | None = None) -> Schemas | None:
    """The schemas typed views use: '--orc' when given, else the bundled copy.

    Not a binary found beside the collection: types do not change between ORC versions (one
    column retyped since 2020, utf8 to binary, both text here), columns are added. A collection
    from a newer ORC is therefore told apart by columns the bundled schema does not know, and
    fleetdb reports those, naming '--schema <DFIR-ORC.exe>' as the way to type them.
    """
    if orc:
        return parse_all(executable_documents(orc), os.path.abspath(orc))

    documents = _read_all(schema_files(BUNDLED_SCHEMAS, nested=False))
    if not documents:
        return None
    return parse_all(documents, BUNDLED_SOURCE)

