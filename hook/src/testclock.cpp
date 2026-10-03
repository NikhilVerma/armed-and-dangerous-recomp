// Deterministic time for comparison tests (handover s12: "control time").
//
// [test] clock_step_ms=16.6667   the game's QueryPerformanceCounter/GetTickCount imports return a virtual clock
//                                that advances exactly one step per presented frame. Everything the game derives
//                                from time (frame dt, game time, rand seed from GetTickCount in App_Init) becomes
//                                a function of the frame number. 0 = real time (default).
//
// While no frames are presented (loading) the clock also creeps forward by 1 us per query, so wait-until-time
// loops cannot hang. Only the main thread's queries advance it; other threads only read it.
#include <windows.h>

#include "config.h"
#include "hooks.h"
#include "log.h"

namespace
{
constexpr LONGLONG kFreq = 10'000'000;  // virtual ticks per second
LONGLONG g_step;                        // ticks per frame
volatile LONGLONG g_now = kFreq;        // start at 1 s, never 0
bool g_enabled;

using QPC_t = BOOL(WINAPI*)(LARGE_INTEGER*);
using QPF_t = BOOL(WINAPI*)(LARGE_INTEGER*);
using GTC_t = DWORD(WINAPI*)();
QPC_t g_realQPC;
QPF_t g_realQPF;
GTC_t g_realGTC;

DWORD g_mainThread;  // the thread that loaded us (process start) runs the game loop

BOOL WINAPI Fake_QPC(LARGE_INTEGER* out)
{
    // Only the main thread's queries creep the clock (1 us each): other threads (loader, audio) query a
    // timing-dependent number of times, which made runs diverge.
    out->QuadPart = GetCurrentThreadId() == g_mainThread ? InterlockedAdd64(&g_now, 10) : g_now;
    return TRUE;
}

BOOL WINAPI Fake_QPF(LARGE_INTEGER* out)
{
    out->QuadPart = kFreq;
    return TRUE;
}

DWORD WINAPI Fake_GTC()
{
    return static_cast<DWORD>(g_now / (kFreq / 1000));
}
}  // namespace

void TestClockInit()
{
    float stepMs = ConfigFloat("test", "clock_step_ms", 0.0f);
    if (stepMs <= 0.0f)
        return;
    g_enabled = true;
    g_mainThread = GetCurrentThreadId();
    g_step = static_cast<LONGLONG>(stepMs * (kFreq / 1000));
    PatchImport("KERNEL32.dll", "QueryPerformanceCounter", reinterpret_cast<void*>(Fake_QPC),
                reinterpret_cast<void**>(&g_realQPC));
    PatchImport("KERNEL32.dll", "QueryPerformanceFrequency", reinterpret_cast<void*>(Fake_QPF),
                reinterpret_cast<void**>(&g_realQPF));
    PatchImport("KERNEL32.dll", "GetTickCount", reinterpret_cast<void*>(Fake_GTC),
                reinterpret_cast<void**>(&g_realGTC));
    Log("test: virtual clock, %.4f ms per frame", stepMs);
}

// Called once per presented frame: snap the clock to the next frame boundary.
void TestClockOnFrame()
{
    if (!g_enabled)
        return;
    LONGLONG next = (g_now / g_step + 1) * g_step;
    InterlockedExchange64(&g_now, next);
}

bool TestClockEnabled()
{
    return g_enabled;
}
