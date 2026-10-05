// Recovered: batch ai_05 (0x004369a0..0x0043edb0): MonkObject, CaptainObject, GruntObject, GoliathObject, Q112,
// FakeGrunt and GenericAIObject virtual methods, including the GenericAIObject script-event handler 0x0043d180.
// Static recovery 2026-10-05, compile-checked only (the game was not run); the oracle (tools/compare.py) is still to
// do.
//
// Class names come from the vtable that holds the function (slot 1 returns a descriptor whose +0x14 is
// "CLASS(Name)"); GenericAIObject is vtable 0x005b2958. Event names are the strings the handlers compare with
// _stricmp. Names stay fn_XXXXXXXX where there is no such evidence; "guess" marks interpretations.
//
// Conventions used here (as in ai_06.cpp / ai_02.cpp):
//   - registers whose low byte the original clears (xor cl, cl) are passed with the upper bytes the original left
//     when known, otherwise 0 (the callees test only the low byte);
//   - _stricmp 0x005448ee and the cue 0x00493b70 receive ecx = 0 (their ecx is whatever the previous call left; ABI
//     table: "runtime unconfirmed");
//   - 0x004e7880 returns st0 (the ABI table lists eax) -> Normalize3, copied from ai_06.cpp;
//   - where the original reuses its own argument slots as locals, the arguments are a mutable local array.
#include <cmath>
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
inline uint32_t& D(uint32_t a)
{
    return *reinterpret_cast<uint32_t*>(a);
}
inline int32_t& I(uint32_t a)
{
    return *reinterpret_cast<int32_t*>(a);
}
inline float& Fl(uint32_t a)
{
    return *reinterpret_cast<float*>(a);
}
inline uint8_t& By(uint32_t a)
{
    return *reinterpret_cast<uint8_t*>(a);
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
// A float constant of the game's .rdata (0x0056c05c = 0.0).
inline float K(uint32_t address)
{
    return GameGlobal<float>(address);
}
// Virtual call through the object's vtable at byte offset `off` (thiscall, callee pops).
template <class... A>
inline uint32_t VC(uint32_t object, uint32_t off, A... args)
{
    return VCall<uint32_t>(P(object), static_cast<int>(off / 4), args...);
}
// Same, the caller tests AL.
template <class... A>
inline bool VB(uint32_t object, uint32_t off, A... args)
{
    return static_cast<uint8_t>(VCall<uint32_t>(P(object), static_cast<int>(off / 4), args...)) != 0;
}
inline uint32_t GameP()
{
    return D(0x005fe490);
}
// 0x00604a98: the game clock (float seconds).
inline float Now()
{
    return K(0x00604a98);
}
// 0x005391cd (_rand), signed as the callers use it (imul / sar).
inline int32_t Rand()
{
    return static_cast<int32_t>(orig::fn_005391cd());
}
// 0x005448ee (_stricmp).
inline uint32_t Stricmp(uint32_t a, uint32_t b)
{
    return orig::fn_005448ee(0, a, b);
}
// Game+0x2c: the audio object (vtable 0x0058a230; see src/audio/audio_01.cpp).
inline uint32_t Audio()
{
    return D(GameP() + 0x2c);
}
// The reference drop the game inlines everywhere: --object+0x28; at zero, unless virtual +0xd8 says the object is
// kept, 0x00487da0 (eax = object) releases it.
inline void Unref(uint32_t object)
{
    if (--I(object + 0x28) == 0 && !VB(object, 0xd8))
        orig::fn_00487da0(object);
}
// 0x004e7880 (ecx = source vec3, edx = destination): normalises a 3D vector and returns its length in st0. The ABI
// table lists eax as the result; the callers pop st0, so it is called here with its real st0 result.
float Normalize3(uint32_t ecx_, uint32_t edx_)
{
    static const uint32_t target = 0x004e7880;
    float r;
    __asm {
        push ebx
        push esi
        push edi
        push ebp
        mov ecx, ecx_
        mov edx, edx_
        call dword ptr [target]
        pop ebp
        pop edi
        pop esi
        pop ebx
        fstp r
    }
    return r;
}
// The ambient-cue throttle shared by the voice functions below (0x00437690, 0x00437860, 0x00438fc0, 0x004391f0,
// 0x0043a200): nothing while clock - Game+0xb44 < Game+0xb50 (or unordered); otherwise Game+0xb44 = clock and
// Game+0xb50 = (Game+0xb4c - Game+0xb48) * rand() * K(0x005bcbc0) + Game+0xb48 (the next random interval). The
// x87 order is kept: fild rand; (b4c - b48); fmulp; * K; + b48.
bool CueThrottle()
{
    uint32_t g = GameP();
    float now = Now();
    if (!(now - Fl(g + 0xb44) >= Fl(g + 0xb50)))
        return false;
    Fl(g + 0xb44) = now;
    float hi = Fl(g + 0xb4c);
    float lo = Fl(g + 0xb48);
    float r = static_cast<float>(Rand());
    Fl(GameP() + 0xb50) = r * (hi - lo) * K(0x005bcbc0) + lo;
    return true;
}
}  // namespace

uint32_t GenericAIObject_HandleEvent(uint32_t self, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3);
uint32_t fn_0043ccb0(uint32_t obj);
uint32_t fn_0043cd70(uint32_t obj);
uint32_t fn_0043ce10();
uint32_t fn_0043eae0(uint32_t ebx, uint32_t target, uint32_t a1, uint32_t radius);
uint32_t fn_0043ec60(uint32_t self, uint32_t a0);
uint32_t fn_00438fc0(uint32_t self, uint32_t esi, uint32_t flags484, uint32_t flags488);
uint32_t fn_004391f0(uint32_t self, uint32_t esi, uint32_t flags484, uint32_t flags488);
uint32_t fn_00439d50(uint32_t state, uint32_t self, uint32_t a0, uint32_t a1);

// ---------------------------------------------------------------------------------------------------------------
// MonkObject (vtable 0x005aac28)
// ---------------------------------------------------------------------------------------------------------------

// 0x004369a0 (ecx = monk; stack: a0..a3; ret 0x10). MonkObject slot +0x18 (not executed in recorded runs). Ai_InitGeneric
// 0x004247f0 (same four arguments), then +0x604 = (Game+0xa84)+0xe88, +0x608 = (Game+0xa84)+0xe7c; when virtual +0xd8
// is true: the "ref_shoot_00" bone setup on +0x61c (0x004a0ea0, 0x004a0ef0 (0x19, 0x1a), 0x004a10e0, 0x004a0e00) and
// five smoke effects (Fx_Create 0x0041cc60, Game+0xa90): "fx_monk_smoke_sleeve" at "ref_sleeve_00"/"_01" and
// "fx_monk_smoke_body_0/1/2" at "ref_body_00/01/02" (a parameter block with +0 = 4, +0x18 = reference name,
// +0x1c = monk). Finally +0xb58 = rand() * 3 >> 15 + 1.
uint32_t MonkObject_Init(uint32_t self, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    uint32_t L[0x80 / 4] = {};  // the original's 0x80-byte frame; the effect parameter block starts at its base
    orig::fn_004247f0(self, a0, a1, a2, a3);
    uint32_t g = GameP();
    D(self + 0x604) = D(D(g + 0xa84) + 0xe88);
    D(self + 0x608) = D(D(g + 0xa84) + 0xe7c);
    if (VB(self, 0xd8))
    {
        uint32_t bones = self + 0x61c;
        orig::fn_004a0ea0(0x005b5f68u /* "ref_shoot_00" */, self, bones, 0, 0);
        orig::fn_004a0ef0(0x19, bones);
        orig::fn_004a0ef0(0x1a, bones);
        uint32_t r = orig::fn_004a10e0(bones, 0);
        orig::fn_004a0e00(r & ~0xffu, bones);
        L[0x1c / 4] = self;
        L[0] = 4;
        L[0x18 / 4] = 0x005b5f58u;  // "ref_sleeve_00"
        r = orig::fn_0041cc60(u(&L[0]) & ~0xffu, D(GameP() + 0xa90), 0x005b5f40u /* "fx_monk_smoke_sleeve" */, u(&L[0]));
        L[0x18 / 4] = 0x005b5f30u;  // "ref_sleeve_01"
        r = orig::fn_0041cc60(r & ~0xffu, D(GameP() + 0xa90), 0x005b5f40u, u(&L[0]));
        L[0x18 / 4] = 0x005b5f24u;  // "ref_body_00"
        r = orig::fn_0041cc60(r & ~0xffu, D(GameP() + 0xa90), 0x005b5f0cu /* "fx_monk_smoke_body_0" */, u(&L[0]));
        L[0x18 / 4] = 0x005b5f00u;  // "ref_body_01"
        r = orig::fn_0041cc60(r & ~0xffu, D(GameP() + 0xa90), 0x005b5ee8u /* "fx_monk_smoke_body_1" */, u(&L[0]));
        L[0x18 / 4] = 0x005b5edcu;  // "ref_body_02"
        orig::fn_0041cc60(r & ~0xffu, D(GameP() + 0xa90), 0x005b5ec4u /* "fx_monk_smoke_body_2" */, u(&L[0]));
    }
    int32_t r = Rand();
    D(self + 0xb58) = static_cast<uint32_t>((r * 3 >> 15) + 1);
    return static_cast<uint32_t>(r);
}

static uint32_t MonkObject_Init_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return MonkObject_Init(ecx, a0, a1, a2, a3);
}
ABI_ENTRY_004369a0(MonkObject_Init_Entry, MonkObject_Init_Abi)
HOOK(0x004369a0, "MonkObject_Init", MonkObject_Init_Entry, nullptr);

// 0x00436b30 (ecx = monk; stack: a0..a7; ret 0x20). MonkObject slot +0x1a0 (not executed in recorded runs): forwards
// to virtual +0x19c (a0, a4, a5, 0, 0, 0); a1..a3, a6, a7 are not read.
uint32_t fn_00436b30(uint32_t self, uint32_t a0, uint32_t a4, uint32_t a5)
{
    return VC(self, 0x19c, a0, a4, a5, 0u, 0u, 0u);
}

static uint32_t fn_00436b30_Abi(uint32_t ecx, uint32_t a0, uint32_t /*a1*/, uint32_t /*a2*/, uint32_t /*a3*/, uint32_t a4,
                                uint32_t a5, uint32_t /*a6*/, uint32_t /*a7*/)
{
    return fn_00436b30(ecx, a0, a4, a5);
}
ABI_ENTRY_00436b30(fn_00436b30_Entry, fn_00436b30_Abi)
HOOK(0x00436b30, "fn_00436b30", fn_00436b30_Entry, nullptr);

// 0x00436b50 (ecx = monk). MonkObject slot +0x188 (not executed in recorded runs): +0x484 |= 0x10, then a tail jump
// to 0x004273a0 (ecx, no stack arguments; frame intact) -> call and return its eax.
uint32_t fn_00436b50(uint32_t self)
{
    D(self + 0x484) |= 0x10;
    return orig::fn_004273a0(self);
}

static uint32_t fn_00436b50_Abi(uint32_t ecx)
{
    return fn_00436b50(ecx);
}
ABI_ENTRY_00436b50(fn_00436b50_Entry, fn_00436b50_Abi)
HOOK(0x00436b50, "fn_00436b50", fn_00436b50_Entry, nullptr);

// 0x00436b60 (ecx = monk; stack: byte flag; ret 4). MonkObject slot +0x18c (not executed in recorded runs). flag set:
// overwrites its argument with 1 and tail-jumps to 0x004274e0 (ecx; that one argument, ret 4) -> 0x004274e0(1).
// Otherwise +0x484 |= 0x10 and virtual +0x188().
uint32_t fn_00436b60(uint32_t self, uint32_t flag)
{
    if (static_cast<uint8_t>(flag))
        return orig::fn_004274e0(self, 1);
    D(self + 0x484) |= 0x10;
    return VC(self, 0x188);
}

static uint32_t fn_00436b60_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_00436b60(ecx, a0);
}
ABI_ENTRY_00436b60(fn_00436b60_Entry, fn_00436b60_Abi)
HOOK(0x00436b60, "fn_00436b60", fn_00436b60_Entry, nullptr);

// 0x00436b90 (ecx = monk). MonkObject slot +0x184 (not executed in recorded runs): +0xb58 = 0, virtual +0x240(1, 0, 0).
uint32_t fn_00436b90(uint32_t self)
{
    D(self + 0xb58) = 0;
    return VC(self, 0x240, 1u, 0u, 0u);
}

static uint32_t fn_00436b90_Abi(uint32_t ecx)
{
    return fn_00436b90(ecx);
}
ABI_ENTRY_00436b90(fn_00436b90_Entry, fn_00436b90_Abi)
HOOK(0x00436b90, "fn_00436b90", fn_00436b90_Entry, nullptr);

// 0x00436bb0 (ecx = monk; stack: 3 arguments, not read; ret 0xc; returns al = 1, upper eax as left by the last call).
// MonkObject slot +0x240 (not executed in recorded runs). The monk's "bamf" (guess: teleport) state:
//  - dead (Object_IsDead 0x00454780): nothing.
//  - counter +0xb58 > 0: clears flag 4 of +0x484, 0x00427600; without a target +0x588: +0x490 = 0, virtual +0x1b8(1).
//    With one: when the target's planar distance from +0x60c (the target's position, virtual +0x54) is within
//    +0x5d8 and flag 4 of +0x484 is (again) set: virtual +0x19c(2, 1, 0, 0, 0, 0) and +0x230(0); otherwise +0x490 = 0
//    and virtual +0x1b8(1).
//  - counter 0: picks a spot around the target (+0x588, else the player Game+0x748): up to five headings
//    (rand() * K(0x005bcdbc) - K(0x005bcb70) + i * K(0x005bcd2c) * K(0x005bcb74)), each turning the heading vector
//    from 0x00453150 by a z-rotation (0x004e36a0) and normalising it to K(0x005bcb2c); +0x934/+0x938 = target position
//    + dir * (rand() * K(0x005bcdfc) + K(0x005bccbc)), +0x93c = target z; accepted when 0x0046f280 (Game+0xaa4) says
//    so, then +0x93c is dropped onto the ground with two downward ray casts (0x004137e0, Game+0xa8c, length 100000).
//    No spot: +0x934 = own position (virtual +0x54). Then: own position into a local, flags (+0x48c &= ~8,
//    +0x484 &= ~0x10, +0x490 = 1.0, +0x494 = 5.0), virtual +0x1a0(2, 3, 0, 1, 1, 0, 0.5, 0.5), +0xf8(0x100, 0), an
//    upward push (0x00453150 vector, z = rand() * K(0x005bcdf4) + K(0x005bcd48), normalised, times mass 0x004581c0 *
//    K(0x005bcdf8), Body_ApplyImpulse 0x00458440), 0x00454c90(1), virtual +0x19c(0x6b, 1, 0, 0, 0, 0), ObjectList_Link
//    into (Game+0xa84)+0xf1c, sound "fol_monk_bamf_1" (rand() * 100 < 0x190000 after masking) or "fol_monk_bamf_2"
//    (audio +0xc4), and virtual +0x1b8(0xf0).
uint32_t MonkObject_Bamf(uint32_t self)
{
    uint32_t r = orig::fn_00454780(self);
    if (static_cast<uint8_t>(r))
        return (r & ~0xffu) | 1;
    if (D(self + 0xb58) > 0)
    {
        D(self + 0x484) &= ~4u;
        orig::fn_00427600(self);
        if (D(self + 0x588) == 0)
        {
            D(self + 0x490) = 0;
            return (VC(self, 0x1b8, 1u) & ~0xffu) | 1;
        }
        float own[3];
        VC(self, 0x54, u(own));
        uint32_t tp = self + 0x60c;
        VC(D(self + 0x588), 0x54, tp);
        float dy = Fl(tp + 4) - own[1];
        float dx = Fl(tp) - own[0];
        float rad = Fl(self + 0x5d8);
        if (!(rad * rad < dx * dx + dy * dy) && (D(self + 0x484) & 4))
        {
            VC(self, 0x19c, 2u, 1u, 0u, 0u, 0u, 0u);
            return (VC(self, 0x230, 0u) & ~0xffu) | 1;
        }
        D(self + 0x490) = 0;
        return (VC(self, 0x1b8, 1u) & ~0xffu) | 1;
    }

    uint32_t target = D(self + 0x588);
    if (target == 0)
        target = D(GameP() + 0x748);
    float heading[3];
    orig::fn_00453150(self, u(heading));
    float angle0 = static_cast<float>(Rand()) * K(0x005bcdbc) - K(0x005bcb70);
    uint32_t spot = self + 0x934;
    float dir[3];
    float pos[3];
    float m[17];
    uint32_t i = 0;
    bool found = false;
    do
    {
        float fi = static_cast<float>(i);
        std::memset(m, 0, sizeof(m));
        float angle = fi * K(0x005bcd2c) * K(0x005bcb74) + angle0;
        float s = static_cast<float>(std::sin(static_cast<double>(angle)));
        reinterpret_cast<uint32_t*>(m)[10] = 0x3f800000;
        reinterpret_cast<uint32_t*>(m)[15] = 0x3f800000;
        reinterpret_cast<uint32_t*>(m)[16] = 1;
        float c = static_cast<float>(std::cos(static_cast<double>(angle)));
        m[0] = c;
        m[1] = -s;
        m[4] = s;
        m[5] = c;
        orig::fn_004e36a0(u(heading), u(m), 1, u(dir));
        float len = std::sqrt(dir[2] * dir[2] + dir[1] * dir[1] + dir[0] * dir[0]);
        if (len != K(0x0056c05c))
        {
            float k = K(0x005bcb2c) / len;
            dir[0] = dir[0] * k;
            dir[1] = dir[1] * k;
            dir[2] = k * dir[2];
        }
        VC(target, 0x54, u(pos));
        float r1 = static_cast<float>(Rand());
        Fl(spot) = (r1 * K(0x005bcdfc) + K(0x005bccbc)) * dir[0] + pos[0];
        float r2 = static_cast<float>(Rand());
        uint32_t g = GameP();
        D(self + 0x93c) = reinterpret_cast<uint32_t*>(pos)[2];
        float y = (r2 * K(0x005bcdfc) + K(0x005bccbc)) * dir[1] + pos[1];
        Fl(self + 0x938) = y;
        if (static_cast<uint8_t>(orig::fn_0046f280(D(g + 0xaa4), D(spot), fb(y))))
        {
            // 0x00436f66: drop +0x93c onto the ground.
            float hit;
            Fl(self + 0x93c) = pos[2] + K(0x005bcc04);
            dir[0] = 0.0f;
            dir[1] = 0.0f;
            reinterpret_cast<uint32_t*>(dir)[2] = 0xbf800000;
            bool h = static_cast<uint8_t>(orig::fn_004137e0(u(&hit), u(dir), D(g + 0xa8c), spot, 0x47c35000)) != 0;
            Fl(self + 0x93c) = h ? (Fl(self + 0x93c) - hit) + K(0x005bcd2c) : Fl(self + 0x93c) - K(0x005bcc04);
            if (i < 5)
            {
                Fl(self + 0x93c) = pos[2] + K(0x005bcc04);
                dir[0] = 0.0f;
                dir[1] = 0.0f;
                reinterpret_cast<uint32_t*>(dir)[2] = 0xbf800000;
                h = static_cast<uint8_t>(orig::fn_004137e0(u(&hit), u(dir), D(GameP() + 0xa8c), spot, 0x47c35000)) != 0;
                Fl(self + 0x93c) = h ? (Fl(self + 0x93c) - hit) + K(0x005bcd2c) : Fl(self + 0x93c) - K(0x005bcc04);
                found = true;
            }
            break;
        }
        ++i;
    } while (i < 5);
    if (!found)
        VC(self, 0x54, spot);  // 0x00436e71

    // 0x00436e79
    VC(self, 0x54, u(pos));
    D(self + 0x48c) &= ~8u;
    D(self + 0x484) &= ~0x10u;
    D(self + 0x490) = 0x3f800000;
    D(self + 0x494) = 0x40a00000;
    VC(self, 0x1a0, 2u, 3u, 0u, 1u, 1u, 0u, 0x3f000000u, 0x3f000000u);
    VC(self, 0xf8, 0x100u, 0u);
    orig::fn_00453150(self, u(dir));
    dir[2] = static_cast<float>(Rand()) * K(0x005bcdf4) + K(0x005bcd48);
    float len = std::sqrt(dir[1] * dir[1] + dir[0] * dir[0] + dir[2] * dir[2]);
    if (len != K(0x0056c05c))
    {
        float k = K(0x005bcb2c) / len;
        dir[0] = dir[0] * k;
        dir[1] = dir[1] * k;
        dir[2] = k * dir[2];
    }
    float mass = orig::fn_004581c0(self) * K(0x005bcdf8);
    dir[0] = dir[0] * mass;
    dir[1] = dir[1] * mass;
    dir[2] = mass * dir[2];
    orig::fn_00458440(0, u(dir), self);
    orig::fn_00454c90(self, 0, 1);  // ecx: xor cl, cl after 0x00458440 clobbered it (upper bytes unknown)
    VC(self, 0x19c, 0x6bu, 1u, 0u, 0u, 0u, 0u);
    orig::fn_004546d0(D(D(GameP() + 0xa84) + 0xf1c), self);
    int32_t pick = (Rand() * 100) & static_cast<int32_t>(0xffff8000);
    if (pick < 0x190000)
        VC(Audio(), 0xc4, 0x005b5eb4u /* "fol_monk_bamf_1" */, self, 0u, 0u, 0xffffffffu, 0u);
    else
        VC(Audio(), 0xc4, 0x005b5ea4u /* "fol_monk_bamf_2" */, self, 0u, 0u, 0xffffffffu, 0u);
    return (VC(self, 0x1b8, 0xf0u) & ~0xffu) | 1;
}

static uint32_t MonkObject_Bamf_Abi(uint32_t ecx, uint32_t /*a0*/, uint32_t /*a1*/, uint32_t /*a2*/)
{
    return MonkObject_Bamf(ecx);
}
ABI_ENTRY_00436bb0(MonkObject_Bamf_Entry, MonkObject_Bamf_Abi)
HOOK(0x00436bb0, "MonkObject_Bamf", MonkObject_Bamf_Entry, nullptr);

// 0x00437130 (ecx = monk; returns al). MonkObject slot +0x220 (not executed in recorded runs). While the counter
// +0xb58 is non-zero it is decremented (al = 0). At zero: +0xb58 = rand() * 3 >> 15 + 1, then n = rand() * 500 >> 15:
// n < 50 and flag 0x4000 of +0x484 clear: +0x884 = rand() * 3 >> 15 + 1, virtual +0x1b8(0xf2), al = 1; n < 200: the
// target's position (+0x588 virtual +0x54) into +0x60c, virtual +0x19c(0x26, 0, 0, 0, 0, 0) and +0x1b8(0xf1), al = 1;
// otherwise al = 0.
uint32_t fn_00437130(uint32_t self)
{
    uint32_t c = D(self + 0xb58);
    if (c > 0)
    {
        D(self + 0xb58) = c - 1;
        return (c - 1) & ~0xffu;
    }
    D(self + 0xb58) = static_cast<uint32_t>((Rand() * 3 >> 15) + 1);
    uint32_t n = static_cast<uint32_t>(Rand() * 500 >> 15);
    if (n < 0x32 && !(D(self + 0x484) & 0x4000))
    {
        D(self + 0x884) = static_cast<uint32_t>((Rand() * 3 >> 15) + 1);
        return (VC(self, 0x1b8, 0xf2u) & ~0xffu) | 1;
    }
    if (n >= 0xc8)
        return n & ~0xffu;
    VC(D(self + 0x588), 0x54, self + 0x60c);
    VC(self, 0x19c, 0x26u, 0u, 0u, 0u, 0u, 0u);
    return (VC(self, 0x1b8, 0xf1u) & ~0xffu) | 1;
}

static uint32_t fn_00437130_Abi(uint32_t ecx)
{
    return fn_00437130(ecx);
}
ABI_ENTRY_00437130(fn_00437130_Entry, fn_00437130_Abi)
HOOK(0x00437130, "fn_00437130", fn_00437130_Entry, nullptr);

// 0x004371e0 (ecx = monk; stack: damage message; ret 4). MonkObject slot +0xd0 (the damage slot; GenericAIObject's is
// GenericAIObject_Slot52 0x00430340) (not executed in recorded runs). Ignored when flag 0x4000 of +0x48c is set or
// the message's source (+0x2c) says virtual +0xe0 (dead). Otherwise a message with flag 0x20 sets +0x878 =
// rand() * K(0x005bcf54) + K(0x005bcbb0); any other goes to 0x00430340. Original bug: a null message pointer passes
// the source test and is then dereferenced (test byte [esi], 0x20).
uint32_t fn_004371e0(uint32_t self, uint32_t msg)
{
    if (D(self + 0x48c) & 0x4000)
        return D(self + 0x48c);
    if (msg != 0)
    {
        uint32_t src = D(msg + 0x2c);
        if (src != 0)
        {
            uint32_t r = VC(src, 0xe0);
            if (static_cast<uint8_t>(r))
                return r;
        }
    }
    if (By(msg) & 0x20)
    {
        int32_t r = Rand();
        Fl(self + 0x878) = static_cast<float>(r) * K(0x005bcf54) + K(0x005bcbb0);
        return static_cast<uint32_t>(r);
    }
    return orig::fn_00430340(self, msg);
}

static uint32_t fn_004371e0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_004371e0(ecx, a0);
}
ABI_ENTRY_004371e0(fn_004371e0_Entry, fn_004371e0_Abi)
HOOK(0x004371e0, "fn_004371e0", fn_004371e0_Entry, nullptr);

// 0x00437240 (ecx = monk; stack: 1 argument, not read; ret 4). MonkObject slot +0x1b0 (not executed in recorded
// runs): 0x00426630 (eax = monk) true: +0x878 = 0 and its eax is returned; otherwise virtual +0x19c(0x72, 1, 0, 0, 0,
// 0) and +0x1b8(0xf4).
uint32_t fn_00437240(uint32_t self)
{
    uint32_t r = orig::fn_00426630(self);
    if (static_cast<uint8_t>(r))
    {
        D(self + 0x878) = 0;
        return r;
    }
    VC(self, 0x19c, 0x72u, 1u, 0u, 0u, 0u, 0u);
    return VC(self, 0x1b8, 0xf4u);
}

static uint32_t fn_00437240_Abi(uint32_t ecx, uint32_t /*a0*/)
{
    return fn_00437240(ecx);
}
ABI_ENTRY_00437240(fn_00437240_Entry, fn_00437240_Abi)
HOOK(0x00437240, "fn_00437240", fn_00437240_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// CaptainObject / SniperCaptain, GruntObject / BowGruntObject voice and state helpers
// ---------------------------------------------------------------------------------------------------------------

// 0x004375c0 (ecx = object; returns al). CaptainObject / SniperCaptain slot +0x25c (not executed in recorded runs):
// 0x0044e600() true and not dead (Object_IsDead 0x00454780, esi = object) -> al = 1, else al = 0.
uint32_t fn_004375c0(uint32_t self)
{
    uint32_t r = orig::fn_0044e600();
    if (static_cast<uint8_t>(r))
    {
        r = orig::fn_00454780(self);
        if (!static_cast<uint8_t>(r))
            return (r & ~0xffu) | 1;
    }
    return r & ~0xffu;
}

static uint32_t fn_004375c0_Abi(uint32_t ecx)
{
    return fn_004375c0(ecx);
}
ABI_ENTRY_004375c0(fn_004375c0_Entry, fn_004375c0_Abi)
HOOK(0x004375c0, "fn_004375c0", fn_004375c0_Entry, nullptr);

// 0x00437690 (ecx = captain). CaptainObject / SniperCaptain slot +0x144 (not executed in recorded runs): the cue
// throttle (Game+0xb44/+0xb50), then the cue 0x00493b70 (ebx = 3; 0x0063a19c with edi = 0x005ce5b4 when flag
// 0x200000 of +0x484 is set, else 0x0063a1a0 with edi = 0x005ce5c0; stack: list, captain+0xb50, captain, 0, 0, 0).
uint32_t fn_00437690(uint32_t self)
{
    if (!CueThrottle())
        return 0;
    if (D(self + 0x484) & 0x200000)
        return orig::fn_00493b70(0, 3, 0x005ce5b4, 0x0063a19c, self + 0xb50, self, 0, 0, 0);
    return orig::fn_00493b70(0, 3, 0x005ce5c0, 0x0063a1a0, self + 0xb50, self, 0, 0, 0);
}

static uint32_t fn_00437690_Abi(uint32_t ecx)
{
    return fn_00437690(ecx);
}
ABI_ENTRY_00437690(fn_00437690_Entry, fn_00437690_Abi)
HOOK(0x00437690, "fn_00437690", fn_00437690_Entry, nullptr);

// 0x00437860 (ecx = captain; returns al = cue played). CaptainObject / SniperCaptain slot +0x13c (not executed in
// recorded runs): the cue throttle, then 0x00493b70 (0x0063a1a4, ebx = 6, edi = 0x005ce5cc with flag 0x200000 of
// +0x484; else 0x0063a1a8, ebx = 3, edi = 0x005ce5e4; stack: list, captain+0xb50, captain, 0, 0, 0).
uint32_t fn_00437860(uint32_t self)
{
    if (!CueThrottle())
        return 0;
    uint32_t r;
    if (D(self + 0x484) & 0x200000)
        r = orig::fn_00493b70(0, 6, 0x005ce5cc, 0x0063a1a4, self + 0xb50, self, 0, 0, 0);
    else
        r = orig::fn_00493b70(0, 3, 0x005ce5e4, 0x0063a1a8, self + 0xb50, self, 0, 0, 0);
    return (r & ~0xffu) | 1;
}

static uint32_t fn_00437860_Abi(uint32_t ecx)
{
    return fn_00437860(ecx);
}
ABI_ENTRY_00437860(fn_00437860_Entry, fn_00437860_Abi)
HOOK(0x00437860, "fn_00437860", fn_00437860_Entry, nullptr);

// 0x00438fc0 (eax = grunt, esi = grunt+0xb50; stack: flags +0x484, flags +0x488; caller pops). Called by 0x004390b0
// (not executed in recorded runs): the cue throttle, then 0x00493b70 (stack: list, esi, grunt, 0, 0, 0) with list
// 0x0063a1ac (ebx = 1, edi = 0x005ce5f0) when flag 0x200000 of +0x484 is set, else 0x0063a1b4 (ebx = 2, edi =
// 0x005ce5f8) when flag 0x20000000 of +0x488 is set, else 0x0063a1b0 (ebx = 1, edi = 0x005ce5f4).
uint32_t fn_00438fc0(uint32_t self, uint32_t esi, uint32_t flags484, uint32_t flags488)
{
    if (!CueThrottle())
        return 0;
    if (flags484 & 0x200000)
        return orig::fn_00493b70(0, 1, 0x005ce5f0, 0x0063a1ac, esi, self, 0, 0, 0);
    if (flags488 & 0x20000000)
        return orig::fn_00493b70(0, 2, 0x005ce5f8, 0x0063a1b4, esi, self, 0, 0, 0);
    return orig::fn_00493b70(0, 1, 0x005ce5f4, 0x0063a1b0, esi, self, 0, 0, 0);
}

static uint32_t fn_00438fc0_Abi(uint32_t eax, uint32_t esi, uint32_t a0, uint32_t a1)
{
    return fn_00438fc0(eax, esi, a0, a1);
}
ABI_ENTRY_00438fc0(fn_00438fc0_Entry, fn_00438fc0_Abi)
HOOK(0x00438fc0, "fn_00438fc0", fn_00438fc0_Entry, nullptr);

// 0x004390b0 (ecx = grunt). GruntObject / BowGruntObject slot +0x144 (not executed in recorded runs):
// 0x00438fc0(eax = grunt, esi = grunt+0xb50; +0x484, +0x488).
uint32_t fn_004390b0(uint32_t self)
{
    return fn_00438fc0(self, self + 0xb50, D(self + 0x484), D(self + 0x488));
}

static uint32_t fn_004390b0_Abi(uint32_t ecx)
{
    return fn_004390b0(ecx);
}
ABI_ENTRY_004390b0(fn_004390b0_Entry, fn_004390b0_Abi)
HOOK(0x004390b0, "fn_004390b0", fn_004390b0_Entry, nullptr);

// 0x004391f0 (eax = grunt, esi = grunt+0xb50; stack: flags +0x484, flags +0x488; caller pops; returns al = cue
// played). Called by 0x004392c0 (executed in recorded runs): the cue throttle, then 0x00493b70 (ebx = 4; stack: list,
// esi, grunt, 0, 0, 0) with 0x0063a1b8 / edi 0x005ce600 (flag 0x200000 of +0x484), 0x0063a1c0 / 0x005ce620 (flag
// 0x20000000 of +0x488) or 0x0063a1bc / 0x005ce610.
uint32_t fn_004391f0(uint32_t self, uint32_t esi, uint32_t flags484, uint32_t flags488)
{
    if (!CueThrottle())
        return 0;
    uint32_t list, edi;
    if (flags484 & 0x200000)
    {
        list = 0x0063a1b8;
        edi = 0x005ce600;
    }
    else if (flags488 & 0x20000000)
    {
        list = 0x0063a1c0;
        edi = 0x005ce620;
    }
    else
    {
        list = 0x0063a1bc;
        edi = 0x005ce610;
    }
    return (orig::fn_00493b70(0, 4, edi, list, esi, self, 0, 0, 0) & ~0xffu) | 1;
}

static uint32_t fn_004391f0_Abi(uint32_t eax, uint32_t esi, uint32_t a0, uint32_t a1)
{
    return fn_004391f0(eax, esi, a0, a1);
}
ABI_ENTRY_004391f0(fn_004391f0_Entry, fn_004391f0_Abi)
HOOK(0x004391f0, "fn_004391f0", fn_004391f0_Entry, nullptr);

// 0x004392c0 (ecx = grunt). GruntObject / BowGruntObject slot +0x13c (not executed in recorded runs):
// 0x004391f0(eax = grunt, esi = grunt+0xb50; +0x484, +0x488).
uint32_t fn_004392c0(uint32_t self)
{
    return fn_004391f0(self, self + 0xb50, D(self + 0x484), D(self + 0x488));
}

static uint32_t fn_004392c0_Abi(uint32_t ecx)
{
    return fn_004392c0(ecx);
}
ABI_ENTRY_004392c0(fn_004392c0_Entry, fn_004392c0_Abi)
HOOK(0x004392c0, "fn_004392c0", fn_004392c0_Entry, nullptr);

// 0x00439d50 (eax = state +0x460, esi = object; stack: +0xb50 (sound handle), +0x588 (target); caller pops; returns
// al). Called by 0x00439d90 (not executed in recorded runs). State other than 0x6e: al = 1. In state 0x6e: al = 0
// when there is a target, 0x0044e600() is true or audio +0xdc(handle) is true; otherwise al = not dead.
uint32_t fn_00439d50(uint32_t state, uint32_t self, uint32_t handle, uint32_t target)
{
    if (state != 0x6e)
        return (state & ~0xffu) | 1;
    if (target != 0)
        return target & ~0xffu;
    uint32_t r = orig::fn_0044e600();
    if (static_cast<uint8_t>(r))
        return r & ~0xffu;
    r = VC(Audio(), 0xdc, handle);
    if (static_cast<uint8_t>(r))
        return r & ~0xffu;
    r = orig::fn_00454780(self);
    if (static_cast<uint8_t>(r))
        return r & ~0xffu;
    return (r & ~0xffu) | 1;
}

static uint32_t fn_00439d50_Abi(uint32_t eax, uint32_t esi, uint32_t a0, uint32_t a1)
{
    return fn_00439d50(eax, esi, a0, a1);
}
ABI_ENTRY_00439d50(fn_00439d50_Entry, fn_00439d50_Abi)
HOOK(0x00439d50, "fn_00439d50", fn_00439d50_Entry, nullptr);

// 0x00439d90 (ecx = object). GruntObject / MonkObject / BowGruntObject slot +0x25c (not executed in recorded runs):
// 0x00439d50(eax = +0x460, esi = object; +0xb50, +0x588).
uint32_t fn_00439d90(uint32_t self)
{
    return fn_00439d50(D(self + 0x460), self, D(self + 0xb50), D(self + 0x588));
}

static uint32_t fn_00439d90_Abi(uint32_t ecx)
{
    return fn_00439d90(ecx);
}
ABI_ENTRY_00439d90(fn_00439d90_Entry, fn_00439d90_Abi)
HOOK(0x00439d90, "fn_00439d90", fn_00439d90_Entry, nullptr);

// 0x0043a0f0 (ecx = object; returns al = !Object_IsDead, upper eax from it). GoliathObject slot +0x25c (not executed
// in recorded runs).
uint32_t fn_0043a0f0(uint32_t self)
{
    uint32_t r = orig::fn_00454780(self);
    return (r & ~0xffu) | (static_cast<uint8_t>(r) ? 0u : 1u);
}

static uint32_t fn_0043a0f0_Abi(uint32_t ecx)
{
    return fn_0043a0f0(ecx);
}
ABI_ENTRY_0043a0f0(fn_0043a0f0_Entry, fn_0043a0f0_Abi)
HOOK(0x0043a0f0, "fn_0043a0f0", fn_0043a0f0_Entry, nullptr);

// 0x0043a200 (ecx = object; returns al = cue played). Q112Object slot +0x13c (not executed in recorded runs): the cue
// throttle, then 0x00493b70 (ebx = 4, edi = 0x005ce630; 0x0063a1c4, object+0xb50, object, 0, 0, 0).
uint32_t fn_0043a200(uint32_t self)
{
    if (!CueThrottle())
        return 0;
    return (orig::fn_00493b70(0, 4, 0x005ce630, 0x0063a1c4, self + 0xb50, self, 0, 0, 0) & ~0xffu) | 1;
}

static uint32_t fn_0043a200_Abi(uint32_t ecx)
{
    return fn_0043a200(ecx);
}
ABI_ENTRY_0043a200(fn_0043a200_Entry, fn_0043a200_Abi)
HOOK(0x0043a200, "fn_0043a200", fn_0043a200_Entry, nullptr);

// 0x0043a980 (ecx = object; returns al = (+0x48c & 0x102) == 0, upper eax = -(+0x48c & 0x102)). GenericAIObject
// slot +0x1c0 (also Penguin, ICObject, GruntLadder, FakeGrunt) (not executed in recorded runs).
uint32_t fn_0043a980(uint32_t self)
{
    uint32_t v = 0u - (D(self + 0x48c) & 0x102);
    return (v & ~0xffu) | (v == 0 ? 1u : 0u);
}

static uint32_t fn_0043a980_Abi(uint32_t ecx)
{
    return fn_0043a980(ecx);
}
ABI_ENTRY_0043a980(fn_0043a980_Entry, fn_0043a980_Abi)
HOOK(0x0043a980, "fn_0043a980", fn_0043a980_Entry, nullptr);

// 0x0043a9a0 (ecx = object; returns al = (+0x9bc == 0)). GoliathObject slot +0x250 (not executed in recorded runs).
uint32_t fn_0043a9a0(uint32_t self)
{
    uint32_t v = D(self + 0x9bc);
    return (v & ~0xffu) | (v == 0 ? 1u : 0u);
}

static uint32_t fn_0043a9a0_Abi(uint32_t ecx)
{
    return fn_0043a9a0(ecx);
}
ABI_ENTRY_0043a9a0(fn_0043a9a0_Entry, fn_0043a9a0_Abi)
HOOK(0x0043a9a0, "fn_0043a9a0", fn_0043a9a0_Entry, nullptr);

// 0x0043a9b0 (ecx = object; returns al). Grunt / BowGrunt / Captain / SniperCaptain slot +0x118 (not executed in
// recorded runs): al = 0 when flag 2 of +0x48c is set, else al = (+0x870 == 0).
uint32_t fn_0043a9b0(uint32_t self)
{
    if (By(self + 0x48c) & 2)
        return 0;
    uint32_t v = D(self + 0x870);
    return (v & ~0xffu) | (v == 0 ? 1u : 0u);
}

static uint32_t fn_0043a9b0_Abi(uint32_t ecx)
{
    return fn_0043a9b0(ecx);
}
ABI_ENTRY_0043a9b0(fn_0043a9b0_Entry, fn_0043a9b0_Abi)
HOOK(0x0043a9b0, "fn_0043a9b0", fn_0043a9b0_Entry, nullptr);

// 0x0043a9f0 (stack: 1 argument, not read; ret 4). FakeGruntObject slot +0x1a4 (not executed in recorded runs):
// returns the string "4_fake_grunts" (0x005b47e4).
uint32_t fn_0043a9f0()
{
    return 0x005b47e4;
}

static uint32_t fn_0043a9f0_Abi(uint32_t /*a0*/)
{
    return fn_0043a9f0();
}
ABI_ENTRY_0043a9f0(fn_0043a9f0_Entry, fn_0043a9f0_Abi)
HOOK(0x0043a9f0, "fn_0043a9f0", fn_0043a9f0_Entry, nullptr);

// 0x0043bc50 (ecx = object; stack: id; ret 4). Grunt / BowGrunt / Captain / SniperCaptain slot +0x1a8 (not executed
// in recorded runs). Jump table 0x0043bc84 / 0x0043bc90 (read from the image): ids 8..0xc and 0x2f -> object+0x96c,
// 0x59 -> object+0x994, anything else 0.
uint32_t fn_0043bc50(uint32_t self, uint32_t id)
{
    switch (id)
    {
    case 0x8:
    case 0x9:
    case 0xa:
    case 0xb:
    case 0xc:
    case 0x2f:
        return self + 0x96c;
    case 0x59:
        return self + 0x994;
    default:
        return 0;
    }
}

static uint32_t fn_0043bc50_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0043bc50(ecx, a0);
}
ABI_ENTRY_0043bc50(fn_0043bc50_Entry, fn_0043bc50_Abi)
HOOK(0x0043bc50, "fn_0043bc50", fn_0043bc50_Entry, nullptr);

// 0x0043c560 (ecx = object; stack: id; ret 4). Q112Object / Q111Object / MolmerObject slot +0x1a8 (not executed in
// recorded runs). Jump table 0x0043c594 / 0x0043c5a0: ids 8..0xa and 0x2f -> +0x96c, 0x59 -> +0x994, else 0.
uint32_t fn_0043c560(uint32_t self, uint32_t id)
{
    switch (id)
    {
    case 0x8:
    case 0x9:
    case 0xa:
    case 0x2f:
        return self + 0x96c;
    case 0x59:
        return self + 0x994;
    default:
        return 0;
    }
}

static uint32_t fn_0043c560_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0043c560(ecx, a0);
}
ABI_ENTRY_0043c560(fn_0043c560_Entry, fn_0043c560_Abi)
HOOK(0x0043c560, "fn_0043c560", fn_0043c560_Entry, nullptr);

// 0x0043c640 (stack: 1 argument, not read; ret 4). ZeppelinObject slot +0x1a4 (not executed in recorded runs):
// returns the string "mini_zeppelin_ref" (0x005b31dc).
uint32_t fn_0043c640()
{
    return 0x005b31dc;
}

static uint32_t fn_0043c640_Abi(uint32_t /*a0*/)
{
    return fn_0043c640();
}
ABI_ENTRY_0043c640(fn_0043c640_Entry, fn_0043c640_Abi)
HOOK(0x0043c640, "fn_0043c640", fn_0043c640_Entry, nullptr);

// 0x0043c650 (ecx = object; stack: id; ret 4). ColossusObject slot +0x1a8 (not executed in recorded runs). Jump table
// 0x0043c678 / 0x0043c680: ids 4, 8..0xc, 0x2f, 0x59 -> +0x96c, else 0.
uint32_t fn_0043c650(uint32_t self, uint32_t id)
{
    switch (id)
    {
    case 0x4:
    case 0x8:
    case 0x9:
    case 0xa:
    case 0xb:
    case 0xc:
    case 0x2f:
    case 0x59:
        return self + 0x96c;
    default:
        return 0;
    }
}

static uint32_t fn_0043c650_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0043c650(ecx, a0);
}
ABI_ENTRY_0043c650(fn_0043c650_Entry, fn_0043c650_Abi)
HOOK(0x0043c650, "fn_0043c650", fn_0043c650_Entry, nullptr);

// 0x0043c7d0 (ecx = object; stack: id; ret 4). Q112Boss slot +0x1a8 (not executed in recorded runs). Jump table
// 0x0043c7f8 / 0x0043c800: ids 8..0xc, 0x2f, 0x59 -> +0x96c, else 0.
uint32_t fn_0043c7d0(uint32_t self, uint32_t id)
{
    switch (id)
    {
    case 0x8:
    case 0x9:
    case 0xa:
    case 0xb:
    case 0xc:
    case 0x2f:
    case 0x59:
        return self + 0x96c;
    default:
        return 0;
    }
}

static uint32_t fn_0043c7d0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0043c7d0(ecx, a0);
}
ABI_ENTRY_0043c7d0(fn_0043c7d0_Entry, fn_0043c7d0_Abi)
HOOK(0x0043c7d0, "fn_0043c7d0", fn_0043c7d0_Entry, nullptr);

// 0x0043c860 (ecx = boss; stack: a0, event name, a2, a3; ret 0x10). Q112Boss slot +0x34 (the event slot) (not executed
// in recorded runs): ignores "fire", passes every other event to GenericAIObject_HandleEvent 0x0043d180.
uint32_t Q112Boss_HandleEvent(uint32_t self, uint32_t a0, uint32_t name, uint32_t a2, uint32_t a3)
{
    uint32_t r = Stricmp(name, 0x005b3154u /* "fire" */);
    if (r == 0)
        return 0;
    return GenericAIObject_HandleEvent(self, a0, name, a2, a3);
}

static uint32_t Q112Boss_HandleEvent_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return Q112Boss_HandleEvent(ecx, a0, a1, a2, a3);
}
ABI_ENTRY_0043c860(Q112Boss_HandleEvent_Entry, Q112Boss_HandleEvent_Abi)
HOOK(0x0043c860, "Q112Boss_HandleEvent", Q112Boss_HandleEvent_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// Event helpers
// ---------------------------------------------------------------------------------------------------------------

// 0x0043ccb0 (stack: object; cdecl; returns al = 1). The "hide" event (0x0043d180, and as the callback of
// "hide_tag" through 0x00459390) (not executed in recorded runs): virtual +0xf4(0), +0xf8(0x4000, 0), unlinks the
// object from the lists (Game+0xa84)+0xe90, +0xe94, +0xe9c, +0xea4 (ObjectList_Unlink 0x00454750) and from +0xe8c when
// it is there (ObjectList_Contains 0x00454690).
uint32_t fn_0043ccb0(uint32_t obj)
{
    VC(obj, 0xf4, 0u);
    VC(obj, 0xf8, 0x4000u, 0u);
    orig::fn_00454750(D(D(GameP() + 0xa84) + 0xe90), obj);
    orig::fn_00454750(D(D(GameP() + 0xa84) + 0xe94), obj);
    orig::fn_00454750(D(D(GameP() + 0xa84) + 0xe9c), obj);
    uint32_t r = orig::fn_00454750(D(D(GameP() + 0xa84) + 0xea4), obj);
    r = orig::fn_00454690(D(D(GameP() + 0xa84) + 0xe8c), obj);
    if (static_cast<uint8_t>(r))
        r = orig::fn_00454750(D(D(GameP() + 0xa84) + 0xe8c), obj);
    return (r & ~0xffu) | 1;
}

static uint32_t fn_0043ccb0_Abi(uint32_t a0)
{
    return fn_0043ccb0(a0);
}
ABI_ENTRY_0043ccb0(fn_0043ccb0_Entry, fn_0043ccb0_Abi)
HOOK(0x0043ccb0, "fn_0043ccb0", fn_0043ccb0_Entry, nullptr);

// 0x0043cd70 (stack: object; cdecl; returns al = 1). The "unhide" event (and "unhide_tag" callback) (not executed in
// recorded runs): virtual +0xf4(1), +0xf8(0x170001, 0), ObjectList_Link 0x004546d0 into (Game+0xa84)+0xe90, +0xe94,
// +0xe9c, +0xea4 and +0xe8c.
uint32_t fn_0043cd70(uint32_t obj)
{
    VC(obj, 0xf4, 1u);
    VC(obj, 0xf8, 0x170001u, 0u);
    orig::fn_004546d0(D(D(GameP() + 0xa84) + 0xe90), obj);
    orig::fn_004546d0(D(D(GameP() + 0xa84) + 0xe94), obj);
    orig::fn_004546d0(D(D(GameP() + 0xa84) + 0xe9c), obj);
    orig::fn_004546d0(D(D(GameP() + 0xa84) + 0xea4), obj);
    uint32_t r = static_cast<uint32_t>(orig::fn_004546d0(D(D(GameP() + 0xa84) + 0xe8c), obj));
    return (r & ~0xffu) | 1;
}

static uint32_t fn_0043cd70_Abi(uint32_t a0)
{
    return fn_0043cd70(a0);
}
ABI_ENTRY_0043cd70(fn_0043cd70_Entry, fn_0043cd70_Abi)
HOOK(0x0043cd70, "fn_0043cd70", fn_0043cd70_Entry, nullptr);

// 0x0043ce10 (no inputs; symbols/abi.csv: no ret found). ecx = (Game+0xa84)+0xe84, then a tail jump to its virtual
// +0x34 with the caller's frame untouched. Every caller (0x0043dc10, 0x004467c6, 0x00446815, 0x00446a7c, 0x0049b09f)
// pushes nothing and compares the eax result with a count (Game+0xbf0 / +0xbf4, 15, 30), so +0x34 takes no arguments
// (guess: the list's element count). Reproduced as a call returning its eax; hand-written entry stub (no generated
// one). Executed in recorded runs.
uint32_t fn_0043ce10()
{
    return VC(D(D(GameP() + 0xa84) + 0xe84), 0x34);
}

static __declspec(naked) void fn_0043ce10_Entry()
{
    __asm {
        push ecx
        push edx
        push ebx
        push esi
        push edi
        push ebp
        call fn_0043ce10
        pop ebp
        pop edi
        pop esi
        pop ebx
        pop edx
        pop ecx
        ret
    }
}
HOOK(0x0043ce10, "fn_0043ce10", fn_0043ce10_Entry, nullptr);

// 0x0043ce30 (ecx = fake grunt; stack: a0, event name, a2, a3; ret 0x10). FakeGruntObject slot +0x34 (the event slot)
// (not executed in recorded runs). "release_platoon": flag 0x40000000 of +0x484, +0x490 = 0, physics on
// (Object_setPhysicsEnabled 0x004577b0(1)), zero velocity (Body_setLinearVelocity 0x00458510) and, at most every
// K(0x005bcbe8) seconds (Game+0xb38), the sound "amb_base_crowd_charge_lp" (audio +0xd0(name, 0, 0, 0, -1, 0)).
// Every other event goes to GenericAIObject_HandleEvent 0x0043d180.
uint32_t FakeGruntObject_HandleEvent(uint32_t self, uint32_t a0, uint32_t name, uint32_t a2, uint32_t a3)
{
    uint32_t r = Stricmp(name, 0x005b2edcu /* "release_platoon" */);
    if (r != 0)
        return GenericAIObject_HandleEvent(self, a0, name, a2, a3);
    D(self + 0x484) |= 0x40000000;
    D(self + 0x490) = r;
    orig::fn_004577b0(self, 1);
    float v[3] = {0.0f, 0.0f, 0.0f};
    orig::fn_00458510(u(v), self);
    uint32_t g = GameP();
    if (!(Now() - Fl(g + 0xb38) >= K(0x005bcbe8)))
        return 0;
    D(g + 0xb38) = D(0x00604a98);
    return VC(D(g + 0x2c), 0xd0, 0x005b2ec0u /* "amb_base_crowd_charge_lp" */, 0u, 0u, 0u, 0xffffffffu, 0u);
}

static uint32_t FakeGruntObject_HandleEvent_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return FakeGruntObject_HandleEvent(ecx, a0, a1, a2, a3);
}
ABI_ENTRY_0043ce30(FakeGruntObject_HandleEvent_Entry, FakeGruntObject_HandleEvent_Abi)
HOOK(0x0043ce30, "FakeGruntObject_HandleEvent", FakeGruntObject_HandleEvent_Entry, nullptr);

// 0x0043cef0 (ecx = goliath; stack: a0, event name, a2, a3; ret 0x10). GoliathObject slot +0x34 (the event slot)
// (not executed in recorded runs).
//  - "create_fireball": when +0xb68 is empty, creates a "goliath_fireball" (ObjectSystem_CreateObject 0x0045f020,
//    Game+0xa84, type 0x3ff, 0xaa) into +0xb68, 0x004324d0 (ebx = fireball; goliath, 1), Object_Attach 0x004539f0 at
//    "ref_fireball" (0, 1, 0), takes a reference and 0x00434570 (eax = goliath, ecx = fireball).
//  - "turn": +0xb58 = 3.
//  - "fire": the same creation when needed, then Object_Detach 0x00453b20, ObjectList_Link into (Game+0xa84)+0xf1c,
//    launch from the fireball's position (virtual +0x54) at the target's position (+0x588 virtual +0x54) or, without
//    a target, at own position + heading (0x00453150) * +0x5d8 (z = the fireball's z); speed from the fireball's
//    virtual +0x8c (into the a3 slot) * K(0x005bcd64), 0x00434560 (st0), +0x484 &= ~0x30000000, 0x00471a00 (ebx =
//    velocity out, esi = target point, edi = fireball position; st0 result, speed, 0), Body_setLinearVelocity, then
//    the reference is dropped and +0xb68 cleared.
//  - anything else: GenericAIObject_HandleEvent 0x0043d180.
uint32_t GoliathObject_HandleEvent(uint32_t self, uint32_t a0, uint32_t name, uint32_t a2, uint32_t a3)
{
    float fpos[3];  // [esp+0x1c]: the fireball's position
    float tpos[3];  // [esp+0x10]: the target point
    float vel[3];   // [esp+0x28]: heading, then the launch velocity
    uint32_t slot = self + 0xb68;
    auto create = [&]() {
        uint32_t g = GameP();
        uint32_t fb_ = orig::fn_0045f020(0, 0x005b2e9cu /* "goliath_fireball" */, D(g + 0xa84), 0, 0x3ff, 0, 0, 0, 0,
                                         0xaa, 0);
        D(slot) = fb_;
        orig::fn_004324d0(0, fb_, self, 1);
        orig::fn_004539f0(0x005b2e8cu /* "ref_fireball" */, self, D(slot), 0, 1, 0);
        ++I(D(slot) + 0x28);
        orig::fn_00434570(self, D(slot));
    };
    if (Stricmp(name, 0x005b2eb0u /* "create_fireball" */) == 0)
    {
        if (D(slot) == 0)
            create();
        return 0;
    }
    if (Stricmp(name, 0x005b2e84u /* "turn" */) == 0)
    {
        D(self + 0xb58) = 3;
        return 0;
    }
    if (Stricmp(name, 0x005b3154u /* "fire" */) != 0)
        return GenericAIObject_HandleEvent(self, a0, name, a2, a3);
    if (D(slot) == 0)
        create();
    orig::fn_00453b20(D(slot));
    orig::fn_004546d0(D(D(GameP() + 0xa84) + 0xf1c), D(slot));
    VC(D(slot), 0x54, u(fpos));
    uint32_t target = D(self + 0x588);
    if (target != 0)
    {
        VC(target, 0x54, u(tpos));
    }
    else
    {
        orig::fn_00453150(self, u(vel));
        tpos[2] = fpos[2];
        tpos[0] = vel[0] * Fl(self + 0x5d8) + fpos[0];
        tpos[1] = vel[1] * Fl(self + 0x5d8) + fpos[1];
    }
    float speedSlot;  // the a3 slot
    VC(D(slot), 0x8c, u(&speedSlot));
    float st = orig::fn_00434560(D(slot));
    float speed = speedSlot * K(0x005bcd64);
    D(self + 0x484) &= 0xcfffffffu;
    orig::fn_00471a00(u(vel), u(tpos), u(fpos), fb(st), fb(speed), 0);
    orig::fn_00458510(u(vel), D(slot));
    Unref(D(slot));
    D(slot) = 0;
    return 0;
}

static uint32_t GoliathObject_HandleEvent_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return GoliathObject_HandleEvent(ecx, a0, a1, a2, a3);
}
ABI_ENTRY_0043cef0(GoliathObject_HandleEvent_Entry, GoliathObject_HandleEvent_Abi)
HOOK(0x0043cef0, "GoliathObject_HandleEvent", GoliathObject_HandleEvent_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// GenericAIObject (vtable 0x005b2958)
// ---------------------------------------------------------------------------------------------------------------

// 0x0043d180 (ecx = AI object; stack: a0 = event record, a1 = event name, a2 = argument list, a3 = int* cursor;
// ret 0x10). GenericAIObject slot +0x34 (the script / notetrack event slot; Penguin, StigTurret, Q112, GruntLadder,
// Saucer ... share it; the Q112Boss, FakeGrunt and Goliath handlers fall back to it). Executed in recorded runs.
// Arguments are read with 0x004090a0 (eax = cursor, edx = list; out: the next string, list+8+i*4) and 0x004090b0
// (eax = out, edx = list, esi = cursor; sscanf "%d"). The original stores several of them into its own argument
// slots (a1..a3) and reuses the a2 slot's low byte as a flag; that is reproduced with the A[] array. The 0x50-byte
// frame (F) holds a vec3 at [esp+0x10] and a 0x44-byte matrix at [esp+0x1c]. Events (name: action):
//   fire (states 0x23, 0x85, 0x6a, 0x68) / fire_alt (0x23, 0x85, 0x68): in state 0x6a 0x0043ec60 must agree (fire:
//     else just the counters); needs counters +0x5bc and +0x5b8 > 0 (fire_alt: else both decremented); the flag
//     (flag 0x100000 of +0x488, Game+0xc80 > 0, Game+0xcbc, or (Game+0xa84)+0xf00 vt+0x34() non-zero) goes into the
//     a2 slot's low byte; virtual +0x194 (target or 0, alt, 1 or 0, a2 slot); both counters decremented.
//   throw: flag 0x100000 clear, Game+0xc80 <= 0, Game+0xcbc clear and (Game+0xa84)+0xf00 vt+0x34() == 0: 0x00428340
//     (ecx = *list, esi = self; "captain_grenade", 0).
//   hit: 0x0043eae0 (ebx = self; target +0x588, 5.0, +0x600).
//   anim_loop / anim_intro / anim_full: animation 0x00408e30 (0x81 / 0x80, edx = -1; name, 0, 0.25, 0, 0) /
//     0x0042c380; anim_loop first resyncs +0xa20/+0xa24 from 0x00408800(4) and the event record when flag 2 of +0xa18
//     is set; anim_intro copies its second name to +0x4ec and sets flag 0x800000 of +0x488.
//   play_sound_loop: stops the old loop +0xb40 (audio +0xd8), +0xb44 = second name, +0xb40 = audio +0xc4(first name,
//     self, 1, 0, -1, 0).
//   ready_roaming / attention_guard / ready_guard / ready_roof / trigger_charge / trigger_marker: virtual +0x1ac(1)
//     then +0x1a0 + +0x1b8(1) / 0x0042d0f0 (1,1) / (0,1) / (0,1) and the ladder hand-over (+0xae0 / +0xadc into
//     +0x880, Object_setPhysicsEnabled(1), flag 0x80 of +0x488) / 0x0042ce70 (Game+0x748) / 0x0042d1b0 (two
//     integers).
//   fire_mode_on / fire_mode_tag_on / fire_mode_ai_on / fire_mode_off: flags 0x800 / 0x1000 / 0x2000 of +0x488 (the
//     _on forms clear flag 4 of +0x484 and call 0x00427600; tag_on looks the tag up with 0x004592e0 and passes it to
//     virtual +0x23c(object, 1)).
//   ic_loop / ic_loop_snap: bytes +0xad8 (/ +0xad9) = 1.
//   hide / unhide (0x0043ccb0 / 0x0043cd70) and hide_tag / unhide_tag (0x00459390 with them as callbacks).
//   create_projectile: once (+0x854): object of the named type (0x0045f020, word +0x394), attached (Object_Attach,
//     "null" = none) and referenced into +0x854.
//   create_object_sl: while 0x0043ce10() < Game+0xbf4: object of the named type at +0xa28, virtual +0x23c(player,
//     1), Game+0x2710 += 1.
//   unlock_ic_object: 0x00429870(1). ic_pause: zero velocity, virtual +0x1b8(0x66). flying: virtual +0xf8(0x100, 0).
//   freefall_marker_range / freefall_attack / freefall_defend_radius / freefall_attack_village: freefall setup
//     (+0x558/+0x55c or +0x858/+0x85c from integers, +0x4ac = 0x12 / 0x13 / 0x14 / 1, +0x890 = 5000.0, virtual
//     +0x19c(0x61, 1, ...), the saved stance +0x4b0..+0x4bc, flag 0x8000 of +0x48c, virtual +0x1b8(0x82)).
//   q112 / q112_marker: a "q112" / "q112_boss" object at the named reference's matrix (0x00453c50), owned by self
//     (0x004324d0), then 0x0042daa0 (and Game+0x2710 += 1) / 0x0042da00 (two integers).
//   ragdoll: virtual +0x1ac(0), +0x17c(0, 1), and +0x19c(0, 1, ...) when flag 2 of +0xa18 was set.
//   base_defend: Game+0xc50, +0xc54 from integers. invincible / vulnerable: 0x00428220 (cl = 1 / 0).
//   orient / orient2d / orient_off: flag 0x2000000 of +0x488, flag 0x40 of +0x48c.
//   powderkeg / health / token_or_health / turret_upgrade: an object ("powderkeg_bomber", "shootable_health",
//     "shootable_health" or "shootable_token" by 0x004a0720(0x0044a660(Game+0xa64), 2), "shootable_turret_upgrade")
//     at the own matrix (virtual +0x5c), 0x004324d0 (ebx = self; object, 1).
//   no_notetracks: flag 0x1000 of +0x48c.
uint32_t GenericAIObject_HandleEvent(uint32_t self, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    uint32_t A[4] = {a0, a1, a2, a3};
    uint32_t F[0x50 / 4];  // [esp+0x10] .. [esp+0x5f]
    float* vec = reinterpret_cast<float*>(&F[0]);
    uint32_t mat = u(&F[3]);
    const uint32_t name = a1;
    auto Tok = [](uint32_t cursor, uint32_t list, uint32_t out) { orig::fn_004090a0(cursor, list, out); };
    auto Scan = [](uint32_t out, uint32_t list, uint32_t cursor) { orig::fn_004090b0(out, list, cursor); };
    // The shared "may fire" flag, stored into the low byte of the a2 slot.
    auto fireFlag = [&]() {
        uint8_t flag = 1;
        if (!(D(self + 0x488) & 0x100000))
        {
            uint32_t g = GameP();
            if (!(Fl(g + 0xc80) > K(0x0056c05c)) && By(g + 0xcbc) == 0)
            {
                uint32_t n = VC(D(D(g + 0xa84) + 0xf00), 0x34);
                A[2] &= ~0xffu;
                flag = n != 0 ? 1 : 0;
            }
        }
        A[2] = (A[2] & ~0xffu) | flag;
    };
    // The "_on" fire modes' common start: flag 4 of +0x484 off and 0x00427600 (esi = self).
    auto fireModeStart = [&]() {
        D(self + 0x484) &= ~4u;
        orig::fn_00427600(self);
    };
    // 0x0043d8df: +0x488 = v, then v & ~0x2000.
    auto store488 = [&](uint32_t v) {
        D(self + 0x488) = v;
        D(self + 0x488) = v & ~0x2000u;
    };
    // The freefall events' tail (after virtual +0x1ac(0)).
    auto freefall = [&](uint32_t mode) {
        D(self + 0x4ac) = mode;
        D(self + 0x890) = 0x459c4000;  // 5000.0
        VC(self, 0x19c, 0x61u, 1u, 0u, 0u, 0u, 0u);
        D(self + 0x4b0) = D(self + 0x498);
        D(self + 0x4b4) = D(self + 0x49c);
        D(self + 0x4b8) = D(self + 0x4a0);
        D(self + 0x4bc) = D(self + 0x4a4);
        D(self + 0x48c) |= 0x8000;
        VC(self, 0x1b8, 0x82u);
    };
    // 0x0043e2f5: an object of type `type` at the matrix, owned via 0x004324d0 (eax = 0, ebx = self; object, 1).
    auto spawnAtMatrix = [&](uint32_t type) {
        uint32_t obj =
            orig::fn_0045f020(0, type, D(GameP() + 0xa84), mat, 0x3ff, 0, 0, 0, 0, 0xaa, 0);
        orig::fn_004324d0(0, self, obj, 1);
    };

    uint32_t st = D(self + 0x498);
    if ((st == 0x23 || st == 0x85 || st == 0x6a || st == 0x68) && Stricmp(name, 0x005b3154u /* "fire" */) == 0)
    {
        bool go = true;
        if (D(self + 0x498) == 0x6a && !static_cast<uint8_t>(fn_0043ec60(self, D(self + 0x588))))
            go = false;
        if (go)
        {
            if (I(self + 0x5bc) <= 0 || I(self + 0x5b8) <= 0)
                return 0;
            fireFlag();
            if (D(self + 0x488) & 0x800)
                VC(self, 0x194, 0u, 0u, 1u, A[2]);
            else if (D(self + 0x498) == 0x68)
                VC(self, 0x194, 0u, 0u, 1u, A[2]);
            else
                VC(self, 0x194, D(self + 0x588), 0u, 0u, A[2]);
        }
        --I(self + 0x5bc);
        --I(self + 0x5b8);
        return 0;
    }
    st = D(self + 0x498);
    if ((st == 0x23 || st == 0x85 || st == 0x68) && Stricmp(name, 0x005b2e78u /* "fire_alt" */) == 0)
    {
        if (D(self + 0x498) == 0x6a && !static_cast<uint8_t>(fn_0043ec60(self, D(self + 0x588))))
            return 0;
        int32_t c = I(self + 0x5bc);
        if (c <= 0 || I(self + 0x5b8) <= 0)
        {
            I(self + 0x5bc) = c - 1;
            --I(self + 0x5b8);
            return 0;
        }
        fireFlag();
        if (D(self + 0x488) & 0x800)
            VC(self, 0x194, 0u, 1u, 1u, A[2]);
        else if (D(self + 0x498) == 0x68)
            VC(self, 0x194, 0u, 1u, 1u, A[2]);
        else
            VC(self, 0x194, D(self + 0x588), 1u, 0u, A[2]);
        --I(self + 0x5bc);
        --I(self + 0x5b8);
        return 0;
    }
    if (Stricmp(name, 0x005b2e70u /* "throw" */) == 0)
    {
        if (D(self + 0x488) & 0x100000)
            return 0;
        uint32_t g = GameP();
        if (!(Fl(g + 0xc80) <= K(0x0056c05c)))
            return 0;
        if (By(g + 0xcbc))
            return 0;
        uint32_t n = VC(D(D(g + 0xa84) + 0xf00), 0x34);
        if (n != 0)
            return 0;
        return orig::fn_00428340(D(A[2]), self, 0x005b2e60u /* "captain_grenade" */, n);
    }
    if (Stricmp(name, 0x005b2e5cu /* "hit" */) == 0)
        return fn_0043eae0(self, D(self + 0x588), 0x40a00000u, D(self + 0x600));
    if (Stricmp(name, 0x005b2e50u /* "anim_loop" */) == 0)
    {
        if (By(self + 0xa18) & 2)
        {
            uint32_t p = orig::fn_00408800(4, self);
            float f = Fl(p + 4);
            Fl(self + 0xa20) = f;
            uint32_t ev = A[0];
            int32_t num = 1 - I(ev + 0x14);
            int32_t den = I(ev + 0x20) * I(ev + 0x1c);
            Fl(self + 0xa24) = static_cast<float>(num) / static_cast<float>(den) + f;
            D(self + 0xa18) &= ~2u;
        }
        Tok(A[3], A[2], u(&A[2]));
        return orig::fn_00408e30(0x81, self, 0xffffffff, A[2], 0, 0x3e800000, 0, 0);
    }
    if (Stricmp(name, 0x005b2e44u /* "anim_full" */) == 0)
        return orig::fn_0042c380(self);
    if (Stricmp(name, 0x005b2e38u /* "anim_intro" */) == 0)
    {
        uint32_t list = A[2];
        uint32_t cursor = A[3];
        D(self + 0x488) |= 0x800000;
        Tok(cursor, list, u(&A[3]));
        Tok(cursor, list, u(&A[2]));
        const char* src = reinterpret_cast<const char*>(A[2]);
        char* dst = reinterpret_cast<char*>(self + 0x4ec);
        char ch;
        do
        {
            ch = *src++;
            *dst++ = ch;
        } while (ch != 0);
        return orig::fn_00408e30(0x80, self, 0xffffffff, A[3], 0, 0x3e800000, 0, 0);
    }
    if (Stricmp(name, 0x005b2e28u /* "play_sound_loop" */) == 0)
    {
        uint32_t loop = self + 0xb40;
        if (D(loop) != 0)
            VC(Audio(), 0xd8, loop, 0u);
        uint32_t list = A[2];
        uint32_t cursor = A[3];
        Tok(cursor, list, u(&A[2]));
        Tok(cursor, list, self + 0xb44);
        D(loop) = VC(Audio(), 0xc4, A[2], self, 1u, 0u, 0xffffffffu, 0u);
        return D(loop);
    }
    if (Stricmp(name, 0x005b6e14u /* "ready_roaming" */) == 0)
    {
        VC(self, 0x1ac, 1u);
        VC(self, 0x1a0, 2u, 3u, 0u, 1u, 1u, 0u, 0x3f000000u, 0x3f000000u);
        return VC(self, 0x1b8, 1u);
    }
    if (Stricmp(name, 0x005b6e04u /* "attention_guard" */) == 0)
    {
        VC(self, 0x1ac, 1u);
        return orig::fn_0042d0f0(self, 0, 1, 1);
    }
    if (Stricmp(name, 0x005b6df8u /* "ready_guard" */) == 0)
    {
        VC(self, 0x1ac, 1u);
        return orig::fn_0042d0f0(self, 0, 0, 1);
    }
    if (Stricmp(name, 0x005b2e1cu /* "ready_roof" */) == 0)
    {
        VC(self, 0x1ac, 1u);
        orig::fn_0042d0f0(self, 0, 0, 1);
        D(self + 0x484) |= 0x80000000;
        uint32_t ladder = self + 0xae0;
        if (D(ladder) == 0 && D(self + 0xadc) == 0)
            return 0;
        orig::fn_004577b0(self, 1);
        D(self + 0x488) |= 0x80;
        uint32_t obj = D(ladder);
        if (obj != 0)
        {
            D(self + 0x880) = obj;
            Unref(obj);
            D(ladder) = 0;
        }
        else
        {
            D(self + 0x880) = D(self + 0xadc);
        }
        ++I(D(self + 0x880) + 0x28);
        uint32_t slot = self + 0xadc;
        obj = D(slot);
        if (obj == 0)
            return 0;
        Unref(obj);
        D(slot) = 0;
        return 0;
    }
    if (Stricmp(name, 0x005b6de8u /* "trigger_charge" */) == 0)
    {
        VC(self, 0x1ac, 1u);
        return orig::fn_0042ce70(D(GameP() + 0x748), self);
    }
    if (Stricmp(name, 0x005b6d70u /* "trigger_marker" */) == 0)
    {
        uint32_t list = A[2];
        uint32_t cursor = A[3];
        Scan(u(&A[3]), list, cursor);
        Scan(u(&A[2]), list, cursor);
        VC(self, 0x1ac, 1u);
        return orig::fn_0042d1b0(A[2], A[3], self, 0, 0, 0, 0);
    }
    if (Stricmp(name, 0x005b2e0cu /* "fire_mode_on" */) == 0)
    {
        fireModeStart();
        uint32_t v = D(self + 0x488) | 0x800;
        D(self + 0x488) = v;
        store488(v & ~0x1000u);
        return 0;
    }
    if (Stricmp(name, 0x005b2df8u /* "fire_mode_tag_on" */) == 0)
    {
        Scan(u(&A[2]), A[2], A[3]);
        uint32_t obj = orig::fn_004592e0(D(D(GameP() + 0xa84) + 0xe70), A[2], self);
        if (obj != 0)
            VC(self, 0x23c, obj, 1u);
        fireModeStart();
        uint32_t v = D(self + 0x488) | 0x800;
        D(self + 0x488) = v;
        store488(v | 0x1000);
        return 0;
    }
    if (Stricmp(name, 0x005b2de8u /* "fire_mode_ai_on" */) == 0)
    {
        fireModeStart();
        uint32_t v = D(self + 0x488) | 0x800;
        D(self + 0x488) = v;
        D(self + 0x488) = v & ~0x1000u;
        D(self + 0x488) = (v & ~0x1000u) | 0x2000;
        return 0;
    }
    if (Stricmp(name, 0x005b2dd8u /* "fire_mode_off" */) == 0)
    {
        uint32_t v = D(self + 0x488) & ~0x800u;
        D(self + 0x488) = v;
        store488(v & ~0x1000u);
        return 0;
    }
    if (Stricmp(name, 0x005b2dd0u /* "ic_loop" */) == 0)
    {
        By(self + 0xad8) = 1;
        return 0;
    }
    if (Stricmp(name, 0x005b2dc0u /* "ic_loop_snap" */) == 0)
    {
        By(self + 0xad8) = 1;
        By(self + 0xad9) = 1;
        return 0;
    }
    if (Stricmp(name, 0x005b2db4u /* "hide_tag" */) == 0)
    {
        Scan(u(&A[2]), A[2], A[3]);
        return orig::fn_00459390(D(D(GameP() + 0xa84) + 0xe70), self, 0, 1, A[2], 0x0043ccb0);
    }
    if (Stricmp(name, 0x005b2dacu /* "hide" */) == 0)
        return fn_0043ccb0(self);
    if (Stricmp(name, 0x005b2da0u /* "unhide_tag" */) == 0)
    {
        Scan(u(&A[2]), A[2], A[3]);
        return orig::fn_00459390(D(D(GameP() + 0xa84) + 0xe70), self, 0, 1, A[2], 0x0043cd70);
    }
    if (Stricmp(name, 0x005b2d98u /* "unhide" */) == 0)
        return fn_0043cd70(self);
    if (Stricmp(name, 0x005b2d84u /* "create_projectile" */) == 0)
    {
        uint32_t list = A[2];
        uint32_t cursor = A[3];
        Tok(cursor, list, u(&A[2]));
        Tok(cursor, list, u(&A[3]));
        Tok(cursor, list, u(&A[1]));
        if (D(self + 0x854) != 0)
            return 0;
        uint32_t obj = orig::fn_0045f020(0, A[2], D(GameP() + 0xa84), 0, Field<uint16_t>(P(self), 0x394), 0, 0, 0, 0,
                                         0xaa, 0);
        uint32_t ref = A[3];
        if (Stricmp(ref, 0x005bba94u /* "null" */) == 0)
            ref = 0;
        uint32_t ref2 = A[1];
        if (Stricmp(ref2, 0x005bba94u) == 0)
            ref2 = 0;
        orig::fn_004539f0(ref, self, obj, ref2, 1, 0);
        D(self + 0x854) = obj;
        ++I(obj + 0x28);
        return 0;
    }
    if (Stricmp(name, 0x005b2d70u /* "create_object_sl" */) == 0)
    {
        Tok(A[3], A[2], u(&A[2]));
        uint32_t n = fn_0043ce10();
        uint32_t g = GameP();
        if (n >= D(g + 0xbf4))
            return 0;
        uint32_t obj = orig::fn_0045f020(0, A[2], D(g + 0xa84), self + 0xa28, Field<uint16_t>(P(self), 0x394), 0, 0,
                                         0, 0, 0xaa, 0);
        VC(obj, 0x23c, D(GameP() + 0x748), 1u);
        ++I(GameP() + 0x2710);
        return 0;
    }
    if (Stricmp(name, 0x005b2d5cu /* "unlock_ic_object" */) == 0)
        return orig::fn_00429870(self, 1);
    if (Stricmp(name, 0x005b2d50u /* "ic_pause" */) == 0)
    {
        F[0] = 0;
        F[1] = 0;
        F[2] = 0;
        orig::fn_00458510(u(vec), self);
        return VC(self, 0x1b8, 0x66u);
    }
    if (Stricmp(name, 0x005b2d48u /* "flying" */) == 0)
        return VC(self, 0xf8, 0x100u, 0u);
    if (Stricmp(name, 0x005b2d30u /* "freefall_marker_range" */) == 0)
    {
        Scan(self + 0x558, A[2], A[3]);
        Scan(self + 0x55c, A[2], A[3]);
        VC(self, 0x1ac, 0u);
        freefall(0x12);
        return 0;
    }
    if (Stricmp(name, 0x005b2d20u /* "freefall_attack" */) == 0)
    {
        VC(self, 0x1ac, 0u);
        freefall(0x13);
        return 0;
    }
    if (Stricmp(name, 0x005b2d08u /* "freefall_defend_radius" */) == 0)
    {
        Scan(self + 0x558, A[2], A[3]);
        Scan(self + 0x55c, A[2], A[3]);
        VC(self, 0x1ac, 0u);
        freefall(0x14);
        return 0;
    }
    if (Stricmp(name, 0x005b2cf0u /* "freefall_attack_village" */) == 0)
    {
        Scan(self + 0x858, A[2], A[3]);
        Scan(self + 0x85c, A[2], A[3]);
        VC(self, 0x1ac, 0u);
        D(self + 0x4ac) = 1;
        D(self + 0x890) = 0x459c4000;  // 5000.0
        D(self + 0x490) = 0;
        VC(self, 0x19c, 0x61u, 1u, 0u, 0u, 0u, 0u);
        D(self + 0x4b0) = 2;
        D(self + 0x4b4) = 3;
        D(self + 0x4b8) = 0;
        D(self + 0x4bc) = 1;
        D(self + 0x48c) |= 0x8000;
        return VC(self, 0x1b8, 0x82u);
    }
    if (Stricmp(name, 0x005b2ce8u /* "q112" */) == 0)
    {
        Tok(A[3], A[2], u(&A[2]));
        orig::fn_00453c50(self, A[2], mat, 1, 0, 0, 1, 0, 0);
        uint32_t obj = orig::fn_0045f020(0, 0x005b2ce8u /* "q112" */, D(GameP() + 0xa84), mat, 0x3ff, 0, 0, 0, 0, 0xaa,
                                         0);
        orig::fn_004324d0(0, obj, self, 1);
        orig::fn_0042daa0(obj);
        ++I(GameP() + 0x2710);
        return 0;
    }
    if (Stricmp(name, 0x005b2cdcu /* "q112_marker" */) == 0)
    {
        uint32_t list = A[2];
        uint32_t cursor = A[3];
        Tok(cursor, list, u(&A[2]));
        Scan(u(&A[1]), list, cursor);
        Scan(u(&A[3]), list, cursor);
        orig::fn_00453c50(self, A[2], mat, 1, 0, 0, 1, 0, 0);
        uint32_t obj = orig::fn_0045f020(0, 0x005b2cd0u /* "q112_boss" */, D(GameP() + 0xa84), mat, 0x3ff, 0, 0, 0, 0,
                                         0xaa, 0);
        orig::fn_004324d0(0, obj, self, 1);
        return orig::fn_0042da00(A[3], A[1], obj);
    }
    if (Stricmp(name, 0x005b2cc8u /* "ragdoll" */) == 0)
    {
        uint8_t was = (By(self + 0xa18) >> 1) & 1;
        VC(self, 0x1ac, 0u);
        VC(self, 0x17c, 0u, 1u);
        if (!was)
            return 0;
        return VC(self, 0x19c, 0u, 1u, 0u, 0u, 0u, 0u);
    }
    if (Stricmp(name, 0x005bb8acu /* "base_defend" */) == 0)
    {
        uint32_t list = A[2];
        uint32_t cursor = A[3];
        Scan(GameP() + 0xc50, list, cursor);
        Scan(GameP() + 0xc54, list, cursor);
        return 0;
    }
    if (Stricmp(name, 0x005b2cbcu /* "invincible" */) == 0)
        return orig::fn_00428220(self, 1);
    if (Stricmp(name, 0x005b2cb0u /* "vulnerable" */) == 0)
        return orig::fn_00428220(self, 0);
    if (Stricmp(name, 0x005b2ca8u /* "orient" */) == 0)
    {
        D(self + 0x488) |= 0x2000000;
        D(self + 0x48c) &= ~0x40u;
        return 0;
    }
    if (Stricmp(name, 0x005b2c9cu /* "orient2d" */) == 0)
    {
        D(self + 0x488) &= ~0x2000000u;
        D(self + 0x48c) |= 0x40;
        return 0;
    }
    if (Stricmp(name, 0x005b2c90u /* "orient_off" */) == 0)
    {
        D(self + 0x488) &= ~0x2000000u;
        D(self + 0x48c) &= ~0x40u;
        return 0;
    }
    if (Stricmp(name, 0x005b2c84u /* "powderkeg" */) == 0)
    {
        VC(self, 0x5c, mat);
        spawnAtMatrix(0x005b2c70u /* "powderkeg_bomber" */);
        return 0;
    }
    if (Stricmp(name, 0x005b2c68u /* "health" */) == 0)
    {
        VC(self, 0x5c, mat);
        spawnAtMatrix(0x005b2c54u /* "shootable_health" */);
        return 0;
    }
    if (Stricmp(name, 0x005b2c44u /* "token_or_health" */) == 0)
    {
        VC(self, 0x5c, mat);
        uint32_t g = GameP();
        uint32_t r = orig::fn_0044a660(D(g + 0xa64), 0);
        bool health = static_cast<uint8_t>(orig::fn_004a0720(r, 2, g)) != 0;
        spawnAtMatrix(health ? 0x005b2c54u /* "shootable_health" */ : 0x005b2c34u /* "shootable_token" */);
        return 0;
    }
    if (Stricmp(name, 0x005b2c24u /* "turret_upgrade" */) == 0)
    {
        VC(self, 0x5c, mat);
        spawnAtMatrix(0x005b2c08u /* "shootable_turret_upgrade" */);
        return 0;
    }
    if (Stricmp(name, 0x005b2bf8u /* "no_notetracks" */) == 0)
        D(self + 0x48c) |= 0x1000;
    return 0;
}

static uint32_t GenericAIObject_HandleEvent_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return GenericAIObject_HandleEvent(ecx, a0, a1, a2, a3);
}
ABI_ENTRY_0043d180(GenericAIObject_HandleEvent_Entry, GenericAIObject_HandleEvent_Abi)
HOOK(0x0043d180, "GenericAIObject_HandleEvent", GenericAIObject_HandleEvent_Entry, nullptr);

// 0x0043e520 (esi = AI object). Called by 0x0040cf40 (not executed in recorded runs). Knock-back towards the player's
// root object (Game+0x748 followed up Object_GetParent 0x004539d0): 0x00454c90(0, 1); Game+0xb24 = clock, Game+0xb28 =
// rand() * K(0x005bcefc) + K(0x005bcb80); own position P (virtual +0x54); the root's "ref_hit_00" matrix (0x00453c50)
// gives the point T (its translation). h = (T.z - P.z) * (rand() * K(0x005bcdf4) + K(0x005bcd48)): h <= 0 -> virtual
// +0xdc() only. Otherwise vz = sqrt(-(K(0x005bcef8) * h)), a speed d / (h / vz) with d the planar distance, 0x004580a0
// (ecx = root, ebx = &P, eax = &T; speed, 1) adjusts T, the planar direction T - P (normalised by 0x004e7780) times the
// speed times K(0x005bcb4c) gives vx, vy: Body_setLinearVelocity (vx, vy, vz); virtual +0xf8(0x20000, 0), flag 8 of
// +0x488, virtual +0x19c(0x49 + (rand() * 2 >> 15), 1, 0, 0, 0, 0) and +0x1b8(0x192).
uint32_t fn_0043e520(uint32_t self)
{
    float speed;     // [esp+0x04]
    float T[3];      // [esp+0x08]
    float Pp[3];     // [esp+0x14]
    float dir[2];    // [esp+0x20]
    float vel[3];    // [esp+0x2c]
    float m[17];     // [esp+0x38]
    orig::fn_00454c90(self, 0, 1);
    uint32_t root = D(GameP() + 0x748);
    if (orig::fn_004539d0(root) != 0)
    {
        do
        {
            root = orig::fn_004539d0(root);
        } while (orig::fn_004539d0(root) != 0);
    }
    uint32_t g = GameP();
    D(g + 0xb24) = D(0x00604a98);
    float r = static_cast<float>(Rand());
    Fl(GameP() + 0xb28) = r * K(0x005bcefc) + K(0x005bcb80);
    VC(self, 0x54, u(Pp));
    orig::fn_00453c50(root, 0x005b68fcu /* "ref_hit_00" */, u(m), 1, 0, 0, 1, 0, 0);
    T[0] = m[3];
    T[1] = m[7];
    T[2] = m[11];
    float r2 = static_cast<float>(Rand());
    float dz = T[2] - Pp[2];
    float h = dz * (r2 * K(0x005bcdf4) + K(0x005bcd48));
    if (h < K(0x0056c05c) || h == K(0x0056c05c))
        return VC(self, 0xdc);
    vel[2] = std::sqrt(-(K(0x005bcef8) * h));
    float dy = T[1] - Pp[1];
    float dx = T[0] - Pp[0];
    float d = std::sqrt(dx * dx + dy * dy);
    speed = d / (h / vel[2]);
    orig::fn_004580a0(u(T), root, u(Pp), fb(speed), 1);
    dir[0] = T[0] - Pp[0];
    dir[1] = T[1] - Pp[1];
    orig::fn_004e7780(u(dir), u(dir));
    vel[0] = dir[0] * speed * K(0x005bcb4c);
    vel[1] = dir[1] * speed * K(0x005bcb4c);
    orig::fn_00458510(u(vel), self);
    VC(self, 0xf8, 0x20000u, 0u);
    D(self + 0x488) |= 8;
    int32_t anim = ((Rand() << 1) >> 15) + 0x49;
    VC(self, 0x19c, static_cast<uint32_t>(anim), 1u, 0u, 0u, 0u, 0u);
    return VC(self, 0x1b8, 0x192u);
}

static uint32_t fn_0043e520_Abi(uint32_t esi)
{
    return fn_0043e520(esi);
}
ABI_ENTRY_0043e520(fn_0043e520_Entry, fn_0043e520_Abi)
HOOK(0x0043e520, "fn_0043e520", fn_0043e520_Entry, nullptr);

// GenericAIObject transform getters (not executed in recorded runs). With flag 2 of +0xa18 set (guess: the AI is
// driven by its own animation transform) they answer from the AI's matrices +0xa6c / +0xa28 (0x44 bytes each);
// otherwise they tail-jump to the base Object versions (same register and stack frame), reproduced as calls.

// 0x0043e900 (ecx = AI; stack: matrix out; ret 4). Slot +0x64: copies +0xa6c, else 0x00457d60.
uint32_t fn_0043e900(uint32_t self, uint32_t out)
{
    if (By(self + 0xa18) & 2)
    {
        std::memcpy(P(out), P(self + 0xa6c), 0x44);
        return 0;
    }
    return orig::fn_00457d60(self, out);
}

static uint32_t fn_0043e900_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0043e900(ecx, a0);
}
ABI_ENTRY_0043e900(fn_0043e900_Entry, fn_0043e900_Abi)
HOOK(0x0043e900, "fn_0043e900", fn_0043e900_Entry, nullptr);

// 0x0043e930 (ecx = AI; stack: matrix out; ret 4). Slot +0x5c: copies +0xa6c, else 0x00457d90.
uint32_t fn_0043e930(uint32_t self, uint32_t out)
{
    if (By(self + 0xa18) & 2)
    {
        std::memcpy(P(out), P(self + 0xa6c), 0x44);
        return 0;
    }
    return orig::fn_00457d90(self, out);
}

static uint32_t fn_0043e930_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0043e930(ecx, a0);
}
ABI_ENTRY_0043e930(fn_0043e930_Entry, fn_0043e930_Abi)
HOOK(0x0043e930, "fn_0043e930", fn_0043e930_Entry, nullptr);

// 0x0043e960 (ecx = AI; stack: vec3 out; ret 4; returns eax = out on the own path). Slot +0x54 (the position slot):
// the translation of +0xa6c (+0xa78, +0xa88, +0xa98), else Body_getPosition 0x00457930.
uint32_t fn_0043e960(uint32_t self, uint32_t out)
{
    if (By(self + 0xa18) & 2)
    {
        D(out) = D(self + 0xa78);
        D(out + 4) = D(self + 0xa88);
        D(out + 8) = D(self + 0xa98);
        return out;
    }
    return orig::fn_00457930(self, out);
}

static uint32_t fn_0043e960_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0043e960(ecx, a0);
}
ABI_ENTRY_0043e960(fn_0043e960_Entry, fn_0043e960_Abi)
HOOK(0x0043e960, "fn_0043e960", fn_0043e960_Entry, nullptr);

// 0x0043e990 (ecx = AI). Slot +0x68: &+0xa6c, else 0x00457ea0.
uint32_t fn_0043e990(uint32_t self)
{
    if (By(self + 0xa18) & 2)
        return self + 0xa6c;
    return orig::fn_00457ea0(self);
}

static uint32_t fn_0043e990_Abi(uint32_t ecx)
{
    return fn_0043e990(ecx);
}
ABI_ENTRY_0043e990(fn_0043e990_Entry, fn_0043e990_Abi)
HOOK(0x0043e990, "fn_0043e990", fn_0043e990_Entry, nullptr);

// 0x0043e9b0 (ecx = AI). Slot +0x60: &+0xa6c, else 0x00457eb0.
uint32_t fn_0043e9b0(uint32_t self)
{
    if (By(self + 0xa18) & 2)
        return self + 0xa6c;
    return orig::fn_00457eb0(self);
}

static uint32_t fn_0043e9b0_Abi(uint32_t ecx)
{
    return fn_0043e9b0(ecx);
}
ABI_ENTRY_0043e9b0(fn_0043e9b0_Entry, fn_0043e9b0_Abi)
HOOK(0x0043e9b0, "fn_0043e9b0", fn_0043e9b0_Entry, nullptr);

// 0x0043e9d0 (ecx = AI). Slot +0x74: &+0xa28, else 0x00457f00.
uint32_t fn_0043e9d0(uint32_t self)
{
    if (By(self + 0xa18) & 2)
        return self + 0xa28;
    return orig::fn_00457f00(self);
}

static uint32_t fn_0043e9d0_Abi(uint32_t ecx)
{
    return fn_0043e9d0(ecx);
}
ABI_ENTRY_0043e9d0(fn_0043e9d0_Entry, fn_0043e9d0_Abi)
HOOK(0x0043e9d0, "fn_0043e9d0", fn_0043e9d0_Entry, nullptr);

// 0x0043e9f0 (ecx = AI). Slot +0x7c: &+0xa28, else 0x00457ec0.
uint32_t fn_0043e9f0(uint32_t self)
{
    if (By(self + 0xa18) & 2)
        return self + 0xa28;
    return orig::fn_00457ec0(self);
}

static uint32_t fn_0043e9f0_Abi(uint32_t ecx)
{
    return fn_0043e9f0(ecx);
}
ABI_ENTRY_0043e9f0(fn_0043e9f0_Entry, fn_0043e9f0_Abi)
HOOK(0x0043e9f0, "fn_0043e9f0", fn_0043e9f0_Entry, nullptr);

// 0x0043ea10 (ecx = AI; stack: matrix out; ret 4). Slot +0x70: copies +0xa28, else 0x00457f80.
uint32_t fn_0043ea10(uint32_t self, uint32_t out)
{
    if (By(self + 0xa18) & 2)
    {
        std::memcpy(P(out), P(self + 0xa28), 0x44);
        return 0;
    }
    return orig::fn_00457f80(self, out);
}

static uint32_t fn_0043ea10_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0043ea10(ecx, a0);
}
ABI_ENTRY_0043ea10(fn_0043ea10_Entry, fn_0043ea10_Abi)
HOOK(0x0043ea10, "fn_0043ea10", fn_0043ea10_Entry, nullptr);

// 0x0043ea40 (ecx = AI; stack: matrix out; ret 4). Slot +0x78: copies +0xa28, else 0x00457f40.
uint32_t fn_0043ea40(uint32_t self, uint32_t out)
{
    if (By(self + 0xa18) & 2)
    {
        std::memcpy(P(out), P(self + 0xa28), 0x44);
        return 0;
    }
    return orig::fn_00457f40(self, out);
}

static uint32_t fn_0043ea40_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0043ea40(ecx, a0);
}
ABI_ENTRY_0043ea40(fn_0043ea40_Entry, fn_0043ea40_Abi)
HOOK(0x0043ea40, "fn_0043ea40", fn_0043ea40_Entry, nullptr);

// 0x0043ea70 (ecx = AI; stack: vec3 out; ret 4). Slot +0x6c: the translation of +0xa28 (+0xa34, +0xa44, +0xa54),
// else Body_getPosition 0x00457930.
uint32_t fn_0043ea70(uint32_t self, uint32_t out)
{
    if (By(self + 0xa18) & 2)
    {
        D(out) = D(self + 0xa34);
        D(out + 4) = D(self + 0xa44);
        D(out + 8) = D(self + 0xa54);
        return out;
    }
    return orig::fn_00457930(self, out);
}

static uint32_t fn_0043ea70_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0043ea70(ecx, a0);
}
ABI_ENTRY_0043ea70(fn_0043ea70_Entry, fn_0043ea70_Abi)
HOOK(0x0043ea70, "fn_0043ea70", fn_0043ea70_Entry, nullptr);

// 0x0043eaa0 (ecx = AI; returns al). GenericAIObject slot +0x30 (the in-frustum slot; FxObject's 0x00419260 has the
// same shape): always visible (al = 1; upper eax as left by the caller, returned as 0) in state +0x460 == 0x67,
// otherwise a tail jump to Object_InFrustum 0x00454360 (ecx, frame intact). Not executed in recorded runs.
uint32_t GenericAIObject_InFrustum(uint32_t self)
{
    if (D(self + 0x460) == 0x67)
        return 1;
    return orig::fn_00454360(self);
}

static uint32_t GenericAIObject_InFrustum_Abi(uint32_t ecx)
{
    return GenericAIObject_InFrustum(ecx);
}
ABI_ENTRY_0043eaa0(GenericAIObject_InFrustum_Entry, GenericAIObject_InFrustum_Abi)
HOOK(0x0043eaa0, "GenericAIObject_InFrustum", GenericAIObject_InFrustum_Entry, nullptr);

// 0x0043eac0 (ecx = AI; stack: byte flag; ret 4). GenericAIObject slot +0xf4 (executed in recorded runs):
// 0x00429a40 (al = flag, ecx = AI; the upper eax bytes are the caller's, passed as 0), then 0x004536b0 (ecx = AI;
// flag).
uint32_t fn_0043eac0(uint32_t self, uint32_t flag)
{
    orig::fn_00429a40(flag & 0xffu, self);
    return orig::fn_004536b0(self, flag);
}

static uint32_t fn_0043eac0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0043eac0(ecx, a0);
}
ABI_ENTRY_0043eac0(fn_0043eac0_Entry, fn_0043eac0_Abi)
HOOK(0x0043eac0, "fn_0043eac0", fn_0043eac0_Entry, nullptr);

// 0x0043eae0 (ebx = AI; stack: target, a1 (not read), radius; caller pops). Callers: the "hit" event (0x0043d180),
// 0x0049f930 (not executed in recorded runs). When both exist and are alive and the target is strictly within
// `radius` (planar), sends the target a 0x34-byte damage message through its virtual +0xd0: +0 = 0x40000, +4 = the
// AI's position, +0x10 = -1, +0x14 = 100.0 / +0x24 = 13000.0 when the AI's name (Object_GetName 0x00453270) contains
// "q112", else 50.0 / 6500.0, +0x18 = direction AI -> target normalised (0x004e7880), its z replaced by
// 0x0046e2c0(0.7, 0.9) (st0) before normalising, +0x28 = +0x2c = AI.
uint32_t fn_0043eae0(uint32_t ebx, uint32_t target, uint32_t /*a1*/, uint32_t radius)
{
    if (target == 0 || ebx == 0)
        return 0;
    if (static_cast<uint8_t>(orig::fn_00454780(ebx)))
        return 0;
    if (static_cast<uint8_t>(orig::fn_00454780(target)))
        return 0;
    float Bp[3];  // [esp+0x04] after push ebp: the target's position
    float Ap[3];  // [esp+0x10]: the AI's position
    uint32_t M[13];  // [esp+0x1c]: the message
    VC(ebx, 0x54, u(Ap));
    VC(target, 0x54, u(Bp));
    float dy = Ap[1] - Bp[1];
    float dx = Ap[0] - Bp[0];
    float r = bf(radius);
    if (!(r * r > dx * dx + dy * dy))
        return 0;
    std::memset(M, 0, sizeof(M));
    M[12] = 0;
    M[4] = 0xffffffff;
    M[0] = 0x40000;
    uint32_t nm = orig::fn_00453270(ebx);
    if (orig::fn_005383d0(nm, 0x005b2ce8u /* "q112" */) != 0)
    {
        M[9] = 0x464b2000;  // 13000.0
        M[5] = 0x42c80000;  // 100.0
    }
    else
    {
        M[9] = 0x45cb2000;  // 6500.0
        M[5] = 0x42480000;  // 50.0
    }
    VC(ebx, 0x54, u(&M[1]));
    float* dir = reinterpret_cast<float*>(&M[6]);
    dir[0] = Bp[0] - Ap[0];
    M[10] = ebx;
    dir[1] = Bp[1] - Ap[1];
    M[11] = ebx;
    dir[2] = Bp[2] - Ap[2];
    dir[2] = orig::fn_0046e2c0(0, 0x3f333333, 0x3f666666);
    Normalize3(u(dir), u(dir));
    return VC(target, 0xd0, u(M));
}

static uint32_t fn_0043eae0_Abi(uint32_t ebx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return fn_0043eae0(ebx, a0, a1, a2);
}
ABI_ENTRY_0043eae0(fn_0043eae0_Entry, fn_0043eae0_Abi)
HOOK(0x0043eae0, "fn_0043eae0", fn_0043eae0_Entry, nullptr);

// 0x0043ec40 (no inputs). GenericAIObject slot 1 (also GruntLadderObject +0x26c): returns the class descriptor
// 0x00581598 ("CLASS(GenericAIObject)" at +0x14). Not executed in recorded runs.
uint32_t GenericAIObject_GetClass()
{
    return 0x00581598;
}

static uint32_t GenericAIObject_GetClass_Abi()
{
    return GenericAIObject_GetClass();
}
ABI_ENTRY_0043ec40(GenericAIObject_GetClass_Entry, GenericAIObject_GetClass_Abi)
HOOK(0x0043ec40, "GenericAIObject_GetClass", GenericAIObject_GetClass_Entry, nullptr);

// 0x0043ec50 (ecx = object; returns al = Object_IsDead != 0, upper eax from it). GenericAIObject slot +0xe0 (also
// GruntLadder, FakeGrunt, ShrubPatrol ...). Not executed in recorded runs.
uint32_t fn_0043ec50(uint32_t self)
{
    uint32_t r = orig::fn_00454780(self);
    return (r & ~0xffu) | (static_cast<uint8_t>(r) ? 1u : 0u);
}

static uint32_t fn_0043ec50_Abi(uint32_t ecx)
{
    return fn_0043ec50(ecx);
}
ABI_ENTRY_0043ec50(fn_0043ec50_Entry, fn_0043ec50_Abi)
HOOK(0x0043ec50, "fn_0043ec50", fn_0043ec50_Entry, nullptr);

// 0x0043ec60 (esi = AI; stack: 1 argument (callers push the target), not read; ret 4; returns al). Callers:
// GenericAIObject_HandleEvent, GenericAI_Update (not executed in recorded runs). With a turret +0x898 whose virtual
// +0x168 returns 2: al = 1 only when virtual +0xf0(target +0x588, 180.0) is true and the target is at least
// sqrt(K(0x005bcc10)) (double) away in the plane; with a turret of another kind: al = 1; without a turret: al = 0.
uint32_t fn_0043ec60(uint32_t self, uint32_t /*a0*/)
{
    uint32_t turret = D(self + 0x898);
    if (turret == 0)
        return 0;
    uint32_t r = VC(turret, 0x168);
    if (r != 2)
        return (r & ~0xffu) | 1;
    r = VC(self, 0xf0, D(self + 0x588), 0x43340000u);
    if (!static_cast<uint8_t>(r))
        return r & ~0xffu;
    float A[3];  // [esp+0xc]
    float B[3];  // [esp+0x0]
    VC(self, 0x54, u(A));
    VC(D(self + 0x588), 0x54, u(B));
    float dy = B[1] - A[1];
    float dx = B[0] - A[0];
    float sum = dx * dx + dy * dy;
    if (static_cast<double>(sum) < GameGlobal<double>(0x005bcc10))
        return 0;
    return 1;
}

static uint32_t fn_0043ec60_Abi(uint32_t esi, uint32_t a0)
{
    return fn_0043ec60(esi, a0);
}
ABI_ENTRY_0043ec60(fn_0043ec60_Entry, fn_0043ec60_Abi)
HOOK(0x0043ec60, "fn_0043ec60", fn_0043ec60_Entry, nullptr);

// 0x0043ecf0 (ecx = AI; stack: other object, radius; ret 8; returns al). GenericAIObject slot +0x180 (executed in
// recorded runs; callers 0x00434290, 0x004853a0). False for a missing or dead object. True when its owner (0x00453290
// into the first argument slot) is Game+0xae8 (when set), flag 4 of +0x488 is set, 0x0044e600() is true or +0x958 is
// set; otherwise true when the object's position (virtual +0x54 into +0x60c) is within `radius` in the plane
// (!(r^2 < d^2)).
uint32_t fn_0043ecf0(uint32_t self, uint32_t other, uint32_t radius)
{
    if (other == 0)
        return 0;
    uint32_t r = orig::fn_00454780(other);
    if (static_cast<uint8_t>(r))
        return r & ~0xffu;
    uint32_t owner = 0;  // the first argument's slot
    orig::fn_00453290(other, u(&owner));
    uint32_t e = D(GameP() + 0xae8);
    if (e != 0 && owner == e)
        return 1;
    if (By(self + 0x488) & 4)
        return 1;
    r = orig::fn_0044e600();
    if (static_cast<uint8_t>(r))
        return (r & ~0xffu) | 1;
    if (D(self + 0x958) != 0)
        return 1;
    float A[3];  // [esp+8]
    VC(self, 0x54, u(A));
    uint32_t p = self + 0x60c;
    VC(other, 0x54, p);
    float dy = Fl(p + 4) - A[1];
    float dx = Fl(p) - A[0];
    float rr = bf(radius);
    if (rr * rr < dx * dx + dy * dy)
        return 0;
    return 1;
}

static uint32_t fn_0043ecf0_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return fn_0043ecf0(ecx, a0, a1);
}
ABI_ENTRY_0043ecf0(fn_0043ecf0_Entry, fn_0043ecf0_Abi)
HOOK(0x0043ecf0, "fn_0043ecf0", fn_0043ecf0_Entry, nullptr);

// 0x0043edb0 (ecx = AI). GenericAIObject slot +0x224 (not executed in recorded runs). Walks the list +0x518
// (0x004e7340 first / 0x004e73c0 next with an 8-byte iterator; afterwards the iterator's +4 object has its +0x18
// decremented): objects whose virtual +0x10c is true get +0x17c(0, 1); the others receive a 0x34-byte damage message
// (virtual +0xd0: +0 = 1, +0x10 = -1, +0x14 = their own health Object_GetHealth 0x004555b0, +0x2c = the AI). Each is
// then removed from the list (list virtual +0x28(object)) and its reference dropped. Guess: kills or releases
// everything the AI owns.
uint32_t fn_0043edb0(uint32_t self)
{
    if (D(self + 0x518) == 0)
        return 0;
    uint32_t it[2] = {0, 0};  // [esp+0xc]
    uint32_t M[13];           // [esp+0x14]
    uint32_t obj = orig::fn_004e7340(D(self + 0x518), u(it));
    while (obj != 0)
    {
        if (VB(obj, 0x10c))
        {
            VC(obj, 0x17c, 0u, 1u);
        }
        else
        {
            std::memset(M, 0, sizeof(M));
            uint32_t f = M[0] | 1;
            M[4] = 0xffffffff;
            M[11] = self;
            M[0] = f;
            reinterpret_cast<float*>(M)[5] = orig::fn_004555b0(obj);
            VC(obj, 0xd0, u(M));
        }
        VC(D(self + 0x518), 0x28, obj);
        Unref(obj);
        obj = orig::fn_004e73c0(u(it));
    }
    if (it[1] != 0)
        --I(it[1] + 0x18);
    return it[1];
}

static uint32_t fn_0043edb0_Abi(uint32_t ecx)
{
    return fn_0043edb0(ecx);
}
ABI_ENTRY_0043edb0(fn_0043edb0_Entry, fn_0043edb0_Abi)
HOOK(0x0043edb0, "fn_0043edb0", fn_0043edb0_Entry, nullptr);
