#pragma once
// Per-object motion blur (remaster, [render] shutter_speed; docs: the player, 2026-10-12: "per-object ... I don't want
// camera pans to blur the screen"). Runs at rhi::SceneEnd after temporal AA, before the snow and the UI.
//
// The motion target (vk_abi.h, "Motion vectors") holds each pixel's full motion: camera and object together. The
// camera's share is rebuilt from the depth buffer (this pixel's camera-space position taken to last frame's camera
// and projection) and subtracted, so only what moves in the world blurs. Then McGuire et al. 2012 ("A Reconstruction
// Filter for Plausible Motion Blur"): per-pixel velocity in pixels (scaled by the shutter, clamped), the largest
// velocity per 16x16 tile, its 3x3 neighbourhood maximum, and a depth-aware gather along that maximum.

#include <string>

namespace rhi::vulkan
{
// One compute shader per pass: 0 velocity, 1 tile maximum, 2 neighbourhood maximum, 3 reconstruction.
std::string MotionBlurGlsl(int pass);

constexpr int kMotionBlurTile = 48;  // = the maximum blur radius (G3D / McGuire: a 3x3 neighbourhood then reaches every source)

// Uniform block (std140), mirrored in MotionBlurGlsl.
struct alignas(16) MotionBlurParams
{
    float camToPrevCam[16];  // this frame's camera space -> last frame's camera space (column-major for GLSL)
    float proj[4];           // this frame's projection _11 _22 _33 _34
    float prevProj[4];       // last frame's projection _11 _22 _33 _34
    float misc[4];           // _43 (this frame), _43 (last frame), exposure / frame time, max radius (px)
    float size[4];           // width, height, tiles x, tiles y
};
}  // namespace rhi::vulkan
