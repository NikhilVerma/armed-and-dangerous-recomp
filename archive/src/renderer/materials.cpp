// Recovered: material binding and fog mode. S2, Approach B.
//   0x004fc7a0 Renderer_BindMaterialDeclaration
//   0x00503f40 Renderer_SelectFogMode
#include "gen/entry_stubs.h"
#include "hooks.h"
#include "renderer.h"

// 0x004fc7a0 (esi = renderer, edi = material): when the material differs from the bound one (+0x3bc, compared
// by first word): bind it, SetVertexDeclaration(material +0x54), clear +0x8c0, and update the render-flags word
// +0x3a4: bits 2-3 = min(material texture count +0x10, renderer max +0x3b4); bit 23 set unless material flag
// bit 4; bit 24 = material flag bit 6. Invalidates +0x3b0. Returns the new flags (or the bound material).
uint32_t Renderer_BindMaterialDeclaration(Renderer* r, uint8_t* material)
{
    uint32_t* bound = Field<uint32_t*>(r, 0x3bc);
    if (bound && Field<uint32_t>(material, 0) == *bound)
        return reinterpret_cast<uint32_t>(bound);
    Field<uint8_t*>(r, 0x3bc) = material;
    r->device->SetVertexDeclaration(Field<IDirect3DVertexDeclaration9*>(material, 0x54));
    Field<uint32_t>(r, 0x8c0) = 0;
    uint32_t textures = material[0x10];
    uint32_t maxTextures = Field<uint32_t>(r, 0x3b4);
    if (textures >= maxTextures)
        textures = maxTextures;
    uint32_t flags = (Field<uint32_t>(r, 0x3a4) & 0xfffffff3) | (textures << 2);
    Field<uint32_t>(r, 0x3a4) = flags;
    uint32_t materialFlags = Field<uint32_t>(material, 8);
    flags = (materialFlags >> 4) & 1 ? flags & 0xff7fffff : flags | 0x800000;
    Field<uint32_t>(r, 0x3a4) = flags;
    flags = (materialFlags >> 6) & 1 ? flags | 0x1000000 : flags & 0xfeffffff;
    Field<uint32_t>(r, 0x3a4) = flags;
    Field<int32_t>(r, 0x3b0) = -1;
    return flags;
}

// 0x00503f40 (eax = renderer): fixed-function path (+0x33c == 0) with table fog available (+0x15fe bits 0
// and 2): D3DRS_FOGTABLEMODE linear, FOGVERTEXMODE none. With only vertex fog (bit 3), or on the shader path:
// FOGTABLEMODE none, FOGVERTEXMODE linear. Returns 0.
uint32_t Renderer_SelectFogMode(Renderer* r)
{
    uint8_t fogCaps = Field<uint8_t>(r, 0x15fe);
    if (Field<uint32_t>(r, 0x33c) == 0)
    {
        if ((fogCaps & 4) && (fogCaps & 1))
        {
            r->device->SetRenderState(D3DRS_FOGTABLEMODE, D3DFOG_LINEAR);
            r->device->SetRenderState(D3DRS_FOGVERTEXMODE, D3DFOG_NONE);
            return static_cast<uint8_t>(~Field<uint8_t>(r, 0x15fe)) & 1;
        }
        if (!(fogCaps & 8))
            return 0;
    }
    r->device->SetRenderState(D3DRS_FOGTABLEMODE, D3DFOG_NONE);
    r->device->SetRenderState(D3DRS_FOGVERTEXMODE, D3DFOG_LINEAR);
    return 0;
}

static uint32_t Renderer_BindMaterialDeclaration_Abi(uint32_t esi, uint32_t edi)
{
    return Renderer_BindMaterialDeclaration(reinterpret_cast<Renderer*>(esi), reinterpret_cast<uint8_t*>(edi));
}
static uint32_t Renderer_SelectFogMode_Abi(uint32_t eax)
{
    return Renderer_SelectFogMode(reinterpret_cast<Renderer*>(eax));
}
ABI_ENTRY_004fc7a0(Renderer_BindMaterialDeclaration_Entry, Renderer_BindMaterialDeclaration_Abi)
ABI_ENTRY_00503f40(Renderer_SelectFogMode_Entry, Renderer_SelectFogMode_Abi)
HOOK(0x004fc7a0, "Renderer_BindMaterialDeclaration", Renderer_BindMaterialDeclaration_Entry, nullptr);
HOOK(0x00503f40, "Renderer_SelectFogMode", Renderer_SelectFogMode_Entry, nullptr);
