"""Split the remaining S0 game-side functions into recovery batches of about --size bytes, per source directory
(subsystem -> archive/src/<dir>/), in address order. Library subsystems (havok, qhull, d3dx, crt, zlib, thunk) are skipped.

Writes symbols/recovery_batches.json: {key: {"file": "archive/src/<dir>/<key>", "addrs": [...]}} and prints a summary.
Usage: python archive/tools/make_batches.py [--size 20000] [--force]   (refuses to overwrite existing assignments)
"""
import argparse
import collections
import csv
import json
import os

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # the repository (this tool lives in archive/tools)
LIBRARY = {"havok", "qhull", "d3dx", "crt", "zlib", "thunk"}
DIRS = {"game": "game", "camera": "game", "ai": "ai", "ui": "ui", "audio": "audio", "physics-glue": "physics",
        "havok-ext": "physics", "loader": "loader", "save": "save", "anim": "anim", "input": "input",
        "movie": "movie"}  # everything else (math, container, platform, debug, unknown, engine) -> archive/src/engine/


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--size", type=int, default=20000)
    ap.add_argument("--force", action="store_true", help="overwrite existing assignments")
    a = ap.parse_args()
    target = os.path.join(ROOT, "symbols", "recovery_batches.json")
    if os.path.exists(target) and not a.force:
        raise SystemExit("symbols/recovery_batches.json exists: in-flight batches depend on it "
                         "(see archive/tools/recovery_status.py); pass --force only to start a new round")
    with open(os.path.join(ROOT, "symbols", "functions.csv"), newline="") as f:
        rows = [r for r in csv.reader(f) if r and r[0].startswith("0x")]
    by = collections.defaultdict(list)
    for r in rows:
        if r[3] in LIBRARY or r[4] != "S0":
            continue
        by[DIRS.get(r[3], "engine")].append((int(r[0], 16), int(r[1])))
    out = {}
    for d, fs in sorted(by.items()):
        fs.sort()
        cur, size, n = [], 0, 0

        def flush():
            nonlocal cur, size, n
            n += 1
            key = f"{d}_{n:02d}"
            out[key] = {"file": f"archive/src/{d}/{key}", "addrs": [f"{x:#010x}" for x in cur], "bytes": size}
            cur, size = [], 0

        for addr, s in fs:
            if cur and size + s > a.size:
                flush()
            cur.append(addr)
            size += s
        if cur:
            flush()
    with open(os.path.join(ROOT, "symbols", "recovery_batches.json"), "w") as f:
        json.dump(out, f, indent=0)
    print(f"{len(out)} batches, {sum(len(v['addrs']) for v in out.values())} functions")
    for k, v in out.items():
        print(f"{k:12} {len(v['addrs']):4} functions {v['bytes']:6} bytes")


if __name__ == "__main__":
    main()
