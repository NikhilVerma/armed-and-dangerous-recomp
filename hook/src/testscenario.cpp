// Scripted test scenarios, driven from outside game code so the original game (every recovered function
// switched off) and recovered code run the same scenario.
//
// [test] scenario_load=lev_1_m1   once the front end has loaded (Game::shellFlags bit 1), write the main loop's
//                                 deferred level request (Game+0x74d flag, +0x84e name), serviced at 0x00402e9e
// [test] exit_frame=N             terminate the process after presenting frame N (flushes test output first)
// [test] close_frame=N            post WM_CLOSE to the game window at frame N: the game shuts down through its own
//                                 code (destructors, device release); test output is flushed when the DLL unloads
// [test] resize_frame=N           resize the game window to an 800x600 client area at frame N
#include <windows.h>

#include <cstdio>

#include "config.h"
#include "game/game_object.h"
#include "hooks.h"
#include "log.h"

namespace
{
wchar_t g_level[0x104];
bool g_requested;
unsigned g_exitFrame, g_closeFrame, g_resizeFrame, g_frame;
// Level-load window for the Direct3D trace: from the scenario's level request until the game's loading flag
// (Game+0x74c) has been seen set and then cleared.
bool g_loadWindow, g_loadSeen;
unsigned g_loadFinishedFrame;  // 0 until the scenario's level has loaded

BOOL CALLBACK ResizeProcessWindows(HWND wnd, LPARAM)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(wnd, &pid);
    if (pid == GetCurrentProcessId() && IsWindowVisible(wnd))
    {
        RECT r = {0, 0, 800, 600};
        AdjustWindowRectEx(&r, GetWindowLongA(wnd, GWL_STYLE), FALSE, GetWindowLongA(wnd, GWL_EXSTYLE));
        SetWindowPos(wnd, nullptr, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER);
    }
    return TRUE;
}

BOOL CALLBACK CloseProcessWindows(HWND wnd, LPARAM)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(wnd, &pid);
    if (pid == GetCurrentProcessId() && IsWindowVisible(wnd))
        PostMessageA(wnd, WM_CLOSE, 0, 0);
    return TRUE;
}
}  // namespace

// [test] no_movies=1: OpenDivxFile (DivxMediaLib import, cdecl) fails, so every movie ends at once. DivX
// decoding runs on its own thread and made scenarios timing-sensitive; this sits outside game code, so the
// original and recovered code see the same thing.
extern "C" static void* __cdecl Fake_OpenDivxFile(void*)
{
    return nullptr;
}

void TestScenarioInit()
{
    if (ConfigInt("test", "no_movies", 0))
    {
        void* real;
        if (PatchImport("DivxMediaLib.dll", "OpenDivxFile", reinterpret_cast<void*>(Fake_OpenDivxFile), &real))
            Log("test: movies disabled");
    }
    char ini[MAX_PATH], level[0x104];
    GetModuleFileNameA(nullptr, ini, MAX_PATH);
    strcpy(strrchr(ini, '\\') + 1, "and.ini");
    GetPrivateProfileStringA("test", "scenario_load", "", level, sizeof(level), ini);
    if (level[0])
        MultiByteToWideChar(CP_ACP, 0, level, -1, g_level, 0x104);
    g_exitFrame = ConfigInt("test", "exit_frame", 0);
    g_closeFrame = ConfigInt("test", "close_frame", 0);
    g_resizeFrame = ConfigInt("test", "resize_frame", 0);
    if (level[0] || g_exitFrame)
        Log("test: scenario load=%s exit_frame=%u", level, g_exitFrame);
}

void D3DTraceFlush();
void TestClockSet(double seconds);  // testclock.cpp
void D3DTraceFinish();
void RhiWriteCalls();

// Main thread, once per presented frame (after Present).
void TestScenarioOnFrame()
{
    g_frame++;
    Game* game = g_game;
    if (g_level[0] && !g_requested && game && (game->shellFlags & 2))
    {
        wcsncpy(game->pendingLevelName, g_level, 0x103);
        game->pendingLevelRequest = 1;
        g_requested = true;
        g_loadWindow = true;
        Log("test: requested %ls at frame %u", g_level, g_frame);
    }
    if (g_loadWindow && game)
    {
        g_loadSeen |= game->loadingLevel != 0;
        if (g_loadSeen && !game->loadingLevel)
        {
            g_loadWindow = false;
            g_loadFinishedFrame = g_frame;
            Log("test: level load finished at frame %u", g_frame);
            // [test] reseed_on_load=1: the same state at the end of the load whatever the load took (the number of
            // loading frames differs per backend and per run): the game's C runtime srand (0x005391c0) with a fixed
            // seed, and the virtual clock set to 1000 s.
            if (ConfigInt("test", "reseed_on_load", 0))
            {
                reinterpret_cast<void(__cdecl*)(unsigned)>(0x005391c0)(12345);
            }
        }
    }
    if (g_resizeFrame && g_frame == g_resizeFrame)
    {
        Log("test: resizing the game window to 800x600 client at frame %u", g_frame);
        EnumWindows(ResizeProcessWindows, 0);
    }
    if (g_closeFrame && g_frame == g_closeFrame)
    {
        Log("test: closing the game window at frame %u", g_frame);
        EnumWindows(CloseProcessWindows, 0);
    }
    if (g_exitFrame && g_frame >= g_exitFrame)
    {
        Log("test: exit at frame %u", g_frame);
        D3DTraceFlush();
        HooksWriteCoverage();
        RhiWriteCalls();
        TerminateProcess(GetCurrentProcess(), 0);
    }
}


// DLL_PROCESS_DETACH: the game exited by itself (close_frame); keep the test output.
void TestScenarioOnExit()
{
    Log("test: process exiting after frame %u", g_frame);
    D3DTraceFinish();
    HooksWriteCoverage();
    RhiWriteCalls();
}

// True while the scenario's level is loading (see g_loadWindow).
bool TestInLoadWindow()
{
    return g_loadWindow;
}
// [test] trace_fatal=1: log who calls the game's fatal-error routine 0x004495b0 (shows the "problem with the disc"
// screen), with its caller's return address, then run it.
static void* g_realFatal;
static void __stdcall LogFatal(DWORD returnAddress, DWORD arg)
{
    Log("test: fatal error routine called from 0x%08lx (arg %ld)", returnAddress - 5, static_cast<long>(arg));
}
static __declspec(naked) void Hook_Fatal()
{
    __asm {
        pushad
        push dword ptr [esp + 36]  // the caller's first stack argument
        push dword ptr [esp + 36]  // return address (esp + 32 + 4 after the first push)
        call LogFatal
        popad
        jmp dword ptr [g_realFatal]
    }
}

void TestFatalTraceInit()
{
    if (ConfigInt("test", "trace_fatal", 0))
        HookAt(reinterpret_cast<void*>(0x004495b0), reinterpret_cast<void*>(Hook_Fatal), &g_realFatal, "fatal-error trace");
}


// Frames presented since the scenario's level finished loading, or -1 before that (for [test] hash_from_load).
int TestFramesSinceLoad()
{
    return g_loadFinishedFrame ? static_cast<int>(g_frame - g_loadFinishedFrame) : -1;
}