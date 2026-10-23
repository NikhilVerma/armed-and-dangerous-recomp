// Recovered (static, batch missing_02): a render-fx function that symbols/functions.csv does not list (Ghidra never
// made it a function: it is only reached through a pointer). Compile-checked only (the game was not run); the oracle
// (tools/compare.py) is still to do. Not executed in recorded runs as far as the tracker knows (no tracker row; the
// recorded traces also miss indirect calls): "not verifiable by compare yet".
//
// Disassembled with capstone from game.unpacked.exe, 0x004193a0 .. 0x004195dd (574 bytes; the tail `jmp 0x0046e2f0`
// at 0x004195d9 is followed by nop padding and the next function at 0x004195e0).
#include <cstdint>
#include <cstring>

#include "game.h"
#include "gen/orig_calls.h"
#include "hooks.h"

namespace
{
inline float& F(const void* p, uint32_t o)
{
    return Field<float>(p, o);
}
inline uint32_t& D(const void* p, uint32_t o)
{
    return Field<uint32_t>(p, o);
}
// A float constant of the game's .rdata: 0x005bcbc0 = 1/32768, 0x005bcba0 = pi/180, 0x005bcb68 = 10.0,
// 0x005bcd64 = -64.348099 (2 * 32.174 ft/s^2: guess, twice gravity in feet).
inline float K(uint32_t address)
{
    return GameGlobal<float>(address);
}
// 0x005391cd _rand (cdecl, returns eax 0..0x7fff).
inline int32_t Rand()
{
    return static_cast<int32_t>(static_cast<uint32_t>(orig::fn_005391cd()));
}
// 0x0056a7c8 _ftol2: truncates st0 (loaded from a float here); the caller uses only AL.
inline uint8_t FtolLow(float x)
{
    return static_cast<uint8_t>(static_cast<int64_t>(x));
}
// The inlined rand-based uniform (same code as Rand_FloatRange 0x0046e2c0): fild rand; fld hi; fsub lo; fmulp;
// fmul 1/32768; fadd lo (x87 at single precision: every step rounded to float).
inline float RandRange(float lo, float hi)
{
    const float r = static_cast<float>(Rand());
    const float span = hi - lo;
    const float scaled = r * span;
    const float unit = scaled * K(0x005bcbc0);
    return unit + lo;
}
// 0x0046e2f0 (ecx = particle; plain ret; recovered in src/engine/missing_01.cpp, no generated orig:: wrapper): the
// tail-jump target. Called at its original address (reaches the recovered version once hooked).
uint32_t CallFn0046e2f0(uint32_t particle)
{
    uint32_t r;
    __asm {
        push ebx
        push esi
        push edi
        push ebp
        mov ecx, particle
        mov eax, 0x0046e2f0
        call eax
        pop ebp
        pop edi
        pop esi
        pop ebx
        mov r, eax
    }
    return r;
}
}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// 0x004193a0 (cdecl callback: stack pool (unused), particle, emitter; plain ret, caller pops 12; returns eax of
// 0x0046e2f0). Reached only through a pointer: fn_0041a050 (src/renderer/render_r1.cpp, the per-emitter spawn step)
// loads `mov ecx, 0x004193a0` at 0x0041a149 / 0x0041a273 and passes it to FxSystem_SpawnInPool2 0x0041f4b0 /
// FxSystem_SpawnInPool 0x0041f470, which forward it to ParticleEmitter_SpawnAlong 0x0046e650 /
// ParticleEmitter_SpawnInBox 0x0046e4c0; those call it per new particle (`call [esp+N]` at 0x0046e726 / 0x0046e613
// after three pushes, `add esp, 0xc`). Name from that call context and the behaviour below. Initialises a spawned
// particle from the emitter's parameter ranges (each "rand(lo, hi)" = lo + rand() * (hi - lo) / 32768):
//   +8 life = Rand_FloatRange(e+0x2c, e+0x30);
//   +0x28 velocity: Particle_RandomConeVelocity 0x0046e320 (esi = edi = particle+0x28, the spawn direction, in
//     place; cone = rand(e+0x4c, e+0x50) * pi/180 degrees; speed e+0x44 .. e+0x48);
//   e+0x190 (texture / animation object, refcount at +4) set: +0x4c = 10.0, +0x50 = 10.0 / life, +0x54 = it
//     (refcount++), +0x48 = 0; else +0x50 = +0x54 = 0, +0x4c = 1.0 and +0x48 = colour ARGB packed from the low bytes
//     of _ftol(e+0x70) (A), e+0x64 (R), e+0x68 (G), e+0x6c (B);
//   +0x40 = rand(e+0x14, e+0x18); +0x44 = -(+0x40 / life) when e+0x34 > e+0x38 (fcomp/test ah,0x41), else
//     rand(e+0x34, e+0x38); +0x3c = rand(e+0x3c, e+0x40); +0x34 = rand(e+0x1c, e+0x20);
//     +0x38 = rand(e+0x24, e+0x28) * -64.348099; +0x5c..+0x68 = e+0x54..+0x60 (4 dwords);
// then tail-jumps to 0x0046e2f0 with ecx = particle (frame intact, plain ret: reproduced as call-then-return).
// The original also writes its temporaries into the caller's argument slots 2 and 3 (dead after the call).
uint32_t ParticleEmitter_InitParticle(uint32_t pool, uint8_t* particle, uint8_t* emitter)
{
    (void)pool;
    F(particle, 8) = orig::fn_0046e2c0(D(emitter, 0x2c), D(emitter, 0x2c), D(emitter, 0x30));

    {
        const float hi = F(emitter, 0x50);
        const float lo = F(emitter, 0x4c);
        const float cone = RandRange(lo, hi) * K(0x005bcba0);
        uint32_t coneBits;
        std::memcpy(&coneBits, &cone, 4);
        const uint32_t velocity = reinterpret_cast<uint32_t>(particle + 0x28);
        orig::fn_0046e320(D(emitter, 0x48), velocity, velocity, coneBits, D(emitter, 0x44), D(emitter, 0x48));
    }

    if (D(emitter, 0x190) != 0)
    {
        D(particle, 0x4c) = 0x41200000u;  // 10.0f
        F(particle, 0x50) = K(0x005bcb68) / F(particle, 8);
        D(particle, 0x54) = D(emitter, 0x190);
        ++D(reinterpret_cast<void*>(D(emitter, 0x190)), 4);
        D(particle, 0x48) = 0;
    }
    else
    {
        D(particle, 0x50) = 0;
        D(particle, 0x54) = 0;
        D(particle, 0x4c) = 0x3f800000u;  // 1.0f
        uint32_t c = static_cast<uint32_t>(FtolLow(F(emitter, 0x70))) << 8;
        c = (c & 0xffffff00u) | FtolLow(F(emitter, 0x64));
        c <<= 8;
        c |= FtolLow(F(emitter, 0x68));
        c <<= 8;
        c |= FtolLow(F(emitter, 0x6c));
        D(particle, 0x48) = c;
    }

    const float v = RandRange(F(emitter, 0x14), F(emitter, 0x18));
    F(particle, 0x40) = v;
    if (F(emitter, 0x34) > F(emitter, 0x38))
        F(particle, 0x44) = -(v / F(particle, 8));
    else
        F(particle, 0x44) = RandRange(F(emitter, 0x34), F(emitter, 0x38));
    F(particle, 0x3c) = RandRange(F(emitter, 0x3c), F(emitter, 0x40));
    F(particle, 0x34) = RandRange(F(emitter, 0x1c), F(emitter, 0x20));
    F(particle, 0x38) = RandRange(F(emitter, 0x24), F(emitter, 0x28)) * K(0x005bcd64);
    D(particle, 0x5c) = D(emitter, 0x54);
    D(particle, 0x60) = D(emitter, 0x58);
    D(particle, 0x64) = D(emitter, 0x5c);
    D(particle, 0x68) = D(emitter, 0x60);
    return CallFn0046e2f0(reinterpret_cast<uint32_t>(particle));
}
static uint32_t ParticleEmitter_InitParticle_Abi(uint32_t a0, uint32_t a1, uint32_t a2)
{
    return ParticleEmitter_InitParticle(a0, reinterpret_cast<uint8_t*>(a1), reinterpret_cast<uint8_t*>(a2));
}
// Hand-written in the shape of src/gen/entry_stubs.h: no register inputs, three stack arguments, plain ret.
static __declspec(naked) void ParticleEmitter_InitParticle_Entry()
{
    __asm {
        push ecx
        push edx
        push ebx
        push esi
        push edi
        push ebp
        push dword ptr [esp + 36]
        push dword ptr [esp + 36]
        push dword ptr [esp + 36]
        call ParticleEmitter_InitParticle_Abi
        add esp, 12
        pop ebp
        pop edi
        pop esi
        pop ebx
        pop edx
        pop ecx
        ret
    }
}
HOOK(0x004193a0, "ParticleEmitter_InitParticle", ParticleEmitter_InitParticle_Entry, nullptr);
