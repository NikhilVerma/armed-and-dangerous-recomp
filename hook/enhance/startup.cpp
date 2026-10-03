// Start-up enhancements. Switches in [startup] in and.ini, all off by default.
//
//   skip_intro=1   do not play the intro movies on the first front-end load
//   skip_legal=1   do not show the legal text screen on the first front-end load
//                  (src/game/start_level.cpp, g_startupOptions)
#include <windows.h>

#include <cstring>

#include "config.h"
#include "game/game_object.h"
#include "log.h"

void StartupInit()
{
    if (ConfigInt("startup", "skip_intro", 0))
    {
        g_startupOptions.playIntroMovies = false;
        Log("startup: intro movies skipped");
    }
    if (ConfigInt("startup", "skip_legal", 0))
    {
        g_startupOptions.showLegalText = false;
        Log("startup: legal text skipped");
    }

    // load=lev_1_m1 (a level) or load=-Game 1 (a save slot): requested right after the front end loads.
    char ini[MAX_PATH], level[0x104];
    GetModuleFileNameA(nullptr, ini, MAX_PATH);
    strcpy(strrchr(ini, '\\') + 1, "and.ini");
    GetPrivateProfileStringA("startup", "load", "", level, sizeof(level), ini);
    if (level[0])
    {
        MultiByteToWideChar(CP_ACP, 0, level, -1, g_startupOptions.autoLoad, 0x104);
        Log("startup: will load %s after the front end", level);
    }
}
