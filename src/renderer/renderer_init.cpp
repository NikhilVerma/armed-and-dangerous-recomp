// Recovered: renderer start-up and pools. S2, Approach B.
//   0x005090e0 Renderer_Init
//   0x0050a200 TextureBuckets_Init
//   0x00506b60 RenderPasses_Init
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
constexpr uint32_t kHeap = 0x0063a210;  // the game's allocator object (0x004e07f0 alloc, 0x004e08d0 free)
const char* const kStrFatalGraphics = reinterpret_cast<const char*>(0x0058953c);
const char* const kStrTexResourceMem = reinterpret_cast<const char*>(0x00589444);  // GFX_TexResourceMem
const char* const kStrRenderPasses = reinterpret_cast<const char*>(0x00589730);
const char* const kStrRenderPassHashTree = reinterpret_cast<const char*>(0x0058971c);

void* GameAlloc(uint32_t size, const char* tag)
{
    return reinterpret_cast<void*>(orig::fn_004e07f0(kHeap, 1, size, 2, u(tag)));
}
uint32_t GameFree(void* p)
{
    return orig::fn_004e08d0(kHeap, u(p));
}
}  // namespace

// 0x0050a200 ([renderer]): empty free (+0x48c) and used (+0x494) bucket lists, then 1024 zeroed 0x24-byte
// buckets from GFX_TexResourceMem (+0x49c), all on the free list in order.
void __stdcall TextureBuckets_Init(Renderer* r)
{
    ListNode* freeList = &Field<ListNode>(r, 0x48c);
    ListNode* used = &Field<ListNode>(r, 0x494);
    freeList->next = freeList->prev = freeList;
    used->next = used->prev = used;
    auto* pool = static_cast<uint8_t*>(GameAlloc(0x9000, kStrTexResourceMem));
    Field<uint8_t*>(r, 0x49c) = pool;
    memset(pool, 0, 0x9000);
    if (!Field<uint8_t*>(r, 0x49c))
        return;
    auto* bucket = Field<uint8_t*>(r, 0x49c);
    for (int i = 0; i < 0x400; i++, bucket += 0x24)
    {
        memset(bucket + 8, 0, 7 * 4);
        auto* node = reinterpret_cast<ListNode*>(bucket);
        node->prev = node;
        node->next = node;
        node->prev = freeList->prev;
        node->next = freeList;
        freeList->prev = node;
        node->prev->next = node;
    }
}

// 0x00506b60 (al = enable, esi = renderer): with enable, allocate (once) the RenderPasses pool (+0x358/+0x35c,
// 0x5000 bytes) and the RenderPassHashTree (+0x354, 255 16-byte nodes chained into the free list +0x34c), and
// select the pixel-shader table (+0x3a0 = +0x360) when pixel shaders are available and enabled. Without enable,
// free both. Clears +0x34c/+0x350 first.
uint32_t RenderPasses_Init(bool enable, Renderer* r)
{
    Field<uint32_t>(r, 0x350) = 0;
    Field<uint32_t>(r, 0x34c) = 0;
    if (!enable)
    {
        if (Field<void*>(r, 0x358))
        {
            GameFree(Field<void*>(r, 0x358));
            Field<uint32_t>(r, 0x35c) = 0;
            Field<uint32_t>(r, 0x358) = 0;
        }
        uint32_t result = Field<uint32_t>(r, 0x354);
        if (Field<void*>(r, 0x354))
        {
            result = GameFree(Field<void*>(r, 0x354));
            Field<uint32_t>(r, 0x354) = 0;
        }
        return result;
    }
    if (!Field<void*>(r, 0x358))
    {
        void* passes = GameAlloc(0x5000, kStrRenderPasses);
        Field<void*>(r, 0x358) = passes;
        Field<void*>(r, 0x35c) = passes;
    }
    if (!Field<void*>(r, 0x354))
        Field<void*>(r, 0x354) = GameAlloc(0x1000, kStrRenderPassHashTree);
    memset(Field<void*>(r, 0x358), 0, 0x5000);
    memset(Field<void*>(r, 0x354), 0, 0x1000);
    auto* tree = Field<uint8_t*>(r, 0x354);
    memset(tree, 0, 0x1000);
    for (int i = 0; i < 0xff; i++)
        Field<uint8_t*>(tree, i * 0x10) = tree + (i + 1) * 0x10;
    Field<uint32_t>(tree, 0xff0) = Field<uint32_t>(r, 0x34c);
    Field<uint8_t*>(r, 0x34c) = tree;
    if ((Field<uint8_t>(r, 0x15fe) & 0x40) && (Field<uint8_t>(r, 0x14dc) & 4))
    {
        Field<uint8_t*>(r, 0x3a0) = &Field<uint8_t>(r, 0x360);
        return u(&Field<uint8_t>(r, 0x360));
    }
    Field<uint32_t>(r, 0x3a0) = 0;
    return 0;
}

// 0x005090e0 (eax = window, ecx = options string, esi = renderer, edi; [a0..a3] = display mode settings):
// parse renderer options (0x00508290), store the display mode (0x00509040), build the texture buckets, create
// IDirect3D9 and the device (0x00508e80). Logs and returns false if the device cannot be created; otherwise
// marks the renderer ready (+0x1628 bit 0), finishes set-up (0x00503b30) and returns true.
bool Renderer_Init(HWND window, uint32_t options, Renderer* r, uint32_t edi, uint32_t a0, uint32_t a1, uint32_t a2,
                   uint32_t a3)
{
    Field<HWND>(r, 0x9b0) = window;
    orig::fn_00508290(u(r), options);
    orig::fn_00509040(a0, a1, a2, edi, u(r), a3);
    TextureBuckets_Init(r);
    r->deviceWindow = Field<HWND>(r, 0x9b0);
    SetForegroundWindow(Field<HWND>(r, 0x9b0));
    // Through the game's import thunk, so hooks on Direct3DCreate9 apply as they do for the original.
    r->d3d = GameFunction<IDirect3D9*(WINAPI*)(UINT)>(0x00565518)(31);  // the 2003 SDK's D3D_SDK_VERSION
    if (static_cast<int32_t>(orig::fn_00508e80(u(r), u(Field<HWND>(r, 0x9b0)))) < 0)
    {
        GameLog(kStrFatalGraphics);
        return false;
    }
    Field<uint8_t>(r, 0x1628) |= 1;
    orig::fn_00503b30(u(r));
    return true;
}

static uint32_t Renderer_Init_Abi(uint32_t eax, uint32_t ecx, uint32_t esi, uint32_t edi, uint32_t a0, uint32_t a1,
                                  uint32_t a2, uint32_t a3)
{
    return Renderer_Init(reinterpret_cast<HWND>(eax), ecx, reinterpret_cast<Renderer*>(esi), edi, a0, a1, a2, a3);
}
static uint32_t TextureBuckets_Init_Abi(uint32_t renderer)
{
    TextureBuckets_Init(reinterpret_cast<Renderer*>(renderer));
    return 0;
}
static uint32_t RenderPasses_Init_Abi(uint32_t eax, uint32_t esi)
{
    return RenderPasses_Init((eax & 0xff) != 0, reinterpret_cast<Renderer*>(esi));
}
ABI_ENTRY_005090e0(Renderer_Init_Entry, Renderer_Init_Abi)
ABI_ENTRY_0050a200(TextureBuckets_Init_Entry, TextureBuckets_Init_Abi)
ABI_ENTRY_00506b60(RenderPasses_Init_Entry, RenderPasses_Init_Abi)
HOOK(0x005090e0, "Renderer_Init", Renderer_Init_Entry, nullptr);
HOOK(0x0050a200, "TextureBuckets_Init", TextureBuckets_Init_Entry, nullptr);
HOOK(0x00506b60, "RenderPasses_Init", RenderPasses_Init_Entry, nullptr);
