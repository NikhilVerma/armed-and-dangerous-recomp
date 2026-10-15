// DirectInput interception: scripted key presses for tests (later: input record and replay, handover s12).
//
// [test] keys=30:ESC,45:ESC   at 30 s and 45 s after start-up, hold Escape for a few frames.
// MOUSEX<n> / MOUSEY<n> move the mouse by n counts per poll while held (frame mode; for debugging motion vectors).
// Key names: ESC, ENTER, SPACE, UP, DOWN, LEFT, RIGHT, or a DIK_ scan code number; MOUSE1..MOUSE3 for mouse
// buttons (left, right, middle).
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "config.h"
#include "hooks.h"
#include "log.h"

int TestFramesSinceLoad();  // testscenario.cpp
int TestFramesSincePlay();  // testoracle.cpp
bool TestKeysFromPlay();

// Enhancement points (hook/enhance/qol.cpp): the keyboard state the game receives (after scripted keys), and the
// mouse movement before the game reads it. Null: unchanged.
void (*g_keyboardObserver)(const BYTE* state) = nullptr;
void (*g_mouseFilter)(LONG* dx, LONG* dy) = nullptr;

namespace
{
struct ScriptedKey
{
    float at;
    BYTE dik;
    int framesLeft;
    int button;  // mouse button 0..2, or -1 for a keyboard key, or -2 for mouse motion (dx, dy per frame)
    int dx, dy;
    float hold;  // frame mode: frames held ("KEY*N"; 3 by default)
};
ScriptedKey g_keys[1024];  // lockstep scenarios (tools/oracle.py) script long timelines
int g_keyCount;
ULONGLONG g_start;
bool g_frameMode;          // [test] keys_frames: schedule in presented frames (deterministic with the test clock)
volatile unsigned g_frames;


// Scripted-key clock: seconds since start-up, or presented frames in frame mode. [test] keys_from_load=1: frames
// since the scenario's level finished loading (no keys before), so backends whose loading takes a different number
// of frames get their input at the same game time.
float KeyClock()
{
    if (!g_frameMode)
        return (GetTickCount64() - g_start) / 1000.0f;
    if (TestKeysFromPlay())  // [test] keys_from_play=1 (testoracle.cpp)
        return static_cast<float>(TestFramesSincePlay());
    static const bool fromLoad = ConfigInt("test", "keys_from_load", 0) != 0;
    if (fromLoad)
    {
        int since = TestFramesSinceLoad();
        return since < 0 ? -1.0f : static_cast<float>(since);
    }
    return static_cast<float>(g_frames);
}

using CreateDevice_t = HRESULT(STDMETHODCALLTYPE*)(IDirectInput8A*, REFGUID, LPDIRECTINPUTDEVICE8A*, LPUNKNOWN);
using GetDeviceState_t = HRESULT(STDMETHODCALLTYPE*)(IDirectInputDevice8A*, DWORD, LPVOID);
using GetDeviceData_t = HRESULT(STDMETHODCALLTYPE*)(IDirectInputDevice8A*, DWORD, LPDIDEVICEOBJECTDATA, LPDWORD,
                                                    DWORD);
CreateDevice_t g_realCreateDevice;
GetDeviceState_t g_realGetDeviceState;
GetDeviceData_t g_realGetDeviceData;
IDirectInputDevice8A* g_keyboard;
IDirectInputDevice8A* g_mouse;
bool g_loggedState, g_loggedData;

BYTE ParseKey(const char* name)
{
    struct { const char* n; BYTE k; } table[] = {{"ESC", DIK_ESCAPE}, {"ENTER", DIK_RETURN}, {"SPACE", DIK_SPACE},
                                                {"UP", DIK_UP},       {"DOWN", DIK_DOWN},    {"LEFT", DIK_LEFT},
                                                {"RIGHT", DIK_RIGHT}};
    for (auto& e : table)
        if (_stricmp(name, e.n) == 0)
            return e.k;
    return static_cast<BYTE>(atoi(name));
}

// Frame mode: a key is down during frames [at, at + hold), whatever the number of polls (deterministic). The
// hold is 3 frames, or N with "frame:KEY*N" (walking: "500:17*120" holds W, scan code 17, for 120 frames).
// Time mode: held for 6 polls from its time (interactive use).
constexpr float kHoldFrames = 3;

bool KeyDown(ScriptedKey& k, float t)
{
    if (g_frameMode)
        return t >= k.at && t < k.at + k.hold;
    return t >= k.at && k.framesLeft > 0;
}

void ActiveKeys(BYTE* down, int* count)
{
    *count = 0;
    float t = KeyClock();
    for (int i = 0; i < g_keyCount; i++)
        if (g_keys[i].button == -1 && KeyDown(g_keys[i], t))
            down[(*count)++] = g_keys[i].dik;
}

void ConsumeFrame()
{
    if (g_frameMode)
        return;
    float t = KeyClock();
    for (int i = 0; i < g_keyCount; i++)
        if (t >= g_keys[i].at && g_keys[i].framesLeft > 0 && --g_keys[i].framesLeft == 0)
            Log("test: released scripted key 0x%02x", g_keys[i].dik);
}

FILE* g_record;  // [test] record_input=1: key changes to and-input.log
BYTE g_lastState[256];

void RecordKeyChanges(const BYTE* state)
{
    if (!g_record)
        return;
    unsigned t = static_cast<unsigned>(GetTickCount64() - g_start);
    for (int k = 0; k < 256; k++)
    {
        BYTE down = state[k] & 0x80;
        if (down != g_lastState[k])
        {
            fprintf(g_record, "%u %u %s\n", t, k, down ? "down" : "up");
            g_lastState[k] = down;
        }
    }
    fflush(g_record);
}

HRESULT STDMETHODCALLTYPE Hook_GetDeviceState(IDirectInputDevice8A* dev, DWORD size, LPVOID data)
{
    HRESULT hr = g_realGetDeviceState(dev, size, data);
    if (g_frameMode)
    {
        // Tests: input is entirely scripted and independent of window focus (a background window cannot
        // acquire the devices). Real keyboard and mouse state never leaks in.
        memset(data, 0, size);
        hr = DI_OK;
    }
    if (dev == g_mouse && size >= sizeof(DIMOUSESTATE) && SUCCEEDED(hr))
    {
        float t = KeyClock();
        for (int i = 0; i < g_keyCount; i++)
            if (g_keys[i].button >= 0 && KeyDown(g_keys[i], t))
                static_cast<DIMOUSESTATE*>(data)->rgbButtons[g_keys[i].button] = 0x80;
            else if (g_keys[i].button == -2 && KeyDown(g_keys[i], t))
            {
                static_cast<DIMOUSESTATE*>(data)->lX += g_keys[i].dx;
                static_cast<DIMOUSESTATE*>(data)->lY += g_keys[i].dy;
            }
        if (g_mouseFilter)
            g_mouseFilter(&static_cast<DIMOUSESTATE*>(data)->lX, &static_cast<DIMOUSESTATE*>(data)->lY);
    }
    if (dev == g_keyboard && size >= 256 && SUCCEEDED(hr))
    {
        RecordKeyChanges(static_cast<const BYTE*>(data));
        if (!g_loggedState)
            Log("input: game polls the keyboard with GetDeviceState (thread %lu)", GetCurrentThreadId()),
                g_loggedState = true;
        BYTE down[32];
        int n;
        ActiveKeys(down, &n);
        for (int i = 0; i < n; i++)
            static_cast<BYTE*>(data)[down[i]] = 0x80;
        if (g_keyboardObserver)
            g_keyboardObserver(static_cast<const BYTE*>(data));
        ConsumeFrame();
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE Hook_GetDeviceData(IDirectInputDevice8A* dev, DWORD size, LPDIDEVICEOBJECTDATA data,
                                             LPDWORD inOut, DWORD flags)
{
    DWORD capacity = inOut ? *inOut : 0;
    HRESULT hr = g_realGetDeviceData(dev, size, data, inOut, flags);
    if (g_frameMode && inOut)
    {
        *inOut = 0;  // tests: only scripted events (added below for the keyboard)
        hr = DI_OK;
    }
    if (dev == g_mouse && SUCCEEDED(hr) && data && inOut && !(flags & DIGDD_PEEK))
    {
        float t = KeyClock();
        for (int i = 0; i < g_keyCount; i++)
        {
            ScriptedKey& k = g_keys[i];
            if (k.button == -2 && KeyDown(k, t))
            {
                for (int axis = 0; axis < 2 && *inOut < capacity; axis++)
                {
                    int v = axis ? k.dy : k.dx;
                    if (!v)
                        continue;
                    DIDEVICEOBJECTDATA& e = *reinterpret_cast<DIDEVICEOBJECTDATA*>(reinterpret_cast<BYTE*>(data) +
                                                                                   *inOut * size);
                    memset(&e, 0, size);
                    e.dwOfs = axis ? DIMOFS_Y : DIMOFS_X;
                    e.dwData = static_cast<DWORD>(v);
                    e.dwTimeStamp = GetTickCount();
                    (*inOut)++;
                }
                continue;
            }
            if (k.button < 0 || *inOut >= capacity)
                continue;
            bool press = k.framesLeft == 6 && t >= k.at;
            bool release = k.framesLeft == 5 && t >= k.at + (g_frameMode ? k.hold : 0.1f);
            if (!press && !release)
                continue;
            k.framesLeft--;
            DIDEVICEOBJECTDATA& e = *reinterpret_cast<DIDEVICEOBJECTDATA*>(reinterpret_cast<BYTE*>(data) +
                                                                           *inOut * size);
            memset(&e, 0, size);
            e.dwOfs = DIMOFS_BUTTON0 + k.button;
            e.dwData = press ? 0x80 : 0;
            e.dwTimeStamp = GetTickCount();
            (*inOut)++;
        }
    }
    if (dev == g_keyboard && SUCCEEDED(hr) && data && inOut && !(flags & DIGDD_PEEK))
    {
        if (!g_loggedData)
            Log("input: game reads buffered keyboard data with GetDeviceData"), g_loggedData = true;
        float t = KeyClock();
        for (int i = 0; i < g_keyCount; i++)
        {
            ScriptedKey& k = g_keys[i];
            if (*inOut >= capacity || k.button != -1)
                continue;
            // One press event and one release event per key. Frame mode: emitted on the first poll at or after
            // the press/release frame (framesLeft tracks which were sent). Time mode: first and last poll.
            bool press, release;
            if (g_frameMode)
            {
                press = k.framesLeft == 6 && t >= k.at;
                release = k.framesLeft == 5 && t >= k.at + k.hold;
                if (press || release)
                    k.framesLeft--;
            }
            else
            {
                if (t < k.at || k.framesLeft <= 0)
                    continue;
                press = k.framesLeft == 6;
                release = k.framesLeft == 1;
            }
            if (press || release)
            {
                DIDEVICEOBJECTDATA& e = *reinterpret_cast<DIDEVICEOBJECTDATA*>(reinterpret_cast<BYTE*>(data) +
                                                                               *inOut * size);
                memset(&e, 0, size);
                e.dwOfs = k.dik;
                e.dwData = press ? 0x80 : 0;
                e.dwTimeStamp = GetTickCount();
                (*inOut)++;
            }
        }
        if (!g_loggedState)
            ConsumeFrame();  // if the game also polls state, that hook advances the frames
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE Hook_CreateDevice(IDirectInput8A* di, REFGUID guid, LPDIRECTINPUTDEVICE8A* out,
                                            LPUNKNOWN outer)
{
    HRESULT hr = g_realCreateDevice(di, guid, out, outer);
    if (SUCCEEDED(hr) && IsEqualGUID(guid, GUID_SysMouse) && !g_mouse)
        g_mouse = *out;  // same device class as the keyboard: the vtable hooks below see it too
    if (SUCCEEDED(hr) && IsEqualGUID(guid, GUID_SysKeyboard) && !g_keyboard)
    {
        g_keyboard = *out;
        void** vt = *reinterpret_cast<void***>(g_keyboard);
        HookAt(vt[9], reinterpret_cast<void*>(Hook_GetDeviceState), reinterpret_cast<void**>(&g_realGetDeviceState),
               "IDirectInputDevice8::GetDeviceState");
        HookAt(vt[10], reinterpret_cast<void*>(Hook_GetDeviceData), reinterpret_cast<void**>(&g_realGetDeviceData),
               "IDirectInputDevice8::GetDeviceData");
    }
    return hr;
}
}  // namespace

void InputHooksInit()
{
    g_start = GetTickCount64();
    char ini[MAX_PATH];
    static char buf[32768];
    GetModuleFileNameA(nullptr, ini, MAX_PATH);
    strcpy(strrchr(ini, '\\') + 1, "and.ini");
    GetPrivateProfileStringA("test", "keys_frames", "", buf, sizeof(buf), ini);
    // [test] offscreen=1 (testoffscreen.cpp): frame mode even without keys, so the desk's keyboard and mouse never
    // reach a test game
    g_frameMode = buf[0] != 0 || GetPrivateProfileIntA("test", "offscreen", 0, ini) != 0;
    if (!buf[0] && !g_frameMode)
        GetPrivateProfileStringA("test", "keys", "", buf, sizeof(buf), ini);
    for (char* tok = strtok(buf, ","); tok && g_keyCount < 1024; tok = strtok(nullptr, ","))
    {
        char* colon = strchr(tok, ':');
        if (!colon)
            continue;
        *colon = '\0';
        const char* key = colon + 1;
        float hold = kHoldFrames;
        if (char* star = strchr(colon + 1, '*'))
            *star = '\0', hold = static_cast<float>(atof(star + 1));
        if (_strnicmp(key, "MOUSEX", 6) == 0 || _strnicmp(key, "MOUSEY", 6) == 0)
        {
            int v = atoi(key + 6);
            bool y = key[5] == 'Y' || key[5] == 'y';
            g_keys[g_keyCount++] = {static_cast<float>(atof(tok)), 0, 6, -2, y ? 0 : v, y ? v : 0, hold};
            continue;
        }
        int button = _strnicmp(key, "MOUSE", 5) == 0 ? atoi(key + 5) - 1 : -1;
        g_keys[g_keyCount++] = {static_cast<float>(atof(tok)), button >= 0 ? BYTE(0) : ParseKey(key), 6, button, 0, 0,
                                hold};
    }
    if (g_keyCount)
        Log("test: %d scripted key presses", g_keyCount);
    if (GetPrivateProfileIntA("test", "record_input", 0, ini))
    {
        g_record = fopen("and-input.log", "w");
        Log("test: recording keyboard to and-input.log");
    }
}

// Called by the dinput8 proxy with the real IDirectInput8 the game receives.
void InputHooksOnDirectInput(void* directInput)
{
    if ((!g_keyCount && !g_record && !g_frameMode) || g_realCreateDevice)
        return;
    void** vt = *reinterpret_cast<void***>(directInput);
    HookAt(vt[3], reinterpret_cast<void*>(Hook_CreateDevice), reinterpret_cast<void**>(&g_realCreateDevice),
           "IDirectInput8::CreateDevice");
}

// Called once per presented frame (frame-mode key schedule).
void InputOnFrame()
{
    g_frames++;
}
