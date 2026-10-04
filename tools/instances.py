"""Game instances for running test scenarios in parallel.

Each instance is work/instances/<n>/, a mirror of orig/pc/GameData made of hard links (no copies of the game
data), so several games can run at once: and.ini, and.log, traces, frame dumps and SavedGames are written per
instance. The hook DLL and its symbols are linked afresh every time, so instances always run the deployed build.

Usage (library): instance_dir(n) -> path, ready to run.
       python tools/instances.py [count]   # prepare count instances (default: CPU count / 4, at least 2)
"""
import os
import shutil
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAME_DIR = os.path.join(ROOT, "orig", "pc", "GameData")
BASE = os.path.join(ROOT, "work", "instances")
# Written by the game or the harness: never shared between instances.
PRIVATE = {"and.ini", "and.log", "and-d3d.txt", "and-coverage.txt", "and-profile.txt", "and-input.log",
           "and-vk-scene.txt", "and-vk-scene.obj", "and-vk-draws.txt", "and-vk-targets.txt", "and-crash.dmp",
           "and-rhi-calls.txt"}
PRIVATE_DIRS = {"SavedGames", "shaders"}
RELINK = {"dinput8.dll", "dinput8.pdb"}  # the deployed build


def _link(src, dst):
    if os.path.lexists(dst):
        os.remove(dst)
    try:
        os.link(src, dst)
    except OSError:
        shutil.copy2(src, dst)  # another volume: copy


def instance_dir(n):
    """Prepares instance n (idempotent) and returns its directory."""
    dst_root = os.path.join(BASE, str(n))
    for dirpath, dirnames, filenames in os.walk(GAME_DIR):
        rel = os.path.relpath(dirpath, GAME_DIR)
        top = rel.split(os.sep)[0]
        if top in PRIVATE_DIRS:
            dirnames[:] = []
            continue
        out = os.path.join(dst_root, rel) if rel != "." else dst_root
        os.makedirs(out, exist_ok=True)
        for f in filenames:
            if rel == "." and (f in PRIVATE or f.startswith("and-frame-") or f.startswith("and-")):
                continue
            src, dst = os.path.join(dirpath, f), os.path.join(out, f)
            if f in RELINK or not os.path.exists(dst):
                _link(src, dst)
    os.makedirs(os.path.join(dst_root, "SavedGames"), exist_ok=True)
    return dst_root


def default_count():
    return max(2, (os.cpu_count() or 8) // 4)


if __name__ == "__main__":
    count = int(sys.argv[1]) if len(sys.argv) > 1 else default_count()
    for n in range(count):
        print(instance_dir(n))
