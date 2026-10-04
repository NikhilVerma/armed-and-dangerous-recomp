#pragma once
// Shadow maps for the remaster ([render] shadow_maps; docs/subsystems/shadows.md). At SceneEnd (the UI pass starts,
// the 3D scene is complete) the device re-draws this frame's opaque scene draws depth-only from the sun into a
// shadow map: the game's own vertex shaders write oPos = r0 . c22..c25 with r0 the camera-space position (vk_scene.cpp),
// so replacing c22..c25 with camera->light-clip rows renders them from the light, skinned meshes included. A
// full-screen pass then darkens the back buffer where the depth buffer's surface is lit by the sun but occluded in
// the shadow map, before the HUD is drawn.

#include <cstdint>
#include <string>

namespace rhi::vulkan
{
// 4x4 matrix for column vectors: out_i = sum_j m[i * 4 + j] in_j (the game's matrices use the same layout).
struct Mat4
{
    float m[16];
};
Mat4 Multiply(const Mat4& a, const Mat4& b);  // a * b (b applied first)
bool Invert(const Mat4& a, Mat4& out);

// The sun's orthographic view-projection (Vulkan clip: depth 0..1) for a box of half size `range` around `centre`,
// light travelling along `dir`. The box is snapped to whole shadow-map texels (`size`) so it does not shimmer as the
// camera moves.
Mat4 LightMatrix(const float centre[3], const float dir[3], float range, uint32_t size);

// GLSL: the depth pass's fragment shader (writes nothing), and the apply pass (full-screen triangle).
std::string ShadowDepthFragmentGlsl();
std::string ShadowApplyVertexGlsl();
std::string ShadowApplyFragmentGlsl();

// The apply pass's uniform block (std140), mirrored in ShadowApplyFragmentGlsl.
struct alignas(16) ShadowApplyParams
{
    float camToLight[16];  // column-major for GLSL (mat4)
    float proj[4];         // scene projection _11, _22, _33, _34
    float misc[4];         // _43, strength, shadow-map texel size, depth bias
    float sunCam[4];       // xyz: camera-space direction towards the sun; w: light depth range (world units)
    float viewport[4];     // scene viewport x, y, width, height (pixels of the target)
    float soft[4];         // tan(sun angular radius), shadow-map width (world units), max filter (texels)
};
}  // namespace rhi::vulkan
