// Direct3D 9 COM facade: the boundary of the renderer interface (docs/renderer-interface.md).
//
// Every D3D9 object the game sees (recovered renderer code and the statically linked D3DX alike) is one of the
// generated Facade_* classes (d3d9_facade.gen.inl), standing for an rhi:: object. The methods the game uses are
// routed to the renderer interface (Route_* below; the list is ROUTED in tools/gen_d3d9_facade.py). Any other
// method falls back to the backend's D3D9 object when there is one, and is reported as unsupported otherwise.
// [render] rhi=1 installs it; with the D3D9 backend the game must stay bit-identical (tools/compare.py --rhi).
//
// Wrapping: an rhi object maps to one wrapper while the game holds references through it. The wrapper counts
// those references; every AddRef/Release also goes to the rhi object, which mirrors them to D3D9.
#include <windows.h>
#include <d3d9.h>

#include <cstdio>
#include <mutex>
#include <unordered_map>

#include "config.h"
#include "log.h"
#include "rhi/rhi.h"

class Facade_Direct3D;
class Facade_Device;
class Facade_StateBlock;
class Facade_SwapChain;
class Facade_VertexDeclaration;
class Facade_VertexShader;
class Facade_PixelShader;
class Facade_Texture;
class Facade_VolumeTexture;
class Facade_CubeTexture;
class Facade_VertexBuffer;
class Facade_IndexBuffer;
class Facade_Surface;
class Facade_Volume;
class Facade_Query;

namespace
{
volatile long g_calls[400];
volatile long g_unsupported[400];
std::mutex g_lock;
std::unordered_map<rhi::Object*, void*> g_byObject;  // rhi object -> wrapper (as its interface pointer)
std::unordered_map<void*, rhi::Object*> g_byWrapper;

void RhiCount(int index)
{
    InterlockedIncrement(&g_calls[index]);
}

const char* MethodName(int index);

template <class T>
T FacadeUnsupported(int index)
{
    if (InterlockedIncrement(&g_unsupported[index]) == 1)
        Log("rhi: %s is not supported by this backend", MethodName(index));
    if constexpr (std::is_same_v<T, HRESULT>)
        return E_NOTIMPL;
    else if constexpr (!std::is_void_v<T>)
        return T{};
}

// A method outside the renderer interface reached the D3D9 backend's object: fine for D3D9, a gap for any other
// backend. Logged once per method; a clean run means the interface covers everything the game used.
volatile long g_fallbacks[400];
void FacadeFallback(int index)
{
    if (InterlockedIncrement(&g_fallbacks[index]) == 1)
        Log("rhi: %s is outside the renderer interface (D3D9 fallback)", MethodName(index));
}

rhi::Object* ObjOf(void* wrapper)
{
    if (!wrapper)
        return nullptr;
    std::lock_guard<std::mutex> g(g_lock);
    auto it = g_byWrapper.find(wrapper);
    return it == g_byWrapper.end() ? nullptr : it->second;
}

// Fallback path: the D3D9 object behind a wrapper.
template <class R>
R* FacadeUnwrap(R* p)
{
    rhi::Object* o = ObjOf(p);
    return o ? static_cast<R*>(o->Native()) : p;
}

template <class W>
void FacadeForget(W* wrapper)
{
    {
        std::lock_guard<std::mutex> g(g_lock);
        g_byObject.erase(wrapper->obj);
        g_byWrapper.erase(static_cast<void*>(wrapper));
    }
    delete wrapper;
}

// Interfaces each wrapper answers QueryInterface for (besides IUnknown).
bool Answers(Facade_Direct3D*, REFIID r) { return r == __uuidof(IDirect3D9); }
bool Answers(Facade_Device*, REFIID r) { return r == __uuidof(IDirect3DDevice9); }
bool Answers(Facade_Texture*, REFIID r)
{
    return r == __uuidof(IDirect3DTexture9) || r == __uuidof(IDirect3DBaseTexture9) || r == __uuidof(IDirect3DResource9);
}
bool Answers(Facade_CubeTexture*, REFIID r)
{
    return r == __uuidof(IDirect3DCubeTexture9) || r == __uuidof(IDirect3DBaseTexture9) ||
           r == __uuidof(IDirect3DResource9);
}
bool Answers(Facade_VolumeTexture*, REFIID r)
{
    return r == __uuidof(IDirect3DVolumeTexture9) || r == __uuidof(IDirect3DBaseTexture9) ||
           r == __uuidof(IDirect3DResource9);
}
bool Answers(Facade_Surface*, REFIID r)
{
    return r == __uuidof(IDirect3DSurface9) || r == __uuidof(IDirect3DResource9);
}
bool Answers(Facade_VertexBuffer*, REFIID r)
{
    return r == __uuidof(IDirect3DVertexBuffer9) || r == __uuidof(IDirect3DResource9);
}
bool Answers(Facade_IndexBuffer*, REFIID r)
{
    return r == __uuidof(IDirect3DIndexBuffer9) || r == __uuidof(IDirect3DResource9);
}
bool Answers(Facade_VertexDeclaration*, REFIID r) { return r == __uuidof(IDirect3DVertexDeclaration9); }
bool Answers(Facade_VertexShader*, REFIID r) { return r == __uuidof(IDirect3DVertexShader9); }
bool Answers(Facade_PixelShader*, REFIID r) { return r == __uuidof(IDirect3DPixelShader9); }
bool Answers(Facade_StateBlock*, REFIID r) { return r == __uuidof(IDirect3DStateBlock9); }
bool Answers(Facade_SwapChain*, REFIID r) { return r == __uuidof(IDirect3DSwapChain9); }
bool Answers(Facade_Volume*, REFIID r) { return r == __uuidof(IDirect3DVolume9); }
bool Answers(Facade_Query*, REFIID r) { return r == __uuidof(IDirect3DQuery9); }

template <class W>
HRESULT FacadeQueryInterface(W* self, REFIID riid, void** out)
{
    if (riid == __uuidof(IUnknown) || Answers(self, riid))
    {
        ++self->refs;
        self->obj->AddRef();
        *out = self;
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

// Fallback path: wrap a D3D9 object returned by a method outside the interface.
IDirect3D9* FacadeWrap(IDirect3D9* r);
IDirect3DDevice9* FacadeWrap(IDirect3DDevice9* r);
IDirect3DStateBlock9* FacadeWrap(IDirect3DStateBlock9* r);
IDirect3DSwapChain9* FacadeWrap(IDirect3DSwapChain9* r);
IDirect3DVertexDeclaration9* FacadeWrap(IDirect3DVertexDeclaration9* r);
IDirect3DVertexShader9* FacadeWrap(IDirect3DVertexShader9* r);
IDirect3DPixelShader9* FacadeWrap(IDirect3DPixelShader9* r);
IDirect3DTexture9* FacadeWrap(IDirect3DTexture9* r);
IDirect3DVolumeTexture9* FacadeWrap(IDirect3DVolumeTexture9* r);
IDirect3DCubeTexture9* FacadeWrap(IDirect3DCubeTexture9* r);
IDirect3DVertexBuffer9* FacadeWrap(IDirect3DVertexBuffer9* r);
IDirect3DIndexBuffer9* FacadeWrap(IDirect3DIndexBuffer9* r);
IDirect3DSurface9* FacadeWrap(IDirect3DSurface9* r);
IDirect3DVolume9* FacadeWrap(IDirect3DVolume9* r);
IDirect3DQuery9* FacadeWrap(IDirect3DQuery9* r);
IDirect3DBaseTexture9* FacadeWrap(IDirect3DBaseTexture9* r);
}  // namespace

#include "d3d9_facade.gen.inl"

namespace
{
const char* MethodName(int index)
{
    return index >= 0 && index < kRhiMethodCount ? kRhiMethodNames[index] : "?";
}

Facade_Device* g_device;  // the game creates one device

// The wrapper for an rhi object (adds a wrapper reference; the object's reference came with it).
template <class W, class I = W>
I* WrapAs(rhi::Object* o)
{
    if (!o)
        return nullptr;
    std::lock_guard<std::mutex> g(g_lock);
    auto it = g_byObject.find(o);
    if (it != g_byObject.end())
    {
        auto* w = static_cast<W*>(static_cast<I*>(it->second));
        ++w->refs;
        return w;
    }
    W* w = new W(o);
    I* iface = w;
    g_byObject[o] = iface;
    g_byWrapper[iface] = o;
    return iface;
}

IDirect3DBaseTexture9* WrapTexture(rhi::Texture* t)
{
    if (!t)
        return nullptr;
    if (t->Type() == D3DRTYPE_CUBETEXTURE)
        return WrapAs<Facade_CubeTexture, IDirect3DCubeTexture9>(t);
    return WrapAs<Facade_Texture, IDirect3DTexture9>(t);
}

template <class T>
T* Obj(void* wrapper)
{
    return static_cast<T*>(ObjOf(wrapper));
}

rhi::Device* Dev(Facade_Device* self)
{
    return static_cast<rhi::Device*>(self->obj);
}

IDirect3D9* FacadeWrap(IDirect3D9* r) { return WrapAs<Facade_Direct3D>(rhi::d3d9::Adopt(r)); }
IDirect3DDevice9* FacadeWrap(IDirect3DDevice9* r) { return WrapAs<Facade_Device>(rhi::d3d9::Adopt(r)); }
IDirect3DStateBlock9* FacadeWrap(IDirect3DStateBlock9* r) { return WrapAs<Facade_StateBlock>(rhi::d3d9::Adopt(r)); }
IDirect3DSwapChain9* FacadeWrap(IDirect3DSwapChain9* r) { return WrapAs<Facade_SwapChain>(rhi::d3d9::Adopt(r)); }
IDirect3DVertexDeclaration9* FacadeWrap(IDirect3DVertexDeclaration9* r)
{
    return WrapAs<Facade_VertexDeclaration>(rhi::d3d9::Adopt(r));
}
IDirect3DVertexShader9* FacadeWrap(IDirect3DVertexShader9* r) { return WrapAs<Facade_VertexShader>(rhi::d3d9::Adopt(r)); }
IDirect3DPixelShader9* FacadeWrap(IDirect3DPixelShader9* r) { return WrapAs<Facade_PixelShader>(rhi::d3d9::Adopt(r)); }
IDirect3DTexture9* FacadeWrap(IDirect3DTexture9* r) { return WrapAs<Facade_Texture>(rhi::d3d9::Adopt(r)); }
IDirect3DVolumeTexture9* FacadeWrap(IDirect3DVolumeTexture9* r)
{
    return WrapAs<Facade_VolumeTexture>(rhi::d3d9::Adopt(r));
}
IDirect3DCubeTexture9* FacadeWrap(IDirect3DCubeTexture9* r) { return WrapAs<Facade_CubeTexture>(rhi::d3d9::Adopt(r)); }
IDirect3DVertexBuffer9* FacadeWrap(IDirect3DVertexBuffer9* r) { return WrapAs<Facade_VertexBuffer>(rhi::d3d9::Adopt(r)); }
IDirect3DIndexBuffer9* FacadeWrap(IDirect3DIndexBuffer9* r) { return WrapAs<Facade_IndexBuffer>(rhi::d3d9::Adopt(r)); }
IDirect3DSurface9* FacadeWrap(IDirect3DSurface9* r) { return WrapAs<Facade_Surface>(rhi::d3d9::Adopt(r)); }
IDirect3DVolume9* FacadeWrap(IDirect3DVolume9* r) { return WrapAs<Facade_Volume>(rhi::d3d9::Adopt(r)); }
IDirect3DQuery9* FacadeWrap(IDirect3DQuery9* r) { return WrapAs<Facade_Query>(rhi::d3d9::Adopt(r)); }
IDirect3DBaseTexture9* FacadeWrap(IDirect3DBaseTexture9* r)
{
    return WrapTexture(static_cast<rhi::Texture*>(rhi::d3d9::Adopt(r)));
}

template <class I>
HRESULT DeviceOf(I** out)
{
    if (!g_device)
        return D3DERR_INVALIDCALL;
    ++g_device->refs;
    g_device->obj->AddRef();
    *out = g_device;
    return S_OK;
}
}  // namespace

// --- routes: IDirect3D9 ---------------------------------------------------------------------------------------

#define ADAPTER static_cast<rhi::Adapter*>(self->obj)
UINT Route_IDirect3D9_GetAdapterModeCount(Facade_Direct3D* self, UINT a, D3DFORMAT f)
{
    return ADAPTER->GetAdapterModeCount(a, f);
}
HRESULT Route_IDirect3D9_EnumAdapterModes(Facade_Direct3D* self, UINT a, D3DFORMAT f, UINT m, D3DDISPLAYMODE* o)
{
    return ADAPTER->EnumAdapterModes(a, f, m, o);
}
HRESULT Route_IDirect3D9_GetAdapterDisplayMode(Facade_Direct3D* self, UINT a, D3DDISPLAYMODE* o)
{
    return ADAPTER->GetAdapterDisplayMode(a, o);
}
HRESULT Route_IDirect3D9_CheckDeviceFormat(Facade_Direct3D* self, UINT a, D3DDEVTYPE t, D3DFORMAT af, DWORD u,
                                           D3DRESOURCETYPE r, D3DFORMAT cf)
{
    return ADAPTER->CheckDeviceFormat(a, t, af, u, r, cf);
}
HRESULT Route_IDirect3D9_CheckDepthStencilMatch(Facade_Direct3D* self, UINT a, D3DDEVTYPE t, D3DFORMAT af,
                                                D3DFORMAT rf, D3DFORMAT df)
{
    return ADAPTER->CheckDepthStencilMatch(a, t, af, rf, df);
}
HRESULT Route_IDirect3D9_GetDeviceCaps(Facade_Direct3D* self, UINT a, D3DDEVTYPE t, D3DCAPS9* c)
{
    return ADAPTER->GetDeviceCaps(a, t, c);
}
HRESULT Route_IDirect3D9_CreateDevice(Facade_Direct3D* self, UINT a, D3DDEVTYPE t, HWND w, DWORD b,
                                      D3DPRESENT_PARAMETERS* p, IDirect3DDevice9** out)
{
    rhi::Device* d = nullptr;
    HRESULT hr = ADAPTER->CreateDevice(a, t, w, b, p, &d);
    *out = WrapAs<Facade_Device>(d);
    if (*out)
        g_device = static_cast<Facade_Device*>(*out);
    return hr;
}
#undef ADAPTER

// --- routes: IDirect3DDevice9 ---------------------------------------------------------------------------------

HRESULT Route_IDirect3DDevice9_TestCooperativeLevel(Facade_Device* self) { return Dev(self)->TestCooperativeLevel(); }
UINT Route_IDirect3DDevice9_GetAvailableTextureMem(Facade_Device* self) { return Dev(self)->GetAvailableTextureMem(); }
HRESULT Route_IDirect3DDevice9_EvictManagedResources(Facade_Device* self) { return Dev(self)->EvictManagedResources(); }
HRESULT Route_IDirect3DDevice9_GetDeviceCaps(Facade_Device* self, D3DCAPS9* c) { return Dev(self)->GetDeviceCaps(c); }
HRESULT Route_IDirect3DDevice9_Reset(Facade_Device* self, D3DPRESENT_PARAMETERS* p) { return Dev(self)->Reset(p); }
HRESULT Route_IDirect3DDevice9_Present(Facade_Device* self, const RECT* s, const RECT* d, HWND w, const RGNDATA* r)
{
    return Dev(self)->Present(s, d, w, r);
}
HRESULT Route_IDirect3DDevice9_GetBackBuffer(Facade_Device* self, UINT sc, UINT i, D3DBACKBUFFER_TYPE t,
                                             IDirect3DSurface9** out)
{
    rhi::Surface* s = nullptr;
    HRESULT hr = Dev(self)->GetBackBuffer(sc, i, t, &s);
    *out = WrapAs<Facade_Surface>(s);
    return hr;
}
void Route_IDirect3DDevice9_SetGammaRamp(Facade_Device* self, UINT sc, DWORD f, const D3DGAMMARAMP* r)
{
    Dev(self)->SetGammaRamp(sc, f, r);
}
HRESULT Route_IDirect3DDevice9_CreateTexture(Facade_Device* self, UINT w, UINT h, UINT l, DWORD u, D3DFORMAT f,
                                             D3DPOOL p, IDirect3DTexture9** out, HANDLE*)
{
    rhi::Texture* t = nullptr;
    HRESULT hr = Dev(self)->CreateTexture(w, h, l, u, f, p, &t);
    *out = WrapAs<Facade_Texture>(t);
    return hr;
}
HRESULT Route_IDirect3DDevice9_CreateCubeTexture(Facade_Device* self, UINT e, UINT l, DWORD u, D3DFORMAT f, D3DPOOL p,
                                                 IDirect3DCubeTexture9** out, HANDLE*)
{
    rhi::Texture* t = nullptr;
    HRESULT hr = Dev(self)->CreateCubeTexture(e, l, u, f, p, &t);
    *out = WrapAs<Facade_CubeTexture>(t);
    return hr;
}
HRESULT Route_IDirect3DDevice9_CreateVertexBuffer(Facade_Device* self, UINT len, DWORD u, DWORD fvf, D3DPOOL p,
                                                  IDirect3DVertexBuffer9** out, HANDLE*)
{
    rhi::Buffer* b = nullptr;
    HRESULT hr = Dev(self)->CreateVertexBuffer(len, u, fvf, p, &b);
    *out = WrapAs<Facade_VertexBuffer>(b);
    return hr;
}
HRESULT Route_IDirect3DDevice9_CreateIndexBuffer(Facade_Device* self, UINT len, DWORD u, D3DFORMAT f, D3DPOOL p,
                                                 IDirect3DIndexBuffer9** out, HANDLE*)
{
    rhi::Buffer* b = nullptr;
    HRESULT hr = Dev(self)->CreateIndexBuffer(len, u, f, p, &b);
    *out = WrapAs<Facade_IndexBuffer>(b);
    return hr;
}
HRESULT Route_IDirect3DDevice9_CreateOffscreenPlainSurface(Facade_Device* self, UINT w, UINT h, D3DFORMAT f, D3DPOOL p,
                                                           IDirect3DSurface9** out, HANDLE*)
{
    rhi::Surface* s = nullptr;
    HRESULT hr = Dev(self)->CreateOffscreenPlainSurface(w, h, f, p, &s);
    *out = WrapAs<Facade_Surface>(s);
    return hr;
}
HRESULT Route_IDirect3DDevice9_StretchRect(Facade_Device* self, IDirect3DSurface9* s, const RECT* sr,
                                           IDirect3DSurface9* d, const RECT* dr, D3DTEXTUREFILTERTYPE f)
{
    return Dev(self)->StretchRect(Obj<rhi::Surface>(s), sr, Obj<rhi::Surface>(d), dr, f);
}
HRESULT Route_IDirect3DDevice9_SetRenderTarget(Facade_Device* self, DWORD i, IDirect3DSurface9* s)
{
    return Dev(self)->SetRenderTarget(i, Obj<rhi::Surface>(s));
}
HRESULT Route_IDirect3DDevice9_SetDepthStencilSurface(Facade_Device* self, IDirect3DSurface9* s)
{
    return Dev(self)->SetDepthStencilSurface(Obj<rhi::Surface>(s));
}
HRESULT Route_IDirect3DDevice9_GetDepthStencilSurface(Facade_Device* self, IDirect3DSurface9** out)
{
    rhi::Surface* s = nullptr;
    HRESULT hr = Dev(self)->GetDepthStencilSurface(&s);
    *out = WrapAs<Facade_Surface>(s);
    return hr;
}
HRESULT Route_IDirect3DDevice9_BeginScene(Facade_Device* self) { return Dev(self)->BeginScene(); }
HRESULT Route_IDirect3DDevice9_EndScene(Facade_Device* self) { return Dev(self)->EndScene(); }
HRESULT Route_IDirect3DDevice9_Clear(Facade_Device* self, DWORD n, const D3DRECT* r, DWORD f, D3DCOLOR c, float z,
                                     DWORD s)
{
    return Dev(self)->Clear(n, r, f, c, z, s);
}
HRESULT Route_IDirect3DDevice9_SetTransform(Facade_Device* self, D3DTRANSFORMSTATETYPE s, const D3DMATRIX* m)
{
    return Dev(self)->SetTransform(s, m);
}
HRESULT Route_IDirect3DDevice9_SetViewport(Facade_Device* self, const D3DVIEWPORT9* v) { return Dev(self)->SetViewport(v); }
HRESULT Route_IDirect3DDevice9_SetMaterial(Facade_Device* self, const D3DMATERIAL9* m) { return Dev(self)->SetMaterial(m); }
HRESULT Route_IDirect3DDevice9_SetLight(Facade_Device* self, DWORD i, const D3DLIGHT9* l)
{
    return Dev(self)->SetLight(i, l);
}
HRESULT Route_IDirect3DDevice9_LightEnable(Facade_Device* self, DWORD i, BOOL e) { return Dev(self)->LightEnable(i, e); }
HRESULT Route_IDirect3DDevice9_SetRenderState(Facade_Device* self, D3DRENDERSTATETYPE s, DWORD v)
{
    return Dev(self)->SetRenderState(s, v);
}
HRESULT Route_IDirect3DDevice9_SetTexture(Facade_Device* self, DWORD st, IDirect3DBaseTexture9* t)
{
    return Dev(self)->SetTexture(st, Obj<rhi::Texture>(t));
}
HRESULT Route_IDirect3DDevice9_SetTextureStageState(Facade_Device* self, DWORD st, D3DTEXTURESTAGESTATETYPE t, DWORD v)
{
    return Dev(self)->SetTextureStageState(st, t, v);
}
HRESULT Route_IDirect3DDevice9_SetSamplerState(Facade_Device* self, DWORD s, D3DSAMPLERSTATETYPE t, DWORD v)
{
    return Dev(self)->SetSamplerState(s, t, v);
}
HRESULT Route_IDirect3DDevice9_DrawPrimitive(Facade_Device* self, D3DPRIMITIVETYPE t, UINT s, UINT c)
{
    return Dev(self)->DrawPrimitive(t, s, c);
}
HRESULT Route_IDirect3DDevice9_DrawIndexedPrimitive(Facade_Device* self, D3DPRIMITIVETYPE t, INT b, UINT mi, UINT nv,
                                                    UINT si, UINT c)
{
    return Dev(self)->DrawIndexedPrimitive(t, b, mi, nv, si, c);
}
HRESULT Route_IDirect3DDevice9_CreateVertexDeclaration(Facade_Device* self, const D3DVERTEXELEMENT9* e,
                                                       IDirect3DVertexDeclaration9** out)
{
    rhi::VertexLayout* l = nullptr;
    HRESULT hr = Dev(self)->CreateVertexLayout(e, &l);
    *out = WrapAs<Facade_VertexDeclaration>(l);
    return hr;
}
HRESULT Route_IDirect3DDevice9_SetVertexDeclaration(Facade_Device* self, IDirect3DVertexDeclaration9* d)
{
    return Dev(self)->SetVertexLayout(Obj<rhi::VertexLayout>(d));
}
HRESULT Route_IDirect3DDevice9_CreateVertexShader(Facade_Device* self, const DWORD* code, IDirect3DVertexShader9** out)
{
    rhi::VertexShader* s = nullptr;
    HRESULT hr = Dev(self)->CreateVertexShader(code, &s);
    *out = WrapAs<Facade_VertexShader>(s);
    return hr;
}
HRESULT Route_IDirect3DDevice9_SetVertexShader(Facade_Device* self, IDirect3DVertexShader9* s)
{
    return Dev(self)->SetVertexShader(Obj<rhi::VertexShader>(s));
}
HRESULT Route_IDirect3DDevice9_SetVertexShaderConstantF(Facade_Device* self, UINT s, const float* d, UINT c)
{
    return Dev(self)->SetVertexShaderConstantF(s, d, c);
}
HRESULT Route_IDirect3DDevice9_SetStreamSource(Facade_Device* self, UINT s, IDirect3DVertexBuffer9* b, UINT o, UINT st)
{
    return Dev(self)->SetStreamSource(s, Obj<rhi::Buffer>(b), o, st);
}
HRESULT Route_IDirect3DDevice9_SetIndices(Facade_Device* self, IDirect3DIndexBuffer9* b)
{
    return Dev(self)->SetIndices(Obj<rhi::Buffer>(b));
}
HRESULT Route_IDirect3DDevice9_CreatePixelShader(Facade_Device* self, const DWORD* code, IDirect3DPixelShader9** out)
{
    rhi::PixelShader* s = nullptr;
    HRESULT hr = Dev(self)->CreatePixelShader(code, &s);
    *out = WrapAs<Facade_PixelShader>(s);
    return hr;
}
HRESULT Route_IDirect3DDevice9_SetPixelShader(Facade_Device* self, IDirect3DPixelShader9* s)
{
    return Dev(self)->SetPixelShader(Obj<rhi::PixelShader>(s));
}
HRESULT Route_IDirect3DDevice9_SetPixelShaderConstantF(Facade_Device* self, UINT s, const float* d, UINT c)
{
    return Dev(self)->SetPixelShaderConstantF(s, d, c);
}

// --- routes: textures, surfaces, buffers ----------------------------------------------------------------------

#define TEX static_cast<rhi::Texture*>(self->obj)
HRESULT Route_IDirect3DTexture9_GetDevice(Facade_Texture*, IDirect3DDevice9** out) { return DeviceOf(out); }
DWORD Route_IDirect3DTexture9_GetLOD(Facade_Texture* self) { return TEX->GetLOD(); }
DWORD Route_IDirect3DTexture9_SetLOD(Facade_Texture* self, DWORD l) { return TEX->SetLOD(l); }
DWORD Route_IDirect3DTexture9_GetLevelCount(Facade_Texture* self) { return TEX->GetLevelCount(); }
HRESULT Route_IDirect3DTexture9_GetLevelDesc(Facade_Texture* self, UINT l, D3DSURFACE_DESC* d)
{
    return TEX->GetLevelDesc(l, d);
}
HRESULT Route_IDirect3DTexture9_GetSurfaceLevel(Facade_Texture* self, UINT l, IDirect3DSurface9** out)
{
    rhi::Surface* s = nullptr;
    HRESULT hr = TEX->GetSurfaceLevel(l, &s);
    *out = WrapAs<Facade_Surface>(s);
    return hr;
}
HRESULT Route_IDirect3DCubeTexture9_GetDevice(Facade_CubeTexture*, IDirect3DDevice9** out) { return DeviceOf(out); }
DWORD Route_IDirect3DCubeTexture9_GetLOD(Facade_CubeTexture* self) { return TEX->GetLOD(); }
DWORD Route_IDirect3DCubeTexture9_SetLOD(Facade_CubeTexture* self, DWORD l) { return TEX->SetLOD(l); }
DWORD Route_IDirect3DCubeTexture9_GetLevelCount(Facade_CubeTexture* self) { return TEX->GetLevelCount(); }
HRESULT Route_IDirect3DCubeTexture9_GetLevelDesc(Facade_CubeTexture* self, UINT l, D3DSURFACE_DESC* d)
{
    return TEX->GetLevelDesc(l, d);
}
HRESULT Route_IDirect3DCubeTexture9_GetCubeMapSurface(Facade_CubeTexture* self, D3DCUBEMAP_FACES f, UINT l,
                                                      IDirect3DSurface9** out)
{
    rhi::Surface* s = nullptr;
    HRESULT hr = TEX->GetCubeMapSurface(f, l, &s);
    *out = WrapAs<Facade_Surface>(s);
    return hr;
}
#undef TEX

#define SURF static_cast<rhi::Surface*>(self->obj)
HRESULT Route_IDirect3DSurface9_GetDevice(Facade_Surface*, IDirect3DDevice9** out) { return DeviceOf(out); }
HRESULT Route_IDirect3DSurface9_GetDesc(Facade_Surface* self, D3DSURFACE_DESC* d) { return SURF->GetDesc(d); }
HRESULT Route_IDirect3DSurface9_LockRect(Facade_Surface* self, D3DLOCKED_RECT* l, const RECT* r, DWORD f)
{
    return SURF->LockRect(l, r, f);
}
HRESULT Route_IDirect3DSurface9_UnlockRect(Facade_Surface* self) { return SURF->UnlockRect(); }
#undef SURF

HRESULT Route_IDirect3DVertexBuffer9_GetDevice(Facade_VertexBuffer*, IDirect3DDevice9** out) { return DeviceOf(out); }
HRESULT Route_IDirect3DVertexBuffer9_Lock(Facade_VertexBuffer* self, UINT o, UINT s, void** d, DWORD f)
{
    return static_cast<rhi::Buffer*>(self->obj)->Lock(o, s, d, f);
}
HRESULT Route_IDirect3DVertexBuffer9_Unlock(Facade_VertexBuffer* self)
{
    return static_cast<rhi::Buffer*>(self->obj)->Unlock();
}
HRESULT Route_IDirect3DIndexBuffer9_GetDevice(Facade_IndexBuffer*, IDirect3DDevice9** out) { return DeviceOf(out); }
HRESULT Route_IDirect3DIndexBuffer9_Lock(Facade_IndexBuffer* self, UINT o, UINT s, void** d, DWORD f)
{
    return static_cast<rhi::Buffer*>(self->obj)->Lock(o, s, d, f);
}
HRESULT Route_IDirect3DIndexBuffer9_Unlock(Facade_IndexBuffer* self) { return static_cast<rhi::Buffer*>(self->obj)->Unlock(); }

// --- installation and diagnostics -----------------------------------------------------------------------------

// Called by the Direct3DCreate9 hook (hook/src/d3d9_hooks.cpp) when [render] rhi=1.
IDirect3D9* RhiFacadeWrapDirect3D(IDirect3D9* real)
{
    Log("rhi: Direct3D 9 facade installed (%d methods), backend d3d9", kRhiMethodCount);
    return WrapAs<Facade_Direct3D>(rhi::d3d9::CreateAdapter(real));
}

// [test] rhi_calls=1: per-method call counts to and-rhi-calls.txt (which D3D9 methods the game really uses).
void RhiWriteCalls()
{
    if (!ConfigInt("test", "rhi_calls", 0))
        return;
    FILE* f = fopen("and-rhi-calls.txt", "w");
    if (!f)
        return;
    for (int k = 0; k < kRhiMethodCount; ++k)
        fprintf(f, "%ld %s\n", g_calls[k], kRhiMethodNames[k]);
    fclose(f);
}
