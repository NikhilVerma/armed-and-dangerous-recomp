# Lighting additions: ambient occlusion, modern fog, shadow fixes (Vulkan backend)

Status 2026-10-04. Remaster switches in `[render]`, remaster default on; `[remaster] original_defaults=1` turns them
off (tests).

## Where the game's fog comes from

- The game fogs in its vertex shaders: `Renderer_SetFog` (0x00504010, archive/src/renderer/auto_00502300.cpp) takes the
  shader path and sets **c1 = {start, end, 1 / (end - start), 0.5}** (camera distances); D3D9 sees
  `D3DRS_FOGTABLEMODE` = none and FOGSTART/FOGEND 0 / 1. Measured in lev_1_m1: start 100, end 1500 (and.log `fog:` line from
  ModernFogUniforms, vk_device.cpp).
- The fog is blended in one place for every draw: D3D9_FINAL (vk_fixed_function.cpp FinalBody), shared by
  fixed-function and translated shaders.

## Modern fog (`fog_modern`, archive/src/rhi/vulkan/vk_fog.h)

Exponential height fog in D3D9_FINAL, in place of the linear blend: density a exp(-b dh) (Z up), a set so half the
light gets through half way through the game's fog range (as its linear fog), b = `fog_height` (0.002 per unit),
in-scattering = the game's fog colour plus a sun glow (`fog_sun`). Only scene draws: the HUD and menus (after
SceneEnd) keep the game's fog. Camera and sun come from rhi::Hints, filled at Renderer_BeginScene (0x005042d0,
wrapped by archive/hook/enhance/shadows.cpp) so they are this frame's.

## Ambient occlusion (`ao`, `ao_strength`, `ao_radius`; archive/src/rhi/vulkan/vk_gtao.h)

- XeGTAO (Intel, MIT) adopted unmodified: archive/third_party/XeGTAO (XeGTAO.h, XeGTAO.hlsli). Entry points in
  archive/src/rhi/vulkan/shaders/gtao.hlsl (from the sample's vaGTAO.hlsl), compiled at run time with glslang's HLSL front
  end (ENABLE_HLSL on), sources embedded by archive/hook/CMakeLists.txt. Two textual fixes at assembly time for glslang:
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
- AO darkens direct light too when ray-traced shadows are off (the game has no ambient-only buffer); with the RT
  helper's visibility it applies to the ambient share only (below).

## Ray-traced shadows (2026-10-05, in progress, not yet verified in the game)

- **Hardware RT is not available in the game process:** the game is 32-bit and NVIDIA's 32-bit Vulkan driver
  exposes no ray-tracing extensions (measured on the RTX 4090, driver 617.14: 270 device extensions, none of
  VK_KHR_acceleration_structure / ray_query). The human chose a 64-bit helper process (decision.md 2026-10-05).
- **Design** (archive/src/rhi/vulkan/rt_ipc.h): archive/rthelper/ builds rt_helper.exe (archive/tools/build_helper.ps1, x64). The game
  (vk_rt_client.cpp) starts it, shares buffers (VK_KHR_external_memory_win32) and two timeline semaphores
  (VK_KHR_external_semaphore_win32); per frame it replays the casters' vertex shaders with c22..c25 =
  camera -> world into the shared vertex buffer, writes triangle indices (strips/fans converted) and the
  camera-space depth, submits the first half of the frame (VulkanDevice::SplitSubmit, signalling the helper),
  and continues in the slot's second command buffer; the apply pass reads the helper's visibility, and the
  final submit waits for the helper only if it acknowledged the frame (no GPU hang possible).
- **Helper** (archive/rthelper/main.cpp): device on the same GPU (LUID), imports, copies inputs into AS-build buffers,
  builds BLAS + TLAS every frame (PREFER_FAST_BUILD), then archive/rthelper/shadow_denoiser.cpp: its own G-buffer pass
  (depth, normals, velocity from the previous camera) and a one-ray-per-pixel trace over the sun's disc into
  8x4 tile bit masks, then AMD FidelityFX Shadow Denoiser v1.1.4 unmodified (archive/third_party/FidelityFX-
  ShadowDenoiser: prepare, tile classification with temporal accumulation, three edge-aware filter passes).
  The penumbra comes from sampling the sun's disc (FrameParams.sun.w = tan of its angular radius) accumulated
  over frames: it widens with the occluder distance, with no blur cap.
- **FFX_HALF is required:** the SDK's filter has no 32-bit path (ffx_denoiser_shadows_filter.h stubs its
  group-shared loads to 0 without it; every filtered tile came out black, quantised to 8x8 tiles). Compiled
  with FFX_HALF 1 as the SDK builds it; the helper enables shaderFloat16, 16-bit storage, shaderInt16.
  Classification and filters use 8 x 8 groups: dispatch (W/8, H/8).
- **Tests:** build/hook-release/rt_helper_test.exe (no game, no display): floor + box, PASS when the umbra is
  dark and open floor / sunny side lit (measured: 0.07 / 1.00 / 1.00). RT_DEBUG=1 raw one-ray result, 2 no
  history; RT_PERF=1 1920x1080 with 324k triangles prints the helper's GPU time (timestamps, Control.gpuMicros):
  0.94 ms per frame for the whole helper frame (build, trace, denoise) with fast build, 1.16 ms with fast trace.
  vk_shader_test compiles every lighting shader offline (XeGTAO's four entries, AO apply, RT shadows, depth).
- **In-process path** (VulkanDevice::RtShadows) kept for 64-bit-capable drivers (rayQuery).
- **AO with RT visibility:** shadows now run before AO; when the helper's visibility exists for the frame, the
  AO pass scales by ((1 - s') ao + s' v) / ((1 - s') + s' v), s' = shadow strength * smoothstep(0.02, 0.2, n.l):
  with the shadow pass's (1 - s') + s' v the product is (1 - s') ao + s' v, i.e. occlusion of the ambient share
  only (full AO in shade and on surfaces facing away from the sun). Without RT: plain AO as before.
- **Status:** headless test green; not yet run in the game (the display slept: Screen = WinDisc, every game run
  crashes in D3D9 device creation).
- **Next:** docs/journal/2026-10-05.md.
