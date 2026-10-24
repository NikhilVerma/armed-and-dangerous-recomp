"""Unit tests of tools/platform_paths.py and gamefiles.find_ignoring_case (no game data needed).

    python -m unittest discover -s tools -p "test_*.py"
"""
import os
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import platform_paths  # noqa: E402
from gamefiles import find_ignoring_case  # noqa: E402


class PlatformPaths(unittest.TestCase):
    def test_engine_exe_is_this_systems(self):
        exe = platform_paths.engine_exe("target-x")
        self.assertEqual(exe.parent, platform_paths.ROOT / "engine" / "target-x" / "release")
        self.assertEqual(exe.name, "and_remaster.exe" if os.name == "nt" else "and_remaster")

    def test_app_data_dir_is_named_for_the_game(self):
        d = platform_paths.app_data_dir()
        if d is not None:
            self.assertEqual(d.name, "AndRemaster")


class FindIgnoringCase(unittest.TestCase):
    def test_bank_spelling_finds_the_file(self):
        with tempfile.TemporaryDirectory() as t:
            base = pathlib.Path(t)
            (base / "Streams").mkdir()
            (base / "Streams" / "Amb_Door.RAW").write_bytes(b"x")
            found = find_ignoring_case(base, "streams\\amb_door.raw")
            self.assertTrue(found.is_file())
            self.assertEqual(found.read_bytes(), b"x")

    def test_missing_file_gives_the_exact_spelling(self):
        with tempfile.TemporaryDirectory() as t:
            base = pathlib.Path(t)
            self.assertEqual(find_ignoring_case(base, "streams/none.raw"), base / "streams" / "none.raw")


if __name__ == "__main__":
    unittest.main()
