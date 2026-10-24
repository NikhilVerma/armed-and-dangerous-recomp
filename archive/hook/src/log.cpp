#include "log.h"

#include <windows.h>
#include <cstdarg>
#include <cstdio>

static FILE* g_log;
static CRITICAL_SECTION g_logLock;

void LogOpen()
{
    InitializeCriticalSection(&g_logLock);
    g_log = fopen("and.log", "w");
}

void Log(const char* fmt, ...)
{
    if (!g_log)
        return;
    EnterCriticalSection(&g_logLock);
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(g_log, "%02d:%02d:%02d.%03d [%5lu] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds,
            GetCurrentThreadId());
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
    LeaveCriticalSection(&g_logLock);
}
