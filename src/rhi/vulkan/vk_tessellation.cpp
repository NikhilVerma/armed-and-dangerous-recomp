// rhi::vulkan tessellation: the generic control and evaluation shaders placed between a translated vertex shader
// (compiled with D3D9_TESS) and the pixel shader. Conventions: vk_abi.h "Tessellation" and the varying locations
// (0..7 texture coordinates, 8/9 colours, 10 fog, 11/12 current/previous clip positions, 13/14 camera-space position
// and normal).
//
// Phong tessellation (Boubekeur and Alexa 2008): the point interpolated linearly in the triangle is projected onto
// the tangent plane of each corner (its camera-space position and normal), and those three projections are interpolated
// again; the shape factor blends between flat (0) and that curved surface (1). The displacement is applied in camera
// space and taken to clip space with the projection (c22..c25), and to the previous frame's clip space with last
// frame's projection (an approximation: the camera's own motion is already in the interpolated previous position).
#include <format>

#include "vk_abi.h"
#include "vk_shaders.h"

namespace rhi::vulkan
{
namespace
{
// Varyings carried through both stages: GLSL type, base name, location.
struct Varying
{
    const char* type;
    const char* name;
    int location;
};
constexpr Varying kVaryings[] = {
    {"vec4", "T0", 0},     {"vec4", "T1", 1},     {"vec4", "T2", 2},     {"vec4", "T3", 3},
    {"vec4", "T4", 4},     {"vec4", "T5", 5},     {"vec4", "T6", 6},     {"vec4", "T7", 7},
    {"vec4", "D0", 8},     {"vec4", "D1", 9},     {"float", "Fog", 10},  {"vec4", "CurClip", 11},
    {"vec4", "PrevClip", 12}, {"vec3", "World", 13}, {"vec3", "Normal", 14},
};
}  // namespace

std::string TessControlGlsl()
{
    std::string g = "#version 450\n// Tessellation control shader (vk_tessellation.cpp).\nlayout(vertices = 3) out;\n";
    g += VertexUniformBlocks();
    for (const Varying& v : kVaryings)
        g += std::format("layout(location = {0}) in {1} i{2}[];\nlayout(location = {0}) out {1} o{2}[];\n", v.location,
                         v.type, v.name);
    g += R"(
float EdgeLevel(vec2 a, vec2 b)
{
    return clamp(length(a - b) / max(ff.tessParams.w, 1.0), 1.0, max(ff.tessParams.z, 1.0));
}

void main()
{
    gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;
)";
    for (const Varying& v : kVaryings)
        g += std::format("    o{0}[gl_InvocationID] = i{0}[gl_InvocationID];\n", v.name);
    g += R"(
    if (gl_InvocationID != 0)
        return;
    // Screen-space corners (pixels) from the clip positions; patches crossing the camera plane or without
    // normals stay as they are.
    vec2 halfSize = ff.viewportSize.xy * 0.5;
    bool keep = false;
    vec2 s[3];
    for (int k = 0; k < 3; ++k)
    {
        vec4 c = iCurClip[k];
        if (c.w <= 1e-4 || dot(iNormal[k], iNormal[k]) == 0.0)
            keep = true;
        s[k] = c.xy / max(c.w, 1e-4) * halfSize;
    }
    float e0 = keep ? 1.0 : EdgeLevel(s[1], s[2]);
    float e1 = keep ? 1.0 : EdgeLevel(s[2], s[0]);
    float e2 = keep ? 1.0 : EdgeLevel(s[0], s[1]);
    gl_TessLevelOuter[0] = e0;
    gl_TessLevelOuter[1] = e1;
    gl_TessLevelOuter[2] = e2;
    gl_TessLevelInner[0] = max(e0, max(e1, e2));
}
)";
    return g;
}

std::string TessEvalGlsl(bool ccw)
{
    std::string g = "#version 450\n// Tessellation evaluation shader (vk_tessellation.cpp).\n";
    g += std::format("layout(triangles, fractional_odd_spacing, {}) in;\n", ccw ? "ccw" : "cw");
    g += VertexUniformBlocks();
    for (const Varying& v : kVaryings)
    {
        g += std::format("layout(location = {}) in {} i{}[];\n", v.location, v.type, v.name);
        if (v.location <= 12)
            g += std::format("layout(location = {}) out {} o{};\n", v.location, v.type, v.name);
    }
    g += R"(
#define LERP3(a) (b.x * a[0] + b.y * a[1] + b.z * a[2])

vec3 ToPlane(vec3 p, int k)
{
    return p - dot(p - iWorld[k], iNormal[k]) * iNormal[k];
}

void main()
{
    vec3 b = gl_TessCoord;
)";
    for (const Varying& v : kVaryings)
        if (v.location <= 12)
            g += std::format("    o{0} = LERP3(i{0});\n", v.name);
    g += R"(
    vec3 p = LERP3(iWorld);
    vec3 q = b.x * ToPlane(p, 0) + b.y * ToPlane(p, 1) + b.z * ToPlane(p, 2);
    bool curved = dot(iNormal[0], iNormal[0]) > 0.0 && dot(iNormal[1], iNormal[1]) > 0.0 &&
                  dot(iNormal[2], iNormal[2]) > 0.0;
    vec3 d = curved ? (q - p) * ff.tessParams.y : vec3(0.0);
    mat4 cam = transpose(mat4(vsc.c[22], vsc.c[23], vsc.c[24], vsc.c[25]));
    mat4 camPrev = transpose(mat4(pf.vsConstants[22], pf.vsConstants[23], pf.vsConstants[24], pf.vsConstants[25]));
    oCurClip += cam * vec4(d, 0.0);
    oPrevClip += camPrev * vec4(d, 0.0);
    gl_Position = oCurClip;
    gl_Position.xy += ff.halfPixel.xy * gl_Position.w;  // D3D9 half-pixel offset (vk_abi.h)
}
)";
    return g;
}
}  // namespace rhi::vulkan
