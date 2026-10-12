"""The agreed texture remaster (docs/retexture.md "Recipe as agreed", 2026-10-11): for every planned texture,
SeedVR2 3b (colour none) to the target size, then Qwen Image 2.1 with that canvas and the original (strength 0.9,
cfg 5, 25 steps, the player's prompt, no negative). Normal maps use batch.NORMAL_PROMPT. Colour and lighting
correction is a separate later step (post).

Results: work/retex/batch/out2/<name>/ (canvas.png, take-01_00001_.png, state in state.json). One process queues
everything (Media Lab runs jobs one at a time, in order): all SeedVR2 jobs first, then each Qwen job as soon as its
canvas is downloaded, then the Qwen results are collected. state.json makes it resumable: a rerun waits for jobs
already submitted instead of submitting them again.

Usage: .venv/Scripts/python.exe tools/retex/recipe.py run [NAME ...] | large | status
"""
import json
import os
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import batch  # noqa: E402
import medialab  # noqa: E402

PROMPT = ("Enhance this image to high resolution with rich fine details. Sharpen textures and surfaces, add natural "
          "microdetails, and improve clarity and definition across the image while preserving the original "
          "composition, lighting, atmosphere, and visual style. The input may be an illustration or a real "
          "photograph; preserve the original style.")
QWEN = {"seed": 0, "image_steps": 25, "image_cfg": 5, "qwen_edit_strength": 0.9, "qwen_output_size": "source"}
# Not redone: hand-tuned by the player, fonts, lettering (faithful upscale; Qwen garbles text).
EXCLUDE = set(batch.HAND_TUNED) | {"mainfontid", "font", "N_B_T_snow", "B_level1_ground"}  # + Poly Haven snow floor


def out(name, *p):
    return batch.path("out2", name, *p)


def state(name):
    f = out(name, "state.json")
    return json.load(open(f)) if os.path.exists(f) else {}


def save_state(name, s):
    os.makedirs(out(name), exist_ok=True)
    json.dump(s, open(out(name, "state.json"), "w"))


def names_to_do(argv):
    plan = batch.load_plan()
    remaining = json.load(open(batch.path("remaining.json")))
    text = set(remaining.get("text", []))
    names = argv or [n for n in plan if n not in EXCLUDE]
    return plan, [n for n in sorted(names, key=lambda n: plan[n]["tw"] * plan[n]["th"])
                  if not os.path.exists(out(n, "take-01_00001_.png"))]


def kind(name):
    d = batch.path("desc", name + ".json")
    return json.load(open(d)).get("kind", "other") if os.path.exists(d) else "other"


def svr_only(name, p):
    """SeedVR2 alone, no Qwen pass: targets above the two-image Qwen budget (the player: "too big for qwen ... just
    use the seedvr2 upscale for now"; Media Lab allows about 1.5 MP for several references), tiny originals (64 px or
    less: Qwen invents, k_health's wood became stone panels, a 16 px smoke puff became a medallion) and particle
    textures (soft blobs, kind fx)."""
    src = Image.open(batch.path("src", name + ".png"))
    # Lettering too (the player: SeedVR2 3b kept the Y button's letter, Media Lab job 20261011-181431-1c07a9;
    # Qwen garbles text).
    text = set(json.load(open(batch.path("remaining.json"))).get("text", []))
    return (p["tw"] * p["th"] > 1024 * 1024 or max(src.size) <= 64 or kind(name) in ("fx", "text")
            or name in text)


def run(argv):
    plan, names = names_to_do(argv)
    print(f"{len(names)} textures", flush=True)
    # 1. SeedVR2 canvases, all queued at once.
    for n in names:
        s = state(n)
        if s.get("svr") or os.path.exists(out(n, "canvas.png")):
            continue
        p = plan[n]
        src = batch.path("src", n + ".png")
        w = Image.open(src).width
        try:  # SeedVR2 goes up to 8x; a larger target is reached by the Lanczos resize below
            # The player: 3b for the canvas under the Qwen pass, 7b where SeedVR2 is the final upscale.
            model = "7b" if svr_only(n, p) else "3b"
            job = medialab.submit("seedvr2", [src], extra={"seedvr2_model": model, "seedvr2_color": "none",
                                                           "seedvr2_scale": min(p["tw"] / w, 8.0)})
        except SystemExit as e:
            print(f"{n}: seedvr2 not queued: {e}", flush=True)
            continue
        s["svr"], s["svr_model"] = job["id"], model
        save_state(n, s)
    print("canvases queued", flush=True)
    # 2. As each canvas lands: the canvas at the exact target size with the original's alpha, then the Qwen job.
    for n in names:
        s = state(n)
        if s.get("qwen") or not (s.get("svr") or os.path.exists(out(n, "canvas.png"))):
            continue
        p = plan[n]
        src = Image.open(batch.path("src", n + ".png")).convert("RGBA")
        canvas_path = out(n, "canvas.png")
        if not os.path.exists(canvas_path):
            job = medialab.wait(s["svr"], timeout=48 * 3600)
            if job["status"] not in ("complete", "partial"):
                print(f"{n}: seedvr2 {job['status']}", flush=True)
                s.pop("svr", None)
                save_state(n, s)
                continue
            got = medialab.download(job, out(n, "svr"))[0]
            c = Image.open(got).convert("RGBA")
            if c.size != (p["tw"], p["th"]):
                c = c.resize((p["tw"], p["th"]), Image.LANCZOS)
            if p.get("alpha"):
                c.putalpha(src.getchannel("A").resize(c.size, Image.LANCZOS))
            c.save(canvas_path)
        if svr_only(n, p):
            Image.open(canvas_path).save(out(n, "take-01_00001_.png"))
            s["method"] = "seedvr2"
            save_state(n, s)
            continue
        prompt = batch.NORMAL_PROMPT if kind(n) == "normal" else PROMPT
        try:
            job = medialab.submit("qwen-image-21", [canvas_path, batch.path("src", n + ".png")], prompt, "", QWEN)
        except SystemExit as e:
            print(f"{n}: qwen not queued: {e}", flush=True)
            continue
        s["qwen"] = job["id"]
        save_state(n, s)
        print(f"{n}: qwen queued", flush=True)
    # 3. Collect.
    for n in names:
        s = state(n)
        if not s.get("qwen") or os.path.exists(out(n, "take-01_00001_.png")):
            continue
        job = medialab.wait(s["qwen"], timeout=48 * 3600)
        if job["status"] not in ("complete", "partial"):
            print(f"{n}: qwen {job['status']}", flush=True)
            s.pop("qwen", None)
            save_state(n, s)
            continue
        medialab.download(job, out(n))
        print(f"{n}: done", flush=True)


def large():
    """The textures above 1024 px (not in the plan: skies, the ad image) with SeedVR2 7b alone at 2x (the player).
    b_a_l4_m1_all is a level blend mask (data), left as it is."""
    d = batch.path("..", "large")
    os.makedirs(os.path.join(d, "out_svr"), exist_ok=True)
    jobs = []
    for f in sorted(os.listdir(os.path.join(d, "src"))):
        n = f[:-4]
        if n == "b_a_l4_m1_all" or os.path.exists(os.path.join(d, "out_svr", f)):
            continue
        job = medialab.submit("seedvr2", [os.path.join(d, "src", f)],
                              extra={"seedvr2_model": "7b", "seedvr2_color": "none", "seedvr2_scale": 2.0})
        jobs.append((n, job["id"]))
    for n, jid in jobs:
        job = medialab.wait(jid, timeout=48 * 3600)
        if job["status"] in ("complete", "partial"):
            got = medialab.download(job, os.path.join(d, "svr", n))[0]
            Image.open(got).save(os.path.join(d, "out_svr", n + ".png"))
            print(f"{n}: done", flush=True)
        else:
            print(f"{n}: {job['status']}", flush=True)


MAX_STRUCTURE = 0.12  # rank_drift structure change above which a result counts as broken (the player: "0.12 is fine")
# _low LOD textures that are exact shrunk copies of a full-size texture (layout match >= 0.998, 2026-10-11): their
# remaster is the full one's, resized.
LOW_FROM = {"k_mg_low": "k_mg", "b_fur_low": "b_fur", "B_trotter_face_low": "B_trotter_face",
            "k_q_chaingun_low": "k_q_chaingun", "k_landsharkgun_low": "k_landsharkgun",
            "k_landsharkmissile_low": "k_landsharkmissile", "N_T_metal_rust_low": "N_t_metal_rust"}


def finish(name, img, p):
    """Colour and lighting pass, original alpha, seamless edges, normal-map renormalisation; returns the stored image."""
    import judge
    src = Image.open(batch.path("src", name + ".png")).convert("RGBA")
    out = img.convert("RGBA").resize((p["tw"], p["th"]), Image.LANCZOS)
    rgb = np.asarray(out)[..., :3].astype(np.float64)
    k = kind(name)
    s = (2.0 if k in ("material", "sky", "other", "normal") else 3.0) * out.width / src.width
    base = np.asarray(src.convert("RGB").resize(out.size, Image.BICUBIC)).astype(np.float64)
    if k == "normal":
        rgb = np.stack([rgb[..., c] - judge.blur(rgb[..., c], s) + judge.blur(base[..., c], s) for c in range(3)], -1)
    else:
        wb = wo = None
        if p["alpha"]:  # visible texels only (alpha-weighted)
            wb = np.asarray(src.getchannel("A").resize(out.size, Image.BICUBIC)).astype(np.float64) / 255.0
            wo = np.asarray(out.getchannel("A")).astype(np.float64) / 255.0
            if wo.max() - wo.min() < 0.01:  # the upscaler gave no alpha: the original's
                wo = wb
        rgb = batch.normalise_saturation(batch.normalise_lighting(rgb, base, s, wo, wb), base, 4.0 * s, wo, wb)
    if p["alpha"]:
        rgb = batch.bleed(rgb, np.asarray(src.getchannel("A").resize(out.size, Image.LANCZOS)))
    if p["wraps"] and k in ("material", "sky", "other", "normal"):
        rgb = batch.tileable(rgb)
    if k == "normal":
        v = rgb / 127.5 - 1
        xy = v[..., :2] * np.minimum(1.0, 0.98 / np.maximum(np.sqrt((v[..., :2] ** 2).sum(-1, keepdims=True)), 1e-6))
        rgb = (np.concatenate([xy, np.sqrt(np.clip(1 - (xy ** 2).sum(-1, keepdims=True), 0, 1))], -1) + 1) * 127.5
    if p["alpha"] and k != "normal":
        # Whole-texture brightness of the visible part (alpha-weighted) matched to the original: soft, mostly
        # transparent particles stayed about half as bright after the local correction (fx_snow_flakes 55 -> 13).
        L = np.array([0.2126, 0.7152, 0.0722])
        wa = np.asarray(src.getchannel("A").resize(out.size, Image.BICUBIC)).astype(np.float64) / 255.0
        if wa.sum() > 1.0:
            lo = ((base @ L) * wa).sum() / wa.sum()
            ln = ((np.clip(rgb, 0, 255) @ L) * wa).sum() / wa.sum()
            rgb = rgb * float(np.clip((lo + 1.0) / (ln + 1.0), 0.5, 4.0))
    if p["alpha"] and k != "normal":
        # Soft edges keep the original's colour: the upscalers darkened the colour where alpha fades (dark fringes; a
        # soft skid puff, fx_smoke_snow, rendered as a black blob; the player, 2026-10-12). The remaster's colour only
        # where the texture is mostly opaque.
        wa = np.asarray(src.getchannel("A").resize(out.size, Image.BICUBIC)).astype(np.float64) / 255.0
        w = np.clip((wa - 0.15) / 0.55, 0.0, 1.0)
        w = (w * w * (3.0 - 2.0 * w))[..., None]
        rgb = base * (1.0 - w) + rgb * w
    # The original's alpha always (SeedVR2 made 92 opaque textures partly transparent).
    a = np.asarray(src.getchannel("A").resize(out.size, Image.LANCZOS)) if p["alpha"] else         np.full(rgb.shape[:2], 255, np.uint8)
    return Image.fromarray(np.dstack([np.clip(rgb + 0.5, 0, 255).astype(np.uint8), a]), "RGBA")


def finalize(install=False):
    """Rank (rank_drift); faithful results above MAX_STRUCTURE: below 32 px the original, else SwinIR-L + Qwen;
    _low textures from their full versions; colour pass; install (--install); the oversized ones from SeedVR2."""
    import rank_drift
    import sweep
    plan = batch.load_plan()
    results = {}
    for n in plan:
        f = out(n, "take-01_00001_.png")
        if n not in EXCLUDE and os.path.exists(f):
            results[n] = rank_drift.measure(n, f)
    # Only faithful (SeedVR2-only) upscales are gated: a Qwen result is a remaster and is meant to differ (the
    # player, 2026-10-12: "qwen is different, its a remaster it WON'T look the same").
    broken = [n for n, m in results.items()
              if m["structure"] > MAX_STRUCTURE and n not in LOW_FROM and svr_only(n, plan[n])]
    print(f"{len(results)} results, {len(broken)} above {MAX_STRUCTURE}: SwinIR-L fallback", flush=True)
    # Broken faithful upscales (the player, 2026-10-12): originals below 32 px stay as they are; the rest get a
    # SwinIR-L canvas (no invented content) and then the same Qwen pass as the recipe (canvas + original).
    tiny = {n for n in broken if max(Image.open(batch.path("src", n + ".png")).size) < 32}
    redo = [n for n in broken if n not in tiny]
    print(f"{len(tiny)} below 32 px kept as original, {len(redo)} SwinIR-L + Qwen", flush=True)
    jobs = {}
    for n in redo:
        if os.path.exists(out(n, "fallback.png")):
            continue
        rgb = out(n, "orig_rgb.png")
        Image.open(batch.path("src", n + ".png")).convert("RGB").save(rgb)
        jobs[n] = medialab.submit("upscale-faithful", [rgb], extra={
            "upscale_model": "003_realSR_BSRGAN_DFOWMFC_s64w8_SwinIR-L_x4_PSNR.pth", "upscale_factor": 4})["id"]
    for n, jid in jobs.items():
        job = medialab.wait(jid, timeout=48 * 3600)
        if job["status"] in ("complete", "partial"):
            Image.open(medialab.download(job, out(n, "swin"))[0]).save(out(n, "fallback.png"))
    qjobs = {}
    for n in redo:
        if os.path.exists(out(n, "swinqwen.png")) or not os.path.exists(out(n, "fallback.png")):
            continue
        p = plan[n]
        src = Image.open(batch.path("src", n + ".png")).convert("RGBA")
        c = Image.open(out(n, "fallback.png")).convert("RGBA").resize((p["tw"], p["th"]), Image.LANCZOS)
        if p.get("alpha"):
            c.putalpha(src.getchannel("A").resize(c.size, Image.LANCZOS))
        c.save(out(n, "swin_canvas.png"))
        prompt = batch.NORMAL_PROMPT if kind(n) == "normal" else PROMPT
        # Two images fit 1.5 MP in total; larger targets send the canvas alone (one image: about 4.2 MP).
        two = p["tw"] * p["th"] + src.width * src.height <= 1_500_000
        images = [out(n, "swin_canvas.png")] + ([batch.path("src", n + ".png")] if two else [])
        try:
            qjobs[n] = medialab.submit("qwen-image-21", images, prompt, "", QWEN)["id"]
        except SystemExit as e:
            print(f"{n}: qwen not queued: {e}", flush=True)
    for n, jid in qjobs.items():
        job = medialab.wait(jid, timeout=48 * 3600)
        if job["status"] in ("complete", "partial"):
            Image.open(medialab.download(job, out(n, "swinqwen"))[0]).save(out(n, "swinqwen.png"))
    final = {}
    for n, m in results.items():
        img, method = Image.open(out(n, "take-01_00001_.png")), "recipe"
        if n in tiny:
            img, method = None, "original"  # no override: the game's own texture
        elif n in broken:
            sq = out(n, "swinqwen.png")
            img, method = (Image.open(sq), "swinir+qwen") if os.path.exists(sq) else (None, "original")
        final[n] = (img, method)
    for lo, hi in LOW_FROM.items():
        if hi in final and final[hi][0] is not None and lo in plan:
            final[lo] = (final[hi][0], "from " + hi)
    counts = {}
    for n, (img, method) in final.items():
        counts[method.split()[0]] = counts.get(method.split()[0], 0) + 1
        for d in (batch.OVERRIDES if install else []):
            f = os.path.join(d, n + ".png")
            if img is None:
                if os.path.exists(f) and n not in batch.HAND_TUNED:
                    os.remove(f)  # an older (worse) remaster: back to the original
                continue
        if img is None:
            continue
        res = finish(n, img, plan[n])
        res.save(out(n, "final.png"))
        json.dump({"method": method, **results.get(n, {})}, open(out(n, "final.json"), "w"))
        if not install:
            continue
        stored = res.transpose(Image.FLIP_TOP_BOTTOM)
        for d in batch.OVERRIDES:
            stored.save(os.path.join(d, n + ".png"))
        plan[n]["installed"], plan[n]["method"] = True, method
    if install:
        batch.save_plan(plan)
    # The oversized textures (SeedVR2 7b, 2x): opaque skies and the ad.
    large = batch.path("..", "large", "out_svr")
    if install and os.path.isdir(large):
        for f in os.listdir(large):
            im = Image.open(os.path.join(large, f)).convert("RGBA")
            im.putalpha(255)
            for d in batch.OVERRIDES:
                im.transpose(Image.FLIP_TOP_BOTTOM).save(os.path.join(d, f))
            counts["large"] = counts.get("large", 0) + 1
    print("installed by method:", counts, flush=True)


def status():
    plan, todo = names_to_do([])
    done = sum(os.path.exists(out(n, "take-01_00001_.png")) for n in plan)
    print(f"done {done}, to do {len(todo)}")


if __name__ == "__main__":
    cmd = sys.argv[1]
    if cmd == "run":
        run(sys.argv[2:])
    elif cmd == "status":
        status()
    elif cmd == "large":
        large()
    elif cmd == "finalize":
        finalize("--install" in sys.argv)
