// Recovered: movie-classified game functions, batch movie_01 (0x0050dcf0-0x0050e770): the DivX movie player glue.
// Static recovery 2026-10-05, compile-checked only (the game was not run); the oracle (tools/compare.py) is still to
// do. None of these functions is executed in the recorded runs (the test scenarios run with [test] no_movies=1).
//
// The movie object (vtable 0x0057d598, 11 slots, no class-name descriptor: slot 1 is 0x0050e4e0) derives from a
// list-node base (vtable 0x0058a204, pure virtual slots): +4 next node, +8 address of the pointer that points at this
// node (the list head or the previous node's +4). Members, from the DivX calls below:
//   +0x0c flags: 1 ended, 2 paused (guess: SetPaused 0x0050e490), 4 error, 8 looped once, 0x10 loop
//   +0x10..+0xeb state block saved/restored by 0x004fb860 / 0x004fb920 (renderer state save, see auto_004fb170.cpp)
//   +0xec file path (_makepath result, passed to OpenDivxFile)
//   +0x1f0 texture descriptor (9 dwords: +0x1f0 name = +0xec, +0x200 width, +0x204 height, +0x210 byte 0x7f,
//          +0x212 word 0x41; bit 0x80 of byte +0x213 = texture created)
//   +0x214 DivX file handle; +0x218 frame-time accumulator; +0x21c seconds per frame (1 / fps);
//   +0x220 audio playback-rate factor (1.0 = nominal); +0x224 audio bytes per second
//   +0x228 DivX content info (DivxGetContentInfo): +0x228 width, +0x22c height, +0x230 double fps,
//          +0x238 frame count (guess), +0x23c current frame (guess), +0x244 channels, +0x248 sample rate,
//          +0x24c bits per sample, +0x258 has-audio (guess)
//   +0x260 RGBA frame buffer ("Movie buffer", width*height*4); +0x264 audio memory streamer (0x004e5ae0, vtable
//   0x0058b178); +0x268 audio stream handle; +0x26c PC sound system (AudioPC_Acquire 0x004f9c10, vtable 0x0058a230)
//
// DivX (DivxMediaLib.dll) is called through the game's import address table slots, as the original does with
// `call dword ptr [IAT]`, so import patches (hook/src/testscenario.cpp OpenDivxFile for no_movies) still apply.
// All DivX imports are cdecl (callers pop).
// The renderer object used for textures is (*(0x0063a0b8))+8 (singleton 0x004fba30): virtuals +0x124 create texture,
// +0x128 release texture, +0x13c lock (desc, &bits, &pitch), +0x144 unlock (names are guesses from use).
#include <cstring>

#include "game.h"
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
inline float bf(uint32_t v)
{
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}
inline float K(uint32_t address)
{
    return GameGlobal<float>(address);
}

// DivxMediaLib.dll imports, read from the IAT at call time.
using DivxFn1 = uint32_t(__cdecl*)(uint32_t);
using DivxFn2 = uint32_t(__cdecl*)(uint32_t, uint32_t);
using DivxFn3 = uint32_t(__cdecl*)(uint32_t, uint32_t, uint32_t);
using DivxFn4 = uint32_t(__cdecl*)(uint32_t, uint32_t, uint32_t, uint32_t);
inline uint32_t DivxGotoFrame(uint32_t h, uint32_t frame) { return (*reinterpret_cast<DivxFn2*>(0x0056b020))(h, frame); }
inline uint32_t DivxDoFrame(uint32_t h) { return (*reinterpret_cast<DivxFn1*>(0x0056b024))(h); }
inline uint32_t OpenDivxFile(uint32_t path) { return (*reinterpret_cast<DivxFn1*>(0x0056b028))(path); }
inline uint32_t DivxSetNumAudVidBuffers(uint32_t a, uint32_t b, uint32_t c)
{
    return (*reinterpret_cast<DivxFn3*>(0x0056b02c))(a, b, c);
}
inline uint32_t DivxNextFrame(uint32_t h) { return (*reinterpret_cast<DivxFn1*>(0x0056b030))(h); }
inline uint32_t DivxGetLastError(uint32_t h, uint32_t out) { return (*reinterpret_cast<DivxFn2*>(0x0056b034))(h, out); }
inline uint32_t DivxFormatErrorMessage(uint32_t h, uint32_t err, uint32_t buf, uint32_t size)
{
    return (*reinterpret_cast<DivxFn4*>(0x0056b038))(h, err, buf, size);
}
inline uint32_t DivxGetContentInfo(uint32_t h, uint32_t out) { return (*reinterpret_cast<DivxFn2*>(0x0056b03c))(h, out); }
inline uint32_t DivxSetOutputVideoFormat(uint32_t h, uint32_t f) { return (*reinterpret_cast<DivxFn2*>(0x0056b040))(h, f); }
inline uint32_t DivxModeSet(uint32_t h, uint32_t m) { return (*reinterpret_cast<DivxFn2*>(0x0056b044))(h, m); }
inline uint32_t DivxSetVideoBuffer(uint32_t h, uint32_t b) { return (*reinterpret_cast<DivxFn2*>(0x0056b048))(h, b); }
inline uint32_t CloseDivxFile(uint32_t h) { return (*reinterpret_cast<DivxFn1*>(0x0056b04c))(h); }
inline uint32_t DivxSetAudioBuffer(uint32_t h, uint32_t b) { return (*reinterpret_cast<DivxFn2*>(0x0056b050))(h, b); }
inline uint32_t DivxNextAudioChunk(uint32_t h, uint32_t out) { return (*reinterpret_cast<DivxFn2*>(0x0056b054))(h, out); }

// Texture/renderer object: *(0x0063a0b8) + 8 (read directly, as 0x0050e060 / 0x0050e620 do).
inline void* MovieRenderer()
{
    return Field<void*>(P(GameGlobal<uint32_t>(0x0063a0b8)), 8);
}

// x87 sequences whose rounding differs from a C++ conversion (unsigned int -> float via fadd 2^32 at single
// precision, then a second rounding in the division): reproduced instruction for instruction.
// fild [i]; (i < 0: fadd qword [0x005bcb20] = 2^32); fdiv qword [d]   (0x0050e750)
float UnsignedOverDouble(const int32_t* i, const double* d)
{
    float r;
    __asm {
        mov eax, i
        mov edx, d
        fild dword ptr [eax]
        cmp dword ptr [eax], 0
        jge positive
        mov ecx, 0x005bcb20
        fadd qword ptr [ecx]
    positive:
        fdiv qword ptr [edx]
        fstp r
    }
    return r;
}
// fild [i]; fdiv qword [d]   (0x0050e740)
float IntOverDouble(const int32_t* i, const double* d)
{
    float r;
    __asm {
        mov eax, i
        mov edx, d
        fild dword ptr [eax]
        fdiv qword ptr [edx]
        fstp r
    }
    return r;
}
// (float)(unsigned)a / (float)(unsigned)b with each operand fild + (negative: fadd dword [0x005bcb28] = 2^32),
// then one fdivp   (0x0050e3b0)
float UnsignedOverUnsigned(int32_t a, int32_t b)
{
    float r;
    __asm {
        mov ecx, 0x005bcb28
        fild a
        cmp a, 0
        jge a_positive
        fadd dword ptr [ecx]
    a_positive:
        fild b
        cmp b, 0
        jge b_positive
        fadd dword ptr [ecx]
    b_positive:
        fdivp st(1), st
        fstp r
    }
    return r;
}
// fld qword [0x005bcb40] (1.0); fdiv qword [d]; fstp dword   (0x0050de70)
float OneOverDouble(const double* d)
{
    float r;
    __asm {
        mov eax, 0x005bcb40
        mov edx, d
        fld qword ptr [eax]
        fdiv qword ptr [edx]
        fstp r
    }
    return r;
}
}  // namespace

uint32_t Movie_Destruct(uint8_t* m);
uint32_t Movie_Close(uint8_t* m);
uint32_t Movie_StreamAudio(uint8_t* m);
uint32_t Movie_DecodeFrame(uint8_t* m);
uint32_t Movie_UploadFrame(uint8_t* m);

// 0x0050dcf0 (ecx = list head (address of the first-node pointer); stack: memory; ret 4). Movie constructor: list-node
// base (vtable 0x0058a204) linked in at the head ecx (+8 = ecx, +4 = old first node, whose +8 becomes this+4), then
// vtable 0x0057d598, texture descriptor +0x1f0..+0x213 zeroed, path byte +0xec, +0x214..+0x21c, +0x260..+0x26c and
// flags +0xc = 0, +0x220 = 1.0f, +0x224 = 44000. The singleton 0x004fba30's +0x10 counts live movies: the first one
// calls DivxSetNumAudVidBuffers(2, 4, 0x10000). Returns the object. Caller 0x005039f0.
uint32_t Movie_Construct(uint32_t* head, uint8_t* m)
{
    Field<uint32_t*>(m, 8) = head;
    Field<uint32_t>(m, 0) = 0x0058a204;
    Field<uint32_t>(m, 4) = *head;
    *head = u(m);
    const uint32_t next = Field<uint32_t>(m, 4);
    if (next != 0)
        Field<uint32_t>(P(next), 8) = u(m) + 4;
    Field<uint32_t>(m, 0) = 0x0057d598;
    for (uint32_t i = 0; i < 9; ++i)
        Field<uint32_t>(m, 0x1f0 + i * 4) = 0;
    Field<uint8_t>(m, 0xec) = 0;
    Field<uint32_t>(m, 0x214) = 0;
    Field<uint32_t>(m, 0x218) = 0;
    Field<uint32_t>(m, 0x21c) = 0;
    Field<uint32_t>(m, 0x260) = 0;
    Field<uint32_t>(m, 0xc) = 0;
    Field<uint32_t>(m, 0x264) = 0;
    Field<uint32_t>(m, 0x268) = 0;
    Field<uint32_t>(m, 0x26c) = 0;
    Field<uint32_t>(m, 0x220) = 0x3f800000;
    Field<uint32_t>(m, 0x224) = 44000;
    const uint32_t counter = orig::fn_004fba30();
    const int32_t count = ++Field<int32_t>(P(counter), 0x10);
    if (count == 1)
        DivxSetNumAudVidBuffers(2, 4, 0x10000);
    return u(m);
}
static uint32_t Movie_Construct_Abi(uint32_t ecx, uint32_t a0)
{
    return Movie_Construct(reinterpret_cast<uint32_t*>(ecx), static_cast<uint8_t*>(P(a0)));
}
ABI_ENTRY_0050dcf0(Movie_Construct_Entry, Movie_Construct_Abi)
HOOK(0x0050dcf0, "Movie_Construct", Movie_Construct_Entry, nullptr);

// 0x0050ddd0 (ecx = movie; stack: flags; ret 4; vtable 0x0057d598 slot 10 (+0x28)). Scalar deleting destructor:
// Movie_Destruct, then with flags bit 0 frees the object on the game heap (PMS_heapFree 0x004e08d0, heap 0x0063a210).
// Returns the object.
uint32_t Movie_ScalarDeletingDestructor(uint8_t* m, uint32_t flags)
{
    Movie_Destruct(m);
    if ((flags & 1) != 0)
        orig::fn_004e08d0(0x0063a210, u(m));
    return u(m);
}
static uint32_t Movie_ScalarDeletingDestructor_Abi(uint32_t ecx, uint32_t a0)
{
    return Movie_ScalarDeletingDestructor(static_cast<uint8_t*>(P(ecx)), a0);
}
ABI_ENTRY_0050ddd0(Movie_ScalarDeletingDestructor_Entry, Movie_ScalarDeletingDestructor_Abi)
HOOK(0x0050ddd0, "Movie_ScalarDeletingDestructor", Movie_ScalarDeletingDestructor_Entry, nullptr);

// 0x0050de00 (stack: movie; ret 4). Movie destructor: vtable 0x0057d598, Movie_Close, live-movie count (0x004fba30
// singleton +0x10) -= 1, then the list-node base: vtable 0x0058a204, unlink (next's +8 = this +8, *(this +8) = next).
// Returns the next node.
uint32_t Movie_Destruct(uint8_t* m)
{
    Field<uint32_t>(m, 0) = 0x0057d598;
    Movie_Close(m);
    const uint32_t counter = orig::fn_004fba30();
    --Field<int32_t>(P(counter), 0x10);
    const uint32_t next = Field<uint32_t>(m, 4);
    Field<uint32_t>(m, 0) = 0x0058a204;
    if (next != 0)
        Field<uint32_t>(P(next), 8) = Field<uint32_t>(m, 8);
    *Field<uint32_t*>(m, 8) = Field<uint32_t>(m, 4);
    return Field<uint32_t>(m, 4);
}
static uint32_t Movie_Destruct_Abi(uint32_t a0)
{
    return Movie_Destruct(static_cast<uint8_t*>(P(a0)));
}
ABI_ENTRY_0050de00(Movie_Destruct_Entry, Movie_Destruct_Abi)
HOOK(0x0050de00, "Movie_Destruct", Movie_Destruct_Entry, nullptr);

// 0x0050de70 (ecx = movie; stack: file name; ret 4; caller 0x005039f0). Open a movie: Movie_Close, path +0xec =
// _makepath(0, *(0x0063a204) directory, name, 0), DivxSetNumAudVidBuffers(1, 4, 0x10000), OpenDivxFile(path) -> +0x214.
// Failure: DivxGetLastError / DivxFormatErrorMessage into a 200-byte local (never used), flags |= 4, returns al = 0.
// Success: DivxGetContentInfo(h, +0x228), DivxSetOutputVideoFormat(h, 4), DivxModeSet(h, 1), frame buffer
// (PMS_heapAlloc 1, width*height*4, 2, "Movie buffer" 0x0057d604) -> +0x260 and DivxSetVideoBuffer; when +0x258 (has
// audio, guess) the PC sound system (AudioPC_Acquire) -> +0x26c and, if it exists, a memory streamer (0xe4 bytes,
// 0x004e5ae0 with total 0x40000, no buffer, block 0x10000) -> +0x264. Then +0x218 = 0, +0x220 = 1.0, +0x21c = 1/fps,
// +0x224 = channels*rate*bits/8 (bytes per second). Returns al = 1.
uint32_t Movie_OpenDivx(uint8_t* m, uint32_t name)
{
    uint32_t error = 0;
    char message[200] = {};
    Movie_Close(m);
    const uint32_t path = u(m) + 0xec;
    orig::fn_0053859e(path, 0, GameGlobal<uint32_t>(0x0063a204), name, 0);
    DivxSetNumAudVidBuffers(1, 4, 0x10000);
    const uint32_t h = OpenDivxFile(path);
    Field<uint32_t>(m, 0x214) = h;
    if (h == 0)
    {
        DivxGetLastError(h, u(&error));
        DivxFormatErrorMessage(0, error, u(message), 200);
        const uint32_t flags = Field<uint32_t>(m, 0xc) | 4;
        Field<uint32_t>(m, 0xc) = flags;
        return flags & 0xffffff00u;
    }
    DivxGetContentInfo(h, u(m) + 0x228);
    DivxSetOutputVideoFormat(Field<uint32_t>(m, 0x214), 4);
    DivxModeSet(Field<uint32_t>(m, 0x214), 1);
    const uint32_t size = static_cast<uint32_t>(Field<int32_t>(m, 0x22c) * Field<int32_t>(m, 0x228)) << 2;
    const uint32_t buffer = orig::fn_004e07f0(0x0063a210, 1, size, 2, 0x0057d604);
    Field<uint32_t>(m, 0x260) = buffer;
    DivxSetVideoBuffer(Field<uint32_t>(m, 0x214), buffer);
    if (Field<uint32_t>(m, 0x258) != 0)
    {
        const uint32_t audio = orig::fn_004f9c10(0);  // ecx "input" is a `push ecx` local reservation
        Field<uint32_t>(m, 0x26c) = audio;
        if (audio != 0)
        {
            const uint32_t memory = orig::fn_004e07f0(0x0063a210, 0x10, 0xe4, 2, 0x0058b5e8);
            const uint32_t streamer = memory != 0 ? orig::fn_004e5ae0(memory, 0x40000, 0, 0x10000) : 0;
            Field<uint32_t>(m, 0x264) = streamer;
        }
    }
    Field<uint32_t>(m, 0x218) = 0;
    Field<uint32_t>(m, 0x220) = 0x3f800000;
    Field<float>(m, 0x21c) = OneOverDouble(&Field<double>(m, 0x230));
    const int32_t bytes = Field<int32_t>(m, 0x24c) * Field<int32_t>(m, 0x248) * Field<int32_t>(m, 0x244);
    const int32_t perSecond = bytes / 8;
    Field<int32_t>(m, 0x224) = perSecond;
    return (static_cast<uint32_t>(perSecond) & 0xffffff00u) | 1;
}
static uint32_t Movie_OpenDivx_Abi(uint32_t ecx, uint32_t a0)
{
    return Movie_OpenDivx(static_cast<uint8_t*>(P(ecx)), a0);
}
ABI_ENTRY_0050de70(Movie_OpenDivx_Entry, Movie_OpenDivx_Abi)
HOOK(0x0050de70, "Movie_OpenDivx", Movie_OpenDivx_Entry, nullptr);

// 0x0050e060 (esi = movie; callers 0x0050de00, 0x0050de70). Close: flags +0xc = 0, path byte +0xec = 0, renderer
// virtual +0x128 (release texture, guess) with (+0x1f0 descriptor, 0); CloseDivxFile(+0x214); frees the frame
// buffer +0x260 (PMS_heapFree); audio stream +0x268 released through sound-system virtual +0xd8 (&+0x268, 0) (the sound
// system +0x26c is not null-checked there); sound system virtual +0x70 (AudioPC_Release); streamer +0x264 slot 0 (0)
// (0x004e5b10: deleting release). Each member is zeroed after use. Returns al = 1.
uint32_t Movie_Close(uint8_t* m)
{
    Field<uint32_t>(m, 0xc) = 0;
    Field<uint8_t>(m, 0xec) = 0;
    VCall<uint32_t>(MovieRenderer(), 0x128 / 4, u(m) + 0x1f0, 0u);
    if (Field<uint32_t>(m, 0x214) != 0)
    {
        CloseDivxFile(Field<uint32_t>(m, 0x214));
        Field<uint32_t>(m, 0x214) = 0;
    }
    if (Field<uint32_t>(m, 0x260) != 0)
    {
        orig::fn_004e08d0(0x0063a210, Field<uint32_t>(m, 0x260));
        Field<uint32_t>(m, 0x260) = 0;
    }
    if (Field<uint32_t>(m, 0x268) != 0)
    {
        VCall<uint32_t>(Field<void*>(m, 0x26c), 0xd8 / 4, u(m) + 0x268, 0u);
        Field<uint32_t>(m, 0x268) = 0;
    }
    if (Field<void*>(m, 0x26c) != nullptr)
    {
        VCall<uint32_t>(Field<void*>(m, 0x26c), 0x70 / 4);
        Field<uint32_t>(m, 0x26c) = 0;
    }
    if (Field<void*>(m, 0x264) != nullptr)
    {
        VCall<uint32_t>(Field<void*>(m, 0x264), 0, 0u);
        Field<uint32_t>(m, 0x264) = 0;
    }
    return 1;
}
static uint32_t Movie_Close_Abi(uint32_t esi)
{
    return Movie_Close(static_cast<uint8_t*>(P(esi)));
}
ABI_ENTRY_0050e060(Movie_Close_Entry, Movie_Close_Abi)
HOOK(0x0050e060, "Movie_Close", Movie_Close_Entry, nullptr);

// 0x0050e110 (ecx = movie; stack: float seconds; ret 4; vtable 0x0057d598 slot 2 (+8)). Advance the movie: unless
// flags & 7 (ended, paused, error), adds the time to +0x218; while it is >= 0.9 frame (+0x21c * 0.9f [0x005bcc38]),
// subtracts one frame and decodes one (Movie_DecodeFrame); if any frame decoded, Movie_UploadFrame. Then, unless
// paused, Movie_StreamAudio. The original's EAX on return is a leftover (fnstsw / callee result): returns 0.
uint32_t Movie_Update(uint8_t* m, float seconds)
{
    bool decoded = false;
    if ((Field<uint8_t>(m, 0xc) & 7) == 0)
    {
        const float sum = seconds + Field<float>(m, 0x218);
        Field<float>(m, 0x218) = sum;
        if (sum >= Field<float>(m, 0x21c) * K(0x005bcc38))
        {
            do
            {
                Field<float>(m, 0x218) = Field<float>(m, 0x218) - Field<float>(m, 0x21c);
                if (static_cast<uint8_t>(Movie_DecodeFrame(m)) != 0)
                    decoded = true;
            } while (Field<float>(m, 0x21c) * K(0x005bcc38) <= Field<float>(m, 0x218));
            if (decoded)
                Movie_UploadFrame(m);
        }
    }
    if ((Field<uint8_t>(m, 0xc) & 2) == 0)
        Movie_StreamAudio(m);
    return 0;
}
static uint32_t Movie_Update_Abi(uint32_t ecx, uint32_t a0)
{
    return Movie_Update(static_cast<uint8_t*>(P(ecx)), bf(a0));
}
ABI_ENTRY_0050e110(Movie_Update_Entry, Movie_Update_Abi)
HOOK(0x0050e110, "Movie_Update", Movie_Update_Entry, nullptr);

// 0x0050e1a0 (esi = movie; caller 0x0050e110). Feed DivX audio to the sound system and keep it in sync: when the
// streamer +0x264 exists and the movie is not paused, streamer virtual +0x28 (&buffer, 0) returns the free byte count;
// if non-zero, DivxSetAudioBuffer(h, buffer) and DivxNextAudioChunk(h, &bytes): success -> streamer +0x2c (bytes) and,
// the first time, the sound-system stream (virtual +0xc0 with (streamer, (u16)channels, rate, (u16)bits)) -> +0x268,
// returns al = 1; failure -> if a stream exists, the buffer is zero-filled and committed (+0x2c (count)), else return.
// Then with a stream: d = this->vt+0x24 (audio time) - this->vt+0x10 (video time); the target rate factor is
// 1 / (1 + 0.05 * min(((d - 0.05) * 2.2222)^2, 1)) for d > 0.05, 1 + 0.05 * min(((d + 0.05) * -2.2222)^2, 1) for
// d < -0.05, else 1; when it differs from +0x220, +0x220 moves 2.5% of the way and the stream frequency becomes
// (int)(rate * +0x220) (sound-system virtual +0xf0). Returns al = 1 when a chunk was streamed (upper bytes leftover;
// 0 here).
uint32_t Movie_StreamAudio(uint8_t* m)
{
    uint32_t frame[3] = {};  // original 0xc-byte frame: [0] buffer, [1] chunk bytes, [2] float temporary
    uint8_t streamed = 0;
    void* streamer = Field<void*>(m, 0x264);
    if (streamer == nullptr || (Field<uint8_t>(m, 0xc) & 2) != 0)
        return streamed;
    const uint32_t count = VCall<uint32_t>(streamer, 0x28 / 4, u(&frame[0]), 0u);
    if (count != 0)
    {
        DivxSetAudioBuffer(Field<uint32_t>(m, 0x214), frame[0]);
        if (DivxNextAudioChunk(Field<uint32_t>(m, 0x214), u(&frame[1])) == 0)
        {
            VCall<uint32_t>(Field<void*>(m, 0x264), 0x2c / 4, frame[1]);
            if (Field<uint32_t>(m, 0x268) == 0)
            {
                Field<uint32_t>(m, 0x268) = VCall<uint32_t>(Field<void*>(m, 0x26c), 0xc0 / 4, Field<uint32_t>(m, 0x264),
                    static_cast<uint32_t>(Field<uint16_t>(m, 0x244)), Field<uint32_t>(m, 0x248),
                    static_cast<uint32_t>(Field<uint16_t>(m, 0x24c)));
            }
            streamed = 1;
        }
        else
        {
            if (Field<uint32_t>(m, 0x268) == 0)
                return streamed;
            std::memset(P(frame[0]), 0, count);
            VCall<uint32_t>(Field<void*>(m, 0x264), 0x2c / 4, count);
        }
    }
    if (Field<uint32_t>(m, 0x268) == 0)
        return streamed;
    const float audioTime = VCall<float>(m, 0x24 / 4);
    const float videoTime = VCall<float>(m, 0x10 / 4);
    float d = audioTime - videoTime;
    float target;
    if (d > K(0x005bcc34))
    {
        d = (d - K(0x005bcc34)) * K(0x005bcc30);
        d = d * d;
        if (d > K(0x005bcb2c))
            d = K(0x005bcb2c);
        target = K(0x005bcb2c) / (d * K(0x005bcc34) + K(0x005bcb2c));
    }
    else if (!(d < K(0x005bcc2c)))
    {
        target = K(0x005bcb2c);
    }
    else
    {
        d = (d + K(0x005bcc34)) * K(0x005bcc28);
        d = d * d;
        if (d > K(0x005bcb2c))
            d = K(0x005bcb2c);
        target = d * K(0x005bcc34) + K(0x005bcb2c);
    }
    const float current = Field<float>(m, 0x220);
    if (target != current)
    {
        const float rate = (target - current) * K(0x005bcc24) + current;
        Field<float>(m, 0x220) = rate;
        void* audio = Field<void*>(m, 0x26c);
        const int32_t frequency = static_cast<int32_t>(static_cast<float>(Field<int32_t>(m, 0x248)) * rate);
        VCall<uint32_t>(audio, 0xf0 / 4, Field<uint32_t>(m, 0x268), static_cast<uint32_t>(frequency));
    }
    return streamed;
}
static uint32_t Movie_StreamAudio_Abi(uint32_t esi)
{
    return Movie_StreamAudio(static_cast<uint8_t*>(P(esi)));
}
ABI_ENTRY_0050e1a0(Movie_StreamAudio_Entry, Movie_StreamAudio_Abi)
HOOK(0x0050e1a0, "Movie_StreamAudio", Movie_StreamAudio_Entry, nullptr);

// 0x0050e3a0 (ecx = movie; returns st0; vtable 0x0057d598 slot 8 (+0x20)). Returns the audio playback-rate factor
// +0x220.
float fn_0050e3a0(uint8_t* m)
{
    return Field<float>(m, 0x220);
}
static float fn_0050e3a0_Abi(uint32_t ecx)
{
    return fn_0050e3a0(static_cast<uint8_t*>(P(ecx)));
}
ABI_ENTRY_0050e3a0(fn_0050e3a0_Entry, fn_0050e3a0_Abi)
HOOK(0x0050e3a0, "fn_0050e3a0", fn_0050e3a0_Entry, nullptr);

// 0x0050e3b0 (ecx = movie; returns st0 - symbols/abi.csv says eax; vtable 0x0057d598 slot 9 (+0x24)). Audio time:
// with a stream +0x268 and the sound system +0x26c, (unsigned) sound-system virtual +0xec (stream) (bytes played,
// guess) / (unsigned) +0x224 bytes per second; otherwise tail-jumps to this->vt+0x10 (video time, 0x0050e740),
// reproduced as call-then-return (no stack arguments, so the frame is intact). Hand-written entry stub: st0 result.
float Movie_GetAudioTime(uint8_t* m)
{
    if (Field<uint32_t>(m, 0x268) != 0 && Field<void*>(m, 0x26c) != nullptr)
    {
        const int32_t played = static_cast<int32_t>(
            VCall<uint32_t>(Field<void*>(m, 0x26c), 0xec / 4, Field<uint32_t>(m, 0x268)));
        return UnsignedOverUnsigned(played, Field<int32_t>(m, 0x224));
    }
    return VCall<float>(m, 0x10 / 4);
}
static float Movie_GetAudioTime_Abi(uint32_t ecx)
{
    return Movie_GetAudioTime(static_cast<uint8_t*>(P(ecx)));
}
static __declspec(naked) void Movie_GetAudioTime_Entry()
{
    __asm {
        push eax
        push ecx
        push edx
        push ebx
        push esi
        push edi
        push ebp
        push ecx
        call Movie_GetAudioTime_Abi
        add esp, 4
        pop ebp
        pop edi
        pop esi
        pop ebx
        pop edx
        pop ecx
        pop eax
        ret
    }
}
HOOK(0x0050e3b0, "Movie_GetAudioTime", Movie_GetAudioTime_Entry, nullptr);

// 0x0050e410 (esi = movie; caller 0x0050e110). Decode one frame: DivxDoFrame(h) == 0 -> DivxNextFrame(h),
// DivxGetContentInfo(h, +0x228), returns al = 1. 0x65 (end of file, guess): with the loop flag 0x10,
// DivxGotoFrame(h, 0) and flags |= 8, else flags |= 1 (ended). Any other result: flags |= 4 (error). Those return
// al = 0 (upper bytes as the original's EAX).
uint32_t Movie_DecodeFrame(uint8_t* m)
{
    const uint32_t result = DivxDoFrame(Field<uint32_t>(m, 0x214));
    if (result == 0)
    {
        DivxNextFrame(Field<uint32_t>(m, 0x214));
        const uint32_t info = DivxGetContentInfo(Field<uint32_t>(m, 0x214), u(m) + 0x228);
        return (info & 0xffffff00u) | 1;
    }
    if (result != 0x65)
    {
        Field<uint32_t>(m, 0xc) |= 4;
        return result & 0xffffff00u;
    }
    if ((Field<uint8_t>(m, 0xc) & 0x10) != 0)
    {
        DivxGotoFrame(Field<uint32_t>(m, 0x214), 0);
        const uint32_t flags = Field<uint32_t>(m, 0xc) | 8;
        Field<uint32_t>(m, 0xc) = flags;
        return flags & 0xffffff00u;
    }
    Field<uint32_t>(m, 0xc) |= 1;
    return 0;
}
static uint32_t Movie_DecodeFrame_Abi(uint32_t esi)
{
    return Movie_DecodeFrame(static_cast<uint8_t*>(P(esi)));
}
ABI_ENTRY_0050e410(Movie_DecodeFrame_Entry, Movie_DecodeFrame_Abi)
HOOK(0x0050e410, "Movie_DecodeFrame", Movie_DecodeFrame_Entry, nullptr);

// 0x0050e490 (ecx = movie; stack: byte paused; ret 4; vtable 0x0057d598 slot 3 (+0xc)). Set or clear flag 2
// (paused, guess: it stops Movie_Update and Movie_StreamAudio); when it changed and a stream +0x268 exists, the sound
// system's virtual +0xe0 (pause) or +0xe4 (resume) with (stream, 0, 0) (both pop three arguments). Returns the
// original's EAX (callee result, old flags, or 0).
uint32_t Movie_SetPaused(uint8_t* m, uint32_t paused)
{
    const uint8_t on = static_cast<uint8_t>(paused);
    const uint32_t old = Field<uint32_t>(m, 0xc);
    const uint32_t flags = on != 0 ? (old | 2) : (old & 0xfffffffdu);
    Field<uint32_t>(m, 0xc) = flags;
    if (((flags ^ old) & 2) == 0)
        return old;
    const uint32_t stream = Field<uint32_t>(m, 0x268);
    if (stream == 0)
        return 0;
    void* audio = Field<void*>(m, 0x26c);
    if (on != 0)
        return VCall<uint32_t>(audio, 0xe0 / 4, stream, 0u, 0u);
    return VCall<uint32_t>(audio, 0xe4 / 4, stream, 0u, 0u);
}
static uint32_t Movie_SetPaused_Abi(uint32_t ecx, uint32_t a0)
{
    return Movie_SetPaused(static_cast<uint8_t*>(P(ecx)), a0);
}
ABI_ENTRY_0050e490(Movie_SetPaused_Entry, Movie_SetPaused_Abi)
HOOK(0x0050e490, "Movie_SetPaused", Movie_SetPaused_Entry, nullptr);

// 0x0050e4e0 (ecx = movie; stack: int* width, int* height; ret 8; vtable 0x0057d598 slot 1 (+4)). Without a frame
// buffer returns al = 0; otherwise stores the content width +0x228 / height +0x22c through the non-null pointers and
// returns al = 1 (upper bytes: the height pointer, or 0).
uint32_t Movie_GetFrameSize(uint8_t* m, uint32_t* width, uint32_t* height)
{
    if (Field<uint32_t>(m, 0x260) == 0)
        return 0;
    if (width != nullptr)
        *width = Field<uint32_t>(m, 0x228);
    if (height != nullptr)
        *height = Field<uint32_t>(m, 0x22c);
    return (u(height) & 0xffffff00u) | 1;
}
static uint32_t Movie_GetFrameSize_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return Movie_GetFrameSize(static_cast<uint8_t*>(P(ecx)), static_cast<uint32_t*>(P(a0)),
        static_cast<uint32_t*>(P(a1)));
}
ABI_ENTRY_0050e4e0(Movie_GetFrameSize_Entry, Movie_GetFrameSize_Abi)
HOOK(0x0050e4e0, "Movie_GetFrameSize", Movie_GetFrameSize_Entry, nullptr);

// 0x0050e520 (ecx = movie; stack: state object, second object, vertex source, a3, float corners[4][2]; ret 0x14;
// vtable 0x0057d598 slot 0). Draw the movie quad when its texture exists (byte +0x213 bit 0x80) and the state object
// is non-null: a 6-dword record {0x130e4, 0xffffffff, &texture descriptor +0x1f0, 0, 0, 0} and 0x004fb860 (eax =
// movie (state block), esi = state object, edi = second object; a3, &record) set the render state; the vertex source's
// virtual +0x14 (0, 2, 1, 0) returns a buffer whose virtual +4 (4) returns 4 vertices of 6 dwords, filled with
// {corner x, corner y, 0, record[1] (colour), u, v} with u, v from the table 0x0057d5c4 ((0,0) (1,0) (0,1) (1,1));
// then vertex source virtual +0x1c (submit, guess) and 0x004fb920 (eax = movie, esi = state object; a3) restores the
// state. Argument mapping checked from the stack offsets (Ghidra's draft mixes them up).
uint32_t Movie_Draw(uint8_t* m, uint32_t state, uint32_t second, void* source, uint32_t a3, const uint32_t* corners)
{
    if ((Field<uint8_t>(m, 0x213) & 0x80) == 0 || state == 0)
        return 0;  // the original leaves the caller's EAX here (void method)
    uint32_t record[6] = {};
    record[2] = u(m) + 0x1f0;
    record[1] = 0xffffffffu;
    record[0] = 0x130e4;
    orig::fn_004fb860(u(m), state, second, a3, u(record));
    void* buffer = VCall<void*>(source, 0x14 / 4, 0u, 2u, 1u, 0u);
    if (buffer != nullptr)
    {
        uint32_t* v = VCall<uint32_t*>(buffer, 1, 4u);
        const uint8_t* uv = reinterpret_cast<const uint8_t*>(0x0057d5c4);
        const uint32_t* c = corners;
        for (int i = 0; i < 4; ++i)
        {
            v[0] = c[0];
            v[1] = c[1];
            v[2] = 0;
            v[3] = record[1];
            v[4] = Field<uint32_t>(uv, i * 8);
            v[5] = Field<uint32_t>(uv, i * 8 + 4);
            v += 6;
            c += 2;
        }
    }
    VCall<uint32_t>(source, 0x1c / 4);
    return orig::fn_004fb920(u(m), state, a3);
}
static uint32_t Movie_Draw_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    return Movie_Draw(static_cast<uint8_t*>(P(ecx)), a0, a1, P(a2), a3, static_cast<const uint32_t*>(P(a4)));
}
ABI_ENTRY_0050e520(Movie_Draw_Entry, Movie_Draw_Abi)
HOOK(0x0050e520, "Movie_Draw", Movie_Draw_Entry, nullptr);

// 0x0050e620 (ebx = movie; caller 0x0050e110). Copy the decoded frame into the movie texture: with a frame buffer,
// creates the texture on first use (byte +0x213 bit 0x80 clear: descriptor +0x1f0 = {name +0xec, width +0x228,
// height +0x22c, 0, 0, byte 0x7f, word 0x41}, renderer virtual +0x124), locks it (renderer virtual +0x13c (desc,
// &bits, &pitch)), copies height rows of width*4 bytes bottom-up (first source row to the last texture row), unlocks
// (renderer virtual +0x144 (desc)). Returns the unlock result, the lock result (al = 0) or 0 without a buffer.
uint32_t Movie_UploadFrame(uint8_t* m)
{
    uint32_t frame[4] = {};  // original 0x10-byte frame: [0] bits / row pointer, [1] pitch, [2] renderer, [3] desc
    void* renderer = MovieRenderer();
    if (Field<uint32_t>(m, 0x260) == 0)
        return 0;
    frame[2] = u(renderer);
    if ((Field<uint8_t>(m, 0x213) & 0x80) == 0)
    {
        const uint32_t desc = u(m) + 0x1f0;
        Field<uint32_t>(P(desc), 0) = u(m) + 0xec;
        Field<uint32_t>(m, 0x200) = Field<uint32_t>(m, 0x228);
        Field<uint32_t>(m, 0x20c) = 0;
        Field<uint32_t>(m, 0x208) = 0;
        Field<uint32_t>(m, 0x204) = Field<uint32_t>(m, 0x22c);
        Field<uint16_t>(m, 0x212) = 0x41;
        Field<uint8_t>(m, 0x210) = 0x7f;
        VCall<uint32_t>(renderer, 0x124 / 4, desc);
    }
    const uint32_t desc = u(m) + 0x1f0;
    frame[3] = desc;
    const uint32_t locked = VCall<uint32_t>(renderer, 0x13c / 4, desc, u(&frame[0]), u(&frame[1]));
    if (static_cast<uint8_t>(locked) == 0)
        return locked;
    const int32_t height = Field<int32_t>(m, 0x22c);
    const uint8_t* source = static_cast<const uint8_t*>(P(Field<uint32_t>(m, 0x260)));
    frame[0] = frame[0] + static_cast<uint32_t>((height - 1) * static_cast<int32_t>(frame[1]));
    if (height > 0)
    {
        int32_t row = 0;
        do
        {
            const uint32_t bytes = Field<uint32_t>(m, 0x228) << 2;
            std::memcpy(P(frame[0]), source, bytes);
            frame[0] = frame[0] - frame[1];
            source += Field<uint32_t>(m, 0x228) << 2;
            ++row;
        } while (row < Field<int32_t>(m, 0x22c));
    }
    return VCall<uint32_t>(P(frame[2]), 0x144 / 4, frame[3]);
}
static uint32_t Movie_UploadFrame_Abi(uint32_t ebx)
{
    return Movie_UploadFrame(static_cast<uint8_t*>(P(ebx)));
}
ABI_ENTRY_0050e620(Movie_UploadFrame_Entry, Movie_UploadFrame_Abi)
HOOK(0x0050e620, "Movie_UploadFrame", Movie_UploadFrame_Entry, nullptr);

// 0x0050e740 (ecx = movie; returns st0; vtable 0x0057d598 slot 4 (+0x10)). Video time (guess): (int) +0x23c (current
// frame, guess) / (double) fps +0x230.
float fn_0050e740(uint8_t* m)
{
    return IntOverDouble(&Field<int32_t>(m, 0x23c), &Field<double>(m, 0x230));
}
static float fn_0050e740_Abi(uint32_t ecx)
{
    return fn_0050e740(static_cast<uint8_t*>(P(ecx)));
}
ABI_ENTRY_0050e740(fn_0050e740_Entry, fn_0050e740_Abi)
HOOK(0x0050e740, "fn_0050e740", fn_0050e740_Entry, nullptr);

// 0x0050e750 (ecx = movie; returns st0; vtable 0x0057d598 slot 5 (+0x14)). Duration (guess): (unsigned) +0x238
// (frame count, guess) / (double) fps +0x230.
float fn_0050e750(uint8_t* m)
{
    return UnsignedOverDouble(&Field<int32_t>(m, 0x238), &Field<double>(m, 0x230));
}
static float fn_0050e750_Abi(uint32_t ecx)
{
    return fn_0050e750(static_cast<uint8_t*>(P(ecx)));
}
ABI_ENTRY_0050e750(fn_0050e750_Entry, fn_0050e750_Abi)
HOOK(0x0050e750, "fn_0050e750", fn_0050e750_Entry, nullptr);

// 0x0050e770 (ecx = movie; vtable 0x0057d598 slot 6 (+0x18)). Returns al = 1 unless flags bit 2 (error) is set;
// upper bytes are flags >> 2 as in the original.
uint32_t fn_0050e770(uint8_t* m)
{
    const uint32_t shifted = Field<uint32_t>(m, 0xc) >> 2;
    return (shifted & 0xffffff00u) | (~shifted & 1u);
}
static uint32_t fn_0050e770_Abi(uint32_t ecx)
{
    return fn_0050e770(static_cast<uint8_t*>(P(ecx)));
}
ABI_ENTRY_0050e770(fn_0050e770_Entry, fn_0050e770_Abi)
HOOK(0x0050e770, "fn_0050e770", fn_0050e770_Entry, nullptr);
