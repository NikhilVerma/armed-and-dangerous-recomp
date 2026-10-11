// Persistent decals and bodies (docs/goals/remaster-basics.md item 3; evidence: docs/subsystems/decals-bodies.md).
// Switches in [persistence] in and.ini, all off by default (the original limits).
//
//   decal_pool=0      decals allocated per level (0: the original 160; each takes 3,948 bytes; at most 4096)
//   decals_keep=1     never fade decals early to keep spares (the original's rule: fade the oldest while fewer than 32
//                     are free or fading). Measured: that rule never fires, because the manager's allocated counter is
//                     twice the pool; the limit is the pool size, so decal_pool is the switch that matters. Not on the
//                     Remaster page.
//   bodies_keep=1     dead characters stay instead of fading out after 1 to 5.5 s
//   log=1             every 300 frames: decal pool counters and kept bodies, to and.log
//   bodies_max=64     with bodies_keep: at most this many kept bodies; beyond it the oldest fade out as before
//                     (0: no limit)
//
// Hook points: Decals_AllocatePool / Decals_Update (src/game/decals.cpp) and Corpse_CountDown (src/game/corpses.cpp).
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <unordered_map>

#include "config.h"
#include "game.h"
#include "game/corpses.h"
#include "game/decals.h"
#include "game/game_object.h"
#include "log.h"
#include "settings.h"

namespace
{
struct Kept
{
    ULONGLONG first, last;  // when first held, when last seen
    bool released;
};
std::unordered_map<void*, Kept> g_kept;
uint32_t g_bodiesMax;
bool g_log;

// Called for each dead character still counting down, once per frame. Bodies not seen for a second are gone
// (removed by the level or a reload) and are forgotten.
bool HoldBody(void* character)
{
    ULONGLONG now = GetTickCount64();
    auto [it, added] = g_kept.try_emplace(character, Kept{now, now, false});
    it->second.last = now;
    if (added && g_kept.size() > 64)
        for (auto i = g_kept.begin(); i != g_kept.end();)
            i = now - i->second.last > 1000 ? g_kept.erase(i) : std::next(i);
    if (it->second.released)
        return false;
    if (g_bodiesMax && g_kept.size() > g_bodiesMax)
    {
        // Over the limit: the oldest kept body starts its original countdown.
        auto oldest = g_kept.end();
        for (auto i = g_kept.begin(); i != g_kept.end(); ++i)
            if (!i->second.released && (oldest == g_kept.end() || i->second.first < oldest->second.first))
                oldest = i;
        if (oldest != g_kept.end())
            oldest->second.released = true;
        if (oldest == it)
            return false;
    }
    return true;
}
}  // namespace

void PersistenceOnFrame()
{
    static unsigned frame;
    if (!g_log || ++frame % 300 || !g_game)
        return;
    void* m = Field<void*>(g_game, 0xa7c);
    if (!m)
        return;
    Log("persistence: decals allocated %u active %u fading %u, fade length %.2f; bodies kept %zu",
        Field<uint32_t>(m, 0x18), Field<uint32_t>(m, 0x1c), Field<uint32_t>(m, 0x20), Field<float>(m, 0x24),
        g_kept.size());
}

void PersistenceInit()
{
    g_log = ConfigInt("persistence", "log", 0) != 0;
    using S = RemasterSetting;
    // decal_pool is a number of decals in and.ini; the page offers the original and three larger pools.
    static const uint32_t kPools[] = {0, 512, 1024, 2048};
    int pool = ConfigIntDefault("persistence", "decal_pool", 0, 1024);
    int choice = pool >= 2048 ? 3 : pool >= 1024 ? 2 : pool >= 512 ? 1 : 0;
    RemasterRegister({"persistence", "decal_pool_choice", L"Decal pool", S::List, 0, 3, 1,
                      {L"Original (160)", L"512", L"1024", L"2048"}, 0, true,
                      [](float v) {
                          // written as decal_pool too, which is what the next level reads
                          uint32_t n = kPools[static_cast<int>(v + 0.5f)];
                          char ini[MAX_PATH], text[16];
                          GetModuleFileNameA(nullptr, ini, MAX_PATH);
                          strcpy(strrchr(ini, '\\') + 1, "and.ini");
                          snprintf(text, sizeof(text), "%u", n);
                          WritePrivateProfileStringA("persistence", "decal_pool", text, ini); ConfigForget();
                          g_decalLimits.poolSize = n ? n : 160;
                      }},
                     static_cast<float>(choice));
    if (pool > 0)
        g_decalLimits.poolSize = static_cast<uint32_t>(pool > 4096 ? 4096 : pool);
    if (ConfigInt("persistence", "decals_keep", 0))
        g_decalLimits.fadeReserve = 0;
    g_bodiesMax = static_cast<uint32_t>(ConfigInt("persistence", "bodies_max", 64));
    if (RemasterRegister({"persistence", "bodies_keep", L"Keep bodies", S::Toggle, 0, 1, 1, {}, 0, false,
                          [](float v) { g_corpseHold = v != 0.0f ? HoldBody : nullptr; }},
                         0, 1) != 0.0f)
        g_corpseHold = HoldBody;
    Log("persistence: decal pool %u, fade reserve %u, bodies %s (max %u)", g_decalLimits.poolSize,
        g_decalLimits.fadeReserve, g_corpseHold ? "kept" : "original", g_bodiesMax);
}
