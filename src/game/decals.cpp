// Recovered: decal pool. S2, Approach B.
//   0x00416150 Decals_AllocatePool
//   0x00416440 Decals_Update
#include "decals.h"

#include "game.h"
#include "game_object.h"
#include "gen/entry_stubs.h"
#include "gen/orig_calls.h"
#include "hooks.h"

DecalLimits g_decalLimits;

namespace
{
template <class T>
uint32_t u(T* p)
{
    return reinterpret_cast<uint32_t>(p);
}

// Decal manager (Game+0xa7c) and decal (0xf6c bytes) members used here.
constexpr uint32_t kFreeList = 0x08, kActiveHead = 0x0c, kAllocated = 0x18, kActive = 0x1c, kFading = 0x20,
                   kFadeLength = 0x24;
constexpr uint32_t kNext = 0x50, kAge = 0xf5c, kFadeLeft = 0xf64, kFadeTotal = 0xf68;
constexpr uint32_t kDecalSize = 0xf6c;
}  // namespace

// 0x00416150 (edi = manager; caller 0x00416130, level setup): allocates the pool. Each decal comes from the heap at
// 0x0063a210 (0x004e07f0: alignment 0x10, size 0xf6c, flags 2, tag string 0x0058b5e8) and goes onto the free list
// (0x00417010, esi = manager +8). The original loops with `dec ebx / jne`, 160 times.
uint32_t Decals_AllocatePool(void* manager)
{
    uint32_t n = g_decalLimits.poolSize;
    do
    {
        uint32_t decal = orig::fn_004e07f0(0x0063a210, 0x10, kDecalSize, 2, 0x0058b5e8);
        if (decal)
            orig::fn_00417010(decal, u(manager) + kFreeList);
    } while (--n);
    return 0;  // the only caller ignores it (0x00416139..0x00416141 call on)
}

// 0x00416440 (eax = manager; stack: frame time (passed on to each decal), an unused 0; callee pops 8; caller
// 0x00403050, once per rendered frame):
// - If free + fading decals (allocated - active + fading) is below 32, the oldest non-fading active decal (largest
//   age +0xf5c; the list head is taken first whatever its state) starts fading with the manager's fade length,
//   unless it is already fading faster, and the manager's fading count goes up (through Game+0xa7c).
// - The fade length eases towards 0.3 s under that pressure (x0.1 per frame), towards 2.0 s otherwise (x0.05).
// - Every active decal is updated (0x00417180, esi = decal, stack: frame time).
// The x87 comparisons are reproduced with their unordered (NaN) outcomes.
uint32_t Decals_Update(void* manager, uint32_t dt, uint32_t /*arg1*/)
{
    float& fadeLength = Field<float>(manager, kFadeLength);
    uint32_t spare = Field<uint32_t>(manager, kFading) - Field<uint32_t>(manager, kActive) +
                     Field<uint32_t>(manager, kAllocated);
    if (spare < g_decalLimits.fadeReserve)
    {
        uint8_t* oldest = nullptr;
        for (uint8_t* d = Field<uint8_t*>(manager, kActiveHead); d; d = Field<uint8_t*>(d, kNext))
        {
            if (!oldest)
            {
                oldest = d;
                continue;
            }
            if (!(Field<float>(d, kFadeTotal) == 0.0f))  // fading (or NaN): skip
                continue;
            if (Field<float>(d, kAge) > Field<float>(oldest, kAge))
                oldest = d;
        }
        if (oldest)
        {
            float length = fadeLength;
            bool start = Field<float>(oldest, kFadeTotal) == 0.0f || length < Field<float>(oldest, kFadeLeft);
            if (start)
            {
                Field<float>(oldest, kFadeLeft) = length;
                Field<float>(oldest, kFadeTotal) = length;
                ++Field<uint32_t>(Field<void*>(g_game, 0xa7c), kFading);
            }
        }
        fadeLength = fadeLength + (0.3f - fadeLength) * 0.1f;  // 0x005bccd4, 0x005bcbfc
    }
    else
        fadeLength = fadeLength + (2.0f - fadeLength) * 0.05f;  // 0x005bcb80, 0x005bcc34
    for (uint8_t* d = Field<uint8_t*>(manager, kActiveHead); d;)
    {
        uint8_t* next = Field<uint8_t*>(d, kNext);
        orig::fn_00417180(0, u(d), dt);  // ecx: only pushed as a stack slot by the callee
        d = next;
    }
    return 0;  // the caller ignores it (0x004030f9 on)
}

static uint32_t Decals_AllocatePool_Abi(uint32_t edi)
{
    return Decals_AllocatePool(reinterpret_cast<void*>(edi));
}
ABI_ENTRY_00416150(Decals_AllocatePool_Entry, Decals_AllocatePool_Abi)
HOOK(0x00416150, "Decals_AllocatePool", Decals_AllocatePool_Entry, nullptr);

static uint32_t Decals_Update_Abi(uint32_t eax, uint32_t /*ecx*/, uint32_t dt, uint32_t arg1)
{
    return Decals_Update(reinterpret_cast<void*>(eax), dt, arg1);
}
ABI_ENTRY_00416440(Decals_Update_Entry, Decals_Update_Abi)
HOOK(0x00416440, "Decals_Update", Decals_Update_Entry, nullptr);
