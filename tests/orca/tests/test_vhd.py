import os
import unittest
from orca import vhd


class TestIsVhd(unittest.TestCase):
    def test_extension_decides_case_insensitively(self):
        self.assertTrue(vhd.is_vhd(r"C:\i\w10.vhd"))
        self.assertTrue(vhd.is_vhd(r"C:\i\w10.VHDX"))
        self.assertFalse(vhd.is_vhd(r"C:\i\w10.dd"))
        self.assertFalse(vhd.is_vhd(r"C:\i\w10"))


class TestDiskNumber(unittest.TestCase):
    def test_parses_the_physical_drive_number(self):
        self.assertEqual(vhd.disk_number(r"\\.\PhysicalDrive3"), 3)
        self.assertEqual(vhd.disk_number(r"\\.\PHYSICALDRIVE11"), 11)

    def test_an_unexpected_path_is_refused(self):
        with self.assertRaises(RuntimeError) as caught:
            vhd.disk_number(r"\\.\CdRom0")
        self.assertIn("CdRom0", str(caught.exception))


class TestFindDriveLetter(unittest.TestCase):
    def test_single_volume_on_the_attached_disk(self):
        letter = vhd.find_drive_letter(
            3, ["C:", "F:"],
            device_number=lambda drive: {"C:": 0, "F:": 3}[drive],
            volume_size=lambda drive: 100)
        self.assertEqual(letter, "F:")

    def test_several_volumes_the_largest_wins(self):
        letter = vhd.find_drive_letter(
            2, ["C:", "E:", "F:"],
            device_number=lambda drive: {"C:": 0, "E:": 2, "F:": 2}[drive],
            volume_size=lambda drive: {"C:": 9, "E:": 50, "F:": 900}[drive])
        self.assertEqual(letter, "F:")

    def test_an_unreadable_volume_is_skipped_not_fatal(self):
        # A volume whose device number cannot be read (no media, access denied) must not
        # abort the search: the one we want may be the next letter.
        letter = vhd.find_drive_letter(
            1, ["A:", "G:"],
            device_number=lambda drive: None if drive == "A:" else 1,
            volume_size=lambda drive: 10)
        self.assertEqual(letter, "G:")

    def test_no_volume_is_an_error_naming_the_disk(self):
        with self.assertRaises(RuntimeError) as caught:
            vhd.find_drive_letter(7, ["C:"], device_number=lambda drive: 0,
                                  volume_size=lambda drive: 1)
        self.assertIn("7", str(caught.exception))


class TestAttachParameters(unittest.TestCase):
    def test_the_struct_is_the_full_eight_bytes_win32_reads(self):
        # ATTACH_VIRTUAL_DISK_PARAMETERS is { Version; union { struct { ULONG Reserved; } } }.
        # Handing AttachVirtualDisk a bare 4-byte DWORD makes it read Reserved from past the
        # end of the allocation, so the call fails with ERROR_INVALID_PARAMETER whenever the
        # adjacent bytes happen to be nonzero.
        import ctypes
        self.assertEqual(ctypes.sizeof(vhd.AttachParameters), 8)
        parameters = vhd.attach_parameters()
        self.assertEqual(parameters.Version, vhd.ATTACH_VIRTUAL_DISK_VERSION_1)
        self.assertEqual(parameters.Version1.Reserved, 0)


class TestWaitForDriveLetter(unittest.TestCase):
    def test_a_letter_that_appears_late_is_still_found(self):
        # AttachVirtualDisk returns once the disk is surfaced; volume arrival and the mount
        # manager's letter assignment are asynchronous, so the first look can see nothing.
        looks = []
        slept = []

        def drives():
            looks.append(1)
            return ["C:"] if len(looks) == 1 else ["C:", "F:"]

        letter = vhd.wait_for_drive_letter(
            3, logical_drives=drives,
            device_number=lambda drive: {"C:": 0, "F:": 3}[drive],
            volume_size=lambda drive: 10, attempts=5, delay=0.2,
            sleep=slept.append)
        self.assertEqual(letter, "F:")
        self.assertEqual(slept, [0.2])

    def test_it_gives_up_and_names_the_disk(self):
        slept = []
        with self.assertRaises(RuntimeError) as caught:
            vhd.wait_for_drive_letter(7, logical_drives=lambda: ["C:"],
                                      device_number=lambda drive: 0,
                                      volume_size=lambda drive: 1, attempts=3, delay=0.1,
                                      sleep=slept.append)
        self.assertIn("7", str(caught.exception))
        self.assertEqual(len(slept), 2)   # slept between attempts, not after the last


class TestImportsOnAnyPlatform(unittest.TestCase):
    def test_module_imports_without_windows(self):
        # The suite runs on Linux; ctypes/DLL lookups must be inside the functions.
        self.assertTrue(hasattr(vhd, "attached"))


@unittest.skipUnless(os.name == "nt", "needs Windows and an elevated shell")
class TestAttachReal(unittest.TestCase):
    def test_attaching_a_missing_file_raises(self):
        with self.assertRaises(RuntimeError):
            with vhd.attached(r"C:\no\such\disk.vhdx"):
                pass


if __name__ == "__main__":
    unittest.main()
