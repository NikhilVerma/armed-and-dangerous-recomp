// rhi::vulkan scene description: geometry extraction and the debug dump. See vk_scene.h.
#include "vk_scene.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>

#include "log.h"

namespace rhi::vulkan
{
namespace
{
const D3DVERTEXELEMENT9* FindPosition(const VulkanVertexLayout* layout)
{
    if (!layout)
        return nullptr;
    for (const D3DVERTEXELEMENT9& e : layout->elements)
    {
        if (e.Stream == 0xff)
            break;
        if (e.Usage == D3DDECLUSAGE_POSITION && e.UsageIndex == 0)
            return &e;
    }
    return nullptr;
}

// Vertex indices of the draw's triangles in order (strips and fans unrolled; degenerate strip triangles dropped).
bool TriangleIndices(const SceneDraw& d, std::vector<uint32_t>& out)
{
    uint32_t n = d.type == D3DPT_TRIANGLELIST ? d.primitives * 3 : d.primitives + 2;
    if (d.type != D3DPT_TRIANGLELIST && d.type != D3DPT_TRIANGLESTRIP && d.type != D3DPT_TRIANGLEFAN)
        return false;
    std::vector<uint32_t> v(n);
    for (uint32_t i = 0; i < n; ++i)
    {
        if (!d.indexed)
        {
            v[i] = d.start + i;
            continue;
        }
        uint32_t size = d.index32 ? 4 : 2, at = (d.start + i) * size;
        if (!d.indexData || at + size > d.indexLength)
            return false;
        uint32_t idx = 0;
        std::memcpy(&idx, d.indexData + at, size);
        v[i] = static_cast<uint32_t>(static_cast<int32_t>(idx) + d.baseVertex);
    }
    for (uint32_t p = 0; p < d.primitives; ++p)
    {
        uint32_t a, b, c;
        if (d.type == D3DPT_TRIANGLELIST)
            a = v[p * 3], b = v[p * 3 + 1], c = v[p * 3 + 2];
        else if (d.type == D3DPT_TRIANGLEFAN)
            a = v[0], b = v[p + 1], c = v[p + 2];
        else if (p & 1)
            a = v[p + 1], b = v[p], c = v[p + 2];  // keep the winding of odd strip triangles
        else
            a = v[p], b = v[p + 1], c = v[p + 2];
        if (a == b || b == c || a == c)
            continue;
        out.push_back(a), out.push_back(b), out.push_back(c);
    }
    return true;
}
// Inverse of a row-major 4x4 (Gauss-Jordan with partial pivoting); false if singular.
bool Invert(const float in[16], double out[16])
{
    double a[4][8];
    for (int r = 0; r < 4; ++r)
        for (int k = 0; k < 4; ++k)
            a[r][k] = in[r * 4 + k], a[r][k + 4] = r == k ? 1.0 : 0.0;
    for (int col = 0; col < 4; ++col)
    {
        int piv = col;
        for (int r = col + 1; r < 4; ++r)
            if (std::fabs(a[r][col]) > std::fabs(a[piv][col]))
                piv = r;
        if (std::fabs(a[piv][col]) < 1e-12)
            return false;
        for (int k = 0; k < 8; ++k)
            std::swap(a[col][k], a[piv][k]);
        double inv = 1.0 / a[col][col];
        for (int k = 0; k < 8; ++k)
            a[col][k] *= inv;
        for (int r = 0; r < 4; ++r)
            if (r != col && a[r][col] != 0.0)
            {
                double f = a[r][col];
                for (int k = 0; k < 8; ++k)
                    a[r][k] -= f * a[col][k];
            }
    }
    for (int r = 0; r < 4; ++r)
        for (int k = 0; k < 4; ++k)
            out[r * 4 + k] = a[r][k + 4];
    return true;
}
}  // namespace

// Armed and Dangerous's vertex shaders all end the same way: every one of the 456 the game creates (work/shaders,
// [test] dump_shaders) computes the world position into r0 (rigid: a 3x4 world matrix at c42..c44; skinned: the
// bone palette at c[a0.x + 42]) and then writes oPos = (dp4 r0 c22, dp4 r0 c23, dp4 r0 c24, dp4 r0 c25). So c22..c25
// is the camera's view-projection, one matrix per frame, and the inverse maps captured clip positions to world space.
// Evidence: position dependency chains of all dumped shaders (8 distinct shapes), session journal 2026-10-04.
constexpr uint32_t kGameCameraRegister = 22;

void SceneCamera(const SceneFrame& f, const SceneDraw& d, float m[16])
{
    if (d.vertexShader)
    {
        std::memcpy(m, &f.vsConstants[d.vsConstantsOffset + kGameCameraRegister * 4], 64);
        return;
    }
    // Row vectors: clip = p * view * projection, i.e. clip[i] = dot(column i of view * projection, p).
    float vp[16];
    const float* v = &d.view._11;
    const float* p = &d.projection._11;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            vp[r * 4 + c] = v[r * 4] * p[c] + v[r * 4 + 1] * p[4 + c] + v[r * 4 + 2] * p[8 + c] + v[r * 4 + 3] * p[12 + c];
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            m[r * 4 + c] = vp[c * 4 + r];
}

bool SceneTriangles(const SceneFrame& f, const SceneDraw& d, std::vector<float>& xyz)
{
    if (d.capture)
    {
        float cam[16];
        double inv[16];
        SceneCamera(f, d, cam);
        std::vector<uint32_t> idx;
        if (!Invert(cam, inv) || !TriangleIndices(d, idx))
            return false;
        for (uint32_t i : idx)
        {
            int64_t k = int64_t(i) - d.captureFirst;
            if (k < 0 || k >= d.captureCount)
                return false;
            const float* c = d.capture + k * 4;
            double w[4];
            for (int r = 0; r < 4; ++r)
                w[r] = inv[r * 4] * c[0] + inv[r * 4 + 1] * c[1] + inv[r * 4 + 2] * c[2] + inv[r * 4 + 3] * c[3];
            double s = std::fabs(w[3]) > 1e-12 ? 1.0 / w[3] : 0.0;
            xyz.push_back(float(w[0] * s)), xyz.push_back(float(w[1] * s)), xyz.push_back(float(w[2] * s));
        }
        return true;
    }
    if (d.vertexShader || d.vertexBlend)
        return false;
    const D3DVERTEXELEMENT9* pos = FindPosition(d.layout);
    if (!pos || pos->Stream >= 4 || (pos->Type != D3DDECLTYPE_FLOAT3 && pos->Type != D3DDECLTYPE_FLOAT4))
        return false;
    const SceneStream& s = d.streams[pos->Stream];
    if (!s.data || !s.stride)
        return false;
    std::vector<uint32_t> idx;
    if (!TriangleIndices(d, idx))
        return false;
    const D3DMATRIX& w = d.world[0];
    for (uint32_t i : idx)
    {
        uint64_t at = s.offset + uint64_t(i) * s.stride + pos->Offset;
        if (at + 12 > s.length)
            return false;
        float p[3];
        std::memcpy(p, s.data + at, 12);
        xyz.push_back(p[0] * w._11 + p[1] * w._21 + p[2] * w._31 + w._41);
        xyz.push_back(p[0] * w._12 + p[1] * w._22 + p[2] * w._32 + w._42);
        xyz.push_back(p[0] * w._13 + p[1] * w._23 + p[2] * w._33 + w._43);
    }
    return true;
}

void DumpScene(const SceneFrame& f)
{
    FILE* t = fopen("and-vk-scene.txt", "w");
    FILE* o = fopen("and-vk-scene.obj", "w");
    if (!t || !o)
    {
        if (t)
            fclose(t);
        if (o)
            fclose(o);
        return;
    }
    fprintf(t, "frame %llu draws %zu lights %u backbuffer %ux%u\n", static_cast<unsigned long long>(f.serial),
            f.draws.size(), f.lightCount, f.width, f.height);
    uint32_t vertexBase = 1, exported = 0;
    std::vector<float> xyz;
    for (size_t i = 0; i < f.draws.size(); ++i)
    {
        const SceneDraw& d = f.draws[i];
        xyz.clear();
        bool geometry = SceneTriangles(f, d, xyz);
        fprintf(t,
                "%zu id=%016llx type=%d prims=%u %s vs=%d blend=%d atest=%d zwrite=%d vblend=%d captured=%d tris=%s "
                "vp=%lu,%lu,%lu,%lu",
                i, static_cast<unsigned long long>(d.id), static_cast<int>(d.type), d.primitives,
                d.indexed ? "indexed" : "list", d.vertexShader ? 1 : 0, d.alphaBlend ? 1 : 0, d.alphaTest ? 1 : 0,
                d.zWrite ? 1 : 0, d.vertexBlend ? 1 : 0, d.capture ? 1 : 0, geometry ? "yes" : "no", d.viewport.X,
                d.viewport.Y, d.viewport.Width, d.viewport.Height);
        for (int s = 0; s < 8; ++s)
            if (d.textures[s])
                fprintf(t, " t%d=%ux%u f%d", s, d.textures[s]->image->width, d.textures[s]->image->height,
                        static_cast<int>(d.textures[s]->image->d3dFormat));
        float cam[16];
        SceneCamera(f, d, cam);
        fprintf(t, "\n  camera");  // row-major: clip[i] = dot(row i, (world, 1))
        for (int k = 0; k < 16; ++k)
            fprintf(t, " %g", cam[k]);
        fprintf(t, "\n");
        if (!geometry || xyz.empty())
            continue;
        ++exported;
        fprintf(o, "o draw_%zu\n", i);
        for (size_t k = 0; k < xyz.size(); k += 3)
            fprintf(o, "v %g %g %g\n", xyz[k], xyz[k + 1], xyz[k + 2]);
        for (uint32_t k = 0; k < xyz.size() / 3; k += 3)
            fprintf(o, "f %u %u %u\n", vertexBase + k, vertexBase + k + 1, vertexBase + k + 2);
        vertexBase += static_cast<uint32_t>(xyz.size() / 3);
    }
    fclose(t);
    fclose(o);
    Log("vulkan: scene of frame %llu: %zu draws, %u with world-space geometry (and-vk-scene.txt, and-vk-scene.obj)",
        static_cast<unsigned long long>(f.serial), f.draws.size(), exported);
}
}  // namespace rhi::vulkan
