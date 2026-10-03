#pragma once

// Function replacement (piece-by-piece migration, docs/decision.md).
//
// A recovered function in src/ declares itself with HOOK(address, name, function). At startup every
// registered hook is installed according to its [hooks] switch in and.ini:
//   0 = leave the original running
//   1 = replace the original with the recovered function (default)
//   2 = differential: reserved for the differential test harness
// `original` receives a trampoline to the original code, so a replacement or a test can call it.

struct HookEntry
{
    unsigned address;  // original virtual address in game.unpacked.exe (f6a690a4...)
    const char* name;
    void* replacement;
    void** original;   // optional: set to a callable trampoline
    HookEntry* next;
};

struct HookRegistrar
{
    HookRegistrar(HookEntry* e);
};

#define HOOK_CONCAT2(a, b) a##b
#define HOOK_CONCAT(a, b) HOOK_CONCAT2(a, b)
#define HOOK(addr, name, fn, orig)                                                         \
    static HookEntry HOOK_CONCAT(hook_entry_, __LINE__){addr, name, (void*)(fn), (void**)(orig), nullptr}; \
    static HookRegistrar HOOK_CONCAT(hook_reg_, __LINE__)(&HOOK_CONCAT(hook_entry_, __LINE__))

bool HooksInit();
void HooksInstallAll();
void HooksLogActive();

// Hook any code address (used by enhancements and API hooks). Returns false on failure.
bool HookAt(void* target, void* detour, void** original, const char* label);
