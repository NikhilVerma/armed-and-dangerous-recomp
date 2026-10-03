"""Helpers shared by the aud, fx and ff kinds: 32-bit words and fixed-size strings in JSON, exactly.

word_to_json(u)   a u32 as a JSON number: a float (shortest decimal that packs back to the same float32 bits)
                  when the bits are a normal float or -0.0, else the integer (0, small ints, NaN/inf patterns,
                  denormals). Both forms round-trip bit-exactly through word_from_json.
word_from_json(v) int -> u32 (negative ints wrap), float -> float32 bits.
str_to_json(b)    a NUL-padded byte field as a str when everything after the first NUL is zero, else
                  {"s": text, "raw": hex of the whole field}.
str_from_json(v, size)  back to `size` bytes.
"""
import math
import struct


def _f32(x):
    return struct.unpack("<I", struct.pack("<f", x))[0]


def word_to_json(u):
    u &= 0xFFFFFFFF
    mag = u & 0x7FFFFFFF
    if (mag >= 0x7F800000) or (mag < 0x00800000 and u != 0x80000000):
        return u
    x = struct.unpack("<f", struct.pack("<I", u))[0]
    for p in range(1, 10):
        y = float(f"{x:.{p}g}")
        if _f32(y) == u:
            return y
    return x


def word_from_json(v):
    if isinstance(v, bool):
        raise ValueError(f"not a word: {v!r}")
    if isinstance(v, int):
        if not -0x80000000 <= v <= 0xFFFFFFFF:
            raise ValueError(f"integer {v} does not fit 32 bits")
        return v & 0xFFFFFFFF
    if isinstance(v, float):
        if math.isnan(v) or math.isinf(v):
            raise ValueError("NaN/inf must be given as integer bit patterns")
        return _f32(v)
    raise ValueError(f"not a word: {v!r}")


def words_bytes(values):
    return b"".join(struct.pack("<I", word_from_json(v)) for v in values)


def str_to_json(b):
    s = b.split(b"\0")[0]
    if any(b[len(s):]):
        return {"s": s.decode("latin-1"), "raw": b.hex()}
    return s.decode("latin-1")


def str_from_json(v, size, what="string"):
    if isinstance(v, dict):
        raw = bytes.fromhex(v["raw"])
        if raw.split(b"\0")[0].decode("latin-1") == v["s"] and len(raw) == size:
            return raw
        v = v["s"]  # the text was edited: drop the stale bytes behind it
    b = v.encode("latin-1")
    if len(b) >= size:
        raise ValueError(f"{what} {v!r} longer than {size - 1} bytes")
    return b.ljust(size, b"\0")
