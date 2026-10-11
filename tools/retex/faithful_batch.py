"""Faithful (non-generative) upscale for the textures Qwen must not redraw: lettering (kind "text") and Qwen results
rejected on review (plan.json "rejected"). Per texture: the Media Lab neural upscaler (no diffusion) 4x, de-ring,
back-project to the original (upscale_texture.py's chain), resize to the plan's target size, the original's alpha
resized in, then installed in stored row order like batch.py post. Hand-tuned textures are never touched.

Usage: .venv/Scripts/python.exe tools/retex/faithful_batch.py NAME [NAME ...] | --text | --rejected
"""
import os
import subprocess
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import batch  # noqa: E402
import medialab  # noqa: E402

MODEL = "4xNomos2_hq_dat2.pth"


def one(name, plan):
    p = plan[name]
    work = batch.path("faithful", name)
    os.makedirs(work, exist_ok=True)
    final_png = os.path.join(work, "final.png")
    src_path = batch.path("src", name + ".png")
    src = Image.open(src_path).convert("RGBA")
    if not os.path.exists(final_png):
        rgb = os.path.join(work, "orig.png")
        src.convert("RGB").save(rgb)
        up = medialab.run("upscale-faithful", [rgb], os.path.join(work, "up"),
                          extra={"upscale_model": MODEL, "upscale_factor": 4})[0]
        py = sys.executable
        dering = os.path.join(work, "dering.png")
        subprocess.run([py, os.path.join(HERE, "dering.py"), rgb, up, dering], check=True)
        subprocess.run([py, os.path.join(HERE, "backproject.py"), rgb, dering, final_png, "--steps", "6"], check=True)
    out = Image.open(final_png).convert("RGB").resize((p["tw"], p["th"]), Image.LANCZOS)
    a = src.getchannel("A").resize(out.size, Image.LANCZOS)
    res = Image.merge("RGBA", (*out.split(), a))
    stored = res.transpose(Image.FLIP_TOP_BOTTOM)
    for d in batch.OVERRIDES:
        os.makedirs(d, exist_ok=True)
        stored.save(os.path.join(d, name + ".png"))
    p["installed"] = True
    p["method"] = "faithful"
    print(f"{name}: {src.size} -> {out.size} installed", flush=True)


def main():
    plan = batch.load_plan()
    import json
    if sys.argv[1] == "--text":
        names = json.load(open(batch.path("remaining.json")))["text"]
    elif sys.argv[1] == "--rejected":
        names = [n for n, p in plan.items() if p.get("rejected")]
    else:
        names = sys.argv[1:]
    for n in names:
        if n in batch.HAND_TUNED:
            continue
        try:
            one(n, plan)
        except Exception as e:  # keep going; report
            print(f"{n}: failed {e}", flush=True)
        batch.save_plan(plan)


if __name__ == "__main__":
    main()
