// Recovered: UI pass. S2, Approach B.
//   0x00464460 UiPass_Draw
#include "ui_pass.h"

#include "game.h"
#include "game_object.h"
#include "gen/entry_stubs.h"
#include "gen/orig_calls.h"
#include "hooks.h"

void (*g_uiObjectBeginDraw)(void* object) = nullptr;
void (*g_uiObjectEndDraw)(void* object) = nullptr;
void (*g_uiPassBegin)() = nullptr;

namespace
{
template <class T>
uint32_t u(T* p)
{
    return reinterpret_cast<uint32_t>(p);
}

// Renderer virtuals (slot = vtable offset / 4). Getters return a 0x44-byte matrix (16 floats + flags word).
constexpr int kGetProjection = 0x60 / 4, kGetView = 0x64 / 4, kGetWorld = 0x68 / 4;
constexpr int kSetProjection = 0x4c / 4, kSetView = 0x50 / 4, kSetWorld = 0x54 / 4;
// UI object virtuals.
constexpr int kObjDraw = 0x13c / 4, kObjDrawListed = 0x138 / 4, kObjDrawFlags = 0x150 / 4;

constexpr uint32_t kMatrixDwords = 0x11;

void* Renderer()
{
    return Field<void*>(g_game, 0x38);
}

// rep movsd, ecx = 0x11.
void CopyMatrix(uint32_t (&dst)[kMatrixDwords], const uint32_t* src)
{
    for (uint32_t i = 0; i < kMatrixDwords; ++i)
        dst[i] = src[i];
}

// Sets world (virtual 21), view (20), projection (19), in that order.
void SetMatrices(void* r, uint32_t* world, uint32_t* view, uint32_t* projection)
{
    VCall<void>(r, kSetWorld, world);
    VCall<void>(r, kSetView, view);
    VCall<void>(r, kSetProjection, projection);
}
}  // namespace

// 0x00464460 (stack: ui). Saves the caller's world/view/projection, sets the screen transform (0x004f01c0) and
// snapshots it, updates the world-tracking markers (list Game+0xa84 +0xebc, 0x004190a0), then draws the UI objects
// (list Game+0xa84 +0xeb8), the list at ui +0x9c and the object at ui +0x54, and restores the caller's matrices.
uint32_t UiPass_Draw(void* ui)
{
    if (g_uiPassBegin)
        g_uiPassBegin();
    // 0x0046446c: list iterator (two dwords at esp+0x10 after the pushes), shared by all three walks.
    uint32_t iter[2] = {0, 0};
    uint32_t savedWorld[kMatrixDwords], savedView[kMatrixDwords], savedProjection[kMatrixDwords];   // +0xe8/+0x12c/+0x170
    uint32_t screenWorld[kMatrixDwords], screenView[kMatrixDwords], screenProjection[kMatrixDwords];  // +0x18/+0x5c/+0xa0

    // 0x00464476: when Game+0xae4 is set, 0x004916d0 (eax = Game+0xab4).
    if (Field<uint8_t>(g_game, 0xae4) != 0)
        orig::fn_004916d0(u(Field<void*>(g_game, 0xab4)));

    // 0x00464498: save the caller's matrices (getters +0x68 world, +0x64 view, +0x60 projection).
    void* r = Renderer();
    CopyMatrix(savedWorld, VCall<const uint32_t*>(r, kGetWorld));
    CopyMatrix(savedView, VCall<const uint32_t*>(r, kGetView));
    CopyMatrix(savedProjection, VCall<const uint32_t*>(r, kGetProjection));

    // 0x004644e0: screen transform (renderer on the stack, callee pops).
    orig::fn_004f01c0(u(Renderer()));

    // 0x004644ef: snapshot the screen transform.
    r = Renderer();
    CopyMatrix(screenWorld, VCall<const uint32_t*>(r, kGetWorld));
    CopyMatrix(screenView, VCall<const uint32_t*>(r, kGetView));
    CopyMatrix(screenProjection, VCall<const uint32_t*>(r, kGetProjection));

    // 0x0046453b: world-tracking markers, list Game+0xa84 +0xebc; 0x004e7340 (eax = list, esi = &iter) returns the
    // first element, 0x004e73c0 (edi = &iter) the next; 0x004190a0 (edi = element).
    void* lists = Field<void*>(g_game, 0xa84);
    uint32_t item = orig::fn_004e7340(Field<uint32_t>(lists, 0xebc), u(iter));
    while (item != 0)
    {
        orig::fn_004190a0(item);
        item = orig::fn_004e73c0(u(iter));
    }

    // 0x00464568: set the screen transform (markers may have changed it).
    r = Renderer();
    SetMatrices(r, screenWorld, screenView, screenProjection);

    // 0x00464598: hide mask = 0x20 while Game+0x2684 is non-zero (neg/sbb/and), else 0.
    uint32_t hideMask = Field<uint32_t>(g_game, 0x2684) != 0 ? 0x20u : 0u;
    lists = Field<void*>(g_game, 0xa84);
    uint8_t* obj = reinterpret_cast<uint8_t*>(orig::fn_004e7340(Field<uint32_t>(lists, 0xeb8), u(iter)));
    while (obj != nullptr)
    {
        // 0x004645d0: visible = (+0x474 & 2) || ((byte +0x470 & 1) && (+0x474 & 4)), and not (+0x474 & hideMask).
        uint32_t flags = Field<uint32_t>(obj, 0x474);
        bool visible = (flags & 2) != 0 || ((Field<uint8_t>(obj, 0x470) & 1) != 0 && (flags & 4) != 0);
        if (visible && (hideMask & flags) == 0)
        {
            // Enhancement point: the screen transform is set here (0x00464568 or the reset below).
            if (g_uiObjectBeginDraw)
                g_uiObjectBeginDraw(obj);

            // 0x004645eb: draw virtual +0x13c.
            VCall<uint32_t>(obj, kObjDraw);
            // 0x004645f5: 0x00453570 (eax = object); if al, flush 0x0048a1a0.
            if (static_cast<uint8_t>(orig::fn_00453570(u(obj))) != 0)
                orig::fn_0048a1a0();

            // Enhancement point: end of the object's draw path.
            if (g_uiObjectEndDraw)
                g_uiObjectEndDraw(obj);

            // 0x00464605: virtual +0x150; when bit 2 of al is clear, reset world/view/projection to the screen
            // transform (0x00464613). When it is set, nothing is reset and the next object inherits the matrices.
            if ((static_cast<uint8_t>(VCall<uint32_t>(obj, kObjDrawFlags)) & 2) == 0)
            {
                r = Renderer();
                SetMatrices(r, screenWorld, screenView, screenProjection);
            }
        }
        // 0x00464643
        obj = reinterpret_cast<uint8_t*>(orig::fn_004e73c0(u(iter)));
    }

    // 0x00464656: list at ui +0x9c; virtual +0x138 instead of +0x13c, no visibility test.
    void* listed = reinterpret_cast<void*>(orig::fn_004e7340(Field<uint32_t>(ui, 0x9c), u(iter)));
    while (listed != nullptr)
    {
        VCall<uint32_t>(listed, kObjDrawListed);
        if (static_cast<uint8_t>(orig::fn_00453570(u(listed))) != 0)
            orig::fn_0048a1a0();
        if ((static_cast<uint8_t>(VCall<uint32_t>(listed, kObjDrawFlags)) & 2) == 0)  // 0x0046468a
        {
            r = Renderer();
            SetMatrices(r, screenWorld, screenView, screenProjection);
        }
        // 0x004646c8: 0x00454750 (esi = ui +0x9c reloaded, edi = element).
        orig::fn_00454750(Field<uint32_t>(ui, 0x9c), u(listed));
        listed = reinterpret_cast<void*>(orig::fn_004e73c0(u(iter)));
    }

    // 0x004646e2: when byte ui +0x50 is set and ui +0x54 is non-null, its draw virtual +0x13c (no flush, no reset).
    if (Field<uint8_t>(ui, 0x50) != 0)
    {
        void* top = Field<void*>(ui, 0x54);
        if (top != nullptr)
            VCall<uint32_t>(top, kObjDraw);
    }

    // 0x004646fa: restore the caller's matrices.
    r = Renderer();
    SetMatrices(r, savedWorld, savedView, savedProjection);

    // 0x0046472f: release the iterator's node (dec [node +0x18]) and return it in eax.
    uint32_t node = iter[1];
    if (node != 0)
        --Field<uint32_t>(reinterpret_cast<void*>(node), 0x18);
    return node;
}

static uint32_t UiPass_Draw_Abi(uint32_t a0)
{
    return UiPass_Draw(reinterpret_cast<void*>(a0));
}
ABI_ENTRY_00464460(UiPass_Draw_Entry, UiPass_Draw_Abi)
HOOK(0x00464460, "UiPass_Draw", UiPass_Draw_Entry, nullptr);
