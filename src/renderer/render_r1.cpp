// Recovered (batch r1, 0x00418190-0x0041a350): the game's shadow update / receiver mesh, the environment-map cube
// pass, a world-tracking marker draw, texture animation and the fx object update. Static recovery 2026-10-05,
// compile-checked only (the game was not run); the oracle (tools/compare.py) is still to do.
//   0x00418190 GameShadow_Update          0x004181e0 GameShadow_RebuildReceivers  0x00418380 GameShadow_Draw
//   0x00418510 Decal_MoveToList           0x00418610 fn_00418610                   0x00418640 fn_00418640
//   0x00418880 EnvMaps_Update             0x00418920 EnvMap_FindNearest            0x00418a10 EnvMaps_ReleaseAll
//   0x00418c30 EnvMap_Release             0x00418c80 EnvMap_Render                 0x00419020 fn_00419020
//   0x004190a0 fn_004190a0                0x00419290 fn_00419290                   0x004195e0 TexAnim_BuildStageMatrix
//   0x004196f0 TexAnim_SetStages          0x004198c0 TexAnim_FindFrameTexture      0x00419940 TexAnim_ResolveTextures
//   0x00419a60 fn_00419a60                0x00419d30 TexAnim_Step                  0x0041a050 fn_0041a050
//   0x0041a2d0 Fx_Lerp                    0x0041a2f0 fn_0041a2f0                   0x0041a310 fn_0041a310
//   0x0041a350 fn_0041a350
#include <cmath>
#include <cstring>

#include "game.h"
#include "game/game_object.h"
#include "gen/entry_stubs.h"
#include "gen/orig_calls.h"
#include "hooks.h"

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
float u2f(uint32_t v)
{
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}
void* p(uint32_t v)
{
    return reinterpret_cast<void*>(v);
}
uint8_t* b(void* v)
{
    return reinterpret_cast<uint8_t*>(v);
}
void* game()
{
    return g_game;
}
void* renderer()
{
    return Field<void*>(g_game, 0x38);
}
float k(uint32_t address)
{
    return GameGlobal<float>(address);
}

// 0x004e7880 (ecx = in, edx = out): normalises a 3-vector and returns its length in st0. symbols/abi.csv lists an
// EAX return; the original callers pop st0 (fstp st(0)), so this wrapper pops it too.
void Vec3_Normalize(uint32_t source, uint32_t dest)
{
    static const uint32_t target = 0x004e7880;
    __asm {
        push ebx
        push esi
        push edi
        push ebp
        mov ecx, source
        mov edx, dest
        call dword ptr [target]
        fstp st(0)
        pop ebp
        pop edi
        pop esi
        pop ebx
    }
}
}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// Game shadows (docs/subsystems/shadows.md). Shadow object: +0x58 caster, +0x118 receiver mesh dirty, +0x119
// texture valid, +0x11c/+0x120 light frustum count/planes, +0x12c.. light view-projection, +0x198/+0x19c near/far.
// ---------------------------------------------------------------------------------------------------------------

// 0x00418190 (ecx = passed through to 0x00416e90, esi = shadow; stack: arg, callee pops 4; caller 0x004163c0).
// Caster bit +0x7f & 0x80 clear: the texture is marked invalid (+0x119 = 0). When the texture is valid and the
// caster neither moved (0x00416e90, edi = caster) nor animates (0x004531e0, ecx = caster), nothing is done (EAX =
// 0x004531e0's result). Otherwise the light camera (0x00417c00, eax = shadow) and the silhouette render (0x00417f20,
// stack: shadow) run and the texture becomes valid. `arg` is not read.
uint32_t GameShadow_Update(uint32_t ecx, void* shadow, uint32_t /*arg*/)
{
    if (!(Field<uint8_t>(shadow, 0x7f) & 0x80))
        Field<uint8_t>(shadow, 0x119) = 0;
    if (Field<uint8_t>(shadow, 0x119))
    {
        if (!static_cast<uint8_t>(orig::fn_00416e90(ecx, Field<uint32_t>(shadow, 0x58))))
        {
            const uint32_t r = orig::fn_004531e0(Field<uint32_t>(shadow, 0x58));
            if (!static_cast<uint8_t>(r))
                return r;
        }
    }
    orig::fn_00417c00(u(shadow));
    const uint32_t r = orig::fn_00417f20(u(shadow));
    Field<uint8_t>(shadow, 0x119) = 1;
    return r;
}

// 0x004181e0 (esi = shadow): rebuilds the receiver mesh. Unless +0x118 (dirty) is set, it returns early when each of
// the six light matrix values +0x12c + i*0x10 is within [0x005bcb80] (2.0) of the copy kept at +0x180 + i*4. Then
// the copy is refreshed, the mesh counters (+0x1c, +0x20, +0x24, +0x2c..+0x38) cleared, the world gathered against
// the light frustum (0x0048eab0: eax = +0x11c, ecx = +0x120, ebx = 0x608-byte result; stack: Game+0xaa0) and walked
// by 0x0048eb60 (Game+0xaa0, result, 0x00449010, 0x00416f50 when caster +0x3ec bit 0x20 else 0, context) with an
// 8-dword context {shadow, 0x20000010, +0x11c, &+0x120, +0x108 eye (4 dwords)}. Finally every vertex's +0x1bc
// (stride 0x10) becomes 1 - v*v * (1 / (far - near)^2). Returns the vertex pointer past the end (0 when empty).
uint32_t GameShadow_RebuildReceivers(void* shadow)
{
    if (!Field<uint8_t>(shadow, 0x118))
    {
        uint32_t i = 0;
        for (; i < 6; ++i)
        {
            const float d = std::fabs(Field<float>(shadow, 0x12c + i * 0x10) - Field<float>(shadow, 0x180 + i * 4));
            if (d > k(0x005bcb80))
                break;
        }
        if (i == 6)
            return 0;
    }
    Field<uint8_t>(shadow, 0x118) = 0;
    Field<uint32_t>(shadow, 0x188) = Field<uint32_t>(shadow, 0x14c);
    Field<uint32_t>(shadow, 0x184) = Field<uint32_t>(shadow, 0x13c);
    Field<uint32_t>(shadow, 0x180) = Field<uint32_t>(shadow, 0x12c);
    Field<uint32_t>(shadow, 0x194) = Field<uint32_t>(shadow, 0x17c);
    Field<uint32_t>(shadow, 0x190) = Field<uint32_t>(shadow, 0x16c);
    Field<uint32_t>(shadow, 0x18c) = Field<uint32_t>(shadow, 0x15c);
    Field<uint32_t>(shadow, 0x24) = 0;
    Field<uint32_t>(shadow, 0x20) = 0;
    Field<uint32_t>(shadow, 0x1c) = 0;
    Field<uint32_t>(shadow, 0x2c) = 0;
    Field<uint32_t>(shadow, 0x30) = 0;
    Field<uint32_t>(shadow, 0x34) = 0;
    Field<uint32_t>(shadow, 0x38) = 0;

    // Original frame: a float temporary, the 8-dword context and the 0x608-byte gather result.
    alignas(8) uint32_t context[8];
    alignas(8) uint8_t gathered[0x608];
    context[4] = Field<uint32_t>(shadow, 0x108);
    context[5] = Field<uint32_t>(shadow, 0x10c);
    context[7] = Field<uint32_t>(shadow, 0x114);
    context[6] = Field<uint32_t>(shadow, 0x110);
    context[0] = u(shadow);
    context[1] = 0x20000010u;
    context[2] = Field<uint32_t>(shadow, 0x11c);
    context[3] = u(b(shadow) + 0x120);
    const uint32_t callback =
        (Field<uint8_t>(Field<void*>(shadow, 0x58), 0x3ec) & 0x20) ? 0x00416f50u : 0u;
    orig::fn_0048eab0(Field<uint32_t>(shadow, 0x11c), u(b(shadow) + 0x120), u(gathered),
                      Field<uint32_t>(game(), 0xaa0));
    orig::fn_0048eb60(Field<uint32_t>(game(), 0xaa0), u(gathered), 0x00449010u, callback, u(context));

    const float range = Field<float>(shadow, 0x19c) - Field<float>(shadow, 0x198);
    const float scale = k(0x005bcb2c) / (range * range);
    if (!Field<uint32_t>(shadow, 0x1c))
        return 0;
    float* v = &Field<float>(shadow, 0x1bc);
    uint32_t i = 0;
    do
    {
        ++i;
        const float x = *v;
        *v = k(0x005bcb2c) - x * x * scale;
        v += 4;
    } while (i < Field<uint32_t>(shadow, 0x1c));
    return u(v);
}

// 0x00418380 (ecx = shadow; stack: decal, flag; callee pops 8): the shadow's draw (vtable 0x005b96bc +0x20, called by
// 0x004162b0). Skipped when caster +0x424 bit 4 is set (EAX = the caster). The colour (decal +4) is:
//  - decal = 0 (then decal = shadow + 4): from the level shadow colour bytes Game+0x2704..0x2707 (b, g, r, a):
//    alpha = a/255 * caster +0x3f0; each of r, g, b becomes (1 - c/255) * alpha * 255 (_ftol), packed as
//    ((0xffffff00 | r) << 8 | g) << 8 | b (the full _ftol results are or-ed, as in the original);
//  - flag byte set: grey from a * caster +0x3f0 (_ftol, low byte) in all three channels, top byte 0xff;
//  - otherwise the dword Game+0x2704 as is.
// Then decal +8 = shadow +0x5c (the shadow texture), the receiver mesh is rebuilt (0x004181e0) and the decal drawn by
// 0x00416740 (ecx = shadow; stack: decal, flag).
uint32_t GameShadow_Draw(void* shadow, uint32_t decal, uint32_t flag)
{
    void* caster = Field<void*>(shadow, 0x58);
    if (Field<uint8_t>(caster, 0x424) & 4)
        return u(caster);
    void* g = game();
    if (decal == 0)
    {
        decal = u(b(shadow) + 4);
        const float alpha =
            static_cast<float>(static_cast<int32_t>(Field<uint8_t>(g, 0x2707))) * k(0x005bcc18) *
            Field<float>(caster, 0x3f0);
        const float r = (k(0x005bcb2c) - static_cast<float>(static_cast<int32_t>(Field<uint8_t>(g, 0x2706))) *
                                             k(0x005bcc18)) *
                        alpha * k(0x005bcb34);
        uint32_t c = static_cast<uint32_t>(static_cast<int32_t>(r));
        c = (c | 0xffffff00u) << 8;
        const float gr = (k(0x005bcb2c) - static_cast<float>(static_cast<int32_t>(Field<uint8_t>(g, 0x2705))) *
                                              k(0x005bcc18)) *
                         alpha * k(0x005bcb34);
        c = (c | static_cast<uint32_t>(static_cast<int32_t>(gr))) << 8;
        const float bl = (k(0x005bcb2c) - static_cast<float>(static_cast<int32_t>(Field<uint32_t>(g, 0x2704) & 0xff)) *
                                              k(0x005bcc18)) *
                         alpha * k(0x005bcb34);
        c |= static_cast<uint32_t>(static_cast<int32_t>(bl));
        Field<uint32_t>(p(decal), 4) = c;
    }
    else if (static_cast<uint8_t>(flag))
    {
        const float a = static_cast<float>(static_cast<int32_t>(Field<uint8_t>(g, 0x2707))) * Field<float>(caster, 0x3f0);
        const uint32_t v = static_cast<uint8_t>(static_cast<int32_t>(a));
        Field<uint32_t>(p(decal), 4) = (((v | 0xffffff00u) << 8 | v) << 8) | v;
    }
    else
    {
        Field<uint32_t>(p(decal), 4) = Field<uint32_t>(g, 0x2704);
    }
    Field<uint32_t>(p(decal), 8) = u(b(shadow) + 0x5c);
    GameShadow_RebuildReceivers(shadow);
    return orig::fn_00416740(u(shadow), decal, flag);
}

// 0x00418510 (ecx = decal, edx = list head or 0): unlinks the decal from its doubly linked list (+0x50 next, +0x54
// address of the link that points at it) and pushes it at the front of `head` (shadows.md: 0x00417b90 moves a shadow
// between the free and active lists of the manager, +0x10/+0x14). Returns &decal+0x50.
uint32_t Decal_MoveToList(void* decal, uint32_t* head)
{
    uint32_t* link = &Field<uint32_t>(decal, 0x50);
    if (*link)
        Field<uint32_t>(p(*link), 0x54) = Field<uint32_t>(decal, 0x54);
    if (Field<uint32_t>(decal, 0x54))
        *reinterpret_cast<uint32_t*>(Field<uint32_t>(decal, 0x54)) = *link;
    if (head)
    {
        *link = *head;
        Field<uint32_t*>(decal, 0x54) = head;
        *head = u(decal);
        if (*link)
            Field<uint32_t>(p(*link), 0x54) = u(link);
    }
    else
    {
        *link = 0;
        Field<uint32_t>(decal, 0x54) = 0;
    }
    return u(link);
}

// 0x00418610 (eax = object): walks the object and its parents (0x004539d0, eax = object) and returns AL = 1 as soon
// as one animates (0x004531e0, ecx = object; the test 0x00418190 uses for the caster), else 0. Caller: 0x00416f50
// (the receiver-gather callback for objects).
uint32_t fn_00418610(uint32_t object)
{
    while (object)
    {
        const uint32_t r = orig::fn_004531e0(object);
        if (static_cast<uint8_t>(r))
            return (r & 0xffffff00u) | 1;
        object = orig::fn_004539d0(object);
    }
    return 0;
}

// 0x00418640 (ecx = object; stack: value, callee pops 4; caller 0x00417180): when +0xf68 equals [0x0056c05c] (0.0)
// or value < +0xf64, both +0xf64 and +0xf68 become value and the decal manager's counter (Game+0xa7c +0x20) is
// incremented. EAX: the manager when updated (the other path leaves FPU status bits, not reproduced).
uint32_t fn_00418640(void* object, uint32_t valueBits)
{
    const float value = u2f(valueBits);
    if (Field<float>(object, 0xf68) == k(0x0056c05c) || value < Field<float>(object, 0xf64))
    {
        Field<uint32_t>(object, 0xf64) = valueBits;
        Field<uint32_t>(object, 0xf68) = valueBits;
        void* manager = Field<void*>(game(), 0xa7c);
        ++Field<int32_t>(manager, 0x20);
        return u(manager);
    }
    return 0;
}

// ---------------------------------------------------------------------------------------------------------------
// Environment maps: objects on the list Game+0xa84 +0xf0c (their init 0x00418a70 reads "EnvMapSize", "EnvMapBpp",
// "EnvMapFreq", "EnvMapNearClip" and names the texture "cubemap_texture" at +0x470). +0x460 texture created, +0x461,
// +0x464 near clip, +0x468 frequency, +0x494 / +0x498 face mask (0x3f = all six faces).
// ---------------------------------------------------------------------------------------------------------------

// 0x00418c80 (esi = env map; caller 0x00418880): creates the cube texture on first use (renderer vt+0x124 with
// +0x470; +0x460 = 1, faces +0x498 = 0x3f, +0x461 = 0, +0x46c = 0), takes the position (vt+0x54 into 12 bytes) and,
// with the nesting counter [0x0063a1c8] raised, renders the scene into the cube faces: 0x00489320 (Game+0xa78,
// position, +0x470, +0x464, +0x494, +0x498; callee pops 24). The face mask then becomes (mask & 0x1f) * 2; when that
// is 0, +0x461 and +0x46c are cleared. Returns the new mask.
uint32_t EnvMap_Render(void* envMap)
{
    if (!Field<uint8_t>(envMap, 0x460))
    {
        VCall<void>(renderer(), 0x124 / 4, static_cast<void*>(b(envMap) + 0x470));
        Field<uint8_t>(envMap, 0x460) = 1;
        Field<uint32_t>(envMap, 0x498) = 0x3f;
        Field<uint8_t>(envMap, 0x461) = 0;
        Field<uint32_t>(envMap, 0x46c) = 0;
    }
    alignas(4) uint32_t position[3];
    VCall<void>(envMap, 0x54 / 4, static_cast<void*>(position));
    const uint32_t faces = Field<uint32_t>(envMap, 0x498);
    const uint32_t a4 = Field<uint32_t>(envMap, 0x494);
    ++GameGlobal<int32_t>(0x0063a1c8);
    orig::fn_00489320(Field<uint32_t>(game(), 0xa78), u(position), u(b(envMap) + 0x470), Field<uint32_t>(envMap, 0x464),
                      a4, faces);
    --GameGlobal<int32_t>(0x0063a1c8);
    const uint32_t mask = (Field<uint32_t>(envMap, 0x498) & 0x1f) * 2;
    Field<uint32_t>(envMap, 0x498) = mask;
    if (mask == 0)
    {
        Field<uint8_t>(envMap, 0x461) = 0;
        Field<uint32_t>(envMap, 0x46c) = 0;
    }
    return mask;
}

// 0x00418880 (al = repeat; caller 0x00403050): with AL = 0, every env map's vt+0x3c runs (it selects the one to
// render into [0x0063a1cc], guess); the choice is remembered in [0x0063a1d0] with its face mask (+0x498) in
// [0x0063a1d4]. With AL != 0 the remembered one is selected again with that face mask restored. The selected map is
// rendered (0x00418c80) and [0x0063a1cc] cleared. Returns EAX as left by the path taken.
uint32_t EnvMaps_Update(uint32_t eax)
{
    void* envMap;
    if (!static_cast<uint8_t>(eax))
    {
        uint32_t iterator[2] = {0, 0};
        void* list = Field<void*>(Field<void*>(game(), 0xa84), 0xf0c);
        for (void* obj = p(orig::fn_004e7340(u(list), u(iterator))); obj; obj = p(orig::fn_004e73c0(u(iterator))))
            VCall<void>(obj, 0x3c / 4);
        envMap = GameGlobal<void*>(0x0063a1cc);
        GameGlobal<void*>(0x0063a1d0) = envMap;
        if (envMap)
            GameGlobal<uint32_t>(0x0063a1d4) = Field<uint32_t>(envMap, 0x498);
        eax = iterator[1];
        if (iterator[1])
            --Field<int32_t>(p(iterator[1]), 0x18);
    }
    else
    {
        envMap = GameGlobal<void*>(0x0063a1d0);
        GameGlobal<void*>(0x0063a1cc) = envMap;
        if (!envMap)
        {
            GameGlobal<void*>(0x0063a1cc) = nullptr;
            return eax;
        }
        Field<uint32_t>(envMap, 0x498) = GameGlobal<uint32_t>(0x0063a1d4);
    }
    if (envMap)
        eax = EnvMap_Render(envMap);
    GameGlobal<void*>(0x0063a1cc) = nullptr;
    return eax;
}

// 0x00418920 (ecx = object or 0; callers 0x00487e30, 0x00488b00): with no object, the first env map of the list.
// Otherwise the object's vt+0x90 (out 12 bytes, out dword, 1) runs, and for each env map its vt+0x54 vector v gives
// sqrt((v.z*v.z + v.y*v.y) + v.x*v.x); the map with the smallest value (first on ties) is returned. Note: the
// object's vt+0x90 output is not used in the distance (see report: possible original bug).
uint32_t EnvMap_FindNearest(void* object)
{
    uint32_t iterator[2] = {0, 0};
    void* list = Field<void*>(Field<void*>(game(), 0xa84), 0xf0c);
    if (!object)
    {
        const uint32_t first = orig::fn_004e7340(u(list), u(iterator));
        if (iterator[1])
            --Field<int32_t>(p(iterator[1]), 0x18);
        return first;
    }
    alignas(4) uint32_t scratch[1];
    alignas(4) uint32_t bounds[3];
    alignas(4) float v[3];
    VCall<void>(object, 0x90 / 4, static_cast<void*>(bounds), static_cast<void*>(scratch), 1u);
    list = Field<void*>(Field<void*>(game(), 0xa84), 0xf0c);
    void* best = nullptr;
    float bestDistance = 0.0f;
    for (void* m = p(orig::fn_004e7340(u(list), u(iterator))); m; m = p(orig::fn_004e73c0(u(iterator))))
    {
        VCall<void>(m, 0x54 / 4, static_cast<void*>(v));
        const float d = sqrtf((v[2] * v[2] + v[1] * v[1]) + v[0] * v[0]);
        if (!best || d < bestDistance)
        {
            bestDistance = d;
            best = m;
        }
    }
    if (iterator[1])
        --Field<int32_t>(p(iterator[1]), 0x18);
    return u(best);
}

// 0x00418c30 (esi = env map; callers 0x00418a10, 0x00418c00): when the cube texture exists (+0x460), releases it
// (renderer vt+0x128 (+0x470, 1)), clears +0x460 and, if +0x461 was 0, sets +0x461 = 1 and faces +0x498 = 1.
// Returns AL = +0x461 as read (upper bytes from the renderer call), or 0.
uint32_t EnvMap_Release(void* envMap)
{
    if (!Field<uint8_t>(envMap, 0x460))
        return 0;
    const uint32_t r = VCall<uint32_t>(renderer(), 0x128 / 4, static_cast<void*>(b(envMap) + 0x470), 1u);
    const uint8_t was = Field<uint8_t>(envMap, 0x461);
    Field<uint8_t>(envMap, 0x460) = 0;
    if (!was)
    {
        Field<uint8_t>(envMap, 0x461) = 1;
        Field<uint32_t>(envMap, 0x498) = 1;
    }
    return (r & 0xffffff00u) | was;
}

// 0x00418a10 (callers 0x00401070, 0x004010c0): releases the cube texture of every env map (0x00418c30). Returns the
// iterator node.
uint32_t EnvMaps_ReleaseAll()
{
    uint32_t iterator[2] = {0, 0};
    void* list = Field<void*>(Field<void*>(game(), 0xa84), 0xf0c);
    for (void* m = p(orig::fn_004e7340(u(list), u(iterator))); m; m = p(orig::fn_004e73c0(u(iterator))))
        EnvMap_Release(m);
    if (iterator[1])
        --Field<int32_t>(p(iterator[1]), 0x18);
    return iterator[1];
}

// ---------------------------------------------------------------------------------------------------------------
// Markers / fx objects
// ---------------------------------------------------------------------------------------------------------------

// 0x00419020 (ecx = object; stack: desc, callee pops 4; virtual, executed): 0x00457810 (eax = object), registers the
// object on the list Game+0xa84 +0xf1c (0x004546d0, esi = list, edi = object), sets desc +0xd and +0xc to 1, calls
// 0x004527d0 (ecx = object; stack: desc) and then desc +0 = 0x4000. Returns 0x004527d0's EAX.
uint32_t fn_00419020(void* object, void* desc)
{
    orig::fn_00457810(u(object));
    orig::fn_004546d0(Field<uint32_t>(Field<void*>(game(), 0xa84), 0xf1c), u(object));
    Field<uint8_t>(desc, 0xd) = 1;
    Field<uint8_t>(desc, 0xc) = 1;
    const uint32_t r = orig::fn_004527d0(u(object), u(desc));
    Field<uint32_t>(desc, 0) = 0x4000;
    return r;
}

// 0x004190a0 (edi = object; caller 0x00464460): draws the object's model directly. 0x004543f0 (eax 0, ecx object,
// edx 0; stack 0, 1, 0), model = 0x00453120 (eax = object, edx = +0x374; stack: [0x00408800(4, object)]). The
// object's matrix (vt+0x70) has its translation (m[3], m[7], m[11]) scaled by [0x005bcbf8] (1/640), [0x005bcbf4]
// (1/480), [0x005bcbf4]; a scale matrix from (+0x37c * 1/640, +0x380 * 1/480, +0x384 * 1/480) (0x004e2d90) times it
// (0x004e2910) becomes the world matrix (renderer vt+0x54). Then, inside the batch (Game+0x44 vt+0x18 / vt+0x1c),
// every mesh of the model (+0x14 count, +0x18 array; mesh +4 -> +0x80 part count, +0x84 parts of 0x2c bytes) is
// drawn by 0x0048a290 (ebx = batch; Game+0xa78, mesh, part, +0x410, object, -1). Screen-space scale: guess (a HUD /
// world-tracking marker). Returns the batch end's EAX. The tracker's size (416) stops 15 bytes short: the function
// ends at 0x0041924e.
uint32_t fn_004190a0(void* object)
{
    // Original frame (0xe8 bytes): scale vector, the object's matrix, the scale matrix, the world matrix.
    alignas(8) float scale[3];
    alignas(8) float matrix[17];
    alignas(8) uint32_t scaleMatrix[17];
    alignas(8) uint32_t world[17];
    orig::fn_004543f0(0, u(object), 0, 0, 1, 0);
    const uint32_t* entry = reinterpret_cast<const uint32_t*>(orig::fn_00408800(4, u(object)));
    void* model = p(orig::fn_00453120(u(object), Field<uint32_t>(object, 0x374), *entry));
    VCall<void>(object, 0x70 / 4, static_cast<void*>(matrix));
    scale[0] = Field<float>(object, 0x37c) * k(0x005bcbf8);
    scale[1] = Field<float>(object, 0x380) * k(0x005bcbf4);
    scale[2] = Field<float>(object, 0x384) * k(0x005bcbf4);
    matrix[3] = matrix[3] * k(0x005bcbf8);
    matrix[7] = matrix[7] * k(0x005bcbf4);
    matrix[11] = matrix[11] * k(0x005bcbf4);
    orig::fn_004e2d90(u(scaleMatrix), u(scale), 0, 0, 0);  // reads no stack arguments (abi.csv lists 3)
    orig::fn_004e2910(u(scaleMatrix), u(world), u(matrix));
    VCall<void>(renderer(), 0x54 / 4, static_cast<void*>(world));
    const uint32_t batch = VCall<uint32_t>(Field<void*>(game(), 0x44), 0x18 / 4);
    if (Field<int32_t>(model, 0x14) > 0)
    {
        uint32_t* mesh = &Field<uint32_t>(model, 0x18);
        int32_t i = 0;
        do
        {
            void* data = Field<void*>(p(*mesh), 4);
            if (Field<int32_t>(data, 0x80) > 0)
            {
                int32_t j = 0;
                uint32_t offset = 0;
                do
                {
                    orig::fn_0048a290(batch, Field<uint32_t>(game(), 0xa78), *mesh,
                                      Field<uint32_t>(data, 0x84) + offset, Field<uint32_t>(object, 0x410), u(object),
                                      0xffffffffu);
                    ++j;
                    offset += 0x2c;
                } while (j < Field<int32_t>(data, 0x80));
            }
            ++i;
            ++mesh;
        } while (i < Field<int32_t>(model, 0x14));
    }
    return VCall<uint32_t>(Field<void*>(game(), 0x44), 0x1c / 4);
}

// 0x00419290 (al = spawn, esi = fx object; callers 0x0041a350, 0x0041cfb0): fx object end. With AL set and +0x460
// bit 0, an effect is spawned through the fx manager Game+0xa90 by 0x0041cc60 (al = 0; stack: 0, desc) from a
// 0x80-byte descriptor {0x38, or 0x238 when +0x464 bit 0x100; 0; +0x51c; position (vt+0x54) at +0xc}. Then, with
// +0x460 bit 0x10000000, +0x464 |= 0x80 (EAX as left); otherwise the object's vt+0xdc runs (its EAX returned).
// The descriptor's other bytes are uninitialised in the original; zero here.
uint32_t fn_00419290(uint32_t eax, void* fx)
{
    if (static_cast<uint8_t>(eax) && (Field<uint8_t>(fx, 0x460) & 1))
    {
        alignas(8) uint32_t desc[0x80 / 4] = {};
        desc[1] = 0;
        VCall<void>(fx, 0x54 / 4, static_cast<void*>(&desc[3]));
        const uint32_t flags = Field<uint32_t>(fx, 0x464);
        desc[2] = Field<uint32_t>(fx, 0x51c);
        desc[0] = (flags & 0x100) ? 0x238u : 0x38u;
        eax = orig::fn_0041cc60(0, Field<uint32_t>(game(), 0xa90), 0, u(desc));
    }
    if (Field<uint32_t>(fx, 0x460) & 0x10000000u)
    {
        Field<uint32_t>(fx, 0x464) |= 0x80;
        return eax;
    }
    return VCall<uint32_t>(fx, 0xdc / 4);
}

// ---------------------------------------------------------------------------------------------------------------
// Texture animation (strings "Bogus shader in Texture Animation (stage N)"). Animation data at fx +0x514: +0 time;
// per stage s: scroll block +s*0x20 (+4/+8 speed, +0xc scale, +0x10 fade-in end, +0x14 fade-out length, +0x18 end,
// +0x1c/+0x20 offsets), +s*0x28 path block (+0x84 mode, +0x88/+0x8c start, +0x90/+0x94 end, +0x98 duration, +0x9c/
// +0xa0 rates, +0xa4/+0xa8 current scale), +s*0x14 rotation block (+0x124 speed, +0x128 fade-in end, +0x12c fade-out
// length, +0x130 end, +0x134 angle in degrees). fx +0x518: frame table (+0x20 + s*4 frame counts).
// ---------------------------------------------------------------------------------------------------------------

// 0x004195e0 (eax = stage; stack: fx, out matrix; callee pops 8; caller 0x004196f0): the stage's texture matrix:
// rotation by angle * [0x005bcd60] (1/180) * [0x005bcb70] (pi) (m[0] = cos, m[1] = -sin, m[4] = sin, m[5] = cos,
// m[10] = m[15] = 1, flags 1) times the scale matrix of (+0xa4, +0xa8, 1) (0x004e2d90 / 0x004e2910 into out), then
// out m[3] = fmod(scroll x + m[3], 64), m[7] = fmod(scroll y + m[7], 64) (0x0056712a = _CIfmod, [0x005bcd58] = 64.0).
uint32_t TexAnim_BuildStageMatrix(uint32_t stage, void* fx, float* out)
{
    // Original frame: angle, scale vector (3), rotation matrix (17 dwords), scale matrix (17 dwords).
    alignas(8) uint32_t vec[3];
    alignas(8) uint32_t rotation[17];
    alignas(8) uint32_t scale[17];
    uint8_t* anim = Field<uint8_t*>(fx, 0x514);
    const float angle = *reinterpret_cast<float*>(anim + stage * 0x14 + 0x134) * k(0x005bcd60) * k(0x005bcb70);
    std::memset(rotation, 0, sizeof(rotation));
    const float s = static_cast<float>(std::sin(static_cast<double>(angle)));
    vec[0] = *reinterpret_cast<uint32_t*>(anim + stage * 0x28 + 0xa4);
    vec[1] = *reinterpret_cast<uint32_t*>(anim + stage * 0x28 + 0xa8);
    rotation[10] = 0x3f800000u;
    rotation[15] = 0x3f800000u;
    rotation[16] = 1;
    vec[2] = 0x3f800000u;
    const float c = static_cast<float>(std::cos(static_cast<double>(angle)));
    rotation[0] = f2u(c);
    rotation[1] = f2u(-s);
    rotation[4] = f2u(s);
    rotation[5] = f2u(c);
    orig::fn_004e2d90(u(scale), u(vec), 0, 0, 0);  // reads no stack arguments (abi.csv lists 3)
    orig::fn_004e2910(u(rotation), u(out), u(scale));
    const double wrap = GameGlobal<double>(0x005bcd58);
    anim = Field<uint8_t*>(fx, 0x514);
    const float x = *reinterpret_cast<float*>(anim + stage * 0x20 + 0x1c) + out[3];
    out[3] = static_cast<float>(std::fmod(static_cast<double>(x), wrap));
    anim = Field<uint8_t*>(fx, 0x514);
    const float y = *reinterpret_cast<float*>(anim + stage * 0x20 + 0x20) + out[7];
    out[7] = static_cast<float>(std::fmod(static_cast<double>(y), wrap));
    return 0;
}

// 0x004196f0 (ecx = fx object; stack: material, callee pops 4; virtual, executed): for stages 0..3, when +0x460 bit
// 0x100000 << s is set: texture matrix (0x004195e0) set by renderer vt+0x58 (matrix, s) and texture stage set-up
// renderer vt+0xa4 (1, 1, texture, 0, s); otherwise vt+0xa4 (0, 0, texture, 0, s). Textures: [material +8] +0x44,
// +0x48, +0x4c, +0x50 for stages 0..3. Returns the last vt+0xa4 EAX.
uint32_t TexAnim_SetStages(void* fx, void* material)
{
    alignas(8) float matrix[17];
    uint32_t r = 0;
    for (uint32_t s = 0; s < 4; ++s)
    {
        const bool on = (Field<uint32_t>(fx, 0x460) & (0x100000u << s)) != 0;
        if (on)
        {
            TexAnim_BuildStageMatrix(s, fx, matrix);
            VCall<void>(renderer(), 0x58 / 4, static_cast<void*>(matrix), s);
        }
        const uint32_t texture = Field<uint32_t>(Field<void*>(material, 8), 0x44 + s * 4);
        const uint32_t flag = on ? 1u : 0u;
        r = VCall<uint32_t>(renderer(), 0xa4 / 4, flag, flag, texture, 0u, s);
    }
    return r;
}

// 0x004198c0 (eax = fx object, edx = stage; stack: name, callee pops 4; caller 0x00419940): when the stage's frame
// count ([+0x518] +0x20 + stage*4) is nonzero, formats "%s_%d" (0x005b9600) with the name and the count into a
// 32-byte buffer (sprintf 0x0053814f) and looks the texture up by name (0x004881c0, ecx = name, esi = Game+0xa78).
// Returns the texture or 0.
uint32_t TexAnim_FindFrameTexture(void* fx, uint32_t stage, uint32_t name)
{
    const uint32_t count = Field<uint32_t>(Field<void*>(fx, 0x518), 0x20 + stage * 4);
    if (count == 0)
        return 0;
    char buffer[0x20];
    GameFunction<int(__cdecl*)(char*, const char*, ...)>(0x0053814f)(
        buffer, reinterpret_cast<const char*>(0x005b9600), name, count);
    return orig::fn_004881c0(u(buffer), Field<uint32_t>(game(), 0xa78), 0, 0, 0, 0, 0, 0, 0, 0);
}

// 0x00419940 (ecx = fx object; stack: material, callee pops 4; virtual, executed): with a frame table (+0x518), for
// each stage s whose bit (1 << s) is set in +0x464: the stage texture (material +8 + s*4) is replaced by its frame
// texture (0x004198c0 with the texture's name, its first dword) when found; with no texture the warning
// "Bogus shader in Texture Animation (stage s)" is logged (0x00407ff0 (Game+0x34, 10, text)). EAX as left.
uint32_t TexAnim_ResolveTextures(void* fx, void* material)
{
    static const uint32_t messages[4] = {0x005b95d4, 0x005b95a8, 0x005b957c, 0x005b9550};
    uint32_t eax = Field<uint32_t>(fx, 0x518);
    if (!eax)
        return 0;
    for (uint32_t s = 0; s < 4; ++s)
    {
        if (!(Field<uint8_t>(fx, 0x464) & (1u << s)))
            continue;
        uint32_t& slot = Field<uint32_t>(material, 8 + s * 4);
        eax = slot;
        if (slot)
        {
            eax = TexAnim_FindFrameTexture(fx, s, Field<uint32_t>(p(slot), 0));
            if (eax)
                slot = eax;
        }
        else
        {
            eax = orig::fn_00407ff0(Field<uint32_t>(game(), 0x34), 0xa, messages[s], 0);
        }
    }
    return eax;
}

// 0x00419a60 (edi = fx object; caller 0x0041a350): camera-facing orientation. Returns AL = 1 when one of these
// modes applied (upper bytes from 0x00457990), else AL = 0 (upper bytes of +0x460):
//  - +0x460 bit 0x400: matrix = 0x004e2210 (eax = out, ebx = the camera's matrix from 0x0040fa50 (eax = out; stack:
//    camera Game+0xa68)) with the translation replaced by the object's position (vt+0x6c); 0x00457990 (al = 0;
//    stack: object, matrix) sets it;
//  - bit 0x4000000: v = normalise(-cam.x, -cam.y, 0) (camera vector 0x0040fa10, esi = out; stack: camera;
//    normalise 0x004e7880), c = 0x004e7830 (eax = (0, 0, 1), ecx = v, edx = out); columns (-c, (0,0,1), v) and the
//    position as translation;
//  - bit 0x2000000: v = normalise(0, -cam.y, -cam.z), c from (1, 0, 0); columns (-c, (1,0,0), v) and the position.
uint32_t fn_00419a60(void* fx)
{
    // Original frame (0xb8 bytes): v (+4), c (+0x10), up/position (+0x1c / +0x28), matrix (+0x34), camera (+0x78).
    alignas(8) float v[3];
    alignas(8) float c[3];
    alignas(8) float axis[3];
    alignas(8) float pos[3];
    alignas(8) uint32_t m[17];
    alignas(8) uint32_t camera[17];
    const uint32_t flags = Field<uint32_t>(fx, 0x460);
    if (flags & 0x400)
    {
        orig::fn_0040fa50(u(camera), Field<uint32_t>(game(), 0xa68));
        orig::fn_004e2210(u(m), u(camera));
        VCall<void>(fx, 0x6c / 4, static_cast<void*>(pos));
        m[3] = f2u(pos[0]);
        m[11] = f2u(pos[2]);
        m[7] = f2u(pos[1]);
        const uint32_t r = orig::fn_00457990(0, u(fx), u(m));
        return (r & 0xffffff00u) | 1;
    }
    if (flags & 0x4000000)
    {
        VCall<void>(fx, 0x6c / 4, static_cast<void*>(pos));
        orig::fn_0040fa10(u(v), Field<uint32_t>(game(), 0xa68));
        v[0] = -v[0];
        v[2] = 0.0f;
        v[1] = -v[1];
        Vec3_Normalize(u(v), u(v));
        axis[0] = 0.0f;
        axis[1] = 0.0f;
        axis[2] = 1.0f;
        orig::fn_004e7830(u(axis), u(v), u(c));
        m[0] = f2u(-c[0]);
        m[2] = f2u(v[0]);
        m[4] = f2u(-c[1]);
        m[3] = f2u(pos[0]);
        m[8] = f2u(-c[2]);
        m[6] = f2u(v[1]);
        m[10] = f2u(v[2]);
        m[1] = 0;
        m[5] = 0;
        m[9] = 0x3f800000u;
        m[7] = f2u(pos[1]);
        m[11] = f2u(pos[2]);
        m[12] = 0;
        m[13] = 0;
        m[14] = 0;
        m[15] = 0x3f800000u;
        m[16] = 0x11;
        const uint32_t r = orig::fn_00457990(0, u(fx), u(m));
        return (r & 0xffffff00u) | 1;
    }
    if (flags & 0x2000000)
    {
        VCall<void>(fx, 0x6c / 4, static_cast<void*>(pos));
        orig::fn_0040fa10(u(v), Field<uint32_t>(game(), 0xa68));
        v[1] = -v[1];
        v[0] = 0.0f;
        v[2] = -v[2];
        Vec3_Normalize(u(v), u(v));
        axis[0] = 1.0f;
        axis[1] = 0.0f;
        axis[2] = 0.0f;
        orig::fn_004e7830(u(axis), u(v), u(c));
        m[0] = f2u(-c[0]);
        m[2] = f2u(v[0]);
        m[4] = f2u(-c[1]);
        m[3] = f2u(pos[0]);
        m[8] = f2u(-c[2]);
        m[6] = f2u(v[1]);
        m[10] = f2u(v[2]);
        m[1] = 0x3f800000u;
        m[5] = 0;
        m[9] = 0;
        m[7] = f2u(pos[1]);
        m[11] = f2u(pos[2]);
        m[12] = 0;
        m[13] = 0;
        m[14] = 0;
        m[15] = 0x3f800000u;
        m[16] = 0x11;
        const uint32_t r = orig::fn_00457990(0, u(fx), u(m));
        return (r & 0xffffff00u) | 1;
    }
    return flags & 0xffffff00u;
}

namespace
{
// The fade factor 0x00419d30 computes twice (inline in the original): 0 after `end`, ramping up until `in`, ramping
// down over the last `out` before `end`. The ramp down divides by the ramp's start (end - out), not by `out`: see the
// report (possible original bug).
float TexAnim_Fade(float t, float in, float out, float end)
{
    if (end > 0.0f && t > end)
        return 0.0f;
    if (in > 0.0f && t < in)
        return t / in;
    if (out > 0.0f)
    {
        const float start = end - out;
        if (start < t)
            return k(0x005bcb2c) - (t - start) / start;
    }
    return k(0x005bcb2c);
}
}  // namespace

// 0x00419d30 (eax = fx object, edi = stage; caller 0x0041a350): advances the stage's texture animation by the frame
// time dt = [0x00604a90]: scroll offsets += speed * scale * fade * dt; the path scale (+0xa4/+0xa8) is the end value
// once t >= duration, else by mode: 1 linear start + (end - start) / duration * t, 2 start + (rate / duration * t)^2
// per axis, 3 eased (s = 1 - t / duration; start + s*p + p with p = (end - start) / duration * t); the rotation angle
// += speed * fade * dt. Comparisons follow the original's x87 tests (NaN never takes a branch). Returns 0 (the
// original leaves FPU status bits in EAX).
uint32_t TexAnim_Step(void* fx, uint32_t stage)
{
    const float dt = GameGlobal<float>(0x00604a90);
    const float zero = k(0x0056c05c);
    const float one = k(0x005bcb2c);
    {
        float* anim = Field<float*>(fx, 0x514);
        float* sc = reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(anim) + stage * 0x20);
        float f;
        const float t = anim[0];
        if (sc[6] > zero && t > sc[6])
            f = zero;
        else if (sc[4] > zero && t < sc[4])
            f = t / sc[4];
        else if (sc[5] > zero && (sc[6] - sc[5]) < t)
            f = one - (t - (sc[6] - sc[5])) / (sc[6] - sc[5]);
        else
            f = one;
        sc[7] = sc[3] * sc[1] * f * dt + sc[7];
        anim = Field<float*>(fx, 0x514);
        sc = reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(anim) + stage * 0x20);
        sc[8] = sc[3] * sc[2] * f * dt + sc[8];
    }
    {
        uint8_t* anim = Field<uint8_t*>(fx, 0x514);
        float* g = reinterpret_cast<float*>(anim + stage * 0x28);
        const float t = *reinterpret_cast<float*>(anim);
        // g[0x21] = +0x84 mode, g[0x22..0x2a] = +0x88..+0xa8
        if (t >= g[0x98 / 4])  // x87: unordered takes the interpolation branch
        {
            g[0xa4 / 4] = g[0x90 / 4];
            anim = Field<uint8_t*>(fx, 0x514);
            g = reinterpret_cast<float*>(anim + stage * 0x28);
            g[0xa8 / 4] = g[0x94 / 4];
        }
        else
        {
            const uint32_t mode = *reinterpret_cast<uint32_t*>(&g[0x84 / 4]);
            if (mode == 1)
            {
                const float r = one / g[0x98 / 4];
                const float dy = (g[0x94 / 4] - g[0x8c / 4]) * r;
                g[0xa4 / 4] = (g[0x90 / 4] - g[0x88 / 4]) * r * t + g[0x88 / 4];
                anim = Field<uint8_t*>(fx, 0x514);
                g = reinterpret_cast<float*>(anim + stage * 0x28);
                g[0xa8 / 4] = dy * *reinterpret_cast<float*>(anim) + g[0x8c / 4];
            }
            else if (mode == 2)
            {
                const float r = one / g[0x98 / 4];
                const float a = r * g[0x9c / 4] * t;
                const float bb = r * g[0xa0 / 4] * t;
                g[0xa4 / 4] = a * a + g[0x88 / 4];
                anim = Field<uint8_t*>(fx, 0x514);
                g = reinterpret_cast<float*>(anim + stage * 0x28);
                g[0xa8 / 4] = bb * bb + g[0x8c / 4];
            }
            else if (mode == 3)
            {
                const float r = one / g[0x98 / 4];
                const float pp = (g[0x90 / 4] - g[0x88 / 4]) * r * t;
                const float q = (g[0x94 / 4] - g[0x8c / 4]) * r * t;
                const float s = one - r * t;
                g[0xa4 / 4] = s * pp + g[0x88 / 4] + pp;
                anim = Field<uint8_t*>(fx, 0x514);
                g = reinterpret_cast<float*>(anim + stage * 0x28);
                g[0xa8 / 4] = s * q + g[0x8c / 4] + q;
            }
        }
    }
    {
        uint8_t* anim = Field<uint8_t*>(fx, 0x514);
        float* h = reinterpret_cast<float*>(anim + stage * 0x14);
        const float t = *reinterpret_cast<float*>(anim);
        const float f = TexAnim_Fade(t, h[0x128 / 4], h[0x12c / 4], h[0x130 / 4]);
        h[0x134 / 4] = f * h[0x124 / 4] * dt + h[0x134 / 4];
    }
    return 0;
}

// 0x0041a050 (eax = fx object; stack: emitter, callee pops 4; callers 0x0041a350, 0x0041e990): one emitter's spawn
// step. Emitter: +0 flags, +4, +8, +0xc, +0x10 life, +0x74 previous position, +0x80 previous direction, +0x8c local
// offset, +0xd0/+0xdc, +0xe8 bone. Skipped once flag 0x10 (expired). Life -= dt ([0x00604a90]); at <= 0 the flag
// 0x10 is set. Then by mode:
//  - 0x20: direction = +0x80 (flag 0x200) or from the object's matrix (0x00453150, ecx = object, esi = out): its
//    offset +0x8c transformed (0x004e36a0, flag 0x400) or its first row; position = vt+0x54;
//  - 0x40: bone matrix 0x00453c50 (object, +0xe8, out, 1, 0, 0, 1, 0, 1): position = its translation; direction =
//    +0x80 (0x200) or the negated second column, transformed by 0x004e36a0 when 0x400 (left unset otherwise);
//    both spawn through 0x0041f4b0 (ecx = 0x004193a0 callback, ebx = &position, esi = &direction, edi = +0x80;
//    stack: Game+0xa90, +4, &+0x74, &+0xc, +8, dt, emitter);
//  - otherwise 0x0041f470 (ecx = 0x004193a0, ebx = +0x80, edi = &direction (+0x80 copy); stack: Game+0xa90, +4,
//    &+0xd0, &+0xdc, &+0xc, +8, dt, emitter).
// Finally +0x80 = direction (unless 0x200) and +0x74 = position. The 0x68-byte frame is kept as in the original
// (positions the original leaves unset are copied as found, see the report).
uint32_t fn_0041a050(void* fx, void* emitter)
{
    alignas(8) uint8_t f[0x68];  // the original frame: direction +0, position +0xc, matrix/vector +0x18 / +0x24
    float* direction = reinterpret_cast<float*>(f);
    float* position = reinterpret_cast<float*>(f + 0xc);
    uint32_t flags = Field<uint32_t>(emitter, 0);
    if (flags & 0x10)
        return u(fx);
    const float dt = GameGlobal<float>(0x00604a90);
    if (Field<float>(emitter, 0x10) > k(0x0056c05c))
    {
        const float life = Field<float>(emitter, 0x10) - dt;
        Field<float>(emitter, 0x10) = life;
        if (life <= k(0x0056c05c))
            Field<uint32_t>(emitter, 0) = flags | 0x10;
    }
    flags = Field<uint32_t>(emitter, 0);
    void* g = game();
    if (flags & 0x20)
    {
        if (flags & 0x200)
        {
            std::memcpy(direction, b(emitter) + 0x80, 12);
        }
        else
        {
            orig::fn_00453150(u(fx), u(f + 0x18));
            if (Field<uint32_t>(emitter, 0) & 0x400)
                orig::fn_004e36a0(u(f + 0x18), u(b(emitter) + 0x8c), 1, u(direction));
            else
                std::memcpy(direction, f + 0x18, 12);
        }
        VCall<void>(fx, 0x54 / 4, static_cast<void*>(position));
        g = game();
        orig::fn_0041f4b0(0x004193a0u, u(position), u(direction), u(b(emitter) + 0x80), Field<uint32_t>(g, 0xa90),
                          Field<uint32_t>(emitter, 4), u(b(emitter) + 0x74), u(b(emitter) + 0xc),
                          Field<uint32_t>(emitter, 8), f2u(dt), u(emitter));
    }
    else if (flags & 0x40)
    {
        orig::fn_00453c50(u(fx), Field<uint32_t>(emitter, 0xe8), u(f + 0x24), 1, 0, 0, 1, 0, 1);
        const uint32_t* m = reinterpret_cast<const uint32_t*>(f + 0x24);
        reinterpret_cast<uint32_t*>(f)[4] = m[7];
        reinterpret_cast<uint32_t*>(f)[3] = m[3];
        reinterpret_cast<uint32_t*>(f)[5] = m[11];
        if (Field<uint32_t>(emitter, 0) & 0x200)
        {
            std::memcpy(direction, b(emitter) + 0x80, 12);
        }
        else
        {
            const uint32_t fl = Field<uint32_t>(emitter, 0);
            float* column = reinterpret_cast<float*>(f + 0x18);
            const float* mf = reinterpret_cast<const float*>(f + 0x24);
            column[0] = -mf[1];
            column[1] = -mf[5];
            column[2] = -mf[9];
            if (fl & 0x400)
                orig::fn_004e36a0(u(f + 0x18), u(b(emitter) + 0x8c), 1, u(direction));
        }
        g = game();
        orig::fn_0041f4b0(0x004193a0u, u(position), u(direction), u(b(emitter) + 0x80), Field<uint32_t>(g, 0xa90),
                          Field<uint32_t>(emitter, 4), u(b(emitter) + 0x74), u(b(emitter) + 0xc),
                          Field<uint32_t>(emitter, 8), f2u(dt), u(emitter));
    }
    else
    {
        std::memcpy(direction, b(emitter) + 0x80, 12);
        orig::fn_0041f470(0x004193a0u, u(b(emitter) + 0x80), u(direction), Field<uint32_t>(g, 0xa90),
                          Field<uint32_t>(emitter, 4), u(b(emitter) + 0xd0), u(b(emitter) + 0xdc),
                          u(b(emitter) + 0xc), Field<uint32_t>(emitter, 8), f2u(dt), u(emitter));
    }
    if (!(Field<uint32_t>(emitter, 0) & 0x200))
        std::memcpy(b(emitter) + 0x80, direction, 12);
    std::memcpy(b(emitter) + 0x74, position, 12);
    return reinterpret_cast<uint32_t*>(f)[4];
}

// 0x0041a2d0 (stack: unused, start, end, t, duration; callee pops 20; returns st0): linear interpolation
// ((end - start) / duration) * t + start.
float Fx_Lerp(uint32_t /*unused*/, float start, float end, float t, float duration)
{
    return (end - start) / duration * t + start;
}

// 0x0041a2f0 (stack: unused, start, t, duration, rate; callee pops 20; returns st0): p = rate / duration * t,
// returns p * p + start (callers pass fx +0x480 / +0x470 / +0x46c / +0x488 and the like).
float fn_0041a2f0(uint32_t /*unused*/, float start, float t, float duration, float rate)
{
    const float q = rate / duration * t;
    return q * q + start;
}

// 0x0041a310 (stack: unused, start, end, t, duration; callee pops 20): eased interpolation r = 1 / duration,
// p = (end - start) * r * t, s = 1 - r * t, returns (s * p + p) + start. Returns st0: symbols/abi.csv lists no
// return, but the code leaves the result in st0 (fxch; fstp st(0) drops p) and 0x0041a350 stores it.
float fn_0041a310(uint32_t /*unused*/, float start, float end, float t, float duration)
{
    const float r = k(0x005bcb2c) / duration;
    const float pp = (end - start) * r * t;
    const float s = k(0x005bcb2c) - r * t;
    return s * pp + pp + start;
}

// 0x0041a350 (ecx = fx object; symbols/abi.csv lists 16 caller-cleaned stack arguments, but the function reads none:
// it realigns its frame (and esp, -8) and reuses EBP at once; virtual, executed): the fx object's per-frame update.
// The 0x680-byte frame is mirrored as `fr`, indexed from the original ESP after the four saved registers (so frame
// offsets below match the disassembly with esp tracked). In order:
//  - +0x464 bit 0x80 (finished): nothing (EAX = +0x464).
//  - bit 0x2000 (first update, cleared): with +0x460 bit 0x2000, a repeat count +0x52c and time ([0x00604a98] -
//    +0x524) past +0x528, a spawn (0x0041cc60, descriptor 0x818 / 0xa18 at fr+0x170: +8 = +0x520, +0xc = +0x544..,
//    +0x78 = [+0x554]+0x10, +0x7c = +0x550), +0x528 = now, count - 1; bit 0x1000: 0x0047f020 (ebx = +0x530; fx,
//    position, fx, +0x540, fx, 0, 0) and the bit cleared; then +0x460 bit 0x1000 ends the object (0x00419290 al 0)
//    and bit 0x800 ends it with al = 1.
//  - dt = [0x00604a90] + +0x468 (cleared) at fr+0x38; +0x460 bit 2: age +0x470 += dt, ending (0x00419290 al 1)
//    once past the lifetime +0x46c; bit 4: fade-in/out via +0x474/+0x478 into vt+0x100 (alpha, guess), bit 0x10:
//    fade-out after +0x47c.
//  - size over life (+0x460 bit 0x20 linear 0x0041a2d0, 0x40 0x0041a2f0, 0x80 eased 0x0041a310 from +0x480 to
//    +0x484 / rate +0x488), applied as a uniform scale (0x004535d0).
//  - attached light +0x5d0 (renderer vt+0xb4 reads it into fr+0x280; colour fr+0x284.. over life (+0x460 bits
//    0x20000000 / 0x40000000 / 0x80000000); re-set by 0x0048a4b0).
//  - colour and two values over life (+0x464 bits 0x10 / 0x20 / 0x40; fields +0x5f8..+0x630) passed to renderer
//    vt+0x114 (value(+0x61c..), value(+0x628..), &colour).
//  - orientation: 0x00419a60, else (+0x460 bit 0x40000) a matrix from 0x00458000 / 0x004e2e30(pi/2) / 0x004e36a0 /
//    0x004e3070 set by 0x00457990. +0x464 bit 0x800: 0x00455e70(0, 0).
//  - +0x460 bit 0x100: up to 10 sub-emitters at [+0x4ec] (stride 0x98; +0 flags, bit 8 = active, +0x24 effect,
//    +0x28 interval, +0x2c timer, +0x30 / +0x34 bones, +0x38 timeout, +0x3c matrix, +0x48/+0x58/+0x68 position,
//    +0x80.. random box) spawn while their timer is at most -interval, repeating the pass (dt = 0) until no timer is
//    still negative: mode 2 bone-interpolated matrices (0x0041cba0, 0x00453c50, 0x004e35a0, 0x004e3510, 0x004e3370,
//    optional random offset with _rand * 1/32768 (+0x460 bit 0x4000), spawn 0x0041cc60 type 0x32 / 0x232); 0x80
//    positions between the emitter and the object (type 0x38 / 0x238); 1 trails (0x0041dc80, when the timer is
//    negative; the timer is reset to the interval); 0x40 bone positions (type 0x38 / 0x238). Then each emitter's
//    position is updated (object position when it has no bone, else the last matrix / position computed).
//  - +0x460 bit 0x8000000: up to 8 attachments [+0x4f4..] placed at their bone (0x004958a0); bit 0x200: up to 10
//    emitters [+0x4f0] (stride 0x194) stepped by 0x0041a050; [+0x634]: 0x0041bd30; bit 0x8000: camera shake
//    0x0040fac0 when the camera (0x0040f9e0) is within +0x4dc, then the bit is cleared; bit 0x80000: sound volume
//    0x004e51f0 (Game+0x30, +0x4e0, v) with v = 1 inside +0x4e4 (compared with the squared distance), falling to 0
//    at +0x4e8; texture animation time and steps (0x00419d30) for stages with bits 0x100000 << s; texture frame
//    counters (+0x518: rate +s*4, accumulator +0x30 + s*4, frame +0x20 + s*4, count +0x10 + s*4, wrap bits +0x40);
//    bit 0x1000000: light attached to bone +0x5c8 (0x0048a520 releases the old one, 0x0048a4b0 adds it, +0x5cc).
// EAX at the end is not reproduced (the two 0x00419290 tail calls return its EAX).
uint32_t fn_0041a350(void* fx)
{
    alignas(8) uint8_t fr[0x690];
    auto D = [&](uint32_t o) -> uint32_t& { return *reinterpret_cast<uint32_t*>(fr + o); };
    auto Fl = [&](uint32_t o) -> float& { return *reinterpret_cast<float*>(fr + o); };
    auto A = [&](uint32_t o) -> uint32_t { return u(fr + o); };
    auto X = [&](uint32_t o) -> uint32_t& { return Field<uint32_t>(fx, o); };
    auto XF = [&](uint32_t o) -> float& { return Field<float>(fx, o); };
    const float zero = k(0x0056c05c);
    const float one = k(0x005bcb2c);
    auto rnd = GameFunction<int(__cdecl*)()>(0x005391cd);

    const uint32_t flags464 = X(0x464);
    if (flags464 & 0x80)
        return flags464;
    if (flags464 & 0x2000)
    {
        X(0x464) = flags464 & ~0x2000u;
        if ((X(0x460) & 0x2000) && X(0x52c) != 0 &&
            GameGlobal<float>(0x00604a98) - XF(0x524) > XF(0x528))
        {
            D(0x184) = X(0x54c);
            D(0x17c) = X(0x544);
            D(0x180) = X(0x548);
            D(0x178) = X(0x520);
            D(0x170) = 0x818;
            void* e = Field<void*>(fx, 0x554);
            D(0x1e8) = e ? Field<uint32_t>(e, 0x10) : 0;
            D(0x1ec) = X(0x550);
            if (X(0x464) & 0x100)
                D(0x170) = 0xa18;
            orig::fn_0041cc60(0, Field<uint32_t>(game(), 0xa90), 0, A(0x170));
            X(0x528) = GameGlobal<uint32_t>(0x00604a98);
            const int32_t n = static_cast<int32_t>(X(0x52c));
            if (n > 0)
                X(0x52c) = static_cast<uint32_t>(n - 1);
        }
        if (X(0x464) & 0x1000)
        {
            VCall<void>(fx, 0x54 / 4, reinterpret_cast<void*>(A(0x18)));
            orig::fn_0047f020(u(fx) + 0x530, u(fx), A(0x18), u(fx), X(0x540), u(fx), 0, 0);
            X(0x464) &= ~0x1000u;
        }
        const uint32_t f460 = X(0x460);
        if (f460 & 0x1000)
            return fn_00419290(f460 & 0xffffff00u, fx);
        if (f460 & 0x800)
            return fn_00419290((f460 & 0xffffff00u) | 1, fx);
    }

    Fl(0x38) = GameGlobal<float>(0x00604a90) + XF(0x468);
    X(0x468) = 0;
    const uint32_t cl = X(0x460);
    if (cl & 2)
    {
        const float age = Fl(0x38) + XF(0x470);
        XF(0x470) = age;
        if (age >= XF(0x46c))
            return fn_00419290(1, fx);
    }
    if (cl & 4)
    {
        if (cl & 8)
        {
            const float v = XF(0x474) - Fl(0x38);
            XF(0x474) = v;
            if (v <= zero)
            {
                X(0x460) &= ~8u;
                XF(0x474) = Fl(0x38) - XF(0x474);
            }
        }
        if (!(Field<uint8_t>(fx, 0x460) & 8))
        {
            const float v = Fl(0x38) + XF(0x474);
            XF(0x474) = v;
            Fl(0x34) = v * XF(0x478);
            if (Fl(0x34) >= one)
            {
                X(0x460) &= ~4u;
                D(0x34) = 0x3f7fbe77u;
            }
            VCall<void>(fx, 0x100 / 4, Fl(0x34));
        }
    }
    else if (cl & 0x10)
    {
        if (XF(0x470) > XF(0x47c))
        {
            const float r = one / (XF(0x46c) - XF(0x47c));
            VCall<void>(fx, 0x100 / 4, one - r * (XF(0x470) - XF(0x47c)));
        }
        else
        {
            VCall<void>(fx, 0x100 / 4, u2f(0x3f7fbe77u));
        }
    }

    // Size over life.
    {
        const uint32_t f = X(0x460);
        bool set = true;
        float s = 0.0f;
        if (f & 0x20)
            s = Fx_Lerp(u(fx), XF(0x480), XF(0x484), XF(0x470), XF(0x46c));
        else if (f & 0x40)
            s = fn_0041a2f0(u(fx), XF(0x480), XF(0x470), XF(0x46c), XF(0x488));
        else if (f & 0x80)
            s = fn_0041a310(u(fx), XF(0x480), XF(0x484), XF(0x470), XF(0x46c));
        else
            set = false;
        if (set)
        {
            Fl(0x18) = s;
            Fl(0x1c) = s;
            Fl(0x20) = s;
            orig::fn_004535d0(A(0x18), u(fx));
        }
    }

    // Attached light colour over life.
    if (X(0x5d0) != 0xffffffffu)
    {
        VCall<void>(renderer(), 0xb4 / 4, X(0x5d0), reinterpret_cast<void*>(A(0x280)));
        const uint32_t f = X(0x460);
        if (f & 0x20000000)
        {
            Fl(0x284) = Fx_Lerp(u(fx), XF(0x5d4), XF(0x5e0), XF(0x470), XF(0x46c));
            Fl(0x288) = Fx_Lerp(u(fx), XF(0x5d8), XF(0x5e4), XF(0x470), XF(0x46c));
            Fl(0x28c) = Fx_Lerp(u(fx), XF(0x5dc), XF(0x5e8), XF(0x470), XF(0x46c));
        }
        else if (f & 0x40000000)
        {
            Fl(0x284) = fn_0041a2f0(u(fx), XF(0x5d4), XF(0x470), XF(0x46c), XF(0x5ec));
            Fl(0x288) = fn_0041a2f0(u(fx), XF(0x5d8), XF(0x470), XF(0x46c), XF(0x5f0));
            Fl(0x28c) = fn_0041a2f0(u(fx), XF(0x5dc), XF(0x470), XF(0x46c), XF(0x5f4));
        }
        else if (f & 0x80000000)
        {
            Fl(0x284) = fn_0041a310(u(fx), XF(0x5d4), XF(0x5e0), XF(0x470), XF(0x46c));
            Fl(0x288) = fn_0041a310(u(fx), XF(0x5d8), XF(0x5e4), XF(0x470), XF(0x46c));
            Fl(0x28c) = fn_0041a310(u(fx), XF(0x5dc), XF(0x5e8), XF(0x470), XF(0x46c));
        }
        X(0x5d0) = orig::fn_0048a4b0(X(0x5d0), Field<uint32_t>(game(), 0xa78), A(0x280));
    }

    // Colour and two values over life, to renderer vt+0x114.
    {
        const uint32_t f = X(0x464);
        if (f & 0x10)
        {
            Fl(0x18) = Fx_Lerp(u(fx), XF(0x5f8), XF(0x604), XF(0x470), XF(0x46c));
            Fl(0x1c) = Fx_Lerp(u(fx), XF(0x5fc), XF(0x608), XF(0x470), XF(0x46c));
            Fl(0x20) = Fx_Lerp(u(fx), XF(0x600), XF(0x60c), XF(0x470), XF(0x46c));
            void* r = renderer();
            const float b2 = Fx_Lerp(u(fx), XF(0x628), XF(0x62c), XF(0x470), XF(0x46c));
            const float a2 = Fx_Lerp(u(fx), XF(0x61c), XF(0x620), XF(0x470), XF(0x46c));
            VCall<void>(r, 0x114 / 4, a2, b2, reinterpret_cast<void*>(A(0x18)));
        }
        else if (f & 0x20)
        {
            Fl(0x18) = fn_0041a2f0(u(fx), XF(0x5f8), XF(0x470), XF(0x46c), XF(0x610));
            Fl(0x1c) = fn_0041a2f0(u(fx), XF(0x5fc), XF(0x470), XF(0x46c), XF(0x614));
            Fl(0x20) = fn_0041a2f0(u(fx), XF(0x600), XF(0x470), XF(0x46c), XF(0x618));
            void* r = renderer();
            const float b2 = fn_0041a2f0(u(fx), XF(0x628), XF(0x470), XF(0x46c), XF(0x630));
            const float a2 = fn_0041a2f0(u(fx), XF(0x61c), XF(0x470), XF(0x46c), XF(0x624));
            VCall<void>(r, 0x114 / 4, a2, b2, reinterpret_cast<void*>(A(0x18)));
        }
        else if (f & 0x40)
        {
            Fl(0x18) = fn_0041a310(u(fx), XF(0x5f8), XF(0x604), XF(0x470), XF(0x46c));
            Fl(0x1c) = fn_0041a310(u(fx), XF(0x5fc), XF(0x608), XF(0x470), XF(0x46c));
            Fl(0x20) = fn_0041a310(u(fx), XF(0x600), XF(0x60c), XF(0x470), XF(0x46c));
            void* r = renderer();
            const float b2 = fn_0041a310(u(fx), XF(0x628), XF(0x62c), XF(0x470), XF(0x46c));
            const float a2 = fn_0041a310(u(fx), XF(0x61c), XF(0x620), XF(0x470), XF(0x46c));
            VCall<void>(r, 0x114 / 4, a2, b2, reinterpret_cast<void*>(A(0x18)));
        }
    }

    // Orientation.
    if (!static_cast<uint8_t>(fn_00419a60(fx)) && (X(0x460) & 0x40000))
    {
        orig::fn_00458000(u(fx), A(0x18));
        orig::fn_004e2e30(A(0x1f0), 0x3fc90fdbu, 0, 0, 0, 0, 0, 0);  // reads one stack argument (pi/2)
        orig::fn_004e36a0(A(0x18), A(0x1f0), 1, A(0x28));
        VCall<void>(fx, 0x54 / 4, reinterpret_cast<void*>(A(0x50)));
        orig::fn_004e3070(A(0x18), A(0x1f0), A(0x28), A(0x50));
        orig::fn_00457990(0, u(fx), A(0x1f0));
    }
    if (X(0x464) & 0x800)
        orig::fn_00455e70(u(fx), 0, 0);

    // Sub-emitters.
    if (X(0x460) & 0x100)
    {
        D(0x44) = 0;
        D(0x50) = 0;
        D(0x48) = 0;
        D(0x54) = 0;
        *reinterpret_cast<uint16_t*>(fr + 0x4c) = 0;
        *reinterpret_cast<uint16_t*>(fr + 0x58) = 0;
        D(0x34) = D(0x38);
        VCall<void>(fx, 0x54 / 4, reinterpret_cast<void*>(A(0x28)));
        uint8_t done;
        do
        {
            uint8_t* em = Field<uint8_t*>(fx, 0x4ec);
            done = 1;
            for (uint32_t i = 0; i < 10; ++i, em += 0x98)
            {
                auto ED = [&](uint32_t o) -> uint32_t& { return *reinterpret_cast<uint32_t*>(em + o); };
                auto EF = [&](uint32_t o) -> float& { return *reinterpret_cast<float*>(em + o); };
                const uint32_t ef = ED(0);
                if (!(ef & 8))
                    break;
                EF(0x2c) = EF(0x2c) - Fl(0x34);
                if (EF(0x38) > zero)
                {
                    const float v = EF(0x38) - Fl(0x34);
                    EF(0x38) = v;
                    if (v <= zero)
                    {
                        ED(0x2c) = 0x3f800000u;
                        ED(0x38) = 0x3f800000u;
                    }
                }
                const uint32_t mo = 0x3e8 + i * 0x44;  // this emitter's matrix in the frame
                const uint32_t vo = 0xf8 + i * 0xc;    // this emitter's position in the frame
                if (ef & 2)
                {
                    if (!(-EF(0x28) >= EF(0x2c)))
                        continue;
                    Fl(0x24) = k(0x005bcb5c) / EF(0x2c) * EF(0x28);
                    const uint32_t bone =
                        orig::fn_0041cba0(ED(0x30), u(g_game), Field<uint32_t>(game(), 0xa90));
                    orig::fn_00453c50(u(fx), bone, A(mo), 1, 0, 0, 1, 0, 1);
                    orig::fn_004e35a0(A(mo), A(0x90));
                    orig::fn_004e35a0(u(em + 0x3c), A(0x80));
                    orig::fn_004535b0(A(0x18), u(fx));
                    D(0xd8) = D(0x80);
                    D(0xf4) = ED(0x7c);
                    D(0xdc) = D(0x84);
                    D(0xe4) = D(0x8c);
                    D(0xe0) = D(0x88);
                    D(0xe8) = D(0x18);
                    D(0xf0) = D(0x20);
                    D(0xec) = D(0x1c);
                    D(0xcc) = ED(0x48);
                    D(0xd4) = ED(0x68);
                    D(0xd0) = ED(0x58);
                    D(0xc8) = D(mo + 0x40);
                    D(0xb0) = D(0x94);
                    D(0xac) = D(0x90);
                    D(0xb4) = D(0x98);
                    D(0xbc) = D(0x18);
                    D(0xb8) = D(0x9c);
                    D(0xc0) = D(0x1c);
                    D(0xa0) = D(mo + 0xc);
                    D(0xc4) = D(0x20);
                    D(0xa4) = D(mo + 0x1c);
                    D(0xa8) = D(mo + 0x2c);
                    orig::fn_004e3510(A(0xa0), D(0x24), A(0x238), A(0xcc), D(0x24));
                    orig::fn_004e3370(A(mo), A(0x238));
                    fr[0x44 + i] = 1;
                    if (X(0x460) & 0x4000)
                    {
                        D(0x78) = ED(0x8c);
                        D(0x64) = ED(0x80);
                        D(0x24) = static_cast<uint32_t>(rnd());
                        D(0x74) = ED(0x90);
                        D(0x68) = ED(0x84);
                        Fl(0x3c) = static_cast<float>(static_cast<int32_t>(D(0x24))) * (Fl(0x78) - Fl(0x64)) *
                                   k(0x005bcbc0);
                        D(0x24) = static_cast<uint32_t>(rnd());
                        D(0x7c) = ED(0x94);
                        D(0x6c) = ED(0x88);
                        Fl(0x70) = static_cast<float>(static_cast<int32_t>(D(0x24))) * (Fl(0x74) - Fl(0x68)) *
                                   k(0x005bcbc0);
                        D(0x24) = static_cast<uint32_t>(rnd());
                        const float z = static_cast<float>(static_cast<int32_t>(D(0x24))) * (Fl(0x7c) - Fl(0x6c)) *
                                            k(0x005bcbc0) +
                                        Fl(0x6c);
                        const float y = Fl(0x70) + Fl(0x68);
                        const float x = Fl(0x3c) + Fl(0x64);
                        orig::fn_004e3db0(A(0x280), ED(0x94), f2u(x), f2u(y), f2u(z));
                        orig::fn_004e2910(A(0x280), A(0x1a0), A(mo));
                        D(0x1cc) = D(mo + 0x2c);
                        D(0x1ac) = D(mo + 0xc);
                        D(0x1bc) = D(mo + 0x1c);
                        D(0x1e0) |= 0x10;
                    }
                    else
                    {
                        std::memcpy(fr + 0x1a0, fr + mo, 0x44);
                    }
                    D(0x1e4) = orig::fn_0041cba0(ED(0x34), u(g_game), Field<uint32_t>(game(), 0xa90));
                    D(0x178) = ED(0x24);
                    D(0x174) = 0;
                    D(0x170) = (X(0x464) & 0x100) ? 0x232u : 0x32u;
                    orig::fn_0041cc60(0, Field<uint32_t>(game(), 0xa90), 0, A(0x170));
                    const float t = EF(0x2c) + EF(0x28);
                    EF(0x2c) = t;
                    if (t < zero)
                        done = 0;
                }
                else if (ef & 0x80)
                {
                    if (!(-EF(0x28) >= EF(0x2c)))
                        continue;
                    const float f = k(0x005bcb5c) / EF(0x2c) * EF(0x28);
                    D(0x370) = ED(0x24);
                    const bool alt = (X(0x464) & 0x100) != 0;
                    D(0x36c) = 0;
                    D(0x368) = 0x38;
                    const float g1 = one - f;
                    Fl(0x374) = g1 * EF(0x48) + Fl(0x28) * f;
                    Fl(0x378) = g1 * EF(0x58) + Fl(0x2c) * f;
                    Fl(0x37c) = g1 * EF(0x68) + Fl(0x30) * f;
                    if (alt)
                        D(0x368) = 0x238;
                    orig::fn_0041cc60(0, Field<uint32_t>(game(), 0xa90), 0, A(0x368));
                    const float t = EF(0x2c) + EF(0x28);
                    EF(0x2c) = t;
                    if (t < zero)
                        done = 0;
                }
                else if (ef & 1)
                {
                    if (!(EF(0x2c) < zero))
                        continue;
                    orig::fn_0041dc80(0, u(fx), Field<uint32_t>(game(), 0xa90), ED(0x24), ED(0x30), ED(0x34), 1, 0);
                    ED(0x2c) = ED(0x28);
                }
                else if (ef & 0x40)
                {
                    if (!(-EF(0x28) >= EF(0x2c)))
                        continue;
                    Fl(0x24) = k(0x005bcb5c) / EF(0x2c) * EF(0x28);
                    const uint32_t a90 = Field<uint32_t>(game(), 0xa90);
                    const uint32_t bone = orig::fn_0041cba0(ED(0x30), a90, a90);
                    orig::fn_00453c50(u(fx), bone, A(0x1f0), 1, 0, 0, 1, 0, 1);
                    const float g1 = one - Fl(0x24);
                    fr[0x50 + i] = 1;
                    D(0x2ec) = 0;
                    D(0x2e8) = 0x38;
                    Fl(vo) = g1 * EF(0x48) + Fl(0x1fc) * Fl(0x24);
                    Fl(vo + 4) = g1 * EF(0x58) + Fl(0x20c) * Fl(0x24);
                    Fl(vo + 8) = g1 * EF(0x68) + Fl(0x21c) * Fl(0x24);
                    D(0x2f8) = D(vo + 4);
                    D(0x2f4) = D(vo);
                    D(0x2fc) = D(vo + 8);
                    D(0x2f0) = ED(0x24);
                    if (X(0x464) & 0x100)
                        D(0x2e8) = 0x238;
                    orig::fn_0041cc60(0, Field<uint32_t>(game(), 0xa90), 0, A(0x2e8));
                    const float t = EF(0x2c) + EF(0x28);
                    EF(0x2c) = t;
                    if (t < zero)
                        done = 0;
                }
            }
            D(0x34) = 0;
        } while (!done);

        // Emitter positions (unrolled ten times in the original).
        uint8_t* em = Field<uint8_t*>(fx, 0x4ec);
        for (uint32_t j = 0; j < 10; ++j, em += 0x98)
        {
            auto ED = [&](uint32_t o) -> uint32_t& { return *reinterpret_cast<uint32_t*>(em + o); };
            const uint32_t ef = ED(0);
            if (!(ef & 8))
                break;
            if (!ED(0x30))
            {
                ED(0x48) = D(0x28);
                ED(0x58) = D(0x2c);
                ED(0x68) = D(0x30);
            }
            else if (ef & 1)
            {
            }
            else if (ef & 2)
            {
                if (fr[0x44 + j])
                    std::memcpy(em + 0x3c, fr + 0x3e8 + j * 0x44, 0x44);
            }
            else if (fr[0x50 + j])
            {
                ED(0x48) = D(0xf8 + j * 0xc);
                ED(0x58) = D(0xfc + j * 0xc);
                ED(0x68) = D(0x100 + j * 0xc);
            }
        }
    }

    // Attachments placed at their bones.
    if (X(0x460) & 0x8000000)
    {
        for (uint32_t j = 0; j < 8; ++j)
        {
            void* a = Field<void*>(fx, 0x4f4 + j * 4);
            if (!a)
                break;
            const uint32_t bone = Field<uint32_t>(a, 0x58);
            if (!bone)
            {
                VCall<void>(fx, 0x54 / 4, reinterpret_cast<void*>(A(0x18)));
            }
            else
            {
                orig::fn_00453c50(u(fx), bone, A(0x1f0), 1, 0, 0, 1, 0, 1);
                D(0x18) = D(0x1fc);
                D(0x1c) = D(0x20c);
                D(0x20) = D(0x21c);
            }
            orig::fn_004958a0(A(0x18), u(a));
        }
    }
    if (X(0x460) & 0x200)
    {
        uint8_t* e = Field<uint8_t*>(fx, 0x4f0);
        for (uint32_t j = 0; j < 10; ++j, e += 0x194)
        {
            if (!(*e & 8))
                break;
            fn_0041a050(fx, e);
        }
    }
    if (X(0x634))
        orig::fn_0041bd30(u(fx));

    // Camera shake when the camera is within +0x4dc.
    if (X(0x460) & 0x8000)
    {
        orig::fn_0040f9e0(A(0x28), Field<uint32_t>(game(), 0xa68));
        VCall<void>(fx, 0x54 / 4, reinterpret_cast<void*>(A(0x18)));
        const float dx = Fl(0x18) - Fl(0x28);
        const float dz = Fl(0x20) - Fl(0x30);
        const float dy = Fl(0x1c) - Fl(0x2c);
        const float r = XF(0x4dc);
        const float d2 = (dy * dy + dz * dz) + dx * dx;
        if (r * r >= d2)
            orig::fn_0040fac0(Field<uint32_t>(game(), 0xa68), X(0x4d8), X(0x4d4));
        X(0x460) &= ~0x8000u;
    }

    // Sound volume by camera distance.
    if (X(0x460) & 0x80000)
    {
        VCall<void>(fx, 0x54 / 4, reinterpret_cast<void*>(A(0x18)));
        void* g = game();
        orig::fn_0040f9e0(A(0x28), Field<uint32_t>(g, 0xa68));
        const float dz = Fl(0x20) - Fl(0x30);
        const float dy = Fl(0x1c) - Fl(0x2c);
        const float dx = Fl(0x18) - Fl(0x28);
        Fl(0x34) = (dx * dx + dy * dy) + dz * dz;
        float v;
        if (Fl(0x34) < XF(0x4e4) || Fl(0x34) == XF(0x4e4))
            v = one;
        else if (XF(0x4e8) == zero)
            v = one;
        else if (!(Fl(0x34) >= XF(0x4e8)))
            v = (sqrtf(Fl(0x34)) - XF(0x4e4)) / (XF(0x4e8) - XF(0x4e4));
        else
            v = 0.0f;
        orig::fn_004e51f0(Field<uint32_t>(g, 0x30), X(0x4e0), f2u(v));
    }

    // Texture animation.
    if (float* anim = Field<float*>(fx, 0x514))
    {
        *anim = Fl(0x38) + *anim;
        for (uint32_t s = 0; s < 4; ++s)
            if (X(0x460) & (0x100000u << s))
                TexAnim_Step(fx, s);
    }

    // Texture frame counters.
    if (Field<void*>(fx, 0x518))
    {
        for (uint32_t s = 0; s < 4; ++s)
        {
            if (!(Field<uint8_t>(fx, 0x464) & (1u << s)))
                continue;
            uint8_t* fa = Field<uint8_t*>(fx, 0x518);
            float* rate = reinterpret_cast<float*>(fa + s * 4);
            float* acc = reinterpret_cast<float*>(fa + 0x30 + s * 4);
            const float t = Fl(0x38) + *acc;
            const float v = t * *rate;
            const int32_t whole = static_cast<int32_t>(v);
            D(0x3c) = static_cast<uint32_t>(whole);
            *acc = (v - static_cast<float>(whole)) / *rate;
            fa = Field<uint8_t*>(fx, 0x518);
            const int32_t frames = static_cast<int32_t>(t * *reinterpret_cast<float*>(fa + s * 4));
            *reinterpret_cast<uint32_t*>(fa + 0x20 + s * 4) += static_cast<uint32_t>(frames);
            fa = Field<uint8_t*>(fx, 0x518);
            uint32_t& frame = *reinterpret_cast<uint32_t*>(fa + 0x20 + s * 4);
            const uint32_t count = *reinterpret_cast<uint32_t*>(fa + 0x10 + s * 4);
            if (fa[0x40] & (1u << s))
                frame = frame % count;
            else if (frame >= count)
                frame = count - 1;
        }
    }

    // Light attached to a bone.
    if (X(0x460) & 0x1000000)
    {
        if (X(0x5cc) != 0xffffffffu)
            orig::fn_0048a520(X(0x5cc), Field<uint32_t>(game(), 0xa78));
        orig::fn_00453c50(u(fx), X(0x5c8), A(0x238), 1, 0, 0, 1, 0, 1);
        XF(0x5a0) = -Fl(0x23c);
        X(0x594) = D(0x244);
        XF(0x5a4) = -Fl(0x24c);
        X(0x598) = D(0x254);
        X(0x59c) = D(0x264);
        XF(0x5a8) = -Fl(0x25c);
        X(0x5cc) = orig::fn_0048a4b0(0xffffffffu, Field<uint32_t>(game(), 0xa78), u(fx) + 0x560);
        return X(0x5cc);
    }
    return 0;
}

// ---------------------------------------------------------------------------------------------------------------
// ABI adapters and hooks
// ---------------------------------------------------------------------------------------------------------------

static uint32_t GameShadow_Update_Abi(uint32_t ecx, uint32_t esi, uint32_t arg)
{
    return GameShadow_Update(ecx, p(esi), arg);
}
ABI_ENTRY_00418190(GameShadow_Update_Entry, GameShadow_Update_Abi)
HOOK(0x00418190, "GameShadow_Update", GameShadow_Update_Entry, nullptr);

static uint32_t GameShadow_RebuildReceivers_Abi(uint32_t esi)
{
    return GameShadow_RebuildReceivers(p(esi));
}
ABI_ENTRY_004181e0(GameShadow_RebuildReceivers_Entry, GameShadow_RebuildReceivers_Abi)
HOOK(0x004181e0, "GameShadow_RebuildReceivers", GameShadow_RebuildReceivers_Entry, nullptr);

static uint32_t GameShadow_Draw_Abi(uint32_t ecx, uint32_t decal, uint32_t flag)
{
    return GameShadow_Draw(p(ecx), decal, flag);
}
ABI_ENTRY_00418380(GameShadow_Draw_Entry, GameShadow_Draw_Abi)
HOOK(0x00418380, "GameShadow_Draw", GameShadow_Draw_Entry, nullptr);

static uint32_t Decal_MoveToList_Abi(uint32_t ecx, uint32_t edx)
{
    return Decal_MoveToList(p(ecx), reinterpret_cast<uint32_t*>(edx));
}
ABI_ENTRY_00418510(Decal_MoveToList_Entry, Decal_MoveToList_Abi)
HOOK(0x00418510, "Decal_MoveToList", Decal_MoveToList_Entry, nullptr);

static uint32_t fn_00418610_Abi(uint32_t eax)
{
    return fn_00418610(eax);
}
ABI_ENTRY_00418610(fn_00418610_Entry, fn_00418610_Abi)
HOOK(0x00418610, "fn_00418610", fn_00418610_Entry, nullptr);

static uint32_t fn_00418640_Abi(uint32_t ecx, uint32_t value)
{
    return fn_00418640(p(ecx), value);
}
ABI_ENTRY_00418640(fn_00418640_Entry, fn_00418640_Abi)
HOOK(0x00418640, "fn_00418640", fn_00418640_Entry, nullptr);

static uint32_t EnvMaps_Update_Abi(uint32_t eax)
{
    return EnvMaps_Update(eax);
}
ABI_ENTRY_00418880(EnvMaps_Update_Entry, EnvMaps_Update_Abi)
HOOK(0x00418880, "EnvMaps_Update", EnvMaps_Update_Entry, nullptr);

static uint32_t EnvMap_FindNearest_Abi(uint32_t ecx)
{
    return EnvMap_FindNearest(p(ecx));
}
ABI_ENTRY_00418920(EnvMap_FindNearest_Entry, EnvMap_FindNearest_Abi)
HOOK(0x00418920, "EnvMap_FindNearest", EnvMap_FindNearest_Entry, nullptr);

static uint32_t EnvMaps_ReleaseAll_Abi()
{
    return EnvMaps_ReleaseAll();
}
ABI_ENTRY_00418a10(EnvMaps_ReleaseAll_Entry, EnvMaps_ReleaseAll_Abi)
HOOK(0x00418a10, "EnvMaps_ReleaseAll", EnvMaps_ReleaseAll_Entry, nullptr);

static uint32_t EnvMap_Release_Abi(uint32_t esi)
{
    return EnvMap_Release(p(esi));
}
ABI_ENTRY_00418c30(EnvMap_Release_Entry, EnvMap_Release_Abi)
HOOK(0x00418c30, "EnvMap_Release", EnvMap_Release_Entry, nullptr);

static uint32_t EnvMap_Render_Abi(uint32_t esi)
{
    return EnvMap_Render(p(esi));
}
ABI_ENTRY_00418c80(EnvMap_Render_Entry, EnvMap_Render_Abi)
HOOK(0x00418c80, "EnvMap_Render", EnvMap_Render_Entry, nullptr);

static uint32_t fn_00419020_Abi(uint32_t ecx, uint32_t desc)
{
    return fn_00419020(p(ecx), p(desc));
}
ABI_ENTRY_00419020(fn_00419020_Entry, fn_00419020_Abi)
HOOK(0x00419020, "fn_00419020", fn_00419020_Entry, nullptr);

static uint32_t fn_004190a0_Abi(uint32_t edi)
{
    return fn_004190a0(p(edi));
}
ABI_ENTRY_004190a0(fn_004190a0_Entry, fn_004190a0_Abi)
HOOK(0x004190a0, "fn_004190a0", fn_004190a0_Entry, nullptr);

static uint32_t fn_00419290_Abi(uint32_t eax, uint32_t esi)
{
    return fn_00419290(eax, p(esi));
}
ABI_ENTRY_00419290(fn_00419290_Entry, fn_00419290_Abi)
HOOK(0x00419290, "fn_00419290", fn_00419290_Entry, nullptr);

static uint32_t TexAnim_BuildStageMatrix_Abi(uint32_t eax, uint32_t fx, uint32_t out)
{
    return TexAnim_BuildStageMatrix(eax, p(fx), reinterpret_cast<float*>(out));
}
ABI_ENTRY_004195e0(TexAnim_BuildStageMatrix_Entry, TexAnim_BuildStageMatrix_Abi)
HOOK(0x004195e0, "TexAnim_BuildStageMatrix", TexAnim_BuildStageMatrix_Entry, nullptr);

static uint32_t TexAnim_SetStages_Abi(uint32_t ecx, uint32_t material)
{
    return TexAnim_SetStages(p(ecx), p(material));
}
ABI_ENTRY_004196f0(TexAnim_SetStages_Entry, TexAnim_SetStages_Abi)
HOOK(0x004196f0, "TexAnim_SetStages", TexAnim_SetStages_Entry, nullptr);

static uint32_t TexAnim_FindFrameTexture_Abi(uint32_t eax, uint32_t edx, uint32_t name)
{
    return TexAnim_FindFrameTexture(p(eax), edx, name);
}
ABI_ENTRY_004198c0(TexAnim_FindFrameTexture_Entry, TexAnim_FindFrameTexture_Abi)
HOOK(0x004198c0, "TexAnim_FindFrameTexture", TexAnim_FindFrameTexture_Entry, nullptr);

static uint32_t TexAnim_ResolveTextures_Abi(uint32_t ecx, uint32_t material)
{
    return TexAnim_ResolveTextures(p(ecx), p(material));
}
ABI_ENTRY_00419940(TexAnim_ResolveTextures_Entry, TexAnim_ResolveTextures_Abi)
HOOK(0x00419940, "TexAnim_ResolveTextures", TexAnim_ResolveTextures_Entry, nullptr);

static uint32_t fn_00419a60_Abi(uint32_t edi)
{
    return fn_00419a60(p(edi));
}
ABI_ENTRY_00419a60(fn_00419a60_Entry, fn_00419a60_Abi)
HOOK(0x00419a60, "fn_00419a60", fn_00419a60_Entry, nullptr);

static uint32_t TexAnim_Step_Abi(uint32_t eax, uint32_t edi)
{
    return TexAnim_Step(p(eax), edi);
}
ABI_ENTRY_00419d30(TexAnim_Step_Entry, TexAnim_Step_Abi)
HOOK(0x00419d30, "TexAnim_Step", TexAnim_Step_Entry, nullptr);

static uint32_t fn_0041a050_Abi(uint32_t eax, uint32_t emitter)
{
    return fn_0041a050(p(eax), p(emitter));
}
ABI_ENTRY_0041a050(fn_0041a050_Entry, fn_0041a050_Abi)
HOOK(0x0041a050, "fn_0041a050", fn_0041a050_Entry, nullptr);

static float Fx_Lerp_Abi(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    return Fx_Lerp(a0, u2f(a1), u2f(a2), u2f(a3), u2f(a4));
}
ABI_ENTRY_0041a2d0(Fx_Lerp_Entry, Fx_Lerp_Abi)
HOOK(0x0041a2d0, "Fx_Lerp", Fx_Lerp_Entry, nullptr);

static float fn_0041a2f0_Abi(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    return fn_0041a2f0(a0, u2f(a1), u2f(a2), u2f(a3), u2f(a4));
}
ABI_ENTRY_0041a2f0(fn_0041a2f0_Entry, fn_0041a2f0_Abi)
HOOK(0x0041a2f0, "fn_0041a2f0", fn_0041a2f0_Entry, nullptr);

// 0x0041a310 returns st0 although the generated stub for it does not: use the st0-returning stub shape of
// 0x0041a2d0 (identical inputs: five stack arguments, callee pops 20).
static float fn_0041a310_Abi(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    return fn_0041a310(a0, u2f(a1), u2f(a2), u2f(a3), u2f(a4));
}
ABI_ENTRY_0041a2d0(fn_0041a310_Entry, fn_0041a310_Abi)
HOOK(0x0041a310, "fn_0041a310", fn_0041a310_Entry, nullptr);

// 0x0041a350: symbols/abi.csv lists 16 caller-cleaned stack arguments; none is read (see the function comment).
static uint32_t fn_0041a350_Abi(uint32_t ecx, uint32_t /*a0*/, uint32_t /*a1*/, uint32_t /*a2*/, uint32_t /*a3*/,
                                uint32_t /*a4*/, uint32_t /*a5*/, uint32_t /*a6*/, uint32_t /*a7*/, uint32_t /*a8*/,
                                uint32_t /*a9*/, uint32_t /*a10*/, uint32_t /*a11*/, uint32_t /*a12*/, uint32_t /*a13*/,
                                uint32_t /*a14*/, uint32_t /*a15*/)
{
    return fn_0041a350(p(ecx));
}
ABI_ENTRY_0041a350(fn_0041a350_Entry, fn_0041a350_Abi)
HOOK(0x0041a350, "fn_0041a350", fn_0041a350_Entry, nullptr);