// Rendering enhancements. Switches in [render] in and.ini, all off by default.
//
//   wireframe=1   draw everything as wireframe
//   soft_particles=1, soft_particles_range=2   (Vulkan; remaster default on) particles fade near the surface
//                 behind them instead of cutting into it (below, and src/rhi/vulkan/vk_device.cpp)
//
// The game has its own debug switch for this: bit 1 of the renderer's option byte (+0x1628) makes
// Renderer_BeginScene (0x005042d0, src/loader/auto_00502cb0.cpp) set D3DRS_FILLMODE to D3DFILL_WIREFRAME
// instead of D3DFILL_SOLID. Nothing in the shipped game sets the bit; this sets it before each scene and leaves
// the rest to the recovered code.
#include "config.h"
#include "game.h"
#include "game/camera.h"
#include "game/game_object.h"
#include "gen/orig_calls.h"
#include "hooks.h"
#include "renderer/scene_pass.h"
#include "log.h"
#include "settings.h"
#include "rhi/rhi.h"

#include <cstring>

static void* g_nextBeginScene;

// Soft particles (Vulkan backend, [render] soft_particles; src/rhi/vulkan/vk_device.cpp SoftParticleMode): the
// backend fades only draws made while rhi::Hints().particlePass is set. The scene pass (ScenePass_Render,
// src/renderer/scene_pass.cpp, flag 0x20) draws the particle pools with Fx_DrawParticles (0x00423820) and the
// streamers with Streamers_Draw (0x00494f80); both are wrapped to set the flag around the call. The phase decides,
// not the render states alone: decals, the game's shadow decals and the translucent layers are also blended without
// depth writes, and a decal lies on its surface, so it would fade out entirely.
static bool* g_particlePass;
static void* g_nextFxDrawParticles;
static void* g_nextStreamersDraw;

// Same registers as the original (edi = fx manager; stack: dt, callee pops 4); every register kept.
static __declspec(naked) void Render_FxDrawParticles()
{
    __asm {
        push eax
        mov eax, g_particlePass
        mov byte ptr [eax], 1
        pop eax
        push dword ptr [esp + 4]
        call dword ptr [g_nextFxDrawParticles]
        push ecx
        mov ecx, g_particlePass
        mov byte ptr [ecx], 0
        pop ecx
        ret 4
    }
}

// Same registers as the original (edx = streamer manager; no stack arguments).
static __declspec(naked) void Render_StreamersDraw()
{
    __asm {
        push eax
        mov eax, g_particlePass
        mov byte ptr [eax], 1
        pop eax
        call dword ptr [g_nextStreamersDraw]
        push ecx
        mov ecx, g_particlePass
        mov byte ptr [ecx], 0
        pop ecx
        ret
    }
}

// Draw distance: the game's far scale (0.25 .. 1.0 from its option) replaced by this value; 1.0 is the game's own
// maximum, above it the far plane and the fog range move further out.
static float g_drawDistance = 1.0f;
static float FarScale(float)
{
    return g_drawDistance;
}
static void ApplyDrawDistance()
{
    if (g_game && Field<void*>(g_game, 0xa68))  // re-run the game's setter on the live camera
        orig::fn_0040f8c0(Field<uint32_t>(g_game, 0xa68), Field<uint32_t>(g_game, 0x4b0));  // edx = camera
}

// Same registers as the original (ecx = renderer, nothing else read or changed besides flags).
static __declspec(naked) void Render_BeginScene_Wireframe()
{
    __asm {
        or byte ptr [ecx + 0x1628], 2
        jmp dword ptr [g_nextBeginScene]
    }
}

void RenderInit()
{
    // Vulkan backend switches (src/rhi/vulkan), read when the device starts.
    using S = RemasterSetting;
    RemasterRegister({"render", "tessellation", L"Tessellated characters (Vulkan)", S::Toggle, 0, 1, 1, {}, 0, true,
                      nullptr},
                     0, 1);
    RemasterRegister({"render", "anisotropy", L"Anisotropic filtering (Vulkan)", S::Slider, 0, 16, 4, {}, 0, true,
                      nullptr},
                     0, 16);
    RemasterRegister({"render", "tonemap_aces", L"Filmic tone mapping (Vulkan)", S::Toggle, 0, 1, 1, {}, 0, true,
                      nullptr},
                     0);
    // No level of detail: every object always draws its finest mesh (Object_SelectLod at distance 0).
    g_lodForceFinest = RemasterRegister({"render", "lod_max", L"Always highest detail", S::Toggle, 0, 1, 1, {}, 0,
                                         false, [](float v) { g_lodForceFinest = v != 0.0f; }},
                                        0, 1) != 0.0f;
    // Draw distance (far plane and fog range scale): the original follows the game's option; the remaster pins it
    // to the game's maximum (1.0) and allows up to 4x.
    const float dd = RemasterRegister({"render", "draw_distance", L"Draw distance", S::Slider, 1.0f, 4.0f, 0.25f, {}, 0,
                                       false,
                                       [](float v) {
                                           g_drawDistance = v;
                                           g_cameraFarScaleAdjust = v > 0.0f ? FarScale : nullptr;
                                           ApplyDrawDistance();
                                       }},
                                      0.0f, 1.0f);
    g_drawDistance = dd;
    g_cameraFarScaleAdjust = dd > 0.0f ? FarScale : nullptr;
    Log("render: lod_max %d, draw_distance %.2f", g_lodForceFinest ? 1 : 0, dd);
    // Soft particles: remaster default on (Vulkan only; the D3D9 backend ignores the hint).
    rhi::Hints().softParticles =
        RemasterRegister({"render", "soft_particles", L"Soft particles (VK)", S::Toggle, 0, 1, 1, {}, 0, false,
                          [](float v) { rhi::Hints().softParticles = v != 0.0f; }},
                         0, 1) != 0.0f;
    rhi::Hints().softParticlesRange =
        RemasterRegister({"render", "soft_particles_range", L"Soft particle fade depth", S::Slider, 0.5f, 16.0f, 0.5f,
                          {}, 0, false, [](float v) { rhi::Hints().softParticlesRange = v; }},
                         2.0f);  // tuned on lev_1_m1: 4 faded most of the snow puff at the feet
    char backend[16];
    ConfigString("render", "backend", "d3d9", backend, sizeof(backend));
    if (ConfigInt("render", "rhi", 0) && !_stricmp(backend, "vulkan"))
    {
        g_particlePass = &rhi::Hints().particlePass;
        HookWrap("Fx_DrawParticles", reinterpret_cast<void*>(Render_FxDrawParticles), &g_nextFxDrawParticles);
        HookWrap("Streamers_Draw", reinterpret_cast<void*>(Render_StreamersDraw), &g_nextStreamersDraw);
    }
    Log("render: soft_particles %d (range %.1f)", rhi::Hints().softParticles ? 1 : 0,
        rhi::Hints().softParticlesRange);
    if (ConfigInt("render", "wireframe", 0))
    {
        HookWrap("Renderer_BeginScene", reinterpret_cast<void*>(Render_BeginScene_Wireframe), &g_nextBeginScene);
        Log("render: wireframe on");
    }
}
