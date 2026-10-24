"""Benchmark mode: a canned traversal of lev_1_m1 with the remaster settings, for checking visuals in motion and for
frame times. Runs in its own game instance (tools/instances.py), never in the player's game folder.

Usage:
  python archive/tools/bench.py [--name NAME] [--every 20] [--set section.key=value ...] [--original]

Writes work/bench/<NAME>/sheet.png (a contact sheet of every --every-th frame, numbered), the frames themselves, and
prints the frame-time summary (and.log "fps:" lines, wall clock; the game clock steps 1/60 s per frame).

The traversal (frames; key names as [test] keys_frames, "*N" holds a key for N frames). Controls measured on the
default bindings: W forward, S back, A / D strafe, mouse turns, arrow keys look up and down, Mouse 1 fires and
dismisses message boxes:
  500  dismiss the briefing
  530  walk forward to the Extras token (the painting), its message box at about 680
  700  dismiss it; walk back, turn right, walk, turn left, strafe, walk back over the same ground
"""
import argparse
import glob
import os
import re
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(1, os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), "tools"))  # compare.py, instances.py (shared with the oracle tools)
import compare as c  # noqa: E402
import instances  # noqa: E402
from PIL import Image, ImageDraw  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # the repository (this tool lives in archive/tools)
W, S, A, D = 17, 31, 30, 32  # DIK scan codes
ROUTE = ",".join([
    "500:MOUSE1",
    f"530:{W}*150",
    "700:MOUSE1", "712:MOUSE1",
    f"730:{S}*120",
    "860:MOUSEX30*40",
    f"910:{W}*180",
    "1100:MOUSEX-30*40",
    f"1150:{A}*80",
    f"1240:{W}*160",
    f"1410:{S}*180",
    "1600:MOUSEX-20*60",
    f"1670:{D}*90",
])
TOTAL = 1800


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--name", default="bench")
    ap.add_argument("--every", type=int, default=20)
    ap.add_argument("--first", type=int, default=520)
    ap.add_argument("--set", action="append", default=[])
    ap.add_argument("--original", action="store_true", help="the original game's settings (no remaster)")
    ap.add_argument("--width", type=int, default=1920)
    ap.add_argument("--height", type=int, default=1080)
    a = ap.parse_args()
    gd = instances.instance_dir("bench")
    count = (TOTAL - 10 - a.first) // a.every + 1
    c.DUMP = f"{a.first}:{a.every}:{count}"
    for f in glob.glob(os.path.join(gd, "and-frame-*")):
        os.remove(f)
    extra = {"display": {"window_mode": 1, "width": a.width, "height": a.height},
             "remaster": {"original_defaults": 1 if a.original else 0},
             "render": {"rhi": 1, "backend": "vulkan"}}
    for kv in a.set:
        key, v = kv.split("=", 1)
        section, k = key.split(".", 1)
        extra.setdefault(section, {})[k] = v
    sc = c.SCENARIOS["play"]
    out = os.path.join(ROOT, "work", "bench", a.name)
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    c.OUT = out  # run_once moves the dumped frames here
    c.run_once(a.name, {n: True for n in c.hook_names()}, sc["level"], TOTAL, 600, ROUTE,
               dict(sc.get("test") or {}), None, extra, game_dir=gd)
    frames = sorted(glob.glob(os.path.join(out, f"{a.name}-and-frame-*.bmp")))
    if frames:
        tw, th = 480, 270
        cols = 6
        rows = (len(frames) + cols - 1) // cols
        sheet = Image.new("RGB", (tw * cols, th * rows))
        for i, f in enumerate(frames):
            im = Image.open(f).convert("RGB").resize((tw, th))
            ImageDraw.Draw(im).text((6, 6), re.search(r"(\d{5})", os.path.basename(f)).group(1)[-5:],
                                    fill=(255, 255, 0))
            sheet.paste(im, ((i % cols) * tw, (i // cols) * th))
        sheet.save(os.path.join(out, "sheet.png"))
    log = open(os.path.join(gd, "and.log"), encoding="latin1").read().splitlines()
    fps = [float(m.group(1)) for l in log for m in [re.search(r"fps: ([\d.]+)", l)] if m]
    crash = [l for l in log if "CRASH" in l]
    print(f"{len(frames)} frames -> {os.path.join(out, 'sheet.png')}")
    if fps:
        print(f"fps (2 s windows): min {min(fps):.1f}, mean {sum(fps) / len(fps):.1f}, max {max(fps):.1f}")
    if crash:
        print("\n".join(crash))
    return 1 if crash else 0


if __name__ == "__main__":
    sys.exit(main())
