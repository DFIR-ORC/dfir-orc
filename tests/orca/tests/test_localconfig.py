import os
import tempfile
import unittest
import xml.etree.ElementTree as ET
from orca import localconfig


class TestBuild(unittest.TestCase):
    def test_empty_build_is_bare_root(self):
        root = ET.fromstring(localconfig.build())
        self.assertEqual(root.tag, "dfir-orc")
        self.assertEqual(list(root), [])

    def test_output_and_temporary(self):
        root = ET.fromstring(localconfig.build(output=r"C:\out", temporary=r"C:\tmp"))
        self.assertEqual(root.find("output").text, r"C:\out")
        self.assertEqual(root.find("temporary").text, r"C:\tmp")

    def test_recipient_carries_the_pem_text(self):
        with tempfile.TemporaryDirectory() as d:
            pem = os.path.join(d, "contoso.pem")
            with open(pem, "w", encoding="utf-8") as f:
                f.write("-----BEGIN CERTIFICATE-----\nAAAA\n-----END CERTIFICATE-----\n")
            root = ET.fromstring(localconfig.build(public_key=pem))
        recipient = root.find("recipient")
        self.assertEqual(recipient.get("name"), "test")
        self.assertEqual(recipient.get("archive"), "*")
        self.assertIn("BEGIN CERTIFICATE", recipient.text)

    def test_upload_filecopy_splits_the_uri(self):
        spec = localconfig.UploadSpec(uri="file://192.168.0.1/Orc/Current")
        upload = ET.fromstring(localconfig.build(upload=spec)).find("upload")
        self.assertEqual(upload.get("job"), "orc")
        self.assertEqual(upload.get("method"), "filecopy")
        self.assertEqual(upload.get("mode"), "sync")
        self.assertEqual(upload.get("operation"), "move")
        self.assertEqual(upload.get("server"), "file://192.168.0.1")
        self.assertEqual(upload.get("path"), "/Orc/Current")
        self.assertIsNone(upload.get("uri"))

    def test_upload_bits_is_async(self):
        spec = localconfig.UploadSpec(uri="http://srv/orc", bits=True)
        upload = ET.fromstring(localconfig.build(upload=spec)).find("upload")
        self.assertEqual(upload.get("method"), "bits")
        self.assertEqual(upload.get("mode"), "async")

    def test_upload_allow_invalid_uri_keeps_one_attribute(self):
        spec = localconfig.UploadSpec(uri="file://%COMPUTERNAME%/Orc", allow_invalid_uri=True)
        upload = ET.fromstring(localconfig.build(upload=spec)).find("upload")
        self.assertEqual(upload.get("uri"), "file://%COMPUTERNAME%/Orc")
        self.assertIsNone(upload.get("server"))

    def test_keyword_lists(self):
        xml = localconfig.build(enable_keys=["GetEvt", "GetThis"],
                                disable_keys=["Hives"], keys=["NTFSInfo"])
        root = ET.fromstring(xml)
        self.assertEqual([e.text for e in root.findall("enable_key")], ["GetEvt", "GetThis"])
        self.assertEqual([e.text for e in root.findall("disable_key")], ["Hives"])
        self.assertEqual([e.text for e in root.findall("key")], ["NTFSInfo"])

    def test_element_order_matches_new_orclocalconfig(self):
        spec = localconfig.UploadSpec(uri="file://h/s")
        with tempfile.TemporaryDirectory() as d:
            pem = os.path.join(d, "k.pem")
            with open(pem, "w", encoding="utf-8") as f:
                f.write("PEM")
            xml = localconfig.build(output=r"C:\o", temporary=r"C:\t", upload=spec,
                                    enable_keys=["A"], disable_keys=["B"], keys=["C"],
                                    public_key=pem)
        tags = [e.tag for e in ET.fromstring(xml)]
        self.assertEqual(tags, ["recipient", "upload", "enable_key", "disable_key", "key",
                                "output", "temporary"])

    def test_has_no_xml_declaration_and_is_indented(self):
        xml = localconfig.build(output=r"C:\out")
        self.assertFalse(xml.startswith("<?xml"))
        self.assertIn("\n  <output>", xml)


BASE = """<dfir-orc altitude="Highest" priority="Low">
  <output>D:\\old</output>
  <enable_key>Old1</enable_key>
  <enable_key>Old2</enable_key>
  <console><file>c.txt</file></console>
</dfir-orc>"""


class TestMerge(unittest.TestCase):
    def test_generated_element_replaces_every_same_named_one(self):
        override = localconfig.build(enable_keys=["New"])
        root = ET.fromstring(localconfig.merge(BASE, override))
        self.assertEqual([e.text for e in root.findall("enable_key")], ["New"])

    def test_untouched_elements_and_root_attributes_survive(self):
        override = localconfig.build(output=r"C:\new")
        root = ET.fromstring(localconfig.merge(BASE, override))
        self.assertEqual(root.get("altitude"), "Highest")
        self.assertEqual(root.get("priority"), "Low")
        self.assertIsNotNone(root.find("console/file"))
        self.assertEqual([e.text for e in root.findall("enable_key")], ["Old1", "Old2"])
        self.assertEqual(root.find("output").text, r"C:\new")

    def test_empty_override_changes_nothing(self):
        root = ET.fromstring(localconfig.merge(BASE, localconfig.build()))
        self.assertEqual(root.find("output").text, r"D:\old")
        self.assertEqual(len(root.findall("enable_key")), 2)

    def test_foreign_root_is_refused(self):
        with self.assertRaises(RuntimeError) as caught:
            localconfig.merge("<wolf><output>x</output></wolf>", localconfig.build())
        self.assertIn("dfir-orc", str(caught.exception))

    def test_unparsable_base_is_refused(self):
        with self.assertRaises(RuntimeError):
            localconfig.merge("<dfir-orc", localconfig.build())

    def test_merge_file_reads_utf8(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "local.xml")
            with open(path, "w", encoding="utf-8") as f:
                f.write(BASE)
            root = ET.fromstring(localconfig.merge_file(path, localconfig.build(output="O")))
        self.assertEqual(root.find("output").text, "O")

    def test_merge_file_reads_a_utf16_base_written_by_powershell(self):
        # 'Invoke-OrcOffline' writes its local.xml with Out-File, which under PowerShell 5 is
        # UTF-16LE with a BOM - and that file, left beside a previous run's results, is the
        # most obvious thing to pass to --local-config.
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "local.xml")
            with open(path, "wb") as f:
                f.write(BASE.encode("utf-16"))   # UTF-16LE with the BOM Out-File writes
            root = ET.fromstring(localconfig.merge_file(path, localconfig.build(output="O")))
        self.assertEqual(root.find("output").text, "O")
        self.assertEqual(root.get("altitude"), "Highest")

    def test_merge_file_names_a_missing_file(self):
        with self.assertRaises(RuntimeError) as caught:
            localconfig.merge_file(os.path.join("no", "such", "local.xml"), localconfig.build())
        self.assertIn("local.xml", str(caught.exception))


if __name__ == "__main__":
    unittest.main()
