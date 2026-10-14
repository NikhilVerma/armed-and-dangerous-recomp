"""Packs the input prompt icons the remaster engine uses into one atlas.

Source: Kenney "Input Prompts" 1.5 (CC0, www.kenney.nl), unpacked under work/kenney_input_prompts/pack (not in
git). Output: <out>/icons.png (RGBA, 128 px cells) and <out>/icons.json ({"cell": 128, "width", "height",
"icons": {name: [x, y, w, h]}}), read by engine/src/hud.rs for `<icon=name>` tags in texts.

Usage: python tools/remaster_export_icons.py [--pack DIR] [--out DIR]
"""

import argparse
import json
import math
from pathlib import Path

from PIL import Image

# (folder, icon) pairs: the buttons input.rs names for prompts
ICONS = {
    "PlayStation Series": [
        "playstation_button_color_cross", "playstation_button_color_circle", "playstation_button_color_square",
        "playstation_button_color_triangle", "playstation_trigger_l1", "playstation_trigger_l2",
        "playstation_trigger_r1", "playstation_trigger_r2", "playstation5_button_options",
        "playstation5_button_create", "playstation_dpad_up", "playstation_dpad_down", "playstation_dpad_left",
        "playstation_dpad_right", "playstation_stick_l", "playstation_stick_r",
    ],
    "Xbox Series": [
        "xbox_button_color_a", "xbox_button_color_b", "xbox_button_color_x", "xbox_button_color_y", "xbox_lb",
        "xbox_lt", "xbox_rb", "xbox_rt", "xbox_button_menu", "xbox_button_view", "xbox_dpad_up", "xbox_dpad_down",
        "xbox_dpad_left", "xbox_dpad_right", "xbox_stick_l", "xbox_stick_r",
    ],
    "Keyboard & Mouse": [
        "keyboard_x", "keyboard_f", "keyboard_space", "keyboard_enter", "keyboard_escape", "keyboard_r",
        "keyboard_q", "keyboard_tab", "keyboard_z", "keyboard_e", "keyboard_1", "keyboard_3", "keyboard_w",
        "keyboard_a", "keyboard_s", "keyboard_d", "mouse_left", "mouse_right", "mouse_move",
    ],
}


def main():
    root = Path(__file__).resolve().parent.parent
    ap = argparse.ArgumentParser()
    ap.add_argument("--pack", default=root / "work/kenney_input_prompts/pack")
    ap.add_argument("--out", default=root / "work/remaster/lev_1_m1/ui")
    a = ap.parse_args()
    pack, out = Path(a.pack), Path(a.out)
    found = []
    for folder, names in ICONS.items():
        for n in names:
            p = pack / folder / "Double" / f"{n}.png"
            if p.exists():
                found.append((n, p))
            else:
                print(f"missing: {folder}/{n}")
    cell = 128
    cols = 8
    rows = math.ceil(len(found) / cols)
    atlas = Image.new("RGBA", (cols * cell, rows * cell), (0, 0, 0, 0))
    rects = {}
    for i, (n, p) in enumerate(found):
        im = Image.open(p).convert("RGBA")
        if im.size != (cell, cell):
            im = im.resize((cell, cell), Image.LANCZOS)
        x, y = (i % cols) * cell, (i // cols) * cell
        atlas.paste(im, (x, y))
        rects[n] = [x, y, cell, cell]
    out.mkdir(parents=True, exist_ok=True)
    atlas.save(out / "icons.png")
    (out / "icons.json").write_text(json.dumps({"cell": cell, "width": atlas.width, "height": atlas.height, "icons": rects}, indent=1))
    print(f"{len(found)} icons -> {out / 'icons.png'} ({atlas.width}x{atlas.height})")


if __name__ == "__main__":
    main()
