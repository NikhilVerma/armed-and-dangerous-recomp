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
void D3DTraceFinish();

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
        Log("test: requested %ls at frame %u", g_level, g_frame);
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
        TerminateProcess(GetCurrentProcess(), 0);
    }
}


// DLL_PROCESS_DETACH: the game exited by itself (close_frame); keep the test output.
void TestScenarioOnExit()
{
    Log("test: process exiting after frame %u", g_frame);
    D3DTraceFinish();
    HooksWriteCoverage();
}