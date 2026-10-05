// Recovered (static, batch game_04): ObjectSystem_CreateObject (the class-id factory) and the constructors it
// calls, Object_SendTrigger, the one-instruction class descriptor getters (vtable slot 1) and constant/float
// getters of many object classes, the pub objective markers (0x00466e70, 0x00469xxx), PubObject / mission object
// Load methods and PickupGenerator / PickupObject virtuals. Compile-checked only (the game was not run); the oracle
// (tools/compare.py) is still to do. Functions the recorded runs never execute are marked "not executed in recorded
// runs". Vtable slots come from a scan of .rdata in game.unpacked.exe (vtable starts = constructor stores
// `mov [reg], imm32`); class names from the vtable's slot 1 descriptor (+0x14 "CLASS(Name)").
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
uint32_t f2u(float f)
{
    uint32_t v;
    std::memcpy(&v, &f, 4);
    return v;
}
void* P(uint32_t v)
{
    return reinterpret_cast<void*>(v);
}
float& F(void* p, uint32_t off)
{
    return Field<float>(p, off);
}
uint32_t& D(void* p, uint32_t off)
{
    return Field<uint32_t>(p, off);
}
uint8_t& B(void* p, uint32_t off)
{
    return Field<uint8_t>(p, off);
}
void* G()
{
    return GameGlobal<void*>(0x005fe490);
}

// Game float constants (read in place, as the original does).
inline const float& kZero = GameGlobal<float>(0x0056c05c);       // 0.0
inline const float& kOne = GameGlobal<float>(0x005bcb2c);        // 1.0
inline const float& kFrameTime = GameGlobal<float>(0x00604a90);  // frame time step

// x87 sine / cosine of a float (fsin / fcos at the game's precision), as the originals compute them.
float X87Sin(float a)
{
    float r;
    __asm {
        fld a
        fsin
        fstp r
    }
    return r;
}
float X87Cos(float a)
{
    float r;
    __asm {
        fld a
        fcos
        fstp r
    }
    return r;
}
// The CRT's _CIpow 0x00566790 (x87 register convention: st1 = base, st0 = exponent; result in st0).
float CIpow(float base, float exponent)
{
    static const uint32_t target = 0x00566790;
    float r;
    __asm {
        push ebx
        push esi
        push edi
        fld base
        fld exponent
        call dword ptr [target]
        fstp r
        pop edi
        pop esi
        pop ebx
    }
    return r;
}

// Releases the object reference at *slot (refcount +0x28): at 0 and when its vt+0xd8() is false it is destroyed
// (0x00487da0, eax = object); *slot becomes 0. The inlined pattern of 0x00471c50 / 0x00471cb0 / 0x00472030 /
// 0x00472080.
void ReleaseRef(uint32_t* slot)
{
    void* o = P(*slot);
    if (--Field<int32_t>(o, 0x28) == 0 && !static_cast<uint8_t>(VCall<uint32_t>(o, 0xd8 / 4)))
        orig::fn_00487da0(u(o));
    if (slot)
        *slot = 0;
}
}  // namespace

// Functions recovered later in this file, called directly by earlier ones.
uint32_t ObjectSystem_CreateObject(uint32_t name, uint32_t templateName, uint32_t a0, uint32_t a1, uint32_t a2,
                                   uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7, uint32_t a8);
uint32_t Object_SendTrigger(void* sys, uint32_t trigger, void* sender);
uint32_t fn_004694e0(void* owner);
uint32_t fn_0046a650(void* obj, uint32_t* out);
uint32_t fn_0046a720(void* obj);

// ---------------------------------------------------------------------------------------------------------------
// Constructors called by ObjectSystem_CreateObject 0x0045f020 (each on freshly allocated memory).

// 0x004614f0 (edx = memory). Twiglet constructor (vtable 0x005af020 "CLASS(Twiglet)"): base object constructor
// 0x004511d0 (edx), vtable, then 0x00406400 (eax = +0x498) clears the embedded block at +0x498. Returns the object.
// Not executed in recorded runs.
uint32_t Twiglet_Construct(void* obj)
{
    orig::fn_004511d0(u(obj));
    D(obj, 0) = 0x005af020;
    orig::fn_00406400(u(obj) + 0x498);
    return u(obj);
}
static uint32_t Twiglet_Construct_Abi(uint32_t edx)
{
    return Twiglet_Construct(P(edx));
}
ABI_ENTRY_004614f0(Twiglet_Construct_Entry, Twiglet_Construct_Abi)
HOOK(0x004614f0, "Twiglet_Construct", Twiglet_Construct_Entry, nullptr);

// The GenericAIObject-derived constructors 0x00461510..0x00461820 (eax = memory, ecx passed through): each is
// GenericAIObject_Construct 0x00437290 (ecx, ebx = memory) followed by the class vtable store. Returns the object.
static uint32_t AIConstruct(uint32_t ecx, void* obj, uint32_t vtable)
{
    orig::fn_00437290(ecx, u(obj));
    D(obj, 0) = vtable;
    return u(obj);
}

#define AI_CTOR(addr, Name, vtable)                                                                              \
    uint32_t Name(uint32_t ecx, void* obj)                                                                             \
    {                                                                                                                  \
        return AIConstruct(ecx, obj, vtable);                                                                          \
    }                                                                                                                  \
    static uint32_t Name##_Abi(uint32_t eax, uint32_t ecx)                                                             \
    {                                                                                                                  \
        return Name(ecx, P(eax));                                                                                      \
    }                                                                                                                  \
    ABI_ENTRY_##addr(Name##_Entry, Name##_Abi)

// 0x00461510 GruntObject (vtable 0x005a9cf0).
AI_CTOR(00461510, GruntObject_Construct, 0x005a9cf0)
HOOK(0x00461510, "GruntObject_Construct", GruntObject_Construct_Entry, nullptr);
// 0x00461530 BowGruntObject (vtable 0x005abb88).
AI_CTOR(00461530, BowGruntObject_Construct, 0x005abb88)
HOOK(0x00461530, "BowGruntObject_Construct", BowGruntObject_Construct_Entry, nullptr);
// 0x00461550 CaptainObject (vtable 0x005acb78). Not executed in recorded runs.
AI_CTOR(00461550, CaptainObject_Construct, 0x005acb78)
HOOK(0x00461550, "CaptainObject_Construct", CaptainObject_Construct_Entry, nullptr);
// 0x00461570 SniperCaptain (vtable 0x005ae868). Not executed in recorded runs.
AI_CTOR(00461570, SniperCaptain_Construct, 0x005ae868)
HOOK(0x00461570, "SniperCaptain_Construct", SniperCaptain_Construct_Entry, nullptr);
// 0x00461590 Q112Object (vtable 0x005a2440). Not executed in recorded runs.
AI_CTOR(00461590, Q112Object_Construct, 0x005a2440)
HOOK(0x00461590, "Q112Object_Construct", Q112Object_Construct_Entry, nullptr);
// 0x004615b0 Q112Boss (vtable 0x005a4098). Not executed in recorded runs.
AI_CTOR(004615b0, Q112Boss_Construct, 0x005a4098)
HOOK(0x004615b0, "Q112Boss_Construct", Q112Boss_Construct_Entry, nullptr);
// 0x004615d0 PeasantGenAI (vtable 0x005aa9c0).
AI_CTOR(004615d0, PeasantGenAI_Construct, 0x005aa9c0)
HOOK(0x004615d0, "PeasantGenAI_Construct", PeasantGenAI_Construct_Entry, nullptr);
// 0x004615f0 FakeGruntObject (vtable 0x005a4da8).
AI_CTOR(004615f0, FakeGruntObject_Construct, 0x005a4da8)
HOOK(0x004615f0, "FakeGruntObject_Construct", FakeGruntObject_Construct_Entry, nullptr);
// 0x00461610 GruntLadderObject (vtable 0x005a3528). Not executed in recorded runs.
AI_CTOR(00461610, GruntLadderObject_Construct, 0x005a3528)
HOOK(0x00461610, "GruntLadderObject_Construct", GruntLadderObject_Construct_Entry, nullptr);
// 0x00461630 ColossusObject (vtable 0x005aa238). Not executed in recorded runs.
AI_CTOR(00461630, ColossusObject_Construct, 0x005aa238)
HOOK(0x00461630, "ColossusObject_Construct", ColossusObject_Construct_Entry, nullptr);
// 0x00461650 GoliathObject (vtable 0x005af298).
AI_CTOR(00461650, GoliathObject_Construct, 0x005af298)
HOOK(0x00461650, "GoliathObject_Construct", GoliathObject_Construct_Entry, nullptr);
// 0x00461670 Polewart (vtable 0x005a7328). Not executed in recorded runs.
AI_CTOR(00461670, Polewart_Construct, 0x005a7328)
HOOK(0x00461670, "Polewart_Construct", Polewart_Construct_Entry, nullptr);
// 0x00461690 GoliathFireball (vtable 0x005af8c0). Not executed in recorded runs.
AI_CTOR(00461690, GoliathFireball_Construct, 0x005af8c0)
HOOK(0x00461690, "GoliathFireball_Construct", GoliathFireball_Construct_Entry, nullptr);
// 0x004616b0 MonkObject (vtable 0x005aac28). Not executed in recorded runs.
AI_CTOR(004616b0, MonkObject_Construct, 0x005aac28)
HOOK(0x004616b0, "MonkObject_Construct", MonkObject_Construct_Entry, nullptr);
// 0x004616d0 GenericAIObject (vtable 0x005a3790). Not executed in recorded runs.
AI_CTOR(004616d0, GenericAIObject_ConstructFull, 0x005a3790)
HOOK(0x004616d0, "GenericAIObject_ConstructFull", GenericAIObject_ConstructFull_Entry, nullptr);
// 0x004616f0 ZeppelinObject (vtable 0x005af500). Not executed in recorded runs.
AI_CTOR(004616f0, ZeppelinObject_Construct, 0x005af500)
HOOK(0x004616f0, "ZeppelinObject_Construct", ZeppelinObject_Construct_Entry, nullptr);
// 0x00461710 SaucerObject (vtable 0x005a5e98). Not executed in recorded runs.
AI_CTOR(00461710, SaucerObject_Construct, 0x005a5e98)
HOOK(0x00461710, "SaucerObject_Construct", SaucerObject_Construct_Entry, nullptr);
// 0x00461730 SheepObject (vtable 0x005a8d00).
AI_CTOR(00461730, SheepObject_Construct, 0x005a8d00)
HOOK(0x00461730, "SheepObject_Construct", SheepObject_Construct_Entry, nullptr);
// 0x00461750 PenguinObject (vtable 0x005a1768).
AI_CTOR(00461750, PenguinObject_Construct, 0x005a1768)
HOOK(0x00461750, "PenguinObject_Construct", PenguinObject_Construct_Entry, nullptr);
// 0x00461770 SquadObject (vtable 0x005ab3e8). Not executed in recorded runs.
AI_CTOR(00461770, SquadObject_Construct, 0x005ab3e8)
HOOK(0x00461770, "SquadObject_Construct", SquadObject_Construct_Entry, nullptr);
// 0x004617c0 ICObject (vtable 0x005a1b60).
AI_CTOR(004617c0, ICObject_Construct, 0x005a1b60)
HOOK(0x004617c0, "ICObject_Construct", ICObject_Construct_Entry, nullptr);
// 0x004617e0 ShrubPatrol (vtable 0x005a7e98).
AI_CTOR(004617e0, ShrubPatrol_Construct, 0x005a7e98)
HOOK(0x004617e0, "ShrubPatrol_Construct", ShrubPatrol_Construct_Entry, nullptr);
// 0x00461800 MolmerObject (vtable 0x005ae5f8).
AI_CTOR(00461800, MolmerObject_Construct, 0x005ae5f8)
HOOK(0x00461800, "MolmerObject_Construct", MolmerObject_Construct_Entry, nullptr);
// 0x00461820 Q111Object (vtable 0x005a6df8).
AI_CTOR(00461820, Q111Object_Construct, 0x005a6df8)
HOOK(0x00461820, "Q111Object_Construct", Q111Object_Construct_Entry, nullptr);

// 0x00461790 (edx = memory). Constructor of the class with vtable 0x005a68b8 (its slot 1 returns 0: no class name;
// class id 98 in ObjectSystem_CreateObject): base constructor 0x004511d0, vtable, then the bytes +0x518 and +0x530
// (two 0x18-byte records) = 0. Returns the object.
uint32_t fn_00461790(void* obj)
{
    orig::fn_004511d0(u(obj));
    D(obj, 0) = 0x005a68b8;
    uint8_t* p = static_cast<uint8_t*>(obj) + 0x518;
    for (int i = 2; i != 0; --i, p += 0x18)
        *p = 0;
    return u(obj);
}
static uint32_t fn_00461790_Abi(uint32_t edx)
{
    return fn_00461790(P(edx));
}
ABI_ENTRY_00461790(fn_00461790_Entry, fn_00461790_Abi)
HOOK(0x00461790, "fn_00461790", fn_00461790_Entry, nullptr);

// 0x00461840 (edi = memory). PlayerObject constructor (vtable 0x005aa888 "CLASS(PlayerObject)"): base constructor
// 0x004511d0 (edx = object), vtable, 0x00406400 (eax = +0xb08), then 0x004a0d00 (edx = object, as left by
// 0x004511d0; esi = +0xc24). Returns the object.
uint32_t PlayerObject_Construct(void* obj)
{
    orig::fn_004511d0(u(obj));
    D(obj, 0) = 0x005aa888;
    orig::fn_00406400(u(obj) + 0xb08);
    orig::fn_004a0d00(u(obj), u(obj) + 0xc24);
    return u(obj);
}
static uint32_t PlayerObject_Construct_Abi(uint32_t edi)
{
    return PlayerObject_Construct(P(edi));
}
ABI_ENTRY_00461840(PlayerObject_Construct_Entry, PlayerObject_Construct_Abi)
HOOK(0x00461840, "PlayerObject_Construct", PlayerObject_Construct_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// 0x0045f020 ObjectSystem_CreateObject: the class-id factory.

namespace
{
enum CaseKind : uint8_t
{
    K_NULL,   // returns 0 at once
    K_PLAIN,  // base constructor 0x004511d0 + vtable store
    K_CTOR,   // a dedicated constructor
};
struct CreateCase
{
    CaseKind kind;
    uint16_t size;
    uint32_t arg;  // K_PLAIN: vtable; K_CTOR: constructor address
};
// Class ids 1..0xaa (jump table 0x00461240): allocation size and vtable / constructor. Class names from the
// vtables' slot 1 descriptors (e.g. 25..42 PickupObject, 43..48 PlayerWeapon, 135 CameraPathObject).
const CreateCase kCreateCases[0xaa] = {
    {K_PLAIN, 0x588, 0x005a4438},  // 1
    {K_PLAIN, 0x588, 0x005a3dd0},  // 2
    {K_PLAIN, 0x588, 0x005aec10},  // 3
    {K_PLAIN, 0x58c, 0x005a9620},  // 4
    {K_PLAIN, 0x588, 0x005ac790},  // 5
    {K_PLAIN, 0x588, 0x005ad190},  // 6
    {K_PLAIN, 0x588, 0x005a4c38},  // 7
    {K_PLAIN, 0x5b4, 0x005a5d28},  // 8
    {K_PLAIN, 0x598, 0x005afc80},  // 9
    {K_PLAIN, 0x5dc, 0x005a9a40},  // 10
    {K_PLAIN, 0x58c, 0x005a33b8},  // 11
    {K_PLAIN, 0x588, 0x005adc78},  // 12
    {K_PLAIN, 0x588, 0x005ab120},  // 13
    {K_PLAIN, 0x588, 0x005a8100},  // 14
    {K_PLAIN, 0x66c, 0x005a83e0},  // 15
    {K_PLAIN, 0x588, 0x005a5010},  // 16
    {K_PLAIN, 0x59c, 0x005a11d8},  // 17
    {K_PLAIN, 0x5a4, 0x005aba18},  // 18
    {K_PLAIN, 0x58c, 0x005a7958},  // 19
    {K_PLAIN, 0x588, 0x005a6608},  // 20
    {K_PLAIN, 0x5a4, 0x005ad858},  // 21
    {K_PLAIN, 0x590, 0x005a2940},  // 22
    {K_PLAIN, 0x588, 0x005ac4b8},  // 23
    {K_PLAIN, 0x58c, 0x005a9218},  // 24
    {K_PLAIN, 0x504, 0x005abf48},  // 25
    {K_PLAIN, 0x504, 0x005a94e0},  // 26
    {K_PLAIN, 0x504, 0x005a5be8},  // 27
    {K_PLAIN, 0x504, 0x005a3120},  // 28
    {K_PLAIN, 0x504, 0x005aead0},  // 29
    {K_PLAIN, 0x504, 0x005a5970},  // 30
    {K_PLAIN, 0x504, 0x005a2ab0},  // 31
    {K_PLAIN, 0x504, 0x005ac900},  // 32
    {K_PLAIN, 0x504, 0x005a9790},  // 33
    {K_PLAIN, 0x504, 0x005ae4b8},  // 34
    {K_PLAIN, 0x504, 0x005a6390},  // 35
    {K_PLAIN, 0x504, 0x005a3b50},  // 36
    {K_PLAIN, 0x504, 0x005af158},  // 37
    {K_PLAIN, 0x524, 0x005ab650},  // 38
    {K_PLAIN, 0x504, 0x005acf18},  // 39
    {K_PLAIN, 0x504, 0x005a9bb0},  // 40
    {K_PLAIN, 0x504, 0x005a6778},  // 41
    {K_PLAIN, 0x504, 0x005a3c90},  // 42
    {K_PLAIN, 0x564, 0x005ae210},  // 43
    {K_PLAIN, 0x564, 0x005ac210},  // 44
    {K_PLAIN, 0x564, 0x005a1348},  // 45
    {K_PLAIN, 0x564, 0x005ad458},  // 46
    {K_PLAIN, 0x564, 0x005ae210},  // 47
    {K_PLAIN, 0x564, 0x005a4858},  // 48
    {K_PLAIN, 0x56c, 0x005a90a8},  // 49
    {K_PLAIN, 0x588, 0x005a98d0},  // 50
    {K_PLAIN, 0x494, 0x005a6258},  // 51
    {K_PLAIN, 0x4c4, 0x005ae380},  // 52
    {K_PLAIN, 0x49c, 0x005a8550},  // 53
    {K_PLAIN, 0x480, 0x005a64d0},  // 54
    {K_PLAIN, 0x4d0, 0x005a1dc8},  // 55
    {K_PLAIN, 0x63c, 0x005a52d8},  // 56
    {K_PLAIN, 0x47c, 0x005a7820},  // 57
    {K_CTOR, 0x58c, 0x004614f0},   // 58
    {K_CTOR, 0xb58, 0x00461510},   // 59
    {K_CTOR, 0xb58, 0x00461530},   // 60
    {K_CTOR, 0xb58, 0x00461550},   // 61
    {K_CTOR, 0xb58, 0x00461570},   // 62
    {K_PLAIN, 0x524, 0x005a2bf0},  // 63
    {K_CTOR, 0xb60, 0x00461590},   // 64
    {K_CTOR, 0xb9c, 0x004615b0},   // 65
    {K_PLAIN, 0x4f4, 0x005a2d28},  // 66
    {K_CTOR, 0xb5c, 0x004615d0},   // 67
    {K_CTOR, 0xbfc, 0x004615f0},   // 68
    {K_PLAIN, 0x554, 0x005adf60},  // 69
    {K_CTOR, 0xb58, 0x00461610},   // 70
    {K_CTOR, 0xb9c, 0x00461630},   // 71
    {K_CTOR, 0xb6c, 0x00461650},   // 72
    {K_CTOR, 0xb7c, 0x00461690},   // 73
    {K_CTOR, 0xb5c, 0x00461670},   // 74
    {K_PLAIN, 0x4bc, 0x005ad720},  // 75
    {K_CTOR, 0xb5c, 0x004616b0},   // 76
    {K_PLAIN, 0x848, 0x005a9f58},  // 77
    {K_PLAIN, 0x660, 0x005a19d8},  // 78
    {K_PLAIN, 0x71c, 0x005a1050},  // 79
    {K_PLAIN, 0x72c, 0x005ac088},  // 80
    {K_PLAIN, 0x5f4, 0x005a8b88},  // 81
    {K_PLAIN, 0x528, 0x005a87c0},  // 82
    {K_PLAIN, 0x55c, 0x005adde8},  // 83
    {K_CTOR, 0xb58, 0x004616d0},   // 84
    {K_CTOR, 0xb98, 0x004616f0},   // 85
    {K_CTOR, 0xb98, 0x00461710},   // 86
    {K_CTOR, 0xb64, 0x00461730},   // 87
    {K_CTOR, 0xb64, 0x00461750},   // 88
    {K_CTOR, 0xb80, 0x00461770},   // 89
    {K_PLAIN, 0x490, 0x005a4300},  // 90
    {K_PLAIN, 0x464, 0x005aafe8},  // 91
    {K_PLAIN, 0x49c, 0x005a5ab0},  // 92
    {K_PLAIN, 0x4cc, 0x005a8a50},  // 93
    {K_PLAIN, 0x460, 0x005a8688},  // 94
    {K_PLAIN, 0x464, 0x005ac380},  // 95
    {K_PLAIN, 0x4a0, 0x005a71d0},  // 96
    {K_PLAIN, 0x4a4, 0x005a1f00},  // 97
    {K_CTOR, 0x548, 0x00461790},   // 98
    {K_PLAIN, 0x50c, 0x005ab290},  // 99
    {K_PLAIN, 0x500, 0x005a7ac8},  // 100
    {K_PLAIN, 0x4a0, 0x005a2190},  // 101
    {K_PLAIN, 0x4f8, 0x005a6a10},  // 102
    {K_PLAIN, 0x4f8, 0x005a3f40},  // 103
    {K_PLAIN, 0x4f8, 0x005afb28},  // 104
    {K_PLAIN, 0x4f8, 0x005ad9c8},  // 105
    {K_PLAIN, 0x4f8, 0x005aae90},  // 106
    {K_PLAIN, 0x4f8, 0x005a8288},  // 107
    {K_PLAIN, 0x4f8, 0x005ad5c8},  // 108
    {K_PLAIN, 0x4f8, 0x005ad300},  // 109
    {K_PLAIN, 0x4fc, 0x005aa0e0},  // 110
    {K_PLAIN, 0x500, 0x005a6b68},  // 111
    {K_PLAIN, 0x500, 0x005a39f8},  // 112
    {K_NULL, 0x000, 0x00000000},   // 113
    {K_PLAIN, 0x4f8, 0x005aa4a0},  // 114
    {K_PLAIN, 0x4f8, 0x005afdf0},  // 115
    {K_PLAIN, 0x4f8, 0x005a5568},  // 116
    {K_PLAIN, 0x4a0, 0x005a6100},  // 117
    {K_PLAIN, 0x4a0, 0x005a26a8},  // 118
    {K_PLAIN, 0x4a0, 0x005ae0b8},  // 119
    {K_PLAIN, 0x4a4, 0x005ab8c0},  // 120
    {K_PLAIN, 0x4a0, 0x005a88f8},  // 121
    {K_PLAIN, 0x4b4, 0x005a3260},  // 122
    {K_PLAIN, 0x4a8, 0x005a9388},  // 123
    {K_PLAIN, 0x4a4, 0x005adb20},  // 124
    {K_PLAIN, 0x4a4, 0x005adb20},  // 125
    {K_PLAIN, 0x4a0, 0x005a56c0},  // 126
    {K_PLAIN, 0x4a4, 0x005a5818},  // 127
    {K_PLAIN, 0x4ac, 0x005a22e8},  // 128
    {K_PLAIN, 0x4a0, 0x005a5180},  // 129
    {K_PLAIN, 0x4a4, 0x005a1610},  // 130
    {K_PLAIN, 0x4fc, 0x005a4700},  // 131
    {K_PLAIN, 0x478, 0x005a4b00},  // 132
    {K_PLAIN, 0x50c, 0x005a7d58},  // 133
    {K_PLAIN, 0x810, 0x005a2800},  // 134
    {K_PLAIN, 0x4a4, 0x005a0f18},  // 135
    {K_PLAIN, 0x5cc, 0x005a49c8},  // 136
    {K_PLAIN, 0x460, 0x005acde0},  // 137
    {K_PLAIN, 0x478, 0x005a7c20},  // 138
    {K_PLAIN, 0x5b8, 0x005aeeb8},  // 139
    {K_PLAIN, 0x498, 0x005a2058},  // 140
    {K_PLAIN, 0x5b8, 0x005a7068},  // 141
    {K_PLAIN, 0x470, 0x005ac628},  // 142
    {K_PLAIN, 0x474, 0x005a2e60},  // 143
    {K_CTOR, 0xb58, 0x004617c0},   // 144
    {K_PLAIN, 0x4d4, 0x005aed80},  // 145
    {K_PLAIN, 0x468, 0x005a7590},  // 146
    {K_PLAIN, 0x464, 0x005a6cc0},  // 147
    {K_PLAIN, 0x48c, 0x005ab788},  // 148
    {K_PLAIN, 0x4a0, 0x005a8f70},  // 149
    {K_PLAIN, 0x554, 0x005aa750},  // 150
    {K_PLAIN, 0x488, 0x005ad058},  // 151
    {K_CTOR, 0xb58, 0x004617e0},   // 152
    {K_CTOR, 0xbd8, 0x00461800},   // 153
    {K_CTOR, 0xbd4, 0x00461820},   // 154
    {K_PLAIN, 0x59c, 0x005aca40},  // 155
    {K_CTOR, 0xb58, 0x00437290},   // 156
    {K_NULL, 0x000, 0x00000000},   // 157
    {K_NULL, 0x000, 0x00000000},   // 158
    {K_NULL, 0x000, 0x00000000},   // 159
    {K_NULL, 0x000, 0x00000000},   // 160
    {K_CTOR, 0xe64, 0x00461840},   // 161
    {K_PLAIN, 0x5b0, 0x005a2fc8},  // 162
    {K_PLAIN, 0x598, 0x005abdf0},  // 163
    {K_PLAIN, 0x534, 0x005a76c8},  // 164
    {K_PLAIN, 0x51c, 0x005a14b8},  // 165
    {K_PLAIN, 0x4fc, 0x005a45a8},  // 166
    {K_PLAIN, 0x588, 0x005af768},  // 167
    {K_PLAIN, 0x7b8, 0x005aa5f8},  // 168
    {K_PLAIN, 0x4f8, 0x005a5410},  // 169
    {K_NULL, 0x000, 0x00000000},   // 170
};

// PMS_heapAlloc 0x004e07f0 on the object heap 0x0063a210 (flags 0x10, tag 2, source name 0x0058b5e8).
uint32_t ObjectAlloc(uint32_t size)
{
    return orig::fn_004e07f0(0x0063a210, 0x10, size, 2, 0x0058b5e8);
}

// The save-loader gate read at entry and after Load: game+0xab8 -> +0x14 -> flag byte; bit 1 set and bit 0 clear
// means "restoring from a save" (guess).
bool SaveLoaderRestoring()
{
    const uint8_t* p = Field<const uint8_t*>(Field<void*>(G(), 0xab8), 0x14);
    return p && (*p & 2) && !(*p & 1);
}
}  // namespace

// 0x0045f020 (eax = class name, ecx = template name; stack: system, a1, a2, template, a4, a5, a6, class id, force;
// ret 0x24). Creates a game object. Unless `force` (byte), returns 0 while a save is being restored
// (SaveLoaderRestoring). With a template name, the template is found with 0x0045abe0 (ecx = name, esi = game+0xa84;
// "CreateObject : Unable to find template\n%s" logged when missing) and replaces the template argument; without a
// class name, the template name is copied to a 32-byte buffer and 0x0045dec0 (eax = a4, ecx = "behavior", esi = a5;
// stack: system, buffer) overwrites it with the "behavior" property, falling back to the template's parent
// (template+0x10 -> +0x3c: {eax = [0], esi = [2]}). Any class name is hashed (0x0044ad70) and looked up in
// system+4 (0x00461fc0) to give the class id. The id selects an allocation size and vtable / constructor
// (kCreateCases; ids outside 1..0xaa make a DefaultGameObject, vtable 0x005a2bf0; ids 113, 157-160, 170 return 0;
// id 161 makes the PlayerObject and stores it in game+0x748). Then: word +0x394 = a2, +0x418 = class id, word +0x396
// = a6, +0x414 = template, vt+0x18 (Load)(template, a1, a4, a5); unless level manager 0x0044a600 (eax =
// game+0xa64) says otherwise or a save is being restored, vt+0x20() and +0x424 |= 0x100. Returns the object.
// Original bug: a failed allocation leaves the object null and the stores after the switch write through it; a
// template name of 32 characters or more overflows the name buffer.
uint32_t ObjectSystem_CreateObject(uint32_t name, uint32_t templateName, uint32_t a0, uint32_t a1, uint32_t a2,
                                   uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7, uint32_t a8)
{
    if (!static_cast<uint8_t>(a8) && SaveLoaderRestoring())
        return 0;
    char buffer[0x20];
    if (templateName)
    {
        a3 = orig::fn_0045abe0(templateName, Field<uint32_t>(G(), 0xa84));
        if (!a3)
            orig::fn_00407ff0(Field<uint32_t>(G(), 0x34), 0x005aff70, templateName, 0);
        if (!name)
        {
            std::strcpy(buffer, static_cast<const char*>(P(templateName)));
            if (!static_cast<uint8_t>(orig::fn_0045dec0(a4, 0x005aff64, a5, a0, u(buffer))))
            {
                const uint32_t* parent = Field<const uint32_t*>(Field<void*>(P(a3), 0x10), 0x3c);
                if (parent)
                    orig::fn_0045dec0(parent[0], 0x005aff64, parent[2], a0, u(buffer));
            }
            name = u(buffer);
        }
        a7 = orig::fn_00461fc0(a0 + 4, orig::fn_0044ad70(name));
    }
    else if (name)
        a7 = orig::fn_00461fc0(a0 + 4, orig::fn_0044ad70(name));

    void* obj = nullptr;
    const uint32_t id = a7 - 1;
    const CreateCase c = id < 0xaa ? kCreateCases[id] : CreateCase{K_PLAIN, 0x524, 0x005a2bf0};
    if (c.kind == K_NULL)
        return 0;
    const uint32_t mem = ObjectAlloc(c.size);
    if (c.kind == K_PLAIN)
    {
        if (mem)
        {
            orig::fn_004511d0(mem);
            D(P(mem), 0) = c.arg;
            obj = P(mem);
        }
    }
    else if (c.arg == 0x00461840)
    {
        // Id 161: the player object, also stored in game+0x748 (0 when the allocation fails).
        obj = mem ? P(PlayerObject_Construct(P(mem))) : nullptr;
        D(G(), 0x748) = u(obj);
    }
    else if (mem)
    {
        // ecx for the GenericAIObject-derived constructors is what PMS_heapAlloc left in it (unused by
        // GenericAIObject_Construct); passed as 0.
        switch (c.arg)
        {
        case 0x004614f0: obj = P(Twiglet_Construct(P(mem))); break;
        case 0x00461790: obj = P(fn_00461790(P(mem))); break;
        case 0x00437290: obj = P(orig::fn_00437290(0, mem)); break;
        case 0x00461510: obj = P(GruntObject_Construct(0, P(mem))); break;
        case 0x00461530: obj = P(BowGruntObject_Construct(0, P(mem))); break;
        case 0x00461550: obj = P(CaptainObject_Construct(0, P(mem))); break;
        case 0x00461570: obj = P(SniperCaptain_Construct(0, P(mem))); break;
        case 0x00461590: obj = P(Q112Object_Construct(0, P(mem))); break;
        case 0x004615b0: obj = P(Q112Boss_Construct(0, P(mem))); break;
        case 0x004615d0: obj = P(PeasantGenAI_Construct(0, P(mem))); break;
        case 0x004615f0: obj = P(FakeGruntObject_Construct(0, P(mem))); break;
        case 0x00461610: obj = P(GruntLadderObject_Construct(0, P(mem))); break;
        case 0x00461630: obj = P(ColossusObject_Construct(0, P(mem))); break;
        case 0x00461650: obj = P(GoliathObject_Construct(0, P(mem))); break;
        case 0x00461670: obj = P(Polewart_Construct(0, P(mem))); break;
        case 0x00461690: obj = P(GoliathFireball_Construct(0, P(mem))); break;
        case 0x004616b0: obj = P(MonkObject_Construct(0, P(mem))); break;
        case 0x004616d0: obj = P(GenericAIObject_ConstructFull(0, P(mem))); break;
        case 0x004616f0: obj = P(ZeppelinObject_Construct(0, P(mem))); break;
        case 0x00461710: obj = P(SaucerObject_Construct(0, P(mem))); break;
        case 0x00461730: obj = P(SheepObject_Construct(0, P(mem))); break;
        case 0x00461750: obj = P(PenguinObject_Construct(0, P(mem))); break;
        case 0x00461770: obj = P(SquadObject_Construct(0, P(mem))); break;
        case 0x004617c0: obj = P(ICObject_Construct(0, P(mem))); break;
        case 0x004617e0: obj = P(ShrubPatrol_Construct(0, P(mem))); break;
        case 0x00461800: obj = P(MolmerObject_Construct(0, P(mem))); break;
        case 0x00461820: obj = P(Q111Object_Construct(0, P(mem))); break;
        default: break;
        }
    }

    // Common tail (0x004611b2). A null object is written through as in the original (original bug).
    Field<uint16_t>(obj, 0x394) = static_cast<uint16_t>(a2);
    D(obj, 0x418) = a7;
    Field<uint16_t>(obj, 0x396) = static_cast<uint16_t>(a6);
    D(obj, 0x414) = a3;
    VCall<uint32_t>(obj, 0x18 / 4, a3, a1, a4, a5);
    if (!static_cast<uint8_t>(orig::fn_0044a600(Field<uint32_t>(G(), 0xa64))) && !SaveLoaderRestoring())
    {
        VCall<uint32_t>(obj, 0x20 / 4);
        D(obj, 0x424) |= 0x100;
    }
    return u(obj);
}
static uint32_t ObjectSystem_CreateObject_Abi(uint32_t eax, uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2,
                                              uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7,
                                              uint32_t a8)
{
    return ObjectSystem_CreateObject(eax, ecx, a0, a1, a2, a3, a4, a5, a6, a7, a8);
}
ABI_ENTRY_0045f020(ObjectSystem_CreateObject_Entry, ObjectSystem_CreateObject_Abi)
HOOK(0x0045f020, "ObjectSystem_CreateObject", ObjectSystem_CreateObject_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// 0x00461870 Object_SendTrigger (eax = object system, ebx = trigger id; stack: sender; ret 4). Walks the system's
// trigger listener list (system+0xe70; first 0x004e7340, eax = list, esi = &iterator; next 0x004e73c0, edi =
// &iterator) and offers the trigger to each listener's vt+0x38(sender, trigger); the first that returns true ends the
// walk: iterator[0] vt+0xc(iterator[1]) and iterator[1]+0x18 -= 1. When the walk ends without a taker, iterator[1]+0x18
// -= 1 if set. Trigger 0 only logs "%s sent 0 trigger\nBAD BAD BAD" with Object_GetName 0x00453270 (eax = sender).
// Returns the last eax of the path taken.
uint32_t Object_SendTrigger(void* sys, uint32_t trigger, void* sender)
{
    uint32_t iter[2] = {0, 0};
    if (!trigger)
    {
        const uint32_t name = orig::fn_00453270(u(sender));
        return orig::fn_00407ff0(Field<uint32_t>(G(), 0x34), 0x005aff44, name, 0);
    }
    uint32_t e = orig::fn_004e7340(D(sys, 0xe70), u(iter));
    while (e)
    {
        if (static_cast<uint8_t>(VCall<uint32_t>(P(e), 0x38 / 4, u(sender), trigger)))
        {
            const uint32_t node = iter[1];
            const uint32_t r = VCall<uint32_t>(P(iter[0]), 0xc / 4, node);
            --Field<int32_t>(P(node), 0x18);
            return r;
        }
        e = orig::fn_004e73c0(u(iter));
    }
    if (iter[1])
        --Field<int32_t>(P(iter[1]), 0x18);
    return iter[1];
}
static uint32_t Object_SendTrigger_Abi(uint32_t eax, uint32_t ebx, uint32_t a0)
{
    return Object_SendTrigger(P(eax), ebx, P(a0));
}
ABI_ENTRY_00461870(Object_SendTrigger_Entry, Object_SendTrigger_Abi)
HOOK(0x00461870, "Object_SendTrigger", Object_SendTrigger_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// Class descriptor getters: vtable slot 1 of each class, `mov eax, descriptor; ret`. The descriptor's +0x14 names the
// class ("CLASS(Name)" / "CLASSP(ObjectSystem)"). None is executed in recorded runs.
#define CLASS_GETTER(addr, Name, desc)                                                                                 \
    uint32_t Name##_GetClass()                                                                                         \
    {                                                                                                                  \
        return desc;                                                                                                   \
    }                                                                                                                  \
    static uint32_t Name##_GetClass_Abi()                                                                              \
    {                                                                                                                  \
        return Name##_GetClass();                                                                                      \
    }                                                                                                                  \
    ABI_ENTRY_##addr(Name##_GetClass_Entry, Name##_GetClass_Abi)

// 0x00461910: descriptor 0x0057f8e8 "CLASS(CameraPathObject)" (vtable 0x005a0f18 slot 1).
CLASS_GETTER(00461910, CameraPathObject, 0x0057f8e8)
HOOK(0x00461910, "CameraPathObject_GetClass", CameraPathObject_GetClass_Entry, nullptr);
// 0x00461930: descriptor 0x00587f58 "CLASS(CaptainRocket)" (vtable 0x005a11d8 slot 1).
CLASS_GETTER(00461930, CaptainRocket, 0x00587f58)
HOOK(0x00461930, "CaptainRocket_GetClass", CaptainRocket_GetClass_Entry, nullptr);
// 0x00461960: descriptor 0x00585138 "CLASS(ICObject)" (vtable 0x005a1b60 slot 1).
CLASS_GETTER(00461960, ICObject, 0x00585138)
HOOK(0x00461960, "ICObject_GetClass", ICObject_GetClass_Entry, nullptr);
// 0x00461970: descriptor 0x0057f4e0 "CLASS(PubObject)" (vtable 0x005a1dc8 slot 1).
CLASS_GETTER(00461970, PubObject, 0x0057f4e0)
HOOK(0x00461970, "PubObject_GetClass", PubObject_GetClass_Entry, nullptr);
// 0x004619b0: descriptor 0x00587898 "CLASS(BlackHoleBox)" (vtable 0x005a2940 slot 1).
CLASS_GETTER(004619b0, BlackHoleBox, 0x00587898)
HOOK(0x004619b0, "BlackHoleBox_GetClass", BlackHoleBox_GetClass_Entry, nullptr);
// 0x004619d0: descriptor 0x00585828 "CLASS(TreeObject)" (vtable 0x005a2e60 slot 1).
CLASS_GETTER(004619d0, TreeObject, 0x00585828)
HOOK(0x004619d0, "TreeObject_GetClass", TreeObject_GetClass_Entry, nullptr);
// 0x004619e0: descriptor 0x00587c28 "CLASS(ForgeCannonProjectile)" (vtable 0x005a33b8 slot 1).
CLASS_GETTER(004619e0, ForgeCannonProjectile, 0x00587c28)
HOOK(0x004619e0, "ForgeCannonProjectile_GetClass", ForgeCannonProjectile_GetClass_Entry, nullptr);
// 0x00461a20: descriptor 0x00581070 "CLASS(FxExplosion)" (vtable 0x005a4300 slot 1).
CLASS_GETTER(00461a20, FxExplosion, 0x00581070)
HOOK(0x00461a20, "FxExplosion_GetClass", FxExplosion_GetClass_Entry, nullptr);
// 0x00461a40: descriptor 0x00583d10 "CLASS(MissionObject)" (vtable 0x005a49c8 slot 1).
CLASS_GETTER(00461a40, MissionObject, 0x00583d10)
HOOK(0x00461a40, "MissionObject_GetClass", MissionObject_GetClass_Entry, nullptr);
// 0x00461a50: descriptor 0x005836f8 "CLASS(CounterObject)" (vtable 0x005a4b00 slot 1).
CLASS_GETTER(00461a50, CounterObject, 0x005836f8)
HOOK(0x00461a50, "CounterObject_GetClass", CounterObject_GetClass_Entry, nullptr);
// 0x00461a60: descriptor 0x00587f10 "CLASS(GurnerProjectile)" (vtable 0x005a4c38 slot 1).
CLASS_GETTER(00461a60, GurnerProjectile, 0x00587f10)
HOOK(0x00461a60, "GurnerProjectile_GetClass", GurnerProjectile_GetClass_Entry, nullptr);
// 0x00461a80: descriptor 0x00588000 "CLASS(GrenadeProjectile)" (vtable 0x005a5010 slot 1).
CLASS_GETTER(00461a80, GrenadeProjectile, 0x00588000)
HOOK(0x00461a80, "GrenadeProjectile_GetClass", GrenadeProjectile_GetClass_Entry, nullptr);
// 0x00461ab0: descriptor 0x00587910 "CLASS(SharkGunProjectile)" (vtable 0x005a5d28 slot 1).
CLASS_GETTER(00461ab0, SharkGunProjectile, 0x00587910)
HOOK(0x00461ab0, "SharkGunProjectile_GetClass", SharkGunProjectile_GetClass_Entry, nullptr);
// 0x00461ad0: descriptor 0x00587388 "CLASS(PickupGenerator)" (vtable 0x005a6258 slot 1).
CLASS_GETTER(00461ad0, PickupGenerator, 0x00587388)
HOOK(0x00461ad0, "PickupGenerator_GetClass", PickupGenerator_GetClass_Entry, nullptr);
// 0x00461b00: descriptor 0x00585d20 "CLASS(BarricadeObject)" (vtable 0x005a7068 slot 1).
CLASS_GETTER(00461b00, BarricadeObject, 0x00585d20)
HOOK(0x00461b00, "BarricadeObject_GetClass", BarricadeObject_GetClass_Entry, nullptr);
// 0x00461ba0: descriptor 0x00587be0 "CLASS(RocketBudProjectile)" (vtable 0x005a7958 slot 1).
CLASS_GETTER(00461ba0, RocketBudProjectile, 0x00587be0)
HOOK(0x00461ba0, "RocketBudProjectile_GetClass", RocketBudProjectile_GetClass_Entry, nullptr);
// 0x00461bb0: descriptor 0x005845e0 "CLASS(TriggerObject)" (vtable 0x005a7c20 slot 1).
CLASS_GETTER(00461bb0, TriggerObject, 0x005845e0)
HOOK(0x00461bb0, "TriggerObject_GetClass", TriggerObject_GetClass_Entry, nullptr);
// 0x00461bc0: descriptor 0x005837e8 "CLASS(TimerObject)" (vtable 0x005a7d58 slot 1).
CLASS_GETTER(00461bc0, TimerObject, 0x005837e8)
HOOK(0x00461bc0, "TimerObject_GetClass", TimerObject_GetClass_Entry, nullptr);
// 0x00461be0: descriptor 0x0057ed48 "CLASSP(ObjectSystem)" (vtable 0x005a826c slot 1).
CLASS_GETTER(00461be0, ObjectSystem, 0x0057ed48)
HOOK(0x00461be0, "ObjectSystem_GetClass", ObjectSystem_GetClass_Entry, nullptr);
// 0x00461c20: descriptor 0x00587c70 "CLASS(StickyBombProjectile)" (vtable 0x005a83e0 slot 1).
CLASS_GETTER(00461c20, StickyBombProjectile, 0x00587c70)
HOOK(0x00461c20, "StickyBombProjectile_GetClass", StickyBombProjectile_GetClass_Entry, nullptr);
// 0x00461c40: descriptor 0x00584040 "CLASS(RocketTowerBuilding)" (vtable 0x005a87c0 slot 1).
CLASS_GETTER(00461c40, RocketTowerBuilding, 0x00584040)
HOOK(0x00461c40, "RocketTowerBuilding_GetClass", RocketTowerBuilding_GetClass_Entry, nullptr);
// 0x00461c90: descriptor 0x005842b0 "CLASS(PlayerVindaloo)" (vtable 0x005a90a8 slot 1).
CLASS_GETTER(00461c90, PlayerVindaloo, 0x005842b0)
HOOK(0x00461c90, "PlayerVindaloo_GetClass", PlayerVindaloo_GetClass_Entry, nullptr);
// 0x00461ca0: descriptor 0x00587eb0 "CLASS(MonkBasketProjectile)" (vtable 0x005a9218 slot 1).
CLASS_GETTER(00461ca0, MonkBasketProjectile, 0x00587eb0)
HOOK(0x00461ca0, "MonkBasketProjectile_GetClass", MonkBasketProjectile_GetClass_Entry, nullptr);
// 0x00461cc0: descriptor 0x00587760 "CLASS(SniperGunBullet)" (vtable 0x005a9620 slot 1).
CLASS_GETTER(00461cc0, SniperGunBullet, 0x00587760)
HOOK(0x00461cc0, "SniperGunBullet_GetClass", SniperGunBullet_GetClass_Entry, nullptr);
// 0x00461cd0: descriptor 0x00587e08 "CLASS(ArrowProjectile)" (vtable 0x005a9a40 slot 1).
CLASS_GETTER(00461cd0, ArrowProjectile, 0x00587e08)
HOOK(0x00461cd0, "ArrowProjectile_GetClass", ArrowProjectile_GetClass_Entry, nullptr);
// 0x00461d20: descriptor 0x00585f90 "CLASS(PlayerObject)" (vtable 0x005aa888 slot 1).
CLASS_GETTER(00461d20, PlayerObject, 0x00585f90)
HOOK(0x00461d20, "PlayerObject_GetClass", PlayerObject_GetClass_Entry, nullptr);
// 0x00461d50: descriptor 0x00585720 "CLASS(BridgeObject)" (vtable 0x005aafe8 slot 1).
CLASS_GETTER(00461d50, BridgeObject, 0x00585720)
HOOK(0x00461d50, "BridgeObject_GetClass", BridgeObject_GetClass_Entry, nullptr);
// 0x00461d70: descriptor 0x0057f600 "CLASS(DefaultGameObject)" (vtable 0x005a2bf0, 0x005ab650 slot 1).
CLASS_GETTER(00461d70, DefaultGameObject, 0x0057f600)
HOOK(0x00461d70, "DefaultGameObject_GetClass", DefaultGameObject_GetClass_Entry, nullptr);
// 0x00461da0: descriptor 0x00587b08 "CLASS(RocketTowerProjectile)" (vtable 0x005aba18 slot 1).
CLASS_GETTER(00461da0, RocketTowerProjectile, 0x00587b08)
HOOK(0x00461da0, "RocketTowerProjectile_GetClass", RocketTowerProjectile_GetClass_Entry, nullptr);
// 0x00461de0: descriptor 0x00584208 "CLASS(PlayerWeapon)" (vtable 0x005a1348, 0x005a4858, 0x005ac210, 0x005ad458, 0x005ae210 slot 1).
CLASS_GETTER(00461de0, PlayerWeapon, 0x00584208)
HOOK(0x00461de0, "PlayerWeapon_GetClass", PlayerWeapon_GetClass_Entry, nullptr);
// 0x00461df0: descriptor 0x005856c0 "CLASS(BackgroundObject)" (vtable 0x005ac380 slot 1).
CLASS_GETTER(00461df0, BackgroundObject, 0x005856c0)
HOOK(0x00461df0, "BackgroundObject_GetClass", BackgroundObject_GetClass_Entry, nullptr);
// 0x00461e60: descriptor 0x005877c0 "CLASS(BlackHoleProjectile)" (vtable 0x005ad858 slot 1).
CLASS_GETTER(00461e60, BlackHoleProjectile, 0x005877c0)
HOOK(0x00461e60, "BlackHoleProjectile_GetClass", BlackHoleProjectile_GetClass_Entry, nullptr);
// 0x00461e80: descriptor 0x005840a0 "CLASS(WeaponObject)" (vtable 0x005adf60 slot 1).
CLASS_GETTER(00461e80, WeaponObject, 0x005840a0)
HOOK(0x00461e80, "WeaponObject_GetClass", WeaponObject_GetClass_Entry, nullptr);
// 0x00461e90: descriptor 0x00583ea8 "CLASS(PlayerTickerBomb)" (vtable 0x005ae380 slot 1).
CLASS_GETTER(00461e90, PlayerTickerBomb, 0x00583ea8)
HOOK(0x00461e90, "PlayerTickerBomb_GetClass", PlayerTickerBomb_GetClass_Entry, nullptr);
// 0x00461ed0: descriptor 0x00587490 "CLASS(Projectile)" (vtable 0x005a3dd0 and ten more Projectile vtables slot 1).
CLASS_GETTER(00461ed0, Projectile, 0x00587490)
HOOK(0x00461ed0, "Projectile_GetClass", Projectile_GetClass_Entry, nullptr);
// 0x00461ef0: descriptor 0x005858d0 "CLASS(BuildingObject)" (vtable 0x005aeeb8 slot 1).
CLASS_GETTER(00461ef0, BuildingObject, 0x005858d0)
HOOK(0x00461ef0, "BuildingObject_GetClass", BuildingObject_GetClass_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// Constant / tuning getters (vtable slots 0, 0x51, 0x54, 0x58). None is executed in recorded runs.

// Projectile slot 0x51 (+0x144, returns st0): a per-class float from the game object or a constant. Guess: the
// projectile's lifetime or range.
#define FLOAT_GETTER(addr, Name, expr)                                                                                 \
    float Name()                                                                                                       \
    {                                                                                                                  \
        return expr;                                                                                                   \
    }                                                                                                                  \
    static float Name##_Abi()                                                                                          \
    {                                                                                                                  \
        return Name();                                                                                                 \
    }                                                                                                                  \
    ABI_ENTRY_##addr(Name##_Entry, Name##_Abi)

// 0x00461a00 (returns st0): game+0x28b8. Projectile (vtable 0x005a3dd0) slot 0x51.
FLOAT_GETTER(00461a00, fn_00461a00, F(G(), 0x28b8))
HOOK(0x00461a00, "fn_00461a00", fn_00461a00_Entry, nullptr);
// 0x00461aa0 (returns st0): game+0x2f48. SharkGunProjectile (vtable 0x005a5d28) slot 0x51.
FLOAT_GETTER(00461aa0, fn_00461aa0, F(G(), 0x2f48))
HOOK(0x00461aa0, "fn_00461aa0", fn_00461aa0_Entry, nullptr);
// 0x00461b90 (returns st0): the constant 0x005bcc08 (2000.0). RocketBudProjectile (vtable 0x005a7958) slot 0x51.
FLOAT_GETTER(00461b90, fn_00461b90, GameGlobal<float>(0x005bcc08))
HOOK(0x00461b90, "fn_00461b90", fn_00461b90_Entry, nullptr);
// 0x00461c10 (returns st0): game+0x35d8. StickyBombProjectile (vtable 0x005a83e0) slot 0x51.
FLOAT_GETTER(00461c10, fn_00461c10, F(G(), 0x35d8))
HOOK(0x00461c10, "fn_00461c10", fn_00461c10_Entry, nullptr);
// 0x00461cb0 (returns st0): game+0x2a08. SniperGunBullet (vtable 0x005a9620) slot 0x51.
FLOAT_GETTER(00461cb0, fn_00461cb0, F(G(), 0x2a08))
HOOK(0x00461cb0, "fn_00461cb0", fn_00461cb0_Entry, nullptr);
// 0x00461d90 (returns st0): the constant 0x005bcc04 (1000.0). RocketTowerProjectile (vtable 0x005aba18) slot 0x51.
FLOAT_GETTER(00461d90, fn_00461d90, GameGlobal<float>(0x005bcc04))
HOOK(0x00461d90, "fn_00461d90", fn_00461d90_Entry, nullptr);
// 0x00461e40 (returns st0): game+0x2b58. Projectile (vtable 0x005ad190) slot 0x51.
FLOAT_GETTER(00461e40, fn_00461e40, F(G(), 0x2b58))
HOOK(0x00461e40, "fn_00461e40", fn_00461e40_Entry, nullptr);
// 0x00461ec0 (returns st0): the constant 0x005bcc00 (5000.0). Slot 0x51 of 17 projectile vtables (CaptainRocket,
// BlackHoleBox, GurnerProjectile, ArrowProjectile, ...): the default.
FLOAT_GETTER(00461ec0, fn_00461ec0, GameGlobal<float>(0x005bcc00))
HOOK(0x00461ec0, "fn_00461ec0", fn_00461ec0_Entry, nullptr);

#define CONST_GETTER(addr, Name, value)                                                                                \
    uint32_t Name()                                                                                                    \
    {                                                                                                                  \
        return value;                                                                                                  \
    }                                                                                                                  \
    static uint32_t Name##_Abi()                                                                                       \
    {                                                                                                                  \
        return Name();                                                                                                 \
    }                                                                                                                  \
    ABI_ENTRY_##addr(Name##_Entry, Name##_Abi)

// 0x00461a30: 0x202. Slot 0x54 of vtable 0x005a45a8 (class id 166; no class name). Guess: a type-flags mask.
CONST_GETTER(00461a30, fn_00461a30, 0x202u)
HOOK(0x00461a30, "fn_00461a30", fn_00461a30_Entry, nullptr);
// 0x00461b30: 0x82. Slot 0x54 of vtable 0x005a76c8 (class id 164).
CONST_GETTER(00461b30, fn_00461b30, 0x82u)
HOOK(0x00461b30, "fn_00461b30", fn_00461b30_Entry, nullptr);
// 0x00461bf0: 0x80000005. ObjectSystem (vtable 0x005a826c "CLASSP(ObjectSystem)") slot 0.
CONST_GETTER(00461bf0, fn_00461bf0, 0x80000005u)
HOOK(0x00461bf0, "fn_00461bf0", fn_00461bf0_Entry, nullptr);
// 0x00461d10: 0x802. Slot 0x54 of vtable 0x005aa5f8 (class id 168).
CONST_GETTER(00461d10, fn_00461d10, 0x802u)
HOOK(0x00461d10, "fn_00461d10", fn_00461d10_Entry, nullptr);
// 0x00461dc0: 0x442. Slot 0x54 of vtable 0x005abdf0 (class id 163).
CONST_GETTER(00461dc0, fn_00461dc0, 0x442u)
HOOK(0x00461dc0, "fn_00461dc0", fn_00461dc0_Entry, nullptr);

// 0x00461c00: StickyBombProjectile (vtable 0x005a83e0) slot 0x58: a tail jump to 0x00408000 with eax = game+0xa6c;
// returns its eax. Not executed in recorded runs.
uint32_t fn_00461c00()
{
    return orig::fn_00408000(D(G(), 0xa6c));
}
static uint32_t fn_00461c00_Abi()
{
    return fn_00461c00();
}
ABI_ENTRY_00461c00(fn_00461c00_Entry, fn_00461c00_Abi)
HOOK(0x00461c00, "fn_00461c00", fn_00461c00_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// 0x00466e70 (ecx = object). Slot 0x2f (update) of vtable 0x005a3260 (class id 122; its slot 1 returns 0, no class
// name). Runs when byte +0x470 bit 0 or byte +0x474 bit 1 is set. Picks a target by +0x4a8: 0 the player
// (game+0x748), 1 *(0x00408020(game+0xa6c) = +0x2c), 2 player+0xad4 (0x00475d40), 3 player+0xad0 (0x00475d30); with no
// player, an unknown selector, no target or game byte +0xdce set, +0x474 bit 2 is cleared. Otherwise r = target+0x454
// / target+0x458 (0x004555b0 / 0x004555c0) is stored at +0x4a0, +0x4a4 = 0.5 below 0.25, 0.25 below 0.5, else 0.
// Above 0.2: vt+0x100(1.0), +0x4ac = 0. At or below 0.2 the value +0x378 (0x00453630) is pulsed: up by frame time *
// 8 while +0x4ac is set (reaching 1.0 clears it), else down by 2 * frame time (reaching 0 sets it), and passed to
// vt+0x100. Both set +0x474 bit 2. Guess: a health-style meter that flashes when low. The original returns
// whatever eax holds (fnstsw bits or a vt+0x100 result); this returns 0. Not executed in recorded runs.
uint32_t fn_00466e70(void* obj)
{
    if (!(B(obj, 0x470) & 1) && !(B(obj, 0x474) & 2))
        return 0;
    void* g = G();
    const uint32_t player = D(g, 0x748);
    uint32_t target = 0;
    if (player)
    {
        switch (D(obj, 0x4a8))
        {
        case 0: target = player; break;
        case 1: target = D(P(orig::fn_00408020(D(g, 0xa6c))), 0); break;
        case 2: target = orig::fn_00475d40(player); break;
        case 3: target = orig::fn_00475d30(player); break;
        default: break;
        }
    }
    if (!target || B(g, 0xdce))
    {
        D(obj, 0x474) &= ~4u;
        return 0;
    }
    const float r = orig::fn_004555b0(target) / orig::fn_004555c0(target);
    F(obj, 0x4a0) = r;
    if (r < GameGlobal<float>(0x005bcbe4))  // 0.25
        D(obj, 0x4a4) = 0x3f000000;          // 0.5
    else if (r < GameGlobal<float>(0x005bcb4c))  // 0.5
        D(obj, 0x4a4) = 0x3e800000;               // 0.25
    else
        D(obj, 0x4a4) = 0;
    if (r > GameGlobal<float>(0x005bcd2c))  // 0.2
    {
        VCall<uint32_t>(obj, 0x100 / 4, 0x3f800000u);
        D(obj, 0x4ac) = 0;
        D(obj, 0x474) |= 4;
        return 0;
    }
    float v;
    if (D(obj, 0x4ac))
    {
        v = orig::fn_00453630(u(obj)) + kFrameTime * GameGlobal<float>(0x005bcb48);  // 8.0
        if (v >= kOne)
        {
            v = 1.0f;
            D(obj, 0x4ac) = 0;
        }
    }
    else
    {
        v = orig::fn_00453630(u(obj)) - (kFrameTime + kFrameTime);
        if (v <= kZero)
        {
            v = 0.0f;
            D(obj, 0x4ac) = 1;
        }
    }
    VCall<uint32_t>(obj, 0x100 / 4, f2u(v));
    D(obj, 0x474) |= 4;
    return 0;
}
static uint32_t fn_00466e70_Abi(uint32_t ecx)
{
    return fn_00466e70(P(ecx));
}
ABI_ENTRY_00466e70(fn_00466e70_Entry, fn_00466e70_Abi)
HOOK(0x00466e70, "fn_00466e70", fn_00466e70_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// Pub objective markers: an owner (the pub / objective controller driven by 0x004930c0 and 0x00467fe0) holds marker
// objects in slots +0x38 (3), +0x44 (5), +0x58 (5) and +0x6c (1). Each marker's +0x4a0 is the "ui" object created
// for it (class name 0x005b03e4 "ui", template game+0x2810 + kind * 0x150) and +0x4a8 its kind (-1 when free).

// Creates the "ui" object of `kind` for `marker` (the block shared by 0x004691c0 / 0x00469270 / 0x00469350 /
// 0x00469450): ObjectSystem_CreateObject(eax = "ui", ecx = game+0x2810 + kind * 0x150; game+0xa84, 0, 0x3ff, 0, 0,
// 0, 0, 0xaa, 0); the new object copies marker +0x46c and +0x468, then 0x00464b10 (esi = object), whose eax is
// returned through `r464`.
static void* CreateMarkerUi(uint32_t kind, uint32_t* markerSlot, uint32_t* r464)
{
    void* g = G();
    void* ui = P(ObjectSystem_CreateObject(0x005b03e4, u(g) + kind * 0x150 + 0x2810, D(g, 0xa84), 0, 0x3ff, 0, 0, 0,
                                           0, 0xaa, 0));
    D(ui, 0x46c) = D(P(*markerSlot), 0x46c);
    D(ui, 0x468) = D(P(*markerSlot), 0x468);
    const uint32_t r = orig::fn_00464b10(u(ui));
    if (r464)
        *r464 = r;
    return ui;
}

// 0x004690c0 (edi = owner; symbols/abi.csv also lists ebp, but nothing reads it: 0x004694e0 saves it). Called from
// 0x004930c0. Clears the owner: with a current marker +0x8c, 0x0046a650 (ecx = marker, esi = 12-byte local) and
// 0x0046a720 (esi = marker) fire its exit trigger, 0x004a1bb0 (eax = player+0xc24) and 0x00403560 (esi = game)
// follow. Then (game+0xa6c) word +0x390 = 2, the 14 objects at owner+0..+0x34 lose +0x474 bit 2, owner+0x7c
// vt+0xdc() (remove), +0x7c = +0x8c = 0, the handles +0x90 / +0x94 are released through (game+0x2c) vt+0xd8(&handle,
// 0) and zeroed, 0x004694e0 frees all markers, and game +0xad8 = 0, bytes +0xae5 / +0xae4 = 0. Returns game. Not
// executed in recorded runs.
uint32_t fn_004690c0(void* owner)
{
    if (D(owner, 0x8c))
    {
        uint32_t local[3];
        fn_0046a650(P(D(owner, 0x8c)), local);
        fn_0046a720(P(D(owner, 0x8c)));
        orig::fn_004a1bb0(D(G(), 0x748) + 0xc24);
        orig::fn_00403560(u(G()));
    }
    Field<uint16_t>(P(D(G(), 0xa6c)), 0x390) = 2;
    for (uint32_t i = 0; i < 0xe; ++i)
        D(P(D(owner, i * 4)), 0x474) &= ~4u;
    VCall<uint32_t>(P(D(owner, 0x7c)), 0xdc / 4);
    const uint32_t h90 = D(owner, 0x90);
    D(owner, 0x7c) = 0;
    D(owner, 0x8c) = 0;
    if (h90)
        VCall<uint32_t>(P(D(G(), 0x2c)), 0xd8 / 4, u(owner) + 0x90, 0u);
    D(owner, 0x90) = 0;
    if (D(owner, 0x94))
        VCall<uint32_t>(P(D(G(), 0x2c)), 0xd8 / 4, u(owner) + 0x94, 0u);
    D(owner, 0x94) = 0;
    fn_004694e0(owner);
    void* g = G();
    D(g, 0xad8) = 0;
    B(g, 0xae5) = 0;
    B(g, 0xae4) = 0;
    return u(g);
}
static uint32_t fn_004690c0_Abi(uint32_t edi, uint32_t /*ebp*/)
{
    return fn_004690c0(P(edi));
}
ABI_ENTRY_004690c0(fn_004690c0_Entry, fn_004690c0_Abi)
HOOK(0x004690c0, "fn_004690c0", fn_004690c0_Entry, nullptr);

// 0x004691c0 (ebx = owner; stack: kind; ret 4; caller 0x00467fe0). Gives the first of the three +0x38 markers
// without a "ui" object one of `kind` (CreateMarkerUi), sets its +0x474 bit 2, and stores it (+0x4a0) and the kind
// (+0x4a8) in the marker. Returns the marker, or owner+0x44 (the loop pointer) when all three are taken. Not
// executed in recorded runs.
uint32_t fn_004691c0(void* owner, uint32_t kind)
{
    uint32_t* slots = &D(owner, 0x38);
    uint32_t i = 0;
    for (; i < 3; ++i)
        if (!D(P(slots[i]), 0x4a0))
            break;
    if (i == 3)
        return u(owner) + 0x44;
    void* ui = CreateMarkerUi(kind, &slots[i], nullptr);
    D(ui, 0x474) |= 4;
    D(P(slots[i]), 0x4a0) = u(ui);
    D(P(slots[i]), 0x4a8) = kind;
    return slots[i];
}
static uint32_t fn_004691c0_Abi(uint32_t ebx, uint32_t a0)
{
    return fn_004691c0(P(ebx), a0);
}
ABI_ENTRY_004691c0(fn_004691c0_Entry, fn_004691c0_Abi)
HOOK(0x004691c0, "fn_004691c0", fn_004691c0_Entry, nullptr);

// 0x00469270 (ebx = owner; stack: kind; ret 4; callers 0x00467fe0, 0x00493130). As 0x004691c0 for the five +0x44
// markers; kind 10 also starts animation 0x10 on the new object (Anim_Set 0x00408d90, eax = 1, esi = object; 0,
// 0x10, 0, 0, 0, 0). Returns the marker, or owner+0x58 when all are taken. Not executed in recorded runs.
uint32_t fn_00469270(void* owner, uint32_t kind)
{
    uint32_t* slots = &D(owner, 0x44);
    uint32_t i = 0;
    for (; i < 5; ++i)
        if (!D(P(slots[i]), 0x4a0))
            break;
    if (i == 5)
        return u(owner) + 0x58;
    void* ui = CreateMarkerUi(kind, &slots[i], nullptr);
    D(ui, 0x474) |= 4;
    if (kind == 10)
        orig::fn_00408d90(1, u(ui), 0, 0x10, 0, 0, 0, 0);
    D(P(slots[i]), 0x4a0) = u(ui);
    D(P(slots[i]), 0x4a8) = kind;
    return slots[i];
}
static uint32_t fn_00469270_Abi(uint32_t ebx, uint32_t a0)
{
    return fn_00469270(P(ebx), a0);
}
ABI_ENTRY_00469270(fn_00469270_Entry, fn_00469270_Abi)
HOOK(0x00469270, "fn_00469270", fn_00469270_Entry, nullptr);

// 0x00469350 (ebx = owner; stack: kind; ret 4; caller 0x00467fe0). As 0x004691c0 for the five +0x58 markers. Then
// kind 1 sets owner byte +0x141 unless game byte +0x575 is set; kind 3 sets owner byte +0x142 unless game byte
// +0x576 is set, and always +0x144. Returns 1 after creating, owner+0x6c when all markers are taken. Not executed in
// recorded runs.
uint32_t fn_00469350(void* owner, uint32_t kind)
{
    uint32_t* slots = &D(owner, 0x58);
    uint32_t i = 0;
    for (; i < 5; ++i)
        if (!D(P(slots[i]), 0x4a0))
            break;
    if (i == 5)
        return u(owner) + 0x6c;
    void* ui = CreateMarkerUi(kind, &slots[i], nullptr);
    D(ui, 0x474) |= 4;
    D(P(slots[i]), 0x4a0) = u(ui);
    D(P(slots[i]), 0x4a8) = kind;
    if (kind == 1)
    {
        if (!B(G(), 0x575))
            B(owner, 0x141) = 1;
    }
    else if (kind == 3)
    {
        if (!B(G(), 0x576))
            B(owner, 0x142) = 1;
        B(owner, 0x144) = 1;
    }
    return 1;
}
static uint32_t fn_00469350_Abi(uint32_t ebx, uint32_t a0)
{
    return fn_00469350(P(ebx), a0);
}
ABI_ENTRY_00469350(fn_00469350_Entry, fn_00469350_Abi)
HOOK(0x00469350, "fn_00469350", fn_00469350_Entry, nullptr);

// 0x00469450 (ebx = kind, edi = owner; callers 0x00467fe0, 0x00493130). As 0x004691c0 for the single +0x6c marker:
// when it has no "ui" object, creates one of `kind`. Returns the marker when it already had one, else 0x00464b10's
// eax. Not executed in recorded runs.
uint32_t fn_00469450(uint32_t kind, void* owner)
{
    if (D(P(D(owner, 0x6c)), 0x4a0))
        return D(owner, 0x6c);
    uint32_t r = 0;
    void* ui = CreateMarkerUi(kind, &D(owner, 0x6c), &r);
    D(ui, 0x474) |= 4;
    D(P(D(owner, 0x6c)), 0x4a0) = u(ui);
    D(P(D(owner, 0x6c)), 0x4a8) = kind;
    return r;
}
static uint32_t fn_00469450_Abi(uint32_t ebx, uint32_t edi)
{
    return fn_00469450(ebx, P(edi));
}
ABI_ENTRY_00469450(fn_00469450_Entry, fn_00469450_Abi)
HOOK(0x00469450, "fn_00469450", fn_00469450_Entry, nullptr);

// 0x004694e0 (stack: owner; ret 4; caller 0x004690c0; symbols/abi.csv lists an ebp input, but ebp is saved and set
// to -1). Frees every marker of the owner, in the order +0x38 (3), +0x58 (5), +0x44 (5), +0x6c: the "ui" object
// +0x4a0 gets vt+0xdc() (remove) when set, +0x4a0 = 0, +0x4a8 = -1. Returns the owner. Not executed in recorded runs.
static void FreeMarker(uint32_t* slot)
{
    const uint32_t ui = D(P(*slot), 0x4a0);
    if (ui)
        VCall<uint32_t>(P(ui), 0xdc / 4);
    D(P(*slot), 0x4a0) = 0;
    D(P(*slot), 0x4a8) = 0xffffffffu;
}
uint32_t fn_004694e0(void* owner)
{
    for (uint32_t i = 0; i < 3; ++i)
        FreeMarker(&D(owner, 0x38 + i * 4));
    for (uint32_t i = 0; i < 5; ++i)
        FreeMarker(&D(owner, 0x58 + i * 4));
    for (uint32_t i = 0; i < 5; ++i)
        FreeMarker(&D(owner, 0x44 + i * 4));
    FreeMarker(&D(owner, 0x6c));
    return u(owner);
}
static uint32_t fn_004694e0_Abi(uint32_t /*ebp*/, uint32_t a0)
{
    return fn_004694e0(P(a0));
}
ABI_ENTRY_004694e0(fn_004694e0_Entry, fn_004694e0_Abi)
HOOK(0x004694e0, "fn_004694e0", fn_004694e0_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// PubObject (vtable 0x005a1dc8 "CLASS(PubObject)") helpers. Member names from the property strings PubObject_Load
// 0x0046a200 reads into them.

// 0x0046a5e0 (eax = pub; caller 0x00467fe0). Returns al = byte +0x467 ("should_refill_ammo"), the upper eax bytes
// as passed. Not executed in recorded runs.
uint32_t fn_0046a5e0(uint32_t pub)
{
    return (pub & 0xffffff00u) | B(P(pub), 0x467);
}
static uint32_t fn_0046a5e0_Abi(uint32_t eax)
{
    return fn_0046a5e0(eax);
}
ABI_ENTRY_0046a5e0(fn_0046a5e0_Entry, fn_0046a5e0_Abi)
HOOK(0x0046a5e0, "fn_0046a5e0", fn_0046a5e0_Entry, nullptr);

// The bone-position lookup shared by 0x0046a5f0 / 0x0046a650: when the bone name at obj+off is not empty,
// 0x00453c50(obj, obj+off, &matrix, 1, 0, 0, 1, 0, 0) fills a 0x44-byte matrix whose translation (m[3], m[7], m[11])
// is copied to out[0..2] (returns m[3]); otherwise obj vt+0x54(out) (the object's own position) and its eax.
static uint32_t BonePosition(void* obj, uint32_t off, uint32_t* out)
{
    if (!B(obj, off))
        return VCall<uint32_t>(obj, 0x54 / 4, u(out));
    uint32_t m[0x44 / 4];
    orig::fn_00453c50(u(obj), u(obj) + off, u(m), 1, 0, 0, 1, 0, 0);
    out[0] = m[3];
    out[1] = m[7];
    out[2] = m[11];
    return m[3];
}

// 0x0046a5f0 (ecx = pub, esi = out vector; callers 0x00467b60, 0x004788a0). The position of the bone named by +0x478
// ("doorway_bone_name"), or the pub's position when no name is set (BonePosition).
uint32_t PubObject_GetDoorwayPosition(void* pub, uint32_t* out)
{
    return BonePosition(pub, 0x478, out);
}
static uint32_t PubObject_GetDoorwayPosition_Abi(uint32_t ecx, uint32_t esi)
{
    return PubObject_GetDoorwayPosition(P(ecx), static_cast<uint32_t*>(P(esi)));
}
ABI_ENTRY_0046a5f0(PubObject_GetDoorwayPosition_Entry, PubObject_GetDoorwayPosition_Abi)
HOOK(0x0046a5f0, "PubObject_GetDoorwayPosition", PubObject_GetDoorwayPosition_Entry, nullptr);

// 0x0046a640 (eax = pub; returns st0; callers 0x00467b60, 0x004788a0). +0x498 ("doorway_entry_radius").
float PubObject_GetDoorwayEntryRadius(void* pub)
{
    return F(pub, 0x498);
}
static float PubObject_GetDoorwayEntryRadius_Abi(uint32_t eax)
{
    return PubObject_GetDoorwayEntryRadius(P(eax));
}
ABI_ENTRY_0046a640(PubObject_GetDoorwayEntryRadius_Entry, PubObject_GetDoorwayEntryRadius_Abi)
HOOK(0x0046a640, "PubObject_GetDoorwayEntryRadius", PubObject_GetDoorwayEntryRadius_Entry, nullptr);

// 0x0046a650 (ecx = pub, esi = out vector; caller 0x004690c0). As 0x0046a5f0 for +0x49c ("player_exit_bone_name").
// Not executed in recorded runs.
uint32_t fn_0046a650(void* pub, uint32_t* out)
{
    return BonePosition(pub, 0x49c, out);
}
static uint32_t fn_0046a650_Abi(uint32_t ecx, uint32_t esi)
{
    return fn_0046a650(P(ecx), static_cast<uint32_t*>(P(esi)));
}
ABI_ENTRY_0046a650(PubObject_GetPlayerExitPosition_Entry, fn_0046a650_Abi)
HOOK(0x0046a650, "PubObject_GetPlayerExitPosition", PubObject_GetPlayerExitPosition_Entry, nullptr);

// 0x0046a6a0 (eax = pub; returns st0 - symbols/abi.csv says none, but 0x00467fe0 uses st0). +0x46c ("health_pool").
// Not executed in recorded runs.
float PubObject_GetHealthPool(void* pub)
{
    return F(pub, 0x46c);
}
static float PubObject_GetHealthPool_Abi(uint32_t eax)
{
    return PubObject_GetHealthPool(P(eax));
}
ABI_ENTRY_0046a6a0(PubObject_GetHealthPool_Entry, PubObject_GetHealthPool_Abi)
HOOK(0x0046a6a0, "PubObject_GetHealthPool", PubObject_GetHealthPool_Entry, nullptr);

// 0x0046a6b0 (ecx = pub; stack: value; ret 4). +0x46c ("health_pool") = value; returns value. Not executed in
// recorded runs.
uint32_t PubObject_SetHealthPool(void* pub, uint32_t value)
{
    D(pub, 0x46c) = value;
    return value;
}
static uint32_t PubObject_SetHealthPool_Abi(uint32_t ecx, uint32_t a0)
{
    return PubObject_SetHealthPool(P(ecx), a0);
}
ABI_ENTRY_0046a6b0(PubObject_SetHealthPool_Entry, PubObject_SetHealthPool_Abi)
HOOK(0x0046a6b0, "PubObject_SetHealthPool", PubObject_SetHealthPool_Entry, nullptr);

// 0x0046a6c0 (ecx = pub; callers 0x00467b60, 0x004788a0). Returns +0x4c8 ("listen_trigger_open") == 0: the pub is
// open once its open trigger has arrived (PubObject_OnTrigger clears it). Not executed in recorded runs.
uint32_t fn_0046a6c0(void* pub)
{
    return D(pub, 0x4c8) == 0 ? 1u : 0u;
}
static uint32_t fn_0046a6c0_Abi(uint32_t ecx)
{
    return fn_0046a6c0(P(ecx));
}
ABI_ENTRY_0046a6c0(fn_0046a6c0_Entry, fn_0046a6c0_Abi)
HOOK(0x0046a6c0, "fn_0046a6c0", fn_0046a6c0_Entry, nullptr);

// 0x0046a6d0 (eax = pub; caller 0x00467b60). Sends +0x4bc ("send_trigger_entry_failure") through
// Object_SendTrigger (game+0xa84; sender = pub) when set. Returns its eax, or the pub. Not executed in recorded
// runs.
uint32_t PubObject_SendEntryFailure(void* pub)
{
    const uint32_t trigger = D(pub, 0x4bc);
    if (!trigger)
        return u(pub);
    return Object_SendTrigger(P(D(G(), 0xa84)), trigger, pub);
}
static uint32_t PubObject_SendEntryFailure_Abi(uint32_t eax)
{
    return PubObject_SendEntryFailure(P(eax));
}
ABI_ENTRY_0046a6d0(PubObject_SendEntryFailure_Entry, PubObject_SendEntryFailure_Abi)
HOOK(0x0046a6d0, "PubObject_SendEntryFailure", PubObject_SendEntryFailure_Entry, nullptr);

// 0x0046a6f0 (esi = pub; caller 0x004788a0). Sends +0x4c0 ("send_trigger_entry") once: Object_SendTrigger when set,
// then +0x4c0 = 0. Returns the send's eax (0 when nothing was sent; the original leaves the caller's eax). Not
// executed in recorded runs.
uint32_t PubObject_SendEntryTrigger(void* pub)
{
    uint32_t r = 0;
    const uint32_t trigger = D(pub, 0x4c0);
    if (trigger)
        r = Object_SendTrigger(P(D(G(), 0xa84)), trigger, pub);
    D(pub, 0x4c0) = 0;
    return r;
}
static uint32_t PubObject_SendEntryTrigger_Abi(uint32_t esi)
{
    return PubObject_SendEntryTrigger(P(esi));
}
ABI_ENTRY_0046a6f0(PubObject_SendEntryTrigger_Entry, PubObject_SendEntryTrigger_Abi)
HOOK(0x0046a6f0, "PubObject_SendEntryTrigger", PubObject_SendEntryTrigger_Entry, nullptr);

// 0x0046a720 (esi = pub; caller 0x004690c0). As 0x0046a6f0 for +0x4c4 ("send_trigger_exit"). Not executed in
// recorded runs.
uint32_t fn_0046a720(void* pub)
{
    uint32_t r = 0;
    const uint32_t trigger = D(pub, 0x4c4);
    if (trigger)
        r = Object_SendTrigger(P(D(G(), 0xa84)), trigger, pub);
    D(pub, 0x4c4) = 0;
    return r;
}
static uint32_t fn_0046a720_Abi(uint32_t esi)
{
    return fn_0046a720(P(esi));
}
ABI_ENTRY_0046a720(PubObject_SendExitTrigger_Entry, fn_0046a720_Abi)
HOOK(0x0046a720, "PubObject_SendExitTrigger", PubObject_SendExitTrigger_Entry, nullptr);

// 0x0046a750 (ecx = pub; stack: info; ret 4). PubObject slot 7: the base 0x004527d0 (ecx = pub; info), then info
// byte +0xd = 1 and info+0 = 4. Returns 0x004527d0's eax.
uint32_t fn_0046a750(void* pub, void* info)
{
    const uint32_t r = orig::fn_004527d0(u(pub), u(info));
    B(info, 0xd) = 1;
    D(info, 0) = 4;
    return r;
}
static uint32_t fn_0046a750_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_0046a750(P(ecx), P(a0));
}
ABI_ENTRY_0046a750(fn_0046a750_Entry, fn_0046a750_Abi)
HOOK(0x0046a750, "fn_0046a750", fn_0046a750_Entry, nullptr);

// 0x0046a770 (ecx = pub; stack: sender, trigger; ret 8). PubObject slot 0xe, the trigger handler Object_SendTrigger
// calls (vt+0x38): a trigger equal to +0x4c8 ("listen_trigger_open") clears it. Returns al = 0 (not consumed), the
// upper eax bytes from the trigger. Not executed in recorded runs.
uint32_t PubObject_OnTrigger(void* pub, uint32_t /*sender*/, uint32_t trigger)
{
    if (trigger == D(pub, 0x4c8))
        D(pub, 0x4c8) = 0;
    return trigger & 0xffffff00u;
}
static uint32_t PubObject_OnTrigger_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return PubObject_OnTrigger(P(ecx), a0, a1);
}
ABI_ENTRY_0046a770(PubObject_OnTrigger_Entry, PubObject_OnTrigger_Abi)
HOOK(0x0046a770, "PubObject_OnTrigger", PubObject_OnTrigger_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// 0x00467fe0 (stack: owner; ret 4; caller 0x004930c0). Opens the pub screen (strings "pui_main", "ui_pub_*",
// "amb_pub_busy", "ui_pub_enter"; guess: the pub UI controller's Enter). Steps, in the original order:
//  - owner (the controller) +0x144 dword, byte +0x148, +0x140 = 0; game byte +0xae4 = 1; 0x00463e80 (ebx =
//    game+0xa70; "pui_main", 0, 0, 0); owner+0x8c = the pub game+0xad8 (read first), +0x74 = +0x78 = 0;
//  - the 14 "ref_*" marker objects (0x00463ad0, ebx = name; game+0xa70, "ui_slot") go to owner+0x38.. and owner+0..
//    (layout below), get +0x474 |= 0x10, and "ref_pub_interior"/"ui_pub_interior" too; marker kinds +0x4a4 (0: 3
//    weapon slots, 1: 5, 3: 1, 2: 5); all 14 get +0x474 |= 4, and every object in game+0xa84 list +0xeb8 whose
//    vt+0x150() has bit 1 and whose +0x46c matches marker 0's gets +0x474 |= 4;
//  - owner+0xd0..+0x13c (0x1c slots) zeroed, 11 of them looked up by 0x00463890 (ebx = game+0x27b0.. name buffers)
//    and every non-null one loses +0x474 bit 2; "ui_pub_interior" gets bit 2; owner+0x70 = marker 0; the
//    "ui_pub_cursor" object (+0x7c) loses bit 2 and is then replaced by a new "ui" object created from that template
//    (ObjectSystem_CreateObject), which copies marker 0's +0x46c / +0x468, 0x00464b10, bit 2 set; +0x80
//    "ui_pub_highlight" loses bit 2; +0x88 "ref_health"/"ui_pub_health" and +0x84 "ref_health_pool"/
//    "ui_pub_healthpool" get bit 2;
//  - player+0xc24 weapon lists: for each of the 3 entries of 0x004a1600 (not -1) fn_004691c0 makes a slot, and
//    unless the pub's "should_refill_ammo" byte is clear 0x004a13a0 (eax = 100000, ecx = weapon; player+0xc24)
//    tops it up; owner byte +0xbc = 0, then for weapons 0..5 the pub sells (pub bytes +0x460.., all when there is no
//    pub) and the player does not hold, +0xbc = 1 and fn_00469350; without a pub, weapons 6..10 are topped up; the 5
//    entries of 0x004a1630 go through fn_00469270 the same way; game byte +0xd90 adds kind 11 (fn_00469450);
//  - owner +0xa0 = +0xa8 = -1, +0xa4 = the time 0x00604a98; a pub with "has_tickerbomb" (+0x466) sets +0xa8 = 0
//    unless game byte +0xd90, and game byte +0xd90 = 1; a pub special weapon (+0x468, not -1) the player lacks
//    becomes +0xa0 (+0xa8 = 5 when still -1); 0x004a14e0 (eax = 10000, ecx = weapon; player+0xc24) gives it
//    (when the player has it, only if the pub refills ammo);
//  - the name buffers game+0x3640 / +0x3660, the special weapon's (game+0x27d0 / 0x27f0 + 0x150 * kind) and kinds 6..10
//    hide their objects (0x00465280, ecx = 0x1b; object, 0x23 for the 0x00463890 ones); +0xc0 "pub_notify_new",
//    +0xc4, +0xc8 hidden, +0xcc "pub_notify_exit" shown (0x00465280 ecx = 0x1c);
//  - health: H = player+0x454, X = player+0x458, Y = pool (pub +0x46c, 100.0 without a pub); the player's health
//    becomes min(X, Y + H) through 0x0047af50 (ecx = player), owner+0x98 = that value, the pool loses what was used;
//    +0x14c = Y, +0x150 = H; health bar (+0x88)+0x4a0 = H / player+0x458; pool bar (+0x84)+0x4a0 = Y / pub+0x470
//    ("health_bar_rate"), or Y * 0.01 without a pub; the player's +0xad0 / +0xad4 objects get +0x454 = +0x458;
//  - level name (0x0044a660, eax = game+0xa64, cl = 1) prefixes via __strnicmp(.., 5): "lev_1_"/"lev_2_" set byte
//    +0x146, else "lev_3_".."lev_5_" byte +0x147; "lev_2_".."lev_5_" set byte +0x148;
//  - owner+0x90 = Pub_PlayAmbientLine 0x00468dd0 (ecx = owner); owner+0x94 = (game+0x2c) vt+0xd0("amb_pub_busy", 0,
//    1, 0, -1, 0); vt+0xd0("ui_pub_enter", 0, 0, 0, -1, 0).
// Returns the list iterator's node (its +0x18 reference released). Not executed in recorded runs.
uint32_t fn_00467fe0(void* o)
{
    void* g = G();
    const uint32_t pub = D(g, 0xad8);
    B(g, 0xae4) = 1;
    D(o, 0x144) = 0;
    B(o, 0x148) = 0;
    D(o, 0x140) = 0;
    orig::fn_00463e80(D(g, 0xa70), 0x005a0df0, 0, 0, 0);
    D(o, 0x8c) = pub;
    D(o, 0x74) = 0;
    D(o, 0x78) = 0;

    // Marker lookups: name -> (owner offset, index in owner+0).
    static const struct
    {
        uint32_t name, off;
    } kRefs[14] = {
        {0x005a0c60, 0x38}, {0x005a0c50, 0x3c}, {0x005a0c40, 0x40}, {0x005a0c2c, 0x44}, {0x005a0c18, 0x48},
        {0x005a0c04, 0x4c}, {0x005a0bf0, 0x50}, {0x005a0bdc, 0x54}, {0x005a0c70, 0x6c}, {0x005a0bd0, 0x58},
        {0x005a0bc4, 0x5c}, {0x005a0bb8, 0x60}, {0x005a0bac, 0x64}, {0x005a0ba0, 0x68},
    };
    for (uint32_t i = 0; i < 14; ++i)
    {
        const uint32_t m = orig::fn_00463ad0(kRefs[i].name, D(G(), 0xa70), 0x005b010c);
        D(o, kRefs[i].off) = m;
        D(o, i * 4) = m;
    }
    for (uint32_t i = 0; i < 0xe; ++i)
        D(P(D(o, i * 4)), 0x474) |= 0x10;
    {
        const uint32_t r = orig::fn_00463ad0(0x005a0b8c, D(G(), 0xa70), 0x005a0b7c);
        if (r)
            D(P(r), 0x474) |= 0x10;
    }
    for (uint32_t off = 0x38; off <= 0x40; off += 4)
        D(P(D(o, off)), 0x4a4) = 0;
    for (uint32_t off = 0x44; off <= 0x54; off += 4)
        D(P(D(o, off)), 0x4a4) = 1;
    D(P(D(o, 0x6c)), 0x4a4) = 3;
    for (uint32_t off = 0x58; off <= 0x68; off += 4)
        D(P(D(o, off)), 0x4a4) = 2;
    for (uint32_t i = 0; i < 0xe; ++i)
        D(P(D(o, i * 4)), 0x474) |= 4;

    // Original frame locals: [esp+0x14] / [esp+0x18] = the list iterator (released at the end).
    uint32_t iter[2] = {0, 0};
    for (uint32_t e = orig::fn_004e7340(D(P(D(G(), 0xa84)), 0xeb8), u(iter)); e; e = orig::fn_004e73c0(u(iter)))
    {
        if ((VCall<uint32_t>(P(e), 0x150 / 4) & 2) && D(P(e), 0x46c) == D(P(D(o, 0)), 0x46c))
            D(P(e), 0x474) |= 4;
    }

    std::memset(&D(o, 0xd0), 0, 0x1c * 4);
    static const uint32_t kNames[11][2] = {
        {0x27b0, 0xd0}, {0x2900, 0xd4}, {0x2a50, 0xd8}, {0x2ba0, 0xdc}, {0x2cf0, 0xe0}, {0x2e40, 0xe4},
        {0x2f90, 0xe8}, {0x30e0, 0xec}, {0x3230, 0xf0}, {0x34d0, 0xf8}, {0x3380, 0xf4},
    };
    for (uint32_t i = 0; i < 11; ++i)
        D(o, kNames[i][1]) = orig::fn_00463890(u(G()) + kNames[i][0], D(G(), 0xa70));
    for (uint32_t i = 0; i < 0x1c; ++i)
    {
        const uint32_t r = D(o, 0xd0 + i * 4);
        if (r)
            D(P(r), 0x474) &= 0xfffffffbu;
    }
    {
        const uint32_t r = orig::fn_00463ad0(0, D(G(), 0xa70), 0x005a0b7c);
        if (r)
            D(P(r), 0x474) |= 4;
    }
    D(o, 0x70) = D(o, 0);
    {
        const uint32_t r = orig::fn_00463ad0(0, D(G(), 0xa70), 0x005a0b6c);
        D(o, 0x7c) = r;
        if (r)
            D(P(r), 0x474) &= 0xfffffffbu;
    }
    D(o, 0x7c) = ObjectSystem_CreateObject(0x005b03e4, 0x005a0b6c, D(G(), 0xa84), 0, 0x3ff, 0, 0, 0, 0, 0xaa, 0);
    D(P(D(o, 0x7c)), 0x46c) = D(P(D(o, 0)), 0x46c);
    D(P(D(o, 0x7c)), 0x468) = D(P(D(o, 0)), 0x468);
    orig::fn_00464b10(D(o, 0x7c));
    D(P(D(o, 0x7c)), 0x474) |= 4;
    {
        const uint32_t r = orig::fn_00463ad0(0, D(G(), 0xa70), 0x005a0b58);
        D(o, 0x80) = r;
        if (r)
            D(P(r), 0x474) &= 0xfffffffbu;
    }
    D(o, 0x88) = orig::fn_00463ad0(0x005a0b4c, D(G(), 0xa70), 0x005b0120);
    D(o, 0x84) = orig::fn_00463ad0(0x005a0b3c, D(G(), 0xa70), 0x005b0130);
    D(P(D(o, 0x88)), 0x474) |= 4;
    D(P(D(o, 0x84)), 0x474) |= 4;

    // Weapon slots.
    const uint32_t* held = static_cast<const uint32_t*>(P(orig::fn_004a1600(D(G(), 0x748) + 0xc24)));
    for (uint32_t i = 0; i < 3; ++i)
    {
        if (held[i] == 0xffffffffu)
            continue;
        fn_004691c0(o, held[i]);
        const uint32_t p = D(o, 0x8c);
        if (!p || static_cast<uint8_t>(fn_0046a5e0(p)))
            orig::fn_004a13a0(0x186a0, held[i], D(G(), 0x748) + 0xc24);
    }
    B(o, 0xbc) = 0;
    for (uint32_t w = 0; w < 6; ++w)
    {
        const uint32_t p = D(o, 0x8c);
        if (p && !B(P(p), 0x460 + w))
            continue;
        uint32_t k = 0;
        while (k < 3 && held[k] != w)
            ++k;
        if (k < 3)
            continue;
        B(o, 0xbc) = 1;
        fn_00469350(o, w);
    }
    if (!D(o, 0x8c))
    {
        for (uint32_t w = 6; w <= 0xa; ++w)
            orig::fn_004a13a0(0x186a0, w, D(G(), 0x748) + 0xc24);
    }
    const uint32_t* items = static_cast<const uint32_t*>(P(orig::fn_004a1630(D(G(), 0x748) + 0xc24)));
    for (uint32_t i = 0; i < 5; ++i)
    {
        if (items[i] == 0xffffffffu)
            continue;
        fn_00469270(o, items[i]);
        const uint32_t p = D(o, 0x8c);
        if (!p || static_cast<uint8_t>(fn_0046a5e0(p)))
            orig::fn_004a13a0(0x186a0, items[i], D(G(), 0x748) + 0xc24);
    }
    if (B(G(), 0xd90))
        fn_00469450(0xb, o);

    // The pub's special weapon.
    D(o, 0xa0) = 0xffffffffu;
    D(o, 0xa8) = 0xffffffffu;
    D(o, 0xa4) = GameGlobal<uint32_t>(0x00604a98);
    if (D(o, 0x8c))
    {
        if (B(P(D(o, 0x8c)), 0x466))
        {
            if (!B(G(), 0xd90))
                D(o, 0xa8) = 0;
            B(G(), 0xd90) = 1;
        }
        void* p = P(D(o, 0x8c));
        const uint32_t special = D(p, 0x468);
        if (special != 0xffffffffu)
        {
            uint32_t k = 0;
            while (k < 5 && items[k] != special)
                ++k;
            if (k >= 5)
            {
                D(o, 0xa0) = special;
                if (D(o, 0xa8) == 0xffffffffu)
                    D(o, 0xa8) = 5;
                orig::fn_004a14e0(0x2710, D(p, 0x468), D(G(), 0x748) + 0xc24);
            }
            else if (!p || static_cast<uint8_t>(fn_0046a5e0(u(p))))
                orig::fn_004a14e0(0x2710, special, D(G(), 0x748) + 0xc24);
        }
    }

    // Hide the special-weapon / kind 6..10 objects, show the notifications.
    {
        const uint32_t r = orig::fn_00463ad0(0, D(G(), 0xa70), u(G()) + 0x3640);
        D(o, 0xac) = r;
        if (r)
            D(P(r), 0x474) &= 0xfffffffbu;
    }
    {
        const uint32_t r = orig::fn_00463890(u(G()) + 0x3660, D(G(), 0xa70));
        D(o, 0xb0) = r;
        if (r)
        {
            D(P(r), 0x474) &= 0xfffffffbu;
            orig::fn_00465280(0x1b, D(o, 0xb0), 0x23);
        }
    }
    D(o, 0xb4) = 0;
    if (D(o, 0xa0) != 0xffffffffu)
    {
        D(o, 0xb4) = orig::fn_00463ad0(0, D(G(), 0xa70), D(o, 0xa0) * 0x150 + u(G()) + 0x27d0);
        const uint32_t r = orig::fn_00463890(D(o, 0xa0) * 0x150 + u(G()) + 0x27f0, D(G(), 0xa70));
        D(o, 0xb8) = r;
        if (r)
            orig::fn_00465280(0x1b, r, 0x23);
    }
    for (uint32_t off = 0x7e0; off <= 0xd20; off += 0x150)
    {
        uint32_t r = orig::fn_00463ad0(0, D(G(), 0xa70), off + u(G()) + 0x27d0);
        if (r)
            D(P(r), 0x474) &= 0xfffffffbu;
        r = orig::fn_00463890(off + u(G()) + 0x27f0, D(G(), 0xa70));
        if (r)
        {
            D(P(r), 0x474) &= 0xfffffffbu;
            orig::fn_00465280(0x1b, r, 0x23);
        }
    }
    static const uint32_t kNotify[3][2] = {{0x005a0b2c, 0xc0}, {0x005a0b18, 0xc4}, {0x005a0b04, 0xc8}};
    for (uint32_t i = 0; i < 3; ++i)
    {
        const uint32_t r = orig::fn_00463890(kNotify[i][0], D(G(), 0xa70));
        D(o, kNotify[i][1]) = r;
        if (r)
        {
            D(P(r), 0x474) &= 0xfffffffbu;
            orig::fn_00465280(0x1b, D(o, kNotify[i][1]), 0x23);
        }
    }
    {
        const uint32_t r = orig::fn_00463890(0x005a0af4, D(G(), 0xa70));
        D(o, 0xcc) = r;
        if (r)
        {
            D(P(r), 0x474) |= 4;
            orig::fn_00465280(0x1c, D(o, 0xcc), 0x23);
        }
    }

    // Health from the pool.
    g = G();
    const uint32_t player = D(g, 0x748);
    const float health = orig::fn_004555b0(player);
    const float maxHealth = orig::fn_004555c0(player);  // kept in the argument slot [esp+0x1c]
    const float pool = D(o, 0x8c) ? PubObject_GetHealthPool(P(D(o, 0x8c))) : GameGlobal<float>(0x005bcb60);  // 100
    const float sum = pool + health;
    const float healed = maxHealth < sum ? maxHealth : sum;
    orig::fn_0047af50(player, f2u(healed));
    F(o, 0x98) = maxHealth < sum ? maxHealth : sum;
    if (D(o, 0x8c))
    {
        const float now = orig::fn_004555b0(D(G(), 0x748));
        PubObject_SetHealthPool(P(D(o, 0x8c)), f2u(pool - (now - health)));
    }
    F(o, 0x14c) = pool;
    F(o, 0x150) = health;
    F(P(D(o, 0x88)), 0x4a0) = health / orig::fn_004555c0(D(G(), 0x748));
    if (D(o, 0x8c))
        F(P(D(o, 0x84)), 0x4a0) = F(o, 0x14c) / F(P(D(o, 0x8c)), 0x470);
    else
        F(P(D(o, 0x84)), 0x4a0) = F(o, 0x14c) * GameGlobal<float>(0x005bcbec);  // 0.01
    if (orig::fn_00475d30(D(G(), 0x748)))
    {
        const float v = orig::fn_004555c0(orig::fn_00475d30(D(G(), 0x748)));
        F(P(orig::fn_00475d30(D(G(), 0x748))), 0x454) = v;
    }
    if (orig::fn_00475d40(D(G(), 0x748)))
    {
        const float v = orig::fn_004555c0(orig::fn_00475d40(D(G(), 0x748)));
        F(P(orig::fn_00475d40(D(G(), 0x748))), 0x454) = v;
    }

    // Level-name flags.
    auto levelIs = [](uint32_t prefix) {
        const uint32_t name = orig::fn_0044a660(D(G(), 0xa64), 1);
        return GameFunction<int(__cdecl*)(uint32_t, uint32_t, uint32_t)>(0x00544957)(prefix, name, 5) == 0;
    };
    if (levelIs(0x005a0aec) || levelIs(0x005a0ae4))
        B(o, 0x146) = 1;
    else if (levelIs(0x005a0adc) || levelIs(0x005a0ad4) || levelIs(0x005a0acc))
        B(o, 0x147) = 1;
    if (levelIs(0x005a0ae4) || levelIs(0x005a0adc) || levelIs(0x005a0ad4) || levelIs(0x005a0acc))
        B(o, 0x148) = 1;

    D(o, 0x90) = orig::fn_00468dd0(u(o));
    D(o, 0x94) = VCall<uint32_t>(P(D(G(), 0x2c)), 0xd0 / 4, 0x005a0abcu, 0u, 1u, 0u, 0xffffffffu, 0u);
    VCall<uint32_t>(P(D(G(), 0x2c)), 0xd0 / 4, 0x005a0aacu, 0u, 0u, 0u, 0xffffffffu, 0u);
    if (iter[1])
        --Field<int32_t>(P(iter[1]), 0x18);
    return iter[1];
}
static uint32_t fn_00467fe0_Abi(uint32_t a0)
{
    return fn_00467fe0(P(a0));
}
ABI_ENTRY_00467fe0(fn_00467fe0_Entry, fn_00467fe0_Abi)
HOOK(0x00467fe0, "fn_00467fe0", fn_00467fe0_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// Template property readers used by the Load methods below (all original): each stores the default first, then
// looks the property up (game+0xa84 templates, falling back to the object's own data). Register use as observed:
//   0x004533a0 string (eax = default, edi = dest; obj, name, template, a3, 0)
//   0x00453400 int    (eax = default, esi = a3, edi = dest; obj, name, template, 0)
//   0x00453450 int    (eax = default, ebx = name, esi = a3, edi = dest; obj, template, 0)
//   0x004534a0 float  (esi = a3, edi = dest; obj, name, template, default, 0)
//   0x004534f0 bool   (al = default, esi = a3, edi = dest; obj, name, template, 0)
// (guess: a3 is the instance's property block).

// 0x0046a200 (ecx = pub; stack: template, a1 (placement matrix), a2, a3; ret 0x10). PubObject (vtable 0x005a1dc8)
// slot 6, Load: global 0x005fe5d0 = pub, +0x4cc = 0, the base Load 0x004517b0, vt+0xfc(1, 2); reads "has_machinegun"
// .. "has_sharkgun" into the bytes +0x460..+0x465 (non-zero) and the special weapons "has_guyfawkes",
// "has_bunchoffives", "has_topsyturvy", "has_blackhole", "has_stickybomb" into +0x468 (6..10, -1 for none; the last
// set wins); more than one special weapon logs "Pub -- New rule: only 1 special weapon ..." with the placement
// (a1 +0xc, +0x1c; 0 without one). Then "should_refill_ammo" (+0x467, default 1), "has_tickerbomb" (+0x466),
// "health_pool" (+0x46c, default 100, copied to +0x470), "health_bar_rate" (+0x474, default 50, times 0.01),
// "doorway_bone_name" (+0x478), "doorway_entry_radius" (+0x498, default 40), "player_exit_bone_name" (+0x49c), the
// triggers "send_trigger_entry_failure" (+0x4bc), "send_trigger_entry" (+0x4c0), "send_trigger_exit" (+0x4c4),
// "listen_trigger_open" (+0x4c8); finally 0x004546d0 (esi = object system +0xe90 / +0xe94 / +0xeac / +0xe9c /
// +0xea0, edi = pub) registers the pub in five lists. Returns the last 0x004546d0 eax. Not executed in recorded runs.
uint32_t PubObject_Load(void* pub, uint32_t templ, uint32_t a1, uint32_t a2, uint32_t a3)
{
    const uint32_t p = u(pub);
    GameGlobal<uint32_t>(0x005fe5d0) = p;
    D(pub, 0x4cc) = 0;
    orig::fn_004517b0(p, templ, a1, a2, a3);
    VCall<uint32_t>(pub, 0xfc / 4, 1u, 2u);
    // Original frame: 11 dwords at [esp+0x10] (sub esp, 0x2c).
    uint32_t has[11];
    static const uint32_t kHas[11] = {0x005a07f8, 0x005a07e8, 0x005a07dc, 0x005a07cc, 0x005a07c0, 0x005a07b0,
                                      0x005a07a0, 0x005a078c, 0x005a077c, 0x005a076c, 0x005a075c};
    uint32_t r = 0;
    for (int i = 0; i < 11; ++i)
        r = orig::fn_00453400(0, a3, u(&has[i]), p, kHas[i], a2, 0);
    orig::fn_004534f0((r & 0xffffff00u) | 1, a3, p + 0x467, p, 0x005a0748, a2, 0);
    D(pub, 0x468) = 0xffffffffu;
    B(pub, 0x461) = has[1] != 0;
    B(pub, 0x460) = has[0] != 0;
    B(pub, 0x462) = has[2] != 0;
    B(pub, 0x463) = has[3] != 0;
    B(pub, 0x464) = has[4] != 0;
    B(pub, 0x465) = has[5] != 0;
    int specials = 0;
    for (uint32_t k = 6; k <= 10; ++k)
    {
        if (has[k])
        {
            D(pub, 0x468) = k;
            ++specials;
        }
    }
    if (specials > 1)
    {
        float x = kZero, y = kZero;
        if (a1)
        {
            x = F(P(a1), 0xc);
            y = F(P(a1), 0x1c);
        }
        GameFunction<void(__cdecl*)(uint32_t, uint32_t, double, double)>(0x00407ff0)(Field<uint32_t>(G(), 0x34),
                                                                                      0x005a06d0, x, y);
    }
    orig::fn_004534f0(0, a3, p + 0x466, p, 0x005a06bc, a2, 0);
    orig::fn_004534a0(a3, p + 0x46c, p, 0x005a06b0, a2, 0x42c80000, 0);
    D(pub, 0x470) = D(pub, 0x46c);
    orig::fn_004534a0(a3, p + 0x474, p, 0x005a06a0, a2, 0x42480000, 0);
    F(pub, 0x474) = F(pub, 0x474) * GameGlobal<float>(0x005bcbec);  // 0.01
    orig::fn_004533a0(0x0058a9a4, p + 0x478, p, 0x005a068c, a2, a3, 0);
    orig::fn_004534a0(a3, p + 0x498, p, 0x005a0674, a2, 0x42200000, 0);
    orig::fn_004533a0(0x0058a9a4, p + 0x49c, p, 0x005a065c, a2, a3, 0);
    orig::fn_00453450(0, 0x005a0640, a3, p + 0x4bc, p, a2, 0);
    orig::fn_00453450(0, 0x005a062c, a3, p + 0x4c0, p, a2, 0);
    orig::fn_00453450(0, 0x005a0618, a3, p + 0x4c4, p, a2, 0);
    orig::fn_00453450(0, 0x005a0604, a3, p + 0x4c8, p, a2, 0);
    static const uint32_t kLists[5] = {0xe90, 0xe94, 0xeac, 0xe9c, 0xea0};
    uint32_t last = 0;
    for (int i = 0; i < 5; ++i)
        last = static_cast<uint32_t>(orig::fn_004546d0(D(P(D(G(), 0xa84)), kLists[i]), p));
    return last;
}
static uint32_t PubObject_Load_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return PubObject_Load(P(ecx), a0, a1, a2, a3);
}
ABI_ENTRY_0046a200(PubObject_Load_Entry, PubObject_Load_Abi)
HOOK(0x0046a200, "PubObject_Load", PubObject_Load_Entry, nullptr);

// 0x0046acf0 (ecx = object; stack: template, a1, a2, a3; ret 0x10). Slot 6 (Load) of vtable 0x005aa750 (class id
// 150; its descriptor is the base "CLASSV(Object)"): the base Load 0x004517b0, then "text_trigger_receive_01..05"
// (+0x460..+0x470), "text_display_01..05" (strings of 0x20 bytes at +0x474..+0x4f4), "text_display_time" (+0x528,
// default 5.0); each display string goes through Text_Lookup 0x00495f00 (eax = string, ebx = game+0xac0; 0) into
// +0x514..+0x524; then "objective_trigger_receive_01..05" (+0x52c..+0x53c) and "objective_tag_01..05"
// (+0x540..+0x550); finally vt+0xf4(0), whose eax is returned. Guess: a mission text / objective object. Not
// executed in recorded runs.
uint32_t fn_0046acf0(void* obj, uint32_t templ, uint32_t a1, uint32_t a2, uint32_t a3)
{
    const uint32_t p = u(obj);
    orig::fn_004517b0(p, templ, a1, a2, a3);
    static const uint32_t kTextTriggers[5] = {0x005a05c8, 0x005a05b0, 0x005a0598, 0x005a0580, 0x005a0568};
    for (uint32_t i = 0; i < 5; ++i)
        orig::fn_00453450(0, kTextTriggers[i], a3, p + 0x460 + i * 4, p, a2, 0);
    static const uint32_t kTexts[5] = {0x005a0558, 0x005a0548, 0x005a0538, 0x005a0528, 0x005a0518};
    for (uint32_t i = 0; i < 5; ++i)
        orig::fn_004533a0(0x0058a9a4, p + 0x474 + i * 0x20, p, kTexts[i], a2, a3, 0);
    orig::fn_004534a0(a3, p + 0x528, p, 0x005a0504, a2, 0x40a00000, 0);
    for (uint32_t i = 0; i < 5; ++i)
        D(obj, 0x514 + i * 4) = orig::fn_00495f00(p + 0x474 + i * 0x20, D(G(), 0xac0), 0);
    static const uint32_t kObjective[10] = {0x005a04e4, 0x005a04c4, 0x005a04a4, 0x005a0484, 0x005a0464,
                                            0x005a0450, 0x005a043c, 0x005a0428, 0x005a0414, 0x005a0400};
    for (uint32_t i = 0; i < 10; ++i)
        orig::fn_00453450(0, kObjective[i], a3, p + 0x52c + i * 4, p, a2, 0);
    return VCall<uint32_t>(obj, 0xf4 / 4, 0u);
}
static uint32_t fn_0046acf0_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_0046acf0(P(ecx), a0, a1, a2, a3);
}
ABI_ENTRY_0046acf0(fn_0046acf0_Entry, fn_0046acf0_Abi)
HOOK(0x0046acf0, "fn_0046acf0", fn_0046acf0_Entry, nullptr);

// 0x0046afa0 (ecx = object; stack: sender, trigger; ret 8). Slot 0xe (trigger handler) of vtable 0x005aa750 (the
// 0x0046acf0 class). A trigger matching "objective_trigger_receive_0N" (+0x52c..) with a non-zero
// "objective_tag_0N" (+0x540..) marks an objective (positive tags go to game+0x2658). Then for each matching
// "text_trigger_receive_0N" (+0x460..) the looked-up text (+0x514..) is shown by 0x00403410 (ecx = game; text,
// flags): flags 3, or 0x13 after 0x0044f560 (eax = game+0xa88, cl = 1) when an objective was marked. Returns al = 0
// (upper eax bytes 0). Not executed in recorded runs.
uint32_t fn_0046afa0(void* obj, uint32_t /*sender*/, uint32_t trigger)
{
    void* g = G();
    bool objective = false;
    for (uint32_t k = 0; k < 5; ++k)
    {
        if (trigger != D(obj, 0x52c + k * 4))
            continue;
        const int32_t tag = Field<int32_t>(obj, 0x540 + k * 4);
        if (!tag)
            continue;
        if (tag > 0)
            Field<int32_t>(g, 0x2658) = tag;
        objective = true;
    }
    for (uint32_t k = 0; k < 5; ++k)
    {
        if (trigger != D(obj, 0x460 + k * 4))
            continue;
        uint32_t flags = 3;
        if (objective)
        {
            orig::fn_0044f560(D(g, 0xa88), 1);
            flags = 0x13;
        }
        orig::fn_00403410(u(g), D(obj, 0x514 + k * 4), flags);
        g = G();
    }
    return 0;
}
static uint32_t fn_0046afa0_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return fn_0046afa0(P(ecx), a0, a1);
}
ABI_ENTRY_0046afa0(fn_0046afa0_Entry, fn_0046afa0_Abi)
HOOK(0x0046afa0, "fn_0046afa0", fn_0046afa0_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// 0x00471810 (eax = point a, ecx = point b, edx = direction, esi = out vector; stack: radius, strength, a2, a3, power;
// caller pops; returns st0; caller 0x004788a0). With d = a - b (locals [esp+4..+0xc]) and dist = |d|: strength'
// = strength, scaled by radius / dist beyond a non-zero radius; c = (d . direction) / dist clamped to [0, 1] (0 for
// dist <= 1e-4); s = sqrt(1 - c * c) clamped to [0, 1]. With strength' = 0 the out vector is zero and 0.0 is
// returned; otherwise k = |s ^ power| * strength' (_CIpow 0x00566790), out = d * k, and a2 / strength' is returned.
// Guess: a cone-shaped push (the pub doorway). a3 is not read.
float fn_00471810(const float* a, const float* b, const float* dir, float* out, float radius, float strength, float a2,
                  uint32_t /*a3*/, float power)
{
    const float dx = a[0] - b[0];
    const float dy = a[1] - b[1];
    const float dz = a[2] - b[2];
    float k0 = strength;
    float dist;
    {
        const float sq = dz * dz + dy * dy + dx * dx;
        __asm {
            fld sq
            fsqrt
            fstp dist
        }
    }
    if (radius != kZero && dist > radius)
        k0 = radius / dist * strength;
    float c;
    if (!(dist > GameGlobal<float>(0x005bcbb0)))  // 1e-4
        c = kZero;
    else
    {
        c = (dz * dir[2] + dy * dir[1] + dx * dir[0]) / dist;
        if (c < kZero)
            c = kZero;
        else if (c > kOne)
            c = kOne;
    }
    float s;
    {
        const float t = kOne - c * c;
        __asm {
            fld t
            fsqrt
            fstp s
        }
    }
    if (s < kZero)
        s = 0.0f;
    else if (s > kOne)
        s = 1.0f;
    if (k0 == kZero)
    {
        out[2] = 0.0f;
        out[1] = 0.0f;
        out[0] = 0.0f;
        return kZero;
    }
    float p = CIpow(s, power);
    p = (p < 0.0f ? -p : p) * k0;
    out[0] = dx * p;
    out[1] = dy * p;
    out[2] = p * dz;
    return a2 / k0;
}
static float fn_00471810_Abi(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t esi, uint32_t a0, uint32_t a1,
                             uint32_t a2, uint32_t a3, uint32_t a4)
{
    float r, s, t, w;
    std::memcpy(&r, &a0, 4);
    std::memcpy(&s, &a1, 4);
    std::memcpy(&t, &a2, 4);
    std::memcpy(&w, &a4, 4);
    return fn_00471810(static_cast<const float*>(P(eax)), static_cast<const float*>(P(ecx)),
                       static_cast<const float*>(P(edx)), static_cast<float*>(P(esi)), r, s, t, a3, w);
}
ABI_ENTRY_00471810(fn_00471810_Entry, fn_00471810_Abi)
HOOK(0x00471810, "fn_00471810", fn_00471810_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// PickupGenerator (vtable 0x005a6258 "CLASS(PickupGenerator)").

// 0x00471b70 (ecx = generator; stack: template, a1, a2, a3; ret 0x10). Slot 6, Load: the base Load 0x004517b0, then
// "delay_time" (+0x464, default 10.0), "turbo_boost" (byte +0x46c), "generated_ammo_name" (string +0x470),
// "listen_activate_trigger" (+0x468); +0x460 (countdown) = 0, +0x490 (the generated pickup) = 0, byte +0x46d = 0;
// vt+0xf4(0), whose eax is returned. Not executed in recorded runs.
uint32_t PickupGenerator_Load(void* gen, uint32_t templ, uint32_t a1, uint32_t a2, uint32_t a3)
{
    const uint32_t p = u(gen);
    orig::fn_004517b0(p, templ, a1, a2, a3);
    const uint32_t r = orig::fn_004534a0(a3, p + 0x464, p, 0x005a0198, a2, 0x41200000, 0);
    orig::fn_004534f0(r & 0xffffff00u, a3, p + 0x46c, p, 0x005a018c, a2, 0);
    orig::fn_004533a0(0x0058a9a4, p + 0x470, p, 0x005a0178, a2, a3, 0);
    orig::fn_00453450(0, 0x005a0160, a3, p + 0x468, p, a2, 0);
    D(gen, 0x460) = 0;
    D(gen, 0x490) = 0;
    B(gen, 0x46d) = 0;
    return VCall<uint32_t>(gen, 0xf4 / 4, 0u);
}
static uint32_t PickupGenerator_Load_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return PickupGenerator_Load(P(ecx), a0, a1, a2, a3);
}
ABI_ENTRY_00471b70(PickupGenerator_Load_Entry, PickupGenerator_Load_Abi)
HOOK(0x00471b70, "PickupGenerator_Load", PickupGenerator_Load_Entry, nullptr);

// 0x00471c20 (stack: info; ret 4). Slot 7 of PickupGenerator and AnimPlayer (vtable 0x005ab788): info+0 = 0x4000.
// Returns info. Not executed in recorded runs.
uint32_t fn_00471c20(void* info)
{
    D(info, 0) = 0x4000;
    return u(info);
}
static uint32_t fn_00471c20_Abi(uint32_t a0)
{
    return fn_00471c20(P(a0));
}
ABI_ENTRY_00471c20(fn_00471c20_Entry, fn_00471c20_Abi)
HOOK(0x00471c20, "fn_00471c20", fn_00471c20_Entry, nullptr);

// 0x00471c30 (ecx = generator; stack: sender, trigger; ret 8). Slot 0xe, the trigger handler: a trigger equal to the
// non-zero +0x468 ("listen_activate_trigger") clears it (activates the generator). Returns al = 0, the upper eax
// bytes from +0x468. Not executed in recorded runs.
uint32_t PickupGenerator_OnTrigger(void* gen, uint32_t /*sender*/, uint32_t trigger)
{
    const uint32_t listen = D(gen, 0x468);
    if (listen && trigger == listen)
        D(gen, 0x468) = 0;
    return listen & 0xffffff00u;
}
static uint32_t PickupGenerator_OnTrigger_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return PickupGenerator_OnTrigger(P(ecx), a0, a1);
}
ABI_ENTRY_00471c30(PickupGenerator_OnTrigger_Entry, PickupGenerator_OnTrigger_Abi)
HOOK(0x00471c30, "PickupGenerator_OnTrigger", PickupGenerator_OnTrigger_Entry, nullptr);

// 0x00471c50 (ecx = generator). Slot 0x37 (vt+0xdc, remove): the generated pickup +0x490, if any, gets vt+0xd4(0)
// when its vt+0xd8() is true and its reference released (ReleaseRef); then a tail jump to Object_Remove 0x004547b0
// (ecx = generator), whose eax is returned. Not executed in recorded runs.
uint32_t PickupGenerator_Remove(void* gen)
{
    uint32_t* slot = &D(gen, 0x490);
    if (*slot)
    {
        if (static_cast<uint8_t>(VCall<uint32_t>(P(*slot), 0xd8 / 4)))
            VCall<uint32_t>(P(*slot), 0xd4 / 4, 0u);
        ReleaseRef(slot);
    }
    return orig::fn_004547b0(u(gen));
}
static uint32_t PickupGenerator_Remove_Abi(uint32_t ecx)
{
    return PickupGenerator_Remove(P(ecx));
}
ABI_ENTRY_00471c50(PickupGenerator_Remove_Entry, PickupGenerator_Remove_Abi)
HOOK(0x00471c50, "PickupGenerator_Remove", PickupGenerator_Remove_Entry, nullptr);

// 0x00471cb0 (ecx = generator). Slot 0x2f, update. Waits while +0x468 (activate trigger) is pending (returns it).
// A generated pickup +0x490 whose vt+0xd8() is false is released; while one exists nothing else happens. The
// countdown +0x460 runs down by the frame time; once it is at or below 0 (or before the first pickup, byte +0x46d
// clear) the template is chosen: "pickup_turbo_boost" for turbo generators unless 0x0047b8e0 (eax = player) is true;
// otherwise "generated_ammo_name" (+0x470) - after the first pickup only when 0x00475c70 (eax = player, ecx = game;
// name) accepts it - or, with no name, 0x00475c30 (edx = player). Without a template +0x460 = 1.0 (retry in a
// second); with one, +0x460 = delay (+0x464), byte +0x46d = 1, ObjectSystem_CreateObject(0, template; game+0xa84,
// 0, 0x3ff, 0, 0, 0, 0, 0xaa, 0) into +0x490 (+0x28 referenced) placed at the generator's position (vt+0x54) raised
// by 4.0 through 0x00457870 (al = 0, ecx = position, edx = pickup). Not executed in recorded runs.
uint32_t PickupGenerator_Update(void* gen)
{
    if (D(gen, 0x468))
        return D(gen, 0x468);
    uint32_t* slot = &D(gen, 0x490);
    if (*slot && !static_cast<uint8_t>(VCall<uint32_t>(P(*slot), 0xd8 / 4)))
        ReleaseRef(slot);
    if (*slot)
        return 0;
    const float t = F(gen, 0x460) - kFrameTime;
    F(gen, 0x460) = t;
    if (t > kZero && B(gen, 0x46d))
        return 0;
    uint32_t name;
    if (B(gen, 0x46c))
        name = static_cast<uint8_t>(orig::fn_0047b8e0(D(G(), 0x748))) ? 0u : 0x005b05ccu;
    else if (B(gen, 0x470))
    {
        name = u(gen) + 0x470;
        if (B(gen, 0x46d) && !static_cast<uint8_t>(orig::fn_00475c70(D(G(), 0x748), u(G()), name)))
            name = 0;
    }
    else
        name = orig::fn_00475c30(D(G(), 0x748));
    if (!name)
    {
        D(gen, 0x460) = 0x3f800000;
        return 0;
    }
    D(gen, 0x460) = D(gen, 0x464);
    B(gen, 0x46d) = 1;
    *slot = ObjectSystem_CreateObject(0, name, D(G(), 0xa84), 0, 0x3ff, 0, 0, 0, 0, 0xaa, 0);
    if (!*slot)
        return 0;
    ++Field<int32_t>(P(*slot), 0x28);
    float pos[3];
    const uint32_t r = VCall<uint32_t>(gen, 0x54 / 4, u(pos));
    pos[2] = pos[2] + GameGlobal<float>(0x005bcbe0);  // 4.0
    return static_cast<uint32_t>(orig::fn_00457870(r & 0xffffff00u, u(pos), *slot, 0, 0, 0));
}
static uint32_t PickupGenerator_Update_Abi(uint32_t ecx)
{
    return PickupGenerator_Update(P(ecx));
}
ABI_ENTRY_00471cb0(PickupGenerator_Update_Entry, PickupGenerator_Update_Abi)
HOOK(0x00471cb0, "PickupGenerator_Update", PickupGenerator_Update_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// PickupObject (17 vtables with slot 1 "CLASS(PickupObject)", e.g. 0x005abf48; class ids 25..42).

// 0x00471e40 (ecx = pickup; stack: template, a1, a2, a3; ret 0x10). Slot 6, Load: bytes +0x4bc..+0x4bf and +0x4b8 =
// 0, the base Load 0x00497460, then "upward_force" (+0x478, default 70), "text_lookup" (+0x47c), "num_ammo" (+0x49c,
// default -1; when -1, "num_health" with default 0), "linger_time" (+0x4a0, 10), "fade_time" (+0x4a8, 1),
// "rotate_rate" (+0x4b0, 180, then times pi/180); +0x4b4 (spin angle) = +0x4ac (fade start) = 0, +0x4a4 (spawn time)
// = the time 0x00604a98; 0x004546d0 (esi = object system +0xef4, edi = pickup) registers it, vt+0xf4(1), 0x00458440
// (eax = 0, ecx = &(0, 0, upward_force), edx = pickup) gives it its upward push, and the matrix +0x4c0 is set to
// identity (flags word 0). Returns 0x3f800000 (eax as left).
uint32_t PickupObject_Load(void* pk, uint32_t templ, uint32_t a1, uint32_t a2, uint32_t a3)
{
    const uint32_t p = u(pk);
    B(pk, 0x4bc) = 0;
    B(pk, 0x4bd) = 0;
    B(pk, 0x4bf) = 0;
    B(pk, 0x4be) = 0;
    D(pk, 0x4b8) = 0;
    orig::fn_00497460(p, templ, a1, a2, a3);
    orig::fn_004534a0(a3, p + 0x478, p, 0x005a0150, a2, 0x428c0000, 0);
    orig::fn_004533a0(0x0058a9a4, p + 0x47c, p, 0x005a0144, a2, a3, 0);
    orig::fn_00453400(0xffffffffu, a3, p + 0x49c, p, 0x005a0138, a2, 0);
    if (D(pk, 0x49c) == 0xffffffffu)
        orig::fn_00453400(0, a3, p + 0x49c, p, 0x005a012c, a2, 0);
    orig::fn_004534a0(a3, p + 0x4a0, p, 0x005a0120, a2, 0x41200000, 0);
    orig::fn_004534a0(a3, p + 0x4a8, p, 0x005a0114, a2, 0x3f800000, 0);
    orig::fn_004534a0(a3, p + 0x4b0, p, 0x005a0108, a2, 0x43340000, 0);
    F(pk, 0x4b0) = F(pk, 0x4b0) * GameGlobal<float>(0x005bcba0);  // pi / 180
    D(pk, 0x4b4) = 0;
    D(pk, 0x4ac) = 0;
    D(pk, 0x4a4) = GameGlobal<uint32_t>(0x00604a98);
    orig::fn_004546d0(D(P(D(G(), 0xa84)), 0xef4), p);
    VCall<uint32_t>(pk, 0xf4 / 4, 1u);
    uint32_t push[3] = {0, 0, D(pk, 0x478)};
    orig::fn_00458440(0, u(push), p);
    uint32_t* m = &D(pk, 0x4c0);
    std::memset(m, 0, 0x11 * 4);
    m[0] = m[5] = m[10] = m[15] = 0x3f800000;
    return 0x3f800000;
}
static uint32_t PickupObject_Load_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return PickupObject_Load(P(ecx), a0, a1, a2, a3);
}
ABI_ENTRY_00471e40(PickupObject_Load_Entry, PickupObject_Load_Abi)
HOOK(0x00471e40, "PickupObject_Load", PickupObject_Load_Entry, nullptr);

// 0x00471fd0 (ecx = pickup; stack: info; ret 4). Slot 7: 0x00457810 (eax = pickup); info+0 = 0x60000, bytes +0x79 /
// +0x7b = 1; the base 0x004527d0 (ecx = pickup; info); then 0x004574e0(pickup, info, (float)(unsigned)+0x470,
// ((byte +0x474 & 2) | 1) << 3), whose eax is returned.
uint32_t fn_00471fd0(void* pk, void* info)
{
    orig::fn_00457810(u(pk));
    D(info, 0) = 0x60000;
    B(info, 0x79) = 1;
    B(info, 0x7b) = 1;
    orig::fn_004527d0(u(pk), u(info));
    const uint32_t flags = ((B(pk, 0x474) & 2u) | 1u) << 3;
    const int32_t n = Field<int32_t>(pk, 0x470);
    double v = static_cast<double>(n);
    if (n < 0)
        v += GameGlobal<float>(0x005bcb28);  // 2^32
    return orig::fn_004574e0(u(pk), u(info), f2u(static_cast<float>(v)), flags);
}
static uint32_t fn_00471fd0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_00471fd0(P(ecx), P(a0));
}
ABI_ENTRY_00471fd0(fn_00471fd0_Entry, fn_00471fd0_Abi)
HOOK(0x00471fd0, "fn_00471fd0", fn_00471fd0_Entry, nullptr);

// 0x00472030 (ecx = pickup). Slot 0x37 (remove): releases the reference +0x4b8 (ReleaseRef) when set, then a tail
// jump to Object_Remove 0x004547b0 (ecx = pickup). Not executed in recorded runs.
uint32_t PickupObject_Remove(void* pk)
{
    if (D(pk, 0x4b8))
        ReleaseRef(&D(pk, 0x4b8));
    return orig::fn_004547b0(u(pk));
}
static uint32_t PickupObject_Remove_Abi(uint32_t ecx)
{
    return PickupObject_Remove(P(ecx));
}
ABI_ENTRY_00472030(PickupObject_Remove_Entry, PickupObject_Remove_Abi)
HOOK(0x00472030, "PickupObject_Remove", PickupObject_Remove_Entry, nullptr);

// 0x00472080 (eax = pickup, ebx = object; callers 0x0043e710, 0x00485910). Replaces the reference +0x4b8: the old
// one is released (ReleaseRef), the new one stored and referenced (+0x28 += 1). Returns the pickup (eax as left
// when nothing was released). Not executed in recorded runs.
uint32_t fn_00472080(void* pk, uint32_t ref)
{
    if (D(pk, 0x4b8))
        ReleaseRef(&D(pk, 0x4b8));
    D(pk, 0x4b8) = ref;
    if (ref)
        ++Field<int32_t>(P(ref), 0x28);
    return u(pk);
}
static uint32_t fn_00472080_Abi(uint32_t eax, uint32_t ebx)
{
    return fn_00472080(P(eax), ebx);
}
ABI_ENTRY_00472080(fn_00472080_Entry, fn_00472080_Abi)
HOOK(0x00472080, "fn_00472080", fn_00472080_Entry, nullptr);

// 0x004720c0 (ecx = pickup; stack: other; ret 4). Slot 0x28: false (al = 0) for the object held in +0x4b8,
// otherwise a tail jump to the base slot 0x00434500 (returns al = 1). The upper eax bytes come from `other`. Not
// executed in recorded runs.
uint32_t fn_004720c0(void* pk, uint32_t other)
{
    if (D(pk, 0x4b8) == other)
        return other & 0xffffff00u;
    return (other & 0xffffff00u) | (orig::fn_00434500(other) & 0xffu);
}
static uint32_t fn_004720c0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_004720c0(P(ecx), a0);
}
ABI_ENTRY_004720c0(fn_004720c0_Entry, fn_004720c0_Abi)
HOOK(0x004720c0, "fn_004720c0", fn_004720c0_Entry, nullptr);

// The spin of 0x004720e0 / 0x00472170: matrix `base` (from the given getter) times a z rotation by +0x4b4 into out
// (Matrix_Multiply 0x004e2910, eax = rotation; out, base). Original frame (sub esp, 0x8c): [8] angle, [0xc] rotation
// (0x44 bytes), [0x50] base (0x44 bytes).
static void SpinMatrix(void* pk, uint32_t getter, uint32_t out)
{
    uint32_t base[0x11];
    if (getter == 0x00457f80)
        orig::fn_00457f80(u(pk), u(base));
    else
        orig::fn_00457f40(u(pk), u(base));
    const float angle = F(pk, 0x4b4);
    const float s = X87Sin(angle);
    uint32_t rot[0x11] = {};
    rot[10] = 0x3f800000;
    rot[15] = 0x3f800000;
    rot[16] = 1;
    const float c = X87Cos(angle);
    float* r = reinterpret_cast<float*>(rot);
    r[0] = c;
    r[1] = -s;
    r[4] = s;
    r[5] = c;
    orig::fn_004e2910(u(rot), out, u(base));
}

// 0x004720e0 (ecx = pickup; stack: out matrix; ret 4). Slot 0x1c: the pickup's matrix (0x00457f80) spun about z by
// +0x4b4. Not executed in recorded runs.
uint32_t fn_004720e0(void* pk, uint32_t out)
{
    SpinMatrix(pk, 0x00457f80, out);
    return 0;
}
static uint32_t fn_004720e0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_004720e0(P(ecx), a0);
}
ABI_ENTRY_004720e0(fn_004720e0_Entry, fn_004720e0_Abi)
HOOK(0x004720e0, "fn_004720e0", fn_004720e0_Entry, nullptr);

// 0x00472170 (ecx = pickup). Slot 0x1f: the matrix from 0x00457f40 spun about z by +0x4b4 into +0x4c0; returns
// pickup+0x4c0. Not executed in recorded runs.
uint32_t fn_00472170(void* pk)
{
    SpinMatrix(pk, 0x00457f40, u(pk) + 0x4c0);
    return u(pk) + 0x4c0;
}
static uint32_t fn_00472170_Abi(uint32_t ecx)
{
    return fn_00472170(P(ecx));
}
ABI_ENTRY_00472170(fn_00472170_Entry, fn_00472170_Abi)
HOOK(0x00472170, "fn_00472170", fn_00472170_Entry, nullptr);

// 0x00472200 (ecx = pickup; caller 0x00472a00). Slot 0x2f, update: spin angle +0x4b4 += frame time * rate (+0x4b0).
// Before fading (+0x4ac == 0): once now (0x00604a98) - spawn (+0x4a4) exceeds the linger time (+0x4a0) it removes
// itself (vt+0xdc) when the fade time (+0x4a8) is below 0.01, else starts the fade (+0x4ac = now). While fading,
// f = (now - +0x4ac) / fade time: vt+0x100(1 - f) below 1, else vt+0x100(0) and vt+0xdc(). Finally, when collected
// (byte +0x4bd) and vt+0xd8() is true, a tail call to vt+0xdc(). Returns 0.
uint32_t PickupObject_Update(void* pk)
{
    F(pk, 0x4b4) = kFrameTime * F(pk, 0x4b0) + F(pk, 0x4b4);
    const float now = GameGlobal<float>(0x00604a98);
    if (F(pk, 0x4ac) == kZero)
    {
        if (now - F(pk, 0x4a4) > F(pk, 0x4a0))
        {
            if (F(pk, 0x4a8) < GameGlobal<float>(0x005bcbec))  // 0.01
                VCall<uint32_t>(pk, 0xdc / 4);
            else
                D(pk, 0x4ac) = GameGlobal<uint32_t>(0x00604a98);
        }
    }
    else
    {
        const float f = (now - F(pk, 0x4ac)) / F(pk, 0x4a8);
        if (!(f >= kOne))
            VCall<uint32_t>(pk, 0x100 / 4, f2u(kOne - f));
        else
        {
            VCall<uint32_t>(pk, 0x100 / 4, 0u);
            VCall<uint32_t>(pk, 0xdc / 4);
        }
    }
    if (B(pk, 0x4bd) && static_cast<uint8_t>(VCall<uint32_t>(pk, 0xd8 / 4)))
        return VCall<uint32_t>(pk, 0xdc / 4);
    return 0;
}
static uint32_t PickupObject_Update_Abi(uint32_t ecx)
{
    return PickupObject_Update(P(ecx));
}
ABI_ENTRY_00472200(PickupObject_Update_Entry, PickupObject_Update_Abi)
HOOK(0x00472200, "PickupObject_Update", PickupObject_Update_Entry, nullptr);

// 0x004722e0 (ecx = pickup; stack: a0, a1, a2, a3; ret 0x10). Slot 0x2c: when a0 is 0, 0x00455860 (edi = pickup;
// 5.0, 20.0); then Object_setPhysicsEnabled 0x004577b0 (eax = pickup; 1), whose eax is returned. Not executed in
// recorded runs.
uint32_t fn_004722e0(void* pk, uint32_t a0, uint32_t /*a1*/, uint32_t /*a2*/, uint32_t /*a3*/)
{
    if (!a0)
        orig::fn_00455860(u(pk), 0x40a00000, 0x41a00000);
    return static_cast<uint32_t>(orig::fn_004577b0(u(pk), 1));
}
static uint32_t fn_004722e0_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_004722e0(P(ecx), a0, a1, a2, a3);
}
ABI_ENTRY_004722e0(fn_004722e0_Entry, fn_004722e0_Abi)
HOOK(0x004722e0, "fn_004722e0", fn_004722e0_Entry, nullptr);

// 0x00472310 (ecx = pickup; stack: flag, toucher; ret 8). Slot 0x2d, touched: ignored without the flag byte or once
// collected (byte +0x4bd). Touched by the player (game+0x748): when vt+0x138() (give to the player) is true, the
// "text_lookup" string (+0x47c) goes through Text_Lookup 0x00495f00 (ebx = game+0xac0; 0) to 0x0046aa60 (eax = text;
// game+0xaac, 1), the trigger +0x460 is sent (Object_SendTrigger) when set, and byte +0x4bd = 1. Touched by anything
// else: byte +0x4bd = 1 when vt+0x13c(toucher) is true. Returns the last call's eax (0 when ignored). Not executed in
// recorded runs.
uint32_t fn_00472310(void* pk, uint32_t flag, uint32_t toucher)
{
    if (!static_cast<uint8_t>(flag) || B(pk, 0x4bd))
        return 0;
    if (toucher == D(G(), 0x748))
    {
        uint32_t r = VCall<uint32_t>(pk, 0x138 / 4);
        if (!static_cast<uint8_t>(r))
            return r;
        const uint32_t text = orig::fn_00495f00(u(pk) + 0x47c, D(G(), 0xac0), 0);
        r = orig::fn_0046aa60(text, D(G(), 0xaac), 1);
        if (D(pk, 0x460))
            r = Object_SendTrigger(P(D(G(), 0xa84)), D(pk, 0x460), pk);
        B(pk, 0x4bd) = 1;
        return r;
    }
    const uint32_t r = VCall<uint32_t>(pk, 0x13c / 4, toucher);
    if (static_cast<uint8_t>(r))
        B(pk, 0x4bd) = 1;
    return r;
}
static uint32_t fn_00472310_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return fn_00472310(P(ecx), a0, a1);
}
ABI_ENTRY_00472310(fn_00472310_Entry, fn_00472310_Abi)
HOOK(0x00472310, "fn_00472310", fn_00472310_Entry, nullptr);

// 0x004723c0 (esi = pickup; caller 0x0042a500). Returns 1 when byte +0x474 bit 1 is set, byte +0x4bc is clear,
// 0x00454be0 (ecx = pickup) is true and 0x00454c20 (ecx = pickup) is false; else 0 (al; the original leaves the
// upper eax bytes). Not executed in recorded runs.
uint32_t fn_004723c0(void* pk)
{
    if ((B(pk, 0x474) & 2) && !B(pk, 0x4bc) && static_cast<uint8_t>(orig::fn_00454be0(u(pk))) &&
        !static_cast<uint8_t>(orig::fn_00454c20(u(pk))))
        return 1;
    return 0;
}
static uint32_t fn_004723c0_Abi(uint32_t esi)
{
    return fn_004723c0(P(esi));
}
ABI_ENTRY_004723c0(fn_004723c0_Entry, fn_004723c0_Abi)
HOOK(0x004723c0, "fn_004723c0", fn_004723c0_Entry, nullptr);

// 0x004723f0 (eax = pickup, cl = value; callers 0x0042a500, 0x00432730, 0x0043ee90). Byte +0x4bc = cl. Not
// executed in recorded runs.
uint32_t fn_004723f0(void* pk, uint32_t value)
{
    B(pk, 0x4bc) = static_cast<uint8_t>(value);
    return u(pk);
}
static uint32_t fn_004723f0_Abi(uint32_t eax, uint32_t ecx)
{
    return fn_004723f0(P(eax), ecx);
}
ABI_ENTRY_004723f0(fn_004723f0_Entry, fn_004723f0_Abi)
HOOK(0x004723f0, "fn_004723f0", fn_004723f0_Entry, nullptr);

// Ammo pickups, slot 0x4e (collect) of six PickupObject vtables: 0x004a13a0 (eax = count +0x49c "num_ammo", ecx =
// weapon; player+0xc24) gives the ammo; when it took it, Pickup_PlayAmmoSound 0x00472400 and al = 1, else al = 0
// (upper eax bytes from the last call). None is executed in recorded runs.
static uint32_t GiveAmmo(void* pk, uint32_t weapon)
{
    const uint32_t r = orig::fn_004a13a0(D(pk, 0x49c), weapon, D(G(), 0x748) + 0xc24);
    if (!static_cast<uint8_t>(r))
        return r & 0xffffff00u;
    return (orig::fn_00472400() & 0xffffff00u) | 1u;
}
#define AMMO_PICKUP(addr, weapon)                                                                                      \
    uint32_t fn_##addr(void* pk)                                                                                       \
    {                                                                                                                  \
        return GiveAmmo(pk, weapon);                                                                                   \
    }                                                                                                                  \
    static uint32_t fn_##addr##_Abi(uint32_t ecx)                                                                      \
    {                                                                                                                  \
        return fn_##addr(P(ecx));                                                                                      \
    }                                                                                                                  \
    ABI_ENTRY_##addr(fn_##addr##_Entry, fn_##addr##_Abi)

// 0x00472470: weapon 0 (vtable 0x005abf48, class id 25).
AMMO_PICKUP(00472470, 0)
HOOK(0x00472470, "fn_00472470", fn_00472470_Entry, nullptr);
// 0x004724a0: weapon 2 (vtable 0x005a94e0, class id 26).
AMMO_PICKUP(004724a0, 2)
HOOK(0x004724a0, "fn_004724a0", fn_004724a0_Entry, nullptr);
// 0x004724e0: weapon 1 (vtable 0x005a5be8, class id 27).
AMMO_PICKUP(004724e0, 1)
HOOK(0x004724e0, "fn_004724e0", fn_004724e0_Entry, nullptr);
// 0x00472520: weapon 3 (vtable 0x005a3120, class id 28).
AMMO_PICKUP(00472520, 3)
HOOK(0x00472520, "fn_00472520", fn_00472520_Entry, nullptr);
// 0x00472560: weapon 4 (vtable 0x005aead0, class id 29).
AMMO_PICKUP(00472560, 4)
HOOK(0x00472560, "fn_00472560", fn_00472560_Entry, nullptr);
// 0x004725a0: weapon 5 (vtable 0x005a5970, class id 30).
AMMO_PICKUP(004725a0, 5)
HOOK(0x004725a0, "fn_004725a0", fn_004725a0_Entry, nullptr);
