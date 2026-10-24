# Design: draw submission off the game thread (proposal, 2026-10-12)

Status: proposal, waiting for the player's go-ahead. Nothing here is built.

## Why

The 4K target is above 100 fps, uncapped, on an RTX 4090 with the driver's 119 fps cap. A 4K walk in the playground
averages about 99 fps, with dips to 75-80 in heavy stretches (docs/engine-limits.md, frame budget).

The GPU needs about 7 ms per frame. The CPU is the limit. In the slow stretches the game thread spends 9.7-10.9 ms in
render submission:

- the game's own scene traversal (recovered code: ScenePass_Render, MeshGroup_Draw, ItemList);
- the D3D9 facade;
- the Vulkan layer preparing about 1,100 draws, about 3 ms (PrepareDraw: pipeline lookup, uniforms, motion snapshot,
  push descriptors, binds);
- the remaster passes' CPU side: the shadow pass about 1.9 ms, AA/blur/snow about 0.4 ms.

All of it runs on one thread, the one the game calls D3D9 from.

Cheap cuts already made: redundant state skipped, scene records in place, per-shader constant counts, persistent
motion snapshots, a static shadow cache, the facade's lookup fast path, and audio rate limiting.

## Shape of the change

Split VulkanDevice into a front end and a back end. The front end runs on the game thread; the back end on one or
more worker threads.

1. **Front end (game thread).** It tracks D3D9 state as today: render states, textures, streams, constants,
   transforms. At each draw it appends a compact draw packet to a per-frame list:
   - the state that differs from the previous packet;
   - a constants slice copied into a frame arena;
   - the draw parameters.

   It records no Vulkan commands, takes no pipeline lookups and pushes no descriptors.
2. **Back end (workers).** At SceneEnd, or when a render pass changes, the packet list of each render pass instance
   is cut into chunks of about 200 draws. Each chunk is translated into its own secondary command buffer
   (VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT with VkCommandBufferInheritanceRenderingInfo for dynamic
   rendering). The translation does what PrepareDraw does now:
   - pipeline lookup;
   - uniform pushes into a per-worker uniform ring;
   - descriptors;
   - binds.

   The primary buffer then executes the secondaries in order.
3. **Shared caches.** These become thread-safe:
   - pipeline cache: lock-free read, locked insert;
   - sampler cache;
   - the motion snapshot map: sharded by draw id.

   The uniform ring splits into one ring per worker.
4. **Order-sensitive pieces stay on the game thread or at frame end.** These are:
   - Lock / Unlock of dynamic buffers (versioning happens at Lock time, so packets carry the version they saw);
   - texture uploads;
   - StretchRect and render target switches;
   - occlusion and capture.

   Packets carry resolved buffer versions and image views, so workers never read mutable D3D9 state.
5. **Remaster passes.**
   - Shadows replays casters from the same packets; its job list can be split across workers the same way.
   - AO, AA, blur and snow stay on the main thread: they are a few dispatches each.

## Risks

- Correctness. Every piece of state PrepareDraw reads must be captured in the packet. Missing one gives wrong
  rendering that only shows in some scenes.
  - Mitigation: a debug switch that translates packets on the game thread in order (same code path, one thread) and
    compares frame hashes with the threaded path (archive/tools/compare_backends.py style), plus the playground scenes.
- The D3D9 trace and the test harness hash frames. Threaded output must be bit-identical to single-threaded output;
  only command order inside a render pass is preserved, so it is.
- Driver cost of secondary command buffers (NVIDIA: small at 200 draws per chunk).
- Effort: the Vulkan layer's draw path is about 2,000 lines. Estimate 2-4 days, plus a verification pass over all
  levels.

## Expected gain

The Vulkan layer's ~3 ms and most of the shadow pass's ~1.9 ms move off the game thread. In the slow stretches the
game thread would drop from ~10.5 ms to ~6-7 ms, under the 8.4 ms of the 119 fps cap.

The game's own scene traversal (~3 ms) stays single-threaded. It is recovered original code and moving it is out of
scope.

## Smaller alternatives, if not this

- A frame of latency: translate frame N on a worker while the game builds frame N+1. This is one worker and simpler
  ordering, but it adds a frame of input latency.
- Keep the D3D9 facade but batch identical draws (same mesh, different constants) into instanced draws. This depends
  on the game's draw patterns (trees, rocks) and is a smaller gain.
