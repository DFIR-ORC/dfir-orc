import glob
import io
import os
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from unittest import mock

from orca import orcschema

HERE = os.path.dirname(os.path.abspath(__file__))
BUILD = os.path.abspath(os.path.join(HERE, "..", "..", "..", "build"))

DOCUMENT = b"""<?xml version="1.0" encoding="utf-8"?>
<sqlschema tool="NTFSInfo">
  <table key="fileinfo">
    <utf8 name="ComputerName" maxlen="50" />
    <timestamp name="CreationDate" />
    <uint64 name="FRN" fmt="0x{:016X}" />
  </table>
  <table key="volstats">
    <guid name="SnapshotID" />
  </table>
</sqlschema>
"""


def built_binaries(pattern: str) -> list[str]:
    """ORC binaries this checkout has built, if any: the resource tests read real ones."""
    return sorted(glob.glob(os.path.join(BUILD, "**", pattern), recursive=True))


class TestParse(unittest.TestCase):
    def test_tables_keys_and_column_types(self):
        tables = orcschema.parse_schema(DOCUMENT)
        self.assertEqual([(t.tool, t.key) for t in tables],
                         [("NTFSInfo", "fileinfo"), ("NTFSInfo", "volstats")])
        self.assertEqual(tables[0].columns, {"ComputerName": "utf8", "CreationDate": "timestamp",
                                             "FRN": "uint64"})

    def test_the_bundled_copy_describes_every_tool(self):
        schemas = orcschema.load()
        self.assertEqual(schemas.source, orcschema.BUNDLED_SOURCE)
        tools = {t.tool for t in schemas.tables}
        self.assertTrue({"NTFSInfo", "USNInfo", "GetThis", "FATInfo"} <= tools, tools)


class TestBundledCopy(unittest.TestCase):
    def test_it_is_the_source_trees(self):
        """The bundled copy is what an orca without --orc types a fleet with: when it falls behind
        the source, a collection from this very build reads as 'made by a newer ORC'. Refresh it
        by copying src/OrcCommand/Command/*/*Schema.xml into tests/orca/orca/schemas/."""
        source = orcschema.schema_files(orcschema.SOURCE_SCHEMAS, nested=True)
        if not source:
            self.skipTest("orca is not in its source tree")
        bundled = orcschema.schema_files(orcschema.BUNDLED_SCHEMAS, nested=False)
        self.assertEqual([os.path.basename(p) for p in bundled],
                         sorted(os.path.basename(p) for p in source))
        for path in source:
            copy = os.path.join(orcschema.BUNDLED_SCHEMAS, os.path.basename(path))
            with open(path, "rb") as a, open(copy, "rb") as b:
                self.assertEqual(a.read(), b.read(), f"{os.path.basename(path)} is out of date")


class TestLoad(unittest.TestCase):
    def test_orc_given_is_used(self):
        with mock.patch.object(orcschema, "executable_documents", return_value=[DOCUMENT]):
            schemas = orcschema.load(orc="C:/orc/DFIR-ORC_x64.exe")
        self.assertIn("DFIR-ORC_x64.exe", schemas.source)
        self.assertEqual([t.key for t in schemas.tables], ["fileinfo", "volstats"])

    def test_orc_given_fails_loudly(self):
        # It was asked for: falling back to the bundled copy would type with the wrong schema.
        with mock.patch.object(orcschema, "executable_documents",
                               side_effect=RuntimeError("holds no schema")):
            with self.assertRaises(RuntimeError):
                orcschema.load(orc="C:/orc/notepad.exe")

    def test_no_binary_is_looked_for_without_orc(self):
        with mock.patch.object(orcschema, "executable_documents") as read:
            self.assertEqual(orcschema.load().source, orcschema.BUNDLED_SOURCE)
        read.assert_not_called()


class TestResolve(unittest.TestCase):
    def test_auto_is_the_default_bundled_then_the_values(self):
        typing = orcschema.resolve()
        self.assertEqual(typing.schemas.source, orcschema.BUNDLED_SOURCE)
        self.assertTrue(typing.infer)
        self.assertEqual(typing.source, "bundled with orca, then inferred from the values")

    def test_the_alternatives(self):
        bundled = orcschema.resolve("bundled")
        self.assertEqual((bundled.schemas.source, bundled.infer), (orcschema.BUNDLED_SOURCE, False))
        infer = orcschema.resolve("infer")
        self.assertEqual((infer.schemas, infer.infer), (None, True))
        self.assertEqual(infer.source, orcschema.INFERRED_SOURCE)

    def test_a_binary_replaces_the_bundled_schemas_and_still_infers_the_rest(self):
        with tempfile.TemporaryDirectory() as d:
            binary = os.path.join(d, "DFIR-ORC_x64.exe")
            open(binary, "wb").close()
            with mock.patch.object(orcschema, "executable_documents", return_value=[DOCUMENT]):
                typing = orcschema.resolve(binary)
        self.assertEqual(typing.schemas.source, os.path.abspath(binary))
        self.assertTrue(typing.infer)

    def test_anything_else_is_refused_by_name(self):
        with self.assertRaisesRegex(RuntimeError, "auto.*bundled.*infer.*ORC binary"):
            orcschema.resolve("C:/no/such/DFIR-ORC.exe")


class TestOlderPython(unittest.TestCase):
    def test_a_capsule_needs_zstd_and_says_what_to_give_instead(self):
        # orca stays standard-library only: before 3.14 there is no zstd to unpack a runner with.
        with mock.patch.dict(sys.modules, {"compression": None}):
            with self.assertRaisesRegex(RuntimeError, "Python 3.14.*DFIR-ORC_x64.exe"):
                orcschema._zstd_decompress(orcschema.ZSTD_MAGIC)


@unittest.skipUnless(sys.platform == "win32", "reads PE resources with the Windows loader")
class TestRealBinaries(unittest.TestCase):
    def test_a_per_architecture_binary(self):
        found = built_binaries("DFIR-ORC_x64.exe")
        if not found:
            self.skipTest("no DFIR-ORC_x64.exe built in this checkout")
        tables = orcschema.parse_all(orcschema.executable_documents(found[0]), found[0]).tables
        self.assertIn(("NTFSInfo", "fileinfo"), [(t.tool, t.key) for t in tables])

    def test_a_capsule(self):
        found = [p for p in built_binaries("DFIR-ORC*.exe") if "_" not in os.path.basename(p)]
        if not found:
            self.skipTest("no capsule DFIR-ORC.exe built in this checkout")
        if sys.version_info < (3, 14):
            with self.assertRaisesRegex(RuntimeError, "Python 3.14"):
                orcschema.executable_documents(found[0])
            return
        tables = orcschema.parse_all(orcschema.executable_documents(found[0]), found[0]).tables
        self.assertIn(("USNInfo", "USNInfo"), [(t.tool, t.key) for t in tables])

    def test_something_that_is_not_orc_is_refused(self):
        with self.assertRaisesRegex(RuntimeError, "neither ORC schemas nor a capsule runner"):
            orcschema.executable_documents(sys.executable)


if __name__ == "__main__":
    unittest.main()
