"""Before / after videos: Roman walking forwards from each level's start, in the original game and in the remaster.

Usage:
  python tools/compare_videos.py [--only LEVEL,...] [--skip-original] [--skip-remaster] [--encode-only]
                                 [--exe target-wt-shots] [--list]

Reads tools/compare_videos.json (the levels, the input, the clip). For each level:
1. The original game (orig/pc/GameData, the shipped code: every recovered function off but Camera_SetupProjection,
   as tools/compare_gallery.py) runs off-screen and muted in its own game instance ("videos", tools/instances.py) with
   the scripted keys counted from the briefing, and dumps every `step`-th play frame of the clip as a BMP with its
   camera ([test] hash_frames first:every:count, dump_hashed: hook/src/testshot.cpp).
2. The remaster runs headless (--background, muted) on the same keys (engine/src/oracle.rs "oracle" section, a
   screenshot every `step`-th play frame), with the JSON's settings (the gallery's: tools/compare_gallery.json
   `remaster`), its own follow camera (the original's moves with Roman too; both start behind him at the level start).
3. Each frame pair is composed side by side (the original's 3D strip, the letterbox cut as in the gallery; labels as
   the gallery's) and ffmpeg encodes work/videos/<level>.mp4 (H.264, `fps`, `width` wide, scaled down until the
   file is under `max_mb`) and the two raw clips work/videos/<level>_original.mp4 / <level>_remaster.mp4. The frame
   dumps are deleted after encoding. Nothing here is committed: the clips go to a release.

Levels whose Roman starts in a turret (the mission's start_in_turret_tag: the base defence levels) are not in the set:
he cannot walk there.
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
import compare_gallery as gallery  # noqa: E402
import instances  # noqa: E402
from PIL import Image  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "work", "videos")
CONFIG = os.path.join(ROOT, "tools", "compare_videos.json")


def frames_of(cfg):
    """The play frames of the clip: `start`, every `step`, for `seconds` at the game's 60 frames a second."""
    n = int(cfg["seconds"] * cfg["fps"])
    return [cfg["start"] + i * cfg["step"] for i in range(n)]


def capture_original(level, cfg, d):
    instances.wait_while_player_plays()
    gd = instances.instance_dir("videos")
    for f in glob.glob(os.path.join(gd, "and-frame-*")) + glob.glob(os.path.join(gd, "and.log")):
        os.remove(f)
    delay = cfg["play_delay"]
    brief = gallery.briefing_frame(level["level"], cfg)
    frames = frames_of(cfg)
    first = brief + delay + frames[0]
    w, h = cfg["original_size"]
    test = {"keys_from_play": 1, "play_delay": delay, "play_frames": frames[-1] + 10, "reseed_on_load": 1,
            "hash_from_load": 1, "hash_frames": f"{first}:{cfg['step']}:{len(frames)}", "dump_hashed": 1, "d3d_trace": 0,
            "offscreen": 1, "parallel": 1, "exit_frame": 0}
    hooks = {n: n == "Camera_SetupProjection" for n in compare.hook_names()}
    compare.write_ini(hooks, level["level"], 0, level.get("keys", cfg["keys"]), test, None, {"display": {"width": w, "height": h}}, gd)
    opt = os.path.join(gd, "SavedGames", "options.opt")
    if os.path.exists(opt):
        os.remove(opt)
    t = time.time()
    proc = compare.launch(gd)
    try:
        proc.wait(timeout=1800)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()
        print("  original: timed out")
    got = 0
    os.makedirs(os.path.join(d, "original"), exist_ok=True)
    for i, f in enumerate(frames):
        bmp = os.path.join(gd, f"and-frame-{brief + delay + f:05d}.bmp")
        cam = os.path.join(gd, f"and-frame-{brief + delay + f:05d}.camera.json")
        if not os.path.exists(bmp):
            continue
        im = Image.open(bmp).convert("RGB")
        top, height = json.load(open(cam)).get("letterbox", [0.1125, 0.775]) if os.path.exists(cam) else (0.1125, 0.775)
        y0 = round(im.height * top)
        im.crop((0, y0, im.width, y0 + round(im.height * height))).save(os.path.join(d, "original", f"{i:04d}.png"))
        os.remove(bmp)
        got += 1
    for f in glob.glob(os.path.join(gd, "and-frame-*")):
        os.remove(f)
    print(f"  original: {got}/{len(frames)} frames in {time.time() - t:.0f} s")
    return got == len(frames)


def capture_remaster(level, cfg, d, exe_dir):
    frames = frames_of(cfg)
    oracle = {"keys": level.get("keys", cfg["keys"]), "frames": frames[-1] + 60, "trace_every": 0,
              "play_delay": cfg["play_delay"], "shots": frames}
    script = os.path.join(d, "remaster.json")
    json.dump({"oracle": oracle}, open(script, "w"), indent=1)
    for f in glob.glob(os.path.join(d, "remaster.oracle-*.png")):
        os.remove(f)
    w, h = cfg["remaster_size"]
    r = cfg["remaster"]
    args = [os.path.join(ROOT, "engine", exe_dir, "release", "and_remaster.exe"), "--background", "--size", f"{w}x{h}",
            "--play", script, "--movies", os.path.join(d, "no-movies"),
            "--scene", os.path.join(ROOT, "work", "remaster", level["level"]),
            "--preset", r["preset"], "--graphics", gallery.graphics_arg(r["graphics"])]
    instances.wait_while_player_plays()
    t = time.time()
    p = subprocess.run(args, cwd=os.path.join(ROOT, "engine"), capture_output=True, text=True, errors="replace",
                       timeout=1800, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    open(os.path.join(d, "remaster.stdout.txt"), "w", encoding="utf-8").write(p.stdout + p.stderr)
    os.makedirs(os.path.join(d, "remaster"), exist_ok=True)
    got = 0
    for i, f in enumerate(frames):
        png = os.path.join(d, f"remaster.oracle-{f}.png")
        if os.path.exists(png):
            shutil.move(png, os.path.join(d, "remaster", f"{i:04d}.png"))
            got += 1
    print(f"  remaster: {got}/{len(frames)} frames in {time.time() - t:.0f} s (exit {p.returncode})")
    return got == len(frames)


def ffmpeg(args):
    return subprocess.run(["ffmpeg", "-y", "-loglevel", "error", *args], capture_output=True, text=True)


def encode(src_pattern, out, cfg, width):
    """H.264 at the JSON's frame rate, scaled to `width`; bit rate lowered until the file fits max_mb."""
    rate = cfg["bitrate_kbps"]
    for _ in range(4):
        r = ffmpeg(["-framerate", str(cfg["fps"]), "-i", src_pattern, "-vf", f"scale={width}:-2:flags=lanczos",
                    "-c:v", "libx264", "-preset", "slow", "-b:v", f"{rate}k", "-maxrate", f"{int(rate * 1.5)}k",
                    "-bufsize", f"{rate * 2}k", "-pix_fmt", "yuv420p", "-movflags", "+faststart", out])
        if r.returncode:
            print("  ffmpeg:", r.stderr[-500:])
            return False
        if os.path.getsize(out) <= cfg["max_mb"] * 1024 * 1024:
            return True
        rate = int(rate * 0.7)
    return True


def compose(level, cfg, d):
    """Side-by-side frames (original left, remaster right, the gallery's labels) -> the three MP4s."""
    n = len(frames_of(cfg))
    side = os.path.join(d, "side")
    os.makedirs(side, exist_ok=True)
    half = (cfg["width"] - 4) // 2
    for i in range(n):
        a = Image.open(os.path.join(d, "original", f"{i:04d}.png")).convert("RGB")
        b = Image.open(os.path.join(d, "remaster", f"{i:04d}.png")).convert("RGB")
        h = round(a.height * half / a.width) // 2 * 2
        a = gallery.label(a.resize((half, h), Image.LANCZOS), "Original (2003)", "tl")
        b = gallery.label(b.resize((half, h), Image.LANCZOS), "Remaster", "tl")
        out = Image.new("RGB", (cfg["width"], h), (16, 16, 16))
        out.paste(a, (0, 0))
        out.paste(b, (cfg["width"] - half, 0))
        out.save(os.path.join(side, f"{i:04d}.png"))
    name = level["level"]
    ok = encode(os.path.join(side, "%04d.png"), os.path.join(OUT, f"{name}.mp4"), cfg, cfg["width"])
    ok &= encode(os.path.join(d, "original", "%04d.png"), os.path.join(OUT, f"{name}_original.mp4"), cfg, half)
    ok &= encode(os.path.join(d, "remaster", "%04d.png"), os.path.join(OUT, f"{name}_remaster.mp4"), cfg, half)
    for f in ("side",):
        shutil.rmtree(os.path.join(d, f), ignore_errors=True)
    sizes = ", ".join(f"{os.path.basename(p)} {os.path.getsize(p) / 1048576:.1f} MB" for p in
                      (os.path.join(OUT, f"{name}{s}.mp4") for s in ("", "_original", "_remaster")) if os.path.exists(p))
    print(f"  {sizes}")
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--config", default=CONFIG)
    ap.add_argument("--only", default="")
    ap.add_argument("--exe", default="target-wt-shots", help="cargo target dir under engine/ (never \"target\")")
    ap.add_argument("--skip-original", action="store_true")
    ap.add_argument("--skip-remaster", action="store_true")
    ap.add_argument("--encode-only", action="store_true")
    ap.add_argument("--keep-frames", action="store_true", help="keep the PNG frames in work/videos/frames/<level>")
    ap.add_argument("--list", action="store_true")
    a = ap.parse_args()
    cfg = json.load(open(a.config))
    cfg["remaster"] = json.load(open(os.path.join(ROOT, "tools", "compare_gallery.json")))["remaster"]
    only = [n for n in a.only.split(",") if n]
    levels = [lv for lv in cfg["levels"] if not only or lv["level"] in only]
    if a.list:
        for lv in cfg["levels"]:
            print(f"{lv['level']:14} {lv.get('keys', cfg['keys'])}  {lv.get('note', '')}")
        for s in cfg.get("skipped", []):
            print(f"{s['level']:14} skipped: {s['why']}")
        return
    os.makedirs(OUT, exist_ok=True)
    for lv in levels:
        print(f"[{lv['level']}]")
        d = os.path.join(OUT, "frames", lv["level"])
        os.makedirs(d, exist_ok=True)
        # the original very occasionally crashes on exit before its last dumps (and.log CRASH 0xc0000005): once more
        if not (a.encode_only or a.skip_original) and not (capture_original(lv, cfg, d) or capture_original(lv, cfg, d)):
            print("  original: incomplete, not encoded")
            continue
        if not (a.encode_only or a.skip_remaster) and not capture_remaster(lv, cfg, d, a.exe):
            print("  remaster: incomplete, not encoded")
            continue
        if compose(lv, cfg, d) and not a.keep_frames:
            shutil.rmtree(d, ignore_errors=True)


if __name__ == "__main__":
    main()
