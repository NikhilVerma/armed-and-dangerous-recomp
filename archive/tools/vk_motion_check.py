"""Checks motion vectors against block-matched pixel displacement between frames F-1 and F (Vulkan backend), with
a scripted mouse turn at F-1. Usage: vk_motion_check.py [frame=600] [turn=2]"""
import glob
import os
import shutil
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(1, os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), "tools"))  # compare.py, instances.py (shared with the oracle tools)
import compare as c  # noqa: E402
from PIL import Image  # noqa: E402

os.chdir(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
sc = c.SCENARIOS["play"]
names = c.hook_names()
F = int(sys.argv[1]) if len(sys.argv) > 1 else 600
turn = sys.argv[2] if len(sys.argv) > 2 else "2"
keys = sc["keys"] + f",{F - 1}:MOUSEX{turn}"
SCALE = 5.0  # 50 * exposure 0.1


def run(view, first, count, extra):
    for f in glob.glob(os.path.join(c.GAME_DIR, "and-frame-*.bmp")):
        os.remove(f)
    c.run_once("vk " + view, {k: True for k in names}, sc["level"], F + 20, 240, keys,
               {"hash_frames": f"{first}:1:{count}", "dump_hashed": 1},
               {"rhi": 1, "backend": "vulkan", "debug_view": view, **extra})
    out = []
    for i in range(count):
        dst = f"work/shots/mc-{view}-{first + i}.bmp"
        shutil.copy(os.path.join(c.GAME_DIR, f"and-frame-{first + i:05d}.bmp"), dst)
        out.append(np.asarray(Image.open(dst).convert("RGB")).astype(np.float32))
    return out


prev, cur = run("none", F - 1, 2, {})
(mot,) = run("motion", F, 1, {"exposure": 0.1})
h, w, _ = cur.shape
gp, gc = prev.mean(2), cur.mean(2)
R, S = 6, 24
rows = []
for y in range(160, 400, 40):
    for x in range(40, 620, 60):
        m = (mot[y, x, :2] / 255.0 - 0.5) / SCALE  # prevUv - curUv
        if abs(m[0]) < 1e-6 and abs(m[1]) < 1e-6:
            continue
        exp = (m[0] * w, m[1] * h)
        patch = gc[y - R:y + R + 1, x - R:x + R + 1]
        best, bo = 1e18, (0, 0)
        for dy in range(-S, S + 1):
            for dx in range(-S, S + 1):
                yy, xx = y + dy, x + dx
                if yy - R < 0 or xx - R < 0 or yy + R >= h or xx + R >= w:
                    continue
                e = ((gp[yy - R:yy + R + 1, xx - R:xx + R + 1] - patch) ** 2).sum()
                if e < best:
                    best, bo = e, (dx, dy)
        rows.append((x, y, exp, bo))
        print(f"({x:3d},{y:3d}) motion {exp[0]:6.1f},{exp[1]:6.1f}  matched {bo[0]:4d},{bo[1]:4d}")
err = [np.hypot(e[0] - b[0], e[1] - b[1]) for _, _, e, b in rows]
print("median error px", np.median(err) if err else None, "n", len(err))
