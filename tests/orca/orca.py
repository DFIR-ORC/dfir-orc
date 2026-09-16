"""orca CLI launcher: relocate bytecode to %TEMP%, then run orca.cli.main."""
import os
import sys
import tempfile

# Redirect all generated __pycache__/*.pyc to a %TEMP% mirror tree so the
# working tree (on the Z: SMB share) stays clean and no .pyc is written over
# the network. Must run before importing the orca package.
sys.pycache_prefix = os.path.join(tempfile.gettempdir(), "orca-pycache")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from orca.cli import main

if __name__ == "__main__":
    sys.exit(main())
