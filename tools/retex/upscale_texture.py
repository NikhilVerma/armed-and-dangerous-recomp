"""Faithful texture upscale, end to end (docs/retexture.md). For one texture name:

  1. take the original from a level's txtr .big dump (work/retex/dump/textures/<name>.dds, tools/formats/bigtool.py)
  2. upscale 4x with the Media Lab neural upscaler (Nomos2 DAT2: the most faithful of the installed models)
  3. de-ring (dering.py: no halos past the original's local range)
  4. back-project (backproject.py: shrunk to the original size it reproduces the original)
  5. score it (judge.py) and refuse it below the faithfulness bar
  6. write <out>/<name>.png in the texture's stored orientation, for overrides\\textures\\

Diffusion upscalers (clarity, creative) redraw content (measured on AnD_title: a different lion, a double for a triple
barrel; PSNR 28-29 dB after shrinking) and are not used here.

Usage: .venv/Scripts/python.exe tools/retex/upscale_texture.py NAME [--out work/retex/out] [--model 4xNomos2_hq_dat2.pth]
       [--min-psnr 45]
"""
import argparse
import os
import shutil
import subprocess
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import judge  # noqa: E402
import medialab  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("name")
    ap.add_argument("--dump", default=os.path.join(ROOT, "work", "retex", "dump", "textures"))
    ap.add_argument("--out", default=os.path.join(ROOT, "work", "retex", "out"))
    ap.add_argument("--model", default="4xNomos2_hq_dat2.pth")
    ap.add_argument("--min-psnr", type=float, default=45.0)
    a = ap.parse_args()
    work = os.path.join(ROOT, "work", "retex", "jobs", a.name)
    os.makedirs(work, exist_ok=True)
    os.makedirs(a.out, exist_ok=True)
    src = Image.open(os.path.join(a.dump, a.name + ".dds"))
    alpha = src.mode == "RGBA" and src.getchannel("A").getextrema() != (255, 255)
    # The dump decodes rows as stored (upside down for the game's DDS files); work upright, store as found.
    upright = os.path.join(work, "orig.png")
    src.convert("RGB").transpose(Image.FLIP_TOP_BOTTOM).save(upright)
    up = medialab.run("upscale-faithful", [upright], os.path.join(work, "up"),
                      extra={"upscale_model": a.model, "upscale_factor": 4})[0]
    py = sys.executable
    dering = os.path.join(work, "dering.png")
    final = os.path.join(work, "final.png")
    subprocess.run([py, os.path.join(HERE, "dering.py"), upright, up, dering], check=True)
    subprocess.run([py, os.path.join(HERE, "backproject.py"), upright, dering, final, "--steps", "6"], check=True)
    s = judge.score(judge.load(upright), judge.load(final))
    print(f"{a.name}: {s['size']} PSNR {s['psnr']:.2f} SSIM {s['ssim']:.4f} shift {s['shift']:.2f} "
          f"detail {s['detail']:.2f}")
    if s["psnr"] < a.min_psnr:
        print(f"{a.name}: below the faithfulness bar ({a.min_psnr} dB); not written")
        return 1
    out = Image.open(final).convert("RGB")
    if alpha:  # the alpha channel: resampled (Lanczos), not generated
        a_ch = src.getchannel("A").transpose(Image.FLIP_TOP_BOTTOM).resize(out.size, Image.LANCZOS)
        out = Image.merge("RGBA", (*out.split(), a_ch))
    out.transpose(Image.FLIP_TOP_BOTTOM).save(os.path.join(a.out, a.name + ".png"))
    shutil.copy(final, os.path.join(a.out, a.name + ".upright.png"))
    print(os.path.join(a.out, a.name + ".png"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
