#pragma once
// The game camera's per-frame projection (docs/subsystems/camera.md). The camera object lives at Game+0xa68; its
// FOV (+0xf4) is the full vertical angle in radians (default 60 degrees).

#include <cstdint>

// Enhancement point (hook/enhance/camera.cpp): called by Camera_SetupProjection before the projection is built,
// with the camera's vertical FOV and the aspect argument of 0x004f0100 (0: the original rule, viewport width /
// height divided by Renderer_GetAspect). Null (the default) keeps the original behaviour exactly.
extern void (*g_cameraProjectionAdjust)(void* camera, float* fov, float* aspect);

// 0x0040f6e0 (edi = camera).
uint32_t Camera_SetupProjection(void* camera);

// Enhancement point (hook/enhance/render.cpp, [render] draw_distance): called by 0x0040f8c0 with the far scale after
// the game's clamp (0.25 .. 1.0, from the draw-distance option Game+0x4b0); returns the scale to use. The scale
// multiplies the far plane (camera +0xd4) and the fog range (+0xdc, +0xe0). Null keeps the original exactly.
extern float (*g_cameraFarScaleAdjust)(float scale);
