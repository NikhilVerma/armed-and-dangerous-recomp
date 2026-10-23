"""Add rows to symbols/functions.csv for functions the tracker is missing (Ghidra never identified them; found by
archive/tools/find_missing_functions.py or reported by recovery agents), keeping address order.

Input CSV lines: address,size,name,subsystem,role[,source file]. A row whose address is hooked in the given source
file (or anywhere in src/ when no file is given) is added at stage S1 ("recovered <file>: <role>"), otherwise S0.
Existing addresses are left alone (reported). Writes LF line endings.
Usage: python archive/tools/add_tracker_rows.py <rows.csv>
"""
import csv
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # the repository (this tool lives in archive/tools)
TRACKER = os.path.join(ROOT, "symbols", "functions.csv")


def hooked():
    out = {}
    for p in glob.glob(os.path.join(ROOT, "archive", "src", "**", "*.cpp"), recursive=True):
        t = open(p, encoding="utf-8", errors="ignore").read()
        for a in re.findall(r"\bHOOK\((0x[0-9a-fA-F]+),", t):
            out[int(a, 16)] = os.path.relpath(p, ROOT).replace(os.sep, "/")
    return out


def main():
    with open(TRACKER, newline="") as f:
        rows = list(csv.reader(f))
    header, body = rows[0], [r for r in rows[1:] if r]
    have = {int(r[0], 16) for r in body}
    hooks = hooked()
    added = 0
    with open(sys.argv[1], newline="") as f:
        for r in csv.reader(f):
            if not r or not r[0].strip().lower().startswith("0x"):
                continue
            a = int(r[0], 16)
            if a in have:
                print(f"exists: {a:#010x}")
                continue
            size, name, sub, role = r[1].strip(), r[2].strip(), r[3].strip(), r[4].strip()
            src = hooks.get(a)
            row = [f"{a:#010x}", size, name, sub, "S1" if src else "S0", "B" if src else "",
                   (f"recovered {os.path.basename(src)}: {role}" if src else f"untracked: {role}")[:160],
                   "pending compare" if src else "", "", ""]
            body.append(row[:len(header)])
            have.add(a)
            added += 1
    body.sort(key=lambda r: int(r[0], 16))
    with open(TRACKER, "w", newline="") as f:
        csv.writer(f, lineterminator="\n").writerows([header] + body)
    print(f"added {added}")


if __name__ == "__main__":
    main()
