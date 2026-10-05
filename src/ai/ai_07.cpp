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
uint32_t AIMarker_CurFlags(uint32_t ai, uint32_t spline);
uint32_t AIMarker_CurTrigger(uint32_t ai, uint32_t spline);
uint32_t AIMarker_CurAlarmTag(uint32_t ai, uint32_t spline);
uint32_t AIMarker_CurActionName(uint32_t ai, uint32_t spline);
uint32_t AIMarker_CurScatter(uint32_t outMax, uint32_t ai, uint32_t outMin, uint32_t spline);
uint32_t AIMarker_NextTag(uint32_t ai, uint32_t spline);
uint32_t AIMarker_Advance(uint32_t index, uint32_t ai, uint32_t spline, uint32_t teleport);
uint32_t Marker_FindSpline(uint32_t sequence, uint32_t tag);
uint32_t Marker_ProcessArrival(uint32_t spline, uint32_t ai, uint32_t wrap);
uint32_t Object_FindByTag(uint32_t list, uint32_t tag, uint32_t from);
uint64_t fn_00454cc0(uint32_t esi, uint32_t edi);
uint32_t fn_00454e60(uint32_t ebx, uint32_t edi, uint32_t a0);
uint32_t Object_MarkGrid(uint32_t self, uint32_t enable, uint32_t mode);
uint32_t fn_0046f480(uint32_t eax, uint32_t ecx);
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

// ---------------------------------------------------------------------------------------------------------------
// MarkerObject (vtable 0x005ad720): path markers. Markers with the same tag (+0x460, read by 0x0044ad40) form a spline
// ordered by "sequence" (+0x46c); the sequence-0 marker (the spline head) holds the sorted array +0x4b4 (count +0x4b8).
// An AI walking a spline keeps its current index at +0x450. Marker flags +0x490 (from the property names): bit 0
// strict, bit 2 kill, bit 3 deactivate, bit 7 face_dir, 0x100000 hide, 0x200000 attention_guard, 0x400000
// ready_guard, 0x800000 trigger_charge, 0x1000000 "ic_*" action, 0x2000000 trigger_charge_tag; bits 4 and 6 are set at
// run time (ladder code 0x00429900 / 0x00429ac0 / 0x00429ce0; meaning unknown).

// 0x0044b130 (ecx = marker; stack: 4 arguments; ret 0x10). MarkerObject slot +0x18, the property load:
// Object_LoadProperties 0x004517b0 with the same arguments, vt+0xfc(0, 2), +0x490 = 0, vt+0xf4(0), linked into the
// marker list (Game+0xa84)+0xee4, then the properties (readers get the object, the name and a2; esi = a3):
// "marker_tag" +0x460, "sequence" +0x46c, "marker_next_min" +0x464, "marker_next_max" +0x468 (0x00453400); "strict"
// bit 0, "face_dir" bit 7 (0x004534f0); "scatter_min" +0x49c, "scatter_max" +0x4a0, "speed" +0x494 (0x004534a0);
// "trigger" +0x498, "obj_charge_tag_min" +0x4a8, "obj_charge_tag_max" +0x4ac, "obj_charge_pct" +0x4b0 (0x00453450);
// "action" (string +0x470, default "none", 0x004533a0) mapped to the action flags; "kill" bit 2, "deactivate" bit 3;
// "alarm_tag" +0x4a4. Clears the spline array. The bool reader's result slot is the method's own a1 stack slot
// ([esp+0x18]), so it starts out holding a1.
uint32_t MarkerObject_LoadProperties(uint32_t ebp, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    uint32_t slot = a1;
    const uint32_t ps = u(&slot);
    orig::fn_004517b0(ebp, a0, a1, a2, a3);
    VC(ebp, 0xfc, 0u, 2u);
    D(ebp + 0x490) = 0;
    VC(ebp, 0xf4, 0u);
    orig::fn_004546d0(D(D(GameP() + 0xa84) + 0xee4), ebp);
    orig::fn_00453400(0, a3, ebp + 0x460, ebp, 0x005badb0, a2, 0);  // "marker_tag"
    orig::fn_00453400(0, a3, ebp + 0x46c, ebp, 0x005b23ec, a2, 0);  // "sequence"
    orig::fn_00453400(0, a3, ebp + 0x464, ebp, 0x005b23dc, a2, 0);  // "marker_next_min"
    orig::fn_00453400(0, a3, ebp + 0x468, ebp, 0x005b23cc, a2, 0);  // "marker_next_max"
    orig::fn_004534f0(0, a3, ps, ebp, 0x005b23c4, a2, 0);           // "strict"
    if (By(ps))
        D(ebp + 0x490) |= 1;
    orig::fn_004534f0(0, a3, ps, ebp, 0x005b23b8, a2, 0);  // "face_dir"
    if (By(ps))
        D(ebp + 0x490) |= 0x80;
    orig::fn_004534a0(a3, ebp + 0x49c, ebp, 0x005b23ac, a2, 0, 0);           // "scatter_min"
    orig::fn_004534a0(a3, ebp + 0x4a0, ebp, 0x005b23a0, a2, 0, 0);           // "scatter_max"
    orig::fn_004534a0(a3, ebp + 0x494, ebp, 0x005b6378, a2, 0, 0);           // "speed"
    orig::fn_00453450(0, 0x005bbafc, a3, ebp + 0x498, ebp, a2, 0);           // "trigger"
    orig::fn_00453450(0, 0x005b238c, a3, ebp + 0x4a8, ebp, a2, 0);           // "obj_charge_tag_min"
    orig::fn_00453450(0, 0x005b2378, a3, ebp + 0x4ac, ebp, a2, 0);           // "obj_charge_tag_max"
    orig::fn_00453450(0, 0x005b2368, a3, ebp + 0x4b0, ebp, a2, 0);           // "obj_charge_pct"
    const uint32_t action = ebp + 0x470;
    if (orig::fn_004533a0(0x005bb9e0, action, ebp, 0x005b2360, a2, a3, 0) == 1)  // "action", default "none"
    {
        uint32_t bit = 0;
        if (Stricmp(action, 0x005b2dac) == 0)  // "hide"
            bit = 0x100000;
        else if (Stricmp(action, 0x005b6e04) == 0)  // "attention_guard"
            bit = 0x200000;
        else if (Stricmp(action, 0x005b6df8) == 0)  // "ready_guard"
            bit = 0x400000;
        else if (Stricmp(action, 0x005b6de8) == 0)  // "trigger_charge"
            bit = 0x800000;
        else if (Stricmp(action, 0x005b234c) == 0)  // "trigger_charge_tag"
            bit = 0x2000000;
        else if (orig::fn_00544957(0, action, 0x005b2348, 3) == 0)  // "ic_"
            bit = 0x1000000;
        if (bit)
            D(ebp + 0x490) |= bit;
    }
    orig::fn_004534f0(0, a3, ps, ebp, 0x005bbae8, a2, 0);  // "kill"
    if (By(ps))
        D(ebp + 0x490) |= 4;
    orig::fn_004534f0(0, a3, ps, ebp, 0x005bbacc, a2, 0);  // "deactivate"
    if (By(ps))
        D(ebp + 0x490) |= 8;
    orig::fn_00453450(0, 0x005b233c, a3, ebp + 0x4a4, ebp, a2, 0);  // "alarm_tag"
    D(ebp + 0x4b4) = 0;
    D(ebp + 0x4b8) = 0;
    return 0;
}
static uint32_t MarkerObject_LoadProperties_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return MarkerObject_LoadProperties(ecx, a0, a1, a2, a3);
}
ABI_ENTRY_0044b130(MarkerObject_LoadProperties_Entry, MarkerObject_LoadProperties_Abi)
HOOK(0x0044b130, "MarkerObject_LoadProperties", MarkerObject_LoadProperties_Entry, nullptr);

// 0x0044b470 (eax = marker): the "face_dir" flag (+0x490 bit 7) in al. Caller 0x0042a660.
uint32_t Marker_GetFaceDir(uint32_t marker)
{
    return (marker & 0xffffff00u) | (By(marker + 0x490) >> 7);
}
static uint32_t Marker_GetFaceDir_Abi(uint32_t eax)
{
    return Marker_GetFaceDir(eax);
}
ABI_ENTRY_0044b470(Marker_GetFaceDir_Entry, Marker_GetFaceDir_Abi)
HOOK(0x0044b470, "Marker_GetFaceDir", Marker_GetFaceDir_Entry, nullptr);

// 0x0044b480 (eax = marker): its "sequence" (+0x46c). Executed in recorded runs.
uint32_t Marker_GetSequence(uint32_t marker)
{
    return D(marker + 0x46c);
}
static uint32_t Marker_GetSequence_Abi(uint32_t eax)
{
    return Marker_GetSequence(eax);
}
ABI_ENTRY_0044b480(Marker_GetSequence_Entry, Marker_GetSequence_Abi)
HOOK(0x0044b480, "Marker_GetSequence", Marker_GetSequence_Entry, nullptr);

// 0x0044b490 (eax = marker): its "speed" (+0x494), returned in st0. Executed in recorded runs.
float Marker_GetSpeed(uint32_t marker)
{
    return Fl(marker + 0x494);
}
static float Marker_GetSpeed_Abi(uint32_t eax)
{
    return Marker_GetSpeed(eax);
}
ABI_ENTRY_0044b490(Marker_GetSpeed_Entry, Marker_GetSpeed_Abi)
HOOK(0x0044b490, "Marker_GetSpeed", Marker_GetSpeed_Entry, nullptr);

// 0x0044b4a0 (eax = marker): the "strict" flag (+0x490 bit 0), movzx'ed. Executed in recorded runs.
uint32_t Marker_IsStrict(uint32_t marker)
{
    return By(marker + 0x490) & 1u;
}
static uint32_t Marker_IsStrict_Abi(uint32_t eax)
{
    return Marker_IsStrict(eax);
}
ABI_ENTRY_0044b4a0(Marker_IsStrict_Entry, Marker_IsStrict_Abi)
HOOK(0x0044b4a0, "Marker_IsStrict", Marker_IsStrict_Entry, nullptr);

// 0x0044b4b0 (al = first, ecx = spline head, edx = AI). Marker_IsStrict (a tail jump) of the spline's first marker
// when al is set, else of the marker before the AI's current one (+0x450 - 1; the last one when +0x450 <= 0).
// Executed in recorded runs.
uint32_t AIMarker_PrevIsStrict(uint32_t eax, uint32_t ecx, uint32_t edx)
{
    if (static_cast<uint8_t>(eax))
        return Marker_IsStrict(D(D(ecx + 0x4b4)));
    const int32_t i = I(edx + 0x450);
    if (i > 0)
        return Marker_IsStrict(D(D(ecx + 0x4b4) + static_cast<uint32_t>(i) * 4 - 4));
    return Marker_IsStrict(D(D(ecx + 0x4b4) + D(ecx + 0x4b8) * 4 - 4));
}
static uint32_t AIMarker_PrevIsStrict_Abi(uint32_t eax, uint32_t ecx, uint32_t edx)
{
    return AIMarker_PrevIsStrict(eax, ecx, edx);
}
ABI_ENTRY_0044b4b0(AIMarker_PrevIsStrict_Entry, AIMarker_PrevIsStrict_Abi)
HOOK(0x0044b4b0, "AIMarker_PrevIsStrict", AIMarker_PrevIsStrict_Entry, nullptr);

// 0x0044b4f0 (eax = AI, ecx = spline head): Marker_GetSpeed (a tail jump) of the marker before the AI's current one
// (the last one when +0x450 <= 0), st0. Executed in recorded runs.
float AIMarker_PrevSpeed(uint32_t eax, uint32_t ecx)
{
    const int32_t i = I(eax + 0x450);
    if (i > 0)
        return Marker_GetSpeed(D(D(ecx + 0x4b4) + static_cast<uint32_t>(i) * 4 - 4));
    return Marker_GetSpeed(D(D(ecx + 0x4b4) + D(ecx + 0x4b8) * 4 - 4));
}
static float AIMarker_PrevSpeed_Abi(uint32_t eax, uint32_t ecx)
{
    return AIMarker_PrevSpeed(eax, ecx);
}
ABI_ENTRY_0044b4f0(AIMarker_PrevSpeed_Entry, AIMarker_PrevSpeed_Abi)
HOOK(0x0044b4f0, "AIMarker_PrevSpeed", AIMarker_PrevSpeed_Entry, nullptr);

// 0x0044b540 (ecx = marker). MarkerObject slot +0x20 (executed in recorded runs). On the spline head (sequence 0)
// only: builds a sorted list (PMS_heapAlloc 0x004e07f0(pool 0x0063a210, 0x10, 0x1c, 2, "new"), 0x004e6bd0(10,
// comparator 0x0044b520 = sequence difference)), inserts every marker of the list (Game+0xa84)+0xee4 with the same tag
// (0x0044ad40, 0x004e6e00), allocates the array +0x4b4 ("spline keys", count * 4) and fills it in sequence order,
// taking a reference on each (+0x28), then deletes the list (vt+0x10(1)).
uint32_t MarkerObject_BuildSpline(uint32_t ebx)
{
    const uint32_t seq = Marker_GetSequence(ebx);
    if (seq)
        return seq;
    uint32_t it[2] = {0, 0};  // [esp+0x18]
    uint32_t list = orig::fn_004e07f0(0x0063a210, 0x10, 0x1c, 2, 0x0058b5e8);
    if (list)
        list = orig::fn_004e6bd0(10, list, 0x0044b520);
    const uint32_t tag = orig::fn_0044ad40(ebx);  // [esp+0x14]
    for (uint32_t m = orig::fn_004e7340(D(D(GameP() + 0xa84) + 0xee4), u(it)); m; m = orig::fn_004e73c0(u(it)))
    {
        if (orig::fn_0044ad40(m) == tag)
            orig::fn_004e6e00(list, m);
    }
    const uint32_t n = VC(list, 0x34);
    D(ebx + 0x4b4) = orig::fn_004e07f0(0x0063a210, 3, n << 2, 2, 0x005b2330);
    for (uint32_t m = orig::fn_004e7340(list, u(it)); m; m = orig::fn_004e73c0(u(it)))
    {
        D(D(ebx + 0x4b4) + D(ebx + 0x4b8) * 4) = m;
        ++I(D(D(ebx + 0x4b4) + D(ebx + 0x4b8) * 4) + 0x28);
        D(ebx + 0x4b8) = D(ebx + 0x4b8) + 1;
    }
    VC(list, 0x10, 1u);
    if (it[1])
        --I(it[1] + 0x18);
    return it[1];
}
static uint32_t MarkerObject_BuildSpline_Abi(uint32_t ecx)
{
    return MarkerObject_BuildSpline(ecx);
}
ABI_ENTRY_0044b540(MarkerObject_BuildSpline_Entry, MarkerObject_BuildSpline_Abi)
HOOK(0x0044b540, "MarkerObject_BuildSpline", MarkerObject_BuildSpline_Entry, nullptr);

// 0x0044b680 (ecx = marker). MarkerObject slot +0xdc: drops the references of the spline array, frees it
// (PMS_heapFree 0x004e08d0, pool 0x0063a210), clears +0x4b4 / +0x4b8, then a tail jump to Object_Remove 0x004547b0.
uint32_t MarkerObject_Remove(uint32_t edi)
{
    if (D(edi + 0x4b4))
    {
        for (uint32_t i = 0; i < D(edi + 0x4b8); ++i)
            Unref(D(D(edi + 0x4b4) + i * 4));
        if (D(edi + 0x4b4))
        {
            orig::fn_004e08d0(0x0063a210, D(edi + 0x4b4));
            D(edi + 0x4b4) = 0;
        }
        D(edi + 0x4b8) = 0;
    }
    return orig::fn_004547b0(edi);
}
static uint32_t MarkerObject_Remove_Abi(uint32_t ecx)
{
    return MarkerObject_Remove(ecx);
}
ABI_ENTRY_0044b680(MarkerObject_Remove_Entry, MarkerObject_Remove_Abi)
HOOK(0x0044b680, "MarkerObject_Remove", MarkerObject_Remove_Entry, nullptr);

// 0x0044b710 (eax = optional matrix out, esi = heading out, edi = spline head; stack: position out). For the spline's
// last marker: vt+0x5c(matrix) when eax is set, 0x00453150 (ecx = marker, esi = out), then a tail jump to its vt+0x54
// (position) with the caller's argument, which that method pops (ret 4). No ret of its own, so the entry stub is
// hand-written (ret 4). Executed in recorded runs (callers 0x00429ac0, 0x00429ce0: ladder bottom point, guess).
uint32_t Spline_GetEndPoint(uint32_t eax, uint32_t esi, uint32_t edi, uint32_t a0)
{
    if (eax)
        VC(D(D(edi + 0x4b4) + D(edi + 0x4b8) * 4 - 4), 0x5c, eax);
    orig::fn_00453150(D(D(edi + 0x4b4) + D(edi + 0x4b8) * 4 - 4), esi);
    return VC(D(D(edi + 0x4b4) + D(edi + 0x4b8) * 4 - 4), 0x54, a0);
}
static uint32_t Spline_GetEndPoint_Abi(uint32_t eax, uint32_t esi, uint32_t edi, uint32_t a0)
{
    return Spline_GetEndPoint(eax, esi, edi, a0);
}
static __declspec(naked) void Spline_GetEndPoint_Entry()
{
    __asm {
        push ecx
        push edx
        push ebx
        push esi
        push edi
        push ebp
        push dword ptr [esp + 28]
        push edi
        push esi
        push eax
        call Spline_GetEndPoint_Abi
        add esp, 16
        pop ebp
        pop edi
        pop esi
        pop ebx
        pop edx
        pop ecx
        ret 4
    }
}
HOOK(0x0044b710, "Spline_GetEndPoint", Spline_GetEndPoint_Entry, nullptr);

// 0x0044b760 (ecx = spline head; returns al). Executed in recorded runs (caller 0x00429ac0). True when the marker is
// a spline head (sequence 0) whose last marker has an "ic_*" action (0x1000000) named "ic_scale_base_wall". The upper
// eax bytes are those the original leaves (the last value tested).
uint32_t Spline_EndsAtScaleWall(uint32_t ecx)
{
    uint32_t eax = Marker_GetSequence(ecx);
    if (eax)
        return eax & 0xffffff00u;
    const uint32_t m = D(D(ecx + 0x4b4) + D(ecx + 0x4b8) * 4 - 4);
    const uint32_t name = Marker_GetActionName(m);
    eax = Marker_GetFlags(m);
    if (!(eax & 0x1000000))
        return eax & 0xffffff00u;
    eax = static_cast<uint32_t>(Stricmp(name, 0x005b231c));
    if (eax)
        return eax & 0xffffff00u;
    return 1;
}
static uint32_t Spline_EndsAtScaleWall_Abi(uint32_t ecx)
{
    return Spline_EndsAtScaleWall(ecx);
}
ABI_ENTRY_0044b760(Spline_EndsAtScaleWall_Entry, Spline_EndsAtScaleWall_Abi)
HOOK(0x0044b760, "Spline_EndsAtScaleWall", Spline_EndsAtScaleWall_Entry, nullptr);

// 0x0044b7b0 (eax = marker): +0x490 bit 4 in al (upper eax bytes: the marker's). Set by 0x0044b7c0; meaning unknown.
uint32_t fn_0044b7b0(uint32_t marker)
{
    return (marker & 0xffffff00u) | ((By(marker + 0x490) >> 4) & 1u);
}
static uint32_t fn_0044b7b0_Abi(uint32_t eax)
{
    return fn_0044b7b0(eax);
}
ABI_ENTRY_0044b7b0(fn_0044b7b0_Entry, fn_0044b7b0_Abi)
HOOK(0x0044b7b0, "fn_0044b7b0", fn_0044b7b0_Entry, nullptr);

// 0x0044b7c0 (eax = marker, cl = on): sets or clears +0x490 bit 4. Caller 0x0042bf70.
void fn_0044b7c0(uint32_t marker, uint32_t on)
{
    if (static_cast<uint8_t>(on))
        D(marker + 0x490) |= 0x10;
    else
        D(marker + 0x490) &= 0xffffffefu;
}
static void fn_0044b7c0_Abi(uint32_t eax, uint32_t ecx)
{
    fn_0044b7c0(eax, ecx);
}
ABI_ENTRY_0044b7c0(fn_0044b7c0_Entry, fn_0044b7c0_Abi)
HOOK(0x0044b7c0, "fn_0044b7c0", fn_0044b7c0_Entry, nullptr);

// 0x0044b7e0 (eax = marker): +0x490 bit 6 in al (upper eax bytes: the marker's). Executed in recorded runs; set by
// 0x0044b7f0 from the ladder code; meaning unknown.
uint32_t fn_0044b7e0(uint32_t marker)
{
    return (marker & 0xffffff00u) | ((By(marker + 0x490) >> 6) & 1u);
}
static uint32_t fn_0044b7e0_Abi(uint32_t eax)
{
    return fn_0044b7e0(eax);
}
ABI_ENTRY_0044b7e0(fn_0044b7e0_Entry, fn_0044b7e0_Abi)
HOOK(0x0044b7e0, "fn_0044b7e0", fn_0044b7e0_Entry, nullptr);

// 0x0044b7f0 (eax = marker, cl = on): sets or clears +0x490 bit 6. Executed in recorded runs.
void fn_0044b7f0(uint32_t marker, uint32_t on)
{
    if (static_cast<uint8_t>(on))
        D(marker + 0x490) |= 0x40;
    else
        D(marker + 0x490) &= 0xffffffbfu;
}
static void fn_0044b7f0_Abi(uint32_t eax, uint32_t ecx)
{
    fn_0044b7f0(eax, ecx);
}
ABI_ENTRY_0044b7f0(fn_0044b7f0_Entry, fn_0044b7f0_Abi)
HOOK(0x0044b7f0, "fn_0044b7f0", fn_0044b7f0_Entry, nullptr);

// 0x0044b810 (eax = marker, edx = min out; stack: max out; ret 4): "marker_next_min" (+0x464) and "marker_next_max"
// (+0x468). Returns the max. Executed in recorded runs.
uint32_t Marker_GetNextRange(uint32_t marker, uint32_t outMin, uint32_t outMax)
{
    D(outMin) = D(marker + 0x464);
    const uint32_t mx = D(marker + 0x468);
    D(outMax) = mx;
    return mx;
}
static uint32_t Marker_GetNextRange_Abi(uint32_t eax, uint32_t edx, uint32_t a0)
{
    return Marker_GetNextRange(eax, edx, a0);
}
ABI_ENTRY_0044b810(Marker_GetNextRange_Entry, Marker_GetNextRange_Abi)
HOOK(0x0044b810, "Marker_GetNextRange", Marker_GetNextRange_Entry, nullptr);

// 0x0044b830 (ecx = AI, edi = spline head). The tag of the spline to switch to at the AI's current marker: a random
// value in [marker_next_min, marker_next_max] ((rand * (max - min + 1) >> 15) + min) when the min is set, else the
// spline's own tag (+0x460). Executed in recorded runs.
uint32_t AIMarker_NextTag(uint32_t ai, uint32_t spline)
{
    uint32_t range[2];  // [esp+4] min, [esp+8] max
    Marker_GetNextRange(D(D(spline + 0x4b4) + D(ai + 0x450) * 4), u(&range[0]), u(&range[1]));
    const uint32_t mn = range[0];
    if (!mn)
        return D(spline + 0x460);
    const uint32_t r = Rand();
    return static_cast<uint32_t>(static_cast<int32_t>(r * (range[1] - mn + 1)) >> 15) + mn;
}
static uint32_t AIMarker_NextTag_Abi(uint32_t ecx, uint32_t edi)
{
    return AIMarker_NextTag(ecx, edi);
}
ABI_ENTRY_0044b830(AIMarker_NextTag_Entry, AIMarker_NextTag_Abi)
HOOK(0x0044b830, "AIMarker_NextTag", AIMarker_NextTag_Entry, nullptr);

// 0x0044b880 (eax = marker): the flags +0x490. Executed in recorded runs.
uint32_t Marker_GetFlags(uint32_t marker)
{
    return D(marker + 0x490);
}
static uint32_t Marker_GetFlags_Abi(uint32_t eax)
{
    return Marker_GetFlags(eax);
}
ABI_ENTRY_0044b880(Marker_GetFlags_Entry, Marker_GetFlags_Abi)
HOOK(0x0044b880, "Marker_GetFlags", Marker_GetFlags_Entry, nullptr);

// 0x0044b890 (eax = AI, edx = spline head): Marker_GetFlags of the AI's current marker (a tail jump).
uint32_t AIMarker_CurFlags(uint32_t ai, uint32_t spline)
{
    return Marker_GetFlags(D(D(spline + 0x4b4) + D(ai + 0x450) * 4));
}
static uint32_t AIMarker_CurFlags_Abi(uint32_t eax, uint32_t edx)
{
    return AIMarker_CurFlags(eax, edx);
}
ABI_ENTRY_0044b890(AIMarker_CurFlags_Entry, AIMarker_CurFlags_Abi)
HOOK(0x0044b890, "AIMarker_CurFlags", AIMarker_CurFlags_Entry, nullptr);

// 0x0044b8b0 (eax = marker): its "trigger" (+0x498). Executed in recorded runs.
uint32_t Marker_GetTrigger(uint32_t marker)
{
    return D(marker + 0x498);
}
static uint32_t Marker_GetTrigger_Abi(uint32_t eax)
{
    return Marker_GetTrigger(eax);
}
ABI_ENTRY_0044b8b0(Marker_GetTrigger_Entry, Marker_GetTrigger_Abi)
HOOK(0x0044b8b0, "Marker_GetTrigger", Marker_GetTrigger_Entry, nullptr);

// 0x0044b8c0 (eax = AI, edx = spline head): Marker_GetTrigger of the AI's current marker (a tail jump).
uint32_t AIMarker_CurTrigger(uint32_t ai, uint32_t spline)
{
    return Marker_GetTrigger(D(D(spline + 0x4b4) + D(ai + 0x450) * 4));
}
static uint32_t AIMarker_CurTrigger_Abi(uint32_t eax, uint32_t edx)
{
    return AIMarker_CurTrigger(eax, edx);
}
ABI_ENTRY_0044b8c0(AIMarker_CurTrigger_Entry, AIMarker_CurTrigger_Abi)
HOOK(0x0044b8c0, "AIMarker_CurTrigger", AIMarker_CurTrigger_Entry, nullptr);

// 0x0044b8e0 (eax = marker): its "alarm_tag" (+0x4a4). Executed in recorded runs.
uint32_t Marker_GetAlarmTag(uint32_t marker)
{
    return D(marker + 0x4a4);
}
static uint32_t Marker_GetAlarmTag_Abi(uint32_t eax)
{
    return Marker_GetAlarmTag(eax);
}
ABI_ENTRY_0044b8e0(Marker_GetAlarmTag_Entry, Marker_GetAlarmTag_Abi)
HOOK(0x0044b8e0, "Marker_GetAlarmTag", Marker_GetAlarmTag_Entry, nullptr);

// 0x0044b8f0 (eax = AI, edx = spline head): Marker_GetAlarmTag of the AI's current marker (a tail jump).
uint32_t AIMarker_CurAlarmTag(uint32_t ai, uint32_t spline)
{
    return Marker_GetAlarmTag(D(D(spline + 0x4b4) + D(ai + 0x450) * 4));
}
static uint32_t AIMarker_CurAlarmTag_Abi(uint32_t eax, uint32_t edx)
{
    return AIMarker_CurAlarmTag(eax, edx);
}
ABI_ENTRY_0044b8f0(AIMarker_CurAlarmTag_Entry, AIMarker_CurAlarmTag_Abi)
HOOK(0x0044b8f0, "AIMarker_CurAlarmTag", AIMarker_CurAlarmTag_Entry, nullptr);

// 0x0044b910 (eax = marker): its "action" name (&+0x470). Executed in recorded runs.
uint32_t Marker_GetActionName(uint32_t marker)
{
    return marker + 0x470;
}
static uint32_t Marker_GetActionName_Abi(uint32_t eax)
{
    return Marker_GetActionName(eax);
}
ABI_ENTRY_0044b910(Marker_GetActionName_Entry, Marker_GetActionName_Abi)
HOOK(0x0044b910, "Marker_GetActionName", Marker_GetActionName_Entry, nullptr);

// 0x0044b920 (eax = AI, edx = spline head): Marker_GetActionName of the AI's current marker (a tail jump).
uint32_t AIMarker_CurActionName(uint32_t ai, uint32_t spline)
{
    return Marker_GetActionName(D(D(spline + 0x4b4) + D(ai + 0x450) * 4));
}
static uint32_t AIMarker_CurActionName_Abi(uint32_t eax, uint32_t edx)
{
    return AIMarker_CurActionName(eax, edx);
}
ABI_ENTRY_0044b920(AIMarker_CurActionName_Entry, AIMarker_CurActionName_Abi)
HOOK(0x0044b920, "AIMarker_CurActionName", AIMarker_CurActionName_Entry, nullptr);

// 0x0044b940 (eax = marker, edx = min out; stack: max out; ret 4): "scatter_min" (+0x49c) and "scatter_max"
// (+0x4a0). Returns the max. Executed in recorded runs.
uint32_t Marker_GetScatter(uint32_t marker, uint32_t outMin, uint32_t outMax)
{
    D(outMin) = D(marker + 0x49c);
    const uint32_t mx = D(marker + 0x4a0);
    D(outMax) = mx;
    return mx;
}
static uint32_t Marker_GetScatter_Abi(uint32_t eax, uint32_t edx, uint32_t a0)
{
    return Marker_GetScatter(eax, edx, a0);
}
ABI_ENTRY_0044b940(Marker_GetScatter_Entry, Marker_GetScatter_Abi)
HOOK(0x0044b940, "Marker_GetScatter", Marker_GetScatter_Entry, nullptr);

// 0x0044b960 (eax = max out, ecx = AI, edx = min out; stack: spline head; ret 4): Marker_GetScatter of the AI's
// current marker (the original pushes eax as the callee's stack argument). Executed in recorded runs.
uint32_t AIMarker_CurScatter(uint32_t outMax, uint32_t ai, uint32_t outMin, uint32_t spline)
{
    return Marker_GetScatter(D(D(spline + 0x4b4) + D(ai + 0x450) * 4), outMin, outMax);
}
static uint32_t AIMarker_CurScatter_Abi(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t a0)
{
    return AIMarker_CurScatter(eax, ecx, edx, a0);
}
ABI_ENTRY_0044b960(AIMarker_CurScatter_Entry, AIMarker_CurScatter_Abi)
HOOK(0x0044b960, "AIMarker_CurScatter", AIMarker_CurScatter_Entry, nullptr);

// 0x0044b980 (eax = index, ecx = AI, edx = spline head; stack: face flag (byte); ret 4). Executed in recorded runs
// (callers AI_FollowMarkerSpline 0x0042d1b0, 0x0044ba30). With more than one marker: the AI's index becomes index + 1,
// the target is that marker's position (vt+0x54) and, when the flag is set, its heading (0x00453150); then the grid
// route request 0x0046f5b0 (al = no heading, ecx = heading or 0, edx = position, esi = AI, edi = grid Game+0xaa4;
// stack 1, 0). With one marker: index -1 becomes 0, then 0x0046f440 (esi = grid, edi = AI).
uint32_t AIMarker_Advance(uint32_t index, uint32_t ai, uint32_t spline, uint32_t face)
{
    uint32_t L[6];  // [esp+8] position, [esp+0x14] heading
    if (D(spline + 0x4b8) > 1)
    {
        const uint32_t i = index + 1;
        D(ai + 0x450) = i;
        VC(D(D(spline + 0x4b4) + i * 4), 0x54, u(&L[0]));
        uint32_t noHeading = 1;
        uint32_t heading = 0;
        if (static_cast<uint8_t>(face))
        {
            orig::fn_00453150(D(D(spline + 0x4b4) + D(ai + 0x450) * 4), u(&L[3]));
            noHeading = 0;
            heading = u(&L[3]);
        }
        return fn_0046f5b0(noHeading, heading, u(&L[0]), ai, D(GameP() + 0xaa4), 1, 0);
    }
    D(ai + 0x450) = index == 0xffffffffu ? 0 : index;
    return fn_0046f440(D(GameP() + 0xaa4), ai);
}
static uint32_t AIMarker_Advance_Abi(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t a0)
{
    return AIMarker_Advance(eax, ecx, edx, a0);
}
ABI_ENTRY_0044b980(AIMarker_Advance_Entry, AIMarker_Advance_Abi)
HOOK(0x0044b980, "AIMarker_Advance", AIMarker_Advance_Entry, nullptr);

namespace
{
// Marker_ProcessArrival: places the AI at its current marker for an "ic_*" action (the marker's matrix, vt+0x5c, then
// Object_SetTransform 0x00457990 with al = 0) and returns the AI's vt+0x248(kind, &out) result.
inline uint32_t Arrival_PlaceAndAct(uint32_t ai, uint32_t spline, uint32_t kind, uint32_t* L)
{
    VC(D(D(spline + 0x4b4) + D(ai + 0x450) * 4), 0x5c, u(&L[0x20 / 4]));
    orig::fn_00457990(0, ai, u(&L[0x20 / 4]));
    return VC(ai, 0x248, kind, u(&L[0x0c / 4]));
}
// The tag test 0x0044ba30 applies to objects of list (Game+0xa84)+0xe70: 0x00453290 gives the object's tag in *out
// (returns al); the tag must be within [+0x4a8, +0x4ac] ("obj_charge_tag_min" / "obj_charge_tag_max") of the spline.
inline bool Arrival_TagInRange(uint32_t object, uint32_t* out, uint32_t spline)
{
    if (!static_cast<uint8_t>(orig::fn_00453290(object, u(out))))
        return false;
    const int32_t t = static_cast<int32_t>(*out);
    return t >= I(spline + 0x4a8) && t <= I(spline + 0x4ac);
}
}  // namespace

// 0x0044ba30 (stack: spline head, AI, wrap; ret 0xc). Executed in recorded runs (callers GenericAI_Update via
// 0x0044bff0, 0x00485910). What an AI does on reaching its current marker (strings "fx_siren", "ic_climb_ladder",
// "ic_plant_ladder", "ic_scale_base_wall", "Marker : Switching to spline that does not exists\nMarker_Tag %d"):
// fires the marker's "trigger" (Object_SendTrigger 0x00461870); with an "alarm_tag" finds that object (0x004592e0) and,
// unless it is already in list (Game+0xa84)+0xef0, starts "fx_siren" on it (Fx_Create 0x0041cc60, {4, ..., "null",
// object}), calls the AI's vt+0x264() and links it into that list. Then by the marker flags: kill (vt+0xdc), deactivate
// (vt+0xd4(0)), hide (vt+0x240(0, 0, 1) else AI_Wander 0x0042d430(60, 100)), ready_guard / attention_guard
// (0x0042d0f0(0 / 1, 1)), trigger_charge (0x0042ce70(0)), trigger_charge_tag (with probability obj_charge_pct: a random
// object of list +0xe70 whose tag is in range, 0x0042ce70(object)), an "ic_*" action (vt+0x248 kind 1 climb ladder /
// 0 plant ladder / 2 scale base wall, else the name after "ic_" with 0, then vt+0x238(result, out)), or a scatter
// wander (AI_Wander with scatter_min / max when the min is positive). Then advances (0x0044b980, face flag from bit 7)
// along the spline, wrapping when allowed (returns 0 at the end otherwise), or switches to the spline with the
// AIMarker_NextTag tag (0x0044c030, 0x00426640) and starts at its first marker. Returns 1.
// The original frame (sub esp, 0x94 after aligning) is kept as L, L[o / 4] = [esp + o] (3 registers pushed).
uint32_t Marker_ProcessArrival(uint32_t spline, uint32_t ai, uint32_t wrap)
{
    uint32_t L[0xa0 / 4];
    uint32_t esi = spline;
    uint32_t edi = ai;
    L[0x10 / 4] = AIMarker_CurFlags(edi, esi);
    const uint32_t trigger = AIMarker_CurTrigger(edi, esi);
    if (trigger)
        orig::fn_00461870(D(GameP() + 0xa84), trigger, edi);
    const uint32_t alarmTag = AIMarker_CurAlarmTag(edi, esi);
    if (alarmTag)
    {
        const uint32_t alarm = Object_FindByTag(D(D(GameP() + 0xa84) + 0xe70), alarmTag, esi);
        if (alarm)
        {
            const uint32_t known = orig::fn_00454690(D(D(GameP() + 0xa84) + 0xef0), alarm);
            if (!static_cast<uint8_t>(known))
            {
                L[0x3c / 4] = alarm;
                L[0x38 / 4] = 0x005bba94;  // "null"
                L[0x20 / 4] = 4;
                orig::fn_0041cc60(known, D(GameP() + 0xa90), 0x005b2310, u(&L[0x20 / 4]));  // "fx_siren"
                VC(ai, 0x264);
                orig::fn_004546d0(D(D(GameP() + 0xa84) + 0xef0), alarm);
            }
            edi = ai;
            esi = spline;
        }
    }
    const uint8_t first = static_cast<uint8_t>(L[0x10 / 4]);
    if (first & 8)
    {
        VC(edi, 0xdc);
        return 1;
    }
    if (first & 4)
    {
        VC(edi, 0xd4, 0u);
        return 1;
    }
    uint32_t ebx = AIMarker_CurFlags(edi, esi);
    L[0x10 / 4] = ebx;
    if (ebx & 0x100000)  // hide
    {
        if (static_cast<uint8_t>(VC(edi, 0x240, 0u, 0u, 1u)))
            return 1;
        orig::fn_0042d430(edi, 0x42700000, 0x42c80000);
        return 1;
    }
    if (ebx & 0x400000)  // ready_guard
    {
        orig::fn_0042d0f0(edi, 0, 0, 1);
        return 1;
    }
    if (ebx & 0x200000)  // attention_guard
    {
        orig::fn_0042d0f0(edi, 0, 1, 1);
        return 1;
    }
    if (ebx & 0x800000)  // trigger_charge
    {
        orig::fn_0042ce70(0, edi);
        return 1;
    }
    if (ebx & 0x2000000)  // trigger_charge_tag
    {
        const int32_t roll = static_cast<int32_t>(Rand() * 100) >> 15;
        if (roll >= I(esi + 0x4b0) || D(esi + 0x4a8) == 0)
        {
            orig::fn_0042ce70(0, edi);
            goto L_bd65;
        }
        L[0x18 / 4] = 0;
        L[0x1c / 4] = 0;
        {
            const uint32_t objects = D(D(GameP() + 0xa84) + 0xe70);
            uint32_t count = 0;
            L[0x0c / 4] = 0;
            uint32_t o = orig::fn_004e7340(objects, u(&L[0x18 / 4]));
            if (o)
            {
                do
                {
                    if (Arrival_TagInRange(o, &L[0x0c / 4], spline))
                        ++count;
                    o = orig::fn_004e73c0(u(&L[0x18 / 4]));
                } while (o);
                L[0x0c / 4] = count;
                if (count)
                {
                    const int32_t pick = static_cast<int32_t>(Rand() * L[0x0c / 4]) >> 15;
                    L[0x0c / 4] = 0;
                    uint32_t c = orig::fn_004e7340(D(D(GameP() + 0xa84) + 0xe70), u(&L[0x18 / 4]));
                    for (; c; c = orig::fn_004e73c0(u(&L[0x18 / 4])))
                    {
                        if (!Arrival_TagInRange(c, &L[0x14 / 4], spline))
                            continue;
                        if (static_cast<int32_t>(L[0x0c / 4]) == pick)
                            break;
                        ++L[0x0c / 4];
                    }
                    if (c)
                        orig::fn_004e7420(u(&L[0x18 / 4]));
                    orig::fn_0042ce70(c, ai);
                    edi = ai;
                    goto L_bd53;
                }
                edi = ai;
            }
            orig::fn_0042ce70(0, edi);
        }
    L_bd53:
        if (L[0x1c / 4])
            --I(L[0x1c / 4] + 0x18);
        ebx = L[0x10 / 4];
        esi = spline;
        goto L_bd65;
    }
    if (ebx & 0x1000000)  // "ic_*" action
    {
        const uint32_t name = AIMarker_CurActionName(edi, esi);
        uint32_t act;
        if ((By(esi + 0x490) & 0x10) && Stricmp(name, 0x005b2300) == 0)  // "ic_climb_ladder"
            act = Arrival_PlaceAndAct(edi, esi, 1, L);
        else if (!(By(esi + 0x490) & 0x10) && Stricmp(name, 0x005b22f0) == 0)  // "ic_plant_ladder"
            act = Arrival_PlaceAndAct(edi, esi, 0, L);
        else if (Stricmp(name, 0x005b231c) == 0)  // "ic_scale_base_wall"
            act = Arrival_PlaceAndAct(edi, esi, 2, L);
        else
        {
            act = name + 3;
            L[0x0c / 4] = 0;
        }
        VC(edi, 0x238, act, L[0x0c / 4]);
        return 1;
    }
    // scatter: wander around the marker when scatter_min is positive
    AIMarker_CurScatter(u(&L[0x14 / 4]), edi, u(&L[0x10 / 4]), esi);
    if (Fl(u(&L[0x10 / 4])) > K(0x0056c05c))
    {
        orig::fn_0042d430(edi, L[0x10 / 4], L[0x14 / 4]);
        return 1;
    }
L_bd65:
    {
        const uint32_t next = AIMarker_NextTag(edi, esi);
        if (next == D(spline + 0x460))
        {
            const uint32_t i = D(ai + 0x450) + 1;
            D(ai + 0x450) = i;
            if (static_cast<int32_t>(i) >= I(spline + 0x4b8))
            {
                if (!wrap)
                    return 0;
                D(ai + 0x450) = 0;
            }
            const uint32_t index = D(ai + 0x450) - 1;
            AIMarker_Advance(index, ai, spline, static_cast<int8_t>(ebx) < 0 ? 1u : 0u);
            return 1;
        }
        const uint32_t other = Marker_FindSpline(0, next);
        if (!other)
        {
            orig::fn_00407ff0(D(GameP() + 0x34), 0x005b22b0, next, 0);
            return 1;
        }
        L[0x14 / 4] = Marker_GetFlags(other);
        orig::fn_00426640(other, ai);
        AIMarker_Advance(0xffffffffu, ai, other, static_cast<int8_t>(L[0x14 / 4]) < 0 ? 1u : 0u);
        return 1;
    }
}
static uint32_t Marker_ProcessArrival_Abi(uint32_t a0, uint32_t a1, uint32_t a2)
{
    return Marker_ProcessArrival(a0, a1, a2);
}
ABI_ENTRY_0044ba30(Marker_ProcessArrival_Entry, Marker_ProcessArrival_Abi)
HOOK(0x0044ba30, "Marker_ProcessArrival", Marker_ProcessArrival_Entry, nullptr);

// 0x0044bff0 (esi = AI; stack: spline head, wrap; ret 8). Executed in recorded runs (caller GenericAI_Update): the
// grid step 0x00470aa0 (eax = AI, ecx = grid Game+0xaa4; 0), then unless the AI's route +0x428 is still running
// (0x00470c30) Marker_ProcessArrival(spline, AI, wrap); returns 1 while the route runs.
uint32_t AIMarker_Step(uint32_t esi, uint32_t a0, uint32_t a1)
{
    fn_00470aa0(esi, D(GameP() + 0xaa4), 0);
    if (static_cast<uint8_t>(fn_00470c30(esi + 0x428)))
        return 1;
    return Marker_ProcessArrival(a0, esi, a1);
}
static uint32_t AIMarker_Step_Abi(uint32_t esi, uint32_t a0, uint32_t a1)
{
    return AIMarker_Step(esi, a0, a1);
}
ABI_ENTRY_0044bff0(AIMarker_Step_Entry, AIMarker_Step_Abi)
HOOK(0x0044bff0, "AIMarker_Step", AIMarker_Step_Entry, nullptr);

// 0x0044c030 (ebx = sequence; stack: tag; caller pops). Executed in recorded runs. The marker of list
// (Game+0xa84)+0xee4 with that tag (0x0044ad40) and sequence, or 0.
uint32_t Marker_FindSpline(uint32_t sequence, uint32_t tag)
{
    uint32_t it[2] = {0, 0};  // [esp+0xc]
    for (uint32_t m = orig::fn_004e7340(D(D(GameP() + 0xa84) + 0xee4), u(it)); m; m = orig::fn_004e73c0(u(it)))
    {
        if (orig::fn_0044ad40(m) == tag && Marker_GetSequence(m) == sequence)
        {
            const uint32_t node = it[1];
            VC(it[0], 0xc, node);
            --I(node + 0x18);
            return m;
        }
    }
    if (it[1])
        --I(it[1] + 0x18);
    return 0;
}
static uint32_t Marker_FindSpline_Abi(uint32_t ebx, uint32_t a0)
{
    return Marker_FindSpline(ebx, a0);
}
ABI_ENTRY_0044c030(Marker_FindSpline_Entry, Marker_FindSpline_Abi)
HOOK(0x0044c030, "Marker_FindSpline", Marker_FindSpline_Entry, nullptr);

// 0x0044c0c0 (ecx = marker; stack: 4 arguments; ret 0x10). TeammateMarker (vtable 0x005a64d0) slot +0x18, the property
// load: "radius" into +0x47c (Object_ReadValuePropDef 0x004534a0, default 100.0), TriggerObject_Load 0x00497460 with
// the same arguments, then linked into list (Game+0xa84)+0xee8.
uint32_t TeammateMarker_LoadProperties(uint32_t ebx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    orig::fn_004534a0(a3, ebx + 0x47c, ebx, 0x005b22a8, a2, 0x42c80000, 0);
    orig::fn_00497460(ebx, a0, a1, a2, a3);
    return static_cast<uint32_t>(orig::fn_004546d0(D(D(GameP() + 0xa84) + 0xee8), ebx));
}
static uint32_t TeammateMarker_LoadProperties_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return TeammateMarker_LoadProperties(ecx, a0, a1, a2, a3);
}
ABI_ENTRY_0044c0c0(TeammateMarker_LoadProperties_Entry, TeammateMarker_LoadProperties_Abi)
HOOK(0x0044c0c0, "TeammateMarker_LoadProperties", TeammateMarker_LoadProperties_Entry, nullptr);

// 0x0044c120 (stack: descriptor; ret 4; ecx not read). TeammateMarker slot +0x1c: *d = 0x4000, d[0xe] = 1,
// d[0x7a] = 1 (bytes). Returns d.
uint32_t fn_0044c120(uint32_t d)
{
    D(d) = 0x4000;
    By(d + 0xe) = 1;
    By(d + 0x7a) = 1;
    return d;
}
static uint32_t fn_0044c120_Abi(uint32_t a0)
{
    return fn_0044c120(a0);
}
ABI_ENTRY_0044c120(fn_0044c120_Entry, fn_0044c120_Abi)
HOOK(0x0044c120, "fn_0044c120", fn_0044c120_Entry, nullptr);

// 0x0044c140 (ecx = marker; stack: flag (byte), object; ret 8). TeammateMarker slot +0xb4: when the flag is set and the
// object is the player (Game+0x748), sets the byte +0x478 (guess: reached by the player). Returns the last eax (the
// object when compared; otherwise al = the flag with the caller's upper bytes, given here as 0).
uint32_t fn_0044c140(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    if (!static_cast<uint8_t>(a0))
        return 0;
    if (a1 == D(GameP() + 0x748))
        By(ecx + 0x478) = 1;
    return a1;
}
static uint32_t fn_0044c140_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return fn_0044c140(ecx, a0, a1);
}
ABI_ENTRY_0044c140(fn_0044c140_Entry, fn_0044c140_Abi)
HOOK(0x0044c140, "fn_0044c140", fn_0044c140_Entry, nullptr);

// 0x0044e6c0 (ecx = molmer). MolmerObject (vtable 0x005ae5f8) slot +0xdc: drops the references +0xbd0 and +0xbd4 and
// clears them, then a tail jump to 0x00486bf0 (the base method).
uint32_t MolmerObject_Remove(uint32_t ebx)
{
    if (D(ebx + 0xbd0))
    {
        Unref(D(ebx + 0xbd0));
        D(ebx + 0xbd0) = 0;
    }
    if (D(ebx + 0xbd4))
    {
        Unref(D(ebx + 0xbd4));
        D(ebx + 0xbd4) = 0;
    }
    return orig::fn_00486bf0(ebx);
}
static uint32_t MolmerObject_Remove_Abi(uint32_t ecx)
{
    return MolmerObject_Remove(ecx);
}
ABI_ENTRY_0044e6c0(MolmerObject_Remove_Entry, MolmerObject_Remove_Abi)
HOOK(0x0044e6c0, "MolmerObject_Remove", MolmerObject_Remove_Entry, nullptr);

// 0x0044f160 (ecx = molmer; stack: 4 arguments; ret 0x10). MolmerObject slot +0x194: Game+0xb0c = the float at
// 0x00604a98, then a tail jump to 0x004284e0 with the same arguments.
uint32_t fn_0044f160(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    D(GameP() + 0xb0c) = D(0x00604a98);
    return orig::fn_004284e0(ecx, a0, a1, a2, a3);
}
static uint32_t fn_0044f160_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_0044f160(ecx, a0, a1, a2, a3);
}
ABI_ENTRY_0044f160(fn_0044f160_Entry, fn_0044f160_Abi)
HOOK(0x0044f160, "fn_0044f160", fn_0044f160_Entry, nullptr);

// 0x0044f180 (ecx = molmer; stack: state; ret 4). MolmerObject slot +0x1b8 (GenericAI_SetState 0x004262c0 is the
// base): when +0xbd4 is held, 0x00429a40 (al = 1, upper eax bytes from +0xbd4 as the original leaves them; ecx =
// molmer), vt+0xdc of it when the molmer's vt+0xd8 is true, then drops and clears it; then GenericAI_SetState(state).
uint32_t MolmerObject_SetState(uint32_t ebx, uint32_t state)
{
    const uint32_t esi = ebx + 0xbd4;
    const uint32_t held = D(esi);
    if (held)
    {
        orig::fn_00429a40((held & 0xffffff00u) | 1u, ebx);
        if (static_cast<uint8_t>(VC(ebx, 0xd8)))
            VC(D(esi), 0xdc);
        Unref(D(esi));
        D(esi) = 0;
    }
    return orig::fn_004262c0(ebx, state);
}
static uint32_t MolmerObject_SetState_Abi(uint32_t ecx, uint32_t a0)
{
    return MolmerObject_SetState(ecx, a0);
}
ABI_ENTRY_0044f180(MolmerObject_SetState_Entry, MolmerObject_SetState_Abi)
HOOK(0x0044f180, "MolmerObject_SetState", MolmerObject_SetState_Entry, nullptr);

// 0x004509a0 (ecx = object; stack: enable (byte), mode; ret 8). Slot +0xfc of DefaultGameObject (vtables 0x005a2bf0,
// 0x005ab650) and RocketTowerBuilding (0x005a87c0); the base is Object_MarkGrid 0x004552f0. The mode argument is
// replaced: 1 when +0x4d0 bit 14 is set (then only enabling does anything), else 2. Objects for which 0x004531e0 is
// false use the base. Otherwise the object marks only the grid cell under its position (vt+0x54): enabling (after a
// vt+0xfc(0, mode) reset when +0x4d0 bit 11 is already set) sets bit 11, finds the cell (0x0046f320 into +0x4ec /
// +0x4f0), and if 0x0046f2e0 accepts it sets bit 10 and marks it (0x0046ece0(..., 1)); disabling clears bit 11 and,
// when bit 10 was set, clears it and unmarks the cell (0x0046ece0(..., 0)).
uint32_t DefaultGameObject_MarkGrid(uint32_t edi, uint32_t a0, uint32_t a1)
{
    uint32_t L[3];  // [esp+0x10] position
    uint32_t mode = a1;
    const uint8_t enable = static_cast<uint8_t>(a0);
    uint32_t esi;
    if (D(edi + 0x4d0) & 0x4000)
    {
        if (!enable)
            return D(edi + 0x4d0);
        esi = 1;
        mode = 1;
    }
    else
    {
        mode = 2;
        esi = mode;
    }
    if (!static_cast<uint8_t>(orig::fn_004531e0(edi)))
        return Object_MarkGrid(edi, a0, esi);
    if ((D(edi + 0x4d0) & 0x800) && enable)
        VC(edi, 0xfc, 0u, esi);
    VC(edi, 0x54, u(L));
    if (enable)
    {
        D(edi + 0x4d0) |= 0x800;
        const uint32_t grid = D(GameP() + 0xaa4);
        if (!static_cast<uint8_t>(fn_0046f320(grid, L[0], L[1], edi + 0x4ec, edi + 0x4f0)))
            return 0;
        if (!static_cast<uint8_t>(fn_0046f2e0(D(GameP() + 0xaa4), D(edi + 0x4ec), D(edi + 0x4f0))))
            return 0;
        D(edi + 0x4d0) |= 0x400;
        return fn_0046ece0(D(edi + 0x4f0), D(edi + 0x4ec), mode, D(GameP() + 0xaa4), 1);
    }
    const uint32_t flags = D(edi + 0x4d0) & 0xfffff7ffu;
    D(edi + 0x4d0) = flags;
    if (!(flags & 0x400))
        return flags;
    D(edi + 0x4d0) = flags & 0xfffffbffu;
    return fn_0046ece0(D(edi + 0x4f0), D(edi + 0x4ec), esi, D(GameP() + 0xaa4), 0);
}
static uint32_t DefaultGameObject_MarkGrid_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return DefaultGameObject_MarkGrid(ecx, a0, a1);
}
ABI_ENTRY_004509a0(DefaultGameObject_MarkGrid_Entry, DefaultGameObject_MarkGrid_Abi)
HOOK(0x004509a0, "DefaultGameObject_MarkGrid", DefaultGameObject_MarkGrid_Entry, nullptr);

// 0x004548f0 (ecx = object; stack: 5 arguments; ret 0x14; returns al = 1). Object base slot +0xa8 (CameraPathObject
// vtable 0x005a0f18 and many others): when the object has a grid route (+0x428) and the first argument is set and
// 0x004531e0 (ecx = that argument) is false, 0x0046f480 (eax = object, ecx = grid Game+0xaa4). Executed in recorded
// runs. The upper eax bytes are those of the last value computed.
uint32_t fn_004548f0(uint32_t esi, uint32_t a0, uint32_t /*a1*/, uint32_t /*a2*/, uint32_t /*a3*/, uint32_t /*a4*/)
{
    uint32_t eax = D(esi + 0x428);
    if (eax && a0)
    {
        eax = orig::fn_004531e0(a0);
        if (!static_cast<uint8_t>(eax))
            eax = fn_0046f480(esi, D(GameP() + 0xaa4));
    }
    return (eax & 0xffffff00u) | 1u;
}
static uint32_t fn_004548f0_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    return fn_004548f0(ecx, a0, a1, a2, a3, a4);
}
ABI_ENTRY_004548f0(fn_004548f0_Entry, fn_004548f0_Abi)
HOOK(0x004548f0, "fn_004548f0", fn_004548f0_Entry, nullptr);

namespace
{
inline uint32_t fb(float f)
{
    uint32_t v;
    std::memcpy(&v, &f, 4);
    return v;
}
// 1 / the body's +0x2c (guess: mass), or 0 when it is 0 (fcom; test ah, 0x44; jp).
inline float InvOrZero(uint32_t body)
{
    const float m = Fl(body + 0x2c);
    return m != K(0x0056c05c) ? K(0x005bcb2c) / m : K(0x0056c05c);
}
// 0x00471970 (eax, ecx, edx, esi; stack: 4 arguments, caller pops): scales the vector edx - eax into ecx and returns
// the distance in st0 (the ABI table lists eax; the callers pop st0). Copied from src/ai/ai_06.cpp.
float Call_00471970(uint32_t eax_, uint32_t ecx_, uint32_t edx_, uint32_t esi_, uint32_t a0, uint32_t a1, uint32_t a2,
                    uint32_t a3)
{
    static const uint32_t target = 0x00471970;
    float r;
    __asm {
        push ebx
        push esi
        push edi
        push ebp
        push a3
        push a2
        push a1
        push a0
        mov eax, eax_
        mov ecx, ecx_
        mov edx, edx_
        mov esi, esi_
        call dword ptr [target]
        add esp, 16
        pop ebp
        pop edi
        pop esi
        pop ebx
        fstp r
    }
    return r;
}
}  // namespace

// 0x00454cc0 (esi = object, edi = &handle; returns eax edx). Executed in recorded runs (GenericAI_Update and many AI
// states, always with edi = object+0x398). Damps the object's spin: from its angular velocity (Body_getAngularVelocity
// 0x00457fc0) toward zero, 0x00471750 (target 0, current, out; 0, inv * 20000 * 40, inv * 20000 with inv = 1 /
// body+0x2c) gives a gain, and 0x00455b10 (eax = out, edx = 0x10; object, *handle, gain, -1, 0) replaces the handle
// (guess: a physics controller). edx is returned as 0 (the original leaves 0x00455b10's edx).
uint64_t fn_00454cc0(uint32_t esi, uint32_t edi)
{
    uint32_t F[0x28 / 4];
    orig::fn_00457fc0(esi, u(&F[0x10 / 4]));
    const uint32_t body = D(D(esi + 0x1c4) + 8);
    const float a = InvOrZero(body);
    F[0x4 / 4] = 0;
    F[0x8 / 4] = 0;
    F[0xc / 4] = 0;
    const float b = InvOrZero(body);
    const float arg2 = a * K(0x005bcd34);
    const float arg1 = b * K(0x005bcd34) * K(0x005bcb30);
    const float gain = orig::fn_00471750(u(&F[0x4 / 4]), u(&F[0x10 / 4]), u(&F[0x1c / 4]), 0, fb(arg1), fb(arg2));
    F[0] = fb(gain);
    const uint32_t h = orig::fn_00455b10(u(&F[0x1c / 4]), 0x10, esi, D(edi), F[0], 0xffffffffu, 0);
    D(edi) = h;
    return h;
}
static uint64_t fn_00454cc0_Abi(uint32_t esi, uint32_t edi)
{
    return fn_00454cc0(esi, edi);
}
ABI_ENTRY_00454cc0(fn_00454cc0_Entry, fn_00454cc0_Abi)
HOOK(0x00454cc0, "fn_00454cc0", fn_00454cc0_Entry, nullptr);

// 0x00454d80 (eax = target point, edi = object; stack: 4 arguments (the fourth is not read); ret 0x10). Executed in
// recorded runs (GenericAI_Update, 0x00470910). Pushes the object toward the target: own position (vt+0x54) with z
// replaced by the target's, 0x00471970 (position, out, target; esi = 0; a0, a1, a2, 0.2) gives the wanted velocity
// (its st0 is dropped) whose z is the current linear velocity's z (Body_getLinearVelocity 0x00458000) when that is not
// positive, else 0; 0x00471750 (wanted, current, out; 0, mass * 50, mass with Body_GetMass 0x004581c0) gives a gain,
// written into the a2 stack slot, and 0x00455a30 (eax = out, edx = 0x10; object, +0x39c, gain, -1, 0) replaces the
// handle +0x39c.
uint32_t fn_00454d80(uint32_t eax, uint32_t edi, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t /*a3*/)
{
    uint32_t F[0x30 / 4];
    uint32_t a2slot = a2;
    VC(edi, 0x54, u(&F[0x18 / 4]));
    orig::fn_00458000(edi, u(&F[0xc / 4]));
    F[0x20 / 4] = D(eax + 8);
    Call_00471970(u(&F[0x18 / 4]), u(&F[0]), eax, 0, a0, a1, a2slot, 0x3e4ccccd);
    F[0x8 / 4] = Fl(u(&F[0x14 / 4])) > K(0x0056c05c) ? 0 : F[0x14 / 4];
    const float mass = orig::fn_004581c0(edi);
    const float massK = orig::fn_004581c0(edi) * K(0x005bcd40);
    a2slot = fb(orig::fn_00471750(u(&F[0]), u(&F[0xc / 4]), u(&F[0x24 / 4]), 0, fb(massK), fb(mass)));
    const uint32_t h = orig::fn_00455a30(u(&F[0x24 / 4]), 0x10, edi, D(edi + 0x39c), a2slot, 0xffffffffu, 0);
    D(edi + 0x39c) = h;
    return h;
}
static uint32_t fn_00454d80_Abi(uint32_t eax, uint32_t edi, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_00454d80(eax, edi, a0, a1, a2, a3);
}
ABI_ENTRY_00454d80(fn_00454d80_Entry, fn_00454d80_Abi)
HOOK(0x00454d80, "fn_00454d80", fn_00454d80_Entry, nullptr);

// 0x00454e60 (ebx = direction, edi = object; stack: turn rate (float); ret 4; returns al). Executed in recorded runs.
// Turns the object about z toward the direction's xy: when the xy length exceeds 0.001, n = the normalised direction,
// h = the object's heading (0x00453150); dot = h.n; the wanted yaw rate is rate * (dot < -0.98 ? 1 : n.y * h.x - h.y *
// n.x) about z; 0x00471750 (wanted, angular velocity, out; 0, inv * 20000 * 10, inv * 20000, inv = 1 / body+0x2c)
// gives a gain and 0x00455b10 (eax = out; object, +0x398, gain, -1, 0) replaces the handle +0x398. Returns 1 when the
// length is too small or dot >= 0.98 (facing), else 0.
uint32_t fn_00454e60(uint32_t ebx, uint32_t edi, uint32_t a0)
{
    uint32_t F[0x28 / 4];
    float* f = reinterpret_cast<float*>(F);
    orig::fn_00453150(edi, u(&F[0x4 / 4]));  // heading: F[1], F[2]
    const float dy = Fl(ebx + 4);
    const float dx = Fl(ebx);
    const float len = std::sqrt(dx * dx + dy * dy);
    if (!(len > K(0x005bcd3c)))
        return 1;
    const float inv = K(0x005bcb2c) / len;
    f[0x10 / 4] = inv * Fl(ebx);
    const float ny = inv * Fl(ebx + 4);
    const float dot = f[0x8 / 4] * ny + f[0x4 / 4] * f[0x10 / 4];
    f[0] = dot;
    if (dot < K(0x005bcd38))
        F[0x18 / 4] = 0x3f800000;
    else
        f[0x18 / 4] = ny * f[0x4 / 4] - f[0x8 / 4] * f[0x10 / 4];
    orig::fn_00457fc0(edi, u(&F[0x1c / 4]));
    float rate;
    std::memcpy(&rate, &a0, 4);
    const uint32_t body = D(D(edi + 0x1c4) + 8);
    F[0x4 / 4] = 0;
    f[0xc / 4] = f[0x18 / 4] * rate;
    F[0x8 / 4] = 0;
    const float a = InvOrZero(body);
    const float b = InvOrZero(body);
    const float arg2 = a * K(0x005bcd34);
    const float arg1 = b * K(0x005bcd34) * K(0x005bcb68);
    const float gain = orig::fn_00471750(u(&F[0x4 / 4]), u(&F[0x1c / 4]), u(&F[0x10 / 4]), 0, fb(arg1), fb(arg2));
    const uint32_t h = orig::fn_00455b10(u(&F[0x10 / 4]), 0x10, edi, D(edi + 0x398), fb(gain), 0xffffffffu, 0);
    D(edi + 0x398) = h;
    return f[0] >= K(0x005bcd30) ? 1u : 0u;  // fcomp; test ah, 1: unordered gives 0
}
static uint32_t fn_00454e60_Abi(uint32_t ebx, uint32_t edi, uint32_t a0)
{
    return fn_00454e60(ebx, edi, a0);
}
ABI_ENTRY_00454e60(fn_00454e60_Entry, fn_00454e60_Abi)
HOOK(0x00454e60, "fn_00454e60", fn_00454e60_Entry, nullptr);

// 0x004592e0 (eax = list or 0, ecx = tag or 0; stack: object; ret 4). Executed in recorded runs (many callers, e.g.
// the marker "alarm_tag" lookup in 0x0044ba30). The first object of the list (default (Game+0xa84)+0xe70; 0 when that
// is empty) other than the given object whose tag (0x00453290) equals the tag (default the object's own +0x390). The
// tag is read into the method's own argument slot.
uint32_t Object_FindByTag(uint32_t list, uint32_t tag, uint32_t from)
{
    if (!list)
    {
        list = D(D(GameP() + 0xa84) + 0xe70);
        if (!list)
            return 0;
    }
    uint32_t it[2] = {0, 0};  // [esp+0xc]
    if (!tag)
        tag = D(from + 0x390);
    uint32_t slot = from;  // [esp+0x1c], the argument slot
    for (uint32_t o = orig::fn_004e7340(list, u(it)); o; o = orig::fn_004e73c0(u(it)))
    {
        if (o == from)
            continue;
        if (static_cast<uint8_t>(orig::fn_00453290(o, u(&slot))) && slot == tag)
        {
            const uint32_t node = it[1];
            VC(it[0], 0xc, node);
            --I(node + 0x18);
            return o;
        }
    }
    if (it[1])
        --I(it[1] + 0x18);
    return 0;
}
static uint32_t Object_FindByTag_Abi(uint32_t eax, uint32_t ecx, uint32_t a0)
{
    return Object_FindByTag(eax, ecx, a0);
}
ABI_ENTRY_004592e0(Object_FindByTag_Entry, Object_FindByTag_Abi)
HOOK(0x004592e0, "Object_FindByTag", Object_FindByTag_Entry, nullptr);

// 0x00459390 (eax = list or 0; stack: object, list out, include self (byte), tag, callback; ret 0x14). Caller
// 0x0043d180. Counts the objects of the list (default (Game+0xa84)+0xe70) whose tag (0x00453290) equals the tag
// (default the object's +0x390), skipping the object itself unless asked; when the out pointer is set it first creates
// a list there (PMS_heapAlloc 0x004e07f0(0x10, 0x18, 2, "new"), 0x004e7010 (eax = Game+0xac4, edx = 0)) and adds each
// match (vt+0x18); the cdecl callback, when set, is called with each match. The count lives in the out-pointer's
// argument slot.
uint32_t ObjectList_CollectByTag(uint32_t list, uint32_t from, uint32_t out, uint32_t self, uint32_t tag,
                                 uint32_t callback)
{
    if (!list)
    {
        list = D(D(GameP() + 0xa84) + 0xe70);
        if (!list)
            return 0;
    }
    uint32_t tagOut = 0;      // [esp+0xc] (not initialised by the original)
    uint32_t it[2] = {0, 0};  // [esp+0x10]
    uint32_t count = 0;       // the out argument's slot
    if (!tag)
        tag = D(from + 0x390);
    if (out)
    {
        const uint32_t mem = orig::fn_004e07f0(0x0063a210, 0x10, 0x18, 2, 0x0058b5e8);
        D(out) = mem ? orig::fn_004e7010(D(GameP() + 0xac4), 0, mem) : 0;
    }
    for (uint32_t o = orig::fn_004e7340(list, u(it)); o; o = orig::fn_004e73c0(u(it)))
    {
        if (o == from && !static_cast<uint8_t>(self))
            continue;
        if (!static_cast<uint8_t>(orig::fn_00453290(o, u(&tagOut))) || tagOut != tag)
            continue;
        ++count;
        if (out)
            VC(D(out), 0x18, o);
        if (callback)
            reinterpret_cast<void(__cdecl*)(uint32_t)>(callback)(o);
    }
    if (it[1])
        --I(it[1] + 0x18);
    return count;
}
static uint32_t ObjectList_CollectByTag_Abi(uint32_t eax, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3,
                                            uint32_t a4)
{
    return ObjectList_CollectByTag(eax, a0, a1, a2, a3, a4);
}
ABI_ENTRY_00459390(ObjectList_CollectByTag_Entry, ObjectList_CollectByTag_Abi)
HOOK(0x00459390, "ObjectList_CollectByTag", ObjectList_CollectByTag_Entry, nullptr);
