// Recovered: renderer option bits, a state-slot getter, the font loader and the text measuring helpers
// (renderer vtable 0x00589990). S2, Approach B. None of these run in the comparison scenarios.
//   0x00504b90 Renderer_GetOptionFlag     0x00504c00 Renderer_SetOptionFlag
//   0x00504d10 fn_00504d10                0x00504d50 fn_00504d50
//   0x00504de0 fn_00504de0                0x00506c90 Renderer_LoadFont
//   0x00506f90 Renderer_GetPixelWidth     0x00506fc0 Renderer_GetPixelHeight
//   0x00506ff0 Renderer_GetGlyphSize      0x00507140 Renderer_TextIsTagStart
//   0x00507180 Renderer_TextIsBreakTag    0x005071a0 Renderer_TextParseLlTag
//   0x00507220 Renderer_TextParseTabTag   0x00507370 Renderer_TextParseRgbTag
//   0x005073f0 Renderer_TextSkipTag       0x005074a0 Renderer_MeasureText
#include <cstring>

#include <windows.h>

#include "gen/entry_stubs.h"
#include "gen/orig_calls.h"
#include "hooks.h"
#include "renderer.h"

uint32_t RenderPasses_Init(bool enable, Renderer* r);  // renderer_init.cpp (0x00506b60)
uint32_t File_GetSize(uint8_t* file);                  // loader/auto_004ef270.cpp (0x004f1f00)

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
// fild of a dword, then fadd 4294967296.0 (0x005bcb28) when it was negative: the game's unsigned-to-float.
float UnsignedToFloat(uint32_t v)
{
    float f = static_cast<float>(static_cast<int32_t>(v));
    if (static_cast<int32_t>(v) < 0)
        f += 4294967296.0f;
    return f;
}
// 0x0053b2a0 is the C runtime _wtof (cdecl, result in st0). The generated orig wrapper only returns EAX and would
// leave st0 on the x87 stack, so this calls it directly and pops st0. The ABI scan lists ebx/esi/edi/ebp as inputs
// (the CRT's SEH prolog saves them); ebx/esi/edi get the values the original callers had, ebp is ours.
double GameWtof(const uint16_t* text, uint32_t ebxValue, uint32_t esiValue, uint32_t ediValue)
{
    static const uint32_t target = 0x0053b2a0;
    double result;
    __asm {
        push ebx
        push esi
        push edi
        push ebp
        push text
        mov ebx, ebxValue
        mov esi, esiValue
        mov edi, ediValue
        call dword ptr [target]
        add esp, 4
        pop ebp
        pop edi
        pop esi
        pop ebx
        fstp qword ptr [result]
    }
    return result;
}
const uint32_t kAllocator = 0x0063a210;  // ecx of the game's allocator 0x004e07f0 / free 0x004e08d0
const uint32_t kNameBuffer = 0x0064a810;  // shared 0x100-byte path buffer
const uint32_t kOne = 0x3f800000;         // 1.0f
}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// Option bits in +0x14dc (bit 0 windowed request, bits 1-4 shader options parsed by 0x00508290, bit 5 projection
// flip used by Renderer_SetProjection 0x004fea60). Option index -> bit (jump tables 0x00504bec / 0x00504cf4):
// 0 -> 3, 1 -> 2, 2 -> 1 (read only), 3 -> 4, 4 -> 5.

// 0x00504b90 (ecx = renderer; [option], callee pops): renderer virtual 86 (+0x158). Returns the option's bit of
// +0x14dc in AL, 0 for options above 4; the upper bytes of EAX are the option's (the original loads it into EAX).
uint32_t Renderer_GetOptionFlag(Renderer* r, uint32_t option)
{
    static const uint8_t kShift[5] = {3, 2, 1, 4, 5};
    uint32_t bit = option <= 4 ? (Field<uint8_t>(r, 0x14dc) >> kShift[option]) & 1 : 0;
    return (option & 0xffffff00) | bit;
}

// 0x00504c00 (ecx = renderer; [option, value byte], callee pops): renderer virtual 85 (+0x154). Options 0, 1 and
// 3: if the bit differs from the value byte, store bit 0 of the value and rebuild the render passes
// (RenderPasses_Init 0x00506b60 with al = 1). Option 4 (projection flip): if it differs, take the current
// projection block (virtual 24, +0x60, 17 dwords copied before the flag changes), store the bit and set the copy
// again with virtual 19 (+0x4c, Renderer_SetProjection) so the flip is applied. Option 2 and others: nothing.
// Returns the last call's result, else what EAX held (option, value byte or the bit).
uint32_t Renderer_SetOptionFlag(Renderer* r, uint32_t option, uint32_t value)
{
    uint8_t v = static_cast<uint8_t>(value);
    uint8_t& opts = Field<uint8_t>(r, 0x14dc);
    int shift;
    switch (option)
    {
    case 0: shift = 3; break;
    case 1: shift = 2; break;
    case 3: shift = 4; break;
    case 4:
    {
        uint8_t bit = (opts >> 5) & 1;
        if (bit == v)
            return bit;
        const uint32_t* current = VCall<const uint32_t*>(r, 0x60 / 4);
        uint8_t old = opts;
        uint32_t block[0x11];
        for (int i = 0; i < 0x11; i++)
            block[i] = current[i];
        opts = old ^ ((static_cast<uint8_t>(v << 5) ^ old) & 0x20);
        return VCall<uint32_t>(r, 0x4c / 4, block);
    }
    default:
        return option;  // option 2 and options above 4: EAX still holds the option
    }
    uint8_t old = opts;
    if (((old >> shift) & 1) == v)
        return v;
    uint8_t mask = static_cast<uint8_t>(1u << shift);
    opts = old ^ ((static_cast<uint8_t>(v << shift) ^ old) & mask);
    return RenderPasses_Init(true, r);
}

static uint32_t Renderer_GetOptionFlag_Abi(uint32_t ecx, uint32_t option)
{
    return Renderer_GetOptionFlag(reinterpret_cast<Renderer*>(ecx), option);
}
static uint32_t Renderer_SetOptionFlag_Abi(uint32_t ecx, uint32_t option, uint32_t value)
{
    return Renderer_SetOptionFlag(reinterpret_cast<Renderer*>(ecx), option, value);
}
ABI_ENTRY_00504b90(Renderer_GetOptionFlag_Entry, Renderer_GetOptionFlag_Abi)
ABI_ENTRY_00504c00(Renderer_SetOptionFlag_Entry, Renderer_SetOptionFlag_Abi)
HOOK(0x00504b90, "Renderer_GetOptionFlag", Renderer_GetOptionFlag_Entry, nullptr);
HOOK(0x00504c00, "Renderer_SetOptionFlag", Renderer_SetOptionFlag_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------

// 0x00504d10 (ecx = renderer; [value], callee pops): renderer virtual 37 (+0x94). Clamps the value to the limit
// +0x15ec (0 means the limit, unsigned compare) and, if it differs from +0x4ac, stores it and rebuilds the render
// passes (RenderPasses_Init, al = 1). Returns that result, or the clamped value when nothing changed.
uint32_t fn_00504d10(Renderer* r, uint32_t value)
{
    uint32_t limit = Field<uint32_t>(r, 0x15ec);
    if (value == 0 || value > limit)
        value = limit;
    if (value == Field<uint32_t>(r, 0x4ac))
        return value;
    Field<uint32_t>(r, 0x4ac) = value;
    return RenderPasses_Init(true, r);
}

// 0x00504d50 (ecx = renderer; [which byte], callee pops): renderer virtual 38 (+0x98), the getter of virtual 37:
// the limit +0x15ec when the byte is non-zero, else the current value +0x4ac.
uint32_t fn_00504d50(Renderer* r, uint32_t which)
{
    return static_cast<uint8_t>(which) != 0 ? Field<uint32_t>(r, 0x15ec) : Field<uint32_t>(r, 0x4ac);
}

// 0x00504de0 (ecx = renderer; [slot], callee pops): renderer virtual 27 (+0x6c). Returns the address of state
// slot record[slot]'s 17-dword block, renderer + 0x5fc + slot * 0x50 (the block fn_00504d70 compares and copies).
uint32_t fn_00504de0(Renderer* r, uint32_t slot)
{
    return slot * 0x50 + u(r) + 0x5fc;
}

static uint32_t fn_00504d10_Abi(uint32_t ecx, uint32_t value)
{
    return fn_00504d10(reinterpret_cast<Renderer*>(ecx), value);
}
static uint32_t fn_00504d50_Abi(uint32_t ecx, uint32_t which)
{
    return fn_00504d50(reinterpret_cast<Renderer*>(ecx), which);
}
static uint32_t fn_00504de0_Abi(uint32_t ecx, uint32_t slot)
{
    return fn_00504de0(reinterpret_cast<Renderer*>(ecx), slot);
}
ABI_ENTRY_00504d10(fn_00504d10_Entry, fn_00504d10_Abi)
ABI_ENTRY_00504d50(fn_00504d50_Entry, fn_00504d50_Abi)
ABI_ENTRY_00504de0(fn_00504de0_Entry, fn_00504de0_Abi)
HOOK(0x00504d10, "fn_00504d10", fn_00504d10_Entry, nullptr);
HOOK(0x00504d50, "fn_00504d50", fn_00504d50_Entry, nullptr);
HOOK(0x00504de0, "fn_00504de0", fn_00504de0_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// Font (strings "Font: Unable to open '%s'", "XBFont: Incorrect version number on font file!"; layout
// docs/formats/big-font.md). Renderer members: +0x58c state object, +0x590 the font texture record in use,
// +0x594 the embedded texture record (9 dwords, name at +0, flags word at +0x22 = +0x5b6), +0x5d8 the font file
// data, +0x5dc line height, +0x5e0/+0x5e4 texture size, +0x5e8/+0x5ea first/last character, +0x5f0 glyphs.

// 0x00506c90 (ecx = renderer; [font {data, texture record} or null, name], callee pops): renderer virtual 1.
// If the embedded texture record is in use, unregister it (virtual 74, +0x128, with 0) and free the font data
// (0x004e08d0). Clears +0x590, the 7 dwords at +0x5d8 and the 9 dwords at +0x594. With a font argument, takes its
// data and texture. Otherwise, with a name: open "<name>.abc" (0x004f1c90, sprintf 0x0053814f into 0x0064a810),
// read it whole into a new block (0x004e07f0: 1, size, 2, tag 0x0059da34) with ReadFile/CloseHandle; register
// "<name>.tga" as the embedded record (flags 0x1000, virtual 73 = Renderer_RegisterTexture) and fail unless its
// flags have 0x9000. A version-4 header gives the line height (+4), texture size (+8/+0xc), first/last character
// (words +0x14/+0x16) and glyphs (+0x1c); any other version only logs. Finally the state object's virtual 2
// (0x1338c, texture record, 0) and virtual 3 (0). Returns 1, or 0 on failure (logs a missing file). The stack
// cookie is not reproduced.
uint32_t Renderer_LoadFont(Renderer* r, const uint32_t* font, const char* name)
{
    using ReadFileFn = BOOL(__stdcall*)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
    using CloseHandleFn = BOOL(__stdcall*)(HANDLE);
    uint8_t* record = &Field<uint8_t>(r, 0x594);
    if (Field<uint8_t*>(r, 0x590) == record)
    {
        VCall<uint32_t>(r, 0x128 / 4, record, 0u);
        if (uint32_t data = Field<uint32_t>(r, 0x5d8))
            orig::fn_004e08d0(kAllocator, data);
    }
    Field<uint32_t>(r, 0x590) = 0;
    memset(&Field<uint8_t>(r, 0x5d8), 0, 7 * 4);
    memset(record, 0, 9 * 4);

    const uint32_t* data;
    if (font)
    {
        data = reinterpret_cast<const uint32_t*>(font[0]);
        Field<const uint32_t*>(r, 0x5d8) = data;
        Field<uint32_t>(r, 0x590) = font[1];
    }
    else
    {
        if (!name)
            return 0;
        uint8_t file[0x108];
        orig::fn_0053814f(kNameBuffer, 0x00589714 /* "%s.abc" */, u(name));
        if (static_cast<uint8_t>(orig::fn_004f1c90(u(file), kNameBuffer)) == 0)
        {
            GameLog(reinterpret_cast<const char*>(0x005896f0) /* "Font: Unable to open '%s'\n" */,
                    reinterpret_cast<const char*>(kNameBuffer));
            return 0;
        }
        DWORD size = File_GetSize(file);
        uint32_t block = orig::fn_004e07f0(kAllocator, 1, size, 2, 0x0059da34);
        Field<uint32_t>(r, 0x5d8) = block;
        HANDLE handle = Field<HANDLE>(file, 0);
        DWORD bytesRead;
        BOOL ok = (*reinterpret_cast<ReadFileFn*>(0x0056b0c8))(handle, reinterpret_cast<void*>(block), size,
                                                               &bytesRead, nullptr);
        (*reinterpret_cast<CloseHandleFn*>(0x0056b084))(Field<HANDLE>(file, 0));
        if (!ok)
        {
            if (uint32_t loaded = Field<uint32_t>(r, 0x5d8))
            {
                orig::fn_004e08d0(kAllocator, loaded);
                Field<uint32_t>(r, 0x5d8) = 0;
            }
            return 0;
        }
        orig::fn_0053814f(kNameBuffer, 0x0058970c /* "%s.tga" */, u(name));
        Field<uint32_t>(record, 0) = kNameBuffer;
        Field<uint16_t>(r, 0x5b6) = 0x1000;
        VCall<uint32_t>(r, 0x124 / 4, record);
        Field<uint8_t*>(r, 0x590) = record;
        if ((Field<uint16_t>(r, 0x5b6) & 0x9000) != 0x9000)
            return 0;
        data = reinterpret_cast<const uint32_t*>(block);
    }

    if (data[0] == 4)
    {
        Field<uint32_t>(r, 0x5dc) = data[1];
        Field<uint32_t>(r, 0x5e0) = data[2];
        Field<uint32_t>(r, 0x5e4) = data[3];
        Field<uint16_t>(r, 0x5e8) = Field<uint16_t>(data, 0x14);
        Field<uint16_t>(r, 0x5ea) = Field<uint16_t>(data, 0x16);
        Field<const uint32_t*>(r, 0x5f0) = data + 7;
    }
    else
    {
        GameLog(reinterpret_cast<const char*>(0x005896c0) /* "XBFont: Incorrect version number on font file!\n" */);
    }
    VCall<uint32_t>(Field<void*>(r, 0x58c), 2, 0x1338cu, Field<uint32_t>(r, 0x590), 0u);
    VCall<uint32_t>(Field<void*>(r, 0x58c), 3, 0u);
    return 1;
}

// 0x00506f90 (ecx = renderer; [size or null], callee pops, result in st0): renderer virtual 32 (+0x80), the pixel
// width Renderer_DrawGlyph uses: 1.0 (0x005bcb2c) / (x + x), x = size[0] or +0x40.
float g_pixelSizeScale[2] = {1.0f, 1.0f};

float Renderer_GetPixelWidth(Renderer* r, const float* size)
{
    float x = size ? size[0] : Field<float>(r, 0x40);
    return g_pixelSizeScale[0] / (x + x);
}

// 0x00506fc0 (ecx = renderer; [size or null], callee pops, result in st0): renderer virtual 33 (+0x84), the pixel
// height: 1.0 / (y + y), y = size[1] or +0x44.
float Renderer_GetPixelHeight(Renderer* r, const float* size)
{
    float y = size ? size[1] : Field<float>(r, 0x44);
    return g_pixelSizeScale[1] / (y + y);
}

// 0x00506ff0 (ecx = renderer; [&w, &h, character word, scaleX, scaleY], callee pops 0x14): renderer virtual 111
// (+0x1bc). Returns 1 without a character, outputs or glyph table (+0x5f0). '\n' (10): w = 0. Characters outside
// [+0x5e8, +0x5ea] (unsigned 16-bit): w = h = 0, return 0. Otherwise w = (glyph advance +0x14 + x offset +0x10,
// signed words of the 24-byte glyph) * scaleX. Then h = (line height +0x5dc + 1, unsigned) * scaleY. Returns 0.
uint32_t Renderer_GetGlyphSize(Renderer* r, float* w, float* h, uint32_t character, float scaleX, float scaleY)
{
    uint16_t ch = static_cast<uint16_t>(character);
    const uint8_t* glyphs = Field<const uint8_t*>(r, 0x5f0);
    if (ch == 0 || !w || !h || !glyphs)
        return 1;
    if (ch == 10)
    {
        Field<uint32_t>(w, 0) = 0;
    }
    else
    {
        uint16_t first = Field<uint16_t>(r, 0x5e8);
        if (ch < first || ch > Field<uint16_t>(r, 0x5ea))
        {
            Field<uint32_t>(h, 0) = 0;
            Field<uint32_t>(w, 0) = 0;
            return 0;
        }
        const uint8_t* glyph = glyphs + (static_cast<uint32_t>(ch) - first) * 24;
        int32_t sum = Field<int16_t>(glyph, 0x14) + Field<int16_t>(glyph, 0x10);
        *w = static_cast<float>(sum) * scaleX;
    }
    *h = UnsignedToFloat(Field<uint32_t>(r, 0x5dc) + 1) * scaleY;
    return 0;
}

static uint32_t Renderer_LoadFont_Abi(uint32_t ecx, uint32_t font, uint32_t name)
{
    return Renderer_LoadFont(reinterpret_cast<Renderer*>(ecx), reinterpret_cast<const uint32_t*>(font),
                             reinterpret_cast<const char*>(name));
}
static float Renderer_GetPixelWidth_Abi(uint32_t ecx, uint32_t size)
{
    return Renderer_GetPixelWidth(reinterpret_cast<Renderer*>(ecx), reinterpret_cast<const float*>(size));
}
static float Renderer_GetPixelHeight_Abi(uint32_t ecx, uint32_t size)
{
    return Renderer_GetPixelHeight(reinterpret_cast<Renderer*>(ecx), reinterpret_cast<const float*>(size));
}
static uint32_t Renderer_GetGlyphSize_Abi(uint32_t ecx, uint32_t w, uint32_t h, uint32_t character,
                                          uint32_t scaleX, uint32_t scaleY)
{
    return Renderer_GetGlyphSize(reinterpret_cast<Renderer*>(ecx), reinterpret_cast<float*>(w),
                                 reinterpret_cast<float*>(h), character, AsFloat(scaleX), AsFloat(scaleY));
}
ABI_ENTRY_00506c90(Renderer_LoadFont_Entry, Renderer_LoadFont_Abi)
ABI_ENTRY_00506f90(Renderer_GetPixelWidth_Entry, Renderer_GetPixelWidth_Abi)
ABI_ENTRY_00506fc0(Renderer_GetPixelHeight_Entry, Renderer_GetPixelHeight_Abi)
ABI_ENTRY_00506ff0(Renderer_GetGlyphSize_Entry, Renderer_GetGlyphSize_Abi)
HOOK(0x00506c90, "Renderer_LoadFont", Renderer_LoadFont_Entry, nullptr);
HOOK(0x00506f90, "Renderer_GetPixelWidth", Renderer_GetPixelWidth_Entry, nullptr);
HOOK(0x00506fc0, "Renderer_GetPixelHeight", Renderer_GetPixelHeight_Entry, nullptr);
HOOK(0x00506ff0, "Renderer_GetGlyphSize", Renderer_GetGlyphSize_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// Text markup (renderer virtuals 102-112, 16-bit characters; see Renderer_TextSkipTaggedBlock in
// auto_00503900.cpp). Virtual 110 (+0x1b8) is Renderer_TextMatchTag.

// 0x00507140 ([text], callee pops, ECX unused): renderer virtual 109 (+0x1b4). True when the text starts with '<'
// and a '>' comes before the end or another '<'. EAX upper bytes as the original leaves them (the text pointer's
// upper word; for a text not starting with '<', its upper three bytes).
uint32_t Renderer_TextIsTagStart(const uint16_t* text)
{
    if (*text != 0x3c)
        return u(text) & 0xffffff00;
    for (const uint16_t* c = text + 1; *c != 0 && *c != 0x3c; c++)
    {
        if (*c == 0x3e)
            return (u(text) & 0xffff0000) | 1;
    }
    return u(text) & 0xffff0000;
}

// 0x00507180 (ecx = renderer; [text], callee pops): renderer virtual 108 (+0x1b0), the line-break tag: returns
// virtual 110 (text, "<br>" 0x0058ea48).
uint32_t Renderer_TextIsBreakTag(Renderer* r, const uint16_t* text)
{
    return VCall<uint32_t>(r, 0x1b8 / 4, text, reinterpret_cast<const uint16_t*>(0x0058ea48));
}

// 0x005071a0 (ecx = renderer; [cursor, out or null, scaleX], callee pops 0xc): renderer virtual 106 (+0x1a8),
// the "<ll=N>" tag. If the text at *cursor starts with "<ll=" (0x005896b4, virtual 110): with an output,
// *out = _wtof(text + 4 characters) * width of 'W' (virtual 111: &w, &h, 0x57, scaleX, 1.0); then skip the tag
// (virtual 102, +0x198) and return its result. Otherwise AL = 0 (upper bytes from virtual 110).
uint32_t Renderer_TextParseLlTag(Renderer* r, const uint16_t** cursor, float* out, uint32_t scaleX)
{
    uint32_t result = VCall<uint32_t>(r, 0x1b8 / 4, *cursor, reinterpret_cast<const uint16_t*>(0x005896b4));
    if ((result & 0xff) == 0)
        return result & 0xffffff00;
    if (out)
    {
        float w, h;
        VCall<uint32_t>(r, 0x1bc / 4, &w, &h, 0x57u, scaleX, kOne);
        double value = GameWtof(*cursor + 4, u(out), u(r), u(cursor));
        *out = static_cast<float>(value * static_cast<double>(w));
    }
    return VCall<uint32_t>(r, 0x198 / 4, cursor);
}

// 0x00507220 (ecx = renderer; [cursor, out or null, base, scaleX], callee pops 0x10): renderer virtual 105
// (+0x1a4), the "<tab=N>" tag. If the text starts with "<tab=" (0x005896a8, virtual 110): with an output,
// tab = (float)_wtof(text + 5 characters), w = width of 'W' (virtual 111: &w, &h, 0x57, scaleX, 1.0) and
// *out = w * tab + base; then skip the tag (virtual 102) and return its result. Otherwise AL = 0.
uint32_t Renderer_TextParseTabTag(Renderer* r, const uint16_t** cursor, float* out, float base, uint32_t scaleX)
{
    uint32_t result = VCall<uint32_t>(r, 0x1b8 / 4, *cursor, reinterpret_cast<const uint16_t*>(0x005896a8));
    if ((result & 0xff) == 0)
        return result & 0xffffff00;
    if (out)
    {
        float tab = static_cast<float>(GameWtof(*cursor + 5, u(out), u(r), u(cursor)));
        float w, h;
        VCall<uint32_t>(r, 0x1bc / 4, &w, &h, 0x57u, scaleX, kOne);
        *out = w * tab + base;
    }
    return VCall<uint32_t>(r, 0x198 / 4, cursor);
}

// 0x00507370 (ecx = renderer; [cursor, default colour, &colour], callee pops 0xc): renderer virtual 104 (+0x1a0).
// "<rgb=RRGGBB>" (character 11 is '>' and virtual 110 matches "<rgb=" 0x00589658): colour = hex at character 5
// (0x004f0b00, edx = text + 10 bytes) | 0xff000000, skip 12 characters. Else "</rgb>" (0x00589648): colour =
// default, skip 6 characters. Returns AL = 1 when a tag was taken, else 0 (upper bytes as the original's EAX).
uint32_t Renderer_TextParseRgbTag(Renderer* r, const uint16_t** cursor, uint32_t defaultColour, uint32_t* colour)
{
    if ((*cursor)[11] == 0x3e)
    {
        uint32_t match = VCall<uint32_t>(r, 0x1b8 / 4, *cursor, reinterpret_cast<const uint16_t*>(0x00589658));
        if ((match & 0xff) != 0)
        {
            uint32_t value = orig::fn_004f0b00(u(*cursor) + 10) | 0xff000000;
            *colour = value;
            *cursor += 12;
            return (value & 0xffffff00) | 1;
        }
    }
    uint32_t match = VCall<uint32_t>(r, 0x1b8 / 4, *cursor, reinterpret_cast<const uint16_t*>(0x00589648));
    if ((match & 0xff) == 0)
        return match & 0xffffff00;
    *colour = defaultColour;
    *cursor += 6;
    return (match & 0xffffff00) | 1;
}

// 0x005073f0 ([cursor], callee pops, ECX unused): renderer virtual 102 (+0x198). If *cursor is at '<', advance
// it past the next '>' (or to the end of the text) and return AL = 1; else AL = 0. The upper bytes of EAX are the
// cursor argument's.
uint32_t Renderer_TextSkipTag(const uint16_t** cursor)
{
    if (**cursor != 0x3c)
        return u(cursor) & 0xffffff00;
    while (**cursor != 0x3e)
    {
        *cursor += 1;
        if (**cursor == 0)
            break;
    }
    if (**cursor != 0)
        *cursor += 1;
    return (u(cursor) & 0xffffff00) | 1;
}

// 0x005074a0 (ecx = renderer; [&width, &height, text, scaleX, scaleY, single line, max characters, flags], callee
// pops 0x20): renderer virtual 112 (+0x1c0), measure 16-bit text (0x00507710 is its 8-bit version). Returns 1 when
// text, width or height is missing; else 0 with width = widest line and height = total line height so far
// (starting at the '\n' height from virtual 111). Unless flags bit 0, markup is interpreted at each tag start
// (virtual 109): "<br>" (virtual 108) is skipped and counts as '\n'; tagged blocks are skipped (virtual 107);
// "<ll=N>" sets the line width (virtual 106) and skips to the line end (virtual 103); "<tab=N>" sets it (virtual
// 105, base 0); other tags are skipped (virtual 102). Each character advances x by its width (virtual 111); '\n'
// restarts x at 0 and adds the line height, or stops when the single-line byte is set. Stops after max
// characters when that is non-zero. Width/height are raised only when the new value is greater (fcomp, ah & 0x41).
uint32_t Renderer_MeasureText(Renderer* r, float* outW, float* outH, const uint16_t* text, uint32_t scaleX,
                              uint32_t scaleY, uint32_t singleLine, uint32_t maxChars, uint32_t flags)
{
    bool markup = (~flags & 1) != 0;
    uint32_t count = 0;
    if (!text || !outW || !outH)
        return 1;
    Field<uint32_t>(outW, 0) = 0;
    Field<uint32_t>(outH, 0) = 0;
    float w, h;
    VCall<uint32_t>(r, 0x1bc / 4, &w, &h, 10u, scaleX, scaleY);
    const uint16_t* cursor = text;
    float curX = 0.0f;
    float totalH = h;
    if (*cursor == 0)
        return 0;
    uint32_t ch = 0;
    do
    {
        bool advance = true;
        if (markup)
        {
            if (VCall<uint32_t>(r, 0x1b4 / 4, cursor) & 0xff)
            {
                do
                {
                    if (VCall<uint32_t>(r, 0x1b0 / 4, cursor) & 0xff)
                    {
                        VCall<uint32_t>(r, 0x198 / 4, &cursor);
                        ch = 10;
                        advance = false;
                        break;
                    }
                    if ((VCall<uint32_t>(r, 0x1ac / 4, &cursor, flags) & 0xff) == 0)
                    {
                        if (VCall<uint32_t>(r, 0x1a8 / 4, &cursor, &curX, scaleX) & 0xff)
                        {
                            if (curX > *outW)
                                *outW = curX;
                            VCall<uint32_t>(r, 0x19c / 4, &cursor, flags);
                        }
                        else if (VCall<uint32_t>(r, 0x1a4 / 4, &cursor, &curX, 0u, scaleX) & 0xff)
                        {
                            if (curX > *outW)
                                *outW = curX;
                        }
                        else
                        {
                            VCall<uint32_t>(r, 0x198 / 4, &cursor);
                        }
                    }
                } while (VCall<uint32_t>(r, 0x1b4 / 4, cursor) & 0xff);
            }
            if (*cursor == 0)
                return 0;
        }
        if (advance)
        {
            ch = *cursor;
            cursor++;
        }
        VCall<uint32_t>(r, 0x1bc / 4, &w, &h, ch, scaleX, scaleY);
        float x;
        if (static_cast<uint16_t>(ch) == 10)
        {
            if (static_cast<uint8_t>(singleLine) != 0)
                return 0;
            x = 0.0f;
            totalH = totalH + h;
        }
        else
        {
            x = curX;
        }
        curX = x + w;
        if (curX > *outW)
            *outW = curX;
        if (totalH > *outH)
            *outH = totalH;
        if (maxChars != 0)
        {
            count++;
            if (count >= maxChars)
                return 0;
        }
    } while (*cursor != 0);
    return 0;
}

static uint32_t Renderer_TextIsTagStart_Abi(uint32_t text)
{
    return Renderer_TextIsTagStart(reinterpret_cast<const uint16_t*>(text));
}
static uint32_t Renderer_TextIsBreakTag_Abi(uint32_t ecx, uint32_t text)
{
    return Renderer_TextIsBreakTag(reinterpret_cast<Renderer*>(ecx), reinterpret_cast<const uint16_t*>(text));
}
static uint32_t Renderer_TextParseLlTag_Abi(uint32_t ecx, uint32_t cursor, uint32_t out, uint32_t scaleX)
{
    return Renderer_TextParseLlTag(reinterpret_cast<Renderer*>(ecx), reinterpret_cast<const uint16_t**>(cursor),
                                   reinterpret_cast<float*>(out), scaleX);
}
static uint32_t Renderer_TextParseTabTag_Abi(uint32_t ecx, uint32_t cursor, uint32_t out, uint32_t base,
                                             uint32_t scaleX)
{
    return Renderer_TextParseTabTag(reinterpret_cast<Renderer*>(ecx), reinterpret_cast<const uint16_t**>(cursor),
                                    reinterpret_cast<float*>(out), AsFloat(base), scaleX);
}
static uint32_t Renderer_TextParseRgbTag_Abi(uint32_t ecx, uint32_t cursor, uint32_t defaultColour, uint32_t colour)
{
    return Renderer_TextParseRgbTag(reinterpret_cast<Renderer*>(ecx), reinterpret_cast<const uint16_t**>(cursor),
                                    defaultColour, reinterpret_cast<uint32_t*>(colour));
}
static uint32_t Renderer_TextSkipTag_Abi(uint32_t cursor)
{
    return Renderer_TextSkipTag(reinterpret_cast<const uint16_t**>(cursor));
}
static uint32_t Renderer_MeasureText_Abi(uint32_t ecx, uint32_t outW, uint32_t outH, uint32_t text, uint32_t scaleX,
                                         uint32_t scaleY, uint32_t singleLine, uint32_t maxChars, uint32_t flags)
{
    return Renderer_MeasureText(reinterpret_cast<Renderer*>(ecx), reinterpret_cast<float*>(outW),
                                reinterpret_cast<float*>(outH), reinterpret_cast<const uint16_t*>(text), scaleX,
                                scaleY, singleLine, maxChars, flags);
}
ABI_ENTRY_00507140(Renderer_TextIsTagStart_Entry, Renderer_TextIsTagStart_Abi)
ABI_ENTRY_00507180(Renderer_TextIsBreakTag_Entry, Renderer_TextIsBreakTag_Abi)
ABI_ENTRY_005071a0(Renderer_TextParseLlTag_Entry, Renderer_TextParseLlTag_Abi)
ABI_ENTRY_00507220(Renderer_TextParseTabTag_Entry, Renderer_TextParseTabTag_Abi)
ABI_ENTRY_00507370(Renderer_TextParseRgbTag_Entry, Renderer_TextParseRgbTag_Abi)
ABI_ENTRY_005073f0(Renderer_TextSkipTag_Entry, Renderer_TextSkipTag_Abi)
ABI_ENTRY_005074a0(Renderer_MeasureText_Entry, Renderer_MeasureText_Abi)
HOOK(0x00507140, "Renderer_TextIsTagStart", Renderer_TextIsTagStart_Entry, nullptr);
HOOK(0x00507180, "Renderer_TextIsBreakTag", Renderer_TextIsBreakTag_Entry, nullptr);
HOOK(0x005071a0, "Renderer_TextParseLlTag", Renderer_TextParseLlTag_Entry, nullptr);
HOOK(0x00507220, "Renderer_TextParseTabTag", Renderer_TextParseTabTag_Entry, nullptr);
HOOK(0x00507370, "Renderer_TextParseRgbTag", Renderer_TextParseRgbTag_Entry, nullptr);
HOOK(0x005073f0, "Renderer_TextSkipTag", Renderer_TextSkipTag_Entry, nullptr);
HOOK(0x005074a0, "Renderer_MeasureText", Renderer_MeasureText_Entry, nullptr);
