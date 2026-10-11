"""Edit-strength test (the player, 2026-10-11: "it's massively changing some images ... maybe try 0.9 edit strength"):
for each texture and strength, Qwen re-renders a Lanczos enlargement of the original at the target size (source
mode, so the composition is the input's), then the result is scored against the original: structure similarity of
luminance after shrinking both to the original size (drift.py-style: 1 = same picture).

Usage: .venv/Scripts/python.exe tools/retex/strength_test.py NAME [NAME ...] [--strengths 0.9,0.7,0.5]
"""
import argparse
import json
import os
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import batch  # noqa: E402
import judge  # noqa: E402
import medialab  # noqa: E402

RECON = ("This is a blurry, low-resolution enlargement of a video game texture. Reconstruct the fine detail the "
         "original high-resolution texture would have had. Keep every shape, object, crack, stone, edge position, "
         "layout and colour exactly where and as it is; only restore lost detail. ")


def structure(a, b):
    """SSIM of luminance, both at a's size, lightly blurred (sub-pixel noise does not count)."""
    w = np.array([0.2126, 0.7152, 0.0722])
    ya = judge.blur(np.asarray(a.convert("RGB")).astype(np.float64) @ w, 1.0)
    yb = judge.blur(np.asarray(b.convert("RGB").resize(a.size, Image.LANCZOS)).astype(np.float64) @ w, 1.0)
    mu_a, mu_b = judge.blur(ya, 3), judge.blur(yb, 3)
    va = judge.blur(ya * ya, 3) - mu_a ** 2
    vb = judge.blur(yb * yb, 3) - mu_b ** 2
    cov = judge.blur(ya * yb, 3) - mu_a * mu_b
    c1, c2 = (0.01 * 255) ** 2, (0.03 * 255) ** 2
    s = ((2 * mu_a * mu_b + c1) * (2 * cov + c2)) / ((mu_a ** 2 + mu_b ** 2 + c1) * (va + vb + c2))
    return float(s.mean())


def submit(name, strength, plan):
    p = plan[name]
    d = json.load(open(batch.path("desc", name + ".json")))
    src = Image.open(batch.path("src", name + ".png")).convert("RGBA")
    work = batch.path("strength", name)
    os.makedirs(work, exist_ok=True)
    base = os.path.join(work, "base.png")
    src.resize((p["tw"], p["th"]), Image.LANCZOS).save(base)
    prompt = RECON + d.get("describe", "")
    job = medialab.submit("qwen-image-21", [base], prompt, batch.NEGATIVE,
                          {"seed": 0, "image_steps": 25, "image_cfg": 2, "qwen_edit_strength": strength,
                           "qwen_output_size": "source"})
    return job["id"], os.path.join(work, f"s{strength}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("names", nargs="+")
    ap.add_argument("--strengths", default="0.9,0.7,0.5")
    a = ap.parse_args()
    plan = batch.load_plan()
    strengths = [float(s) for s in a.strengths.split(",")]
    jobs = [(n, s, *submit(n, s, plan)) for n in a.names for s in strengths]
    for n, s, jid, out in jobs:
        job = medialab.wait(jid)
        path = medialab.download(job, out)[0]
        src = Image.open(batch.path("src", n + ".png"))
        old = batch.path("out", n, "take-01_00001_.png")
        old_s = structure(src, Image.open(old)) if os.path.exists(old) else float("nan")
        print(f"{n} strength {s}: structure {structure(src, Image.open(path)):.3f} (strength 1.0 batch: {old_s:.3f})",
              flush=True)


if __name__ == "__main__":
    main()
