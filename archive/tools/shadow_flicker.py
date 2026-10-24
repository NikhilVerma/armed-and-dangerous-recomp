"""Shadow-only flicker: the shadow term of each frame is (frame with shadows) / (same frame without shadows) from
two deterministic captures of the same run (archive/tools/visual_capture.py with --set render.shadow_maps=0 for the second).
Reports how much the shadow term toggles between consecutive frames (darker-lighter-darker), which is flicker, not
motion, and writes a map of where it happens.

Usage: .venv/Scripts/python.exe archive/tools/shadow_flicker.py work/visual/fl_maps work/visual/fl_none --first 600
       --last 639 [--map out.png]
"""
import argparse
import os

import numpy as np
from PIL import Image


def load(folder, n):
    return np.asarray(Image.open(os.path.join(folder, n)).convert("L")).astype(np.float32) + 4.0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("shadowed")
    ap.add_argument("plain")
    ap.add_argument("--first", type=int, default=0)
    ap.add_argument("--last", type=int, default=10 ** 9)
    ap.add_argument("--threshold", type=float, default=0.08, help="shadow-term change counted (0..1)")
    ap.add_argument("--map")
    a = ap.parse_args()
    names = sorted(n for n in os.listdir(a.shadowed)
                   if n.endswith(".bmp") and a.first <= int(n[:-4]) <= a.last and os.path.exists(os.path.join(a.plain, n)))
    terms = [np.clip(load(a.shadowed, n) / load(a.plain, n), 0, 1.5) for n in names]
    toggles = np.zeros(terms[0].shape, np.int32)
    rates, changes = [], []
    for k in range(1, len(terms) - 1):
        d1, d2 = terms[k] - terms[k - 1], terms[k + 1] - terms[k]
        t = (np.abs(d1) > a.threshold) & (np.abs(d2) > a.threshold) & (np.sign(d1) != np.sign(d2))
        toggles += t
        rates.append(t.mean())
        changes.append(np.abs(d1).mean())
    print(f"{len(names)} frames: shadow term mean |change| {np.mean(changes):.4f}, toggling {100 * np.mean(rates):.3f}% "
          f"of pixels per frame (max {100 * np.max(rates):.3f}%)")
    if a.map:
        m = (255 * toggles / max(1, toggles.max())).astype(np.uint8)
        Image.fromarray(m).save(a.map)


if __name__ == "__main__":
    main()
