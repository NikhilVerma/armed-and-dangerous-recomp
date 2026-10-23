"""Z-buffers the dumped scene (depth-writing draws) by eye depth (clip w) and compares it per pixel with the depth
debug view of the same frame (white = 300 units). Uses work/shots files written by scene_check.py."""
import os

import numpy as np
from PIL import Image

os.chdir(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
lines = open("work/shots/and-vk-scene.txt").read().splitlines()
hdr = lines[0].split()
vps = {}
mats, zw = {}, {}
for i in range(1, len(lines), 2):
    head = lines[i].split()
    vps[int(head[0])] = list(map(int, [h for h in head if h.startswith("vp=")][0][3:].split(",")))
    mats[int(head[0])] = np.array(list(map(float, lines[i + 1].split()[1:]))).reshape(4, 4)
    zw[int(head[0])] = "zwrite=1" in head

depth = np.asarray(Image.open("work/shots/scene-depth.bmp").convert("L")).astype(np.float32)
H, W = depth.shape
zbuf = np.full((H, W), np.inf)
groups, cur, verts = {}, None, []
for ln in open("work/shots/and-vk-scene.obj"):
    if ln.startswith("o "):
        cur = int(ln.split("_")[1])
        groups[cur] = []
    elif ln.startswith("v "):
        verts.append(list(map(float, ln.split()[1:4])))
    elif ln.startswith("f "):
        groups[cur].append([int(x) - 1 for x in ln.split()[1:4]])
verts = np.array(verts)
for d, faces in groups.items():
    if not zw[d] or not faces:
        continue
    vp = vps[d]
    f = np.array(faces)
    p = np.c_[verts, np.ones(len(verts))]
    used = np.unique(f)
    cl = np.zeros((len(verts), 4))
    cl[used] = p[used] @ mats[d].T
    for a, b, c in f:
        t = cl[[a, b, c]]
        if (t[:, 3] <= 1e-3).any():
            continue
        sx = vp[0] + (t[:, 0] / t[:, 3] + 1) / 2 * vp[2]
        sy = vp[1] + (1 - t[:, 1] / t[:, 3]) / 2 * vp[3]
        x0, x1 = max(int(np.floor(sx.min())), 0), min(int(np.ceil(sx.max())), W - 1)
        y0, y1 = max(int(np.floor(sy.min())), 0), min(int(np.ceil(sy.max())), H - 1)
        if x0 > x1 or y0 > y1:
            continue
        den = (sy[1] - sy[2]) * (sx[0] - sx[2]) + (sx[2] - sx[1]) * (sy[0] - sy[2])
        if abs(den) < 1e-9:
            continue
        xs, ys = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
        l0 = ((sy[1] - sy[2]) * (xs - sx[2]) + (sx[2] - sx[1]) * (ys - sy[2])) / den
        l1 = ((sy[2] - sy[0]) * (xs - sx[2]) + (sx[0] - sx[2]) * (ys - sy[2])) / den
        l2 = 1 - l0 - l1
        inside = (l0 >= 0) & (l1 >= 0) & (l2 >= 0)
        # perspective-correct w: 1/w interpolates linearly in screen space
        w = 1.0 / (l0 / t[0, 3] + l1 / t[1, 3] + l2 / t[2, 3])
        sub = zbuf[y0:y1 + 1, x0:x1 + 1]
        np.minimum(sub, np.where(inside, w, np.inf), out=sub)

valid = (depth < 250) & np.isfinite(zbuf)
ref = depth[valid] / 255.0 * 300.0
got = np.minimum(zbuf[valid], 300.0)
err = np.abs(ref - got)
print("pixels compared", valid.sum(), "of depth-covered", (depth < 250).sum())
print("eye-depth error (units): median %.2f, p95 %.2f; within 2 units: %.3f" %
      (np.median(err), np.percentile(err, 95), (err < 2).mean()))
vis = np.clip(np.where(np.isfinite(zbuf), zbuf, 300) / 300 * 255, 0, 255).astype(np.uint8)
Image.fromarray(np.concatenate([depth.astype(np.uint8), vis], 1)).save("work/shots/scene_zcheck.png")
