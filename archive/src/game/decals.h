#pragma once
// The decal manager (Game+0xa7c; docs/subsystems/decals-bodies.md). Decals live in a fixed pool allocated at level
// setup; when fewer than 32 are free or fading, the oldest visible decal starts to fade, so about 128 stay visible.

#include <cstdint>

// Enhancement point (hook/enhance/persistence.cpp). The defaults are the original game's constants.
struct DecalLimits
{
    uint32_t poolSize = 160;     // decals allocated per level (0x00416152 mov ebx, 0xa0)
    uint32_t fadeReserve = 32;   // start fading the oldest decal while free + fading < this (0x0041644f cmp eax, 0x20)
};
extern DecalLimits g_decalLimits;

// 0x00416150 (edi = manager): allocates the pool into the free list (manager +8).
uint32_t Decals_AllocatePool(void* manager);
// 0x00416440 (eax = manager; stack: frame time, unused): pool pressure fade, fade-length easing, per-decal update.
uint32_t Decals_Update(void* manager, uint32_t dt, uint32_t arg1);
