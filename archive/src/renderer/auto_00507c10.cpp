// Recovered: renderer virtual methods (Direct3D 9 layer): wide text drawing, aspect, gamma ramp, debug options,
// queries, statistics and the display mode list. S2, Approach B. None of these run in the comparison scenarios.
//   0x00507c10 Renderer_DrawTextW        (virtual 114, +0x1c8)
//   0x00508860 Renderer_GetAspect        (virtual 34,  +0x088)
//   0x005091c0 Renderer_SetGamma         (virtual 16,  +0x040)
//   0x00509a40 Renderer_ToggleOption     (virtual 90,  +0x168)
//   0x00509b70 Renderer_GetOption        (virtual 91,  +0x16c)
//   0x00509c10 Renderer_QueryInfo        (virtual 88,  +0x160)
//   0x00509cf0 Renderer_ResetStats       (virtual 50,  +0x0c8)
//   0x00509d10 Renderer_GetInfoString    (virtual 89,  +0x164)
//   0x00509da0 Renderer_GetDisplayModes  (virtual 13,  +0x034)
// Slots from tools\vtable.py 0x00589990.
#include <cstring>

#include "gen/entry_stubs.h"
#include "gen/orig_calls.h"
#include "hooks.h"
#include "renderer.h"

namespace
{
template <class T>
uint32_t u(T* p)
{
    return reinterpret_cast<uint32_t>(p);
}
float AsFloat(uint32_t bits)
{
    float f;
    memcpy(&f, &bits, 4);
    return f;
}
uint32_t FloatBits(float f)
{
    uint32_t bits;
    memcpy(&bits, &f, 4);
    return bits;
}

const uint32_t kHeap = 0x0063a210;                                                   // ecx of the allocator 0x004e07f0
const char* const kStrGfxModes = reinterpret_cast<const char*>(0x0058949c);          // "gfx_modes"
const char* const kStrStatsFormat = reinterpret_cast<const char*>(0x005894a8);       // "ITri %d LTri %d FTri %d ..."
const char* const kStrRendererName = reinterpret_cast<const char*>(0x005894d4);      // "PC DX8 - v1.0" (14 bytes)
char* const kInfoStringBuffer = reinterpret_cast<char*>(0x0064a770);                 // static result buffer

// One gamma ramp entry: _ftol(_CIpow(v, exponent) * 65535) as 0x005091c0 computes it. _CIpow (0x00566790) takes
// its operands on the x87 stack and _ftol (0x0056a7c8) pops st0 into edx:eax, so both are called directly; the
// low 16 bits of the result are stored.
uint16_t GammaRampEntry(float v, float exponent)
{
    static const uint32_t powFn = 0x00566790;
    static const uint32_t ftolFn = 0x0056a7c8;
    static const float scale = 65535.0f;  // the game's constant at 0x005bcc3c
    uint16_t result;
    __asm {
        push ebx
        push esi
        push edi
        push ebp
        fld dword ptr [v]
        fld dword ptr [exponent]
        call dword ptr [powFn]
        fmul dword ptr [scale]
        call dword ptr [ftolFn]
        pop ebp
        pop edi
        pop esi
        pop ebx
        mov result, ax
    }
    return result;
}
}  // namespace

uint32_t Renderer_DrawGlyph(uint32_t character, Renderer* r, uint32_t colour, float* penX, float* penY, uint32_t z,
                            float alignX, float alignY, float scaleX, float scaleY, float pixelW, float pixelH,
                            uint32_t flags);  // auto_00503900.cpp
uint32_t Format_BitsPerPixel(uint32_t format);  // textures.cpp

// ---------------------------------------------------------------------------------------------------------------

// 0x00507c10 (ecx = renderer; [text (16-bit chars), colour, &x, &y, z, alignX, alignY, scaleX, scaleY, pixelW,
// pixelH, maxChars, flags], callee pops 0x34): renderer virtual 114 (+0x1c8), the wide-text version that
// Renderer_DrawTextA (0x00507f50) widens into (not executed in the comparison scenarios). pixelW/pixelH of 0.0
// come from virtuals 32/33 (+0x80/+0x84, argument 0); sx = scaleX * pixelW, sy = scaleY * pixelH. alignY is
// replaced by 1 - alignY and, when non-zero, virtual 112 (+0x1c0: &w, &h, text, sx, sy, 0, maxChars, flags)
// measures the text and y += h * alignY. At the start of each line x = *penX, minus w * alignX (virtual 112 with
// mode 1) when alignX is non-zero; that x is kept as the line start. Unless flags bit 0, tags are parsed: while
// virtual 109 (+0x1b4) says a tag starts and virtual 108 (+0x1b0) does not report a line break, virtual 105
// (+0x1a4: &text, &x, line start, sx), then 107 (+0x1ac, Renderer_TextSkipTaggedBlock: &text, flags), then
// (unless flags bit 2) 104 (+0x1a0: &text, original colour, &new colour) which changes the colour, else 102
// (+0x198: &text) consume it. A tag line break (virtual 108, then 102 skips it) or a '\n' moves y down by the
// height from virtual 111 (+0x1bc: &w, &h, 10, sx, sy) and starts a new line. Every character, '\n' included,
// is drawn with Renderer_DrawGlyph (alignX 0, alignY 1.0, the unscaled scaleX/scaleY); EAX for it carries the
// high half of sx's bits, as in the original. Stops at the terminator or after maxChars (when non-zero)
// characters. Stores x and y back; returns 0.
uint32_t Renderer_DrawTextW(Renderer* r, const uint16_t* text, uint32_t colour, float* penX, float* penY, uint32_t z,
                            float alignX, float alignY, float scaleX, float scaleY, float pixelW, float pixelH,
                            uint32_t maxChars, uint32_t flags)
{
    bool newLine = true;
    uint32_t count = 0;
    const uint32_t originalColour = colour;  // the original keeps a copy and always passes it to virtual 104
    if (pixelW == 0.0f)
        pixelW = VCall<float>(r, 0x80 / 4, 0u);
    if (pixelH == 0.0f)
        pixelH = VCall<float>(r, 0x84 / 4, 0u);
    float sx = scaleX * pixelW;
    float x = *penX;
    float y = *penY;
    float sy = scaleY * pixelH;
    const bool parseTags = (flags & 1) == 0;
    alignY = 1.0f - alignY;
    float w = 0.0f;
    float h = 0.0f;
    if (!(alignY == 0.0f))
    {
        w = 0.0f;
        h = 0.0f;
        VCall<uint32_t>(r, 0x1c0 / 4, &w, &h, text, sx, sy, 0u, maxChars, flags);
        y = h * alignY + y;
    }
    float lineX = x;
    const uint32_t charHigh = FloatBits(sx) & 0xffff0000u;  // EDI holds sx; the character is loaded into DI
    if (*text != 0)
    {
        for (;;)
        {
            if (newLine)
            {
                x = *penX;
                if (!(alignX == 0.0f))
                {
                    w = 0.0f;
                    h = 0.0f;
                    VCall<uint32_t>(r, 0x1c0 / 4, &w, &h, text, sx, sy, 1u, maxChars, flags);
                    x = x - w * alignX;
                }
                newLine = false;
                lineX = x;
            }
            if (parseTags && VCall<bool>(r, 0x1b4 / 4, text))
            {
                while (!VCall<bool>(r, 0x1b0 / 4, text))
                {
                    if (!VCall<bool>(r, 0x1a4 / 4, &text, &x, lineX, sx) && !VCall<bool>(r, 0x1ac / 4, &text, flags))
                    {
                        uint32_t tagColour;
                        if (!(flags & 4) && VCall<bool>(r, 0x1a0 / 4, &text, originalColour, &tagColour))
                            colour = tagColour;
                        else
                            VCall<uint32_t>(r, 0x198 / 4, &text);
                    }
                    if (!VCall<bool>(r, 0x1b4 / 4, text))
                        break;
                }
            }
            if (*text == 0)
                break;
            uint32_t character;
            bool lineBreak;
            if (parseTags && VCall<bool>(r, 0x1b0 / 4, text))
            {
                character = 10;
                VCall<uint32_t>(r, 0x198 / 4, &text);
                lineBreak = true;
            }
            else
            {
                uint16_t c = *text++;
                character = charHigh | c;
                lineBreak = c == 10;
            }
            if (lineBreak)
            {
                VCall<uint32_t>(r, 0x1bc / 4, &w, &h, 10u, sx, sy);
                y = y - h;
                newLine = true;
            }
            Renderer_DrawGlyph(character, r, colour, &x, &y, z, 0.0f, 1.0f, scaleX, scaleY, pixelW, pixelH, flags);
            if (maxChars != 0 && ++count >= maxChars)
                break;
            if (*text == 0)
                break;
        }
    }
    *penX = x;
    *penY = y;
    return 0;
}

static uint32_t Renderer_DrawTextW_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4,
                                       uint32_t a5, uint32_t a6, uint32_t a7, uint32_t a8, uint32_t a9, uint32_t a10,
                                       uint32_t a11, uint32_t a12)
{
    return Renderer_DrawTextW(reinterpret_cast<Renderer*>(ecx), reinterpret_cast<const uint16_t*>(a0), a1,
                              reinterpret_cast<float*>(a2), reinterpret_cast<float*>(a3), a4, AsFloat(a5), AsFloat(a6),
                              AsFloat(a7), AsFloat(a8), AsFloat(a9), AsFloat(a10), a11, a12);
}
ABI_ENTRY_00507c10(Renderer_DrawTextW_Entry, Renderer_DrawTextW_Abi)
HOOK(0x00507c10, "Renderer_DrawTextW", Renderer_DrawTextW_Entry, nullptr);

// 0x00508860 (ecx = renderer; result in st0): renderer virtual 34 (+0x88). Returns the aspect value at +0x1648,
// which 0x00503b30 sets to width / height * 0.75 (not executed in the comparison scenarios).
float Renderer_GetAspect(Renderer* r)
{
    return Field<float>(r, 0x1648);
}

bool (*g_gammaHandler)(float red, float green, float blue) = nullptr;

static float Renderer_GetAspect_Abi(uint32_t ecx)
{
    return Renderer_GetAspect(reinterpret_cast<Renderer*>(ecx));
}
ABI_ENTRY_00508860(Renderer_GetAspect_Entry, Renderer_GetAspect_Abi)
HOOK(0x00508860, "Renderer_GetAspect", Renderer_GetAspect_Entry, nullptr);

// 0x005091c0 (ecx = renderer; [red, green, blue gamma], callee pops 0xc): renderer virtual 16 (+0x40) (not
// executed in the comparison scenarios). Each gamma is clamped: above 2.0 (0x005bcb80) becomes 2.0, else below 0
// becomes 0. Only when caps flags +0x15fd bit 2 is set: entry i of each channel = _ftol(pow(i * (1/255), 1/gamma)
// * 65535) (constants 0x005bcc18, 0x005bcc3c), then IDirect3DDevice9::SetGammaRamp(0, 0, ramp). Returns 1 then,
// else 0. The original's stack cookie check has no observable effect and is not reproduced.
uint32_t Renderer_SetGamma(Renderer* r, float red, float green, float blue)
{
    if (red > 2.0f)
        red = 2.0f;
    else if (red < 0.0f)
        red = 0.0f;
    if (green > 2.0f)
        green = 2.0f;
    else if (green < 0.0f)
        green = 0.0f;
    if (blue > 2.0f)
        blue = 2.0f;
    else if (blue < 0.0f)
        blue = 0.0f;
    if (g_gammaHandler && g_gammaHandler(red, green, blue))
        return 1;
    if (!(r->capsFlags & 4))
        return 0;
    float invRed = 1.0f / red;
    float invGreen = 1.0f / green;
    float invBlue = 1.0f / blue;
    const float step = *reinterpret_cast<const float*>(0x005bcc18);  // 1/255
    D3DGAMMARAMP ramp;
    for (int32_t i = 0; i < 0x100; ++i)
    {
        float v = static_cast<float>(i) * step;
        ramp.red[i] = GammaRampEntry(v, invRed);
        ramp.green[i] = GammaRampEntry(v, invGreen);
        ramp.blue[i] = GammaRampEntry(v, invBlue);
    }
    r->device->SetGammaRamp(0, 0, &ramp);
    return 1;
}

static uint32_t Renderer_SetGamma_Abi(uint32_t ecx, uint32_t red, uint32_t green, uint32_t blue)
{
    return Renderer_SetGamma(reinterpret_cast<Renderer*>(ecx), AsFloat(red), AsFloat(green), AsFloat(blue));
}
ABI_ENTRY_005091c0(Renderer_SetGamma_Entry, Renderer_SetGamma_Abi)
HOOK(0x005091c0, "Renderer_SetGamma", Renderer_SetGamma_Entry, nullptr);

// 0x00509a40 (ecx = renderer; [option letter], callee pops 4): renderer virtual 90 (+0x168), toggles a debug
// option chosen by an ASCII letter (jump table 0x00509b3c/0x00509b58; not executed in the comparison scenarios).
// 'B' flips +0x1628 bit 2 (point filtering in Renderer_BeginScene 0x005042d0), 'E' bit 1 (wireframe fill there),
// 'O' bit 4, 'T' bit 3; 'F' calls virtual 85 (+0x154) with (3, !virtual 91('F')); 'V' switches the shader path +0x33c between
// null and renderer + 0x340, then virtual 11 (+0x2c, Renderer_ResetDeviceState). Returns what the original leaves
// in EAX: the old option byte for the bit toggles, the virtual's result for 'F'/'V', letter - 0x42 otherwise.
uint32_t Renderer_ToggleOption(Renderer* r, uint32_t letter)
{
    uint8_t& options = Field<uint8_t>(r, 0x1628);
    uint8_t old = options;
    switch (letter)
    {
    case 'B':
        options = old ^ 4;
        return old;
    case 'E':
        options = old ^ 2;
        return old;
    case 'O':
        options = old ^ 0x10;
        return old;
    case 'T':
        options = old ^ 8;
        return old;
    case 'F':
    {
        uint32_t on = VCall<uint32_t>(r, 0x16c / 4, 0x46u);
        // neg eax / sbb al, al / inc al: low byte = (on == 0), upper bytes from -on.
        uint32_t arg = ((0u - on) & 0xffffff00u) | (on == 0 ? 1u : 0u);
        return VCall<uint32_t>(r, 0x154 / 4, 3u, arg);
    }
    case 'V':
        if (Field<uint32_t>(r, 0x33c) == 0)
            Field<uint32_t>(r, 0x33c) = u(r) + 0x340;
        else
            Field<uint32_t>(r, 0x33c) = 0;
        return VCall<uint32_t>(r, 0x2c / 4);
    default:
        return letter - 0x42;
    }
}

// 0x00509b70 (ecx = renderer; [option letter], callee pops 4): renderer virtual 91 (+0x16c), reads the options
// that virtual 90 toggles (jump table 0x00509be4/0x00509bfc; not executed in the comparison scenarios): 'B' +0x1628
// bit 2, 'E' bit 1, 'O' bit 4, 'T' bit 3, 'F' the low byte of virtual 86 (+0x158) with 3. Anything else 0.
uint32_t Renderer_GetOption(Renderer* r, uint32_t letter)
{
    uint32_t options = Field<uint8_t>(r, 0x1628);
    switch (letter)
    {
    case 'B': return (options >> 2) & 1;
    case 'E': return (options >> 1) & 1;
    case 'O': return (options >> 4) & 1;
    case 'T': return (options >> 3) & 1;
    case 'F': return VCall<uint32_t>(r, 0x158 / 4, 3u) & 0xff;
    default: return 0;
    }
}

static uint32_t Renderer_ToggleOption_Abi(uint32_t ecx, uint32_t letter)
{
    return Renderer_ToggleOption(reinterpret_cast<Renderer*>(ecx), letter);
}
static uint32_t Renderer_GetOption_Abi(uint32_t ecx, uint32_t letter)
{
    return Renderer_GetOption(reinterpret_cast<Renderer*>(ecx), letter);
}
ABI_ENTRY_00509a40(Renderer_ToggleOption_Entry, Renderer_ToggleOption_Abi)
ABI_ENTRY_00509b70(Renderer_GetOption_Entry, Renderer_GetOption_Abi)
HOOK(0x00509a40, "Renderer_ToggleOption", Renderer_ToggleOption_Entry, nullptr);
HOOK(0x00509b70, "Renderer_GetOption", Renderer_GetOption_Entry, nullptr);

// 0x00509c10 (ecx = renderer; [index], callee pops 4): renderer virtual 88 (+0x160), returns one value by index
// (jump table 0x00509cc4; not executed in the comparison scenarios): 0 fullscreen (+0x1628 bit 5 clear), 1 width
// (+0x1634), 2 height (+0x1638), 3 max texture width (+0x14e8), 4 max texture height (+0x14f0), 5 colour bits
// (+0x163c), 6 depth bits (+0x1640) (both set by 0x00503b30), 7 +0x14e0 (capability bits; 0x0050bb30 tests bit 14 for render targets), 8 the shader level
// (0 without the shader path +0x33c, else 2 + (pixel shader +0x3a0 set)), 9 the sum of the six counters
// +0x1600..+0x1614 (triangles, see 0x00509d10). Anything else 0.
uint32_t Renderer_QueryInfo(Renderer* r, uint32_t index)
{
    switch (index)
    {
    case 0: return (~(static_cast<uint32_t>(Field<uint8_t>(r, 0x1628)) >> 5)) & 1;
    case 1: return Field<uint32_t>(r, 0x1634);
    case 2: return Field<uint32_t>(r, 0x1638);
    case 3: return Field<uint32_t>(r, 0x14e8);
    case 4: return Field<uint32_t>(r, 0x14f0);
    case 5: return Field<uint32_t>(r, 0x163c);
    case 6: return Field<uint32_t>(r, 0x1640);
    case 7: return Field<uint32_t>(r, 0x14e0);
    case 8:
        if (Field<uint32_t>(r, 0x33c) == 0)
            return 0;
        return (Field<uint32_t>(r, 0x3a0) != 0 ? 1u : 0u) + 2;
    case 9:
        return Field<uint32_t>(r, 0x1614) + Field<uint32_t>(r, 0x1610) + Field<uint32_t>(r, 0x160c) +
               Field<uint32_t>(r, 0x1608) + Field<uint32_t>(r, 0x1604) + Field<uint32_t>(r, 0x1600);
    default: return 0;
    }
}

static uint32_t Renderer_QueryInfo_Abi(uint32_t ecx, uint32_t index)
{
    return Renderer_QueryInfo(reinterpret_cast<Renderer*>(ecx), index);
}
ABI_ENTRY_00509c10(Renderer_QueryInfo_Entry, Renderer_QueryInfo_Abi)
HOOK(0x00509c10, "Renderer_QueryInfo", Renderer_QueryInfo_Entry, nullptr);

// 0x00509cf0 (ecx = renderer): renderer virtual 50 (+0xc8), zeroes the ten statistics counters +0x1600..+0x1624
// (rep stosd; the ones printed by 0x00509d10) (not executed in the comparison scenarios). Returns 0 (EAX).
uint32_t Renderer_ResetStats(Renderer* r)
{
    memset(&Field<uint8_t>(r, 0x1600), 0, 10 * 4);
    return 0;
}

static uint32_t Renderer_ResetStats_Abi(uint32_t ecx)
{
    return Renderer_ResetStats(reinterpret_cast<Renderer*>(ecx));
}
ABI_ENTRY_00509cf0(Renderer_ResetStats_Entry, Renderer_ResetStats_Abi)
HOOK(0x00509cf0, "Renderer_ResetStats", Renderer_ResetStats_Entry, nullptr);

// 0x00509d10 (ecx = renderer; [index], callee pops 4): renderer virtual 89 (+0x164), returns the static buffer
// 0x0064a770 holding (not executed in the comparison scenarios): 0 the renderer name "PC DX8 - v1.0" (14 bytes
// copied from 0x005894d4), 1 "ITri %d LTri %d FTri %d LLine %d NMode %d" (0x005894a8, sprintf 0x0053814f) with
// +0x1600, +0x160c, +0x1614, +0x161c, +0x1624; anything else an empty string.
const char* Renderer_GetInfoString(Renderer* r, uint32_t index)
{
    kInfoStringBuffer[0] = 0;
    if (index == 0)
    {
        memcpy(kInfoStringBuffer, kStrRendererName, 14);
    }
    else if (index == 1)
    {
        GameFunction<int(__cdecl*)(char*, const char*, ...)>(0x0053814f)(
            kInfoStringBuffer, kStrStatsFormat, Field<uint32_t>(r, 0x1600), Field<uint32_t>(r, 0x160c),
            Field<uint32_t>(r, 0x1614), Field<uint32_t>(r, 0x161c), Field<uint32_t>(r, 0x1624));
    }
    return kInfoStringBuffer;
}

static uint32_t Renderer_GetInfoString_Abi(uint32_t ecx, uint32_t index)
{
    return u(Renderer_GetInfoString(reinterpret_cast<Renderer*>(ecx), index));
}
ABI_ENTRY_00509d10(Renderer_GetInfoString_Entry, Renderer_GetInfoString_Abi)
HOOK(0x00509d10, "Renderer_GetInfoString", Renderer_GetInfoString_Entry, nullptr);

// 0x00509da0 (ecx = renderer; [&list], callee pops 4): renderer virtual 13 (+0x34), the display mode list (not
// executed in the comparison scenarios). With modes (+0x9f0 count, +0x9f4 D3DDISPLAYMODE array): allocate count *
// 12 bytes from the game heap (0x004e07f0, category 1, alignment 2, "gfx_modes") and fill {Width, Height,
// Format_BitsPerPixel(Format)} per mode; *list = the array. With none, *list = null. Returns the count.
int32_t Renderer_GetDisplayModes(Renderer* r, uint32_t** list)
{
    int32_t count = static_cast<int32_t>(r->modeCount);
    if (count == 0)
    {
        *list = nullptr;
        return 0;
    }
    auto* out = reinterpret_cast<uint32_t*>(orig::fn_004e07f0(kHeap, 1, count * 12, 2, u(kStrGfxModes)));
    for (int32_t i = 0; i < count; ++i)
    {
        out[i * 3 + 0] = r->modes[i].Width;
        out[i * 3 + 1] = r->modes[i].Height;
        out[i * 3 + 2] = Format_BitsPerPixel(r->modes[i].Format);
    }
    *list = out;
    return count;
}

static uint32_t Renderer_GetDisplayModes_Abi(uint32_t ecx, uint32_t list)
{
    return static_cast<uint32_t>(
        Renderer_GetDisplayModes(reinterpret_cast<Renderer*>(ecx), reinterpret_cast<uint32_t**>(list)));
}
ABI_ENTRY_00509da0(Renderer_GetDisplayModes_Entry, Renderer_GetDisplayModes_Abi)
HOOK(0x00509da0, "Renderer_GetDisplayModes", Renderer_GetDisplayModes_Entry, nullptr);
