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
    void* wrapper = nullptr;       // set by HookWrap: an enhancement that runs in front of the function
    void** wrapperNext = nullptr;  // receives whatever the wrapper must call: recovered code or original
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

// Enhancements (hook/enhance/) extend a function without editing src/: the wrapper is installed at the
// function's address and `*next` is set to the recovered function, or to the original when its [hooks] switch
// is 0. Call before HooksInstallAll. Returns false if no hook with that name is registered.
bool HookWrap(const char* name, void* wrapper, void** next);

bool HooksInit();
void HooksInstallAll();
void HooksLogActive();
void HooksWriteCoverage();  // and-coverage.txt when [test] hook_coverage=1 (the harness exits with TerminateProcess)

// Hook any code address (used by enhancements and API hooks). Returns false on failure.
bool HookAt(void* target, void* detour, void** original, const char* label);

// Replace one import in the game's import table (the loader has bound all imports before any DllMain runs).
bool PatchImport(const char* dll, const char* func, void* hook, void** original);

// Overwrite bytes in the game image after checking they still hold `expected` (guards against a wrong build).
bool PatchBytes(unsigned address, const void* expected, const void* replacement, unsigned length, const char* label);
bool PatchU32(unsigned address, unsigned expected, unsigned value, const char* label);
