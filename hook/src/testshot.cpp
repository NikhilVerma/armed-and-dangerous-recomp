// Back-buffer screenshots for tests (handover s12: capture at present time, not from the desktop).
//
// [test] shots=8,15   seconds after start-up at which to save and-shot-<seconds>s.bmp next to the game.
#include <windows.h>
#include <d3d9.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <cstdint>
#include "config.h"
#include "game/game_object.h"
#include "log.h"

static float g_shotTimes[16];
static int g_shotCount, g_nextShot;
static ULONGLONG g_startTick;

void TestShotInit()
{
    g_startTick = GetTickCount64();
    char buf[256];
    char ini[MAX_PATH];
    GetModuleFileNameA(nullptr, ini, MAX_PATH);
    strcpy(strrchr(ini, '\\') + 1, "and.ini");
    GetPrivateProfileStringA("test", "shots", "", buf, sizeof(buf), ini);
    for (char* tok = strtok(buf, ","); tok && g_shotCount < 16; tok = strtok(nullptr, ","))
        g_shotTimes[g_shotCount++] = static_cast<float>(atof(tok));
}

void D3DTraceSuppress(bool on);

static void SaveBackBuffer(IDirect3DDevice9* dev, const char* path)
{
    D3DTraceSuppress(true);
    struct Unsuppress { ~Unsuppress() { D3DTraceSuppress(false); } } unsuppress;
    IDirect3DSurface9* back = nullptr;
    IDirect3DSurface9* copy = nullptr;
    D3DSURFACE_DESC desc;
    D3DLOCKED_RECT lr;
    if (FAILED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back)))
        return;
    back->GetDesc(&desc);
    if (SUCCEEDED(dev->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, &copy,
                                                   nullptr)) &&
        SUCCEEDED(dev->GetRenderTargetData(back, copy)) && SUCCEEDED(copy->LockRect(&lr, nullptr, D3DLOCK_READONLY)))
    {
        FILE* f = fopen(path, "wb");
        if (f)
        {
            BITMAPFILEHEADER fh{};
            BITMAPINFOHEADER ih{};
            ih.biSize = sizeof(ih);
            ih.biWidth = desc.Width;
            ih.biHeight = -static_cast<LONG>(desc.Height);  // top-down
            ih.biPlanes = 1;
            ih.biBitCount = 32;
            fh.bfType = 0x4d42;
            fh.bfOffBits = sizeof(fh) + sizeof(ih);
            fh.bfSize = fh.bfOffBits + desc.Width * desc.Height * 4;
            fwrite(&fh, sizeof(fh), 1, f);
            fwrite(&ih, sizeof(ih), 1, f);
            for (UINT y = 0; y < desc.Height; y++)
                fwrite(static_cast<BYTE*>(lr.pBits) + y * lr.Pitch, 4, desc.Width, f);  // X8R8G8B8 = BGRA bytes
            fclose(f);
            Log("test: saved %s (%ux%u, format %d)", path, desc.Width, desc.Height, desc.Format);
        }
        copy->UnlockRect();
    }
    if (copy)
        copy->Release();
    back->Release();
}

// [test] hash_frames=first:every:count   FNV-1a hash of the back buffer at those frame numbers, logged as
// "hash frame N 0x..." and appended to and-hashes.txt. With [test] clock_step_ms the frames are deterministic.
static unsigned g_frame, g_hashFirst, g_hashEvery = 1, g_hashCount;

void TestHashInit()
{
    char buf[64], ini[MAX_PATH];
    GetModuleFileNameA(nullptr, ini, MAX_PATH);
    strcpy(strrchr(ini, '\\') + 1, "and.ini");
    GetPrivateProfileStringA("test", "hash_frames", "", buf, sizeof(buf), ini);
    if (buf[0])
    {
        sscanf(buf, "%u:%u:%u", &g_hashFirst, &g_hashEvery, &g_hashCount);
        DeleteFileA("and-hashes.txt");
        Log("test: hashing frames %u + k*%u, %u frames", g_hashFirst, g_hashEvery, g_hashCount);
    }
}

static unsigned long long HashBackBuffer(IDirect3DDevice9* dev)
{
    D3DTraceSuppress(true);
    struct Unsuppress { ~Unsuppress() { D3DTraceSuppress(false); } } unsuppress;
    IDirect3DSurface9* back = nullptr;
    IDirect3DSurface9* copy = nullptr;
    D3DSURFACE_DESC desc;
    D3DLOCKED_RECT lr;
    unsigned long long h = 1469598103934665603ull;
    if (FAILED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back)))
        return 0;
    back->GetDesc(&desc);
    if (SUCCEEDED(dev->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, &copy,
                                                   nullptr)) &&
        SUCCEEDED(dev->GetRenderTargetData(back, copy)) && SUCCEEDED(copy->LockRect(&lr, nullptr, D3DLOCK_READONLY)))
    {
        for (UINT y = 0; y < desc.Height; y++)
        {
            const BYTE* row = static_cast<BYTE*>(lr.pBits) + y * lr.Pitch;
            for (UINT x = 0; x < desc.Width * 4; x++)
            {
                if ((x & 3) == 3)
                    continue;  // X8R8G8B8: ignore the unused byte
                h = (h ^ row[x]) * 1099511628211ull;
            }
        }
        copy->UnlockRect();
    }
    if (copy)
        copy->Release();
    back->Release();
    return h;
}

int TestFramesSinceLoad();
static bool FrameNumber(unsigned absolute, unsigned* f);

extern void* g_testCamera;
extern float g_testCameraFov, g_testCameraAspect;

// The frame's camera next to its dump (and-frame-N.camera.json), for rendering the same view elsewhere (the remaster
// engine's comparisons): the view matrix Game+0x704 (as Camera_SetupProjection 0x0040f6e0 passes it to SetView, 16
// floats in memory order), the FOV (camera +0xf4, after the [camera] adjustments), near (+0xf8), far (+0xd4 * +0xd0),
// the aspect passed (0: from the viewport) and the letterbox constants 0x005bcf24 / 0x005bcf28.
static void TestDumpCamera(unsigned frame)
{
    if (!g_game || !g_testCamera)
        return;
    char path[64];
    sprintf(path, "and-frame-%05u.camera.json", frame);
    FILE* out = fopen(path, "w");
    if (!out)
        return;
    const float* view = reinterpret_cast<const float*>(reinterpret_cast<const uint8_t*>(g_game) + 0x704);
    const uint8_t* cam = static_cast<const uint8_t*>(g_testCamera);
    auto f32 = [](const void* p) { return *static_cast<const float*>(p); };
    fprintf(out, "{\"view\": [");
    for (int i = 0; i < 16; ++i)
        fprintf(out, "%s%.7g", i ? ", " : "", view[i]);
    fprintf(out, "], \"fov\": %.7g, \"aspect\": %.7g, \"near\": %.7g, \"far\": %.7g, \"letterbox\": [%.7g, %.7g]}\n",
            g_testCameraFov, g_testCameraAspect, f32(cam + 0xf8), f32(cam + 0xd4) * f32(cam + 0xd0),
            *reinterpret_cast<const float*>(0x005bcf24), *reinterpret_cast<const float*>(0x005bcf28));
    fclose(out);
}

void TestHashBeforePresent(IDirect3DDevice9* dev)
{
    unsigned f;
    if (!FrameNumber(g_frame++, &f))
        return;
    if (!g_hashCount || f < g_hashFirst || (f - g_hashFirst) % g_hashEvery || (f - g_hashFirst) / g_hashEvery >= g_hashCount)
        return;
    unsigned long long h = HashBackBuffer(dev);
    Log("hash frame %u 0x%016llx", f, h);
    static const int dump = ConfigInt("test", "dump_hashed", 0);
    if (dump)
    {
        char path[64];
        sprintf(path, "and-frame-%05u.bmp", f);
        SaveBackBuffer(dev, path);
        TestDumpCamera(f);
    }
    if (FILE* out = fopen("and-hashes.txt", "a"))
    {
        fprintf(out, "%u %016llx\n", f, h);
        fclose(out);
    }
}

void TestShotBeforePresent(IDirect3DDevice9* dev)
{
    if (g_nextShot >= g_shotCount)
        return;
    float elapsed = (GetTickCount64() - g_startTick) / 1000.0f;
    if (elapsed < g_shotTimes[g_nextShot])
        return;
    char path[64];
    sprintf(path, "and-shot-%gs.bmp", g_shotTimes[g_nextShot]);
    SaveBackBuffer(dev, path);
    g_nextShot++;
}

// For backends that present without the real D3D9 device (rhi::vulkan): the same frame counting and selection as
// TestHashBeforePresent. Returns true when this frame is to be saved (hash_frames + dump_hashed) and gives its
// number; the backend then reads its back buffer and calls TestSaveBgra.
int TestFramesSinceLoad();  // testscenario.cpp

// [test] hash_from_load=1: hash_frames counts from the end of the scenario's level load instead of start-up, so
// backends whose loading takes a different number of frames are compared at the same game time.
static bool FrameNumber(unsigned absolute, unsigned* f)
{
    static const int fromLoad = ConfigInt("test", "hash_from_load", 0);
    if (!fromLoad)
        return *f = absolute, true;
    int since = TestFramesSinceLoad();
    if (since < 0)
        return false;
    *f = static_cast<unsigned>(since);
    return true;
}

bool TestFrameDump(unsigned* frame)
{
    unsigned f;
    if (!FrameNumber(g_frame++, &f))
        return false;
    *frame = f;
    if (!g_hashCount || f < g_hashFirst || (f - g_hashFirst) % g_hashEvery || (f - g_hashFirst) / g_hashEvery >= g_hashCount)
        return false;
    static const int dump = ConfigInt("test", "dump_hashed", 0);
    return dump != 0;
}

void TestSaveBgra(unsigned frame, const void* pixels, unsigned width, unsigned height, unsigned pitch)
{
    char path[64];
    sprintf(path, "and-frame-%05u.bmp", frame);
    FILE* f = fopen(path, "wb");
    if (!f)
        return;
    BITMAPFILEHEADER fh{};
    BITMAPINFOHEADER ih{};
    ih.biSize = sizeof(ih);
    ih.biWidth = width;
    ih.biHeight = -static_cast<LONG>(height);
    ih.biPlanes = 1;
    ih.biBitCount = 32;
    fh.bfType = 0x4d42;
    fh.bfOffBits = sizeof(fh) + sizeof(ih);
    fh.bfSize = fh.bfOffBits + width * height * 4;
    fwrite(&fh, sizeof(fh), 1, f);
    fwrite(&ih, sizeof(ih), 1, f);
    for (unsigned y = 0; y < height; y++)
        fwrite(static_cast<const BYTE*>(pixels) + y * pitch, 4, width, f);
    fclose(f);
    Log("test: saved %s (%ux%u, backend)", path, width, height);
}
