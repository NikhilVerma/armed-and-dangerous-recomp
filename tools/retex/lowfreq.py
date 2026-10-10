"""Low-frequency lock: the candidate's fine detail on the original's broad colour and values.
out = candidate - blur(candidate) + blur(original), per channel (Gaussian, sigma in pixels of the candidate).
A generative re-render that drifted in colour or brightness (a darker cannon, a deeper red) is pulled back to the
source wherever the drift is broader than sigma, while edges and texture finer than sigma stay as rendered.

Usage: .venv/Scripts/python.exe tools/retex/lowfreq.py ORIGINAL CANDIDATE OUT [--sigma 2.0]
"""
import argparse
import os
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import judge  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("original")
    ap.add_argument("candidate")
    ap.add_argument("out")
    ap.add_argument("--sigma", type=float, default=2.0)
    a = ap.parse_args()
    cand = Image.open(a.candidate).convert("RGB")
    orig = np.asarray(Image.open(a.original).convert("RGB").resize(cand.size, Image.BICUBIC)).astype(np.float64)
    c = np.asarray(cand).astype(np.float64)
    out = np.stack([c[..., i] - judge.blur(c[..., i], a.sigma) + judge.blur(orig[..., i], a.sigma) for i in range(3)],
                   axis=-1)
    Image.fromarray(np.clip(out + 0.5, 0, 255).astype(np.uint8)).save(a.out)


if __name__ == "__main__":
    main()
