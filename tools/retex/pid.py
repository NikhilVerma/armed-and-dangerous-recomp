"""PiD texture upscale (Media Lab engine `pid`, NVIDIA PiD; tried 2026-11-02 on the sweep's eight samples).

Per texture, from batch.prepare's upright source (work/retex/batch/src/<name>.png) and its committed description
(tools/retex/desc/<name>.json):
  1. clean: SwinIR-L realSR 2x (upscale-faithful), Lanczos back to the original size. Fed the raw original, PiD
     sharpens the DXT compression blocks into a crosshatch of scratches (wood_crate's FRAGILE panel, k_pub_sign);
     the cleaned copy keeps the picture without the blocks.
  2. PiD at the plan's scale (tw / w, at most 4), 4 steps, degrade 0, tone transfer 4, prompt "Game texture, highly
     detailed." plus the description. A non-square source is first padded to a square by repeating its edge pixels
     and the result cropped back: fed the 256x64 k_player_rifle as is, PiD drew vertical streaks across it.
     Sources under MIN_SIDE px are skipped (16 px fx_smoke_brown came back as invented blocks).
  3. post: the original alpha (resized) and, when the original wraps, batch.tileable.
Results: work/retex/pid_batch/<name>.png (upright, not installed); sheet: work/retex/pid_batch/sheet_<k>.jpg
(original | result).

Usage: .venv/Scripts/python.exe tools/retex/pid.py NAME [NAME ...] [--sheet]
"""
import argparse
import json
import os
import sys

import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import batch  # noqa: E402
import medialab  # noqa: E402

OUT = os.path.join(batch.ROOT, "work", "retex", "pid_batch")
CLEAN_MODEL = "003_realSR_BSRGAN_DFOWMFC_s64w8_SwinIR-L_x4_PSNR.pth"
PID = {"pid_steps": 4, "pid_degrade": 0, "tone_transfer": 4, "seed": 1337}
MIN_SIDE = 32


def clean(name, src):
    """The SwinIR-cleaned source at its own size, padded to a square (edge pixels repeated)."""
    path = os.path.join(OUT, "clean", name + ".png")
    if not os.path.exists(path):
        rgb = os.path.join(OUT, "clean", name + "_rgb.png")
        os.makedirs(os.path.dirname(rgb), exist_ok=True)
        src.convert("RGB").save(rgb)
        up = medialab.run("upscale-faithful", [rgb], os.path.join(OUT, "clean", name),
                          extra={"upscale_model": CLEAN_MODEL, "upscale_factor": 2})[0]
        a = np.asarray(Image.open(up).convert("RGB").resize(src.size, Image.LANCZOS))
        side = max(src.size)
        a = np.pad(a, ((0, side - src.height), (0, side - src.width), (0, 0)), mode="edge")
        Image.fromarray(a).save(path)
    return path


def upscale(name, plan):
    dst = os.path.join(OUT, name + ".png")
    if os.path.exists(dst):
        return dst
    src = Image.open(batch.path("src", name + ".png")).convert("RGBA")
    if min(src.size) < MIN_SIDE:
        print(f"{name}: {src.width}x{src.height}, under {MIN_SIDE} px: skipped")
        return None
    p = plan[name]
    desc = json.load(open(batch.path("desc", name + ".json")))
    prompt = "Game texture, highly detailed. " + desc.get("describe", "")
    out = medialab.run("pid", [clean(name, src)], os.path.join(OUT, "jobs", name), prompt,
                       extra={**PID, "pid_scale": min(4, p["tw"] / p["w"])})[0]  # PiD's maximum is 4x
    im = Image.open(out).convert("RGB")
    k = im.width / max(src.size)  # crop the square padding off again
    im = im.crop((0, 0, round(src.width * k), round(src.height * k))).resize((p["tw"], p["th"]), Image.LANCZOS)
    rgb = np.asarray(im).astype(np.float64)
    if p["wraps"]:
        rgb = batch.tileable(rgb)
    a = src.getchannel("A").resize(im.size, Image.LANCZOS)
    Image.merge("RGBA", (*Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8)).split(), a)).save(dst)
    print(f"{name}: {p['w']}x{p['h']} -> {p['tw']}x{p['th']}")
    return dst


def sheet(names, k=6, side=640):
    for i in range(0, len(names), k):
        part = names[i:i + k]
        sh = Image.new("RGB", (side * 2, side * len(part)), (110, 110, 110))
        d = ImageDraw.Draw(sh)
        for j, n in enumerate(part):
            for c, path in enumerate((batch.path("src", n + ".png"), os.path.join(OUT, n + ".png"))):
                im = Image.open(path).convert("RGBA")
                s = side / max(im.size)
                im = im.resize((round(im.width * s), round(im.height * s)), Image.LANCZOS)
                bg = Image.new("RGBA", im.size, (110, 110, 110, 255))
                bg.alpha_composite(im)
                sh.paste(bg.convert("RGB"), (c * side, j * side))
            d.text((6, j * side + 6), n, fill=(255, 255, 0))
        sh.save(os.path.join(OUT, f"sheet_{i // k}.jpg"), quality=90)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("names", nargs="+")
    ap.add_argument("--sheet", action="store_true")
    args = ap.parse_args()
    plan = batch.load_plan()
    done = [n for n in args.names if upscale(n, plan)]
    if args.sheet:
        sheet(done)


if __name__ == "__main__":
    main()
