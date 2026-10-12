// Engine caps lifted (docs/engine-limits.md). Switches in [limits] in and.ini; the original values with
// [remaster] original_defaults=1.
//
//   force_pool=8192         physics force actions (ForceActionPool_Init 0x00403ef0; original 512). When the pool is
//                           full ForceActionPool_Add 0x00403fb0 drops the force: big explosions among kept bodies and
//                           debris lost impulses. At most 65535 (16-bit free list).
//   particle_pool_scale=16  particle pools x this (ParticlePool_Create 0x0046d780; original 2000 / 4000 / 2000).
//                           Particle_Alloc 0x0046d910 returns nothing when a pool is full: the particle never appears.
//   path_nodes=16000        A* nodes per path search (AStar_Add 0x00470600, Grid_FindPath 0x0046fd20; original 2000).
//                           At the cap the search stops growing and the AI takes a partial path.
//   log=1                   log the drop counters when they change (every 2 s)
//
// All take effect at the next level load (the pools are made when a level loads).
#include <windows.h>

#include <cstdint>

#include "config.h"
#include "game.h"
#include "game/game_object.h"
#include "log.h"
#include "settings.h"

extern uint32_t g_forcePoolCapacity, g_forcePoolDrops;  // src/physics/physics_01.cpp
extern int32_t g_particlePoolScale;                     // src/loader/auto_0045bbe0.cpp
extern uint32_t g_particleDrops;                        // src/renderer/render_r3.cpp
extern void* g_particleDropPool;
extern uint32_t g_astarCapacity, g_astarRefused;        // src/ai/ai_07.cpp

namespace
{
bool g_log;
}

void LimitsInit()
{
    using S = RemasterSetting;
    g_forcePoolCapacity = static_cast<uint32_t>(
        RemasterRegister({"limits", "force_pool", L"Physics force pool", S::Slider, 512, 65535, 512, {}, 0, true,
                          nullptr},
                         512, 8192));
    g_particlePoolScale = static_cast<int32_t>(
        RemasterRegister({"limits", "particle_pool_scale", L"Particle pool size (x)", S::Slider, 1, 16, 1, {}, 0, true,
                          nullptr},
                         1, 16));
    g_astarCapacity = static_cast<uint32_t>(
        RemasterRegister({"limits", "path_nodes", L"AI path search nodes", S::Slider, 2000, 64000, 1000, {}, 0, true,
                          nullptr},
                         2000, 16000));
    if (g_astarCapacity < 2000)
        g_astarCapacity = 2000;
    g_log = ConfigInt("limits", "log", 0) != 0;
    Log("limits: force_pool %u, particle_pool_scale %d, path_nodes %u", g_forcePoolCapacity, g_particlePoolScale,
        g_astarCapacity);
}

// Frames on which each particle bank (fx system Game+0xa90, pools +0x1c / +0x20 / +0x24) had no free particle: the
// free list (sentinel at pool +0xc) empty. Counted here because original code that is not recovered also takes
// particles off the list, past the g_particleDrops counter.
uint32_t g_bankFullFrames[3];

// Main thread, once per presented frame.
void LimitsOnFrame()
{
    if (!g_log)
        return;
    if (g_game)
        if (uint8_t* fx = Field<uint8_t*>(g_game, 0xa90))
            for (int b = 0; b < 3; ++b)
                if (uint8_t* pool = Field<uint8_t*>(fx, 0x1c + b * 4))
                    if (Field<uint8_t*>(pool, 0xc) == pool + 0xc)
                        ++g_bankFullFrames[b];
    static DWORD last;
    static uint32_t force, particles, nodes, full, frames;
    ++frames;
    const DWORD now = GetTickCount();
    if (now - last < 2000)
        return;
    const float fps = last ? frames * 1000.0f / static_cast<float>(now - last) : 0.0f;
    last = now, frames = 0;
    Log("limits: %.1f fps", fps);
    const uint32_t fullNow = g_bankFullFrames[0] + g_bankFullFrames[1] + g_bankFullFrames[2];
    if (g_forcePoolDrops != force || g_particleDrops != particles || g_astarRefused != nodes || fullNow != full)
    {
        force = g_forcePoolDrops, particles = g_particleDrops, nodes = g_astarRefused, full = fullNow;
        Log("limits: dropped so far: forces %u, particles %u, path nodes %u; frames with a full particle bank %u / %u "
            "/ %u",
            force, particles, nodes, g_bankFullFrames[0], g_bankFullFrames[1], g_bankFullFrames[2]);
        if (void* p = g_particleDropPool)
        {
            uint8_t* fx = g_game ? Field<uint8_t*>(g_game, 0xa90) : nullptr;
            int bank = -1;
            for (int b = 0; fx && b < 3; ++b)
                if (Field<void*>(fx, 0x1c + b * 4) == p)
                    bank = b;
            Log("limits: last refusing pool %p: %d particles, owner %08x, fx bank %d", p, Field<int32_t>(p, 4),
                Field<uint32_t>(p, 0), bank);
        }
    }
}
