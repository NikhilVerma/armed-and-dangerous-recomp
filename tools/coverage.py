"""Which registered functions do the comparison scenarios execute? Runs every scenario of tools/compare.py with
the original code (all hooks switched off) and [test] hook_coverage=1, and merges the call counts.

Usage: python tools/coverage.py [--scenario all] [--range 0x004fb000-0x0050d000]

Writes work/coverage.json {name: {"address", "calls"}} and prints executed / not executed per range. A
function that is never executed passes the comparison test trivially: its recovery is compiled, not verified.
"""
import argparse
import json
import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import compare  # noqa: E402

ROOT = compare.ROOT if hasattr(compare, "ROOT") else os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def unsafe_hooked():
    """Hooked functions where a branch lands inside their first 5 bytes (archive/tools/record.py unsafe_to_patch)."""
    import re
    cache = os.path.join(ROOT, "work", "coverage_unsafe.json")
    try:
        sys.path.insert(0, os.path.join(ROOT, "archive", "tools"))
        import record  # archive/tools/record.py; needs capstone, pefile and frida (the .venv python)
    except ImportError:
        print(f"note: no capstone/frida here; using {cache}")
        return {f"{int(a, 16):08x}" for a in json.load(open(cache))}
    funcs = json.load(open(os.path.join(ROOT, "work", "exports", "functions.json")))
    hooked = set()
    for dirpath, _, files in os.walk(os.path.join(ROOT, "archive", "src")):
        for f in files:
            if f.endswith(".cpp"):
                text = open(os.path.join(dirpath, f), encoding="utf-8").read()
                hooked |= {int(m, 16) for m in re.findall(r"HOOK\((0x[0-9a-fA-F]+)", text)}
    try:
        unsafe = record.unsafe_to_patch(funcs) & hooked
    except ImportError:
        print(f"note: no capstone here; using {cache}")
        return {f"{int(a, 16):08x}" for a in json.load(open(cache))}
    json.dump([hex(a) for a in sorted(unsafe)], open(cache, "w"))
    return {f"{a:08x}" for a in unsafe}


UNSAFE = set()


def main():
    global UNSAFE
    UNSAFE = unsafe_hooked()
    print(f"not counted (unsafe to patch): {sorted(UNSAFE)}")
    ap = argparse.ArgumentParser()
    ap.add_argument("--scenario", default="all")
    ap.add_argument("--range", default="")
    ap.add_argument("--timeout", type=int, default=180)
    a = ap.parse_args()
    names = compare.hook_names()
    off = {n: False for n in names}
    scenarios = list(compare.SCENARIOS) if a.scenario == "all" else a.scenario.split(",")
    calls = {}
    for name in scenarios:
        sc = compare.SCENARIOS[name]
        test = dict(sc.get("test") or {})
        test["hook_coverage"] = 1
        # Functions whose own branches land inside the 5 patched bytes crash under a counting stub: not counted.
        test["coverage_skip"] = ",".join(sorted(UNSAFE))
        import instances
        path = os.path.join(instances.instance_dir("main"), "and-coverage.txt")  # compare.run_once runs there
        if os.path.exists(path):
            os.remove(path)
        compare.run_once(f"coverage {name}", off, sc["level"], sc["frames"], a.timeout, sc["keys"], test)
        if not os.path.exists(path):
            print(f"  {name}: no and-coverage.txt written")
            continue
        for line in open(path):
            addr, count, fn = line.split()
            entry = calls.setdefault(fn, {"address": addr, "calls": 0, "scenarios": []})
            entry["calls"] += int(count)
            if int(count):
                entry["scenarios"].append(name)
    out = os.path.join(ROOT, "work", "coverage.json")
    json.dump(calls, open(out, "w"), indent=1)
    lo, hi = (int(x, 16) for x in a.range.split("-")) if a.range else (0, 1 << 32)
    sel = {n: v for n, v in calls.items() if lo <= int(v["address"], 16) < hi}
    run = sorted(n for n, v in sel.items() if v["calls"])
    print(f"{len(run)} of {len(sel)} registered functions executed in {', '.join(scenarios)} -> {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
