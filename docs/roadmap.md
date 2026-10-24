# Remaster roadmap (human's goals, 2026-10-03)

> Historical: the roadmap of the hook-DLL route (now in archive/). The current roadmap is in the README; the
> engine's working order is in docs/remaster-engine.md.

North star: a fully decompiled game, then a remaster built on open-source technology. Order of work:

1. **Renderer and asset loading recovered** (current goal: `docs/goals/renderer-and-assets.md`): wireframe,
   texture/model replacement, lossless `.big` rebuild.
2. **Remaster basics** (requested 2026-10-04: `docs/goals/remaster-basics.md`): aspect ratio and UI anchoring,
   menu extension for remaster options, persistent decals and bodies, FOV, tessellation (Vulkan), QoL. Runs
   alongside the backend work; tessellation needs the Vulkan backend.
2b. **Modern rendering backend** behind a clean renderer interface (the recovered D3D9 layer is the reference).
3. **Image quality:** DLAA (and other upscalers/anti-aliasing), HDR output, high frame rates (fix the
   frame-rate-dependent behaviour first: `docs/original-bugs.md`).
4. **PBR materials.** Requested by the human for the future. Implications for work happening now:
   - Recover the material system completely and document it: per-material flags (`+0x48` bits seen so far:
     texture factor 17-18, pixel-shader constants 23-24), texture stages, the named pixel shaders
     (mixtexplusenv, alphashadow, bumpspec, mixbump, distort, depthmod) and the vertex shaders in `shaders.dat`.
     A PBR pipeline needs to map each original material to PBR parameters (albedo, normal, roughness, metalness)
     or to accept replacement material definitions.
   - The asset replacement path (loose files overriding `.big` content) should allow *extra* maps per texture
     (normal, roughness/metal) next to the replaced albedo, not just a one-for-one swap.
   - Keep lighting state recoverable: light setup (`SetLight`, fog, ambient) and the shader constant layout
     (`c0` texture factor, `c1`/`c2` from Renderer+0x950/+0x980) are what a PBR shader would consume.
5. **Ray tracing.** Needs the scene described independently of D3D9 draw calls (geometry, materials, lights);
   the same material work as PBR is a prerequisite.
6. **Physics:** replace Havok with an open-source engine, after its interface is drawn (identify and fence the
   Havok code range first).
