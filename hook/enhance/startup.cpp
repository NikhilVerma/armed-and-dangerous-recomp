// Start-up enhancements. Switches in [startup] in and.ini, all off by default.
//
//   skip_intro=1   do not play the intro movies on the first front-end load
//   skip_legal=1   do not show the legal text screen on the first front-end load
//                  (src/game/start_level.cpp, g_startupOptions)
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
}
