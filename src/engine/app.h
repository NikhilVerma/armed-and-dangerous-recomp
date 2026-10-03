#pragma once
// Application object and its start-up configuration.
// Evidence: App_Init (0x004e3fc0) and the virtual init it calls (vtable slot 1, 0x004e03e0).

#include "game.h"

// Start-up configuration, 0x58 bytes, copied by value in App_Init. `present` says which blocks the caller
// filled; App_Init supplies defaults for the rest. Field meanings beyond the display block are not yet known.
struct AppConfig
{
    uint32_t present;       // +0x00 bit0 display block, bit1 block at +0x4c, bit2 block at +0x1c
    uint32_t displayFlags;  // +0x04 default 3; bit0 creates the renderer (0x004e03e0)
    int32_t width;          // +0x08 default 640
    int32_t height;         // +0x0c default 480
    int32_t bpp;            // +0x10 default 32
    uint32_t field_14;      // +0x14 -> App::field_0c
    uint32_t field_18;      // +0x18 -> App::field_10
    uint8_t enable_1c;      // +0x1c default 1; gates subsystem 0x004f9c10 in 0x004e03e0
    uint8_t pad_1d[3];
    int32_t field_20;       // +0x20 default 10; first of ten arguments to that subsystem
    uint32_t field_24;      // +0x24 default 0x3e9c0ec1 (float 0.30479)
    uint32_t field_28[8];   // +0x28 remaining arguments to that subsystem
    uint32_t field_48;      // +0x48 -> App::field_08
    uint8_t enable_4c;      // +0x4c default 1; gates subsystem 0x004f7b30 in 0x004e03e0
    uint8_t pad_4d[3];
    int32_t field_50;       // +0x50 default 10
    uint8_t field_54;       // +0x54 default 0
    uint8_t pad_55[3];
};
GAME_STATIC_ASSERT_SIZE(AppConfig, 0x58);
GAME_STATIC_ASSERT_OFFSET(AppConfig, enable_1c, 0x1c);
GAME_STATIC_ASSERT_OFFSET(AppConfig, field_48, 0x48);
GAME_STATIC_ASSERT_OFFSET(AppConfig, field_54, 0x54);

struct App;

struct AppVtbl
{
    void* slot0;
    // slot 1, 0x004e03e0: creates renderer and other subsystems from the configuration. thiscall, ret 0xc.
    void(__fastcall* Setup)(App* self, void* edx, AppConfig* config, uint32_t arg2, uint32_t arg3);
};

// Only the members App_Init touches; size unknown.
struct App
{
    AppVtbl* vtbl;          // +0x00
    uint32_t field_04;
    uint32_t field_08;      // +0x08 <- AppConfig::field_48
    uint32_t field_0c;      // +0x0c <- AppConfig::field_14
    uint32_t field_10;      // +0x10 <- AppConfig::field_18
    uint32_t displayFlags;  // +0x14 <- AppConfig::displayFlags
    uint8_t unknown_18[0x34 - 0x18];
    void* object_34;        // +0x34 <- 0x004e43f0()
    uint8_t unknown_38[0x60 - 0x38];
    void* object_60;        // +0x60 <- 0x004fbab0()
};
GAME_STATIC_ASSERT_OFFSET(App, displayFlags, 0x14);
GAME_STATIC_ASSERT_OFFSET(App, object_34, 0x34);
GAME_STATIC_ASSERT_OFFSET(App, object_60, 0x60);

// 0x004e3fc0, thiscall, ret 0xc.
void __fastcall App_Init(App* self, void* edx, const AppConfig* config, uint32_t arg2, uint32_t arg3);
