// UI at any aspect ratio (docs/goals/remaster-basics.md item 1; evidence: docs/subsystems/ui.md). Switches in [ui]
// in and.ini, all off by default.
//
//   anchor=1         keep the UI at its 4:3 proportions on wider screens: HUD elements pinned to the left or right
//                    edge or centred (by their layout reference, see kAnchors), menu pages centred (pillarbox).
//                    The original stretches the whole 4:3 layout across the screen.
//   text_scale=1.0   UI text size multiplier
//   text_relative=1  text keeps the size it has in the 640x480 layout at every resolution (the game sizes glyphs in
//                    screen pixels, so at 3840x2160 its text is a quarter of its 640x480 size); text_scale on top
//   log_objects=N    log every UI object drawn in presented frame N (page, ref, flags, positions) to and.log
//
// How: every UI object is drawn by the UI pass (UiPass_Draw, src/game/ui_pass.cpp) in a normalised screen space set by
// 0x004f01c0 (x in 0..1 across the viewport). For each object the view matrix gets an extra x mapping
// x_ndc' = k * x_ndc + c (k = (4/3) / aspect), applied in view space as x' = k x - c z so that UI meshes drawn with
// depth (the projection is perspective, w = -z) move the same way; the pass's own matrices are restored after it.
#include <windows.h>

#include <algorithm>
#include <cstring>

#include "config.h"
#include "game.h"
#include "game/game_object.h"
#include "game/ui_pass.h"
#include "log.h"
#include "renderer/renderer.h"
#include "settings.h"

unsigned PresentedFrames();  // hook/src/d3d9_hooks.cpp

namespace
{
int g_logFrame = -1;
bool g_anchor;

enum Anchor : uint8_t { kCentre, kLeft, kRight, kStretch };

// HUD layout references (UI bank ref list, docs/formats/big-ui.md; names from lev_1_m1_big12_ui.big) and where each
// HUD element goes on a wide screen. Unlisted references are centred; texts without a known reference use their
// position (left third, middle, right third).
Anchor HudAnchor(int ref)
{
    switch (ref)
    {
    case 2: case 3: case 4: case 5: case 6: case 7: case 8: case 9:  // enemy/team health, peasants, team text, ...
    case 21: case 22: case 23: case 24: case 38:                     // chat lines
    case 28: case 40:                                                // player health, health pool
        return kLeft;
    case 1: case 12: case 13: case 14: case 15:  // weapon background, weapon select arrows
    case 25: case 26: case 27: case 30:          // compass, objective pointer, weapon, grenade
    case 36: case 37:                            // ammo and grenade counts
        return kRight;
    case 17: case 18: case 19: case 20:  // hit flashes along the screen edges
        return kStretch;
    default:
        return kCentre;
    }
}

float g_textScale = 1.0f;
bool g_textRelative;

// The numerator of the screen-pixel size (g_pixelSizeScale, Renderer_GetPixelWidth/Height): text_scale, times the
// viewport height / 480 when relative, so one glyph pixel is 1/480 of the screen height as at 640x480.
// fixedLayout: text placed on the game's 640-wide layout grid (menu pages, mission objectives, placed texts). Its
// line positions do not move with the glyph size, so a text_scale above 1 would push lines out of the 4:3 frame
// (measured: objectives cut off at text_scale 1.25); such text keeps the resolution-relative size only.
float TextScale(bool fixedLayout = false)
{
    float s = fixedLayout ? std::min(g_textScale, 1.0f) : g_textScale;
    if (g_textRelative && g_game)
        if (void* renderer = Field<void*>(g_game, 0x38))
        {
            const float* viewport = VCall<const float*>(renderer, 0x7c / 4);  // half width, half height, centre
            if (viewport && viewport[1] > 0.0f)
                s *= (viewport[1] * 2.0f) / 480.0f;
        }
    return s;
}

void ApplyTextScale()
{
    g_pixelSizeScale[0] = g_pixelSizeScale[1] = TextScale();
}
uint8_t g_saved[0x44];  // the pass's view matrix while an object draws with its own
bool g_changed;
bool g_fixedLayout;  // the object being drawn uses the fixed layout (TextScale)

void LogObject(void* o)
{
    if (static_cast<int>(PresentedFrames()) != g_logFrame)
        return;
    Log("ui: obj %p vt %08x page %d ref %d id %d flags474 %08x f470 %02x pos %.3f %.3f align %.3f %.3f", o,
        Field<uint32_t>(o, 0), Field<int32_t>(o, 0x46c), Field<int32_t>(o, 0x468), Field<int32_t>(o, 0x478),
        Field<uint32_t>(o, 0x474), Field<uint8_t>(o, 0x470), Field<float>(o, 0x4e8), Field<float>(o, 0x4ec),
        Field<float>(o, 0x4f0), Field<float>(o, 0x4f4));
}

void BeginDraw(void* o)
{
    if (g_logFrame >= 0)
        LogObject(o);
    g_changed = false;
    bool fixedLayout = Field<int32_t>(o, 0x46c) != 0 || (Field<uint32_t>(o, 0x474) & 0x2000) != 0;
    g_fixedLayout = fixedLayout;
    g_pixelSizeScale[0] = g_pixelSizeScale[1] = TextScale(fixedLayout);
    if (!g_anchor)
        return;
    void* renderer = Field<void*>(g_game, 0x38);
    const float* viewport = VCall<const float*>(renderer, 0x7c / 4);  // half width, half height, centre x, y
    if (!viewport || viewport[1] <= 0.0f)
        return;
    float k = (4.0f / 3.0f) / (viewport[0] / viewport[1]);
    if (k >= 0.999f)
        return;  // 4:3 or narrower: the original layout
    Anchor a;
    if (Field<int32_t>(o, 0x46c) != 0)
        a = kCentre;  // menu and shell pages
    else
    {
        a = HudAnchor(Field<int32_t>(o, 0x468));
        bool placed = (Field<uint32_t>(o, 0x474) & 0x2000) != 0;  // text with a layout position (+0x4e8)
        if (a == kCentre && placed && Field<int32_t>(o, 0x468) < 0)
        {
            float x = Field<float>(o, 0x4e8);
            a = x < 1.0f / 3.0f ? kLeft : x > 2.0f / 3.0f ? kRight : kCentre;
        }
    }
    if (a == kStretch)
        return;
    // Screen x in 0..1: left a = 0, centre (1 - k) / 2, right 1 - k. In NDC: x' = k x + (2a + k - 1).
    float shift = a == kLeft ? 0.0f : a == kRight ? 1.0f - k : (1.0f - k) * 0.5f;
    float c = 2.0f * shift + k - 1.0f;
    const uint8_t* view = VCall<const uint8_t*>(renderer, 0x64 / 4);
    if (!view)
        return;
    std::memcpy(g_saved, view, sizeof(g_saved));
    // The game's matrices hold one output coordinate per 4 floats (x_view = v[0] x + v[1] y + v[2] z + v[3]; measured:
    // the column form scaled the pre-view 0..1 x instead). After the view: x' = k x_view - c z_view (z_view < 0 in
    // front, w = -z_view), i.e. row 0 = k row 0 - c row 2.
    float v[16], m[16];
    std::memcpy(v, view, 64);
    std::memcpy(m, v, 64);
    for (int i = 0; i < 4; ++i)
        m[i] = v[i] * k - v[8 + i] * c;
    uint8_t changed[0x44];
    std::memcpy(changed, g_saved, sizeof(changed));
    std::memcpy(changed, m, 64);
    // Flags word (+0x40, read by the game's matrix routines 0x004e2910 / 0x004e2b40): bit 0x20 = general 4x4 and
    // the low bits describe the 3x3 part; the screen view is flagged as a plain scale, which would drop the
    // x-from-z term. Mark the result general.
    Field<uint32_t>(changed, 0x40) |= 0x2f;
    VCall<void>(renderer, 0x50 / 4, changed);
    // Text drawn by the object is squeezed by k with everything else; glyphs keep their proportions with 1 / k.
    g_pixelSizeScale[0] = TextScale(fixedLayout) / k;
    g_changed = true;
}

void EndDraw(void*)
{
    if (g_fixedLayout)
        ApplyTextScale();
    g_fixedLayout = false;
    if (!g_changed)
        return;
    VCall<void>(Field<void*>(g_game, 0x38), 0x50 / 4, g_saved);
    g_pixelSizeScale[0] = TextScale();
    g_changed = false;
}
}  // namespace

// Once per frame: text drawn outside the UI pass (loading screens, subtitles, fades) uses the same scale.
void UiAspectOnFrame()
{
    if (g_textRelative || g_textScale != 1.0f)
        ApplyTextScale();
}

void UiAspectInit()
{
    g_logFrame = ConfigInt("ui", "log_objects", -1);
    g_anchor = RemasterRegister({"ui", "anchor", L"HUD and menus at 4:3 proportions", RemasterSetting::Toggle, 0, 1, 1,
                                 {}, 0, false, [](float v) { g_anchor = v != 0.0f; }},
                                0, 1) != 0.0f;
    g_textScale = RemasterRegister({"ui", "text_scale", L"Text size", RemasterSetting::Slider, 1.0f, 2.5f, 0.25f, {}, 0,
                                    false,
                                    [](float v) {
                                        g_textScale = v;
                                        ApplyTextScale();
                                    }},
                                   1.0f);
    g_textRelative = RemasterRegister({"ui", "text_relative", L"Text follows resolution", RemasterSetting::Toggle, 0, 1,
                                       1, {}, 0, false,
                                       [](float v) {
                                           g_textRelative = v != 0.0f;
                                           ApplyTextScale();
                                       }},
                                      0, 1) != 0.0f;
    g_pixelSizeScale[0] = g_pixelSizeScale[1] = g_textScale;  // the renderer does not exist yet
    g_uiObjectBeginDraw = BeginDraw;  // both return at once with the original settings
    g_uiObjectEndDraw = EndDraw;
    Log("ui: anchor %d", g_anchor ? 1 : 0);
}
