// Crash record (handover s12): minidump plus the list of active hooks.
#include <windows.h>
#include <dbghelp.h>

#include "hooks.h"
#include "log.h"

static LONG WINAPI OnCrash(EXCEPTION_POINTERS* ep)
{
    Log("CRASH code 0x%08lx at %p", ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress);
    const char* hookName;
    unsigned hookAddress;
    if (HooksDescribeAddress(ep->ExceptionRecord->ExceptionAddress, &hookName, &hookAddress))
        Log("  in the trampoline (original first instructions) of 0x%08x %s", hookAddress, hookName);
    const CONTEXT* c = ep->ContextRecord;
    Log("  regs eax=%08lx ecx=%08lx edx=%08lx ebx=%08lx esi=%08lx edi=%08lx ebp=%08lx esp=%08lx", c->Eax, c->Ecx,
        c->Edx, c->Ebx, c->Esi, c->Edi, c->Ebp, c->Esp);
    // Likely return addresses: stack dwords that point just after a call instruction (game or hook DLL code).
    HMODULE self = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCSTR>(&OnCrash), &self);
    const DWORD* sp = reinterpret_cast<const DWORD*>(c->Esp);
    int shown = 0;
    for (int k = 0; k < 2048 && shown < 24; k++)
    {
        DWORD v;
        __try { v = sp[k]; } __except (EXCEPTION_EXECUTE_HANDLER) { break; }
        HMODULE m = nullptr;
        bool game = v >= 0x00401000 && v < 0x00538000;
        if (!game && !(GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                              GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                          reinterpret_cast<LPCSTR>(static_cast<uintptr_t>(v)), &m) && m == self))
            continue;
        const BYTE* p = reinterpret_cast<const BYTE*>(static_cast<uintptr_t>(v));
        bool afterCall = false;
        __try { afterCall = p[-5] == 0xe8 || (p[-6] == 0xff && (p[-5] & 0x38) == 0x10) || (p[-2] == 0xff && (p[-1] & 0x38) == 0x10) || (p[-3] == 0xff && (p[-2] & 0x38) == 0x10); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
        if (!afterCall)
            continue;
        if (game)
            Log("  stack +%03x: %08lx (game)", k * 4, v);
        else
            Log("  stack +%03x: %08lx (hook dll +%lx)", k * 4, v, v - static_cast<DWORD>(reinterpret_cast<uintptr_t>(self)));
        shown++;
    }
    HooksLogActive();

    HANDLE f = CreateFileA("and-crash.dmp", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE)
    {
        MINIDUMP_EXCEPTION_INFORMATION info{GetCurrentThreadId(), ep, FALSE};
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), f,
                          static_cast<MINIDUMP_TYPE>(MiniDumpWithDataSegs | MiniDumpWithIndirectlyReferencedMemory),
                          &info, nullptr, nullptr);
        CloseHandle(f);
        Log("wrote and-crash.dmp");
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

// First-chance fatal exceptions, logged before anyone else's handler runs (others may replace the unhandled filter):
// access violations, stack overflow, stack-buffer overruns, illegal instructions. At most 8 are logged, with the
// same register and stack report as a crash; the exception then continues to its normal handlers.
static LONG WINAPI OnFirstChance(EXCEPTION_POINTERS* ep)
{
    static LONG logged;
    const DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_STACK_OVERFLOW && code != 0xC0000409 &&
        code != EXCEPTION_ILLEGAL_INSTRUCTION && code != EXCEPTION_INT_DIVIDE_BY_ZERO)
        return EXCEPTION_CONTINUE_SEARCH;
    if (InterlockedIncrement(&logged) > 8)
        return EXCEPTION_CONTINUE_SEARCH;
    Log("first-chance exception (may be handled):");
    OnCrash(ep);
    return EXCEPTION_CONTINUE_SEARCH;
}

void CrashInstall()
{
    SetUnhandledExceptionFilter(OnCrash);
    AddVectoredExceptionHandler(1, OnFirstChance);
}
