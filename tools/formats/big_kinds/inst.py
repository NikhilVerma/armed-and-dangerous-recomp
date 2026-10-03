"""Object instances (level placement): `*_big10_inst.big`, magic 0xBABEBABE. Layout and safe edits:
docs/formats/big-inst.md.

The loader 0x0045d510 only reads the payload into a buffer (object system +0xe5c). 0x0045d930 walks it and
spawns every instance through 0x0045d7a0; 0x0045d750 finds instance `id` (1-based, in stored order).

  u32 group_count                                    1 in all 25 files
  group_count x { u32 count; count x instance }
  instance:
    u32  size                                        whole record; the walkers step by it
    char object[32]                                  object (template) name, passed to 0x0045f020
    u32  mask                                        spawned only if bit `pass` is set (0x0045d930); 0x3FF = all
    f32  transform[12]                               3 rows x (3 rotation columns, translation)
    u32  props_dir_bytes                             size of the hash block that follows
    hash block (common.py, no size prefix): value = { char name[32]; u32 index }, key = the property name
    u32  value_count                                 = number of properties
    u32  data_bytes
    u32  offset[value_count]                         property `index` starts at data + offset[index]
    u8   data[data_bytes]                            the values back to back, in index order (4 or 32 bytes)

Dump: instances.json, one entry per instance in stored order (the order is the instance id).
Rebuild recomputes every size, count, offset and hash table from the entries.
"""
import json
import math
import struct

from . import common

KIND = "inst"
MAGIC = 0xBABEBABE

NAME = 32
HEAD = 0x5c  # size, object name, mask, transform, props_dir_bytes


def short_f32(x):
    bits = struct.pack("<f", x)
    for p in range(1, 10):
        s = float(f"{x:.{p}g}")
        if struct.pack("<f", s) == bits:
            return s
    return x


def name_field(raw):
    name, _, tail = raw.partition(b"\0")
    return name.decode("latin-1"), (raw.hex() if tail.strip(b"\0") else None)


def name_bytes(name, fill=None, what="name"):
    b = name.encode("latin-1")
    if len(b) > NAME - 1:
        raise ValueError(f"{what} {name!r} longer than {NAME - 1} bytes")
    base = bytes.fromhex(fill) if fill else bytes(NAME)
    return b + b"\0" + base[len(b) + 1:]


def bucket_count(n):
    """Retail rule: the smallest prime > n, but 1 for an empty table (2678 instances without properties)."""
    return 1 if n == 0 else common.next_prime(n)


# ---------------------------------------------------------------------------------------------------------------
# Property values. The file stores no types: the object's code knows them. The dump guesses a readable form
# that always packs back to the same bytes.
def value_json(raw):
    if len(raw) == NAME:
        s, _, tail = raw.partition(b"\0")
        if _ and all(0x20 <= c < 0x7F for c in s):
            return s.decode("ascii") if not tail.strip(b"\0") else {"str": s.decode("ascii"), "fill": raw.hex()}
    elif len(raw) == 4:
        i = struct.unpack("<i", raw)[0]
        if -(1 << 20) <= i <= (1 << 24):
            return i
        x = struct.unpack("<f", raw)[0]
        if math.isfinite(x):
            return short_f32(x)
    return {"hex": raw.hex()}


def value_bytes(v, what):
    if isinstance(v, bool):
        raise ValueError(f"{what}: booleans are not a stored type; use 0 or 1")
    if isinstance(v, int):
        return struct.pack("<I", v & 0xFFFFFFFF)
    if isinstance(v, float):
        return struct.pack("<f", v)
    if isinstance(v, str):
        return name_bytes(v, None, what)
    if "str" in v:
        return name_bytes(v["str"], v.get("fill"), what)
    return bytes.fromhex(v["hex"])


# ---------------------------------------------------------------------------------------------------------------
def parse_instance(buf, p):
    size = struct.unpack_from("<I", buf, p)[0]
    obj, obj_fill = name_field(buf[p + 4:p + 4 + NAME])
    mask = struct.unpack_from("<I", buf, p + 0x24)[0]
    tf = struct.unpack_from("<12f", buf, p + 0x28)
    hs = struct.unpack_from("<I", buf, p + 0x58)[0]
    hd = common.parse_block(buf, p + HEAD, hs)
    q = p + HEAD + hs
    count, data_bytes = struct.unpack_from("<II", buf, q)
    offs = struct.unpack_from(f"<{count}I", buf, q + 8)
    data = buf[q + 8 + 4 * count:q + 8 + 4 * count + data_bytes]
    if size != HEAD + hs + 8 + 4 * count + data_bytes:
        raise ValueError(f"instance at {p}: size {size} does not match its parts")
    if hd.value_size != 36 or len(hd) != count or hd.bucket_count != bucket_count(count):
        raise ValueError(f"instance at {p}: property table V={hd.value_size} n={len(hd)} B={hd.bucket_count}, "
                         f"{count} values")
    names = [None] * count
    for v in hd.values:
        key, fill = name_field(v[:NAME])
        if fill:
            raise ValueError(f"instance at {p}: property name {key!r} has bytes after its NUL")
        names[struct.unpack_from("<I", v, NAME)[0]] = key
    bounds = list(offs) + [data_bytes]
    if any(bounds[i] > bounds[i + 1] for i in range(count)) or (count and offs[0] != 0):
        raise ValueError(f"instance at {p}: value offsets are not contiguous")
    props = [[names[i], value_json(data[bounds[i]:bounds[i + 1]])] for i in range(count)]
    inst = {"object": obj, "mask": f"0x{mask:X}", "seed": f"0x{hd.seed:08X}",
            "transform": [[short_f32(x) for x in tf[r * 4:r * 4 + 4]] for r in range(3)], "props": props}
    if obj_fill:
        inst["object_fill"] = obj_fill
    if instance_bytes(inst) != bytes(buf[p:p + size]):
        raise AssertionError(f"instance at {p} ({obj}) does not rebuild exactly")
    return inst, size


def instance_bytes(inst, what="instance"):
    props = inst["props"]
    vals = [value_bytes(v, f"{what} {inst['object']}.{k}") for k, v in props]
    offs, pos = [], 0
    for v in vals:
        offs.append(pos)
        pos += len(v)
    keys = [k.encode("latin-1") for k, _ in props]
    for k in keys:
        if len(k) > NAME - 1:
            raise ValueError(f"{what}: property name {k!r} longer than {NAME - 1} bytes")
    hd = common.build(keys, [k.ljust(NAME, b"\0") + struct.pack("<I", i) for i, k in enumerate(keys)],
                      int(inst["seed"], 16), 36, bucket_count(len(keys)))
    block = hd.to_bytes(sized=True)  # u32 props_dir_bytes + block
    tail = struct.pack(f"<II{len(offs)}I", len(vals), pos, *offs) + b"".join(vals)
    tf = [x for row in inst["transform"] for x in row]
    if len(tf) != 12:
        raise ValueError(f"{what} {inst['object']}: transform needs 3 rows of 4 numbers")
    body = name_bytes(inst["object"], inst.get("object_fill"), "object name") + \
        struct.pack("<I12f", int(inst["mask"], 16), *tf) + block + tail
    return struct.pack("<I", 4 + len(body)) + body


def dump(payload, out):
    groups = []
    p = 4
    for _ in range(struct.unpack_from("<I", payload, 0)[0]):
        n = struct.unpack_from("<I", payload, p)[0]
        p += 4
        insts = []
        for _ in range(n):
            inst, size = parse_instance(payload, p)
            insts.append(inst)
            p += size
        groups.append(insts)
    if p != len(payload):
        raise ValueError(f"inst: parsed {p} bytes of {len(payload)}")
    # A stored 0 reads as the integer 0. Where the same object's same property holds floats elsewhere in this
    # file, write it as 0.0 so an edit keeps the float type (the bytes are the same).
    floats = {(i["object"], k) for g in groups for i in g for k, v in i["props"] if isinstance(v, float)}
    for g in groups:
        for i in g:
            for pv in i["props"]:
                if pv[1] == 0 and isinstance(pv[1], int) and (i["object"], pv[0]) in floats:
                    pv[1] = 0.0
    meta = {"kind": KIND,
            "note": "groups of instances; an instance's id is its 1-based position over all groups. props are "
                    "[name, value] in stored (index) order: int = 4-byte integer, float = 4-byte f32, string = "
                    "32-byte string, {str, fill} = string with stale bytes after the NUL, {hex} = raw bytes. "
                    "See docs/formats/big-inst.md.",
            "groups": groups}
    (out / "instances.json").write_text(_dumps(meta), encoding="utf-8")


def _dumps(meta):
    def inst_text(i):
        head = json.dumps({k: v for k, v in i.items() if k != "props"}, ensure_ascii=False)[:-1]
        if not i["props"]:
            return "   " + head + ', "props": []}'
        props = ",\n".join("     " + json.dumps(p, ensure_ascii=False) for p in i["props"])
        return "   " + head + ', "props": [\n' + props + "\n   ]}"
    groups = ",\n".join("  [\n" + ",\n".join(inst_text(i) for i in g) + "\n  ]" for g in meta["groups"])
    head = json.dumps({k: v for k, v in meta.items() if k != "groups"}, indent=1, ensure_ascii=False)[:-2]
    return head + ',\n "groups": [\n' + groups + "\n ]\n}\n"


def rebuild(src):
    m = json.loads((src / "instances.json").read_text(encoding="utf-8"))
    out = bytearray(struct.pack("<I", len(m["groups"])))
    n = 0
    for g in m["groups"]:
        out += struct.pack("<I", len(g))
        for inst in g:
            n += 1
            out += instance_bytes(inst, f"instance {n}")
    return bytes(out)
