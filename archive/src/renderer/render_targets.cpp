// Recovered: render targets and the back buffer. S2, Approach B.
//   0x00508c50 Renderer_CaptureBackBuffer
//   0x0050a360 Renderer_SetRenderTarget
//   0x0050a5f0 Renderer_DescribeBackBufferTexture
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
const char* const kStrGetBackBufferAsTexture = reinterpret_cast<const char*>(0x0058942c);
}  // namespace

// 0x00508c50 (eax = renderer): re-acquire the back buffer (+0x3d0, its description at +0x3e0) and the depth
// stencil surface (+0x3d4, description at +0x400). Returns the last D3D result.
HRESULT Renderer_CaptureBackBuffer(Renderer* r)
{
    IDirect3DSurface9*& back = Field<IDirect3DSurface9*>(r, 0x3d0);
    IDirect3DSurface9*& depth = Field<IDirect3DSurface9*>(r, 0x3d4);
    if (back)
    {
        back->Release();
        back = nullptr;
    }
    if (depth)
    {
        depth->Release();
        depth = nullptr;
    }
    r->device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back);
    if (back)
        back->GetDesc(&Field<D3DSURFACE_DESC>(r, 0x3e0));
    HRESULT hr = r->device->GetDepthStencilSurface(&depth);
    if (depth)
        hr = depth->GetDesc(&Field<D3DSURFACE_DESC>(r, 0x400));
    return hr;
}

// 0x0050a360 (eax = depth surface, cl = use default depth, edx = colour target, esi = renderer): bind a render
// target (default: the back buffer) and depth surface (null, or the default when cl is set). On change: flush
// (virtual 9), record them (+0x3d8/+0x3dc), SetDepthStencilSurface, SetRenderTarget(0) and cache the target's
// width, height (+0x1634/+0x1638) and bits per pixel (+0x163c). Returns the first failing result, else 0.
HRESULT Renderer_SetRenderTarget(IDirect3DSurface9* depth, bool useDefaultDepth, IDirect3DSurface9* target,
                                 Renderer* r)
{
    if (!target)
        target = Field<IDirect3DSurface9*>(r, 0x3d0);
    if (useDefaultDepth && !depth)
        depth = Field<IDirect3DSurface9*>(r, 0x3d4);
    if (target == Field<IDirect3DSurface9*>(r, 0x3d8) && depth == Field<IDirect3DSurface9*>(r, 0x3dc))
        return 0;
    VCall<void>(r, 9);
    Field<IDirect3DSurface9*>(r, 0x3d8) = target;
    Field<IDirect3DSurface9*>(r, 0x3dc) = depth;
    HRESULT hr = r->device->SetDepthStencilSurface(depth);
    if (hr != 0)
        return hr;
    hr = r->device->SetRenderTarget(0, Field<IDirect3DSurface9*>(r, 0x3d8));
    if (hr == 0)
    {
        D3DSURFACE_DESC desc;
        Field<IDirect3DSurface9*>(r, 0x3d8)->GetDesc(&desc);
        Field<UINT>(r, 0x1638) = desc.Height;
        Field<UINT>(r, 0x1634) = desc.Width;
        Field<uint32_t>(r, 0x163c) = static_cast<uint32_t>(orig::fn_00508750(desc.Format));  // bits per pixel
    }
    return hr;
}

// 0x0050a5f0 (eax = renderer): keep the "GetBackBufferAsTexture" texture descriptor (+0x420) in step with
// the back buffer size: if it is live (+0x443 bit 7) and the size changed, the renderer's virtual 74 rebuilds
// it; if it is (still) not live, reinitialise it for the back buffer size. Returns the back buffer width, or
// the rebuild's result when that left it live.
uint32_t Renderer_DescribeBackBufferTexture(Renderer* r)
{
    uint8_t* tex = &Field<uint8_t>(r, 0x420);
    D3DSURFACE_DESC desc;
    Field<IDirect3DSurface9*>(r, 0x3d0)->GetDesc(&desc);
    uint32_t result = desc.Width;
    if (tex[0x23] & 0x80)
    {
        if (desc.Width != Field<UINT>(tex, 0x10) || desc.Height != Field<UINT>(tex, 0x14))
            result = VCall<uint32_t>(r, 0x128 / 4, tex, 0);
        if (tex[0x23] & 0x80)
            return result;
    }
    memset(tex, 0, 9 * 4);
    Field<uint32_t>(tex, 0x1c) = 0;
    Field<uint32_t>(tex, 0x18) = 0;
    tex[0x21] = 0;
    Field<const char*>(tex, 0) = kStrGetBackBufferAsTexture;
    tex[0x20] = 0x7f;
    Field<UINT>(tex, 0x10) = desc.Width;
    Field<UINT>(tex, 0x14) = desc.Height;
    Field<uint16_t>(tex, 0x22) = 0x6081;
    return desc.Width;
}

static uint32_t Renderer_CaptureBackBuffer_Abi(uint32_t eax)
{
    return Renderer_CaptureBackBuffer(reinterpret_cast<Renderer*>(eax));
}
static uint32_t Renderer_SetRenderTarget_Abi(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t esi)
{
    return Renderer_SetRenderTarget(reinterpret_cast<IDirect3DSurface9*>(eax), (ecx & 0xff) != 0,
                                    reinterpret_cast<IDirect3DSurface9*>(edx), reinterpret_cast<Renderer*>(esi));
}
static uint32_t Renderer_DescribeBackBufferTexture_Abi(uint32_t eax)
{
    return Renderer_DescribeBackBufferTexture(reinterpret_cast<Renderer*>(eax));
}
ABI_ENTRY_00508c50(Renderer_CaptureBackBuffer_Entry, Renderer_CaptureBackBuffer_Abi)
ABI_ENTRY_0050a360(Renderer_SetRenderTarget_Entry, Renderer_SetRenderTarget_Abi)
ABI_ENTRY_0050a5f0(Renderer_DescribeBackBufferTexture_Entry, Renderer_DescribeBackBufferTexture_Abi)
HOOK(0x00508c50, "Renderer_CaptureBackBuffer", Renderer_CaptureBackBuffer_Entry, nullptr);
HOOK(0x0050a360, "Renderer_SetRenderTarget", Renderer_SetRenderTarget_Entry, nullptr);
HOOK(0x0050a5f0, "Renderer_DescribeBackBufferTexture", Renderer_DescribeBackBufferTexture_Entry, nullptr);
