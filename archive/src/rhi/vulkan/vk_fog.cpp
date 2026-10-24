// Modern fog (vk_fog.h).
#include "vk_fog.h"


namespace rhi::vulkan
{
const char* ModernFogMembers()
{
    return "    vec4 on;\n    vec4 proj;\n    vec4 proj2;\n    vec4 viewport;\n    vec4 toWorld[3];\n    vec4 sun;\n";
}

std::string ModernFogGlsl(const std::string& u, const std::string& depth, const std::string& f,
                          const std::string& col, const std::string& fogColor)
{
    // Camera-space position from the pixel and its depth (depth = (z _33 + _43) / (z _34), D3D9 projection), then
    // its offset from the camera in world space.
    return "{ vec2 mf_q = (gl_FragCoord.xy - " + u + ".viewport.xy) / " + u + ".viewport.zw * 2.0 - 1.0; "
           "float mf_den = (" + depth + ") * " + u + ".proj2.x - " + u + ".proj.z; "
           "float mf_z = " + u + ".proj.w / (abs(mf_den) < 1e-7 ? 1e-7 : mf_den); "
           "vec3 mf_v = vec3(mf_q.x * mf_z * " + u + ".proj2.x / " + u + ".proj.x, -mf_q.y * mf_z * " + u +
           ".proj2.x / " + u + ".proj.y, mf_z); "
           "vec3 mf_w = vec3(dot(" + u + ".toWorld[0].xyz, mf_v), dot(" + u + ".toWorld[1].xyz, mf_v), dot(" + u +
           ".toWorld[2].xyz, mf_v)); "
           "float mf_s = max(length(mf_v) - " + u + ".on.w, 0.0); "
           "float mf_k = " + u + ".on.z * mf_w.z; "
           "float mf_h = abs(mf_k) > 1e-4 ? (1.0 - exp(-mf_k)) / mf_k : 1.0; " +
           f + " = exp(-" + u + ".on.y * mf_s * mf_h); "
           // In-scattering towards the sun: the Henyey-Greenstein phase function (g = 0.65, haze), relative to
           // isotropic scattering, (1 - g^2) / (1 + g^2 - 2 g cos)^1.5; its excess over 1 brightens the fog
           // around the sun.
           "float mf_c = dot(normalize(mf_w + vec3(1e-6)), -" + u + ".sun.xyz); "
           "float mf_hg = 0.5775 / pow(1.4225 - 1.3 * mf_c, 1.5); "
           "float mf_g = max(mf_hg - 1.0, 0.0) * 0.08 * " + u + ".proj2.y; " +
           col + " = " + fogColor + " + vec3(1.0, 0.95, 0.85) * mf_g; }";
}

}  // namespace rhi::vulkan
