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
