"""Skin (model) bank: `*_big05_skin.big`, magic 0x0BADF00D, loader 0x0045af30 ObjectSystem_LoadSkins.
Layout, mesh format and safe edits: docs/formats/big-skin.md. Shared bank/chunk code: objchunk.py.

Payload:
  name hash table (common.py; value = skin index; key = the skin's name, from its own source path)
  u32 skin count
  count x { skin chunk, zero padding to (size & ~31) + 32 bytes }   (the loader steps exactly that far)

Dump:
  skins.json             hash seed and, per skin in bank order: name, file, and informational fields
  skins/<name>.skn       one in-place object chunk per skin, byte for byte: the authoritative data
  skins/<name>.obj       view-only export of the render meshes (positions, uv set 0, triangles from the strips,
                         one OBJ group per draw group, usemtl = material name); not read back
  skins/<name>.json      view-only description: vertex format, draw groups, materials, textures, bones

Rebuild recomputes the hash table (from each chunk's own name), the count, the order and the padding, so skins
may be added, removed, reordered or replaced by another skin chunk of any size. Editing the .obj/.json does
nothing: the chunk is an in-place object graph and this tool has no writer for it (see the doc).
"""
import json
import struct

from . import objchunk
from .objchunk import u16, u32

KIND = "skin"
MAGIC = 0x0BADF00D

NOTE = ("Order = skin index. Read back: name and file (.skn) only; the name must equal the chunk's own name. "
        "The .obj/.json next to each .skn are view-only. See docs/formats/big-skin.md.")

INV_32767 = struct.unpack("<f", struct.pack("<f", 1 / 32767))[0]  # 0x005bcb78

# Mesh object (fixed up by 0x004e8620): offsets used here.
M_ROOT, M_NVERT, M_STREAMS = 0x04, 0x40, 0x44
M_MATNAMES, M_TEXNAMES = 0x70, 0x74
M_NMAT, M_MAT, M_NGRP, M_GRP, M_NBONE, M_BONE, M_NPAL, M_PAL = 0x78, 0x7C, 0x80, 0x84, 0x88, 0x8C, 0x90, 0x94
MAT_SIZE, GRP_SIZE, BONE_SIZE, PAL_SIZE = 0x64, 0x2C, 0x58, 0x18

# The 16 attribute slots of the stream block (+0xac, {stream, offset, type, bytes}), as the decls show them.
SLOTS = {0: "position", 1: "blendweight", 2: "normal", 9: "uv0", 10: "uv1", 11: "uv2", 12: "uv3",
         13: "blendindices", 14: "binormal", 15: "tangent"}
DECL_TYPES = {0: "FLOAT1", 1: "FLOAT2", 2: "FLOAT3", 3: "FLOAT4", 4: "D3DCOLOR", 5: "UBYTE4", 6: "SHORT2",
              7: "SHORT4", 8: "UBYTE4N", 9: "SHORT2N", 10: "SHORT4N"}
DECL_USAGE = {0: "POSITION", 1: "BLENDWEIGHT", 2: "BLENDINDICES", 3: "NORMAL", 4: "PSIZE", 5: "TEXCOORD",
              6: "TANGENT", 7: "BINORMAL", 10: "COLOR"}


def floats(b, o, n):
    return list(struct.unpack_from(f"<{n}f", b, o))


def _read3(b, desc, streams, n, scale, offset):
    """0x004effa0 for every vertex: 3 components (types 7/8 short, 11/12 short/32767, 15/16 float) * scale +
    offset. None for a type that function does not read."""
    s, off, typ = desc[0], desc[1], desc[2]
    data, stride = streams[s]
    if typ in (7, 8, 11, 12):
        k = INV_32767 if typ in (11, 12) else 1.0
        raw = [struct.unpack_from("<3h", b, data + i * stride + off) for i in range(n)]
        vals = [(x * k, y * k, z * k) for x, y, z in raw]
    elif typ in (15, 16):
        vals = [struct.unpack_from("<3f", b, data + i * stride + off) for i in range(n)]
    else:
        return None
    return [(x * scale[0] + offset[0], y * scale[1] + offset[1], z * scale[2] + offset[2]) for x, y, z in vals]


def _read2(b, desc, streams, n, scale, offset):
    """VertexStream_ReadVec2 0x004efec0 for every vertex: types 6-8 short, 10-12 short/32767, 14-16 float."""
    s, off, typ = desc[0], desc[1], desc[2]
    data, stride = streams[s]
    if typ in (6, 7, 8, 10, 11, 12):
        k = INV_32767 if typ >= 10 else 1.0
        vals = [struct.unpack_from("<2h", b, data + i * stride + off) for i in range(n)]
        vals = [(x * k, y * k) for x, y in vals]
    elif typ in (14, 15, 16):
        vals = [struct.unpack_from("<2f", b, data + i * stride + off) for i in range(n)]
    else:
        return None
    return [(x * scale[0] + offset[0], y * scale[1] + offset[1]) for x, y in vals]


def strip_triangles(idx):
    """D3DPT_TRIANGLESTRIP as drawn at 0x004fe01f (primitive count = indices - 2), degenerates dropped."""
    for k in range(len(idx) - 2):
        a, b_, c = idx[k], idx[k + 1], idx[k + 2]
        if a == b_ or b_ == c or a == c:
            continue
        yield (a, b_, c) if k % 2 == 0 else (b_, a, c)


def decode_mesh(b, m, bone_names):
    """Everything the loader shows about one mesh object, plus decoded vertices/triangles for the OBJ."""
    sb = u32(b, m + M_STREAMS)
    mat_names = {i: n for n, _, i in objchunk.dict_items(b, u32(b, m + M_MATNAMES))}
    tex_names = {i: n for n, _, i in objchunk.dict_items(b, u32(b, m + M_TEXNAMES))}
    info = {"vertex_count": u32(b, m + M_NVERT)}
    mats = []
    for i in range(u32(b, m + M_NMAT)):
        p = u32(b, m + M_MAT) + i * MAT_SIZE
        mats.append({"name": mat_names.get(u16(b, p + 0x14)), "texture": tex_names.get(u16(b, p + 0x16)),
                     "flags_18": u32(b, p + 0x18), "flags_34": f"0x{u32(b, p + 0x34):08X}",
                     "floats_38_4c": floats(b, p + 0x38, 6),
                     "colors_50_5c": [f"0x{u32(b, p + o):06X}" for o in (0x50, 0x54, 0x58, 0x5C)],
                     "power_60": floats(b, p + 0x60, 1)[0]})
    pals = []
    for i in range(u32(b, m + M_NPAL)):
        p = u32(b, m + M_PAL) + i * PAL_SIZE
        n, arr = u32(b, p + 0x10), u32(b, p + 0x14)
        pals.append([u16(b, arr + 2 * k) for k in range(n)] if arr else [])
    bones = []
    for i in range(u32(b, m + M_NBONE)):
        p = u32(b, m + M_BONE) + i * BONE_SIZE
        ident = u16(b, p + 8)
        bones.append({"id": ident, "name": bone_names.get(ident), "matrix": floats(b, p + 0xC, 16)})
    groups = []
    for i in range(u32(b, m + M_NGRP)):
        p = u32(b, m + M_GRP) + i * GRP_SIZE
        ib = u32(b, p + 0x24)
        n = u32(b, ib + 0x10) if ib else 0
        idx = list(struct.unpack_from(f"<{n}H", b, u32(b, ib + 0x3C))) if n else []
        pal = struct.unpack_from("<i", b, p + 0x1C)[0]
        g = {"material": u32(b, p + 0x10), "first_vertex": u32(b, p + 0x14), "vertex_count": u32(b, p + 0x18),
             "palette": pal, "strip_indices": n, "unknown_20": u32(b, p + 0x20),
             "bounds": floats(b, ib + 0x24, 6) if ib else None, "_idx": idx}
        if 0 <= pal < len(pals):
            g["palette_bones"] = [bones[k]["name"] if k < len(bones) else k for k in pals[pal]]
        groups.append(g)
    info.update(materials=mats, groups=groups, bones=bones)
    if not sb:
        return info
    streams = [(u32(b, sb + 0x70 + 20 * s), u32(b, sb + 0x74 + 20 * s)) for s in range(2)]
    descs = {k: tuple(b[sb + 0xAC + 4 * k:sb + 0xB0 + 4 * k]) for k in range(16)}
    decl = []
    for k in range(16 if u32(b, sb + 0x9C) else 0):  # stride 0: no vertex data, the decl area is all zero
        st, off, typ, _, usage, ui = struct.unpack_from("<HHBBBB", b, sb + 0xF0 + 8 * k)
        if st == 0xFF or typ == 17:  # D3DDECL_END
            break
        decl.append(f"stream {st} +{off} {DECL_TYPES.get(typ, typ)} {DECL_USAGE.get(usage, usage)}{ui}")
    info["streams"] = {
        "vertex_count": u32(b, sb + 0xC), "stride": u32(b, sb + 0x9C), "fvf": f"0x{u32(b, sb + 0xA0):08X}",
        "slot_mask": f"0x{u32(b, sb + 0xA4):04X}",
        "slots": {SLOTS.get(k, str(k)): {"stream": d[0], "offset": d[1], "type": d[2], "bytes": d[3]}
                  for k, d in descs.items() if d[2]},
        "stream_strides": [s[1] for s in streams if s[0]],
        "position_scale": floats(b, sb + 0x10, 3), "position_offset": floats(b, sb + 0x20, 3),
        "uv_scale_offset": [floats(b, sb + 0x30 + 16 * c, 4) for c in range(4)],
        "d3d_decl": decl,
    }
    n = u32(b, sb + 0xC)
    if n and descs[0][2] and streams[descs[0][0]][0]:
        info["_pos"] = _read3(b, descs[0], streams, n, floats(b, sb + 0x10, 3), floats(b, sb + 0x20, 3))
        # uv set c (slot 9 + c; scale +0x30 + 16c, offset +8 after it), as the vertex shaders read them (v9..v12 *
        # c18..c21 .xy + .zw): "_uv" the first, "_uv1" (terrain blend masks, the bump walls' normal maps), "_uv2" (the
        # snow domes' masks); which map takes which set is the material's byte +0x32 (remaster_export.py uv_sets)
        for c, key in enumerate(("_uv", "_uv1", "_uv2", "_uv3")):
            d = descs[9 + c]
            if d[2] and streams[d[0]][0]:
                info[key] = _read2(b, d, streams, n, floats(b, sb + 0x30 + 16 * c, 2), floats(b, sb + 0x38 + 16 * c, 2))
        # the normal map's frame (slots 15 tangent, 14 binormal; SHORT4, directions): the bump vertex shaders take the
        # light into it as (L.v15, L.v14, L.normal), so the normal map's x runs along the tangent, y along the binormal
        for slot, key in ((15, "_tangent"), (14, "_binormal")):
            d = descs[slot]
            if d[2] and streams[d[0]][0]:
                info[key] = _read3(b, d, streams, n, (1.0, 1.0, 1.0), (0.0, 0.0, 0.0))
    return info


def decode_skin(ch):
    b, r = ch.data, ch.root
    bone_names = {i: n for n, _, i in objchunk.dict_items(b, u32(b, r + 0x18))}
    out = {"root_words": [u16(b, r + 4), u16(b, r + 6)],
           "root_unknown_24_30": [u32(b, r + o) for o in (0x24, 0x28, 0x2C, 0x30)],
           "has_element_list_14": u32(b, r + 0x14) != 0,
           "bone_names": [{"id": i, "name": n} for i, n in sorted(bone_names.items())], "trees": []}
    for tname, tree, _ in objchunk.dict_items(b, u32(b, r + 0x1C)):
        meshes = []
        for mname, m, _ in objchunk.dict_items(b, tree):
            info = decode_mesh(b, m, bone_names)
            info["name"] = mname
            meshes.append(info)
        out["trees"].append({"name": tname, "meshes": meshes})
    return out


def _fmt(x):
    return f"{x:.6g}"


def write_obj(skin, path, title):
    lines = [f"# {title}: view-only export of the render meshes (docs/formats/big-skin.md); not read back",
             "# raw game coordinates; triangles in Direct3D strip order; vt = (u, 1 - v)"]
    base_v = 1
    for tree in skin["trees"]:
        for m in tree["meshes"]:
            pos = m.get("_pos")
            if not pos:
                continue
            uv = m.get("_uv")
            lines.append("o " + f"{tree['name']}/{m['name']}".replace(" ", "_"))
            lines += [f"v {_fmt(x)} {_fmt(y)} {_fmt(z)}" for x, y, z in pos]
            if uv:
                lines += [f"vt {_fmt(u)} {_fmt(1 - v)}" for u, v in uv]
            for gi, g in enumerate(m["groups"]):
                mat = m["materials"][g["material"]]["name"] if 0 <= g["material"] < len(m["materials"]) else None
                lines.append(f"g group{gi}")
                lines.append(f"usemtl {(mat or 'none').replace(' ', '_')}")
                for a, b_, c in strip_triangles(g["_idx"]):
                    if max(a, b_, c) >= len(pos):
                        continue
                    a, b_, c = a + base_v, b_ + base_v, c + base_v
                    lines.append(f"f {a}/{a} {b_}/{b_} {c}/{c}" if uv else f"f {a} {b_} {c}")
            base_v += len(pos)
    path.write_text("\n".join(lines) + "\n")


def _public(skin):
    """The JSON view: drop the decoded arrays (they are in the OBJ) and keep it small."""
    out = dict(skin)
    out["trees"] = []
    for tree in skin["trees"]:
        meshes = []
        for m in tree["meshes"]:
            mm = {k: v for k, v in m.items() if not k.startswith("_")}
            mm["groups"] = [{k: v for k, v in g.items() if not k.startswith("_")} for g in m["groups"]]
            meshes.append(mm)
        out["trees"].append({"name": tree["name"], "meshes": meshes})
    return out


def _extra(ch, out, file):
    stem = file[:-4]
    try:
        skin = decode_skin(ch)
    except (struct.error, ValueError, IndexError) as e:  # view-only: never block the dump
        return {"view": f"not decoded: {type(e).__name__}: {e}"}
    write_obj(skin, out / f"{stem}.obj", ch.name)
    (out / f"{stem}.json").write_text(objchunk.json_text(_public(skin)))
    meshes = [m for t in skin["trees"] for m in t["meshes"]]
    return {"view": [f"{stem}.obj", f"{stem}.json"], "meshes": len(meshes),
            "vertices": sum(m["vertex_count"] for m in meshes), "bones": len(skin["bone_names"])}


def dump(payload, out):
    objchunk.dump_bank(payload, out, KIND, "skins", "skn", padded=True, note=NOTE, extra=_extra)


def rebuild(src):
    return objchunk.rebuild_bank(src, "skins", padded=True)
