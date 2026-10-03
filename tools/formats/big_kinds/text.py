"""String bank: `*_big01_text.big`, magic 0xDEADC0DE, loader 0x00496060 TextData_Load.
Layout and safe edits: docs/formats/big-text.md.

Payload (read whole into "text data", fixed up in place):
  name hash table (common.py; V = 36: char name[32] + u32 string index; compare 0x00495ee0 = _stricmp)
  u32 count                              number of strings (= hash entries)
  u32 strings_bytes                      byte size of the string area (skipped by the loader)
  u32 offset[count]                      byte offset of string i from the start of the string area
  string area: count x UTF-16LE string, NUL-terminated (2 zero bytes), packed in index order, no padding

Dump:
  strings.json   hash seed, and per string (index order) its id (the hash key) and text (UTF-16 decoded)

Rebuild recomputes the hash table, count, area size and every offset from strings.json, so texts may change
length and strings may be added, removed or renamed.
"""
import json
import struct

from . import common

KIND = "text"
MAGIC = 0xDEADC0DE
NAME = 32


def parse(payload):
    hd, off = common.parse(payload, 0)
    count, area = struct.unpack_from("<II", payload, off)
    offs = struct.unpack_from(f"<{count}I", payload, off + 8)
    base = off + 8 + 4 * count
    if base + area != len(payload):
        raise ValueError(f"string area of {area} bytes at {base} does not end the payload ({len(payload)})")
    by_index = {}
    for v in hd.values:
        by_index[struct.unpack_from("<I", v, NAME)[0]] = v[:NAME].split(b"\0")[0]
    if sorted(by_index) != list(range(count)):
        raise ValueError("hash values are not a permutation of the string indices")
    strings = []
    for i in range(count):
        o = base + offs[i]
        e = o
        while payload[e:e + 2] != b"\0\0":
            e += 2
        strings.append((by_index[i], payload[o:e]))
    return hd, strings


def build_payload(seed, strings):
    """strings: list of (id bytes, UTF-16LE bytes without terminator)."""
    names = [n for n, _ in strings]
    for n in names:
        if len(n) > NAME - 1:
            raise ValueError(f"string id {n!r} longer than {NAME - 1} bytes")
    values = [n.ljust(NAME, b"\0") + struct.pack("<I", i) for i, n in enumerate(names)]
    out = bytearray(common.build(names, values, seed, NAME + 4).to_bytes(sized=True))
    area = bytearray()
    offs = []
    for _, s in strings:
        offs.append(len(area))
        area += s + b"\0\0"
    out += struct.pack(f"<II{len(offs)}I", len(strings), len(area), *offs)
    return bytes(out + area)


def dump(payload, out):
    hd, strings = parse(payload)
    entries = [{"id": n.decode("latin-1"), "text": s.decode("utf-16-le", "surrogatepass")} for n, s in strings]
    meta = {"kind": KIND, "hash_seed": f"0x{hd.seed:08X}",
            "note": "Order = string index. Edit text freely; ids are looked up case-insensitively (max 31 "
                    "bytes). See docs/formats/big-text.md.",
            "strings": entries}
    (out / "strings.json").write_text(json.dumps(meta, indent=1, ensure_ascii=False), encoding="utf-8")
    if build_payload(hd.seed, strings) != payload:
        raise AssertionError("text payload is not reproduced from its parts")


def rebuild(src):
    meta = json.loads((src / "strings.json").read_text(encoding="utf-8"))
    strings = []
    for e in meta["strings"]:
        s = e["text"].encode("utf-16-le", "surrogatepass")
        if b"\0\0" in [s[k:k + 2] for k in range(0, len(s), 2)]:
            raise ValueError(f"string {e['id']!r} contains a NUL character")
        strings.append((e["id"].encode("latin-1"), s))
    return build_payload(int(meta["hash_seed"], 16), strings)
