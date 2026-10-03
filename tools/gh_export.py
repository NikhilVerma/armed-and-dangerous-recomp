"""Bulk export from the Ghidra project (goal 1: every function exported and tracked).

Usage: python tools/gh_export.py [--no-decomp]

Writes:
  work/decomp/<address>.c            Ghidra decompilation of each function (ignored: bulk output)
  work/exports/functions.json        per function: address, size, name, callers, callees, string refs
  symbols/functions.csv              tracker rows; existing rows keep their name/stage/etc, new rows are S0

Subsystem tags come from referenced strings (first pass, provenance "string"):
  havok   - strings naming hk*.cpp/h files or Havok messages
  zlib    - zlib/deflate/inflate messages
  libjpeg - IJG libjpeg messages
  crt     - functions Function ID named from the C runtime
Everything else stays untagged until a later pass.
"""
import argparse
import csv
import json
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gh  # noqa: E402

ROOT = gh.ROOT
CSV = os.path.join(ROOT, "symbols", "functions.csv")
FIELDS = ["address", "size", "name", "subsystem", "stage", "approach", "provenance", "test", "tokens", "minutes"]

TAGS = [
    ("havok", re.compile(r"\bhk[A-Z]\w*|hkbase|havok", re.I)),
    ("zlib", re.compile(r"deflate|inflate|zlib|incorrect header check|invalid (block|distance|literal)", re.I)),
    ("libjpeg", re.compile(r"JPEG|JFIF|Huffman table|quantization table|Thomas G\. Lane", re.I)),
]


def load_tracker():
    rows = {}
    if os.path.exists(CSV):
        with open(CSV, newline="", encoding="utf-8") as f:
            for r in csv.DictReader(f):
                rows[r["address"]] = r
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--no-decomp", action="store_true")
    args = ap.parse_args()

    decomp_dir = os.path.join(ROOT, "work", "decomp")
    export_dir = os.path.join(ROOT, "work", "exports")
    os.makedirs(decomp_dir, exist_ok=True)
    os.makedirs(export_dir, exist_ok=True)

    tracker = load_tracker()
    t0 = time.time()
    with gh.open_program() as flat:
        program = flat.getCurrentProgram()
        from ghidra.util.task import ConsoleTaskMonitor
        from ghidra.app.decompiler import DecompInterface
        from ghidra.program.model.data import StringDataInstance
        monitor = ConsoleTaskMonitor()
        fm = program.getFunctionManager()
        refman = program.getReferenceManager()
        listing = program.getListing()

        dec = None
        if not args.no_decomp:
            dec = DecompInterface()
            dec.openProgram(program)

        funcs = []
        failed = 0
        for f in fm.getFunctions(True):
            a = f"0x{f.getEntryPoint().getOffset():08x}"
            strings = set()
            for rng in f.getBody():
                for ref_src in refman.getReferenceSourceIterator(rng.getMinAddress(), True):
                    if ref_src > rng.getMaxAddress():
                        break
                    for r in refman.getReferencesFrom(ref_src):
                        d = listing.getDataAt(r.getToAddress())
                        if d is not None and StringDataInstance.isString(d):
                            strings.add(str(d.getValue())[:160])
            rec = {
                "address": a,
                "size": f.getBody().getNumAddresses(),
                "name": f.getName(),
                "thunk": f.isThunk(),
                "callers": sorted(f"0x{c.getEntryPoint().getOffset():08x}" for c in f.getCallingFunctions(monitor)),
                "callees": sorted(f"0x{c.getEntryPoint().getOffset():08x}" for c in f.getCalledFunctions(monitor)),
                "strings": sorted(strings),
            }
            funcs.append(rec)

            if dec is not None:
                r = dec.decompileFunction(f, 60, monitor)
                if r.decompileCompleted():
                    with open(os.path.join(decomp_dir, f"{a}.c"), "w", encoding="utf-8", newline="\n") as out:
                        out.write(f"// {a} {f.getName()} size {rec['size']}\n")
                        out.write(r.getDecompiledFunction().getC())
                else:
                    failed += 1

            if len(funcs) % 500 == 0:
                print(f"  {len(funcs)} functions, {time.time() - t0:.0f}s", flush=True)

    with open(os.path.join(export_dir, "functions.json"), "w", encoding="utf-8") as out:
        json.dump(funcs, out, indent=0)

    tagged = {}
    for rec in funcs:
        old = tracker.get(rec["address"])
        if old:  # never overwrite work already recorded
            old["size"] = str(rec["size"])
            continue
        subsystem, prov = "", ""
        name = rec["name"]
        if rec["thunk"]:
            subsystem = "thunk"
        elif not name.startswith("FUN_"):
            prov = "fid"
            subsystem = "crt" if name.startswith("_") or name[:1].islower() else ""
        text = " ".join(rec["strings"])
        for tag, rx in TAGS:
            if rx.search(text):
                subsystem, prov = tag, prov or "string"
                break
        tracker[rec["address"]] = {
            "address": rec["address"], "size": str(rec["size"]), "name": name,
            "subsystem": subsystem, "stage": "S0", "approach": "", "provenance": prov,
            "test": "", "tokens": "", "minutes": "",
        }
        tagged[subsystem] = tagged.get(subsystem, 0) + 1

    with open(CSV, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=FIELDS, lineterminator="\n")
        w.writeheader()
        for a in sorted(tracker, key=lambda x: int(x, 16)):
            w.writerow(tracker[a])

    print(json.dumps({"functions": len(funcs), "decompile_failures": failed,
                      "seconds": round(time.time() - t0), "new_rows_by_subsystem": tagged}, indent=1))


if __name__ == "__main__":
    main()
