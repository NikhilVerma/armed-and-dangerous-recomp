// Recovered (batch r7): render-state forwarder, a UI gauge draw, the post-process render targets, renderer helpers
// (release, perspective, matrix copy) and the pixel accumulate/pack tables 0x005caae4 / 0x005cab20.
// Static recovery 2026-10-05, compile-checked only (the game was not run); the oracle (tools/compare.py) is still
// to do. Functions marked "not executed in recorded runs" cannot be verified by the comparison yet.
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
void* P(uint32_t v)
{
    return reinterpret_cast<void*>(v);
}
float K(uint32_t address)
{
    return GameGlobal<float>(address);
}
void* Renderer()
{
    return Field<void*>(g_game, 0x38);
}
// __ftol2 (0x0056a7c8): truncating float -> 64-bit integer; callers use the low dword.
uint32_t Ftol(float f)
{
    return static_cast<uint32_t>(static_cast<int64_t>(f));
}
// fild of a dword the original tests for sign and corrects with + [0x005bcb28] (2^32): unsigned -> float.
float UnsignedToFloat(uint32_t v)
{
    float f = static_cast<float>(static_cast<int32_t>(v));
    if (static_cast<int32_t>(v) < 0)
        f += K(0x005bcb28);
    return f;
}
}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// 0x00416730 (no register input; 1 stack argument, popped by the tail-called method, so effectively ret 4): forwards
// its argument to the render-state setter Game+0x40 virtual +0x14 (set state block, see render_r3.cpp conventions
// and render_r0.cpp 0x005fe498) by a tail jump; the argument frame is unchanged, so a call-then-return reproduces it.
// recover.py "no ret found" because of the `jmp [edx+0x14]`; abi.csv says stack 0, but the only direct caller
// 0x004184e0 pushes one argument and does not pop it. Also vtable 0x005b964c slot 9 (+0x24) and 0x005b96b4. Executed
// in recorded runs.
uint32_t RenderStates_Apply(uint32_t block)
{
    return VCall<uint32_t>(Field<void*>(g_game, 0x40), 0x14 / 4, block);
}

// ---------------------------------------------------------------------------------------------------------------
// 0x0046d130 (ecx = UI object; no stack arguments): draws a horizontal gauge: three 0x0046b200 boxes (frame
// 9..0x113 x -9..9 colour +0x578; fill 0x50..(0x50 - ftol(+0x59c * [0x005bcde4])) colour +0x57c; rest ..0x110
// colour +0x580; y -6..6), then when +0x5ac is set +0x584+1 tick marks (Ui_DrawLineInt 0x0046b260 at
// x = ftol(i * (+0x594 * [0x005bcde0]) + [0x005bcddc]), from y 6..9 and -6..-9, colour +0x57c), then UiText_Render
// and the label (UiText_Draw, ebx = +0x4f8, Game+0xa70, 0.45, 0, 1.0) between renderer virtual +0x190 (1) and
// +0x194 (Renderer_EndScreenBatch). The original tail-jumps to +0x194 with its frame already popped (+0x194 takes
// no stack arguments), so a call-then-return is equivalent. Vtable 0x005a2fc8 slot 79 (+0x13c, ctor near
// 0x004605c8); no direct callers; not executed in recorded runs. Meaning of +0x59c / +0x594 (value, tick spacing):
// guess.
uint32_t fn_0046d130(void* obj)
{
    const uint32_t o = u(obj);
    const int32_t fillEnd = 0x50 - static_cast<int32_t>(Ftol(Field<float>(obj, 0x59c) * K(0x005bcde4)));
    orig::fn_0046b200(o, 9, 0x4d, 0, 0x113, static_cast<uint32_t>(-9), Field<uint32_t>(obj, 0x578), 0, 0);
    orig::fn_0046b200(o, 6, 0x50, 0, static_cast<uint32_t>(fillEnd), static_cast<uint32_t>(-6),
                      Field<uint32_t>(obj, 0x57c), 0, 0);
    orig::fn_0046b200(o, 6, static_cast<uint32_t>(fillEnd), 0, 0x110, static_cast<uint32_t>(-6),
                      Field<uint32_t>(obj, 0x580), 0, 0);
    if (Field<uint8_t>(obj, 0x5ac) != 0)
    {
        const float step = Field<float>(obj, 0x594) * K(0x005bcde0);
        uint32_t i = 0;
        do
        {
            const uint32_t x = Ftol(UnsignedToFloat(i) * step + K(0x005bcddc));
            orig::fn_0046b260(o, 6, x, x, 9, Field<uint32_t>(obj, 0x57c), 0);
            orig::fn_0046b260(o, static_cast<uint32_t>(-6), x, x, static_cast<uint32_t>(-9),
                              Field<uint32_t>(obj, 0x57c), 0);
            ++i;
        } while (i <= Field<uint32_t>(obj, 0x584));
    }
    orig::fn_00465360(o, o);
    VCall<void>(Renderer(), 0x190 / 4, 1u);
    orig::fn_00464750(o, o + 0x4f8, o, Field<uint32_t>(g_game, 0xa70), 0x3ee66666u /* 0.45 */, 0,
                      0x3f800000u /* 1.0 */);
    return VCall<uint32_t>(Renderer(), 0x194 / 4);
}

// ---------------------------------------------------------------------------------------------------------------
// Post-process render targets. The post-process object (Game+0xa98, game_object.h) holds: +0xdc number of blur
// targets, +0xe0 / +0xe1 flags, +0xe4 up to 8 blur target descriptors (0x24 bytes each), +0x204 the afterburn
// target descriptor, +0x22c / +0x24c two 8-entry pointer arrays. Descriptor: +0 name, +0x10 / +0x14 size (9/8, 8/7,
// ...; log2 width/height: guess), +0x18 / +0x1c 0, +0x20 byte 0x7f, +0x22 word flags 0x2080 (bit 15 set by the
// renderer when the target exists). docs/subsystems/shadows.md.
uint32_t PostProcess_ReleaseTargets(void* pp);

// Enhancement hook ([render] post_blur_prescale, hook/enhance/render.cpp): up to 2 extra render targets above the
// blur chain (2048x1024 and/or 1024x512, codes 11/10 and 10/9). Post_BlitChain (render_r3.cpp) copies the full-screen
// source through them before the chain's first step, so that step shrinks 2x instead of 7.5x from 4K (the chain, its
// level count and every effect's maths are unchanged). Registered and released with the chain.
int32_t g_postPrescale = 0;      // levels asked for
int32_t g_postPreCount = 0;      // levels registered
uint8_t g_postPreDesc[2][0x24];  // their descriptors (the chain's targets arrays hold descriptor pointers)

// 0x0047c920 (eax = post-process object): (re)creates the targets. Releases old ones when +0xdc != 0, asks renderer
// virtual +0x138 (Renderer_GetTextureSize; 0, &a, &b; results unused), then registers "blur texture" targets
// (renderer +0x124, Renderer_RegisterTexture) at sizes 9x8, 8x7, ... while the height is >= 4 and fewer than 8 exist
// (both pointer arrays get each one); a failed registration (flag bit 15 clear) ends the chain and decrements
// +0xdc. Then registers "afterburn texture" (9x8); +0xe0 = 0, +0xe1 = its bit 15, and if set +0x24c[0] = it.
// Returns that bit (0/1). Executed in recorded runs.
uint32_t PostProcess_CreateTargets(void* pp)
{
    void* r = Renderer();
    if (Field<uint32_t>(pp, 0xdc) != 0)
        PostProcess_ReleaseTargets(pp);
    int32_t size[2];
    VCall<void>(r, 0x138 / 4, 0u, &size[0], &size[1]);
    int32_t w = 9;
    int32_t h = 8;
    size[0] = w;
    size[1] = h;
    Field<uint32_t>(pp, 0xdc) = 0;
    uint8_t* desc = static_cast<uint8_t*>(pp) + 0xe4;
    while (h >= 4 && Field<int32_t>(pp, 0xdc) < 8)
    {
        Field<int32_t>(desc, 0x14) = h;
        Field<uint32_t>(desc, 0) = 0x0059e558;  // "blur texture"
        Field<int32_t>(desc, 0x10) = w;
        Field<uint32_t>(desc, 0x1c) = 0;
        Field<uint32_t>(desc, 0x18) = 0;
        Field<uint8_t>(desc, 0x20) = 0x7f;
        Field<uint16_t>(desc, 0x22) = 0x2080;
        VCall<void>(r, 0x124 / 4, static_cast<void*>(desc));
        if (!(Field<uint8_t>(desc, 0x23) & 0x80))
        {
            --Field<int32_t>(pp, 0xdc);
            break;
        }
        Field<uint8_t*>(pp, 0x22c + Field<uint32_t>(pp, 0xdc) * 4) = desc;
        Field<uint8_t*>(pp, 0x24c + Field<uint32_t>(pp, 0xdc) * 4) = desc;
        --w;
        --h;
        desc += 0x24;
        ++Field<int32_t>(pp, 0xdc);
        if (w < 4)
            break;
    }
    g_postPreCount = 0;
    for (int32_t j = 0; j < g_postPrescale && j < 2; ++j)
    {
        uint8_t* d = g_postPreDesc[j];
        std::memset(d, 0, 0x24);
        Field<uint32_t>(d, 0) = 0x0059e558;  // "blur texture"
        Field<int32_t>(d, 0x10) = 9 + g_postPrescale - j;
        Field<int32_t>(d, 0x14) = 8 + g_postPrescale - j;
        Field<uint8_t>(d, 0x20) = 0x7f;
        Field<uint16_t>(d, 0x22) = 0x2080;
        VCall<void>(r, 0x124 / 4, static_cast<void*>(d));
        if (!(Field<uint8_t>(d, 0x23) & 0x80))
            break;
        ++g_postPreCount;
    }
    uint8_t* after = static_cast<uint8_t*>(pp) + 0x204;
    Field<uint32_t>(after, 0) = 0x0059e544;  // "afterburn texture"
    Field<uint32_t>(after, 0x10) = 9;
    Field<uint32_t>(after, 0x14) = 8;
    Field<uint32_t>(after, 0x1c) = 0;
    Field<uint32_t>(after, 0x18) = 0;
    Field<uint8_t>(after, 0x20) = 0x7f;
    Field<uint16_t>(after, 0x22) = 0x2080;
    VCall<void>(r, 0x124 / 4, static_cast<void*>(after));
    const uint32_t ok = (static_cast<uint32_t>(Field<uint16_t>(after, 0x22)) >> 15) & 1;
    Field<uint8_t>(pp, 0xe0) = 0;
    Field<uint8_t>(pp, 0xe1) = static_cast<uint8_t>(ok);
    if (ok)
        Field<uint8_t*>(pp, 0x24c) = after;
    return ok;
}

// 0x0047ca60 (esi = post-process object): when +0xdc != 0, releases each blur target (renderer virtual +0x128,
// Renderer_ReleaseTexture(desc, 1), count re-read every step, signed) and the afterburn target, then zeroes +0xdc,
// the descriptors (0x51 dwords from +0xe4) and both pointer arrays. Always clears +0xe0. Returns 0. Executed in
// recorded runs.
uint32_t PostProcess_ReleaseTargets(void* pp)
{
    void* r = Renderer();
    if (Field<int32_t>(pp, 0xdc) != 0)
    {
        uint8_t* desc = static_cast<uint8_t*>(pp) + 0xe4;
        for (int32_t i = 0; i < Field<int32_t>(pp, 0xdc); ++i, desc += 0x24)
            VCall<void>(r, 0x128 / 4, static_cast<void*>(desc), 1u);
        VCall<void>(r, 0x128 / 4, static_cast<void*>(static_cast<uint8_t*>(pp) + 0x204), 1u);
        for (int32_t j = 0; j < g_postPreCount; ++j)
            VCall<void>(r, 0x128 / 4, static_cast<void*>(g_postPreDesc[j]), 1u);
        g_postPreCount = 0;
        Field<uint32_t>(pp, 0xdc) = 0;
        std::memset(static_cast<uint8_t*>(pp) + 0xe4, 0, 0x51 * 4);
        std::memset(static_cast<uint8_t*>(pp) + 0x22c, 0, 8 * 4);
        std::memset(static_cast<uint8_t*>(pp) + 0x24c, 0, 8 * 4);
    }
    Field<uint8_t>(pp, 0xe0) = 0;
    return 0;
}

// ---------------------------------------------------------------------------------------------------------------
// 0x004f00c0 (ecx = renderer; tracker address 0x004f00bb is the 5-byte nop pad in front of it): renderer virtual
// +0x14 (vtable 0x00589990 slot 5, entry 0x005899a4 holds 0x004f00c0). If ecx is not the global renderer
// (0x0063a0c4) logs "Error releasing the graphics sub-system\n" (0x0058add4); otherwise, when the renderer exists,
// decrements the reference count 0x0063a0c8 and at zero calls its virtual 0 (Renderer_Destroy 0x004ffd40, no stack
// argument) and clears 0x0063a0c4. Returns EAX as left (the log's result, the destroy result, or the renderer
// pointer / 0). Not executed in recorded runs.
uint32_t Renderer_Release(void* renderer)
{
    void* g = GameGlobal<void*>(0x0063a0c4);
    if (renderer != g)
        return static_cast<uint32_t>(reinterpret_cast<int(__cdecl*)(const char*, ...)>(0x004e0b30)(
            reinterpret_cast<const char*>(0x0058add4)));
    if (g == nullptr)
        return 0;
    if (--GameGlobal<int32_t>(0x0063a0c8) != 0)
        return u(g);
    const uint32_t r = VCall<uint32_t>(g, 0);
    GameGlobal<void*>(0x0063a0c4) = nullptr;
    return r;
}

// 0x004f0100 (esi = renderer; stack: out matrix, fov, near, far, aspect; caller pops 5): builds a perspective
// projection with Matrix_Perspective 0x004e3490 (eax = out 0x44-byte matrix; fov (scaled by [0x005bcb4c] before
// cos/sin), near, far, aspect: m[0] = cot / aspect, m[5] = cot, m[10] = -far / (far - near), m[11] =
// -near * far / (far - near), m[14] = -1). The five stack arguments, from the body and the callers Camera_SetupProjection
// (camera.cpp: matrix, fov, near, far, aspect), the scene pass (fov, near, 1e6, 0) and Renderer 0x004fffe0 (60 deg
// 0x3f860a92, 1.0, 1000.0, 0): out matrix, field of view (radians), near plane, far plane, aspect ratio. aspect
// == 0 (exactly; NaN does not match) means "derive it": with a renderer, (viewport w / h from virtual +0x7c,
// renderer+0x40) / Renderer_GetAspect (virtual +0x88, pixel aspect); without one 1.0. Returns the matrix pointer
// (EAX, untouched by Matrix_Perspective). Executed in recorded runs.
uint32_t Renderer_MakePerspective(void* renderer, float* out, float fov, float nearPlane, float farPlane, float aspect)
{
    if (aspect == K(0x0056c05c))
    {
        if (renderer != nullptr)
        {
            const float* vp = VCall<float*>(renderer, 0x7c / 4);
            const float ratio = vp[0] / vp[1];
            aspect = ratio / VCall<float>(renderer, 0x88 / 4);
        }
        else
        {
            aspect = 1.0f;
        }
    }
    orig::fn_004e3490(u(out), orig::f2u(fov), orig::f2u(nearPlane), orig::f2u(farPlane), orig::f2u(aspect));
    return u(out);
}

// 0x004f0170 (stack: renderer, out; callee pops 8): copies the renderer's three current matrices (0x44 bytes each)
// to out: virtual +0x68 (renderer+0x6c) to out+0, virtual +0x64 (renderer+0xb0) to out+0x44, virtual +0x60
// (Renderer_GetProjection) to out+0x88. Callers save/restore the transforms around screen-space drawing
// (render_r4.cpp, auto_00503900.cpp). Which of +0x6c / +0xb0 is world and which view: unknown. Returns the
// projection pointer. Executed in recorded runs.
uint32_t Renderer_CopyMatrices(void* renderer, uint8_t* out)
{
    std::memcpy(out, VCall<void*>(renderer, 0x68 / 4), 0x44);
    std::memcpy(out + 0x44, VCall<void*>(renderer, 0x64 / 4), 0x44);
    void* projection = VCall<void*>(renderer, 0x60 / 4);
    std::memcpy(out + 0x88, projection, 0x44);
    return u(projection);
}

// ---------------------------------------------------------------------------------------------------------------
// Pixel accumulate table 0x005caae4 (10 entries) and pixel pack table 0x005cab20 (10 entries), indexed by the same
// format order: 0 A8R8G8B8, 1 X8R8G8B8, 2 R8G8B8, 3 A4R4G4B4, 4 A1R5G5B5, 5 R5G6B5, 6 A8L8, 7 A4L4, 8 L8, 9 A8
// (order inferred from the bit layouts; note the accumulate table holds 0x004f0620 (1555) before 0x004f0590 (565)
// while the pack table holds 0x004f0950 (1555) before 0x004f0900 (565)). Accumulators (cdecl: source pixel, float
// sum[4] = r, g, b, a) add the pixel's components, expanded to 0..255, to sum; packers (cdecl: destination pixel,
// float[4] r, g, b, a) truncate (__ftol2) and store. A box filter for mip/downsample generation (guess). Neither
// table base occurs in the code of this build (searched as an absolute dword), so all twenty are not executed in
// recorded runs and have no direct callers. Every expanded component is non-negative, so the original's
// "fild; jl -> + 2^32" correction never fires (reproduced anyway through UnsignedToFloat).

// 0x004f0390 A8R8G8B8. Reads a dword. Returns sum.
uint32_t PixelAccumulate_A8R8G8B8(const uint32_t* src, float* sum)
{
    const uint32_t v = *src;
    sum[0] = UnsignedToFloat((v >> 16) & 0xff) + sum[0];
    sum[1] = UnsignedToFloat((v >> 8) & 0xff) + sum[1];
    sum[2] = UnsignedToFloat(v & 0xff) + sum[2];
    sum[3] = UnsignedToFloat(v >> 24) + sum[3];
    return u(sum);
}

// 0x004f0420 X8R8G8B8 (alpha not touched). Returns the blue byte (EAX as left).
uint32_t PixelAccumulate_X8R8G8B8(const uint32_t* src, float* sum)
{
    const uint32_t v = *src;
    sum[0] = UnsignedToFloat((v >> 16) & 0xff) + sum[0];
    sum[1] = UnsignedToFloat((v >> 8) & 0xff) + sum[1];
    sum[2] = UnsignedToFloat(v & 0xff) + sum[2];
    return v & 0xff;
}

// 0x004f0490 R8G8B8 (3 bytes b, g, r). Returns sum.
uint32_t PixelAccumulate_R8G8B8(const uint8_t* src, float* sum)
{
    sum[2] = static_cast<float>(static_cast<int32_t>(src[0])) + sum[2];
    sum[1] = static_cast<float>(static_cast<int32_t>(src[1])) + sum[1];
    sum[0] = static_cast<float>(static_cast<int32_t>(src[2])) + sum[0];
    return u(sum);
}

// 0x004f04d0 A4R4G4B4 (reads a dword; each nibble n expands to n * 0x11). Returns the alpha value.
uint32_t PixelAccumulate_A4R4G4B4(const uint32_t* src, float* sum)
{
    const uint32_t v = *src;
    const uint32_t s = v >> 4;
    sum[0] = UnsignedToFloat(((s & 0xf0) | (v & 0xf00)) >> 4) + sum[0];
    sum[1] = UnsignedToFloat((s & 0xf) | (v & 0xf0)) + sum[1];
    sum[2] = UnsignedToFloat(((v & 0xf) << 4) | (v & 0xf)) + sum[2];
    const uint32_t a = ((v & 0xf000) | (s & 0xf00)) >> 8;
    sum[3] = UnsignedToFloat(a) + sum[3];
    return a;
}

// 0x004f0590 R5G6B5 (reads a dword; 5/6-bit fields expanded with their top bits replicated). Returns (v >> 2) & 7.
uint32_t PixelAccumulate_R5G6B5(const uint32_t* src, float* sum)
{
    const uint32_t v = *src;
    sum[0] = UnsignedToFloat((((v >> 5) & 0x700) | (v & 0xf800)) >> 8) + sum[0];
    sum[1] = UnsignedToFloat((((v >> 6) & 0x18) | (v & 0x7e0)) >> 3) + sum[1];
    sum[2] = UnsignedToFloat(((v & 0x1f) << 3) | ((v >> 2) & 7)) + sum[2];
    return (v >> 2) & 7;
}

// 0x004f0620 A1R5G5B5 (reads a dword; alpha bit 15 -> 0 or 0xff). Returns (v >> 8) & 0x80.
uint32_t PixelAccumulate_A1R5G5B5(const uint32_t* src, float* sum)
{
    const uint32_t v = *src;
    const uint32_t s = v >> 5;
    sum[0] = UnsignedToFloat(((v & 0x7c00) | (s & 0x380)) >> 7) + sum[0];
    sum[1] = UnsignedToFloat(((s & 0x1c) | (v & 0x3e0)) >> 2) + sum[1];
    sum[2] = UnsignedToFloat(((v & 0x1f) << 3) | ((v >> 2) & 7)) + sum[2];
    const uint32_t a = (v >> 8) & 0x80;
    sum[3] = UnsignedToFloat((a != 0 ? 0x7fu : 0u) | a) + sum[3];
    return a;
}

// 0x004f06e0 A8L8 (reads a word: low byte luminance into r, g, b; high byte alpha). Returns sum.
uint32_t PixelAccumulate_A8L8(const uint16_t* src, float* sum)
{
    const uint16_t v = *src;
    const float l = static_cast<float>(static_cast<int32_t>(v & 0xff));
    const int32_t a = v >> 8;
    sum[0] = l + sum[0];
    sum[1] = l + sum[1];
    sum[2] = l + sum[2];
    sum[3] = static_cast<float>(a) + sum[3];
    return u(sum);
}

// 0x004f0730 A4L4 (byte). Luminance is added as byte << 4 without masking the alpha nibble (values up to 0xff0:
// original bug, reproduced); alpha = (b & 0xf0) | (b >> 4). The byte is re-read for each component. Returns sum.
uint32_t PixelAccumulate_A4L4(const uint8_t* src, float* sum)
{
    sum[0] = static_cast<float>(static_cast<int32_t>(src[0] << 4)) + sum[0];
    sum[1] = static_cast<float>(static_cast<int32_t>(src[0] << 4)) + sum[1];
    sum[2] = static_cast<float>(static_cast<int32_t>(src[0] << 4)) + sum[2];
    const uint8_t b = src[0];
    sum[3] = static_cast<float>(static_cast<int32_t>(static_cast<uint8_t>((b & 0xf0) | (b >> 4)))) + sum[3];
    return u(sum);
}

// 0x004f0790 L8 (byte into r, g, b; re-read each time). Returns sum.
uint32_t PixelAccumulate_L8(const uint8_t* src, float* sum)
{
    sum[0] = static_cast<float>(static_cast<int32_t>(src[0])) + sum[0];
    sum[1] = static_cast<float>(static_cast<int32_t>(src[0])) + sum[1];
    sum[2] = static_cast<float>(static_cast<int32_t>(src[0])) + sum[2];
    return u(sum);
}

// 0x004f07d0 A8 (byte into alpha). Returns sum.
uint32_t PixelAccumulate_A8(const uint8_t* src, float* sum)
{
    sum[3] = static_cast<float>(static_cast<int32_t>(src[0])) + sum[3];
    return u(sum);
}

// 0x004f07f0 A8R8G8B8: dword = ((a << 8 | r) << 8 | g) << 8 | b with no masking (an out-of-range component spills
// into its neighbour). Truncation order a, r, g, b. Returns dst.
uint32_t PixelPack_A8R8G8B8(uint32_t* dst, const float* c)
{
    uint32_t v = Ftol(c[3]);
    v <<= 8;
    v |= Ftol(c[0]);
    v <<= 8;
    v |= Ftol(c[1]);
    v <<= 8;
    v |= Ftol(c[2]);
    *dst = v;
    return u(dst);
}

// 0x004f0830 X8R8G8B8: dword = (r << 8 | g) << 8 | b (unmasked). Returns dst.
uint32_t PixelPack_X8R8G8B8(uint32_t* dst, const float* c)
{
    uint32_t v = Ftol(c[0]);
    v <<= 8;
    v |= Ftol(c[1]);
    v <<= 8;
    v |= Ftol(c[2]);
    *dst = v;
    return u(dst);
}

// 0x004f0870 R8G8B8: bytes b, g, r. Returns the truncated red (EAX as left).
uint32_t PixelPack_R8G8B8(uint8_t* dst, const float* c)
{
    dst[0] = static_cast<uint8_t>(Ftol(c[2]));
    dst[1] = static_cast<uint8_t>(Ftol(c[1]));
    const uint32_t r = Ftol(c[0]);
    dst[2] = static_cast<uint8_t>(r);
    return r;
}

// 0x004f08a0 A4R4G4B4: word = ((r & 0xfff0) << 4 | g) & 0xfff0 | (a & 0xf0) << 8 | b >> 4 (arithmetic shift).
// Truncation order r, g, a, b. Returns b >> 4.
uint32_t PixelPack_A4R4G4B4(uint16_t* dst, const float* c)
{
    uint32_t v = (Ftol(c[0]) & 0xfff0) << 4;
    v = (v | Ftol(c[1])) & 0xfff0;
    v |= (Ftol(c[3]) & 0xf0) << 8;
    const uint32_t b = static_cast<uint32_t>(static_cast<int32_t>(Ftol(c[2])) >> 4);
    v |= b;
    *dst = static_cast<uint16_t>(v);
    return b;
}

// 0x004f0900 R5G6B5: word = (((r & 0xfff8) << 5 | g) & 0xfffc) << 3 | b >> 3. Returns dst.
uint32_t PixelPack_R5G6B5(uint16_t* dst, const float* c)
{
    uint32_t v = (Ftol(c[0]) & 0xfff8) << 5;
    v = ((v | Ftol(c[1])) & 0xfffc) << 3;
    v |= static_cast<uint32_t>(static_cast<int32_t>(Ftol(c[2])) >> 3);
    *dst = static_cast<uint16_t>(v);
    return u(dst);
}

// 0x004f0950 A1R5G5B5: word = ((((r & 0xfff8) << 5 | g) & 0xfff8) | (a & 0xff80) << 6) << 2 | b >> 3. Truncation
// order r, g, a, b. Returns dst.
uint32_t PixelPack_A1R5G5B5(uint16_t* dst, const float* c)
{
    uint32_t v = (Ftol(c[0]) & 0xfff8) << 5;
    v = (v | Ftol(c[1])) & 0xfff8;
    v |= (Ftol(c[3]) & 0xff80) << 6;
    v <<= 2;
    v |= static_cast<uint32_t>(static_cast<int32_t>(Ftol(c[2])) >> 3);
    *dst = static_cast<uint16_t>(v);
    return u(dst);
}

// Luminance of the packers below: (int)(b + g + r) / 3 (signed), truncation order b, g, r.
namespace
{
int32_t Luminance(const float* c)
{
    int32_t s = static_cast<int32_t>(Ftol(c[2]));
    s += static_cast<int32_t>(Ftol(c[1]));
    s += static_cast<int32_t>(Ftol(c[0]));
    return s / 3;
}
}  // namespace

// 0x004f09b0 A8L8: word = l | (a & 0xff) << 8 (l unmasked). Returns the truncated alpha.
uint32_t PixelPack_A8L8(uint16_t* dst, const float* c)
{
    const uint32_t l = static_cast<uint32_t>(Luminance(c));
    const uint32_t a = Ftol(c[3]);
    *dst = static_cast<uint16_t>(l | ((a & 0xff) << 8));
    return a;
}

// 0x004f0a00 A4L4: byte = (l >> 4) | (a & 0xf0). Returns the truncated alpha with its low nibble cleared.
uint32_t PixelPack_A4L4(uint8_t* dst, const float* c)
{
    const int32_t l = Luminance(c) >> 4;
    const uint32_t a = Ftol(c[3]) & 0xfffffff0u;
    *dst = static_cast<uint8_t>(static_cast<uint8_t>(l) | static_cast<uint8_t>(a));
    return a;
}

// 0x004f0a50 L8: byte = l. Returns the sign bit of the division's high product (0 for non-negative sums).
uint32_t PixelPack_L8(uint8_t* dst, const float* c)
{
    int32_t s = static_cast<int32_t>(Ftol(c[2]));
    s += static_cast<int32_t>(Ftol(c[1]));
    s += static_cast<int32_t>(Ftol(c[0]));
    *dst = static_cast<uint8_t>(s / 3);
    const int64_t q = static_cast<int64_t>(s) * 0x55555556LL;
    return static_cast<uint32_t>(static_cast<uint64_t>(q) >> 63);
}

// 0x004f0a90 A8: stores 0 (the colour is ignored: original bug, alpha is dropped). abi.csv lists one stack
// argument; the table's callers pass two (caller pops). Returns dst.
uint32_t PixelPack_A8(uint8_t* dst)
{
    *dst = 0;
    return u(dst);
}

// ---------------------------------------------------------------------------------------------------------------
// ABI adapters (register inputs in the order eax ecx edx ebx esi edi, then stack arguments), entries and hooks.

// 0x00416730 has no generated stub (recover.py: no ret found). Hand-written: one stack argument, ret 4 (the
// tail-called method's thiscall pop), every register but EAX preserved (the original clobbers eax ecx edx).
static uint32_t RenderStates_Apply_Abi(uint32_t a0)
{
    return RenderStates_Apply(a0);
}
static __declspec(naked) void RenderStates_Apply_Entry()
{
    __asm push ecx __asm push edx __asm push ebx __asm push esi __asm push edi __asm push ebp
    __asm push dword ptr [esp + 28]
    __asm call RenderStates_Apply_Abi
    __asm add esp, 4
    __asm pop ebp __asm pop edi __asm pop esi __asm pop ebx __asm pop edx __asm pop ecx
    __asm ret 4
}
HOOK(0x00416730, "RenderStates_Apply", RenderStates_Apply_Entry, nullptr);

// 0x0041c050 FxOwner_OnMessage: implemented (non-static) in render_r2a.cpp and deliberately not hooked there; it
// is hooked here. Vtable method only: vtable 0x005a8f70 (ctor near 0x00460f80) slot 14 (+0x38), entry 0x005a8fa8;
// no direct callers; not executed in recorded runs. (Note: render_r2a.cpp passes the flags word as ECX to
// 0x0046e2c0 where the original has ECX = +0x46c; 0x0046e2c0's `push ecx` only reserves a stack slot and does not
// use the value, so the behaviour is the same.)
uint32_t FxOwner_OnMessage(void* obj, uint32_t a0, uint32_t message);
static uint32_t FxOwner_OnMessage_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return FxOwner_OnMessage(P(ecx), a0, a1);
}
ABI_ENTRY_0041c050(FxOwner_OnMessage_Entry, FxOwner_OnMessage_Abi)
HOOK(0x0041c050, "FxOwner_OnMessage", FxOwner_OnMessage_Entry, nullptr);

// 0x0046d130 has no generated stub (no ret found: tail jump). Hand-written: ecx input, no stack arguments, plain
// ret, every register but EAX preserved.
static uint32_t fn_0046d130_Abi(uint32_t ecx)
{
    return fn_0046d130(P(ecx));
}
static __declspec(naked) void fn_0046d130_Entry()
{
    __asm push ecx __asm push edx __asm push ebx __asm push esi __asm push edi __asm push ebp
    __asm push ecx
    __asm call fn_0046d130_Abi
    __asm add esp, 4
    __asm pop ebp __asm pop edi __asm pop esi __asm pop ebx __asm pop edx __asm pop ecx
    __asm ret
}
HOOK(0x0046d130, "fn_0046d130", fn_0046d130_Entry, nullptr);

static uint32_t PostProcess_CreateTargets_Abi(uint32_t eax)
{
    return PostProcess_CreateTargets(P(eax));
}
ABI_ENTRY_0047c920(PostProcess_CreateTargets_Entry, PostProcess_CreateTargets_Abi)
HOOK(0x0047c920, "PostProcess_CreateTargets", PostProcess_CreateTargets_Entry, nullptr);

static uint32_t PostProcess_ReleaseTargets_Abi(uint32_t esi)
{
    return PostProcess_ReleaseTargets(P(esi));
}
ABI_ENTRY_0047ca60(PostProcess_ReleaseTargets_Entry, PostProcess_ReleaseTargets_Abi)
HOOK(0x0047ca60, "PostProcess_ReleaseTargets", PostProcess_ReleaseTargets_Entry, nullptr);

// Renderer_Release: the stub generated for the tracker address 0x004f00bb (ecx input, plain ret) is installed at
// the real entry 0x004f00c0, the address the vtable holds (0x004f00bb..0x004f00bf are nops never reached).
static uint32_t Renderer_Release_Abi(uint32_t ecx)
{
    return Renderer_Release(P(ecx));
}
ABI_ENTRY_004f00bb(Renderer_Release_Entry, Renderer_Release_Abi)
HOOK(0x004f00c0, "Renderer_Release", Renderer_Release_Entry, nullptr);

static uint32_t Renderer_MakePerspective_Abi(uint32_t esi, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3,
                                             uint32_t a4)
{
    float f[4];
    const uint32_t bits[4] = {a1, a2, a3, a4};
    std::memcpy(f, bits, sizeof f);
    return Renderer_MakePerspective(P(esi), reinterpret_cast<float*>(a0), f[0], f[1], f[2], f[3]);
}
ABI_ENTRY_004f0100(Renderer_MakePerspective_Entry, Renderer_MakePerspective_Abi)
HOOK(0x004f0100, "Renderer_MakePerspective", Renderer_MakePerspective_Entry, nullptr);

static uint32_t Renderer_CopyMatrices_Abi(uint32_t a0, uint32_t a1)
{
    return Renderer_CopyMatrices(P(a0), reinterpret_cast<uint8_t*>(a1));
}
ABI_ENTRY_004f0170(Renderer_CopyMatrices_Entry, Renderer_CopyMatrices_Abi)
HOOK(0x004f0170, "Renderer_CopyMatrices", Renderer_CopyMatrices_Entry, nullptr);

#define R7_PIXEL_HOOK(addr, name, SrcT, SumT)                                     \
    static uint32_t name##_Abi(uint32_t a0, uint32_t a1)                          \
    {                                                                             \
        return name(reinterpret_cast<SrcT*>(a0), reinterpret_cast<SumT*>(a1));    \
    }                                                                             \
    ABI_ENTRY_##addr(name##_Entry, name##_Abi) HOOK(0x##addr, #name, name##_Entry, nullptr);

R7_PIXEL_HOOK(004f0390, PixelAccumulate_A8R8G8B8, const uint32_t, float)
R7_PIXEL_HOOK(004f0420, PixelAccumulate_X8R8G8B8, const uint32_t, float)
R7_PIXEL_HOOK(004f0490, PixelAccumulate_R8G8B8, const uint8_t, float)
R7_PIXEL_HOOK(004f04d0, PixelAccumulate_A4R4G4B4, const uint32_t, float)
R7_PIXEL_HOOK(004f0590, PixelAccumulate_R5G6B5, const uint32_t, float)
R7_PIXEL_HOOK(004f0620, PixelAccumulate_A1R5G5B5, const uint32_t, float)
R7_PIXEL_HOOK(004f06e0, PixelAccumulate_A8L8, const uint16_t, float)
R7_PIXEL_HOOK(004f0730, PixelAccumulate_A4L4, const uint8_t, float)
R7_PIXEL_HOOK(004f0790, PixelAccumulate_L8, const uint8_t, float)
R7_PIXEL_HOOK(004f07d0, PixelAccumulate_A8, const uint8_t, float)
R7_PIXEL_HOOK(004f07f0, PixelPack_A8R8G8B8, uint32_t, const float)
R7_PIXEL_HOOK(004f0830, PixelPack_X8R8G8B8, uint32_t, const float)
R7_PIXEL_HOOK(004f0870, PixelPack_R8G8B8, uint8_t, const float)
R7_PIXEL_HOOK(004f08a0, PixelPack_A4R4G4B4, uint16_t, const float)
R7_PIXEL_HOOK(004f0900, PixelPack_R5G6B5, uint16_t, const float)
R7_PIXEL_HOOK(004f0950, PixelPack_A1R5G5B5, uint16_t, const float)
R7_PIXEL_HOOK(004f09b0, PixelPack_A8L8, uint16_t, const float)
R7_PIXEL_HOOK(004f0a00, PixelPack_A4L4, uint8_t, const float)
R7_PIXEL_HOOK(004f0a50, PixelPack_L8, uint8_t, const float)

static uint32_t PixelPack_A8_Abi(uint32_t a0)
{
    return PixelPack_A8(reinterpret_cast<uint8_t*>(a0));
}
ABI_ENTRY_004f0a90(PixelPack_A8_Entry, PixelPack_A8_Abi)
HOOK(0x004f0a90, "PixelPack_A8", PixelPack_A8_Entry, nullptr);
