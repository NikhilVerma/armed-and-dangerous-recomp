"""Bake every level's environment cube map (engine/src/graphics/env_map.rs): the original renders the scene into a
cube from the level's env map marker once, when the level starts (EnvMapFreq 0); the remaster renders it once here,
per level and preset, into work/remaster/<level>/env_map_<preset>.png (3 x 2 faces), which the level then loads.
Levels without a marker are skipped by the engine (nothing written).

Usage:
  python tools/bake_env_maps.py [LEVEL ...] [--exe target-wt-NAME] [--presets remaster,original] [--force]

Runs the engine off-screen and muted (--background), one level at a time, and waits while the player's own game runs
(tools/instances.py). Takes ~10 s a level and preset. Re-run after a re-export or a change to what the cube shows.
"""
import argparse
import glob
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import instances  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LEVELS = os.path.join(ROOT, "work", "remaster")


def has_marker(level_dir):
    """Whether the level places an env map marker (the export's scene logic; the engine decides for sure)."""
    try:
        return "envmapmarker" in open(os.path.join(level_dir, "scene.json"), encoding="utf-8").read()
    except OSError:
        return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("levels", nargs="*")
    ap.add_argument("--exe", default="target-wt-shots", help="cargo target dir under engine/ (never \"target\")")
    ap.add_argument("--presets", default="remaster,original")
    ap.add_argument("--force", action="store_true", help="bake again where a file exists")
    a = ap.parse_args()
    exe = os.path.join(ROOT, "engine", a.exe, "release", "and_remaster.exe")
    levels = a.levels or sorted(os.path.basename(d) for d in glob.glob(os.path.join(LEVELS, "lev_*")) if os.path.isfile(os.path.join(d, "scene.json")))
    for level in levels:
        d = os.path.join(LEVELS, level)
        if not has_marker(d):
            print(f"{level}: no env map marker")
            continue
        for preset in a.presets.split(","):
            out = os.path.join(d, f"env_map_{preset}.png")
            if os.path.exists(out) and not a.force:
                print(f"{level} {preset}: baked already")
                continue
            if os.path.exists(out):
                os.remove(out)
            instances.wait_while_player_plays()
            p = subprocess.run([exe, "--background", "--no-ai", "--preset", preset, "--scene", d, "--bake-env-map"],
                               cwd=os.path.join(ROOT, "engine"), capture_output=True, text=True, errors="replace", timeout=600,
                               creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
            ok = os.path.exists(out)
            print(f"{level} {preset}: {'baked ' + str(os.path.getsize(out) // 1024) + ' KB' if ok else 'FAILED (exit ' + str(p.returncode) + ')'}")
            if not ok:
                print("\n".join(line for line in (p.stdout + p.stderr).splitlines() if "env map" in line or "ERROR" in line or "panicked" in line)[-2000:])


if __name__ == "__main__":
    main()
