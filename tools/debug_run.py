"""Runs the game (the test instance, with its current and.ini) under a minimal Win32 debugger and reports the first
fatal exception, including fast-fail (0xC0000409), which no in-process handler sees: code, address, module+offset,
registers and the raw stack words that point into our DLL. Use with the and.ini a visual_capture / compare run wrote.

Usage: .venv/Scripts/python.exe tools/debug_run.py [--timeout 300]
"""
import argparse
import ctypes
import ctypes.wintypes as wt
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import instances  # noqa: E402

k32 = ctypes.WinDLL("kernel32", use_last_error=True)
DEBUG_ONLY_THIS_PROCESS = 0x2
EXCEPTION_DEBUG_EVENT, CREATE_PROCESS_DEBUG_EVENT, EXIT_PROCESS_DEBUG_EVENT, LOAD_DLL_DEBUG_EVENT = 1, 3, 5, 6
DBG_CONTINUE, DBG_EXCEPTION_NOT_HANDLED = 0x00010002, 0x80010001
FATAL = {0xC0000005, 0xC0000409, 0xC00000FD, 0xC000001D, 0xC0000094, 0xC0000374}


class EXCEPTION_RECORD(ctypes.Structure):
    _fields_ = [("ExceptionCode", wt.DWORD), ("ExceptionFlags", wt.DWORD), ("ExceptionRecord", ctypes.c_void_p),
                ("ExceptionAddress", ctypes.c_void_p), ("NumberParameters", wt.DWORD),
                ("ExceptionInformation", ctypes.c_size_t * 15)]


class DEBUG_EVENT(ctypes.Structure):
    class _U(ctypes.Union):
        class _Exc(ctypes.Structure):
            _fields_ = [("ExceptionRecord", EXCEPTION_RECORD), ("dwFirstChance", wt.DWORD)]

        class _Dll(ctypes.Structure):
            _fields_ = [("hFile", wt.HANDLE), ("lpBaseOfDll", ctypes.c_void_p), ("dwDebugInfoFileOffset", wt.DWORD),
                        ("nDebugInfoSize", wt.DWORD), ("lpImageName", ctypes.c_void_p), ("fUnicode", wt.WORD)]

        class _Exit(ctypes.Structure):
            _fields_ = [("dwExitCode", wt.DWORD)]

        _fields_ = [("Exception", _Exc), ("LoadDll", _Dll), ("ExitProcess", _Exit), ("raw", ctypes.c_byte * 160)]

    _fields_ = [("dwDebugEventCode", wt.DWORD), ("dwProcessId", wt.DWORD), ("dwThreadId", wt.DWORD), ("u", _U)]


class STARTUPINFO(ctypes.Structure):
    _fields_ = [("cb", wt.DWORD), ("lpReserved", wt.LPWSTR), ("lpDesktop", wt.LPWSTR), ("lpTitle", wt.LPWSTR),
                ("dwX", wt.DWORD), ("dwY", wt.DWORD), ("dwXSize", wt.DWORD), ("dwYSize", wt.DWORD),
                ("dwXCountChars", wt.DWORD), ("dwYCountChars", wt.DWORD), ("dwFillAttribute", wt.DWORD),
                ("dwFlags", wt.DWORD), ("wShowWindow", wt.WORD), ("cbReserved2", wt.WORD),
                ("lpReserved2", ctypes.c_void_p), ("hStdInput", wt.HANDLE), ("hStdOutput", wt.HANDLE),
                ("hStdError", wt.HANDLE)]


class PROCESS_INFORMATION(ctypes.Structure):
    _fields_ = [("hProcess", wt.HANDLE), ("hThread", wt.HANDLE), ("dwProcessId", wt.DWORD), ("dwThreadId", wt.DWORD)]


class WOW64_CONTEXT(ctypes.Structure):
    _fields_ = [("ContextFlags", wt.DWORD), ("Dr", wt.DWORD * 6), ("FloatSave", ctypes.c_byte * 112),
                ("SegGs", wt.DWORD), ("SegFs", wt.DWORD), ("SegEs", wt.DWORD), ("SegDs", wt.DWORD),
                ("Edi", wt.DWORD), ("Esi", wt.DWORD), ("Ebx", wt.DWORD), ("Edx", wt.DWORD), ("Ecx", wt.DWORD),
                ("Eax", wt.DWORD), ("Ebp", wt.DWORD), ("Eip", wt.DWORD), ("SegCs", wt.DWORD), ("EFlags", wt.DWORD),
                ("Esp", wt.DWORD), ("SegSs", wt.DWORD), ("Ext", ctypes.c_byte * 512)]


def read(hp, addr, n):
    buf = ctypes.create_string_buffer(n)
    got = ctypes.c_size_t()
    if not k32.ReadProcessMemory(hp, ctypes.c_void_p(addr), buf, n, ctypes.byref(got)):
        return b""
    return buf.raw[:got.value]


def module_name(hp, ptr, unicode):
    if not ptr:
        return "?"
    raw = read(hp, ptr, 4)
    if len(raw) < 4:
        return "?"
    p = int.from_bytes(raw, "little")
    if not p:
        return "?"
    data = read(hp, p, 520)
    s = data.decode("utf-16-le" if unicode else "latin1", errors="replace")
    return s.split("\x00")[0]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--timeout", type=int, default=300)
    a = ap.parse_args()
    gd = instances.instance_dir("main")
    exe = os.path.join(gd, "game.unpacked.exe")
    si = STARTUPINFO()
    si.cb = ctypes.sizeof(si)
    pi = PROCESS_INFORMATION()
    if not k32.CreateProcessW(exe, None, None, None, False, DEBUG_ONLY_THIS_PROCESS, None, gd, ctypes.byref(si),
                              ctypes.byref(pi)):
        raise SystemExit(f"CreateProcess failed: {ctypes.get_last_error()}")
    modules = {}
    ev = DEBUG_EVENT()
    end = time.time() + a.timeout
    while time.time() < end:
        if not k32.WaitForDebugEvent(ctypes.byref(ev), 1000):
            continue
        status = DBG_CONTINUE
        code = ev.dwDebugEventCode
        if code == LOAD_DLL_DEBUG_EVENT:
            d = ev.u.LoadDll
            modules[d.lpBaseOfDll or 0] = os.path.basename(module_name(pi.hProcess, d.lpImageName, d.fUnicode))
        elif code == EXCEPTION_DEBUG_EVENT:
            rec = ev.u.Exception.ExceptionRecord
            exc = rec.ExceptionCode & 0xFFFFFFFF
            if exc in FATAL:
                addr = rec.ExceptionAddress or 0
                base = max((b for b in modules if b <= addr), default=0)
                print(f"exception 0x{exc:08x} at 0x{addr:08x} = {modules.get(base, '?')}+0x{addr - base:x} "
                      f"(first chance {ev.u.Exception.dwFirstChance}), params "
                      f"{[hex(rec.ExceptionInformation[i]) for i in range(rec.NumberParameters)]}")
                th = k32.OpenThread(0x1FFFFF, False, ev.dwThreadId)
                ctx = WOW64_CONTEXT()
                ctx.ContextFlags = 0x10007  # WOW64_CONTEXT_FULL
                if k32.Wow64GetThreadContext(th, ctypes.byref(ctx)):
                    print(f"  eip {ctx.Eip:08x} esp {ctx.Esp:08x} ebp {ctx.Ebp:08x} eax {ctx.Eax:08x} "
                          f"ecx {ctx.Ecx:08x} edx {ctx.Edx:08x}")
                    stack = read(pi.hProcess, ctx.Esp, 4096)
                    shown = 0
                    for i in range(0, len(stack) - 3, 4):
                        v = int.from_bytes(stack[i:i + 4], "little")
                        b = max((x for x in modules if x <= v), default=0)
                        name = modules.get(b, "")
                        if name.lower() == "dinput8.dll" and v - b < 0x2000000 and shown < 40:
                            print(f"  stack +{i:03x}: {name}+0x{v - b:x}")
                            shown += 1
                if exc == 0xC0000409 or not ev.u.Exception.dwFirstChance:
                    k32.TerminateProcess(pi.hProcess, 1)
                    k32.ContinueDebugEvent(ev.dwProcessId, ev.dwThreadId, DBG_CONTINUE)
                    return
            status = DBG_EXCEPTION_NOT_HANDLED
        elif code == EXIT_PROCESS_DEBUG_EVENT:
            print(f"process exited, code 0x{ev.u.ExitProcess.dwExitCode:08x}")
            return
        k32.ContinueDebugEvent(ev.dwProcessId, ev.dwThreadId, status)
    print("timeout")
    k32.TerminateProcess(pi.hProcess, 1)


if __name__ == "__main__":
    main()
