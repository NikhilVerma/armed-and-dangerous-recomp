"""Property sets: `*_big06_prop.big`, magic 0x00BEADED, loader 0x0045b7d0 ObjectSystem_LoadProperties.
Layout and safe edits: docs/formats/big-prop.md.

Payload (read whole into "prop data", fixed up in place):
  name hash table (common.py; V = 36: char name[32] + u32 set index; compare 0x00495ee0 = _stricmp)
  u32 count                              number of property sets (= hash entries)
  count x property set, in index order:
    name hash table (sized; V = 36: char key[32] + u32 value index; own seed; compare 0x00495ee0)
    u32 nvalues                          (= entries of that table)
    u32 data_bytes                       byte size of the value data
    u32 offset[nvalues]                  byte offset of value j from the start of the value data
    value data: values packed in index order: 4 bytes (int, float or bool) or 32 bytes (NUL-padded string)

The value type is not stored. The getters read a key as a string (0x0045dec0: strncpy 32), a dword
(0x0045df00) or a bool (0x0045df30: dword != 0). The dump guesses int/float per dword (see _guess) and writes
32-byte values as strings; the bytes rebuilt depend only on the JSON value's type, never on the guess.

Dump:
  properties.json            hash seed and the set files in index order
  properties/<name>.json     one per set: name, hash seed of its key table, values [[key, value], ...] in index
                             order (int -> u32/i32, float -> float32, string -> char[32], {"hex": ".."} -> raw).
                             A list because keys can repeat (the lookup then finds the last one).
"""
import json
import math
import re
import struct

from . import common

KIND = "prop"
MAGIC = 0x00BEADED
NAME = 32
STR = 32


def _names(payload_hd, count, what):
    by_index = {}
    for v in payload_hd.values:
        by_index[struct.unpack_from("<I", v, NAME)[0]] = v[:NAME].split(b"\0")[0]
    if sorted(by_index) != list(range(count)):
        raise ValueError(f"{what}: hash values are not a permutation of 0..{count - 1}")
    return [by_index[i] for i in range(count)]


def parse(payload):
    hd, off = common.parse(payload, 0)
    count = struct.unpack_from("<I", payload, off)[0]
    off += 4
    names = _names(hd, count, "property table")
    sets = []
    for i in range(count):
        sub, off = common.parse(payload, off)
        nv, size = struct.unpack_from("<II", payload, off)
        off += 8
        offs = list(struct.unpack_from(f"<{nv}I", payload, off))
        off += 4 * nv
        data = payload[off:off + size]
        off += size
        keys = _names(sub, nv, names[i])
        ends = offs[1:] + [size]
        vals = [data[offs[j]:ends[j]] for j in range(nv)]
        sets.append({"name": names[i], "seed": sub.seed, "keys": keys, "values": vals})
    if off != len(payload):
        raise ValueError(f"property sets end at {off}, payload is {len(payload)} bytes")
    return hd, sets


def _table(names, seed):
    for n in names:
        if len(n) > NAME - 1:
            raise ValueError(f"name {n!r} longer than {NAME - 1} bytes")
    values = [n.ljust(NAME, b"\0") + struct.pack("<I", i) for i, n in enumerate(names)]
    return common.build(names, values, seed, NAME + 4, bucket_count(len(names))).to_bytes(sized=True)


def bucket_count(n):
    """The original tool's rule: the smallest prime > n, except an empty table has 1 bucket (25 empty sets in
    the retail files have B = 1; common.next_prime(0) would give 2)."""
    return 1 if n == 0 else common.next_prime(n)


def build_payload(seed, sets):
    out = bytearray(_table([s["name"] for s in sets], seed))
    out += struct.pack("<I", len(sets))
    for s in sets:
        out += _table(s["keys"], s["seed"])
        offs, data = [], bytearray()
        for v in s["values"]:
            offs.append(len(data))
            data += v
        out += struct.pack(f"<II{len(offs)}I", len(offs), len(data), *offs) + data
    return bytes(out)


# ---------------------------------------------------------------------------------------------------------------
# value <-> JSON
def _f32_text(raw):
    """Shortest decimal that packs back to exactly these float32 bits."""
    f = struct.unpack("<f", raw)[0]
    for p in range(1, 10):
        s = f"{f:.{p}g}"
        if struct.pack("<f", float(s)) == raw:
            return float(s)
    return f


def _guess(raw):
    """'int' or 'float' for a dword: small magnitudes are ints, plausible float32 values are floats."""
    u = struct.unpack("<I", raw)[0]
    if u == 0 or u < 0x00800000 or u >= 0xFF800000:
        return "int"
    f = struct.unpack("<f", raw)[0]
    if math.isfinite(f) and 1e-6 <= abs(f) <= 1e9:
        return "float"
    return "int"


def value_to_json(raw, float_keys, key):
    if len(raw) == STR:
        s = raw.split(b"\0")[0]
        if s.ljust(STR, b"\0") == raw:
            return s.decode("latin-1")
        return {"hex": raw.hex()}
    if len(raw) == 4:
        kind = _guess(raw)
        if kind == "float" or (raw == b"\0\0\0\0" and key.lower() in float_keys):
            return _f32_text(raw)
        return struct.unpack("<i", raw)[0]
    return {"hex": raw.hex()}


def value_from_json(v, what):
    if isinstance(v, bool):
        return struct.pack("<I", int(v))
    if isinstance(v, int):
        return struct.pack("<i" if v < 0 else "<I", v)
    if isinstance(v, float):
        return struct.pack("<f", v)
    if isinstance(v, str):
        b = v.encode("latin-1")
        if len(b) > STR:
            raise ValueError(f"{what}: string longer than {STR} bytes")
        return b.ljust(STR, b"\0")
    if isinstance(v, dict) and "hex" in v:
        return bytes.fromhex(v["hex"])
    raise ValueError(f"{what}: unsupported value {v!r}")


def _set_json(name, seed, pairs):
    """One [key, value] pair per line. A list, not an object: 21 sets repeat a key (see the doc)."""
    rows = ",\n".join("  " + json.dumps(p) for p in pairs)
    return (f'{{\n "name": {json.dumps(name)},\n "hash_seed": "0x{seed:08X}",\n "values": [\n'
            + rows + ("\n" if rows else "") + " ]\n}\n")


def _file_names(names):
    used, files = set(), []
    for i, n in enumerate(names):
        s = n.decode("latin-1")
        base = s if re.fullmatch(r"[A-Za-z0-9_.\-]+", s) and s.lower() not in used else \
            f"{i:04d}_" + re.sub(r"[^A-Za-z0-9_.\-]", "_", s)
        used.add(base.lower())
        files.append(f"properties/{base}.json")
    return files


def dump(payload, out):
    hd, sets = parse(payload)
    if build_payload(hd.seed, sets) != payload:
        raise AssertionError("prop payload is not reproduced from its parts")
    # keys that hold a float somewhere in this file: their 0 values are written as 0.0
    float_keys = {k.decode("latin-1").lower() for s in sets for k, v in zip(s["keys"], s["values"])
                  if len(v) == 4 and _guess(v) == "float"}
    (out / "properties").mkdir(parents=True, exist_ok=True)
    files = _file_names([s["name"] for s in sets])
    for s, f in zip(sets, files):
        keys = [k.decode("latin-1") for k in s["keys"]]
        pairs = [[k, value_to_json(v, float_keys, k)] for k, v in zip(keys, s["values"])]
        (out / f).write_text(_set_json(s["name"].decode("latin-1"), s["seed"], pairs))
    meta = {"kind": KIND, "hash_seed": f"0x{hd.seed:08X}",
            "note": "Order = property set index. Each file's 'name' is the set's name (the file name is only a "
                    "label). Value types: int, float (float32), string (<= 32 bytes) or {\"hex\": ..}. "
                    "See docs/formats/big-prop.md.",
            "sets": files}
    (out / "properties.json").write_text(json.dumps(meta, indent=1))


def rebuild(src):
    meta = json.loads((src / "properties.json").read_text())
    sets = []
    for f in meta["sets"]:
        doc = json.loads((src / f).read_text())
        pairs = doc["values"]
        sets.append({"name": doc["name"].encode("latin-1"), "seed": int(doc.get("hash_seed", "0"), 16),
                     "keys": [k.encode("latin-1") for k, _ in pairs],
                     "values": [value_from_json(v, f"{f}: {k}") for k, v in pairs]})
    return build_payload(int(meta["hash_seed"], 16), sets)
