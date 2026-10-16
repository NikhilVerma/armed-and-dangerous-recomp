// Lockstep state trace of the original game, for comparing the remaster engine with it on the same inputs
// (tools/oracle.py, docs/remaster-engine.md "Oracle runs"). The remaster writes the same schema (engine/src/oracle.rs).
//
// [test] keys_from_play=1     scripted keys (input_hooks.cpp, frame mode) count "play frames": frame 0 is
//                             play_delay frames after the level's first paused message (the briefing) appears, so a
//                             scenario's own frame 0 key (ENTER) dismisses it. Before that the clock is negative.
// [test] play_delay=30        see above
// [test] play_warp=x,y,z      at play frame -10 (the briefing still up) Roman is put there (Object_SetPosition
//                             0x00457870, as [test] warp)
// [test] trace_from=-N        start the trace N frames before play frame 0 (down to the level load: lines before the
//                             briefing are kept in memory until it appears and fixes the play frames)
// [test] play_frames=N        terminate the process after play frame N (the trace flushed)
// [test] trace_every=N        and-trace.jsonl: from play frame trace_from, every N frames one JSON line (every frame
//                             before play frame 0):
//   {"f": play frame, "t": game seconds since the first line, "paused": 0/1,
//    "roman": {"pos": [x, y, z], "heading": degrees (the model's facing, atan2(x, y) of its -Y axis), "health",
//              "max_health", "weapon": id (descriptors Game+0x2770 + id * 0x150: 0 machine gun, 2 rifle), "clip",
//              "ammo", "stickies"},
//    "grunts": [{"id": instance id (+0x396), "pos", "health", "state": AI state +0x460, "target": id | "roman" |
//               null, "class": vtable}],
//    "vehicles": [{"id", "kind": goliath / saucer / mini_zeppelin / colossus / rocket_tower / rocket_tower_turret /
//                 rocket_bud (by vtable), "pos", "health", "state"}],
//    "companions": [[x, y, z, health] | null, ...] (Jonesy, Q),
//    "mission": {"state": level state (1 playing, 2/10 won, 3..9 failed), "objective": compass tag Game+0x2658},
//    "triggers": [ids sent since the last line], "trigger_senders": [their senders' instance ids], "deaths": [instance ids of placed objects that died],
//    "sounds": [definition names started]}
// Game axes (Z up). Offsets: docs/subsystems/player.md / grunt-ai.md / mission.md, and the comments below.
#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "config.h"
#include "game/game_object.h"
#include "gen/orig_calls.h"
#include "hooks.h"
#include "log.h"

int TestFramesSinceLoad();  // testscenario.cpp

namespace
{
const uint8_t* B(const void* p)
{
    return static_cast<const uint8_t*>(p);
}
template <class T>
T At(const void* p, unsigned off)
{
    return *reinterpret_cast<const T*>(B(p) + off);
}

int g_traceEvery, g_playFrames;
int g_playDelay = 30;
bool g_fromPlay;
int g_briefFrame = -1;  // frame since load at which the briefing appeared
float g_warp[3];
bool g_warpOn, g_warpDone;
int g_traceFrom;
FILE* g_out;
bool g_traced;      // a line has been written or kept
float g_clockFirst;  // game clock at the first line
// lines made before the briefing fixed the play frames: frame since the load, game clock, the line after "t"
struct Pending
{
    int since;
    float clock;
    std::string body;
};
std::vector<Pending> g_pending;
std::vector<uint32_t> g_triggers, g_senders, g_deaths;
std::vector<std::string> g_sounds;

void* Player()
{
    return g_game ? *reinterpret_cast<void* const*>(B(g_game) + 0x748) : nullptr;
}

void GetPos(void* obj, float* out)
{
    using Fn = void(__thiscall*)(void*, float*);
    reinterpret_cast<Fn>((*reinterpret_cast<void***>(obj))[0x54 / 4])(obj, out);
}

// The game clock (0x00604a98, Timer_Tick 0x004e47e0) stops counting while paused: Game_SetPaused 0x004031e0 keeps the
// value at the pause in Game+0x2688 and subtracts the paused span on resume (engine_01.cpp:210-260).
float GameClock()
{
    const int pauses = At<int>(g_game, 0x2684);
    return pauses ? At<float>(g_game, 0x2688) : *reinterpret_cast<const float*>(0x00604a98);
}

bool Readable(uint32_t p, unsigned n)
{
    return p >= 0x10000 && !IsBadReadPtr(reinterpret_cast<const void*>(p), n);
}

void Num(std::string& o, float v)
{
    char b[32];
    if (!std::isfinite(v))
        v = 0;
    snprintf(b, sizeof(b), "%.2f", v);
    o += b;
}

void Vec(std::string& o, const float* v)
{
    o += '[';
    Num(o, v[0]), o += ',', Num(o, v[1]), o += ',', Num(o, v[2]);
    o += ']';
}

int Id(const void* obj)
{
    return At<uint16_t>(obj, 0x396);  // the 1-based level object table entry (CreateObject a6, game_04.cpp:576)
}

// The trace's vehicle kinds by class vtable (the remaster's names: engine/src/oracle.rs VehicleKind); null otherwise.
const char* VehicleKind(uint32_t vtable)
{
    switch (vtable)
    {
    case 0x005af298: return "goliath";
    case 0x005a5e98: return "saucer";
    case 0x005af500: return "mini_zeppelin";
    case 0x005aa238: return "colossus";
    case 0x005a87c0: return "rocket_tower";
    case 0x005a8b88: return "rocket_tower_turret";
    case 0x005adde8: return "rocket_bud";
    default: return nullptr;
    }
}

// One trace line from "paused" on (the frame and the time are put in front by Emit); takes the events gathered so far.
std::string Body()
{
    void* player = Player();
    std::string o = "\"paused\":" + std::to_string(At<int>(g_game, 0x2684) ? 1 : 0);
    if (player)
    {
        float pos[4] = {};
        GetPos(player, pos);
        float m[20] = {};
        using GetMat = void(__thiscall*)(void*, float*);
        reinterpret_cast<GetMat>((*reinterpret_cast<void***>(player))[0x5c / 4])(player, m);
        // the model faces its -Y axis: minus column 1 of the world matrix (row-major, as 0x00453180 reads it)
        const float heading = std::atan2(-m[1], -m[5]) * 57.29578f;
        // the weapon block P+0xc24 (game_09.cpp:723-728): primary slot ids +4.., current slot +0x24; ammo records
        // +0x60 + id * 0xc: clip +0, spare +4; sticky bombs are weapon 10's spare (0x004a1380)
        const uint8_t* s = B(player) + 0xc24;
        const int slot = At<int>(s, 0x24);
        const int weapon = slot >= 0 && slot < 7 ? At<int>(s, 4 + slot * 4) : -1;
        const int clip = weapon >= 0 && weapon < 32 ? At<int>(s, 0x60 + weapon * 0xc) : 0;
        const int spare = weapon >= 0 && weapon < 32 ? At<int>(s, 0x64 + weapon * 0xc) : 0;
        o += ",\"roman\":{\"pos\":";
        Vec(o, pos);
        o += ",\"heading\":";
        Num(o, heading);
        o += ",\"health\":";
        Num(o, At<float>(player, 0x454));
        o += ",\"max_health\":";
        Num(o, At<float>(player, 0x458));
        o += ",\"weapon\":" + std::to_string(weapon);
        o += ",\"clip\":" + std::to_string(clip) + ",\"ammo\":" + std::to_string(spare);
        o += ",\"stickies\":" + std::to_string(At<int>(s, 0x60 + 10 * 0xc + 4)) + "}";
        o += ",\"companions\":[";
        for (int i = 0; i < 2; ++i)
        {
            void* c = At<void*>(player, 0xad0 + 4 * i);  // Jonesy, Q (0x0047afa0, ai_07.cpp:3924)
            o += i ? "," : "";
            if (!c)
            {
                o += "null";
                continue;
            }
            float cp[4] = {};
            GetPos(c, cp);
            o += '[';
            Num(o, cp[0]), o += ',', Num(o, cp[1]), o += ',', Num(o, cp[2]), o += ',', Num(o, At<float>(c, 0x454));
            o += ']';
        }
        o += ']';
    }
    // the World's AI list (Game+0xa84 +0xe7c), walked as TestDumpCamera does
    o += ",\"grunts\":[";
    const uint32_t world = At<uint32_t>(g_game, 0xa84);
    if (world)
    {
        uint32_t iter[2] = {0, 0};
        bool first = true;
        for (uint32_t a = orig::fn_004e7340(At<uint32_t>(reinterpret_cast<void*>(world), 0xe7c),
                                            reinterpret_cast<uint32_t>(iter));
             a; a = orig::fn_004e73c0(reinterpret_cast<uint32_t>(iter)))
        {
            void* g = reinterpret_cast<void*>(a);
            float gp[4] = {};
            GetPos(g, gp);
            o += first ? "{" : ",{";
            first = false;
            o += "\"id\":" + std::to_string(Id(g)) + ",\"pos\":";
            Vec(o, gp);
            o += ",\"health\":";
            Num(o, At<float>(g, 0x454));
            o += ",\"state\":" + std::to_string(At<int>(g, 0x460)) + ",\"target\":";
            const uint32_t t = At<uint32_t>(g, 0x588);
            if (t && t == reinterpret_cast<uint32_t>(player))
                o += "\"roman\"";
            else if (Readable(t, 0x398))
                o += std::to_string(Id(reinterpret_cast<void*>(t)));
            else
                o += "null";
            o += ",\"class\":" + std::to_string(At<uint32_t>(g, 0));
            o += '}';
        }
    }
    o += ']';
    // the vehicles and big units (docs/subsystems/vehicles.md, defence.md) by class: the bosses and goliaths are on the
    // AI list too, the rocket towers / buds only on the damageable (+0xe94) and projectile (+0xe90) lists; a goliath
    // waiting for its script (trigger_ic) is off the AI list (GoliathObject_Slot6 0x00433fe0): the collidable (+0xe9c),
    // +0xea0 and +0xea4 lists are walked too; each object once
    o += ",\"vehicles\":[";
    if (world)
    {
        std::vector<uint32_t> seen;
        for (const unsigned list : {0xe7cu, 0xe94u, 0xe90u, 0xe9cu, 0xea0u, 0xea4u})
        {
            uint32_t iter[2] = {0, 0};
            for (uint32_t a = orig::fn_004e7340(At<uint32_t>(reinterpret_cast<void*>(world), list),
                                                reinterpret_cast<uint32_t>(iter));
                 a; a = orig::fn_004e73c0(reinterpret_cast<uint32_t>(iter)))
            {
                const char* kind = VehicleKind(At<uint32_t>(reinterpret_cast<void*>(a), 0));
                bool dup = false;
                for (const uint32_t s : seen)
                    dup = dup || s == a;
                if (!kind || dup)
                    continue;
                seen.push_back(a);
                void* v = reinterpret_cast<void*>(a);
                float vp[4] = {};
                GetPos(v, vp);
                o += seen.size() > 1 ? ",{" : "{";
                o += "\"id\":" + std::to_string(Id(v)) + ",\"kind\":\"" + kind + "\",\"pos\":";
                Vec(o, vp);
                o += ",\"health\":";
                Num(o, At<float>(v, 0x454));
                o += ",\"state\":" + std::to_string(At<int>(v, 0x460)) + '}';
            }
        }
    }
    o += ']';
    // the alarm controller's gates (fn_00404e50, ai_01.cpp:468-660): sirens World+0xef0, forced Game+0xdcc, Game+0xc80 /
    // +0xcbc / +0xadc / +0xae0 (all must be clear), the spawn timer 0x0063a1f4, the generators listed 0x0063a1f0 and
    // its live troops 0x0063a1ec
    if (world)
    {
        auto count = [](uint32_t list) {
            int n = 0;
            uint32_t iter[2] = {0, 0};
            if (list)
                for (uint32_t a = orig::fn_004e7340(list, reinterpret_cast<uint32_t>(iter)); a;
                     a = orig::fn_004e73c0(reinterpret_cast<uint32_t>(iter)))
                    ++n;
            return n;
        };
        o += ",\"alarm\":{\"sirens\":" + std::to_string(count(At<uint32_t>(reinterpret_cast<void*>(world), 0xef0)));
        o += ",\"forced\":" + std::to_string(At<uint8_t>(g_game, 0xdcc)) + ",\"c80\":";
        Num(o, At<float>(g_game, 0xc80));
        o += ",\"cbc\":" + std::to_string(At<uint8_t>(g_game, 0xcbc)) + ",\"adc\":" + std::to_string(At<uint32_t>(g_game, 0xadc));
        o += ",\"ae0\":";
        Num(o, At<float>(g_game, 0xae0));
        o += ",\"timer\":";
        Num(o, *reinterpret_cast<const float*>(0x0063a1f4));
        o += ",\"generators\":" + std::to_string(count(*reinterpret_cast<const uint32_t*>(0x0063a1f0)));
        o += ",\"troops\":" + std::to_string(count(*reinterpret_cast<const uint32_t*>(0x0063a1ec))) + "}";
    }
    // the level record Game+0xa64: state +0xc (fn_0044a390, ui_01.cpp:2043; mission.md)
    const uint32_t level = At<uint32_t>(g_game, 0xa64);
    o += ",\"mission\":{\"state\":" + std::to_string(Readable(level, 0x10) ? At<int>(reinterpret_cast<void*>(level), 0xc) : -1);
    o += ",\"objective\":" + std::to_string(At<int>(g_game, 0x2658)) + "}";
    auto list = [&o](const char* name, const std::vector<uint32_t>& v) {
        o += ",\"" + std::string(name) + "\":[";
        for (size_t i = 0; i < v.size(); ++i)
            o += (i ? "," : "") + std::to_string(v[i]);
        o += ']';
    };
    list("triggers", g_triggers);
    list("trigger_senders", g_senders);
    list("deaths", g_deaths);
    o += ",\"sounds\":[";
    for (size_t i = 0; i < g_sounds.size(); ++i)
        o += (i ? ",\"" : "\"") + g_sounds[i] + "\"";
    o += "]}\n";
    g_triggers.clear(), g_senders.clear(), g_deaths.clear(), g_sounds.clear();
    return o;
}

void ClearEvents()
{
    g_triggers.clear(), g_senders.clear(), g_deaths.clear(), g_sounds.clear();
}

void Emit(int frame, float clock, const std::string& body)
{
    if (!g_out)
        return;
    if (!g_traced)
        g_clockFirst = clock, g_traced = true;
    std::string o = "{\"f\":" + std::to_string(frame) + ",\"t\":";
    Num(o, clock - g_clockFirst);
    o += "," + body;
    fputs(o.c_str(), g_out);
    fflush(g_out);
}

// --- event hooks (wrappers in front of recovered code or the original, HookWrap) --------------------------------

void* g_nextSendTrigger;
void* g_nextDgoDeath;
void* g_nextFxDie;

void __stdcall OnTrigger(uint32_t id, uint32_t sender)
{
    if (!g_traceEvery)
        return;
    g_triggers.push_back(id);
    g_senders.push_back(Readable(sender, 0x398) ? Id(reinterpret_cast<void*>(sender)) : 0);
}

// Object_SendTrigger 0x00461870: eax = object system, ebx = trigger id; stack: sender; ret 4 (game_04.cpp:596)
__declspec(naked) void Wrap_SendTrigger()
{
    __asm {
        pushad
        push dword ptr [esp + 36]  // sender
        push ebx                   // trigger id
        call OnTrigger
        popad
        jmp dword ptr [g_nextSendTrigger]
    }
}

void __stdcall OnDgoDeath(void* obj)
{
    if (g_traceEvery && !(At<uint32_t>(obj, 0x4d0) & 0x10000))  // once: the flag the function sets (game_03.cpp:966)
        g_deaths.push_back(Id(obj));
}

// DefaultGameObject_OnDeath 0x00450ee0: ecx = object; stack: unused; ret 4
__declspec(naked) void Wrap_DgoDeath()
{
    __asm {
        pushad
        push ecx
        call OnDgoDeath
        popad
        jmp dword ptr [g_nextDgoDeath]
    }
}

void __stdcall OnFxDie(void* obj, uint32_t rec)
{
    // a chain-reaction record (flag 0x40) only lights the fuse (game_02.cpp:766)
    if (g_traceEvery && !(rec && (*reinterpret_cast<const uint8_t*>(rec) & 0x40)))
        g_deaths.push_back(Id(obj));
}

// FxExplosion_Die 0x0041c570 (kegs, pipes): ecx = explosion; stack: damage record or 0; ret 4
__declspec(naked) void Wrap_FxDie()
{
    __asm {
        pushad
        push dword ptr [esp + 36]  // record
        push ecx
        call OnFxDie
        popad
        jmp dword ptr [g_nextFxDie]
    }
}
}  // namespace

extern void (*g_testSoundStarted)(const char* name, float volume);
static void (*g_prevSoundStarted)(const char* name, float volume);

static void OnSound(const char* name, float volume)
{
    if (g_traceEvery && name)
    {
        std::string s;
        for (int i = 0; i < 32 && name[i] >= 0x20 && name[i] < 0x7f && name[i] != '"' && name[i] != '\\'; ++i)
            s += name[i];
        g_sounds.push_back(s);
    }
    if (g_prevSoundStarted)
        g_prevSoundStarted(name, volume);
}

// Play frames (see the top): -1 and below before frame 0, or a very negative value before the briefing.
int TestFramesSincePlay()
{
    const int since = TestFramesSinceLoad();
    if (since < 0 || g_briefFrame < 0)
        return -1000000;
    return since - (g_briefFrame + g_playDelay);
}

bool TestKeysFromPlay()
{
    return g_fromPlay;
}

void TestOracleInit()
{
    g_traceEvery = ConfigInt("test", "trace_every", 0);
    g_fromPlay = ConfigInt("test", "keys_from_play", 0) != 0;
    g_playDelay = ConfigInt("test", "play_delay", 30);
    g_playFrames = ConfigInt("test", "play_frames", 0);
    g_traceFrom = ConfigInt("test", "trace_from", 0);
    char buf[128];
    ConfigString("test", "play_warp", "", buf, sizeof(buf));
    g_warpOn = sscanf(buf, "%f,%f,%f", &g_warp[0], &g_warp[1], &g_warp[2]) == 3;
    if (!g_traceEvery && !g_fromPlay)
        return;
    if (g_traceEvery)
    {
        HookWrap("Object_SendTrigger", reinterpret_cast<void*>(Wrap_SendTrigger), &g_nextSendTrigger);
        HookWrap("DefaultGameObject_OnDeath", reinterpret_cast<void*>(Wrap_DgoDeath), &g_nextDgoDeath);
        HookWrap("FxExplosion_Die", reinterpret_cast<void*>(Wrap_FxDie), &g_nextFxDie);
        g_prevSoundStarted = g_testSoundStarted;
        g_testSoundStarted = OnSound;
    }
    Log("test: oracle trace every %d frames, keys from play %d (delay %d)%s", g_traceEvery, g_fromPlay, g_playDelay,
        g_warpOn ? ", warp" : "");
}

// Main thread, once per presented frame (testscenario.cpp, before the exit check).
void TestOracleOnFrame()
{
    if ((!g_traceEvery && !g_fromPlay) || !g_game)
        return;
    const int since = TestFramesSinceLoad();
    if (since < 0)
        return;
    // the briefing: the first paused message after the load (Game+0x2684 pause count, raised by 0x00403410); none
    // within 600 frames: the clock starts anyway
    if (g_briefFrame < 0 && (At<int>(g_game, 0x2684) > 0 || since >= 600))
    {
        g_briefFrame = since;
        Log("test: briefing at %d frames after the load (paused %d)", since, At<int>(g_game, 0x2684));
    }
    const int f = TestFramesSincePlay();
    if (g_warpOn && !g_warpDone && f >= -10)
    {
        if (void* player = Player())
        {
            g_warpDone = true;
            orig::fn_00457870(0, reinterpret_cast<uint32_t>(g_warp), reinterpret_cast<uint32_t>(player), 0, 0, 0);
            Log("test: play warp at %d to %.1f %.1f %.1f", f, g_warp[0], g_warp[1], g_warp[2]);
        }
    }
    if (!g_traceEvery)
    {
        // keys only (tools/coverage_level.py: play-frame keys, no trace): play_frames still ends the run
        if (g_playFrames && g_briefFrame >= 0 && f >= g_playFrames)
        {
            Log("test: play frames done (%d)", f);
            HooksWriteCoverage();
            TerminateProcess(GetCurrentProcess(), 0);
        }
        return;
    }
    if (g_briefFrame < 0)
    {
        // the play frames are not known yet: keep every frame's line (trace_from < 0), else drop the events
        if (g_traceFrom < 0)
            g_pending.push_back({since, GameClock(), Body()});
        else
            ClearEvents();
        return;
    }
    if (!g_out && f >= g_traceFrom)
    {
        g_out = fopen("and-trace.jsonl", "w");
        for (const Pending& p : g_pending)
        {
            const int pf = p.since - (g_briefFrame + g_playDelay);
            if (pf >= g_traceFrom)
                Emit(pf, p.clock, p.body);
        }
        g_pending.clear();
    }
    if (!g_out)
        ClearEvents();
    else if (f < 0 || f % g_traceEvery == 0)
        Emit(f, GameClock(), Body());
    if (g_playFrames && f >= g_playFrames)
    {
        Log("test: play frames done (%d)", f);
        if (g_out)
            fclose(g_out), g_out = nullptr;
        HooksWriteCoverage();  // [test] hook_coverage runs (TerminateProcess skips atexit)
        TerminateProcess(GetCurrentProcess(), 0);
    }
}
