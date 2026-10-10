"""Qwen remaster of whole textures with a generic prompt plus an image-specific description (docs/retexture.md).

Spec (JSON): {"generic": "...", "negative": "...", "settings": {"cfg": 2, "steps": 25, "seed": 0, "strength": 1.0},
              "assets": [{"name": "b_q_face", "source": "C:/.../b_q_face_original.png", "width": 2048,
                          "height": 2048, "describe": "The image shows ..."}, ...]}
Prompt sent per asset: generic + " " + describe (the description says what the low-resolution pixels are: subject,
materials, colours, transparency). Output size: qwen_output_size=custom at width x height (single image up to
2048x2048; transparency is kept). Results: <out>/<name>/take-01_00001_.png (skipped when already there); with
--compare DIR also an "original | previous | new" sheet per asset (previous from --previous DIR/<name>.png).

Usage: .venv/Scripts/python.exe tools/retex/qwen_assets.py SPEC.json OUT_DIR [--compare DIR] [--previous DIR]
"""
import argparse
import json
import os
import sys

from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import medialab  # noqa: E402


def on_grey(im):
    bg = Image.new("RGBA", im.size, (70, 70, 70, 255))
    bg.alpha_composite(im.convert("RGBA"))
    return bg.convert("RGB")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("spec")
    ap.add_argument("out")
    ap.add_argument("--compare", default="")
    ap.add_argument("--previous", default="")
    a = ap.parse_args()
    spec = json.load(open(a.spec, encoding="utf-8"))
    st = spec.get("settings", {})
    for asset in spec["assets"]:
        name = asset["name"]
        out_dir = os.path.join(a.out, name)
        done = os.path.join(out_dir, "take-01_00001_.png")
        prompt = (spec["generic"] + " " + asset.get("describe", "")).strip()
        if not os.path.exists(done):
            medialab.run("qwen-image-21", [asset["source"]], out_dir, prompt, spec.get("negative", ""),
                         {"seed": st.get("seed", 0), "image_steps": st.get("steps", 25), "image_cfg": st.get("cfg", 2),
                          "qwen_edit_strength": st.get("strength", 1.0), "qwen_output_size": "custom",
                          "custom_width": asset["width"], "custom_height": asset["height"]})
        print(f"{name}: {done}", flush=True)
        if a.compare:
            os.makedirs(a.compare, exist_ok=True)
            h = 900
            cols = [("original", Image.open(asset["source"]), Image.NEAREST)]
            prev = os.path.join(a.previous, name + ".png") if a.previous else ""
            if prev and os.path.exists(prev):
                cols.append(("generic prompt", Image.open(prev), Image.LANCZOS))
            cols.append(("generic + description", Image.open(done), Image.LANCZOS))
            ims = [(t, im.resize((int(im.width * h / im.height), h), f)) for t, im, f in cols]
            sheet = Image.new("RGB", (sum(im.width for _, im in ims) + 10 * (len(ims) - 1), h + 22), "black")
            d = ImageDraw.Draw(sheet)
            x = 0
            for t, im in ims:
                sheet.paste(on_grey(im), (x, 22))
                d.text((x + 6, 5), t, fill="white")
                x += im.width + 10
            sheet.save(os.path.join(a.compare, f"compare_{name}.png"))


if __name__ == "__main__":
    main()
