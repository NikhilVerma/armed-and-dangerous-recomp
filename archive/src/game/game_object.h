#pragma once
// Top-level game object. The global at 0x005fe490 points at it; Game_StartLevel receives it as `this`.
// Only members used by recovered code are named.

#include "game.h"

struct Game;

struct GameVtbl
{
    void* slots_00[9];
    void(__fastcall* SetBusy)(Game* self, void* edx, int busy);                 // +0x24
    void* slots_28[5];
    void(__fastcall* ConsoleCommand)(Game* self, void* edx, char* cmd, char* args);  // +0x3c
    void(__fastcall* OnLevelLoadFailed)(Game* self, void* edx);                 // +0x40
};

struct Game
{
    GameVtbl* vtbl;                  // +0x000
    uint8_t unknown_004[0x028 - 0x004];
    struct MoviePlayer* moviePlayer; // +0x028 (slot +8: busy?, slot +0x10: close movie)
    uint8_t unknown_02c[0x4b0 - 0x02c];
    float field_4b0;                 // +0x4b0 argument to 0x0040f8c0 when field_a68 is set
    uint8_t unknown_4b4[0x568 - 0x4b4];
    uint8_t shellFlags;              // +0x568 bit1: intro movies and legal text already shown
    uint8_t unknown_569[0x748 - 0x569];
    uint32_t field_748;              // +0x748 when set, only lev_shell pushes the main menu
    uint8_t loadingLevel;            // +0x74c
    uint8_t pendingLevelRequest;     // +0x74d (main loop, 0x00402e9e)
    uint8_t unknown_74e[0x84e - 0x74e];
    wchar_t pendingLevelName[0x104]; // +0x84e
    uint8_t unknown_a56[0xa64 - 0xa56];
    void* levelManager;              // +0xa64
    uint32_t field_a68;              // +0xa68
    uint8_t unknown_a6c[0xa7c - 0xa6c];
    void* field_a7c;                 // +0xa7c
    uint8_t unknown_a80[0xa98 - 0xa80];
    void* postProcess;               // +0xa98 owns the "blur texture"/"afterburn texture" render targets
    uint8_t unknown_a9c[0xab4 - 0xa9c];
    void* ui;                        // +0xab4 screen stack
    void* field_ab8;                 // +0xab8 save-game loader ('-' names)
    uint8_t unknown_abc[0xac8 - 0xabc];
    uint32_t field_ac8[8];           // +0xac8 cleared on level start
    uint8_t unknown_ae8[0xbd0 - 0xae8];
    uint32_t field_bd0[0x70];        // +0xbd0 cleared on level start (0x70 dwords end exactly at +0xd90)
    uint32_t field_d90[0x669];       // +0xd90 cleared on level start (through the global pointer)
};
GAME_STATIC_ASSERT_OFFSET(Game, moviePlayer, 0x028);
GAME_STATIC_ASSERT_OFFSET(Game, postProcess, 0xa98);
GAME_STATIC_ASSERT_OFFSET(Game, shellFlags, 0x568);
GAME_STATIC_ASSERT_OFFSET(Game, loadingLevel, 0x74c);
GAME_STATIC_ASSERT_OFFSET(Game, pendingLevelName, 0x84e);
GAME_STATIC_ASSERT_OFFSET(Game, levelManager, 0xa64);
GAME_STATIC_ASSERT_OFFSET(Game, ui, 0xab4);
GAME_STATIC_ASSERT_OFFSET(Game, field_ac8, 0xac8);
GAME_STATIC_ASSERT_OFFSET(Game, field_bd0, 0xbd0);
GAME_STATIC_ASSERT_OFFSET(Game, field_d90, 0xd90);

inline Game*& g_game = GameGlobal<Game*>(0x005fe490);

// Start-up behaviour. Defaults are the original game's; enhancements may change them before the first level
// loads. Recovered code only reads this.
struct StartupOptions
{
    bool playIntroMovies = true;  // push the "PlayMovie" screen on the first front-end load
    bool showLegalText = true;    // push the "LegalText" screen on the first front-end load
    // After the first front-end load, request this level through the main loop's own deferred-load request
    // (Game::pendingLevelRequest/pendingLevelName, serviced at 0x00402e9e). "lev_1_m1" loads a level;
    // "-Game 1" restores that save. Empty: original behaviour.
    wchar_t autoLoad[0x104] = {};
};
extern StartupOptions g_startupOptions;

// 0x00403690. Register ABI: ECX = this, EAX = level name (wide); two stack arguments; returns bool in AL;
// callee pops 8. Called from C++ through Game_StartLevel_Entry's ABI shim.
bool Game_StartLevel(Game* self, const wchar_t* name, uint32_t arg1, uint32_t restoring);
