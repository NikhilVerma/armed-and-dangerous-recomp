"""Export the player's animation table to the remaster engine (game data: lives under work/, never committed).

  python tools/remaster_export_player_anims.py [lev_1_m1] [--out DIR]  -> work/remaster/<level>/player_anims.json

Reads, from orig/pc/GameData/game.unpacked.exe:
- the mode table 0x005cb650 (0x28-byte entries, index mode * 7 + state; type +0 0 = "link to state +4 of this mode";
  +4 name prefix, +8 slot flags (1 loop, 0x40 backwards), +0xc rate block, +0x10 kind), chosen and started by
  0x00475ff0 (src/game/game_05.cpp);
- the cross-fade times 0x005cb3a8 (13 x 13 floats, [previous kind][kind] in seconds, AnimSlot_Set's weight);
- which rate block each locomotion entry points at (the blocks are filled at load by PlayerObject_Load 0x00473da0 from
  p_trotter's anim_run_* / anim_walk_* / anim_side_* keys: those values are in player.json `movement.
  anim_speed_tables`; the directions are the load's constants, written here).

Output: {"modes": [{"mode", "state", "name", "type", "flags", "kind", "rate"} ...], "links": {"mode,state": state},
"blend": [[13 floats] x 13], "rate_dirs": {block: [x, y, z]}}. Model space: game axes, the model faces -Y, +X is its
left.
"""
import argparse
import json
import math
import pathlib
import struct

ROOT = pathlib.Path(__file__).resolve().parents[1]
MAIN = ROOT if (ROOT / "orig").exists() else ROOT.parents[2]   # a worktree reads the main checkout's orig/ and work/
EXE = MAIN / "orig" / "pc" / "GameData" / "game.unpacked.exe"
BASE = 0x400000
MODE_TABLE, BLEND_TABLE = 0x005cb650, 0x005cb3a8
MODES = 41           # modes 0..40 (0x29 is the death mode, outside the table)
# the rate blocks (PlayerObject_Load 0x00473da0, game_05.cpp:660-707) and their directions after the load
BLOCKS = {0x005feab4: "run_fwd", 0x005fea8c: "run_back", 0x005feb54: "walk_fwd", 0x005feb2c: "walk_back",
          0x005fea64: "side_run_left", 0x005fea3c: "side_run_right", 0x005feb04: "side_walk_left",
          0x005feadc: "side_walk_right"}


def side_dirs():
    # PlayerAnimTable_Normalize (game_05.cpp:516): a = (1, -0.5, 0) / sqrt(1.25); b = (-1, -0.5, 0) / sqrt(1.25)
    k = 1.0 / math.sqrt(1.25)
    return [k, -0.5 * k, 0.0], [-k, -0.5 * k, 0.0]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("level", nargs="?", default="lev_1_m1")
    ap.add_argument("--out", help="level folder (default work/remaster/<level>)")
    a = ap.parse_args()
    level = a.level
    import pefile
    img = pefile.PE(str(EXE)).get_memory_mapped_image()
    u32 = lambda va: struct.unpack_from("<I", img, va - BASE)[0]
    f32 = lambda va: struct.unpack_from("<f", img, va - BASE)[0]

    def cstr(va):
        end = img.find(b"\0", va - BASE)
        return img[va - BASE:end].decode("latin1")

    modes, links = [], {}
    for m in range(MODES):
        for s in range(7):
            e = MODE_TABLE + (m * 7 + s) * 0x28
            typ = u32(e)
            if typ == 0:
                links[f"{m},{s}"] = u32(e + 4)
                continue
            modes.append({"mode": m, "state": s, "name": cstr(u32(e + 4)).lower(), "type": typ, "flags": u32(e + 8),
                          "kind": u32(e + 0x10), "rate": BLOCKS.get(u32(e + 0xc))})
    blend = [[round(f32(BLEND_TABLE + (r * 13 + c) * 4), 6) for c in range(13)] for r in range(13)]
    left, right = side_dirs()
    dirs = {"run_fwd": [0.0, -1.0, 0.0], "run_back": [0.0, 1.0, 0.0], "walk_fwd": [0.0, -1.0, 0.0],
            "walk_back": [0.0, 1.0, 0.0], "side_run_left": left, "side_run_right": right, "side_walk_left": left,
            "side_walk_right": right}
    out = {"_doc": "the player's anim table 0x005cb650, cross-fade times 0x005cb3a8 [previous kind][kind], rate block "
                   "directions (tools/remaster_export_player_anims.py)", "modes": modes, "links": links, "blend": blend, "rate_dirs": dirs}
    path = (pathlib.Path(a.out) if a.out else MAIN / "work" / "remaster" / level) / "player_anims.json"
    path.write_text(json.dumps(out, indent=1))
    print(f"{path}: {len(modes)} entries, {len(links)} links")


if __name__ == "__main__":
    main()
