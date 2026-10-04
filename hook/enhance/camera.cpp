// Camera enhancements (docs/goals/remaster-basics.md items 1 and 4). Switches in [camera] in and.ini, all off by
// default (the original look).
//
//   hor_plus=1      Hor+ widescreen: keep the vertical field of view and widen the horizontal one to the viewport's
//                   real aspect. The original divides the viewport aspect by Renderer_GetAspect (width / height *
//                   0.75), so every resolution gets the same horizontal FOV and wide screens stretch.
//   letterbox=0     drop the fixed letterbox (the 3D view is drawn at y = 11.25% with height 77.5% at every aspect,
//                   constants 0x005bcf24 / 0x005bcf28, read by the scene render 0x00403050 and 0x00406840). With
//                   hor_plus, views narrower than the letterbox's 1.72 keep its horizontal framing.
//   fov=1.0         field-of-view scale: tan(fov / 2) is multiplied by it (1.15 is about +10 degrees at 60).
//   fov_zoom=0      1: scale zoomed views (sniper and other weapon cameras narrower than the camera's base FOV) too
//
// Evidence and addresses: docs/subsystems/camera.md. The hook point is Camera_SetupProjection (src/game/camera.cpp).
#include <windows.h>

#include <cmath>

#include "config.h"
#include "game.h"
#include "game/camera.h"
#include "game/game_object.h"
#include "hooks.h"
#include "log.h"
#include "settings.h"

namespace
{
struct
{
    bool horPlus = false, letterbox = true, fovZoom = false;
    float fovScale = 1.0f;
} g_cfg;

constexpr float kLetterboxTop = 0.1125f, kLetterboxHeight = 0.775f;  // 0x005bcf24, 0x005bcf28

void Camera_Adjust(void* camera, float* fov, float* aspect)
{
    if (!g_cfg.horPlus && g_cfg.fovScale == 1.0f && g_cfg.letterbox)
        return;  // the original projection, untouched
    float viewportAspect = 0.0f;
    if (g_cfg.horPlus)
    {
        void* renderer = Field<void*>(g_game, 0x38);
        const float* size = VCall<const float*>(renderer, 0x7c / 4);  // viewport width, height (as 0x004f0100)
        if (size && size[1] > 0.0f)
            viewportAspect = size[0] / size[1];
    }
    float base = Field<float>(camera, 0xf0);  // the camera's base FOV; weapon zoom cameras are narrower
    bool zoomed = base > 0.0f && *fov < base * 0.95f;
    float t = std::tan(*fov * 0.5f);
    if (g_cfg.fovScale != 1.0f && (!zoomed || g_cfg.fovZoom))
        t *= g_cfg.fovScale;
    if (viewportAspect > 0.0f)
    {
        // Without the letterbox the view can be narrower than the original 1.72 (e.g. 4:3): keep the original
        // horizontal extent there by widening the vertical FOV (no Vert- crop).
        const float design = (4.0f / 3.0f) / kLetterboxHeight;  // the letterboxed 4:3 view's aspect: 1.72
        if (!g_cfg.letterbox && viewportAspect < design)
            t *= design / viewportAspect;
        *aspect = viewportAspect;
    }
    *fov = 2.0f * std::atan(t);
}

void PatchFloat(unsigned address, float expected, float value, const char* label)
{
    PatchBytes(address, &expected, &value, 4, label);
}

void SetLetterbox(bool on)
{
    if (on == g_cfg.letterbox)
        return;
    // The scene render 0x00403050 reads these per frame, so the change shows on the next frame.
    PatchFloat(0x005bcf24, on ? 0.0f : kLetterboxTop, on ? kLetterboxTop : 0.0f, "camera: letterbox top");
    PatchFloat(0x005bcf28, on ? 1.0f : kLetterboxHeight, on ? kLetterboxHeight : 1.0f, "camera: letterbox height");
    g_cfg.letterbox = on;
}
}  // namespace

void CameraInit()
{
    using S = RemasterSetting;
    g_cfg.horPlus = RemasterRegister({"camera", "hor_plus", L"Widescreen (Hor+)", S::Toggle, 0, 1, 1, {}, 0, false,
                                      [](float v) { g_cfg.horPlus = v != 0.0f; }},
                                     0) != 0.0f;
    bool letterbox = RemasterRegister({"camera", "letterbox", L"Cinematic letterbox", S::Toggle, 0, 1, 1, {}, 0, false,
                                       [](float v) { SetLetterbox(v != 0.0f); }},
                                      1) != 0.0f;
    g_cfg.fovScale = RemasterRegister({"camera", "fov", L"Field of view", S::Slider, 0.75f, 1.75f, 0.05f, {}, 0, false,
                                       [](float v) { g_cfg.fovScale = v; }},
                                      1.0f);
    g_cfg.fovZoom = RemasterRegister({"camera", "fov_zoom", L"Field of view also when zoomed", S::Toggle, 0, 1, 1, {},
                                      0, false, [](float v) { g_cfg.fovZoom = v != 0.0f; }},
                                     0) != 0.0f;
    if (g_cfg.fovScale < 0.5f || g_cfg.fovScale > 2.0f)
        g_cfg.fovScale = 1.0f;
    g_cameraProjectionAdjust = Camera_Adjust;  // returns at once with the original settings
    SetLetterbox(letterbox);
    Log("camera: hor_plus %d letterbox %d fov %.2f fov_zoom %d", g_cfg.horPlus ? 1 : 0, g_cfg.letterbox ? 1 : 0,
        g_cfg.fovScale, g_cfg.fovZoom ? 1 : 0);
}