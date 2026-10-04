#pragma once
// Renderer interface (docs/renderer-interface.md): everything the game asks of the GPU.
//
// The game's renderer (src/renderer, recovered) and the statically linked D3DX talk Direct3D 9. The D3D9 facade
// (d3d9_facade.cpp) turns those COM calls into calls on these classes, and a backend implements them:
//   rhi::d3d9    pass-through to the real Direct3D 9 (bit-identical: tools/compare.py --rhi)
//   rhi::vulkan  the new backend (docs/goals/modern-renderer.md)
// The vocabulary is the subset of D3D9 the game uses (73 methods, measured with [test] rhi_calls=1 over every
// comparison scenario, plus 5 that only unexercised recovered code calls). D3D9's enums and structs (formats,
// render states, D3DPRESENT_PARAMETERS) are used as plain value types: they are the game's own description of
// its state. Objects are reference counted like COM so the D3D9 backend can mirror every AddRef/Release.
#include <windows.h>
#include <d3d9.h>

namespace rhi
{
class Device;

class Object
{
public:
    virtual ULONG AddRef() = 0;
    virtual ULONG Release() = 0;
    // The D3D9 object behind this one (D3D9 backend), or null. Only the facade's fallback for methods that are
    // not part of the interface uses it.
    virtual void* Native() = 0;

protected:
    virtual ~Object() = default;
};

class Texture;

// A surface of a texture (GetSurfaceLevel / GetCubeMapSurface) is, as in D3D9, owned by the texture: the same object
// for each level and face while the texture lives, and its AddRef/Release act on the texture. The game compares
// surface pointers (Renderer_SetRenderTargetTexture skips SetRenderTarget for "the same" surface), so a surface
// recreated at a recycled address made it skip cube faces at random (lev_1_m1, environment cube).
class Surface : public Object
{
public:
    virtual Texture* Container() { return nullptr; }  // the owning texture, or null (back buffer, depth, CPU)
    virtual HRESULT GetDesc(D3DSURFACE_DESC* desc) = 0;
    virtual HRESULT LockRect(D3DLOCKED_RECT* locked, const RECT* rect, DWORD flags) = 0;
    virtual HRESULT UnlockRect() = 0;
};

// 2D or cube texture (Type()).
class Texture : public Object
{
public:
    virtual D3DRESOURCETYPE Type() = 0;
    virtual DWORD GetLevelCount() = 0;
    virtual DWORD GetLOD() = 0;
    virtual DWORD SetLOD(DWORD lod) = 0;
    virtual HRESULT GetLevelDesc(UINT level, D3DSURFACE_DESC* desc) = 0;
    // New reference, as in D3D9. Cube textures use GetCubeMapSurface.
    virtual HRESULT GetSurfaceLevel(UINT level, Surface** out) = 0;
    virtual HRESULT GetCubeMapSurface(D3DCUBEMAP_FACES face, UINT level, Surface** out) = 0;
    // Lock a level (face 0 for 2D textures) directly, as IDirect3DTexture9::LockRect / cube LockRect do.
    virtual HRESULT LockRect(UINT face, UINT level, D3DLOCKED_RECT* locked, const RECT* rect, DWORD flags) = 0;
    virtual HRESULT UnlockRect(UINT face, UINT level) = 0;
};

// Vertex or index buffer.
class Buffer : public Object
{
public:
    virtual HRESULT Lock(UINT offset, UINT size, void** data, DWORD flags) = 0;
    virtual HRESULT Unlock() = 0;
};

class VertexLayout : public Object {};
class VertexShader : public Object {};
class PixelShader : public Object {};

class Device : public Object
{
public:
    // Device state and frame
    virtual HRESULT TestCooperativeLevel() = 0;
    virtual UINT GetAvailableTextureMem() = 0;
    virtual HRESULT EvictManagedResources() = 0;
    virtual HRESULT GetDeviceCaps(D3DCAPS9* caps) = 0;
    virtual HRESULT Reset(D3DPRESENT_PARAMETERS* params) = 0;
    virtual HRESULT Present(const RECT* src, const RECT* dst, HWND window, const RGNDATA* dirty) = 0;
    virtual HRESULT GetBackBuffer(UINT swapChain, UINT index, D3DBACKBUFFER_TYPE type, Surface** out) = 0;
    virtual void SetGammaRamp(UINT swapChain, DWORD flags, const D3DGAMMARAMP* ramp) = 0;
    virtual HRESULT BeginScene() = 0;
    virtual HRESULT EndScene() = 0;
    virtual HRESULT Clear(DWORD count, const D3DRECT* rects, DWORD flags, D3DCOLOR color, float z, DWORD stencil) = 0;

    // Resources
    virtual HRESULT CreateTexture(UINT width, UINT height, UINT levels, DWORD usage, D3DFORMAT format, D3DPOOL pool,
                                  Texture** out) = 0;
    virtual HRESULT CreateCubeTexture(UINT edge, UINT levels, DWORD usage, D3DFORMAT format, D3DPOOL pool,
                                      Texture** out) = 0;
    virtual HRESULT CreateVertexBuffer(UINT length, DWORD usage, DWORD fvf, D3DPOOL pool, Buffer** out) = 0;
    virtual HRESULT CreateIndexBuffer(UINT length, DWORD usage, D3DFORMAT format, D3DPOOL pool, Buffer** out) = 0;
    virtual HRESULT CreateOffscreenPlainSurface(UINT width, UINT height, D3DFORMAT format, D3DPOOL pool,
                                                Surface** out) = 0;
    virtual HRESULT CreateVertexLayout(const D3DVERTEXELEMENT9* elements, VertexLayout** out) = 0;
    virtual HRESULT CreateVertexShader(const DWORD* code, VertexShader** out) = 0;
    virtual HRESULT CreatePixelShader(const DWORD* code, PixelShader** out) = 0;
    virtual HRESULT StretchRect(Surface* src, const RECT* srcRect, Surface* dst, const RECT* dstRect,
                                D3DTEXTUREFILTERTYPE filter) = 0;

    // Targets
    virtual HRESULT SetRenderTarget(DWORD index, Surface* target) = 0;
    virtual HRESULT SetDepthStencilSurface(Surface* depth) = 0;
    virtual HRESULT GetDepthStencilSurface(Surface** out) = 0;
    virtual HRESULT SetViewport(const D3DVIEWPORT9* viewport) = 0;

    // Fixed-function state
    virtual HRESULT SetTransform(D3DTRANSFORMSTATETYPE state, const D3DMATRIX* matrix) = 0;
    virtual HRESULT SetMaterial(const D3DMATERIAL9* material) = 0;
    virtual HRESULT SetLight(DWORD index, const D3DLIGHT9* light) = 0;
    virtual HRESULT LightEnable(DWORD index, BOOL enable) = 0;
    virtual HRESULT SetRenderState(D3DRENDERSTATETYPE state, DWORD value) = 0;
    virtual HRESULT SetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD value) = 0;
    virtual HRESULT SetSamplerState(DWORD sampler, D3DSAMPLERSTATETYPE type, DWORD value) = 0;
    virtual HRESULT SetTexture(DWORD stage, Texture* texture) = 0;

    // Shaders
    virtual HRESULT SetVertexLayout(VertexLayout* layout) = 0;
    virtual HRESULT SetVertexShader(VertexShader* shader) = 0;
    virtual HRESULT SetVertexShaderConstantF(UINT start, const float* data, UINT count) = 0;
    virtual HRESULT SetPixelShader(PixelShader* shader) = 0;
    virtual HRESULT SetPixelShaderConstantF(UINT start, const float* data, UINT count) = 0;

    // Geometry and draws
    virtual HRESULT SetStreamSource(UINT stream, Buffer* buffer, UINT offset, UINT stride) = 0;
    virtual HRESULT SetIndices(Buffer* indices) = 0;
    virtual HRESULT DrawPrimitive(D3DPRIMITIVETYPE type, UINT start, UINT count) = 0;
    virtual HRESULT DrawIndexedPrimitive(D3DPRIMITIVETYPE type, INT baseVertex, UINT minIndex, UINT vertices,
                                         UINT startIndex, UINT count) = 0;
};

// The adapter: display modes, capabilities and device creation (IDirect3D9's role).
class Adapter : public Object
{
public:
    virtual UINT GetAdapterModeCount(UINT adapter, D3DFORMAT format) = 0;
    virtual HRESULT EnumAdapterModes(UINT adapter, D3DFORMAT format, UINT mode, D3DDISPLAYMODE* out) = 0;
    virtual HRESULT GetAdapterDisplayMode(UINT adapter, D3DDISPLAYMODE* out) = 0;
    virtual HRESULT CheckDeviceFormat(UINT adapter, D3DDEVTYPE type, D3DFORMAT adapterFormat, DWORD usage,
                                      D3DRESOURCETYPE resourceType, D3DFORMAT checkFormat) = 0;
    virtual HRESULT CheckDepthStencilMatch(UINT adapter, D3DDEVTYPE type, D3DFORMAT adapterFormat,
                                           D3DFORMAT targetFormat, D3DFORMAT depthFormat) = 0;
    virtual HRESULT GetDeviceCaps(UINT adapter, D3DDEVTYPE type, D3DCAPS9* caps) = 0;
    virtual HRESULT CreateDevice(UINT adapter, D3DDEVTYPE type, HWND window, DWORD behavior,
                                 D3DPRESENT_PARAMETERS* params, Device** out) = 0;
};

// Output settings for backends with a present pass (the Vulkan backend; the D3D9 backend ignores them). gamma: per
// channel, out = in^(1 / gamma), the curve of the game's brightness option (Renderer_SetGamma); 1 = unchanged.
struct OutputSettings
{
    float gamma[3] = {1.0f, 1.0f, 1.0f};
};
OutputSettings& Output();

namespace d3d9
{
// Pass-through backend over a real IDirect3D9 (takes over the caller's reference).
Adapter* CreateAdapter(IDirect3D9* real);
// The backend object for a real D3D9 object (same object every time while it lives; adds a reference), so the
// facade's fallbacks can wrap objects returned by methods outside the interface.
Object* Adopt(IUnknown* real);
}  // namespace d3d9

namespace vulkan
{
// The Vulkan backend (src/rhi/vulkan). `queries` (a real IDirect3D9, reference taken over) answers caps, mode and
// format queries so the game chooses the same rendering paths as with D3D9; no D3D9 device is created. Null if
// Vulkan is unavailable.
Adapter* CreateAdapter(IDirect3D9* queries);
}  // namespace vulkan
}  // namespace rhi
