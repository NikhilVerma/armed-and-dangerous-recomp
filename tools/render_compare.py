"""Render comparison: the original game and the remaster engine from the same camera, scored.

The original's frame and camera come from tools/shot.py (and-frame-N.camera.json, archive/hook/src/testshot.cpp); the
engine renders the same view with `--camera <dump> --no-hud --size 640x480 --snap <png>` (engine/src/camdump.rs).
The original draws its 3D view into the letterbox strip (y 11.25 %, height 77.5 %) with the strip's own aspect
(aspect 0: the viewport's aspect over Renderer_GetAspect, which is 1.0 at 640x480), so the engine renders the strip
itself: `--size 640x372`. HUD areas (and, by default, the player in the middle) are masked.

Metrics (over the unmasked pixels):
  mae        mean absolute difference per channel, 0..255 (lower is better)
  psnr       peak signal-to-noise ratio, dB (higher is better)
  ssim       structural similarity on luminance, 8x8 windows, -1..1 (higher is better)
  regions    mean colour of the top third (sky / far scenery) and the bottom third (ground) in both images

Usage:
  python tools/render_compare.py ORIGINAL.png ENGINE.png [--name NAME] [--keep-player]
  -> work/remaster/render/<name>.png (original | engine | heat map) and <name>.json; one line of metrics printed.
"""
import argparse
import json
import os
import sys

import numpy as np
from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "work", "remaster", "render")
TOP, HEIGHT = 0.1125, 0.775  # letterbox: 0x005bcf24, 0x005bcf28

# masked rectangles in 640x480 screen pixels (the original's HUD at lev_1_m1, hud.md 4)
HUD = [(510, 54, 600, 140), (50, 330, 200, 426), (420, 360, 600, 426)]
PLAYER = (260, 220, 380, 426)


def ssim(a, b, win=8):
    """Mean SSIM of two luminance images over non-overlapping win x win blocks."""
    h, w = (a.shape[0] // win) * win, (a.shape[1] // win) * win
    a = a[:h, :w].reshape(h // win, win, w // win, win)
    b = b[:h, :w].reshape(h // win, win, w // win, win)
    ma, mb = a.mean(axis=(1, 3)), b.mean(axis=(1, 3))
    va, vb = a.var(axis=(1, 3)), b.var(axis=(1, 3))
    cov = ((a - ma[:, None, :, None]) * (b - mb[:, None, :, None])).mean(axis=(1, 3))
    c1, c2 = (0.01 * 255) ** 2, (0.03 * 255) ** 2
    s = ((2 * ma * mb + c1) * (2 * cov + c2)) / ((ma ** 2 + mb ** 2 + c1) * (va + vb + c2))
    return s


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("original")
    ap.add_argument("engine")
    ap.add_argument("--name", default="")
    ap.add_argument("--keep-player", action="store_true", help="do not mask the player in the middle")
    a = ap.parse_args()
    name = a.name or os.path.splitext(os.path.basename(a.engine))[0]
    o = Image.open(a.original).convert("RGB").resize((640, 480))
    top, h = round(TOP * 480), round(HEIGHT * 480)
    o = o.crop((0, top, 640, top + h))
    e = Image.open(a.engine).convert("RGB").resize((640, h), Image.BILINEAR)
    oa, ea = np.asarray(o, dtype=np.float64), np.asarray(e, dtype=np.float64)
    mask = np.ones((h, 640), dtype=bool)
    rects = HUD + ([] if a.keep_player else [PLAYER])
    for x0, y0, x1, y1 in rects:
        mask[max(0, y0 - top):max(0, y1 - top), x0:x1] = False
    d = np.abs(oa - ea)
    mae = float(d[mask].mean())
    mse = float(((oa - ea) ** 2)[mask].mean())
    psnr = float(10 * np.log10(255 ** 2 / max(mse, 1e-9)))
    lum = lambda x: x @ np.array([0.299, 0.587, 0.114])
    s = ssim(lum(oa), lum(ea))
    blocks = mask[: (h // 8) * 8, :640].reshape(h // 8, 8, 80, 8).all(axis=(1, 3))
    ssim_mean = float(s[blocks].mean())
    third = h // 3
    region = lambda x, r: [round(float(v), 1) for v in x[r][mask[r]].mean(axis=0)]
    regions = {
        "top": {"original": region(oa, slice(0, third)), "engine": region(ea, slice(0, third))},
        "bottom": {"original": region(oa, slice(2 * third, h)), "engine": region(ea, slice(2 * third, h))},
    }
    heat = (np.clip(d.mean(axis=2) * 2, 0, 255)).astype(np.uint8)
    heat[~mask] = 0
    hi = Image.fromarray(heat, "L").convert("RGB")
    out = Image.new("RGB", (640 * 3 + 20, h + 24), (20, 20, 20))
    for i, im in enumerate([o, e, hi]):
        out.paste(im, (i * 650, 24))
    dr = ImageDraw.Draw(out)
    dr.text((6, 6), "original", fill=(255, 255, 255))
    dr.text((656, 6), "engine (same camera)", fill=(255, 255, 255))
    dr.text((1306, 6), f"|diff| x2  mae {mae:.1f}  psnr {psnr:.1f}  ssim {ssim_mean:.3f}", fill=(255, 255, 255))
    os.makedirs(OUT, exist_ok=True)
    out.save(os.path.join(OUT, f"{name}.png"))
    result = {"name": name, "original": a.original, "engine": a.engine, "mae": round(mae, 2), "psnr": round(psnr, 2), "ssim": round(ssim_mean, 4), "regions": regions}
    with open(os.path.join(OUT, f"{name}.json"), "w") as f:
        json.dump(result, f, indent=1)
    print(f"{name}: mae {mae:.2f}  psnr {psnr:.2f} dB  ssim {ssim_mean:.4f}  top {regions['top']}  bottom {regions['bottom']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
