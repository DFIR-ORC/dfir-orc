import csv
import io
import os
import shutil
import subprocess
import tempfile
import unittest
from contextlib import redirect_stdout
from unittest import mock

from orca import engine, fleet, fleetdb, ingest, layout, orcschema

DUCKDB = engine.find_duckdb()


def write_name_map(dataset: str, tables: dict) -> None:
    os.makedirs(layout.orca_dir(dataset), exist_ok=True)
    with open(layout.name_map_path(dataset), "w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(["name", "source_archive", "entry_path", "source_mtime"])
        for name, entry in tables.items():
            writer.writerow([name, "General.7z", entry, ""])


class TestTableFamily(unittest.TestCase):
    def test_per_volume_tables_lose_their_volume(self):
        for name in ("NTFSInfo_00000000_DiskInterface_0x4e106ff4106fe187_",
                     "NTFSInfo_00000001_Volume_C_",
                     "NTFSInfo_00000002_HarddiskVolume2_",
                     "NTFSInfo_00000003_Volume_C_{3808876b-c176-4e48-b7ae-04046e6cc752}_"):
            self.assertEqual(fleetdb.table_family(name), "NTFSInfo", name)
        self.assertEqual(fleetdb.table_family("FatInfo_00000000_DiskInterface_0x84174ccd_"),
                         "FatInfo")
        self.assertEqual(fleetdb.table_family("volstats_NTFSInfo_00000000_Volume_C_"),
                         "volstats_NTFSInfo")

    def test_other_tables_are_their_own_family(self):
        for name in ("autoruns", "processes1", "GetThis_GetThis_Default",
                     "volstats_NTFSInfo_details", "Tool_12345678_no_trailing_underscore",
                     "Tool_1234567_Volume_C_"):
            self.assertEqual(fleetdb.table_family(name), name)


class TestFamilyOf(unittest.TestCase):
    def test_from_the_collected_entry(self):
        self.assertEqual(fleetdb.family_of("NTFSInfo_00000000_Volume_C_",
                                           "NTFSInfo/NTFSInfo_00000000_Volume_C_.csv"),
                         "NTFSInfo")

    def test_a_keyword_assign_names_appended_is_kept(self):
        # Two command sets, two configurations: not one table to stack.
        self.assertEqual(fleetdb.family_of("NTFSInfo_00000000_Volume_C__NTFSInfo_quick",
                                           "NTFSInfo_quick/NTFSInfo_00000000_Volume_C_.csv"),
                         "NTFSInfo_NTFSInfo_quick")

    def test_falls_back_to_the_name(self):
        self.assertEqual(fleetdb.family_of("NTFSInfo_00000000_Volume_C_", ""), "NTFSInfo")
        self.assertEqual(fleetdb.family_of("renamed", "other.csv"), "renamed")


class TestFamilies(unittest.TestCase):
    def test_grouped_across_datasets_and_only_what_is_on_disk(self):
        with tempfile.TemporaryDirectory() as d:
            a, b = os.path.join(d, "b-host"), os.path.join(d, "a-host")
            write_name_map(a, {"NTFSInfo_00000000_Volume_C_": "NTFSInfo_00000000_Volume_C_.csv",
                               "autoruns": "autoruns.csv", "absent": "absent.csv"})
            write_name_map(b, {"NTFSInfo_00000000_Volume_D_": "NTFSInfo_00000000_Volume_D_.csv"})
            for path in (os.path.join(a, "NTFSInfo_00000000_Volume_C_.parquet"),
                         os.path.join(a, "autoruns.parquet"),
                         os.path.join(b, "NTFSInfo_00000000_Volume_D_.parquet")):
                open(path, "wb").close()

            families = fleetdb.fleet_families([a, b])

        self.assertEqual(list(families), ["autoruns", "NTFSInfo"])
        self.assertEqual([os.path.basename(os.path.dirname(p)) for p in families["NTFSInfo"]],
                         ["a-host", "b-host"])


class TestSql(unittest.TestCase):
    def test_a_view_reads_its_stacked_file_by_a_relative_path(self):
        sql = fleetdb.view_sql("NTFSInfo", "fleet/NTFSInfo.parquet")
        self.assertTrue(sql.startswith('CREATE VIEW "NTFSInfo"'))
        self.assertIn("read_parquet('fleet/NTFSInfo.parquet')", sql)

    def test_a_stack_lists_its_files_and_keeps_their_path_relative_to_the_fleet(self):
        sql = fleetdb.stack_sql(["C:\\fleet\\O'B\\x.parquet"], "C:\\fleet\\fleet\\N.parquet",
                                "C:\\fleet")
        self.assertIn("'C:/fleet/O''B/x.parquet'", sql)
        self.assertIn("union_by_name = true", sql)
        self.assertIn("filename = 'OrcParquetFile'", sql)
        self.assertIn("replace(\"OrcParquetFile\", 'C:/fleet/', '')", sql)
        self.assertIn("COMPRESSION zstd", sql)

    def test_a_table_quotes_its_values(self):
        sql = fleetdb.table_sql("orca_hosts", ["a"], [["it's"]])
        self.assertIn("('it''s')", sql)
        self.assertEqual(fleetdb.table_sql("t", ["a"], []).count("INSERT"), 0)

    def test_the_database_is_fleet_duckdb(self):
        self.assertEqual(fleetdb.database_path("C:\\work"),
                         os.path.join("C:\\work", "fleet.duckdb"))


class TestReservedNames(unittest.TestCase):
    def test_a_family_may_not_take_orcas_own_names(self):
        eng = mock.Mock()
        eng.run_in_database.side_effect = lambda database, sql: open(database, "ab").close()
        with tempfile.TemporaryDirectory() as d:
            skipped = fleetdb.build_database(eng, os.path.join(d, "x.duckdb"),
                                             {"orca_hosts": ["p.parquet"]}, {}).skipped
        self.assertEqual([family for family, _ in skipped], ["orca_hosts"])
        # Only orca's own tables were written: the family view was never attempted.
        written = "".join(call.args[1] for call in eng.run_in_database.call_args_list)
        self.assertNotIn("CREATE VIEW", written)


def make_parquet(path: str, host: str, value: str) -> None:
    os.makedirs(os.path.dirname(path), exist_ok=True)
    sql = (f"COPY (SELECT '{host}' AS OrcComputerName, '{value}' AS Value) "
           f"TO '{engine.sql_path(path)}' (FORMAT parquet);")
    subprocess.run([DUCKDB], input=sql, text=True, capture_output=True, check=True)


def query(database: str, sql: str, cwd: str) -> str:
    return subprocess.run([DUCKDB, database, "-csv", "-c", sql], text=True,
                          capture_output=True, cwd=cwd).stdout


@unittest.skipUnless(DUCKDB, "duckdb not available")
class TestBuildDatabase(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)
        self.elsewhere = tempfile.TemporaryDirectory()
        self.addCleanup(self.elsewhere.cleanup)
        self.datasets = []
        for host, volume in (("a.lab", "Volume_C"), ("b.lab", "Volume_D")):
            dataset = os.path.join(self.dir.name, host)
            name = f"NTFSInfo_00000000_{volume}_"
            write_name_map(dataset, {name: name + ".csv"})
            make_parquet(os.path.join(dataset, name + ".parquet"), host, volume)
            self.datasets.append(dataset)
        self.database = os.path.join(self.dir.name, "fleet.duckdb")

    def build(self):
        families = fleetdb.fleet_families(self.datasets)
        tables = {"orca_hosts": (["OrcComputerName"], [["a.lab"], ["b.lab"]])}
        return fleetdb.build_database(engine.DuckDbEngine(DUCKDB), self.database, families,
                                      tables)

    def test_a_family_view_reads_every_host_from_the_fleet_directory(self):
        self.assertEqual(self.build().skipped, [])

        out = query(self.database, 'SELECT OrcComputerName, Value FROM "NTFSInfo" ORDER BY 1',
                    self.dir.name)
        self.assertEqual(out.split(), ["OrcComputerName,Value", "a.lab,Volume_C",
                                       "b.lab,Volume_D"])
        # Which host file a row came from, relative to the fleet directory.
        files = query(self.database, 'SELECT OrcParquetFile FROM "NTFSInfo" ORDER BY 1',
                      self.dir.name).split()
        self.assertEqual(files[1:], ["a.lab/NTFSInfo_00000000_Volume_C_.parquet",
                                     "b.lab/NTFSInfo_00000000_Volume_D_.parquet"])

    def test_each_family_is_stacked_into_one_file(self):
        self.build()
        self.assertEqual(sorted(os.listdir(os.path.join(self.dir.name, "fleet"))),
                         [".orca", "NTFSInfo.parquet"])

    def test_the_fleet_directory_moves_whole_without_its_datasets(self):
        self.build()
        moved = os.path.join(self.elsewhere.name, "moved")
        os.makedirs(moved)
        shutil.copy(self.database, moved)
        shutil.copytree(os.path.join(self.dir.name, "fleet"), os.path.join(moved, "fleet"))
        for dataset in self.datasets:
            shutil.rmtree(dataset)

        out = query(os.path.join(moved, "fleet.duckdb"), 'SELECT count(*) AS n FROM "NTFSInfo"',
                    moved)
        self.assertEqual(out.split(), ["n", "2"])

    def test_a_current_family_file_is_not_stacked_again(self):
        self.build()
        stacked = os.path.join(self.dir.name, "fleet", "NTFSInfo.parquet")
        os.utime(stacked, (2_000_000_000, 2_000_000_000))
        self.build()
        self.assertEqual(os.path.getmtime(stacked), 2_000_000_000)

    def test_a_host_gone_restacks_the_family(self):
        self.build()
        self.datasets = self.datasets[:1]
        self.build()
        self.assertNotIn("b.lab", query(self.database, 'SELECT * FROM "NTFSInfo"', self.dir.name))

    def test_orcas_own_tables_are_there(self):
        self.build()
        self.assertIn("b.lab", query(self.database, "SELECT * FROM orca_hosts",
                                     self.dir.name))
        catalog = query(self.database, "SELECT family, hosts, files, status FROM orca_tables",
                        self.dir.name)
        self.assertIn("NTFSInfo,2,2,view", catalog)

    def test_a_value_outside_ascii_reaches_the_database(self):
        # A French Windows error message in a failed host's 'error' was sent in the locale's
        # code page, and duckdb refused the whole orca_hosts table as invalid unicode.
        families = fleetdb.fleet_families(self.datasets)
        tables = {"orca_hosts": (["OrcComputerName", "error"],
                                 [["a.lab", "Accès refusé"], ["b.lab", ""]])}

        fleetdb.build_database(engine.DuckDbEngine(DUCKDB), self.database, families, tables)

        rows = engine.DuckDbEngine(DUCKDB).query_rows_in_database(
            self.database, "SELECT error FROM orca_hosts WHERE OrcComputerName = 'a.lab'")
        self.assertEqual(rows, [{"error": "Accès refusé"}])

    def test_an_unreadable_parquet_costs_its_view_only(self):
        dataset = self.datasets[0]
        write_name_map(dataset, {"NTFSInfo_00000000_Volume_C_": "NTFSInfo_00000000_Volume_C_.csv",
                                 "autoruns": "autoruns.csv"})
        with open(os.path.join(dataset, "autoruns.parquet"), "wb") as f:
            f.write(b"not parquet")

        skipped = self.build().skipped

        self.assertEqual([family for family, _ in skipped], ["autoruns"])
        self.assertIn("a.lab", query(self.database, 'SELECT * FROM "NTFSInfo"', self.dir.name))
        self.assertIn("autoruns,1,1,skipped", query(
            self.database, "SELECT family, hosts, files, status FROM orca_tables",
            self.dir.name))
        self.assertFalse(os.path.exists(os.path.join(self.dir.name, "fleet", "autoruns.parquet")))

    def test_a_rebuild_replaces_the_previous_database(self):
        self.build()
        self.datasets = self.datasets[:1]
        self.build()
        self.assertNotIn("b.lab", query(self.database, 'SELECT * FROM "NTFSInfo"',
                                        self.dir.name))

    def test_a_failed_build_leaves_the_previous_database_whole(self):
        self.build()
        broken = mock.Mock(spec=engine.DuckDbEngine)
        broken.run_in_database.side_effect = RuntimeError("duckdb failed: disk full")
        with self.assertRaises(RuntimeError):
            fleetdb.build_database(broken, self.database, {}, {})
        # The previous database, and the stacked files it reads.
        self.assertIn("a.lab", query(self.database, 'SELECT * FROM "NTFSInfo"', self.dir.name))


FILEINFO = orcschema.SchemaTable("NTFSInfo", "fileinfo", {
    "ComputerName": "utf8", "File": "utf16", "CreationDate": "timestamp", "SizeInBytes": "uint64",
    "FRN": "uint64", "FilenameID": "uint32", "RecordInUse": "bool", "SnapshotID": "guid",
    "MD5": "binary", "VolumeID": "uint64"})
NTFS_VOLSTATS = orcschema.SchemaTable("NTFSInfo", "volstats", {"ComputerName": "utf8",
                                                               "VolumeID": "uint64"})
FAT_VOLSTATS = orcschema.SchemaTable("FATInfo", "volstats", {"ComputerName": "utf8",
                                                             "VolumeID": "uint64"})


class TestMatchTable(unittest.TestCase):
    def test_a_configurations_subset_of_columns_finds_its_table(self):
        columns = ["ComputerName", "File", "FRN", "OrcComputerName", "OrcParquetFile"]
        self.assertIs(fleetdb.match_table("NTFSInfo", columns, [FAT_VOLSTATS, FILEINFO]),
                      FILEINFO)

    def test_a_tie_goes_to_the_tool_named_in_the_family(self):
        columns = ["ComputerName", "VolumeID"]
        for family, expected in (("volstats_NTFSInfo_details", NTFS_VOLSTATS),
                                 ("volstats_FatInfo_detail", FAT_VOLSTATS)):
            self.assertIs(fleetdb.match_table(family, columns, [FAT_VOLSTATS, NTFS_VOLSTATS]),
                          expected, family)

    def test_a_table_orc_does_not_describe_has_none(self):
        columns = ["Time", "Entry Location", "Entry", "Image Path", "ComputerName"]
        self.assertIsNone(fleetdb.match_table("autoruns", columns, [FILEINFO, NTFS_VOLSTATS]))


class TestTypedViewSql(unittest.TestCase):
    def test_only_typed_columns_are_replaced(self):
        sql = fleetdb.typed_view_sql("NTFSInfo", fleetdb.schema_casts(
            ["File", "CreationDate", "FRN", "MD5"], FILEINFO))
        self.assertIn('CREATE VIEW "NTFSInfo_typed"', sql)
        # Held to the full-width shape before it is parsed (see TestTimestampsAreFullWidth).
        self.assertIn('regexp_full_match("CreationDate"', sql)
        self.assertIn('try_strptime("CreationDate", \'%Y-%m-%d %H:%M:%S.%g\') END AS '
                      '"CreationDate"', sql)
        self.assertIn('try_cast("FRN" AS UBIGINT) AS "FRN"', sql)
        self.assertNotIn('"File"', sql)
        self.assertNotIn('"MD5"', sql)

    def test_nothing_to_type_is_no_view(self):
        self.assertIsNone(fleetdb.typed_view_sql("x", fleetdb.schema_casts(["File", "MD5"],
                                                                           FILEINFO)))


@unittest.skipUnless(DUCKDB, "duckdb not available")
class TestTypedViews(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)
        dataset = os.path.join(self.dir.name, "a.lab")
        name = "NTFSInfo_00000000_Volume_C_"
        write_name_map(dataset, {name: name + ".csv"})
        path = os.path.join(dataset, name + ".parquet")
        rows = ("SELECT * FROM (VALUES "
                "('a.lab', 'ok.exe', '2013-08-22 11:45:22.390', '2048', '0x00010000000044DE', "
                "'2', 'Y', '{00000000-0000-0000-0000-000000000001}', 'C67E'), "
                "('a.lab', 'forged.exe', '2013-08-22 11:4', '0x10', 'not a number', "
                "'', 'N', '', '')) AS t(ComputerName, File, CreationDate, SizeInBytes, FRN, "
                "FilenameID, RecordInUse, SnapshotID, MD5)")
        subprocess.run([DUCKDB], input=f"COPY ({rows}) TO '{engine.sql_path(path)}' "
                                       f"(FORMAT parquet);", text=True, check=True,
                       capture_output=True)
        self.database = os.path.join(self.dir.name, "fleet.duckdb")
        self.families = fleetdb.fleet_families([dataset])

    def build(self, schemas, infer=False):
        typing = orcschema.Typing(schemas, infer) if schemas or infer else None
        return fleetdb.build_database(engine.DuckDbEngine(DUCKDB), self.database, self.families,
                                      {}, typing)

    def test_the_typed_view_reads_orcs_renderings(self):
        built = self.build(orcschema.Schemas("test schema", [FILEINFO]))
        self.assertEqual((built.skipped, built.typed), ([], 1))

        out = query(self.database, "SELECT File, CreationDate, SizeInBytes, FRN, FilenameID, "
                                   "RecordInUse, SnapshotID, MD5 FROM NTFSInfo_typed ORDER BY 1",
                    self.dir.name)
        self.assertEqual(out.splitlines()[1:3], [
            # The truncated timestamp, the non-number and the empty cells are NULL, not invented.
            "forged.exe,NULL,16,NULL,NULL,false,NULL,",
            "ok.exe,2013-08-22 11:45:22.39,2048,281474976728286,2,true,"
            "00000000-0000-0000-0000-000000000001,C67E"])
        types = query(self.database, "SELECT column_name, column_type FROM "
                                     "(DESCRIBE NTFSInfo_typed)", self.dir.name)
        for column, kind in (("CreationDate", "TIMESTAMP"), ("FRN", "UBIGINT"),
                             ("FilenameID", "UINTEGER"), ("RecordInUse", "BOOLEAN"),
                             ("SnapshotID", "UUID"), ("MD5", "VARCHAR"),
                             ("OrcParquetFile", "VARCHAR")):
            self.assertIn(f"{column},{kind}", types)

    def test_the_text_view_keeps_what_was_collected(self):
        self.build(orcschema.Schemas("test schema", [FILEINFO]))
        out = query(self.database, "SELECT CreationDate FROM NTFSInfo WHERE File = 'forged.exe'",
                    self.dir.name)
        self.assertIn("2013-08-22 11:4", out)

    def test_the_schema_and_its_source_are_recorded(self):
        self.build(orcschema.Schemas("C:/orc/DFIR-ORC_x64.exe", [FILEINFO]))
        catalog = query(self.database, "SELECT view, status, schema FROM orca_tables",
                        self.dir.name)
        self.assertIn("NTFSInfo_typed,typed,NTFSInfo.fileinfo", catalog)
        self.assertIn("C:/orc/DFIR-ORC_x64.exe", query(self.database, "SELECT * FROM orca_schema",
                                                       self.dir.name))

    def older(self):
        # The schema of an ORC that did not have FilenameID yet.
        table = orcschema.SchemaTable("NTFSInfo", "fileinfo", {
            k: v for k, v in FILEINFO.columns.items() if k != "FilenameID"})
        return orcschema.Schemas(orcschema.BUNDLED_SOURCE, [table])

    def test_schema_only_leaves_a_newer_orcs_column_text_and_records_it(self):
        built = self.build(self.older())

        self.assertEqual(built.outdated, [("NTFSInfo", ["FilenameID"])])
        self.assertIn("FilenameID,VARCHAR", query(
            self.database, "SELECT column_name, column_type FROM (DESCRIBE NTFSInfo_typed)",
            self.dir.name))
        self.assertIn("NTFSInfo_typed,typed,FilenameID", query(
            self.database, "SELECT view, status, not_in_schema FROM orca_tables", self.dir.name))

    def test_auto_reads_a_newer_orcs_column_from_its_values(self):
        built = self.build(self.older(), infer=True)

        self.assertEqual(built.outdated, [("NTFSInfo", ["FilenameID"])])
        types = query(self.database, "SELECT column_name, column_type FROM "
                                     "(DESCRIBE NTFSInfo_typed)", self.dir.name)
        self.assertIn("FilenameID,UBIGINT", types)       # its values: '2' and ''
        self.assertIn("CreationDate,TIMESTAMP", types)   # still the schema's
        self.assertIn("NTFSInfo_typed,typed,NTFSInfo.fileinfo + inferred", query(
            self.database, "SELECT view, status, schema FROM orca_tables", self.dir.name))

    def test_auto_does_not_read_what_the_schema_already_types(self):
        eng = engine.DuckDbEngine(DUCKDB)
        with mock.patch.object(eng, "query_rows_in_database",
                               wraps=eng.query_rows_in_database) as census:
            fleetdb.build_database(eng, self.database, self.families, {},
                                   orcschema.Typing(orcschema.Schemas("s", [FILEINFO]), True))
        census.assert_not_called()                       # no scan: every column is known
        self.assertIn("NTFSInfo_typed,NTFSInfo.fileinfo", query(
            self.database, "SELECT view, schema FROM orca_tables", self.dir.name))

    def test_no_schema_no_typed_view(self):
        self.assertEqual(self.build(None).typed, 0)


class TestOutdatedSchema(unittest.TestCase):
    """A newer ORC adds columns (282 added, one retyped, since 2020): a column the schema does not
    know is how a collection newer than the bundled schema shows."""

    def test_columns_the_schema_does_not_know(self):
        columns = ["File", "CreationDate", "NewColumn", "OrcComputerName", "OrcParquetFile"]
        self.assertEqual(fleetdb.unknown_columns(columns, FILEINFO), ["NewColumn"])

    def test_the_bundled_schema_points_at_orc(self):
        printed = io.StringIO()
        with redirect_stdout(printed):
            fleet.report_outdated([("NTFSInfo", ["NewColumn"])], orcschema.Typing(
                orcschema.Schemas(orcschema.BUNDLED_SOURCE, []), True))
        self.assertIn("NTFSInfo: NewColumn", printed.getvalue())
        self.assertIn("typed from their values", printed.getvalue())
        self.assertIn("pass --schema", printed.getvalue())

    def test_schema_only_says_they_stay_text(self):
        printed = io.StringIO()
        with redirect_stdout(printed):
            fleet.report_outdated([("NTFSInfo", ["NewColumn"])], orcschema.Typing(
                orcschema.Schemas(orcschema.BUNDLED_SOURCE, []), False))
        self.assertIn("they stay text", printed.getvalue())

    def test_a_given_binary_is_not_told_to_give_one(self):
        printed = io.StringIO()
        with redirect_stdout(printed):
            fleet.report_outdated([("NTFSInfo", ["NewColumn"])], orcschema.Typing(
                orcschema.Schemas("C:/orc/DFIR-ORC_x64.exe", []), True))
        self.assertIn("NTFSInfo: NewColumn", printed.getvalue())
        self.assertNotIn("pass --schema", printed.getvalue())

    def test_nothing_unknown_says_nothing(self):
        printed = io.StringIO()
        with redirect_stdout(printed):
            fleet.report_outdated([], orcschema.Typing(
                orcschema.Schemas(orcschema.BUNDLED_SOURCE, []), True))
        self.assertEqual(printed.getvalue(), "")


@unittest.skipUnless(DUCKDB, "duckdb not available")
class TestInferredTypes(unittest.TestCase):
    """Types read from the values: what --schema auto does for a table no schema describes, and
    --schema infer for every table."""

    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)
        self.datasets = []
        guid = "00000000-0000-0000-0000-00000000000"
        hosts = {
            "a.lab": [("20261006-120557", "Y", "12", "0x1F", "-3", "{" + guid + "1}", "x", ""),
                      ("20261006-120558", "N", "007", "0x20", "4", guid + "2", "1.2", "")],
            "b.lab": [("20261006-120559", "TRUE", "5", "31", "5", "", "y", ""),
                      ("20261006-12055", "Y", "6", "32", "6", "", "z", "")]}
        for host, rows in hosts.items():
            dataset = os.path.join(self.dir.name, host)
            write_name_map(dataset, {"autoruns": "autoruns.csv"})
            values = ", ".join("(" + ", ".join(f"'{v}'" for v in row) + ")" for row in rows)
            select = (f"SELECT * FROM (VALUES {values}) AS t(Time, Flag, Count, Hex, Signed, Guid, "
                      f"Text, Empty)")
            path = os.path.join(dataset, "autoruns.parquet")
            subprocess.run([DUCKDB], input=f"COPY ({select}) TO '{engine.sql_path(path)}' "
                                           f"(FORMAT parquet);", text=True, check=True,
                           capture_output=True)
            self.datasets.append(dataset)
        self.database = os.path.join(self.dir.name, "fleet.duckdb")

    def test_each_column_takes_the_type_every_value_fits(self):
        built = fleetdb.build_database(engine.DuckDbEngine(DUCKDB), self.database,
                                       fleetdb.fleet_families(self.datasets), {},
                                       orcschema.Typing(None, True))
        self.assertEqual(built.typed, 1)
        types = dict(line.split(",") for line in query(
            self.database, "SELECT column_name, column_type FROM (DESCRIBE autoruns_typed)",
            self.dir.name).splitlines()[1:])

        # One host's '20261006-12055' fits no timestamp format: the column stays text, fleet-wide.
        self.assertEqual(types["Time"], "VARCHAR")
        self.assertEqual(types["Flag"], "BOOLEAN")
        self.assertEqual(types["Count"], "UBIGINT")
        self.assertEqual(types["Hex"], "UBIGINT")
        self.assertEqual(types["Signed"], "BIGINT")
        self.assertEqual(types["Guid"], "UUID")      # empty cells do not count against a type
        self.assertEqual(types["Text"], "VARCHAR")
        self.assertEqual(types["Empty"], "VARCHAR")  # nothing to infer from

    def test_the_inference_is_recorded(self):
        fleetdb.build_database(engine.DuckDbEngine(DUCKDB), self.database,
                               fleetdb.fleet_families(self.datasets), {},
                               orcschema.Typing(None, True))
        self.assertIn("autoruns_typed,typed,inferred", query(
            self.database, "SELECT view, status, schema FROM orca_tables", self.dir.name))
        self.assertIn("inferred from the values", query(
            self.database, "SELECT * FROM orca_schema", self.dir.name))

    def test_auto_infers_a_table_no_schema_describes(self):
        typing = orcschema.Typing(orcschema.Schemas(orcschema.BUNDLED_SOURCE, [FILEINFO]), True)
        built = fleetdb.build_database(engine.DuckDbEngine(DUCKDB), self.database,
                                       fleetdb.fleet_families(self.datasets), {}, typing)
        self.assertEqual(built.typed, 1)
        self.assertIn("autoruns_typed,inferred", query(
            self.database, "SELECT view, schema FROM orca_tables", self.dir.name))
        self.assertIn("bundled with orca, then inferred from the values", query(
            self.database, "SELECT * FROM orca_schema", self.dir.name))


class TestRunFleetWritesTheDatabase(unittest.TestCase):
    def test_where_the_fleet_bookkeeping_goes(self):
        with tempfile.TemporaryDirectory() as d, tempfile.TemporaryDirectory() as out:
            collection = ingest.Collection(None, [], "a.lab", "{A}", "T")
            result = fleet.FleetResult([(collection, os.path.join(out, "ds"))], [])
            with mock.patch.object(fleet.fleetdb, "build_database",
                                   return_value=fleetdb.BuildResult([], 0, [])) as build, \
                    mock.patch.object(fleet, "get_engine"), redirect_stdout(io.StringIO()):
                self.assertTrue(fleet.write_database(out, [collection], result))

            database = build.call_args.args[1]
            self.assertEqual(database, os.path.join(out, "fleet.duckdb"))
            self.assertEqual(set(build.call_args.args[3]), {"orca_hosts", "orca_coverage"})

    def test_a_database_that_cannot_be_written_is_reported_not_raised(self):
        collection = ingest.Collection(None, [], "a.lab", "{A}", "T")
        result = fleet.FleetResult([(collection, "ds")], [])
        printed = io.StringIO()
        with mock.patch.object(fleet.fleetdb, "build_database",
                               side_effect=RuntimeError("it is open")), \
                mock.patch.object(fleet, "get_engine"), redirect_stdout(printed):
            self.assertFalse(fleet.write_database("out", [collection], result))
        self.assertIn("could not write the fleet database", printed.getvalue())

    def test_no_dataset_no_database(self):
        with mock.patch.object(fleet.fleetdb, "build_database") as build:
            self.assertTrue(fleet.write_database("out", [], fleet.FleetResult([], [])))
        build.assert_not_called()


if __name__ == "__main__":
    unittest.main()
