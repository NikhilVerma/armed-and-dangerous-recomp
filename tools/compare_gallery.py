"""Before / after gallery: the original Armed and Dangerous next to the Rust / Bevy remaster, same camera, same moment.

Usage:
  python tools/compare_gallery.py [--only NAME,...] [--skip-original] [--skip-remaster] [--compose-only]
                                  [--exe target-wt-shots] [--list]

Reads tools/compare_gallery.json (shot definitions). For each shot:
1. The original game (orig/pc/GameData, every recovered function off: the shipped code, D3D9) runs off-screen and
   muted in its own game instance (tools/instances.py "gallery"; [test] offscreen, tools/compare.py) with the shot's
   scripted keys counted from the briefing ([test] keys_from_play, as tools/oracle_diff.py) and dumps the frame
   `frame` play frames in, with its camera (hook/src/testshot.cpp: and-frame-N.bmp + and-frame-N.camera.json).
2. The remaster runs headless (--background, muted) on a playtest script with the same keys (engine/src/oracle.rs
   "oracle" section), held at the original's dumped camera (--camera, engine/src/camdump.rs), and takes its shot at
   the same play frame (a "shot" step after the briefing + play_delay + frame).
   "after" uses the JSON's `remaster` settings (preset + --graphics changes); a shot with "before": "preset" also
   renders the engine's Original preset (a fallback "before", labelled as such).
3. Composes the pair: the original's 3D strip (letterbox from its camera dump) and the remaster at the same size,
   an optional crop (fractions of the strip), side by side or stacked, with small corner labels, as JPEG under the
   size budget into docs/images/compare/<name>.jpg. Raw captures stay in work/gallery/<name>/ (never committed).

The hero image (docs/images/preview.jpg) is the shot named by "hero" in the JSON, composed as a diagonal split.
Captures are cached: --skip-original / --skip-remaster / --compose-only reuse what work/gallery holds.
"""
import argparse
import glob
import json
import os
import shutil
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import compare  # noqa: E402
import instances  # noqa: E402
from PIL import Image, ImageDraw, ImageFont  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WORK = os.path.join(ROOT, "work", "gallery")
OUT = os.path.join(ROOT, "docs", "images", "compare")


# --- the original -------------------------------------------------------------------------------------------------

def briefing_frame(level, cfg):
    """Frames from the level load to its briefing in the original (deterministic: virtual clock). From the JSON's
    table, else from an earlier oracle run's log (tools/oracle_diff.py: 'briefing at N frames after the load')."""
    table = cfg.get("briefing_frames", {})
    if level in table:
        return table[level]
    import re
    for log in glob.glob(os.path.join(ROOT, "work", "oracle", "*", "original.log")):
        if os.path.basename(os.path.dirname(log)) not in (f"{level}_start", f"sf_{level}_start"):
            continue
        m = re.search(r"briefing at (\d+) frames after the load", open(log, encoding="latin1").read())
        if m:
            return int(m.group(1))
    sys.exit(f"{level}: briefing frame unknown (run tools/oracle_diff.py {level}_start once, or add it to "
             "briefing_frames in the JSON)")


def capture_original(shot, cfg, d):
    instances.wait_while_player_plays()
    gd = instances.instance_dir("gallery")
    for f in glob.glob(os.path.join(gd, "and-frame-*")) + [os.path.join(gd, "and.log")]:
        if os.path.exists(f):
            os.remove(f)
    delay = cfg.get("play_delay", 30)
    frame = briefing_frame(shot["level"], cfg) + delay + shot["frame"]
    w, h = shot.get("original_size", cfg["original_size"])
    test = {"keys_from_play": 1, "play_delay": delay, "play_frames": shot["frame"] + 10, "reseed_on_load": 1,
            "hash_from_load": 1, "hash_frames": f"{frame}:1:1", "dump_hashed": 1, "d3d_trace": 0,
            "offscreen": 1, "parallel": 1, "exit_frame": 0}
    if shot.get("warp"):
        test["play_warp"] = ",".join(str(v) for v in shot["warp"])
    if shot.get("hold_health"):
        test["play_hold_health"] = 1
    # the shipped code, except the recovered Camera_SetupProjection 0x0040f6e0 (S2, verified equal by
    # tools/compare.py): its projection callback is where the hook records the camera it dumps
    hooks = {n: n == "Camera_SetupProjection" for n in compare.hook_names()}
    compare.write_ini(hooks, shot["level"], 0, shot.get("keys", ""), test, None,
                      {"display": {"width": w, "height": h}}, gd)
    opt = os.path.join(gd, "SavedGames", "options.opt")
    if os.path.exists(opt):
        os.remove(opt)
    t = time.time()
    proc = compare.launch(gd)
    try:
        proc.wait(timeout=600)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()
        print("  original: timed out")
    bmp = os.path.join(gd, f"and-frame-{frame:05d}.bmp")
    cam = os.path.join(gd, f"and-frame-{frame:05d}.camera.json")
    if not os.path.exists(bmp):
        print(f"  original: no frame {frame} ({os.path.join(gd, 'and.log')})")
        return False
    Image.open(bmp).convert("RGB").save(os.path.join(d, "original.png"))
    shutil.copy2(cam, os.path.join(d, "original.camera.json"))
    print(f"  original: frame {frame} in {time.time() - t:.0f} s")
    return True


# --- the remaster -------------------------------------------------------------------------------------------------

def capture_remaster(shot, cfg, d, exe_dir, variant):
    """variant: "remaster" (the JSON's remaster settings) or "original_preset"."""
    delay = cfg.get("play_delay", 30)
    # the shot at the same play frame as the original's dump (oracle.rs `shots`), then a few frames to save it
    oracle = {"keys": shot.get("keys", ""), "frames": shot["frame"] + 15, "trace_every": 0, "play_delay": delay,
              "shots": [shot["frame"]]}
    if shot.get("warp"):
        oracle["warp"] = shot["warp"]
    if shot.get("hold_health"):
        oracle["hold_health"] = True
    script = os.path.join(d, f"{variant}.json")
    json.dump({"oracle": oracle}, open(script, "w"), indent=1)
    png = os.path.join(d, f"{variant}.oracle-{shot['frame']}.png")
    if os.path.exists(png):
        os.remove(png)
    cam = os.path.join(d, "original.camera.json")
    # widescreen: the remaster's own follow camera at 16:9 (Hor+, no letterbox) next to the original's full 4:3 frame
    w, h = cfg.get("widescreen_size", [1920, 1080]) if shot.get("widescreen") else strip_size(cfg, shot)
    args = [os.path.join(ROOT, "engine", exe_dir, "release", "and_remaster.exe"), "--background", "--size", f"{w}x{h}",
            "--play", script, "--movies", os.path.join(d, "no-movies"),
            "--scene", os.path.join(ROOT, "work", "remaster", shot["level"])]
    if not shot.get("widescreen"):
        args += ["--camera", cam]
    if variant == "remaster":
        r = cfg["remaster"]
        args += ["--preset", r["preset"], "--graphics", graphics_arg(r["graphics"])]
    else:
        args += ["--preset", "original"]
    args += shot.get("engine_args", [])
    instances.wait_while_player_plays()
    t = time.time()
    p = subprocess.run(args, cwd=os.path.join(ROOT, "engine"), capture_output=True, text=True, errors="replace",
                       timeout=900, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    open(os.path.join(d, f"{variant}.stdout.txt"), "w", encoding="utf-8").write(p.stdout + p.stderr)
    if not os.path.exists(png):
        print(f"  remaster ({variant}): no shot (exit {p.returncode}; {os.path.join(d, variant + '.stdout.txt')})")
        return False
    shutil.copy2(png, os.path.join(d, f"{variant}.png"))
    print(f"  remaster ({variant}): {time.time() - t:.0f} s")
    return True


def graphics_arg(changes):
    """--graphics key=value,...: values as JSON (true, 8192), strings bare (taa). The engine resolves these before its
    log starts, so a value of the wrong type ("True") or an unknown key would be dropped without a word
    (graphics/mod.rs resolve): JSON spelling only."""
    return ",".join(f"{k}={v if isinstance(v, str) else json.dumps(v)}" for k, v in changes.items())


def strip_size(cfg, shot):
    """The original's 3D strip at its capture size (letterbox 0.1125 / 0.775 of the height: 640x372 at 640x480)."""
    w, h = shot.get("original_size", cfg["original_size"])
    return w, round(h * 0.775)


# --- composition --------------------------------------------------------------------------------------------------

def original_strip(d):
    im = Image.open(os.path.join(d, "original.png")).convert("RGB")
    cam = json.load(open(os.path.join(d, "original.camera.json")))
    top, height = cam.get("letterbox", [0.1125, 0.775])
    y0 = round(im.height * top)
    return im.crop((0, y0, im.width, y0 + round(im.height * height)))


def crop(im, frac):
    if not frac:
        return im
    x0, y0, x1, y1 = frac
    return im.crop((round(x0 * im.width), round(y0 * im.height), round(x1 * im.width), round(y1 * im.height)))


def font(px):
    for name in ("segoeuib.ttf", "arialbd.ttf", "DejaVuSans-Bold.ttf"):
        try:
            return ImageFont.truetype(name, px)
        except OSError:
            pass
    return ImageFont.load_default()


def label(im, text, corner="tl"):
    """A small translucent tag in a corner."""
    im = im.convert("RGBA")
    px = max(14, im.height // 30)
    f = font(px)
    dr = ImageDraw.Draw(im)
    tw, th = dr.textbbox((0, 0), text, font=f)[2:]
    pad = px // 2
    x = pad if corner[1] == "l" else im.width - tw - 3 * pad
    y = pad if corner[0] == "t" else im.height - th - 3 * pad
    over = Image.new("RGBA", im.size, (0, 0, 0, 0))
    od = ImageDraw.Draw(over)
    od.rounded_rectangle((x, y, x + tw + 2 * pad, y + th + 2 * pad), radius=pad // 2, fill=(0, 0, 0, 140))
    od.text((x + pad, y + pad - th // 6), text, font=f, fill=(255, 255, 255, 235))
    return Image.alpha_composite(im, over).convert("RGB")


def save_jpeg(im, path, budget):
    for q in range(90, 40, -4):
        im.save(path, "JPEG", quality=q, optimize=True, progressive=True, subsampling="4:2:0")
        if os.path.getsize(path) <= budget:
            return q
    return q


def compose_widescreen(shot, cfg, d):
    """The original's whole 4:3 frame (letterbox bars included) next to the remaster's 16:9 frame, same height."""
    before = Image.open(os.path.join(d, "original.png")).convert("RGB")
    after = Image.open(os.path.join(d, "remaster.png")).convert("RGB")
    h = cfg.get("widescreen_height", 540)
    before = label(before.resize((round(before.width * h / before.height), h), Image.LANCZOS), "Original (2003), 4:3", "tl")
    after = label(after.resize((round(after.width * h / after.height), h), Image.LANCZOS), "Remaster, 16:9 Hor+", "tl")
    gap = 4
    out = Image.new("RGB", (before.width + gap + after.width, h), (16, 16, 16))
    out.paste(before, (0, 0))
    out.paste(after, (before.width + gap, 0))
    return out


def compose(shot, cfg, d):
    gap = 4
    if shot.get("widescreen"):
        out = compose_widescreen(shot, cfg, d)
        return save_pair(shot, cfg, out)
    before = original_strip(d)
    after_name = "remaster.png"
    after = Image.open(os.path.join(d, after_name)).convert("RGB")
    before_label = "Original (2003)"
    if shot.get("before") == "preset":
        before = Image.open(os.path.join(d, "original_preset.png")).convert("RGB")
        before_label = "Original look (engine preset)"
    after = after.resize(before.size, Image.LANCZOS) if after.size != before.size else after
    before, after = crop(before, shot.get("crop")), crop(after, shot.get("crop"))
    layout = shot.get("layout", cfg.get("layout", "side"))
    if layout == "side":
        cell_w = (cfg.get("side_width", 1920) - gap) // 2
    else:
        cell_w = cfg.get("stack_width", 1280)
    cell_h = round(before.height * cell_w / before.width)
    before = label(before.resize((cell_w, cell_h), Image.LANCZOS), before_label, "tl")
    after = label(after.resize((cell_w, cell_h), Image.LANCZOS), "Remaster", "tl")
    if layout == "side":
        out = Image.new("RGB", (2 * cell_w + gap, cell_h), (16, 16, 16))
        out.paste(before, (0, 0))
        out.paste(after, (cell_w + gap, 0))
    else:
        out = Image.new("RGB", (cell_w, 2 * cell_h + gap), (16, 16, 16))
        out.paste(before, (0, 0))
        out.paste(after, (0, cell_h + gap))
    save_pair(shot, cfg, out)


def save_pair(shot, cfg, out):
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, f"{shot['name']}.jpg")
    q = save_jpeg(out, path, cfg.get("budget_kb", 400) * 1024)
    print(f"  {os.path.relpath(path, ROOT)}: {out.width}x{out.height}, q{q}, {os.path.getsize(path) // 1024} KB")


def compose_hero(shot, cfg, d):
    """A diagonal split: the original on the left, the remaster on the right, at the hero width."""
    before = original_strip(d)
    after = Image.open(os.path.join(d, "remaster.png")).convert("RGB").resize(before.size, Image.LANCZOS)
    before, after = crop(before, shot.get("crop")), crop(after, shot.get("crop"))
    w = cfg.get("hero_width", 1600)
    h = round(before.height * w / before.width)
    before, after = before.resize((w, h), Image.LANCZOS), after.resize((w, h), Image.LANCZOS)
    mask = Image.new("L", (w, h), 0)
    md = ImageDraw.Draw(mask)
    skew = h * 0.18
    md.polygon([(w / 2 + skew, 0), (w, 0), (w, h), (w / 2 - skew, h)], fill=255)
    out = Image.composite(after, before, mask)
    ImageDraw.Draw(out).line([(w / 2 + skew, 0), (w / 2 - skew, h)], fill=(240, 240, 240), width=3)
    out = label(out, "Original (2003)", "bl")
    out = label(out, "Remaster", "br")
    path = os.path.join(ROOT, "docs", "images", "preview.jpg")
    q = save_jpeg(out, path, cfg.get("budget_kb", 400) * 1024)
    print(f"  {os.path.relpath(path, ROOT)}: {w}x{h}, q{q}, {os.path.getsize(path) // 1024} KB")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--config", default=os.path.join(ROOT, "tools", "compare_gallery.json"))
    ap.add_argument("--only", default="")
    ap.add_argument("--exe", default="target-wt-shots", help="cargo target dir under engine/ (never \"target\")")
    ap.add_argument("--skip-original", action="store_true")
    ap.add_argument("--skip-remaster", action="store_true")
    ap.add_argument("--compose-only", action="store_true")
    ap.add_argument("--list", action="store_true")
    a = ap.parse_args()
    cfg = json.load(open(a.config))
    only = [n for n in a.only.split(",") if n]
    shots = [s for s in cfg["shots"] if not only or s["name"] in only]
    if a.list:
        for s in cfg["shots"]:
            print(f"{s['name']:24} {s['level']:12} frame {s['frame']:5}  {s.get('caption', '')[:70]}")
        return
    for s in shots:
        print(f"[{s['name']}]")
        d = os.path.join(WORK, s["name"])
        os.makedirs(d, exist_ok=True)
        if not (a.compose_only or a.skip_original) and not capture_original(s, cfg, d):
            continue
        if not (a.compose_only or a.skip_remaster):
            if not capture_remaster(s, cfg, d, a.exe, "remaster"):
                continue
            if s.get("before") == "preset" and not capture_remaster(s, cfg, d, a.exe, "original_preset"):
                continue
        if not os.path.exists(os.path.join(d, "remaster.png")):
            print("  no remaster capture yet: not composed")
            continue
        compose(s, cfg, d)
        if s["name"] == cfg.get("hero"):
            compose_hero(s, cfg, d)


if __name__ == "__main__":
    main()
