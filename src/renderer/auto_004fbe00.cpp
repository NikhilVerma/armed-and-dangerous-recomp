// Recovered: vertex formats, transform/light shader constants, per-draw stream setup, renderer defaults.
//   0x004fbe00 VertexFormat_BuildDeclaration
//   0x004fc020 VertexFormats_Init
//   0x004fcc30 Renderer_FlushTransforms
//   0x004fd0a0 Renderer_PrepareDraw
//   0x004ff2e0 Renderer_UpdateLightConstants
//   0x004ffe00 Renderer_InitDefaults
#include <cmath>
#include <cstring>

#include "gen/entry_stubs.h"
#include "gen/orig_calls.h"
#include "hooks.h"
#include "renderer.h"

namespace
{
template <class T>
uint32_t u(T* p)
{
    return reinterpret_cast<uint32_t>(p);
}

// The 13 vertex formats (0x154 bytes each): +0x08 flags, +0x14 16 element slots {stream, offset, type,
// type size}, +0x54 IDirect3DVertexDeclaration9*, +0x58 D3DVERTEXELEMENT9[17].
uint8_t* const kVertexFormats = reinterpret_cast<uint8_t*>(0x00638f28);
constexpr uint32_t kVertexFormatSize = 0x154;
constexpr uint32_t kVertexFormatCount = 13;

const char* const kStrErrorRegisteringDecl = reinterpret_cast<const char*>(0x00589ea4);

// Vertex shader constant shadow: register n lives at renderer + 0x8b0 + n * 16.
float* ShaderConstantShadow(Renderer* r, uint32_t reg)
{
    return reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(r) + (reg + 0x8b) * 16);
}

// 0x004e37f0 (eax = source vec3s, ecx = matrix, edx = count; [out, mask]) transforms vectors and returns
// nothing (its x87 stack is left empty), so it is called here without the generated wrapper's fstp.
void Renderer_UpdateLightConstants_TransformVec3(uint32_t src_, uint32_t matrix_, uint32_t count_, uint32_t a0,
                                                 uint32_t a1)
{
    static const uint32_t target = 0x004e37f0;
    __asm {
        push ebx
        push esi
        push edi
        push ebp
        push a1
        push a0
        mov eax, src_
        mov ecx, matrix_
        mov edx, count_
        call dword ptr [target]
        add esp, 8
        pop ebp
        pop edi
        pop esi
        pop ebx
    }
}
}  // namespace

uint32_t Renderer_FlushTransforms(Renderer* r);
uint32_t Renderer_UpdateLightConstants(Renderer* r);

// 0x004fbe00 ([vertex format]): build the format's D3DVERTEXELEMENT9 list at +0x58 from its 16 element slots
// (+0x14: stream, offset, type index; type from the table at 0x0057e5d8, usage/index pairs from 0x00589ecc),
// terminate it with D3DDECL_END and qsort it with 0x004fbde0. Returns qsort's EAX.
uint32_t VertexFormat_BuildDeclaration(uint8_t* format)
{
    uint8_t* out = format + 0x58;
    memset(out, 0, 0x80);
    const uint8_t* typeTable = reinterpret_cast<const uint8_t*>(0x0057e5d8);
    const uint8_t* usageTable = reinterpret_cast<const uint8_t*>(0x00589ecc);
    int32_t count = 0;
    for (uint32_t i = 0; i < 16; ++i)
    {
        const uint8_t* slot = format + 0x14 + i * 4;
        if (slot[2] == 0)
            continue;
        uint8_t* e = out + count * 8;
        *reinterpret_cast<uint16_t*>(e) = slot[0];
        *reinterpret_cast<uint16_t*>(e + 2) = slot[1];
        e[4] = typeTable[slot[2] * 4];
        e[5] = 0;
        e[6] = usageTable[i * 2];
        e[7] = usageTable[i * 2 + 1];
        ++count;
    }
    Field<uint16_t>(format, 0x58 + count * 8) = 0xff;
    Field<uint8_t>(format, 0x5c + count * 8) = 0x11;
    return orig::fn_00538f30(u(out), static_cast<uint32_t>(count), 8, 0x004fbde0);
}

namespace
{
void VertexFormats_SetElement(uint32_t format, uint32_t index, uint8_t offset, uint8_t type, const uint8_t* sizes)
{
    uint8_t* e = kVertexFormats + format * kVertexFormatSize + 0x14 + index * 4;
    e[0] = 0;
    e[1] = offset;
    e[2] = type;
    e[3] = sizes[type * 4];
}
}  // namespace

// 0x004fc020 ([renderer]): reset the 13 vertex formats at 0x00638f28, fill in their elements (offset, type
// 0xe/0xf/0x12 with sizes from the table pointed to by 0x005ca8a4) and flags, then for each one compute its
// derived data (0x004fbc30) and create its vertex declaration if missing, logging failures.
uint32_t VertexFormats_Init(Renderer* r)
{
    const uint8_t* sizes = GameGlobal<const uint8_t*>(0x005ca8a4);
    for (uint32_t f = 0; f < kVertexFormatCount; ++f)
    {
        uint8_t* fmt = kVertexFormats + f * kVertexFormatSize;
        memset(fmt, 0, kVertexFormatSize);
        for (uint32_t i = 0; i < 16; ++i)
        {
            uint8_t* e = fmt + 0x14 + i * 4;
            e[0] = 0;
            e[1] = 0;
            e[2] = 0;
            e[3] = sizes[0];
        }
    }
    auto flags = [](uint32_t f) -> uint32_t& { return Field<uint32_t>(kVertexFormats + f * kVertexFormatSize, 8); };
    auto set = [sizes](uint32_t f, uint32_t i, uint8_t offset, uint8_t type) {
        VertexFormats_SetElement(f, i, offset, type, sizes);
    };
    flags(0) = 0x142;
    set(0, 0, 0x00, 0xf);
    set(0, 3, 0x0c, 0x12);
    set(0, 9, 0x10, 0xe);
    flags(12) = 0x442;
    set(12, 0, 0x00, 0xf);
    set(12, 3, 0x0c, 0x12);
    set(12, 9, 0x10, 0xe);
    set(12, 10, 0x18, 0xe);
    set(12, 11, 0x20, 0xe);
    set(12, 12, 0x28, 0xe);
    flags(1) = 0x152;
    set(1, 0, 0x00, 0xf);
    set(1, 2, 0x0c, 0xf);
    set(1, 3, 0x18, 0x12);
    set(1, 9, 0x1c, 0xe);
    flags(2) = 0x42;
    set(2, 0, 0x00, 0xf);
    set(2, 3, 0x0c, 0x12);
    flags(3) = 0xc2;
    set(3, 0, 0x00, 0xf);
    set(3, 3, 0x0c, 0x12);
    set(3, 4, 0x10, 0x12);
    flags(4) = 0x10142;
    set(4, 0, 0x00, 0xf);
    set(4, 3, 0x0c, 0x12);
    set(4, 9, 0x10, 0xf);
    flags(5) = 0x112;
    set(5, 0, 0x00, 0xf);
    set(5, 2, 0x0c, 0xf);
    set(5, 9, 0x18, 0xe);
    flags(6) = 0x212;
    set(6, 0, 0x00, 0xf);
    set(6, 2, 0x0c, 0xf);
    set(6, 9, 0x18, 0xe);
    set(6, 10, 0x20, 0xe);
    flags(7) = 0x312;
    set(7, 0, 0x00, 0xf);
    set(7, 2, 0x0c, 0xf);
    set(7, 9, 0x18, 0xe);
    set(7, 10, 0x20, 0xe);
    set(7, 11, 0x28, 0xe);
    flags(8) = 0x412;
    set(8, 0, 0x00, 0xf);
    set(8, 2, 0x0c, 0xf);
    set(8, 9, 0x18, 0xe);
    set(8, 10, 0x20, 0xe);
    set(8, 11, 0x30, 0xe);
    flags(9) = 0x1d2;
    set(9, 0, 0x00, 0xf);
    set(9, 2, 0x0c, 0xf);
    set(9, 3, 0x18, 0x12);
    set(9, 4, 0x1c, 0x12);
    set(9, 9, 0x20, 0xe);
    flags(10) = 0x150342;
    set(10, 0, 0x00, 0xf);
    set(10, 3, 0x0c, 0x12);
    set(10, 9, 0x10, 0xf);
    set(10, 10, 0x1c, 0xf);
    set(10, 11, 0x28, 0xf);
    flags(11) = 0x80000000;

    auto log = reinterpret_cast<uint32_t(__cdecl*)(const char*, ...)>(0x004e0b30);
    uint32_t result = 0;
    for (uint32_t f = 0; f < kVertexFormatCount; ++f)
    {
        uint8_t* fmt = kVertexFormats + f * kVertexFormatSize;
        orig::fn_004fbc30(0 /* ecx: only scratch for its local slot */, u(fmt));
        IDirect3DDevice9* device = r->device;
        result = u(device);
        if (Field<uint32_t>(fmt, 0x54) != 0)
            continue;
        HRESULT hr = device->CreateVertexDeclaration(reinterpret_cast<D3DVERTEXELEMENT9*>(fmt + 0x58),
                                                     reinterpret_cast<IDirect3DVertexDeclaration9**>(fmt + 0x54));
        result = static_cast<uint32_t>(hr);
        if (hr != 0)
            result = log(kStrErrorRegisteringDecl);
    }
    return result;
}

// 0x004fcc30 (esi = renderer): flush dirty transforms (+0x2d0). Bit 2: projection (+0x138) via 0x004e2210 to
// SetTransform(3). Fixed function (+0x33c null): bit 0 world (+0x6c) -> SetTransform(256), bit 1 view (+0xb0)
// -> SetTransform(2). Shaders: bit 3 projection -> c22..c25, bit 5 world*view (0x004e2910 into +0x1c0) -> c42..c44,
// bit 6 combines world with +0x248 into +0x28c; bit 4 is just cleared.
uint32_t Renderer_FlushTransforms(Renderer* r)
{
    float m[17];
    uint32_t& dirty = Field<uint32_t>(r, 0x2d0);
    uint32_t base = u(r);
    if (dirty & 4)
    {
        orig::fn_004e2210(u(m), base + 0x138);
        r->device->SetTransform(static_cast<D3DTRANSFORMSTATETYPE>(3), reinterpret_cast<D3DMATRIX*>(m));
        dirty &= 0xfffffffb;
    }
    if (Field<uint32_t>(r, 0x33c) == 0)
    {
        uint32_t result = Field<uint8_t>(r, 0x2d0);
        if (dirty & 1)
        {
            orig::fn_004e2210(u(m), base + 0x6c);
            result = static_cast<uint32_t>(
                r->device->SetTransform(static_cast<D3DTRANSFORMSTATETYPE>(0x100), reinterpret_cast<D3DMATRIX*>(m)));
            dirty &= 0xfffffffe;
        }
        if (dirty & 2)
        {
            orig::fn_004e2210(u(m), base + 0xb0);
            result = static_cast<uint32_t>(
                r->device->SetTransform(static_cast<D3DTRANSFORMSTATETYPE>(2), reinterpret_cast<D3DMATRIX*>(m)));
            dirty &= 0xfffffffd;
        }
        return result;
    }
    if (dirty & 8)
    {
        r->device->SetVertexShaderConstantF(0x16, &Field<float>(r, 0x138), 4);
        dirty &= 0xfffffff7;
    }
    uint32_t result = dirty;
    if (result & 0x10)
    {
        result &= 0xffffffef;
        dirty = result;
    }
    if (dirty & 0x20)
    {
        orig::fn_004e2910(base + 0x6c, base + 0x1c0, base + 0xb0);
        result = static_cast<uint32_t>(r->device->SetVertexShaderConstantF(0x2a, &Field<float>(r, 0x1c0), 3));
        dirty &= 0xffffffdf;
    }
    if (dirty & 0x40)
    {
        orig::fn_004e2910(base + 0x6c, base + 0x28c, base + 0x248);
        result = dirty & 0xffffffbf;
        dirty = result;
    }
    return result;
}

// 0x004fd0a0 ([draw item]): before drawing: sync the state object's (+0x18) value into its renderer (+8, via
// 0x00504940), flush transforms, refresh light constants when lit (+0x14 bit 0, shaders). Bind stream sources
// (cached at renderer +0x3c0) from the item's mesh (+0x7c, two streams) or from its buffer and vertex format
// stride; bind the declaration (0x004fc7a0) and upload c16..c21 from the mesh (+0x10) or renderer +0x2dc.
uint32_t Renderer_PrepareDraw(uint8_t* item)
{
    uint8_t* state = Field<uint8_t*>(item, 0x18);
    uint8_t* stateRenderer = Field<uint8_t*>(state, 0xc);
    if (Field<uint32_t>(state, 0x10) != Field<uint32_t>(stateRenderer, 8))
    {
        Field<uint32_t>(stateRenderer, 8) = Field<uint32_t>(state, 0x10);
        orig::fn_00504940(Field<uint32_t>(state, 0xc), u(state) + 0x10, u(state) + 0x10);
    }
    Renderer_FlushTransforms(Field<Renderer*>(state, 0xc));
    if ((Field<uint8_t>(state, 0x14) & 1) && Field<uint32_t>(Field<uint8_t*>(state, 0xc), 0x33c) != 0)
        Renderer_UpdateLightConstants(Field<Renderer*>(state, 0xc));

    uint8_t* declaration;
    uint32_t constants;
    uint8_t* mesh = Field<uint8_t*>(item, 0x7c);
    if (mesh)
    {
        declaration = mesh + 0x98;
        constants = u(mesh) + 0x10;
        uint32_t stream = 0;
        for (uint32_t off = 0; off < 0x28; off += 0x14, ++stream)
        {
            uint8_t* m = Field<uint8_t*>(item, 0x7c);
            uint32_t stride = Field<uint32_t>(m, off + 0x74);
            auto* vb = Field<IDirect3DVertexBuffer9*>(m, off + 0x78);
            Renderer* r = Field<Renderer*>(item, 0xc);
            if (stride == 0)
                vb = nullptr;
            if (u(vb) != Field<uint32_t>(r, 0x3c0 + stream * 8) || stride != Field<uint32_t>(r, 0x3c4 + stream * 8))
            {
                r->device->SetStreamSource(stream, vb, 0, stride);
                Field<uint32_t>(r, 0x3c0 + stream * 8) = u(vb);
                Field<uint32_t>(r, 0x3c4 + stream * 8) = stride;
            }
        }
    }
    else
    {
        Renderer* r = Field<Renderer*>(item, 0xc);
        declaration = kVertexFormats + Field<int32_t>(item, 0x28) * kVertexFormatSize;
        uint32_t stride = Field<uint32_t>(declaration, 4);
        auto* vb = Field<IDirect3DVertexBuffer9*>(item, 0x70 + Field<int32_t>(item, 0x64) * 4);
        constants = u(r) + 0x2dc;
        if (stride == 0)
            vb = nullptr;
        if (u(vb) != Field<uint32_t>(r, 0x3c0) || stride != Field<uint32_t>(r, 0x3c4))
        {
            r->device->SetStreamSource(0, vb, 0, stride);
            Field<uint32_t>(r, 0x3c0) = u(vb);
            Field<uint32_t>(r, 0x3c4) = stride;
        }
    }
    orig::fn_004fc7a0(Field<uint32_t>(item, 0xc), u(declaration));
    Renderer* r = Field<Renderer*>(item, 0xc);
    if (Field<uint32_t>(r, 0x33c) == 0 || Field<uint32_t>(r, 0x2d8) == constants)
        return u(r);
    Field<uint32_t>(r, 0x2d8) = constants;
    return static_cast<uint32_t>(Field<Renderer*>(item, 0xc)->device->SetVertexShaderConstantF(
        0x10, reinterpret_cast<const float*>(constants), 6));
}

// 0x004ff2e0 ([renderer]): shader lighting (+0x33c set). When lights changed (+0x1680): pick up to two
// directional lights and one other light from the 8 D3DLIGHT9s at +0x169c (enabled bytes +0x1a44, only lights
// with diffuse or specular), sum ambient, zero unused c7/c10/c13 slots. Then upload view-space light
// directions/positions (c7+), material-modulated diffuse (c10+) and specular (c13+), power (c6) and ambient (c3).
uint32_t Renderer_UpdateLightConstants(Renderer* r)
{
    if (Field<uint32_t>(r, 0x33c) == 0)
        return 0;
    const uint32_t base = u(r);
    uint8_t* lights = reinterpret_cast<uint8_t*>(r) + 0x169c;  // D3DLIGHT9[8], 0x68 bytes each
    uint32_t* slot = &Field<uint32_t>(r, 0x1a50);              // [3] light index per slot (8 = none)
    uint32_t* slotFlags = &Field<uint32_t>(r, 0x1a5c);         // [3] bit 0 diffuse, bit 1 specular
    float* ambient = &Field<float>(r, 0x1a68);                 // [4] sum of enabled lights' ambient
    uint32_t& count = Field<uint32_t>(r, 0x1a4c);
    const float zero = GameGlobal<float>(0x0056c05c);
    IDirect3DDevice9* device;

    if (Field<uint8_t>(r, 0x1680) != 0)
    {
        slot[0] = 8;
        slot[1] = 8;
        slot[2] = 8;
        Field<uint32_t>(r, 0x1a74) = 0;
        Field<uint32_t>(r, 0x1a70) = 0;
        Field<uint32_t>(r, 0x1a6c) = 0;
        Field<uint32_t>(r, 0x1a68) = 0;
        uint32_t directional = 0;
        bool haveOther = false;
        slotFlags[0] = 0;
        slotFlags[1] = 0;
        slotFlags[2] = 0;
        count = 0;
        for (uint32_t i = 0; i < 8; ++i)
        {
            if (Field<uint8_t>(r, 0x1a44 + i) == 0)
                continue;
            const uint8_t* light = lights + i * 0x68;
            ambient[0] = Field<float>(light, 0x24) + ambient[0];
            ambient[1] = Field<float>(light, 0x28) + ambient[1];
            ambient[2] = Field<float>(light, 0x2c) + ambient[2];
            ambient[3] = Field<float>(light, 0x30) + ambient[3];
            if (count >= 3)
                continue;
            uint32_t diffuse = 0;
            if (Field<float>(light, 0x04) != zero || Field<float>(light, 0x08) != zero ||
                Field<float>(light, 0x0c) != zero || Field<float>(light, 0x10) != zero)
                diffuse = 1;
            uint32_t specular = 0;
            if (Field<float>(light, 0x14) != zero || Field<float>(light, 0x18) != zero ||
                Field<float>(light, 0x1c) != zero || Field<float>(light, 0x20) != zero)
                specular = 1;
            if (diffuse == 0 && specular == 0)
                continue;
            if (Field<int32_t>(light, 0) == 3)  // D3DLIGHT_DIRECTIONAL
            {
                if (directional != 2)
                {
                    slot[directional] = i;
                    slotFlags[directional] = specular * 2 | diffuse;
                    ++directional;
                }
                count = haveOther ? 3 : directional;
            }
            else
            {
                if (!haveOther)
                {
                    slot[2] = i;
                    slotFlags[2] = specular * 2 | diffuse;
                    haveOther = true;
                }
                count = 3;
            }
        }
        uint32_t unused = 3 - count;
        if (unused != 0)
        {
            const float* zeros = reinterpret_cast<const float*>(0x0064a6c8);
            uint32_t reg = count + 7;
            memcpy(ShaderConstantShadow(r, reg), zeros, unused * 16);
            r->device->SetVertexShaderConstantF(reg, zeros, unused);
            reg = count + 10;
            memcpy(ShaderConstantShadow(r, reg), zeros, unused * 16);
            r->device->SetVertexShaderConstantF(reg, zeros, unused);
            reg = count + 13;
            memcpy(ShaderConstantShadow(r, reg), zeros, unused * 16);
            r->device->SetVertexShaderConstantF(reg, zeros, unused);
        }
        Field<uint32_t>(r, 0x3a4) = (Field<uint32_t>(r, 0x3a4) & 0xfffffffc) | count;
        Field<uint32_t>(r, 0x3b0) = 0xffffffff;
        Field<uint32_t>(r, 0x2d0) |= 0x10000;
        Field<uint8_t>(r, 0x1680) = 0;
        Field<uint32_t>(r, 0x1684) |= 6;
    }

    const uint32_t lightCount = count;
    if (lightCount == 0)
        return 0;
    uint32_t result = lightCount;

    if (Field<uint32_t>(r, 0x2d0) & 0x10000)
    {
        float dirs[3][4];
        for (uint32_t k = 0; k < 2; ++k)
        {
            float* d = dirs[k];
            Renderer_UpdateLightConstants_TransformVec3(u(lights + slot[k] * 0x68 + 0x40), base + 0xb0, 1, u(d),
                                                        0xffffffef);
            float x = d[0], y = d[1], z = d[2];
            float sum = z * z + x * x;
            sum = sum + y * y;
            float len = sqrtf(sum);
            if (len != zero)
            {
                float inv = GameGlobal<float>(0x005bcb2c) / len;
                d[0] = inv * d[0];
                d[1] = inv * d[1];
                d[2] = inv * d[2];
            }
            reinterpret_cast<uint32_t*>(d)[3] = 0;
        }
        if (lightCount > 2)
        {
            for (uint32_t k = 2; k < count; ++k)
            {
                float* d = dirs[k];
                uint8_t* light = lights + slot[k] * 0x68;
                if (Field<int32_t>(light, 0) == 3)
                {
                    Renderer_UpdateLightConstants_TransformVec3(u(light + 0x40), base + 0xb0, 1, u(d), 0xffffffef);
                    const float scale = GameGlobal<float>(0x005bcdc4);
                    d[0] = d[0] * scale;
                    d[1] = d[1] * scale;
                    d[2] = d[2] * scale;
                    reinterpret_cast<uint32_t*>(d)[3] = 0;
                }
                else
                {
                    orig::fn_004e36a0(u(light + 0x34), base + 0xb0, 1, u(d));
                    reinterpret_cast<uint32_t*>(d)[3] = Field<uint32_t>(light, 0x58);
                }
            }
        }
        memcpy(&Field<uint8_t>(r, 0x920), dirs, lightCount * 16);
        result = static_cast<uint32_t>(r->device->SetVertexShaderConstantF(7, dirs[0], lightCount));
    }

    if (Field<uint8_t>(r, 0x1684) & 2)
    {
        const float S = GameGlobal<float>(0x005bcc18);  // 1/255
        float diffuse[3][4];
        float specular[3][4];
        uint32_t n = count;
        if (n != 0)
        {
            const float a0 = static_cast<float>(static_cast<int32_t>(Field<uint8_t>(r, 0x2e))) * S;
            const float a1 = static_cast<float>(static_cast<int32_t>(Field<uint8_t>(r, 0x2d))) * S;
            const float a2 = static_cast<float>(static_cast<int32_t>(Field<uint32_t>(r, 0x2c) & 0xff)) * S;
            const float a3 = static_cast<float>(static_cast<int32_t>(Field<uint8_t>(r, 0x2f))) * S;
            const float b0 = static_cast<float>(static_cast<int32_t>(Field<uint8_t>(r, 0x32))) * S;
            const float b1 = static_cast<float>(static_cast<int32_t>(Field<uint8_t>(r, 0x31))) * S;
            const float b2 = static_cast<float>(static_cast<int32_t>(Field<uint32_t>(r, 0x30) & 0xff)) * S;
            const float b3 = static_cast<float>(static_cast<int32_t>(Field<uint8_t>(r, 0x33))) * S;
            for (uint32_t k = 0; k < n; ++k)
            {
                const uint8_t* light = lights + slot[k] * 0x68;
                diffuse[k][0] = a0 * Field<float>(light, 0x04);
                diffuse[k][1] = a1 * Field<float>(light, 0x08);
                diffuse[k][2] = a2 * Field<float>(light, 0x0c);
                diffuse[k][3] = a3 * Field<float>(light, 0x10);
                specular[k][0] = b0 * Field<float>(light, 0x14);
                specular[k][1] = b1 * Field<float>(light, 0x18);
                specular[k][2] = b2 * Field<float>(light, 0x1c);
                specular[k][3] = b3 * Field<float>(light, 0x20);
            }
        }
        memcpy(&Field<uint8_t>(r, 0x950), diffuse, n * 16);
        r->device->SetVertexShaderConstantF(0xa, diffuse[0], n);
        n = count;
        memcpy(&Field<uint8_t>(r, 0x980), specular, n * 16);
        r->device->SetVertexShaderConstantF(0xd, specular[0], n);

        uint32_t power = Field<uint32_t>(r, 0x34);
        uint32_t powerVec[4] = {power, power, power, power};
        Field<uint32_t>(r, 0x910) = power;
        Field<uint32_t>(r, 0x914) = power;
        Field<uint32_t>(r, 0x918) = power;
        Field<uint32_t>(r, 0x91c) = power;
        r->device->SetVertexShaderConstantF(6, reinterpret_cast<const float*>(powerVec), 1);

        float c[4];
        c[0] = static_cast<float>(static_cast<int32_t>(Field<uint8_t>(r, 0x2a))) * S * ambient[0] +
               static_cast<float>(static_cast<int32_t>(Field<uint8_t>(r, 0x26))) * S;
        c[1] = static_cast<float>(static_cast<int32_t>(Field<uint8_t>(r, 0x29))) * S * ambient[1] +
               static_cast<float>(static_cast<int32_t>(Field<uint8_t>(r, 0x25))) * S;
        c[2] = static_cast<float>(static_cast<int32_t>(Field<uint32_t>(r, 0x28) & 0xff)) * S * ambient[2] +
               static_cast<float>(static_cast<int32_t>(Field<uint32_t>(r, 0x24) & 0xff)) * S;
        c[3] = static_cast<float>(static_cast<int32_t>(Field<uint8_t>(r, 0x2b))) * S * ambient[3] +
               static_cast<float>(static_cast<int32_t>(Field<uint8_t>(r, 0x27))) * S;
        const float hi = GameGlobal<float>(0x005bcbe0);
        const float lo = GameGlobal<float>(0x005bcdc0);
        for (float& v : c)
        {
            if (v > hi)
                v = 4.0f;
            else if (v < lo)
                v = -4.0f;
        }
        memcpy(&Field<uint8_t>(r, 0x8e0), c, 16);
        device = r->device;
        result = static_cast<uint32_t>(device->SetVertexShaderConstantF(3, c, 1));
        Field<uint32_t>(r, 0x1684) &= 0xfffffffd;
    }
    Field<uint32_t>(r, 0x2d0) &= 0xfffeffff;
    return result;
}

// 0x004ffe00 (esi = renderer): constructor defaults. Zero the state blocks, make the three intrusive lists
// (+0x50, +0x58, +0x60) empty, set defaults (+0x2dc..+0x33c from 0x00589bc8.. and 0x005795d4.., 3s, flag +0x1680),
// clear the D3D/device pointers, then virtual 43 (1.0f, 0, 0), virtual 99 (0 x5) and virtual 18 with
// 0x0050b9e0/0x0050bae0; returns the last call's result.
uint32_t Renderer_InitDefaults(Renderer* r)
{
    uint8_t* p = reinterpret_cast<uint8_t*>(r);
    memset(p + 0x1490, 0, 0xe * 4);
    memset(p + 0x14e0, 0, 0x48 * 4);
    memset(p + 0x1600, 0, 0xa * 4);
    memset(p + 0x1628, 0, 0x16 * 4);
    memset(p + 0x1680, 0, 0x113 * 4);
    memset(p + 0x2d8, 0, 0x3e * 4);
    memset(p + 0x3d0, 0, 0x38 * 4);
    memset(p + 0x4b0, 0, 0x51 * 4);
    memset(p + 0x50, 0, 7 * 4);
    memset(p + 0x6c, 0, 0x9b * 4);
    Field<uint32_t>(p, 0x9b4) = 0;
    Field<uint32_t>(p, 0x9b8) = 0;
    Field<uint32_t>(p, 0x9bc) = 0;
    memset(p + 0x9c0, 0, 0x2b2 * 4);

    ListNode* lists[3] = {&Field<ListNode>(p, 0x60), &Field<ListNode>(p, 0x50), &Field<ListNode>(p, 0x58)};
    for (ListNode* n : lists)
    {
        n->next = n;
        n->prev = n;
    }
    Field<uint32_t>(p, 0x19dc) = 3;
    Field<uint32_t>(p, 0x3b4) = 3;
    Field<uint32_t>(p, 0x3b8) = 3;
    Field<uint32_t>(p, 0x2dc) = GameGlobal<uint32_t>(0x00589bd8);
    Field<uint32_t>(p, 0x2e0) = GameGlobal<uint32_t>(0x00589bdc);
    Field<uint32_t>(p, 0x2e4) = GameGlobal<uint32_t>(0x00589be0);
    Field<uint32_t>(p, 0x2e8) = GameGlobal<uint32_t>(0x00589be4);
    Field<uint32_t>(p, 0x2ec) = GameGlobal<uint32_t>(0x005795d4);
    Field<uint32_t>(p, 0x2f0) = GameGlobal<uint32_t>(0x005795d8);
    Field<uint32_t>(p, 0x2f4) = GameGlobal<uint32_t>(0x005795dc);
    Field<uint32_t>(p, 0x2f8) = GameGlobal<uint32_t>(0x005795e0);
    Field<uint8_t>(p, 0x1680) = 1;
    Field<uint32_t>(p, 0x1670) = 0;
    Field<uint32_t>(p, 0x166c) = 2;
    for (uint32_t off = 0x2fc; off <= 0x32c; off += 0x10)
    {
        Field<uint32_t>(p, off) = GameGlobal<uint32_t>(0x00589bc8);
        Field<uint32_t>(p, off + 4) = GameGlobal<uint32_t>(0x00589bcc);
        Field<uint32_t>(p, off + 8) = GameGlobal<uint32_t>(0x00589bd0);
        Field<uint32_t>(p, off + 0xc) = GameGlobal<uint32_t>(0x00589bd4);
    }
    Field<uint32_t>(p, 4) = 0;
    r->d3d = nullptr;
    r->device = nullptr;
    VCall<void>(r, 0xac / 4, 0x3f800000u /* 1.0f */, 0u, 0u);
    VCall<void>(r, 0x18c / 4, 0u, 0u, 0u, 0u, 0u);
    return VCall<uint32_t>(r, 0x48 / 4, 0x0050b9e0u, 0x0050bae0u);
}

static uint32_t VertexFormat_BuildDeclaration_Abi(uint32_t format)
{
    return VertexFormat_BuildDeclaration(reinterpret_cast<uint8_t*>(format));
}
static uint32_t VertexFormats_Init_Abi(uint32_t renderer)
{
    return VertexFormats_Init(reinterpret_cast<Renderer*>(renderer));
}
static uint32_t Renderer_FlushTransforms_Abi(uint32_t esi)
{
    return Renderer_FlushTransforms(reinterpret_cast<Renderer*>(esi));
}
static uint32_t Renderer_PrepareDraw_Abi(uint32_t item)
{
    return Renderer_PrepareDraw(reinterpret_cast<uint8_t*>(item));
}
static uint32_t Renderer_UpdateLightConstants_Abi(uint32_t renderer)
{
    return Renderer_UpdateLightConstants(reinterpret_cast<Renderer*>(renderer));
}
static uint32_t Renderer_InitDefaults_Abi(uint32_t esi)
{
    return Renderer_InitDefaults(reinterpret_cast<Renderer*>(esi));
}
ABI_ENTRY_004fbe00(VertexFormat_BuildDeclaration_Entry, VertexFormat_BuildDeclaration_Abi)
ABI_ENTRY_004fc020(VertexFormats_Init_Entry, VertexFormats_Init_Abi)
ABI_ENTRY_004fcc30(Renderer_FlushTransforms_Entry, Renderer_FlushTransforms_Abi)
ABI_ENTRY_004fd0a0(Renderer_PrepareDraw_Entry, Renderer_PrepareDraw_Abi)
ABI_ENTRY_004ff2e0(Renderer_UpdateLightConstants_Entry, Renderer_UpdateLightConstants_Abi)
ABI_ENTRY_004ffe00(Renderer_InitDefaults_Entry, Renderer_InitDefaults_Abi)
HOOK(0x004fbe00, "VertexFormat_BuildDeclaration", VertexFormat_BuildDeclaration_Entry, nullptr);
HOOK(0x004fc020, "VertexFormats_Init", VertexFormats_Init_Entry, nullptr);
HOOK(0x004fcc30, "Renderer_FlushTransforms", Renderer_FlushTransforms_Entry, nullptr);
HOOK(0x004fd0a0, "Renderer_PrepareDraw", Renderer_PrepareDraw_Entry, nullptr);
HOOK(0x004ff2e0, "Renderer_UpdateLightConstants", Renderer_UpdateLightConstants_Entry, nullptr);
HOOK(0x004ffe00, "Renderer_InitDefaults", Renderer_InitDefaults_Entry, nullptr);
