"""Parity between renderer backends: the same deterministic scenario through rhi::d3d9 (the reference) and another
backend (default vulkan), compared per frame.

Usage: python tools/compare_backends.py [--scenario menu,level,...] [--backend vulkan] [--frames first:every:count]

For each scenario (tools/compare.py SCENARIOS) both runs use the virtual clock and scripted input, with every
recovered function enabled and [render] rhi=1. Checks:
  1. draws per frame ([test] rhi_draws=1) must be identical: missing or extra geometry fails at once;
  2. frames saved at --frames ([test] hash_frames + dump_hashed) are compared as images. Exact pixels are not
     expected across APIs (rasterisation rules, filtering, precision), so a frame passes when
       mean absolute error <= MAE_MAX (0..255 scale) and
       the share of pixels differing by more than PIXEL_TOLERANCE in any channel <= BAD_SHARE_MAX.
In level scenarios, frames are numbered from the end of the level load ([test] hash_from_load): loading takes a
different number of frames per backend, but game time afterwards is deterministic. Draw counts are compared the same
way (from the load end); loading frames are not compared.
Writes work/backends/<scenario>/ (both runs' frames, a diff image per failing frame) and prints a summary.
Exit code 1 on any failure.
"""
import argparse
import os
import shutil
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import compare  # noqa: E402
import instances  # noqa: E402
import queue
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAE_MAX = 3.0
PIXEL_TOLERANCE = 24
BAD_SHARE_MAX = 0.02


def shift_keys(keys, offset):
    """frame:KEY,... with every frame moved by -offset (keys before the load end are dropped)."""
    out = []
    for item in filter(None, keys.split(",")):
        f, k = item.split(":", 1)
        if int(f) - offset >= 0:
            out.append(f"{int(f) - offset}:{k}")
    return ",".join(out)


def run(label, backend, sc, frames_spec, timeout, keys=None, keys_from_load=False, gd=None, out=print):
    gd = gd or instances.instance_dir("main")  # never the player's game folder
    names = compare.hook_names()
    for name in os.listdir(gd):
        if name.startswith("and-frame-") or name == "and-rhi-draws.txt":
            os.remove(os.path.join(gd, name))
    test = dict(sc.get("test") or {})
    test.update({"rhi_draws": 1, "hash_frames": frames_spec, "dump_hashed": 1,
                 "hash_from_load": 1 if sc["level"] else 0, "keys_from_load": 1 if keys_from_load else 0,
                 "reseed_on_load": 1 if sc["level"] else 0})
    compare.run_once(label, {n: True for n in names}, sc["level"], sc["frames"], timeout,
                     sc.get("keys", "") if keys is None else keys, test,
                     {"rhi": 1, "backend": backend}, game_dir=gd, out=out)
    draws = {}
    path = os.path.join(gd, "and-rhi-draws.txt")
    if os.path.exists(path):
        for line in open(path):
            f, n = line.split()
            draws[int(f)] = int(n)
    images = {}
    for name in os.listdir(gd):
        if name.startswith("and-frame-") and name.endswith(".bmp"):
            images[int(name[10:15])] = os.path.join(gd, name)
    log = open(os.path.join(gd, "and.log"), encoding="latin1").read()
    import re
    m = re.search(r"level load finished at frame (\d+)", log)
    if sc["level"]:
        start = int(m.group(1)) if m else None
        draws = {f - start: n for f, n in draws.items() if start is not None and f >= start}
    return draws, images, log, (int(m.group(1)) if m else None)


def check_scenario(name, a, gd=None, out=print):
    """One comparison of a scenario between the reference (d3d9) and a.backend; True when it passes."""
    failed = False
    sc = compare.SCENARIOS[name]
    folder = os.path.join(ROOT, "work", "backends", name)
    shutil.rmtree(folder, ignore_errors=True)
    os.makedirs(folder)
    out(f"[{name}]")
    ref_draws, ref_images, _, ref_load = run(a.reference, a.reference, sc, a.frames, a.timeout, gd=gd, out=out)
    ref_saved = {f: shutil.copy(p, os.path.join(folder, f"d3d9-{f:05d}.bmp")) for f, p in ref_images.items()}
    # Scripted input: the reference ran it on absolute frames; the other backend gets it at the same game time,
    # counted from its own load end.
    keys, from_load = None, False
    if sc["level"] and sc.get("keys") and ref_load is not None:
        keys, from_load = shift_keys(sc["keys"], ref_load), True
    new_draws, new_images, log, _ = run(a.backend, a.backend, sc, a.frames, a.timeout, keys, from_load, gd=gd, out=out)
    new_saved = {f: shutil.copy(p, os.path.join(folder, f"{a.backend}-{f:05d}.bmp")) for f, p in new_images.items()}
    if a.backend != "d3d9" and "backend " + a.backend not in log:
        out(f"  FAIL: the {a.backend} backend did not start (see and.log)")
        return False
    # 1. draw counts
    frames = sorted(set(ref_draws) | set(new_draws))
    bad = [f for f in frames if ref_draws.get(f) != new_draws.get(f)]
    if bad:
        f = bad[0]
        out(f"  FAIL draws: {len(bad)} frames differ, first {f}: d3d9 {ref_draws.get(f)} vs {a.backend} {new_draws.get(f)}")
        failed = True
    else:
        out(f"  draws: {len(frames)} frames identical")
    # 2. images
    for f in sorted(ref_saved):
        if f not in new_saved:
            out(f"  FAIL frame {f}: missing from {a.backend}")
            failed = True
            continue
        x = np.asarray(Image.open(ref_saved[f]).convert("RGB"), dtype=np.int16)
        y = np.asarray(Image.open(new_saved[f]).convert("RGB"), dtype=np.int16)
        if x.shape != y.shape:
            out(f"  FAIL frame {f}: size {x.shape} vs {y.shape}")
            failed = True
            continue
        d = np.abs(x - y)
        mae = float(d.mean())
        share = float((d.max(axis=2) > PIXEL_TOLERANCE).mean())
        ok = mae <= MAE_MAX and share <= BAD_SHARE_MAX
        out(f"  frame {f}: MAE {mae:.2f}, {share * 100:.2f}% pixels off -> {'ok' if ok else 'FAIL'}")
        if not ok:
            failed = True
            Image.fromarray(np.clip(d * 4, 0, 255).astype(np.uint8)).save(os.path.join(folder, f"diff-{f:05d}.png"))
    return not failed


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--scenario", default="menu,level,play")
    ap.add_argument("--backend", default="vulkan")
    ap.add_argument("--reference", default="d3d9", help="reference backend (d3d9; vulkan to test vulkan's own determinism)")
    ap.add_argument("--frames", default="100:100:9")
    ap.add_argument("--timeout", type=int, default=240)
    ap.add_argument("--jobs", type=int, default=0, help="scenarios at once, each in its own game instance (default 1: in "
                    "the game folder, one after another; more is faster but timing-flaky)")
    a = ap.parse_args()
    scenarios = a.scenario.split(",")
    jobs = a.jobs or 1  # parallel runs add texture-streaming timing differences (+-1 draw), so one at a time
    free = queue.Queue()
    for n in range(jobs):
        free.put(n)

    def one(name):
        lines = []
        out = lines.append
        slot = free.get() if jobs > 1 else None
        try:
            gd = instances.instance_dir(slot) if slot is not None else None
            if check_scenario(name, a, gd, out):
                return lines, False
            # A real backend difference fails every time; timing-dependent game state (texture streaming, measured
            # as an occasional lev_1_m1 frame 200 difference in either backend) can make one frame differ once.
            out(f"  {name}: failed once; running it again to rule out a timing-dependent frame")
            if check_scenario(name, a, gd, out):
                out(f"  {name}: passed on the second run (flaky frame)")
                return lines, False
            return lines, True
        finally:
            if slot is not None:
                free.put(slot)

    with ThreadPoolExecutor(max(jobs, 1)) as pool:
        results = list(pool.map(one, scenarios))
    failed = False
    for lines, bad in results:
        print("\n".join(lines))
        failed |= bad
    print("FAIL" if failed else "PASS")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
