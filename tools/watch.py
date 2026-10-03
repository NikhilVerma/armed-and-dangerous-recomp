"""Watch memory at one instruction: every time the game executes ADDRESS, copy SIZE bytes from
[REGISTER + OFFSET] and log them with a timestamp. Integer-only C hook (safe on game threads).

Usage:
  python tools/watch.py --at 0x0040f683 --reg eax --size 32 --kill 30 --out work/watch/camslot.csv
  (uses the current and.ini; combine with [startup] load and [test] fixed_dt_ms for repeatable runs)

Output CSV: ms since first hit, then the bytes as u32 words (w0..wN) and as floats (f0..fN).
--every N keeps one hit in N (default 1).
"""
import argparse
import csv
import os
import struct
import sys
import threading
import time

import frida

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAME_DIR = os.path.join(ROOT, "orig", "pc", "GameData")
EXE = os.path.join(GAME_DIR, "game.unpacked.exe")

AGENT = r"""
'use strict';
let ring, widx, recWords, capacity, readIdx = 0;
let cm, counter;  // module scope: a garbage-collected CModule frees the hook's code (crashed at first hit)
recv('cfg', function (c) {
  recWords = 2 + c.size / 4; capacity = 1 << 16;
  ring = Memory.alloc(capacity * recWords * 4); widx = Memory.alloc(4);
  counter = Memory.alloc(4);
  const src = `
#include <gum/guminterceptor.h>
extern unsigned int GetTickCount(void);
extern volatile int widx; extern unsigned int ring[]; extern volatile int counter;
void onEnter(GumInvocationContext *ic) {
  int n = g_atomic_int_add(&counter, 1);
  if (n % ${c.every}) return;
  unsigned int base = (unsigned int)ic->cpu_context->${c.reg} + ${c.offset};
  int i = g_atomic_int_add(&widx, 1) % ${capacity};
  unsigned int *r = ring + i * ${recWords};
  unsigned int *p = (unsigned int *)base;
  r[1] = GetTickCount();
  for (int k = 0; k < ${c.size / 4}; k++) r[2 + k] = p[k];
  *(volatile unsigned int *)&r[0] = 1;
}`;
  cm = new CModule(src, { GetTickCount: Process.getModuleByName('kernel32.dll').getExportByName('GetTickCount'),
                                widx: widx, ring: ring, counter: counter });
  Interceptor.attach(ptr(c.at), { onEnter: cm.onEnter });
  setInterval(function () {
    const w = widx.readS32(); const out = [];
    while (readIdx < w && out.length < 20000) {
      const b = ring.add((readIdx % capacity) * recWords * 4);
      if (b.readU32() === 0) break;
      const rec = []; for (let k = 1; k < recWords; k++) rec.push(b.add(k * 4).readU32());
      b.writeU32(0); out.push(rec); readIdx++;
    }
    if (out.length) send({ recs: out });
  }, 100);
  console.log("attached at " + ptr(c.at) + ", template ok");
  send({ ready: true });
});
"""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--at", required=True)
    ap.add_argument("--reg", default="eax")
    ap.add_argument("--offset", type=lambda s: int(s, 0), default=0)
    ap.add_argument("--size", type=int, default=32)
    ap.add_argument("--every", type=int, default=1)
    ap.add_argument("--kill", type=float, default=30)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()

    rows = []
    ready = threading.Event()

    def on_message(m, _):
        if m["type"] == "send":
            if m["payload"].get("ready"):
                ready.set()
            for r in m["payload"].get("recs", []):
                rows.append(r)
        elif m["type"] == "log":
            print("agent:", m.get("payload"))
        elif m["type"] == "error":
            print("agent error:", m.get("description"))
            ready.set()

    dev = frida.get_local_device()
    pid = dev.spawn([EXE], cwd=GAME_DIR)
    s = dev.attach(pid)
    sc = s.create_script(AGENT)
    sc.on("message", on_message)
    sc.load()
    sc.post({"type": "cfg", "at": int(a.at, 16), "reg": a.reg, "offset": a.offset, "size": a.size,
             "every": a.every})
    ready.wait(30)
    dev.resume(pid)
    time.sleep(a.kill)
    dev.kill(pid)
    time.sleep(0.5)
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    n = a.size // 4
    t0 = rows[0][0] if rows else 0
    with open(a.out, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["ms"] + [f"w{k}" for k in range(n)] + [f"f{k}" for k in range(n)])
        for r in rows:
            words = r[1:1 + n]
            floats = [round(struct.unpack("<f", struct.pack("<I", x))[0], 5) for x in words]
            w.writerow([r[0] - t0] + [f"0x{x:08x}" for x in words] + floats)
    print(f"{len(rows)} hits -> {a.out}")


if __name__ == "__main__":
    sys.exit(main())
