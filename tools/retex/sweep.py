"""Settings sweep for the Qwen texture remaster (the player, 2026-10-11: strength 1.0 from the small original redrew
textures; 0.9 from a Lanczos enlargement "barely modified any pixels"). For each texture and setting, two numbers:

  fidelity  SSIM of luminance against the original at the original's size, alpha-weighted (transparent texels do
            not count); 1 = the same picture
  detail    fine-detail energy of the result over that of the Lanczos enlargement at the output size (mean absolute
            difference from a 1.5 px blur, alpha-weighted); 1 = nothing added

Settings: "small:S" = the original at its own size, custom output at the target size, strength S (the old batch at
S = 1.0); "base:S" = the Lanczos enlargement at the target size, source output, strength S; "sr:S" = the same from a
faithful neural 4x (sharp base; the player: a Lanczos base teaches the model to stay blurry).
Results: work/retex/batch/sweep/<name>/<setting>/, table in sweep.json, montage sweep_<name>.png.

Usage: .venv/Scripts/python.exe tools/retex/sweep.py NAME [NAME ...] --settings small:1.0,base:0.9,base:1.0
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

W = np.array([0.2126, 0.7152, 0.0722])
# The player's prompt (Media Lab job 20261011-165915-1c14b4: neural-sharpened canvas, strength 0.9, cfg 7, 20 steps,
# no negative): rich added detail with the layout kept.
ENHANCE = ("Enhance this image to high resolution with rich fine details. Sharpen textures and surfaces, add natural "
           "microdetails, and improve clarity and definition across the image while preserving the original "
           "composition, lighting, atmosphere, and visual style. The input may be an illustration or a real "
           "photograph; preserve the original style.")
REF_PROMPT = ("Image 1 is an enlarged, blurry copy of a video game texture; image 2 is the original texture at its "
              "native resolution. Redraw image 1 at full resolution as a sharp, detailed version of image 2: the "
              "same shapes, objects, layout and colours in exactly the same places, with the fine detail the "
              "original high-resolution texture would have had. Do not add, remove or move anything. ")


def lum(im, size=None):
    im = im.convert("RGBA")
    if size and im.size != size:
        im = im.resize(size, Image.LANCZOS)
    return np.asarray(im.convert("RGB")).astype(np.float64) @ W


def fidelity(src, out):
    a = np.asarray(src.convert("RGBA").getchannel("A")).astype(np.float64) / 255.0
    ya, yb = judge.blur(lum(src), 1.0), judge.blur(lum(out, src.size), 1.0)
    mu_a, mu_b = judge.blur(ya, 3), judge.blur(yb, 3)
    va, vb = judge.blur(ya * ya, 3) - mu_a ** 2, judge.blur(yb * yb, 3) - mu_b ** 2
    cov = judge.blur(ya * yb, 3) - mu_a * mu_b
    c1, c2 = (0.01 * 255) ** 2, (0.03 * 255) ** 2
    s = ((2 * mu_a * mu_b + c1) * (2 * cov + c2)) / ((mu_a ** 2 + mu_b ** 2 + c1) * (va + vb + c2))
    wgt = judge.blur(a, 2) + 1e-6
    return float((s * wgt).sum() / wgt.sum())


def detail(base, out):
    a = np.asarray(base.convert("RGBA").getchannel("A")).astype(np.float64) / 255.0 + 1e-6

    def hf(im):
        y = lum(im, base.size)
        return float((np.abs(y - judge.blur(y, 1.5)) * a).sum() / a.sum())
    return hf(out) / max(hf(base), 1e-6)


def hat_canvas(n, src, p, root, model="HAT-L_SRx4_ImageNet-pretrain.pth", tag="hat"):
    """A PSNR-model canvas (sharp, no invented texture), 4x, resized to the target size, the original's alpha.
    HAT-L by default; SwinIR-L realSR (trained on real-world degradation: removes compression blocks) as "swin"."""
    path = os.path.join(root, n, f"base_{tag}.png")
    if not os.path.exists(path):
        rgb = os.path.join(root, n, "orig_rgb.png")
        src.convert("RGB").save(rgb)
        up = medialab.run("upscale-faithful", [rgb], os.path.join(root, n, tag),
                          extra={"upscale_model": model, "upscale_factor": 4})[0]
        im = Image.open(up).convert("RGB").resize((p["tw"], p["th"]), Image.LANCZOS)
        Image.merge("RGBA", (*im.split(), src.getchannel("A").resize(im.size, Image.LANCZOS))).save(path)
    return path


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("names", nargs="+")
    ap.add_argument("--settings", default="small:1.0,base:0.9,base:1.0")
    a = ap.parse_args()
    plan = batch.load_plan()
    root = batch.path("sweep")
    settings = a.settings.split(",")
    jobs = []
    for n in a.names:
        p = plan[n]
        d = json.load(open(batch.path("desc", n + ".json")))
        src_path = batch.path("src", n + ".png")
        src = Image.open(src_path).convert("RGBA")
        os.makedirs(os.path.join(root, n), exist_ok=True)
        base_path = os.path.join(root, n, "base.png")
        src.resize((p["tw"], p["th"]), Image.LANCZOS).save(base_path)
        sr_path = os.path.join(root, n, "base_sr.png")
        if any(s.startswith(("sr:", "srref:", "enh:")) for s in settings) and not os.path.exists(sr_path):
            # A sharp base: the faithful neural upscaler (no diffusion) 4x, resized to the target size.
            rgb = os.path.join(root, n, "orig_rgb.png")
            src.convert("RGB").save(rgb)
            up = medialab.run("upscale-faithful", [rgb], os.path.join(root, n, "sr"),
                              extra={"upscale_model": "4xNomos2_hq_dat2.pth", "upscale_factor": 4})[0]
            sr = Image.open(up).convert("RGB").resize((p["tw"], p["th"]), Image.LANCZOS)
            sr = Image.merge("RGBA", (*sr.split(), src.getchannel("A").resize(sr.size, Image.LANCZOS)))
            sr.save(sr_path)
        for s in settings:
            # A third field "n": no texture description in the prompt (Haiku's descriptions can be wrong, and Qwen
            # follows the text: x_dirtrocks1 described as orange earth with pebble piles was painted that way).
            parts = s.split(":")
            mode, strength = parts[0], parts[1]
            describe = "" if len(parts) > 2 and parts[2] == "n" else d.get("describe", "")
            cfg = float(parts[3]) if len(parts) > 3 else 2.0  # a fourth field: guidance (the player's test: 5..7)
            out_dir = os.path.join(root, n, s.replace(":", "_"))
            if os.path.exists(os.path.join(out_dir, "take-01_00001_.png")):
                jobs.append((n, s, None, out_dir))
                continue
            if mode == "small":
                prompt = batch.GENERIC + " " + describe
                job = medialab.submit("qwen-image-21", [src_path], prompt, batch.NEGATIVE,
                                      {"seed": 0, "image_steps": 25, "image_cfg": 2,
                                       "qwen_edit_strength": float(strength), "qwen_output_size": "custom",
                                       "custom_width": p["tw"], "custom_height": p["th"]})
            else:
                prompt = batch.RECON + describe
                # "ref" / "srref": the enlargement is the canvas (image 1) and the original the reference (image 2;
                # the player: "qwen accepts multiple inputs ... try sending both").
                canvas = sr_path if mode in ("sr", "srref") else base_path
                images = [canvas] + ([src_path] if mode in ("ref", "srref") else [])
                if mode in ("ref", "srref"):
                    prompt = REF_PROMPT + describe
                negative = batch.NEGATIVE
                if mode in ("enh", "enhl", "enhh", "enhs"):  # the player's recipe: their prompt, no negative, 20 steps
                    # canvas: enh = Nomos2 (GAN, invents texture), enhl = Lanczos, enhh = HAT-L (PSNR, invents none),
                    # enhs = SwinIR-L realSR (PSNR, trained on compression and noise)
                    if mode == "enhs":
                        canvas = hat_canvas(n, src, p, root,
                                            "003_realSR_BSRGAN_DFOWMFC_s64w8_SwinIR-L_x4_PSNR.pth", "swin")
                    else:
                        canvas = {"enh": sr_path, "enhl": base_path}.get(mode) or hat_canvas(n, src, p, root)
                    images, prompt, negative = [canvas], ENHANCE, ""
                job = medialab.submit("qwen-image-21", images, prompt,
                                      negative,
                                      {"seed": 0, "image_steps": 20 if mode == "enh" else 25, "image_cfg": cfg,
                                       "qwen_edit_strength": float(strength), "qwen_output_size": "source"})
            jobs.append((n, s, job["id"], out_dir))
    table = {}
    for n, s, jid, out_dir in jobs:
        if jid:
            job = medialab.wait(jid)
            if job["status"] not in ("complete", "partial"):
                print(f"{n} {s}: {job['status']}", flush=True)
                continue
            medialab.download(job, out_dir)
        out = Image.open(os.path.join(out_dir, "take-01_00001_.png"))
        src = Image.open(batch.path("src", n + ".png"))
        base = Image.open(os.path.join(root, n, "base.png"))
        f, dt = fidelity(src, out), detail(base, out)
        table.setdefault(n, {})[s] = {"fidelity": round(f, 3), "detail": round(dt, 2)}
        print(f"{n:22s} {s:10s} fidelity {f:.3f} detail {dt:.2f}", flush=True)
    json.dump(table, open(os.path.join(root, "sweep.json"), "w"), indent=1)
    for n in a.names:
        cells = [Image.open(os.path.join(root, n, "base.png"))] + [
            Image.open(os.path.join(root, n, s.replace(":", "_"), "take-01_00001_.png")) for s in settings
            if os.path.exists(os.path.join(root, n, s.replace(":", "_"), "take-01_00001_.png"))]
        sh = Image.new("RGB", (len(cells) * 388, 384), (60, 60, 60))
        for i, c in enumerate(cells):
            bg = Image.new("RGBA", (384, 384), (90, 90, 90, 255))
            bg.alpha_composite(c.convert("RGBA").resize((384, 384), Image.LANCZOS))
            sh.paste(bg.convert("RGB"), (i * 388, 0))
        sh.save(os.path.join(root, f"sweep_{n}.png"))


if __name__ == "__main__":
    main()
