"""Export one level's in-game UI (HUD, menus, font, subtitles) for the remaster engine (engine/).

  python tools/remaster_export_ui.py lev_1_m1          -> work/remaster/lev_1_m1/ui/

How the original builds and draws this UI: docs/subsystems/hud.md. Formats: docs/formats/big-ui.md,
big-font.md, big-anim.md, big-skin.md, big-txtr.md, big-text.md.

Where the data comes from:
  big12_ui    pages, refs, objects, texts, the element instances (page, ref, nav ids, text scale and colour),
              movie subtitles. It holds no positions (0x004629b0 Hud_CreateLevelUi).
  big03_anim  the page anchor objects (pui_main, pui_pub, ...): each is an anim whose nodes are the `ref_*`
              layout points, in 640x480 pixels, origin bottom left, y up (0x00464b10 reads node m[3], m[7] and
              divides by 640 / 480).
  big07_obj   template of each UI object -> its model (LOD 0) and the page objects' anchor anim.
  big05_skin  UI meshes: quads in 640x480 pixels around the object's origin (0x00464e20 scales by 1/640, 1/480).
  big02_txtr  the meshes' textures and the font atlas `mainfontid`.
  big13_font  glyph metrics. big01_text: the display strings.
  game.unpacked.exe (optional, read only): the caption/speaker table 0x005cadd8 (colour, size, position).

Output (game data: lives under work/, never committed):
  layout.json          space, pages (anchor points), elements, subtitles; schema in docs/subsystems/hud.md
  font.json            BMFont-like metrics: char code -> x, y, width, height, xoffset, yoffset, xadvance
  font.png             the glyph atlas (mainfontid, 256x512)
  textures/<name>.png  every texture a UI mesh uses (mip 0, RGBA; DXT2 premultiplied alpha is un-premultiplied)
  texts.json           every display string of the text bank, id -> text
  ui_bank.json         the raw ui bank dump (tools/formats/big_kinds/ui.py) for reference
"""
import argparse
import io
import json
import pathlib
import struct
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "formats"))
sys.path.insert(0, str(ROOT / "tools"))
from big_kinds import font as fontkind, obj, objchunk, text as textkind, txtr, ui as uikind  # noqa: E402
import remaster_export as rx  # noqa: E402  (payload(), mesh_submeshes(), the texture converters)

GAME = ROOT / "orig" / "pc" / "GameData"
EXE = GAME / "game.unpacked.exe"
DXT1, DXT2, DXT3 = rx.DXT1, rx.DXT2, rx.DXT3
A8R8G8B8, R5G6B5 = rx.A8R8G8B8, rx.R5G6B5

# What each named HUD element shows, and the code that drives it (docs/subsystems/hud.md has the detail).
SHOWS = {
    "ui_ammo_count": ("current primary weapon ammo: '%d/%d' (clip/reserve), '%d/' + ui_ammo_infinity for weapon type 2",
                      "0x00465a20"),
    "ui_grenade_count": ("current special weapon (grenade) count '%d' (guess: 0x004a1380 = count)", "0x00465ad0"),
    "ui_ammo_infinity": ("infinity sign after the ammo count when the primary weapon type is 2", "0x00463fa0"),
    "ui_weapon_bkd": ("weapon/grenade panel background, shown with a player", "0x00463fa0"),
    "ui_player_rifle": ("primary weapon icon (one shown: the current weapon index)", "0x00463fa0"),
    "ui_player_sniper": ("primary weapon icon", "0x00463fa0"),
    "ui_player_mg": ("primary weapon icon", "0x00463fa0"),
    "ui_player_rlgurner": ("primary weapon icon", "0x00463fa0"),
    "ui_player_sharkgun": ("primary weapon icon", "0x00463fa0"),
    "ui_player_rlvindaloo": ("primary weapon icon", "0x00463fa0"),
    "ui_player_grenades": ("special weapon icon (sticky bomb; 'on01' lock frame animated for special 10)", "0x00463fa0"),
    "ui_player_guyfawkes": ("special weapon icon", "0x00463fa0"),
    "ui_player_bunchoffives": ("special weapon icon", "0x00463fa0"),
    "ui_player_blackhole": ("special weapon icon", "0x00463fa0"),
    "ui_player_topsy": ("special weapon icon", "0x00463fa0"),
    "ui_select_up_on": ("weapon cycle arrow, lit while the button is held (more than one special)", "0x00463fa0"),
    "ui_select_down_on": ("weapon cycle arrow, lit while held (more than one special)", "0x00463fa0"),
    "ui_select_left_on": ("weapon cycle arrow, lit while held (more than one weapon)", "0x00463fa0"),
    "ui_select_right_on": ("weapon cycle arrow, lit while held (more than one weapon)", "0x00463fa0"),
    "ui_select_up_off": ("weapon cycle arrow (idle), shown with more than one special", "0x00463fa0"),
    "ui_select_down_off": ("weapon cycle arrow (idle), shown with more than one special", "0x00463fa0"),
    "ui_select_left_off": ("weapon cycle arrow (idle), shown with more than one weapon", "0x00463fa0"),
    "ui_select_right_off": ("weapon cycle arrow (idle), shown with more than one weapon", "0x00463fa0"),
    "ui_player_health": ("player health: texture-scroll meter, health/max of the player", "0x00466e70"),
    "ui_health_enemy": ("boss/enemy health meter (selector 1: *(0x00408020(g+0xa6c)) +0x2c)", "0x00466e70"),
    "ui_health_q": ("Q's health meter (player +0xad4)", "0x00466e70"),
    "ui_health_jonesy": ("Jonesy's health meter (player +0xad0)", "0x00466e70"),
    "ui_compass": ("compass dial: animation frame = camera heading", "0x00466700"),
    "ui_compass_objective": ("objective pointer arrow on the compass, fades with distance", "0x00466790"),
    "ui_crosshair": ("default crosshair (per-weapon variants below; _in/_out anims on target)", "0x00406f50"),
    "ui_crosshair_friendly": ("crosshair over a friendly", "0x00406f50"),
    "ui_autoaim": ("auto-aim marker", "0x00406f50"),
    "ui_multi_lock": ("lock-on marker (instanced 4x; world tracked, list g+0xa84 +0xebc)", "0x00407c20"),
    "ui_hit_top": ("damage direction flash (top), shown for g+0xcb8 s on a hit from that side", "0x004670f0"),
    "ui_hit_bottom": ("damage direction flash (bottom)", "0x004670f0"),
    "ui_hit_left": ("damage direction flash (left)", "0x004670f0"),
    "ui_hit_right": ("damage direction flash (right)", "0x004670f0"),
    "ui_clock": ("ticker bomb clock (plays when a bomb is armed)", "0x0044e4a0"),
    "ui_tickerbomb": ("ticker bomb icon", "0x0044e3a0"),
    "ui_tickerbomb_set": ("ticker bomb arming progress (slide meter)", "0x004672d0"),
    "ui_text_bkd": ("box behind the mission briefing / pause text (sized to the text)", "0x00466470"),
    "ui_peasant": ("peasant icon on a rescue level (g+0x26ec > 0; behaviour id 129, vtable 0x005a5180 slot 47)", "0x00465d30"),
    "ui_flag_base": ("flag base marker (multiplayer-style mode)", "0x00465d50 (guess)"),
    "ui_mouse_cursor": ("menu mouse cursor (drawn last, ui +0x54)", "0x00463ac0"),
    "ui_mission_briefing": ("mission objectives (pause menu): title, objectives, 'Not Complete'/'Complete'", ""),
    "ui_pause_text": ("pause screen text", ""),
    "ui_mission_timer": ("mission countdown/elapsed timer 'm:ss'", "0x00465b70"),
    # (behaviour ids 105 / 106 / 107, vtables 0x005ad9c8 / 0x005aae90 / 0x005a8288, slot 47; shown while
    # peasant_max_saved, g+0x26ec, > 0)
    "ui_captured_count": ("peasants still to pick up '%d' (counts on list +0xef8 - g+0x26f4)", "0x00465be0"),
    "ui_safe_count": ("peasants saved '%d' (g+0x26e8)", "0x00465c90"),
    "ui_return_count": ("peasants carried '%d' (g+0x26f4)", "0x00465ce0"),
    "ui_grunt_over_wall_count": ("grunts-over-the-wall counter", ""),
    "ui_chat_01": ("message line 1 (Chat: word-wrapped at 490 units, 6 s per line, colour table)", "0x0046a820"),
    "ui_chat_02": ("message line 2", "0x0046a820"),
    "ui_chat_03": ("message line 3", "0x0046a820"),
    "ui_chat_04": ("message line 4 (not filled by Chat, which uses 3 lines)", ""),
    "ui_chat_05": ("message line 5 (not filled by Chat)", ""),
    "ui_teammate_defend_player": ("teammate order message, 3 s", "0x00467890"),
    "ui_teammate_defend_loc": ("teammate order message, 3 s", "0x00467890"),
    "ui_teammate_attack": ("teammate order message, 3 s", "0x00467890"),
    "ui_teammate_return": ("teammate order message, 3 s", "0x00467890"),
    "ui_tickerbomb_hint": ("prompt near an unarmed ticker bomb", "0x00467450"),
    "ui_tickerbomb_patience": ("ticker bomb 'patience' prompt", "0x0044e3a0"),
    "ui_tickerbomb_none": ("no ticker bombs prompt", "0x00467450"),
    "ui_vehicle_entry": ("'press to enter' vehicle prompt (fit to 576 units)", "0x004662a0 (guess)"),
    "ui_vehicle_exit": ("vehicle exit prompt", "0x00466210 (guess)"),
    "ui_pub_entry_hint": ("pub entrance prompt", "0x00467b60"),
    "ui_pub_fail": ("pub entrance refused prompt", "0x00467b60"),
    "ui_bof_hint": ("bunch-of-fives hint", ""),
    "ui_memory_warning": ("memory warning (debug)", ""),
}

# The remaster's widescreen anchor per HUD layout reference (hook/enhance/ui_aspect.cpp HudAnchor). Others: by x.
ANCHOR = {
    "left": {"ref_health_enemy", "ref_health_jonesy", "ref_health_q", "ref_peasant", "ref_team_text",
             "ref_captured", "ref_safe", "ref_return", "ref_chat_01", "ref_chat_02", "ref_chat_03", "ref_chat_04",
             "ref_chat_05", "ref_health", "ref_health_pool"},
    "right": {"ref_weapon_bkd", "ref_weapon_select_up", "ref_weapon_select_down", "ref_weapon_select_left",
              "ref_weapon_select_right", "ref_compass", "ref_compass_objective", "ref_weapon", "ref_grenade",
              "ref_ammo_count", "ref_grenade_count"},
    "stretch": {"ref_hit_top", "ref_hit_bottom", "ref_hit_left", "ref_hit_right"},
}


def anchor_of(page, ref, x):
    if page != 0:
        return "centre"  # menu pages: pillarbox at 4:3
    for a, refs in ANCHOR.items():
        if ref in refs:
            return a
    if ref is None or x is None:
        return "centre"
    return "left" if x < 640 / 3 else "right" if x > 1280 / 3 else "centre"


def colour_hex(c):
    """0x00RRGGBB (alpha ignored: Text_Draw 0x00495e80 ORs 0xff000000) -> '#rrggbb'."""
    return f"#{c & 0xFFFFFF:06x}"


# ---------------------------------------------------------------- textures

def unpremultiply(img):
    from PIL import Image
    px = img.load()
    for y in range(img.height):
        for x in range(img.width):
            r, g, b, a = px[x, y]
            if 0 < a < 255:
                px[x, y] = (min(255, r * 255 // a), min(255, g * 255 // a), min(255, b * 255 // a), a)
    return img


def texture_png(t, path):
    """One txtr bank texture (mip 0) -> PNG. Returns {width, height, format, alpha, premultiplied}."""
    from PIL import Image
    fmt, w, h = t["format"], t["width"], t["height"]
    pixels, levels = t["pixels"], t["levels"]
    if fmt == R5G6B5:
        fmt, pixels, levels = A8R8G8B8, rx.convert_565(w, h, 1, pixels), 1
    if fmt not in (DXT1, DXT2, DXT3, A8R8G8B8):
        return None
    dds = txtr.dds_bytes(DXT3 if fmt == DXT2 else fmt, w, h, levels, pixels)
    # The bank stores rows bottom-up (the decoded DDS shows b_ad_image upside down). Flip so the PNG is upright:
    # mesh uv then read as OpenGL-style (v = 0 is the bottom row), and font.json pixel rects as top-left based.
    img = Image.open(io.BytesIO(dds)).convert("RGBA").transpose(Image.Transpose.FLIP_TOP_BOTTOM)
    if fmt == DXT2:
        img = unpremultiply(img)
    img.save(path)
    alpha = img.getextrema()[3][0] < 255
    return {"file": f"textures/{path.name}" if path.parent.name == "textures" else path.name, "width": w,
            "height": h, "format": txtr.format_name(t["format"]), "alpha": alpha,
            "premultiplied_in_bank": t["format"] == DXT2}


# ---------------------------------------------------------------- anchors (anim nodes)

def anim_nodes(chunk):
    """Node name -> rest position (x, y, z) of one anim chunk (docs/formats/big-anim.md 3.2, +0x1c)."""
    b, r = chunk.data, chunk.root
    out = {}
    for name, node, _ in objchunk.dict_items(b, objchunk.u32(b, r + 0x2C)):
        x, y, z = struct.unpack_from("<3f", b, node + 0x1C)
        keyed = objchunk.u32(b, node + 0x48) != 0
        out[name] = {"x": round(x, 3), "y": round(y, 3), "z": round(z, 3)}
        if keyed:
            out[name]["keyed"] = True
    return out


# ---------------------------------------------------------------- meshes

def mesh_parts(chunk, textures_used):
    """UI model -> parts: texture, colour, triangles in 640x480 pixels (x right, y up) around the object origin,
    bounds and uv rect. The whole model is drawn by 0x00464e20 with no per-part culling."""
    parts = []
    for sub in rx.mesh_submeshes(chunk):
        tex, vp, _vn, vt, idx, colour = sub[:6]  # later fields (skinning) are not needed here
        xs, ys = [v[0] for v in vp], [v[1] for v in vp]
        us, vs = [t[0] for t in vt], [t[1] for t in vt]
        # uv: OpenGL-style against the upright PNGs (v = 0 bottom row); the quads have v = 1 at their top edge.
        p = {"texture": tex, "colour": colour_hex(colour),
             "rect": [round(min(xs), 2), round(min(ys), 2), round(max(xs), 2), round(max(ys), 2)],
             "uv_rect": [round(min(us), 4), round(min(vs), 4), round(max(us), 4), round(max(vs), 4)],
             "z": round(sum(v[2] for v in vp) / len(vp), 3),
             "vertices": [[round(v[0], 3), round(v[1], 3), round(t[0], 5), round(t[1], 5)] for v, t in zip(vp, vt)],
             "triangles": [idx[i:i + 3] for i in range(0, len(idx), 3)]}
        if tex:
            textures_used.add(tex)
        parts.append(p)
    return parts


# ---------------------------------------------------------------- font

def export_font(level, out, textures):
    _, raw = None, rx.payload(level, "big13_font")
    with tempfile.TemporaryDirectory() as d:
        fontkind.dump(raw, pathlib.Path(d))
        f = json.loads((pathlib.Path(d) / "font.json").read_text(encoding="utf-8"))
    tex = textures.get(f["texture"].lower())
    tw, th = f["texture_width"], f["texture_height"]
    if tex is not None:
        info = texture_png(tex, out / "font.png")
        info["file"] = "font.png"
    else:
        info = None
    line = f["line_height"] + 1  # glyph quad height: (line height + 1) * scale (0x005078c0)
    chars = {}
    for g in f["glyphs"]:
        u0, v0, u1, v1 = g["uv"]
        # Pixel = uv * (texture size - 1) (docs/formats/big-font.md). The renderer samples 1 - v of the bottom-up
        # bank image (0x005078c0), so in the upright font.png the stored v runs down from the top row.
        x0, x1 = u0 * (tw - 1), u1 * (tw - 1)
        y0, y1 = v0 * (th - 1), v1 * (th - 1)
        chars[str(g["code"])] = {
            "char": g["char"],
            "x": round(min(x0, x1)), "y": round(min(y0, y1)),
            "width": g["width"] + 1, "height": line,
            "atlas_width": round(abs(x1 - x0), 2), "atlas_height": round(abs(y1 - y0), 2),
            "xoffset": g["x_offset"], "yoffset": 0,
            "xadvance": g["x_offset"] + g["advance"],
            "advance_after_glyph": g["advance"],
            "uv": g["uv"]}
    meta = {
        "note": "BMFont-like metrics of the game's only font (big13_font + txtr mainfontid). Pen model of "
                "0x005078c0: left = pen + xoffset * s, quad width = width * s, height = height * s from the pen "
                "line top, pen += xadvance * s, where s = text size * one screen pixel. A line is "
                "line_height * s tall (0x00506ff0: (line_height_raw + 1) * s). Glyph x/y/width/height are pixels "
                "in font.png (top-left origin); 'uv' is the stored 0..1 rect. Text markup: <br>, <rgb=RRGGBB>, "
                "</rgb>, <tab=N>, <ll=N> (docs/subsystems/hud.md).",
        "texture": f["texture"], "image": info,
        "common": {"line_height": line, "line_height_raw": f["line_height"], "base": line, "scale_w": tw,
                   "scale_h": th, "first_char": f["first_char"], "unknown_10": f["unknown_10"]},
        "shadow": {"colour": "#000000", "offset_px": [2, 2], "note": "flag 2: drawn first, 2 px right and 2 px down"},
        "chars": chars}
    (out / "font.json").write_text(json.dumps(meta, indent=1, ensure_ascii=False), encoding="utf-8")
    return len(chars)


# ---------------------------------------------------------------- caption table (exe, optional)

def caption_table():
    """The speaker/caption styles at 0x005cadd8 (24-byte entries: name*, colour, flags, size, x, y), as
    0x00495cb0 reads them. None without the unpacked exe."""
    if not EXE.exists():
        return None
    try:
        import pefile
    except ImportError:
        return None
    pe = pefile.PE(str(EXE), fast_load=True)
    img, base = pe.get_memory_mapped_image(), pe.OPTIONAL_HEADER.ImageBase
    rows = []
    for i in range(64):
        o = 0x005cadd8 - base + 24 * i
        name_p, colour, flags, size, x, y = struct.unpack_from("<IIIfff", img, o)
        if not base <= name_p < base + len(img):
            break
        e = img.index(b"\0", name_p - base)
        rows.append({"speaker": img[name_p - base:e].decode("latin-1"), "colour": colour_hex(colour),
                     "always_shown": bool(flags & 1), "size": round(size, 3), "x": round(x, 3), "y": round(y, 3)})
    return rows


# ---------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("level")
    ap.add_argument("--out")
    a = ap.parse_args()
    level = a.level
    out = pathlib.Path(a.out) if a.out else ROOT / "work" / "remaster" / level / "ui"
    (out / "textures").mkdir(parents=True, exist_ok=True)

    # UI bank
    with tempfile.TemporaryDirectory() as d:
        uikind.dump(rx.payload(level, "big12_ui"), pathlib.Path(d))
        bank_text = (pathlib.Path(d) / "ui.json").read_text(encoding="utf-8")
    (out / "ui_bank.json").write_text(bank_text, encoding="utf-8")
    bank = json.loads(bank_text)
    pages, refs = bank["pages"], bank["refs"]

    # strings
    _, strings = textkind.parse(rx.payload(level, "big01_text"))
    text_of = {k.decode("latin-1").lower(): v.decode("utf-16-le") for k, v in strings}

    # templates, models, anims, textures
    _, templates = obj.parse(rx.payload(level, "big07_obj"))
    tpl = {t["name"].decode("latin-1").lower(): t for t in templates}
    _, skins = objchunk.split_bank(rx.payload(level, "big05_skin"), padded=True)
    skin_of = {c.name.lower(): c for c in skins}
    _, anims = objchunk.split_bank(rx.payload(level, "big03_anim"), padded=False)
    anim_of = {c.name.lower(): c for c in anims}
    _, texlist, _ = txtr.parse(rx.payload(level, "big02_txtr"))
    textures = {t["name"].decode("latin-1").lower(): t for t in texlist}

    def lod0(name):
        t = tpl.get(name.lower())
        return t["lods"][0].decode("latin-1").lower() if t and t["lods"] else None

    # pages: the anchor object of each page is an anim whose nodes are the ref points. Which anim a page object
    # poses with is not traced (docs/subsystems/hud.md, open questions). Guess, checked against captures: the anim
    # named as the page when there is one (pui_main, pui_pub, pui_pressstart), else pui_general (the pause menu
    # items land where work/visual/pause_m shows them), else the template's model.
    page_rows = []
    for i, p in enumerate(pages):
        chain = [x for x in (p.lower(), "pui_general" if i != 0 else None, lod0(p)) if x and x in anim_of]
        chain = list(dict.fromkeys(chain))
        refs_here = {}
        for src in reversed(chain):  # earlier sources win
            for k, v in anim_nodes(anim_of[src]).items():
                refs_here[k] = dict(v, source=src)
        page_rows.append({"index": i, "name": p, "kind": "hud" if i == 0 else "menu", "anchor_anims": chain,
                          "refs": refs_here})

    def ref_pos(page, ref):
        if ref is None or page < 0 or page >= len(page_rows):
            return None
        return page_rows[page]["refs"].get(ref)

    textures_used = set()
    models = {}
    elements = []
    for n, o in enumerate(bank["object_instances"]):
        name = o["object"] if isinstance(o["object"], str) else bank["objects"][o["object"]]
        page, ref = o["field_46c"], refs[o["field_468"]] if o["field_468"] >= 0 else None
        pos = ref_pos(page, ref)
        if pos and name.lower() == "ui_ammo_infinity":
            # Hud_CreateLevelUi 0x004629b0 moves it 23 units right of its anchor (0x005bcee0, 0x00457870)
            pos = dict(pos, x=round(pos["x"] + 23.0, 3))
        model = lod0(name)
        if model and model not in models and model in skin_of:
            models[model] = mesh_parts(skin_of[model], textures_used)
        shows, fn = SHOWS.get(name.lower(), ("", ""))
        anims_for = sorted(k for k in anim_of if k == model or k.startswith(name.lower() + "_")
                           or (model and k.startswith(model + "_")))
        elements.append({
            "id": f"obj{n}", "kind": "mesh", "name": name, "page": page,
            "page_name": pages[page] if 0 <= page < len(pages) else None, "ref": ref,
            "pos": [pos["x"], pos["y"]] if pos else None,
            "pos_top_left": [pos["x"], round(480 - pos["y"], 3)] if pos else None,
            "z": pos["z"] if pos else None, "ref_source": pos["source"] if pos else None,
            "anchor": anchor_of(page, ref, pos["x"] if pos else None),
            "model": model if model in models else None, "anims": anims_for,
            "nav": dict(zip(("id", "left", "right", "up", "down"), o["fields_478"])),
            "shows": shows, "driver": fn})
    for n, t in enumerate(bank["text_instances"]):
        name = t["text"] if isinstance(t["text"], str) else bank["texts"][t["text"]]
        page, ref = t["field_46c"], refs[t["field_468"]] if t["field_468"] >= 0 else None
        pos = ref_pos(page, ref)
        nav = dict(zip(("id", "left", "right", "up", "down"), t["fields_478"]))
        row = nav["id"] % 100 if nav["id"] > 0 else 0
        x, y = (pos["x"], pos["y"] - (row * 24 if page != 0 and nav["id"] > 0 else 0)) if pos else (None, None)
        colour = int(t["colour"], 16)
        shows, fn = SHOWS.get(name.lower(), ("", ""))
        elements.append({
            "id": f"txt{n}", "kind": "text", "name": name, "page": page,
            "page_name": pages[page] if 0 <= page < len(pages) else None, "ref": ref,
            "pos": [x, round(y, 3)] if pos else None,
            "pos_top_left": [x, round(480 - y, 3)] if pos else None,
            "ref_source": pos["source"] if pos else None, "align": [0.5, 0.5],
            "anchor": anchor_of(page, ref, x),
            "text_id": name, "string": text_of.get(name.lower()),
            "scale": t["scale"], "colour": colour_hex(colour), "colour_raw": t["colour"],
            "glyph_height_px_at_480": round(t["scale"] * 25, 2),
            "shadow": True,
            "nav": nav, "menu_action": nav["id"] // 100 if nav["id"] > 0 else None,
            "menu_row": row if nav["id"] > 0 else None,
            "shows": shows, "driver": fn})

    tex_rows = {}
    for name in sorted(textures_used):
        t = textures.get(name)
        if t is None:
            tex_rows[name] = None
            continue
        tex_rows[name] = texture_png(t, out / "textures" / f"{name}.png")

    subtitles = []
    for m in bank["subtitles"]:
        subtitles.append({"movie": m["movie"], "lines": [
            {"start": ln["start"], "end": ln["end"], "speaker": ln["speaker"], "text_id": ln["text"],
             "string": text_of.get(str(ln["text"]).lower())} for ln in m["lines"]]})

    layout = {
        "level": level,
        "space": {"width": 640, "height": 480, "origin": "bottom-left", "y": "up",
                  "note": "pos = layout point in 640x480 pixels, y up (0x00464b10). pos_top_left = (x, 480 - y). "
                          "Meshes: vertices are pixels around pos. Text: centred on pos (align 0.5, 0.5). "
                          "Text size: glyph pixels * scale in SCREEN pixels in the original (does not grow with "
                          "resolution); remaster: scale with height / 480 (hook/enhance/ui_aspect.cpp)."},
        "colours": {"text_selected": "#ffffff", "text_disabled": "#404040", "text_flag_0x800": "#7f0000",
                    "text_bkd_fill": "#4080c0c0 (ARGB 0xc04080c0)", "text_bkd_outline": "#4080c0"},
        "pulse": {"amplitude": 0.15, "rate": 5.0, "fade_step_per_frame": 0.2,
                  "formula": "size = ((|sin(phase)| * 0.15 + 1 - 1) * fade + 1) * scale; phase += dt * 5"},
        "pages": page_rows,
        "models": models,
        "textures": tex_rows,
        "elements": elements,
        "captions": caption_table(),
        "subtitles": subtitles,
    }
    (out / "layout.json").write_text(json.dumps(layout, indent=1, ensure_ascii=False), encoding="utf-8")
    # every display string (big01_text) by id, for the menu pages' labels
    _, strings = textkind.parse(rx.payload(level, "big01_text"))
    texts = {i.decode("latin-1").lower(): v.decode("utf-16-le", "surrogatepass") for i, v in strings}
    (out / "texts.json").write_text(json.dumps(texts, indent=1, ensure_ascii=False), encoding="utf-8")
    # the credits (GameData/credits.txt, UTF-16; the credits screen 0x00415940 loads it)
    cred = ROOT / "orig" / "pc" / "GameData" / "credits.txt"
    if cred.exists():
        (out / "credits.txt").write_text(cred.read_bytes().decode("utf-16"), encoding="utf-8")
    nchars = export_font(level, out, textures)
    hud = [e for e in elements if e["page"] == 0]
    print(f"ui: {len(pages)} pages, {len(elements)} elements ({len(hud)} HUD), {len(models)} models, "
          f"{sum(1 for v in tex_rows.values() if v)} textures, {nchars} glyphs, {len(subtitles)} subtitle tracks "
          f"-> {out}")
    missing = [e["name"] for e in elements if e["pos"] is None and e["page"] >= 0]
    if missing:
        print(f"no layout point for {len(missing)} elements: {sorted(set(missing))[:20]}")


if __name__ == "__main__":
    main()
