"""Adds what a newer tools/remaster_export.py writes per submesh to an existing export's models/*.json, without
exporting the level again (the textures, meshes and every other file are left as they are):
  - material fields: "blend", "opacity" (the translucency of kind bit 0x10), "uv_sets" (byte +0x32: the uv set each
    map is sampled with)
  - "uvs2", the third uv set (the snow domes' and some terrains' masks), and "tangents", the stored normal map frame
    of the parts with a normal map, appended to the model's .bin (earlier offsets unchanged)

    python tools/remaster_patch_materials.py [LEVEL ...]     (default: every work/remaster/lev_* with models/)

Each model's submeshes are decoded again from the level's skin bank (remaster_export.mesh_submeshes, the same order
as the export; the landscape from the land bank) and the new fields copied over. Prints one line per level.
"""
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "formats"))
import remaster_export as rx  # noqa: E402
import remaster_export_anim  # noqa: E402
from big_kinds import land, objchunk  # noqa: E402

NEW = ("blend", "opacity", "uv_sets")
work = rx.ROOT / "work" / "remaster"


def replace(path, data):
    """Replaced in one step: a game loading the level meanwhile never reads half a file."""
    tmp = path.with_suffix(path.suffix + ".tmp")
    (tmp.write_bytes if isinstance(data, bytes) else tmp.write_text)(data)
    os.replace(tmp, path)


def patch(f, subs):
    """Copies the new fields of `subs` (mesh_submeshes) into the model file `f`; None when the submeshes differ."""
    m = json.loads(f.read_text())
    extras = [(s[7] if len(s) > 7 else {}) for s in subs]
    if len(extras) != len(m.get("submeshes", [])):
        return None
    changed = False
    blob = None
    for e, x in zip(m["submeshes"], extras):
        mat = e.get("material")
        if mat is not None and x:
            for k in NEW:
                if k in x and mat.get(k) != x[k]:
                    mat[k] = x[k]
                    changed = True
        # appended vertex data: (submesh key, mesh_submeshes extra, format); tangents only where a normal map reads them
        for key, k, fmt in (("uvs2", "uv2", "<2f"), ("tangents", "tangents", "<4f")):
            data = x.get(k)
            if not data or key in e or len(data) != e["vertices"] or (key == "tangents" and not (mat or {}).get("normal_map")):
                continue
            blob = bytearray(f.with_suffix(".bin").read_bytes()) if blob is None else blob
            e[key] = len(blob)
            blob += b"".join(struct.pack(fmt, *v) for v in data)
            changed = True
    if not changed:
        return False
    if blob is not None:  # the .bin first: its old offsets stay valid for the old .json meanwhile
        replace(f.with_suffix(".bin"), bytes(blob))
    replace(f, json.dumps(m, indent=1))
    return True


levels = sys.argv[1:] or sorted(d.name for d in work.iterdir() if d.name.startswith("lev_") and (d / "models").is_dir())
for level in levels:
    _, chunks = objchunk.split_bank(rx.payload(level, "big05_skin"), padded=True)
    skins = {c.name.lower(): c for c in chunks}
    patched = skipped = 0
    for f in sorted((work / level / "models").glob("*.json")):
        name = f.stem
        if name == "landscape":
            land_skin, _ = land.parse(rx.payload(level, "big04_land"))
            subs = rx.mesh_submeshes(objchunk.parse_chunk(land_skin))
        elif name in skins:
            # (skinned models split by bone as the export does: the same skeleton lookup, export_model)
            bone_index = remaster_export_anim.skeleton_for(level, skins[name], work / level)
            subs = rx.mesh_submeshes(skins[name], bone_index)
        else:
            continue
        r = patch(f, subs)
        if r is None:
            skipped += 1
        elif r:
            patched += 1
    print(f"{level}: {patched} models patched, {skipped} skipped (submesh count differs)")
