"""Removes a generator's periodic grid (Qwen Image 2.1 leaves a faint 4-pixel diagonal crosshatch that upscalers
reproduce or amplify) by notching the spectrum where the candidate has more energy than the original at the grid
frequencies. Measured on AnD_title: diagonal 4-px peak 5.5x its neighbours in the Qwen render, 1.5x in the original.

Usage: .venv/Scripts/python.exe tools/retex/degrid.py ORIGINAL CANDIDATE OUT [--periods 4] [--radius 3]
"""
import argparse

import numpy as np
from PIL import Image


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("original")
    ap.add_argument("candidate")
    ap.add_argument("out")
    ap.add_argument("--periods", default="4")
    ap.add_argument("--radius", type=int, default=3)
    a = ap.parse_args()
    cand = np.asarray(Image.open(a.candidate).convert("RGB")).astype(np.float64)
    orig = np.asarray(Image.open(a.original).convert("RGB").resize((cand.shape[1], cand.shape[0]),
                                                                  Image.LANCZOS)).astype(np.float64)
    h, w = cand.shape[:2]
    gain = np.ones((h, w))
    yy, xx = np.mgrid[0:h, 0:w]
    lo = np.mean([np.abs(np.fft.fft2(orig[..., c] - orig[..., c].mean())) for c in range(3)], axis=0)
    hi = np.mean([np.abs(np.fft.fft2(cand[..., c] - cand[..., c].mean())) for c in range(3)], axis=0)
    for per in (int(p) for p in a.periods.split(",")):
        fy, fx = h // per, w // per
        # Horizontal and both diagonals (and their conjugates); the vertical peak is real content in these images.
        for cy, cx in [(0, fx), (fy, fx), (fy, -fx)]:
            for sy, sx in [(cy, cx), (-cy, -cx)]:
                d = ((yy - sy % h + h // 2) % h - h // 2) ** 2 + ((xx - sx % w + w // 2) % w - w // 2) ** 2
                m = d <= a.radius ** 2
                ratio = np.clip(lo[m] / np.maximum(hi[m], 1e-9), 0.0, 1.0)  # only ever reduce, to the original's level
                gain[m] = np.minimum(gain[m], ratio)
    out = np.stack([np.real(np.fft.ifft2(np.fft.fft2(cand[..., c]) * gain)) for c in range(3)], axis=-1)
    print(f"notched {int((gain < 1).sum())} frequency bins")
    Image.fromarray(np.clip(out + 0.5, 0, 255).astype(np.uint8)).save(a.out)


if __name__ == "__main__":
    main()
