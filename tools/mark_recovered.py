"""Record statically recovered functions in symbols/functions.csv (stage S1: written and compile-checked, the
behavioural comparison still pending).

Input: a CSV file with lines `address,name,subsystem,role` (the recovery agents' report format; a header line is
optional). Each row sets name, subsystem, stage S1, approach B, provenance "recovered <file>: <role>" (truncated)
and test "pending compare". Rows already at S2 or later are left alone. Prints what changed.
Usage: python tools/mark_recovered.py <list.csv> <source file, e.g. src/renderer/render_r2b.cpp>
       python tools/mark_recovered.py --hooks <source file> <subsystem>   (every HOOK() in the file)
"""
import csv
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TRACKER = os.path.join(ROOT, "symbols", "functions.csv")


def from_hooks(source, subsystem):
    """address -> (name, subsystem, role) for every HOOK(addr, "Name", ...) registration in a source file."""
    with open(source, encoding="utf-8") as f:
        text = f.read()
    found = {int(a, 16): n for a, n in re.findall(r'\bHOOK\((0x[0-9a-fA-F]+),\s*"(\w+)"', text)}
    # File-local wrappers such as R2A_HOOK(00420450, Name, Name_Abi) (address without 0x).
    found.update({int(a, 16): n for a, n in re.findall(r'\b\w+_HOOK\(([0-9a-fA-F]{8}),\s*(\w+)\s*,', text)})
    return {a: (n, subsystem, f"see {os.path.basename(source)}") for a, n in found.items()}


def main():
    # mark_recovered.py <list.csv> <source>  |  mark_recovered.py --hooks <source> <subsystem>
    if sys.argv[1] == "--hooks":
        source = sys.argv[2]
        wanted = from_hooks(source, sys.argv[3])
    else:
        listing, source = sys.argv[1], sys.argv[2]
        wanted = {}
        with open(listing, newline="") as f:
            for r in csv.reader(f):
                if len(r) < 3 or not r[0].strip().lower().startswith("0x"):
                    continue
                wanted[int(r[0], 16)] = (r[1].strip(), r[2].strip(), ",".join(r[3:]).strip())
    with open(TRACKER, newline="") as f:
        rows = list(csv.reader(f))
    done = 0
    for row in rows:
        if not row or not row[0].startswith("0x"):
            continue
        a = int(row[0], 16)
        if a not in wanted or row[4] in ("S2", "S3", "S4"):
            continue
        name, sub, role = wanted.pop(a)
        if sub == "keep":  # --hooks <file> keep: leave the classified subsystem
            sub = row[3]
        row[2], row[3], row[4], row[5] = name, sub, "S1", "B"
        row[6] = (f"recovered {os.path.basename(source)}: {role}")[:160]
        row[7] = "pending compare"
        done += 1
    with open(TRACKER, "w", newline="") as f:
        csv.writer(f, lineterminator="\n").writerows(rows)
    print(f"marked {done}")
    for a in wanted:
        print(f"not in tracker or already verified: {a:#010x}")


if __name__ == "__main__":
    main()
