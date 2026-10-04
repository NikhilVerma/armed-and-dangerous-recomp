// rhi::d3d9: the renderer interface implemented by the real Direct3D 9. Every call forwards one to one, so the
// Direct3D call stream (and every frame) stays identical to the original game: tools/compare.py --rhi.
//
// A real object maps to one backend object while references obtained through the interface are outstanding;
// the backend object counts those and is deleted when they are released, even if D3D9 itself still holds the
// real object (a texture bound to a stage), so an object later created at the same address starts clean.
#include <mutex>
#include <unordered_map>

#include "rhi/rhi.h"

namespace rhi::d3d9
{
namespace
{
std::mutex g_lock;
std::unordered_map<IUnknown*, Object*> g_objects;

template <class Base, class NativeT>
class Wrapped : public Base
{
public:
    explicit Wrapped(NativeT* n) : n_(n) {}
    ULONG AddRef() override
    {
        ++refs_;
        return n_->AddRef();
    }
    ULONG Release() override
    {
        ULONG r = n_->Release();
        if (--refs_ == 0)
        {
            {
                std::lock_guard<std::mutex> g(g_lock);
                g_objects.erase(n_);
            }
            delete this;
        }
        return r;
    }
    void* Native() override { return n_; }
    NativeT* n() const { return n_; }
    void Ref() { ++refs_; }

protected:
    NativeT* n_;
    ULONG refs_ = 1;
};

template <class W, class N>
W* Get(N* n)
{
    if (!n)
        return nullptr;
    std::lock_guard<std::mutex> g(g_lock);
    auto it = g_objects.find(n);
    if (it != g_objects.end())
    {
        auto* w = static_cast<W*>(it->second);
        w->Ref();  // the D3D9 call that returned it added a reference to the real object
        return w;
    }
    auto* w = new W(n);
    g_objects[n] = w;
    return w;
}

template <class N>
N* Real(Object* o)
{
    return o ? static_cast<N*>(o->Native()) : nullptr;
}

class D3D9Surface final : public Wrapped<Surface, IDirect3DSurface9>
{
public:
    using Wrapped::Wrapped;
    HRESULT GetDesc(D3DSURFACE_DESC* d) override { return n_->GetDesc(d); }
    HRESULT LockRect(D3DLOCKED_RECT* l, const RECT* r, DWORD f) override { return n_->LockRect(l, r, f); }
    HRESULT UnlockRect() override { return n_->UnlockRect(); }
};

class D3D9Texture final : public Wrapped<Texture, IDirect3DBaseTexture9>
{
public:
    using Wrapped::Wrapped;
    D3DRESOURCETYPE Type() override { return n_->GetType(); }
    DWORD GetLevelCount() override { return n_->GetLevelCount(); }
    DWORD GetLOD() override { return n_->GetLOD(); }
    DWORD SetLOD(DWORD lod) override { return n_->SetLOD(lod); }
    HRESULT GetLevelDesc(UINT level, D3DSURFACE_DESC* d) override
    {
        return Type() == D3DRTYPE_CUBETEXTURE ? Cube()->GetLevelDesc(level, d) : Tex()->GetLevelDesc(level, d);
    }
    HRESULT GetSurfaceLevel(UINT level, Surface** out) override
    {
        IDirect3DSurface9* s = nullptr;
        HRESULT hr = Tex()->GetSurfaceLevel(level, &s);
        *out = Get<D3D9Surface>(s);
        return hr;
    }
    HRESULT GetCubeMapSurface(D3DCUBEMAP_FACES face, UINT level, Surface** out) override
    {
        IDirect3DSurface9* s = nullptr;
        HRESULT hr = Cube()->GetCubeMapSurface(face, level, &s);
        *out = Get<D3D9Surface>(s);
        return hr;
    }

private:
    IDirect3DTexture9* Tex() { return static_cast<IDirect3DTexture9*>(n_); }
    IDirect3DCubeTexture9* Cube() { return static_cast<IDirect3DCubeTexture9*>(n_); }
};

class D3D9VertexBuffer final : public Wrapped<Buffer, IDirect3DVertexBuffer9>
{
public:
    using Wrapped::Wrapped;
    HRESULT Lock(UINT o, UINT s, void** d, DWORD f) override { return n_->Lock(o, s, d, f); }
    HRESULT Unlock() override { return n_->Unlock(); }
};

class D3D9IndexBuffer final : public Wrapped<Buffer, IDirect3DIndexBuffer9>
{
public:
    using Wrapped::Wrapped;
    HRESULT Lock(UINT o, UINT s, void** d, DWORD f) override { return n_->Lock(o, s, d, f); }
    HRESULT Unlock() override { return n_->Unlock(); }
};

class D3D9VertexLayout final : public Wrapped<VertexLayout, IDirect3DVertexDeclaration9>
{
public:
    using Wrapped::Wrapped;
};
class D3D9VertexShader final : public Wrapped<VertexShader, IDirect3DVertexShader9>
{
public:
    using Wrapped::Wrapped;
};
class D3D9PixelShader final : public Wrapped<PixelShader, IDirect3DPixelShader9>
{
public:
    using Wrapped::Wrapped;
};

class D3D9Device final : public Wrapped<Device, IDirect3DDevice9>
{
public:
    using Wrapped::Wrapped;
    HRESULT TestCooperativeLevel() override { return n_->TestCooperativeLevel(); }
    UINT GetAvailableTextureMem() override { return n_->GetAvailableTextureMem(); }
    HRESULT EvictManagedResources() override { return n_->EvictManagedResources(); }
    HRESULT GetDeviceCaps(D3DCAPS9* c) override { return n_->GetDeviceCaps(c); }
    HRESULT Reset(D3DPRESENT_PARAMETERS* p) override { return n_->Reset(p); }
    HRESULT Present(const RECT* s, const RECT* d, HWND w, const RGNDATA* r) override { return n_->Present(s, d, w, r); }
    HRESULT GetBackBuffer(UINT sc, UINT i, D3DBACKBUFFER_TYPE t, Surface** out) override
    {
        IDirect3DSurface9* s = nullptr;
        HRESULT hr = n_->GetBackBuffer(sc, i, t, &s);
        *out = Get<D3D9Surface>(s);
        return hr;
    }
    void SetGammaRamp(UINT sc, DWORD f, const D3DGAMMARAMP* r) override { n_->SetGammaRamp(sc, f, r); }
    HRESULT BeginScene() override { return n_->BeginScene(); }
    HRESULT EndScene() override { return n_->EndScene(); }
    HRESULT Clear(DWORD n, const D3DRECT* r, DWORD f, D3DCOLOR c, float z, DWORD s) override
    {
        return n_->Clear(n, r, f, c, z, s);
    }

    HRESULT CreateTexture(UINT w, UINT h, UINT l, DWORD u, D3DFORMAT f, D3DPOOL p, Texture** out) override
    {
        IDirect3DTexture9* t = nullptr;
        HRESULT hr = n_->CreateTexture(w, h, l, u, f, p, &t, nullptr);
        *out = Get<D3D9Texture>(static_cast<IDirect3DBaseTexture9*>(t));
        return hr;
    }
    HRESULT CreateCubeTexture(UINT e, UINT l, DWORD u, D3DFORMAT f, D3DPOOL p, Texture** out) override
    {
        IDirect3DCubeTexture9* t = nullptr;
        HRESULT hr = n_->CreateCubeTexture(e, l, u, f, p, &t, nullptr);
        *out = Get<D3D9Texture>(static_cast<IDirect3DBaseTexture9*>(t));
        return hr;
    }
    HRESULT CreateVertexBuffer(UINT len, DWORD u, DWORD fvf, D3DPOOL p, Buffer** out) override
    {
        IDirect3DVertexBuffer9* b = nullptr;
        HRESULT hr = n_->CreateVertexBuffer(len, u, fvf, p, &b, nullptr);
        *out = Get<D3D9VertexBuffer>(b);
        return hr;
    }
    HRESULT CreateIndexBuffer(UINT len, DWORD u, D3DFORMAT f, D3DPOOL p, Buffer** out) override
    {
        IDirect3DIndexBuffer9* b = nullptr;
        HRESULT hr = n_->CreateIndexBuffer(len, u, f, p, &b, nullptr);
        *out = Get<D3D9IndexBuffer>(b);
        return hr;
    }
    HRESULT CreateOffscreenPlainSurface(UINT w, UINT h, D3DFORMAT f, D3DPOOL p, Surface** out) override
    {
        IDirect3DSurface9* s = nullptr;
        HRESULT hr = n_->CreateOffscreenPlainSurface(w, h, f, p, &s, nullptr);
        *out = Get<D3D9Surface>(s);
        return hr;
    }
    HRESULT CreateVertexLayout(const D3DVERTEXELEMENT9* e, VertexLayout** out) override
    {
        IDirect3DVertexDeclaration9* d = nullptr;
        HRESULT hr = n_->CreateVertexDeclaration(e, &d);
        *out = Get<D3D9VertexLayout>(d);
        return hr;
    }
    HRESULT CreateVertexShader(const DWORD* code, VertexShader** out) override
    {
        IDirect3DVertexShader9* s = nullptr;
        HRESULT hr = n_->CreateVertexShader(code, &s);
        *out = Get<D3D9VertexShader>(s);
        return hr;
    }
    HRESULT CreatePixelShader(const DWORD* code, PixelShader** out) override
    {
        IDirect3DPixelShader9* s = nullptr;
        HRESULT hr = n_->CreatePixelShader(code, &s);
        *out = Get<D3D9PixelShader>(s);
        return hr;
    }
    HRESULT StretchRect(Surface* s, const RECT* sr, Surface* d, const RECT* dr, D3DTEXTUREFILTERTYPE f) override
    {
        return n_->StretchRect(Real<IDirect3DSurface9>(s), sr, Real<IDirect3DSurface9>(d), dr, f);
    }

    HRESULT SetRenderTarget(DWORD i, Surface* s) override { return n_->SetRenderTarget(i, Real<IDirect3DSurface9>(s)); }
    HRESULT SetDepthStencilSurface(Surface* s) override
    {
        return n_->SetDepthStencilSurface(Real<IDirect3DSurface9>(s));
    }
    HRESULT GetDepthStencilSurface(Surface** out) override
    {
        IDirect3DSurface9* s = nullptr;
        HRESULT hr = n_->GetDepthStencilSurface(&s);
        *out = Get<D3D9Surface>(s);
        return hr;
    }
    HRESULT SetViewport(const D3DVIEWPORT9* v) override { return n_->SetViewport(v); }

    HRESULT SetTransform(D3DTRANSFORMSTATETYPE s, const D3DMATRIX* m) override { return n_->SetTransform(s, m); }
    HRESULT SetMaterial(const D3DMATERIAL9* m) override { return n_->SetMaterial(m); }
    HRESULT SetLight(DWORD i, const D3DLIGHT9* l) override { return n_->SetLight(i, l); }
    HRESULT LightEnable(DWORD i, BOOL e) override { return n_->LightEnable(i, e); }
    HRESULT SetRenderState(D3DRENDERSTATETYPE s, DWORD v) override { return n_->SetRenderState(s, v); }
    HRESULT SetTextureStageState(DWORD st, D3DTEXTURESTAGESTATETYPE t, DWORD v) override
    {
        return n_->SetTextureStageState(st, t, v);
    }
    HRESULT SetSamplerState(DWORD s, D3DSAMPLERSTATETYPE t, DWORD v) override { return n_->SetSamplerState(s, t, v); }
    HRESULT SetTexture(DWORD st, Texture* t) override { return n_->SetTexture(st, Real<IDirect3DBaseTexture9>(t)); }

    HRESULT SetVertexLayout(VertexLayout* l) override
    {
        return n_->SetVertexDeclaration(Real<IDirect3DVertexDeclaration9>(l));
    }
    HRESULT SetVertexShader(VertexShader* s) override { return n_->SetVertexShader(Real<IDirect3DVertexShader9>(s)); }
    HRESULT SetVertexShaderConstantF(UINT s, const float* d, UINT c) override
    {
        return n_->SetVertexShaderConstantF(s, d, c);
    }
    HRESULT SetPixelShader(PixelShader* s) override { return n_->SetPixelShader(Real<IDirect3DPixelShader9>(s)); }
    HRESULT SetPixelShaderConstantF(UINT s, const float* d, UINT c) override
    {
        return n_->SetPixelShaderConstantF(s, d, c);
    }

    HRESULT SetStreamSource(UINT s, Buffer* b, UINT o, UINT st) override
    {
        return n_->SetStreamSource(s, Real<IDirect3DVertexBuffer9>(b), o, st);
    }
    HRESULT SetIndices(Buffer* b) override { return n_->SetIndices(Real<IDirect3DIndexBuffer9>(b)); }
    HRESULT DrawPrimitive(D3DPRIMITIVETYPE t, UINT s, UINT c) override { return n_->DrawPrimitive(t, s, c); }
    HRESULT DrawIndexedPrimitive(D3DPRIMITIVETYPE t, INT b, UINT mi, UINT nv, UINT si, UINT c) override
    {
        return n_->DrawIndexedPrimitive(t, b, mi, nv, si, c);
    }
};

class D3D9Adapter final : public Wrapped<Adapter, IDirect3D9>
{
public:
    using Wrapped::Wrapped;
    UINT GetAdapterModeCount(UINT a, D3DFORMAT f) override { return n_->GetAdapterModeCount(a, f); }
    HRESULT EnumAdapterModes(UINT a, D3DFORMAT f, UINT m, D3DDISPLAYMODE* o) override
    {
        return n_->EnumAdapterModes(a, f, m, o);
    }
    HRESULT GetAdapterDisplayMode(UINT a, D3DDISPLAYMODE* o) override { return n_->GetAdapterDisplayMode(a, o); }
    HRESULT CheckDeviceFormat(UINT a, D3DDEVTYPE t, D3DFORMAT af, DWORD u, D3DRESOURCETYPE r, D3DFORMAT cf) override
    {
        return n_->CheckDeviceFormat(a, t, af, u, r, cf);
    }
    HRESULT CheckDepthStencilMatch(UINT a, D3DDEVTYPE t, D3DFORMAT af, D3DFORMAT rf, D3DFORMAT df) override
    {
        return n_->CheckDepthStencilMatch(a, t, af, rf, df);
    }
    HRESULT GetDeviceCaps(UINT a, D3DDEVTYPE t, D3DCAPS9* c) override { return n_->GetDeviceCaps(a, t, c); }
    HRESULT CreateDevice(UINT a, D3DDEVTYPE t, HWND w, DWORD b, D3DPRESENT_PARAMETERS* p, Device** out) override
    {
        IDirect3DDevice9* d = nullptr;
        HRESULT hr = n_->CreateDevice(a, t, w, b, p, &d);
        *out = Get<D3D9Device>(d);
        return hr;
    }
};
}  // namespace

Adapter* CreateAdapter(IDirect3D9* real)
{
    return Get<D3D9Adapter>(real);
}

namespace
{
class D3D9Other final : public Wrapped<Object, IUnknown>
{
public:
    using Wrapped::Wrapped;
};
}  // namespace

Object* Adopt(IUnknown* real)
{
    if (!real)
        return nullptr;
    IUnknown* p = nullptr;
    // Identify the interface to pick the backend class; QueryInterface adds a reference we hand on.
    if (SUCCEEDED(real->QueryInterface(__uuidof(IDirect3DSurface9), reinterpret_cast<void**>(&p))))
    {
        real->Release();
        return Get<D3D9Surface>(static_cast<IDirect3DSurface9*>(p));
    }
    if (SUCCEEDED(real->QueryInterface(__uuidof(IDirect3DBaseTexture9), reinterpret_cast<void**>(&p))))
    {
        real->Release();
        return Get<D3D9Texture>(static_cast<IDirect3DBaseTexture9*>(p));
    }
    return Get<D3D9Other>(real);  // anything else: identity and reference counting only
}
}  // namespace rhi::d3d9
