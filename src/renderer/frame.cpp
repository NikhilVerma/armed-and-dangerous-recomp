// Recovered: Renderer frame and state helpers. S2, Approach B.
//   0x005043d0 Renderer_EndScene
//   0x00505070 Renderer_SetFogColor
//   0x00509740 Renderer_CheckDevice
//   0x00504e70 Renderer_SetTransform
//   0x00506ed0 Renderer_InitStateCache
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
const float& kColorScale = GameGlobal<float>(0x005bcb34);           // 255
const D3DTRANSFORMSTATETYPE* const kTransformStates =
    reinterpret_cast<const D3DTRANSFORMSTATETYPE*>(0x0057d9d0);    // per transform slot
}  // namespace

// 0x005043d0 (ecx = renderer): nested scene end. When the begin count (+0x162c) drops to 0: release the two
// per-scene objects (+0x3d0, +0x3d4), clear the scene fields and call EndScene. Returns true if EndScene
// succeeded, false otherwise or when still nested.
bool Renderer_EndScene(Renderer* r)
{
    if (--Field<int32_t>(r, 0x162c) != 0)
        return false;
    static const uint32_t kSceneObjects[] = {0x3d0, 0x3d4};
    for (uint32_t offset : kSceneObjects)
    {
        IUnknown*& object = Field<IUnknown*>(r, offset);
        if (object)
        {
            object->Release();
            object = nullptr;
        }
    }
    Field<uint32_t>(r, 0x3d4) = 0;
    Field<uint32_t>(r, 0x3d0) = 0;
    Field<uint32_t>(r, 0x3dc) = 0;
    Field<uint32_t>(r, 0x3d8) = 0;
    return r->device->EndScene() == 0;
}

// 0x00505070 (eax = renderer, ecx = float RGB or null for the renderer's default at +0x1688):
// D3DRS_FOGCOLOR = 0x00RRGGBB from components * 255 (truncated).
HRESULT Renderer_SetFogColor(Renderer* r, const float* rgb)
{
    if (!rgb)
        rgb = &Field<float>(r, 0x1688);
    IDirect3DDevice9* device = r->device;
    uint32_t red = static_cast<uint32_t>(static_cast<int32_t>(rgb[0] * kColorScale)) & 0xff;
    uint32_t green = static_cast<uint32_t>(static_cast<int32_t>(rgb[1] * kColorScale)) & 0xff;
    uint32_t blue = static_cast<uint32_t>(static_cast<int32_t>(rgb[2] * kColorScale)) & 0xff;
    return device->SetRenderState(D3DRS_FOGCOLOR, (red << 16) | (green << 8) | blue);
}

// 0x00509740 (ecx = renderer; [active]): device state. 0 = no device or lost (wait); after a
// D3DERR_DEVICENOTRESET: refresh the desktop mode when windowed, then 0 if the reset (0x00509620) succeeded,
// else 3. A working device returns 2 when `active` is 0, else 1; other errors 1.
int32_t Renderer_CheckDevice(Renderer* r, int32_t active)
{
    if (!r->device)
        return 0;
    HRESULT hr = r->device->TestCooperativeLevel();
    if (hr >= 0)
        return active == 0 ? 2 : 1;
    if (hr == D3DERR_DEVICELOST)
        return 0;
    if (hr != D3DERR_DEVICENOTRESET)
        return 1;
    if (Field<uint8_t>(r, 0x1628) & 0x20)
        r->d3d->GetAdapterDisplayMode(r->field_9e8, &r->desktopMode);
    bool reset = (orig::fn_00509620(u(r)) & 0xff) != 0;
    return reset ? 0 : 3;
}

// 0x00504e70 (eax = renderer, ecx = matrix, edi = transform slot): fixed function (+0x33c == 0): transform
// the matrix (0x004e2910 with the renderer's matrix at +0xf4, then 0x004e2210) and SetTransform to the
// slot's state; shader path: SetVertexShaderConstantF(26 + 4 * slot, matrix, 4).
HRESULT Renderer_SetTransform(Renderer* r, const float* matrix, int32_t slot)
{
    if (Field<uint32_t>(r, 0x33c) == 0)
    {
        float combined[17];
        float result[17];
        orig::fn_004e2910(u(&Field<uint8_t>(r, 0xf4)), u(combined), u(matrix));
        orig::fn_004e2210(u(result), u(combined));
        return r->device->SetTransform(kTransformStates[slot], reinterpret_cast<D3DMATRIX*>(result));
    }
    return r->device->SetVertexShaderConstantF(slot * 4 + 0x1a, matrix, 4);
}

// 0x00506ed0 (esi = renderer): create the state-cache object (virtual 8 with 0, 0x400, 1, stored at +0x580),
// fetch virtual 6's value (+0x58c) and hand it to the cache (its virtual 3); clear the cache bookkeeping.
uint32_t Renderer_InitStateCache(Renderer* r)
{
    void* cache = VCall<void*>(r, 8, 0, 0x400, 1);
    Field<void*>(r, 0x580) = cache;
    uint32_t value = VCall<uint32_t>(r, 6);
    Field<uint32_t>(r, 0x58c) = value;
    VCall<void>(Field<void*>(r, 0x580), 3, value);
    Field<uint32_t>(r, 0x584) = 0;
    memset(&Field<uint8_t>(r, 0x5d8), 0, 7 * 4);
    Field<void*>(r, 0x590) = &Field<uint8_t>(r, 0x594);
    memset(&Field<uint8_t>(r, 0x594), 0, 9 * 4);
    return 0;
}

static uint32_t Renderer_EndScene_Abi(uint32_t ecx)
{
    return Renderer_EndScene(reinterpret_cast<Renderer*>(ecx));
}
static uint32_t Renderer_SetFogColor_Abi(uint32_t eax, uint32_t ecx)
{
    return Renderer_SetFogColor(reinterpret_cast<Renderer*>(eax), reinterpret_cast<const float*>(ecx));
}
static uint32_t Renderer_CheckDevice_Abi(uint32_t ecx, uint32_t active)
{
    return Renderer_CheckDevice(reinterpret_cast<Renderer*>(ecx), active);
}
static uint32_t Renderer_SetTransform_Abi(uint32_t eax, uint32_t ecx, uint32_t edi)
{
    return Renderer_SetTransform(reinterpret_cast<Renderer*>(eax), reinterpret_cast<const float*>(ecx), edi);
}
static uint32_t Renderer_InitStateCache_Abi(uint32_t esi)
{
    return Renderer_InitStateCache(reinterpret_cast<Renderer*>(esi));
}
ABI_ENTRY_005043d0(Renderer_EndScene_Entry, Renderer_EndScene_Abi)
ABI_ENTRY_00505070(Renderer_SetFogColor_Entry, Renderer_SetFogColor_Abi)
ABI_ENTRY_00509740(Renderer_CheckDevice_Entry, Renderer_CheckDevice_Abi)
ABI_ENTRY_00504e70(Renderer_SetTransform_Entry, Renderer_SetTransform_Abi)
ABI_ENTRY_00506ed0(Renderer_InitStateCache_Entry, Renderer_InitStateCache_Abi)
HOOK(0x005043d0, "Renderer_EndScene", Renderer_EndScene_Entry, nullptr);
HOOK(0x00505070, "Renderer_SetFogColor", Renderer_SetFogColor_Entry, nullptr);
HOOK(0x00509740, "Renderer_CheckDevice", Renderer_CheckDevice_Entry, nullptr);
HOOK(0x00504e70, "Renderer_SetTransform", Renderer_SetTransform_Entry, nullptr);
HOOK(0x00506ed0, "Renderer_InitStateCache", Renderer_InitStateCache_Entry, nullptr);
