"""Analyse a recorded session (tools/record.py) into coverage, observed ABI and call edges.

Usage: python tools/analyse_trace.py work/traces/session1

Writes into <trace>/analysis/:
  coverage.csv  function, first-seen seconds, calls recorded, threads, tracker name/subsystem
  abi.csv       function, register inputs (static candidates confirmed by runtime values), stack arg count,
                sample values
  edges.csv     caller function -> callee, with whether the call site is direct (call rel32) or indirect
Prints a summary.

Register-input rule: a register is reported as an input when the static scan (tools/reginputs.py, depth 1)
finds it read before written on some path, AND at runtime it is not one of the scratch values a caller leaves
behind by accident (it is either a plausible pointer or consistent across calls). Static-only candidates are
listed separately as "unconfirmed".
"""
import bisect
import collections
import csv
import json
import os
import struct
import sys

import capstone
import pefile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import reginputs  # noqa: E402

EXE = os.path.join(ROOT, "orig", "pc", "GameData", "game.unpacked.exe")
BASE = 0x400000
REG_NAMES = ["eax", "ecx", "edx", "ebx", "esi", "edi", "ebp"]
ENTRY_FIELDS = ["kind", "fn", "tick", "tid", "caller", "eax", "ecx", "edx", "ebx", "esi", "edi", "ebp", "esp",
                "s1", "s2", "s3", "s4"]


def pointer_kind(v):
    if 0x00400000 <= v < 0x00700000:
        return "image"
    if 0x00010000 <= v < 0x7fff0000:
        return "heap/stack"
    return None


def describe(values):
    vs = list(values)
    distinct = len(set(vs))
    kinds = collections.Counter(pointer_kind(v) for v in vs)
    if distinct == 1:
        return f"const 0x{vs[0]:x}"
    kind = kinds.most_common(1)[0][0]
    return f"{distinct} values, {kind or 'small ints'} (e.g. 0x{vs[0]:x})"


def stack_arg_count(img, md, start, size):
    for i in md.disasm(img[start - BASE:start - BASE + size], start):
        if i.mnemonic == "ret":
            return int(i.op_str, 16) // 4 if i.op_str else 0
    return None


def main():
    trace = sys.argv[1]
    out = os.path.join(trace, "analysis")
    os.makedirs(out, exist_ok=True)
    funcs = {int(f["address"], 16): f for f in json.load(open(os.path.join(ROOT, "work", "exports", "functions.json")))}
    starts = sorted(funcs)
    tracker = {int(r["address"], 16): r for r in csv.DictReader(open(os.path.join(ROOT, "symbols", "functions.csv")))}

    entries = collections.defaultdict(list)
    t0 = None
    with open(os.path.join(trace, "calls.jsonl")) as f:
        for line in f:
            r = json.loads(line)
            if r[0] != 1:
                continue
            rec = dict(zip(ENTRY_FIELDS, r))
            t0 = rec["tick"] if t0 is None else min(t0, rec["tick"])
            entries[rec["fn"]].append(rec)

    pe = pefile.PE(EXE, fast_load=True)
    img = pe.get_memory_mapped_image()
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)

    def func_of(addr):
        j = bisect.bisect_right(starts, addr) - 1
        if j < 0:
            return None
        s = starts[j]
        return s if addr < s + max(funcs[s]["size"], 1) else None

    def call_kind(ret_addr):
        # a direct call rel32 is 5 bytes before the return address
        o = ret_addr - 5 - BASE
        if 0 <= o < len(img) and img[o] == 0xE8:
            return "direct"
        return "indirect"

    cov_rows, abi_rows, edge_counts = [], [], collections.Counter()
    confirmed_total = collections.Counter()
    for fn, recs in sorted(entries.items(), key=lambda kv: min(r["tick"] for r in kv[1])):
        first = min(r["tick"] for r in recs) - t0
        tr = tracker.get(fn, {})
        cov_rows.append({"address": f"0x{fn:08x}", "first_seen_s": round(first / 1000, 2), "calls": len(recs),
                         "threads": len({r["tid"] for r in recs}), "name": tr.get("name", ""),
                         "subsystem": tr.get("subsystem", "")})
        for r in recs:
            caller_fn = func_of(r["caller"])
            edge_counts[(caller_fn, fn, call_kind(r["caller"]))] += 1

        static = reginputs.inputs(fn, 1)
        confirmed, unconfirmed = [], []
        for reg in REG_NAMES:
            if reg not in static:
                continue
            vals = [r[reg] for r in recs]
            desc = describe(vals)
            plausible = len(recs) >= 2 and (pointer_kind(vals[0]) or len(set(vals)) == 1)
            (confirmed if plausible else unconfirmed).append(f"{reg}: {desc}")
            if plausible:
                confirmed_total[reg] += 1
        n_args = stack_arg_count(img, md, fn, funcs[fn]["size"]) if fn in funcs else None
        samples = []
        for k in range(1, min(n_args or 0, 4) + 1):
            samples.append(f"s{k}: {describe(r['s' + str(k)] for r in recs)}")
        abi_rows.append({"address": f"0x{fn:08x}", "name": tr.get("name", ""), "calls": len(recs),
                         "register_inputs": "; ".join(confirmed), "unconfirmed": "; ".join(unconfirmed),
                         "callee_pops_args": n_args if n_args is not None else "", "stack_args": "; ".join(samples)})

    def write(name, rows):
        if not rows:
            return
        with open(os.path.join(out, name), "w", newline="", encoding="utf-8") as f:
            w = csv.DictWriter(f, fieldnames=list(rows[0].keys()), lineterminator="\n")
            w.writeheader()
            w.writerows(rows)

    write("coverage.csv", cov_rows)
    write("abi.csv", abi_rows)
    write("edges.csv", [{"caller": f"0x{c:08x}" if c else "(outside)", "callee": f"0x{e:08x}", "kind": k, "calls": n}
                        for (c, e, k), n in sorted(edge_counts.items(), key=lambda x: (str(x[0][0]), x[0][1]))])

    total = len([f for f in funcs.values() if not f["thunk"]])
    indirect = sum(1 for (c, e, k) in edge_counts if k == "indirect")
    print(f"functions seen: {len(entries)} of {total} ({100 * len(entries) / total:.0f}%)")
    by_sub = collections.Counter(tracker.get(fn, {}).get("subsystem", "") or "untagged" for fn in entries)
    print("by subsystem:", dict(by_sub.most_common()))
    print(f"call edges observed: {len(edge_counts)} ({indirect} indirect: vtable or function-pointer calls)")
    print("functions with confirmed register inputs, by register:", dict(confirmed_total.most_common()))
    print(f"written to {out}")


if __name__ == "__main__":
    main()
