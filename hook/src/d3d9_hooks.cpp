// Direct3D 9 interception: the game's single d3d9 import (Direct3DCreate9) is redirected through the IAT,
// then IDirect3D9::CreateDevice and IDirect3DDevice9::Reset/Present are hooked by vtable slot.
// Everything is logged so recon learns what the game asks for; display.cpp may alter the parameters.
#include <windows.h>
#include <d3d9.h>

#include "config.h"
#include "display.h"
#include "hooks.h"
#include "log.h"

void TestShotBeforePresent(IDirect3DDevice9* dev);
void TestHashBeforePresent(IDirect3DDevice9* dev);
void TestClockOnFrame();
void D3DTraceAttach(IDirect3DDevice9* dev);
void TestScenarioOnFrame();
void InputOnFrame();
IDirect3D9* RhiFacadeWrapDirect3D(IDirect3D9* real);  // src/rhi/d3d9_facade.cpp

// vtable slots from d3d9.h declaration order
static constexpr int kD3D9_CreateDevice = 16;
static constexpr int kDevice_TestCooperativeLevel = 3;
static constexpr int kDevice_Reset = 16;
static constexpr int kDevice_Present = 17;

using Direct3DCreate9_t = IDirect3D9*(WINAPI*)(UINT);
using CreateDevice_t = HRESULT(STDMETHODCALLTYPE*)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD,
                                                   D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);
using TestCooperativeLevel_t = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*);
using Reset_t = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
using Present_t = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);

static Direct3DCreate9_t g_realCreate9;
static CreateDevice_t g_realCreateDevice;
static Reset_t g_realReset;
static TestCooperativeLevel_t g_realTestCooperativeLevel;
// [test] device_lost_frame=N: at frame N TestCooperativeLevel reports D3DERR_DEVICENOTRESET once, so the game
// runs its own device-reset path (Renderer_CheckDevice 0x00509740 -> 0x00509620). 0 = off.
static int g_deviceLostFrame, g_presentCount;
static bool g_deviceLostDone;

static HRESULT STDMETHODCALLTYPE Hook_TestCooperativeLevel(IDirect3DDevice9* dev)
{
    if (g_deviceLostFrame > 0 && !g_deviceLostDone && g_presentCount >= g_deviceLostFrame)
    {
        g_deviceLostDone = true;
        Log("test: reporting a lost device (D3DERR_DEVICENOTRESET) at frame %d", g_presentCount);
        return D3DERR_DEVICENOTRESET;
    }
    return g_realTestCooperativeLevel(dev);
}
static Present_t g_realPresent;

static void LogParams(const char* what, const D3DPRESENT_PARAMETERS* pp)
{
    Log("%s: %ux%u fmt %d count %u ms %d swap %d windowed %d depth %d/%d flags 0x%lx refresh %u interval 0x%x",
        what, pp->BackBufferWidth, pp->BackBufferHeight, pp->BackBufferFormat, pp->BackBufferCount,
        pp->MultiSampleType, pp->SwapEffect, pp->Windowed, pp->EnableAutoDepthStencil, pp->AutoDepthStencilFormat,
        pp->Flags, pp->FullScreen_RefreshRateInHz, pp->PresentationInterval);
}

static HRESULT STDMETHODCALLTYPE Hook_Reset(IDirect3DDevice9* dev, D3DPRESENT_PARAMETERS* pp)
{
    LogParams("Reset requested", pp);
    DisplayAdjustParams(pp);
    LogParams("Reset applied", pp);
    HRESULT hr = g_realReset(dev, pp);
    Log("Reset -> 0x%08lx", hr);
    return hr;
}

static HRESULT STDMETHODCALLTYPE Hook_Present(IDirect3DDevice9* dev, const RECT* src, const RECT* dst, HWND wnd,
                                              const RGNDATA* dirty)
{
    TestHashBeforePresent(dev);
    TestShotBeforePresent(dev);
    DisplayBeforePresent(dev);
    LARGE_INTEGER a, b;
    QueryPerformanceCounter(&a);
    HRESULT hr = g_realPresent(dev, src, dst, wnd, dirty);
    ++g_presentCount;
    QueryPerformanceCounter(&b);
    DisplayAfterPresent(b.QuadPart - a.QuadPart);
    TestClockOnFrame();
    TestScenarioOnFrame();
    InputOnFrame();
    return hr;
}

// Per-frame harness work for backends that present without the real D3D9 device (rhi::vulkan calls this after
// each Present): the same as Hook_Present does after the real Present.
void HarnessOnFrame()
{
    TestClockOnFrame();
    TestScenarioOnFrame();
    InputOnFrame();
}

static HRESULT STDMETHODCALLTYPE Hook_CreateDevice(IDirect3D9* d3d, UINT adapter, D3DDEVTYPE type, HWND wnd,
                                                   DWORD flags, D3DPRESENT_PARAMETERS* pp, IDirect3DDevice9** out)
{
    LogParams("CreateDevice requested", pp);
    Log("  adapter %u type %d hwnd %p behavior 0x%lx", adapter, type, wnd, flags);
    DisplayAdjustParams(pp);
    DisplayAdjustWindow(wnd, pp);
    LogParams("CreateDevice applied", pp);
    HRESULT hr = g_realCreateDevice(d3d, adapter, type, wnd, flags, pp, out);
    Log("CreateDevice -> 0x%08lx device %p", hr, out ? *out : nullptr);
    if (SUCCEEDED(hr) && out && *out && !g_realReset)
    {
        void** vt = *reinterpret_cast<void***>(*out);
        HookAt(vt[kDevice_Reset], reinterpret_cast<void*>(Hook_Reset), reinterpret_cast<void**>(&g_realReset),
               "IDirect3DDevice9::Reset");
        HookAt(vt[kDevice_Present], reinterpret_cast<void*>(Hook_Present), reinterpret_cast<void**>(&g_realPresent),
               "IDirect3DDevice9::Present");
        g_deviceLostFrame = ConfigInt("test", "device_lost_frame", 0);
        if (g_deviceLostFrame > 0)
            HookAt(vt[kDevice_TestCooperativeLevel], reinterpret_cast<void*>(Hook_TestCooperativeLevel),
                   reinterpret_cast<void**>(&g_realTestCooperativeLevel), "IDirect3DDevice9::TestCooperativeLevel");
        D3DTraceAttach(*out);  // after the code hooks: it swaps the object's vtable for thunks
    }
    return hr;
}

static IDirect3D9* WINAPI Hook_Direct3DCreate9(UINT sdk)
{
    IDirect3D9* d3d = g_realCreate9(sdk);
    Log("Direct3DCreate9(%u) -> %p", sdk, d3d);
    IDirect3D9* real = d3d;
    if (d3d && !g_realCreateDevice)
    {
        void** vt = *reinterpret_cast<void***>(d3d);
        HookAt(vt[kD3D9_CreateDevice], reinterpret_cast<void*>(Hook_CreateDevice),
               reinterpret_cast<void**>(&g_realCreateDevice), "IDirect3D9::CreateDevice");
    }
    // [render] rhi=1: the game gets the renderer-interface facade; the hooks above stay on the real objects.
    if (real && ConfigInt("render", "rhi", 0))
        return RhiFacadeWrapDirect3D(real);
    return d3d;
}

// Replace one import in the game's IAT. The loader has bound all imports before any DllMain runs.
bool PatchImport(const char* dll, const char* func, void* hook, void** original)
{
    auto base = reinterpret_cast<BYTE*>(GetModuleHandleA(nullptr));
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
    auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    for (auto imp = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); imp->Name; ++imp)
    {
        if (_stricmp(reinterpret_cast<char*>(base + imp->Name), dll) != 0)
            continue;
        auto names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->OriginalFirstThunk);
        auto slots = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots)
        {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal))
                continue;
            auto byName = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
            if (strcmp(reinterpret_cast<char*>(byName->Name), func) != 0)
                continue;
            DWORD old;
            VirtualProtect(&slots->u1.Function, sizeof(void*), PAGE_READWRITE, &old);
            *original = reinterpret_cast<void*>(slots->u1.Function);
            slots->u1.Function = reinterpret_cast<uintptr_t>(hook);
            VirtualProtect(&slots->u1.Function, sizeof(void*), old, &old);
            Log("IAT %s!%s at %p patched", dll, func, &slots->u1.Function);
            return true;
        }
    }
    Log("IAT %s!%s not found", dll, func);
    return false;
}

void D3D9HooksInstall()
{
    PatchImport("d3d9.dll", "Direct3DCreate9", reinterpret_cast<void*>(Hook_Direct3DCreate9),
                reinterpret_cast<void**>(&g_realCreate9));
}
