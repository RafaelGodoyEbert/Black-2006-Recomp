"""Compile the production throttle and register accessors without an audio device."""
import sys
from pathlib import Path

source = Path(sys.argv[1]).read_text()


def function(signature):
    start = source.index(signature)
    # Each selected top-level function ends with an unindented closing brace.
    end = source.index("\n}", start) + 2
    return source[start:end] + "\n"


Path(sys.argv[2]).write_text("\n".join(function(signature) for signature in (
    "uint64_t mcpx_apu_read(",
    "void mcpx_apu_write(",
    "static void throttle(",
)))
