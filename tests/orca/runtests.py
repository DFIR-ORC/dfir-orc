"""Run the orca test suite with bytecode relocated to %TEMP% (keeps the tree clean)."""
import os
import sys
import tempfile
import unittest

# Same relocation as orca.py; must precede test discovery/imports.
sys.pycache_prefix = os.path.join(tempfile.gettempdir(), "orca-pycache")
here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)

suite = unittest.TestLoader().discover(os.path.join(here, "tests"))
sys.exit(0 if unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful() else 1)
