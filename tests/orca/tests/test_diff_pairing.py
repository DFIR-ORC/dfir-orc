import unittest
from orca import diff


def row(**kw):
    base = {"FRN": "0x1", "FilenameID": "0", "DataID": "0", "Name": "a", "Size": "1", "Date": "d"}
    base.update(kw)
    return base

COLS = ["FRN", "FilenameID", "DataID", "Name", "Size", "Date"]


class FakeEngine:
    """Returns canned rows and records the SQL it was handed."""

    def __init__(self, added, removed):
        self.added, self.removed = added, removed
        self.sql = []

    def query_rows(self, select_sql, limit):
        self.sql.append(select_sql)
        return (self.added if len(self.sql) == 1 else self.removed)[:limit]


class TestKeyColumns(unittest.TestCase):
    def test_prefix_match_on_table_name(self):
        keys = diff.key_columns_for("NTFSInfo_00000001_VolumeImage_vss.dd_", COLS)
        self.assertEqual(keys, ["FRN", "FilenameID", "DataID"])

    def test_absent_columns_are_dropped(self):
        self.assertEqual(diff.key_columns_for("NTFSInfo_x", ["FRN", "Name"]), ["FRN"])

    def test_unknown_tool_disables_pairing(self):
        self.assertEqual(diff.key_columns_for("volstats_NTFSInfo_details", COLS), [])

    def test_override_wins(self):
        self.assertEqual(diff.key_columns_for("NTFSInfo_x", COLS, ["Name"]), ["Name"])

    def test_override_keeps_only_known_columns(self):
        self.assertEqual(diff.key_columns_for("NTFSInfo_x", COLS, ["Name", "Nope"]), ["Name"])


class TestAnchorColumns(unittest.TestCase):
    def test_prefers_readable_labels_over_the_numeric_key(self):
        cols = ["File", "FullName", "FRN", "FilenameID", "DataID"]
        anchors = diff.anchor_columns_for(cols, ["FRN", "FilenameID", "DataID"])
        self.assertEqual(anchors, ["File", "FullName", "FRN"])

    def test_falls_back_to_the_key_without_a_name_column(self):
        anchors = diff.anchor_columns_for(["ID", "Hash", "SDDL"], ["ID", "VolumeID"])
        self.assertEqual(anchors, ["ID", "VolumeID"])

    def test_does_not_repeat_a_label_that_is_also_the_key(self):
        self.assertEqual(diff.anchor_columns_for(["Name", "X"], ["Name"]), ["Name"])


class TestPairRows(unittest.TestCase):
    def test_pairs_on_key_and_reports_leftovers(self):
        added = [row(FRN="0x1", Size="2"), row(FRN="0x9")]
        removed = [row(FRN="0x1", Size="1"), row(FRN="0x7")]
        pairs, un_add, un_rem = diff.pair_rows(added, removed, ["FRN"])
        self.assertEqual(len(pairs), 1)
        self.assertEqual(pairs[0][0]["Size"], "2")
        self.assertEqual(pairs[0][1]["Size"], "1")
        self.assertEqual([r["FRN"] for r in un_add], ["0x9"])
        self.assertEqual([r["FRN"] for r in un_rem], ["0x7"])

    def test_duplicate_keys_pair_in_order_and_leave_the_extra(self):
        added = [row(Size="2"), row(Size="3")]
        removed = [row(Size="1")]
        pairs, un_add, un_rem = diff.pair_rows(added, removed, ["FRN"])
        self.assertEqual(len(pairs), 1)
        self.assertEqual([r["Size"] for r in un_add], ["3"])
        self.assertEqual(un_rem, [])

    def test_no_key_means_no_pairing(self):
        pairs, un_add, un_rem = diff.pair_rows([row()], [row(Size="2")], [])
        self.assertEqual(pairs, [])
        self.assertEqual(len(un_add), 1)
        self.assertEqual(len(un_rem), 1)

    def test_unpaired_removed_keeps_input_order(self):
        removed = [row(FRN="0x3"), row(FRN="0x2")]
        _, _, un_rem = diff.pair_rows([], removed, ["FRN"])
        self.assertEqual([r["FRN"] for r in un_rem], ["0x3", "0x2"])


class TestChangedColumns(unittest.TestCase):
    def test_lists_only_differing_columns_in_column_order(self):
        sig = diff.changed_columns(row(Size="2", Date="x"), row(), COLS)
        self.assertEqual(sig, ("Size", "Date"))

    def test_missing_value_counts_as_empty(self):
        self.assertEqual(diff.changed_columns({"A": ""}, {}, ["A"]), ())


class TestGrouping(unittest.TestCase):
    def test_groups_by_signature_most_frequent_first(self):
        pairs = [(row(FRN="0x1", Size="2"), row(FRN="0x1")),
                 (row(FRN="0x2", Date="z"), row(FRN="0x2")),
                 (row(FRN="0x3", Date="y"), row(FRN="0x3"))]
        groups = diff.group_pairs_by_change(pairs, COLS)
        self.assertEqual([sig for sig, _ in groups], [("Date",), ("Size",)])
        self.assertEqual(len(groups[0][1]), 2)


class TestRenderModificationGroups(unittest.TestCase):
    def setUp(self):
        self.groups = [(("Size",), [(row(Size="2"), row())]),
                       (("Date",), [(row(Date="z"), row())])]

    def test_names_every_changed_column_set(self):
        out = diff.render_modification_groups(self.groups, ["FRN"])
        self.assertIn("changed: Size", out)
        self.assertIn("changed: Date", out)

    def test_shows_key_and_changed_columns_only(self):
        out = diff.render_modification_groups(self.groups, ["FRN"])
        self.assertIn("FRN", out)
        self.assertNotIn("FilenameID", out)

    def test_marks_new_and_reference_rows(self):
        lines = diff.render_modification_groups(self.groups, ["FRN"]).splitlines()
        self.assertTrue(any(line.lstrip().startswith("+ ") for line in lines))
        self.assertTrue(any(line.lstrip().startswith("- ") for line in lines))

    def test_caps_the_number_of_patterns(self):
        many = [((f"C{i}",), [(row(), row())]) for i in range(12)]
        out = diff.render_modification_groups(many, ["FRN"], max_groups=3)
        self.assertIn("9 more change pattern", out)

    def test_caps_rows_inside_a_group(self):
        big = [(("Size",), [(row(Size="2"), row())] * 5)]
        out = diff.render_modification_groups(big, ["FRN"], rows_per_group=2)
        self.assertIn("showing 2", out)

    def test_empty_signature_is_labelled(self):
        out = diff.render_modification_groups([((), [(row(), row())])], ["FRN"])
        self.assertIn("duplicate", out.lower())


class TestSampleTablePairing(unittest.TestCase):
    def dt(self, added, removed):
        return {"compatible": True, "added": added, "removed": removed, "compare_cols": COLS,
                "path_a": "a.parquet", "path_b": "b.parquet"}

    def test_grouped_output_when_rows_pair_up(self):
        engine = FakeEngine([row(Size="2")], [row(Size="1")])
        out = diff.sample_table(engine, self.dt(1, 1), 20, name="NTFSInfo_x")
        self.assertIn("1 modified", out)
        self.assertIn("changed: Size", out)

    def test_orders_both_sides_by_the_key(self):
        engine = FakeEngine([row(Size="2")], [row(Size="1")])
        diff.sample_table(engine, self.dt(1, 1), 20, name="NTFSInfo_x")
        self.assertEqual(len(engine.sql), 2)
        for sql in engine.sql:
            self.assertIn('ORDER BY "FRN", "FilenameID", "DataID"', sql)

    def test_falls_back_to_plain_rows_without_a_key(self):
        engine = FakeEngine([row(Size="2")], [row(Size="1")])
        out = diff.sample_table(engine, self.dt(1, 1), 20, name="Unknown_x")
        self.assertNotIn("modified", out)
        self.assertNotIn("ORDER BY", engine.sql[0])

    def test_unpaired_rows_are_still_shown(self):
        engine = FakeEngine([row(FRN="0x1", Size="2"), row(FRN="0x9")], [row(FRN="0x1")])
        out = diff.sample_table(engine, self.dt(2, 1), 20, name="NTFSInfo_x")
        self.assertIn("1 modified", out)
        self.assertIn("unpaired", out)
        self.assertIn("0x9", out)

    def test_pure_additions_keep_the_plain_table(self):
        engine = FakeEngine([row(), row(FRN="0x2")], [])
        out = diff.sample_table(engine, self.dt(2, 0), 20, name="USNInfo_x")
        self.assertNotIn("modified", out)
        self.assertIn("FilenameID", out)


if __name__ == "__main__":
    unittest.main()
