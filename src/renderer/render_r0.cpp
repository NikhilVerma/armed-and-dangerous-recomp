// Recovered: game-side rendering, address range 0x00401070-0x00417f20 (batch r0). Static recovery 2026-10-05,
// compile-checked only (the game was not run); the oracle (tools/compare.py) is still to do.
//   Frame and HUD: 0x00403050 Game_RenderFrame, 0x00406840 Hud_ProjectMarkers, 0x00408390 HudFrustum_Build,
//     0x0040f750, 0x0040f970, 0x00410150, 0x004102e0, 0x00410d40, 0x00410fa0 (fn_ names), 0x00415dc0 RenderPass_Begin
//   Decals: 0x00415eb0/0x00416010 polygon clipping, 0x00416050..0x00416290 manager, 0x00416330 Decals_Draw,
//     0x004165a0..0x00417180 decal objects and geometry, 0x00417260 Decal_Build, 0x004175c0 texture coordinates
//   Shadows: 0x00416530 Shadows_Attach, 0x00417990..0x00417b90 shadow objects, 0x00417c00 light camera,
//     0x00417e90 Shadow_DrawCaster, 0x00417f20 Shadow_RenderTexture
// Not recovered here (see the batch report): 0x00401070/0x004010c0 (device lost/restored handlers) and the other
// unexecuted functions without callers, 0x00416730 (no ret), 0x00415b40 (string helper), 0x004101a0 (entity init).
#include <cmath>
#include <cstring>

#include "game.h"
#include "game/game_object.h"
#include "gen/entry_stubs.h"
#include "gen/orig_calls.h"
#include "hooks.h"

namespace
{
template <class T>
uint32_t u(T* p)
{
    return reinterpret_cast<uint32_t>(p);
}
inline void* P(uint32_t v)
{
    return reinterpret_cast<void*>(v);
}
inline void* Renderer()
{
    return Field<void*>(g_game, 0x38);
}
}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// 0x00403050 (ebx = game; caller: the main loop 0x00402e80, once per frame). The frame:
//  - frame time = [0x00604a90], read first; 0x00410d40; game virtual +0x14 (begin?); 0x00418880 (eax: al cleared);
//  - renderer virtual +0xf4 (0); the current viewport (renderer virtual +0x7c, 16 bytes) is saved, then
//    renderer virtual +0x70 (0, h * 0.1125, (float)w, 0.775 * h) with w/h the ints at game +0x4c/+0x50 (constants
//    0x005bcf24 and 0x005bcf28): the 3D view area below the HUD band (guess);
//  - Camera_SetupProjection (0x0040f6e0, edi = Game+0xa68);
//  - when 0x00490a30 (eax = Game+0xab4, the screen stack) says so: Decals_Update (0x00416440, eax = Game+0xa7c;
//    frame time, 0) and the main scene 0x00489610 (eax = Decals_Update's result with al cleared, edx = Game+0xa78;
//    stack: frame time), which runs ScenePass_Render(Game+0xa78, -1, frame time, eax);
//  - the viewport is restored (renderer virtual +0x74), UiPass_Draw (0x00464460, Game+0xa70), 0x00491670 (esi =
//    Game+0xab4), game virtual +0x18 (end?), renderer virtual +0x108 (present?), whose result is returned.
// ebx is only read (the game object), never written; 0x00489610 does not read it.
uint32_t Game_RenderFrame(void* game)
{
    const uint32_t frameTime = GameGlobal<uint32_t>(0x00604a90);
    orig::fn_00410d40();
    const uint32_t r14 = VCall<uint32_t>(game, 0x14 / 4);
    orig::fn_00418880(r14 & 0xffffff00u);
    VCall<void>(Field<void*>(game, 0x38), 0xf4 / 4, 0u);
    const uint32_t* vp = VCall<const uint32_t*>(Field<void*>(game, 0x38), 0x7c / 4);
    uint32_t savedViewport[4];
    const float h = static_cast<float>(Field<int32_t>(game, 0x50));
    const float bottom = GameGlobal<float>(0x005bcf28) * h;
    savedViewport[0] = vp[0];
    savedViewport[1] = vp[1];
    savedViewport[2] = vp[2];
    const float w = static_cast<float>(Field<int32_t>(game, 0x4c));
    savedViewport[3] = vp[3];
    const float top = h * GameGlobal<float>(0x005bcf24);
    VCall<void>(Field<void*>(game, 0x38), 0x70 / 4, 0u, top, w, bottom);
    orig::fn_0040f6e0(Field<uint32_t>(game, 0xa68));
    if (orig::fn_00490a30(Field<uint32_t>(game, 0xab4)) & 0xff)
    {
        const uint32_t r = orig::fn_00416440(Field<uint32_t>(game, 0xa7c), 0 /* ecx: not read */, frameTime, 0);
        orig::fn_00489610(r & 0xffffff00u, Field<uint32_t>(game, 0xa78), frameTime);
    }
    VCall<void>(Field<void*>(game, 0x38), 0x74 / 4, static_cast<void*>(savedViewport));
    orig::fn_00464460(Field<uint32_t>(game, 0xa70));
    orig::fn_00491670(Field<uint32_t>(game, 0xab4));
    VCall<void>(game, 0x18 / 4);
    return VCall<uint32_t>(Field<void*>(game, 0x38), 0x108 / 4);
}

static uint32_t Game_RenderFrame_Abi(uint32_t ebx)
{
    return Game_RenderFrame(P(ebx));
}
ABI_ENTRY_00403050(Game_RenderFrame_Entry, Game_RenderFrame_Abi)
HOOK(0x00403050, "Game_RenderFrame", Game_RenderFrame_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// 0x00406840 (stdcall, 1 arg: the HUD marker block Game+0xa6c; caller ScenePass_Render flag 0x80).
// Projects world points to screen and shows or hides HUD sprites (bit 2 of sprite +0x474):
//  - point slots of 0x2c bytes: { +0 active, +4 world point, +0x10 screen x (copied), +0x14 screen y, +0x18 0 }.
//    Projection 0x004f02f0 (ebx = out {x, y, 0}, esi = renderer, edi = point); y is then remapped into the 3D view
//    area: y * 0.775 + 0.1125 (0x005bcf28, 0x005bcf24; the viewport set by 0x00403050).
//    Slots: 4 at +0x154 when byte +0x152, otherwise the one at +0; the one at +0x84 when byte +0x151; 6 at +0x204
//    when byte Game+0xcbc.
//  - four sprites from Game+0xa70 (0x004643b0 copies 4 pointers). A sprite is shown when its slot's screen point
//    lies inside (0.08, 0.92) x (0.1625, 0.8375) (0x005bcd28, 0x005bcf20, 0x005bcf18, 0x005bcf1c), then placed by
//    0x00457870 (eax: al cleared, ecx = screen point, edx = sprite).
//  - 28 sprites at +0x30c and five at +0x37c..+0x38c are shown according to the selection (0x004a1660/0x004a1670
//    on Game+0x748 +0xc24, Game+0xac8 mode), word +0x392 -> +0x390 state changes call 0x00466650/0x004666a0.
// Returns 0 here; the original returns whatever the last path left in EAX (the only caller ignores it).
namespace
{
void ProjectSlot(uint8_t* point)
{
    uint32_t out[3];
    orig::fn_004f02f0(u(out), u(Renderer()), u(point));
    float y;
    std::memcpy(&y, &out[1], 4);
    const float sy = y * GameGlobal<float>(0x005bcf28);
    Field<uint32_t>(point, 0xc) = out[0];
    Field<uint32_t>(point, 0x14) = 0;
    Field<float>(point, 0x10) = sy + GameGlobal<float>(0x005bcf24);
}

bool OnScreen(const uint8_t* p)
{
    const float x = Field<float>(p, 0), y = Field<float>(p, 4);
    return x < GameGlobal<float>(0x005bcf20) && x > GameGlobal<float>(0x005bcd28) &&
           y < GameGlobal<float>(0x005bcf1c) && y > GameGlobal<float>(0x005bcf18);
}

inline uint32_t& SpriteFlags(uint32_t sprite)
{
    return Field<uint32_t>(P(sprite), 0x474);
}

// Sets or clears bit 2 of the sprite's flags and returns the new value (EAX in the original).
inline uint32_t SetShown(uint32_t sprite, bool shown)
{
    uint32_t f = SpriteFlags(sprite);
    f = shown ? (f | 4u) : (f & 0xfffffffbu);
    SpriteFlags(sprite) = f;
    return f;
}

void PlaceSprite(uint32_t flags, uint8_t* screenPoint, uint32_t sprite)
{
    orig::fn_00457870(flags & 0xffffff00u, u(screenPoint), sprite, 0, 0, 0);  // reads no stack arguments
}
}  // namespace

uint32_t Hud_ProjectMarkers(uint8_t* hud)
{
    if (Field<uint8_t>(hud, 0x152))
    {
        uint8_t* p = hud + 0x158;
        for (int n = 4; n; --n, p += 0x2c)
            if (Field<uint32_t>(p - 4, 0))
                ProjectSlot(p);
    }
    else if (Field<uint32_t>(hud, 0))
        ProjectSlot(hud + 4);
    if (Field<uint8_t>(hud, 0x151) && Field<uint32_t>(hud, 0x84))
        ProjectSlot(hud + 0x88);
    if (Field<uint8_t>(g_game, 0xcbc))
    {
        uint8_t* p = hud + 0x208;
        for (int n = 6; n; --n, p += 0x2c)
            if (Field<uint32_t>(p - 4, 0))
                ProjectSlot(p);
    }

    uint32_t sprites[4];
    orig::fn_004643b0(u(sprites), Field<uint32_t>(g_game, 0xa70));
    if (Field<uint32_t>(g_game, 0x748) && Field<uint8_t>(g_game, 0xcbc))
    {
        uint8_t* p = hud + 0x164;  // screen point of slot i (the 6-slot block's first four at +0x214 are placed)
        for (uint32_t i = 0; i < 4; ++i, p += 0x2c)
        {
            const uint32_t s = sprites[i];
            if (!s)
                continue;
            if (Field<uint32_t>(p, 0xa0))
                PlaceSprite(SetShown(s, OnScreen(p)), p + 0xb0, s);
            else
                SpriteFlags(s) &= 0xfffffffbu;
        }
    }
    else if (Field<uint8_t>(hud, 0x152))
    {
        uint8_t* p = hud + 0x164;
        for (uint32_t i = 0; i < 4; ++i, p += 0x2c)
        {
            const uint32_t s = sprites[i];
            if (!s)
                continue;
            if ((orig::fn_00406740(u(hud)) & 0xff) && Field<uint32_t>(p - 0x10, 0))
            {
                const bool inside = OnScreen(p);
                if (i < 2 || Field<uint8_t>(g_game, 0xdd0))
                {
                    PlaceSprite(SetShown(s, inside), p, s);
                    continue;
                }
            }
            SpriteFlags(s) &= 0xfffffffbu;
        }
    }
    else
    {
        for (uint32_t i = 0; i < 4; ++i)
            SpriteFlags(sprites[i]) &= 0xfffffffbu;  // no null test in the original
        if (Field<uint32_t>(hud, 0) && Field<uint32_t>(g_game, 0x748) &&
            orig::fn_004a1660(Field<uint32_t>(g_game, 0x748) + 0xc24) == 5)
        {
            SpriteFlags(sprites[0]) |= 4u;
            PlaceSprite(0, hud + 0x10, sprites[0]);
        }
    }

    uint32_t* list = &Field<uint32_t>(hud, 0x30c);  // 28 sprites, then five more at +0x37c..+0x38c
    if ((orig::fn_00406790(u(hud)) & 0xff) && Field<uint32_t>(g_game, 0x748) && list[0])
    {
        if (orig::fn_0044e600() & 0xff)
        {
            for (int i = 0; i < 0x1c; ++i)
                if (list[i])
                    SpriteFlags(list[i]) &= 0xfffffffbu;
            SetShown(Field<uint32_t>(hud, 0x37c), Field<uint8_t>(hud, 0x394) != 0);
            SpriteFlags(Field<uint32_t>(hud, 0x380)) &= 0xfffffffbu;
            SpriteFlags(Field<uint32_t>(hud, 0x384)) &= 0xfffffffbu;
            SpriteFlags(Field<uint32_t>(hud, 0x38c)) &= 0xfffffffbu;
            SpriteFlags(Field<uint32_t>(hud, 0x388)) |= 4u;
            if (Field<uint16_t>(hud, 0x392) != Field<uint16_t>(hud, 0x390))
            {
                if (Field<uint16_t>(hud, 0x392))
                    orig::fn_00466650(Field<uint32_t>(hud, 0x388));
                else
                    orig::fn_004666a0(Field<uint32_t>(hud, 0x388));
                Field<uint16_t>(hud, 0x390) = Field<uint16_t>(hud, 0x392);
            }
            return 0;
        }
        if (Field<uint16_t>(hud, 0x390) == 2)
            for (int i = 0; i < 0x1c; ++i)
                if (list[i])
                    orig::fn_004666a0(list[i]);
        for (uint32_t i = 0; i < 0x1c; ++i)
        {
            const uint32_t s = list[i];
            if (!s)
                continue;
            const bool selected = Field<uint32_t>(g_game, 0xac8) == 0 &&
                                  orig::fn_004a1660(Field<uint32_t>(g_game, 0x748) + 0xc24) == i;
            if (!selected)
            {
                SpriteFlags(s) &= 0xfffffffbu;
                continue;
            }
            SpriteFlags(s) |= 4u;
            if (i == 2)
            {
                if (Field<uint8_t>(hud, 0x394))
                    SpriteFlags(Field<uint32_t>(hud, 0x314)) &= 0xfffffffbu;
                else
                    SpriteFlags(Field<uint32_t>(hud, 0x314)) |= 4u;
            }
            if (Field<uint16_t>(hud, 0x392) != Field<uint16_t>(hud, 0x390))
            {
                if (Field<uint16_t>(hud, 0x392))
                    orig::fn_00466650(list[i]);
                else
                    orig::fn_004666a0(list[i]);
                Field<uint16_t>(hud, 0x390) = Field<uint16_t>(hud, 0x392);
            }
        }
        SetShown(Field<uint32_t>(hud, 0x37c), Field<uint8_t>(hud, 0x394) != 0);
        SetShown(Field<uint32_t>(hud, 0x380), Field<uint32_t>(g_game, 0xac8) == 4);
        SetShown(Field<uint32_t>(hud, 0x384), Field<uint32_t>(g_game, 0xac8) == 3);
        SpriteFlags(Field<uint32_t>(hud, 0x388)) &= 0xfffffffbu;
        const bool any = Field<uint32_t>(g_game, 0xac8) == 0 &&
                         orig::fn_004a1670(Field<uint32_t>(g_game, 0x748) + 0xc24) != 0xffffffffu;
        return SetShown(Field<uint32_t>(hud, 0x38c), any);
    }
    if (list[0])
    {
        for (int i = 0; i < 0x1c; ++i)
            if (list[i])
                SpriteFlags(list[i]) &= 0xfffffffbu;
        SpriteFlags(Field<uint32_t>(hud, 0x37c)) &= 0xfffffffbu;
        SpriteFlags(Field<uint32_t>(hud, 0x380)) &= 0xfffffffbu;
        SpriteFlags(Field<uint32_t>(hud, 0x384)) &= 0xfffffffbu;
        SpriteFlags(Field<uint32_t>(hud, 0x388)) &= 0xfffffffbu;
        SpriteFlags(Field<uint32_t>(hud, 0x38c)) &= 0xfffffffbu;
    }
    return 0;
}

static uint32_t Hud_ProjectMarkers_Abi(uint32_t hud)
{
    return Hud_ProjectMarkers(static_cast<uint8_t*>(P(hud)));
}
ABI_ENTRY_00406840(Hud_ProjectMarkers_Entry, Hud_ProjectMarkers_Abi)
HOOK(0x00406840, "Hud_ProjectMarkers", Hud_ProjectMarkers_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// 0x00408390 (esi = the HUD marker block, as 0x00406840; callers 0x00406f50, 0x00407c20). Builds frustum planes
// for the markers. M = 0x004e2910(view, projection) with the view copied from Game+0x704 by 0x0040fa50 (stack: the
// camera Game+0xa68, not read) and the renderer's projection (virtual +0x60); rows of the 0x44-byte matrix at
// M[0..3], M[4..7], M[8..11], M[12..15]. With scales (s0, s1):
//   +0xb0 = row0 + row3*s0, +0xc0 = row3*s0 - row0, +0xd0 = row1 + row3*s1, +0xe0 = row3*s1 - row1,
//   +0xf0 = row2 + row3, each then scaled by 1.0 / sqrt((x*x + y*y) + z*z) (all four components).
// Scales: byte +0x153: Game+0x274c; byte +0x152: Game+0x2744 when Game+0xac8 == 1 else Game+0x273c, times 1.2
// (0x005bcd70) when byte Game+0xdd0; otherwise Game+0x2764 when byte +0x150 else Game+0x2734.
// When byte +0x151: a second set at +0x100 with the single scales Game+0x2754 / Game+0x2758 and plane +0x140 =
// the first set's unnormalised row2 + row3. Returns EAX as the original leaves it (a pointer past the planes).
namespace
{
void NormalizePlanes(float* p, int count)
{
    for (; count; --count, p += 4)
    {
        const float s = GameGlobal<float>(0x005bcb2c) / sqrtf((p[0] * p[0] + p[1] * p[1]) + p[2] * p[2]);
        p[0] = s * p[0];
        p[1] = s * p[1];
        p[2] = s * p[2];
        p[3] = s * p[3];
    }
}

// planes[0] = row + row3*s, planes[1] = row3*s - row (four components each).
void SidePlanes(float* out, const float* row, const float* row3, float s)
{
    const float a = row3[0] * s, b = row3[1] * s, c = row3[2] * s, d = row3[3] * s;
    out[0] = row[0] + a;
    out[1] = row[1] + b;
    out[2] = row[2] + c;
    out[3] = row[3] + d;
    out[4] = a - row[0];
    out[5] = b - row[1];
    out[6] = c - row[2];
    out[7] = d - row[3];
}
}  // namespace

uint32_t HudFrustum_Build(uint8_t* hud)
{
    alignas(8) uint32_t view[17];
    alignas(8) float m[17];
    orig::fn_0040fa50(u(view), Field<uint32_t>(g_game, 0xa68));
    const uint32_t projection = VCall<uint32_t>(Renderer(), 0x60 / 4);
    orig::fn_004e2910(u(view), u(m), projection);
    void* game = g_game;
    const float* scale;
    float boosted[2];
    if (Field<uint8_t>(hud, 0x153))
        scale = &Field<float>(game, 0x274c);
    else if (Field<uint8_t>(hud, 0x152))
    {
        scale = Field<uint32_t>(game, 0xac8) == 1 ? &Field<float>(game, 0x2744) : &Field<float>(game, 0x273c);
        if (Field<uint8_t>(game, 0xdd0))
        {
            boosted[0] = scale[0] * GameGlobal<float>(0x005bcd70);
            boosted[1] = scale[1] * GameGlobal<float>(0x005bcd70);
            scale = boosted;
        }
    }
    else
        scale = Field<uint8_t>(hud, 0x150) ? &Field<float>(game, 0x2764) : &Field<float>(game, 0x2734);

    const float* row3 = m + 12;
    float* planes = &Field<float>(hud, 0xb0);
    SidePlanes(planes, m, row3, scale[0]);
    SidePlanes(planes + 8, m + 4, row3, scale[1]);
    float far4[4];
    for (int i = 0; i < 4; ++i)
        far4[i] = m[8 + i] + row3[i];
    planes[16] = far4[0], planes[17] = far4[1], planes[18] = far4[2], planes[19] = far4[3];
    NormalizePlanes(planes, 5);
    if (!Field<uint8_t>(hud, 0x151))
        return (u(hud) + 0x108) & 0xffffff00u;
    float* second = &Field<float>(hud, 0x100);
    SidePlanes(second, m, row3, Field<float>(game, 0x2754));
    SidePlanes(second + 8, m + 4, row3, Field<float>(game, 0x2758));
    second[16] = far4[0], second[17] = far4[1], second[18] = far4[2], second[19] = far4[3];
    NormalizePlanes(second, 5);
    return u(hud) + 0x158;
}

static uint32_t HudFrustum_Build_Abi(uint32_t esi)
{
    return HudFrustum_Build(static_cast<uint8_t*>(P(esi)));
}
ABI_ENTRY_00408390(HudFrustum_Build_Entry, HudFrustum_Build_Abi)
HOOK(0x00408390, "HudFrustum_Build", HudFrustum_Build_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// 0x0040f750 (stdcall, 1 arg not read; caller 0x0040e350). Moves two object lists of Game+0xa84 to the camera
// (Game+0xa68): each object of list +0xecc goes through 0x00457990 (eax: object with al cleared; stack: object,
// camera +0x8c, the camera matrix); each object of list +0xed0 through 0x00457870 (eax: [camera+0xb8] with al
// cleared, ecx = {camera +0x98, +0xa8, +0xb8} (the matrix's translation column), edx = object). One iterator for
// both lists (0x004e7340 / 0x004e73c0); its node loses a reference (+0x18) at the end and is returned.
uint32_t fn_0040f750(uint32_t /*arg*/)
{
    uint32_t iterator[2] = {0, 0};
    for (uint32_t obj = orig::fn_004e7340(Field<uint32_t>(Field<void*>(g_game, 0xa84), 0xecc), u(iterator)); obj;
         obj = orig::fn_004e73c0(u(iterator)))
        orig::fn_00457990(obj & 0xffffff00u, obj, Field<uint32_t>(g_game, 0xa68) + 0x8c);
    for (uint32_t obj = orig::fn_004e7340(Field<uint32_t>(Field<void*>(g_game, 0xa84), 0xed0), u(iterator)); obj;
         obj = orig::fn_004e73c0(u(iterator)))
    {
        void* camera = Field<void*>(g_game, 0xa68);
        uint32_t position[3];
        position[0] = Field<uint32_t>(camera, 0x98);
        position[1] = Field<uint32_t>(camera, 0xa8);
        position[2] = Field<uint32_t>(camera, 0xb8);
        orig::fn_00457870(position[2] & 0xffffff00u, u(position), obj, 0, 0, 0);  // reads no stack arguments
    }
    if (iterator[1])
        --Field<int32_t>(P(iterator[1]), 0x18);
    return iterator[1];
}

static uint32_t fn_0040f750_Abi(uint32_t a0)
{
    return fn_0040f750(a0);
}
ABI_ENTRY_0040f750(fn_0040f750_Entry, fn_0040f750_Abi)
HOOK(0x0040f750, "fn_0040f750", fn_0040f750_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// 0x0040f970 (eax = camera-like object, ecx = colour/vector (3 dwords); stack: a, b (floats); callee pops 8;
// caller 0x00423900). Stores a at +0xdc, b at +0xe0 and the vector at +0xe4, then calls renderer virtual +0x114
// (a * [+0xd0], b * [+0xd0], &obj[+0xe4]); returns its result. Guess: fog start/end and colour.
uint32_t fn_0040f970(uint8_t* obj, const uint32_t* vec, uint32_t a, float b)
{
    const float scaledB = b * Field<float>(obj, 0xd0);
    Field<uint32_t>(obj, 0xdc) = a;
    std::memcpy(&Field<uint32_t>(obj, 0xe0), &b, 4);
    uint32_t* dst = &Field<uint32_t>(obj, 0xe4);
    dst[0] = vec[0];
    dst[1] = vec[1];
    dst[2] = vec[2];
    float af;
    std::memcpy(&af, &a, 4);
    const float scaledA = af * Field<float>(obj, 0xd0);
    return VCall<uint32_t>(Renderer(), 0x114 / 4, scaledA, scaledB, static_cast<void*>(dst));
}

static uint32_t fn_0040f970_Abi(uint32_t eax, uint32_t ecx, uint32_t a0, uint32_t a1)
{
    float b;
    std::memcpy(&b, &a1, 4);
    return fn_0040f970(static_cast<uint8_t*>(P(eax)), static_cast<const uint32_t*>(P(ecx)), a0, b);
}
ABI_ENTRY_0040f970(fn_0040f970_Entry, fn_0040f970_Abi)
HOOK(0x0040f970, "fn_0040f970", fn_0040f970_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// 0x00410150 (eax = optional out pointer, ecx = object; 8 callers): *out = object + 0x184 when out is given;
// returns object[+0x17c] == 1.
uint32_t fn_00410150(uint32_t* out, uint8_t* obj)
{
    if (out)
        *out = u(obj) + 0x184;
    return Field<uint32_t>(obj, 0x17c) == 1 ? 1u : 0u;
}

static uint32_t fn_00410150_Abi(uint32_t eax, uint32_t ecx)
{
    return fn_00410150(static_cast<uint32_t*>(P(eax)), static_cast<uint8_t*>(P(ecx)));
}
ABI_ENTRY_00410150(fn_00410150_Entry, fn_00410150_Abi)
HOOK(0x00410150, "fn_00410150", fn_00410150_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// 0x004102e0 (edx = fx entity; callers 0x004101a0 (its init, which reads "fx_name", "trigger_start"), 0x004102b0).
// Starts the entity's effect: a zeroed 0x80-byte request {+0 = 4, +0x18 = 0, +0x1c = entity, +0x74 = 0} goes to
// the fx manager 0x0041cc60 (eax: request address with al cleared, ecx = Game+0xa90; stack: entity +0x460 (the
// fx name/handle set by the init), request).
uint32_t fn_004102e0(uint8_t* entity)
{
    alignas(8) uint32_t request[0x20] = {};
    request[6] = 0;
    request[0x1d] = 0;
    request[7] = u(entity);
    request[0] = 4;
    return orig::fn_0041cc60(u(request) & 0xffffff00u, Field<uint32_t>(g_game, 0xa90), u(entity) + 0x460,
                             u(request));
}

static uint32_t fn_004102e0_Abi(uint32_t edx)
{
    return fn_004102e0(static_cast<uint8_t*>(P(edx)));
}
ABI_ENTRY_004102e0(fn_004102e0_Entry, fn_004102e0_Abi)
HOOK(0x004102e0, "fn_004102e0", fn_004102e0_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// 0x00410d40 (no inputs; caller Game_RenderFrame 0x00403050, first thing each frame): when the renderer exists,
// renderer virtual +0x154 (4, (Game+0x4c8 >> 14) & 0xffffff01) (bit 14 of the option word; guess: a render
// option such as wireframe). Returns the virtual's result, or the game pointer when there is no renderer.
uint32_t fn_00410d40()
{
    Game* game = g_game;
    void* r = Field<void*>(game, 0x38);
    if (!r)
        return u(game);
    return VCall<uint32_t>(r, 0x154 / 4, 4u, (Field<uint32_t>(game, 0x4c8) >> 14) & 0xffffff01u);
}

static uint32_t fn_00410d40_Abi()
{
    return fn_00410d40();
}
ABI_ENTRY_00410d40(fn_00410d40_Entry, fn_00410d40_Abi)
HOOK(0x00410d40, "fn_00410d40", fn_00410d40_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// 0x00410fa0 (cdecl, 1 arg: a model instance; caller 0x00419060, itself only reached through a table). Not executed
// in the recorded runs. When the option word Game+0x4c8 masked by [obj+0x414]+0x44 has bits other than 0x400:
// picks the current node (obj virtual +0x40 = index into [obj+0x414]+0xc, 12-byte entries, +8), then for each of
// the 7 entries {value, scale, mask} of the table at 0x00589120 whose mask matches Game+0x4c8: looks the value up
// in the node's sorted map (0x004e0f40, esi = map: binary search through its callback), takes the found part,
// copies its 0x2c-byte descriptor ([obj+0x410]+0x20, 0x30-byte entries), scales its +0x1c..+0x24 by the table's
// scale (or sets them to it and sets flag 2), and calls 0x004ec2f0 (edx = descriptor; stack: part+0x10, part,
// [obj+0x410], descriptor's +0x2c, 0, 0). Every part so handled gets bit 0x8000 (+8), its level ([obj+0x410]+0x1c,
// 0x48-byte entries, +0x44) = count - 1 (or -1), and is listed. Then parts of the node whose parent (+0x10) carries
// bit 0x8000 are handled the same way, 0x004543f0 (ecx = obj; 0, 1, 0) runs, and every listed part loses bit 0x8000
// and is called again with its own descriptor. Guess: per-option highlighted sub-mesh drawing.
// Original bug, reproduced: the second pass reads +8 of a part slot that resolved to null (no null test).
uint32_t fn_00410fa0(uint8_t* obj)
{
    void* game = g_game;
    uint32_t ret = Field<uint32_t>(obj, 0x414);
    if (!((Field<uint32_t>(game, 0x4c8) & Field<uint32_t>(P(ret), 0x44)) & 0xfffffbffu))
        return ret;
    uint8_t* models = Field<uint8_t*>(obj, 0x410);
    if (!models || !Field<uint32_t>(models, 0x20))
        return u(models);
    const int32_t index = VCall<int32_t>(obj, 0x40 / 4);
    if (index < 0)
        return static_cast<uint32_t>(index);
    uint8_t* node = Field<uint8_t*>(Field<uint8_t*>(Field<void*>(obj, 0x414), 0xc) + index * 12, 8);
    uint32_t descriptor[0xb];
    uint32_t lodCount = 0;  // the descriptor's +0x2c of the last part found (esp+0x1c)
    uint8_t* parts[0x80];
    uint32_t count = 0;
    auto finish = [&](uint8_t* part) {
        const int32_t level = lodCount <= 1 ? -1 : static_cast<int32_t>(lodCount - 1);
        Field<int32_t>(Field<uint8_t*>(Field<uint8_t*>(obj, 0x410), 0x1c) + Field<uint16_t>(part, 0xa) * 0x48, 0x44) =
            level;
        Field<uint8_t>(part, 9) |= 0x80;
        parts[count++] = part;
    };
    for (uint32_t t = 0; t < 0x54; t += 0xc)
    {
        if (!(GameGlobal<uint32_t>(0x00589128 + t) & Field<uint32_t>(g_game, 0x4c8)))
            continue;
        uint8_t* a = Field<uint8_t*>(Field<uint8_t*>(node, 0x2c), 4);
        const uint32_t key = Field<uint32_t>(a, 0x1c);
        const uint32_t value = GameGlobal<uint32_t>(0x00589120 + t);
        if (!key || !Field<uint32_t>(a, 0x20))
            continue;
        uint8_t* map = Field<uint8_t*>(a, 0x24);
        Field<uint32_t>(map, 0) = key;
        Field<uint32_t>(map, 0x18) = value;
        orig::fn_004e0f40(u(map));
        if (Field<uint32_t>(map, 0xc))
            continue;
        uint8_t* entry = Field<uint8_t*>(Field<uint8_t*>(map, 0x10) + Field<uint32_t>(map, 8) * 4, 0);
        if (!entry)
            continue;
        uint8_t* part = Field<uint8_t*>(entry + Field<uint16_t>(entry, 2), 0);
        if (!part)
            continue;
        models = Field<uint8_t*>(obj, 0x410);
        const uint8_t* src = Field<uint8_t*>(models, 0x20) + Field<uint16_t>(part, 0xa) * 0x30;
        const uint32_t lods = Field<uint32_t>(src, 0x2c);
        std::memcpy(descriptor, src, sizeof(descriptor));
        lodCount = lods;
        float* scale = reinterpret_cast<float*>(&descriptor[7]);
        const float s = GameGlobal<float>(0x00589124 + t);
        if (descriptor[10] & 2)
        {
            scale[0] = scale[0] * s;
            scale[1] = scale[1] * s;
            scale[2] = s * scale[2];
        }
        else
        {
            descriptor[10] |= 2;
            scale[2] = s;
            scale[1] = s;
            scale[0] = s;
        }
        ret = orig::fn_004ec2f0(u(descriptor), Field<uint32_t>(part, 0x10), u(part), u(models), lods, 0, 0);
        finish(part);
    }
    if (!count)
        return ret;
    for (uint32_t i = 0; i < Field<uint32_t>(node, 0x30); ++i)
    {
        uint8_t* a = Field<uint8_t*>(Field<uint8_t*>(node, 0x2c), 4);
        uint8_t* part = nullptr;
        if (i < Field<uint32_t>(a, 0x1c) && Field<uint32_t>(a, 0x20))
        {
            uint8_t* entry = Field<uint8_t*>(Field<uint8_t*>(Field<uint8_t*>(a, 0x24), 0x10) + i * 4, 0);
            if (entry)
                part = Field<uint8_t*>(entry + Field<uint16_t>(entry, 2), 0);
        }
        if (Field<uint16_t>(part, 8) & 0x8000)  // part may be null here (original bug)
            continue;
        uint8_t* parent = Field<uint8_t*>(part, 0x10);
        if (!parent || !(Field<uint16_t>(parent, 8) & 0x8000))
            continue;
        models = Field<uint8_t*>(obj, 0x410);
        ret = orig::fn_004ec2f0(u(Field<uint8_t*>(models, 0x20) + Field<uint16_t>(part, 0xa) * 0x30), u(parent),
                                u(part), u(models), lodCount, 0, 0);
        finish(part);
    }
    ret = orig::fn_004543f0(0, u(obj), 0, 0, 1, 0);
    for (uint32_t i = 0; i < count; ++i)
    {
        uint8_t* part = parts[i];
        Field<uint16_t>(part, 8) &= 0x7fff;
        models = Field<uint8_t*>(obj, 0x410);
        ret = orig::fn_004ec2f0(u(Field<uint8_t*>(models, 0x20) + Field<uint16_t>(part, 0xa) * 0x30),
                                Field<uint32_t>(part, 0x10), u(part), u(models), lodCount, 0, 0);
    }
    return ret;
}

static uint32_t fn_00410fa0_Abi(uint32_t a0)
{
    return fn_00410fa0(static_cast<uint8_t*>(P(a0)));
}
ABI_ENTRY_00410fa0(fn_00410fa0_Entry, fn_00410fa0_Abi)
HOOK(0x00410fa0, "fn_00410fa0", fn_00410fa0_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// 0x00415dc0 (esi = renderer, edi = render-pass state; stack: a0, a1 (bytes); callee pops 8; 12 callers: the
// render-to-texture and post passes). Pass begin: the current viewport (renderer virtual +0x7c, 16 bytes) is saved
// at state +0xcc; 0x004f0170 (renderer, state); state +0xe2 = renderer virtual +0x158 (4) (low byte); when a1 is
// clear and that byte is set, renderer virtual +0x154 (4, 0); renderer virtual +0x74 (0) (full viewport);
// 0x004f01c0 (renderer); renderer virtual +0xa4 (0, 0, 0, 2, stage) for stages 0..3; when byte Game+0x4bc and a0:
// state +0x228 = renderer virtual +0xdc (). Returns state +0x228.
uint32_t RenderPass_Begin(void* renderer, uint8_t* state, uint8_t a0, uint8_t a1)
{
    const uint32_t* vp = VCall<const uint32_t*>(renderer, 0x7c / 4);
    uint32_t* saved = &Field<uint32_t>(state, 0xcc);
    saved[0] = vp[0];
    saved[1] = vp[1];
    saved[2] = vp[2];
    saved[3] = vp[3];
    orig::fn_004f0170(u(renderer), u(state));
    const uint8_t on = static_cast<uint8_t>(VCall<uint32_t>(renderer, 0x158 / 4, 4u));
    Field<uint8_t>(state, 0xe2) = on;
    if (!a1 && on)
        VCall<void>(renderer, 0x154 / 4, 4u, 0u);
    VCall<void>(renderer, 0x74 / 4, 0u);
    orig::fn_004f01c0(u(renderer));
    for (uint32_t stage = 0; stage < 4; ++stage)
        VCall<void>(renderer, 0xa4 / 4, 0u, 0u, 0u, 2u, stage);
    if (Field<uint8_t>(g_game, 0x4bc) && a0)
        Field<uint32_t>(state, 0x228) = VCall<uint32_t>(renderer, 0xdc / 4);
    return Field<uint32_t>(state, 0x228);
}

static uint32_t RenderPass_Begin_Abi(uint32_t esi, uint32_t edi, uint32_t a0, uint32_t a1)
{
    return RenderPass_Begin(P(esi), static_cast<uint8_t*>(P(edi)), static_cast<uint8_t>(a0),
                            static_cast<uint8_t>(a1));
}
ABI_ENTRY_00415dc0(RenderPass_Begin_Entry, RenderPass_Begin_Abi)
HOOK(0x00415dc0, "RenderPass_Begin", RenderPass_Begin_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// Polygons used by the decal clipper: { uint32 count; float v[count][4] } (x, y, z, w; w = signed distance to the
// last clipping plane, or 0 at an intersection point).
//
// 0x00415eb0 (ecx = out polygon; stack: in polygon, plane {nx, ny, nz, d}; callee pops 8; caller 0x00416010).
// Sutherland-Hodgman against one plane: distance = ((nz*z + y*ny) + nx*x) + d, kept when not below 0.0 (0x0056c05c;
// NaN counts as outside). An edge crossing the plane adds prev + (cur - prev) * -(dPrev / (d - dPrev)) with w = 0;
// a kept vertex is copied with w = its distance. Returns out.count >= 3.
uint32_t Polygon_ClipPlane(uint32_t* out, const uint32_t* in, const float* plane)
{
    out[0] = 0;
    const float zero = GameGlobal<float>(0x0056c05c);
    uint32_t n = in[0];
    const float* prev = reinterpret_cast<const float*>(in + 1 + (n - 1) * 4);
    float dPrev = ((prev[2] * plane[2]) + (prev[1] * plane[1])) + prev[0] * plane[0];
    dPrev = dPrev + plane[3];
    bool prevInside = dPrev >= zero;  // C0 clear: not below and ordered
    for (uint32_t i = 0; i < n; ++i)
    {
        const float* cur = reinterpret_cast<const float*>(in + 1 + i * 4);
        float d = (plane[2] * cur[2] + cur[1] * plane[1]) + plane[0] * cur[0];
        d = d + plane[3];
        const bool inside = d >= zero;
        if (inside != prevInside)
        {
            const float t = -(dPrev / (d - dPrev));
            float* v = reinterpret_cast<float*>(out + 1 + out[0] * 4);
            v[0] = (cur[0] - prev[0]) * t + prev[0];
            v[1] = (cur[1] - prev[1]) * t + prev[1];
            v[2] = (cur[2] - prev[2]) * t + prev[2];
            out[(out[0] + 1) * 4] = 0;
            ++out[0];
        }
        if (inside)
        {
            uint32_t* v = out + 1 + out[0] * 4;
            const uint32_t* c = reinterpret_cast<const uint32_t*>(cur);
            v[0] = c[0];
            v[1] = c[1];
            v[2] = c[2];
            std::memcpy(&out[(out[0] + 1) * 4], &d, 4);
            ++out[0];
        }
        dPrev = d;
        prevInside = inside;
        prev = cur;
        n = in[0];
    }
    return out[0] >= 3 ? 1u : 0u;
}

static uint32_t Polygon_ClipPlane_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return Polygon_ClipPlane(static_cast<uint32_t*>(P(ecx)), static_cast<const uint32_t*>(P(a0)),
                             static_cast<const float*>(P(a1)));
}
ABI_ENTRY_00415eb0(Polygon_ClipPlane_Entry, Polygon_ClipPlane_Abi)
HOOK(0x00415eb0, "Polygon_ClipPlane", Polygon_ClipPlane_Entry, nullptr);

// 0x00416010 (eax = plane count, ecx = scratch polygon, edx = polygon; stack: planes (16 bytes each), caller pops;
// caller 0x00416930). Clips the polygon against each plane in turn (0x00415eb0), ping-ponging between the two
// buffers. Returns the buffer holding the result, or 0 as soon as fewer than 3 vertices remain.
uint32_t Polygon_ClipPlanes(uint32_t count, uint32_t* scratch, uint32_t* poly, const float* planes)
{
    uint32_t* dst = scratch;
    uint32_t* src = poly;
    while (count)
    {
        --count;
        if (!Polygon_ClipPlane(dst, src, planes))
            return 0;
        planes += 4;
        uint32_t* t = src;
        src = dst;
        dst = t;
    }
    return u(src);
}

static uint32_t Polygon_ClipPlanes_Abi(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t a0)
{
    return Polygon_ClipPlanes(eax, static_cast<uint32_t*>(P(ecx)), static_cast<uint32_t*>(P(edx)),
                              static_cast<const float*>(P(a0)));
}
ABI_ENTRY_00416010(Polygon_ClipPlanes_Entry, Polygon_ClipPlanes_Abi)
HOOK(0x00416010, "Polygon_ClipPlanes", Polygon_ClipPlanes_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// The decal and shadow manager (Game+0xa7c; vtable 0x005b9678): +4/+5 flags (+4: shadows on, read by
// Shadows_Update), lists +8 (free decals), +0xc (active decals), +0x10, +0x14 (active shadows, read by
// Shadows_Draw), counters +0x18..+0x20, +0x24 fade length (decals.cpp).
//
// 0x00416050 (edx = manager; caller 0x004011e0): constructor. Vtable, counters and lists 0, flags +4/+5 = 1, fade
// length 1.0, and the shared identity matrix at 0x005fe528 (0x44 bytes; used by Shadows_Draw). Returns the manager.
uint32_t DecalManager_Construct(uint8_t* m)
{
    Field<uint32_t>(m, 0) = 0x005b9678;
    Field<uint32_t>(m, 0x18) = 0;
    Field<uint32_t>(m, 0x1c) = 0;
    Field<uint32_t>(m, 0x20) = 0;
    Field<uint8_t>(m, 4) = 1;
    Field<uint8_t>(m, 5) = 1;
    Field<uint32_t>(m, 0xc) = 0;
    Field<uint32_t>(m, 8) = 0;
    Field<uint32_t>(m, 0x14) = 0;
    Field<uint32_t>(m, 0x10) = 0;
    uint32_t* identity = &GameGlobal<uint32_t>(0x005fe528);
    for (int i = 0; i < 0x11; ++i)
        identity[i] = 0;
    identity[0] = 0x3f800000;
    identity[5] = 0x3f800000;
    identity[10] = 0x3f800000;
    identity[15] = 0x3f800000;
    Field<uint32_t>(m, 0x24) = 0x3f800000;
    return u(m);
}

static uint32_t DecalManager_Construct_Abi(uint32_t edx)
{
    return DecalManager_Construct(static_cast<uint8_t*>(P(edx)));
}
ABI_ENTRY_00416050(DecalManager_Construct_Entry, DecalManager_Construct_Abi)
HOOK(0x00416050, "DecalManager_Construct", DecalManager_Construct_Entry, nullptr);

// 0x004160d0 (esi = manager; callers 0x00403940, 0x004160c0, 0x00416130, 0x0044a670): empties lists +0xc, +8,
// +0x14, +0x10 in that order by calling each head's virtual +0x18 (1) (deleting destructor, which unlinks it) until
// the list is empty. Returns 0 here; the original leaves the last virtual call's EAX (callers ignore it: guess).
uint32_t DecalManager_Clear(uint8_t* m)
{
    static const uint32_t lists[4] = {0xc, 8, 0x14, 0x10};
    for (uint32_t off : lists)
        while (void* head = Field<void*>(m, off))
            VCall<void>(head, 0x18 / 4, 1u);
    return 0;
}

static uint32_t DecalManager_Clear_Abi(uint32_t esi)
{
    return DecalManager_Clear(static_cast<uint8_t*>(P(esi)));
}
ABI_ENTRY_004160d0(DecalManager_Clear_Entry, DecalManager_Clear_Abi)
HOOK(0x004160d0, "DecalManager_Clear", DecalManager_Clear_Entry, nullptr);

// 0x004160c0 (eax = manager; caller 0x00401710; not executed in the recorded runs): destructor body. Restores
// the vtable 0x005b9678, then DecalManager_Clear.
uint32_t DecalManager_Destruct(uint8_t* m)
{
    Field<uint32_t>(m, 0) = 0x005b9678;
    return DecalManager_Clear(m);
}

static uint32_t DecalManager_Destruct_Abi(uint32_t eax)
{
    return DecalManager_Destruct(static_cast<uint8_t*>(P(eax)));
}
ABI_ENTRY_004160c0(DecalManager_Destruct_Entry, DecalManager_Destruct_Abi)
HOOK(0x004160c0, "DecalManager_Destruct", DecalManager_Destruct_Entry, nullptr);

// 0x00416190 (stdcall, 1 arg: manager; caller 0x00416130). Shadow setup for a level:
//  - global matrix 0x005fe4e0 (0x44 bytes) = texture-space bias: [0] 0.5, [5] -0.5, [8]... set as the original:
//    +0x00 0.5, +0x14 -0.5, +0x28 1.0, +0x0c 0.5, +0x1c 0.5, +0x3c 1.0; flags word +0x40 = 0x13;
//  - global block 0x005fe498 (0x48 bytes): +0 0x020a3060, +4 0x80ffffff, +0x18 0x006047a4, +0x1c 0xff404040,
//    +0x30 0x001038a0, +0x34 = Game+0x2704 (guess: shadow vertex formats / colours);
//  - shadows: for each of the 3 entries {count, size} at 0x005b96f8, count shadows of 0x59b0 bytes from the heap
//    0x0063a210 (0x004e07f0: alignment 0x10, flags 2, tag 0x0058b5e8), each set up by 0x00417990 (edx = shadow;
//    stack: manager +0x10 (the free shadow list), size).
// Returns EAX as the original leaves it.
uint32_t Shadows_AllocatePool(uint8_t* m)
{
    uint32_t* bias = &GameGlobal<uint32_t>(0x005fe4e0);
    for (int i = 0; i < 0x11; ++i)
        bias[i] = 0;
    uint32_t* block = &GameGlobal<uint32_t>(0x005fe498);
    for (int i = 0; i < 0x12; ++i)
        block[i] = 0;
    void* game = g_game;
    GameGlobal<uint32_t>(0x005fe51c) = 0x3f800000;
    GameGlobal<uint32_t>(0x005fe4e0) = 0x3f000000;
    GameGlobal<uint32_t>(0x005fe4f4) = 0xbf000000;
    GameGlobal<uint32_t>(0x005fe508) = 0x3f800000;
    GameGlobal<uint32_t>(0x005fe4ec) = 0x3f000000;
    GameGlobal<uint32_t>(0x005fe4fc) = 0x3f000000;
    GameGlobal<uint32_t>(0x005fe520) = 0x13;
    GameGlobal<uint32_t>(0x005fe498) = 0x020a3060;
    GameGlobal<uint32_t>(0x005fe49c) = 0x80ffffff;
    GameGlobal<uint32_t>(0x005fe4b0) = 0x006047a4;
    GameGlobal<uint32_t>(0x005fe4b4) = 0xff404040;
    GameGlobal<uint32_t>(0x005fe4c8) = 0x001038a0;
    GameGlobal<uint32_t>(0x005fe4cc) = Field<uint32_t>(game, 0x2704);
    uint32_t ret = 0;
    for (uint32_t e = 0x005b96fc; e < 0x005b9714; e += 8)
    {
        const int32_t count = GameGlobal<int32_t>(e - 4);
        const uint32_t size = GameGlobal<uint32_t>(e);
        ret = static_cast<uint32_t>(count);
        if (count <= 0)
            continue;
        for (int32_t n = count; n; --n)
        {
            ret = orig::fn_004e07f0(0x0063a210, 0x10, 0x59b0, 2, 0x0058b5e8);
            if (ret)
                ret = orig::fn_00417990(ret, u(m) + 0x10, size);
        }
    }
    return ret;
}

static uint32_t Shadows_AllocatePool_Abi(uint32_t a0)
{
    return Shadows_AllocatePool(static_cast<uint8_t*>(P(a0)));
}
ABI_ENTRY_00416190(Shadows_AllocatePool_Entry, Shadows_AllocatePool_Abi)
HOOK(0x00416190, "Shadows_AllocatePool", Shadows_AllocatePool_Entry, nullptr);

// 0x00416130 (eax = manager; caller Game_StartLevel 0x00403690): level setup: DecalManager_Clear, then
// Decals_AllocatePool (0x00416150, edi = manager; decals.cpp) and Shadows_AllocatePool. Returns the latter's EAX.
uint32_t DecalManager_Reset(uint8_t* m)
{
    DecalManager_Clear(m);
    orig::fn_00416150(u(m));
    return Shadows_AllocatePool(m);
}

static uint32_t DecalManager_Reset_Abi(uint32_t eax)
{
    return DecalManager_Reset(static_cast<uint8_t*>(P(eax)));
}
ABI_ENTRY_00416130(DecalManager_Reset_Entry, DecalManager_Reset_Abi)
HOOK(0x00416130, "DecalManager_Reset", DecalManager_Reset_Entry, nullptr);

// 0x00416290 (eax = manager; callers 0x00401070 / 0x004010c0, the device-lost / device-restored handlers; not
// executed in the recorded runs): clears byte +0x119 of every active shadow (list +0x14, linked by +0x50) (guess:
// "render target valid"). Returns 0 (the list end).
uint32_t Shadows_InvalidateTargets(uint8_t* m)
{
    uint8_t* s = Field<uint8_t*>(m, 0x14);
    for (; s; s = Field<uint8_t*>(s, 0x50))
        Field<uint8_t>(s, 0x119) = 0;
    return 0;
}

static uint32_t Shadows_InvalidateTargets_Abi(uint32_t eax)
{
    return Shadows_InvalidateTargets(static_cast<uint8_t*>(P(eax)));
}
ABI_ENTRY_00416290(Shadows_InvalidateTargets_Entry, Shadows_InvalidateTargets_Abi)
HOOK(0x00416290, "Shadows_InvalidateTargets", Shadows_InvalidateTargets_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// 0x00416330 (edi = manager Game+0xa7c; caller ScenePass_Render flag 2). Draws the decals: when the active list
// (+0xc) is not empty: world = the identity 0x005fe528 (renderer virtual +0x54), renderer virtual +0xa4
// (0, 0, 0, 2, 0); each active decal (linked by +0x50) is drawn (its virtual +0x20 (0, 1)) when byte manager+5 is
// set or its +0xf5c (age, decals.cpp) is below 0.0 (0x0056c05c); then renderer virtual +0xa4 (0, 0, 0, 0, 0),
// whose result is returned (0 when nothing was active).
uint32_t Decals_Draw(uint8_t* m)
{
    if (!Field<void*>(m, 0xc))
        return 0;
    VCall<void>(Renderer(), 0x54 / 4, reinterpret_cast<void*>(0x005fe528));
    VCall<void>(Renderer(), 0xa4 / 4, 0u, 0u, 0u, 2u, 0u);
    for (uint8_t* d = Field<uint8_t*>(m, 0xc); d; d = Field<uint8_t*>(d, 0x50))
        if (Field<uint8_t>(m, 5) || Field<float>(d, 0xf5c) < GameGlobal<float>(0x0056c05c))
            VCall<void>(d, 0x20 / 4, 0u, 1u);
    return VCall<uint32_t>(Renderer(), 0xa4 / 4, 0u, 0u, 0u, 0u, 0u);
}

static uint32_t Decals_Draw_Abi(uint32_t edi)
{
    return Decals_Draw(static_cast<uint8_t*>(P(edi)));
}
ABI_ENTRY_00416330(Decals_Draw_Entry, Decals_Draw_Abi)
HOOK(0x00416330, "Decals_Draw", Decals_Draw_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// 0x00416530 (ebx = entity; stack: manager; callee pops 4; caller 0x00455c10). When the entity has no shadow yet
// (+0x3e0 == 0): picks from the manager's free shadow list (+0x10, linked by +0x50) the one whose size (+0x6c)
// matches the entity's wanted size (+0x3e4) exactly, else the one with the smallest cost (larger by k: 3k, smaller
// by k: 2k; first wins ties), and attaches it (0x00417b90, ecx = shadow, edx = entity). Returns entity +0x3e0.
uint32_t Shadows_Attach(uint8_t* entity, uint8_t* m)
{
    if (!Field<uint32_t>(entity, 0x3e0))
    {
        const int32_t want = Field<int32_t>(entity, 0x3e4);
        uint8_t* best = nullptr;
        int32_t bestCost = static_cast<int32_t>(u(m));  // the original reuses the argument's slot
        for (uint8_t* s = Field<uint8_t*>(m, 0x10); s; s = Field<uint8_t*>(s, 0x50))
        {
            const int32_t diff = Field<int32_t>(s, 0x6c) - want;
            if (diff == 0)
            {
                best = s;
                break;
            }
            const int32_t cost = diff * (diff >= 0 ? 3 : -2);
            if (!best || cost < bestCost)
            {
                bestCost = cost;
                best = s;
            }
        }
        if (best)
            orig::fn_00417b90(u(best), u(entity));
    }
    return Field<uint32_t>(entity, 0x3e0);
}

static uint32_t Shadows_Attach_Abi(uint32_t ebx, uint32_t a0)
{
    return Shadows_Attach(static_cast<uint8_t*>(P(ebx)), static_cast<uint8_t*>(P(a0)));
}
ABI_ENTRY_00416530(Shadows_Attach_Entry, Shadows_Attach_Abi)
HOOK(0x00416530, "Shadows_Attach", Shadows_Attach_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// 0x004165a0 (eax = manager; caller 0x0048cb30; not executed in the recorded runs): takes the first free decal
// (+8) and activates it (0x00418510, ecx = decal, edx = manager +0xc, the active list). Returns the decal or 0.
uint32_t Decals_TakeFree(uint8_t* m)
{
    const uint32_t d = Field<uint32_t>(m, 8);
    if (d)
        orig::fn_00418510(d, u(m) + 0xc);
    return d;
}

static uint32_t Decals_TakeFree_Abi(uint32_t eax)
{
    return Decals_TakeFree(static_cast<uint8_t*>(P(eax)));
}
ABI_ENTRY_004165a0(Decals_TakeFree_Entry, Decals_TakeFree_Abi)
HOOK(0x004165a0, "Decals_TakeFree", Decals_TakeFree_Entry, nullptr);

// 0x004165c0 (eax = manager; stack: a0..a4; callee pops 0x14; callers 0x0040cb10, 0x00420770). Spawns a decal: the
// first free one (+8), or else the oldest active one (largest age +0xf5c, the first on ties) recycled by 0x00417110
// (ecx = decal; called even with an empty active list, ecx = 0: original behaviour); then 0x00417260 (eax = a0;
// stack: decal, a1, a2, a3, a4) builds it. Returns the decal when that succeeded (al), else 0.
uint32_t Decals_Spawn(uint8_t* m, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    uint8_t* d = Field<uint8_t*>(m, 8);
    if (!d)
    {
        for (uint8_t* c = Field<uint8_t*>(m, 0xc); c; c = Field<uint8_t*>(c, 0x50))
            if (!d || Field<float>(c, 0xf5c) > Field<float>(d, 0xf5c))
                d = c;
        orig::fn_00417110(u(d));
    }
    return (orig::fn_00417260(a0, u(d), a1, a2, a3, a4) & 0xff) ? u(d) : 0u;
}

static uint32_t Decals_Spawn_Abi(uint32_t eax, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    return Decals_Spawn(static_cast<uint8_t*>(P(eax)), a0, a1, a2, a3, a4);
}
ABI_ENTRY_004165c0(Decals_Spawn_Entry, Decals_Spawn_Abi)
HOOK(0x004165c0, "Decals_Spawn", Decals_Spawn_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// 0x00416620 (eax = manager, edi = entity; caller 0x004547b0). When the entity's +0x3ec has bit 0x20000000, every
// active shadow (list +0x14) that lists the entity among its +0x24 receivers (+0x2c array) gets its virtual +0x1c;
// bit 0x40000000: the same over the active decals (list +0xc). Both bits are then cleared; returns the new word.
uint32_t DecalManager_EntityChanged(uint8_t* m, uint8_t* entity)
{
    auto notify = [&](uint32_t list) {
        for (uint8_t* s = Field<uint8_t*>(m, list); s; s = Field<uint8_t*>(s, 0x50))
        {
            const uint32_t n = Field<uint32_t>(s, 0x24);
            for (uint32_t i = 0; i < n; ++i)
                if (Field<uint8_t*>(s, 0x2c + i * 4) == entity)
                {
                    VCall<void>(s, 0x1c / 4);
                    break;
                }
        }
    };
    if (Field<uint32_t>(entity, 0x3ec) & 0x20000000u)
        notify(0x14);
    if (Field<uint32_t>(entity, 0x3ec) & 0x40000000u)
        notify(0xc);
    const uint32_t v = Field<uint32_t>(entity, 0x3ec) & 0x9fffffffu;
    Field<uint32_t>(entity, 0x3ec) = v;
    return v;
}

static uint32_t DecalManager_EntityChanged_Abi(uint32_t eax, uint32_t edi)
{
    return DecalManager_EntityChanged(static_cast<uint8_t*>(P(eax)), static_cast<uint8_t*>(P(edi)));
}
ABI_ENTRY_00416620(DecalManager_EntityChanged_Entry, DecalManager_EntityChanged_Abi)
HOOK(0x00416620, "DecalManager_EntityChanged", DecalManager_EntityChanged_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// 0x004166b0 (edx = object; callers 0x00417010 (decal), 0x00417990 (shadow)): base constructor shared by decals
// and shadows: vtable 0x005b964c, +4..+0x18 zeroed, then +0x1c..+0x28, +0x3c..+0x4c zeroed, +4 = 0x6333c, +8 = -1.
// Returns the object.
uint32_t DecalBase_Construct(uint8_t* o)
{
    Field<uint32_t>(o, 0) = 0x005b964c;
    for (uint32_t off = 4; off < 0x1c; off += 4)
        Field<uint32_t>(o, off) = 0;
    Field<uint32_t>(o, 0x20) = 0;
    Field<uint32_t>(o, 0x1c) = 0;
    Field<uint32_t>(o, 0x24) = 0;
    Field<uint32_t>(o, 0x40) = 0;
    Field<uint32_t>(o, 0x3c) = 0;
    Field<uint32_t>(o, 0x28) = 0;
    Field<uint32_t>(o, 0x4c) = 0;
    Field<uint32_t>(o, 0x44) = 0;
    Field<uint32_t>(o, 0x48) = 0;
    Field<uint32_t>(o, 4) = 0x6333c;
    Field<uint32_t>(o, 8) = 0xffffffffu;
    return u(o);
}

static uint32_t DecalBase_Construct_Abi(uint32_t edx)
{
    return DecalBase_Construct(static_cast<uint8_t*>(P(edx)));
}
ABI_ENTRY_004166b0(DecalBase_Construct_Entry, DecalBase_Construct_Abi)
HOOK(0x004166b0, "DecalBase_Construct", DecalBase_Construct_Entry, nullptr);

// 0x00416720 (ecx = object; returns nothing, EAX preserved): base destructor body: restores vtable 0x005b964c.
void DecalBase_Destruct(uint8_t* o)
{
    Field<uint32_t>(o, 0) = 0x005b964c;
}

static void DecalBase_Destruct_Abi(uint32_t ecx)
{
    DecalBase_Destruct(static_cast<uint8_t*>(P(ecx)));
}
ABI_ENTRY_00416720(DecalBase_Destruct_Entry, DecalBase_Destruct_Abi)
HOOK(0x00416720, "DecalBase_Destruct", DecalBase_Destruct_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// 0x00416740 (ecx = decal or shadow; stack: colour (8-byte record or null), tinted (byte); callee pops 8; caller
// 0x00418380). Draws the object's clipped geometry through the game's dynamic vertex buffer (Game+0x44):
//  - nothing when the vertex count +0x1c is 0; the record defaults to object +4;
//  - object virtual +0x24 (record) (set up), saved = renderer virtual +0x90 (0x10) (guess: set vertex format,
//    returns the previous one);
//  - p = buffer virtual +0x14 (0, 2, 0, 0) (begin), then buffer virtual +4 (count +0x20) = vertices (0x18 bytes:
//    x, y, z, colour, u, v); per index i (words at +0x48): position from +0x44 (16-byte {x, y, z, a}), uv from
//    +0x4c (+0x28 pairs per index); colour = Game+0x2704 when not tinted, else the record's bytes 4..7 (B, G, R, A)
//    / 255 (0x005bcc18) times 255 (0x005bcb34), B/G/R also times the vertex's a, truncated (_ftol 0x0056a7c8) and
//    packed A<<24 | R<<16 | G<<8 | B with the original's shift/or order; buffer virtual +0x1c (end);
//  - renderer virtual +0x90 (saved), object virtual +0x28 (), whose result is returned.
namespace
{
inline uint32_t Ftol(float v)
{
    return static_cast<uint32_t>(static_cast<int64_t>(v));
}
}  // namespace

uint32_t Decal_Render(uint8_t* obj, const uint8_t* colour, uint8_t tinted)
{
    if (!colour)
        colour = obj + 4;
    if (!Field<uint32_t>(obj, 0x1c))
        return 0;
    VCall<void>(obj, 0x24 / 4, colour);
    const uint32_t saved = VCall<uint32_t>(Renderer(), 0x90 / 4, 0x10u);
    void* batch = VCall<void*>(Field<void*>(g_game, 0x44), 0x14 / 4, 0u, 2u, 0u, 0u);
    if (batch)
    {
        const float k = GameGlobal<float>(0x005bcc18);
        const float c6 = static_cast<float>(static_cast<int32_t>(colour[6])) * k;
        const float c5 = static_cast<float>(static_cast<int32_t>(colour[5])) * k;
        const float c4 = static_cast<float>(static_cast<int32_t>(colour[4])) * k;  // the 2^32 fix-up never applies
        const float c7 = static_cast<float>(static_cast<int32_t>(colour[7])) * k;
        uint32_t* v = VCall<uint32_t*>(batch, 4 / 4, Field<uint32_t>(obj, 0x20));
        if (v)
        {
            for (uint32_t i = 0; i < Field<uint32_t>(obj, 0x20); ++i, v += 6)
            {
                const uint32_t idx = Field<uint16_t*>(obj, 0x48)[i];
                const float* src = reinterpret_cast<const float*>(Field<uint8_t*>(obj, 0x44) + idx * 16);
                const uint32_t* uv = reinterpret_cast<const uint32_t*>(Field<uint8_t*>(obj, 0x4c) +
                                                                       Field<uint32_t>(obj, 0x28) * idx * 8);
                std::memcpy(v, src, 12);
                uint32_t c;
                if (tinted)
                {
                    const float s = GameGlobal<float>(0x005bcb34);
                    c = Ftol(c6 * src[3] * s);
                    c |= Ftol(c7 * s) << 8;
                    c <<= 8;
                    c |= Ftol(c5 * src[3] * s);
                    c <<= 8;
                    c |= Ftol(c4 * src[3] * s);
                }
                else
                    c = Field<uint32_t>(g_game, 0x2704);
                v[3] = c;
                v[4] = uv[0];
                v[5] = uv[1];
            }
            VCall<void>(Field<void*>(g_game, 0x44), 0x1c / 4);
        }
    }
    VCall<void>(Renderer(), 0x90 / 4, saved);
    return VCall<uint32_t>(obj, 0x28 / 4);
}

static uint32_t Decal_Render_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return Decal_Render(static_cast<uint8_t*>(P(ecx)), static_cast<const uint8_t*>(P(a0)),
                        static_cast<uint8_t>(a1));
}
ABI_ENTRY_00416740(Decal_Render_Entry, Decal_Render_Abi)
HOOK(0x00416740, "Decal_Render", Decal_Render_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// Decal / shadow geometry builder (decal object: vertex count +0x1c, index count +0x20, vertex capacity +0x3c,
// index capacity +0x40, vertices +0x44 (16 bytes: x, y, z, w), indices +0x48 (words)). A projector is
// { +4 flags, +8 plane count, +0xc planes (16 bytes each), +0x10 point }.
//
// 0x00416930 (esi = decal; stack: submesh, projector; callee pops 8; caller 0x00416bf0). Returns at once when
// fewer than 3 vertices or indices are left. Triangle count: strip length (+0x10) - 2 when the submesh has a strip
// (+0x24), else index count (+0x20) / 3 (signed). With a strip, its box (+0x24 min, +0x30 max) is tested first: the
// submesh is skipped unless every plane has some corner with ((nz*z + nx*x) + ny*y) + d not below 0.0. Then each
// triangle (0x004efd00, ecx = submesh; stack: index, &polygon.v; false = degenerate) becomes the polygon
// {3, v[3]} clipped by Polygon_ClipPlanes against the projector's planes; a clipped n-gon is appended as a fan
// (vertices copied, indices (base, base+i-1, base+i) for i = 2..n-1; counts grow by n and 3n - 6). Running out of
// vertex or index room ends the whole function.
// Returns 0 here (the original's EAX is unused by the only caller: 0x00416d85 reloads it).
uint32_t DecalGeometry_AddSubmesh(uint8_t* decal, uint8_t* submesh, const uint8_t* projector)
{
    if (Field<uint32_t>(decal, 0x1c) > Field<uint32_t>(decal, 0x3c) - 3)
        return 0;
    if (Field<uint32_t>(decal, 0x20) > Field<uint32_t>(decal, 0x40) - 3)
        return 0;
    uint8_t* strip = Field<uint8_t*>(submesh, 0x24);
    uint32_t triangles;
    if (strip)
        triangles = Field<uint32_t>(strip, 0x10) - 2;
    else
        triangles = static_cast<uint32_t>(Field<int32_t>(submesh, 0x20) / 3);
    if (!triangles)
        return 0;
    if (strip)
    {
        const uint32_t planeCount = Field<uint32_t>(projector, 8);
        const float* planes = Field<const float*>(projector, 0xc);
        uint32_t mask = (1u << planeCount) - 1;
        bool visible = false;
        for (uint32_t c = 0; c < 8 && !visible; ++c)
        {
            const float x = Field<float>(strip, 0x24 + (c & 1) * 12);
            const float y = Field<float>(strip, 0x28 + ((c >> 1) & 1) * 12);
            const float z = Field<float>(strip, 0x2c + ((c >> 2) & 1) * 12);
            uint32_t bit = 1;
            for (uint32_t i = 0; i < planeCount; ++i, bit <<= 1)
            {
                if (!(mask & bit))
                    continue;
                const float* p = planes + i * 4;
                float d = (z * p[2] + x * p[0]) + y * p[1];
                d = d + p[3];
                if (!(d >= GameGlobal<float>(0x0056c05c)))  // C0 set: below or unordered
                    continue;
                mask ^= bit;
                if (!mask)
                {
                    visible = true;
                    break;
                }
            }
        }
        if (!visible)
            return 0;
    }
    uint32_t* vtx = reinterpret_cast<uint32_t*>(Field<uint8_t*>(decal, 0x44) + Field<uint32_t>(decal, 0x1c) * 16);
    uint16_t* idx = reinterpret_cast<uint16_t*>(Field<uint8_t*>(decal, 0x48)) + Field<uint32_t>(decal, 0x20);
    // The original's two adjacent 0xc4-byte polygons (count + 12 vertices each).
    alignas(8) uint32_t poly[0x31];
    alignas(8) uint32_t scratch[0x31];
    for (uint32_t t = 0; t < triangles; ++t)
    {
        if (!(orig::fn_004efd00(u(submesh), t, u(poly + 1)) & 0xff))
            continue;
        poly[0] = 3;
        const uint32_t* r = reinterpret_cast<const uint32_t*>(Polygon_ClipPlanes(
            Field<uint32_t>(projector, 8), scratch, poly, Field<const float*>(projector, 0xc)));
        if (!r)
            continue;
        const uint32_t n = r[0];
        if (Field<uint32_t>(decal, 0x1c) + n > Field<uint32_t>(decal, 0x3c))
            return 0;
        const uint32_t addIndices = n * 3 - 6;
        if (Field<uint32_t>(decal, 0x20) + addIndices > Field<uint32_t>(decal, 0x40))
            return 0;
        std::memcpy(vtx, r + 1, 16);
        std::memcpy(vtx + 4, r + 5, 16);
        vtx += 8;
        for (uint32_t i = 2; i < r[0]; ++i)
        {
            std::memcpy(vtx, r + 1 + i * 4, 16);
            const uint16_t base = static_cast<uint16_t>(Field<uint32_t>(decal, 0x1c));
            idx[0] = base;
            idx[1] = static_cast<uint16_t>(static_cast<uint16_t>(base + i) - 1);
            idx[2] = static_cast<uint16_t>(base + i);
            vtx += 4;
            idx += 3;
        }
        Field<uint32_t>(decal, 0x1c) += r[0];
        Field<uint32_t>(decal, 0x20) += addIndices;
    }
    return 0;
}

static uint32_t DecalGeometry_AddSubmesh_Abi(uint32_t esi, uint32_t a0, uint32_t a1)
{
    return DecalGeometry_AddSubmesh(static_cast<uint8_t*>(P(esi)), static_cast<uint8_t*>(P(a0)),
                                    static_cast<const uint8_t*>(P(a1)));
}
ABI_ENTRY_00416930(DecalGeometry_AddSubmesh_Entry, DecalGeometry_AddSubmesh_Abi)
HOOK(0x00416930, "DecalGeometry_AddSubmesh", DecalGeometry_AddSubmesh_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// 0x00416bf0 (eax = decal; stack: entity, projector; callee pops 8; caller 0x00416f50). Adds the entity's mesh to
// the decal: the projector's planes go to model space (each plane p -> M^T p with M = entity virtual +0x7c, the
// world matrix; the original's operand grouping is kept), and with flag 0x10 its point too (inverse 0x004e2b40
// (eax = M, ecx = out), then 0x004e3950 (eax = inverse, ecx = out, edx = 1; stack: point)). Every submesh of the
// entity's model (0x00453120 (eax = entity, edx = entity +0x374; stack: *0x00408800(4, entity)), +0x14 meshes at
// +0x18, each mesh +4: +0x80 submeshes of 0x2c bytes at +0x84) whose material (+0x7c, 0x64-byte entries, index
// submesh +0x10) lacks flag 0x10 at +0x34 goes through DecalGeometry_AddSubmesh. New vertices get w = 1.0, go back
// to world space (0x004e3950, eax = M, ecx = edx-count's array, edx = count; stack: the vertices) and get w = their
// distance to projector plane 5 (+0x50). The entity is recorded as a receiver (+0x2c, up to 4, count +0x24) and its
// +0x3ec gets the projector's flags (DecalManager_EntityChanged clears them). Returns EAX as the original.
uint32_t DecalGeometry_AddEntity(uint8_t* decal, uint8_t* entity, const uint8_t* projector)
{
    const float* m = VCall<const float*>(entity, 0x7c / 4);
    // Local copy of the projector (esp+0x24): +4 flags, +8 count, +0xc planes (here), +0x10 point (esp+0x34).
    alignas(8) uint32_t local[4 + 4];
    alignas(8) uint32_t inverse[17];
    alignas(8) float planes[6 * 4];
    local[2] = Field<uint32_t>(projector, 8);
    local[3] = u(planes);
    local[1] = Field<uint32_t>(projector, 4);
    if (local[1] & 0x10)
    {
        orig::fn_004e2b40(u(m), u(inverse));
        orig::fn_004e3950(u(inverse), u(&local[4]), 1, u(projector) + 0x10);
    }
    const float* src = Field<const float*>(projector, 0xc);
    float* dst = planes;
    for (uint32_t n = local[2]; n; --n, src += 4, dst += 4)
    {
        dst[0] = ((m[12] * src[3] + m[0] * src[0]) + m[4] * src[1]) + m[8] * src[2];
        dst[1] = ((src[3] * m[13] + m[5] * src[1]) + m[9] * src[2]) + m[1] * src[0];
        dst[2] = ((m[14] * src[3] + src[1] * m[6]) + m[10] * src[2]) + m[2] * src[0];
        dst[3] = ((src[1] * m[7] + src[3] * m[15]) + m[3] * src[0]) + src[2] * m[11];
    }
    const uint32_t first = Field<uint32_t>(decal, 0x1c);
    const uint32_t lod = Field<uint32_t>(P(orig::fn_00408800(4, u(entity))), 0);
    uint8_t* list = static_cast<uint8_t*>(P(orig::fn_00453120(u(entity), Field<uint32_t>(entity, 0x374), lod)));
    for (uint32_t i = 0; i < Field<uint32_t>(list, 0x14); ++i)
    {
        uint8_t* mesh = Field<uint8_t*>(Field<uint8_t*>(list, 0x18 + i * 4), 4);
        for (uint32_t j = 0; j < Field<uint32_t>(mesh, 0x80); ++j)
        {
            uint8_t* sub = Field<uint8_t*>(mesh, 0x84) + j * 0x2c;
            if (!(Field<uint8_t>(Field<uint8_t*>(mesh, 0x7c) + Field<uint32_t>(sub, 0x10) * 0x64, 0x34) & 0x10))
                DecalGeometry_AddSubmesh(decal, sub, reinterpret_cast<const uint8_t*>(local));
        }
    }
    const uint32_t added = Field<uint32_t>(decal, 0x1c) - first;
    if (!added)
        return Field<uint32_t>(decal, 0x1c);
    for (uint32_t k = first; k < Field<uint32_t>(decal, 0x1c); ++k)
        Field<uint32_t>(Field<uint8_t*>(decal, 0x44) + k * 16, 0xc) = 0x3f800000;
    const uint32_t verts = u(Field<uint8_t*>(decal, 0x44) + first * 16);
    orig::fn_004e3950(u(m), verts, added, verts);
    for (uint32_t k = first; k < Field<uint32_t>(decal, 0x1c); ++k)
    {
        const float* p = Field<const float*>(projector, 0xc) + 0x50 / 4;
        float* v = reinterpret_cast<float*>(Field<uint8_t*>(decal, 0x44) + k * 16);
        float w = (v[2] * p[2] + v[1] * p[1]) + v[0] * p[0];
        v[3] = w + p[3];
    }
    const uint32_t receivers = Field<uint32_t>(decal, 0x24);
    if (receivers >= 4)
        return receivers;
    Field<uint32_t>(decal, 0x2c + receivers * 4) = u(entity);
    ++Field<uint32_t>(decal, 0x24);
    const uint32_t flags = Field<uint32_t>(projector, 4);
    Field<uint32_t>(entity, 0x3ec) |= flags;
    return flags;
}

static uint32_t DecalGeometry_AddEntity_Abi(uint32_t eax, uint32_t a0, uint32_t a1)
{
    return DecalGeometry_AddEntity(static_cast<uint8_t*>(P(eax)), static_cast<uint8_t*>(P(a0)),
                                   static_cast<const uint8_t*>(P(a1)));
}
ABI_ENTRY_00416bf0(DecalGeometry_AddEntity_Entry, DecalGeometry_AddEntity_Abi)
HOOK(0x00416bf0, "DecalGeometry_AddEntity", DecalGeometry_AddEntity_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// 0x00416e90 (edi = entity; ecx: listed by the ABI but only pushed to make a stack slot; callers 0x00416f50,
// 0x00418190). Cached decal-exclusion test on entity +0x3ec (true: Decals_ProjectOnEntity skips it): bits 0xc0000000 already set -> returns
// bit 30. Otherwise scans the entity's current LOD (0x004555d0 (eax = entity) +0x10: +0x28 slots per LOD, slot
// array +0x38; LOD = entity virtual +0x40) for a mesh list (+0x14 meshes at +0x18) with some mesh +8 non-zero:
// found -> clears 0x80000018 and sets 0x40000000, returns 1; none -> sets 0x80000000, returns 0 (in AL; EAX upper
// bytes as the original: the new flags word).
uint32_t Entity_DecalExcluded(uint8_t* entity)
{
    const uint32_t f = Field<uint32_t>(entity, 0x3ec);
    if (f & 0xc0000000u)
        return (f >> 30) & 1;
    uint8_t* lods = static_cast<uint8_t*>(P(orig::fn_004555d0(u(entity))));
    const uint32_t lod = VCall<uint32_t>(entity, 0x40 / 4);
    uint8_t* table = Field<uint8_t*>(lods, 0x10);
    const uint32_t slots = Field<uint32_t>(table, 0x28);
    uint8_t** slot = reinterpret_cast<uint8_t**>(Field<uint8_t*>(table, 0x38) + slots * lod * 4);
    for (uint32_t i = 0; i < slots; ++i, ++slot)
    {
        uint8_t* list = *slot;
        if (!list)
            continue;
        const int32_t n = Field<int32_t>(list, 0x14);
        for (int32_t k = 0; k < n; ++k)
            if (Field<uint32_t>(Field<uint8_t*>(list, 0x18 + k * 4), 8))
            {
                const uint32_t v = Field<uint32_t>(entity, 0x3ec) & 0x7fffffe7u;
                Field<uint32_t>(entity, 0x3ec) = v;
                Field<uint32_t>(entity, 0x3ec) = v | 0x40000000u;
                return ((v | 0x40000000u) & 0xffffff00u) | 1u;
            }
    }
    const uint32_t v = Field<uint32_t>(entity, 0x3ec) | 0x80000000u;
    Field<uint32_t>(entity, 0x3ec) = v;
    return v & 0xffffff00u;
}

static uint32_t Entity_DecalExcluded_Abi(uint32_t /*ecx*/, uint32_t edi)
{
    return Entity_DecalExcluded(static_cast<uint8_t*>(P(edi)));
}
ABI_ENTRY_00416e90(Entity_DecalExcluded_Entry, Entity_DecalExcluded_Abi)
HOOK(0x00416e90, "Entity_DecalExcluded", Entity_DecalExcluded_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// 0x00416f50 (cdecl: entity, projector; called directly by 0x00417260 and as the per-entity callback of the world
// walk 0x0048eb60, which ignores the result). Projects only when the entity's +0x3ec shares a bit with the
// projector's flags, it is not excluded (bit 31 set skips the test; otherwise Entity_DecalExcluded (ecx = projector
// flags, not read; edi = entity) true stops), and 0x00418610 (eax = entity) is false (true clears 0x18 in +0x3ec and
// stops). Then the entity's bounding sphere (entity virtual +0x90 (&centre, &radius, 1)) must not lie entirely behind any projector
// plane (((cz*nz + cy*ny) + cx*nx) + d < -radius culls); then DecalGeometry_AddEntity (eax = projector +0 (the
// decal); entity, projector). The radius is kept in the first argument's slot and -radius in the second's in the
// original (not reproduced: the caller's copies are not reused). Returns EAX roughly as the original.
uint32_t Decals_ProjectOnEntity(uint8_t* entity, const uint8_t* projector)
{
    const uint32_t f = Field<uint32_t>(entity, 0x3ec);
    if (!(f & Field<uint32_t>(projector, 4)))
        return f;
    if (static_cast<int32_t>(f) >= 0)
    {
        const uint32_t r = Entity_DecalExcluded(entity);
        if (r & 0xff)
            return r;
    }
    const uint32_t r = orig::fn_00418610(u(entity));
    if (r & 0xff)
    {
        Field<uint32_t>(entity, 0x3ec) &= 0xffffffe7u;
        return r;
    }
    float centre[3];
    float radius;
    VCall<void>(entity, 0x90 / 4, static_cast<void*>(centre), static_cast<void*>(&radius), 1u);
    const uint32_t n = Field<uint32_t>(projector, 8);
    if (n)
    {
        const float neg = -radius;
        const float* p = Field<const float*>(projector, 0xc);
        for (uint32_t i = 0; i < n; ++i, p += 4)
        {
            float d = (centre[2] * p[2] + centre[1] * p[1]) + centre[0] * p[0];
            d = d + p[3];
            if (d < neg)  // jnp after test ah,5: only "less" (C0 alone) culls
                return 0;
        }
    }
    return DecalGeometry_AddEntity(Field<uint8_t*>(projector, 0), entity, projector);
}

static uint32_t Decals_ProjectOnEntity_Abi(uint32_t a0, uint32_t a1)
{
    return Decals_ProjectOnEntity(static_cast<uint8_t*>(P(a0)), static_cast<const uint8_t*>(P(a1)));
}
ABI_ENTRY_00416f50(Decals_ProjectOnEntity_Entry, Decals_ProjectOnEntity_Abi)
HOOK(0x00416f50, "Decals_ProjectOnEntity", Decals_ProjectOnEntity_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// Decal (0xf6c bytes, vtable 0x005b9690): intrusive list links +0x50 (next) and +0x54 (address of the pointer that
// points at it), sort key +0xc, flags +0xf58 (active) / +0xf59, age +0xf5c, lifetime +0xf60, fade left +0xf64,
// fade total +0xf68 (decals.cpp), storage: vertices +0x58 (128), uvs +0x858, indices +0xc58 (384).
namespace
{
void Decal_Unlink(uint8_t* d)
{
    uint8_t* next = Field<uint8_t*>(d, 0x50);
    if (next)
        Field<uint32_t>(next, 0x54) = Field<uint32_t>(d, 0x54);
    uint8_t** pprev = Field<uint8_t**>(d, 0x54);
    if (pprev)
        *pprev = Field<uint8_t*>(d, 0x50);
}

void Decal_LinkAt(uint8_t* d, uint8_t** at)
{
    Field<uint8_t*>(d, 0x50) = *at;
    Field<uint8_t**>(d, 0x54) = at;
    *at = d;
}
}  // namespace

// 0x00417010 (edx = decal, esi = list head address (manager +8, the free list); caller Decals_AllocatePool):
// constructor: DecalBase_Construct, storage pointers, vtable 0x005b9690, capacities +0x3c = 0x80 vertices, +0x40 =
// 0x180 indices, +0x28 = 1 uv set, state cleared, the manager's allocated count (+0x18) up, linked at the head of
// the list (unlinked first: a no-op on a fresh object). Returns the decal.
uint32_t Decal_Construct(uint8_t* d, uint8_t** list)
{
    DecalBase_Construct(d);
    Field<uint32_t>(d, 0x4c) = u(d) + 0x858;
    Field<uint32_t>(d, 0x44) = u(d) + 0x58;
    Field<uint32_t>(d, 0x48) = u(d) + 0xc58;
    Field<uint32_t>(d, 0x50) = 0;
    Field<uint32_t>(d, 0) = 0x005b9690;
    Field<uint32_t>(d, 0x54) = 0;
    Field<uint32_t>(d, 0x3c) = 0x80;
    Field<uint32_t>(d, 0x40) = 0x180;
    Field<uint32_t>(d, 0x28) = 1;
    Field<uint32_t>(d, 0xf64) = 0;
    Field<uint32_t>(d, 0xf68) = 0;
    Field<uint8_t>(d, 0xf58) = 0;
    Field<uint8_t>(d, 0xf59) = 0;
    Field<uint32_t>(d, 0xf60) = 0;
    ++Field<uint32_t>(Field<void*>(g_game, 0xa7c), 0x18);
    Decal_Unlink(d);
    if (list)
    {
        Decal_LinkAt(d, list);
        if (Field<uint8_t*>(d, 0x50))
            Field<uint32_t>(Field<uint8_t*>(d, 0x50), 0x54) = u(d) + 0x50;
    }
    else
    {
        Field<uint32_t>(d, 0x50) = 0;
        Field<uint32_t>(d, 0x54) = 0;
    }
    return u(d);
}

static uint32_t Decal_Construct_Abi(uint32_t edx, uint32_t esi)
{
    return Decal_Construct(static_cast<uint8_t*>(P(edx)), static_cast<uint8_t**>(P(esi)));
}
ABI_ENTRY_00417010(Decal_Construct_Entry, Decal_Construct_Abi)
HOOK(0x00417010, "Decal_Construct", Decal_Construct_Entry, nullptr);

// 0x004170e0 (ecx = decal; caller 0x004170c0): destructor body: vtable 0x005b9690, unlink, links cleared, then
// (tail jump) DecalBase_Destruct. Returns the old +0x54 (EAX at the tail jump).
uint32_t Decal_Destruct(uint8_t* d)
{
    Field<uint32_t>(d, 0) = 0x005b9690;
    Decal_Unlink(d);
    const uint32_t ret = Field<uint32_t>(d, 0x54);
    Field<uint32_t>(d, 0x50) = 0;
    Field<uint32_t>(d, 0x54) = 0;
    DecalBase_Destruct(d);
    return ret;
}

static uint32_t Decal_Destruct_Abi(uint32_t ecx)
{
    return Decal_Destruct(static_cast<uint8_t*>(P(ecx)));
}
ABI_ENTRY_004170e0(Decal_Destruct_Entry, Decal_Destruct_Abi)
HOOK(0x004170e0, "Decal_Destruct", Decal_Destruct_Entry, nullptr);

// 0x004170c0 (thiscall, 1 arg: flags; callee pops 4; the decal vtable's deleting destructor, slot +0x18):
// Decal_Destruct, then when flags bit 0 the memory goes back to the heap 0x0063a210 (0x004e08d0). Returns the decal.
uint32_t Decal_DeletingDestruct(uint8_t* d, uint32_t flags)
{
    Decal_Destruct(d);
    if (flags & 1)
        orig::fn_004e08d0(0x0063a210, u(d));
    return u(d);
}

static uint32_t Decal_DeletingDestruct_Abi(uint32_t ecx, uint32_t a0)
{
    return Decal_DeletingDestruct(static_cast<uint8_t*>(P(ecx)), a0);
}
ABI_ENTRY_004170c0(Decal_DeletingDestruct_Entry, Decal_DeletingDestruct_Abi)
HOOK(0x004170c0, "Decal_DeletingDestruct", Decal_DeletingDestruct_Entry, nullptr);

// 0x00417110 (ecx = decal; callers Decals_Spawn, 0x00417180). Recycles an active decal: counts and receivers
// cleared, the manager's fading count (+0x20) down when it was fading (+0xf68 != 0, NaN included), active count
// (+0x1c) down, inactive, then (tail jump) 0x00418510 (ecx = decal, edx = manager +8) moves it to the free list.
// Returns that call's EAX, or 0 when the decal was not active (the original: AL = 0; callers ignore it).
uint32_t Decal_Recycle(uint8_t* d)
{
    if (!Field<uint8_t>(d, 0xf58))
        return 0;
    Field<uint32_t>(d, 0x24) = 0;
    Field<uint32_t>(d, 0x20) = 0;
    Field<uint32_t>(d, 0x1c) = 0;
    Field<uint32_t>(d, 0x2c) = 0;
    Field<uint32_t>(d, 0x30) = 0;
    Field<uint32_t>(d, 0x34) = 0;
    Field<uint32_t>(d, 0x38) = 0;
    if (!(Field<float>(d, 0xf68) == GameGlobal<float>(0x0056c05c)))
    {
        Field<uint32_t>(d, 0xf68) = 0;
        --Field<int32_t>(Field<void*>(g_game, 0xa7c), 0x20);
    }
    --Field<int32_t>(Field<void*>(g_game, 0xa7c), 0x1c);
    Field<uint8_t>(d, 0xf58) = 0;
    return orig::fn_00418510(u(d), Field<uint32_t>(g_game, 0xa7c) + 8);
}

static uint32_t Decal_Recycle_Abi(uint32_t ecx)
{
    return Decal_Recycle(static_cast<uint8_t*>(P(ecx)));
}
ABI_ENTRY_00417110(Decal_Recycle_Entry, Decal_Recycle_Abi)
HOOK(0x00417110, "Decal_Recycle", Decal_Recycle_Entry, nullptr);

// 0x00417180 (esi = decal; stack: frame time; callee pops 4; ecx only pushed as a stack slot; caller
// Decals_Update). When fading (+0xf68 != 0): fade left (+0xf64) -= dt; with +0xf59 clear and fade left still
// above 0 (or NaN), the colour's alpha (+8 top byte) = _ftol(left / total * 255); otherwise Decal_Recycle and
// return. When not fading and it has a lifetime (+0xf60 != 0) that the age (+0xf5c) has reached, 0x00418640 (ecx =
// decal; stack: the manager's fade length +0x24) starts the fade. Finally the age grows by dt unless it is below 0
// (or NaN). Returns 0 here (the caller ignores it).
uint32_t Decal_Update(uint8_t* d, float dt)
{
    const float zero = GameGlobal<float>(0x0056c05c);
    if (!(Field<float>(d, 0xf68) == zero))
    {
        const float left = Field<float>(d, 0xf64) - dt;
        Field<float>(d, 0xf64) = left;
        if (Field<uint8_t>(d, 0xf59) || left <= zero)
        {
            Decal_Recycle(d);
            return 0;
        }
        const uint32_t alpha = Ftol(left / Field<float>(d, 0xf68) * GameGlobal<float>(0x005bcb34));
        Field<uint32_t>(d, 8) = (alpha << 24) | (Field<uint32_t>(d, 8) & 0xffffffu);
    }
    else if (!(Field<float>(d, 0xf60) == zero) && Field<float>(d, 0xf5c) >= Field<float>(d, 0xf60))
        orig::fn_00418640(u(d), Field<uint32_t>(Field<void*>(g_game, 0xa7c), 0x24));
    if (Field<float>(d, 0xf5c) >= zero)
        Field<float>(d, 0xf5c) = dt + Field<float>(d, 0xf5c);
    return 0;
}

static uint32_t Decal_Update_Abi(uint32_t /*ecx*/, uint32_t esi, uint32_t a0)
{
    float dt;
    std::memcpy(&dt, &a0, 4);
    return Decal_Update(static_cast<uint8_t*>(P(esi)), dt);
}
ABI_ENTRY_00417180(Decal_Update_Entry, Decal_Update_Abi)
HOOK(0x00417180, "Decal_Update", Decal_Update_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// 0x00417260 (eax = the decal's placement matrix (0x44 bytes: rows at +0, +0x10, +0x20; column 3 = the position);
// stack: decal, half size (float), a2, entity (or 0), single (byte); callee pops 0x14; caller Decals_Spawn). Builds
// a decal:
//  - counts and receivers cleared, +8 = -1 (colour), sort key +0xc = 0x004881c0 (ecx = a2, esi = Game+0xa78; it
//    reads no stack arguments), fade cleared;
//  - projector (esp+0x10) {decal, flags 0x40000008, 6 planes, planes, point = column 3 of the matrix}: the
//    box around the position, planes (axis, size - n.p) and (-axis, size - n.p) for each of the three axes (-1.0 at
//    0x005bcb5c), n.p computed as (n.z*p.z + n.y*p.y) + n.x*p.x;
//  - with an entity: Decals_ProjectOnEntity(entity, projector); otherwise the world is gathered against the planes
//    (0x0048eab0 World_GatherFrustum: eax = 6, ecx = planes, ebx = 0x608-byte result; stack: Game+0xaa0) and walked
//    (0x0048eb60: Game+0xaa0, result, 0x00449010 (the static-world callback), 0x00416f50 unless `single`, projector);
//  - no vertices -> returns 0 (AL);
//  - 0x004175c0 (eax = projector; stack: decal, size) (texture coordinates, guess), then the decal is moved into
//    the manager's active list (+0xc) before the first decal with a larger key; age, fade cleared; active count
//    (+0x1c) up; active flag +0xf58 = 1; returns 1 (AL; upper bytes: the next decal's address as the original).
uint32_t Decal_Build(const uint32_t* matrix, uint8_t* decal, float size, uint32_t a2, uint32_t entity,
                     uint8_t single)
{
    Field<uint32_t>(decal, 0x24) = 0;
    Field<uint32_t>(decal, 0x20) = 0;
    Field<uint32_t>(decal, 0x1c) = 0;
    Field<uint32_t>(decal, 0x2c) = 0;
    Field<uint32_t>(decal, 0x30) = 0;
    Field<uint32_t>(decal, 0x34) = 0;
    Field<uint32_t>(decal, 0x38) = 0;
    Field<uint32_t>(decal, 8) = 0xffffffffu;
    Field<uint32_t>(decal, 0xc) = orig::fn_004881c0(a2, Field<uint32_t>(g_game, 0xa78), 0, 0, 0, 0, 0, 0, 0, 0);
    Field<uint32_t>(decal, 0xf64) = 0;
    Field<uint32_t>(decal, 0xf68) = 0;

    // esp+0x10 .. esp+0x8f of the original frame: projector, point, six planes.
    alignas(8) uint32_t proj[8];
    alignas(8) float planes[6 * 4];
    const float* m = reinterpret_cast<const float*>(matrix);
    float* point = reinterpret_cast<float*>(&proj[4]);
    std::memcpy(&point[0], &matrix[3], 4);
    std::memcpy(&point[1], &matrix[7], 4);
    std::memcpy(&point[2], &matrix[11], 4);
    std::memcpy(&point[3], &matrix[15], 4);
    proj[3] = u(planes);
    proj[0] = u(decal);
    proj[1] = 0x40000008;
    proj[2] = 6;
    const float neg = GameGlobal<float>(0x005bcb5c);
    for (int axis = 0; axis < 3; ++axis)
    {
        float* a = planes + axis * 8;
        float* b = a + 4;
        a[0] = m[axis];
        a[1] = m[4 + axis];
        a[2] = m[8 + axis];
        a[3] = size - ((a[2] * point[2] + a[1] * point[1]) + a[0] * point[0]);
        b[0] = m[axis] * neg;
        b[1] = m[4 + axis] * neg;
        b[2] = m[8 + axis] * neg;
        b[3] = size - ((b[2] * point[2] + b[1] * point[1]) + b[0] * point[0]);
    }
    if (entity)
        Decals_ProjectOnEntity(static_cast<uint8_t*>(P(entity)), reinterpret_cast<const uint8_t*>(proj));
    else
    {
        alignas(8) uint8_t gathered[0x608];
        orig::fn_0048eab0(6, u(planes), u(gathered), Field<uint32_t>(g_game, 0xaa0));
        orig::fn_0048eb60(Field<uint32_t>(g_game, 0xaa0), u(gathered), 0x00449010u, single ? 0u : 0x00416f50u,
                          u(proj));
    }
    if (!Field<uint32_t>(decal, 0x1c))
        return 0;
    float sizeArg = size;
    uint32_t sizeBits;
    std::memcpy(&sizeBits, &sizeArg, 4);
    orig::fn_004175c0(u(proj), u(decal), sizeBits);
    uint8_t* manager = Field<uint8_t*>(g_game, 0xa7c);
    uint8_t** at = reinterpret_cast<uint8_t**>(manager + 0xc);
    if (*at)
    {
        const uint32_t key = Field<uint32_t>(decal, 0xc);
        for (uint8_t* c = *at; c && !(Field<uint32_t>(c, 0xc) > key); c = *at)
            at = reinterpret_cast<uint8_t**>(c + 0x50);
    }
    Field<uint32_t>(decal, 0xf5c) = 0;
    Field<uint32_t>(decal, 0xf68) = 0;
    Field<uint32_t>(decal, 0xf64) = 0;
    ++Field<uint32_t>(Field<void*>(g_game, 0xa7c), 0x1c);
    Decal_Unlink(decal);
    Decal_LinkAt(decal, at);
    uint8_t* next = Field<uint8_t*>(decal, 0x50);
    if (next)
        Field<uint32_t>(next, 0x54) = u(decal) + 0x50;
    Field<uint8_t>(decal, 0xf58) = 1;
    return (u(next) & 0xffffff00u) | 1u;
}

static uint32_t Decal_Build_Abi(uint32_t eax, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    float size;
    std::memcpy(&size, &a1, 4);
    return Decal_Build(static_cast<const uint32_t*>(P(eax)), static_cast<uint8_t*>(P(a0)), size, a2, a3,
                       static_cast<uint8_t>(a4));
}
ABI_ENTRY_00417260(Decal_Build_Entry, Decal_Build_Abi)
HOOK(0x00417260, "Decal_Build", Decal_Build_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// 0x004175c0 (eax = projector (Decal_Build's); stack: decal, half size; callee pops 8; caller Decal_Build).
// Texture coordinates of the decal's vertices. Three axis planes of the projector (planes 0, 2, 4) are scaled by
// 1 / (2 * size) (0x005bcb2c) with 0.5 (0x005bcb4c) taken off d. Over all vertices the range (max - min, minima
// starting at 1.0, maxima at -1.0) of each planar coordinate is measured; the planes are ordered so that the two
// with the largest ranges come first (swap U/V if U < V, V/W if V < W, U/V again if U < V, comparing the ranges as
// the original does: the second U/V test uses U's range from the first step). Then per vertex: (u, v) from the
// first two planes, scaled by sqrt(u*u + v*v + w*w) / sqrt(u*u + v*v) when u*u + v*v is not 0, plus 0.5; uv
// stride = +0x28 pairs. Returns EAX as the original leaves it.
uint32_t Decal_ComputeTexCoords(const uint8_t* projector, uint8_t* decal, float size)
{
    const float* src = Field<const float*>(projector, 0xc);
    const float inv = GameGlobal<float>(0x005bcb2c) / (size + size);
    const float half = GameGlobal<float>(0x005bcb4c);
    float pl[3][4];
    for (int k = 0; k < 3; ++k)
    {
        const float* p = src + k * 8;
        pl[k][0] = inv * p[0];
        pl[k][1] = inv * p[1];
        pl[k][2] = inv * p[2];
        pl[k][3] = inv * p[3] - half;
    }
    float minU = 1.0f, minV = 1.0f, minW = 1.0f, maxU = -1.0f, maxW = -1.0f;
    float maxV = GameGlobal<float>(0x005bcb5c);  // -1.0, kept on the x87 stack in the original
    const uint32_t count = Field<uint32_t>(decal, 0x1c);
    const float* v = Field<const float*>(decal, 0x44);
    auto dot = [](const float* p, const float* q) { return ((p[2] * q[2] + p[1] * q[1]) + p[0] * q[0]) + p[3]; };
    for (uint32_t i = 0; i < count; ++i, v += 4)
    {
        const float a = dot(pl[0], v);
        minU = a < minU ? a : minU;
        maxU = a > maxU ? a : maxU;
        const float b = dot(pl[1], v);
        minV = b < minV ? b : minV;
        maxV = b > maxV ? b : maxV;
        const float c = dot(pl[2], v);
        minW = c < minW ? c : minW;
        maxW = c > maxW ? c : maxW;
    }
    float rangeU = maxU - minU;
    float rangeV = maxV - minV;
    const float rangeW = maxW - minW;
    float tmp[4];
    if (rangeU < rangeV)
    {
        std::memcpy(tmp, pl[0], 16), std::memcpy(pl[0], pl[1], 16), std::memcpy(pl[1], tmp, 16);
        const float t = rangeU;
        rangeU = rangeV;
        rangeV = t;
    }
    if (rangeV < rangeW)
    {
        std::memcpy(tmp, pl[1], 16), std::memcpy(pl[1], pl[2], 16), std::memcpy(pl[2], tmp, 16);
        rangeV = rangeW;
    }
    if (rangeU < rangeV)
        std::memcpy(tmp, pl[0], 16), std::memcpy(pl[0], pl[1], 16), std::memcpy(pl[1], tmp, 16);

    if (!count)
        return 0;
    v = Field<const float*>(decal, 0x44);
    float* uv = Field<float*>(decal, 0x4c);
    uint32_t ret = 0;
    for (uint32_t i = 0; i < Field<uint32_t>(decal, 0x1c); ++i, v += 4)
    {
        uv[0] = dot(pl[0], v);
        const float b = dot(pl[1], v);
        uv[1] = b;
        const float a = uv[0];
        const float s = a * a + b * b;
        if (!(s == GameGlobal<float>(0x0056c05c)))
        {
            const float c = dot(pl[2], v);
            const float l3 = sqrtf(c * c + s);
            const float l2 = sqrtf(s);
            const float f = l3 * (GameGlobal<float>(0x005bcb2c) / l2);
            uv[0] = f * uv[0];
            uv[1] = f * uv[1];
        }
        uv[0] = uv[0] + half;
        uv[1] = uv[1] + half;
        ret = Field<uint32_t>(decal, 0x28);
        uv += ret * 2;
    }
    return ret;
}

static uint32_t Decal_ComputeTexCoords_Abi(uint32_t eax, uint32_t a0, uint32_t a1)
{
    float size;
    std::memcpy(&size, &a1, 4);
    return Decal_ComputeTexCoords(static_cast<const uint8_t*>(P(eax)), static_cast<uint8_t*>(P(a0)), size);
}
ABI_ENTRY_004175c0(Decal_ComputeTexCoords_Entry, Decal_ComputeTexCoords_Abi)
HOOK(0x004175c0, "Decal_ComputeTexCoords", Decal_ComputeTexCoords_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// Shadow (0x59b0 bytes, vtable 0x005b96bc; shares DecalBase and the list links +0x50/+0x54 with decals): owner
// entity +0x58 (entity +0x3e0 points back), render target object embedded at +0x5c (vtable 0x005b96e8, size
// +0x6c/+0x70), flags +0x118 (needs render) / +0x119 (target valid), two 0x44-byte matrices at +0x80 and +0xc4,
// light camera parameters +0x198.., vertices +0x1b0 (0x400), indices +0x41b0 (0xc00).
//
// 0x00417990 (edx = shadow; stack: list head address (manager +0x10, the free shadows), texture size; callee pops
// 8; caller Shadows_AllocatePool): constructor. +0x1a4 = 1 / tan(0.0436 rad) computed as the original (fptan on
// the double at 0x005bcd68, then fdivr by 1.0f under the game's single-precision x87 setting); +0x198 = 50.0,
// +0x19c = 150.0, +0x1a0 = 0.0873; colour +8 = 0xff404040, +4 = 0x32a4; linked at the head of the list.
uint32_t Shadow_Construct(uint8_t* s, uint8_t** list, uint32_t size)
{
    DecalBase_Construct(s);
    float cot;
    __asm {
        fld qword ptr ds:[0x005bcd68]
        fptan
        fstp st(0)
        fdivr dword ptr ds:[0x005bcb2c]
        fstp cot
    }
    Field<uint32_t>(s, 0x1a8) = 0;
    Field<uint32_t>(s, 0x50) = 0;
    Field<uint32_t>(s, 0) = 0x005b96bc;
    Field<uint32_t>(s, 0x54) = 0;
    Field<uint32_t>(s, 0x58) = 0;
    Field<uint32_t>(s, 0x11c) = 0;
    Field<uint32_t>(s, 0x3c) = 0x400;
    Field<uint32_t>(s, 0x40) = 0xc00;
    Field<uint32_t>(s, 0x28) = 1;
    Field<uint32_t>(s, 0x1ac) = 0;
    Field<uint32_t>(s, 0x44) = u(s) + 0x1b0;
    Field<uint32_t>(s, 0x4c) = u(s) + 0x1a8;
    Field<uint32_t>(s, 0x48) = u(s) + 0x41b0;
    for (uint32_t off = 0x5c; off < 0x80; off += 4)
        Field<uint32_t>(s, off) = 0;
    Field<uint32_t>(s, 0x70) = size;
    Field<uint32_t>(s, 0x6c) = size;
    Field<uint32_t>(s, 0x5c) = 0x005b96e8;
    Field<uint32_t>(s, 0xc) = u(s) + 0x5c;
    Field<uint8_t>(s, 0x7d) = 0;
    Field<uint8_t>(s, 0x7c) = 0x7f;
    Field<uint16_t>(s, 0x7e) = 0x80;
    Field<uint32_t>(s, 4) = 0x32a4;
    Field<uint32_t>(s, 8) = 0xff404040u;
    Field<uint32_t>(s, 0x78) = 8;
    Field<uint32_t>(s, 0x74) = 8;
    static const uint32_t matrices[2] = {0x80, 0xc4};
    for (uint32_t base : matrices)
    {
        for (uint32_t i = 0; i < 0x11; ++i)
            Field<uint32_t>(s, base + i * 4) = 0;
        Field<uint32_t>(s, base) = 0x3f800000;
        Field<uint32_t>(s, base + 0x14) = 0x3f800000;
        Field<uint32_t>(s, base + 0x28) = 0x3f800000;
        Field<uint32_t>(s, base + 0x3c) = 0x3f800000;
    }
    Field<uint32_t>(s, 0x114) = 0x3f800000;
    Field<uint32_t>(s, 0x110) = 0;
    Field<uint32_t>(s, 0x10c) = 0;
    Field<uint32_t>(s, 0x108) = 0;
    Field<uint32_t>(s, 0x1a0) = 0x3db2b8c2;
    Field<uint32_t>(s, 0x198) = 0x42480000;
    Field<uint32_t>(s, 0x19c) = 0x43160000;
    Field<float>(s, 0x1a4) = cot;
    Decal_Unlink(s);
    if (list)
    {
        Decal_LinkAt(s, list);
        if (Field<uint8_t*>(s, 0x50))
            Field<uint32_t>(Field<uint8_t*>(s, 0x50), 0x54) = u(s) + 0x50;
    }
    else
    {
        Field<uint32_t>(s, 0x50) = 0;
        Field<uint32_t>(s, 0x54) = 0;
    }
    return u(s);
}

static uint32_t Shadow_Construct_Abi(uint32_t edx, uint32_t a0, uint32_t a1)
{
    return Shadow_Construct(static_cast<uint8_t*>(P(edx)), static_cast<uint8_t**>(P(a0)), a1);
}
ABI_ENTRY_00417990(Shadow_Construct_Entry, Shadow_Construct_Abi)
HOOK(0x00417990, "Shadow_Construct", Shadow_Construct_Entry, nullptr);

// 0x00417b90 (ecx = shadow, edx = entity or 0; callers Shadows_Attach, 0x00417b40, 0x004547b0, 0x00455c10). Sets the
// shadow's owner: nothing when unchanged (returns the owner). Otherwise target invalid (+0x119 = 0), needs render
// (+0x118 = 1), the old owner's +0x3e0 cleared, the new owner linked both ways, and (tail jump) 0x00418510 moves
// the shadow to the manager's active list (+0x14) when owned, else to the free list (+0x10). Returns that EAX.
uint32_t Shadow_SetOwner(uint8_t* s, uint8_t* entity)
{
    uint8_t* old = Field<uint8_t*>(s, 0x58);
    if (old == entity)
        return u(old);
    Field<uint8_t>(s, 0x119) = 0;
    Field<uint8_t>(s, 0x118) = 1;
    if (old)
    {
        Field<uint32_t>(old, 0x3e0) = 0;
        Field<uint32_t>(s, 0x58) = 0;
    }
    if (entity)
    {
        Field<uint32_t>(s, 0x58) = u(entity);
        Field<uint32_t>(entity, 0x3e0) = u(s);
    }
    const uint32_t list = Field<uint32_t>(g_game, 0xa7c) + (Field<uint32_t>(s, 0x58) ? 0x14u : 0x10u);
    return orig::fn_00418510(u(s), list);
}

static uint32_t Shadow_SetOwner_Abi(uint32_t ecx, uint32_t edx)
{
    return Shadow_SetOwner(static_cast<uint8_t*>(P(ecx)), static_cast<uint8_t*>(P(edx)));
}
ABI_ENTRY_00417b90(Shadow_SetOwner_Entry, Shadow_SetOwner_Abi)
HOOK(0x00417b90, "Shadow_SetOwner", Shadow_SetOwner_Entry, nullptr);

// 0x00417b40 (esi = shadow; caller 0x00417b20): destructor body: vtable 0x005b96bc, owner dropped
// (Shadow_SetOwner(shadow, 0)), renderer virtual +0x128 (&shadow[+0x5c], 1) (releases the render target, guess),
// unlinked, links cleared, then (tail jump) DecalBase_Destruct. Returns the old +0x54 (EAX at the tail jump).
uint32_t Shadow_Destruct(uint8_t* s)
{
    Field<uint32_t>(s, 0) = 0x005b96bc;
    Shadow_SetOwner(s, nullptr);
    VCall<void>(Renderer(), 0x128 / 4, static_cast<void*>(s + 0x5c), 1u);
    Decal_Unlink(s);
    const uint32_t ret = Field<uint32_t>(s, 0x54);
    Field<uint32_t>(s, 0x50) = 0;
    Field<uint32_t>(s, 0x54) = 0;
    DecalBase_Destruct(s);
    return ret;
}

static uint32_t Shadow_Destruct_Abi(uint32_t esi)
{
    return Shadow_Destruct(static_cast<uint8_t*>(P(esi)));
}
ABI_ENTRY_00417b40(Shadow_Destruct_Entry, Shadow_Destruct_Abi)
HOOK(0x00417b40, "Shadow_Destruct", Shadow_Destruct_Entry, nullptr);

// 0x00417b20 (thiscall, 1 arg: flags; callee pops 4; the shadow vtable's deleting destructor, slot +0x18):
// Shadow_Destruct, then when flags bit 0 the memory goes back to the heap 0x0063a210 (0x004e08d0). Returns it.
uint32_t Shadow_DeletingDestruct(uint8_t* s, uint32_t flags)
{
    Shadow_Destruct(s);
    if (flags & 1)
        orig::fn_004e08d0(0x0063a210, u(s));
    return u(s);
}

static uint32_t Shadow_DeletingDestruct_Abi(uint32_t ecx, uint32_t a0)
{
    return Shadow_DeletingDestruct(static_cast<uint8_t*>(P(ecx)), a0);
}
ABI_ENTRY_00417b20(Shadow_DeletingDestruct_Entry, Shadow_DeletingDestruct_Abi)
HOOK(0x00417b20, "Shadow_DeletingDestruct", Shadow_DeletingDestruct_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// 0x00417c00 (eax = shadow; caller 0x00418190, per shadow each frame before Shadow_RenderTexture). The light camera:
//  - light direction D = light 0's direction (renderer virtual +0xb4 (0, &light) fills a 0x68-byte light, D at
//    +0x40) or (0, -1, -1) when that fails; up = (0, 0, 1);
//  - the owner's bounding sphere (owner virtual +0x90 (&centre, &radius, 1)); distance = radius * cot (+0x1a4);
//    eye (+0x108) = centre + (-distance) * D; 0x004e2eb0 (eax = up, ecx = matrix, edx = -D, edi = eye) builds the
//    camera matrix, 0x004e2b40 (eax = it, ecx = +0xc4) the view;
//  - near (+0x198) = distance - radius, far (+0x19c) = distance + 100 (0x005bcb60); projection 0x004f0100 (esi =
//    renderer; out, fov +0x1a0, near, far, 1.0); view-projection = 0x004e2910 (eax = view; out, projection);
//  - frustum planes at +0x120 (Frustum_FromViewProjection 0x004889a0 with the far plane: ecx = Game+0xa78 with cl
//    = 1, edx = planes, edi = view-projection; stack: Game+0xa78), count at +0x11c;
//  - plane 5 (+0x170): z cleared, (x, y) renormalised when not 0, then moved through the centre offset by
//    k * plane * radius (k = 1.0 when owner virtual +0xec is true, else 0.5): d = -((cz*z + cy*y) + cx*x);
//  - texture matrix +0x80 = 0x004e2910 (eax = view-projection; +0x80, the bias 0x005fe4e0).
// Returns 0 here (the caller ignores it: guess, 0x00418190 not read in this batch).
uint32_t Shadow_SetupLightCamera(uint8_t* s)
{
    alignas(8) uint8_t light[0x68];
    float up[3] = {0.0f, 0.0f, 1.0f};
    float fallback[3];
    const float* dir;
    if (VCall<uint32_t>(Renderer(), 0xb4 / 4, 0u, static_cast<void*>(light)) & 0xff)
        dir = reinterpret_cast<const float*>(light + 0x40);
    else
    {
        fallback[0] = 0.0f;
        std::memcpy(&fallback[2], &GameGlobal<uint32_t>(0x005bcb5c), 4);  // -1.0 (the original stores 0xbf800000)
        fallback[1] = fallback[2];
        dir = fallback;
    }
    float radius;
    float centre[3];
    VCall<void>(Field<void*>(s, 0x58), 0x90 / 4, static_cast<void*>(centre), static_cast<void*>(&radius), 1u);
    const float distance = radius * Field<float>(s, 0x1a4);
    const float neg = -distance;
    float* eye = &Field<float>(s, 0x108);
    eye[0] = neg * dir[0];
    eye[1] = neg * dir[1];
    eye[2] = neg * dir[2];
    eye[0] = centre[0] + eye[0];
    eye[1] = centre[1] + eye[1];
    eye[2] = centre[2] + eye[2];
    float target[3];
    target[0] = -dir[0];
    target[1] = -dir[1];
    target[2] = -dir[2];
    alignas(8) uint32_t camera[17];
    orig::fn_004e2eb0(u(up), u(camera), u(target), u(eye));
    uint8_t* view = s + 0xc4;
    orig::fn_004e2b40(u(camera), u(view));
    const float nearPlane = distance - radius;
    Field<float>(s, 0x198) = nearPlane;
    const float farPlane = distance + GameGlobal<float>(0x005bcb60);
    Field<float>(s, 0x19c) = farPlane;
    alignas(8) uint32_t projection[17];
    orig::fn_004f0100(u(Renderer()), u(projection), Field<uint32_t>(s, 0x1a0), orig::f2u(nearPlane),
                      orig::f2u(farPlane), 0x3f800000u);
    orig::fn_004e2910(u(view), u(camera), u(projection));  // camera := view * projection
    const uint32_t pass = Field<uint32_t>(g_game, 0xa78);
    Field<uint32_t>(s, 0x11c) = orig::fn_004889a0((pass & 0xffffff00u) | 1u, u(s) + 0x120, u(camera), pass);
    Field<uint32_t>(s, 0x178) = 0;
    float* p5 = &Field<float>(s, 0x170);
    const float len = sqrtf((p5[0] * p5[0] + p5[1] * p5[1]) + p5[2] * p5[2]);
    if (!(len == GameGlobal<float>(0x0056c05c)))
    {
        const float inv = GameGlobal<float>(0x005bcb2c) / len;
        p5[0] = inv * p5[0];
        p5[1] = inv * p5[1];
        p5[2] = inv * p5[2];
    }
    const float k = (VCall<uint32_t>(Field<void*>(s, 0x58), 0xec / 4) & 0xff) ? GameGlobal<float>(0x005bcb2c)
                                                                                  : GameGlobal<float>(0x005bcb4c);
    centre[0] = centre[0] - k * p5[0] * radius;
    centre[1] = centre[1] - k * p5[1] * radius;
    p5[3] = -((centre[2] * p5[2] + centre[1] * p5[1]) + centre[0] * p5[0]);
    orig::fn_004e2910(u(camera), u(s) + 0x80, 0x005fe4e0);
    return 0;
}

static uint32_t Shadow_SetupLightCamera_Abi(uint32_t eax)
{
    return Shadow_SetupLightCamera(static_cast<uint8_t*>(P(eax)));
}
ABI_ENTRY_00417c00(Shadow_SetupLightCamera_Entry, Shadow_SetupLightCamera_Abi)
HOOK(0x00417c00, "Shadow_SetupLightCamera", Shadow_SetupLightCamera_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// 0x00417e90 (ebx = draw context (the dynamic buffer's virtual +0x18 result, from Shadow_RenderTexture), esi =
// entity; caller Shadow_RenderTexture). Draws an entity into the shadow texture: world = entity virtual +0x7c
// (renderer virtual +0x54), its model's mesh list (0x00453120 (eax = entity, edx = entity +0x374;
// stack: *0x00408800(4, entity))) is drawn twice by 0x00488b00 (eax = list; Game+0xa78, context, entity +0x410,
// pass 0 then 1, the callback 0x00418600, 0). Returns the second call's EAX.
uint32_t Shadow_DrawCaster(uint32_t context, uint8_t* entity)
{
    void* renderer = Renderer();
    void** vtbl = *static_cast<void***>(renderer);
    const uint32_t world = VCall<uint32_t>(entity, 0x7c / 4);
    // The original fetched the renderer's vtable before the entity call and reloads the renderer pointer after.
    reinterpret_cast<void(__fastcall*)(void*, void*, uint32_t)>(vtbl[0x54 / 4])(Renderer(), nullptr, world);
    const uint32_t lod = Field<uint32_t>(P(orig::fn_00408800(4, u(entity))), 0);
    const uint32_t list = orig::fn_00453120(u(entity), Field<uint32_t>(entity, 0x374), lod);
    orig::fn_00488b00(list, Field<uint32_t>(g_game, 0xa78), context, Field<uint32_t>(entity, 0x410), 0, 0x00418600,
                      0);
    return orig::fn_00488b00(list, Field<uint32_t>(g_game, 0xa78), context, Field<uint32_t>(entity, 0x410), 1,
                             0x00418600, 0);
}

static uint32_t Shadow_DrawCaster_Abi(uint32_t ebx, uint32_t esi)
{
    return Shadow_DrawCaster(ebx, static_cast<uint8_t*>(P(esi)));
}
ABI_ENTRY_00417e90(Shadow_DrawCaster_Entry, Shadow_DrawCaster_Abi)
HOOK(0x00417e90, "Shadow_DrawCaster", Shadow_DrawCaster_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// 0x00417f20 (stdcall, 1 arg: shadow; caller 0x00418190 (Shadows_Update)). The shadow's render-to-texture pass:
//  - saves the viewport (renderer virtual +0x7c, 16 bytes), world/view/projection (+0x68, +0x64, +0x60, 0x44 each);
//  - render target = the shadow's texture (renderer virtual +0xcc (shadow +0x5c, 0, 0, 0)); on failure returns;
//  - state = renderer virtual +0x158 (4) (low byte kept), +0x154 (4, 0), viewport +0x74 (0), view +0x50 (+0xc4),
//    projection 0x004f0100 (esi = renderer; out, fov +0x1a0, 1.0, far +0x19c, 0) set by +0x4c, +0xf4 (0);
//  - context = dynamic buffer (Game+0x44) virtual +0x18; Game+0x40 virtual +0x14 (0x005fe498, the shadow material
//    block set up by Shadows_AllocatePool); the owner (+0x58) is drawn (Shadow_DrawCaster), and when its +0x3ec has
//    bit 2, every object of the list Game+0xa84 +0xe78 whose 0x004539d0 (eax = object) is the owner as well;
//  - Game+0x44 virtual +0x1c; renderer +0x154 (4, saved state), +0xcc (0, 0, 0, 0), world/view/projection and the
//    viewport restored. The iterator's node loses a reference (+0x18) and is returned.
// The original passes the saved state as a dword whose upper three bytes are uninitialised stack; only the low byte
// (what +0x158 returned) is reproduced here.
uint32_t Shadow_RenderTexture(uint8_t* s)
{
    uint32_t iterator[2] = {0, 0};
    uint32_t viewport[4];
    alignas(8) uint32_t world[17];
    alignas(8) uint32_t view[17];
    alignas(8) uint32_t projection[17];
    alignas(8) uint32_t shadowProjection[17];
    std::memcpy(viewport, VCall<const void*>(Renderer(), 0x7c / 4), 16);
    void* r = Renderer();
    std::memcpy(world, VCall<const void*>(r, 0x68 / 4), 0x44);
    std::memcpy(view, VCall<const void*>(r, 0x64 / 4), 0x44);
    std::memcpy(projection, VCall<const void*>(r, 0x60 / 4), 0x44);
    const uint32_t ok = VCall<uint32_t>(Renderer(), 0xcc / 4, static_cast<void*>(s + 0x5c), 0u, 0u, 0u);
    if (!(ok & 0xff))
        return ok;
    const uint8_t saved = static_cast<uint8_t>(VCall<uint32_t>(Renderer(), 0x158 / 4, 4u));
    VCall<void>(Renderer(), 0x154 / 4, 4u, 0u);
    VCall<void>(Renderer(), 0x74 / 4, 0u);
    VCall<void>(Renderer(), 0x50 / 4, static_cast<void*>(s + 0xc4));
    void* renderer = Renderer();
    orig::fn_004f0100(u(renderer), u(shadowProjection), Field<uint32_t>(s, 0x1a0), 0x3f800000u,
                      Field<uint32_t>(s, 0x19c), 0);
    VCall<void>(renderer, 0x4c / 4, static_cast<void*>(shadowProjection));
    VCall<void>(Renderer(), 0xf4 / 4, 0u);
    const uint32_t context = VCall<uint32_t>(Field<void*>(g_game, 0x44), 0x18 / 4);
    VCall<void>(Field<void*>(g_game, 0x40), 0x14 / 4, reinterpret_cast<void*>(0x005fe498));
    Shadow_DrawCaster(context, Field<uint8_t*>(s, 0x58));
    if (Field<uint8_t>(Field<uint8_t*>(s, 0x58), 0x3ec) & 4)
    {
        void* list = Field<void*>(Field<void*>(g_game, 0xa84), 0xe78);
        for (uint32_t obj = orig::fn_004e7340(u(list), u(iterator)); obj; obj = orig::fn_004e73c0(u(iterator)))
            if (orig::fn_004539d0(obj) == Field<uint32_t>(s, 0x58))
                Shadow_DrawCaster(context, static_cast<uint8_t*>(P(obj)));
    }
    VCall<void>(Field<void*>(g_game, 0x44), 0x1c / 4);
    VCall<void>(Renderer(), 0x154 / 4, 4u, static_cast<uint32_t>(saved));
    VCall<void>(Renderer(), 0xcc / 4, 0u, 0u, 0u, 0u);
    void* rr = Renderer();
    VCall<void>(rr, 0x54 / 4, static_cast<void*>(world));
    VCall<void>(rr, 0x50 / 4, static_cast<void*>(view));
    VCall<void>(rr, 0x4c / 4, static_cast<void*>(projection));
    VCall<void>(Renderer(), 0x74 / 4, static_cast<void*>(viewport));
    if (iterator[1])
        --Field<int32_t>(P(iterator[1]), 0x18);
    return iterator[1];
}

static uint32_t Shadow_RenderTexture_Abi(uint32_t a0)
{
    return Shadow_RenderTexture(static_cast<uint8_t*>(P(a0)));
}
ABI_ENTRY_00417f20(Shadow_RenderTexture_Entry, Shadow_RenderTexture_Abi)
HOOK(0x00417f20, "Shadow_RenderTexture", Shadow_RenderTexture_Entry, nullptr);
