#pragma once
// The game's 3D scene pass and its view frustum (docs/subsystems/shadows.md "Per frame").

#include <cstdint>

// Enhancement point (hook/enhance/shadows.cpp): when > 0, the main scene pass (flags 0xffffffff, from 0x00489610)
// moves its frustum's four side planes and its near plane out by this many world units before the world gather,
// so nothing within that distance of the camera is culled (shadow casters behind and beside the view reach the
// renderer). 0, the default, is the original behaviour.
extern float g_sceneCullRadius;
// The world-space direction sunlight travels (zero: unknown, then all planes move out by the full radius).
extern float g_sceneCullSun[3];

// Enhancement point (hook/enhance/render.cpp, [render] lod_max): when true, Object_SelectLod 0x00452f50 measures
// every object at view distance 0, so the finest level of detail the object has is always chosen. False (the
// default) is the original distance-based choice.
extern bool g_lodForceFinest;

// 0x004889a0 (cl = also the far plane, edx = planes out, edi = view-projection; stack: unused, callee pops 4):
// the frustum planes of a 0x44-byte view-projection matrix, normalised by their xyz length. Returns the count,
// 5 or 6 (left, right, bottom, top, near[, far]).
uint32_t Frustum_FromViewProjection(bool withFar, float* planes, const float* viewProjection);

// 0x0048eab0 (eax = plane count, ecx = planes, ebx = result; stack: world): clips the terrain grid against the
// planes and collects the world cells the clipped polygon covers into `result`.
uint32_t World_GatherFrustum(uint32_t count, const float* planes, uint32_t* result, uint32_t world);

// 0x00488e90 (stdcall: pass, translucent): draws the pass's render layers (opaque buckets, or the sorted layer).
uint32_t ScenePass_DrawLayers(void* pass, bool translucent);
// 0x0048a1a0: renderer virtual +0xa4 (0, 0, 0, 0, stage) for texture stages 0..3.
uint32_t ScenePass_ResetStages();

// 0x00489070 (stdcall: pass, flags, arg2, arg3): one scene pass with the renderer's current view and projection.
// Flags: 0x10 sky, 1 | 8 world gather, 2 decals, 4 the game's shadows, 0x20 fx, 0x40 post-process, 0x80 HUD
// markers. Callers: 0x00489610 (main scene, 0xffffffff), 0x00489320 (env map), 0x00489540 (render to texture).
uint32_t ScenePass_Render(void* pass, uint32_t flags, uint32_t arg2, uint32_t arg3);
