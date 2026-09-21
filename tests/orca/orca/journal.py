"""JRNL journal replay: rebuild the plain file a journaled stream describes.

ORC wraps an archive stream in a journal (Orc::JournalingStream) before encrypting it, so a
decrypted '.p7b' payload is a journal, not a '.7z'. Replaying it here replaces the external
'unstream.exe'. Pure stdlib: no crypto happens at this layer (see cms.py).

Format, from src/OrcLib/JournalingStream.cpp:
    header  "JRNL" + uint32 version (2)                          8 bytes
    record  char[4] signature + uint32 + uint64, little endian  16 bytes
      "WRIT"  uint32 padding, uint64 length; `length` payload bytes follow
      "SEEK"  uint32 method (0 begin, 1 current, 2 end), int64 distance
      "CLOS"  padding only; the journal ends here
Records run to EOF. Operations seek, so the output is written with random access.
"""
import os
import struct

MAGIC = b"JRNL"
VERSION = 2
HEADER_SIZE = 8
RECORD_SIZE = 16
RECORD_FORMAT = "<4sIQ"

OP_WRITE = b"WRIT"
OP_SEEK = b"SEEK"
OP_CLOSE = b"CLOS"

SEEK_BEGIN = 0
SEEK_CURRENT = 1
SEEK_END = 2

_COPY_CHUNK = 1 << 20


def is_journaled(path: str) -> bool:
    """True when the file starts with a JRNL header of the version we replay."""
    try:
        with open(path, "rb") as source:
            header = source.read(HEADER_SIZE)
    except OSError:
        return False
    return len(header) == HEADER_SIZE and _parse_header(header) is not None


def _parse_header(header: bytes) -> int | None:
    """Journal version, or None when the header is not a JRNL header we support."""
    if len(header) != HEADER_SIZE:
        return None
    magic, version = struct.unpack("<4sI", header)
    if magic != MAGIC or version != VERSION:
        return None
    return version


def _as_signed(value: int) -> int:
    # The distance field is declared unsigned in the C++ struct but used as a signed seek
    # distance, so a backward SEEK arrives here as a huge unsigned value.
    return value - (1 << 64) if value >= (1 << 63) else value


def _target_position(target, method: int, distance: int) -> int:
    if method == SEEK_BEGIN:
        return distance
    if method == SEEK_CURRENT:
        return target.tell() + distance
    if method == SEEK_END:
        return os.fstat(target.fileno()).st_size + distance
    raise RuntimeError(f"journal: unknown seek method {method}")


def _copy_payload(source, target, length: int, remaining_in_source: int, src: str) -> None:
    # Never size a buffer from the file: the length is attacker-controlled. Reject it against
    # what the source actually holds, then copy in bounded chunks.
    if length > remaining_in_source:
        raise RuntimeError(f"journal: '{src}' declares a {length} byte write with only "
                           f"{remaining_in_source} bytes left")

    remaining = length
    while remaining:
        chunk = source.read(min(_COPY_CHUNK, remaining))
        if not chunk:
            raise RuntimeError(f"journal: '{src}' ends inside a write payload "
                               f"({remaining} bytes short)")
        target.write(chunk)
        remaining -= len(chunk)


def replay(src: str, dst: str) -> int:
    """Replay the journal at 'src' into a new file at 'dst'; returns the bytes written.

    Every length and distance in the journal comes from a decrypted blob and is treated as
    untrusted: each is validated against the source size before it is used, and anything
    unrecognized aborts rather than resynchronizing on garbage.
    """
    source_size = os.path.getsize(src)

    with open(src, "rb") as source, open(dst, "w+b") as target:
        if _parse_header(source.read(HEADER_SIZE)) is None:
            raise RuntimeError(f"journal: '{src}' is not a JRNL v{VERSION} stream")

        closed = False
        while not closed:
            record = source.read(RECORD_SIZE)
            if not record:
                break
            if len(record) != RECORD_SIZE:
                raise RuntimeError(f"journal: '{src}' ends inside a record header")

            signature, method, value = struct.unpack(RECORD_FORMAT, record)
            if signature == OP_WRITE:
                _copy_payload(source, target, value, source_size - source.tell(), src)
            elif signature == OP_SEEK:
                position = _target_position(target, method, _as_signed(value))
                # Every output byte comes from a WRIT payload, so the output can never exceed
                # the source. That ceiling is what stops a forged seek from growing a sparse
                # file to an arbitrary size.
                if position < 0 or position > source_size:
                    raise RuntimeError(f"journal: '{src}' seeks to {position}, outside "
                                       f"0..{source_size}")
                target.seek(position)
            elif signature == OP_CLOSE:
                closed = True
            else:
                # The payload size of an unknown record is unknowable, so the stream is
                # desynchronized from here on. The C++ reference logs and reads the next 16
                # bytes anyway; we fail closed instead.
                raise RuntimeError(f"journal: '{src}' holds an unknown record "
                                   f"{signature!r} at offset {source.tell() - RECORD_SIZE}")

        if closed and source.read(1):
            raise RuntimeError(f"journal: '{src}' holds data after its close record")

        target.flush()
        return os.fstat(target.fileno()).st_size
