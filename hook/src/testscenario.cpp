// Scripted test scenarios, driven from outside game code so the original game (every recovered function
// switched off) and recovered code run the same scenario.
//
// [test] scenario_load=lev_1_m1   once the front end has loaded (Game::shellFlags bit 1), write the main loop's
//                                 deferred level request (Game+0x74d flag, +0x84e name), serviced at 0x00402e9e
// [test] exit_frame=N             terminate the process after presenting frame N (flushes test output first)
#include <windows.h>

#include <cstdio>

#include "config.h"
#include "game/game_object.h"
#include "log.h"

namespace
{
wchar_t g_level[0x104];
bool g_requested;
unsigned g_exitFrame, g_frame;
}  // namespace

void TestScenarioInit()
{
    char ini[MAX_PATH], level[0x104];
    GetModuleFileNameA(nullptr, ini, MAX_PATH);
    strcpy(strrchr(ini, '\\') + 1, "and.ini");
    GetPrivateProfileStringA("test", "scenario_load", "", level, sizeof(level), ini);
    if (level[0])
        MultiByteToWideChar(CP_ACP, 0, level, -1, g_level, 0x104);
    g_exitFrame = ConfigInt("test", "exit_frame", 0);
    if (level[0] || g_exitFrame)
        Log("test: scenario load=%s exit_frame=%u", level, g_exitFrame);
}

void D3DTraceFlush();

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
    if (g_exitFrame && g_frame >= g_exitFrame)
    {
        Log("test: exit at frame %u", g_frame);
        D3DTraceFlush();
        TerminateProcess(GetCurrentProcess(), 0);
    }
}
