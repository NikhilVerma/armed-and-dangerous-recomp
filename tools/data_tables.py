"""Extract the game's member-description tables (DATA(Class::member) entries) into symbols/types/.

Each table entry in game.unpacked.exe is six u32 words:
    offset, size, count, 0, name_ptr -> "DATA(Class::member)", type_code
(found at 0x0058424c.. for PlayerWeapon; see docs/patterns.md). Every pointer to a DATA() string is located,
and the entry around it is decoded. Type codes are recorded raw until their meaning is established.

Usage: python tools/data_tables.py   -> symbols/types/data_members.csv + summary
"""
import collections
import csv
import os
import re
import struct
import sys

import pefile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gamefiles import game_exe  # noqa: E402
EXE = str(game_exe(os.path.join(ROOT, "orig", "pc", "GameData")))  # the tables live in .data / .rdata
OUT = os.path.join(ROOT, "symbols", "types", "data_members.csv")
BASE = 0x400000


def main():
    pe = pefile.PE(EXE)
    img = pe.get_memory_mapped_image()
    names = {m.start() + BASE: m.group(1).decode()
             for m in re.finditer(rb"DATA\(([A-Za-z_]\w*::\w+)\)\0", img)}
    rows, missing = [], 0
    for va, full in names.items():
        needle = struct.pack("<I", va)
        i = img.find(needle)
        if i == -1:
            missing += 1
            continue
        while i != -1:
            off, size, count, zero, ptr, typ = struct.unpack_from("<6I", img, i - 16)
            cls, member = full.split("::")
            rows.append({"class": cls, "member": member, "offset": f"0x{off:x}", "size": size, "count": count,
                         "type": typ, "entry": f"0x{BASE + i - 16:08x}", "check": zero})
            i = img.find(needle, i + 1)
    rows.sort(key=lambda r: (r["class"], int(r["offset"], 16), r["member"]))
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=["class", "member", "offset", "size", "count", "type", "entry", "check"],
                           lineterminator="\n")
        w.writeheader()
        w.writerows(rows)
    types = collections.Counter(r["type"] for r in rows)
    odd = sum(1 for r in rows if r["check"] != 0)
    print(f"{len(names)} DATA names, {len(rows)} table entries, {missing} names without a pointer, "
          f"{odd} entries whose 4th word is not 0")
    print("classes:", len({r['class'] for r in rows}), " type codes:", dict(sorted(types.items())))


if __name__ == "__main__":
    main()
