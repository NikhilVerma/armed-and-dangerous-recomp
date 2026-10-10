"""Makes a regenerated repeating texture tile again (Qwen does not know the texture wraps; measured on B_T_tree_bark_1:
edge mismatch 24.7 vs 14.9 between ordinary neighbours). Offset blend: a copy shifted by half the width and height
has continuous wrap edges (its borders come from the interior); near the borders the result uses that copy, in the
middle the original, with a smooth window between them. The copy's own seam sits in the middle, where it is unused.

Usage: .venv/Scripts/python.exe tools/retex/tileable.py IN OUT [--band 0.18]
"""
import argparse

import numpy as np
from PIL import Image


def window(n, band):
    """1 in the middle, 0 at both ends, raised-cosine ramps over band * n."""
    x = np.arange(n) / (n - 1)
    d = np.minimum(x, 1 - x) / band
    return np.where(d >= 1, 1.0, 0.5 - 0.5 * np.cos(np.pi * np.clip(d, 0, 1)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("inp")
    ap.add_argument("out")
    ap.add_argument("--band", type=float, default=0.18)
    a = ap.parse_args()
    im = Image.open(a.inp)
    mode = "RGBA" if im.mode == "RGBA" else "RGB"
    img = np.asarray(im.convert(mode)).astype(np.float64)
    h, w = img.shape[:2]
    shifted = np.roll(img, (h // 2, w // 2), axis=(0, 1))
    m = np.outer(window(h, a.band), window(w, a.band))[..., None]
    out = img * m + shifted * (1 - m)
    edge = lambda t: (np.abs(t[:, 0] - t[:, -1]).mean(), np.abs(t[0] - t[-1]).mean(),
                      np.abs(t[:, 1:] - t[:, :-1]).mean())
    print("edge mismatch L|R %.1f T|B %.1f (neighbours %.1f) -> L|R %.1f T|B %.1f" % (*edge(img), *edge(out)[:2]))
    Image.fromarray(np.clip(out + 0.5, 0, 255).astype(np.uint8), mode).save(a.out)


if __name__ == "__main__":
    main()
