// [test] offscreen=1: the game runs as a test oracle without disturbing the desktop (docs/remaster-engine.md,
// "Oracle runs"). Nothing here touches game code: only the game's imports from USER32 and the window procedure
// in front of the game's own (testscenario.cpp), so the original and recovered code see the same thing.
//
//   window   CreateWindowExA puts the game's top-level window at (-20000, -20000), with WS_EX_TOOLWINDOW (no taskbar
//            button, not in Alt+Tab) and WS_EX_NOACTIVATE; every later move (the game's SetWindowPos, our display
//            enhancement) is held there: the game's SetWindowPos import (its calls pass SWP_NOSENDCHANGING, so no
//            WM_WINDOWPOSCHANGING) and WM_WINDOWPOSCHANGING for anyone else; at the bottom, never activated.
//            ShowWindow becomes SW_SHOWNOACTIVATE. The window is "visible" (D3D9 presents and IsWindowVisible tests
//            in testscenario.cpp still work) but off every monitor.
//   focus    SetForegroundWindow is a no-op. The game's pause on focus loss (App_WndProc 0x004e0110: WM_ACTIVATE /
//            WM_ACTIVATEAPP -> 0x005fb918 -> AppBase_UpdateActive) never sees a deactivation: testscenario.cpp's
//            window procedure reports every one as an activation, and both flags start at 1 in the image
//            (0x005fb918, 0x005cad48), so a window that is never activated stays "active".
//   input    ClipCursor and SetCapture are no-ops (the cursor is never trapped); GetCursorPos (the menus' mouse,
//            0x00491380) reads a fixed point outside the window; DirectInput gets only scripted input
//            (input_hooks.cpp, frame mode forced on).
//   dialogs  MessageBoxA is logged and answered IDOK instead of shown.
//   sound    the process's audio sessions are muted on every render device (a thread re-applies it, so a device
//            plugged in later is muted too). The game still plays its sounds; only the output is silent.
//
// Frame pacing is unaffected: with [test] clock_step_ms the game's clock advances one fixed step per presented frame
// (testclock.cpp), so an unfocused, unthrottled or slow run plays the same frames.
#include <windows.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>

#include "config.h"
#include "hooks.h"
#include "log.h"

namespace
{
constexpr int kOffX = -20000, kOffY = -20000;
bool g_offscreen;
HWND g_gameWindow;  // the first top-level window the game created

using CreateWindowExA_t = HWND(WINAPI*)(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE,
                                        LPVOID);
using ShowWindow_t = BOOL(WINAPI*)(HWND, int);
using SetWindowPos_t = BOOL(WINAPI*)(HWND, HWND, int, int, int, int, UINT);
using SetForegroundWindow_t = BOOL(WINAPI*)(HWND);
using ClipCursor_t = BOOL(WINAPI*)(const RECT*);
using SetCapture_t = HWND(WINAPI*)(HWND);
using GetCursorPos_t = BOOL(WINAPI*)(POINT*);
using MessageBoxA_t = int(WINAPI*)(HWND, LPCSTR, LPCSTR, UINT);
CreateWindowExA_t g_realCreateWindowExA;
ShowWindow_t g_realShowWindow;
SetWindowPos_t g_realSetWindowPos;
SetForegroundWindow_t g_realSetForegroundWindow;
ClipCursor_t g_realClipCursor;
SetCapture_t g_realSetCapture;
GetCursorPos_t g_realGetCursorPos;
MessageBoxA_t g_realMessageBoxA;

extern "C" HWND WINAPI Off_CreateWindowExA(DWORD exStyle, LPCSTR cls, LPCSTR title, DWORD style, int x, int y, int w,
                                           int h, HWND parent, HMENU menu, HINSTANCE inst, LPVOID param)
{
    if (!(style & WS_CHILD))
    {
        exStyle = (exStyle & ~WS_EX_APPWINDOW) | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
        style &= ~WS_VISIBLE;
        x = kOffX, y = kOffY;
        if (w == static_cast<int>(CW_USEDEFAULT))
            w = 640, h = 480;
    }
    HWND wnd = g_realCreateWindowExA(exStyle, cls, title, style, x, y, w, h, parent, menu, inst, param);
    if (!(style & WS_CHILD) && !g_gameWindow)
    {
        g_gameWindow = wnd;
        Log("test: offscreen window %p at %d,%d", static_cast<void*>(wnd), kOffX, kOffY);
    }
    return wnd;
}

extern "C" BOOL WINAPI Off_ShowWindow(HWND wnd, int cmd)
{
    if (cmd != SW_HIDE)
        cmd = SW_SHOWNOACTIVATE;
    return g_realShowWindow(wnd, cmd);
}

// The game's own moves bypass WM_WINDOWPOSCHANGING: Renderer_ResizeWindow 0x005080d0 passes SWP_NOSENDCHANGING
// (0x540, auto_00503900.cpp:629) and 0x00508210's windowed path asks for HWND_TOPMOST with SWP_SHOWWINDOW
// (auto_00508210.cpp:464). Its top-level windows keep the size asked for but stay off-screen, at the bottom, inactive.
extern "C" BOOL WINAPI Off_SetWindowPos(HWND wnd, HWND after, int x, int y, int cx, int cy, UINT flags)
{
    if (!GetParent(wnd))
    {
        x = kOffX, y = kOffY;
        after = HWND_BOTTOM;
        flags = (flags & ~(SWP_NOMOVE | SWP_NOZORDER | SWP_NOSENDCHANGING)) | SWP_NOACTIVATE | SWP_NOOWNERZORDER;
    }
    return g_realSetWindowPos(wnd, after, x, y, cx, cy, flags);
}

extern "C" BOOL WINAPI Off_SetForegroundWindow(HWND)
{
    return TRUE;
}

extern "C" BOOL WINAPI Off_ClipCursor(const RECT*)
{
    return TRUE;
}

extern "C" HWND WINAPI Off_SetCapture(HWND)
{
    return nullptr;
}

extern "C" BOOL WINAPI Off_GetCursorPos(POINT* p)
{
    if (!p)
        return FALSE;
    p->x = kOffX - 10000, p->y = kOffY - 10000;  // left of and above the window: no menu item under it
    return TRUE;
}

extern "C" int WINAPI Off_MessageBoxA(HWND, LPCSTR text, LPCSTR caption, UINT type)
{
    Log("test: message box not shown (offscreen): [%s] %s (type 0x%x)", caption ? caption : "", text ? text : "",
        type);
    return IDOK;
}

// Mutes this process's default audio session on every active render device; true when at least one was muted.
bool MuteSessions()
{
    IMMDeviceEnumerator* devices = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                reinterpret_cast<void**>(&devices))))
        return false;
    bool muted = false;
    IMMDeviceCollection* list = nullptr;
    if (SUCCEEDED(devices->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &list)))
    {
        UINT count = 0;
        list->GetCount(&count);
        for (UINT i = 0; i < count; ++i)
        {
            IMMDevice* dev = nullptr;
            IAudioSessionManager* mgr = nullptr;
            ISimpleAudioVolume* volume = nullptr;
            if (SUCCEEDED(list->Item(i, &dev)) &&
                SUCCEEDED(dev->Activate(__uuidof(IAudioSessionManager), CLSCTX_ALL, nullptr,
                                        reinterpret_cast<void**>(&mgr))) &&
                SUCCEEDED(mgr->GetSimpleAudioVolume(nullptr, FALSE, &volume)) &&
                SUCCEEDED(volume->SetMute(TRUE, nullptr)))
                muted = true;
            if (volume)
                volume->Release();
            if (mgr)
                mgr->Release();
            if (dev)
                dev->Release();
        }
        list->Release();
    }
    devices->Release();
    return muted;
}

DWORD WINAPI MuteThread(void*)
{
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    for (unsigned i = 0;; ++i)
    {
        const bool ok = MuteSessions();
        if (i == 0)
            Log("test: audio sessions %s", ok ? "muted" : "NOT muted (no render device?)");
        Sleep(i < 40 ? 250 : 2000);  // often while the game starts its sound device, then now and then
    }
}
}  // namespace

bool TestOffscreen()
{
    return g_offscreen;
}

// Called by testscenario.cpp's window procedure (in front of the game's) for every message. Returns true when the
// message is handled here (*result is the answer).
bool TestOffscreenWndProc(HWND wnd, UINT msg, WPARAM, LPARAM lp, LRESULT* result)
{
    if (!g_offscreen)
        return false;
    switch (msg)
    {
    case WM_WINDOWPOSCHANGING:
    {
        auto* pos = reinterpret_cast<WINDOWPOS*>(lp);
        if (GetParent(wnd))
            return false;
        // Wherever the game or an enhancement puts it: off-screen, at the bottom, never activated.
        pos->x = kOffX, pos->y = kOffY;
        pos->flags = (pos->flags & ~(SWP_NOMOVE | SWP_NOZORDER)) | SWP_NOACTIVATE | SWP_NOOWNERZORDER;
        pos->hwndInsertAfter = HWND_BOTTOM;
        return false;  // the game's procedure (DefWindowProc) still sees it
    }
    case WM_MOUSEACTIVATE:
        *result = MA_NOACTIVATE;
        return true;
    }
    return false;
}

void TestOffscreenInit()
{
    g_offscreen = ConfigInt("test", "offscreen", 0) != 0;
    if (!g_offscreen)
        return;
    PatchImport("USER32.dll", "CreateWindowExA", reinterpret_cast<void*>(Off_CreateWindowExA),
                reinterpret_cast<void**>(&g_realCreateWindowExA));
    PatchImport("USER32.dll", "ShowWindow", reinterpret_cast<void*>(Off_ShowWindow),
                reinterpret_cast<void**>(&g_realShowWindow));
    PatchImport("USER32.dll", "SetWindowPos", reinterpret_cast<void*>(Off_SetWindowPos),
                reinterpret_cast<void**>(&g_realSetWindowPos));
    PatchImport("USER32.dll", "SetForegroundWindow", reinterpret_cast<void*>(Off_SetForegroundWindow),
                reinterpret_cast<void**>(&g_realSetForegroundWindow));
    PatchImport("USER32.dll", "ClipCursor", reinterpret_cast<void*>(Off_ClipCursor),
                reinterpret_cast<void**>(&g_realClipCursor));
    PatchImport("USER32.dll", "SetCapture", reinterpret_cast<void*>(Off_SetCapture),
                reinterpret_cast<void**>(&g_realSetCapture));
    PatchImport("USER32.dll", "GetCursorPos", reinterpret_cast<void*>(Off_GetCursorPos),
                reinterpret_cast<void**>(&g_realGetCursorPos));
    PatchImport("USER32.dll", "MessageBoxA", reinterpret_cast<void*>(Off_MessageBoxA),
                reinterpret_cast<void**>(&g_realMessageBoxA));
    if (HANDLE t = CreateThread(nullptr, 0, MuteThread, nullptr, 0, nullptr))
        CloseHandle(t);
    Log("test: offscreen v2 (window off every monitor, no focus, cursor free, muted)");
}
