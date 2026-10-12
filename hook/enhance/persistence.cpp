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
//   debris_keep=1     explosion debris (fx objects "fxo_debri*": blown-up houses, towers, props) stays instead of
//                     fading out after its scripted 1-12 s (fx opcodes 0x08 lifespan / 0x0a fade, FxObject update
//                     0x0041a350); at most debris_max (1000) pieces per level, beyond that they fade as before
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

extern bool (*g_fxKeep)(void* fx);  // src/renderer/render_r1.cpp (0x0041a350)
extern bool (*g_isHeldBody)(void* character);  // src/physics/physics_01.cpp (Character_GoRagdoll)
extern uint32_t g_ragdollLimit;                    // ditto: original 3 (four ragdolls)
extern bool (*g_meshCollision)(void* object);      // src/physics/physics_02.cpp (Object_BuildRadiusCollision)

namespace
{
uint32_t g_debrisMax, g_debrisKept;
void* g_debrisLevel;  // the level the count belongs to (its object manager, Game+0xa84)

bool IsDebris(void* fx)
{
    void* tmpl = Field<void*>(fx, 0x414);
    if (!tmpl || !Field<uint32_t>(tmpl, 0x10))
        return false;
    const char* name = reinterpret_cast<const char*>(Field<uint32_t>(tmpl, 0x10) + 4);
    return _strnicmp(name, "fxo_debri", 9) == 0;
}

// Explosion debris: fx objects whose template name starts "fxo_debri" (obj+0x414 -> +0x10, name at +4).
bool KeepDebris(void* fx)
{
    void* tmpl = Field<void*>(fx, 0x414);
    if (!tmpl)
        return false;
    const char* name = reinterpret_cast<const char*>(Field<uint32_t>(tmpl, 0x10) + 4);
    if (!name || _strnicmp(name, "fxo_debri", 9) != 0)
        return false;
    // Only pieces that fall and land: full gravity (+0x48c, opcode 0x11; 0 = weightless, the fx default) and ground
    // collision (+0x460 bit 0x10000, Fx_SetHitGround). The rest drift or sink in the original and fade within
    // seconds; kept, they hung in the air (the player, 2026-10-12: "building particles are floating in the air").
    if (!(Field<float>(fx, 0x48c) >= 0.99f) || !(Field<uint32_t>(fx, 0x460) & 0x10000))
        return false;
    void* level = g_game ? Field<void*>(g_game, 0xa84) : nullptr;  // the level's object manager
    if (level != g_debrisLevel)
        g_debrisLevel = level, g_debrisKept = 0;
    if (g_debrisMax && g_debrisKept >= g_debrisMax)
        return false;
    ++g_debrisKept;
    return true;
}

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
// A body held past its removal (Character_GoRagdoll leaves it out of the game's four-ragdoll limit).
bool IsHeldBody(void* character)
{
    auto it = g_kept.find(character);
    return it != g_kept.end() && !it->second.released;
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
    g_isHeldBody = IsHeldBody;  // empty until bodies are held
    // Debris collides by its model's shape, not one bounding sphere ([persistence] debris_mesh_collision, remaster on).
    if (ConfigIntDefault("persistence", "debris_mesh_collision", 0, 1))
        g_meshCollision = IsDebris;
    // Simultaneous ragdolls: the original allows four (a 2003 CPU budget); remaster 64 (the player: "why not 256").
    g_ragdollLimit = static_cast<uint32_t>(
        RemasterRegister({"persistence", "ragdoll_limit", L"Max ragdolls", S::Slider, 4, 256, 4, {}, 0, false,
                          [](float v) { g_ragdollLimit = v >= 1.0f ? static_cast<uint32_t>(v) - 1 : 0; }},
                         4.0f, 64.0f) - 1.0f);
    g_debrisMax = static_cast<uint32_t>(ConfigInt("persistence", "debris_max", 1000));
    if (RemasterRegister({"persistence", "debris_keep", L"Keep explosion debris", S::Toggle, 0, 1, 1, {}, 0, false,
                          [](float v) { g_fxKeep = v != 0.0f ? KeepDebris : nullptr; }},
                         0, 1) != 0.0f)
        g_fxKeep = KeepDebris;
    Log("persistence: decal pool %u, fade reserve %u, bodies %s (max %u), debris %s (max %u)", g_decalLimits.poolSize,
        g_decalLimits.fadeReserve, g_corpseHold ? "kept" : "original", g_bodiesMax, g_fxKeep ? "kept" : "original",
        g_debrisMax);
}
