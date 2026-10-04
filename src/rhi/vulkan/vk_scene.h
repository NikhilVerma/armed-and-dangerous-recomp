// rhi::vulkan scene description: what a frame drew into the back buffer with depth testing, one record per draw,
// so later passes (ray tracing, path tracing, PBR material overrides) can rebuild the 3D scene the game submitted
// instead of working from pixels. docs/goals/modern-renderer.md step 5.
//
// Records are valid until the device starts the next frame: they point at the game's buffers, layouts and textures
// (not reference-counted) and at the buffer versions that frame's draws used, which are not rewritten before the
// GPU finished the frame (VulkanBuffer renaming). A consumer copies what it keeps.
//
//   [render] vk_dump_scene=N   frame N's records (and-vk-scene.txt) and the camera-space triangles of its
//                              fixed-function draws (and-vk-scene.obj, one object per draw)
#pragma once

#include <cstdint>
#include <vector>

#include "vk_resources.h"

namespace rhi::vulkan
{
struct SceneStream
{
    const uint8_t* data = nullptr;  // mapped bytes of the version this draw used, from offset 0 (null: unbound)
    VkBuffer buffer = VK_NULL_HANDLE;
    uint32_t offset = 0, stride = 0, length = 0;
};

struct SceneDraw
{
    uint64_t id;  // stable across frames for the same draw (the motion-vector draw id)
    D3DPRIMITIVETYPE type;
    bool indexed;
    int32_t baseVertex;   // indexed draws: added to every index
    uint32_t start;       // first index (indexed) or first vertex
    uint32_t primitives;
    SceneStream streams[4];
    const uint8_t* indexData;  // mapped index bytes from offset 0 (indexed draws)
    VkBuffer indexBuffer;
    uint32_t indexLength;
    bool index32;
    const VulkanVertexLayout* layout;
    D3DVIEWPORT9 viewport;
    D3DMATRIX world[4], view, projection;  // fixed-function transforms (row vectors: p * world * view * projection)
    bool vertexShader;           // positions come out of a game vertex shader; its constants are kept
    uint32_t vsConstantsOffset;  // into SceneFrame::vsConstants (kVsConstantCount float4), when vertexShader
    bool vertexBlend;            // fixed-function vertex blending (world[0..3] weighted)
    VulkanTexture* textures[8];
    D3DMATERIAL9 material;
    bool alphaBlend, alphaTest, zWrite;
    // Captured clip positions (capture frames, vk_abi.h kBindingCapture): vertex index captureFirst + k at
    // capture[k * 4 .. k * 4 + 3], valid after the frame's fence. Null when not captured.
    const float* capture;
    int64_t captureFirst;
    uint32_t captureCount;
    // Replay (shadow maps, vk_shadows.h): the draw's own vertex shader (not a variant) and vertex input.
    VkShaderModule vertexModule;
    const struct VertexInput* input;
    uint32_t vertexCount;  // vertices or indices the draw reads (VertexCount of its primitives)
    // The buffer objects behind streams[] and indexBuffer (the shadow caster cache keeps references).
    VulkanBuffer* streamObjects[4];
    VulkanBuffer* indexObject;
};

struct SceneFrame
{
    uint64_t serial = 0;
    std::vector<SceneDraw> draws;
    std::vector<float> vsConstants;
    D3DLIGHT9 lights[8]{};
    uint32_t lightCount = 0;
    uint32_t width = 0, height = 0;  // back buffer
    // The fog of the frame's opaque geometry (the last opaque draw with D3D9 linear table fog): passes that change
    // the scene's colour after it was fogged (ambient occlusion, shadows) take the fog out and put it back.
    bool fog = false;
    float fogStart = 0.0f, fogEnd = 1.0f;  // 0..1 depth
    float fogColor[3] = {};
    void Clear()
    {
        draws.clear();
        vsConstants.clear();
        lightCount = 0;
        fog = false;
    }
};

// The draw's camera: row-major 4x4 `m` with clip[i] = dot(row i, (world, 1)). The fixed-function view * projection,
// or for the game's vertex shaders their c22..c25, the projection alone (camera space; see vk_scene.cpp).
void SceneCamera(const SceneFrame& f, const SceneDraw& d, float m[16]);

// Positions of a draw's triangles (3 xyz per triangle) appended to `xyz`, in the space SceneCamera maps from
// (camera space for the game's vertex shaders, world space for fixed function): from the captured clip positions
// through the inverse camera, or for uncaptured fixed-function draws (no vertex blending, FLOAT3/FLOAT4
// position) from the vertex buffer and world matrix. Otherwise false.
bool SceneTriangles(const SceneFrame& f, const SceneDraw& d, std::vector<float>& xyz);

// Writes and-vk-scene.txt and and-vk-scene.obj (see above).
void DumpScene(const SceneFrame& f);

// [render] vk_dump_scene: the frame serial to dump and capture, or ~0 for none.
uint64_t DumpSceneFrame();
}  // namespace rhi::vulkan
