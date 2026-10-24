// Recovered: dynamic vertex/index buffer management (gfx\d3d\buffer.cpp). S2, Approach B.
//   0x004fe310 VertexBufferSet_Restore
//   0x004fe3a0 IndexBuffer_Restore
#include "gen/entry_stubs.h"
#include "hooks.h"
#include "renderer.h"

// 0x004fe310 (esi = set; ecx unused): reset the cycling state and (re)create every missing buffer of the set.
// Returns 1 if any creation failed.
uint32_t VertexBufferSet_Restore(VertexBufferSet* set)
{
    set->field_1c = 4;
    set->field_20 = 4;
    set->field_24 = 3;
    set->current = 0;
    set->field_6c = 0x800;
    uint32_t failed = 0;
    if (set->sizeBytes != 0 && set->count != 0)
    {
        for (uint32_t i = set->count; i != 0; i--)
        {
            IDirect3DVertexBuffer9** slot = &set->buffers[i - 1];
            if (*slot)
                continue;
            Renderer* r = set->renderer;
            HRESULT hr = r->device->CreateVertexBuffer(set->sizeBytes, r->bufferUsage | D3DUSAGE_DYNAMIC, 0,
                                                       r->bufferPool, slot, nullptr);
            failed |= hr < 0;
        }
    }
    return failed;
}

// 0x004fe3a0 (edx = buffer): create the index buffer if missing. Returns 1 on failure.
uint32_t IndexBuffer_Restore(IndexBuffer* ib)
{
    HRESULT hr = 0;
    if (!ib->buffer)
    {
        Renderer* r = ib->renderer;
        hr = r->device->CreateIndexBuffer(ib->indexCount * 2, r->bufferUsage | D3DUSAGE_DYNAMIC, D3DFMT_INDEX16,
                                          r->bufferPool, &ib->buffer, nullptr);
    }
    return hr < 0;
}

static uint32_t VertexBufferSet_Restore_Abi(uint32_t /*ecx*/, uint32_t esi)
{
    return VertexBufferSet_Restore(reinterpret_cast<VertexBufferSet*>(esi));
}
static uint32_t IndexBuffer_Restore_Abi(uint32_t edx)
{
    return IndexBuffer_Restore(reinterpret_cast<IndexBuffer*>(edx));
}
ABI_ENTRY_004fe310(VertexBufferSet_Restore_Entry, VertexBufferSet_Restore_Abi)
ABI_ENTRY_004fe3a0(IndexBuffer_Restore_Entry, IndexBuffer_Restore_Abi)
HOOK(0x004fe310, "VertexBufferSet_Restore", VertexBufferSet_Restore_Entry, nullptr);
HOOK(0x004fe3a0, "IndexBuffer_Restore", IndexBuffer_Restore_Entry, nullptr);
