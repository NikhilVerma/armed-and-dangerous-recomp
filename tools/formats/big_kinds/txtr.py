"""Texture bank: `*_big02_txtr.big`, magic 0x0B00B1E5, loader 0x00488400 RenderSystem_LoadTextures.
Layout and safe edits: docs/formats/big-txtr.md.

Payload:
  name hash table (common.py; value = texture index; key = texture name)
  u32 texture count
  texture blob: count x entry
    char  name[32]                   NUL-padded
    u32   entry_size                 0x28 + pad + image chain
    u32   pad                        128 - (blob offset of entry + 0x28) % 128, zero bytes: aligns the chain
    u8    zero[pad]
    image chain (renderer virtual 84, 0x0050c360):
      u32 levels
      levels x { u16 width, u16 height, u32 D3DFORMAT, u16 rows, u16 pitch, u32 bytes, u8 pixels[bytes] }

Dump:
  textures.json          seed of the hash table, and per texture (in bank order): name, file, format, width,
                         height, levels (format..levels are informational; the DDS is authoritative)
  textures/<name>.dds    one DDS per texture: the stored format and the whole stored mip chain. DXT2 is
                         written with the DXT3 FourCC (identical block encoding; most tools cannot open DXT2)
                         and turned back into DXT2 on rebuild when textures.json says "DXT2".

Rebuild recomputes everything else (hash table, count, entry sizes, padding, level headers) from the DDS
files and the names, so textures may be replaced by a DDS of another size, format or level count.
"""
import json
import re
import struct

from . import common

KIND = "txtr"
MAGIC = 0x0B00B1E5

ALIGN = 128
HEADER = 0x28

DXT = {b"DXT1": 8, b"DXT2": 16, b"DXT3": 16, b"DXT4": 16, b"DXT5": 16}
DXT2, DXT3 = (struct.unpack("<I", cc)[0] for cc in (b"DXT2", b"DXT3"))
# D3DFORMAT -> (bits per pixel, R, G, B, A masks); the retail files use 21, 23 and 24 (and DXT1, DXT2).
RGB = {
    20: (24, 0xFF0000, 0x00FF00, 0x0000FF, 0),           # R8G8B8
    21: (32, 0xFF0000, 0x00FF00, 0x0000FF, 0xFF000000),  # A8R8G8B8
    22: (32, 0xFF0000, 0x00FF00, 0x0000FF, 0),           # X8R8G8B8
    23: (16, 0xF800, 0x07E0, 0x001F, 0),                 # R5G6B5
    24: (16, 0x7C00, 0x03E0, 0x001F, 0),                 # X1R5G5B5
    25: (16, 0x7C00, 0x03E0, 0x001F, 0x8000),            # A1R5G5B5
    26: (16, 0x0F00, 0x00F0, 0x000F, 0xF000),            # A4R4G4B4
}
NAMES = {20: "R8G8B8", 21: "A8R8G8B8", 22: "X8R8G8B8", 23: "R5G6B5", 24: "X1R5G5B5", 25: "A1R5G5B5",
         26: "A4R4G4B4"}


def fourcc(fmt):
    return struct.pack("<I", fmt) if fmt > 0xFF else None


def format_name(fmt):
    cc = fourcc(fmt)
    return cc.decode("latin-1") if cc else NAMES.get(fmt, str(fmt))


def format_from_name(s):
    if s in (k.decode() for k in DXT):
        return struct.unpack("<I", s.encode())[0]
    for k, v in NAMES.items():
        if v == s:
            return k
    return int(s)


def level_layout(fmt, width, height, level):
    """(w, h, rows, pitch, bytes) of a stored level: DXT levels never go below 4x4, others below 1x1."""
    cc = fourcc(fmt)
    if cc:
        if cc not in DXT:
            raise ValueError(f"unsupported FourCC {cc!r}")
        w, h = max(4, width >> level), max(4, height >> level)
        rows = (h + 3) // 4
        pitch = (w + 3) // 4 * DXT[cc]
    else:
        if fmt not in RGB:
            raise ValueError(f"unsupported D3DFORMAT {fmt}")
        w, h = max(1, width >> level), max(1, height >> level)
        rows = h
        pitch = w * RGB[fmt][0] // 8
    return w, h, rows, pitch, rows * pitch


# ---------------------------------------------------------------------------------------------------------------
# DDS
def dds_bytes(fmt, width, height, levels, pixels):
    cc = fourcc(fmt)
    flags = 0x1 | 0x2 | 0x4 | 0x1000 | (0x20000 if levels > 1 else 0)
    caps = 0x1000 | (0x400008 if levels > 1 else 0)
    if cc:
        flags |= 0x80000
        top = level_layout(fmt, width, height, 0)[4]
        pf = struct.pack("<II4s5I", 32, 0x4, cc, 0, 0, 0, 0, 0)
    else:
        bpp, r, g, b, a = RGB[fmt]
        flags |= 0x8
        top = width * bpp // 8
        pf = struct.pack("<II4s5I", 32, 0x40 | (0x1 if a else 0), b"\0\0\0\0", bpp, r, g, b, a)
    head = struct.pack("<7I", 124, flags, height, width, top, 0, levels) + b"\0" * 44 + pf
    head += struct.pack("<4I", caps, 0, 0, 0) + b"\0" * 4
    assert len(head) == 124
    return b"DDS " + head + pixels


def dds_parse(data, what="dds"):
    """-> (D3DFORMAT, width, height, levels, pixel bytes after the header)."""
    if data[:4] != b"DDS " or struct.unpack_from("<I", data, 4)[0] != 124:
        raise ValueError(f"{what}: not a DDS file")
    _, flags, height, width, _, _, mips = struct.unpack_from("<7I", data, 4)
    pf_flags, cc, bpp, r, g, b, a = struct.unpack_from("<I4s5I", data, 4 + 76)
    levels = mips if (mips and (flags & 0x20000 or mips > 1)) else 1
    if pf_flags & 0x4:
        if cc == b"DX10":
            raise ValueError(f"{what}: DX10-header DDS is not supported; save as legacy DXT1/DXT5 or 32-bit")
        if cc not in DXT:
            raise ValueError(f"{what}: unsupported FourCC {cc!r}")
        fmt = struct.unpack("<I", cc)[0]
    else:
        if not pf_flags & 0x1:
            a = 0
        match = [k for k, v in RGB.items() if v == (bpp, r, g, b, a)]
        if not match:
            raise ValueError(f"{what}: unsupported pixel format bpp={bpp} masks="
                             f"{r:#x}/{g:#x}/{b:#x}/{a:#x}")
        fmt = match[0]
    return fmt, width, height, levels, data[128:]


# ---------------------------------------------------------------------------------------------------------------
def parse(payload):
    hd, off = common.parse(payload, 0)
    count = struct.unpack_from("<I", payload, off)[0]
    off += 4
    blob = off
    textures = []
    for i in range(count):
        name = payload[off:off + 32].split(b"\0")[0]
        size, pad = struct.unpack_from("<II", payload, off + 32)
        p = off + HEADER + pad
        levels = struct.unpack_from("<I", payload, p)[0]
        p += 4
        first = p
        w0, h0, fmt = struct.unpack_from("<HHI", payload, p)
        for lv in range(levels):
            p += 16 + struct.unpack_from("<I", payload, p + 12)[0]
        pixels = b"".join(_level_pixels(payload, first, levels))
        textures.append({"name": name, "format": fmt, "width": w0, "height": h0, "levels": levels,
                         "pixels": pixels})
        off += size
    if off != len(payload):
        raise ValueError(f"texture blob ends at {off}, payload is {len(payload)} bytes")
    return hd, textures, blob


def _level_pixels(payload, p, levels):
    for _ in range(levels):
        n = struct.unpack_from("<I", payload, p + 12)[0]
        yield payload[p + 16:p + 16 + n]
        p += 16 + n


def chain_bytes(fmt, width, height, levels, pixels, what="texture"):
    out = bytearray(struct.pack("<I", levels))
    pos = 0
    for lv in range(levels):
        w, h, rows, pitch, n = level_layout(fmt, width, height, lv)
        if pos + n > len(pixels):
            raise ValueError(f"{what}: pixel data too short for level {lv} ({w}x{h} {format_name(fmt)})")
        out += struct.pack("<HHIHHI", w, h, fmt, rows, pitch, n) + pixels[pos:pos + n]
        pos += n
    if pos != len(pixels):
        raise ValueError(f"{what}: {len(pixels) - pos} bytes of pixel data left after {levels} levels")
    return bytes(out)


def build_payload(seed, textures):
    """textures: list of (name bytes, chain bytes). Everything derived is recomputed here."""
    names = [n for n, _ in textures]
    hd = common.build(names, list(range(len(names))), seed)
    out = bytearray(hd.to_bytes(sized=True))
    out += struct.pack("<I", len(textures))
    blob = bytearray()
    for name, chain in textures:
        if len(name) > 31:
            raise ValueError(f"texture name {name!r} longer than 31 bytes")
        pad = ALIGN - (len(blob) + HEADER) % ALIGN
        blob += name.ljust(32, b"\0") + struct.pack("<II", HEADER + pad + len(chain), pad) + b"\0" * pad + chain
    return bytes(out + blob)


def _file_names(names):
    used, files = set(), []
    for i, n in enumerate(names):
        s = n.decode("latin-1")
        base = s if re.fullmatch(r"[A-Za-z0-9_.\-]+", s) and s.lower() not in used else \
            f"{i:04d}_" + re.sub(r"[^A-Za-z0-9_.\-]", "_", s)
        used.add(base.lower())
        files.append(f"textures/{base}.dds")
    return files


def dump(payload, out):
    hd, textures, _ = parse(payload)
    (out / "textures").mkdir(parents=True, exist_ok=True)
    files = _file_names([t["name"] for t in textures])
    entries = []
    for t, f in zip(textures, files):
        dds_fmt = DXT3 if t["format"] == DXT2 else t["format"]
        (out / f).write_bytes(dds_bytes(dds_fmt, t["width"], t["height"], t["levels"], t["pixels"]))
        entries.append({"name": t["name"].decode("latin-1"), "file": f, "format": format_name(t["format"]),
                        "width": t["width"], "height": t["height"], "levels": t["levels"]})
    meta = {"kind": KIND, "hash_seed": f"0x{hd.seed:08X}",
            "note": "Order = texture index. Read back: name, file, and format only to turn a DXT3 DDS back into "
                    "DXT2; size, levels and pixel format come from the DDS header. See docs/formats/big-txtr.md.",
            "textures": entries}
    (out / "textures.json").write_text(json.dumps(meta, indent=1))
    # Sanity: the derived hash table must match the stored one, or rebuild would not be exact.
    if common.build([t["name"] for t in textures], list(range(len(textures))), hd.seed).to_bytes() != \
            hd.to_bytes():
        raise AssertionError("stored hash table differs from the one rebuilt from the names")


def rebuild(src):
    meta = json.loads((src / "textures.json").read_text())
    textures = []
    for e in meta["textures"]:
        fmt, w, h, levels, pixels = dds_parse((src / e["file"]).read_bytes(), e["file"])
        if fmt == DXT3 and e.get("format") == "DXT2":
            fmt = DXT2  # same blocks; the dump writes DXT2 as DXT3 so ordinary tools can open it
        textures.append((e["name"].encode("latin-1"), chain_bytes(fmt, w, h, levels, pixels, e["file"])))
    return build_payload(int(meta["hash_seed"], 16), textures)
