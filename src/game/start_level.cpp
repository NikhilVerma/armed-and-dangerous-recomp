// Recovered: Game_StartLevel, 0x00403690 (S3, Approach B).
//
// Starts a level by name. "-name" restores a save game and opens the pause menu; "!command args" runs a
// console command; anything else loads that level (falling back to lev_shell on failure). The first time the
// front end (lev_shell) loads, it pushes MainMenu, then PlayMovie (the intro movies), then LegalText; the last
// pushed screen shows first.
#include <cstring>
#include <cwchar>

#include "game/game_object.h"
#include "config.h"
#include "hooks.h"
#include "log.h"

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

// --- calls into original code that use register arguments (link-time code generation) ---------------------

// 0x00490b00: push a UI screen. ECX = screen stack, EAX = screen argument, stack: screen name; callee pops 4.
void Orig_PushScreen(void* ui, const char* screen, const char* argument)
{
    __asm {
        mov ecx, ui
        mov eax, argument
        push screen
        mov edx, 0x00490b00
        call edx
    }
}

// 0x004e5a10: wide to narrow copy. ESI = destination, EDI = source, stack: capacity (cdecl).
void Orig_WideToNarrow(char* dest, const wchar_t* src, int capacity)
{
    __asm {
        mov esi, dest
        mov edi, src
        push capacity
        mov edx, 0x004e5a10
        call edx
        add esp, 4
    }
}

// 0x00403940: tear down the current level. EDI = game.
void Orig_UnloadLevel(Game* game)
{
    __asm {
        mov edi, game
        mov edx, 0x00403940
        call edx
    }
}

// 0x0044ab90: does the level exist? ECX = level manager, EAX = name; returns AL.
bool Orig_LevelExists(void* levelManager, const char* name)
{
    char result;
    __asm {
        mov ecx, levelManager
        mov eax, name
        mov edx, 0x0044ab90
        call edx
        mov result, al
    }
    return result != 0;
}

// 0x0044a110: begin loading. EAX = level manager, stack: name; callee pops 4.
void Orig_BeginLevelLoad(void* levelManager, const char* name)
{
    __asm {
        mov eax, levelManager
        push name
        mov edx, 0x0044a110
        call edx
    }
}

// 0x00416130: EAX = object.
void Orig_00416130(void* object)
{
    __asm {
        mov eax, object
        mov edx, 0x00416130
        call edx
    }
}

// 0x0044a4b0: ECX = level manager, EAX = arg1, stack: restoring flag; callee pops 4.
void Orig_FinishLevelLoad(void* levelManager, uint32_t arg1, bool restoring)
{
    int flag = restoring ? 1 : 0;
    __asm {
        mov ecx, levelManager
        mov eax, arg1
        push flag
        mov edx, 0x0044a4b0
        call edx
    }
}

// 0x00401e80: AL = flag, EDX = game, stack: one argument; callee pops 4.
void Orig_00401e80(Game* game, bool flag, int argument)
{
    __asm {
        mov edx, game
        mov al, flag
        push argument
        mov ecx, 0x00401e80
        call ecx
    }
}

// 0x00401f10: EAX = game.
void Orig_00401f10(Game* game)
{
    __asm {
        mov eax, game
        mov edx, 0x00401f10
        call edx
    }
}

// Plain conventions.
bool Orig_RestoreSaveGame(void* loader, const wchar_t* name)  // 0x0048c9b0 thiscall
{
    return GameFunction<char(__fastcall*)(void*, void*, const wchar_t*)>(0x0048c9b0)(loader, nullptr, name) != 0;
}
// 0x0040f8c0: EDX = object at Game+0xa68 (reads +0xd0/+0xdc/+0xe0, then calls the renderer at Game+0x38,
// vtable +0x114), stack: float level; callee pops 4. EDX is a hidden argument: the original caller loads
// it for the null test just before the call.
void Orig_0040f8c0(void* object, float value)
{
    __asm {
        mov edx, object
        push value
        mov eax, 0x0040f8c0
        call eax
    }
}
void Orig_SetLevelFade(Game* game, float alpha, int arg)  // 0x00403aa0 thiscall
{
    GameFunction<void(__fastcall*)(Game*, void*, float, int)>(0x00403aa0)(game, nullptr, alpha, arg);
}
bool Orig_LoadLevelByName(Game* game, const char* name, int a, int b)  // 0x00403640 thiscall
{
    return GameFunction<char(__fastcall*)(Game*, void*, const char*, int, int)>(0x00403640)(game, nullptr, name, a,
                                                                                            b) != 0;
}

}  // namespace

bool Game_StartLevel(Game* self, const wchar_t* name, uint32_t arg1, uint32_t restoring)
{
    self->loadingLevel = 1;
    self->vtbl->SetBusy(self, nullptr, 1);

    if (name && name[0] == L'-')
    {
        if (!Orig_RestoreSaveGame(g_game->field_ab8, name + 1))
            return false;
        Orig_PushScreen(g_game->ui, kStrPauseMenu, nullptr);
        return true;
    }

    if (name && name[0] == L'!')
    {
        char command[0x104];
        Orig_WideToNarrow(command, name, 0x104);
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

    Orig_UnloadLevel(self);
    memset(self->field_bd0, 0, sizeof(self->field_bd0));
    memset(g_game->field_d90, 0, sizeof(g_game->field_d90));
    memset(g_game->field_ac8, 0, sizeof(g_game->field_ac8));

    char levelName[0x104];
    size_t length = wcslen(name);
    size_t i = 0;
    for (int room = 0x104; i < length && --room != 0; i++)
        levelName[i] = static_cast<char>(name[i]);
    levelName[i] = '\0';

    if (levelName[0] == '\0' || !Orig_LevelExists(self->levelManager, levelName))
    {
        if (_stricmp(levelName, kStrLevShell) == 0 || !Orig_LoadLevelByName(self, kStrLevShell, 0, 0))
        {
            self->vtbl->OnLevelLoadFailed(self, nullptr);
            return false;
        }
        return true;
    }

    Orig_BeginLevelLoad(self->levelManager, levelName);
    Orig_00416130(self->field_a7c);
    Orig_FinishLevelLoad(self->levelManager, arg1, restoring != 0);
    if (self->field_a68 != 0)
        Orig_0040f8c0(reinterpret_cast<void*>(self->field_a68), self->field_4b0);
    Orig_SetLevelFade(self, 1.0f, 0);

    if (!restoring && (self->field_748 == 0 || _stricmp(levelName, kStrLevShell) == 0))
    {
        Orig_PushScreen(self->ui, kStrMainMenu, nullptr);
        if (!(self->shellFlags & 2))
        {
            if (g_startupOptions.playIntroMovies)
                Orig_PushScreen(self->ui, kStrPlayMovie, kStrIntroMovies);
            if (g_startupOptions.showLegalText)
                Orig_PushScreen(self->ui, kStrLegalText, nullptr);
            self->shellFlags |= 2;
        }
        Orig_00401e80(self, false, 1);
    }
    Orig_00401f10(self);
    return true;
}

static void* g_origStartLevel;  // trampoline to the original, set by the hook installer

static bool Orig_StartLevel(Game* self, const wchar_t* name, uint32_t arg1, uint32_t restoring)
{
    void* target = g_origStartLevel;
    char result;
    __asm {
        mov ecx, self
        mov eax, name
        push restoring
        push arg1
        call target
        mov result, al
    }
    return result != 0;
}

// [hooks] Game_StartLevel=2: log every call and its result, running the original. For comparing with mode 1.
static bool Game_StartLevel_Dispatch(Game* self, const wchar_t* name, uint32_t arg1, uint32_t restoring)
{
    static const int mode = ConfigInt("hooks", "Game_StartLevel", 1);
    bool traced = mode == 2;
    if (mode >= 1)
        Log("Game_StartLevel(%ls, arg1=0x%x, restoring=%u) field_748=%u shellFlags=0x%02x a68=%u [%s]",
            name ? name : L"(null)", arg1, restoring, self->field_748, self->shellFlags, self->field_a68,
            traced ? "original" : "recovered");
    bool result = traced ? Orig_StartLevel(self, name, arg1, restoring) : Game_StartLevel(self, name, arg1, restoring);
    Log("  -> %d shellFlags=0x%02x", result, self->shellFlags);
    return result;
}

// Entry from original code: ECX = this, EAX = name, [esp+4] = arg1, [esp+8] = restoring; ret 8; result in AL.
static __declspec(naked) void Game_StartLevel_Entry()
{
    __asm {
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        push eax
        push ecx
        call Game_StartLevel_Dispatch
        add esp, 16
        ret 8
    }
}

HOOK(0x00403690, "Game_StartLevel", Game_StartLevel_Entry, &g_origStartLevel);
