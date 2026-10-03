"""Compare two recorded traces (tools/record.py): functions that ran in one but not the other.

Usage: python tools/trace_diff.py work/traces/A work/traces/B [--limit 40]

Times are seconds from each trace's first recorded call. For each function only seen in A, prints its
first-seen time and the call sites it was called from (return addresses), which lead to the code that decided
to take that path.
"""
import argparse
import collections
import json
import os


def load(path):
    first, callers = {}, collections.defaultdict(set)
    with open(os.path.join(path, "calls.jsonl")) as f:
        for line in f:
            r = json.loads(line)
            if r[0] != 1:
                continue
            first.setdefault(r[1], r[2])
            callers[r[1]].add(r[4])
    t0 = min(first.values())
    return {fn: (t - t0) / 1000 for fn, t in first.items()}, callers


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--limit", type=int, default=40)
    args = ap.parse_args()
    a, ca = load(args.a)
    b, cb = load(args.b)
    for name, x, y, cx in ((args.a, a, b, ca), (args.b, b, a, cb)):
        only = sorted((t, fn) for fn, t in x.items() if fn not in y)
        print(f"only in {name}: {len(only)} functions")
        for t, fn in only[:args.limit]:
            sites = ", ".join(f"0x{c:08x}" for c in sorted(cx[fn])[:4])
            print(f"  {t:7.1f}s  0x{fn:08x}  called from {sites}")


if __name__ == "__main__":
    main()
