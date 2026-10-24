"""Checks the Vulkan scene description (archive/src/rhi/vulkan/vk_scene.h): runs the play scenario with
[render] vk_dump_scene=F and the depth debug view, then projects the dumped world-space triangles with each draw's
camera and viewport; they must cover what the depth buffer shows. archive/tools/vk_scene_zcheck.py then compares depths.
Usage: vk_scene_check.py [frame=600]"""
import glob
import os
import shutil
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(1, os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), "tools"))  # compare.py, instances.py (shared with the oracle tools)
import compare as c  # noqa: E402
from PIL import Image, ImageDraw  # noqa: E402

os.chdir(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
sc = c.SCENARIOS["play"]
F = int(sys.argv[1]) if len(sys.argv) > 1 else 600
for f in glob.glob(os.path.join(c.GAME_DIR, "and-frame-*.bmp")) + glob.glob(os.path.join(c.GAME_DIR, "and-vk-scene.*")):
    os.remove(f)
c.run_once("vk scene", {k: True for k in c.hook_names()}, sc["level"], F + 20, 240, sc["keys"],
           {"hash_frames": f"{F}:1:1", "dump_hashed": 1},
           {"rhi": 1, "backend": "vulkan", "debug_view": "depth", "vk_dump_scene": F})
for n in ("and-vk-scene.txt", "and-vk-scene.obj"):
    shutil.copy(os.path.join(c.GAME_DIR, n), "work/shots/" + n)
shutil.copy(os.path.join(c.GAME_DIR, f"and-frame-{F:05d}.bmp"), "work/shots/scene-depth.bmp")

lines = open("work/shots/and-vk-scene.txt").read().splitlines()
hdr = lines[0].split()
vps = {}
mats, kinds = {}, {"yes": 0, "no": 0, "vs": 0}
i = 1
while i < len(lines):
    head = lines[i].split()
    idx = int(head[0])
    tris = [t for t in head if t.startswith("tris=")][0][5:]
    kinds[tris] += 1
    kinds["vs"] += "vs=1" in head
    vps[idx] = list(map(int, [h for h in head if h.startswith("vp=")][0][3:].split(",")))
    mats[idx] = np.array(list(map(float, lines[i + 1].split()[1:]))).reshape(4, 4)
    i += 2
print("draws", len(mats), kinds, "viewports", sorted({tuple(v) for v in vps.values()}))

depth = np.asarray(Image.open("work/shots/scene-depth.bmp").convert("L")).astype(np.float32)
H, W = depth.shape
mask = Image.new("L", (W, H), 0)
draw = ImageDraw.Draw(mask)
cur, verts, ntri = None, [], 0
for ln in open("work/shots/and-vk-scene.obj"):
    if ln.startswith("o "):
        cur = int(ln.split("_")[1])
    elif ln.startswith("v "):
        verts.append((list(map(float, ln.split()[1:4])), cur))
    elif ln.startswith("f "):
        ids = [int(x) - 1 for x in ln.split()[1:4]]
        pts = []
        for k in ids:
            p, dr = verts[k]
            m, vp = mats[dr], vps[dr]
            cl = m @ np.array([*p, 1.0])
            if cl[3] <= 1e-4:
                break
            nx, ny = cl[0] / cl[3], cl[1] / cl[3]
            pts.append((vp[0] + (nx + 1) / 2 * vp[2], vp[1] + (1 - ny) / 2 * vp[3]))
        if len(pts) == 3:
            draw.polygon(pts, fill=255)
            ntri += 1
m = np.asarray(mask) > 0
geom = depth < 250  # nearer than the depth view's white (far plane, sky)
print("triangles", ntri)
print("depth-covered pixels also covered by the scene triangles: %.3f" % ((m & geom).sum() / max(geom.sum(), 1)))
out = np.zeros((H, W, 3), np.uint8)
out[..., 0] = geom * 255
out[..., 1] = m * 255
Image.fromarray(out).save("work/shots/scene_check.png")
