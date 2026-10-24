"""Disassemble Direct3D shader model 1.x / 2.0 bytecode (the game's pixel and vertex shaders, dumped by the hook
with [test] dump_shaders=1 into work/shaders/) into readable assembly, for porting materials to the remaster engine.

Usage:
  python tools/ps_disasm.py work/shaders/ps_98a72838035bad82.bin [more.bin ...]
"""
import struct
import sys

OPS = {0: "nop", 1: "mov", 2: "add", 3: "sub", 4: "mad", 5: "mul", 6: "rcp", 7: "rsq", 8: "dp3", 9: "dp4", 10: "min",
       11: "max", 12: "slt", 13: "sge", 14: "exp", 15: "log", 16: "lit", 17: "dst", 18: "lrp", 19: "frc", 20: "m4x4",
       21: "m4x3", 22: "m3x4", 23: "m3x3", 24: "m3x2", 31: "dcl", 64: "texcoord", 65: "texkill", 66: "tex", 67: "texbem",
       68: "texbeml", 69: "texreg2ar", 70: "texreg2gb", 71: "texm3x2pad", 72: "texm3x2tex", 73: "texm3x3pad",
       74: "texm3x3tex", 75: "texm3x3diff", 76: "texm3x3spec", 77: "texm3x3vspec", 78: "expp", 79: "logp", 80: "cnd",
       81: "def", 82: "texreg2rgb", 83: "texdp3tex", 84: "texm3x2depth", 85: "texdp3", 86: "texm3x3", 87: "texdepth",
       88: "cmp", 89: "bem", 90: "dp2add", 0xFFFD: "phase"}
# operand counts (destination first) for the ps 1.x / vs 1.1 instructions
ARGS = {0: 0, 1: 2, 2: 3, 3: 3, 4: 4, 5: 3, 6: 2, 7: 2, 8: 3, 9: 3, 10: 3, 11: 3, 12: 3, 13: 3, 14: 2, 15: 2, 16: 2,
        17: 3, 18: 4, 19: 2, 20: 3, 21: 3, 22: 3, 23: 3, 24: 3, 31: 2, 64: 1, 65: 1, 66: 1, 67: 2, 68: 2, 69: 2, 70: 2,
        71: 2, 72: 2, 73: 2, 74: 2, 75: 2, 76: 3, 77: 2, 78: 2, 79: 2, 80: 4, 81: 5, 82: 2, 83: 2, 84: 2, 85: 2, 86: 2,
        87: 1, 88: 4, 89: 3, 90: 4, 0xFFFD: 0}
PS_REG = {0: "r", 1: "v", 2: "c", 3: "t", 4: "rast", 5: "attr", 6: "tc", 7: "oC", 8: "oDepth", 9: "s"}
VS_REG = {0: "r", 1: "v", 2: "c", 3: "a", 4: "oPos", 5: "oD", 6: "oT", 7: "o", 8: "oDepth", 9: "s"}
SRCMOD = {0: "{}", 1: "-{}", 2: "{}_bias", 3: "-{}_bias", 4: "{}_bx2", 5: "-{}_bx2", 6: "1-{}", 7: "{}_x2", 8: "-{}_x2",
          9: "{}_dz", 10: "{}_dw", 11: "abs({})", 12: "-abs({})", 13: "!{}"}
SHIFT = {0: "", 1: "_x2", 2: "_x4", 3: "_x8", 15: "_d2", 14: "_d4", 13: "_d8"}


def reg(tok, pixel):
    t = ((tok >> 28) & 7) | ((tok >> 8) & 0x18)
    name = (PS_REG if pixel else VS_REG).get(t, f"reg{t}")
    return f"{name}{tok & 0x7FF}"


def dst(tok, pixel):
    mask = (tok >> 16) & 0xF
    m = "" if mask == 0xF else "." + "".join(c for i, c in enumerate("xyzw") if mask & (1 << i))
    mod = (tok >> 20) & 0xF
    shift = (tok >> 24) & 0xF
    sat = "_sat" if mod & 1 else ""
    return reg(tok, pixel) + m, SHIFT.get(shift, f"_shift{shift}") + sat


def src(tok, pixel):
    sw = (tok >> 16) & 0xFF
    comps = "".join("xyzw"[(sw >> (2 * i)) & 3] for i in range(4))
    s = "" if comps == "xyzw" else "." + (comps[0] if len(set(comps)) == 1 else comps)
    return SRCMOD.get((tok >> 24) & 0xF, "{}?").format(reg(tok, pixel) + s)


def disasm(data):
    words = struct.unpack(f"<{len(data) // 4}I", data[: len(data) // 4 * 4])
    ver = words[0]
    pixel = (ver >> 16) == 0xFFFF
    out = [f"{'ps' if pixel else 'vs'}_{(ver >> 8) & 0xFF}_{ver & 0xFF}"]
    i = 1
    while i < len(words):
        tok = words[i]
        op = tok & 0xFFFF
        if op == 0xFFFF:
            break
        if op == 0xFFFE:  # comment
            i += 1 + ((tok >> 16) & 0x7FFF)
            continue
        size = (tok >> 24) & 0xF  # ps 2.0+ instruction length; 0 in 1.x
        n = size if size else ARGS.get(op, 0)
        args = words[i + 1:i + 1 + n]
        name = OPS.get(op, f"op{op}")
        co = "+" if tok & 0x40000000 else ""
        if op == 81:  # def c, f, f, f, f
            vals = struct.unpack("<4f", struct.pack("<4I", *args[1:5]))
            out.append(f"def {reg(args[0], pixel)}, " + ", ".join(f"{v:g}" for v in vals))
        elif n:
            d, mod = dst(args[0], pixel)
            ss = [src(a, pixel) for a in args[1:]]
            out.append(f"{co}{name}{mod} {d}" + ("" if not ss else ", " + ", ".join(ss)))
        else:
            out.append(f"{co}{name}")
        i += 1 + n
    return "\n".join(out)


if __name__ == "__main__":
    for p in sys.argv[1:]:
        print(f"; {p}")
        print(disasm(open(p, "rb").read()))
        print()
