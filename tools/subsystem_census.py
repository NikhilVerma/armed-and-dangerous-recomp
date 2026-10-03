"""Seed and size the renderer and data-loading subsystems (goal: docs/goals/renderer-and-assets.md).

Seeds (evidence-based):
  renderer  functions that use Renderer+0x14c8 (IDirect3D9) or +0x14cc (device), virtual methods of the
            Renderer class (vtable 0x00589990), functions referencing gfx\\d3d\\*.cpp strings
  loader    functions referencing filesys.cpp, calling CreateFileA/ReadFile/SetFilePointer, or using one of the
            13 .big magic numbers as an immediate
Then each set grows by callees that are only called from inside the set (helpers private to the subsystem).
Writes work/census/<subsystem>.csv (address, size, why, called from outside?) and prints sizes.

Usage: python tools/subsystem_census.py
"""
import collections
import csv
import json
import os
import re
import struct

import capstone
from capstone import x86
import pefile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, "orig", "pc", "GameData", "game.unpacked.exe")
BASE = 0x400000
BIG_MAGICS = {0xDEADC0DE, 0x0B00B1E5, 0x00C0FFEE, 0xFADEBACC, 0x0BADF00D, 0x00BEADED, 0x00BADA55, 0xDEAFDEAF,
              0x00EFFEC5, 0xBABEBABE, 0x00FFDBAC, 0x00B0D1E5, 0x0ADDBEEF}
RENDERER_VTABLE = 0x00589990


def main():
    funcs = {int(f["address"], 16): f for f in json.load(open(os.path.join(ROOT, "work", "exports", "functions.json")))}
    pe = pefile.PE(EXE)
    img = pe.get_memory_mapped_image()
    imports = {}
    for e in pe.DIRECTORY_ENTRY_IMPORT:
        for i in e.imports:
            if i.name:
                imports[i.address] = i.name.decode()
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True

    why = {"renderer": collections.defaultdict(set), "loader": collections.defaultdict(set)}
    for a, f in funcs.items():
        if f["thunk"]:
            continue
        for s in f["strings"]:
            if re.search(r"gfx\\d3d|IDirec3D|Direct3D|DX function", s):
                why["renderer"][a].add("string:" + s[:30])
            if re.search(r"filesys", s, re.I):
                why["loader"][a].add("string:filesys")
        for i in md.disasm(img[a - BASE:a - BASE + f["size"]], a):
            for op in i.operands:
                if op.type == x86.X86_OP_MEM and op.mem.base != 0 and op.mem.disp in (0x14c8, 0x14cc):
                    why["renderer"][a].add(f"Renderer+0x{op.mem.disp:x}")
                if op.type == x86.X86_OP_MEM and op.mem.base == 0 and op.mem.disp in imports:
                    name = imports[op.mem.disp]
                    if name in ("CreateFileA", "ReadFile", "SetFilePointer", "FindFirstFileA"):
                        why["loader"][a].add("api:" + name)
                if op.type == x86.X86_OP_IMM and (op.imm & 0xffffffff) in BIG_MAGICS:
                    why["loader"][a].add(f"magic:0x{op.imm & 0xffffffff:08x}")
    # Renderer virtual methods
    for k in range(80):
        p = struct.unpack_from("<I", img, RENDERER_VTABLE - BASE + 4 * k)[0]
        if p not in funcs:
            break
        why["renderer"][p].add(f"vtable[{k}]")

    callers = collections.defaultdict(set)
    for a, f in funcs.items():
        for c in f["callees"]:
            callers[int(c, 16)].add(a)

    os.makedirs(os.path.join(ROOT, "work", "census"), exist_ok=True)
    for name, seeds in why.items():
        members = set(seeds)
        changed = True
        while changed:  # add callees whose every caller is already a member
            changed = False
            for a in list(members):
                for c in funcs.get(a, {}).get("callees", []):
                    c = int(c, 16)
                    if c in members or c not in funcs or funcs[c]["thunk"] or c >= 0x00538000:
                        continue
                    if callers[c] and callers[c] <= members:
                        members.add(c)
                        why[name][c].add("private helper")
                        changed = True
        rows = []
        for a in sorted(members):
            rows.append({"address": f"0x{a:08x}", "size": funcs[a]["size"],
                         "why": "; ".join(sorted(why[name][a])),
                         "external_callers": len(callers[a] - members),
                         "callees_outside": len([c for c in funcs[a]["callees"] if int(c, 16) not in members])})
        with open(os.path.join(ROOT, "work", "census", f"{name}.csv"), "w", newline="") as f:
            w = csv.DictWriter(f, fieldnames=list(rows[0].keys()), lineterminator="\n")
            w.writeheader()
            w.writerows(rows)
        print(f"{name}: {len(seeds)} seeds, {len(members)} with private helpers, "
              f"{sum(funcs[a]['size'] for a in members):,} bytes")


if __name__ == "__main__":
    main()
