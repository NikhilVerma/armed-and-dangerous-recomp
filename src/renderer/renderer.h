#pragma once
// Direct3D 9 renderer object (allocated 0x1adc bytes in 0x004fffe0, vtable 0x00589990).
// Only members used by recovered code are named; everything else is padding named by offset.

#include <d3d9.h>

#include "game.h"

constexpr int kRendererMaxModes = 150;  // room between +0x9f4 and +0x1354

// Intrusive doubly linked list head; elements embed a ListNode and the list ends when next == head.
struct ListNode
{
    ListNode* next;
    ListNode* prev;
};

struct Renderer
{
    void** vtbl;                                 // +0x000 vtable 0x00589990
    uint8_t unknown_0004[0x50 - 0x04];
    ListNode vertexBufferSets;                   // +0x050 VertexBufferSet::node (object = node - 4)
    ListNode indexBuffers;                       // +0x058 IndexBuffer::node (object = node - 4)
    uint8_t unknown_0060[0x9e8 - 0x60];
    uint32_t field_9e8;                          // +0x9e8 set to 0 after enumeration
    uint32_t field_9ec;                          // +0x9ec set to 1 after enumeration
    uint32_t modeCount;                          // +0x9f0
    D3DDISPLAYMODE modes[kRendererMaxModes];     // +0x9f4 sorted by format desc, width, height; RefreshRate 0
    D3DDISPLAYMODE desktopMode;                  // +0x1354 GetAdapterDisplayMode(0)
    uint8_t capsSummary[0x120];                  // +0x1364 filled from D3DCAPS9 by 0x00508370
    HWND deviceWindow;                           // +0x1484
    D3DPOOL bufferPool;                          // +0x1488 pool for dynamic buffers (0 or 2, see 0x00508e80)
    DWORD bufferUsage;                           // +0x148c usage for dynamic buffers (8 or 0x10)
    D3DPRESENT_PARAMETERS presentParams;         // +0x1490
    IDirect3D9* d3d;                             // +0x14c8
    IDirect3DDevice9* device;                    // +0x14cc
    uint8_t unknown_14d0[0x15fd - 0x14d0];
    uint8_t capsFlags;                           // +0x15fd bit3: depth buffer W, bit5: DEPTHBIAS, bit6: slope bias
    uint8_t unknown_15fe[0x164c - 0x15fe];
    DWORD zEnableState;                          // +0x164c D3DRS_ZENABLE value last set (1 = Z, 2 = W)
    int32_t depthBias;                           // +0x1650
    float depthBiasScale;                        // +0x1654
    uint8_t unknown_1658[0x167c - 0x1658];
    int32_t zEnableRequested;                    // +0x167c argument of the last Renderer_SetZEnable
    uint8_t unknown_1680[0x1adc - 0x1680];
};
GAME_STATIC_ASSERT_SIZE(Renderer, 0x1adc);
GAME_STATIC_ASSERT_OFFSET(Renderer, vertexBufferSets, 0x50);
GAME_STATIC_ASSERT_OFFSET(Renderer, indexBuffers, 0x58);
GAME_STATIC_ASSERT_OFFSET(Renderer, modeCount, 0x9f0);
GAME_STATIC_ASSERT_OFFSET(Renderer, modes, 0x9f4);
GAME_STATIC_ASSERT_OFFSET(Renderer, desktopMode, 0x1354);
GAME_STATIC_ASSERT_OFFSET(Renderer, capsSummary, 0x1364);
GAME_STATIC_ASSERT_OFFSET(Renderer, bufferPool, 0x1488);
GAME_STATIC_ASSERT_OFFSET(Renderer, presentParams, 0x1490);
GAME_STATIC_ASSERT_OFFSET(Renderer, d3d, 0x14c8);
GAME_STATIC_ASSERT_OFFSET(Renderer, device, 0x14cc);
GAME_STATIC_ASSERT_OFFSET(Renderer, capsFlags, 0x15fd);
GAME_STATIC_ASSERT_OFFSET(Renderer, zEnableState, 0x164c);
GAME_STATIC_ASSERT_OFFSET(Renderer, depthBiasScale, 0x1654);
GAME_STATIC_ASSERT_OFFSET(Renderer, zEnableRequested, 0x167c);

// A set of dynamic vertex buffers cycled per frame (gfx\d3d\buffer.cpp).
struct VertexBufferSet
{
    uint32_t field_00;
    ListNode node;                               // +0x04 in Renderer::vertexBufferSets
    Renderer* renderer;                          // +0x0c
    uint8_t unknown_10[0x1c - 0x10];
    uint32_t field_1c;                           // +0x1c reset to 4
    uint32_t field_20;                           // +0x20 reset to 4
    uint32_t field_24;                           // +0x24 reset to 3
    uint8_t unknown_28[0x48 - 0x28];
    uint32_t locked;                             // +0x48
    uint8_t unknown_4c[0x60 - 0x4c];
    uint32_t count;                              // +0x60 buffers in the set
    uint32_t current;                            // +0x64
    uint32_t sizeBytes;                          // +0x68
    uint32_t field_6c;                           // +0x6c reset to 0x800
    IDirect3DVertexBuffer9* buffers[1];          // +0x70 [count]
};
GAME_STATIC_ASSERT_OFFSET(VertexBufferSet, renderer, 0x0c);
GAME_STATIC_ASSERT_OFFSET(VertexBufferSet, locked, 0x48);
GAME_STATIC_ASSERT_OFFSET(VertexBufferSet, buffers, 0x70);

// A dynamic 16-bit index buffer (gfx\d3d\buffer.cpp).
struct IndexBuffer
{
    uint32_t field_00;
    ListNode node;                               // +0x04 in Renderer::indexBuffers
    Renderer* renderer;                          // +0x0c
    uint32_t indexCount;                         // +0x10
    uint8_t unknown_14[0x1c - 0x14];
    uint32_t locked;                             // +0x1c
    IDirect3DIndexBuffer9* buffer;               // +0x20
};
GAME_STATIC_ASSERT_OFFSET(IndexBuffer, indexCount, 0x10);
GAME_STATIC_ASSERT_OFFSET(IndexBuffer, locked, 0x1c);
GAME_STATIC_ASSERT_OFFSET(IndexBuffer, buffer, 0x20);

// Which display modes the renderer offers. The defaults are the original game's rules (0x00508870);
// enhancements may widen them before the renderer is created. Recovered code only reads this.
struct DisplayModeFilter
{
    uint32_t minWidth = 640;
    uint32_t maxWidth = 1600;
    bool allowAnyAspect = false;  // original: only 16:9, 4:3 and 5:4
    // An extra mode (X8R8G8B8) offered besides the adapter's, e.g. a windowed size no display mode has. 0: none.
    uint32_t extraWidth = 0, extraHeight = 0;
};
extern DisplayModeFilter g_displayModeFilter;

// Enhancement point for Renderer_SetGamma (0x005091c0): called with the clamped red, green and blue gamma before the
// device gamma ramp is considered. True: the enhancement applies the gamma itself (e.g. in the present pass, which
// also works in a window); the function then returns 1 without a ramp. Null (the default): original.
extern bool (*g_gammaHandler)(float red, float green, float blue);

// Enhancement point for Renderer_GetPixelWidth / Renderer_GetPixelHeight (0x00506f90 / 0x00506fc0): the numerator
// of 1 / (2 * half size), i.e. a scale on the size of one screen pixel in UI space, which is how text glyphs are
// sized (Renderer_DrawGlyph). {1, 1}: original (x / 1.0 is exact).
extern float g_pixelSizeScale[2];

// 0x005089d0, stdcall, ret 4.
void __stdcall Renderer_EnumerateDisplayModes(Renderer* self);
