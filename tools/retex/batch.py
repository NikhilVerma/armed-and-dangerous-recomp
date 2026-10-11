"""Batch Qwen remaster of game textures (docs/retexture.md "Batch"). Steps, each resumable:

  prepare N      the N smallest not-yet-prepared textures from work/retex/inventory.json: upright source PNG
                 (src/<name>.png), a preview for describing (preview/<name>.png, enlarged to about 512 px, alpha on
                 grey), and plan.json entries (target size: 4x when both sides stay <= 2048, else 2x; at least 256 per
                 side, Qwen's custom minimum; whether the original wraps seamlessly)
  run            every planned texture with a description (desc/<name>.json) and no result yet: Qwen Image 2.1,
                 generic prompt + description, custom output at the target size; skipped kinds are not generated
  post           results -> overrides: original alpha (resized) replaces Qwen's, seamless re-tiling when the original
                 wrapped, stored row order (flipped); written to the game's and the test instance's overrides folders
  sheet K        contact sheets of the processed textures (original | remaster), K per sheet, into the OneDrive folder

Descriptions (desc/<name>.json): {"kind": "portrait|ui|material|decal|sprite|fx|sky|text|other",
  "skip": false, "describe": "The image shows ..."}; written by describing subagents from the previews.

Usage: .venv/Scripts/python.exe tools/retex/batch.py prepare 120 | run | post | sheet 24
"""
import json
import os
import sys

import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import medialab  # noqa: E402

B = os.path.join(ROOT, "work", "retex", "batch")
SHARE = "C:/Users/nikhi/OneDrive/AnD-retexture-test/batch"
OVERRIDES = [os.path.join(ROOT, "orig", "pc", "GameData", "overrides", "textures"),
             os.path.join(ROOT, "work", "instances", "main", "overrides", "textures")]
GENERIC = ("Convert this video game asset into an upscaled realistic version of its original intention using modern "
           "physically based material rendering, correct shadows, correct lighting, accurate details without modifying "
           "the actual contents of the image itself.")
NEGATIVE = ("altered, reinterpreted, restyled, redesigned, recolored, relit, reframed, cropped, added elements, removed "
            "elements, invented detail, hallucinated detail, added texture, added grain, added pattern, added outlines, "
            "changed texture, changed material, changed medium, changed proportions, changed text, garbled text, "
            "upscaling artifacts, compression artifacts, oversharpening, halos, ringing, over-smoothing, watermark, "
            "signature")
SKIP_NAMES = ("font", "mainfontid")
STRENGTH = 0.9
RECON = ("This is a blurry, low-resolution enlargement of a video game texture. Reconstruct the fine detail the "
         "original high-resolution texture would have had. Keep every shape, object, crack, stone, edge position, "
         "layout and colour exactly where and as it is; only restore lost detail. ")
MIN_STRUCTURE = 0.85  # post(): below this the result is a different picture (strength_test.structure); rejected
# Tangent-space normal maps (kind "normal"): technical data, not a picture (worked on N_B_T_snow, 2026-10-11). post()
# renormalises them.
NORMAL_PROMPT = ("Upscale this tangent-space normal map to high resolution. It is technical data, not a photo: keep "
                 "the exact same normal map colour encoding (flat areas stay the same lavender blue, RGB about 128,128,"
                 "255; red and green tints encode slope direction). Reconstruct crisp, fine surface relief with every "
                 "existing bump, groove, crack and edge in the same place as the original. No lighting, no shadows, no "
                 "colour change, no photographic content.")
NORMAL_NEGATIVE = ("photograph, shading, shadows, highlights, lighting, colour shift, saturated, text, blur, "
                   "over-smoothing, added objects, changed layout, seams, watermark")


def path(*p):
    return os.path.join(B, *p)


def load_plan():
    p = path("plan.json")
    return json.load(open(p)) if os.path.exists(p) else {}


def save_plan(plan):
    json.dump(plan, open(path("plan.json"), "w"), indent=1)


def wraps(a):
    """The original tiles seamlessly: the wrap-around edge difference is like an ordinary neighbour difference."""
    a = a[..., :3].astype(np.float64)
    inner = np.abs(a[:, 1:] - a[:, :-1]).mean() + np.abs(a[1:] - a[:-1]).mean()
    edge = np.abs(a[:, 0] - a[:, -1]).mean() + np.abs(a[0] - a[-1]).mean()
    return bool(edge <= 1.5 * inner + 1.0)


def target(w, h):
    # 4x while the result stays <= 1024 per side (a 2048x2048 Qwen job takes ~2.5 min and fills the 24 GB GPU
    # the player shares), otherwise 2x up to 2048.
    for k in (4, 2):
        if (k == 4 and w * k <= 1024 and h * k <= 1024) or (k == 2 and w * k <= 2048 and h * k <= 2048):
            tw, th = w * k, h * k
            s = max(1.0, 256 / tw, 256 / th)  # Qwen custom minimum 256 per side
            return int(round(tw * s / 16) * 16), int(round(th * s / 16) * 16), k
    return None


def prepare(n):
    os.makedirs(path("src"), exist_ok=True)
    os.makedirs(path("preview"), exist_ok=True)
    inv = json.load(open(os.path.join(ROOT, "work", "retex", "inventory.json")))
    plan = load_plan()
    todo = []
    for t in inv:
        v = t["variants"][0]
        if t["name"] in plan or any(s in t["name"].lower() for s in SKIP_NAMES):
            continue
        if target(v["w"], v["h"]) is None or min(v["w"], v["h"]) < 16:  # under 16 px Qwen invents content
            continue
        todo.append((v["w"] * v["h"], t["name"], v))
    todo.sort()
    for _, name, v in todo[:n]:
        im = Image.open(os.path.join(ROOT, v["path"])).convert("RGBA").transpose(Image.FLIP_TOP_BOTTOM)
        a = np.asarray(im)
        alpha = bool(a[..., 3].min() < 250)
        im.save(path("src", name + ".png"))
        k = max(1, 512 // max(im.size))
        pv = im.resize((im.width * k, im.height * k), Image.LANCZOS) if k > 1 else im
        bg = Image.new("RGBA", pv.size, (110, 110, 110, 255))
        bg.alpha_composite(pv)
        bg.convert("RGB").save(path("preview", name + ".png"))
        tw, th, k = target(im.width, im.height)
        plan[name] = {"w": im.width, "h": im.height, "tw": tw, "th": th, "scale": k, "alpha": alpha,
                      "wraps": wraps(a), "levels": len(v["levels"]), "fmt": v["fmt"]}
        print(f"{name}: {im.width}x{im.height} -> {tw}x{th} alpha {alpha} wraps {plan[name]['wraps']}")
    save_plan(plan)


HAND_TUNED = ("b_jonesy_face", "b_q_face", "b_compass", "B_T_tree_bark_1", "b_monk_face", "AnD_title")
TEXT_SKIP = ("b_q_name", "n_temp", "b_wanted")


def run():
    plan = load_plan()
    for name in HAND_TUNED + TEXT_SKIP:  # never batch-generated (describers may have written a description)
        if name in plan:
            json.dump({"kind": "other", "skip": True, "describe": "", "note": "hand-tuned or lettering"},
                      open(path("desc", name + ".json"), "w"))
    os.makedirs(path("out"), exist_ok=True)
    # Media Lab queues jobs and runs them one at a time, so submit every texture up front and collect the results as
    # they finish (the GPU never idles between jobs). The job id goes to out/<name>/queued.json, created exclusively:
    # a rerun, or a second copy running at the same time, waits for that job instead of submitting it again (two
    # copies once submitted every texture twice; Media Lab history, 2026-10-11).
    queued = []
    for name, p in sorted(plan.items(), key=lambda kv: kv[1]["tw"] * kv[1]["th"]):  # small outputs first
        dfile = path("desc", name + ".json")
        done = path("out", name, "take-01_00001_.png")
        if os.path.exists(done) or not os.path.exists(dfile) or p.get("rejected"):
            continue
        d = json.load(open(dfile))
        if d.get("skip"):
            continue
        os.makedirs(path("out", name), exist_ok=True)
        qfile = path("out", name, "queued.json")
        try:
            fd = os.open(qfile, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
        except FileExistsError:
            queued.append(name)  # submitted earlier: collect it below
            continue
        prompt, negative = RECON + d.get("describe", ""), NEGATIVE
        if d.get("kind") == "normal":
            prompt, negative = NORMAL_PROMPT, NORMAL_NEGATIVE
        if p["wraps"] and d.get("kind") in ("material", "sky", "other", "normal"):
            prompt += " It is a seamless repeating texture: keep it tileable."
        try:
            # Qwen re-renders a Lanczos enlargement of the original at the target size, at strength 0.9 (the
            # player's suggestion; strength_test.py, x_dirtrocks1: structure 0.97 against 0.30 for strength 1.0 from
            # the small original, which redrew textures, e.g. rock piles on bare cracked ground).
            os.makedirs(path("base"), exist_ok=True)
            base = path("base", name + ".png")
            Image.open(path("src", name + ".png")).convert("RGBA").resize((p["tw"], p["th"]), Image.LANCZOS).save(base)
            job = medialab.submit("qwen-image-21", [base], prompt, negative,
                                  {"seed": 0, "image_steps": 25, "image_cfg": 2, "qwen_edit_strength": STRENGTH,
                                   "qwen_output_size": "source"})
        except BaseException:
            os.close(fd)
            os.remove(qfile)
            raise
        with os.fdopen(fd, "w") as f:
            json.dump({"id": job["id"]}, f)
        queued.append(name)
        print(f"{name}: queued {job['id']}", flush=True)
    for name in queued:
        out = path("out", name)
        if os.path.exists(os.path.join(out, "take-01_00001_.png")):
            continue  # collected by another copy
        try:
            job_id = json.load(open(os.path.join(out, "queued.json")))["id"]
        except (OSError, ValueError, KeyError):
            continue  # another copy is still writing it
        job = medialab.wait(job_id)
        if job["status"] not in ("complete", "partial"):
            os.remove(os.path.join(out, "queued.json"))  # the next run submits it again
            print(f"{name}: {job['status']} {job.get('error', '')}", flush=True)
            continue
        medialab.download(job, out)
        with open(os.path.join(out, "job.json"), "w") as f:
            json.dump(job, f, indent=1)
        print(f"{name}: done", flush=True)


def _smooth_wrap(v, sigma):
    """1-D Gaussian blur along axis 0 of an (n, 3) array, wrapping (the edge it runs along also tiles)."""
    r = max(1, int(3 * sigma))
    k = np.exp(-0.5 * (np.arange(-r, r + 1) / sigma) ** 2)
    k /= k.sum()
    pad = np.concatenate([v[-r:], v, v[:r]])
    return np.stack([np.convolve(pad[:, c], k, mode="valid") for c in range(v.shape[1])], axis=1)


def normalise_lighting(rgb, base, sigma):
    """Lighting normalisation (the player, 2026-10-11: "it's darkening some images a lot ... ensure the output has
    similar lighting, colours can vary"): Qwen renders came out darker (72 of 257 more than 15 % below the original's
    mean luminance; x_dirtrocks1 at half). The output's luminance above about two original pixels is replaced by the
    original's, below that Qwen's detail stays. The correction is ADDED to all three channels: Qwen's colour
    differences (hue, chroma) are kept as they are. Scaling by the luminance ratio instead turned a darkened orange
    into a vivid one (x_dirtrocks1). (Replaces the full-colour lock, which also pinned the colours.)"""
    import judge
    w = np.array([0.2126, 0.7152, 0.0722])
    y = rgb @ w
    yb = base @ w
    delta = judge.blur(yb, sigma) - judge.blur(y, sigma)
    return rgb + delta[..., None]


def normalise_saturation(rgb, base, sigma):
    """Saturation normalisation (the player, 2026-10-11: "it's desaturating the images quite a bit"): each pixel's
    chroma (its colour minus its luminance) is scaled so the broad-scale chroma strength matches the original's;
    hue and fine colour detail stay Qwen's. The scale is limited to 0.5..3 so a near-grey area cannot be blown up."""
    import judge
    w = np.array([0.2126, 0.7152, 0.0722])
    c = rgb - (rgb @ w)[..., None]
    cb = base - (base @ w)[..., None]
    mag = judge.blur(np.sqrt((c * c).sum(-1)), sigma)
    magb = judge.blur(np.sqrt((cb * cb).sum(-1)), sigma)
    k = np.clip((magb + 1.0) / (mag + 1.0), 0.5, 3.0)
    return rgb - c + c * k[..., None]


def tileable(img, band=0.12):
    """Makes opposite edges agree without importing content: the per-row (per-column) difference between the
    first and last column (row) is split and faded in from each edge. Only the broad part of the difference (blurred
    along the edge) is faded over band x size; the pixel-scale rest fades within a few pixels. Fading the raw
    difference over the whole band drew it out into streaks across the texture (B_T_level1_water, 2026-10-11).
    (An offset blend with a half-shifted copy moved a keg's skull into the corners: rejected.)"""
    out = img.copy()
    for axis in (1, 0, 1, 0):  # twice: each pass shifts the other pair of edges near the corners
        n = out.shape[axis]
        first = np.take(out, 0, axis=axis)  # (m, 3): the edge line
        last = np.take(out, n - 1, axis=axis)
        d = first - last  # make first -> mid, last -> mid
        lo = _smooth_wrap(d, max(1.0, out.shape[1 - axis] / 64))
        for part, k in ((lo, max(2, int(band * n))), (d - lo, max(2, n // 256))):
            ramp = 0.5 * (1 - np.arange(k) / k)  # 0.5 at the edge -> 0 inside
            if axis == 1:
                out[:, :k] -= part[:, None, :] * ramp[None, :, None]
                out[:, n - k:] += part[:, None, :] * ramp[::-1][None, :, None]
            else:
                out[:k] -= part[None, :, :] * ramp[:, None, None]
                out[n - k:] += part[None, :, :] * ramp[::-1][:, None, None]
    return out


def bleed(rgb, a, iters=24):
    """Colour for weakly opaque pixels from the nearby fully opaque ones (Qwen's own alpha), so the original alpha's
    soft edges never show the generator's background colour (purple fringes)."""
    m = (a > 200).astype(np.float64)[..., None]
    if m.min() == 1.0:
        return rgb
    c, w = rgb * m, m.copy()
    known = m[..., 0] > 0
    out = rgb.copy()
    for _ in range(iters):
        c = (c + np.roll(c, 1, 0) + np.roll(c, -1, 0) + np.roll(c, 1, 1) + np.roll(c, -1, 1)) / 5
        w = (w + np.roll(w, 1, 0) + np.roll(w, -1, 0) + np.roll(w, 1, 1) + np.roll(w, -1, 1)) / 5
    fill = c / np.maximum(w, 1e-6)
    out[~known] = fill[~known]
    return out


def post():
    plan = load_plan()
    for d in OVERRIDES:
        os.makedirs(d, exist_ok=True)
    n = 0
    for name, p in plan.items():
        done = path("out", name, "take-01_00001_.png")
        if not os.path.exists(done) or p.get("rejected") or name in HAND_TUNED + TEXT_SKIP:
            continue
        out = Image.open(done).convert("RGBA")
        src = Image.open(path("src", name + ".png")).convert("RGBA")
        from strength_test import structure
        st = structure(src, out)
        p["structure"] = round(st, 3)
        if st < MIN_STRUCTURE:
            p["rejected"] = f"structure {st:.2f} < {MIN_STRUCTURE}: a different picture"
            print(f"{name}: rejected, structure {st:.2f}", flush=True)
            continue
        rgb = np.asarray(out)[..., :3].astype(np.float64)
        if p["alpha"]:
            rgb = bleed(rgb, np.asarray(out)[..., 3])
        kind = json.load(open(path("desc", name + ".json"))).get("kind", "other")
        import judge
        s = (2.0 if kind in ("material", "sky", "other", "normal") else 3.0) * out.width / src.width
        base = np.asarray(src.convert("RGB").resize(out.size, Image.BICUBIC)).astype(np.float64)
        if kind == "normal":
            # Normal maps: the full colour is the slope, so all three channels keep the original's broad values.
            rgb = np.stack([rgb[..., c] - judge.blur(rgb[..., c], s) + judge.blur(base[..., c], s) for c in range(3)],
                           axis=-1)
        else:
            rgb = normalise_saturation(normalise_lighting(rgb, base, s), base, 4.0 * s)
        if p["wraps"] and kind in ("material", "sky", "other", "normal"):  # icons and sprites never wrap
            rgb = tileable(rgb)
        if kind == "normal":  # tangent-space normal map: unit vectors again after the colour lock and edge blend
            n = rgb / 127.5 - 1
            xy = n[..., :2]
            ln = np.sqrt((xy ** 2).sum(-1, keepdims=True))
            xy = xy * np.minimum(1.0, 0.98 / np.maximum(ln, 1e-6))
            z = np.sqrt(np.clip(1 - (xy ** 2).sum(-1, keepdims=True), 0, 1))
            rgb = (np.concatenate([xy, z], -1) + 1) * 127.5
        if p["alpha"]:
            a = np.asarray(src.getchannel("A").resize(out.size, Image.LANCZOS))
        else:
            a = np.full(rgb.shape[:2], 255, np.uint8)
        res = Image.fromarray(np.dstack([np.clip(rgb + 0.5, 0, 255).astype(np.uint8), a]), "RGBA")
        res.save(path("out", name, "final.png"))
        stored = res.transpose(Image.FLIP_TOP_BOTTOM)
        for d in OVERRIDES:
            stored.save(os.path.join(d, name + ".png"))
        p["installed"] = True
        n += 1
    save_plan(plan)
    print(f"{n} textures installed")


def sheet(k):
    plan = load_plan()
    names = [n for n, p in plan.items() if p.get("installed")]
    os.makedirs(SHARE, exist_ok=True)
    cell = 300
    for s in range(0, len(names), k):
        group = names[s:s + k]
        cols = 4
        rows = (len(group) + cols - 1) // cols
        img = Image.new("RGB", (cols * (2 * cell + 20), rows * (cell + 24)), (30, 30, 30))
        d = ImageDraw.Draw(img)
        for i, name in enumerate(group):
            x, y = (i % cols) * (2 * cell + 20), (i // cols) * (cell + 24)
            for j, (pth, f) in enumerate([(path("src", name + ".png"), Image.NEAREST),
                                          (path("out", name, "final.png"), Image.LANCZOS)]):
                im = Image.open(pth).convert("RGBA")
                r = cell / max(im.size)
                im = im.resize((max(1, int(im.width * r)), max(1, int(im.height * r))), f)
                bg = Image.new("RGBA", im.size, (110, 110, 110, 255))
                bg.alpha_composite(im)
                img.paste(bg.convert("RGB"), (x + j * cell, y + 20))
            p = plan[name]
            d.text((x + 4, y + 4), f"{name}  {p['w']}x{p['h']} -> {p['tw']}x{p['th']}", fill="white")
        img.save(os.path.join(SHARE, f"sheet_{s // k + 1:02d}.png"))
    print(f"{len(names)} textures on {(len(names) + k - 1) // k} sheets in {SHARE}")


if __name__ == "__main__":
    cmd = sys.argv[1]
    if cmd == "prepare":
        prepare(int(sys.argv[2]))
    elif cmd == "run":
        run()
    elif cmd == "post":
        post()
    elif cmd == "sheet":
        sheet(int(sys.argv[2]) if len(sys.argv) > 2 else 24)
