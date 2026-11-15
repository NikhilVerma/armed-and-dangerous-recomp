"""The release's tool list is complete: every module the packaged tools import, and every script they run, is
packaged too (tools/package_release.py TOOLS). A missing one would only show on a player's machine."""
import ast
import pathlib
import re
import sys
import unittest

TOOLS_DIR = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS_DIR))
import package_release  # noqa: E402


def needed(name):
    """The tools/*.py modules NAME imports, plus the tools scripts it names as strings (run as subprocesses)."""
    src = (TOOLS_DIR / f"{name}.py").read_text(encoding="utf-8")
    found = set()
    for node in ast.walk(ast.parse(src)):
        if isinstance(node, ast.Import):
            found.update(a.name.split(".")[0] for a in node.names)
        elif isinstance(node, ast.ImportFrom) and node.module:
            found.add(node.module.split(".")[0])
    found.update(pathlib.Path(s).stem for s in re.findall(r"[\"']([\w/]+\.py)[\"']", src))
    return {m for m in found if (TOOLS_DIR / f"{m}.py").is_file()}


class ReleaseToolList(unittest.TestCase):
    def test_every_needed_tool_is_packaged(self):
        packaged = set(package_release.TOOLS)
        for name in sorted(packaged):
            self.assertTrue((TOOLS_DIR / f"{name}.py").is_file(), f"tools/{name}.py is listed but missing")
            self.assertFalse(needed(name) - packaged, f"tools/{name}.py needs tools that are not packaged")

    def test_listed_files_exist(self):
        for rel in package_release.FILES:
            self.assertTrue((package_release.ROOT / rel).is_file(), rel)


if __name__ == "__main__":
    unittest.main()
