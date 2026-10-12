// Sampling profiler for the game's main thread ([test] profile=1). A background thread suspends the thread that
// presents frames about 1000 times a second, copies its registers and stack top (nothing is analysed while it is suspended) and counts:
//   - self: the instruction pointer's function (game image: raw address, mapped to tracker names by
//     tools/profile_report.py; other modules: dbghelp symbol or module+offset)
//   - game: the innermost return address into the game image found on the stack (which game function the time is
//     spent under)
//   - hook (inclusive): every function of this DLL with a return address on the stack, once per sample (where the
//     time spent in memcpy, the driver or the system is charged to)
// Every 10 s and at exit it writes and-profile.txt (top entries). Off by default; costs about 1-2% when on.
#include <windows.h>
#include <dbghelp.h>
#include <mmsystem.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "config.h"
#include "log.h"

namespace
{
std::atomic<DWORD> g_mainThread{0};
std::map<DWORD64, uint32_t> g_writeCallers, g_regCallers;  // see the sampler
std::atomic<bool> g_window{true};  // inside [test] profile_window
std::atomic<bool> g_writeNow{false};  // the window just closed: write the profile
HANDLE g_thread;
std::mutex g_lock;
std::map<DWORD64, uint32_t> g_self, g_game, g_hookRet;  // g_hookRet: return addresses, folded per function on write
uintptr_t g_hookBegin, g_hookEnd;
uint32_t g_samples;
constexpr DWORD64 kGameBegin = 0x00400000, kGameEnd = 0x00700000;

bool InGame(DWORD64 a)
{
    return a >= kGameBegin && a < kGameEnd;
}

std::string Describe(HANDLE process, DWORD64 a)
{
    char buf[64];
    if (InGame(a))
    {
        snprintf(buf, sizeof(buf), "game %08llx", static_cast<unsigned long long>(a));
        return buf;
    }
    alignas(SYMBOL_INFO) char symbuf[sizeof(SYMBOL_INFO) + 256];
    auto* sym = reinterpret_cast<SYMBOL_INFO*>(symbuf);
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = 255;
    DWORD64 disp = 0;
    HMODULE mod = nullptr;
    char modName[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(static_cast<uintptr_t>(a)), &mod))
    {
        GetModuleFileNameA(mod, modName, MAX_PATH);
        char* slash = strrchr(modName, '\\');
        if (slash)
            memmove(modName, slash + 1, strlen(slash));
    }
    if (SymFromAddr(process, a, &disp, sym))
        return std::string(modName) + "!" + sym->Name;
    snprintf(buf, sizeof(buf), "+%llx", static_cast<unsigned long long>(a - reinterpret_cast<uintptr_t>(mod)));
    return std::string(modName) + buf;
}

void Write()
{
    std::lock_guard<std::mutex> g(g_lock);
    HANDLE process = GetCurrentProcess();
    FILE* f = fopen("and-profile.txt", "w");
    if (!f)
        return;
    auto dump = [&](const char* title, const std::map<DWORD64, uint32_t>& counts, bool collapse) {
        // collapse: merge addresses by described name (functions), else by address
        std::map<std::string, uint32_t> byName;
        for (auto& [a, n] : counts)
            byName[collapse ? Describe(process, a) : Describe(process, a)] += n;
        std::vector<std::pair<uint32_t, std::string>> v;
        for (auto& [name, n] : byName)
            v.push_back({n, name});
        std::sort(v.rbegin(), v.rend());
        fprintf(f, "== %s (%u samples)\n", title, g_samples);
        for (size_t i = 0; i < v.size() && i < 60; ++i)
            fprintf(f, "%6u %5.1f%%  %s\n", v[i].first, 100.0 * v[i].first / (g_samples ? g_samples : 1),
                    v[i].second.c_str());
    };
    dump("self: where the instruction pointer was", g_self, true);
    dump("game: innermost game-image frame on the stack", g_game, false);
    dump("hook (inclusive): functions of this DLL on the stack, once per sample", g_hookRet, true);
    dump("file writes: the first game or DLL return addresses on the stack while in ZwWriteFile", g_writeCallers,
         false);
    dump("registry opens: the same while in NtOpenKeyEx", g_regCallers, false);
    fclose(f);
}

DWORD WINAPI Sampler(LPVOID)
{
    timeBeginPeriod(1);
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(GetCurrentProcess(), nullptr, TRUE);  // symbols are only looked up when writing the report
    HANDLE thread = nullptr;
    ULONGLONG lastWrite = GetTickCount64();
    {
        HMODULE self = nullptr;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(&Sampler), &self);
        auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(self);
        auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(reinterpret_cast<const uint8_t*>(self) + dos->e_lfanew);
        g_hookBegin = reinterpret_cast<uintptr_t>(self);
        g_hookEnd = g_hookBegin + nt->OptionalHeader.SizeOfImage;
    }
    std::map<DWORD64, DWORD64> funcOf;  // return address -> function start (dbghelp; cached)
    for (;;)
    {
        Sleep(1);
        if (g_writeNow.exchange(false))
            Write();
        DWORD id = g_mainThread.load();
        if (!id || !g_window.load())
            continue;
        if (!thread)
            thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, id);
        if (!thread)
            continue;
        // While the thread is suspended nothing here may lock or allocate (the thread may hold the heap or loader
        // lock): copy its registers and the top of its stack, resume it, then analyse the copy.
        static uint32_t stack[4096];
        uint32_t words = 0;
        if (SuspendThread(thread) == static_cast<DWORD>(-1))
            continue;
        CONTEXT ctx{};
        ctx.ContextFlags = CONTEXT_CONTROL;
        bool ok = GetThreadContext(thread, &ctx) != 0;
        if (ok)
        {
            MEMORY_BASIC_INFORMATION mbi;
            if (VirtualQuery(reinterpret_cast<void*>(static_cast<uintptr_t>(ctx.Esp)), &mbi, sizeof(mbi)))
            {
                uintptr_t end = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
                uintptr_t n = (end - ctx.Esp) / 4;
                words = static_cast<uint32_t>(n < 4096 ? n : 4096);
                memcpy(stack, reinterpret_cast<void*>(static_cast<uintptr_t>(ctx.Esp)), words * 4);
            }
        }
        ResumeThread(thread);
        if (!ok)
            continue;
        DWORD64 self = ctx.Eip, game = InGame(self) ? self : 0;
        // Who is writing files / opening registry keys on the presenting thread (8% of a 4K walk was in ZwWriteFile).
        static const uintptr_t zwWrite = reinterpret_cast<uintptr_t>(GetProcAddress(GetModuleHandleA("ntdll"), "ZwWriteFile"));
        static const uintptr_t ntOpenKey = reinterpret_cast<uintptr_t>(GetProcAddress(GetModuleHandleA("ntdll"), "NtOpenKeyEx"));
        std::map<DWORD64, uint32_t>* callers = (self >= zwWrite && self < zwWrite + 0x20)       ? &g_writeCallers
                                               : (self >= ntOpenKey && self < ntOpenKey + 0x20) ? &g_regCallers
                                                                                                : nullptr;
        if (callers)
        {
            int found = 0;
            std::lock_guard<std::mutex> g(g_lock);
            for (uint32_t w = 0; w < words && found < 10; ++w)
            {
                uintptr_t a = stack[w];
                const bool inHook = a >= g_hookBegin + 0x1000 && a < g_hookEnd;
                const bool inGame = a >= 0x00401005 && a < 0x0056b000;
                if (!inHook && !inGame)
                    continue;
                const uint8_t* q = reinterpret_cast<const uint8_t*>(a);
                if (q[-5] == 0xe8 || (q[-2] == 0xff && (q[-1] & 0x38) == 0x10) || (q[-3] == 0xff && (q[-2] & 0x38) == 0x10) ||
                    (q[-6] == 0xff && (q[-5] & 0x38) == 0x10))
                    ++(*callers)[a], ++found;
            }
        }
        // Innermost return address into the game's code: a stack word inside the game image that follows a call.
        for (uint32_t w = 0; !game && w < words; ++w)
        {
            uint32_t a = stack[w];
            if (a < 0x00401005 || a >= 0x0056b000)
                continue;
            const uint8_t* p = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(a));
            if (p[-5] == 0xe8 || (p[-2] == 0xff && (p[-1] & 0x38) == 0x10) || (p[-3] == 0xff && (p[-2] & 0x38) == 0x10) ||
                (p[-6] == 0xff && (p[-5] & 0x38) == 0x10))
                game = a;
        }
        // Inclusive: each function of this DLL with a return address on the stack, once per sample.
        std::vector<DWORD64> funcs;
        {
            uintptr_t eip = static_cast<uintptr_t>(ctx.Eip);
            auto add = [&](DWORD64 a) {
                auto it = funcOf.find(a);
                if (it == funcOf.end())
                {
                    alignas(SYMBOL_INFO) char sb[sizeof(SYMBOL_INFO) + 256];
                    auto* sym = reinterpret_cast<SYMBOL_INFO*>(sb);
                    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
                    sym->MaxNameLen = 255;
                    DWORD64 disp = 0;
                    it = funcOf.emplace(a, SymFromAddr(GetCurrentProcess(), a, &disp, sym) ? sym->Address : a).first;
                }
                if (std::find(funcs.begin(), funcs.end(), it->second) == funcs.end())
                    funcs.push_back(it->second);
            };
            if (eip >= g_hookBegin && eip < g_hookEnd)
                add(eip);
            for (uint32_t w = 0; w < words; ++w)
            {
                uintptr_t a = stack[w];
                if (a < g_hookBegin + 0x1000 || a >= g_hookEnd)
                    continue;
                const uint8_t* q = reinterpret_cast<const uint8_t*>(a);
                if (q[-5] == 0xe8 || (q[-2] == 0xff && (q[-1] & 0x38) == 0x10) || (q[-3] == 0xff && (q[-2] & 0x38) == 0x10) ||
                    (q[-6] == 0xff && (q[-5] & 0x38) == 0x10))
                    add(a);
            }
        }
        {
            std::lock_guard<std::mutex> g(g_lock);
            for (DWORD64 fn : funcs)
                ++g_hookRet[fn];
            ++g_self[self];
            if (game)
                ++g_game[game];
            ++g_samples;
        }
        if (GetTickCount64() - lastWrite > 10000)
        {
            Write();
            lastWrite = GetTickCount64();
        }
    }
}
}  // namespace

int TestFramesSinceLoad();  // testscenario.cpp

// [test] profile_window=first,last: sample only while the frames since the level load are in that range (0,0 = all).
int g_windowFirst, g_windowLast;

void ProfilerInit()
{
    char w[32];
    ConfigString("test", "profile_window", "", w, sizeof(w));
    if (std::sscanf(w, "%d,%d", &g_windowFirst, &g_windowLast) != 2)
        g_windowFirst = g_windowLast = 0;
    if (!ConfigInt("test", "profile", 0))
        return;
    g_thread = CreateThread(nullptr, 0, Sampler, nullptr, 0, nullptr);
    Log("profiler: sampling the presenting thread; and-profile.txt every 10 s");
}

// Called on every presented frame (both backends): the presenting thread is the one sampled.
void ProfilerOnFrame()
{
    if (g_thread && !g_mainThread.load())
        g_mainThread = GetCurrentThreadId();
    if (g_thread)
    {
        const int f = TestFramesSinceLoad();
        const bool in = !g_windowLast || (f >= g_windowFirst && f <= g_windowLast);
        if (g_window.load() && !in && g_windowLast && f > g_windowLast)
            g_writeNow = true;
        g_window = in;
    }
}
