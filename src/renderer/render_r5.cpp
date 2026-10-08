// Recovered (static, batch r5): object-side rendering. Compile-checked only (the game was not run); the oracle
// (tools/compare.py) is still to do.
//   0x0043a8d0 FxBodyfall_Spawn
//   0x004495b0 LoadingScreen_ErrorLoop
//   0x00449860 LoadingScreen_Fade
//   0x004499c0 LoadingScreen_Show
//   0x00449ac0 LoadingScreen_Draw
//   0x0044add0 LightObject_Load              (LightObject vt slot 6)
//   0x0044b0e0 fn_0044b0e0                   (LightObject vt slot 7)
//   0x0044b100 LightObject_ReleaseLight      (LightObject vt slot 0x37)
//   0x0044f1f0 Texture_LoadRgbPixels
//   0x0044f2d0 SceneThumbnail_Render
//   0x00452f50 Object_SelectLod              (object base vt slot 0x27)
//   0x00453180 Object_GetMatrixColumn2
//   0x00454360 Object_InFrustum              (object base vt slot 0xc)
//   0x004544a0 fn_004544a0                   (object base vt slot 0xa)
//   0x004544b0 fn_004544b0                   (object base vt slot 0x32)
//   0x00455bf0 Object_IsShadowEnabled        (object base vt slot 0x4b)
//   0x00455c10 Object_GetShadow              (object base vt slot 0x4c, +0x130)
//   0x00455d60 Object_SetShadowEnabled       (object base vt slot 0x4d)
//   0x00461a90 FxObject_GetClass             (CLASS(FxObject))
//   0x00461c50 LightObject_GetClass          (CLASS(LightObject))
//   0x00461e30 CameraFXObject_GetClass       (CLASS(CameraFXObject))
#include <cmath>
#include <cstring>

#include "game.h"
#include "game/game_object.h"
#include "gen/entry_stubs.h"
#include "gen/orig_calls.h"
#include "hooks.h"
#include "renderer/scene_pass.h"

namespace
{
template <class T>
uint32_t u(T* p)
{
    return reinterpret_cast<uint32_t>(p);
}
uint32_t f2u(float f)
{
    uint32_t v;
    std::memcpy(&v, &f, 4);
    return v;
}
float u2f(uint32_t v)
{
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}
void* P(uint32_t v)
{
    return reinterpret_cast<void*>(v);
}

// Game float constants (read in place, as the original does).
inline const float& kZero = GameGlobal<float>(0x0056c05c);   // 0.0
inline const float& kOne = GameGlobal<float>(0x005bcb2c);    // 1.0

void* Renderer()
{
    return Field<void*>(g_game, 0x38);
}
void* PostProcess()
{
    return Field<void*>(g_game, 0xa98);
}

// QueryPerformanceCounter through the game's import slot 0x0056b08c, minus the timer base 0x00604ad8, divided by
// the frequency 0x00604ac8 (fild/fild/fdivp; single precision at run time).
int64_t TimerTicks()
{
    int64_t t;
    reinterpret_cast<int(__stdcall*)(int64_t*)>(GameGlobal<uint32_t>(0x0056b08c))(&t);
    return t - GameGlobal<int64_t>(0x00604ad8);
}
float TimerSeconds()
{
    const int64_t t = TimerTicks();
    return static_cast<float>(static_cast<double>(t) / static_cast<double>(GameGlobal<int64_t>(0x00604ac8)));
}

// The block inlined three times in the loading screen: renderer vt+0x154(4, post-process byte +0xe2), then world
// (+0x54), view (+0x50) and projection (+0x4c) from post-process +0, +0x44, +0x88, viewport (+0x74) from +0xcc, and
// post-process +0x228 = 0.
void RestorePostProcessView()
{
    void* r = Renderer();
    void* pp = PostProcess();
    VCall<void>(r, 0x154 / 4, 4u, static_cast<uint32_t>(Field<uint8_t>(pp, 0xe2)));
    VCall<void>(r, 0x54 / 4, pp);
    VCall<void>(r, 0x50 / 4, u(pp) + 0x44);
    VCall<void>(r, 0x4c / 4, u(pp) + 0x88);
    VCall<void>(r, 0x74 / 4, u(pp) + 0xcc);
    Field<uint32_t>(pp, 0x228) = 0;
}
}  // namespace

uint32_t Texture_LoadRgbPixels(const uint8_t* src, void* desc, uint32_t width, uint32_t height, uint32_t alpha);
uint32_t LoadingScreen_Draw(void* screen, float setValue, float fade, uint32_t force);
uint32_t Object_GetMatrixColumn2(void* obj, uint32_t* out);

// 0x0043a8d0 (esi = object; caller 0x0042e1c0): when the object holds a sound (+0xb4c), the audio object
// (g_game+0x2c, docs/subsystems/qol.md) vt+0xd8(&object+0xb4c, 0). Then fills a 0x80-byte fx spawn record in the
// 0x94-byte frame (record at +0x14: flags 8, position from the object's vt+0x54 at +0x20; if vt+0xb8(&a, &c, &b, 0)
// succeeds, flags |= 0x800 and the record's +0x78/+0x7c = b/a) and spawns "fx_bodyfall" (0x005b4828) with 0x0041cc60
// (ecx = fx manager g_game+0xa90; eax = the record address with al cleared, as the original leaves it).
uint32_t FxBodyfall_Spawn(void* obj)
{
    uint32_t frame[0x94 / 4];
    if (Field<uint32_t>(obj, 0xb4c))
        VCall<void>(Field<void*>(g_game, 0x2c), 0xd8 / 4, u(obj) + 0xb4c, 0u);
    VCall<void>(obj, 0x54 / 4, &frame[0x20 / 4]);
    frame[0x14 / 4] = 8;
    if (VCall<bool>(obj, 0xb8 / 4, &frame[0], &frame[2], &frame[1], 0u))
    {
        frame[0x14 / 4] |= 0x800;
        frame[0x8c / 4] = frame[1];
        frame[0x90 / 4] = frame[0];
    }
    return orig::fn_0041cc60(u(&frame[0x14 / 4]) & ~0xffu, Field<uint32_t>(g_game, 0xa90), 0x005b4828,
                             u(&frame[0x14 / 4]));
}

// 0x004495b0 (stack: arg, caller pops; never returns; callers 0x004039f0, 0x00447a00, 0x00488880): logs
// "Error loading file or level\n", turns the RGB image at 0x005ce6d0 (0x100 wide, [0x005ce6c8] / 0x300 rows) into
// the texture 0x004909d0(ui, 1) with 0x0044f1f0, then loops forever drawing it: game vt+0x14, renderer Clear
// (vt+0xf4)(0), and when the texture exists 0x00415dc0(0, 1), 0x0047cdd0(post-process, record, quad, 0), the
// restore block; game vt+0x18, renderer vt+0x108 (present). The 14-dword record: 0x130e4, arg, texture, 0, 0, 0, then
// the quad x 0.2333/0.7667, y (240 +- rows') * 1/480 (0x005bcbf4).
uint32_t LoadingScreen_ErrorLoop(uint32_t arg)
{
    GameLog(reinterpret_cast<const char*>(0x005b2564));
    uint32_t rec[14];
    for (int i = 0; i < 6; ++i)
        rec[i] = 0;
    uint32_t rows = GameGlobal<uint32_t>(0x005ce6c8) / 0x300;
    const uint32_t tex = orig::fn_004909d0(Field<uint32_t>(g_game, 0xab4), 1);
    rec[2] = Texture_LoadRgbPixels(reinterpret_cast<const uint8_t*>(0x005ce6d0), P(tex), 0x100, rows, 0);
    int32_t half = static_cast<int32_t>(rows);
    if (half > 0x80)
        half = half / 2;
    const float k = GameGlobal<float>(0x005bcbf4);
    const float bottom = static_cast<float>(half + 0xf0) * k;
    const float top = static_cast<float>(0xf0 - half) * k;
    rec[0] = 0x130e4;
    rec[1] = arg;
    rec[6] = 0x3e6eeeef, rec[7] = f2u(bottom), rec[8] = 0x3f444444, rec[9] = f2u(bottom);
    rec[10] = 0x3e6eeeef, rec[11] = f2u(top), rec[12] = 0x3f444444, rec[13] = f2u(top);
    for (;;)
    {
        VCall<void>(g_game, 0x14 / 4);
        VCall<void>(Renderer(), 0xf4 / 4, 0u);
        if (rec[2])
        {
            orig::fn_00415dc0(u(Renderer()), u(PostProcess()), 0, 1);
            orig::fn_0047cdd0(0, 0, u(PostProcess()), u(&rec[0]), u(&rec[6]), 0);
            RestorePostProcessView();
        }
        VCall<void>(g_game, 0x18 / 4);
        VCall<void>(Renderer(), 0x108 / 4);
    }
}

// 0x00449860 (esi = loading screen; stack: duration, callee pops 4; caller 0x004499c0): unless duration is 0, redraws
// the loading screen (0x00449ac0 forced, fade f) until f reaches 0: v = (now - start) / duration clamped to [0, 1],
// t = 1 - v, f = 3 t^2 - 2 t^3 (0x005bcb7c = 3). Then renderer vt+0x128(screen+0x2c, 1) (release the texture) and
// screen+0x1c = 0. Returns the renderer call's EAX (the skip path returns the FPU status word in the original; the
// only caller never takes it).
uint32_t LoadingScreen_Fade(void* screen, float duration)
{
    if (duration == kZero)
        return 0;
    const float start = TimerSeconds();
    const float rate = kOne / duration;
    float f;
    do
    {
        const float v = (TimerSeconds() - start) * rate;
        float c;
        if (!(v > kZero))
            c = kZero;
        else if (!(v < kOne))
            c = kOne;
        else if (!(v > kZero))
            c = kZero;
        else
            c = v;
        const float t = kOne - c;
        const float tt = t * t;
        const float k3tt = GameGlobal<float>(0x005bcb7c) * tt;
        const float ttt = tt * t;
        f = k3tt - (ttt + ttt);
        LoadingScreen_Draw(screen, 0.0f, f, 1);
    } while (!(f == kZero));
    const uint32_t r = VCall<uint32_t>(Renderer(), 0x128 / 4, u(screen) + 0x2c, 1u);
    Field<uint32_t>(screen, 0x1c) = 0;
    return r;
}

// 0x004499c0 (al = show, ecx = loading screen; caller 0x0044a110).
// Hide: fade out over 0.25 s (0x00449860), then 0x00490cb0(eax 0, ecx = ui g_game+0xab4, ebx 0; 0, 0.5, 0.25).
// Show: screen+0x54 = 0, +0x50 = now - 1 s (so the first draw is due), record +0x14..+0x2c = {0x130e4, -1, 0...},
// counter [0x0063a190]++ (argument: old != 0 in al, the new count's upper bytes kept), texture = renderer
// vt+0xe4(screen+0x2c, that) into +0x1c; on the first show with a texture, fade in over 1 s.
uint32_t LoadingScreen_Show(bool show, void* screen)
{
    if (!show)
    {
        LoadingScreen_Fade(screen, 0.25f);
        return orig::fn_00490cb0(0, Field<uint32_t>(g_game, 0xab4), 0, 0, 0x3f000000, 0x3e800000);
    }
    const float now = TimerSeconds();
    Field<uint32_t>(screen, 0x54) = 0;
    Field<float>(screen, 0x50) = now - kOne;
    for (uint32_t off = 0x14; off < 0x2c; off += 4)
        Field<uint32_t>(screen, off) = 0;
    uint32_t& count = GameGlobal<uint32_t>(0x0063a190);
    const uint32_t old = count;
    Field<uint32_t>(screen, 0x14) = 0x130e4;
    Field<uint32_t>(screen, 0x18) = 0xffffffff;
    count = old + 1;
    const uint32_t flag = ((old + 1) & ~0xffu) | (old != 0 ? 1u : 0u);
    const uint32_t tex = VCall<uint32_t>(Renderer(), 0xe4 / 4, u(screen) + 0x2c, flag);
    Field<uint32_t>(screen, 0x1c) = tex;
    if (count == 1 && tex)
        return LoadingScreen_Fade(screen, 1.0f);
    return tex;
}

// 0x00449ac0 (stdcall: screen, setValue, fade, force; ret 0x10; caller 0x00449860). When the screen is active
// (+0x14): setValue != 0 goes to +0x54; unless forced, draws at most every 1/30 s (0x005bcc50, last time +0x50).
// Frame: game vt+0x14, Clear, 0x00415dc0(0, 0), the screen image (0x0047cdd0 with record +0x14) when it has a texture
// (+0x1c), the restore block, 0x00415dc0(0, 1); when +0x54 != 0 two progress quads (0x0047cdd0, the second one's
// right edge 0.25 + 0.5 * progress); the restore block; without a texture a text line (renderer vt+0x190(0),
// 0x00447600, 0x00495e80 colour 0xa0ffa0, vt+0x194); when fade < 1 a full-screen fade 0x0047cff0(colour
// [0x0064aaf8] = 0xff000000 (static init), 1 - max(fade, 0)); game vt+0x18 and present (renderer vt+0x108).
uint32_t LoadingScreen_Draw(void* screen, float setValue, float fade, uint32_t force)
{
    const float now = TimerSeconds();
    if (!Field<uint32_t>(screen, 0x14))
        return 0;
    if (!(setValue == kZero))
        Field<float>(screen, 0x54) = setValue;
    if (static_cast<uint8_t>(force) == 0 && now - Field<float>(screen, 0x50) < GameGlobal<float>(0x005bcc50))
        return 0;
    Field<float>(screen, 0x50) = now;
    VCall<void>(g_game, 0x14 / 4);
    VCall<void>(Renderer(), 0xf4 / 4, 0u);
    orig::fn_00415dc0(u(Renderer()), u(PostProcess()), 0, 0);
    if (Field<uint32_t>(screen, 0x1c))
        orig::fn_0047cdd0(0, 0, u(PostProcess()), u(screen) + 0x14, 0, 0);
    RestorePostProcessView();
    orig::fn_00415dc0(u(Renderer()), u(PostProcess()), 0, 1);
    if (!(Field<float>(screen, 0x54) == kZero))
    {
        // Frame +0x18: a 6-dword record, then +0x30: the quad (8 floats), contiguous as in the original.
        uint32_t q[14] = {0x103a0, 0x80008000, 0, 0, 0, 0,
                          0x3e7ae148, 0x3e333333, 0x3f4147ae, 0x3e333333, 0x3e7ae148, 0x3e666666, 0x3f4147ae,
                          0x3e666666};
        orig::fn_0047cdd0(0, 0, u(PostProcess()), u(&q[0]), u(&q[6]), 0);
        const float right = Field<float>(screen, 0x54) * GameGlobal<float>(0x005bcb4c) + GameGlobal<float>(0x005bcbe4);
        q[1] = 0xc000c000;
        q[6 + 6] = f2u(right);
        q[6 + 2] = f2u(right);
        q[6 + 4] = 0x3e800000;
        q[6 + 0] = 0x3e800000;
        q[6 + 3] = 0x3e3ae148;
        q[6 + 1] = 0x3e3ae148;
        q[6 + 7] = 0x3e5eb852;
        q[6 + 5] = 0x3e5eb852;
        orig::fn_0047cdd0(0, 0, u(PostProcess()), u(&q[0]), u(&q[6]), 0);
    }
    RestorePostProcessView();
    if (!Field<uint32_t>(screen, 0x1c))
    {
        VCall<void>(Renderer(), 0x190 / 4, 0u);
        const uint32_t text = orig::fn_00447600();
        orig::fn_00495e80(0xa0ffa0, Field<uint32_t>(g_game, 0xac0), Field<uint32_t>(g_game, 0xac0), text, 0x3fc00000,
                          0x3f000000, 0x3e0e147b, 0x3f000000, 0x3f000000, 0);
        VCall<void>(Renderer(), 0x194 / 4);
    }
    if (fade < kOne)
    {
        uint32_t& guard = GameGlobal<uint32_t>(0x0064aafc);
        if (!(guard & 1))
        {
            guard |= 1;
            GameGlobal<uint32_t>(0x0064aaf8) = 0xff000000;
        }
        const float c = fade > kZero ? fade : kZero;
        orig::fn_0047cff0(u(PostProcess()), GameGlobal<uint32_t>(0x0064aaf8), f2u(kOne - c));
    }
    VCall<void>(g_game, 0x18 / 4);
    return VCall<uint32_t>(Renderer(), 0x108 / 4);
}

// 0x0044add0 (thiscall LightObject, vt slot 6 (+0x18), 4 stack args, ret 0x10; docs/subsystems/shadows.md s3).
// Base loader 0x004517b0 first, then properties through 0x004534a0 (esi = arg3 the source, edi = destination; this,
// name, arg2, default, 0) and 0x00453400 (booleans, eax 0): RedValue (default 255), GreenValue, BlueValue, Intensity
// (default 1, at most 4); colour = value * Intensity / 255 (0x005bcc18). Builds a D3DLIGHT9 at +0x460 (cleared):
// Diffuse +0x464 / Specular +0x474 / Ambient +0x484 = (r, g, b, 1) or zero per AffectDiffuse / AffectSpecular /
// AmbientOnly; HalfRange h gives the Attenuation1 default 1/h (+0x4b8); type (+0x460) 3 (directional) when
// Attenuation1 = 0, else 1 (point); Position +0x494 from vt+0x54; Direction +0x4a0 = -(0x00453180); Range 1e6,
// Falloff 0, Attenuation0 1, Attenuation2 0. The renderer light slot 0x0048a4b0(eax -1; [g_game+0xa78], light) goes
// to +0x4c8, then vt+0xf4(0). The original uses its argument slots as the property locals; they start with the
// argument values (Red in arg2's slot, Green arg1, Blue arg0, Intensity arg3).
uint32_t LightObject_Load(void* self, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    uint32_t red = a2, green = a1, blue = a0, v = a3;
    const uint32_t zero[4] = {0, 0, 0, 0};
    uint32_t c1[4], c2[4];
    orig::fn_004517b0(u(self), a0, a1, a2, a3);
    orig::fn_004534a0(a3, u(&red), u(self), 0x005b2464, a2, 0x437f0000, 0);
    orig::fn_004534a0(a3, u(&green), u(self), 0x005b2458, a2, 0, 0);
    orig::fn_004534a0(a3, u(&blue), u(self), 0x005b244c, a2, 0, 0);
    orig::fn_004534a0(a3, u(&v), u(self), 0x005b2440, a2, 0x3f800000, 0);
    if (u2f(v) > GameGlobal<float>(0x005bcbe0))
        v = 0x40800000;
    const float s = u2f(v) * GameGlobal<float>(0x005bcc18);
    v = f2u(s);
    const float r = s * u2f(red);
    for (uint32_t off = 0x460; off < 0x4c8; off += 4)
        Field<uint32_t>(self, off) = 0;
    const float g = s * u2f(green);
    const float b = s * u2f(blue);
    v = f2u(b);
    c1[0] = f2u(r), c1[1] = f2u(g), c1[2] = f2u(b), c1[3] = 0x3f800000;
    c2[0] = f2u(r), c2[1] = f2u(g), c2[2] = f2u(b), c2[3] = 0x3f800000;
    orig::fn_00453400(0, a3, u(&v), u(self), 0x005b2430, a2, 0);
    std::memcpy(reinterpret_cast<uint8_t*>(self) + 0x464, v ? c1 : zero, 16);
    orig::fn_00453400(0, a3, u(&v), u(self), 0x005b2420, a2, 0);
    std::memcpy(reinterpret_cast<uint8_t*>(self) + 0x474, v ? c2 : zero, 16);
    orig::fn_00453400(0, a3, u(&v), u(self), 0x005b2414, a2, 0);
    std::memcpy(reinterpret_cast<uint8_t*>(self) + 0x484, v ? c1 : zero, 16);
    orig::fn_004534a0(a3, u(&v), u(self), 0x005b2408, a2, 0, 0);
    if (!(u2f(v) == kZero))
        v = f2u(kOne / u2f(v));
    orig::fn_004534a0(a3, u(self) + 0x4b8, u(self), 0x005b23f8, a2, v, 0);
    Field<uint32_t>(self, 0x460) = Field<float>(self, 0x4b8) == kZero ? 3u : 1u;
    VCall<void>(self, 0x54 / 4, u(self) + 0x494);
    Object_GetMatrixColumn2(self, &Field<uint32_t>(self, 0x4a0));
    Field<float>(self, 0x4a0) = -Field<float>(self, 0x4a0);
    Field<uint32_t>(self, 0x4ac) = 0x49742400;
    Field<uint32_t>(self, 0x4b0) = 0;
    Field<float>(self, 0x4a4) = -Field<float>(self, 0x4a4);
    Field<uint32_t>(self, 0x4b4) = 0x3f800000;
    Field<uint32_t>(self, 0x4bc) = 0;
    Field<float>(self, 0x4a8) = -Field<float>(self, 0x4a8);
    Field<uint32_t>(self, 0x4c8) = orig::fn_0048a4b0(0xffffffff, Field<uint32_t>(g_game, 0xa78), u(self) + 0x460);
    return VCall<uint32_t>(self, 0xf4 / 4, 0u);
}

// 0x0044b0e0 (stdcall, 1 arg, ret 4; LightObject vt slot 7): *arg = 0x4000, byte arg+0x7a = 1. Returns arg (EAX).
uint32_t fn_0044b0e0(uint32_t arg)
{
    Field<uint32_t>(P(arg), 0) = 0x4000;
    Field<uint8_t>(P(arg), 0x7a) = 1;
    return arg;
}

// 0x0044b100 (ecx = LightObject; vt slot 0x37): frees its renderer light slot (+0x4c8, -1 = none) with
// 0x0048a520(eax = slot; [g_game+0xa78]), then tail-jumps to 0x004547b0 (ecx = this, the base-class method).
uint32_t LightObject_ReleaseLight(void* self)
{
    const uint32_t slot = Field<uint32_t>(self, 0x4c8);
    if (slot != 0xffffffff)
        orig::fn_0048a520(slot, Field<uint32_t>(g_game, 0xa78));
    return orig::fn_004547b0(u(self));
}

// 0x0044f1f0 (eax = RGB(A) pixels, ebx = 0x24-byte texture record; stack: width, height, alpha; caller pops;
// callers 0x004495b0, 0x0048c870). Releases the record (renderer vt+0x128(record, 1)), clears it, sets width +0x10,
// height +0x14, format word +0x22 = 0x41, creates and locks it (vt+0x13c(record, &bits, &pitch)), copies the pixels
// as B, G, R, 0xff (source stride 4 when alpha, else 3) and unlocks (vt+0x144). Returns the record, 0 on failure.
uint32_t Texture_LoadRgbPixels(const uint8_t* src, void* desc, uint32_t width, uint32_t height, uint32_t alpha)
{
    if (!desc || !src)
        return 0;
    VCall<void>(Renderer(), 0x128 / 4, desc, 1u);
    for (uint32_t off = 0; off < 0x24; off += 4)
        Field<uint32_t>(desc, off) = 0;
    Field<uint32_t>(desc, 0x1c) = 0;
    Field<uint32_t>(desc, 0x10) = width;
    Field<uint32_t>(desc, 0x14) = height;
    Field<uint16_t>(desc, 0x22) = 0x41;
    uint32_t lock[2];  // bits, pitch (frame +0xc, +0x10)
    if (!VCall<bool>(Renderer(), 0x13c / 4, desc, &lock[0], &lock[1]))
        return 0;
    const uint32_t stride = (static_cast<uint8_t>(alpha) != 0 ? 1u : 0u) + 3;
    for (uint32_t rows = height; rows != 0; --rows)
    {
        uint8_t* d = reinterpret_cast<uint8_t*>(lock[0]);
        for (uint32_t n = width; n != 0; --n)
        {
            d[0] = src[0];
            d[1] = src[1];
            d[2] = src[2];
            d[3] = 0xff;
            d += 4;
            src += stride;
        }
        lock[0] += lock[1];
    }
    VCall<void>(Renderer(), 0x144 / 4, desc);
    return u(desc);
}

// 0x0044f2d0 (stack: record, width, height; caller pops; caller 0x0044f3a0). Guess: the save-game thumbnail.
// Releases and re-describes the texture record (as 0x0044f1f0: +0x10 width, +0x14 height, +0x22 = 0x41), copies it
// to a local render-target record (+0x22 = 0x81), game vt+0x14, renders the scene into it with 0x00489540 (ecx =
// g_game, ebx = 0; [g_game+0xa78], local, 0), copies it into the texture (renderer vt+0x148(local, record, 0, 0)),
// releases the local (vt+0x128(local, 1)) and game vt+0x18. Returns the record, or 0 when the copy failed (the
// original zeroes its own argument slot for that; the caller's copy of the argument is not reproduced).
uint32_t SceneThumbnail_Render(void* desc, uint32_t width, uint32_t height)
{
    VCall<void>(Renderer(), 0x128 / 4, desc, 1u);
    for (uint32_t off = 0; off < 0x24; off += 4)
        Field<uint32_t>(desc, off) = 0;
    Field<uint32_t>(desc, 0x14) = height;
    Field<uint32_t>(desc, 0x10) = width;
    Field<uint32_t>(desc, 0x1c) = 0;
    Field<uint16_t>(desc, 0x22) = 0x41;
    uint32_t target[9];
    std::memcpy(target, desc, 0x24);
    Field<uint16_t>(target, 0x22) = 0x81;
    VCall<void>(g_game, 0x14 / 4);
    orig::fn_00489540(u(g_game), 0, Field<uint32_t>(g_game, 0xa78), u(target), 0);
    uint32_t result = u(desc);
    if (!VCall<bool>(Renderer(), 0x148 / 4, target, desc, 0u, 0u))
        result = 0;
    VCall<void>(Renderer(), 0x128 / 4, target, 1u);
    VCall<void>(g_game, 0x18 / 4);
    return result;
}

// 0x00452f50 (ecx = object; object base vt slot 0x27 (+0x9c), shared by CameraPathObject, PlayerWeapon, StigTurret,
// ... vtables). LOD record = +0x414. If (record+0x44 & g_game+0x4c8) & 0x400: LOD = record+0 (forced). Otherwise
// distance along the camera's view direction d = obj.dir - cam.dir (clamped at 0; position vt+0x58, camera 0x0040f9e0
// / 0x0040fa10 / FOV 0x0040f840 on g_game+0xa68), metric m = tan(fov / 2) * d * A + B (pair g_game+0x4a4 of the table
// 0x005b0c78: (16, 100), (8, 50), ...). Walks the LODs (count record+0x10 -> +0x28, thresholds -> +0x40, skipping
// indices record+0 and +4): each one whose m is not below the last chosen threshold becomes the choice (+0x374).
// None chosen: LOD 0 when there are LODs, else "Can't select LOD for object %s\n" (0x00407ff0 on g_game+0x34, name
// 0x00453270).
bool g_lodForceFinest = false;

uint32_t Object_SelectLod(void* obj)
{
    void* rec = Field<void*>(obj, 0x414);
    if ((Field<uint32_t>(rec, 0x44) & Field<uint32_t>(g_game, 0x4c8)) & 0x400)
    {
        const uint32_t v = Field<uint32_t>(rec, 0);
        Field<uint32_t>(obj, 0x374) = v;
        return v;
    }
    // Frame +0x14 camera direction, +0x20 camera position, +0x2c object position.
    float frame[9];
    float* camDir = &frame[0];
    float* camPos = &frame[3];
    float* objPos = &frame[6];
    VCall<void>(obj, 0x58 / 4, objPos);
    void* game = g_game;
    orig::fn_0040f9e0(u(camPos), Field<uint32_t>(game, 0xa68));
    orig::fn_0040fa10(u(camDir), Field<uint32_t>(game, 0xa68));
    const float fov = orig::fn_0040f840(Field<uint32_t>(game, 0xa68));
    float d = -((camPos[2] * camDir[2] + camPos[1] * camDir[1]) + camPos[0] * camDir[0]);
    d = d + objPos[2] * camDir[2];
    d = d + objPos[1] * camDir[1];
    d = d + objPos[0] * camDir[0];
    if (d < kZero)
        d = kZero;
    if (g_lodForceFinest)  // enhancement point (scene_pass.h); false: the original
        d = kZero;
    const uint32_t q = Field<uint32_t>(game, 0x4a4) * 8;
    rec = Field<void*>(obj, 0x414);
    float m = static_cast<float>(std::tan(static_cast<double>(fov * GameGlobal<float>(0x005bcb4c))) *
                                 static_cast<double>(d));
    m = m * GameGlobal<float>(0x005b0c78 + q);
    m = m + GameGlobal<float>(0x005b0c7c + q);
    Field<int32_t>(obj, 0x374) = -1;
    uint32_t count = Field<uint32_t>(Field<void*>(rec, 0x10), 0x28);
    double threshold = kZero;  // fild result kept on the FPU stack (exact integer)
    for (uint32_t i = 0; i < count;)
    {
        if (i != Field<uint32_t>(rec, 0) && i != Field<uint32_t>(rec, 4) && !(m < threshold))
        {
            Field<uint32_t>(obj, 0x374) = i;
            const int32_t t = Field<int32_t*>(Field<void*>(rec, 0x10), 0x40)[i];
            threshold = static_cast<double>(t);
            if (t < 0)
                threshold = static_cast<float>(threshold + GameGlobal<float>(0x005bcb28));
        }
        ++i;
        count = Field<uint32_t>(Field<void*>(Field<void*>(obj, 0x414), 0x10), 0x28);
    }
    const int32_t chosen = Field<int32_t>(obj, 0x374);
    if (chosen >= 0)
        return static_cast<uint32_t>(chosen);
    const uint32_t n = Field<uint32_t>(Field<void*>(rec, 0x10), 0x28);
    if (n)
    {
        Field<uint32_t>(obj, 0x374) = 0;
        return n;
    }
    const uint32_t name = orig::fn_00453270(u(obj));
    GameFunction<void(__cdecl*)(uint32_t, const char*, ...)>(0x00407ff0)(
        Field<uint32_t>(game, 0x34), reinterpret_cast<const char*>(0x005b0c58), name);
    return 0;
}

// 0x00453180 (ecx = object, esi = out vec3; callers 0x0044add0, 0x00473da0): out = elements 2, 6, 10 of the object's
// 0x44-byte matrix from vt+0x5c (the 0x44-byte frame read at +8, +0x18, +0x28 after the thiscall pops its argument).
// Returns element 2 (EAX). docs/subsystems/shadows.md s3 says [1],[5],[9]; the disassembly reads [2],[6],[10].
uint32_t Object_GetMatrixColumn2(void* obj, uint32_t* out)
{
    uint32_t m[17];
    VCall<void>(obj, 0x5c / 4, m);
    out[0] = m[2];
    out[1] = m[6];
    out[2] = m[10];
    return m[2];
}

// 0x00454360 (ecx = object; object base vt slot 0xc; callers 0x00419260, 0x0043eaa0): visible (1) when byte +0x421 is
// set; otherwise bounding sphere from vt+0x90(&centre, &radius, 1) against the planes 0x00488990(eax =
// [g_game+0xa78], edx = &planes) returns (count in EAX): outside (0) as soon as n.c + d < -radius.
uint32_t Object_InFrustum(void* obj)
{
    if (Field<uint8_t>(obj, 0x421))
        return 1;
    // Frame: +0 radius, +4 planes, +8 -radius, +0xc centre.
    uint32_t frame[6];
    VCall<void>(obj, 0x90 / 4, &frame[3], &frame[0], 1u);
    uint32_t n = orig::fn_00488990(Field<uint32_t>(g_game, 0xa78), u(&frame[1]));
    if (n == 0)
        return 1;
    const float* p = reinterpret_cast<const float*>(frame[1]);
    const float negRadius = -u2f(frame[0]);
    frame[2] = f2u(negRadius);
    const float* c = reinterpret_cast<const float*>(&frame[3]);
    do
    {
        --n;
        const float dist = ((c[2] * p[2] + c[1] * p[1]) + c[0] * p[0]) + p[3];
        if (dist < negRadius)
            return 0;
        p += 4;
    } while (n != 0);
    return 1;
}

// 0x004544a0 (stdcall, 1 arg, ret 4; object base vt slot 0xa (+0x28); ecx unused): 0x0048a210(esi = arg).
uint32_t fn_004544a0(uint32_t arg)
{
    return orig::fn_0048a210(arg);
}

// 0x004544b0 (ecx = object; stack: a0, matrix, a2; ret 0xc; object base vt slot 0x32 (+0xc8); caller 0x00410340):
// 0x0048a030(eax = a2, ecx = matrix; a0). With byte +0x421 set the matrix is first copied (0x44 bytes) and its
// translation (elements 3, 7, 11) replaced by the object position (vt+0x58), flags word +0x40 |= 0x10.
uint32_t fn_004544b0(void* obj, uint32_t a0, uint32_t matrix, uint32_t a2)
{
    if (!Field<uint8_t>(obj, 0x421))
        return orig::fn_0048a030(a2, matrix, a0);
    uint32_t frame[20];  // +0 position, +0xc matrix (0x44 bytes)
    VCall<void>(obj, 0x58 / 4, &frame[0]);
    uint32_t* m = &frame[3];
    std::memcpy(m, P(matrix), 0x44);
    m[16] |= 0x10;
    m[3] = frame[0];
    m[7] = frame[1];
    m[11] = frame[2];
    return orig::fn_0048a030(a2, u(m), a0);
}

// 0x00455bf0 (ecx = object; vt slot 0x4b): 1 when shadow_enable bits 0-1 of +0x3ec are both set (docs shadows.md).
uint32_t Object_IsShadowEnabled(void* obj)
{
    return (Field<uint32_t>(obj, 0x3ec) & 3) == 3 ? 1u : 0u;
}

// 0x00455c10 (ecx = object; vt slot 0x4c = +0x130, the shadow getter of Shadows_Update 0x004163c0). Shadows off
// (+0x3ec & 3 != 3) or beyond shadow_radius_max (+0x3fc, squared distance to the camera 0x0040f9e0, position vt+0x54):
// releases its shadow (+0x3e0, 0x00417b90 edx = 0) and returns 0. Otherwise, when radius_max > 0, the fade +0x3f0 =
// +0x3f4 * min(1, 1 - (d^2/max^2 - q) / (1 - q)) with q = min^2/max^2 (+0x3f8; 1 when q >= 1), and returns
// 0x00416530(ebx = object; [g_game+0xa7c]), the assigned shadow.
uint32_t Object_GetShadow(void* obj)
{
    if ((Field<uint32_t>(obj, 0x3ec) & 3) == 3)
    {
        const float rmax = Field<float>(obj, 0x3fc);
        bool assign = true;
        if (rmax > kZero)
        {
            float pos[3], cam[3];  // frame +0x10, +0x1c
            orig::fn_0040f9e0(u(cam), Field<uint32_t>(g_game, 0xa68));
            VCall<void>(obj, 0x54 / 4, pos);
            const float dx = pos[0] - cam[0];
            const float dy = pos[1] - cam[1];
            const float dz = pos[2] - cam[2];
            const float d2 = (dz * dz + dy * dy) + dx * dx;
            const float r2 = rmax * rmax;
            if (!(d2 < r2))
            {
                assign = false;
            }
            else
            {
                const float rmin = Field<float>(obj, 0x3f8);
                const float inv = kOne / r2;
                const float q = rmin * rmin * inv;
                float a;
                if (!(q < kOne))
                {
                    a = kOne;
                }
                else
                {
                    a = kOne - (inv * d2 - q) / (kOne - q);
                    if (a > kOne)
                        a = kOne;
                }
                Field<float>(obj, 0x3f0) = a * Field<float>(obj, 0x3f4);
            }
        }
        if (assign)
            return orig::fn_00416530(u(obj), Field<uint32_t>(g_game, 0xa7c));
    }
    if (const uint32_t shadow = Field<uint32_t>(obj, 0x3e0))
        orig::fn_00417b90(shadow, 0);
    return 0;
}

// 0x00455d60 (ecx = object, stack: enable byte, ret 4; vt slot 0x4d): +0x3ec bit 1 = enable && bit 0. Returns the
// argument byte (AL; the upper EAX bytes are the caller's in the original).
uint32_t Object_SetShadowEnabled(void* obj, uint32_t enable)
{
    const uint8_t e = static_cast<uint8_t>(enable);
    if (e && (Field<uint8_t>(obj, 0x3ec) & 1))
        Field<uint32_t>(obj, 0x3ec) |= 2;
    else
        Field<uint32_t>(obj, 0x3ec) &= 0xfffffffd;
    return e;
}

// 0x00461a90 / 0x00461c50 / 0x00461e30: vt slot 1 of FxObject (vtable 0x005a52d8), LightObject (0x005a8a50) and
// CameraFXObject (0x005ad058): the class descriptor (+8 object size 0x63c / 0x4cc / 0x488, +0x14 "CLASS(Name)").
uint32_t FxObject_GetClass()
{
    return 0x00580920;
}
uint32_t LightObject_GetClass()
{
    return 0x00583ae8;
}
uint32_t CameraFXObject_GetClass()
{
    return 0x0057f948;
}

// ---- ABI adapters and hooks ----

static uint32_t FxBodyfall_Spawn_Abi(uint32_t esi)
{
    return FxBodyfall_Spawn(P(esi));
}
ABI_ENTRY_0043a8d0(FxBodyfall_Spawn_Entry, FxBodyfall_Spawn_Abi)
HOOK(0x0043a8d0, "FxBodyfall_Spawn", FxBodyfall_Spawn_Entry, nullptr);

static uint32_t LoadingScreen_ErrorLoop_Abi(uint32_t a0)
{
    return LoadingScreen_ErrorLoop(a0);
}
ABI_ENTRY_004495b0(LoadingScreen_ErrorLoop_Entry, LoadingScreen_ErrorLoop_Abi)
HOOK(0x004495b0, "LoadingScreen_ErrorLoop", LoadingScreen_ErrorLoop_Entry, nullptr);

static uint32_t LoadingScreen_Fade_Abi(uint32_t esi, uint32_t duration)
{
    return LoadingScreen_Fade(P(esi), u2f(duration));
}
ABI_ENTRY_00449860(LoadingScreen_Fade_Entry, LoadingScreen_Fade_Abi)
HOOK(0x00449860, "LoadingScreen_Fade", LoadingScreen_Fade_Entry, nullptr);

static uint32_t LoadingScreen_Show_Abi(uint32_t eax, uint32_t ecx)
{
    return LoadingScreen_Show(static_cast<uint8_t>(eax) != 0, P(ecx));
}
ABI_ENTRY_004499c0(LoadingScreen_Show_Entry, LoadingScreen_Show_Abi)
HOOK(0x004499c0, "LoadingScreen_Show", LoadingScreen_Show_Entry, nullptr);

static uint32_t LoadingScreen_Draw_Abi(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return LoadingScreen_Draw(P(a0), u2f(a1), u2f(a2), a3);
}
ABI_ENTRY_00449ac0(LoadingScreen_Draw_Entry, LoadingScreen_Draw_Abi)
HOOK(0x00449ac0, "LoadingScreen_Draw", LoadingScreen_Draw_Entry, nullptr);

static uint32_t LightObject_Load_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return LightObject_Load(P(ecx), a0, a1, a2, a3);
}
ABI_ENTRY_0044add0(LightObject_Load_Entry, LightObject_Load_Abi)
HOOK(0x0044add0, "LightObject_Load", LightObject_Load_Entry, nullptr);

static uint32_t fn_0044b0e0_Abi(uint32_t a0)
{
    return fn_0044b0e0(a0);
}
ABI_ENTRY_0044b0e0(fn_0044b0e0_Entry, fn_0044b0e0_Abi)
HOOK(0x0044b0e0, "fn_0044b0e0", fn_0044b0e0_Entry, nullptr);

static uint32_t LightObject_ReleaseLight_Abi(uint32_t ecx)
{
    return LightObject_ReleaseLight(P(ecx));
}
ABI_ENTRY_0044b100(LightObject_ReleaseLight_Entry, LightObject_ReleaseLight_Abi)
HOOK(0x0044b100, "LightObject_ReleaseLight", LightObject_ReleaseLight_Entry, nullptr);

static uint32_t Texture_LoadRgbPixels_Abi(uint32_t eax, uint32_t ebx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return Texture_LoadRgbPixels(reinterpret_cast<const uint8_t*>(eax), P(ebx), a0, a1, a2);
}
ABI_ENTRY_0044f1f0(Texture_LoadRgbPixels_Entry, Texture_LoadRgbPixels_Abi)
HOOK(0x0044f1f0, "Texture_LoadRgbPixels", Texture_LoadRgbPixels_Entry, nullptr);

static uint32_t SceneThumbnail_Render_Abi(uint32_t a0, uint32_t a1, uint32_t a2)
{
    return SceneThumbnail_Render(P(a0), a1, a2);
}
ABI_ENTRY_0044f2d0(SceneThumbnail_Render_Entry, SceneThumbnail_Render_Abi)
HOOK(0x0044f2d0, "SceneThumbnail_Render", SceneThumbnail_Render_Entry, nullptr);

static uint32_t Object_SelectLod_Abi(uint32_t ecx)
{
    return Object_SelectLod(P(ecx));
}
ABI_ENTRY_00452f50(Object_SelectLod_Entry, Object_SelectLod_Abi)
HOOK(0x00452f50, "Object_SelectLod", Object_SelectLod_Entry, nullptr);

static uint32_t Object_GetMatrixColumn2_Abi(uint32_t ecx, uint32_t esi)
{
    return Object_GetMatrixColumn2(P(ecx), reinterpret_cast<uint32_t*>(esi));
}
ABI_ENTRY_00453180(Object_GetMatrixColumn2_Entry, Object_GetMatrixColumn2_Abi)
HOOK(0x00453180, "Object_GetMatrixColumn2", Object_GetMatrixColumn2_Entry, nullptr);

static uint32_t Object_InFrustum_Abi(uint32_t ecx)
{
    return Object_InFrustum(P(ecx));
}
ABI_ENTRY_00454360(Object_InFrustum_Entry, Object_InFrustum_Abi)
HOOK(0x00454360, "Object_InFrustum", Object_InFrustum_Entry, nullptr);

static uint32_t fn_004544a0_Abi(uint32_t a0)
{
    return fn_004544a0(a0);
}
ABI_ENTRY_004544a0(fn_004544a0_Entry, fn_004544a0_Abi)
HOOK(0x004544a0, "fn_004544a0", fn_004544a0_Entry, nullptr);

static uint32_t fn_004544b0_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return fn_004544b0(P(ecx), a0, a1, a2);
}
ABI_ENTRY_004544b0(fn_004544b0_Entry, fn_004544b0_Abi)
HOOK(0x004544b0, "fn_004544b0", fn_004544b0_Entry, nullptr);

static uint32_t Object_IsShadowEnabled_Abi(uint32_t ecx)
{
    return Object_IsShadowEnabled(P(ecx));
}
ABI_ENTRY_00455bf0(Object_IsShadowEnabled_Entry, Object_IsShadowEnabled_Abi)
HOOK(0x00455bf0, "Object_IsShadowEnabled", Object_IsShadowEnabled_Entry, nullptr);

static uint32_t Object_GetShadow_Abi(uint32_t ecx)
{
    return Object_GetShadow(P(ecx));
}
ABI_ENTRY_00455c10(Object_GetShadow_Entry, Object_GetShadow_Abi)
HOOK(0x00455c10, "Object_GetShadow", Object_GetShadow_Entry, nullptr);

static uint32_t Object_SetShadowEnabled_Abi(uint32_t ecx, uint32_t a0)
{
    return Object_SetShadowEnabled(P(ecx), a0);
}
ABI_ENTRY_00455d60(Object_SetShadowEnabled_Entry, Object_SetShadowEnabled_Abi)
HOOK(0x00455d60, "Object_SetShadowEnabled", Object_SetShadowEnabled_Entry, nullptr);

static uint32_t FxObject_GetClass_Abi()
{
    return FxObject_GetClass();
}
ABI_ENTRY_00461a90(FxObject_GetClass_Entry, FxObject_GetClass_Abi)
HOOK(0x00461a90, "FxObject_GetClass", FxObject_GetClass_Entry, nullptr);

static uint32_t LightObject_GetClass_Abi()
{
    return LightObject_GetClass();
}
ABI_ENTRY_00461c50(LightObject_GetClass_Entry, LightObject_GetClass_Abi)
HOOK(0x00461c50, "LightObject_GetClass", LightObject_GetClass_Entry, nullptr);

static uint32_t CameraFXObject_GetClass_Abi()
{
    return CameraFXObject_GetClass();
}
ABI_ENTRY_00461e30(CameraFXObject_GetClass_Entry, CameraFXObject_GetClass_Abi)
HOOK(0x00461e30, "CameraFXObject_GetClass", CameraFXObject_GetClass_Entry, nullptr);
