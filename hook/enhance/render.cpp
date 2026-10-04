// Rendering enhancements. Switches in [render] in and.ini, all off by default.
//
//   wireframe=1   draw everything as wireframe
//
// The game has its own debug switch for this: bit 1 of the renderer's option byte (+0x1628) makes
// Renderer_BeginScene (0x005042d0, src/loader/auto_00502cb0.cpp) set D3DRS_FILLMODE to D3DFILL_WIREFRAME
// instead of D3DFILL_SOLID. Nothing in the shipped game sets the bit; this sets it before each scene and leaves
// the rest to the recovered code.
#include "config.h"
#include "hooks.h"
#include "log.h"
#include "settings.h"

static void* g_nextBeginScene;

// Same registers as the original (ecx = renderer, nothing else read or changed besides flags).
static __declspec(naked) void Render_BeginScene_Wireframe()
{
    __asm {
        or byte ptr [ecx + 0x1628], 2
        jmp dword ptr [g_nextBeginScene]
    }
}

void RenderInit()
{
    // Vulkan backend switches (src/rhi/vulkan), read when the device starts.
    using S = RemasterSetting;
    RemasterRegister({"render", "tessellation", L"Tessellated characters (Vulkan)", S::Toggle, 0, 1, 1, {}, 0, true,
                      nullptr},
                     0);
    RemasterRegister({"render", "anisotropy", L"Anisotropic filtering (Vulkan)", S::Slider, 0, 16, 4, {}, 0, true,
                      nullptr},
                     0);
    RemasterRegister({"render", "tonemap_aces", L"Filmic tone mapping (Vulkan)", S::Toggle, 0, 1, 1, {}, 0, true,
                      nullptr},
                     0);
    if (ConfigInt("render", "wireframe", 0))
    {
        HookWrap("Renderer_BeginScene", reinterpret_cast<void*>(Render_BeginScene_Wireframe), &g_nextBeginScene);
        Log("render: wireframe on");
    }
}
