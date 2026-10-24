// Mouse in menus and window focus (docs/subsystems/qol.md section 1). Switches in [input] in and.ini; the original
// behaviour unless set (remaster defaults on).
//
//   hardware_cursor=1  menus show the Windows pointer instead of the game's cursor sprite. The game already places
//                      its sprite at GetCursorPos every frame (0x00491380), but draws it a frame or more late, so it
//                      trailed the pointer and felt slow and accelerated (the player, 2026-10-09). The Windows
//                      pointer has no lag and exactly the desktop's speed. Hidden again in gameplay.
//   clip_cursor=1      while the game window is in front, the pointer stays inside it: the game opens the mouse
//                      non-exclusive (cooperative level 6) and clips only in exclusive mode (0x004f82d0), so in a
//                      window or borderless the hidden pointer wandered onto other monitors and a click there took
//                      the focus away. Released when the window loses focus.
#include <windows.h>

#include "config.h"
#include "game.h"
#include "game/game_object.h"
#include "game/ui_pass.h"
#include "log.h"
#include "settings.h"

namespace
{
bool g_hardwareCursor, g_clipCursor;
bool g_shown;        // our ShowCursor state (the game hides the pointer once at window creation, 0x004e0300)
bool g_clipped;
HWND g_hwnd;
WNDPROC g_nextWndProc;

void SetShown(bool show)
{
    if (show == g_shown)
        return;
    g_shown = show;
    // ShowCursor keeps a counter: step it until it crosses zero the way we want.
    if (show)
        while (ShowCursor(TRUE) < 0)
        {
        }
    else
        while (ShowCursor(FALSE) >= 0)
        {
        }
}

void Unclip()
{
    if (g_clipped)
        ClipCursor(nullptr), g_clipped = false;
}

void Clip()
{
    if (!g_hwnd || GetForegroundWindow() != g_hwnd || IsIconic(g_hwnd))
        return Unclip();
    RECT r;
    GetClientRect(g_hwnd, &r);
    POINT a{r.left, r.top}, b{r.right, r.bottom};
    ClientToScreen(g_hwnd, &a);
    ClientToScreen(g_hwnd, &b);
    RECT s{a.x, a.y, b.x, b.y};
    ClipCursor(&s);
    g_clipped = true;
}

// Focus changes release the clip at once (the per-frame clip would not run while another window is in front).
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_KILLFOCUS || (msg == WM_ACTIVATE && LOWORD(wp) == WA_INACTIVE) ||
        (msg == WM_ACTIVATEAPP && !wp))
        Unclip();
    if (msg == WM_SETCURSOR && g_shown && LOWORD(lp) == HTCLIENT)
    {
        SetCursor(LoadCursor(nullptr, IDC_ARROW));
        return TRUE;
    }
    return CallWindowProc(g_nextWndProc, hwnd, msg, wp, lp);
}

// Once per frame from the UI pass (src/game/ui_pass.cpp), where the cursor sprite would be drawn.
bool CursorSprite(bool active)
{
    if (!g_hardwareCursor)
        SetShown(false);  // switched off in the menu: the game's hidden pointer again
    if (!g_hardwareCursor && !g_clipCursor)
        return active;  // the original
    if (!g_hwnd && g_game)
    {
        g_hwnd = Field<HWND>(g_game, 0x64);  // the game window (0x00491380 reads it there)
        if (g_hwnd)
            g_nextWndProc = reinterpret_cast<WNDPROC>(
                SetWindowLongPtrA(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&WndProc)));
    }
    if (g_clipCursor)
        Clip();
    if (!g_hardwareCursor)
        return active;
    SetShown(active && g_hwnd && GetForegroundWindow() == g_hwnd);
    if (g_shown)
        SetCursor(LoadCursor(nullptr, IDC_ARROW));
    return false;  // the Windows pointer replaces the sprite
}
}  // namespace

void InputInit()
{
    using S = RemasterSetting;
    g_hardwareCursor = RemasterRegister({"input", "hardware_cursor", L"Windows mouse pointer in menus", S::Toggle, 0, 1,
                                         1, {}, 0, false, [](float v) { g_hardwareCursor = v != 0.0f; }},
                                        0, 1) != 0.0f;
    g_clipCursor = RemasterRegister({"input", "clip_cursor", L"Keep the mouse in the window", S::Toggle, 0, 1, 1, {},
                                     0, false,
                                     [](float v) {
                                         g_clipCursor = v != 0.0f;
                                         if (!g_clipCursor)
                                             Unclip();
                                     }},
                                    0, 1) != 0.0f;
    g_uiCursorSprite = CursorSprite;  // the original sprite and no clipping while both are off
    Log("input: hardware cursor %d, clip cursor %d", g_hardwareCursor ? 1 : 0, g_clipCursor ? 1 : 0);
}
