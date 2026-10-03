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

static uint32_t Renderer_CreateVertexShader_Abi(uint32_t eax, uint32_t /*ecx*/, uint32_t edx, uint32_t function)
{
    return reinterpret_cast<uint32_t>(Renderer_CreateVertexShader(
        reinterpret_cast<Renderer*>(eax), reinterpret_cast<const char*>(edx), reinterpret_cast<const DWORD*>(function)));
}
ABI_ENTRY_0050c510(Renderer_CreateVertexShader_Entry, Renderer_CreateVertexShader_Abi)
HOOK(0x0050c510, "Renderer_CreateVertexShader", Renderer_CreateVertexShader_Entry, nullptr);
