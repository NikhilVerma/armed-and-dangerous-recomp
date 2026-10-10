"""Iterative back-projection: makes an upscaled texture shrink back to the original (the retexture rule) while
keeping the detail the upscaler added. Each step adds the bicubic-upsampled residual between the original and the
candidate box-downsampled to the original size:  hr += up(orig - down(hr)).

Usage: .venv/Scripts/python.exe tools/retex/backproject.py ORIGINAL CANDIDATE OUT [--steps 8]
"""
import argparse

import numpy as np
from PIL import Image


def down(a, size):
    return np.asarray(Image.fromarray(np.clip(a, 0, 255).astype(np.uint8)).resize(size, Image.BOX)).astype(np.float64)


def up(a, size):
    # Float-precise bicubic per channel (mode "F"), so the residual is not quantised to 8 bits.
    chans = [np.asarray(Image.fromarray(a[..., c].astype(np.float32), mode="F").resize(size, Image.BICUBIC))
             for c in range(a.shape[2])]
    return np.stack(chans, axis=-1).astype(np.float64)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("original")
    ap.add_argument("candidate")
    ap.add_argument("out")
    ap.add_argument("--steps", type=int, default=8)
    a = ap.parse_args()
    orig_im = Image.open(a.original)
    mode = "RGBA" if orig_im.mode == "RGBA" else "RGB"
    orig = np.asarray(orig_im.convert(mode)).astype(np.float64)
    hr = np.asarray(Image.open(a.candidate).convert(mode)).astype(np.float64)
    big = (hr.shape[1], hr.shape[0])
    small = (orig.shape[1], orig.shape[0])
    for i in range(a.steps):
        res = orig - down(hr, small)
        print(f"step {i}: mean |residual| {np.abs(res).mean():.3f}")
        hr = hr + up(res, big)
    Image.fromarray(np.clip(hr + 0.5, 0, 255).astype(np.uint8), mode).save(a.out)


if __name__ == "__main__":
    main()
