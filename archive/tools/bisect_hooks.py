"""Find which recovered functions fail the comparison test.

Usage: python archive/tools/bisect_hooks.py [--files archive/src/loader/auto_*.cpp ...] [--scenario all]

Every HOOK in the given files is a candidate; all other recovered functions stay enabled as a known-good
base (pass --base-off to test candidates alone). Each file is tested as a group; failing groups are split
in halves until single functions remain. Writes work/bisect-result.json: {"failing": [...], "passing": [...]}.
Assumes failures are independent (a function fails on its own); an interaction shows up as a group that fails
while both halves pass, and is reported as such.
"""
import argparse
import glob
import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # the repository (this tool lives in archive/tools)
PY = sys.executable


def hooks_in(path):
    text = open(path, encoding="utf-8").read()
    names = re.findall(r'HOOK\(0x[0-9a-fA-F]+,\s*"([^"]+)"', text)
    # file-local wrapper macros, as tools/compare.py hook_names()
    names += [n for _, n in re.findall(r'^\s*[A-Z0-9_]+_HOOK\(([0-9a-fA-F]{8}),\s*(\w+)', text, re.M)]
    return names


def all_hooks():
    names = []
    for p in glob.glob(os.path.join(ROOT, "archive", "src", "**", "*.cpp"), recursive=True):
        names += hooks_in(p)
    return names


def passes(enabled, scenario):
    # The list goes through a file: ~2000 hook names exceed the Windows command-line limit (WinError 206).
    listing = os.path.join(ROOT, "work", "bisect-only.txt")
    with open(listing, "w") as f:
        f.write(",".join(enabled))
    r = subprocess.run([PY, "-u", os.path.join(ROOT, "tools", "compare.py"), "--scenario", scenario, "--only",
                        "@" + listing], cwd=ROOT, capture_output=True, text=True, timeout=1800)
    last = [l for l in r.stdout.splitlines() if l.strip()][-2:] if r.stdout else ["(no output)"]
    print(f"    {len(enabled)} enabled -> {'PASS' if r.returncode == 0 else 'FAIL'}  {last[0].strip()[:110]}", flush=True)
    return r.returncode == 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--files", nargs="+", required=True)
    ap.add_argument("--scenario", default="all")
    ap.add_argument("--base-off", action="store_true")
    a = ap.parse_args()

    files = []
    for pattern in a.files:
        files += glob.glob(os.path.join(ROOT, pattern))
    candidates = {os.path.relpath(f, ROOT): hooks_in(f) for f in sorted(files)}
    all_candidate = {n for v in candidates.values() for n in v}
    base = [] if a.base_off else [n for n in all_hooks() if n not in all_candidate]
    print(f"base: {len(base)} known-good functions; candidates: {len(all_candidate)} in {len(candidates)} files")

    failing, passing, interactions = [], [], []

    def search(group, label):
        print(f"  testing {label} ({len(group)})", flush=True)
        if passes(base + group, a.scenario):
            passing.extend(group)
            return
        if len(group) == 1:
            failing.extend(group)
            return
        half = len(group) // 2
        before = len(failing)
        search(group[:half], label + "/a")
        search(group[half:], label + "/b")
        if len(failing) == before:
            interactions.append(group)

    for f, names in candidates.items():
        if names:
            search(names, f)
    result = {"failing": failing, "passing": passing, "interactions": interactions}
    json.dump(result, open(os.path.join(ROOT, "work", "bisect-result.json"), "w"), indent=1)
    print(json.dumps({k: len(v) for k, v in result.items()}))
    print("failing:", failing)


if __name__ == "__main__":
    sys.exit(main())
