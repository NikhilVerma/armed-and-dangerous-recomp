// Recovered: batch ai_07 (AI subclasses' updates, MarkerObject / marker splines, TeammateMarker, MolmerObject, class
// descriptor getters, the AI navigation grid helpers at game+0xaa4). Static recovery 2026-10-05, compile-checked only
// (the game was not run); the oracle (tools/compare.py) is still to do. None of the virtual methods here is executed
// in recorded runs unless the comment says otherwise.
//
// Vtables and slots were found by scanning .rdata of game.unpacked.exe; the class is named by slot 1 (its descriptor's
// +0x14 string "CLASS(Name)"). Slot +0xbc is the per-frame update (GenericAIObject vtable 0x005b2958 slot +0xbc is
// GenericAI_Update 0x0043ee90).
//
// Callees the ABI table describes wrongly (handled here):
//   0x0043ce10 has no ret: `jmp [vt+0x34]` of the object list (Game+0xa84)+0xe84 (the list count) -> ListCount().
//   0x00455d90 (not in the function table): thiscall, 2 stack arguments, ret 8 -> called through a __fastcall pointer.
//   0x004e7880 returns st0 (the vec3 length), listed as eax -> Normalize3 (inline asm, pops st0).
//   0x0043e710 lists ebp as an input: it is only pushed (saved) by 0x00475a90 -> passed as 0.
//   0x0042ec20's ecx is only a `push ecx` stack-slot reservation -> passed as 0.
//   0x0046e2c0's ecx is only a `push ecx` stack-slot reservation -> passed as 0.
// Byte results (`mov al, ...`) keep the upper eax bytes the original leaves when they are known (cheap to reproduce).
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
// Memory at an absolute address (object fields: D(self + 0x460)).
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
inline uint16_t& Wd(uint32_t a)
{
    return *reinterpret_cast<uint16_t*>(a);
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
inline uint32_t GameP()
{
    return D(0x005fe490);
}
// The frame time 0x00604a90.
inline float Dt()
{
    return Fl(0x00604a90);
}
// 0x005391cd (_rand).
inline uint32_t Rand()
{
    return static_cast<uint32_t>(orig::fn_005391cd());
}
// _stricmp 0x005448ee (cdecl; the table's ecx input is not read).
inline int32_t Stricmp(uint32_t a, uint32_t b)
{
    return static_cast<int32_t>(orig::fn_005448ee(0, a, b));
}
// The reference drop the game inlines everywhere: --object+0x28; at zero, unless virtual +0xd8 says the object is
// kept, 0x00487da0 (eax = object) releases it.
inline void Unref(uint32_t object)
{
    if (--I(object + 0x28) == 0 && !static_cast<uint8_t>(VC(object, 0xd8)))
        orig::fn_00487da0(object);
}
// 0x0043ce10: ((Game+0xa84)+0xe84) vt+0x34(), the number of objects in that list (a tail jump in the original).
inline uint32_t ListCount()
{
    return VC(D(D(GameP() + 0xa84) + 0xe84), 0x34);
}
// 0x004e7880 (ecx = source vec3, edx = destination): normalises a 3D vector and returns its length in st0. The ABI
// table lists eax as the result; the callers pop st0 (fstp), so it is called here with its real st0 result.
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
// 0x00455d90 (ecx = object; stack: 2 arguments; ret 8; returns al). Not in the function table (Ghidra did not split
// it); the base-class method behind GoliathObject slot +0xf0.
inline uint32_t Call_00455d90(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return reinterpret_cast<uint32_t(__fastcall*)(uint32_t, uint32_t, uint32_t, uint32_t)>(0x00455d90)(ecx, 0, a0, a1);
}
// Copies the translation column (m[3], m[7], m[11]) of a 0x44-byte matrix into a particle's two position vec3s
// (+0x10 and +0x1c), in the original's store order.
inline void PlaceParticle(uint32_t p, const uint32_t* m)
{
    D(p + 0x10) = m[3];
    D(p + 0x14) = m[7];
    D(p + 0x18) = m[11];
    D(p + 0x1c) = m[3];
    D(p + 0x20) = m[7];
    D(p + 0x24) = m[11];
}
}  // namespace

// Prototypes of functions recovered further down in this file (called directly).
uint32_t Marker_GetSequence(uint32_t marker);
uint32_t Marker_GetFlags(uint32_t marker);
uint32_t Marker_GetActionName(uint32_t marker);
uint32_t Marker_IsFlag1(uint32_t marker);
uint32_t AIMarker_CurFlags(uint32_t ai, uint32_t spline);
uint32_t AIMarker_CurTrigger(uint32_t ai, uint32_t spline);
uint32_t AIMarker_CurAlarmTag(uint32_t ai, uint32_t spline);
uint32_t AIMarker_CurActionName(uint32_t ai, uint32_t spline);
uint32_t AIMarker_CurScatter(uint32_t outMax, uint32_t ai, uint32_t outMin, uint32_t spline);
uint32_t AIMarker_NextTag(uint32_t ai, uint32_t spline);
uint32_t AIMarker_Advance(uint32_t index, uint32_t ai, uint32_t spline, uint32_t teleport);
uint32_t Marker_FindSpline(uint32_t sequence, uint32_t tag);
uint32_t Marker_ProcessArrival(uint32_t spline, uint32_t ai, uint32_t wrap);
uint32_t Object_FindByAlarmTag(uint32_t list, uint32_t tag, uint32_t from);
uint32_t fn_00454cc0(uint32_t esi, uint32_t edi);
uint32_t fn_00454e60(uint32_t ebx, uint32_t edi, uint32_t a0);
uint32_t Object_SetGridBlocking(uint32_t self, uint32_t enable, uint32_t mode);
uint32_t fn_0046ece0(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t esi, uint32_t a0);
uint32_t fn_0046f2e0(uint32_t eax, uint32_t ecx, uint32_t edx);
uint32_t fn_0046f320(uint32_t esi, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3);
uint32_t fn_0046f440(uint32_t esi, uint32_t edi);
uint32_t fn_0046f5b0(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t esi, uint32_t edi, uint32_t a0, uint32_t a1);
uint32_t fn_00470aa0(uint32_t eax, uint32_t ecx, uint32_t a0);
uint32_t fn_00470c30(uint32_t eax);

// ---------------------------------------------------------------------------------------------------------------
// 0x00446440 (ecx = object). ShrubPatrol (vtable 0x005a7e98) slot +0xbc, the update: when +0x1cc is set, its +0x18 =
// Game+0x4c30 (a float, copied), then a tail jump to GenericAI_Update 0x0043ee90.
uint32_t ShrubPatrol_Update(uint32_t self)
{
    const uint32_t a = D(self + 0x1cc);
    if (a)
        D(a + 0x18) = D(GameP() + 0x4c30);
    return orig::fn_0043ee90(self);
}
static uint32_t ShrubPatrol_Update_Abi(uint32_t ecx)
{
    return ShrubPatrol_Update(ecx);
}
ABI_ENTRY_00446440(ShrubPatrol_Update_Entry, ShrubPatrol_Update_Abi)
HOOK(0x00446440, "ShrubPatrol_Update", ShrubPatrol_Update_Entry, nullptr);

// 0x00446460 (ecx = object). FakeGruntObject (vtable 0x005a4da8) slot +0xbc, the update. Strings "ref_captain_0%d",
// "ref_grunt_0%d", "ref_regular_0%d", "ref_torch_0%d", "Fake Grunt has no valid ref points\nDeactivating\n'%s'".
// First call (+0xbf8 set): counts the reference points of each kind (0x00453b90 probes "ref_<kind>_0<n>"; counts at
// +0xb58 captain, +0xb5c grunt, +0xb60 regular, +0xb64 torch, total +0xb68), deactivates (vt+0xdc) without any, else
// allocates one particle per point (0x0046d950 on the (Game+0xa90) pool 2, array +0xb6c) coloured by kind and placed
// at the point (0x00432ef0). Later: state 0x66 waits for the live object count (0x0043ce10) to drop below Game+0xbf0,
// then re-enables physics (0x004577b0) and enters state 100; with +0x484 bit 30 set it counts the timer +0x490 down
// and at zero turns the first remaining particle into a real "grunt" / "grunt_regular" / "captain"
// (ObjectSystem_CreateObject 0x0045f020) that climbs a ladder (0x00429900 / AI_ClimbLadder 0x0042d710) or follows
// the marker spline (AI_FollowMarkerSpline 0x0042d1b0); otherwise it runs GenericAI_Update and keeps the particles on
// their points. The original frame (sub esp, 0x84) is kept as F, F[o / 4] = [esp + o] (4 registers pushed).
uint32_t FakeGrunt_Update(uint32_t ebp)
{
    uint32_t F[0x94 / 4];  // only [0x10, 0x94) is used: 0x30 name buffer, 0x50 matrix (0x44 bytes)
    const uint32_t buf = u(&F[0x30 / 4]);
    const uint32_t mat = u(&F[0x50 / 4]);
    if (By(ebp + 0xbf8))
    {
        static const uint32_t kFormat[4] = {0x005b6424, 0x005b6444, 0x005b6434, 0x005b2764};  // captain grunt regular torch
        By(ebp + 0xbf8) = 0;
        for (uint32_t k = 0; k < 4; ++k)
        {
            const uint32_t slot = ebp + 0xb58 + 4 * k;
            orig::fn_0053814f(buf, kFormat[k], D(slot));
            while (static_cast<uint8_t>(orig::fn_00453b90(0, ebp, buf)))
            {
                const uint32_t n = D(slot) + 1;
                D(slot) = n;
                orig::fn_0053814f(buf, kFormat[k], n);
            }
        }
        const uint32_t total = D(ebp + 0xb58) + (D(ebp + 0xb64) + D(ebp + 0xb60) + D(ebp + 0xb5c));
        D(ebp + 0xb68) = total;
        if (total == 0)
        {
            const uint32_t name = D(orig::fn_004555d0(ebp) + 0x10) + 4;
            orig::fn_00407ff0(D(GameP() + 0x34), 0x005b272c, name, 0);
            return VC(ebp, 0xdc);
        }
        F[0x20 / 4] = orig::fn_0041f4f0(D(GameP() + 0xa90), 2);
        uint32_t esi = 0;
        F[0x10 / 4] = 0;
        F[0x18 / 4] = ebp + 0xb6c;
        for (;;)
        {
            const uint32_t ebx = orig::fn_0046d950(F[0x20 / 4]);
            D(F[0x18 / 4]) = ebx;
            if (!ebx)
            {
                D(ebp + 0xb68) = esi;
                return 0;
            }
            const uint32_t grunts = D(ebp + 0xb5c);
            const uint32_t regulars = grunts + D(ebp + 0xb60);
            if (esi >= grunts && esi >= regulars && esi < D(ebp + 0xb58) + regulars)
            {
                D(ebx + 0x5c) = 0x3e800000;
                D(ebx + 0x60) = 0x3ec00000;
                D(ebx + 0x64) = 0x3f028f5c;
                D(ebx + 0x68) = 0x3f1fbe77;
            }
            else
            {
                D(ebx + 0x5c) = 0;
                D(ebx + 0x60) = 0x3e800000;
                D(ebx + 0x64) = 0x3f200000;
                D(ebx + 0x68) = 0x3f600000;
            }
            orig::fn_00432ef0(esi, u(&F[0x1c / 4]), ebp, mat);
            PlaceParticle(ebx, &F[0x50 / 4]);
            D(ebx + 8) = 0x4bed4e00;
            D(ebx + 0x40) = 0x41000000;
            D(ebx + 0x4c) = 0x3f800000;
            D(ebx + 0x48) = 0x0fffffff;
            const uint32_t next = F[0x10 / 4] + 1;
            F[0x10 / 4] = next;
            F[0x18 / 4] += 4;
            if (next >= D(ebp + 0xb68))
                return next;
            esi = F[0x10 / 4];
        }
    }
    if (D(ebp + 0x460) == 0x66)
    {
        const uint32_t limit = GameP() + 0xbf0;
        const uint32_t n = ListCount();
        if (n >= D(limit))
            return n;
        D(limit) = D(GameP() + 0xbf4);
        orig::fn_004577b0(ebp, 0);
        return VC(ebp, 0x1b8, 0x64u);
    }
    if (D(ebp + 0x484) & 0x40000000)
    {
        const uint32_t n = ListCount();
        if (n >= D(GameP() + 0xbf0))
            return n;
        const float t = Fl(ebp + 0x490) - Dt();
        Fl(ebp + 0x490) = t;
        if (!(t <= K(0x0056c05c)))
            return 0;
        F[0x10 / 4] = 0;  // particles found
        F[0x18 / 4] = 0;  // index
        if (D(ebp + 0xb68) != 0)
        {
            uint32_t ebx = F[0x1c / 4];  // the original keeps the last created object here (stale on entry)
            uint32_t edi = ebp + 0xb6c;
            F[0x20 / 4] = edi;
            for (;;)
            {
                const uint32_t particle = D(edi);
                if (particle)
                {
                    const uint32_t found = F[0x10 / 4] + 1;
                    F[0x10 / 4] = found;
                    if (found != 1)
                        break;
                    orig::fn_0046d9a0(particle, orig::fn_0041f4f0(D(GameP() + 0xa90), 2));
                    D(edi) = 0;
                    bool ladder = true;  // [esp+0x17]
                    orig::fn_00432ef0(F[0x18 / 4], u(&F[0x1c / 4]), ebp, mat);
                    const uint32_t kind = F[0x1c / 4];
                    if (kind <= 3)
                    {
                        uint32_t name = 0x005bc01c;  // "grunt" (kinds 0 and 3)
                        if (kind == 1)
                            name = 0x005bbfe8;  // "grunt_regular"
                        else if (kind == 2)
                            name = 0x005bbff8;  // "captain"
                        ebx = orig::fn_0045f020(0, name, D(GameP() + 0xa84), mat, Wd(ebp + 0x394), 0, 0, 0, 0, 0xaa, 0);
                        if (kind == 2)
                            ladder = false;
                        ++I(GameP() + 0x2710);
                    }
                    orig::fn_004546d0(D(D(GameP() + 0xa84) + 0xe84), ebx);
                    uint32_t climb = 0;
                    if (ladder && (climb = orig::fn_00429900(D(ebp + 0xbec), ebx, D(ebp + 0xbf0))) != 0)
                    {
                        orig::fn_0042d710(climb, ebx);
                    }
                    else
                    {
                        orig::fn_0042d1b0(D(ebp + 0x55c), D(ebp + 0x558), ebx, 1, 0, 0, 0);
                        VC(ebx, 0xf8, 0x120000u, 0u);
                        orig::fn_00455860(ebx, 0x447a0000, 0x461c4000);
                    }
                }
                const uint32_t index = F[0x18 / 4] + 1;
                edi = F[0x20 / 4] + 4;
                F[0x18 / 4] = index;
                F[0x20 / 4] = edi;
                if (index >= D(ebp + 0xb68))
                    break;
            }
        }
        if (F[0x10 / 4] > 1)
        {
            const uint32_t r = Rand();
            Fl(ebp + 0x490) = static_cast<float>(static_cast<int32_t>(r)) * K(0x005bcfcc) + K(0x005bcbfc);
            return r;
        }
        return VC(ebp, 0xdc);
    }
    if (ListCount() >= D(GameP() + 0xbf0))
    {
        F[0x24 / 4] = 0;
        F[0x28 / 4] = 0;
        F[0x2c / 4] = 0;
        orig::fn_00458510(u(&F[0x24 / 4]), ebp);
        D(GameP() + 0xbf0) = D(GameP() + 0xbf8);
        orig::fn_004577b0(ebp, 1);
        return VC(ebp, 0x1b8, 0x66u);
    }
    orig::fn_0043ee90(ebp);
    if (D(ebp + 0x484) & 0x40000000)
    {
        F[0x24 / 4] = 0;
        F[0x28 / 4] = 0;
        F[0x2c / 4] = 0;
        orig::fn_00458510(u(&F[0x24 / 4]), ebp);
    }
    uint32_t i = 0;
    for (uint32_t p = ebp + 0xb6c; i < D(ebp + 0xb68); ++i, p += 4)
    {
        if (D(p))
        {
            orig::fn_00432ef0(i, u(&F[0x1c / 4]), ebp, mat);
            PlaceParticle(D(p), &F[0x50 / 4]);
        }
    }
    return D(ebp + 0xb68);
}
static uint32_t FakeGrunt_Update_Abi(uint32_t ecx)
{
    return FakeGrunt_Update(ecx);
}
ABI_ENTRY_00446460(FakeGrunt_Update_Entry, FakeGrunt_Update_Abi)
HOOK(0x00446460, "FakeGrunt_Update", FakeGrunt_Update_Entry, nullptr);

// 0x00446ba0 (ecx = squad). SquadObject (vtable 0x005ab3e8) slot +0xbc, the update. Skipped while 0x0044a600
// (Game+0xa64) is true. Drops the leader +0xb7c once it is dead (Object_IsDead 0x00454780), then resets the member
// counts +0xb70 / +0xb74 to the list +0xb78's count, clears the timers +0x490 / +0x494 and calls vt+0x23c(player
// Game+0x748, 1). With no members and no leader it deactivates (vt+0xdc). In state 0x98, once the timer +0x490 has run
// out, it finds the first live member (dead ones are unlinked) that has a target (GenericAI_GetTarget 0x0042c6f0) or
// is in state 10, rearms the timer between the floats +0xb60 and +0xb64 and passes it (0x00433ba0) to every object of
// list (Game+0xa84)+0xf08 with the same 0x00433b90 group and a 0x00433b80 rank not below the squad's, then hands the
// target to 0x00433bc0. Always ends with GenericAI_Update.
uint32_t SquadObject_Update(uint32_t ebx)
{
    const uint32_t skip = orig::fn_0044a600(D(GameP() + 0xa64));
    if (static_cast<uint8_t>(skip))
        return skip;
    const uint32_t edi = ebx + 0xb7c;
    if (D(edi) && static_cast<uint8_t>(orig::fn_00454780(D(edi))))
    {
        Unref(D(edi));
        D(edi) = 0;
        const uint32_t n = VC(D(ebx + 0xb78), 0x34);
        D(ebx + 0xb70) = n;
        D(ebx + 0xb74) = n;
        D(ebx + 0x490) = 0;
        D(ebx + 0x494) = 0;
        VC(ebx, 0x23c, D(GameP() + 0x748), 1u);
    }
    if (VC(D(ebx + 0xb78), 0x34) == 0 && D(edi) == 0)
        return VC(ebx, 0xdc);
    if (D(ebx + 0x460) == 0x98 && Fl(ebx + 0x490) - Dt() <= K(0x0056c05c))
    {
        uint32_t it[2] = {0, 0};  // [esp+0x20]
        uint32_t target = 0;      // ebp
        uint32_t o = orig::fn_004e7340(D(ebx + 0xb78), u(it));
        for (; o; o = orig::fn_004e73c0(u(it)))
        {
            if (static_cast<uint8_t>(orig::fn_00454780(o)))
            {
                orig::fn_00454750(D(ebx + 0xb78), o);
                continue;
            }
            target = orig::fn_0042c6f0(o);
            if (target || D(o + 0x460) == 10)
                break;
        }
        if (o)
        {
            const uint32_t node = it[1];
            VC(it[0], 0xc, node);
            --I(node + 0x18);
            it[1] = 0;
            const float hi = Fl(ebx + 0xb64);
            const float lo = Fl(ebx + 0xb60);
            const int32_t r = static_cast<int32_t>(Rand());
            Fl(ebx + 0x490) = static_cast<float>(r) * (hi - lo) * K(0x005bcbc0) + lo;
            for (uint32_t c = orig::fn_004e7340(D(D(GameP() + 0xa84) + 0xf08), u(it)); c;
                 c = orig::fn_004e73c0(u(it)))
            {
                const uint32_t group = orig::fn_00433b90(c);
                if (group != orig::fn_00433b90(ebx))
                    continue;
                const int32_t rank = static_cast<int32_t>(orig::fn_00433b80(c));
                if (rank >= static_cast<int32_t>(orig::fn_00433b80(ebx)))
                    orig::fn_00433ba0(c, D(ebx + 0x490));
            }
            orig::fn_00433bc0(ebx, target);
        }
        if (it[1])
            --I(it[1] + 0x18);
    }
    return orig::fn_0043ee90(ebx);
}
static uint32_t SquadObject_Update_Abi(uint32_t ecx)
{
    return SquadObject_Update(ecx);
}
ABI_ENTRY_00446ba0(SquadObject_Update_Entry, SquadObject_Update_Abi)
HOOK(0x00446ba0, "SquadObject_Update", SquadObject_Update_Entry, nullptr);

// 0x00446de0 (ecx = polewart). Polewart (vtable 0x005a7328) slot +0x224. For every object of the list +0x518: sends
// it a 0x34-byte message (vt+0xd0) {+0 |= 3, +0x10 = 0x00458720(object, 0), +0x14 = its health (Object_GetHealth
// 0x004555b0), +0x18 = a direction from 0x00453150 (the polewart's heading, guess) with z = rand * 6.1e-6 + 0.7,
// normalised (scale 1 / length unless the length is 0), +0x24 = rand * 0.661 + 21666.7, +0x2c = the polewart}, removes
// it from the list (vt+0x28) and drops the reference. Guess: throws off whatever it carries.
uint32_t fn_00446de0(uint32_t ebp)
{
    uint32_t it[2] = {0, 0};  // [esp+0x14]
    uint32_t msg[13];         // [esp+0x1c], 0x34 bytes
    for (uint32_t ebx = orig::fn_004e7340(D(ebp + 0x518), u(it)); ebx; ebx = orig::fn_004e73c0(u(it)))
    {
        std::memset(msg, 0, sizeof(msg));
        msg[0x10 / 4] = orig::fn_00458720(ebx, 0);
        orig::fn_00453150(ebp, u(&msg[0x18 / 4]));
        float* v = reinterpret_cast<float*>(&msg[0x18 / 4]);
        v[2] = static_cast<float>(static_cast<int32_t>(Rand())) * K(0x005bcdf4) + K(0x005bcd48);
        reinterpret_cast<float*>(msg)[0x24 / 4] =
            static_cast<float>(static_cast<int32_t>(Rand())) * K(0x005bcdf0) + K(0x005bcdec);
        const float len = std::sqrt(v[1] * v[1] + v[0] * v[0] + v[2] * v[2]);
        if (len != K(0x0056c05c))
        {
            const float s = K(0x005bcb2c) / len;
            v[0] = v[0] * s;
            v[1] = v[1] * s;
            v[2] = s * v[2];
        }
        msg[0] |= 3;
        msg[0x2c / 4] = ebp;
        reinterpret_cast<float*>(msg)[0x14 / 4] = orig::fn_004555b0(ebx);
        VC(ebx, 0xd0, u(msg));
        VC(D(ebp + 0x518), 0x28, ebx);
        Unref(ebx);
    }
    if (it[1])
        --I(it[1] + 0x18);
    return it[1];
}
static uint32_t fn_00446de0_Abi(uint32_t ecx)
{
    return fn_00446de0(ecx);
}
ABI_ENTRY_00446de0(fn_00446de0_Entry, fn_00446de0_Abi)
HOOK(0x00446de0, "fn_00446de0", fn_00446de0_Entry, nullptr);

// 0x00446f30 (ecx = polewart; the table's edx input is not read). Polewart (vtable 0x005a7328) slot +0xbc, the
// update. Outside state 0x8c (0x00426600) it is GenericAI_Update (a tail jump). In state 0x8c: with +0xb58 clear, when
// the timer +0x490 runs out it plays "polewart_graze" / "polewart_scan" / "polewart_chew" (one third each by rand,
// 0x00408e30 track 0x80, blend 0.25) and rearms the timer to rand * 1.5e-4 + 5; with +0xb58 set it plays
// "polewart_stand" (track 0x81) when channel 4's +0x1c bit 1 is set (0x00408800, guess: animation finished).
uint32_t Polewart_Update(uint32_t esi)
{
    if (!static_cast<uint8_t>(orig::fn_00426600(esi)))
        return orig::fn_0043ee90(esi);
    if (D(esi + 0xb58) == 0)
    {
        const float t = Fl(esi + 0x490) - Dt();
        Fl(esi + 0x490) = t;
        if (!(t <= K(0x0056c05c)))
            return 0;
        const uint32_t r = static_cast<uint32_t>(static_cast<int32_t>(Rand() * 0x12c) >> 15);
        const uint32_t anim = r < 0x64 ? 0x005b271c : (r < 0xc8 ? 0x005b270c : 0x005b26fc);
        orig::fn_00408e30(0x80, esi, 0xffffffff, anim, 0, 0x3e800000, 0, 0);
        const uint32_t r2 = Rand();
        Fl(esi + 0x490) = static_cast<float>(static_cast<int32_t>(r2)) * K(0x005bcdd8) + K(0x005bcbdc);
        return r2;
    }
    const uint32_t ch = orig::fn_00408800(4, esi);
    if (!((D(ch + 0x1c) >> 1) & 1))
        return ch;
    return orig::fn_00408e30(0x81, esi, 0xffffffff, 0x005b47c4, 0, 0x3e800000, 0, 0);
}
static uint32_t Polewart_Update_Abi(uint32_t ecx, uint32_t /*edx*/)
{
    return Polewart_Update(ecx);
}
ABI_ENTRY_00446f30(Polewart_Update_Entry, Polewart_Update_Abi)
HOOK(0x00446f30, "Polewart_Update", Polewart_Update_Entry, nullptr);

// 0x00447020 (ecx = goliath; stack: 2 arguments; ret 8). GoliathObject (vtable 0x005af298) slot +0xf0: in state 0x79
// returns 1 (al; the original leaves the caller's upper eax bytes), otherwise a tail jump to the base method 0x00455d90
// with the same arguments. (recover.py lists the size as 240: the function is 14 bytes, the rest is 0x00447040.)
uint32_t fn_00447020(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    if (D(ecx + 0x460) == 0x79)
        return 1;
    return Call_00455d90(ecx, a0, a1);
}
static uint32_t fn_00447020_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return fn_00447020(ecx, a0, a1);
}
ABI_ENTRY_00447020(fn_00447020_Entry, fn_00447020_Abi)
HOOK(0x00447020, "fn_00447020", fn_00447020_Entry, nullptr);

namespace
{
// GoliathObject: faces the direction perpendicular to +0xb5c in the ground plane: +0x51c = (+0xb60, -+0xb5c, +0xb64),
// then 0x00454e60 (ebx = &+0x51c, edi = goliath; turn rate 4.0).
inline void Goliath_FaceAcross(uint32_t esi)
{
    const float x = Fl(esi + 0xb5c);
    const uint32_t y = D(esi + 0xb60);
    const uint32_t z = D(esi + 0xb64);
    Fl(esi + 0x520) = -x;
    D(esi + 0x51c) = y;
    D(esi + 0x524) = z;
    fn_00454e60(esi + 0x51c, esi, 0x40800000);
}
// The animation-finished test the goliath and polewart use: channel 4 (0x00408800) +0x1c bit 1 (guess).
inline bool AnimDone(uint32_t object)
{
    return ((D(orig::fn_00408800(4, object) + 0x1c) >> 1) & 1) != 0;
}
}  // namespace

// 0x00447040 (ecx = goliath). GoliathObject (vtable 0x005af298) slot +0xbc, the update (strings "goliath_walk",
// "goliath_fireball_pickup", "goliath_stand", "goliath_fireball_throw"). During a cutscene (0x0044e600) it only leaves
// state 0x66 ("goliath_walk", physics off, state 100) once the animation is done. Outside state 0x79 it scales the
// speed +0x578 by dt * 0.5 and runs GenericAI_Update. In state 0x79 (+0x424 bit 5 set by 0x00454c90, 0x0042ec20 with
// +0x5dc) it runs the fireball cycle on +0xb58: 1 pick up -> 2 throw (turning to the target +0x588 or along +0xb5c,
// 0x00454e60 / 0x00454cc0) -> 3 stand (timer +0x494 = 0x0046e2c0(1, 3)) -> 4/5 wait for the timer, then 1 again while a
// target exists.
uint32_t GoliathObject_Update(uint32_t esi)
{
    uint32_t F[0x24 / 4];  // [esp+0xc] target position, [esp+0x18] own position
    const uint32_t state = D(esi + 0x460);
    if (static_cast<uint8_t>(orig::fn_0044e600()))
    {
        if (state != 0x66)
            return orig::fn_0043ee90(esi);
        if (!AnimDone(esi))
            return 0;
        orig::fn_00408e30(0x81, esi, 0xffffffff, 0x005b47b4, 0, 0x3e800000, 0, 0);
        orig::fn_004577b0(esi, 0);
        return VC(esi, 0x1b8, 0x64u);
    }
    if (state != 0x79)
    {
        Fl(esi + 0x578) = Dt() * K(0x005bcb4c) * Fl(esi + 0x578);
        return orig::fn_0043ee90(esi);
    }
    orig::fn_00454c90(esi, 1, 1);  // cl = 1 (ecx is 0 when 0x0044e600 returns false)
    orig::fn_0042ec20(esi, 0, D(esi + 0x5dc), 0);
    switch (D(esi + 0xb58))
    {
    case 4:
    case 5:
    {
        Goliath_FaceAcross(esi);
        const float t = Fl(esi + 0x494) - Dt();
        Fl(esi + 0x494) = t;
        if (!(t <= K(0x0056c05c)))
            return 0;
        if (!AnimDone(esi) || !D(esi + 0x588))
            return 0;
        orig::fn_00408e30(0x80, esi, 0xffffffff, 0x005b26e4, 0, 0x3e800000, 0, 0);
        D(esi + 0xb58) = 1;
        return 0;
    }
    case 3:
        Goliath_FaceAcross(esi);
        if (!AnimDone(esi))
            return 0;
        orig::fn_00408e30(0x81, esi, 0xffffffff, 0x005b63b0, 0, 0x3e800000, 0, 0);
        Fl(esi + 0x494) = orig::fn_0046e2c0(0, 0x3f800000, 0x40400000);
        D(esi + 0xb58) = 4;
        return 0;
    case 1:
        Goliath_FaceAcross(esi);
        if (!AnimDone(esi))
            return 0;
        orig::fn_00408e30(0x80, esi, 0xffffffff, 0x005b26cc, 0, 0x3e800000, 0, 0);
        D(esi + 0xb58) = 2;
        return 0;
    case 2:
    {
        const uint32_t dir = esi + 0x51c;
        if (D(esi + 0x588))
        {
            VC(esi, 0x54, u(&F[0x18 / 4]));
            VC(D(esi + 0x588), 0x54, u(&F[0x0c / 4]));
            const float* a = reinterpret_cast<const float*>(&F[0x0c / 4]);
            const float* b = reinterpret_cast<const float*>(&F[0x18 / 4]);
            Fl(dir) = a[0] - b[0];
            Fl(dir + 4) = a[1] - b[1];
            Fl(dir + 8) = a[2] - b[2];
            Normalize3(dir, dir);
        }
        else
        {
            D(dir) = D(esi + 0xb5c);
            D(dir + 4) = D(esi + 0xb60);
            D(dir + 8) = D(esi + 0xb64);
        }
        if (static_cast<uint8_t>(fn_00454e60(dir, esi, 0x40800000)))
            fn_00454cc0(esi, esi + 0x398);
        if (D(esi + 0x588))
            return D(esi + 0x588);
        return orig::fn_0042ec20(esi, 0, D(esi + 0x5dc), 0);
    }
    default:
        return 0;
    }
}
static uint32_t GoliathObject_Update_Abi(uint32_t ecx)
{
    return GoliathObject_Update(ecx);
}
ABI_ENTRY_00447040(GoliathObject_Update_Entry, GoliathObject_Update_Abi)
HOOK(0x00447040, "GoliathObject_Update", GoliathObject_Update_Entry, nullptr);

// 0x00447400 (ecx = object). Slot +0xbc (the update) of Q112Boss (vtable 0x005a4098), SaucerObject (0x005a5e98),
// ColossusObject (0x005aa238) and ZeppelinObject (0x005af500). Alive: outside states 0x12c..0x132 it runs 0x00435b50
// first; inside them (+0x424 bit 5 set by 0x00454c90, 0x00454cc0 on +0x398) once channel 4 +0x1c bit 1 is set it
// calls vt+0x19c(8, 1, 0, 0, 0, 0) and enters state 0x10f; otherwise GenericAI_Update. Dead and its +0x3c0 name is not
// "null": the same 0x00454c90 / 0x00454cc0, then when the animation is done 0x0043e710 and Object_Kill 0x00455640(0).
uint32_t Q112Boss_Update(uint32_t esi)
{
    if (!static_cast<uint8_t>(orig::fn_00454780(esi)))
    {
        uint32_t s = D(esi + 0x460);
        if (s < 0x12c || s > 0x132)
            orig::fn_00435b50(esi);
        s = D(esi + 0x460);
        if (s < 0x12c || s > 0x132)
            return orig::fn_0043ee90(esi);
        orig::fn_00454c90(esi, 1, 1);
        fn_00454cc0(esi, esi + 0x398);
        const uint32_t ch = orig::fn_00408800(4, esi);
        if (!(By(ch + 0x1c) & 2))
            return ch;
        VC(esi, 0x19c, 8u, 1u, 0u, 0u, 0u, 0u);
        return VC(esi, 0x1b8, 0x10fu);
    }
    if (Stricmp(esi + 0x3c0, 0x005bba94) == 0)
        return orig::fn_0043ee90(esi);
    orig::fn_00454c90(esi, 1, 1);
    fn_00454cc0(esi, esi + 0x398);
    const uint32_t ch = orig::fn_00408800(4, esi);
    if (!(By(ch + 0x1c) & 2))
        return ch;
    orig::fn_0043e710(esi, 0);
    return orig::fn_00455640(esi, 0);
}
static uint32_t Q112Boss_Update_Abi(uint32_t ecx)
{
    return Q112Boss_Update(ecx);
}
ABI_ENTRY_00447400(Q112Boss_Update_Entry, Q112Boss_Update_Abi)
HOOK(0x00447400, "Q112Boss_Update", Q112Boss_Update_Entry, nullptr);

// 0x00447500 (ecx = monk). MonkObject (vtable 0x005aac28) slot +0xbc, the update. While the timer +0x878 is positive
// it counts up by dt, except that a monk not in state 0xf4 for which 0x00454bc0 is true sets +0x87c = Game+0xc7c + 1,
// calls vt+0x19c(0x72, 1, 0, 0, 0, 0) and enters state 0xf4. Always ends with GenericAI_Update (tail jumps).
uint32_t MonkObject_Update(uint32_t esi)
{
    if (Fl(esi + 0x878) > K(0x0056c05c))
    {
        if (D(esi + 0x460) != 0xf4 && static_cast<uint8_t>(orig::fn_00454bc0(esi)))
        {
            Fl(esi + 0x87c) = Fl(GameP() + 0xc7c) + K(0x005bcb2c);
            VC(esi, 0x19c, 0x72u, 1u, 0u, 0u, 0u, 0u);
            VC(esi, 0x1b8, 0xf4u);
            return orig::fn_0043ee90(esi);
        }
        Fl(esi + 0x878) = Dt() + Fl(esi + 0x878);
    }
    return orig::fn_0043ee90(esi);
}
static uint32_t MonkObject_Update_Abi(uint32_t ecx)
{
    return MonkObject_Update(ecx);
}
ABI_ENTRY_00447500(MonkObject_Update_Entry, MonkObject_Update_Abi)
HOOK(0x00447500, "MonkObject_Update", MonkObject_Update_Entry, nullptr);
