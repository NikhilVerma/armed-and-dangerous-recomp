// Recovered: zlib inflate-blocks teardown and Direct3D renderer state code (gfx\d3d area). S2, Approach B.
//   0x00503880 Zlib_InflateBlocksFree
//   0x00503b30 Renderer_InitDeviceState
//   0x005042d0 Renderer_BeginScene
//   0x005046c0 Renderer_UpdateMaterialColors
//   0x00504940 Renderer_ApplyDrawState
//   0x00504ef0 Renderer_SetStageAddressing
//   0x00505320 Renderer_ApplyMaterial
//   0x00505b00 Material_InitCombiners
// Skipped: 0x00502cb0 (zlib inflate_blocks_new) reads a caller-cleaned stack argument that symbols/abi.csv does
// not list (stack 0), so its generated entry stub cannot forward it.
#include "gen/entry_stubs.h"
#include "gen/orig_calls.h"
#include "hooks.h"
#include "renderer/renderer.h"

namespace
{
template <class T>
uint32_t u(T* p)
{
    return reinterpret_cast<uint32_t>(p);
}

// A dword of a game table: base + index * stride (index may be negative, as the game's movsx indexing).
uint32_t GameTable(uint32_t base, int32_t index, uint32_t stride)
{
    return GameGlobal<uint32_t>(base + static_cast<uint32_t>(index) * stride);
}

// A packed texture-stage operation {op, arg0, arg1, arg2}: bit 7 of the op byte is set when any argument's
// source nibble is 2 (D3DTA_TEXTURE), unless the op is 1 (D3DTOP_DISABLE), which leaves the dword as written.
uint32_t StageOpFixup(uint32_t v)
{
    if ((v & 0x7f) == 1)
        return v;
    uint32_t usesTexture =
        ((v & 0xf00) == 0x200 || (v & 0xf0000) == 0x20000 || (v & 0xf000000) == 0x2000000) ? 1u : 0u;
    return v ^ (((usesTexture << 7) ^ v) & 0x80);
}

void SetStageOp(uint8_t* p, uint8_t op, uint8_t a0, uint8_t a1, uint8_t a2)
{
    uint32_t v = op | (static_cast<uint32_t>(a0) << 8) | (static_cast<uint32_t>(a1) << 16) |
                 (static_cast<uint32_t>(a2) << 24);
    Field<uint32_t>(p, 0) = StageOpFixup(v);
}

int32_t SignExtend7(uint32_t v)
{
    return static_cast<int32_t>(v << 25) >> 25;
}

typedef void(__cdecl* ZFreeFn)(uint32_t opaque, uint32_t address);
typedef uint32_t(__cdecl* CheckFn)(uint32_t, uint32_t, uint32_t);
}  // namespace

uint32_t Renderer_SetStageAddressing(Renderer* r, const int8_t* desc, uint32_t stage);

// 0x00503880 (esi = inflate blocks state, edi = z_stream): zlib 1.1 inflate_blocks_free. Inlined
// inflate_blocks_reset (free the bit-length table in BTREE/DTREE (4/5) or the codes in CODES (6), mode = TYPE,
// window read/write = window, adler = checkfn(0, 0, 0)), then zfree (+0x24, opaque +0x28) of the window (+0x28),
// the huft array (+0x24) and the state. Returns Z_OK.
uint32_t Zlib_InflateBlocksFree(uint8_t* s, uint8_t* z)
{
    ZFreeFn zfree = Field<ZFreeFn>(z, 0x24);
    uint32_t mode = Field<uint32_t>(s, 0);
    if (mode == 4 || mode == 5)
        zfree(Field<uint32_t>(z, 0x28), Field<uint32_t>(s, 0xc));
    if (Field<uint32_t>(s, 0) == 6)
        Field<ZFreeFn>(z, 0x24)(Field<uint32_t>(z, 0x28), Field<uint32_t>(s, 4));
    uint32_t window = Field<uint32_t>(s, 0x28);
    Field<uint32_t>(s, 0x34) = window;
    Field<uint32_t>(s, 0x30) = window;
    Field<uint32_t>(s, 0) = 0;
    Field<uint32_t>(s, 0x1c) = 0;
    Field<uint32_t>(s, 0x20) = 0;
    CheckFn check = Field<CheckFn>(s, 0x38);
    if (check)
    {
        uint32_t adler = check(0, 0, 0);
        Field<uint32_t>(s, 0x3c) = adler;
        Field<uint32_t>(z, 0x30) = adler;
    }
    Field<ZFreeFn>(z, 0x24)(Field<uint32_t>(z, 0x28), Field<uint32_t>(s, 0x28));
    Field<ZFreeFn>(z, 0x24)(Field<uint32_t>(z, 0x28), Field<uint32_t>(s, 0x24));
    Field<ZFreeFn>(z, 0x24)(Field<uint32_t>(z, 0x28), u(s));
    return 0;
}

// 0x00503b30 (eax = renderer): after the device is created or reset. Aspect (+0x1648) = width / height * 0.75,
// recapture the back buffer and release the old surfaces (+0x3d0/+0x3d4), record back-buffer size and bits,
// depth-buffer bits/stencil/bias unit from the depth format (+0x400, logs unknown ones), read the caps
// (0x00508370), reset caches through renderer virtuals, set default render, texture-stage states, then call
// virtual 69 (viewport). Returns that call's result.
uint32_t Renderer_InitDeviceState(Renderer* r)
{
    const float kAspectScale = GameGlobal<float>(0x005bc15c);  // 0.75
    float width = static_cast<float>(r->presentParams.BackBufferWidth);
    float height = static_cast<float>(r->presentParams.BackBufferHeight);
    Field<float>(r, 0x1648) = width / height * kAspectScale;
    orig::fn_00508c50(u(r));
    if (auto* s = Field<IUnknown*>(r, 0x3d0))
    {
        s->Release();
        Field<uint32_t>(r, 0x3d0) = 0;
    }
    if (auto* s = Field<IUnknown*>(r, 0x3d4))
    {
        s->Release();
        Field<uint32_t>(r, 0x3d4) = 0;
    }
    Field<uint32_t>(r, 0x1638) = Field<uint32_t>(r, 0x3fc);
    Field<uint32_t>(r, 0x1634) = Field<uint32_t>(r, 0x3f8);
    Field<uint32_t>(r, 0x163c) = orig::fn_00508750(Field<uint32_t>(r, 0x3e0));
    Field<uint32_t>(r, 0x1654) = 0x33800001;
    Field<uint32_t>(r, 0x1640) = 0x18;
    Field<uint32_t>(r, 0x1644) = 0;
    switch (Field<uint32_t>(r, 0x400))
    {
    case 0x47:  // D24... 32-bit
        Field<uint32_t>(r, 0x1654) = 0x33800001;
        Field<uint32_t>(r, 0x1640) = 0x20;
        break;
    case 0x4b:  // D24S8
        Field<uint32_t>(r, 0x1654) = 0x33800001;
        Field<uint32_t>(r, 0x1640) = 0x18;
        Field<uint32_t>(r, 0x1644) = 8;
        break;
    case 0x4d:  // D24X8
        Field<uint32_t>(r, 0x1654) = 0x33800001;
        Field<uint32_t>(r, 0x1640) = 0x18;
        break;
    case 0x4f:  // D24X4S4
        Field<uint32_t>(r, 0x1654) = 0x33800001;
        Field<uint32_t>(r, 0x1640) = 0x18;
        Field<uint32_t>(r, 0x1644) = 4;
        break;
    case 0x46:
    case 0x50:  // D16_LOCKABLE, D16
        Field<uint32_t>(r, 0x1654) = 0x37800080;
        Field<uint32_t>(r, 0x1640) = 0x10;
        break;
    case 0x49:  // D15S1
        Field<uint32_t>(r, 0x1654) = 0x38000100;
        Field<uint32_t>(r, 0x1640) = 0xf;
        Field<uint32_t>(r, 0x1644) = 1;
        break;
    default:
        GameLog(reinterpret_cast<const char*>(0x00589810));  // "Unknown depthbuffer format\n"
        break;
    }
    D3DCAPS9 caps;
    r->device->GetDeviceCaps(&caps);
    orig::fn_00508370(u(&caps), u(r) + 0x14e0, u(r));
    VCall<void>(r, 0x94 / 4, Field<uint32_t>(r, 0x4ac));
    Field<uint32_t>(r, 0x3a0) = (Field<uint8_t>(r, 0x15fe) & 0x40) ? u(r) + 0x360 : 0;
    Field<uint32_t>(r, 0x33c) = u(r) + 0x340;
    uint32_t v = VCall<uint32_t>(r, 0x2c / 4);
    orig::fn_00506b60((v & 0xffffff00) | 1, u(r));
    orig::fn_0050a290(0 /* ecx: only a reserved stack slot in the callee */, u(r));
    VCall<void>(r, 0x74 / 4, 0u);
    VCall<void>(r, 0xf4 / 4, 0u);
    IDirect3DDevice9* d = r->device;
    d->SetRenderState(static_cast<D3DRENDERSTATETYPE>(0x1a), 1);
    r->device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(0x1d), 0);
    r->device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(0x8b), 0);
    r->device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(0x89), 0);
    r->device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(0x8f), 1);
    r->device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(0x92), 0);
    r->device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(7), 1);
    r->device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(0x9c), 0);
    r->device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(0x9d), 0);
    r->device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(0x9a), 0x3f800000);
    r->device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(0x9e), 0);
    r->device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(0x9f), 0);
    r->device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(0xa0), 0x3f800000);
    if (Field<uint8_t>(r, 0x15fe) & 0x10)
    {
        r->device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(0x18), 1);
        r->device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(0xf), 0);
        r->device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(0x19), 7);
    }
    for (int i = 4; i != 0; --i)
    {
        r->device->SetTextureStageState(0, static_cast<D3DTEXTURESTAGESTATETYPE>(1), 1);
        r->device->SetTextureStageState(1, static_cast<D3DTEXTURESTAGESTATETYPE>(4), 1);
    }
    Field<uint8_t>(r, 0x1680) = 1;
    return VCall<uint32_t>(r, 0x114 / 4, Field<uint32_t>(r, 0x1694), Field<uint32_t>(r, 0x1698), u(r) + 0x1688);
}

// 0x005042d0 (ecx = renderer): nested begin-scene. Increments the depth (+0x162c); only the outermost call
// runs device->BeginScene and, on success, virtuals 11 and 12, MIN/MAG filters of samplers 0-3 (point when flag
// +0x1628 bit 2, else +0x15f0/+0x15f4), FILLMODE (wireframe when +0x1628 bit 1), recaptures the back buffer and
// describes it. Returns al = 1 when the scene began, else 0 (upper bits as the original's EAX).
uint32_t Renderer_BeginScene(Renderer* r)
{
    uint32_t depth = Field<uint32_t>(r, 0x162c);
    Field<uint32_t>(r, 0x162c) = depth + 1;
    if (depth != 0)
        return depth & 0xffffff00;
    uint32_t hr = r->device->BeginScene();
    if (hr != 0)
        return hr & 0xffffff00;
    VCall<void>(r, 0x2c / 4);
    VCall<void>(r, 0x30 / 4);
    for (uint32_t sampler = 0; sampler < 4; ++sampler)
    {
        DWORD minFilter = (Field<uint8_t>(r, 0x1628) & 4) ? 1 : Field<uint32_t>(r, 0x15f0);
        r->device->SetSamplerState(sampler, static_cast<D3DSAMPLERSTATETYPE>(6), minFilter);
        DWORD magFilter = (Field<uint8_t>(r, 0x1628) & 4) ? 1 : Field<uint32_t>(r, 0x15f4);
        r->device->SetSamplerState(sampler, static_cast<D3DSAMPLERSTATETYPE>(5), magFilter);
    }
    uint32_t fill = ((~(static_cast<uint32_t>(Field<uint8_t>(r, 0x1628)) >> 1)) & 1) | 2;
    r->device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(8), fill);
    orig::fn_00508c50(u(r));
    Field<uint32_t>(r, 0x3d8) = Field<uint32_t>(r, 0x3d0);
    Field<uint32_t>(r, 0x3dc) = Field<uint32_t>(r, 0x3d4);
    uint32_t v = orig::fn_0050a5f0(u(r));
    return (v & 0xffffff00) | 1;
}

// 0x005046c0 (al = force, ebx = renderer; [draw state]): unless forced or unchanged, copy the state's five
// material colour words (+0x1c..+0x2c: emissive, ambient, diffuse, specular ARGB, power float) to the renderer
// (+0x24..+0x34), expand them into the D3DMATERIAL9 at +0x1a88 (bytes / 255; specular and power only with state
// flag +4 bit 1) and mark +0x1684 bits 1-3 dirty. Then, with bit 3 dirty and no vertex shader path (+0x33c),
// device->SetMaterial. Callers ignore the result.
uint32_t Renderer_UpdateMaterialColors(uint32_t force, Renderer* r, const uint8_t* state)
{
    const float scale = GameGlobal<float>(0x005bcc18);  // 1/255
    uint32_t result = 0;
    bool same = static_cast<uint8_t>(force) == 0 && Field<uint32_t>(state, 0x24) == Field<uint32_t>(r, 0x2c) &&
                Field<uint32_t>(state, 0x20) == Field<uint32_t>(r, 0x28) &&
                Field<uint32_t>(state, 0x1c) == Field<uint32_t>(r, 0x24) &&
                Field<uint32_t>(state, 0x28) == Field<uint32_t>(r, 0x30) &&
                Field<float>(state, 0x2c) == Field<float>(r, 0x34);
    if (!same)
    {
        for (uint32_t i = 0; i < 5; ++i)
            Field<uint32_t>(r, 0x24 + i * 4) = Field<uint32_t>(state, 0x1c + i * 4);
        auto c = [&](uint32_t offset) { return static_cast<float>(static_cast<int32_t>(Field<uint8_t>(r, offset))) * scale; };
        Field<float>(r, 0x1a88) = c(0x2e);  // diffuse
        Field<float>(r, 0x1a8c) = c(0x2d);
        Field<float>(r, 0x1a90) = c(0x2c);
        Field<float>(r, 0x1a94) = c(0x2f);
        Field<float>(r, 0x1a98) = c(0x2a);  // ambient
        Field<float>(r, 0x1a9c) = c(0x29);
        Field<float>(r, 0x1aa0) = c(0x28);
        Field<float>(r, 0x1aa4) = c(0x2b);
        if (Field<uint8_t>(state, 4) & 2)
        {
            Field<float>(r, 0x1aa8) = c(0x32);  // specular
            Field<float>(r, 0x1aac) = c(0x31);
            Field<float>(r, 0x1ab0) = c(0x30);
            Field<uint32_t>(r, 0x1ac8) = Field<uint32_t>(r, 0x34);  // power
            Field<float>(r, 0x1ab4) = c(0x33);
        }
        else
        {
            Field<uint32_t>(r, 0x1ac8) = 0;
            Field<uint32_t>(r, 0x1ab4) = 0;
            Field<uint32_t>(r, 0x1ab0) = 0;
            Field<uint32_t>(r, 0x1aac) = 0;
            Field<uint32_t>(r, 0x1aa8) = 0;
        }
        Field<float>(r, 0x1ab8) = c(0x26);  // emissive
        Field<float>(r, 0x1abc) = c(0x25);
        Field<float>(r, 0x1ac0) = c(0x24);
        result = Field<uint8_t>(r, 0x27);
        Field<float>(r, 0x1ac4) = c(0x27);
        Field<uint32_t>(r, 0x1684) |= 0xe;
    }
    if ((Field<uint8_t>(r, 0x1684) & 8) && Field<uint32_t>(r, 0x33c) == 0)
    {
        result = r->device->SetMaterial(&Field<D3DMATERIAL9>(r, 0x1a88));
        Field<uint32_t>(r, 0x1684) &= 0xfffffff7;
    }
    return result;
}

// 0x00504940 (eax = renderer, ecx unused; [draw state]): apply a draw state. Flags (+4) are masked by the
// renderer's options (+0x1628 bits 3-4) and normalised; when they differ from +0x738 the vertex declaration
// (+0x3ac, looked up via 0x00461fc0 or created via 0x00506210) and the +0x3a4 bit 20 follow them. Texture
// factor (+8 -> +0x734), four stage words (+0xc..+0x18 -> +0x5f4 step 0x50), material colours (flag bit 0,
// 0x005046c0), D3DRS 0x88 (+0x34) and D3DRS_CULLMODE from table 0x005ca798 (+0x30) are cached; the six words
// at +4 are copied to +0xc. Callers ignore the result.
uint32_t Renderer_ApplyDrawState(Renderer* r, uint32_t /*ecx*/, uint8_t* state)
{
    uint32_t flags = Field<uint32_t>(state, 4);
    uint8_t options = Field<uint8_t>(r, 0x1628);
    if (options & 8)
        flags &= 0xfffffff3;
    if (options & 0x10)
        flags &= 0xfffff0ff;
    if ((flags & 0xc) == 0)
        flags &= 0xffff0fff;
    if (Field<uint32_t>(state, 0xc) == 0 && (flags & 0xf000) == 0x7000)
        flags = (flags & 0xffff3fff) | 0x8003000;
    uint32_t diff = Field<uint32_t>(r, 0x738) ^ flags;
    uint32_t result = diff;
    if (diff != 0)
    {
        if (static_cast<int32_t>(diff) < 0)
            diff = 0xffffffff;
        result = diff;
        if (diff & 0x7ff7ffff)
        {
            uint32_t key = flags & 0x7ff7ffff;
            uint32_t decl = orig::fn_00461fc0(u(r) + 0x34c, key);
            Field<uint32_t>(r, 0x3ac) = decl;
            if (decl == 0)
                Field<uint32_t>(r, 0x3ac) = orig::fn_00506210(u(r), key);
            Field<uint32_t>(r, 0x3b0) = 0xffffffff;
        }
        if (diff & 0x80000)
        {
            if (Field<uint32_t>(state, 4) & 0x80000)
                Field<uint32_t>(r, 0x3a4) |= 0x100000;
            else
                Field<uint32_t>(r, 0x3a4) &= 0xffefffff;
        }
        Field<uint32_t>(r, 0x738) = flags;
    }
    if (Field<uint32_t>(r, 0x5f4) != Field<uint32_t>(state, 0xc) || Field<uint32_t>(r, 0x644) != Field<uint32_t>(state, 0x10) ||
        Field<uint32_t>(r, 0x694) != Field<uint32_t>(state, 0x14) || Field<uint32_t>(r, 0x6e4) != Field<uint32_t>(state, 0x18))
    {
        Field<uint32_t>(r, 0x5f4) = Field<uint32_t>(state, 0xc);
        Field<uint32_t>(r, 0x644) = Field<uint32_t>(state, 0x10);
        Field<uint32_t>(r, 0x694) = Field<uint32_t>(state, 0x14);
        Field<uint32_t>(r, 0x6e4) = Field<uint32_t>(state, 0x18);
        Field<uint32_t>(r, 0x3b0) = 0xffffffff;
    }
    if (Field<uint32_t>(r, 0x734) != Field<uint32_t>(state, 8))
    {
        Field<uint32_t>(r, 0x734) = Field<uint32_t>(state, 8);
        Field<uint32_t>(r, 0x1684) |= 4;
    }
    if (Field<uint8_t>(state, 4) & 1)
        result = Renderer_UpdateMaterialColors((diff >> 1) & 1, r, state);
    uint32_t v34 = Field<uint32_t>(state, 0x34);
    if (Field<uint32_t>(r, 0x3c) != v34)
    {
        r->device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(0x88), v34 != 0 ? 1 : 0);
        result = Field<uint32_t>(state, 0x34);
        Field<uint32_t>(r, 0x3c) = result;
    }
    uint32_t cull = Field<uint32_t>(state, 0x30);
    if (Field<uint32_t>(r, 0x38) != cull)
    {
        result = r->device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(0x16),
                                           GameTable(0x005ca798, static_cast<int32_t>(cull), 4));
        Field<uint32_t>(r, 0x38) = Field<uint32_t>(state, 0x30);
    }
    for (uint32_t i = 0; i < 6; ++i)
        Field<uint32_t>(r, 0xc + i * 4) = Field<uint32_t>(state, 4 + i * 4);
    return result;
}

// 0x00504ef0 (eax = renderer, ebx = stage addressing {address mode, flag, transform kind, texcoord index}
// (signed bytes), edi = stage): with the shader path (+0x33c) pack texcoord index + 1 and the flag into the
// stage's nibble of +0x3a4; else set D3DTSS_TEXCOORDINDEX (index 0-3, else table 0x0057d98c). Then
// D3DTSS_TEXTURETRANSFORMFLAGS (tables 0x0057d9ac / 0x0057d9a8) and ADDRESSU/V/W (table 0x0057d988) where they
// differ from the stage's cache (+0x788 + stage * 0x50), which then takes all four bytes.
uint32_t Renderer_SetStageAddressing(Renderer* r, const int8_t* desc, uint32_t stage)
{
    uint8_t* cache = reinterpret_cast<uint8_t*>(r) + stage * 0x50 + 0x788;
    if (Field<uint32_t>(r, 0x33c) != 0)
    {
        uint32_t shift = stage * 4;
        uint32_t packed = static_cast<uint32_t>(desc[3] + 1) << ((shift + 4) & 31);
        Field<uint32_t>(r, 0x3a4) = packed | (~(0x70u << (shift & 31)) & Field<uint32_t>(r, 0x3a4));
        if (desc[1] != 0)
            Field<uint32_t>(r, 0x3a4) |= 0x80u << (shift & 31);
        else
            Field<uint32_t>(r, 0x3a4) &= ~(0x80u << (shift & 31));
        if (static_cast<int8_t>(cache[2]) != desc[2])
            r->device->SetTextureStageState(stage, static_cast<D3DTEXTURESTAGESTATETYPE>(0x18),
                                            GameTable(0x0057d9ac, desc[2], 8));
    }
    else
    {
        int32_t index = desc[3];
        if (static_cast<int8_t>(cache[3]) != index)
        {
            uint32_t value = (index >= 0 && index <= 3) ? static_cast<uint32_t>(index) : GameTable(0x0057d98c, index, 4);
            r->device->SetTextureStageState(stage, static_cast<D3DTEXTURESTAGESTATETYPE>(0xb), value);
        }
        if (static_cast<int8_t>(cache[2]) != desc[2])
            r->device->SetTextureStageState(stage, static_cast<D3DTEXTURESTAGESTATETYPE>(0x18),
                                            GameTable(0x0057d9a8, desc[2], 8));
    }
    if (static_cast<int8_t>(cache[0]) != desc[0])
    {
        r->device->SetSamplerState(stage, static_cast<D3DSAMPLERSTATETYPE>(1), GameTable(0x0057d988, desc[0], 4));
        r->device->SetSamplerState(stage, static_cast<D3DSAMPLERSTATETYPE>(2), GameTable(0x0057d988, desc[0], 4));
        uint32_t hr = r->device->SetSamplerState(stage, static_cast<D3DSAMPLERSTATETYPE>(3),
                                                 GameTable(0x0057d988, desc[0], 4));
        Field<uint32_t>(cache, 0) = Field<uint32_t>(desc, 0);
        return hr;
    }
    uint32_t all = Field<uint32_t>(desc, 0);
    Field<uint32_t>(cache, 0) = all;
    return all;
}

// 0x00505320 (esi = renderer; [material]): bind a material. For stages 0-3: the material's slot byte (+4+i,
// 0xff none; slot 1 when flags +0x48 bit 27) selects a renderer slot (+0x5f4 + slot * 0x50) whose transform and
// addressing are applied, then its texture is created if needed (virtual 73), bound with SetTexture and its
// sampler mip/anisotropy states set (+0x3a8 stage nibble tracks it). Changed flag bits drive COLORWRITEENABLE,
// FOGENABLE, fog colour, ZFUNC, ZWRITEENABLE, lighting/colour-source states, SPECULARENABLE and +0x3a4 bits.
// Then SetPixelShader (+0x4c, cached at +0x8bc); without one, the fixed-function colour/alpha ops (+0x18 step
// 0xc; texture ops on an empty stage become DISABLE via 0x0064a7c0) are cached at +0x87c. Finally the blend
// (0x005050e0) and 0x0050ca70, whose result is returned.
// Test hook (null in play): told every bound material with its four stage textures' names (record +0) and its
// pixel shader (+0x4c). Set by the test harness ([test] log_materials, hook/src/testshot.cpp).
void (*g_testMaterialApplied)(const uint8_t* material, const char* const names[4]) = nullptr;

uint32_t Renderer_ApplyMaterial(Renderer* r, uint8_t* material)
{
    uint8_t* rb = reinterpret_cast<uint8_t*>(r);
    const char* stageNames[4] = {};
    for (uint32_t stage = 0; stage < 4; ++stage)
    {
        uint8_t* stageCache = rb + 0x740 + stage * 0x50;
        uint32_t texture = 0;
        uint32_t slot = Field<uint8_t>(material, 4 + stage);
        if (slot != 0xff)
        {
            if (Field<uint32_t>(material, 0x48) & 0x8000000)
                slot = 1;
            uint8_t* slotBase = rb + slot * 0x50;
            if (Field<uint32_t>(slotBase, 0x5f4) != 0)
            {
                uint32_t transformId = Field<uint32_t>(slotBase, 0x5f8);
                if (transformId != Field<uint32_t>(stageCache, 0))
                {
                    Field<uint32_t>(stageCache, 0) = transformId;
                    orig::fn_00504e70(u(r), u(slotBase) + 0x5fc, stage);
                }
                uint8_t* addressing = slotBase + 0x640;
                if (Field<uint32_t>(addressing, 0) != Field<uint32_t>(stageCache, 0x48))
                    Renderer_SetStageAddressing(r, reinterpret_cast<const int8_t*>(addressing), stage);
                texture = Field<uint32_t>(slotBase, 0x5f4);
            }
        }
        Field<uint32_t>(stageCache - 4, 0) = texture;  // +0x73c + stage * 0x50: the stage's texture
        uint32_t d3dTexture = 0;
        uint8_t* tex = reinterpret_cast<uint8_t*>(texture);
        stageNames[stage] = tex ? Field<const char*>(tex, 0) : nullptr;
        if (tex)
        {
            if (!(Field<uint16_t>(tex, 0x22) & 0x8000))
                VCall<void>(r, 0x124 / 4, tex);
            if (uint8_t* d3d = Field<uint8_t*>(tex, 0xc))
                d3dTexture = Field<uint32_t>(d3d, 0xc);
        }
        uint32_t& boundTexture = Field<uint32_t>(r, 0x8d0 + stage * 4);
        if (boundTexture == d3dTexture)
            continue;
        r->device->SetTexture(stage, reinterpret_cast<IDirect3DBaseTexture9*>(d3dTexture));
        boundTexture = d3dTexture;
        if (tex)
        {
            if (Field<uint8_t>(tex, 0x23) & 1)
            {
                r->device->SetSamplerState(stage, static_cast<D3DSAMPLERSTATETYPE>(8), Field<uint32_t>(r, 0x1658));
                r->device->SetSamplerState(stage, static_cast<D3DSAMPLERSTATETYPE>(7), Field<uint32_t>(r, 0x15f8));
                if (uint32_t aniso = Field<uint32_t>(r, 0x15e8))
                    r->device->SetSamplerState(stage, static_cast<D3DSAMPLERSTATETYPE>(10), aniso);
            }
            else
            {
                r->device->SetSamplerState(stage, static_cast<D3DSAMPLERSTATETYPE>(7), 0);
                if (Field<uint32_t>(r, 0x15e8) != 0)
                    r->device->SetSamplerState(stage, static_cast<D3DSAMPLERSTATETYPE>(10), 1);
            }
            Field<uint32_t>(r, 0x3a8) |= 0xf0u << (stage * 4);
        }
        else
        {
            Field<uint32_t>(r, 0x3a8) &= ~(0xf0u << (stage * 4));
        }
    }

    uint32_t flags = Field<uint32_t>(material, 0x48);
    uint32_t changed = Field<uint32_t>(r, 0x8cc) ^ flags;
    if (changed != 0)
    {
        if (static_cast<int32_t>(changed) < 0)
            changed = 0xffffffff;
        if (changed & 0x7f00073)
        {
            if (changed & 0x600000)
            {
                uint32_t mask = (flags & 0x200000) ? 0 : 7;
                if (flags & 0x400000)
                    mask |= 8;
                r->device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(0xa8), mask);
            }
            if (changed & 0x10)
            {
                DWORD fog = ((Field<uint8_t>(material, 0x48) & 0x10) && (Field<uint8_t>(r, 0x14dc) & 0x10)) ? 1 : 0;
                r->device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(0x1c), fog);
            }
            if (changed & 0x4000000)
                orig::fn_00505070(u(r), (Field<uint32_t>(material, 0x48) & 0x4000000) ? 0x00589764 : 0);
            if (changed & 0x40)
                r->device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(0x17),
                                          (Field<uint8_t>(material, 0x48) & 0x40) ? 8 : 4);
            if (changed & 0x20)
                r->device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(0xe),
                                          ~(Field<uint32_t>(material, 0x48) >> 5) & 1);
            if (changed & 1)
            {
                uint32_t shaderPath = Field<uint32_t>(r, 0x33c);
                if (Field<uint8_t>(material, 0x48) & 1)
                {
                    if (shaderPath)
                    {
                        Field<uint32_t>(r, 0x3a8) |= 0x400003;
                        Field<uint32_t>(r, 0x3a4) &= 0xffdfffff;
                    }
                    else
                    {
                        r->device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(0x89), 1);
                        r->device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(0x8d), 0);
                        r->device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(0x91), 0);
                        r->device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(0x92), 0);
                    }
                }
                else
                {
                    if (shaderPath)
                    {
                        Field<uint32_t>(r, 0x3a8) &= 0xffbffffc;
                        Field<uint32_t>(r, 0x3a4) |= 0x200000;
                    }
                    else
                    {
                        r->device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(0x89), 0);
                        r->device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(0x8d), 1);
                        r->device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(0x91), 1);
                        r->device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(0x92), 2);
                    }
                }
            }
            if (changed & 2)
            {
                if (Field<uint8_t>(material, 0x48) & 2)
                {
                    r->device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(0x1d), 1);
                    Field<uint32_t>(r, 0x3a4) |= 0x400000;
                }
                else
                {
                    r->device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(0x1d), 0);
                    Field<uint32_t>(r, 0x3a4) &= 0xffbfffff;
                }
            }
            if (changed & 0x1800000)
            {
                uint32_t v = Field<uint32_t>(r, 0x3a4);
                uint32_t w;
                if (Field<uint32_t>(material, 0x48) & 0x1800000)
                {
                    v |= 0x2000000;
                    Field<uint32_t>(r, 0x3a4) = v;
                    w = (Field<uint32_t>(material, 0x48) & 0x1000000) ? (v | 0x4000000) : (v & 0xfbffffff);
                }
                else
                {
                    v &= 0xfdffffff;
                    Field<uint32_t>(r, 0x3a4) = v;
                    w = v & 0xfbffffff;
                }
                Field<uint32_t>(r, 0x3a4) = w;
            }
            if (changed & 0x2000000)
            {
                Field<uint32_t>(r, 0x3b4) = (Field<uint32_t>(material, 0x48) & 0x2000000) ? 1 : Field<uint32_t>(r, 0x3b8);
                uint32_t level = (Field<uint32_t>(r, 0x3a4) >> 2) & 3;
                uint32_t limit = Field<uint32_t>(r, 0x3b4);
                if (!(level < limit))
                    level = limit;
                Field<uint32_t>(r, 0x3a4) = (Field<uint32_t>(r, 0x3a4) & 0xfffffff3) | (level * 4);
            }
        }
        Field<uint32_t>(r, 0x8cc) = Field<uint32_t>(material, 0x48);
    }

    uint32_t shader = Field<uint32_t>(material, 0x4c);
    if (shader != Field<uint32_t>(r, 0x8bc))
    {
        r->device->SetPixelShader(reinterpret_cast<IDirect3DPixelShader9*>(shader));
        Field<uint32_t>(r, 0x8bc) = Field<uint32_t>(material, 0x4c);
        Field<uint32_t>(r, 0x1684) |= 4;
        for (uint32_t i = 0; i < 12; ++i)
            Field<uint32_t>(r, 0x87c + i * 4) = 0xffffffff;
    }
    if (Field<uint32_t>(material, 0x4c) == 0)
    {
        uint32_t& disabledOp = GameGlobal<uint32_t>(0x0064a7c0);
        for (uint32_t stage = 0; stage < 4; ++stage)
        {
            uint8_t* cache = rb + 0x87c + stage * 0xc;
            uint8_t* ops = material + 0x18 + stage * 0xc;
            const uint32_t stageTexture = u(rb) + 0x73c + stage * 0x50;

            const uint8_t* color = ops;
            if (static_cast<int8_t>(color[0]) < 0 && GameGlobal<uint32_t>(stageTexture) == 0)
            {
                disabledOp = 0x01010101;
                color = reinterpret_cast<const uint8_t*>(&disabledOp);
            }
            IDirect3DDevice9* d = r->device;
            uint32_t want = Field<uint32_t>(color, 0);
            if (Field<uint32_t>(cache, 0) != want)
            {
                if (SignExtend7(want) != SignExtend7(Field<uint32_t>(cache, 0)))
                    d->SetTextureStageState(stage, static_cast<D3DTEXTURESTAGESTATETYPE>(1),
                                            static_cast<DWORD>(SignExtend7(want)));
                if (color[1] != cache[1])
                    d->SetTextureStageState(stage, static_cast<D3DTEXTURESTAGESTATETYPE>(0x1a), color[1]);
                if (color[2] != cache[2])
                    d->SetTextureStageState(stage, static_cast<D3DTEXTURESTAGESTATETYPE>(2), color[2]);
                if (color[3] != cache[3])
                    d->SetTextureStageState(stage, static_cast<D3DTEXTURESTAGESTATETYPE>(3), color[3]);
                Field<uint32_t>(cache, 0) = Field<uint32_t>(color, 0);
            }

            const uint8_t* alpha = ops + 4;
            if (static_cast<int8_t>(alpha[0]) < 0 && GameGlobal<uint32_t>(stageTexture) == 0)
            {
                disabledOp = 0x01010101;
                alpha = reinterpret_cast<const uint8_t*>(&disabledOp);
            }
            d = r->device;
            want = Field<uint32_t>(alpha, 0);
            if (Field<uint32_t>(cache, 4) != want)
            {
                if (SignExtend7(want) != SignExtend7(Field<uint32_t>(cache, 4)))
                    d->SetTextureStageState(stage, static_cast<D3DTEXTURESTAGESTATETYPE>(4),
                                            static_cast<DWORD>(SignExtend7(want)));
                if (alpha[1] != cache[5])
                    d->SetTextureStageState(stage, static_cast<D3DTEXTURESTAGESTATETYPE>(0x1b), alpha[1]);
                if (alpha[2] != cache[6])
                    d->SetTextureStageState(stage, static_cast<D3DTEXTURESTAGESTATETYPE>(5), alpha[2]);
                if (alpha[3] != cache[7])
                    d->SetTextureStageState(stage, static_cast<D3DTEXTURESTAGESTATETYPE>(6), alpha[3]);
                Field<uint32_t>(cache, 4) = Field<uint32_t>(alpha, 0);
            }

            uint32_t resultArg = Field<uint32_t>(ops, 8);
            if (resultArg != Field<uint32_t>(cache, 8))
            {
                r->device->SetTextureStageState(stage, static_cast<D3DTEXTURESTAGESTATETYPE>(0x1c), resultArg);
                Field<uint32_t>(cache, 8) = Field<uint32_t>(ops, 8);
            }
        }
    }
    if (g_testMaterialApplied)
        g_testMaterialApplied(material, stageNames);
    orig::fn_005050e0(u(r), u(material) + 8);
    return orig::fn_0050ca70(u(r));
}

// 0x00505b00 (ecx = kind, edx = material; [object, flags, colour arg, &factor, slot0, slot1, slot2]): fill a
// material's fixed-function combiner set-up. Kind 0-1: one stage (MODULATE texture with the colour arg).
// Otherwise, unless kind 2 or the object's +0x11c lacks bits 1-2, two stages with a BLENDDIFFUSEALPHA-style
// third op (+0x30) and +0x20 = 5; else the four-pass layout that also fills the second block (+0x50..+0x9f,
// flags +0x98 and kind byte 2). Each op word is packed by SetStageOp; the blend words (+8..+0x14) come from the
// table 0x0057d9e0 indexed by flags bits 8-11.
uint32_t Material_InitCombiners(uint32_t kind, uint8_t* m, uint8_t* object, uint32_t flags, uint32_t colorArg,
                                const uint32_t* factor, uint32_t slot0, uint32_t slot1, uint32_t slot2)
{
    uint8_t arg = static_cast<uint8_t>(colorArg);
    if (kind <= 1)
    {
        m[4] = static_cast<uint8_t>(slot0);
        SetStageOp(m + 0x18, 4, 1, 2, arg);
        Field<uint32_t>(m, 0x1c) = *factor;
    }
    else if (kind == 2 || !(Field<uint8_t>(object, 0x11c) & 2) || !(Field<uint8_t>(object, 0x11c) & 4))
    {
        m[5] = static_cast<uint8_t>(slot2);
        m[4] = static_cast<uint8_t>(slot0);
        SetStageOp(m + 0x18, 4, 1, 2, arg);
        SetStageOp(m + 0x24, 4, 1, 0x12, 1);
        SetStageOp(m + 0x1c, 2, 1, 2, 1);
        Field<uint32_t>(m, 0x28) = *factor;
        uint32_t blend = 0x0057d9e0 + ((flags >> 8) & 0xf) * 16;
        for (uint32_t i = 0; i < 4; ++i)
            Field<uint32_t>(m, 8 + i * 4) = GameGlobal<uint32_t>(blend + i * 4);
        uint8_t* second = m + 0x50;
        for (uint32_t i = 0; i < 0x14; ++i)
            Field<uint32_t>(second, i * 4) = 0;
        second[1] = 1;
        second[0] = 2;
        for (uint32_t i = 0; i < 4; ++i)
        {
            uint8_t* stage = second + 0x18 + i * 0xc;
            second[4 + i] = 0xff;
            Field<uint32_t>(stage, 8) = 1;
            SetStageOp(stage, 1, 1, 1, 1);
            SetStageOp(stage + 4, 1, 1, 1, 1);
        }
        m[0x55] = static_cast<uint8_t>(slot2);
        m[0x54] = static_cast<uint8_t>(slot1);
        SetStageOp(m + 0x68, 4, 1, 2, arg);
        SetStageOp(m + 0x74, 4, 1, 2, 1);
        SetStageOp(m + 0x6c, 2, 1, 2, 1);
        Field<uint32_t>(m, 0x78) = *factor;
        for (uint32_t i = 0; i < 4; ++i)
            Field<uint32_t>(m, 0x58 + i * 4) = GameGlobal<uint32_t>(0x0057d9f0 + i * 4);
        uint32_t secondFlags = (flags & 0x7bf7ffdf) | 0x4000020;
        Field<uint32_t>(m, 0x98) = secondFlags;
        m[0] = 2;
        return secondFlags;
    }
    else
    {
        m[4] = static_cast<uint8_t>(slot0);
        m[6] = static_cast<uint8_t>(slot2);
        m[5] = static_cast<uint8_t>(slot1);
        Field<uint32_t>(m, 0x20) = 5;
        SetStageOp(m + 0x18, 4, 1, 2, arg);
        SetStageOp(m + 0x24, 4, 1, 2, arg);
        SetStageOp(m + 0x30, 0x1a, 2, 1, 5);
        SetStageOp(m + 0x1c, 2, 1, 2, 1);
        SetStageOp(m + 0x28, 2, 1, 2, 1);
        Field<uint32_t>(m, 0x34) = *factor;
    }
    uint32_t blend = 0x0057d9e0 + ((flags >> 8) & 0xf) * 16;
    Field<uint32_t>(m, 8) = GameGlobal<uint32_t>(blend);
    Field<uint32_t>(m, 0xc) = GameGlobal<uint32_t>(blend + 4);
    Field<uint32_t>(m, 0x10) = GameGlobal<uint32_t>(blend + 8);
    uint32_t last = GameGlobal<uint32_t>(blend + 0xc);
    Field<uint32_t>(m, 0x14) = last;
    return last;
}

static uint32_t Zlib_InflateBlocksFree_Abi(uint32_t esi, uint32_t edi)
{
    return Zlib_InflateBlocksFree(reinterpret_cast<uint8_t*>(esi), reinterpret_cast<uint8_t*>(edi));
}
static uint32_t Renderer_InitDeviceState_Abi(uint32_t eax)
{
    return Renderer_InitDeviceState(reinterpret_cast<Renderer*>(eax));
}
static uint32_t Renderer_BeginScene_Abi(uint32_t ecx)
{
    return Renderer_BeginScene(reinterpret_cast<Renderer*>(ecx));
}
static uint32_t Renderer_UpdateMaterialColors_Abi(uint32_t eax, uint32_t ebx, uint32_t state)
{
    return Renderer_UpdateMaterialColors(eax, reinterpret_cast<Renderer*>(ebx), reinterpret_cast<const uint8_t*>(state));
}
static uint32_t Renderer_ApplyDrawState_Abi(uint32_t eax, uint32_t ecx, uint32_t state)
{
    return Renderer_ApplyDrawState(reinterpret_cast<Renderer*>(eax), ecx, reinterpret_cast<uint8_t*>(state));
}
static uint32_t Renderer_SetStageAddressing_Abi(uint32_t eax, uint32_t ebx, uint32_t edi)
{
    return Renderer_SetStageAddressing(reinterpret_cast<Renderer*>(eax), reinterpret_cast<const int8_t*>(ebx), edi);
}
static uint32_t Renderer_ApplyMaterial_Abi(uint32_t esi, uint32_t material)
{
    return Renderer_ApplyMaterial(reinterpret_cast<Renderer*>(esi), reinterpret_cast<uint8_t*>(material));
}
static uint32_t Material_InitCombiners_Abi(uint32_t ecx, uint32_t edx, uint32_t a0, uint32_t a1, uint32_t a2,
                                           uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6)
{
    return Material_InitCombiners(ecx, reinterpret_cast<uint8_t*>(edx), reinterpret_cast<uint8_t*>(a0), a1, a2,
                                  reinterpret_cast<const uint32_t*>(a3), a4, a5, a6);
}

ABI_ENTRY_00503880(Zlib_InflateBlocksFree_Entry, Zlib_InflateBlocksFree_Abi)
ABI_ENTRY_00503b30(Renderer_InitDeviceState_Entry, Renderer_InitDeviceState_Abi)
ABI_ENTRY_005042d0(Renderer_BeginScene_Entry, Renderer_BeginScene_Abi)
ABI_ENTRY_005046c0(Renderer_UpdateMaterialColors_Entry, Renderer_UpdateMaterialColors_Abi)
ABI_ENTRY_00504940(Renderer_ApplyDrawState_Entry, Renderer_ApplyDrawState_Abi)
ABI_ENTRY_00504ef0(Renderer_SetStageAddressing_Entry, Renderer_SetStageAddressing_Abi)
ABI_ENTRY_00505320(Renderer_ApplyMaterial_Entry, Renderer_ApplyMaterial_Abi)
ABI_ENTRY_00505b00(Material_InitCombiners_Entry, Material_InitCombiners_Abi)
HOOK(0x00503880, "Zlib_InflateBlocksFree", Zlib_InflateBlocksFree_Entry, nullptr);
HOOK(0x00503b30, "Renderer_InitDeviceState", Renderer_InitDeviceState_Entry, nullptr);
HOOK(0x005042d0, "Renderer_BeginScene", Renderer_BeginScene_Entry, nullptr);
HOOK(0x005046c0, "Renderer_UpdateMaterialColors", Renderer_UpdateMaterialColors_Entry, nullptr);
HOOK(0x00504940, "Renderer_ApplyDrawState", Renderer_ApplyDrawState_Entry, nullptr);
HOOK(0x00504ef0, "Renderer_SetStageAddressing", Renderer_SetStageAddressing_Entry, nullptr);
HOOK(0x00505320, "Renderer_ApplyMaterial", Renderer_ApplyMaterial_Entry, nullptr);
HOOK(0x00505b00, "Material_InitCombiners", Material_InitCombiners_Entry, nullptr);
