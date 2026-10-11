// Weather (evidence: docs/subsystems/weather.md). Switches in [render] in and.ini; remaster default on.
//
//   snow=1             GPU snowfall (Vulkan, vk_snow.h) in levels whose camera fx attach the snow cylinders
//                      ("fxo_snow", "fxo_snow_2": textured meshes scrolling around the camera, whose innermost layer
//                      crossing the near plane makes the "snow bubbles"); those meshes are made invisible with their
//                      own virtual +0xf4, as the fx "invisible" opcode does, so the fx script and the level's
//                      camera path run unchanged.
//   snow_amount=1500000 flakes in the volume around the camera (it fills the visible world, 150 units sideways)
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
#include "gen/orig_calls.h"
#include "hooks.h"
#include "log.h"
#include "rhi/rhi.h"
#include "settings.h"

namespace
{
bool g_enabled, g_force, g_levelSnow, g_fxLog;

void OnMeshAttached(void* object, const char* name)
{
    if (g_fxLog && name)
        Log("weather: fx mesh %s attached (object %p)", name, object);
    if (!g_enabled || !object || !name || _strnicmp(name, "fxo_snow", 8) != 0)
        return;
    VCall<void>(object, 0xf4 / 4, 0u);  // set visible = 0, as the fx code does (0x0041fe2c, 0x004245f2: push 0)
    if (!g_levelSnow)
        Log("weather: %s hidden; GPU snowfall on", name);
    g_levelSnow = true;
}
// Fx objects from every creation path. A level entered from a saved game recreates its fx objects through the
// object factory (0x0045f020, 12 callers) rather than fx opcode 0x13, so Fx_AttachMesh never sees them. Every fx
// object (class vtable 0x005a52d8) runs its update through vtable slot +0xbc (0x0041a350; thiscall, ecx = object,
// no stack arguments: the ABI tool's 16 are its aligned locals). A trampoline in that slot looks at the object's
// name (0x00453270, eax = object) and parent (+0x34, stored by the attach function 0x004539f0 at 0x00453a02):
//   - snow meshes ("fxo_snow*"): made invisible once, then updated as usual
//   - the camera-locked particle emitter: an invisible "fxo_glow_red" attached to a camera_fx object (vtable
//     0x005ad058; fx_camera_l1_m1_locked) whose particles fly along the view towards the camera: the on-screen
//     "snow bubbles". Its script sets no lifetime, so it lives on (and is restored by saved games). Its update,
//     which emits, is skipped; particles already in the shared pool finish their own short lives. Footstep puffs
//     use the same carrier name attached to characters and are left alone.
constexpr uint32_t kFxVtable = 0x005a52d8, kFxUpdate = 0x0041a350, kCameraFxVtable = 0x005ad058;
void* g_hidden[64];
unsigned g_hiddenCount;
void* g_muted[16];
unsigned g_mutedCount;
uint32_t g_skipUpdate;  // set by FxUpdateCheck for the stub

bool Listed(void* const* list, unsigned count, void* obj)
{
    for (unsigned i = 0; i < count; ++i)
        if (list[i] == obj)
            return true;
    return false;
}

extern "C" void __cdecl FxUpdateCheck(void* obj)
{
    g_skipUpdate = 0;
    if (Listed(g_muted, g_mutedCount, obj))
    {
        g_skipUpdate = 1;
        return;
    }
    if (Listed(g_hidden, g_hiddenCount, obj))
        return;
    const char* name = reinterpret_cast<const char*>(orig::fn_00453270(reinterpret_cast<uint32_t>(obj)));
    if (!name)
        return;
    if (!_strnicmp(name, "fxo_snow", 8))
    {
        if (g_hiddenCount < 64)
            g_hidden[g_hiddenCount++] = obj;
        OnMeshAttached(obj, name);
        return;
    }
    if (!_stricmp(name, "fxo_glow_red"))
    {
        void* parent = Field<void*>(obj, 0x34);
        if (parent && *static_cast<uint32_t*>(parent) == kCameraFxVtable && g_mutedCount < 16)
        {
            g_muted[g_mutedCount++] = obj;
            g_skipUpdate = 1;
            Log("weather: camera-locked particle emitter %p muted (the on-screen snow bubbles)", obj);
        }
    }
}

__declspec(naked) void FxUpdateStub()
{
    __asm {
        pushad
        push ecx
        call FxUpdateCheck
        add esp, 4
        popad
        cmp g_skipUpdate, 0
        jne skip
        mov eax, kFxUpdate
        jmp eax
    skip:
        ret
    }
}}  // namespace

// Once per frame before the scene ends (hook/enhance/shadows.cpp): whether this frame snows, and the snow clock.
void WeatherBeforeSceneEnd()
{
    rhi::SceneHints& h = rhi::Hints();
    if (g_game && g_game->loadingLevel)
    {
        g_levelSnow = false;  // a new level decides again
        g_hiddenCount = g_mutedCount = 0;
    }
    h.snowDt = GameGlobal<float>(0x00604a90);
    // Not while the game is paused: the pause menu draws a frozen, blurred copy of the scene, and flakes drawn over
    // it hung in front of the menu (the game clock stops while paused).
    h.snow = g_enabled && (g_levelSnow || g_force) && h.snowDt > 0.0f;
    if (!h.snow)
        h.snowDt = 0.0f;
    if (h.snow)
        h.snowTime += h.snowDt;
    static int n;
    if (g_fxLog && h.snow && (n++ % 120) == 0)
        Log("weather: snow dt %.5f time %.2f camera up in world (%.2f %.2f %.2f)", h.snowDt, h.snowTime, h.view[4],
            h.view[5], h.view[6]);
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
        RemasterRegister({"render", "snow_amount", L"Snow amount", S::Slider, 100000, 3000000, 100000, {}, 0, false,
                          [](float v) { rhi::Hints().snowCount = static_cast<uint32_t>(v); }},
                         1500000));
    rhi::Hints().snowDebug =
        RemasterRegister({"render", "snow_debug", L"Snow debug: red flakes", S::Toggle, 0, 1, 1, {}, 0, false,
                          [](float v) { rhi::Hints().snowDebug = v != 0.0f; }},
                         0, 0) != 0.0f;
    g_force = ConfigInt("render", "snow_force", 0) != 0;
    g_fxLog = ConfigInt("render", "fx_log", 0) != 0;  // log every attached fx mesh (finding weather effects)
    if (g_enabled || g_fxLog)
    {
        g_fxMeshAttached = OnMeshAttached;
        g_fxObjectCreated = OnMeshAttached;
    }
    if (g_enabled)
    {
        PatchU32(kFxVtable + 0xbc, kFxUpdate, reinterpret_cast<uint32_t>(&FxUpdateStub), "fx object update (snow meshes)");
    }
    Log("weather: GPU snowfall %s%s", g_enabled ? "on" : "off", g_force ? " (every level)" : "");
}
