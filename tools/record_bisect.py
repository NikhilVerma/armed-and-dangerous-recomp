"""Find functions that crash the game when hooked by the recorder, by bisection. Appends each culprit to
tools/frida/unhookable.json so tools/record.py skips it.

Usage: python tools/record_bisect.py [--seconds 25] [--max-culprits 10]

A run "fails" when and.log reports CRASH or the game dies before the time limit. Each probe is time-capped.
Uses the current and.ini (set skip_intro so the front end is reached quickly).
"""
import argparse
import json
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAME_DIR = os.path.join(ROOT, "orig", "pc", "GameData")
PY = sys.executable
UNHOOKABLE = os.path.join(ROOT, "tools", "frida", "unhookable.json")
SUBSET = os.path.join(ROOT, "work", "bisect-subset.json")


def all_targets():
    funcs = json.load(open(os.path.join(ROOT, "work", "exports", "functions.json")))
    never = {int(x, 16) for x in json.load(open(UNHOOKABLE))}
    return [int(f["address"], 16) for f in funcs
            if not f["thunk"] and f["size"] >= 5 and int(f["address"], 16) < 0x00538000
            and int(f["address"], 16) not in never]


def fails(subset, seconds):
    json.dump(subset, open(SUBSET, "w"))
    log = os.path.join(GAME_DIR, "and.log")
    if os.path.exists(log):
        os.remove(log)
    subprocess.run([PY, "-u", os.path.join(ROOT, "tools", "record.py"), "--name", "bisect", "--kill", str(seconds),
                    "--only", SUBSET], cwd=ROOT, capture_output=True, timeout=seconds + 180)
    text = open(log, encoding="latin1").read() if os.path.exists(log) else ""
    bad = "CRASH" in text or "CreateDevice ->" not in text
    print(f"  probe {len(subset):5} functions -> {'FAIL' if bad else 'ok'}", flush=True)
    return bad


def find_one(subset, seconds):
    """subset fails; return one function that fails alone with ... (assumes a single culprit per half)."""
    while len(subset) > 1:
        half = len(subset) // 2
        a, b = subset[:half], subset[half:]
        if fails(a, seconds):
            subset = a
        elif fails(b, seconds):
            subset = b
        else:
            print("  both halves pass: interaction between halves; keeping the smaller search space")
            return None
    return subset[0]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seconds", type=float, default=25)
    ap.add_argument("--max-culprits", type=int, default=10)
    a = ap.parse_args()
    for _ in range(a.max_culprits):
        targets = all_targets()
        print(f"testing all {len(targets)} functions")
        if not fails(targets, a.seconds):
            print("no crash with the remaining set: done")
            return 0
        culprit = find_one(targets, a.seconds)
        if culprit is None:
            return 1
        never = json.load(open(UNHOOKABLE))
        never.append(f"0x{culprit:08x}")
        json.dump(never, open(UNHOOKABLE, "w"), indent=1)
        print(f"culprit: 0x{culprit:08x} (added to unhookable.json)", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
