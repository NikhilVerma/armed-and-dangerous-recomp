// Shadow maps (Vulkan backend; evidence: docs/subsystems/shadows.md). Switches in [render] in and.ini, off by default.
//
//   shadow_maps=1        the sun casts shadow maps from every opaque scene object (characters, props, buildings,
//                        the landscape) instead of the game's per-caster silhouette decals, which are switched off
//                        (Shadows_Update / Shadows_Draw, src/game/shadows.cpp)
//   shadow_strength=0.55 how dark a full shadow is (0..1)
//   shadow_range=60      half size of the shadowed area around the view, in world units
//   shadow_size=4096     shadow map resolution
//   shadow_softness=0.6  the light's angular radius in degrees (the bare sun is 0.27; haze and the bright snow
//                        sky widen it): penumbrae widen with the occluder's distance, without a ceiling (PCSS)
//
// Each frame, when the UI pass starts (g_uiPassBegin, src/game/ui_pass.cpp) the 3D scene is complete: the camera's
// view (Game+0x704) and the sun (renderer light slot 0, a D3DLIGHT9 at renderer +0x169c, its direction at +0x40;
// the game's own shadows use the same light, falling back to (0, -1, -1)) go to rhi::Hints and rhi::SceneEnd adds
// the passes.
#include <windows.h>

#include <cmath>
#include <cstring>

#include "config.h"
#include "game.h"
#include "game/game_object.h"
#include "game/shadows.h"
#include "game/ui_pass.h"
#include "renderer/scene_pass.h"
#include "hooks.h"
#include "log.h"
#include "rhi/rhi.h"
#include "settings.h"

void WeatherBeforeSceneEnd();  // hook/enhance/weather.cpp

namespace
{
// Camera (Game+0x704) and sun (renderer light slot 0) into rhi::Hints.
void FillHints()
{
    rhi::SceneHints& h = rhi::Hints();
    if (!(h.shadows || h.snow || h.ao || h.fogModern) || !g_game)
        return;
    h.levelLoading = g_game->loadingLevel != 0;
    std::memcpy(h.view, reinterpret_cast<const uint8_t*>(g_game) + 0x704, sizeof(h.view));
    h.haveView = true;
    const uint8_t* renderer = static_cast<const uint8_t*>(Field<void*>(g_game, 0x38));
    float dir[3] = {0.0f, -1.0f, -1.0f};  // the game's fallback (0x00417c1c)
    if (renderer && renderer[0x1a44])
        std::memcpy(dir, renderer + 0x169c + 0x40, sizeof(dir));
    float len = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    if (len > 1e-6f)
    {
        for (int i = 0; i < 3; ++i)
            h.sun[i] = dir[i] / len;
        h.haveSun = true;
    }
    static const bool logSun = ConfigInt("render", "shadow_log", 0) != 0;  // diagnosis
    if (logSun)
        Log("shadows: sun %.3f %.3f %.3f (light 0 enabled %d)", h.sun[0], h.sun[1], h.sun[2],
            renderer ? renderer[0x1a44] : -1);
}

// When the frame's scene starts (Renderer_BeginScene, 0x005042d0: ecx = renderer; called after the game's update,
// so Game+0x704 already holds this frame's camera): the hints are current for passes the backend runs mid-frame
// (ambient occlusion and shadows go in before the first translucent draw, so the translucent layers stay on top).
using BeginScene_t = uint32_t(__fastcall*)(void* renderer, void* edx);
BeginScene_t g_nextBeginScene;
uint32_t __fastcall Shadows_BeginScene(void* renderer, void* edx)
{
    uint32_t r = g_nextBeginScene(renderer, edx);
    FillHints();
    return r;
}

void OnUiPassBegin()
{
    WeatherBeforeSceneEnd();
    FillHints();
    rhi::SceneHints& h = rhi::Hints();
    if (!(h.shadows || h.snow || h.ao || h.fogModern) || !g_game)
        return;
    rhi::SceneEnd();
}

void SetShadows(bool on)
{
    rhi::Hints().shadows = on;
    g_gameShadowsReplaced = on;
}
}  // namespace

void ShadowsInit()
{
    using S = RemasterSetting;
    char backend[16];
    ConfigString("render", "backend", "d3d9", backend, sizeof(backend));
    bool vulkan = ConfigInt("render", "rhi", 0) && !_stricmp(backend, "vulkan");
    bool on = RemasterRegister({"render", "shadow_maps", L"Shadow maps (VK)", S::Toggle, 0, 1, 1, {}, 0, false,
                                [](float v) { SetShadows(v != 0.0f); }},
                               0, 1) != 0.0f;
    rhi::Hints().shadowStrength =
        RemasterRegister({"render", "shadow_strength", L"Shadow darkness", S::Slider, 0.1f, 1.0f, 0.05f, {}, 0, false,
                          [](float v) { rhi::Hints().shadowStrength = v; }},
                         0.55f);
    rhi::Hints().shadowRange =
        RemasterRegister({"render", "shadow_range", L"Shadow distance", S::Slider, 50.0f, 500.0f, 25.0f, {}, 0, false,
                          [](float v) { rhi::Hints().shadowRange = v; }},
                         200.0f);
    rhi::Hints().shadowSoftness =
        RemasterRegister({"render", "shadow_softness", L"Shadow softness", S::Slider, 0.05f, 3.0f, 0.05f, {}, 0, false,
                          [](float v) { rhi::Hints().shadowSoftness = v; }},
                         0.6f);
    // Ambient occlusion (XeGTAO, src/rhi/vulkan/vk_gtao.h): remaster default on.
    bool ao = RemasterRegister({"render", "ao", L"Ambient occlusion (VK)", S::Toggle, 0, 1, 1, {}, 0, false,
                                [](float v) { rhi::Hints().ao = v != 0.0f; }},
                               0, 1) != 0.0f;
    rhi::Hints().ao = ao && vulkan;
    rhi::Hints().aoStrength =
        RemasterRegister({"render", "ao_strength", L"AO strength", S::Slider, 0.0f, 1.0f, 0.05f, {}, 0, false,
                          [](float v) { rhi::Hints().aoStrength = v; }},
                         1.0f);
    rhi::Hints().aoRadius =
        RemasterRegister({"render", "ao_radius", L"AO radius", S::Slider, 0.25f, 4.0f, 0.25f, {}, 0, false,
                          [](float v) { rhi::Hints().aoRadius = v; }},
                         2.5f);
    // Modern fog (exponential height fog, src/rhi/vulkan/vk_fog.h): remaster default on.
    bool fog = RemasterRegister({"render", "fog_modern", L"Height fog (VK)", S::Toggle, 0, 1, 1, {}, 0, false,
                                 [](float v) { rhi::Hints().fogModern = v != 0.0f; }},
                                0, 1) != 0.0f;
    rhi::Hints().fogModern = fog && vulkan;
    rhi::Hints().fogDensity =
        RemasterRegister({"render", "fog_density", L"Fog density", S::Slider, 0.25f, 3.0f, 0.25f, {}, 0, false,
                          [](float v) { rhi::Hints().fogDensity = v; }},
                         1.0f);
    rhi::Hints().fogHeightFalloff =
        RemasterRegister({"render", "fog_height", L"Fog height falloff", S::Slider, 0.0f, 0.01f, 0.0005f, {}, 0, false,
                          [](float v) { rhi::Hints().fogHeightFalloff = v; }},
                         0.002f);
    rhi::Hints().fogSunGlow =
        RemasterRegister({"render", "fog_sun", L"Fog sun glow", S::Slider, 0.0f, 2.0f, 0.1f, {}, 0, false,
                          [](float v) { rhi::Hints().fogSunGlow = v; }},
                         0.6f);
    // Ray-traced shadows (src/rhi/vulkan/vk_rt.h) in place of shadow maps, on GPUs with ray queries: remaster
    // default on.
    rhi::Hints().shadowsRt =
        RemasterRegister({"render", "shadow_rt", L"Ray-traced shadows (VK)", S::Toggle, 0, 1, 1, {}, 0, false,
                          [](float v) { rhi::Hints().shadowsRt = v != 0.0f; }},
                         0, 1) != 0.0f;
    // No frustum culling within this distance of the camera in the main scene pass (ScenePass_Render,
    // src/renderer/scene_pass.cpp): casters behind and beside the view stay in the scene, so their shadows neither
    // pop in nor out. Original 0 (the game's culling), remaster default 400 world units.
    g_sceneCullRadius =
        RemasterRegister({"render", "cull_radius", L"Shadow caster range", S::Slider, 0.0f, 1000.0f, 50.0f, {}, 0,
                          false, [](float v) { g_sceneCullRadius = v, rhi::Hints().shadowCasterCache = v <= 0.0f; }},
                         0.0f, 400.0f);  // 2x the default shadow range: low sun, long shadows (100 still popped)
    rhi::Hints().shadowCasterCache = g_sceneCullRadius <= 0.0f;
    rhi::Hints().shadowRays = static_cast<uint32_t>(ConfigInt("render", "shadow_rays", 8));  // and.ini only
    int size = ConfigInt("render", "shadow_size", 4096);
    rhi::Hints().shadowSize = static_cast<uint32_t>(size < 512 ? 512 : size > 8192 ? 8192 : size);
    g_uiPassBegin = OnUiPassBegin;  // does nothing while shadow maps and snow are off
    HookWrap("Renderer_BeginScene", reinterpret_cast<void*>(Shadows_BeginScene),
             reinterpret_cast<void**>(&g_nextBeginScene));
    SetShadows(on && vulkan);
    Log("shadows: shadow maps %s (strength %.2f, range %.0f, %u)", on && vulkan ? "on" : "off",
        rhi::Hints().shadowStrength, rhi::Hints().shadowRange, rhi::Hints().shadowSize);
}
