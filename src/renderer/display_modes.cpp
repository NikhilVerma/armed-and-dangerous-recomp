// Recovered: display mode enumeration (S3, Approach B).
//   0x005089d0 Renderer_EnumerateDisplayModes
//   0x00508870 Renderer_AddDisplayModes   (inlined here: the original takes the count pointer in EDI)
//   0x00508990 DisplayMode_Compare
#include <cstring>

#include "game.h"
#include "gen/orig_calls.h"
#include "hooks.h"
#include "renderer.h"

DisplayModeFilter g_displayModeFilter;

namespace
{
// 0x00508370: copies what the renderer needs from D3DCAPS9 into a 0x120-byte summary
// (eax = caps, edx = output; [renderer]; generated wrapper, symbols/abi.csv).
void Orig_SummariseCaps(Renderer* self, const D3DCAPS9* caps, void* summary)
{
    orig::fn_00508370(reinterpret_cast<uint32_t>(caps), reinterpret_cast<uint32_t>(summary),
                      reinterpret_cast<uint32_t>(self));
}

// 0x00538f30, the game's CRT qsort (cdecl).
void Orig_qsort(void* base, unsigned count, unsigned size, int(__cdecl* compare)(const void*, const void*))
{
    GameFunction<void(__cdecl*)(void*, unsigned, unsigned, int(__cdecl*)(const void*, const void*))>(0x00538f30)(
        base, count, size, compare);
}

bool AspectAllowed(uint32_t width, uint32_t height)
{
    if (g_displayModeFilter.allowAnyAspect)
        return true;
    return height * 16 == width * 9      // 16:9
           || height * 4 == width * 3    // 4:3
           || height * 5 == width * 4;   // 5:4
}
}  // namespace

// 0x00508990: format descending, then width ascending, then height ascending.
int __cdecl DisplayMode_Compare(const void* a, const void* b)
{
    auto* x = static_cast<const D3DDISPLAYMODE*>(a);
    auto* y = static_cast<const D3DDISPLAYMODE*>(b);
    if (static_cast<int>(x->Format) > static_cast<int>(y->Format))
        return -1;
    if (static_cast<int>(x->Format) < static_cast<int>(y->Format))
        return 1;
    if (x->Width < y->Width)
        return -1;
    if (x->Width > y->Width)
        return 1;
    if (x->Height < y->Height)
        return -1;
    return x->Height > y->Height ? 1 : 0;
}

// 0x00508870: append the adapter's modes in `format` that pass the filter, skipping duplicates that differ
// only in refresh rate. Stored refresh rate is 0.
// The original has no capacity check; this version stops at `capacity` (see docs/original-bugs.md).
static void Renderer_AddDisplayModes(Renderer* self, UINT adapter, D3DFORMAT format, D3DDISPLAYMODE* modes,
                                     uint32_t* count, uint32_t capacity)
{
    UINT available = self->d3d->GetAdapterModeCount(adapter, format);
    for (UINT i = 0; i < available; i++)
    {
        D3DDISPLAYMODE mode;
        self->d3d->EnumAdapterModes(adapter, format, i, &mode);
        if (mode.Width < g_displayModeFilter.minWidth || mode.Width > g_displayModeFilter.maxWidth)
            continue;
        if (!AspectAllowed(mode.Width, mode.Height))
            continue;

        uint32_t j = 0;
        while (j < *count && !(modes[j].Width == mode.Width && modes[j].Height == mode.Height &&
                               modes[j].Format == mode.Format))
            j++;
        if (j != *count || *count >= capacity)
            continue;

        modes[*count] = mode;
        modes[*count].RefreshRate = 0;
        (*count)++;
    }
}

void __stdcall Renderer_EnumerateDisplayModes(Renderer* self)
{
    D3DCAPS9 caps;
    self->d3d->GetDeviceCaps(0, D3DDEVTYPE_HAL, &caps);

    uint8_t capsSummary[sizeof(self->capsSummary)];
    Orig_SummariseCaps(self, &caps, capsSummary);

    D3DDISPLAYMODE modes[kRendererMaxModes];
    uint32_t count = 0;
    Renderer_AddDisplayModes(self, 0, D3DFMT_A8R8G8B8, modes, &count, kRendererMaxModes);  // 0x15
    Renderer_AddDisplayModes(self, 0, D3DFMT_X8R8G8B8, modes, &count, kRendererMaxModes);  // 0x16
    Renderer_AddDisplayModes(self, 0, D3DFMT_R5G6B5, modes, &count, kRendererMaxModes);    // 0x17
    Orig_qsort(modes, count, sizeof(D3DDISPLAYMODE), DisplayMode_Compare);

    memcpy(self->capsSummary, capsSummary, sizeof(capsSummary));
    self->field_9e8 = 0;
    self->field_9ec = 1;
    self->modeCount = count;
    memcpy(self->modes, modes, count * sizeof(D3DDISPLAYMODE));

    self->d3d->GetAdapterDisplayMode(0, &self->desktopMode);
}

HOOK(0x005089d0, "Renderer_EnumerateDisplayModes", Renderer_EnumerateDisplayModes, nullptr);
