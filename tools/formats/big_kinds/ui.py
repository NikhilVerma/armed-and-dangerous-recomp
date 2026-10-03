"""User interface layout: `*_big12_ui.big`, magic 0x00B0D1E5. Layout and safe edits: docs/formats/big-ui.md.

The loader 0x00462950 only reads the payload into a buffer (ui object +0x48); 0x004629b0 parses it when the
UI is built. Identical in all 25 levels. All integers u32 unless noted.

  u32 page_count, ref_count, object_count, text_count            name list sizes
  u32 page_instance_count, object_instance_count, text_instance_count
  u32 movie_count                                                 subtitle tracks (0x00495ae0)
  u32 subtitle_count                                              = sum of the tracks' line counts; not read
  char pages[page_count][32]        "plnames list"   (ui +0x84)
  char refs[ref_count][32]          "refnames list"  (ui +0x88)
  char objects[object_count][32]    "objnames list"  (ui +0x8c)
  char texts[text_count][32]        "txtnames list"  (ui +0x90)
  u32  page_instances[n]            index into pages; each spawns that object (0x0045f020)
  object_instances[n] x { u32 object (index into objects); u32 field_46c; u32 field_468; u32 fields_478[5] }
  text_instances[n]   x { u32 text (index into texts); u32 field_46c; u32 field_468; f32 scale (+0x4c8/+0x4cc);
                          u32 colour (+0x4e4); u32 fields_478[5] }
  movies[n]           x { char name[32]; u32 line_count; u32 first_line (index into subtitles) }
  subtitles[n]        x { f32 start; f32 end; u32 speaker (index into texts); u32 text (index into texts) }

Dump: ui.json (one table per section; indices into the name lists are written as the names).
Rebuild recomputes the header counts, every index into a name list, first_line and subtitle_count.
"""
import json
import struct

KIND = "ui"
MAGIC = 0x00B0D1E5

NAME = 32
LISTS = ("pages", "refs", "objects", "texts")


def short_f32(x):
    bits = struct.pack("<f", x)
    for p in range(1, 10):
        s = float(f"{x:.{p}g}")
        if struct.pack("<f", s) == bits:
            return s
    return x


def f32(bits):
    x = struct.unpack("<f", struct.pack("<I", bits))[0]
    if x != x or x in (float("inf"), float("-inf")):
        raise ValueError(f"non-finite float 0x{bits:08X}")
    return short_f32(x)


def f32_bits(x):
    return struct.unpack("<I", struct.pack("<f", x))[0]


def i32(u):
    return u - (1 << 32) if u & 0x80000000 else u


def u32(i):
    return i & 0xFFFFFFFF


def name_field(raw):
    name, _, tail = raw.partition(b"\0")
    return name.decode("latin-1"), (raw.hex() if tail.strip(b"\0") else None)


def name_bytes(name, fill=None, what="name"):
    b = name.encode("latin-1")
    if len(b) > NAME - 1:
        raise ValueError(f"{what} {name!r} longer than {NAME - 1} bytes")
    base = bytes.fromhex(fill) if fill else bytes(NAME)
    return b + b"\0" + base[len(b) + 1:]


class Names:
    """A name list; references to it are written as the name when it is unique, else as the index."""

    def __init__(self, kind, names):
        self.kind, self.names = kind, names
        self.index = {}
        for i, n in enumerate(names):
            self.index.setdefault(n.lower(), []).append(i)

    def ref(self, i):
        if i < len(self.names) and len(self.index[self.names[i].lower()]) == 1:
            return self.names[i]
        return i

    def idx(self, r):
        if isinstance(r, int):
            return r
        hit = self.index.get(r.lower(), [])
        if len(hit) != 1:
            raise ValueError(f"ui: {r!r} is {'not in' if not hit else 'ambiguous in'} the {self.kind} list "
                             f"(add it there, or use an index)")
        return hit[0]


def dump(payload, out):
    h = struct.unpack_from("<9I", payload, 0)
    p = 36
    meta = {"kind": KIND,
            "note": "Name lists keep their order (other code may index them). References to them are written "
                    "as names and turned back into indices on rebuild. See docs/formats/big-ui.md."}
    fills = {}
    lists = {}
    for key, n in zip(LISTS, h[:4]):
        names = []
        for i in range(n):
            s, fill = name_field(payload[p:p + NAME])
            if fill:
                fills.setdefault(key, {})[str(i)] = fill
            names.append(s)
            p += NAME
        meta[key] = names
        lists[key] = Names(key, names)
    meta["name_fill"] = fills
    pages, objects, texts = lists["pages"], lists["objects"], lists["texts"]

    meta["page_instances"] = [pages.ref(i) for i in struct.unpack_from(f"<{h[4]}I", payload, p)]
    p += 4 * h[4]
    rows = []
    for _ in range(h[5]):
        r = struct.unpack_from("<8I", payload, p)
        rows.append({"object": objects.ref(r[0]), "field_46c": i32(r[1]), "field_468": i32(r[2]),
                     "fields_478": [i32(x) for x in r[3:]]})
        p += 32
    meta["object_instances"] = rows
    rows = []
    for _ in range(h[6]):
        r = struct.unpack_from("<10I", payload, p)
        rows.append({"text": texts.ref(r[0]), "field_46c": i32(r[1]), "field_468": i32(r[2]),
                     "scale": f32(r[3]), "colour": f"0x{r[4]:08X}", "fields_478": [i32(x) for x in r[5:]]})
        p += 40
    meta["text_instances"] = rows
    movies = []
    first_expected = 0
    tracks = []
    for _ in range(h[7]):
        s, fill = name_field(payload[p:p + NAME])
        count, first = struct.unpack_from("<II", payload, p + NAME)
        if first != first_expected:
            raise ValueError(f"ui: movie {s!r} first line {first}, expected {first_expected}")
        first_expected += count
        tracks.append((s, fill, count))
        p += 40
    if first_expected != h[8]:
        raise ValueError(f"ui: tracks hold {first_expected} lines, header says {h[8]}")
    for s, fill, count in tracks:
        lines = []
        for _ in range(count):
            a, b, spk, txt = struct.unpack_from("<4I", payload, p)
            lines.append({"start": f32(a), "end": f32(b), "speaker": texts.ref(spk), "text": texts.ref(txt)})
            p += 16
        m = {"movie": s, "lines": lines}
        if fill:
            m["name_fill"] = fill
        movies.append(m)
    meta["subtitles"] = movies
    if p != len(payload):
        raise ValueError(f"ui: parsed {p} bytes of {len(payload)}")
    (out / "ui.json").write_text(_dumps(meta), encoding="utf-8")


def _dumps(meta):
    """Indented JSON, with each table row and each name list entry on a line of its own."""
    def rows(v, ind):
        pad = " " * ind
        return "[\n" + ",\n".join(pad + json.dumps(x, ensure_ascii=False) for x in v) + "\n" + " " * (ind - 1) + "]"
    parts = []
    for k, v in meta.items():
        if k == "subtitles":
            tracks = []
            for m in v:
                head = json.dumps({x: y for x, y in m.items() if x != "lines"}, ensure_ascii=False)[:-1]
                tracks.append("  " + head + ', "lines": ' + rows(m["lines"], 4) + "}")
            body = "[\n" + ",\n".join(tracks) + "\n ]"
        elif isinstance(v, list):
            body = rows(v, 2)
        else:
            body = json.dumps(v, indent=1, ensure_ascii=False).replace("\n", "\n ")
        parts.append(f' "{k}": {body}')
    return "{\n" + ",\n".join(parts) + "\n}\n"


def rebuild(src):
    m = json.loads((src / "ui.json").read_text(encoding="utf-8"))
    lists = {k: Names(k, m[k]) for k in LISTS}
    pages, objects, texts = lists["pages"], lists["objects"], lists["texts"]
    movies = m["subtitles"]
    n_lines = sum(len(t["lines"]) for t in movies)
    out = bytearray(struct.pack("<9I", *(len(m[k]) for k in LISTS), len(m["page_instances"]),
                                len(m["object_instances"]), len(m["text_instances"]), len(movies), n_lines))
    for k in LISTS:
        fills = m.get("name_fill", {}).get(k, {})
        for i, s in enumerate(m[k]):
            out += name_bytes(s, fills.get(str(i)), f"{k} name")
    out += struct.pack(f"<{len(m['page_instances'])}I", *(pages.idx(r) for r in m["page_instances"]))
    for r in m["object_instances"]:
        out += struct.pack("<3I", objects.idx(r["object"]), u32(r["field_46c"]), u32(r["field_468"]))
        out += struct.pack("<5I", *map(u32, r["fields_478"]))
    for r in m["text_instances"]:
        out += struct.pack("<5I", texts.idx(r["text"]), u32(r["field_46c"]), u32(r["field_468"]),
                           f32_bits(r["scale"]), int(r["colour"], 16))
        out += struct.pack("<5I", *map(u32, r["fields_478"]))
    first = 0
    for t in movies:
        out += name_bytes(t["movie"], t.get("name_fill"), "movie name") + struct.pack("<II", len(t["lines"]), first)
        first += len(t["lines"])
    for t in movies:
        for ln in t["lines"]:
            out += struct.pack("<2f2I", ln["start"], ln["end"], texts.idx(ln["speaker"]), texts.idx(ln["text"]))
    return bytes(out)
