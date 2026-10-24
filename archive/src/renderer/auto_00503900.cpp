// Recovered: movie manager (".pcv"), renderer state slots, text helpers, glyph quads and window sizing.
// S2, Approach B.
//   0x00503900 MovieManager_DeletingDestructor   0x00503930 MovieManager_Destructor
//   0x005039c0 fn_005039c0                       0x005039f0 MovieManager_Open
//   0x00504d70 fn_00504d70                       0x00504e00 fn_00504e00
//   0x00506f30 Renderer_ReleaseStateCache        0x005070d0 Renderer_TextMatchTag
//   0x005072b0 Renderer_TextSkipTaggedBlock      0x00507430 Renderer_TextSkipToLineEnd
//   0x00507710 fn_00507710                       0x005077b0 VertexBatch_AddQuad
//   0x005078c0 Renderer_DrawGlyph                0x00507f50 Renderer_DrawTextA
//   0x00508010 Renderer_BeginScreenBatch         0x00508070 Renderer_EndScreenBatch
//   0x005080d0 Renderer_ResizeWindow
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
// fild of a dword, then fadd 4294967296.0 (0x005bcb28) when it was negative: the game's unsigned-to-float.
float UnsignedToFloat(uint32_t v)
{
    float f = static_cast<float>(static_cast<int32_t>(v));
    if (static_cast<int32_t>(v) < 0)
        f += 4294967296.0f;
    return f;
}
// 0x0053a1e0 is the C runtime floor (cdecl, double argument, result in st0). The generated orig wrapper only
// returns EAX and would leave st0 on the x87 stack, so this calls it directly and pops st0. ECX is listed as an
// input by the ABI scan; the original caller has the font line height (+0x5dc) in it.
double GameFloor(uint32_t ecxValue, double value)
{
    static const uint32_t target = 0x0053a1e0;
    double result;
    __asm {
        push ebx
        push esi
        push edi
        push ebp
        push dword ptr [value + 4]
        push dword ptr [value]
        mov ecx, ecxValue
        call dword ptr [target]
        add esp, 8
        pop ebp
        pop edi
        pop esi
        pop ebx
        fstp qword ptr [result]
    }
    return result;
}
const uint32_t kAllocator = 0x0063a210;  // ecx of the game's allocator 0x004e07f0 / free 0x004e08d0
}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// Movie manager, vtable 0x0058982c (slots: 0x00503900, 0x005039c0, 0x00503b10, 0x005039f0, 0x00503af0). Named
// from 0x005039f0, which only opens files with the extension ".pcv" (0x0058fd8c); the retail .pcv files are the
// GameData/movies/*.pcv cut scenes (docs/manifest.md). Base class vtable 0x0058a1f0. Singleton pointer 0x0063a0b8.

// 0x00503930 ([this], callee pops): destructor (not executed in the comparison scenarios). Sets the vtable
// 0x0058982c, deletes every child while the list head +4 is non-null (child virtual 10, +0x28, with 1; each child
// unlinks itself), clears +0x10 (the original counts it down to zero first), sets the base vtable 0x0058a1f0 and
// clears the singleton 0x0063a0b8 if it is this object. The original's SEH frame only covers unwinding and is
// not reproduced. Returns 0.
uint32_t MovieManager_Destructor(void* self)
{
    Field<uint32_t>(self, 0) = 0x0058982c;
    while (Field<void*>(self, 4) != nullptr)
    {
        if (void* child = Field<void*>(self, 4))
            VCall<uint32_t>(child, 0x28 / 4, 1u);
    }
    if (Field<uint32_t>(self, 0x10) != 0)
        Field<uint32_t>(self, 0x10) = 0;
    Field<uint32_t>(self, 0) = 0x0058a1f0;
    if (GameGlobal<void*>(0x0063a0b8) == self)
        GameGlobal<void*>(0x0063a0b8) = nullptr;
    return 0;
}

// 0x00503900 (ecx = this; [flags]): scalar deleting destructor, vtable 0x0058982c slot 0 (not executed in the
// comparison scenarios). Runs 0x00503930, then frees the object with 0x004e08d0 (ecx = 0x0063a210) when flags
// bit 0 is set. Returns this.
void* MovieManager_DeletingDestructor(void* self, uint32_t flags)
{
    MovieManager_Destructor(self);
    if (flags & 1)
        orig::fn_004e08d0(kAllocator, u(self));
    return self;
}

// 0x005039c0 (ecx = this; [argument]): movie manager virtual 1. Walks the child list (+4, next pointer at child
// +4) and, once per child, calls virtual 2 (+8) of the FIRST child (the head is re-read each time) with the
// argument. Returns the last call's result (the original leaves EAX unchanged when the list is empty; 0 here).
uint32_t fn_005039c0(void* self, uint32_t argument)
{
    uint32_t result = 0;
    for (uint8_t* node = Field<uint8_t*>(self, 4); node; node = Field<uint8_t*>(node, 4))
        result = VCall<uint32_t>(Field<void*>(self, 4), 2, argument);
    return result;
}

// 0x005039f0 (ecx = this; [path]): movie manager virtual 3, open a movie (not executed in the comparison
// scenarios). Splits the path with 0x00538456 (_splitpath-like: path, 0, 0, 0, extension buffer[256]) and only
// accepts ".pcv" (_stricmp 0x005448ee with 0x0058fd8c). Allocates 0x270 bytes (0x004e07f0: 0x10, 0x270, 2, "new"
// 0x0058b5e8), builds the movie with 0x0050dcf0 (ecx = this + 4) and opens it with 0x0050de70 (ecx = movie, called
// even when the allocation failed). If the movie's virtual 6 (+0x18) returns false, it is removed with this
// object's virtual 4 (+0x10) and null is returned. The SEH frame and stack cookie are not reproduced.
uint32_t MovieManager_Open(void* self, const char* path)
{
    char extension[256];
    orig::fn_00538456(u(self), u(path), 0, 0, 0, u(extension));
    if (orig::fn_005448ee(u(extension), u(extension), 0x0058fd8c) != 0)
        return 0;
    uint32_t memory = orig::fn_004e07f0(kAllocator, 0x10, 0x270, 2, 0x0058b5e8);
    uint32_t movie = memory ? orig::fn_0050dcf0(u(self) + 4, memory) : 0;
    orig::fn_0050de70(movie, u(path));
    if (movie && (VCall<uint32_t>(reinterpret_cast<void*>(movie), 0x18 / 4) & 0xff) == 0)
    {
        VCall<uint32_t>(self, 0x10 / 4, movie);
        return 0;
    }
    return movie;
}

static uint32_t MovieManager_DeletingDestructor_Abi(uint32_t ecx, uint32_t flags)
{
    return u(MovieManager_DeletingDestructor(reinterpret_cast<void*>(ecx), flags));
}
static uint32_t MovieManager_Destructor_Abi(uint32_t self)
{
    return MovieManager_Destructor(reinterpret_cast<void*>(self));
}
static uint32_t fn_005039c0_Abi(uint32_t ecx, uint32_t argument)
{
    return fn_005039c0(reinterpret_cast<void*>(ecx), argument);
}
static uint32_t MovieManager_Open_Abi(uint32_t ecx, uint32_t path)
{
    return MovieManager_Open(reinterpret_cast<void*>(ecx), reinterpret_cast<const char*>(path));
}
ABI_ENTRY_00503900(MovieManager_DeletingDestructor_Entry, MovieManager_DeletingDestructor_Abi)
ABI_ENTRY_00503930(MovieManager_Destructor_Entry, MovieManager_Destructor_Abi)
ABI_ENTRY_005039c0(fn_005039c0_Entry, fn_005039c0_Abi)
ABI_ENTRY_005039f0(MovieManager_Open_Entry, MovieManager_Open_Abi)
HOOK(0x00503900, "MovieManager_DeletingDestructor", MovieManager_DeletingDestructor_Entry, nullptr);
HOOK(0x00503930, "MovieManager_Destructor", MovieManager_Destructor_Entry, nullptr);
HOOK(0x005039c0, "fn_005039c0", fn_005039c0_Entry, nullptr);
HOOK(0x005039f0, "MovieManager_Open", MovieManager_Open_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// Renderer state slots: four 0x50-byte records at +0x5f8 (+0x00 serial number, +0x04 a 17-dword block like the
// matrix blocks of virtuals 19/20, +0x48 a packed dword).

// 0x00504d70 (ecx = renderer; [block of 17 dwords, slot]): renderer virtual 22 (+0x58). If the block differs from
// record[slot] +0x04 (repe cmpsd), call virtual 9 (+0x24, flush), give the record a fresh serial number from the
// counter +0x2d4 (pre-increment), copy the 17 dwords and set +0x3b0 = -1. Returns the new serial number, or
// slot * 0x50 when nothing changed (what EAX held).
uint32_t fn_00504d70(Renderer* r, const uint32_t* block, uint32_t slot)
{
    uint32_t offset = slot * 0x50;
    uint32_t* cached = &Field<uint32_t>(r, offset + 0x5fc);
    bool same = true;
    for (int i = 0; i < 0x11; i++)
    {
        if (cached[i] != block[i])
        {
            same = false;
            break;
        }
    }
    if (same)
        return offset;
    VCall<uint32_t>(r, 0x24 / 4);
    uint32_t serial = ++Field<uint32_t>(r, 0x2d4);
    Field<uint32_t>(r, offset + 0x5f8) = serial;
    for (int i = 0; i < 0x11; i++)
        cached[i] = block[i];
    Field<uint32_t>(r, 0x3b0) = 0xffffffff;
    return serial;
}

// 0x00504e00 (ecx = renderer; [a, b, c, d, slot]): renderer virtual 41 (+0xa4). Packs
// c << 24 | (b & 0xff) << 16 | (a & 0xff) << 8 | (d & 0xff) (c is not masked) and, if it differs from
// record[slot] +0x48 (+0x640 + slot * 0x50), calls virtual 9 (+0x24, flush), stores it and sets +0x3b0 = -1.
// Returns virtual 9's result, or the unchanged cached value.
uint32_t fn_00504e00(Renderer* r, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t slot)
{
    uint32_t packed = (((((c << 8) | (b & 0xff)) << 8) | (a & 0xff)) << 8) | (d & 0xff);
    uint32_t& cached = Field<uint32_t>(r, (slot * 5 + 0x64) * 16);
    if (cached == packed)
        return cached;
    uint32_t result = VCall<uint32_t>(r, 0x24 / 4);
    cached = packed;
    Field<uint32_t>(r, 0x3b0) = 0xffffffff;
    return result;
}

static uint32_t fn_00504d70_Abi(uint32_t ecx, uint32_t block, uint32_t slot)
{
    return fn_00504d70(reinterpret_cast<Renderer*>(ecx), reinterpret_cast<const uint32_t*>(block), slot);
}
static uint32_t fn_00504e00_Abi(uint32_t ecx, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t slot)
{
    return fn_00504e00(reinterpret_cast<Renderer*>(ecx), a, b, c, d, slot);
}
ABI_ENTRY_00504d70(fn_00504d70_Entry, fn_00504d70_Abi)
ABI_ENTRY_00504e00(fn_00504e00_Entry, fn_00504e00_Abi)
HOOK(0x00504d70, "fn_00504d70", fn_00504d70_Entry, nullptr);
HOOK(0x00504e00, "fn_00504e00", fn_00504e00_Entry, nullptr);

// 0x00506f30 (esi = renderer): counterpart of Renderer_InitStateCache (0x00506ed0), called from 0x004ffd40 and
// the device shutdown 0x00509920 (not executed in the comparison scenarios). Destroys the objects at +0x58c and
// +0x580 (their virtual 0, no arguments) and clears the pointers, frees +0x5d8 with 0x004e08d0, then zeroes the
// 0x51 dwords at +0x4b0 (saved transforms and batch state). Returns 0.
uint32_t Renderer_ReleaseStateCache(Renderer* r)
{
    if (void* value = Field<void*>(r, 0x58c))
    {
        VCall<uint32_t>(value, 0);
        Field<uint32_t>(r, 0x58c) = 0;
    }
    if (void* cache = Field<void*>(r, 0x580))
    {
        VCall<uint32_t>(cache, 0);
        Field<uint32_t>(r, 0x580) = 0;
    }
    if (uint32_t block = Field<uint32_t>(r, 0x5d8))
    {
        orig::fn_004e08d0(kAllocator, block);
        Field<uint32_t>(r, 0x5d8) = 0;
    }
    memset(&Field<uint8_t>(r, 0x4b0), 0, 0x51 * 4);
    return 0;
}

static uint32_t Renderer_ReleaseStateCache_Abi(uint32_t esi)
{
    return Renderer_ReleaseStateCache(reinterpret_cast<Renderer*>(esi));
}
ABI_ENTRY_00506f30(Renderer_ReleaseStateCache_Entry, Renderer_ReleaseStateCache_Abi)
HOOK(0x00506f30, "Renderer_ReleaseStateCache", Renderer_ReleaseStateCache_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// Text markup helpers (renderer virtuals 102-115, 16-bit characters). The tag tables are 0x005fb7d8
// {"<short>", "</short>"} and 0x005fb7e0 {"<long>", "</long>"}.

// 0x005070d0 ([text, tag], callee pops, ECX unused): renderer virtual 110 (+0x1b8). Case-insensitive (ASCII
// letters, xor 0x20) test that the 16-bit text starts with the tag. Original quirks kept: it is true only when the
// text is LONGER than the tag (text ending together with the tag gives false), and for empty text it tests the
// low word of the text POINTER, not a character.
bool Renderer_TextMatchTag(const uint16_t* text, const uint16_t* tag)
{
    uint16_t a = *text;
    if (a == 0)
        return (reinterpret_cast<uintptr_t>(text) & 0xffff) == 0;
    for (;;)
    {
        uint16_t c = *tag;
        text++;
        tag++;
        if (c == 0)
            return true;
        if (a != c)
        {
            bool letter = (a >= 0x61 && a <= 0x7a) || (a >= 0x41 && a <= 0x5a);
            if (!letter || static_cast<uint16_t>(a ^ 0x20) != c)
                return false;
        }
        a = *text;
        if (a == 0)
            return c == 0;
    }
}

// 0x005072b0 (ecx = renderer; [cursor, flags]): renderer virtual 107 (+0x1ac). With flags bit 3 the tags are
// "<long>"/"</long>", else "<short>"/"</short>". If the text at *cursor does not start with the open tag
// (virtual 110), returns false. Otherwise skips it (virtual 102, +0x198) and walks the text: at a tag start
// (virtual 109, +0x1b4) it tries a nested block (virtual 107); if that fails, the close tag (virtual 110) ends
// the block (skipped with virtual 102, return true), any other tag is skipped with virtual 102; other characters
// advance the cursor by one. Returns true at the end of the text. The upper bytes of EAX are the last call's.
uint32_t Renderer_TextSkipTaggedBlock(Renderer* r, const uint16_t** cursor, uint32_t flags)
{
    auto tags = reinterpret_cast<const uint16_t* const*>((flags & 8) ? 0x005fb7e0 : 0x005fb7d8);
    uint32_t result = VCall<uint32_t>(r, 0x1b8 / 4, *cursor, tags[0]);
    if ((result & 0xff) == 0)
        return result;
    result = VCall<uint32_t>(r, 0x198 / 4, cursor);
    while (**cursor != 0)
    {
        result = VCall<uint32_t>(r, 0x1b4 / 4, *cursor);
        if ((result & 0xff) == 0)
        {
            *cursor += 1;
            continue;
        }
        result = VCall<uint32_t>(r, 0x1ac / 4, cursor, flags);
        if ((result & 0xff) != 0)
            continue;
        bool closing = (VCall<uint32_t>(r, 0x1b8 / 4, *cursor, tags[1]) & 0xff) != 0;
        result = VCall<uint32_t>(r, 0x198 / 4, cursor);
        if (closing)
            break;
    }
    return (result & 0xffffff00) | 1;
}

// 0x00507430 (ecx = renderer; [cursor, flags]): renderer virtual 103 (+0x19c). Advances *cursor to the end of
// the text or the next '\n', skipping markup: at a tag start (virtual 109) it stops when virtual 108 (+0x1b0)
// accepts the tag (returning that result), else tries virtual 107 (tagged block) and skips an unknown tag with
// virtual 102. Returns what EAX last held (the cursor or the last call's result).
uint32_t Renderer_TextSkipToLineEnd(Renderer* r, const uint16_t** cursor, uint32_t flags)
{
    uint32_t result = u(*cursor);
    if (**cursor == 0)
        return result;
    do
    {
        result = u(*cursor);
        if (**cursor == 0x0a)
            return result;
        result = VCall<uint32_t>(r, 0x1b4 / 4, *cursor);
        if ((result & 0xff) == 0)
        {
            *cursor += 1;
            continue;
        }
        result = VCall<uint32_t>(r, 0x1b0 / 4, *cursor);
        if ((result & 0xff) != 0)
            return result;
        result = VCall<uint32_t>(r, 0x1ac / 4, cursor, flags);
        if ((result & 0xff) == 0)
            result = VCall<uint32_t>(r, 0x198 / 4, cursor);
    } while (**cursor != 0);
    return result;
}

// Copies an 8-bit string into a new 16-bit one (sign-extending each byte, movsx) of `length` characters, or
// strlen when length is 0, allocated with 0x004e07f0 (1, length * 2 + 2, 2, "a_to_s" 0x00589640).
static uint16_t* WidenAscii(const char* text, int32_t length)
{
    if (length == 0)
        length = static_cast<int32_t>(strlen(text));
    auto wide = reinterpret_cast<uint16_t*>(
        orig::fn_004e07f0(kAllocator, 1, static_cast<uint32_t>(length * 2 + 2), 2, 0x00589640));
    int32_t i = 0;
    for (; i < length; i++)
        wide[i] = static_cast<uint16_t>(static_cast<int16_t>(static_cast<int8_t>(text[i])));
    wide[i] = 0;
    return wide;
}

// 0x00507710 (ecx = renderer; 8 arguments): renderer virtual 113 (+0x1c4), the 8-bit version of virtual 112
// (+0x1c0, 0x005074a0, a text layout routine using virtual 111) (not executed in the comparison scenarios). Widens
// the third argument (length = seventh argument, or strlen when 0), calls virtual 112 with the same arguments and
// the wide string (the original length argument is passed through unchanged), frees the copy. Returns 0.
uint32_t fn_00507710(Renderer* r, uint32_t a0, uint32_t a1, const char* text, uint32_t a3, uint32_t a4,
                     uint32_t a5, int32_t length, uint32_t a7)
{
    uint16_t* wide = WidenAscii(text, length);
    VCall<uint32_t>(r, 0x1c0 / 4, a0, a1, wide, a3, a4, a5, length, a7);
    orig::fn_004e08d0(kAllocator, u(wide));
    return 0;
}

static uint32_t Renderer_TextMatchTag_Abi(uint32_t text, uint32_t tag)
{
    return Renderer_TextMatchTag(reinterpret_cast<const uint16_t*>(text), reinterpret_cast<const uint16_t*>(tag))
               ? 1
               : 0;
}
static uint32_t Renderer_TextSkipTaggedBlock_Abi(uint32_t ecx, uint32_t cursor, uint32_t flags)
{
    return Renderer_TextSkipTaggedBlock(reinterpret_cast<Renderer*>(ecx), reinterpret_cast<const uint16_t**>(cursor),
                                        flags);
}
static uint32_t Renderer_TextSkipToLineEnd_Abi(uint32_t ecx, uint32_t cursor, uint32_t flags)
{
    return Renderer_TextSkipToLineEnd(reinterpret_cast<Renderer*>(ecx), reinterpret_cast<const uint16_t**>(cursor),
                                      flags);
}
static uint32_t fn_00507710_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4,
                                uint32_t a5, uint32_t a6, uint32_t a7)
{
    return fn_00507710(reinterpret_cast<Renderer*>(ecx), a0, a1, reinterpret_cast<const char*>(a2), a3, a4, a5,
                       static_cast<int32_t>(a6), a7);
}
ABI_ENTRY_005070d0(Renderer_TextMatchTag_Entry, Renderer_TextMatchTag_Abi)
ABI_ENTRY_005072b0(Renderer_TextSkipTaggedBlock_Entry, Renderer_TextSkipTaggedBlock_Abi)
ABI_ENTRY_00507430(Renderer_TextSkipToLineEnd_Entry, Renderer_TextSkipToLineEnd_Abi)
ABI_ENTRY_00507710(fn_00507710_Entry, fn_00507710_Abi)
HOOK(0x005070d0, "Renderer_TextMatchTag", Renderer_TextMatchTag_Entry, nullptr);
HOOK(0x005072b0, "Renderer_TextSkipTaggedBlock", Renderer_TextSkipTaggedBlock_Entry, nullptr);
HOOK(0x00507430, "Renderer_TextSkipToLineEnd", Renderer_TextSkipToLineEnd_Entry, nullptr);
HOOK(0x00507710, "fn_00507710", fn_00507710_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// Glyph quads (font layout: docs/formats/big-font.md).

// 0x005077b0 (ecx = vertex batch, ebx = colour, esi = corner A, edi = corner B; [z, dx, dy], caller pops): reserve
// 6 vertices with the batch's virtual 1 (+4, argument 6) and write two triangles of a quad. Vertex = 24 bytes
// {x, y, z, colour, u, v}; a corner is {x, y, u, v}. Positions get dx/dy added; z, colour and uv are copied as
// bits. Order: (B.x, A.y), (A.x, B.y), (B.x, B.y), (B.x, A.y), (A.x, A.y), (A.x, B.y). Returns the vertices.
float* VertexBatch_AddQuad(void* batch, uint32_t colour, const float* a, const float* b, uint32_t z, float dx,
                           float dy)
{
    float* v = VCall<float*>(batch, 1, 6u);
    const uint32_t* ab = reinterpret_cast<const uint32_t*>(a);
    const uint32_t* bb = reinterpret_cast<const uint32_t*>(b);
    const float* xs[6] = {b, a, b, b, a, a};
    const float* ys[6] = {a, b, b, a, a, b};
    for (int i = 0; i < 6; i++)
    {
        float* vertex = v + i * 6;
        uint32_t* bits = reinterpret_cast<uint32_t*>(vertex);
        vertex[0] = dx + xs[i][0];
        vertex[1] = dy + ys[i][1];
        bits[2] = z;
        bits[3] = colour;
        bits[4] = (xs[i] == a ? ab : bb)[2];
        bits[5] = (ys[i] == a ? ab : bb)[3];
    }
    return v;
}

// 0x005078c0 (eax = character; [renderer, colour, &x, &y, z, alignX, alignY, scaleX, scaleY, pixelW, pixelH,
// flags], callee pops 0x30): draw one glyph, called by virtual 114 (0x00507c10). Only with a font (+0x5f0 glyph
// table). pixelW/pixelH of 0.0 come from virtuals 32/33 (+0x80/+0x84, argument 0); scaleX *= pixelW,
// scaleY *= pixelH, alignY = 1 - alignY. Characters outside [+0x5e8, +0x5ea] (unsigned 16-bit) only store x and
// y back. If alignX or alignY is non-zero, virtual 111 (+0x1bc: &w, &h, character, scaleX, scaleY) gives a size
// and x -= w * alignX, y += h * alignY. Glyph (24 bytes): uv0/uv1 floats, x_offset +0x10, width +0x12, advance
// +0x14 (signed 16-bit). left = x_offset * scaleX + x, right = (width + 1) * scaleX + left,
// bottom = y - (line height +0x5dc + 1) * scaleY, new x = advance * scaleX + left. Positions snap to the pixel
// grid: subtract frac(left / pixelW) * pixelW + scaleX / 2 from left/right (floor 0x0053a1e0) and
// frac(y / pixelH) * pixelH + scaleY / 2 from top (= y) and bottom. uv = uv * (texture size - 1) / texture size
// (+0x5e0 width, +0x5e4 height), v flipped (1 - v). Flags bit 1 first draws a black (0xff000000) shadow offset
// by (2 * pixelW, -2 * pixelH). Quads go to the batch +0x588 (0x005077b0). Stores x and y back; returns 0.
uint32_t Renderer_DrawGlyph(uint32_t character, Renderer* r, uint32_t colour, float* penX, float* penY, uint32_t z,
                            float alignX, float alignY, float scaleX, float scaleY, float pixelW, float pixelH,
                            uint32_t flags)
{
    const uint8_t* glyphs = Field<const uint8_t*>(r, 0x5f0);
    if (!glyphs)
        return 0;
    if (pixelW == 0.0f)
        pixelW = VCall<float>(r, 0x80 / 4, 0u);
    if (pixelH == 0.0f)
        pixelH = VCall<float>(r, 0x84 / 4, 0u);
    uint16_t ch = static_cast<uint16_t>(character);
    scaleX = scaleX * pixelW;
    float x = *penX;
    float y = *penY;
    scaleY = scaleY * pixelH;
    alignY = 1.0f - alignY;
    uint16_t first = Field<uint16_t>(r, 0x5e8);
    if (ch >= first && ch <= Field<uint16_t>(r, 0x5ea))
    {
        if (!(alignX == 0.0f) || !(alignY == 0.0f))
        {
            float w, h;
            VCall<uint32_t>(r, 0x1bc / 4, &w, &h, character, scaleX, scaleY);
            x = x - w * alignX;
            y = h * alignY + y;
        }
        // The original re-reads the first character and the glyph table after virtual 111.
        const uint8_t* glyph = Field<const uint8_t*>(r, 0x5f0) +
                               (static_cast<uint32_t>(ch) - Field<uint16_t>(r, 0x5e8)) * 24;
        int32_t xOffset = Field<int16_t>(glyph, 0x10);
        int32_t width = Field<int16_t>(glyph, 0x12);
        uint32_t lineHeight = Field<uint32_t>(r, 0x5dc);
        int32_t advance = Field<int16_t>(glyph, 0x14);
        float left = static_cast<float>(xOffset) * scaleX + x;
        float right = (static_cast<float>(width) + 1.0f) * scaleX + left;
        float bottom = y - (UnsignedToFloat(lineHeight) + 1.0f) * scaleY;
        x = static_cast<float>(advance) * scaleX + left;

        float q = left / pixelW;
        float snap = (q - static_cast<float>(GameFloor(lineHeight, q))) * pixelW + scaleX * 0.5f;
        left = left - snap;
        right = right - snap;
        q = y / pixelH;
        snap = (q - static_cast<float>(GameFloor(lineHeight, q))) * pixelH + scaleY * 0.5f;
        float top = y - snap;
        bottom = bottom - snap;

        uint32_t texW = Field<uint32_t>(r, 0x5e0);
        uint32_t texH = Field<uint32_t>(r, 0x5e4);
        float wM1 = UnsignedToFloat(texW - 1);
        float wf = UnsignedToFloat(texW);
        const float* uv = reinterpret_cast<const float*>(glyph);
        float u0 = wM1 * uv[0] / wf;
        float hM1 = UnsignedToFloat(texH - 1);
        float hf = UnsignedToFloat(texH);
        float v0 = 1.0f - hM1 * uv[1] / hf;
        float u1 = wM1 * uv[2] / wf;
        float v1 = 1.0f - hM1 * uv[3] / hf;

        float cornerA[4] = {right, bottom, u1, v1};
        float cornerB[4] = {left, top, u0, v0};
        void* batch = Field<void*>(r, 0x588);
        if (flags & 2)
            VertexBatch_AddQuad(batch, 0xff000000, cornerA, cornerB, z, pixelW + pixelW, pixelH * -2.0f);
        VertexBatch_AddQuad(batch, colour, cornerA, cornerB, z, 0.0f, 0.0f);
    }
    *penX = x;
    *penY = y;
    return 0;
}

// 0x00507f50 (ecx = renderer; [text, 12 more arguments]): renderer virtual 115 (+0x1cc), the 8-bit version of
// virtual 114 (+0x1c8, 0x00507c10, which draws glyphs with 0x005078c0) (not executed in the comparison
// scenarios). Widens the text (length = twelfth argument, or strlen when 0; "a_to_s" allocation), calls virtual
// 114 with the wide text and the other arguments unchanged, frees the copy. Returns 0.
uint32_t Renderer_DrawTextA(Renderer* r, const char* text, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4,
                            uint32_t a5, uint32_t a6, uint32_t a7, uint32_t a8, uint32_t a9, uint32_t a10,
                            int32_t length, uint32_t a12)
{
    uint16_t* wide = WidenAscii(text, length);
    VCall<uint32_t>(r, 0x1c8 / 4, wide, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, length, a12);
    orig::fn_004e08d0(kAllocator, u(wide));
    return 0;
}

static uint32_t VertexBatch_AddQuad_Abi(uint32_t ecx, uint32_t ebx, uint32_t esi, uint32_t edi, uint32_t z,
                                        uint32_t dx, uint32_t dy)
{
    return u(VertexBatch_AddQuad(reinterpret_cast<void*>(ecx), ebx, reinterpret_cast<const float*>(esi),
                                 reinterpret_cast<const float*>(edi), z, AsFloat(dx), AsFloat(dy)));
}
static uint32_t Renderer_DrawGlyph_Abi(uint32_t eax, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3,
                                       uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7, uint32_t a8, uint32_t a9,
                                       uint32_t a10, uint32_t a11)
{
    return Renderer_DrawGlyph(eax, reinterpret_cast<Renderer*>(a0), a1, reinterpret_cast<float*>(a2),
                              reinterpret_cast<float*>(a3), a4, AsFloat(a5), AsFloat(a6), AsFloat(a7), AsFloat(a8),
                              AsFloat(a9), AsFloat(a10), a11);
}
static uint32_t Renderer_DrawTextA_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3,
                                       uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7, uint32_t a8, uint32_t a9,
                                       uint32_t a10, uint32_t a11, uint32_t a12)
{
    return Renderer_DrawTextA(reinterpret_cast<Renderer*>(ecx), reinterpret_cast<const char*>(a0), a1, a2, a3, a4,
                              a5, a6, a7, a8, a9, a10, static_cast<int32_t>(a11), a12);
}
ABI_ENTRY_005077b0(VertexBatch_AddQuad_Entry, VertexBatch_AddQuad_Abi)
ABI_ENTRY_005078c0(Renderer_DrawGlyph_Entry, Renderer_DrawGlyph_Abi)
ABI_ENTRY_00507f50(Renderer_DrawTextA_Entry, Renderer_DrawTextA_Abi)
HOOK(0x005077b0, "VertexBatch_AddQuad", VertexBatch_AddQuad_Entry, nullptr);
HOOK(0x005078c0, "Renderer_DrawGlyph", Renderer_DrawGlyph_Entry, nullptr);
HOOK(0x00507f50, "Renderer_DrawTextA", Renderer_DrawTextA_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// Screen-space batch: +0x580 the batch owner (created by Renderer_InitStateCache), +0x584 the active one, +0x588
// the vertex batch it returned, +0x57c "keep transforms", +0x4b0 three saved 0x44-byte transform blocks.

// 0x00508010 (ecx = renderer; [keep transforms]): renderer virtual 100 (+0x190). Unless a batch is active
// (+0x584): without "keep transforms", 0x004f0170(renderer, renderer + 0x4b0) and 0x004f01c0(renderer) (the
// blocks restored by 0x00508070 through the transform virtuals 19-21); then active = +0x580, +0x57c = the
// argument, +0x588 = +0x580's virtual 5 (+0x14) with (0, 2, 0, 0). Returns 0.
uint32_t Renderer_BeginScreenBatch(Renderer* r, uint32_t keepTransforms)
{
    if (Field<uint32_t>(r, 0x584) != 0)
        return 0;
    if (keepTransforms == 0)
    {
        orig::fn_004f0170(u(r), u(r) + 0x4b0);
        orig::fn_004f01c0(u(r));
    }
    void* owner = Field<void*>(r, 0x580);
    Field<void*>(r, 0x584) = owner;
    Field<uint32_t>(r, 0x57c) = keepTransforms;
    Field<uint32_t>(r, 0x588) = VCall<uint32_t>(owner, 0x14 / 4, 0u, 2u, 0u, 0u);
    return 0;
}

// 0x00508070 (ecx = renderer): renderer virtual 101 (+0x194). If a batch is active (+0x584): its virtual 7
// (+0x1c, no arguments); unless +0x57c (keep transforms), restore the saved blocks with virtual 21 (+0x54,
// +0x4b0), virtual 20 (+0x50, +0x4f4) and virtual 19 (+0x4c, Renderer_SetProjection, +0x538); clear +0x584.
// Returns 0.
uint32_t Renderer_EndScreenBatch(Renderer* r)
{
    void* active = Field<void*>(r, 0x584);
    if (!active)
        return 0;
    VCall<uint32_t>(active, 0x1c / 4);
    if (Field<uint32_t>(r, 0x57c) == 0)
    {
        uint8_t* saved = &Field<uint8_t>(r, 0x4b0);
        VCall<uint32_t>(r, 0x54 / 4, saved);
        VCall<uint32_t>(r, 0x50 / 4, saved + 0x44);
        VCall<uint32_t>(r, 0x4c / 4, saved + 0x88);
    }
    Field<uint32_t>(r, 0x584) = 0;
    return 0;
}

static uint32_t Renderer_BeginScreenBatch_Abi(uint32_t ecx, uint32_t keepTransforms)
{
    return Renderer_BeginScreenBatch(reinterpret_cast<Renderer*>(ecx), keepTransforms);
}
static uint32_t Renderer_EndScreenBatch_Abi(uint32_t ecx)
{
    return Renderer_EndScreenBatch(reinterpret_cast<Renderer*>(ecx));
}
ABI_ENTRY_00508010(Renderer_BeginScreenBatch_Entry, Renderer_BeginScreenBatch_Abi)
ABI_ENTRY_00508070(Renderer_EndScreenBatch_Entry, Renderer_EndScreenBatch_Abi)
HOOK(0x00508010, "Renderer_BeginScreenBatch", Renderer_BeginScreenBatch_Entry, nullptr);
HOOK(0x00508070, "Renderer_EndScreenBatch", Renderer_EndScreenBatch_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------

// 0x005080d0 (esi = renderer; [resize, width, height]): called by Renderer_SetupPresentParams (0x00508ce0) and
// Renderer_CreateDevice (0x00508e80). Stores the device window's (+0x1484) style and extended style
// (GetWindowLongA -16/-20) at +0x9e0/+0x9e4. If the low byte of resize is set: width/height default to
// +0x1634/+0x1638 when 0; in windowed mode (+0x1628 bit 5) the client area is centred on the current client rect
// (signed halves), else placed at 0,0; AdjustWindowRectEx (no menu) and SetWindowPos(insert after 0, ..., 0x540 =
// NOSENDCHANGING | FRAMECHANGED | SHOWWINDOW). Then GetWindowRect into +0x9c0 and GetClientRect into
// +0x9d0; returns GetClientRect's result. Win32 calls go through the game's import table (0x0056b2xx).
uint32_t Renderer_ResizeWindow(Renderer* r, uint32_t resize, int32_t width, int32_t height)
{
    using GetWindowLongFn = LONG(__stdcall*)(HWND, int);
    using GetRectFn = BOOL(__stdcall*)(HWND, LPRECT);
    using AdjustFn = BOOL(__stdcall*)(LPRECT, DWORD, BOOL, DWORD);
    using SetWindowPosFn = BOOL(__stdcall*)(HWND, HWND, int, int, int, int, UINT);
    auto getWindowLong = *reinterpret_cast<GetWindowLongFn*>(0x0056b240);
    auto getClientRect = *reinterpret_cast<GetRectFn*>(0x0056b250);
    auto adjustWindowRectEx = *reinterpret_cast<AdjustFn*>(0x0056b234);
    auto setWindowPos = *reinterpret_cast<SetWindowPosFn*>(0x0056b238);
    auto getWindowRect = *reinterpret_cast<GetRectFn*>(0x0056b254);

    HWND window = r->deviceWindow;
    Field<LONG>(r, 0x9e0) = getWindowLong(window, -16);
    Field<LONG>(r, 0x9e4) = getWindowLong(r->deviceWindow, -20);
    if (static_cast<uint8_t>(resize) != 0)
    {
        if (width == 0)
            width = Field<int32_t>(r, 0x1634);
        if (height == 0)
            height = Field<int32_t>(r, 0x1638);
        RECT rect;
        if (Field<uint8_t>(r, 0x1628) & 0x20)
        {
            getClientRect(r->deviceWindow, &rect);
            rect.left = (rect.right + rect.left) / 2 - width / 2;
            rect.top = (rect.bottom + rect.top) / 2 - height / 2;
        }
        else
        {
            rect.left = 0;
            rect.top = 0;
        }
        rect.right = rect.left + width;
        rect.bottom = rect.top + height;
        adjustWindowRectEx(&rect, Field<DWORD>(r, 0x9e0), FALSE, Field<DWORD>(r, 0x9e4));
        setWindowPos(r->deviceWindow, nullptr, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
                     0x540);
    }
    getWindowRect(r->deviceWindow, &Field<RECT>(r, 0x9c0));
    return getClientRect(r->deviceWindow, &Field<RECT>(r, 0x9d0));
}

static uint32_t Renderer_ResizeWindow_Abi(uint32_t esi, uint32_t resize, uint32_t width, uint32_t height)
{
    return Renderer_ResizeWindow(reinterpret_cast<Renderer*>(esi), resize, static_cast<int32_t>(width),
                                 static_cast<int32_t>(height));
}
ABI_ENTRY_005080d0(Renderer_ResizeWindow_Entry, Renderer_ResizeWindow_Abi)
HOOK(0x005080d0, "Renderer_ResizeWindow", Renderer_ResizeWindow_Entry, nullptr);
