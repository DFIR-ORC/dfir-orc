"""7-Zip access: pure listing parser + thin exe wrappers (list / extract one entry)."""
import os
import shutil
import subprocess
import xml.etree.ElementTree as ET
from dataclasses import dataclass


SIGNATURE = b"7z\xbc\xaf\x27\x1c"


@dataclass(frozen=True)
class ArchiveEntry:
    path: str
    size: int
    crc: str
    attributes: str
    modified: str = ""


def is_archive(path: str) -> bool:
    """True when the file starts with the 7z signature.

    A cheap header read, not a validity check: enough to tell a real archive from a blob that
    only claims to be one, which is what separates '/no_journaling' content from garbage.
    """
    try:
        with open(path, "rb") as source:
            return source.read(len(SIGNATURE)) == SIGNATURE
    except OSError:
        return False


def parse_listing(lines: list[str]) -> list[ArchiveEntry]:
    if not lines:
        return []

    # Entries follow the first dashed-separator line; everything before is header.
    start = -1
    for i, line in enumerate(lines):
        if line.strip() == "----------":
            start = i
            break
    if start < 0:
        return []

    entries: list[ArchiveEntry] = []
    current: dict[str, str] = {}

    def flush():
        if "Path" in current and not current.get("Attributes", "").startswith("D"):
            size = current.get("Size", "").strip()
            entries.append(ArchiveEntry(
                path=current["Path"],
                size=int(size) if size else 0,
                crc=current.get("CRC", ""),
                attributes=current.get("Attributes", ""),
                modified=current.get("Modified", ""),
            ))

    for line in lines[start + 1:]:
        if not line.strip():
            flush()
            current = {}
            continue
        sep = line.find(" = ")
        if sep < 0:
            continue
        current[line[:sep]] = line[sep + 3:]
    flush()
    return entries


@dataclass(frozen=True)
class ArchiveTest:
    """What '7z t' said about one archive: its exit code and what it complained about."""
    code: int
    problems: list[str]

    @property
    def ok(self) -> bool:
        return self.code == 0


def parse_test_output(lines: list[str]) -> list[str]:
    """What '7z t' complained about, one string per problem, in the order 7-Zip printed them.

    Three shapes carry a finding: 'ERROR: <kind> : <entry>' for an entry that did not decode,
    'Open ERROR: <reason>' for an archive that could not be opened at all, and the lines of the
    trailing 'ERRORS:' block, which is where the reason for an unopenable archive ends up. A
    bare 'ERROR: <name>' is 7-Zip restating the file it is working on - no " : " and no finding
    - so it is dropped: a report quoting the archive's own name back says nothing.
    """
    problems: list[str] = []
    in_block = False
    for raw in lines:
        line = raw.strip()
        if not line:
            in_block = False
            continue

        if line == "ERRORS:":
            in_block = True
            continue

        if in_block:
            problems.append(line)
            continue

        if line.startswith("Open ERROR: "):
            problems.append(line[len("Open ERROR: "):].strip())
            continue

        if line.startswith("ERROR: ") and " : " in line[len("ERROR: "):]:
            problems.append(line[len("ERROR: "):].strip())

    return problems


def find_7z() -> str | None:
    found = shutil.which("7z")
    if found:
        return found
    default = r"C:\Program Files\7-Zip\7z.exe"
    return default if os.path.exists(default) else None


def list_entries(seven_zip: str, archive: str, password: str | None = None) -> list[ArchiveEntry]:
    args = [seven_zip, "l", "-slt"]
    if password:
        args.append("-p" + password)
    args.append(archive)
    result = subprocess.run(args, capture_output=True, text=True)
    return parse_listing(result.stdout.splitlines())


def test_archive(seven_zip: str, archive: str, password: str | None = None) -> ArchiveTest:
    """Decompress every entry of 'archive' to nothing, verifying each stored CRC on the way.

    The only layer that reads the payload: a listing reports the CRC the archive claims for an
    entry, this recomputes it. Nothing is written, so it costs a read and some CPU, not space.

    '-p' is passed even with no password, and stdin is closed: an encrypted entry would
    otherwise make 7-Zip prompt on the console, and a verification pass that hangs waiting for
    a password nobody is there to type is worse than one that reports the archive as failed.
    """
    args = [seven_zip, "t", archive, "-y", "-bsp0", "-p" + (password or "")]
    result = subprocess.run(args, capture_output=True, text=True, stdin=subprocess.DEVNULL)
    problems = parse_test_output(result.stdout.splitlines() + result.stderr.splitlines())
    if result.returncode and not problems:
        # Fail closed: a non-zero 7-Zip whose output held nothing quotable still has to read
        # as a failure, and with whatever it did say rather than an empty list.
        message = (result.stderr or result.stdout or "").strip()
        problems = [message or f"7z exited {result.returncode}"]

    return ArchiveTest(result.returncode, problems)


def extract_entry(seven_zip: str, archive: str, entry_path: str, dest_dir: str,
                  password: str | None = None) -> str:
    # -bsp0: silence the console progress line, which 7-Zip writes straight to the console
    # (bypassing captured stdout) and would otherwise leak into the caller's terminal.
    args = [seven_zip, "x", archive, entry_path, "-o" + dest_dir, "-y", "-bsp0"]
    if password:
        args.append("-p" + password)
    result = subprocess.run(args, capture_output=True, text=True)
    out_path = os.path.join(dest_dir, entry_path)

    # 7-Zip returns 1 for non-fatal warnings yet still writes the entry; the PowerShell pipeline
    # this replaces ignored 7z exit codes entirely. Judge success by whether the file was
    # produced, so a benign Windows-7-Zip warning does not abort the whole ingest - but surface
    # 7z's own message (fail closed) when the entry really is missing.
    if not os.path.exists(out_path):
        message = (result.stderr or result.stdout or "").strip()
        raise RuntimeError(f"7z extract of '{entry_path}' from '{archive}' failed "
                           f"(code {result.returncode}): {message}")

    return out_path


def extract_all(seven_zip: str, archive: str, dest_dir: str, password: str | None = None) -> int:
    """Extract everything in 'archive'. Returns 7-Zip's exit code: 0 clean, 1 warnings.

    The code is worth having rather than discarding: on extraction a warning means entries did
    not come out, which is the difference between an archive whose contents are now on disk and
    one that is still the only copy of something.
    """
    args = [seven_zip, "x", archive, "-o" + dest_dir, "-y", "-bsp0"]
    if password:
        args.append("-p" + password)
    result = subprocess.run(args, capture_output=True, text=True)

    # Tolerate 7-Zip's warning code (1); only a fatal error (>= 2) aborts, and then with 7z's
    # own message so the failing archive is identifiable without a rerun.
    if result.returncode >= 2:
        message = (result.stderr or result.stdout or "").strip()
        raise RuntimeError(f"7z extract of '{archive}' failed "
                           f"(code {result.returncode}): {message}")

    return result.returncode


def password_from_cli_config(path: str) -> str | None:
    """Read an archive password from an ORC '*_cli_config.xml' (any element's
    'password' attribute), so encrypted sub-archives can be opened without prompting."""
    try:
        tree = ET.parse(path)
    except (ET.ParseError, OSError):
        return None
    for element in tree.iter():
        for key, value in element.attrib.items():
            if key.lower() == "password" and value:
                return value
    return None
