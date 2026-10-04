// Shadow maps (Vulkan backend; evidence: docs/subsystems/shadows.md). Switches in [render] in and.ini, off by default.
//
//   shadow_maps=1        the sun casts shadow maps from every opaque scene object (characters, props, buildings,
//                        the landscape) instead of the game's per-caster silhouette decals, which are switched off
//                        (Shadows_Update / Shadows_Draw, src/game/shadows.cpp)
//   shadow_strength=0.55 how dark a full shadow is (0..1)
//   shadow_range=60      half size of the shadowed area around the view, in world units
//   shadow_size=4096     shadow map resolution
//   shadow_softness=1.5  the sun's angular radius in degrees: soft shadows widen with the caster's distance (PCSS)
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
#include "log.h"
#include "rhi/rhi.h"
#include "settings.h"

void WeatherBeforeSceneEnd();  // hook/enhance/weather.cpp

namespace
{
void OnUiPassBegin()
{
    rhi::SceneHints& h = rhi::Hints();
    WeatherBeforeSceneEnd();
    if (!(h.shadows || h.snow) || !g_game)
        return;
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
        RemasterRegister({"render", "shadow_range", L"Shadow distance", S::Slider, 20.0f, 200.0f, 10.0f, {}, 0, false,
                          [](float v) { rhi::Hints().shadowRange = v; }},
                         60.0f);
    rhi::Hints().shadowSoftness =
        RemasterRegister({"render", "shadow_softness", L"Shadow softness", S::Slider, 0.0f, 5.0f, 0.25f, {}, 0, false,
                          [](float v) { rhi::Hints().shadowSoftness = v; }},
                         1.5f);
    int size = ConfigInt("render", "shadow_size", 4096);
    rhi::Hints().shadowSize = static_cast<uint32_t>(size < 512 ? 512 : size > 8192 ? 8192 : size);
    g_uiPassBegin = OnUiPassBegin;  // does nothing while shadow maps and snow are off
    SetShadows(on && vulkan);
    Log("shadows: shadow maps %s (strength %.2f, range %.0f, %u)", on && vulkan ? "on" : "off",
        rhi::Hints().shadowStrength, rhi::Hints().shadowRange, rhi::Hints().shadowSize);
}
