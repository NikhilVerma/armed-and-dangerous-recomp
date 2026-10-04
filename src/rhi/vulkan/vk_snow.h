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
std::string SnowComputeGlsl();
std::string SnowVertexGlsl();
std::string SnowFragmentGlsl();

// Uniform block (std140), mirrored in the shaders.
struct alignas(16) SnowParams
{
    float view[16];      // world -> camera, column-major for GLSL
    float proj[4];       // scene projection _11 _22 _33 _34
    float proj2[4];      // _43, time, flake radius, horizontal radius of the volume
    float camPos[4];     // camera position (world), w: 1 = fill the volume this frame
    float motion[4];     // wind x, wind z, terminal fall speed, turbulence
    float viewport[4];   // scene viewport x y w h (pixels)
    float target[4];     // target width, height, flake count, frame time (s)
    float color[4];      // flake colour (rgb) and opacity
    float sim[4];        // sky height above the camera, depth below it, drag time (s), streak exposure (s)
};
}  // namespace rhi::vulkan
