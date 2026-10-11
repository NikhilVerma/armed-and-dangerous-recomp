// rhi::vulkan: vertex input state from a D3D9 vertex declaration, and the hash used for pipeline cache keys.
//
// This file makes no Vulkan calls (it only fills Vulkan structs), so it links into the offline vk_shader_test.
// Conventions: vk_abi.h (attribute locations come from VertexLocation, the same function the shader generators use).
#include <cstring>
#include <algorithm>

#include "vk_abi.h"
#include "vk_pipeline.h"

namespace rhi::vulkan
{
namespace
{
// D3DDECLTYPE -> Vulkan vertex format and component count. VK_FORMAT_UNDEFINED: no exact Vulkan equivalent.
struct DeclFormat
{
    VkFormat format;
    uint8_t components;
};

DeclFormat DeclTypeFormat(BYTE type)
{
    switch (type)
    {
    case D3DDECLTYPE_FLOAT1: return {VK_FORMAT_R32_SFLOAT, 1};
    case D3DDECLTYPE_FLOAT2: return {VK_FORMAT_R32G32_SFLOAT, 2};
    case D3DDECLTYPE_FLOAT3: return {VK_FORMAT_R32G32B32_SFLOAT, 3};
    case D3DDECLTYPE_FLOAT4: return {VK_FORMAT_R32G32B32A32_SFLOAT, 4};
    // D3DCOLOR is ARGB in a DWORD, i.e. bytes B, G, R, A in memory; D3D9 hands it to the shader as (r, g, b, a),
    // which is exactly what B8G8R8A8_UNORM's component swizzle produces.
    case D3DDECLTYPE_D3DCOLOR: return {VK_FORMAT_B8G8R8A8_UNORM, 4};
    // The integer types expand to floats without normalisation ("scaled"). Note: the *SCALED vertex formats are
    // not in Vulkan's mandatory list, but every desktop driver this backend targets supports them.
    case D3DDECLTYPE_UBYTE4: return {VK_FORMAT_R8G8B8A8_USCALED, 4};
    case D3DDECLTYPE_SHORT2: return {VK_FORMAT_R16G16_SSCALED, 2};
    case D3DDECLTYPE_SHORT4: return {VK_FORMAT_R16G16B16A16_SSCALED, 4};
    case D3DDECLTYPE_UBYTE4N: return {VK_FORMAT_R8G8B8A8_UNORM, 4};
    // D3D9 SHORTnN divides by 32767 (so -32768 gives -1.00003); Vulkan SNORM clamps that one value to -1.
    case D3DDECLTYPE_SHORT2N: return {VK_FORMAT_R16G16_SNORM, 2};
    case D3DDECLTYPE_SHORT4N: return {VK_FORMAT_R16G16B16A16_SNORM, 4};
    case D3DDECLTYPE_USHORT2N: return {VK_FORMAT_R16G16_UNORM, 2};
    case D3DDECLTYPE_USHORT4N: return {VK_FORMAT_R16G16B16A16_UNORM, 4};
    // UDEC3 / DEC3N: 10:10:10 packed with the top two bits ignored and w defined as 1. The closest Vulkan formats
    // (A2B10G10R10_USCALED/SNORM_PACK32) return the top two bits as w and are optional for vertex buffers, so
    // these are unsupported: the element is dropped (the shader then reads the default (0, 0, 0, 1)).
    case D3DDECLTYPE_UDEC3: return {VK_FORMAT_UNDEFINED, 3};
    case D3DDECLTYPE_DEC3N: return {VK_FORMAT_UNDEFINED, 3};
    case D3DDECLTYPE_FLOAT16_2: return {VK_FORMAT_R16G16_SFLOAT, 2};
    case D3DDECLTYPE_FLOAT16_4: return {VK_FORMAT_R16G16B16A16_SFLOAT, 4};
    default: return {VK_FORMAT_UNDEFINED, 0};  // D3DDECLTYPE_UNUSED and anything invalid
    }
}
}  // namespace

VertexInput BuildVertexInput(const D3DVERTEXELEMENT9* elements, const uint32_t strides[16])
{
    VertexInput vi;
    uint32_t streamsUsed = 0;
    for (const D3DVERTEXELEMENT9* e = elements; e && e->Stream != 0xff; ++e)  // D3DDECL_END: Stream 0xff
    {
        if (e->Stream >= 16)
            continue;
        // D3DDECLMETHOD_DEFAULT is the only method that reads the element as is; the others (PARTIALU/V,
        // CROSSUV, UV, LOOKUP, LOOKUPPRESAMPLED) belong to the N-patch tessellator / displacement mapping,
        // which this backend does not emulate.
        if (e->Method != D3DDECLMETHOD_DEFAULT)
            continue;
        uint32_t location = VertexLocation(e->Usage, e->UsageIndex);
        if (location == 0xff)
            continue;
        if (vi.mask & (1u << location))  // a location may be fed only once; D3D9 rejects such declarations too
            continue;
        DeclFormat f = DeclTypeFormat(e->Type);
        if (f.format == VK_FORMAT_UNDEFINED)
            continue;
        vi.attributes.push_back({location, e->Stream, f.format, e->Offset});
        vi.mask |= 1u << location;
        streamsUsed |= 1u << e->Stream;
        if (e->Usage == D3DDECLUSAGE_TEXCOORD && e->UsageIndex < 8)
            vi.texCoordSize[e->UsageIndex] = f.components;
    }
    // One binding per stream that feeds at least one attribute; binding number = D3D9 stream index. Instancing
    // (SetStreamSourceFreq) is not represented here: every stream is per-vertex.
    for (uint32_t s = 0; s < 16; ++s)
        if (streamsUsed & (1u << s))
            vi.bindings.push_back({s, strides ? strides[s] : 0, VK_VERTEX_INPUT_RATE_VERTEX});
    std::sort(vi.attributes.begin(), vi.attributes.end(),
              [](const VkVertexInputAttributeDescription& a, const VkVertexInputAttributeDescription& b) {
                  return a.location < b.location;
              });
    return vi;
}

// 64-bit hash from two independent 32-bit lanes (murmur3-style mixing, 4 bytes per step). The 32-bit build has no
// native 64-bit multiply: the previous byte-wise FNV-1a 64 (a _allmul call per byte) was ~17% of the CPU at 4K
// (2026-10-11). Only in-memory cache keys depend on the values.
uint64_t Hash(const void* data, size_t size, uint64_t seed)
{
    const auto* p = static_cast<const unsigned char*>(data);
    uint32_t h1 = static_cast<uint32_t>(seed) ^ 0x9e3779b9u, h2 = static_cast<uint32_t>(seed >> 32) ^ 0x85ebca6bu;
    auto rotl = [](uint32_t x, int r) { return (x << r) | (x >> (32 - r)); };
    size_t i = 0;
    for (; i + 4 <= size; i += 4)
    {
        uint32_t k;
        std::memcpy(&k, p + i, 4);
        uint32_t k1 = rotl(k * 0xcc9e2d51u, 15) * 0x1b873593u;
        h1 = rotl(h1 ^ k1, 13) * 5 + 0xe6546b64u;
        uint32_t k2 = rotl(k * 0x7fb5d329u, 17) * 0x9c4a62c3u;
        h2 = rotl(h2 ^ k2, 11) * 9 + 0x2c8a1db5u;
    }
    uint32_t tail = 0;
    for (size_t j = 0; i + j < size; ++j)
        tail |= uint32_t(p[i + j]) << (8 * j);
    h1 ^= tail * 0xcc9e2d51u;
    h2 ^= tail * 0x7fb5d329u;
    h1 ^= static_cast<uint32_t>(size), h2 ^= static_cast<uint32_t>(size) * 0x27d4eb2fu;
    auto fmix = [](uint32_t h) {
        h ^= h >> 16, h *= 0x85ebca6bu, h ^= h >> 13, h *= 0xc2b2ae35u, h ^= h >> 16;
        return h;
    };
    h1 = fmix(h1 + h2), h2 = fmix(h2 + h1);
    return (uint64_t(h2) << 32) | h1;
}
}  // namespace rhi::vulkan
