"""Measure frame-to-frame flicker in captured frames (work/visual/<name>/*.bmp, consecutive frames).

For each consecutive pair: mean absolute luminance change, and the share of pixels whose change is a flicker (the
pixel changes by more than `--threshold` and changes back within the next frame: up-down or down-up). Motion makes
pixels change steadily; flicker makes them toggle. Compare a capture against the same run with the suspect
feature off (e.g. shadows) to isolate it.

Usage: .venv/Scripts/python.exe tools/flicker.py work/visual/fl_maps work/visual/fl_none [--first 580 --last 600]
       [--threshold 12] [--box x0,y0,x1,y1] [--map out.png]  (out.png: where toggles happen, summed)
"""
import argparse
import os

import numpy as np
from PIL import Image


def frames(folder, first, last):
    names = sorted(n for n in os.listdir(folder) if n.endswith(".bmp") and first <= int(n[:-4]) <= last)
    return names, [np.asarray(Image.open(os.path.join(folder, n)).convert("L")).astype(np.int16) for n in names]


def analyse(folder, a):
    names, imgs = frames(folder, a.first, a.last)
    if a.box:
        x0, y0, x1, y1 = (int(v) for v in a.box.split(","))
        imgs = [i[y0:y1, x0:x1] for i in imgs]
    toggles = np.zeros(imgs[0].shape, np.int32)
    diffs, rates = [], []
    for k in range(1, len(imgs) - 1):
        d1 = imgs[k] - imgs[k - 1]
        d2 = imgs[k + 1] - imgs[k]
        t = (np.abs(d1) > a.threshold) & (np.abs(d2) > a.threshold) & (np.sign(d1) != np.sign(d2))
        toggles += t
        diffs.append(np.abs(d1).mean())
        rates.append(t.mean())
    print(f"{folder}: {len(imgs)} frames, mean |change| {np.mean(diffs):.2f}, toggling pixels {100 * np.mean(rates):.3f}%"
          f" per frame (max {100 * np.max(rates):.3f}%)")
    return toggles


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("folders", nargs="+")
    ap.add_argument("--first", type=int, default=0)
    ap.add_argument("--last", type=int, default=10 ** 9)
    ap.add_argument("--threshold", type=int, default=12)
    ap.add_argument("--box")
    ap.add_argument("--map")
    a = ap.parse_args()
    maps = [analyse(f, a) for f in a.folders]
    if a.map:
        m = maps[0].astype(np.float32)
        m = (255 * m / max(1.0, m.max())).astype(np.uint8)
        Image.fromarray(m).save(a.map)
        print("toggle map ->", a.map)


if __name__ == "__main__":
    main()
