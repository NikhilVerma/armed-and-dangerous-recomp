// Weather (evidence: docs/subsystems/weather.md). Switches in [render] in and.ini; remaster default on.
//
//   snow=1             GPU snowfall (Vulkan, vk_snow.h) in levels whose camera fx attach the snow cylinders
//                      ("fxo_snow", "fxo_snow_2": textured meshes scrolling around the camera, whose innermost layer
//                      crossing the near plane makes the "snow bubbles"); those meshes are made invisible with their
//                      own virtual +0xf4, as the fx "invisible" opcode does, so the fx script and the level's
//                      camera path run unchanged.
//   snow_amount=60000  flakes around the camera
//   snow_force=1       snowfall in every level (testing)
//
// Hook point: Fx_AttachMesh (src/game/fx.cpp, g_fxMeshAttached). The flakes advance with the game's frame time
// (0x00604a90), so they stop while the game is paused.
#include <windows.h>

#include <cstring>

#include "config.h"
#include "game.h"
#include "game/fx.h"
#include "game/game_object.h"
#include "log.h"
#include "rhi/rhi.h"
#include "settings.h"

namespace
{
bool g_enabled, g_force, g_levelSnow;

void OnMeshAttached(void* object, const char* name)
{
    if (!g_enabled || !object || !name || _strnicmp(name, "fxo_snow", 8) != 0)
        return;
    VCall<void>(object, 0xf4 / 4, 0u);  // set visible = 0, as the fx code does (0x0041fe2c, 0x004245f2: push 0)
    if (!g_levelSnow)
        Log("weather: %s hidden; GPU snowfall on", name);
    g_levelSnow = true;
}
}  // namespace

// Once per frame before the scene ends (hook/enhance/shadows.cpp): whether this frame snows, and the snow clock.
void WeatherBeforeSceneEnd()
{
    rhi::SceneHints& h = rhi::Hints();
    if (g_game && g_game->loadingLevel)
        g_levelSnow = false;  // a new level decides again
    h.snow = g_enabled && (g_levelSnow || g_force);
    if (h.snow)
        h.snowTime += GameGlobal<float>(0x00604a90);
}

void WeatherInit()
{
    using S = RemasterSetting;
    char backend[16];
    ConfigString("render", "backend", "d3d9", backend, sizeof(backend));
    bool vulkan = ConfigInt("render", "rhi", 0) && !_stricmp(backend, "vulkan");
    g_enabled = vulkan && RemasterRegister({"render", "snow", L"GPU snowfall (VK)", S::Toggle, 0, 1, 1, {}, 0, true,
                                            nullptr},
                                           0, 1) != 0.0f;
    rhi::Hints().snowCount = static_cast<uint32_t>(
        RemasterRegister({"render", "snow_amount", L"Snow amount", S::Slider, 5000, 100000, 5000, {}, 0, false,
                          [](float v) { rhi::Hints().snowCount = static_cast<uint32_t>(v); }},
                         60000));
    g_force = ConfigInt("render", "snow_force", 0) != 0;
    if (g_enabled)
        g_fxMeshAttached = OnMeshAttached;
    Log("weather: GPU snowfall %s%s", g_enabled ? "on" : "off", g_force ? " (every level)" : "");
}
