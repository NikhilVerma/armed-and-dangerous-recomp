#include "display.h"

#include "config.h"
#include "log.h"

static struct
{
    int windowed, width, height, vsync, fpsLimit;
} g_cfg;

static LARGE_INTEGER g_freq, g_lastPresent;

void DisplayInit()
{
    g_cfg.windowed = ConfigInt("display", "windowed", 0);
    g_cfg.width = ConfigInt("display", "width", 0);
    g_cfg.height = ConfigInt("display", "height", 0);
    g_cfg.vsync = ConfigInt("display", "vsync", -1);
    g_cfg.fpsLimit = ConfigInt("display", "fps_limit", 0);
    QueryPerformanceFrequency(&g_freq);
    Log("display: windowed %d size %dx%d vsync %d fps_limit %d", g_cfg.windowed, g_cfg.width, g_cfg.height,
        g_cfg.vsync, g_cfg.fpsLimit);
}

void DisplayAdjustParams(D3DPRESENT_PARAMETERS* pp)
{
    if (g_cfg.width > 0 && g_cfg.height > 0)
    {
        pp->BackBufferWidth = g_cfg.width;
        pp->BackBufferHeight = g_cfg.height;
    }
    if (g_cfg.windowed)
    {
        pp->Windowed = TRUE;
        pp->FullScreen_RefreshRateInHz = 0;
        if (pp->BackBufferFormat != D3DFMT_UNKNOWN)
            pp->BackBufferFormat = D3DFMT_UNKNOWN;  // use the desktop format
    }
    if (g_cfg.vsync == 0)
        pp->PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    else if (g_cfg.vsync == 1)
        pp->PresentationInterval = D3DPRESENT_INTERVAL_ONE;
}

void DisplayAdjustWindow(HWND wnd, const D3DPRESENT_PARAMETERS* pp)
{
    if (!g_cfg.windowed || !wnd)
        return;
    LONG style = GetWindowLongA(wnd, GWL_STYLE);
    style = (style & ~WS_POPUP) | WS_OVERLAPPEDWINDOW;
    SetWindowLongA(wnd, GWL_STYLE, style);
    RECT r{0, 0, static_cast<LONG>(pp->BackBufferWidth), static_cast<LONG>(pp->BackBufferHeight)};
    AdjustWindowRect(&r, style, FALSE);
    SetWindowPos(wnd, HWND_NOTOPMOST, 64, 64, r.right - r.left, r.bottom - r.top, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
}

void DisplayBeforePresent(IDirect3DDevice9*)
{
    if (g_cfg.fpsLimit <= 0)
        return;
    const LONGLONG period = g_freq.QuadPart / g_cfg.fpsLimit;
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    while (now.QuadPart - g_lastPresent.QuadPart < period)
    {
        if (period - (now.QuadPart - g_lastPresent.QuadPart) > g_freq.QuadPart / 500)
            Sleep(1);
        QueryPerformanceCounter(&now);
    }
    g_lastPresent = now;
}
