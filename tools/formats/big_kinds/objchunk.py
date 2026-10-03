"""In-place object chunks and the banks that hold them: shared by `anim` (`*_big03_anim.big`) and `skin`
(`*_big05_skin.big`). Layout and evidence: docs/formats/big-anim.md and docs/formats/big-skin.md.

Bank payload (ObjectSystem_LoadAnims 0x0045b430, ObjectSystem_LoadSkins 0x0045af30):
  name hash table (common.py; value = asset index; key = asset name, see below)
  u32 count
  count x chunk          anim: back to back; skin: each followed by 1..32 zero bytes (cursor += (size & ~31) + 32)

One chunk is a dump of a small heap that the game uses where it lies (0x004e1db0, then a typed relocation walk):
  +0x00 u32 size         bytes of the whole chunk (the anim loader 0x004e8430 reads 12 bytes, then size - 12)
  +0x04 u32 align        log2 of the largest block alignment (2 or 4) - measured, no reader found
  +0x08 u32 base         chunk offset of the heap header block (always the last 0x30 bytes)
  +0x0c blocks           { u32 next, u32 size, u16 flags, u16 align_log2, data } in a ring through `next`
  heap header (base): a block whose +0x10 starts the ring, +0x14 = sum of block sizes, +0x18 = block count,
                      +0x1c = 2, +0x20 = the root object
Every pointer inside is a chunk offset; the game adds the chunk address to exactly the fields its type walk
(0x004ef620 anim, 0x004eae90 skin) visits, so a chunk is relocatable as a whole but its blocks cannot be moved
without knowing every type. The root object holds the source path at +0x34 (e.g.
"C:\\dev\\intermediate\\common\\skn\\captain_1.SKN"); the bank's lookup key is that file name without its
last four characters (compare callbacks 0x0045b200 / 0x0045ad40: strrchr '\\' + 1, _strnicmp over len - 4).
"""
import json
import re
import struct
from dataclasses import dataclass, field

from . import common

HEADER = 0x0C        # chunk header: size, align, base
BASE_SIZE = 0x30     # the heap header block (0x004e1db0 builds the same 0x30-byte header for a new heap)
ROOT_PATH = 0x34     # root object: char path[] (0x0045b200 / 0x0045ad40 read root + 0x34)


def u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


def u16(b, o):
    return struct.unpack_from("<H", b, o)[0]


def cstr(b, o, limit=1024):
    end = b.find(b"\0", o, o + limit)
    if end < 0:
        raise ValueError(f"unterminated string at 0x{o:x}")
    return bytes(b[o:end]).decode("latin-1")


@dataclass
class Chunk:
    data: bytes
    align: int                  # chunk +4
    base: int                   # chunk +8
    root: int                   # heap header +0x20
    path: str                   # root +0x34
    blocks: list = field(default_factory=list)  # (offset, size, flags, align_log2) in ring order

    @property
    def name(self):
        return key_of(self.path)


def key_of(path):
    """The name the bank's hash table and compare callback use: after the last '\\', minus 4 characters."""
    if "\\" not in path:
        raise ValueError(f"source path {path!r} has no '\\' (the game's compare would read address 1)")
    stem = path.rsplit("\\", 1)[1]
    if len(stem) <= 4:
        raise ValueError(f"source path {path!r}: file name too short")
    return stem[:-4]


def parse_chunk(data, what="chunk"):
    """Strict structural parse of one chunk (bytes of exactly one chunk)."""
    if len(data) < HEADER + BASE_SIZE:
        raise ValueError(f"{what}: {len(data)} bytes is too small for a chunk")
    size, align, base = struct.unpack_from("<III", data, 0)
    if size != len(data):
        raise ValueError(f"{what}: size word {size} != {len(data)} bytes")
    if base + BASE_SIZE != size:
        raise ValueError(f"{what}: heap header at 0x{base:x} is not the last 0x30 bytes")
    start = base + 0x10
    if u32(data, base) != start:
        raise ValueError(f"{what}: heap header +0 does not point at its own ring node")
    blocks = []
    node = u32(data, start)
    while node != start:
        if not HEADER <= node <= size - 12 or len(blocks) > size // 12:
            raise ValueError(f"{what}: block ring broken at 0x{node:x}")
        blocks.append((node, u32(data, node + 4), u16(data, node + 8), u16(data, node + 10)))
        node = u32(data, node)
    if not blocks or blocks[-1][0] != base:
        raise ValueError(f"{what}: block ring does not end at the heap header")
    if u32(data, base + 0x18) != len(blocks) or u32(data, base + 0x14) != sum(b[1] for b in blocks):
        raise ValueError(f"{what}: heap header block count/total does not match the ring")
    root = u32(data, base + 0x20)
    if not HEADER <= root < base:
        raise ValueError(f"{what}: root 0x{root:x} outside the chunk")
    path = cstr(data, root + ROOT_PATH)
    return Chunk(bytes(data), align, base, root, path, blocks)


def json_text(obj):
    """json.dumps(indent=1) with every innermost list on one line (JSON strings hold no raw newlines, so only
    layout whitespace is touched)."""
    text = json.dumps(obj, indent=1)
    def one_line(mt):
        s = re.sub(r"\[\n\s*", "[", mt.group(0))
        s = re.sub(r"\n\s*\]", "]", s)
        return re.sub(r"\n\s*", " ", s)
    return re.sub(r"\[[^\[\]{}]*\]", one_line, text) + "\n"


def chunk_summary(ch):
    """Small JSON-able description of the container level of a chunk (informational)."""
    return {"source_path": ch.path, "bytes": len(ch.data), "align_log2": ch.align, "blocks": len(ch.blocks),
            "root": f"0x{ch.root:x}"}


# ---------------------------------------------------------------------------------------------------------------
# The generic dictionary the objects use (fixed up by 0x004f0e60 / Reloc_FixupIndexedTable 0x004e1320).
# container: +0 heap header, +4 table, +8 flags (bit 0: second index by id, bit 1: case-insensitive names)
# table: +0x1c count, +0x20 index count, +0x24 index array (0x1c bytes each; +0x10 = entry pointer array)
# entry: u16 size, u16 value offset v; value dword at +v, name string at +v+4; when flags bit 0: u16 w at +4,
#        refcount at +w, id at +w+2 (compare 0x004f0be0).
def dict_entries(b, cont):
    if not cont:
        return []
    t = u32(b, cont + 4)
    n, rows = u32(b, t + 0x1c), u32(b, t + 0x20)
    if n == 0 or rows == 0:
        return []
    arr = u32(b, u32(b, t + 0x24) + 0x10)
    return [u32(b, arr + 4 * i) for i in range(n)]


def dict_items(b, cont):
    """[(name, value dword, id or None)] in the dictionary's first index order."""
    flags = u32(b, cont + 8) if cont else 0
    out = []
    for e in dict_entries(b, cont):
        v = u16(b, e + 2)
        ident = None
        if flags & 1:
            w = u16(b, e + 4)
            ident = u16(b, e + w + 2)
        out.append((cstr(b, e + v + 4), u32(b, e + v), ident))
    return out


# ---------------------------------------------------------------------------------------------------------------
# The bank: hash table + count + chunks.
def split_bank(payload, padded):
    hd, off = common.parse(payload, 0)
    count = u32(payload, off)
    off += 4
    chunks = []
    for i in range(count):
        size = u32(payload, off)
        chunks.append(parse_chunk(payload[off:off + size], f"asset {i}"))
        nxt = off + ((size & ~0x1F) + 0x20 if padded else size)
        if padded and payload[off + size:nxt].strip(b"\0"):
            raise ValueError(f"asset {i}: non-zero padding")
        off = nxt
    if off != len(payload):
        raise ValueError(f"chunks end at {off}, payload is {len(payload)} bytes")
    if len(hd) != count:
        raise ValueError(f"hash table has {len(hd)} entries, count word says {count}")
    return hd, chunks


def join_bank(seed, chunks, padded, table=None):
    """chunks: list of chunk bytes. The hash table is rebuilt from the chunks' own names unless `table` (a
    common.HashDir kept verbatim) is given."""
    names = [parse_chunk(c, f"asset {i}").name for i, c in enumerate(chunks)]
    hd = table or common.build(names, list(range(len(chunks))), seed)
    out = bytearray(hd.to_bytes(sized=True))
    out += struct.pack("<I", len(chunks))
    for c in chunks:
        out += c
        if padded:
            out += b"\0" * ((len(c) & ~0x1F) + 0x20 - len(c))
    return bytes(out)


def file_names(names, folder, ext):
    used, files = set(), []
    for i, s in enumerate(names):
        base = s if re.fullmatch(r"[A-Za-z0-9_.\-]+", s) and s.lower() not in used else \
            f"{i:04d}_" + re.sub(r"[^A-Za-z0-9_.\-]", "_", s)
        used.add(base.lower())
        files.append(f"{folder}/{base}.{ext}")
    return files


def dump_bank(payload, out, kind, folder, ext, padded, note, extra=None):
    """Write <folder>/<name>.<ext> per chunk and <folder>.json. `extra(chunk, out, file)` may add files and
    returns a dict merged into the asset's JSON entry."""
    hd, chunks = split_bank(payload, padded)
    (out / folder).mkdir(parents=True, exist_ok=True)
    files = file_names([c.name for c in chunks], folder, ext)
    entries = []
    for c, f in zip(chunks, files):
        (out / f).write_bytes(c.data)
        e = {"name": c.name, "file": f}
        e.update(chunk_summary(c))
        if extra:
            e.update(extra(c, out, f))
        entries.append(e)
    meta = {"kind": kind, "hash_seed": f"0x{hd.seed:08X}", "note": note, folder: entries}
    if common.build([c.name for c in chunks], list(range(len(chunks))), hd.seed).to_bytes() != hd.to_bytes():
        meta["hash_table"] = hd.to_json()  # never seen in the retail files; keeps the dump exact anyway
    (out / f"{folder}.json").write_text(json_text(meta))


def rebuild_bank(src, folder, padded):
    meta = json.loads((src / f"{folder}.json").read_text())
    chunks = []
    for i, e in enumerate(meta[folder]):
        data = (src / e["file"]).read_bytes()
        ch = parse_chunk(data, e["file"])
        if ch.name.lower() != e["name"].lower():
            raise ValueError(f"{e['file']}: the chunk's own name is {ch.name!r} (from {ch.path!r}) but "
                             f"{folder}.json says {e['name']!r}; the game looks assets up by the chunk's name")
        chunks.append(data)
    table = common.HashDir.from_json(meta["hash_table"]) if "hash_table" in meta else None
    return join_bank(int(meta["hash_seed"], 16), chunks, padded, table)
