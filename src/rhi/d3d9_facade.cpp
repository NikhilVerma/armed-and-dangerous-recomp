// Direct3D 9 COM facade: the boundary of the renderer interface (docs/renderer-interface.md).
//
// Every D3D9 object the game sees (recovered renderer code and the statically linked D3DX alike) is one of the
// generated Facade_* classes (d3d9_facade.gen.inl). Each method either goes to the renderer interface (a
// Route_<Interface>_<Method> defined below with ROUTE_<Interface>_<Method>) or still forwards to the real D3D9
// object. [render] rhi=1 installs it; with the D3D9 backend the game must stay bit-identical (tools/compare.py).
//
// Wrapping rules: an object returned by a call is wrapped (the same real object always maps to the same wrapper
// while the game holds references through it); objects passed in are unwrapped. A wrapper counts the references
// the game obtained through it and is deleted when they are all released, even if D3D9 itself still holds the
// real object (a texture bound to a stage), so a later object at the same address never meets a stale wrapper.
#include <windows.h>
#include <d3d9.h>

#include <cstdio>
#include <mutex>
#include <unordered_map>

#include "config.h"
#include "log.h"

namespace
{
volatile long g_calls[400];
std::mutex g_lock;
struct Entry
{
    void* wrapper;
    ULONG* refs;
};
std::unordered_map<void*, Entry> g_byReal;
std::unordered_map<void*, void*> g_byWrapper;

void RhiCount(int index)
{
    InterlockedIncrement(&g_calls[index]);
}

template <class R>
R* FacadeUnwrap(R* p)
{
    if (!p)
        return p;
    std::lock_guard<std::mutex> g(g_lock);
    auto it = g_byWrapper.find(p);
    return it == g_byWrapper.end() ? p : static_cast<R*>(it->second);
}

// Declared here, defined after the generated classes (they are needed complete to construct wrappers).
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

template <class W, class R>
void FacadeForget(W* wrapper, R* real)
{
    {
        std::lock_guard<std::mutex> g(g_lock);
        g_byReal.erase(real);
        g_byWrapper.erase(static_cast<void*>(wrapper));
    }
    delete wrapper;
}

// Same object (D3D9 answers with the real pointer itself): hand out the wrapper; otherwise the raw answer.
template <class W, class R>
HRESULT FacadeQueryInterface(W* self, R* real, REFIID riid, void** out)
{
    void* p = nullptr;
    HRESULT hr = real->QueryInterface(riid, &p);
    if (SUCCEEDED(hr) && p == static_cast<void*>(real))
    {
        ++self->refs;
        *out = self;
        return hr;
    }
    if (SUCCEEDED(hr))
        Log("rhi: QueryInterface returned a different object (not wrapped)");
    *out = p;
    return hr;
}
}  // namespace

#include "d3d9_facade.gen.inl"

namespace
{
template <class W, class R>
R* WrapAs(R* r)
{
    if (!r)
        return r;
    std::lock_guard<std::mutex> g(g_lock);
    auto it = g_byReal.find(r);
    if (it != g_byReal.end())
    {
        ++*it->second.refs;  // the call that returned it added a reference to the real object
        return static_cast<R*>(it->second.wrapper);
    }
    W* w = new W(r);
    R* iface = w;
    g_byReal[r] = {iface, &w->refs};
    g_byWrapper[iface] = r;
    return iface;
}

IDirect3D9* FacadeWrap(IDirect3D9* r) { return WrapAs<Facade_Direct3D>(r); }
IDirect3DDevice9* FacadeWrap(IDirect3DDevice9* r) { return WrapAs<Facade_Device>(r); }
IDirect3DStateBlock9* FacadeWrap(IDirect3DStateBlock9* r) { return WrapAs<Facade_StateBlock>(r); }
IDirect3DSwapChain9* FacadeWrap(IDirect3DSwapChain9* r) { return WrapAs<Facade_SwapChain>(r); }
IDirect3DVertexDeclaration9* FacadeWrap(IDirect3DVertexDeclaration9* r) { return WrapAs<Facade_VertexDeclaration>(r); }
IDirect3DVertexShader9* FacadeWrap(IDirect3DVertexShader9* r) { return WrapAs<Facade_VertexShader>(r); }
IDirect3DPixelShader9* FacadeWrap(IDirect3DPixelShader9* r) { return WrapAs<Facade_PixelShader>(r); }
IDirect3DTexture9* FacadeWrap(IDirect3DTexture9* r) { return WrapAs<Facade_Texture>(r); }
IDirect3DVolumeTexture9* FacadeWrap(IDirect3DVolumeTexture9* r) { return WrapAs<Facade_VolumeTexture>(r); }
IDirect3DCubeTexture9* FacadeWrap(IDirect3DCubeTexture9* r) { return WrapAs<Facade_CubeTexture>(r); }
IDirect3DVertexBuffer9* FacadeWrap(IDirect3DVertexBuffer9* r) { return WrapAs<Facade_VertexBuffer>(r); }
IDirect3DIndexBuffer9* FacadeWrap(IDirect3DIndexBuffer9* r) { return WrapAs<Facade_IndexBuffer>(r); }
IDirect3DSurface9* FacadeWrap(IDirect3DSurface9* r) { return WrapAs<Facade_Surface>(r); }
IDirect3DVolume9* FacadeWrap(IDirect3DVolume9* r) { return WrapAs<Facade_Volume>(r); }
IDirect3DQuery9* FacadeWrap(IDirect3DQuery9* r) { return WrapAs<Facade_Query>(r); }

// GetTexture and friends return the base interface: wrap by the object's real type.
IDirect3DBaseTexture9* FacadeWrap(IDirect3DBaseTexture9* r)
{
    if (!r)
        return r;
    switch (r->GetType())
    {
    case D3DRTYPE_TEXTURE: return FacadeWrap(static_cast<IDirect3DTexture9*>(r));
    case D3DRTYPE_CUBETEXTURE: return FacadeWrap(static_cast<IDirect3DCubeTexture9*>(r));
    case D3DRTYPE_VOLUMETEXTURE: return FacadeWrap(static_cast<IDirect3DVolumeTexture9*>(r));
    default: return r;
    }
}
}  // namespace

// Called by the Direct3DCreate9 hook (hook/src/d3d9_hooks.cpp) when [render] rhi=1.
IDirect3D9* RhiFacadeWrapDirect3D(IDirect3D9* real)
{
    Log("rhi: Direct3D 9 facade installed (%d methods)", kRhiMethodCount);
    return FacadeWrap(real);
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
