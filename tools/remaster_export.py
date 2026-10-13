"""Export one level's assets from the retail `.big` files to the remaster engine's scene format (engine/).

  python tools/remaster_export.py lev_1_m1            -> work/remaster/lev_1_m1/

Output (game data: lives under work/, never committed):
  scene.json             landscape model, placed instances (template, model, 4x3 transform), lights, bounds
  models/<name>.json     submeshes (one per material): texture, alpha, byte ranges in <name>.bin
  models/<name>.bin      per submesh: positions f32x3, normals f32x3, uv0 f32x2, indices u32 (triangle list)
  textures/<name>.dds    the bank's mip chain: DXT1, DXT2 (written as DXT3), A8R8G8B8; R5G6B5 -> A8R8G8B8

Coordinates stay the game's (Z up, docs/formats/big-land.md); the engine converts. Formats: docs/formats/big-*.md.
Reusable part: everything after load_level() works on plain meshes, so another game needs only its own loader.
"""
import argparse
import json
import math
import pathlib
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "formats"))
from big_kinds import inst, land, obj, objchunk, skin, txtr  # noqa: E402

GAME = ROOT / "orig" / "pc" / "GameData"
DXT1 = struct.unpack("<I", b"DXT1")[0]
DXT2 = struct.unpack("<I", b"DXT2")[0]
DXT3 = struct.unpack("<I", b"DXT3")[0]
A8R8G8B8, R5G6B5 = 21, 23


def payload(level, part):
    data = (GAME / level / f"{level}_{part}.big").read_bytes()
    return data[8:]


def stem(path):
    return path.replace("/", "\\").rsplit("\\", 1)[-1].rsplit(".", 1)[0].lower() if path else None


# ---------------------------------------------------------------- textures

def convert_565(w, h, levels, pixels):
    out, p = bytearray(), 0
    for lv in range(levels):
        lw, lh = max(1, w >> lv), max(1, h >> lv)
        n = lw * lh
        src = struct.unpack_from(f"<{n}H", pixels, p)
        p += n * 2
        for c in src:
            r, g, b = (c >> 11) & 31, (c >> 5) & 63, c & 31
            out += bytes(((b << 3) | (b >> 2), (g << 2) | (g >> 4), (r << 3) | (r >> 2), 255))
    return bytes(out)


def export_textures(level, out):
    _, textures, _ = txtr.parse(payload(level, "big02_txtr"))
    (out / "textures").mkdir(parents=True, exist_ok=True)
    info = {}
    for t in textures:
        name = t["name"].decode("latin1").lower()
        fmt, pixels = t["format"], t["pixels"]
        alpha = fmt in (DXT2, DXT3, A8R8G8B8)
        if fmt == R5G6B5:
            fmt, pixels = A8R8G8B8, convert_565(t["width"], t["height"], t["levels"], pixels)
        if fmt not in (DXT1, DXT2, DXT3, A8R8G8B8):
            print(f"texture {name}: format {txtr.format_name(fmt)} skipped")
            continue
        (out / "textures" / f"{name}.dds").write_bytes(
            txtr.dds_bytes(DXT3 if fmt == DXT2 else fmt, t["width"], t["height"], t["levels"], pixels))
        if fmt == A8R8G8B8 and alpha:  # only call it alpha if some pixel is not opaque
            alpha = any(pixels[i] != 255 for i in range(3, t["width"] * t["height"] * 4, 4))
        info[name] = {"alpha": alpha, "premultiplied": fmt == DXT2, "width": t["width"], "height": t["height"]}
    return info


# ---------------------------------------------------------------- meshes

def read_normals(b, m_streams_block, n):
    """Slot 2 (normal) when stored as shorts (type 7): normalised. None otherwise."""
    sb = m_streams_block
    d = tuple(b[sb + 0xAC + 8:sb + 0xB0 + 8])
    if d[2] != 7:
        return None
    data, stride = objchunk.u32(b, sb + 0x70 + 20 * d[0]), objchunk.u32(b, sb + 0x74 + 20 * d[0])
    if not data:
        return None
    out = []
    for i in range(n):
        x, y, z = struct.unpack_from("<3h", b, data + i * stride + d[1])
        ln = math.sqrt(x * x + y * y + z * z) or 1.0
        out.append((x / ln, y / ln, z / ln))
    return out


def face_normals(pos, tris):
    acc = [[0.0, 0.0, 0.0] for _ in pos]
    for a, b_, c in tris:
        ax, ay, az = pos[a]
        e1 = (pos[b_][0] - ax, pos[b_][1] - ay, pos[b_][2] - az)
        e2 = (pos[c][0] - ax, pos[c][1] - ay, pos[c][2] - az)
        n = (e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0])
        for v in (a, b_, c):
            acc[v][0] += n[0]
            acc[v][1] += n[1]
            acc[v][2] += n[2]
    out = []
    for x, y, z in acc:
        ln = math.sqrt(x * x + y * y + z * z) or 1.0
        out.append((x / ln, y / ln, z / ln))
    return out


def mesh_submeshes(ch):
    """Decoded render meshes of one skin chunk -> [(texture stem, positions, normals, uvs, triangles)],
    one entry per material, vertices remapped to the ones that material uses."""
    b = ch.data
    s = skin.decode_skin(ch)
    out = []
    # mesh objects in dictionary order, to find each one's stream block again for the normals
    mesh_ptrs = [m for _, tree, _ in objchunk.dict_items(b, objchunk.u32(b, ch.root + 0x1C))
                 for _, m, _ in objchunk.dict_items(b, tree)]
    meshes = [m for t in s["trees"] for m in t["meshes"]]
    for mptr, m in zip(mesh_ptrs, meshes):
        pos = m.get("_pos")
        if not pos:
            continue
        uv = m.get("_uv") or [(0.0, 0.0)] * len(pos)
        by_mat = {}
        for g in m["groups"]:
            tris = [t for t in skin.strip_triangles(g["_idx"]) if max(t) < len(pos)]
            by_mat.setdefault(g["material"], []).extend(tris)
        all_tris = [t for ts in by_mat.values() for t in ts]
        computed = face_normals(pos, all_tris)
        stored = read_normals(b, objchunk.u32(b, mptr + 0x44), len(pos))
        normals = computed
        if stored:  # use the stored normals when they agree with the geometry (else they are not plain shorts)
            agree = sum(1 for a, c in zip(stored, computed) if a[0] * c[0] + a[1] * c[1] + a[2] * c[2] > 0.3)
            if agree > 0.8 * len(pos):
                normals = stored
        for mat, tris in by_mat.items():
            if not tris:
                continue
            mats = m["materials"]
            tex = stem(mats[mat]["texture"]) if 0 <= mat < len(mats) else None
            # untextured parts draw in the material's colour (guess: the third of the four colours is diffuse)
            color = int(mats[mat]["colors_50_5c"][2], 16) if 0 <= mat < len(mats) else 0xFFFFFF
            remap, vp, vn, vt, idx = {}, [], [], [], []
            for t in tris:
                for v in t:
                    if v not in remap:
                        remap[v] = len(vp)
                        vp.append(pos[v])
                        vn.append(normals[v])
                        vt.append(uv[v])
                    idx.append(remap[v])
            out.append((tex, vp, vn, vt, idx, color))
    return out


def write_model(out, name, subs, texinfo):
    blob = bytearray()
    entries = []
    lo = [math.inf] * 3
    hi = [-math.inf] * 3
    for tex, vp, vn, vt, idx, color in subs:
        e = {"texture": tex if tex in texinfo else None, "vertices": len(vp), "indices": len(idx),
             "color": [(color >> 16 & 255) / 255, (color >> 8 & 255) / 255, (color & 255) / 255]}
        e["alpha"] = bool(e["texture"] and texinfo[tex]["alpha"])
        for key, arr, fmt in (("positions", vp, "3f"), ("normals", vn, "3f"), ("uvs", vt, "2f")):
            e[key] = len(blob)
            blob += b"".join(struct.pack("<" + fmt, *v) for v in arr)
        e["index_offset"] = len(blob)
        blob += struct.pack(f"<{len(idx)}I", *idx)
        for x, y, z in vp:
            lo = [min(lo[0], x), min(lo[1], y), min(lo[2], z)]
            hi = [max(hi[0], x), max(hi[1], y), max(hi[2], z)]
        entries.append(e)
    (out / "models").mkdir(parents=True, exist_ok=True)
    (out / "models" / f"{name}.bin").write_bytes(bytes(blob))
    (out / "models" / f"{name}.json").write_text(json.dumps({"name": name, "submeshes": entries,
                                                             "min": lo, "max": hi}, indent=1))
    return sum(e["indices"] for e in entries) // 3


# ---------------------------------------------------------------- level

def load_instances(level):
    p = payload(level, "big10_inst")
    groups, off, out = struct.unpack_from("<I", p, 0)[0], 4, []
    for _ in range(groups):
        n = struct.unpack_from("<I", p, off)[0]
        off += 4
        for _ in range(n):
            it, size = inst.parse_instance(p, off)
            off += size
            out.append(it)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("level")
    ap.add_argument("--out")
    a = ap.parse_args()
    level = a.level
    out = pathlib.Path(a.out) if a.out else ROOT / "work" / "remaster" / level
    out.mkdir(parents=True, exist_ok=True)

    texinfo = export_textures(level, out)
    print(f"textures: {len(texinfo)}")

    _, chunks = objchunk.split_bank(payload(level, "big05_skin"), padded=True)
    skins = {c.name.lower(): c for c in chunks}
    _, templates = obj.parse(payload(level, "big07_obj"))
    tpl = {t["name"].decode("latin1").lower(): t for t in templates}

    land_skin, grid = land.parse(payload(level, "big04_land"))
    tris = write_model(out, "landscape", mesh_submeshes(objchunk.parse_chunk(land_skin)), texinfo)
    print(f"landscape: {tris} triangles")

    models, placed, lights, skipped = {}, [], [], {}
    for i, it in enumerate(load_instances(level)):
        name = it["object"].lower() if isinstance(it["object"], str) else it["object"].decode("latin1").lower()
        props = dict((k if isinstance(k, str) else k.decode("latin1"), v) for k, v in it["props"])
        if name == "light":
            lights.append({"transform": it["transform"], "props": props})
            continue
        t = tpl.get(name)
        model = t["lods"][0].decode("latin1").lower() if t and t["lods"] else None
        if model and model not in models and model in skins:
            subs = mesh_submeshes(skins[model])
            models[model] = write_model(out, model, subs, texinfo) if subs else 0
        if not model or not models.get(model):
            skipped[name] = skipped.get(name, 0) + 1
            continue
        placed.append({"id": i + 1, "template": name, "model": model, "transform": it["transform"],
                       "property": t["property"].decode("latin1")})
    scene = {"level": level, "up": "z", "landscape": "landscape", "bounds": [grid["min"], grid["max"]],
             "instances": placed, "lights": lights, "textures": texinfo}
    (out / "scene.json").write_text(json.dumps(scene, indent=1))
    print(f"instances: {len(placed)} placed from {len(models)} models; lights {len(lights)}; "
          f"skipped (no model) {sum(skipped.values())}: {sorted(skipped)}")


if __name__ == "__main__":
    main()
