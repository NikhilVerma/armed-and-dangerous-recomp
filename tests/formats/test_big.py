"""Every .big file dumps and rebuilds byte for byte, and no kind falls back to the raw module."""
import glob
import pathlib
import sys

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "formats"))
import bigtool  # noqa: E402

FILES = sorted(glob.glob(str(ROOT / "orig" / "pc" / "GameData" / "**" / "*.big"), recursive=True))


@pytest.mark.skipif(not FILES, reason="game files not present")
@pytest.mark.parametrize("path", FILES, ids=lambda p: pathlib.Path(p).name)
def test_roundtrip(path, tmp_path):
    kind, module = bigtool.dump(path, tmp_path)
    assert module != "big_kinds.raw", f"{kind} has no parser"
    assert bigtool.rebuild(tmp_path) == pathlib.Path(path).read_bytes()
