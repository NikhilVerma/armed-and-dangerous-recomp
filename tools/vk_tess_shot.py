"""Side-by-side screenshots of the Vulkan backend with tessellation off and on (play scenario), optionally in
wireframe, into work/shots/tess.png. Usage: vk_tess_shot.py [frame=600] [wire=1] [extra render key=value ...]"""
import glob
import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import compare as c  # noqa: E402
from PIL import Image  # noqa: E402

os.chdir(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sc = c.SCENARIOS["play"]
frame = int(sys.argv[1]) if len(sys.argv) > 1 else 600
wire = sys.argv[2] if len(sys.argv) > 2 else "1"
extra = dict(a.split("=", 1) for a in sys.argv[3:])
shots = []
for tess in ("0", "1"):
    for f in glob.glob(os.path.join(c.GAME_DIR, "and-frame-*.bmp")):
        os.remove(f)
    c.run_once("tess " + tess, {k: True for k in c.hook_names()}, sc["level"], frame + 20, 240, sc["keys"],
               {"hash_frames": f"{frame}:1:1", "dump_hashed": 1},
               {"rhi": 1, "backend": "vulkan", "tessellation": tess, "wireframe": wire, **extra})
    dst = f"work/shots/tess-{tess}.bmp"
    shutil.copy(os.path.join(c.GAME_DIR, f"and-frame-{frame:05d}.bmp"), dst)
    shots.append(Image.open(dst).convert("RGB"))
w, h = shots[0].size
out = Image.new("RGB", (w * 2 + 8, h), "white")
out.paste(shots[0], (0, 0))
out.paste(shots[1], (w + 8, 0))
out.save("work/shots/tess.png")
print("work/shots/tess.png")
