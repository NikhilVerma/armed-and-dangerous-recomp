"""Progress of the parallel static recovery (docs/handover-recovery.md), derived from the files only:
symbols/recovery_batches.json (batch -> file, addresses), the HOOK lines in each batch's file, and the tracker stages.

Per batch: functions, hooked in its file, tracker stage counts (S1 = recovered, compile-checked; S2 = verified by
tools/compare.py), and a status: not started / partial / recovered (all hooked or skipped) / verified (no S1 left
among the executed ones is not knowable here: S2 count shown). Totals at the end, plus the overall tracker figures.

Usage: python archive/tools/recovery_status.py [--markdown]
"""
import argparse
import collections
import csv
import json
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # the repository (this tool lives in archive/tools)
LIBRARY = {"havok", "qhull", "d3dx", "crt", "zlib", "thunk"}


def hooks_in(path):
    if not os.path.exists(path):
        return None
    text = open(path, encoding="utf-8", errors="ignore").read()
    found = {int(a, 16) for a in re.findall(r"\bHOOK\((0x[0-9a-fA-F]+),", text)}
    found |= {int(a, 16) for a in re.findall(r"\b\w+_HOOK\(([0-9a-fA-F]{8}),", text)}
    return found


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--markdown", action="store_true")
    a = ap.parse_args()
    batches = json.load(open(os.path.join(ROOT, "symbols", "recovery_batches.json")))
    with open(os.path.join(ROOT, "symbols", "functions.csv"), newline="") as f:
        rows = {int(r[0], 16): r for r in csv.reader(f) if r and r[0].startswith("0x")}
    lines = []
    tot = collections.Counter()
    for key, b in batches.items():
        # Functions fenced off as library code after the batches were made (subsystem havok / qhull / ...; e.g. the
        # hkInertiaTensorComputer block inside physics_03 / physics_04, docs/subsystems/physics.md) are not game code:
        # count them as "fenced", not as S0.
        all_addrs = [int(x, 16) for x in b["addrs"]]
        # Addresses without a tracker row were removed as not functions (padding / mid-function, journal 2026-10-06).
        addrs = [x for x in all_addrs if x in rows and rows[x][3] not in LIBRARY]
        fenced = len([x for x in all_addrs if x in rows and rows[x][3] in LIBRARY])
        tot["fenced"] += fenced
        hooks = hooks_in(os.path.join(ROOT, b["file"] + ".cpp"))
        stages = collections.Counter(rows[x][4] for x in addrs if x in rows)
        hooked = len([x for x in addrs if hooks and x in hooks])
        if hooks is None:
            status = "not started"
        elif stages.get("S0", 0) == 0:
            status = "recovered"
        else:
            status = "partial"
        tot["functions"] += len(addrs)
        tot["hooked"] += hooked
        for s, n in stages.items():
            tot[s] += n
        if fenced:
            status += f" (+{fenced} fenced library)"
        lines.append((key, len(addrs), hooked, stages.get("S0", 0), stages.get("S1", 0), stages.get("S2", 0), status))
    if a.markdown:
        print("| Batch | Functions | Hooked | S0 | S1 | S2 | Status |\n| --- | ---: | ---: | ---: | ---: | ---: | --- |")
        for l in lines:
            print("| " + " | ".join(str(x) for x in l) + " |")
    else:
        for l in lines:
            print(f"{l[0]:12} {l[1]:4} funcs  hooked {l[2]:4}  S0 {l[3]:4} S1 {l[4]:4} S2 {l[5]:4}  {l[6]}")
    print(f"\nbatches total: {tot['functions']} game functions, {tot['hooked']} hooked; "
          f"S0 {tot['S0']}, S1 {tot['S1']}, S2 {tot['S2']}; {tot['fenced']} fenced as library code (not counted)")
    game = [r for r in rows.values() if r[3] not in LIBRARY]
    st = collections.Counter(r[4] for r in game)
    print(f"tracker, game code (libraries excluded): {len(game)} functions: " +
          ", ".join(f"{k} {v}" for k, v in sorted(st.items())))


if __name__ == "__main__":
    main()
