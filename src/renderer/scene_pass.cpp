// Recovered: the game's 3D scene pass and its view frustum. Static recovery 2026-10-05, compile-checked only (the
// game was not run); the oracle (tools/compare.py) is still to do.
//   0x004889a0 Frustum_FromViewProjection
//   0x00489070 ScenePass_Render
#include "scene_pass.h"

#include <cmath>
#include <cstring>

#include "game.h"
#include "game/game_object.h"
#include "game/shadows.h"
#include "gen/entry_stubs.h"
#include "gen/orig_calls.h"
#include "hooks.h"

float g_sceneCullRadius = 0.0f;

namespace
{
template <class T>
uint32_t u(T* p)
{
    return reinterpret_cast<uint32_t>(p);
}
uint32_t f2u(float f)
{
    uint32_t v;
    std::memcpy(&v, &f, 4);
    return v;
}
}  // namespace

// 0x004889a0: planes from the rows of the game's 0x44-byte matrix (rows are output coordinates, clip = M p,
// docs/subsystems/shadows.md s4): planes 0..3 = row3 + row0, row3 - row0, row3 + row1, row3 - row1 (the copied
// row is negated for odd planes, then row3 added), plane 4 = row3 + row2 * [0x005bcb5c], plane 5 (only when cl is
// set) = row2. Each plane is then scaled by [0x005bcb2c] (1.0) / sqrt((x*x + y*y) + z*z), all four components.
// A point is inside when n.p + d >= 0. Returns the count (EAX = the loop bound, stored by the caller at +0x170).
uint32_t Frustum_FromViewProjection(bool withFar, float* planes, const float* m)
{
    const uint32_t count = (withFar ? 1u : 0u) + 5u;
    for (uint32_t i = 0; i < count; ++i)
    {
        float* p = planes + i * 4;
        if (i < 4)
        {
            const float* row = m + ((i >> 1) & 1) * 4;
            p[0] = row[0], p[1] = row[1], p[2] = row[2], p[3] = row[3];
            if (i & 1)
                p[0] = -p[0], p[1] = -p[1], p[2] = -p[2], p[3] = -p[3];
            p[0] = m[12] + p[0];
            p[1] = m[13] + p[1];
            p[2] = m[14] + p[2];
            p[3] = m[15] + p[3];
        }
        else if (i == 4)
        {
            const float k = GameGlobal<float>(0x005bcb5c);
            p[0] = m[8] * k, p[1] = m[9] * k, p[2] = m[10] * k, p[3] = m[11] * k;
            p[0] = m[12] + p[0];
            p[1] = m[13] + p[1];
            p[2] = m[14] + p[2];
            p[3] = m[15] + p[3];
        }
        else
        {
            p[0] = m[8], p[1] = m[9], p[2] = m[10], p[3] = m[11];
        }
        const float s = GameGlobal<float>(0x005bcb2c) / sqrtf((p[0] * p[0] + p[1] * p[1]) + p[2] * p[2]);
        p[0] = s * p[0];
        p[1] = s * p[1];
        p[2] = s * p[2];
        p[3] = s * p[3];
    }
    return count;
}

// 0x00489070 (stdcall, ret 0x10: pass, flags, arg2, arg3). With flags != 0:
//  - the renderer's view (virtual +0x64) is copied to pass +0x174 and its projection (virtual +0x60) to a local;
//    view-projection = 0x004e2910 (eax = pass +0x174; stack: out, projection); the frustum (0x004889a0, no far
//    plane) goes to pass +0x110 and its plane count to +0x170.
//  - 0x10 sky: projection from the camera (Game+0xa68: +0xf4 and +0xf8, read by 0x0040f840 / 0x0040f850) with far
//    1e6 (0x004f0100, esi = renderer), set (virtual +0x4c), every object of the lists pass +0x1bc and +0x1c0 drawn
//    (its virtual +0xc0), then the scene projection restored.
//  - 1 | 8 world: 0x0048eab0 (eax = plane count, ecx = planes, ebx = result buffer; stack: Game+0xaa0) gathers the
//    world against the frustum, 0x0048eb60 (Game+0xaa0, buffer, 0x00448db0 when flag 1, 0x00489050 when flag 8,
//    0) walks the result.
//  - 1 | 8 | 0x10: 0x00488e90(pass, 0) and 0x0048a1a0; 2: decals 0x00416330 (edi = Game+0xa7c); 1 | 8 | 0x10 again
//    with 1; 4: the game's shadows (0x004163c0 with arg3, 0x004162b0); 0x20: fx 0x00423820 (edi = Game+0xa90;
//    arg2) and 0x00494f80 (edx = Game+0xa94); 0x40: post-process 0x0047c600 (esi = Game+0xa9c; Game+0xa98, arg2);
//    0x80: 0x00406840(Game+0xa6c) and 0x0046a820(Game+0xaac).
//  - The list iterator's node (second dword) loses a reference (+0x18) at the end; it is also the return value.
uint32_t ScenePass_Render(void* pass, uint32_t flags, uint32_t arg2, uint32_t arg3)
{
    uint32_t iterator[2] = {0, 0};
    if (flags == 0)
        return 0;
    // Locals sized from the original frame: view-projection and projection 0x44 bytes each, the gather result
    // 0x608 bytes (esp+0xa8 .. the end of the 0x6a4-byte frame plus the three saved registers).
    alignas(8) uint32_t viewProjection[17];
    alignas(8) uint32_t projection[17];
    alignas(8) uint8_t gathered[0x608];

    void* renderer = Field<void*>(g_game, 0x38);
    std::memcpy(reinterpret_cast<uint8_t*>(pass) + 0x174, VCall<const void*>(renderer, 0x64 / 4), 0x44);
    std::memcpy(projection, VCall<const void*>(Field<void*>(g_game, 0x38), 0x60 / 4), 0x44);
    orig::fn_004e2910(u(reinterpret_cast<uint8_t*>(pass) + 0x174), u(viewProjection), u(projection));
    float* planes = reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(pass) + 0x110);
    Field<uint32_t>(pass, 0x170) =
        Frustum_FromViewProjection(false, planes, reinterpret_cast<const float*>(viewProjection));
    // Enhancement point: no culling within g_sceneCullRadius of the camera in the main pass. The camera lies on
    // the four side planes (the frustum's apex) and just behind the near plane, so moving those five planes out by
    // R takes in every point within R of it.
    if (flags == 0xffffffffu && g_sceneCullRadius > 0.0f)
        for (int i = 0; i < 5; ++i)
            planes[i * 4 + 3] += g_sceneCullRadius;

    if (flags & 0x10)
    {
        void* r = Field<void*>(g_game, 0x38);
        void* camera = Field<void*>(g_game, 0xa68);
        float a2 = Field<float>(camera, 0xf8);  // 0x0040f850
        float a1 = Field<float>(camera, 0xf4);  // 0x0040f840
        orig::fn_004f0100(u(r), u(viewProjection), f2u(a1), f2u(a2), 0x49742400u /* 1e6 */, 0);
        VCall<void>(r, 0x4c / 4, static_cast<void*>(viewProjection));
        // Both lists share one iterator, as the original (one reference release at the end).
        for (void* obj = reinterpret_cast<void*>(orig::fn_004e7340(Field<uint32_t>(pass, 0x1bc), u(iterator)));
             obj; obj = reinterpret_cast<void*>(orig::fn_004e73c0(u(iterator))))
            VCall<void>(obj, 0xc0 / 4);
        for (void* obj = reinterpret_cast<void*>(orig::fn_004e7340(Field<uint32_t>(pass, 0x1c0), u(iterator)));
             obj; obj = reinterpret_cast<void*>(orig::fn_004e73c0(u(iterator))))
            VCall<void>(obj, 0xc0 / 4);
        VCall<void>(Field<void*>(g_game, 0x38), 0x4c / 4, static_cast<void*>(projection));
    }
    if (flags & 9)
    {
        World_GatherFrustum(Field<uint32_t>(pass, 0x170), planes, reinterpret_cast<uint32_t*>(gathered),
                            Field<uint32_t>(g_game, 0xaa0));
        orig::fn_0048eb60(Field<uint32_t>(g_game, 0xaa0), u(gathered), (flags & 1) ? 0x00448db0u : 0u,
                          (flags & 8) ? 0x00489050u : 0u, 0);
    }
    const uint32_t layers = flags & 0x19;
    if (layers)
    {
        ScenePass_DrawLayers(pass, false);
        ScenePass_ResetStages();
    }
    if (flags & 2)
        orig::fn_00416330(Field<uint32_t>(g_game, 0xa7c));
    if (layers)
    {
        ScenePass_DrawLayers(pass, true);
        ScenePass_ResetStages();
    }
    if (flags & 4)
    {
        Shadows_Update(Field<void*>(g_game, 0xa7c), arg3);
        Shadows_Draw(Field<void*>(g_game, 0xa7c));
    }
    if (flags & 0x20)
    {
        orig::fn_00423820(Field<uint32_t>(g_game, 0xa90), arg2);
        orig::fn_00494f80(Field<uint32_t>(g_game, 0xa94));
    }
    if (flags & 0x40)
        orig::fn_0047c600(Field<uint32_t>(g_game, 0xa9c), Field<uint32_t>(g_game, 0xa98), arg2);
    if (flags & 0x80)
    {
        orig::fn_00406840(Field<uint32_t>(g_game, 0xa6c));
        orig::fn_0046a820(Field<uint32_t>(g_game, 0xaac));
    }
    if (iterator[1])
        --Field<int32_t>(reinterpret_cast<void*>(iterator[1]), 0x18);
    return iterator[1];
}

// 0x0048eab0 (eax = plane count, ecx = planes, ebx = result; stack: world, callee pops 4): the world gather. The
// result's first dword is cleared; 0x00449230 (eax = a 400-byte polygon workspace; stack: Game+0xa60 (the
// terrain grid), planes, count; callee pops 12) clips the terrain grid's footprint against the planes and returns
// the clipped polygon (dword 0 = vertex count, dword 1 = ?, vertices from +8). With at least a triangle,
// 0x004e7ed0 (ecx = vertices; stack: 128-byte edge workspace, count) prepares its edges and 0x004e7970 (eax = 0,
// ecx = edge workspace, edx = result; stack: polygon dword 1, count, 0x005cb190) collects what the polygon
// covers. The original caller pops those five stack arguments at once (add esp, 0x14): 0x004e7970 takes three,
// not the 16 symbols/abi.csv lists. `world` (stack) is not read here. Returns 0x004e7970's EAX, or the polygon.
uint32_t World_GatherFrustum(uint32_t count, const float* planes, uint32_t* result, uint32_t /*world*/)
{
    alignas(8) uint8_t edges[0x80];
    alignas(8) uint8_t polygon[0x190];
    *result = 0;
    uint32_t* poly = reinterpret_cast<uint32_t*>(
        orig::fn_00449230(u(polygon), Field<uint32_t>(g_game, 0xa60), u(planes), count));
    const uint32_t n = poly[0];
    if (static_cast<int32_t>(n) < 3)
        return u(poly);
    orig::fn_004e7ed0(u(poly + 2), u(edges), n);
    return orig::fn_004e7970(0, u(edges), u(result), poly[1], n, 0x005cb190u, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
}

// 0x00488e90 (stdcall, ret 8: pass, translucent): draws the render layers. The pass holds layer objects at
// +0x1c4[0 .. count] with count = [pass+0x20]; index count + 1 is the sorted (translucent) layer. Layer +0x10 is
// its bucket list (node: +0 next, +0xc item). Bracketed by the batch drawer Game+0x44 (virtual +0x18 begin,
// returning the batch, and +0x1c end).
//  - Opaque (translucent = 0), layers 0..count: for each bucket item: world matrix (renderer virtual +0x54) when
//    it changes (item +0), then 0x0048a290 (ebx = batch; stack: pass, item +4, +8, +0xc, +0x14, draw pass index;
//    callee pops 24) draws it. A nonzero AL means it needs another pass (the loop repeats over the remaining
//    buckets with the pass index + 1, starting from the first such node); otherwise the item goes back to the
//    free list (pass +0x1b8, +8) and the node is emptied. Then the layer's virtual +4.
//  - Translucent: only layer count + 1: 0x00488df0 (ecx = pass; stack: batch, &current matrix, layer +0x10)
//    walks the sorted tree; the layer's list is cleared before and after.
uint32_t ScenePass_DrawLayers(void* pass, bool translucent)
{
    const uint32_t sorted = Field<uint32_t>(pass, 0x20) + 1;
    uint32_t first = translucent ? sorted : 0;
    const uint32_t last = sorted;
    uint32_t batch = VCall<uint32_t>(Field<void*>(g_game, 0x44), 0x18 / 4);
    uint32_t matrix = 0;
    void** layers = nullptr;
    for (uint32_t i = first; i <= last; ++i)
    {
        layers = Field<void**>(pass, 0x1c4);
        if (i == Field<uint32_t>(pass, 0x20) + 1)
        {
            if (!translucent)
                continue;
            orig::fn_00488df0(u(pass), batch, u(&matrix), Field<uint32_t>(layers[i], 0x10));
            Field<uint32_t>(Field<void**>(pass, 0x1c4)[i], 0x10) = 0;
        }
        else
        {
            if (translucent)
                continue;
            void* node = Field<void*>(layers[i], 0x10);
            uint32_t drawPass = 0;
            while (node)
            {
                void* again = nullptr;
                for (void* n = node; n; n = Field<void*>(n, 0))
                {
                    uint32_t* item = Field<uint32_t*>(n, 0xc);
                    if (!item)
                        continue;
                    if (item[0] != matrix)
                    {
                        matrix = item[0];
                        VCall<void>(Field<void*>(g_game, 0x38), 0x54 / 4, reinterpret_cast<void*>(item[0]));
                    }
                    if (static_cast<uint8_t>(
                            orig::fn_0048a290(batch, u(pass), item[1], item[2], item[3], item[5], drawPass)))
                    {
                        if (!again)
                            again = n;
                    }
                    else
                    {
                        void* freeList = Field<void*>(pass, 0x1b8);
                        item[0] = Field<uint32_t>(freeList, 8);
                        Field<uint32_t*>(freeList, 8) = item;
                        Field<uint32_t>(n, 0xc) = 0;
                    }
                }
                ++drawPass;
                node = again;
            }
        }
        if (i != Field<uint32_t>(pass, 0x20) + 1)
            VCall<void>(Field<void**>(pass, 0x1c4)[i], 4 / 4);
    }
    VCall<void>(Field<void*>(g_game, 0x44), 0x1c / 4);
    if (translucent)
        Field<uint32_t>(Field<void**>(pass, 0x1c4)[Field<uint32_t>(pass, 0x20) + 1], 0x10) = 0;
    return 0;
}

// 0x0048a1a0: renderer virtual +0xa4 (0, 0, 0, 0, stage) for stages 0..3 (the texture stages cleared after the
// layers are drawn; Shadows_Draw calls the same virtual for its blend set-up).
uint32_t ScenePass_ResetStages()
{
    uint32_t r = 0;
    for (uint32_t stage = 0; stage < 4; ++stage)
        r = VCall<uint32_t>(Field<void*>(g_game, 0x38), 0xa4 / 4, 0u, 0u, 0u, 0u, stage);
    return r;
}

static uint32_t ScenePass_DrawLayers_Abi(uint32_t pass, uint32_t translucent)
{
    return ScenePass_DrawLayers(reinterpret_cast<void*>(pass), (translucent & 0xff) != 0);
}
static uint32_t ScenePass_ResetStages_Abi()
{
    return ScenePass_ResetStages();
}

static uint32_t World_GatherFrustum_Abi(uint32_t eax, uint32_t ecx, uint32_t ebx, uint32_t world)
{
    return World_GatherFrustum(eax, reinterpret_cast<const float*>(ecx), reinterpret_cast<uint32_t*>(ebx), world);
}

static uint32_t Frustum_FromViewProjection_Abi(uint32_t ecx, uint32_t edx, uint32_t edi, uint32_t /*unused*/)
{
    return Frustum_FromViewProjection((ecx & 0xff) != 0, reinterpret_cast<float*>(edx),
                                      reinterpret_cast<const float*>(edi));
}
static uint32_t ScenePass_Render_Abi(uint32_t pass, uint32_t flags, uint32_t arg2, uint32_t arg3)
{
    return ScenePass_Render(reinterpret_cast<void*>(pass), flags, arg2, arg3);
}
ABI_ENTRY_004889a0(Frustum_FromViewProjection_Entry, Frustum_FromViewProjection_Abi)
ABI_ENTRY_00489070(ScenePass_Render_Entry, ScenePass_Render_Abi)
ABI_ENTRY_0048eab0(World_GatherFrustum_Entry, World_GatherFrustum_Abi)
HOOK(0x0048eab0, "World_GatherFrustum", World_GatherFrustum_Entry, nullptr);
ABI_ENTRY_00488e90(ScenePass_DrawLayers_Entry, ScenePass_DrawLayers_Abi)
ABI_ENTRY_0048a1a0(ScenePass_ResetStages_Entry, ScenePass_ResetStages_Abi)
HOOK(0x00488e90, "ScenePass_DrawLayers", ScenePass_DrawLayers_Entry, nullptr);
HOOK(0x0048a1a0, "ScenePass_ResetStages", ScenePass_ResetStages_Entry, nullptr);
HOOK(0x004889a0, "Frustum_FromViewProjection", Frustum_FromViewProjection_Entry, nullptr);
HOOK(0x00489070, "ScenePass_Render", ScenePass_Render_Entry, nullptr);
