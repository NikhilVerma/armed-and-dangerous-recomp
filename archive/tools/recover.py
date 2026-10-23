"""Everything needed to recover one function, in one view (tools, not dumps: AGENTS.md).

Usage: python archive/tools/recover.py 0x004fc7a0 [--asm]

Prints: ABI (symbols/abi.csv), tracker row, whether the comparison scenarios execute it (from recorded traces),
its callers, the ABIs of its callees (to call them via orig::fn_XXXXXXXX or recovered functions), and the
current Ghidra decompilation (work/decomp). --asm adds the disassembly.
"""
import argparse
import csv
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # the repository (this tool lives in archive/tools)


def load_csv(path, key="address"):
    return {r[key]: r for r in csv.DictReader(open(path, encoding="utf-8"))}


def traced_functions():
    seen = set()
    tdir = os.path.join(ROOT, "work", "traces")
    for name in os.listdir(tdir) if os.path.isdir(tdir) else []:
        p = os.path.join(tdir, name, "calls.jsonl")
        if not os.path.exists(p):
            continue
        with open(p) as f:
            for line in f:
                r = json.loads(line)
                if r[0] == 1:
                    seen.add(r[1])
    return seen


def recovered_functions():
    import re
    out = {}
    for dirpath, _, files in os.walk(os.path.join(ROOT, "archive", "src")):
        for f in files:
            if f.endswith(".cpp"):
                for m in re.finditer(r'HOOK\((0x[0-9a-fA-F]+),\s*"([^"]+)"', open(os.path.join(dirpath, f)).read()):
                    out[int(m.group(1), 16)] = (m.group(2), os.path.relpath(os.path.join(dirpath, f), ROOT))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("address")
    ap.add_argument("--asm", action="store_true")
    a = ap.parse_args()
    addr = int(a.address, 16)
    key = f"0x{addr:08x}"
    abi = load_csv(os.path.join(ROOT, "symbols", "abi.csv"))
    tracker = load_csv(os.path.join(ROOT, "symbols", "functions.csv"))
    funcs = {int(f["address"], 16): f for f in json.load(open(os.path.join(ROOT, "work", "exports", "functions.json")))}
    traced = traced_functions()
    recovered = recovered_functions()
    f = funcs[addr]

    def sig(x):
        r = abi.get(f"0x{x:08x}")
        if not r:
            return "(no abi)"
        s = f"inputs [{r['inputs']}] stack {r['stack_args']} ({r['cleanup']}) returns {r['returns']}"
        return s + (f"  NOTE {r['notes']}" if r["notes"] else "")

    print(f"== {key} {tracker.get(key, {}).get('name', '')} size {f['size']}")
    print(f"abi: {sig(addr)}   runtime: {abi.get(key, {}).get('runtime', '')}")
    print(f"executed in recorded runs: {'yes' if addr in traced else 'NO (comparison cannot verify it)'}")
    print("callers:", ", ".join(f"{c}{' (recovered)' if int(c, 16) in recovered else ''}" for c in f["callers"][:12]))
    print("callees:")
    for c in f["callees"]:
        ci = int(c, 16)
        name = recovered[ci][0] if ci in recovered else tracker.get(c, {}).get("name", "")
        print(f"   {c} {name:30} {sig(ci)}{'  [recovered]' if ci in recovered else ''}")
    if f["strings"]:
        print("strings:", f["strings"][:8])
    path = os.path.join(ROOT, "work", "decomp", f"{key}.c")
    if os.path.exists(path):
        print("---- decompilation")
        print(open(path, encoding="utf-8").read())
    if a.asm:
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(1, os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), "tools"))  # compare.py, instances.py (shared with the oracle tools)
        import reginputs
        print("---- disassembly")
        for i in reginputs.MD.disasm(reginputs.IMG[addr - reginputs.BASE:addr - reginputs.BASE + f["size"]], addr):
            print(f"{i.address:08x} {i.mnemonic:6} {i.op_str}")


if __name__ == "__main__":
    main()
