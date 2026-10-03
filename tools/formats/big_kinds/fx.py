"""Effects bank: `*_big09_fx.big`, magic 0x00EFFEC5, loader 0x0041c7e0 Fx_LoadChunk.
Layout and safe edits: docs/formats/big-fx.md.

Payload (read as one "Fx Data" allocation after the size word):
  name hash table (common.py, sized; value = byte offset of the fx in the script area; compare 0x0041c7b0)
  script area (no count word): n x fx, in offset order, back to back
    fx:   name (string slot), then instructions until the next fx
    instruction: u32 opcode, then operands by opcode (interpreter 0x00420770, jump table 0x00423088 via the byte
                 table 0x00423250 indexed by opcode - 4; GRAMMAR below)
    string slot: NUL-terminated text padded with zeros to (strlen + 0x20) & ~0x1f bytes (0x0041cb60)

Dump:
  fx.json                hash seed and the fx names in offset order, each with its file
  fx/<name>.json         one fx: name and its instructions, one per line: ["0x13", "fxo_glow_red"]
                         (opcode, then operands: numbers for 32-bit words, strings for string slots, a list
                         for a counted list - the count is recomputed)
  fx/<name>.bin          only if an fx does not parse with the grammar (none in the retail files)
Rebuild recomputes the string slot sizes, list counts, every offset and the hash table.
"""
import json
import re
import struct

from . import common
from .words import word_from_json, word_to_json

KIND = "fx"
MAGIC = 0x00EFFEC5

# Operand grammar per jump-table case of 0x00420770: w = 32-bit word, s = string slot,
# L = u32 count + count string slots, W = u32 count + count words. Derived from how each case advances its
# script pointer (Ghidra decompilation of 0x00420770, checked against the jump targets).
GRAMMAR = {
    0x00: "", 0x01: "s", 0x02: "", 0x03: "", 0x04: "ww", 0x05: "wwww", 0x06: "ww", 0x07: "ww", 0x08: "wwww",
    0x09: "wwww", 0x0a: "wwww", 0x0b: "s", 0x0c: "ww", 0x0d: "ww", 0x0e: "ww", 0x0f: "s", 0x10: "", 0x11: "",
    0x12: "", 0x13: "sww", 0x14: "sww", 0x15: "sww", 0x16: "wwwwww", 0x17: "swwwwww", 0x18: "ww", 0x19: "s",
    0x1a: "ww", 0x1b: "www", 0x1c: "ww", 0x1d: "ww", 0x1e: "ww", 0x1f: "ww", 0x20: "ww", 0x21: "ww", 0x22: "ww",
    0x23: "www", 0x24: "www", 0x25: "www", 0x26: "ww", 0x27: "wwww", 0x28: "wwww", 0x29: "wwwwwwwww", 0x2a: "",
    0x2b: "s", 0x2c: "sww", 0x2d: "wwwwww", 0x2e: "", 0x2f: "", 0x30: "", 0x31: "sww", 0x32: "swwwwwwww",
    0x33: "s", 0x34: "Lw", 0x35: "Lw", 0x36: "Lw", 0x37: "www", 0x38: "", 0x39: "", 0x3a: "", 0x3b: "swwww",
    0x3c: "wwwwww", 0x3d: "wwwww", 0x3e: "wwwwww", 0x3f: "wwwwww", 0x40: "wwwwww", 0x41: "wwwww", 0x42: "www",
    0x43: "www", 0x44: "ww", 0x45: "www", 0x46: "www", 0x47: "", 0x48: "", 0x49: "", 0x4a: "", 0x4b: "W",
    0x4c: "w", 0x4d: "s", 0x4e: "wwww", 0x4f: "w", 0x50: "www", 0x51: "www", 0x52: "w", 0x53: "wwww", 0x54: "w",
    0x55: "wwww", 0x56: "www", 0x57: "ww", 0x58: "", 0x59: "ww", 0x5a: "ww", 0x5b: "swwww", 0x5c: "wwwwww",
    0x5d: "wws", 0x5e: "", 0x5f: "wwwwwww", 0x60: "wwwwwww", 0x61: "wwwwwww", 0x62: "ws", 0x63: "wwwww",
    0x64: "wwwww", 0x65: "wwwww", 0x66: "", 0x67: "", 0x68: "ww", 0x69: "s", 0x6a: "s", 0x6b: "", 0x6c: "wwww",
    0x6d: "wwww", 0x6e: "s", 0x6f: "", 0x70: "L",
}


def case_of(op):
    """Opcode -> jump-table case (the byte table at 0x00423250); None = 'unknown opcode' (case 0x71)."""
    if 4 <= op <= 0x4F:
        return op - 4
    if 0x50 <= op <= 0x53:
        return 0x4B
    if 0x54 <= op <= 0x6F:
        return op - 8
    if op in (0x70, 0x71):
        return op - 0x1C
    if 0x72 <= op <= 0x79:
        return op - 10
    if op == 0x104:
        return 0x70
    return None


def slot_size(text_len):
    return (text_len + 0x20) & ~0x1F


def read_str(buf, p, end):
    z = buf.index(b"\0", p, end)
    size = slot_size(z - p)
    if p + size > end:
        raise ValueError("string slot runs past the fx")
    if any(buf[z:p + size]):
        raise ValueError("non-zero bytes in string padding")
    return buf[p:z].decode("latin-1"), p + size


def str_bytes(s):
    b = s.encode("latin-1")
    if b"\0" in b:
        raise ValueError(f"string {s!r} contains NUL")
    return b.ljust(slot_size(len(b)), b"\0")


def parse_script(buf, p, end):
    ins = []
    while p < end:
        op = struct.unpack_from("<I", buf, p)[0]
        p += 4
        case = case_of(op)
        if case is None:
            raise ValueError(f"unknown opcode {op:#x}")
        item = [f"0x{op:02x}"]
        for t in GRAMMAR[case]:
            if p > end:
                break
            if t == "w":
                item.append(word_to_json(struct.unpack_from("<I", buf, p)[0]))
                p += 4
            elif t == "s":
                s, p = read_str(buf, p, end)
                item.append(s)
            elif t in "LW":
                k = struct.unpack_from("<I", buf, p)[0]
                p += 4
                lst = []
                for _ in range(k):
                    if p >= end:
                        raise ValueError("list runs past the fx")
                    if t == "L":
                        s, p = read_str(buf, p, end)
                        lst.append(s)
                    else:
                        lst.append(word_to_json(struct.unpack_from("<I", buf, p)[0]))
                        p += 4
                item.append(lst)
        if p > end:
            raise ValueError(f"instruction {item[0]} runs past the fx")
        ins.append(item)
    return ins


def script_bytes(ins, what="fx"):
    out = bytearray()
    for item in ins:
        op = int(item[0], 0) if isinstance(item[0], str) else item[0]
        case = case_of(op)
        if case is None:
            raise ValueError(f"{what}: unknown opcode {item[0]}")
        g = GRAMMAR[case]
        args = item[1:]
        if len(args) != len(g):
            raise ValueError(f"{what}: opcode {item[0]} takes {len(g)} operands ({g or 'none'}), got {len(args)}")
        out += struct.pack("<I", op)
        for t, a in zip(g, args):
            if t == "w":
                out += struct.pack("<I", word_from_json(a))
            elif t == "s":
                out += str_bytes(a)
            elif t == "L":
                out += struct.pack("<I", len(a)) + b"".join(str_bytes(s) for s in a)
            elif t == "W":
                out += struct.pack("<I", len(a)) + b"".join(struct.pack("<I", word_from_json(v)) for v in a)
    return bytes(out)


def parse(payload):
    hd, area = common.parse(payload, 0)
    data = payload[area:]
    offsets = hd.u32_values()
    order = sorted(set(offsets))
    if len(order) != len(offsets) or (order and order[0] != 0):
        raise ValueError("fx offsets are not distinct or do not start at 0")
    bounds = order + [len(data)]
    entries = []
    for i, o in enumerate(order):
        end = bounds[i + 1]
        z = data.index(b"\0", o, end)
        name = data[o:z]
        p = o + slot_size(len(name))
        if any(data[z:p]):
            raise ValueError(f"fx at {o}: non-zero name padding")
        body = data[p:end]
        try:
            script = parse_script(data, p, end)
            if script_bytes(script) != body:
                raise ValueError("re-encoding differs")
        except (ValueError, IndexError) as e:
            script = None
        entries.append({"name": name, "script": script, "body": body})
    return hd, entries


def build_payload(seed, entries):
    """entries: list of (name bytes, body bytes) in storage order."""
    area = bytearray()
    offsets = []
    for name, body in entries:
        if b"\0" in name:
            raise ValueError(f"fx name {name!r} contains NUL")
        offsets.append(len(area))
        area += name.ljust(slot_size(len(name)), b"\0") + body
    hd = common.build([n for n, _ in entries], offsets, seed)
    return hd.to_bytes(sized=True) + bytes(area)


def _file_names(names):
    used, files = set(), []
    for i, n in enumerate(names):
        s = n.decode("latin-1")
        base = s if re.fullmatch(r"[A-Za-z0-9_.\-]+", s) and s.lower() not in used else \
            f"{i:04d}_" + re.sub(r"[^A-Za-z0-9_.\-]", "_", s)
        used.add(base.lower())
        files.append(f"fx/{base}")
    return files


def _script_json(name, script):
    lines = ",\n  ".join(json.dumps(item) for item in script)
    return '{\n "name": %s,\n "script": [\n  %s\n ]\n}\n' % (json.dumps(name), lines)


def dump(payload, out):
    hd, entries = parse(payload)
    if build_payload(hd.seed, [(e["name"], e["body"]) for e in entries]) != payload:
        raise AssertionError("stored hash table / layout differs from the one rebuilt from the parts")
    (out / "fx").mkdir(parents=True, exist_ok=True)
    files = _file_names([e["name"] for e in entries])
    index = []
    for e, f in zip(entries, files):
        name = e["name"].decode("latin-1")
        if e["script"] is not None:
            f += ".json"
            (out / f).write_text(_script_json(name, e["script"]))
        else:
            f += ".bin"
            (out / f).write_bytes(e["body"])
        index.append({"name": name, "file": f})
    meta = {"kind": KIND, "hash_seed": f"0x{hd.seed:08X}",
            "note": "Order = storage order of the fx scripts. A .json file's own 'name' is ignored; the name "
                    "here is used. See docs/formats/big-fx.md for the opcode grammar.",
            "fx": index}
    (out / "fx.json").write_text(json.dumps(meta, indent=1))


def rebuild(src):
    meta = json.loads((src / "fx.json").read_text())
    entries = []
    for e in meta["fx"]:
        path = src / e["file"]
        if path.suffix == ".json":
            body = script_bytes(json.loads(path.read_text())["script"], e["file"])
        else:
            body = path.read_bytes()
        entries.append((e["name"].encode("latin-1"), body))
    return build_payload(int(meta["hash_seed"], 16), entries)
