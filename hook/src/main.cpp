// and_hook: loaded into the original game as dinput8.dll (the game imports DirectInput8Create from it).
// Installs recovered-function hooks and enhancement hooks before the game's own startup code runs.
#include <windows.h>
#include <unknwn.h>

#include "display.h"
#include "hooks.h"
#include "log.h"

void CrashInstall();
void StartupInit();
void RenderInit();
void CameraInit();
void PersistenceInit();
void PlaygroundInit();
void LimitsInit();
void AudioInit();
void QolInit();
void InputInit();
void GamepadInit();
void ProfilerInit();
void UiAspectInit();
void ShadowsInit();
void WeatherInit();
void PhysicsJoltInit();
void OverridesInit();
void RemasterMenuInit();
void TestShotInit();
void TestMaterialLogInit();
void InputHooksInit();
void TestHashInit();
void TestScenarioInit();
void TestOffscreenInit();
void TestOracleInit();
void TestScenarioOnExit();
void TestFatalTraceInit();
void TestClockInit();
void InputHooksOnDirectInput(void* directInput);
void D3D9HooksInstall();

// The game expects this image base; refuse to patch anything else.
static constexpr unsigned kExpectedTimestamp = 1074793270;  // PE timestamp of game.unpacked.exe

static bool CheckGameBuild()
{
    auto base = reinterpret_cast<BYTE*>(GetModuleHandleA(nullptr));
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->FileHeader.TimeDateStamp != kExpectedTimestamp || nt->OptionalHeader.ImageBase != 0x400000)
    {
        Log("unexpected executable (timestamp %lu, base 0x%08lx); hooks NOT installed",
            nt->FileHeader.TimeDateStamp, nt->OptionalHeader.ImageBase);
        return false;
    }
    return true;
}

static void Startup()
{
    LogOpen();
    Log("and_hook loaded into %s", GetCommandLineA());
    TestShotInit();
    TestMaterialLogInit();
    TestHashInit();
    TestOffscreenInit();  // before TestScenarioInit (its window procedure asks TestOffscreen)
    TestScenarioInit();
    InputHooksInit();
    CrashInstall();
    if (!CheckGameBuild() || !HooksInit())
        return;
    TestClockInit();  // before anything reads the time
    DisplayInit();    // enhancements register their wrappers before hooks are installed
    StartupInit();
    RenderInit();
    CameraInit();
    PersistenceInit();
    PlaygroundInit();
    LimitsInit();
    AudioInit();
    QolInit();
    InputInit();
    GamepadInit();
    ProfilerInit();
    UiAspectInit();
    ShadowsInit();
    WeatherInit();
    PhysicsJoltInit();
    OverridesInit();
    RemasterMenuInit();  // after every enhancement has registered its settings
    TestFatalTraceInit();  // a wrapper: registered before the hooks are installed
    TestOracleInit();      // wrappers too; after TestMaterialLogInit (it chains the sound callback)
    HooksInstallAll();
    D3D9HooksInstall();
}

// --- dinput8 proxy -------------------------------------------------------------------------------------------

using DirectInput8Create_t = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);

extern "C" HRESULT WINAPI Proxy_DirectInput8Create(HINSTANCE inst, DWORD version, REFIID riid, LPVOID* out,
                                                   LPUNKNOWN outer)
{
    static DirectInput8Create_t real;
    if (!real)
    {
        char path[MAX_PATH];
        GetSystemDirectoryA(path, MAX_PATH);  // SysWOW64 for a 32-bit process
        strcat(path, "\\dinput8.dll");
        HMODULE m = LoadLibraryA(path);
        real = m ? reinterpret_cast<DirectInput8Create_t>(GetProcAddress(m, "DirectInput8Create")) : nullptr;
        Log("real dinput8: %s -> %p", path, real);
    }
    HRESULT hr = real ? real(inst, version, riid, out, outer) : E_FAIL;
    if (SUCCEEDED(hr) && out && *out)
        InputHooksOnDirectInput(*out);
    return hr;
}

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
        Startup();
    else if (reason == DLL_PROCESS_DETACH)
    {
        Log("process detach (%s)", reserved ? "the process is exiting" : "FreeLibrary");
        TestScenarioOnExit();
    }
    return TRUE;
}
