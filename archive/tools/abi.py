"""Derive every function's calling convention from the binary and generate the C++ calling layer.

The game was built with link-time code generation: functions take arguments in arbitrary registers, may
clobber callee-saved registers, and may return values in EDX or ST0 (docs/patterns.md). This tool records the
real interface of each function and generates:

  symbols/abi.csv            per function: register inputs, stack args, cleanup, returns, clobbers, confidence
  archive/src/gen/orig_calls.h       orig::fn_XXXXXXXX(...): call an original function from C++ (any convention)
  archive/src/gen/entry_stubs.h      ABI_ENTRY_XXXXXXXX(stub, impl): naked stub so original code can call a recovered
                             C function `impl` (cdecl, parameters: register inputs then stack args)

Usage: python archive/tools/abi.py [--trace work/traces/session1]   (runtime trace optional: confirms register inputs)
       python archive/tools/abi.py --show 0x0040f8c0                (print one function's ABI)

Rules
- Register inputs: archive/tools/reginputs.py full-path scan (depth 1), excluding esp. A runtime trace marks each as
  confirmed (pointer-like or consistent values) or unconfirmed. EBP inputs are not supported by the generated
  wrappers (marked 'unsupported').
- Stack args: `ret N` -> callee pops N/4. Plain `ret` -> caller pops; count = most common `add esp, K` right
  after direct call sites (0 when none).
- Returns: callers read EAX/EDX/ST0 after the call before writing it (EAX assumed when nothing is observed).
- Wrappers save and restore EBX, ESI, EDI and EBP around every call, because LTCG callees may clobber them.
- Entry stubs preserve every register except the returned ones, because LTCG callers may rely on the original
  callee leaving them untouched.
"""
import argparse
import bisect
import collections
import csv
import json
import os
import sys

import capstone
from capstone import x86

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # the repository (this tool lives in archive/tools)
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(1, os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), "tools"))  # compare.py, instances.py (shared with the oracle tools)
import reginputs  # noqa: E402

ORDER = ["eax", "ecx", "edx", "ebx", "esi", "edi", "ebp"]
FPU_READS_ST0 = {"fstp", "fst", "fcomp", "fcom", "fcompp", "fucomp", "fucom", "fucompp", "fistp", "fist", "fxch",
                 "fadd", "faddp", "fsub", "fsubp", "fsubr", "fsubrp", "fmul", "fmulp", "fdiv", "fdivp", "fdivr",
                 "fdivrp", "fchs", "fabs", "fsqrt", "ftst", "fcomi", "fcomip", "fucomi", "fucomip", "frndint"}


def load_functions():
    funcs = json.load(open(os.path.join(ROOT, "work", "exports", "functions.json")))
    return {int(f["address"], 16): f for f in funcs}


def call_sites(funcs):
    """callee -> list of (site address, address after the call)."""
    md = reginputs.MD
    sites = collections.defaultdict(list)
    for a, f in funcs.items():
        if f["thunk"]:
            continue
        body = reginputs.IMG[a - reginputs.BASE:a - reginputs.BASE + f["size"]]
        for i in md.disasm(body, a):
            if i.mnemonic == "call" and i.operands and i.operands[0].type == x86.X86_OP_IMM:
                sites[i.operands[0].imm].append((i.address, i.address + i.size))
    return sites


def after_call(addr):
    """What the caller does right after a call: returns (reads eax?, reads edx?, uses st0?, add esp amount)."""
    reads = {"eax": None, "edx": None}
    st0 = False
    add_esp = None
    for step in range(12):
        try:
            i = reginputs.insn_at(addr)
        except StopIteration:
            break
        if i.mnemonic == "add" and i.op_str.startswith("esp, ") and add_esp is None and step <= 1:
            try:
                k = int(i.op_str.split(", ")[1], 0)
                if 0 < k <= 0x40 and k % 4 == 0:  # argument cleanup, not a frame teardown
                    add_esp = k
            except ValueError:
                pass
        if i.mnemonic in FPU_READS_ST0 and not st0:
            st0 = True
        r, w = i.regs_access()
        rn = {reginputs.family(i.reg_name(x)) for x in r}
        wn = {reginputs.family(i.reg_name(x)) for x in w}
        for reg in ("eax", "edx"):
            if reads[reg] is None:
                if reg in rn and not reginputs.is_self_clear(i):
                    reads[reg] = True
                elif reg in wn:
                    reads[reg] = False
        if i.mnemonic in ("call", "ret", "jmp") or i.group(capstone.CS_GRP_JUMP):
            break
        addr = i.address + i.size
    return bool(reads["eax"]), bool(reads["edx"]), st0, add_esp


def ret_pop(a, size, depth=3):
    """Bytes the function pops on return. Follows its own control flow; a function without a `ret` that
    ends in a jump to another function (tail call) returns through that function's `ret`. "noreturn" when no
    path returns (fatal errors), None when unknown."""
    body = reginputs.function_body(a)
    for i in body:
        if i.mnemonic == "ret":
            return int(i.op_str, 16) if i.op_str else 0
    if depth > 0:
        for i in body:
            if i.mnemonic == "jmp" and i.operands[0].type == reginputs.x86.X86_OP_IMM:
                t = i.operands[0].imm
                if not a <= t < a + max(size, 1):
                    pop = ret_pop(t, 0, depth - 1)
                    if pop is not None:
                        return pop
    if not any(i.mnemonic == "jmp" and i.operands[0].type != reginputs.x86.X86_OP_IMM for i in body):
        return "noreturn"
    return None


X87_LOADS = ("fld", "fild", "fldz", "fld1", "fldpi", "fldl2e", "fldl2t", "fldlg2", "fldln2")


def leaves_st0(a):
    """Callee-side evidence for a float return: on some path the last x87 instructions before a `ret` load a
    value (fld/fild, or arithmetic on it) without storing it back with a popping store. Call-site votes alone
    mistake callers that start x87 work right after the call for float returns (0x00403690)."""
    body = reginputs.function_body(a)
    for k, i in enumerate(body):
        if i.mnemonic != "ret":
            continue
        for j in range(k - 1, max(k - 8, -1), -1):
            m = body[j].mnemonic
            if body[j].address >= i.address or m in ("ret", "jmp", "call"):
                break
            if not m.startswith("f"):
                continue
            if m.endswith("p") or m in ("fistp", "fstp", "fcompp", "fucompp"):
                break  # the last x87 instruction pops: nothing left in st0
            if m in X87_LOADS or m in ("fmul", "fadd", "fsub", "fdiv", "fsubr", "fdivr", "fchs", "fabs", "fsqrt",
                                      "fsin", "fcos", "fxch"):
                return True
            break
    return False


def runtime_confirmation(trace):
    """address -> set of confirmed registers, from archive/tools/analyse_trace.py output."""
    path = os.path.join(trace, "analysis", "abi.csv")
    if not os.path.exists(path):
        return {}
    out = {}
    for r in csv.DictReader(open(path)):
        regs = {part.split(":")[0].strip() for part in r["register_inputs"].split(";") if part.strip()}
        out[int(r["address"], 16)] = regs
    return out


def analyse(funcs, sites, confirm):
    rows = {}
    for a, f in sorted(funcs.items()):
        if f["thunk"]:
            continue
        inputs = [r for r in ORDER if r in reginputs.inputs(a, 1)]
        clob = sorted(reginputs.clobbers(a) & set(ORDER), key=ORDER.index)
        pop = ret_pop(a, f["size"])
        uses = [after_call(nxt) for _, nxt in sites.get(a, [])]
        # A register is a return value only if the original writes it; otherwise LTCG callers may rely on it
        # surviving the call, and entry stubs must preserve it.
        ret = []
        if "eax" in clob:
            ret.append("eax")
        if "edx" in clob and any(u[1] for u in uses):
            ret.append("edx")
        # Virtual methods have no direct call sites to vote: the callee's own code decides.
        if (not uses or sum(u[2] for u in uses) * 2 >= len(uses)) and leaves_st0(a):
            ret = ["st0"]
        if not ret:
            ret = ["none"]
        if pop and pop != "noreturn":
            cleanup, nargs = "callee", pop // 4
        else:
            adds = collections.Counter(u[3] for u in uses if u[3])
            cleanup, nargs = "caller", (adds.most_common(1)[0][0] // 4 if adds else 0)
            # Callers that merge the cleanup of several calls into one `add esp` hide arguments; the callee's own
            # stack reads (tracked depth) give a lower bound (zlib 0x00501e00 reads 3 that no `add esp` shows).
            nargs = max(nargs, min(reginputs.stack_arg_count(a), 16))
        notes = []
        if pop is None:
            notes.append("no ret found (unknown return)")
        info = "noreturn" if pop == "noreturn" else ""
        conf = confirm.get(a)
        unconfirmed = [r for r in inputs if conf is not None and r not in conf]
        rows[a] = {"address": f"0x{a:08x}", "inputs": " ".join(inputs), "stack_args": nargs, "cleanup": cleanup,
                   "returns": " ".join(ret), "clobbers": " ".join(clob), "call_sites": len(sites.get(a, [])),
                   "runtime": "" if conf is None else ("confirmed" if not unconfirmed else
                                                       "unconfirmed: " + " ".join(unconfirmed)),
                   "notes": "; ".join(notes) or info}
    return rows


# --- code generation ---------------------------------------------------------------------------------------------

def wrapper(row):
    a = int(row["address"], 16)
    inputs = row["inputs"].split()
    if "no ret" in row["notes"]:
        return None
    n = int(row["stack_args"])
    params = [f"uint32_t {r}_" for r in inputs] + [f"uint32_t a{k}" for k in range(n)]
    st0 = row["returns"] == "st0"
    edx = "edx" in row["returns"].split()
    rtype = "float" if st0 else ("uint64_t" if edx else "uint32_t")
    lines = [f"// 0x{a:08x}: inputs [{row['inputs']}] stack {n} ({row['cleanup']} pops) returns {row['returns']}"
             f" clobbers [{row['clobbers']}]{' runtime ' + row['runtime'] if row['runtime'] else ''}",
             f"inline {rtype} fn_{a:08x}({', '.join(params)})", "{",
             f"    static const uint32_t target = 0x{a:08x};"]
    if st0:
        lines.append("    float r;")
    else:
        lines.append("    uint32_t lo, hi;")
    asm = ["push ebx", "push esi", "push edi", "push ebp"]
    for k in reversed(range(n)):
        asm.append(f"push a{k}")
    # load register inputs; the operands are addressed through ebp, so an ebp input is loaded last (ORDER)
    for r in inputs:
        asm.append(f"mov {r}, {r}_")
    asm.append("call dword ptr [target]")
    if row["cleanup"] == "caller" and n:
        asm.append(f"add esp, {4 * n}")
    asm += ["pop ebp", "pop edi", "pop esi", "pop ebx"]
    if st0:
        asm.append("fstp r")
    else:
        asm += ["mov lo, eax", "mov hi, edx"]
    lines.append("    __asm {")
    lines += [f"        {x}" for x in asm]
    lines.append("    }")
    if st0:
        lines.append("    return r;")
    elif edx:
        lines.append("    return (static_cast<uint64_t>(hi) << 32) | lo;")
    else:
        lines.append("    (void)hi;")
        lines.append("    return lo;")
    lines.append("}")
    return "\n".join(lines)


def entry_stub(row):
    """Macro defining a naked stub `stub` that adapts the original ABI to `uint32_t impl(regs..., args...)`."""
    a = int(row["address"], 16)
    inputs = row["inputs"].split()
    if "no ret" in row["notes"]:
        return None
    n = int(row["stack_args"])
    returns = row["returns"].split()
    preserve = [r for r in ["eax", "ecx", "edx", "ebx", "esi", "edi", "ebp"] if r not in returns]
    asm = [f"push {r}" for r in preserve]
    depth = 4 * len(preserve)
    # stack args of the original call: [esp + depth + 4 + 4k]; push in reverse so impl sees a0 first
    for k in reversed(range(n)):
        off = depth + 4 + 4 * k + 4 * (n - 1 - k)
        asm.append(f"push dword ptr [esp + {off}]")
    for r in reversed(inputs):
        asm.append(f"push {r}")
    asm.append("call impl")
    asm.append(f"add esp, {4 * (n + len(inputs))}")
    asm += [f"pop {r}" for r in reversed(preserve)]
    asm.append(f"ret {4 * n}" if row["cleanup"] == "callee" and n else "ret")
    body = " ".join(f"__asm {x}" for x in asm)
    sig = ", ".join([f"{r}" for r in inputs] + [f"a{k}" for k in range(n)]) or "void"
    return (f"// 0x{a:08x}: impl(cdecl) parameters: {sig}; returns {row['returns']}\n"
            f"#define ABI_ENTRY_{a:08x}(stub, impl) static __declspec(naked) void stub() {{ {body} }}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--trace", default=os.path.join(ROOT, "work", "traces", "session1"))
    ap.add_argument("--show")
    args = ap.parse_args()

    funcs = load_functions()
    if args.show:
        a = int(args.show, 16)
        sites = call_sites(funcs)
        row = analyse({a: funcs[a]}, sites, runtime_confirmation(args.trace))[a]
        for k, v in row.items():
            print(f"{k:12} {v}")
        print(wrapper(row) or "(no wrapper)")
        print(entry_stub(row) or "(no stub)")
        return

    sites = call_sites(funcs)
    rows = analyse(funcs, sites, runtime_confirmation(args.trace))
    out = os.path.join(ROOT, "symbols", "abi.csv")
    with open(out, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=list(next(iter(rows.values())).keys()), lineterminator="\n")
        w.writeheader()
        w.writerows(rows.values())

    gen = os.path.join(ROOT, "archive", "src", "gen")
    os.makedirs(gen, exist_ok=True)
    wrappers = [w for w in (wrapper(r) for r in rows.values()) if w]
    with open(os.path.join(gen, "orig_calls.h"), "w", encoding="utf-8", newline="\n") as f:
        f.write("#pragma once\n// GENERATED by archive/tools/abi.py from symbols/abi.csv. Do not edit.\n"
                "// orig::fn_XXXXXXXX calls an original function with its real (LTCG) convention.\n"
                "// Parameters: register inputs (named reg_) then stack arguments a0..; floats pass as bits.\n"
                "#include <cstdint>\n#include <cstring>\n\n// EBP is saved and restored around each call (LTCG callees may clobber it).\n#pragma warning(disable : 4731)\n\nnamespace orig\n{\n"
                "inline uint32_t f2u(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }\n\n")
        f.write("\n\n".join(wrappers))
        f.write("\n}  // namespace orig\n")
    stubs = [s for s in (entry_stub(r) for r in rows.values()) if s]
    with open(os.path.join(gen, "entry_stubs.h"), "w", encoding="utf-8", newline="\n") as f:
        f.write("#pragma once\n// GENERATED by archive/tools/abi.py from symbols/abi.csv. Do not edit.\n"
                "// ABI_ENTRY_XXXXXXXX(stub, impl) defines a naked function `stub` to pass to HOOK(): it takes the\n"
                "// original's registers and stack arguments and calls cdecl `impl(register inputs..., a0, ...)`.\n\n")
        f.write("\n".join(stubs))
        f.write("\n")
    stats = collections.Counter()
    for r in rows.values():
        stats["functions"] += 1
        stats["with register inputs"] += bool(r["inputs"])
        stats["returns st0"] += r["returns"] == "st0"
        stats["returns edx"] += "edx" in r["returns"]
        stats["unsupported"] += r["notes"].startswith("no ret")
        stats["noreturn"] += r["notes"] == "noreturn"
        stats["runtime confirmed"] += r["runtime"] == "confirmed"
        stats["runtime unconfirmed"] += r["runtime"].startswith("unconfirmed")
    print(dict(stats))
    print(f"{len(wrappers)} wrappers, {len(stubs)} entry stubs -> archive/src/gen/")


if __name__ == "__main__":
    main()
