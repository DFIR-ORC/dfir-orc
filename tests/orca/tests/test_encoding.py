import codecs
import io
import json
import os
import tempfile
import unittest
from contextlib import redirect_stdout

from orca import encoding, engine, ingest

DUCKDB = engine.find_duckdb()


def write_bytes(directory: str, name: str, data: bytes) -> str:
    path = os.path.join(directory, name)
    with open(path, "wb") as f:
        f.write(data)
    return path


class TestBomEncoding(unittest.TestCase):
    def test_utf16_le_and_be_are_recognised(self):
        with tempfile.TemporaryDirectory() as d:
            le = write_bytes(d, "le.csv", "a,b\r\n".encode("utf-16"))
            be = write_bytes(d, "be.csv", codecs.BOM_UTF16_BE + "a,b\r\n".encode("utf-16-be"))
            self.assertEqual(encoding.bom_encoding(le), "utf-16")
            self.assertEqual(encoding.bom_encoding(be), "utf-16")

    def test_utf8_is_left_to_duckdb(self):
        with tempfile.TemporaryDirectory() as d:
            self.assertIsNone(encoding.bom_encoding(write_bytes(d, "a.csv", b"a,b\r\n")))
            self.assertIsNone(encoding.bom_encoding(
                write_bytes(d, "b.csv", codecs.BOM_UTF8 + b"a,b\r\n")))


class TestTypeLine(unittest.TestCase):
    def test_only_powershells_type_line_is_recognised(self):
        with tempfile.TemporaryDirectory() as d:
            self.assertTrue(encoding.starts_with_type_line(
                write_bytes(d, "a.csv", b"#TYPE System.Object\r\n\"A\"\r\n")))
            self.assertTrue(encoding.starts_with_type_line(
                write_bytes(d, "b.csv", codecs.BOM_UTF8 + b"#TYPE X\r\n")))
            self.assertFalse(encoding.starts_with_type_line(
                write_bytes(d, "c.csv", b"#Fields,TYPE\r\n")))
            self.assertFalse(encoding.starts_with_type_line(write_bytes(d, "d.csv", b"")))

    def test_the_rest_is_copied_byte_for_byte(self):
        with tempfile.TemporaryDirectory() as d:
            src = write_bytes(d, "a.csv", b"#TYPE X\r\n\"A\"\r\n\"1\xff024\"\r\n")
            dst = encoding.drop_first_line(src, os.path.join(d, "b.csv"))
            with open(dst, "rb") as f:
                self.assertEqual(f.read(), b"\"A\"\r\n\"1\xff024\"\r\n")


class TestHostCodepage(unittest.TestCase):
    def outline(self, directory: str, system: dict) -> str:
        path = os.path.join(directory, "X_Outline.json")
        with open(path, "w", encoding="utf-8") as f:
            json.dump({"dfir-orc": {"outline": {"system": system}}}, f)
        return path

    def test_any_windows_codepage_python_knows(self):
        with tempfile.TemporaryDirectory() as d:
            for number, codec in ((850, "cp850"), (437, "cp437"), (866, "cp866"),
                                  (932, "cp932"), (936, "gbk"), (65001, "utf-8")):
                self.assertEqual(encoding.host_codepage([self.outline(d, {"codepage": number})]),
                                 codec)

    def test_none_without_a_usable_codepage(self):
        with tempfile.TemporaryDirectory() as d:
            self.assertIsNone(encoding.host_codepage([self.outline(d, {})]))
            self.assertIsNone(encoding.host_codepage([self.outline(d, {"codepage": 99999})]))
            self.assertIsNone(encoding.host_codepage([os.path.join(d, "missing.json")]))


@unittest.skipUnless(DUCKDB, "duckdb not available")
class TestAnyEncodingToParquet(unittest.TestCase):
    """The files a fleet of French domain controllers' collections could not ingest right."""

    def convert(self, directory: str, src: str, codepage=None) -> list[str]:
        eng = engine.DuckDbEngine(DUCKDB)
        parquet = os.path.join(directory, "out.parquet")
        ingest.csv_to_parquet_any_encoding(eng, src, parquet, None, codepage)
        rows = eng.query_rows(f"SELECT * FROM read_parquet('{engine.sql_path(parquet)}')", 10)
        return [list(row.values()) for row in rows]

    def test_utf16_autoruns(self):
        with tempfile.TemporaryDirectory() as d:
            src = write_bytes(d, "autoruns.csv",
                              "Entry,Signer\r\nRun,(Vérifié) Microsoft\r\n"
                              .encode("utf-16"))
            self.assertEqual(self.convert(d, src), [["Run", "(Vérifié) Microsoft"]])

    def test_rows_shorter_than_the_header_still_split_into_columns(self):
        """autorunsc omits the last field of some rows: 55 of 784 on a 2008R2 DC. The sniffer
        then read every line as one column named 'Time,Entry Location,Entry,...'."""
        header = "Time,Entry Location,Entry,Enabled,Image Path,SHA-256\r\n"
        full = "20261006-120557,HKLM\\Run,autochk,enabled,c:\\windows\\autochk.exe,ab12\r\n"
        short = '20261006-120557,HKLM\\BootExecute,,,"Boot Execute"\r\n'
        text = header + (full * 3 + short) * 5
        with tempfile.TemporaryDirectory() as d:
            src = write_bytes(d, "autoruns.csv", text.encode("utf-16"))
            eng = engine.DuckDbEngine(DUCKDB)
            parquet = os.path.join(d, "out.parquet")
            ingest.csv_to_parquet_any_encoding(eng, src, parquet, None, None)

            self.assertEqual(eng.columns(parquet), ["Time", "Entry Location", "Entry", "Enabled",
                                                    "Image Path", "SHA-256"])
            self.assertEqual(eng.count(parquet), 20)

    def test_a_one_column_table_is_not_re_read(self):
        with tempfile.TemporaryDirectory() as d:
            src = write_bytes(d, "AD_computers.csv", b'"DNSHostName"\r\n"DC.lab"\r\n')
            eng = engine.DuckDbEngine(DUCKDB)
            self.assertFalse(ingest.collapsed_to_one_column(
                eng, src, self.convert_to(d, eng, src), None))

    def convert_to(self, directory: str, eng, src: str) -> str:
        parquet = os.path.join(directory, "out.parquet")
        eng.csv_to_parquet(src, parquet)
        return parquet

    def test_each_hosts_console_codepage(self):
        """Not one guess for everyone: the same text written by French, US, Russian and
        Japanese hosts each reads back right through that host's own codepage."""
        samples = (("cp850", "Mémoire", "1 024 Mo"),
                   ("cp437", "Memory", "1,024 MB"),
                   ("cp866", "Память", "1 024 МБ"),
                   ("cp932", "メモリ", "1,024 MB"))
        for codec, header, value in samples:
            with self.subTest(codec=codec), tempfile.TemporaryDirectory() as d:
                text = f'"{header}"\r\n"{value}"\r\n'
                src = write_bytes(d, "Systeminfo.csv", text.encode(codec))
                self.assertEqual(self.convert(d, src, codec), [[value]])

    def test_no_codepage_still_produces_the_table(self):
        with tempfile.TemporaryDirectory() as d:
            src = write_bytes(d, "Systeminfo.csv", b'"M"\r\n"1\xff024"\r\n')
            with redirect_stdout(io.StringIO()) as out:
                self.assertEqual(self.convert(d, src), [["1ÿ024"]])
            self.assertIn("warning", out.getvalue())

    def test_powershells_type_line_is_not_taken_for_the_header(self):
        """ConvertTo-Csv without -NoTypeInformation, as the RODC's AD_computers.csv was: the
        header came out as '#TYPE Selected...ADComputer' and 'DNSHostName' as the first row."""
        text = ("#TYPE Selected.Microsoft.ActiveDirectory.Management.ADComputer\r\n"
                "\"DNSHostName\"\r\n\"DC.lab\"\r\n\"PC-Vérif.lab\"\r\n")
        for label, data, codepage in (("utf-8", text.encode("utf-8"), None),
                                      ("utf-8 bom", codecs.BOM_UTF8 + text.encode("utf-8"), None),
                                      ("utf-16", text.encode("utf-16"), None),
                                      ("cp850", text.encode("cp850"), "cp850")):
            with self.subTest(encoding=label), tempfile.TemporaryDirectory() as d:
                src = write_bytes(d, "AD_computers.csv", data)
                eng = engine.DuckDbEngine(DUCKDB)
                parquet = os.path.join(d, "out.parquet")
                ingest.csv_to_parquet_any_encoding(eng, src, parquet, None, codepage)
                self.assertEqual(eng.columns(parquet), ["DNSHostName"])
                self.assertEqual(self.convert(d, src, codepage),
                                 [["DC.lab"], ["PC-Vérif.lab"]])

    def test_a_message_saved_as_csv_is_reported_by_what_it_says(self):
        with tempfile.TemporaryDirectory() as d:
            src = write_bytes(d, "AD_computers.csv",
                              b"WARNING: Error initializing default drive: 'Unable to find a "
                              b"default server \r\nwith Active Directory Web Services running.'.\n")
            with self.assertRaises(RuntimeError) as caught:
                self.convert(d, src, "cp850")
            self.assertIn("not a CSV, it starts with: 'WARNING: Error initializing",
                          str(caught.exception))


if __name__ == "__main__":
    unittest.main()
