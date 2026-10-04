#pragma once
// GPU snowfall for the remaster ([render] snow; docs/subsystems/weather.md). Replaces the game's camera-locked snow
// cylinders (textured meshes scrolling around the camera; the "snow bubbles" are the innermost one crossing the
// near plane). Each flake is a function of its index and the game time: a hashed home position in a box that wraps
// around the camera, gravity, wind drift and turbulence, so the field is stable without simulation buffers. Drawn
// as soft instanced sprites against the scene's depth buffer (flakes behind or under surfaces are hidden, flakes
// near a surface fade), after the shadow maps and before the HUD.

#include <string>

namespace rhi::vulkan
{
std::string SnowVertexGlsl();
std::string SnowFragmentGlsl();

// Uniform block (std140), mirrored in the shaders.
struct alignas(16) SnowParams
{
    float view[16];      // world -> camera, column-major for GLSL
    float proj[4];       // scene projection _11 _22 _33 _34
    float proj2[4];      // _43, time, flake radius, box half size
    float camPos[4];     // camera position (world)
    float motion[4];     // wind x, wind z, fall speed, turbulence
    float viewport[4];   // scene viewport x y w h (pixels)
    float target[4];     // target width, height
};
}  // namespace rhi::vulkan
