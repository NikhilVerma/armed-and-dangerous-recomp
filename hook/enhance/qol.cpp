// Quality-of-life enhancements (docs/goals/remaster-basics.md item 6; evidence: docs/subsystems/qol.md). Switches in
// [qol] in and.ini, all off by default.
//
//   quicksave_key=F5     a key that opens the game's own QuickSave screen in a level (0x00403560, the same path as
//                        the pause menu item). Names F1..F12, or a DirectInput scan code number. Empty: none.
//   quickload_key=F9     a key that loads the quicksave (0x004a2ef0, the pause menu's Quickload; the game shows its
//                        own message when there is none)
//   mouse_invert_y=1     invert vertical mouse look
//   mouse_x=1.0          horizontal and vertical mouse sensitivity multipliers, on top of the game's own slider
//   mouse_y=1.0          (which scales both axes alike)
//
// The keys are read from the keyboard state the game itself polls (DirectInput), so scripted test input reaches
// them too; the actions run once per frame after Present, on the game's thread.
#include <windows.h>
#include <dinput.h>

#include <cmath>
#include <cstdlib>
#include <cstring>

#include "config.h"
#include "game.h"
#include "game/game_object.h"
#include "gen/orig_calls.h"
#include "log.h"
#include "settings.h"

extern void (*g_keyboardObserver)(const BYTE* state);  // hook/src/input_hooks.cpp
extern void (*g_mouseFilter)(LONG* dx, LONG* dy);

namespace
{
BYTE g_saveKey, g_loadKey;
bool g_saveDown, g_loadDown, g_savePending, g_loadPending;
bool g_invertY;
float g_mouseX = 1.0f, g_mouseY = 1.0f, g_restX, g_restY;

BYTE ParseKey(const char* name)
{
    if (!name[0])
        return 0;
    if ((name[0] == 'F' || name[0] == 'f') && name[1] >= '1' && name[1] <= '9')
    {
        int n = atoi(name + 1);
        static const BYTE f[] = {0, DIK_F1, DIK_F2, DIK_F3, DIK_F4,  DIK_F5, DIK_F6,
                                 DIK_F7, DIK_F8, DIK_F9, DIK_F10, DIK_F11, DIK_F12};
        return n >= 1 && n <= 12 ? f[n] : 0;
    }
    return static_cast<BYTE>(atoi(name));
}

void OnKeyboard(const BYTE* state)
{
    if (!g_saveKey && !g_loadKey)
        return;
    bool save = g_saveKey && (state[g_saveKey] & 0x80), load = g_loadKey && (state[g_loadKey] & 0x80);
    if (save && !g_saveDown)
        g_savePending = true;
    if (load && !g_loadDown)
        g_loadPending = true;
    g_saveDown = save, g_loadDown = load;
}

// Scaled deltas keep their fractions, so slow movements are not lost to rounding.
void OnMouse(LONG* dx, LONG* dy)
{
    if (!g_invertY && g_mouseX == 1.0f && g_mouseY == 1.0f)
        return;
    float x = *dx * g_mouseX + g_restX, y = *dy * g_mouseY * (g_invertY ? -1.0f : 1.0f) + g_restY;
    *dx = static_cast<LONG>(std::trunc(x)), *dy = static_cast<LONG>(std::trunc(y));
    g_restX = x - *dx, g_restY = y - *dy;
}

// Game+0x568 bit 3: what the game itself requires for saving and quickloading (0x00403560, 0x004a2e90). It is not
// simply "in a level" (it is clear early in lev_1_m1, flags 0x02 at play frame 600); the hotkeys follow the game's
// own rule, like the pause menu items.
bool SaveAllowed()
{
    return g_game && (g_game->shellFlags & 8) && !g_game->loadingLevel;
}
}  // namespace

void QolInit()
{
    using S = RemasterSetting;
    char buf[32];
    ConfigString("qol", "quicksave_key", ConfigOriginalDefaults() ? "" : "F5", buf, sizeof(buf));
    g_saveKey = ParseKey(buf);
    ConfigString("qol", "quickload_key", ConfigOriginalDefaults() ? "" : "F9", buf, sizeof(buf));
    g_loadKey = ParseKey(buf);
    // The page offers the usual pair; and.ini can name other keys.
    RemasterRegister({"qol", "quick_keys", L"Quicksave F5 / quickload F9", S::Toggle, 0, 1, 1, {}, 0, false,
                      [](float v) {
                          g_saveKey = v != 0.0f ? DIK_F5 : 0;
                          g_loadKey = v != 0.0f ? DIK_F9 : 0;
                      }},
                     g_saveKey || g_loadKey ? 1.0f : 0.0f);
    g_invertY = RemasterRegister({"qol", "mouse_invert_y", L"Invert mouse Y", S::Toggle, 0, 1, 1, {}, 0, false,
                                  [](float v) { g_invertY = v != 0.0f; }},
                                 0) != 0.0f;
    g_mouseX = RemasterRegister({"qol", "mouse_x", L"Mouse sensitivity X", S::Slider, 0.25f, 3.0f, 0.05f, {}, 0, false,
                                 [](float v) { g_mouseX = v; }},
                                1.0f);
    g_mouseY = RemasterRegister({"qol", "mouse_y", L"Mouse sensitivity Y", S::Slider, 0.25f, 3.0f, 0.05f, {}, 0, false,
                                 [](float v) { g_mouseY = v; }},
                                1.0f);
    g_keyboardObserver = OnKeyboard;  // both do nothing with the original settings
    g_mouseFilter = OnMouse;
    Log("qol: quicksave key 0x%02x, quickload key 0x%02x, mouse x %.2f y %.2f invert %d", g_saveKey, g_loadKey, g_mouseX,
        g_mouseY, g_invertY ? 1 : 0);
}

// Once per frame after Present (both backends).
void QolOnFrame()
{
    if (g_savePending)
    {
        g_savePending = false;
        if (SaveAllowed())
        {
            Log("qol: quicksave");
            orig::fn_00403560(reinterpret_cast<uint32_t>(g_game));
        }
        else
            Log("qol: quicksave not allowed by the game now (flags 0x%02x)", g_game ? g_game->shellFlags : 0);
    }
    if (g_loadPending)
    {
        g_loadPending = false;
        if (SaveAllowed())
        {
            Log("qol: quickload");
            orig::fn_004a2ef0();
        }
    }
}
