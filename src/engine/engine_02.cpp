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
