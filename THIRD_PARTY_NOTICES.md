# Third-party notices

This project's own code is dual-licensed MIT OR Apache-2.0 (LICENSE-MIT, LICENSE-APACHE). The parts below come from
others and keep their own licences. Rust crates the engine depends on (Bevy, Jolt Physics through oxijolt, bevy_egui,
bevy_enhanced_input, serde, rodio, zune-jpeg, swash and their dependencies) are fetched by Cargo and are not in this
repository; their licences come with their sources (`cargo tree` lists them).

No part of Armed and Dangerous (its executable, data, textures, audio or movies) is in this repository. The
decompiled reference code in `archive/src/` is described in archive/README.md.

## In the engine

| What | Where | Source | Licence |
| --- | --- | --- | --- |
| XeGTAO main pass (ambient occlusion), ported to WGSL | engine/src/graphics/xegtao.wesl | Intel, github.com/GameTechDev/XeGTAO; the original kept in archive/third_party/XeGTAO | MIT, Copyright (C) 2016-2021 Intel Corporation (full text: archive/third_party/XeGTAO/LICENSE) |
| Bevy 0.20's SSAO shader: bindings, noise, edges and view-space helpers kept around the XeGTAO pass; Bevy's standard fragment entry reused | engine/src/graphics/xegtao.wesl, engine/src/graphics/farclip.wesl | bevy_pbr (github.com/bevyengine/bevy) | MIT OR Apache-2.0, Copyright (c) Bevy contributors |
| Kenney "Input Prompts" 1.5 icons, packed into one atlas | tools/data/input_prompts/icons.png (+ icons.json) | Kenney, www.kenney.nl | CC0 1.0 (public domain); credit appreciated, not required |

## In the archive (reference, not built by default)

| What | Where | Licence |
| --- | --- | --- |
| XeGTAO, unmodified | archive/third_party/XeGTAO | MIT, Intel Corporation (archive/third_party/XeGTAO/LICENSE) |
| AMD FidelityFX Shadow Denoiser, unmodified | archive/third_party/FidelityFX-ShadowDenoiser | MIT, Advanced Micro Devices (its LICENSE.txt) |
| AMD FidelityFX API headers (ffx-api), unmodified | archive/third_party/ffx-api | MIT, Advanced Micro Devices (its LICENSE.txt) |
| The SSLD GTAO "afterlight" shader | archive/src/rhi/vulkan/shaders/afterlight.hlsl | MIT, Copyright 2026 Nikhil Verma (includes Intel's XeGTAO MIT notice; docs/references.md) |

The archived hook DLL's build fetches further libraries at pinned versions (MinHook, volk, Vulkan-Headers, glslang,
Jolt Physics, the FidelityFX SDK; optionally NVIDIA DLSS and Intel XeSS SDKs under their own licences): see
archive/hook/CMakeLists.txt and archive/rthelper/CMakeLists.txt. None of them is in this repository.
