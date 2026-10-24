"""Side-by-side images of the original game's screens (tools/shot.py captures, work/shots/) and the remaster
engine's (playtest shots, work/remaster/play/), for checking the UI against the original.

Usage:
  python tools/ui_compare.py            -> work/remaster/play/compare/<name>.png and index.html
"""
import html
import os
import sys

from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SHOTS = os.path.join(ROOT, "work", "shots")
PLAY = os.path.join(ROOT, "work", "remaster", "play")
OUT = os.path.join(PLAY, "compare")

# (name, original capture, remaster capture)
PAIRS = [
    ("main_menu", "orig_ui_menu-800.png", "menus.main.png"),
    ("options", "orig_ui_opts-950.png", "menus.options.png"),
    ("graphics", "orig_ui_gfx-1140.png", "menus.gfx.png"),
    ("audio", "orig_ui_sfx-1120.png", "menus.sfx.png"),
    ("controls", "orig_ui_controls-1100.png", "menus.controls.png"),
    ("extras", "orig_ui_extras-950.png", "menus.extras.png"),
    ("briefing", "orig_ui_play-480.png", "menus.briefing.png"),
    ("pause", "orig_ui_play-720.png", "menus.pause.png"),
    ("hud", "../remaster/play/original_hud.png", "run14.04_objective.png"),
]


def main():
    os.makedirs(OUT, exist_ok=True)
    rows = []
    h = 540
    for name, a, b in PAIRS:
        pa, pb = os.path.join(SHOTS, a), os.path.join(PLAY, b)
        if not (os.path.exists(pa) and os.path.exists(pb)):
            print(f"{name}: missing {pa if not os.path.exists(pa) else pb}")
            continue
        ia, ib = Image.open(pa).convert("RGB"), Image.open(pb).convert("RGB")
        ia = ia.resize((round(ia.width * h / ia.height), h))
        ib = ib.resize((round(ib.width * h / ib.height), h))
        out = Image.new("RGB", (ia.width + ib.width + 10, h + 30), (20, 20, 20))
        out.paste(ia, (0, 30))
        out.paste(ib, (ia.width + 10, 30))
        d = ImageDraw.Draw(out)
        d.text((8, 8), f"original (640x480): {a}", fill=(255, 255, 255))
        d.text((ia.width + 18, 8), f"remaster engine: {b}", fill=(255, 255, 255))
        path = os.path.join(OUT, f"{name}.png")
        out.save(path)
        rows.append(f"<h2>{html.escape(name)}</h2><img src='{name}.png' style='max-width:100%'>")
        print(f"{name}: {path}")
    with open(os.path.join(OUT, "index.html"), "w", encoding="utf-8") as f:
        f.write("<!doctype html><meta charset=utf-8><title>UI comparison</title><body style='background:#111;color:#eee;font-family:sans-serif'>"
                + "".join(rows) + "</body>")
    return 0


if __name__ == "__main__":
    sys.exit(main())
