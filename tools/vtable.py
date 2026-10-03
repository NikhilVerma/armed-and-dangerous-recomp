"""List a vtable's slots: offset, target, name and stage from symbols/functions.csv.

Usage: python tools/vtable.py 0x00589990 [--max 200] [--md]

Slots are read until a dword that does not point into game code (0x00401000-0x00538000), or --max slots.
--md prints a Markdown table (for docs/renderer-interface.md).
"""
import argparse
import csv
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import reginputs  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("vtable")
    ap.add_argument("--max", type=int, default=200)
    ap.add_argument("--md", action="store_true")
    a = ap.parse_args()
    funcs = {int(r["address"], 16): r for r in csv.DictReader(open(os.path.join(ROOT, "symbols", "functions.csv")))}
    vt = int(a.vtable, 16)
    rows = []
    for k in range(a.max):
        t = struct.unpack_from("<I", reginputs.IMG, vt - reginputs.BASE + 4 * k)[0]
        if not 0x00401000 <= t < 0x00538000:
            break
        f = funcs.get(t, {})
        rows.append((k, 4 * k, t, f.get("name", "?"), f.get("stage", "?")))
    if a.md:
        print("| Slot | Offset | Address | Name | Stage |\n| ---: | ---: | --- | --- | --- |")
        for k, off, t, name, stage in rows:
            print(f"| {k} | 0x{off:03x} | 0x{t:08x} | {name} | {stage} |")
    else:
        for k, off, t, name, stage in rows:
            print(f"{k:3} +0x{off:03x} 0x{t:08x} {stage:3} {name}")
    print(f"{len(rows)} slots", file=sys.stderr)


if __name__ == "__main__":
    sys.exit(main())
