"""Export one level's assets from the retail `.big` files to the remaster engine's scene format (engine/).

  python tools/remaster_export.py lev_1_m1            -> work/remaster/lev_1_m1/

Output (game data: lives under work/, never committed):
  scene.json             landscape model, placed instances (template, model, 4x3 transform), lights, bounds
  models/<name>.json     submeshes (one per material): texture, alpha, byte ranges in <name>.bin
  models/<name>.bin      per submesh: positions f32x3, normals f32x3, uv0 f32x2, indices u32 (triangle list);
                         skinned submeshes add "joints" (u16x4 skeleton bone indices) and "weights" (f32x4, sum 1)
                         byte offsets, and the model json names its "skeleton" (skeletons/<name>.json, written by
                         tools/remaster_export_anim.py; anims too: run that script for the anims/)
  properties.json        every property set: name -> {key: value} (behaviour, damage, fx names, ...)
  templates.json         every object template: name -> {model (LOD 0), lods, property}
  fx.json                every fx script: name -> instructions (docs/formats/big-fx.md; run by engine/src/fxvm.rs)
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
from big_kinds import fx, inst, land, obj, objchunk, prop, skin, txtr  # noqa: E402

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


def stored_tangents(m, normals):
    """The mesh's own normal map frame per vertex (skin slots 15 tangent, 14 binormal: skin.decode_mesh), as
    (tx, ty, tz, w): the unit tangent and w = the side of the binormal, sign((normal x tangent) . binormal), so that
    binormal = (normal x tangent) * w as the engine rebuilds it. The bump vertex shaders take the light into this
    frame as (L.tangent, L.binormal, L.normal) (shaders.dat, vs_00359a0adf630f5e: v15, v14). None without one, or
    when it is degenerate (a zero tangent): the engine generates tangents from the uvs then."""
    tan, bi = m.get("_tangent"), m.get("_binormal")
    if not tan or not bi or len(tan) != len(normals):
        return None
    out = []
    for (tx, ty, tz), (bx, by, bz), (nx, ny, nz) in zip(tan, bi, normals):
        ln = math.sqrt(tx * tx + ty * ty + tz * tz)
        if ln == 0.0:
            return None
        cx, cy, cz = ny * tz - nz * ty, nz * tx - nx * tz, nx * ty - ny * tx
        w = 1.0 if cx * bx + cy * by + cz * bz >= 0.0 else -1.0
        out.append((tx / ln, ty / ln, tz / ln, w))
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


def mesh_submeshes(ch, bone_index=None):
    """Decoded render meshes of one skin chunk -> [(texture stem, positions, normals, uvs, triangles, colour,
    skin)], one entry per material, vertices remapped to the ones that material uses. With `bone_index` (bone name
    -> skeleton index, tools/remaster_export_anim.py), skin = (joints, weights) per remapped vertex for skinned
    meshes (else None)."""
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
        uv1 = m.get("_uv1")
        uv2 = m.get("_uv2")
        # the material records (docs/formats/big-skin.md 3.5) and the mesh's texture dictionary (id -> path): +0x16
        # base texture, +0x18 normal map, +0x1a second texture, +0x1c blend mask (ids; 0 none), +0x34 the shading
        # kind in its low byte (seen with the original's material log, [test] log_materials: 0x03 bump diffuse
        # ps, 0x0f the terrain ps t0 normal / t1 base / t2 second / t3 mask, 0x05 environment cube map * factor +
        # texture * lighting), +0x50.. emissive, ambient, diffuse, specular, +0x60 power
        texids = {i: stem(name) for name, _p, i in objchunk.dict_items(b, objchunk.u32(b, mptr + 0x74))}
        # the material-name dictionary (+0x70; a record's +0x14 id): the surface name the physics mesh parts carry
        # (MeshPart_LoadMaterial 0x004122f0: material +0x10), whose "_" tokens pick an fx's material variant
        # (Fx_Create 0x0041cc60 flag 0x800: fx_fs_roman_run_shallow for "Water_shallow_path_streambed")
        names = {i: (n.decode("latin1") if isinstance(n, bytes) else n) for n, _p, i in objchunk.dict_items(b, objchunk.u32(b, mptr + 0x70))}
        mat_raw = []
        for k in range(objchunk.u32(b, mptr + 0x78)):
            p = objchunk.u32(b, mptr + 0x7C) + k * 0x64
            h = lambda o: struct.unpack_from("<H", b, p + o)[0]
            col = lambda o: struct.unpack_from("<I", b, p + o)[0]
            # stored blue-first (0x00BBGGRR): checked against the original's captures (Roman's belt brown, rock warm;
            # the UI bank's flat colours are stored the same way)
            rgb = lambda v: [(v & 255) / 255, (v >> 8 & 255) / 255, (v >> 16 & 255) / 255]
            mat_raw.append({
                "kind": b[p + 0x34],
                # translucency (kind bit 0x10, fn_004897d0 0x004897d0): the blend mode in byte +0x37 (0 SRCALPHA/ONE,
                # 1 DESTCOLOR/ZERO, 2 SRCALPHA/INVSRCALPHA: blend table 0x0057d9e0 entries 1, 4, 3) and the opacity
                # +0x48, which the draw state takes as its constant alpha (texture factor) when below 1
                "blend": b[p + 0x37],
                "opacity": struct.unpack_from("<f", b, p + 0x48)[0],
                # the environment materials' (kind bit 4 without a mask) cube map strength: the draw state's texture
                # factor is this grey, ftol(+0x40 * 255) clamped to 255, alpha 255 (the material set-up in
                # archive/src/loader/loader_01.cpp, the 0x7000 colour mix); stage 0 multiplies the cube by it
                "env_factor": struct.unpack_from("<f", b, p + 0x40)[0],
                "normal_map": texids.get(h(0x18)),
                "texture2": texids.get(h(0x1A)),
                "mask": texids.get(h(0x1C)),
                "emissive": rgb(col(0x50)), "ambient": rgb(col(0x54)), "diffuse": rgb(col(0x58)),
                "specular": rgb(col(0x5C)), "power": struct.unpack_from("<f", b, p + 0x60)[0],
                "surface": names.get(h(0x14)),
                # the uv set of each map, two bits apiece: base (bits 0-1), normal map (2-3), second texture (4-5),
                # mask (6-7). RenderModes_Load (0x00489b40) hands them to the stages with their textures; the stage
                # addressing (0x00504ef0) packs set + 1 into the vertex shader key, whose shader reads TEXCOORD<set>
                # (shaders.dat "2100212": bump, base set 0, normal set 1). E.g. 0x40 the terrain (mask on set 1), 0x04
                # the village walls' and boulders' plaster normal map (set 1, ~5x the base's tiling), 0x90 the snow
                # domes (second flake layer set 1, mask set 2)
                "uv_sets": b[p + 0x32],
            })
        jw = rigid = None
        if bone_index is not None:
            import remaster_export_anim  # noqa: PLC0415 (it imports this module's helpers)
            jw = remaster_export_anim.skin_vertex_weights(ch, mptr, bone_index)
            # rigid parts on several bones (UBYTE4 blend indices, no weights: oilpump_1's arm, strut, gyro and pump
            # follow oilpump_cycle): each material's triangles split by bone, each part exported with its "bone"
            if jw is None:
                rigid = remaster_export_anim.rigid_vertex_bones(ch, mptr)
                if rigid is not None and len({r for r in rigid if r}) < 2:
                    rigid = None
        by_mat = {}
        for g in m["groups"]:
            tris = [t for t in skin.strip_triangles(g["_idx"]) if max(t) < len(pos)]
            by_mat.setdefault(g["material"], []).extend(tris)
        all_tris = [t for ts in by_mat.values() for t in ts]
        computed = face_normals(pos, all_tris)
        stored = read_normals(b, objchunk.u32(b, mptr + 0x44), len(pos))
        normals = computed
        if stored:  # use the stored normals when they agree with the geometry (else they are not plain shorts)
            # only where the geometry gives a normal: a double-sided card (both windings over the same vertices: lev_4_m3's
            # reeds, the b_grass / b_beam cards) sums to zero there, and those vertices used to fail the test, so the
            # whole mesh got the zero normals (black, NaN in the game material) instead of its stored ones
            comparable = [(a, c) for a, c in zip(stored, computed) if c != (0.0, 0.0, 0.0)]
            agree = sum(1 for a, c in comparable if a[0] * c[0] + a[1] * c[1] + a[2] * c[2] > 0.3)
            if not comparable or agree > 0.8 * len(comparable):
                normals = stored
        tangents = stored_tangents(m, normals)
        parts = []
        for mat, tris in by_mat.items():
            if rigid is None:
                parts.append((mat, tris, None))
                continue
            by_bone = {}
            for t in tris:  # a triangle's bone = its first vertex's (the three agree in every lev_1_m1 mesh)
                by_bone.setdefault(rigid[t[0]], []).append(t)
            mixed = sum(1 for t in tris if len({rigid[v] for v in t}) > 1)
            if mixed:
                print(f"{ch.name}: {mixed} triangles span two rigid bones (split by their first vertex)")
            parts += [(mat, ts, bone or "") for bone, ts in by_bone.items()]
        for mat, tris, rigid_bone in parts:
            if not tris:
                continue
            mats = m["materials"]
            tex = stem(mats[mat]["texture"]) if 0 <= mat < len(mats) else None
            # untextured parts draw in the material's colour (guess: the third of the four colours is diffuse)
            color = int(mats[mat]["colors_50_5c"][2], 16) if 0 <= mat < len(mats) else 0xFFFFFF
            remap, vp, vn, vt, idx, vs, vt1, vt2, vtan = {}, [], [], [], [], [], [], [], []
            for t in tris:
                for v in t:
                    if v not in remap:
                        remap[v] = len(vp)
                        vp.append(pos[v])
                        vn.append(normals[v])
                        vt.append(uv[v])
                        if uv1:
                            vt1.append(uv1[v])
                        if uv2:
                            vt2.append(uv2[v])
                        if tangents:
                            vtan.append(tangents[v])
                        if jw:
                            vs.append(jw[v])
                    idx.append(remap[v])
            extra = dict(mat_raw[mat]) if 0 <= mat < len(mat_raw) else {}
            extra["uv1"] = vt1 if uv1 else None
            extra["uv2"] = vt2 if uv2 else None
            extra["tangents"] = vtan if tangents else None
            # a mesh bound to one bone (the collision models' parts: grunt_coll_9 m_chest -> chest ...): its bone
            if len(m.get("bones") or []) == 1:
                extra["bone"] = m["bones"][0]["name"]
            # a rigid part of a multi-bone mesh: its bone ("" = the base matrix, no bone); the model gets "skeleton"
            if rigid_bone is not None:
                extra["bone"] = rigid_bone or None
                extra["rigid"] = True
            out.append((tex, vp, vn, vt, idx, color, vs if jw else None, extra))
    return out


def write_model(out, name, subs, texinfo, skeleton=None):
    blob = bytearray()
    entries = []
    lo = [math.inf] * 3
    hi = [-math.inf] * 3
    for tex, vp, vn, vt, idx, color, vs, *rest in subs:
        extra = rest[0] if rest else {}
        e = {"texture": tex if tex in texinfo else None, "vertices": len(vp), "indices": len(idx),
             # the material's diffuse, stored blue-first (0x00BBGGRR) like every material colour
             "color": [(color & 255) / 255, (color >> 8 & 255) / 255, (color >> 16 & 255) / 255]}
        e["alpha"] = bool(e["texture"] and texinfo[tex]["alpha"])
        # the material as the original shades it (mesh_submeshes): kind, extra maps, colours
        if extra:
            e["material"] = {k: (v if not (k in ("normal_map", "texture2", "mask") and v not in texinfo) else None)
                             for k, v in extra.items() if k not in ("uv1", "uv2", "tangents", "bone", "rigid", "surface")}
        if extra.get("surface"):
            e["surface"] = extra["surface"]
        if extra.get("bone"):
            e["bone"] = extra["bone"]
        for key, arr, fmt in (("positions", vp, "3f"), ("normals", vn, "3f"), ("uvs", vt, "2f")):
            e[key] = len(blob)
            blob += b"".join(struct.pack("<" + fmt, *v) for v in arr)
        for k, key in (("uv1", "uvs1"), ("uv2", "uvs2")):
            if extra.get(k):
                e[key] = len(blob)
                blob += b"".join(struct.pack("<2f", *v) for v in extra[k])
        # the original's normal map frame (stored_tangents), for the parts that have a normal map
        if extra.get("tangents") and (e.get("material") or {}).get("normal_map"):
            e["tangents"] = len(blob)
            blob += b"".join(struct.pack("<4f", *v) for v in extra["tangents"])
        e["index_offset"] = len(blob)
        blob += struct.pack(f"<{len(idx)}I", *idx)
        if vs:  # skinning (tools/remaster_export_anim.py): u16x4 skeleton bone indices, f32x4 weights (sum 1)
            e["joints"] = len(blob)
            blob += b"".join(struct.pack("<4H", *j) for j, _ in vs)
            e["weights"] = len(blob)
            blob += b"".join(struct.pack("<4f", *w) for _, w in vs)
        for x, y, z in vp:
            lo = [min(lo[0], x), min(lo[1], y), min(lo[2], z)]
            hi = [max(hi[0], x), max(hi[1], y), max(hi[2], z)]
        entries.append(e)
    (out / "models").mkdir(parents=True, exist_ok=True)
    (out / "models" / f"{name}.bin").write_bytes(bytes(blob))
    doc = {"name": name, "submeshes": entries, "min": lo, "max": hi}
    # skinned (joints / weights) or rigid parts carried by bones ("bone" per part, posed by the skeleton)
    if skeleton and (any("joints" in e for e in entries) or any(s[7].get("rigid") for s in subs if len(s) > 7)):
        doc["skeleton"] = skeleton
        if not any("joints" in e for e in entries):
            doc["rigid"] = True
    (out / "models" / f"{name}.json").write_text(json.dumps(doc, indent=1))
    return sum(e["indices"] for e in entries) // 3


def export_model(level, out, name, ch, texinfo):
    """One skin chunk -> models/<name>.{json,bin}; skinned ones also get skeletons/<name>.json and joints/weights."""
    import remaster_export_anim  # noqa: PLC0415
    bone_index = remaster_export_anim.skeleton_for(level, ch, out)
    subs = mesh_submeshes(ch, bone_index)
    return write_model(out, name, subs, texinfo, name if bone_index else None) if subs else 0


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

    models, placed, lights, skipped, settings = {}, [], [], {}, {}
    for i, it in enumerate(load_instances(level)):
        name = it["object"].lower() if isinstance(it["object"], str) else it["object"].decode("latin1").lower()
        props = dict((k if isinstance(k, str) else k.decode("latin1"), v) for k, v in it["props"])
        if name == "light":
            lights.append({"transform": it["transform"], "props": props})
            continue
        if name == "global_settings":  # fog and other level-wide values
            settings.update(props)
            continue
        t = tpl.get(name)
        model = t["lods"][0].decode("latin1").lower() if t and t["lods"] else None
        if model and model not in models and model in skins:
            models[model] = export_model(level, out, model, skins[model], texinfo)
        if not model or not models.get(model):
            skipped[name] = skipped.get(name, 0) + 1
            continue
        placed.append({"id": i + 1, "template": name, "model": model, "transform": it["transform"],
                       "property": t["property"].decode("latin1"),
                       # this instance's own property values: they override the template's set
                       "props": {k.lower(): v for k, v in props.items()}})
    # Game data the engine runs as the game does: property sets, templates, fx scripts. Every template's
    # model is exported too (fx objects and debris are spawned by name at run time).
    _, psets = prop.parse(payload(level, "big06_prop"))
    float_keys = {k.decode("latin-1").lower() for st in psets for k, v in zip(st["keys"], st["values"])
                  if len(v) == 4 and prop._guess(v) == "float"}
    props = {st["name"].decode("latin-1").lower(): {k.decode("latin-1").lower(): prop.value_to_json(v, float_keys, k.decode("latin-1").lower())
                                                    for k, v in zip(st["keys"], st["values"])} for st in psets}
    (out / "properties.json").write_text(json.dumps(props, indent=0))
    tdefs = {}
    for name, t in tpl.items():
        lods = [x.decode("latin1").lower() for x in t["lods"]]
        pr = t["property"].decode("latin1").lower()
        model = lods[0] if lods else None
        ci = props.get(pr, {}).get("collision_lod_index")
        coll = lods[ci] if isinstance(ci, int) and 0 <= ci < len(lods) else None
        # every LOD model (Object_SelectLod 0x00452f50 switches between them by distance), the collision one too
        for m in lods:
            if m and m not in models and m in skins:
                models[m] = export_model(level, out, m, skins[m], texinfo)
        tdefs[name] = {"model": model if models.get(model) else None, "lods": lods, "property": pr,
                       "collision": coll if models.get(coll) else None,
                       # the default animation every object plays in a loop from load (Object_LoadProperties
                       # 0x004517b0: 0x00408e30 flags 0x81, 0xa01 at a random phase with anim_rand): slot index
                       # vt+0x40 = fn_00451780: 0 with one slot (slot 0 is "staticmodel"), else the slot named
                       # like the object (guess: never the case for placed objects), else 1
                       "default_anim": t["slots"][1].decode("latin1").lower() if len(t["slots"]) > 1 else None,
                       # per LOD index: the exported model, or null (no mesh)
                       "lod_models": [m if models.get(m) else None for m in lods]}
    (out / "templates.json").write_text(json.dumps(tdefs, indent=0))
    _, fxs = fx.parse(payload(level, "big09_fx"))
    scripts = {e["name"].decode("latin-1").lower(): e["script"] for e in fxs if e["script"] is not None}
    (out / "fx.json").write_text(json.dumps(scripts))
    print(f"properties {len(props)}, templates {len(tdefs)}, fx scripts {len(scripts)}, models {len(models)}")
    # logic objects with no model (mission, markers, sound and ui ears, generators, camera paths): kept for the
    # engine's mission logic with their props and placement
    logic = []
    for i, it in enumerate(load_instances(level)):
        name = it["object"].lower() if isinstance(it["object"], str) else it["object"].decode("latin1").lower()
        if name in ("light",):
            continue
        t = tpl.get(name)
        if t and t["lods"] and models.get(t["lods"][0].decode("latin1").lower()):
            continue
        logic.append({"id": i + 1, "template": name, "transform": it["transform"],
                      "property": t["property"].decode("latin1").lower() if t else None,
                      "props": {(k if isinstance(k, str) else k.decode("latin1")).lower(): v for k, v in it["props"]}})
    scene = {"level": level, "logic": logic, "up": "z", "landscape": "landscape", "bounds": [grid["min"], grid["max"]],
             "cell_size": grid["cell_size"], "instances": placed, "lights": lights, "settings": settings, "textures": texinfo}
    (out / "scene.json").write_text(json.dumps(scene, indent=1))
    print(f"instances: {len(placed)} placed from {len(models)} models; lights {len(lights)}; "
          f"skipped (no model) {sum(skipped.values())}: {sorted(skipped)}")


if __name__ == "__main__":
    main()
