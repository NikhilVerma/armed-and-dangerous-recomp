"""Record a play session: every function's first calls (registers, stack arguments, caller, return) plus
keyboard input, for later analysis (archive/tools/analyse_trace.py).

Usage: python archive/tools/record.py [--calls 50] [--name session1]
  1. The game starts under Frida (start-up is slower while hooks are installed).
  2. Play. Quit the game normally (or close its window) when done.
  3. Output: work/traces/<name>/calls.jsonl, hooks.json, input.log (keyboard), and.log copy.

Functions with recovered replacements (HOOK() in src/) are skipped so and.ini options keep working.
"""
import argparse
import datetime
import json
import os
import re
import shutil
import sys
import threading
import time

import frida

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # the repository (this tool lives in archive/tools)
GAME_DIR = os.path.join(ROOT, "orig", "pc", "GameData")
EXE = os.path.join(GAME_DIR, "game.unpacked.exe")


def recovered_addresses():
    found = set()
    for dirpath, _, files in os.walk(os.path.join(ROOT, "archive", "src")):
        for f in files:
            if f.endswith(".cpp"):
                text = open(os.path.join(dirpath, f), encoding="utf-8").read()
                found |= {int(m, 16) for m in re.findall(r"HOOK\((0x[0-9a-fA-F]+)", text)}
    return found


def unsafe_to_patch(funcs):
    """Functions where some branch (from anywhere, including the function's own loops) lands inside the bytes
    Frida overwrites with its jump. Hooking those crashes the game (first seen at 0x004dea43, a loop head
    3 bytes into 0x004dea40)."""
    import capstone
    import pefile
    pe = pefile.PE(EXE, fast_load=True)
    img = pe.get_memory_mapped_image()
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    base = 0x400000
    patch_end = {}
    for f in funcs:
        a = int(f["address"], 16)
        end = a
        for i in md.disasm(img[a - base:a - base + 32], a):  # whole instructions covering 5 bytes
            end = i.address + i.size
            if end - a >= 5:
                break
        patch_end[a] = end
    starts = sorted(patch_end)
    import bisect
    unsafe = set()
    for f in funcs:
        a, size = int(f["address"], 16), f["size"]
        for i in md.disasm(img[a - base:a - base + size], a):
            if i.group(capstone.CS_GRP_JUMP) or i.group(capstone.CS_GRP_CALL):
                op = i.operands[0] if i.operands else None
                if op is None or op.type != capstone.x86.X86_OP_IMM:
                    continue
                t = op.imm
                j = bisect.bisect_right(starts, t) - 1
                if j >= 0 and starts[j] < t < patch_end[starts[j]]:
                    unsafe.add(starts[j])
    return unsafe


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--calls", type=int, default=50)
    ap.add_argument("--kill", type=float, default=0, help="stop the game after this many seconds (tests)")
    ap.add_argument("--only", help="JSON file with a list of function addresses to hook (bisection)")
    ap.add_argument("--exclude", default=os.path.join(ROOT, "archive", "tools", "frida", "unhookable.json"),
                    help="JSON file of addresses never to hook (found by archive/tools/record_bisect.py)")
    ap.add_argument("--name", default=datetime.datetime.now().strftime("%Y%m%d-%H%M%S"))
    a = ap.parse_args()

    out_dir = os.path.join(ROOT, "work", "traces", a.name)
    os.makedirs(out_dir, exist_ok=True)
    funcs = json.load(open(os.path.join(ROOT, "work", "exports", "functions.json")))
    skip = recovered_addresses()
    # The C runtime (0x538000 and up) changes FPU control state and returns values on the x87 stack;
    # hooking it crashed the game. It is library code we do not recover, so leave it alone.
    targets = [[int(f["address"], 16), f["size"]] for f in funcs
               if not f["thunk"] and f["size"] >= 5 and int(f["address"], 16) not in skip
               and int(f["address"], 16) < 0x00538000]
    unsafe = unsafe_to_patch(funcs)
    targets = [t for t in targets if t[0] not in unsafe]
    print(f"{len(unsafe)} functions skipped: a branch lands inside their first 5 bytes")
    if a.only:
        only = set(json.load(open(a.only)))
        targets = [t for t in targets if t[0] in only]
    if a.exclude and os.path.exists(a.exclude):
        never = {int(x, 16) for x in json.load(open(a.exclude))}
        targets = [t for t in targets if t[0] not in never]
    print(f"{len(targets)} functions to hook ({len(skip)} recovered ones skipped)")

    calls = open(os.path.join(out_dir, "calls.jsonl"), "w")
    stats = {"records": 0, "failed": []}
    done = threading.Event()
    hooked = threading.Event()

    def on_message(msg, _data):
        if msg["type"] == "send":
            p = msg["payload"]
            if "records" in p:
                for r in p["records"]:
                    if r[0] == "x":
                        stats["failed"].append(r[1:])
                    else:
                        calls.write(json.dumps(r) + "\n")
                stats["records"] += len(p["records"])
            elif "hooked" in p:
                print(f"hooked {p['hooked']} functions, {p['failed']} failed; resuming the game")
                hooked.set()
        elif msg["type"] == "error":
            print("agent error:", msg.get("description"))

    device = frida.get_local_device()
    pid = device.spawn([EXE], cwd=GAME_DIR)
    session = device.attach(pid)
    session.on("detached", lambda reason, crash: (print(f"game exited ({reason})"), done.set()))
    script = session.create_script(open(os.path.join(ROOT, "archive", "tools", "frida", "record_agent.js")).read())
    script.on("message", on_message)
    script.load()
    t = time.time()
    script.post({"type": "config", "functions": targets, "callsPerFunction": a.calls})
    # keep the game suspended until every hook is in place
    if not hooked.wait(120):
        print("hooks were not installed within 120 s; killing the game")
        device.kill(pid)
        return 1
    device.resume(pid)
    print(f"game running (pid {pid}); play, then quit the game. Recording to {out_dir}")
    try:
        while not done.wait(5):
            if a.kill and time.time() - t > a.kill:
                print(f"time limit {a.kill:.0f}s reached; killing the game")
                device.kill(pid)
                done.wait(10)
                break
            print(f"  {stats['records']:,} records, {time.time() - t:.0f}s", flush=True)
    except KeyboardInterrupt:
        print("stopping: killing the game")
        device.kill(pid)
        done.wait(10)
    calls.close()
    json.dump({"functions": len(targets), "failed": stats["failed"], "records": stats["records"],
               "seconds": round(time.time() - t)}, open(os.path.join(out_dir, "hooks.json"), "w"), indent=1)
    for f in ("and.log", "and-input.log"):
        src = os.path.join(GAME_DIR, f)
        if os.path.exists(src):
            shutil.copy(src, os.path.join(out_dir, f))
    print(f"done: {stats['records']:,} records, {len(stats['failed'])} functions could not be hooked -> {out_dir}")


if __name__ == "__main__":
    sys.exit(main())
