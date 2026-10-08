# Remaster: temporal anti-aliasing and upscaling (FSR / DLSS / XeSS)

The player asked on 2026-10-08 for the best temporal solutions, all supported, using libraries rather than
hand-written versions: DLAA/DLSS, FSR, XeSS and TAA.

## Constraint: the game is 32-bit, the libraries are 64-bit

- FidelityFX SDK v1.1.4 (MIT): the last release with a Vulkan backend for FSR 3.1. SDK 2.x is DirectX 12 only.
  Its prebuilt, signed `amd_fidelityfx_vk.dll` is x64 (machine 0x8664, checked 2026-10-08).
- NVIDIA DLSS SDK (github.com/NVIDIA/DLSS, NVIDIA RTX SDK licence): `nvngx_dlss.dll` is x64. DLAA is its 1:1 mode.
- Intel XeSS 2.x SDK (Intel Simplified Software License, binaries only): x64, with Vulkan for super resolution.

So the AA runs in the existing 64-bit helper process (`rthelper/`, `rt_helper.exe`), which already shares GPU
memory with the game for ray-traced shadows (`src/rhi/vulkan/rt_ipc.h`: Win32 external-memory buffers plus
timeline semaphores).

## Data flow per frame

1. Game, scene draws only (not the HUD or UI): the subpixel jitter (Halton 2,3, 8 or 16 phases) is added to the
   existing D3D9 half-pixel offset (`ff.halfPixel`; vertex shaders do `clip.xy += halfPixel.xy * clip.w`).
   `vCurClip` / `vPrevClip` are taken before that offset, so the motion vectors carry no jitter, as the libraries
   expect.
2. Game, SceneEnd, after shadows, AO, fog and snow but before the UI pass (`g_uiPassBegin`): copy into shared
   buffers
   - colour: the back buffer, RGBA16F;
   - depth: R32F 0..1, D3D convention, not inverted;
   - motion: the motion target, RGBA16F, of which the helper reads `.rg` = prevUv - curUv in 0..1, y down;
   then signal `aaToHelper` = N.
3. Helper: buffers to images; library dispatch with jitter, motionVectorScale = (width, height), near/far/fov from
   the scene projection and reset on camera cuts; output image to the shared output buffer; signal
   `aaFromHelper` = N.
4. Game: the GPU waits for `aaFromHelper` = N (only for frames the helper accepted, as with RT), then copies the
   output buffer into the back buffer. The UI then draws over it.

At 4K the copies are about 280 MB of GPU-local traffic per frame (roughly 0.3 ms on an RTX 4090). Sharing images
directly would avoid them; buffers are the proven path, so they come first.

## Protocol (rt_ipc.h v4)

- New shared buffers: `kAaColor`, `kAaDepth`, `kAaMotion`, `kAaOutput`.
- An AA ring of params (`AaParams`):
  - frame, width, height, jitter x/y (pixels), the scene projection and near/far/fov, frame time;
  - reset, mode (1 FSR, 2 DLSS, 3 XeSS), sharpness.
- A semaphore pair (`semAaToHelper`, `semAaFromHelper`) and counters (`aaRequested`, `aaAccepted`).
- The helper loop serves both RT and AA requests. Either may be off.

## Settings ([render])

- `aa = off|fsr|dlss|xess|auto`. The remaster default is `auto`: DLSS on NVIDIA RTX when the SDK is present,
  otherwise FSR.
- `aa_sharpness`, from 0 to 1.
- Later: `upscale = native|quality|balanced|performance`. Native = DLAA / FSR native AA. Upscaling needs the game
  to render at a lower internal resolution (back buffer smaller than the swapchain); not in the first version.

## Status

- Design written on 2026-10-08. Implementation starts with FSR, which needs no licence acceptance. DLSS and XeSS
  wait for the player to accept their SDK licences.
