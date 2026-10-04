"""Captures the Vulkan backend's debug views (colour, depth, normals, motion) at one frame of the play scenario into
work/shots/debug_views.png. Usage: vk_debug_views.py [frame=600] [mouse turn counts, e.g. 2]"""
import glob
import os
import shutil
import sys

sys.path.insert(0, r"C:\Users\nikhi\and-decomp\tools")
import compare as c  # noqa: E402
from PIL import Image  # noqa: E402

os.chdir(r"C:\Users\nikhi\and-decomp")
sc = c.SCENARIOS["play"]
names = c.hook_names()
views = ["none", "depth", "normals", "motion"]
frame = int(sys.argv[1]) if len(sys.argv) > 1 else 600
for v in views:
    for f in glob.glob(os.path.join(c.GAME_DIR, "and-frame-*.bmp")):
        os.remove(f)
    keys = sc["keys"] + (f",{frame - 1}:MOUSEX{sys.argv[2]}" if len(sys.argv) > 2 else "")
    c.run_once("vk " + v, {k: True for k in names}, sc["level"], frame + 20, 240, keys,
               {"hash_frames": f"{frame}:1:1", "dump_hashed": 1}, {"rhi": 1, "backend": "vulkan", "debug_view": v})
    shutil.copy(os.path.join(c.GAME_DIR, f"and-frame-{frame:05d}.bmp"), f"work/shots/dv-{v}.bmp")
ims = [Image.open(f"work/shots/dv-{v}.bmp").convert("RGB") for v in views]
w, h = ims[0].size
o = Image.new("RGB", (w * 2 + 8, h * 2 + 8), "white")
for k, i in enumerate(ims):
    o.paste(i, ((k % 2) * (w + 8), (k // 2) * (h + 8)))
o.save("work/shots/debug_views.png")
print("ok")
