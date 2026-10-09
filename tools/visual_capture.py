"""Capture frames of the game with the player's remaster settings, for looking at visual problems (shadow pop-in,
AO, particles) while the camera moves. Runs in the "main" instance (tools/instances.py), never the player's folder.

The [render] / [display] / [camera] sections are copied from the player's and.ini (orig/pc/GameData/and.ini), the
remaster defaults are on (original_defaults=0), every recovered hook is on. Frames go to work/visual/<name>/.

Usage:
  python tools/visual_capture.py --name walk --keys "500:MOUSE1,560:17*240,700:MOUSEX8*60" --dump 560:4:60
  (17 = DIK_W: hold W for 240 frames; MOUSEX8*60: turn right 8 counts per poll for 60 frames)
  --set render.cull_radius=200  overrides one setting (repeatable)
"""
import argparse
import configparser
import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import compare  # noqa: E402
import instances  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PLAYER_INI = os.path.join(ROOT, "orig", "pc", "GameData", "and.ini")


def player_sections():
    cp = configparser.ConfigParser(strict=False, interpolation=None)
    cp.optionxform = str
    cp.read(PLAYER_INI, encoding="latin1")
    out = {}
    for s in ("render", "display", "camera", "ui"):
        if cp.has_section(s):
            out[s] = dict(cp.items(s))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--name", default="capture")
    ap.add_argument("--level", default="lev_1_m1")
    ap.add_argument("--frames", type=int, default=900)
    ap.add_argument("--keys", default="500:MOUSE1")
    ap.add_argument("--dump", default="560:10:30", help="first:every:count frames to save")
    ap.add_argument("--set", action="append", default=[], help="section.key=value override")
    ap.add_argument("--timeout", type=int, default=300)
    a = ap.parse_args()

    sections = player_sections()
    sections.setdefault("display", {}).update({"window_mode": 0, "windowed": 1, "vsync": 0, "fps_limit": 0})
    sections["remaster"] = {"original_defaults": 0, "menu": 0}
    for s in a.set:
        key, value = s.split("=", 1)
        sec, k = key.split(".", 1)
        sections.setdefault(sec, {})[k] = value
    render = sections.pop("render", {})
    # [test] overrides join the run's own test settings (a [test] section here would replace the scenario's).
    test = {"keys_from_load": 0}
    test.update(sections.pop("test", {}))
    compare.DUMP = a.dump
    hooks = {n: True for n in compare.hook_names()}
    rows, crashed = compare.run_once(f"visual-{a.name}", hooks, a.level, a.frames, a.timeout, a.keys,
                                     test, render, sections)
    out = os.path.join(ROOT, "work", "visual", a.name)
    if os.path.isdir(out):
        shutil.rmtree(out)
    os.makedirs(out)
    moved = 0
    for name in os.listdir(compare.OUT):
        if name.startswith(f"visual-{a.name}-and-frame-"):
            shutil.move(os.path.join(compare.OUT, name), os.path.join(out, name.split("and-frame-")[1]))
            moved += 1
    log = os.path.join(instances.instance_dir("main"), "and.log")
    shutil.copy(log, os.path.join(out, "and.log"))
    print(f"{len(rows)} frames, {moved} images -> {out}{' (CRASH)' if crashed else ''}")
    return 1 if crashed else 0


if __name__ == "__main__":
    sys.exit(main())
