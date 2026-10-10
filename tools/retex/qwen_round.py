"""One round of the prompt / guidance search for a Qwen re-render (docs/retexture.md; GEPA-style: run a population of
candidates, score them, look at the crop sheet, reflect, write the next round's candidates).

Round file (JSON): {"source": "work/retex/title_up.png", "crops": [[x, y, w, h], ...],
                    "candidates": [{"name": "...", "engine": "qwen-image-21", "prompt": "...", "negative": "...",
                                    "cfg": 1.0, "steps": 25, "seed": 7, "strength": 1.0}, ...]}
Each candidate's output goes to work/retex/rounds/<round>/<name>/ (skipped when already there, so a round can be
extended and re-run). Scores, all against the source at the source size:
  struct   SSIM of both images blurred (sigma 2): shapes, values and colour layout kept (sharpening is not punished)
  shift    mean colour shift, max over channels (0..255)
  sharp    fine detail (|Laplacian|) relative to the source: > 1 = crisper than the original
Writes <round>/scores.txt and <round>/sheet.png (crops: source, then candidates in order).

Usage: .venv/Scripts/python.exe tools/retex/qwen_round.py work/retex/rounds/r1.json
"""
import json
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import judge  # noqa: E402
import medialab  # noqa: E402


def scores(src, cand):
    s, c = judge.to_f(src), judge.to_f(cand.resize(src.size, Image.LANCZOS))
    ls, lc = judge.luma(s), judge.luma(c)
    struct = judge.ssim(judge.blur(ls, 2.0), judge.blur(lc, 2.0))
    shift = float(np.abs(s.mean(axis=(0, 1)) - c.mean(axis=(0, 1))).max())
    sharp = judge.detail(cand.resize(src.size, Image.LANCZOS)) / max(judge.detail(src), 1e-6)
    return struct, shift, sharp


def main():
    spec_path = sys.argv[1]
    spec = json.load(open(spec_path))
    rdir = os.path.splitext(spec_path)[0]
    os.makedirs(rdir, exist_ok=True)
    source = os.path.join(ROOT, spec["source"])
    src = judge.load(source)
    rows, outs = [], []
    for c in spec["candidates"]:
        out_dir = os.path.join(rdir, c["name"])
        out = os.path.join(out_dir, "take-01_00001_.png")
        if not os.path.exists(out):
            extra = {"seed": c.get("seed", 7), "image_steps": c.get("steps", 25), "image_cfg": c.get("cfg", 1.0)}
            if c.get("engine", "qwen-image-21") == "qwen-image-21":
                extra["qwen_output_size"] = "source"
                if c.get("strength", 1.0) < 1.0:
                    extra["qwen_edit_strength"] = c["strength"]
            else:
                extra["edit_resolution"] = "source"
                extra["structure_protect"] = "false"
                if c.get("strength", 1.0) < 1.0:
                    extra["edit_strength"] = c["strength"]
            refs = [source] + [os.path.join(ROOT, r) for r in c.get("refs", [])]
            medialab.run(c.get("engine", "qwen-image-21"), refs, out_dir, c["prompt"], c.get("negative", ""), extra)
        cand = judge.load(out)
        st, sh, sp = scores(src, cand)
        rows.append(f"{c['name']:24s} struct {st:.4f}  shift {sh:5.2f}  sharp {sp:5.2f}  cfg {c.get('cfg', 1.0)}")
        outs.append((c["name"], cand))
        print(rows[-1], flush=True)
    with open(os.path.join(rdir, "scores.txt"), "w") as f:
        f.write("\n".join(rows) + "\n")
    crops = spec.get("crops", [])
    if crops:
        tile = 256
        cols = 1 + len(outs)
        img = Image.new("RGB", (cols * tile, len(crops) * tile + 16), "black")
        d = ImageDraw.Draw(img)
        for i, label in enumerate(["source"] + [n for n, _ in outs]):
            d.text((i * tile + 3, 2), label[:40], fill="white")
        for r, (x, y, w, h) in enumerate(crops):
            box = (x, y, x + w, y + h)
            ims = [src.crop(box)] + [cand.resize(src.size, Image.LANCZOS).crop(box) for _, cand in outs]
            for i, im in enumerate(ims):
                img.paste(im.resize((tile, tile), Image.LANCZOS), (i * tile, 16 + r * tile))
        img.save(os.path.join(rdir, "sheet.png"))


if __name__ == "__main__":
    main()
