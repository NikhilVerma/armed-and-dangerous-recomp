"""Object templates: `*_big07_obj.big`, magic 0x00BADA55, loader 0x0045bf70 (unnamed; "ObjectSystem_LoadTemplates"
would describe it). Layout and safe edits: docs/formats/big-obj.md.

Payload:
  name hash table (common.py; V = 4: template index; compare 0x0045b5f0 = _stricmp(key, template[v].name))
  u32 count                              number of templates (= hash entries)
  count x template record, in index order (the loader reads the size word, then size - 4 bytes):
    +0x00 u32  size                      whole record; in memory overwritten by the slot hash table object
    +0x04 char name[32]
    +0x24 u32  A                         slot count (slot 0 is "staticmodel", the rest mostly anims)
    +0x28 u32  L                         LOD count
    +0x2c u32  0x3c                      offset of the slot table's size word (loader adds record + 4)
    +0x30 u32  slot names offset         (loader adds record)
    +0x34 u32  LOD names offset          (loader adds record)
    +0x38 u32  matrix offset             (loader adds record)
    +0x3c      slot hash table (sized; V = 4: slot index; compare 0x0045b630 = _stricmp(key, slot name))
               char slot_name[A][32]
               char lod_name[L][32]
               u32  matrix[L*A]          zero in the files; ModelSlots_Resolve 0x0045bbe0 memsets and fills it
               char property[32]         property set name, looked up with 0x0045b6c0 ObjectSystem_FindProperty
               u32  lodcutoff[L]         zero in the files; filled from the property set's lodcutoff_<i> keys

Dump:
  objects.json           hash seed and the template files in index order
  objects/<name>.json    one per template: name, slot table seed, property, slots, lods
                         (matrix / lodcutoff only when the file has non-zero bytes there: none of the retail files)

Rebuild recomputes the hash tables, counts, all offsets and record sizes.
"""
import json
import re
import struct

from . import common

KIND = "obj"
MAGIC = 0x00BADA55
NAME = 32
HDR = 0x3c


def bucket_count(n):
    """Smallest prime > n; an empty table has 1 bucket (the original tool's rule, seen in prop files)."""
    return 1 if n == 0 else common.next_prime(n)


def _str(b):
    s = b.split(b"\0")[0]
    if s.ljust(len(b), b"\0") != b:
        raise ValueError(f"name field {b!r} has bytes after its NUL")
    return s


def parse_template(r):
    size, = struct.unpack_from("<I", r, 0)
    name = _str(r[4:4 + NAME])
    a, l, ho, no, lo, mo = struct.unpack_from("<6I", r, 0x24)
    if ho != HDR:
        raise ValueError(f"{name!r}: slot table offset {ho:#x}, expected {HDR:#x}")
    sub, end = common.parse(r, ho)
    if len(sub) != a or end != no or lo != no + NAME * a or mo != lo + NAME * l:
        raise ValueError(f"{name!r}: unexpected record layout")
    if sorted(sub.u32_values()) != list(range(a)):
        raise ValueError(f"{name!r}: slot table values are not 0..{a - 1}")
    slots = [_str(r[no + NAME * j:no + NAME * (j + 1)]) for j in range(a)]
    lods = [_str(r[lo + NAME * j:lo + NAME * (j + 1)]) for j in range(l)]
    p = mo
    matrix = list(struct.unpack_from(f"<{a * l}I", r, p))
    p += 4 * a * l
    prop = _str(r[p:p + NAME])
    p += NAME
    cutoff = list(struct.unpack_from(f"<{l}I", r, p))
    p += 4 * l
    if p != size:
        raise ValueError(f"{name!r}: record ends at {p:#x}, size {size:#x}")
    return {"name": name, "seed": sub.seed, "slots": slots, "lods": lods, "matrix": matrix,
            "property": prop, "lodcutoff": cutoff}


def template_bytes(t):
    a, l = len(t["slots"]), len(t["lods"])
    for s in [t["name"], t["property"], *t["slots"], *t["lods"]]:
        if len(s) > NAME:  # 32 without a NUL occurs in the retail files (see the doc)
            raise ValueError(f"{t['name']!r}: name {s!r} longer than {NAME} bytes")
    table = common.build(t["slots"], list(range(a)), t["seed"], 4, bucket_count(a)).to_bytes(sized=True)
    matrix = t.get("matrix") or [0] * (a * l)
    cutoff = t.get("lodcutoff") or [0] * l
    if len(matrix) != a * l or len(cutoff) != l:
        raise ValueError(f"{t['name']!r}: matrix needs {a * l} and lodcutoff {l} entries")
    no = HDR + len(table)
    lo = no + NAME * a
    mo = lo + NAME * l
    size = mo + 4 * a * l + NAME + 4 * l
    out = bytearray(struct.pack("<I", size) + t["name"].ljust(NAME, b"\0"))
    out += struct.pack("<6I", a, l, HDR, no, lo, mo) + table
    out += b"".join(s.ljust(NAME, b"\0") for s in t["slots"] + t["lods"])
    out += struct.pack(f"<{a * l}I", *matrix) + t["property"].ljust(NAME, b"\0") + struct.pack(f"<{l}I", *cutoff)
    assert len(out) == size
    return bytes(out)


def parse(payload):
    hd, off = common.parse(payload, 0)
    count = struct.unpack_from("<I", payload, off)[0]
    off += 4
    templates = []
    for _ in range(count):
        size = struct.unpack_from("<I", payload, off)[0]
        templates.append(parse_template(payload[off:off + size]))
        off += size
    if off != len(payload):
        raise ValueError(f"templates end at {off}, payload is {len(payload)} bytes")
    if sorted(hd.u32_values()) != list(range(count)):
        raise ValueError("template table values are not a permutation of the indices")
    return hd, templates


def build_payload(seed, templates):
    names = [t["name"] for t in templates]
    out = bytearray(common.build(names, list(range(len(names))), seed, 4,
                                 bucket_count(len(names))).to_bytes(sized=True))
    out += struct.pack("<I", len(templates))
    for t in templates:
        out += template_bytes(t)
    return bytes(out)


def _file_names(names):
    used, files = set(), []
    for i, n in enumerate(names):
        s = n.decode("latin-1")
        base = s if re.fullmatch(r"[A-Za-z0-9_.\-]+", s) and s.lower() not in used else \
            f"{i:04d}_" + re.sub(r"[^A-Za-z0-9_.\-]", "_", s)
        used.add(base.lower())
        files.append(f"objects/{base}.json")
    return files


def dump(payload, out):
    hd, templates = parse(payload)
    if build_payload(hd.seed, templates) != payload:
        raise AssertionError("obj payload is not reproduced from its parts")
    (out / "objects").mkdir(parents=True, exist_ok=True)
    files = _file_names([t["name"] for t in templates])
    for t, f in zip(templates, files):
        doc = {"name": t["name"].decode("latin-1"), "hash_seed": f"0x{t['seed']:08X}",
               "property": t["property"].decode("latin-1"),
               "lods": [s.decode("latin-1") for s in t["lods"]],
               "slots": [s.decode("latin-1") for s in t["slots"]]}
        if any(t["matrix"]):
            doc["matrix"] = t["matrix"]
        if any(t["lodcutoff"]):
            doc["lodcutoff"] = t["lodcutoff"]
        (out / f).write_text(json.dumps(doc, indent=1))
    meta = {"kind": KIND, "hash_seed": f"0x{hd.seed:08X}",
            "note": "Order = template index. Each file's 'name' is the template's name (the file name is only a "
                    "label). See docs/formats/big-obj.md.",
            "objects": files}
    (out / "objects.json").write_text(json.dumps(meta, indent=1))


def rebuild(src):
    meta = json.loads((src / "objects.json").read_text())
    templates = []
    for f in meta["objects"]:
        d = json.loads((src / f).read_text())
        enc = lambda s: s.encode("latin-1")  # noqa: E731
        templates.append({"name": enc(d["name"]), "seed": int(d.get("hash_seed", "0"), 16),
                          "property": enc(d["property"]), "lods": [enc(s) for s in d["lods"]],
                          "slots": [enc(s) for s in d["slots"]], "matrix": d.get("matrix"),
                          "lodcutoff": d.get("lodcutoff")})
    return build_payload(int(meta["hash_seed"], 16), templates)
