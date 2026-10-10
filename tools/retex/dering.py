"""De-ringing clamp for upscaled textures: each pixel of the candidate is limited to the range (min..max) that a
smooth bicubic upscale of the original has in its neighbourhood, plus a small tolerance. Edges stay as sharp as the
upscaler made them, but halos (overshoot past both sides of an edge) and bright rims are cut off.

Usage: .venv/Scripts/python.exe tools/retex/dering.py ORIGINAL CANDIDATE OUT [--radius 3] [--tolerance 4]
"""
import argparse

import numpy as np
from PIL import Image


def local(a, r, op):
    """Separable (2r+1)^2 min or max filter with edge padding."""
    p = np.pad(a, ((r, r), (0, 0), (0, 0)), mode="edge")
    out = p[0:a.shape[0]]
    for i in range(1, 2 * r + 1):
        out = op(out, p[i:i + a.shape[0]])
    p = np.pad(out, ((0, 0), (r, r), (0, 0)), mode="edge")
    out = p[:, 0:a.shape[1]]
    for i in range(1, 2 * r + 1):
        out = op(out, p[:, i:i + a.shape[1]])
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("original")
    ap.add_argument("candidate")
    ap.add_argument("out")
    ap.add_argument("--radius", type=int, default=3)
    ap.add_argument("--tolerance", type=float, default=4.0)
    a = ap.parse_args()
    cand = Image.open(a.candidate).convert("RGB")
    bic = np.asarray(Image.open(a.original).convert("RGB").resize(cand.size, Image.BICUBIC)).astype(np.float32)
    hr = np.asarray(cand).astype(np.float32)
    lo = local(bic, a.radius, np.minimum) - a.tolerance
    hi = local(bic, a.radius, np.maximum) + a.tolerance
    out = np.clip(hr, lo, hi)
    print(f"clamped {100 * float((out != hr).any(axis=2).mean()):.2f}% of pixels")
    Image.fromarray(np.clip(out + 0.5, 0, 255).astype(np.uint8)).save(a.out)


if __name__ == "__main__":
    main()
