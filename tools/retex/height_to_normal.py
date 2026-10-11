"""Builds a tangent-space normal map from a greyscale height map (white = high), for terrain normal maps redesigned
rather than upscaled (N_B_T_snow, 2026-10-11: the original's broad ripples read as blobs, not snow).

Convention, measured on the original N_B_T_snow (upright image, rows downwards): the slopes -r/b and -g/b form a
curl-free field with both signs as stored (curl 0.040 vs 0.145 with green flipped), so
    normal = normalize(-dh/dx, -dh/drow, 1 / strength),  encoded as (n + 1) * 127.5.
Broad height variation is removed first (--flatten, wavelengths above size / that), so the surface has no large
undulations; --strength scales the bumps. Output is upright; --install also writes the stored (flipped) override.

Usage: .venv/Scripts/python.exe tools/retex/height_to_normal.py HEIGHT.png OUT.png [--strength 4] [--flatten 16]
       [--shaded 135]
       [--size 2048] [--install N_B_T_snow]
"""
import argparse
import os
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import batch  # noqa: E402
import judge  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("height")
    ap.add_argument("out")
    ap.add_argument("--strength", type=float, default=4.0)
    ap.add_argument("--flatten", type=float, default=16.0)
    ap.add_argument("--size", type=int, default=2048)
    ap.add_argument("--install", default="")
    ap.add_argument("--shaded", type=float, default=None,
                    help="input is lit relief (light from this angle, degrees, 0 = from +x, 135 = from upper left): "
                         "shading is integrated back to height")
    a = ap.parse_args()
    h = np.asarray(Image.open(a.height).convert("L").resize((a.size, a.size), Image.LANCZOS)).astype(np.float64) / 255
    if a.shaded is not None:
        # Lit relief (Qwen draws snow "height maps" as lit snow): brightness ~ slope towards the light. Integrate that
        # directional slope in the Fourier domain (h = S / (i 2 pi f.l), small frequencies damped), which tiles.
        s = h - h.mean()
        f = np.fft.fftfreq(a.size)
        fx, fy = np.meshgrid(f, f)
        ang = np.radians(a.shaded)
        lx, ly = np.cos(ang), -np.sin(ang)  # image rows run downwards
        d = 2j * np.pi * (fx * lx + fy * ly)
        eps = 2 * np.pi / a.size * 2
        h = np.fft.ifft2(np.fft.fft2(s) * np.conj(d) / (np.abs(d) ** 2 + eps ** 2)).real
        h = (h - h.min()) / (h.max() - h.min())
    h = h - judge.blur(h, a.size / a.flatten)  # no broad undulations
    h = batch.tileable(np.repeat(h[..., None], 3, axis=2))[..., 0]
    # Central differences with wrap-around (the texture tiles), in height units per texel x size, so the strength
    # does not depend on the resolution.
    dx = (np.roll(h, -1, axis=1) - np.roll(h, 1, axis=1)) * 0.5 * a.size / 256
    dy = (np.roll(h, -1, axis=0) - np.roll(h, 1, axis=0)) * 0.5 * a.size / 256
    n = np.stack([-dx * a.strength, -dy * a.strength, np.ones_like(h)], axis=-1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    rgb = np.clip((n + 1) * 127.5 + 0.5, 0, 255).astype(np.uint8)
    im = Image.fromarray(rgb, "RGB").convert("RGBA")
    im.save(a.out)
    if a.install:
        for d in batch.OVERRIDES:
            im.transpose(Image.FLIP_TOP_BOTTOM).save(os.path.join(d, a.install + ".png"))
    print(f"{a.out}: mean z {n[..., 2].mean():.3f}, slope p95 {np.percentile(np.hypot(dx, dy) * a.strength, 95):.2f}")


if __name__ == "__main__":
    main()
