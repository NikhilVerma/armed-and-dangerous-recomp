# AMD FidelityFX API headers (unmodified)

From the FidelityFX SDK v1.1.4 (https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK, tag v1.1.4,
`ffx-api/include`, DirectX 12 headers left out), MIT licence (LICENSE.txt). Used by the 64-bit helper
(rthelper/aa.cpp) to run FSR 3.1 through AMD's prebuilt, signed `amd_fidelityfx_vk.dll`, which rthelper/CMakeLists.txt
downloads from the same tag (SHA-256 checked) and places next to rt_helper.exe. v1.1.4 is the last SDK release with
a Vulkan backend (2.x is DirectX 12 only). docs/subsystems/remaster-aa.md.
