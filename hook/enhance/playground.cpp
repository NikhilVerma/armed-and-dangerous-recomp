// Graphics playground (the player, 2026-10-12: "a graphics playground where you can render a grunt in the game's real
// scenery ... specify a scenario, replicate it and test it"). A [playground] section in and.ini (written by
// tools/playground.py from a scene file) sets up a controlled scene after the level loads. Frames count from the end
// of the level load ([test] scenario_load; testscenario.cpp TestFramesSinceLoad), so runs repeat with the virtual
// clock. Everything is off without enabled=1.
//
//   enabled=1
//   skip_briefing=1                 dismiss the mission-start text (the "Press Mouse 1 to continue" pause) by itself
//   camera=x,y,z,yaw,pitch          pin the camera (world units, Z up; yaw/pitch in degrees) after Camera_Update
//   camera_look=x,y,z,tx,ty,tz      or: pin it at x,y,z looking at tx,ty,tz
//   camera_orbit=N,radius,eye_z,target_z,start_deg,deg_per_frame
//                                   turntable around spawn N (follows it; heights above its origin)
//   camera_fov=60                   vertical FOV in degrees (0 = the game's)
//   player=hide | x,y,z             hide the player, or teleport it once after the load
//   spawnN=template,x,y,z,yaw,frame create a template (lev_1_m1: grunt, grunt_rifle, captain, grunt_barracks_mini,
//                                   v_house_*, pine_tree ...) at a frame (N = 1..64)
//   killN=spawn,frame               destroy spawn N (Object_Kill: death effect and husk) at a frame
//   fxN=name,x,y,z,frame            play an effect by name at a frame (fx_explosion_barracks ...)
//   sun=azimuth,elevation,r,g,b     replace the level's sun (light slot 0): degrees, colour 0..n (shadows follow it)
//   ambient=r,g,b                   the sun's ambient term (shader constant c3 is the sum of the lights' ambient)
//   key=azimuth,elevation,r,g,b     a second directional light (replaces the level's fill): key or rim light
//   point=x,y,z,r,g,b,halfrange     a point light (the shaders take one): Attenuation1 = 1/halfrange
//   point_follow=N                  the point light's x,y,z are an offset from spawn N
//   isolate=1                       switch off the level's lights other than the sun and these
//   The shaders take two directional lights and one point light, in slot order (Renderer_UpdateLightConstants
//   0x004ff2e0; its g_beforeLightConstants hook writes the lights into the renderer's slots +0x169c before each update).
//   log_spawn=N                     log spawn N's physics body position every frame (300 frames; stops when killed)
//   log_player=1                    log the player position every second (to find coordinates for a scene)
//
// API (scouted 2026-10-12): ObjectSystem_CreateObject 0x0045f020, Object_SetPosition 0x00457870, SetVisible vt+0xf4
// (0x004536b0), Object_Kill vt+0xd4 (0x00455640), Fx_Create 0x0041cc60, Game_SetPaused 0x004031e0, camera Game+0xa68
// (world matrix +0x8c, view Game+0x704 = its inverse, FOV +0xf4 radians), player Game+0x748.
#include <windows.h>
#include <d3d9.h>

#include <cmath>
#include <cstdio>
#include <cstring>

#include "config.h"
#include "game.h"
#include "game/game_object.h"
#include "gen/orig_calls.h"
#include "hooks.h"
#include "log.h"
#include "settings.h"

struct Renderer;
extern void (*g_beforeLightConstants)(Renderer* r);  // src/renderer/auto_004fbe00.cpp
int TestFramesSinceLoad();  // hook/src/testscenario.cpp
uint64_t Object_SetPosition(uint32_t eax, const float* position, uint8_t* obj);  // src/physics/physics_02.cpp

namespace
{
constexpr int kSlots = 64;
struct Spawn
{
    char tmpl[32] = {};
    float pos[3] = {};
    float yaw = 0.0f;
    int frame = -1;
    void* obj = nullptr;
    bool done = false;
};
struct Kill
{
    int spawn = -1, frame = -1;
    bool done = false;
};
struct Fx
{
    char name[64] = {};
    float pos[3] = {};
    int frame = -1;
    bool done = false;
};

int g_logSpawn = -1;
bool g_on, g_skipBriefing, g_hidePlayer, g_movePlayer, g_playerDone, g_logPlayer;
bool g_pinCamera;
float g_camEye[3], g_camYaw, g_camPitch, g_camLook[3], g_camFov;
bool g_camLookAt;
int g_orbitSpawn = -1;
float g_orbit[5];  // radius, eye z, target z, start degrees, degrees per frame
int g_frameNow = -1;
float g_playerPos[3];
Spawn g_spawns[kSlots];
Kill g_kills[kSlots];
Fx g_fx[kSlots];

struct FakeLights
{
    bool sun, ambient, key, point, isolate;
    float sunAz, sunEl, sunColor[3], ambientColor[3], keyAz, keyEl, keyColor[3], pointPos[3], pointColor[3];
    float pointHalf = 10.0f;
    int pointFollow = -1;
} g_lights;
bool g_anyLight;

uint32_t u(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

int Floats(const char* s, float* out, int n)
{
    int k = 0;
    while (k < n && s && *s)
    {
        char* end;
        out[k++] = std::strtof(s, &end);
        s = std::strchr(end, ',');
        if (s)
            ++s;
    }
    return k;
}

// Game matrix: 17 floats (row-major 3x4 + translation at 3, 7, 11; flags word at +0x40), Z up.
void PoseMatrix(float m[17])
{
    float fwd[3];
    if (g_camLookAt)
    {
        for (int i = 0; i < 3; ++i)
            fwd[i] = g_camLook[i] - g_camEye[i];
    }
    else
    {
        const float y = g_camYaw * 0.0174532925f, p = g_camPitch * 0.0174532925f;
        fwd[0] = std::cos(p) * std::cos(y), fwd[1] = std::cos(p) * std::sin(y), fwd[2] = std::sin(p);
    }
    float l = std::sqrt(fwd[0] * fwd[0] + fwd[1] * fwd[1] + fwd[2] * fwd[2]);
    if (l < 1e-6f)
        fwd[0] = 1, fwd[1] = fwd[2] = 0, l = 1;
    for (float& f : fwd)
        f /= l;
    // Camera space is right-handed with -z forward (as the game's view matrices): columns right, up, back.
    float right[3] = {fwd[1], -fwd[0], 0.0f};
    l = std::sqrt(right[0] * right[0] + right[1] * right[1]);
    if (l < 1e-6f)
        right[0] = 1, right[1] = 0, l = 1;
    right[0] /= l, right[1] /= l;
    const float up[3] = {right[1] * fwd[2] - right[2] * fwd[1], right[2] * fwd[0] - right[0] * fwd[2],
                         right[0] * fwd[1] - right[1] * fwd[0]};
    std::memset(m, 0, 17 * sizeof(float));
    for (int r = 0; r < 3; ++r)
    {
        m[r * 4 + 0] = right[r];
        m[r * 4 + 1] = up[r];
        m[r * 4 + 2] = -fwd[r];
        m[r * 4 + 3] = g_camEye[r];
    }
    m[15] = 1.0f;
    reinterpret_cast<uint32_t*>(m)[16] = 0x2f;  // a general 4x4 (see ui_aspect.cpp on the flags word)
}

void SetDirection(D3DLIGHT9& l, float az, float el)
{
    const float a = az * 0.0174532925f, e = el * 0.0174532925f;  // towards the light; the light travels the other way
    l.Direction = {-std::cos(e) * std::cos(a), -std::cos(e) * std::sin(a), -std::sin(e)};
}

void Colour(D3DCOLORVALUE& c, const float* rgb) { c = {rgb[0], rgb[1], rgb[2], 1.0f}; }

void Playground_UpdateLights(Renderer* renderer)
{
    void* r = renderer;
    auto* slots = reinterpret_cast<D3DLIGHT9*>(static_cast<uint8_t*>(r) + 0x169c);
    uint8_t* enabled = static_cast<uint8_t*>(r) + 0x1a44;
    static int calls;
    if (calls++ == 0)  // what the level put in the slots (before this override)
        for (int i = 0; i < 8; ++i)
            if (enabled[i])
                Log("playground: light slot %d type %d diffuse %.2f %.2f %.2f ambient %.2f %.2f %.2f dir %.2f %.2f %.2f "
                    "(call %d, shader path %u)",
                    i, slots[i].Type, slots[i].Diffuse.r, slots[i].Diffuse.g, slots[i].Diffuse.b, slots[i].Ambient.r,
                    slots[i].Ambient.g, slots[i].Ambient.b, slots[i].Direction.x, slots[i].Direction.y,
                    slots[i].Direction.z, calls, Field<uint32_t>(r, 0x33c));
    // lev_1_m1 (logged 2026-10-12): slot 0 the sun (diffuse 1.2 1.2 0.9), slot 1 an ambient-only light (diffuse 0,
    // ambient 0.59 0.71 0.78), slot 2 a blue fill from the other side (0.13 0.21 0.32). The shaders take the first two
    // directional lights with diffuse, the first other light, and the sum of every enabled light's ambient.
    if (g_lights.sun)
    {
        D3DLIGHT9& s = slots[0];
        s.Type = D3DLIGHT_DIRECTIONAL, SetDirection(s, g_lights.sunAz, g_lights.sunEl);
        Colour(s.Diffuse, g_lights.sunColor), Colour(s.Specular, g_lights.sunColor), enabled[0] = 1;
    }
    if (g_lights.ambient)
    {
        for (int i = 0; i < 8; ++i)
            slots[i].Ambient = {0, 0, 0, 0};
        Colour(slots[0].Ambient, g_lights.ambientColor), enabled[0] = 1;
    }
    auto lit = [&](int i) {
        const D3DCOLORVALUE& d = slots[i].Diffuse;
        return enabled[i] && (d.r > 0 || d.g > 0 || d.b > 0);
    };
    if (g_lights.key)  // takes the place of the level's second directional light (the fill)
    {
        int k = -1, firstFree = -1;
        for (int i = 1; i < 8 && k < 0; ++i)
            if (lit(i) && slots[i].Type == D3DLIGHT_DIRECTIONAL)
                k = i;
            else if (!enabled[i] && firstFree < 0)
                firstFree = i;
        if (k < 0)
            k = firstFree >= 0 ? firstFree : 7;
        const D3DCOLORVALUE keepAmbient = slots[k].Ambient;
        D3DLIGHT9 l{};
        l.Type = D3DLIGHT_DIRECTIONAL;
        SetDirection(l, g_lights.keyAz, g_lights.keyEl);
        Colour(l.Diffuse, g_lights.keyColor), Colour(l.Specular, g_lights.keyColor);
        l.Ambient = keepAmbient;
        slots[k] = l, enabled[k] = 1;
    }
    if (g_lights.point)  // the first non-directional light with diffuse; else a free slot
    {
        int k = -1;
        for (int i = 0; i < 8 && k < 0; ++i)
            if (lit(i) && slots[i].Type != D3DLIGHT_DIRECTIONAL)
                k = i;
        for (int i = 7; i >= 1 && k < 0; --i)
            if (!enabled[i])
                k = i;
        if (k < 0)
            k = 7;
        float at[3] = {g_lights.pointPos[0], g_lights.pointPos[1], g_lights.pointPos[2]};
        void* obj = g_lights.pointFollow >= 0 ? g_spawns[g_lights.pointFollow].obj : nullptr;
        if (obj && Field<void*>(obj, 0x1c4))
        {
            alignas(16) float o[4] = {};
            orig::fn_00457930(u(obj), u(o));  // Body_getPosition
            at[0] += o[0], at[1] += o[1], at[2] += o[2];
        }
        D3DLIGHT9 pl{};
        pl.Type = D3DLIGHT_POINT;
        Colour(pl.Diffuse, g_lights.pointColor), Colour(pl.Specular, g_lights.pointColor);
        pl.Position = {at[0], at[1], at[2]};
        pl.Range = 1e6f, pl.Attenuation0 = 1.0f, pl.Attenuation1 = 1.0f / g_lights.pointHalf;  // as Fx_SetLight
        slots[k] = pl, enabled[k] = 1;
    }
    if (g_lights.isolate)  // only the sun (with the ambient folded into it when ambient= is given)
        for (int i = 1; i < 8; ++i)
            if (!(g_lights.key && slots[i].Type == D3DLIGHT_DIRECTIONAL && lit(i)) &&
                !(g_lights.point && slots[i].Type == D3DLIGHT_POINT))
                enabled[i] = 0;
    Field<uint8_t>(r, 0x1680) = 1;  // re-derive the constants from the slots
}

using CameraUpdate_t = uint32_t(__stdcall*)(void* camera, float dt);
CameraUpdate_t g_nextCameraUpdate;
uint32_t __stdcall Playground_CameraUpdate(void* camera, float dt)
{
    uint32_t r = g_nextCameraUpdate(camera, dt);
    if (g_orbitSpawn >= 0 && g_frameNow >= 0)
    {
        void* obj = g_spawns[g_orbitSpawn].obj;
        if (obj && Field<void*>(obj, 0x1c4))
        {
            alignas(16) float at[4] = {};
            orig::fn_00457930(u(obj), u(at));  // Body_getPosition
            const float a = (g_orbit[3] + g_orbit[4] * static_cast<float>(g_frameNow)) * 0.0174532925f;
            g_camEye[0] = at[0] + g_orbit[0] * std::cos(a), g_camEye[1] = at[1] + g_orbit[0] * std::sin(a);
            g_camEye[2] = at[2] + g_orbit[1];
            g_camLook[0] = at[0], g_camLook[1] = at[1], g_camLook[2] = at[2] + g_orbit[2];
            g_camLookAt = g_pinCamera = true;
        }
    }
    if (g_pinCamera && g_game && camera == Field<void*>(g_game, 0xa68) && !g_game->loadingLevel)
    {
        float* world = &Field<float>(camera, 0x8c);
        PoseMatrix(world);
        orig::fn_004e2b40(u(world), u(&Field<float>(g_game, 0x704)));  // Matrix_Inverse(src eax, dst ecx)
        if (g_camFov > 0.0f)
            Field<float>(camera, 0xf4) = g_camFov * 0.0174532925f;
    }
    return r;
}

void Load()
{
    char key[16], v[160];
    g_skipBriefing = ConfigInt("playground", "skip_briefing", 1) != 0;
    g_logPlayer = ConfigInt("playground", "log_player", 0) != 0;
    g_logSpawn = ConfigInt("playground", "log_spawn", 0) - 1;
    ConfigString("playground", "camera", "", v, sizeof(v));
    float c[6];
    if (Floats(v, c, 5) == 5)
        g_pinCamera = true, std::memcpy(g_camEye, c, 12), g_camYaw = c[3], g_camPitch = c[4];
    ConfigString("playground", "camera_look", "", v, sizeof(v));
    if (Floats(v, c, 6) == 6)
        g_pinCamera = g_camLookAt = true, std::memcpy(g_camEye, c, 12), std::memcpy(g_camLook, c + 3, 12);
    ConfigString("playground", "camera_orbit", "", v, sizeof(v));
    float o[6];
    if (Floats(v, o, 6) == 6)
        g_orbitSpawn = static_cast<int>(o[0]) - 1, std::memcpy(g_orbit, o + 1, sizeof(g_orbit));
    float l[7];
    ConfigString("playground", "sun", "", v, sizeof(v));
    if (Floats(v, l, 5) == 5)
        g_lights.sun = true, g_lights.sunAz = l[0], g_lights.sunEl = l[1], std::memcpy(g_lights.sunColor, l + 2, 12);
    ConfigString("playground", "ambient", "", v, sizeof(v));
    if (Floats(v, l, 3) == 3)
        g_lights.ambient = true, std::memcpy(g_lights.ambientColor, l, 12);
    ConfigString("playground", "key", "", v, sizeof(v));
    if (Floats(v, l, 5) == 5)
        g_lights.key = true, g_lights.keyAz = l[0], g_lights.keyEl = l[1], std::memcpy(g_lights.keyColor, l + 2, 12);
    ConfigString("playground", "point", "", v, sizeof(v));
    if (Floats(v, l, 7) == 7)
        g_lights.point = true, std::memcpy(g_lights.pointPos, l, 12), std::memcpy(g_lights.pointColor, l + 3, 12),
        g_lights.pointHalf = l[6] > 0.0f ? l[6] : 10.0f;
    g_lights.pointFollow = ConfigInt("playground", "point_follow", 0) - 1;
    g_lights.isolate = ConfigInt("playground", "isolate", 0) != 0;
    g_anyLight = g_lights.sun || g_lights.ambient || g_lights.key || g_lights.point || g_lights.isolate;
    g_camFov = ConfigFloat("playground", "camera_fov", 0.0f);
    ConfigString("playground", "player", "", v, sizeof(v));
    if (!_stricmp(v, "hide"))
        g_hidePlayer = true;
    else if (Floats(v, g_playerPos, 3) == 3)
        g_movePlayer = true;
    for (int i = 0; i < kSlots; ++i)
    {
        std::snprintf(key, sizeof(key), "spawn%d", i + 1);
        ConfigString("playground", key, "", v, sizeof(v));
        if (v[0])
        {
            const char* comma = std::strchr(v, ',');
            const size_t n = comma ? static_cast<size_t>(comma - v) : std::strlen(v);
            Spawn& s = g_spawns[i];
            std::memcpy(s.tmpl, v, n < 31 ? n : 31);
            float f[5] = {0, 0, 0, 0, 1};
            Floats(comma ? comma + 1 : "", f, 5);
            std::memcpy(s.pos, f, 12), s.yaw = f[3], s.frame = static_cast<int>(f[4]);
        }
        std::snprintf(key, sizeof(key), "kill%d", i + 1);
        ConfigString("playground", key, "", v, sizeof(v));
        float k[2];
        if (Floats(v, k, 2) == 2)
            g_kills[i].spawn = static_cast<int>(k[0]) - 1, g_kills[i].frame = static_cast<int>(k[1]);
        std::snprintf(key, sizeof(key), "fx%d", i + 1);
        ConfigString("playground", key, "", v, sizeof(v));
        if (v[0])
        {
            const char* comma = std::strchr(v, ',');
            const size_t n = comma ? static_cast<size_t>(comma - v) : std::strlen(v);
            Fx& x = g_fx[i];
            std::memcpy(x.name, v, n < 63 ? n : 63);
            float f[4] = {0, 0, 0, 1};
            Floats(comma ? comma + 1 : "", f, 4);
            std::memcpy(x.pos, f, 12), x.frame = static_cast<int>(f[3]);
        }
    }
}

void* Player() { return g_game ? Field<void*>(g_game, 0x748) : nullptr; }

void DoSpawn(Spawn& s, int index)
{
    float m[17] = {};
    const float y = s.yaw * 0.0174532925f;
    m[0] = std::cos(y), m[1] = -std::sin(y), m[4] = std::sin(y), m[5] = std::cos(y), m[10] = 1.0f, m[15] = 1.0f;
    m[3] = s.pos[0], m[7] = s.pos[1], m[11] = s.pos[2];
    reinterpret_cast<uint32_t*>(m)[16] = 0x2f;
    // ObjectSystem_CreateObject (eax = class 0: from the template; ecx = name; stack: manager, matrix, 0x3ff, 0, 0,
    // 0, 0, 0xaa, 0). Returns the object or 0.
    const uint32_t obj = orig::fn_0045f020(0, u(s.tmpl), Field<uint32_t>(g_game, 0xa84), u(m), 0x3ff, 0, 0, 0, 0,
                                           0xaa, 0);
    s.obj = reinterpret_cast<void*>(static_cast<uintptr_t>(obj));
    Log("playground: spawn%d %s at %.1f %.1f %.1f -> %p", index + 1, s.tmpl, s.pos[0], s.pos[1], s.pos[2], s.obj);
}

void DoFx(Fx& x, int index)
{
    alignas(16) uint8_t params[0x80] = {};
    Field<uint32_t>(params, 0) = 8;  // position at +0xc
    std::memcpy(params + 0xc, x.pos, 12);
    const uint32_t r = orig::fn_0041cc60(1, Field<uint32_t>(g_game, 0xa90), u(x.name), u(params));
    Log("playground: fx%d %s at %.1f %.1f %.1f -> %08x", index + 1, x.name, x.pos[0], x.pos[1], x.pos[2], r);
}
}  // namespace

void PlaygroundInit()
{
    g_on = ConfigInt("playground", "enabled", 0) != 0;
    if (!g_on)
        return;
    Load();
    if (g_pinCamera || g_orbitSpawn >= 0)
        HookWrap("Camera_Update", reinterpret_cast<void*>(Playground_CameraUpdate),
                 reinterpret_cast<void**>(&g_nextCameraUpdate));
    if (g_anyLight)
        g_beforeLightConstants = Playground_UpdateLights;
    Log("playground: on (lights %s, camera %s, player %s)", g_anyLight ? "fake" : "level", g_pinCamera || g_orbitSpawn >= 0 ? "pinned" : "game",
        g_hidePlayer ? "hidden" : g_movePlayer ? "moved" : "as is");
}

// Main thread, once per presented frame (after Present: outside the game's own update).
void PlaygroundOnFrame()
{
    if (!g_on || !g_game)
        return;
    // Frames since the level load ended: the scenario's count under [test] load, else counted here.
    static bool loading;
    static int ownFrame = -1;
    const bool nowLoading = g_game->loadingLevel != 0;
    if (loading && !nowLoading)
        ownFrame = 0;
    else if (ownFrame >= 0 && !nowLoading)
        ++ownFrame;
    loading = nowLoading;
    const int scenario = TestFramesSinceLoad();
    const int f = scenario >= 0 ? scenario : ownFrame;
    g_frameNow = nowLoading ? -1 : f;
    if (f < 0 || nowLoading)
        return;
    // The mission-start text pauses the game (Game+0x15dc set, pause count Game+0x2684): dismiss it once.
    static bool briefingDone;
    if (g_skipBriefing && !briefingDone && Field<uint8_t>(g_game, 0x15dc) && Field<int32_t>(g_game, 0x2684) > 0)
    {
        briefingDone = true;
        orig::fn_004031e0(1, u(g_game), 0);  // Game_SetPaused (bl = 1: resume)
        Log("playground: briefing dismissed at frame %d", f);
    }
    void* player = Player();
    if (player && !g_playerDone && f >= 1)
    {
        if (g_hidePlayer)
            orig::fn_004536b0(u(player), 0);  // SetVisible(0)
        if (g_movePlayer && Field<void*>(player, 0x1c4))
            Object_SetPosition(0, g_playerPos, static_cast<uint8_t*>(player));  // moves the rigid body
        g_playerDone = true;
    }
    if (g_hidePlayer && player && (f % 30) == 0)
        orig::fn_004536b0(u(player), 0);  // the game shows it again on some state changes
    for (int i = 0; i < kSlots; ++i)
    {
        Spawn& s = g_spawns[i];
        if (s.tmpl[0] && !s.done && f >= s.frame)
            s.done = true, DoSpawn(s, i);
        Kill& k = g_kills[i];
        if (k.spawn >= 0 && !k.done && f >= k.frame)
        {
            k.done = true;
            void* obj = k.spawn < kSlots ? g_spawns[k.spawn].obj : nullptr;
            if (obj)
            {
                orig::fn_00455640(u(obj), 0);  // Object_Kill
                if (k.spawn == g_logSpawn)
                    g_logSpawn = -1;  // the object may be freed and its memory reused
                Log("playground: kill%d spawn%d at frame %d", i + 1, k.spawn + 1, f);
            }
        }
        Fx& x = g_fx[i];
        if (x.name[0] && !x.done && f >= x.frame)
            x.done = true, DoFx(x, i);
    }
    if (g_logSpawn >= 0 && g_logSpawn < kSlots)
    {
        static int logged;
        void* obj = g_spawns[g_logSpawn].obj;
        if (obj && Field<void*>(obj, 0x1c4) && logged < 300)
        {
            alignas(16) float pos[4] = {};
            orig::fn_00457930(u(obj), u(pos));  // Body_getPosition
            Log("playground: spawn%d body %.3f %.3f %.3f (frame %d)", g_logSpawn + 1, pos[0], pos[1], pos[2], f);
            ++logged;
        }
    }
    if (g_logPlayer && player && Field<void*>(player, 0x1c4) && (f % 60) == 0)
    {
        alignas(16) float pos[4] = {};
        orig::fn_00457930(u(player), u(pos));  // Body_getPosition
        const float* cam = Field<void*>(g_game, 0xa68) ? &Field<float>(Field<void*>(g_game, 0xa68), 0x8c) : nullptr;
        if (cam)  // camera position and forward (-z column), for camera_look
            Log("playground: player at %.2f %.2f %.2f, camera %.2f %.2f %.2f looking %.2f %.2f %.2f (frame %d)", pos[0],
                pos[1], pos[2], cam[3], cam[7], cam[11], -cam[2], -cam[6], -cam[10], f);
    }
}
