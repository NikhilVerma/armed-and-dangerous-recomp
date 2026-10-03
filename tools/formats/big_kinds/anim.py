"""Animation bank: `*_big03_anim.big`, magic 0x00C0FFEE, loader 0x0045b430 ObjectSystem_LoadAnims.
Layout and safe edits: docs/formats/big-anim.md. Shared bank/chunk code: objchunk.py.

Payload:
  name hash table (common.py; value = anim index; key = the anim's name, from its own source path)
  u32 anim count
  count x anim chunk, back to back (cursor += chunk size; 0x004e8430 reads 12 bytes, then size - 12)

Dump:
  anims.json            hash seed and, per anim in bank order: name, file, and informational fields
                        (source path, size, block count, frame rate and time range from the root object)
  anims/<name>.anm      one in-place object chunk per animation, byte for byte (see the doc for its layout)

Rebuild recomputes the hash table (from each chunk's own name), the count and the chunk order from anims.json,
so anims may be added, removed, reordered or replaced by another chunk of any size.
"""
from . import objchunk

KIND = "anim"
MAGIC = 0x00C0FFEE

NOTE = ("Order = anim index. Read back: name and file only (name must equal the chunk's own name). Everything "
        "else is informational. See docs/formats/big-anim.md.")


def _info(ch, out, file):
    b, r = ch.data, ch.root
    info = {
        "root_words": [objchunk.u16(b, r + 4), objchunk.u16(b, r + 6)],
        "ticks": {"start": objchunk.u32(b, r + 0x14), "end": objchunk.u32(b, r + 0x18),
                  "per_frame": objchunk.u32(b, r + 0x1c)},
        "fps": objchunk.u32(b, r + 0x20),
        "nodes": objchunk.u32(b, r + 0x30),
    }
    trees = objchunk.dict_items(b, objchunk.u32(b, r + 0x28))
    info["trees"] = [name for name, _, _ in trees]
    return info


def dump(payload, out):
    objchunk.dump_bank(payload, out, KIND, "anims", "anm", padded=False, note=NOTE, extra=_info)


def rebuild(src):
    return objchunk.rebuild_bank(src, "anims", padded=False)
