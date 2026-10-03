"""Bitmap font: `*_big13_font.big`, magic 0x0ADDBEEF, loader 0x004888b0. Layout and safe edits:
docs/formats/big-font.md.

Payload (identical in all 25 levels):
  char  texture[32]                  name of the glyph texture, looked up in the txtr bank (0x004881c0)
  font data (handed to renderer virtual 1, 0x00506c90, which keeps a pointer to it):
    u32 version                      must be 4 (0x00506e32), else the font is rejected
    u32 line_height                  pixels; renderer +0x5dc
    u32 texture_width                renderer +0x5e0, divides the glyph u coordinates
    u32 texture_height               renderer +0x5e4, divides the glyph v coordinates
    u32 unknown_10                   not read by 0x00506c90 (0x10 in the retail file)
    u16 first_char, u16 last_char    renderer +0x5e8 / +0x5ea; characters outside are not drawn
    u32 glyph_count                  = last - first + 1; not read by the game (glyphs start at data+0x1c)
    glyph_count x { f32 u0, v0, u1, v1; i16 x_offset, width, advance; u16 pad (0xBAAD) }

Dump:  font.json   texture name, the header fields, one entry per glyph in character order.
Rebuild recomputes last_char and glyph_count from the glyph list.
"""
import json
import math
import struct

KIND = "font"
MAGIC = 0x0ADDBEEF

NAME = 32
GLYPH = struct.Struct("<4f3hH")


def short_f32(x):
    """The shortest decimal that packs to the same f32 (keeps the JSON readable and the rebuild exact)."""
    bits = struct.pack("<f", x)
    for p in range(1, 10):
        s = float(f"{x:.{p}g}")
        if struct.pack("<f", s) == bits:
            return s
    return x


def name_field(raw):
    """32-byte NUL-padded name -> (str, fill hex or None). fill keeps non-zero bytes after the NUL verbatim."""
    name, _, tail = raw.partition(b"\0")
    return name.decode("latin-1"), (raw.hex() if tail.strip(b"\0") else None)


def name_bytes(name, fill=None, what="name"):
    b = name.encode("latin-1")
    if len(b) > NAME - 1:
        raise ValueError(f"{what} {name!r} longer than {NAME - 1} bytes")
    base = bytes.fromhex(fill) if fill else bytes(NAME)
    return b + b"\0" + base[len(b) + 1:]


def _char(c):
    try:
        return bytes([c]).decode("cp1252") if c < 256 else None
    except UnicodeDecodeError:
        return None


def dump(payload, out):
    tex, tex_fill = name_field(payload[:NAME])
    ver, line_h, tw, th, unk, first, last, count = struct.unpack_from("<5IHHI", payload, NAME)
    p = NAME + 0x1c
    n = (len(payload) - p) // GLYPH.size
    if (len(payload) - p) % GLYPH.size or n != count or count != last - first + 1:
        raise ValueError(f"font: {len(payload) - p} glyph bytes, count {count}, chars {first}..{last}")
    glyphs = []
    for i in range(n):
        u0, v0, u1, v1, xo, w, adv, pad = GLYPH.unpack_from(payload, p + i * GLYPH.size)
        if not all(math.isfinite(f) for f in (u0, v0, u1, v1)):
            raise ValueError(f"font: glyph {i} has a non-finite uv")
        c = first + i
        g = {"code": c, "char": _char(c),
             "uv": [short_f32(u0), short_f32(v0), short_f32(u1), short_f32(v1)],
             "x_offset": xo, "width": w, "advance": adv}
        if pad != 0xBAAD:
            g["pad"] = pad
        glyphs.append(g)
    meta = {"kind": KIND, "texture": tex, "version": ver, "line_height": line_h, "texture_width": tw,
            "texture_height": th, "unknown_10": unk, "first_char": first,
            "note": "glyphs in character order from first_char; 'code' and 'char' are informational. "
                    "uv in 0..1 of the texture. See docs/formats/big-font.md.",
            "glyphs": glyphs}
    if tex_fill:
        meta["texture_fill"] = tex_fill
    (out / "font.json").write_text(dumps_rows(meta, "glyphs"), encoding="utf-8")


def dumps_rows(meta, key):
    """JSON with meta[key] written one list item per line (readable and diff-friendly)."""
    head = json.dumps({k: v for k, v in meta.items() if k != key}, indent=1)[:-2]
    rows = ",\n".join("  " + json.dumps(r, ensure_ascii=False) for r in meta[key])
    return f'{head},\n "{key}": [\n{rows}\n ]\n}}\n'


def rebuild(src):
    m = json.loads((src / "font.json").read_text(encoding="utf-8"))
    out = bytearray(name_bytes(m["texture"], m.get("texture_fill"), "texture name"))
    glyphs = m["glyphs"]
    first = m["first_char"]
    last = first + len(glyphs) - 1
    if not glyphs or last > 0xFFFF:
        raise ValueError(f"font: {len(glyphs)} glyphs from {first} do not fit u16 character codes")
    out += struct.pack("<5IHHI", m["version"], m["line_height"], m["texture_width"], m["texture_height"],
                       m["unknown_10"], first, last, len(glyphs))
    for g in glyphs:
        out += GLYPH.pack(*g["uv"], g["x_offset"], g["width"], g["advance"], g.get("pad", 0xBAAD))
    return bytes(out)
