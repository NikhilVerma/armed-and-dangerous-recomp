#pragma once
// Modern fog for the remaster ([render] fog_modern): exponential height fog in place of the game's D3D9 linear fog,
// applied where the game's fog is applied (D3D9_FINAL, every fogged draw, opaque or translucent) and recomputed by
// passes that change the scene after it was fogged (ambient occlusion), so both always agree.
//
// Model: density falls off with height, rho(h) = a * exp(-b (h - h_cam)) (world Z is up). The optical depth from
// the camera to a point at distance s (beyond the fog start) and height difference dh is
//   tau = a * s * (1 - exp(-b dh)) / (b dh),
// transmittance T = exp(-tau). a is chosen so that, level with the camera, T is 1/2 half way through the game's
// fog range (as with its linear fog) and about 6% at its end: valleys get thicker and peaks thinner. The game's
// fog runs from 100 to 1500 world units in lev_1_m1 (vertex shader constant c1, Renderer_SetFog). In-scattering: the game's fog colour
// plus a forward-scattering glow towards the sun.

#include <string>

#include <d3d9.h>

namespace rhi
{
struct SceneHints;
}

namespace rhi::vulkan
{
// The uniforms the fog statements read (std140, 8 x vec4), filled by ModernFogUniforms.
struct alignas(16) ModernFogParams
{
    float on[4];          // x: 1 = modern fog, y: a (density level with the camera), z: b (height falloff), w: start
    float proj[4];        // scene projection _11 _22 _33 _43
    float proj2[4];       // _34, sun glow strength, 0, 0
    float viewport[4];    // scene viewport x y w h (pixels)
    float toWorld[3][4];  // camera -> world rows (rotation and translation)
    float sun[4];         // world direction the sunlight travels
};

// GLSL member list of the block (for a uniform block declaration).
const char* ModernFogMembers();

// Statements computing `float <f>` (transmittance, 1 = clear) and `vec3 <col>` (in-scattered colour) for the pixel
// at gl_FragCoord.xy with depth `depth`, reading the block members through `u` (e.g. "ff.mf").
std::string ModernFogGlsl(const std::string& u, const std::string& depth, const std::string& f,
                          const std::string& col, const std::string& fogColor);

// Fills `p` from the frame's hints, the scene projection and viewport, and the game's fog start and end (0..1 depth
// for table fog, camera distances for vertex fog).
void ModernFogUniforms(ModernFogParams& p, const rhi::SceneHints& h, const D3DMATRIX& proj,
                       const D3DVIEWPORT9& viewport, float start, float end, bool depthUnits);
}  // namespace rhi::vulkan
