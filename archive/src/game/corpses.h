#pragma once
// Dead characters (docs/subsystems/decals-bodies.md): the die function 0x004319e0 sets a removal timer (+0x9f0;
// 5.5 s for ragdolls, 1.0 s for animated deaths) and a fade length (+0x9f4, 0.5 s). The character update then counts
// it down with Corpse_CountDown, fades the body out over the fade length and removes it at zero.

#include <cstdint>

// Enhancement point (hook/enhance/persistence.cpp): asked every frame for each dead character still counting down.
// True keeps the body as it is this frame (no countdown, no fade, no removal). Null (the default): original.
extern bool (*g_corpseHold)(void* character);

// 0x0042eba0 (esi = character).
void Corpse_CountDown(uint8_t* character);
