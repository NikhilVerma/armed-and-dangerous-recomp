"""Regenerates the textures the 2026-10-25 audit rejected (tools/data/retexture_rejects.json).

The rejected AI textures are blurry or invent shapes, and no single upscaler suits every size. In the 2026-11-02 trials
PiD gave the best results from 256 px up, and broke smaller sources into blocks and grid lines. So each texture takes one
of two routes:

- 256 px and up: PiD at 2x (pid.py). A 256 px source hasn't been judged at 4x.
- Under 256 px: SwinIR 4x cleans and enlarges the original, then Qwen at edit strength 0.5 redraws it lightly.
- Textures whose description is flagged as lettering or hand-tuned also take the Qwen route, because PiD garbles small
  text (the "COLLAR" label on b_grunt_front).

Results are upright PNGs in work/retex/pid_batch (PiD) and work/retex/regen (Qwen). results.json in work/retex/regen says
where each one is. This tool doesn't install them. A rerun skips finished textures and retries failed ones.

Needs MEDIALAB_URL. Usage: python tools/retex/regen_rejects.py [--names a,b,c]
"""
import argparse
import glob
import json
import os
import sys
from concurrent.futures import ThreadPoolExecutor

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import batch  # noqa: E402
import medialab  # noqa: E402
import pid  # noqa: E402

OUT = os.path.join(batch.ROOT, "work", "retex", "regen")
PID_MIN_SIDE = 256
# The prompt avoids per-texture descriptions, because Qwen follows the text even when a description is wrong.
QWEN_PROMPT = ("Enhance this image to high resolution with rich fine details. Sharpen textures and surfaces, add natural "
               "microdetails, and improve clarity and definition across the image while preserving the original "
               "composition, lighting, atmosphere, and visual style. The input may be an illustration or a real "
               "photograph; preserve the original style.")
QWEN = {"seed": 0, "image_steps": 20, "image_cfg": 7, "qwen_edit_strength": 0.5, "qwen_output_size": "source"}
WRAPPING_KINDS = ("material", "sky", "other")  # icons and sprites never repeat, whatever their edges look like


def qwen(name, src, wraps, kind):
    dst = os.path.join(OUT, name + ".png")
    if os.path.exists(dst):
        return dst
    work = os.path.join(OUT, "jobs", name)
    os.makedirs(work, exist_ok=True)
    size = (src.width * 4, src.height * 4)
    rgb = os.path.join(work, "rgb.png")
    src.convert("RGB").save(rgb)
    up = medialab.run("upscale-faithful", [rgb], os.path.join(work, "canvas"),
                      extra={"upscale_model": pid.CLEAN_MODEL, "upscale_factor": 4})[0]
    canvas = os.path.join(work, "canvas.png")
    Image.open(up).convert("RGB").resize(size, Image.LANCZOS).save(canvas)
    out = medialab.run("qwen-image-21", [canvas], os.path.join(work, "qwen"), QWEN_PROMPT, "", QWEN)[0]
    im = Image.open(out).convert("RGB").resize(size, Image.LANCZOS)
    if wraps and kind in WRAPPING_KINDS:
        im = Image.fromarray(np.clip(batch.tileable(np.asarray(im).astype(np.float64)), 0, 255).astype(np.uint8))
    Image.merge("RGBA", (*im.split(), src.getchannel("A").resize(size, Image.LANCZOS))).save(dst)
    return dst


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--names", default="", help="comma-separated names, for a trial; default: every rejected texture")
    a = ap.parse_args()
    os.makedirs(OUT, exist_ok=True)

    rejects = json.load(open(os.path.join(batch.ROOT, "tools", "data", "retexture_rejects.json")))
    wanted = {n.strip().lower() for n in a.names.split(",") if n.strip()} or \
             {n.lower() for n, v in rejects.items() if n != "about" and v["use"] == "original"}
    originals = {}
    for p in glob.glob(os.path.join(batch.ROOT, "work", "remaster", "*", "textures", "*.dds")):
        if "textures_pack" not in p:
            originals.setdefault(os.path.basename(p)[:-4].lower(), p)
    # Descriptions and pid.py use the game's spelling of a name, which can differ from the lower-case reject list.
    spelling = {os.path.basename(p)[:-5].lower(): os.path.basename(p)[:-5] for p in glob.glob(batch.path("desc", "*.json"))}

    os.makedirs(batch.path("src"), exist_ok=True)
    plan = batch.load_plan()
    jobs = []
    for low in sorted(wanted):
        name = spelling[low]
        src = Image.open(originals[low]).convert("RGBA").transpose(Image.FLIP_TOP_BOTTOM)
        src.save(batch.path("src", name + ".png"))
        desc = json.load(open(batch.path("desc", name + ".json")))
        arr = np.asarray(src)
        wraps = batch.wraps(arr)
        lettering = desc.get("skip") or desc.get("kind") == "text"
        route = "qwen" if lettering or max(src.size) < PID_MIN_SIDE or min(src.size) < pid.MIN_SIDE else "pid"
        if route == "pid":
            plan[name] = {"w": src.width, "h": src.height, "tw": src.width * 2, "th": src.height * 2, "scale": 2,
                          "alpha": bool(arr[..., 3].min() < 250), "wraps": wraps}
        jobs.append((name, route, src, wraps, desc.get("kind")))
    batch.save_plan(plan)

    def work(job):
        name, route, src, wraps, kind = job
        try:
            path = pid.upscale(name, plan) if route == "pid" else qwen(name, src, wraps, kind)
            print(f"done {route} {name}", flush=True)
            return name, {"route": route, "path": os.path.relpath(path, batch.ROOT), "orig": list(src.size)}
        except (Exception, SystemExit) as e:  # medialab.run raises SystemExit; one bad texture mustn't stop the batch
            print(f"FAILED {route} {name}: {e}", flush=True)
            return name, {"route": route, "error": str(e)[:300]}

    # Media Lab runs its queue in turn, so a few threads only keep the queue full.
    with ThreadPoolExecutor(4) as pool:
        results = dict(pool.map(work, jobs))
    with open(os.path.join(OUT, "results.json"), "w") as f:
        json.dump(results, f, indent=1)
    failed = [n for n, r in results.items() if "error" in r]
    print(f"finished: {len(results) - len(failed)} done, {len(failed)} failed {failed}", flush=True)


if __name__ == "__main__":
    main()
