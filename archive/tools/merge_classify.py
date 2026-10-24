"""Merge the static classification CSVs (work/classify/*.csv) into symbols/functions.csv.

Input files: address,subsystem,confidence,evidence,name_suggestion,calls_physics (one row per function).
Rules:
  - The subsystem fills only empty tracker cells (recovered rows keep theirs), except the corrections below.
  - Rows whose evidence starts with "D3DX" become subsystem d3dx (statically linked D3DX: a library, not recovered).
  - A suggested name replaces only an auto name (FUN_xxxxxxxx / fn_xxxxxxxx) and only at high confidence; the
    provenance column then says "classify: <evidence>".
  - Later files win over earlier ones for the same address (pass havok.csv last: it owns the library ranges).
Usage: python archive/tools/merge_classify.py work/classify/range0.csv ... work/classify/havok.csv [--dry-run]
Prints counts per subsystem of what changed. Writes LF line endings (the tracker's).
"""
import argparse
import collections
import csv
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # the repository (this tool lives in archive/tools)
TRACKER = os.path.join(ROOT, "symbols", "functions.csv")

# Evidence-backed corrections of earlier labels (address -> subsystem), from the 2026-10-05 classification.
CORRECTIONS = {
    # "renderer" range 0x004fb000+, but audio: 0x004fb360 has "Sequence Outro not setup right" and calls the audio
    # lookup 0x004f2470 (range4 classifier); its neighbours in the same object file.
    **{a: "audio" for a in range(0x004fb170, 0x004fb541)},
    0x004fb860: "movie",  # movie frame drawing, called from 0x0050e520 (movie glue)
    0x004fb920: "movie",
    0x004e80b0: "math",  # Plane_FromPoints (was loader)
    # Tagged havok on 2026-10-03 for their hk*.cpp strings, but game code: physics init (switches off five Havok
    # warnings via hkError) and the frame/reset profiler ("END Havok step") (work/classify/havok_boundary.md).
    0x00411960: "physics-glue",
    0x0044a670: "debug",
}
AUTO_NAME = re.compile(r"^(FUN|fn)_[0-9a-fA-F]{8}$")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("files", nargs="+")
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()
    found = {}
    for path in a.files:
        with open(path, newline="") as f:
            for r in csv.DictReader(f):
                found[int(r["address"], 16)] = r
    with open(TRACKER, newline="") as f:
        rows = list(csv.reader(f))
    changed = collections.Counter()
    named = 0
    for row in rows:
        if not row or not row[0].startswith("0x"):
            continue
        addr = int(row[0], 16)
        if addr in CORRECTIONS and row[3] != CORRECTIONS[addr]:
            row[3] = CORRECTIONS[addr]
            changed["corrected:" + row[3]] += 1
        r = found.get(addr)
        if not r:
            continue
        sub = r["subsystem"].strip()
        if r["evidence"].startswith("D3DX"):
            sub = "d3dx"
        if not row[3] and sub:
            row[3] = sub
            changed[sub] += 1
        name = (r.get("name_suggestion") or "").strip()
        if name and r["confidence"] == "high" and AUTO_NAME.match(row[2]) and re.match(r"^[A-Za-z_][\w]*$", name):
            row[2] = name
            row[6] = ("classify: " + r["evidence"])[:120]
            named += 1
    for k, v in sorted(changed.items(), key=lambda kv: -kv[1]):
        print(f"{k:24} {v}")
    print("named", named)
    if not a.dry_run:
        with open(TRACKER, "w", newline="") as f:
            csv.writer(f, lineterminator="\n").writerows(rows)


if __name__ == "__main__":
    main()
