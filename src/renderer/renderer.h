#pragma once
// Direct3D 9 renderer object (allocated 0x1adc bytes in 0x004fffe0, vtable 0x00589990).
// Only members used by recovered code are named; everything else is padding named by offset.

#include <d3d9.h>

#include "game.h"

constexpr int kRendererMaxModes = 150;  // room between +0x9f4 and +0x1354

struct Renderer
{
    uint8_t unknown_0000[0x9e8];
    uint32_t field_9e8;                          // +0x9e8 set to 0 after enumeration
    uint32_t field_9ec;                          // +0x9ec set to 1 after enumeration
    uint32_t modeCount;                          // +0x9f0
    D3DDISPLAYMODE modes[kRendererMaxModes];     // +0x9f4 sorted by format desc, width, height; RefreshRate 0
    D3DDISPLAYMODE desktopMode;                  // +0x1354 GetAdapterDisplayMode(0)
    uint8_t capsSummary[0x120];                  // +0x1364 filled from D3DCAPS9 by 0x00508370
    uint8_t unknown_1484[0x14c8 - 0x1484];
    IDirect3D9* d3d;                             // +0x14c8
    uint8_t unknown_14cc[0x1adc - 0x14cc];
};
GAME_STATIC_ASSERT_SIZE(Renderer, 0x1adc);
GAME_STATIC_ASSERT_OFFSET(Renderer, modeCount, 0x9f0);
GAME_STATIC_ASSERT_OFFSET(Renderer, modes, 0x9f4);
GAME_STATIC_ASSERT_OFFSET(Renderer, desktopMode, 0x1354);
GAME_STATIC_ASSERT_OFFSET(Renderer, capsSummary, 0x1364);
GAME_STATIC_ASSERT_OFFSET(Renderer, d3d, 0x14c8);

// Which display modes the renderer offers. The defaults are the original game's rules (0x00508870);
// enhancements may widen them before the renderer is created. Recovered code only reads this.
struct DisplayModeFilter
{
    uint32_t minWidth = 640;
    uint32_t maxWidth = 1600;
    bool allowAnyAspect = false;  // original: only 16:9, 4:3 and 5:4
};
extern DisplayModeFilter g_displayModeFilter;

// 0x005089d0, stdcall, ret 4.
void __stdcall Renderer_EnumerateDisplayModes(Renderer* self);
