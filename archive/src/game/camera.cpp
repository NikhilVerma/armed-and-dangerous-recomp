// Recovered: the game camera's projection setup. S2, Approach B.
//   0x0040f6e0 Camera_SetupProjection
#include "camera.h"

#include "game.h"
#include "game_object.h"
#include "gen/entry_stubs.h"
#include "gen/orig_calls.h"
#include "hooks.h"

void (*g_cameraProjectionAdjust)(void* camera, float* fov, float* aspect) = nullptr;

namespace
{
template <class T>
uint32_t u(T* p)
{
    return reinterpret_cast<uint32_t>(p);
}

uint32_t f2u(float f)
{
    return *reinterpret_cast<uint32_t*>(&f);
}
}  // namespace

// 0x0040f6e0 (edi = camera; caller 0x00403050, the scene render): sets the frame's projection and view.
// - renderer virtual 31 (0x7c, the viewport size; the result is not used here)
// - far = camera +0xd4 * +0xd0 (0x0040f860 with cl = 0), near = camera +0xf8 (0x0040f850)
// - 0x004f0100 (esi = renderer) builds the projection into a local 0x44-byte matrix from the FOV (+0xf4), near,
//   far and aspect 0 (computed from the viewport and Renderer_GetAspect)
// - renderer virtual 19 (0x4c, SetProjection) with it, then virtual 20 (0x50, SetView) with Game+0x704; returns
//   what SetView returns.
uint32_t Camera_SetupProjection(void* camera)
{
    void* renderer = Field<void*>(g_game, 0x38);
    VCall<void*>(renderer, 0x7c / 4);
    renderer = Field<void*>(g_game, 0x38);
    float farPlane = Field<float>(camera, 0xd4) * Field<float>(camera, 0xd0);
    float nearPlane = Field<float>(camera, 0xf8);
    float fov = Field<float>(camera, 0xf4);
    float aspect = 0.0f;
    if (g_cameraProjectionAdjust)
        g_cameraProjectionAdjust(camera, &fov, &aspect);
    uint8_t matrix[0x44];
    orig::fn_004f0100(u(renderer), u(matrix), f2u(fov), f2u(nearPlane), f2u(farPlane), f2u(aspect));
    VCall<void>(renderer, 0x4c / 4, matrix);
    return VCall<uint32_t>(Field<void*>(g_game, 0x38), 0x50 / 4, reinterpret_cast<uint8_t*>(g_game) + 0x704);
}

static uint32_t Camera_SetupProjection_Abi(uint32_t edi)
{
    return Camera_SetupProjection(reinterpret_cast<void*>(edi));
}
ABI_ENTRY_0040f6e0(Camera_SetupProjection_Entry, Camera_SetupProjection_Abi)
HOOK(0x0040f6e0, "Camera_SetupProjection", Camera_SetupProjection_Entry, nullptr);
