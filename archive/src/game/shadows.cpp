// Recovered: the game's shadow passes. S2, Approach B.
//   0x004163c0 Shadows_Update
//   0x004162b0 Shadows_Draw
#include "shadows.h"

#include "game.h"
#include "game_object.h"
#include "gen/entry_stubs.h"
#include "gen/orig_calls.h"
#include "hooks.h"

bool g_gameShadowsReplaced = false;

namespace
{
template <class T>
uint32_t u(T* p)
{
    return reinterpret_cast<uint32_t>(p);
}
}  // namespace

// 0x004163c0 (eax = decal manager; stack: arg, callee pops 4; caller: the main scene pass 0x00489070, flag 4).
// When shadows are on (+4): for each object of the list Game+0xa84 +0xe78 (iterator 0x004e7340 eax = list, esi =
// iterator / 0x004e73c0 edi = iterator), its shadow (virtual +0x130) is updated by 0x00418190 (esi = shadow;
// stack: arg; ecx is only pushed as a stack slot there): the light camera (0x00417c00) and the silhouette render
// (0x00417f20). Afterwards the iterator's node (second dword) loses a reference (+0x18). Returns eax as left: the
// caller ignores it.
uint32_t Shadows_Update(void* manager, uint32_t arg)
{
    if (!Field<uint8_t>(manager, 4) || g_gameShadowsReplaced)
        return 0;
    uint32_t iterator[2] = {0, 0};
    void* list = Field<void*>(Field<void*>(g_game, 0xa84), 0xe78);
    for (void* obj = reinterpret_cast<void*>(orig::fn_004e7340(u(list), u(iterator))); obj;
         obj = reinterpret_cast<void*>(orig::fn_004e73c0(u(iterator))))
    {
        void* shadow = VCall<void*>(obj, 0x130 / 4);
        if (shadow)
            orig::fn_00418190(0, u(shadow), arg);
    }
    if (iterator[1])
        --Field<int32_t>(reinterpret_cast<void*>(iterator[1]), 0x18);
    return 0;
}

// 0x004162b0 (eax = decal manager; caller 0x00489070): when shadows are on (+4) and some are active (list +0x14):
// world matrix = identity (renderer virtual 21 with 0x005fe528), renderer virtual 41 (+0xa4)(1, 3, 4, 3, 0) (a blend
// state), each active shadow's draw (virtual +0x20)(0, 1) along +0x50, then virtual 41 (0, 0, 0, 0, 0).
uint32_t Shadows_Draw(void* manager)
{
    if (!Field<uint8_t>(manager, 4) || !Field<void*>(manager, 0x14) || g_gameShadowsReplaced)
        return 0;
    VCall<void>(Field<void*>(g_game, 0x38), 0x54 / 4, reinterpret_cast<void*>(0x005fe528));
    VCall<void>(Field<void*>(g_game, 0x38), 0xa4 / 4, 1u, 3u, 4u, 3u, 0u);
    for (void* s = Field<void*>(manager, 0x14); s; s = Field<void*>(s, 0x50))
        VCall<void>(s, 0x20 / 4, 0u, 1u);
    return VCall<uint32_t>(Field<void*>(g_game, 0x38), 0xa4 / 4, 0u, 0u, 0u, 0u, 0u);
}

static uint32_t Shadows_Update_Abi(uint32_t eax, uint32_t arg)
{
    return Shadows_Update(reinterpret_cast<void*>(eax), arg);
}
ABI_ENTRY_004163c0(Shadows_Update_Entry, Shadows_Update_Abi)
HOOK(0x004163c0, "Shadows_Update", Shadows_Update_Entry, nullptr);

static uint32_t Shadows_Draw_Abi(uint32_t eax)
{
    return Shadows_Draw(reinterpret_cast<void*>(eax));
}
ABI_ENTRY_004162b0(Shadows_Draw_Entry, Shadows_Draw_Abi)
HOOK(0x004162b0, "Shadows_Draw", Shadows_Draw_Entry, nullptr);
