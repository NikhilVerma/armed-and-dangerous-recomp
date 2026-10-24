#pragma once
// The UI pass (docs/subsystems/ui.md): draws world-tracking markers, every visible UI object, the list at ui +0x9c
// and the object at ui +0x54 in the normalised screen space set by 0x004f01c0.

#include <cstdint>

// Called for each visible UI object right before its draw virtual (+0x13c), after the pass has set the screen
// transform; the enhancement may set its own view matrix (renderer virtual 20). Null by default (original
// behaviour).
extern void (*g_uiObjectBeginDraw)(void* object);
// Called right after the object's draw path (draw virtual +0x13c and the 0x0048a1a0 flush), before the pass queries
// virtual +0x150. The pass resets world/view/projection to the screen transform after each object ONLY when
// (virtual +0x150 result & 2) == 0; otherwise the view would leak to the next object, so an enhancement that
// changed the view must restore it here. Null by default (original behaviour).
extern void (*g_uiObjectEndDraw)(void* object);
// Called once per frame when the UI pass starts, before anything of it is drawn: the 3D scene is complete then
// (hook/enhance/shadows.cpp adds its passes here). Null by default.
extern void (*g_uiPassBegin)();
// Called once per frame where the pass draws the mouse cursor sprite (ui +0x54), with whether the original would draw
// it (ui byte +0x50 set and the sprite present); returns whether to draw it. Null by default (original behaviour).
extern bool (*g_uiCursorSprite)(bool active);

// 0x00464460 (stack: ui = Game+0xab4 screen stack; callee pops; caller 0x00403050, once per frame). Returns the
// iterator's node pointer left after the last list walk (eax at 0x0046472f).
uint32_t UiPass_Draw(void* ui);
