"""The struct atlas: every object offset the recovered C++ uses, with its meaning inferred and tested.

Porting agents look fields up here instead of re-deriving what `Field<float>(obj, 0x9f0)` means.

    python tools/struct_atlas.py static            # parse archive/src/**/*.cpp -> work/struct_atlas_static.json (summary)
    python tools/struct_atlas.py dynamic DIR...    # read and-structsnap.bin files (hook [test] struct_snap)
    python tools/struct_atlas.py infer             # static + dynamic -> work/struct_atlas.json + coverage, precision
    python tools/struct_atlas.py field CLASS OFF   # one field: a few lines
    python tools/struct_atlas.py class CLASS [--all]  # one class: its fields, one line each
    python tools/struct_atlas.py prop NAME         # which class / offset a property name is stored at
    python tools/struct_atlas.py docs              # docs/structs/<Class>.md + work/struct_atlas_proposals.csv

Classes come from the game's own class descriptors (vtable slot 1 returns a descriptor: +8 size, +0x14
"CLASS(Name)", then member-table entries: BASECLASS(...) whose 4th word is the base's descriptor, DATA(...)).
Static accesses are attributed to a class by the function that makes them (its vtable, or the class prefix of its
tracker name) and the base expression (the method's `this` and its aliases; Game globals; fields of known pointer
type). Everything inferred is labelled with its evidence and a confidence; docs/structs/README.md explains the labels.
"""
import argparse
import collections
import csv
import glob
import json
import os
import re
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, "orig", "pc", "GameData", "game.unpacked.exe")
BASE = 0x400000
WORK = os.path.join(ROOT, "work")
STATIC_JSON = os.path.join(WORK, "struct_atlas_static.json")
DYN_JSON = os.path.join(WORK, "struct_atlas_dynamic.json")
ATLAS_JSON = os.path.join(WORK, "struct_atlas.json")
CLASSES_JSON = os.path.join(WORK, "struct_atlas_classes.json")
GAME_PTR = 0x005fe490

_img = None


def img():
    global _img
    if _img is None:
        import pefile
        pe = pefile.PE(EXE, fast_load=True)
        _img = bytes(pe.get_memory_mapped_image())
    return _img


def u32(va):
    o = va - BASE
    d = img()
    if 0 <= o <= len(d) - 4:
        return struct.unpack_from("<I", d, o)[0]
    return None


def f32(va):
    o = va - BASE
    d = img()
    if 0 <= o <= len(d) - 4:
        return struct.unpack_from("<f", d, o)[0]
    return None


def cstr(va, maxlen=96):
    o = va - BASE
    d = img()
    if not 0 <= o < len(d):
        return None
    e = d.find(b"\0", o, o + maxlen + 1)
    if e <= o:
        return None
    t = d[o:e]
    if all(32 <= c < 127 for c in t):
        return t.decode("ascii")
    return None


def is_code(v):
    return v is not None and 0x00401000 <= v < 0x0056b000


# ---------------------------------------------------------------------------------------------------------------------
# Classes: descriptors, vtables, hierarchy (cached in work/struct_atlas_classes.json)

def build_classes():
    d = img()
    names = {BASE + m.start(): m.group(1).decode() for m in re.finditer(rb"CLASSV?\(([A-Za-z0-9_]+)\)\0", d)}
    classes = {}
    desc_of = {}
    for sva, name in names.items():
        needle = struct.pack("<I", sva)
        i = d.find(needle)
        while i != -1:
            entry = i - 16
            desc = BASE + entry - 4
            off, size, count, ptr4, nameptr, typ = struct.unpack_from("<6I", d, entry)
            if off == 0 and nameptr == sva:
                c = classes.setdefault(name, {"name": name, "descriptor": f"0x{desc:08x}", "size": size, "base": None,
                                              "members": [], "vtables": []})
                desc_of[desc] = name
                k = entry + 24
                for _ in range(400):
                    e = struct.unpack_from("<6I", d, k)
                    s = cstr(e[4]) if e[4] else None
                    if not s:
                        break
                    if s.startswith("BASECLASS("):
                        c["base_desc"] = e[3]
                        c["base_size"] = e[1]
                    elif s.startswith("DATA("):
                        c["members"].append({"offset": e[0], "size": e[1], "count": e[2], "name": s[5:-1].split("::")[-1],
                                             "type": e[5]})
                    else:
                        break
                    k += 24
            i = d.find(needle, i + 1)
    for c in classes.values():
        bd = c.pop("base_desc", None)
        if bd is not None:
            c["base"] = desc_of.get(bd)
    # vtables: slot 1 is `mov eax, descriptor; ret`
    getters = {}
    text_lo, text_hi = 0x1000, 0x16b000
    for m in re.finditer(rb"\xb8(....)\xc3", d[text_lo:text_hi], re.S):
        v = struct.unpack("<I", m.group(1))[0]
        if v in desc_of:
            getters[BASE + text_lo + m.start()] = desc_of[v]
    rlo, rhi = 0x16b000, 0x1be0e2
    vt_list = []
    for i in range(rlo, rhi, 4):
        v = struct.unpack_from("<I", d, i)[0]
        if v in getters and is_code(struct.unpack_from("<I", d, i - 4)[0]):
            vt = BASE + i - 4
            slots = []
            k = i - 4
            while k < rhi and len(slots) < 600:
                t = struct.unpack_from("<I", d, k)[0]
                if not is_code(t):
                    break
                if slots and (BASE + k) != vt and t in getters and len(slots) > 2 and \
                        is_code(struct.unpack_from("<I", d, k - 4)[0]) and (BASE + k - 4) in [x[0] for x in vt_list]:
                    break
                slots.append(t)
                k += 4
            vt_list.append((vt, getters[v], slots))
    # trim each vtable at the next vtable's start
    starts = sorted(v[0] for v in vt_list)
    vtables = {}
    for vt, name, slots in vt_list:
        nxt = next((s for s in starts if s > vt), None)
        if nxt is not None:
            slots = slots[:(nxt - vt) // 4]
        vtables[f"0x{vt:08x}"] = {"class": name, "slots": [f"0x{s:08x}" for s in slots]}
        classes[name]["vtables"].append(f"0x{vt:08x}")
    # The factory (game_04.cpp kCreateCases: class id -> size, vtable or constructor) and the behaviour names
    # (game_03.cpp kBehaviours: name -> class id) name the vtables that have no descriptor (slot 1 ReturnZero):
    # pseudo-classes "obj:<behaviour>" over Object (every factory object is built over Object_Construct 0x004511d0).
    try:
        g3 = open(os.path.join(ROOT, "archive", "src", "game", "game_03.cpp"), encoding="utf-8").read()
        g4 = open(os.path.join(ROOT, "archive", "src", "game", "game_04.cpp"), encoding="utf-8").read()
        beh = collections.defaultdict(list)
        tb = g3[g3.index("kBehaviours[][2] = {"):]
        tb = tb[:tb.index("};")]
        for m in re.finditer(r"\{\s*(0x[0-9a-f]+)\s*,\s*(0x[0-9a-f]+)\s*\}", tb):
            s = cstr(int(m.group(1), 16))
            if s:
                beh[int(m.group(2), 16)].append(s)
        cases = g4[g4.index("kCreateCases[0xaa] = {"):]
        cases = cases[:cases.index("};")]
        for k, m in enumerate(re.finditer(r"\{\s*(K_\w+)\s*,\s*(0x[0-9a-f]+)\s*,\s*(0x[0-9a-f]+)\s*\}", cases)):
            cid, size, arg = k + 1, int(m.group(2), 16), int(m.group(3), 16)
            vt = arg
            if m.group(1) == "K_CTOR":
                vt = None
                code = d[arg - BASE:arg - BASE + 0x300]
                for mm in re.finditer(rb"\xc7[\x00-\x07](....)", code, re.S):
                    v = struct.unpack("<I", mm.group(1))[0]
                    if 0x0056b000 <= v < 0x005bf000 and is_code(u32(v)) and is_code(u32(v + 4)):
                        vt = v
            if not vt:
                continue
            key = f"0x{vt:08x}"
            names = beh.get(cid, [])
            if key in vtables:
                vtables[key].setdefault("behaviours", []).extend(names)
                vtables[key]["factory_size"] = size
                continue
            cname = "obj:" + (names[0] if names else f"id{cid}")
            slots = []
            k2 = vt
            while len(slots) < 600 and is_code(u32(k2)):
                if k2 != vt and f"0x{k2:08x}" in vtables:
                    break
                slots.append(f"0x{u32(k2):08x}")
                k2 += 4
            vtables[key] = {"class": cname, "slots": slots, "behaviours": names, "factory_size": size}
            c = classes.setdefault(cname, {"name": cname, "descriptor": None, "size": size, "base": "Object",
                                           "members": [], "vtables": [], "behaviours": names, "base_assumed": True})
            c["vtables"].append(key)
    except (OSError, ValueError) as e:
        print(f"factory tables not read: {e}", file=sys.stderr)
    out = {"classes": classes, "vtables": vtables}
    os.makedirs(WORK, exist_ok=True)
    json.dump(out, open(CLASSES_JSON, "w"), indent=0)
    return out


_classes = None


def class_model():
    global _classes
    if _classes is None:
        if os.path.exists(CLASSES_JSON) and os.path.getmtime(CLASSES_JSON) > os.path.getmtime(__file__) - 1e9:
            try:
                _classes = json.load(open(CLASSES_JSON))
            except Exception:
                _classes = None
        if _classes is None:
            _classes = build_classes()
        # The Game singleton (0x005fe490) and the object system have no descriptor: pseudo-classes
        _classes["classes"].setdefault("Game", {"name": "Game", "descriptor": None, "size": 0x4c40, "base": None,
                                                "members": [], "vtables": [], "pseudo": True})
        _classes["classes"].setdefault("World", {"name": "World", "descriptor": None, "size": 0x1000, "base": None,
                                                 "members": [], "vtables": ["0x005a826c"], "pseudo": True})
    return _classes


def ancestors(name):
    cm = class_model()["classes"]
    out = []
    seen = set()
    while name and name in cm and name not in seen:
        seen.add(name)
        out.append(name)
        name = cm[name].get("base")
    return out


def lca(names):
    names = [n for n in names if n]
    if not names:
        return None
    common = None
    for n in names:
        a = ancestors(n)
        common = a if common is None else [x for x in common if x in a]
    return common[0] if common else None


def func_classes():
    """function address -> set of classes whose vtables hold it (with slot offsets)."""
    m = collections.defaultdict(set)
    slots = collections.defaultdict(set)
    for vt, v in class_model()["vtables"].items():
        for k, s in enumerate(v["slots"]):
            a = int(s, 16)
            m[a].add(v["class"])
            slots[a].add(k * 4)
    return m, slots


# ---------------------------------------------------------------------------------------------------------------------
# Static: parse the recovered C++

TYPE_NORM = {
    "float": "f32", "double": "f64", "uint32_t": "u32", "int32_t": "i32", "int": "i32", "unsigned": "u32",
    "uint16_t": "u16", "int16_t": "i16", "short": "i16", "uint8_t": "u8", "int8_t": "i8", "char": "i8", "bool": "bool",
    "void*": "ptr", "uint8_t*": "ptr", "uint32_t*": "ptr", "float*": "ptr", "const char*": "ptr", "char*": "ptr",
    "Game*": "ptr", "DWORD": "u32", "BYTE": "u8", "WORD": "u16", "LONG": "i32", "uintptr_t": "u32",
}
WIDTH = {"f32": 4, "f64": 8, "u32": 4, "i32": 4, "u16": 2, "i16": 2, "u8": 1, "i8": 1, "bool": 1, "ptr": 4}


def norm_type(t):
    t = re.sub(r"\s+", " ", t.replace("const ", "")).strip()
    t = t.replace(" *", "*")
    if t.endswith("*") and t not in TYPE_NORM:
        return "ptr"
    return TYPE_NORM.get(t)


HEX = r"0x[0-9a-fA-F]+u?"
IDENT = r"[A-Za-z_]\w*"
PROP_READERS = {
    0x004533a0: "string", 0x00453400: "value", 0x00453450: "value", 0x004534a0: "float", 0x004534f0: "bool",
    0x00453320: "value", 0x00453360: "bool",
}
PROP_READER_NAMES = {"Object_ReadStringProp": "string", "Object_ReadValueProp": "value",
                     "Object_ReadValuePropEbx": "value", "Object_ReadValuePropDef": "float",
                     "Object_ReadBoolProp": "bool", "Object_GetTemplateValue": "value",
                     "Object_GetTemplateBool": "bool", "Object_GetTemplateString": "string"}
CMP_RE = re.compile(r"^\s*(==|!=|<=|>=|<(?!<)|>(?!>))\s*(-?(?:0x[0-9a-fA-F]+|\d+(?:\.\d*)?(?:e-?\d+)?)[uUfL]*|K\(0x[0-9a-fA-F]+u?\))")
STORE_RE = re.compile(r"^\s*=(?!=)\s*(-?(?:0x[0-9a-fA-F]+|\d+(?:\.\d*)?(?:e-?\d+)?)[uUfL]*|K\(0x[0-9a-fA-F]+u?\)|bf\(0x[0-9a-fA-F]+u?\)|u2f\(0x[0-9a-fA-F]+u?\))\s*[;,)]")
WRITE_RE = re.compile(r"^\s*(=(?!=)|\+=|-=|\*=|/=|\|=|&=|\^=|<<=|>>=|\+\+|--)")
MASK_RE = re.compile(r"^\s*(&|\|=|&=|\^=)\s*~?\(?\s*(0x[0-9a-fA-F]+)")


def parse_const(s):
    s = s.strip()
    m = re.match(r"(?:K|bf|u2f)\((0x[0-9a-fA-F]+)u?\)", s)
    if m:
        a = int(m.group(1), 16)
        if s.startswith("K"):
            v = f32(a)
            return round(v, 6) if v is not None else None
        return round(struct.unpack("<f", struct.pack("<I", a & 0xffffffff))[0], 6)
    s = s.rstrip("uUfL")
    try:
        if s.lower().startswith(("0x", "-0x")):
            return int(s, 16)
        if re.match(r"^-?\d+$", s):
            return int(s)
        return float(s)
    except ValueError:
        return None


def split_args(s):
    """Top-level comma split of an argument list text (no outer parentheses)."""
    out, depth, cur = [], 0, ""
    for ch in s:
        if ch in "(<[{":
            depth += 1
        elif ch in ")>]}":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur.strip())
            cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out


def match_paren(text, i):
    """index of the ')' matching text[i] == '('."""
    depth = 0
    for j in range(i, len(text)):
        c = text[j]
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                return j
    return -1


def file_accessors(text):
    """Accessor helpers defined in one file: name -> (type, arity). `T& D(uint32_t a) { return *reinterpret_cast<T*>(a); }`
    (absolute: D(base + off)) and `T& F(void* p, uint32_t off) { ... (p) + off ... }` (two arguments)."""
    acc = {}
    for m in re.finditer(r"(?:^|\n)\s*(?:static\s+|inline\s+)*(?:const\s+)?([\w:]+\s*\*?)\s*&?\s*(\w+)\s*\(([^()]*)\)\s*\{\s*return\s+"
                         r"\*\s*(?:reinterpret_cast|static_cast)<\s*(?:const\s+)?([\w:]+\s*\*?)\s*\*\s*>\s*\(", text):
        name, params, t = m.group(2), m.group(3), m.group(4)
        nt = norm_type(t)
        if not nt:
            continue
        arity = len([p for p in params.split(",") if p.strip()])
        if arity in (1, 2):
            acc[name] = (nt, arity)
    # by-value getters: `T At(const void* p, unsigned off) { return *reinterpret_cast<const T*>(B(p) + off); }` templates
    for m in re.finditer(r"template\s*<\s*class\s+T\s*>\s*(?:inline\s+)?(?:const\s+)?T\s*&?\s*(\w+)\s*\(([^()]*)\)\s*\{\s*return\s+\*", text):
        arity = len([p for p in m.group(2).split(",") if p.strip()])
        acc[m.group(1)] = ("T", arity)
    return acc


GLOBAL_ACC = {"Field": ("T", 2), "At": ("T", 2)}


def find_functions(lines):
    """Top-level function definitions: (name, params text, start line index of '{', end line index of '}', comment)."""
    funcs = []
    n = len(lines)
    i = 0
    while i < n:
        if lines[i].rstrip() == "{" and i > 0:
            # signature: previous lines back to a blank / comment / ';' / '}' / '#' line
            j = i - 1
            sig = []
            while j >= 0 and len(sig) < 8:
                t = lines[j].rstrip()
                if not t or t.lstrip().startswith(("//", "#", "}")) or t.endswith(";") or t.endswith("{"):
                    break
                sig.insert(0, t.strip())
                j -= 1
            s = " ".join(sig)
            m = re.match(r"^(?:template\s*<[^>]*>\s*)?(?:static\s+|inline\s+|extern\s+\"C\"\s+)*([\w:<>\*&\s,]+?)\s*\b(\w+)\s*\((.*)\)\s*(?:const)?\s*$", s)
            # body end: the next '}' at column 0
            k = i + 1
            while k < n and not lines[k].startswith("}"):
                k += 1
            if m and not re.match(r"^(namespace|struct|class|enum|union|if|for|while|switch)\b", s):
                comment = []
                c = j
                while c >= 0 and lines[c].lstrip().startswith("//"):
                    comment.insert(0, lines[c].strip()[2:].strip())
                    c -= 1
                funcs.append({"name": m.group(2), "ret": m.group(1).strip(), "params": m.group(3), "open": i,
                              "close": k, "comment": " ".join(comment), "sig_line": j + 2})
                i = k + 1
                continue
        i += 1
    return funcs


def parse_params(ptext):
    out = []
    for p in split_args(ptext):
        p = re.sub(r"=.*$", "", p).strip()
        m = re.match(r"^(.*?)(\w+)$", p)
        if m:
            out.append((m.group(1).strip(), m.group(2)))
    return out


COMMENT_CLASS = [  # words in a function's register / stack comment -> class (checked in this order)
    (r"\bAI object\b|\bgrunt\b|\bthe AI\b|^AI\b|\bgeneric AI\b", "GenericAIObject"),
    (r"\bplayer\b|\bRoman\b", "PlayerObject"), (r"\bpeasant\b", "PeasantObject"), (r"\btwiglet\b", "Twiglet"),
    (r"\bprojectile\b", "Projectile"), (r"\bfx object\b", "FxObject"), (r"\bexplosion\b", "FxExplosion"),
    (r"\bpickup\b", "PickupObject"), (r"\bQ111 / Molmer\b|\bteammate\b|\bcompanion\b", "TeamateObject"),
    (r"\bpub\b", "PubObject"), (r"\bmonk\b", "MonkObject"), (r"\bgoliath\b", "GoliathObject"),
    (r"\bsaucer\b", "SaucerObject"), (r"\bmarker\b", "MarkerObject"), (r"\bsound ear\b", "SoundEar"),
    (r"\bobject system\b|\bworld\b", "World"), (r"^game\b|\bthe game\b", "Game"),
    (r"\bdefault (?:game )?object\b", "DefaultGameObject"), (r"\bbuilding\b", "BuildingObject"),
    (r"\bturret\b", "TurretObject"), (r"\bgame object\b|^object\b|\bobject$", "Object"),
]
REGS = ("eax", "ecx", "edx", "ebx", "esi", "edi", "ebp")


def comment_param_classes(comment):
    """{abi param name (register or a<k>) -> class} from '(ecx = AI object; stack: player, vx, ...)'."""
    out = {}
    m = re.match(r"^0x[0-9a-fA-F]{8}(?:\s+\w+)?\s*\(([^)]*)\)", comment)
    if not m:
        return out
    inner = m.group(1)
    for part in re.split(r";", inner):
        part = part.strip()
        sm = re.match(r"^stack:\s*(.*)$", part)
        if sm:
            for k, w in enumerate(split_args(sm.group(1))):
                c = comment_word_class(w)
                if c:
                    out[f"a{k}"] = c
            continue
        for rm in re.finditer(r"\b(e[a-d]x|e[sd]i|ebp)\s*=\s*([^,=]+)", part):
            c = comment_word_class(rm.group(2))
            if c:
                out[rm.group(1)] = c
    return out


def comment_word_class(w):
    w = w.strip()
    if not w or len(w) > 40:
        return None
    for pat, c in COMMENT_CLASS:
        if re.search(pat, w, re.I if c not in ("Game",) else 0):
            return c
    return None


class Func:
    __slots__ = ("wrappers", "name", "file", "line", "comment", "params", "body", "body_line", "addr", "hook_name", "abi_map",
                 "abi_names", "pclass", "pwhy", "acc", "getters", "tname", "vt_classes", "this")


class StaticIndex:
    def __init__(self):
        self.funcs_csv = {}
        for r in csv.DictReader(open(os.path.join(ROOT, "symbols", "functions.csv"))):
            self.funcs_csv[int(r["address"], 16)] = r
        self.name_to_addr = {}
        for a, r in self.funcs_csv.items():
            self.name_to_addr.setdefault(r["name"], a)
        self.fclasses, self.fslots = func_classes()
        cm = class_model()["classes"]
        self.class_names = set(cm)
        self.prefix_alias = {"GenericAI": "GenericAIObject", "Ai": "GenericAIObject", "AI": "GenericAIObject",
                             "Player": "PlayerObject", "Peasant": "PeasantObject", "Goliath": "GoliathObject",
                             "Game": "Game", "Fx": "FxObject", "Dgo": "DefaultGameObject", "Object": "Object",
                             "ObjectSystem": "World", "World": "World"}
        self.accesses = []
        self.props = []
        self.total_offset_uses = 0
        self.funcs = []
        self.by_addr = {}
        self.by_name = collections.defaultdict(list)

    def class_of_name(self, name):
        if not name:
            return None
        p = name.split("_")[0]
        if p in self.class_names:
            return p
        return self.prefix_alias.get(p)

    # -- phase A: read every file
    def run(self):
        files = sorted(glob.glob(os.path.join(ROOT, "archive", "src", "**", "*.cpp"), recursive=True))
        for path in files:
            self.read_file(path)
        self.assign_classes()
        for f in self.funcs:
            self.scan(f)

    def read_file(self, path):
        text = open(path, encoding="utf-8", errors="replace").read()
        rel = os.path.relpath(path, ROOT).replace("\\", "/")
        lines = text.split("\n")
        acc = dict(GLOBAL_ACC)
        acc.update(file_accessors(text))
        raw = find_functions(lines)
        byname = collections.defaultdict(list)
        for f in raw:
            byname[f["name"]].append(f)
        entry_to_abi = {m.group(2): (int(m.group(1), 16), m.group(3)) for m in
                        re.finditer(r"ABI_ENTRY_([0-9a-f]{8})\(\s*(\w+)\s*,\s*(\w+)\s*\)", text)}
        hooks = {}
        for m in re.finditer(r"\bHOOK\(\s*0x([0-9a-fA-F]{8})\s*,\s*\"(\w+)\"\s*,\s*(\w+)", text):
            hooks[m.group(3)] = (int(m.group(1), 16), m.group(2))
        for m in re.finditer(r"^\s*[A-Z0-9_]+_HOOK\(\s*([0-9a-f]{8})\s*,\s*(\w+)", text, re.M):
            hooks[m.group(2)] = (int(m.group(1), 16), m.group(2))
        # core function -> (address, hook name, abi param names, abi index -> core param index)
        core_info = {}
        for entry, (addr, abi) in entry_to_abi.items():
            fs = byname.get(abi)
            hook_name = hooks.get(entry, (addr, None))[1]
            if not fs:
                continue
            af = fs[0]
            anames = [p[1] for p in parse_params(af["params"])]
            body = "\n".join(lines[af["open"] + 1:af["close"]])
            core = None
            for cm_ in re.finditer(r"\b(\w+)\s*\(", body):
                if cm_.group(1) in byname and cm_.group(1) != abi:
                    core = cm_.group(1)
                    j = match_paren(body, cm_.end() - 1)
                    args = split_args(body[cm_.end():j])
                    amap = {}
                    for ci, a in enumerate(args):
                        for ai, an in enumerate(anames):
                            if re.search(r"\b" + re.escape(an) + r"\b", a):
                                amap.setdefault(ai, ci)
                    core_info.setdefault(core, (addr, hook_name, anames, amap))
                    break
            if core is None:
                core_info.setdefault(abi, (addr, hook_name, anames, {k: k for k in range(len(anames))}))
        for name, (addr, hn) in hooks.items():
            if name in byname and name not in core_info:
                n = len(parse_params(byname[name][0]["params"]))
                core_info[name] = (addr, hn, None, None)
        # file-local property readers: Rd(dest, "name", default) called with a property-like name at least 3 times
        wc = collections.Counter()
        for m in re.finditer(r"\b(\w+)\(\s*(?:u\(\s*&\s*\w+\s*\)|\w+\s*\+\s*0x[0-9a-fA-F]+u?)\s*,\s*(0x00[45][0-9a-fA-F]{5})u?\s*[,)]", text):
            s = cstr(int(m.group(2), 16))
            if s and re.match(r"^[a-z][a-z0-9_]{2,40}$", s) and not m.group(1).startswith("fn_"):
                wc[m.group(1)] += 1
        wrappers = {k for k, v in wc.items() if v >= 3 and k not in acc}
        getters = {}
        for f in raw:
            if parse_params(f["params"]):
                continue
            body = "\n".join(lines[f["open"] + 1:f["close"]])
            if len(body) < 220:
                if "0xa84" in body and re.search(r"0x005fe490|\bg_game\b|\bG\(\)|\bGameP\(\)", body):
                    getters[f["name"] + "()"] = "World"
                elif re.search(r"0x005fe490|\bg_game\b", body) and not re.search(r"\+\s*0x|,\s*0x(?!005fe490)", body):
                    getters[f["name"] + "()"] = "Game"
        for f in raw:
            if f["name"].endswith("_Abi") or f["name"].endswith("_Entry"):
                continue
            fn = Func()
            fn.name = f["name"]
            fn.file = rel
            fn.line = f["sig_line"]
            fn.comment = f["comment"]
            fn.params = [p[1] for p in parse_params(f["params"])]
            fn.body = "\n".join(lines[f["open"] + 1:f["close"]])
            fn.body_line = f["open"] + 2
            fn.acc = acc
            fn.getters = getters
            fn.wrappers = wrappers
            info = core_info.get(f["name"])
            fn.addr, fn.hook_name, fn.abi_names, fn.abi_map = (info if info else (None, None, None, None))
            if fn.addr is None:
                m = re.match(r"^(0x[0-9a-fA-F]{8})\b", f["comment"])
                if m:
                    fn.addr = int(m.group(1), 16)
            if fn.addr is None:
                m = re.search(r"_([0-9a-f]{8})$", f["name"])
                if m:
                    fn.addr = int(m.group(1), 16)
            if fn.addr is None and f["name"] in self.name_to_addr:
                fn.addr = self.name_to_addr[f["name"]]
            fn.tname = self.funcs_csv.get(fn.addr, {}).get("name") if fn.addr else None
            fn.vt_classes = sorted(self.fclasses.get(fn.addr, ())) if fn.addr else []
            fn.pclass = {}
            fn.pwhy = {}
            fn.this = None
            self.funcs.append(fn)
            if fn.addr and fn.addr not in self.by_addr:
                self.by_addr[fn.addr] = fn
            self.by_name[fn.name].append(fn)

    # -- phase B/C: classes of parameters
    def base_uses(self, fn, p):
        return len(re.findall(r"(?:\(\s*|\bB\(\s*|\bu\(\s*|\bP\(\s*)" + re.escape(p) + r"\s*(?:\)\s*)?(?:\+\s*0x|,\s*0x)", fn.body))

    def set_param(self, fn, idx, cls, why, rank):
        if idx is None or idx >= len(fn.params) or not cls:
            return False
        old = fn.pwhy.get(idx)
        if old is not None and old[1] <= rank:
            return False
        fn.pclass[idx] = cls
        fn.pwhy[idx] = (why, rank)
        return True

    def abi_to_core(self, fn, abi_idx):
        if fn.abi_map is None:
            return abi_idx
        return fn.abi_map.get(abi_idx)

    def assign_classes(self):
        # rank: 0 vtable this, 1 comment, 2 tracker-name prefix, 3 call propagation
        for fn in self.funcs:
            if not fn.params:
                continue
            if fn.vt_classes:
                cls = lca(fn.vt_classes)
                named = self.class_of_name(fn.tname) or self.class_of_name(fn.hook_name)
                if named and cls in ancestors(named) and named in fn.vt_classes:
                    cls = named
                idx = None
                if fn.abi_names and "ecx" in fn.abi_names:
                    idx = self.abi_to_core(fn, fn.abi_names.index("ecx"))
                if idx is None:
                    idx = 0 if (self.base_uses(fn, fn.params[0]) or len(fn.params) == 1) else \
                        max(range(len(fn.params)), key=lambda k: self.base_uses(fn, fn.params[k]))
                self.set_param(fn, idx, cls, "vtable " + "/".join(fn.vt_classes[:3]), 0)
            for reg, cls in comment_param_classes(fn.comment).items():
                if fn.abi_names and reg in fn.abi_names:
                    idx = self.abi_to_core(fn, fn.abi_names.index(reg))
                elif reg in fn.params:
                    idx = fn.params.index(reg)
                elif reg.startswith("a") and fn.abi_names is None:
                    continue
                else:
                    continue
                self.set_param(fn, idx, cls, "comment", 1)
            named = self.class_of_name(fn.tname) or self.class_of_name(fn.hook_name) or self.class_of_name(fn.name)
            if named and not fn.pclass:
                best = max(range(len(fn.params)), key=lambda k: self.base_uses(fn, fn.params[k]))
                if self.base_uses(fn, fn.params[best]):
                    self.set_param(fn, best, named, "name " + (fn.tname or fn.name), 2)
        for _ in range(4):
            votes = collections.defaultdict(collections.Counter)
            for fn in self.funcs:
                bases = self.bases(fn)
                if not bases:
                    continue
                for m in re.finditer(r"\b(?:orig::fn_([0-9a-f]{8})|(\w+))\s*\(", fn.body):
                    callee = None
                    via_abi = False
                    if m.group(1):
                        callee = self.by_addr.get(int(m.group(1), 16))
                        via_abi = True
                    elif m.group(2) in self.by_name and len(self.by_name[m.group(2)]) == 1:
                        callee = self.by_name[m.group(2)][0]
                    if callee is None or callee is fn:
                        continue
                    j = match_paren(fn.body, m.end() - 1)
                    if j < 0:
                        continue
                    for k, arg in enumerate(split_args(fn.body[m.end():j])):
                        a = strip_wrappers(arg)
                        if a in bases and bases[a] not in ("Game", "World"):
                            idx = self.abi_to_core(callee, k) if via_abi else k
                            if idx is not None:
                                votes[(id(callee), idx)][bases[a]] += 1
            changed = 0
            idmap = {id(f): f for f in self.funcs}
            for (fid, idx), cnt in votes.items():
                callee = idmap[fid]
                cls = lca(list(cnt)) if len(cnt) > 1 else next(iter(cnt))
                if cls and self.set_param(callee, idx, cls, "callers " + ",".join(f"{c}x{n}" for c, n in cnt.most_common(3)), 3):
                    changed += 1
            if not changed:
                break

    def bases(self, fn):
        bases = {}
        for idx, cls in fn.pclass.items():
            bases[fn.params[idx]] = cls
        bases.update(fn.getters)
        bases["g_game"] = "Game"
        body = fn.body
        for _ in range(2):
            for m in re.finditer(r"\b(?:const\s+)?[\w:]+\s*\*?\s*(?:const\s+)?&?\s*(\w+)\s*=\s*([^;]{1,80});", body):
                v, src = m.group(1), strip_wrappers(m.group(2))
                if v in bases:
                    continue
                if src in bases:
                    bases[v] = bases[src]
                elif re.match(r"^(?:GameGlobal<[^>]+>|D|\*reinterpret_cast<[^>]+>)\(\s*0x005fe490u?\s*\)$", src):
                    bases[v] = "Game"
            # getter lambdas: auto game = []() { return u(G()); };
            for m in re.finditer(r"\bauto\s+(\w+)\s*=\s*\[[^\]]*\]\s*\(\s*\)\s*\{\s*return\s+([^;]{1,60});\s*\}\s*;", body):
                src = strip_wrappers(m.group(2))
                if src in bases and m.group(1) + "()" not in bases:
                    bases[m.group(1) + "()"] = bases[src]
        return bases

    # -- phase D: the accesses
    def scan(self, fn):
        cls_this = None
        frec_addr = f"0x{fn.addr:08x}" if fn.addr else None
        bases = self.bases(fn)
        body = fn.body
        line_starts = [0]
        for k, ch in enumerate(body):
            if ch == "\n":
                line_starts.append(k + 1)
        import bisect

        def where(pos):
            return f"{fn.file}:{fn.body_line + bisect.bisect_right(line_starts, pos) - 1}"

        fkey = frec_addr or f"{fn.file}:{fn.name}"
        acc = fn.acc
        found = []
        for m in re.finditer(r"\b(\w+)\s*(?:<\s*([\w:\s\*]+?)\s*>)?\s*\(", body):
            nm = m.group(1)
            if nm not in acc:
                continue
            t, arity = acc[nm]
            if t == "T":
                if not m.group(2):
                    continue
                t = norm_type(m.group(2))
                if not t:
                    continue
            j = match_paren(body, m.end() - 1)
            if j < 0:
                continue
            args = split_args(body[m.end():j])
            if arity == 2 and len(args) == 2:
                base_text, off_text = args
            elif arity == 1 and len(args) == 1:
                mm = re.match(r"^(.*?)\s*\+\s*(" + HEX + r")\s*$", args[0], re.S)
                if not mm:
                    continue
                base_text, off_text = mm.group(1), mm.group(2)
            else:
                continue
            mo = re.match(r"^(" + HEX + r")$", off_text.strip())
            idx = False
            if not mo:
                mo = re.match(r"^(" + HEX + r")\s*\+", off_text.strip())
                idx = True
                if not mo:
                    continue
            found.append((m.start(), j + 1, base_text.strip(), int(mo.group(1).rstrip("u"), 16), t, idx))
        for m in re.finditer(r"\*\s*(?:reinterpret_cast<\s*(?:const\s+)?([\w:]+)\s*\*\s*>|\(\s*([\w:]+)\s*\*\s*\))\s*\(", body):
            t = norm_type(m.group(1) or m.group(2))
            if not t:
                continue
            j = match_paren(body, m.end() - 1)
            mm = re.match(r"^(.*?)\s*\+\s*(" + HEX + r")\s*$", body[m.end():j], re.S)
            if not mm:
                continue
            found.append((m.start(), j + 1, mm.group(1).strip(), int(mm.group(2).rstrip("u"), 16), t, False))
        self.total_offset_uses += len(found)
        covered = []
        spans = {}
        for start, end, base_text, off, t, idx in sorted(found):
            covered.append((start, end))
            cls, via = self.resolve_base(base_text, bases)
            after = body[end:end + 80]
            before = body[max(0, start - 3):start]
            wm = WRITE_RE.match(after)
            if wm:
                rw = "w" if wm.group(1) == "=" else "rw"
            elif before.endswith(("++", "--")):
                rw = "rw"
            else:
                rw = "r"
            rec = {"cls": cls, "off": off, "type": t, "rw": rw, "func": fkey, "where": where(start)}
            if via:
                rec["via"] = via
            if idx:
                rec["indexed"] = 1
            m = CMP_RE.match(after)
            if m:
                rec["cmp"] = [m.group(1), parse_const(m.group(2))]
            m = STORE_RE.match(after)
            if m:
                rec["store"] = parse_const(m.group(1))
            m = MASK_RE.match(after)
            if m:
                rec["mask"] = [m.group(1), int(m.group(2), 16)]
            ctx = body[max(0, start - 40):start]
            if re.search(r"(?:\bVC|\bVB|\bVF|VCall<[^>]+>|\bVCall)\s*\(\s*(?:P\(|B\()?$", ctx):
                rec["as_ptr"] = "vcall"
            elif re.search(r"(?:Unref|ClearRef|Release\w*)\s*\(\s*$", ctx):
                rec["as_ptr"] = "ref"
            if cls and cls in class_model()["classes"] and not class_model()["classes"][cls].get("pseudo"):
                size = class_model()["classes"][cls]["size"]
                if off >= size:
                    rec["beyond"] = cls
                    rec["cls"] = None
            spans[(start, end)] = rec
            self.accesses.append(rec)
        # an access used as the base of another access is a pointer
        for (s1, e1), r1 in spans.items():
            for (s2, e2), r2 in spans.items():
                if s1 < s2 and e2 < e1 and r1 is not r2:
                    inner_text = body[s2:e2]
                    if body[s1:e1].find(inner_text) >= 0 and r2["type"] in ("u32", "ptr", "i32"):
                        r2["as_ptr"] = r2.get("as_ptr") or "base"
        alt = "|".join(re.escape(b) for b in sorted(bases, key=len, reverse=True))
        if alt:
            for m in re.finditer(r"(?<![\w.>])(" + alt + r")\s*\+\s*(" + HEX + r")\b(?!\s*\+)", body):
                if any(s <= m.start() < e for s, e in covered):
                    continue
                cls = bases.get(m.group(1))
                off = int(m.group(2).rstrip("u"), 16)
                self.total_offset_uses += 1
                rec = {"cls": cls, "off": off, "type": None, "rw": "addr", "func": fkey, "where": where(m.start())}
                if cls and cls in class_model()["classes"] and not class_model()["classes"][cls].get("pseudo") and \
                        off >= class_model()["classes"][cls]["size"]:
                    rec["beyond"] = cls
                    rec["cls"] = None
                self.accesses.append(rec)
        for m in re.finditer(r"\b(?:orig::fn_([0-9a-f]{8})|(\w+))\s*\(", body):
            kind = None
            if m.group(1):
                kind = PROP_READERS.get(int(m.group(1), 16))
            elif m.group(2) in PROP_READER_NAMES:
                kind = PROP_READER_NAMES[m.group(2)]
            elif m.group(2) in fn.wrappers:
                kind = "wrapper"
            if not kind:
                continue
            j = match_paren(body, m.end() - 1)
            args = split_args(body[m.end():j])
            name = dest = None
            for ai, a in enumerate(args):
                c = re.match(r"^\s*(0x[0-9a-fA-F]{8})u?\s*$", a)
                # the string readers take their default string first (0x004533a0: eax = default)
                if c and name is None and not (kind == "string" and ai == 0):
                    s = cstr(int(c.group(1), 16))
                    if s and re.match(r"^[\w\- ]+$", s):
                        name = s
                        continue
                d = re.match(r"^\s*(?:u\()?\s*(\w+(?:\(\))?)\s*\)?\s*\+\s*(" + HEX + r")\s*$", a)
                if d and dest is None:
                    dest = (d.group(1), int(d.group(2).rstrip("u"), 16))
            mask = None
            if name and not dest:
                # read into a local, then `if (local) Field(obj, 0x4d0) |= 1;` or `Field(obj, OFF) = local`
                lv = next((x.group(1) for a in args for x in [re.match(r"^\s*u\(\s*&\s*(\w+)\s*\)\s*$", a)] if x), None)
                if lv:
                    tail = body[j + 1:j + 260]
                    w = re.search(r"if\s*\(\s*(!?)\s*" + re.escape(lv) + r"\s*\)\s*\{?\s*\w+(?:<[^>]*>)?\(\s*(?:u\()?(\w+)\)?\s*(?:,|\+)\s*(0x[0-9a-fA-F]+)u?\s*\)\s*(\|=|&=)\s*(~?)\s*(0x[0-9a-fA-F]+)", tail)
                    w2 = re.search(r"\w+(?:<[^>]*>)?\(\s*(?:u\()?(\w+)\)?\s*(?:,|\+)\s*(0x[0-9a-fA-F]+)u?\s*\)\s*=\s*[^;]*\b" + re.escape(lv) + r"\b", tail)
                    nxt = re.search(r"orig::fn_004533|orig::fn_004534|Object_Read", tail)
                    if w and (not nxt or nxt.start() > w.start()) and w.group(2) in bases:
                        dest = (w.group(2), int(w.group(3), 16))
                        mask = int(w.group(6), 16)
                    elif w2 and w2.group(1) in bases:
                        dest = (w2.group(1), int(w2.group(2), 16))
            if name:
                if mask is not None:
                    kind = "flag"
                rec = {"prop": name, "cls": bases.get(dest[0]) if dest else None, "mask": mask,
                       "off": dest[1] if dest else None, "kind": kind, "func": fkey, "where": where(m.start()),
                       "how": "reader"}
                if kind == "wrapper":
                    # a file-local reader Rd(dest, name, default bits): the default after the name
                    k = None
                    for i, a in enumerate(args):
                        if re.match(r"^\s*0x[0-9a-fA-F]{8}u?\s*$", a) and cstr(int(a.strip().rstrip("u"), 16)) == name:
                            k = i
                    if k is not None and k + 1 < len(args):
                        dv = re.match(r"^\s*(0x[0-9a-fA-F]+|\d+)u?\s*$", args[k + 1])
                        if dv:
                            bits = int(dv.group(1), 16) if dv.group(1).startswith("0x") else int(dv.group(1))
                            if 0x30000000 <= bits < 0x50000000 or 0xb0000000 <= bits < 0xd0000000:
                                rec["default"] = round(struct.unpack("<f", struct.pack("<I", bits))[0], 6)
                                rec["kind"] = "float"
                            else:
                                rec["default"] = bits
                self.props.append(rec)
        # parameter tables {'F', 0x8fc, 0x005b75a4 "name", default bits, ...} read for this object (RunParamSteps)
        this_cls = next((c for i, c in fn.pclass.items() if fn.pwhy[i][1] <= 2), None)
        for m in re.finditer(r"\{\s*'([A-Z])'\s*,\s*(0x[0-9a-fA-F]+)u?\s*,\s*(0x[0-9a-fA-F]{8})u?\s*,\s*(0x[0-9a-fA-F]+|-?\d+)u?", body):
            s = cstr(int(m.group(3), 16))
            if not s or not re.match(r"^[\w\- ]+$", s):
                continue
            kind = {"F": "float", "I": "value", "T": "trigger", "B": "bool", "S": "string"}.get(m.group(1), m.group(1))
            dv = int(m.group(4), 16) if m.group(4).startswith("0x") else int(m.group(4))
            default = round(struct.unpack("<f", struct.pack("<I", dv & 0xffffffff))[0], 6) if kind == "float" else dv
            self.props.append({"prop": s, "cls": this_cls, "off": int(m.group(2), 16), "kind": kind, "default": default,
                               "func": fkey, "where": where(m.start()), "how": "table"})
        # a statement that reads a named property and writes one field of this object: `if (!readBool(1, "can_be_sharked"))
        # Field(obj, 0x48c) |= 0x100;` (weaker: the name and the field share a statement)
        thisvars = [v for v, c in bases.items() if c == this_cls and not v.endswith("()")] if this_cls else []
        if thisvars:
            tv = "|".join(re.escape(v) for v in thisvars)
            for st in re.finditer(r"[^;{}]*;", body):
                seg = st.group(0)
                if "orig::fn_0045340" in seg or "orig::fn_004534" in seg or "orig::fn_004533" in seg:
                    continue
                names = [cstr(int(x, 16)) for x in re.findall(r"\b(0x005[0-9a-fA-F]{5})u?\b", seg)]
                names = [n for n in names if n and re.match(r"^[a-z][a-z0-9_]{2,40}$", n)]
                if len(names) != 1:
                    continue
                w = re.search(r"\b\w+(?:<[^>]*>)?\(\s*(?:u\()?(" + tv + r")\)?\s*(?:,|\+)\s*(0x[0-9a-fA-F]+)u?\s*\)\s*(\|=|&=|=(?!=))\s*~?\(?\s*(0x[0-9a-fA-F]+|[\w.]+)?", seg)
                if not w:
                    continue
                rec = {"prop": names[0], "cls": this_cls, "off": int(w.group(2), 16), "kind": "stmt", "func": fkey,
                       "where": where(st.start()), "how": "statement"}
                if w.group(3) in ("|=", "&=") and w.group(4) and w.group(4).startswith("0x"):
                    rec["mask"] = int(w.group(4), 16)
                self.props.append(rec)

    def resolve_base(self, base_text, bases):
        b = strip_wrappers(base_text)
        if b in bases:
            return bases[b], None
        mm = re.match(r"^(\w+)\s*(?:<[^>]*>)?\s*\((.*)\)$", b, re.S)
        if mm and match_paren(b, b.index("(")) == len(b) - 1:
            args = split_args(mm.group(2))
            if len(args) == 2:
                inner_base, off = args
            elif len(args) == 1:
                x = re.match(r"^(.*?)\s*\+\s*(" + HEX + r")$", args[0], re.S)
                if not x:
                    return None, None
                inner_base, off = x.group(1), x.group(2)
            else:
                return None, None
            mo = re.match(r"^(" + HEX + r")$", off.strip())
            if not mo:
                return None, None
            pc, pv = self.resolve_base(inner_base, bases)
            if pc or pv:
                o = int(mo.group(1).rstrip("u"), 16)
                return None, (f"{pv}>0x{o:x}" if pv else f"{pc}+0x{o:x}")
        return None, None

    def func_records(self):
        out = {}
        for fn in self.funcs:
            key = f"0x{fn.addr:08x}" if fn.addr else f"{fn.file}:{fn.name}"
            if key in out:
                continue
            out[key] = {"name": fn.tname or fn.hook_name or fn.name, "file": fn.file, "line": fn.line,
                        "params": {fn.params[i]: [c, fn.pwhy[i][0]] for i, c in fn.pclass.items()}}
        return out


def strip_wrappers(b):
    b = b.strip()
    for _ in range(4):
        mm = re.match(r"^(?:u|B|P|reinterpret_cast<[^>]+>|static_cast<[^>]+>|const_cast<[^>]+>)\s*\((.*)\)$", b, re.S)
        if mm and match_paren(b, b.index("(")) == len(b) - 1:
            b = mm.group(1).strip()
        elif b.startswith("(") and match_paren(b, 0) == len(b) - 1:
            b = b[1:-1].strip()
        else:
            break
    return b


def cmd_static(a):
    si = StaticIndex()
    si.run()
    # pointer evidence from nested accesses: rec "via" = "Cls+0xOFF" marks (Cls, OFF) as a pointer
    out = {"accesses": si.accesses, "props": si.props, "functions": si.func_records(),
           "total_offset_uses": si.total_offset_uses}
    os.makedirs(WORK, exist_ok=True)
    json.dump(out, open(STATIC_JSON, "w"))
    attributed = [r for r in si.accesses if r["cls"]]
    pairs = {(r["cls"], r["off"]) for r in attributed}
    via = [r for r in si.accesses if r.get("via")]
    print(f"STATIC accesses {len(si.accesses)} (offset uses {si.total_offset_uses}); attributed to a class "
          f"{len(attributed)} ({100 * len(attributed) / max(1, len(si.accesses)):.0f}%), distinct (class, offset) "
          f"{len(pairs)}; through a pointer field {len(via)}")
    print(f"STATIC functions {len(si.funcs)} ({sum(1 for f in si.funcs if f.pclass)} with a typed parameter); properties {len(si.props)} ({sum(1 for p in si.props if p['cls'])} attributed), distinct names "
          f"{len({p['prop'] for p in si.props})}")
    top = collections.Counter(r["cls"] for r in attributed).most_common(12)
    print("STATIC top classes: " + ", ".join(f"{c} {n}" for c, n in top))


# ---------------------------------------------------------------------------------------------------------------------
# Dynamic: and-structsnap.bin (archive/hook/src/teststructsnap.cpp)

def read_snap(path):
    """-> {"frames": [(frame, clock, dt)], "objects": {addr: {"vt", "id", "cls", "samples": [frame index], "pos": [...],
    "data": [bytes]}}, "ptrs": {addr: {off: [(value, target dword)]}}}"""
    import numpy as np  # noqa: F401
    raw = open(path, "rb").read()
    if raw[:4] != b"SNP1":
        raise SystemExit(f"{path}: not a struct snapshot")
    vt_class = {int(k, 16): v["class"] for k, v in class_model()["vtables"].items()}
    vt_class.update({1: "Game", 2: "World"})  # the hook's pseudo objects
    frames, objects, ptrs = [], {}, collections.defaultdict(lambda: collections.defaultdict(list))
    i = 4
    n = len(raw)
    fi = -1
    while i + 4 <= n:
        tag = raw[i:i + 4]
        if tag == b"FRM1":
            if i + 20 > n:
                break
            f, clock, dt, cnt = struct.unpack_from("<iffI", raw, i + 4)
            frames.append((f, clock, dt))
            fi = len(frames) - 1
            i += 20
        elif tag == b"OBJ1":
            if i + 32 > n:
                break
            addr, vt, oid, _z, px, py, pz, size = struct.unpack_from("<IIHHfffI", raw, i + 4)
            if i + 32 + size > n:
                break
            data = raw[i + 32:i + 32 + size]
            i += 32 + size
            if size == 0:
                continue
            o = objects.get(addr)
            if o is not None and o["vt"] != vt:
                # a new object at a freed address: the old one is kept under its own key
                objects[f"{addr:08x}@{fi}"] = o
                o = None
            if o is None:
                o = objects[addr] = {"addr": addr, "vt": vt, "id": oid, "cls": vt_class.get(vt, f"vt_{vt:08x}"),
                                     "fi": [], "pos": [], "data": []}
            o["fi"].append(fi)
            o["pos"].append((px, py, pz))
            o["data"].append(data)
        elif tag == b"PTR1":
            addr, cnt = struct.unpack_from("<II", raw, i + 4)
            for k in range(cnt):
                off, val, tgt = struct.unpack_from("<III", raw, i + 12 + 12 * k)
                ptrs[addr][off].append((val, tgt))
            i += 12 + 12 * cnt
        else:
            break
    return {"frames": frames, "objects": objects, "ptrs": ptrs}


def load_trace(path):
    out = {}
    if not os.path.exists(path):
        return out
    for line in open(path):
        try:
            j = json.loads(line)
        except ValueError:
            continue
        out[j["f"]] = j
    return out


def dyn_scenario(dirpath):
    """Per-object time series from one scenario folder (and-structsnap.bin + original.jsonl)."""
    import numpy as np
    snap = read_snap(os.path.join(dirpath, "and-structsnap.bin"))
    frames = snap["frames"]
    trace = load_trace(os.path.join(dirpath, "original.jsonl"))
    deaths = {}
    for f, j in trace.items():
        for d in j.get("deaths", []):
            deaths.setdefault(d, f)
    seen = set()
    series = []
    for key, o in snap["objects"].items():
        if id(o) in seen or len(o["fi"]) < 2:
            continue
        seen.add(id(o))
        size = min(len(d) for d in o["data"])
        arr = np.frombuffer(b"".join(d[:size] for d in o["data"]), dtype=np.uint8).reshape(len(o["data"]), size)
        fr = np.array([frames[k][0] for k in o["fi"]])
        clock = np.array([frames[k][1] for k in o["fi"]], dtype=np.float64)
        series.append({"cls": o["cls"], "addr": o["addr"], "id": o["id"], "frames": fr, "clock": clock,
                       "pos": np.array(o["pos"], dtype=np.float32), "bytes": arr,
                       "ptrs": snap["ptrs"].get(o["addr"], {}), "death": deaths.get(o["id"]) if o["id"] else None})
    addrs = {o["addr"]: o["cls"] for o in snap["objects"].values()}
    return series, addrs, trace


_vt_labels = {}


def vt_label(vt):
    """A vtable without a class descriptor: 'vtable 0x... (Prefix?)' from the tracker names of its slot functions."""
    if vt in _vt_labels:
        return _vt_labels[vt]
    names = collections.Counter()
    if not hasattr(vt_label, "funcs"):
        vt_label.funcs = {int(r["address"], 16): r["name"] for r in csv.DictReader(open(os.path.join(ROOT, "symbols", "functions.csv")))}
        vt_label.subsys = {int(r["address"], 16): r["subsystem"] for r in csv.DictReader(open(os.path.join(ROOT, "symbols", "functions.csv")))}
    subs = collections.Counter()
    for k in range(64):
        s = u32(vt + 4 * k)
        if not is_code(s):
            break
        nm = vt_label.funcs.get(s, "")
        subs[vt_label.subsys.get(s, "")] += 1
        m2 = re.match(r"^([A-Z][A-Za-z0-9]+_[A-Z][A-Za-z0-9]+)_[A-Za-z0-9]", nm)
        if m2:
            names[m2.group(1)] += 1.5
        m = re.match(r"^([A-Z][A-Za-z0-9]+)_", nm)
        if m and m.group(1) not in ("Debug", "EmptyMethod", "Object", "ObjectDesc", "Return", "ReturnZero", "Havok", "FUN"):
            names[m.group(1)] += 1
    hint = names.most_common(1)[0][0] if names else (subs.most_common(1)[0][0] if subs else "")
    _vt_labels[vt] = f"vtable 0x{vt:08x}" + (f" ({hint}?)" if hint else "")
    return _vt_labels[vt]


def heapish(v):
    return 0x00010000 <= v < 0x7fff0000 and not (0x00400000 <= v < 0x0064d000) and not (v & 3)


def analyse_dynamic(dirs, classes=None, byte_offsets=None):
    """-> {(cls, off): facts} over every scenario folder in dirs."""
    import numpy as np
    vt_class = {int(k, 16): v["class"] for k, v in class_model()["vtables"].items()}
    per = collections.defaultdict(lambda: collections.defaultdict(list))  # cls -> list of series
    addr_cls = {}
    for d in dirs:
        if not os.path.exists(os.path.join(d, "and-structsnap.bin")):
            continue
        series, addrs, trace = dyn_scenario(d)
        addr_cls.update(addrs)
        for s in series:
            s["scenario"] = os.path.basename(d)
            per[s["cls"]]["series"].append(s)
    facts = {}
    for cls, bucket in per.items():
        if classes and cls not in classes:
            continue
        ss = bucket["series"]
        size = min(s["bytes"].shape[1] for s in ss)
        for off in range(0, size - 3, 4):
            facts[(cls, off)] = dword_facts(cls, off, ss, addr_cls, vt_class)
        # byte-wide fields: bytes that differ from their dword's low byte pattern are judged in infer() from static
        # widths; here only the per-byte change summary
        for off in range(0, size):
            if byte_offsets is not None and off not in byte_offsets:
                continue
            vals = np.concatenate([s["bytes"][:, off] for s in ss])
            facts.setdefault((cls, off), {})["byte"] = {"distinct": int(len(np.unique(vals))),
                                                       "values": sorted(int(x) for x in np.unique(vals)[:8])}
    return facts, {c: len(b["series"]) for c, b in per.items()}


def dword_facts(cls, off, ss, addr_cls, vt_class):
    import numpy as np
    with np.errstate(all="ignore"):
        return _dword_facts(cls, off, ss, addr_cls, vt_class)


def _dword_facts(cls, off, ss, addr_cls, vt_class):
    import numpy as np
    u = [s["bytes"][:, off:off + 4].copy().view("<u4").ravel() for s in ss]
    f = [x.view("<f4") for x in u]
    allu = np.concatenate(u)
    allf = np.concatenate(f)
    n = len(allu)
    fact = {"n": int(n), "objects": len(ss)}
    nz = allu[allu != 0]
    fact["zero_frac"] = round(1 - len(nz) / max(1, n), 3)
    uniq = np.unique(allu)
    fact["distinct"] = int(len(uniq))
    changes = sum(int(np.count_nonzero(np.diff(x.astype(np.int64)))) for x in u)
    steps = sum(len(x) - 1 for x in u)
    fact["change_frac"] = round(changes / max(1, steps), 3)
    per_obj_const = all(len(np.unique(x)) == 1 for x in u)
    fact["per_object_constant"] = per_obj_const
    if len(nz) == 0:
        fact["guess"] = "zero"
        return fact
    # floats: plausible magnitude and not small integers
    fin = np.isfinite(allf)
    mag = np.abs(allf)
    plaus = fin & ((allu == 0) | ((mag > 1e-5) & (mag < 1e7)))
    fact["float_frac"] = round(float(np.count_nonzero(plaus)) / n, 3)
    small_int = (allu < 0x10000) | (allu > 0xffff0000)
    fact["smallint_frac"] = round(float(np.count_nonzero(small_int)) / n, 3)
    heap = np.array([heapish(int(v)) for v in nz[:4000]])
    fact["heap_frac"] = round(float(heap.mean()), 3) if len(heap) else 0.0
    # pointer targets: other snapshotted objects or the PTR records' target vtables
    targets = collections.Counter()
    for s in ss:
        for v in np.unique(s["bytes"][:, off:off + 4].copy().view("<u4").ravel()):
            v = int(v)
            if v in addr_cls:
                targets[addr_cls[v]] += 1
        for val, tgt in s["ptrs"].get(off, []):
            if val not in addr_cls and tgt in vt_class:
                targets[vt_class[tgt]] += 1
            elif val not in addr_cls and 0x0056b000 <= tgt < 0x005bf000:
                targets[vt_label(tgt)] += 1
    if targets:
        fact["targets"] = dict(targets.most_common(4))
    # mapped: of the distinct non-zero values of the objects that have pointer records, the share the hook found mapped
    seen_v, mapped_v = set(), set()
    for s in ss:
        if not s["ptrs"]:
            continue
        seen_v.update(int(v) for v in np.unique(s["bytes"][:, off:off + 4].copy().view("<u4").ravel()) if v)
        mapped_v.update(val for val, _ in s["ptrs"].get(off, []))
    fact["mapped_frac"] = round(len(seen_v & mapped_v) / len(seen_v), 3) if seen_v else None
    is_obj = any(t in class_model()["classes"] for t in targets)
    if fact["heap_frac"] > 0.8 and (is_obj or (fact["mapped_frac"] or 0) >= 0.5 or
                                    (fact["mapped_frac"] is None and fact["float_frac"] < 0.95)):
        fact["guess"] = "ptr"
    elif fact["float_frac"] > 0.97 and fact["smallint_frac"] < 0.5:
        fact["guess"] = "f32"
        good = allf[fin]
        fact["min"], fact["max"] = round(float(good.min()), 4), round(float(good.max()), 4)
    else:
        fact["guess"] = "int"
        iv = allu.view("<i4")
        fact["min"], fact["max"] = int(iv.min()), int(iv.max())
        if fact["distinct"] <= 24:
            fact["values"] = [int(x) for x in np.unique(iv)][:24]
    tests = {}
    # timers: dv == -dclock (countdown) or +dclock (stopwatch) on samples where the value changes
    if fact["guess"] == "f32" and changes:
        down = up = other = 0
        for s, x in zip(ss, f):
            dv = np.diff(x.astype(np.float64))
            dt = np.diff(s["clock"])
            ch = (dv != 0) & np.isfinite(dv) & (dt > 0)
            if not ch.any():
                continue
            tol = 0.002 + 0.03 * dt[ch]
            d = np.abs(dv[ch] + dt[ch]) < tol
            u_ = np.abs(dv[ch] - dt[ch]) < tol
            down += int(d.sum())
            up += int(u_.sum())
            other += int((~d & ~u_).sum())
        tot = down + up + other
        if tot:
            tests["countdown"] = [down, tot]
            tests["stopwatch"] = [up, tot]
    # timestamps: the value is the game clock (set to "now" when something happens)
    if fact["guess"] == "f32":
        eq = tot = 0
        for s, x in zip(ss, f):
            ok_ = np.isfinite(x)
            tot += int(ok_.sum())
            eq += int((np.abs(x[ok_].astype(np.float64) - s["clock"][ok_]) < 0.05).sum())
        if eq:
            tests["equals_clock"] = [eq, tot]
    # int counters by frames: dv == frames elapsed (+ or -)
    if fact["guess"] == "int" and changes:
        cup = cdown = tot = 0
        for s, x in zip(ss, u):
            dv = np.diff(x.view("<i4").astype(np.int64))
            df = np.diff(s["frames"])
            ch = dv != 0
            tot += int(ch.sum())
            cup += int((dv[ch] == df[ch]).sum())
            cdown += int((dv[ch] == -df[ch]).sum())
        if tot:
            tests["frame_counter"] = [cup, tot]
            tests["frame_countdown"] = [cdown, tot]
    # position triple / matrix translation (off, off+4, off+8) or (off, off+16, off+32) against vt+0x54
    if fact["guess"] == "f32":
        for name, step in (("position", 4), ("matrix_translation", 16)):
            ok = tot = 0
            for s in ss:
                b = s["bytes"]
                if off + 2 * step + 4 > b.shape[1]:
                    tot = 0
                    break
                v = np.stack([b[:, off + k * step:off + k * step + 4].copy().view("<f4").ravel() for k in range(3)], 1)
                p = s["pos"]
                moving = np.ptp(p, 0).max() > 0.01 or True
                fin = np.all(np.isfinite(p), 1)
                good = np.all(np.abs(v - p) < 0.05 + 0.002 * np.abs(p), 1) & fin
                ok += int(good.sum())
                tot += int(fin.sum())
            if tot and ok:
                tests[name] = [ok, tot]
        # velocity: matches the position's derivative between samples
        ok = tot = 0
        for s in ss:
            b = s["bytes"]
            if off + 12 > b.shape[1] or len(s["pos"]) < 3:
                continue
            v = np.stack([b[:, off + 4 * k:off + 4 * k + 4].copy().view("<f4").ravel() for k in range(3)], 1).astype(np.float64)
            dp = np.diff(s["pos"].astype(np.float64), axis=0)
            dt = np.diff(s["clock"])[:, None]
            okdt = dt[:, 0] > 0
            vel = dp[okdt] / dt[okdt]
            mid = (v[1:][okdt] + v[:-1][okdt]) / 2
            speed = np.linalg.norm(vel, axis=1)
            moving = (speed > 0.5) & np.isfinite(speed)
            if not moving.any():
                continue
            err = np.linalg.norm(mid[moving] - vel[moving], axis=1)
            ok += int((err < 0.25 * speed[moving] + 0.3).sum())
            tot += int(moving.sum())
        if tot >= 3 and ok:
            tests["velocity"] = [ok, tot]
    # health-like: float, changes only downwards (or back up to the start value), reaches <= 0 at a traced death
    if fact["guess"] == "f32" and changes:
        downs = ups = 0
        dead_ok = dead_tot = 0
        for s, x in zip(ss, f):
            dv = np.diff(x.astype(np.float64))
            downs += int((dv < 0).sum())
            ups += int((dv > 0).sum())
            if s["death"] is not None:
                after = s["frames"] >= s["death"]
                if after.any():
                    dead_tot += 1
                    dead_ok += int(x[after][0] <= 0)
        tests["decreasing"] = [downs, downs + ups]
        if dead_tot:
            tests["zero_at_death"] = [dead_ok, dead_tot]
    # flags: integer values that differ from each other by single bits; values beyond small enums
    if fact["guess"] == "int" and fact["distinct"] > 1:
        flips = single = 0
        for x in u:
            dv = x[1:] ^ x[:-1]
            ch = dv != 0
            flips += int(ch.sum())
            single += int(sum(1 for d in dv[ch] if bin(int(d)).count("1") <= 2))
        tests["bitflips"] = [single, flips]
    # integers that change together with the AI state (GenericAIObject +0x460, the trace's "state")
    if fact["guess"] == "int" and changes and off != 0x460 and "GenericAIObject" in ancestors(cls):
        co = tot = 0
        for s, x in zip(ss, u):
            if s["bytes"].shape[1] < 0x464:
                continue
            st = s["bytes"][:, 0x460:0x464].copy().view("<u4").ravel()
            ch = np.diff(x.astype(np.int64)) != 0
            tot += int(ch.sum())
            co += int((ch & (np.diff(st.astype(np.int64)) != 0)).sum())
        if tot:
            tests["with_ai_state"] = [co, tot]
        fact["bits_seen"] = f"0x{int(np.bitwise_or.reduce(allu)):x}"
    if tests:
        fact["tests"] = tests
    return fact


def cmd_run(a):
    """Runs the original on oracle scenarios with struct snapshots (hook [test] struct_snap), into ORACLE_OUT."""
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    os.environ.setdefault("AND_HOOK_BUILD", "build/hook-structatlas")
    import importlib
    import instances
    importlib.reload(instances)
    import oracle_diff
    import shutil
    for name in a.scenarios:
        sc = oracle_diff.load_scenario(name)
        if a.frames:
            sc["frames"] = min(sc["frames"], a.frames)
        test = dict(sc.get("hook_test", {}))
        test.update({"struct_snap": a.every, "struct_snap_per_class": a.per_class, "struct_snap_ptr_every": a.ptr_every})
        if a.classes:
            test["struct_snap_classes"] = a.classes
        sc["hook_test"] = test
        trace = oracle_diff.run_original(sc, False, a.timeout, a.instance)
        gd = instances.instance_dir(a.instance)
        src = os.path.join(gd, "and-structsnap.bin")
        dst_dir = oracle_diff.out_dir(sc)
        if os.path.exists(src):
            shutil.copy2(src, os.path.join(dst_dir, "and-structsnap.bin"))
            os.remove(src)
            print(f"RUN {name}: {os.path.getsize(os.path.join(dst_dir, 'and-structsnap.bin')) // 1024} KB snapshots, trace "
                  f"{'ok' if trace else 'missing'}")
        else:
            print(f"RUN {name}: no snapshots (see {os.path.join(gd, 'and.log')})")


def snap_dirs():
    """Scenario folders with snapshots: ORACLE_OUT if set, else every work/oracle-structatlas*/ (several runs)."""
    roots = [os.environ["ORACLE_OUT"]] if os.environ.get("ORACLE_OUT") else glob.glob(os.path.join(WORK, "oracle-structatlas*"))
    return sorted(os.path.dirname(p) for r in roots for p in glob.glob(os.path.join(r, "*", "and-structsnap.bin")))


def cmd_dynamic(a):
    dirs = a.dirs or snap_dirs()
    for d in dirs:
        snap = read_snap(os.path.join(d, "and-structsnap.bin"))
        objs = {id(o): o for o in snap["objects"].values()}
        by = collections.Counter(o["cls"] for o in objs.values())
        print(f"DYNAMIC {os.path.basename(d)}: {len(snap['frames'])} snapshots (play frames "
              f"{snap['frames'][0][0] if snap['frames'] else '-'}..{snap['frames'][-1][0] if snap['frames'] else '-'}), "
              f"{len(objs)} objects, {len(by)} classes: " + ", ".join(f"{c} {n}" for c, n in by.most_common(8)))


# ---------------------------------------------------------------------------------------------------------------------
# Inference: static + dynamic + known names -> work/struct_atlas.json

MAIN_CLASSES = ["Object", "GenericAIObject", "PlayerObject", "FxObject", "DefaultGameObject", "PeasantObject",
                "TeamateObject", "GoliathObject", "TurretGun", "SaucerObject", "Twiglet", "Projectile", "Game"]
DOC_CLASS = {"grunt-ai.md": "GenericAIObject", "infantry.md": "GenericAIObject", "player.md": "PlayerObject",
             "objects.md": "Object", "peasants.md": "PeasantObject", "twiglets.md": "Twiglet",
             "companions.md": "TeamateObject", "damage.md": "Object", "turret.md": "TurretObject",
             "vehicles.md": "TurretGun", "defence.md": "GenericAIObject"}
# What the oracle trace (archive/hook/src/testoracle.cpp) reads, by its own evidence: the traced ground truth
TRACED = [("Object", 0x396, "id", "level object id (trace 'id')"), ("GenericAIObject", 0x460, "state", "AI state (trace 'state')"),
          ("Object", 0x454, "health", "health (trace 'health')"), ("Object", 0x458, "health", "max_health (trace)"),
          ("GenericAIObject", 0x588, "pointer", "target (trace 'target': the player or an object)"),
          ("Object", 0x1c4, "pointer", "Havok entity (trace 'entity')"),
          ("PlayerObject", 0xad0, "pointer", "Jonesy (trace companions)"), ("PlayerObject", 0xad4, "pointer", "Q (trace companions)"),
          ("Twiglet", 0x464, "state", "twiglet state (trace)"), ("Twiglet", 0x55c, "pointer", "carried bomb (trace 'bomb')"),
          ("PeasantObject", 0x48c, "bool", "captured (trace)"), ("PeasantObject", 0x478, "float", "rope length (trace)"),
          ("PeasantObject", 0x480, "int", "rope link (trace)"), ("PeasantObject", 0x484, "int", "count (trace)"),
          ("Object", 0x34, "pointer", "parent (trace 'attached')"), ("Object", 0x414, "pointer", "class template (objects.md)"),
          ("Object", 0x28, "int", "reference count (objects.md)"),
          # the Game singleton and the World as archive/hook/src/testoracle.cpp reads them
          ("Game", 0x748, "pointer", "the player (testoracle Player())"), ("Game", 0xa84, "pointer", "the World (object system)"),
          ("Game", 0x2684, "int", "pause count"), ("Game", 0x2688, "float", "game clock at the pause"),
          ("Game", 0x26f4, "int", "peasants carried"), ("Game", 0x26e8, "int", "peasants saved"),
          ("Game", 0x26e4, "int", "peasant casualties"), ("Game", 0x270c, "int", "twiglets killed"),
          ("Game", 0x2710, "int", "twiglets made"), ("Game", 0x2658, "int", "compass objective tag"),
          ("Game", 0xa64, "pointer", "level record (+0xc state)"), ("Game", 0xdcc, "bool", "alarm forced"),
          ("World", 0xe7c, "pointer", "AI list"), ("World", 0xe70, "pointer", "every object (trigger listeners)"),
          ("World", 0xe94, "pointer", "area-damage list"), ("World", 0xe90, "pointer", "bullet list"),
          ("World", 0xe9c, "pointer", "collidable list"), ("World", 0xeec, "pointer", "twiglet list"),
          ("World", 0xef8, "pointer", "peasant list"), ("World", 0xef0, "pointer", "siren list")]
CATEGORY_RE = [
    ("bool", r"^m_b[A-Z]|^m_is[A-Z]|^m_has[A-Z]|^m_do[A-Z]|captured"),
    ("timer", r"timer|Timer|time\b|Time\b|delay|Delay|cooldown|countdown|Countdown|duration"),
    ("position", r"(^|_|[a-z])[Pp]os(ition)?\b|[Pp]osition|location|Location|Loc$"),
    ("velocity", r"[Vv]el(ocity)?\b|Velocity"),
    ("health", r"[Hh]ealth|hitpoints|\bhp\b"),
    ("state", r"(^|_|m_)state\b|State$|^m_state|mode\b|Mode$"),
    ("flags", r"[Ff]lags\d*$|_flags?\d*$"),
    ("vector", r"^m_v[A-Z]"),
    ("pointer", r"^m_p[A-Z]|[Pp]tr\b|[Tt]arget$|[Oo]wner$|[Pp]arent$|[Ee]ntity$|template"),
]


STOP = {"m", "min", "max", "num", "on", "of", "the", "a", "pct", "percent", "is", "do", "has", "b", "p", "f", "n"}
SYN = {"pct": "percent", "dist": "distance", "num": "count", "anim": "animation", "fx": "effect", "txt": "text"}


def tokens(name):
    """snake_case / m_camelCase -> word stems ('fire_num_bursts_min' -> {fire, burst}; 'm_burstMin' -> {burst})."""
    s = re.sub(r"^m_", "", name)
    s = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", s).lower()
    out = set()
    for w in re.split(r"[^a-z0-9]+", s):
        w = SYN.get(w, w)
        if not w or w in STOP:
            continue
        for suf in ("ies", "es", "s", "ing", "ed"):
            if len(w) > 4 and w.endswith(suf):
                w = w[:-len(suf)]
                break
        out.add(w[:5])
    return out


def names_agree(prop, member):
    """A property name and a member-table name name the same thing: one shares at least half of its word stems with
    the other (or the min / max halves agree)."""
    if re.sub(r"[^a-z0-9]", "", prop.lower()) == re.sub(r"^m", "", re.sub(r"[^a-z0-9]", "", member.lower())):
        return True
    a, b = tokens(prop), tokens(member)
    if not a or not b:
        return False
    shared = len(a & b)
    mm = lambda n: ("min" in n.lower()) - ("max" in n.lower())  # noqa: E731
    return shared >= 1 and shared * 2 >= min(len(a), len(b)) and mm(prop) == mm(member)


DOC_CATEGORY_RE = [("timer", r"\btimer\b|count(s)? down|countdown|\bdelay\b"), ("position", r"\bposition\b"),
                   ("velocity", r"\bvelocity\b"), ("health", r"\bhealth\b"), ("flags", r"\bflags\b"),
                   ("state", r"\bstate\b"), ("pointer", r"\bpointer\b|\bthe (?:parent|owner|target)\b")]


def name_category(name):
    for cat, rx in CATEGORY_RE:
        if re.search(rx, name):
            return cat
    return None


def meaning_category(m):
    k = m["kind"]
    return {"countdown": "timer", "stopwatch": "timer", "timestamp": "timer", "frame_counter": None, "position": "position",
            "matrix_translation": "position", "velocity": "velocity", "health": "health", "state": "state",
            "state_linked": None,
            "flags": "flags", "pointer": "pointer", "bool": "bool"}.get(k)


def known_members():
    """(class, offset) -> [{"name", "size", "source"}] from symbols/types/data_members.csv (the game's own tables)."""
    out = collections.defaultdict(list)
    for r in csv.DictReader(open(os.path.join(ROOT, "symbols", "types", "data_members.csv"))):
        out[(r["class"], int(r["offset"], 16))].append({"name": r["member"], "size": int(r["size"]),
                                                         "count": int(r["count"]), "source": "data_members"})
    return out


def docs_mentions():
    out = collections.defaultdict(list)
    for path in glob.glob(os.path.join(ROOT, "docs", "subsystems", "*.md")):
        cls = DOC_CLASS.get(os.path.basename(path))
        if not cls:
            continue
        text = open(path, encoding="utf-8", errors="replace").read()
        for m in re.finditer(r"(?<![\w+])(\w+\s*)?\+0x([0-9a-f]{2,4})\b([^|\n]{0,70})", text):
            prefix = (m.group(1) or "").strip()
            if prefix and re.match(r"^(Game|World|g|G|vt|list|entity|motion|island|record|rec|desc|fx|hud|slot|block|"
                                   r"s|e|t|m|p|r|Player|P|weapon|turret|camera|cam|ui|matrix)$", prefix):
                continue
            ctx = re.sub(r"\s+", " ", m.group(3)).split("+0x")[0].strip(" ,;:)(/")
            out[(cls, int(m.group(2), 16))].append(f"{os.path.basename(path)}: {ctx[:70]}")
    return out


def effective(cls, off):
    """The classes whose records describe cls+off: cls and each ancestor whose size covers off."""
    cm = class_model()["classes"]
    out = []
    for a in ancestors(cls) or [cls]:
        size = cm.get(a, {}).get("size", 1 << 30)
        if off < size or a == cls:
            out.append(a)
    return out


def infer_fields(static, facts):
    cm = class_model()["classes"]
    fr = static["functions"]
    fields = collections.defaultdict(dict)

    def F(cls, off):
        d = fields[cls].get(off)
        if d is None:
            d = fields[cls][off] = {"off": off, "types": collections.Counter(), "rw": collections.Counter(),
                                    "funcs": collections.Counter(), "where": [], "cmp": collections.Counter(),
                                    "store": collections.Counter(), "masks": set(), "as_ptr": collections.Counter(),
                                    "n": 0, "props": [], "names": [], "via_uses": collections.Counter(), "indexed": 0}
        return d

    for r in static["accesses"]:
        if not r["cls"]:
            continue
        d = F(owner(r["cls"], r["off"]), r["off"])
        if owner(r["cls"], r["off"]) != r["cls"]:
            d.setdefault("via_class", collections.Counter())[r["cls"]] += 1
        d["n"] += 1
        if r["type"]:
            d["types"][r["type"]] += 1
        d["rw"][r["rw"]] += 1
        d["funcs"][r["func"]] += 1
        if len(d["where"]) < 6:
            d["where"].append(r["where"])
        if "cmp" in r and r["cmp"][1] is not None:
            d["cmp"][f"{r['cmp'][0]} {fmt_const(r['cmp'][1])}"] += 1
        if "store" in r and r["store"] is not None:
            d["store"][fmt_const(r["store"])] += 1
        if "mask" in r:
            d["masks"].add(r["mask"][1])
        if r.get("as_ptr"):
            d["as_ptr"][r["as_ptr"]] += 1
        if r.get("indexed"):
            d["indexed"] += 1
    for r in static["accesses"]:
        v = r.get("via")
        if v and "+" in v and ">" not in v:
            c, o = v.split("+")
            d = F(owner(c, int(o, 16)), int(o, 16))
            d["via_uses"][f"+0x{r['off']:x} {r['type'] or 'addr'}"] += 1
            d["as_ptr"]["base"] += 1
    for p in static["props"]:
        if p["cls"] and p["off"] is not None:
            d = F(owner(p["cls"], p["off"]), p["off"])
            e = {"prop": p["prop"], "kind": p["kind"], "how": p["how"], "where": p["where"]}
            for k in ("default", "mask"):
                if p.get(k) is not None:
                    e[k] = p[k]
            if not any(x["prop"] == e["prop"] and x.get("mask") == e.get("mask") for x in d["props"]):
                d["props"].append(e)
    known = known_members()
    for (c, o), lst in known.items():
        d = F(c, o)
        d["names"].extend(lst)
    docs = docs_mentions()
    for (c, o), lst in docs.items():
        lst = [x for x in lst if x.split(": ", 1)[-1].strip()]
        if lst:
            F(owner(c, o), o).setdefault("docs", []).extend(lst[:4])
    for c, o, cat, text in TRACED:
        F(owner(c, o), o)["traced"] = {"category": cat, "what": text}
    # dynamic facts are per concrete class (the vtable's): merged onto the class that owns the offset
    dyn_lists = collections.defaultdict(list)
    for (c, o), fct in facts.items():
        oc = owner(c, o)
        if o in fields.get(oc, {}) and "byte" in fct:
            F(oc, o).setdefault("dynbyte", []).append(fct["byte"])
        if "guess" not in fct:
            continue
        dyn_lists[(oc, o)].append((c, fct))
    for (oc, o), lst in dyn_lists.items():
        merged = merge_facts(lst)
        if o in fields.get(oc, {}) or (merged.get("guess") != "zero" and (merged.get("change_frac", 0) > 0 or merged.get("tests"))):
            F(oc, o)["dyn"] = merged
    out = {}
    for cls, offs in fields.items():
        for off, d in offs.items():
            dyn = d.get("dyn")
            rec = finish_field(cls, off, d, dyn, fr)
            out.setdefault(cls, {})[off] = rec
    return out


def owner(cls, off):
    """The class that defines cls+off: the most basic ancestor whose size still covers the offset."""
    cm = class_model()["classes"]
    best = cls
    for a in ancestors(cls)[1:]:
        if off < cm.get(a, {}).get("size", 0):
            best = a
        else:
            break
    return best


def merge_facts(lst):
    """One view of an offset from the facts of several concrete classes (tests and targets summed)."""
    if len(lst) == 1:
        c, f = lst[0]
        out = dict(f)
        out["classes"] = [c]
        return out
    n = sum(f["n"] for _, f in lst) or 1
    guesses = collections.Counter()
    for _, f in lst:
        guesses[f.get("guess")] += f["n"] if f.get("guess") != "zero" else f["n"] * 0.01
    out = {"n": n, "objects": sum(f["objects"] for _, f in lst), "guess": guesses.most_common(1)[0][0],
           "classes": [c for c, _ in sorted(lst, key=lambda x: -x[1]["n"])]}
    for k in ("zero_frac", "change_frac", "float_frac", "smallint_frac", "heap_frac"):
        vals = [(f[k], f["n"]) for _, f in lst if f.get(k) is not None]
        if vals:
            out[k] = round(sum(v * w for v, w in vals) / sum(w for _, w in vals), 3)
    out["distinct"] = max(f.get("distinct", 0) for _, f in lst)
    out["per_object_constant"] = all(f.get("per_object_constant") for _, f in lst)
    mins = [f["min"] for _, f in lst if "min" in f and f.get("guess") == out["guess"]]
    maxs = [f["max"] for _, f in lst if "max" in f and f.get("guess") == out["guess"]]
    if mins:
        out["min"], out["max"] = min(mins), max(maxs)
    if all("values" in f or f.get("guess") == "zero" for _, f in lst):
        vs = sorted({v for _, f in lst for v in f.get("values", [0])})
        if len(vs) <= 24:
            out["values"] = vs
    tg = collections.Counter()
    tests = {}
    for _, f in lst:
        tg.update(f.get("targets", {}))
        for k, (a, b) in f.get("tests", {}).items():
            x = tests.setdefault(k, [0, 0])
            x[0] += a
            x[1] += b
    if tg:
        out["targets"] = dict(tg.most_common(4))
    if tests:
        out["tests"] = tests
    bits = [int(f["bits_seen"], 16) for _, f in lst if f.get("bits_seen")]
    if bits:
        b = 0
        for x in bits:
            b |= x
        out["bits_seen"] = f"0x{b:x}"
    mf = [(f["mapped_frac"], f["n"]) for _, f in lst if f.get("mapped_frac") is not None]
    if mf:
        out["mapped_frac"] = round(sum(v * w for v, w in mf) / sum(w for _, w in mf), 3)
    return out


def fmt_const(v):
    if isinstance(v, float):
        return f"{v:g}"
    if isinstance(v, int) and (v > 255 or v < -255):
        return f"0x{v & 0xffffffff:x}"
    return str(v)


def finish_field(cls, off, d, dyn, fr):
    types = d["types"]
    stype = types.most_common(1)[0][0] if types else None
    # the recovered code moves floats as dwords (D(a) = D(b), Copy3): any float use makes an unmasked dword a float
    int_math = bool(d["masks"]) or any(re.match(r"^[<>=!]+ -?\d+$", c) and not c.endswith(" 0") for c in d["cmp"])
    if stype in ("u32", "i32") and types.get("f32", 0) and not int_math:
        stype = "f32"
    width = max((WIDTH.get(x, 0) for x in types), default=0) or None
    rec = {"off": f"0x{off:x}", "n": d["n"], "type": stype, "types": dict(types), "width": width,
           "rw": dict(d["rw"]), "funcs": [f"{k} {fr.get(k, {}).get('name', '')}".strip() for k, _ in d["funcs"].most_common(6)],
           "where": d["where"][:4]}
    if d["cmp"]:
        rec["cmp"] = [k for k, _ in d["cmp"].most_common(6)]
    if d["store"]:
        rec["store"] = [k for k, _ in d["store"].most_common(6)]
    if d["masks"]:
        rec["masks"] = sorted(f"0x{m:x}" for m in d["masks"])[:12]
    if d["via_uses"]:
        rec["pointee_fields"] = [k for k, _ in d["via_uses"].most_common(6)]
    if d["props"]:
        rec["props"] = d["props"][:6]
    if d["names"]:
        rec["names"] = d["names"]
    if d.get("docs"):
        rec["docs"] = d["docs"]
    if d.get("traced"):
        rec["traced"] = d["traced"]
    if d["indexed"]:
        rec["indexed"] = d["indexed"]
    if d.get("via_class"):
        rec["used_from"] = [f"{k} {v}" for k, v in d["via_class"].most_common(4)]
    if d.get("dynbyte"):
        bv = sorted({v for b in d["dynbyte"] for v in b["values"]})
        rec["byte_values"] = bv[:8]
    if dyn:
        rec["dyn"] = {k: v for k, v in dyn.items() if k not in ("n",)}
        rec["dyn"]["samples"] = dyn["n"]
    meanings = []

    def add(kind, text, conf, evidence):
        meanings.append({"kind": kind, "meaning": text, "confidence": round(conf, 2), "evidence": evidence})

    tests = (dyn or {}).get("tests", {})

    def ratio(t):
        ok, tot = tests.get(t, [0, 0])
        return ok / tot if tot else 0.0, ok, tot

    # property names: the strongest naming evidence
    for p in d["props"][:3]:
        if p["how"] in ("reader", "table"):
            add("property", f"property \"{p['prop']}\" ({p['kind']}"
                + (f", default {p['default']}" if "default" in p else "") + ")", 0.9,
                f"{p['how']} at {p['where']}")
        elif p.get("mask") is not None:
            add("flag", f"bit 0x{p['mask']:x} = property \"{p['prop']}\"", 0.8, f"{p['how']} at {p['where']}")
        else:
            add("resource", f"\"{p['prop']}\" (named in the statement that sets it)", 0.5, f"{p['how']} at {p['where']}")
    r, ok, tot = ratio("countdown")
    if ok >= 3 and r >= 0.6:
        add("countdown", "countdown timer (seconds): falls by the frame time", min(0.95, r),
            f"dynamic: {ok}/{tot} changes equal -dt")
    r, ok, tot = ratio("equals_clock")
    if ok >= 3 and r >= 0.5:
        add("timestamp", f"timestamp: holds the game clock ({ok}/{tot} samples equal it)", min(0.9, 0.4 + r / 2),
            "dynamic: equals the game clock (0x00604a98) at the sample")
    r, ok, tot = ratio("stopwatch")
    if ok >= 3 and r >= 0.6 and not any(m["kind"] == "timestamp" and m["confidence"] >= 0.8 for m in meanings):
        add("stopwatch", "stopwatch / accumulator (seconds): grows by the frame time", min(0.95, r),
            f"dynamic: {ok}/{tot} changes equal +dt")
    r, ok, tot = ratio("frame_counter")
    if ok >= 3 and r >= 0.6:
        add("frame_counter", "frame counter: +1 per frame", min(0.95, r), f"dynamic: {ok}/{tot} changes equal frames")
    r, ok, tot = ratio("frame_countdown")
    if ok >= 3 and r >= 0.6:
        add("frame_counter", "frame countdown: -1 per frame", min(0.95, r), f"dynamic: {ok}/{tot}")
    r, ok, tot = ratio("position")
    if ok >= 3 and r >= 0.9:
        add("position", "position x (+4 y, +8 z): equals the object's position (vt+0x54)", min(0.97, r),
            f"dynamic: {ok}/{tot} samples")
    r, ok, tot = ratio("matrix_translation")
    if ok >= 3 and r >= 0.9:
        add("matrix_translation", "world matrix translation x (+0x10 y, +0x20 z; the matrix starts at -0xc)",
            min(0.97, r), f"dynamic: {ok}/{tot} samples")
    r, ok, tot = ratio("velocity")
    if ok >= 3 and r >= 0.7:
        add("velocity", "velocity x (+4 y, +8 z): the position's derivative", min(0.9, r), f"dynamic: {ok}/{tot} moving samples")
    r, ok, tot = ratio("decreasing")
    zr, zok, ztot = ratio("zero_at_death")
    dead_cmp = any(c.startswith(("<= 0", "< 0", "> 0")) for c in rec.get("cmp", []))
    if tot >= 2 and r >= 0.9 and (dyn or {}).get("guess") == "f32" and (ztot or dead_cmp) and \
            (dyn or {}).get("min", 0) > -1000 and not any(m["kind"] in ("countdown", "stopwatch") for m in meanings):
        conf = 0.45 + (0.4 if ztot and zok == ztot else 0) + (0.1 if dead_cmp else 0)
        if ztot and zok < ztot:
            conf = 0.3
        add("health", "health-like: only falls" + (f", <= 0 at the traced death ({zok}/{ztot})" if ztot else "")
            + (", tested against 0" if dead_cmp else ""), conf, f"dynamic: {ok}/{tot} changes downwards")
    # pointers
    tg = (dyn or {}).get("targets")
    if (dyn or {}).get("guess") == "ptr" or d["as_ptr"] or stype == "ptr":
        if tg:
            best, cnt = max(tg.items(), key=lambda x: x[1])
            total = sum(tg.values())
            conf = (0.85 if total >= 5 else 0.6) if (dyn or {}).get("guess") == "ptr" else 0.5
            if cnt < total * 0.6:
                conf -= 0.2
            add("pointer", f"pointer to {best}", conf, "dynamic targets " + ", ".join(f"{k} x{v}" for k, v in tg.items()))
        elif d["as_ptr"]:
            add("pointer", "pointer (" + ", ".join(f"{k}" for k in d["as_ptr"]) + ")", 0.7,
                "static: " + ", ".join(f"{k} x{v}" for k, v in d["as_ptr"].items()))
        elif (dyn or {}).get("guess") == "ptr":
            add("pointer", "pointer (heap)", 0.6, f"dynamic: {dyn['heap_frac']} of non-zero values are heap addresses")
    # flags / enums / bools
    masks = d["masks"]
    br, bok, btot = ratio("bitflips")
    if len(masks) >= 2 or (len(masks) == 1 and stype in ("u32", "i32")) or any(p.get("mask") is not None for p in d["props"]):
        add("flags", "flags bitfield (bits " + ", ".join(sorted(f"0x{m:x}" for m in masks)[:8]) + ")", 0.8 if len(masks) >= 2 else 0.6,
            "static: masked / set / cleared with constants" + (f"; dynamic {bok}/{btot} changes flip 1-2 bits" if btot else ""))
    elif btot >= 3 and br >= 0.9 and (dyn or {}).get("distinct", 0) > 2 and not d["cmp"]:
        add("flags", "flags bitfield" + (f" (bits seen {dyn['bits_seen']})" if dyn.get("bits_seen") else ""), 0.5,
            f"dynamic: {bok}/{btot} changes flip 1-2 bits")
    sr, sok, stot = ratio("with_ai_state")
    if stot >= 8 and sr >= 0.5 and "GenericAIObject" in ancestors(cls):
        add("state_linked", f"changes with the AI state +0x460 ({sok}/{stot} changes)", min(0.8, sr),
            "dynamic: changes in the same sample interval as the traced AI state")
    cmpvals = {c.split(" ", 1)[1] for c in d["cmp"] if c.startswith(("==", "!="))}
    vals = (dyn or {}).get("values")
    if stype in ("u32", "i32", "u16", "i16") and len(cmpvals) >= 3 and not masks:
        add("state", "state / enum (compared with " + ", ".join(sorted(cmpvals)[:8]) + ")", 0.7 if (dyn or {}).get("change_frac", 0) > 0 else 0.55,
            "static: equality tests against several constants" + (f"; dynamic values {vals[:10]}" if vals else ""))
    elif vals and 2 < len(vals) <= 24 and (dyn or {}).get("guess") == "int" and (dyn or {}).get("change_frac", 0) > 0 and \
            max(vals) < 0x1000 and min(vals) >= -1 and not masks and not tests.get("frame_counter"):
        add("state", f"state / enum (values {vals[:10]})", 0.5, "dynamic: a small set of values that changes")
    if stype in ("u8", "bool") and d.get("dynbyte"):
        vals = sorted({v for b in d["dynbyte"] for v in b["values"]})
    if stype in ("u8", "bool") and (not vals or set(vals) <= {0, 1}):
        stores = set(d["store"])
        if stores and stores <= {"0", "1"}:
            add("bool", "bool (stores 0 / 1)", 0.7, "static: only 0 / 1 stored")
    if dyn and dyn.get("per_object_constant") and dyn.get("guess") not in ("zero",) and not any(m["kind"] in ("countdown", "position") for m in meanings):
        w = d["rw"]
        add("constant", "constant per object while traced (set at load / creation)", 0.4,
            f"dynamic: unchanged over {dyn['n']} samples of {dyn['objects']} objects")
    rec["meanings"] = sorted(meanings, key=lambda m: -m["confidence"])
    # type: static, refined by the dynamic view
    if dyn and stype in ("u32", "i32") and dyn.get("guess") == "ptr":
        rec["type"] = "ptr"
    elif dyn and not stype and dyn.get("guess") in ("f32", "ptr"):
        rec["type"] = dyn["guess"]
        rec["type_from"] = "dynamic"
    elif dyn and stype in ("u32", "i32") and dyn.get("guess") == "f32" and dyn.get("smallint_frac", 1) < 0.2 and not int_math:
        rec["type"] = "f32"
        rec["type_from"] = "dynamic (the C++ only copies it as a dword)"
    # contradictions
    contra = []
    if dyn and stype == "f32" and dyn.get("guess") == "ptr":
        contra.append("static float, dynamic values look like heap pointers")
    if dyn and stype in ("u32", "i32") and dyn.get("guess") == "f32" and dyn.get("smallint_frac", 1) < 0.2 and int_math:
        contra.append("integer arithmetic in the C++ (masks / integer compares), dynamic values look like floats")
    if types.get("f32") and d["masks"]:
        contra.append("used as a float and masked as an integer (" + ", ".join(f"{k} {v}" for k, v in types.items()) + ")")
    for nm in d["names"]:
        if width and nm["count"] == 1 and nm["size"] in (1, 2, 4) and nm["size"] != width and not d["indexed"]:
            contra.append(f"width {width} vs the game's table {nm['name']} size {nm['size']}")
    if contra:
        rec["contradictions"] = contra
    return rec


def best_label(rec):
    if rec.get("names"):
        return rec["names"][0]["name"]
    for m in rec.get("meanings", []):
        if m["kind"] == "property":
            return re.search(r'"([^"]+)"', m["meaning"]).group(1)
    if rec.get("traced"):
        return rec["traced"]["what"]
    for m in rec.get("meanings", []):
        if m["kind"] not in ("constant",):
            return m["meaning"]
    return ""


def cmd_infer(a):
    static = json.load(open(STATIC_JSON))
    dirs = snap_dirs()
    facts, nseries = ({}, {})
    if dirs:
        bytes_used = {r["off"] for r in static["accesses"] if r["cls"] and (r["type"] in ("u8", "i8", "bool") or r["off"] % 4)}
        facts, nseries = analyse_dynamic(dirs, byte_offsets=bytes_used)
    fields = infer_fields(static, facts)
    cm = class_model()["classes"]
    atlas = {"classes": {}, "props": static["props"], "dynamic_objects": nseries, "scenarios": [os.path.basename(d) for d in dirs]}
    for cls, offs in fields.items():
        c = cm.get(cls, {})
        atlas["classes"][cls] = {"size": c.get("size"), "base": c.get("base"), "vtables": c.get("vtables", []),
                                 "fields": {f"0x{o:x}": offs[o] for o in sorted(offs)}}
    rep = coverage_report(static, atlas)
    prec = precision_report(atlas)
    atlas["coverage"] = rep
    atlas["precision"] = prec
    json.dump(atlas, open(ATLAS_JSON, "w"), indent=0, default=str)
    for line in rep["lines"] + prec["lines"]:
        print(line)


TESTED_KINDS = ("countdown", "stopwatch", "timestamp", "frame_counter", "position", "matrix_translation", "velocity",
                "health", "state_linked")


def coverage_report(static, atlas):
    acc = static["accesses"]
    attributed = [r for r in acc if r["cls"]]
    lines = []
    total_pairs = typed = named = tested = meant = 0
    per = {}
    for cls, c in atlas["classes"].items():
        p = t = n = te = me = 0
        for off, f in c["fields"].items():
            if not f["n"]:
                continue
            p += 1
            t += 1 if f.get("type") else 0
            n += 1 if (f.get("names") or f.get("props") or f.get("traced")) else 0
            te += 1 if any(m["kind"] in TESTED_KINDS or (m["kind"] == "pointer" and "dynamic" in m["evidence"])
                           for m in f.get("meanings", [])) else 0
            me += 1 if any(m["kind"] != "constant" for m in f.get("meanings", [])) or f.get("names") else 0
        per[cls] = (p, t, n, te, me)
        total_pairs += p
        typed += t
        named += n
        tested += te
        meant += me
    lines.append(f"COVERAGE accesses {len(acc)} (all `base + 0x..` member uses in src/), attributed to a class "
                 f"{len(attributed)} ({100 * len(attributed) // max(1, len(acc))}%); object classes only (not Game / "
                 f"World) {sum(1 for r in attributed if r['cls'] not in ('Game', 'World'))}")
    # access-weighted: the share of the object-class accesses whose field has a name / a meaning / a tested meaning
    fidx = {(c, int(o, 16)): f for c, cc in atlas["classes"].items() for o, f in cc["fields"].items()}
    w_named = w_meant = w_tested = w_typed = w_all = 0
    for r in attributed:
        if r["cls"] in ("Game", "World"):
            continue
        f = fidx.get((owner(r["cls"], r["off"]), r["off"]))
        if not f:
            continue
        w_all += 1
        w_typed += bool(f.get("type"))
        w_named += bool(f.get("names") or f.get("props") or f.get("traced"))
        w_meant += bool(f.get("names") or any(m["kind"] != "constant" for m in f.get("meanings", [])))
        w_tested += any(m["kind"] in TESTED_KINDS or (m["kind"] == "pointer" and "dynamic" in m["evidence"])
                        for m in f.get("meanings", []))
    lines.append(f"COVERAGE object-class accesses {w_all}: on a typed field {w_typed} ({100 * w_typed // max(1, w_all)}%), "
                 f"named {w_named} ({100 * w_named // max(1, w_all)}%), named or with a meaning {w_meant} "
                 f"({100 * w_meant // max(1, w_all)}%), dynamically tested {w_tested} ({100 * w_tested // max(1, w_all)}%)")
    lines.append(f"COVERAGE (class, offset) pairs used by the C++ {total_pairs}: typed {typed}, named {named} "
                 f"(game tables / properties / trace), with an inferred meaning or name {meant}, with a dynamically "
                 f"tested meaning {tested}")
    for cls in MAIN_CLASSES:
        if cls in per:
            p, t, n, te, me = per[cls]
            lines.append(f"COVERAGE {cls}: {p} offsets used, typed {t}, named {n}, meaning/name {me}, tested {te}")
    return {"lines": lines, "per_class": per, "pairs": total_pairs, "typed": typed, "named": named, "tested": tested,
            "meaning": meant}


def precision_report(atlas):
    """Inferred meanings against what is known independently: the game's member tables (names), the oracle trace's
    fields, and property names against member names."""
    checks = []
    for cls, c in atlas["classes"].items():
        for off, f in c["fields"].items():
            truth = []
            for nm in f.get("names", []):
                cat = name_category(nm["name"])
                if cat:
                    truth.append((cat, f"table {nm['name']}"))
            if f.get("traced"):
                truth.append((f["traced"]["category"], "trace " + f["traced"]["what"]))
            # the subsystem docs' words right after the offset (weaker: a separate tally)
            for dm in f.get("docs", []):
                ctx = dm.split(": ", 1)[-1][:40].lower()
                for cat, rx in DOC_CATEGORY_RE:
                    if re.search(rx, ctx):
                        truth.append((cat, "docs " + dm[:60]))
                        break
            inferred = [(meaning_category(m), m) for m in f.get("meanings", []) if meaning_category(m)]
            # the type is a checked inference too (traced kinds float / int / bool / pointer); meanings by category
            tmap = {"f32": "float", "ptr": "pointer", "u32": "int", "i32": "int", "u8": "bool", "u16": "int", "i16": "int",
                    "bool": "bool"}
            for cat, why in truth:
                if cat in ("float", "int", "pointer", "bool") and f.get("type") and why.startswith("trace"):
                    got = tmap.get(f["type"])
                    if f.get("dyn", {}).get("guess") == "ptr" or (not f.get("dyn") and any(m["kind"] == "pointer" for _, m in inferred)):
                        got = "pointer"
                    if got:
                        ok = got == cat or (cat == "bool" and f["type"] in ("u8", "bool"))
                        checks.append((cls, off, f"type {f['type']}" + (" (pointer use)" if got == "pointer" else ""), why, ok))
                if cat in ("float", "int") or not inferred:
                    continue
                cats = {x for x, _ in inferred}
                ok = cat in cats
                best = next((m for x, m in inferred if x == cat), inferred[0][1])
                checks.append((cls, off, best["meaning"], f"[{cat}] {why}", ok))
            # width against the game's tables
            for nm in f.get("names", []):
                if f.get("width") and nm["count"] == 1 and nm["size"] in (1, 2, 4):
                    checks.append((cls, off, f"width {f['width']}", f"table {nm['name']} size {nm['size']}",
                                   f["width"] == nm["size"] or (nm["size"] == 4 and f.get("indexed"))))
            # property names against the table names (e.g. "patrol_speed" vs m_patrolSpeed)
            for p in f.get("props", []):
                if p["how"] not in ("reader", "table") or p.get("mask") is not None:
                    continue
                for nm in f.get("names", []):
                    ok = names_agree(p["prop"], nm["name"])
                    checks.append((cls, off, f"property {p['prop']}", f"table {nm['name']}", ok))
    by = collections.defaultdict(lambda: [0, 0])
    for cls, off, inf, why, ok in checks:
        k = inf.split(" ")[0] if inf.startswith(("width", "property", "type")) else \
            ("meaning vs docs" if "] docs" in why else "meaning")
        by[k][0] += bool(ok)
        by[k][1] += 1
    agree = sum(1 for x in checks if x[4])
    lines = [f"PRECISION {len(checks)} inferences checked against known names / traced fields: {agree} agree "
             f"({100 * agree // max(1, len(checks))}%); " + ", ".join(f"{k} {v[0]}/{v[1]}" for k, v in by.items())]
    cat_tally = collections.defaultdict(lambda: [0, 0])
    for cls, off, inf, why, ok in checks:
        m = re.match(r"^\[(\w+)\] (table|trace)", why)
        if m:
            cat_tally[m.group(1)][0] += bool(ok)
            cat_tally[m.group(1)][1] += 1
    lines.append("PRECISION meanings by known category (tables + trace): " +
                 ", ".join(f"{k} {v[0]}/{v[1]}" for k, v in sorted(cat_tally.items())))
    for cls, off, inf, why, ok in checks:
        if not ok:
            lines.append(f"PRECISION disagree {cls}+{off}: inferred {inf}; known {why}")
    return {"lines": lines, "checks": [{"cls": c, "off": o, "inferred": i, "known": w, "agree": k}
                                       for c, o, i, w, k in checks], "agree": agree, "total": len(checks)}


# ---------------------------------------------------------------------------------------------------------------------
# Lookups

_atlas = None


def atlas():
    global _atlas
    if _atlas is None:
        if not os.path.exists(ATLAS_JSON):
            raise SystemExit("no work/struct_atlas.json: run `python tools/struct_atlas.py static` then `infer`")
        _atlas = json.load(open(ATLAS_JSON))
    return _atlas


def find_class(name):
    cs = dict(class_model()["classes"])
    cs.update(atlas()["classes"])
    if name in cs:
        return name
    low = {c.lower(): c for c in cs}
    if name.lower() in low:
        return low[name.lower()]
    cands = [c for c in cs if name.lower() in c.lower()]
    if len(cands) == 1:
        return cands[0]
    raise SystemExit(f"unknown class {name}" + (f"; candidates: {', '.join(cands[:10])}" if cands else ""))


def field_line(f, cls_note=""):
    label = best_label(f)
    ms = [m for m in f.get("meanings", []) if m["kind"] != "property"]
    t = f.get("type") or "?"
    rw = f.get("rw", {})
    s = f"+{f['off']:<6} {t:<4} {label[:60]:<60} n={f['n']} r{rw.get('r', 0)} w{rw.get('w', 0) + rw.get('rw', 0)}"
    if ms:
        s += f" | {ms[0]['meaning'][:50]} ({ms[0]['confidence']})"
    if f.get("contradictions"):
        s += " | CONTRADICTION"
    return s + cls_note


def cmd_field(a):
    cls = find_class(a.cls)
    off = int(a.off, 16) if a.off.lower().startswith(("0x", "+0x")) else int(a.off.lstrip("+"), 16)
    shown = False
    own = effective(cls, off)
    subs = [x for x in atlas()["classes"] if cls in ancestors(x)[1:] and f"0x{off:x}" in atlas()["classes"][x]["fields"]]
    for c in own + subs:
        f = atlas()["classes"].get(c, {}).get("fields", {}).get(f"0x{off:x}")
        if not f:
            continue
        if c not in own and not a.all:
            print(f"  (also recorded on subclass {c}: n={f['n']}" + (f", {best_label(f)[:50]}" if best_label(f) else "")
                  + "; --all for details)")
            continue
        shown = True
        print(f"FIELD {c}+0x{off:x}" + (f" (from {cls})" if c != cls else "") + f": type {f.get('type')}, {f['n']} accesses "
              f"(r {f['rw'].get('r', 0)}, w {f['rw'].get('w', 0)}, rw {f['rw'].get('rw', 0)}, addr {f['rw'].get('addr', 0)})")
        for nm in f.get("names", []):
            print(f"  name: {nm['name']} (game member table, size {nm['size']})")
        if f.get("traced"):
            print(f"  traced: {f['traced']['what']}")
        for m in f.get("meanings", [])[:4]:
            print(f"  meaning: {m['meaning']} [conf {m['confidence']}; {m['evidence']}]")
        dyn = f.get("dyn")
        if dyn:
            rng = f" range {dyn.get('min')}..{dyn.get('max')}" if "min" in dyn else ""
            vals = f" values {dyn['values'][:8]}" if dyn.get("values") else ""
            print(f"  dynamic: {dyn.get('guess')}{rng}{vals}, changes {dyn.get('change_frac')}, {dyn.get('objects')} objects"
                  + (f" (seen on {', '.join(dyn['classes'][:4])})" if dyn.get("classes") and dyn["classes"] != [c] else "")
                  + (f", targets {dyn['targets']}" if dyn.get("targets") else ""))
        extra = []
        if f.get("cmp"):
            extra.append("compared " + ", ".join(f["cmp"][:4]))
        if f.get("store"):
            extra.append("stored " + ", ".join(f["store"][:4]))
        if f.get("masks"):
            extra.append("masks " + ", ".join(f["masks"][:6]))
        if f.get("pointee_fields"):
            extra.append("pointee fields " + ", ".join(f["pointee_fields"][:4]))
        if extra:
            print("  static: " + "; ".join(extra))
        print("  functions: " + ", ".join(f["funcs"][:4]) + (f"  e.g. {f['where'][0]}" if f.get("where") else ""))
        for dmt in f.get("docs", [])[:2]:
            print(f"  docs: {dmt}")
        for cx in f.get("contradictions", []):
            print(f"  CONTRADICTION: {cx}")
    if not shown:
        print(f"FIELD {cls}+0x{off:x}: not used by the recovered C++ and not in the game's tables")


def cmd_class(a):
    cls = find_class(a.cls)
    cm = class_model()["classes"].get(cls, {})
    c = atlas()["classes"].get(cls) or {"size": cm.get("size"), "vtables": cm.get("vtables", []), "fields": {}}
    chain = ancestors(cls)
    print(f"CLASS {cls}: size {hex(c['size']) if c.get('size') else '?'}, base {' > '.join(chain[1:]) or '-'}, "
          f"vtables {', '.join(c.get('vtables', [])[:3])}; {len(c['fields'])} fields in the atlas")
    rows = [(int(o, 16), f, cls) for o, f in c["fields"].items()]
    if a.all or not rows:
        for anc in chain[1:]:
            for o, f in atlas()["classes"].get(anc, {}).get("fields", {}).items():
                rows.append((int(o, 16), f, anc))
    for o, f, owner in sorted(rows, key=lambda x: x[0]):
        if a.min_n and f["n"] < a.min_n and not f.get("names") and not f.get("props"):
            continue
        print(field_line(f, f"  [{owner}]" if owner != cls else ""))


def cmd_prop(a):
    hits = [p for p in atlas()["props"] if p["prop"].lower() == a.name.lower() or (a.like and a.name.lower() in p["prop"].lower())]
    if not hits:
        print(f"PROP {a.name}: not found (try --like)")
        return
    seen = set()
    for p in hits:
        k = (p["prop"], p["cls"], p["off"], p.get("mask"))
        if k in seen:
            continue
        seen.add(k)
        off = f"+0x{p['off']:x}" if p["off"] is not None else "(local)"
        print(f"PROP {p['prop']}: {p['cls'] or '?'}{off} {p['kind']}" + (f" bit 0x{p['mask']:x}" if p.get("mask") else "")
              + (f" default {p['default']}" if "default" in p else "") + f"  ({p['how']} {p['where']})")

# ---------------------------------------------------------------------------------------------------------------------
# Docs: docs/structs/<Class>.md (committed summaries) and the proposed member names

PROPOSALS = os.path.join(ROOT, "symbols", "types", "data_members_proposed.csv")


def md_escape(s):
    return str(s).replace("|", "\\|")


def cmd_docs(a):
    at = atlas()
    os.makedirs(os.path.join(ROOT, "docs", "structs"), exist_ok=True)
    classes = a.classes.split(",") if a.classes else MAIN_CLASSES
    written = []
    for cls in classes:
        if cls not in at["classes"]:
            continue
        c = at["classes"][cls]
        chain = ancestors(cls)
        fields = [f for f in c["fields"].values() if f["n"] or f.get("names") or f.get("props")]
        lines = [f"# {cls}", "",
                 f"Generated by `python tools/struct_atlas.py docs` from work/struct_atlas.json (static index of src/, "
                 f"dynamic snapshots of the original: {', '.join(at.get('scenarios', [])[:6]) or 'none'}"
                 f"{' ...' if len(at.get('scenarios', [])) > 6 else ''}). **Inferred, not fact** unless the name column "
                 f"cites the game's member table or the trace. Labels and confidence: docs/structs/README.md. Look one "
                 f"field up with `python tools/struct_atlas.py field {cls} 0xOFF` (functions, sites, values).", "",
                 f"Size {hex(c['size']) if c.get('size') else '?'}; base {' > '.join(chain[1:]) or '-'}; vtables "
                 f"{', '.join(c.get('vtables', [])) or '-'}. Fields below: offsets the recovered C++ uses on this "
                 f"class (inherited offsets are listed in the base class's file).", "",
                 "| Offset | Type | Name / meaning | Evidence | Uses (r/w) |", "| --- | --- | --- | --- | --- |"]
        for f in sorted(fields, key=lambda f: int(f["off"], 16)):
            name = ""
            if f.get("names"):
                name = "**" + ", ".join(n["name"] for n in f["names"]) + "** (table)"
            elif f.get("traced"):
                name = "**" + f["traced"]["what"] + "**"
            ms = f.get("meanings", [])
            desc = "; ".join(f"{m['meaning']} ({m['confidence']})" for m in ms[:2])
            ev = "; ".join(m["evidence"] for m in ms[:1])
            if not ev and f.get("where"):
                ev = f["where"][0]
            dyn = f.get("dyn")
            if dyn and dyn.get("guess") not in (None, "zero") and "values" not in ev and "seen" not in ev:
                rng = f" {dyn.get('min')}..{dyn.get('max')}" if "min" in dyn else ""
                ev += (f"; seen {dyn['guess']}{rng}" if not dyn.get("values") else f"; values {dyn['values'][:6]}")
            if f.get("contradictions"):
                ev += "; **contradiction**: " + "; ".join(f["contradictions"])
            rw = f.get("rw", {})
            lines.append(f"| +{f['off']} | {f.get('type') or '?'} | {md_escape((name + ' ' + desc).strip())} | "
                         f"{md_escape(ev)} | {f['n']} ({rw.get('r', 0)}/{rw.get('w', 0) + rw.get('rw', 0)}) |")
        path = os.path.join(ROOT, "docs", "structs", f"{cls}.md")
        open(path, "w", encoding="utf-8", newline="\n").write("\n".join(lines) + "\n")
        written.append((cls, len(fields)))
    # proposals: fields without a table name that have a property name or a tested meaning
    rows = []
    for cls, c in at["classes"].items():
        for off, f in c["fields"].items():
            if f.get("names"):
                continue
            prop = next((p for p in f.get("props", []) if p["how"] in ("reader", "table")), None)
            m = next((m for m in f.get("meanings", []) if m["kind"] in ("countdown", "stopwatch", "position",
                                                                        "matrix_translation", "velocity", "health")
                      and m["confidence"] >= 0.6), None)
            ptr = next((m for m in f.get("meanings", []) if m["kind"] == "pointer" and "dynamic targets" in m["evidence"]), None)
            if prop:
                member = prop["prop"]
                prov = f"struct_atlas:property {prop['how']} {prop['where']}"
                conf = 0.9
            elif m:
                member = {"countdown": "timer", "stopwatch": "elapsed", "position": "pos", "matrix_translation": "matrix_t",
                          "velocity": "vel", "health": "health_like"}[m["kind"]] + f"_{off[2:]}"
                prov = f"struct_atlas:dynamic {m['kind']} {m['evidence']}"
                conf = m["confidence"]
            elif ptr and not ptr["meaning"].startswith("pointer to vtable") and ptr["confidence"] >= 0.6:
                tgt = ptr["meaning"].replace("pointer to ", "")
                member = "p" + re.sub(r"\W", "", tgt) + f"_{off[2:]}"
                prov = f"struct_atlas:dynamic pointer ({ptr['evidence']})"
                conf = ptr["confidence"]
            else:
                continue
            rows.append({"class": cls, "member": member, "offset": off, "size": f.get("width") or "",
                         "type": f.get("type") or "", "confidence": conf, "provenance": prov, "status": "inferred"})
    rows.sort(key=lambda r: (r["class"], int(r["offset"], 16)))
    with open(PROPOSALS, "w", newline="", encoding="utf-8") as fh:
        w = csv.DictWriter(fh, fieldnames=["class", "member", "offset", "size", "type", "confidence", "provenance", "status"],
                           lineterminator="\n")
        w.writeheader()
        w.writerows(rows)
    print("DOCS " + ", ".join(f"{c} {n}" for c, n in written) + f"; proposals {len(rows)} -> {os.path.relpath(PROPOSALS, ROOT)}")


# ---------------------------------------------------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("classes")
    sub.add_parser("static")
    p = sub.add_parser("run", help="the original on oracle scenarios with struct snapshots (ORACLE_OUT)")
    p.add_argument("scenarios", nargs="+")
    p.add_argument("--every", type=int, default=4)
    p.add_argument("--per-class", type=int, default=4)
    p.add_argument("--ptr-every", type=int, default=8)
    p.add_argument("--classes", default="")
    p.add_argument("--frames", type=int, default=0, help="cap the scenario's play frames")
    p.add_argument("--timeout", type=float, default=900)
    p.add_argument("--instance", default="oracle-structatlas")
    p = sub.add_parser("dynamic")
    p.add_argument("dirs", nargs="*")
    sub.add_parser("infer")
    p = sub.add_parser("field")
    p.add_argument("cls")
    p.add_argument("off")
    p.add_argument("--all", action="store_true", help="also the subclasses' records")
    p = sub.add_parser("class")
    p.add_argument("cls")
    p.add_argument("--all", action="store_true", help="also the inherited fields")
    p.add_argument("--min-n", type=int, default=0, help="hide fields with fewer uses (unless named)")
    p = sub.add_parser("prop")
    p.add_argument("name")
    p.add_argument("--like", action="store_true")
    p = sub.add_parser("docs")
    p.add_argument("--classes", default="")
    a = ap.parse_args()
    if a.cmd == "classes":
        cm = build_classes()
        print(f"CLASSES {len(cm['classes'])} descriptors, {len(cm['vtables'])} vtables, "
              f"{sum(len(c['members']) for c in cm['classes'].values())} DATA members")
    elif a.cmd == "static":
        cmd_static(a)
    elif a.cmd == "run":
        cmd_run(a)
    elif a.cmd == "dynamic":
        cmd_dynamic(a)
    elif a.cmd == "infer":
        cmd_infer(a)
    elif a.cmd == "field":
        cmd_field(a)
    elif a.cmd == "class":
        cmd_class(a)
    elif a.cmd == "prop":
        cmd_prop(a)
    elif a.cmd == "docs":
        cmd_docs(a)


if __name__ == "__main__":
    sys.exit(main())
