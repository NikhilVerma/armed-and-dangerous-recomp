"""Summarise and-profile.txt (written by the hook's sampling profiler, [test] profile=1) by function.

Game-image addresses are mapped to the function that contains them (symbols/functions.csv: address, size, name,
stage), so the report says which original or recovered function the main thread spends its time in.
Usage: python archive/tools/profile_report.py [path to and-profile.txt] [--top N]
"""
import argparse
import bisect
import csv
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # the repository (this tool lives in archive/tools)


def load_functions():
    rows = []
    for r in csv.DictReader(open(os.path.join(ROOT, "symbols", "functions.csv"), encoding="utf-8")):
        a = int(r["address"], 16)
        rows.append((a, a + max(int(r["size"] or 1), 1), r["name"] or r["address"], r["stage"]))
    rows.sort()
    return rows


def name_of(funcs, starts, addr):
    i = bisect.bisect_right(starts, addr) - 1
    if i >= 0 and funcs[i][0] <= addr < funcs[i][1]:
        return f"{funcs[i][2]} [{funcs[i][3]}]"
    return f"game {addr:08x} (no function)"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("path", nargs="?", default=os.path.join(ROOT, "orig", "pc", "GameData", "and-profile.txt"))
    ap.add_argument("--top", type=int, default=25)
    a = ap.parse_args()
    funcs = load_functions()
    starts = [f[0] for f in funcs]
    section, totals, samples = None, {}, {}
    for line in open(a.path, encoding="utf-8", errors="replace"):
        m = re.match(r"== (\w+).*\((\d+) samples\)", line)
        if m:
            section = m.group(1)
            samples[section] = int(m.group(2))
            totals[section] = {}
            continue
        m = re.match(r"\s*(\d+)\s+[\d.]+%\s+(.*)", line)
        if not m or not section:
            continue
        n, what = int(m.group(1)), m.group(2).strip()
        g = re.match(r"game ([0-9a-f]{8})", what)
        key = name_of(funcs, starts, int(g.group(1), 16)) if g else what
        totals[section][key] = totals[section].get(key, 0) + n
    for section, t in totals.items():
        print(f"== {section} ({samples[section]} samples)")
        for key, n in sorted(t.items(), key=lambda kv: -kv[1])[:a.top]:
            print(f"{n:7d} {100.0 * n / max(samples[section], 1):5.1f}%  {key}")
        print()


if __name__ == "__main__":
    main()
