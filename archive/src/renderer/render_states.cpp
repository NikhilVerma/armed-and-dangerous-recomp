// Recovered: Renderer render-state setters. S2, Approach B.
//   0x004fee90 Renderer_SetZEnable
//   0x004feee0 Renderer_SetDepthBias
#include <cstring>

#include "gen/entry_stubs.h"
#include "hooks.h"
#include "renderer.h"

namespace
{
const float& kDepthBiasUnit = GameGlobal<float>(0x005bcb48);  // scale from game bias units to D3D bias

DWORD FloatBits(float f)
{
    DWORD d;
    memcpy(&d, &f, 4);
    return d;
}
}  // namespace

// 0x004fee90 (ecx = renderer; [enable]): D3DRS_ZENABLE = D3DZB_USEW when asked for 1 and W-buffering is
// available, else D3DZB_TRUE. Returns the SetRenderState result (0 without a device).
HRESULT Renderer_SetZEnable(Renderer* r, int32_t enable)
{
    r->zEnableState = (enable == 1 && (r->capsFlags & 8)) ? D3DZB_USEW : D3DZB_TRUE;
    r->zEnableRequested = enable;
    if (!r->device)
        return 0;
    return r->device->SetRenderState(D3DRS_ZENABLE, r->zEnableState);
}

// 0x004feee0 (ecx = renderer; [bias]): flush (vtable slot 9), store the bias and program it. With DEPTHBIAS
// support the bias becomes -bias * depthBiasScale * unit (and the same times unit again as slope bias when
// supported); otherwise legacy render state 47. Returns the previous bias.
int32_t Renderer_SetDepthBias(Renderer* r, int32_t bias)
{
    int32_t previous = r->depthBias;
    reinterpret_cast<void(__fastcall*)(Renderer*)>(r->vtbl[9])(r);
    r->depthBias = bias;
    if (!(r->capsFlags & 0x20))
    {
        r->device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(47), bias);
        return previous;
    }
    float depth = static_cast<float>(-bias) * r->depthBiasScale * kDepthBiasUnit;
    r->device->SetRenderState(D3DRS_DEPTHBIAS, FloatBits(depth));
    float slope = depth * kDepthBiasUnit;
    if (r->capsFlags & 0x40)
        r->device->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS, FloatBits(slope));
    return previous;
}

// 0x005050e0 (eax = renderer, edi = blend {src, dest, op, enable byte}): cached alpha blending. Disabled:
// turn off ALPHABLENDENABLE and ALPHATESTENABLE if they were on. Enabled: set SRCBLEND, DESTBLEND and BLENDOP
// where they differ from the cache (+0x8ac..+0x8b4), switch both enables on if the cached enable differs, then
// cache all four words.
uint32_t Renderer_SetBlend(Renderer* r, const uint32_t* blend)
{
    IDirect3DDevice9* d = r->device;
    uint32_t* cache = &Field<uint32_t>(r, 0x8ac);
    if (reinterpret_cast<const uint8_t*>(blend)[0xc] == 0)
    {
        if (Field<uint8_t>(r, 0x8b8) == 0)
            return 0;
        d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        HRESULT hr = r->device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
        Field<uint8_t>(r, 0x8b8) = 0;
        return hr;
    }
    if (blend[0] != cache[0])
        d->SetRenderState(D3DRS_SRCBLEND, blend[0]);
    if (blend[1] != Field<uint32_t>(r, 0x8b0))
        r->device->SetRenderState(D3DRS_DESTBLEND, blend[1]);
    if (blend[2] != Field<uint32_t>(r, 0x8b4))
        r->device->SetRenderState(D3DRS_BLENDOP, blend[2]);
    if (reinterpret_cast<const uint8_t*>(blend)[0xc] != Field<uint8_t>(r, 0x8b8))
    {
        r->device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        r->device->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
    }
    cache[0] = blend[0];
    cache[1] = blend[1];
    cache[2] = blend[2];
    cache[3] = blend[3];
    return blend[3];
}

// 0x005051e0 (esi = renderer, edi = material): for materials using the texture factor (flags +0x48 bits
// 17-18) keep D3DRS_TEXTUREFACTOR in step with +0x734 (cached at +0x8c8). When pixel-shader constants are dirty
// (+0x1684 bit 2) and the material has a shader (+0x4c): c0 = texture factor as float RGBA (factor material),
// c1 = +0x950 and c2 = +0x980 (flags bits 23-24, c2 only with flag bit 1); then mark them clean.
uint32_t Renderer_UpdateMaterialConstants(Renderer* r, const uint8_t* material)
{
    const float scale = GameGlobal<float>(0x005bcc18);  // 1/255
    uint32_t flags = Field<uint32_t>(material, 0x48);
    uint32_t result = flags;
    uint32_t factor = Field<uint32_t>(r, 0x734);
    if ((flags & 0x60000) && factor != Field<uint32_t>(r, 0x8c8))
    {
        r->device->SetRenderState(D3DRS_TEXTUREFACTOR, factor);
        result = Field<uint32_t>(r, 0x734);
        Field<uint32_t>(r, 0x8c8) = result;
    }
    if (!(Field<uint8_t>(r, 0x1684) & 4))
        return result;
    result = Field<uint32_t>(material, 0x4c);
    if (result == 0)
        return result;
    if (Field<uint32_t>(material, 0x48) & 0x60000)
    {
        float c0[4];
        c0[0] = static_cast<float>(static_cast<int32_t>(Field<uint8_t>(r, 0x736))) * scale;
        c0[1] = static_cast<float>(static_cast<int32_t>(Field<uint8_t>(r, 0x735))) * scale;
        c0[2] = static_cast<float>(static_cast<int32_t>(Field<uint32_t>(r, 0x734) & 0xff)) * scale;
        c0[3] = static_cast<float>(static_cast<int32_t>(Field<uint8_t>(r, 0x737))) * scale;
        result = r->device->SetPixelShaderConstantF(0, c0, 1);
    }
    if (Field<uint32_t>(material, 0x48) & 0x1800000)
    {
        result = r->device->SetPixelShaderConstantF(1, &Field<float>(r, 0x950), 1);
        if (Field<uint8_t>(material, 0x48) & 2)
            result = r->device->SetPixelShaderConstantF(2, &Field<float>(r, 0x980), 1);
    }
    Field<uint32_t>(r, 0x1684) &= 0xfffffffb;
    return result;
}

static uint32_t Renderer_SetBlend_Abi(uint32_t eax, uint32_t edi)
{
    return Renderer_SetBlend(reinterpret_cast<Renderer*>(eax), reinterpret_cast<const uint32_t*>(edi));
}
static uint32_t Renderer_UpdateMaterialConstants_Abi(uint32_t esi, uint32_t edi)
{
    return Renderer_UpdateMaterialConstants(reinterpret_cast<Renderer*>(esi), reinterpret_cast<const uint8_t*>(edi));
}
ABI_ENTRY_005050e0(Renderer_SetBlend_Entry, Renderer_SetBlend_Abi)
ABI_ENTRY_005051e0(Renderer_UpdateMaterialConstants_Entry, Renderer_UpdateMaterialConstants_Abi)
HOOK(0x005050e0, "Renderer_SetBlend", Renderer_SetBlend_Entry, nullptr);
HOOK(0x005051e0, "Renderer_UpdateMaterialConstants", Renderer_UpdateMaterialConstants_Entry, nullptr);

static uint32_t Renderer_SetZEnable_Abi(uint32_t ecx, uint32_t enable)
{
    return Renderer_SetZEnable(reinterpret_cast<Renderer*>(ecx), static_cast<int32_t>(enable));
}
static uint32_t Renderer_SetDepthBias_Abi(uint32_t ecx, uint32_t bias)
{
    return Renderer_SetDepthBias(reinterpret_cast<Renderer*>(ecx), static_cast<int32_t>(bias));
}
ABI_ENTRY_004fee90(Renderer_SetZEnable_Entry, Renderer_SetZEnable_Abi)
ABI_ENTRY_004feee0(Renderer_SetDepthBias_Entry, Renderer_SetDepthBias_Abi)
HOOK(0x004fee90, "Renderer_SetZEnable", Renderer_SetZEnable_Entry, nullptr);
HOOK(0x004feee0, "Renderer_SetDepthBias", Renderer_SetDepthBias_Entry, nullptr);
