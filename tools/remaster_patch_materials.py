"""Adds the material fields a newer tools/remaster_export.py writes ("blend", "opacity": the translucency of kind bit
0x10) to an existing export's models/*.json, without exporting the level again (the textures, meshes and every other
file are left as they are).

    python tools/remaster_patch_materials.py [LEVEL ...]     (default: every work/remaster/lev_* with models/)

Each model's submeshes are decoded again from the level's skin bank (remaster_export.mesh_submeshes, the same order
as the export) and their material records' new fields copied over. Prints one line per level.
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "formats"))
import remaster_export as rx  # noqa: E402
import remaster_export_anim  # noqa: E402
from big_kinds import objchunk  # noqa: E402

NEW = ("blend", "opacity")
work = rx.ROOT / "work" / "remaster"
levels = sys.argv[1:] or sorted(d.name for d in work.iterdir() if d.name.startswith("lev_") and (d / "models").is_dir())
for level in levels:
    _, chunks = objchunk.split_bank(rx.payload(level, "big05_skin"), padded=True)
    skins = {c.name.lower(): c for c in chunks}
    patched = skipped = 0
    for f in sorted((work / level / "models").glob("*.json")):
        name = f.stem
        if name not in skins:
            continue
        m = json.loads(f.read_text())
        # (skinned models split by bone as the export does: the same skeleton lookup, export_model)
        bone_index = remaster_export_anim.skeleton_for(level, skins[name], work / level)
        subs = rx.mesh_submeshes(skins[name], bone_index)
        extras = [(s[7] if len(s) > 7 else {}) for s in subs]
        if len(extras) != len(m.get("submeshes", [])):
            skipped += 1
            continue
        changed = False
        for e, x in zip(m["submeshes"], extras):
            mat = e.get("material")
            if mat is None or not x:
                continue
            for k in NEW:
                if k in x and mat.get(k) != x[k]:
                    mat[k] = x[k]
                    changed = True
        if changed:
            # replaced in one step: a game loading the level meanwhile never reads half a file
            tmp = f.with_suffix(".json.tmp")
            tmp.write_text(json.dumps(m, indent=1))
            os.replace(tmp, f)
            patched += 1
    print(f"{level}: {patched} models patched, {skipped} skipped (submesh count differs)")
