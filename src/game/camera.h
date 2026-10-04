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
