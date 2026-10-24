"""Update symbols/functions.csv from the recovered functions in archive/src/.

Usage: python archive/tools/tracker_sync.py [--check]

Every HOOK(addr, "Name", ...) (or a file-local *_HOOK wrapper) in archive/src/**/*.cpp is recovered C++. Rows still at S0 are
taken as compared (the per-function loop hooks a function once tools/compare.py passes with it) and get: name, subsystem (the src/
directory), stage S2 (compiles with a modern compiler and matches the original in the scenarios), approach B
(hook and replace), test "compare:menu+level" when empty. Rows at S1 (static recovery, archive/tools/mark_recovered.py)
or later keep their stage; archive/tools/promote_verified.py promotes S1. --check exits 1 if the file
would change (for archive/tools/check.ps1).
"""
import argparse
import csv
import glob
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # the repository (this tool lives in archive/tools)
CSV = os.path.join(ROOT, "symbols", "functions.csv")
STAGES = ["S0", "S1", "S2", "S3", "S4"]


def recovered():
    out = {}
    for path in glob.glob(os.path.join(ROOT, "archive", "src", "**", "*.cpp"), recursive=True):
        sub = os.path.relpath(path, os.path.join(ROOT, "archive", "src")).split(os.sep)[0]
        text = open(path, encoding="utf-8").read()
        rel = os.path.relpath(path, ROOT).replace(os.sep, "/")
        for addr, name in re.findall(r'\bHOOK\((0x[0-9a-fA-F]+),\s*"([^"]+)"', text):
            out[int(addr, 16)] = (name, sub, rel)
        # File-local wrappers of HOOK, e.g. R2A_HOOK(00420450, Name, Name_Abi) (address without 0x).
        for addr, name in re.findall(r'\b\w+_HOOK\(([0-9a-fA-F]{8}),\s*(\w+)\s*,', text):
            out[int(addr, 16)] = (name, sub, rel)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    text = open(CSV, encoding="utf-8", newline="").read()
    rows = list(csv.DictReader(io.StringIO(text)))
    fields = list(rows[0].keys())
    rec = recovered()
    by_addr = {int(r["address"], 16): r for r in rows}
    missing = [hex(x) for x in rec if x not in by_addr]
    for addr, (name, sub, path) in rec.items():
        r = by_addr.get(addr)
        if r is None:
            continue
        r["name"] = name
        r["subsystem"] = r["subsystem"] or sub
        # S1 = recovered statically, not yet (or not) verified by the comparison: archive/tools/promote_verified.py moves
        # it to S2 once the scenarios execute it. Only unrecorded rows (S0) are taken as verified here.
        if (r["stage"] or "S0") == "S0":
            r["stage"] = "S2"
        r["approach"] = r["approach"] or "B"
        r["provenance"] = r["provenance"] or f"recovered:{path}"
        r["test"] = r["test"] or "compare:menu+level"
    buf = io.StringIO()
    w = csv.DictWriter(buf, fieldnames=fields, lineterminator="\n" if "\r\n" not in text else "\r\n")
    w.writeheader()
    w.writerows(rows)
    new = buf.getvalue()
    if missing:
        print("not in functions.csv:", ", ".join(missing))
    changed = new != text
    if a.check:
        print("tracker out of date (run archive/tools/tracker_sync.py)" if changed else "tracker up to date")
        return 1 if changed else 0
    if changed:
        open(CSV, "w", encoding="utf-8", newline="").write(new)
    s2 = sum(1 for r in rows if r["stage"] in ("S2", "S3", "S4"))
    print(f"{len(rec)} recovered functions; {s2} rows at S2 or later of {len(rows)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
