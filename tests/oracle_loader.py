"""Load the current source parser without modifying its behavior."""
import pathlib
import sys

def load(source):
    source = pathlib.Path(source).resolve()
    sys.path.insert(0, str(source/'src' if (source/'src/fast_parse_time').is_dir() else source))
    import fast_parse_time
    import logging
    logging.disable(logging.CRITICAL)
    return fast_parse_time
