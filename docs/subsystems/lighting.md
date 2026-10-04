# Lighting additions: ambient occlusion, modern fog, shadow fixes (Vulkan backend)

Status 2026-10-04. Remaster switches in `[render]`, remaster default on; `[remaster] original_defaults=1` turns them
off (tests).

## Where the game's fog comes from

- The game fogs in its vertex shaders: `Renderer_SetFog` (0x00504010, src/renderer/auto_00502300.cpp) takes the
  shader path and sets **c1 = {start, end, 1 / (end - start), 0.5}** (camera distances); D3D9 sees
  `D3DRS_FOGTABLEMODE` = none and FOGSTART/FOGEND 0 / 1. Measured in lev_1_m1: start 100, end 1500 (and.log `fog:` line from
  ModernFogUniforms, vk_device.cpp).
- The fog is blended in one place for every draw: D3D9_FINAL (vk_fixed_function.cpp FinalBody), shared by
  fixed-function and translated shaders.

## Modern fog (`fog_modern`, src/rhi/vulkan/vk_fog.h)

Exponential height fog in D3D9_FINAL, in place of the linear blend: density a exp(-b dh) (Z up), a set so half the
light gets through half way through the game's fog range (as its linear fog), b = `fog_height` (0.002 per unit),
in-scattering = the game's fog colour plus a sun glow (`fog_sun`). Only scene draws: the HUD and menus (after
SceneEnd) keep the game's fog. Camera and sun come from rhi::Hints, filled at Renderer_BeginScene (0x005042d0,
wrapped by hook/enhance/shadows.cpp) so they are this frame's.

## Ambient occlusion (`ao`, `ao_strength`, `ao_radius`; src/rhi/vulkan/vk_gtao.h)

- XeGTAO (Intel, MIT) adopted unmodified: third_party/XeGTAO (XeGTAO.h, XeGTAO.hlsli). Entry points in
  src/rhi/vulkan/shaders/gtao.hlsl (from the sample's vaGTAO.hlsl), compiled at run time with glslang's HLSL front
  end (ENABLE_HLSL on), sources embedded by hook/CMakeLists.txt. Two textual fixes at assembly time for glslang:
  the sample framework's `VA_SATURATE` / `VA_PI`, and literal swizzles (`2.xx` -> `float2(2, 2)`).
- Passes: depth prefilter (R32F, 5 MIPs), main pass "high" (3 slices x 3 steps, normals from depth), denoise
  "medium" (2 passes). Constants from XeGTAO's own GTAOUpdateConstants with the game's D3D projection.
- Apply: the colour is multiplied by the occlusion **under the fog**: with F = C T + Fog (1 - T), the result is
  F ao + Fog (1 - T)(1 - ao), T from the same fog formula the draws used (modern or the game's linear fog on
  camera distance). Occlusion never shows through fog.
- Ordering: AO and shadows are applied at the first translucent scene draw (blended, no depth writes) after the
  opaque draws (VulkanDevice::BeforeDraw), so translucent layers stay on top (measured: the intro's white fade
  was darkened from above when they ran at SceneEnd).
- Defaults: radius 2.5, strength 1.0 (work/shots/ao4_feet.png: contact shadows and self-occlusion).

## Shadow fixes

- Casters the game culled from the view: a cache of static opaque draws seen this level (VulkanDevice::Shadows,
  CachedCaster) keeps casting within `shadowCasterRadius` (100 units) of the camera; draws whose world transform
  changes between sightings are never cached; cached draws hold buffer references; dropped on level load.
- Reach towards the sun: the light box's depth now spans 40 x range up-sun (was 4 x): distant casters up-sun were
  clipped and their shadows popped as the box moved with the camera.

## Open

- Shadow apply still darkens through fog (the same fog removal as AO is not applied there yet).
- XeGTAO requires the scene viewport to cover the depth buffer from its origin; otherwise AO is skipped (logged).
- AO darkens direct light too (the game has no ambient-only buffer).
