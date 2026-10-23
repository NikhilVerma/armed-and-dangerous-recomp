// Recovered: game-side rendering below the scene pass (batch r4, 0x00487e30-0x004f0b00). Static recovery 2026-10-05,
// compile-checked only (the game was not run); the oracle (tools/compare.py) is still to do.
//
// Conventions: every function's comment gives its address, its register/stack ABI (as the original reads it), what it
// does and the evidence. "guess" marks interpretations not proven by strings or behaviour. Names stay fn_XXXXXXXX
// unless the behaviour was verified from the disassembly.
#include <cmath>
#include <cstring>
#include <initializer_list>

#include "game.h"
#include "game/game_object.h"
#include "gen/entry_stubs.h"
#include "gen/orig_calls.h"
#include "hooks.h"
#include "renderer/scene_pass.h"

namespace
{
template <class T>
uint32_t u(T* p)
{
    return reinterpret_cast<uint32_t>(p);
}
inline void* P(uint32_t v)
{
    return reinterpret_cast<void*>(v);
}
inline uint8_t* B(uint32_t v)
{
    return reinterpret_cast<uint8_t*>(v);
}
inline uint32_t fb(float f)
{
    uint32_t v;
    std::memcpy(&v, &f, 4);
    return v;
}
inline float bf(uint32_t v)
{
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}
// A float constant of the game's .rdata (0x005bcb2c = 1.0, 0x005bcb34 = 255.0, 0x005bcb4c = 0.5, 0x005bcbe4 = 0.25,
// 0x005bcbe0 = 4.0, 0x005bcbe8 = 20.0, 0x005bcb80 = 2.0, 0x005bcb7c = 3.0, 0x005bcc18 = 1/255, 0x005bccfc = 0.6,
// 0x005bcd80 = 1e-10, 0x0056c05c = 0.0).
inline float K(uint32_t address)
{
    return GameGlobal<float>(address);
}
inline void* Renderer()
{
    return Field<void*>(g_game, 0x38);
}
// 0x004e08d0 (ecx = heap 0x0063a210; stack: block): the game's free.
inline uint32_t Free(uint32_t block)
{
    return orig::fn_004e08d0(0x0063a210u, block);
}
// 0x0056a7c8 (_ftol): truncates st0 to a 64-bit integer; callers use EAX.
inline int32_t Ftol(float x)
{
    return static_cast<int32_t>(static_cast<int64_t>(x));
}
}  // namespace

// Prototypes (functions of this file call each other directly).
uint32_t fn_00487e30(uint32_t object, uint32_t* state);
uint32_t ScenePass_Release(uint8_t* pass);
uint32_t fn_004880d0(uint8_t* pass);
uint32_t fn_00488230(uint8_t* pass);
uint32_t fn_00488850(uint8_t* pass);
uint32_t fn_00489910(uint8_t* pass);
uint32_t fn_004897d0(uint32_t* state, uint8_t* material, uint32_t arg0, float alpha);
uint32_t Mesh_FillDynamicVertices(uint8_t* group, uint32_t arg0, uint8_t* mesh, void* vb, const uint8_t* positions,
                                  const uint8_t* normals);
float fn_0048a030(uint8_t* object, uint32_t world, uint32_t view);
uint32_t Material_BindTextures(uint8_t* material);
uint32_t MeshGroup_Draw(void* batch, uint8_t* pass, uint32_t item, uint8_t* group, uint32_t arg3, void* owner,
                        uint32_t drawPass);
uint32_t fn_0048eb20(uint32_t* cell, uint32_t callback, uint32_t context, uint8_t* world);
uint32_t fn_004937a0(const float* in, float* out, uint32_t arg0, int32_t width, int32_t height);
uint32_t fn_004938e0(float* quad, uint32_t arg0, int32_t width, int32_t height, float cx, float cy);
uint32_t fn_00494d40(uint8_t* params);
uint32_t Streamers_AllocBlock(uint8_t* manager, int32_t count, uint32_t extra);
uint32_t StreamerList_Link(uint8_t* node, uint32_t* head);
uint32_t fn_00495000(uint8_t* node, const uint32_t* source);
uint32_t fn_00495040(uint8_t* node);
uint32_t fn_004952f0(uint8_t* node);
uint32_t Streamer_Update(uint8_t* node, float dt);
uint32_t Streamer_Draw(const float* camera, uint8_t* node);
uint32_t Streamer_SetDirection(const float* a, uint8_t* out, const float* b);
uint32_t fn_00495cb0(uint32_t arg0, float arg1, uint32_t arg2);
uint32_t Text_Draw(uint32_t colour, uint32_t ecx, uint32_t font, uint32_t text, uint32_t x, uint32_t y, uint32_t a4,
                   uint32_t a5, uint32_t a6, uint32_t a7);
uint32_t Skin_Deform(uint32_t* bones, uint8_t* group, uint8_t* instance, uint8_t* positions, uint8_t* normals);
uint32_t Skin_DeformAll(uint8_t* model, uint32_t bones, uint32_t instance, uint32_t positions, uint32_t normals);
uint32_t Skin_BuildPalette(uint32_t* out, uint32_t* bones, uint8_t* group, uint8_t* instance);
uint32_t Skin_FaceNormals(float* out, uint8_t* model, const uint8_t* positions);
uint32_t Skin_VertexNormals(uint8_t* model, float* out, const float* faceNormals, uint32_t normalize);
uint32_t Projection2D_Set(void* renderer);

// ================================================================================================================
// Scene pass object (vtable 0x0059d9f8; ScenePass_Render 0x00489070 takes it)
// ================================================================================================================

// 0x00487e30 (ecx = object passed to 0x00418920, esi = 6-dword render state): for state types (dword 0 & 0xf000)
// 0x7000, 0xa000 and 0xf000, state[2] = the texture of the object 0x00418920 returns (its +0x470 when its byte +0x460
// is set, else 0; 0 when there is no object), then renderer virtual +0xd4(0). Caller: MeshGroup_Draw (state of the
// group's material); 0x00488b00 has the same code inline. guess: environment/reflection map bind.
uint32_t fn_00487e30(uint32_t object, uint32_t* state)
{
    const uint32_t type = state[0] & 0xf000;
    if (type != 0x7000 && type != 0xa000 && type != 0xf000)
        return type;
    const uint32_t found = orig::fn_00418920(object);
    if (!found)
    {
        state[2] = 0;
        return 0;
    }
    state[2] = Field<uint8_t>(P(found), 0x460) ? found + 0x470 : 0;
    return VCall<uint32_t>(Renderer(), 0xd4 / 4, 0u);
}

// 0x00487f00 (edi = pass): constructor. vtable 0x0059d9f8, members cleared in the original order, then two lists
// from the 0x0063a210 heap (0x004e07f0: 0x10, 0x1c, 2, "new" 0x0058b5e8) wrapped by 0x004e6bd0 (edx = capacity
// 0x14 / 10, esi = block; stack: callback 0x00487ec0 / 0x00487ee0) at +0x1bc and +0x1c0 (the sky lists ScenePass_Render
// draws with flag 0x10). Returns the pass.
uint32_t ScenePass_Construct(uint8_t* pass)
{
    Field<uint32_t>(pass, 0) = 0x0059d9f8;
    Field<uint32_t>(pass, 0x28) = 0;
    Field<uint32_t>(pass, 0x30) = 0;
    Field<uint32_t>(pass, 0x2c) = 0;
    Field<uint32_t>(pass, 0x14) = 0;
    Field<uint32_t>(pass, 0x0c) = 0;
    Field<uint32_t>(pass, 0x18) = 0;
    Field<uint32_t>(pass, 0x1c) = 0;
    Field<uint32_t>(pass, 0x24) = 0;
    Field<uint32_t>(pass, 0x20) = 0;
    Field<uint32_t>(pass, 0x1c4) = 0;
    Field<uint32_t>(pass, 0x1c8) = 0;
    Field<uint32_t>(pass, 4) = 0;
    Field<uint32_t>(pass, 8) = 0;
    Field<uint32_t>(pass, 0x10c) = 0;
    uint32_t block = orig::fn_004e07f0(0x0063a210u, 0x10, 0x1c, 2, 0x0058b5e8u);
    Field<uint32_t>(pass, 0x1bc) = block ? orig::fn_004e6bd0(0x14, block, 0x00487ec0u) : 0;
    block = orig::fn_004e07f0(0x0063a210u, 0x10, 0x1c, 2, 0x0058b5e8u);
    Field<uint32_t>(pass, 0x1c0) = block ? orig::fn_004e6bd0(10, block, 0x00487ee0u) : 0;
    return u(pass);
}

// 0x00487fc0 (esi = pass): destructor body. vtable 0x0059d9f8, ScenePass_Release, then the two sky lists' virtual
// +0x10(1) (deleting destructor).
uint32_t ScenePass_Destruct(uint8_t* pass)
{
    Field<uint32_t>(pass, 0) = 0x0059d9f8;
    uint32_t r = ScenePass_Release(pass);
    if (void* list = Field<void*>(pass, 0x1bc))
        r = VCall<uint32_t>(list, 0x10 / 4, 1u);
    if (void* list = Field<void*>(pass, 0x1c0))
        r = VCall<uint32_t>(list, 0x10 / 4, 1u);
    return r;
}

// 0x00487ff0 (eax = pass): clears +0x28/+0x2c/+0x30, sets the three 0x44-byte matrices at +0x40, +0x84 and +0xc8 to
// identity (rep stosd 0x11 dwords, diagonal 1.0), then sorts the layers (0x00489910) and returns its EAX.
uint32_t fn_00487ff0(uint8_t* pass)
{
    Field<uint32_t>(pass, 0x28) = 0;
    Field<uint32_t>(pass, 0x2c) = 0;
    Field<uint32_t>(pass, 0x30) = 0;
    for (uint32_t base : {0x40u, 0x84u, 0xc8u})
    {
        std::memset(pass + base, 0, 0x44);
        Field<uint32_t>(pass, base) = 0x3f800000;
        Field<uint32_t>(pass, base + 0x14) = 0x3f800000;
        Field<uint32_t>(pass, base + 0x28) = 0x3f800000;
        Field<uint32_t>(pass, base + 0x3c) = 0x3f800000;
    }
    return fn_00489910(pass);
}

// 0x00488060 (eax = pass): frees the three skinning buffers +0x28/+0x2c/+0x30, then 0x00488230 (layers), 0x00488850,
// the sky lists' virtual +0x30 (0x00488110 / 0x00488140 are tail jumps to it, inlined here), and 0x004880d0 (lights).
uint32_t ScenePass_Release(uint8_t* pass)
{
    for (uint32_t off : {0x28u, 0x2cu, 0x30u})
    {
        if (const uint32_t b = Field<uint32_t>(pass, off))
        {
            Free(b);
            Field<uint32_t>(pass, off) = 0;
        }
    }
    fn_00488230(pass);
    fn_00488850(pass);
    VCall<uint32_t>(Field<void*>(pass, 0x1bc), 0x30 / 4);
    VCall<uint32_t>(Field<void*>(pass, 0x1c0), 0x30 / 4);
    return fn_004880d0(pass);
}

// 0x004880d0 (edi = pass): renderer virtual +0xb0(i, 0, 0) (light i off, as LightSlot_Release) for i below
// pass+0x10c (re-read each step), then +0x10c = 0. Returns the last count read.
uint32_t fn_004880d0(uint8_t* pass)
{
    uint32_t count = Field<uint32_t>(pass, 0x10c);
    for (uint32_t i = 0; i < count;)
    {
        VCall<void>(Renderer(), 0xb0 / 4, i, 0u, 0u);
        count = Field<uint32_t>(pass, 0x10c);
        ++i;
    }
    Field<uint32_t>(pass, 0x10c) = 0;
    return count;
}

// 0x00488120 / 0x00488130 (eax = object, ecx = pass): 0x004e6e00 (edi = list; stack: object) on the sky list +0x1bc
// / +0x1c0 (adds the object to it; guess).
uint32_t fn_00488120(uint32_t object, uint8_t* pass)
{
    return orig::fn_004e6e00(Field<uint32_t>(pass, 0x1bc), object);
}
uint32_t fn_00488130(uint32_t object, uint8_t* pass)
{
    return orig::fn_004e6e00(Field<uint32_t>(pass, 0x1c0), object);
}

// 0x00488230 (esi = pass): releases the layer set. Renderer virtual +0x2c, frees +0x14 and the +0xc object (when its
// byte +0xc is clear: 0x004f11c0 (ebx = it) and its +0x1c block first) and +0x10. When +0x18 (0x24-byte records,
// count +0x20) exists: renderer virtual +0x128(record, 0) per record, each layer object (+0x1c4[0 .. count + 1])
// gets vtable 0x0058b0d8 and is freed, the item allocator +0x1b8 (block list at +0xc, node +0 next) is freed,
// renderer virtual +0x28, then +0x18, +0x1c, +0x1c4, +0x1c8 and +0x24 are freed. +0x20 = 0 at the end.
uint32_t fn_00488230(uint8_t* pass)
{
    VCall<void>(Renderer(), 0x2c / 4);
    if (const uint32_t b = Field<uint32_t>(pass, 0x14))
    {
        Free(b);
        Field<uint32_t>(pass, 0x14) = 0;
    }
    if (uint8_t* x = Field<uint8_t*>(pass, 0x0c))
    {
        if (!Field<uint8_t>(x, 0x0c))
        {
            orig::fn_004f11c0(u(x));
            Free(Field<uint32_t>(x, 0x1c));
        }
        Free(u(x));
        Field<uint32_t>(pass, 0x0c) = 0;
        if (const uint32_t b = Field<uint32_t>(pass, 0x10))
        {
            Free(b);
            Field<uint32_t>(pass, 0x10) = 0;
        }
    }
    if (Field<uint32_t>(pass, 0x18))
    {
        uint32_t off = 0;
        for (uint32_t i = 0; i < Field<uint32_t>(pass, 0x20); ++i, off += 0x24)
        {
            VCall<void>(Renderer(), 0x128 / 4, Field<uint32_t>(pass, 0x18) + off, 0u);
            if (uint32_t* layer = Field<uint32_t**>(pass, 0x1c4)[i])
            {
                layer[0] = 0x0058b0d8;
                Free(u(layer));
            }
        }
        for (uint32_t i = Field<uint32_t>(pass, 0x20); i < Field<uint32_t>(pass, 0x20) + 2; ++i)
        {
            if (uint32_t* layer = Field<uint32_t**>(pass, 0x1c4)[i])
            {
                layer[0] = 0x0058b0d8;
                Free(u(layer));
            }
        }
        if (uint8_t* items = Field<uint8_t*>(pass, 0x1b8))
        {
            for (uint32_t node = Field<uint32_t>(items, 0x0c); node;)
            {
                const uint32_t next = Field<uint32_t>(P(node), 0);
                Free(node);
                node = next;
            }
            Field<uint32_t>(items, 0x0c) = 0;
            Field<uint32_t>(items, 8) = 0;
            Free(u(items));
        }
        Field<uint32_t>(pass, 0x1b8) = 0;
        VCall<void>(Renderer(), 0x28 / 4);
        for (uint32_t off2 : {0x18u, 0x1cu, 0x1c4u, 0x1c8u, 0x24u})
        {
            if (const uint32_t b = Field<uint32_t>(pass, off2))
            {
                Free(b);
                Field<uint32_t>(pass, off2) = 0;
            }
        }
    }
    Field<uint32_t>(pass, 0x20) = 0;
    return 0;
}

// 0x00488850 (esi = pass): frees +4, then renderer virtual +4(0, 0).
uint32_t fn_00488850(uint8_t* pass)
{
    if (const uint32_t b = Field<uint32_t>(pass, 4))
    {
        Free(b);
        Field<uint32_t>(pass, 4) = 0;
    }
    return VCall<uint32_t>(Renderer(), 4 / 4, 0u, 0u);
}

// 0x00488990 (eax = pass, edx = out): *out = the frustum planes (pass +0x110); returns their count (+0x170).
uint32_t ScenePass_GetFrustum(uint8_t* pass, uint32_t* out)
{
    *out = u(pass) + 0x110;
    return Field<uint32_t>(pass, 0x170);
}

// 0x00488b00 (eax = model instance: +0x14 count, +0x18 parts; stack: owner (+0x28/+0x2c/+0x30 skinning buffers:
// positions, normals, face normals), drawer (its virtual +0: vertex batch whose +0x10 / +8 give the state and
// matrix-palette interfaces), bone instance, subset flag (byte), state callback (cdecl, or 0), shadow flag (byte);
// callee pops 24). Called by 0x00417e90 (shadow silhouette, guess). For each part P (+4 = model Q): with the shadow
// flag on an unskinned-by-hardware model (Q +0x44 = 0) and all three buffers, software-skins it (Skin_DeformAll,
// Skin_FaceNormals, Skin_VertexNormals, then Mesh_FillDynamicVertices). Then every mesh group (Q +0x84, 0x2c bytes,
// count Q +0x80) whose material (Q +0x7c, 0x64 bytes, index group +0x10) has a shader (+8) and whose bit 4 of byte
// +0x34 equals the subset flag: binds the state (callback result, or shader +0x18) with Material_BindTextures and
// the inline env-map code, then draws: hardware-skinned (palette Skin_BuildPalette, palette virtuals +0x68/+0x64/
// +0x5c, drawer +4 / +0x24), or a triangle list (drawer virtual +0x18(2, 0, first, base, count)), for the shadow
// flag directly, otherwise after software skinning when the buffers exist. Returns 0.
uint32_t fn_00488b00(uint8_t* model, uint8_t* owner, void* drawer, uint32_t bones, uint32_t subset,
                     uint32_t callback, uint32_t shadow)
{
    alignas(8) uint32_t palette[0x100];  // esp+0x38 .. the frame end (0x400 bytes)
    const uint32_t positions = Field<uint32_t>(owner, 0x28);
    const uint32_t normals = Field<uint32_t>(owner, 0x2c);
    const uint32_t faceNormals = Field<uint32_t>(owner, 0x30);
    void* vertexBatch = VCall<void*>(drawer, 0);
    void* stateApi = VCall<void*>(vertexBatch, 0x10 / 4);
    void* paletteApi = VCall<void*>(vertexBatch, 8 / 4);
    VCall<void>(stateApi, 0xc / 4, 1u);
    uint32_t count = Field<uint32_t>(model, 0x14);
    const uint32_t* parts = &Field<uint32_t>(model, 0x18);
    for (; count; --count)
    {
        uint8_t* part = B(*parts++);
        uint8_t* q = Field<uint8_t*>(part, 4);
        if (static_cast<uint8_t>(shadow) && !Field<uint32_t>(q, 0x44) && positions && normals && faceNormals)
        {
            Skin_DeformAll(q, u(part) + 8, bones, positions, normals);
            Skin_FaceNormals(reinterpret_cast<float*>(faceNormals), q, B(positions));
            Skin_VertexNormals(q, reinterpret_cast<float*>(normals), reinterpret_cast<const float*>(faceNormals), 0);
            Mesh_FillDynamicVertices(nullptr, u(owner), q, drawer, B(positions), B(normals));
        }
        uint32_t groups = Field<uint32_t>(q, 0x80);
        uint8_t* g = Field<uint8_t*>(q, 0x84);
        for (; groups; --groups, g += 0x2c)
        {
            uint8_t* material = Field<uint8_t*>(q, 0x7c) + Field<uint32_t>(g, 0x10) * 0x64;
            if (!Field<uint32_t>(material, 8))
                continue;
            if (((Field<uint8_t>(material, 0x34) >> 4) & 1) != static_cast<uint8_t>(subset))
                continue;
            uint32_t* state;
            if (!callback)
                state = reinterpret_cast<uint32_t*>(Field<uint32_t>(material, 8) + 0x18);
            else
                state = reinterpret_cast<uint32_t*>(reinterpret_cast<uint32_t(__cdecl*)(void*)>(callback)(material));
            if (state)
            {
                Material_BindTextures(material);
                fn_00487e30(0, state);  // inline copy in the original (ecx = 0)
                VCall<void>(stateApi, 0x18 / 4, state, Field<uint32_t>(material, 8) + 0x30);
            }
            if (Field<uint32_t>(q, 0x44) && Field<uint32_t>(g, 0x24))
            {
                const uint32_t n =
                    Skin_BuildPalette(palette, reinterpret_cast<uint32_t*>(u(part) + 8), g, B(bones));
                // LTCG argument building (see MeshGroup_Draw): +0x68 / +0x64 are argument-less getters whose callers' pushes
                // become the arguments of +0x5c (ret 0x10).
                const uint32_t world = VCall<uint32_t>(paletteApi, 0x68 / 4);
                const uint32_t view = VCall<uint32_t>(paletteApi, 0x64 / 4);
                VCall<void>(paletteApi, 0x5c / 4, view, world, palette, n);
                VCall<void>(drawer, 4 / 4, Field<uint32_t>(q, 0x44));
                VCall<void>(drawer, 0x24 / 4, Field<uint32_t>(g, 0x24), 0xffffffffu);
            }
            else if (static_cast<uint8_t>(shadow))
            {
                VCall<void>(drawer, 0x18 / 4, 2u, 0u, Field<uint32_t>(g, 0x20), 0u, Field<uint32_t>(g, 0x28));
            }
            else if (positions && normals && faceNormals)
            {
                Skin_Deform(reinterpret_cast<uint32_t*>(u(part) + 8), g, B(bones), B(positions), B(normals));
                // 0x00488d82: eax = [esp+0x44c] after four pushes = the first stack argument (owner), not the callback.
                Mesh_FillDynamicVertices(g, u(owner), nullptr, drawer, B(positions), B(normals));
                VCall<void>(drawer, 0x18 / 4, 2u, 0u, Field<uint32_t>(g, 0x20), 0u - Field<uint32_t>(g, 0x14),
                            Field<uint32_t>(g, 0x28));
            }
        }
    }
    return 0;
}

// 0x00488df0 (ecx = pass; stack: batch, &current world matrix, tree node; callee pops 12): in-order walk of the
// sorted (translucent) layer's tree (node +4 left, +0 right, +0xc item). Per item: world matrix (renderer virtual
// +0x54) when item +0 changes, MeshGroup_Draw(batch; pass, item +4, +8, +0xc, +0x14, -1), then (after the right
// subtree) the item goes back to the pass's item allocator (+0x1b8, free list +8) and the node to Game+0xac4's.
uint32_t SortedTree_Draw(uint8_t* pass, uint32_t batch, uint32_t* currentMatrix, uint32_t* node)
{
    if (!node)
        return 0;
    SortedTree_Draw(pass, batch, currentMatrix, reinterpret_cast<uint32_t*>(node[1]));
    uint32_t* item = reinterpret_cast<uint32_t*>(node[3]);
    if (item[0] != *currentMatrix)
    {
        *currentMatrix = item[0];
        VCall<void>(Renderer(), 0x54 / 4, item[0]);
    }
    MeshGroup_Draw(P(batch), pass, item[1], B(item[2]), item[3], P(item[5]), 0xffffffffu);
    SortedTree_Draw(pass, batch, currentMatrix, reinterpret_cast<uint32_t*>(node[0]));
    uint8_t* items = Field<uint8_t*>(pass, 0x1b8);
    item[0] = Field<uint32_t>(items, 8);
    Field<uint32_t*>(items, 8) = item;
    uint8_t* nodes = Field<uint8_t*>(g_game, 0xac4);
    node[0] = Field<uint32_t>(nodes, 8);
    Field<uint32_t*>(nodes, 8) = node;
    return u(nodes);
}

// 0x00489320 (stdcall, ret 0x18: pass, view object, cube target, aspect (float bits), scene flags, face mask): renders
// the scene into the faces of a cube render target. Saves the viewport (renderer virtual +0x7c, 4 dwords) and the
// world/view/projection (0x004f0170). For each face f (0..5) in the mask (bits 0..5) for which renderer virtual
// +0xcc(target, 0, f, 1) succeeds: the first time, saves the camera's two lens values (0x0040f850 / 0x0040f840),
// sets the camera (0x0040f890 (eax = camera; aspect), 0x0040f880 (ecx = camera; pi/2 [0x005bce6c])), viewport
// virtual +0x74(0), and a pi/2 projection (0x004f0100 with the camera's near 0x0040f860 (cl = 0), far 0) set by
// +0x4c; then +0xf4(0) (clear), +0xd0(&view, view object, f) builds the face view, +0x50 sets it and
// ScenePass_Render(pass, flags, 0, 0) draws. If any face was drawn, everything is restored (camera, target +0xcc(0,
// 0, 0, 0), +0x54/+0x50/+0x4c, viewport +0x74). Caller: 0x00418c80 (env maps, guess).
uint32_t CubeMap_RenderScene(void* pass, uint32_t viewObject, uint32_t target, uint32_t aspect, uint32_t flags,
                             uint32_t faces)
{
    if (!(faces & 0x3f))
        return 0;
    alignas(8) uint32_t viewport[4];
    alignas(8) uint32_t projection[17];
    alignas(8) uint32_t view[17];
    alignas(8) uint32_t saved[0x33];  // world, view, projection (3 x 0x44 bytes, 0x004f0170)
    const uint32_t* vp = VCall<const uint32_t*>(Renderer(), 0x7c / 4);
    viewport[0] = vp[0];
    viewport[1] = vp[1];
    viewport[2] = vp[2];
    viewport[3] = vp[3];
    orig::fn_004f0170(u(Renderer()), u(saved));
    float lens850 = 0.0f, lens840 = 0.0f;
    bool first = true;
    for (uint32_t face = 0; face < 6; ++face)
    {
        if (!(faces & (1u << face)))
            continue;
        if (!static_cast<uint8_t>(VCall<uint32_t>(Renderer(), 0xcc / 4, target, 0u, face, 1u)))
            continue;
        if (first)
        {
            Game* game = g_game;
            first = false;
            lens850 = orig::fn_0040f850(Field<uint32_t>(game, 0xa68));
            lens840 = orig::fn_0040f840(Field<uint32_t>(game, 0xa68));
            orig::fn_0040f890(Field<uint32_t>(game, 0xa68), aspect);
            orig::fn_0040f880(Field<uint32_t>(game, 0xa68), GameGlobal<uint32_t>(0x005bce6c));
            VCall<void>(Field<void*>(game, 0x38), 0x74 / 4, 0u);
            void* r = Renderer();
            const float nearZ = orig::fn_0040f860(Field<uint32_t>(g_game, 0xa68), 0);
            orig::fn_004f0100(u(r), u(projection), GameGlobal<uint32_t>(0x005bce6c), aspect, fb(nearZ), 0);
            VCall<void>(r, 0x4c / 4, projection);
        }
        VCall<void>(Renderer(), 0xf4 / 4, 0u);
        VCall<void>(Renderer(), 0xd0 / 4, view, viewObject, face);
        VCall<void>(Renderer(), 0x50 / 4, view);
        ScenePass_Render(pass, flags, 0, 0);
    }
    if (first)
        return 0;
    Game* game = g_game;
    orig::fn_0040f890(Field<uint32_t>(game, 0xa68), fb(lens850));
    orig::fn_0040f880(Field<uint32_t>(game, 0xa68), fb(lens840));
    VCall<void>(Field<void*>(game, 0x38), 0xcc / 4, 0u, 0u, 0u, 0u);
    void* r = Renderer();
    VCall<void>(r, 0x54 / 4, saved);
    VCall<void>(r, 0x50 / 4, saved + 0x11);
    VCall<void>(r, 0x4c / 4, saved + 0x22);
    return VCall<uint32_t>(Renderer(), 0x74 / 4, viewport);
}

// 0x00489540 (ebx = bracket flag (byte); stack: pass, target, keep (byte); callee pops 12. ECX is only the reserved
// local slot: its upper three bytes travel into the final call). Renders the whole scene into a texture: saves
// renderer virtual +0x158(4) (a render state, byte), turns it off (+0x154(4, 0)) unless `keep` or already off,
// brackets with Game virtual +0x14 / +0x18 when ebx is set, sets the target (+0xcc(target, 0, 0, 1)), clears
// (+0xf4(0)), ScenePass_Render(pass, all flags, 0, 0), resets the target and restores the state.
uint32_t ScenePass_RenderToTexture(uint32_t ecx, uint32_t ebx, void* pass, uint32_t target, uint32_t keep)
{
    const uint32_t state = VCall<uint32_t>(Renderer(), 0x158 / 4, 4u);
    const uint32_t slot = (ecx & 0xffffff00u) | (state & 0xffu);
    if (!static_cast<uint8_t>(keep) && static_cast<uint8_t>(state))
        VCall<void>(Renderer(), 0x154 / 4, 4u, 0u);
    if (static_cast<uint8_t>(ebx))
        VCall<void>(g_game, 0x14 / 4);
    VCall<void>(Renderer(), 0xcc / 4, target, 0u, 0u, 1u);
    VCall<void>(Renderer(), 0xf4 / 4, 0u);
    ScenePass_Render(pass, 0xffffffffu, 0, 0);
    VCall<void>(Renderer(), 0xcc / 4, 0u, 0u, 0u, 0u);
    if (static_cast<uint8_t>(ebx))
        VCall<void>(g_game, 0x18 / 4);
    return VCall<uint32_t>(Renderer(), 0x154 / 4, 4u, slot);
}

// 0x00489610 (eax = arg3, edx = pass; stack: arg2; callee pops 4): the main scene: ScenePass_Render(pass, all
// flags, arg2, arg3), then a tail jump to 0x00433e70, which is an empty `ret 4` (its argument, the game, is unused).
// Returns the game pointer (EAX at the jump).
uint32_t ScenePass_RenderMain(uint32_t arg3, void* pass, uint32_t arg2)
{
    ScenePass_Render(pass, 0xffffffffu, arg2, arg3);
    return u(g_game);
}

// ================================================================================================================
// Materials, layers, items
// ================================================================================================================

// 0x004897a0 (eax = material; stack: unused, alpha (float); callee pops 8): translucent test. AL = 1 when the
// material's byte +0x34 has bit 4 (blend mode at +0x37) or alpha < 1.0 ([0x005bcb2c]), else 0.
uint32_t Material_IsTranslucent(uint8_t* material, uint32_t /*arg0*/, float alpha)
{
    if (Field<uint8_t>(material, 0x34) & 0x10)
        return 1;
    return alpha < K(0x005bcb2c) ? 1u : 0u;
}

// 0x004897d0 (esi = 6-dword render state, edi = material; stack: unused (the pass), alpha (float); callee pops 8):
// derives blend flags. Clears state bits 8..11; with bit 4 of material +0x34 the blend mode is byte +0x37 (0:
// |0x120, 1: |0x420, 2: |0x320), otherwise alpha < 1 gives |0x320. With alpha * material +0x48 below 1.0 the state
// gets a constant alpha: dword 1 = (ftol(x * 255) << 24) | bytes 6, 5, 4 of the state, and bit 0x40000; otherwise
// 0x40000 is cleared. Bit 0x20 when alpha < 1 or material bit 4. A redundant clear for type 0xc000 when alpha != 1
// is kept (it stores the same value).
uint32_t fn_004897d0(uint32_t* state, uint8_t* material, uint32_t /*arg0*/, float alpha)
{
    uint32_t f = state[0] & 0xfffff0ffu;
    state[0] = f;
    if ((f & 0xf000) == 0xc000 && !(alpha == K(0x005bcb2c)))
        state[0] = f & 0xfffff0ffu;
    bool set320 = false;
    bool done = false;
    if (Field<uint8_t>(material, 0x34) & 0x10)
    {
        switch (Field<uint8_t>(material, 0x37))
        {
        case 0:
            state[0] |= 0x120;
            done = true;
            break;
        case 1:
            state[0] |= 0x420;
            done = true;
            break;
        case 2:
            set320 = true;
            break;
        default:
            break;
        }
    }
    if (!done && !set320 && alpha < K(0x005bcb2c))
        set320 = true;
    if (set320)
        state[0] |= 0x320;
    const float x = alpha * Field<float>(material, 0x48);
    uint32_t v;
    if (x < K(0x005bcb2c))
    {
        const int32_t a = Ftol(x * K(0x005bcb34));
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(state);
        uint32_t c = (static_cast<uint32_t>(static_cast<uint8_t>(a)) << 8) | bytes[6];
        c = (c << 8) | bytes[5];
        c = (c << 8) | bytes[4];
        v = state[0];
        state[1] = c;
        v |= 0x40000;
    }
    else
    {
        v = state[0] & 0xfffbffffu;
    }
    state[0] = v;
    if (alpha < K(0x005bcb2c) || (Field<uint8_t>(material, 0x34) & 0x10))
        state[0] |= 0x20;
    return 0;
}

// 0x00489910 (esi = pass): sorts the layer keys. pass +0x1c8 holds +0x20 16-byte records (+0 key, +4, +8 depth,
// +0xc layer). qsort (0x00538f30) by key (0x004898b0), every run of equal keys gets the run's largest depth
// (+8; a NaN depth wins) written into +4, qsort again by +4 (0x004898c0), the layer table +0x1c4 is rebuilt in that
// order and the records are freed (+0x1c8 = 0).
uint32_t fn_00489910(uint8_t* pass)
{
    orig::fn_00538f30(Field<uint32_t>(pass, 0x1c8), Field<uint32_t>(pass, 0x20), 0x10, 0x004898b0u);
    uint32_t i = 0;
    if (Field<uint32_t>(pass, 0x20))
    {
        do
        {
            uint8_t* records = Field<uint8_t*>(pass, 0x1c8);
            float depth = Field<float>(records + i * 16, 8);
            const int32_t key = Field<int32_t>(records + i * 16, 0);
            uint32_t j = i + 1;
            while (j < Field<uint32_t>(pass, 0x20) && Field<int32_t>(records + j * 16, 0) == key)
            {
                const float d = Field<float>(records + j * 16, 8);
                if (!(depth > d))
                    depth = d;
                ++j;
            }
            for (uint32_t k = i; k < j; ++k)
                Field<float>(Field<uint8_t*>(pass, 0x1c8) + k * 16, 4) = depth;
            i = j;
        } while (i < Field<uint32_t>(pass, 0x20));
    }
    orig::fn_00538f30(Field<uint32_t>(pass, 0x1c8), Field<uint32_t>(pass, 0x20), 0x10, 0x004898c0u);
    for (uint32_t k = 0; k < Field<uint32_t>(pass, 0x20); ++k)
        Field<uint32_t*>(pass, 0x1c4)[k] = Field<uint32_t>(Field<uint8_t*>(pass, 0x1c8) + k * 16, 0xc);
    if (const uint32_t records = Field<uint32_t>(pass, 0x1c8))
    {
        const uint32_t r = Free(records);
        Field<uint32_t>(pass, 0x1c8) = 0;
        return r;
    }
    return 0;
}

// 0x00489a20 (eax = segment (two points, 6 floats), edi = pass; stack: object, arg, scale (float); callee pops 12;
// caller 0x0045a860): while the layer records (+0x1c8) exist, for an object whose layer key (+0x54) is a normal
// layer (not +0x1c4[count] or [count + 1]) found at index i: d = ((c2 * dz + c1 * dy) + c0 * dx) * scale, with
// (c0, c1, c2) the globals 0x005fb940.. (view direction, guess) and scale * 20 ([0x005bcbe8]) for object types (+0x18
// & 0xf000) 0xa000/0xd000/0xf000. When d exceeds record i's depth: record i = (renderer virtual +0x3c(arg, object
// +0x18, 3, object +0x44), d, d).
uint32_t fn_00489a20(const float* segment, uint8_t* pass, uint8_t* object, uint32_t arg, float scale)
{
    if (!Field<uint32_t>(pass, 0x1c8))
        return u(segment);
    const uint32_t key = Field<uint32_t>(object, 0x54);
    if (!key)
        return u(object);
    const uint32_t count = Field<uint32_t>(pass, 0x20);
    const uint32_t* layers = Field<uint32_t*>(pass, 0x1c4);
    if (key == layers[count] || key == layers[count + 1])
        return count;
    uint32_t i = 0;
    while (i < count && layers[i] != key)
        ++i;
    if (i == count)
        return count;
    const uint32_t type = Field<uint32_t>(object, 0x18) & 0xf000;
    float s = scale;
    if (type == 0xa000 || type == 0xd000 || type == 0xf000)
        s = scale * K(0x005bcbe8);
    const float dx = segment[3] - segment[0];
    const float dy = segment[4] - segment[1];
    const float dz = segment[5] - segment[2];
    const float d =
        ((GameGlobal<float>(0x005fb948) * dz + GameGlobal<float>(0x005fb944) * dy) + GameGlobal<float>(0x005fb940) * dx) *
        s;
    uint8_t* records = Field<uint8_t*>(pass, 0x1c8);
    if (!(d > Field<float>(records + i * 16, 8)))
        return count;
    const uint32_t r = VCall<uint32_t>(Renderer(), 0x3c / 4, arg, u(object) + 0x18, 3u, u(object) + 0x44);
    Field<uint32_t>(Field<uint8_t*>(pass, 0x1c8) + i * 16, 0) = r;
    Field<float>(Field<uint8_t*>(pass, 0x1c8) + i * 16, 4) = d;
    Field<float>(Field<uint8_t*>(pass, 0x1c8) + i * 16, 8) = d;
    return r;
}

// 0x00489f30 (ecx = mesh group or 0; stack: unused, mesh, vertex buffer, positions, normals (or 0); callee pops 20):
// fills a dynamic vertex buffer (0x20-byte vertices: position, normal, uv). Range: the group's (+4 mesh, +0x14
// first, +0x18 count) or, without a group, the whole mesh (count +0x40). Locks with the buffer's virtual +8(5,
// count), copies positions[mesh +0x48 index] (12 bytes), normals[mesh +0x4c index] when given and uv[mesh +0x60
// index] from mesh +0x34 (8 bytes) when present, unlocks with virtual +0x10(0).
uint32_t Mesh_FillDynamicVertices(uint8_t* group, uint32_t /*arg0*/, uint8_t* mesh, void* vb, const uint8_t* positions,
                                  const uint8_t* normals)
{
    uint32_t count;
    uint32_t first;
    if (mesh)
    {
        count = Field<uint32_t>(mesh, 0x40);
        first = 0;
    }
    else
    {
        count = u(vb);  // the original loads the buffer argument here; the group always overrides it
        first = u(vb);
    }
    if (group)
    {
        mesh = Field<uint8_t*>(group, 4);
        count = Field<uint32_t>(group, 0x18);
        first = Field<uint32_t>(group, 0x14);
    }
    const uint32_t* uvIndex = Field<uint32_t*>(mesh, 0x60) + first;
    const uint32_t* posIndex = Field<uint32_t*>(mesh, 0x48) + first;
    const uint32_t* nrmIndex = Field<uint32_t*>(mesh, 0x4c) + first;
    uint8_t* dst = VCall<uint8_t*>(vb, 8 / 4, 5u, count);
    for (; count; --count, dst += 0x20)
    {
        const uint32_t* p = reinterpret_cast<const uint32_t*>(positions + *posIndex * 12);
        Field<uint32_t>(dst, 0) = p[0];
        Field<uint32_t>(dst, 4) = p[1];
        Field<uint32_t>(dst, 8) = p[2];
        ++posIndex;
        if (normals)
        {
            const uint32_t* n = reinterpret_cast<const uint32_t*>(normals + *nrmIndex * 12);
            Field<uint32_t>(dst, 0xc) = n[0];
            Field<uint32_t>(dst, 0x10) = n[1];
            Field<uint32_t>(dst, 0x14) = n[2];
            ++nrmIndex;
        }
        if (const uint8_t* uv = Field<uint8_t*>(mesh, 0x34))
        {
            const uint32_t k = *uvIndex;
            Field<uint32_t>(dst, 0x18) = Field<uint32_t>(uv, k * 8);
            Field<uint32_t>(dst, 0x1c) = Field<uint32_t>(uv, k * 8 + 4);
            ++uvIndex;
        }
    }
    return VCall<uint32_t>(vb, 0x10 / 4, 0u);
}

// 0x0048a030 (eax = mesh group or 0, ecx = world matrix; stack: view matrix; caller pops 4; returns st0): view-space
// depth of an item. The point is the centre of the group's box (+0x24: (min +0x24.. + max +0x30..) * 0.5) through
// the world matrix (0x004e36a0, edx = 1), or the world matrix's translation (+0xc, +0x1c, +0x2c); then through the
// view matrix; returns its z.
float fn_0048a030(uint8_t* group, uint32_t world, uint32_t view)
{
    alignas(8) float local[6];  // esp+0 .. 0x18: result (0..2) and point (3..5), as the original frame
    float* out = local;
    float* point = local + 3;
    uint8_t* box;
    if (group && (box = Field<uint8_t*>(group, 0x24)) != nullptr)
    {
        const float x = Field<float>(box, 0x24) + Field<float>(box, 0x30);
        const float y = Field<float>(box, 0x28) + Field<float>(box, 0x34);
        const float z = Field<float>(box, 0x2c) + Field<float>(box, 0x38);
        out[2] = z;
        out[0] = x * K(0x005bcb4c);
        out[1] = y * K(0x005bcb4c);
        out[2] = out[2] * K(0x005bcb4c);
        orig::fn_004e36a0(u(out), world, 1, u(point));
    }
    else
    {
        point[0] = Field<float>(P(world), 0xc);
        point[1] = Field<float>(P(world), 0x1c);
        point[2] = Field<float>(P(world), 0x2c);
    }
    orig::fn_004e36a0(u(point), view, 1, u(out));
    return out[2];
}

// 0x0048a0d0 (eax = pass; stack: item source (6 dwords), depth object (or 0), translucent (byte); callee pops 12;
// caller 0x00419060): queues a draw item. A new item from the pass allocator (0x004f6020, esi = +0x1b8) gets the
// six dwords (world matrix, owner data, group, ...). Translucent: item +0x10 = depth object virtual +0xc8(pass
// +0x174 view, world, group) and it goes to the sorted layer (+0x1c4[count + 1], virtual +0(item)). Otherwise it
// goes to its material shader's layer (material +8, +0x54; virtual +0(item)); when that layer is the sorted one the
// depth comes from the object or 0x0048a030 first.
uint32_t ItemList_Add(uint8_t* pass, const uint32_t* source, void* depthObject, uint32_t translucent)
{
    uint32_t* item = reinterpret_cast<uint32_t*>(orig::fn_004f6020(Field<uint32_t>(pass, 0x1b8)));
    for (int k = 0; k < 6; ++k)
        item[k] = source[k];
    uint8_t* group = B(source[2]);
    uint8_t* material = Field<uint8_t*>(Field<uint8_t*>(group, 4), 0x7c) + Field<uint32_t>(group, 0x10) * 0x64;
    if (static_cast<uint8_t>(translucent))
    {
        const float d = VCall<float>(depthObject, 0xc8 / 4, u(pass) + 0x174, source[0], u(group));
        Field<float>(item, 0x10) = d;
        void* layer = P(Field<uint32_t*>(pass, 0x1c4)[Field<uint32_t>(pass, 0x20) + 1]);
        return VCall<uint32_t>(layer, 0, item);
    }
    const uint32_t key = Field<uint32_t>(Field<uint8_t*>(material, 8), 0x54);
    if (key == Field<uint32_t*>(pass, 0x1c4)[Field<uint32_t>(pass, 0x20) + 1])
    {
        float d;
        if (depthObject)
            d = VCall<float>(depthObject, 0xc8 / 4, u(pass) + 0x174, source[0], u(group));
        else
            d = fn_0048a030(group, source[0], u(pass) + 0x174);
        Field<float>(item, 0x10) = d;
    }
    return VCall<uint32_t>(Field<void*>(Field<uint8_t*>(material, 8), 0x54), 0, item);
}

// 0x0048a210 (esi = material): binds the shader's four textures (+8: +0x44, +0x48, +0x4c, +0x50) to stages 0..3 with
// renderer virtual +0xa4(0, 0, texture, 0, stage) (the call ScenePass_ResetStages makes with texture 0).
uint32_t Material_BindTextures(uint8_t* material)
{
    uint32_t r = 0;
    for (uint32_t stage = 0; stage < 4; ++stage)
        r = VCall<uint32_t>(Renderer(), 0xa4 / 4, 0u, 0u, Field<uint32_t>(Field<uint8_t*>(material, 8), 0x44 + stage * 4),
                            0u, stage);
    return r;
}

// 0x0048a290 (ebx = batch drawer; stack: pass, item data (its +8: bone pointers), mesh group, bone instance, owner
// (or 0), draw pass index; callee pops 24): the central mesh-group draw. Nothing without indices (group +0x20).
// Material = group mesh +0x7c [group +0x10] (0x64 bytes). The state interface is batch virtual +0 -> +0x10 (its
// +0xc(1) first). With an owner that overrides (0x00453570): the shader state (material +8, +0x18, 6 dwords) is
// copied, blended by fn_004897d0 with the owner's alpha (0x00453630), OR'ed with owner virtual +0xc4, and handed to
// owner virtuals +0x2c(state) and +0x28(material); otherwise the shader's own state and Material_BindTextures. Then
// the env map (0x00487e30, ecx = owner) and state virtual +0x18(state, shader +0x30). Hardware-skinned meshes (mesh
// +0x44 and group +0x24): bone palette (Skin_BuildPalette) through renderer virtuals +0x68 / +0x64 / +0x5c, batch
// +4(mesh +0x44), and the return value is batch +0x24(group +0x24, draw pass) (AL nonzero: needs another pass).
// Otherwise, when the pass has skinning buffers (+0x28, +0x2c): software skinning (Skin_Deform), the dynamic
// vertices (Mesh_FillDynamicVertices) and batch +0x18(2, 0, count, -first, primitives); returns 0.
uint32_t MeshGroup_Draw(void* batch, uint8_t* pass, uint32_t item, uint8_t* group, uint32_t bones, void* owner,
                        uint32_t drawPass)
{
    alignas(8) uint32_t state[6];      // esp+0x1c
    alignas(8) uint32_t palette[0x100];  // esp+0x34 .. the frame end (0x400 bytes)
    if (!Field<uint32_t>(group, 0x20))
        return 0;
    uint8_t* material = Field<uint8_t*>(Field<uint8_t*>(group, 4), 0x7c) + Field<uint32_t>(group, 0x10) * 0x64;
    void* vertexBatch = VCall<void*>(batch, 0);
    void* stateApi = VCall<void*>(vertexBatch, 0x10 / 4);
    VCall<void>(stateApi, 0xc / 4, 1u);
    uint32_t* bound;
    if (owner && static_cast<uint8_t>(orig::fn_00453570(u(owner))))
    {
        std::memcpy(state, Field<uint8_t*>(material, 8) + 0x18, 0x18);
        bound = state;
        const float alpha = orig::fn_00453630(u(owner));
        fn_004897d0(state, material, u(pass), alpha);
        const uint32_t extra = VCall<uint32_t>(owner, 0xc4 / 4);
        state[0] |= extra;
        VCall<void>(owner, 0x2c / 4, state);
        VCall<void>(owner, 0x28 / 4, material);
    }
    else
    {
        bound = reinterpret_cast<uint32_t*>(Field<uint8_t*>(material, 8) + 0x18);
        Material_BindTextures(material);
    }
    fn_00487e30(u(owner), bound);
    VCall<void>(stateApi, 0x18 / 4, bound, u(Field<uint8_t*>(material, 8)) + 0x30);
    if (Field<uint32_t>(Field<uint8_t*>(group, 4), 0x44) && Field<uint32_t>(group, 0x24))
    {
        const uint32_t n = Skin_BuildPalette(palette, reinterpret_cast<uint32_t*>(item + 8), group, B(bones));
        // LTCG argument building: virtuals +0x68 / +0x64 are getters (lea eax, [ecx+0x6c] / [ecx+0xb0]; plain ret, no
        // arguments); the pushes around them (n, palette, then each getter's result) stay on the stack as the
        // arguments of +0x5c (ret 0x10): +0x5c(view +0xb0, world +0x6c, palette, n).
        void* r = Renderer();
        const uint32_t world = VCall<uint32_t>(r, 0x68 / 4);
        const uint32_t view = VCall<uint32_t>(r, 0x64 / 4);
        VCall<void>(Renderer(), 0x5c / 4, view, world, palette, n);
        VCall<void>(batch, 4 / 4, Field<uint32_t>(Field<uint8_t*>(group, 4), 0x44));
        return VCall<uint32_t>(batch, 0x24 / 4, Field<uint32_t>(group, 0x24), drawPass);
    }
    if (Field<uint32_t>(pass, 0x28) && Field<uint32_t>(pass, 0x2c))
    {
        Skin_Deform(reinterpret_cast<uint32_t*>(item + 8), group, B(bones), Field<uint8_t*>(pass, 0x28),
                    Field<uint8_t*>(pass, 0x2c));
        Mesh_FillDynamicVertices(group, u(pass), nullptr, batch, Field<uint8_t*>(pass, 0x28), Field<uint8_t*>(pass, 0x2c));
        VCall<void>(batch, 0x18 / 4, 2u, 0u, Field<uint32_t>(group, 0x20), 0u - Field<uint32_t>(group, 0x14),
                    Field<uint32_t>(group, 0x28));
    }
    return 0;
}

// 0x0048a4b0 (eax = slot or negative; stack: unused, light; callee pops 8): takes a hardware light slot. A negative
// slot means the first of 0..7 whose renderer virtual +0xb4(i, &light 0x68 bytes, D3DLIGHT9-sized) returns AL = 0
// (free). Slot 8 or more: returns -1. Otherwise renderer virtual +0xb0(slot, 1, light) and returns the slot.
uint32_t LightSlot_Acquire(int32_t slot, uint32_t /*arg0*/, uint32_t light)
{
    alignas(8) uint8_t buffer[0x68];
    if (slot < 0)
    {
        for (slot = 0; slot < 8; ++slot)
            if (!static_cast<uint8_t>(VCall<uint32_t>(Renderer(), 0xb4 / 4, static_cast<uint32_t>(slot), buffer)))
                break;
        if (slot >= 8)
            return 0xffffffffu;
    }
    if (slot >= 8)
        return 0xffffffffu;
    VCall<void>(Renderer(), 0xb0 / 4, static_cast<uint32_t>(slot), 1u, light);
    return static_cast<uint32_t>(slot);
}

// 0x0048a520 (eax = slot; stack: unused; callee pops 4): releases a light slot below 8: renderer virtual +0xb0(slot,
// 0, 0).
uint32_t LightSlot_Release(uint32_t slot, uint32_t /*arg0*/)
{
    if (slot < 8)
        return VCall<uint32_t>(Renderer(), 0xb0 / 4, slot, 0u, 0u);
    return slot;
}

// 0x0048a540 (edi = object; stack: pass; callee pops 4; callers 0x0040aa00, 0x00419060): draws a character-style
// object directly. When its virtual +0x30 allows: 0x004543f0 (eax = 0, ecx = object, edx = 0; stack 0, 1, 0),
// model set S = 0x00453120 (eax = object, edx = object +0x374; stack: *0x00408800(eax = 4, ecx = object)), world
// matrix from object virtual +0x78 set by renderer +0x54, batch = Game+0x44 virtual +0x18. For each part P of S
// (+0x14 count, +0x18 array; P +4 = model Q) with Q +0x78: every group of Q gets shader state bit 0x20 and the
// object's virtual +0xc4 bits (written into the shared shader state, permanently), and MeshGroup_Draw(batch; pass,
// P, group, object +0x410, object, -1). Game+0x44 virtual +0x1c at the end.
uint32_t Object_DrawGroups(void* object, uint8_t* pass)
{
    alignas(8) uint32_t world[17];
    const uint32_t ok = VCall<uint32_t>(object, 0x30 / 4);
    if (!static_cast<uint8_t>(ok))
        return ok;
    orig::fn_004543f0(0, u(object), 0, 0, 1, 0);
    const uint32_t* p = reinterpret_cast<const uint32_t*>(orig::fn_00408800(4, u(object)));
    uint8_t* set = B(orig::fn_00453120(u(object), Field<uint32_t>(object, 0x374), *p));
    VCall<void>(object, 0x78 / 4, world);
    VCall<void>(Renderer(), 0x54 / 4, world);
    const uint32_t batch = VCall<uint32_t>(Field<void*>(g_game, 0x44), 0x18 / 4);
    const uint32_t* parts = &Field<uint32_t>(set, 0x18);
    for (int32_t i = 0; i < Field<int32_t>(set, 0x14); ++i, ++parts)
    {
        uint8_t* part = B(*parts);
        uint8_t* q = Field<uint8_t*>(part, 4);
        if (!Field<uint32_t>(q, 0x78))
            continue;
        uint32_t off = 0;
        for (int32_t k = 0; k < Field<int32_t>(q, 0x80); ++k, off += 0x2c)
        {
            uint8_t* group = Field<uint8_t*>(q, 0x84) + off;
            uint8_t* material =
                Field<uint8_t*>(Field<uint8_t*>(group, 4), 0x7c) + Field<uint32_t>(group, 0x10) * 0x64;
            Field<uint32_t>(Field<uint8_t*>(material, 8), 0x18) |= 0x20;
            uint8_t* shader = Field<uint8_t*>(material, 8);
            const uint32_t bits = VCall<uint32_t>(object, 0xc4 / 4);
            Field<uint32_t>(shader, 0x18) |= bits;
            MeshGroup_Draw(P(batch), pass, u(part), group, Field<uint32_t>(object, 0x410), object, 0xffffffffu);
        }
    }
    return VCall<uint32_t>(Field<void*>(g_game, 0x44), 0x1c / 4);
}

// ================================================================================================================
// World grid walk
// ================================================================================================================

// 0x0048eb20 (eax = grid cell (+0 stamp, +4 object list), ebx = callback (cdecl), edi = context; stack: world; callee
// pops 4): calls callback(object, context) for each object of the cell (node +4 next, +0x10 object) whose stamp
// (+0x404) differs from the world's (+0x34), stamping it first (objects spanning several cells are visited once).
uint32_t fn_0048eb20(uint32_t* cell, uint32_t callback, uint32_t context, uint8_t* world)
{
    uint8_t* node = B(cell[1]);
    while (node)
    {
        uint8_t* object = Field<uint8_t*>(node, 0x10);
        const uint32_t stamp = Field<uint32_t>(world, 0x34);
        node = Field<uint8_t*>(node, 4);
        if (Field<uint32_t>(object, 0x404) != stamp)
        {
            Field<uint32_t>(object, 0x404) = stamp;
            reinterpret_cast<void(__cdecl*)(void*, uint32_t)>(callback)(object, context);
        }
    }
    return 0;
}

// 0x0048eb60 (stdcall, ret 0x14: world, runs, cell callback, object callback, context): walks the grid cells a gather
// (World_GatherFrustum) collected. Nothing when both callbacks are 0. The world stamp (+0x34) advances. runs: +0
// count, +4 axis (0: runs along x, step 1; 1: along y, step = width +0x20), then 12-byte runs (start, last, line).
// Each run is clipped to the grid (+0x20 width, +0x24 height; cells 8 bytes at +0x38); each cell not yet stamped is
// stamped, then cell callback(&coords (2 ints, it may move them), context) and the object walk (0x0048eb20). At the
// end the overflow cell (+0x30) gets the object walk too.
uint32_t World_WalkCells(uint8_t* world, const uint32_t* runs, uint32_t cellCallback, uint32_t objectCallback,
                         uint32_t context)
{
    if (!cellCallback && !objectCallback)
        return 0;
    ++Field<uint32_t>(world, 0x34);
    const uint32_t axis = runs[1];
    const uint32_t other = axis ^ 1;
    const uint32_t step = axis ? Field<uint32_t>(world, 0x20) : 1;
    int32_t coords[2];
    const int32_t* dims = &Field<int32_t>(world, 0x20);
    for (uint32_t r = 0; r < runs[0]; ++r)
    {
        const int32_t* run = reinterpret_cast<const int32_t*>(runs + 2 + r * 3);
        coords[axis] = run[0];
        int32_t end = run[1] + 1;
        coords[other] = run[2];
        if (run[2] < 0 || run[2] >= dims[other])
            continue;
        if (coords[axis] >= dims[axis] || end < 0)
            continue;
        const int32_t start = coords[axis] <= 0 ? 0 : coords[axis];
        coords[axis] = start;
        if (end >= dims[axis])
            end = dims[axis];
        const uint32_t index = static_cast<uint32_t>(Field<int32_t>(world, 0x20)) * static_cast<uint32_t>(coords[1]) +
                               static_cast<uint32_t>(coords[0]);
        uint32_t* cell = reinterpret_cast<uint32_t*>(Field<uint8_t*>(world, 0x38) + index * 8);
        if (start >= end)
            continue;
        int32_t next;
        do
        {
            if (cell[0] != Field<uint32_t>(world, 0x34))
            {
                cell[0] = Field<uint32_t>(world, 0x34);
                if (cellCallback)
                    reinterpret_cast<void(__cdecl*)(int32_t*, uint32_t)>(cellCallback)(coords, context);
                if (objectCallback)
                    fn_0048eb20(cell, objectCallback, context, world);
            }
            next = coords[axis] + 1;
            cell += step * 2;
            coords[axis] = next;
        } while (next < end);
    }
    if (objectCallback)
        fn_0048eb20(reinterpret_cast<uint32_t*>(Field<uint8_t*>(world, 0x38) + Field<uint32_t>(world, 0x30) * 8),
                    objectCallback, context, world);
    return 0;
}

// ================================================================================================================
// Screen overlays, menu images, text
// ================================================================================================================

// 0x00491670 (esi = fader; caller 0x00403050): lets +0xf4 (unless its +0x4c has bit 0x400) run its virtual +0x1c,
// then with +4 set feeds the post-process (Game+0xa98, 0x0047cff0 ebx; stack 0, v^3) a cubic of the fade value +0:
// v = x below 1.0, else 2.0 - x. guess: menu fade.
uint32_t fn_00491670(uint8_t* fader)
{
    if (void* f = Field<void*>(fader, 0xf4))
        if (!(Field<uint32_t>(f, 0x4c) & 0x400))
            VCall<void>(f, 0x1c / 4);
    if (!Field<uint32_t>(fader, 4))
        return 0;
    float v;
    if (Field<float>(fader, 0) < K(0x005bcb2c))
        v = Field<float>(fader, 0);
    else
        v = K(0x005bcb80) - Field<float>(fader, 0);
    const float cube = (v * v) * v;
    return orig::fn_0047cff0(Field<uint32_t>(g_game, 0xa98), 0u, fb(cube));
}

// 0x004916d0 (eax = fader): tail call of +0xf4's virtual +0x1c when present; otherwise returns EAX (the fader).
uint32_t fn_004916d0(uint8_t* fader)
{
    if (void* f = Field<void*>(fader, 0xf4))
        return VCall<uint32_t>(f, 0x1c / 4);
    return u(fader);
}

// 0x004927f0 (ecx = screen): with bit 0x800 of +0x4c draws the version text: sprintf (0x0053862e) "v%d.%02d" with 1,
// 0, the 2D setup 0x00415dc0 (esi = renderer, edi = Game+0xa98; stack 0, 1), renderer +0x190(1), Text_Draw(colour
// 0xffc0c0c0, ecx = game; Game+0xac0, text, 0.8, 0.9, 0.1, 1.0, 0, 2), +0x194, then restores the post-process
// saved state (+0x154(4, byte +0xe2), world +0, view +0x44, projection +0x88, viewport +0xcc) and clears +0x228.
uint32_t fn_004927f0(uint8_t* screen)
{
    char text[0x100];
    if (!(Field<uint32_t>(screen, 0x4c) & 0x800))
        return 0;
    orig::fn_0053862e(u(text), 0x0058fdfcu, 1, 0);
    orig::fn_00415dc0(Field<uint32_t>(g_game, 0x38), Field<uint32_t>(g_game, 0xa98), 0, 1);
    VCall<void>(Renderer(), 0x190 / 4, 1u);
    Text_Draw(0xffc0c0c0u, u(g_game), Field<uint32_t>(g_game, 0xac0), u(text), 0x3f4ccccd, 0x3f666666, 0x3dcccccd,
              0x3f800000, 0, 2);
    VCall<void>(Renderer(), 0x194 / 4);
    uint8_t* saved = Field<uint8_t*>(g_game, 0xa98);
    void* r = Renderer();
    VCall<void>(r, 0x154 / 4, 4u, static_cast<uint32_t>(Field<uint8_t>(saved, 0xe2)));
    VCall<void>(r, 0x54 / 4, saved);
    VCall<void>(r, 0x50 / 4, saved + 0x44);
    VCall<void>(r, 0x4c / 4, saved + 0x88);
    VCall<void>(r, 0x74 / 4, saved + 0xcc);
    Field<uint32_t>(saved, 0x228) = 0;
    return 0;
}

// 0x00493440 (ecx = menu item; caller 0x00415bb0): draws the item's image (+0x68) when its virtual +4(&width,
// &height) succeeds: quad centred at (0.5, 0.5) sized from the image (fn_004938e0), fitted to the screen
// (fn_004937a0, width * 5 / 4 with bit 0x8000 of +0x4c), drawn by image virtual +0(renderer, Game+0x40, Game+0x44, 0,
// quad). With +0x6c it then draws that caption (fn_00495cb0(+0x6c, image virtual +0x10, Game byte +0x176)).
uint32_t fn_00493440(uint8_t* item)
{
    alignas(8) float quad[8];
    uint32_t width = 0, height = 0;
    void* image = Field<void*>(item, 0x68);
    if (!image)
        return 0;
    if (static_cast<uint8_t>(VCall<uint32_t>(image, 4 / 4, &width, &height)))
    {
        fn_004938e0(quad, Field<uint32_t>(g_game, 0xa98), static_cast<int32_t>(width), static_cast<int32_t>(height),
                    0.5f, 0.5f);
        const uint32_t w = (Field<uint32_t>(item, 0x4c) & 0x8000) ? (width * 5) >> 2 : width;
        fn_004937a0(quad, quad, Field<uint32_t>(g_game, 0xa98), static_cast<int32_t>(w), 0);
        VCall<void>(Field<void*>(item, 0x68), 0, Field<uint32_t>(g_game, 0x38), Field<uint32_t>(g_game, 0x40),
                    Field<uint32_t>(g_game, 0x44), 0u, quad);
    }
    if (Field<uint32_t>(item, 0x6c))
    {
        const uint32_t flag = Field<uint8_t>(g_game, 0x176);
        const float t = VCall<float>(Field<void*>(item, 0x68), 0x10 / 4);
        fn_00495cb0(Field<uint32_t>(item, 0x6c), t, flag);
    }
    return 0;
}

// 0x004937a0 (esi = quad in, edi = quad out (may be the same); stack: unused, width, height (ints); callee pops 12):
// fits a quad (4 corners x, y) to the screen. Viewport = renderer virtual +0x7c, aspect = +0x88 (st0). Scales: both
// sizes <= 0: (1, 1/aspect); width >= height: t = 2 * viewport w / width, (t, t/aspect); else t = 2 * viewport h /
// height, (aspect * t, t). Each corner becomes (c - centre) * scale + 0.5 (x with the first scale, y the second).
uint32_t fn_004937a0(const float* in, float* out, uint32_t /*arg0*/, int32_t width, int32_t height)
{
    const float* vp = VCall<const float*>(Renderer(), 0x7c / 4);
    const float aspect = VCall<float>(Renderer(), 0x88 / 4);
    float sx, sy;
    if (width <= 0 && height <= 0)
    {
        sx = 1.0f;
        sy = K(0x005bcb2c) / aspect;
    }
    else if (width >= height)
    {
        const float t = (vp[0] + vp[0]) / static_cast<float>(width);
        sx = t;
        sy = t / aspect;
    }
    else
    {
        const float t = (vp[1] + vp[1]) / static_cast<float>(height);
        sx = aspect * t;
        sy = t;
    }
    const float cx = (((in[2] + in[0]) + in[4]) + in[6]) * K(0x005bcbe4);
    const float cy = (((in[3] + in[1]) + in[5]) + in[7]) * K(0x005bcbe4);
    out[0] = (in[0] - cx) * sx + K(0x005bcb4c);
    out[4] = (in[4] - cx) * sx + K(0x005bcb4c);
    out[2] = (in[2] - cx) * sx + K(0x005bcb4c);
    out[6] = (in[6] - cx) * sx + K(0x005bcb4c);
    out[1] = (in[1] - cy) * sy + K(0x005bcb4c);
    out[5] = (in[5] - cy) * sy + K(0x005bcb4c);
    out[3] = (in[3] - cy) * sy + K(0x005bcb4c);
    out[7] = (in[7] - cy) * sy + K(0x005bcb4c);
    return 0;
}

// 0x004938e0 (esi = quad out; stack: unused, width, height (ints), centre x, centre y; callee pops 20): a quad of
// half-size (width / (viewport w * 4), height / (viewport h * 4)) around the centre. Returns the viewport pointer.
uint32_t fn_004938e0(float* quad, uint32_t /*arg0*/, int32_t width, int32_t height, float cx, float cy)
{
    const float* vp = VCall<const float*>(Renderer(), 0x7c / 4);
    const float hx = static_cast<float>(width) / (vp[0] * K(0x005bcbe0));
    const float hy = static_cast<float>(height) / (vp[1] * K(0x005bcbe0));
    const float x0 = cx - hx;
    quad[4] = x0;
    quad[0] = x0;
    const float x1 = hx + cx;
    quad[6] = x1;
    quad[2] = x1;
    const float y0 = cy - hy;
    quad[7] = y0;
    quad[5] = y0;
    const float y1 = hy + cy;
    quad[3] = y1;
    quad[1] = y1;
    return u(vp);
}

// 0x00494b70 (callers 0x004010c0, 0x00401f40): the wanted-reward HUD. With [0x0063a158] and [0x0063a15c]: makes
// sure texture slot "b_wanted_reward" (0x0058fb40) of Game+0xa78 points at the image 0x005fec80 (0x004881c0 /
// 0x00488180, then 0x0045a740 (ebx = image, esi = [0x0063a15c]; stack Game+0xa84)), Game virtual +0x14, 2D setup
// 0x00415dc0, draws the image (0x0047cdd0 eax = 0, ecx = image; stack Game+0xa98, {0x004130ec, -1, [0x0063a15c],
// 0, 0, 0}, uv rect {0, 0, 1, 0, 0, 1, 1, 1}, 0), then the amount Game+0x570 as text (sprintf 0x0058fb38; Text_Draw
// (0; 4.0, 0.5, 0.2, 0.5, 0.5, 0)), 2D restore 0x00415d50 (al = 1) and Game virtual +0x18.
uint32_t WantedReward_Draw()
{
    if (!GameGlobal<uint32_t>(0x0063a158) || !GameGlobal<uint32_t>(0x0063a15c))
        return 0;
    if (orig::fn_004881c0(0x0058fb40u, Field<uint32_t>(g_game, 0xa78), 0, 0, 0, 0, 0, 0, 0, 0) != 0x005fec80u)
    {
        orig::fn_00488180(0x0058fb40u, Field<uint32_t>(g_game, 0xa78), 0x005fec80u);
        orig::fn_0045a740(0x005fec80u, GameGlobal<uint32_t>(0x0063a15c), Field<uint32_t>(g_game, 0xa84));
    }
    VCall<void>(g_game, 0x14 / 4);
    orig::fn_00415dc0(Field<uint32_t>(g_game, 0x38), Field<uint32_t>(g_game, 0xa98), 0, 1);
    alignas(8) uint32_t desc[6] = {0x004130ec, 0xffffffffu, GameGlobal<uint32_t>(0x0063a15c), 0, 0, 0};
    alignas(8) uint32_t rect[8] = {0, 0, 0x3f800000, 0, 0, 0x3f800000, 0x3f800000, 0x3f800000};
    orig::fn_0047cdd0(0, 0x005fec80u, Field<uint32_t>(g_game, 0xa98), u(desc), u(rect), 0);
    VCall<void>(Renderer(), 0x190 / 4, 1u);
    char text[0x40];
    orig::fn_0053862e(u(text), 0x0058fb38u, Field<uint32_t>(g_game, 0x570), 0);
    Text_Draw(0, u(text), Field<uint32_t>(g_game, 0xac0), u(text), 0x40800000, 0x3f000000, 0x3e4ccccd, 0x3f000000,
              0x3f000000, 0);
    const uint32_t r = VCall<uint32_t>(Renderer(), 0x194 / 4);
    orig::fn_00415d50((r & 0xffffff00u) | 1u, Field<uint32_t>(g_game, 0x38), Field<uint32_t>(g_game, 0xa98));
    VCall<void>(g_game, 0x18 / 4);
    return 0;
}

// 0x00495cb0 (stdcall, ret 12: id, time (float), force (byte); caller fn_00493440): draws a caption from the table at
// 0x005caddc (24-byte entries: colour, flags (bit 0: shown), width, x?, y?). Entry = 0x00495c50 (eax = id; stack:
// time); skipped unless `force` or flag bit 0; text = 0x00495f00 (eax = entry +0xc, ebx = Game+0xac0; stack 1). Saves
// the matrices (0x004f0170), sets the 2D projection (Projection2D_Set), renderer +0x190(1), width = 0x00465720 (ebx =
// text; stack: entry width, 576.0, 0), Text_Draw(colour; Game+0xac0, text, width, +0xc, +0x10, 0.5, 0.5, 2), +0x194,
// and restores world/view/projection. guess: movie subtitles.
uint32_t fn_00495cb0(uint32_t id, float time, uint32_t force)
{
    alignas(8) uint32_t saved[0x33];
    uint8_t* entry = B(orig::fn_00495c50(id, fb(time)));
    if (!entry)
        return 0;
    if (!static_cast<uint8_t>(force) && !(GameGlobal<uint8_t>(0x005cade0 + Field<uint32_t>(entry, 8) * 24) & 1))
        return 0;
    const uint32_t text = orig::fn_00495f00(Field<uint32_t>(entry, 0xc), Field<uint32_t>(g_game, 0xac0), 1);
    if (!text)
        return 0;
    orig::fn_004f0170(Field<uint32_t>(g_game, 0x38), u(saved));
    Projection2D_Set(Renderer());
    VCall<void>(Renderer(), 0x190 / 4, 1u);
    const float width =
        orig::fn_00465720(text, GameGlobal<uint32_t>(0x005cade4 + Field<uint32_t>(entry, 8) * 24), 0x44100000u, 0);
    const uint32_t row = Field<uint32_t>(entry, 8) * 24;
    Text_Draw(GameGlobal<uint32_t>(0x005caddc + row), Field<uint32_t>(g_game, 0xac0), Field<uint32_t>(g_game, 0xac0),
              text, fb(width), GameGlobal<uint32_t>(0x005cade8 + row), GameGlobal<uint32_t>(0x005cadec + row),
              0x3f000000, 0x3f000000, 2);
    VCall<void>(Renderer(), 0x194 / 4);
    void* r = Renderer();
    VCall<void>(r, 0x54 / 4, saved);
    VCall<void>(r, 0x50 / 4, saved + 0x11);
    return VCall<uint32_t>(r, 0x4c / 4, saved + 0x22);
}

// 0x00495e80 (eax = colour, ecx = unused (its slot is reused); stack: font (unused), text, x, y, a4, a5, a6, a7;
// callee pops 32): renderer virtual +0x1c8(text, colour | 0xff000000, &y, &a4, 0, a5, a6, x, x, 0, 0, 0, a7). The
// two pointers point at stack copies, as the original. Callers: the version text, captions, the wanted reward, ...
uint32_t Text_Draw(uint32_t colour, uint32_t /*ecx*/, uint32_t /*font*/, uint32_t text, uint32_t x, uint32_t y,
                   uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7)
{
    uint32_t yCopy = y;
    uint32_t a4Copy = a4;
    return VCall<uint32_t>(Renderer(), 0x1c8 / 4, text, colour | 0xff000000u, &yCopy, &a4Copy, 0u, a5, a6, x, x, 0u,
                           0u, 0u, a7);
}

// 0x00495fa0 (edx = first, esi = second; stack: unused, a1, scale (float); callee pops 12): unless either pointer is
// 0 or scale == 0.0: renderer virtual +0x1c0(first, second, a1, scale, scale, 0, 0, 0). guess: sprite draw.
uint32_t fn_00495fa0(uint32_t first, uint32_t second, uint32_t /*arg0*/, uint32_t a1, float scale)
{
    if (!first || !second || scale == K(0x0056c05c))
        return 0;
    return VCall<uint32_t>(Renderer(), 0x1c0 / 4, first, second, a1, fb(scale), fb(scale), 0u, 0u, 0u);
}

// ================================================================================================================
// Streamers (ribbon trails; "Streamers" heap name 0x0058fb08). Manager: +4 active list, +8 free list, +0xc blocks.
// Node (0x84 bytes, vtable 0x0058fad8): +4 next, +8 link to the previous next-field, +0xc parameters (0x4c bytes),
// +0x24 lifetime, +0x28 width curve, +0x38..+0x44 four ARGB key colours, +0x48/+0x4c/+0x50/+0x54 uv, +0x68 head,
// +0x6a tail, +0x6c mask, +0x6e count (16-bit), +0x70 flags (1 draw, 2 emitting), +0x74 time, +0x78/+0x7c last two
// points, +0x80 ring of 32-byte points (position, direction, time, size).
// ================================================================================================================

// 0x00494d40 (eax = parameters, esi saved only): default streamer parameters.
uint32_t fn_00494d40(uint8_t* p)
{
    Field<uint32_t>(p, 4) = 0xffffffffu;
    Field<uint32_t>(p, 0x2c) = 0xffffffffu;
    Field<uint32_t>(p, 0x30) = 0xffffffffu;
    Field<uint32_t>(p, 0x34) = 0xffffffffu;
    Field<uint32_t>(p, 0) = 0x133a0;
    Field<uint32_t>(p, 8) = 0;
    Field<uint32_t>(p, 0xc) = 0;
    Field<uint32_t>(p, 0x10) = 0;
    Field<uint32_t>(p, 0x14) = 0;
    Field<uint32_t>(p, 0x18) = 0x3f800000;
    Field<uint32_t>(p, 0x40) = 0;
    Field<uint32_t>(p, 0x3c) = 0;
    Field<uint32_t>(p, 0x48) = 0x3f800000;
    Field<uint32_t>(p, 0x44) = 0x3f800000;
    Field<uint32_t>(p, 0x1c) = 0x3f800000;
    Field<uint32_t>(p, 0x20) = 0x3f800000;
    Field<uint32_t>(p, 0x24) = 0x3f800000;
    Field<uint32_t>(p, 0x28) = 0;
    Field<uint32_t>(p, 0x38) = 0xffffff;
    return 0;
}

// 0x00494d90 (edx = manager): constructor: vtable 0x0058faf0, the global identity matrix 0x005feca8 (0x44 bytes),
// lists cleared. Returns the manager.
uint32_t Streamers_Construct(uint8_t* manager)
{
    Field<uint32_t>(manager, 0) = 0x0058faf0;
    std::memset(reinterpret_cast<void*>(0x005feca8), 0, 0x44);
    GameGlobal<uint32_t>(0x005feca8) = 0x3f800000;
    GameGlobal<uint32_t>(0x005fecbc) = 0x3f800000;
    GameGlobal<uint32_t>(0x005fecd0) = 0x3f800000;
    GameGlobal<uint32_t>(0x005fece4) = 0x3f800000;
    Field<uint32_t>(manager, 4) = 0;
    Field<uint32_t>(manager, 8) = 0;
    Field<uint32_t>(manager, 0xc) = 0;
    return u(manager);
}

// 0x00494df0 (esi = manager): frees the block list (+0xc, block +0 next) and clears the three lists.
uint32_t Streamers_Clear(uint8_t* manager)
{
    while (const uint32_t block = Field<uint32_t>(manager, 0xc))
    {
        const uint32_t next = Field<uint32_t>(P(block), 0);
        Free(block);
        Field<uint32_t>(manager, 0xc) = next;
    }
    Field<uint32_t>(manager, 4) = 0;
    Field<uint32_t>(manager, 8) = 0;
    Field<uint32_t>(manager, 0xc) = 0;
    return 0;
}

// 0x00494de0 (eax = manager): destructor body: vtable 0x0058faf0, Streamers_Clear.
uint32_t Streamers_Destruct(uint8_t* manager)
{
    Field<uint32_t>(manager, 0) = 0x0058faf0;
    return Streamers_Clear(manager);
}

// 0x00494e30 (stdcall, ret 12: manager, count, points per node): allocates a zeroed block ("Streamers") of `count`
// nodes plus their rings (points * 32 bytes each), links it into the block list and every node into the free list
// (+8) with default parameters, ring pointer (+0x80) and mask (+0x6c = points - 1).
uint32_t Streamers_AllocBlock(uint8_t* manager, int32_t count, uint32_t points)
{
    const uint32_t ringBytes = points << 5;
    const uint32_t size = (ringBytes + 0x84) * static_cast<uint32_t>(count) + 4;
    uint8_t* block = B(orig::fn_004e07f0(0x0063a210u, 1, size, 2, 0x0058fb08u));
    std::memset(block, 0, size);
    Field<uint32_t>(block, 0) = Field<uint32_t>(manager, 0xc);
    Field<uint8_t*>(manager, 0xc) = block;
    uint8_t* node = block + 4;
    uint8_t* ring = node + static_cast<uint32_t>(count) * 0x84;
    uint32_t result = 0;
    for (int32_t i = count; i > 0; --i)
    {
        if (node)
        {
            Field<uint32_t>(node, 0) = 0x0058fad8;
            fn_00494d40(node + 0xc);
        }
        StreamerList_Link(node, &Field<uint32_t>(manager, 8));
        Field<uint8_t*>(node, 0x80) = ring;
        Field<uint16_t>(node, 0x6c) = static_cast<uint16_t>(points - 1);
        result = points - 1;
        node += 0x84;
        ring += ringBytes;
    }
    return result;
}

// 0x00494ee0 (edi = manager; stack: parameters (or 0); callee pops 4): takes a node from the free list (allocating 32
// more with 64 points when empty), initialises it (fn_00495000) and moves it to the active list. Returns the node.
uint32_t Streamers_Spawn(uint8_t* manager, uint32_t parameters)
{
    if (!Field<uint32_t>(manager, 8))
        Streamers_AllocBlock(manager, 0x20, 0x40);
    uint8_t* node = Field<uint8_t*>(manager, 8);
    fn_00495000(node, reinterpret_cast<const uint32_t*>(parameters));
    StreamerList_Link(node, &Field<uint32_t>(manager, 4));
    return u(node);
}

// 0x00494f10 (eax = node; stack: unused; callee pops 4): stops emitting (fn_004952f0).
uint32_t Streamer_Stop(uint8_t* node, uint32_t /*arg0*/)
{
    return fn_004952f0(node);
}

// 0x00494f20 (eax = node, edx = manager): flags cleared (fn_00495040) and the node goes back to the free list.
uint32_t Streamers_Free(uint8_t* node, uint8_t* manager)
{
    fn_00495040(node);
    return StreamerList_Link(node, &Field<uint32_t>(manager, 8));
}

// 0x00494f30 (ebx = manager; stack: dt (float); callee pops 4; caller 0x00402e80): unless Game+0x2684 is set, ages
// every active node (Streamer_Update) and frees the ones that report 0.
uint32_t Streamers_Update(uint8_t* manager, float dt)
{
    if (Field<uint32_t>(g_game, 0x2684) || !Field<uint32_t>(manager, 4))
        return 0;
    for (uint8_t* node = Field<uint8_t*>(manager, 4); node;)
    {
        uint8_t* next = Field<uint8_t*>(node, 4);
        if (!Streamer_Update(node, dt))
            Streamers_Free(node, manager);
        node = next;
    }
    return 0;
}

// 0x00494f80 (edx = manager; caller ScenePass_Render flag 0x20): the camera matrix (0x0040fa50 eax = local 0x44
// bytes; stack: Game+0xa68), then Streamer_Draw for each active node.
uint32_t Streamers_Draw(uint8_t* manager)
{
    alignas(8) float camera[17];
    orig::fn_0040fa50(u(camera), Field<uint32_t>(g_game, 0xa68));
    for (uint8_t* node = Field<uint8_t*>(manager, 4); node; node = Field<uint8_t*>(node, 4))
        Streamer_Draw(camera, node);
    return 0;
}

// 0x00494fc0 (ecx = node, edx = list head field or 0): unlinks the node (+4 next, +8 pointer to the field that
// points at it) and pushes it on the given list. Returns &node +4.
uint32_t StreamerList_Link(uint8_t* node, uint32_t* head)
{
    uint32_t* link = &Field<uint32_t>(node, 4);
    if (uint8_t* next = Field<uint8_t*>(node, 4))
        Field<uint32_t>(next, 8) = Field<uint32_t>(node, 8);
    if (uint32_t* prev = Field<uint32_t*>(node, 8))
        *prev = *link;
    Field<uint32_t*>(node, 8) = head;
    if (!head)
    {
        *link = 0;
        return u(link);
    }
    *link = *head;
    *head = u(node);
    if (uint8_t* next = B(*link))
        Field<uint32_t*>(next, 8) = link;
    return u(link);
}

// 0x00495000 (eax = node, ecx = parameters or 0 (then the defaults at 0x0064a2b0)): flags = 3, parameters copied
// (0x13 dwords to +0xc), ring indices and the last points cleared.
uint32_t fn_00495000(uint8_t* node, const uint32_t* source)
{
    Field<uint32_t>(node, 0x70) = 3;
    if (!source)
        source = reinterpret_cast<const uint32_t*>(0x0064a2b0);
    std::memcpy(node + 0xc, source, 0x4c);
    Field<uint16_t>(node, 0x6a) = 0;
    Field<uint16_t>(node, 0x68) = 0;
    Field<uint16_t>(node, 0x6e) = 0;
    Field<uint32_t>(node, 0x7c) = 0;
    Field<uint32_t>(node, 0x78) = 0;
    Field<uint32_t>(node, 0x74) = 0;
    return 0;
}

// 0x00495040 (eax = node): flags = 0.
uint32_t fn_00495040(uint8_t* node)
{
    Field<uint32_t>(node, 0x70) = 0;
    return 0;
}

// 0x00495050 (ecx = node; stack: lifetime; callee pops 4): +0x24 = lifetime.
uint32_t Streamer_SetLifetime(uint8_t* node, uint32_t lifetime)
{
    Field<uint32_t>(node, 0x24) = lifetime;
    return lifetime;
}

// 0x00495060 (eax = 4 floats, ecx = node): width curve +0x28..+0x34.
uint32_t Streamer_SetWidthCurve(const uint32_t* v, uint8_t* node)
{
    Field<uint32_t>(node, 0x28) = v[0];
    Field<uint32_t>(node, 0x2c) = v[1];
    Field<uint32_t>(node, 0x30) = v[2];
    Field<uint32_t>(node, 0x34) = v[3];
    return v[3];
}

// 0x00495080 / 0x00495100 / 0x00495180 / 0x00495200 (esi = node, edi = 4 floats): one channel of the four key
// colours +0x38..+0x44: ftol(v * 255) shifted to bits 24 / 16 / 8 / 0 (not clamped), the rest of the colour kept.
static uint32_t Streamer_SetChannel(uint8_t* node, const float* v, uint32_t shift, uint32_t keep)
{
    uint32_t r = 0;
    for (uint32_t k = 0; k < 4; ++k)
    {
        const uint32_t c = static_cast<uint32_t>(Ftol(v[k] * K(0x005bcb34))) << shift;
        r = c | (Field<uint32_t>(node, 0x38 + k * 4) & keep);
        Field<uint32_t>(node, 0x38 + k * 4) = r;
    }
    return r;
}
uint32_t Streamer_SetAlpha(uint8_t* node, const float* v)
{
    return Streamer_SetChannel(node, v, 24, 0x00ffffffu);
}
uint32_t Streamer_SetRed(uint8_t* node, const float* v)
{
    return Streamer_SetChannel(node, v, 16, 0xff00ffffu);
}
uint32_t Streamer_SetGreen(uint8_t* node, const float* v)
{
    return Streamer_SetChannel(node, v, 8, 0xffff00ffu);
}
uint32_t Streamer_SetBlue(uint8_t* node, const float* v)
{
    return Streamer_SetChannel(node, v, 0, 0xffffff00u);
}

// 0x00495270 / 0x00495290 (eax = node; stack: two values; callee pops 8): uv pairs +0x48/+0x50 and +0x4c/+0x54.
uint32_t Streamer_SetU(uint8_t* node, uint32_t a, uint32_t b)
{
    Field<uint32_t>(node, 0x48) = a;
    Field<uint32_t>(node, 0x50) = b;
    return 0;
}
uint32_t Streamer_SetV(uint8_t* node, uint32_t a, uint32_t b)
{
    Field<uint32_t>(node, 0x4c) = a;
    Field<uint32_t>(node, 0x54) = b;
    return 0;
}

// 0x004952b0 (eax = node, ecx = texture): +0x14 = texture; render flags +0xc: | 0xc when set, &= ~4 when 0 (bit 8 is
// left set: original asymmetry, kept).
uint32_t Streamer_SetTexture(uint8_t* node, uint32_t texture)
{
    Field<uint32_t>(node, 0x14) = texture;
    const uint32_t f = Field<uint32_t>(node, 0xc);
    if (texture)
        Field<uint32_t>(node, 0xc) = f | 0xc;
    else
        Field<uint32_t>(node, 0xc) = f & 0xfffffffbu;
    return 0;
}

// 0x004952d0 (eax = additive flag, ecx = node): blend bits 8..11 of +0xc = 0x100 when set, else 0x300.
uint32_t Streamer_SetBlend(uint32_t additive, uint8_t* node)
{
    const uint32_t mode = additive ? 0x100u : 0x300u;
    const uint32_t v = mode | (Field<uint32_t>(node, 0xc) & 0xfffff0ffu);
    Field<uint32_t>(node, 0xc) = v;
    return v;
}

// 0x004952f0 (eax = node): clears the emitting flag (+0x70 bit 1).
uint32_t fn_004952f0(uint8_t* node)
{
    Field<uint32_t>(node, 0x70) &= 0xfffffffdu;
    return 0;
}

// 0x00495300 (ecx = node; stack: dt (float); callee pops 4): time += dt; every ring point older than time - lifetime
// moves the head past it and decrements the count. Returns 1 while emitting or points remain (head != the tail at
// entry), else 0 (the caller frees the node).
uint32_t Streamer_Update(uint8_t* node, float dt)
{
    const float now = dt + Field<float>(node, 0x74);
    Field<float>(node, 0x74) = now;
    const float cutoff = now - Field<float>(node, 0x24);
    uint32_t i = Field<uint16_t>(node, 0x68);
    const uint16_t tail = Field<uint16_t>(node, 0x6a);
    if (i != tail)
    {
        const uint8_t* ring = Field<uint8_t*>(node, 0x80);
        const uint32_t mask = Field<uint16_t>(node, 0x6c);
        do
        {
            if (Field<float>(ring + (i << 5), 0x18) < cutoff)
            {
                --Field<uint16_t>(node, 0x6e);
                Field<uint16_t>(node, 0x68) = static_cast<uint16_t>((i + 1) & mask);
            }
            i = (i + 1) & mask;
        } while (i != Field<uint16_t>(node, 0x6a));
    }
    if ((Field<uint8_t>(node, 0x70) & 2) || Field<uint16_t>(node, 0x68) != tail)
        return 1;
    return 0;
}

// 0x00495380 (eax = camera matrix, esi = node): draws one streamer as a camera-facing strip. Needs flag 1 and two
// points. Width curve (0x00495a20 on +0x28) and the four colour channels as cubics (0x0046d630 on the key colours'
// bytes * 1/255). State: Game+0x40 virtuals +0x14(params +0xc), +0xc(0), +0x10(1); identity world (0x005feca8);
// vertices from Game+0x44 +0x14(0, 2, 1, 0) and +4(count * 2) (24-byte vertices: position, ARGB, uv). Per point
// (head to tail): side = direction x camera axis (camera row 2, or row 1 when |direction . axis|^2 > 0.6, sticky),
// t = age / lifetime, half width = curve(t) * size, colour = channel cubics(t) * 255 (ftol, not clamped), u = lerp
// of +0x48..+0x50 by t, v = +0x4c / +0x54; two vertices point +- side * width. Game+0x44 +0x1c at the end.
uint32_t Streamer_Draw(const float* camera, uint8_t* node)
{
    if (!(Field<uint8_t>(node, 0x70) & 1) || Field<uint16_t>(node, 0x6e) < 2)
        return u(camera);
    alignas(8) float width[4];
    alignas(8) float red[4], green[4], blue[4], alpha[4];
    orig::fn_00495a20(u(node) + 0x28, u(width));
    const float k = K(0x005bcc18);
    auto channel = [&](uint32_t shift, uint32_t index) {
        return static_cast<float>(static_cast<int32_t>((Field<uint32_t>(node, 0x38 + index * 4) >> shift) & 0xff)) * k;
    };
    {
        const float c3 = channel(16, 3), c2 = channel(16, 2), c1 = channel(16, 1), c0 = channel(16, 0);
        orig::fn_0046d630(u(red), fb(c0), fb(c1), fb(c2), fb(c3));
    }
    {
        const float c3 = channel(8, 3), c2 = channel(8, 2), c1 = channel(8, 1), c0 = channel(8, 0);
        orig::fn_0046d630(u(green), fb(c0), fb(c1), fb(c2), fb(c3));
    }
    {
        const float c3 = channel(0, 3), c2 = channel(0, 2), c1 = channel(0, 1), c0 = channel(0, 0);
        orig::fn_0046d630(u(blue), fb(c0), fb(c1), fb(c2), fb(c3));
    }
    {
        const float c3 = channel(24, 3), c2 = channel(24, 2), c1 = channel(24, 1), c0 = channel(24, 0);
        orig::fn_0046d630(u(alpha), fb(c0), fb(c1), fb(c2), fb(c3));
    }
    const float invLife = K(0x005bcb2c) / Field<float>(node, 0x24);
    float axes[6] = {camera[8], camera[9], camera[10], camera[4], camera[5], camera[6]};
    uint32_t select = 0;
    const float* axis = axes;
    VCall<void>(Field<void*>(g_game, 0x40), 0x14 / 4, u(node) + 0xc);
    VCall<void>(Field<void*>(g_game, 0x40), 0xc / 4, 0u);
    VCall<void>(Field<void*>(g_game, 0x40), 0x10 / 4, 1u);
    VCall<void>(Renderer(), 0x54 / 4, 0x005feca8u);
    void* batch = VCall<void*>(Field<void*>(g_game, 0x44), 0x14 / 4, 0u, 2u, 1u, 0u);
    if (batch)
    {
        uint32_t* vtx = VCall<uint32_t*>(batch, 4 / 4, static_cast<uint32_t>(Field<uint16_t>(node, 0x6e)) * 2);
        uint32_t i = Field<uint16_t>(node, 0x68);
        if (i != Field<uint16_t>(node, 0x6a))
        {
            const float k255 = K(0x005bcb34);
            const float a3 = alpha[3] * k255;
            const float r3 = red[3] * k255;
            const float g3 = green[3] * k255;
            const float b3 = blue[3] * k255;
            do
            {
                const float* e = reinterpret_cast<const float*>(Field<uint8_t*>(node, 0x80) + (i << 5));
                const float d = (e[5] * axis[2] + e[4] * axis[1]) + axis[0] * e[3];
                if (d * d > K(0x005bccfc))
                {
                    select ^= 1;
                    axis = axes + select * 3;
                }
                const float sx = e[5] * axis[1] - axis[2] * e[4];
                const float sy = axis[2] * e[3] - e[5] * axis[0];
                const float sz = axis[0] * e[4] - axis[1] * e[3];
                const float t = (Field<float>(node, 0x74) - e[6]) * invLife;
                const float w = (((width[0] * t + width[1]) * t + width[2]) * t + width[3]) * e[7];
                uint32_t colour = static_cast<uint32_t>(Ftol(((alpha[0] * t + alpha[1]) * t + alpha[2]) * t * k255 + a3))
                                  << 8;
                colour = (colour | static_cast<uint32_t>(Ftol(((red[0] * t + red[1]) * t + red[2]) * t * k255 + r3)))
                         << 8;
                colour =
                    (colour | static_cast<uint32_t>(Ftol(((green[0] * t + green[1]) * t + green[2]) * t * k255 + g3)))
                    << 8;
                colour = colour | static_cast<uint32_t>(Ftol(((blue[0] * t + blue[1]) * t + blue[2]) * t * k255 + b3));
                const float ox = sx * w;
                vtx[0] = fb(ox + e[0]);
                const float oy = sy * w;
                vtx[1] = fb(oy + e[1]);
                const float oz = sz * w;
                vtx[3] = colour;
                vtx[2] = fb(oz + e[2]);
                const float u0 = Field<float>(node, 0x48);
                const float uu = (Field<float>(node, 0x50) - u0) * t + u0;
                vtx[4] = fb(uu);
                vtx[5] = Field<uint32_t>(node, 0x4c);
                vtx[6] = fb(e[0] - ox);
                vtx[7] = fb(e[1] - oy);
                vtx[10] = fb(uu);
                vtx[8] = fb(e[2] - oz);
                vtx[9] = colour;
                vtx[11] = Field<uint32_t>(node, 0x54);
                vtx += 12;
                i = (i + 1) & Field<uint16_t>(node, 0x6c);
            } while (i != Field<uint16_t>(node, 0x6a));
        }
    }
    return VCall<uint32_t>(Field<void*>(g_game, 0x44), 0x1c / 4);
}

// 0x004958a0 (eax = position, esi = node; caller 0x0041a350): while emitting, appends a point at the tail: position,
// time, size = smoothstep(time / (lifetime / 4)) (3x^2 - 2x^3) until a quarter of the lifetime, then 1.0; the
// previous point's direction = new - point before it, the new one's = new - previous (Streamer_SetDirection). The
// ring drops its oldest point when full, otherwise the count grows. Returns the new tail.
uint32_t Streamer_AddPoint(const uint32_t* position, uint8_t* node)
{
    if (!(Field<uint8_t>(node, 0x70) & 2))
        return u(position);
    uint8_t* e = Field<uint8_t*>(node, 0x80) + (static_cast<uint32_t>(Field<uint16_t>(node, 0x6a)) << 5);
    Field<uint32_t>(e, 0) = position[0];
    Field<uint32_t>(e, 4) = position[1];
    Field<uint32_t>(e, 8) = position[2];
    Field<uint32_t>(e, 0x18) = Field<uint32_t>(node, 0x74);
    const float q = Field<float>(node, 0x24) * K(0x005bcbe4);
    float size;
    if (q > Field<float>(node, 0x74))
    {
        const float x = Field<float>(node, 0x74) / q;
        const float x2 = x * x;
        const float a = K(0x005bcb7c) * x2;
        const float x3 = x2 * x;
        size = a - (x3 + x3);
    }
    else
    {
        size = K(0x005bcb2c);
    }
    Field<float>(e, 0x1c) = size;
    if (Field<uint32_t>(node, 0x78))
        Streamer_SetDirection(reinterpret_cast<const float*>(e), Field<uint8_t*>(node, 0x78),
                              Field<const float*>(node, 0x7c));
    else
        Field<uint8_t*>(node, 0x78) = e;
    Streamer_SetDirection(reinterpret_cast<const float*>(e), e, Field<const float*>(node, 0x78));
    const uint32_t mask = Field<uint16_t>(node, 0x6c);
    const uint32_t tail = static_cast<uint16_t>(Field<uint16_t>(node, 0x6a) + 1) & mask;
    Field<uint16_t>(node, 0x6a) = static_cast<uint16_t>(tail);
    if (static_cast<uint16_t>(tail) == Field<uint16_t>(node, 0x68))
        Field<uint16_t>(node, 0x68) = static_cast<uint16_t>((Field<uint16_t>(node, 0x68) + 1u) & mask);
    else
        ++Field<uint16_t>(node, 0x6e);
    const uint32_t previous = Field<uint32_t>(node, 0x78);
    Field<uint8_t*>(node, 0x78) = e;
    Field<uint32_t>(node, 0x7c) = previous;
    return tail;
}

// 0x004959b0 (eax = a, ecx = point, edx = b): point +0xc.. = normalise(a - b) (left unnormalised at length 0.0).
uint32_t Streamer_SetDirection(const float* a, uint8_t* out, const float* b)
{
    Field<float>(out, 0xc) = a[0] - b[0];
    Field<float>(out, 0x10) = a[1] - b[1];
    Field<float>(out, 0x14) = a[2] - b[2];
    const float x = Field<float>(out, 0xc);
    const float y = Field<float>(out, 0x10);
    const float z = Field<float>(out, 0x14);
    const float len = sqrtf((x * x + y * y) + z * z);
    if (len == K(0x0056c05c))
        return 0;
    const float inv = K(0x005bcb2c) / len;
    Field<float>(out, 0xc) = inv * Field<float>(out, 0xc);
    Field<float>(out, 0x10) = inv * Field<float>(out, 0x10);
    Field<float>(out, 0x14) = inv * Field<float>(out, 0x14);
    return 0;
}

// ================================================================================================================
// App (Game) renderer set-up methods (virtual; this +0x38 = renderer)
// ================================================================================================================

// 0x004e40e0 (ecx = game): renderer capability queries: +0x48 = (virtual +0x160(0) == 0), +0x4c/+0x50/+0x54/+0x58/
// +0x5c = +0x160(1, 2, 5, 6, 7); renderer +0x8c(0); +0x70(0, 0, (float)+0x4c, (float)+0x50) (screen size, guess).
uint32_t App_QueryRendererCaps(uint8_t* game)
{
    Field<uint32_t>(game, 0x48) = VCall<uint32_t>(Field<void*>(game, 0x38), 0x160 / 4, 0u) == 0 ? 1u : 0u;
    Field<uint32_t>(game, 0x4c) = VCall<uint32_t>(Field<void*>(game, 0x38), 0x160 / 4, 1u);
    Field<uint32_t>(game, 0x50) = VCall<uint32_t>(Field<void*>(game, 0x38), 0x160 / 4, 2u);
    Field<uint32_t>(game, 0x54) = VCall<uint32_t>(Field<void*>(game, 0x38), 0x160 / 4, 5u);
    Field<uint32_t>(game, 0x58) = VCall<uint32_t>(Field<void*>(game, 0x38), 0x160 / 4, 6u);
    Field<uint32_t>(game, 0x5c) = VCall<uint32_t>(Field<void*>(game, 0x38), 0x160 / 4, 7u);
    VCall<void>(Field<void*>(game, 0x38), 0x8c / 4, 0u);
    const float h = static_cast<float>(Field<int32_t>(game, 0x50));
    const float w = static_cast<float>(Field<int32_t>(game, 0x4c));
    return VCall<uint32_t>(Field<void*>(game, 0x38), 0x70 / 4, 0u, 0u, fb(w), fb(h));
}

// 0x004e4180 (ecx = game): +0x3c = renderer virtual +0x1c, +0x40 = +0x18, +0x44 (the batch drawer) = +0x20(5,
// 0x10000, 1), then batch virtual +0xc(+0x40).
uint32_t App_CreateBatch(uint8_t* game)
{
    Field<uint32_t>(game, 0x3c) = VCall<uint32_t>(Field<void*>(game, 0x38), 0x1c / 4);
    Field<uint32_t>(game, 0x40) = VCall<uint32_t>(Field<void*>(game, 0x38), 0x18 / 4);
    void* batch = VCall<void*>(Field<void*>(game, 0x38), 0x20 / 4, 5u, 0x10000u, 1u);
    Field<void*>(game, 0x44) = batch;
    return VCall<uint32_t>(batch, 0xc / 4, Field<uint32_t>(game, 0x40));
}

// ================================================================================================================
// Skinning
// ================================================================================================================

// 0x004ed8a0 (eax = bone pointers (+0 count, then pointers); stack: mesh group, bone instance, positions out, normals
// out (either may be 0); caller pops 16): software skinning of a group's vertices. Mesh = group +4; vertex stride
// [0x005fb8ac + influences * 4] (influences mesh +0x24); position / normal index lists mesh +0x48 / +0x4c from group
// +0x14; count group +0x18. With bones, influences, an instance and a subset (group +0x1c >= 0): palette (local,
// slot -1 = identity 0x004e28f0) = instance +0x1c + bone +0xa * 72 (identity for a missing bone); per vertex the
// position (vertex +0, influences from +0xc, weights after them, the last weight = 1 - the others) accumulates
// 0x004ed7d0 (eax = bone, ecx = vertex, edx = out; stack weight) and the normal (mesh +0x2c) the 3x3 rotation, the
// last through 0x004ed840. Otherwise copies positions and normals unchanged. The palette holds 255 entries in the
// original frame; more bones would overrun it (not reproduced: writes stop at 255).
uint32_t Skin_Deform(uint32_t* bones, uint8_t* group, uint8_t* instance, uint8_t* positions, uint8_t* normals)
{
    uint8_t* mesh = Field<uint8_t*>(group, 4);
    const uint32_t influences = Field<uint32_t>(mesh, 0x24);
    const uint32_t stride = GameGlobal<uint32_t>(0x005fb8ac + influences * 4);
    const uint32_t* posIndex = Field<uint32_t*>(mesh, 0x48) + Field<uint32_t>(group, 0x14);
    const uint32_t* nrmIndex = Field<uint32_t*>(mesh, 0x4c) + Field<uint32_t>(group, 0x14);
    if (bones && bones[0] && influences && instance && Field<int32_t>(group, 0x1c) >= 0)
    {
        alignas(8) uint32_t identity[17];
        alignas(8) uint32_t table[0x100];  // [0] = identity (esp+0x70), [1..] the palette (esp+0x74)
        uint32_t* palette = table + 1;
        orig::fn_004e28f0(u(identity));
        table[0] = u(identity);
        const uint32_t* b = bones + 1;
        uint32_t nb = Field<uint32_t>(mesh, 0x88);
        for (uint32_t k = 0; nb; --nb, ++k)
        {
            const uint32_t bone = *b++;
            const uint32_t m = bone ? Field<uint32_t>(instance, 0x1c) + static_cast<uint32_t>(Field<uint16_t>(P(bone), 0xa)) * 72
                                    : u(identity);
            if (k < 0xff)
                palette[k] = m;
        }
        for (uint32_t count = Field<uint32_t>(group, 0x18); count; --count, ++posIndex, ++nrmIndex)
        {
            const uint32_t pi = *posIndex;
            uint8_t* vertex = Field<uint8_t*>(mesh, 0x28) + pi * stride;
            if (positions)
            {
                float* out = reinterpret_cast<float*>(positions + pi * 12);
                out[2] = 0.0f;
                out[1] = 0.0f;
                out[0] = 0.0f;
                const uint32_t* index = reinterpret_cast<const uint32_t*>(vertex + 0xc);
                const float* weight = reinterpret_cast<const float*>(vertex + 0xc + Field<uint32_t>(mesh, 0x24) * 4);
                float rest = 1.0f;
                for (uint32_t k = Field<uint32_t>(mesh, 0x24) - 1; k; --k)
                {
                    const float w = *weight;
                    rest = rest - w;
                    orig::fn_004ed7d0(palette[*index], u(vertex), u(out), fb(w));
                    ++index;
                    ++weight;
                }
                orig::fn_004ed7d0(palette[*index], u(vertex), u(out), fb(rest));
            }
            if (normals)
            {
                const uint32_t ni = *nrmIndex;
                float rest = 1.0f;
                const float* src = reinterpret_cast<const float*>(Field<uint8_t*>(mesh, 0x2c) + ni * 12);
                float* out = reinterpret_cast<float*>(normals + ni * 12);
                const uint32_t* index = reinterpret_cast<const uint32_t*>(vertex + 0xc);
                const float* weight = reinterpret_cast<const float*>(vertex + 0xc + Field<uint32_t>(mesh, 0x24) * 4);
                out[2] = 0.0f;
                out[1] = 0.0f;
                out[0] = 0.0f;
                for (uint32_t k = Field<uint32_t>(mesh, 0x24) - 1; k; --k)
                {
                    rest = rest - *weight;
                    const float* m = reinterpret_cast<const float*>(palette[*index]);
                    const float w = *weight;
                    out[0] = ((m[1] * src[1] + m[2] * src[2]) + src[0] * m[0]) * w + out[0];
                    out[1] = ((m[4] * src[0] + m[5] * src[1]) + m[6] * src[2]) * w + out[1];
                    out[2] = ((m[8] * src[0] + m[9] * src[1]) + m[10] * src[2]) * w + out[2];
                    ++index;
                    ++weight;
                }
                orig::fn_004ed840(palette[*index], u(src), u(out), fb(rest));
            }
        }
        return 0;
    }
    for (uint32_t count = Field<uint32_t>(group, 0x18); count; --count, ++posIndex, ++nrmIndex)
    {
        if (positions)
        {
            const uint32_t pi = *posIndex;
            const uint32_t* v = reinterpret_cast<const uint32_t*>(pi * stride + Field<uint32_t>(mesh, 0x28));
            uint32_t* out = reinterpret_cast<uint32_t*>(positions + pi * 12);
            out[0] = v[0];
            out[1] = v[1];
            out[2] = v[2];
        }
        if (normals)
        {
            const uint32_t ni = *nrmIndex;
            const uint32_t* v = reinterpret_cast<const uint32_t*>(Field<uint8_t*>(mesh, 0x2c) + ni * 12);
            uint32_t* out = reinterpret_cast<uint32_t*>(normals + ni * 12);
            out[0] = v[0];
            out[1] = v[1];
            out[2] = v[2];
        }
    }
    return 0;
}

// 0x004edb80 (edi = model; stack: bones, instance, positions, normals; caller pops 16): Skin_Deform for every group
// of the model (+0x84, 0x2c bytes, count +0x80, signed). Returns the count.
uint32_t Skin_DeformAll(uint8_t* model, uint32_t bones, uint32_t instance, uint32_t positions, uint32_t normals)
{
    for (int32_t i = 0; i < Field<int32_t>(model, 0x80); ++i)
        Skin_Deform(reinterpret_cast<uint32_t*>(bones), Field<uint8_t*>(model, 0x84) + i * 0x2c, B(instance),
                    B(positions), B(normals));
    return Field<uint32_t>(model, 0x80);
}

// 0x004edbe0 (eax = palette out, edx = bone pointers; stack: group, instance; caller pops 8): the hardware matrix
// palette: count = mesh +0x88 bones, or the subset's (mesh +0x94 [group +0x1c], 24 bytes: +0x10 count, +0x14 int16
// bone indices). Entry = instance +0x1c + bone +0xa * 72, or 0 for a missing bone; all zero without bones/instance.
// Logs "Warning bone palette size greater than %d" (0x0058ae74) above [0x005ca89c]. Returns the count.
uint32_t Skin_BuildPalette(uint32_t* out, uint32_t* bones, uint8_t* group, uint8_t* instance)
{
    uint8_t* mesh = Field<uint8_t*>(group, 4);
    uint32_t count;
    const int16_t* list = nullptr;
    if (Field<int32_t>(group, 0x1c) < 0)
    {
        count = Field<uint32_t>(mesh, 0x88);
    }
    else
    {
        uint8_t* subset = Field<uint8_t*>(mesh, 0x94) + Field<int32_t>(group, 0x1c) * 24;
        list = Field<const int16_t*>(subset, 0x14);
        count = Field<uint32_t>(subset, 0x10);
    }
    if (bones && bones[0] && Field<uint32_t>(mesh, 0x24) && instance)
    {
        const uint32_t* b = bones + 1;
        const uint32_t* base = b;
        for (uint32_t k = count; k; --k)
        {
            uint32_t bone;
            if (Field<int32_t>(group, 0x1c) < 0)
                bone = *b++;
            else
                bone = base[*list++];
            *out++ = bone ? Field<uint32_t>(instance, 0x1c) + static_cast<uint32_t>(Field<uint16_t>(P(bone), 0xa)) * 72
                          : 0;
        }
    }
    else if (count)
    {
        std::memset(out, 0, count * 4);
    }
    if (static_cast<int32_t>(count) > GameGlobal<int32_t>(0x005ca89c))
        GameLog(reinterpret_cast<const char*>(0x0058ae74), GameGlobal<int32_t>(0x005ca89c));
    return count;
}

// 0x004edcb0 (eax = face normals out; stack: model, positions; caller pops 8; EBP frame): per triangle of every group
// (+0x84 + 0x20: index count, +0x28: indices, through the model's remap +0x48) the unit normal of (b - a) x (c - a)
// (component order as the original). A length at or below 1e-10 logs "Skin \"%s\", mesh \"%s\" has a degenerate
// triangle ..." (0x0058ae18; model +4 +0x34, 0x004eb280 name, the nine coordinates as doubles) and stores (0, 0, 1).
uint32_t Skin_FaceNormals(float* out, uint8_t* model, const uint8_t* positions)
{
    if (!out)
        return 0;
    uint32_t groups = Field<uint32_t>(model, 0x80);
    const uint32_t* remap = Field<uint32_t*>(model, 0x48);
    if (!groups)
        return 0;
    for (uint8_t* g = Field<uint8_t*>(model, 0x84) + 0x20; groups; --groups, g += 0x2c)
    {
        uint32_t count = Field<uint32_t>(g, 0);
        const uint32_t* tri = Field<uint32_t*>(g, 8);
        if (!count)
            continue;
        do
        {
            const float* a = reinterpret_cast<const float*>(positions + remap[tri[0]] * 12);
            const float* b = reinterpret_cast<const float*>(positions + remap[tri[1]] * 12);
            const float* c = reinterpret_cast<const float*>(positions + remap[tri[2]] * 12);
            out[0] = (c[2] - a[2]) * (b[1] - a[1]) - (b[2] - a[2]) * (c[1] - a[1]);
            out[1] = (b[2] - a[2]) * (c[0] - a[0]) - (b[0] - a[0]) * (c[2] - a[2]);
            out[2] = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
            const float x = out[0], y = out[1], z = out[2];
            const float len = sqrtf((x * x + y * y) + z * z);
            if (len > K(0x005bcd80))
            {
                const float inv = K(0x005bcb2c) / len;
                out[0] = inv * out[0];
                out[1] = inv * out[1];
                out[2] = inv * out[2];
            }
            else
            {
                const uint32_t meshName = orig::fn_004eb280(u(a), u(model));
                GameLog(reinterpret_cast<const char*>(0x0058ae18), Field<uint32_t>(model, 4) + 0x34, meshName,
                        static_cast<double>(a[0]), static_cast<double>(a[1]), static_cast<double>(a[2]),
                        static_cast<double>(b[0]), static_cast<double>(b[1]), static_cast<double>(b[2]),
                        static_cast<double>(c[0]), static_cast<double>(c[1]), static_cast<double>(c[2]));
                out[1] = 0.0f;
                out[0] = 0.0f;
                Field<uint32_t>(out, 8) = 0x3f800000;
            }
            count -= 3;
            tri += 3;
            out += 3;
        } while (count != 0);
    }
    return 0;
}

// 0x004ede80 (eax = model, ecx = vertex normals out, edi = face normals; stack: normalise (byte); caller pops 4): per
// vertex (count model +0x14) the sum of its faces' normals (adjacency model +0x3c: count, then face indices);
// optionally normalised, a length below 1e-10 taking the last face normal used instead (the out base itself before
// any face was read). A vertex without faces keeps its previous contents.
uint32_t Skin_VertexNormals(uint8_t* model, float* out, const float* faceNormals, uint32_t normalize)
{
    if (!out || !faceNormals)
        return 0;
    const uint32_t* adjacency = Field<uint32_t*>(model, 0x3c);
    if (!adjacency)
        return 0;
    uint32_t vertices = Field<uint32_t>(model, 0x14);
    if (!vertices)
        return 0;
    const uint32_t* last = reinterpret_cast<const uint32_t*>(out);
    for (float* o = out; vertices; --vertices, o += 3)
    {
        const uint32_t faces = *adjacency;
        const uint32_t* f = adjacency + 1;
        if (faces)
        {
            last = reinterpret_cast<const uint32_t*>(faceNormals + *f * 3);
            uint32_t* oi = reinterpret_cast<uint32_t*>(o);
            oi[0] = last[0];
            oi[1] = last[1];
            oi[2] = last[2];
            ++f;
            for (uint32_t k = faces - 1; k; --k, ++f)
            {
                const float* n = faceNormals + *f * 3;
                last = reinterpret_cast<const uint32_t*>(n);
                o[0] = n[0] + o[0];
                o[1] = n[1] + o[1];
                o[2] = n[2] + o[2];
            }
        }
        adjacency = f;
        if (static_cast<uint8_t>(normalize))
        {
            const float z = o[2], y = o[1], x = o[0];
            const float len = sqrtf((z * z + x * x) + y * y);
            if (len < K(0x005bcd80))
            {
                uint32_t* oi = reinterpret_cast<uint32_t*>(o);
                oi[0] = last[0];
                oi[1] = last[1];
                oi[2] = last[2];
            }
            else
            {
                const float inv = K(0x005bcb2c) / len;
                o[0] = inv * o[0];
                o[1] = inv * o[1];
                o[2] = inv * o[2];
            }
        }
    }
    return 0;
}

// ================================================================================================================
// 2D projection, world to screen, colour parsing
// ================================================================================================================

// 0x004f01c0 (stdcall, ret 4: renderer): the 2D (screen-space) transforms. Built once while [0x005fb898] is set:
// projection 0x0064aab0 = 0x004e3490(pi/2, 1, 10000, 1), view 0x0064aa68 = 0x004e2910 of {1, 0, 0, -0.5; 0, 1, 0,
// -0.5; 0, 0, 1, -1; 0, 0, 0, 1; flags 0x10} with a (2, 2, 1) scale (0x004e2d90), world 0x0064aa20 = identity. Then
// sets them (renderer virtuals +0x4c, +0x50, +0x54).
uint32_t Projection2D_Set(void* renderer)
{
    if (GameGlobal<uint8_t>(0x005fb898))
    {
        orig::fn_004e3490(0x0064aab0u, 0x3fc90fdbu, 0x3f800000u, 0x461c4000u, 0x3f800000u);
        alignas(8) uint32_t scale[3] = {0x40000000, 0x40000000, 0x3f800000};
        alignas(8) uint32_t offset[17];
        alignas(8) uint32_t scaling[17];
        orig::fn_004e2d90(u(scaling), u(scale), 0, 0, 0);
        std::memset(offset, 0, sizeof(offset));
        offset[0] = 0x3f800000;
        offset[5] = 0x3f800000;
        offset[10] = 0x3f800000;
        offset[15] = 0x3f800000;
        offset[3] = 0xbf000000;
        offset[7] = 0xbf000000;
        offset[11] = 0xbf800000;
        offset[16] = 0x10;
        orig::fn_004e2910(u(offset), 0x0064aa68u, u(scaling));
        std::memset(reinterpret_cast<void*>(0x0064aa20), 0, 0x44);
        GameGlobal<uint32_t>(0x0064aa20) = 0x3f800000;
        GameGlobal<uint32_t>(0x0064aa34) = 0x3f800000;
        GameGlobal<uint32_t>(0x0064aa48) = 0x3f800000;
        GameGlobal<uint32_t>(0x0064aa5c) = 0x3f800000;
        GameGlobal<uint8_t>(0x005fb898) = 0;
    }
    VCall<void>(renderer, 0x4c / 4, 0x0064aab0u);
    VCall<void>(renderer, 0x50 / 4, 0x0064aa68u);
    return VCall<uint32_t>(renderer, 0x54 / 4, 0x0064aa20u);
}

// 0x004f02f0 (ebx = out (3 floats), esi = renderer, edi = world point): out = ((x / w + 1) * 0.5, (y / w + 1) * 0.5,
// 0) of the point through view (virtual +0x64) x projection (+0x60) (0x004e2910, then 0x004e3950 edx = 1 on (p, 1)).
// Callers 0x00406840, 0x00407c20 (HUD markers).
uint32_t World_ToScreen(float* out, void* renderer, const float* point)
{
    const uint32_t view = VCall<uint32_t>(renderer, 0x64 / 4);
    const uint32_t projection = VCall<uint32_t>(renderer, 0x60 / 4);
    alignas(8) uint32_t viewProjection[17];
    orig::fn_004e2910(view, u(viewProjection), projection);
    alignas(8) float in[4] = {point[0], point[1], point[2], 1.0f};
    alignas(8) float clip[4];
    orig::fn_004e3950(u(viewProjection), u(clip), 1, u(in));
    const float inv = K(0x005bcb2c) / clip[3];
    Field<uint32_t>(out, 8) = 0;
    const float x = clip[0] * inv;
    clip[1] = inv * clip[1];
    out[0] = (x + K(0x005bcb2c)) * K(0x005bcb4c);
    out[1] = (clip[1] + K(0x005bcb2c)) * K(0x005bcb4c);
    return 0;
}

// 0x004f0b00 (edx = wide string): parses a hex colour: decimal digits only among the first six characters, hex
// letters a-f/A-F at any position (original quirk: letters past the sixth keep being stored, beyond the 20-dword
// frame they would overwrite the stack; stopped at 20 here). Value = sum of digit * ftol(pow(16.0, position from the
// right)). Caller 0x00507370.
uint32_t Colour_ParseHex(const uint16_t* s)
{
    int32_t digits[20];
    uint32_t n = 0;
    for (;;)
    {
        const uint16_t c = *s;
        if (!(n < 6 && c >= 0x30 && c <= 0x39))
        {
            const uint16_t lower = (c >= 0x41 && c <= 0x5a) ? static_cast<uint16_t>(c + 0x20) : c;
            if (lower < 0x61 || lower > 0x66)
                break;
        }
        if (c >= 0x30 && c <= 0x39)
            digits[n] = c - 0x30;
        else
            digits[n] = ((c >= 0x41 && c <= 0x5a) ? c + 0x20 : c) - 0x57;
        ++n;
        ++s;
        if (n >= 20)
            break;
    }
    uint32_t value = 0;
    float exponent = 0.0f;
    while (n)
    {
        --n;
        const int32_t power = static_cast<int32_t>(static_cast<int64_t>(std::pow(16.0, static_cast<double>(exponent))));
        value += static_cast<uint32_t>(power * digits[n]);
        exponent = exponent + K(0x005bcb2c);
    }
    return value;
}

// ================================================================================================================
// ABI adapters, entry stubs, hooks
// ================================================================================================================

static uint32_t fn_00487e30_Abi(uint32_t ecx, uint32_t esi)
{
    return fn_00487e30(ecx, reinterpret_cast<uint32_t*>(esi));
}
static uint32_t ScenePass_Construct_Abi(uint32_t edi)
{
    return ScenePass_Construct(B(edi));
}
static uint32_t ScenePass_Destruct_Abi(uint32_t esi)
{
    return ScenePass_Destruct(B(esi));
}
static uint32_t fn_00487ff0_Abi(uint32_t eax)
{
    return fn_00487ff0(B(eax));
}
static uint32_t ScenePass_Release_Abi(uint32_t eax)
{
    return ScenePass_Release(B(eax));
}
static uint32_t fn_004880d0_Abi(uint32_t edi)
{
    return fn_004880d0(B(edi));
}
static uint32_t fn_00488120_Abi(uint32_t eax, uint32_t ecx)
{
    return fn_00488120(eax, B(ecx));
}
static uint32_t fn_00488130_Abi(uint32_t eax, uint32_t ecx)
{
    return fn_00488130(eax, B(ecx));
}
static uint32_t fn_00488230_Abi(uint32_t esi)
{
    return fn_00488230(B(esi));
}
static uint32_t fn_00488850_Abi(uint32_t esi)
{
    return fn_00488850(B(esi));
}
static uint32_t ScenePass_GetFrustum_Abi(uint32_t eax, uint32_t edx)
{
    return ScenePass_GetFrustum(B(eax), reinterpret_cast<uint32_t*>(edx));
}
static uint32_t fn_00488b00_Abi(uint32_t eax, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4,
                                uint32_t a5)
{
    return fn_00488b00(B(eax), B(a0), P(a1), a2, a3, a4, a5);
}
static uint32_t SortedTree_Draw_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return SortedTree_Draw(B(ecx), a0, reinterpret_cast<uint32_t*>(a1), reinterpret_cast<uint32_t*>(a2));
}
static uint32_t CubeMap_RenderScene_Abi(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
    return CubeMap_RenderScene(P(a0), a1, a2, a3, a4, a5);
}
static uint32_t ScenePass_RenderToTexture_Abi(uint32_t ecx, uint32_t ebx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return ScenePass_RenderToTexture(ecx, ebx, P(a0), a1, a2);
}
static uint32_t ScenePass_RenderMain_Abi(uint32_t eax, uint32_t edx, uint32_t a0)
{
    return ScenePass_RenderMain(eax, P(edx), a0);
}
static uint32_t Material_IsTranslucent_Abi(uint32_t eax, uint32_t a0, uint32_t a1)
{
    return Material_IsTranslucent(B(eax), a0, bf(a1));
}
static uint32_t fn_004897d0_Abi(uint32_t esi, uint32_t edi, uint32_t a0, uint32_t a1)
{
    return fn_004897d0(reinterpret_cast<uint32_t*>(esi), B(edi), a0, bf(a1));
}
static uint32_t fn_00489910_Abi(uint32_t esi)
{
    return fn_00489910(B(esi));
}
static uint32_t fn_00489a20_Abi(uint32_t eax, uint32_t edi, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return fn_00489a20(reinterpret_cast<const float*>(eax), B(edi), B(a0), a1, bf(a2));
}
static uint32_t Mesh_FillDynamicVertices_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3,
                                             uint32_t a4)
{
    return Mesh_FillDynamicVertices(B(ecx), a0, B(a1), P(a2), B(a3), B(a4));
}
static float fn_0048a030_Abi(uint32_t eax, uint32_t ecx, uint32_t a0)
{
    return fn_0048a030(B(eax), ecx, a0);
}
static uint32_t ItemList_Add_Abi(uint32_t eax, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return ItemList_Add(B(eax), reinterpret_cast<const uint32_t*>(a0), P(a1), a2);
}
static uint32_t Material_BindTextures_Abi(uint32_t esi)
{
    return Material_BindTextures(B(esi));
}
static uint32_t MeshGroup_Draw_Abi(uint32_t ebx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4,
                                   uint32_t a5)
{
    return MeshGroup_Draw(P(ebx), B(a0), a1, B(a2), a3, P(a4), a5);
}
static uint32_t LightSlot_Acquire_Abi(uint32_t eax, uint32_t a0, uint32_t a1)
{
    return LightSlot_Acquire(static_cast<int32_t>(eax), a0, a1);
}
static uint32_t LightSlot_Release_Abi(uint32_t eax, uint32_t a0)
{
    return LightSlot_Release(eax, a0);
}
static uint32_t Object_DrawGroups_Abi(uint32_t edi, uint32_t a0)
{
    return Object_DrawGroups(P(edi), B(a0));
}
static uint32_t fn_0048eb20_Abi(uint32_t eax, uint32_t ebx, uint32_t edi, uint32_t a0)
{
    return fn_0048eb20(reinterpret_cast<uint32_t*>(eax), ebx, edi, B(a0));
}
static uint32_t World_WalkCells_Abi(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    return World_WalkCells(B(a0), reinterpret_cast<const uint32_t*>(a1), a2, a3, a4);
}
static uint32_t fn_00491670_Abi(uint32_t esi)
{
    return fn_00491670(B(esi));
}
static uint32_t fn_004916d0_Abi(uint32_t eax)
{
    return fn_004916d0(B(eax));
}
static uint32_t fn_004927f0_Abi(uint32_t ecx)
{
    return fn_004927f0(B(ecx));
}
static uint32_t fn_00493440_Abi(uint32_t ecx)
{
    return fn_00493440(B(ecx));
}
static uint32_t fn_004937a0_Abi(uint32_t esi, uint32_t edi, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return fn_004937a0(reinterpret_cast<const float*>(esi), reinterpret_cast<float*>(edi), a0, static_cast<int32_t>(a1),
                       static_cast<int32_t>(a2));
}
static uint32_t fn_004938e0_Abi(uint32_t esi, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    return fn_004938e0(reinterpret_cast<float*>(esi), a0, static_cast<int32_t>(a1), static_cast<int32_t>(a2), bf(a3),
                       bf(a4));
}
static uint32_t WantedReward_Draw_Abi()
{
    return WantedReward_Draw();
}
static uint32_t fn_00494d40_Abi(uint32_t eax, uint32_t /*esi*/)
{
    return fn_00494d40(B(eax));
}
static uint32_t Streamers_Construct_Abi(uint32_t edx)
{
    return Streamers_Construct(B(edx));
}
static uint32_t Streamers_Destruct_Abi(uint32_t eax)
{
    return Streamers_Destruct(B(eax));
}
static uint32_t Streamers_Clear_Abi(uint32_t esi)
{
    return Streamers_Clear(B(esi));
}
static uint32_t Streamers_AllocBlock_Abi(uint32_t a0, uint32_t a1, uint32_t a2)
{
    return Streamers_AllocBlock(B(a0), static_cast<int32_t>(a1), a2);
}
static uint32_t Streamers_Spawn_Abi(uint32_t edi, uint32_t a0)
{
    return Streamers_Spawn(B(edi), a0);
}
static uint32_t Streamer_Stop_Abi(uint32_t eax, uint32_t a0)
{
    return Streamer_Stop(B(eax), a0);
}
static uint32_t Streamers_Free_Abi(uint32_t eax, uint32_t edx)
{
    return Streamers_Free(B(eax), B(edx));
}
static uint32_t Streamers_Update_Abi(uint32_t ebx, uint32_t a0)
{
    return Streamers_Update(B(ebx), bf(a0));
}
static uint32_t Streamers_Draw_Abi(uint32_t edx)
{
    return Streamers_Draw(B(edx));
}
static uint32_t StreamerList_Link_Abi(uint32_t ecx, uint32_t edx)
{
    return StreamerList_Link(B(ecx), reinterpret_cast<uint32_t*>(edx));
}
static uint32_t fn_00495000_Abi(uint32_t eax, uint32_t ecx)
{
    return fn_00495000(B(eax), reinterpret_cast<const uint32_t*>(ecx));
}
static uint32_t fn_00495040_Abi(uint32_t eax)
{
    return fn_00495040(B(eax));
}
static uint32_t Streamer_SetLifetime_Abi(uint32_t ecx, uint32_t a0)
{
    return Streamer_SetLifetime(B(ecx), a0);
}
static uint32_t Streamer_SetWidthCurve_Abi(uint32_t eax, uint32_t ecx)
{
    return Streamer_SetWidthCurve(reinterpret_cast<const uint32_t*>(eax), B(ecx));
}
static uint32_t Streamer_SetAlpha_Abi(uint32_t esi, uint32_t edi)
{
    return Streamer_SetAlpha(B(esi), reinterpret_cast<const float*>(edi));
}
static uint32_t Streamer_SetRed_Abi(uint32_t esi, uint32_t edi)
{
    return Streamer_SetRed(B(esi), reinterpret_cast<const float*>(edi));
}
static uint32_t Streamer_SetGreen_Abi(uint32_t esi, uint32_t edi)
{
    return Streamer_SetGreen(B(esi), reinterpret_cast<const float*>(edi));
}
static uint32_t Streamer_SetBlue_Abi(uint32_t esi, uint32_t edi)
{
    return Streamer_SetBlue(B(esi), reinterpret_cast<const float*>(edi));
}
static uint32_t Streamer_SetU_Abi(uint32_t eax, uint32_t a0, uint32_t a1)
{
    return Streamer_SetU(B(eax), a0, a1);
}
static uint32_t Streamer_SetV_Abi(uint32_t eax, uint32_t a0, uint32_t a1)
{
    return Streamer_SetV(B(eax), a0, a1);
}
static uint32_t Streamer_SetTexture_Abi(uint32_t eax, uint32_t ecx)
{
    return Streamer_SetTexture(B(eax), ecx);
}
static uint32_t Streamer_SetBlend_Abi(uint32_t eax, uint32_t ecx)
{
    return Streamer_SetBlend(eax, B(ecx));
}
static uint32_t fn_004952f0_Abi(uint32_t eax)
{
    return fn_004952f0(B(eax));
}
static uint32_t Streamer_Update_Abi(uint32_t ecx, uint32_t a0)
{
    return Streamer_Update(B(ecx), bf(a0));
}
static uint32_t Streamer_Draw_Abi(uint32_t eax, uint32_t esi)
{
    return Streamer_Draw(reinterpret_cast<const float*>(eax), B(esi));
}
static uint32_t Streamer_AddPoint_Abi(uint32_t eax, uint32_t esi)
{
    return Streamer_AddPoint(reinterpret_cast<const uint32_t*>(eax), B(esi));
}
static uint32_t Streamer_SetDirection_Abi(uint32_t eax, uint32_t ecx, uint32_t edx)
{
    return Streamer_SetDirection(reinterpret_cast<const float*>(eax), B(ecx), reinterpret_cast<const float*>(edx));
}
static uint32_t fn_00495cb0_Abi(uint32_t a0, uint32_t a1, uint32_t a2)
{
    return fn_00495cb0(a0, bf(a1), a2);
}
static uint32_t Text_Draw_Abi(uint32_t eax, uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3,
                              uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7)
{
    return Text_Draw(eax, ecx, a0, a1, a2, a3, a4, a5, a6, a7);
}
static uint32_t fn_00495fa0_Abi(uint32_t edx, uint32_t esi, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return fn_00495fa0(edx, esi, a0, a1, bf(a2));
}
static uint32_t App_QueryRendererCaps_Abi(uint32_t ecx)
{
    return App_QueryRendererCaps(B(ecx));
}
static uint32_t App_CreateBatch_Abi(uint32_t ecx)
{
    return App_CreateBatch(B(ecx));
}
static uint32_t Skin_Deform_Abi(uint32_t eax, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return Skin_Deform(reinterpret_cast<uint32_t*>(eax), B(a0), B(a1), B(a2), B(a3));
}
static uint32_t Skin_DeformAll_Abi(uint32_t edi, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return Skin_DeformAll(B(edi), a0, a1, a2, a3);
}
static uint32_t Skin_BuildPalette_Abi(uint32_t eax, uint32_t edx, uint32_t a0, uint32_t a1)
{
    return Skin_BuildPalette(reinterpret_cast<uint32_t*>(eax), reinterpret_cast<uint32_t*>(edx), B(a0), B(a1));
}
static uint32_t Skin_FaceNormals_Abi(uint32_t eax, uint32_t a0, uint32_t a1)
{
    return Skin_FaceNormals(reinterpret_cast<float*>(eax), B(a0), B(a1));
}
static uint32_t Skin_VertexNormals_Abi(uint32_t eax, uint32_t ecx, uint32_t edi, uint32_t a0)
{
    return Skin_VertexNormals(B(eax), reinterpret_cast<float*>(ecx), reinterpret_cast<const float*>(edi), a0);
}
static uint32_t Projection2D_Set_Abi(uint32_t a0)
{
    return Projection2D_Set(P(a0));
}
static uint32_t World_ToScreen_Abi(uint32_t ebx, uint32_t esi, uint32_t edi)
{
    return World_ToScreen(reinterpret_cast<float*>(ebx), P(esi), reinterpret_cast<const float*>(edi));
}
static uint32_t Colour_ParseHex_Abi(uint32_t edx)
{
    return Colour_ParseHex(reinterpret_cast<const uint16_t*>(edx));
}

ABI_ENTRY_00487e30(fn_00487e30_Entry, fn_00487e30_Abi)
HOOK(0x00487e30, "fn_00487e30", fn_00487e30_Entry, nullptr);
ABI_ENTRY_00487f00(ScenePass_Construct_Entry, ScenePass_Construct_Abi)
HOOK(0x00487f00, "ScenePass_Construct", ScenePass_Construct_Entry, nullptr);
ABI_ENTRY_00487fc0(ScenePass_Destruct_Entry, ScenePass_Destruct_Abi)
HOOK(0x00487fc0, "ScenePass_Destruct", ScenePass_Destruct_Entry, nullptr);
ABI_ENTRY_00487ff0(fn_00487ff0_Entry, fn_00487ff0_Abi)
HOOK(0x00487ff0, "fn_00487ff0", fn_00487ff0_Entry, nullptr);
ABI_ENTRY_00488060(ScenePass_Release_Entry, ScenePass_Release_Abi)
HOOK(0x00488060, "ScenePass_Release", ScenePass_Release_Entry, nullptr);
ABI_ENTRY_004880d0(fn_004880d0_Entry, fn_004880d0_Abi)
HOOK(0x004880d0, "fn_004880d0", fn_004880d0_Entry, nullptr);
ABI_ENTRY_00488120(fn_00488120_Entry, fn_00488120_Abi)
HOOK(0x00488120, "fn_00488120", fn_00488120_Entry, nullptr);
ABI_ENTRY_00488130(fn_00488130_Entry, fn_00488130_Abi)
HOOK(0x00488130, "fn_00488130", fn_00488130_Entry, nullptr);
ABI_ENTRY_00488230(fn_00488230_Entry, fn_00488230_Abi)
HOOK(0x00488230, "fn_00488230", fn_00488230_Entry, nullptr);
ABI_ENTRY_00488850(fn_00488850_Entry, fn_00488850_Abi)
HOOK(0x00488850, "fn_00488850", fn_00488850_Entry, nullptr);
ABI_ENTRY_00488990(ScenePass_GetFrustum_Entry, ScenePass_GetFrustum_Abi)
HOOK(0x00488990, "ScenePass_GetFrustum", ScenePass_GetFrustum_Entry, nullptr);
ABI_ENTRY_00488b00(fn_00488b00_Entry, fn_00488b00_Abi)
HOOK(0x00488b00, "fn_00488b00", fn_00488b00_Entry, nullptr);
ABI_ENTRY_00488df0(SortedTree_Draw_Entry, SortedTree_Draw_Abi)
HOOK(0x00488df0, "SortedTree_Draw", SortedTree_Draw_Entry, nullptr);
ABI_ENTRY_00489320(CubeMap_RenderScene_Entry, CubeMap_RenderScene_Abi)
HOOK(0x00489320, "CubeMap_RenderScene", CubeMap_RenderScene_Entry, nullptr);
ABI_ENTRY_00489540(ScenePass_RenderToTexture_Entry, ScenePass_RenderToTexture_Abi)
HOOK(0x00489540, "ScenePass_RenderToTexture", ScenePass_RenderToTexture_Entry, nullptr);
ABI_ENTRY_00489610(ScenePass_RenderMain_Entry, ScenePass_RenderMain_Abi)
HOOK(0x00489610, "ScenePass_RenderMain", ScenePass_RenderMain_Entry, nullptr);
ABI_ENTRY_004897a0(Material_IsTranslucent_Entry, Material_IsTranslucent_Abi)
HOOK(0x004897a0, "Material_IsTranslucent", Material_IsTranslucent_Entry, nullptr);
ABI_ENTRY_004897d0(fn_004897d0_Entry, fn_004897d0_Abi)
HOOK(0x004897d0, "fn_004897d0", fn_004897d0_Entry, nullptr);
ABI_ENTRY_00489910(fn_00489910_Entry, fn_00489910_Abi)
HOOK(0x00489910, "fn_00489910", fn_00489910_Entry, nullptr);
ABI_ENTRY_00489a20(fn_00489a20_Entry, fn_00489a20_Abi)
HOOK(0x00489a20, "fn_00489a20", fn_00489a20_Entry, nullptr);
ABI_ENTRY_00489f30(Mesh_FillDynamicVertices_Entry, Mesh_FillDynamicVertices_Abi)
HOOK(0x00489f30, "Mesh_FillDynamicVertices", Mesh_FillDynamicVertices_Entry, nullptr);
ABI_ENTRY_0048a030(fn_0048a030_Entry, fn_0048a030_Abi)
HOOK(0x0048a030, "fn_0048a030", fn_0048a030_Entry, nullptr);
ABI_ENTRY_0048a0d0(ItemList_Add_Entry, ItemList_Add_Abi)
HOOK(0x0048a0d0, "ItemList_Add", ItemList_Add_Entry, nullptr);
ABI_ENTRY_0048a210(Material_BindTextures_Entry, Material_BindTextures_Abi)
HOOK(0x0048a210, "Material_BindTextures", Material_BindTextures_Entry, nullptr);
ABI_ENTRY_0048a290(MeshGroup_Draw_Entry, MeshGroup_Draw_Abi)
HOOK(0x0048a290, "MeshGroup_Draw", MeshGroup_Draw_Entry, nullptr);
ABI_ENTRY_0048a4b0(LightSlot_Acquire_Entry, LightSlot_Acquire_Abi)
HOOK(0x0048a4b0, "LightSlot_Acquire", LightSlot_Acquire_Entry, nullptr);
ABI_ENTRY_0048a520(LightSlot_Release_Entry, LightSlot_Release_Abi)
HOOK(0x0048a520, "LightSlot_Release", LightSlot_Release_Entry, nullptr);
ABI_ENTRY_0048a540(Object_DrawGroups_Entry, Object_DrawGroups_Abi)
HOOK(0x0048a540, "Object_DrawGroups", Object_DrawGroups_Entry, nullptr);
ABI_ENTRY_0048eb20(fn_0048eb20_Entry, fn_0048eb20_Abi)
HOOK(0x0048eb20, "fn_0048eb20", fn_0048eb20_Entry, nullptr);
ABI_ENTRY_0048eb60(World_WalkCells_Entry, World_WalkCells_Abi)
HOOK(0x0048eb60, "World_WalkCells", World_WalkCells_Entry, nullptr);
ABI_ENTRY_00491670(fn_00491670_Entry, fn_00491670_Abi)
HOOK(0x00491670, "fn_00491670", fn_00491670_Entry, nullptr);
ABI_ENTRY_004916d0(fn_004916d0_Entry, fn_004916d0_Abi)
HOOK(0x004916d0, "fn_004916d0", fn_004916d0_Entry, nullptr);
ABI_ENTRY_004927f0(fn_004927f0_Entry, fn_004927f0_Abi)
HOOK(0x004927f0, "fn_004927f0", fn_004927f0_Entry, nullptr);
ABI_ENTRY_00493440(fn_00493440_Entry, fn_00493440_Abi)
HOOK(0x00493440, "fn_00493440", fn_00493440_Entry, nullptr);
ABI_ENTRY_004937a0(fn_004937a0_Entry, fn_004937a0_Abi)
HOOK(0x004937a0, "fn_004937a0", fn_004937a0_Entry, nullptr);
ABI_ENTRY_004938e0(fn_004938e0_Entry, fn_004938e0_Abi)
HOOK(0x004938e0, "fn_004938e0", fn_004938e0_Entry, nullptr);
ABI_ENTRY_00494b70(WantedReward_Draw_Entry, WantedReward_Draw_Abi)
HOOK(0x00494b70, "WantedReward_Draw", WantedReward_Draw_Entry, nullptr);
ABI_ENTRY_00494d40(fn_00494d40_Entry, fn_00494d40_Abi)
HOOK(0x00494d40, "fn_00494d40", fn_00494d40_Entry, nullptr);
ABI_ENTRY_00494d90(Streamers_Construct_Entry, Streamers_Construct_Abi)
HOOK(0x00494d90, "Streamers_Construct", Streamers_Construct_Entry, nullptr);
ABI_ENTRY_00494de0(Streamers_Destruct_Entry, Streamers_Destruct_Abi)
HOOK(0x00494de0, "Streamers_Destruct", Streamers_Destruct_Entry, nullptr);
ABI_ENTRY_00494df0(Streamers_Clear_Entry, Streamers_Clear_Abi)
HOOK(0x00494df0, "Streamers_Clear", Streamers_Clear_Entry, nullptr);
ABI_ENTRY_00494e30(Streamers_AllocBlock_Entry, Streamers_AllocBlock_Abi)
HOOK(0x00494e30, "Streamers_AllocBlock", Streamers_AllocBlock_Entry, nullptr);
ABI_ENTRY_00494ee0(Streamers_Spawn_Entry, Streamers_Spawn_Abi)
HOOK(0x00494ee0, "Streamers_Spawn", Streamers_Spawn_Entry, nullptr);
ABI_ENTRY_00494f10(Streamer_Stop_Entry, Streamer_Stop_Abi)
HOOK(0x00494f10, "Streamer_Stop", Streamer_Stop_Entry, nullptr);
ABI_ENTRY_00494f20(Streamers_Free_Entry, Streamers_Free_Abi)
HOOK(0x00494f20, "Streamers_Free", Streamers_Free_Entry, nullptr);
ABI_ENTRY_00494f30(Streamers_Update_Entry, Streamers_Update_Abi)
HOOK(0x00494f30, "Streamers_Update", Streamers_Update_Entry, nullptr);
ABI_ENTRY_00494f80(Streamers_Draw_Entry, Streamers_Draw_Abi)
HOOK(0x00494f80, "Streamers_Draw", Streamers_Draw_Entry, nullptr);
ABI_ENTRY_00494fc0(StreamerList_Link_Entry, StreamerList_Link_Abi)
HOOK(0x00494fc0, "StreamerList_Link", StreamerList_Link_Entry, nullptr);
ABI_ENTRY_00495000(fn_00495000_Entry, fn_00495000_Abi)
HOOK(0x00495000, "fn_00495000", fn_00495000_Entry, nullptr);
ABI_ENTRY_00495040(fn_00495040_Entry, fn_00495040_Abi)
HOOK(0x00495040, "fn_00495040", fn_00495040_Entry, nullptr);
ABI_ENTRY_00495050(Streamer_SetLifetime_Entry, Streamer_SetLifetime_Abi)
HOOK(0x00495050, "Streamer_SetLifetime", Streamer_SetLifetime_Entry, nullptr);
ABI_ENTRY_00495060(Streamer_SetWidthCurve_Entry, Streamer_SetWidthCurve_Abi)
HOOK(0x00495060, "Streamer_SetWidthCurve", Streamer_SetWidthCurve_Entry, nullptr);
ABI_ENTRY_00495080(Streamer_SetAlpha_Entry, Streamer_SetAlpha_Abi)
HOOK(0x00495080, "Streamer_SetAlpha", Streamer_SetAlpha_Entry, nullptr);
ABI_ENTRY_00495100(Streamer_SetRed_Entry, Streamer_SetRed_Abi)
HOOK(0x00495100, "Streamer_SetRed", Streamer_SetRed_Entry, nullptr);
ABI_ENTRY_00495180(Streamer_SetGreen_Entry, Streamer_SetGreen_Abi)
HOOK(0x00495180, "Streamer_SetGreen", Streamer_SetGreen_Entry, nullptr);
ABI_ENTRY_00495200(Streamer_SetBlue_Entry, Streamer_SetBlue_Abi)
HOOK(0x00495200, "Streamer_SetBlue", Streamer_SetBlue_Entry, nullptr);
ABI_ENTRY_00495270(Streamer_SetU_Entry, Streamer_SetU_Abi)
HOOK(0x00495270, "Streamer_SetU", Streamer_SetU_Entry, nullptr);
ABI_ENTRY_00495290(Streamer_SetV_Entry, Streamer_SetV_Abi)
HOOK(0x00495290, "Streamer_SetV", Streamer_SetV_Entry, nullptr);
ABI_ENTRY_004952b0(Streamer_SetTexture_Entry, Streamer_SetTexture_Abi)
HOOK(0x004952b0, "Streamer_SetTexture", Streamer_SetTexture_Entry, nullptr);
ABI_ENTRY_004952d0(Streamer_SetBlend_Entry, Streamer_SetBlend_Abi)
HOOK(0x004952d0, "Streamer_SetBlend", Streamer_SetBlend_Entry, nullptr);
ABI_ENTRY_004952f0(fn_004952f0_Entry, fn_004952f0_Abi)
HOOK(0x004952f0, "fn_004952f0", fn_004952f0_Entry, nullptr);
ABI_ENTRY_00495300(Streamer_Update_Entry, Streamer_Update_Abi)
HOOK(0x00495300, "Streamer_Update", Streamer_Update_Entry, nullptr);
ABI_ENTRY_00495380(Streamer_Draw_Entry, Streamer_Draw_Abi)
HOOK(0x00495380, "Streamer_Draw", Streamer_Draw_Entry, nullptr);
ABI_ENTRY_004958a0(Streamer_AddPoint_Entry, Streamer_AddPoint_Abi)
HOOK(0x004958a0, "Streamer_AddPoint", Streamer_AddPoint_Entry, nullptr);
ABI_ENTRY_004959b0(Streamer_SetDirection_Entry, Streamer_SetDirection_Abi)
HOOK(0x004959b0, "Streamer_SetDirection", Streamer_SetDirection_Entry, nullptr);
ABI_ENTRY_00495cb0(fn_00495cb0_Entry, fn_00495cb0_Abi)
HOOK(0x00495cb0, "fn_00495cb0", fn_00495cb0_Entry, nullptr);
ABI_ENTRY_00495e80(Text_Draw_Entry, Text_Draw_Abi)
HOOK(0x00495e80, "Text_Draw", Text_Draw_Entry, nullptr);
ABI_ENTRY_00495fa0(fn_00495fa0_Entry, fn_00495fa0_Abi)
HOOK(0x00495fa0, "fn_00495fa0", fn_00495fa0_Entry, nullptr);
ABI_ENTRY_004e40e0(App_QueryRendererCaps_Entry, App_QueryRendererCaps_Abi)
HOOK(0x004e40e0, "App_QueryRendererCaps", App_QueryRendererCaps_Entry, nullptr);
ABI_ENTRY_004e4180(App_CreateBatch_Entry, App_CreateBatch_Abi)
HOOK(0x004e4180, "App_CreateBatch", App_CreateBatch_Entry, nullptr);
ABI_ENTRY_004ed8a0(Skin_Deform_Entry, Skin_Deform_Abi)
HOOK(0x004ed8a0, "Skin_Deform", Skin_Deform_Entry, nullptr);
ABI_ENTRY_004edb80(Skin_DeformAll_Entry, Skin_DeformAll_Abi)
HOOK(0x004edb80, "Skin_DeformAll", Skin_DeformAll_Entry, nullptr);
ABI_ENTRY_004edbe0(Skin_BuildPalette_Entry, Skin_BuildPalette_Abi)
HOOK(0x004edbe0, "Skin_BuildPalette", Skin_BuildPalette_Entry, nullptr);
ABI_ENTRY_004edcb0(Skin_FaceNormals_Entry, Skin_FaceNormals_Abi)
HOOK(0x004edcb0, "Skin_FaceNormals", Skin_FaceNormals_Entry, nullptr);
ABI_ENTRY_004ede80(Skin_VertexNormals_Entry, Skin_VertexNormals_Abi)
HOOK(0x004ede80, "Skin_VertexNormals", Skin_VertexNormals_Entry, nullptr);
ABI_ENTRY_004f01c0(Projection2D_Set_Entry, Projection2D_Set_Abi)
HOOK(0x004f01c0, "Projection2D_Set", Projection2D_Set_Entry, nullptr);
ABI_ENTRY_004f02f0(World_ToScreen_Entry, World_ToScreen_Abi)
HOOK(0x004f02f0, "World_ToScreen", World_ToScreen_Entry, nullptr);
ABI_ENTRY_004f0b00(Colour_ParseHex_Entry, Colour_ParseHex_Abi)
HOOK(0x004f0b00, "Colour_ParseHex", Colour_ParseHex_Entry, nullptr);
