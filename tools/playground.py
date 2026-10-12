"""Graphics playground: render a controlled scene in a real level and compare variants side by side.

A scene file (JSON, examples in tools/scenes/) says where the camera is, what to spawn, kill or explode and when, and
which frames to save. Each variant runs the same scene with different settings (textures on/off, a shader switch, a
shutter speed ...). Frames count from the end of the level load, so every run repeats the same moments.
The hook side is hook/enhance/playground.cpp ([playground] in and.ini).

  python tools/playground.py tools/scenes/grunt.json
  python tools/playground.py tools/scenes/grunt.json --variant textures_on    one variant only
  python tools/playground.py --find lev_1_m1                                 walk forward, log the player's position

Scene keys:
  level          lev_1_m1 ...
  skip_briefing  1 (default): dismiss the mission-start pause; 2: every message pause; 0: none
  camera         [x, y, z, yaw, pitch] (degrees, Z up)   or  camera_look [x, y, z, tx, ty, tz]
  camera_orbit   {"spawn": 1, "radius": 10, "eye_z": 6, "target_z": 4.5, "start": 0, "speed": 1.5}
                 turntable around a spawned object (degrees per frame)
  fov            vertical degrees (0 = the game's)
  player         "hide" or [x, y, z]
  spawn          [{"template": "grunt", "at": [x, y, z], "yaw": 0, "frame": 5}, ...]
  kill           [{"spawn": 1, "frame": 120}, ...]          (spawn numbers start at 1)
  fx             [{"name": "fx_explosion_barracks", "at": [x, y, z], "frame": 60}, ...]
  lights         {"sun": [az, el, r, g, b], "ambient": [r, g, b], "key": [az, el, r, g, b],
                  "point": [x, y, z, r, g, b, halfrange], "point_follow": 1, "isolate": 1}
                 fake lights through the game's own light slots (degrees; colours 0..n)
  shots          frames to save, e.g. [60, 90, 120]       or  dump "first:every:count"
  keys           scripted input, frames from the load ("10:17*120" = hold forward 120 frames)
  resolution     [1920, 1080] (default)
  settings       {"section.key": value} for every variant
  variants       {"name": {"section.key": value}, ...}
Output: work/playground/<scene>/<variant>/*.png and work/playground/<scene>/sheet.png (rows = variants).
"""
import argparse
import json
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOAD_FRAMES = 700  # start-up and level load before the scene's frame 0 (lev_1_m1 loads in about 450)


def playground_sets(scene):
    s = {"playground.enabled": 1, "playground.skip_briefing": scene.get("skip_briefing", 1)}
    if "camera" in scene:
        s["playground.camera"] = ",".join(str(v) for v in scene["camera"])
    if "camera_look" in scene:
        s["playground.camera_look"] = ",".join(str(v) for v in scene["camera_look"])
    if "camera_orbit" in scene:
        o = scene["camera_orbit"]
        s["playground.camera_orbit"] = ",".join(str(o.get(k, d)) for k, d in
                                                (("spawn", 1), ("radius", 10), ("eye_z", 6), ("target_z", 4.5),
                                                 ("start", 0), ("speed", 0)))
    if scene.get("fov"):
        s["playground.camera_fov"] = scene["fov"]
    p = scene.get("player")
    if p:
        s["playground.player"] = p if isinstance(p, str) else ",".join(str(v) for v in p)
    for i, sp in enumerate(scene.get("spawn", []), 1):
        x, y, z = sp["at"]
        s[f"playground.spawn{i}"] = f"{sp['template']},{x},{y},{z},{sp.get('yaw', 0)},{sp.get('frame', 1)}"
    for i, k in enumerate(scene.get("kill", []), 1):
        s[f"playground.kill{i}"] = f"{k['spawn']},{k['frame']}"
    for i, f in enumerate(scene.get("fx", []), 1):
        x, y, z = f["at"]
        s[f"playground.fx{i}"] = f"{f['name']},{x},{y},{z},{f.get('frame', 1)}"
    lights = scene.get("lights", {})
    for k in ("sun", "ambient", "key", "point"):
        if k in lights:
            s[f"playground.{k}"] = ",".join(str(v) for v in lights[k])
    for k in ("point_follow", "isolate"):
        if k in lights:
            s[f"playground.{k}"] = int(lights[k])
    if scene.get("log_spawn"):
        s["playground.log_spawn"] = scene["log_spawn"]
    if scene.get("log_player"):
        s["playground.log_player"] = 1
    return s


def dump_spec(scene):
    if "dump" in scene:
        return scene["dump"]
    shots = sorted(scene.get("shots", [120]))
    if len(shots) == 1:
        return f"{shots[0]}:1:1"
    step = shots[1] - shots[0]
    if all(b - a == step for a, b in zip(shots, shots[1:])):
        return f"{shots[0]}:{step}:{len(shots)}"
    # Irregular: every gcd-th frame of the range, the asked ones kept below. Saving every frame (a 1080p image each)
    # slowed a run to 10 fps and was mistaken for a game stall (2026-10-12).
    from functools import reduce
    from math import gcd
    g = reduce(gcd, [b - a for a, b in zip(shots, shots[1:])])
    return f"{shots[0]}:{g}:{(shots[-1] - shots[0]) // g + 1}"


def run_variant(scene, name, extra):
    sets = dict(playground_sets(scene))
    sets.update(scene.get("settings", {}))
    sets.update(extra)
    res = scene.get("resolution", [1920, 1080])
    sets.setdefault("display.width", res[0])
    sets.setdefault("display.height", res[1])
    sets["test.hash_from_load"] = 1
    sets["test.keys_from_load"] = 1
    last = max(scene.get("shots", [120]) + [int(x) for x in scene.get("dump", "0:0:0").split(":")[:1]])
    frames = LOAD_FRAMES + last + 30
    capture = f"pg_{scene['name']}_{name}"
    cmd = [sys.executable, os.path.join(ROOT, "tools", "visual_capture.py"), "--name", capture,
           "--level", scene.get("level", "lev_1_m1"), "--frames", str(frames), "--dump", dump_spec(scene),
           "--keys", scene.get("keys", "")]
    for k, v in sets.items():
        cmd += ["--set", f"{k}={v}"]
    print(f"[{name}] {' '.join(cmd[2:])}")
    subprocess.run(cmd, check=False)
    src = os.path.join(ROOT, "work", "visual", capture)
    dst = os.path.join(ROOT, "work", "playground", scene["name"], name)
    os.makedirs(dst, exist_ok=True)
    kept = []
    if os.path.isdir(src):
        for f in sorted(os.listdir(src)):
            path = os.path.join(src, f)
            if f == "and.log":
                os.replace(path, os.path.join(dst, f))
                continue
            stem = os.path.splitext(f)[0]
            digits = "".join(c for c in stem if c.isdigit())
            if "shots" in scene and digits and int(digits) not in scene["shots"]:
                continue
            out = os.path.join(dst, stem + ".png")
            from PIL import Image
            Image.open(path).convert("RGB").save(out)
            kept.append(out)
    log = os.path.join(dst, "and.log")
    if os.path.exists(log):
        for line in open(log, encoding="utf-8", errors="replace"):
            if "playground:" in line:
                print("   ", line.rstrip())
    return kept


def contact_sheet(scene, results):
    from PIL import Image, ImageDraw
    rows = [(n, imgs) for n, imgs in results.items() if imgs]
    if not rows:
        return None
    cols = max(len(imgs) for _, imgs in rows)
    w = 960
    first = Image.open(rows[0][1][0])
    h = round(first.height * w / first.width)
    sheet = Image.new("RGB", (cols * w, len(rows) * (h + 28)), (20, 20, 20))
    d = ImageDraw.Draw(sheet)
    for r, (name, imgs) in enumerate(rows):
        for c, p in enumerate(imgs):
            sheet.paste(Image.open(p).resize((w, h), Image.LANCZOS), (c * w, r * (h + 28) + 28))
            d.text((c * w + 6, r * (h + 28) + 6), f"{name}  {os.path.basename(p)}", fill=(230, 230, 230))
    out = os.path.join(ROOT, "work", "playground", scene["name"], "sheet.png")
    sheet.save(out)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("scene", nargs="?")
    ap.add_argument("--variant", action="append", default=[])
    ap.add_argument("--find", metavar="LEVEL", help="walk forward and log the player's position")
    a = ap.parse_args()
    if a.find:
        scene = {"name": "find", "level": a.find, "log_player": 1, "keys": "30:17*300",
                 "dump": "60:60:6", "variants": {"walk": {}}}
    else:
        scene = json.load(open(a.scene, encoding="utf-8"))
        scene.setdefault("name", os.path.splitext(os.path.basename(a.scene))[0])
    variants = scene.get("variants") or {"default": {}}
    results = {}
    for name, extra in variants.items():
        if a.variant and name not in a.variant:
            continue
        results[name] = run_variant(scene, name, extra)
    sheet = contact_sheet(scene, results)
    print(f"sheet: {sheet}")


if __name__ == "__main__":
    main()
