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
#include <climits>
#include <cstring>
#include <mutex>
#include <unordered_map>

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
using SetPan_t = HRESULT(STDMETHODCALLTYPE*)(IDirectSoundBuffer*, LONG);
using Duplicate_t = HRESULT(STDMETHODCALLTYPE*)(IDirectSound8*, LPDIRECTSOUNDBUFFER, LPDIRECTSOUNDBUFFER*);
SetPan_t g_realSetPan;
Duplicate_t g_realDuplicate;
// [audio] skip_redundant (remaster on): the game sets every playing voice's volume and pan every frame
// (0x004fa1d0, 0x004fb1f0), mostly to the same values, and each DirectSound call reached the registry (5% of the
// CPU samples of a 4K walk in NtOpenKeyEx, 2026-10-12). Calls that would set a buffer's current value, or one within
// 0.15 dB / 1% of the pan range of it, are skipped.
// The cache is forgotten for a buffer whenever CreateSoundBuffer / DuplicateSoundBuffer hands it out.
bool g_skipRedundant;
struct BufferState
{
    LONG volume = LONG_MIN, pan = LONG_MIN;
};
std::unordered_map<IDirectSoundBuffer*, BufferState> g_bufferState;
std::mutex g_bufferLock;
uint32_t g_passed[2], g_skipped[2], g_forgotten;  // volume, pan (diagnostics: [audio] log_calls=1)
LONG g_masterAttenuation;  // hundredths of a dB (<= 0)
char g_device[128];
GUID g_deviceGuid;
bool g_deviceFound;

HRESULT STDMETHODCALLTYPE Hook_SetVolume(IDirectSoundBuffer* b, LONG volume)
{
    LONG v = volume + g_masterAttenuation;
    v = v < DSBVOLUME_MIN ? DSBVOLUME_MIN : v;
    if (g_skipRedundant)
    {
        std::lock_guard<std::mutex> g(g_bufferLock);
        LONG& last = g_bufferState[b].volume;
        // Within 0.15 dB (inaudible) of what the buffer has, unless it is silence or full volume.
        if (last == v || (last != LONG_MIN && std::abs(last - v) < 15 && v != DSBVOLUME_MIN && v != DSBVOLUME_MAX))
            return ++g_skipped[0], DS_OK;
        ++g_passed[0];
        last = v;
    }
    return g_realSetVolume(b, v);
}

HRESULT STDMETHODCALLTYPE Hook_SetPan(IDirectSoundBuffer* b, LONG pan)
{
    if (g_skipRedundant)
    {
        std::lock_guard<std::mutex> g(g_bufferLock);
        LONG& last = g_bufferState[b].pan;
        if (last == pan || (last != LONG_MIN && std::abs(last - pan) < 100))  // 1% of the pan range
            return ++g_skipped[1], DS_OK;
        ++g_passed[1];
        last = pan;
    }
    return g_realSetPan(b, pan);
}

void ForgetBuffer(IDirectSoundBuffer* b)
{
    std::lock_guard<std::mutex> g(g_bufferLock);
    g_bufferState.erase(b);
    ++g_forgotten;
}

HRESULT STDMETHODCALLTYPE Hook_Duplicate(IDirectSound8* ds, LPDIRECTSOUNDBUFFER src, LPDIRECTSOUNDBUFFER* out)
{
    HRESULT hr = g_realDuplicate(ds, src, out);
    if (SUCCEEDED(hr) && out && *out)
        ForgetBuffer(*out);
    return hr;
}

HRESULT STDMETHODCALLTYPE Hook_CreateSoundBuffer(IDirectSound8* ds, LPCDSBUFFERDESC desc, LPDIRECTSOUNDBUFFER* out,
                                                 LPUNKNOWN outer)
{
    HRESULT hr = g_realCreateBuffer(ds, desc, out, outer);
    if (SUCCEEDED(hr) && out && *out)
        ForgetBuffer(*out);
    // Secondary buffers share one vtable: hook its SetVolume (slot 15) and SetPan (slot 16) once.
    if (SUCCEEDED(hr) && out && *out && !g_realSetVolume &&
        !(desc && (desc->dwFlags & DSBCAPS_PRIMARYBUFFER)))
    {
        void** vt = *reinterpret_cast<void***>(*out);
        HookAt(vt[15], reinterpret_cast<void*>(Hook_SetVolume), reinterpret_cast<void**>(&g_realSetVolume),
               "IDirectSoundBuffer::SetVolume");
        HookAt(vt[16], reinterpret_cast<void*>(Hook_SetPan), reinterpret_cast<void**>(&g_realSetPan),
               "IDirectSoundBuffer::SetPan");
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
        HookAt(vt[5], reinterpret_cast<void*>(Hook_Duplicate), reinterpret_cast<void**>(&g_realDuplicate),
               "IDirectSound8::DuplicateSoundBuffer");
    }
    return hr;
}
}  // namespace

extern uint32_t g_audioMixRate, g_audioMixRateResult, g_audioMixRateHr;  // src/audio/audio_03.cpp

// The rate the primary buffer accepted, logged once the game has made it.
void AudioOnFrame()
{
    static const bool logCalls = ConfigInt("audio", "log_calls", 0) != 0;
    static DWORD last;
    if (logCalls && GetTickCount() - last > 2000)
    {
        last = GetTickCount();
        Log("audio: 2 s: volume %u set / %u skipped, pan %u set / %u skipped, %u new buffers, %zu tracked", g_passed[0],
            g_skipped[0], g_passed[1], g_skipped[1], g_forgotten, g_bufferState.size());
        g_passed[0] = g_passed[1] = g_skipped[0] = g_skipped[1] = g_forgotten = 0;
    }
    static bool logged;
    if (!logged && g_audioMixRate && g_audioMixRateResult)
    {
        logged = true;
        Log("audio: mix rate asked %u Hz, primary buffer reports %u Hz (SetFormat %08x)", g_audioMixRate,
            g_audioMixRateResult, g_audioMixRateHr);
    }
}

void AudioInit()
{
    // Mixing rate: the original sets the DirectSound primary buffer to 22050 Hz, 16-bit stereo; DirectSound mixes every
    // sound at that rate, so anything above 11 kHz was lost. The remaster asks for 48000 Hz (applies at start).
    g_audioMixRate = static_cast<uint32_t>(RemasterRegister(
        {"audio", "mix_rate", L"Audio mixing rate (Hz)", RemasterSetting::List, 0, 2, 1,
         {L"22050 (original)", L"44100", L"48000"}, 0, true, nullptr},
        0, 2));
    static const uint32_t kRates[] = {0, 44100, 48000};
    g_audioMixRate = kRates[g_audioMixRate < 3 ? g_audioMixRate : 2];
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
    g_skipRedundant = ConfigIntDefault("audio", "skip_redundant", 0, 1) != 0;
    {  // always: the master volume can change from the Remaster page; at 1.0 nothing is altered
        if (!PatchImport("dsound.dll", "DirectSoundCreate8", reinterpret_cast<void*>(Hook_DirectSoundCreate8),
                         reinterpret_cast<void**>(&g_realCreate)))
            Log("audio: DirectSoundCreate8 import not found; [audio] settings inactive");
        Log("audio: master %.2f (%ld/100 dB), device \"%s\"", master, g_masterAttenuation, g_device);
    }
}
