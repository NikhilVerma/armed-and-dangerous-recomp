"""After a passing comparison (tools/compare.py --scenario all): promote S1 functions that the comparison scenarios
actually execute (recorded traces, work/traces/*/calls.jsonl, as tools/recover.py reports) to S2, test
"compare:<label>". S1 functions the traces never reach stay S1 ("not executed: compare cannot verify").

Usage: python tools/promote_verified.py --label all-2026-10-05 [--dry-run]
"""
import argparse
import csv
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from recover import traced_functions  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TRACKER = os.path.join(ROOT, "symbols", "functions.csv")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--label", required=True)
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()
    traced = traced_functions()
    with open(TRACKER, newline="") as f:
        rows = list(csv.reader(f))
    promoted = unverifiable = 0
    for r in rows:
        if not r or not r[0].startswith("0x") or r[4] != "S1":
            continue
        if int(r[0], 16) in traced:
            r[4], r[7] = "S2", f"compare:{a.label}"
            promoted += 1
        else:
            r[7] = "not executed: compare cannot verify"
            unverifiable += 1
    print(f"promoted to S2: {promoted}; S1 not executed by the scenarios: {unverifiable}")
    if not a.dry_run:
        with open(TRACKER, "w", newline="") as f:
            csv.writer(f, lineterminator="\n").writerows(rows)


if __name__ == "__main__":
    main()
