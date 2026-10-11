"""Procedural snow height map (white = high), seamless by construction (noise shaped in the Fourier domain is
periodic), for the redesigned terrain normal map (height_to_normal.py). Layers, each with its own weight:
  grain    fine powder grain, a few texels across
  crust    granular crust, tens of texels, slightly sharpened (|noise|) so it reads as packed snow
  ridges   soft wind ridges (sastrugi): noise stretched along one direction
  pits     sparse small round dents
No broad undulation: everything above size / 12 is filtered out.

Usage: .venv/Scripts/python.exe tools/retex/snow_height.py OUT.png [--size 2048] [--seed 1]
       [--grain 0.25] [--crust 0.5] [--ridges 0.35] [--pits 0.3] [--angle 25]
"""
import argparse

import numpy as np
from PIL import Image


def band_noise(rng, n, lo, hi, stretch=1.0, angle=0.0):
    """White noise kept between wavelengths hi and lo texels (lo < hi), optionally stretched along `angle` degrees."""
    f = np.fft.fftfreq(n)
    fx, fy = np.meshgrid(f, f)
    a = np.radians(angle)
    u = fx * np.cos(a) + fy * np.sin(a)
    v = -fx * np.sin(a) + fy * np.cos(a)
    r = np.hypot(u * stretch, v)  # stretch > 1: features long along the direction
    lam = 1.0 / np.maximum(r, 1e-9)
    mask = np.exp(-0.5 * (np.log(lam / np.sqrt(lo * hi)) / (0.5 * np.log(hi / lo) + 1e-6)) ** 2)
    mask[0, 0] = 0
    z = np.fft.ifft2(np.fft.fft2(rng.standard_normal((n, n))) * mask).real
    return z / (z.std() + 1e-9)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--size", type=int, default=2048)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--grain", type=float, default=0.25)
    ap.add_argument("--crust", type=float, default=0.5)
    ap.add_argument("--ridges", type=float, default=0.35)
    ap.add_argument("--pits", type=float, default=0.3)
    ap.add_argument("--angle", type=float, default=25.0)
    a = ap.parse_args()
    n = a.size
    k = n / 2048  # wavelengths below are for 2048
    rng = np.random.default_rng(a.seed)
    grain = band_noise(rng, n, 2 * k, 6 * k)
    crust = band_noise(rng, n, 10 * k, 60 * k)
    crust = 1.0 - np.abs(crust)  # rounded lumps with creases between
    crust = (crust - crust.mean()) / crust.std()
    ridges = band_noise(rng, n, 40 * k, 170 * k, stretch=4.0, angle=a.angle)
    h = a.grain * grain + a.crust * crust + a.ridges * ridges
    # Pits: sparse small Gaussian dents, placed with wrap-around.
    yy, xx = np.mgrid[0:n, 0:n]
    pits = np.zeros((n, n))
    for _ in range(int(60 * (n / 2048) ** 2)):
        cx, cy = rng.uniform(0, n, 2)
        r = rng.uniform(6, 18) * k
        dx = (xx - cx + n / 2) % n - n / 2
        dy = (yy - cy + n / 2) % n - n / 2
        pits -= np.exp(-(dx * dx + dy * dy) / (2 * r * r))
    h += a.pits * pits * 2.0
    h = (h - h.min()) / (h.max() - h.min())
    Image.fromarray((h * 255 + 0.5).astype(np.uint8), "L").save(a.out)
    print(a.out)


if __name__ == "__main__":
    main()
