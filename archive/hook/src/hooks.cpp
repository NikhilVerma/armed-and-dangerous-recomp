#include "hooks.h"

#include <windows.h>
#include <MinHook.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>

#include "config.h"
#include "log.h"

static HookEntry* g_entries;

HookRegistrar::HookRegistrar(HookEntry* e)
{
    e->next = g_entries;
    g_entries = e;
}

bool HookWrap(const char* name, void* wrapper, void** next)
{
    for (HookEntry* e = g_entries; e; e = e->next)
    {
        if (strcmp(e->name, name) == 0)
        {
            if (e->wrapper)
            {
                // a second wrapper runs in front of the first, which keeps receiving the function
                *next = e->wrapper;
                e->wrapper = wrapper;
                Log("wrap %s with a further enhancement", name);
                return true;
            }
            e->wrapper = wrapper;
            e->wrapperNext = next;
            Log("wrap %s with an enhancement", name);
            return true;
        }
    }
    Log("wrap %s: no such hook", name);
    return false;
}

bool HooksInit()
{
    MH_STATUS s = MH_Initialize();
    if (s != MH_OK)
    {
        Log("MH_Initialize failed: %s", MH_StatusToString(s));
        return false;
    }
    return true;
}

bool HookAt(void* target, void* detour, void** original, const char* label)
{
    MH_STATUS s = MH_CreateHook(target, detour, original);
    if (s == MH_OK)
        s = MH_EnableHook(target);
    Log("hook %-40s at %p: %s", label, target, MH_StatusToString(s));
    return s == MH_OK;
}

// Last resort for data that has no function to recover (e.g. a constant table). Code changes go through
// recovered functions plus HookWrap instead.
bool PatchBytes(unsigned address, const void* expected, const void* replacement, unsigned length, const char* label)
{
    auto p = reinterpret_cast<void*>(static_cast<uintptr_t>(address));
    if (memcmp(p, expected, length) != 0)
    {
        Log("patch %-40s at 0x%08x: original bytes differ, NOT patched", label, address);
        return false;
    }
    DWORD old;
    VirtualProtect(p, length, PAGE_EXECUTE_READWRITE, &old);
    memcpy(p, replacement, length);
    VirtualProtect(p, length, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, length);
    Log("patch %-40s at 0x%08x: ok", label, address);
    return true;
}

bool PatchU32(unsigned address, unsigned expected, unsigned value, const char* label)
{
    return PatchBytes(address, &expected, &value, sizeof(value), label);
}

// [test] hook_coverage=1: every registered function (recovered or not) goes through a counting stub
// `lock inc dword [count]; jmp dword [next]`, and the counts are written to and-coverage.txt at exit. Tells which
// functions the comparison scenarios actually execute (in the original as well as in recovered code).
struct CoverageSlot
{
    const char* name;
    unsigned address;
    volatile long count;
    void* next;
};
static CoverageSlot* g_coverage;
static int g_coverageCount;

void HooksWriteCoverage()
{
    if (!g_coverage)
        return;
    FILE* f = fopen("and-coverage.txt", "w");
    if (!f)
        return;
    for (int k = 0; k < g_coverageCount; ++k)
        fprintf(f, "0x%08x %ld %s\n", g_coverage[k].address, g_coverage[k].count, g_coverage[k].name);
    fclose(f);
}

static void* CoverageStub(CoverageSlot* slot)
{
    static BYTE* page;
    static int used;
    if (!page || used + 16 > 4096)
    {
        page = static_cast<BYTE*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        used = 0;
    }
    BYTE* p = page + used;
    used += 16;
    p[0] = 0xf0, p[1] = 0xff, p[2] = 0x05;  // lock inc dword ptr [&slot->count]
    *reinterpret_cast<volatile long**>(p + 3) = &slot->count;
    p[7] = 0xff, p[8] = 0x25;  // jmp dword ptr [&slot->next]
    *reinterpret_cast<void***>(p + 9) = &slot->next;
    return p;
}

// The [hooks] section read once (GetPrivateProfileInt per hook took minutes with ~3,500 hooks).
static std::unordered_map<std::string, int>& HookSwitches()
{
    static std::unordered_map<std::string, int> switches;
    static bool loaded;
    if (!loaded)
    {
        loaded = true;
        std::vector<char> buf(1 << 20);
        GetPrivateProfileSectionA("hooks", buf.data(), static_cast<DWORD>(buf.size()), ConfigIniPath());
        for (const char* p = buf.data(); *p; p += strlen(p) + 1)
            if (const char* eq = strchr(p, '='))
                switches[std::string(p, eq)] = atoi(eq + 1);
    }
    return switches;
}

static int HookSwitch(const char* name)
{
    auto& switches = HookSwitches();
    auto it = switches.find(name);
    return it == switches.end() ? 1 : it->second;
}

// Trampolines of the recovered-function hooks, so a crash inside one names its function (crash.cpp).
struct TrampolineRecord
{
    const BYTE* trampoline;
    const char* name;
    unsigned address;
};
static std::vector<TrampolineRecord> g_trampolines;

bool HooksDescribeAddress(const void* address, const char** name, unsigned* original)
{
    const BYTE* a = static_cast<const BYTE*>(address);
    for (const TrampolineRecord& r : g_trampolines)
        if (a >= r.trampoline && a < r.trampoline + 64)  // MinHook memory slot size
        {
            *name = r.name, *original = r.address;
            return true;
        }
    return false;
}

static bool CoverageSkipped(unsigned address)
{
    static std::vector<unsigned> skip;
    static bool loaded;
    if (!loaded)
    {
        loaded = true;
        char list[4096];
        ConfigString("test", "coverage_skip", "", list, sizeof(list));
        for (char* p = list; *p;)
        {
            char* end;
            const unsigned long a = strtoul(p, &end, 16);
            if (end == p)
                break;
            skip.push_back(static_cast<unsigned>(a));
            p = *end == ',' ? end + 1 : end;
        }
    }
    for (unsigned a : skip)
        if (a == address)
            return true;
    return false;
}

void HooksInstallAll()
{
    int on = 0, off = 0, failed = 0;
    bool coverage = ConfigInt("test", "hook_coverage", 0) != 0;
    if (coverage)
    {
        int n = 0;
        for (HookEntry* e = g_entries; e; e = e->next)
            ++n;
        g_coverage = new CoverageSlot[n]();
        atexit(HooksWriteCoverage);
    }
    for (HookEntry* e = g_entries; e; e = e->next)
    {
        int mode = HookSwitch(e->name);
        mode == 0 ? off++ : on++;
        if (mode == 0 && !e->wrapper && !coverage)
            continue;
        // Originals that branch back into their own first 5 bytes cannot run under a patch: when switched off they
        // are left alone even in coverage mode ([test] coverage_skip, from tools/coverage.py; seen at 0x004643d0).
        if (mode == 0 && !e->wrapper && coverage && CoverageSkipped(e->address))
            continue;
        void* target = reinterpret_cast<void*>(static_cast<uintptr_t>(e->address));
        void* detour = mode == 0 && !e->wrapper ? nullptr : (e->wrapper ? e->wrapper : e->replacement);
        CoverageSlot* slot = nullptr;
        if (coverage)
        {
            slot = &g_coverage[g_coverageCount++];
            slot->name = e->name;
            slot->address = e->address;
            slot->next = detour;
            detour = CoverageStub(slot);
        }
        // Created now, enabled together below (MH_EnableHook freezes every thread on each call).
        void* trampoline = nullptr;
        MH_STATUS s = MH_CreateHook(target, detour, &trampoline);
        if (s == MH_OK)
            s = MH_QueueEnableHook(target);
        if (s != MH_OK)
        {
            Log("hook %-40s at %p: %s", e->name, target, MH_StatusToString(s));
            ++failed;
            continue;
        }
        g_trampolines.push_back({static_cast<const BYTE*>(trampoline), e->name, e->address});
        if (slot && !slot->next)
            slot->next = trampoline;  // switched off: count, then run the original
        if (e->original)
            *e->original = trampoline;
        if (e->wrapper)
            *e->wrapperNext = mode == 0 ? trampoline : e->replacement;
    }
    const MH_STATUS applied = MH_ApplyQueued();
    Log("recovered-function hooks: %d enabled, %d switched off, %d failed%s; enable: %s", on, off, failed,
        coverage ? " (coverage counting on)" : "", MH_StatusToString(applied));
}

void HooksLogActive()
{
    for (HookEntry* e = g_entries; e; e = e->next)
        if (HookSwitch(e->name) != 0)
            Log("  active: 0x%08x %s", e->address, e->name);
}
