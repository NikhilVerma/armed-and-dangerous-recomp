// Crash record (handover s12): minidump plus the list of active hooks.
#include <windows.h>
#include <dbghelp.h>

#include "hooks.h"
#include "log.h"

static LONG WINAPI OnCrash(EXCEPTION_POINTERS* ep)
{
    Log("CRASH code 0x%08lx at %p", ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress);
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

void CrashInstall()
{
    SetUnhandledExceptionFilter(OnCrash);
}
