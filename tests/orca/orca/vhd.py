"""Attach a VHD(X) read-only so ORC can read it, through virtdisk.dll, with no PowerShell.

The attachment is deliberately NOT permanent: it is tied to the open handle, so a killed orca
detaches the disk by itself instead of leaving an orphan attached, which is the failure
'MountPoint.Dispose()' in tools/ci/test.psm1 warns about. The handle is therefore held open
for the whole ORC run.

Win32 types are spelled with plain ctypes aliases rather than ctypes.wintypes: importing
wintypes raises on Linux, and this module has to import there, where the test suite runs.
"""
import contextlib
import ctypes
import os
import re
import time
from dataclasses import dataclass

VHD_EXTENSIONS = (".vhd", ".vhdx")

# Win32 DWORD/ULONG/BOOL are fixed 32-bit; c_ulong would be 8 bytes on 64-bit Linux and the
# struct layouts below would then only be right on Windows, where they cannot be tested.
DWORD = ctypes.c_uint32
BOOL = ctypes.c_int32
HANDLE = ctypes.c_void_p
ULONG = ctypes.c_uint32

ATTACH_POLL_ATTEMPTS = 10
ATTACH_POLL_DELAY = 0.2

OPEN_VIRTUAL_DISK_VERSION_2 = 2
ATTACH_VIRTUAL_DISK_VERSION_1 = 1
ATTACH_VIRTUAL_DISK_FLAG_READ_ONLY = 0x1
VIRTUAL_DISK_ACCESS_NONE = 0
ERROR_SHARING_VIOLATION = 32
IOCTL_STORAGE_GET_DEVICE_NUMBER = 0x2D1080
GENERIC_READ = 0x80000000
FILE_SHARE_READ_WRITE = 0x1 | 0x2
OPEN_EXISTING = 3
INVALID_HANDLE_VALUE = -1
MAX_PATH = 260


@dataclass(frozen=True)
class Attached:
    path: str
    physical_path: str
    number: int
    drive_letter: str
    attached_by_orca: bool


def is_vhd(path: str) -> bool:
    return os.path.splitext(path)[1].lower() in VHD_EXTENSIONS


def disk_number(physical_path: str) -> int:
    """3 out of '\\\\.\\PhysicalDrive3'."""
    found = re.search(r"PhysicalDrive(\d+)$", physical_path, re.IGNORECASE)
    if not found:
        raise RuntimeError(f"not a physical drive path: '{physical_path}'")

    return int(found.group(1))


def find_drive_letter(number: int, letters: list[str], device_number, volume_size) -> str:
    """Which mounted volume belongs to the attached disk.

    A volume whose device number cannot be read is skipped rather than fatal: an empty card
    reader must not hide the disk on the next letter. Several volumes on one disk (a system
    partition plus the data one) resolve to the largest, as 'test.psm1' does, and the choice
    is printed so a wrong pick is visible in the log.
    """
    matches = [(letter, volume_size(letter)) for letter in letters
               if device_number(letter) == number]
    if not matches:
        raise RuntimeError(f"no mounted volume found on disk {number}; is automount disabled?")

    matches.sort(key=lambda pair: pair[1], reverse=True)
    if len(matches) > 1:
        print(f"disk {number} has {len(matches)} volumes "
              f"({', '.join(letter for letter, _ in matches)}), using {matches[0][0]}")

    return matches[0][0]


class _StorageType(ctypes.Structure):
    _fields_ = [("DeviceId", DWORD), ("VendorId", ctypes.c_byte * 16)]


class _OpenParametersVersion2(ctypes.Structure):
    _fields_ = [("GetInfoOnly", BOOL), ("ReadOnly", BOOL),
                ("ResiliencyGuid", ctypes.c_byte * 16)]


class _OpenParameters(ctypes.Structure):
    _fields_ = [("Version", DWORD), ("Version2", _OpenParametersVersion2)]


class _AttachParametersVersion1(ctypes.Structure):
    _fields_ = [("Reserved", ULONG)]


class AttachParameters(ctypes.Structure):
    """ATTACH_VIRTUAL_DISK_PARAMETERS: a version plus a union whose Version1 member is one
    reserved ULONG. The whole 8 bytes must exist and Reserved must be zero - AttachVirtualDisk
    reads it, so a bare 4-byte DWORD makes it read past the end of the allocation and fail
    with ERROR_INVALID_PARAMETER whenever the adjacent bytes are not zero."""
    _fields_ = [("Version", DWORD), ("Version1", _AttachParametersVersion1)]


def attach_parameters() -> AttachParameters:
    parameters = AttachParameters()
    parameters.Version = ATTACH_VIRTUAL_DISK_VERSION_1
    parameters.Version1.Reserved = 0
    return parameters


class _DeviceNumber(ctypes.Structure):
    _fields_ = [("DeviceType", DWORD), ("DeviceNumber", DWORD), ("PartitionNumber", DWORD)]


def wait_for_drive_letter(number: int, *, logical_drives, device_number, volume_size,
                          attempts: int = ATTACH_POLL_ATTEMPTS,
                          delay: float = ATTACH_POLL_DELAY, sleep=time.sleep) -> str:
    """Poll for the volume of a freshly attached disk.

    AttachVirtualDisk returns as soon as the disk is surfaced, but volume arrival and the
    mount manager's letter assignment happen asynchronously, so the first look at the drive
    letters can legitimately come up empty on a cold VHDX or a loaded host. Without the poll
    that race surfaces as "no mounted volume found", which blames automount for a problem
    that would have fixed itself in 200 ms.
    """
    for attempt in range(attempts):
        try:
            return find_drive_letter(number, logical_drives(), device_number, volume_size)
        except RuntimeError:
            if attempt == attempts - 1:
                raise

            sleep(delay)

    raise RuntimeError(f"no mounted volume found on disk {number}")


def _describe(status: int) -> str:
    """The system's own message for a Win32 status, empty where there is none to ask.

    ctypes.FormatError exists only on Windows, and this module has to import on Linux.
    """
    formatter = getattr(ctypes, "FormatError", None)
    if not formatter:
        return ""

    try:
        return formatter(status).strip()
    except (OSError, ValueError):
        return ""


def error_text(status: int, describe=_describe) -> str:
    """'32: The process cannot access the file...' rather than a bare number, which is the
    difference between a one-line diagnosis and a web search."""
    message = describe(status)
    return f"{status}: {message}" if message else str(status)


def _virtdisk():
    return ctypes.WinDLL("virtdisk.dll", use_last_error=True)


def _kernel32():
    return ctypes.WinDLL("kernel32.dll", use_last_error=True)


def open_disk(path: str) -> HANDLE:
    """A handle on the virtual disk. Closing it detaches, so the caller holds it."""
    storage_type = _StorageType()
    parameters = _OpenParameters()
    parameters.Version = OPEN_VIRTUAL_DISK_VERSION_2
    parameters.Version2.GetInfoOnly = 0
    parameters.Version2.ReadOnly = 1

    handle = HANDLE()
    status = _virtdisk().OpenVirtualDisk(ctypes.byref(storage_type), ctypes.c_wchar_p(path),
                                         VIRTUAL_DISK_ACCESS_NONE, 0,
                                         ctypes.byref(parameters), ctypes.byref(handle))
    if status != 0:
        raise RuntimeError(f"OpenVirtualDisk('{path}') failed with {error_text(status)}")

    return handle


def attach_disk(handle: HANDLE) -> bool:
    """Attach read-only. False when the disk was already attached by someone else."""
    parameters = attach_parameters()
    status = _virtdisk().AttachVirtualDisk(handle, None, ATTACH_VIRTUAL_DISK_FLAG_READ_ONLY,
                                           0, ctypes.byref(parameters), None)
    if status == ERROR_SHARING_VIOLATION:
        return False

    if status != 0:
        raise RuntimeError(f"AttachVirtualDisk failed with {error_text(status)}")

    return True


def physical_path(handle: HANDLE) -> str:
    size = ULONG(MAX_PATH * ctypes.sizeof(ctypes.c_wchar))
    buffer = ctypes.create_unicode_buffer(MAX_PATH)
    status = _virtdisk().GetVirtualDiskPhysicalPath(handle, ctypes.byref(size), buffer)
    if status != 0:
        raise RuntimeError(f"GetVirtualDiskPhysicalPath failed with {error_text(status)}")

    return buffer.value


def logical_drives() -> list[str]:
    mask = _kernel32().GetLogicalDrives()
    return [f"{chr(ord('A') + bit)}:" for bit in range(26) if mask & (1 << bit)]


def device_number(drive: str) -> int | None:
    """The disk a mounted volume sits on, or None when it cannot be read."""
    kernel32 = _kernel32()
    kernel32.CreateFileW.restype = HANDLE
    handle = kernel32.CreateFileW(ctypes.c_wchar_p(f"\\\\.\\{drive}"), DWORD(GENERIC_READ),
                                  DWORD(FILE_SHARE_READ_WRITE), None, DWORD(OPEN_EXISTING),
                                  DWORD(0), None)
    if not handle or handle == ctypes.c_void_p(INVALID_HANDLE_VALUE).value:
        return None

    try:
        info = _DeviceNumber()
        returned = DWORD()
        ok = kernel32.DeviceIoControl(HANDLE(handle), DWORD(IOCTL_STORAGE_GET_DEVICE_NUMBER),
                                      None, DWORD(0), ctypes.byref(info),
                                      DWORD(ctypes.sizeof(info)), ctypes.byref(returned),
                                      None)
        return int(info.DeviceNumber) if ok else None
    finally:
        kernel32.CloseHandle(HANDLE(handle))


def volume_size(drive: str) -> int:
    total = ctypes.c_ulonglong(0)
    free = ctypes.c_ulonglong(0)
    available = ctypes.c_ulonglong(0)
    ok = _kernel32().GetDiskFreeSpaceExW(ctypes.c_wchar_p(f"{drive}\\"),
                                         ctypes.byref(available), ctypes.byref(total),
                                         ctypes.byref(free))
    return int(total.value) if ok else 0


@contextlib.contextmanager
def attached(path: str):
    """Attach 'path' read-only for the duration of the block, and yield an 'Attached'.

    A disk someone else already attached is used but never detached on exit: yanking a disk
    the user mounted on purpose is worse than leaving ours attached.
    """
    if not os.path.exists(path):
        raise RuntimeError(f"virtual disk not found: '{path}'")

    handle = open_disk(path)
    try:
        by_orca = attach_disk(handle)
        physical = physical_path(handle)
        number = disk_number(physical)
        letter = wait_for_drive_letter(number, logical_drives=logical_drives,
                                       device_number=device_number, volume_size=volume_size)
        yield Attached(path, physical, number, letter, by_orca)
    finally:
        # Closing the handle detaches, since the attachment is not permanent. For a disk
        # that was already attached, that is a no-op by design.
        _kernel32().CloseHandle(handle)
