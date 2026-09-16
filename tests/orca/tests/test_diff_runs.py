import os
import tempfile
import unittest
from orca import diff


class TestMatch(unittest.TestCase):
    def test_match_parquet_pairs_and_singletons(self):
        with tempfile.TemporaryDirectory() as a, tempfile.TemporaryDirectory() as b:
            for d in (a, b):
                os.makedirs(os.path.join(d, "parquet"))
            for n in ("NTFSInfo", "USNInfo"):
                open(os.path.join(a, "parquet", n + ".parquet"), "w").close()
            for n in ("NTFSInfo", "NewTool"):
                open(os.path.join(b, "parquet", n + ".parquet"), "w").close()
            common, only_a, only_b = diff._match_parquet(a, b)
        self.assertEqual(sorted(common), ["NTFSInfo"])
        self.assertEqual(only_a, ["USNInfo"])
        self.assertEqual(only_b, ["NewTool"])


if __name__ == "__main__":
    unittest.main()
