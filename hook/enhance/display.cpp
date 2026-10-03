#include "display.h"

#include "config.h"
#include "engine/app.h"
#include "renderer/renderer.h"
#include "hooks.h"
#include "log.h"

static struct
{
    int windowed, width, height, vsync, fpsLimit;
} g_cfg;

static LARGE_INTEGER g_freq, g_lastPresent;

using AppInit_t = decltype(&App_Init);
static AppInit_t g_nextAppInit;

static void __fastcall Display_App_Init(App* self, void* edx, const AppConfig* config, uint32_t arg2, uint32_t arg3)
{
    AppConfig cfg{};
    if (config)
        cfg = *config;
    if (!(cfg.present & 1))
    {
        // Same as App_Init's own defaults, except the size.
        cfg.present |= 1;
        cfg.displayFlags |= 3;
        cfg.bpp = 32;
    }
    Log("display: start-up size %dx%d (caller %s)", g_cfg.width, g_cfg.height,
        config && (config->present & 1) ? "supplied a size" : "used the 640x480 default");
    cfg.width = g_cfg.width;
    cfg.height = g_cfg.height;
    g_nextAppInit(self, edx, &cfg, arg2, arg3);
}

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

    // App_Init (src/engine/app_init.cpp) defaults the display block to 640x480x32 when the caller supplies
    // none. Wrapping it with a filled display block makes the game itself choose the configured size, so its
    // mode selection, window and viewport follow it.
    if (g_cfg.width > 0 && g_cfg.height > 0)
        HookWrap("App_Init", reinterpret_cast<void*>(Display_App_Init), reinterpret_cast<void**>(&g_nextAppInit));

    // The renderer only offers modes 640-1600 wide in 16:9, 4:3 or 5:4 (src/renderer/display_modes.cpp).
    // all_modes=1 offers every mode the adapter reports; a configured size needs it to be selectable.
    if (ConfigInt("display", "all_modes", 0) || g_cfg.width > 0)
    {
        g_displayModeFilter.minWidth = 320;
        g_displayModeFilter.maxWidth = 16384;
        g_displayModeFilter.allowAnyAspect = true;
        Log("display: all adapter modes offered");
    }
}

void DisplayAdjustParams(D3DPRESENT_PARAMETERS* pp)
{
    // Size is set by the game from the patched defaults; only report a mismatch here.
    if (g_cfg.width > 0 && g_cfg.height > 0 &&
        (pp->BackBufferWidth != static_cast<UINT>(g_cfg.width) || pp->BackBufferHeight != static_cast<UINT>(g_cfg.height)))
        Log("display: game requested %ux%u, configured %dx%d", pp->BackBufferWidth, pp->BackBufferHeight,
            g_cfg.width, g_cfg.height);
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

// Frame-rate log: frames, average and worst frame time every 2 s (always on; cheap).
static double g_presentSeconds;  // summed over the current fps window

void DisplayAfterPresent(long long presentTicks)
{
    g_presentSeconds += double(presentTicks) / g_freq.QuadPart;
}

static void CountFrame()
{
    static LARGE_INTEGER windowStart, last;
    static int frames;
    static double worst;
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    if (windowStart.QuadPart == 0)
        windowStart = last = now;
    double ft = double(now.QuadPart - last.QuadPart) / g_freq.QuadPart;
    if (ft > worst)
        worst = ft;
    last = now;
    frames++;
    double span = double(now.QuadPart - windowStart.QuadPart) / g_freq.QuadPart;
    if (span >= 2.0)
    {
        float dt = *reinterpret_cast<float*>(0x00604a90);  // game frame delta (seconds), see Timer_Tick
        Log("fps: %.1f (worst frame %.1f ms), game dt %.2f ms, %.2f ms/frame blocked in Present", frames / span,
            worst * 1000.0, dt * 1000.0f, g_presentSeconds * 1000.0 / frames);
        g_presentSeconds = 0;
        frames = 0;
        worst = 0;
        windowStart = now;
    }
}

void DisplayBeforePresent(IDirect3DDevice9*)
{
    CountFrame();
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
