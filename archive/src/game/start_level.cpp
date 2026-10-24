// Recovered: Game_StartLevel, 0x00403690 (S3, Approach B).
//
// Starts a level by name. "-name" restores a save game and opens the pause menu; "!command args" runs a
// console command; anything else loads that level (falling back to lev_shell on failure). The first time the
// front end (lev_shell) loads, it pushes MainMenu, then PlayMovie (the intro movies), then LegalText; the last
// pushed screen shows first.
//
// Calls into original code go through the generated layer (src/gen/orig_calls.h, tools/abi.py), which loads
// each callee's real register inputs (link-time code generation; see docs/patterns.md).
#include <cstring>
#include <cwchar>

#include "game/game_object.h"
#include "gen/entry_stubs.h"
#include "gen/orig_calls.h"
#include "hooks.h"

StartupOptions g_startupOptions;

namespace
{
// Strings in the game image. The UI keeps these pointers, so pass the originals, not copies.
const char* const kStrLegalText = reinterpret_cast<const char*>(0x005bc0f0);
const char* const kStrPlayMovie = reinterpret_cast<const char*>(0x005bc0fc);
const char* const kStrIntroMovies = reinterpret_cast<const char*>(0x005bc108);  // LucasAnim,pm_logo,pre_1_1_opening
const char* const kStrMainMenu = reinterpret_cast<const char*>(0x005bc12c);
const char* const kStrPauseMenu = reinterpret_cast<const char*>(0x005bc138);
const char* const kStrLevShell = reinterpret_cast<const char*>(0x005bc144);

template <class T>
uint32_t u(T* p)
{
    return reinterpret_cast<uint32_t>(p);
}

// Named views of the original functions called here (inputs per symbols/abi.csv).
void UI_PushScreen(void* ui, const char* screen, const char* argument)  // 0x00490b00: eax=arg, ecx=ui; [name]
{
    orig::fn_00490b00(u(argument), u(ui), u(screen));
}
void WideToNarrow(char* dest, const wchar_t* src, int capacity)  // 0x004e5a10: esi=dest, edi=src; [capacity]
{
    orig::fn_004e5a10(u(dest), u(src), capacity);
}
bool LevelExists(void* levelManager, const char* name)  // 0x0044ab90: eax=name, ecx=manager; AL
{
    return (orig::fn_0044ab90(u(name), u(levelManager)) & 0xff) != 0;
}
}  // namespace

bool Game_StartLevel(Game* self, const wchar_t* name, uint32_t arg1, uint32_t restoring)
{
    self->loadingLevel = 1;
    self->vtbl->SetBusy(self, nullptr, 1);

    if (name && name[0] == L'-')
    {
        if ((orig::fn_0048c9b0(u(g_game->field_ab8), u(name + 1)) & 0xff) == 0)  // restore save
            return false;
        UI_PushScreen(g_game->ui, kStrPauseMenu, nullptr);
        return true;
    }

    if (name && name[0] == L'!')
    {
        char command[0x104];
        WideToNarrow(command, name, 0x104);
        // The narrow copy starts with the '!'; the command word follows it. Split at the first run of spaces.
        char* p = command + 1;
        while (*p != ' ' && *p != '\0')
            p++;
        char* args = nullptr;
        if (*p != '\0')
        {
            while (*p == ' ')
                *p++ = '\0';
            if (*p != '\0')
                args = p;
        }
        g_game->vtbl->ConsoleCommand(g_game, nullptr, command + 1, args);
        return true;
    }

    orig::fn_00403940(u(self));  // unload the current level (edi = game)
    memset(self->field_bd0, 0, sizeof(self->field_bd0));
    memset(g_game->field_d90, 0, sizeof(g_game->field_d90));
    memset(g_game->field_ac8, 0, sizeof(g_game->field_ac8));

    char levelName[0x104];
    size_t length = wcslen(name);
    size_t i = 0;
    for (int room = 0x104; i < length && --room != 0; i++)
        levelName[i] = static_cast<char>(name[i]);
    levelName[i] = '\0';

    if (levelName[0] == '\0' || !LevelExists(self->levelManager, levelName))
    {
        if (_stricmp(levelName, kStrLevShell) == 0 ||
            (orig::fn_00403640(u(self), u(kStrLevShell), 0, 0) & 0xff) == 0)  // load lev_shell instead
        {
            self->vtbl->OnLevelLoadFailed(self, nullptr);
            return false;
        }
        return true;
    }

    orig::fn_0044a110(u(self->levelManager), 0, u(levelName));  // begin loading (ecx unused: stack slot)
    orig::fn_00416130(u(self->field_a7c));
    orig::fn_0044a4b0(arg1, u(self->levelManager), restoring != 0);  // finish loading
    if (self->field_a68 != 0)
        orig::fn_0040f8c0(self->field_a68, orig::f2u(self->field_4b0));  // edx = Game+0xa68 object
    orig::fn_00403aa0(u(self), orig::f2u(1.0f), 0);  // level fade

    if (!restoring && (self->field_748 == 0 || _stricmp(levelName, kStrLevShell) == 0))
    {
        UI_PushScreen(self->ui, kStrMainMenu, nullptr);
        if (!(self->shellFlags & 2))
        {
            if (g_startupOptions.playIntroMovies)
                UI_PushScreen(self->ui, kStrPlayMovie, kStrIntroMovies);
            if (g_startupOptions.showLegalText)
                UI_PushScreen(self->ui, kStrLegalText, nullptr);
            self->shellFlags |= 2;
            if (g_startupOptions.autoLoad[0])
            {
                wcsncpy(self->pendingLevelName, g_startupOptions.autoLoad, 0x103);
                self->pendingLevelRequest = 1;
            }
        }
        orig::fn_00401e80(0, u(self), 1);  // al = 0, edx = game; [1]
    }
    orig::fn_00401f10(u(self));
    return true;
}

// Generated entry: original callers pass eax = name, ecx = game, [arg1, restoring].
static uint32_t Game_StartLevel_Abi(uint32_t eax, uint32_t ecx, uint32_t arg1, uint32_t restoring)
{
    return Game_StartLevel(reinterpret_cast<Game*>(ecx), reinterpret_cast<const wchar_t*>(eax), arg1, restoring);
}
ABI_ENTRY_00403690(Game_StartLevel_Entry, Game_StartLevel_Abi)

HOOK(0x00403690, "Game_StartLevel", Game_StartLevel_Entry, nullptr);
