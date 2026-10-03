"""The name hash table that most `.big` payloads start with (docs/formats/big-common.md).

In memory the game keeps a 0x20-byte table object; on disk only its "entries" block is stored, prefixed by its
byte size. Every loader reads that size, reads the block, and calls 0x004f1220 (HashTable_FixupInPlace) on it:

  u32 dir_bytes                       size of the block below (read separately by the loader)
  block:
    u32 bucket_count  B               the smallest prime > n (all 225 files of the nine kinds)
    u32 seed                          hash seed, read by the hash 0x004f1100
    u32 value_size    V               bytes of each entry's value (4 for most kinds, 36 for text and prop)
    i32 heads[B]                      first entry index of the bucket's chain, -1 = empty
    n x { u32 next; u8 value[V] }     next = index + 1 of the following entry in the chain, 0xFFFFFFFF = end

Chains are contiguous runs of entries, stored in bucket order (0x004f1220 walks the entries sequentially for
each non-empty bucket and turns `next` into a pointer to the physically following entry or null). So heads and
next are derived values: given each entry's bucket in stored order, the block is fully determined.

The hash (0x004f1100) of a key string: sum over its bytes of (seed XOR c), c the signed char with A-Z
lowered, modulo B. Lookups (0x004f1160) hash the key, walk the chain and call the table's compare callback.
Keys are not stored in the table; the value is usually an index into the kind's own list, from which the
callback fetches the name (txtr: 0x00488150 compares with _stricmp against texture[value].name).

API
  parse(buf, off=0) -> (HashDir, end)       parse a sized block starting at `off` (the u32 dir_bytes)
  parse_block(buf, off, size) -> HashDir    parse the block without the size prefix
  HashDir.to_bytes(sized=True)              serialise; heads/next recomputed from the entries' buckets
  build(keys, values, seed, value_size)     a new table from names, in the original tool's order
  hash_key(key, seed, buckets)              the game's hash
  next_prime(n)                             the bucket count rule

CLI: python tools/formats/big_kinds/common.py check [glob ...]   header-level parse of each file
"""
import struct
from dataclasses import dataclass, field

END = 0xFFFFFFFF


def next_prime(n):
    """Smallest prime strictly greater than n: the bucket count of every table in the retail files."""
    x = n + 1
    while x < 2 or any(x % p == 0 for p in range(2, int(x ** 0.5) + 1)):
        x += 1
    return x


def hash_key(key, seed, buckets):
    """0x004f1100: sum of (seed ^ c) over the key's chars (signed, A-Z lowered), mod bucket count."""
    if isinstance(key, str):
        key = key.encode("latin-1")
    total = 0
    for c in key:
        if c == 0:
            break
        if 0x41 <= c <= 0x5A:
            c += 0x20
        elif c >= 0x80:
            c -= 0x100  # movsx
        total = (total + ((seed ^ (c & 0xFFFFFFFF)) & 0xFFFFFFFF)) & 0xFFFFFFFF
    return total % buckets


@dataclass
class HashDir:
    seed: int
    value_size: int
    bucket_count: int
    buckets: list = field(default_factory=list)  # bucket of each entry, in stored order
    values: list = field(default_factory=list)   # value bytes of each entry, in stored order

    def __len__(self):
        return len(self.values)

    def block_size(self):
        return 12 + 4 * self.bucket_count + len(self.values) * (4 + self.value_size)

    def to_bytes(self, sized=True):
        n = len(self.values)
        order = sorted(range(n), key=lambda i: self.buckets[i])
        if order != list(range(n)):
            raise ValueError("entries must be stored grouped by bucket in ascending bucket order")
        heads = [-1] * self.bucket_count
        nexts = []
        for i, b in enumerate(self.buckets):
            if not 0 <= b < self.bucket_count:
                raise ValueError(f"entry {i}: bucket {b} out of range")
            if heads[b] == -1:
                heads[b] = i
            last = i + 1 == n or self.buckets[i + 1] != b
            nexts.append(END if last else i + 1)
        out = bytearray()
        if sized:
            out += struct.pack("<I", self.block_size())
        out += struct.pack("<III", self.bucket_count, self.seed, self.value_size)
        out += struct.pack(f"<{self.bucket_count}i", *heads)
        for nx, v in zip(nexts, self.values):
            if len(v) != self.value_size:
                raise ValueError(f"value of {len(v)} bytes, table holds {self.value_size}")
            out += struct.pack("<I", nx) + v
        return bytes(out)

    def u32_values(self):
        return [struct.unpack("<I", v)[0] for v in self.values]

    def meta(self):
        """JSON-able header fields (heads and next are derived and not kept)."""
        return {"seed": f"0x{self.seed:08X}", "value_size": self.value_size, "bucket_count": self.bucket_count}

    def to_json(self):
        """Generic JSON form for kinds whose keys are not known: per-entry bucket and value (hex)."""
        d = self.meta()
        d["entries"] = [{"bucket": b, "value": v.hex()} for b, v in zip(self.buckets, self.values)]
        return d

    @classmethod
    def from_json(cls, d):
        return cls(int(d["seed"], 16), d["value_size"], d["bucket_count"],
                   [e["bucket"] for e in d["entries"]], [bytes.fromhex(e["value"]) for e in d["entries"]])


def parse_block(buf, off, size):
    """Parse the block (no size prefix) of `size` bytes at `off`; strict: raises on anything inconsistent."""
    bucket_count, seed, value_size = struct.unpack_from("<III", buf, off)
    stride = 4 + value_size
    rest = size - 12 - 4 * bucket_count
    if rest < 0 or rest % stride:
        raise ValueError(f"block of {size} bytes does not fit B={bucket_count}, V={value_size}")
    n = rest // stride
    heads = struct.unpack_from(f"<{bucket_count}i", buf, off + 12)
    e0 = off + 12 + 4 * bucket_count
    nexts = [struct.unpack_from("<I", buf, e0 + i * stride)[0] for i in range(n)]
    values = [bytes(buf[e0 + i * stride + 4:e0 + (i + 1) * stride]) for i in range(n)]
    buckets = []
    pos = 0
    for b, h in enumerate(heads):
        if h == -1:
            continue
        if h != pos:
            raise ValueError(f"bucket {b}: head {h}, expected {pos} (chains not contiguous)")
        while True:
            if pos >= n:
                raise ValueError(f"bucket {b}: chain runs past the last entry")
            buckets.append(b)
            nx = nexts[pos]
            pos += 1
            if nx == END:
                break
            if nx != pos:
                raise ValueError(f"entry {pos - 1}: next {nx}, expected {pos}")
    if pos != n:
        raise ValueError(f"{n - pos} entries not reachable from any bucket")
    hd = HashDir(seed, value_size, bucket_count, buckets, values)
    if hd.to_bytes(sized=False) != bytes(buf[off:off + size]):
        raise AssertionError("hash block does not re-serialise exactly")
    return hd


def parse(buf, off=0):
    """Parse `u32 size` + block at `off`. Returns (HashDir, offset after the block)."""
    size = struct.unpack_from("<I", buf, off)[0]
    return parse_block(buf, off + 4, size), off + 4 + size


def build(keys, values, seed, value_size=4, bucket_count=None):
    """A table over keys[i] -> values[i] (bytes or int) as the original tool laid it out: B = next prime > n,
    entries grouped by bucket ascending, and within a bucket the most recently inserted (highest i) first."""
    n = len(keys)
    bucket_count = bucket_count or next_prime(n)
    vals = [struct.pack("<I", v) if isinstance(v, int) else bytes(v) for v in values]
    bk = [hash_key(k, seed, bucket_count) for k in keys]
    order = sorted(range(n), key=lambda i: (bk[i], -i))
    return HashDir(seed, value_size, bucket_count, [bk[i] for i in order], [vals[i] for i in order])


# Where the sized hash block starts in each kind's payload (see docs/formats/big-common.md).
DIR_OFFSET = {"text": 0, "txtr": 0, "anim": 0, "skin": 0, "prop": 0, "obj": 0, "aud": 0, "fx": 0, "ff": 4}


def _check(patterns):
    import glob
    import pathlib
    import re
    root = pathlib.Path(__file__).resolve().parents[3]
    pats = patterns or [str(root / "orig" / "pc" / "GameData" / "**" / f"*_{k}.big") for k in DIR_OFFSET]
    files = sorted(f for p in pats for f in glob.glob(p, recursive=True))
    summary = {}
    for f in files:
        kind = re.search(r"_([a-z]+)\.big$", f).group(1)
        data = pathlib.Path(f).read_bytes()[8:]
        try:
            hd, end = parse(data, DIR_OFFSET.get(kind, 0))
            ok = hd.bucket_count == next_prime(len(hd))
            res = "ok" if ok else "ok (bucket count not next prime)"
        except Exception as e:  # report and continue
            res = f"FAIL {type(e).__name__}: {e}"
        summary.setdefault((kind, res), []).append(pathlib.Path(f).name)
    for (kind, res), names in sorted(summary.items()):
        print(f"{kind:5} {len(names):3} {res}" + ("" if res.startswith("ok") else f"  e.g. {names[0]}"))


if __name__ == "__main__":
    import sys
    if len(sys.argv) >= 2 and sys.argv[1] == "check":
        _check(sys.argv[2:])
    else:
        print(__doc__)
