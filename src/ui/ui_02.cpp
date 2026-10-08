// Recovered: UI batch ui_02 (0x00465470-0x004907b0: ui_text objects, menu widgets and helpers). Static recovery
// 2026-10-05, compile-checked only (the game was not run); the oracle (tools/compare.py) is still to do.
//
// Conventions: every function's comment gives its address, its register/stack ABI (as the original reads it), what it
// does and the evidence. "guess" marks interpretations not proven by strings or behaviour. Names stay fn_XXXXXXXX
// unless the behaviour was verified from the disassembly. g = the Game object (0x005fe490). "ui_text" = the text
// object whose members +0x4a0 (char name[0x20]), +0x4c0 (wide text pointer), +0x4c4 (owned wide buffer), +0x4c8/+0x4cc
// (scale / base scale), +0x4d8/+0x4dc (measured size), +0x4e4 (colour), +0x4e8..+0x4f4 (position, alignment) and
// +0x474 (flags; 0x2000 = layout dirty) are described in docs/subsystems/ui.md.
#include <cmath>
#include <cstdarg>
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
inline uint8_t* B(uint32_t v)
{
    return reinterpret_cast<uint8_t*>(v);
}
inline uint32_t fb(float f)
{
    uint32_t v;
    std::memcpy(&v, &f, 4);
    return v;
}
inline float bf(uint32_t v)
{
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}
// A float constant of the game's .rdata (0x005bcb2c = 1.0, 0x005bcb4c = 0.5, 0x005bcbbc = 32.0, 0x005bcbe0 = 4.0,
// 0x005bcbf8 = 1/640, 0x005bcbf4 = 1/480, 0x005bcbfc = 0.1, 0x0056c05c = 0.0).
inline float K(uint32_t address)
{
    return GameGlobal<float>(address);
}
inline uint8_t* G()
{
    return reinterpret_cast<uint8_t*>(g_game);
}
inline void* Renderer()
{
    return Field<void*>(G(), 0x38);
}
// Renderer virtual +0x160 (QueryInfo): 1 = back-buffer width, 2 = height (docs/subsystems/ui.md).
inline int32_t QueryInfo(uint32_t what)
{
    return VCall<int32_t>(Renderer(), 0x160 / 4, what);
}
// 0x004e07f0 (ecx = heap 0x0063a210; stack: size, align?, flags, tag): the game's allocator.
inline uint32_t Alloc(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return orig::fn_004e07f0(0x0063a210u, a0, a1, a2, a3);
}
// 0x004e08d0 (ecx = heap 0x0063a210; stack: block): the game's free.
inline uint32_t Free(uint32_t block)
{
    return orig::fn_004e08d0(0x0063a210u, block);
}
// 0x00538a66: the CRT's vswprintf (buffer, format, va_list) (the body is the CRT string-stream setup, count
// 0x7fffffff, flags 0x42).
inline int Vswprintf(wchar_t* buffer, const wchar_t* format, va_list va)
{
    return GameFunction<int(__cdecl*)(wchar_t*, const wchar_t*, va_list)>(0x00538a66)(buffer, format, va);
}
}  // namespace

// Prototypes (functions of this file call each other directly).
uint32_t UiText_ExtendBounds(uint32_t eax, uint8_t* text, float x0, float y0, float x1, float y1);
uint32_t UiText_SetBounds(uint8_t* text, float x, float y, float alignX, float alignY, float width, float height,
                          uint32_t reset);
uint32_t UiText_Layout(uint8_t* text);
float UiText_FitScale(uint32_t font, float scale, float width, float height);
uint32_t UiText_FitToBox(uint8_t* text, float width, float height);
uint32_t UiText_SetName(const char* name, uint8_t* text, uint32_t lookup);
uint32_t UiText_SetWideText(const wchar_t* source, uint8_t* text);
uint32_t UiText_VFormat(uint8_t* text, const wchar_t* format, va_list va);
uint32_t UiText_Format(uint8_t* text, const wchar_t* format, ...);

// ================================================================================================================
// ui_text layout
// ================================================================================================================

// 0x00465470 (al = reset flag, ecx = ui_text; stack: x0, y0, x1, y1 floats; ret 0x10): grows the box +0x490 (min x),
// +0x494 (max x), +0x498 (min y), +0x49c (max y) to include (x0, y0) and (x1, y1). With al set, the box is first
// reset to x = min(x0, x1), y = min(y0, y1) (both min and max, as the original does). The returned EAX is the last
// stored coordinate (y1) when it lowered min y, otherwise an FPU status word (returned as 0 here). Evidence: the
// disassembly (fcomp / test ah,5 = "less", test ah,0x41 = "greater").
uint32_t UiText_ExtendBounds(uint32_t eax, uint8_t* text, float x0, float y0, float x1, float y1)
{
    float& minX = Field<float>(text, 0x490);
    float& maxX = Field<float>(text, 0x494);
    float& minY = Field<float>(text, 0x498);
    float& maxY = Field<float>(text, 0x49c);
    if (static_cast<uint8_t>(eax) != 0)
    {
        const float x = x0 < x1 ? x0 : x1;
        maxX = x;
        minX = x;
        const float y = y0 < y1 ? y0 : y1;
        maxY = y;
        minY = y;
    }
    if (x0 < minX)
        minX = x0;
    else if (x0 > maxX)
        maxX = x0;
    if (y0 < minY)
        minY = y0;
    else if (y0 > maxY)
        maxY = y0;
    if (x1 < minX)
        minX = x1;
    else if (x1 > maxX)
        maxX = x1;
    if (y1 < minY)
    {
        minY = y1;
        return fb(y1);
    }
    if (y1 > maxY)
        maxY = y1;
    return 0;
}

static uint32_t UiText_ExtendBounds_Abi(uint32_t eax, uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return UiText_ExtendBounds(eax, B(ecx), bf(a0), bf(a1), bf(a2), bf(a3));
}
ABI_ENTRY_00465470(UiText_ExtendBounds_Entry, UiText_ExtendBounds_Abi)
HOOK(0x00465470, "UiText_ExtendBounds", UiText_ExtendBounds_Entry, nullptr);

// 0x004655b0 (stack: text, x, y, alignX, alignY, width, height, reset (byte); ret 0x20): converts a box of
// width x height back-buffer pixels placed at normalised (x, y) with alignment (alignX, alignY) into normalised
// corners using px = 1 / QueryInfo(1) and py = 1 / QueryInfo(2), with a margin of 32 pixels in x and 4 in y, and
// passes them to UiText_ExtendBounds (al = reset): (x + ((1 - alignX) * w + 32) * px, y + ((1 - alignY) * h + 4) * py,
// x - (alignX * w + 32) * px, y - (alignY * h + 4) * py). Returns UiText_ExtendBounds' EAX.
uint32_t UiText_SetBounds(uint8_t* text, float x, float y, float alignX, float alignY, float width, float height,
                          uint32_t reset)
{
    const float px = K(0x005bcb2c) / static_cast<float>(QueryInfo(1));
    const float py = K(0x005bcb2c) / static_cast<float>(QueryInfo(2));
    const float y1 = y - (alignY * height + K(0x005bcbe0)) * py;
    const float x1 = x - (alignX * width + K(0x005bcbbc)) * px;
    const float y0 = (K(0x005bcb2c) - alignY) * height + K(0x005bcbe0);
    const float y0f = y0 * py + y;
    const float x0f = ((K(0x005bcb2c) - alignX) * width + K(0x005bcbbc)) * px + x;
    return UiText_ExtendBounds(reset & 0xff, text, x0f, y0f, x1, y1);
}

static uint32_t UiText_SetBounds_Abi(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5,
                                     uint32_t a6, uint32_t a7)
{
    return UiText_SetBounds(B(a0), bf(a1), bf(a2), bf(a3), bf(a4), bf(a5), bf(a6), a7);
}
ABI_ENTRY_004655b0(UiText_SetBounds_Entry, UiText_SetBounds_Abi)
HOOK(0x004655b0, "UiText_SetBounds", UiText_SetBounds_Entry, nullptr);

// 0x00465680 (ecx = ui_text): clears the layout-dirty bit 0x2000 of +0x474, measures the text +0x4c0 with font g+0xac0
// at scale +0x4c8 into +0x4d8/+0x4dc (0x00495fa0, edx/esi = out pointers), then sets the bounds from the position,
// alignment and measured size (UiText_SetBounds, reset 1). Returns its EAX.
uint32_t UiText_Layout(uint8_t* text)
{
    Field<uint32_t>(text, 0x474) &= 0xffffdfffu;
    orig::fn_00495fa0(u(text + 0x4d8), u(text + 0x4dc), Field<uint32_t>(G(), 0xac0), Field<uint32_t>(text, 0x4c0),
                      Field<uint32_t>(text, 0x4c8));
    return UiText_SetBounds(text, Field<float>(text, 0x4e8), Field<float>(text, 0x4ec), Field<float>(text, 0x4f0),
                            Field<float>(text, 0x4f4), Field<float>(text, 0x4d8), Field<float>(text, 0x4dc), 1);
}

static uint32_t UiText_Layout_Abi(uint32_t ecx)
{
    return UiText_Layout(B(ecx));
}
ABI_ENTRY_00465680(UiText_Layout_Entry, UiText_Layout_Abi)
HOOK(0x00465680, "UiText_Layout", UiText_Layout_Entry, nullptr);

// 0x004656f0 (esi = ui_text; stack: width, height (640x480 units); ret 8): +0x4c8 = UiText_FitScale(font +0x4c0,
// base scale +0x4cc, width, height). The original's EAX is a leftover of 0x00465720 (returned as 0).
uint32_t UiText_FitToBox(uint8_t* text, float width, float height)
{
    Field<float>(text, 0x4c8) =
        UiText_FitScale(Field<uint32_t>(text, 0x4c0), Field<float>(text, 0x4cc), width, height);
    return 0;
}

static uint32_t UiText_FitToBox_Abi(uint32_t esi, uint32_t a0, uint32_t a1)
{
    return UiText_FitToBox(B(esi), bf(a0), bf(a1));
}
ABI_ENTRY_004656f0(UiText_FitToBox_Entry, UiText_FitToBox_Abi)
HOOK(0x004656f0, "UiText_FitToBox", UiText_FitToBox_Entry, nullptr);

// 0x00465720 (ebx = wide text; stack: scale, width, height (640x480 units; 0 = unlimited); caller pops; returns st0):
// text auto-fit (docs/subsystems/ui.md). Converts width/height to back-buffer pixels (QueryInfo(1) * width / 640,
// QueryInfo(2) * height / 480), then binary-searches 16 times between 0.1 * scale and scale, measuring the text with
// font g+0xac0 (0x00495fa0). Returns the last scale that fitted; when the first try (scale itself) fits it returns at
// once; when nothing fits it returns 0.1 * scale.
float UiText_FitScale(uint32_t text, float scale, float width, float height)
{
    width = static_cast<float>(QueryInfo(1)) * width * K(0x005bcbf8);
    height = static_cast<float>(QueryInfo(2)) * height * K(0x005bcbf4);
    float hi = scale;
    float current = scale;
    float lo = scale * K(0x005bcbfc);
    float result = lo;
    int32_t i = 0;
    do
    {
        float measured[2];  // [esp+0xc] = width, [esp+0x10] = height (0x00495fa0 edx/esi outputs)
        orig::fn_00495fa0(u(&measured[0]), u(&measured[1]), Field<uint32_t>(G(), 0xac0), text, fb(current));
        const bool fits = (width == K(0x0056c05c) || measured[0] <= width) &&
                          (height == K(0x0056c05c) || measured[1] <= height);
        if (!fits)
        {
            hi = current;
        }
        else
        {
            result = current;
            if (i == 0)
                return result;
            lo = current;
        }
        ++i;
        current = (hi + lo) * K(0x005bcb4c);
    } while (i < 16);
    return result;
}

static float UiText_FitScale_Abi(uint32_t ebx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return UiText_FitScale(ebx, bf(a0), bf(a1), bf(a2));
}
ABI_ENTRY_00465720(UiText_FitScale_Entry, UiText_FitScale_Abi)
HOOK(0x00465720, "UiText_FitScale", UiText_FitScale_Entry, nullptr);

// 0x00465850 (ecx = name, edi = ui_text; stack: lookup (byte); ret 4): copies the name to +0x4a0 (strcpy); when lookup
// is set, +0x4c0 = Text_Lookup(eax = +0x4a0, ebx = string table g+0xac0; stack 0) and marks the layout dirty (+0x474 |=
// 0x2000). Returns Text_Lookup's result, else +0x4a0. Name: Text_Lookup is named in the tracker.
uint32_t UiText_SetName(const char* name, uint8_t* text, uint32_t lookup)
{
    char* dst = reinterpret_cast<char*>(text + 0x4a0);
    char c;
    do
    {
        c = *name++;
        *dst++ = c;
    } while (c != 0);
    if (static_cast<uint8_t>(lookup) == 0)
        return u(text + 0x4a0);
    const uint32_t r = orig::fn_00495f00(u(text + 0x4a0), Field<uint32_t>(G(), 0xac0), 0);
    Field<uint32_t>(text, 0x4c0) = r;
    Field<uint32_t>(text, 0x474) |= 0x2000;
    return r;
}

static uint32_t UiText_SetName_Abi(uint32_t ecx, uint32_t edi, uint32_t a0)
{
    return UiText_SetName(reinterpret_cast<const char*>(ecx), B(edi), a0);
}
ABI_ENTRY_00465850(UiText_SetName_Entry, UiText_SetName_Abi)
HOOK(0x00465850, "UiText_SetName", UiText_SetName_Entry, nullptr);

// 0x004658a0 (eax = wide string, esi = ui_text): sets the displayed text to a private copy: null or empty -> +0x4c0 =
// the empty string 0x005a0d1c; otherwise allocates the 0x800-byte buffer +0x4c4 once (Alloc(6, 0x800, 2, 0)), copies at
// most about 0x3ff characters (three per step, as the original) plus a terminator, +0x4c0 = +0x4c4. Then clears the
// name +0x4a0 and marks the layout dirty. Returns the copied count (or the input pointer for null/empty).
uint32_t UiText_SetWideText(const wchar_t* source, uint8_t* text)
{
    uint32_t r;
    if (source == nullptr || source[0] == 0)
    {
        r = u(source);
        Field<uint32_t>(text, 0x4c0) = 0x005a0d1cu;
    }
    else
    {
        if (Field<uint32_t>(text, 0x4c4) == 0)
            Field<uint32_t>(text, 0x4c4) = Alloc(6, 0x800, 2, 0);
        int32_t n = 0;
        for (;;)
        {
            if (source[0] == 0)
                break;
            Field<wchar_t*>(text, 0x4c4)[n] = source[0];
            if (source[1] == 0)
            {
                n += 1;
                break;
            }
            Field<wchar_t*>(text, 0x4c4)[n + 1] = source[1];
            if (source[2] == 0)
            {
                n += 2;
                break;
            }
            Field<wchar_t*>(text, 0x4c4)[n + 2] = source[2];
            n += 3;
            source += 3;
            if (n >= 0x3ff)
                break;
        }
        Field<wchar_t*>(text, 0x4c4)[n] = 0;
        Field<uint32_t>(text, 0x4c0) = Field<uint32_t>(text, 0x4c4);
        r = static_cast<uint32_t>(n);
    }
    Field<uint8_t>(text, 0x4a0) = 0;
    Field<uint32_t>(text, 0x474) |= 0x2000;
    return r;
}

static uint32_t UiText_SetWideText_Abi(uint32_t eax, uint32_t esi)
{
    return UiText_SetWideText(reinterpret_cast<const wchar_t*>(eax), B(esi));
}
ABI_ENTRY_004658a0(UiText_SetWideText_Entry, UiText_SetWideText_Abi)
HOOK(0x004658a0, "UiText_SetWideText", UiText_SetWideText_Entry, nullptr);

// 0x00465970 (esi = ui_text; stack: wide format, ... (variadic, caller pops); ecx listed by the ABI table but
// overwritten before use): vswprintf into a 0x1000-byte stack buffer (0x00538a66), then UiText_SetWideText. Hand-written
// entry stub: the generated one would copy only two stack arguments, the implementation needs the caller's va_list.
uint32_t UiText_VFormat(uint8_t* text, const wchar_t* format, va_list va)
{
    wchar_t buffer[0x800];
    Vswprintf(buffer, format, va);
    return UiText_SetWideText(buffer, text);
}

uint32_t UiText_Format(uint8_t* text, const wchar_t* format, ...)
{
    va_list va;
    va_start(va, format);
    const uint32_t r = UiText_VFormat(text, format, va);
    va_end(va);
    return r;
}

static uint32_t UiText_Format_Abi(uint32_t /*ecx*/, uint32_t esi, uint32_t args)
{
    const uint32_t* stack = reinterpret_cast<const uint32_t*>(args);
    return UiText_VFormat(B(esi), reinterpret_cast<const wchar_t*>(stack[0]),
                          reinterpret_cast<va_list>(const_cast<uint32_t*>(stack + 1)));
}
// clang-format off
static __declspec(naked) void UiText_Format_Entry()
{
    __asm push ecx __asm push edx __asm push ebx __asm push esi __asm push edi __asm push ebp
    __asm lea eax, [esp + 28] __asm push eax __asm push esi __asm push ecx
    __asm call UiText_Format_Abi __asm add esp, 12 __asm pop ebp __asm pop edi __asm pop esi __asm pop ebx
    __asm pop edx __asm pop ecx __asm ret
}
// clang-format on
HOOK(0x00465970, "UiText_Format", UiText_Format_Entry, nullptr);

// 0x004659b0 (eax = ui_text; stack: scale; ret 4): +0x4c8 = +0x4cc = scale, marks the layout dirty.
void UiText_SetScale(uint8_t* text, uint32_t scale)
{
    Field<uint32_t>(text, 0x4c8) = scale;
    Field<uint32_t>(text, 0x4cc) = scale;
    Field<uint32_t>(text, 0x474) |= 0x2000;
}

static void UiText_SetScale_Abi(uint32_t eax, uint32_t a0)
{
    UiText_SetScale(B(eax), a0);
}
ABI_ENTRY_004659b0(UiText_SetScale_Entry, UiText_SetScale_Abi)
HOOK(0x004659b0, "UiText_SetScale", UiText_SetScale_Entry, nullptr);

// 0x004659d0 (eax = ui_text, ecx = colour): +0x4e4 = colour (the draw colour, docs/subsystems/ui.md).
void UiText_SetColour(uint8_t* text, uint32_t colour)
{
    Field<uint32_t>(text, 0x4e4) = colour;
}

static void UiText_SetColour_Abi(uint32_t eax, uint32_t ecx)
{
    UiText_SetColour(B(eax), ecx);
}
ABI_ENTRY_004659d0(UiText_SetColour_Entry, UiText_SetColour_Abi)
HOOK(0x004659d0, "UiText_SetColour", UiText_SetColour_Entry, nullptr);

// 0x004659e0 (eax = ui_text; stack: x, y, alignX, alignY; ret 0x10): stores the position +0x4e8/+0x4ec and alignment
// +0x4f0/+0x4f4, marks the layout dirty (docs/subsystems/ui.md: 0x00464b10 places texts with it).
void UiText_SetPosition(uint8_t* text, uint32_t x, uint32_t y, uint32_t alignX, uint32_t alignY)
{
    Field<uint32_t>(text, 0x4e8) = x;
    Field<uint32_t>(text, 0x4ec) = y;
    Field<uint32_t>(text, 0x4f0) = alignX;
    Field<uint32_t>(text, 0x4f4) = alignY;
    Field<uint32_t>(text, 0x474) |= 0x2000;
}

static void UiText_SetPosition_Abi(uint32_t eax, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    UiText_SetPosition(B(eax), a0, a1, a2, a3);
}
ABI_ENTRY_004659e0(UiText_SetPosition_Entry, UiText_SetPosition_Abi)
HOOK(0x004659e0, "UiText_SetPosition", UiText_SetPosition_Entry, nullptr);

// 0x00465a20 (ecx = ui_text; no direct callers: a virtual update method): when the object is active (+0x470 bit 1 or
// +0x474 bit 2) and g+0x748 is set and g+0x26cc is clear and 0x004a1320(eax = 0x1c; stack: g+0x748 +0xc24) is
// non-zero: shows it (+0x474 |= 4) and formats "%d" (0x005a0d14) with 0x004a1300(0x1c) when 0x004a1660(state) == 2,
// otherwise "%d/%d" (0x005a0d08) with 0x004a1300(0x1c) and 0x004a1360(0x1c) (edx = g+0x748 +0xc24 for both);
// otherwise hides it (+0x474 &= ~4). guess: an ammo-style counter for item 0x1c. Returns the format's EAX or g+0x748 /
// 0 as the original (0 when inactive: the original returns its incoming EAX).
uint32_t fn_00465a20(uint8_t* text)
{
    if ((Field<uint8_t>(text, 0x470) & 1) == 0 && (Field<uint8_t>(text, 0x474) & 2) == 0)
        return 0;
    uint8_t* g = G();
    uint32_t eax = Field<uint32_t>(g, 0x748);
    if (eax != 0 && Field<uint32_t>(g, 0x26cc) == 0)
    {
        eax = orig::fn_004a1320(0x1c, Field<uint32_t>(g, 0x748) + 0xc24);
        if (eax != 0)
        {
            Field<uint32_t>(text, 0x474) |= 4;
            const uint32_t state = Field<uint32_t>(g, 0x748) + 0xc24;
            if (orig::fn_004a1660(state) == 2)
            {
                const uint32_t a = orig::fn_004a1300(0x1c, state);
                return UiText_Format(text, reinterpret_cast<const wchar_t*>(0x005a0d14), a);
            }
            const uint32_t b = orig::fn_004a1360(0x1c, state);
            const uint32_t a = orig::fn_004a1300(0x1c, state);
            return UiText_Format(text, reinterpret_cast<const wchar_t*>(0x005a0d08), a, b);
        }
    }
    Field<uint32_t>(text, 0x474) &= 0xfffffffbu;
    return eax;
}

static uint32_t fn_00465a20_Abi(uint32_t ecx)
{
    return fn_00465a20(B(ecx));
}
ABI_ENTRY_00465a20(fn_00465a20_Entry, fn_00465a20_Abi)
HOOK(0x00465a20, "fn_00465a20", fn_00465a20_Entry, nullptr);

// 0x00465ad0 (ecx = ui_text; virtual, no direct callers): when active (as 0x00465a20), g+0x26cc clear and g+0x748
// set: shows it and formats "%d" (0x005a0d00) with 0x004a1380(eax = 0x1c, edx = g+0x748 +0xc24); otherwise hides it.
// Returns the format's EAX, else g (as the original's EAX; 0 when inactive).
uint32_t fn_00465ad0(uint8_t* text)
{
    if ((Field<uint8_t>(text, 0x470) & 1) == 0 && (Field<uint8_t>(text, 0x474) & 2) == 0)
        return 0;
    uint8_t* g = G();
    if (Field<uint32_t>(g, 0x26cc) == 0 && Field<uint32_t>(g, 0x748) != 0)
    {
        Field<uint32_t>(text, 0x474) |= 4;
        const uint32_t a = orig::fn_004a1380(0x1c, Field<uint32_t>(g, 0x748) + 0xc24);
        return UiText_Format(text, reinterpret_cast<const wchar_t*>(0x005a0d00), a);
    }
    Field<uint32_t>(text, 0x474) &= 0xfffffffbu;
    return u(g);
}

static uint32_t fn_00465ad0_Abi(uint32_t ecx)
{
    return fn_00465ad0(B(ecx));
}
ABI_ENTRY_00465ad0(fn_00465ad0_Entry, fn_00465ad0_Abi)
HOOK(0x00465ad0, "fn_00465ad0", fn_00465ad0_Entry, nullptr);

// 0x00465b40 (ecx = ui_text; stack: 4 args; ret 0x10): calls 0x004651a0 (ecx = object, the same four arguments), then
// hides the object (+0x474 &= ~4). Returns 0x004651a0's result. Not executed in recorded runs.
uint32_t fn_00465b40(uint8_t* text, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    const uint32_t r = orig::fn_004651a0(u(text), a0, a1, a2, a3);
    Field<uint32_t>(text, 0x474) &= 0xfffffffbu;
    return r;
}

static uint32_t fn_00465b40_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_00465b40(B(ecx), a0, a1, a2, a3);
}
ABI_ENTRY_00465b40(fn_00465b40_Entry, fn_00465b40_Abi)
HOOK(0x00465b40, "fn_00465b40", fn_00465b40_Entry, nullptr);

namespace
{
// 0x0056a7c8 (_ftol2): truncates st0 to an integer; callers use EAX.
inline int32_t Ftol(float x)
{
    return static_cast<int32_t>(static_cast<int64_t>(x));
}
// The reference-counted handle release inlined in 0x00465de0, 0x00465e30, 0x00465f70, 0x00466010: when *slot is set,
// decrements its count +0x28; at zero, unless its virtual +0xd8 returns true, frees it with 0x00487da0 (eax = object);
// then clears the slot.
inline void ReleaseHandle(uint32_t* slot)
{
    const uint32_t h = *slot;
    if (h == 0)
        return;
    if (--Field<int32_t>(P(h), 0x28) == 0)
    {
        if (static_cast<uint8_t>(VCall<uint32_t>(P(h), 0xd8 / 4)) == 0)
            orig::fn_00487da0(h);
    }
    if (slot != nullptr)
        *slot = 0;
}
// The "is shown" test of the ui objects: +0x470 bit 1 or +0x474 bit 2.
inline bool UiActive(const uint8_t* obj)
{
    return (Field<uint8_t>(obj, 0x470) & 1) != 0 || (Field<uint8_t>(obj, 0x474) & 2) != 0;
}
}  // namespace

// 0x00465b70 (eax = ui_text; stack: seconds (float); ret 4): a timer display. When seconds >= 0: t = seconds + 0.9
// (0x005bcc38), minutes = ftol(t * 1/60 (0x005bcc98)), formats "%d:%02d" (0x005a0cf0) with minutes and
// ftol(t - minutes * 60), shows the object (+0x474 |= 4); otherwise hides it. Returns the new flags (0 when hidden: the
// original returns an FPU status word). Not executed in recorded runs.
uint32_t UiText_ShowTime(uint8_t* text, float seconds)
{
    if (!(seconds >= K(0x0056c05c)))  // fcomp / test ah,1: below or unordered
    {
        Field<uint32_t>(text, 0x474) &= 0xfffffffbu;
        return 0;
    }
    const float t = seconds + K(0x005bcc38);
    const int32_t minutes = Ftol(K(0x005bcc98) * t);
    const int32_t secs = Ftol(t - static_cast<float>(minutes * 60));
    UiText_Format(text, reinterpret_cast<const wchar_t*>(0x005a0cf0), minutes, secs);
    const uint32_t flags = Field<uint32_t>(text, 0x474) | 4;
    Field<uint32_t>(text, 0x474) = flags;
    return flags;
}

static uint32_t UiText_ShowTime_Abi(uint32_t eax, uint32_t a0)
{
    return UiText_ShowTime(B(eax), bf(a0));
}
ABI_ENTRY_00465b70(UiText_ShowTime_Entry, UiText_ShowTime_Abi)
HOOK(0x00465b70, "UiText_ShowTime", UiText_ShowTime_Entry, nullptr);

// 0x00465be0 (ecx = ui_text; virtual, no direct callers): when active and g+0x26ec > 0: sums 0x004063f0(eax = item)
// over the list g+0xa84 +0xef8 (iterator: 0x004e7340 first (eax = list, esi = iterator), 0x004e73c0 next (edi =
// iterator); the 8-byte iterator's second word, when set, is a locked object whose +0x18 is decremented afterwards),
// formats "%d" (0x005a0d00) with sum - g+0x26f4 and shows the object. guess: multiplayer score counter. Returns the
// iterator's second word (as the original's EAX), g when g+0x26ec is 0, 0 when inactive.
uint32_t fn_00465be0(uint8_t* text)
{
    if (!UiActive(text))
        return 0;
    if (Field<uint32_t>(G(), 0x26ec) == 0)
        return u(G());
    uint32_t iterator[2] = {0, 0};
    int32_t sum = 0;
    uint32_t item = orig::fn_004e7340(Field<uint32_t>(Field<void*>(G(), 0xa84), 0xef8), u(iterator));
    while (item != 0)
    {
        sum += static_cast<int32_t>(orig::fn_004063f0(item));
        item = orig::fn_004e73c0(u(iterator));
    }
    sum -= Field<int32_t>(G(), 0x26f4);
    UiText_Format(text, reinterpret_cast<const wchar_t*>(0x005a0d00), sum);
    Field<uint32_t>(text, 0x474) |= 4;
    if (iterator[1] != 0)
        --Field<int32_t>(P(iterator[1]), 0x18);
    return iterator[1];
}

static uint32_t fn_00465be0_Abi(uint32_t ecx)
{
    return fn_00465be0(B(ecx));
}
ABI_ENTRY_00465be0(fn_00465be0_Entry, fn_00465be0_Abi)
HOOK(0x00465be0, "fn_00465be0", fn_00465be0_Entry, nullptr);

// 0x00465c90 (ecx = ui_text; virtual, no direct callers): when active and g+0x26ec > 0: formats "%d" with g+0x26e8 and
// shows the object. Returns the new flags (g when g+0x26ec is 0, 0 when inactive).
uint32_t fn_00465c90(uint8_t* text)
{
    if (!UiActive(text))
        return 0;
    if (Field<uint32_t>(G(), 0x26ec) == 0)
        return u(G());
    UiText_Format(text, reinterpret_cast<const wchar_t*>(0x005a0d00), Field<uint32_t>(G(), 0x26e8));
    const uint32_t flags = Field<uint32_t>(text, 0x474) | 4;
    Field<uint32_t>(text, 0x474) = flags;
    return flags;
}

static uint32_t fn_00465c90_Abi(uint32_t ecx)
{
    return fn_00465c90(B(ecx));
}
ABI_ENTRY_00465c90(fn_00465c90_Entry, fn_00465c90_Abi)
HOOK(0x00465c90, "fn_00465c90", fn_00465c90_Entry, nullptr);

// 0x00465ce0 (ecx = ui_text; virtual, no direct callers): as 0x00465c90 with g+0x26f4.
uint32_t fn_00465ce0(uint8_t* text)
{
    if (!UiActive(text))
        return 0;
    if (Field<uint32_t>(G(), 0x26ec) == 0)
        return u(G());
    UiText_Format(text, reinterpret_cast<const wchar_t*>(0x005a0d00), Field<uint32_t>(G(), 0x26f4));
    const uint32_t flags = Field<uint32_t>(text, 0x474) | 4;
    Field<uint32_t>(text, 0x474) = flags;
    return flags;
}

static uint32_t fn_00465ce0_Abi(uint32_t ecx)
{
    return fn_00465ce0(B(ecx));
}
ABI_ENTRY_00465ce0(fn_00465ce0_Entry, fn_00465ce0_Abi)
HOOK(0x00465ce0, "fn_00465ce0", fn_00465ce0_Entry, nullptr);

// 0x00465d30 (ecx = ui object; virtual, no direct callers): shows the object (+0x474 |= 4) when g+0x26ec > 0. Returns g
// (EAX). Not executed in recorded runs.
uint32_t fn_00465d30(uint8_t* obj)
{
    if (Field<uint32_t>(G(), 0x26ec) != 0)
        Field<uint32_t>(obj, 0x474) |= 4;
    return u(G());
}

static uint32_t fn_00465d30_Abi(uint32_t ecx)
{
    return fn_00465d30(B(ecx));
}
ABI_ENTRY_00465d30(fn_00465d30_Entry, fn_00465d30_Abi)
HOOK(0x00465d30, "fn_00465d30", fn_00465d30_Entry, nullptr);

// 0x00465d50 (ecx = ui object; virtual, no direct callers): +0x4a0 = 0; when g+0x26fc > 0, +0x4a0 = 0x004592e0(eax =
// 0, ecx = g+0x26fc; stack: object) and its count +0x28 is incremented. Then creates the effect "fx_red_flag"
// (0x005a0cd8) with Fx_Create (al = 0, ecx = g+0xa90; stack: name, params), params = a zeroed 0x80-byte block (8-byte
// aligned frame) with [0] = 0x204, [6] = "ref_flag" (0x005a0ce4), [7] = the object. Returns Fx_Create's result.
uint32_t fn_00465d50(uint8_t* obj)
{
    Field<uint32_t>(obj, 0x4a0) = 0;
    const uint32_t list = Field<uint32_t>(G(), 0x26fc);
    if (list != 0)
    {
        const uint32_t h = orig::fn_004592e0(0, list, u(obj));
        Field<uint32_t>(obj, 0x4a0) = h;
        if (h != 0)
            ++Field<int32_t>(P(h), 0x28);
    }
    alignas(8) uint32_t params[32];
    std::memset(params, 0, sizeof(params));
    params[7] = u(obj);
    params[6] = 0x005a0ce4u;
    params[0] = 0x204;
    return orig::fn_0041cc60(0, Field<uint32_t>(G(), 0xa90), 0x005a0cd8u, u(params));
}

static uint32_t fn_00465d50_Abi(uint32_t ecx)
{
    return fn_00465d50(B(ecx));
}
ABI_ENTRY_00465d50(fn_00465d50_Entry, fn_00465d50_Abi)
HOOK(0x00465d50, "fn_00465d50", fn_00465d50_Entry, nullptr);

// 0x00465de0 (ecx = ui object; virtual, no direct callers): releases the handle +0x4a0 (see ReleaseHandle), then
// tail-jumps to Object_Remove (0x004547b0, ecx = object). Destroy-style method paired with 0x00465d50. Not executed
// in recorded runs.
uint32_t fn_00465de0(uint8_t* obj)
{
    ReleaseHandle(&Field<uint32_t>(obj, 0x4a0));
    return orig::fn_004547b0(u(obj));
}

static uint32_t fn_00465de0_Abi(uint32_t ecx)
{
    return fn_00465de0(B(ecx));
}
ABI_ENTRY_00465de0(fn_00465de0_Entry, fn_00465de0_Abi)
HOOK(0x00465de0, "fn_00465de0", fn_00465de0_Entry, nullptr);

// 0x00465e30 (ecx = ui object; virtual, no direct callers): when the handle +0x4a0 is set and its virtual +0xd8 returns
// false, releases it. Then, when active and the handle is set: anim 4 (0x00408800, eax = 4, ecx = object) gets
// +4 = (float)(unsigned)0x0044b480(handle) / (float)(unsigned)0x00415900(handle), Anim_Set(eax = 1, esi = object;
// stack: 0, 0x210, 0, 0, 0, 0), and the object is shown unless g+0xdce (cinematic) is set (then hidden). guess: a
// progress meter. Returns the flags (0 otherwise). Not executed in recorded runs.
uint32_t fn_00465e30(uint8_t* obj)
{
    uint32_t* slot = &Field<uint32_t>(obj, 0x4a0);
    if (*slot != 0 && static_cast<uint8_t>(VCall<uint32_t>(P(*slot), 0xd8 / 4)) == 0)
        ReleaseHandle(slot);
    if (!UiActive(obj) || *slot == 0)
        return 0;
    const uint32_t h = *slot;
    const uint32_t a = orig::fn_0044b480(h);
    const uint32_t b = orig::fn_00415900(h);
    const float ratio = static_cast<float>(a) / static_cast<float>(b);
    const uint32_t anim = orig::fn_00408800(4, u(obj));
    Field<float>(P(anim), 4) = ratio;
    orig::fn_00408d90(1, u(obj), 0, 0x210, 0, 0, 0, 0);
    uint32_t flags = Field<uint32_t>(obj, 0x474);
    if (Field<uint8_t>(G(), 0xdce) == 0)
        flags |= 4;
    else
        flags &= 0xfffffffbu;
    Field<uint32_t>(obj, 0x474) = flags;
    return flags;
}

static uint32_t fn_00465e30_Abi(uint32_t ecx)
{
    return fn_00465e30(B(ecx));
}
ABI_ENTRY_00465e30(fn_00465e30_Entry, fn_00465e30_Abi)
HOOK(0x00465e30, "fn_00465e30", fn_00465e30_Entry, nullptr);

// 0x00465f30 (ecx = ui object; virtual, no direct callers): as the first half of 0x00465d50 on the handle +0x4f8. Not
// executed in recorded runs.
uint32_t fn_00465f30(uint8_t* obj)
{
    Field<uint32_t>(obj, 0x4f8) = 0;
    const uint32_t list = Field<uint32_t>(G(), 0x26fc);
    if (list == 0)
        return u(G());
    const uint32_t h = orig::fn_004592e0(0, list, u(obj));
    Field<uint32_t>(obj, 0x4f8) = h;
    if (h != 0)
        ++Field<int32_t>(P(h), 0x28);
    return h;
}

static uint32_t fn_00465f30_Abi(uint32_t ecx)
{
    return fn_00465f30(B(ecx));
}
ABI_ENTRY_00465f30(fn_00465f30_Entry, fn_00465f30_Abi)
HOOK(0x00465f30, "fn_00465f30", fn_00465f30_Entry, nullptr);

// 0x00465f70 (ecx = ui_text; virtual, no direct callers): as 0x00465e30 on the handle +0x4f8, but displays
// "%d" (0x005a0d00) of 0x0044b480(handle) instead of driving an animation; shown unless g+0xdce is set. Returns the
// flags (0 otherwise).
uint32_t fn_00465f70(uint8_t* text)
{
    uint32_t* slot = &Field<uint32_t>(text, 0x4f8);
    if (*slot != 0 && static_cast<uint8_t>(VCall<uint32_t>(P(*slot), 0xd8 / 4)) == 0)
        ReleaseHandle(slot);
    if (!UiActive(text) || *slot == 0)
        return 0;
    UiText_Format(text, reinterpret_cast<const wchar_t*>(0x005a0d00), orig::fn_0044b480(*slot));
    uint32_t flags = Field<uint32_t>(text, 0x474);
    if (Field<uint8_t>(G(), 0xdce) == 0)
        flags |= 4;
    else
        flags &= 0xfffffffbu;
    Field<uint32_t>(text, 0x474) = flags;
    return flags;
}

static uint32_t fn_00465f70_Abi(uint32_t ecx)
{
    return fn_00465f70(B(ecx));
}
ABI_ENTRY_00465f70(fn_00465f70_Entry, fn_00465f70_Abi)
HOOK(0x00465f70, "fn_00465f70", fn_00465f70_Entry, nullptr);

// 0x00466010 (ecx = ui object; virtual, no direct callers): releases the handle +0x4f8, then tail-jumps to 0x00465160
// (ecx = object). Not executed in recorded runs.
uint32_t fn_00466010(uint8_t* obj)
{
    ReleaseHandle(&Field<uint32_t>(obj, 0x4f8));
    return orig::fn_00465160(u(obj));
}

static uint32_t fn_00466010_Abi(uint32_t ecx)
{
    return fn_00466010(B(ecx));
}
ABI_ENTRY_00466010(fn_00466010_Entry, fn_00466010_Abi)
HOOK(0x00466010, "fn_00466010", fn_00466010_Entry, nullptr);

// 0x00466060 (ecx = ui_text; virtual, no direct callers; the tracker's 0x00466077 is the interior of this function,
// entered only by falling through 0x00466075): a message box text. When g+0x15dc is clear: counts the timer g+0x15d8
// down by the frame time 0x00604a90 (when non-zero and g+0x2684 is clear) and at <= 0 zeroes it and calls 0x00403410
// (ecx = g; stack: g+0xdd8, g+0xdd4); then if g+0x15dc is still clear the text is cleared and hidden. Otherwise, with
// a message g+0x1de0 and the object active: when +0x4c0 differs from it, either takes it (g+0x15de bit 1 clear) or
// formats "%s<br><br>%s" (0x005a0cb0) with the message and swprintf(Text_Lookup("ui_stats_ok"), 0x00415740(ecx =
// 0x1b, edx = g+0xa80)), clears g+0x15de bit 1 and stores +0x4c0 back into g+0x1de0. Sets +0x474 bit 0x8000 from
// g+0x15de bit 4, calls 0x00464400 (eax = g+0xa70; stack: +0x4c0, +0x4c8, +0x4c8, g+0x15de bit 4) and shows the
// object. Returns 0x00464400's result (other paths: 0 / g+0x1de0 as noted).
uint32_t fn_00466060(uint8_t* text)
{
    wchar_t buffer[0x100];  // the 0x200-byte frame
    uint8_t* g = G();
    if (Field<uint8_t>(g, 0x15dc) == 0)
    {
        if (Field<float>(g, 0x15d8) != K(0x0056c05c) && Field<uint32_t>(g, 0x2684) == 0)
        {
            const float t = Field<float>(g, 0x15d8) - GameGlobal<float>(0x00604a90);
            Field<float>(g, 0x15d8) = t;
            if (t <= K(0x0056c05c))
            {
                Field<uint32_t>(g, 0x15d8) = 0;
                orig::fn_00403410(u(g), u(g + 0xdd8), Field<uint32_t>(g, 0xdd4));
                g = G();
            }
        }
        if (Field<uint8_t>(g, 0x15dc) == 0)
        {
            Field<uint32_t>(text, 0x4c0) = 0;
            Field<uint32_t>(text, 0x474) &= 0xfffffffbu;
            return 0;
        }
    }
    const uint32_t message = Field<uint32_t>(g, 0x1de0);
    if (message == 0)
    {
        Field<uint32_t>(text, 0x4c0) = 0;
        Field<uint32_t>(text, 0x474) &= 0xfffffffbu;
        return 0;
    }
    if (!UiActive(text))
        return message;
    if (Field<uint32_t>(text, 0x4c0) != message)
    {
        if ((Field<uint8_t>(g, 0x15de) & 1) != 0)
        {
            const uint32_t value = orig::fn_00415740(0x1b, Field<uint32_t>(g, 0xa80));
            const uint32_t format = orig::fn_00495f00(0x005a0ccc, Field<uint32_t>(G(), 0xac0), 0);
            GameFunction<int(__cdecl*)(wchar_t*, uint32_t, uint32_t)>(0x0053862e)(buffer, format, value);
            UiText_Format(text, reinterpret_cast<const wchar_t*>(0x005a0cb0), Field<uint32_t>(G(), 0x1de0), buffer);
            g = G();
            Field<uint8_t>(g, 0x15de) &= 0xfe;
            Field<uint32_t>(g, 0x1de0) = Field<uint32_t>(text, 0x4c0);
        }
        else
        {
            Field<uint32_t>(text, 0x4c0) = message;
        }
    }
    if ((Field<uint8_t>(g, 0x15de) & 4) != 0)
        Field<uint32_t>(text, 0x474) |= 0x8000;
    else
        Field<uint32_t>(text, 0x474) &= 0xffff7fffu;
    const uint32_t big = static_cast<uint32_t>(Field<uint8_t>(g, 0x15de) >> 2) & 1u;
    const uint32_t r = orig::fn_00464400(Field<uint32_t>(g, 0xa70), Field<uint32_t>(text, 0x4c0),
                                         Field<uint32_t>(text, 0x4c8), Field<uint32_t>(text, 0x4c8), big);
    Field<uint32_t>(text, 0x474) |= 4;
    return r;
}

static uint32_t fn_00466060_Abi(uint32_t ecx)
{
    return fn_00466060(B(ecx));
}
ABI_ENTRY_00466060(fn_00466060_Entry, fn_00466060_Abi)
HOOK(0x00466060, "fn_00466060", fn_00466060_Entry, nullptr);

// 0x00466210 (ecx = ui_text; virtual, no direct callers): when active: hides it, then when g+0x26cc is clear, g+0xac8
// set, g+0xdd3 clear and (g+0x748 is null or 0x0047a950(eax = g+0x748) returns true): shows it, calls 0x00465280
// (ecx = 0x14; stack: object, 0x23) and fits the text to 576 x unlimited (UiText_FitToBox). Returns g or the
// 0x0047a950 result as the original's EAX where cheap (0 after the fit).
uint32_t fn_00466210(uint8_t* text)
{
    if (!UiActive(text))
        return 0;
    Field<uint32_t>(text, 0x474) &= 0xfffffffbu;
    uint8_t* g = G();
    if (Field<uint32_t>(g, 0x26cc) != 0 || Field<uint32_t>(g, 0xac8) == 0 || Field<uint8_t>(g, 0xdd3) != 0)
        return u(g);
    const uint32_t player = Field<uint32_t>(g, 0x748);
    if (player != 0)
    {
        const uint32_t r = orig::fn_0047a950(player);
        if (static_cast<uint8_t>(r) == 0)
            return r;
    }
    Field<uint32_t>(text, 0x474) |= 4;
    orig::fn_00465280(0x14, u(text), 0x23);
    return UiText_FitToBox(text, bf(0x44100000u), 0.0f);
}

static uint32_t fn_00466210_Abi(uint32_t ecx)
{
    return fn_00466210(B(ecx));
}
ABI_ENTRY_00466210(fn_00466210_Entry, fn_00466210_Abi)
HOOK(0x00466210, "fn_00466210", fn_00466210_Entry, nullptr);

namespace
{
// 0x004e7880 (ecx, edx = vectors; returns st0): the generated orig wrapper only returns EAX and would leave st0 on the
// x87 stack, so it is called here and st0 stored as a float (the callers use it at once). guess: length / dot.
float Fn004e7880(uint32_t ecxValue, uint32_t edxValue)
{
    static const uint32_t target = 0x004e7880;
    float result;
    __asm {
        push ebx
        push esi
        push edi
        push ebp
        mov ecx, ecxValue
        mov edx, edxValue
        call dword ptr [target]
        pop ebp
        pop edi
        pop esi
        pop ebx
        fstp dword ptr [result]
    }
    return result;
}
// strcpy(buffer, Object_GetName(object)) + strcat of a suffix, as inlined in 0x00466580, 0x00466650 and 0x004666a0.
inline void NameWithSuffix(char* buffer, uint32_t object, const char* suffix)
{
    const char* name = reinterpret_cast<const char*>(orig::fn_00453270(object));
    char* d = buffer;
    char c;
    do
    {
        c = *name++;
        *d++ = c;
    } while (c != 0);
    d = buffer;
    while (*d != 0)
        ++d;
    do
    {
        c = *suffix++;
        *d++ = c;
    } while (c != 0);
}
}  // namespace

// 0x004662a0 (ecx = ui_text; virtual, no direct callers): when active and the player g+0x748 is set: hides it; then
// when g+0xac8 is clear, g+0xdd3 clear and 0x0047a950(player) true: gets the player position (virtual +0x54) and walks
// the list g+0xa84 +0xec4 (0x004e7340 / 0x004e73c0). Each item gets 0x00498740(eax = item, cl = 0) (clears +0x530 bit
// 1); for items whose virtual +0x150 returns false, the distance 0x004e7880(player - item) is compared with
// (unsigned)0x004983e0(item): the first item in range gets 0x00498740(cl = 1), the object is shown, 0x00465280(ecx =
// 0x14; object, 0x23) and UiText_FitToBox(576, 0), and the walk stops. Releases the iterator lock (+0x18). guess: the
// "press to use" prompt near an interactive object. Frame: 0x30 bytes (iterator at +4, delta +0xc, positions +0x18 /
// +0x24). Not executed in recorded runs.
uint32_t fn_004662a0(uint8_t* text)
{
    if (!UiActive(text))
        return 0;
    uint8_t* g = G();
    if (Field<uint32_t>(g, 0x748) == 0)
        return 0;
    Field<uint32_t>(text, 0x474) &= 0xfffffffbu;
    if (Field<uint32_t>(g, 0xac8) != 0 || Field<uint8_t>(g, 0xdd3) != 0)
        return 0;
    if (Field<uint32_t>(g, 0x748) != 0)
    {
        const uint32_t r = orig::fn_0047a950(Field<uint32_t>(g, 0x748));
        if (static_cast<uint8_t>(r) == 0)
            return r;
        g = G();
    }
    uint32_t iterator[2];
    float delta[3];
    float playerPos[3];
    float itemPos[3];
    VCall<uint32_t>(Field<void*>(g, 0x748), 0x54 / 4, playerPos);
    iterator[0] = 0;
    iterator[1] = 0;
    uint32_t item = orig::fn_004e7340(Field<uint32_t>(Field<void*>(G(), 0xa84), 0xec4), u(iterator));
    while (item != 0)
    {
        orig::fn_00498740(item, 0);
        if (static_cast<uint8_t>(VCall<uint32_t>(P(item), 0x150 / 4)) == 0)
        {
            VCall<uint32_t>(P(item), 0x54 / 4, itemPos);
            delta[0] = playerPos[0] - itemPos[0];
            delta[1] = playerPos[1] - itemPos[1];
            delta[2] = playerPos[2] - itemPos[2];
            const float distance = Fn004e7880(u(delta), u(delta));
            const uint32_t range = orig::fn_004983e0(item);
            if (!(static_cast<float>(range) < distance))  // fcompp / test ah,1: in range unless below or unordered
            {
                orig::fn_00498740(item, 1);
                Field<uint32_t>(text, 0x474) |= 4;
                orig::fn_00465280(0x14, u(text), 0x23);
                UiText_FitToBox(text, bf(0x44100000u), 0.0f);
                break;
            }
        }
        item = orig::fn_004e73c0(u(iterator));
    }
    if (iterator[1] != 0)
        --Field<int32_t>(P(iterator[1]), 0x18);
    return iterator[1];
}

static uint32_t fn_004662a0_Abi(uint32_t ecx)
{
    return fn_004662a0(B(ecx));
}
ABI_ENTRY_004662a0(fn_004662a0_Entry, fn_004662a0_Abi)
HOOK(0x004662a0, "fn_004662a0", fn_004662a0_Entry, nullptr);

// 0x00466430 (ecx = ui object; stack: 4 args; ret 0x10; virtual, no direct callers): 0x004648b0 (ecx = object, the
// same arguments; the base method), then 0x004643f0 (eax = g+0xa70, ecx = object) and +0x4a0 = 0. Not executed in
// recorded runs.
uint32_t fn_00466430(uint8_t* obj, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    orig::fn_004648b0(u(obj), a0, a1, a2, a3);
    orig::fn_004643f0(Field<uint32_t>(G(), 0xa70), u(obj));
    Field<uint32_t>(obj, 0x4a0) = 0;
    return 0;
}

static uint32_t fn_00466430_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_00466430(B(ecx), a0, a1, a2, a3);
}
ABI_ENTRY_00466430(fn_00466430_Entry, fn_00466430_Abi)
HOOK(0x00466430, "fn_00466430", fn_00466430_Entry, nullptr);

// 0x00466470 (al = flag, ecx = ui object; stack: a0, a1, a2; ret 0xc): shows the object, calls renderer virtual +0x1c0
// (&a1, &a2, a0, a1, a2, 0, 0, flag ? 8 : 0) which writes back into the a1/a2 stack slots, then +0x4a0 = min(a2 /
// 480, 1.0) is set as the +4 of anim 4 (0x00408800) and Anim_Set(eax = 1, esi = object; 0, 0x210, 0, 0, 0, 0). guess:
// a1/a2 = measured text width/height (a0 = text), the object a box sized to the text. Returns Anim_Set's result.
uint32_t fn_00466470(uint32_t eax, uint8_t* obj, uint32_t a0, uint32_t a1, uint32_t a2)
{
    Field<uint32_t>(obj, 0x474) |= 4;
    const uint32_t flag = static_cast<uint8_t>(eax) != 0 ? 8u : 0u;
    VCall<uint32_t>(Renderer(), 0x1c0 / 4, &a1, &a2, a0, a1, a2, 0u, 0u, flag);
    const float h = bf(a2) * K(0x005bcbf4);
    Field<float>(obj, 0x4a0) = h;
    if (h > K(0x005bcb2c))
        Field<uint32_t>(obj, 0x4a0) = 0x3f800000u;
    const uint32_t anim = orig::fn_00408800(4, u(obj));
    Field<uint32_t>(P(anim), 4) = Field<uint32_t>(obj, 0x4a0);
    return orig::fn_00408d90(1, u(obj), 0, 0x210, 0, 0, 0, 0);
}

static uint32_t fn_00466470_Abi(uint32_t eax, uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return fn_00466470(eax, B(ecx), a0, a1, a2);
}
ABI_ENTRY_00466470(fn_00466470_Entry, fn_00466470_Abi)
HOOK(0x00466470, "fn_00466470", fn_00466470_Entry, nullptr);

// 0x00466520 (ecx = ui object; virtual, no direct callers): hides the object (+0x474 &= ~4). Not executed in recorded
// runs.
void fn_00466520(uint8_t* obj)
{
    Field<uint32_t>(obj, 0x474) &= 0xfffffffbu;
}

static void fn_00466520_Abi(uint32_t ecx)
{
    fn_00466520(B(ecx));
}
ABI_ENTRY_00466520(fn_00466520_Entry, fn_00466520_Abi)
HOOK(0x00466520, "fn_00466520", fn_00466520_Entry, nullptr);

// 0x00466530 (ecx = ui object; stack: 4 args; ret 0x10; virtual, no direct callers): 0x004648b0 (base method, same
// arguments), 0x004643d0 (ecx = g+0xa70; stack: object), then clears +0x474 bit 8 and sets bit 4 (two stores, as the
// original). Returns the flags. Not executed in recorded runs.
uint32_t fn_00466530(uint8_t* obj, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    orig::fn_004648b0(u(obj), a0, a1, a2, a3);
    orig::fn_004643d0(Field<uint32_t>(G(), 0xa70), u(obj));
    const uint32_t flags = Field<uint32_t>(obj, 0x474) & 0xfffffff7u;
    Field<uint32_t>(obj, 0x474) = flags;
    Field<uint32_t>(obj, 0x474) = flags | 4;
    return flags | 4;
}

static uint32_t fn_00466530_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_00466530(B(ecx), a0, a1, a2, a3);
}
ABI_ENTRY_00466530(fn_00466530_Entry, fn_00466530_Abi)
HOOK(0x00466530, "fn_00466530", fn_00466530_Entry, nullptr);

// 0x00466580 (ecx = ui object; stack: 4 args; ret 0x10; virtual, no direct callers): 0x004648b0 (base method), then
// 0x00454750 (esi = list g+0xa84 +0xe74, edi = object), +0x474 |= 0x20; when the object's anim count (+0x414 -> +0x10
// -> +0x24) is above 1: anim 4 +4 = 1.0 and Anim_Set(eax = 2, esi = object; "<name>_out" (suffix 0x005a0ca8), 0x210, 0,
// 0, 0, 0). Finally hides the object. Returns the flags. Not executed in recorded runs.
uint32_t fn_00466580(uint8_t* obj, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    char name[0x28];
    orig::fn_004648b0(u(obj), a0, a1, a2, a3);
    orig::fn_00454750(Field<uint32_t>(Field<void*>(G(), 0xa84), 0xe74), u(obj));
    Field<uint32_t>(obj, 0x474) |= 0x20;
    if (Field<uint32_t>(Field<void*>(Field<void*>(obj, 0x414), 0x10), 0x24) > 1)
    {
        const uint32_t anim = orig::fn_00408800(4, u(obj));
        Field<uint32_t>(P(anim), 4) = 0x3f800000u;
        NameWithSuffix(name, u(obj), reinterpret_cast<const char*>(0x005a0ca8));
        orig::fn_00408d90(2, u(obj), u(name), 0x210, 0, 0, 0, 0);
    }
    const uint32_t flags = Field<uint32_t>(obj, 0x474) & 0xfffffffbu;
    Field<uint32_t>(obj, 0x474) = flags;
    return flags;
}

static uint32_t fn_00466580_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_00466580(B(ecx), a0, a1, a2, a3);
}
ABI_ENTRY_00466580(fn_00466580_Entry, fn_00466580_Abi)
HOOK(0x00466580, "fn_00466580", fn_00466580_Entry, nullptr);

// 0x00466650 (eax = object): plays the animation "<name>_in" (suffix 0x005a0ca4): Anim_Set(eax = -1, esi = object;
// name, 0x80, 0, 0, 0, 0). Caller 0x00406840. Returns Anim_Set's result.
uint32_t Object_PlayAnimIn(uint32_t object)
{
    char name[0x28];
    NameWithSuffix(name, object, reinterpret_cast<const char*>(0x005a0ca4));
    return orig::fn_00408d90(0xffffffffu, object, u(name), 0x80, 0, 0, 0, 0);
}

static uint32_t Object_PlayAnimIn_Abi(uint32_t eax)
{
    return Object_PlayAnimIn(eax);
}
ABI_ENTRY_00466650(Object_PlayAnimIn_Entry, Object_PlayAnimIn_Abi)
HOOK(0x00466650, "Object_PlayAnimIn", Object_PlayAnimIn_Entry, nullptr);

// 0x004666a0 (eax = object): as 0x00466650 with "<name>_out" (0x005a0ca8).
uint32_t Object_PlayAnimOut(uint32_t object)
{
    char name[0x28];
    NameWithSuffix(name, object, reinterpret_cast<const char*>(0x005a0ca8));
    return orig::fn_00408d90(0xffffffffu, object, u(name), 0x80, 0, 0, 0, 0);
}

static uint32_t Object_PlayAnimOut_Abi(uint32_t eax)
{
    return Object_PlayAnimOut(eax);
}
ABI_ENTRY_004666a0(Object_PlayAnimOut_Entry, Object_PlayAnimOut_Abi)
HOOK(0x004666a0, "Object_PlayAnimOut", Object_PlayAnimOut_Entry, nullptr);

// 0x00466700 (ecx = ui object; virtual, no direct callers): compass. When active: Camera_GetDirection (esi = out
// vector; stack: camera g+0xa68), anim 4 +4 = 1 - (pi - atan2(dir[1], dir[0])) * 1/(2 pi) (0x005bcb70, 0x005bced8;
// fpatan), Anim_Set(eax = 1, esi = object; 0, 0x210, 0, 0, 0, 0), shows the object. Not executed in recorded runs.
uint32_t fn_00466700(uint8_t* obj)
{
    if (!UiActive(obj))
        return 0;
    float dir[3];
    orig::fn_0040fa10(u(dir), Field<uint32_t>(G(), 0xa68));
    const double angle = std::atan2(static_cast<double>(dir[1]), static_cast<double>(dir[0]));
    const float a = static_cast<float>(static_cast<double>(K(0x005bcb70)) - angle);
    const float value = K(0x005bcb2c) - a * K(0x005bced8);
    const uint32_t anim = orig::fn_00408800(4, u(obj));
    Field<float>(P(anim), 4) = value;
    const uint32_t r = orig::fn_00408d90(1, u(obj), 0, 0x210, 0, 0, 0, 0);
    Field<uint32_t>(obj, 0x474) |= 4;
    return r;
}

static uint32_t fn_00466700_Abi(uint32_t ecx)
{
    return fn_00466700(B(ecx));
}
ABI_ENTRY_00466700(fn_00466700_Entry, fn_00466700_Abi)
HOOK(0x00466700, "fn_00466700", fn_00466700_Entry, nullptr);

namespace
{
// The distance loop inlined three times in 0x00466790: for each item of a list (0x004e7340 / 0x004e73c0 with the
// 8-byte iterator), optionally filtered, position by virtual +0x54, distance to `from` computed as
// sqrt((dz*dz + dy*dy) + dx*dx); keeps the item when best > distance.
template <class Filter>
uint32_t NearestInList(uint32_t list, uint32_t* iterator, const float* from, float& best, uint32_t found, Filter filter)
{
    uint32_t item = orig::fn_004e7340(list, u(iterator));
    while (item != 0)
    {
        if (filter(item))
        {
            float pos[3];
            VCall<uint32_t>(P(item), 0x54 / 4, pos);
            const float dx = pos[0] - from[0];
            const float dy = pos[1] - from[1];
            const float dz = pos[2] - from[2];
            const float distance = std::sqrt(dz * dz + dy * dy + dx * dx);
            if (best > distance)
            {
                best = distance;
                found = item;
            }
        }
        item = orig::fn_004e73c0(u(iterator));
    }
    return found;
}
}  // namespace

// 0x00466790 (ecx = ui object; virtual, no direct callers): target pointer arrow. When active: hides it, then finds
// the nearest object to the camera position (Camera_GetPosition, camera g+0xa68) in list g+0xa84 +0xefc when its
// count (virtual +0x34) is non-zero, else in list +0xf04 (when non-zero); then, when g+0x2658 is set, also in list
// +0xe70 among objects whose 0x00453290 (eax = item, ecx = out) value (+0x390) equals g+0x2658; the latter wins only when
// strictly nearer. Distances start at 0x60ad78ec (about 1e20). With a target: shows the object; the direction d =
// target (virtual +0x6c) - camera in x/y is normalised (when its length, including z, is non-zero), the arrow angle is
// atan2(d.y, d.x) - atan2(cam.y, cam.x) (Camera_GetDirection), and a 0x44-byte matrix {cos, -sin, 0, x; sin, cos, 0,
// y; 0, 0, 1, z; 0, 0, 0, 1; flags 0x11} with the object's own position (virtual +0x6c) is set with 0x00457990 (al = 0;
// stack: object, matrix). Anim 4 +4 = 1 - min(distance * 0.002, 1), Anim_Set(eax = 1, esi = object; 0, 0x210, 0, 0, 0,
// 0). Frame: 8-byte aligned, 0xa8 bytes. The original's EAX is returned as 0.
uint32_t fn_00466790(uint8_t* obj)
{
    if (!UiActive(obj))
        return 0;
    Field<uint32_t>(obj, 0x474) &= 0xfffffffbu;
    float best = bf(0x60ad78ecu);
    float bestFirst = bf(0x60ad78ecu);
    uint32_t target = 0;
    uint32_t iterator[2];
    float camera[3];
    void* lists = Field<void*>(G(), 0xa84);
    if (VCall<uint32_t>(Field<void*>(lists, 0xefc), 0x34 / 4) != 0)
    {
        iterator[0] = 0;
        iterator[1] = 0;
        orig::fn_0040f9e0(u(camera), Field<uint32_t>(G(), 0xa68));
        target = NearestInList(Field<uint32_t>(Field<void*>(G(), 0xa84), 0xefc), iterator, camera, best, target,
                               [](uint32_t) { return true; });
        bestFirst = best;
        if (iterator[1] != 0)
            --Field<int32_t>(P(iterator[1]), 0x18);
    }
    else if (VCall<uint32_t>(Field<void*>(Field<void*>(G(), 0xa84), 0xf04), 0x34 / 4) != 0)
    {
        iterator[0] = 0;
        iterator[1] = 0;
        orig::fn_0040f9e0(u(camera), Field<uint32_t>(G(), 0xa68));
        target = NearestInList(Field<uint32_t>(Field<void*>(G(), 0xa84), 0xf04), iterator, camera, best, target,
                               [](uint32_t) { return true; });
        bestFirst = best;
        if (iterator[1] != 0)
            --Field<int32_t>(P(iterator[1]), 0x18);
    }
    if (Field<uint32_t>(G(), 0x2658) != 0)
    {
        iterator[0] = 0;
        iterator[1] = 0;
        orig::fn_0040f9e0(u(camera), Field<uint32_t>(G(), 0xa68));
        const uint32_t other = NearestInList(Field<uint32_t>(Field<void*>(G(), 0xa84), 0xe70), iterator, camera, best,
                                             0, [](uint32_t item) {
                                                 uint32_t value;
                                                 if (static_cast<uint8_t>(orig::fn_00453290(item, u(&value))) == 0)
                                                     return false;
                                                 return value == Field<uint32_t>(G(), 0x2658);
                                             });
        if (best < bestFirst)
            target = other;
        if (iterator[1] != 0)
            --Field<int32_t>(P(iterator[1]), 0x18);
    }
    if (target == 0)
        return 0;
    Field<uint32_t>(obj, 0x474) |= 4;
    float targetPos[3];
    VCall<uint32_t>(P(target), 0x6c / 4, targetPos);
    orig::fn_0040f9e0(u(camera), Field<uint32_t>(G(), 0xa68));
    float d0 = targetPos[0] - camera[0];
    float d1 = targetPos[1] - camera[1];
    const float dz = targetPos[2] - camera[2];
    const float distance = std::sqrt(dz * dz + d1 * d1 + d0 * d0);
    if (distance != K(0x0056c05c))
    {
        const float inv = K(0x005bcb2c) / distance;
        d0 = d0 * inv;
        d1 = inv * d1;
    }
    float direction[3];
    orig::fn_0040fa10(u(direction), Field<uint32_t>(G(), 0xa68));
    const double angleTarget = std::atan2(static_cast<double>(d1), static_cast<double>(d0));
    uint32_t m[17];
    std::memset(m, 0, sizeof(m));
    m[10] = 0x3f800000u;
    m[15] = 0x3f800000u;
    const double angleCamera = std::atan2(static_cast<double>(direction[1]), static_cast<double>(direction[0]));
    const float angle = static_cast<float>(angleTarget - angleCamera);
    const float s = static_cast<float>(std::sin(static_cast<double>(angle)));
    const float c = static_cast<float>(std::cos(static_cast<double>(angle)));
    m[0] = fb(c);
    m[1] = fb(-s);
    m[4] = fb(s);
    m[5] = fb(c);
    float own[3];
    VCall<uint32_t>(obj, 0x6c / 4, own);
    m[3] = fb(own[0]);
    m[7] = fb(own[1]);
    m[11] = fb(own[2]);
    m[16] = 0x11;
    orig::fn_00457990(0, u(obj), u(m));
    float v = distance * K(0x005bcc90);
    if (v > K(0x005bcb2c))
        v = K(0x005bcb2c);
    const uint32_t anim = orig::fn_00408800(4, u(obj));
    Field<float>(P(anim), 4) = K(0x005bcb2c) - v;
    orig::fn_00408d90(1, u(obj), 0, 0x210, 0, 0, 0, 0);
    return 0;
}

static uint32_t fn_00466790_Abi(uint32_t ecx)
{
    return fn_00466790(B(ecx));
}
ABI_ENTRY_00466790(fn_00466790_Entry, fn_00466790_Abi)
HOOK(0x00466790, "fn_00466790", fn_00466790_Entry, nullptr);

namespace
{
// 0x005448ee: the CRT's _stricmp (cdecl; the ABI table's ecx input is a CRT-internal false positive: callers leave
// arbitrary values in ECX).
inline int Stricmp(uint32_t a, uint32_t b)
{
    return GameFunction<int(__cdecl*)(uint32_t, uint32_t)>(0x005448ee)(a, b);
}
}  // namespace

// 0x00466c20 (ecx = ui object; stack: 4 args; ret 0x10; virtual, no direct callers): health bar init. 0x004648b0 (base
// method, same arguments), 0x00453540 (eax = object, cl = 1: sets +0x424 bit 0x400), +0x4a0 = 1.0, +0x4a4 = 0, then
// by Object_GetName (stricmp): "ui_player_health" -> +0x4a8 = 0; "ui_health_enemy" -> 1; "ui_health_q" -> 2;
// anything else -> 3; +0x4b0 = 0 in every case. Not executed in recorded runs.
uint32_t fn_00466c20(uint8_t* obj, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    orig::fn_004648b0(u(obj), a0, a1, a2, a3);
    orig::fn_00453540(u(obj), 1);
    Field<uint32_t>(obj, 0x4a0) = 0x3f800000u;
    Field<uint32_t>(obj, 0x4a4) = 0;
    if (Stricmp(orig::fn_00453270(u(obj)), 0x005b01acu) == 0)
    {
        Field<uint32_t>(obj, 0x4a8) = 0;
        Field<uint32_t>(obj, 0x4b0) = 0;
        return 0;
    }
    if (Stricmp(orig::fn_00453270(u(obj)), 0x005b019cu) == 0)
    {
        Field<uint32_t>(obj, 0x4b0) = 0;
        Field<uint32_t>(obj, 0x4a8) = 1;
        return 0;
    }
    const uint32_t kind = (Stricmp(orig::fn_00453270(u(obj)), 0x005b017cu) != 0 ? 1u : 0u) + 2u;
    Field<uint32_t>(obj, 0x4b0) = 0;
    Field<uint32_t>(obj, 0x4a8) = kind;
    return kind;
}

static uint32_t fn_00466c20_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_00466c20(B(ecx), a0, a1, a2, a3);
}
ABI_ENTRY_00466c20(fn_00466c20_Entry, fn_00466c20_Abi)
HOOK(0x00466c20, "fn_00466c20", fn_00466c20_Entry, nullptr);

// 0x00467010 (ecx = ui object; stack: 4 args; ret 0x10; virtual, no direct callers): hit indicator init. 0x004648b0
// (base method), then +0x4a0 = direction bit by name: "ui_hit_top" 1, "ui_hit_bottom" 2, "ui_hit_left" 4,
// "ui_hit_right" 8 (unchanged otherwise); +0x4a4 (timer) = 0. Not executed in recorded runs.
uint32_t fn_00467010(uint8_t* obj, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    orig::fn_004648b0(u(obj), a0, a1, a2, a3);
    static const uint32_t names[4] = {0x005b0170u, 0x005b0160u, 0x005b0154u, 0x005b0144u};
    for (uint32_t i = 0; i < 4; ++i)
    {
        if (Stricmp(orig::fn_00453270(u(obj)), names[i]) == 0)
        {
            Field<uint32_t>(obj, 0x4a0) = 1u << i;
            break;
        }
    }
    Field<uint32_t>(obj, 0x4a4) = 0;
    return 0;
}

static uint32_t fn_00467010_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_00467010(B(ecx), a0, a1, a2, a3);
}
ABI_ENTRY_00467010(fn_00467010_Entry, fn_00467010_Abi)
HOOK(0x00467010, "fn_00467010", fn_00467010_Entry, nullptr);

// 0x004670f0 (ecx = ui object; virtual, no direct callers): hit indicator update. When active: if the pending hit
// mask g+0x26d0 has this object's bit (+0x4a0), the bit is cleared, the object shown and the timer +0x4a4 = g+0xcb8;
// then while the timer is > 0 it counts down by the frame time 0x00604a90, else the object is hidden. Not executed in
// recorded runs.
uint32_t fn_004670f0(uint8_t* obj)
{
    if (!UiActive(obj))
        return 0;
    uint8_t* g = G();
    const uint32_t bit = Field<uint32_t>(obj, 0x4a0);
    if ((Field<uint32_t>(g, 0x26d0) & bit) != 0)
    {
        Field<uint32_t>(g, 0x26d0) = ~bit & Field<uint32_t>(g, 0x26d0);
        Field<uint32_t>(obj, 0x474) |= 4;
        Field<uint32_t>(obj, 0x4a4) = Field<uint32_t>(g, 0xcb8);
    }
    if (Field<float>(obj, 0x4a4) <= K(0x0056c05c))
    {
        Field<uint32_t>(obj, 0x474) &= 0xfffffffbu;
        return 0;
    }
    Field<float>(obj, 0x4a4) = Field<float>(obj, 0x4a4) - GameGlobal<float>(0x00604a90);
    return 0;
}

static uint32_t fn_004670f0_Abi(uint32_t ecx)
{
    return fn_004670f0(B(ecx));
}
ABI_ENTRY_004670f0(fn_004670f0_Entry, fn_004670f0_Abi)
HOOK(0x004670f0, "fn_004670f0", fn_004670f0_Entry, nullptr);

// 0x00467170 (ecx = ui object; stack: 4 args; ret 0x10; virtual, no direct callers): 0x004651a0 (ecx = object, same
// arguments), then 0x00454750 (esi = list g+0xa84 +0xe74, edi = object). Not executed in recorded runs.
uint32_t fn_00467170(uint8_t* obj, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    orig::fn_004651a0(u(obj), a0, a1, a2, a3);
    return orig::fn_00454750(Field<uint32_t>(Field<void*>(G(), 0xa84), 0xe74), u(obj));
}

static uint32_t fn_00467170_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_00467170(B(ecx), a0, a1, a2, a3);
}
ABI_ENTRY_00467170(fn_00467170_Entry, fn_00467170_Abi)
HOOK(0x00467170, "fn_00467170", fn_00467170_Entry, nullptr);

// 0x004671b0 (ecx = ui_text; virtual, no direct callers): when active: hides it; when g+0xcbc is set and g+0x578 clear:
// sets g+0x578, calls 0x00401f40 (stack: g, 0), 0x00465280 (ecx = 8; stack: object, 0x23) and 0x00403410 (ecx = g;
// stack: +0x4c0, 1). Returns 0x00403410's result (g / 0 on the other paths).
uint32_t fn_004671b0(uint8_t* text)
{
    if (!UiActive(text))
        return 0;
    Field<uint32_t>(text, 0x474) &= 0xfffffffbu;
    uint8_t* g = G();
    if (Field<uint8_t>(g, 0xcbc) == 0 || Field<uint8_t>(g, 0x578) != 0)
        return u(g);
    Field<uint8_t>(g, 0x578) = 1;
    orig::fn_00401f40(u(g), 0);
    orig::fn_00465280(8, u(text), 0x23);
    return orig::fn_00403410(u(G()), Field<uint32_t>(text, 0x4c0), 1);
}

static uint32_t fn_004671b0_Abi(uint32_t ecx)
{
    return fn_004671b0(B(ecx));
}
ABI_ENTRY_004671b0(fn_004671b0_Entry, fn_004671b0_Abi)
HOOK(0x004671b0, "fn_004671b0", fn_004671b0_Entry, nullptr);

// 0x00467220 (ecx = ui object; virtual, no direct callers): when active: shown when g+0xd90 is set and the list g+0xa84
// +0xeb4 is non-empty (virtual +0x34 count), else hidden. Returns the count (0 / g on the other paths).
uint32_t fn_00467220(uint8_t* obj)
{
    if (!UiActive(obj))
        return 0;
    if (Field<uint8_t>(G(), 0xd90) != 0)
    {
        const uint32_t count = VCall<uint32_t>(Field<void*>(Field<void*>(G(), 0xa84), 0xeb4), 0x34 / 4);
        if (count != 0)
        {
            Field<uint32_t>(obj, 0x474) |= 4;
            return count;
        }
    }
    Field<uint32_t>(obj, 0x474) &= 0xfffffffbu;
    return 0;
}

static uint32_t fn_00467220_Abi(uint32_t ecx)
{
    return fn_00467220(B(ecx));
}
ABI_ENTRY_00467220(fn_00467220_Entry, fn_00467220_Abi)
HOOK(0x00467220, "fn_00467220", fn_00467220_Entry, nullptr);

// 0x00467270 (ecx = ui object; stack: 4 args; ret 0x10; virtual, no direct callers): 0x004648b0 (base method), +0x474
// |= 0x20, 0x00454750 (esi = list g+0xa84 +0xe74, edi = object), 0x00453540 (eax = object, cl = 1), +0x4a0 = 0. Not
// executed in recorded runs.
uint32_t fn_00467270(uint8_t* obj, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    orig::fn_004648b0(u(obj), a0, a1, a2, a3);
    Field<uint32_t>(obj, 0x474) |= 0x20;
    orig::fn_00454750(Field<uint32_t>(Field<void*>(G(), 0xa84), 0xe74), u(obj));
    orig::fn_00453540(u(obj), 1);
    Field<uint32_t>(obj, 0x4a0) = 0;
    return 0;
}

static uint32_t fn_00467270_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_00467270(B(ecx), a0, a1, a2, a3);
}
ABI_ENTRY_00467270(fn_00467270_Entry, fn_00467270_Abi)
HOOK(0x00467270, "fn_00467270", fn_00467270_Entry, nullptr);

// 0x00467420 (ecx = ui object; stack: 4 args; ret 0x10; virtual, no direct callers): 0x00465230 (ecx = object, same
// arguments), then +0x4f8 = 0. Not executed in recorded runs.
uint32_t fn_00467420(uint8_t* obj, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    const uint32_t r = orig::fn_00465230(u(obj), a0, a1, a2, a3);
    Field<uint32_t>(obj, 0x4f8) = 0;
    return r;
}

static uint32_t fn_00467420_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_00467420(B(ecx), a0, a1, a2, a3);
}
ABI_ENTRY_00467420(fn_00467420_Entry, fn_00467420_Abi)
HOOK(0x00467420, "fn_00467420", fn_00467420_Entry, nullptr);

namespace
{
// 0x00463890 (ebx = name; stack: page list g+0xa70): finds a ui object by name (recovered elsewhere; called through
// the original). Returns it or 0.
inline uint8_t* FindUi(uint32_t name)
{
    return B(orig::fn_00463890(name, Field<uint32_t>(G(), 0xa70)));
}
}  // namespace

// 0x00467450 (ecx = ui_text; virtual, no direct callers): ticker-bomb prompt, re-evaluated every 6th call (counter
// +0x4f8). Clears g+0xdd3. With a player g+0x748 for which 0x0047a950 is false: hides itself and the texts
// "ui_tickerbomb_patience" (0x005b1640) and "ui_tickerbomb_none" (0x005b0294). Otherwise, unless g+0xdcf is set, walks
// the list g+0xa84 +0xeb4 for the first bomb that is not armed (PlayerTickerBomb_IsArmed) and within range
// (unsigned 0x0044b8b0(bomb) >= distance 0x004e7880(player +0xc0c - bomb position (virtual +0x54))). For that bomb:
// without g+0xd90 shows "ui_tickerbomb_none" and sets g+0xdd3; with g+0xd90 and PlayerTickerBomb_IsEnabled hides both
// texts and shows itself (g+0xdd3 = 1, 0x00465280 (ecx = 0x14; object, 0x23), UiText_FitToBox(576, 0)); else
// PlayerTickerBomb_SendNonEnabled. The iterator is then released with its first word's virtual +0xc (stack: second
// word) and the second word's +0x18 decremented. In every other case it hides itself. Frame: 0x28 bytes (flag byte
// +3, iterator +8, delta +0x10, position +0x1c). Not executed in recorded runs.
uint32_t fn_00467450(uint8_t* text)
{
    if (!UiActive(text))
        return 0;
    const uint32_t counter = Field<uint32_t>(text, 0x4f8) + 1;
    Field<uint32_t>(text, 0x4f8) = counter;
    if (counter < 6)
        return counter;
    Field<uint32_t>(text, 0x4f8) = 0;
    uint8_t* g = G();
    Field<uint8_t>(g, 0xdd3) = 0;
    uint8_t show = 0;
    const uint32_t player = Field<uint32_t>(g, 0x748);
    if (player != 0)
    {
        if (static_cast<uint8_t>(orig::fn_0047a950(player)) == 0)
        {
            Field<uint32_t>(text, 0x474) &= 0xfffffffbu;
            if (uint8_t* t = FindUi(0x005b1640u))
                Field<uint32_t>(t, 0x474) &= 0xfffffffbu;
            if (uint8_t* t = FindUi(0x005b0294u))
                Field<uint32_t>(t, 0x474) &= 0xfffffffbu;
            return 0;
        }
        g = G();
    }
    if (Field<uint8_t>(g, 0xdcf) == 0)
    {
        uint32_t iterator[2] = {0, 0};
        uint32_t item = orig::fn_004e7340(Field<uint32_t>(Field<void*>(g, 0xa84), 0xeb4), u(iterator));
        bool found = false;
        while (item != 0)
        {
            if (static_cast<uint8_t>(orig::fn_0044e390(item)) == 0)
            {
                float pos[3];
                float delta[3];
                VCall<uint32_t>(P(item), 0x54 / 4, pos);
                const float* playerPos = &Field<float>(Field<void*>(G(), 0x748), 0xc0c);
                delta[0] = playerPos[0] - pos[0];
                delta[1] = playerPos[1] - pos[1];
                delta[2] = playerPos[2] - pos[2];
                const float distance = Fn004e7880(u(delta), u(delta));
                const uint32_t range = orig::fn_0044b8b0(item);
                if (!(static_cast<float>(range) < distance))
                {
                    found = true;
                    break;
                }
            }
            item = orig::fn_004e73c0(u(iterator));
        }
        if (!found)
        {
            if (iterator[1] != 0)
                --Field<int32_t>(P(iterator[1]), 0x18);
        }
        else
        {
            g = G();
            if (Field<uint8_t>(g, 0xd90) == 0)
            {
                if (uint8_t* t = FindUi(0x005b0294u))
                    Field<uint32_t>(t, 0x474) |= 4;
                Field<uint8_t>(G(), 0xdd3) = 1;
            }
            else if (static_cast<uint8_t>(orig::fn_0044e380(item)) != 0)
            {
                if (uint8_t* t = FindUi(0x005b0294u))
                    Field<uint32_t>(t, 0x474) &= 0xfffffffbu;
                if (uint8_t* t = FindUi(0x005b1640u))
                    Field<uint32_t>(t, 0x474) &= 0xfffffffbu;
                show = 1;
            }
            else
            {
                orig::fn_0044e0e0(item);
            }
            VCall<uint32_t>(P(iterator[0]), 0xc / 4, iterator[1]);
            --Field<int32_t>(P(iterator[1]), 0x18);
            if (show != 0)
            {
                Field<uint32_t>(text, 0x474) |= 4;
                Field<uint8_t>(G(), 0xdd3) = 1;
                orig::fn_00465280(0x14, u(text), 0x23);
                return UiText_FitToBox(text, bf(0x44100000u), 0.0f);
            }
        }
    }
    const uint32_t flags = Field<uint32_t>(text, 0x474) & 0xfffffffbu;
    Field<uint32_t>(text, 0x474) = flags;
    return flags;
}

static uint32_t fn_00467450_Abi(uint32_t ecx)
{
    return fn_00467450(B(ecx));
}
ABI_ENTRY_00467450(fn_00467450_Entry, fn_00467450_Abi)
HOOK(0x00467450, "fn_00467450", fn_00467450_Entry, nullptr);

// 0x004676f0 (ecx = ui object; stack: 4 args; ret 0x10; virtual, no direct callers): 0x004648b0 (base method),
// 0x00454750 (esi = list g+0xa84 +0xe74, edi = object), then hides the object. Not executed in recorded runs.
uint32_t fn_004676f0(uint8_t* obj, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    orig::fn_004648b0(u(obj), a0, a1, a2, a3);
    orig::fn_00454750(Field<uint32_t>(Field<void*>(G(), 0xa84), 0xe74), u(obj));
    Field<uint32_t>(obj, 0x474) &= 0xfffffffbu;
    return 0;
}

static uint32_t fn_004676f0_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_004676f0(B(ecx), a0, a1, a2, a3);
}
ABI_ENTRY_004676f0(fn_004676f0_Entry, fn_004676f0_Abi)
HOOK(0x004676f0, "fn_004676f0", fn_004676f0_Entry, nullptr);

// 0x00467740 (ecx = ui object; virtual, no direct callers): when active (+0x470 bit 1 or +0x474 bit 2) and visible
// (+0x474 bit 2, or +0x470 bit 1 with +0x474 bit 4) and anim 4 (0x00408800) has +0x1c bit 2 set, hides the object.
// guess: hide when its animation has finished. Returns the anim pointer (EAX after 0x00408800) or 0. Not executed in
// recorded runs.
uint32_t fn_00467740(uint8_t* obj)
{
    const uint32_t bit = Field<uint32_t>(obj, 0x470) & 1;
    if (bit == 0 && (Field<uint8_t>(obj, 0x474) & 2) == 0)
        return 0;
    const uint32_t anim = orig::fn_00408800(4, u(obj));
    const uint32_t flags = Field<uint32_t>(obj, 0x474);
    if ((flags & 2) == 0 && (bit == 0 || (flags & 4) == 0))
        return anim;
    if ((Field<uint8_t>(P(anim), 0x1c) & 2) != 0)
        Field<uint32_t>(obj, 0x474) &= 0xfffffffbu;
    return anim;
}

static uint32_t fn_00467740_Abi(uint32_t ecx)
{
    return fn_00467740(B(ecx));
}
ABI_ENTRY_00467740(fn_00467740_Entry, fn_00467740_Abi)
HOOK(0x00467740, "fn_00467740", fn_00467740_Entry, nullptr);

// 0x00467790 (ecx = ui object; stack: 4 args; ret 0x10; virtual, no direct callers): 0x00465230 (same arguments), then
// the byte +0x4fc = 0. Not executed in recorded runs.
uint32_t fn_00467790(uint8_t* obj, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    const uint32_t r = orig::fn_00465230(u(obj), a0, a1, a2, a3);
    Field<uint8_t>(obj, 0x4fc) = 0;
    return r;
}

static uint32_t fn_00467790_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_00467790(B(ecx), a0, a1, a2, a3);
}
ABI_ENTRY_00467790(fn_00467790_Entry, fn_00467790_Abi)
HOOK(0x00467790, "fn_00467790", fn_00467790_Entry, nullptr);

// 0x004677c0 (ecx = ui object; virtual, no direct callers): timed message. When active and visible: on the first
// visible frame (byte +0x4fc clear) the timer +0x4f8 = 2.0, otherwise it counts down by the frame time 0x00604a90; the
// object is shown, and hidden again when the timer is <= 0. Then +0x4fc = visible, and while visible g+0xdd3 = 1. Not
// executed in recorded runs.
uint32_t fn_004677c0(uint8_t* obj)
{
    const uint32_t bit = Field<uint32_t>(obj, 0x470) & 1;
    if (bit == 0 && (Field<uint8_t>(obj, 0x474) & 2) == 0)
        return 0;
    uint32_t flags = Field<uint32_t>(obj, 0x474);
    if ((flags & 2) != 0 || (bit != 0 && (flags & 4) != 0))
    {
        if (Field<uint8_t>(obj, 0x4fc) == 0)
            Field<uint32_t>(obj, 0x4f8) = 0x40000000u;
        else
            Field<float>(obj, 0x4f8) = Field<float>(obj, 0x4f8) - GameGlobal<float>(0x00604a90);
        Field<uint32_t>(obj, 0x474) |= 4;
        if (Field<float>(obj, 0x4f8) <= K(0x0056c05c))
            Field<uint32_t>(obj, 0x474) &= 0xfffffffbu;
    }
    flags = Field<uint32_t>(obj, 0x474);
    const bool visible = (flags & 2) != 0 || ((Field<uint8_t>(obj, 0x470) & 1) != 0 && (flags & 4) != 0);
    Field<uint8_t>(obj, 0x4fc) = visible ? 1 : 0;
    if (visible)
        Field<uint8_t>(G(), 0xdd3) = 1;
    return visible ? 1u : 0u;
}

static uint32_t fn_004677c0_Abi(uint32_t ecx)
{
    return fn_004677c0(B(ecx));
}
ABI_ENTRY_004677c0(fn_004677c0_Entry, fn_004677c0_Abi)
HOOK(0x004677c0, "fn_004677c0", fn_004677c0_Entry, nullptr);

// 0x00467890 (ecx = ui object; virtual, no direct callers): teammate order message. As 0x004677c0 with a 3.0 timer,
// and on the first visible frame it also hides the other order texts "ui_teammate_defend_player" (0x005b026c),
// "ui_teammate_defend_loc" (0x005b0254), "ui_teammate_attack" (0x005b0240) and "ui_teammate_return" (0x005b022c)
// (each when found and not this object). Then +0x4fc = visible; returns it (does not touch g+0xdd3).
uint32_t fn_00467890(uint8_t* obj)
{
    const uint32_t bit = Field<uint32_t>(obj, 0x470) & 1;
    if (bit == 0 && (Field<uint8_t>(obj, 0x474) & 2) == 0)
        return 0;
    const uint32_t flags = Field<uint32_t>(obj, 0x474);
    if ((flags & 2) != 0 || (bit != 0 && (flags & 4) != 0))
    {
        if (Field<uint8_t>(obj, 0x4fc) == 0)
        {
            Field<uint32_t>(obj, 0x4f8) = 0x40400000u;
            static const uint32_t names[4] = {0x005b026cu, 0x005b0254u, 0x005b0240u, 0x005b022cu};
            for (uint32_t name : names)
            {
                uint8_t* t = FindUi(name);
                if (t != nullptr && t != obj)
                    Field<uint32_t>(t, 0x474) &= 0xfffffffbu;
            }
        }
        else
        {
            Field<float>(obj, 0x4f8) = Field<float>(obj, 0x4f8) - GameGlobal<float>(0x00604a90);
        }
        Field<uint32_t>(obj, 0x474) |= 4;
        if (Field<float>(obj, 0x4f8) <= K(0x0056c05c))
            Field<uint32_t>(obj, 0x474) &= 0xfffffffbu;
    }
    const uint32_t now = Field<uint32_t>(obj, 0x474);
    if ((now & 2) == 0 && ((Field<uint8_t>(obj, 0x470) & 1) == 0 || (now & 4) == 0))
    {
        Field<uint8_t>(obj, 0x4fc) = 0;
        return 0;
    }
    Field<uint8_t>(obj, 0x4fc) = 1;
    return 1;
}

static uint32_t fn_00467890_Abi(uint32_t ecx)
{
    return fn_00467890(B(ecx));
}
ABI_ENTRY_00467890(fn_00467890_Entry, fn_00467890_Abi)
HOOK(0x00467890, "fn_00467890", fn_00467890_Entry, nullptr);

// 0x004679f0 (ecx = ui object; stack: 4 args; ret 0x10; virtual, no direct callers): 0x004648b0 (base method), then
// the byte +0x4a0 = 0. Not executed in recorded runs.
uint32_t fn_004679f0(uint8_t* obj, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    const uint32_t r = orig::fn_004648b0(u(obj), a0, a1, a2, a3);
    Field<uint8_t>(obj, 0x4a0) = 0;
    return r;
}

static uint32_t fn_004679f0_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_004679f0(B(ecx), a0, a1, a2, a3);
}
ABI_ENTRY_004679f0(fn_004679f0_Entry, fn_004679f0_Abi)
HOOK(0x004679f0, "fn_004679f0", fn_004679f0_Entry, nullptr);

namespace
{
// 0x0046a640 (eax = object; returns st0): called here so that st0 is popped into a float (the generated orig wrapper
// would leave it on the x87 stack). guess: the object's trigger radius.
float Fn0046a640(uint32_t eaxValue)
{
    static const uint32_t target = 0x0046a640;
    float result;
    __asm {
        push ebx
        push esi
        push edi
        push ebp
        mov eax, eaxValue
        call dword ptr [target]
        pop ebp
        pop edi
        pop esi
        pop ebx
        fstp dword ptr [result]
    }
    return result;
}
// Releases a list iterator after an early exit: its first word's virtual +0xc (stack: second word), then the second
// word's +0x18 is decremented (0x00467450, 0x00467b60).
inline void IteratorRelease(const uint32_t* iterator)
{
    const uint32_t lock = iterator[1];
    VCall<uint32_t>(P(iterator[0]), 0xc / 4, lock);
    --Field<int32_t>(P(lock), 0x18);
}
}  // namespace

// 0x00467b60 (ecx = ui_text; virtual, no direct callers): pub entrance prompt. When active and the player g+0x748 is
// set: hides it; when g+0xac8 is clear and 0x0047a950(player) is true: walks the list g+0xa84 +0xeac; for each item
// with position 0x0046a5f0 (ecx = item, esi = out), distA = |player +0xc18 - pos| and distB = |player +0xc0c - pos|
// (0x004e7880); the first item with distB <= radius (0x0046a640) is taken: if radius < distA and 0x0046a6c0(item) is
// false, 0x0046a6d0(item) and "ui_pub_fail" (0x005b0288) is shown; else if 0x0046a6c0(item) is true the object is
// shown and "ui_pub_fail" hidden; the iterator is released early (first word's virtual +0xc). Finally, when visible,
// 0x00465280 (ecx = 0x14; object, 0x23) and UiText_FitToBox(576, 0); the iterator lock is released when the walk
// ended normally. Frame: 0x24 bytes (distA +0, iterator +4, delta +0xc, position +0x18). Not executed in recorded runs.
uint32_t fn_00467b60(uint8_t* text)
{
    if (!UiActive(text))
        return 0;
    uint8_t* g = G();
    if (Field<uint32_t>(g, 0x748) == 0)
        return 0;
    Field<uint32_t>(text, 0x474) &= 0xfffffffbu;
    if (Field<uint32_t>(g, 0xac8) != 0)
        return 0;
    if (Field<uint32_t>(g, 0x748) != 0)
    {
        if (static_cast<uint8_t>(orig::fn_0047a950(Field<uint32_t>(g, 0x748))) == 0)
            return 0;
        g = G();
    }
    uint32_t iterator[2] = {0, 0};
    uint32_t lock = 0;
    uint32_t item = orig::fn_004e7340(Field<uint32_t>(Field<void*>(g, 0xa84), 0xeac), u(iterator));
    for (;;)
    {
        if (item == 0)
        {
            lock = iterator[1];
            break;
        }
        float pos[3];
        float delta[3];
        orig::fn_0046a5f0(item, u(pos));
        uint8_t* game = G();
        const uint8_t* player = B(Field<uint32_t>(game, 0x748));
        delta[0] = Field<float>(player, 0xc18) - pos[0];
        delta[1] = Field<float>(player, 0xc1c) - pos[1];
        delta[2] = Field<float>(player, 0xc20) - pos[2];
        const float distA = Fn004e7880(u(delta), u(delta));
        delta[0] = Field<float>(player, 0xc0c) - pos[0];
        delta[1] = Field<float>(player, 0xc10) - pos[1];
        delta[2] = Field<float>(player, 0xc14) - pos[2];
        const float distB = Fn004e7880(u(delta), u(delta));
        if (!(Fn0046a640(item) < distB))
        {
            const float radius = Fn0046a640(item);
            if (radius < distA && static_cast<uint8_t>(orig::fn_0046a6c0(item)) == 0)
            {
                orig::fn_0046a6d0(item);
                IteratorRelease(iterator);
                if (uint8_t* t = FindUi(0x005b0288u))
                    Field<uint32_t>(t, 0x474) |= 4;
            }
            else
            {
                if (static_cast<uint8_t>(orig::fn_0046a6c0(item)) != 0)
                {
                    Field<uint32_t>(text, 0x474) |= 4;
                    if (uint8_t* t = B(orig::fn_00463890(0x005b0288u, Field<uint32_t>(game, 0xa70))))
                        Field<uint32_t>(t, 0x474) &= 0xfffffffbu;
                }
                IteratorRelease(iterator);
            }
            lock = 0;
            break;
        }
        item = orig::fn_004e73c0(u(iterator));
    }
    const uint32_t flags = Field<uint32_t>(text, 0x474);
    if ((flags & 2) != 0 || ((Field<uint8_t>(text, 0x470) & 1) != 0 && (flags & 4) != 0))
    {
        orig::fn_00465280(0x14, u(text), 0x23);
        UiText_FitToBox(text, bf(0x44100000u), 0.0f);
    }
    if (lock != 0)
        --Field<int32_t>(P(lock), 0x18);
    return lock;
}

static uint32_t fn_00467b60_Abi(uint32_t ecx)
{
    return fn_00467b60(B(ecx));
}
ABI_ENTRY_00467b60(fn_00467b60_Entry, fn_00467b60_Abi)
HOOK(0x00467b60, "fn_00467b60", fn_00467b60_Entry, nullptr);

// 0x00467d90 (ecx = ui object; stack: 4 args; ret 0x10; virtual, no direct callers): 0x004648b0 (base method),
// 0x00453540 (eax = object, cl = 1), +0x4a0 = 1.0. Not executed in recorded runs.
uint32_t fn_00467d90(uint8_t* obj, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    orig::fn_004648b0(u(obj), a0, a1, a2, a3);
    orig::fn_00453540(u(obj), 1);
    Field<uint32_t>(obj, 0x4a0) = 0x3f800000u;
    return u(obj);
}

static uint32_t fn_00467d90_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_00467d90(B(ecx), a0, a1, a2, a3);
}
ABI_ENTRY_00467d90(fn_00467d90_Entry, fn_00467d90_Abi)
HOOK(0x00467d90, "fn_00467d90", fn_00467d90_Entry, nullptr);

// 0x00467f00 (ecx = ui object; stack: 4 args; ret 0x10; virtual, no direct callers): 0x004648b0 (base method), then
// +0x4a0 = 0, +0x4a4 = +0x4a8 = -1. Returns -1. Not executed in recorded runs.
uint32_t fn_00467f00(uint8_t* obj, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    orig::fn_004648b0(u(obj), a0, a1, a2, a3);
    Field<uint32_t>(obj, 0x4a0) = 0;
    Field<uint32_t>(obj, 0x4a4) = 0xffffffffu;
    Field<uint32_t>(obj, 0x4a8) = 0xffffffffu;
    return 0xffffffffu;
}

static uint32_t fn_00467f00_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_00467f00(B(ecx), a0, a1, a2, a3);
}
ABI_ENTRY_00467f00(fn_00467f00_Entry, fn_00467f00_Abi)
HOOK(0x00467f00, "fn_00467f00", fn_00467f00_Entry, nullptr);

// 0x00467f50 (ecx = ui object; virtual, no direct callers): 0x00464a00 (ecx = object), then unless the object's ref
// name (0x00464ae0, eax = object) is "ref_mission_bombs" (0x005a0c70, stricmp): gets its 0x44-byte matrix (virtual
// +0x70) and sets the box +0x490 = m[3]/640 - 0.08, +0x494 = that + 0.16, +0x498 = m[7]/480 - 0.07, +0x49c = that +
// 0.14 (0x005bcd28 / 0x005bcd20 / 0x005bcd24 / 0x005bcd1c). Returns the stricmp result. Not executed in recorded runs.
uint32_t fn_00467f50(uint8_t* obj)
{
    float m[17];
    orig::fn_00464a00(u(obj));
    const int r = Stricmp(orig::fn_00464ae0(u(obj)), 0x005a0c70u);
    if (r == 0)
        return 0;
    VCall<uint32_t>(obj, 0x70 / 4, m);
    const float x = m[3] * K(0x005bcbf8) - K(0x005bcd28);
    Field<float>(obj, 0x490) = x;
    const float y = m[7] * K(0x005bcbf4) - K(0x005bcd24);
    Field<float>(obj, 0x498) = y;
    Field<float>(obj, 0x494) = x + K(0x005bcd20);
    Field<float>(obj, 0x49c) = y + K(0x005bcd1c);
    return static_cast<uint32_t>(r);
}

static uint32_t fn_00467f50_Abi(uint32_t ecx)
{
    return fn_00467f50(B(ecx));
}
ABI_ENTRY_00467f50(fn_00467f50_Entry, fn_00467f50_Abi)
HOOK(0x00467f50, "fn_00467f50", fn_00467f50_Entry, nullptr);

// 0x00467fd0 (edx = block): zeroes 0x55 dwords (0x154 bytes) at edx and returns it. Caller 0x004011e0.
uint32_t fn_00467fd0(uint32_t* block)
{
    for (int i = 0; i < 0x55; ++i)
        block[i] = 0;
    return u(block);
}

static uint32_t fn_00467fd0_Abi(uint32_t edx)
{
    return fn_00467fd0(reinterpret_cast<uint32_t*>(edx));
}
ABI_ENTRY_00467fd0(fn_00467fd0_Entry, fn_00467fd0_Abi)
HOOK(0x00467fd0, "fn_00467fd0", fn_00467fd0_Entry, nullptr);

// ================================================================================================================
// Chat display (3 lines of 0x100 wide characters at +0x24; colour table +0 (6 dwords); line colour indices +0x18;
// line times +0x624; ui texts "ui_chat_01".."ui_chat_03" +0x630)
// ================================================================================================================

namespace
{
inline void* Memmove(void* d, const void* s, uint32_t n)
{
    return GameFunction<void*(__cdecl*)(void*, const void*, uint32_t)>(0x00538ae0)(d, s, n);
}
inline wchar_t* Wcsncpy(wchar_t* d, const wchar_t* s, uint32_t n)
{
    return GameFunction<wchar_t*(__cdecl*)(wchar_t*, const wchar_t*, uint32_t)>(0x00538e1d)(d, s, n);
}
inline wchar_t* Wcscpy(wchar_t* d, const wchar_t* s)
{
    return GameFunction<wchar_t*(__cdecl*)(wchar_t*, const wchar_t*)>(0x00538e84)(d, s);
}
// Scrolls the chat up one line (inlined in 0x0046a820 and 0x0046a980).
inline void Chat_Scroll(uint8_t* chat)
{
    Memmove(chat + 0x24, chat + 0x224, 0x400);
    Memmove(chat + 0x624, chat + 0x628, 8);
    Memmove(chat + 0x18, chat + 0x1c, 8);
    Field<uint16_t>(chat, 0x424) = 0;
    Field<uint32_t>(chat, 0x62c) = 0;
    Field<uint32_t>(chat, 0x20) = 0;
}
// Renderer virtual +0x1c0 (outWidth, outHeight, text, 1.0, 1.0, 1, count (0 = all), 0): measures text (guess from use).
inline void MeasureText(float* width, float* height, const wchar_t* text, uint32_t count)
{
    VCall<uint32_t>(Renderer(), 0x1c0 / 4, width, height, text, 0x3f800000u, 0x3f800000u, 1u, count, 0u);
}
}  // namespace

uint32_t Chat_Clear(uint8_t* chat);

// 0x0046a7e0 (esi = chat): sets the colour table (+0: 0xff000000, 0xff1fffff, 0xfff0f01f, 0xffff1fff, 0xff1fffff,
// 0xffff0000) and clears the chat (Chat_Clear). Returns the chat. Caller 0x004011e0.
uint32_t Chat_Init(uint8_t* chat)
{
    Field<uint32_t>(chat, 0x00) = 0xff000000u;
    Field<uint32_t>(chat, 0x04) = 0xff1fffffu;
    Field<uint32_t>(chat, 0x08) = 0xfff0f01fu;
    Field<uint32_t>(chat, 0x0c) = 0xffff1fffu;
    Field<uint32_t>(chat, 0x10) = 0xff1fffffu;
    Field<uint32_t>(chat, 0x14) = 0xffff0000u;
    Chat_Clear(chat);
    return u(chat);
}

static uint32_t Chat_Init_Abi(uint32_t esi)
{
    return Chat_Init(B(esi));
}
ABI_ENTRY_0046a7e0(Chat_Init_Entry, Chat_Init_Abi)
HOOK(0x0046a7e0, "Chat_Init", Chat_Init_Entry, nullptr);

// 0x0046a820 (stack: chat; ret 4): per-frame chat update (caller ScenePass_Render 0x00489070). When the oldest line
// is 6 s old (game time 0x00604a98 - +0x624 >= 6.0, 0x005bccf0) the chat scrolls. Finds "ui_chat_01".."03" once
// (sprintf "ui_chat_0%d" (0x005a05e4) into a 0x20-byte buffer, 0x00463890). Each non-empty line i is shown in text i
// (UiText_SetWideText, UiText_SetColour(colour table[+0x18 + 4i]), +0x474 |= 4); at the first empty line the
// remaining texts are hidden. Returns the line counter.
uint32_t Chat_Update(uint8_t* chat)
{
    if (GameGlobal<float>(0x00604a98) - Field<float>(chat, 0x624) >= K(0x005bccf0))  // test ah,1: not below
        Chat_Scroll(chat);
    uint32_t* texts = &Field<uint32_t>(chat, 0x630);
    if (texts[0] == 0)
    {
        char name[0x20];
        int32_t i = 0;
        do
        {
            ++i;
            GameFunction<int(__cdecl*)(char*, const char*, int32_t)>(0x0053814f)(
                name, reinterpret_cast<const char*>(0x005a05e4), i);
            texts[i - 1] = orig::fn_00463890(u(name), Field<uint32_t>(G(), 0xa70));
        } while (i < 3);
    }
    int32_t i = 0;
    do
    {
        const wchar_t* line = reinterpret_cast<const wchar_t*>(chat + 0x24 + i * 0x200);
        if (line[0] == 0)
        {
            for (int32_t j = i; j < 3; ++j)
            {
                if (texts[j] != 0)
                    Field<uint32_t>(P(texts[j]), 0x474) &= 0xfffffffbu;
            }
            return static_cast<uint32_t>(i);
        }
        if (texts[i] != 0)
        {
            UiText_SetWideText(line, B(texts[i]));
            UiText_SetColour(B(texts[i]), Field<uint32_t>(chat, Field<uint32_t>(chat, 0x18 + i * 4) * 4));
            Field<uint32_t>(P(texts[i]), 0x474) |= 4;
        }
        ++i;
    } while (i < 3);
    return static_cast<uint32_t>(i);
}

static uint32_t Chat_Update_Abi(uint32_t a0)
{
    return Chat_Update(B(a0));
}
ABI_ENTRY_0046a820(Chat_Update_Entry, Chat_Update_Abi)
HOOK(0x0046a820, "Chat_Update", Chat_Update_Entry, nullptr);

// 0x0046a980 (esi = chat; stack: text, colour index; ret 8): appends a line: the first empty line (scrolling when all
// three are used), copied with wcsncpy(.., 0xff) (or " " (0x005a05e0) for null/empty text), terminated at 0xff, time
// = game time 0x00604a98, colour index stored. Returns the colour index.
uint32_t Chat_AddLine(uint8_t* chat, const wchar_t* text, uint32_t colour)
{
    int32_t i = 0;
    while (Field<uint16_t>(chat, 0x24 + i * 0x200) != 0)
    {
        ++i;
        if (i >= 3)
            break;
    }
    if (i == 3)
    {
        Chat_Scroll(chat);
        i = 2;
    }
    uint8_t* line = chat + i * 0x200;
    if (text == nullptr || text[0] == 0)
        Wcscpy(reinterpret_cast<wchar_t*>(line + 0x24), reinterpret_cast<const wchar_t*>(0x005a05e0));
    else
        Wcsncpy(reinterpret_cast<wchar_t*>(line + 0x24), text, 0xff);
    Field<uint16_t>(line, 0x222) = 0;
    Field<float>(chat, 0x624 + i * 4) = GameGlobal<float>(0x00604a98);
    Field<uint32_t>(chat, 0x18 + i * 4) = colour;
    return colour;
}

static uint32_t Chat_AddLine_Abi(uint32_t esi, uint32_t a0, uint32_t a1)
{
    return Chat_AddLine(B(esi), reinterpret_cast<const wchar_t*>(a0), a1);
}
ABI_ENTRY_0046a980(Chat_AddLine_Entry, Chat_AddLine_Abi)
HOOK(0x0046a980, "Chat_AddLine", Chat_AddLine_Entry, nullptr);

// 0x0046aa60 (eax = text; stack: chat, colour index; ret 8): adds a chat message with word wrap at 490 units
// (0x005bccc4) measured with renderer virtual +0x1c0. The text is copied to a 300-character stack buffer
// (wcsncpy, not terminated when longer: original behaviour). While the rest is wider than 490: grows a prefix one
// character at a time, remembering the first position where it overflows and the last space before the overflow;
// breaks at the space (its width <= 490) when one exists, else at the overflow position, where that character is
// dropped (original bug, see the final report). Each line goes through Chat_AddLine. Frame: 0x26c bytes (width +0,
// height +4, prefix width +8, byte count +0xc, space width +0x10, buffer +0x14). Returns 0 (the original's EAX is a
// leftover).
uint32_t Chat_AddMessage(const wchar_t* text, uint8_t* chat, uint32_t colour)
{
    float width;
    float height;
    float prefixWidth;
    float spaceWidth;
    wchar_t buffer[300];
    Wcsncpy(buffer, text, 0x12c);
    wchar_t* line = buffer;
    MeasureText(&width, &height, line, 0);
    if (width > K(0x005bccc4))
    {
        do
        {
            if (line[0] == 0)
                break;
            wchar_t* space = nullptr;
            wchar_t* overflow = nullptr;
            prefixWidth = 0.0f;
            spaceWidth = 0.0f;
            wchar_t* p = line + 1;
            if (*p == 0)
                break;
            int32_t bytes = static_cast<int32_t>(reinterpret_cast<uint8_t*>(p) - reinterpret_cast<uint8_t*>(line));
            bool atSpace = false;
            for (;;)
            {
                MeasureText(&prefixWidth, &height, line, static_cast<uint32_t>(bytes >> 1));
                if (overflow == nullptr && prefixWidth > K(0x005bccc4))
                    overflow = p;
                if (*p == 0x20)
                {
                    if (prefixWidth > K(0x005bccc4) && space != nullptr && spaceWidth <= K(0x005bccc4))
                    {
                        atSpace = true;
                        break;
                    }
                    spaceWidth = prefixWidth;
                    space = p;
                }
                ++p;
                bytes += 2;
                if (*p == 0)
                    break;
            }
            if (atSpace)
            {
                *space = 0;
                Chat_AddLine(chat, line, colour);
                line = space + 1;
            }
            else if (space != nullptr)
            {
                *space = 0;
                Chat_AddLine(chat, line, colour);
                line = space + 1;
            }
            else
            {
                if (overflow == nullptr)
                    break;
                const wchar_t saved = *overflow;
                *overflow = 0;
                Chat_AddLine(chat, line, colour);
                *overflow = saved;
                line = overflow + 1;
            }
            MeasureText(&width, &height, line, 0);
        } while (width > K(0x005bccc4));
    }
    if (line != nullptr && line[0] != 0)
        Chat_AddLine(chat, line, colour);
    return 0;
}

static uint32_t Chat_AddMessage_Abi(uint32_t eax, uint32_t a0, uint32_t a1)
{
    return Chat_AddMessage(reinterpret_cast<const wchar_t*>(eax), B(a0), a1);
}
ABI_ENTRY_0046aa60(Chat_AddMessage_Entry, Chat_AddMessage_Abi)
HOOK(0x0046aa60, "Chat_AddMessage", Chat_AddMessage_Entry, nullptr);

// 0x0046acb0 (edx = chat): clears the three lines (0x180 dwords at +0x24), the colour indices +0x18..+0x20, the times
// +0x624..+0x62c and the text pointers +0x630..+0x638. Returns 0.
uint32_t Chat_Clear(uint8_t* chat)
{
    uint32_t* lines = &Field<uint32_t>(chat, 0x24);
    for (int i = 0; i < 0x180; ++i)
        lines[i] = 0;
    Field<uint32_t>(chat, 0x18) = 0;
    Field<uint32_t>(chat, 0x1c) = 0;
    Field<uint32_t>(chat, 0x20) = 0;
    Field<uint32_t>(chat, 0x624) = 0;
    Field<uint32_t>(chat, 0x628) = 0;
    Field<uint32_t>(chat, 0x62c) = 0;
    Field<uint32_t>(chat, 0x630) = 0;
    Field<uint32_t>(chat, 0x634) = 0;
    Field<uint32_t>(chat, 0x638) = 0;
    return 0;
}

static uint32_t Chat_Clear_Abi(uint32_t edx)
{
    return Chat_Clear(B(edx));
}
ABI_ENTRY_0046acb0(Chat_Clear_Entry, Chat_Clear_Abi)
HOOK(0x0046acb0, "Chat_Clear", Chat_Clear_Entry, nullptr);

// ================================================================================================================
// Menu widget drawing helpers (640x480 pixel offsets relative to a ui_text's position +0x4e8/+0x4ec)
// ================================================================================================================

// 0x0046b0a0 (eax = ui_text, ecx = out x, edx = out y; stack: x, y (int, 640x480 units); ret 8): *ecx = x / 640 +
// +0x4e8, *edx = y / 480 + +0x4ec. EAX is preserved (callers chain on it).
void Ui_PixelToScreen(const uint8_t* text, float* outX, float* outY, int32_t x, int32_t y)
{
    *outX = static_cast<float>(x) * K(0x005bcbf8) + Field<float>(text, 0x4e8);
    *outY = static_cast<float>(y) * K(0x005bcbf4) + Field<float>(text, 0x4ec);
}

static void Ui_PixelToScreen_Abi(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t a0, uint32_t a1)
{
    Ui_PixelToScreen(B(eax), reinterpret_cast<float*>(ecx), reinterpret_cast<float*>(edx), static_cast<int32_t>(a0),
                     static_cast<int32_t>(a1));
}
ABI_ENTRY_0046b0a0(Ui_PixelToScreen_Entry, Ui_PixelToScreen_Abi)
HOOK(0x0046b0a0, "Ui_PixelToScreen", Ui_PixelToScreen_Entry, nullptr);

// 0x0046b0d0 (eax = ui_text, ecx = x0, edx = y0; stack: x1, y1, width, height (ints, 640x480 units), a4, a5, a6, a7,
// align (float[2] or null), a9; ret 0x28): sorts the corners (signed), converts them (Ui_PixelToScreen), computes the
// centre c and half size h of the box, then an outer half size and an inner one offset by the border (width / 640,
// height / 480): without align outer = h, inner = h - border; with align outer = h + border * align, inner = h -
// border * (1 - align). Draws with Ui_DrawRing (eax = ui_text, esi = &c, edi = &inner; stack: &outer, a4, a5, a6, a7,
// a9). The border height is kept in the align argument's stack slot (as the original). Returns Ui_DrawRing's result.
uint32_t Ui_DrawFrame(uint8_t* text, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t width, int32_t height,
                      uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7, const float* align, uint32_t a9)
{
    float outer[2];   // [esp+0x10] S+0 / S+4
    float centre[2];  // S+8 / S+0xc
    float inner[2];   // S+0x10 / S+0x14
    if (x0 > x1)
    {
        const int32_t t = x0;
        x0 = x1;
        x1 = t;
    }
    if (y0 > y1)
    {
        const int32_t t = y0;
        y0 = y1;
        y1 = t;
    }
    Ui_PixelToScreen(text, &centre[0], &centre[1], x0, y0);
    Ui_PixelToScreen(text, &outer[0], &outer[1], x1, y1);
    outer[0] = (outer[0] - centre[0]) * K(0x005bcb4c);
    outer[1] = (outer[1] - centre[1]) * K(0x005bcb4c);
    centre[0] = centre[0] + outer[0];
    centre[1] = centre[1] + outer[1];
    const float bw = static_cast<float>(width) * K(0x005bcbf8);
    const float bh = static_cast<float>(height) * K(0x005bcbf4);
    if (align == nullptr)
    {
        inner[0] = outer[0] - bw;
        inner[1] = outer[1] - bh;
    }
    else
    {
        inner[0] = outer[0] - (K(0x005bcb2c) - align[0]) * bw;
        inner[1] = outer[1] - (K(0x005bcb2c) - align[1]) * bh;
        outer[0] = bw * align[0] + outer[0];
        outer[1] = bh * align[1] + outer[1];
    }
    return orig::fn_0046b570(u(text), u(centre), u(inner), u(outer), a4, a5, a6, a7, a9);
}

static uint32_t Ui_DrawFrame_Abi(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t a0, uint32_t a1, uint32_t a2,
                                 uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7, uint32_t a8,
                                 uint32_t a9)
{
    return Ui_DrawFrame(B(eax), static_cast<int32_t>(ecx), static_cast<int32_t>(edx), static_cast<int32_t>(a0),
                        static_cast<int32_t>(a1), static_cast<int32_t>(a2), static_cast<int32_t>(a3), a4, a5, a6, a7,
                        reinterpret_cast<const float*>(a8), a9);
}
ABI_ENTRY_0046b0d0(Ui_DrawFrame_Entry, Ui_DrawFrame_Abi)
HOOK(0x0046b0d0, "Ui_DrawFrame", Ui_DrawFrame_Entry, nullptr);

// 0x0046b200 (eax = ui_text, ecx = y0, edx = x0, esi = passed through; stack: x1, y1 (ints), a2, a3, a4; ret 0x14):
// converts both corners (Ui_PixelToScreen; the second pair is written back into the x1/y1 stack slots) and draws
// Ui_DrawRect (eax = ui_text, esi; stack: x0, y0, x1, y1, a2, a3, a4). Returns its result.
uint32_t Ui_DrawPixelRect(uint8_t* text, int32_t y0, int32_t x0, uint32_t esi, int32_t x1, int32_t y1, uint32_t a2,
                          uint32_t a3, uint32_t a4)
{
    float p0[2];  // S+4 = x, S+0 = y
    float p1[2];
    Ui_PixelToScreen(text, &p0[0], &p0[1], x0, y0);
    Ui_PixelToScreen(text, &p1[0], &p1[1], x1, y1);
    return orig::fn_0046b2c0(u(text), esi, fb(p0[0]), fb(p0[1]), fb(p1[0]), fb(p1[1]), a2, a3, a4);
}

static uint32_t Ui_DrawPixelRect_Abi(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t esi, uint32_t a0, uint32_t a1,
                                     uint32_t a2, uint32_t a3, uint32_t a4)
{
    return Ui_DrawPixelRect(B(eax), static_cast<int32_t>(ecx), static_cast<int32_t>(edx), esi,
                            static_cast<int32_t>(a0), static_cast<int32_t>(a1), a2, a3, a4);
}
ABI_ENTRY_0046b200(Ui_DrawPixelRect_Entry, Ui_DrawPixelRect_Abi)
HOOK(0x0046b200, "Ui_DrawPixelRect", Ui_DrawPixelRect_Entry, nullptr);

void Spline_Coefficients(float* out, float p0, float p1, float p2, float p3);

// 0x0046bbe0 (eax = ui_text, cl = down, edx = y, esi = arrow widget; stack: x; ret 4): sets up a scroll arrow: byte +2
// = down, +4 (highlight) = 0, and the corners (+8, +0xc) / (+0x10, +0x14) = Ui_PixelToScreen of (x - 40, y - 16) and
// (x + 40, y - 4) when down, else (x - 40, y + 4) and (x + 40, y + 16). Caller 0x0046c980.
void Ui_ArrowInit(uint8_t* text, uint32_t down, int32_t y, uint8_t* arrow, int32_t x)
{
    Field<uint8_t>(arrow, 2) = static_cast<uint8_t>(down);
    Field<uint32_t>(arrow, 4) = 0;
    int32_t y0;
    int32_t y1;
    if (static_cast<uint8_t>(down) != 0)
    {
        y0 = y - 0x10;
        y1 = y - 4;
    }
    else
    {
        y0 = y + 4;
        y1 = y + 0x10;
    }
    Ui_PixelToScreen(text, &Field<float>(arrow, 8), &Field<float>(arrow, 0xc), x - 0x28, y0);
    Ui_PixelToScreen(text, &Field<float>(arrow, 0x10), &Field<float>(arrow, 0x14), x + 0x28, y1);
}

static void Ui_ArrowInit_Abi(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t esi, uint32_t a0)
{
    Ui_ArrowInit(B(eax), ecx, static_cast<int32_t>(edx), B(esi), static_cast<int32_t>(a0));
}
ABI_ENTRY_0046bbe0(Ui_ArrowInit_Entry, Ui_ArrowInit_Abi)
HOOK(0x0046bbe0, "Ui_ArrowInit", Ui_ArrowInit_Entry, nullptr);

// 0x0046bc30 (al = enabled, cl = highlighted, esi = arrow widget; stack: ui_text; ret 4): byte +0 = enabled, +1 =
// highlighted. Disabled: +4 = 0. Enabled: the glow +4 decays by 0.05 (0x005bcc34) per call but not below 0.25 when
// highlighted (0 otherwise; the decayed value is kept only when greater); brightness b = ftol of the cubic through
// (96, 128, 128, 255) (Spline_Coefficients, Horner in +4); draws the arrow as two Ui_DrawTriangle calls (eax =
// ui_text): (x0, yA) (mid, yA) (mid, yB) with colours ecx = 0x00bbbbbb, 0xffbbbbbb, 0x00bbbbbb, 0, and (mid, yA) (x1, yA)
// (mid, yB) with ecx = 0xffbbbbbb, 0x00bbbbbb, 0x00bbbbbb, 0 (yA = the edge at the tip side by byte +2, mid = (x0 + x1) /
// 2). Frame 0x18 bytes (mid +0, yB pointer +4, coefficients +8). Not executed in recorded runs.
uint32_t Ui_ArrowDraw(uint32_t eax, uint32_t ecx, uint8_t* arrow, uint32_t text)
{
    Field<uint8_t>(arrow, 0) = static_cast<uint8_t>(eax);
    Field<uint8_t>(arrow, 1) = static_cast<uint8_t>(ecx);
    if (static_cast<uint8_t>(eax) == 0)
    {
        Field<uint32_t>(arrow, 4) = 0;
        return eax;
    }
    float c[4];
    Spline_Coefficients(c, bf(0x42c00000u), bf(0x43000000u), bf(0x43000000u), bf(0x437f0000u));
    const float t = Field<float>(arrow, 4) - K(0x005bcc34);
    const uint32_t up = Field<uint8_t>(arrow, 2) == 0 ? 1u : 0u;
    Field<float>(arrow, 4) = t;
    float v = static_cast<uint8_t>(ecx) != 0 ? K(0x005bcbe4) : K(0x0056c05c);
    if (t > v)
        v = t;
    Field<float>(arrow, 4) = v;
    const uint32_t b = static_cast<uint32_t>(Ftol(((c[0] * v + c[1]) * v + c[2]) * v + c[3])) & 0xff;
    const float mid = (Field<float>(arrow, 8) + Field<float>(arrow, 0x10)) * K(0x005bcb4c);
    const uint32_t opaque = ((((b | 0xffffff00u) << 8) | b) << 8) | b;
    const uint32_t clear = (((b << 8) | b) << 8) | b;
    const uint32_t yA = Field<uint32_t>(arrow, up * 8 + 0xc);
    const uint32_t yB = Field<uint32_t>(arrow, (up ^ 1) * 8 + 0xc);
    orig::fn_0046b960(text, clear, Field<uint32_t>(arrow, 8), yA, fb(mid), yA, fb(mid), yB, opaque, clear, 0);
    return orig::fn_0046b960(text, opaque, fb(mid), yA, Field<uint32_t>(arrow, 0x10), yA, fb(mid), yB, clear, clear, 0);
}

static uint32_t Ui_ArrowDraw_Abi(uint32_t eax, uint32_t ecx, uint32_t esi, uint32_t a0)
{
    return Ui_ArrowDraw(eax, ecx, B(esi), a0);
}
ABI_ENTRY_0046bc30(Ui_ArrowDraw_Entry, Ui_ArrowDraw_Abi)
HOOK(0x0046bc30, "Ui_ArrowDraw", Ui_ArrowDraw_Entry, nullptr);

uint32_t Ui_ArrowGlow(uint8_t* arrow, float value);
uint32_t ListBox_SetRows(uint32_t rows, uint8_t* box);

// 0x0046bd70 (ecx = arrow widget; stack: x, y (floats); ret 8): hit test. When enabled (byte +0) and x0 <= x <= x1,
// y0 <= y <= y1 (+8..+0x14): Ui_ArrowGlow(0.75) and returns al = 1; else al = 0. Caller 0x0046bfd0. Not executed in
// recorded runs.
uint32_t Ui_ArrowHit(uint8_t* arrow, float x, float y)
{
    if (Field<uint8_t>(arrow, 0) == 0)
        return 0;
    if (x >= Field<float>(arrow, 8) && x <= Field<float>(arrow, 0x10) && y >= Field<float>(arrow, 0xc) &&
        y <= Field<float>(arrow, 0x14))
    {
        Ui_ArrowGlow(arrow, bf(0x3f400000u));
        return 1;
    }
    return 0;
}

static uint32_t Ui_ArrowHit_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return Ui_ArrowHit(B(ecx), bf(a0), bf(a1));
}
ABI_ENTRY_0046bd70(Ui_ArrowHit_Entry, Ui_ArrowHit_Abi)
HOOK(0x0046bd70, "Ui_ArrowHit", Ui_ArrowHit_Entry, nullptr);

// 0x0046bdd0 (ecx = list box; stack: 4 args; ret 0x10; virtual, no direct callers): list box init. 0x004651a0 (ecx =
// object, same arguments), +0x474 |= 0x400, capacity +0x4f8 = 0x40, rows +0x4fc = 12, +0x500..+0x50c = 0, the two
// arrow widgets +0x518 (12 dwords) zeroed, entries +0x50c = Alloc(1, capacity * 4, 2, "ListBoxEntries" (0x005a03f0))
// zero-filled, then ListBox_SetRows(12). Not executed in recorded runs.
uint32_t ListBox_Init(uint8_t* box, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    orig::fn_004651a0(u(box), a0, a1, a2, a3);
    Field<uint32_t>(box, 0x474) |= 0x400;
    Field<uint32_t>(box, 0x4f8) = 0x40;
    Field<uint32_t>(box, 0x4fc) = 0xc;
    Field<uint32_t>(box, 0x500) = 0;
    Field<uint32_t>(box, 0x504) = 0;
    Field<uint32_t>(box, 0x508) = 0;
    Field<uint32_t>(box, 0x50c) = 0;
    std::memset(box + 0x518, 0, 0xc * 4);
    const uint32_t bytes = Field<uint32_t>(box, 0x4f8) << 2;
    const uint32_t entries = Alloc(1, bytes, 2, 0x005a03f0u);
    Field<uint32_t>(box, 0x50c) = entries;
    std::memset(P(entries), 0, bytes);
    return ListBox_SetRows(0xc, box);
}

static uint32_t ListBox_Init_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return ListBox_Init(B(ecx), a0, a1, a2, a3);
}
ABI_ENTRY_0046bdd0(ListBox_Init_Entry, ListBox_Init_Abi)
HOOK(0x0046bdd0, "ListBox_Init", ListBox_Init_Entry, nullptr);

// 0x0046be90 (ecx = list box; virtual, no direct callers): frees the entry array +0x50c (0x004e08d0) and clears it,
// then tail-jumps to 0x00465160 (ecx = object).
uint32_t ListBox_Destroy(uint8_t* box)
{
    if (Field<uint32_t>(box, 0x50c) != 0)
    {
        Free(Field<uint32_t>(box, 0x50c));
        Field<uint32_t>(box, 0x50c) = 0;
    }
    return orig::fn_00465160(u(box));
}

static uint32_t ListBox_Destroy_Abi(uint32_t ecx)
{
    return ListBox_Destroy(B(ecx));
}
ABI_ENTRY_0046be90(ListBox_Destroy_Entry, ListBox_Destroy_Abi)
HOOK(0x0046be90, "ListBox_Destroy", ListBox_Destroy_Entry, nullptr);

// 0x0046bec0 (no inputs; virtual, no direct callers): returns 6 (guess: a type id). Not executed in recorded runs.
uint32_t fn_0046bec0()
{
    return 6;
}

static uint32_t fn_0046bec0_Abi()
{
    return fn_0046bec0();
}
ABI_ENTRY_0046bec0(fn_0046bec0_Entry, fn_0046bec0_Abi)
HOOK(0x0046bec0, "fn_0046bec0", fn_0046bec0_Entry, nullptr);

// 0x0046bed0 (ecx = list box; virtual, no direct callers): UiText_Layout, then resets the bounds (UiText_ExtendBounds,
// al = 0) to the box from (-128, 8) to (128, +0x514) in 640x480 units around the position (Ui_PixelToScreen). Frame:
// 0x10 bytes. Not executed in recorded runs.
uint32_t ListBox_Layout(uint8_t* box)
{
    float x0, y0, x1, y1;
    UiText_Layout(box);
    Ui_PixelToScreen(box, &x0, &y0, -0x80, 8);
    Ui_PixelToScreen(box, &x1, &y1, 0x80, Field<int32_t>(box, 0x514));
    return UiText_ExtendBounds(0, box, x0, y0, x1, y1);
}

static uint32_t ListBox_Layout_Abi(uint32_t ecx)
{
    return ListBox_Layout(B(ecx));
}
ABI_ENTRY_0046bed0(ListBox_Layout_Entry, ListBox_Layout_Abi)
HOOK(0x0046bed0, "ListBox_Layout", ListBox_Layout_Entry, nullptr);

uint32_t ListBox_Select(int32_t index, uint8_t* box);

// 0x0046bf30 (edi = list box; stack: x, y, click (byte); ret 0xc): finds the first visible entry (entries +0x50c,
// capacity +0x4f8; stops at a null entry) whose hit test (virtual +0x140 (x, y, click)) returns true; when it is not
// the selection +0x508 and click is set, plays UiSound_PlayMove (eax = 0) and selects it (ListBox_Select). Returns
// its index or -1. Not executed in recorded runs.
int32_t ListBox_HitEntry(uint8_t* box, uint32_t x, uint32_t y, uint32_t click)
{
    for (int32_t i = 0; i < Field<int32_t>(box, 0x4f8); ++i)
    {
        uint8_t* entry = B(Field<uint32_t*>(box, 0x50c)[i]);
        if (entry == nullptr)
            return -1;
        const uint32_t flags = Field<uint32_t>(entry, 0x474);
        if ((flags & 2) == 0 && ((Field<uint8_t>(entry, 0x470) & 1) == 0 || (flags & 4) == 0))
            continue;
        if (static_cast<uint8_t>(VCall<uint32_t>(entry, 0x140 / 4, x, y, click)) == 0)
            continue;
        if (i != Field<int32_t>(box, 0x508) && static_cast<uint8_t>(click) != 0)
        {
            orig::fn_00491890(0);
            ListBox_Select(i, box);
        }
        return i;
    }
    return -1;
}

static uint32_t ListBox_HitEntry_Abi(uint32_t edi, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return static_cast<uint32_t>(ListBox_HitEntry(B(edi), a0, a1, a2));
}
ABI_ENTRY_0046bf30(ListBox_HitEntry_Entry, ListBox_HitEntry_Abi)
HOOK(0x0046bf30, "ListBox_HitEntry", ListBox_HitEntry_Entry, nullptr);

// 0x0046bfd0 (eax = list box, ebx saved only (listed by the ABI table, overwritten before use); stack: x, y; ret 8):
// hit tests the two scroll arrows (+0x518 up, +0x530 down) with Ui_ArrowHit; returns -1 / +1 for the arrow hit, 0 for
// none. Not executed in recorded runs.
int32_t ListBox_HitArrows(uint8_t* box, float x, float y)
{
    int32_t result = -1;
    uint8_t* arrow = box + 0x518;
    for (int32_t i = 0; i < 2; ++i)
    {
        if (static_cast<uint8_t>(Ui_ArrowHit(arrow, x, y)) != 0)
            return result;
        arrow += 0x18;
        result += 2;
    }
    return 0;
}

static uint32_t ListBox_HitArrows_Abi(uint32_t eax, uint32_t /*ebx*/, uint32_t a0, uint32_t a1)
{
    return static_cast<uint32_t>(ListBox_HitArrows(B(eax), bf(a0), bf(a1)));
}
ABI_ENTRY_0046bfd0(ListBox_HitArrows_Entry, ListBox_HitArrows_Abi)
HOOK(0x0046bfd0, "ListBox_HitArrows", ListBox_HitArrows_Entry, nullptr);

// 0x0046c020 (ecx = list box; stack: x, y, click; ret 0xc; virtual +0x140, no direct callers): the base hit test
// (0x00467f40 = jmp 0x00464990, ecx = object, same arguments); when it hits, ListBox_HitEntry and, when no entry is
// hit, ListBox_HitArrows (x, y). Returns al = 1 when the base test hit (upper bytes from the last call), else the base
// result. Not executed in recorded runs.
uint32_t ListBox_HitTest(uint8_t* box, uint32_t x, uint32_t y, uint32_t click)
{
    const uint32_t r = orig::fn_00464990(u(box), x, y, click);
    if (static_cast<uint8_t>(r) == 0)
        return r;
    uint32_t last = static_cast<uint32_t>(ListBox_HitEntry(box, x, y, click));
    if (last == 0xffffffffu)
        last = static_cast<uint32_t>(ListBox_HitArrows(box, bf(x), bf(y)));
    return (last & 0xffffff00u) | 1u;
}

static uint32_t ListBox_HitTest_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return ListBox_HitTest(B(ecx), a0, a1, a2);
}
ABI_ENTRY_0046c020(ListBox_HitTest_Entry, ListBox_HitTest_Abi)
HOOK(0x0046c020, "ListBox_HitTest", ListBox_HitTest_Entry, nullptr);

uint32_t ListBox_SetTop(int32_t top, uint8_t* box);
uint32_t ListBox_SelectEntry(uint8_t* box, uint32_t entry);
uint32_t ListBox_AddEntry(uint32_t eax, uint32_t ebx, uint32_t a0, uint32_t a1, uint32_t a2);

// 0x0046c070 (ecx = list box; virtual, no direct callers): list box render. Once (bit 1 of 0x0064aaf4) fills the
// colour table 0x0064a734.. (0x80144a64, 0xe0144a64, 0x800a2532, 0xe00a2532, 0x000a2532 x2); colour row = +0x474 bit
// 0x80 (highlight). Draws the body frame Ui_DrawFrame(-128, 8, 128, +0x510, border 32 x 8, 0.25, colour[0 + row],
// colour[4 + row], 0, align {0.25, 1.0}, 0) and the footer frame (-128, +0x510, 128, +0x514, border 32 x 16, 0.25,
// colour[2 + row], colour[4 + row], 0, align {0.5, 0.5}, 0) (the 8-byte frame holds the align pair), the up arrow
// (enabled when top +0x504 != 0) and the down arrow (enabled when top + rows +0x4fc < count +0x500), highlighted with
// the box; then UiText_Render (ecx = edi = box) and ListBox_SetTop(top). Not executed in recorded runs.
uint32_t ListBox_Render(uint8_t* box)
{
    float align[2];
    if ((GameGlobal<uint8_t>(0x0064aaf4) & 1) == 0)
    {
        GameGlobal<uint32_t>(0x0064aaf4) |= 1;
        GameGlobal<uint32_t>(0x0064a734) = 0x80144a64u;
        GameGlobal<uint32_t>(0x0064a738) = 0xe0144a64u;
        GameGlobal<uint32_t>(0x0064a73c) = 0x800a2532u;
        GameGlobal<uint32_t>(0x0064a740) = 0xe00a2532u;
        GameGlobal<uint32_t>(0x0064a744) = 0x000a2532u;
        GameGlobal<uint32_t>(0x0064a748) = 0x000a2532u;
    }
    const uint32_t row = (Field<uint32_t>(box, 0x474) >> 7) & 1;
    align[0] = bf(0x3e800000u);
    align[1] = bf(0x3f800000u);
    Ui_DrawFrame(box, -0x80, 8, 0x80, Field<int32_t>(box, 0x510), 0x20, 8, 0x3e800000u,
                 GameGlobal<uint32_t>(0x0064a734 + row * 4), GameGlobal<uint32_t>(0x0064a744 + row * 4), 0, align, 0);
    align[0] = bf(0x3f000000u);
    align[1] = bf(0x3f000000u);
    Ui_DrawFrame(box, -0x80, Field<int32_t>(box, 0x510), 0x80, Field<int32_t>(box, 0x514), 0x20, 0x10, 0x3e800000u,
                 GameGlobal<uint32_t>(0x0064a73c + row * 4), GameGlobal<uint32_t>(0x0064a744 + row * 4), 0, align, 0);
    const uint32_t highlight = row != 0 ? 1u : 0u;
    Ui_ArrowDraw(Field<uint32_t>(box, 0x504) != 0 ? 1u : 0u, highlight, box + 0x518, u(box));
    const bool more = Field<int32_t>(box, 0x4fc) + Field<int32_t>(box, 0x504) < Field<int32_t>(box, 0x500);
    Ui_ArrowDraw(more ? 1u : 0u, highlight, box + 0x530, u(box));
    orig::fn_00465360(u(box), u(box));
    return ListBox_SetTop(Field<int32_t>(box, 0x504), box);
}

static uint32_t ListBox_Render_Abi(uint32_t ecx)
{
    return ListBox_Render(B(ecx));
}
ABI_ENTRY_0046c070(ListBox_Render_Entry, ListBox_Render_Abi)
HOOK(0x0046c070, "ListBox_Render", ListBox_Render_Entry, nullptr);

// 0x0046c1d0 (eax = list box): clears the list: ListBox_Select(0), ListBox_SetTop(0), hides every entry up to the first
// null one, count +0x500 = 0. Returns the capacity +0x4f8 (EAX of the last load) or the last entry pointer. Callers
// 0x00492c90, 0x00492d80. Not executed in recorded runs.
uint32_t ListBox_Clear(uint8_t* box)
{
    ListBox_Select(0, box);
    ListBox_SetTop(0, box);
    uint32_t eax = Field<uint32_t>(box, 0x4f8);
    for (int32_t i = 0; i < Field<int32_t>(box, 0x4f8); ++i)
    {
        eax = Field<uint32_t*>(box, 0x50c)[i];
        if (eax == 0)
            break;
        Field<uint32_t>(P(eax), 0x474) &= 0xfffffffbu;
        eax = Field<uint32_t>(box, 0x4f8);
    }
    Field<uint32_t>(box, 0x500) = 0;
    return eax;
}

static uint32_t ListBox_Clear_Abi(uint32_t eax)
{
    return ListBox_Clear(B(eax));
}
ABI_ENTRY_0046c1d0(ListBox_Clear_Entry, ListBox_Clear_Abi)
HOOK(0x0046c1d0, "ListBox_Clear", ListBox_Clear_Entry, nullptr);

// 0x0046c2d0 (edx = comparator (null = 0x0046c280), esi = list box): when the box has entries (+0x500), remembers the
// selected entry, qsort(entries +0x50c, count, 4, comparator) (CRT 0x00538f30) and re-selects that entry
// (0x0046c530, ecx = box, edi = entry). Returns its EAX. Not executed in recorded runs.
uint32_t ListBox_Sort(uint32_t comparator, uint8_t* box)
{
    const uint32_t count = Field<uint32_t>(box, 0x500);
    if (count == 0)
        return 0;
    const uint32_t entries = Field<uint32_t>(box, 0x50c);
    const uint32_t selected = Field<uint32_t*>(box, 0x50c)[Field<int32_t>(box, 0x508)];
    if (comparator == 0)
        comparator = 0x0046c280u;
    GameFunction<void(__cdecl*)(uint32_t, uint32_t, uint32_t, uint32_t)>(0x00538f30)(entries, count, 4, comparator);
    return ListBox_SelectEntry(box, selected);
}

static uint32_t ListBox_Sort_Abi(uint32_t edx, uint32_t esi)
{
    return ListBox_Sort(edx, B(esi));
}
ABI_ENTRY_0046c2d0(ListBox_Sort_Entry, ListBox_Sort_Abi)
HOOK(0x0046c2d0, "ListBox_Sort", ListBox_Sort_Entry, nullptr);

// 0x0046c2b0 (esi = list box): tail jump to ListBox_Sort with the comparator 0x0046c230. Caller 0x00492d80. Not
// executed in recorded runs.
uint32_t ListBox_SortA(uint8_t* box)
{
    return ListBox_Sort(0x0046c230u, box);
}

static uint32_t ListBox_SortA_Abi(uint32_t esi)
{
    return ListBox_SortA(B(esi));
}
ABI_ENTRY_0046c2b0(ListBox_SortA_Entry, ListBox_SortA_Abi)
HOOK(0x0046c2b0, "ListBox_SortA", ListBox_SortA_Entry, nullptr);

// 0x0046c2c0 (esi = list box): tail jump to ListBox_Sort with the comparator 0x0046c250. Caller 0x00492c90. Not
// executed in recorded runs.
uint32_t ListBox_SortB(uint8_t* box)
{
    return ListBox_Sort(0x0046c250u, box);
}

static uint32_t ListBox_SortB_Abi(uint32_t esi)
{
    return ListBox_SortB(B(esi));
}
ABI_ENTRY_0046c2c0(ListBox_SortB_Entry, ListBox_SortB_Abi)
HOOK(0x0046c2c0, "ListBox_SortB", ListBox_SortB_Entry, nullptr);

// 0x0046c310 (eax, ecx, edx, ebx = list box): ListBox_AddEntry (eax = 0, ebx; stack: edx, ecx, eax). Caller
// 0x00492d80. Not executed in recorded runs.
uint32_t ListBox_AddEntryDefault(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t ebx)
{
    return ListBox_AddEntry(0, ebx, edx, ecx, eax);
}

static uint32_t ListBox_AddEntryDefault_Abi(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t ebx)
{
    return ListBox_AddEntryDefault(eax, ecx, edx, ebx);
}
ABI_ENTRY_0046c310(ListBox_AddEntryDefault_Entry, ListBox_AddEntryDefault_Abi)
HOOK(0x0046c310, "ListBox_AddEntryDefault", ListBox_AddEntryDefault_Entry, nullptr);

// 0x0046c320 (eax = wide text, ebx = list box; stack: name (char*), select (byte), colour index; ret 0xc): appends an
// entry when count +0x500 < capacity +0x4f8 (else returns 0). The slot's ui_text is created once
// (ObjectSystem_CreateObject, eax = 0, ecx = "ui_text" 0x005b0334; stack: g+0xa84, 0, 0x3ff, 0, 0, 0, 0, 0xaa, 0) with
// the box's page +0x46c and ref +0x468, UiText_SetPosition(0.5 x4) and UiText_SetScale(0.8). Then count++, the entry's
// +0x474 bits 0x400/0x800 cleared and 0x400 set unless the box is highlighted (+0x474 bit 0x80); text
// (UiText_SetWideText) and/or name (UiText_SetName, lookup when there is no text), UiText_SetColour, position = the
// box's +0x4e8/+0x4ec; finally ListBox_Select(count - 1) when select is set, else ListBox_Select(+0x508). Returns the
// entry. Not executed in recorded runs.
uint32_t ListBox_AddEntry(uint32_t eax, uint32_t ebx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint8_t* box = B(ebx);
    const wchar_t* text = reinterpret_cast<const wchar_t*>(eax);
    const int32_t count = Field<int32_t>(box, 0x500);
    if (count >= Field<int32_t>(box, 0x4f8))
        return 0;
    uint32_t* entries = Field<uint32_t*>(box, 0x50c);
    if (entries[count] == 0)
    {
        Field<uint32_t*>(box, 0x50c)[Field<int32_t>(box, 0x500)] =
            orig::fn_0045f020(0, 0x005b0334u, Field<uint32_t>(G(), 0xa84), 0, 0x3ff, 0, 0, 0, 0, 0xaa, 0);
        uint8_t* created = B(Field<uint32_t*>(box, 0x50c)[Field<int32_t>(box, 0x500)]);
        Field<uint32_t>(created, 0x46c) = Field<uint32_t>(box, 0x46c);
        Field<uint32_t>(B(Field<uint32_t*>(box, 0x50c)[Field<int32_t>(box, 0x500)]), 0x468) =
            Field<uint32_t>(box, 0x468);
        UiText_SetPosition(B(Field<uint32_t*>(box, 0x50c)[Field<int32_t>(box, 0x500)]), 0x3f000000u, 0x3f000000u,
                           0x3f000000u, 0x3f000000u);
        UiText_SetScale(B(Field<uint32_t*>(box, 0x50c)[Field<int32_t>(box, 0x500)]), 0x3f4ccccdu);
    }
    const int32_t index = Field<int32_t>(box, 0x500);
    uint8_t* entry = B(Field<uint32_t*>(box, 0x50c)[index]);
    Field<int32_t>(box, 0x500) = index + 1;
    uint32_t flags = Field<uint32_t>(entry, 0x474) & 0xfffff3ffu;
    Field<uint32_t>(entry, 0x474) = flags;
    if (((~(Field<uint32_t>(box, 0x474) >> 7)) & 1) != 0)
        flags |= 0x400;
    else
        flags &= 0xfffffbffu;
    Field<uint32_t>(entry, 0x474) = flags;
    if (text != nullptr)
        UiText_SetWideText(text, entry);
    if (a0 != 0)
        UiText_SetName(reinterpret_cast<const char*>(a0), entry, text == nullptr ? 1u : 0u);
    UiText_SetColour(entry, a2);
    Field<uint32_t>(entry, 0x4e8) = Field<uint32_t>(box, 0x4e8);
    Field<uint32_t>(entry, 0x4ec) = Field<uint32_t>(box, 0x4ec);
    if (static_cast<uint8_t>(a1) != 0)
        ListBox_Select(Field<int32_t>(box, 0x500) - 1, box);
    else
        ListBox_Select(Field<int32_t>(box, 0x508), box);
    return u(entry);
}

static uint32_t ListBox_AddEntry_Abi(uint32_t eax, uint32_t ebx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return ListBox_AddEntry(eax, ebx, a0, a1, a2);
}
ABI_ENTRY_0046c320(ListBox_AddEntry_Entry, ListBox_AddEntry_Abi)
HOOK(0x0046c320, "ListBox_AddEntry", ListBox_AddEntry_Entry, nullptr);

// 0x0046c4d0 (ecx = list box; stack: highlight (byte); ret 4; virtual, no direct callers): the base highlight setter
// 0x00465130 (ecx = object, sets/clears +0x474 bit 0x80; preserves ECX and EDX), then for every entry: highlight ?
// clear : set +0x474 bit 0x400. Not executed in recorded runs.
uint32_t ListBox_SetHighlight(uint8_t* box, uint32_t highlight)
{
    orig::fn_00465130(u(box), highlight);
    const int32_t count = Field<int32_t>(box, 0x500);
    for (int32_t i = 0; i < Field<int32_t>(box, 0x500); ++i)
    {
        const uint32_t e = Field<uint32_t*>(box, 0x50c)[i];
        if (e == 0)
            continue;
        if (static_cast<uint8_t>(highlight) == 0)
            Field<uint32_t>(P(e), 0x474) |= 0x400;
        else
            Field<uint32_t>(P(e), 0x474) &= 0xfffffbffu;
    }
    return static_cast<uint32_t>(count);
}

static uint32_t ListBox_SetHighlight_Abi(uint32_t ecx, uint32_t a0)
{
    return ListBox_SetHighlight(B(ecx), a0);
}
ABI_ENTRY_0046c4d0(ListBox_SetHighlight_Entry, ListBox_SetHighlight_Abi)
HOOK(0x0046c4d0, "ListBox_SetHighlight", ListBox_SetHighlight_Entry, nullptr);

// 0x0046c530 (ecx = list box, edi = entry): selects the entry: with an empty list ListBox_Select(0); otherwise
// ListBox_Select(index of entry) when found (tail jumps), else returns the count. Caller ListBox_Sort. Not executed in
// recorded runs.
uint32_t ListBox_SelectEntry(uint8_t* box, uint32_t entry)
{
    const int32_t count = Field<int32_t>(box, 0x500);
    if (count == 0)
        return ListBox_Select(0, box);
    int32_t i = 0;
    if (count <= 0)
        return 0;
    const uint32_t* entries = Field<uint32_t*>(box, 0x50c);
    do
    {
        if (entries[i] == entry)
            return ListBox_Select(i, box);
        ++i;
    } while (i < count);
    return static_cast<uint32_t>(i);
}

static uint32_t ListBox_SelectEntry_Abi(uint32_t ecx, uint32_t edi)
{
    return ListBox_SelectEntry(B(ecx), edi);
}
ABI_ENTRY_0046c530(ListBox_SelectEntry_Entry, ListBox_SelectEntry_Abi)
HOOK(0x0046c530, "ListBox_SelectEntry", ListBox_SelectEntry_Entry, nullptr);

// 0x0046c560 (eax = index, ecx = list box): selection. When the entries exist and 0 <= index < count: clears bit 0x80 of
// the old selection +0x508, stores index, sets bit 0x80 on the new entry, and scrolls it into view: ListBox_SetTop(index)
// when above the top +0x504, else ListBox_SetTop(top raised until index < top + rows). Returns its result, else the
// index.
uint32_t ListBox_Select(int32_t index, uint8_t* box)
{
    if (Field<uint32_t>(box, 0x50c) == 0 || index < 0 || index >= Field<int32_t>(box, 0x500))
        return static_cast<uint32_t>(index);
    const uint32_t old = Field<uint32_t*>(box, 0x50c)[Field<int32_t>(box, 0x508)];
    if (old != 0)
        Field<uint32_t>(P(old), 0x474) &= 0xffffff7fu;
    int32_t top = Field<int32_t>(box, 0x504);
    Field<int32_t>(box, 0x508) = index;
    Field<uint32_t>(P(Field<uint32_t*>(box, 0x50c)[index]), 0x474) |= 0x80;
    if (index < top)
        return ListBox_SetTop(index, box);
    if (index >= Field<int32_t>(box, 0x4fc) + top)
    {
        int32_t bottom = Field<int32_t>(box, 0x4fc) + top;
        do
        {
            ++top;
            ++bottom;
        } while (bottom <= index);
    }
    return ListBox_SetTop(top, box);
}

static uint32_t ListBox_Select_Abi(uint32_t eax, uint32_t ecx)
{
    return ListBox_Select(static_cast<int32_t>(eax), B(ecx));
}
ABI_ENTRY_0046c560(ListBox_Select_Entry, ListBox_Select_Abi)
HOOK(0x0046c560, "ListBox_Select", ListBox_Select_Entry, nullptr);

// 0x0046c5f0 (ecx = top, esi = list box): scrolls: top is clamped to [0, count - rows] (upper clamp first); when it
// grows, the down arrow glow +0x534 is raised to at least 1.0, when it shrinks the up arrow glow +0x51c. Stores +0x504,
// then shows the visible entries (top <= i < top + rows) at y = box +0x4ec - ((i - top) * 16 + 36) / 480 and calls
// their virtual +0x144 (layout), and hides the others (stops at a null entry). Returns al = scrolled. Frame: 8 bytes.
uint32_t ListBox_SetTop(int32_t top, uint8_t* box)
{
    uint8_t scrolled = 0;
    const int32_t last = Field<int32_t>(box, 0x500) - Field<int32_t>(box, 0x4fc);
    if (top >= last)
        top = last;
    if (top < 0)
        top = 0;
    const int32_t old = Field<int32_t>(box, 0x504);
    if (old < top)
    {
        scrolled = 1;
        if (Field<float>(box, 0x534) > K(0x005bcb2c))
            Field<float>(box, 0x534) = Field<float>(box, 0x534);
        else
            Field<float>(box, 0x534) = K(0x005bcb2c);
    }
    else if (old > top)
    {
        scrolled = 1;
        Field<float>(box, 0x51c) = Field<float>(box, 0x51c) > K(0x005bcb2c) ? Field<float>(box, 0x51c) : K(0x005bcb2c);
    }
    const int32_t bottom = Field<int32_t>(box, 0x4fc) + top;
    Field<int32_t>(box, 0x504) = top;
    for (int32_t i = 0; i < Field<int32_t>(box, 0x500); ++i)
    {
        const uint32_t e = Field<uint32_t*>(box, 0x50c)[i];
        if (e == 0)
            break;
        const int32_t first = Field<int32_t>(box, 0x504);
        if (i < first || i >= bottom)
        {
            Field<uint32_t>(P(e), 0x474) &= 0xfffffffbu;
            continue;
        }
        Field<float>(P(e), 0x4ec) =
            Field<float>(box, 0x4ec) - static_cast<float>((i - first) * 16 + 0x24) * K(0x005bcbf4);
        Field<uint32_t>(P(Field<uint32_t*>(box, 0x50c)[i]), 0x474) |= 4;
        VCall<uint32_t>(P(Field<uint32_t*>(box, 0x50c)[i]), 0x144 / 4);
    }
    return scrolled;
}

static uint32_t ListBox_SetTop_Abi(uint32_t ecx, uint32_t esi)
{
    return ListBox_SetTop(static_cast<int32_t>(ecx), B(esi));
}
ABI_ENTRY_0046c5f0(ListBox_SetTop_Entry, ListBox_SetTop_Abi)
HOOK(0x0046c5f0, "ListBox_SetTop", ListBox_SetTop_Entry, nullptr);

// 0x0046c740 (eax = index, ecx = list box): returns entry[index] when 0 <= index < count, else 0.
uint32_t ListBox_GetEntry(int32_t index, uint8_t* box)
{
    if (index < Field<int32_t>(box, 0x500) && index >= 0)
        return Field<uint32_t*>(box, 0x50c)[index];
    return 0;
}

static uint32_t ListBox_GetEntry_Abi(uint32_t eax, uint32_t ecx)
{
    return ListBox_GetEntry(static_cast<int32_t>(eax), B(ecx));
}
ABI_ENTRY_0046c740(ListBox_GetEntry_Entry, ListBox_GetEntry_Abi)
HOOK(0x0046c740, "ListBox_GetEntry", ListBox_GetEntry_Entry, nullptr);

// 0x0046c760 (eax = index, ecx = list box, edx = size, edi = char buffer; stack: prefer name (byte); ret 4): copies an
// entry's string as 8-bit text: its name +0x4a0 (strncpy 0x005382a0, terminated at size - 1) when preferred and
// non-empty, else the low bytes of its wide text +0x4c0 (wcslen 0x00538ea0; at most size - 1 characters). Returns al =
// 1, or al = 0 without an entry. Caller 0x0046c840. Not executed in recorded runs.
uint32_t ListBox_GetEntryTextA(int32_t index, uint8_t* box, uint32_t size, char* out, uint32_t preferName)
{
    const uint32_t e = ListBox_GetEntry(index, box);
    if (e == 0)
        return 0;
    if (static_cast<uint8_t>(preferName) != 0 && Field<char>(P(e), 0x4a0) != 0)
    {
        GameFunction<char*(__cdecl*)(char*, const char*, uint32_t)>(0x005382a0)(
            out, reinterpret_cast<const char*>(e + 0x4a0), size);
        out[size - 1] = 0;
        return 1;
    }
    const wchar_t* s = Field<const wchar_t*>(P(e), 0x4c0);
    const uint32_t n = GameFunction<uint32_t(__cdecl*)(const wchar_t*)>(0x00538ea0)(s);
    uint32_t i = 0;
    uint32_t left = size;
    if (n != 0)
    {
        do
        {
            if (--left == 0)
                break;
            out[i] = static_cast<char>(s[i]);
            ++i;
        } while (i < n);
    }
    out[i] = 0;
    return 1;
}

static uint32_t ListBox_GetEntryTextA_Abi(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t edi, uint32_t a0)
{
    return ListBox_GetEntryTextA(static_cast<int32_t>(eax), B(ecx), edx, reinterpret_cast<char*>(edi), a0);
}
ABI_ENTRY_0046c760(ListBox_GetEntryTextA_Entry, ListBox_GetEntryTextA_Abi)
HOOK(0x0046c760, "ListBox_GetEntryTextA", ListBox_GetEntryTextA_Entry, nullptr);

// 0x0046c7e0 (eax = index, ecx = list box, ebx = wide buffer, esi = size; stack: prefer name (byte); ret 4): as
// 0x0046c760 for a wide buffer: the name is widened with 0x004e59d0 (eax = size, ecx = buffer, edi = name; then
// buffer[size - 1] = 0), else the text is copied with 0x00403c10 (eax = text, ecx = size, edx = buffer). Returns al = 1
// or 0. Caller 0x0046c850. Not executed in recorded runs.
uint32_t ListBox_GetEntryTextW(int32_t index, uint8_t* box, wchar_t* out, uint32_t size, uint32_t preferName)
{
    const uint32_t e = ListBox_GetEntry(index, box);
    if (e == 0)
        return 0;
    if (static_cast<uint8_t>(preferName) != 0 && Field<char>(P(e), 0x4a0) != 0)
    {
        const uint32_t r = orig::fn_004e59d0(size, u(out), e + 0x4a0);
        out[size - 1] = 0;
        return (r & 0xffffff00u) | 1u;
    }
    const uint32_t r = orig::fn_00403c10(Field<uint32_t>(P(e), 0x4c0), size, u(out));
    return (r & 0xffffff00u) | 1u;
}

static uint32_t ListBox_GetEntryTextW_Abi(uint32_t eax, uint32_t ecx, uint32_t ebx, uint32_t esi, uint32_t a0)
{
    return ListBox_GetEntryTextW(static_cast<int32_t>(eax), B(ecx), reinterpret_cast<wchar_t*>(ebx), esi, a0);
}
ABI_ENTRY_0046c7e0(ListBox_GetEntryTextW_Entry, ListBox_GetEntryTextW_Abi)
HOOK(0x0046c7e0, "ListBox_GetEntryTextW", ListBox_GetEntryTextW_Entry, nullptr);

// 0x0046c840 (eax = prefer name, ecx = list box, edx = size, edi = char buffer): ListBox_GetEntryTextA of the
// selection +0x508. Caller 0x004921a0. Not executed in recorded runs.
uint32_t ListBox_GetSelectedTextA(uint32_t preferName, uint8_t* box, uint32_t size, char* out)
{
    return ListBox_GetEntryTextA(Field<int32_t>(box, 0x508), box, size, out, preferName);
}

static uint32_t ListBox_GetSelectedTextA_Abi(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t edi)
{
    return ListBox_GetSelectedTextA(eax, B(ecx), edx, reinterpret_cast<char*>(edi));
}
ABI_ENTRY_0046c840(ListBox_GetSelectedTextA_Entry, ListBox_GetSelectedTextA_Abi)
HOOK(0x0046c840, "ListBox_GetSelectedTextA", ListBox_GetSelectedTextA_Entry, nullptr);

// 0x0046c850 (eax = prefer name, ecx = list box, ebx = wide buffer, esi = size): ListBox_GetEntryTextW of the
// selection. Caller 0x004921a0. Not executed in recorded runs.
uint32_t ListBox_GetSelectedTextW(uint32_t preferName, uint8_t* box, wchar_t* out, uint32_t size)
{
    return ListBox_GetEntryTextW(Field<int32_t>(box, 0x508), box, out, size, preferName);
}

static uint32_t ListBox_GetSelectedTextW_Abi(uint32_t eax, uint32_t ecx, uint32_t ebx, uint32_t esi)
{
    return ListBox_GetSelectedTextW(eax, B(ecx), reinterpret_cast<wchar_t*>(ebx), esi);
}
ABI_ENTRY_0046c850(ListBox_GetSelectedTextW_Entry, ListBox_GetSelectedTextW_Abi)
HOOK(0x0046c850, "ListBox_GetSelectedTextW", ListBox_GetSelectedTextW_Entry, nullptr);

// 0x0046c860 (ecx = list box; stack: input, key flags pointer; ret 8; virtual, no direct callers): input. With an input,
// non-zero flags and entries: bit 1 (up) selects the previous entry, bit 2 (down) the next one (returning al = 1),
// unless already at the end; bit 0x10 (click), when 0x00414b20 (edi = g+0xa80; stack: 0x10004, 0.5) and the pointer
// position 0x00491360 (eax = g+0xab4, edx = &x (the 4-byte frame slot); stack: &y (the input argument's slot)) succeed:
// ListBox_HitEntry(x, y, 1) hit -> al = 0; otherwise clears bit 0x10 and scrolls by ListBox_HitArrows(x, y)
// (ListBox_SetTop(top + d)), al = scrolled. Everything else goes to the base handler 0x00464a10 (ecx = box; input,
// flags). Not executed in recorded runs.
uint32_t ListBox_HandleInput(uint8_t* box, uint32_t input, uint32_t* flags)
{
    uint32_t x;
    uint32_t y = input;
    if (input != 0 && *flags != 0 && Field<int32_t>(box, 0x500) != 0)
    {
        const uint32_t keys = *flags;
        bool tryClick = true;
        if ((keys & 1) != 0)
        {
            if (Field<int32_t>(box, 0x508) != 0)
            {
                ListBox_Select(Field<int32_t>(box, 0x508) - 1, box);
                return 1;
            }
        }
        else if ((keys & 2) != 0)
        {
            if (Field<int32_t>(box, 0x508) != Field<int32_t>(box, 0x500) - 1)
            {
                ListBox_Select(Field<int32_t>(box, 0x508) + 1, box);
                return 1;
            }
        }
        if (tryClick && (keys & 0x10) != 0 &&
            static_cast<uint8_t>(orig::fn_00414b20(Field<uint32_t>(G(), 0xa80), 0x10004, 0x3f000000u)) != 0 &&
            static_cast<uint8_t>(orig::fn_00491360(Field<uint32_t>(G(), 0xab4), u(&x), u(&y))) != 0)
        {
            if (ListBox_HitEntry(box, x, y, 1) != -1)
                return 0;
            *flags &= 0xffffffefu;
            const int32_t d = ListBox_HitArrows(box, bf(x), bf(y));
            const uint32_t r = ListBox_SetTop(d + Field<int32_t>(box, 0x504), box);
            return static_cast<uint8_t>(r) != 0 ? 1u : 0u;
        }
    }
    return orig::fn_00464a10(u(box), input, u(flags));
}

static uint32_t ListBox_HandleInput_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return ListBox_HandleInput(B(ecx), a0, reinterpret_cast<uint32_t*>(a1));
}
ABI_ENTRY_0046c860(ListBox_HandleInput_Entry, ListBox_HandleInput_Abi)
HOOK(0x0046c860, "ListBox_HandleInput", ListBox_HandleInput_Entry, nullptr);

// 0x0046c980 (eax = rows, edi = list box): sets the row count: marks the layout dirty, +0x4fc = rows, footer +0x514 =
// (-3 - rows) * 16, header +0x510 = -8, the up arrow (Ui_ArrowInit at y = -8, down = 1, +0x518) and the down arrow (y =
// +0x514, +0x530), ListBox_SetTop(top), then tail-jumps to the box's virtual +0x144 (layout; recovered as call and
// return: the jump has no stack arguments). Returns its EAX.
uint32_t ListBox_SetRows(uint32_t rows, uint8_t* box)
{
    Field<uint32_t>(box, 0x474) |= 0x2000;
    Field<uint32_t>(box, 0x4fc) = rows;
    Field<int32_t>(box, 0x514) = static_cast<int32_t>((0xfffffffdu - rows) << 4);
    Field<int32_t>(box, 0x510) = -8;
    Ui_ArrowInit(box, 1, -8, box + 0x518, 0);
    Ui_ArrowInit(box, 0, Field<int32_t>(box, 0x514), box + 0x530, 0);
    ListBox_SetTop(Field<int32_t>(box, 0x504), box);
    return VCall<uint32_t>(box, 0x144 / 4);
}

static uint32_t ListBox_SetRows_Abi(uint32_t eax, uint32_t edi)
{
    return ListBox_SetRows(eax, B(edi));
}
// clang-format off
// Hand-written entry stub: the generator emits none for 0x0046c980 ("no ret found": it ends in a tail jump).
static __declspec(naked) void ListBox_SetRows_Entry()
{
    __asm push ecx __asm push edx __asm push ebx __asm push esi __asm push edi __asm push ebp __asm push edi
    __asm push eax __asm call ListBox_SetRows_Abi __asm add esp, 8 __asm pop ebp __asm pop edi __asm pop esi
    __asm pop ebx __asm pop edx __asm pop ecx __asm ret
}
// clang-format on
HOOK(0x0046c980, "ListBox_SetRows", ListBox_SetRows_Entry, nullptr);

// ================================================================================================================
// Slider widget (+0x4f8 step, +0x4fc min, +0x500 max, +0x504 value, +0x508 width in 640 units)
// ================================================================================================================

// 0x0046c9f0 (ecx = slider; stack: 4 args; ret 0x10; virtual, no direct callers): 0x004651a0 (base, same arguments),
// then step 1, min 0, max 50, value 25, width 128. Not executed in recorded runs.
uint32_t Slider_Init(uint8_t* slider, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    orig::fn_004651a0(u(slider), a0, a1, a2, a3);
    Field<uint32_t>(slider, 0x4f8) = 1;
    Field<uint32_t>(slider, 0x4fc) = 0;
    Field<uint32_t>(slider, 0x500) = 0x32;
    Field<uint32_t>(slider, 0x504) = 0x19;
    Field<uint32_t>(slider, 0x508) = 0x80;
    return 0;
}

static uint32_t Slider_Init_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return Slider_Init(B(ecx), a0, a1, a2, a3);
}
ABI_ENTRY_0046c9f0(Slider_Init_Entry, Slider_Init_Abi)
HOOK(0x0046c9f0, "Slider_Init", Slider_Init_Entry, nullptr);

// 0x0046ca50 (no inputs; virtual, no direct callers): returns 10 (guess: the slider's type id; the list box returns 6,
// 0x0046bec0). Not executed in recorded runs.
uint32_t fn_0046ca50()
{
    return 10;
}

static uint32_t fn_0046ca50_Abi()
{
    return fn_0046ca50();
}
ABI_ENTRY_0046ca50(fn_0046ca50_Entry, fn_0046ca50_Abi)
HOOK(0x0046ca50, "fn_0046ca50", fn_0046ca50_Entry, nullptr);

// 0x0046ca60 (ecx = slider; stack: input, key flags pointer; ret 8; virtual, no direct callers): with an input and
// flags: bit 8 (right) adds the step (clamped to max), bit 4 (left) subtracts it (clamped to min), returning al = 1;
// otherwise the base handler 0x00464a10. Not executed in recorded runs.
uint32_t Slider_HandleInput(uint8_t* slider, uint32_t input, uint32_t* flags)
{
    if (input != 0 && *flags != 0)
    {
        const uint32_t keys = *flags;
        if ((keys & 8) != 0)
        {
            const int32_t v = Field<int32_t>(slider, 0x504) + Field<int32_t>(slider, 0x4f8);
            const int32_t max = Field<int32_t>(slider, 0x500);
            Field<int32_t>(slider, 0x504) = v;
            if (v > max)
                Field<int32_t>(slider, 0x504) = max;
            return (static_cast<uint32_t>(max) & 0xffffff00u) | 1u;
        }
        if ((keys & 4) != 0)
        {
            const int32_t v = Field<int32_t>(slider, 0x504) - Field<int32_t>(slider, 0x4f8);
            Field<int32_t>(slider, 0x504) = v;
            const int32_t min = Field<int32_t>(slider, 0x4fc);
            if (v < min)
                Field<int32_t>(slider, 0x504) = min;
            return (static_cast<uint32_t>(min) & 0xffffff00u) | 1u;
        }
    }
    return orig::fn_00464a10(u(slider), input, u(flags));
}

static uint32_t Slider_HandleInput_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return Slider_HandleInput(B(ecx), a0, reinterpret_cast<uint32_t*>(a1));
}
ABI_ENTRY_0046ca60(Slider_HandleInput_Entry, Slider_HandleInput_Abi)
HOOK(0x0046ca60, "Slider_HandleInput", Slider_HandleInput_Entry, nullptr);

// 0x0046cae0 (ecx = slider; virtual, no direct callers): draws the track Ui_DrawPixelRect((64, 4) - (64 + width, -4),
// esi = 0; 0, colour 0x0064a31c, 0) and the knob at pos = (value - min) * width / (max - min) (signed division):
// ((pos + 60, 8) - (pos + 68, -8); colours 0x0064a320, 0x0064a324, 0), then tail-jumps to UiText_Render (ecx = slider;
// edi is the caller's, which UiText_Render passes through unused: the slider is passed). Not executed in recorded runs.
uint32_t Slider_Render(uint8_t* slider)
{
    Ui_DrawPixelRect(slider, 4, 0x40, 0, Field<int32_t>(slider, 0x508) + 0x40, -4, 0, GameGlobal<uint32_t>(0x0064a31c),
                     0);
    const int32_t min = Field<int32_t>(slider, 0x4fc);
    const int32_t pos =
        (Field<int32_t>(slider, 0x504) - min) * Field<int32_t>(slider, 0x508) / (Field<int32_t>(slider, 0x500) - min) +
        0x40;
    Ui_DrawPixelRect(slider, 8, pos - 4, 0, pos + 4, -8, GameGlobal<uint32_t>(0x0064a320),
                     GameGlobal<uint32_t>(0x0064a324), 0);
    return orig::fn_00465360(u(slider), u(slider));
}

static uint32_t Slider_Render_Abi(uint32_t ecx)
{
    return Slider_Render(B(ecx));
}
ABI_ENTRY_0046cae0(Slider_Render_Entry, Slider_Render_Abi)
HOOK(0x0046cae0, "Slider_Render", Slider_Render_Entry, nullptr);

// ================================================================================================================
// Check box widget (+0x4f8 checked byte, +0x4fc bound bool pointer)
// ================================================================================================================

// 0x0046cb60 (eax = widget, esi = bool pointer; stack: name, enabled (byte); caller pops): when the widget's virtual
// +0x150 (type flags) has bit 0x10: UiText_SetName(name, widget, lookup 1), binds +0x4fc = pointer and copies its
// value to +0x4f8, and sets/clears +0x474 bit 1 by enabled. Returns the new flags (or the virtual's result). Callers
// 0x004710d0, 0x00491b70.
uint32_t CheckBox_Bind(uint8_t* widget, uint8_t* value, uint32_t name, uint32_t enabled)
{
    const uint32_t kind = VCall<uint32_t>(widget, 0x150 / 4);
    if ((kind & 0x10) == 0)
        return kind;
    UiText_SetName(reinterpret_cast<const char*>(name), widget, 1);
    Field<uint32_t>(widget, 0x4fc) = u(value);
    if (value != nullptr)
        Field<uint8_t>(widget, 0x4f8) = *value;
    uint32_t flags = Field<uint32_t>(widget, 0x474);
    if (static_cast<uint8_t>(enabled) != 0)
        flags |= 1;
    else
        flags &= 0xfffffffeu;
    Field<uint32_t>(widget, 0x474) = flags;
    return flags;
}

static uint32_t CheckBox_Bind_Abi(uint32_t eax, uint32_t esi, uint32_t a0, uint32_t a1)
{
    return CheckBox_Bind(B(eax), B(esi), a0, a1);
}
ABI_ENTRY_0046cb60(CheckBox_Bind_Entry, CheckBox_Bind_Abi)
HOOK(0x0046cb60, "CheckBox_Bind", CheckBox_Bind_Entry, nullptr);

// 0x0046cbc0 (ecx = check box; stack: 4 args; ret 0x10; virtual, no direct callers): 0x004651a0 (base), then checked
// = 0 and no binding. Not executed in recorded runs.
uint32_t CheckBox_Init(uint8_t* widget, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    orig::fn_004651a0(u(widget), a0, a1, a2, a3);
    Field<uint8_t>(widget, 0x4f8) = 0;
    Field<uint32_t>(widget, 0x4fc) = 0;
    return 0;
}

static uint32_t CheckBox_Init_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return CheckBox_Init(B(ecx), a0, a1, a2, a3);
}
ABI_ENTRY_0046cbc0(CheckBox_Init_Entry, CheckBox_Init_Abi)
HOOK(0x0046cbc0, "CheckBox_Init", CheckBox_Init_Entry, nullptr);

// 0x0046cbf0 (ecx = check box; stack: input, key flags pointer; ret 8; virtual, no direct callers): on bit 0x10
// (activate) toggles checked and writes it through the binding; returns al = 0. Otherwise the base handler 0x00464a10.
// Not executed in recorded runs.
uint32_t CheckBox_HandleInput(uint8_t* widget, uint32_t input, uint32_t* flags)
{
    if (input != 0 && *flags != 0 && (*flags & 0x10) != 0)
    {
        const uint8_t checked = Field<uint8_t>(widget, 0x4f8) == 0 ? 1 : 0;
        Field<uint8_t>(widget, 0x4f8) = checked;
        uint8_t* bound = Field<uint8_t*>(widget, 0x4fc);
        if (bound != nullptr)
            *bound = checked;
        return *flags & 0xffffff00u;
    }
    return orig::fn_00464a10(u(widget), input, u(flags));
}

static uint32_t CheckBox_HandleInput_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return CheckBox_HandleInput(B(ecx), a0, reinterpret_cast<uint32_t*>(a1));
}
ABI_ENTRY_0046cbf0(CheckBox_HandleInput_Entry, CheckBox_HandleInput_Abi)
HOOK(0x0046cbf0, "CheckBox_HandleInput", CheckBox_HandleInput_Entry, nullptr);

// 0x0046cc40 (ecx = check box; virtual, no direct callers): draws the box Ui_DrawPixelRect((-128, 8) - (-112, -8),
// esi = 0; fill = checked ? [0x0064a328] : 0, border [0x0064a32c], 0), then tail-jumps to UiText_Render (edi passed
// through unused; the widget is passed). Not executed in recorded runs.
uint32_t CheckBox_Render(uint8_t* widget)
{
    const uint32_t fill = Field<uint8_t>(widget, 0x4f8) != 0 ? GameGlobal<uint32_t>(0x0064a328) : 0u;
    Ui_DrawPixelRect(widget, 8, -0x80, 0, -0x70, -8, fill, GameGlobal<uint32_t>(0x0064a32c), 0);
    return orig::fn_00465360(u(widget), u(widget));
}

static uint32_t CheckBox_Render_Abi(uint32_t ecx)
{
    return CheckBox_Render(B(ecx));
}
ABI_ENTRY_0046cc40(CheckBox_Render_Entry, CheckBox_Render_Abi)
HOOK(0x0046cc40, "CheckBox_Render", CheckBox_Render_Entry, nullptr);

// 0x0046cc80 (ecx = check box; virtual, no direct callers): UiText_FitToBox(192, 0), UiText_Layout, then resets the
// bounds (UiText_ExtendBounds, al = 0) to the box (-128, 8) - (-112, -8) (Ui_PixelToScreen). Frame 0x10 bytes. Not
// executed in recorded runs.
uint32_t CheckBox_Layout(uint8_t* widget)
{
    float x0, y0, x1, y1;
    UiText_FitToBox(widget, bf(0x43400000u), 0.0f);
    UiText_Layout(widget);
    Ui_PixelToScreen(widget, &x0, &y0, -0x80, 8);
    Ui_PixelToScreen(widget, &x1, &y1, -0x70, -8);
    return UiText_ExtendBounds(0, widget, x0, y0, x1, y1);
}

static uint32_t CheckBox_Layout_Abi(uint32_t ecx)
{
    return CheckBox_Layout(B(ecx));
}
ABI_ENTRY_0046cc80(CheckBox_Layout_Entry, CheckBox_Layout_Abi)
HOOK(0x0046cc80, "CheckBox_Layout", CheckBox_Layout_Entry, nullptr);

// ================================================================================================================
// Stepped value widget (guess: option spinner). +0x4f8 wide value label (0x40 characters), +0x578..+0x580 colours,
// +0x584 step count (positions - 1), +0x588 min, +0x58c max, +0x590 step size, +0x594 1 / steps, +0x598 value,
// +0x59c normalised position, +0x5a0 index, +0x5a4 change callback (cdecl (widget, user)), +0x5a8 user data, +0x5ac
// byte flag.
// ================================================================================================================

uint32_t Stepper_SetLabelA(uint8_t* widget, uint32_t text);
uint32_t Stepper_SetCallback(uint8_t* widget, uint32_t callback, uint32_t user, uint32_t notify);
uint32_t Stepper_SetValue(uint8_t* widget, float value, uint32_t notify);
uint32_t Stepper_SetRange(uint8_t* widget, float min, float max, uint32_t notify);
uint32_t Stepper_SetSteps(uint8_t* widget, uint32_t positions, uint32_t notify);
uint32_t Stepper_SetIndex(uint8_t* widget, uint32_t ecx, uint32_t index, uint32_t notify);

// 0x0046cce0 (eax = widget, ecx = name; stack: positions, flag (byte), callback, user, min, max, value; caller pops):
// configures a value widget: UiText_SetName(name, lookup 1), empty label (Stepper_SetLabelA 0x0058a9a4), +0x5ac = flag,
// Stepper_SetCallback(callback, user, 0), Stepper_SetSteps(positions, 0), Stepper_SetRange(min, max, 0),
// Stepper_SetValue(value, 1). Returns the widget. Callers 0x004710d0, 0x00491b70.
uint32_t Stepper_Setup(uint8_t* widget, uint32_t name, uint32_t positions, uint32_t flag, uint32_t callback,
                       uint32_t user, uint32_t min, uint32_t max, uint32_t value)
{
    UiText_SetName(reinterpret_cast<const char*>(name), widget, 1);
    Stepper_SetLabelA(widget, 0x0058a9a4u);
    Field<uint8_t>(widget, 0x5ac) = static_cast<uint8_t>(flag);
    Stepper_SetCallback(widget, callback, user, 0);
    Stepper_SetSteps(widget, positions, 0);
    Stepper_SetRange(widget, bf(min), bf(max), 0);
    Stepper_SetValue(widget, bf(value), 1);
    return u(widget);
}

static uint32_t Stepper_Setup_Abi(uint32_t eax, uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3,
                                  uint32_t a4, uint32_t a5, uint32_t a6)
{
    return Stepper_Setup(B(eax), ecx, a0, a1, a2, a3, a4, a5, a6);
}
ABI_ENTRY_0046cce0(Stepper_Setup_Entry, Stepper_Setup_Abi)
HOOK(0x0046cce0, "Stepper_Setup", Stepper_Setup_Entry, nullptr);

// 0x0046cd50 (eax = widget, ecx = name; stack: positions, flag, callback, user, index; caller pops): as 0x0046cce0 for
// an index-only widget: range (0, 0) and Stepper_SetIndex(index, notify 1) (ECX at that call is a leftover of
// Stepper_SetRange; SetIndex does not read it). Returns the widget. Callers 0x00470d70, 0x004710d0. Not executed in
// recorded runs.
uint32_t Stepper_SetupIndex(uint8_t* widget, uint32_t name, uint32_t positions, uint32_t flag, uint32_t callback,
                            uint32_t user, uint32_t index)
{
    UiText_SetName(reinterpret_cast<const char*>(name), widget, 1);
    Stepper_SetLabelA(widget, 0x0058a9a4u);
    Field<uint8_t>(widget, 0x5ac) = static_cast<uint8_t>(flag);
    Stepper_SetCallback(widget, callback, user, 0);
    Stepper_SetSteps(widget, positions, 0);
    Stepper_SetRange(widget, 0.0f, 0.0f, 0);
    Stepper_SetIndex(widget, 0, index, 1);
    return u(widget);
}

static uint32_t Stepper_SetupIndex_Abi(uint32_t eax, uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3,
                                       uint32_t a4)
{
    return Stepper_SetupIndex(B(eax), ecx, a0, a1, a2, a3, a4);
}
ABI_ENTRY_0046cd50(Stepper_SetupIndex_Entry, Stepper_SetupIndex_Abi)
HOOK(0x0046cd50, "Stepper_SetupIndex", Stepper_SetupIndex_Entry, nullptr);

// 0x0046cdb0 (ecx = widget; stack: 4 args; ret 0x10; virtual, no direct callers): 0x004651a0 (base), then clears the
// label's first character, index, min 0, max 1.0, step 1.0, position, value, flag, callback; Stepper_SetSteps(1001, 0);
// colours +0x578 = +0x580 = 0xff204000, +0x57c = 0xff40c040. Not executed in recorded runs.
uint32_t Stepper_Init(uint8_t* widget, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    orig::fn_004651a0(u(widget), a0, a1, a2, a3);
    Field<uint16_t>(widget, 0x4f8) = 0;
    Field<uint32_t>(widget, 0x5a0) = 0;
    Field<uint32_t>(widget, 0x588) = 0;
    Field<uint32_t>(widget, 0x58c) = 0x3f800000u;
    Field<uint32_t>(widget, 0x590) = 0x3f800000u;
    Field<uint32_t>(widget, 0x59c) = 0;
    Field<uint32_t>(widget, 0x598) = 0;
    Field<uint8_t>(widget, 0x5ac) = 0;
    Field<uint32_t>(widget, 0x5a4) = 0;
    Stepper_SetSteps(widget, 0x3e9, 0);
    Field<uint32_t>(widget, 0x578) = 0xff204000u;
    Field<uint32_t>(widget, 0x57c) = 0xff40c040u;
    Field<uint32_t>(widget, 0x580) = 0xff204000u;
    return 0xff204000u;
}

static uint32_t Stepper_Init_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return Stepper_Init(B(ecx), a0, a1, a2, a3);
}
ABI_ENTRY_0046cdb0(Stepper_Init_Entry, Stepper_Init_Abi)
HOOK(0x0046cdb0, "Stepper_Init", Stepper_Init_Entry, nullptr);

// 0x0046ce40 (no inputs; virtual, no direct callers): returns 0x22 (guess: the widget's type flags; CheckBox_Bind tests
// bit 0x10 of the same virtual slot). Not executed in recorded runs.
uint32_t fn_0046ce40()
{
    return 0x22;
}

static uint32_t fn_0046ce40_Abi()
{
    return fn_0046ce40();
}
ABI_ENTRY_0046ce40(fn_0046ce40_Entry, fn_0046ce40_Abi)
HOOK(0x0046ce40, "fn_0046ce40", fn_0046ce40_Entry, nullptr);

uint32_t Stepper_Notify(uint8_t* widget);

// 0x0046ce50 (eax = widget, ecx = callback, edx = user data; stack: notify (byte); ret 4): stores the change callback
// +0x5a4 and its user data +0x5a8; with notify calls it (Stepper_Notify). Returns EAX (the widget or the callback's
// result).
uint32_t Stepper_SetCallback(uint8_t* widget, uint32_t callback, uint32_t user, uint32_t notify)
{
    Field<uint32_t>(widget, 0x5a4) = callback;
    Field<uint32_t>(widget, 0x5a8) = user;
    if (static_cast<uint8_t>(notify) != 0)
        return Stepper_Notify(widget);
    return u(widget);
}

static uint32_t Stepper_SetCallback_Abi(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t a0)
{
    return Stepper_SetCallback(B(eax), ecx, edx, a0);
}
ABI_ENTRY_0046ce50(Stepper_SetCallback_Entry, Stepper_SetCallback_Abi)
HOOK(0x0046ce50, "Stepper_SetCallback", Stepper_SetCallback_Entry, nullptr);

// 0x0046ce70 (esi = widget; stack: position (0..1 float), notify; ret 8): index = ftol((1/steps * 0.5 + position) /
// (1/steps)), or 0 when that is negative; Stepper_SetIndex(index, notify) (ecx = notify, as the original). Callers
// 0x0046d290, 0x00494160. Not executed in recorded runs.
uint32_t Stepper_SetPosition(uint8_t* widget, float position, uint32_t notify)
{
    const float t = (Field<float>(widget, 0x594) * K(0x005bcb4c) + position) / Field<float>(widget, 0x594);
    if (t < K(0x0056c05c))
        return Stepper_SetIndex(widget, notify, 0, notify);
    return Stepper_SetIndex(widget, notify, static_cast<uint32_t>(Ftol(t)), notify);
}

static uint32_t Stepper_SetPosition_Abi(uint32_t esi, uint32_t a0, uint32_t a1)
{
    return Stepper_SetPosition(B(esi), bf(a0), a1);
}
ABI_ENTRY_0046ce70(Stepper_SetPosition_Entry, Stepper_SetPosition_Abi)
HOOK(0x0046ce70, "Stepper_SetPosition", Stepper_SetPosition_Entry, nullptr);

// 0x0046cec0 (esi = widget; stack: value, notify; ret 8): index = ftol(((value - min) + step * 0.5) / step), or 0 when
// negative; Stepper_SetIndex(index, notify). Caller Stepper_Setup.
uint32_t Stepper_SetValue(uint8_t* widget, float value, uint32_t notify)
{
    const float t = ((value - Field<float>(widget, 0x588)) + Field<float>(widget, 0x590) * K(0x005bcb4c)) /
                    Field<float>(widget, 0x590);
    if (t < K(0x0056c05c))
        return Stepper_SetIndex(widget, notify, 0, notify);
    return Stepper_SetIndex(widget, notify, static_cast<uint32_t>(Ftol(t)), notify);
}

static uint32_t Stepper_SetValue_Abi(uint32_t esi, uint32_t a0, uint32_t a1)
{
    return Stepper_SetValue(B(esi), bf(a0), a1);
}
ABI_ENTRY_0046cec0(Stepper_SetValue_Entry, Stepper_SetValue_Abi)
HOOK(0x0046cec0, "Stepper_SetValue", Stepper_SetValue_Entry, nullptr);

// 0x0046cf20 (eax = widget; stack: min, max, notify; ret 0xc): +0x588 = min, +0x58c = max, step +0x590 = (max - min) /
// (float)(unsigned)steps, then Stepper_SetIndex(current index, notify) (ecx = steps, as the original).
uint32_t Stepper_SetRange(uint8_t* widget, float min, float max, uint32_t notify)
{
    const float span = max - min;
    Field<float>(widget, 0x588) = min;
    Field<float>(widget, 0x58c) = max;
    Field<float>(widget, 0x590) = span / static_cast<float>(Field<uint32_t>(widget, 0x584));
    return Stepper_SetIndex(widget, Field<uint32_t>(widget, 0x584), Field<uint32_t>(widget, 0x5a0), notify);
}

static uint32_t Stepper_SetRange_Abi(uint32_t eax, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return Stepper_SetRange(B(eax), bf(a0), bf(a1), a2);
}
ABI_ENTRY_0046cf20(Stepper_SetRange_Entry, Stepper_SetRange_Abi)
HOOK(0x0046cf20, "Stepper_SetRange", Stepper_SetRange_Entry, nullptr);

// 0x0046cf70 (eax = widget, ecx = positions; stack: notify; ret 4): steps +0x584 = max(positions, 2) - 1 (unsigned),
// step size = (max - min) / steps, +0x594 = 1 / steps, then tail-jumps to Stepper_SetIndex(current index, notify) with
// its own argument frame (recovered as call and return).
uint32_t Stepper_SetSteps(uint8_t* widget, uint32_t positions, uint32_t notify)
{
    if (positions <= 2)
        positions = 2;
    const uint32_t steps = positions - 1;
    const float f = static_cast<float>(steps);
    Field<uint32_t>(widget, 0x584) = steps;
    Field<float>(widget, 0x590) = (Field<float>(widget, 0x58c) - Field<float>(widget, 0x588)) / f;
    Field<float>(widget, 0x594) = K(0x005bcb2c) / f;
    return Stepper_SetIndex(widget, steps, Field<uint32_t>(widget, 0x5a0), notify);
}

static uint32_t Stepper_SetSteps_Abi(uint32_t eax, uint32_t ecx, uint32_t a0)
{
    return Stepper_SetSteps(B(eax), ecx, a0);
}
ABI_ENTRY_0046cf70(Stepper_SetSteps_Entry, Stepper_SetSteps_Abi)
HOOK(0x0046cf70, "Stepper_SetSteps", Stepper_SetSteps_Entry, nullptr);

// 0x0046cfd0 (eax = widget, ecx = scratch (saved, not read), edx = index; stack: notify (byte); ret 4): index =
// min(index, steps) (unsigned), +0x5a0 = index, position +0x59c = index / steps, value +0x598 = index * step + min;
// with notify, Stepper_Notify. Returns EAX (the widget or the callback's result).
uint32_t Stepper_SetIndex(uint8_t* widget, uint32_t /*ecx*/, uint32_t index, uint32_t notify)
{
    uint32_t steps = Field<uint32_t>(widget, 0x584);
    if (index < steps)
        steps = index;
    const float f = static_cast<float>(steps);
    Field<uint32_t>(widget, 0x5a0) = steps;
    Field<float>(widget, 0x59c) = f * Field<float>(widget, 0x594);
    Field<float>(widget, 0x598) = f * Field<float>(widget, 0x590) + Field<float>(widget, 0x588);
    if (static_cast<uint8_t>(notify) != 0)
        return Stepper_Notify(widget);
    return u(widget);
}

static uint32_t Stepper_SetIndex_Abi(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t a0)
{
    return Stepper_SetIndex(B(eax), ecx, edx, a0);
}
ABI_ENTRY_0046cfd0(Stepper_SetIndex_Entry, Stepper_SetIndex_Abi)
HOOK(0x0046cfd0, "Stepper_SetIndex", Stepper_SetIndex_Entry, nullptr);

// 0x0046d030 (eax = widget): calls the change callback +0x5a4 (cdecl: widget, user data +0x5a8) when set. Returns its
// result, else the widget.
uint32_t Stepper_Notify(uint8_t* widget)
{
    const uint32_t callback = Field<uint32_t>(widget, 0x5a4);
    if (callback == 0)
        return u(widget);
    return reinterpret_cast<uint32_t(__cdecl*)(uint8_t*, uint32_t)>(callback)(widget, Field<uint32_t>(widget, 0x5a8));
}

static uint32_t Stepper_Notify_Abi(uint32_t eax)
{
    return Stepper_Notify(B(eax));
}
ABI_ENTRY_0046d030(Stepper_Notify_Entry, Stepper_Notify_Abi)
HOOK(0x0046d030, "Stepper_Notify", Stepper_Notify_Entry, nullptr);

// 0x0046d050 (ecx = widget; virtual, no direct callers): UiText_FitToBox(144, 0), UiText_Layout, measures the value
// label +0x4f8 (0x00495fa0, font g+0xac0, scale +0x4c8; width/height into the 0x10-byte frame) and adds its bounds
// (UiText_SetBounds(reset 0)) at (288, 0) (Ui_PixelToScreen) with alignment (0, +0x4f4). Not executed in recorded runs.
uint32_t Stepper_Layout(uint8_t* widget)
{
    float size[2];  // S+0 = height, S+4 = width
    float pos[2];   // S+8 = y, S+0xc = x
    UiText_FitToBox(widget, bf(0x43100000u), 0.0f);
    UiText_Layout(widget);
    orig::fn_00495fa0(u(&size[1]), u(&size[0]), Field<uint32_t>(G(), 0xac0), u(widget + 0x4f8),
                      Field<uint32_t>(widget, 0x4c8));
    Ui_PixelToScreen(widget, &pos[1], &pos[0], 0x120, 0);
    return UiText_SetBounds(widget, pos[1], pos[0], 0.0f, Field<float>(widget, 0x4f4), size[1], size[0], 0);
}

static uint32_t Stepper_Layout_Abi(uint32_t ecx)
{
    return Stepper_Layout(B(ecx));
}
ABI_ENTRY_0046d050(Stepper_Layout_Entry, Stepper_Layout_Abi)
HOOK(0x0046d050, "Stepper_Layout", Stepper_Layout_Entry, nullptr);

// 0x0046d0e0 (ebx = widget, esi = wide text): when wcslen(text) < 0x40, copies it to the label +0x4f8 with 0x00403be0
// (eax = text, edx = label, edi = 0); always marks the layout dirty. Not executed in recorded runs.
uint32_t Stepper_SetLabelW(uint8_t* widget, const wchar_t* text)
{
    const uint32_t n = GameFunction<uint32_t(__cdecl*)(const wchar_t*)>(0x00538ea0)(text);
    uint32_t r = n;
    if (n < 0x40)
        r = orig::fn_00403be0(u(text), u(widget + 0x4f8), 0);
    Field<uint32_t>(widget, 0x474) |= 0x2000;
    return r;
}

static uint32_t Stepper_SetLabelW_Abi(uint32_t ebx, uint32_t esi)
{
    return Stepper_SetLabelW(B(ebx), reinterpret_cast<const wchar_t*>(esi));
}
ABI_ENTRY_0046d0e0(Stepper_SetLabelW_Entry, Stepper_SetLabelW_Abi)
HOOK(0x0046d0e0, "Stepper_SetLabelW", Stepper_SetLabelW_Entry, nullptr);

// 0x0046d110 (esi = widget, edi = 8-bit text): widens the text into the label +0x4f8 (0x004e59d0, eax = 0x40
// characters, ecx = label, edi = text) and marks the layout dirty.
uint32_t Stepper_SetLabelA(uint8_t* widget, uint32_t text)
{
    const uint32_t r = orig::fn_004e59d0(0x40, u(widget + 0x4f8), text);
    Field<uint32_t>(widget, 0x474) |= 0x2000;
    return r;
}

static uint32_t Stepper_SetLabelA_Abi(uint32_t esi, uint32_t edi)
{
    return Stepper_SetLabelA(B(esi), edi);
}
ABI_ENTRY_0046d110(Stepper_SetLabelA_Entry, Stepper_SetLabelA_Abi)
HOOK(0x0046d110, "Stepper_SetLabelA", Stepper_SetLabelA_Entry, nullptr);

// 0x0046d290 (ecx = widget; stack: input, key flags pointer; ret 8; virtual, no direct callers): input. Bit 0x10
// (click): reads the pointer (0x00491360, eax = g+0xab4, edx = &x; stack: &y) and, when inside the track (80, 6) -
// (272, -6) (Ui_PixelToScreen, written into the two argument slots and frame; corners sorted), sets the position
// (x - x0) / (x1 - x0) with Stepper_SetPosition(.., 1); bit 8 steps up, bit 4 down (Stepper_SetIndex(.., 1)) unless at
// the end; each returns al = 1. Otherwise the base handler 0x00464a10. Not executed in recorded runs.
uint32_t Stepper_HandleInput(uint8_t* widget, uint32_t input, uint32_t* flags)
{
    if (input == 0 || *flags == 0)
        return orig::fn_00464a10(u(widget), input, u(flags));
    const uint32_t keys = *flags;
    if ((keys & 0x10) != 0)
    {
        float mouse[2];  // S+8 = x, S+0xc = y
        if (static_cast<uint8_t>(orig::fn_00491360(Field<uint32_t>(G(), 0xab4), u(&mouse[0]), u(&mouse[1]))) == 0)
            return 1;
        float x0, y0, x1, y1;
        Ui_PixelToScreen(widget, &x0, &y0, 0x50, 6);
        Ui_PixelToScreen(widget, &x1, &y1, 0x110, -6);
        if (x1 < x0)
        {
            const float t = x0;
            x0 = x1;
            x1 = t;
        }
        if (y1 < y0)
        {
            const float t = y0;
            y0 = y1;
            y1 = t;
        }
        if (!(mouse[0] >= x0) || !(mouse[0] <= x1) || !(mouse[1] >= y0) || !(mouse[1] <= y1))
            return 1;
        const float width = x1 - x0;
        Stepper_SetPosition(widget, (mouse[0] - x0) / width, 1);
        return 1;
    }
    if ((keys & 8) != 0)
    {
        const uint32_t index = Field<uint32_t>(widget, 0x5a0);
        if (index != Field<uint32_t>(widget, 0x584))
            Stepper_SetIndex(widget, u(flags), index + 1, 1);
        return 1;
    }
    if ((keys & 4) != 0)
    {
        const uint32_t index = Field<uint32_t>(widget, 0x5a0);
        if (index != 0)
            Stepper_SetIndex(widget, u(flags), index - 1, 1);
        return 1;
    }
    return orig::fn_00464a10(u(widget), input, u(flags));
}

static uint32_t Stepper_HandleInput_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return Stepper_HandleInput(B(ecx), a0, reinterpret_cast<uint32_t*>(a1));
}
ABI_ENTRY_0046d290(Stepper_HandleInput_Entry, Stepper_HandleInput_Abi)
HOOK(0x0046d290, "Stepper_HandleInput", Stepper_HandleInput_Entry, nullptr);

// ================================================================================================================
// Fitted value text (guess): +0x4f8 own wide buffer (0x40), +0x578 text pointer, +0x57c base scale, +0x580 scale,
// +0x584 maximum width (640 units; 0 = no fit)
// ================================================================================================================

uint32_t FitText_SetScale(uint8_t* widget, uint32_t scale);

// 0x0046d400 (ecx = widget; stack: 4 args; ret 0x10): 0x004651a0 (base), no text, empty buffer, no width limit,
// FitText_SetScale(1.0). Caller 0x00447d20.
uint32_t FitText_Init(uint8_t* widget, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    orig::fn_004651a0(u(widget), a0, a1, a2, a3);
    Field<uint32_t>(widget, 0x578) = 0;
    Field<uint16_t>(widget, 0x4f8) = 0;
    Field<uint32_t>(widget, 0x584) = 0;
    return FitText_SetScale(widget, 0x3f800000u);
}

static uint32_t FitText_Init_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return FitText_Init(B(ecx), a0, a1, a2, a3);
}
ABI_ENTRY_0046d400(FitText_Init_Entry, FitText_Init_Abi)
HOOK(0x0046d400, "FitText_Init", FitText_Init_Entry, nullptr);

// 0x0046d4b0 (ecx = widget): layout. Alignment x +0x4f0 = 1.0, UiText_Layout; with a width limit and a text, scale
// +0x580 = UiText_FitScale(text, +0x57c, limit, 0) / +0x4c8; measures the text at +0x580 * +0x4c8 (0x00495fa0) and adds
// its bounds (UiText_SetBounds(reset 0)) at (16, 0) with alignment (0, +0x4f4). Frame 0x10 bytes. Caller 0x00447da0.
uint32_t FitText_Layout(uint8_t* widget)
{
    float size[2];  // S+0 = height, S+4 = width
    float pos[2];   // S+8 = y, S+0xc = x
    Field<uint32_t>(widget, 0x4f0) = 0x3f800000u;
    UiText_Layout(widget);
    if (Field<float>(widget, 0x584) != K(0x0056c05c))
    {
        const uint32_t text = Field<uint32_t>(widget, 0x578);
        if (text != 0)
            Field<float>(widget, 0x580) =
                UiText_FitScale(text, Field<float>(widget, 0x57c), Field<float>(widget, 0x584), 0.0f) /
                Field<float>(widget, 0x4c8);
    }
    const float scale = Field<float>(widget, 0x580) * Field<float>(widget, 0x4c8);
    orig::fn_00495fa0(u(&size[1]), u(&size[0]), Field<uint32_t>(G(), 0xac0), Field<uint32_t>(widget, 0x578), fb(scale));
    Ui_PixelToScreen(widget, &pos[1], &pos[0], 0x10, 0);
    return UiText_SetBounds(widget, pos[1], pos[0], 0.0f, Field<float>(widget, 0x4f4), size[1], size[0], 0);
}

static uint32_t FitText_Layout_Abi(uint32_t ecx)
{
    return FitText_Layout(B(ecx));
}
ABI_ENTRY_0046d4b0(FitText_Layout_Entry, FitText_Layout_Abi)
HOOK(0x0046d4b0, "FitText_Layout", FitText_Layout_Entry, nullptr);

// 0x0046d580 (eax = widget; stack: scale; ret 4): marks the layout dirty, +0x57c = +0x580 = scale.
uint32_t FitText_SetScale(uint8_t* widget, uint32_t scale)
{
    Field<uint32_t>(widget, 0x474) |= 0x2000;
    Field<uint32_t>(widget, 0x57c) = scale;
    Field<uint32_t>(widget, 0x580) = scale;
    return u(widget);
}

static void FitText_SetScale_Abi(uint32_t eax, uint32_t a0)
{
    FitText_SetScale(B(eax), a0);
}
ABI_ENTRY_0046d580(FitText_SetScale_Entry, FitText_SetScale_Abi)
HOOK(0x0046d580, "FitText_SetScale", FitText_SetScale_Entry, nullptr);

// 0x0046d5a0 (eax = widget; stack: width; ret 4): marks the layout dirty, +0x584 = width limit. Caller 0x00447da0.
void FitText_SetWidth(uint8_t* widget, uint32_t width)
{
    Field<uint32_t>(widget, 0x474) |= 0x2000;
    Field<uint32_t>(widget, 0x584) = width;
}

static void FitText_SetWidth_Abi(uint32_t eax, uint32_t a0)
{
    FitText_SetWidth(B(eax), a0);
}
ABI_ENTRY_0046d5a0(FitText_SetWidth_Entry, FitText_SetWidth_Abi)
HOOK(0x0046d5a0, "FitText_SetWidth", FitText_SetWidth_Entry, nullptr);

// 0x0046d5c0 (eax = 8-bit text or null, esi = widget): marks the layout dirty and clears the text; with a text, widens
// it into +0x4f8 (0x004e59d0, eax = 0x40, ecx = buffer, edi = text; it preserves ECX) and points +0x578 at the buffer.
// Caller 0x00494280. Not executed in recorded runs.
uint32_t FitText_SetTextA(uint32_t text, uint8_t* widget)
{
    Field<uint32_t>(widget, 0x474) |= 0x2000;
    Field<uint32_t>(widget, 0x578) = 0;
    if (text == 0)
        return 0;
    const uint32_t r = orig::fn_004e59d0(0x40, u(widget + 0x4f8), text);
    Field<uint32_t>(widget, 0x578) = u(widget + 0x4f8);
    return r;
}

static uint32_t FitText_SetTextA_Abi(uint32_t eax, uint32_t esi)
{
    return FitText_SetTextA(eax, B(esi));
}
ABI_ENTRY_0046d5c0(FitText_SetTextA_Entry, FitText_SetTextA_Abi)
HOOK(0x0046d5c0, "FitText_SetTextA", FitText_SetTextA_Entry, nullptr);

// 0x0046d600 (ecx = arrow widget; stack: value; ret 4): glow +4 = max(+4, value) (stores +4 back when it is greater).
// Caller Ui_ArrowHit. Not executed in recorded runs.
uint32_t Ui_ArrowGlow(uint8_t* arrow, float value)
{
    if (Field<float>(arrow, 4) > value)
    {
        const uint32_t v = Field<uint32_t>(arrow, 4);
        Field<uint32_t>(arrow, 4) = v;
        return v;
    }
    Field<float>(arrow, 4) = value;
    return 0;
}

static uint32_t Ui_ArrowGlow_Abi(uint32_t ecx, uint32_t a0)
{
    return Ui_ArrowGlow(B(ecx), bf(a0));
}
ABI_ENTRY_0046d600(Ui_ArrowGlow_Entry, Ui_ArrowGlow_Abi)
HOOK(0x0046d600, "Ui_ArrowGlow", Ui_ArrowGlow_Entry, nullptr);

// 0x0046d630 (eax = out float[4]; stack: p0, p1, p2, p3; ret 0x10): the cubic coefficients of 4 control values through
// the 4x4 basis matrix at 0x0057e7b0: out[i] = ((m[i][0] p0 + m[i][1] p1) + m[i][2] p2) + m[i][3] p3 for rows 0..2, and
// out[3] = ((m[3][3] p3 + m[3][0] p0) + m[3][1] p1) + m[3][2] p2 (the original's order). guess: Bezier basis.
void Spline_Coefficients(float* out, float p0, float p1, float p2, float p3)
{
    const float* m = &GameGlobal<float>(0x0057e7b0);
    out[0] = m[0] * p0 + m[1] * p1 + m[2] * p2 + m[3] * p3;
    out[1] = m[4] * p0 + m[5] * p1 + m[6] * p2 + m[7] * p3;
    out[2] = m[8] * p0 + m[9] * p1 + m[10] * p2 + m[11] * p3;
    out[3] = m[15] * p3 + m[12] * p0 + m[13] * p1 + m[14] * p2;
}

static void Spline_Coefficients_Abi(uint32_t eax, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    Spline_Coefficients(reinterpret_cast<float*>(eax), bf(a0), bf(a1), bf(a2), bf(a3));
}
ABI_ENTRY_0046d630(Spline_Coefficients_Entry, Spline_Coefficients_Abi)
HOOK(0x0046d630, "Spline_Coefficients", Spline_Coefficients_Entry, nullptr);

// ================================================================================================================
// Display mode packing (the option value g+0x4a8, see 0x00401010 in src/ui/ui_01.cpp)
// ================================================================================================================

// 0x00470c90 (eax = height, ecx = width, esi = bits per pixel): packs a display mode: bit 31 set unless bpp == 16,
// bits 16..30 = height & 0x7fff, bits 0..15 = width. Evidence: 0x00401010 passes renderer QueryInfo(2) / (1) / (5).
uint32_t DisplayMode_Pack(uint32_t height, uint32_t width, uint32_t bpp)
{
    const uint32_t top = (static_cast<uint32_t>(bpp == 0x10 ? 1 : 0) - 1u) & 0x80000000u;
    return top | ((height & 0x7fff) << 16) | (width & 0xffff);
}

static uint32_t DisplayMode_Pack_Abi(uint32_t eax, uint32_t ecx, uint32_t esi)
{
    return DisplayMode_Pack(eax, ecx, esi);
}
ABI_ENTRY_00470c90(DisplayMode_Pack_Entry, DisplayMode_Pack_Abi)
HOOK(0x00470c90, "DisplayMode_Pack", DisplayMode_Pack_Entry, nullptr);

// 0x00470cc0 (eax = packed mode, edx = &width; stack: &height, &bpp; caller pops): the inverse of DisplayMode_Pack;
// bpp = bit 31 ? 32 : 16. Returns bpp.
uint32_t DisplayMode_Unpack(uint32_t mode, uint32_t* width, uint32_t* height, uint32_t* bpp)
{
    *width = mode & 0xffff;
    *height = (mode >> 16) & 0x7fff;
    const uint32_t b = ((mode & 0x80000000u) != 0 ? 0x10u : 0u) + 0x10u;
    *bpp = b;
    return b;
}

static uint32_t DisplayMode_Unpack_Abi(uint32_t eax, uint32_t edx, uint32_t a0, uint32_t a1)
{
    return DisplayMode_Unpack(eax, reinterpret_cast<uint32_t*>(edx), reinterpret_cast<uint32_t*>(a0),
                              reinterpret_cast<uint32_t*>(a1));
}
ABI_ENTRY_00470cc0(DisplayMode_Unpack_Entry, DisplayMode_Unpack_Abi)
HOOK(0x00470cc0, "DisplayMode_Unpack", DisplayMode_Unpack_Entry, nullptr);

// 0x004710d0 (edx = widget id, ebx = passed to 0x004927a0; stack: widget, unused, value for ids 44/45; ret 0xc):
// graphics/options menu widget setup, a switch on id - 7 (byte table 0x00471470, jump table 0x00471424):
//   7: 0x004927a0 (eax = widget, cl = (0x00403bb0(edi = g) && g+0x568 bit 8); stack: ebx, 0)
//   76: copies 0xd5 dwords g+0x174 -> 0x005fe6e8 (options backup), then as below
//   13, 27, 46, 52, 78 (and 76): 0x004927a0 (cl = 1; ebx, 0)
//   44, 45: 0x00447d60 (eax = value, ecx = widget, edx = id)
//   48: GfxMenu_InitResolutionList(widget)
//   47 "ui_gfxmenu_brightness": Stepper_Setup(11 positions, 1, cb 0x00470eb0, user 0x005fea28, 1.0 .. 2.0, g+0x4b4)
//   82 "ui_gfxmenu_drawdistance": Stepper_Setup(16, 1, 0x00470f20, 0x005fea24, 0.25 .. 1.0, g+0x4b0)
//   68 "ui_controls_mousescale": Stepper_Setup(21, 1, 0x00471050, &g+0x4b8, 0 .. 1.0, g+0x4b8)
//   49 "ui_gfxmenu_texturelod": Stepper_SetupIndex(5, 1, 0x00470f70, 0x005fea14, g+0x4a0)
//   81 "ui_gfxmenu_texturequality": Stepper_SetupIndex(3, 1, 0x00470fc0, 0x005fea10, g+0x49c)
//   50 "ui_gfxmenu_modellod": Stepper_SetupIndex(5, 1, 0x00470f70, 0x005fea18, g+0x4a4)
//   80 "ui_gfxmenu_maxtextures": Stepper_SetupIndex(max(renderer vt+0x98(1), 2), 1, 0x00471010, 0x005fea20,
//       g+0x4ac - 1) (the virtual is called twice when it returns more than 2)
//   77 / 51 / 55 / 59 "ui_gfxmenu_skinquality" / "_blur" / "_shadows" / "_decals": CheckBox_Bind(bool 0x005fea35 /
//       0x005fea30 / 0x005fea32 / 0x005fea33, enabled 0)
//   60 / 53 "ui_gfxmenu_pixelshaders" / "_bumpmap": supported = renderer vt+0x15c(1 / 0); CheckBox_Bind(0x005fea34 /
//       0x005fea31, enabled 0), then 0x004927a0 (cl = supported; ebx, 0) (the original keeps the flag in the widget
//       argument's stack slot)
//   default: 0x004927a0 (cl = 0; ebx, 1).
// Returns al = 0. Caller 0x00491b70.
uint32_t GfxMenu_SetupWidget(uint32_t id, uint32_t ebx, uint32_t widget, uint32_t /*a1*/, uint32_t value)
{
    uint8_t* g = G();
    switch (id)
    {
    case 7:
    {
        uint32_t enable = 0;
        if (static_cast<uint8_t>(orig::fn_00403bb0(u(G()))) != 0 && (Field<uint8_t>(G(), 0x568) & 8) != 0)
            enable = 1;
        orig::fn_004927a0(widget, enable, ebx, 0);
        return 0;
    }
    case 76:
        std::memcpy(P(0x005fe6e8), G() + 0x174, 0xd5 * 4);
        orig::fn_004927a0(widget, 1, ebx, 0);
        return 0;
    case 13:
    case 27:
    case 46:
    case 52:
    case 78:
        orig::fn_004927a0(widget, 1, ebx, 0);
        return 0;
    case 44:
    case 45:
        orig::fn_00447d60(value, widget, id);
        return 0;
    case 48:
        orig::fn_00470d70(widget);
        return 0;
    case 47:
        Stepper_Setup(B(widget), 0x005a02fcu, 0xb, 1, 0x00470eb0u, 0x005fea28u, 0x3f800000u, 0x40000000u,
                      Field<uint32_t>(g, 0x4b4));
        return 0;
    case 82:
        Stepper_Setup(B(widget), 0x005a02e4u, 0x10, 1, 0x00470f20u, 0x005fea24u, 0x3e800000u, 0x3f800000u,
                      Field<uint32_t>(g, 0x4b0));
        return 0;
    case 68:
        Stepper_Setup(B(widget), 0x005a0284u, 0x15, 1, 0x00471050u, u(g + 0x4b8), 0, 0x3f800000u,
                      Field<uint32_t>(g, 0x4b8));
        return 0;
    case 49:
        Stepper_SetupIndex(B(widget), 0x005a02ccu, 5, 1, 0x00470f70u, 0x005fea14u, Field<uint32_t>(g, 0x4a0));
        return 0;
    case 81:
        Stepper_SetupIndex(B(widget), 0x005a02b0u, 3, 1, 0x00470fc0u, 0x005fea10u, Field<uint32_t>(g, 0x49c));
        return 0;
    case 50:
        Stepper_SetupIndex(B(widget), 0x005a029cu, 5, 1, 0x00470f70u, 0x005fea18u, Field<uint32_t>(g, 0x4a4));
        return 0;
    case 80:
    {
        uint32_t n = VCall<uint32_t>(Renderer(), 0x98 / 4, 1u);
        if (n > 2)
            n = VCall<uint32_t>(Renderer(), 0x98 / 4, 1u);
        else
            n = 2;
        Stepper_SetupIndex(B(widget), 0x005a026cu, n, 1, 0x00471010u, 0x005fea20u, Field<uint32_t>(G(), 0x4ac) - 1);
        return 0;
    }
    case 77:
        CheckBox_Bind(B(widget), B(0x005fea35u), 0x005a0254u, 0);
        return 0;
    case 51:
        CheckBox_Bind(B(widget), B(0x005fea30u), 0x005a0244u, 0);
        return 0;
    case 55:
        CheckBox_Bind(B(widget), B(0x005fea32u), 0x005a0230u, 0);
        return 0;
    case 59:
        CheckBox_Bind(B(widget), B(0x005fea33u), 0x005a021cu, 0);
        return 0;
    case 60:
    case 53:
    {
        const bool shaders = id == 60;
        const uint32_t supported = VCall<uint32_t>(Renderer(), 0x15c / 4, shaders ? 1u : 0u);
        const uint32_t flag = static_cast<uint8_t>(supported) != 0 ? 1u : 0u;
        CheckBox_Bind(B(widget), B(shaders ? 0x005fea34u : 0x005fea31u), shaders ? 0x005a0204u : 0x005a01f0u, 0);
        orig::fn_004927a0(widget, flag, ebx, 0);
        return 0;
    }
    default:
        orig::fn_004927a0(widget, 0, ebx, 1);
        return 0;
    }
}

static uint32_t GfxMenu_SetupWidget_Abi(uint32_t edx, uint32_t ebx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return GfxMenu_SetupWidget(edx, ebx, a0, a1, a2);
}
ABI_ENTRY_004710d0(GfxMenu_SetupWidget_Entry, GfxMenu_SetupWidget_Abi)
HOOK(0x004710d0, "GfxMenu_SetupWidget", GfxMenu_SetupWidget_Entry, nullptr);

// 0x004714c0 (eax = action id, ecx = only pushed as the 4-byte frame slot, edi = menu stack; stack: a0, a1, a2; ret
// 0xc): menu action handler, a switch on id - 7 (byte table 0x00471704, jump table 0x004716e4):
//   7: when g+0x568 bit 8: UI_PushScreen (eax = g+0x510, ecx = menu; stack "QuickSave" 0x005bc150)
//   13: 0x00414520 (eax = g+0xa80), 0x004919c0 (a0, 0), Options_Apply (al = 1, esi = g)
//   27: UI_PushYesNo (eax = "ui_yesno_exitgame" 0x005a01b8, ecx = menu, edx = "ExitMenu" 0x005a01cc; 1, 0, 0, 0, 0, 0)
//   44, 45: UI_ShowAssignKeyPrompt (esi = a1)
//   46: UI_PushYesNo (eax = "ui_yesno_resetcontrols" 0x005a01d8, ecx = menu, edx = 0; 1, 0, callback 0x00470c40, 0, 0,
//       0)
//   76: apply graphics: when the chosen mode 0x005fea1c differs from g+0x4a8 it is stored and, if renderer QueryInfo(0)
//       is non-zero, set with renderer virtual +0xc (width, height, bpp, 0) (DisplayMode_Unpack into the frame slot and
//       the a1/a0 argument slots); on failure UI_PushNotify (eax = "ui_notify_badres" 0x005a01a4, ecx = g+0xab4, edx =
//       0; 0, 0, 0) and nothing else. Otherwise copies the menu copies 0x005fea10.. back into the options (g+0x49c,
//       +0x4a0, +0x4a4, +0x4ac, +0x4b0, +0x4b4, bytes +0x4bc..+0x4c1), Options_Apply (al = 1) and 0x00490b60 (ecx =
//       menu: pops the screen, guess).
//   78: 0x00490b60 (ecx = menu), Options_Apply (al = 0)
// Finally, when the menu depth 0x0063a160 did not change, UiSound_PlaySelectSet (eax = 0). The Options_Apply EAX upper
// bytes are the leftovers the original has there. Returns 0 (the original's EAX is a leftover). Caller 0x004921a0. Not
// executed in recorded runs.
uint32_t OptionsMenu_OnAction(uint32_t id, uint32_t menu, uint32_t a0, uint32_t a1, uint32_t /*a2*/)
{
    const uint32_t depth = GameGlobal<uint32_t>(0x0063a160);
    switch (id)
    {
    case 7:
        if ((Field<uint8_t>(G(), 0x568) & 8) != 0)
            orig::fn_00490b00(u(G()) + 0x510, menu, 0x005bc150u);
        break;
    case 13:
    {
        orig::fn_00414520(Field<uint32_t>(G(), 0xa80));
        const uint32_t r = orig::fn_004919c0(a0, 0);
        orig::fn_00401ce0((r & 0xffffff00u) | 1u, u(G()));
        break;
    }
    case 27:
        orig::fn_00490910(0x005a01b8u, menu, 0x005a01ccu, 1, 0, 0, 0, 0, 0);
        break;
    case 44:
    case 45:
        orig::fn_00447f50(a1);
        break;
    case 46:
        orig::fn_00490910(0x005a01d8u, menu, 0, 1, 0, 0x00470c40u, 0, 0, 0);
        break;
    case 76:
    {
        uint8_t* g = G();
        const uint32_t mode = GameGlobal<uint32_t>(0x005fea1c);
        if (mode != Field<uint32_t>(g, 0x4a8))
        {
            Field<uint32_t>(g, 0x4a8) = mode;
            uint32_t width;
            uint32_t height;
            uint32_t bpp;
            DisplayMode_Unpack(GameGlobal<uint32_t>(0x005fea1c), &width, &height, &bpp);
            if (VCall<uint32_t>(Field<void*>(g, 0x38), 0x160 / 4, 0u) != 0)
            {
                const uint32_t ok = VCall<uint32_t>(Field<void*>(G(), 0x38), 0xc / 4, width, height, bpp, 0u);
                if (static_cast<uint8_t>(ok) == 0)
                {
                    orig::fn_00490970(0x005a01a4u, Field<uint32_t>(G(), 0xab4), 0, 0, 0, 0);
                    break;
                }
            }
            g = G();
        }
        Field<uint32_t>(g, 0x4a4) = GameGlobal<uint32_t>(0x005fea18);
        Field<uint32_t>(g, 0x4a0) = GameGlobal<uint32_t>(0x005fea14);
        Field<uint32_t>(g, 0x49c) = GameGlobal<uint32_t>(0x005fea10);
        Field<uint32_t>(g, 0x4b0) = GameGlobal<uint32_t>(0x005fea24);
        Field<uint32_t>(g, 0x4b4) = GameGlobal<uint32_t>(0x005fea28);
        Field<uint8_t>(g, 0x4bc) = GameGlobal<uint8_t>(0x005fea30);
        Field<uint8_t>(g, 0x4bd) = GameGlobal<uint8_t>(0x005fea31);
        Field<uint8_t>(g, 0x4be) = GameGlobal<uint8_t>(0x005fea32);
        Field<uint8_t>(g, 0x4bf) = GameGlobal<uint8_t>(0x005fea33);
        Field<uint8_t>(g, 0x4c0) = GameGlobal<uint8_t>(0x005fea34);
        Field<uint8_t>(g, 0x4c1) = GameGlobal<uint8_t>(0x005fea35);
        Field<uint32_t>(g, 0x4ac) = GameGlobal<uint32_t>(0x005fea20);
        orig::fn_00401ce0((GameGlobal<uint32_t>(0x005fea14) & 0xffffff00u) | 1u, u(g));
        orig::fn_00490b60(menu);
        break;
    }
    case 78:
    {
        const uint32_t r = orig::fn_00490b60(menu);
        orig::fn_00401ce0(r & 0xffffff00u, u(G()));
        break;
    }
    default:
        break;
    }
    if (depth == GameGlobal<uint32_t>(0x0063a160))
        orig::fn_00491850(0);
    return 0;
}

static uint32_t OptionsMenu_OnAction_Abi(uint32_t eax, uint32_t /*ecx*/, uint32_t edi, uint32_t a0, uint32_t a1,
                                         uint32_t a2)
{
    return OptionsMenu_OnAction(eax, edi, a0, a1, a2);
}
ABI_ENTRY_004714c0(OptionsMenu_OnAction_Entry, OptionsMenu_OnAction_Abi)
HOOK(0x004714c0, "OptionsMenu_OnAction", OptionsMenu_OnAction_Entry, nullptr);

// ================================================================================================================
// UI manager (the object UI_Init 0x0048ff90 builds; +0xf0 flags, +0xf4 / +0xf8 screen stacks)
// ================================================================================================================

namespace
{
// 0x00415910 (edx = screen list, esi = memory, edi = name): the generated orig wrapper does not load EDI, which this
// constructor reads (see src/ui/ui_01.cpp), so it is called here with all three registers.
uint32_t Fn00415910(uint32_t edxValue, uint32_t esiValue, uint32_t ediValue)
{
    static const uint32_t target = 0x00415910;
    uint32_t result;
    __asm {
        push ebx
        push esi
        push edi
        push ebp
        mov edx, edxValue
        mov esi, esiValue
        mov edi, ediValue
        call dword ptr [target]
        pop ebp
        pop edi
        pop esi
        pop ebx
        mov result, eax
    }
    return result;
}
// The intrusive list insertion inlined 8 times in UI_Init: unlink node {next, prev-link} (prev-link points at the
// previous node's next field or the head), then insert it at the head.
inline void ListLinkHead(uint32_t* node, uint32_t* head)
{
    if (node[0] != 0)
        reinterpret_cast<uint32_t*>(node[0])[1] = node[1];
    if (node[1] != 0)
        *reinterpret_cast<uint32_t*>(node[1]) = node[0];
    node[1] = u(head);
    node[0] = 0;
    if (head != nullptr)
    {
        node[0] = *head;
        *head = u(node);
        if (node[0] != 0)
            reinterpret_cast<uint32_t*>(node[0])[1] = u(node);
    }
}
// Alloc(0x10, size, 2, "new" 0x0058b5e8): the screen allocations of UI_Init.
inline uint32_t AllocScreen(uint32_t size)
{
    return Alloc(0x10, size, 2, 0x0058b5e8u);
}
// UiScreen_Construct 0x004916e0 (ecx = flags, esi = memory, edi = name; stack: screen list, page name, value).
inline void ConstructScreen(uint32_t mem, uint32_t flags, uint32_t name, uint32_t list, uint32_t page, uint32_t value,
                            uint32_t vtable)
{
    if (mem == 0)
        return;
    orig::fn_004916e0(flags, mem, name, list, page, value);
    if (vtable != 0)
        Field<uint32_t>(P(mem), 0) = vtable;
}
}  // namespace

// 0x0048ff90 UI_Init (stack: UI manager; ret 4): initialises the UI manager: flag byte +0xf0 = (& 0xe4) | 4, clears
// +0xf4, +0xec, +0xfc (word), +4, +0xf8, +8 and the 0x38 dwords at +0xc, links the eight 0x1c-byte nodes +0xc, +0x28,
// .. +0xd0 into the list +8 (ListLinkHead, inlined), clears +0 and the 9 dwords at +0x2fc, then allocates (Alloc(0x10,
// size, 2, "new")) and constructs every screen into the screen list +0xf8: "ExitMenu" (0x00493770), "MainMenu"
// (pui_mainmenu, flags 0x8a0, value 110.0), "SaveGame" / "LoadGame" / "ChooseGame" (pui_savegame, vtable 0x0058fc2c),
// "Saving" (0x320) / "QuickSave" (0x324) (pui_saving, vtable 0x0058fc50), "Options", "Extras", "ReplayLevel",
// "MovieSelect", "Stats" (0x00494000, pui_stats), "MissionFailed" (0x24), "Difficulty", "GfxOpt", "Cheats", "SfxOpt",
// "Credits" (0x00415910), "PauseMenu" (0x100c, vtable 0x0058fce0), "ShowControls", "Loading" (0x280, vtable
// 0x0058fc74), "YesNo", "Notify", "LegalText" (0x00493520, b_legal, 4.0), "PlayMovie" (0x8000), "PlayMovieNoSkip"
// (0xc000), "AttractMode" (0x8050) (0x00493150) and "PubMenu" (pui_pub, 0x408, vtable 0x0058fc98); finally +0x320 =
// +0x324 = 0. Names, page names and sizes are the original's literal arguments. Returns the manager.
uint32_t UI_Init(uint8_t* ui)
{
    Field<uint8_t>(ui, 0xf0) = static_cast<uint8_t>((Field<uint8_t>(ui, 0xf0) & 0xe4) | 4);
    Field<uint32_t>(ui, 0xf4) = 0;
    Field<uint32_t>(ui, 0xec) = 0;
    Field<uint16_t>(ui, 0xfc) = 0;
    Field<uint32_t>(ui, 4) = 0;
    const uint32_t list = u(ui + 0xf8);
    uint32_t* head = &Field<uint32_t>(ui, 8);
    Field<uint32_t>(ui, 0xf8) = 0;
    *head = 0;
    std::memset(ui + 0xc, 0, 0x38 * 4);
    for (uint32_t offset = 0xc; offset <= 0xd0; offset += 0x1c)
        ListLinkHead(&Field<uint32_t>(ui, offset), head);
    Field<uint32_t>(ui, 0) = 0;
    std::memset(ui + 0x2fc, 0, 9 * 4);

    uint32_t mem = AllocScreen(0x68);
    if (mem != 0)
        orig::fn_00493770(list, mem, 0x005a01ccu);
    ConstructScreen(AllocScreen(0x68), 0x8a0, 0x005bc12cu, list, 0x00590134u, 0x42dc0000u, 0);
    ConstructScreen(AllocScreen(0x88), 0, 0x00590118u, list, 0x00590124u, 0, 0x0058fc2cu);
    ConstructScreen(AllocScreen(0x88), 0, 0x0059010cu, list, 0x00590124u, 0, 0x0058fc2cu);
    ConstructScreen(AllocScreen(0x88), 0, 0x00590100u, list, 0x00590124u, 0, 0x0058fc2cu);
    ConstructScreen(AllocScreen(0xac), 0x320, 0x005900ecu, list, 0x005900f4u, 0, 0x0058fc50u);
    ConstructScreen(AllocScreen(0xac), 0x324, 0x005bc150u, list, 0x005900f4u, 0, 0x0058fc50u);
    ConstructScreen(AllocScreen(0x68), 0, 0x005900d8u, list, 0x005900e0u, 0, 0);
    ConstructScreen(AllocScreen(0x68), 0, 0x005900c0u, list, 0x005900c8u, 0, 0);
    ConstructScreen(AllocScreen(0x68), 0, 0x005900a4u, list, 0x005900b0u, 0, 0);
    ConstructScreen(AllocScreen(0x68), 0, 0x00590088u, list, 0x00590094u, 0, 0);
    mem = AllocScreen(0x8c);
    if (mem != 0)
        orig::fn_00494000(0x0059007cu, 0, list, 0x005b2548u, mem);
    ConstructScreen(AllocScreen(0x68), 0x24, 0x005b251cu, list, 0x00590068u, 0, 0);
    ConstructScreen(AllocScreen(0x68), 0, 0x0059004cu, list, 0x00590058u, 0, 0);
    ConstructScreen(AllocScreen(0x68), 0, 0x00590038u, list, 0x00590040u, 0, 0);
    ConstructScreen(AllocScreen(0x68), 0, 0x00590024u, list, 0x0059002cu, 0, 0);
    ConstructScreen(AllocScreen(0x68), 0, 0x00590010u, list, 0x00590018u, 0, 0);
    mem = AllocScreen(0x284);
    if (mem != 0)
        Fn00415910(list, mem, 0x005b2534u);
    ConstructScreen(AllocScreen(0x68), 0x100c, 0x005bc138u, list, 0x00590000u, 0, 0x0058fce0u);
    ConstructScreen(AllocScreen(0x68), 0, 0x0058ffdcu, list, 0x0058ffecu, 0, 0);
    ConstructScreen(AllocScreen(0x26c), 0x280, 0x005b252cu, list, 0x0058ffd0u, 0, 0x0058fc74u);
    ConstructScreen(AllocScreen(0x68), 0, 0x0058ffbcu, list, 0x0058ffc4u, 0, 0);
    ConstructScreen(AllocScreen(0x68), 0, 0x0058ffa8u, list, 0x0058ffb0u, 0, 0);
    mem = AllocScreen(0xac);
    if (mem != 0)
        orig::fn_00493520(0x0058a9a4u, 0, u(ui) + 0xf8, 0, mem, 0x005bc0f0u, 0x40800000u, 0x0058ffa0u);
    mem = AllocScreen(0x278);
    if (mem != 0)
        orig::fn_00493150(0x0058a9a4u, 0x8000, u(ui) + 0xf8, 0x005bc0fcu, mem);
    mem = AllocScreen(0x278);
    if (mem != 0)
        orig::fn_00493150(0x0058a9a4u, 0xc000, u(ui) + 0xf8, 0x0058ff90u, mem);
    mem = AllocScreen(0x278);
    if (mem != 0)
        orig::fn_00493150(0x0058a9a4u, 0x8050, u(ui) + 0xf8, 0x0058ff84u, mem);
    mem = AllocScreen(0x278);
    if (mem != 0)
    {
        orig::fn_00493150(0x005a03e8u, 0x408, u(ui) + 0xf8, 0x005bc08cu, mem);
        Field<uint32_t>(P(mem), 0) = 0x0058fc98u;
    }
    Field<uint32_t>(ui, 0x320) = 0;
    Field<uint32_t>(ui, 0x324) = 0;
    return u(ui);
}

static uint32_t UI_Init_Abi(uint32_t a0)
{
    return UI_Init(B(a0));
}
ABI_ENTRY_0048ff90(UI_Init_Entry, UI_Init_Abi)
HOOK(0x0048ff90, "UI_Init", UI_Init_Entry, nullptr);

// 0x00490760 (esi = UI manager): 0x004909f0 (eax = manager), then deletes every screen on the stacks +0xf4 and +0xf8
// (virtual 0 = scalar deleting destructor, argument 1; each removes itself from the stack). Caller 0x00401710. Not
// executed in recorded runs.
uint32_t Ui_DestroyScreens(uint8_t* ui)
{
    uint32_t r = orig::fn_004909f0(u(ui));
    while (Field<uint32_t>(ui, 0xf4) != 0)
        r = VCall<uint32_t>(Field<void*>(ui, 0xf4), 0, 1u);
    while (Field<uint32_t>(ui, 0xf8) != 0)
        r = VCall<uint32_t>(Field<void*>(ui, 0xf8), 0, 1u);
    return r;
}

static uint32_t Ui_DestroyScreens_Abi(uint32_t esi)
{
    return Ui_DestroyScreens(B(esi));
}
ABI_ENTRY_00490760(Ui_DestroyScreens_Entry, Ui_DestroyScreens_Abi)
HOOK(0x00490760, "Ui_DestroyScreens", Ui_DestroyScreens_Entry, nullptr);

// 0x004907a0 (eax = UI manager): sets bit 0x10 of the flag byte +0xf0. Caller 0x004145c0. Not executed in recorded runs.
void Ui_SetFlag10(uint8_t* ui)
{
    Field<uint8_t>(ui, 0xf0) |= 0x10;
}

static void Ui_SetFlag10_Abi(uint32_t eax)
{
    Ui_SetFlag10(B(eax));
}
ABI_ENTRY_004907a0(Ui_SetFlag10_Entry, Ui_SetFlag10_Abi)
HOOK(0x004907a0, "Ui_SetFlag10", Ui_SetFlag10_Entry, nullptr);

// 0x004907b0 (eax = UI manager): returns the top screen's (+0xf4) member +4, or 0 without a screen. Caller 0x004a2e40.
// Not executed in recorded runs.
uint32_t Ui_TopScreenField4(uint8_t* ui)
{
    const uint32_t top = Field<uint32_t>(ui, 0xf4);
    if (top == 0)
        return 0;
    return Field<uint32_t>(P(top), 4);
}

static uint32_t Ui_TopScreenField4_Abi(uint32_t eax)
{
    return Ui_TopScreenField4(B(eax));
}
ABI_ENTRY_004907b0(Ui_TopScreenField4_Entry, Ui_TopScreenField4_Abi)
HOOK(0x004907b0, "Ui_TopScreenField4", Ui_TopScreenField4_Entry, nullptr);
