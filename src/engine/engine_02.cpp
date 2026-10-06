// Recovered: batch engine_02, shared engine utilities (timer, string helpers, the PMS_List containers and their item
// pools, small vector / matrix math). Static recovery 2026-10-05, compile-checked only (the game was not run); the
// oracle (tools/compare.py) is still to do.
//
// Floats follow the disassembly's x87 operation order in single precision (the game runs the FPU at 24 bits).
// Functions that return in st0 have adapters returning float, so the generated entry stubs leave st0 as the
// original does. Callees that return st0 while the ABI table lists eax are called through small asm shims that
// pop st0.
//
// Containers (evidence: vtables in .rdata, constructor stores, the pool name string 0x0058b0e0
// "PMS_List::m_listData" passed by both constructors 0x004e7010 / 0x004e6bd0):
//   PMS_List (vtable 0x0058b098): +4 byte "owns its pool", +8 item pool, +0xc head, +0x10 tail, +0x14 count.
//     node (0x1c bytes from the pool): +0 next, +4 prev, +0xc value, +0x14 byte "removed while locked",
//     +0x18 lock count.
//     slots: 0 0x004e71c0 RemoveNode, 1 0x004a46b0, 2 0x004e7310 Next, 3 0x00433e70, 4 0x004e7060 deleting dtor,
//     5 0x004e70f0 PushFront, 6 0x004e7160 PushBack, 7 0x004e7210 FindNode, 8 0x004e7240 Find, 9 0x004e7270
//     FindString, 10 0x004e72e0 RemoveValue, 11 0x004e72b0 RemoveAll.
//   PMS_Tree (guess name; vtable 0x0058b060, derived from PMS_List): a binary search tree over the same nodes
//     (+0 right / "greater or equal" child, +4 left child, +8 parent), comparator (cdecl, 2 args) at +0x18.
//     slots: 0 0x004e6d80 RemoveNode, 1 0x004e6c70 First, 2 0x004e6c80 Next, 3 0x004e6fe0, 4 0x004e6c10 deleting
//     dtor, 5/6 0x004e6eb0 Insert, 7 0x004e6f90 FindNode, 8..10 inherited, 11 0x004e6d20 Clear.
//   Item pool (0x24-byte header): +4 chain of extra blocks, +8 free list, +0xc heap, +0x10 block size,
//     +0x14 item size, +0x18 items per block, +0x1c alignment shift (u16), +0x1e flags (u16), +0x20 alloc tag.
#include <windows.h>

#include <cmath>
#include <cstring>

#include "game.h"
#include "gen/entry_stubs.h"
#include "gen/orig_calls.h"
#include "hooks.h"

namespace
{
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
inline uint16_t& W(uint32_t a)
{
    return *reinterpret_cast<uint16_t*>(a);
}
inline void* P(uint32_t v)
{
    return reinterpret_cast<void*>(v);
}
template <class... A>
inline uint32_t VC(uint32_t object, uint32_t off, A... args)
{
    return VCall<uint32_t>(P(object), static_cast<int>(off / 4), args...);
}
// QueryPerformanceCounter through the game's import slot 0x0056b08c (so the hook DLL's import hooks still apply).
inline void Qpc(LARGE_INTEGER* li)
{
    using Fn = BOOL(WINAPI*)(LARGE_INTEGER*);
    GameGlobal<Fn>(0x0056b08c)(li);
}
// The game's PMS heap object.
constexpr uint32_t kPmsHeap = 0x0063a210;

// 0x004e4520 Timer_Seconds: seconds since the timer start in st0 (the ABI table lists eax). Also returns the eax it
// leaves (the high dword of the start counter 0x00604adc), which 0x004e4790 passes on.
float Call_Timer_Seconds(uint32_t* eax_out)
{
    static const uint32_t target = 0x004e4520;
    float r;
    uint32_t e;
    __asm {
        push ebx
        push esi
        push edi
        push ebp
        call dword ptr [target]
        pop ebp
        pop edi
        pop esi
        pop ebx
        mov e, eax
        fstp r
    }
    *eax_out = e;
    return r;
}
}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// Timer (globals: 0x00604a90 frame dt, 0x00604a94 unscaled dt, 0x00604a98 game time, 0x00604aa0 time scale,
// 0x00604aa4 paused/clamped time offset, 0x00604aa8 pause start, 0x00604aac pause count, 0x00604ab0 / 0x00604ab4
// clock, 0x00604ab8 fixed dt override, 0x00604abc / 0x00604ac0 slow-motion flag and factor, 0x00604ac4 last time,
// 0x00604ac8 QPC frequency, 0x00604ad0 / 0x00604ad8 start counter, 0x0063a0e4 frame counter, 0x005bcbe4 max dt).
// ---------------------------------------------------------------------------------------------------------------

// 0x004e4560 Timer_Init (no inputs; returns eax = low dword of the counter delta): resets the timer globals, reads
// the QPC frequency (import 0x0056b090) and start counter, and sets the game time to the elapsed seconds.
// Name from the tracker; behaviour from the disassembly.
uint32_t Timer_Init()
{
    LARGE_INTEGER now;
    GameGlobal<uint32_t>(0x00604a9c) = 0;
    GameGlobal<uint32_t>(0x00604a94) = 0x3c888889;  // 1/60
    GameGlobal<uint32_t>(0x00604a90) = 0x3c888889;
    GameGlobal<uint32_t>(0x00604aa0) = 0x3f800000;
    GameGlobal<uint32_t>(0x00604aa4) = 0;
    GameGlobal<uint32_t>(0x00604aa8) = 0;
    GameGlobal<uint32_t>(0x00604aac) = 0;
    GameGlobal<uint32_t>(0x00604ab0) = 0;
    GameGlobal<uint32_t>(0x00604ab4) = 0;
    GameGlobal<uint32_t>(0x00604ac4) = 0;
    GameGlobal<uint8_t>(0x00604abc) = 0;
    GameGlobal<uint32_t>(0x00604ac0) = 0x3f800000;
    GameGlobal<uint32_t>(0x00604ab8) = 0;
    GameGlobal<uint32_t>(0x0063a0e4) = 0;
    using QpfFn = BOOL(WINAPI*)(LARGE_INTEGER*);
    GameGlobal<QpfFn>(0x0056b090)(reinterpret_cast<LARGE_INTEGER*>(0x00604ac8));
    Qpc(reinterpret_cast<LARGE_INTEGER*>(0x00604ad0));
    GameGlobal<int64_t>(0x00604ad8) = GameGlobal<int64_t>(0x00604ad0);
    Qpc(&now);
    int64_t delta = now.QuadPart - GameGlobal<int64_t>(0x00604ad8);
    float t = static_cast<float>(static_cast<double>(delta) / static_cast<double>(GameGlobal<int64_t>(0x00604ac8)));
    GameGlobal<float>(0x00604a98) = t;
    GameGlobal<float>(0x00604ac4) = t;
    return static_cast<uint32_t>(delta);
}

static uint32_t Timer_Init_Abi()
{
    return Timer_Init();
}
ABI_ENTRY_004e4560(Timer_Init_Entry, Timer_Init_Abi)
HOOK(0x004e4560, "Timer_Init", Timer_Init_Entry, nullptr);

// 0x004e4650 (no inputs; returns st0): the timer clock. Unless paused (0x00604aac), reads the counter, subtracts
// the accumulated offset 0x00604aa4 and advances the clock 0x00604ab0/0x00604ab4 by the elapsed time, replaced by
// the fixed dt 0x00604ab8 when set, scaled by 0x00604ac0 when 0x00604abc is set and clamped to 0x005bcbe4; the time
// cut off goes into the offset. Returns the clock. Caller: Timer_Tick 0x004e47e0.
float Timer_Clock()
{
    if (GameGlobal<uint32_t>(0x00604aac) == 0)
    {
        LARGE_INTEGER now;
        Qpc(&now);
        int64_t delta = now.QuadPart - GameGlobal<int64_t>(0x00604ad8);
        float x =
            static_cast<float>(static_cast<double>(delta) / static_cast<double>(GameGlobal<int64_t>(0x00604ac8)));
        x = x - GameGlobal<float>(0x00604aa4);
        GameGlobal<float>(0x00604ab0) = x;
        x = x - GameGlobal<float>(0x00604ab4);
        float cur;
        if (GameGlobal<float>(0x00604ab8) == GameGlobal<float>(0x0056c05c))
        {
            cur = GameGlobal<float>(0x00604ab4);
        }
        else
        {
            GameGlobal<float>(0x00604ab0) = GameGlobal<float>(0x00604ab8) + GameGlobal<float>(0x00604ab4);
            GameGlobal<float>(0x00604aa4) = (x - GameGlobal<float>(0x00604ab8)) + GameGlobal<float>(0x00604aa4);
            x = GameGlobal<float>(0x00604ab8);
            cur = GameGlobal<float>(0x00604ab0);
        }
        if (GameGlobal<uint8_t>(0x00604abc) != 0)
        {
            float s = GameGlobal<float>(0x00604ac0) * x;
            GameGlobal<float>(0x00604ab0) = cur + s;
            GameGlobal<float>(0x00604aa4) = (x - s) + GameGlobal<float>(0x00604aa4);
            x = s;
            cur = GameGlobal<float>(0x00604ab0);
        }
        float k = GameGlobal<float>(0x005bcbe4);
        if (x > k)
        {
            GameGlobal<float>(0x00604ab0) = cur + k;
            GameGlobal<uint32_t>(0x00604ab4) = GameGlobal<uint32_t>(0x00604ab0);
            GameGlobal<float>(0x00604aa4) = (x - k) + GameGlobal<float>(0x00604aa4);
            return GameGlobal<float>(0x00604ab0);
        }
        GameGlobal<uint32_t>(0x00604ab4) = GameGlobal<uint32_t>(0x00604ab0);
    }
    return GameGlobal<float>(0x00604ab0);
}

static float Timer_Clock_Abi()
{
    return Timer_Clock();
}
ABI_ENTRY_004e4650(Timer_Clock_Entry, Timer_Clock_Abi)
HOOK(0x004e4650, "Timer_Clock", Timer_Clock_Entry, nullptr);

// 0x004e4790 (eax = pause): pause counter. pause != 0: the first pause records Timer_Seconds in 0x00604aa8, then
// the count 0x00604aac is incremented; pause == 0: decrements a nonzero count and, when it reaches zero, adds the
// paused time to the offset 0x00604aa4. Returns eax as the original leaves it (the old count, or Timer_Seconds's
// eax after a call).
uint32_t Timer_Pause(uint32_t pause)
{
    uint32_t eax = GameGlobal<uint32_t>(0x00604aac);
    if (pause != 0)
    {
        if (eax == 0)
            GameGlobal<float>(0x00604aa8) = Call_Timer_Seconds(&eax);
        ++GameGlobal<uint32_t>(0x00604aac);
        return eax;
    }
    if (eax != 0 && --GameGlobal<uint32_t>(0x00604aac) == 0)
    {
        float t = Call_Timer_Seconds(&eax);
        GameGlobal<float>(0x00604aa4) = (t - GameGlobal<float>(0x00604aa8)) + GameGlobal<float>(0x00604aa4);
    }
    return eax;
}

static uint32_t Timer_Pause_Abi(uint32_t eax)
{
    return Timer_Pause(eax);
}
ABI_ENTRY_004e4790(Timer_Pause_Entry, Timer_Pause_Abi)
HOOK(0x004e4790, "Timer_Pause", Timer_Pause_Entry, nullptr);

// 0x004e47e0 Timer_Tick (no inputs): per-frame timer update. Paused: frame dt 0x00604a90 = 0. Otherwise the game
// time 0x00604a98 = Timer_Clock (the last time 0x00604ac4 follows it when it went backwards), unscaled dt 0x00604a94 =
// time - last, frame counter 0x0063a0e4++, frame dt = time scale 0x00604aa0 * (fixed dt 0x00604ab8 if set, else
// the unscaled dt), capped to 0.25 when above 0x005bcbe4; last += unscaled dt. Name from the tracker.
// Return value: the original leaves the x87 status word in ax (upper half = the frame counter); reproduced as the
// frame counter in the upper half and 0 below (callers ignore it: 0x004e41e0).
uint32_t Timer_Tick()
{
    if (GameGlobal<uint32_t>(0x00604aac) != 0)
    {
        GameGlobal<uint32_t>(0x00604a90) = 0;
        return GameGlobal<uint32_t>(0x00604aac);
    }
    float t = Timer_Clock();
    GameGlobal<float>(0x00604a98) = t;
    if (t < GameGlobal<float>(0x00604ac4))
        GameGlobal<uint32_t>(0x00604ac4) = GameGlobal<uint32_t>(0x00604a98);
    float dt = GameGlobal<float>(0x00604a98) - GameGlobal<float>(0x00604ac4);
    uint32_t frame = GameGlobal<uint32_t>(0x0063a0e4) + 1;
    GameGlobal<float>(0x00604a94) = dt;
    GameGlobal<uint32_t>(0x0063a0e4) = frame;
    float v = (GameGlobal<float>(0x00604ab8) == GameGlobal<float>(0x0056c05c)) ? dt : GameGlobal<float>(0x00604ab8);
    GameGlobal<float>(0x00604a90) = GameGlobal<float>(0x00604aa0) * v;
    if (GameGlobal<float>(0x00604a90) > GameGlobal<float>(0x005bcbe4))
        GameGlobal<uint32_t>(0x00604a90) = 0x3e800000;  // 0.25
    GameGlobal<float>(0x00604ac4) = GameGlobal<float>(0x00604ac4) + dt;
    return frame & 0xffff0000u;
}

static uint32_t Timer_Tick_Abi()
{
    return Timer_Tick();
}
ABI_ENTRY_004e47e0(Timer_Tick_Entry, Timer_Tick_Abi)
HOOK(0x004e47e0, "Timer_Tick", Timer_Tick_Entry, nullptr);

// 0x004e574f: skipped, not a function (the last nop of the padding before 0x004e5750).

// ---------------------------------------------------------------------------------------------------------------
// Strings
// ---------------------------------------------------------------------------------------------------------------

// 0x004e5980 (ecx unused, esi = path, edi = destination): copies the file name after the last '\\' (the whole path
// when there is none) to the destination and cuts it at its last '.'. Returns eax = strrchr's result (0 when there
// is no '.'). _strrchr 0x00539ee0 is called through the original (its ABI lists ecx, passed as the caller had it).
uint32_t Path_GetBaseName(uint32_t ecx, const char* path, char* dest)
{
    uint32_t slash = orig::fn_00539ee0(ecx, reinterpret_cast<uint32_t>(path), 0x5c);
    const char* src = slash ? reinterpret_cast<const char*>(slash + 1) : path;
    char* d = dest;
    char c;
    do
    {
        c = *src++;
        *d++ = c;
    } while (c != 0);
    uint32_t dot = orig::fn_00539ee0(static_cast<uint8_t>(c) | (ecx & 0xffffff00u), reinterpret_cast<uint32_t>(dest), 0x2e);
    if (dot)
        *reinterpret_cast<char*>(dot) = 0;
    return dot;
}

static uint32_t Path_GetBaseName_Abi(uint32_t ecx, uint32_t esi, uint32_t edi)
{
    return Path_GetBaseName(ecx, reinterpret_cast<const char*>(esi), reinterpret_cast<char*>(edi));
}
ABI_ENTRY_004e5980(Path_GetBaseName_Entry, Path_GetBaseName_Abi)
HOOK(0x004e5980, "Path_GetBaseName", Path_GetBaseName_Entry, nullptr);

// 0x004e59d0 (eax = capacity, ecx = wide destination, edi = narrow source): widens the string (movsx: chars are
// sign-extended) into at most capacity - 1 characters plus a 0. Returns the count copied. Original quirk kept: a
// capacity of 0 does not stop the copy (dec wraps to -1).
uint32_t Str_ToWide(uint32_t capacity, int16_t* dest, const char* src)
{
    uint32_t len = static_cast<uint32_t>(std::strlen(src));
    uint32_t i = 0;
    if (len != 0)
    {
        while (--capacity != 0)
        {
            dest[i] = static_cast<int16_t>(static_cast<signed char>(src[i]));
            ++i;
            if (i >= len)
                break;
        }
    }
    dest[i] = 0;
    return i;
}

static uint32_t Str_ToWide_Abi(uint32_t eax, uint32_t ecx, uint32_t edi)
{
    return Str_ToWide(eax, reinterpret_cast<int16_t*>(ecx), reinterpret_cast<const char*>(edi));
}
ABI_ENTRY_004e59d0(Str_ToWide_Entry, Str_ToWide_Abi)
HOOK(0x004e59d0, "Str_ToWide", Str_ToWide_Entry, nullptr);

// 0x004e5a10 (esi = narrow destination, edi = wide source; stack: capacity, caller pops): narrows the string (low
// byte of each character) into at most capacity - 1 characters plus a 0; length from _wcslen 0x00538ea0. Returns
// the count copied. Same capacity-0 quirk as 0x004e59d0. Not executed in recorded runs.
uint32_t Str_FromWide(char* dest, const uint16_t* src, uint32_t capacity)
{
    uint32_t len = orig::fn_00538ea0(reinterpret_cast<uint32_t>(src));
    uint32_t i = 0;
    if (len != 0)
    {
        while (--capacity != 0)
        {
            dest[i] = static_cast<char>(src[i]);
            ++i;
            if (i >= len)
                break;
        }
    }
    dest[i] = 0;
    return i;
}

static uint32_t Str_FromWide_Abi(uint32_t esi, uint32_t edi, uint32_t a0)
{
    return Str_FromWide(reinterpret_cast<char*>(esi), reinterpret_cast<const uint16_t*>(edi), a0);
}
ABI_ENTRY_004e5a10(Str_FromWide_Entry, Str_FromWide_Abi)
HOOK(0x004e5a10, "Str_FromWide", Str_FromWide_Entry, nullptr);

// 0x004e5a40 (ecx = string; stack: pattern, callee pops): the first position in the string where the pattern
// occurs (_strncmp 0x00539ea0 over the pattern length, positions up to len(string) - len(pattern), unsigned
// compare), or 0; 0 also for an empty string. The ABI lists eax edx; edx is what _strncmp left (0 here).
// Not executed in recorded runs.
uint32_t Str_Find(const char* str, const char* pattern)
{
    uint32_t plen = static_cast<uint32_t>(std::strlen(pattern));
    uint32_t last = reinterpret_cast<uint32_t>(str) + static_cast<uint32_t>(std::strlen(str)) - plen;
    uint32_t p = reinterpret_cast<uint32_t>(str);
    if (*str == 0)
        return 0;
    while (p <= last)
    {
        if (orig::fn_00539ea0(p, reinterpret_cast<uint32_t>(pattern), plen) == 0)
            return p;
        ++p;
        if (By(p) == 0)
            break;
    }
    return 0;
}

static uint64_t Str_Find_Abi(uint32_t ecx, uint32_t a0)
{
    return Str_Find(reinterpret_cast<const char*>(ecx), reinterpret_cast<const char*>(a0));
}
ABI_ENTRY_004e5a40(Str_Find_Entry, Str_Find_Abi)
HOOK(0x004e5a40, "Str_Find", Str_Find_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// PMS_List / PMS_Tree (layouts in the header comment)
// ---------------------------------------------------------------------------------------------------------------

uint32_t PMS_Pool_Grow(uint32_t pool);
uint32_t PMS_Pool_Reset(uint32_t pool, uint32_t always);
uint32_t PMS_List_Clear(uint32_t list);
uint32_t PMS_List_Destruct(uint32_t list);

namespace
{
// The tree's comparator (+0x18, cdecl): compare(key, node value).
inline int32_t TreeCompare(uint32_t tree, uint32_t key, uint32_t value)
{
    return reinterpret_cast<int32_t(__cdecl*)(uint32_t, uint32_t)>(D(tree + 0x18))(key, value);
}
// Takes a node from the list's pool (+8), growing the pool when its free list (+8) is empty (inlined everywhere).
inline uint32_t TakeNode(uint32_t pool)
{
    if (D(pool + 8) == 0)
        PMS_Pool_Grow(pool);
    uint32_t node = D(pool + 8);
    D(pool + 8) = D(node);
    return node;
}
}  // namespace

// 0x004e6bd0 (edx = passed on to 0x004e06f0, esi = this; stack: comparator, callee pops): PMS_Tree constructor.
// Base part as 0x004e7010 with its own pool (0x004e06f0: heap 0x0063a210, "PMS_List::m_listData", item size 0x1c,
// 2, 5), the comparator at +0x18, then the derived vtable 0x0058b060. Returns this.
uint32_t PMS_Tree_Construct(uint32_t edx, uint32_t self, uint32_t comparator)
{
    D(self) = 0x0058b098;
    D(self + 8) = orig::fn_004e06f0(edx, kPmsHeap, 0x0058b0e0, 0x1c, 2, 5);
    D(self + 0xc) = 0;
    D(self + 0x10) = 0;
    D(self + 0x14) = 0;
    D(self + 0x18) = comparator;
    By(self + 4) = 1;
    D(self) = 0x0058b060;
    return self;
}

static uint32_t PMS_Tree_Construct_Abi(uint32_t edx, uint32_t esi, uint32_t a0)
{
    return PMS_Tree_Construct(edx, esi, a0);
}
ABI_ENTRY_004e6bd0(PMS_Tree_Construct_Entry, PMS_Tree_Construct_Abi)
HOOK(0x004e6bd0, "PMS_Tree_Construct", PMS_Tree_Construct_Entry, nullptr);

// 0x004e6c10 (ecx = this; stack: flags, callee pops): PMS_Tree deleting destructor (vtable 0x0058b060 slot 4):
// 0x004e6c40, then PMS_heapFree 0x004e08d0 (heap 0x0063a210) when bit 0 of flags is set. Returns this.
uint32_t PMS_Tree_DeletingDtor(uint32_t self, uint32_t flags)
{
    PMS_List_Destruct(self);
    if (flags & 1)
        orig::fn_004e08d0(kPmsHeap, self);
    return self;
}

static uint32_t PMS_Tree_DeletingDtor_Abi(uint32_t ecx, uint32_t a0)
{
    return PMS_Tree_DeletingDtor(ecx, a0);
}
ABI_ENTRY_004e6c10(PMS_Tree_DeletingDtor_Entry, PMS_Tree_DeletingDtor_Abi)
HOOK(0x004e6c10, "PMS_Tree_DeletingDtor", PMS_Tree_DeletingDtor_Entry, nullptr);

// 0x004e6c40 (eax = this): PMS_List destructor body: base vtable 0x0058b098, PMS_List_Clear, and when the list owns
// its pool (+4) frees it (0x004e0790, esi = pool, edi = heap 0x0063a210). Returns 0x004e0790's eax, else 0 (the
// original leaves al = 0 with stale upper bytes; the only caller 0x004e6c10 ignores it).
uint32_t PMS_List_Destruct(uint32_t self)
{
    D(self) = 0x0058b098;
    PMS_List_Clear(self);
    if (By(self + 4) == 0)
        return 0;
    return orig::fn_004e0790(D(self + 8), kPmsHeap);
}

static uint32_t PMS_List_Destruct_Abi(uint32_t eax)
{
    return PMS_List_Destruct(eax);
}
ABI_ENTRY_004e6c40(PMS_List_Destruct_Entry, PMS_List_Destruct_Abi)
HOOK(0x004e6c40, "PMS_List_Destruct", PMS_List_Destruct_Entry, nullptr);

// 0x004e6c70 (ecx = this): PMS_Tree slot 1: the first node of an in-order walk, virtual slot 2 (Next) with
// (0, root). Not executed in recorded runs.
uint32_t PMS_Tree_First(uint32_t self)
{
    return VC(self, 8, 0u, D(self + 0xc));
}

static uint32_t PMS_Tree_First_Abi(uint32_t ecx)
{
    return PMS_Tree_First(ecx);
}
ABI_ENTRY_004e6c70(PMS_Tree_First_Entry, PMS_Tree_First_Abi)
HOOK(0x004e6c70, "PMS_Tree_First", PMS_Tree_First_Entry, nullptr);

// 0x004e6c80 (ecx = this; stack: node, fromRoot, callee pops): PMS_Tree slot 2: one step of a threaded (Morris)
// in-order walk over the +4 / +0 children: starts at the root when fromRoot is set, otherwise at node->+0; threads
// are written into and removed from the +0 links. Returns the next node or 0. Not executed in recorded runs.
uint32_t PMS_Tree_Next(uint32_t self, uint32_t node, uint32_t fromRoot)
{
    uint32_t eax;
    if (fromRoot)
    {
        eax = D(self + 0xc);
    }
    else
    {
        if (node == 0)
            return 0;
        eax = D(node);
    }
    if (eax == 0)
        return 0;
    for (;;)
    {
        uint32_t edx = D(eax + 4);
        if (edx == 0)
            return eax;
        uint32_t ecx = D(edx);
        while (ecx != 0 && ecx != eax)
        {
            edx = ecx;
            ecx = D(edx);
        }
        if (D(edx) != 0)
        {
            D(edx) = 0;
            return eax;
        }
        D(edx) = eax;
        eax = D(eax + 4);
        if (eax == 0)
            return 0;
    }
}

static uint32_t PMS_Tree_Next_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return PMS_Tree_Next(ecx, a0, a1);
}
ABI_ENTRY_004e6c80(PMS_Tree_Next_Entry, PMS_Tree_Next_Abi)
HOOK(0x004e6c80, "PMS_Tree_Next", PMS_Tree_Next_Entry, nullptr);

// 0x004e6ce0 (ecx = node; stack: 1 argument, unused, callee pops): the in-order successor: the +0 child's last
// descendant along +4 links; without a +0 child, climbs while the node is its parent's +0 child and returns that
// parent (0 at the end). Caller: 0x004e6d80 (passes the tree as the unused argument). Not executed in recorded runs.
uint32_t PMS_Tree_Successor(uint32_t node, uint32_t /*unused*/)
{
    uint32_t eax = D(node);
    if (eax != 0)
    {
        uint32_t ecx = D(eax + 4);
        if (ecx == 0)
            return eax;
        do
        {
            eax = ecx;
            ecx = D(eax + 4);
        } while (ecx != 0);
        return eax;
    }
    eax = D(node + 8);
    while (eax != 0 && node == D(eax))
    {
        node = eax;
        eax = D(eax + 8);
    }
    return eax;
}

static uint32_t PMS_Tree_Successor_Abi(uint32_t ecx, uint32_t a0)
{
    return PMS_Tree_Successor(ecx, a0);
}
ABI_ENTRY_004e6ce0(PMS_Tree_Successor_Entry, PMS_Tree_Successor_Abi)
HOOK(0x004e6ce0, "PMS_Tree_Successor", PMS_Tree_Successor_Entry, nullptr);

// 0x004e6d20 (ecx = this): PMS_Tree slot 11: returns every node to the pool (post-order: descends +4 first, then
// +0, pushes a leaf on the pool's free list +8 and cuts it from its parent), then root = count = 0. Returns 0.
// Not executed in recorded runs.
uint32_t PMS_Tree_Clear(uint32_t self)
{
    uint32_t eax = D(self + 0xc);
    while (eax != 0)
    {
        while (D(eax + 4) != 0)
            eax = D(eax + 4);
        uint32_t edx = D(eax);
        if (edx == 0)
        {
            uint32_t pool = D(self + 8);
            edx = D(eax + 8);
            D(eax) = D(pool + 8);
            D(pool + 8) = eax;
            if (edx != 0)
            {
                if (D(edx + 4) != 0)
                    D(edx + 4) = 0;
                else
                    D(edx) = 0;
            }
        }
        eax = edx;
    }
    D(self + 0xc) = 0;
    D(self + 0x14) = 0;
    return 0;
}

static uint32_t PMS_Tree_Clear_Abi(uint32_t ecx)
{
    return PMS_Tree_Clear(ecx);
}
ABI_ENTRY_004e6d20(PMS_Tree_Clear_Entry, PMS_Tree_Clear_Abi)
HOOK(0x004e6d20, "PMS_Tree_Clear", PMS_Tree_Clear_Entry, nullptr);

// 0x004e6d80 (ecx = this; stack: node, callee pops): PMS_Tree slot 0, remove a node. A locked node (+0x18 != 0)
// is only marked removed (+0x14 = 1; returns the lock count). Otherwise the node to unlink is the node itself or,
// with two children, its successor (0x004e6ce0) whose value is then copied into the node; its child takes its
// place and the unlinked node goes back to the pool's free list. Returns the unlinked node.
// ORIGINAL BUG (reproduced): the parent test is inverted. When the unlinked node has a parent, the tree root
// (+0xc) is overwritten with the child; when it has none (it is the root), the code reads and writes address 4
// (crash). The parent's child link is never updated. Not executed in recorded runs.
uint32_t PMS_Tree_RemoveNode(uint32_t self, uint32_t node)
{
    if (D(node + 0x18) != 0)
    {
        By(node + 0x14) = 1;
        return D(node + 0x18);
    }
    uint32_t eax = (D(node + 4) != 0 && D(node) != 0) ? PMS_Tree_Successor(node, self) : node;
    uint32_t ecx = D(eax + 4);
    if (ecx == 0)
        ecx = D(eax);
    if (ecx != 0)
        D(ecx + 8) = D(eax + 8);
    uint32_t edx = D(eax + 8);
    if (edx != 0)
    {
        D(self + 0xc) = ecx;
    }
    else
    {
        volatile uint32_t* low = reinterpret_cast<volatile uint32_t*>(static_cast<uintptr_t>(4));
        if (eax == *low)
            *low = ecx;
        else
            *reinterpret_cast<volatile uint32_t*>(static_cast<uintptr_t>(edx)) = ecx;
    }
    if (eax != node)
        D(node + 0xc) = D(eax + 0xc);
    uint32_t pool = D(self + 8);
    D(eax) = D(pool + 8);
    D(pool + 8) = eax;
    return eax;
}

static uint32_t PMS_Tree_RemoveNode_Abi(uint32_t ecx, uint32_t a0)
{
    return PMS_Tree_RemoveNode(ecx, a0);
}
ABI_ENTRY_004e6d80(PMS_Tree_RemoveNode_Entry, PMS_Tree_RemoveNode_Abi)
HOOK(0x004e6d80, "PMS_Tree_RemoveNode", PMS_Tree_RemoveNode_Entry, nullptr);

namespace
{
// The node setup shared by 0x004e6e00 / 0x004e6ed0 after the descent.
uint32_t TreeLink(uint32_t self, uint32_t value, uint32_t parent, int32_t cmp)
{
    uint32_t pool = D(self + 8);
    uint32_t node = TakeNode(pool);
    D(node + 0xc) = value;
    D(node + 8) = parent;
    D(node + 4) = 0;
    D(node) = 0;
    D(node + 0x10) = D(self + 0x14);
    D(node + 0x18) = 0;
    By(node + 0x14) = 0;
    if (parent == 0)
        D(self + 0xc) = node;
    else if (cmp < 0)
        D(parent + 4) = node;
    else
        D(parent) = node;
    D(self + 0x14) = D(self + 0x14) + 1;
    return node;
}
}  // namespace

// 0x004e6e00 (edi = this; stack: value, callee pops): PMS_Tree insert: descends from the root with the comparator
// (+0x18, (value, node value): < 0 -> +4, else +0), takes a node from the pool (0x004f6150 grows it when empty),
// stores the value, parent and the insertion index (+0x10 = count), links it and counts it. Returns the node.
uint32_t PMS_Tree_Insert(uint32_t self, uint32_t value)
{
    uint32_t parent = 0;
    int32_t cmp = 0;
    uint32_t n = D(self + 0xc);
    while (n != 0)
    {
        parent = n;
        cmp = TreeCompare(self, value, D(n + 0xc));
        n = (cmp < 0) ? D(n + 4) : D(n);
    }
    return TreeLink(self, value, parent, cmp);
}

static uint32_t PMS_Tree_Insert_Abi(uint32_t edi, uint32_t a0)
{
    return PMS_Tree_Insert(edi, a0);
}
ABI_ENTRY_004e6e00(PMS_Tree_Insert_Entry, PMS_Tree_Insert_Abi)
HOOK(0x004e6e00, "PMS_Tree_Insert", PMS_Tree_Insert_Entry, nullptr);

// 0x004e6eb0 (ecx = this; stack: value, callee pops): PMS_Tree slots 5 and 6: PMS_Tree_Insert. Not executed in
// recorded runs.
uint32_t PMS_Tree_InsertThunk(uint32_t self, uint32_t value)
{
    return PMS_Tree_Insert(self, value);
}

static uint32_t PMS_Tree_InsertThunk_Abi(uint32_t ecx, uint32_t a0)
{
    return PMS_Tree_InsertThunk(ecx, a0);
}
ABI_ENTRY_004e6eb0(PMS_Tree_InsertThunk_Entry, PMS_Tree_InsertThunk_Abi)
HOOK(0x004e6eb0, "PMS_Tree_InsertThunk", PMS_Tree_InsertThunk_Entry, nullptr);

// 0x004e6ed0 (stack: tree, value; callee pops): as PMS_Tree_Insert, but returns the existing node when the
// comparator reports an equal value.
uint32_t PMS_Tree_InsertUnique(uint32_t self, uint32_t value)
{
    uint32_t parent = 0;
    int32_t cmp = 0;
    uint32_t n = D(self + 0xc);
    while (n != 0)
    {
        parent = n;
        cmp = TreeCompare(self, value, D(n + 0xc));
        if (cmp == 0)
            return n;
        n = (cmp < 0) ? D(n + 4) : D(n);
    }
    return TreeLink(self, value, parent, cmp);
}

static uint32_t PMS_Tree_InsertUnique_Abi(uint32_t a0, uint32_t a1)
{
    return PMS_Tree_InsertUnique(a0, a1);
}
ABI_ENTRY_004e6ed0(PMS_Tree_InsertUnique_Entry, PMS_Tree_InsertUnique_Abi)
HOOK(0x004e6ed0, "PMS_Tree_InsertUnique", PMS_Tree_InsertUnique_Entry, nullptr);

// 0x004e6f90 (ecx = this; stack: key, callee pops): PMS_Tree slot 7: the node whose value compares equal to the
// key and is not marked removed (+0x14), searching on through +0 past removed ones; 0 when none.
uint32_t PMS_Tree_FindNode(uint32_t self, uint32_t key)
{
    uint32_t n = D(self + 0xc);
    while (n != 0)
    {
        int32_t c = TreeCompare(self, key, D(n + 0xc));
        if (c == 0)
        {
            if (By(n + 0x14) == 0)
                return n;
            n = D(n);
        }
        else
        {
            n = (c > 0) ? D(n) : D(n + 4);
        }
    }
    return 0;
}

static uint32_t PMS_Tree_FindNode_Abi(uint32_t ecx, uint32_t a0)
{
    return PMS_Tree_FindNode(ecx, a0);
}
ABI_ENTRY_004e6f90(PMS_Tree_FindNode_Entry, PMS_Tree_FindNode_Abi)
HOOK(0x004e6f90, "PMS_Tree_FindNode", PMS_Tree_FindNode_Entry, nullptr);

// 0x004e6fe0 (ecx = this; stack: node, callee pops): PMS_Tree slot 3: calls virtual slot 2 (Next) with (node, 0)
// and then with each result until it returns 0 (finishes a threaded walk, removing its threads). Returns 0.
uint32_t fn_004e6fe0(uint32_t self, uint32_t node)
{
    uint32_t p = VC(self, 8, node, 0u);
    while (p != 0)
        p = VC(self, 8, p, 0u);
    return 0;
}

static uint32_t fn_004e6fe0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_004e6fe0(ecx, a0);
}
ABI_ENTRY_004e6fe0(fn_004e6fe0_Entry, fn_004e6fe0_Abi)
HOOK(0x004e6fe0, "fn_004e6fe0", fn_004e6fe0_Entry, nullptr);

// 0x004e7010 (eax = pool or 0, edx = passed on to 0x004e06f0, esi = this): PMS_List constructor: vtable 0x0058b098;
// with a pool given the list uses it (+4 = 0), otherwise creates its own (0x004e06f0: heap 0x0063a210,
// "PMS_List::m_listData" 0x0058b0e0, item size 0x1c, 2, 5; +4 = 1). Empty list. Returns this.
uint32_t PMS_List_Construct(uint32_t pool, uint32_t edx, uint32_t self)
{
    D(self) = 0x0058b098;
    if (pool != 0)
    {
        By(self + 4) = 0;
        D(self + 8) = pool;
        D(self + 0xc) = 0;
        D(self + 0x10) = 0;
        D(self + 0x14) = 0;
        return self;
    }
    D(self + 8) = orig::fn_004e06f0(edx, kPmsHeap, 0x0058b0e0, 0x1c, 2, 5);
    D(self + 0xc) = 0;
    D(self + 0x10) = 0;
    D(self + 0x14) = 0;
    By(self + 4) = 1;
    return self;
}

static uint32_t PMS_List_Construct_Abi(uint32_t eax, uint32_t edx, uint32_t esi)
{
    return PMS_List_Construct(eax, edx, esi);
}
ABI_ENTRY_004e7010(PMS_List_Construct_Entry, PMS_List_Construct_Abi)
HOOK(0x004e7010, "PMS_List_Construct", PMS_List_Construct_Entry, nullptr);

// 0x004e7060 (ecx = this; stack: flags, callee pops): PMS_List deleting destructor (vtable 0x0058b098 slot 4): the
// same body as 0x004e6c40, then PMS_heapFree 0x004e08d0 when bit 0 of flags is set. Returns this. Not executed in
// recorded runs.
uint32_t PMS_List_DeletingDtor(uint32_t self, uint32_t flags)
{
    PMS_List_Destruct(self);
    if (flags & 1)
        orig::fn_004e08d0(kPmsHeap, self);
    return self;
}

static uint32_t PMS_List_DeletingDtor_Abi(uint32_t ecx, uint32_t a0)
{
    return PMS_List_DeletingDtor(ecx, a0);
}
ABI_ENTRY_004e7060(PMS_List_DeletingDtor_Entry, PMS_List_DeletingDtor_Abi)
HOOK(0x004e7060, "PMS_List_DeletingDtor", PMS_List_DeletingDtor_Entry, nullptr);

// 0x004e70a0 (ecx = this): PMS_List clear. A list owning its pool resets the whole pool (0x004f61f0 with
// flag bit 0 of pool +0x1e); otherwise the node chain head..tail is spliced onto the pool's free list. Head, tail and
// count become 0. Returns 0x004f61f0's eax, the pool, or 0 (callers ignore it).
uint32_t PMS_List_Clear(uint32_t self)
{
    uint32_t r = 0;
    if (By(self + 4) != 0)
    {
        uint32_t pool = D(self + 8);
        r = PMS_Pool_Reset(pool, By(pool + 0x1e) & 1u);
    }
    else if (D(self + 0xc) != 0)
    {
        uint32_t pool = D(self + 8);
        D(D(self + 0x10)) = D(pool + 8);
        D(pool + 8) = D(self + 0xc);
        r = pool;
    }
    D(self + 0xc) = 0;
    D(self + 0x10) = 0;
    D(self + 0x14) = 0;
    return r;
}

static uint32_t PMS_List_Clear_Abi(uint32_t ecx)
{
    return PMS_List_Clear(ecx);
}
ABI_ENTRY_004e70a0(PMS_List_Clear_Entry, PMS_List_Clear_Abi)
HOOK(0x004e70a0, "PMS_List_Clear", PMS_List_Clear_Entry, nullptr);

// 0x004e70f0 (ecx = this; stack: value, callee pops): PMS_List slot 5, push front: takes a node from the pool
// (0x004f6150 grows it), clears its lock (+0x18) and removed flag (+0x14), stores the value and links it before
// the head (it becomes the tail of an empty list). Returns the node.
uint32_t PMS_List_PushFront(uint32_t self, uint32_t value)
{
    uint32_t node = TakeNode(D(self + 8));
    D(node + 0x18) = 0;
    By(node + 0x14) = 0;
    D(node + 0xc) = value;
    D(node + 4) = 0;
    if (D(self + 0xc) != 0)
        D(D(self + 0xc) + 4) = node;
    else
        D(self + 0x10) = node;
    D(node) = D(self + 0xc);
    D(self + 0xc) = node;
    D(self + 0x14) = D(self + 0x14) + 1;
    return node;
}

static uint32_t PMS_List_PushFront_Abi(uint32_t ecx, uint32_t a0)
{
    return PMS_List_PushFront(ecx, a0);
}
ABI_ENTRY_004e70f0(PMS_List_PushFront_Entry, PMS_List_PushFront_Abi)
HOOK(0x004e70f0, "PMS_List_PushFront", PMS_List_PushFront_Entry, nullptr);

// 0x004e7160 (ecx = this; stack: value, callee pops): PMS_List slot 6, push back (as PushFront, linked after the
// tail). Returns the node.
uint32_t PMS_List_PushBack(uint32_t self, uint32_t value)
{
    uint32_t node = TakeNode(D(self + 8));
    D(node + 0x18) = 0;
    By(node + 0x14) = 0;
    D(node + 0xc) = value;
    D(node + 4) = D(self + 0x10);
    if (D(self + 0x10) != 0)
        D(D(self + 0x10)) = node;
    else
        D(self + 0xc) = node;
    D(node) = 0;
    D(self + 0x10) = node;
    D(self + 0x14) = D(self + 0x14) + 1;
    return node;
}

static uint32_t PMS_List_PushBack_Abi(uint32_t ecx, uint32_t a0)
{
    return PMS_List_PushBack(ecx, a0);
}
ABI_ENTRY_004e7160(PMS_List_PushBack_Entry, PMS_List_PushBack_Abi)
HOOK(0x004e7160, "PMS_List_PushBack", PMS_List_PushBack_Entry, nullptr);

// 0x004e71c0 (ecx = this; stack: node, callee pops): PMS_List slot 0, remove a node: a locked node (+0x18) is only
// marked removed (+0x14 = 1); otherwise it is unlinked (head / tail fixed) and pushed on the pool's free list. The
// count is not changed here (0x004e72e0 decrements it). Returns the node. Not executed in recorded runs.
uint32_t PMS_List_RemoveNode(uint32_t self, uint32_t node)
{
    if (D(node + 0x18) != 0)
    {
        By(node + 0x14) = 1;
        return node;
    }
    if (node == D(self + 0xc))
        D(self + 0xc) = D(node);
    if (node == D(self + 0x10))
        D(self + 0x10) = D(node + 4);
    if (D(node) != 0)
        D(D(node) + 4) = D(node + 4);
    if (D(node + 4) != 0)
        D(D(node + 4)) = D(node);
    uint32_t pool = D(self + 8);
    D(node) = D(pool + 8);
    D(pool + 8) = node;
    return node;
}

static uint32_t PMS_List_RemoveNode_Abi(uint32_t ecx, uint32_t a0)
{
    return PMS_List_RemoveNode(ecx, a0);
}
ABI_ENTRY_004e71c0(PMS_List_RemoveNode_Entry, PMS_List_RemoveNode_Abi)
HOOK(0x004e71c0, "PMS_List_RemoveNode", PMS_List_RemoveNode_Entry, nullptr);

// 0x004e7210 (ecx = this; stack: value, callee pops): PMS_List slot 7: the first node holding the value that is not
// marked removed, or 0. Not executed in recorded runs.
uint32_t PMS_List_FindNode(uint32_t self, uint32_t value)
{
    for (uint32_t n = D(self + 0xc); n != 0; n = D(n))
    {
        if (D(n + 0xc) == value && By(n + 0x14) == 0)
            return n;
    }
    return 0;
}

static uint32_t PMS_List_FindNode_Abi(uint32_t ecx, uint32_t a0)
{
    return PMS_List_FindNode(ecx, a0);
}
ABI_ENTRY_004e7210(PMS_List_FindNode_Entry, PMS_List_FindNode_Abi)
HOOK(0x004e7210, "PMS_List_FindNode", PMS_List_FindNode_Entry, nullptr);

// 0x004e7240 (ecx = this; stack: value, callee pops): PMS_List slot 8: the value when a node not marked removed holds
// it, else 0. Not executed in recorded runs.
uint32_t PMS_List_Find(uint32_t self, uint32_t value)
{
    for (uint32_t n = D(self + 0xc); n != 0; n = D(n))
    {
        if (D(n + 0xc) == value && By(n + 0x14) == 0)
            return D(n + 0xc);
    }
    return 0;
}

static uint32_t PMS_List_Find_Abi(uint32_t ecx, uint32_t a0)
{
    return PMS_List_Find(ecx, a0);
}
ABI_ENTRY_004e7240(PMS_List_Find_Entry, PMS_List_Find_Abi)
HOOK(0x004e7240, "PMS_List_Find", PMS_List_Find_Entry, nullptr);

// 0x004e7270 (ecx = this; stack: name, callee pops): PMS_List slot 9: the first value (a string) equal to the name
// by _stricmp 0x005448ee (removed flag not checked), or 0. Not executed in recorded runs.
uint32_t PMS_List_FindString(uint32_t self, uint32_t name)
{
    for (uint32_t n = D(self + 0xc); n != 0; n = D(n))
    {
        if (orig::fn_005448ee(self, D(n + 0xc), name) == 0)
            return D(n + 0xc);
    }
    return 0;
}

static uint32_t PMS_List_FindString_Abi(uint32_t ecx, uint32_t a0)
{
    return PMS_List_FindString(ecx, a0);
}
ABI_ENTRY_004e7270(PMS_List_FindString_Entry, PMS_List_FindString_Abi)
HOOK(0x004e7270, "PMS_List_FindString", PMS_List_FindString_Entry, nullptr);

// 0x004e72b0 (ecx = this): PMS_List slot 11: while the list has a head, calls virtual slot 10 (RemoveValue) with
// the head's value. Returns 0. (A locked head is never unlinked: the loop would not end; original behaviour.)
// Not executed in recorded runs.
uint32_t PMS_List_RemoveAll(uint32_t self)
{
    while (D(self + 0xc) != 0)
        VC(self, 0x28, D(D(self + 0xc) + 0xc));
    return 0;
}

static uint32_t PMS_List_RemoveAll_Abi(uint32_t ecx)
{
    return PMS_List_RemoveAll(ecx);
}
ABI_ENTRY_004e72b0(PMS_List_RemoveAll_Entry, PMS_List_RemoveAll_Abi)
HOOK(0x004e72b0, "PMS_List_RemoveAll", PMS_List_RemoveAll_Entry, nullptr);

// 0x004e72e0 (ecx = this; stack: value, callee pops): PMS_List slot 10: finds the node (virtual slot 7), clears its
// value, decrements the count and removes it (virtual slot 0). Returns slot 0's result, or 0 when not found.
uint32_t PMS_List_RemoveValue(uint32_t self, uint32_t value)
{
    uint32_t node = VC(self, 0x1c, value);
    if (node == 0)
        return 0;
    D(node + 0xc) = 0;
    D(self + 0x14) = D(self + 0x14) - 1;
    return VC(self, 0, node);
}

static uint32_t PMS_List_RemoveValue_Abi(uint32_t ecx, uint32_t a0)
{
    return PMS_List_RemoveValue(ecx, a0);
}
ABI_ENTRY_004e72e0(PMS_List_RemoveValue_Entry, PMS_List_RemoveValue_Abi)
HOOK(0x004e72e0, "PMS_List_RemoveValue", PMS_List_RemoveValue_Entry, nullptr);

// 0x004e7310 (thiscall, ecx unused; stack: node, unused, callee pops): PMS_List slot 2: the next node (node->+0),
// 0 for a null node. Not executed in recorded runs.
uint32_t PMS_List_Next(uint32_t node, uint32_t /*unused*/)
{
    return node ? D(node) : 0;
}

static uint32_t PMS_List_Next_Abi(uint32_t a0, uint32_t a1)
{
    return PMS_List_Next(a0, a1);
}
ABI_ENTRY_004e7310(PMS_List_Next_Entry, PMS_List_Next_Abi)
HOOK(0x004e7310, "PMS_List_Next", PMS_List_Next_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// PMS_List iterator {+0 list, +4 current node}: the current node is locked (+0x18) while the iterator is on it, so
// removals during the walk only mark it (+0x14); the iterator removes marked nodes as it passes them.
// ---------------------------------------------------------------------------------------------------------------

// 0x004e7340 (eax = list, esi = iterator): begin. Removes marked, unlocked nodes from the head (virtual slot 0),
// takes the first node (virtual slot 1, no arguments: 0x004a46b0 / 0x004e6c70 end in a plain ret; the `push edi`
// before the call only saves edi), skips marked nodes (slot 2 Next with (node, 0), then slot 0 on the skipped one),
// locks the node it stops on and returns its value; 0 at the end.
uint32_t PMS_Iter_Begin(uint32_t list, uint32_t it)
{
    D(it) = list;
    while (D(D(it) + 0xc) != 0)
    {
        uint32_t l = D(it);
        uint32_t head = D(l + 0xc);
        if (By(head + 0x14) == 0 || D(head + 0x18) != 0)
            break;
        VC(l, 0, head);
    }
    uint32_t node = VC(D(it), 4);
    D(it + 4) = node;
    while (node != 0)
    {
        node = D(it + 4);
        if (By(node + 0x14) == 0)
        {
            D(D(it + 4) + 0x18) = D(D(it + 4) + 0x18) + 1;
            return D(D(it + 4) + 0xc);
        }
        uint32_t next = VC(D(it), 8, node, 0u);
        VC(D(it), 0, D(it + 4));
        D(it + 4) = next;
        node = next;
    }
    return 0;
}

static uint32_t PMS_Iter_Begin_Abi(uint32_t eax, uint32_t esi)
{
    return PMS_Iter_Begin(eax, esi);
}
ABI_ENTRY_004e7340(PMS_Iter_Begin_Entry, PMS_Iter_Begin_Abi)
HOOK(0x004e7340, "PMS_Iter_Begin", PMS_Iter_Begin_Entry, nullptr);

// 0x004e73c0 (edi = iterator): next. Unlocks the current node, steps with virtual slot 2 (Next, (node, 0)),
// removing marked nodes it passes (slot 0), locks the new node and returns its value; 0 at the end.
uint32_t PMS_Iter_Next(uint32_t it)
{
    uint32_t node = D(it + 4);
    if (node == 0)
        return 0;
    D(node + 0x18) = D(node + 0x18) - 1;
    uint32_t esi = VC(D(it), 8, D(it + 4), 0u);
    while (esi != 0)
    {
        if (By(esi + 0x14) == 0)
        {
            D(esi + 0x18) = D(esi + 0x18) + 1;
            break;
        }
        uint32_t next = VC(D(it), 8, esi, 0u);
        VC(D(it), 0, esi);
        esi = next;
    }
    D(it + 4) = esi;
    return esi ? D(esi + 0xc) : 0;
}

static uint32_t PMS_Iter_Next_Abi(uint32_t edi)
{
    return PMS_Iter_Next(edi);
}
ABI_ENTRY_004e73c0(PMS_Iter_Next_Entry, PMS_Iter_Next_Abi)
HOOK(0x004e73c0, "PMS_Iter_Next", PMS_Iter_Next_Entry, nullptr);

// 0x004e7420 (esi = iterator): end early: virtual slot 3 with the current node (0x00433e70 ret 4 for PMS_List,
// 0x004e6fe0 for PMS_Tree), unlocks the node and clears the iterator. Returns the node. Not executed in recorded
// runs.
uint32_t PMS_Iter_Stop(uint32_t it)
{
    VC(D(it), 0xc, D(it + 4));
    uint32_t node = D(it + 4);
    D(node + 0x18) = D(node + 0x18) - 1;
    D(it + 4) = 0;
    return node;
}

static uint32_t PMS_Iter_Stop_Abi(uint32_t esi)
{
    return PMS_Iter_Stop(esi);
}
ABI_ENTRY_004e7420(PMS_Iter_Stop_Entry, PMS_Iter_Stop_Abi)
HOOK(0x004e7420, "PMS_Iter_Stop", PMS_Iter_Stop_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// Second container (vtable 0x0058b0d0, constructor 0x004e7570: +4 pool, +8 first, +0xc count, +0x10 last / tree
// root, +0x14 comparator). Slots: 0 0x004e7590 red-black insert, 1 0x00407ff0 (empty), 2 0x004e7440 push,
// 3 0x004e7490 clear. Node: +0 / +4 links (+0 = "greater or equal" child, +4 = left child), +8 parent, +0xc value,
// +0x14 red flag (byte).
// ---------------------------------------------------------------------------------------------------------------

// 0x004e7440 (ecx = this; stack: value, callee pops): slot 2: takes a node from the pool (+4), stores the value,
// links it to the previous last node through +0 (+4 = 0), the first push also becomes +8; last (+0x10) = node,
// count++. Returns the node.
uint32_t PMS_Chain_Push(uint32_t self, uint32_t value)
{
    uint32_t node = TakeNode(D(self + 4));
    D(node + 0xc) = value;
    D(node + 4) = 0;
    uint32_t last = D(self + 0x10);
    if (last == 0)
        D(self + 8) = node;
    D(node) = last;
    D(self + 0x10) = node;
    D(self + 0xc) = D(self + 0xc) + 1;
    return node;
}

static uint32_t PMS_Chain_Push_Abi(uint32_t ecx, uint32_t a0)
{
    return PMS_Chain_Push(ecx, a0);
}
ABI_ENTRY_004e7440(PMS_Chain_Push_Entry, PMS_Chain_Push_Abi)
HOOK(0x004e7440, "PMS_Chain_Push", PMS_Chain_Push_Entry, nullptr);

// 0x004e7490 (ecx = this): slot 3: when a chain exists, splices it (last ... first via +0) onto the pool's free
// list; first, last and count become 0. Returns the pool when a chain existed, else 0 (the original leaves eax
// unchanged then). Not executed in recorded runs.
uint32_t PMS_Chain_Clear(uint32_t self)
{
    uint32_t r = 0;
    uint32_t last = D(self + 0x10);
    if (last != 0)
    {
        uint32_t pool = D(self + 4);
        D(D(self + 8)) = D(pool + 8);
        D(pool + 8) = last;
        r = pool;
    }
    D(self + 0x10) = 0;
    D(self + 8) = 0;
    D(self + 0xc) = 0;
    return r;
}

static uint32_t PMS_Chain_Clear_Abi(uint32_t ecx)
{
    return PMS_Chain_Clear(ecx);
}
ABI_ENTRY_004e7490(PMS_Chain_Clear_Entry, PMS_Chain_Clear_Abi)
HOOK(0x004e7490, "PMS_Chain_Clear", PMS_Chain_Clear_Entry, nullptr);

// 0x004e74c0 (ecx = this; stack: value, callee pops): binary-tree insert for the 0x0058b0d0 container: descends
// from the root (+0x10) with the comparator (+0x14, (value, node value): < 0 -> +4, else +0), takes a node from
// the pool (+4), stores value and parent, links it, count++. Returns the node. Caller: 0x004e7590.
uint32_t PMS_RBTree_InsertNode(uint32_t self, uint32_t value)
{
    uint32_t parent = 0;
    int32_t cmp = 0;
    uint32_t n = D(self + 0x10);
    while (n != 0)
    {
        parent = n;
        cmp = reinterpret_cast<int32_t(__cdecl*)(uint32_t, uint32_t)>(D(self + 0x14))(value, D(n + 0xc));
        n = (cmp < 0) ? D(n + 4) : D(n);
    }
    uint32_t node = TakeNode(D(self + 4));
    D(node + 0xc) = value;
    D(node + 8) = parent;
    D(node + 4) = 0;
    D(node) = 0;
    if (parent == 0)
        D(self + 0x10) = node;
    else if (cmp < 0)
        D(parent + 4) = node;
    else
        D(parent) = node;
    D(self + 0xc) = D(self + 0xc) + 1;
    return node;
}

static uint32_t PMS_RBTree_InsertNode_Abi(uint32_t ecx, uint32_t a0)
{
    return PMS_RBTree_InsertNode(ecx, a0);
}
ABI_ENTRY_004e74c0(PMS_RBTree_InsertNode_Entry, PMS_RBTree_InsertNode_Abi)
HOOK(0x004e74c0, "PMS_RBTree_InsertNode", PMS_RBTree_InsertNode_Entry, nullptr);

// 0x004e7590 (ecx = this; stack: value, callee pops): slot 0: red-black insert: 0x004e74c0, the node is red
// (+0x14 = 1), then the usual recolouring / rotations up the tree; the root ends black. Returns the last node
// looked at (eax as the original leaves it). Not executed in recorded runs.
uint32_t PMS_RBTree_Insert(uint32_t self, uint32_t value)
{
    uint32_t eax = PMS_RBTree_InsertNode(self, value);
    uint32_t ecx, edx, esi;
    By(eax + 0x14) = 1;
    if (eax == D(self + 0x10))
        goto L71c;
L5b0:
    ecx = D(eax + 8);
    if (By(ecx + 0x14) == 0)
        goto L71c;
    esi = D(ecx + 8);
    edx = D(esi + 4);
    if (ecx != edx)
        goto L680;
    edx = D(esi);
    if (edx != 0 && By(edx + 0x14) != 0)
        goto L5d5;
    if (eax == D(ecx))
    {
        eax = ecx;
        ecx = D(eax);
        D(eax) = D(ecx + 4);
        edx = D(ecx + 4);
        if (edx != 0)
            D(edx + 8) = eax;
        D(ecx + 8) = D(eax + 8);
        edx = D(eax + 8);
        if (edx == 0)
            D(self + 0x10) = ecx;
        else if (eax == D(edx + 4))
            D(edx + 4) = ecx;
        else
            D(edx) = ecx;
        D(ecx + 4) = eax;
        D(eax + 8) = ecx;
    }
    By(D(eax + 8) + 0x14) = 0;
    By(D(D(eax + 8) + 8) + 0x14) = 1;
    ecx = D(D(eax + 8) + 8);
    edx = D(ecx + 4);
    D(ecx + 4) = D(edx);
    esi = D(edx);
    if (esi != 0)
        D(esi + 8) = ecx;
    D(edx + 8) = D(ecx + 8);
    esi = D(ecx + 8);
    if (esi == 0)
        D(self + 0x10) = edx;
    else if (ecx == D(esi))
        D(esi) = edx;
    else
        D(esi + 4) = edx;
    D(edx) = ecx;
    D(ecx + 8) = edx;
    goto L712;
L5d5:
    By(ecx + 0x14) = 0;
    By(edx + 0x14) = 0;
    By(D(D(eax + 8) + 8) + 0x14) = 1;
    eax = D(D(eax + 8) + 8);
    goto L712;
L680:
    if (edx != 0 && By(edx + 0x14) != 0)
        goto L5d5;
    if (eax == D(ecx + 4))
    {
        eax = ecx;
        ecx = D(eax + 4);
        D(eax + 4) = D(ecx);
        edx = D(ecx);
        if (edx != 0)
            D(edx + 8) = eax;
        D(ecx + 8) = D(eax + 8);
        edx = D(eax + 8);
        if (edx == 0)
            D(self + 0x10) = ecx;
        else if (eax == D(edx))
            D(edx) = ecx;
        else
            D(edx + 4) = ecx;
        D(ecx) = eax;
        D(eax + 8) = ecx;
    }
    By(D(eax + 8) + 0x14) = 0;
    By(D(D(eax + 8) + 8) + 0x14) = 1;
    ecx = D(D(eax + 8) + 8);
    edx = D(ecx);
    D(ecx) = D(edx + 4);
    esi = D(edx + 4);
    if (esi != 0)
        D(esi + 8) = ecx;
    D(edx + 8) = D(ecx + 8);
    esi = D(ecx + 8);
    if (esi == 0)
        D(self + 0x10) = edx;
    else if (ecx == D(esi + 4))
        D(esi + 4) = edx;
    else
        D(esi) = edx;
    D(edx + 4) = ecx;
    D(ecx + 8) = edx;
L712:
    if (eax != D(self + 0x10))
        goto L5b0;
L71c:
    By(D(self + 0x10) + 0x14) = 0;
    return eax;
}

static uint32_t PMS_RBTree_Insert_Abi(uint32_t ecx, uint32_t a0)
{
    return PMS_RBTree_Insert(ecx, a0);
}
ABI_ENTRY_004e7590(PMS_RBTree_Insert_Entry, PMS_RBTree_Insert_Abi)
HOOK(0x004e7590, "PMS_RBTree_Insert", PMS_RBTree_Insert_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// Random numbers and vector math (0x0056c05c = 0.0f, 0x005bcb2c = 1.0f)
// ---------------------------------------------------------------------------------------------------------------

// 0x004e7730 (stack: n, caller pops): (rand() * n) >> 15 (_rand 0x005391cd; 32-bit imul, arithmetic shift): a
// random integer in [0, n).
uint32_t Rand_Below(uint32_t n)
{
    uint32_t r = static_cast<uint32_t>(orig::fn_005391cd());
    return static_cast<uint32_t>(static_cast<int32_t>(r * n) >> 15);
}

static uint32_t Rand_Below_Abi(uint32_t a0)
{
    return Rand_Below(a0);
}
ABI_ENTRY_004e7730(Rand_Below_Entry, Rand_Below_Abi)
HOOK(0x004e7730, "Rand_Below", Rand_Below_Entry, nullptr);

// 0x004e7740 (eax = high, edi = low): a random integer in [low, high]: ((rand() * (high - low + 1)) >> 15) + low.
// Not executed in recorded runs.
uint32_t Rand_Range(uint32_t high, uint32_t low)
{
    uint32_t r = static_cast<uint32_t>(orig::fn_005391cd());
    return static_cast<uint32_t>(static_cast<int32_t>(r * (high - low + 1)) >> 15) + low;
}

static uint32_t Rand_Range_Abi(uint32_t eax, uint32_t edi)
{
    return Rand_Range(eax, edi);
}
ABI_ENTRY_004e7740(Rand_Range_Entry, Rand_Range_Abi)
HOOK(0x004e7740, "Rand_Range", Rand_Range_Entry, nullptr);

// 0x004e7760 (eax = vec2): its length sqrt(x*x + y*y), returned in st0 (the ABI table lists no result; eax is left
// unchanged). Not executed in recorded runs.
float Vec2_Length(const float* v)
{
    float x = v[0];
    float y = v[1];
    return std::sqrt(x * x + y * y);
}

static float Vec2_Length_Abi(uint32_t eax)
{
    return Vec2_Length(reinterpret_cast<const float*>(eax));
}
ABI_ENTRY_004e7760(Vec2_Length_Entry, Vec2_Length_Abi)
HOOK(0x004e7760, "Vec2_Length", Vec2_Length_Entry, nullptr);

// 0x004e7780 (ecx = source vec2, edx = destination; returns st0): normalises a 2D vector and returns its length.
// A zero length logs "Zero length vector\n" (0x0058b04c, Log_Printf 0x004e0b30) and leaves the destination alone.
float Vec2_Normalize(const float* src, float* dst)
{
    float x = src[0];
    float y = src[1];
    float len = std::sqrt(x * x + y * y);
    if (len == GameGlobal<float>(0x0056c05c))
    {
        GameLog(reinterpret_cast<const char*>(0x0058b04c));
        return len;
    }
    float inv = GameGlobal<float>(0x005bcb2c) / len;
    dst[0] = inv * src[0];
    dst[1] = inv * src[1];
    return len;
}

static float Vec2_Normalize_Abi(uint32_t ecx, uint32_t edx)
{
    return Vec2_Normalize(reinterpret_cast<const float*>(ecx), reinterpret_cast<float*>(edx));
}
ABI_ENTRY_004e7780(Vec2_Normalize_Entry, Vec2_Normalize_Abi)
HOOK(0x004e7780, "Vec2_Normalize", Vec2_Normalize_Entry, nullptr);

// 0x004e77e0 (eax = a, ecx = b, edx = out): out = b - a (3D). Not executed in recorded runs.
void Vec3_Sub(const float* a, const float* b, float* out)
{
    out[0] = b[0] - a[0];
    out[1] = b[1] - a[1];
    out[2] = b[2] - a[2];
}

static void Vec3_Sub_Abi(uint32_t eax, uint32_t ecx, uint32_t edx)
{
    Vec3_Sub(reinterpret_cast<const float*>(eax), reinterpret_cast<const float*>(ecx), reinterpret_cast<float*>(edx));
}
ABI_ENTRY_004e77e0(Vec3_Sub_Entry, Vec3_Sub_Abi)
HOOK(0x004e77e0, "Vec3_Sub", Vec3_Sub_Entry, nullptr);

// 0x004e7800 (eax = vec3): its length sqrt((x*x + y*y) + z*z), returned in st0 (the ABI table lists no result).
float Vec3_Length(const float* v)
{
    float z = v[2];
    float y = v[1];
    float x = v[0];
    return std::sqrt(x * x + y * y + z * z);
}

static float Vec3_Length_Abi(uint32_t eax)
{
    return Vec3_Length(reinterpret_cast<const float*>(eax));
}
ABI_ENTRY_004e7800(Vec3_Length_Entry, Vec3_Length_Abi)
HOOK(0x004e7800, "Vec3_Length", Vec3_Length_Entry, nullptr);

// 0x004e7830 (eax = a, ecx = b, edx = out): out = b x a, i.e. (a.z*b.y - a.y*b.z, a.x*b.z - a.z*b.x,
// a.y*b.x - a.x*b.y); every input is read before the output is written. Returns eax = the bits of out.z.
uint32_t Vec3_CrossReversed(const float* a, const float* b, float* out)
{
    float x = a[2] * b[1] - a[1] * b[2];
    float y = a[0] * b[2] - a[2] * b[0];
    float z = a[1] * b[0] - a[0] * b[1];
    out[0] = x;
    out[1] = y;
    out[2] = z;
    uint32_t bits;
    std::memcpy(&bits, &z, 4);
    return bits;
}

static uint32_t Vec3_CrossReversed_Abi(uint32_t eax, uint32_t ecx, uint32_t edx)
{
    return Vec3_CrossReversed(reinterpret_cast<const float*>(eax), reinterpret_cast<const float*>(ecx),
                              reinterpret_cast<float*>(edx));
}
ABI_ENTRY_004e7830(Vec3_CrossReversed_Entry, Vec3_CrossReversed_Abi)
HOOK(0x004e7830, "Vec3_CrossReversed", Vec3_CrossReversed_Entry, nullptr);

// 0x004e7880 (ecx = source vec3, edx = destination): normalises a 3D vector (destination untouched when the length
// is 0) and returns the length in st0 (the ABI table lists eax; callers pop st0, src/ai/ai_06.cpp Normalize3).
float Vec3_Normalize(const float* src, float* dst)
{
    float z = src[2];
    float y = src[1];
    float x = src[0];
    float len = std::sqrt(x * x + y * y + z * z);
    if (len != GameGlobal<float>(0x0056c05c))
    {
        float inv = GameGlobal<float>(0x005bcb2c) / len;
        dst[0] = inv * src[0];
        dst[1] = inv * src[1];
        dst[2] = inv * src[2];
    }
    return len;
}

static float Vec3_Normalize_Abi(uint32_t ecx, uint32_t edx)
{
    return Vec3_Normalize(reinterpret_cast<const float*>(ecx), reinterpret_cast<float*>(edx));
}
ABI_ENTRY_004e7880(Vec3_Normalize_Entry, Vec3_Normalize_Abi)
HOOK(0x004e7880, "Vec3_Normalize", Vec3_Normalize_Entry, nullptr);

// 0x004e78d0 (ecx = source vec4, edx = destination): as Vec3_Normalize for four components; length in st0 (the
// ABI table lists eax). Not executed in recorded runs.
float Vec4_Normalize(const float* src, float* dst)
{
    float w = src[3];
    float z = src[2];
    float y = src[1];
    float x = src[0];
    float len = std::sqrt(x * x + y * y + z * z + w * w);
    if (len != GameGlobal<float>(0x0056c05c))
    {
        float inv = GameGlobal<float>(0x005bcb2c) / len;
        dst[0] = inv * src[0];
        dst[1] = inv * src[1];
        dst[2] = inv * src[2];
        dst[3] = inv * src[3];
    }
    return len;
}

static float Vec4_Normalize_Abi(uint32_t ecx, uint32_t edx)
{
    return Vec4_Normalize(reinterpret_cast<const float*>(ecx), reinterpret_cast<float*>(edx));
}
ABI_ENTRY_004e78d0(Vec4_Normalize_Entry, Vec4_Normalize_Abi)
HOOK(0x004e78d0, "Vec4_Normalize", Vec4_Normalize_Entry, nullptr);

// 0x004e7930 (stack: object, caller pops): repeatedly calls the callback +0x28 (cdecl) with object + 0x10, then
// advances the coordinate +0x10[i] by the step +0x18[i] (i = +0x20) and subtracts that step from the remaining
// amount +0xc, until it reaches 0. It is every entry of the default span table 0x005cac9c of 0x004e7970, whose span
// record (frame +0x48) has: +0xc span length, +0x10 / +0x14 coordinates, +0x18 / +0x1c steps, +0x20 axis,
// +0x28 per-pixel callback. Returns the last step. Not executed in recorded runs; no direct callers.
uint32_t Raster_SpanLoop(uint32_t self)
{
    uint32_t step;
    do
    {
        reinterpret_cast<void(__cdecl*)(uint32_t)>(D(self + 0x28))(self + 0x10);
        uint32_t i = D(self + 0x20);
        D(self + i * 4 + 0x10) = D(self + i * 4 + 0x10) + D(self + i * 4 + 0x18);
        step = D(self + D(self + 0x20) * 4 + 0x18);
        D(self + 0xc) = D(self + 0xc) - step;
    } while (D(self + 0xc) != 0);
    return step;
}

static uint32_t Raster_SpanLoop_Abi(uint32_t a0)
{
    return Raster_SpanLoop(a0);
}
ABI_ENTRY_004e7930(Raster_SpanLoop_Entry, Raster_SpanLoop_Abi)
HOOK(0x004e7930, "Raster_SpanLoop", Raster_SpanLoop_Entry, nullptr);

// 0x004e7970 (eax = per-pixel callback or 0, ecx = vertices, edx = user value; stack: flags, count, span table,
// caller pops; the ABI table lists 16 stack arguments, the code reads three): scan-converts a convex polygon of
// integer vertices {x, y} (fixed point, 5 fractional bits: >> 5; edges stepped in 13-bit fixed point) and calls
// span_table[flags] (cdecl, default table 0x005cac9c = Raster_SpanLoop) once per scanline with a span record built
// in its own stack frame at +0x48: {+0 user value, +4 flags, +8 2 (| 2 on the last span), +0xc length,
// +0x10 / +0x14 start coordinates, +0x18 / +0x1c steps (flags bits 1 and 2 negate them), +0x20 major axis
// (flags bit 0), +0x24 other axis, +0x28 per-pixel callback (default 0x00407ff0, an empty function)}. Name: guess
// from that behaviour. Recovered instruction by instruction: F is the original frame ([esp + o] with no call
// argument pushed; F + 0x78.. are the argument slots, which the original also reuses as locals), registers are
// variables. Returns the last span call's eax. Caller: 0x0048eab0.
uint32_t Raster_ConvexPolygon(uint32_t fn, uint32_t verts, uint32_t user, uint32_t flags, uint32_t count,
                              uint32_t table)
{
    alignas(4) uint8_t F[0x84] = {};
    auto FS = [&](int32_t o) -> int32_t& { return *reinterpret_cast<int32_t*>(F + o); };
    auto M = [](int32_t a) -> int32_t& { return *reinterpret_cast<int32_t*>(static_cast<uintptr_t>(static_cast<uint32_t>(a))); };
    auto Span = [&]() -> uint32_t {
        uint32_t entry = D(static_cast<uint32_t>(FS(0x80)) + static_cast<uint32_t>(FS(0x78)) * 4);
        return reinterpret_cast<uint32_t(__cdecl*)(void*)>(entry)(F + 0x48);
    };
    int32_t eax, ebx, ecx, edx, esi, edi, ebp;
    FS(0x78) = static_cast<int32_t>(flags);
    FS(0x7c) = static_cast<int32_t>(count);
    FS(0x80) = static_cast<int32_t>(table);
    ecx = static_cast<int32_t>(verts);

    edi = FS(0x80);
    FS(0x48) = static_cast<int32_t>(user);
    edx = FS(0x78);
    esi = static_cast<int32_t>(fn);
    eax = FS(0x7c);
    FS(0x4c) = edx;
    FS(0x50) = 2;
    if (edi == 0)
        FS(0x80) = 0x005cac9c;
    FS(0x70) = esi;
    if (esi == 0)
        FS(0x70) = 0x00407ff0;
    esi = edx & 1;
    ebx = (esi == 0) ? 1 : 0;
    FS(0x68) = esi;
    edi = ecx + 8;
    FS(0x2c) = ecx;
    ebp = ecx;
    esi = ebx;
    FS(0x6c) = esi;
    FS(0x60) = (edx & 2) ? -1 : 1;
    FS(0x64) = (edx & 4) ? -1 : 1;
    ebx = eax - 1;
    if (ebx != 0)
    {
        FS(0x44) = ebx;
        do
        {
            ebx = M(edi + esi * 4);
            if (ebx < M(ebp + esi * 4))
                ebp = edi;
            eax = FS(0x2c);
            if (ebx > M(eax + esi * 4))
                FS(0x2c) = edi;
            eax = FS(0x44);
            edi += 8;
            eax--;
            FS(0x44) = eax;
        } while (eax != 0);
        eax = FS(0x7c);
    }
    ebx = edx >> 1;
    ebx ^= edx;
    ebx >>= 1;
    ebx ^= edx;
    if (ebx & 1)
    {
        FS(0x34) = ecx;
        ecx = ecx + eax * 8 - 8;
        FS(0x38) = -1;
    }
    else
    {
        edx = ecx + eax * 8 - 8;
        FS(0x34) = edx;
        FS(0x38) = 1;
    }
    eax = 0;
    FS(0x3c) = ecx;
    FS(0x24) = 0;
    FS(0x20) = 0;
    if (FS(0x60 + esi * 4) > 0)
    {
        ecx = FS(0x2c);
        eax = ebp;
        FS(0x14) = ebp;
        FS(0x10) = eax;
        FS(0x28) = ecx;
    }
    else
    {
        eax = FS(0x2c);
        FS(0x14) = eax;
        FS(0x10) = eax;
        FS(0x28) = ebp;
    }
    ecx = M(eax + esi * 4);
    edx = FS(0x68);
    ebp = M(eax + edx * 4);
    edi = FS(0x80);
    ebx = FS(0x80);
    ecx >>= 5;
    FS(0x58 + esi * 4) = ecx;
    esi = FS(0x6c);
    ebp = static_cast<int32_t>(static_cast<uint32_t>(ebp) << 8);
    esi <<= 2;
    ecx = FS(0x60 + esi);
    edx = ebp;
    FS(0x18) = ebp;
    FS(0x7c) = edx;
    FS(0x30) = ecx;
    goto L_ae0;

L_ad4:
    edx = FS(0x7c);
L_ad8:
    eax = FS(0x10);
L_ae0:
    // left edge: advance to the next vertex with a different scanline
    ecx = FS(0x20);
    if (ecx != 0)
        goto L_bfb;
    if (eax == FS(0x28))
        goto L_bfb;
    edi = FS(0x68);
    FS(0x1c) = (FS(0x60 + edi * 4) < 0) ? 1 : 0;
    goto L_b10;
L_b09:
    eax = FS(0x10);
L_b10:
    FS(0x2c) = eax;
    if (eax == FS(0x3c))
        ecx = FS(0x34);
    else
        ecx = eax + (-FS(0x38)) * 8;
    FS(0x10) = ecx;
    ecx = FS(0x68);
    edi = M(eax + ecx * 4);
    ecx = FS(0x1c);
    edi = static_cast<int32_t>(static_cast<uint32_t>(edi) << 8);
    eax = (edi < edx) ? 1 : 0;
    eax ^= ecx;
    if (eax != 0)
        edx = edi;
    ecx = FS(0x10);
    eax = M(esi + ecx);
    ecx = FS(0x2c);
    ecx = M(esi + ecx);
    eax >>= 5;
    ecx >>= 5;
    eax -= ecx;
    FS(0x20) = eax;
    eax = FS(0x10);
    if (FS(0x20) == 0)
    {
        if (eax != FS(0x28))
            goto L_b09;
        FS(0x7c) = edx;
        goto L_bfb;
    }
    ebp = FS(0x2c);
    ecx = M(esi + ebp);
    FS(0x7c) = edx;
    edx = FS(0x68);
    eax = M(eax + edx * 4);
    eax -= M(ebp + edx * 4);
    edx = FS(0x10);
    ebp = M(esi + edx);
    eax = static_cast<int32_t>(static_cast<uint32_t>(eax) << 13);
    ebp -= ecx;
    eax = eax / ebp;
    ebp = eax;
    eax = FS(0x30);
    FS(0x40) = ebp;
    if (eax > 0)
    {
        edx = FS(0x1c);
        eax = ((ebp < 0) ? 1 : 0) ^ edx;
        if (eax == 0)
            goto L_be8;
        ecx = ~ecx;
        goto L_bd3;
    }
    eax = FS(0x1c);
    ebp = -ebp;
    edx = (ebp < 0) ? 1 : 0;
    FS(0x40) = ebp;
    edx ^= eax;
    if (edx == 0)
        goto L_be6;
L_bd3:
    ecx &= 0x1f;
    ecx *= ebp;
    ecx >>= 5;
    edi += ecx;
    edx = edi;
    FS(0x7c) = edx;
    goto L_bf7;
L_be6:
    ecx = ~ecx;
L_be8:
    edx = FS(0x7c);
    ecx &= 0x1f;
    ecx *= ebp;
    ecx >>= 5;
    edi -= ecx;
L_bf7:
    ebp = FS(0x18);
L_bfb:
    // right edge
    eax = FS(0x24);
    if (eax != 0)
        goto L_d25;
    eax = FS(0x28);
    if (FS(0x14) == eax)
        goto L_d25;
    eax = FS(0x68);
    ebx = FS(0x60 + eax * 4);
    FS(0x1c) = (ebx < 0) ? 1 : 0;
L_c28:
    eax = FS(0x14);
    FS(0x2c) = eax;
    if (eax == FS(0x34))
        ecx = FS(0x3c);
    else
        ecx = eax + FS(0x38) * 8;
    FS(0x14) = ecx;
    ecx = FS(0x68);
    ebx = M(eax + ecx * 4);
    ecx = FS(0x1c);
    ebx = static_cast<int32_t>(static_cast<uint32_t>(ebx) << 8);
    eax = ((ebx > ebp) ? 1 : 0) ^ ecx;
    if (eax != 0)
        ebp = ebx;
    ecx = FS(0x14);
    eax = M(esi + ecx);
    ecx = FS(0x2c);
    ecx = M(esi + ecx);
    eax >>= 5;
    ecx >>= 5;
    eax -= ecx;
    FS(0x24) = eax;
    if (eax == 0)
    {
        eax = FS(0x28);
        if (FS(0x14) != eax)
            goto L_c28;
        FS(0x18) = ebp;
        goto L_d25;
    }
    edx = FS(0x68);
    eax = FS(0x14);
    eax = M(eax + edx * 4);
    FS(0x18) = ebp;
    ebp = FS(0x2c);
    eax -= M(ebp + edx * 4);
    ecx = M(esi + ebp);
    edx = FS(0x14);
    esi = M(esi + edx);
    eax = static_cast<int32_t>(static_cast<uint32_t>(eax) << 13);
    esi -= ecx;
    eax = eax / esi;
    esi = eax;
    eax = FS(0x30);
    FS(0x44) = esi;
    if (eax > 0)
    {
        edx = FS(0x1c);
        eax = ((esi > 0) ? 1 : 0) ^ edx;
        edx = FS(0x7c);
        if (eax == 0)
            goto L_d16;
        ecx = ~ecx;
        ecx &= 0x1f;
        ecx *= esi;
        ecx >>= 5;
        ebx += ecx;
        ebp = ebx;
        goto L_d25;
    }
    {
        eax = FS(0x1c);
        esi = -esi;
        edx = (esi > 0) ? 1 : 0;
        FS(0x44) = esi;
        edx ^= eax;
        bool zero = (edx == 0);
        edx = FS(0x7c);
        if (zero)
            goto L_d14;
        ecx &= 0x1f;
        ecx *= esi;
        ecx >>= 5;
        ebx += ecx;
        ebp = ebx;
        goto L_d25;
    }
L_d14:
    ecx = ~ecx;
L_d16:
    ebp = FS(0x18);
    ecx &= 0x1f;
    ecx *= esi;
    ecx >>= 5;
    ebx -= ecx;
L_d25:
    // emit spans for the shorter of the two edge runs
    eax = FS(0x30);
    esi = FS(0x24);
    ecx = FS(0x20);
    if (eax > 0)
    {
        if (ecx < esi)
            FS(0x7c) = ecx;
        else
            FS(0x7c) = esi;
    }
    else
    {
        if (ecx <= esi)
            FS(0x7c) = esi;
        else
            FS(0x7c) = ecx;
    }
    eax = FS(0x7c);
    if (eax == 0)
        goto L_e7a;
    ecx -= eax;
    esi -= eax;
    FS(0x20) = ecx;
    FS(0x24) = esi;
    do
    {
        eax = FS(0x68);
        edx >>= 13;
        FS(0x58 + eax * 4) = edx;
        ecx = FS(0x68);
        eax = ebp;
        ebp = FS(0x58 + ecx * 4);
        eax >>= 13;
        eax -= ebp;
        FS(0x54) = eax;
        eax += FS(0x60 + ecx * 4);
        FS(0x54) = eax;
        Span();
        eax = FS(0x40);
        ecx = FS(0x6c);
        esi = FS(0x58 + ecx * 4);
        ebp = FS(0x44);
        edi += eax;
        int32_t slot = 0x58 + ecx * 4;
        ecx = FS(0x60 + ecx * 4);
        esi += ecx;
        ecx = FS(0x7c);
        FS(slot) = esi;
        esi = FS(0x6c);
        esi <<= 2;
        eax = FS(0x60 + esi);
        ebx += ebp;
        ecx -= eax;
        edx = edi;
        ebp = ebx;
        FS(0x30) = eax;
        FS(0x7c) = ecx;
    } while (ecx != 0);
    eax = FS(0x20);
    FS(0x18) = ebp;
    FS(0x7c) = edx;
    if (eax == 0)
    {
        eax = FS(0x68);
        ecx = FS(0x60 + eax * 4);
        eax = FS(0x40);
        if ((((ecx < 0) ? 1 : 0) ^ ((eax < 0) ? 1 : 0)) != 0)
        {
            edx = FS(0x10);
            eax = FS(0x68);
            edx = M(edx + eax * 4);
            edx = static_cast<int32_t>(static_cast<uint32_t>(edx) << 8);
            FS(0x7c) = edx;
        }
        else
        {
            edx = FS(0x7c);
        }
    }
    eax = FS(0x24);
    if (eax != 0)
        goto L_ad8;
    {
        int32_t axis = FS(0x68);
        eax = FS(0x60 + axis * 4);
        int32_t step = FS(0x44);
        if ((((eax < 0) ? 1 : 0) ^ ((step > 0) ? 1 : 0)) != 0)
        {
            ecx = FS(0x14);
            ebp = M(ecx + axis * 4);
            ebp = static_cast<int32_t>(static_cast<uint32_t>(ebp) << 8);
            FS(0x18) = ebp;
        }
        edx = step;  // edx holds [esp+0x44] here; L_ad4 reloads it
    }
    goto L_ad4;

L_e7a:
    // last span
    eax = FS(0x68);
    edx >>= 13;
    FS(0x58 + eax * 4) = edx;
    eax = FS(0x68);
    ebx = FS(0x58 + eax * 4);
    esi = FS(0x50);
    ebp >>= 13;
    ebp -= ebx;
    FS(0x54) = ebp;
    edi = FS(0x60 + eax * 4);
    ebp += edi;
    esi |= 2;
    FS(0x54) = ebp;
    FS(0x50) = esi;
    return Span();
}

static uint32_t Raster_ConvexPolygon_Abi(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t a0, uint32_t a1,
                                         uint32_t a2, uint32_t /*a3*/, uint32_t /*a4*/, uint32_t /*a5*/,
                                         uint32_t /*a6*/, uint32_t /*a7*/, uint32_t /*a8*/, uint32_t /*a9*/,
                                         uint32_t /*a10*/, uint32_t /*a11*/, uint32_t /*a12*/, uint32_t /*a13*/,
                                         uint32_t /*a14*/, uint32_t /*a15*/)
{
    return Raster_ConvexPolygon(eax, ecx, edx, a0, a1, a2);
}
ABI_ENTRY_004e7970(Raster_ConvexPolygon_Entry, Raster_ConvexPolygon_Abi)
HOOK(0x004e7970, "Raster_ConvexPolygon", Raster_ConvexPolygon_Entry, nullptr);

// 0x004e7ed0 (ecx = source vec3 array; stack: destination, count, caller pops): converts the x, y of each vertex
// to fixed point (x 32.0 at 0x005bcbbc, truncated by __ftol2 0x0056a7c8; y converted first) into {int x, int y}
// pairs. Returns the count. Caller: 0x0048eab0 (feeds Raster_ConvexPolygon).
uint32_t Raster_ToFixed(const float* src, int32_t* dst, int32_t count)
{
    for (int32_t i = 0; i < count; ++i)
    {
        int32_t y = static_cast<int32_t>(src[1] * GameGlobal<float>(0x005bcbbc));
        int32_t x = static_cast<int32_t>(src[0] * GameGlobal<float>(0x005bcbbc));
        dst[i * 2] = x;
        dst[i * 2 + 1] = y;
        src += 3;
    }
    return static_cast<uint32_t>(count);
}

static uint32_t Raster_ToFixed_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return Raster_ToFixed(reinterpret_cast<const float*>(ecx), reinterpret_cast<int32_t*>(a0),
                          static_cast<int32_t>(a1));
}
ABI_ENTRY_004e7ed0(Raster_ToFixed_Entry, Raster_ToFixed_Abi)
HOOK(0x004e7ed0, "Raster_ToFixed", Raster_ToFixed_Entry, nullptr);

// 0x004e7f30 (eax = count, ecx = vertices (vec3), edx = output, edi = plane {n.x, n.y, n.z, d}): clips a polygon
// against a plane (Sutherland-Hodgman): keeps vertices with distance >= 0 (not < 0) and inserts the crossing point
// prev + (cur - prev) * t, t = d_prev / (d_prev - d_cur), where the sign changes. Returns the output count.
uint32_t Poly_ClipPlane(uint32_t count, uint32_t v, uint32_t out, const float* n)
{
    uint32_t prev = v + count * 12 - 12;
    float dPrev = Fl(prev + 8) * n[2] + Fl(prev + 4) * n[1] + Fl(prev) * n[0] + n[3];
    uint32_t written = 0;
    float zero = GameGlobal<float>(0x0056c05c);
    for (uint32_t k = count; k != 0; --k)
    {
        float dCur = Fl(v) * n[0] + Fl(v + 4) * n[1] + Fl(v + 8) * n[2] + n[3];
        bool prevNeg = dPrev < zero;
        bool curNeg = dCur < zero;
        if (prevNeg != curNeg)
        {
            float t = dPrev / (dPrev - dCur);
            float s = GameGlobal<float>(0x005bcb2c) - t;
            Fl(out) = t * Fl(v) + s * Fl(prev);
            Fl(out + 4) = s * Fl(prev + 4) + t * Fl(v + 4);
            Fl(out + 8) = s * Fl(prev + 8) + t * Fl(v + 8);
            out += 12;
            ++written;
        }
        if (!(dCur < zero) && dCur == dCur)
        {
            D(out) = D(v);
            D(out + 4) = D(v + 4);
            D(out + 8) = D(v + 8);
            out += 12;
            ++written;
        }
        dPrev = dCur;
        prev = v;
        v += 12;
    }
    return written;
}

static uint32_t Poly_ClipPlane_Abi(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t edi)
{
    return Poly_ClipPlane(eax, ecx, edx, reinterpret_cast<const float*>(edi));
}
ABI_ENTRY_004e7f30(Poly_ClipPlane_Entry, Poly_ClipPlane_Abi)
HOOK(0x004e7f30, "Poly_ClipPlane", Poly_ClipPlane_Entry, nullptr);

// 0x004e8040 (eax = a, ecx = b): 2D distance |a - b| in st0 (the ABI table lists no result). Not executed in
// recorded runs.
float Vec2_Distance(const float* a, const float* b)
{
    float dy = a[1] - b[1];
    float dx = a[0] - b[0];
    return std::sqrt(dx * dx + dy * dy);
}

static float Vec2_Distance_Abi(uint32_t eax, uint32_t ecx)
{
    return Vec2_Distance(reinterpret_cast<const float*>(eax), reinterpret_cast<const float*>(ecx));
}
ABI_ENTRY_004e8040(Vec2_Distance_Entry, Vec2_Distance_Abi)
HOOK(0x004e8040, "Vec2_Distance", Vec2_Distance_Entry, nullptr);

// 0x004e8060 (eax = a, ecx = b): squared 2D distance in st0 (the ABI table lists no result).
float Vec2_DistanceSq(const float* a, const float* b)
{
    float dy = a[1] - b[1];
    float dx = a[0] - b[0];
    return dx * dx + dy * dy;
}

static float Vec2_DistanceSq_Abi(uint32_t eax, uint32_t ecx)
{
    return Vec2_DistanceSq(reinterpret_cast<const float*>(eax), reinterpret_cast<const float*>(ecx));
}
ABI_ENTRY_004e8060(Vec2_DistanceSq_Entry, Vec2_DistanceSq_Abi)
HOOK(0x004e8060, "Vec2_DistanceSq", Vec2_DistanceSq_Entry, nullptr);

// 0x004e8080 (eax = a, ecx = b): squared 3D distance (dx*dx + dy*dy) + dz*dz in st0 (the ABI table lists no
// result).
float Vec3_DistanceSq(const float* a, const float* b)
{
    float dz = a[2] - b[2];
    float dy = a[1] - b[1];
    float dx = a[0] - b[0];
    return dx * dx + dy * dy + dz * dz;
}

static float Vec3_DistanceSq_Abi(uint32_t eax, uint32_t ecx)
{
    return Vec3_DistanceSq(reinterpret_cast<const float*>(eax), reinterpret_cast<const float*>(ecx));
}
ABI_ENTRY_004e8080(Vec3_DistanceSq_Entry, Vec3_DistanceSq_Abi)
HOOK(0x004e8080, "Vec3_DistanceSq", Vec3_DistanceSq_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// Angles (degrees; 0x005bcb88 = 360, 0x005bcbb8 = 180, 0x005bcbb4 = -180, 0x005bcb84 = 359.99, 0x005bcb90 = 90,
// 0x005bcb8c = 57.29578) and the trigonometric tables. x87-only operations (fprem via _CIfmod, fsin, fpatan, the
// CRT's _CI function 0x00566ad0) go through asm helpers so their extended-precision results are used exactly as
// the original uses them (the game runs the FPU at 24-bit precision, which rounds fadd/fsub/fmul/fdiv/fsqrt but not
// fprem/fsin/fpatan).
// ---------------------------------------------------------------------------------------------------------------

namespace
{
// _CIfmod 0x0056712a: fmod(a, 360.0 (double 0x005bcb98)), exact, returned as double.
double Fmod360(float a)
{
    static const uint32_t target = 0x0056712a;
    double r;
    __asm {
        push ebx
        push esi
        push edi
        push ebp
        fld dword ptr [a]
        fld qword ptr ds:[0x005bcb98]
        call dword ptr [target]
        pop ebp
        pop edi
        pop esi
        pop ebx
        fstp qword ptr [r]
    }
    return r;
}
// fild i; fmul k; fsin; (fdivr one); fstp float.
float SinOf(int32_t i, float k)
{
    float r;
    __asm {
        fild dword ptr [i]
        fmul dword ptr [k]
        fsin
        fstp dword ptr [r]
    }
    return r;
}
float InvSinOf(int32_t i, float k, float one)
{
    float r;
    __asm {
        fild dword ptr [i]
        fmul dword ptr [k]
        fsin
        fdivr dword ptr [one]
        fstp dword ptr [r]
    }
    return r;
}
// fild i; fmul k; fsub one; call 0x00566ad0 (a CRT _CI function taking and returning st0); fstp float. Also returns
// the eax it leaves.
float CrtTableValue(int32_t i, float k, float one, uint32_t* eax_out)
{
    static const uint32_t target = 0x00566ad0;
    float r;
    uint32_t e;
    __asm {
        push ebx
        push esi
        push edi
        push ebp
        fild dword ptr [i]
        fmul dword ptr [k]
        fsub dword ptr [one]
        call dword ptr [target]
        pop ebp
        pop edi
        pop esi
        pop ebx
        fstp dword ptr [r]
        mov e, eax
    }
    *eax_out = e;
    return r;
}
// fld y; fld x; fpatan; fmul k; fstp float (the product is already rounded to 24 bits).
float AtanTimes(float y, float x, float k)
{
    float r;
    __asm {
        fld dword ptr [y]
        fld dword ptr [x]
        fpatan
        fmul dword ptr [k]
        fstp dword ptr [r]
    }
    return r;
}
}  // namespace

// 0x004e81a0 (stack: a, b, caller pops; returns st0): the absolute angle between two headings in degrees:
// d = a - b, + 360 when negative, - 360 when above 360, then 360 - d when above 180. Not executed in recorded runs.
float Angle_AbsDiff(float a, float b)
{
    float d = a - b;
    if (d < GameGlobal<float>(0x0056c05c))
        d = d + GameGlobal<float>(0x005bcb88);
    if (d > GameGlobal<float>(0x005bcb88))
        d = d - GameGlobal<float>(0x005bcb88);
    if (d > GameGlobal<float>(0x005bcbb8))
        d = GameGlobal<float>(0x005bcb88) - d;
    return d;
}

static float Angle_AbsDiff_Abi(uint32_t a0, uint32_t a1)
{
    float a, b;
    std::memcpy(&a, &a0, 4);
    std::memcpy(&b, &a1, 4);
    return Angle_AbsDiff(a, b);
}
ABI_ENTRY_004e81a0(Angle_AbsDiff_Entry, Angle_AbsDiff_Abi)
HOOK(0x004e81a0, "Angle_AbsDiff", Angle_AbsDiff_Entry, nullptr);

// 0x004e81f0 (stack: a, b, caller pops; returns st0): the signed angle a - b in (-180, 180]: both headings are
// wrapped into [0, 360) with _CIfmod (+ 360 when negative; a is stored back as float, b stays on the FPU stack),
// then the difference is shifted by 360 when above 180 or below -180.
float Angle_Diff(float a, float b)
{
    float zero = GameGlobal<float>(0x0056c05c);
    float full = GameGlobal<float>(0x005bcb88);
    if (a < zero)
        a = static_cast<float>(Fmod360(a) + full);
    else if (!(a < full) && a == a)
        a = static_cast<float>(Fmod360(a));
    double bb = b;
    if (b < zero)
        bb = Fmod360(b) + full;
    else if (!(b < full) && b == b)
        bb = Fmod360(b);
    float d = static_cast<float>(a - bb);
    if (d > GameGlobal<float>(0x005bcbb8))
        return d - full;
    if (d < GameGlobal<float>(0x005bcbb4))
        d = d + full;
    return d;
}

static float Angle_Diff_Abi(uint32_t a0, uint32_t a1)
{
    float a, b;
    std::memcpy(&a, &a0, 4);
    std::memcpy(&b, &a1, 4);
    return Angle_Diff(a, b);
}
ABI_ENTRY_004e81f0(Angle_Diff_Entry, Angle_Diff_Abi)
HOOK(0x004e81f0, "Angle_Diff", Angle_Diff_Entry, nullptr);

// 0x004e82b0 (ecx, edx, esi = pointers to the y, z, x components, three separate floats): normalises the vector
// (x = [esi], y = [ecx], z = [edx]; sum of squares (x*x + y*y) + z*z); when the sum is below 1e-4 (0x005bcbb0) or
// NaN all three become 0. Returns 0 (the original leaves the x87 status word in ax; caller 0x00493d00).
uint32_t Vec3_NormalizeParts(float* y, float* z, float* x)
{
    float zz = *z;
    float yy = *y;
    float xx = *x;
    float sum = xx * xx + yy * yy + zz * zz;
    if (!(sum >= GameGlobal<float>(0x005bcbb0)))
    {
        *reinterpret_cast<uint32_t*>(y) = 0;
        *reinterpret_cast<uint32_t*>(z) = 0;
        *reinterpret_cast<uint32_t*>(x) = 0;
        return 0;
    }
    float inv = GameGlobal<float>(0x005bcb2c) / std::sqrt(sum);
    *x = inv * *x;
    *z = inv * *z;
    *y = inv * *y;
    return 0;
}

static uint32_t Vec3_NormalizeParts_Abi(uint32_t ecx, uint32_t edx, uint32_t esi)
{
    return Vec3_NormalizeParts(reinterpret_cast<float*>(ecx), reinterpret_cast<float*>(edx),
                               reinterpret_cast<float*>(esi));
}
ABI_ENTRY_004e82b0(Vec3_NormalizeParts_Entry, Vec3_NormalizeParts_Abi)
HOOK(0x004e82b0, "Vec3_NormalizeParts", Vec3_NormalizeParts_Entry, nullptr);

// 0x004e8310 (ecx unused: `push ecx` reserves the loop counter slot): builds the trigonometric tables:
// sin(i * 2pi/16384) for i < 0x5000 at 0x00614b08 (0x00614b00 = 0x00618b08, the cosine view a quarter turn on),
// 1 / sin at 0x00628b08 (i < 0x4000), and the CRT function 0x00566ad0 (acos or asin, guess) of i * 2/16383 - 1 at
// 0x00604b00 (i < 0x4000). Returns the eax the last CRT call left.
uint32_t Trig_InitTables(uint32_t /*ecx*/)
{
    float k = GameGlobal<float>(0x005bcbac);
    float one = GameGlobal<float>(0x005bcb2c);
    for (int32_t i = 0; i < 0x5000; ++i)
        GameGlobal<float>(0x00614b08 + i * 4) = SinOf(i, k);
    GameGlobal<uint32_t>(0x00614b00) = 0x00618b08;
    for (int32_t i = 0; i < 0x4000; ++i)
        GameGlobal<float>(0x00628b08 + i * 4) = InvSinOf(i, k, one);
    float k2 = GameGlobal<float>(0x005bcba8);
    uint32_t eax = 0;
    for (int32_t i = 0; i < 0x4000; ++i)
        GameGlobal<float>(0x00604b00 + i * 4) = CrtTableValue(i, k2, one, &eax);
    return eax;
}

static uint32_t Trig_InitTables_Abi(uint32_t ecx)
{
    return Trig_InitTables(ecx);
}
ABI_ENTRY_004e8310(Trig_InitTables_Entry, Trig_InitTables_Abi)
HOOK(0x004e8310, "Trig_InitTables", Trig_InitTables_Entry, nullptr);

// 0x004e83b0 (ecx = vec2; returns st0): the heading of a 2D vector in degrees: 90 for (0, 0), else
// atan2(y, x) * 57.29578 + 90 wrapped into [0, 360) (- 360 above 360, + 360 below 0), and 0 above 359.99.
float Vec2_Heading(const float* v)
{
    float zero = GameGlobal<float>(0x0056c05c);
    if (v[0] == zero && v[1] == zero)
        return GameGlobal<float>(0x005bcb90);
    float a = AtanTimes(v[1], v[0], GameGlobal<float>(0x005bcb8c));
    a = a + GameGlobal<float>(0x005bcb90);
    if (a > GameGlobal<float>(0x005bcb88))
        a = a - GameGlobal<float>(0x005bcb88);
    if (a < zero)
        a = a + GameGlobal<float>(0x005bcb88);
    if (a > GameGlobal<float>(0x005bcb84))
        a = zero;
    return a;
}

static float Vec2_Heading_Abi(uint32_t ecx)
{
    return Vec2_Heading(reinterpret_cast<const float*>(ecx));
}
ABI_ENTRY_004e83b0(Vec2_Heading_Entry, Vec2_Heading_Abi)
HOOK(0x004e83b0, "Vec2_Heading", Vec2_Heading_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// 0x004f0be0 .. 0x004f0c90: name-directory helpers (record {+8 index, +0x10 entry table, +0x18 key}; entry: +2 / +4
// u16 offsets, the name at entry + 4 + [+2]). Not executed in recorded runs.
// ---------------------------------------------------------------------------------------------------------------

// 0x004f0be0 (stack: record, caller pops): key u16 minus the u16 at entry + [entry+4] + 2 of entry table[index]
// (a comparison callback, guess). No direct callers.
uint32_t fn_004f0be0(uint32_t rec)
{
    uint32_t entry = D(D(rec + 0x10) + D(rec + 8) * 4);
    uint32_t off = W(entry + 4);
    uint32_t v = W(off + entry + 2);
    return W(D(rec + 0x18)) - v;
}

static uint32_t fn_004f0be0_Abi(uint32_t a0)
{
    return fn_004f0be0(a0);
}
ABI_ENTRY_004f0be0(fn_004f0be0_Entry, fn_004f0be0_Abi)
HOOK(0x004f0be0, "fn_004f0be0", fn_004f0be0_Entry, nullptr);

// 0x004f0c00 (stack: record, caller pops): strcmp-style comparison (signed chars) of the key string +0x18 with the
// name of entry table[index] (entry + [entry+2] + 4). Returns the first difference or 0 (a comparison callback,
// guess). No direct callers.
uint32_t fn_004f0c00(uint32_t rec)
{
    uint32_t key = D(rec + 0x18);
    uint32_t entry = D(D(rec + 0x10) + D(rec + 8) * 4);
    uint32_t name = W(entry + 2) + entry + 4;
    int32_t c, diff;
    do
    {
        c = static_cast<signed char>(By(key));
        int32_t n = static_cast<signed char>(By(name));
        diff = c - n;
        ++key;
        ++name;
        if (diff != 0)
            return static_cast<uint32_t>(diff);
    } while (c != 0);
    return 0;
}

static uint32_t fn_004f0c00_Abi(uint32_t a0)
{
    return fn_004f0c00(a0);
}
ABI_ENTRY_004f0c00(fn_004f0c00_Entry, fn_004f0c00_Abi)
HOOK(0x004f0c00, "fn_004f0c00", fn_004f0c00_Entry, nullptr);

// 0x004f0c90 (eax = owner; stack: key, caller pops): d = owner->+4; when d+0x1c is set and the count d+0x20 is
// nonzero, fills the search record d+0x24 (+0 = d+0x1c, +0x18 = key), runs the search 0x004e0f40 (esi = record) and
// returns entry table[index] when the record's +0xc (not found, guess) is 0; else 0. Not executed in recorded runs.
uint32_t fn_004f0c90(uint32_t owner, uint32_t key)
{
    uint32_t d = D(owner + 4);
    uint32_t list = D(d + 0x1c);
    if (list == 0 || D(d + 0x20) == 0)
        return 0;
    uint32_t rec = D(d + 0x24);
    D(rec) = list;
    D(rec + 0x18) = key;
    orig::fn_004e0f40(rec);
    if (D(rec + 0xc) != 0)
        return 0;
    return D(D(rec + 0x10) + D(rec + 8) * 4);
}

static uint32_t fn_004f0c90_Abi(uint32_t eax, uint32_t a0)
{
    return fn_004f0c90(eax, a0);
}
ABI_ENTRY_004f0c90(fn_004f0c90_Entry, fn_004f0c90_Abi)
HOOK(0x004f0c90, "fn_004f0c90", fn_004f0c90_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// Hash table {+0 bucket count, +4 hash seed, +8 value size, +0xc byte "loaded in place", +0x10 compare (cdecl:
// context, key, entry key; nonzero = match), +0x14 compare context, +0x1c buckets}; entry {+0 next, +4 key/value}.
// ---------------------------------------------------------------------------------------------------------------

// 0x004f1100 (edx = string, edi = table): case-insensitive string hash: sum of (seed ^ lower-cased signed char)
// modulo the bucket count (unsigned).
uint32_t HashTable_HashString(const char* s, uint32_t table)
{
    uint32_t h = 0;
    for (; *s != 0; ++s)
    {
        signed char c = static_cast<signed char>(*s);
        int32_t v = (c >= 0x41 && c <= 0x5a) ? c + 0x20 : c;
        h += D(table + 4) ^ static_cast<uint32_t>(v);
    }
    return h % D(table);
}

static uint32_t HashTable_HashString_Abi(uint32_t edx, uint32_t edi)
{
    return HashTable_HashString(reinterpret_cast<const char*>(edx), edi);
}
ABI_ENTRY_004f1100(HashTable_HashString_Entry, HashTable_HashString_Abi)
HOOK(0x004f1100, "HashTable_HashString", HashTable_HashString_Entry, nullptr);

uint32_t HashTable_Find(uint32_t table, uint32_t key);

// 0x004f1140 (eax = key string; stack: table, callee pops): the dword stored at the found entry's +4, or -1.
// The original swaps the operands for HashTable_Find: `push eax` (key) then `mov eax, [esp+8]` (table)
// (caller 0x00489eef: eax = string, push [esi+0xc] = table).
uint32_t HashTable_FindValue(uint32_t table, uint32_t key)
{
    uint32_t p = HashTable_Find(table, key);
    return p ? D(p) : 0xffffffffu;
}

static uint32_t HashTable_FindValue_Abi(uint32_t eax, uint32_t a0)
{
    return HashTable_FindValue(a0, eax);
}
ABI_ENTRY_004f1140(HashTable_FindValue_Entry, HashTable_FindValue_Abi)
HOOK(0x004f1140, "HashTable_FindValue", HashTable_FindValue_Entry, nullptr);

// 0x004f1160 (eax = table; stack: key string, callee pops): returns entry + 4 of the first entry in the key's bucket
// (HashTable_HashString) that the compare callback +0x10 matches, else 0; 0 when there is no callback.
uint32_t HashTable_Find(uint32_t table, uint32_t key)
{
    if (D(table + 0x10) == 0)
        return 0;
    uint32_t idx = HashTable_HashString(reinterpret_cast<const char*>(key), table);
    for (uint32_t e = D(D(table + 0x1c) + idx * 4); e != 0; e = D(e))
    {
        auto cmp = reinterpret_cast<uint32_t(__cdecl*)(uint32_t, uint32_t, uint32_t)>(D(table + 0x10));
        if (cmp(D(table + 0x14), key, e + 4) != 0)
            return e + 4;
    }
    return 0;
}

static uint32_t HashTable_Find_Abi(uint32_t eax, uint32_t a0)
{
    return HashTable_Find(eax, a0);
}
ABI_ENTRY_004f1160(HashTable_Find_Entry, HashTable_Find_Abi)
HOOK(0x004f1160, "HashTable_Find", HashTable_Find_Entry, nullptr);

// 0x004f11c0 (ebx = table): frees every entry of every bucket (PMS_heapFree 0x004e08d0, heap 0x0063a210: the
// entry's +4 pointer when set, then the entry). Returns the bucket count (eax as the original leaves it).
// Not executed in recorded runs.
uint32_t HashTable_FreeAll(uint32_t table)
{
    for (int32_t i = 0; i < I(table); ++i)
    {
        while (D(D(table + 0x1c) + i * 4) != 0)
        {
            uint32_t slot = D(table + 0x1c) + i * 4;
            uint32_t e = D(slot);
            D(slot) = D(e);
            if (D(e + 4) != 0)
                orig::fn_004e08d0(kPmsHeap, D(e + 4));
            orig::fn_004e08d0(kPmsHeap, e);
        }
    }
    return D(table);
}

static uint32_t HashTable_FreeAll_Abi(uint32_t ebx)
{
    return HashTable_FreeAll(ebx);
}
ABI_ENTRY_004f11c0(HashTable_FreeAll_Entry, HashTable_FreeAll_Abi)
HOOK(0x004f11c0, "HashTable_FreeAll", HashTable_FreeAll_Entry, nullptr);

// 0x004f1220 (eax = serialised data, ecx = table): sets the table up in place over a serialised image: +0xc = 1,
// bucket count / seed / value size from the first three dwords, buckets = data + 12; bucket and next-entry indices
// (-1 = none) become pointers (entries of value size + 4 bytes follow the bucket array; chains are contiguous).
// Returns the number of bytes consumed.
uint32_t HashTable_LoadInPlace(uint32_t data, uint32_t table)
{
    uint32_t start = data;
    By(table + 0xc) = 1;
    D(table) = D(data);
    D(table + 4) = D(data + 4);
    D(table + 8) = D(data + 8);
    uint32_t p = data + 12;
    D(table + 0x1c) = p;
    for (int32_t i = 0; i < I(table); ++i)
    {
        uint32_t slot = D(table + 0x1c) + i * 4;
        int32_t idx = I(slot);
        if (idx == -1)
            D(slot) = 0;
        else
            D(slot) = p + (D(table + 8) + 4) * static_cast<uint32_t>(idx) + D(table) * 4;
    }
    uint32_t e = p + D(table) * 4;
    for (int32_t i = 0; i < I(table); ++i)
    {
        if (D(D(table + 0x1c) + i * 4) == 0)
            continue;
        uint32_t cur;
        do
        {
            uint32_t next = e + D(table + 8) + 4;
            D(e) = (I(e) == -1) ? 0 : next;
            cur = e;
            e = e + D(table + 8) + 4;
        } while (D(cur) != 0);
    }
    return e - start;
}

static uint32_t HashTable_LoadInPlace_Abi(uint32_t eax, uint32_t ecx)
{
    return HashTable_LoadInPlace(eax, ecx);
}
ABI_ENTRY_004f1220(HashTable_LoadInPlace_Entry, HashTable_LoadInPlace_Abi)
HOOK(0x004f1220, "HashTable_LoadInPlace", HashTable_LoadInPlace_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// Quaternions {x, y, z, w} (0x005bcb4c = 0.5, 0x005bcb5c = -1, 0x005bcbb0 = 1e-4, 0x005bcc88 = FLT_EPSILON)
// ---------------------------------------------------------------------------------------------------------------

namespace
{
// fld y; fld x; fpatan; fstp float.
float AtanF(float y, float x)
{
    float r;
    __asm {
        fld dword ptr [y]
        fld dword ptr [x]
        fpatan
        fstp dword ptr [r]
    }
    return r;
}
// fld x; fsin; fmul m; fstp float (the product is rounded to 24 bits).
float SinMulF(float x, float m)
{
    float r;
    __asm {
        fld dword ptr [x]
        fsin
        fmul dword ptr [m]
        fstp dword ptr [r]
    }
    return r;
}
}  // namespace

// 0x004f12e0 (ecx = source, edx = destination): normalises a quaternion; a zero length gives the identity
// (0, 0, 0, 1). Returns 0 (the original leaves the x87 status word in ax).
uint32_t Quat_Normalize(const float* q, float* out)
{
    float w = q[3];
    float z = q[2];
    float y = q[1];
    float x = q[0];
    float len = std::sqrt(x * x + y * y + z * z + w * w);
    if (len == GameGlobal<float>(0x0056c05c))
    {
        *reinterpret_cast<uint32_t*>(&out[3]) = 0x3f800000;
        *reinterpret_cast<uint32_t*>(&out[0]) = 0;
        *reinterpret_cast<uint32_t*>(&out[1]) = 0;
        *reinterpret_cast<uint32_t*>(&out[2]) = 0;
        return 0;
    }
    float inv = GameGlobal<float>(0x005bcb2c) / len;
    out[0] = inv * q[0];
    out[1] = inv * q[1];
    out[2] = inv * q[2];
    out[3] = inv * q[3];
    return 0;
}

static uint32_t Quat_Normalize_Abi(uint32_t ecx, uint32_t edx)
{
    return Quat_Normalize(reinterpret_cast<const float*>(ecx), reinterpret_cast<float*>(edx));
}
ABI_ENTRY_004f12e0(Quat_Normalize_Entry, Quat_Normalize_Abi)
HOOK(0x004f12e0, "Quat_Normalize", Quat_Normalize_Entry, nullptr);

// 0x004f1350 (eax = source, ecx = destination): inverse: (-x, -y, -z, w) / n with n = ((w*w + z*z) + y*y) + x*x
// (n = 1 when 0). Returns 0 (status-word junk in the original).
uint32_t Quat_Inverse(const float* q, float* out)
{
    float y = q[1];
    float x = q[0];
    float w = q[3];
    float z = q[2];
    float n = w * w + z * z + y * y + x * x;
    if (n == GameGlobal<float>(0x0056c05c))
        n = GameGlobal<float>(0x005bcb2c);
    float inv = GameGlobal<float>(0x005bcb2c) / n;
    out[0] = -(inv * x);
    out[1] = -(y * inv);
    out[2] = -(z * inv);
    out[3] = inv * w;
    return 0;
}

static uint32_t Quat_Inverse_Abi(uint32_t eax, uint32_t ecx)
{
    return Quat_Inverse(reinterpret_cast<const float*>(eax), reinterpret_cast<float*>(ecx));
}
ABI_ENTRY_004f1350(Quat_Inverse_Entry, Quat_Inverse_Abi)
HOOK(0x004f1350, "Quat_Inverse", Quat_Inverse_Entry, nullptr);

// 0x004f13d0 (eax = axis-angle {x, y, z, angle radians}, ecx = destination): quaternion from axis and angle:
// h = angle * 0.5; (axis * sin h, cos h) (fsin / fcos on the x87 stack, kept in asm for exact results).
void Quat_FromAxisAngle(const float* aa, float* q)
{
    float h = aa[3] * GameGlobal<float>(0x005bcb4c);
    __asm {
        mov eax, aa
        mov ecx, q
        fld dword ptr [h]
        fld st(0)
        fsin
        fld st(0)
        fmul dword ptr [eax]
        fstp dword ptr [ecx]
        fld st(0)
        fmul dword ptr [eax + 4]
        fstp dword ptr [ecx + 4]
        fmul dword ptr [eax + 8]
        fstp dword ptr [ecx + 8]
        fcos
        fstp dword ptr [ecx + 0xc]
    }
}

static void Quat_FromAxisAngle_Abi(uint32_t eax, uint32_t ecx)
{
    Quat_FromAxisAngle(reinterpret_cast<const float*>(eax), reinterpret_cast<float*>(ecx));
}
ABI_ENTRY_004f13d0(Quat_FromAxisAngle_Entry, Quat_FromAxisAngle_Abi)
HOOK(0x004f13d0, "Quat_FromAxisAngle", Quat_FromAxisAngle_Entry, nullptr);

// 0x004f1400 (ecx = a, edx = b, esi = out; stack: t, caller pops): spherical interpolation. d = dot(a, b)
// ((b.x*a.x + b.z*a.z) + b.y*a.y + a.w*b.w). For d >= 0: out = kb*b + ka*a; for d < 0 (or NaN) out = ka*a - kb*b
// with |d|. kb = sin((1 - t) * th) / s, ka = sin(th * t) / s, th = atan2(s, |d|), s = sqrt(1 - d*d), or linear
// weights (1 - t, t) when 1 - |d| <= 1e-4. Returns 0 (status-word junk in the original).
uint32_t Quat_Slerp(const float* a, const float* b, float* out, float t)
{
    float one = GameGlobal<float>(0x005bcb2c);
    float eps = GameGlobal<float>(0x005bcbb0);
    float d = b[0] * a[0] + b[2] * a[2] + b[1] * a[1] + a[3] * b[3];
    float kb, ka;
    if (d >= GameGlobal<float>(0x0056c05c))
    {
        if (one - d > eps)
        {
            float s = std::sqrt(one - d * d);
            float th = AtanF(s, d);
            float invs = one / s;
            kb = SinMulF((one - t) * th, invs);
            ka = SinMulF(th * t, invs);
        }
        else
        {
            kb = one - t;
            ka = t;
        }
        out[0] = kb * b[0] + ka * a[0];
        out[1] = kb * b[1] + ka * a[1];
        out[2] = kb * b[2] + ka * a[2];
        out[3] = ka * a[3] + kb * b[3];
        return 0;
    }
    if (d + one > eps)
    {
        float s = std::sqrt(one - d * d);
        float th = AtanF(s, -d);
        float invs = one / s;
        kb = SinMulF((one - t) * th, invs);
        ka = SinMulF(th * t, invs);
    }
    else
    {
        kb = one - t;
        ka = t;
    }
    out[0] = ka * a[0] - kb * b[0];
    out[1] = ka * a[1] - kb * b[1];
    out[2] = ka * a[2] - kb * b[2];
    out[3] = ka * a[3] - kb * b[3];
    return 0;
}

static uint32_t Quat_Slerp_Abi(uint32_t ecx, uint32_t edx, uint32_t esi, uint32_t a0)
{
    float t;
    std::memcpy(&t, &a0, 4);
    return Quat_Slerp(reinterpret_cast<const float*>(ecx), reinterpret_cast<const float*>(edx),
                      reinterpret_cast<float*>(esi), t);
}
ABI_ENTRY_004f1400(Quat_Slerp_Entry, Quat_Slerp_Abi)
HOOK(0x004f1400, "Quat_Slerp", Quat_Slerp_Entry, nullptr);

// 0x004f1710 (eax = a, ecx = b, esi = out): quaternion product (written through a stack temporary when out aliases a
// or b): w = a.w*b.w - b.x*a.x - b.y*a.y - b.z*a.z, x = (b.y*a.z + b.w*a.x) + a.w*b.x - b.z*a.y,
// y = (b.w*a.y + b.y*a.w) + b.z*a.x - b.x*a.z, z = (b.w*a.z + b.x*a.y) + b.z*a.w - b.y*a.x.
// Returns eax as the original leaves it: a, or the bits of z after a copy.
uint32_t Quat_Mul(const float* a, const float* b, float* out)
{
    float tmp[4];
    float* dst = (out == b || out == a) ? tmp : out;
    dst[3] = a[3] * b[3] - b[0] * a[0] - b[1] * a[1] - b[2] * a[2];
    dst[0] = b[1] * a[2] + b[3] * a[0] + a[3] * b[0] - b[2] * a[1];
    dst[1] = b[3] * a[1] + b[1] * a[3] + b[2] * a[0] - b[0] * a[2];
    dst[2] = b[3] * a[2] + b[0] * a[1] + b[2] * a[3] - b[1] * a[0];
    if (dst == out)
        return reinterpret_cast<uint32_t>(a);
    std::memcpy(out, dst, 16);
    uint32_t bits;
    std::memcpy(&bits, &dst[2], 4);
    return bits;
}

static uint32_t Quat_Mul_Abi(uint32_t eax, uint32_t ecx, uint32_t esi)
{
    return Quat_Mul(reinterpret_cast<const float*>(eax), reinterpret_cast<const float*>(ecx),
                    reinterpret_cast<float*>(esi));
}
ABI_ENTRY_004f1710(Quat_Mul_Entry, Quat_Mul_Abi)
HOOK(0x004f1710, "Quat_Mul", Quat_Mul_Entry, nullptr);

// 0x004f17c0 (eax = vec3, ebx = quaternion, edi = out vec3): rotates a vector: q^-1 (0x004f1350) * (v, 0)
// (0x004f1710), then that * q; out = the xyz part. Returns the bits of out.z (eax as the original leaves it).
uint32_t Quat_RotateVector(const float* v, const float* q, float* out)
{
    alignas(4) float frame[12];
    frame[0] = v[0];
    frame[2] = v[2];
    frame[1] = v[1];
    frame[3] = 0.0f;
    Quat_Inverse(q, &frame[4]);
    Quat_Mul(&frame[4], &frame[0], &frame[8]);
    Quat_Mul(&frame[8], q, &frame[0]);
    std::memcpy(out, &frame[0], 12);
    uint32_t bits;
    std::memcpy(&bits, &frame[2], 4);
    return bits;
}

static uint32_t Quat_RotateVector_Abi(uint32_t eax, uint32_t ebx, uint32_t edi)
{
    return Quat_RotateVector(reinterpret_cast<const float*>(eax), reinterpret_cast<const float*>(ebx),
                             reinterpret_cast<float*>(edi));
}
ABI_ENTRY_004f17c0(Quat_RotateVector_Entry, Quat_RotateVector_Abi)
HOOK(0x004f17c0, "Quat_RotateVector", Quat_RotateVector_Entry, nullptr);

// 0x004f1830 (eax = a, ecx = b, edx = out): the shortest-arc rotation between two unit vectors: xyz = b x a
// components as in 0x004e7830, w = dot clamped to [-1, 1]; h = sqrt((w + 1) * 0.5); above FLT_EPSILON the result is
// (xyz / (2h), h), otherwise the identity. Returns 0 (status-word junk in the original).
uint32_t Quat_FromTwoVectors(const float* a, const float* b, float* out)
{
    float x = a[2] * b[1] - a[1] * b[2];
    float y = a[0] * b[2] - a[2] * b[0];
    float z = a[1] * b[0] - a[0] * b[1];
    out[0] = x;
    out[1] = y;
    out[2] = z;
    float w = b[2] * a[2] + b[1] * a[1] + b[0] * a[0];
    out[3] = w;
    if (w > GameGlobal<float>(0x005bcb2c))
        *reinterpret_cast<uint32_t*>(&out[3]) = 0x3f800000;
    if (out[3] < GameGlobal<float>(0x005bcb5c))
        *reinterpret_cast<uint32_t*>(&out[3]) = 0xbf800000;
    float h = std::sqrt((out[3] + GameGlobal<float>(0x005bcb2c)) * GameGlobal<float>(0x005bcb4c));
    if (h > GameGlobal<float>(0x005bcc88))
    {
        float inv = GameGlobal<float>(0x005bcb2c) / (h + h);
        out[0] = inv * out[0];
        out[1] = inv * out[1];
        out[2] = inv * out[2];
        out[3] = h;
        return 0;
    }
    *reinterpret_cast<uint32_t*>(&out[2]) = 0;
    *reinterpret_cast<uint32_t*>(&out[1]) = 0;
    *reinterpret_cast<uint32_t*>(&out[0]) = 0;
    *reinterpret_cast<uint32_t*>(&out[3]) = 0x3f800000;
    return 0;
}

static uint32_t Quat_FromTwoVectors_Abi(uint32_t eax, uint32_t ecx, uint32_t edx)
{
    return Quat_FromTwoVectors(reinterpret_cast<const float*>(eax), reinterpret_cast<const float*>(ecx),
                               reinterpret_cast<float*>(edx));
}
ABI_ENTRY_004f1830(Quat_FromTwoVectors_Entry, Quat_FromTwoVectors_Abi)
HOOK(0x004f1830, "Quat_FromTwoVectors", Quat_FromTwoVectors_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// Item pools (header layout in the file comment) and the "Big List Chain" block list.
// ---------------------------------------------------------------------------------------------------------------

// 0x004f5ff0 (edi = big list {+0 item size, +4 items per block, +8 free list, +0xc block chain}): frees every block
// of the chain (PMS_heapFree 0x004e08d0, heap 0x0063a210) and clears the chain and the free list. Returns 0.
// Not executed in recorded runs.
uint32_t BigList_FreeAll(uint32_t list)
{
    uint32_t b = D(list + 0xc);
    while (b != 0)
    {
        uint32_t next = D(b);
        orig::fn_004e08d0(kPmsHeap, b);
        b = next;
    }
    D(list + 0xc) = 0;
    D(list + 8) = 0;
    return 0;
}

static uint32_t BigList_FreeAll_Abi(uint32_t edi)
{
    return BigList_FreeAll(edi);
}
ABI_ENTRY_004f5ff0(BigList_FreeAll_Entry, BigList_FreeAll_Abi)
HOOK(0x004f5ff0, "BigList_FreeAll", BigList_FreeAll_Entry, nullptr);

// 0x004f6020 (esi = big list): allocates one item. When the free list is empty, a block of size * count + 4 bytes
// is allocated (PMS_heapAlloc 0x004e07f0: heap 0x0063a210, 3, size, 2, "Big List Chain" 0x0058aa10), zeroed,
// linked into the chain (+0), and its items are chained into the free list. Original quirk kept: only count - 1
// items are linked (the loop runs count - 2 times), the last item of each block is never used.
uint32_t BigList_Alloc(uint32_t list)
{
    if (D(list + 8) == 0)
    {
        uint32_t bytes = D(list) * D(list + 4) + 4;
        uint32_t block = orig::fn_004e07f0(kPmsHeap, 3, bytes, 2, 0x0058aa10);
        std::memset(P(block), 0, D(list) * D(list + 4) + 4);
        D(block) = D(list + 0xc);
        uint32_t item = block + 4;
        D(list + 0xc) = block;
        D(list + 8) = item;
        for (int32_t i = 0; i < static_cast<int32_t>(D(list + 4) - 2); ++i)
        {
            uint32_t next = D(list) + item;
            D(item) = next;
            item = next;
        }
        D(item) = 0;
    }
    uint32_t r = D(list + 8);
    D(list + 8) = D(r);
    return r;
}

static uint32_t BigList_Alloc_Abi(uint32_t esi)
{
    return BigList_Alloc(esi);
}
ABI_ENTRY_004f6020(BigList_Alloc_Entry, BigList_Alloc_Abi)
HOOK(0x004f6020, "BigList_Alloc", BigList_Alloc_Entry, nullptr);

// 0x004f60e0 (eax = pool, edx = alignment shift; stack: heap, item size, block size, flags, alloc tag, callee pops):
// initialises a pool header: zeroed, item size and header size rounded up to the alignment, items per block =
// (block size - header) / item size (unsigned), then PMS_Pool_Reset(pool, 1) links the first block's items.
// Returns PMS_Pool_Reset's eax.
uint32_t PMS_Pool_Init(uint32_t pool, uint32_t shift, uint32_t heap, uint32_t itemSize, uint32_t blockSize,
                       uint32_t flags, uint32_t tag)
{
    std::memset(P(pool), 0, 0x24);
    D(pool + 0xc) = heap;
    W(pool + 0x1e) = static_cast<uint16_t>(flags);
    W(pool + 0x1c) = static_cast<uint16_t>(shift);
    uint32_t mask = (1u << (shift & 31)) - 1;
    D(pool + 0x10) = blockSize;
    uint32_t item = (mask + itemSize) & ~mask;
    uint32_t header = (mask + 0x24) & ~mask;
    D(pool + 0x14) = item;
    D(pool + 0x18) = (blockSize - header) / item;
    D(pool + 0x20) = tag;
    return PMS_Pool_Reset(pool, 1);
}

static uint32_t PMS_Pool_Init_Abi(uint32_t eax, uint32_t edx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3,
                                  uint32_t a4)
{
    return PMS_Pool_Init(eax, edx, a0, a1, a2, a3, a4);
}
ABI_ENTRY_004f60e0(PMS_Pool_Init_Entry, PMS_Pool_Init_Abi)
HOOK(0x004f60e0, "PMS_Pool_Init", PMS_Pool_Init_Entry, nullptr);

uint32_t PMS_Pool_LinkBlock(uint32_t block, uint32_t pool);

// 0x004f6150 (stack: pool, callee pops): grows a pool by one block: PMS_heapAlloc 0x004e07f0 (ecx = pool heap +0xc;
// 0x19, block size, 0xc, alloc tag), copies the 0x24-byte header into it, flags |= 8, clears its links, inserts it
// after the pool header in the block chain (+4) and links its items (0x004f61a0). Returns 0x004f61a0's eax.
uint32_t PMS_Pool_Grow(uint32_t pool)
{
    uint32_t block = orig::fn_004e07f0(D(pool + 0xc), 0x19, D(pool + 0x10), 0xc, D(pool + 0x20));
    std::memcpy(P(block), P(pool), 0x24);
    By(block + 0x1e) |= 8;
    D(block + 8) = 0;
    D(block) = 0;
    D(block + 4) = D(pool + 4);
    D(pool + 4) = block;
    return PMS_Pool_LinkBlock(block, pool);
}

static uint32_t PMS_Pool_Grow_Abi(uint32_t a0)
{
    return PMS_Pool_Grow(a0);
}
ABI_ENTRY_004f6150(PMS_Pool_Grow_Entry, PMS_Pool_Grow_Abi)
HOOK(0x004f6150, "PMS_Pool_Grow", PMS_Pool_Grow_Entry, nullptr);

// 0x004f61a0 (eax = block or 0 for the pool header itself, edx = pool): chains the block's items (after its aligned
// header, stride = item size) and pushes them on the pool's free list (+8). Returns the last item.
uint32_t PMS_Pool_LinkBlock(uint32_t block, uint32_t pool)
{
    uint32_t base = block ? block : pool;
    uint32_t mask = (1u << (By(pool + 0x1c) & 31)) - 1;
    uint32_t first = ((mask + 0x24) & ~mask) + base;
    uint32_t item = first;
    for (uint32_t i = 1; i < D(pool + 0x18); ++i)
    {
        uint32_t next = D(pool + 0x14) + item;
        D(item) = next;
        item = next;
    }
    D(item) = D(pool + 8);
    D(pool + 8) = first;
    return item;
}

static uint32_t PMS_Pool_LinkBlock_Abi(uint32_t eax, uint32_t edx)
{
    return PMS_Pool_LinkBlock(eax, edx);
}
ABI_ENTRY_004f61a0(PMS_Pool_LinkBlock_Entry, PMS_Pool_LinkBlock_Abi)
HOOK(0x004f61a0, "PMS_Pool_LinkBlock", PMS_Pool_LinkBlock_Entry, nullptr);

// 0x004f61f0 (esi = pool; stack: force, callee pops): resets a pool when its flags bit 0 or the force byte is set:
// unless flags bit 2, the extra blocks are freed (PMS_heapFree 0x004e08d0 with the pool heap); then the free list is
// rebuilt from every remaining block (the header block first). Returns the last item linked (or 0 when nothing is
// done: the original returns eax with al = 0 then).
uint32_t PMS_Pool_Reset(uint32_t pool, uint32_t force)
{
    if ((By(pool + 0x1e) & 1) == 0 && static_cast<uint8_t>(force) == 0)
        return 0;
    if ((By(pool + 0x1e) & 4) == 0)
    {
        uint32_t b;
        while ((b = D(pool + 4)) != 0)
        {
            D(pool + 4) = D(b + 4);
            orig::fn_004e08d0(D(pool + 0xc), b);
        }
    }
    D(pool + 8) = 0;
    uint32_t last = 0;
    for (uint32_t b = pool; b != 0; b = D(b + 4))
        last = PMS_Pool_LinkBlock(b, pool);
    return last;
}

static uint32_t PMS_Pool_Reset_Abi(uint32_t esi, uint32_t a0)
{
    return PMS_Pool_Reset(esi, a0);
}
ABI_ENTRY_004f61f0(PMS_Pool_Reset_Entry, PMS_Pool_Reset_Abi)
HOOK(0x004f61f0, "PMS_Pool_Reset", PMS_Pool_Reset_Entry, nullptr);

// 0x004f6290 (ebx = pool): releases extra blocks whose items are all free: for each block of the chain (+4), counts
// the free-list items inside it (address > block and < block + block size); when that equals the items per block,
// those items are unlinked from the free list, the block is unlinked and freed (PMS_heapFree 0x004e08d0, pool heap).
// Returns the last PMS_heapFree's eax, else 0.
uint32_t PMS_Pool_Compact(uint32_t pool)
{
    uint32_t eax = 0;
    uint32_t link = pool + 4;
    uint32_t block = D(link);
    while (block != 0)
    {
        uint32_t n = 0;
        for (uint32_t it = D(pool + 8); it != 0; it = D(it))
        {
            if (it > block && it < D(block + 0x10) + block)
                ++n;
        }
        if (n == D(block + 0x18))
        {
            uint32_t prev = pool + 8;
            uint32_t it = D(prev);
            while (it != 0)
            {
                if (it > block && it < D(block + 0x10) + block)
                    D(prev) = D(it);
                else
                    prev = it;
                it = D(prev);
            }
            D(link) = D(block + 4);
            eax = orig::fn_004e08d0(D(pool + 0xc), block);
        }
        else
        {
            link = block + 4;
        }
        block = D(link);
    }
    return eax;
}

static uint32_t PMS_Pool_Compact_Abi(uint32_t ebx)
{
    return PMS_Pool_Compact(ebx);
}
ABI_ENTRY_004f6290(PMS_Pool_Compact_Entry, PMS_Pool_Compact_Abi)
HOOK(0x004f6290, "PMS_Pool_Compact", PMS_Pool_Compact_Entry, nullptr);
