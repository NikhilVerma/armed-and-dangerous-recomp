#pragma once
// Ambient occlusion for the remaster ([render] ao): XeGTAO (Intel, MIT; third_party/XeGTAO, unmodified) run on the
// scene's depth buffer at SceneEnd, then applied to the frame. The game has no normal or ambient-only buffer, so
// normals come from depth (XeGTAO's in-place option) and the occlusion darkens the lit colour. Fog is respected:
// the apply pass removes the game's fog from a pixel, darkens what is left and puts the fog back, so occlusion
// never shows through fog (vk_device.cpp VulkanDevice::Ambient).

#include <string>

#include "vk_fog.h"

namespace rhi::vulkan
{
// The HLSL of the three passes (entry points CSPrefilterDepths16x16, CSGTAOHigh, CSDenoiseLastPass): XeGTAO.h,
// XeGTAO.hlsli and src/rhi/vulkan/shaders/gtao.hlsl, embedded at build time (hook/CMakeLists.txt).
std::string GtaoHlsl();

// The apply pass (full-screen triangle).
std::string GtaoApplyVertexGlsl();
std::string GtaoApplyFragmentGlsl();

// Apply-pass uniform block (std140), mirrored in GtaoApplyFragmentGlsl.
struct alignas(16) GtaoApplyParams
{
    float viewport[4];  // scene viewport x y w h (pixels)
    float fog[4];       // fog start, end (camera distances: the game's linear fog), enabled (1/0), AO strength
    float fogColor[4];  // rgb
    ModernFogParams mf; // modern fog (vk_fog.h): used instead of fog[] when mf.on.x is set
    // With ray-traced sun visibility (binding 3): occlusion applies to the ambient share of the light only.
    float sun[4];       // camera-space direction towards the sun; w: the sun's share s (shadow strength), 0 = no RT
    float proj[4];      // scene projection _11 _22 _33 _34
    float proj43[4];    // _43
};
}  // namespace rhi::vulkan
