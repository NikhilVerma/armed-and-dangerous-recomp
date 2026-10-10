// Asset overrides. Switches in [overrides] in and.ini, off in the original; textures is on in the remaster (a folder
// without files changes nothing).
//
//   textures=1     replace textures by loose files: overrides\textures\<name>.dds or <name>.png next to the game
//                  executable, <name> being the texture's name in the level's txtr .big (characters that cannot
//                  be in a file name become '_'). DDS: DXT1/3/5 or 32/16-bit RGB, any size, with or without
//                  mipmaps. PNG: any size, mipmaps generated.
//   models=1       replace models (skins) by loose files: overrides\models\<name>.skn, a skin chunk as written by
//                  tools/formats/bigtool.py dump (skins\*.skn) from any level. It is renamed to <name> on load.
//   log_names=1    log every texture and model name as the game loads it (to find what to name a file)
//
// How: the game registers every texture record with renderer virtual 73 (Renderer_RegisterTexture 0x0050bb30,
// src/renderer/textures.cpp), which creates the D3D texture and uploads the .big pixels. This wraps it and, when a
// file matches, swaps the created texture in the record's bucket (+0x0c; bucket +0x0c and +0x10 hold the
// texture) for one made from the file. Size and format may differ from the original: the game samples by
// normalised coordinates and never reads the pixels back.
#include <windows.h>
#include <d3d9.h>
#include <wincodec.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "config.h"
#include "hooks.h"
#include "log.h"
#include "renderer/renderer.h"

extern uint32_t (*g_skinChunkOverride)(uint32_t chunk);  // src/loader/auto_00410ec0.cpp (ObjectSystem_LoadSkins)

namespace
{
using RegisterTexture_t = uint32_t(__fastcall*)(Renderer* r, void* edx, uint8_t* tex);
RegisterTexture_t g_nextRegister;
bool g_textures, g_logNames;
std::string g_dir;

std::string SafeName(const char* name)
{
    std::string s;
    for (const char* p = name; *p; ++p)
        s += (strchr("<>:\"/\\|?*", *p) || static_cast<unsigned char>(*p) < 32) ? '_' : *p;
    return s;
}

bool ReadFileBytes(const std::string& path, std::vector<uint8_t>& out)
{
    HANDLE f = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return false;
    DWORD size = GetFileSize(f, nullptr), got = 0;
    out.resize(size);
    BOOL ok = ReadFile(f, out.data(), size, &got, nullptr);
    CloseHandle(f);
    return ok && got == size;
}

uint32_t U32(const std::vector<uint8_t>& d, size_t o)
{
    uint32_t v;
    memcpy(&v, d.data() + o, 4);
    return v;
}

// Copy `rows` rows of `rowBytes` into a locked level.
void CopyLevel(IDirect3DTexture9* t, UINT level, const uint8_t* src, uint32_t rowBytes, uint32_t rows)
{
    D3DLOCKED_RECT lr;
    if (FAILED(t->LockRect(level, &lr, nullptr, 0)))
        return;
    for (uint32_t y = 0; y < rows; ++y)
        memcpy(static_cast<uint8_t*>(lr.pBits) + y * lr.Pitch, src + y * rowBytes, rowBytes);
    t->UnlockRect(level);
}

IDirect3DTexture9* LoadDds(IDirect3DDevice9* dev, const std::vector<uint8_t>& d, const std::string& path)
{
    if (d.size() < 128 || memcmp(d.data(), "DDS ", 4) != 0)
    {
        Log("overrides: %s is not a DDS file", path.c_str());
        return nullptr;
    }
    uint32_t height = U32(d, 12), width = U32(d, 16), mips = U32(d, 28);
    uint32_t pfFlags = U32(d, 80), fourCC = U32(d, 84), bits = U32(d, 88);
    uint32_t rMask = U32(d, 92), aMask = U32(d, 104);
    if (mips == 0 || !(U32(d, 8) & 0x20000))  // DDSD_MIPMAPCOUNT
        mips = 1;
    D3DFORMAT fmt = D3DFMT_UNKNOWN;
    uint32_t block = 0, bpp = 0;
    if (pfFlags & 4)  // DDPF_FOURCC
    {
        if (fourCC == MAKEFOURCC('D', 'X', 'T', '1'))
            fmt = D3DFMT_DXT1, block = 8;
        else if (fourCC == MAKEFOURCC('D', 'X', 'T', '3'))
            fmt = D3DFMT_DXT3, block = 16;
        else if (fourCC == MAKEFOURCC('D', 'X', 'T', '5'))
            fmt = D3DFMT_DXT5, block = 16;
    }
    else if (bits == 32)
        fmt = (pfFlags & 1) && aMask ? D3DFMT_A8R8G8B8 : D3DFMT_X8R8G8B8, bpp = 4;
    else if (bits == 16)
        fmt = rMask == 0xf800 ? D3DFMT_R5G6B5 : rMask == 0x7c00 ? D3DFMT_A1R5G5B5 : D3DFMT_A4R4G4B4, bpp = 2;
    if (fmt == D3DFMT_UNKNOWN || (bpp == 4 && rMask != 0x00ff0000))
    {
        Log("overrides: %s: unsupported DDS pixel format (flags 0x%x, fourCC 0x%08x, %u bits)", path.c_str(),
            pfFlags, fourCC, bits);
        return nullptr;
    }
    IDirect3DTexture9* t = nullptr;
    if (FAILED(dev->CreateTexture(width, height, mips, 0, fmt, D3DPOOL_MANAGED, &t, nullptr)))
        return nullptr;
    size_t at = 128;
    for (uint32_t level = 0; level < mips; ++level)
    {
        uint32_t w = width >> level ? width >> level : 1, h = height >> level ? height >> level : 1;
        uint32_t rowBytes = block ? ((w + 3) / 4) * block : w * bpp;
        uint32_t rows = block ? (h + 3) / 4 : h;
        if (at + size_t(rowBytes) * rows > d.size())
            break;
        CopyLevel(t, level, d.data() + at, rowBytes, rows);
        at += size_t(rowBytes) * rows;
    }
    return t;
}

IDirect3DTexture9* LoadPng(IDirect3DDevice9* dev, const std::string& path)
{
    HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IWICImagingFactory* wic = nullptr;
    IWICBitmapDecoder* dec = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* conv = nullptr;
    IDirect3DTexture9* t = nullptr;
    std::wstring wpath(path.begin(), path.end());
    UINT w = 0, h = 0;
    std::vector<uint8_t> px;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) &&
        SUCCEEDED(wic->CreateDecoderFromFilename(wpath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand,
                                                 &dec)) &&
        SUCCEEDED(dec->GetFrame(0, &frame)) && SUCCEEDED(wic->CreateFormatConverter(&conv)) &&
        SUCCEEDED(conv->Initialize(frame, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0,
                                   WICBitmapPaletteTypeCustom)) &&
        SUCCEEDED(conv->GetSize(&w, &h)))
    {
        px.resize(size_t(w) * h * 4);
        if (FAILED(conv->CopyPixels(nullptr, w * 4, static_cast<UINT>(px.size()), px.data())))
            px.clear();
    }
    if (conv) conv->Release();
    if (frame) frame->Release();
    if (dec) dec->Release();
    if (wic) wic->Release();
    if (SUCCEEDED(co))
        CoUninitialize();
    if (px.empty())
    {
        Log("overrides: could not decode %s", path.c_str());
        return nullptr;
    }
    UINT levels = 1;
    while ((w >> levels) || (h >> levels))
        ++levels;
    if (FAILED(dev->CreateTexture(w, h, levels, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &t, nullptr)))
        return nullptr;
    // Box-filter each level from the previous one.
    for (UINT level = 0; level < levels; ++level)
    {
        CopyLevel(t, level, px.data(), w * 4, h);
        UINT nw = w > 1 ? w / 2 : 1, nh = h > 1 ? h / 2 : 1;
        std::vector<uint8_t> next(size_t(nw) * nh * 4);
        for (UINT y = 0; y < nh; ++y)
            for (UINT x = 0; x < nw; ++x)
                for (int c = 0; c < 4; ++c)
                {
                    UINT x0 = x * 2 < w ? x * 2 : w - 1, x1 = x * 2 + 1 < w ? x * 2 + 1 : w - 1;
                    UINT y0 = y * 2 < h ? y * 2 : h - 1, y1 = y * 2 + 1 < h ? y * 2 + 1 : h - 1;
                    next[(y * nw + x) * 4 + c] = static_cast<uint8_t>(
                        (px[(y0 * w + x0) * 4 + c] + px[(y0 * w + x1) * 4 + c] + px[(y1 * w + x0) * 4 + c] +
                         px[(y1 * w + x1) * 4 + c] + 2) / 4);
                }
        px.swap(next);
        w = nw;
        h = nh;
    }
    return t;
}

void ApplyTextureOverride(Renderer* r, uint8_t* tex)
{
    const char* name = *reinterpret_cast<const char**>(tex);
    if (!name || !*name)
        return;
    if (g_logNames)
        Log("overrides: texture %s", name);
    if (!g_textures)
        return;
    uint8_t* bucket = *reinterpret_cast<uint8_t**>(tex + 0xc);
    if (!bucket || tex[0x22] & 0x20)  // cube maps are not replaced
        return;
    auto& slot = *reinterpret_cast<IDirect3DTexture9**>(bucket + 0x10);
    if (!slot)
        return;
    std::string base = g_dir + SafeName(name);
    IDirect3DTexture9* t = nullptr;
    std::vector<uint8_t> bytes;
    std::string used;
    if (ReadFileBytes(base + ".dds", bytes))
        t = LoadDds(r->device, bytes, used = base + ".dds");
    else if (GetFileAttributesA((base + ".png").c_str()) != INVALID_FILE_ATTRIBUTES)
        t = LoadPng(r->device, used = base + ".png");
    if (!t)
        return;
    D3DSURFACE_DESC desc{};
    t->GetLevelDesc(0, &desc);
    slot->Release();
    slot = t;
    *reinterpret_cast<IDirect3DTexture9**>(bucket + 0xc) = t;
    Log("overrides: %s <- %s (%ux%u, %u levels)", name, used.c_str(), desc.Width, desc.Height, t->GetLevelCount());
}

// --- models ----------------------------------------------------------------------------------------------------
// A skin chunk (docs/formats/big-skin.md) is a relocatable heap image: +8 is the offset of its heap header, whose
// +0x20 is the root object's offset; the root holds the source path at +0x34 and the game's name for the skin is
// that path's file name without its last four characters (compare 0x0045ad40).

bool g_models;
std::string g_modelDir;

std::string SkinName(const uint8_t* chunk, size_t limit)
{
    uint32_t header = *reinterpret_cast<const uint32_t*>(chunk + 8);
    if (header + 0x24 > limit)
        return {};
    uint32_t root = *reinterpret_cast<const uint32_t*>(chunk + header + 0x20);
    if (root + 0x35 > limit)
        return {};
    const char* path = reinterpret_cast<const char*>(chunk + root + 0x34);
    std::string p(path, strnlen(path, limit - root - 0x34));
    std::string file = p.substr(p.find_last_of('\\') + 1);
    return file.size() > 4 ? file.substr(0, file.size() - 4) : std::string();
}

uint32_t SkinChunkOverride(uint32_t chunk)
{
    const auto* bank = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(chunk));
    std::string name = SkinName(bank, *reinterpret_cast<const uint32_t*>(bank));
    if (g_logNames)
        Log("overrides: model %s", name.c_str());
    if (!g_models || name.empty())
        return 0;
    std::string path = g_modelDir + SafeName(name.c_str()) + ".skn";
    std::vector<uint8_t> d;
    if (!ReadFileBytes(path, d))
        return 0;
    if (d.size() < 0x40 || *reinterpret_cast<const uint32_t*>(d.data()) != d.size())
    {
        Log("overrides: %s is not a skin chunk (size word does not match the file)", path.c_str());
        return 0;
    }
    std::string own = SkinName(d.data(), d.size());
    if (own.empty())
    {
        Log("overrides: %s: no skin name found", path.c_str());
        return 0;
    }
    // The game finds skins by the name in their path, so give the replacement the replaced skin's name. The
    // new path ("\<name>.SKN") is never longer than the old one is allowed to be: names are at most 31 chars.
    uint32_t header = *reinterpret_cast<uint32_t*>(d.data() + 8);
    uint32_t root = *reinterpret_cast<uint32_t*>(d.data() + header + 0x20);
    char* oldPath = reinterpret_cast<char*>(d.data() + root + 0x34);
    std::string newPath = "\\" + name + ".SKN";
    if (newPath.size() > strlen(oldPath))
    {
        Log("overrides: %s: cannot rename %s to %s (path too short)", path.c_str(), own.c_str(), name.c_str());
        return 0;
    }
    memset(oldPath, 0, strlen(oldPath));
    memcpy(oldPath, newPath.data(), newPath.size());
    // Kept for the rest of the process: the game keeps pointers into the opened chunk. 32-byte aligned like the
    // bank's chunks (VirtualAlloc gives page alignment).
    void* mem = VirtualAlloc(nullptr, d.size(), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!mem)
        return 0;
    memcpy(mem, d.data(), d.size());
    Log("overrides: model %s <- %s (%s, %u bytes)", name.c_str(), path.c_str(), own.c_str(),
        static_cast<unsigned>(d.size()));
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(mem));
}

uint32_t __fastcall Overrides_RegisterTexture(Renderer* r, void* edx, uint8_t* tex)
{
    uint32_t created = g_nextRegister(r, edx, tex);
    if (created && tex)
        ApplyTextureOverride(r, tex);
    return created;
}
}  // namespace

void OverridesInit()
{
    g_textures = ConfigIntDefault("overrides", "textures", 0, 1) != 0;  // remaster default on: texture packs load
    g_models = ConfigInt("overrides", "models", 0) != 0;
    g_logNames = ConfigInt("overrides", "log_names", 0) != 0;
    if (!g_textures && !g_models && !g_logNames)
        return;
    char exe[MAX_PATH];
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    std::string base(exe);
    base = base.substr(0, base.find_last_of("\\/") + 1) + "overrides\\";
    g_dir = base + "textures\\";
    g_modelDir = base + "models\\";
    if (g_models || g_logNames)
    {
        g_skinChunkOverride = SkinChunkOverride;
        Log("overrides: models %d (from %s)", g_models, g_modelDir.c_str());
    }
    HookWrap("Renderer_RegisterTexture", reinterpret_cast<void*>(Overrides_RegisterTexture),
             reinterpret_cast<void**>(&g_nextRegister));
    Log("overrides: textures %d (from %s), log_names %d", g_textures, g_dir.c_str(), g_logNames);
}
