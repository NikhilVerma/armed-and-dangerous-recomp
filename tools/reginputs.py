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
CALLEE_SAVED = {"ebx", "esi", "edi", "ebp"}

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
    """xor/sub r, r define r without reading it; mov r, r and lea r, [r] are padding no-ops (MSVC aligns loops
    with them), not reads."""
    ops = i.op_str.split(", ")
    if i.mnemonic in ("xor", "sub", "mov") and len(ops) == 2 and ops[0] == ops[1]:
        return True
    return i.mnemonic == "lea" and len(ops) == 2 and ops[1] == f"[{ops[0]}]"


_clobber_cache = {}


def jump_table_targets(i, func):
    """Targets of a switch `jmp dword ptr [reg*4 + table]`: consecutive table entries that point into the code
    just around the function. Returns None when the jump is not a recognisable table."""
    op = i.operands[0]
    if op.type != x86.X86_OP_MEM or op.mem.base != 0 or op.mem.index == 0 or op.mem.scale != 4:
        return None
    table = op.mem.disp & 0xffffffff
    targets = []
    for k in range(256):
        o = table - BASE + 4 * k
        if not 0 <= o < len(IMG) - 4:
            break
        t = int.from_bytes(IMG[o:o + 4], "little")
        if not func <= t < i.address + 0x2000:
            break
        targets.append(t)
    return targets or None


def clobbers(func, depth=3):
    """Registers a function may leave changed: written anywhere on any path and not restored by a matching
    push/pop save. Callees are included up to `depth`; indirect calls count as eax/ecx/edx."""
    if func in _clobber_cache:
        return _clobber_cache[func]
    _clobber_cache[func] = {"eax", "ecx", "edx"}  # recursion guard: conservative
    written, saved = set(), set()
    work, seen, steps = [func], set(), 0
    while work and steps < 20000:
        addr = work.pop()
        while addr not in seen and steps < 20000:
            seen.add(addr)
            steps += 1
            try:
                i = insn_at(addr)
            except StopIteration:
                break
            if i.mnemonic == "push" and i.operands[0].type == x86.X86_OP_REG:
                r = family(i.reg_name(i.operands[0].reg))
                if r in CALLEE_SAVED and r not in written:
                    saved.add(r)
            else:
                _, wr = i.regs_access()
                written |= {family(i.reg_name(x)) for x in wr} - {None}
            if i.mnemonic == "call":
                op = i.operands[0]
                written |= (clobbers(op.imm, depth - 1) if op.type == x86.X86_OP_IMM and depth > 0
                            else {"eax", "ecx", "edx"})
            if i.mnemonic == "ret":
                break
            if i.mnemonic == "jmp":
                op = i.operands[0]
                if op.type == x86.X86_OP_IMM:
                    addr = op.imm
                    continue
                targets = jump_table_targets(i, func)
                if targets:
                    work.extend(targets)  # a switch stays inside the function
                else:
                    written |= {"eax", "ecx", "edx"}
                break
            if i.group(capstone.CS_GRP_JUMP):
                work.append(i.operands[0].imm)
            addr = i.address + i.size
    result = (written - saved) & FULL_WRITE
    _clobber_cache[func] = result
    return result


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
            if (i.mnemonic == "push" and i.operands[0].type == x86.X86_OP_REG
                    and family(i.reg_name(i.operands[0].reg)) in CALLEE_SAVED):
                rd = set()  # saving a callee-saved register is not a use (pushing eax/ecx/edx passes a value)
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
                # A direct callee overwrites only what it writes (link-time code generation lets callers rely
                # on that: 0x0040f8c0 reads EDX across a call to 0x0044e600). Unknown callees: eax/ecx/edx.
                if op.type == x86.X86_OP_IMM:
                    written = written | clobbers(op.imm)
                else:
                    written = written | {"eax", "ecx", "edx"}
            # Any write, including a partial one (mov cx, ...), counts as defining the register: reading the
            # untouched upper bits of a caller value is not a real calling convention.
            written = written | (wr & FULL_WRITE)
            if i.mnemonic == "ret":
                break
            if i.mnemonic == "jmp":
                op = i.operands[0]
                if op.type == x86.X86_OP_IMM:
                    addr = op.imm
                    continue
                targets = jump_table_targets(i, func)
                if targets:
                    work.extend((t, written) for t in targets)
                else:
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
