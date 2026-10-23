"""Minimal vs_1_1/vs_2_0 disassembler: groups the game's vertex shaders (work/shaders, from [test] dump_shaders=1)
by how they compute oPos (the position dependency chain; write masks are not decoded).
Usage: vs_position_chains.py [how many shapes to print]"""
import collections
import glob
import struct
import os
import sys

OPS = {1: "mov", 2: "add", 3: "sub", 4: "mad", 5: "mul", 6: "rcp", 7: "rsq", 8: "dp3", 9: "dp4", 10: "min", 11: "max",
       12: "slt", 13: "sge", 14: "exp", 15: "log", 16: "lit", 17: "dst", 18: "lrp", 19: "frc", 20: "m4x4", 21: "m4x3",
       22: "m3x4", 23: "m3x3", 24: "m3x2", 31: "dcl", 46: "mova", 81: "def"}
TYPES = {0: "r", 1: "v", 2: "c", 3: "a", 4: "o", 5: "oD", 6: "oT"}


def reg(tok, dst=False):
    t = ((tok >> 28) & 7) | ((tok >> 8) & 0x18)
    n = tok & 0x7ff
    name = TYPES.get(t, f"t{t}") + str(n)
    if t == 4:
        name = ["oPos", "oFog", "oPts"][n]
    if not dst and tok & 0x2000:
        name = f"c[a0.x+{n}]"
    return name


def dis(path):
    d = open(path, "rb").read()
    toks = struct.unpack(f"<{len(d) // 4}I", d)
    out, i = [], 1
    while i < len(toks):
        t = toks[i]
        if t == 0xFFFF:
            break
        op = t & 0xFFFF
        if op == 0xFFFE:  # comment
            i += 1 + ((t >> 16) & 0x7FFF)
            continue
        ln = (t >> 24) & 0xF
        if ln == 0:  # vs_1_1: no length field; parameter tokens have bit 31 set
            j = i + 1
            while j < len(toks) and toks[j] & 0x80000000:
                j += 1
            ln = j - i - 1
        args = toks[i + 1:i + 1 + ln]
        name = OPS.get(op, f"op{op}")
        if name not in ("dcl", "def") and args:
            out.append((name, reg(args[0], True), [reg(a) for a in args[1:]]))
        i += 1 + ln
    return out


def pos_chain(ins):
    need, chain = {"oPos"}, []
    for name, dst, srcs in reversed(ins):
        if dst in need:
            chain.append(f"{name} {dst}, {', '.join(srcs)}")
            need |= {s for s in srcs if s[0] in "rv"}
    return list(reversed(chain))


shapes = collections.Counter()
examples = {}
for p in sorted(glob.glob(os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), "work", "shaders", "vs_*.bin"))):
    ch = pos_chain(dis(p))
    key = "\n".join(ch)
    shapes[key] += 1
    examples.setdefault(key, p)
for key, n in shapes.most_common(int(sys.argv[1]) if len(sys.argv) > 1 else 8):
    print(f"--- {n} shaders, e.g. {examples[key].split(chr(92))[-1]}")
    print(key)
print("distinct position chains:", len(shapes))
