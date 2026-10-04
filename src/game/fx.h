#pragma once
// The fx script system (docs/subsystems/weather.md: "fx script opcodes"). An fx instance runs a byte-code script;
// its owner object is at +0xb4, its flags at +0x14 (bit 2: locked objects allowed, bit 7 and bit 10: list
// insertions for created objects), its start time at +0xc.

#include <cstdint>

// Enhancement point.
// Called with each mesh object this opcode creates and attaches, and its object name (the class/name string the
// opcode spawns, the second stack argument), right before the function returns it. Null by default.
extern void (*g_fxMeshAttached)(void* object, const char* name);

// 0x00423680 (ebx = fx instance; stack: opcode params, object name, attach point list; callee pops 0xc): fx script
// opcode 0x13, creates an object and attaches it to the fx's owner. Returns the object, or null.
void* Fx_AttachMesh(void* fx, void* params, const char* name, void* attachPoints);
