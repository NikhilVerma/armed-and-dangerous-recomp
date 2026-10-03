#include "hooks.h"

#include <windows.h>
#include <MinHook.h>

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

void HooksInstallAll()
{
    int on = 0, off = 0;
    for (HookEntry* e = g_entries; e; e = e->next)
    {
        int mode = ConfigInt("hooks", e->name, 1);
        mode == 0 ? off++ : on++;
        if (mode == 0 && !e->wrapper)
            continue;
        void* target = reinterpret_cast<void*>(static_cast<uintptr_t>(e->address));
        void* detour = e->wrapper ? e->wrapper : e->replacement;
        void* trampoline = nullptr;
        if (!HookAt(target, detour, &trampoline, e->name))
            continue;
        if (e->original)
            *e->original = trampoline;
        if (e->wrapper)
            *e->wrapperNext = mode == 0 ? trampoline : e->replacement;
    }
    Log("recovered-function hooks: %d enabled, %d switched off", on, off);
}

void HooksLogActive()
{
    for (HookEntry* e = g_entries; e; e = e->next)
        if (ConfigInt("hooks", e->name, 1) != 0)
            Log("  active: 0x%08x %s", e->address, e->name);
}
