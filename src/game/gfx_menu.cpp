// Recovered: the Graphics options page's resolution list. S2, Approach B.
//   0x00470d70 GfxMenu_InitResolutionList
#include "game.h"
#include "game_object.h"
#include "gen/entry_stubs.h"
#include "gen/orig_calls.h"
#include "hooks.h"
#include "renderer/renderer.h"

namespace
{
// The list's values (packed modes, 0x00470c90) and their count, read back by the list callback 0x00470d00.
inline uint32_t* ResolutionValues()
{
    return &GameGlobal<uint32_t>(0x005fe5e8);
}
inline uint32_t& ResolutionCount()
{
    return GameGlobal<uint32_t>(0x0063a188);
}
constexpr uint32_t kMaxResolutions = 0x100 / 4;  // 0x00470e17 cmp edi, 0x100 (bytes)

bool AspectListed(int32_t width, int32_t height)
{
    if (g_displayModeFilter.allowAnyAspect)
        return true;
    return height * 16 == width * 9 || height * 4 == width * 3 || height * 5 == width * 4;
}
}  // namespace

// 0x00470d70 ([item], caller pops; called by the Graphics page's InitItem 0x004710d0 for code 48): fills the
// resolution list from the renderer's modes (virtual 13, +0x34: returns the count and stores a pointer to
// {width, height, bpp} records; virtual 14, +0x38 releases it). A mode is listed when its bpp is 16 or 32, its aspect
// is 16:9, 4:3 or 5:4 and its width is 640..1600 (signed compares), once per packed value (0x00470c90: eax = height,
// ecx = width, esi = bpp), at most 64. The current option Game+0x4a8 selects the initial index (0 if absent). Then
// 0x0046cd50 (eax = item, ecx = "ui_gfxmenu_resolution" 0x005a0348; count, 1, callback 0x00470d00, 0, index).
// Enhancement point: the width range and aspect test follow g_displayModeFilter (src/renderer/renderer.h), whose
// defaults (640, 1600, three aspects) are exactly this function's constants.
uint32_t GfxMenu_InitResolutionList(uint32_t item)
{
    void* renderer = Field<void*>(g_game, 0x38);
    const int32_t* modes = nullptr;
    uint32_t available = VCall<uint32_t>(renderer, 0x34 / 4, &modes);
    uint32_t count = 0;
    ResolutionCount() = 0;
    if (available)
    {
        for (uint32_t i = 0; i < available; ++i)
        {
            int32_t width = modes[i * 3], height = modes[i * 3 + 1], bpp = modes[i * 3 + 2];
            if (bpp != 0x10 && bpp != 0x20)
                continue;
            if (!AspectListed(width, height))
                continue;
            if (width < static_cast<int32_t>(g_displayModeFilter.minWidth) ||
                width > static_cast<int32_t>(g_displayModeFilter.maxWidth))
                continue;
            uint32_t packed = orig::fn_00470c90(height, width, bpp);
            uint32_t j = 0;
            while (j < count && ResolutionValues()[j] != packed)
                ++j;
            if (j == count && count < kMaxResolutions)
                ResolutionValues()[count++] = packed;
        }
        ResolutionCount() = count;
    }
    VCall<void>(Field<void*>(g_game, 0x38), 0x38 / 4, modes);
    uint32_t index = 0;
    for (uint32_t i = 0; i < ResolutionCount(); ++i)
        if (ResolutionValues()[i] == Field<uint32_t>(g_game, 0x4a8))
        {
            index = i;
            break;
        }
    return orig::fn_0046cd50(item, 0x005a0348, ResolutionCount(), 1, 0x00470d00, 0, index);
}

static uint32_t GfxMenu_InitResolutionList_Abi(uint32_t item)
{
    return GfxMenu_InitResolutionList(item);
}
ABI_ENTRY_00470d70(GfxMenu_InitResolutionList_Entry, GfxMenu_InitResolutionList_Abi)
HOOK(0x00470d70, "GfxMenu_InitResolutionList", GfxMenu_InitResolutionList_Entry, nullptr);
