// Recovered: dead-character removal countdown. S2, Approach B.
//   0x0042eba0 Corpse_CountDown
#include "corpses.h"

#include "game.h"
#include "gen/entry_stubs.h"
#include "gen/orig_calls.h"
#include "hooks.h"

bool (*g_corpseHold)(void* character) = nullptr;

namespace
{
constexpr uint32_t kTimeLeft = 0x9f0, kFadeLength = 0x9f4;
inline float& FrameDt()
{
    return GameGlobal<float>(0x00604a90);  // Timer_Tick's frame time (seconds)
}
}  // namespace

// 0x0042eba0 (esi = character; called from the character update 0x0043ee90, four call sites 0x00445d3a..0x00445f23, for dead characters):
// - time left (+0x9f0) -= frame time, clamped to 0 when it reaches or passes it;
// - within the last fade-length (+0x9f4) seconds, virtual 64 (+0x100, set alpha) with 1 - (fade - left) / fade;
// - at 0, removes the character (0x00455640, ecx = character, stack 0): its death fx and husk object.
// x87 comparisons: `<= 0` (unordered: no clamp), `fade - left >= 0` (unordered: no fade), `== 0` (unordered: kept).
void Corpse_CountDown(uint8_t* c)
{
    if (g_corpseHold && g_corpseHold(c))
        return;
    float left = Field<float>(c, kTimeLeft) - FrameDt();
    Field<float>(c, kTimeLeft) = left;
    if (left <= 0.0f)
        Field<float>(c, kTimeLeft) = 0.0f;
    float into = Field<float>(c, kFadeLength) - Field<float>(c, kTimeLeft);
    if (into >= 0.0f)
    {
        float alpha = 1.0f - into / Field<float>(c, kFadeLength);  // 0x005bcb2c = 1.0
        VCall<void>(c, 0x100 / 4, alpha);
    }
    if (Field<float>(c, kTimeLeft) == 0.0f)
        orig::fn_00455640(reinterpret_cast<uint32_t>(c), 0);
}

static uint32_t Corpse_CountDown_Abi(uint32_t /*ecx: only pushed as a stack slot*/, uint32_t esi)
{
    Corpse_CountDown(reinterpret_cast<uint8_t*>(esi));
    return 0;  // every caller jumps to 0x004460a2, which loads eax before using it
}
ABI_ENTRY_0042eba0(Corpse_CountDown_Entry, Corpse_CountDown_Abi)
HOOK(0x0042eba0, "Corpse_CountDown", Corpse_CountDown_Entry, nullptr);
