"""Scores upscaled / regenerated textures against the original (the retexture rule: shrunk back to the original size,
a candidate must look like the original; at full size it should carry more real detail than plain resampling).

For each candidate:
  consistency  the candidate box-downsampled to the original size vs the original:
               PSNR (dB), SSIM (luma, 8x8 Gaussian windows), mean colour shift (0..255 per channel)
  detail       high-frequency energy (mean |Laplacian| of luma) relative to a bicubic upscale of the original
               (1.0 = no new detail; noise also raises it, so read it with the crops)
Also writes a crop sheet (original nearest-upscaled, bicubic, each candidate) for looking at.

Usage: .venv/Scripts/python.exe tools/retex/judge.py ORIGINAL CANDIDATE [CANDIDATE...] [--crops x0,y0,w,h;...]
       [--sheet out.png]
Crop boxes are in original pixels.
"""
import argparse
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFilter


def load(path):
    return Image.open(path).convert("RGB")


def to_f(im):
    return np.asarray(im).astype(np.float64)


def luma(a):
    return a[..., 0] * 0.299 + a[..., 1] * 0.587 + a[..., 2] * 0.114


def blur(a, sigma=1.5):
    r = int(3 * sigma + 0.5)
    k = np.exp(-0.5 * (np.arange(-r, r + 1) / sigma) ** 2)
    k /= k.sum()
    p = np.pad(a, r, mode="reflect")
    p = sum(k[i] * p[:, i:i + a.shape[1]] for i in range(2 * r + 1))  # rows
    return sum(k[i] * p[i:i + a.shape[0], :] for i in range(2 * r + 1))  # columns


def ssim(x, y):
    c1, c2 = (0.01 * 255) ** 2, (0.03 * 255) ** 2
    mx, my = blur(x), blur(y)
    sxx = blur(x * x) - mx * mx
    syy = blur(y * y) - my * my
    sxy = blur(x * y) - mx * my
    m = ((2 * mx * my + c1) * (2 * sxy + c2)) / ((mx * mx + my * my + c1) * (sxx + syy + c2))
    return float(m.mean())


def psnr(x, y):
    mse = float(((x - y) ** 2).mean())
    return 99.0 if mse == 0 else 10 * np.log10(255.0 ** 2 / mse)


def detail(im):
    lap = np.asarray(im.convert("L").filter(ImageFilter.FIND_EDGES)).astype(np.float64)
    return float(np.abs(lap).mean())


def score(orig, cand):
    small = cand.resize(orig.size, Image.BOX)
    o, s = to_f(orig), to_f(small)
    bic = orig.resize(cand.size, Image.BICUBIC)
    return {
        "size": f"{cand.size[0]}x{cand.size[1]}",
        "psnr": psnr(o, s),
        "ssim": ssim(luma(o), luma(s)),
        "shift": float(np.abs(o.mean(axis=(0, 1)) - s.mean(axis=(0, 1))).max()),
        "detail": detail(cand) / max(detail(bic), 1e-6),
    }


def sheet(orig, cands, names, crops, out, tile=320):
    cols = 2 + len(cands)
    img = Image.new("RGB", (cols * tile, len(crops) * tile + 18), "black")
    d = ImageDraw.Draw(img)
    for c, label in enumerate(["original (nearest)", "bicubic"] + names):
        d.text((c * tile + 4, 3), label[:44], fill="white")
    for r, (x, y, w, h) in enumerate(crops):
        box = (x, y, x + w, y + h)
        tiles = [orig.crop(box).resize((tile, tile), Image.NEAREST),
                 orig.crop(box).resize((tile, tile), Image.BICUBIC)]
        for cand in cands:
            k = cand.size[0] / orig.size[0]
            tiles.append(cand.crop(tuple(int(v * k) for v in box)).resize((tile, tile), Image.LANCZOS))
        for c, t in enumerate(tiles):
            img.paste(t, (c * tile, 18 + r * tile))
    img.save(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("original")
    ap.add_argument("candidates", nargs="+")
    ap.add_argument("--crops", default="")
    ap.add_argument("--sheet", default="")
    a = ap.parse_args()
    orig = load(a.original)
    cands = [load(p) for p in a.candidates]
    names = [os.path.basename(os.path.dirname(p)) or os.path.basename(p) for p in a.candidates]
    print(f"{'candidate':34s} {'size':>10s} {'PSNR':>6s} {'SSIM':>6s} {'shift':>6s} {'detail':>7s}")
    for n, c in zip(names, cands):
        s = score(orig, c)
        print(f"{n[:34]:34s} {s['size']:>10s} {s['psnr']:6.2f} {s['ssim']:6.4f} {s['shift']:6.2f} {s['detail']:7.2f}")
    if a.sheet and a.crops:
        crops = [tuple(int(v) for v in c.split(",")) for c in a.crops.split(";")]
        sheet(orig, cands, names, crops, a.sheet)


if __name__ == "__main__":
    main()
