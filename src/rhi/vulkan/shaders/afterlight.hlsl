// Afterlight unified AO + GI (the player's SSLD GTAO, D:\devbox\code\afterlight-gtao, MIT; export/SSLD_GTAO.fxh and
// SSLD_GTAO.fx, single-pass graph, SSLD_GTAO_TWO_TIER 0). One XeGTAO horizon search per pixel gives both the
// occlusion and the bounced light: the gather keeps its own horizon on a wider ring inside the same slice and step
// loops, and a tap that raises it is credited the drop in the cosine-weighted arc integral, so the gathered weight
// never exceeds the occluded share of the hemisphere (AO and GI split one hemisphere). Ported from the ReShade FX to
// plain HLSL compute for the Vulkan backend (vk_device.cpp Ambient, [render] afterlight); appended after
// gtao.hlsl, whose bindings 0..13 and XeGTAO helpers it uses. Differences from the FX:
//   - no off-screen depth extension, log-depth prefilter, two-tier passes, exposure state, history or LUT
//   - normals come from the prefiltered view depth (Intel's in-place method), stored for the gather and the blur
//   - the source radiance is one image with five MIPs (the FX keeps five textures; same 2x2 box reduction)
//   - [render] ao_thickness applies to both rings, as in the XeGTAO path (vk_gtao.cpp)
//
// Bindings 14..23 (one push-descriptor layout with gtao.hlsl):
//   14 Afterlight constants, 15 scene colour (as stored, display-encoded unless sceneIsLinear), 16 linear-clamp
//   sampler, 17 radiance MIP in (reduction source), 18 radiance MIP out, 19 radiance (all MIPs), 20 normals out,
//   21 normals (xyz view-space normal, w view z), 22 bounce out, 23 bounce in

struct AfterlightConstants
{
    float GIGain;                 // 0 = AO only (the gather is skipped)
    float GIEmitterBoost;         // extra transport from detected light sources
    float GIRadianceMipBias;
    float GIDistanceFalloff;      // 0 = off; >0 adds 1 / (1 + k d^2)

    float GIRadiusScale;          // gather ring / AO ring
    float GIFalloffMul;           // -1 / GIFalloffRange
    float GIFalloffAdd;           // GIFalloffFrom / GIFalloffRange + 1
    float GIRadianceFalloffPower;

    float GISourceCosine;         // 1 = Lambert cosine at the source, 0 = gate only
    float GISourceGate;           // 1 = reject back-facing sources
    float GISourceGateKnee;
    float GIMaxSampleRadiance;    // luminance ceiling for one sample

    float EmitterThreshold;
    float EmitterContrast;
    float SliceCount;
    float Thickness;              // [render] ao_thickness (world units), 0 = off

    float SceneIsLinear;          // 1: the colour target stores linear values
    float3 Pad;
};

[[vk::binding(14)]] cbuffer AfterlightBuffer
{
    AfterlightConstants g_AL;
}
[[vk::binding(15)]] Texture2D<float4> g_sceneColour;
[[vk::binding(16)]] SamplerState g_samplerLinearClamp;
[[vk::binding(17)]] Texture2D<float4> g_radIn;
[[vk::binding(18)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> g_radOut;
[[vk::binding(19)]] Texture2D<float4> g_radiance;
[[vk::binding(20)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> g_outNormals;
[[vk::binding(21)]] Texture2D<float4> g_normals;
[[vk::binding(22)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> g_outBounce;
[[vk::binding(23)]] Texture2D<float4> g_bounceIn;

static const float3 AL_LUMA = float3(0.2126, 0.7152, 0.0722);

float3 AL_SrgbToLinear(float3 c)
{
    return (c <= 0.04045) ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

float3 AL_SceneLinear(int2 p)
{
    float3 c = saturate(g_sceneColour.Load(int3(p, 0)).rgb);
    return g_AL.SceneIsLinear > 0.5 ? c : AL_SrgbToLinear(c);
}

// ---- Normals (Intel's in-place normal, XeGTAO.hlsli, written out once for the gather's source gate and the blur).
[numthreads(8, 8, 1)]
void CSAfterlightNormals(const uint2 pixCoord : SV_DispatchThreadID)
{
    const GTAOConstants consts = g_GTAOConsts;
    if (any(pixCoord >= uint2(consts.ViewportSize)))
        return;
    int2 p = int2(pixCoord);
    int2 lim = consts.ViewportSize - 1;
    float z = g_srcWorkingDepth.Load(int3(p, 0));
    float zl = g_srcWorkingDepth.Load(int3(clamp(p + int2(-1, 0), 0, lim), 0));
    float zr = g_srcWorkingDepth.Load(int3(clamp(p + int2(1, 0), 0, lim), 0));
    float zt = g_srcWorkingDepth.Load(int3(clamp(p + int2(0, -1), 0, lim), 0));
    float zb = g_srcWorkingDepth.Load(int3(clamp(p + int2(0, 1), 0, lim), 0));
    float2 uv = (float2(p) + 0.5) * consts.ViewportPixelSize;
    float2 px = consts.ViewportPixelSize;
    float3 c = XeGTAO_ComputeViewspacePosition(uv, z, consts);
    float3 l = XeGTAO_ComputeViewspacePosition(uv + float2(-px.x, 0), zl, consts);
    float3 r = XeGTAO_ComputeViewspacePosition(uv + float2(px.x, 0), zr, consts);
    float3 t = XeGTAO_ComputeViewspacePosition(uv + float2(0, -px.y), zt, consts);
    float3 b = XeGTAO_ComputeViewspacePosition(uv + float2(0, px.y), zb, consts);
    float4 edges = XeGTAO_CalculateEdges(z, zl, zr, zt, zb);
    g_outNormals[p] = float4(XeGTAO_CalculateNormal(edges, c, l, r, t, b), z);
}

// ---- Source radiance, MIP 0: linear colour, alpha = emitter confidence x luminance (SSLD_RadiancePixel).
[numthreads(8, 8, 1)]
void CSAfterlightRadiance(const uint2 pixCoord : SV_DispatchThreadID)
{
    const int2 size = g_GTAOConsts.ViewportSize;
    if (any(pixCoord >= uint2(size)))
        return;
    int2 p = int2(pixCoord);
    float3 lin = AL_SceneLinear(p);
    float lum = dot(lin, AL_LUMA);
    // Local mean on a 5x5 grid at stride 4 (~17 px): a lamp does not average itself away.
    float localSum = 0.0;
    [unroll] for (int dy = -2; dy <= 2; dy++)
        [unroll] for (int dx = -2; dx <= 2; dx++)
        {
            float3 c = AL_SceneLinear(clamp(p + int2(dx, dy) * 4, int2(0, 0), size - 1));
            localSum += dot(c * c, AL_LUMA);
        }
    float localMean = localSum / 25.0;
    float te = g_AL.EmitterThreshold * g_AL.EmitterThreshold;
    float bright = smoothstep(te * 0.5, te, lum);
    float standout = smoothstep(1.0, g_AL.EmitterContrast, lum / max(localMean, 1e-5));
    float hotCore = smoothstep(max(te, 0.75), 0.95, lum);
    float confidence = saturate(max(bright * standout, hotCore));
    // [render] afterlight_emitter_threshold >= 1: no detector (sunlit snow is near white and read as lamps).
    if (g_AL.EmitterThreshold >= 1.0)
        confidence = 0.0;
    if (g_srcRawDepth.Load(int3(p, 0)) >= 0.99999)
        confidence = 1.0;  // sky is a light source
    g_radOut[p] = float4(lin, confidence * lum);
}

// ---- Radiance MIP n from n-1: the 2x2 mean of RGB and emitter energy (SSLD_DownsampleRadiance).
[numthreads(8, 8, 1)]
void CSAfterlightRadianceMip(const uint2 pixCoord : SV_DispatchThreadID)
{
    uint w, h, ow, oh;
    g_radIn.GetDimensions(w, h);
    g_radOut.GetDimensions(ow, oh);
    if (pixCoord.x >= ow || pixCoord.y >= oh)
        return;
    int2 q = int2(pixCoord) * 2;
    int2 lim = int2(w, h) - 1;
    g_radOut[pixCoord] = 0.25 * (g_radIn.Load(int3(min(q, lim), 0)) + g_radIn.Load(int3(min(q + int2(1, 0), lim), 0)) +
                                 g_radIn.Load(int3(min(q + int2(0, 1), lim), 0)) +
                                 g_radIn.Load(int3(min(q + int2(1, 1), lim), 0)));
}

lpfloat XeGTAO_GI_InnerArc(lpfloat h, lpfloat n, lpfloat cosNorm)
{
    return (lpfloat(cosNorm) + lpfloat(2.0) * lpfloat(h) * sin(n) - cos(lpfloat(2.0) * lpfloat(h) - n)) / lpfloat(4.0);
}

// One sample's radiance (rgb) and its source-facing gate (a): SSLD XeGTAO_GI_SampleRadiance.
lpfloat4 AL_SampleRadiance(float2 screenPos, lpfloat mipLevel, lpfloat3 dirToSample)
{
    float lod = clamp(float(mipLevel) + g_AL.GIRadianceMipBias, 0.0, 4.0);
    lpfloat4 texel = g_radiance.SampleLevel(g_samplerLinearClamp, screenPos, lod);
    lpfloat baseLum = dot(texel.rgb, AL_LUMA);
    lpfloat emitterFraction = saturate(texel.a / max(baseLum, lpfloat(1e-6)));
    lpfloat3 radiance = texel.rgb * (lpfloat(1.0) + lpfloat(g_AL.GIEmitterBoost) * emitterFraction);
    lpfloat lum = dot(radiance, AL_LUMA);
    radiance *= (lum > lpfloat(g_AL.GIMaxSampleRadiance)) ? lpfloat(g_AL.GIMaxSampleRadiance) / max(lum, lpfloat(1e-6))
                                                           : lpfloat(1.0);
    lpfloat3 sourceNormal = normalize(g_normals.SampleLevel(g_samplerPointClamp, screenPos, 0).xyz);
    lpfloat nd = dot(sourceNormal, -dirToSample);
    lpfloat facing = lerp(lpfloat(1.0), smoothstep(lpfloat(0.0), lpfloat(g_AL.GISourceGateKnee), nd),
                          lpfloat(g_AL.GISourceGate));
    facing *= lerp(lpfloat(1.0), saturate(nd), lpfloat(g_AL.GISourceCosine));
    return lpfloat4(radiance, facing);
}

// Thickness limit ([render] ao_thickness): a sample far in front of the centre (a character before the ground
// behind it) fades out over another `thickness`.
lpfloat AL_Thickness(float3 delta)
{
    return g_AL.Thickness > 0.0 ? saturate(2.0 - (-delta.z) / g_AL.Thickness) : 1.0;
}

// ---- The unified pass: XeGTAO_MainPass (Intel) with the SSLD gather in the same loops (SSLD_GTAO.fxh 366-750).
// pixCoord: the full-resolution pixel; writeAO: also write the AO term and edges (the full-resolution unified pass);
// bouncePix: where the bounce goes (the same pixel, or the half-resolution texel this pixel stands for).
void AfterlightMain(const uint2 pixCoord, const bool writeAO, const uint2 bouncePix)
{
    const GTAOConstants consts = g_GTAOConsts;
    const lpfloat sliceCount = lpfloat(g_AL.SliceCount);
    const lpfloat stepsPerSlice = 3.0;
    const lpfloat2 localNoise = SpatioTemporalNoise(pixCoord, consts.NoiseIndex);
    const bool giOn = g_AL.GIGain > 0.0;
    float2 normalizedScreenPos = (float2(pixCoord) + float2(0.5, 0.5)) * consts.ViewportPixelSize;

    lpfloat4 valuesUL = g_srcWorkingDepth.GatherRed(g_samplerPointClamp, float2(pixCoord * consts.ViewportPixelSize));
    lpfloat4 valuesBR = g_srcWorkingDepth.GatherRed(g_samplerPointClamp, float2(pixCoord * consts.ViewportPixelSize),
                                                    int2(1, 1));
    lpfloat viewspaceZ = valuesUL.y;
    const lpfloat pixLZ = valuesUL.x;
    const lpfloat pixTZ = valuesUL.z;
    const lpfloat pixRZ = valuesBR.z;
    const lpfloat pixBZ = valuesBR.x;
    lpfloat4 edgesLRTB = XeGTAO_CalculateEdges(viewspaceZ, pixLZ, pixRZ, pixTZ, pixBZ);
    if (writeAO)
        g_outWorkingEdges[pixCoord] = XeGTAO_PackEdges(edgesLRTB);

    lpfloat3 viewspaceNormal = lpfloat3(g_normals.Load(int3(pixCoord, 0)).xyz);
    viewspaceZ *= 0.99999;
    const float3 pixCenterPos = XeGTAO_ComputeViewspacePosition(normalizedScreenPos, viewspaceZ, consts);
    const lpfloat3 viewVec = lpfloat3(normalize(-pixCenterPos));

    const lpfloat effectRadius = lpfloat(consts.EffectRadius) * lpfloat(consts.RadiusMultiplier);
    const lpfloat sampleDistributionPower = lpfloat(consts.SampleDistributionPower);
    const lpfloat thinOccluderCompensation = lpfloat(consts.ThinOccluderCompensation);
    const lpfloat falloffRange = lpfloat(consts.EffectFalloffRange) * effectRadius;
    const lpfloat falloffFrom = effectRadius * (lpfloat(1.0) - lpfloat(consts.EffectFalloffRange));
    const lpfloat falloffMul = lpfloat(-1.0) / (falloffRange);
    const lpfloat falloffAdd = falloffFrom / (falloffRange) + lpfloat(1.0);

    lpfloat visibility = 0.0;
    lpfloat3 gatheredRadiance = lpfloat3(0.0, 0.0, 0.0);
    lpfloat gatheredWeight = lpfloat(0.0);
    {
        const lpfloat noiseSlice = lpfloat(localNoise.x);
        const lpfloat noiseSample = lpfloat(localNoise.y);
        const lpfloat pixelTooCloseThreshold = 1.3;
        const float2 pixelDirRBViewspaceSizeAtCenterZ = float2(viewspaceZ, viewspaceZ) * consts.NDCToViewMul_x_PixelSize;
        lpfloat screenspaceRadius = effectRadius / lpfloat(pixelDirRBViewspaceSizeAtCenterZ.x);
        visibility += saturate((10.0 - screenspaceRadius) / 100.0) * 0.5;
        const lpfloat minS = lpfloat(pixelTooCloseThreshold) / screenspaceRadius;

        [loop] for (lpfloat slice = 0.0; slice < sliceCount; slice++)
        {
            lpfloat sliceK = (slice + noiseSlice) / sliceCount;
            lpfloat phi = sliceK * lpfloat(XE_GTAO_PI);
            lpfloat cosPhi = cos(phi);
            lpfloat sinPhi = sin(phi);
            lpfloat2 omega = lpfloat2(cosPhi, -sinPhi);
            omega *= screenspaceRadius;
            const lpfloat3 directionVec = lpfloat3(cosPhi, sinPhi, 0.0);
            const lpfloat3 orthoDirectionVec = directionVec - (dot(directionVec, viewVec) * viewVec);
            const lpfloat3 axisVec = normalize(cross(orthoDirectionVec, viewVec));
            lpfloat3 projectedNormalVec = viewspaceNormal - axisVec * dot(viewspaceNormal, axisVec);
            lpfloat signNorm = lpfloat(sign(dot(orthoDirectionVec, projectedNormalVec)));
            lpfloat projectedNormalVecLength = length(projectedNormalVec);
            lpfloat cosNorm = lpfloat(saturate(dot(projectedNormalVec, viewVec) / projectedNormalVecLength));
            lpfloat n = signNorm * XeGTAO_FastACos(cosNorm);
            const lpfloat lowHorizonCos0 = cos(n + lpfloat(XE_GTAO_PI_HALF));
            const lpfloat lowHorizonCos1 = cos(n - lpfloat(XE_GTAO_PI_HALF));
            lpfloat horizonCos0 = lowHorizonCos0;
            lpfloat horizonCos1 = lowHorizonCos1;

            // GI: its own horizon on its own, wider ring; shares the slice's direction, noise and trigonometry.
            lpfloat2 giOmega = omega * lpfloat(g_AL.GIRadiusScale);
            lpfloat giHorizonCos0 = lowHorizonCos0;
            lpfloat giHorizonCos1 = lowHorizonCos1;
            lpfloat giAngA = XeGTAO_FastACos(giHorizonCos0);
            lpfloat giAngB = -XeGTAO_FastACos(giHorizonCos1);
            lpfloat arcA = XeGTAO_GI_InnerArc(giAngA, n, cosNorm);
            lpfloat arcB = XeGTAO_GI_InnerArc(giAngB, n, cosNorm);
            lpfloat3 sliceRadiance = lpfloat3(0.0, 0.0, 0.0);
            lpfloat sliceWeight = lpfloat(0.0);

            [loop] for (lpfloat step = 0.0; step < stepsPerSlice; step++)
            {
                const lpfloat stepBaseNoise = lpfloat(slice + step * stepsPerSlice) * 0.6180339887498948482;
                lpfloat stepNoise = frac(noiseSample + stepBaseNoise);
                lpfloat s = (step + stepNoise) / (stepsPerSlice);
                s = lpfloat(pow(max(s, lpfloat(0.0)), lpfloat(sampleDistributionPower)));
                s += minS;
                lpfloat2 sampleOffset = s * omega;
                lpfloat sampleOffsetLength = length(sampleOffset);
                const lpfloat mipLevel = lpfloat(clamp(log2(sampleOffsetLength) - consts.DepthMIPSamplingOffset, 0.0,
                                                       float(XE_GTAO_DEPTH_MIP_LEVELS)));
                sampleOffset = round(sampleOffset) * lpfloat2(consts.ViewportPixelSize);

                float2 sampleScreenPos0 = normalizedScreenPos + sampleOffset;
                float SZ0 = g_srcWorkingDepth.SampleLevel(g_samplerPointClamp, sampleScreenPos0, mipLevel).x;
                float3 samplePos0 = XeGTAO_ComputeViewspacePosition(sampleScreenPos0, SZ0, consts);
                float2 sampleScreenPos1 = normalizedScreenPos - sampleOffset;
                float SZ1 = g_srcWorkingDepth.SampleLevel(g_samplerPointClamp, sampleScreenPos1, mipLevel).x;
                float3 samplePos1 = XeGTAO_ComputeViewspacePosition(sampleScreenPos1, SZ1, consts);
                float3 sampleDelta0 = (samplePos0 - float3(pixCenterPos));
                float3 sampleDelta1 = (samplePos1 - float3(pixCenterPos));
                lpfloat sampleDist0 = lpfloat(length(sampleDelta0));
                lpfloat sampleDist1 = lpfloat(length(sampleDelta1));
                lpfloat3 sampleHorizonVec0 = lpfloat3(sampleDelta0 / sampleDist0);
                lpfloat3 sampleHorizonVec1 = lpfloat3(sampleDelta1 / sampleDist1);
                lpfloat falloffBase0 = length(lpfloat3(sampleDelta0.x, sampleDelta0.y, sampleDelta0.z * (1.0 + thinOccluderCompensation)));
                lpfloat falloffBase1 = length(lpfloat3(sampleDelta1.x, sampleDelta1.y, sampleDelta1.z * (1.0 + thinOccluderCompensation)));
                lpfloat weight0 = saturate(falloffBase0 * falloffMul + falloffAdd) * AL_Thickness(sampleDelta0);
                lpfloat weight1 = saturate(falloffBase1 * falloffMul + falloffAdd) * AL_Thickness(sampleDelta1);
                lpfloat shc0 = lpfloat(dot(sampleHorizonVec0, viewVec));
                lpfloat shc1 = lpfloat(dot(sampleHorizonVec1, viewVec));
                shc0 = lerp(lowHorizonCos0, shc0, weight0);
                shc1 = lerp(lowHorizonCos1, shc1, weight1);
                horizonCos0 = max(horizonCos0, shc0);
                horizonCos1 = max(horizonCos1, shc1);

                [branch] if (giOn)
                {
                    lpfloat2 giOffset = s * giOmega;
                    const lpfloat giMip = lpfloat(clamp(log2(length(giOffset)) - consts.DepthMIPSamplingOffset, 0.0,
                                                        float(XE_GTAO_DEPTH_MIP_LEVELS)));
                    giOffset = round(giOffset) * lpfloat2(consts.ViewportPixelSize);
                    float2 giScreenPos0 = normalizedScreenPos + giOffset;
                    float2 giScreenPos1 = normalizedScreenPos - giOffset;
                    float3 giSamplePos0 = XeGTAO_ComputeViewspacePosition(
                        giScreenPos0, g_srcWorkingDepth.SampleLevel(g_samplerPointClamp, giScreenPos0, giMip).x, consts);
                    float3 giSamplePos1 = XeGTAO_ComputeViewspacePosition(
                        giScreenPos1, g_srcWorkingDepth.SampleLevel(g_samplerPointClamp, giScreenPos1, giMip).x, consts);
                    float3 giDelta0 = giSamplePos0 - float3(pixCenterPos);
                    float3 giDelta1 = giSamplePos1 - float3(pixCenterPos);
                    lpfloat giDist0 = lpfloat(length(giDelta0));
                    lpfloat giDist1 = lpfloat(length(giDelta1));
                    lpfloat giWeight0 = saturate(giDist0 * lpfloat(g_AL.GIFalloffMul) + lpfloat(g_AL.GIFalloffAdd)) *
                                        AL_Thickness(giDelta0);
                    lpfloat giWeight1 = saturate(giDist1 * lpfloat(g_AL.GIFalloffMul) + lpfloat(g_AL.GIFalloffAdd)) *
                                        AL_Thickness(giDelta1);
                    lpfloat giShc0 = lerp(lowHorizonCos0, lpfloat(dot(lpfloat3(giDelta0 / giDist0), viewVec)), giWeight0);
                    lpfloat giShc1 = lerp(lowHorizonCos1, lpfloat(dot(lpfloat3(giDelta1 / giDist1), viewVec)), giWeight1);
                    lpfloat giPrev0 = giHorizonCos0;
                    lpfloat giPrev1 = giHorizonCos1;
                    giHorizonCos0 = max(giHorizonCos0, giShc0);
                    giHorizonCos1 = max(giHorizonCos1, giShc1);
                    // A tap that raises the horizon is visible from the centre: the drop in the arc integral is the
                    // cosine-weighted solid angle it took, the weight its radiance deserves.
                    if (giHorizonCos0 > giPrev0)
                    {
                        lpfloat angNew = XeGTAO_FastACos(giHorizonCos0);
                        lpfloat wedge = max(lpfloat(0.0), arcA - XeGTAO_GI_InnerArc(angNew, n, cosNorm));
                        arcA = XeGTAO_GI_InnerArc(angNew, n, cosNorm);
                        giAngA = angNew;
                        wedge *= pow(max(giWeight0, lpfloat(0.0)), lpfloat(g_AL.GIRadianceFalloffPower));
                        wedge *= lpfloat(1.0) / (lpfloat(1.0) + lpfloat(g_AL.GIDistanceFalloff) * giDist0 * giDist0);
                        lpfloat4 smp = AL_SampleRadiance(giScreenPos0, giMip, lpfloat3(giDelta0 / giDist0));
                        wedge *= smp.a;
                        sliceRadiance += smp.rgb * wedge;
                        sliceWeight += wedge;
                    }
                    if (giHorizonCos1 > giPrev1)
                    {
                        lpfloat angNew = -XeGTAO_FastACos(giHorizonCos1);
                        lpfloat wedge = max(lpfloat(0.0), arcB - XeGTAO_GI_InnerArc(angNew, n, cosNorm));
                        arcB = XeGTAO_GI_InnerArc(angNew, n, cosNorm);
                        giAngB = angNew;
                        wedge *= pow(max(giWeight1, lpfloat(0.0)), lpfloat(g_AL.GIRadianceFalloffPower));
                        wedge *= lpfloat(1.0) / (lpfloat(1.0) + lpfloat(g_AL.GIDistanceFalloff) * giDist1 * giDist1);
                        lpfloat4 smp = AL_SampleRadiance(giScreenPos1, giMip, lpfloat3(giDelta1 / giDist1));
                        wedge *= smp.a;
                        sliceRadiance += smp.rgb * wedge;
                        sliceWeight += wedge;
                    }
                }
            }
            projectedNormalVecLength = lerp(projectedNormalVecLength, 1.0, 0.05);
            lpfloat h0 = -XeGTAO_FastACos(lpfloat(horizonCos1));
            lpfloat h1 = XeGTAO_FastACos(lpfloat(horizonCos0));
            lpfloat iarc0 = (lpfloat(cosNorm) + lpfloat(2.0) * lpfloat(h0) * sin(n) - cos(lpfloat(2.0) * lpfloat(h0) - n)) / lpfloat(4.0);
            lpfloat iarc1 = (lpfloat(cosNorm) + lpfloat(2.0) * lpfloat(h1) * sin(n) - cos(lpfloat(2.0) * lpfloat(h1) - n)) / lpfloat(4.0);
            visibility += lpfloat(projectedNormalVecLength) * lpfloat(iarc0 + iarc1);
            // Same foreshortening as the visibility: keeps sum(weight) <= open - visibility per slice.
            gatheredRadiance += sliceRadiance * lpfloat(projectedNormalVecLength);
            gatheredWeight += sliceWeight * lpfloat(projectedNormalVecLength);
        }
        visibility /= lpfloat(sliceCount);
        gatheredRadiance /= lpfloat(sliceCount);  // before FinalValuePower: radiance stays linear
        gatheredWeight /= lpfloat(sliceCount);
        visibility = pow(max(visibility, lpfloat(0.0)), lpfloat(consts.FinalValuePower));
        visibility = max(lpfloat(0.03), visibility);
    }
    g_outBounce[bouncePix] = float4(gatheredRadiance, gatheredWeight);
    if (writeAO)
        XeGTAO_OutputWorkingTerm(pixCoord, visibility, lpfloat3(0, 0, 1), g_outWorkingAOTerm);
}

[numthreads(XE_GTAO_NUMTHREADS_X, XE_GTAO_NUMTHREADS_Y, 1)]
void CSAfterlightMain(const uint2 pixCoord : SV_DispatchThreadID)
{
    if (all(pixCoord < uint2(g_GTAOConsts.ViewportSize)))
        AfterlightMain(pixCoord, true, pixCoord);
}

// [render] afterlight_scale > 1: the gather at a lower resolution (one thread per bounce texel, at the top-left
// full-resolution pixel of its block); the occlusion comes from XeGTAO's full-resolution main pass.
[numthreads(XE_GTAO_NUMTHREADS_X, XE_GTAO_NUMTHREADS_Y, 1)]
void CSAfterlightGather(const uint2 pixCoord : SV_DispatchThreadID)
{
    uint bw, bh;
    g_outBounce.GetDimensions(bw, bh);
    if (pixCoord.x >= bw || pixCoord.y >= bh)
        return;
    uint scale = max(1u, uint(g_GTAOConsts.ViewportSize.x) / bw);
    AfterlightMain(min(pixCoord * scale, uint2(g_GTAOConsts.ViewportSize) - 1), false, pixCoord);
}

// ---- Bounce blur (SSLD_BlurBounceA / Pong): 9 taps over +-32 px, depth- and normal^8-guided, rejected across
// silhouettes so a bright wall cannot bleed onto the object in front of it.
// Works at the bounce's resolution: the reach (32 full-resolution pixels) and the guide (full-resolution normals and
// depth at the texel's pixel) follow the scale.
float4 AL_BlurBounce(int2 pixCoord, int2 direction)
{
    uint bw, bh;
    g_bounceIn.GetDimensions(bw, bh);
    const int scale = max(1, g_GTAOConsts.ViewportSize.x / int(bw));
    const int2 limit = int2(bw, bh) - 1;
    const int2 fullLimit = g_GTAOConsts.ViewportSize - 1;
    float4 centre = g_normals.Load(int3(min(clamp(pixCoord, int2(0, 0), limit) * scale, fullLimit), 0));
    float depthScale = 1.0 / max(centre.w * 0.03, 1e-6);
    const float spatialK = -0.5 / (16.0 * 16.0);
    float4 sum = 0.0;
    float sumWeight = 0.0;
    [unroll] for (int i = 0; i < 9; i++)
    {
        int offset = -32 + i * 8;  // full-resolution pixels
        int2 tap = clamp(pixCoord + direction * (offset / scale), int2(0, 0), limit);
        float4 guide = g_normals.Load(int3(min(tap * scale, fullLimit), 0));
        float depthTerm = abs(guide.w - centre.w) * depthScale;
        if (depthTerm > 1.0)
            continue;
        float nd = max(0.0, dot(centre.xyz, guide.xyz));
        nd *= nd;
        nd *= nd;
        nd *= nd;
        float weight = exp(spatialK * float(offset * offset)) * (1.0 - depthTerm * depthTerm) * nd;
        sum += g_bounceIn.Load(int3(tap, 0)) * weight;
        sumWeight += weight;
    }
    return sum / max(sumWeight, 1e-6);
}

[numthreads(8, 8, 1)]
void CSAfterlightBlurH(const uint2 pixCoord : SV_DispatchThreadID)
{
    uint bw, bh;
    g_outBounce.GetDimensions(bw, bh);
    if (pixCoord.x < bw && pixCoord.y < bh)
        g_outBounce[pixCoord] = AL_BlurBounce(int2(pixCoord), int2(1, 0));
}

[numthreads(8, 8, 1)]
void CSAfterlightBlurV(const uint2 pixCoord : SV_DispatchThreadID)
{
    uint bw, bh;
    g_outBounce.GetDimensions(bw, bh);
    if (pixCoord.x < bw && pixCoord.y < bh)
        g_outBounce[pixCoord] = AL_BlurBounce(int2(pixCoord), int2(0, 1));
}

// ---------------------------------------------------------------------------------------------------------------
// Smooth normals for XeGTAO ([render] ao_smooth_normals; vk_device.cpp Ambient): the in-place normals are flat per
// triangle, so a rounded low-poly mound took its occlusion triangle by triangle (the player, 2026-10-12: "lets pass
// those smooth normals to GTAO too"). CSAfterlightNormals writes the flat normals and view z (binding 20); this pass
// averages them over 8 spiral points within 1.5% of the view height, each weighted by staying near the centre's
// plane and by facing within ~45 degrees of it (creases stay hard), into binding 22; CSGTAOHighSmooth is the main
// pass reading them (binding 23) instead of generating its own.
[numthreads(8, 8, 1)]
void CSSmoothNormals(const uint2 pixCoord : SV_DispatchThreadID)
{
    const GTAOConstants consts = g_GTAOConsts;
    if (any(pixCoord >= uint2(consts.ViewportSize)))
        return;
    int2 p = int2(pixCoord);
    int2 lim = consts.ViewportSize - 1;
    float4 c0 = g_normals.Load(int3(p, 0));
    float3 n0 = c0.xyz;
    float3 P = XeGTAO_ComputeViewspacePosition((float2(p) + 0.5) * consts.ViewportPixelSize, c0.w, consts);
    float3 sum = n0;
    float radius = float(consts.ViewportSize.y) * 0.015;
    float a = frac(sin(dot(float2(p), float2(12.9898, 78.233))) * 43758.5453) * 6.2831853;
    for (int i = 0; i < 8; ++i)
    {
        float rr = sqrt((float(i) + 0.5) / 8.0) * radius;
        float th = float(i) * 2.39996323 + a;
        int2 q = clamp(p + int2(round(float2(cos(th), sin(th)) * rr)), int2(0, 0), lim);
        float4 cq = g_normals.Load(int3(q, 0));
        float3 Pq = XeGTAO_ComputeViewspacePosition((float2(q) + 0.5) * consts.ViewportPixelSize, cq.w, consts);
        float gap = abs(dot(n0, Pq - P)) / (0.02 * abs(c0.w) + 0.05);
        sum += cq.xyz * (1.0 - smoothstep(1.0, 3.0, gap)) * smoothstep(0.6, 0.85, dot(cq.xyz, n0));
    }
    g_outBounce[p] = float4(normalize(sum), c0.w);
}

[numthreads(XE_GTAO_NUMTHREADS_X, XE_GTAO_NUMTHREADS_Y, 1)]
void CSGTAOHighSmooth(const uint2 pixCoord : SV_DispatchThreadID)
{
    lpfloat3 n = (lpfloat3)g_bounceIn.Load(int3(pixCoord, 0)).xyz;
    XeGTAO_MainPass(pixCoord, 3, 3, SpatioTemporalNoise(pixCoord, g_GTAOConsts.NoiseIndex), n,
                    g_GTAOConsts, g_srcWorkingDepth, g_samplerPointClamp, g_outWorkingAOTerm, g_outWorkingEdges);
}
