"""Sound bank: `*_big08_aud.big`, magic 0xDEAFDEAF, loader 0x004fab80 (renderer_2c vtable +0x80; not a Ghidra
function yet). Layout and safe edits: docs/formats/big-aud.md.

Payload:
  name hash table (common.py, sized; value = sound index; key = sound name, compare 0x004f4cc0)
  u32 count                         -> audio +0x78
  u32 unknown                       -> audio +0x8c (no reader found; kept verbatim)
  count x sound:
    0xfc-byte header (below)
    if header.flags & 0x40 (streamed):  char path[0x80] ("streams\\x.raw", stale bytes after the NUL),
                                        u32 k, k x char name[32] (follow-on stream names)
    else:                               u8 pcm[header.size]
  header:
    +0x00 char name[32]   +0x20 u32 flags   +0x24..+0x7b 22 words (unknown; volume/distance floats etc.)
    +0x7c, +0x9c, +0xbc  char[32] each (linked sound names, e.g. "<x>_start", "none")
    +0xdc u16 format tag (0 in the file; the game always creates WAVE_FORMAT_PCM, 0x004fa6d0)
    +0xde u16 channels  +0xe0 u32 rate  +0xe4 u32 bytes/s  +0xe8 u16 block align  +0xea u16 bits
    +0xec u32 0  +0xf0 u32 size (PCM bytes, or the stream file's size)  +0xf4 u32 frames  +0xf8 u32 0 (data ptr)

Dump:
  sounds.json              hash seed, the +0x8c word, and per sound (index order) its header fields
  sounds/<name>.wav        in-memory sounds as standard PCM WAV (the stored samples, byte for byte)
Rebuild recomputes the hash table, count, and for WAV sounds channels, rate, bits, block align, bytes/s, size
and frames from the WAV file.
"""
import json
import re
import struct

from . import common
from .words import str_from_json, str_to_json, word_from_json, word_to_json

KIND = "aud"
MAGIC = 0xDEAFDEAF

HEADER = 0xFC
STREAMED = 0x40
PARAMS = range(0x24, 0x7C, 4)
LINKS = (0x7C, 0x9C, 0xBC)
PATH = 0x80


def _derived(ch, rate, bits, size):
    ba = ch * bits // 8
    return {"block_align": ba, "bytes_per_sec": rate * ba, "frames": size // ba if ba else 0}


def parse(payload):
    hd, off = common.parse(payload, 0)
    count, x8c = struct.unpack_from("<II", payload, off)
    off += 8
    if count != len(hd):
        raise ValueError(f"count {count} != hash entries {len(hd)}")
    sounds = []
    for _ in range(count):
        h = payload[off:off + HEADER]
        off += HEADER
        flags = struct.unpack_from("<I", h, 0x20)[0]
        size = struct.unpack_from("<I", h, 0xf0)[0]
        s = {"header": h}
        if flags & STREAMED:
            s["path"] = payload[off:off + PATH]
            k = struct.unpack_from("<I", payload, off + PATH)[0]
            off += PATH + 4
            s["next"] = [payload[off + 32 * i:off + 32 * (i + 1)] for i in range(k)]
            off += 32 * k
        else:
            s["pcm"] = payload[off:off + size]
            off += size
        sounds.append(s)
    if off != len(payload):
        raise ValueError(f"sounds end at {off}, payload is {len(payload)} bytes")
    return hd, x8c, sounds


def header_to_json(h, streamed):
    d = {"name": str_to_json(h[:32]), "flags": f"0x{struct.unpack_from('<I', h, 0x20)[0]:08X}"}
    params = {f"0x{o:02x}": word_to_json(struct.unpack_from("<I", h, o)[0]) for o in PARAMS
              if struct.unpack_from("<I", h, o)[0]}
    d["params"] = params
    d["links"] = [str_to_json(h[o:o + 32]) for o in LINKS]
    tag, ch, rate, bps, ba, bits, z_ec, size, frames, z_f8 = struct.unpack_from("<HHIIHHIIII", h, 0xdc)
    der = _derived(ch, rate, bits, size)
    if streamed:
        d.update(channels=ch, rate=rate, bits=bits, stream_size=size)
    # Anything that the rebuild would not derive is kept as an explicit override.
    extra = {}
    if tag:
        extra["format_tag"] = tag
    if ba != der["block_align"]:
        extra["block_align"] = ba
    if bps != der["bytes_per_sec"]:
        extra["bytes_per_sec"] = bps
    if frames != der["frames"]:
        extra["frames"] = frames
    if z_ec:
        extra["word_ec"] = z_ec
    if z_f8:
        extra["word_f8"] = z_f8
    if extra:
        d["override"] = extra
    return d


def header_from_json(d, ch, rate, bits, size):
    h = bytearray(HEADER)
    h[0:32] = str_from_json(d["name"], 32, "sound name")
    struct.pack_into("<I", h, 0x20, int(d["flags"], 16))
    for k, v in d.get("params", {}).items():
        o = int(k, 16)
        if o not in PARAMS:
            raise ValueError(f"{d['name']}: param offset {k} is outside 0x24..0x78")
        struct.pack_into("<I", h, o, word_from_json(v))
    links = d.get("links", ["", "", ""])
    if len(links) != 3:
        raise ValueError(f"{d['name']}: links must have 3 entries")
    for o, v in zip(LINKS, links):
        h[o:o + 32] = str_from_json(v, 32, "link name")
    der = _derived(ch, rate, bits, size)
    ov = d.get("override", {})
    struct.pack_into("<HHIIHHIIII", h, 0xdc, ov.get("format_tag", 0), ch, rate,
                     ov.get("bytes_per_sec", der["bytes_per_sec"]), ov.get("block_align", der["block_align"]),
                     bits, ov.get("word_ec", 0), size, ov.get("frames", der["frames"]), ov.get("word_f8", 0))
    return bytes(h)


# ---------------------------------------------------------------------------------------------------------------
# WAV
def wav_bytes(ch, rate, bits, pcm):
    ba = ch * bits // 8
    fmt = struct.pack("<HHIIHH", 1, ch, rate, rate * ba, ba, bits)
    body = b"WAVE" + b"fmt " + struct.pack("<I", len(fmt)) + fmt + b"data" + struct.pack("<I", len(pcm)) + pcm
    if len(pcm) & 1:
        body += b"\0"
    return b"RIFF" + struct.pack("<I", len(body)) + body


def wav_parse(data, what="wav"):
    """-> (channels, rate, bits, pcm bytes). Plain PCM only (format 1, or EXTENSIBLE with the PCM subtype)."""
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError(f"{what}: not a RIFF/WAVE file")
    p, fmt, pcm = 12, None, None
    while p + 8 <= len(data):
        cid, n = data[p:p + 4], struct.unpack_from("<I", data, p + 4)[0]
        body = data[p + 8:p + 8 + n]
        if cid == b"fmt ":
            fmt = body
        elif cid == b"data":
            pcm = body
        p += 8 + n + (n & 1)
    if fmt is None or pcm is None:
        raise ValueError(f"{what}: missing fmt or data chunk")
    tag, ch, rate, _, _, bits = struct.unpack_from("<HHIIHH", fmt)
    if tag == 0xFFFE and len(fmt) >= 40 and fmt[24:26] == b"\x01\x00":
        tag = 1
    if tag != 1:
        raise ValueError(f"{what}: format tag {tag:#x}; the game only plays uncompressed PCM - save as PCM WAV")
    if bits not in (8, 16):
        raise ValueError(f"{what}: {bits}-bit samples; use 16-bit (all retail sounds) or 8-bit PCM")
    if len(pcm) % (ch * bits // 8):
        raise ValueError(f"{what}: data size {len(pcm)} is not a whole number of frames")
    return ch, rate, bits, pcm


# ---------------------------------------------------------------------------------------------------------------
def _name(h):
    return h[:32].split(b"\0")[0]


def build_payload(seed, x8c, sounds):
    """sounds: list of (header bytes, tail bytes) in index order."""
    hd = common.build([_name(h) for h, _ in sounds], list(range(len(sounds))), seed)
    out = bytearray(hd.to_bytes(sized=True))
    out += struct.pack("<II", len(sounds), x8c)
    for h, tail in sounds:
        out += h + tail
    return bytes(out)


def _tail(s):
    if "pcm" in s:
        return s["pcm"]
    return s["path"] + struct.pack("<I", len(s["next"])) + b"".join(s["next"])


def _file_names(names):
    used, files = set(), []
    for i, n in enumerate(names):
        s = n.decode("latin-1")
        base = s if re.fullmatch(r"[A-Za-z0-9_.\-]+", s) and s.lower() not in used else \
            f"{i:04d}_" + re.sub(r"[^A-Za-z0-9_.\-]", "_", s)
        used.add(base.lower())
        files.append(f"sounds/{base}.wav")
    return files


def dump(payload, out):
    hd, x8c, sounds = parse(payload)
    if build_payload(hd.seed, x8c, [(s["header"], _tail(s)) for s in sounds]) != payload:
        raise AssertionError("stored hash table differs from the one rebuilt from the names")
    (out / "sounds").mkdir(parents=True, exist_ok=True)
    files = _file_names([_name(s["header"]) for s in sounds])
    entries = []
    for s, f in zip(sounds, files):
        h = s["header"]
        d = header_to_json(h, "pcm" not in s)
        if "pcm" in s:
            _, ch, rate, _, _, bits = struct.unpack_from("<HHIIHH", h, 0xdc)
            (out / f).write_bytes(wav_bytes(ch, rate, bits, s["pcm"]))
            d = {"name": d.pop("name"), "file": f, **d}
        else:
            path = s["path"]
            d["stream"] = str_to_json(path)
            d["next"] = [str_to_json(n) for n in s["next"]]
        entries.append(d)
        # Sanity: the header must come back exactly from the JSON (+ WAV fields).
        if "pcm" in s:
            ch, rate, bits = struct.unpack_from("<H", h, 0xde)[0], struct.unpack_from("<I", h, 0xe0)[0], \
                struct.unpack_from("<H", h, 0xea)[0]
            size = len(s["pcm"])
        else:
            ch, rate, bits, size = d["channels"], d["rate"], d["bits"], d["stream_size"]
        if header_from_json(d, ch, rate, bits, size) != h:
            raise AssertionError(f"header of {_name(h)!r} does not re-encode exactly")
    meta = {"kind": KIND, "hash_seed": f"0x{hd.seed:08X}", "unknown_8c": x8c,
            "note": "Order = sound index. Sounds with a 'file' are PCM WAVs (format fields come from the WAV); "
                    "the others are streamed from GameData\\<stream> and only described here. params: non-zero "
                    "header words by offset (meaning unknown). See docs/formats/big-aud.md.",
            "sounds": entries}
    (out / "sounds.json").write_text(json.dumps(meta, indent=1))


def rebuild(src):
    meta = json.loads((src / "sounds.json").read_text())
    sounds = []
    for d in meta["sounds"]:
        if "file" in d:
            ch, rate, bits, pcm = wav_parse((src / d["file"]).read_bytes(), d["file"])
            sounds.append((header_from_json(d, ch, rate, bits, len(pcm)), pcm))
        else:
            h = header_from_json(d, d["channels"], d["rate"], d["bits"], d["stream_size"])
            if not struct.unpack_from("<I", h, 0x20)[0] & STREAMED:
                raise ValueError(f"{d['name']}: no 'file' but flags lack the streamed bit 0x40")
            tail = str_from_json(d["stream"], PATH, "stream path") + struct.pack("<I", len(d["next"]))
            tail += b"".join(str_from_json(n, 32, "next stream name") for n in d["next"])
            sounds.append((h, tail))
        if "file" in d and int(d["flags"], 16) & STREAMED:
            raise ValueError(f"{d['name']}: has a WAV file but flags has the streamed bit 0x40")
    return build_payload(int(meta["hash_seed"], 16), meta["unknown_8c"], sounds)
