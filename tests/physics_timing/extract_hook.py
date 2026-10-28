"""Build the authored wrapper without retail code or assets."""
from pathlib import Path
import sys

source = Path(sys.argv[1]).read_text(encoding="utf-8")
start = source.index("/* BEGIN BLACK_PC_PHYSICS_TIMING")
end = source.index("/* END BLACK_PC_PHYSICS_TIMING */", start)
Path(sys.argv[2]).write_text(source[start:end], encoding="utf-8")
