"""Generate the Direct3D 9 COM facade (src/rhi/d3d9_facade.gen.inl) from the Windows SDK's d3d9.h.

Usage: python tools/gen_d3d9_facade.py [--d3d9h path]

The facade is the boundary of the renderer interface (docs/renderer-interface.md). Every D3D9 object that the
game (recovered code and the statically linked D3DX) sees is one of our classes. A method either goes to the
rhi::Device backend (hand-written in src/rhi/d3d9_facade.cpp, listed in ROUTED) or, until it is routed, forwards
to the real D3D9 object and is counted as "not yet routed". Objects returned by a call are wrapped; objects passed
in are unwrapped. A real object always maps to the same wrapper while the game holds a reference through it.

Output (generated, committed so builds need no SDK parse): one class per interface with every method, plus a
table of method names for the call counter ([test] rhi_calls=1 -> and-rhi-calls.txt).
"""
import argparse
import glob
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "src", "rhi", "d3d9_facade.gen.inl")

# Concrete interfaces we wrap (abstract bases IDirect3DResource9 / IDirect3DBaseTexture9 are reached through these).
WRAPPED = ["IDirect3D9", "IDirect3DDevice9", "IDirect3DStateBlock9", "IDirect3DSwapChain9",
           "IDirect3DVertexDeclaration9", "IDirect3DVertexShader9", "IDirect3DPixelShader9", "IDirect3DTexture9",
           "IDirect3DVolumeTexture9", "IDirect3DCubeTexture9", "IDirect3DVertexBuffer9", "IDirect3DIndexBuffer9",
           "IDirect3DSurface9", "IDirect3DVolume9", "IDirect3DQuery9"]
COM_TYPES = set(WRAPPED) | {"IDirect3DBaseTexture9", "IDirect3DResource9"}

# Methods that go through the renderer interface (src/rhi/rhi.h). Everything the game uses (measured with
# [test] rhi_calls=1 over all comparison scenarios) plus what unexercised recovered code calls.
ROUTED = {
    "IDirect3D9": ["GetAdapterModeCount", "EnumAdapterModes", "GetAdapterDisplayMode", "CheckDeviceFormat",
                   "CheckDepthStencilMatch", "GetDeviceCaps", "CreateDevice"],
    "IDirect3DDevice9": ["TestCooperativeLevel", "GetAvailableTextureMem", "EvictManagedResources", "GetDeviceCaps",
                         "Reset", "Present", "GetBackBuffer", "SetGammaRamp", "CreateTexture", "CreateCubeTexture",
                         "CreateVertexBuffer", "CreateIndexBuffer", "CreateOffscreenPlainSurface", "StretchRect",
                         "SetRenderTarget", "SetDepthStencilSurface", "GetDepthStencilSurface", "BeginScene",
                         "EndScene", "Clear", "SetTransform", "SetViewport", "SetMaterial", "SetLight", "LightEnable",
                         "SetRenderState", "SetTexture", "SetTextureStageState", "SetSamplerState", "DrawPrimitive",
                         "DrawIndexedPrimitive", "CreateVertexDeclaration", "SetVertexDeclaration",
                         "CreateVertexShader", "SetVertexShader", "SetVertexShaderConstantF", "SetStreamSource",
                         "SetIndices", "CreatePixelShader", "SetPixelShader", "SetPixelShaderConstantF"],
    "IDirect3DTexture9": ["GetDevice", "GetLOD", "SetLOD", "GetLevelCount", "GetLevelDesc", "GetSurfaceLevel"],
    "IDirect3DCubeTexture9": ["GetDevice", "GetLOD", "SetLOD", "GetLevelCount", "GetLevelDesc", "GetCubeMapSurface"],
    "IDirect3DSurface9": ["GetDevice", "GetDesc", "LockRect", "UnlockRect"],
    "IDirect3DVertexBuffer9": ["GetDevice", "Lock", "Unlock"],
    "IDirect3DIndexBuffer9": ["GetDevice", "Lock", "Unlock"],
}


def find_header():
    hits = sorted(glob.glob(r"C:\Program Files (x86)\Windows Kits\10\Include\*\shared\d3d9.h"))
    return hits[-1] if hits else None


def parse(text):
    """interface -> [(ret, name, [(type, pname)])], first declaration of each interface only."""
    out = {}
    for m in re.finditer(r"DECLARE_INTERFACE_\((\w+)\s*,\s*\w+\)\s*\{(.*?)\n\};", text, re.S):
        iface, body = m.group(1), m.group(2)
        if iface in out:
            continue
        body = re.sub(r"#ifdef D3D_DEBUG_INFO.*?#endif", "", body, flags=re.S)
        methods = []
        for mm in re.finditer(r"STDMETHOD(?:_\(\s*([\w\s\*]+?)\s*,\s*(\w+)\s*\)|\((\w+)\))\s*\((THIS_?)\s*([^)]*)\)\s*PURE;",
                              body):
            ret = mm.group(1) or "HRESULT"
            name = mm.group(2) or mm.group(3)
            args = mm.group(5).strip()
            params = []
            if mm.group(4) == "THIS_" and args:
                for k, p in enumerate(a.strip() for a in args.split(",")):
                    p = re.sub(r"\bCONST\b", "const", p)
                    pm = re.match(r"(.*?)(\w+)$", p)
                    ptype, pname = (pm.group(1).strip(), pm.group(2)) if pm and pm.group(1).strip() else (p, f"a{k}")
                    params.append((ptype, pname))
            methods.append((ret.strip(), name, params))
        out[iface] = methods
    return out


def com_base(t):
    """('IDirect3DSurface9', stars) for COM pointer types, else (None, 0)."""
    m = re.match(r"(?:const\s+)?(\w+)\s*(\*+)$", t.replace(" ", "") if "const" not in t else t.strip())
    if not m:
        m = re.match(r"(\w+)\s*(\*+)$", t.strip())
    if m and m.group(1) in COM_TYPES:
        return m.group(1), len(m.group(2))
    return None, 0


def gen_method(iface, idx, ret, name, params):
    sig = ", ".join(f"{t} {n}" for t, n in params)
    lines = [f"    {ret} STDMETHODCALLTYPE {name}({sig}) override", "    {",
             f"        RhiCount({idx});"]
    if name == "QueryInterface":
        lines += ["        return FacadeQueryInterface(this, riid, ppvObj);", "    }"]
        return lines
    if name == "AddRef":
        lines += ["        ++refs;", "        return obj->AddRef();", "    }"]
        return lines
    if name == "Release":
        lines += ["        rhi::Object* o = obj;", "        if (--refs == 0)", "            FacadeForget(this);",
                  "        return o->Release();", "    }"]
        return lines
    routed = f"Route_{iface}_{name}"
    args = []
    post = []
    for t, n in params:
        base, stars = com_base(t)
        if base and stars == 1:
            args.append(f"FacadeUnwrap({n})")
        elif base and stars == 2:
            args.append(n)
            post.append(f"        if ({n} && *{n}) *{n} = FacadeWrap(*{n});")
        else:
            args.append(n)
    call = f"real->{name}({', '.join(args)})"
    lines.append(f"#ifdef ROUTE_{iface}_{name}")
    lines.append(f"        return {routed}(this{''.join(', ' + n for _, n in params)});")
    lines.append("#else")
    lines.append(f"        if (!real) return FacadeUnsupported<{ret}>({idx});")
    lines.append(f"        FacadeFallback({idx});")
    if ret == "void":
        lines.append(f"        {call};")
        lines += [p for p in post]
    elif post:
        lines.append(f"        {ret} r = {call};")
        lines += post
        lines.append("        return r;")
    else:
        lines.append(f"        return {call};")
    lines.append("#endif")
    lines.append("    }")
    return lines


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--d3d9h", default=find_header())
    a = ap.parse_args()
    ifaces = parse(open(a.d3d9h, encoding="latin1").read())
    missing = [i for i in WRAPPED if i not in ifaces]
    if missing:
        raise SystemExit(f"not found in d3d9.h: {missing}")
    decls = []
    names, out = [], ["// GENERATED by tools/gen_d3d9_facade.py from d3d9.h. Do not edit; route methods in d3d9_facade.cpp.",
                      "// Included by src/rhi/d3d9_facade.cpp, which defines RhiCount, FacadeWrap/Unwrap/Forget and",
                      "// FacadeQueryInterface, and Route_<Interface>_<Method> for every ROUTE_<Interface>_<Method> it defines.",
                      ""]
    for iface in WRAPPED:
        cls = "Facade_" + iface[len("IDirect3D"):-1] if iface != "IDirect3D9" else "Facade_Direct3D"
        out += [f"class {cls} final : public {iface}", "{", "public:",
                "    rhi::Object* obj;  // the renderer-interface object behind this one",
                f"    {iface}* real;  // its D3D9 object (D3D9 backend), for methods outside the interface",
                "    ULONG refs = 1;",
                f"    explicit {cls}(rhi::Object* o) : obj(o), real(static_cast<{iface}*>(o ? o->Native() : nullptr)) {{}}"]
        decls.append(f"class {cls};")
        for ret, name, params in ifaces[iface]:
            idx = len(names)
            names.append(f"{iface}::{name}")
            out += gen_method(iface, idx, ret, name, params)
        out += ["};", ""]
    protos = []
    for iface, methods in ROUTED.items():
        cls = "Facade_" + iface[len("IDirect3D"):-1] if iface != "IDirect3D9" else "Facade_Direct3D"
        table = {name: (ret, params) for ret, name, params in ifaces[iface]}
        for name in methods:
            ret, params = table[name]
            protos.append(f"#define ROUTE_{iface}_{name}")
            protos.append(f"{ret} Route_{iface}_{name}({cls}* self{"".join(", " + t + " " + n for t, n in params)});")
    out[4:4] = decls + [""] + protos + [""]
    out.insert(4, f"constexpr int kRhiMethodCount = {len(names)};")
    out.insert(5, "static const char* const kRhiMethodNames[kRhiMethodCount] = {")
    out.insert(6, "    " + ",\n    ".join(f'"{n}"' for n in names) + "};")
    out.insert(7, "")
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    open(OUT, "w", encoding="utf-8", newline="\n").write("\n".join(out) + "\n")
    print(f"{len(WRAPPED)} interfaces, {len(names)} methods -> {os.path.relpath(OUT, ROOT)}")


if __name__ == "__main__":
    main()
