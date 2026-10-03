// DirectInput interception: scripted key presses for tests (later: input record and replay, handover s12).
//
// [test] keys=30:ESC,45:ESC   at 30 s and 45 s after start-up, hold Escape for a few frames.
// Key names: ESC, ENTER, SPACE, UP, DOWN, LEFT, RIGHT, or a DIK_ scan code number.
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>

#include <cstdlib>
#include <cstring>

#include "hooks.h"
#include "log.h"

namespace
{
struct ScriptedKey
{
    float at;
    BYTE dik;
    int framesLeft;
};
ScriptedKey g_keys[32];
int g_keyCount;
ULONGLONG g_start;

using CreateDevice_t = HRESULT(STDMETHODCALLTYPE*)(IDirectInput8A*, REFGUID, LPDIRECTINPUTDEVICE8A*, LPUNKNOWN);
using GetDeviceState_t = HRESULT(STDMETHODCALLTYPE*)(IDirectInputDevice8A*, DWORD, LPVOID);
using GetDeviceData_t = HRESULT(STDMETHODCALLTYPE*)(IDirectInputDevice8A*, DWORD, LPDIDEVICEOBJECTDATA, LPDWORD,
                                                    DWORD);
CreateDevice_t g_realCreateDevice;
GetDeviceState_t g_realGetDeviceState;
GetDeviceData_t g_realGetDeviceData;
IDirectInputDevice8A* g_keyboard;
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

// Which scripted keys are down now (held for 6 polls each, starting at their time).
void ActiveKeys(BYTE* down, int* count)
{
    *count = 0;
    float t = (GetTickCount64() - g_start) / 1000.0f;
    for (int i = 0; i < g_keyCount; i++)
        if (t >= g_keys[i].at && g_keys[i].framesLeft > 0)
            down[(*count)++] = g_keys[i].dik;
}

void ConsumeFrame()
{
    float t = (GetTickCount64() - g_start) / 1000.0f;
    for (int i = 0; i < g_keyCount; i++)
        if (t >= g_keys[i].at && g_keys[i].framesLeft > 0 && --g_keys[i].framesLeft == 0)
            Log("test: released scripted key 0x%02x", g_keys[i].dik);
}

HRESULT STDMETHODCALLTYPE Hook_GetDeviceState(IDirectInputDevice8A* dev, DWORD size, LPVOID data)
{
    HRESULT hr = g_realGetDeviceState(dev, size, data);
    if (dev == g_keyboard && size >= 256 && SUCCEEDED(hr))
    {
        if (!g_loggedState)
            Log("input: game polls the keyboard with GetDeviceState"), g_loggedState = true;
        BYTE down[32];
        int n;
        ActiveKeys(down, &n);
        for (int i = 0; i < n; i++)
            static_cast<BYTE*>(data)[down[i]] = 0x80;
        ConsumeFrame();
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE Hook_GetDeviceData(IDirectInputDevice8A* dev, DWORD size, LPDIDEVICEOBJECTDATA data,
                                             LPDWORD inOut, DWORD flags)
{
    DWORD capacity = inOut ? *inOut : 0;
    HRESULT hr = g_realGetDeviceData(dev, size, data, inOut, flags);
    if (dev == g_keyboard && SUCCEEDED(hr) && data && inOut && !(flags & DIGDD_PEEK))
    {
        if (!g_loggedData)
            Log("input: game reads buffered keyboard data with GetDeviceData"), g_loggedData = true;
        float t = (GetTickCount64() - g_start) / 1000.0f;
        for (int i = 0; i < g_keyCount; i++)
        {
            ScriptedKey& k = g_keys[i];
            if (t < k.at || k.framesLeft <= 0 || *inOut >= capacity)
                continue;
            // One press event on the first poll, one release event on the last.
            bool press = k.framesLeft == 6, release = k.framesLeft == 1;
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
    char ini[MAX_PATH], buf[512];
    GetModuleFileNameA(nullptr, ini, MAX_PATH);
    strcpy(strrchr(ini, '\\') + 1, "and.ini");
    GetPrivateProfileStringA("test", "keys", "", buf, sizeof(buf), ini);
    for (char* tok = strtok(buf, ","); tok && g_keyCount < 32; tok = strtok(nullptr, ","))
    {
        char* colon = strchr(tok, ':');
        if (!colon)
            continue;
        *colon = '\0';
        g_keys[g_keyCount++] = {static_cast<float>(atof(tok)), ParseKey(colon + 1), 6};
    }
    if (g_keyCount)
        Log("test: %d scripted key presses", g_keyCount);
}

// Called by the dinput8 proxy with the real IDirectInput8 the game receives.
void InputHooksOnDirectInput(void* directInput)
{
    if (!g_keyCount || g_realCreateDevice)
        return;
    void** vt = *reinterpret_cast<void***>(directInput);
    HookAt(vt[3], reinterpret_cast<void*>(Hook_CreateDevice), reinterpret_cast<void**>(&g_realCreateDevice),
           "IDirectInput8::CreateDevice");
}
