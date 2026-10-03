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

void HooksInstallAll()
{
    int on = 0, off = 0;
    for (HookEntry* e = g_entries; e; e = e->next)
    {
        int mode = ConfigInt("hooks", e->name, 1);
        if (mode == 0)
        {
            off++;
            continue;
        }
        if (HookAt(reinterpret_cast<void*>(static_cast<uintptr_t>(e->address)), e->replacement, e->original, e->name))
            on++;
    }
    Log("recovered-function hooks: %d enabled, %d switched off", on, off);
}

void HooksLogActive()
{
    for (HookEntry* e = g_entries; e; e = e->next)
        if (ConfigInt("hooks", e->name, 1) != 0)
            Log("  active: 0x%08x %s", e->address, e->name);
}
