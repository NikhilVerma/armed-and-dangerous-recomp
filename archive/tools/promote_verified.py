"""After a passing comparison (tools/compare.py --scenario all): promote S1 functions that the comparison scenarios
actually execute (recorded traces, work/traces/*/calls.jsonl, as archive/tools/recover.py reports) to S2, test
"compare:<label>". S1 functions the traces never reach stay S1 ("not executed: compare cannot verify").

--coverage work/coverage.json (tools/coverage.py) adds the functions the scenarios call according to the hook DLL's
counting stubs. Prefer it: it counts virtual and indirect calls too, and recovered functions (which archive/tools/record.py
skips, so traces recorded after their recovery never show them).

Usage: python archive/tools/promote_verified.py --label all-2026-10-05 [--coverage work/coverage.json] [--dry-run]
"""
import argparse
import csv
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(1, os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), "tools"))  # compare.py, instances.py (shared with the oracle tools)
from recover import traced_functions  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # the repository (this tool lives in archive/tools)
TRACKER = os.path.join(ROOT, "symbols", "functions.csv")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--label", required=True)
    ap.add_argument("--coverage", help="work/coverage.json from tools/coverage.py")
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()
    traced = set(traced_functions())
    if a.coverage:
        import json
        covered = {int(v["address"], 16) for v in json.load(open(a.coverage)).values() if v["calls"]}
        print(f"coverage: {len(covered)} functions executed; {len(covered - traced)} not in the traces")
        traced |= covered
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
