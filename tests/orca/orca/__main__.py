"""orca CLI entry for `python -m orca`. Implementation lives in orca.cli."""
import sys

from orca.cli import main

if __name__ == "__main__":
    sys.exit(main())
