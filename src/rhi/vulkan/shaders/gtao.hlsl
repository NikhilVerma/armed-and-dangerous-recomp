// XeGTAO (third_party/XeGTAO, MIT, unmodified) entry points for the Vulkan backend (vk_gtao.cpp). Adapted from
// XeGTAO's own sample wrapper (Source/Rendering/Shaders/vaGTAO.hlsl): the resources carry Vulkan bindings, normals
// are generated in place from depth (the game has no normal buffer), depths are 32-bit, no half precision.
// Compiled with glslang's HLSL front end; vk_gtao.cpp puts XeGTAO.h and XeGTAO.hlsli in front of this text in place
// of the #includes below.
//
// Bindings (one push-descriptor layout for all passes; each pass uses a subset):
//   0 constants, 1 point-clamp sampler, 2 scene depth (0..1, D3D convention), 3..7 view-depth MIP 0..4 (write),
//   8 view depth with MIPs (read), 9 AO term out, 10 edges out, 11 AO term in, 12 edges in, 13 final AO out

[[vk::binding(0)]] cbuffer GTAOConstantBuffer
{
    GTAOConstants g_GTAOConsts;
}

// XeGTAO.hlsli goes here (vk_gtao.cpp).
#include "XeGTAO.hlsli"

[[vk::binding(1)]] SamplerState g_samplerPointClamp;
[[vk::binding(2)]] Texture2D<float> g_srcRawDepth;
[[vk::binding(3)]] [[vk::image_format("r32f")]] RWTexture2D<float> g_outWorkingDepthMIP0;
[[vk::binding(4)]] [[vk::image_format("r32f")]] RWTexture2D<float> g_outWorkingDepthMIP1;
[[vk::binding(5)]] [[vk::image_format("r32f")]] RWTexture2D<float> g_outWorkingDepthMIP2;
[[vk::binding(6)]] [[vk::image_format("r32f")]] RWTexture2D<float> g_outWorkingDepthMIP3;
[[vk::binding(7)]] [[vk::image_format("r32f")]] RWTexture2D<float> g_outWorkingDepthMIP4;
[[vk::binding(8)]] Texture2D<float> g_srcWorkingDepth;
[[vk::binding(9)]] [[vk::image_format("r8ui")]] RWTexture2D<uint> g_outWorkingAOTerm;
[[vk::binding(10)]] [[vk::image_format("r8")]] RWTexture2D<unorm float> g_outWorkingEdges;
[[vk::binding(11)]] Texture2D<uint> g_srcWorkingAOTerm;
[[vk::binding(12)]] Texture2D<float> g_srcWorkingEdges;
[[vk::binding(13)]] [[vk::image_format("r8ui")]] RWTexture2D<uint> g_outFinalAOTerm;

// From vaGTAO.hlsl: Hilbert curve driving R2 (no temporal accumulation here, so temporalIndex is 0).
lpfloat2 SpatioTemporalNoise(uint2 pixCoord, uint temporalIndex)
{
    uint index = HilbertIndex(pixCoord.x, pixCoord.y);
    index += 288 * (temporalIndex % 64);
    return lpfloat2(frac(0.5 + index * float2(0.75487766624669276005, 0.5698402909980532659114)));
}

[numthreads(8, 8, 1)]
void CSPrefilterDepths16x16(uint2 dispatchThreadID : SV_DispatchThreadID, uint2 groupThreadID : SV_GroupThreadID)
{
    XeGTAO_PrefilterDepths16x16(dispatchThreadID, groupThreadID, g_GTAOConsts, g_srcRawDepth, g_samplerPointClamp,
                                g_outWorkingDepthMIP0, g_outWorkingDepthMIP1, g_outWorkingDepthMIP2,
                                g_outWorkingDepthMIP3, g_outWorkingDepthMIP4);
}

// Quality "high" of the sample (3 slices, 3 steps per slice).
[numthreads(XE_GTAO_NUMTHREADS_X, XE_GTAO_NUMTHREADS_Y, 1)]
void CSGTAOHigh(const uint2 pixCoord : SV_DispatchThreadID)
{
    XeGTAO_MainPass(pixCoord, 3, 3, SpatioTemporalNoise(pixCoord, g_GTAOConsts.NoiseIndex), (lpfloat3)0,
                    g_GTAOConsts, g_srcWorkingDepth, g_samplerPointClamp, g_outWorkingAOTerm, g_outWorkingEdges);
}

// Each thread does two horizontal pixels (as in the sample). Two passes ("medium" in the sample): an intermediate
// pass, then the last one.
[numthreads(XE_GTAO_NUMTHREADS_X, XE_GTAO_NUMTHREADS_Y, 1)]
void CSDenoisePass(const uint2 dispatchThreadID : SV_DispatchThreadID)
{
    const uint2 pixCoordBase = dispatchThreadID * uint2(2, 1);
    XeGTAO_Denoise(pixCoordBase, g_GTAOConsts, g_srcWorkingAOTerm, g_srcWorkingEdges, g_samplerPointClamp,
                   g_outFinalAOTerm, false);
}

[numthreads(XE_GTAO_NUMTHREADS_X, XE_GTAO_NUMTHREADS_Y, 1)]
void CSDenoiseLastPass(const uint2 dispatchThreadID : SV_DispatchThreadID)
{
    const uint2 pixCoordBase = dispatchThreadID * uint2(2, 1);
    XeGTAO_Denoise(pixCoordBase, g_GTAOConsts, g_srcWorkingAOTerm, g_srcWorkingEdges, g_samplerPointClamp,
                   g_outFinalAOTerm, true);
}
