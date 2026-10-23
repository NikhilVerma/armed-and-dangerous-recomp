#pragma once
#include <windows.h>
#include <d3d9.h>

// Display enhancements (goal 3). All switches live in [display] in and.ini and default to off.
//
//   windowed=1          run in a window instead of fullscreen
//   width=, height=     force the back-buffer size (0 = keep the game's choice). EXPERIMENTAL until the
//                       game's own resolution variables are found; the game may still draw at its own size.
//   vsync=-1|0|1        -1 keep the game's choice, 0 off (immediate), 1 on
//   fps_limit=0         frame cap in Present (0 = none). The game's own timing still applies.
//   borderless=1        a popup window covering the monitor (implies windowed); Present scales the back buffer to it
//   render_scale=1.0    windowed: the window shows the back buffer at 1/scale (2.0 = 2x supersampling)
//   brightness_shader=1 the game's brightness option applied by the Vulkan present pass (works in a window)

void DisplayInit();
void DisplayAdjustParams(D3DPRESENT_PARAMETERS* pp);
void DisplayAdjustWindow(HWND wnd, const D3DPRESENT_PARAMETERS* pp);
void DisplayBeforePresent(IDirect3DDevice9* dev);
void DisplayAfterPresent(long long presentTicks);  // time spent inside Present, for the fps log
