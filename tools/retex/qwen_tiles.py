"""Tiled Qwen upscale: each level doubles the image with Qwen re-rendering the detail, so no SR network invents texture
(docs/retexture.md: Nomos2 amplified Qwen's faint latent grid and left dark halos).

One level: Lanczos 2x of the input; overlapping square tiles (Qwen source mode is limited to about 1.5 MP); each tile
re-rendered by Qwen Image 2.1 at partial strength (it starts from the tile, so content and seams stay put); each result
locked to its input tile's broad colour (lowfreq); tiles blended with raised-cosine weights over the overlaps.

Usage: .venv/Scripts/python.exe tools/retex/qwen_tiles.py INPUT OUT_PNG [--tile 1024] [--overlap 256]
           [--strength 0.5] [--cfg 1.0] [--steps 30] [--seed 7] [--prompt-file P]
       --grid 4x4 --scale 1: INPUT is already the output size (e.g. a HAT-L 4x); a fixed grid of tiles sized to
       overlap by --overlap pixels horizontally and half that vertically, rounded to the 32-pixel latent grid.
"""
import argparse
import os
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import judge  # noqa: E402
import medialab  # noqa: E402

# Reconstruction, not sharpening (the player, 2026-10-10: "it should encourage mostly reconstructing details"). Measured
# on the cannon tile: from a Lanczos 4x of the original at strength 0.95 this rebuilds casting marks, bores and wood
# grain with edge contrast 1.20x the original's at source scale; "sharper / crisp" prompts and sharpened bases
# (HAT-L, an earlier Qwen render) gave 1.55-1.83x: oversharpened, and partial strength keeps the base's grid.
PROMPT = ("This tile is a blurry, low-resolution enlargement of a hand-painted 2003 video game title illustration. "
          "Reconstruct the fine detail the original high-resolution painting would have had: the real surface of the "
          "cast-iron cannons and gun barrels with casting marks, subtle wear and clean bores, the screws, bolts and "
          "rivets, the wood grain of the stocks, the cracks and chips of the painted shield, and the painter's brush "
          "and airbrush texture. Keep every shape, edge position, object and colour exactly where and as it is, and "
          "keep the soft airbrushed painting style with soft natural edges. Only restore lost detail; do not sharpen, "
          "outline or add anything new.")
NEGATIVE = ("oversharpened, halos, dark outlines, bright rims, crunchy, HDR, high contrast edges, woven texture, grid "
            "pattern, crosshatch, noise, grain, jpeg artifacts, blur, new objects, changed shapes, changed colours, text, "
            "watermark")


def positions(size, tile, overlap):
    if size <= tile:
        return [0]
    step = tile - overlap
    n = int(np.ceil((size - tile) / step)) + 1
    return [round(i * (size - tile) / (n - 1)) for i in range(n)]


def weight(tile, overlap):
    w = np.ones(tile)
    ramp = 0.5 - 0.5 * np.cos(np.linspace(0, np.pi, overlap))
    w[:overlap] = ramp
    w[-overlap:] = ramp[::-1]
    return w


def seam_join(a, b, axis, margin=64, feather=12):
    """Joins two equal-size overlap strips a (left / top) and b (right / bottom) along the path where they differ
    least (image quilting): dynamic programming over the strip without its outer margins, so neither tile's border
    (where Qwen is least reliable) reaches the result; then a narrow feather around the cut. Returns the joined strip.
    axis 1: a vertical seam (strips side by side); axis 0: a horizontal seam."""
    if axis == 0:
        return seam_join(a.transpose(1, 0, 2), b.transpose(1, 0, 2), 1, margin, feather).transpose(1, 0, 2)
    h, w = a.shape[:2]
    lo, hi = min(margin, w // 4), max(w - margin, 3 * w // 4)
    cost = np.abs(a - b).sum(axis=2)
    cost = sum(judge.blur(cost, 1.5) for _ in range(1))[:, lo:hi]
    acc = cost.copy()
    for y in range(1, h):
        prev = acc[y - 1]
        left = np.r_[np.inf, prev[:-1]]
        right = np.r_[prev[1:], np.inf]
        acc[y] += np.minimum(np.minimum(left, prev), right)
    path = np.zeros(h, dtype=int)
    path[-1] = int(np.argmin(acc[-1]))
    for y in range(h - 2, -1, -1):
        x = path[y + 1]
        cand = [(acc[y, x], x)]
        if x > 0:
            cand.append((acc[y, x - 1], x - 1))
        if x < acc.shape[1] - 1:
            cand.append((acc[y, x + 1], x + 1))
        path[y] = min(cand)[1]
    path += lo
    xs = np.arange(w)[None, :]
    mask = (xs >= path[:, None]).astype(np.float64)  # 0: a, 1: b
    mask = np.clip(judge.blur(mask, feather / 2.0), 0, 1)
    return a * (1 - mask[..., None]) + b * mask[..., None]


def lock(cand, base, sigma):
    return np.stack([cand[..., i] - judge.blur(cand[..., i], sigma) + judge.blur(base[..., i], sigma)
                     for i in range(3)], axis=-1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("input")
    ap.add_argument("out")
    ap.add_argument("--tile", type=int, default=1024)
    ap.add_argument("--overlap", type=int, default=256)
    ap.add_argument("--strength", type=float, default=0.5)
    ap.add_argument("--cfg", type=float, default=1.0)
    ap.add_argument("--steps", type=int, default=30)
    ap.add_argument("--seed", type=int, default=7)
    ap.add_argument("--sigma", type=float, default=3.0, help="colour lock radius (pixels of the output)")
    ap.add_argument("--prompt-file", default="")
    ap.add_argument("--negative-file", default="")
    ap.add_argument("--grid", default="", help="COLSxROWS: a fixed tile grid instead of square tiles")
    ap.add_argument("--scale", type=int, default=2, help="resize factor before tiling (1: INPUT is the output size)")
    ap.add_argument("--blend", default="seam", choices=["seam", "feather"],
                    help="seam: cut along the least-difference path in each overlap (no ghosting); feather: crossfade")
    a = ap.parse_args()
    prompt = open(a.prompt_file).read().strip() if a.prompt_file else PROMPT
    negative = open(a.negative_file).read().strip() if a.negative_file else NEGATIVE
    src = Image.open(a.input).convert("RGB")
    base_im = src.resize((src.width * a.scale, src.height * a.scale), Image.LANCZOS) if a.scale != 1 else src
    base = np.asarray(base_im).astype(np.float64)
    H, W = base.shape[:2]
    work = os.path.splitext(a.out)[0] + "_tiles"
    os.makedirs(work, exist_ok=True)
    acc = np.zeros_like(base)
    tiles = {}
    wsum = np.zeros((H, W, 1))
    if a.grid:
        cols, rows = (int(v) for v in a.grid.lower().split("x"))
        ox, oy = a.overlap, a.overlap
        tw = -(-((W + (cols - 1) * ox) // cols) // 32) * 32
        th = -(-((H + (rows - 1) * oy) // rows) // 32) * 32
        xs = [round(i * (W - tw) / max(cols - 1, 1)) for i in range(cols)]
        ys = [round(i * (H - th) / max(rows - 1, 1)) for i in range(rows)]
        ovx, ovy = tw - (xs[1] - xs[0]) if cols > 1 else 0, th - (ys[1] - ys[0]) if rows > 1 else 0
    else:
        t = a.tile
        th, tw = min(t, H), min(t, W)
        ys, xs = positions(H, t, a.overlap), positions(W, t, a.overlap)
        ovx = ovy = a.overlap
    print(f"{W}x{H}: {len(xs)} x {len(ys)} tiles of {tw}x{th}, overlap {ovx}/{ovy}", flush=True)
    for y in ys:
        for x in xs:
            name = f"t_{x}_{y}"
            tile_path = os.path.join(work, name + ".png")
            base_im.crop((x, y, x + tw, y + th)).save(tile_path)
            out_dir = os.path.join(work, name)
            done = os.path.join(out_dir, "take-01_00001_.png")
            if not os.path.exists(done):
                medialab.run("qwen-image-21", [tile_path], out_dir, prompt, negative,
                             {"seed": a.seed, "image_steps": a.steps, "image_cfg": a.cfg,
                              "qwen_output_size": "source", "qwen_edit_strength": a.strength})
            r = Image.open(done).convert("RGB")
            if r.size != (tw, th):
                r = r.resize((tw, th), Image.LANCZOS)
            rt = lock(np.asarray(r).astype(np.float64), base[y:y + th, x:x + tw], a.sigma)
            wy = weight(th, ovy) if len(ys) > 1 else np.ones(th)
            wx = weight(tw, ovx) if len(xs) > 1 else np.ones(tw)
            # Image borders get full weight (no neighbour to blend with).
            if y == ys[0] and ovy:
                wy[:ovy] = 1
            if y == ys[-1] and ovy:
                wy[-ovy:] = 1
            if x == xs[0] and ovx:
                wx[:ovx] = 1
            if x == xs[-1] and ovx:
                wx[-ovx:] = 1
            w = np.outer(wy, wx)[..., None]
            acc[y:y + th, x:x + tw] += rt * w
            wsum[y:y + th, x:x + tw] += w
            tiles[(x, y)] = rt
            print(f"  tile {name} done", flush=True)
    out = acc / np.maximum(wsum, 1e-6)
    if a.blend == "seam":
        # Rows: tiles left to right, each joined to the canvas along a vertical least-difference seam; then the row
        # strips top to bottom along horizontal seams.
        strips = []
        for y in ys:
            row = tiles[(xs[0], y)]
            for x in xs[1:]:
                end = row.shape[1]
                ov = end - x
                joined = seam_join(row[:, x:end], tiles[(x, y)][:, :ov], 1)
                row = np.concatenate([row[:, :x], joined, tiles[(x, y)][:, ov:]], axis=1)
            strips.append(row)
        out = strips[0]
        for y, strip in zip(ys[1:], strips[1:]):
            end = out.shape[0]
            ov = end - y
            joined = seam_join(out[y:end], strip[:ov], 0)
            out = np.concatenate([out[:y], joined, strip[ov:]], axis=0)
    Image.fromarray(np.clip(out + 0.5, 0, 255).astype(np.uint8)).save(a.out)
    print(a.out)


if __name__ == "__main__":
    main()
