"""Launch the game working copy with the hook, take screenshots of its window, then kill it.

Usage:
  python archive/tools/run.py --set display.windowed=1 --set display.width=1920 --set display.height=1080 \
                      --shots 15,30 --kill 35 [--name res1080] [--args "..."]

- Edits orig/pc/GameData/and.ini (section.key=value) before launch.
- Screenshots go to work/shots/<name>-<seconds>s.png (the game window's client area only).
- Prints the tail of and.log at the end. Exit code 1 if the game died before the kill timer.
"""
import argparse
import configparser
import ctypes
import ctypes.wintypes as wt
import os
import subprocess
import sys
import time

import mss
import mss.tools

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # the repository (this tool lives in archive/tools)
GAME_DIR = os.path.join(ROOT, "orig", "pc", "GameData")
EXE = os.path.join(GAME_DIR, "game.unpacked.exe")
INI = os.path.join(GAME_DIR, "and.ini")
SHOTS = os.path.join(ROOT, "work", "shots")

user32 = ctypes.windll.user32


def set_ini(pairs):
    cp = configparser.ConfigParser()
    cp.optionxform = str
    cp.read(INI)
    for p in pairs:
        key, value = p.split("=", 1)
        section, name = key.split(".", 1)
        if not cp.has_section(section):
            cp.add_section(section)
        cp.set(section, name, value)
    with open(INI, "w") as f:
        cp.write(f)


def window_of(pid):
    found = []

    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def cb(hwnd, _):
        p = wt.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(p))
        if p.value == pid and user32.IsWindowVisible(hwnd):
            found.append(hwnd)
        return True

    user32.EnumWindows(cb, 0)
    return found[0] if found else None


def client_rect_on_screen(hwnd):
    r = wt.RECT()
    user32.GetClientRect(hwnd, ctypes.byref(r))
    pt = wt.POINT(0, 0)
    user32.ClientToScreen(hwnd, ctypes.byref(pt))
    return {"left": pt.x, "top": pt.y, "width": r.right - r.left, "height": r.bottom - r.top}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--set", action="append", default=[])
    ap.add_argument("--shots", default="20")
    ap.add_argument("--kill", type=float, default=30)
    ap.add_argument("--name", default="run")
    ap.add_argument("--args", default="")
    ap.add_argument("--desktop", dest="backbuffer", action="store_false",
                    help="grab the window from the desktop instead of the game's back buffer")
    ap.add_argument("--keep-options", action="store_true",
                    help="keep SavedGames/options.opt (default: remove it so every run starts from defaults)")
    a = ap.parse_args()

    opt = os.path.join(GAME_DIR, "SavedGames", "options.opt")
    if not a.keep_options and os.path.exists(opt):
        os.remove(opt)
    for old in os.listdir(GAME_DIR):
        if old.startswith("and-shot-"):
            os.remove(os.path.join(GAME_DIR, old))
    if a.backbuffer:
        a.set.append(f"test.shots={a.shots}")
    else:
        a.set.append("test.shots=")

    ctypes.windll.shcore.SetProcessDpiAwareness(2)  # real pixel coordinates on scaled desktops
    set_ini(a.set)
    os.makedirs(SHOTS, exist_ok=True)
    proc = subprocess.Popen([EXE] + a.args.split(), cwd=GAME_DIR)
    start = time.time()
    shots = [] if a.backbuffer else sorted(float(s) for s in a.shots.split(",") if s)
    died = False
    with mss.MSS() if hasattr(mss, "MSS") else mss.mss() as sct:
        while time.time() - start < a.kill:
            if proc.poll() is not None:
                died = True
                break
            if shots and time.time() - start >= shots[0]:
                t = shots.pop(0)
                hwnd = window_of(proc.pid)
                if hwnd:
                    user32.SetForegroundWindow(hwnd)
                    time.sleep(0.3)
                    rect = client_rect_on_screen(hwnd)
                    path = os.path.join(SHOTS, f"{a.name}-{int(t)}s.png")
                    img = sct.grab(rect)
                    mss.tools.to_png(img.rgb, img.size, output=path)
                    print(f"shot {path} {rect['width']}x{rect['height']}")
                else:
                    print(f"shot at {t}s: no visible window")
            time.sleep(0.2)
    if not died:
        proc.kill()
        proc.wait()
    print(f"{'DIED' if died else 'killed'} after {time.time() - start:.1f}s, exit code {proc.returncode}")
    for f in sorted(os.listdir(GAME_DIR)):
        if f.startswith("and-shot-") and f.endswith(".bmp"):
            from PIL import Image
            dest = os.path.join(SHOTS, f"{a.name}-{f[len('and-shot-'):-4]}.png")
            Image.open(os.path.join(GAME_DIR, f)).convert("RGB").save(dest)
            print(f"shot {dest}")
    log = os.path.join(GAME_DIR, "and.log")
    if os.path.exists(log):
        lines = open(log, encoding="latin1").read().splitlines()
        print("--- and.log (last 25)")
        print("\n".join(lines[-25:]))
    sys.exit(1 if died else 0)


if __name__ == "__main__":
    main()
