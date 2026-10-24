// Recovered: the fx system (fx instances, Fx_Create, fx script opcode helpers, emitters, fx lights and fog),
// 0x0041bd30 .. 0x00420670. Static recovery 2026-10-05, compile-checked only (the game was not run); the oracle
// (tools/compare.py) is still to do. docs/subsystems/weather.md describes the opcodes and fx objects.
//
// Terms used below (evidence in each function's comment):
//  - fx system: Game+0xa90, vtable 0x005b7e0c (ctor 0x0041c700). +4 list of fx instances, +0xc / +0x10 / +0x14
//    the loaded fx bank (+0x10 records, +0x14 name index), +0x1c..+0x24 three particle pools.
//  - fx instance: 0x128 bytes, vtable 0x005b7df4 (0x0041cc20, "fxData"). +4 script record, +0xc start time,
//    +0x10 state (3 = dead), +0x14 flags, +0x18 position, +0x24 name (0x20 chars), +0x44 attach-point name,
//    +0x68 -> it or null, +0x6c / +0xb4 referenced objects (refcount +0x28), +0x70 0x44-byte matrix,
//    +0xb8 owner attach-point name, +0xd8 -> it or null, +0x124 emitter list.
//  - fxpush: the script's current created object record; +4 = the fx object (class `fx`, vtable 0x005a52d8).
//  - The `fx` argument of the opcode helpers (used for their error messages) is the fx system as the script
//    interpreter 0x00420770 passes it: FxBank_Find takes it as the system, and the messages print [fx+8] + 0x24, i.e.
//    (guess) the instance being run, whose name is at +0x24.
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
uint32_t f2u(float f)
{
    uint32_t v;
    std::memcpy(&v, &f, 4);
    return v;
}
float u2f(uint32_t v)
{
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}
void* p(uint32_t v)
{
    return reinterpret_cast<void*>(v);
}
constexpr uint32_t kHeap = 0x0063a210;     // allocator object for 0x004e07f0 (alloc) / 0x004e08d0 (free)
constexpr uint32_t kNullName = 0x005bba94;  // compared with _stricmp: the "null" attach point (weather.md)

float Zero()
{
    return GameGlobal<float>(0x0056c05c);
}
float FrameDt()
{
    return GameGlobal<float>(0x00604a90);
}
float ColourScale()
{
    return GameGlobal<float>(0x005bcc18);
}
// 0x00407ff0 (cdecl: console Game+0x34, level, format, argument): the game's console/log print.
void ConsolePrint(uint32_t level, uint32_t format, uint32_t arg)
{
    orig::fn_00407ff0(Field<uint32_t>(g_game, 0x34), level, format, arg);
}
// The script-error message every opcode helper prints when there is no fxpush: the fx's name is the record's
// name at [fx+8] + 0x24.
void FxPushError(void* fx, uint32_t format)
{
    ConsolePrint(0xa, format, Field<uint32_t>(fx, 8) + 0x24);
}
void Free(void* block)
{
    orig::fn_004e08d0(kHeap, u(block));
}
}  // namespace

uint32_t Fx_Create(bool force, void* sys, const char* name, uint32_t* params);
uint32_t Fx_Release(void* sys, void** slot);
uint32_t FxBank_Find(void* sys, const char* name);
uint32_t FxNameList_PickRandom(const uint32_t* list);

// ---------------------------------------------------------------------------------------------------------------
// 0x0041bd30 (eax = object): asks the object's position (virtual +0x54, vec3 local) and passes it with the
// object's +0x634 to the post-process manager (Game+0xa9c) via 0x0047c060 (eax = manager, ecx = 0, edx = position,
// esi = object+0x634; 0x0047c060 reads no stack argument although abi.csv lists 3). Returns its EAX.
uint32_t fn_0041bd30(void* obj)
{
    float pos[3];
    VCall<void>(obj, 0x54 / 4, static_cast<void*>(pos));
    return orig::fn_0047c060(Field<uint32_t>(g_game, 0xa9c), 0, u(pos), Field<uint32_t>(obj, 0x634), 0, 0, 0);
}

// 0x0041bf60 (esi = object with an fx at +0x470 name, +0x498 handle, +0x49c flags): (re)starts or stops the
// object's fx. Handle set: released (0x0041d030 with the fx system). Otherwise a 0x80-byte params block is built
// (flags 4 = owner when +0x49c bit 3, else 8 = position from virtual +0x54 into params +0xc) and Fx_Create runs
// (al = 0, ecx = Game+0xa90, name +0x470); with +0x49c bit 0 the params get 0x20 and +4 = 8 and the handle is
// kept. Afterwards +0x490 = +0x464 unless (bit 1 set and bit 0 clear), which clears +0x460 instead. Returns EAX
// as left (the flags word, or 0x0041d030's result).
uint32_t FxOwner_Toggle(void* obj)
{
    uint32_t params[0x20];
    uint32_t r;
    void** handle = &Field<void*>(obj, 0x498);
    if (*handle == nullptr)
    {
        if (Field<uint8_t>(obj, 0x49c) & 8)
        {
            params[7] = u(obj);
            params[6] = 0;
            params[0] = 4;
        }
        else
        {
            params[0] = 8;
            VCall<void>(obj, 0x54 / 4, static_cast<void*>(&params[3]));
        }
        if (Field<uint8_t>(obj, 0x49c) & 1)
        {
            params[0] |= 0x20;
            params[1] = 8;
            *handle = p(Fx_Create(false, Field<void*>(g_game, 0xa90),
                                  reinterpret_cast<const char*>(u(obj) + 0x470), params));
        }
        else
        {
            Fx_Create(false, Field<void*>(g_game, 0xa90), reinterpret_cast<const char*>(u(obj) + 0x470), params);
        }
        r = Field<uint32_t>(obj, 0x49c);
        if ((r & 2) && !(r & 1))
        {
            Field<uint32_t>(obj, 0x460) = 0;
            return r;
        }
    }
    else
    {
        r = Fx_Release(Field<void*>(g_game, 0xa90), handle);
        if (Field<uint8_t>(obj, 0x49c) & 2)
        {
            Field<uint32_t>(obj, 0x460) = 0;
            return r;
        }
    }
    Field<uint32_t>(obj, 0x490) = Field<uint32_t>(obj, 0x464);
    return r;
}

// 0x0041c050 (ecx = object; stack: a0 (unused), message; callee pops 8; vtable method, no direct callers): when
// message == +0x460: a pending flag 4 in +0x49c is cleared; else with no fx handle (+0x498): a delay +0x494 > 0 is
// cancelled (set to 0), else when the cool-down +0x490 <= 0 a new delay = 0x0046e2c0(ecx = flags; +0x468, +0x46c)
// (random range, guess) and if that is <= 0 the fx toggles at once (FxOwner_Toggle); with cool-down > 0 and
// flag bit 0, flag 4 is set (pending). With an fx handle the fx toggles. Returns AL = 0. Not hooked (see the
// adapters at the end of the file). Not hooked (see the end of
// the file).
uint32_t FxOwner_OnMessage(void* obj, uint32_t /*a0*/, uint32_t message)
{
    if (message != Field<uint32_t>(obj, 0x460))
        return message & ~0xffu;
    const uint32_t flags = Field<uint32_t>(obj, 0x49c);
    if (flags & 4)
    {
        Field<uint32_t>(obj, 0x49c) = flags & ~4u;
        return (flags & ~4u) & ~0xffu;
    }
    if (Field<void*>(obj, 0x498) == nullptr)
    {
        if (Field<float>(obj, 0x494) > Zero())
        {
            Field<float>(obj, 0x494) = 0.0f;
            return 0;
        }
        if (!(Field<float>(obj, 0x490) > Zero()) && !std::isnan(Field<float>(obj, 0x490)))
        {
            const float delay = orig::fn_0046e2c0(flags, Field<uint32_t>(obj, 0x468), Field<uint32_t>(obj, 0x46c));
            Field<float>(obj, 0x494) = delay;
            if (delay > Zero() || std::isnan(delay))
                return 0;
        }
        else
        {
            if (flags & 1)
                Field<uint32_t>(obj, 0x49c) |= 4;
            return 0;
        }
    }
    FxOwner_Toggle(obj);
    return 0;
}

// 0x0041c110 (ecx = object; vtable method): per-frame timers: the delay +0x494 counts down by the frame time
// (0x00604a90) and when it falls below 0 the fx toggles (FxOwner_Toggle); the cool-down +0x490 counts down while
// it is > 0.
uint32_t FxOwner_Update(void* obj)
{
    if (Field<float>(obj, 0x494) > Zero())
    {
        const float t = Field<float>(obj, 0x494) - FrameDt();
        Field<float>(obj, 0x494) = t;
        if (t < Zero())
            FxOwner_Toggle(obj);
    }
    if (Field<float>(obj, 0x490) > Zero())
        Field<float>(obj, 0x490) = Field<float>(obj, 0x490) - FrameDt();
    return 0;
}

// 0x0041c700 (edi = fx system; only caller the game constructor 0x004011e0): FxSystem constructor. vtable
// 0x005b7e0c; +4 = a 0x10-byte list object (0x004e07f0 alloc with tag 0x0058b5e8, built by 0x004e7010 with
// eax = Game+0xac4, edx = 0, esi = block) or null; +0xc .. +0x24 cleared. Returns the system.
uint32_t FxSystem_Construct(void* sys)
{
    Field<uint32_t>(sys, 0) = 0x005b7e0c;
    const uint32_t block = orig::fn_004e07f0(kHeap, 0x10, 0x18, 2, 0x0058b5e8);
    uint32_t list = 0;
    if (block)
        list = orig::fn_004e7010(Field<uint32_t>(g_game, 0xac4), 0, block);
    Field<uint32_t>(sys, 4) = list;
    Field<uint32_t>(sys, 0xc) = 0;
    Field<uint32_t>(sys, 0x10) = 0;
    Field<uint32_t>(sys, 0x18) = 0;
    Field<uint32_t>(sys, 0x14) = 0;
    Field<uint32_t>(sys, 0x1c) = 0;
    Field<uint32_t>(sys, 0x20) = 0;
    Field<uint32_t>(sys, 0x24) = 0;
    return u(sys);
}

uint32_t FxSystem_Unload(void* sys);

// 0x0041c760 (esi = fx system; caller the game destructor 0x00401710): FxSystem destructor: vtable reset,
// FxSystem_Unload, then the list's scalar-deleting destructor (virtual +0x10, 1).
uint32_t FxSystem_Destruct(void* sys)
{
    Field<uint32_t>(sys, 0) = 0x005b7e0c;
    uint32_t r = FxSystem_Unload(sys);
    void* list = Field<void*>(sys, 4);
    if (list)
        r = VCall<uint32_t>(list, 0x10 / 4, 1u);
    return r;
}

// 0x0041c780 (eax = pointer, ecx = fx system or bank with base +0xc): pointer to offset (-1 for null). Saved-game
// serialisation helper (caller 0x0048bda0).
uint32_t FxBank_PointerToOffset(uint32_t ptr, void* bank)
{
    if (ptr == 0)
        return 0xffffffffu;
    return ptr - Field<uint32_t>(bank, 0xc);
}

// 0x0041c790 (ecx = offset; stack: bank; callee pops 4): offset to pointer (null for -1). Inverse of 0x0041c780.
uint32_t FxBank_OffsetToPointer(uint32_t offset, void* bank)
{
    if (offset == 0xffffffffu)
        return 0;
    return Field<uint32_t>(bank, 0xc) + offset;
}

// 0x0041c9a0 (eax = fx system; caller 0x004010c0): 0x0046d810 (ebx = pool; stack 1) on the three particle pools
// +0x1c, +0x20, +0x24.
uint32_t FxSystem_ResetPools(void* sys)
{
    uint32_t r = 0;
    for (int i = 0; i < 3; ++i)
        r = orig::fn_0046d810(Field<uint32_t>(sys, 0x1c + i * 4), 1);
    return r;
}

// 0x0041c9d0 (stack: fx system; callee pops 4): unload: every fx instance of list +4 is released (Fx_Release),
// the list cleared (virtual +0x30); the bank +0x14 (when its +0xc byte is 0: 0x004f11c0 (ebx = bank) and its +0x1c
// freed) freed; +0xc freed; +0x10 / +0x14 / +0x18 cleared; the three particle pools destroyed (0x0046d8c0,
// eax = pool) and cleared. Returns the iterator node (reference released).
uint32_t FxSystem_Unload(void* sys)
{
    uint32_t iterator[2] = {0, 0};
    void* fx = p(orig::fn_004e7340(Field<uint32_t>(sys, 4), u(iterator)));
    while (fx)
    {
        Fx_Release(sys, &fx);
        fx = p(orig::fn_004e73c0(u(iterator)));
    }
    VCall<void>(Field<void*>(sys, 4), 0x30 / 4);
    void* bank = Field<void*>(sys, 0x14);
    if (bank)
    {
        if (Field<uint8_t>(bank, 0xc) == 0)
        {
            orig::fn_004f11c0(u(bank));
            orig::fn_004e08d0(kHeap, Field<uint32_t>(bank, 0x1c));
        }
        orig::fn_004e08d0(kHeap, u(bank));
        Field<uint32_t>(sys, 0x14) = 0;
    }
    Field<uint32_t>(sys, 0x18) = 0;
    if (Field<uint32_t>(sys, 0xc))
    {
        orig::fn_004e08d0(kHeap, Field<uint32_t>(sys, 0xc));
        Field<uint32_t>(sys, 0xc) = 0;
    }
    Field<uint32_t>(sys, 0x10) = 0;
    orig::fn_0046d8c0(Field<uint32_t>(sys, 0x1c));
    Field<uint32_t>(sys, 0x1c) = 0;
    orig::fn_0046d8c0(Field<uint32_t>(sys, 0x20));
    Field<uint32_t>(sys, 0x20) = 0;
    orig::fn_0046d8c0(Field<uint32_t>(sys, 0x24));
    Field<uint32_t>(sys, 0x24) = 0;
    if (iterator[1])
        --Field<int32_t>(p(iterator[1]), 0x18);
    return iterator[1];
}

// 0x0041cab0 (stack: fx system; callee pops 4; caller 0x0045d670): kill all: every fx object of list
// Game+0xa84 -> +0xedc gets virtual +0xdc (destroy); then each fx instance is released, except those with flag
// 0x10 (+0x14), which are only marked dead (+0x10 = 3). One iterator for both loops, released at the end.
uint32_t FxSystem_KillAll(void* sys)
{
    uint32_t iterator[2] = {0, 0};
    void* list = Field<void*>(Field<void*>(g_game, 0xa84), 0xedc);
    for (void* obj = p(orig::fn_004e7340(u(list), u(iterator))); obj; obj = p(orig::fn_004e73c0(u(iterator))))
        VCall<void>(obj, 0xdc / 4);
    void* fx = p(orig::fn_004e7340(Field<uint32_t>(sys, 4), u(iterator)));
    while (fx)
    {
        if (!(Field<uint8_t>(fx, 0x14) & 0x10))
            Fx_Release(sys, &fx);
        else
            Field<uint32_t>(fx, 0x10) = 3;
        fx = p(orig::fn_004e73c0(u(iterator)));
    }
    if (iterator[1])
        --Field<int32_t>(p(iterator[1]), 0x18);
    return iterator[1];
}

// 0x0041cb60 (eax = fx system, ecx = name): looks the name up in the bank index +0x14 (0x004f1160, eax = index;
// stack: name) and returns the script record after its name: base +0x10 + offset + the name length rounded up to
// 32 bytes ((len + 0x20) & ~0x1f). Null when not found or the offset is negative.
uint32_t FxBank_Find(void* sys, const char* name)
{
    const int32_t* entry = reinterpret_cast<const int32_t*>(orig::fn_004f1160(Field<uint32_t>(sys, 0x14), u(name)));
    if (!entry || *entry < 0)
        return 0;
    const uint32_t base = Field<uint32_t>(sys, 0x10);
    const uint32_t len = static_cast<uint32_t>(std::strlen(reinterpret_cast<const char*>(base + *entry)));
    return ((len + 0x20) & ~0x1fu) + base + *entry;
}

// 0x0041cba0 (eax = name list: count, then names each padded to 32 bytes; ecx and the stack argument are not read,
// callee pops 4): picks entry (rand() * count) >> 15 and returns it, or null when it is "null" (0x005bba94) or the
// list is empty/null.
uint32_t FxNameList_PickRandom(const uint32_t* list)
{
    if (!list)
        return 0;
    const uint32_t count = list[0];
    const char* s = reinterpret_cast<const char*>(list + 1);
    const int32_t pick =
        static_cast<int32_t>(static_cast<uint32_t>(GameFunction<int(__cdecl*)()>(0x005391cd)()) * count) >> 15;
    for (uint32_t i = 0; i < count; ++i)
    {
        const char* cur = s;
        s += (std::strlen(cur) + 0x20) & ~0x1fu;
        if (i == static_cast<uint32_t>(pick))
        {
            if (GameFunction<int(__cdecl*)(const char*, const char*)>(0x005448ee)(
                    cur, reinterpret_cast<const char*>(kNullName)) == 0)
                return 0;
            return u(cur);
        }
    }
    return 0;
}

// 0x0041cc20 (eax = fx system): allocates a zeroed 0x128-byte fx instance (0x004e07f0 tag "fxData" 0x005b9304)
// with vtable 0x005b7df4. Null when the bank (+0x10) is not loaded. Like the original, the block is cleared before
// the null test.
uint32_t FxInstance_Alloc(void* sys)
{
    if (Field<uint32_t>(sys, 0x10) == 0)
        return 0;
    uint32_t* fx = reinterpret_cast<uint32_t*>(orig::fn_004e07f0(kHeap, 0x11, 0x128, 2, 0x005b9304));
    std::memset(fx, 0, 0x128);
    if (fx)
        fx[0] = 0x005b7df4;
    return u(fx);
}

// 0x0041cc60 Fx_Create (al = force, ecx = fx system; stack: name, params; callee pops 8). Strings "FxCreate: Could
// not find fx\n%s", "%s_%s", "material". Unless force, nothing is created while the save loader (Game+0xab8 +0x14
// byte) has bit 1 set and bit 0 clear. params (0x80 bytes): +0 flags, +4 more flags, +8 record (flag 0x10),
// +0xc position (8), +0x18 owner attach point and +0x1c owner (4), +0x2c start time (0x40), +0x30 matrix 0x44
// (2 / 0x100), +0x74 attach point name, +0x78 material variant list (0x800, "%s_%s" with each strtok token of
// 0x005b9300), +0x7c object whose "material" property (0x004532e0) gives another variant. The position computed
// first (flag 8 / owner virtual +0x54 / matrix row / camera 0x0040f9e0) is not used afterwards (as in the
// original). Returns the new fx instance (added to list +4 by virtual +0x18) or null.
uint32_t Fx_Create(bool force, void* sys, const char* name, uint32_t* params)
{
    if (!force)
    {
        const uint8_t* state = Field<const uint8_t*>(Field<void*>(g_game, 0xab8), 0x14);
        if (state && (*state & 2) && !(*state & 1))
            return 0;
    }
    if (Field<uint32_t>(sys, 0x10) == 0)
        return 0;

    alignas(4) uint32_t pos[3];
    alignas(4) char property[0x20];
    alignas(4) char variants[0x44];
    alignas(4) char fullName[0x80];
    uint32_t record = 0;
    const uint32_t flags = params[0];
    if (flags & 8)
    {
        pos[0] = params[3], pos[1] = params[4], pos[2] = params[5];
    }
    else if (flags & 4)
    {
        VCall<void>(p(params[7]), 0x54 / 4, static_cast<void*>(pos));
    }
    else if (flags & 2)
    {
        pos[0] = params[0xf], pos[1] = params[0x13], pos[2] = params[0x17];
    }
    else
    {
        orig::fn_0040f9e0(u(pos), Field<uint32_t>(g_game, 0xa68));
    }

    if (params[0] & 0x10)
    {
        record = params[2];
    }
    else
    {
        if (params[0] & 0x800)
        {
            if (params[0x1e])
            {
                std::strcpy(variants, reinterpret_cast<const char*>(params[0x1e]));
                auto strtok_ = GameFunction<char*(__cdecl*)(char*, const char*)>(0x005381e4);
                auto sprintf_ = GameFunction<int(__cdecl*)(char*, const char*, ...)>(0x0053814f);
                for (char* token = strtok_(variants, reinterpret_cast<const char*>(0x005b9300)); token;
                     token = strtok_(nullptr, reinterpret_cast<const char*>(0x005b9300)))
                {
                    sprintf_(fullName, reinterpret_cast<const char*>(0x005b92f8), name, token);
                    record = FxBank_Find(sys, fullName);
                    if (record)
                        break;
                }
            }
            if (!record && params[0x1f] &&
                static_cast<uint8_t>(orig::fn_004532e0(params[0x1f], 0x005b92ec, u(property))))
            {
                GameFunction<int(__cdecl*)(char*, const char*, ...)>(0x0053814f)(
                    variants, reinterpret_cast<const char*>(0x005b92f8), name, property);
                record = FxBank_Find(sys, variants);
            }
        }
        if (!record)
            record = FxBank_Find(sys, name);
    }
    if (!record)
    {
        ConsolePrint(8, 0x005b92cc, u(name));
        return 0;
    }

    void* fx = p(FxInstance_Alloc(sys));
    VCall<void>(Field<void*>(sys, 4), 0x18 / 4, fx);
    GameFunction<char*(__cdecl*)(char*, const char*, size_t)>(0x005382a0)(
        reinterpret_cast<char*>(u(fx) + 0x24), reinterpret_cast<const char*>(record - 0x20), 0x20);
    Field<uint32_t>(fx, 4) = record;
    Field<uint32_t>(fx, 0x10) = 0;
    if (params[0] & 8)
    {
        Field<uint32_t>(fx, 0x18) = params[3];
        Field<uint32_t>(fx, 0x1c) = params[4];
        Field<uint32_t>(fx, 0x20) = params[5];
    }
    else if (params[0] & 2)
    {
        Field<uint32_t>(fx, 0x14) |= 2;
        Field<uint32_t>(fx, 0x14) |= 0x40;
        Field<uint32_t>(fx, 0x18) = params[0xf];
        Field<uint32_t>(fx, 0x1c) = params[0x13];
        Field<uint32_t>(fx, 0x20) = params[0x17];
    }
    if (params[0] & 0x200)
        Field<uint32_t>(fx, 0x14) |= 0x80;
    if (params[0] & 0x400)
        Field<uint32_t>(fx, 0x14) |= 0x400;
    auto stricmp_ = GameFunction<int(__cdecl*)(const char*, const char*)>(0x005448ee);
    if (!(params[0] & 1) && (params[0] & 2))
    {
        Field<uint32_t>(fx, 0x14) |= 2;
        Field<uint32_t>(fx, 0x14) |= 0x40;
        std::memcpy(reinterpret_cast<void*>(u(fx) + 0x70), &params[0xc], 0x44);
        const char* point = reinterpret_cast<const char*>(params[0x1d]);
        if (point && stricmp_(point, reinterpret_cast<const char*>(kNullName)) != 0)
        {
            std::strcpy(reinterpret_cast<char*>(u(fx) + 0x44), reinterpret_cast<const char*>(params[0x1d]));
            Field<uint32_t>(fx, 0x68) = u(fx) + 0x44;
        }
        else
        {
            Field<uint32_t>(fx, 0x68) = 0;
        }
    }
    if (static_cast<uint8_t>(params[0]) & 4)
    {
        Field<uint32_t>(fx, 0x14) |= 4;
        Field<uint32_t>(fx, 0xb4) = params[7];
        ++Field<int32_t>(p(params[7]), 0x28);
        const char* point = reinterpret_cast<const char*>(params[6]);
        if (point && stricmp_(point, reinterpret_cast<const char*>(kNullName)) != 0)
        {
            std::strcpy(reinterpret_cast<char*>(u(fx) + 0xb8), reinterpret_cast<const char*>(params[6]));
            Field<uint32_t>(fx, 0xd8) = u(fx) + 0xb8;
        }
        else
        {
            Field<uint32_t>(fx, 0xd8) = 0;
        }
    }
    if (static_cast<uint8_t>(params[0]) & 0x40)
        Field<uint32_t>(fx, 0xc) = params[0xb];
    if (static_cast<int8_t>(params[0]) < 0)
    {
        Field<uint32_t>(fx, 0x6c) = params[7];
        ++Field<int32_t>(p(params[7]), 0x28);
    }
    if (params[0] & 0x100)
        std::memcpy(reinterpret_cast<void*>(u(fx) + 0x70), &params[0xc], 0x44);
    if ((static_cast<uint8_t>(params[0]) & 0x20) && (static_cast<uint8_t>(params[1]) & 8))
        Field<uint32_t>(fx, 0x14) |= 0x10;
    return u(fx);
}

// 0x0041cfb0 (ebx = fx instance; stack: fx system, not read; callee pops 4): destroys the instance's emitter list
// +0x124: each object whose virtual +0xd8 returns true is detached by 0x00419290 (al = 1, esi = object); then the
// list's scalar-deleting destructor (virtual +0x10, 1). The pointer is not cleared (as in the original).
uint32_t FxInstance_DestroyEmitters(void* fx, uint32_t /*sys*/)
{
    uint32_t iterator[2] = {0, 0};
    for (void* obj = p(orig::fn_004e7340(Field<uint32_t>(fx, 0x124), u(iterator))); obj;
         obj = p(orig::fn_004e73c0(u(iterator))))
    {
        const uint32_t r = VCall<uint32_t>(obj, 0xd8 / 4);
        if (static_cast<uint8_t>(r))
            orig::fn_00419290((r & ~0xffu) | 1, u(obj));
    }
    void* list = Field<void*>(fx, 0x124);
    if (list)
        VCall<void>(list, 0x10 / 4, 1u);
    if (iterator[1])
        --Field<int32_t>(p(iterator[1]), 0x18);
    return iterator[1];
}

// Drops one reference (+0x28) of an object held by an fx instance; at zero, when its virtual +0xd8 is false, it
// is deleted by 0x00487da0 (eax = object). The slot is cleared. (Inlined twice in 0x0041d030.)
static void Fx_DropReference(void** slot)
{
    void* obj = *slot;
    if (!obj)
        return;
    if (--Field<int32_t>(obj, 0x28) == 0 && !static_cast<uint8_t>(VCall<uint32_t>(obj, 0xd8 / 4)))
        orig::fn_00487da0(u(obj));
    *slot = nullptr;
}

// 0x0041d030 Fx_Release (stack: fx system, &fx; callee pops 8; 12 callers). Null: nothing. A protected instance
// (flag 0x200) is only detached: *slot = 0 and flag 0x100. Otherwise its owner (+0xb4) and second object (+0x6c)
// lose a reference, its emitters are destroyed when flag 0x20 (0x0041cfb0), it leaves the system's list
// (virtual +0x28), is marked dead (+0x10 = 3) and freed; *slot = 0.
uint32_t Fx_Release(void* sys, void** slot)
{
    void* fx = *slot;
    if (!fx)
        return 0;
    if (Field<uint32_t>(fx, 0x14) & 0x200)
    {
        *slot = nullptr;
        Field<uint32_t>(fx, 0x14) |= 0x100;
        return Field<uint32_t>(fx, 0x14);
    }
    Fx_DropReference(&Field<void*>(fx, 0xb4));
    Fx_DropReference(&Field<void*>(fx, 0x6c));
    if (Field<uint8_t>(fx, 0x14) & 0x20)
        FxInstance_DestroyEmitters(fx, u(sys));
    VCall<void>(Field<void*>(sys, 4), 0x28 / 4, fx);
    Field<uint32_t>(fx, 0x10) = 3;
    const uint32_t r = orig::fn_004e08d0(kHeap, u(fx));
    *slot = nullptr;
    return r;
}

// 0x0041d110 (ecx = fxpush; stack: fx, lifespan; callee pops 8): opcode helper "set lifespan": object +0x460 |= 2,
// +0x470 = 0 (age), +0x46c = lifespan. Without fxpush: "Fx : Failed to set lifespan (no fxpush)\n%s".
uint32_t Fx_SetLifespan(void* push, void* fx, uint32_t lifespan)
{
    if (!push)
    {
        FxPushError(fx, 0x005b92a0);
        return 0;
    }
    Field<uint32_t>(Field<void*>(push, 4), 0x460) |= 2;
    Field<uint32_t>(Field<void*>(push, 4), 0x470) = 0;
    Field<uint32_t>(Field<void*>(push, 4), 0x46c) = lifespan;
    return Field<uint32_t>(push, 4);
}

// 0x0041d170 / 0x0041d210 / 0x0041d2c0 (esi = fxpush; stack: fx, from, to; callee pops 12): opcodes "set scale
// linear / exp / log": the object's scale is set to (from, from, from) by 0x004535d0 (eax = vec3, ecx = object),
// +0x480 = from, +0x484 = to; when from != to the mode flag (0x20 / 0x40 / 0x80) is set in +0x460; exp also stores
// sqrt(to - from) at +0x488.
static uint32_t Fx_SetScaleCurve(void* push, void* fx, uint32_t from, uint32_t to, uint32_t flag, bool exp,
                                 uint32_t error)
{
    if (!push)
    {
        FxPushError(fx, error);
        return 0;
    }
    uint32_t v[3] = {from, from, from};
    orig::fn_004535d0(u(v), Field<uint32_t>(push, 4));
    Field<uint32_t>(Field<void*>(push, 4), 0x480) = from;
    Field<uint32_t>(Field<void*>(push, 4), 0x484) = to;
    if (u2f(from) == u2f(to))
        return 0;
    if (exp)
        Field<float>(Field<void*>(push, 4), 0x488) = sqrtf(u2f(to) - u2f(from));
    Field<uint32_t>(Field<void*>(push, 4), 0x460) |= flag;
    return Field<uint32_t>(push, 4) + 0x460;
}
uint32_t Fx_SetScaleLinear(void* push, void* fx, uint32_t from, uint32_t to)
{
    return Fx_SetScaleCurve(push, fx, from, to, 0x20, false, 0x005b9270);
}
uint32_t Fx_SetScaleExp(void* push, void* fx, uint32_t from, uint32_t to)
{
    return Fx_SetScaleCurve(push, fx, from, to, 0x40, true, 0x005b9244);
}
uint32_t Fx_SetScaleLog(void* push, void* fx, uint32_t from, uint32_t to)
{
    return Fx_SetScaleCurve(push, fx, from, to, 0x80, false, 0x005b9218);
}

// 0x0041d360 / 0x0041d440 / 0x0041d560 (eax = fxpush, ebx = light index, edi = colour vec3 (0..255); stack: fx;
// callee pops 4): opcodes "set light scale linear / exp / log": reads the renderer's light (renderer virtual
// +0xb4 = GetLight(index, D3DLIGHT9 0x68 bytes)), stores the index at +0x5d0, its diffuse r, g, b at
// +0x5d4..+0x5dc and the target colour * [0x005bcc18] at +0x5e0..+0x5e8; exp also sqrt(target - diffuse) per
// channel at +0x5ec..+0x5f4. Flag 0x20000000 / 0x40000000 / 0x80000000 in +0x460. The log variant prints the
// scale-log message (0x005b9218) when it fails (as in the original).
static uint32_t Fx_SetLightCurve(void* push, uint32_t index, const float* colour, void* fx, uint32_t flag, bool exp,
                                 uint32_t error)
{
    if (!push)
    {
        FxPushError(fx, error);
        return 0;
    }
    alignas(4) uint32_t light[0x68 / 4];
    VCall<void>(Field<void*>(g_game, 0x38), 0xb4 / 4, index, static_cast<void*>(light));
    Field<uint32_t>(Field<void*>(push, 4), 0x5d0) = index;
    Field<uint32_t>(Field<void*>(push, 4), 0x5d4) = light[1];
    Field<uint32_t>(Field<void*>(push, 4), 0x5d8) = light[2];
    Field<uint32_t>(Field<void*>(push, 4), 0x5dc) = light[3];
    Field<float>(Field<void*>(push, 4), 0x5e0) = colour[0] * ColourScale();
    Field<float>(Field<void*>(push, 4), 0x5e4) = colour[1] * ColourScale();
    Field<float>(Field<void*>(push, 4), 0x5e8) = colour[2] * ColourScale();
    if (exp)
    {
        void* o = Field<void*>(push, 4);
        Field<float>(o, 0x5ec) = sqrtf(Field<float>(o, 0x5e0) - Field<float>(o, 0x5d4));
        o = Field<void*>(push, 4);
        Field<float>(o, 0x5f0) = sqrtf(Field<float>(o, 0x5e4) - Field<float>(o, 0x5d8));
        o = Field<void*>(push, 4);
        Field<float>(o, 0x5f4) = sqrtf(Field<float>(o, 0x5e8) - Field<float>(o, 0x5dc));
    }
    Field<uint32_t>(Field<void*>(push, 4), 0x460) |= flag;
    return Field<uint32_t>(push, 4) + 0x460;
}
uint32_t Fx_SetLightScaleLinear(void* push, uint32_t index, const float* colour, void* fx)
{
    return Fx_SetLightCurve(push, index, colour, fx, 0x20000000u, false, 0x005b91e0);
}
uint32_t Fx_SetLightScaleExp(void* push, uint32_t index, const float* colour, void* fx)
{
    return Fx_SetLightCurve(push, index, colour, fx, 0x40000000u, true, 0x005b91ac);
}
uint32_t Fx_SetLightScaleLog(void* push, uint32_t index, const float* colour, void* fx)
{
    return Fx_SetLightCurve(push, index, colour, fx, 0x80000000u, false, 0x005b9218);
}

// 0x0041d640 / 0x0041d700 / 0x0041d830 (eax = fxpush, edi = colour vec3 (0..255); stack: fx, start, end; callee
// pops 12): opcodes "set fog linear / exp / log": the renderer's current fog (renderer virtual +0x110 =
// GetFog(&start +0x61c, &end +0x628, &colour +0x5f8)) is the starting point; target colour * [0x005bcc18] at
// +0x604..+0x60c, target start +0x620, end +0x62c; exp also sqrt(target - current) for the colour (+0x610..+0x618),
// start (+0x624) and end (+0x630). Flag 0x10 / 0x20 / 0x40 in +0x464.
static uint32_t Fx_SetFogCurve(void* push, const float* colour, void* fx, uint32_t start, uint32_t end,
                               uint32_t flag, bool exp, uint32_t error)
{
    if (!push)
    {
        FxPushError(fx, error);
        return 0;
    }
    {
        const uint32_t o = Field<uint32_t>(push, 4);
        VCall<void>(Field<void*>(g_game, 0x38), 0x110 / 4, p(o + 0x61c), p(o + 0x628), p(o + 0x5f8));
    }
    Field<float>(Field<void*>(push, 4), 0x604) = colour[0] * ColourScale();
    Field<float>(Field<void*>(push, 4), 0x608) = colour[1] * ColourScale();
    Field<float>(Field<void*>(push, 4), 0x60c) = colour[2] * ColourScale();
    if (exp)
    {
        void* o = Field<void*>(push, 4);
        Field<float>(o, 0x610) = sqrtf(Field<float>(o, 0x604) - Field<float>(o, 0x5f8));
        o = Field<void*>(push, 4);
        Field<float>(o, 0x614) = sqrtf(Field<float>(o, 0x608) - Field<float>(o, 0x5fc));
        o = Field<void*>(push, 4);
        Field<float>(o, 0x618) = sqrtf(Field<float>(o, 0x60c) - Field<float>(o, 0x600));
        Field<uint32_t>(Field<void*>(push, 4), 0x620) = start;
        o = Field<void*>(push, 4);
        Field<float>(o, 0x624) = sqrtf(Field<float>(o, 0x620) - Field<float>(o, 0x61c));
        Field<uint32_t>(Field<void*>(push, 4), 0x62c) = end;
        o = Field<void*>(push, 4);
        Field<float>(o, 0x630) = sqrtf(Field<float>(o, 0x62c) - Field<float>(o, 0x628));
    }
    else
    {
        Field<uint32_t>(Field<void*>(push, 4), 0x620) = start;
        Field<uint32_t>(Field<void*>(push, 4), 0x62c) = end;
    }
    Field<uint32_t>(Field<void*>(push, 4), 0x464) |= flag;
    return Field<uint32_t>(push, 4) + 0x464;
}
uint32_t Fx_SetFogLinear(void* push, const float* colour, void* fx, uint32_t start, uint32_t end)
{
    return Fx_SetFogCurve(push, colour, fx, start, end, 0x10, false, 0x005b917c);
}
uint32_t Fx_SetFogExp(void* push, const float* colour, void* fx, uint32_t start, uint32_t end)
{
    return Fx_SetFogCurve(push, colour, fx, start, end, 0x20, true, 0x005b9150);
}
uint32_t Fx_SetFogLog(void* push, const float* colour, void* fx, uint32_t start, uint32_t end)
{
    return Fx_SetFogCurve(push, colour, fx, start, end, 0x40, false, 0x005b9124);
}

// 0x0041d8f0 / 0x0041d980 (eax = colour vec4, scaled in place; ecx = fxpush; edx = fx): opcodes "set light
// specular / ambient": only after "set light" (flag 0x1000000 in +0x460): colour[3] = 1, rgb * [0x005bcc18],
// copied to the object's D3DLIGHT9 (at +0x560) Specular (+0x574) / Ambient (+0x584). Errors "no fxpush" /
// "no light set".
static uint32_t Fx_SetLightColour(float* colour, void* push, void* fx, uint32_t offset, uint32_t noPush,
                                  uint32_t noLight)
{
    if (!push)
    {
        FxPushError(fx, noPush);
        return 0;
    }
    if (!(Field<uint32_t>(Field<void*>(push, 4), 0x460) & 0x1000000))
    {
        FxPushError(fx, noLight);
        return 0;
    }
    reinterpret_cast<uint32_t*>(colour)[3] = 0x3f800000;
    colour[0] = colour[0] * ColourScale();
    colour[1] = colour[1] * ColourScale();
    colour[2] = colour[2] * ColourScale();
    std::memcpy(reinterpret_cast<void*>(Field<uint32_t>(push, 4) + offset), colour, 16);
    return reinterpret_cast<uint32_t*>(colour)[3];
}
uint32_t Fx_SetLightSpecular(float* colour, void* push, void* fx)
{
    return Fx_SetLightColour(colour, push, fx, 0x574, 0x005b90bc, 0x005b90f0);
}
uint32_t Fx_SetLightAmbient(float* colour, void* push, void* fx)
{
    return Fx_SetLightColour(colour, push, fx, 0x584, 0x005b9058, 0x005b9088);
}

// 0x0041da10 (eax = fxpush, edx = fx; stack: half range; callee pops 4): opcode "set light halfrange": the light's
// Attenuation1 (+0x5b8) = 1 / halfrange ([0x005bcb2c] = 1.0), or 0 when halfrange == 0.
uint32_t Fx_SetLightHalfRange(void* push, void* fx, uint32_t halfRange)
{
    if (!push)
    {
        FxPushError(fx, 0x005b8fec);
        return 0;
    }
    void* o = Field<void*>(push, 4);
    if (!(Field<uint32_t>(o, 0x460) & 0x1000000))
    {
        FxPushError(fx, 0x005b9020);
        return 0;
    }
    if (u2f(halfRange) == Zero())
        Field<float>(o, 0x5b8) = Zero();
    else
        Field<float>(o, 0x5b8) = GameGlobal<float>(0x005bcb2c) / u2f(halfRange);
    return 0;
}

// 0x0041da90 (eax = fxpush, edx = colour vec4 (scaled in place), ebx = fx; stack: attach-point name list; callee
// pops 4): opcode "set light": clears the object's D3DLIGHT9 (+0x560, 0x68 bytes), Type = 1 (point), Diffuse =
// colour (alpha 1, rgb * [0x005bcc18]), Range 1e6, Falloff 0, Attenuation0 1, Attenuation1 0.1; +0x5c8 = a random
// attach point of the list (0x0041cba0, null for "null"), +0x5cc = -1; flag 0x1000000 in +0x460.
uint32_t Fx_SetLight(void* push, float* colour, void* fx, const uint32_t* names)
{
    if (!push)
    {
        FxPushError(fx, 0x005b8fc4);
        return 0;
    }
    std::memset(reinterpret_cast<void*>(Field<uint32_t>(push, 4) + 0x560), 0, 0x68);
    Field<uint32_t>(Field<void*>(push, 4), 0x560) = 1;
    reinterpret_cast<uint32_t*>(colour)[3] = 0x3f800000;
    colour[0] = colour[0] * ColourScale();
    colour[1] = colour[1] * ColourScale();
    colour[2] = colour[2] * ColourScale();
    std::memcpy(reinterpret_cast<void*>(Field<uint32_t>(push, 4) + 0x564), colour, 16);
    Field<uint32_t>(Field<void*>(push, 4), 0x5ac) = 0x49742400;  // 1e6
    Field<uint32_t>(Field<void*>(push, 4), 0x5b0) = 0;
    Field<uint32_t>(Field<void*>(push, 4), 0x5b4) = 0x3f800000;
    Field<uint32_t>(Field<void*>(push, 4), 0x5b8) = 0x3dcccccd;  // 0.1
    Field<uint32_t>(Field<void*>(push, 4), 0x5c8) = FxNameList_PickRandom(names);
    Field<uint32_t>(Field<void*>(push, 4), 0x5cc) = 0xffffffffu;
    Field<uint32_t>(Field<void*>(push, 4), 0x460) |= 0x1000000;
    return 0;
}

// 0x0041dba0 (eax = fxpush; stack: fx, start, end; callee pops 12): opcode 0x09 "set fade in": +0x474 = start,
// flags +0x460 |= 4 then 8, +0x478 = 1 / (end - start); then the object's virtual +0x100 (0).
uint32_t Fx_SetFadeIn(void* push, void* fx, uint32_t start, uint32_t end)
{
    if (!push)
    {
        FxPushError(fx, 0x005b8f98);
        return 0;
    }
    const float d = u2f(end) - u2f(start);
    Field<uint32_t>(Field<void*>(push, 4), 0x474) = start;
    const float inv = GameGlobal<float>(0x005bcb2c) / d;
    Field<uint32_t>(Field<void*>(push, 4), 0x460) |= 4;
    Field<uint32_t>(Field<void*>(push, 4), 0x460) |= 8;
    Field<float>(Field<void*>(push, 4), 0x478) = inv;
    return VCall<uint32_t>(Field<void*>(push, 4), 0x100 / 4, 0u);
}

// 0x0041dc30 (eax = fxpush; stack: fx, duration; callee pops 8): opcode 0x0a "set fade out": +0x47c = duration,
// flag 0x10 in +0x460.
uint32_t Fx_SetFadeOut(void* push, void* fx, uint32_t duration)
{
    if (!push)
    {
        FxPushError(fx, 0x005b8f6c);
        return 0;
    }
    Field<uint32_t>(Field<void*>(push, 4), 0x47c) = duration;
    Field<uint32_t>(Field<void*>(push, 4), 0x460) |= 0x10;
    return Field<uint32_t>(push, 4) + 0x460;
}

// 0x0041dc80 (eax = matrix or null, ebx = owner object; stack: fx system, script record, attach-point list 1,
// list 2, detached (byte), params +4; callee pops 0x18): starts a child fx on an object: params flags 0x38
// (record given, position = the owner's virtual +0x54), Fx_Create (al = 0); no null test on the result (as in the
// original). Detached: flag 1; else flag 2 and, with a matrix, flag 8 and the matrix copied to +0xe0 (0x44 bytes).
// +0x64 / +0x68 = random attach points of the two lists, +0x6c = owner (reference +0x28). Flag 2: 0x00423360
// (ecx = fx system; stack: fx) places it. Returns the new fx instance.
uint32_t Fx_StartOnObject(const uint32_t* matrix, void* owner, void* sys, uint32_t record, const uint32_t* list1,
                          const uint32_t* list2, uint32_t detached, uint32_t extra)
{
    uint32_t params[0x20];
    VCall<void>(owner, 0x54 / 4, static_cast<void*>(&params[3]));
    params[1] = extra;
    params[2] = record;
    params[0] = 0x38;
    void* fx = p(Fx_Create(false, sys, nullptr, params));
    if (static_cast<uint8_t>(detached))
    {
        Field<uint32_t>(fx, 0x14) |= 1;
    }
    else
    {
        Field<uint32_t>(fx, 0x14) |= 2;
        if (matrix)
        {
            Field<uint32_t>(fx, 0x14) |= 8;
            std::memcpy(reinterpret_cast<void*>(u(fx) + 0xe0), matrix, 0x44);
        }
    }
    Field<uint32_t>(fx, 0x64) = FxNameList_PickRandom(list1);
    Field<uint32_t>(fx, 0x68) = FxNameList_PickRandom(list2);
    Field<void*>(fx, 0x6c) = owner;
    ++Field<int32_t>(owner, 0x28);
    if (Field<uint8_t>(fx, 0x14) & 2)
        orig::fn_00423360(u(sys), u(fx));
    return u(fx);
}

// 0x0041dd50 (eax = fx name, esi = fxpush, edi = fx system; stack: list 1, list 2, detached, matrix; callee pops
// 0x10): opcode "attach": finds the named fx (FxBank_Find) and starts it on the fxpush object (Fx_StartOnObject,
// params +4 = 0). Errors "Failed to attach (no fxpush)" (printed with [edi+8] + 0x24, the fx system's running
// instance, see the file header) and "Failed to attach, can't find fx" (with the name).
uint32_t Fx_Attach(const char* name, void* push, void* sys, uint32_t list1, uint32_t list2, uint32_t detached,
                   uint32_t matrix)
{
    if (!push)
    {
        FxPushError(sys, 0x005b8f1c);
        return 0;
    }
    const uint32_t record = FxBank_Find(sys, name);
    if (!record)
    {
        ConsolePrint(0xa, 0x005b8f44, u(name));
        return 0;
    }
    return Fx_StartOnObject(reinterpret_cast<const uint32_t*>(matrix), Field<void*>(push, 4), sys, record,
                            reinterpret_cast<const uint32_t*>(list1), reinterpret_cast<const uint32_t*>(list2),
                            detached, 0);
}

// 0x0041ddc0 (eax = fxpush; stack: fx; callee pops 4): opcode "set face direction": flag 0x800 in +0x464.
uint32_t Fx_SetFaceDirection(void* push, void* fx)
{
    if (!push)
    {
        FxPushError(fx, 0x005b8ee8);
        return 0;
    }
    Field<uint32_t>(Field<void*>(push, 4), 0x464) |= 0x800;
    return Field<uint32_t>(push, 4) + 0x464;
}

// 0x0041de10 (ebx = fxpush; stack: fx, gravity; callee pops 8): opcode "set gravity": a nonzero value goes to
// +0x48c; exactly 1.0 also registers the object with the physics list Game+0xa84 +0xf1c (0x00454750, esi = list,
// edi = object); then 0x004577b0 (eax = object; stack: 0) (wakes it, guess).
uint32_t Fx_SetGravity(void* push, void* fx, uint32_t gravity)
{
    if (!push)
    {
        FxPushError(fx, 0x005b8ebc);
        return 0;
    }
    if (!(u2f(gravity) == Zero()))
        Field<uint32_t>(Field<void*>(push, 4), 0x48c) = gravity;
    if (u2f(gravity) == GameGlobal<float>(0x005bcb2c))
        orig::fn_00454750(Field<uint32_t>(Field<void*>(g_game, 0xa84), 0xf1c), Field<uint32_t>(push, 4));
    return static_cast<uint32_t>(orig::fn_004577b0(Field<uint32_t>(push, 4), 0));
}

// 0x0041dea0 (eax = script state, ecx = fxpush; stack: fx, time; callee pops 8): opcode "pause": only valid with
// no fxpush ("Fx : Invalid Pause location (no fxppop)"): state +8 = time, +0x10 = 1.
uint32_t Fx_Pause(void* state, void* push, void* fx, uint32_t time)
{
    if (push)
    {
        FxPushError(fx, 0x005b8e90);
        return 0;
    }
    Field<uint32_t>(state, 8) = time;
    Field<uint32_t>(state, 0x10) = 1;
    return u(state);
}

// 0x0041dee0 (eax = fxpush; stack: fx, state; callee pops 8): opcode "wait": only valid with no fxpush
// ("Fx : Invalid Wait location (no fxppop)"): state +0x10 = 4.
uint32_t Fx_Wait(void* push, void* fx, void* state)
{
    if (push)
    {
        FxPushError(fx, 0x005b8e64);
        return 0;
    }
    Field<uint32_t>(state, 0x10) = 4;
    return 0;
}

// 0x0041df20 (eax = fx instance, ebx = fxpush; stack: fx; callee pops 4): opcode "set logobject": the instance
// gets flag 0x20 and an object list +0x124 (created like the system's, 0x004e07f0 + 0x004e7010) and the fxpush
// object is added to it (0x004546d0, esi = list, edi = object). These are the objects 0x0041cfb0 destroys.
uint32_t Fx_SetLogObject(void* instance, void* push, void* fx)
{
    if (!push)
    {
        FxPushError(fx, 0x005b8e38);
        return 0;
    }
    Field<uint32_t>(instance, 0x14) |= 0x20;
    if (Field<uint32_t>(instance, 0x124) == 0)
    {
        const uint32_t block = orig::fn_004e07f0(kHeap, 0x10, 0x18, 2, 0x0058b5e8);
        uint32_t list = 0;
        if (block)
            list = orig::fn_004e7010(Field<uint32_t>(g_game, 0xac4), 0, block);
        Field<uint32_t>(instance, 0x124) = list;
    }
    return static_cast<uint32_t>(orig::fn_004546d0(Field<uint32_t>(instance, 0x124), Field<uint32_t>(push, 4)));
}

// 0x0041dfb0 / 0x0041e0c0 (eax = reference object or null, edi = fxpush; stack: fx, strength; callee pops 8):
// opcodes "set drift" / "set impulse". Zero strength: nothing. The object is woken (0x004577b0 (eax = object;
// stack: 0)). With a reference: 0x00453c50 (object, reference, 0x44-byte matrix, 1, 0, 0, 1, 0, 1) gives a
// relative matrix; the force is its column 0 (drift) or minus its column 1 (impulse) times the strength, applied
// at its translation (column 3): 0x00458440 (eax = point, ecx = force, edx = object). Without: the force is a
// vector of the object (0x004531b0 drift / 0x00453150 impulse, ecx = object, esi = out) times the strength,
// applied with eax = 0.
static uint32_t Fx_ApplyForce(void* reference, void* push, void* fx, uint32_t strength, bool impulse,
                              uint32_t error)
{
    if (!push)
    {
        FxPushError(fx, error);
        return 0;
    }
    const float s = u2f(strength);
    if (s == Zero())
        return 0;
    orig::fn_004577b0(Field<uint32_t>(push, 4), 0);
    const uint32_t obj = Field<uint32_t>(push, 4);
    float force[3];
    float point[3];
    if (reference)
    {
        alignas(4) float m[17];
        orig::fn_00453c50(obj, u(reference), u(m), 1, 0, 0, 1, 0, 1);
        const int c = impulse ? 1 : 0;
        if (impulse)
        {
            force[0] = -(m[c] * s);
            std::memcpy(&point[0], &m[3], 4);
            std::memcpy(&point[1], &m[7], 4);
            std::memcpy(&point[2], &m[11], 4);
            force[1] = -(m[c + 4] * s);
            force[2] = -(m[c + 8] * s);
        }
        else
        {
            force[0] = m[c] * s;
            std::memcpy(&point[0], &m[3], 4);
            std::memcpy(&point[1], &m[7], 4);
            std::memcpy(&point[2], &m[11], 4);
            force[1] = m[c + 4] * s;
            force[2] = m[c + 8] * s;
        }
        return orig::fn_00458440(u(point), u(force), Field<uint32_t>(push, 4));
    }
    if (impulse)
        orig::fn_00453150(obj, u(point));
    else
        orig::fn_004531b0(obj, u(point));
    force[0] = point[0] * s;
    force[1] = point[1] * s;
    force[2] = point[2] * s;
    return orig::fn_00458440(0, u(force), Field<uint32_t>(push, 4));
}
uint32_t Fx_SetDrift(void* reference, void* push, void* fx, uint32_t strength)
{
    return Fx_ApplyForce(reference, push, fx, strength, false, 0x005b8e10);
}
uint32_t Fx_SetImpulse(void* reference, void* push, void* fx, uint32_t strength)
{
    return Fx_ApplyForce(reference, push, fx, strength, true, 0x005b8de4);
}

// 0x0041e1e0 (esi = fxpush; stack: fx, force vec3; callee pops 8): opcode "set absolute impulse": wakes the object
// (0x004577b0) and applies the force as given (0x00458440, eax = 0, ecx = force, edx = object).
uint32_t Fx_SetAbsoluteImpulse(void* push, void* fx, uint32_t force)
{
    if (!push)
    {
        FxPushError(fx, 0x005b8db0);
        return 0;
    }
    orig::fn_004577b0(Field<uint32_t>(push, 4), 0);
    return orig::fn_00458440(0, force, Field<uint32_t>(push, 4));
}

// 0x0041e230 (eax = fxpush; stack: fx; callee pops 4): opcode 0x79 "set inert": object +0x424 |= 0x20000.
uint32_t Fx_SetInert(void* push, void* fx)
{
    if (!push)
    {
        FxPushError(fx, 0x005b8d88);
        return 0;
    }
    Field<uint32_t>(Field<void*>(push, 4), 0x424) |= 0x20000;
    return Field<uint32_t>(push, 4) + 0x424;
}

// 0x0041e280 (eax = fxpush; stack: fx; callee pops 4): opcode 0x16 "set invisible": tail call of the object's
// virtual +0xf4 with its stack argument replaced by 0.
uint32_t Fx_SetInvisible(void* push, void* fx)
{
    if (!push)
    {
        FxPushError(fx, 0x005b8d5c);
        return 0;
    }
    return VCall<uint32_t>(Field<void*>(push, 4), 0xf4 / 4, 0u);
}

// 0x0041e2c0 (esi = fxpush; stack: not read, callee pops 4; callers 0x0041e310 and 0x0048e0b0): gives the object a
// zeroed 0x44-byte texture-animation block at +0x518 ("fx_texanim", 0x004e07f0) when it has none. Cleared without a
// null test (as in the original).
uint32_t FxObject_EnsureTexAnim(void* push, uint32_t /*unused*/)
{
    if (!push || Field<uint32_t>(Field<void*>(push, 4), 0x518))
        return 0;
    const uint32_t block = orig::fn_004e07f0(kHeap, 0x12, 0x44, 2, 0x005b8d50);
    Field<uint32_t>(Field<void*>(push, 4), 0x518) = block;
    std::memset(Field<void*>(Field<void*>(push, 4), 0x518), 0, 0x44);
    return 0;
}

// 0x0041e310 (eax = fx, ecx = layer 1..4, edx = fxpush; stack: unused, frames, rate, loop; callee pops 0x10):
// opcode "set texture animation": ensures the texanim block (0x0041e2c0), 0x00453540 (eax = object, cl = 1),
// sets bit (layer - 1) in +0x464, block float [layer - 1] = rate, dword [4 + layer - 1] = frames, and with loop the
// bit in block +0x40. Error "Fx : Failed to set texture animation (need a push)".
uint32_t Fx_SetTextureAnimation(void* fx, uint32_t layer, void* push, uint32_t /*a0*/, uint32_t frames,
                                uint32_t rate, uint32_t loop)
{
    if (!push)
    {
        FxPushError(fx, 0x005b8d18);
        return 0;
    }
    const uint32_t i = layer - 1;
    FxObject_EnsureTexAnim(push, u(fx));
    orig::fn_00453540(Field<uint32_t>(push, 4), 1);
    const uint32_t bit = 1u << (i & 31);
    Field<uint32_t>(Field<void*>(push, 4), 0x464) |= bit;
    Field<uint32_t*>(Field<void*>(push, 4), 0x518)[i] = rate;
    Field<uint32_t*>(Field<void*>(push, 4), 0x518)[4 + i] = frames;
    if (loop)
        Field<uint32_t*>(Field<void*>(push, 4), 0x518)[0x10] |= bit;
    return 0;
}

// 0x0041e3b0 (eax = fxpush, esi = fx; stack: value; callee pops 4): opcode "stop emitter": among the object's 10
// emitters (+0x4ec, 0x98 bytes each, flag 8 = active), the one before the first inactive (or the first, when it is
// inactive) gets +0x38 = value. With all 10 active: "Fx : Failed to stop emitter (no emitters)" (as in the
// original).
uint32_t Fx_StopEmitter(void* push, void* fx, uint32_t value)
{
    if (!push)
    {
        FxPushError(fx, 0x005b8cb4);
        return 0;
    }
    uint8_t* e = Field<uint8_t*>(Field<void*>(push, 4), 0x4ec);
    uint8_t* last = e;
    uint32_t i = 0;
    for (; i < 10; ++i, e += 0x98)
    {
        if (!(*e & 8))
            break;
        last = e;
    }
    if (i >= 10)
    {
        FxPushError(fx, 0x005b8ce8);
        return 0;
    }
    Field<uint32_t>(last, 0x38) = value;
    return 0;
}

// 0x0041e420 (eax = fxpush; stack: fx; callee pops 4): allocates a mesh emitter slot: the object's emitter array
// (+0x4ec, 10 x 0x98 bytes, "fx_emitter", zeroed) is created when missing; the first inactive slot gets flag 8 and
// the object flag 0x100 (+0x460). Returns the slot, or null ("Too many emitters (limit is %d)", 10).
uint32_t FxObject_AllocEmitter(void* push, void* fx)
{
    if (!push)
        return 0;
    if (Field<uint32_t>(Field<void*>(push, 4), 0x4ec) == 0)
    {
        Field<uint32_t>(Field<void*>(push, 4), 0x4ec) = orig::fn_004e07f0(kHeap, 0x12, 0x5f0, 2, 0x005b8ca8);
        std::memset(Field<void*>(Field<void*>(push, 4), 0x4ec), 0, 0x5f0);
    }
    void* obj = Field<void*>(push, 4);
    uint8_t* e = Field<uint8_t*>(obj, 0x4ec);
    for (uint32_t i = 0; i < 10; ++i, e += 0x98)
    {
        if (!(*e & 8))
        {
            Field<uint32_t>(obj, 0x460) |= 0x100;
            *reinterpret_cast<uint32_t*>(e) |= 8;
            return u(e);
        }
    }
    GameFunction<void(__cdecl*)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(0x00407ff0)(
        Field<uint32_t>(g_game, 0x34), 0xa, 0x005b8c68, Field<uint32_t>(fx, 8) + 0x24, 0xa);
    return 0;
}

// 0x0041e4d0 (ebx = fx, edi = fxpush; stack: unused, fx name, speed, attach-point list, a4, flags; callee pops
// 0x18): opcode "emit": finds the named fx (FxBank_Find) and fills a new emitter slot (FxObject_AllocEmitter):
// name +4 (0x20), +0x24 record, +0x28 speed, +0x2c -speed, +0x30 list, +0x34 a4, flags |= flags, +0x38 = -1.
// Flag 0x80 (without 0x40): the emitter matrix (+0x3c) translation = the object's position (virtual +0x54);
// 0x40, or 2 without 1: the matrix of a random attach point (0x00453c50 (object, point, +0x3c, 1, 0, 0, 1, 0, 1)).
// Errors "Failed to emit (no push)", "Failed to emit for fx %s\nCouldn't find fx %s".
uint32_t Fx_Emit(void* fx, void* push, uint32_t /*a0*/, const char* name, uint32_t speed, uint32_t list,
                 uint32_t a4, uint32_t flags)
{
    if (!push)
    {
        FxPushError(fx, 0x005b8c10);
        return 0;
    }
    const uint32_t record = FxBank_Find(fx, name);
    if (!record)
    {
        GameFunction<void(__cdecl*)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(0x00407ff0)(
            Field<uint32_t>(g_game, 0x34), 0xa, 0x005b8c34, Field<uint32_t>(fx, 8) + 0x24, u(name));
        return 0;
    }
    uint32_t* e = reinterpret_cast<uint32_t*>(FxObject_AllocEmitter(push, fx));
    if (!e)
        return 0;
    GameFunction<char*(__cdecl*)(char*, const char*, size_t)>(0x005382a0)(reinterpret_cast<char*>(e + 1), name, 0x20);
    e[0xb] = f2u(-u2f(speed));
    e[0xa] = speed;
    e[9] = record;
    const uint32_t f = e[0] | flags;
    e[0xc] = list;
    e[0xd] = a4;
    e[0] = f;
    e[0xe] = 0xbf800000u;  // -1.0
    if (static_cast<uint8_t>(f) & 0xc0)
    {
        if (!(static_cast<uint8_t>(f) & 0x40))
        {
            uint32_t pos[3];
            VCall<void>(Field<void*>(push, 4), 0x54 / 4, static_cast<void*>(pos));
            e[0x12] = pos[0];
            e[0x16] = pos[1];
            e[0x1a] = pos[2];
            return u(e);
        }
    }
    else if ((f & 1) || !(f & 2))
    {
        return u(e);
    }
    const uint32_t point = FxNameList_PickRandom(reinterpret_cast<const uint32_t*>(list));
    orig::fn_00453c50(Field<uint32_t>(push, 4), point, u(e + 0xf), 1, 0, 0, 1, 0, 1);
    return u(e);
}

// 0x0041e610 (eax = a4, ecx = attach-point list, edx = fxpush, esi = vec3 b; stack: fx, unused, fx name, speed,
// vec3 a, unused; callee pops 0x18): opcode "emit random": Fx_Emit (flags 2); on success the object gets flag
// 0x4000 (+0x460) and the emitter +0x80 = *a, +0x8c = *b. Returns the emitter + 0x8c (EAX as left), or prints
// "Fx : Failed to emit random for fx '%s'".
uint32_t Fx_EmitRandom(uint32_t a4, uint32_t list, void* push, const uint32_t* b, void* fx, uint32_t a1,
                       uint32_t name, uint32_t speed, const uint32_t* a, uint32_t /*a5*/)
{
    uint32_t* e = reinterpret_cast<uint32_t*>(
        Fx_Emit(fx, push, a1, reinterpret_cast<const char*>(name), speed, list, a4, 2));
    if (!e)
    {
        FxPushError(fx, 0x005b8be8);
        return 0;
    }
    Field<uint32_t>(Field<void*>(push, 4), 0x460) |= 0x4000;
    e[0x20] = a[0];
    e[0x21] = a[1];
    e[0x22] = a[2];
    e[0x23] = b[0];
    e[0x24] = b[1];
    e[0x25] = b[2];
    return u(e + 0x23);
}

// 0x0041e6b0 (eax = fxpush; stack: not read, callee pops 4): allocates a particle emitter slot: the object's
// particle emitter array (+0x4f0, 10 x 0x194 bytes, "fx_emitter", zeroed) is created when missing; the first
// inactive slot (flag 8) is taken and the object gets flag 0x200. Null when full.
uint32_t FxObject_AllocParticleEmitter(void* push, uint32_t /*unused*/)
{
    if (!push)
        return 0;
    if (Field<uint32_t>(Field<void*>(push, 4), 0x4f0) == 0)
    {
        Field<uint32_t>(Field<void*>(push, 4), 0x4f0) = orig::fn_004e07f0(kHeap, 0x12, 0xfc8, 2, 0x005b8ca8);
        std::memset(Field<void*>(Field<void*>(push, 4), 0x4f0), 0, 0xfc8);
    }
    void* obj = Field<void*>(push, 4);
    uint8_t* e = Field<uint8_t*>(obj, 0x4f0);
    for (int32_t i = 0; i < 10; ++i, e += 0x194)
    {
        if (!(*e & 8))
        {
            Field<uint32_t>(obj, 0x460) |= 0x200;
            *reinterpret_cast<uint32_t*>(e) |= 8;
            return u(e);
        }
    }
    return 0;
}

// 0x0041e730 (eax = fxpush; stack: not read, callee pops 4; 12 callers): the object's current particle emitter:
// the last of the leading active slots of +0x4f0 (null when there is no array or slot 0 is inactive).
uint32_t FxObject_CurrentParticleEmitter(void* push, uint32_t /*unused*/)
{
    if (!push)
        return 0;
    uint8_t* e = Field<uint8_t*>(Field<void*>(push, 4), 0x4f0);
    uint32_t last = 0;
    if (!e)
        return 0;
    for (int32_t i = 0; i < 10; ++i, e += 0x194)
    {
        if (!(*e & 8))
            break;
        last = u(e);
    }
    return last;
}

// 0x0041e760 (eax = position vec3; stack: fx, fx instance, fxpush, a3, a4, attach-point list, flags, vec3 c,
// vec3 d; callee pops 0x24): opcode "emit particle" (0x27 etc.): takes a particle emitter slot (0x0041e6b0),
// flags |= flags, +0xec = the instance's +0x6c object (reference +0x28). Direction / origin: flag 0x20: direction
// from the object (0x00453150 into +0x80) and its position (virtual +0x54 into +0x74); 0x40: a random attach
// point's matrix (0x00453c50): origin = its translation, direction = minus its column 1; else direction = *d,
// origin and +0xd0 = *position, +0xdc = *c. Then defaults: +4 a3, +8 a4, blur 1/1 (+0x1c/+0x20), +0x3c/+0x40
// 1e-6, +0x58/+0x60 1, size 0.2/0.3 (+0x14/+0x18), +0x10 -1, speed 8/30 (+0x44/+0x48), lifespan 4/7
// (+0x2c/+0x30), cone 36/36 (+0x4c/+0x50), colour 255 (+0x64..+0x6c), the rest 0. Errors "Failed to emit
// particle (no push)", "... Too many emitters (limit is %d)".
uint32_t Fx_EmitParticle(const uint32_t* position, void* fx, void* instance, void* push, uint32_t a3, uint32_t a4,
                         uint32_t list, uint32_t flags, const uint32_t* c, const uint32_t* d)
{
    if (!push)
    {
        FxPushError(fx, 0x005b8b70);
        return 0;
    }
    uint32_t* e = reinterpret_cast<uint32_t*>(FxObject_AllocParticleEmitter(push, u(fx)));
    if (!e)
    {
        GameFunction<void(__cdecl*)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(0x00407ff0)(
            Field<uint32_t>(g_game, 0x34), 0xa, 0x005b8ba0, Field<uint32_t>(fx, 8) + 0x24, 0xa);
        return 0;
    }
    e[0] |= flags;
    void* ref = Field<void*>(instance, 0x6c);
    e[0x3b] = u(ref);
    if (ref)
        ++Field<int32_t>(ref, 0x28);
    if (static_cast<uint8_t>(flags) & 0x20)
    {
        orig::fn_00453150(Field<uint32_t>(push, 4), u(e + 0x20));
        VCall<void>(Field<void*>(push, 4), 0x54 / 4, static_cast<void*>(e + 0x1d));
    }
    else if (static_cast<uint8_t>(flags) & 0x40)
    {
        alignas(4) float m[17];
        const uint32_t point = FxNameList_PickRandom(reinterpret_cast<const uint32_t*>(list));
        orig::fn_00453c50(Field<uint32_t>(push, 4), point, u(m), 1, 0, 0, 1, 0, 1);
        reinterpret_cast<float*>(e)[0x20] = -m[1];
        std::memcpy(&e[0x1d], &m[3], 4);
        reinterpret_cast<float*>(e)[0x21] = -m[5];
        std::memcpy(&e[0x1e], &m[7], 4);
        std::memcpy(&e[0x1f], &m[11], 4);
        reinterpret_cast<float*>(e)[0x22] = -m[9];
    }
    else
    {
        e[0x20] = d[0], e[0x21] = d[1], e[0x22] = d[2];
        e[0x1d] = position[0], e[0x1e] = position[1], e[0x1f] = position[2];
        e[0x34] = position[0], e[0x35] = position[1], e[0x36] = position[2];
        e[0x37] = c[0], e[0x38] = c[1], e[0x39] = c[2];
    }
    e[1] = a3;
    e[0x20 / 4] = 0x3f800000u;
    e[0x1c / 4] = 0x3f800000u;
    e[0x40 / 4] = 0x358637bdu;
    e[0x3c / 4] = 0x358637bdu;
    e[0x58 / 4] = 0x3f800000u;
    e[0x60 / 4] = 0x3f800000u;
    e[0xc / 4] = 0;
    e[0x24 / 4] = 0;
    e[0x28 / 4] = 0;
    e[0x34 / 4] = 0;
    e[0x38 / 4] = 0;
    e[0x54 / 4] = 0;
    e[0x5c / 4] = 0;
    e[0x70 / 4] = 0;
    e[2] = a4;
    e[0x10 / 4] = 0xbf800000u;
    e[0x18 / 4] = 0x3e99999au;
    e[0x14 / 4] = 0x3e4ccccdu;
    e[0x48 / 4] = 0x41f00000u;
    e[0x44 / 4] = 0x41000000u;
    e[0x30 / 4] = 0x40e00000u;
    e[0x2c / 4] = 0x40800000u;
    e[0x50 / 4] = 0x42100000u;
    e[0x4c / 4] = 0x42100000u;
    e[0x64 / 4] = 0x437f0000u;
    e[0x68 / 4] = 0x437f0000u;
    e[0x6c / 4] = 0x437f0000u;
    return 0x437f0000u;
}

// 0x0041e990 (ebx = fx, esi = fxpush): opcode "start dir particle": the current particle emitter is started
// (0x0041a050, eax = object; stack: emitter) and then cleared (0x194 bytes). Errors "need a push",
// "need EmitParticleRef".
uint32_t Fx_StartDirParticle(void* fx, void* push)
{
    if (!push)
    {
        FxPushError(fx, 0x005b8b00);
        return 0;
    }
    uint32_t e = FxObject_CurrentParticleEmitter(push, u(fx));
    if (!e)
    {
        FxPushError(fx, 0x005b8b34);
        return 0;
    }
    orig::fn_0041a050(Field<uint32_t>(push, 4), e);
    std::memset(p(e), 0, 0x194);
    return 0;
}

// Particle emitter setters (eax = fxpush, esi = fx; stack: a, b; callee pops 8): the current particle emitter
// (0x0041e730) gets two values; errors "need a push" / "need EmitParticle".
static uint32_t Fx_SetParticlePair(void* push, void* fx, uint32_t a, uint32_t b, uint32_t offset, uint32_t noPush,
                                   uint32_t noEmitter)
{
    if (!push)
    {
        FxPushError(fx, noPush);
        return 0;
    }
    uint32_t e = FxObject_CurrentParticleEmitter(push, u(fx));
    if (!e)
    {
        FxPushError(fx, noEmitter);
        return 0;
    }
    Field<uint32_t>(p(e), offset + 4) = b;
    Field<uint32_t>(p(e), offset) = a;
    return e;
}
// 0x0041e9f0 "setup particle blur": +0x1c, +0x20.
uint32_t Fx_SetParticleBlur(void* push, void* fx, uint32_t a, uint32_t b)
{
    return Fx_SetParticlePair(push, fx, a, b, 0x1c, 0x005b8a90, 0x005b8ac4);
}
// 0x0041ea40 "setup particle size": +0x14, +0x18.
uint32_t Fx_SetParticleSize(void* push, void* fx, uint32_t a, uint32_t b)
{
    return Fx_SetParticlePair(push, fx, a, b, 0x14, 0x005b8a20, 0x005b8a54);
}
// 0x0041ea90 "setup particle scale": +0x34, +0x38.
uint32_t Fx_SetParticleScale(void* push, void* fx, uint32_t a, uint32_t b)
{
    return Fx_SetParticlePair(push, fx, a, b, 0x34, 0x005b89ac, 0x005b89e4);
}
// 0x0041eae0 "setup particle lifespan": +0x2c, +0x30.
uint32_t Fx_SetParticleLifespan(void* push, void* fx, uint32_t a, uint32_t b)
{
    return Fx_SetParticlePair(push, fx, a, b, 0x2c, 0x005b8934, 0x005b896c);
}
// 0x0041ee50 "setup particle speed": +0x44, +0x48.
uint32_t Fx_SetParticleSpeed(void* push, void* fx, uint32_t a, uint32_t b)
{
    return Fx_SetParticlePair(push, fx, a, b, 0x44, 0x005b87e4, 0x005b881c);
}
// 0x0041eea0 "setup particle gravity": +0x24, +0x28.
uint32_t Fx_SetParticleGravity(void* push, void* fx, uint32_t a, uint32_t b)
{
    return Fx_SetParticlePair(push, fx, a, b, 0x24, 0x005b876c, 0x005b87a4);
}
// 0x0041f2f0 "setup particle cone angle": +0x4c, +0x50.
uint32_t Fx_SetParticleConeAngle(void* push, void* fx, uint32_t a, uint32_t b)
{
    return Fx_SetParticlePair(push, fx, a, b, 0x4c, 0x005b85f8, 0x005b8638);
}

// 0x0041eb30 (stack: not read, callee pops 4): allocates a zeroed 0xc8-byte "particle color table" (0x004e07f0)
// with vtable 0x005b7ddc; null on failure (cleared before the null test, as in the original).
uint32_t ParticleColourTable_Alloc(uint32_t /*unused*/)
{
    uint32_t* t = reinterpret_cast<uint32_t*>(orig::fn_004e07f0(kHeap, 0x12, 0xc8, 2, 0x005b891c));
    std::memset(t, 0, 0xc8);
    if (!t)
        return 0;
    t[0] = 0x005b7ddc;
    return u(t);
}

// 0x0041eb70 (eax = fxpush, ecx = fx; stack: fade-in start, fade-in end, fade-out start; callee pops 12): opcode
// "setup particle fadeinout": builds the current particle emitter's colour table (+0x190, 0x0041eb30; a second
// fade prints "duplicate fade command" and rebuilds the existing one): 10 entries of rgba downwards from +0x98
// (rgb 1), +0xa8..+0xc0 = 1, +0xc4 = 0, +0xb4 = 1 when fade-in end == 0, else 0; +4 = 1. Time per entry
// step = T * [0x005bcbfc] with T = (lifespan max + lifespan min) * [0x005bcb4c]; alpha of entry i at
// t = i * [0x005bcbfc] * T: 0 before the fade-in start, a rising ramp (count / in-steps, capped at 1) until the
// fade-in end, 1 until the fade-out start, a falling ramp (1 - count / out-steps, 0 past it) until lifespan min,
// then 0.
uint32_t Fx_SetParticleFadeInOut(void* push, void* fx, uint32_t inStartBits, uint32_t inEndBits,
                                 uint32_t outStartBits)
{
    if (!push)
    {
        FxPushError(fx, 0x005b8858);
        return 0;
    }
    uint8_t* e = reinterpret_cast<uint8_t*>(FxObject_CurrentParticleEmitter(push, u(fx)));
    if (!e)
    {
        FxPushError(fx, 0x005b8894);
        return 0;
    }
    if (Field<uint32_t>(e, 0x190))
        FxPushError(fx, 0x005b88d8);
    else
        Field<uint32_t>(e, 0x190) = ParticleColourTable_Alloc(u(fx));
    const float inStart = u2f(inStartBits), inEnd = u2f(inEndBits), outStart = u2f(outStartBits);
    int32_t outCount = 1;
    int32_t inCount = 1;
    const float total = (Field<float>(e, 0x30) + Field<float>(e, 0x2c)) * GameGlobal<float>(0x005bcb4c);
    const float lifeMin = Field<float>(e, 0x2c);
    auto table = [&]() { return Field<uint8_t*>(e, 0x190); };
    Field<uint32_t>(table(), 0xb8) = 0x3f800000u;
    const float step = total * GameGlobal<float>(0x005bcbfc);
    Field<uint32_t>(table(), 0xbc) = 0x3f800000u;
    Field<uint32_t>(table(), 0xc0) = 0x3f800000u;
    Field<uint32_t>(table(), 0xc4) = 0;
    Field<uint32_t>(table(), 0xa8) = 0x3f800000u;
    Field<uint32_t>(table(), 0xac) = 0x3f800000u;
    Field<uint32_t>(table(), 0xb0) = 0x3f800000u;
    const float inSteps = (inEnd - inStart) / step;
    const float outSteps = (lifeMin - outStart) / step;
    Field<uint32_t>(table(), 0xb4) = (inEnd == Zero()) ? 0x3f800000u : 0u;
    const float one = GameGlobal<float>(0x005bcb2c);
    for (int32_t i = 0; i < 10; ++i)
    {
        uint8_t* entry = table() - 0x10 * i;
        Field<uint32_t>(entry, 0x98) = 0x3f800000u;
        Field<uint32_t>(entry, 0x9c) = 0x3f800000u;
        Field<uint32_t>(table() + 0xa0 - 0x10 * i, 0) = 0x3f800000u;
        const float t = static_cast<float>(i) * GameGlobal<float>(0x005bcbfc) * total;
        float alpha;
        if (t < inStart)
        {
            alpha = 0.0f;
        }
        else if (t < inEnd)
        {
            const float n = static_cast<float>(inCount);
            alpha = (n > inSteps) ? 1.0f : (one / inSteps) * n;
            ++inCount;
        }
        else if (t < outStart)
        {
            alpha = 1.0f;
        }
        else if (t < lifeMin)
        {
            const float n = static_cast<float>(outCount);
            alpha = (n > outSteps) ? 0.0f : one - (one / outSteps) * n;
            ++outCount;
        }
        else
        {
            alpha = 0.0f;
        }
        Field<float>(table() - 0x10 * i, 0xa4) = alpha;
    }
    Field<uint32_t>(Field<void*>(e, 0x190), 4) = 1;
    return 0;
}

// x87 fsin / fcos, as the original (not the CRT's).
static float X87Sin(float x)
{
    float r;
    __asm {
        fld x
        fsin
        fstp r
    }
    return r;
}
static float X87Cos(float x)
{
    float r;
    __asm {
        fld x
        fcos
        fstp r
    }
    return r;
}

// 0x0041eef0 / 0x0041f0f0 (eax = fxpush, ecx = fx; stack: x, y, z angles in degrees; callee pops 12): opcodes
// "setup particle axis" / "axis relative": angles * [0x005bcd60] * [0x005bcb70] (to radians); rotation matrices
// X (A), Y (C), Z (B) (0x44 bytes, flags word 1); D = 0x004e2910 (eax = C; out D, A), emitter +0x8c = 0x004e2910
// (eax = B; out +0x8c, D); the emitter direction +0x80 is rotated by it (0x004e36a0, eax = +0x80, ecx = +0x8c,
// edx = 1; out); flag 0x200 / 0x400 in the emitter.
static uint32_t Fx_SetParticleAxisImpl(void* push, void* fx, uint32_t ax, uint32_t ay, uint32_t az, uint32_t flag,
                                       uint32_t noPush, uint32_t noEmitter)
{
    if (!push)
    {
        FxPushError(fx, noPush);
        return 0;
    }
    uint32_t* e = reinterpret_cast<uint32_t*>(FxObject_CurrentParticleEmitter(push, u(fx)));
    if (!e)
    {
        FxPushError(fx, noEmitter);
        return 0;
    }
    const float k1 = GameGlobal<float>(0x005bcd60), k2 = GameGlobal<float>(0x005bcb70);
    alignas(8) float a[17] = {};
    alignas(8) float b[17];
    alignas(8) float c[17] = {};
    alignas(8) float d[17];
    float out[3];
    const float tx = u2f(ax) * k1 * k2;
    const float sx = X87Sin(tx);
    reinterpret_cast<uint32_t*>(a)[16] = 1;
    reinterpret_cast<uint32_t*>(c)[16] = 1;
    a[0] = 1.0f;
    a[15] = 1.0f;
    c[5] = 1.0f;
    c[15] = 1.0f;
    const float cx = X87Cos(tx);
    a[5] = cx;
    a[6] = -sx;
    a[9] = sx;
    a[10] = cx;
    const float ty = u2f(ay) * k1 * k2;
    const float sy = X87Sin(ty);
    const float cy = X87Cos(ty);
    c[2] = sy;
    std::memset(b, 0, sizeof(b));
    reinterpret_cast<uint32_t*>(b)[16] = 1;
    b[10] = 1.0f;
    b[15] = 1.0f;
    c[0] = cy;
    c[8] = -sy;
    c[10] = cy;
    const float tz = u2f(az) * k1 * k2;
    const float sz = X87Sin(tz);
    const float cz = X87Cos(tz);
    b[0] = cz;
    b[1] = -sz;
    b[4] = sz;
    b[5] = cz;
    orig::fn_004e2910(u(c), u(d), u(a));
    orig::fn_004e2910(u(b), u(e + 0x23), u(d));
    orig::fn_004e36a0(u(e + 0x20), u(e + 0x23), 1, u(out));
    std::memcpy(&e[0x20], &out[0], 4);
    std::memcpy(&e[0x21], &out[1], 4);
    const uint32_t f = e[0] | flag;
    std::memcpy(&e[0x22], &out[2], 4);
    e[0] = f;
    return f;
}
uint32_t Fx_SetParticleAxis(void* push, void* fx, uint32_t ax, uint32_t ay, uint32_t az)
{
    return Fx_SetParticleAxisImpl(push, fx, ax, ay, az, 0x200, 0x005b86fc, 0x005b8730);
}
uint32_t Fx_SetParticleAxisRelative(void* push, void* fx, uint32_t ax, uint32_t ay, uint32_t az)
{
    return Fx_SetParticleAxisImpl(push, fx, ax, ay, az, 0x400, 0x005b8678, 0x005b86b8);
}

// 0x0041f340 (eax = fxpush, esi = fx; stack: a, b; callee pops 8): opcode "setup particle damping": emitter
// +0x40 = b, +0x3c = a clamped below to [0x005bcc68].
uint32_t Fx_SetParticleDamping(void* push, void* fx, uint32_t a, uint32_t b)
{
    if (!push)
    {
        FxPushError(fx, 0x005b8580);
        return 0;
    }
    uint32_t e = FxObject_CurrentParticleEmitter(push, u(fx));
    if (!e)
    {
        FxPushError(fx, 0x005b85b8);
        return 0;
    }
    Field<uint32_t>(p(e), 0x40) = b;
    const float k = GameGlobal<float>(0x005bcc68);
    Field<float>(p(e), 0x3c) = (u2f(a) < k) ? k : u2f(a);
    return 0;
}

// 0x0041f3b0 (eax = fxpush, esi = fx; stack: u0, v0, u1, v1; callee pops 0x10): opcode "setup particle uvs":
// emitter +0x54..+0x60.
uint32_t Fx_SetParticleUvs(void* push, void* fx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    if (!push)
    {
        FxPushError(fx, 0x005b8510);
        return 0;
    }
    uint32_t e = FxObject_CurrentParticleEmitter(push, u(fx));
    if (!e)
    {
        FxPushError(fx, 0x005b8544);
        return 0;
    }
    Field<uint32_t>(p(e), 0x54) = a0;
    Field<uint32_t>(p(e), 0x58) = a1;
    Field<uint32_t>(p(e), 0x5c) = a2;
    Field<uint32_t>(p(e), 0x60) = a3;
    return e;
}

// 0x0041f410 (eax = fxpush, esi = fx; stack: a, r, g, b; callee pops 0x10): opcode "setup particle color":
// emitter +0x70 = a, +0x64..+0x6c = r, g, b. Without a push it prints the uvs message (0x005b8510), as the original.
uint32_t Fx_SetParticleColour(void* push, void* fx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    if (!push)
    {
        FxPushError(fx, 0x005b8510);
        return 0;
    }
    uint32_t e = FxObject_CurrentParticleEmitter(push, u(fx));
    if (!e)
    {
        FxPushError(fx, 0x005b84d4);
        return 0;
    }
    Field<uint32_t>(p(e), 0x70) = a0;
    Field<uint32_t>(p(e), 0x64) = a1;
    Field<uint32_t>(p(e), 0x68) = a2;
    Field<uint32_t>(p(e), 0x6c) = a3;
    return e;
}

// 0x0041f470 (ecx, ebx, edi; stack: fx system, pool index, a2..a7; callee pops 0x20; caller 0x0041a050): forwards
// to 0x0046e4c0 (ebx, edi; stack: pool = system +0x1c[index], a2, a3, a4, a5, a6, ecx, a7) (particle spawn into
// one of the system's three pools, guess).
uint32_t FxSystem_SpawnInPool(uint32_t ecx, uint32_t ebx, uint32_t edi, void* sys, uint32_t index, uint32_t a2,
                              uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7)
{
    return orig::fn_0046e4c0(ebx, edi, Field<uint32_t>(sys, 0x1c + index * 4), a2, a3, a4, a5, a6, ecx, a7);
}

// 0x0041f4b0 (ecx, ebx, esi, edi; stack: fx system, pool index, a2..a6; callee pops 0x1c; caller 0x0041a050):
// forwards to 0x0046e650 (ebx, esi, edi; stack: pool, a2, a3, a4, a5, ecx, a6).
uint32_t FxSystem_SpawnInPool2(uint32_t ecx, uint32_t ebx, uint32_t esi, uint32_t edi, void* sys, uint32_t index,
                               uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6)
{
    return orig::fn_0046e650(ebx, esi, edi, Field<uint32_t>(sys, 0x1c + index * 4), a2, a3, a4, a5, ecx, a6);
}

// 0x0041f500 (ecx = fxpush; stack: not read, callee pops 4): opcode "streamer": object flag 0x8000000 (+0x460)
// and a new streamer (0x00494ee0, edi = Game+0xa94 streamer manager; stack: 0) in the first free of the 8 slots
// at +0x4f4. Returns it, or 0 when all are used or there is no push.
uint32_t FxObject_AddStreamer(void* push, uint32_t /*unused*/)
{
    if (!push)
        return 0;
    Field<uint32_t>(Field<void*>(push, 4), 0x460) |= 0x8000000;
    uint32_t* slot = reinterpret_cast<uint32_t*>(Field<uint32_t>(push, 4) + 0x4f4);
    for (int32_t i = 0; i < 8; ++i, ++slot)
    {
        if (*slot == 0)
        {
            const uint32_t s = orig::fn_00494ee0(Field<uint32_t>(g_game, 0xa94), 0);
            *slot = s;
            return s;
        }
    }
    return 0;
}

// 0x0041f560 (eax = fxpush; stack: not read, callee pops 4): the object's current streamer: the last of the
// leading non-null slots of +0x4f4 (needs flag 0x8000000), or 0.
uint32_t FxObject_CurrentStreamer(void* push, uint32_t /*unused*/)
{
    if (!push)
        return 0;
    void* obj = Field<void*>(push, 4);
    if (!(Field<uint32_t>(obj, 0x460) & 0x8000000))
        return 0;
    uint32_t* last = nullptr;
    uint32_t* slot = reinterpret_cast<uint32_t*>(u(obj) + 0x4f4);
    for (int32_t i = 0; i < 8; ++i, ++slot)
    {
        if (*slot == 0)
            break;
        last = slot;
    }
    return last ? *last : 0;
}

// 0x0041f5a0 (eax = fxpush, ebx = texture name or null; stack: fx; callee pops 4): opcode "streamer texture":
// texture looked up in the texture bank Game+0xa78 (0x004881c0, ecx = name, esi = bank; it reads no stack
// argument) and set on the current streamer (0x004952b0, eax = streamer, ecx = texture). A missing texture prints
// "Failed to find streamer texture %s\n%s" and passes the print's EAX as the texture (as the original).
uint32_t Fx_SetStreamerTexture(void* push, const char* name, void* fx)
{
    const uint32_t s = FxObject_CurrentStreamer(push, u(fx));
    if (!s)
    {
        FxPushError(fx, 0x005b847c);
        return 0;
    }
    if (!name)
        return orig::fn_004952b0(s, 0);
    const uint32_t tex = orig::fn_004881c0(u(name), Field<uint32_t>(g_game, 0xa78), 0, 0, 0, 0, 0, 0, 0, 0);
    if (tex)
        return orig::fn_004952b0(s, tex);
    const uint32_t r = GameFunction<uint32_t(__cdecl*)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(0x00407ff0)(
        Field<uint32_t>(g_game, 0x34), 0xa, 0x005b84a8, u(name), Field<uint32_t>(fx, 8) + 0x24);
    return orig::fn_004952b0(s, r);
}

// 0x0041f640 (eax = fxpush, esi = uv rectangle (4 floats), edi = fx): opcode "streamer texture uv's":
// 0x00495270 (streamer; uv[0], uv[1]) and 0x00495290 (streamer; uv[2], uv[3]) (they write +0x48/+0x50 and
// +0x4c/+0x54).
uint32_t Fx_SetStreamerUvs(void* push, const uint32_t* uv, void* fx)
{
    const uint32_t s = FxObject_CurrentStreamer(push, u(fx));
    if (!s)
    {
        FxPushError(fx, 0x005b844c);
        return 0;
    }
    orig::fn_00495270(s, uv[0], uv[1]);
    return orig::fn_00495290(s, uv[2], uv[3]);
}

// 0x0041f690 / 0x0041f6d0 (eax = fxpush, esi = fx; stack: value; callee pops 4): opcodes "streamer transparency
// mode" (0x004952d0) / "streamer thickness" (0x00495060), each (eax = value, ecx = streamer).
uint32_t Fx_SetStreamerTransparency(void* push, void* fx, uint32_t value)
{
    const uint32_t s = FxObject_CurrentStreamer(push, u(fx));
    if (!s)
    {
        FxPushError(fx, 0x005b8418);
        return 0;
    }
    return orig::fn_004952d0(value, s);
}
uint32_t Fx_SetStreamerThickness(void* push, void* fx, uint32_t value)
{
    const uint32_t s = FxObject_CurrentStreamer(push, u(fx));
    if (!s)
    {
        FxPushError(fx, 0x005b83ec);
        return 0;
    }
    return orig::fn_00495060(value, s);
}

// 0x0041f710 / 0x0041f750 / 0x0041f790 / 0x0041f7d0 (eax = fxpush, ecx = fx, edi = colour curve): opcodes
// "streamer color alpha / red / green / blue": 0x00495080 / 0x00495100 / 0x00495180 / 0x00495200 (esi = streamer,
// edi = curve).
static uint32_t Fx_SetStreamerChannel(void* push, void* fx, uint32_t curve, uint32_t target, uint32_t error)
{
    const uint32_t s = FxObject_CurrentStreamer(push, u(fx));
    if (!s)
    {
        FxPushError(fx, error);
        return 0;
    }
    switch (target)
    {
    case 0x00495080:
        return orig::fn_00495080(s, curve);
    case 0x00495100:
        return orig::fn_00495100(s, curve);
    case 0x00495180:
        return orig::fn_00495180(s, curve);
    default:
        return orig::fn_00495200(s, curve);
    }
}
uint32_t Fx_SetStreamerAlpha(void* push, void* fx, uint32_t curve)
{
    return Fx_SetStreamerChannel(push, fx, curve, 0x00495080, 0x005b83bc);
}
uint32_t Fx_SetStreamerRed(void* push, void* fx, uint32_t curve)
{
    return Fx_SetStreamerChannel(push, fx, curve, 0x00495100, 0x005b8390);
}
uint32_t Fx_SetStreamerGreen(void* push, void* fx, uint32_t curve)
{
    return Fx_SetStreamerChannel(push, fx, curve, 0x00495180, 0x005b8360);
}
uint32_t Fx_SetStreamerBlue(void* push, void* fx, uint32_t curve)
{
    return Fx_SetStreamerChannel(push, fx, curve, 0x00495200, 0x005b8334);
}

// 0x0041f810 (eax = fxpush, esi = fx; stack: length; callee pops 4): opcode "streamer length": tail call of
// 0x00495050 (ecx = streamer; stack: length).
uint32_t Fx_SetStreamerLength(void* push, void* fx, uint32_t length)
{
    const uint32_t s = FxObject_CurrentStreamer(push, u(fx));
    if (!s)
    {
        FxPushError(fx, 0x005b830c);
        return 0;
    }
    return orig::fn_00495050(s, length);
}

// 0x0041f850 (esi = fxpush; stack: fx, a1, a2, a3; callee pops 0x10): opcode "initial rotation": R = 0x004e3db0
// (eax = R, ecx = a3; stack: a1, a3, a2) (a rotation from three angles, guess), the object's matrix (virtual
// +0x70) W, X = 0x004e2910 (eax = R; out X, W), set by 0x00457990 (eax = object with al = 0; stack: object, X).
// Error "Failed to setup initial rotation for '%s' (need a fxpushobj)".
uint32_t Fx_SetInitialRotation(void* push, void* fx, uint32_t a1, uint32_t a2, uint32_t a3)
{
    if (!push)
    {
        FxPushError(fx, 0x005b82c8);
        return 0;
    }
    alignas(8) uint32_t r[17];
    alignas(8) uint32_t w[17];
    alignas(8) uint32_t x[17];
    orig::fn_004e3db0(u(r), a3, a1, a3, a2);
    VCall<void>(Field<void*>(push, 4), 0x70 / 4, static_cast<void*>(w));
    orig::fn_004e2910(u(r), u(x), u(w));
    const uint32_t obj = Field<uint32_t>(push, 4);
    return orig::fn_00457990(obj & ~0xffu, obj, u(x));
}

// 0x0041f8f0 (esi = fxpush; stack: fx, x, y, z; callee pops 0x10): opcode "rotate object": wakes the object
// (0x004577b0) and 0x00455b80 (eax = vec3 (x, y, z), edx = 0x10; stack: object, 1/120 (0x3c088889), -1, 0).
uint32_t Fx_RotateObject(void* push, void* fx, uint32_t x, uint32_t y, uint32_t z)
{
    if (!push)
    {
        FxPushError(fx, 0x005b829c);
        return 0;
    }
    uint32_t v[3] = {x, y, z};
    orig::fn_004577b0(Field<uint32_t>(push, 4), 0);
    return orig::fn_00455b80(u(v), 0x10, Field<uint32_t>(push, 4), 0x3c088889u, 0xffffffffu, 0);
}

// 0x0041f970 (esi = fxpush; stack: fx, offset; callee pops 8): opcode "set height offset": position (virtual
// +0x6c) with z += offset, set by 0x00457870 (eax = the virtual's EAX with al = 0, ecx = position, edx = object;
// it reads no stack argument although abi.csv lists 3).
uint32_t Fx_SetHeightOffset(void* push, void* fx, uint32_t offset)
{
    if (!push)
    {
        FxPushError(fx, 0x005b826c);
        return 0;
    }
    float v[3];
    const uint32_t r = VCall<uint32_t>(Field<void*>(push, 4), 0x6c / 4, static_cast<void*>(v));
    v[2] = v[2] + u2f(offset);
    return static_cast<uint32_t>(orig::fn_00457870(r & ~0xffu, u(v), Field<uint32_t>(push, 4), 0, 0, 0));
}

// 0x0041f9d0 (esi = fxpush; stack: fx; callee pops 4; 6 callers): opcode "set hit ground": flag 0x10000 (+0x460);
// the object's virtual +0xf8 (0x70001 when it also has 0x20000, else 0x20000; 0) (collision mask, guess); then
// tail call of 0x004577b0 (eax = object; stack: 0).
uint32_t Fx_SetHitGround(void* push, void* fx)
{
    if (!push)
    {
        FxPushError(fx, 0x005b8240);
        return 0;
    }
    Field<uint32_t>(Field<void*>(push, 4), 0x460) |= 0x10000;
    void* obj = Field<void*>(push, 4);
    if (Field<uint32_t>(obj, 0x460) & 0x20000)
        VCall<void>(obj, 0xf8 / 4, 0x70001u, 0u);
    else
        VCall<void>(obj, 0xf8 / 4, 0x20000u, 0u);
    return static_cast<uint32_t>(orig::fn_004577b0(Field<uint32_t>(push, 4), 0));
}

// 0x0041fa60 (esi = fxpush; stack: fx; callee pops 4): the counterpart of 0x0041f9d0 with flag 0x20000 (virtual
// +0xf8 (0x70001 when it also has 0x10000, else 0x50000; 0)); prints the same "set hit ground" message.
uint32_t fn_0041fa60(void* push, void* fx)
{
    if (!push)
    {
        FxPushError(fx, 0x005b8240);
        return 0;
    }
    Field<uint32_t>(Field<void*>(push, 4), 0x460) |= 0x20000;
    void* obj = Field<void*>(push, 4);
    if (Field<uint32_t>(obj, 0x460) & 0x10000)
        VCall<void>(obj, 0xf8 / 4, 0x70001u, 0u);
    else
        VCall<void>(obj, 0xf8 / 4, 0x50000u, 0u);
    return static_cast<uint32_t>(orig::fn_004577b0(Field<uint32_t>(push, 4), 0));
}

// 0x0041faf0 (eax = fx, esi = fxpush): Fx_SetHitGround(push, fx).
uint32_t fn_0041faf0(void* fx, void* push)
{
    return Fx_SetHitGround(push, fx);
}

// 0x0041fb00 (eax = fxpush, edi = fx; stack: a0, a1, a2, a3; callee pops 0x10): opcode "on hit aoe": hit ground
// (0x0041f9d0), then +0x540 = a0, +0x530 = a1, +0x534 = a2 (inner), +0x538 = a3 (outer); +0x53c = 1 / (outer -
// inner) when outer > [0x005bcbec] and outer > inner, else 1; flag 0x1000 in +0x464.
uint32_t Fx_SetOnHitAoe(void* push, void* fx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    Fx_SetHitGround(push, fx);
    if (!push)
    {
        FxPushError(fx, 0x005b8214);
        return 0;
    }
    Field<uint32_t>(Field<void*>(push, 4), 0x540) = a0;
    Field<uint32_t>(Field<void*>(push, 4), 0x530) = a1;
    Field<uint32_t>(Field<void*>(push, 4), 0x534) = a2;
    Field<uint32_t>(Field<void*>(push, 4), 0x538) = a3;
    void* o = Field<void*>(push, 4);
    if (Field<float>(o, 0x538) > GameGlobal<float>(0x005bcbec) && Field<float>(o, 0x538) > Field<float>(o, 0x534))
        Field<float>(o, 0x53c) = GameGlobal<float>(0x005bcb2c) / (Field<float>(o, 0x538) - Field<float>(o, 0x534));
    else
        Field<uint32_t>(o, 0x53c) = 0x3f800000u;
    Field<uint32_t>(Field<void*>(push, 4), 0x464) |= 0x1000;
    return 0;
}

// 0x0041fbd0 / 0x0041fc20 (eax = fxpush, edi = fx): opcodes "on hit death" (+0x460 |= 0x800) / "on hit delete"
// (+0x460 |= 0x1000), after hit ground (0x0041f9d0).
uint32_t Fx_SetOnHitDeath(void* push, void* fx)
{
    Fx_SetHitGround(push, fx);
    if (!push)
    {
        FxPushError(fx, 0x005b81e4);
        return 0;
    }
    Field<uint32_t>(Field<void*>(push, 4), 0x460) |= 0x800;
    return 0;
}
uint32_t Fx_SetOnHitDelete(void* push, void* fx)
{
    Fx_SetHitGround(push, fx);
    if (!push)
    {
        FxPushError(fx, 0x005b81b4);
        return 0;
    }
    Field<uint32_t>(Field<void*>(push, 4), 0x460) |= 0x1000;
    return 0;
}

// 0x0041fc70 (eax = fxpush, ebx = fx name, edi = fx system; stack: a0, a1; callee pops 8): opcode "on hit
// create": after hit ground, the named fx (FxBank_Find) goes to +0x520, flag 0x2000, +0x52c = a0, +0x524 = a1,
// +0x528 = 0. Errors "Failed to set hit create (no push)", "Failed to set on hit create (missing effect)".
uint32_t Fx_SetOnHitCreate(void* push, const char* name, void* fx, uint32_t a0, uint32_t a1)
{
    Fx_SetHitGround(push, fx);
    if (!push)
    {
        FxPushError(fx, 0x005b8150);
        return 0;
    }
    const uint32_t record = FxBank_Find(fx, name);
    if (!record)
    {
        ConsolePrint(0xa, 0x005b817c, u(name));
        return 0;
    }
    Field<uint32_t>(Field<void*>(push, 4), 0x460) |= 0x2000;
    Field<uint32_t>(Field<void*>(push, 4), 0x520) = record;
    Field<uint32_t>(Field<void*>(push, 4), 0x52c) = a0;
    Field<uint32_t>(Field<void*>(push, 4), 0x524) = a1;
    Field<uint32_t>(Field<void*>(push, 4), 0x528) = 0;
    return 0;
}

// 0x0041fd10 (eax = fx system, esi = fxpush, edi = fx name): opcode "on death": the named fx (FxBank_Find) goes
// to +0x51c with flag 1 (+0x460). Errors "Failed to set on death action (no push)", "(missing effect)".
uint32_t Fx_SetOnDeath(void* fx, void* push, const char* name)
{
    if (!push)
    {
        FxPushError(fx, 0x005b80f0);
        return 0;
    }
    const uint32_t record = FxBank_Find(fx, name);
    if (!record)
    {
        ConsolePrint(0xa, 0x005b8120, u(name));
        return 0;
    }
    Field<uint32_t>(Field<void*>(push, 4), 0x460) |= 1;
    Field<uint32_t>(Field<void*>(push, 4), 0x51c) = record;
    return record;
}

// 0x0041fd70 (eax = fxpush, ecx = flags; stack: fx; callee pops 4): opcode "kite": +0x460 |= flags.
uint32_t Fx_SetKite(void* push, uint32_t flags, void* fx)
{
    if (!push)
    {
        FxPushError(fx, 0x005b80c8);
        return 0;
    }
    Field<uint32_t>(Field<void*>(push, 4), 0x460) |= flags;
    return Field<uint32_t>(push, 4) + 0x460;
}

// 0x0041fdb0 (eax = fxpush, ecx = flag; stack: fx; callee pops 4): opcode "shader flag": +0x464 |= flag. The
// error print passes the fx name and the flag in the opposite order to its format "(%d) ... %s" (original bug).
uint32_t Fx_SetShaderFlag(void* push, uint32_t flag, void* fx)
{
    if (!push)
    {
        GameFunction<void(__cdecl*)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(0x00407ff0)(
            Field<uint32_t>(g_game, 0x34), 0xa, 0x005b8094, Field<uint32_t>(fx, 8) + 0x24, flag);
        return 0;
    }
    Field<uint32_t>(Field<void*>(push, 4), 0x464) |= flag;
    return Field<uint32_t>(push, 4) + 0x464;
}

// 0x0041fe00 (eax = fxpush; ecx is only pushed as a stack slot; stack: fx, a1, on; callee pops 12): opcode "set
// background": flag 0x10000000 (+0x460), invisible (virtual +0xf4 (0)), +0x638 = a1, 0x00488130 (eax = object,
// ecx = Game+0xa78), then for the model (0x004555d0, eax = object): every node of its grid (+0x10: +0x24 * +0x28
// entries at +0x38), each of its meshes (+0x14 count, list at +0x18; mesh = entry +4), each of their parts (+0x80
// count, 0x2c-byte records at +0x84): the part's material (record +4 -> +0x7c + 0x64 * record +0x10, then +8)
// gets flag 0x20 in +0x18 and flag 0x10 set or cleared by `on`.
uint32_t Fx_SetBackground(uint32_t /*ecx*/, void* push, void* fx, uint32_t a1, uint32_t on)
{
    if (!push)
    {
        FxPushError(fx, 0x005b8068);
        return 0;
    }
    Field<uint32_t>(Field<void*>(push, 4), 0x460) |= 0x10000000;
    VCall<void>(Field<void*>(push, 4), 0xf4 / 4, 0u);
    Field<uint32_t>(Field<void*>(push, 4), 0x638) = a1;
    orig::fn_00488130(Field<uint32_t>(push, 4), Field<uint32_t>(g_game, 0xa78));
    void* model = p(orig::fn_004555d0(Field<uint32_t>(push, 4)));
    void* grid = Field<void*>(model, 0x10);
    for (uint32_t i = 0; i < Field<uint32_t>(grid, 0x28) * Field<uint32_t>(grid, 0x24);
         ++i, grid = Field<void*>(model, 0x10))
    {
        void* node = Field<void**>(grid, 0x38)[i];
        for (int32_t j = 0; j < Field<int32_t>(node, 0x14); ++j)
        {
            void* mesh = Field<void*>(reinterpret_cast<void**>(u(node) + 0x18)[j], 4);
            for (int32_t k = 0; k < Field<int32_t>(mesh, 0x80); ++k)
            {
                uint8_t* rec = Field<uint8_t*>(mesh, 0x84) + 0x2c * k;
                uint8_t* m = reinterpret_cast<uint8_t*>(Field<uint32_t>(rec, 0x10) * 0x64 +
                                                         Field<uint32_t>(Field<void*>(rec, 4), 0x7c));
                Field<uint32_t>(Field<void*>(m, 8), 0x18) |= 0x20;
                uint32_t f = Field<uint32_t>(Field<void*>(m, 8), 0x18);
                f = on ? (f | 0x10) : (f & ~0x10u);
                Field<uint32_t>(Field<void*>(m, 8), 0x18) = f;
            }
        }
    }
    return u(model);
}

// 0x00420260 (eax = fxpush; stack: fx; callee pops 4): opcode "orient dir": +0x460 |= 0x40000.
uint32_t Fx_SetOrientDir(void* push, void* fx)
{
    if (!push)
    {
        FxPushError(fx, 0x005b7f80);
        return 0;
    }
    Field<uint32_t>(Field<void*>(push, 4), 0x460) |= 0x40000;
    return Field<uint32_t>(push, 4) + 0x460;
}

uint32_t FxObject_EnsureUvScroll(void* push, uint32_t unused);

// 0x004202b0 (eax = fx, ecx = layer 1..4, edx = fxpush; stack: from (vec2), to (vec2), duration, mode; callee
// pops 0x10): opcode "uv scale": ensures the uv block (0x00420520), flag 0x100000 << (layer - 1) (+0x460),
// 0x00453540 (eax = object, cl = 1); layer record (0x28 bytes at uv +0x84): +0 mode, +4 from, +0xc to, +0x14
// duration. Mode 0: current scale +0x20/+0x24 = from + rand() * (to - from) * [0x005bcbc0] per axis; mode 2:
// +0x18/+0x1c = sqrt(to - from).
uint32_t Fx_SetUvScale(void* fx, uint32_t layer, void* push, const uint32_t* from, const uint32_t* to,
                       uint32_t duration, uint32_t mode)
{
    if (!push)
    {
        FxPushError(fx, 0x005b7f54);
        return 0;
    }
    const uint32_t l = layer - 1;
    FxObject_EnsureUvScroll(push, u(fx));
    Field<uint32_t>(Field<void*>(push, 4), 0x460) |= 0x100000u << (l & 31);
    orig::fn_00453540(Field<uint32_t>(push, 4), 1);
    const uint32_t r = l * 0x28;
    auto uv = [&]() { return Field<uint8_t*>(Field<void*>(push, 4), 0x514) + r; };
    Field<uint32_t>(uv(), 0x84) = mode;
    Field<uint32_t>(uv(), 0x88) = from[0];
    Field<uint32_t>(uv(), 0x8c) = from[1];
    Field<uint32_t>(uv(), 0x90) = to[0];
    Field<uint32_t>(uv(), 0x94) = to[1];
    Field<uint32_t>(uv(), 0x98) = duration;
    auto rand_ = GameFunction<int(__cdecl*)()>(0x005391cd);
    if (mode == 0)
    {
        float hi = u2f(to[0]), lo = u2f(from[0]);
        int32_t n = rand_();
        Field<float>(uv(), 0xa4) = static_cast<float>(n) * (hi - lo) * GameGlobal<float>(0x005bcbc0) + lo;
        hi = u2f(to[1]), lo = u2f(from[1]);
        n = rand_();
        Field<float>(uv(), 0xa8) = static_cast<float>(n) * (hi - lo) * GameGlobal<float>(0x005bcbc0) + lo;
    }
    else if (mode == 2)
    {
        Field<float>(uv(), 0x9c) = sqrtf(u2f(to[0]) - u2f(from[0]));
        Field<float>(uv(), 0xa0) = sqrtf(u2f(to[1]) - u2f(from[1]));
    }
    return 0;
}

// 0x00420450 (eax = fx, ecx = layer 1..4, edx = fxpush; stack: a0, a1, a2, a3; callee pops 0x10): another uv
// layer opcode (its message also reads "Failed to set uv scroll"): flag 0x100000 << (layer - 1), 0x00453540, and
// the layer's 0x14-byte record at uv +0x124: +0 a0, +4 a2, +0xc a1, and +8 = a3 when a1 > 0.
uint32_t fn_00420450(void* fx, uint32_t layer, void* push, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    if (!push)
    {
        FxPushError(fx, 0x005b7f28);
        return 0;
    }
    const uint32_t l = layer - 1;
    FxObject_EnsureUvScroll(push, u(fx));
    Field<uint32_t>(Field<void*>(push, 4), 0x460) |= 0x100000u << (l & 31);
    orig::fn_00453540(Field<uint32_t>(push, 4), 1);
    const uint32_t r = l * 0x14;
    Field<uint32_t>(Field<uint8_t*>(Field<void*>(push, 4), 0x514) + r, 0x128) = a2;
    Field<uint32_t>(Field<uint8_t*>(Field<void*>(push, 4), 0x514) + r, 0x130) = a1;
    Field<uint32_t>(Field<uint8_t*>(Field<void*>(push, 4), 0x514) + r, 0x124) = a0;
    if (u2f(a1) > Zero())
        Field<uint32_t>(Field<uint8_t*>(Field<void*>(push, 4), 0x514) + r, 0x12c) = a3;
    return 0;
}

// 0x00420520 (esi = fxpush; stack: not read, callee pops 4): gives the object its 0x174-byte uv block at +0x514
// ("fx_uvscroll", zeroed) when it has none, with the scale fields of the four 0x28-byte layer records (uv +0x84)
// set to 1: +0x90/+0x94 and +0xa4/+0xa8 of each.
uint32_t FxObject_EnsureUvScroll(void* push, uint32_t /*unused*/)
{
    if (!push || Field<uint32_t>(Field<void*>(push, 4), 0x514))
        return 0;
    Field<uint32_t>(Field<void*>(push, 4), 0x514) = orig::fn_004e07f0(kHeap, 0x12, 0x174, 2, 0x005b7f1c);
    std::memset(Field<void*>(Field<void*>(push, 4), 0x514), 0, 0x174);
    static const uint16_t ones[] = {0xa4, 0xa8, 0x90, 0x94, 0xcc, 0xd0, 0xb8, 0xbc,
                                    0xf4, 0xf8, 0xe0, 0xe4, 0x11c, 0x120, 0x108, 0x10c};
    for (uint16_t off : ones)
        Field<uint32_t>(Field<void*>(Field<void*>(push, 4), 0x514), off) = 0x3f800000u;
    return 0x3f800000u;
}

// 0x00420670 (eax = fx, ecx = layer 1..4, edx = fxpush; stack: angle (degrees), speed, duration, ramp in,
// ramp out; callee pops 0x14): opcode 0x40 "set uv scroll" (weather.md): ensures the uv block, flag 0x100000 <<
// (layer - 1), 0x00453540; layer record (0x20 bytes at uv + 0x20 * (layer - 1)): +0xc speed, +0x18 duration,
// +0x10 ramp in, +0x14 ramp out (only when duration > 0); direction +4 / +8 from the sine table: index =
// (int)(angle * [0x005bcba4] + [0x005bce68]) & 0x3fff, +4 = [0x00614b08 + 4i], +8 = ([0x00614b00])[i].
uint32_t Fx_SetUvScroll(void* fx, uint32_t layer, void* push, uint32_t angle, uint32_t speed, uint32_t duration,
                        uint32_t rampIn, uint32_t rampOut)
{
    if (!push)
    {
        FxPushError(fx, 0x005b7f28);
        return 0;
    }
    const uint32_t l = layer - 1;
    FxObject_EnsureUvScroll(push, u(fx));
    Field<uint32_t>(Field<void*>(push, 4), 0x460) |= 0x100000u << (l & 31);
    orig::fn_00453540(Field<uint32_t>(push, 4), 1);
    const uint32_t r = l << 5;
    auto uv = [&]() { return Field<uint8_t*>(Field<void*>(push, 4), 0x514) + r; };
    Field<uint32_t>(uv(), 0x10) = rampIn;
    Field<uint32_t>(uv(), 0x18) = duration;
    Field<uint32_t>(uv(), 0xc) = speed;
    if (u2f(duration) > Zero())
        Field<uint32_t>(uv(), 0x14) = rampOut;
    const float a = u2f(angle) * GameGlobal<float>(0x005bcba4) + GameGlobal<float>(0x005bce68);
    const uint32_t i = static_cast<uint32_t>(static_cast<int64_t>(a)) & 0x3fff;
    const uint32_t s = GameGlobal<uint32_t*>(0x00614b00)[i];
    const uint32_t c = reinterpret_cast<const uint32_t*>(0x00614b08)[i];
    Field<uint32_t>(uv(), 4) = c;
    Field<uint32_t>(uv(), 8) = s;
    return 0;
}

// ---------------------------------------------------------------------------------------------------------------
// ABI adapters (register inputs in the order eax ecx edx ebx esi edi, then stack arguments), entries and hooks.
#define R2A_P(x) reinterpret_cast<void*>(x)
#define R2A_F(x) reinterpret_cast<float*>(x)
#define R2A_U(x) reinterpret_cast<const uint32_t*>(x)
#define R2A_S(x) reinterpret_cast<const char*>(x)
#define R2A_HOOK(addr, name, abi) \
    ABI_ENTRY_##addr(name##_Entry, abi) HOOK(0x##addr, #name, name##_Entry, nullptr);

static uint32_t fn_0041bd30_Abi(uint32_t eax) { return fn_0041bd30(R2A_P(eax)); }
R2A_HOOK(0041bd30, fn_0041bd30, fn_0041bd30_Abi)
static uint32_t FxOwner_Toggle_Abi(uint32_t esi) { return FxOwner_Toggle(R2A_P(esi)); }
R2A_HOOK(0041bf60, FxOwner_Toggle, FxOwner_Toggle_Abi)
// 0x0041c050 FxOwner_OnMessage is not hooked: no recorded run executes it and it has no direct callers (vtable
// method), so the comparison cannot verify it.
static uint32_t FxOwner_Update_Abi(uint32_t ecx) { return FxOwner_Update(R2A_P(ecx)); }
R2A_HOOK(0041c110, FxOwner_Update, FxOwner_Update_Abi)
static uint32_t FxSystem_Construct_Abi(uint32_t edi) { return FxSystem_Construct(R2A_P(edi)); }
R2A_HOOK(0041c700, FxSystem_Construct, FxSystem_Construct_Abi)
static uint32_t FxSystem_Destruct_Abi(uint32_t esi) { return FxSystem_Destruct(R2A_P(esi)); }
R2A_HOOK(0041c760, FxSystem_Destruct, FxSystem_Destruct_Abi)
static uint32_t FxBank_PointerToOffset_Abi(uint32_t eax, uint32_t ecx) { return FxBank_PointerToOffset(eax, R2A_P(ecx)); }
R2A_HOOK(0041c780, FxBank_PointerToOffset, FxBank_PointerToOffset_Abi)
static uint32_t FxBank_OffsetToPointer_Abi(uint32_t ecx, uint32_t a0) { return FxBank_OffsetToPointer(ecx, R2A_P(a0)); }
R2A_HOOK(0041c790, FxBank_OffsetToPointer, FxBank_OffsetToPointer_Abi)
static uint32_t FxSystem_ResetPools_Abi(uint32_t eax) { return FxSystem_ResetPools(R2A_P(eax)); }
R2A_HOOK(0041c9a0, FxSystem_ResetPools, FxSystem_ResetPools_Abi)
static uint32_t FxSystem_Unload_Abi(uint32_t a0) { return FxSystem_Unload(R2A_P(a0)); }
R2A_HOOK(0041c9d0, FxSystem_Unload, FxSystem_Unload_Abi)
static uint32_t FxSystem_KillAll_Abi(uint32_t a0) { return FxSystem_KillAll(R2A_P(a0)); }
R2A_HOOK(0041cab0, FxSystem_KillAll, FxSystem_KillAll_Abi)
static uint32_t FxBank_Find_Abi(uint32_t eax, uint32_t ecx) { return FxBank_Find(R2A_P(eax), R2A_S(ecx)); }
R2A_HOOK(0041cb60, FxBank_Find, FxBank_Find_Abi)
static uint32_t FxNameList_PickRandom_Abi(uint32_t eax, uint32_t /*ecx*/, uint32_t /*a0*/) { return FxNameList_PickRandom(R2A_U(eax)); }
R2A_HOOK(0041cba0, FxNameList_PickRandom, FxNameList_PickRandom_Abi)
static uint32_t FxInstance_Alloc_Abi(uint32_t eax) { return FxInstance_Alloc(R2A_P(eax)); }
R2A_HOOK(0041cc20, FxInstance_Alloc, FxInstance_Alloc_Abi)
static uint32_t Fx_Create_Abi(uint32_t eax, uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return Fx_Create((eax & 0xff) != 0, R2A_P(ecx), R2A_S(a0), reinterpret_cast<uint32_t*>(a1));
}
R2A_HOOK(0041cc60, Fx_Create, Fx_Create_Abi)
static uint32_t FxInstance_DestroyEmitters_Abi(uint32_t ebx, uint32_t a0) { return FxInstance_DestroyEmitters(R2A_P(ebx), a0); }
R2A_HOOK(0041cfb0, FxInstance_DestroyEmitters, FxInstance_DestroyEmitters_Abi)
static uint32_t Fx_Release_Abi(uint32_t a0, uint32_t a1) { return Fx_Release(R2A_P(a0), reinterpret_cast<void**>(a1)); }
R2A_HOOK(0041d030, Fx_Release, Fx_Release_Abi)
static uint32_t Fx_SetLifespan_Abi(uint32_t ecx, uint32_t a0, uint32_t a1) { return Fx_SetLifespan(R2A_P(ecx), R2A_P(a0), a1); }
R2A_HOOK(0041d110, Fx_SetLifespan, Fx_SetLifespan_Abi)
static uint32_t Fx_SetScaleLinear_Abi(uint32_t esi, uint32_t a0, uint32_t a1, uint32_t a2) { return Fx_SetScaleLinear(R2A_P(esi), R2A_P(a0), a1, a2); }
R2A_HOOK(0041d170, Fx_SetScaleLinear, Fx_SetScaleLinear_Abi)
static uint32_t Fx_SetScaleExp_Abi(uint32_t esi, uint32_t a0, uint32_t a1, uint32_t a2) { return Fx_SetScaleExp(R2A_P(esi), R2A_P(a0), a1, a2); }
R2A_HOOK(0041d210, Fx_SetScaleExp, Fx_SetScaleExp_Abi)
static uint32_t Fx_SetScaleLog_Abi(uint32_t esi, uint32_t a0, uint32_t a1, uint32_t a2) { return Fx_SetScaleLog(R2A_P(esi), R2A_P(a0), a1, a2); }
R2A_HOOK(0041d2c0, Fx_SetScaleLog, Fx_SetScaleLog_Abi)
static uint32_t Fx_SetLightScaleLinear_Abi(uint32_t eax, uint32_t ebx, uint32_t edi, uint32_t a0) { return Fx_SetLightScaleLinear(R2A_P(eax), ebx, R2A_F(edi), R2A_P(a0)); }
R2A_HOOK(0041d360, Fx_SetLightScaleLinear, Fx_SetLightScaleLinear_Abi)
static uint32_t Fx_SetLightScaleExp_Abi(uint32_t eax, uint32_t ebx, uint32_t edi, uint32_t a0) { return Fx_SetLightScaleExp(R2A_P(eax), ebx, R2A_F(edi), R2A_P(a0)); }
R2A_HOOK(0041d440, Fx_SetLightScaleExp, Fx_SetLightScaleExp_Abi)
static uint32_t Fx_SetLightScaleLog_Abi(uint32_t eax, uint32_t ebx, uint32_t edi, uint32_t a0) { return Fx_SetLightScaleLog(R2A_P(eax), ebx, R2A_F(edi), R2A_P(a0)); }
R2A_HOOK(0041d560, Fx_SetLightScaleLog, Fx_SetLightScaleLog_Abi)
static uint32_t Fx_SetFogLinear_Abi(uint32_t eax, uint32_t edi, uint32_t a0, uint32_t a1, uint32_t a2) { return Fx_SetFogLinear(R2A_P(eax), R2A_F(edi), R2A_P(a0), a1, a2); }
R2A_HOOK(0041d640, Fx_SetFogLinear, Fx_SetFogLinear_Abi)
static uint32_t Fx_SetFogExp_Abi(uint32_t eax, uint32_t edi, uint32_t a0, uint32_t a1, uint32_t a2) { return Fx_SetFogExp(R2A_P(eax), R2A_F(edi), R2A_P(a0), a1, a2); }
R2A_HOOK(0041d700, Fx_SetFogExp, Fx_SetFogExp_Abi)
static uint32_t Fx_SetFogLog_Abi(uint32_t eax, uint32_t edi, uint32_t a0, uint32_t a1, uint32_t a2) { return Fx_SetFogLog(R2A_P(eax), R2A_F(edi), R2A_P(a0), a1, a2); }
R2A_HOOK(0041d830, Fx_SetFogLog, Fx_SetFogLog_Abi)
static uint32_t Fx_SetLightSpecular_Abi(uint32_t eax, uint32_t ecx, uint32_t edx) { return Fx_SetLightSpecular(R2A_F(eax), R2A_P(ecx), R2A_P(edx)); }
R2A_HOOK(0041d8f0, Fx_SetLightSpecular, Fx_SetLightSpecular_Abi)
static uint32_t Fx_SetLightAmbient_Abi(uint32_t eax, uint32_t ecx, uint32_t edx) { return Fx_SetLightAmbient(R2A_F(eax), R2A_P(ecx), R2A_P(edx)); }
R2A_HOOK(0041d980, Fx_SetLightAmbient, Fx_SetLightAmbient_Abi)
static uint32_t Fx_SetLightHalfRange_Abi(uint32_t eax, uint32_t edx, uint32_t a0) { return Fx_SetLightHalfRange(R2A_P(eax), R2A_P(edx), a0); }
R2A_HOOK(0041da10, Fx_SetLightHalfRange, Fx_SetLightHalfRange_Abi)
static uint32_t Fx_SetLight_Abi(uint32_t eax, uint32_t edx, uint32_t ebx, uint32_t a0) { return Fx_SetLight(R2A_P(eax), R2A_F(edx), R2A_P(ebx), R2A_U(a0)); }
R2A_HOOK(0041da90, Fx_SetLight, Fx_SetLight_Abi)
static uint32_t Fx_SetFadeIn_Abi(uint32_t eax, uint32_t a0, uint32_t a1, uint32_t a2) { return Fx_SetFadeIn(R2A_P(eax), R2A_P(a0), a1, a2); }
R2A_HOOK(0041dba0, Fx_SetFadeIn, Fx_SetFadeIn_Abi)
static uint32_t Fx_SetFadeOut_Abi(uint32_t eax, uint32_t a0, uint32_t a1) { return Fx_SetFadeOut(R2A_P(eax), R2A_P(a0), a1); }
R2A_HOOK(0041dc30, Fx_SetFadeOut, Fx_SetFadeOut_Abi)
static uint32_t Fx_StartOnObject_Abi(uint32_t eax, uint32_t ebx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
    return Fx_StartOnObject(R2A_U(eax), R2A_P(ebx), R2A_P(a0), a1, R2A_U(a2), R2A_U(a3), a4, a5);
}
R2A_HOOK(0041dc80, Fx_StartOnObject, Fx_StartOnObject_Abi)
static uint32_t Fx_Attach_Abi(uint32_t eax, uint32_t esi, uint32_t edi, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return Fx_Attach(R2A_S(eax), R2A_P(esi), R2A_P(edi), a0, a1, a2, a3);
}
R2A_HOOK(0041dd50, Fx_Attach, Fx_Attach_Abi)
static uint32_t Fx_SetFaceDirection_Abi(uint32_t eax, uint32_t a0) { return Fx_SetFaceDirection(R2A_P(eax), R2A_P(a0)); }
R2A_HOOK(0041ddc0, Fx_SetFaceDirection, Fx_SetFaceDirection_Abi)
static uint32_t Fx_SetGravity_Abi(uint32_t ebx, uint32_t a0, uint32_t a1) { return Fx_SetGravity(R2A_P(ebx), R2A_P(a0), a1); }
R2A_HOOK(0041de10, Fx_SetGravity, Fx_SetGravity_Abi)
static uint32_t Fx_Pause_Abi(uint32_t eax, uint32_t ecx, uint32_t a0, uint32_t a1) { return Fx_Pause(R2A_P(eax), R2A_P(ecx), R2A_P(a0), a1); }
R2A_HOOK(0041dea0, Fx_Pause, Fx_Pause_Abi)
static uint32_t Fx_Wait_Abi(uint32_t eax, uint32_t a0, uint32_t a1) { return Fx_Wait(R2A_P(eax), R2A_P(a0), R2A_P(a1)); }
R2A_HOOK(0041dee0, Fx_Wait, Fx_Wait_Abi)
static uint32_t Fx_SetLogObject_Abi(uint32_t eax, uint32_t ebx, uint32_t a0) { return Fx_SetLogObject(R2A_P(eax), R2A_P(ebx), R2A_P(a0)); }
R2A_HOOK(0041df20, Fx_SetLogObject, Fx_SetLogObject_Abi)
static uint32_t Fx_SetDrift_Abi(uint32_t eax, uint32_t edi, uint32_t a0, uint32_t a1) { return Fx_SetDrift(R2A_P(eax), R2A_P(edi), R2A_P(a0), a1); }
R2A_HOOK(0041dfb0, Fx_SetDrift, Fx_SetDrift_Abi)
static uint32_t Fx_SetImpulse_Abi(uint32_t eax, uint32_t edi, uint32_t a0, uint32_t a1) { return Fx_SetImpulse(R2A_P(eax), R2A_P(edi), R2A_P(a0), a1); }
R2A_HOOK(0041e0c0, Fx_SetImpulse, Fx_SetImpulse_Abi)
static uint32_t Fx_SetAbsoluteImpulse_Abi(uint32_t esi, uint32_t a0, uint32_t a1) { return Fx_SetAbsoluteImpulse(R2A_P(esi), R2A_P(a0), a1); }
R2A_HOOK(0041e1e0, Fx_SetAbsoluteImpulse, Fx_SetAbsoluteImpulse_Abi)
static uint32_t Fx_SetInert_Abi(uint32_t eax, uint32_t a0) { return Fx_SetInert(R2A_P(eax), R2A_P(a0)); }
R2A_HOOK(0041e230, Fx_SetInert, Fx_SetInert_Abi)
static uint32_t Fx_SetInvisible_Abi(uint32_t eax, uint32_t a0) { return Fx_SetInvisible(R2A_P(eax), R2A_P(a0)); }
R2A_HOOK(0041e280, Fx_SetInvisible, Fx_SetInvisible_Abi)
static uint32_t FxObject_EnsureTexAnim_Abi(uint32_t esi, uint32_t a0) { return FxObject_EnsureTexAnim(R2A_P(esi), a0); }
R2A_HOOK(0041e2c0, FxObject_EnsureTexAnim, FxObject_EnsureTexAnim_Abi)
static uint32_t Fx_SetTextureAnimation_Abi(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return Fx_SetTextureAnimation(R2A_P(eax), ecx, R2A_P(edx), a0, a1, a2, a3);
}
R2A_HOOK(0041e310, Fx_SetTextureAnimation, Fx_SetTextureAnimation_Abi)
static uint32_t Fx_StopEmitter_Abi(uint32_t eax, uint32_t esi, uint32_t a0) { return Fx_StopEmitter(R2A_P(eax), R2A_P(esi), a0); }
R2A_HOOK(0041e3b0, Fx_StopEmitter, Fx_StopEmitter_Abi)
static uint32_t FxObject_AllocEmitter_Abi(uint32_t eax, uint32_t a0) { return FxObject_AllocEmitter(R2A_P(eax), R2A_P(a0)); }
R2A_HOOK(0041e420, FxObject_AllocEmitter, FxObject_AllocEmitter_Abi)
static uint32_t Fx_Emit_Abi(uint32_t ebx, uint32_t edi, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
    return Fx_Emit(R2A_P(ebx), R2A_P(edi), a0, R2A_S(a1), a2, a3, a4, a5);
}
R2A_HOOK(0041e4d0, Fx_Emit, Fx_Emit_Abi)
static uint32_t Fx_EmitRandom_Abi(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t esi, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
    return Fx_EmitRandom(eax, ecx, R2A_P(edx), R2A_U(esi), R2A_P(a0), a1, a2, a3, R2A_U(a4), a5);
}
R2A_HOOK(0041e610, Fx_EmitRandom, Fx_EmitRandom_Abi)
static uint32_t FxObject_AllocParticleEmitter_Abi(uint32_t eax, uint32_t a0) { return FxObject_AllocParticleEmitter(R2A_P(eax), a0); }
R2A_HOOK(0041e6b0, FxObject_AllocParticleEmitter, FxObject_AllocParticleEmitter_Abi)
static uint32_t FxObject_CurrentParticleEmitter_Abi(uint32_t eax, uint32_t a0) { return FxObject_CurrentParticleEmitter(R2A_P(eax), a0); }
R2A_HOOK(0041e730, FxObject_CurrentParticleEmitter, FxObject_CurrentParticleEmitter_Abi)
static uint32_t Fx_EmitParticle_Abi(uint32_t eax, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7, uint32_t a8)
{
    return Fx_EmitParticle(R2A_U(eax), R2A_P(a0), R2A_P(a1), R2A_P(a2), a3, a4, a5, a6, R2A_U(a7), R2A_U(a8));
}
R2A_HOOK(0041e760, Fx_EmitParticle, Fx_EmitParticle_Abi)
static uint32_t Fx_StartDirParticle_Abi(uint32_t ebx, uint32_t esi) { return Fx_StartDirParticle(R2A_P(ebx), R2A_P(esi)); }
R2A_HOOK(0041e990, Fx_StartDirParticle, Fx_StartDirParticle_Abi)
static uint32_t Fx_SetParticleBlur_Abi(uint32_t eax, uint32_t esi, uint32_t a0, uint32_t a1) { return Fx_SetParticleBlur(R2A_P(eax), R2A_P(esi), a0, a1); }
R2A_HOOK(0041e9f0, Fx_SetParticleBlur, Fx_SetParticleBlur_Abi)
static uint32_t Fx_SetParticleSize_Abi(uint32_t eax, uint32_t esi, uint32_t a0, uint32_t a1) { return Fx_SetParticleSize(R2A_P(eax), R2A_P(esi), a0, a1); }
R2A_HOOK(0041ea40, Fx_SetParticleSize, Fx_SetParticleSize_Abi)
static uint32_t Fx_SetParticleScale_Abi(uint32_t eax, uint32_t esi, uint32_t a0, uint32_t a1) { return Fx_SetParticleScale(R2A_P(eax), R2A_P(esi), a0, a1); }
R2A_HOOK(0041ea90, Fx_SetParticleScale, Fx_SetParticleScale_Abi)
static uint32_t Fx_SetParticleLifespan_Abi(uint32_t eax, uint32_t esi, uint32_t a0, uint32_t a1) { return Fx_SetParticleLifespan(R2A_P(eax), R2A_P(esi), a0, a1); }
R2A_HOOK(0041eae0, Fx_SetParticleLifespan, Fx_SetParticleLifespan_Abi)
static uint32_t ParticleColourTable_Alloc_Abi(uint32_t a0) { return ParticleColourTable_Alloc(a0); }
R2A_HOOK(0041eb30, ParticleColourTable_Alloc, ParticleColourTable_Alloc_Abi)
static uint32_t Fx_SetParticleFadeInOut_Abi(uint32_t eax, uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2) { return Fx_SetParticleFadeInOut(R2A_P(eax), R2A_P(ecx), a0, a1, a2); }
R2A_HOOK(0041eb70, Fx_SetParticleFadeInOut, Fx_SetParticleFadeInOut_Abi)
static uint32_t Fx_SetParticleSpeed_Abi(uint32_t eax, uint32_t esi, uint32_t a0, uint32_t a1) { return Fx_SetParticleSpeed(R2A_P(eax), R2A_P(esi), a0, a1); }
R2A_HOOK(0041ee50, Fx_SetParticleSpeed, Fx_SetParticleSpeed_Abi)
static uint32_t Fx_SetParticleGravity_Abi(uint32_t eax, uint32_t esi, uint32_t a0, uint32_t a1) { return Fx_SetParticleGravity(R2A_P(eax), R2A_P(esi), a0, a1); }
R2A_HOOK(0041eea0, Fx_SetParticleGravity, Fx_SetParticleGravity_Abi)
static uint32_t Fx_SetParticleAxis_Abi(uint32_t eax, uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2) { return Fx_SetParticleAxis(R2A_P(eax), R2A_P(ecx), a0, a1, a2); }
R2A_HOOK(0041eef0, Fx_SetParticleAxis, Fx_SetParticleAxis_Abi)
static uint32_t Fx_SetParticleAxisRelative_Abi(uint32_t eax, uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2) { return Fx_SetParticleAxisRelative(R2A_P(eax), R2A_P(ecx), a0, a1, a2); }
R2A_HOOK(0041f0f0, Fx_SetParticleAxisRelative, Fx_SetParticleAxisRelative_Abi)
static uint32_t Fx_SetParticleConeAngle_Abi(uint32_t eax, uint32_t esi, uint32_t a0, uint32_t a1) { return Fx_SetParticleConeAngle(R2A_P(eax), R2A_P(esi), a0, a1); }
R2A_HOOK(0041f2f0, Fx_SetParticleConeAngle, Fx_SetParticleConeAngle_Abi)
static uint32_t Fx_SetParticleDamping_Abi(uint32_t eax, uint32_t esi, uint32_t a0, uint32_t a1) { return Fx_SetParticleDamping(R2A_P(eax), R2A_P(esi), a0, a1); }
R2A_HOOK(0041f340, Fx_SetParticleDamping, Fx_SetParticleDamping_Abi)
static uint32_t Fx_SetParticleUvs_Abi(uint32_t eax, uint32_t esi, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3) { return Fx_SetParticleUvs(R2A_P(eax), R2A_P(esi), a0, a1, a2, a3); }
R2A_HOOK(0041f3b0, Fx_SetParticleUvs, Fx_SetParticleUvs_Abi)
static uint32_t Fx_SetParticleColour_Abi(uint32_t eax, uint32_t esi, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3) { return Fx_SetParticleColour(R2A_P(eax), R2A_P(esi), a0, a1, a2, a3); }
R2A_HOOK(0041f410, Fx_SetParticleColour, Fx_SetParticleColour_Abi)
static uint32_t FxSystem_SpawnInPool_Abi(uint32_t ecx, uint32_t ebx, uint32_t edi, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7)
{
    return FxSystem_SpawnInPool(ecx, ebx, edi, R2A_P(a0), a1, a2, a3, a4, a5, a6, a7);
}
R2A_HOOK(0041f470, FxSystem_SpawnInPool, FxSystem_SpawnInPool_Abi)
static uint32_t FxSystem_SpawnInPool2_Abi(uint32_t ecx, uint32_t ebx, uint32_t esi, uint32_t edi, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6)
{
    return FxSystem_SpawnInPool2(ecx, ebx, esi, edi, R2A_P(a0), a1, a2, a3, a4, a5, a6);
}
R2A_HOOK(0041f4b0, FxSystem_SpawnInPool2, FxSystem_SpawnInPool2_Abi)
static uint32_t FxObject_AddStreamer_Abi(uint32_t ecx, uint32_t a0) { return FxObject_AddStreamer(R2A_P(ecx), a0); }
R2A_HOOK(0041f500, FxObject_AddStreamer, FxObject_AddStreamer_Abi)
static uint32_t FxObject_CurrentStreamer_Abi(uint32_t eax, uint32_t a0) { return FxObject_CurrentStreamer(R2A_P(eax), a0); }
R2A_HOOK(0041f560, FxObject_CurrentStreamer, FxObject_CurrentStreamer_Abi)
static uint32_t Fx_SetStreamerTexture_Abi(uint32_t eax, uint32_t ebx, uint32_t a0) { return Fx_SetStreamerTexture(R2A_P(eax), R2A_S(ebx), R2A_P(a0)); }
R2A_HOOK(0041f5a0, Fx_SetStreamerTexture, Fx_SetStreamerTexture_Abi)
static uint32_t Fx_SetStreamerUvs_Abi(uint32_t eax, uint32_t esi, uint32_t edi) { return Fx_SetStreamerUvs(R2A_P(eax), R2A_U(esi), R2A_P(edi)); }
R2A_HOOK(0041f640, Fx_SetStreamerUvs, Fx_SetStreamerUvs_Abi)
static uint32_t Fx_SetStreamerTransparency_Abi(uint32_t eax, uint32_t esi, uint32_t a0) { return Fx_SetStreamerTransparency(R2A_P(eax), R2A_P(esi), a0); }
R2A_HOOK(0041f690, Fx_SetStreamerTransparency, Fx_SetStreamerTransparency_Abi)
static uint32_t Fx_SetStreamerThickness_Abi(uint32_t eax, uint32_t esi, uint32_t a0) { return Fx_SetStreamerThickness(R2A_P(eax), R2A_P(esi), a0); }
R2A_HOOK(0041f6d0, Fx_SetStreamerThickness, Fx_SetStreamerThickness_Abi)
static uint32_t Fx_SetStreamerAlpha_Abi(uint32_t eax, uint32_t ecx, uint32_t edi) { return Fx_SetStreamerAlpha(R2A_P(eax), R2A_P(ecx), edi); }
R2A_HOOK(0041f710, Fx_SetStreamerAlpha, Fx_SetStreamerAlpha_Abi)
static uint32_t Fx_SetStreamerRed_Abi(uint32_t eax, uint32_t ecx, uint32_t edi) { return Fx_SetStreamerRed(R2A_P(eax), R2A_P(ecx), edi); }
R2A_HOOK(0041f750, Fx_SetStreamerRed, Fx_SetStreamerRed_Abi)
static uint32_t Fx_SetStreamerGreen_Abi(uint32_t eax, uint32_t ecx, uint32_t edi) { return Fx_SetStreamerGreen(R2A_P(eax), R2A_P(ecx), edi); }
R2A_HOOK(0041f790, Fx_SetStreamerGreen, Fx_SetStreamerGreen_Abi)
static uint32_t Fx_SetStreamerBlue_Abi(uint32_t eax, uint32_t ecx, uint32_t edi) { return Fx_SetStreamerBlue(R2A_P(eax), R2A_P(ecx), edi); }
R2A_HOOK(0041f7d0, Fx_SetStreamerBlue, Fx_SetStreamerBlue_Abi)
static uint32_t Fx_SetStreamerLength_Abi(uint32_t eax, uint32_t esi, uint32_t a0) { return Fx_SetStreamerLength(R2A_P(eax), R2A_P(esi), a0); }
R2A_HOOK(0041f810, Fx_SetStreamerLength, Fx_SetStreamerLength_Abi)
static uint32_t Fx_SetInitialRotation_Abi(uint32_t esi, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3) { return Fx_SetInitialRotation(R2A_P(esi), R2A_P(a0), a1, a2, a3); }
R2A_HOOK(0041f850, Fx_SetInitialRotation, Fx_SetInitialRotation_Abi)
static uint32_t Fx_RotateObject_Abi(uint32_t esi, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3) { return Fx_RotateObject(R2A_P(esi), R2A_P(a0), a1, a2, a3); }
R2A_HOOK(0041f8f0, Fx_RotateObject, Fx_RotateObject_Abi)
static uint32_t Fx_SetHeightOffset_Abi(uint32_t esi, uint32_t a0, uint32_t a1) { return Fx_SetHeightOffset(R2A_P(esi), R2A_P(a0), a1); }
R2A_HOOK(0041f970, Fx_SetHeightOffset, Fx_SetHeightOffset_Abi)
static uint32_t Fx_SetHitGround_Abi(uint32_t esi, uint32_t a0) { return Fx_SetHitGround(R2A_P(esi), R2A_P(a0)); }
R2A_HOOK(0041f9d0, Fx_SetHitGround, Fx_SetHitGround_Abi)
static uint32_t fn_0041fa60_Abi(uint32_t esi, uint32_t a0) { return fn_0041fa60(R2A_P(esi), R2A_P(a0)); }
R2A_HOOK(0041fa60, fn_0041fa60, fn_0041fa60_Abi)
static uint32_t fn_0041faf0_Abi(uint32_t eax, uint32_t esi) { return fn_0041faf0(R2A_P(eax), R2A_P(esi)); }
R2A_HOOK(0041faf0, fn_0041faf0, fn_0041faf0_Abi)
static uint32_t Fx_SetOnHitAoe_Abi(uint32_t eax, uint32_t edi, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3) { return Fx_SetOnHitAoe(R2A_P(eax), R2A_P(edi), a0, a1, a2, a3); }
R2A_HOOK(0041fb00, Fx_SetOnHitAoe, Fx_SetOnHitAoe_Abi)
static uint32_t Fx_SetOnHitDeath_Abi(uint32_t eax, uint32_t edi) { return Fx_SetOnHitDeath(R2A_P(eax), R2A_P(edi)); }
R2A_HOOK(0041fbd0, Fx_SetOnHitDeath, Fx_SetOnHitDeath_Abi)
static uint32_t Fx_SetOnHitDelete_Abi(uint32_t eax, uint32_t edi) { return Fx_SetOnHitDelete(R2A_P(eax), R2A_P(edi)); }
R2A_HOOK(0041fc20, Fx_SetOnHitDelete, Fx_SetOnHitDelete_Abi)
static uint32_t Fx_SetOnHitCreate_Abi(uint32_t eax, uint32_t ebx, uint32_t edi, uint32_t a0, uint32_t a1) { return Fx_SetOnHitCreate(R2A_P(eax), R2A_S(ebx), R2A_P(edi), a0, a1); }
R2A_HOOK(0041fc70, Fx_SetOnHitCreate, Fx_SetOnHitCreate_Abi)
static uint32_t Fx_SetOnDeath_Abi(uint32_t eax, uint32_t esi, uint32_t edi) { return Fx_SetOnDeath(R2A_P(eax), R2A_P(esi), R2A_S(edi)); }
R2A_HOOK(0041fd10, Fx_SetOnDeath, Fx_SetOnDeath_Abi)
static uint32_t Fx_SetKite_Abi(uint32_t eax, uint32_t ecx, uint32_t a0) { return Fx_SetKite(R2A_P(eax), ecx, R2A_P(a0)); }
R2A_HOOK(0041fd70, Fx_SetKite, Fx_SetKite_Abi)
static uint32_t Fx_SetShaderFlag_Abi(uint32_t eax, uint32_t ecx, uint32_t a0) { return Fx_SetShaderFlag(R2A_P(eax), ecx, R2A_P(a0)); }
R2A_HOOK(0041fdb0, Fx_SetShaderFlag, Fx_SetShaderFlag_Abi)
static uint32_t Fx_SetBackground_Abi(uint32_t eax, uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2) { return Fx_SetBackground(ecx, R2A_P(eax), R2A_P(a0), a1, a2); }
R2A_HOOK(0041fe00, Fx_SetBackground, Fx_SetBackground_Abi)
static uint32_t Fx_SetOrientDir_Abi(uint32_t eax, uint32_t a0) { return Fx_SetOrientDir(R2A_P(eax), R2A_P(a0)); }
R2A_HOOK(00420260, Fx_SetOrientDir, Fx_SetOrientDir_Abi)
static uint32_t Fx_SetUvScale_Abi(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return Fx_SetUvScale(R2A_P(eax), ecx, R2A_P(edx), R2A_U(a0), R2A_U(a1), a2, a3);
}
R2A_HOOK(004202b0, Fx_SetUvScale, Fx_SetUvScale_Abi)
static uint32_t fn_00420450_Abi(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_00420450(R2A_P(eax), ecx, R2A_P(edx), a0, a1, a2, a3);
}
R2A_HOOK(00420450, fn_00420450, fn_00420450_Abi)
static uint32_t FxObject_EnsureUvScroll_Abi(uint32_t esi, uint32_t a0) { return FxObject_EnsureUvScroll(R2A_P(esi), a0); }
R2A_HOOK(00420520, FxObject_EnsureUvScroll, FxObject_EnsureUvScroll_Abi)
static uint32_t Fx_SetUvScroll_Abi(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    return Fx_SetUvScroll(R2A_P(eax), ecx, R2A_P(edx), a0, a1, a2, a3, a4);
}
R2A_HOOK(00420670, Fx_SetUvScroll, Fx_SetUvScroll_Abi)
