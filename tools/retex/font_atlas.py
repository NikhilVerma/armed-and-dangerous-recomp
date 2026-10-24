"""Redraws the game's bitmap UI font atlas (mainfontid, 256x512, metrics in *_big13_font.big; docs/formats/big-font.md)
at K x resolution from a vector font, glyph by glyph into the same normalised cells, so the game's layout (pixel widths
and advances in the font file) is unchanged and only the texture gets sharper.

Each glyph: its cell = uv * (texture size - 1) (the game's convention, 0x005078c0), scaled by K. One font size for all
glyphs, chosen so the vector "H" has the original "H" ink height x K; every glyph sits on the baseline measured from the
original "H" and starts at the original ink's left edge in its cell. White RGB, alpha = coverage (as the original).

Usage: .venv/Scripts/python.exe tools/retex/font_atlas.py ORIGINAL.png FONT.json OUT.png [--scale 4]
       [--ttf C:/Windows/Fonts/arial.ttf]
ORIGINAL.png is the upright atlas (the .dds dump flipped); OUT.png is upright too.
"""
import argparse
import json

import numpy as np
from PIL import Image, ImageDraw, ImageFont


def ink_bbox(alpha, box, thr=60):
    x0, y0, x1, y1 = box
    a = alpha[y0:y1, x0:x1]
    ys, xs = np.where(a > thr)
    if not len(xs):
        return None
    return x0 + xs.min(), y0 + ys.min(), x0 + xs.max() + 1, y0 + ys.max() + 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("original")
    ap.add_argument("metrics")
    ap.add_argument("out")
    ap.add_argument("--scale", type=int, default=4)
    ap.add_argument("--ttf", default="C:/Windows/Fonts/arial.ttf")
    a = ap.parse_args()
    K = a.scale
    orig = Image.open(a.original).convert("RGBA")
    W, H = orig.size
    alpha = np.asarray(orig)[..., 3]
    m = json.load(open(a.metrics, encoding="utf-8"))
    glyphs = m["glyphs"]

    def cell(g):
        u0, v0, u1, v1 = g["uv"]
        return (int(round(u0 * (W - 1))), int(round(v0 * (H - 1))), int(round(u1 * (W - 1))) + 1,
                int(round(v1 * (H - 1))) + 1)

    by_char = {g["code"]: g for g in glyphs}
    hb = ink_bbox(alpha, cell(by_char[ord("H")]))
    hcell = cell(by_char[ord("H")])
    cap = (hb[3] - hb[1]) * K               # target cap height in output pixels
    base_off = (hb[3] - hcell[1]) * K       # baseline below the cell top, output pixels
    # Font size whose "H" ink height equals the target cap height.
    size = cap
    for _ in range(30):
        f = ImageFont.truetype(a.ttf, size)
        bb = f.getbbox("H")
        h = bb[3] - bb[1]
        if abs(h - cap) <= 0:
            break
        size = max(4, int(round(size * cap / max(h, 1))))
    font = ImageFont.truetype(a.ttf, size)
    ascent, _ = font.getmetrics()
    out = Image.new("L", (W * K, H * K), 0)
    d = ImageDraw.Draw(out)
    drawn = 0
    for g in glyphs:
        ch = chr(g["code"]).encode("latin1").decode("cp1252", errors="replace")
        if ch.isspace() or g["code"] < 0x21:
            continue
        c = cell(g)
        ib = ink_bbox(alpha, c)
        if not ib:
            continue
        # Render large, crop to the ink, then fit it exactly into the original glyph's ink box (x K): same extents as
        # the original, so nothing is cut by the game's tight glyph rectangle and the spacing is unchanged.
        big = Image.new("L", (size * 3, size * 3), 0)
        ImageDraw.Draw(big).text((size, size // 2), ch, font=font, fill=255)
        bb = big.getbbox()
        if not bb:
            continue
        ink = big.crop(bb)
        tw, th = (ib[2] - ib[0]) * K, (ib[3] - ib[1]) * K
        ink = ink.resize((max(1, tw), max(1, th)), Image.LANCZOS)
        out.paste(ink, (ib[0] * K, ib[1] * K), ink)
        drawn += 1
    rgba = Image.merge("RGBA", (Image.new("L", out.size, 255),) * 3 + (out,))
    rgba.save(a.out)
    print(f"{drawn} glyphs, font size {size}px, cap {cap}px, atlas {out.size[0]}x{out.size[1]}")


if __name__ == "__main__":
    main()
