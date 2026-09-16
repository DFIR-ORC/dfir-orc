import unittest
from orca import naming


class TestNaming(unittest.TestCase):
    def test_base_name_strips_dir_and_ext(self):
        self.assertEqual(naming.base_name("Foo\\Bar\\NTFSInfo.csv"), "NTFSInfo")
        self.assertEqual(naming.base_name("SystemInfo.csv"), "SystemInfo")

    def test_path_keyword_is_parent_segment_sanitized(self):
        self.assertEqual(naming.path_keyword("SystemDrive\\NTFSInfo.csv"), "SystemDrive")
        self.assertEqual(naming.path_keyword("vss 1/NTFSInfo.csv"), "vss1")
        self.assertEqual(naming.path_keyword("NTFSInfo.csv"), "")

    def test_keyword_preserves_underscores(self):
        self.assertEqual(naming.path_keyword("Browsers_complet/GetThis.csv"), "Browsers_complet")
        self.assertEqual(
            naming.assign_names(["Browsers_complet/GetThis.csv", "EXE_TMP/GetThis.csv"]),
            ["GetThis_Browsers_complet", "GetThis_EXE_TMP"])

    def test_single_instance_keeps_bare_name(self):
        self.assertEqual(naming.assign_names(["a/NTFSInfo.csv"]), ["NTFSInfo"])

    def test_multiple_use_keyword(self):
        names = naming.assign_names(["C\\NTFSInfo.csv", "D\\NTFSInfo.csv"])
        self.assertEqual(names, ["NTFSInfo_C", "NTFSInfo_D"])

    def test_collision_falls_back_to_counter(self):
        names = naming.assign_names(["x\\NTFSInfo.csv", "x\\NTFSInfo.csv"])
        self.assertEqual(names, ["NTFSInfo_x", "NTFSInfo_x_2"])

    def test_distinct_tools_unaffected(self):
        names = naming.assign_names(["a/NTFSInfo.csv", "a/SystemInfo.csv"])
        self.assertEqual(names, ["NTFSInfo", "SystemInfo"])


if __name__ == "__main__":
    unittest.main()
