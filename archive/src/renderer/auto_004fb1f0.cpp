// Recovered: 0x004fb1f0..0x004fcb30. Despite sitting between renderer code, 0x004fb1f0-0x004fbaa0 belong to the
// sound system (DirectSound: the game imports DSOUND.dll ordinal 11, and these call IDirectSoundBuffer SetPan,
// Stop, SetCurrentPosition, Lock/Unlock, GetCurrentPosition). 0x004fc860 and 0x004fcb30 are renderer (Direct3D 9)
// mesh registration. S2, Approach B.
//   0x004fb1f0 fn_004fb1f0
//   0x004fb360 fn_004fb360
//   0x004fb4b0 fn_004fb4b0
//   0x004fb540 fn_004fb540
//   0x004fb560 fn_004fb560
//   0x004fb790 fn_004fb790
//   0x004fb7b0 fn_004fb7b0
//   0x004fb820 fn_004fb820
//   0x004fb840 fn_004fb840
//   0x004fb9d0 fn_004fb9d0
//   0x004fbaa0 fn_004fbaa0
//   0x004fc860 Renderer_RegisterHWVertices
//   0x004fcb30 Renderer_RegisterHWTriangles
#include <cmath>
#include <cstring>

#include "gen/entry_stubs.h"
#include "gen/orig_calls.h"
#include "hooks.h"
#include "renderer.h"

#include <mmsystem.h>  // WAVEFORMATEX for dsound.h (WIN32_LEAN_AND_MEAN leaves it out)
#include <dsound.h>

uint32_t fn_00500d60(uint8_t* stream, uint32_t /*unused*/, uint32_t position, uint8_t* buffer, uint32_t count,
                     uint32_t* outBytes);  // auto_00500d60.cpp

namespace
{
template <class T>
uint32_t u(T* p)
{
    return reinterpret_cast<uint32_t>(p);
}

constexpr uint32_t kGameHeap = 0x0063a210;  // ecx of 0x004e07f0/0x004e08d0
const char* const kStrOutroNotSetup = reinterpret_cast<const char*>(0x0058a3d8);  // "Sequence Outro not setup right for\n%s"
const char* const kStrLockFailed = reinterpret_cast<const char*>(0x0058a3bc);     // "lock failed : %s, %d, %d\n"
const char* const kStrLock2Failed = reinterpret_cast<const char*>(0x0058a3a0);    // "lock2 failed : %s, %d, %d\n"
const char* const kStrLock3Failed = reinterpret_cast<const char*>(0x0058a384);    // "lock3 failed : %s, %d, %d\n"
const char* const kStrErrVertexDecl = reinterpret_cast<const char*>(0x00589ea4);  // "Error registering vertex declaration"
const char* const kStrErrHwVertices = reinterpret_cast<const char*>(0x00589e88);  // "Error creating hwvertices"
const char* const kStrHwVerticesTwice = reinterpret_cast<const char*>(0x00589e5c);
const char* const kStrErrHwTriangles = reinterpret_cast<const char*>(0x00589ddc);  // "Error creating hwtriangles"
const char* const kStrHwTrianglesTwice = reinterpret_cast<const char*>(0x00589db0);

// The voice's DirectSound buffer (+0x5c).
IDirectSoundBuffer*& VoiceBuffer(uint8_t* voice)
{
    return Field<IDirectSoundBuffer*>(voice, 0x5c);
}
}  // namespace

// 0x004fb1f0 (ecx = sound system; [voice, position], callee pops) (not executed in the comparison scenarios):
// positional update of one voice. Stores the distance from the listener position (system +0xac/+0xb0/+0xb4) to
// `position` in voice +0x60 (sqrt of (dz*dz + dy*dy) + dx*dx, as the x87 code orders it). Without a DirectSound
// buffer (+0x5c) returns 0. Otherwise: the system's virtual 4 (+0x10, returns float) with the voice, handed to
// virtual 16 (+0x40) with (voice, value); the position is transformed by the listener matrix (+0xc4) with
// 0x004e36a0, and the pan is clamp(1.0 / distance * local x, -0.9, 0.9) (0.0 when distance < 1e-4) * 10000 + 0.5,
// truncated, passed to IDirectSoundBuffer::SetPan. Then, unless voice flag 0x40: with flag 0x800, when the voice
// is closer than its record's (+0x0c) range (+0x34) calls virtual 57 (+0xe4) with (voice +4, 0, 1); without it,
// when the record flags (+0x20) have 0x40 and the voice is not closer than that range, the voice is stopped:
// flag 0x20 -> virtual 56 (+0xe0) with (voice +4, 0, 1), else 0x004f26a0(system, voice, 1); both return 0.
// Returns 1 otherwise.
uint32_t fn_004fb1f0(uint8_t* system, uint8_t* voice, const float* position)
{
    float dx = position[0] - Field<float>(system, 0xac);
    float dy = position[1] - Field<float>(system, 0xb0);
    float dz = position[2] - Field<float>(system, 0xb4);
    float distance = sqrtf((dz * dz + dy * dy) + dx * dx);
    Field<float>(voice, 0x60) = distance;
    if (!VoiceBuffer(voice))
        return 0;

    float value = VCall<float>(system, 0x10 / 4, voice);
    VCall<void>(system, 0x40 / 4, voice, value);
    float local[3];
    orig::fn_004e36a0(u(position), u(system + 0xc4), 1, u(local));

    float pan;
    if (Field<float>(voice, 0x60) < GameGlobal<float>(0x005bcbb0))  // 1e-4
    {
        pan = GameGlobal<float>(0x0056c05c);  // 0.0
    }
    else
    {
        pan = GameGlobal<float>(0x005bcb2c) / Field<float>(voice, 0x60) * local[0];  // 1.0
        if (pan < GameGlobal<float>(0x005bcd74))                                   // -0.9
            pan = GameGlobal<float>(0x005bcd74);
        else if (pan > GameGlobal<float>(0x005bcc38))  // 0.9
            pan = GameGlobal<float>(0x005bcc38);
    }
    pan = pan * GameGlobal<float>(0x005bcc1c);  // 10000.0
    pan = pan + GameGlobal<float>(0x005bcb4c);  // 0.5
    VoiceBuffer(voice)->SetPan(static_cast<LONG>(pan));

    uint32_t flags = Field<uint32_t>(voice, 0);
    if (flags & 0x40)
        return 1;
    uint8_t* record = Field<uint8_t*>(voice, 0xc);
    if (flags & 0x800)
    {
        if (Field<float>(voice, 0x60) < Field<float>(record, 0x34))
            VCall<uint32_t>(system, 0xe4 / 4, Field<uint32_t>(voice, 4), 0u, 1u);
        return 1;
    }
    if (!(record[0x20] & 0x40))
        return 1;
    if (Field<float>(voice, 0x60) < Field<float>(record, 0x34))
        return 1;
    if (flags & 0x20)
    {
        VCall<uint32_t>(system, 0xe0 / 4, Field<uint32_t>(voice, 4), 0u, 1u);
        return 0;
    }
    orig::fn_004f26a0(u(system), u(voice), 1);
    return 0;
}

static uint32_t fn_004fb1f0_Abi(uint32_t ecx, uint32_t voice, uint32_t position)
{
    return fn_004fb1f0(reinterpret_cast<uint8_t*>(ecx), reinterpret_cast<uint8_t*>(voice),
                       reinterpret_cast<const float*>(position));
}
ABI_ENTRY_004fb1f0(fn_004fb1f0_Entry, fn_004fb1f0_Abi)
HOOK(0x004fb1f0, "fn_004fb1f0", fn_004fb1f0_Entry, nullptr);

// 0x004fb360 (ecx = sound system; [voice, immediate (byte)], callee pops) (not executed in the comparison
// scenarios): stop a voice. Voice flag 0x1000: IDirectSoundBuffer::Stop on its buffer (+0x5c). Otherwise, when
// the record (+0x0c) flags (+0x20) have 0x1000000, `immediate` is 0, system byte +0x11c is 0 and voice byte +0x64
// has bit 7, it looks up the record's outro sequence (0x004f2470 with record +0x9c). Found with data (+0xf8):
// record flag 0x40 -> the voice's player (+0x78) virtual 4 (+0x10) with (0x004f19b0(sequence +0xfc), 0, 0, 1),
// else virtual 5 (+0x14) then 0x004e5e80(player, sequence +0xf0, sequence +0xf8); either way clears voice +0x64
// bit 7 and returns. Found without data: "Sequence Outro not setup right for\n%s" goes to 0x00407ff0 (an empty
// function). Then: record flag 0x200000 with `immediate` 0 and system +0x11c 0 sets voice +0x8c = record +0x48
// (a fade-out, guessed), else Stop on the buffer.
void fn_004fb360(uint8_t* system, uint8_t* voice, uint8_t immediate)
{
    if (Field<uint32_t>(voice, 0) & 0x1000)
    {
        VoiceBuffer(voice)->Stop();
        return;
    }
    uint8_t frame[0x108];  // the original's locals: 4 bytes for 0x004f2470 (ebx), 260 for 0x004f19b0 (esi)
    uint8_t* record = Field<uint8_t*>(voice, 0xc);
    if ((Field<uint32_t>(record, 0x20) & 0x1000000) && immediate == 0 && Field<uint8_t>(system, 0x11c) == 0 &&
        (Field<uint8_t>(voice, 0x64) & 0x80))
    {
        uint8_t* sequence =
            reinterpret_cast<uint8_t*>(orig::fn_004f2470(u(frame), u(system), u(record + 0x9c), 1));
        if (sequence)
        {
            if (Field<uint32_t>(sequence, 0xf8) != 0)
            {
                void* player = Field<void*>(voice, 0x78);
                if (Field<uint8_t>(Field<uint8_t*>(voice, 0xc), 0x20) & 0x40)
                {
                    uint32_t name = orig::fn_004f19b0(u(sequence + 0xfc), u(frame + 4));
                    VCall<uint32_t>(Field<void*>(voice, 0x78), 0x10 / 4, name, 0u, 0u, 1u);
                }
                else
                {
                    VCall<uint32_t>(player, 0x14 / 4);
                    orig::fn_004e5e80(u(player), Field<uint32_t>(sequence, 0xf0), Field<uint32_t>(sequence, 0xf8));
                }
                Field<uint32_t>(voice, 0x64) &= ~0x80u;
                return;
            }
            orig::fn_00407ff0(Field<uint32_t>(system, 8), u(kStrOutroNotSetup), Field<uint32_t>(voice, 0xc), 0);
        }
    }
    record = Field<uint8_t*>(voice, 0xc);
    if ((Field<uint32_t>(record, 0x20) & 0x200000) && immediate == 0 && Field<uint8_t>(system, 0x11c) == 0)
    {
        Field<uint32_t>(voice, 0x8c) = Field<uint32_t>(record, 0x48);
        return;
    }
    VoiceBuffer(voice)->Stop();
}

static uint32_t fn_004fb360_Abi(uint32_t ecx, uint32_t voice, uint32_t immediate)
{
    fn_004fb360(reinterpret_cast<uint8_t*>(ecx), reinterpret_cast<uint8_t*>(voice), static_cast<uint8_t>(immediate));
    return 0;
}
ABI_ENTRY_004fb360(fn_004fb360_Entry, fn_004fb360_Abi)
HOOK(0x004fb360, "fn_004fb360", fn_004fb360_Entry, nullptr);

// 0x004fb4b0 (ecx = sound system; [voice], callee pops) (not executed in the comparison scenarios): release a
// voice's resources, then hand it to 0x004f2d10 (whose result it returns). Only for voices with flag bit 31: with
// flag 0x400 (cleared here, and record (+0x0c) flag 0x20000000 too) the buffer (+0x5c) is rewound with
// SetCurrentPosition(0) and forgotten (shared, not released); otherwise it is released. Flag 0x8000 decrements
// system +0x120. The player object (+0x78) is destroyed in place (virtual 0 with 0) unless record flag 0x40000000,
// and forgotten.
uint32_t fn_004fb4b0(uint8_t* system, uint8_t* voice)
{
    uint32_t flags = Field<uint32_t>(voice, 0);
    if (static_cast<int32_t>(flags) < 0)
    {
        if (flags & 0x400)
        {
            Field<uint32_t>(voice, 0) = flags & ~0x400u;
            Field<uint32_t>(Field<uint8_t*>(voice, 0xc), 0x20) &= 0xdfffffffu;
            VoiceBuffer(voice)->SetCurrentPosition(0);
            VoiceBuffer(voice) = nullptr;
        }
        else if (VoiceBuffer(voice))
        {
            VoiceBuffer(voice)->Release();
            VoiceBuffer(voice) = nullptr;
        }
        if (Field<uint32_t>(voice, 0) & 0x8000)
            Field<int32_t>(system, 0x120) -= 1;
        if (void* player = Field<void*>(voice, 0x78))
        {
            if (!(Field<uint32_t>(Field<uint8_t*>(voice, 0xc), 0x20) & 0x40000000))
                VCall<uint32_t>(player, 0, 0u);
            Field<uint32_t>(voice, 0x78) = 0;
        }
    }
    return orig::fn_004f2d10(u(system), u(voice));
}

static uint32_t fn_004fb4b0_Abi(uint32_t ecx, uint32_t voice)
{
    return fn_004fb4b0(reinterpret_cast<uint8_t*>(ecx), reinterpret_cast<uint8_t*>(voice));
}
ABI_ENTRY_004fb4b0(fn_004fb4b0_Entry, fn_004fb4b0_Abi)
HOOK(0x004fb4b0, "fn_004fb4b0", fn_004fb4b0_Entry, nullptr);

// 0x004fb540 (ecx = sound system; [out], callee pops) (not executed in the comparison scenarios): copy the
// listener position (+0xac, three floats; the distance origin of 0x004fb1f0) to `out`. Returns `out`.
float* fn_004fb540(uint8_t* system, float* out)
{
    out[0] = Field<float>(system, 0xac);
    out[1] = Field<float>(system, 0xb0);
    out[2] = Field<float>(system, 0xb4);
    return out;
}

static uint32_t fn_004fb540_Abi(uint32_t ecx, uint32_t out)
{
    return u(fn_004fb540(reinterpret_cast<uint8_t*>(ecx), reinterpret_cast<float*>(out)));
}
ABI_ENTRY_004fb540(fn_004fb540_Entry, fn_004fb540_Abi)
HOOK(0x004fb540, "fn_004fb540", fn_004fb540_Entry, nullptr);

// 0x004fb560 (ecx = sound system; [voice, stream], callee pops) (not executed in the comparison scenarios): refill
// a streaming DirectSound buffer up to the play cursor. Stream: +0x00 buffer, +0x08 flags, +0x18 buffer size,
// +0x20 write offset, +0x28 last play position, +0x2c bytes played. Flag 4: the play position comes from the
// system's virtual 1 (+4) with (stream, &pos) and the played count advances (with wrap-around); else pos = size.
// The region from the write offset to pos (two pieces when it wraps) is locked (IDirectSoundBuffer::Lock), filled
// from stream +8 with fn_00500d60 and unlocked; lock failures log "lock failed", "lock2 failed", "lock3 failed"
// through 0x004e4480 with the voice's record (+0x0c). When the first piece's read reports false the second is
// skipped. The unwrapped case locks the voice's buffer (+0x5c) but unlocks the stream's (+0x00), as the original
// does. Finally stream flag 0x1000 calls the system's virtual 12 (+0x30) with (voice, 1).
uint32_t fn_004fb560(uint8_t* system, uint8_t* voice, uint8_t* stream)
{
    uint8_t* source = stream + 8;  // esi for fn_00500d60; its first dword holds the flags
    uint32_t pos;
    if (stream[8] & 4)
    {
        VCall<uint32_t>(system, 4 / 4, stream, &pos);
        uint32_t last = Field<uint32_t>(stream, 0x28);
        if (pos < last)
            Field<uint32_t>(stream, 0x2c) += Field<uint32_t>(stream, 0x18) - last + pos;
        else
            Field<uint32_t>(stream, 0x2c) += pos - last;
        Field<uint32_t>(stream, 0x28) = pos;
    }
    else
    {
        pos = Field<uint32_t>(stream, 0x18);
    }

    void* data;
    DWORD dataBytes;
    uint32_t got;
    uint32_t start = Field<uint32_t>(stream, 0x20);
    if (pos < start)
    {
        uint32_t tail = Field<uint32_t>(stream, 0x18) - start;
        if (tail + pos == 0)
            return pos;
        if (tail != 0)
        {
            IDirectSoundBuffer* buffer = Field<IDirectSoundBuffer*>(stream, 0);
            HRESULT hr = buffer->Lock(start, tail, &data, &dataBytes, nullptr, nullptr, 0);
            if (hr < 0)
            {
                uint32_t offset = Field<uint32_t>(stream, 0x20);
                orig::fn_004e4480(u(&data), Field<uint32_t>(buffer, 0), u(stream), u(system),
                                  Field<uint32_t>(system, 8), u(kStrLockFailed), Field<uint32_t>(voice, 0xc), offset,
                                  Field<uint32_t>(stream, 0x18) - offset);
            }
            else
            {
                uint32_t ok = fn_00500d60(source, u(system), Field<uint32_t>(stream, 0x20),
                                          static_cast<uint8_t*>(data), dataBytes, &got);
                Field<IDirectSoundBuffer*>(stream, 0)->Unlock(data, got, nullptr, 0);
                if (static_cast<uint8_t>(ok) == 0)
                    goto done;
            }
        }
        if (pos != 0)
        {
            HRESULT hr = Field<IDirectSoundBuffer*>(stream, 0)->Lock(0, pos, &data, &dataBytes, nullptr, nullptr, 0);
            if (hr < 0)
            {
                orig::fn_004e4480(u(&data), u(source), u(stream), u(system), Field<uint32_t>(system, 8),
                                  u(kStrLock2Failed), Field<uint32_t>(voice, 0xc), 0, pos);
            }
            else
            {
                fn_00500d60(source, u(system), 0, static_cast<uint8_t*>(data), dataBytes, &got);
                Field<IDirectSoundBuffer*>(stream, 0)->Unlock(data, got, nullptr, 0);
            }
        }
    }
    else
    {
        uint32_t count = pos - start;
        if (count == 0)
            goto done;
        IDirectSoundBuffer* buffer = VoiceBuffer(voice);
        HRESULT hr = buffer->Lock(start, count, &data, &dataBytes, nullptr, nullptr, 0);
        if (hr < 0)
        {
            uint32_t offset = Field<uint32_t>(stream, 0x20);
            orig::fn_004e4480(u(&data), Field<uint32_t>(buffer, 0), offset, u(system), Field<uint32_t>(system, 8),
                              u(kStrLock3Failed), Field<uint32_t>(voice, 0xc), offset, pos - offset);
        }
        else
        {
            fn_00500d60(source, u(system), Field<uint32_t>(stream, 0x20), static_cast<uint8_t*>(data), dataBytes,
                        &got);
            Field<IDirectSoundBuffer*>(stream, 0)->Unlock(data, got, nullptr, 0);
        }
    }
done:
    uint32_t flags = Field<uint32_t>(source, 0);
    if (flags & 0x1000)
        return VCall<uint32_t>(system, 0x30 / 4, voice, 1u);
    return flags;
}

static uint32_t fn_004fb560_Abi(uint32_t ecx, uint32_t voice, uint32_t stream)
{
    return fn_004fb560(reinterpret_cast<uint8_t*>(ecx), reinterpret_cast<uint8_t*>(voice),
                       reinterpret_cast<uint8_t*>(stream));
}
ABI_ENTRY_004fb560(fn_004fb560_Entry, fn_004fb560_Abi)
HOOK(0x004fb560, "fn_004fb560", fn_004fb560_Entry, nullptr);

// 0x004fb790 ([stream, out position], callee pops; no `this` used) (not executed in the comparison scenarios):
// IDirectSoundBuffer::GetCurrentPosition(out, null) on the stream's buffer (+0). Likely the sound system's
// virtual 1, which 0x004fb560 calls with (stream, &pos). Returns the HRESULT.
HRESULT fn_004fb790(uint8_t* stream, DWORD* playCursor)
{
    return Field<IDirectSoundBuffer*>(stream, 0)->GetCurrentPosition(playCursor, nullptr);
}

static uint32_t fn_004fb790_Abi(uint32_t stream, uint32_t playCursor)
{
    return static_cast<uint32_t>(fn_004fb790(reinterpret_cast<uint8_t*>(stream), reinterpret_cast<DWORD*>(playCursor)));
}
ABI_ENTRY_004fb790(fn_004fb790_Entry, fn_004fb790_Abi)
HOOK(0x004fb790, "fn_004fb790", fn_004fb790_Entry, nullptr);

// 0x004fb7b0 (ecx = object) (not executed in the comparison scenarios): returns the dword at +0x10c.
uint32_t fn_004fb7b0(uint8_t* object)
{
    return Field<uint32_t>(object, 0x10c);
}

static uint32_t fn_004fb7b0_Abi(uint32_t ecx)
{
    return fn_004fb7b0(reinterpret_cast<uint8_t*>(ecx));
}
ABI_ENTRY_004fb7b0(fn_004fb7b0_Entry, fn_004fb7b0_Abi)
HOOK(0x004fb7b0, "fn_004fb7b0", fn_004fb7b0_Entry, nullptr);

// 0x004fb820 (ecx = object; [enable (byte)], callee pops) (not executed in the comparison scenarios): set or clear
// bit 1 of the flags at +0x0c. Returns the new flags.
uint32_t fn_004fb820(uint8_t* object, uint8_t enable)
{
    uint32_t flags = Field<uint32_t>(object, 0xc);
    flags = enable ? (flags | 2) : (flags & ~2u);
    Field<uint32_t>(object, 0xc) = flags;
    return flags;
}

static uint32_t fn_004fb820_Abi(uint32_t ecx, uint32_t enable)
{
    return fn_004fb820(reinterpret_cast<uint8_t*>(ecx), static_cast<uint8_t>(enable));
}
ABI_ENTRY_004fb820(fn_004fb820_Entry, fn_004fb820_Abi)
HOOK(0x004fb820, "fn_004fb820", fn_004fb820_Entry, nullptr);

// 0x004fb840 (ecx = object; [enable (byte)], callee pops) (not executed in the comparison scenarios): flags at
// +0x0c: when `enable`, set bit 4 and clear bit 0; always clear bit 3. Returns the flags after the first step
// (eax in the original; with `enable` 0 the original leaves only the byte in al, here 0).
uint32_t fn_004fb840(uint8_t* object, uint8_t enable)
{
    uint32_t result = 0;
    if (enable)
    {
        uint32_t flags = Field<uint32_t>(object, 0xc) | 0x10;
        Field<uint32_t>(object, 0xc) = flags;
        flags &= ~1u;
        Field<uint32_t>(object, 0xc) = flags;
        result = flags;
    }
    Field<uint32_t>(object, 0xc) &= ~8u;
    return result;
}

static uint32_t fn_004fb840_Abi(uint32_t ecx, uint32_t enable)
{
    return fn_004fb840(reinterpret_cast<uint8_t*>(ecx), static_cast<uint8_t>(enable));
}
ABI_ENTRY_004fb840(fn_004fb840_Entry, fn_004fb840_Abi)
HOOK(0x004fb840, "fn_004fb840", fn_004fb840_Entry, nullptr);

// 0x004fb9d0 (ecx = object; [flags], callee pops) (not executed in the comparison scenarios): scalar deleting
// destructor of the singleton created by 0x004fba30 (pointer at 0x0063a0b8). It is the first entry of vtable
// 0x0058a1f0, which it installs; clears the singleton pointer when it is this object and, with flags bit 0, frees
// the object on the game heap (0x004e08d0, 0x0063a210). Returns the object.
void* fn_004fb9d0(uint8_t* object, uint32_t flags)
{
    bool isSingleton = GameGlobal<uint8_t*>(0x0063a0b8) == object;
    Field<uint32_t>(object, 0) = 0x0058a1f0;
    if (isSingleton)
        GameGlobal<uint32_t>(0x0063a0b8) = 0;
    if (flags & 1)
        orig::fn_004e08d0(kGameHeap, u(object));
    return object;
}

static uint32_t fn_004fb9d0_Abi(uint32_t ecx, uint32_t flags)
{
    return u(fn_004fb9d0(reinterpret_cast<uint8_t*>(ecx), flags));
}
ABI_ENTRY_004fb9d0(fn_004fb9d0_Entry, fn_004fb9d0_Abi)
HOOK(0x004fb9d0, "fn_004fb9d0", fn_004fb9d0_Entry, nullptr);

// 0x004fbaa0 (no inputs; returns st0) (not executed in the comparison scenarios): returns the constant 1.0
// (0x005bcb2c).
float fn_004fbaa0()
{
    return GameGlobal<float>(0x005bcb2c);
}

static float fn_004fbaa0_Abi()
{
    return fn_004fbaa0();
}
ABI_ENTRY_004fbaa0(fn_004fbaa0_Entry, fn_004fbaa0_Abi)
HOOK(0x004fbaa0, "fn_004fbaa0", fn_004fbaa0_Entry, nullptr);

// 0x004fc860 (ecx = renderer; [hw vertices, register (byte)], callee pops) (not executed in the comparison
// scenarios): create or release the Direct3D resources of a hardware vertex block. Block: +0x08 flags (bit 0
// registered), +0x0c vertex count, two streams of 0x14 bytes at +0x70 (data, stride, IDirect3DVertexBuffer9*),
// +0xec vertex declaration, +0xf0 its D3DVERTEXELEMENT9 array. Register: logs "Attempting to register HWVertices
// twice!!" if already registered (returns 1); else, with a vertex count, creates the declaration if missing
// (CreateVertexDeclaration, logs "Error registering vertex declaration"), and for each stream with a stride a
// vertex buffer of count * stride bytes (renderer usage +0x148c, FVF 0, pool +0x1488), filled with Lock(0, 0) /
// copy / Unlock; a failure logs "Error creating hwvertices" and makes the result 0. Sets flag bit 0. Unregister:
// releases the declaration and both buffers and clears bit 0. Returns 0 for a null block, else the result (1 unless
// a buffer failed).
uint32_t Renderer_RegisterHWVertices(Renderer* r, uint8_t* hw, uint8_t reg)
{
    uint8_t ok = 1;
    if (!hw)
        return 0;
    if (reg)
    {
        if (hw[8] & 1)
        {
            GameLog(kStrHwVerticesTwice);
            return ok;
        }
        if (Field<uint32_t>(hw, 0xc) != 0)
        {
            if (Field<uint32_t>(hw, 0xec) == 0 &&
                r->device->CreateVertexDeclaration(reinterpret_cast<const D3DVERTEXELEMENT9*>(hw + 0xf0),
                                                   reinterpret_cast<IDirect3DVertexDeclaration9**>(hw + 0xec)) != 0)
                GameLog(kStrErrVertexDecl);
            for (int i = 0; i < 2; ++i)
            {
                uint8_t* entry = hw + 0x70 + i * 0x14;
                uint32_t stride = Field<uint32_t>(entry, 4);
                if (stride == 0)
                    continue;
                uint32_t size = Field<uint32_t>(hw, 0xc) * stride;
                auto** vb = reinterpret_cast<IDirect3DVertexBuffer9**>(entry + 8);
                void* dest;
                if (r->device->CreateVertexBuffer(size, r->bufferUsage, 0, r->bufferPool, vb, nullptr) == 0 &&
                    (*vb)->Lock(0, 0, &dest, 0) == 0)
                {
                    memcpy(dest, Field<const void*>(entry, 0), size);
                    (*vb)->Unlock();
                }
                else
                {
                    ok = 0;
                    GameLog(kStrErrHwVertices);
                }
            }
        }
        Field<uint32_t>(hw, 8) |= 1;
        return ok;
    }
    if (hw[8] & 1)
    {
        if (auto* decl = Field<IDirect3DVertexDeclaration9*>(hw, 0xec))
        {
            decl->Release();
            Field<uint32_t>(hw, 0xec) = 0;
        }
        for (int i = 0; i < 2; ++i)
        {
            uint8_t* entry = hw + 0x70 + i * 0x14;
            if (auto* vb = Field<IDirect3DVertexBuffer9*>(entry, 8))
            {
                vb->Release();
                Field<uint32_t>(entry, 8) = 0;
            }
        }
        Field<uint32_t>(hw, 8) &= ~1u;
    }
    return ok;
}

static uint32_t Renderer_RegisterHWVertices_Abi(uint32_t ecx, uint32_t hw, uint32_t reg)
{
    return Renderer_RegisterHWVertices(reinterpret_cast<Renderer*>(ecx), reinterpret_cast<uint8_t*>(hw),
                                       static_cast<uint8_t>(reg));
}
ABI_ENTRY_004fc860(Renderer_RegisterHWVertices_Entry, Renderer_RegisterHWVertices_Abi)
HOOK(0x004fc860, "Renderer_RegisterHWVertices", Renderer_RegisterHWVertices_Entry, nullptr);

// 0x004fcb30 (ecx = renderer; [hw triangles, register (byte)], callee pops) (not executed in the comparison
// scenarios): create or release the index buffer of a hardware triangle block. Block: +0x08 flags (bit 0
// registered), +0x10 index count, +0x18 IDirect3DIndexBuffer9*, +0x3c 16-bit index data. Register: logs "Attempting
// to register HWTriangles twice!!" if already registered (returns 1); else CreateIndexBuffer(count * 2, renderer
// usage +0x148c, D3DFMT_INDEX16 (0x65), pool +0x1488), filled with Lock(0, 0) / copy / Unlock; failure logs "Error
// creating hwtriangles" and returns 0. Either way sets flag bit 0 and clears bits 1-2. Unregister: releases the
// buffer and clears bits 0-2. Returns 0 for a null block.
uint32_t Renderer_RegisterHWTriangles(Renderer* r, uint8_t* tri, uint8_t reg)
{
    uint8_t ok = 1;
    if (!tri)
        return 0;
    if (reg)
    {
        if (tri[8] & 1)
        {
            GameLog(kStrHwTrianglesTwice);
            return ok;
        }
        uint32_t size = Field<uint32_t>(tri, 0x10) << 1;
        auto** ib = reinterpret_cast<IDirect3DIndexBuffer9**>(tri + 0x18);
        void* dest;
        if (r->device->CreateIndexBuffer(size, r->bufferUsage, static_cast<D3DFORMAT>(0x65), r->bufferPool, ib,
                                         nullptr) == 0 &&
            (*ib)->Lock(0, 0, &dest, 0) == 0)
        {
            memcpy(dest, Field<const void*>(tri, 0x3c), size);
            (*ib)->Unlock();
        }
        else
        {
            ok = 0;
            GameLog(kStrErrHwTriangles);
        }
        uint32_t flags = Field<uint32_t>(tri, 8) | 1;
        Field<uint32_t>(tri, 8) = flags;
        Field<uint32_t>(tri, 8) = flags & 0xfffffff9u;
        return ok;
    }
    if (tri[8] & 1)
    {
        if (auto* ib = Field<IDirect3DIndexBuffer9*>(tri, 0x18))
        {
            ib->Release();
            Field<uint32_t>(tri, 0x18) = 0;
        }
        Field<uint32_t>(tri, 8) &= 0xfffffff8u;
    }
    return ok;
}

static uint32_t Renderer_RegisterHWTriangles_Abi(uint32_t ecx, uint32_t tri, uint32_t reg)
{
    return Renderer_RegisterHWTriangles(reinterpret_cast<Renderer*>(ecx), reinterpret_cast<uint8_t*>(tri),
                                        static_cast<uint8_t>(reg));
}
ABI_ENTRY_004fcb30(Renderer_RegisterHWTriangles_Entry, Renderer_RegisterHWTriangles_Abi)
HOOK(0x004fcb30, "Renderer_RegisterHWTriangles", Renderer_RegisterHWTriangles_Entry, nullptr);
