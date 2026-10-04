// Audio enhancements (docs/goals/remaster-basics.md item 6; evidence: docs/subsystems/qol.md section 3). Switches in
// [audio] in and.ini, all off by default.
//
//   master=1.0        master volume 0..1 on top of the game's sound, voice and music volumes. The game sets every
//                     buffer's volume through one function (0x004fa1d0 -> IDirectSoundBuffer::SetVolume, hundredths
//                     of a dB, -10000 = silent); the master attenuation is added there.
//   device=           output device: part of a DirectSound device description (as listed in and.log at start-up),
//                     e.g. "Headphones". Empty: the default device, as the original (DirectSoundCreate8(NULL)).
#include <windows.h>
#include <mmsystem.h>
#include <dsound.h>

#include <cmath>
#include <cstring>

#include "config.h"
#include "hooks.h"
#include "log.h"
#include "settings.h"

namespace
{
using DirectSoundCreate8_t = HRESULT(WINAPI*)(LPCGUID, LPDIRECTSOUND8*, LPUNKNOWN);
using CreateSoundBuffer_t = HRESULT(STDMETHODCALLTYPE*)(IDirectSound8*, LPCDSBUFFERDESC, LPDIRECTSOUNDBUFFER*,
                                                        LPUNKNOWN);
using SetVolume_t = HRESULT(STDMETHODCALLTYPE*)(IDirectSoundBuffer*, LONG);

DirectSoundCreate8_t g_realCreate;
CreateSoundBuffer_t g_realCreateBuffer;
SetVolume_t g_realSetVolume;
LONG g_masterAttenuation;  // hundredths of a dB (<= 0)
char g_device[128];
GUID g_deviceGuid;
bool g_deviceFound;

HRESULT STDMETHODCALLTYPE Hook_SetVolume(IDirectSoundBuffer* b, LONG volume)
{
    LONG v = volume + g_masterAttenuation;
    return g_realSetVolume(b, v < DSBVOLUME_MIN ? DSBVOLUME_MIN : v);
}

HRESULT STDMETHODCALLTYPE Hook_CreateSoundBuffer(IDirectSound8* ds, LPCDSBUFFERDESC desc, LPDIRECTSOUNDBUFFER* out,
                                                 LPUNKNOWN outer)
{
    HRESULT hr = g_realCreateBuffer(ds, desc, out, outer);
    // Secondary buffers share one vtable: hook its SetVolume (slot 15) once.
    if (SUCCEEDED(hr) && out && *out && !g_realSetVolume &&
        !(desc && (desc->dwFlags & DSBCAPS_PRIMARYBUFFER)))
    {
        void** vt = *reinterpret_cast<void***>(*out);
        HookAt(vt[15], reinterpret_cast<void*>(Hook_SetVolume), reinterpret_cast<void**>(&g_realSetVolume),
               "IDirectSoundBuffer::SetVolume");
    }
    return hr;
}

bool ContainsNoCase(const char* text, const char* part)
{
    size_t n = strlen(part);
    for (; *text; ++text)
        if (_strnicmp(text, part, n) == 0)
            return true;
    return n == 0;
}

BOOL CALLBACK EnumDevice(LPGUID guid, LPCSTR description, LPCSTR, LPVOID)
{
    Log("audio: output device \"%s\"", description);
    if (guid && g_device[0] && !g_deviceFound && ContainsNoCase(description, g_device))
    {
        g_deviceGuid = *guid;
        g_deviceFound = true;
    }
    return TRUE;
}

HRESULT WINAPI Hook_DirectSoundCreate8(LPCGUID guid, LPDIRECTSOUND8* out, LPUNKNOWN outer)
{
    DirectSoundEnumerateA(EnumDevice, nullptr);
    if (g_device[0] && !guid)
    {
        if (g_deviceFound)
            guid = &g_deviceGuid, Log("audio: using output device matching \"%s\"", g_device);
        else
            Log("audio: no output device matches \"%s\"; using the default", g_device);
    }
    HRESULT hr = g_realCreate(guid, out, outer);
    if (SUCCEEDED(hr) && out && *out && !g_realCreateBuffer)
    {
        void** vt = *reinterpret_cast<void***>(*out);
        HookAt(vt[3], reinterpret_cast<void*>(Hook_CreateSoundBuffer), reinterpret_cast<void**>(&g_realCreateBuffer),
               "IDirectSound8::CreateSoundBuffer");
    }
    return hr;
}
}  // namespace

void AudioInit()
{
    float master = RemasterRegister({"audio", "master", L"Master volume", RemasterSetting::Slider, 0.0f, 1.0f, 0.05f, {},
                                     0, false,
                                     [](float v) {
                                         // used from the next volume change the game makes (any volume slider)
                                         g_masterAttenuation =
                                             v >= 1.0f ? 0
                                             : v <= 0.0001f ? DSBVOLUME_MIN
                                                            : static_cast<LONG>(2000.0f * std::log10(v));
                                     }},
                                    1.0f);
    if (master < 1.0f)
        g_masterAttenuation = master <= 0.0001f ? DSBVOLUME_MIN : static_cast<LONG>(2000.0f * std::log10(master));
    ConfigString("audio", "device", "", g_device, sizeof(g_device));
    {  // always: the master volume can change from the Remaster page; at 1.0 nothing is altered
        if (!PatchImport("dsound.dll", "DirectSoundCreate8", reinterpret_cast<void*>(Hook_DirectSoundCreate8),
                         reinterpret_cast<void**>(&g_realCreate)))
            Log("audio: DirectSoundCreate8 import not found; [audio] settings inactive");
        Log("audio: master %.2f (%ld/100 dB), device \"%s\"", master, g_masterAttenuation, g_device);
    }
}
