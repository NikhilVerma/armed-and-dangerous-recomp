// Recovered: renderer setup, caps, display modes, device and texture creation. S2, Approach B (parallel batch).
//   0x00506210 Renderer_BuildColorMixPass
//   0x00508290 Renderer_ParseOptions
//   0x00508370 Renderer_SummarizeCaps
//   0x00508ae0 Renderer_FindDisplayMode
//   0x00508ce0 Renderer_SetupPresentParams
//   0x00508e80 Renderer_CreateDevice
//   0x00509eb0 Renderer_CreateTexture
//   0x0050a290 Renderer_ProbeTextureFormats
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
const char* Str(uint32_t address)
{
    return reinterpret_cast<const char*>(address);
}
const uint32_t kStrDXError = 0x0058b33c;                // "Error %s in DX function %s\n"
const uint32_t kStrUnsupportedColorMix = 0x00589740;    // "Unsupported shader color mix flags\n"
const uint32_t kStrCreateDevice = 0x0058959c;           // "IDirec3D9::CreateDevice"
const uint32_t kStrCouldNotInitDevice = 0x00589578;     // "Could not initialize Direct3DDevice"
const uint32_t kStrError = 0x0058e504;                  // "Error"
const uint32_t kStrCreateTexture = 0x00589458;          // "IDirect3DDevice9::CreateTexture"
const uint32_t kStrCreateCubeTexture = 0x00589478;      // "IDirect3DDevice9::CreateCubeTexture"
const uint32_t kStrNoVShaders = 0x005895d8;             // "novshaders"
const uint32_t kStrNoPShaders = 0x005895cc;             // "nopshaders"
const uint32_t kStrOptionDelimiters = 0x005895e4;

// Largest power of two the exact-log2 helpers test against (0x005caae0).
int32_t PowerLimit()
{
    return GameGlobal<int32_t>(0x005caae0);
}

// Inlined helper (0x5083dc, 0x50a046, 0x50a08f, 0x50a0b4): log2(v) when v is a power of two up to the limit,
// otherwise minus the number of doublings tried. Signed comparisons, as in the original.
int32_t ExactLog2(int32_t v)
{
    int32_t limit = PowerLimit();
    int32_t e = 1;
    int32_t c = 0;
    if (limit < e)
        return -c;
    for (;;)
    {
        if (v <= e)
            return v == e ? c : -c;
        e += e;
        ++c;
        if (e > limit)
            return -c;
    }
}

// A texture stage op word: low byte = op, then three argument bytes; bit 7 of the op byte is set when any
// argument byte equals 2 (the 0xf00/0xf0000/0xf000000 tests), unless the op is 1.
void StageFix(uint8_t* p)
{
    uint32_t v = Field<uint32_t>(p, 0);
    if ((v & 0x7f) == 1)
        return;
    bool flag = (v & 0xf00) == 0x200 || (v & 0xf0000) == 0x20000 || (v & 0xf000000) == 0x2000000;
    Field<uint32_t>(p, 0) = (((flag ? 1u : 0u) << 7 ^ v) & 0x80) ^ v;
}
void SetStage(uint8_t* p, uint8_t op, uint8_t a, uint8_t b, uint8_t c)
{
    p[0] = op;
    p[1] = a;
    p[2] = b;
    p[3] = c;
    StageFix(p);
}
void CopyColorMixTable(uint8_t* out, uint32_t flags)
{
    memcpy(out + 8, reinterpret_cast<const void*>(0x0057d9e0 + ((flags >> 8) & 0xf) * 16), 16);
}

void SetBit(uint8_t& b, uint8_t mask, bool on)
{
    b = on ? static_cast<uint8_t>(b | mask) : static_cast<uint8_t>(b & ~mask);
}
}  // namespace

// 0x00506210 ([renderer, mix flags]): fill the next 0x50-byte pass record (pointer at +0x35c): one pass, stage
// texture indices 0xff, default stage op words, then the colour op from flags bits 12-15, the alpha op from
// bits 3/16/18, an optional pixel shader from the shader table (+0x3a0), constants from 0x0057d9e0 by bits 8-11
// (multi-stage mixes go to 0x00505b00). Logs unknown mixes. Advances +0x35c by the record's pass count.
uint8_t* Renderer_BuildColorMixPass(Renderer* r, uint32_t inFlags)
{
    uint8_t* out = Field<uint8_t*>(r, 0x35c);
    memset(out, 0, 0x50);
    out[1] = 0;
    out[0] = 1;
    for (uint32_t i = 0; i < 4; ++i)
    {
        out[4 + i] = 0xff;
        uint8_t* stage = out + 0x18 + i * 12;
        Field<uint32_t>(stage, 8) = 1;
        SetStage(stage, 1, 1, 1, 1);
        SetStage(stage + 4, 1, 1, 1, 1);
    }
    uint32_t flags = inFlags;
    uint8_t arg3;
    if (flags & 0x80)
    {
        arg3 = 0;
    }
    else
    {
        arg3 = 3;
        if (!(flags & 0x20000))
            flags |= 0x20000;
    }
    uint32_t alphaOp = 0;
    switch (flags & 0x50008)
    {
    case 0x00000: alphaOp = 0x1010101; break;
    case 0x00008: alphaOp = 0x1020182; break;
    case 0x10000: alphaOp = 0x1000102; break;
    case 0x40000: alphaOp = 0x1030102; break;
    case 0x40008: alphaOp = 0x3020184; break;
    case 0x50000: alphaOp = 0x3000104; break;
    case 0x10008:
    case 0x50008: alphaOp = 0x20184; break;
    }
    uint8_t* stage0 = out + 0x18;
    uint8_t* shaders = Field<uint8_t*>(r, 0x3a0);
    uint8_t caps = Field<uint8_t>(r, 0x15fc);
    uint32_t stageCount = Field<uint32_t>(r, 0x4ac);
    uint32_t mixA = 0, mixB = 0, mixC = 0;
    bool multi = false;
    switch (flags & 0xf000)
    {
    case 0x0000:
        out[4] = 0;
        SetStage(stage0, 2, 1, arg3, 1);
        Field<uint32_t>(out, 0x1c) = alphaOp;
        CopyColorMixTable(out, flags);
        break;
    case 0x1000:
    case 0x3000:
    case 0x5000:
    case 0x6000:
    case 0xe000:
        out[4] = 0;
        SetStage(stage0, static_cast<uint8_t>(GameGlobal<uint32_t>(0x0057da70 + ((flags >> 12) & 0xf) * 4) & 0x7f), 1,
                 2, arg3);
        Field<uint32_t>(out, 0x1c) = alphaOp;
        CopyColorMixTable(out, flags);
        break;
    case 0x2000:
    {
        out[4] = 0;
        uint8_t op = 2;
        if (flags & 0x10000)
            op = 0xc;
        else if (flags & 0x40000)
            op = 0xe;
        SetStage(stage0, op & 0x7f, 1, 2, arg3);
        Field<uint32_t>(out, 0x1c) = alphaOp;
        CopyColorMixTable(out, flags);
        break;
    }
    case 0x4000:
        out[4] = 0;
        if (shaders)
        {
            Field<uint32_t>(out, 0x4c) = Field<uint32_t>(shaders, 4);
        }
        else
        {
            SetStage(stage0, 5, 1, 2, arg3);
            Field<uint32_t>(out, 0x1c) = alphaOp;
        }
        CopyColorMixTable(out, flags);
        break;
    case 0x7000:
        if ((caps & 1) && (caps & 8) && stageCount >= 2)
        {
            out[4] = 0;
            out[5] = 1;
            SetStage(stage0, 4, 1, 2, 3);
            SetStage(out + 0x24, 0x19, 1, 2, arg3);
            SetStage(out + 0x1c, 2, 1, 1, 1);
            flags |= 0x20000;
            Field<uint32_t>(out, 0x28) = alphaOp;
        }
        else
        {
            out[4] = 1;
            SetStage(stage0, 4, 1, 2, arg3);
            Field<uint32_t>(out, 0x1c) = alphaOp;
        }
        CopyColorMixTable(out, flags);
        break;
    case 0x8000:
        out[4] = 0;
        out[5] = 1;
        out[6] = 2;
        out[7] = 3;
        Field<uint32_t>(out, 0x4c) = Field<uint32_t>(shaders, 0x1c);
        CopyColorMixTable(out, flags);
        break;
    case 0xa000:
        if (shaders && stageCount >= 4)
        {
            out[4] = 0;
            out[5] = 1;
            out[6] = 2;
            out[7] = 3;
            Field<uint32_t>(out, 0x4c) = Field<uint32_t>(shaders, 0);
            CopyColorMixTable(out, flags);
        }
        else
        {
            multi = true;
            mixA = 1, mixB = 2, mixC = 3;
        }
        break;
    case 0xb000:
        if ((caps & 0x10) && (Field<uint8_t>(r, 0x14dc) & 8) && stageCount >= 4 && shaders)
        {
            out[4] = 0;
            out[5] = 1;
            out[6] = 2;
            out[7] = 3;
            Field<uint32_t>(out, 0x4c) = Field<uint32_t>(shaders, 0x10);
            if (!(flags & 2))
                flags |= 2;
            flags |= 0x1000000;
            CopyColorMixTable(out, flags);
        }
        else
        {
            multi = true;
            mixA = 1, mixB = 2, mixC = 3;
        }
        break;
    case 0xc000:
        if ((caps & 0x10) && (Field<uint8_t>(r, 0x14dc) & 8) && stageCount >= 2 && shaders)
        {
            out[4] = 0;
            out[5] = 1;
            uint32_t shader = (flags & 2) ? Field<uint32_t>(shaders, 0xc) : Field<uint32_t>(shaders, 8);
            flags |= 0x800000;
            Field<uint32_t>(out, 0x4c) = shader;
            CopyColorMixTable(out, flags);
        }
        else
        {
            out[4] = 0;
            SetStage(stage0, 4, 1, 2, arg3);
            Field<uint32_t>(out, 0x1c) = alphaOp;
            CopyColorMixTable(out, flags);
        }
        break;
    case 0xd000:
        multi = true;
        mixA = 0, mixB = 1, mixC = 2;
        break;
    case 0xf000:
        multi = true;
        mixA = 1, mixB = 0, mixC = 2;
        break;
    default:
        GameLog(Str(kStrUnsupportedColorMix));
        break;
    }
    if (multi)
        orig::fn_00505b00(stageCount, u(out), u(r) + 0x14e0, flags, arg3, u(&alphaOp), mixA, mixB, mixC);
    Field<uint32_t>(out, 0x48) = flags & 0x7ff7ffff;
    orig::fn_00461ff0(inFlags, u(out), u(r) + 0x34c);
    Field<uint32_t>(r, 0x35c) += static_cast<uint32_t>(out[0]) * 0x50;
    return out;
}

// 0x00508290 ([renderer, option string or null]): reset the shader option bits of +0x14dc (bits 1-4 set, bit 5
// clear), then strtok the string (game's strtok 0x005381e4, delimiters 0x005895e4): "novshaders" clears bit 1,
// "nopshaders" clears bit 2. Returns 0.
uint32_t Renderer_ParseOptions(Renderer* r, const char* options)
{
    uint8_t& opts = Field<uint8_t>(r, 0x14dc);
    opts = static_cast<uint8_t>((opts & 0xdf) | 0x1e);
    if (!options)
        return 0;
    char buffer[256] = {};
    orig::fn_005382a0(u(buffer), u(options), 0xff);
    for (auto token = reinterpret_cast<const char*>(orig::fn_005381e4(u(buffer), kStrOptionDelimiters)); token;
         token = reinterpret_cast<const char*>(orig::fn_005381e4(0, kStrOptionDelimiters)))
    {
        if (strcmp(token, Str(kStrNoVShaders)) == 0)
            opts &= 0xfd;
        else if (strcmp(token, Str(kStrNoPShaders)) == 0)
            opts &= 0xfb;
    }
    return 0;
}

// 0x00508370 (eax = D3DCAPS9, edx = summary (0x120 bytes, Renderer +0x1364); [unused]): condense the device caps:
// max texture size (+8/+0x10, default 0x1000), log2 max aspect (+0x104), anisotropy and filter levels
// (+0x108..+0x118), stage count (+0x10c), feature bits at +0x11c..+0x11e, and a capability mask at +0 (returned).
uint32_t Renderer_SummarizeCaps(const uint8_t* caps, uint8_t* out, uint32_t /*unused*/)
{
    memset(out, 0, 0x120);
    Field<uint32_t>(out, 4) = 2;
    Field<uint32_t>(out, 0xc) = 2;
    uint32_t v = Field<uint32_t>(caps, 0x58);
    Field<uint32_t>(out, 8) = v ? v : 0x1000;
    v = Field<uint32_t>(caps, 0x5c);
    Field<uint32_t>(out, 0x10) = v ? v : 0x1000;
    Field<uint32_t>(out, 0x10c) = Field<uint32_t>(caps, 0x98);
    uint8_t& b0 = out[0x11c];
    uint8_t& b1 = out[0x11d];
    uint8_t& b2 = out[0x11e];
    const uint32_t textureCaps = Field<uint32_t>(caps, 0x3c);
    SetBit(b0, 1, (textureCaps >> 11) & 1);
    if (!(caps[0x3c] & 0x20))
    {
        int32_t aspect = Field<int32_t>(caps, 0x68);
        Field<int32_t>(out, 0x104) = aspect == 0 ? -1 : ExactLog2(aspect);
    }
    const uint32_t filter = Field<uint32_t>(caps, 0x40);
    const uint32_t raster = Field<uint32_t>(caps, 0x24);
    if ((raster & 0x20000) && (filter & 0x400) && (filter & 0x4000000))
    {
        Field<uint32_t>(out, 0x108) = Field<uint32_t>(caps, 0x6c);
        Field<uint32_t>(out, 0x110) = 3;
        Field<uint32_t>(out, 0x114) = 3;
    }
    else
    {
        Field<uint32_t>(out, 0x108) = 0;
        uint32_t f = Field<uint32_t>(caps, 0x40);
        uint32_t level = ((f & 0x200) && (f & 0x2000000)) ? 2 : 1;
        Field<uint32_t>(out, 0x110) = level;
        Field<uint32_t>(out, 0x114) = level;
    }
    uint32_t f = Field<uint32_t>(caps, 0x40);
    if (f & 0x20000)
    {
        Field<uint32_t>(out, 0x118) = 2;
        b1 |= 0x10;
    }
    else if (f & 0x10000)
    {
        Field<uint32_t>(out, 0x118) = 1;
        b1 |= 0x10;
    }
    else
    {
        b1 &= 0xef;
    }
    const uint32_t misc = Field<uint32_t>(caps, 0x20);
    const uint32_t texOp = Field<uint32_t>(caps, 0x90);
    SetBit(b0, 0x02, (misc >> 10) & 1);
    SetBit(b0, 0x04, (texOp >> 25) & 1);
    SetBit(b0, 0x08, caps[0x93] & 1);
    SetBit(b0, 0x10, (texOp >> 23) & 1);
    SetBit(b0, 0x20, (texOp >> 4) & 1);
    SetBit(b0, 0x40, (texOp >> 5) & 1);
    SetBit(b0, 0x80, (texOp >> 10) & 1);
    SetBit(b1, 0x01, (Field<uint32_t>(caps, 0x2c) & 0x100) && (caps[0x30] & 4));
    b1 = static_cast<uint8_t>((((misc >> 11) & 1 | 2) << 1) | (b1 & 0xfd));
    SetBit(b1, 0x08, (raster >> 18) & 1);
    SetBit(b2, 0x01, (raster >> 20) & 1);
    SetBit(b2, 0x02, caps[0x26] & 1);
    SetBit(b2, 0x04, (raster >> 8) & 1);
    SetBit(b2, 0x08, (raster >> 7) & 1);
    SetBit(b1, 0x80, (raster >> 13) & 1);
    SetBit(b2, 0x10, (Field<uint32_t>(caps, 0x34) >> 6) & 1);
    SetBit(b1, 0x20, (raster >> 26) & 1);
    SetBit(b1, 0x40, (raster >> 25) & 1);
    SetBit(b2, 0x80, (Field<uint32_t>(caps, 0xc4) & 0xffff) >= 0x101);
    SetBit(b2, 0x40, (Field<uint32_t>(caps, 0xcc) & 0xffff) >= 0x101);
    b2 |= 0x20;
    uint8_t c = b0;
    uint32_t mask = 0x4f409;
    if ((c & 0x10) && Field<uint32_t>(out, 0x10c) >= 3)
        mask = 0x4f40b;
    if ((c & 1) && Field<uint32_t>(out, 0x10c) >= 2)
        mask |= 4;
    if (c & 0x20)
        mask |= 0x20;
    if (c & 0x40)
        mask |= 0x10;
    uint8_t a = b1;
    if (a & 8)
        mask |= 0x100;
    if (a & 0x20)
        mask |= 0x80;
    if (a & 1)
        mask |= 0x40;
    if (c & 0x80)
        mask |= 0x800;
    Field<uint32_t>(out, 0) = mask;
    return mask;
}

// 0x00508ae0 (al = windowed; [renderer, width, height, bpp]): the desktop mode (+0x1354) when windowed, else
// the first exact match in the mode list (+0x9f4, count +0x9f0) or the closest mode by bpp, width, height
// differences (Format_BitsPerPixel on each mode's format). Mode 0 when nothing qualifies.
D3DDISPLAYMODE* Renderer_FindDisplayMode(uint8_t windowed, Renderer* r, int32_t width, int32_t height, int32_t bpp)
{
    if (windowed)
        return &r->desktopMode;
    uint32_t count = r->modeCount;
    uint32_t best = 0;
    for (uint32_t i = 0; i < count; ++i)
    {
        const D3DDISPLAYMODE& m = r->modes[i];
        int32_t dW = static_cast<int32_t>(m.Width) - width;
        int32_t dH = static_cast<int32_t>(m.Height) - height;
        int32_t dB = static_cast<int32_t>(orig::fn_00508750(m.Format)) - bpp;
        if (dW == 0 && dH == 0 && dB == 0)
        {
            best = i;
            break;
        }
        const D3DDISPLAYMODE& b = r->modes[best];
        int32_t bW = static_cast<int32_t>(b.Width) - width;
        int32_t bH = static_cast<int32_t>(b.Height) - height;
        int32_t bB = static_cast<int32_t>(orig::fn_00508750(b.Format)) - bpp;
        if (dW == bW && dH == bH && dB == bB)
            continue;
        if (dB > 0 ? dB > bB : dB < bB)
            continue;
        if (dW > 0 ? dW > bW : dW < bW)
            continue;
        if (dH > 0 ? dH > bH : dH < bH)
            continue;
        best = i;
    }
    return &r->modes[best];
}

// 0x00508ce0 (eax = width, ecx = height, edx = bpp, each kept at +0x14d0..+0x14d8 when non-zero; [renderer,
// windowed request]): store the request in +0x14dc bit 0, windowed = request and +0x1482 bit 5 (kept in +0x1628
// bit 5), pick the mode (0x00508ae0), resize (0x005080d0), choose the first depth format from 0x005895b4 the
// adapter supports with the mode (D16 otherwise) and fill the present parameters. Returns the back buffer height.
uint32_t Renderer_SetupPresentParams(uint32_t width, uint32_t height, uint32_t bpp, Renderer* r, uint32_t windowed)
{
    if (width)
        Field<uint32_t>(r, 0x14d0) = width;
    if (height)
        Field<uint32_t>(r, 0x14d4) = height;
    if (bpp)
        Field<uint32_t>(r, 0x14d8) = bpp;
    uint8_t& opts = Field<uint8_t>(r, 0x14dc);
    opts = static_cast<uint8_t>(opts ^ ((opts ^ static_cast<uint8_t>(windowed)) & 1));
    bool isWindowed = (Field<uint8_t>(r, 0x1482) & 0x20) && (opts & 1);
    uint8_t& state = Field<uint8_t>(r, 0x1628);
    state = static_cast<uint8_t>(state ^ (((isWindowed ? 1 : 0) << 5 ^ state) & 0x20));
    uint32_t h = Field<uint32_t>(r, 0x14d4);
    uint32_t w = Field<uint32_t>(r, 0x14d0);
    D3DDISPLAYMODE* mode = Renderer_FindDisplayMode((state >> 5) & 1, r, w, h, Field<int32_t>(r, 0x14d8));
    orig::fn_005080d0(u(r), 1, w, h);
    const D3DFORMAT* depthFormats = reinterpret_cast<const D3DFORMAT*>(0x005895b4);
    D3DFORMAT depth = static_cast<D3DFORMAT>(0x50);  // D3DFMT_D16
    for (uint32_t i = 0; i < 6; ++i)
    {
        if (r->d3d->CheckDeviceFormat(0, static_cast<D3DDEVTYPE>(1), mode->Format, 2, static_cast<D3DRESOURCETYPE>(1),
                                      depthFormats[i]) != 0)
            continue;
        if (r->d3d->CheckDepthStencilMatch(0, static_cast<D3DDEVTYPE>(1), mode->Format, mode->Format,
                                           depthFormats[i]) != 0)
            continue;
        if (depthFormats[i])
            depth = depthFormats[i];
        break;
    }
    D3DPRESENT_PARAMETERS& pp = r->presentParams;
    pp.SwapEffect = static_cast<D3DSWAPEFFECT>(1);
    pp.EnableAutoDepthStencil = 1;
    pp.BackBufferCount = 2;
    pp.AutoDepthStencilFormat = depth;
    uint32_t win = (state >> 5) & 1;
    pp.Windowed = win;
    pp.MultiSampleType = static_cast<D3DMULTISAMPLE_TYPE>(0);
    pp.hDeviceWindow = r->deviceWindow;
    pp.FullScreen_RefreshRateInHz = 0;
    uint32_t result;
    if (win)
    {
        pp.BackBufferWidth = Field<uint32_t>(r, 0x9d8) - Field<uint32_t>(r, 0x9d0);
        result = Field<uint32_t>(r, 0x9dc) - Field<uint32_t>(r, 0x9d4);
    }
    else
    {
        pp.BackBufferWidth = mode->Width;
        result = mode->Height;
    }
    pp.BackBufferHeight = result;
    pp.BackBufferFormat = mode->Format;
    return result;
}

// 0x00508e80 (eax = renderer; [unused]): enumerate display modes, set up the present parameters, choose
// hardware (+0x1482 bit 7 and option bit 1: pool 0, usage WRITEONLY) or software vertex processing (pool 2,
// usage DYNAMIC), then CreateDevice. Out of video memory, or under 12 MB of texture memory: retry at 640x480,
// then 640x480x16; on other errors log, show "Could not initialize Direct3DDevice" and exit(-1). Then resize
// (0x005080d0), 0x00503b30 and the state cache (0x00506ed0). Returns 0.
uint32_t Renderer_CreateDevice(Renderer* r, uint32_t /*unused*/)
{
    orig::fn_005089d0(u(r));
    Renderer_SetupPresentParams(Field<uint32_t>(r, 0x14d0), Field<uint32_t>(r, 0x14d4), Field<uint32_t>(r, 0x14d8), r,
                                Field<uint8_t>(r, 0x14dc) & 1);
    DWORD behavior;
    if ((Field<uint8_t>(r, 0x1482) & 0x80) && (Field<uint8_t>(r, 0x14dc) & 2))
    {
        behavior = 0x40;
        r->bufferPool = static_cast<D3DPOOL>(0);
        r->bufferUsage = 8;
    }
    else
    {
        behavior = 0x20;
        r->bufferPool = static_cast<D3DPOOL>(2);
        r->bufferUsage = 0x10;
    }
    behavior |= 4;
    const HRESULT kOutOfVideoMemory = static_cast<HRESULT>(0x8876017c);
    bool fail = false;
    for (;;)
    {
        HRESULT hr = r->d3d->CreateDevice(r->field_9e8, static_cast<D3DDEVTYPE>(r->field_9ec), r->deviceWindow,
                                          behavior, &r->presentParams, &r->device);
        if (hr != kOutOfVideoMemory)
        {
            if (hr != 0)
            {
                auto name = reinterpret_cast<const char*>(orig::fn_004e0d20(static_cast<uint32_t>(hr)));
                GameLog(Str(kStrDXError), name, Str(kStrCreateDevice));
                fail = true;
                break;
            }
            if (r->device && r->device->GetAvailableTextureMem() >= 0xc00000)
                break;
            r->device->Release();
        }
        if (Field<uint32_t>(r, 0x14d0) == 0x280 && Field<uint32_t>(r, 0x14d4) == 0x1e0)
        {
            if (Field<uint32_t>(r, 0x14d8) == 0x10)
            {
                fail = true;
                break;
            }
            Renderer_SetupPresentParams(0x280, 0x1e0, 0x10, r, Field<uint8_t>(r, 0x14dc) & 1);
        }
        else
        {
            Renderer_SetupPresentParams(0x280, 0x1e0, Field<uint32_t>(r, 0x14d8), r, Field<uint8_t>(r, 0x14dc) & 1);
        }
    }
    if (fail)
    {
        auto messageBox = *reinterpret_cast<int(__stdcall**)(HWND, LPCSTR, LPCSTR, UINT)>(0x0056b220);
        messageBox(r->deviceWindow, Str(kStrCouldNotInitDevice), Str(kStrError), 0);
        GameFunction<void(__cdecl*)(int)>(0x0053a117)(-1);  // exit
    }
    orig::fn_005080d0(u(r), 1, 0, 0);
    orig::fn_00503b30(u(r));
    orig::fn_00506ed0(u(r));
    return 0;
}

// 0x00509eb0 (eax = renderer; [texture, width, height, max levels, format]): create the D3D texture for a
// texture object: virtual 74, take a bucket (0x0050b330), resolve format and class (0x0050b1f0, 0x0050ab40,
// 0x0050b2b0 or the table at 0x005ca600), clamp the size to +0x14e4..+0x14f0, compute levels, then
// CreateTexture or CreateCubeTexture (word +0x22 bit 5). On success marks it created, updates LOD and adds
// its byte size (0x00509e80) to +0x4a8, returning the size; otherwise releases the bucket (0x0050bcc0), 0.
uint32_t Renderer_CreateTexture(Renderer* r, uint8_t* tex, uint32_t width, uint32_t height, uint32_t maxLevels,
                                uint32_t format)
{
    VCall<uint32_t>(r, 0x128 / 4, tex, 0u);
    uint8_t* bucket = reinterpret_cast<uint8_t*>(orig::fn_0050b330(u(r)));
    if (!bucket)
        return 0;
    Field<uint8_t*>(bucket, 8) = tex;
    Field<uint8_t*>(tex, 0xc) = bucket;
    uint32_t usage, pool;
    bool haveClass = false;  // reached the format/class resolution with only the class (path through 0x509f3f)
    bool skipClass = false;  // flags 0xc0: straight to creation (0x509f84)
    if (tex[0x22] & 0x80)
    {
        usage = 1;
        pool = 0;
    }
    else
    {
        pool = 1;
        usage = 0;
        if (tex[0x22] & 0x40)
            format = 0x15;
    }
    if (format != 0)
    {
        uint32_t cls = orig::fn_0050b1f0(format, u(r));
        Field<uint32_t>(bucket, 0x1c) = cls;
        if (tex[0x22] & 0xc0)
        {
            skipClass = true;
        }
        else
        {
            Field<uint32_t>(bucket, 0x1c) = orig::fn_0050ab40(cls, 0, u(r));
            haveClass = true;
        }
    }
    else
    {
        Field<uint32_t>(bucket, 0x1c) = Field<uint32_t>(tex, 0x1c);
        haveClass = true;
    }
    if (haveClass && !skipClass)
    {
        uint32_t cls = orig::fn_0050ab40(Field<uint32_t>(bucket, 0x1c), (tex[0x22] >> 7) & 1, u(r));
        Field<uint32_t>(bucket, 0x1c) = cls;
        if (cls == 0xf)
        {
            orig::fn_0050bcc0(0, u(bucket), u(r));
            return 0;
        }
        uint32_t chosen;
        if (tex[0x22] & 0x80)
            chosen = orig::fn_0050b2b0(cls, u(r), Field<uint16_t>(tex, 0x22));
        else
            chosen = GameGlobal<uint32_t>(0x005ca600 + cls * 8);
        format = chosen;
        if (chosen == 0)
        {
            orig::fn_0050bcc0(0, u(bucket), u(r));
            return 0;
        }
    }
    Field<uint32_t>(tex, 0x1c) = orig::fn_0050b1f0(format, u(r));
    if (width == 0 || height == 0)
    {
        VCall<uint32_t>(r, 0x138 / 4, tex, width != 0 ? nullptr : &width, height != 0 ? nullptr : &height);
    }
    {
        uint32_t lo = Field<uint32_t>(r, 0x14e4), hi = Field<uint32_t>(r, 0x14e8);
        uint32_t m = width > lo ? width : lo;
        if (m >= hi)
            width = hi;
        else if (!(width > lo))
            width = lo;
    }
    {
        uint32_t lo = Field<uint32_t>(r, 0x14ec), hi = Field<uint32_t>(r, 0x14f0);
        uint32_t m = height > lo ? height : lo;
        if (m >= hi)
            height = hi;
        else if (!(height > lo))
            height = lo;
    }
    uint16_t flags = Field<uint16_t>(tex, 0x22);
    uint32_t levels;
    if ((flags & 0x100) && (Field<uint8_t>(r, 0x15fd) & 0x10))
    {
        uint32_t smaller = width < height ? width : height;
        levels = static_cast<uint32_t>(ExactLog2(static_cast<int32_t>(smaller)) + 1);
    }
    else
    {
        levels = 1;
    }
    if (flags & 1)
    {
        Field<uint32_t>(tex, 0x10) = width;
        Field<uint32_t>(tex, 0x14) = height;
        levels = 1;
    }
    else
    {
        Field<int32_t>(tex, 0x10) = ExactLog2(static_cast<int32_t>(width));
        Field<int32_t>(tex, 0x14) = ExactLog2(static_cast<int32_t>(height));
    }
    if (maxLevels != 0 && levels >= maxLevels)
        levels = maxLevels;
    HRESULT hr;
    if (flags & 0x20)
    {
        auto pp = &Field<IDirect3DCubeTexture9*>(bucket, 0x14);
        hr = r->device->CreateCubeTexture(width, levels, usage, static_cast<D3DFORMAT>(format),
                                          static_cast<D3DPOOL>(pool), pp, nullptr);
        if (hr != 0)
        {
            auto name = reinterpret_cast<const char*>(orig::fn_004e0d20(static_cast<uint32_t>(hr)));
            GameLog(Str(kStrDXError), name, Str(kStrCreateCubeTexture));
        }
        Field<uint32_t>(bucket, 0xc) = Field<uint32_t>(bucket, 0x14);
    }
    else
    {
        auto pp = &Field<IDirect3DTexture9*>(bucket, 0x10);
        hr = r->device->CreateTexture(width, height, levels, usage, static_cast<D3DFORMAT>(format),
                                      static_cast<D3DPOOL>(pool), pp, nullptr);
        if (hr != 0)
        {
            auto name = reinterpret_cast<const char*>(orig::fn_004e0d20(static_cast<uint32_t>(hr)));
            GameLog(Str(kStrDXError), name, Str(kStrCreateTexture));
        }
        Field<uint32_t>(bucket, 0xc) = Field<uint32_t>(bucket, 0x10);
    }
    if (hr != 0)
    {
        orig::fn_0050bcc0(Field<uint32_t>(bucket, 0xc) & 0xffffff00, u(bucket), u(r));
        return 0;
    }
    tex[0x23] |= 0x80;
    orig::fn_0050a510(u(r), u(bucket));
    uint32_t faces = (tex[0x22] & 0x20) ? 6 : 1;
    uint32_t bpp = orig::fn_00508750(format);
    uint32_t size = orig::fn_00509e80(bpp, levels, width, height, faces);
    Field<uint32_t>(bucket, 0x20) = size;
    Field<uint32_t>(r, 0x4a8) += size;
    return size;
}

// 0x0050a290 (esi = renderer; ecx unused): for the 15 format pairs at 0x005ca600 and each of 4 combinations
// (bit 0: second format of the pair, bit 1: RENDERTARGET usage) store the format at +0x14f4 + 4 * index when
// CheckDeviceFormat accepts it as a texture, except plain-texture entries of classes 0-2 when +0x166c < 2 and of
// classes 3-5 when it is 0. Returns the end of the table (+0x15e4).
uint32_t Renderer_ProbeTextureFormats(Renderer* r)
{
    uint32_t* entry = &Field<uint32_t>(r, 0x14f4);
    const uint32_t* table = reinterpret_cast<const uint32_t*>(0x005ca600);
    for (int32_t base = 0; base < 0x1e; base += 2)
    {
        for (int32_t i = 0; i < 4; ++i, ++entry)
        {
            const uint32_t* candidate = &table[(i & 1) + base];
            *entry = 0;
            if (*candidate == 0)
                continue;
            HRESULT hr = r->d3d->CheckDeviceFormat(r->field_9e8, static_cast<D3DDEVTYPE>(r->field_9ec),
                                                   r->presentParams.BackBufferFormat,
                                                   (static_cast<uint32_t>(i) >> 1) & 1,
                                                   static_cast<D3DRESOURCETYPE>(3), static_cast<D3DFORMAT>(*candidate));
            bool ok = hr >= 0;
            if (!ok)
                continue;
            if (i < 2)
            {
                uint32_t level = Field<uint32_t>(r, 0x166c);
                if (level < 2 && (base == 0 || base == 2 || base == 4))
                    ok = false;
                if (level < 1 && (base == 6 || base == 8 || base == 10))
                    continue;
            }
            if (ok)
                *entry = *candidate;
        }
    }
    return u(entry);
}

static uint32_t Renderer_BuildColorMixPass_Abi(uint32_t r, uint32_t flags)
{
    return u(Renderer_BuildColorMixPass(reinterpret_cast<Renderer*>(r), flags));
}
static uint32_t Renderer_ParseOptions_Abi(uint32_t r, uint32_t options)
{
    return Renderer_ParseOptions(reinterpret_cast<Renderer*>(r), reinterpret_cast<const char*>(options));
}
static uint32_t Renderer_SummarizeCaps_Abi(uint32_t eax, uint32_t edx, uint32_t unused)
{
    return Renderer_SummarizeCaps(reinterpret_cast<const uint8_t*>(eax), reinterpret_cast<uint8_t*>(edx), unused);
}
static uint32_t Renderer_FindDisplayMode_Abi(uint32_t eax, uint32_t r, uint32_t w, uint32_t h, uint32_t bpp)
{
    return u(Renderer_FindDisplayMode(static_cast<uint8_t>(eax), reinterpret_cast<Renderer*>(r), static_cast<int32_t>(w),
                                      static_cast<int32_t>(h), static_cast<int32_t>(bpp)));
}
static uint32_t Renderer_SetupPresentParams_Abi(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t r, uint32_t windowed)
{
    return Renderer_SetupPresentParams(eax, ecx, edx, reinterpret_cast<Renderer*>(r), windowed);
}
static uint32_t Renderer_CreateDevice_Abi(uint32_t eax, uint32_t unused)
{
    return Renderer_CreateDevice(reinterpret_cast<Renderer*>(eax), unused);
}
static uint32_t Renderer_CreateTexture_Abi(uint32_t eax, uint32_t tex, uint32_t w, uint32_t h, uint32_t levels,
                                           uint32_t format)
{
    return Renderer_CreateTexture(reinterpret_cast<Renderer*>(eax), reinterpret_cast<uint8_t*>(tex), w, h, levels,
                                  format);
}
static uint32_t Renderer_ProbeTextureFormats_Abi(uint32_t /*ecx*/, uint32_t esi)
{
    return Renderer_ProbeTextureFormats(reinterpret_cast<Renderer*>(esi));
}
ABI_ENTRY_00506210(Renderer_BuildColorMixPass_Entry, Renderer_BuildColorMixPass_Abi)
ABI_ENTRY_00508290(Renderer_ParseOptions_Entry, Renderer_ParseOptions_Abi)
ABI_ENTRY_00508370(Renderer_SummarizeCaps_Entry, Renderer_SummarizeCaps_Abi)
ABI_ENTRY_00508ae0(Renderer_FindDisplayMode_Entry, Renderer_FindDisplayMode_Abi)
ABI_ENTRY_00508ce0(Renderer_SetupPresentParams_Entry, Renderer_SetupPresentParams_Abi)
ABI_ENTRY_00508e80(Renderer_CreateDevice_Entry, Renderer_CreateDevice_Abi)
ABI_ENTRY_00509eb0(Renderer_CreateTexture_Entry, Renderer_CreateTexture_Abi)
ABI_ENTRY_0050a290(Renderer_ProbeTextureFormats_Entry, Renderer_ProbeTextureFormats_Abi)
HOOK(0x00506210, "Renderer_BuildColorMixPass", Renderer_BuildColorMixPass_Entry, nullptr);
HOOK(0x00508290, "Renderer_ParseOptions", Renderer_ParseOptions_Entry, nullptr);
HOOK(0x00508370, "Renderer_SummarizeCaps", Renderer_SummarizeCaps_Entry, nullptr);
HOOK(0x00508ae0, "Renderer_FindDisplayMode", Renderer_FindDisplayMode_Entry, nullptr);
HOOK(0x00508ce0, "Renderer_SetupPresentParams", Renderer_SetupPresentParams_Entry, nullptr);
HOOK(0x00508e80, "Renderer_CreateDevice", Renderer_CreateDevice_Entry, nullptr);
HOOK(0x00509eb0, "Renderer_CreateTexture", Renderer_CreateTexture_Entry, nullptr);
HOOK(0x0050a290, "Renderer_ProbeTextureFormats", Renderer_ProbeTextureFormats_Entry, nullptr);
