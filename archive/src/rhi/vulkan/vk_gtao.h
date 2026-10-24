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
std::string GtaoHlsl(float thickness = 0.0f, int slices = 3);  // [render] ao_thickness, ao_slices (vk_gtao.cpp)

// The apply pass (full-screen triangle).
std::string GtaoApplyVertexGlsl();
std::string GtaoApplyFragmentGlsl(bool afterlight = false, bool half = false);  // [render] afterlight: the GI composite

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
    float material[4];  // x: share of the occlusion kept on snow-white surfaces
    float gi[4];        // Afterlight composite: x reality gain, y max relight stops, z ambient fill, w GI scale
};

// Afterlight constants (shaders/afterlight.hlsl AfterlightConstants, HLSL cbuffer packing: float4 rows).
struct alignas(16) AfterlightConstants
{
    float giGain, giEmitterBoost, giRadianceMipBias, giDistanceFalloff;
    float giRadiusScale, giFalloffMul, giFalloffAdd, giRadianceFalloffPower;
    float giSourceCosine, giSourceGate, giSourceGateKnee, giMaxSampleRadiance;
    float emitterThreshold, emitterContrast, sliceCount, thickness;
    float sceneIsLinear, pad[3];
};
}  // namespace rhi::vulkan
