#pragma once
// The game's own shadows (docs/subsystems/shadows.md): one render-to-texture silhouette per caster, projected onto
// the receivers as a decal. The pool and lists belong to the decal manager (Game+0xa7c); +4 is m_enableShadows.

#include <cstdint>

// Enhancement point (hook/enhance/shadows.cpp): true skips both passes (renderer-side shadow maps replace them).
// False (the default) is the original behaviour.
extern bool g_gameShadowsReplaced;

// 0x004163c0 (eax = decal manager; stack: arg passed on to each shadow's update): renders the silhouettes.
uint32_t Shadows_Update(void* manager, uint32_t arg);
// 0x004162b0 (eax = decal manager): draws the active shadows onto their receivers.
uint32_t Shadows_Draw(void* manager);
