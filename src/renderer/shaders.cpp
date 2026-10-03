// Recovered: vertex shader creation and caching (shaders.dat). S2, Approach B.
//   0x0050c510 Renderer_CreateVertexShader
#include "gen/entry_stubs.h"
#include "gen/orig_calls.h"
#include "hooks.h"
#include "renderer.h"

uint32_t ParseHex(const char* text);  // src/util/parse.cpp

namespace
{
const char* const kStrDuplicateShader = reinterpret_cast<const char*>(0x005892f0);
}  // namespace

// 0x0050c510 (eax = renderer, edx = hex shader id; [function bytecode]): shader ids are 27-bit hex numbers.
// A binary trie at +0x344 (node: children +0/+4 chosen by successive id bits, id +8, shader +0xc) caches
// created shaders: a duplicate id is logged and the cached shader returned. Otherwise CreateVertexShader and,
// on success, insert it (0x00461ff0: edx = id, ebx = shader, edi = trie at +0x340). Returns the shader or null.
IDirect3DVertexShader9* Renderer_CreateVertexShader(Renderer* r, const char* idText, const DWORD* function)
{
    uint32_t id = ParseHex(idText);
    if ((id & 0x7ffffff) != id)
        return nullptr;
    uint32_t bit = 0;
    for (uint8_t* node = Field<uint8_t*>(r, 0x344); node; bit++)
    {
        if (Field<uint32_t>(node, 8) == id)
        {
            GameLog(kStrDuplicateShader, id);
            return Field<IDirect3DVertexShader9*>(node, 0xc);
        }
        node = Field<uint8_t*>(node, ((id >> bit) & 1) * 4);
    }
    IDirect3DVertexShader9* shader = nullptr;
    if (r->device->CreateVertexShader(function, &shader) == 0)
        orig::fn_00461ff0(id, reinterpret_cast<uint32_t>(shader), reinterpret_cast<uint32_t>(&Field<uint8_t>(r, 0x340)));
    return shader;
}

// 0x0050c5c0 (eax = shader name, ecx = renderer; [function bytecode]): the renderer's seven named pixel
// shaders, created on first request into fixed slots (+0x360..+0x378). Null without pixel shader support
// (+0x15fe bit 6), for an unknown name, or when creation fails (logged).
IDirect3DPixelShader9* Renderer_GetNamedPixelShader(const char* name, Renderer* r, const DWORD* function)
{
    static const struct
    {
        uint32_t nameAddress;  // string in the game image
        uint32_t slot;
    } kShaders[] = {
        {0x005892e0, 0x360},  // mixtexplusenv
        {0x005892d4, 0x364},  // alphashadow
        {0x005892cc, 0x368},
        {0x005892c0, 0x36c},  // bumpspec
        {0x005892b8, 0x370},  // mixbump
        {0x005892b0, 0x374},  // distort
        {0x005892a4, 0x378},  // depthmod
    };
    if (!(Field<uint8_t>(r, 0x15fe) & 0x40))
        return nullptr;
    auto stricmp = GameFunction<int(__cdecl*)(const char*, const char*)>(0x005448ee);  // the game's CRT
    IDirect3DPixelShader9** slot = nullptr;
    for (const auto& s : kShaders)
    {
        if (stricmp(name, reinterpret_cast<const char*>(s.nameAddress)) == 0)
        {
            slot = &Field<IDirect3DPixelShader9*>(r, s.slot);
            break;
        }
    }
    if (!slot)
        return nullptr;
    if (!*slot && r->device->CreatePixelShader(function, slot) < 0)
    {
        GameLog(reinterpret_cast<const char*>(0x00589270));  // "CreatePixelShader(): Could not create ..."
        return nullptr;
    }
    return *slot;
}

static uint32_t Renderer_GetNamedPixelShader_Abi(uint32_t eax, uint32_t ecx, uint32_t function)
{
    return reinterpret_cast<uint32_t>(Renderer_GetNamedPixelShader(
        reinterpret_cast<const char*>(eax), reinterpret_cast<Renderer*>(ecx), reinterpret_cast<const DWORD*>(function)));
}
ABI_ENTRY_0050c5c0(Renderer_GetNamedPixelShader_Entry, Renderer_GetNamedPixelShader_Abi)
HOOK(0x0050c5c0, "Renderer_GetNamedPixelShader", Renderer_GetNamedPixelShader_Entry, nullptr);

static uint32_t Renderer_CreateVertexShader_Abi(uint32_t eax, uint32_t /*ecx*/, uint32_t edx, uint32_t function)
{
    return reinterpret_cast<uint32_t>(Renderer_CreateVertexShader(
        reinterpret_cast<Renderer*>(eax), reinterpret_cast<const char*>(edx), reinterpret_cast<const DWORD*>(function)));
}
ABI_ENTRY_0050c510(Renderer_CreateVertexShader_Entry, Renderer_CreateVertexShader_Abi)
HOOK(0x0050c510, "Renderer_CreateVertexShader", Renderer_CreateVertexShader_Entry, nullptr);
