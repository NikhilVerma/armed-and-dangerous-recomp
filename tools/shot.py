"""Screenshots of the running game at chosen frames, with extra scripted input and and.ini settings.

Usage:
  python tools/shot.py [--scenario play] [--frames 600,900] [--keys "880:ESC,900:DOWN"] [--frames-total N]
                       [--set section.key=value ...] [--name NAME] [--crop x0,y0,x1,y1] [--scale 2]

Writes work/shots/<name>-<frame>.png (and a side-by-side <name>.png when several frames are given). The scenario's
own keys run first; --keys adds to them (presented-frame numbers). --set writes and.ini values for this run only
(e.g. --set render.backend=vulkan --set camera.hor_plus=1 --set display.width=1280).
"""
import argparse
import glob
import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import compare as c  # noqa: E402
import instances  # noqa: E402
GD = instances.instance_dir("main")  # runs never touch the player's game folder
from PIL import Image  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--scenario", default="play")
    ap.add_argument("--level", default="", help="load this level instead of the scenario's (e.g. lev_2_m1)")
    ap.add_argument("--frames", default="600")
    ap.add_argument("--keys", default="")
    ap.add_argument("--frames-total", type=int, default=0)
    ap.add_argument("--set", action="append", default=[])
    ap.add_argument("--name", default="shot")
    ap.add_argument("--crop", default="")
    ap.add_argument("--scale", type=float, default=1.0)
    a = ap.parse_args()
    os.chdir(ROOT)
    sc = c.SCENARIOS[a.scenario]
    frames = [int(f) for f in a.frames.split(",") if f]
    keys = ",".join(k for k in (sc.get("keys", ""), a.keys) if k)
    sections = {"test": dict(sc.get("test", {})), "render": {"rhi": 1}}
    for s in a.set:
        key, value = s.split("=", 1)
        section, name = key.split(".", 1)
        sections.setdefault(section, {})[name] = value
    test = sections.pop("test")
    render = sections.pop("render")
    steps = {b - a_ for a_, b in zip(frames, frames[1:])}
    if len(steps) > 1:
        sys.exit("--frames must be evenly spaced ([test] hash_frames is first:every:count)")
    hash_frames = f"{frames[0]}:{steps.pop() if steps else 1}:{len(frames)}"
    for f in glob.glob(os.path.join(GD, "and-frame-*.bmp")):
        os.remove(f)
    total = a.frames_total or max(frames) + 20
    c.run_once(a.name, {k: True for k in c.hook_names()}, a.level or sc["level"], total, 300, keys,
               {"hash_frames": hash_frames, "dump_hashed": 1, **test}, render, extra_sections=sections)
    shots = []
    for f in frames:
        src = os.path.join(GD, f"and-frame-{f:05d}.bmp")
        if not os.path.exists(src):
            print(f"missing frame {f}")
            continue
        im = Image.open(src).convert("RGB")
        if a.crop:
            im = im.crop(tuple(int(v) for v in a.crop.split(",")))
        if a.scale != 1.0:
            im = im.resize((int(im.width * a.scale), int(im.height * a.scale)), Image.LANCZOS)
        out = os.path.join("work", "shots", f"{a.name}-{f}.png")
        im.save(out)
        shots.append(im)
        print(out)
    if len(shots) > 1:
        w = sum(s.width for s in shots) + 8 * (len(shots) - 1)
        o = Image.new("RGB", (w, max(s.height for s in shots)), "white")
        x = 0
        for s in shots:
            o.paste(s, (x, 0))
            x += s.width + 8
        o.save(os.path.join("work", "shots", f"{a.name}.png"))
        print(os.path.join("work", "shots", f"{a.name}.png"))


if __name__ == "__main__":
    main()
