// Ray-traced shadows with AMD's FidelityFX Shadow Denoiser (shadow_denoiser.h).
#include "shadow_denoiser.h"

#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <SPIRV/GlslangToSpv.h>

#include <cmath>
#include <cstring>

namespace rth
{
namespace
{
// #include resolution from the embedded FidelityFX files (all file names are unique).
class Includer : public glslang::TShader::Includer
{
public:
    IncludeResult* includeLocal(const char* name, const char*, size_t) override { return Find(name); }
    IncludeResult* includeSystem(const char* name, const char*, size_t) override { return Find(name); }
    void releaseInclude(IncludeResult* r) override { delete r; }

private:
    IncludeResult* Find(const char* path)
    {
        std::string name = path;
        size_t slash = name.find_last_of("/\\");
        if (slash != std::string::npos)
            name = name.substr(slash + 1);
        const char* text = FfxSource(name);
        if (!text)
            return nullptr;
        return new IncludeResult(name, text, std::strlen(text), nullptr);
    }
};

std::vector<uint32_t> Compile(const std::string& source, std::string& log)
{
    static bool init = (glslang::InitializeProcess(), true);
    (void)init;
    glslang::TShader shader(EShLangCompute);
    const char* src = source.c_str();
    shader.setStrings(&src, 1);
    // The SDK's compile options (CMakeCompileDenoiserShaders.txt): FFX_GPU, GLSL, normal (not inverted) depth, and
    // FFX_HALF: the filter passes have no 32-bit path (ffx_denoiser_shadows_filter.h stubs its group-shared loads
    // to 0 without it, which turns every filtered tile black).
    shader.setPreamble("#define FFX_GPU 1\n#define FFX_GLSL 1\n#define FFX_HALF 1\n#define FFX_DENOISER_OPTION_INVERTED_DEPTH 0\n");
    shader.setEnvInput(glslang::EShSourceGlsl, EShLangCompute, glslang::EShClientVulkan, 100);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_3);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_6);
    Includer inc;
    if (!shader.parse(GetDefaultResources(), 460, false, EShMsgDefault, inc))
    {
        log += shader.getInfoLog();
        return {};
    }
    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(EShMsgDefault))
    {
        log += program.getInfoLog();
        return {};
    }
    std::vector<uint32_t> spirv;
    glslang::GlslangToSpv(*program.getIntermediate(EShLangCompute), spirv);
    return spirv;
}

uint32_t MemType(const VkPhysicalDeviceMemoryProperties& mem, uint32_t bits, VkMemoryPropertyFlags flags)
{
    for (uint32_t i = 0; i < mem.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (mem.memoryTypes[i].propertyFlags & flags) == flags)
            return i;
    return 0;
}

// G-buffer for the denoiser: raw depth, world-space normals (from the depth neighbours), camera motion vectors
// (previous uv - uv, static world; FidelityFX: previous_uv = uv + velocity).
const char* kGBuffer = R"(#version 460
layout(local_size_x = 8, local_size_y = 8) in;
layout(std430, set = 0, binding = 0) readonly buffer Depth { float d[]; } depth;
layout(set = 0, binding = 1, r32f) writeonly uniform image2D oDepth;
layout(set = 0, binding = 2, rgba16f) writeonly uniform image2D oNormal;
layout(set = 0, binding = 3, rg16f) writeonly uniform image2D oVelocity;
layout(std140, set = 0, binding = 4) uniform Params
{
    vec4 c2w[3];
    vec4 proj;   // _11 _22 _33 _34
    vec4 proj2;  // _43, width, height
    mat4 prevViewProj;
} p;
float D(ivec2 q)
{
    q = clamp(q, ivec2(0), ivec2(p.proj2.yz) - 1);
    return depth.d[q.y * int(p.proj2.y) + q.x];
}
vec3 Cam(ivec2 q, float d)
{
    vec2 uv = (vec2(q) + 0.5) / p.proj2.yz;
    vec2 ndc = vec2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    float den = d * p.proj.w - p.proj.z;
    float z = p.proj2.x / (abs(den) < 1e-7 ? 1e-7 : den);
    return vec3(ndc.x * z * p.proj.w / p.proj.x, ndc.y * z * p.proj.w / p.proj.y, z);
}
vec3 Rot(vec3 v) { return vec3(dot(p.c2w[0].xyz, v), dot(p.c2w[1].xyz, v), dot(p.c2w[2].xyz, v)); }
vec3 W(vec3 c) { return Rot(c) + vec3(p.c2w[0].w, p.c2w[1].w, p.c2w[2].w); }
void main()
{
    ivec2 q = ivec2(gl_GlobalInvocationID.xy);
    if (q.x >= int(p.proj2.y) || q.y >= int(p.proj2.z))
        return;
    float d = D(q);
    imageStore(oDepth, q, vec4(d));
    if (d >= 0.99999)
    {
        imageStore(oNormal, q, vec4(0.0, 0.0, 1.0, 0.0));
        imageStore(oVelocity, q, vec4(0.0));
        return;
    }
    vec3 c = Cam(q, d);
    vec3 cxp = Cam(q + ivec2(1, 0), D(q + ivec2(1, 0))), cxm = Cam(q - ivec2(1, 0), D(q - ivec2(1, 0)));
    vec3 cyp = Cam(q + ivec2(0, 1), D(q + ivec2(0, 1))), cym = Cam(q - ivec2(0, 1), D(q - ivec2(0, 1)));
    vec3 dx = abs(cxp.z - c.z) < abs(c.z - cxm.z) ? cxp - c : c - cxm;
    vec3 dy = abs(cyp.z - c.z) < abs(c.z - cym.z) ? cyp - c : c - cym;
    vec3 n = normalize(Rot(cross(dx, dy)));
    if (dot(n, Rot(-c)) < 0.0)
        n = -n;
    imageStore(oNormal, q, vec4(n, 0.0));
    vec4 prev = p.prevViewProj * vec4(W(c), 1.0);
    vec2 pn = prev.xy / prev.w;
    vec2 uv = (vec2(q) + 0.5) / p.proj2.yz;
    vec2 prevUv = vec2(pn.x * 0.5 + 0.5, 0.5 - pn.y * 0.5);
    imageStore(oVelocity, q, vec4(prevUv - uv, 0.0, 0.0));
}
)";

// One ray per pixel towards a point of the sun's disc (R2 sequence over frames, per-pixel offset); a hit sets the
// pixel's bit in its 8x4 tile (FidelityFX hit mask: bit y * 8 + x, set = occluded).
const char* kTrace = R"(#version 460
#extension GL_EXT_ray_query : require
layout(local_size_x = 8, local_size_y = 4) in;
layout(set = 0, binding = 0) uniform accelerationStructureEXT scene;
layout(set = 0, binding = 1, r32f) readonly uniform image2D iDepth;
layout(set = 0, binding = 2, rgba16f) readonly uniform image2D iNormal;
layout(std430, set = 0, binding = 3) buffer Bits { uint b[]; } bits;
layout(std140, set = 0, binding = 4) uniform Params
{
    vec4 c2w[3];
    vec4 proj;
    vec4 proj2;  // _43, width, height, frame
    vec4 sun;    // towards the sun, w: tan(angular radius)
} p;
vec3 Cam(ivec2 q, float d)
{
    vec2 uv = (vec2(q) + 0.5) / p.proj2.yz;
    vec2 ndc = vec2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    float den = d * p.proj.w - p.proj.z;
    float z = p.proj2.x / (abs(den) < 1e-7 ? 1e-7 : den);
    return vec3(ndc.x * z * p.proj.w / p.proj.x, ndc.y * z * p.proj.w / p.proj.y, z);
}
vec3 W(vec3 c)
{
    return vec3(dot(p.c2w[0], vec4(c, 1.0)), dot(p.c2w[1], vec4(c, 1.0)), dot(p.c2w[2], vec4(c, 1.0)));
}
float Hash(vec2 v) { return fract(sin(dot(v, vec2(12.9898, 78.233))) * 43758.5453); }
void main()
{
    ivec2 q = ivec2(gl_GlobalInvocationID.xy);
    int w = int(p.proj2.y), h = int(p.proj2.z);
    if (q.x >= w || q.y >= h)
        return;
    float d = imageLoad(iDepth, q).r;
    if (d >= 0.99999)
        return;  // sky: lit
    vec3 c = Cam(q, d);
    vec3 wp = W(c);
    vec3 n = imageLoad(iNormal, q).xyz;
    vec3 L = normalize(p.sun.xyz);
    uint tile = uint(q.y / 4) * uint((w + 7) / 8) + uint(q.x / 8);
    uint bit = 1u << uint((q.y % 4) * 8 + (q.x % 8));
    if (dot(n, L) <= 0.0)
    {
        atomicOr(bits.b[tile], bit);  // facing away from the sun
        return;
    }
    vec2 u = fract(vec2(Hash(vec2(q)), Hash(vec2(q) + 17.0)) + p.proj2.w * vec2(0.7548776662, 0.5698402910));
    vec3 t = normalize(abs(L.z) < 0.9 ? cross(L, vec3(0.0, 0.0, 1.0)) : cross(L, vec3(1.0, 0.0, 0.0)));
    vec3 b = cross(L, t);
    float r = sqrt(u.x) * p.sun.w, a = 6.2831853 * u.y;
    vec3 dir = normalize(L + (t * cos(a) + b * sin(a)) * r);
    vec3 origin = wp + n * (0.02 + length(c) * 0.001);
    rayQueryEXT rq;
    rayQueryInitializeEXT(rq, scene, gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT, 0xff, origin, 0.0, dir, 1e5);
    while (rayQueryProceedEXT(rq))
    {
    }
    if (rayQueryGetIntersectionTypeEXT(rq, true) != gl_RayQueryCommittedIntersectionNoneEXT)
        atomicOr(bits.b[tile], bit);
}
)";

// Debug: the tile hit mask decoded to visibility (1 = lit), without the denoiser.
const char* kDecode = R"(#version 460
layout(local_size_x = 8, local_size_y = 8) in;
layout(std430, set = 0, binding = 0) readonly buffer Bits { uint b[]; } bits;
layout(std430, set = 0, binding = 1) writeonly buffer Vis { float v[]; } vis;
layout(push_constant) uniform P { uint w, h; } p;
void main()
{
    uvec2 q = gl_GlobalInvocationID.xy;
    if (q.x >= p.w || q.y >= p.h)
        return;
    uint tile = (q.y / 4u) * ((p.w + 7u) / 8u) + q.x / 8u;
    uint bit = 1u << ((q.y % 4u) * 8u + (q.x % 8u));
    vis.v[q.y * p.w + q.x] = (bits.b[tile] & bit) != 0u ? 0.0 : 1.0;
}
)";

// Row-major 4x4 helpers (column vectors).
struct M4
{
    float m[16];
};
M4 Mul(const M4& a, const M4& b)
{
    M4 r{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            for (int k = 0; k < 4; ++k)
                r.m[i * 4 + j] += a.m[i * 4 + k] * b.m[k * 4 + j];
    return r;
}
bool Inverse(const M4& a, M4& out)
{
    float inv[16], det;
    const float* m = a.m;
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (std::fabs(det) < 1e-20f)
        return false;
    for (int i = 0; i < 16; ++i)
        out.m[i] = inv[i] / det;
    return true;
}
// D3D9 projection (row vectors) as a column-vector matrix: clip = (x _11, y _22, z _33 + _43, z _34).
M4 Proj(float p11, float p22, float p33, float p34, float p43)
{
    return M4{{p11, 0, 0, 0, 0, p22, 0, 0, 0, 0, p33, p43, 0, 0, p34, 0}};
}
// GLSL mat4 (column-major) from a row-major matrix.
void Store(float* dst, const M4& a)
{
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            dst[c * 4 + r] = a.m[r * 4 + c];
}

bool MakeImage(VkDevice dev, const VkPhysicalDeviceMemoryProperties& mem, Image& im, VkFormat f, uint32_t w,
               uint32_t h)
{
    VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = f;
    ii.extent = {w, h, 1};
    ii.mipLevels = ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
               VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (vkCreateImage(dev, &ii, nullptr, &im.image) != VK_SUCCESS)
        return false;
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(dev, im.image, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = MemType(mem, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(dev, &ai, nullptr, &im.memory) != VK_SUCCESS)
        return false;
    vkBindImageMemory(dev, im.image, im.memory, 0);
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = im.image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = f;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCreateImageView(dev, &vi, nullptr, &im.view);
    im.format = f, im.width = w, im.height = h;
    return true;
}

void FreeImage(VkDevice dev, Image& im)
{
    if (im.view)
        vkDestroyImageView(dev, im.view, nullptr);
    if (im.image)
        vkDestroyImage(dev, im.image, nullptr);
    if (im.memory)
        vkFreeMemory(dev, im.memory, nullptr);
    im = Image{};
}

bool MakeBuffer(VkDevice dev, const VkPhysicalDeviceMemoryProperties& mem, Buffer& b, VkDeviceSize size,
                VkBufferUsageFlags usage, bool host)
{
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = size;
    bi.usage = usage;
    if (vkCreateBuffer(dev, &bi, nullptr, &b.buffer) != VK_SUCCESS)
        return false;
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(dev, b.buffer, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = MemType(mem, req.memoryTypeBits, host ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                                                               : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(dev, &ai, nullptr, &b.memory) != VK_SUCCESS)
        return false;
    vkBindBufferMemory(dev, b.buffer, b.memory, 0);
    b.size = size;
    return true;
}

void FreeBuffer(VkDevice dev, Buffer& b)
{
    if (b.buffer)
        vkDestroyBuffer(dev, b.buffer, nullptr);
    if (b.memory)
        vkFreeMemory(dev, b.memory, nullptr);
    b = Buffer{};
}

void Barrier(VkCommandBuffer cb)
{
    VkMemoryBarrier2 mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    mb.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    mb.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    mb.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    mb.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    VkDependencyInfo di{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    di.memoryBarrierCount = 1;
    di.pMemoryBarriers = &mb;
    vkCmdPipelineBarrier2(cb, &di);
}

VkDescriptorSetLayoutBinding B(uint32_t binding, VkDescriptorType type)
{
    return {binding, type, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
}

// One descriptor write (image, buffer or sampler).
struct DW
{
    uint32_t binding;
    VkDescriptorType type;
    VkDescriptorImageInfo image{};
    VkDescriptorBufferInfo buffer{};
};
DW Img(uint32_t binding, VkDescriptorType type, VkImageView v)
{
    DW w{binding, type};
    w.image = {VK_NULL_HANDLE, v, VK_IMAGE_LAYOUT_GENERAL};
    return w;
}
DW Buf(uint32_t binding, VkDescriptorType type, VkBuffer b, VkDeviceSize offset, VkDeviceSize range)
{
    DW w{binding, type};
    w.buffer = {b, offset, range};
    return w;
}
DW Smp(uint32_t binding, VkSampler s)
{
    DW w{binding, VK_DESCRIPTOR_TYPE_SAMPLER};
    w.image = {s, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
    return w;
}
void Push(VkCommandBuffer cb, VkPipelineLayout layout, std::vector<DW> ws, const void* asNext = nullptr)
{
    std::vector<VkWriteDescriptorSet> out(ws.size());
    for (size_t i = 0; i < ws.size(); ++i)
    {
        VkWriteDescriptorSet& w = out[i];
        w = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w.dstBinding = ws[i].binding;
        w.descriptorCount = 1;
        w.descriptorType = ws[i].type;
        if (ws[i].type == VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR)
            w.pNext = asNext;
        else if (ws[i].type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER || ws[i].type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
            w.pBufferInfo = &ws[i].buffer;
        else
            w.pImageInfo = &ws[i].image;
    }
    vkCmdPushDescriptorSetKHR(cb, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, static_cast<uint32_t>(out.size()),
                              out.data());
}

uint32_t Div(uint32_t a, uint32_t b) { return (a + b - 1) / b; }

constexpr VkDescriptorType kTex = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kImg = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                           kSsbo = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kUbo = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
// Offsets of the constant blocks in constants_ (256-aligned).
constexpr VkDeviceSize kCb0 = 0, kCb1 = 256, kCb2 = 512, kGb = 768, kTr = 1024, kConstSize = 1280;

struct Cb1
{
    float eye[3];
    int32_t firstFrame;
    int32_t dims[2];
    float invDims[2];
    float motionScale[2];
    float normalsMulAdd[2];
    float projInv[16];
    float reproj[16];
    float viewProjInv[16];
};
struct Cb2
{
    float projInv[16];
    float invDims[2];
    float normalsMulAdd[2];
    int32_t dims[2];
    float depthSigma;
    float pad;
};
}  // namespace

bool CompileAll(std::string& log)
{
    const char* ffx[5] = {"ffx_denoiser_prepare_shadow_mask_pass.glsl", "ffx_denoiser_shadows_tile_classification_pass.glsl",
                          "ffx_denoiser_filter_soft_shadows_0_pass.glsl", "ffx_denoiser_filter_soft_shadows_1_pass.glsl",
                          "ffx_denoiser_filter_soft_shadows_2_pass.glsl"};
    bool ok = !Compile(kGBuffer, log).empty();
    ok = !Compile(kTrace, log).empty() && ok;
    for (const char* name : ffx)
    {
        const char* src = FfxSource(name);
        std::string one;
        if (!src || Compile(src, one).empty())
            ok = false, log += std::string(name) + ": " + (src ? one : std::string("not embedded")) + "\n";
    }
    return ok;
}

VkPipeline ShadowDenoiser::Pipeline(const std::string& source, VkPipelineLayout& layoutOut,
                                    VkDescriptorSetLayout& setOut, const std::vector<VkDescriptorSetLayoutBinding>& b,
                                    std::string& log)
{
    std::vector<uint32_t> spirv = Compile(source, log);
    if (spirv.empty())
        return VK_NULL_HANDLE;
    VkShaderModuleCreateInfo smi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    smi.codeSize = spirv.size() * 4;
    smi.pCode = spirv.data();
    VkShaderModule module;
    if (vkCreateShaderModule(device_, &smi, nullptr, &module) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    VkDescriptorSetLayoutCreateInfo dl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dl.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    dl.bindingCount = static_cast<uint32_t>(b.size());
    dl.pBindings = b.data();
    vkCreateDescriptorSetLayout(device_, &dl, nullptr, &setOut);
    VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &setOut;
    vkCreatePipelineLayout(device_, &pl, nullptr, &layoutOut);
    VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    ci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT, ci.stage.module = module, ci.stage.pName = "main";
    ci.layout = layoutOut;
    VkPipeline p = VK_NULL_HANDLE;
    if (vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &ci, nullptr, &p) != VK_SUCCESS)
        log += "pipeline creation failed; ";
    vkDestroyShaderModule(device_, module, nullptr);
    return p;
}

bool ShadowDenoiser::Init(VkDevice device, const VkPhysicalDeviceMemoryProperties& mem, std::string& log)
{
    device_ = device, mem_ = mem;
    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.magFilter = si.minFilter = VK_FILTER_LINEAR;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    vkCreateSampler(device, &si, nullptr, &sampler_);  // the SDK's s_trilinerClamp
    gbuffer_.pipeline = Pipeline(kGBuffer, gbuffer_.layout, gbuffer_.set,
                                 {B(0, kSsbo), B(1, kImg), B(2, kImg), B(3, kImg), B(4, kUbo)}, log);
    trace_.pipeline = Pipeline(kTrace, trace_.layout, trace_.set,
                               {B(0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR), B(1, kImg), B(2, kImg),
                                B(3, kSsbo), B(4, kUbo)},
                               log);
    prepare_.pipeline = Pipeline(FfxSource("ffx_denoiser_prepare_shadow_mask_pass.glsl"), prepare_.layout,
                                 prepare_.set, {B(0, kTex), B(2000, kSsbo), B(3000, kUbo)}, log);
    classify_.pipeline = Pipeline(FfxSource("ffx_denoiser_shadows_tile_classification_pass.glsl"), classify_.layout,
                                  classify_.set,
                                  {B(0, kTex), B(1, kTex), B(2, kTex), B(3, kTex), B(4, kTex), B(5, kTex),
                                   B(1000, VK_DESCRIPTOR_TYPE_SAMPLER), B(2000, kSsbo), B(2001, kImg), B(2002, kImg),
                                   B(2003, kSsbo), B(3000, kUbo)},
                                  log);
    const char* filters[3] = {"ffx_denoiser_filter_soft_shadows_0_pass.glsl",
                              "ffx_denoiser_filter_soft_shadows_1_pass.glsl",
                              "ffx_denoiser_filter_soft_shadows_2_pass.glsl"};
    for (int i = 0; i < 3; ++i)
        filter_[i].pipeline = Pipeline(FfxSource(filters[i]), filter_[i].layout, filter_[i].set,
                                       {B(0, kTex), B(1, kTex), B(2, kTex), B(2000, kSsbo), B(2001, kImg),
                                        B(3000, kUbo)},
                                       log);
    {
        std::vector<uint32_t> spirv = Compile(kDecode, log);
        VkShaderModuleCreateInfo smi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        smi.codeSize = spirv.size() * 4;
        smi.pCode = spirv.data();
        VkShaderModule module = VK_NULL_HANDLE;
        if (!spirv.empty())
            vkCreateShaderModule(device, &smi, nullptr, &module);
        VkDescriptorSetLayoutBinding db[2] = {B(0, kSsbo), B(1, kSsbo)};
        VkDescriptorSetLayoutCreateInfo dl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        dl.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
        dl.bindingCount = 2;
        dl.pBindings = db;
        vkCreateDescriptorSetLayout(device, &dl, nullptr, &decode_.set);
        VkPushConstantRange pr{VK_SHADER_STAGE_COMPUTE_BIT, 0, 8};
        VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pl.setLayoutCount = 1;
        pl.pSetLayouts = &decode_.set;
        pl.pushConstantRangeCount = 1;
        pl.pPushConstantRanges = &pr;
        vkCreatePipelineLayout(device, &pl, nullptr, &decode_.layout);
        VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        ci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT, ci.stage.module = module, ci.stage.pName = "main";
        ci.layout = decode_.layout;
        if (module)
            vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &ci, nullptr, &decode_.pipeline), vkDestroyShaderModule(device, module, nullptr);
    }
    if (!MakeBuffer(device, mem, constants_, kConstSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true))
        return false;
    return gbuffer_.pipeline && trace_.pipeline && prepare_.pipeline && classify_.pipeline && filter_[0].pipeline &&
           filter_[1].pipeline && filter_[2].pipeline;
}

bool ShadowDenoiser::Resize(VkCommandBuffer cb, uint32_t w, uint32_t h)
{
    vkDeviceWaitIdle(device_);
    for (Image* im : {&depth_, &prevDepth_, &normal_, &velocity_, &hitMask_, &moments_[0], &moments_[1], &scratch_[0],
                      &scratch_[1], &output_})
        FreeImage(device_, *im);
    for (Buffer* b : {&hitBits_, &tiles_, &metadata_})
        FreeBuffer(device_, *b);
    const uint32_t tx = Div(w, 8), ty = Div(h, 4);
    bool ok = MakeImage(device_, mem_, depth_, VK_FORMAT_R32_SFLOAT, w, h) &&
              MakeImage(device_, mem_, prevDepth_, VK_FORMAT_R32_SFLOAT, w, h) &&
              MakeImage(device_, mem_, normal_, VK_FORMAT_R16G16B16A16_SFLOAT, w, h) &&
              MakeImage(device_, mem_, velocity_, VK_FORMAT_R16G16_SFLOAT, w, h) &&
              MakeImage(device_, mem_, hitMask_, VK_FORMAT_R32_UINT, tx, ty) &&
              MakeImage(device_, mem_, moments_[0], VK_FORMAT_R32G32B32A32_SFLOAT, w, h) &&
              MakeImage(device_, mem_, moments_[1], VK_FORMAT_R32G32B32A32_SFLOAT, w, h) &&
              MakeImage(device_, mem_, scratch_[0], VK_FORMAT_R32G32_SFLOAT, w, h) &&
              MakeImage(device_, mem_, scratch_[1], VK_FORMAT_R32G32_SFLOAT, w, h) &&
              MakeImage(device_, mem_, output_, VK_FORMAT_R32_SFLOAT, w, h) &&
              MakeBuffer(device_, mem_, hitBits_, VkDeviceSize(tx) * ty * 4,
                         VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                             VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                         false) &&
              MakeBuffer(device_, mem_, tiles_, VkDeviceSize(tx) * ty * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false) &&
              MakeBuffer(device_, mem_, metadata_, VkDeviceSize(tx) * ty * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false);
    if (!ok)
        return false;
    // Everything in GENERAL for good.
    std::vector<VkImageMemoryBarrier2> bs;
    for (Image* im : {&depth_, &prevDepth_, &normal_, &velocity_, &hitMask_, &moments_[0], &moments_[1], &scratch_[0],
                      &scratch_[1], &output_})
    {
        VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        b.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        b.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        b.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
        b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = im->image;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        bs.push_back(b);
    }
    VkDependencyInfo di{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    di.imageMemoryBarrierCount = static_cast<uint32_t>(bs.size());
    di.pImageMemoryBarriers = bs.data();
    vkCmdPipelineBarrier2(cb, &di);
    w_ = w, h_ = h;
    fresh_ = true;
    return true;
}

bool ShadowDenoiser::Record(VkCommandBuffer cb, const DenoiseFrame& f, VkBuffer depth,
                            VkAccelerationStructureKHR tlas, VkBuffer visibility, std::string& log)
{
    if ((f.width != w_ || f.height != h_) && !Resize(cb, f.width, f.height))
    {
        log += "denoiser resources failed; ";
        return false;
    }
    const uint32_t W = f.width, H = f.height, tx = Div(W, 8), ty = Div(H, 4);
    const bool first = fresh_ || f.first;
    fresh_ = false;

    // Matrices (column vectors, row-major here): P, camera -> world, the previous frame's view-projection.
    M4 P = Proj(f.proj[0], f.proj[1], f.proj[2], f.proj[3], f.proj43), Pinv;
    M4 C2W{{f.camToWorld[0], f.camToWorld[1], f.camToWorld[2], f.camToWorld[3], f.camToWorld[4], f.camToWorld[5],
            f.camToWorld[6], f.camToWorld[7], f.camToWorld[8], f.camToWorld[9], f.camToWorld[10], f.camToWorld[11],
            0, 0, 0, 1}};
    Inverse(P, Pinv);
    M4 viewProjInv = Mul(C2W, Pinv);
    M4 prevV;
    std::memcpy(prevV.m, f.prevView, sizeof(prevV.m));
    M4 prevVP = Mul(Proj(f.prevProj[0], f.prevProj[1], f.prevProj[2], f.prevProj[3], f.prevProj[4]), prevV);
    M4 reproj = Mul(prevVP, viewProjInv);

    void* mapped = nullptr;
    vkMapMemory(device_, constants_.memory, 0, kConstSize, 0, &mapped);
    uint8_t* c = static_cast<uint8_t*>(mapped);
    int32_t dims[4] = {int32_t(W), int32_t(H), 0, 0};
    std::memcpy(c + kCb0, dims, sizeof(dims));
    Cb1 cb1{};
    cb1.eye[0] = f.camToWorld[3], cb1.eye[1] = f.camToWorld[7], cb1.eye[2] = f.camToWorld[11];
    cb1.firstFrame = first ? 1 : 0;
    cb1.dims[0] = int32_t(W), cb1.dims[1] = int32_t(H);
    cb1.invDims[0] = 1.0f / W, cb1.invDims[1] = 1.0f / H;
    cb1.motionScale[0] = cb1.motionScale[1] = 1.0f;
    cb1.normalsMulAdd[0] = 1.0f, cb1.normalsMulAdd[1] = 0.0f;  // normals stored as -1..1
    Store(cb1.projInv, Pinv);
    Store(cb1.reproj, reproj);
    Store(cb1.viewProjInv, viewProjInv);
    std::memcpy(c + kCb1, &cb1, sizeof(cb1));
    Cb2 cb2{};
    Store(cb2.projInv, Pinv);
    cb2.invDims[0] = 1.0f / W, cb2.invDims[1] = 1.0f / H;
    cb2.normalsMulAdd[0] = 1.0f, cb2.normalsMulAdd[1] = 0.0f;
    cb2.dims[0] = int32_t(W), cb2.dims[1] = int32_t(H);
    cb2.depthSigma = 1.0f;
    std::memcpy(c + kCb2, &cb2, sizeof(cb2));
    float gb[12 + 4 + 4 + 16];
    std::memcpy(gb, f.camToWorld, 48);
    gb[12] = f.proj[0], gb[13] = f.proj[1], gb[14] = f.proj[2], gb[15] = f.proj[3];
    gb[16] = f.proj43, gb[17] = float(W), gb[18] = float(H), gb[19] = float(f.frame % 1024);
    Store(gb + 20, prevVP);
    std::memcpy(c + kGb, gb, sizeof(gb));
    float tr[12 + 4 + 4 + 4];
    std::memcpy(tr, gb, 20 * sizeof(float));
    std::memcpy(tr + 20, f.sun, 16);
    std::memcpy(c + kTr, tr, sizeof(tr));
    vkUnmapMemory(device_, constants_.memory);

    if (first)
    {
        VkClearColorValue zero{};
        VkImageSubresourceRange all{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        for (Image* im : {&moments_[0], &moments_[1], &scratch_[0], &scratch_[1]})
            vkCmdClearColorImage(cb, im->image, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &all);
    }
    vkCmdFillBuffer(cb, hitBits_.buffer, 0, VK_WHOLE_SIZE, 0);
    Barrier(cb);

    // 1. G-buffer.
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, gbuffer_.pipeline);
    Push(cb, gbuffer_.layout,
         {Buf(0, kSsbo, depth, 0, VkDeviceSize(W) * H * 4), Img(1, kImg, depth_.view), Img(2, kImg, normal_.view),
          Img(3, kImg, velocity_.view), Buf(4, kUbo, constants_.buffer, kGb, 256)});
    vkCmdDispatch(cb, Div(W, 8), Div(H, 8), 1);
    Barrier(cb);

    // 2. Trace: one ray per pixel into the tile hit mask.
    VkWriteDescriptorSetAccelerationStructureKHR wa{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};
    wa.accelerationStructureCount = 1;
    wa.pAccelerationStructures = &tlas;
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, trace_.pipeline);
    Push(cb, trace_.layout,
         {DW{0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR}, Img(1, kImg, depth_.view), Img(2, kImg, normal_.view),
          Buf(3, kSsbo, hitBits_.buffer, 0, VK_WHOLE_SIZE), Buf(4, kUbo, constants_.buffer, kTr, 256)},
         &wa);
    vkCmdDispatch(cb, Div(W, 8), Div(H, 4), 1);
    Barrier(cb);
    VkBufferImageCopy copy{};
    copy.bufferRowLength = tx;
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {tx, ty, 1};
    vkCmdCopyBufferToImage(cb, hitBits_.buffer, hitMask_.image, VK_IMAGE_LAYOUT_GENERAL, 1, &copy);
    Barrier(cb);
    if (f.raw && decode_.pipeline)
    {
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, decode_.pipeline);
        Push(cb, decode_.layout, {Buf(0, kSsbo, hitBits_.buffer, 0, VK_WHOLE_SIZE), Buf(1, kSsbo, visibility, 0, VK_WHOLE_SIZE)});
        uint32_t pc[2] = {W, H};
        vkCmdPushConstants(cb, decode_.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, 8, pc);
        vkCmdDispatch(cb, Div(W, 8), Div(H, 8), 1);
        return true;
    }

    // 3. FidelityFX: prepare, tile classification, (depth -> previous depth), three filter passes (as
    //    denoiserDispatchShadows: moments ping-pong by frame parity, scratch0/1 alternate).
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, prepare_.pipeline);
    Push(cb, prepare_.layout,
         {Img(0, kTex, hitMask_.view), Buf(2000, kSsbo, tiles_.buffer, 0, VK_WHOLE_SIZE),
          Buf(3000, kUbo, constants_.buffer, kCb0, 256)});
    vkCmdDispatch(cb, Div(W, 32), Div(H, 16), 1);
    Barrier(cb);

    const bool even = !(f.frame & 1);
    Image& prevMoments = even ? moments_[0] : moments_[1];
    Image& curMoments = even ? moments_[1] : moments_[0];
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, classify_.pipeline);
    Push(cb, classify_.layout,
         {Img(0, kTex, depth_.view), Img(1, kTex, velocity_.view), Img(2, kTex, normal_.view),
          Img(3, kTex, scratch_[1].view), Img(4, kTex, prevDepth_.view), Img(5, kTex, prevMoments.view),
          Smp(1000, sampler_), Buf(2000, kSsbo, metadata_.buffer, 0, VK_WHOLE_SIZE), Img(2001, kImg, scratch_[0].view),
          Img(2002, kImg, curMoments.view), Buf(2003, kSsbo, tiles_.buffer, 0, VK_WHOLE_SIZE),
          Buf(3000, kUbo, constants_.buffer, kCb1, 256)});
    vkCmdDispatch(cb, Div(W, 8), Div(H, 8), 1);  // 8 x 8 groups (one metadata tile each)
    Barrier(cb);
    VkImageCopy ic{};
    ic.srcSubresource = ic.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    ic.extent = {W, H, 1};
    vkCmdCopyImage(cb, depth_.image, VK_IMAGE_LAYOUT_GENERAL, prevDepth_.image, VK_IMAGE_LAYOUT_GENERAL, 1, &ic);

    struct
    {
        Image* input;
        Image* output;
    } chain[3] = {{&scratch_[0], &scratch_[1]}, {&scratch_[1], &scratch_[0]}, {&scratch_[0], &output_}};
    for (int i = 0; i < 3; ++i)
    {
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, filter_[i].pipeline);
        Push(cb, filter_[i].layout,
             {Img(0, kTex, depth_.view), Img(1, kTex, normal_.view), Img(2, kTex, chain[i].input->view),
              Buf(2000, kSsbo, metadata_.buffer, 0, VK_WHOLE_SIZE), Img(2001, kImg, chain[i].output->view),
              Buf(3000, kUbo, constants_.buffer, kCb2, 256)});
        vkCmdDispatch(cb, Div(W, 8), Div(H, 8), 1);  // 8 x 8 groups, as the classification
        Barrier(cb);
    }

    // 4. The result to the game.
    VkBufferImageCopy out{};
    out.bufferRowLength = W;
    out.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    out.imageExtent = {W, H, 1};
    vkCmdCopyImageToBuffer(cb, output_.image, VK_IMAGE_LAYOUT_GENERAL, visibility, 1, &out);
    return true;
}
}  // namespace rth
