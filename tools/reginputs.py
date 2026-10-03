"""Find a function's register inputs: registers read on some path before being written.

Link-time code generation lets functions take arguments in any register (see docs/patterns.md). Every
recovered function's callees must be checked with this before writing a shim, and every recovered function's
own entry must be checked too.

Usage: python tools/reginputs.py 0x0040f8c0 [0x... ...] [--depth 2]

Walks all paths of the function's control flow (both sides of every branch), follows direct calls up to
--depth levels, and treats push/pop pairs of callee-saved registers as saves. Reports, per register, the first
instruction that reads it while still holding the caller's value. Flags are ignored; the stack pointer is
assumed. Indirect jumps end a path (reported).
"""
import argparse
import os
import sys

import capstone
from capstone import x86
import pefile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, "orig", "pc", "GameData", "game.unpacked.exe")
BASE = 0x400000
REGS = {
    "eax": {"eax", "ax", "al", "ah"}, "ecx": {"ecx", "cx", "cl", "ch"}, "edx": {"edx", "dx", "dl", "dh"},
    "ebx": {"ebx", "bx", "bl", "bh"}, "esi": {"esi", "si"}, "edi": {"edi", "di"}, "ebp": {"ebp", "bp"},
}
FULL_WRITE = {"eax", "ecx", "edx", "ebx", "esi", "edi", "ebp"}

pe = pefile.PE(EXE, fast_load=True)
IMG = pe.get_memory_mapped_image()
MD = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
MD.detail = True
_cache = {}


def insn_at(addr):
    return next(MD.disasm(IMG[addr - BASE:addr - BASE + 16], addr))


def family(name):
    for r, names in REGS.items():
        if name in names:
            return r
    return None


def is_self_clear(i):
    ops = i.op_str.split(", ")
    return i.mnemonic in ("xor", "sub") and len(ops) == 2 and ops[0] == ops[1]


def inputs(func, depth):
    """Map register -> description of first read-before-write, over all paths."""
    key = (func, depth)
    if key in _cache:
        return _cache[key]
    _cache[key] = {}  # recursion guard
    found = {}
    # state: (address, frozenset of registers already written on this path)
    work = [(func, frozenset())]
    seen = set()
    steps = 0
    while work and steps < 20000:
        addr, written = work.pop()
        if (addr, written) in seen:
            continue
        seen.add((addr, written))
        while steps < 20000:
            steps += 1
            try:
                i = insn_at(addr)
            except StopIteration:
                break
            reads, writes = i.regs_access()
            rd = {family(i.reg_name(r)) for r in reads} - {None}
            wr = {family(i.reg_name(r)) for r in writes} - {None}
            if i.mnemonic == "push" and i.operands[0].type == x86.X86_OP_REG:
                rd = set()  # saving a register is not a use
            if is_self_clear(i):
                rd = set()
            for r in rd - set(written):
                found.setdefault(r, f"{i.address:08x}: {i.mnemonic} {i.op_str}")
            if i.mnemonic == "call":
                op = i.operands[0]
                if op.type == x86.X86_OP_IMM and depth > 0:
                    for r, where in inputs(op.imm, depth - 1).items():
                        if r not in written:
                            found.setdefault(r, f"{i.address:08x}: call 0x{op.imm:08x} -> {where}")
                # after a call, eax/ecx/edx hold callee results or garbage: treat as written
                written = written | {"eax", "ecx", "edx"}
            # partial writes (al, cl) do not define the whole register
            full = {r for r in wr if any(i.reg_name(x) in (r,) for x in writes)}
            if i.mnemonic == "pop":
                full = wr
            written = written | (full & FULL_WRITE)
            if i.mnemonic == "ret":
                break
            if i.mnemonic == "jmp":
                op = i.operands[0]
                if op.type == x86.X86_OP_IMM:
                    addr = op.imm
                    continue
                found.setdefault("(indirect jump)", f"{i.address:08x}: {i.op_str}")
                break
            if i.group(capstone.CS_GRP_JUMP):  # conditional
                work.append((i.operands[0].imm, written))
            addr = i.address + i.size
    _cache[key] = found
    return found


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("functions", nargs="+")
    ap.add_argument("--depth", type=int, default=2)
    a = ap.parse_args()
    for f in a.functions:
        res = inputs(int(f, 16), a.depth)
        res = {k: v for k, v in res.items() if k != "esp"}
        print(f"{int(f, 16):08x}: " + ("no register inputs" if not res else ""))
        for r, where in sorted(res.items()):
            print(f"    {r:5} first read at {where}")


if __name__ == "__main__":
    sys.exit(main())
