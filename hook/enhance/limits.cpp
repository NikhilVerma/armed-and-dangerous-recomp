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
//   physics_fixed_step=1    above 60 fps the original steps physics 1/60 s every frame (x2.41 too fast at 144 Hz);
//                           this steps whole 1/60 s steps from accumulated frame time instead (PhysicsManager_Step
//                           0x00411d40). Objects then move at 60 Hz (no interpolation yet).
//   physics_solver_iterations=8  Havok solver iterations per step (original 4): steadier stacks and resting piles.
//   physics_interpolation=1 draw body-driven objects between their last two physics steps above 60 fps (one step
//                           of display lag; translation only; not the player, ragdolls, or objects that move every
//                           frame such as walking characters). interp_log=1 logs one object's true and drawn position.
//   log=1                   log the drop counters when they change (every 2 s)
//
// All take effect at the next level load (the pools are made when a level loads).
#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

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
extern bool g_astarEnlarged;
extern double g_physicsGameTime, g_physicsSimTime;      // src/physics/physics_01.cpp
extern bool g_physicsFixedStep;
extern uint32_t g_solverIterations;
extern uint32_t g_physicsStepId;                                                  // src/physics/physics_01.cpp
extern float g_physicsAlpha;
extern const float* (*g_drawMatrix)(uint8_t* obj, const float* matrix);           // src/engine/missing_01.cpp
unsigned PresentedFrames();                                                       // hook/src/d3d9_hooks.cpp

namespace
{
bool g_log;

// Render interpolation ([limits] physics_interpolation). Above 60 fps the world steps every few frames
// (physics_fixed_step), so body-driven objects jumped at 60 Hz. Each object's drawn position is blended between its
// positions after the last two steps by how far the frame is into the next step (one step of display lag). Drawing
// only: gameplay keeps the true matrix. Translation only; the player (the camera follows its true position) and
// ragdolls (bones) are left alone.
struct Track
{
    float prev[3], cur[3];
    uint32_t step;
    unsigned frame;
    unsigned movedOffStep;  // the last frame it moved without a physics step (characters move every frame)
};
bool g_interp, g_interpLog;
std::unordered_map<uint8_t*, Track> g_tracks;
std::vector<float> g_arena;  // 17 floats per interpolated matrix, reset each frame; never grows inside a frame
size_t g_arenaUsed;
unsigned g_arenaFrame = ~0u;
uint8_t* g_logObj;
int g_logLeft = 240;

const float* DrawMatrix(uint8_t* obj, const float* m)
{
    if (!g_interp || !g_game || m != reinterpret_cast<const float*>(obj + 0x32c) || !Field<void*>(obj, 0x1c4) ||
        obj == Field<uint8_t*>(g_game, 0x748) || Field<uint8_t>(obj, 0x421))
        return m;
    const unsigned frame = PresentedFrames();
    if (frame != g_arenaFrame)
    {
        g_arenaFrame = frame, g_arenaUsed = 0;
        if (g_tracks.size() > 20000)
            g_tracks.clear();
    }
    const float pos[3] = {m[3], m[7], m[11]};
    Track& t = g_tracks[obj];
    const uint32_t step = g_physicsStepId;
    const bool seen = t.frame + 1 == frame || t.frame == frame;
    if (t.step != step)
    {
        if (seen && t.step + 1 == step)
            std::memcpy(t.prev, t.cur, sizeof(t.prev));
        else
            std::memcpy(t.prev, pos, sizeof(t.prev));  // new, unseen for a while, or several steps at once: snap
        std::memcpy(t.cur, pos, sizeof(t.cur));
        t.step = step;
    }
    else if (!seen || pos[0] != t.cur[0] || pos[1] != t.cur[1] || pos[2] != t.cur[2])
    {
        // Moved without a step: moved every frame (characters: already smooth), teleported, or reused memory.
        std::memcpy(t.prev, pos, sizeof(t.prev));
        std::memcpy(t.cur, pos, sizeof(t.cur));
        t.movedOffStep = frame;
    }
    t.frame = frame;
    const float a = g_physicsAlpha < 0.0f ? 0.0f : g_physicsAlpha > 1.0f ? 1.0f : g_physicsAlpha;
    if (a >= 1.0f || frame - t.movedOffStep < 60 ||
        (t.prev[0] == t.cur[0] && t.prev[1] == t.cur[1] && t.prev[2] == t.cur[2]))
        return m;
    if (g_arenaUsed + 17 > g_arena.size())
        return m;
    float* out = &g_arena[g_arenaUsed];
    g_arenaUsed += 17;
    std::memcpy(out, m, 17 * sizeof(float));
    out[3] = t.prev[0] + (t.cur[0] - t.prev[0]) * a;
    out[7] = t.prev[1] + (t.cur[1] - t.prev[1]) * a;
    out[11] = t.prev[2] + (t.cur[2] - t.prev[2]) * a;
    if (g_interpLog && g_logLeft > 0 && (!g_logObj || g_logObj == obj))
    {
        g_logObj = obj;
        --g_logLeft;
        Log("limits: interp frame %u step %u alpha %.2f true %.3f %.3f drawn %.3f %.3f", frame, step, a, pos[0], pos[1],
            out[3], out[7]);
    }
    return out;
}
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
    g_physicsFixedStep =
        RemasterRegister({"limits", "physics_fixed_step", L"Physics at real speed above 60 fps", S::Toggle, 0, 1, 1, {},
                          0, false, [](float v) { g_physicsFixedStep = v != 0.0f; }},
                         0, 1) != 0.0f;
    g_solverIterations = static_cast<uint32_t>(
        RemasterRegister({"limits", "physics_solver_iterations", L"Physics solver iterations", S::Slider, 4, 16, 1, {}, 0,
                          true, nullptr},
                         4, 8));
    g_interp = RemasterRegister({"limits", "physics_interpolation", L"Smooth physics above 60 fps", S::Toggle, 0, 1, 1,
                                 {}, 0, false, [](float v) { g_interp = v != 0.0f; }},
                                0, 1) != 0.0f;  // checked in the playground interp scene at 144 Hz
    g_interpLog = ConfigInt("limits", "interp_log", 0) != 0;
    g_arena.resize(17 * 16384);
    g_drawMatrix = DrawMatrix;
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
    static bool astarLogged;
    if (g_astarEnlarged && !astarLogged)
    {
        astarLogged = true;
        Log("limits: path search arrays enlarged to %u nodes", g_astarCapacity);
    }
    // Texture buckets in use (TextureBuckets_Init 0x0050a200: 1024; used list at renderer +0x494).
    static uint32_t bucketPeak;
    if (uint8_t* r = g_game ? Field<uint8_t*>(g_game, 0x38) : nullptr)
    {
        uint32_t used = 0;
        auto* head = r + 0x494;
        for (auto* n = Field<uint8_t*>(head, 0); n && n != head && used < 4096; n = Field<uint8_t*>(n, 0))
            ++used;
        if (used > bucketPeak)
        {
            bucketPeak = used;
            Log("limits: texture buckets in use %u of 1024 (new peak)", used);
        }
    }
    const float fps = last ? frames * 1000.0f / static_cast<float>(now - last) : 0.0f;
    last = now, frames = 0;
    Log("limits: %.1f fps; physics simulated %.2f s for %.2f s of game time (x%.2f)", fps, g_physicsSimTime,
        g_physicsGameTime, g_physicsGameTime > 0.0 ? g_physicsSimTime / g_physicsGameTime : 0.0);
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
