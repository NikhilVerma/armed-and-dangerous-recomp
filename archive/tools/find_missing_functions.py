"""Find game functions missing from symbols/functions.csv (Ghidra missed them: callbacks reached only through a
pointer, tail-jump targets, bone callbacks ...). They have no tracker row, so no recovery batch would ever cover them.

Candidates: targets of direct `call rel32` (E8) and `jmp rel32` (E9) anywhere in .text, plus dwords in .rdata/.data
that point into .text. A candidate is reported when it is not a known function start, does not fall inside a known
function's range (so ordinary jump targets inside functions are excluded), lies outside the library ranges (Havok,
qhull, D3DX, CRT, zlib by tracker subsystem of the surrounding known functions), and the byte before it is padding
(0xCC / 0x90) or a `ret` - the usual shape of a function start.

Writes work/classify/missing_functions.csv (address, evidence, preceding known function) and prints a summary.
Usage: python archive/tools/find_missing_functions.py
"""
import bisect
import collections
import csv
import json
import os
import struct

import pefile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # the repository (this tool lives in archive/tools)
EXE = os.path.join(ROOT, "orig", "pc", "GameData", "game.unpacked.exe")
LIBRARY = {"havok", "qhull", "d3dx", "crt", "zlib", "thunk"}


def main():
    pe = pefile.PE(EXE, fast_load=True)
    base = pe.OPTIONAL_HEADER.ImageBase
    img = pe.get_memory_mapped_image()
    text = next(s for s in pe.sections if s.Name.rstrip(b"\0") == b".text")
    t0, t1 = base + text.VirtualAddress, base + text.VirtualAddress + text.Misc_VirtualSize
    with open(os.path.join(ROOT, "symbols", "functions.csv"), newline="") as f:
        rows = [r for r in csv.reader(f) if r and r[0].startswith("0x")]
    starts = sorted(int(r[0], 16) for r in rows)
    info = {int(r[0], 16): (int(r[1]), r[3], r[2]) for r in rows}
    exports = json.load(open(os.path.join(ROOT, "work", "exports", "functions.json")))
    starts_set = set(starts) | {int(f["address"], 16) for f in exports}

    def owner(a):
        i = bisect.bisect_right(starts, a) - 1
        return starts[i] if i >= 0 else None

    def inside_known(a):
        o = owner(a)
        return o is not None and a < o + info[o][0]

    evidence = collections.defaultdict(set)
    code = img[text.VirtualAddress:text.VirtualAddress + text.Misc_VirtualSize]
    for i in range(len(code) - 5):
        op = code[i]
        if op in (0xE8, 0xE9):
            tgt = (t0 + i + 5 + struct.unpack_from("<i", code, i + 1)[0]) & 0xFFFFFFFF
            if t0 <= tgt < t1:
                evidence[tgt].add("call" if op == 0xE8 else "jmp")
    for s in pe.sections:
        if s.Name.rstrip(b"\0") in (b".rdata", b".data"):
            data = img[s.VirtualAddress:s.VirtualAddress + s.Misc_VirtualSize]
            for i in range(0, len(data) - 3, 4):
                v = struct.unpack_from("<I", data, i)[0]
                if t0 <= v < t1:
                    evidence[v].add("pointer")
    found = []
    for a, ev in evidence.items():
        if a in starts_set or inside_known(a):
            continue
        o = owner(a)
        if o is not None and info[o][1] in LIBRARY:
            continue
        prev = img[a - base - 1]
        if prev not in (0xCC, 0x90, 0xC3, 0xC2) and not (img[a - base - 3] == 0xC2):
            continue
        found.append((a, "+".join(sorted(ev)), f"{o:#010x} {info[o][2]}" if o else ""))
    found.sort()
    out = os.path.join(ROOT, "work", "classify", "missing_functions.csv")
    with open(out, "w", newline="") as f:
        w = csv.writer(f, lineterminator="\n")
        w.writerow(["address", "evidence", "after"])
        for a, ev, o in found:
            w.writerow([f"{a:#010x}", ev, o])
    c = collections.Counter(ev for _, ev, _ in found)
    print(f"{len(found)} candidate functions missing from the tracker -> {out}")
    print(dict(c))
    for a, ev, o in found[:25]:
        print(f"  {a:#010x} {ev:14} after {o}")


if __name__ == "__main__":
    main()
