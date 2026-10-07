import unittest

from orca import typeinfer

NAMES = [candidate.name for candidate in typeinfer.CANDIDATES]


def census(nonempty: int, failing: dict) -> dict:
    """A one-column census: 'failing' maps a candidate name to its failure count (else 0)."""
    row = {"n0": str(nonempty)}
    for j, name in enumerate(NAMES):
        row[f"f0_{j}"] = str(failing.get(name, nonempty))
    return row


class TestChoose(unittest.TestCase):
    def test_the_first_candidate_every_value_fits(self):
        # Fits both unsigned and signed: unsigned comes first.
        chosen = typeinfer.choose(census(3, {"ubigint": 0, "bigint": 0}), 1)
        self.assertEqual(chosen[0].name, "ubigint")

    def test_one_value_that_fits_nothing_keeps_the_column_text(self):
        self.assertEqual(typeinfer.choose(census(3, {"ubigint": 1}), 1), [None])

    def test_an_empty_column_is_not_typed(self):
        self.assertEqual(typeinfer.choose(census(0, {name: 0 for name in NAMES}), 1), [None])


class TestSql(unittest.TestCase):
    def test_one_pass_counts_every_candidate(self):
        sql = typeinfer.census_sql('"autoruns"', ['"Time"', '"Flag"'])
        self.assertEqual(sql.count(" AS n"), 2)
        self.assertEqual(sql.count(" AS f"), 2 * len(NAMES))
        self.assertTrue(sql.endswith('FROM "autoruns"'))

    def test_regex_braces_survive(self):
        guid = next(c for c in typeinfer.CANDIDATES if c.name == "uuid")
        self.assertIn("{8}", typeinfer.fill(guid.fits, '"Guid"'))
        self.assertIn('"Guid"', typeinfer.cast_sql(guid, '"Guid"'))


if __name__ == "__main__":
    unittest.main()


DUCKDB = __import__("orca.engine", fromlist=["find_duckdb"]).find_duckdb()


@unittest.skipUnless(DUCKDB, "duckdb not available")
class TestTimestampsAreFullWidth(unittest.TestCase):
    """strptime does not hold a field to its width: '11:4:22.390' parsed as 11:04:22 and
    '20261006-12055' as 12:05:05. A short field is a damaged value, not a time."""

    CASES = {
        "timestamp (ORC)": ("2013-08-22 11:45:22.390", ["2013-08-22 11:4:22.390",
                                                        "2013-8-22 11:45:22.390",
                                                        "2013-08-22 11:45:22.39"]),
        "timestamp": ("2013-08-22 11:45:22", ["2013-08-22 11:45:2"]),
        "timestamp (ISO 8601)": ("2013-08-22T11:45:22", ["2013-08-22T1:45:22"]),
        "timestamp (autorunsc)": ("20261006-120557", ["20261006-12055", "2026106-120557"]),
    }

    def test_only_full_width_text_is_a_timestamp(self):
        import subprocess
        for candidate in typeinfer.CANDIDATES:
            if candidate.name not in self.CASES:
                continue
            good, short = self.CASES[candidate.name]
            for value, expected in [(good, "true")] + [(s, "false") for s in short]:
                literal = f"'{value}'"
                sql = (f"SELECT ({typeinfer.cast_sql(candidate, literal)}) IS NOT NULL AS ok, "
                       f"coalesce({typeinfer.fill(candidate.fits, literal)}, false) AS fits")
                out = subprocess.run([DUCKDB, "-csv", "-noheader", "-c", sql], text=True,
                                     capture_output=True, check=True).stdout.strip()
                self.assertEqual(out, f"{expected},{expected}", (candidate.name, value))
