#pragma once
// Ray-traced sun shadows for the remaster ([render] shadow_rt; needs ray queries, Context::rayQuery). Replaces the
// shadow map pass (vk_shadows.h) when on.
//
// Geometry: every frame the opaque scene draws that write depth (and the static draws of the caster cache that the
// game culled from the view: VulkanDevice::Shadows) are replayed through their vertex shaders' capture variants
// with the projection registers c22..c25 replaced by camera -> world, so the captured "clip positions" are world
// positions (w = 1). Their index data (strips and fans converted) becomes one triangle list; one bottom-level and
// one top-level acceleration structure are built per frame.
//
// Shading: per pixel, the world position from the depth buffer, a few rays towards points spread over the sun's
// disc (angular radius shadow_softness): the fraction blocked is the shadow. Penumbrae widen and fade with the
// distance to the occluder because that is what the rays see: no blocker estimate, no shadow-map extent, no bias
// in depth units. Applied like the shadow maps: sky-tinted, under the fog, before the translucent draws.

#include <string>

#include "vk_fog.h"

namespace rhi::vulkan
{
std::string RtShadowVertexGlsl();
// remote: the visibility comes from the 64-bit helper's buffer (binding 1, rt_ipc.h) instead of ray queries here.
std::string RtShadowFragmentGlsl(bool remote = false);
// Helper path: the scene depth (0..1) per pixel of the viewport, into a shared buffer (rt_ipc.h kDepth).
std::string RtDepthComputeGlsl();

struct alignas(16) RtShadowParams
{
    float camToWorld[3][4];  // rows: camera -> world
    float proj[4];           // scene projection _11 _22 _33 _34
    float misc[4];           // _43, strength, rays per pixel, frame index
    float sun[4];            // xyz: world direction towards the sun; w: tan(angular radius)
    float viewport[4];       // x y w h
    float fog[4];            // start, end (camera distances), enabled, debug
    float fogColor[4];
    ModernFogParams mf;
};
}  // namespace rhi::vulkan
