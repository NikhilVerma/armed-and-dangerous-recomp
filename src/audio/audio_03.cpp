// Recovered: audio-classified game functions, batch audio_03 (0x004fa8f0-0x004fafc0): virtual overrides of the PC
// (DirectSound) sound system. Static recovery 2026-10-05, compile-checked only (the game was not run); the oracle
// (tools/compare.py) is still to do.
//
// Game+0x2c is the audio object. The DirectSound implementation has vtable 0x0058a230 (0x128 bytes; see
// src/audio/audio_02.cpp): +0xc initialised byte, +0x78 definition count, +0x7c byte "definitions loaded as a bank",
// +0x80 definition table, +0x8c (dword read from the bank), +0xa0 name hash table (0x20 bytes), +0xa4 its entries,
// +0x108 IDirectSoundBuffer table (one per definition), +0x10c IDirectSound8, +0x110 primary buffer, +0x11c byte
// "shutting down", +0x120. Its base class has vtable 0x0058aa40. The four functions here are slots +0x74, +0x7c,
// +0x80 and +0x84 of 0x0058a230 (found by scanning .rdata for their addresses); the base's +0x74, +0x7c and +0x84 are
// 0x004f23a0, 0x004f4c50 and 0x004f2590, its +0x80 is _purecall.
// COM calls go through the interfaces' own vtables (stdcall); method names follow the DirectSound 8 vtable layout.
// Debug_Printf 0x00407ff0 is a bare `ret` (compiled-out printf); it is still called as the original does.
// Stack cookies (__security_check_cookie) are not reproduced.
#include <windows.h>

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
template <class R, class... A>
R Com(uint32_t object, int slot, A... args)
{
    void** vt = *reinterpret_cast<void***>(object);
    return reinterpret_cast<R(__stdcall*)(uint32_t, A...)>(vt[slot])(object, args...);
}
// PMS_heapAlloc 0x004e07f0 on the game heap 0x0063a210: (alignment, size, 2, tag).
inline uint32_t GameHeapAlloc(uint32_t align, uint32_t size, uint32_t tag)
{
    return orig::fn_004e07f0(0x0063a210, align, size, 2, tag);
}
inline void GameHeapFree(uint32_t p)
{
    orig::fn_004e08d0(0x0063a210, p);
}
inline void DebugPrintf2(uint32_t a, uint32_t b)
{
    GameFunction<void(__cdecl*)(uint32_t, uint32_t)>(0x00407ff0)(a, b);
}
inline void DebugPrintf3(uint32_t a, uint32_t b, uint32_t c)
{
    GameFunction<void(__cdecl*)(uint32_t, uint32_t, uint32_t)>(0x00407ff0)(a, b, c);
}
// QueryPerformanceCounter through the IAT slot 0x0056b08c (the original loads it once into ebp).
using QpcFn = BOOL(WINAPI*)(LARGE_INTEGER*);
// Seconds since the timer start: (counter - *(int64*)0x00604ad8) / *(int64*)0x00604ac8 as fild/fild/fdivp, stored as
// float (the x87 runs at single precision).
float ElapsedSeconds(int64_t counter)
{
    const int64_t delta = counter - GameGlobal<int64_t>(0x00604ad8);
    return static_cast<float>(static_cast<double>(delta) / static_cast<double>(GameGlobal<int64_t>(0x00604ac8)));
}

// The level stream's inlined read (pattern of 0x004f59a0, inlined in 0x004fab80): with a file streamer +0x28 that has
// not failed (+0x10 bit 6), r = streamer virtual +4 (size, buffer); if it failed during the read and the stream has an
// error callback (+0, cdecl), callback(-1). Returns r, or 0 without a usable streamer.
uint32_t StreamRead(uint8_t* stream, uint32_t size, uint32_t buffer)
{
    void* fs = Field<void*>(stream, 0x28);
    if (fs == nullptr || ((Field<uint32_t>(fs, 0x10) >> 6) & 1) != 0)
        return 0;
    const uint32_t r = VCall<uint32_t>(fs, 1, size, buffer);
    if (((Field<uint32_t>(Field<void*>(stream, 0x28), 0x10) >> 6) & 1) != 0)
    {
        const uint32_t callback = Field<uint32_t>(stream, 0);
        if (callback != 0)
            reinterpret_cast<void(__cdecl*)(int32_t)>(callback)(-1);
    }
    return r;
}
}  // namespace

// 0x004fa8f0 (ecx = sound system; stack: 11 arguments; callee pops 0x2c; vtable 0x0058a230 +0x74, overriding
// 0x004f23a0). Initialise DirectSound once: with byte +0xc set returns al = 1. Without IDirectSound8 (+0x10c):
// DirectSoundCreate8(0, &+0x10c, 0) (import thunk 0x005665e0); SetCooperativeLevel(a10 (window), 3); primary buffer
// CreateSoundBuffer({0x24, flags 0x91}, &+0x110, 0); SetFormat(PCM, 2 channels, 22050 Hz, 88200 bytes/s, align 4,
// 16 bits); a failed SetFormat is logged by HRESULT ("DS SetFormat : ..." strings 0x0058a4a4..0x0058a560, Debug_Printf
//, first argument this+8). Then 0x004f9e90 (esi = this; a1) sets up the listener; on failure the
// IDirectSound8 is released, +0x10c = 0 and al = 0 is returned. Failed DirectSoundCreate8 / CreateSoundBuffer also
// return al = 0 (upper bytes from the HRESULT). With a device: when a0 != 0, "audio buffer list" (a0 * 4 bytes,
// zeroed) -> +0x108; then returns the base 0x004f23a0 (ecx = this; all 11 arguments).
uint32_t Audio_InitDirectSound(uint8_t* self, const uint32_t* args)
{
    if (Field<uint8_t>(self, 0xc) != 0)
        return 1;
    uint32_t* device = &Field<uint32_t>(self, 0x10c);
    if (*device == 0)
    {
        using CreateFn = HRESULT(__stdcall*)(uint32_t, uint32_t*, uint32_t);
        HRESULT hr = GameFunction<CreateFn>(0x005665e0)(0, device, 0);
        if (hr < 0)
            return static_cast<uint32_t>(hr) & 0xffffff00u;
        Com<HRESULT>(*device, 0x18 / 4, args[10], 3u);  // SetCooperativeLevel(hwnd, DSSCL_EXCLUSIVE)
        uint32_t frame[0x3c / 4] = {};  // original 0x3c-byte frame: WAVEFORMATEX at +0xc, DSBUFFERDESC at +0x20
        uint8_t* f = reinterpret_cast<uint8_t*>(frame);
        uint32_t* desc = reinterpret_cast<uint32_t*>(f + 0x20);
        for (int i = 0; i < 9; ++i)
            desc[i] = 0;
        desc[0] = 0x24;  // dwSize
        desc[1] = 0x91;  // DSBCAPS_PRIMARYBUFFER | DSBCAPS_CTRLVOLUME | DSBCAPS_CTRL3D
        hr = Com<HRESULT>(*device, 0xc / 4, u(desc), u(&Field<uint32_t>(self, 0x110)), 0u);  // CreateSoundBuffer
        if (hr < 0)
            return static_cast<uint32_t>(hr) & 0xffffff00u;
        const uint32_t primary = Field<uint32_t>(self, 0x110);
        Field<uint16_t>(f, 0xc) = 1;         // wFormatTag = WAVE_FORMAT_PCM
        Field<uint32_t>(f, 0x10) = 0x5622;   // nSamplesPerSec = 22050
        Field<uint16_t>(f, 0x1a) = 0x10;     // wBitsPerSample
        Field<uint16_t>(f, 0xe) = 2;         // nChannels
        Field<uint16_t>(f, 0x1c) = 0;        // cbSize
        Field<uint16_t>(f, 0x18) = 4;        // nBlockAlign
        Field<uint32_t>(f, 0x14) = 0x15888;  // nAvgBytesPerSec
        const uint32_t result = static_cast<uint32_t>(Com<HRESULT>(primary, 0x38 / 4, u(f + 0xc)));  // SetFormat
        if (result != 0)
        {
            const uint32_t tag = Field<uint32_t>(self, 8);
            if (result == 0x88780064u)
                DebugPrintf2(tag, 0x0058a560);  // "DS SetFormat : Bad Format"
            else if (result == 0x88780032u)
                DebugPrintf2(tag, 0x0058a544);  // "DS SetFormat : Invalid Call"
            else if (result == 0x80070057u)
                DebugPrintf2(tag, 0x0058a524);  // "DS SetFormat : Invalid Param"
            else if (result == 0x8007000eu)
                DebugPrintf2(tag, 0x0058a504);  // "DS SetFormat : Out of Memory"
            else if (result == 0x88780046u)
                DebugPrintf2(tag, 0x0058a4dc);  // "DS SetFormat : Priority Level Needed"
            else if (result == 0x80004001u)
                DebugPrintf2(tag, 0x0058a4c0);  // "DS SetFormat : Unsupported"
            else
                DebugPrintf3(tag, 0x0058a4a4, result);  // "DS SetFormat : Unknown (%d)"
        }
        if (static_cast<uint8_t>(orig::fn_004f9e90(u(self), args[1])) == 0)
        {
            const uint32_t released = Com<uint32_t>(*device, 2);  // Release
            *device = 0;
            return released & 0xffffff00u;
        }
    }
    const uint32_t count = args[0];
    if (count != 0)
    {
        const uint32_t list = GameHeapAlloc(2, count * 4, 0x0058a490);  // "audio buffer list"
        Field<uint32_t>(self, 0x108) = list;
        std::memset(P(list), 0, count * 4);
    }
    return orig::fn_004f23a0(u(self), args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8],
        args[9], args[10]);
}
static uint32_t Audio_InitDirectSound_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4,
    uint32_t a5, uint32_t a6, uint32_t a7, uint32_t a8, uint32_t a9, uint32_t a10)
{
    const uint32_t args[11] = {a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10};
    return Audio_InitDirectSound(static_cast<uint8_t*>(P(ecx)), args);
}
ABI_ENTRY_004fa8f0(Audio_InitDirectSound_Entry, Audio_InitDirectSound_Abi)
HOOK(0x004fa8f0, "Audio_InitDirectSound", Audio_InitDirectSound_Entry, nullptr);

// 0x004fab40 (ecx = sound system; stack: index; callee pops 4; vtable 0x0058a230 +0x7c, overriding 0x004f4c50;
// executed in recorded runs). Unload definition `index`: with a buffer table +0x108 and a buffer at the index, its
// IDirectSoundBuffer Release (slot 2) and the entry cleared; then the base 0x004f4c50 (ecx = this; index), whose result
// is returned.
uint32_t AudioPC_UnloadDefinition(uint8_t* self, uint32_t index)
{
    const uint32_t table = Field<uint32_t>(self, 0x108);
    if (table != 0)
    {
        const uint32_t buffer = reinterpret_cast<uint32_t*>(table)[index];
        if (buffer != 0)
        {
            Com<uint32_t>(buffer, 2);  // IDirectSoundBuffer::Release
            Field<uint32_t*>(self, 0x108)[index] = 0;
        }
    }
    return orig::fn_004f4c50(u(self), index);
}
static uint32_t AudioPC_UnloadDefinition_Abi(uint32_t ecx, uint32_t a0)
{
    return AudioPC_UnloadDefinition(static_cast<uint8_t*>(P(ecx)), a0);
}
ABI_ENTRY_004fab40(AudioPC_UnloadDefinition_Entry, AudioPC_UnloadDefinition_Abi)
HOOK(0x004fab40, "AudioPC_UnloadDefinition", AudioPC_UnloadDefinition_Entry, nullptr);

// 0x004fab80 (ecx = sound system; stack: level stream, size, unused; callee pops 0xc; vtable 0x0058a230 +0x80 (base:
// _purecall); not executed in recorded runs). Load the sound definitions from the level stream. Not initialised (byte
// +0xc clear): the block is skipped by reading `size` bytes into a "tmp buffer" (0x004f59a0 (eax = stream; size, buffer,
// -1)) that is freed again. Otherwise:
//   hash entry size (4 bytes); hash table (0x20 bytes, align 0x10, tag 0x0058b5e8: +0..+8 = 0, +0x10 = 0x004f4cc0
//   callback, +0x14 = this, +0x1c = 0) -> +0xa0; "audio hashtable entries" -> +0xa4 read (0x004f59a0, ..., 0x1e) and
//   fixed up by 0x004f1220 (eax = entries, ecx = table); definition count +0x78 and +0x8c (4 bytes each);
//   "audio definition list" (+0x78 * 4, zeroed) -> +0x80. Per definition: the 0xfc-byte header into a local; header
//   +0x20 bit 0x40 (extended/streamed definition): name (up to 0x80 bytes; n = bytes read), extra count c (4 bytes,
//   r = bytes read), "ext definition" of c*32 + r + n + 0xfc bytes (the first c*32 + r + n zeroed, original quirk), the
//   header copied in, the name strcpy'd to +0xfc, +0x17c = c, and c*32 bytes read to +0x180; otherwise a "base definiton"
//   (0xfc bytes, header copy) and its "pcm raw data" (+0xf0 bytes) -> +0xf8, read from the stream. Each goes to
//   +0x80[i]. Then byte +0x7c = 1, "audio buffer list" (+0x78 * 4, zeroed) -> +0x108, and 0x004fa7f0 (this, def, i)
//   uploads every definition. The original's EAX on return is a leftover: returns 0.
uint32_t AudioPC_LoadDefinitions(uint8_t* self, uint8_t* stream, uint32_t size)
{
    if (Field<uint8_t>(self, 0xc) == 0)
    {
        const uint32_t tmp = GameHeapAlloc(2, size, 0x0058a484);  // "tmp buffer"
        orig::fn_004f59a0(u(stream), size, tmp, 0xffffffffu);
        GameHeapFree(tmp);
        return 0;
    }
    // Original frame (0x190 bytes): +0 index, +8 extra count, +0xc hash entry size, +0x10 name[0x80], +0x90 header.
    uint32_t hashSize = 0;
    uint32_t extraCount = 0;
    char name[0x80] = {};
    uint32_t header[0x3f] = {};
    StreamRead(stream, 4, u(&hashSize));
    uint32_t table = GameHeapAlloc(0x10, 0x20, 0x0058b5e8);
    if (table != 0)
    {
        Field<uint32_t>(P(table), 0) = 0;
        Field<uint32_t>(P(table), 4) = 0;
        Field<uint32_t>(P(table), 8) = 0;
        Field<uint32_t>(P(table), 0x10) = 0x004f4cc0;
        Field<uint32_t>(P(table), 0x14) = u(self);
        Field<uint32_t>(P(table), 0x1c) = 0;
    }
    else
    {
        table = 0;
    }
    Field<uint32_t>(self, 0xa0) = table;
    const uint32_t entries = GameHeapAlloc(2, hashSize, 0x0058a46c);  // "audio hashtable entries"
    Field<uint32_t>(self, 0xa4) = entries;
    orig::fn_004f59a0(u(stream), hashSize, entries, 0x1e);
    orig::fn_004f1220(Field<uint32_t>(self, 0xa4), Field<uint32_t>(self, 0xa0));
    orig::fn_004f59a0(u(stream), 4, u(self) + 0x78, 0x1e);
    orig::fn_004f59a0(u(stream), 4, u(self) + 0x8c, 0x1e);
    const uint32_t defs = GameHeapAlloc(2, Field<uint32_t>(self, 0x78) << 2, 0x0058ad40);  // "audio definition list"
    Field<uint32_t>(self, 0x80) = defs;
    std::memset(P(defs), 0, Field<uint32_t>(self, 0x78) << 2);
    for (uint32_t i = 0; i < Field<uint32_t>(self, 0x78); ++i)
    {
        StreamRead(stream, 0xfc, u(header));
        uint32_t def;
        if ((reinterpret_cast<const uint8_t*>(header)[0x20] & 0x40) != 0)
        {
            const uint32_t nameBytes = StreamRead(stream, 0x80, u(name));
            const uint32_t countBytes = StreamRead(stream, 4, u(&extraCount));
            const uint32_t extra = (extraCount << 5) + countBytes + nameBytes;
            def = GameHeapAlloc(2, extra + 0xfc, 0x0058a45c);  // "ext definition"
            std::memset(P(def), 0, extra);
            std::memcpy(P(def), header, 0xfc);
            char* dst = static_cast<char*>(P(def + 0xfc));
            const char* src = name;
            char c;
            do
            {
                c = *src++;
                *dst++ = c;
            } while (c != 0);
            Field<uint32_t>(P(def), 0x17c) = extraCount;
            if (extraCount != 0)
                orig::fn_004f59a0(u(stream), extraCount << 5, def + 0x180, 0x1e);
        }
        else
        {
            def = GameHeapAlloc(2, 0xfc, 0x0058a44c);  // "base definiton"
            std::memcpy(P(def), header, 0xfc);
            const uint32_t pcm = GameHeapAlloc(2, Field<uint32_t>(P(def), 0xf0), 0x0058a43c);  // "pcm raw data"
            Field<uint32_t>(P(def), 0xf8) = pcm;
            StreamRead(stream, Field<uint32_t>(P(def), 0xf0), pcm);
        }
        Field<uint32_t*>(self, 0x80)[i] = def;
    }
    Field<uint8_t>(self, 0x7c) = 1;
    const uint32_t buffers = GameHeapAlloc(2, Field<uint32_t>(self, 0x78) << 2, 0x0058a490);  // "audio buffer list"
    Field<uint32_t>(self, 0x108) = buffers;
    std::memset(P(buffers), 0, Field<uint32_t>(self, 0x78) << 2);
    for (uint32_t i = 0; i < Field<uint32_t>(self, 0x78); ++i)
    {
        const uint32_t def = Field<uint32_t*>(self, 0x80)[i];
        if (def != 0)
            orig::fn_004fa7f0(u(self), def, i);
    }
    return 0;
}
static uint32_t AudioPC_LoadDefinitions_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t /*a2*/)
{
    return AudioPC_LoadDefinitions(static_cast<uint8_t*>(P(ecx)), static_cast<uint8_t*>(P(a0)), a1);
}
ABI_ENTRY_004fab80(AudioPC_LoadDefinitions_Entry, AudioPC_LoadDefinitions_Abi)
HOOK(0x004fab80, "AudioPC_LoadDefinitions", AudioPC_LoadDefinitions_Entry, nullptr);

// 0x004fafc0 (ecx = sound system; vtable 0x0058a230 +0x84, overriding the base shutdown 0x004f2590; not executed in
// recorded runs). Shutdown: byte +0x11c = 1; virtual +0xa8(1) (stop all) timed with QueryPerformanceCounter and logged
// "%2.2f\tEND : Audio StopAll\n" (Log_Printf 0x004e0b30, seconds as double); every definition +0x80[i] (i < +0x78)
// and its data +0xf8 freed (the table +0x80 itself is left to the base); the base shutdown 0x004f2590 timed and logged
// "%2.2f\tEND2 : Audio GlobalReset\n"; the buffer table +0x108 freed and cleared; byte +0x11c = 0, +0x120 = 0.
// The base then calls virtual +0x7c (AudioPC_UnloadDefinition -> 0x004f4c50) for each index while +0x80[i] still holds
// the freed pointers (0x004f4c50 skips the data free because byte +0x7c is set).
uint32_t AudioPC_Shutdown(uint8_t* self)
{
    const QpcFn qpc = GameGlobal<QpcFn>(0x0056b08c);
    LARGE_INTEGER counter;
    Field<uint8_t>(self, 0x11c) = 1;
    qpc(&counter);
    const float start = ElapsedSeconds(counter.QuadPart);
    VCall<uint32_t>(self, 0xa8 / 4, 1u);
    qpc(&counter);
    GameLog(reinterpret_cast<const char*>(0x0058a420), static_cast<double>(ElapsedSeconds(counter.QuadPart) - start));
    for (uint32_t i = 0; i < Field<uint32_t>(self, 0x78); ++i)
    {
        const uint32_t def = Field<uint32_t*>(self, 0x80)[i];
        if (def != 0)
        {
            const uint32_t data = Field<uint32_t>(P(def), 0xf8);
            if (data != 0)
                GameHeapFree(data);
            GameHeapFree(Field<uint32_t*>(self, 0x80)[i]);
        }
    }
    qpc(&counter);
    const float resetStart = ElapsedSeconds(counter.QuadPart);
    orig::fn_004f2590(u(self));
    qpc(&counter);
    GameLog(reinterpret_cast<const char*>(0x0058a400),
        static_cast<double>(ElapsedSeconds(counter.QuadPart) - resetStart));
    if (Field<uint32_t>(self, 0x108) != 0)
    {
        GameHeapFree(Field<uint32_t>(self, 0x108));
        Field<uint32_t>(self, 0x108) = 0;
    }
    Field<uint8_t>(self, 0x11c) = 0;
    Field<uint32_t>(self, 0x120) = 0;
    return 0;
}
static uint32_t AudioPC_Shutdown_Abi(uint32_t ecx)
{
    return AudioPC_Shutdown(static_cast<uint8_t*>(P(ecx)));
}
ABI_ENTRY_004fafc0(AudioPC_Shutdown_Entry, AudioPC_Shutdown_Abi)
HOOK(0x004fafc0, "AudioPC_Shutdown", AudioPC_Shutdown_Entry, nullptr);
