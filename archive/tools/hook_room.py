"""Hooks need 5 bytes for their jump: list hooked functions with less than 5 bytes before the next function
(their hook would overwrite the next function unless the bytes after them are padding). Usage: python archive/tools/hook_room.py
"""
import re, glob, json, pefile
pe = pefile.PE("orig/pc/GameData/game.unpacked.exe", fast_load=True)
img = pe.get_memory_mapped_image()
funcs = sorted(int(f["address"], 16) for f in json.load(open("work/exports/functions.json")))
hooks = set()
for f in glob.glob("archive/src/**/*.cpp", recursive=True):
    t = open(f, encoding="utf-8", errors="ignore").read()
    hooks |= {int(a, 16) for a in re.findall(r"\bHOOK\((0x[0-9a-fA-F]+),", t)}
    hooks |= {int(a, 16) for a in re.findall(r"\b\w+_HOOK\(([0-9a-fA-F]{8}),", t)}
import bisect
bad = []
for a in sorted(hooks):
    i = bisect.bisect_right(funcs, a)
    nxt = funcs[i] if i < len(funcs) else a + 0x1000
    room = nxt - a
    if room < 5:
        bad.append((a, room, img[a - 0x400000:a - 0x400000 + 8].hex()))
print(len(hooks), "hooks;", len(bad), "with less than 5 bytes before the next function")
for a, room, b in bad:
    print(hex(a), room, b)
