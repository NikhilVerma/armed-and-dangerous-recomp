// Recovered: game-side physics glue around the statically linked Havok 2.x library (batch physics_02,
// 0x004517b0-0x00459810). Static recovery 2026-10-05, compile-checked only (the game was not run); the oracle
// (tools/compare.py) is still to do.
//
// Havok itself is not decompiled: Havok functions are called through orig::fn_XXXXXXXX exactly as the original does,
// and Havok objects are accessed with Field<T> at the offsets documented in docs/subsystems/physics.md (entity +0x08
// motion, +0x10 simulation island, +0x38 friction, +0x3c restitution, +0x44 world, +0x78/+0x7c pair array; island
// +0x24 index, +0x29 active; motion +0x2c inverse mass, +0x30 rotation, +0x40/+0x50 linear/angular velocity, +0x70
// centre of mass, +0xb0 position; motion vtable slots 0x18 getType, 0x40 setPosition, 0x48 setPositionAndRotation,
// 0x50/0x54 set linear/angular velocity, 0x5c/0x60/0x64 impulses).
//
// Body creation info ("cinfo", 0xc4 bytes, initialised by 0x00449510): +0x10/+0x11 flags, +0x20 mass properties
// (0x50 bytes), +0x70 motion type, +0x74 shape, +0x78/+0x79/+0x7a/+0x7b/+0x7c flags, +0x80 transform (0x44 bytes).
//
// Conventions: every function's comment gives its address, its register/stack ABI (as the original reads it), what it
// does and the evidence. "guess" marks interpretations not proven by strings or behaviour. Large functions keep the
// original stack frame as one byte array (Frame), addressed with the original's esp offsets after the register pushes.
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
// A float constant of the game's .rdata (0x0056c05c = 0.0, 0x005bcb2c = 1.0, 0x005bcb4c = 0.5).
inline float K(uint32_t address)
{
    return GameGlobal<float>(address);
}
// The original stack frame of a large function, addressed by the original's esp offsets.
template <uint32_t N>
struct Frame
{
    alignas(16) uint8_t b[N];
    Frame()
    {
        std::memset(b, 0, N);
    }
    uint32_t& L(uint32_t o)
    {
        return *reinterpret_cast<uint32_t*>(b + o);
    }
    float& F(uint32_t o)
    {
        return *reinterpret_cast<float*>(b + o);
    }
    uint8_t& C(uint32_t o)
    {
        return b[o];
    }
    uint32_t A(uint32_t o)
    {
        return reinterpret_cast<uint32_t>(b + o);
    }
};
// 0x004e07f0 (ecx = heap 0x0063a210; stack: tag, size, align, name; callee pops): the game's tagged allocator.
inline uint32_t Alloc(uint32_t tag, uint32_t size, uint32_t align, uint32_t name)
{
    return orig::fn_004e07f0(0x0063a210u, tag, size, align, name);
}
// hkMemory singleton [0x005fed84]: virtual +0x10 alloc(size, class), +0x14 free(p, size, class).
inline uint8_t* HkAlloc(uint32_t size, uint32_t cls)
{
    return VCall<uint8_t*>(GameGlobal<void*>(0x005fed84), 0x10 / 4, size, cls);
}
inline uint32_t HkFree(uint32_t p, uint32_t size, uint32_t cls)
{
    return VCall<uint32_t>(GameGlobal<void*>(0x005fed84), 0x14 / 4, p, size, cls);
}
// hkReferencedObject::removeReference inlined: u16 +6 reference count; at zero, virtual 0 (deleting destructor)(1).
inline void HkRelease(uint8_t* object)
{
    --Field<uint16_t>(object, 6);
    if (Field<uint16_t>(object, 6) == 0)
        VCall<void>(object, 0, 1u);
}
// hkWorld collision input +4 (Game+0xa8c -> +8 world -> +0xe8 -> +4), passed to the phantom-shape builder 0x004a4530.
inline uint32_t CollisionInput4()
{
    return Field<uint32_t>(Field<uint8_t*>(Field<uint8_t*>(Field<uint8_t*>(g_game, 0xa8c), 8), 0xe8), 4);
}
// The inlined Havok wake idiom: when the entity has a simulation island (+0x10) that is not active (island +0x29 ==
// 0), the entity is in a world (+0x44) and the island has a valid index (+0x24 != -1), call hkWorld_activateIsland
// 0x004ad130 (esi = world, edi = island).
inline void WakeEntity(uint8_t* entity)
{
    uint8_t* island = Field<uint8_t*>(entity, 0x10);
    if (island && !island[0x29])
    {
        uint8_t* world = Field<uint8_t*>(entity, 0x44);
        if (world && Field<int32_t>(island, 0x24) != -1)
            orig::fn_004ad130(u(world), u(island));
    }
}
inline uint8_t* Motion(uint8_t* entity)
{
    return Field<uint8_t*>(entity, 8);
}
// 1 / inverse mass (motion +0x2c), or 0 when the inverse mass is 0 (fcom / test ah, 0x44 / jp idiom).
inline float MassOf(uint8_t* entity)
{
    const float inv = Field<float>(Motion(entity), 0x2c);
    if (inv == K(0x0056c05c))
        return K(0x0056c05c);
    return K(0x005bcb2c) / inv;
}
// x87 fsin / fcos of a single-precision angle, as the original computes them.
inline float X87Sin(float angle)
{
    float r;
    __asm {
        fld angle
        fsin
        fstp r
    }
    return r;
}
inline float X87Cos(float angle)
{
    float r;
    __asm {
        fld angle
        fcos
        fstp r
    }
    return r;
}
// 0x004e7880 (ecx = in, edx = out): normalises a 3-vector and returns its length in st0. The original callers pop
// st0 (fstp st(0)), so this wrapper pops it too (as src/physics/physics_01.cpp).
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

// Prototypes (functions of this file call each other directly).
uint32_t Object_ForceGround(uint8_t* obj, float height, float range);
uint8_t fn_00455720(const uint32_t* collidable, uint8_t* world);
uint32_t PointCollector_Construct(uint8_t* input, uint32_t other, uint32_t collidable, uint8_t* collector);
uint32_t fn_004563d0(uint8_t* child, uint8_t* shape);
uint32_t fn_004564b0(uint8_t* desc, uint8_t* skeleton, uint8_t* table);
uint32_t fn_00456a90(uint8_t* obj, uint32_t manager, uint32_t a1, uint8_t keyframed);
uint32_t fn_00456c40(uint32_t manager, uint8_t* obj, uint8_t* def, uint32_t a3, uint32_t a4, uint8_t* desc);
uint32_t Body_Create(uint32_t manager, uint8_t* obj, uint8_t* cinfo);
uint64_t Object_SetPosition(uint32_t eax, const float* position, uint8_t* obj);
uint32_t Object_SetTransform(uint32_t eax, uint8_t* obj, const float* matrix);
void Body_SetFriction(uint8_t* obj, uint32_t value);
void Body_SetRestitution(uint8_t* obj, uint32_t value);
uint32_t fn_00458dd0(uint8_t* obj, uint32_t a1, uint32_t a2, uint32_t a3);
uint32_t fn_00459810(const float* axis, uint8_t* bodyA, uint8_t* bodyB, uint8_t* out, const float* pivot);

// ================================================================================================================
// Object property loader and collision shapes
// ================================================================================================================

// 0x004517b0 (ecx = object; stack: instance, matrix (0x44 bytes or null), properties, section; callee pops 0x10;
// callers: the object loaders 0x00404a40, 0x0040a700, 0x00418a70 ...). Loads an object's physical properties and
// builds its rigid body. Clears +0x400..+0x40c, +0x34..+0x1bc and the animation slots +0x248..+0x2c8; +0x2d8 = a new
// 0x10-byte block initialised by 0x004e7010 (Game+0xac4); registers the object (0x004546d0, list Game+0xa84 +0xe70);
// +0x424 |= 4; virtual +0xf4(1); 0x004540a0. Scales +0x378..+0x384 = 1.0, +0x454/+0x458 = 100.0. Properties:
// "material" (default "default") loaded by PhysMaterial_Load into friction 1.0 / restitution 0.5 / density 0,
// overridden by "friction", "restitution", "density" (density also into +0x38c); 0x00451360 (base properties) and
// 0x004087d0 (animation slots). The instance's +8 word: bit 31 = cached, bits 16..30 = cache index, low 16 = stamp
// code; "morphcache" decides the stamp variant (0x0045df30 / 0x00453360); the stamp (0x004ed0d0) goes into +0x410 (with
// a reference count kept in the cache Game+0xa84 +0xe60/+0xe64, "Error: Can't generate stamp code, no cache").
// With a matrix: its scale (0x004e3e90) is divided out (Matrix_Scale by 1/scale, Matrix_Multiply), flag 2 of the
// matrix word cleared, and the scale picks the nearest entry of the scale table (+0x414 +0x40 count, +0x48 vec3 list)
// into +0x388 when it was negative ("LoadInstances : Scale index out of range on object %s"); 0x004535d0 applies it.
// Without a matrix: identity. The body cinfo (frame +0x1d0, 0x00449510) is filled by virtual +0x1c; unless flagged
// (+0x424 bit 0x20000, cinfo +0x7a, scale table +0x4c bit 0, cinfo +0x7b, +0x45c <= 0) a collision shape is built:
// cinfo +0x7c set -> a box from the bounds virtual +0x98 (box 0x005120f0 with half extents, transform shape
// 0x00512ab0 to the box centre, phantom wrapper 0x004a4530 with 0x4000), else a sphere from virtual +0x90 (centre,
// radius; convex radius shape 0x0050f190 in the same wrappers, or, for an existing type-0x1a shape over a non-0x10
// child of type 5 (through 0x13 / 0x12 wrappers), the radius and centre set on it with 0x004a3d70 / 0x004a3d50). An
// existing cinfo shape and the new one are combined into a list shape (0x00511350). No shape: +0x424 |= 0x20000.
// The body is created (0x00457030) into +0x1c4; virtual +0x48(&r) true -> the action +0x1cc gets the body, (+0x45c *
// 0.5)^2 and r. Friction / restitution set (0x00458250 / 0x00458230), the transform applied (0x00457990, al = 1).
// "anim_rand" plays the default animation (virtual +0x40 index, 0x00408e30 flags 0x81, or 0xa01 at a random phase
// rand * 1/32768); "force_ground" drops the object to the ground (0x00455860(1000, 10000)); "fx_origin_start" and
// "fx_origin_start_lock" create effects (Fx_Create 0x0041cc60, flags 2 / 4, | 0x200 when +0x424 bit 0x10000; the
// lock variant with "fx_origin_start_lock_parent" / "_child" names unless "null"). Returns the last call's eax.
// Note: when "fx_origin_start_lock_child" is absent the original clears descriptor +0x74 instead of +0x18 (reproduced).
uint32_t Object_LoadProperties(uint8_t* self, uint8_t* instance, uint8_t* matrix, uint32_t props, uint32_t section)
{
    Frame<0x2a0> f;  // the original's frame (offsets from esp after the three register pushes)
    Field<uint32_t>(self, 0x400) = 0;
    Field<uint32_t>(self, 0x404) = 0;
    Field<uint32_t>(self, 0x408) = 0;
    Field<uint32_t>(self, 0x40c) = 0;
    std::memset(self + 0x34, 0, 0x62 * 4);
    std::memset(self + 0x248, 0, 0x20 * 4);
    const uint32_t block = Alloc(0x10, 0x18, 2, 0x0058b5e8);  // "new"
    uint32_t value = 0;
    if (block)
        value = orig::fn_004e7010(Field<uint32_t>(g_game, 0xac4), 0, block);
    Field<uint32_t>(self, 0x2d8) = value;
    orig::fn_004546d0(Field<uint32_t>(Field<uint8_t*>(g_game, 0xa84), 0xe70), u(self));
    Field<uint32_t>(self, 0x424) |= 4;
    VCall<void>(self, 0xf4 / 4, 1u);
    orig::fn_004540a0(u(self));
    Field<uint32_t>(self, 0x37c) = 0x3f800000;
    Field<uint32_t>(self, 0x380) = 0x3f800000;
    Field<uint32_t>(self, 0x384) = 0x3f800000;
    Field<uint32_t>(self, 0x378) = 0x3f800000;
    Field<uint32_t>(self, 0x458) = 0x42c80000;  // 100.0
    Field<uint32_t>(self, 0x454) = 0x42c80000;
    f.L(0x90) = 0x3f800000;  // density 1.0
    f.L(0x94) = 0x3f000000;  // restitution 0.5
    f.L(0x98) = 0;           // friction 0
    orig::fn_004533a0(0x005baaf0, f.A(0x50), u(self), 0x005b92ec, props, section, 0);  // "default", "material"
    orig::fn_00412140(f.A(0x50), f.A(0x90), 1);
    orig::fn_004534a0(section, f.A(0x98), u(self), 0x005b67ec, props, f.L(0x98), 0);  // "friction"
    orig::fn_004534a0(section, f.A(0x94), u(self), 0x005b0d68, props, f.L(0x94), 0);  // "restitution"
    orig::fn_004534a0(section, f.A(0x90), u(self), 0x005b0d60, props, f.L(0x90), 0);  // "density"
    Field<uint32_t>(self, 0x38c) = f.L(0x90);
    orig::fn_00451360(section, u(self), props);
    orig::fn_004087d0(u(self) + 0x248, u(self));
    Field<uint32_t>(self, 0x2cc) = 0;
    Field<uint32_t>(self, 0x2c8) = 0;
    Field<uint32_t>(self, 0x2d0) = 0;
    const uint32_t code = Field<uint32_t>(instance, 8);
    if (code)
    {
        f.C(0x16) = 0;
        f.C(0x17) = static_cast<uint8_t>(code >> 31);
        f.L(0x18) = code & 0xffff;
        f.L(0x1c) = (code >> 16) & 0x7fff;
        if (!static_cast<uint8_t>(
                orig::fn_0045df30(props, 0x005b0d54, section, Field<uint32_t>(g_game, 0xa84), f.A(0x16))))
            orig::fn_00453360(u(self), 0x005b0d54, f.A(0x16));  // "morphcache"
        if (f.C(0x17))
        {
            const uint32_t index = f.L(0x1c);
            Field<uint32_t>(self, 0x410) = Field<uint32_t*>(Field<uint8_t*>(g_game, 0xa84), 0xe60)[index];
            Field<uint32_t>(self, 0x1d0) = Field<uint32_t*>(Field<uint8_t*>(g_game, 0xa84), 0xe64)[index];
            if (!Field<uint32_t>(self, 0x410))
            {
                const uint32_t stamp = orig::fn_004ed0d0(f.L(0x18), 0, 0xd);
                Field<uint32_t*>(Field<uint8_t*>(g_game, 0xa84), 0xe60)[f.L(0x1c)] = stamp;
                uint8_t* s = B(Field<uint32_t*>(Field<uint8_t*>(g_game, 0xa84), 0xe60)[f.L(0x1c)]);
                Field<uint8_t*>(self, 0x410) = s;
                uint32_t refs;
                if (s)
                {
                    if (++Field<int32_t>(s, 8) == 0)
                        Field<int32_t>(s, 8) = 1;
                    refs = Field<uint32_t>(s, 8);
                }
                else
                {
                    GameLog(reinterpret_cast<const char*>(0x0058af8c));
                    refs = 0;
                }
                Field<uint32_t*>(Field<uint8_t*>(g_game, 0xa84), 0xe64)[f.L(0x1c)] = refs;
                Field<uint32_t>(self, 0x1d0) = Field<uint32_t*>(Field<uint8_t*>(g_game, 0xa84), 0xe64)[f.L(0x1c)];
            }
        }
        else
        {
            const uint32_t stamp = orig::fn_004ed0d0(f.L(0x18), 0, f.C(0x16) ? 0x1fu : 0xdu);
            Field<uint32_t>(self, 0x410) = stamp;
            if (stamp)
                *reinterpret_cast<uint32_t*>(stamp) = u(self);
        }
    }

    if (matrix)
    {
        orig::fn_004e3e90(u(matrix), f.A(0x20));
        f.F(0x44) = K(0x005bcb2c) / f.F(0x20);
        f.F(0x48) = K(0x005bcb2c) / f.F(0x24);
        f.F(0x4c) = K(0x005bcb2c) / f.F(0x28);
        orig::fn_004e2d90(f.A(0xd0), f.A(0x44), 0, 0, 0);  // Matrix_Scale reads only edx / esi
        orig::fn_004e2910(f.A(0xd0), f.A(0x150), u(matrix));
        f.L(0x190) &= ~2u;
        f.L(0x38) = 0x3f800000;
        f.L(0x34) = 0x3f800000;
        f.L(0x30) = 0x3f800000;
        if (Field<int32_t>(self, 0x388) < 0)
        {
            uint8_t* table = Field<uint8_t*>(self, 0x414);
            if (Field<uint32_t>(table, 0x40))
            {
                const float x = K(0x005bcb2c) - f.F(0x20);
                const float y = K(0x005bcb2c) - f.F(0x24);
                const float z = K(0x005bcb2c) - f.F(0x28);
                f.F(0x1c) = std::sqrt((x * x + z * z) + y * y);
                for (uint32_t i = 0; i < Field<uint32_t>(Field<uint8_t*>(self, 0x414), 0x40); ++i)
                {
                    const float* t = reinterpret_cast<const float*>(Field<uint8_t*>(table, 0x48) + i * 0xc);
                    const float dx = t[0] - f.F(0x20);
                    const float dy = t[1] - f.F(0x24);
                    const float dz = t[2] - f.F(0x28);
                    const float d = std::sqrt((dx * dx + dz * dz) + dy * dy);
                    if (f.F(0x1c) > d)
                    {
                        f.F(0x1c) = d;
                        f.F(0x30) = t[0];
                        f.F(0x34) = t[1];
                        f.F(0x38) = t[2];
                        Field<uint32_t>(self, 0x388) = i;
                    }
                }
            }
        }
        const int32_t sel = Field<int32_t>(self, 0x388);
        bool applied = false;
        if (sel >= 0)
        {
            uint8_t* table = Field<uint8_t*>(self, 0x414);
            if (sel < Field<int32_t>(table, 0x40))
            {
                const uint32_t* t = Field<uint32_t*>(table, 0x48) + sel * 3;
                f.L(0x34) = t[1];
                f.L(0x38) = t[2];
                f.L(0x30) = t[0];
                orig::fn_004535d0(f.A(0x30), u(self));
                applied = true;
            }
            else
            {
                const uint32_t name = orig::fn_00453270(u(self));
                orig::fn_00407ff0(Field<uint32_t>(g_game, 0x34), 0x005b0d1c, name, 0);
            }
        }
        if (!applied)
            orig::fn_004535d0(f.A(0x30), u(self));
    }
    else
    {
        std::memset(f.b + 0x150, 0, 0x44);
        f.L(0x150) = 0x3f800000;
        f.L(0x164) = 0x3f800000;
        f.L(0x178) = 0x3f800000;
        f.L(0x18c) = 0x3f800000;
    }

    orig::fn_00449510(f.A(0x1d0));
    VCall<void>(self, 0x1c / 4, f.A(0x1d0));
    if (Field<uint32_t>(self, 0x424) & 0x20000)
    {
        f.C(0x24a) = 1;
        goto shapeDone;
    }
    if (f.C(0x24a) || (Field<uint8_t*>(self, 0x414)[0x4c] & 1))
        goto shapeDone;
    if (f.C(0x24b) || !(Field<float>(self, 0x45c) > K(0x0056c05c)))
        goto flagsDone;
    if (f.C(0x24c))
    {
        // Box from the object's bounds.
        VCall<void>(self, 0x98 / 4, f.A(0x44), f.A(0x80), 0u);
        f.F(0x30) = f.F(0x80) - f.F(0x44);
        f.F(0x34) = f.F(0x84) - f.F(0x48);
        f.F(0x38) = f.F(0x88) - f.F(0x4c);
        if (!(f.F(0x30) > K(0x0056c05c)) || !(f.F(0x34) > K(0x0056c05c)) || !(f.F(0x38) > K(0x0056c05c)))
            goto shapeDone;
        f.L(0x5c) = 0;
        f.F(0x50) = f.F(0x30) * K(0x005bcb4c);
        f.F(0x54) = f.F(0x34) * K(0x005bcb4c);
        f.F(0x58) = f.F(0x38) * K(0x005bcb4c);
        uint8_t* memory = HkAlloc(0x20, 0x1d);
        Field<uint16_t>(memory, 4) = 0x20;
        uint8_t* box = B(orig::fn_005120f0(u(memory), f.A(0x50)));
        Field<uint32_t>(box, 8) = 0xfffffffe;
        memory = HkAlloc(0x50, 0x1d);
        uint8_t* transform = memory ? B(orig::fn_00512ab0(u(memory), u(box))) : nullptr;
        Field<uint32_t>(transform, 8) = 0xfffffffe;
        HkRelease(box);
        memory = HkAlloc(0x18, 0x1d);
        uint8_t* phantom = memory ? B(orig::fn_004a4530(u(memory), u(transform), 0x4000, CollisionInput4())) : nullptr;
        Field<uint32_t>(phantom, 8) = 0xfffffffe;
        HkRelease(transform);
        f.L(0x1c) = u(phantom);
        f.L(0x3c) = f.L(0x5c);
        f.L(0x20) = 0;
        f.L(0x24) = 0;
        f.F(0x30) = f.F(0x50) + f.F(0x44);
        f.L(0x28) = 0;
        f.L(0x2c) = 0x3f800000;
        f.F(0x34) = f.F(0x48) + f.F(0x54);
        f.F(0x38) = f.F(0x4c) + f.F(0x58);
        orig::fn_004de840(f.A(0x20), f.A(0xd0));
        f.L(0x104) = f.L(0x34);
        f.L(0x100) = f.L(0x30);
        f.L(0x108) = f.L(0x38);
        f.L(0x10c) = f.L(0x3c);
        orig::fn_00414240(u(transform) + 0x10, f.A(0xd0));
        if (!f.L(0x244))
        {
            f.L(0x244) = u(phantom);
            goto shapeDone;
        }
        // Combine the existing shape and the new one into a list shape (hkArray at frame +0x20).
        f.L(0x20) = 0;
        f.L(0x24) = 0;
        f.L(0x28) = 0x80000000;
        orig::fn_004aa830(f.A(0x20), 1, 4);
        uint32_t size = f.L(0x24);
        uint32_t* data = reinterpret_cast<uint32_t*>(f.L(0x20));
        data[size] = f.L(0x244);
        ++size;
        f.L(0x24) = size;
        if (size == (f.L(0x28) & 0x7fffffff))
        {
            orig::fn_004aa830(f.A(0x20), size ? size * 2 : 1, 4);
            size = f.L(0x24);
            data = reinterpret_cast<uint32_t*>(f.L(0x20));
        }
        data[size] = f.L(0x1c);
        memory = HkAlloc(0x28, 0x1d);
        ++size;
        Field<uint16_t>(memory, 4) = 0x28;
        uint8_t* list = B(orig::fn_00511350(u(memory), u(data), size));
        f.L(0x1c) = u(list);
        Field<uint32_t>(list, 8) = 0xfffffffc;
        for (f.L(0x18) = 0; static_cast<int32_t>(f.L(0x18)) < static_cast<int32_t>(size); ++f.L(0x18))
            HkRelease(B(data[f.L(0x18)]));
        f.L(0x244) = f.L(0x1c);
        if (static_cast<int32_t>(f.L(0x28)) >= 0)
            HkFree(u(data), (f.L(0x28) & 0x7fffffff) * 4, 0x10);
        goto shapeDone;
    }
    // Sphere from the object's centre and radius.
    VCall<void>(self, 0x90 / 4, f.A(0x30), f.A(0x1c), 0u);
    if (!(f.F(0x1c) > K(0x0056c05c)))
        goto shapeDone;
    {
        uint8_t* existing = B(f.L(0x244));
        if (existing && VCall<int32_t>(existing, 1) == 0x1a)
        {
            existing = Field<uint8_t*>(existing, 0xc);
            if (VCall<int32_t>(existing, 1) != 0x10)
            {
                // Reuse: a type-0x1a wrapper over a convex shape of type 5 gets the new radius and centre.
                if (Field<uint32_t>(existing, 8) != 0xffffffff)
                    goto shapeDone;
                if (VCall<int32_t>(existing, 1) == 0x13)
                    existing = Field<uint8_t*>(existing, 0xc);
                VCall<int32_t>(existing, 1);
                if (VCall<int32_t>(existing, 1) == 0x12)
                    existing = Field<uint8_t*>(existing, 0xc);
                if (VCall<int32_t>(existing, 1) != 5)
                    goto shapeDone;
                orig::fn_004a3d70(u(existing), f.L(0x1c));
                const float z = f.F(0x38) - Field<float>(existing, 0xc);
                f.L(0x54) = f.L(0x34);
                f.F(0x38) = z;
                f.L(0x50) = f.L(0x30);
                f.F(0x58) = z;
                f.L(0x5c) = 0;
                orig::fn_004a3d50(f.A(0x50), u(existing));  // ecx = the shape (left by 0x004a3d70)
                goto shapeDone;
            }
        }
        uint8_t* memory = HkAlloc(0x10, 0x1d);
        Field<uint16_t>(memory, 4) = 0x10;
        uint8_t* sphere = B(orig::fn_0050f190(u(memory), f.L(0x1c)));
        Field<uint32_t>(sphere, 8) = 0xfffffffe;
        memory = HkAlloc(0x50, 0x1d);
        uint8_t* transform = memory ? B(orig::fn_00512ab0(u(memory), u(sphere))) : nullptr;
        Field<uint32_t>(transform, 8) = 0xfffffffe;
        HkRelease(sphere);
        memory = HkAlloc(0x18, 0x1d);
        uint8_t* phantom = memory ? B(orig::fn_004a4530(u(memory), u(transform), 0x4000, CollisionInput4())) : nullptr;
        Field<uint32_t>(phantom, 8) = 0xfffffffe;
        HkRelease(transform);
        f.L(0x18) = u(phantom);
        const uint32_t cy = f.L(0x34);
        f.L(0x58) = f.L(0x38);
        f.L(0x50) = f.L(0x30);
        f.L(0x54) = cy;
        f.L(0x80) = 0;
        f.L(0x84) = 0;
        f.L(0x88) = 0;
        f.L(0x8c) = 0x3f800000;
        orig::fn_004de840(f.A(0x80), f.A(0xd0));
        f.L(0x100) = f.L(0x50);
        f.L(0x104) = cy;
        f.L(0x108) = f.L(0x58);
        f.L(0x10c) = 0;
        orig::fn_00414240(u(transform) + 0x10, f.A(0xd0));
        if (!f.L(0x244))
        {
            f.L(0x244) = u(phantom);
            goto shapeDone;
        }
        f.L(0x20) = 0;
        f.L(0x24) = 0;
        f.L(0x28) = 0x80000000;
        orig::fn_004aa830(f.A(0x20), 1, 4);
        uint32_t size = f.L(0x24);
        uint32_t* data = reinterpret_cast<uint32_t*>(f.L(0x20));
        data[size] = f.L(0x244);
        ++size;
        f.L(0x24) = size;
        if (size == (f.L(0x28) & 0x7fffffff))
        {
            orig::fn_004aa830(f.A(0x20), size ? size * 2 : 1, 4);
            size = f.L(0x24);
            data = reinterpret_cast<uint32_t*>(f.L(0x20));
        }
        data[size] = f.L(0x18);
        memory = HkAlloc(0x28, 0x1d);
        ++size;
        Field<uint16_t>(memory, 4) = 0x28;
        uint8_t* list = B(orig::fn_00511350(u(memory), u(data), size));
        f.L(0x40) = u(list);
        Field<uint32_t>(list, 8) = 0xfffffffc;
        for (f.L(0x18) = 0; static_cast<int32_t>(f.L(0x18)) < static_cast<int32_t>(size); ++f.L(0x18))
            HkRelease(B(data[f.L(0x18)]));
        f.L(0x244) = f.L(0x40);
        if (static_cast<int32_t>(f.L(0x28)) >= 0)
            HkFree(u(data), (f.L(0x28) & 0x7fffffff) * 4, 0x10);
    }
shapeDone:
    if (!f.L(0x244))
    {
        f.C(0x24a) = 1;
        Field<uint32_t>(self, 0x424) |= 0x20000;
    }
    else if (f.C(0x24a))
        Field<uint32_t>(self, 0x424) |= 0x20000;
flagsDone:
    if ((Field<uint32_t>(self, 0x424) & 0x100000) && (Field<uint8_t*>(self, 0x414)[0x4c] & 1))
        f.C(0x1e1) = 1;
    std::memcpy(f.b + 0x250, f.b + 0x150, 0x44);
    Field<uint32_t>(self, 0x1c4) = Body_Create(Field<uint32_t>(g_game, 0xa8c), self, f.b + 0x1d0);
    f.L(0x7c) = 0;
    if (VCall<uint8_t>(self, 0x48 / 4, f.A(0x7c)))
    {
        Field<uint32_t>(Field<uint8_t*>(self, 0x1cc), 0x1c) = Field<uint32_t>(self, 0x1c4);
        const float h = Field<float>(self, 0x45c) * K(0x005bcb4c);
        Field<float>(Field<uint8_t*>(self, 0x1cc), 0x10) = h * h;
        Field<uint32_t>(Field<uint8_t*>(self, 0x1cc), 0x24) = f.L(0x7c);
    }
    Body_SetFriction(self, f.L(0x98));
    Body_SetRestitution(self, f.L(0x94));
    Object_SetTransform((u(self) & 0xffffff00u) | 1, self, reinterpret_cast<const float*>(f.b + 0x150));
    orig::fn_004534f0(props & 0xffffff00u, section, f.A(0x17), u(self), 0x005b0d10, props, 0);  // "anim_rand"
    uint32_t flags;
    uint32_t phase = 0;
    uint32_t index;
    if (f.C(0x17))
    {
        const int32_t r = static_cast<int32_t>(orig::fn_005391cd());
        f.F(0x40) = static_cast<float>(r) * K(0x005bcbc0);  // 1/32768
        phase = f.L(0x40);
        index = VCall<uint32_t>(self, 0x40 / 4);
        flags = 0xa01;
    }
    else
    {
        index = VCall<uint32_t>(self, 0x40 / 4);
        flags = 0x81;
    }
    // Virtual +0x40 is a plain-ret getter: the two pushes before it are 0x00408e30's last arguments.
    orig::fn_00408e30(flags, u(self), index, 0, 0, 0, phase, 0);
    orig::fn_00453400(0, section, f.A(0x40), u(self), 0x005b0d00, props, 0);  // "force_ground"
    if (f.L(0x40))
        Object_ForceGround(self, 1000.0f, 10000.0f);
    uint32_t result = orig::fn_004533a0(0x005bba94, f.A(0xb0), u(self), 0x005b0cf0, props, section, 0);
    if (result && f.C(0xb0))  // "fx_origin_start"
    {
        std::memset(f.b + 0xd0, 0, 0x80);
        std::memcpy(f.b + 0x100, f.b + 0x150, 0x44);
        f.L(0x144) = 0;
        f.L(0xec) = u(self);
        f.L(0xd0) = (Field<uint32_t>(self, 0x424) & 0x10000) ? 0x202u : 2u;
        result = orig::fn_0041cc60(f.A(0xd0) & 0xffffff00u, Field<uint32_t>(g_game, 0xa90), f.A(0xb0), f.A(0xd0));
    }
    else if (result)
        result &= 0xffffff00u;
    result = orig::fn_004533a0(0x005bba94, f.A(0xb0), u(self), 0x005b0cd8, props, section, 0);
    if (!result)
        return 0;
    if (!f.C(0xb0))  // "fx_origin_start_lock"
        return result & 0xffffff00u;
    std::memset(f.b + 0x150, 0, 0x80);
    f.L(0x16c) = u(self);
    uint32_t found = orig::fn_004533a0(0x005bba94, f.A(0x90), u(self), 0x005b0cbc, props, section, 0);
    if (found && f.C(0x90))  // "fx_origin_start_lock_parent" unless "null"
        f.L(0x168) = orig::fn_005448ee(0, f.A(0x90), 0x005bba94) ? f.A(0x90) : 0;
    else
        f.L(0x168) = 0;
    found = orig::fn_004533a0(0x005bba94, f.A(0x50), u(self), 0x005b0ca0, props, section, 0);
    if (found && f.C(0x50))  // "fx_origin_start_lock_child" unless "null"
        f.L(0x168) = orig::fn_005448ee(0, f.A(0x50), 0x005bba94) ? f.A(0x50) : 0;
    else
        f.L(0x1c4) = 0;
    f.L(0x150) = (Field<uint32_t>(self, 0x424) & 0x10000) ? 0x204u : 4u;
    return orig::fn_0041cc60(f.A(0xb0) & 0xffffff00u, Field<uint32_t>(g_game, 0xa90), f.A(0xb0), f.A(0x150));
}

static uint32_t Object_LoadProperties_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return Object_LoadProperties(B(ecx), B(a0), B(a1), a2, a3);
}
ABI_ENTRY_004517b0(Object_LoadProperties_Entry, Object_LoadProperties_Abi)
HOOK(0x004517b0, "Object_LoadProperties", Object_LoadProperties_Entry, nullptr);

// 0x004527d0 (ecx = object; stack: cinfo; callee pops 4; callers: object loaders 0x00405880, 0x00419020, 0x0044e100
// ...; string "collision_radius"). Builds a body from a sphere. With scale-table flag 4 (+0x414 +0x4c) and
// 0x004531e0 true: +0x1bc = the ragdoll bodies (0x00456a90(Game+0xa8c, 1, cinfo +0x12)). With flag 1: compound shape
// 0x00456c40(Game+0xa8c, object, 0, !(flags >> 1) & 1, 0, cinfo) and straight to the wrappers below. Otherwise the
// radius +0x45c = cinfo +4 when > 0 (centre 0), else virtual +0x90 (centre, radius) when cinfo +0xc, else
// "collision_radius" or, when absent, (virtual +0x90 radius + virtual +0x94 value) * 0.5. Mass = sum of the
// ragdoll body masses (+0x1bc records) or radius^3 * density (+0x38c) * 4.18879 (4/3 pi), at least 0.05. With radius
// > 0: a game sphere shape (0x004a3c50, 0x30 bytes; cinfo +8 > 0 scales its +0x20 by radius, 0x004a3d70), its mass
// properties (0x004a46c0(radius, 1.0), 0x004a6950 mass) combined (0x004a5ed0) into cinfo +0x20 with a transform at
// the centre (cinfo +0xc) or (0, 0, radius); cinfo +0x70 = 2. Virtual +0x48(0) true: a 0x5b0ad8 wrapper (0x004563d0,
// +0x20 = 1, +0x14 = radius + 0.3) into cinfo +0x74 and the action +0x1cc. Virtual +0x4c true: a multi-sphere
// (0x00512cc0, centre (0, 0, -(radius + 0.1))) listed with the shape (0x00511350). The result is wrapped in a transform
// shape (0x00512ab0). Then cinfo +0x78 adds a 0x5b0af8 pair shape over a new game shape (0x00411590), cinfo +0x79 a
// phantom wrapper (0x004a4530, edx = cinfo +0; +8 = -3 when +0x78 else -1); cinfo +0 = +0x79 ? 0 : 0x4000. Returns
// 0x00458dd0(object, 0, 0, 0).
// Enhancement point (hook/enhance/persistence.cpp): true for an object that should collide by its model's shape (the
// flag-1 compound path) instead of one bounding sphere. Kept debris planks rested on a sphere half their length and
// hovered (the player, 2026-10-12). Null = original.
bool (*g_meshCollision)(void* object) = nullptr;

uint32_t Object_BuildRadiusCollision(uint8_t* obj, uint8_t* desc)
{
    Frame<0x190> f;  // the original's frame (offsets from esp after the three register pushes)
    f.L(0x18) = u(obj);
    if ((Field<uint8_t*>(obj, 0x414)[0x4c] & 4) && static_cast<uint8_t>(orig::fn_004531e0(u(obj))))
        Field<uint32_t>(obj, 0x1bc) = fn_00456a90(obj, Field<uint32_t>(g_game, 0xa8c), 1, desc[0x12]);
    uint32_t tableFlags = Field<uint32_t>(Field<uint8_t*>(obj, 0x414), 0x4c);
    if (!(tableFlags & 1) && g_meshCollision && g_meshCollision(obj))
        tableFlags |= 1;
    if (tableFlags & 1)
    {
        const uint32_t shifted = tableFlags >> 1;
        fn_00456c40(Field<uint32_t>(g_game, 0xa8c), obj, nullptr, (shifted & 0xffffff00u) | (~shifted & 1u), 0, desc);
    }
    else
    {
        float* radius = &Field<float>(obj, 0x45c);
        if (Field<float>(desc, 4) > K(0x0056c05c))
        {
            *radius = Field<float>(desc, 4);
            f.L(0x4c) = 0;
            f.L(0x48) = 0;
            f.L(0x44) = 0;
        }
        else if (desc[0xc])
            VCall<void>(obj, 0x90 / 4, f.A(0x44), u(radius), 0u);
        else if (!orig::fn_004534a0(0, u(radius), u(obj), 0x005b65ec, 0, 0, 0))  // "collision_radius"
        {
            VCall<void>(obj, 0x90 / 4, f.A(0x44), f.A(0x30), 0u);
            VCall<void>(obj, 0x94 / 4, f.A(0x44), f.A(0x1c), 0u);
            *radius = (f.F(0x1c) + f.F(0x30)) * K(0x005bcb4c);
        }
        float mass;
        uint8_t* record = Field<uint8_t*>(obj, 0x1bc);
        if (record)
        {
            mass = K(0x0056c05c);
            if (!Field<uint32_t>(record, 0))
                goto minimumMass;
            do
            {
                mass = mass + MassOf(Field<uint8_t*>(record, 0));
                record += 0x4c;
            } while (Field<uint32_t>(record, 0));
        }
        else
        {
            const float r = *radius;
            float t = r * Field<float>(obj, 0x38c);
            t = r * t;
            t = t * r;
            mass = t * K(0x005bcfc8);  // 4/3 pi
        }
        f.F(0x1c) = mass;
        if (mass < K(0x005bcc34))
        minimumMass:
            f.L(0x1c) = 0x3d4ccccd;  // 0.05
        if (*radius > K(0x0056c05c))
        {
            if (desc[0xc])
            {
                f.L(0x20) = f.L(0x44);
                f.L(0x24) = f.L(0x48);
                f.L(0x28) = f.L(0x4c);
            }
            else
            {
                f.L(0x20) = 0;
                f.L(0x24) = 0;
                f.F(0x28) = *radius;
            }
            f.L(0x30) = 0;
            f.L(0x34) = 0;
            f.L(0x38) = 0;
            f.L(0x3c) = 0x3f800000;
            orig::fn_004de840(f.A(0x30), f.A(0xc0));
            Field<uint32_t>(desc, 0x70) = 2;
            f.L(0xf0) = f.L(0x20);
            f.L(0xf4) = f.L(0x24);
            f.L(0xf8) = f.L(0x28);
            f.L(0xfc) = 0;
            uint8_t* memory = HkAlloc(0x30, 0x1d);
            Field<uint16_t>(memory, 4) = 0x30;
            uint8_t* sphere = B(orig::fn_004a3c50(u(memory), fb(*radius)));
            Field<uint32_t>(sphere, 8) = 0xffffffff;
            f.L(0x30) = u(sphere);
            if (Field<float>(desc, 8) > K(0x0056c05c))
                orig::fn_004a3d70(u(sphere), fb(Field<float>(desc, 8) * *radius));
            f.L(0x50) = 0;
            f.L(0x54) = 0;
            std::memset(f.b + 0x60, 0, 0x40);
            orig::fn_004a46c0(f.A(0x50), fb(*radius), 0x3f800000);
            orig::fn_004a6950(f.A(0x50), f.L(0x1c));
            f.L(0x20) = 0;
            f.L(0x24) = 0;
            f.L(0x28) = 0x80000000;
            orig::fn_004142b0(f.A(0x100));
            orig::fn_00413770(f.A(0x100), f.A(0x50));
            orig::fn_00414240(f.A(0x150), f.A(0xc0));
            orig::fn_004aa830(f.A(0x20), 1, 0x90);
            const uint32_t entry = f.L(0x20) + f.L(0x24) * 0x90;
            ++f.L(0x24);
            orig::fn_00413770(entry, f.A(0x100));
            orig::fn_00414240(entry + 0x50, f.A(0x150));
            if (f.L(0x24))
                orig::fn_004a5ed0(u(desc) + 0x20, f.A(0x20));
            else
            {
                orig::fn_004143f0(f.A(0x50));
                orig::fn_00413770(u(desc) + 0x20, f.A(0x50));
            }
            Field<uint32_t>(desc, 0x74) = f.L(0x30);
            f.F(0x30) = *radius + K(0x005bccd4);  // + 0.3
            f.F(0x1c) = *radius + K(0x005bcbfc);  // + 0.1
            if (VCall<uint8_t>(obj, 0x48 / 4, 0u))
            {
                memory = HkAlloc(0x28, 0x1d);
                Field<uint16_t>(memory, 4) = 0x28;
                uint8_t* wrapper = B(fn_004563d0(Field<uint8_t*>(desc, 0x74), memory));
                Field<uint32_t>(wrapper, 8) = 0xffffffff;
                wrapper[0x20] = 1;
                HkRelease(Field<uint8_t*>(desc, 0x74));
                Field<uint8_t*>(desc, 0x74) = wrapper;
                Field<uint8_t*>(obj, 0x1cc) = wrapper;
                Field<uint32_t>(wrapper, 0x14) = f.L(0x30);
            }
            else
                Field<uint32_t>(obj, 0x1cc) = 0;
            if (VCall<uint8_t>(obj, 0x4c / 4))
            {
                f.L(0xa0) = 0;
                f.L(0xa4) = 0;
                f.L(0xa8) = 0;
                f.L(0xac) = 0;
                f.L(0xb0) = 0;
                f.L(0xb4) = 0;
                f.F(0xb8) = -f.F(0x1c);
                f.L(0xbc) = 0;
                memory = HkAlloc(0xa0, 0x1d);
                Field<uint16_t>(memory, 4) = 0xa0;
                uint8_t* spheres = B(orig::fn_00512cc0(u(memory), f.A(0xa0), 1));
                Field<uint32_t>(spheres, 8) = 0xffffffff;
                f.L(0x30) = u(spheres);
                f.L(0x34) = Field<uint32_t>(desc, 0x74);
                memory = HkAlloc(0x28, 0x1d);
                Field<uint16_t>(memory, 4) = 0x28;
                uint8_t* list = B(orig::fn_00511350(u(memory), f.A(0x30), 2));
                Field<uint32_t>(list, 8) = 0xffffffff;
                HkRelease(spheres);
                HkRelease(Field<uint8_t*>(desc, 0x74));
                Field<uint8_t*>(desc, 0x74) = list;
            }
            memory = HkAlloc(0x50, 0x1d);
            uint8_t* transform = memory ? B(orig::fn_00512ab0(u(memory), Field<uint32_t>(desc, 0x74))) : nullptr;
            Field<uint32_t>(transform, 8) = 0xffffffff;
            HkRelease(Field<uint8_t*>(desc, 0x74));
            orig::fn_00414240(u(transform) + 0x10, f.A(0xc0));
            Field<uint8_t*>(desc, 0x74) = transform;
            if (static_cast<int32_t>(f.L(0x28)) >= 0)
                HkFree(f.L(0x20), (f.L(0x28) & 0x7fffffff) * 0x90, 0x10);
        }
        obj = B(f.L(0x18));
    }
    if (Field<uint32_t>(desc, 0x74))
    {
        if (desc[0x78])
        {
            uint8_t* memory = HkAlloc(0xc, 0x1d);
            Field<uint16_t>(memory, 4) = 0xc;
            orig::fn_00411590(u(memory));
            uint8_t* gameShape = memory;
            uint8_t* pair = HkAlloc(0x14, 0x1d);
            Field<uint16_t>(pair, 4) = 0x14;
            Field<uint32_t>(pair, 0xc) = Field<uint32_t>(desc, 0x74);
            Field<uint16_t>(pair, 6) = 1;
            Field<uint32_t>(pair, 8) = 0;
            Field<uint32_t>(pair, 0) = 0x005b0af8;
            Field<uint8_t*>(pair, 0x10) = gameShape;
            ++Field<uint16_t>(gameShape, 6);
            ++Field<uint16_t>(Field<uint8_t*>(pair, 0xc), 6);
            Field<uint32_t>(pair, 8) = 0xfffffffd;
            HkRelease(gameShape);
            HkRelease(Field<uint8_t*>(desc, 0x74));
            obj = B(f.L(0x18));
            Field<uint8_t*>(desc, 0x74) = pair;
        }
        if (desc[0x79])
        {
            uint8_t* memory = HkAlloc(0x18, 0x1d);
            uint8_t* phantom = memory ? B(orig::fn_004a4530(u(memory), Field<uint32_t>(desc, 0x74),
                                                           Field<uint32_t>(desc, 0), CollisionInput4()))
                                      : nullptr;
            Field<int32_t>(phantom, 8) = desc[0x78] ? -3 : -1;
            HkRelease(Field<uint8_t*>(desc, 0x74));
            Field<uint8_t*>(desc, 0x74) = phantom;
        }
    }
    Field<uint32_t>(desc, 0) = desc[0x79] ? 0u : 0x4000u;
    return fn_00458dd0(obj, 0, 0, 0);
}

static uint32_t Object_BuildRadiusCollision_Abi(uint32_t ecx, uint32_t a0)
{
    return Object_BuildRadiusCollision(B(ecx), B(a0));
}
ABI_ENTRY_004527d0(Object_BuildRadiusCollision_Entry, Object_BuildRadiusCollision_Abi)
HOOK(0x004527d0, "Object_BuildRadiusCollision", Object_BuildRadiusCollision_Entry, nullptr);

// 0x00453750 (stack: object; callee pops 4; callers 0x004234d0, 0x00423680, 0x004540b0 = virtual +0x3c of the object
// vtables). guess: an attached object follows its parent (+0x34). Only with +0x424 bit 0. When the parent's virtual
// +0xd8 is false, or flag 0x20 (+0x38) is set and the parent is ragdolled (+0x421): flag 4 -> 0x00453b20(esi =
// object), else virtual +0xdc. Otherwise the parent is updated first (virtual +0x3c); then (same tests again) the
// parent matrix (virtual +0x70) times the attach point (0x00453c50(parent, +0xc4, ..., object +0x3c)) with the
// translation scaled by the parent's scale (0x004535b0), optionally relative to the object's own point +0x184
// (0x00453c50(object, +0x184, ..., +0xfc), Matrix_Inverse 0x004e2b40). Flag 8 sets only the position (0x00457870),
// else the transform (0x00457990, al = 0). Then flag 2 (+0x424): 0x00453b20 and clears it; else when the body is in a
// world, 0x004aafa0(eax = body). Returns the last call's eax.
uint32_t fn_00453750(uint8_t* obj)
{
    Frame<0x170> f;  // offsets from esp after the pushes of ebp, esi and edi (the original's deepest frame)
    if (!(obj[0x424] & 1))
        return 0;
    uint8_t* parent = Field<uint8_t*>(obj, 0x34);
    if (!VCall<uint8_t>(parent, 0xd8 / 4) || ((obj[0x38] & 0x20) && Field<uint8_t*>(obj, 0x34)[0x421]))
    {
        if (obj[0x38] & 4)
            return orig::fn_00453b20(u(obj));
        return VCall<uint32_t>(obj, 0xdc / 4);
    }
    VCall<void>(Field<uint8_t*>(obj, 0x34), 0x3c / 4);
    if (!(obj[0x424] & 1))
        return 0;
    if (!VCall<uint8_t>(Field<uint8_t*>(obj, 0x34), 0xd8 / 4) ||
        ((obj[0x38] & 0x20) && Field<uint8_t*>(obj, 0x34)[0x421]))
    {
        if (obj[0x38] & 4)
            return orig::fn_00453b20(u(obj));
        return VCall<uint32_t>(obj, 0xdc / 4);
    }
    VCall<void>(Field<uint8_t*>(obj, 0x34), 0x70 / 4, f.A(0xe4));
    parent = Field<uint8_t*>(obj, 0x34);
    orig::fn_004535b0(f.A(0xc), u(parent));
    orig::fn_00453c50(u(parent), Field<uint32_t>(obj, 0xc4), f.A(0xa0), 0, u(obj) + 0x3c, 0, 0, 0, 0);
    const float tx = f.F(0xac) * f.F(0xc);
    std::memcpy(f.b + 0x18, f.b + 0xa0, 0x44);
    f.F(0x24) = tx;
    f.F(0x34) = f.F(0x34) * f.F(0x10);
    f.F(0x44) = f.F(0x44) * f.F(0x14);
    orig::fn_004e2910(f.A(0x18), f.A(0x5c), f.A(0xe4));
    const uint32_t own = Field<uint32_t>(obj, 0x184);
    if (own)
    {
        orig::fn_00453c50(u(obj), own, f.A(0xa0), 0, u(obj) + 0xfc, 0, 0, 0, 0);
        orig::fn_004e2b40(f.A(0xa0), f.A(0x128));
        orig::fn_004e2910(f.A(0x128), f.A(0x18), f.A(0x5c));
        if (obj[0x38] & 8)
        {
            f.L(0xc) = f.L(0x24);
            f.L(0x10) = f.L(0x34);
            f.L(0x14) = f.L(0x44);
            Object_SetPosition(f.L(0x34) & 0xffffff00u, reinterpret_cast<const float*>(f.b + 0xc), obj);
        }
        else
            Object_SetTransform(0, obj, reinterpret_cast<const float*>(f.b + 0x18));
    }
    else if (obj[0x38] & 8)
    {
        f.L(0xc) = f.L(0x68);
        f.L(0x10) = f.L(0x78);
        f.L(0x14) = f.L(0x88);
        Object_SetPosition(f.L(0x68) & 0xffffff00u, reinterpret_cast<const float*>(f.b + 0xc), obj);
    }
    else
        Object_SetTransform(0, obj, reinterpret_cast<const float*>(f.b + 0x5c));
    if (Field<uint32_t>(obj, 0x424) & 2)
    {
        orig::fn_00453b20(u(obj));
        const uint32_t flags = Field<uint32_t>(obj, 0x424) & ~2u;
        Field<uint32_t>(obj, 0x424) = flags;
        return flags;
    }
    uint8_t* body = Field<uint8_t*>(obj, 0x1c4);
    if (Field<uint32_t>(body, 0x44))
        return orig::fn_004aafa0(u(body));
    return u(body);
}

static uint32_t fn_00453750_Abi(uint32_t a0)
{
    return fn_00453750(B(a0));
}
ABI_ENTRY_00453750(fn_00453750_Entry, fn_00453750_Abi)
HOOK(0x00453750, "fn_00453750", fn_00453750_Entry, nullptr);

// 0x00454940 (stack: context; caller pops; the per-contact callback that 0x00454b20 passes to Havok's for-each-contact
// 0x004ae6c0; no direct callers). The context's collidables +4 / +8 (entity +0x1c): the owner of the body that is
// context +0x418 (pair key 0, 0x00404510) is kept and the other body's owner looked up. For each contact point
// (context +0 virtual +0x18(context +0xc [i]), i < context +0x10) the normal (+0x10) is flipped to point up (z >= 0)
// and normalised; when 0x004548d0(eax = &normal; owner, filter [0x005fe5b4]) accepts it and it is the first or has a
// larger z than the best so far, the best is stored: [0x005fe5cc] other owner, [0x005fe5bc] material of the contact
// (context +0 virtual +0x14(...) +0x10 -> +0x10, or 0), [0x005fe5c0] normal, [0x005fe5b8] = 1. guess: ground contact.
uint32_t fn_00454940(uint8_t* ctx)
{
    Frame<0x40> f;  // offsets from esp after the pushes of ebx, esi and edi
    uint8_t* other = Field<uint8_t*>(Field<uint8_t*>(ctx, 4), 0x1c);
    uint32_t result;
    if (Field<uint8_t*>(ctx, 0x418) == other)
    {
        result = orig::fn_00404510(f.A(0x20), u(other), 0);
        f.L(0x18) = *reinterpret_cast<uint32_t*>(result);
        other = Field<uint8_t*>(Field<uint8_t*>(ctx, 8), 0x1c);
    }
    else
    {
        result = orig::fn_00404510(f.A(0x20), Field<uint32_t>(Field<uint8_t*>(ctx, 8), 0x1c), 0);
        f.L(0x18) = *reinterpret_cast<uint32_t*>(result);
    }
    if (!other)
        return result;
    f.L(0x1c) = *reinterpret_cast<uint32_t*>(orig::fn_00404510(f.A(0x20), u(other), 0));
    const uint32_t n = Field<uint32_t>(ctx, 0x10);
    f.L(0x20) = n;
    for (uint32_t i = 0; i < n; ++i)
    {
        const float* point = VCall<const float*>(Field<void*>(ctx, 0), 0x18 / 4, Field<uint32_t*>(ctx, 0xc)[i]);
        const float nx = point[4];
        const float ny = point[5];
        const float nz = point[6];
        if (!(nz >= K(0x0056c05c)))  // fcomp 0 / test ah, 1: flipped when below 0 (or NaN)
        {
            f.F(0x24) = -nx;
            f.F(0x28) = -ny;
            f.F(0x2c) = -nz;
        }
        else
        {
            f.F(0x24) = nx;
            f.F(0x28) = ny;
            f.F(0x2c) = nz;
        }
        const float length = std::sqrt((f.F(0x2c) * f.F(0x2c) + f.F(0x28) * f.F(0x28)) + f.F(0x24) * f.F(0x24));
        if (!(length == K(0x0056c05c)))
        {
            const float inv = K(0x005bcb2c) / length;
            f.F(0x24) = f.F(0x24) * inv;
            f.F(0x28) = f.F(0x28) * inv;
            f.F(0x2c) = inv * f.F(0x2c);
        }
        if (!static_cast<uint8_t>(orig::fn_004548d0(f.A(0x24), f.L(0x18), GameGlobal<uint32_t>(0x005fe5b4))))
            continue;
        if (GameGlobal<uint8_t>(0x005fe5b8) && !(f.F(0x2c) > GameGlobal<float>(0x005fe5c8)))
            continue;
        // The original searches the other entity's pair array for key 0 here and discards the index.
        for (int32_t k = 0; k < Field<int32_t>(other, 0x7c) && Field<uint32_t*>(other, 0x78)[k * 2] != 0; ++k)
        {
        }
        GameGlobal<uint32_t>(0x005fe5cc) = f.L(0x1c);
        uint8_t* contact = VCall<uint8_t*>(Field<void*>(ctx, 0), 0x14 / 4, Field<uint32_t*>(ctx, 0xc)[i]);
        uint8_t* material = Field<uint8_t*>(contact, 0x10);
        GameGlobal<uint32_t>(0x005fe5bc) = material ? Field<uint32_t>(material, 0x10) : 0;
        GameGlobal<uint32_t>(0x005fe5c0) = f.L(0x24);
        GameGlobal<uint32_t>(0x005fe5c4) = f.L(0x28);
        GameGlobal<uint32_t>(0x005fe5c8) = f.L(0x2c);
        GameGlobal<uint8_t>(0x005fe5b8) = 1;
    }
    return n;
}

static uint32_t fn_00454940_Abi(uint32_t a0)
{
    return fn_00454940(B(a0));
}
ABI_ENTRY_00454940(fn_00454940_Entry, fn_00454940_Abi)
HOOK(0x00454940, "fn_00454940", fn_00454940_Entry, nullptr);

// 0x00454b20 (ecx = object; stack: &owner, &normal, &material, filter; callee pops 0x10; caller 0x0045da90). Resets
// the best-contact state ([0x005fe5c0] = (0, 0, 1), [0x005fe5b4] = filter, [0x005fe5b8] = 0, [0x005fe5cc] = 0,
// [0x005fe5bc] = 0) and, when the body (+0x1c4) is in a world (+0x44), walks its contacts (0x004ae6c0(eax = body;
// 0x00454940)). When one was found: *owner, *normal (normalised, 0x004e7880) and *material. Returns the found flag in
// AL (EAX's upper bytes as the original leaves them).
uint32_t fn_00454b20(uint8_t* obj, uint32_t* owner, float* normal, uint32_t* material, uint32_t filter)
{
    GameGlobal<uint32_t>(0x005fe5c0) = 0;
    GameGlobal<uint32_t>(0x005fe5c4) = 0;
    GameGlobal<uint32_t>(0x005fe5c8) = 0x3f800000;
    GameGlobal<uint32_t>(0x005fe5b4) = filter;
    uint8_t* body = Field<uint8_t*>(obj, 0x1c4);
    GameGlobal<uint8_t>(0x005fe5b8) = 0;
    GameGlobal<uint32_t>(0x005fe5cc) = 0;
    GameGlobal<uint32_t>(0x005fe5bc) = 0;
    if (!Field<uint32_t>(body, 0x44))
        return u(body) & 0xffffff00u;
    const uint32_t r = orig::fn_004ae6c0(u(body), 0x00454940);
    const uint8_t found = GameGlobal<uint8_t>(0x005fe5b8);
    if (!found)
        return r & 0xffffff00u;
    *owner = GameGlobal<uint32_t>(0x005fe5cc);
    Vec3_Normalize(0x005fe5c0, u(normal));
    *material = GameGlobal<uint32_t>(0x005fe5bc);
    return (u(material) & 0xffffff00u) | found;
}

static uint32_t fn_00454b20_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_00454b20(B(ecx), reinterpret_cast<uint32_t*>(a0), reinterpret_cast<float*>(a1),
                       reinterpret_cast<uint32_t*>(a2), a3);
}
ABI_ENTRY_00454b20(fn_00454b20_Entry, fn_00454b20_Abi)
HOOK(0x00454b20, "fn_00454b20", fn_00454b20_Entry, nullptr);

// 0x00455720 (eax = collidable (0x20 bytes, shape at +0x10); stack: world; caller pops; caller 0x00455860). guess:
// penetration test. A list shape (type 0x10, virtual +0x20 count) is narrowed to its first child whose +8 word has
// the high half 0xffff and a low half other than 0xfffe (else the last child); such a shape replaces the copy's
// shape. A point collector (0xc0 bytes, class 0x18, 0x004562a0) is built against 0x00448460(Game+0xa60) + 0x14 with
// the world's collision input (+0xe8); its agent (+0xb0) runs virtual +0xc (closest points) and +0x10 (cleanup).
// Returns AL = 1 when the closest distance (+0xa0) is below the input's tolerance (+0xc).
uint8_t fn_00455720(const uint32_t* collidable, uint8_t* world)
{
    uint32_t copy[8];
    uint8_t* shape = B(collidable[4]);
    std::memcpy(copy, collidable, sizeof(copy));
    if (!shape)
        return 0;
    if (VCall<int32_t>(shape, 1) == 0x10)
    {
        uint8_t* list = shape;
        int32_t i = 0;
        if (VCall<int32_t>(list, 0x20 / 4) > 0)
        {
            for (;;)
            {
                shape = B(Field<uint32_t*>(list, 0xc)[i]);
                const uint32_t tag = Field<uint32_t>(shape, 8);
                if ((tag >> 16) == 0xffff && (tag & 0xffff) != 0xfffe)
                    break;
                ++i;
                if (!(i < VCall<int32_t>(list, 0x20 / 4)))
                    break;
            }
        }
    }
    const uint32_t tag = Field<uint32_t>(shape, 8);
    if ((tag >> 16) == 0xffff && (tag & 0xffff) != 0xfffe)
        copy[4] = u(shape);
    const uint32_t other = orig::fn_00448460(Field<uint32_t>(g_game, 0xa60)) + 0x14;
    uint8_t* input = Field<uint8_t*>(world, 0xe8);
    uint8_t* memory = HkAlloc(0xc0, 0x18);
    Field<uint16_t>(memory, 4) = 0xc0;
    uint8_t* collector = B(PointCollector_Construct(input, other, u(copy), memory));
    Field<uint32_t>(collector, 0xa4) = 0xffffffff;
    Field<uint32_t>(collector, 0xa0) = GameGlobal<uint32_t>(0x00588dcc);  // FLT_MAX
    Field<uint32_t>(collector, 0x14) = 0;
    void* agent = Field<void*>(collector, 0xb0);
    if (agent)
        VCall<void>(agent, 0xc / 4, Field<uint32_t>(collector, 0xa8), Field<uint32_t>(collector, 0xac), u(input),
                    u(collector));
    const uint8_t hit = Field<float>(collector, 0xa0) < Field<float>(input, 0xc) ? 1 : 0;
    agent = Field<void*>(collector, 0xb0);
    if (agent)
    {
        VCall<void>(agent, 0x10 / 4, u(input));
        Field<uint32_t>(collector, 0xb0) = 0;
    }
    VCall<void>(collector, 0, 1u);
    return hit;
}

static uint32_t fn_00455720_Abi(uint32_t eax, uint32_t a0)
{
    return fn_00455720(reinterpret_cast<const uint32_t*>(eax), B(a0));
}
ABI_ENTRY_00455720(fn_00455720_Entry, fn_00455720_Abi)
HOOK(0x00455720, "fn_00455720", fn_00455720_Entry, nullptr);

// 0x00455860 (edi = object; stack: height, range (floats); callee pops 8; callers: 0x004517b0 for "force_ground"
// with (1000, 10000), and 11 others). Drops the object onto the ground: position (virtual +0x54) raised by height,
// ray cast down (0x004137e0, direction (0, 0, -1), length range, hit distance into the height slot); no hit returns
// 0. Moves the object to the hit + 0.1 (0x00457870, al = 0); then while the body's collidable (+0x1c4 +0x14)
// penetrates (0x00455720) it is bisected vertically by the bounding radius (virtual +0x90 into the range slot, halved
// at each change of state) until more than 10 changes, and finally put at the last free height + 0.1. Note: when the
// state never changes the loop does not end (reproduced). Returns 1.
uint32_t Object_ForceGround(uint8_t* obj, float height, float range)
{
    Frame<0x20> f;  // offsets from esp after the push of esi
    float arg0 = height;  // the original reuses its argument slots (hit distance, radius)
    float arg1 = range;
    VCall<void>(obj, 0x54 / 4, f.A(8));
    f.F(0x10) = f.F(0x10) + arg0;
    f.L(0x14) = 0;
    f.L(0x18) = 0;
    f.L(0x1c) = 0xbf800000;  // -1.0
    const uint32_t hit = orig::fn_004137e0(u(&arg0), f.A(0x14), Field<uint32_t>(g_game, 0xa8c), f.A(8), fb(arg1));
    if (!static_cast<uint8_t>(hit))
        return hit & 0xffffff00u;
    f.F(0x10) = (f.F(0x10) - arg0) + K(0x005bcbfc);
    const float* position = reinterpret_cast<const float*>(f.b + 8);
    Object_SetPosition(0, position, obj);
    const uint32_t collidable = u(Field<uint8_t*>(obj, 0x1c4) + 0x14);
    uint8_t now = fn_00455720(reinterpret_cast<const uint32_t*>(collidable),
                              Field<uint8_t*>(Field<uint8_t*>(g_game, 0xa8c), 8));
    if (now)
    {
        VCall<void>(obj, 0x90 / 4, f.A(0x14), u(&arg1), 0u);
        uint32_t changes = 0;
        uint8_t previous = now;
        for (;;)
        {
            if (now)
                f.F(0x10) = arg1 + f.F(0x10);
            else
            {
                f.L(4) = f.L(0x10);
                f.F(0x10) = f.F(0x10) - arg1;
            }
            if (previous != now)
            {
                arg1 = arg1 * K(0x005bcb4c);
                ++changes;
            }
            if (changes > 10)
                break;
            Object_SetPosition(0, position, obj);
            previous = now;
            now = fn_00455720(reinterpret_cast<const uint32_t*>(collidable),
                              Field<uint8_t*>(Field<uint8_t*>(g_game, 0xa8c), 8));
        }
        f.F(0x10) = f.F(4) + K(0x005bcbfc);
        Object_SetPosition(0, position, obj);
    }
    return 1;
}

static uint32_t Object_ForceGround_Abi(uint32_t edi, uint32_t a0, uint32_t a1)
{
    return Object_ForceGround(B(edi), bf(a0), bf(a1));
}
ABI_ENTRY_00455860(Object_ForceGround_Entry, Object_ForceGround_Abi)
HOOK(0x00455860, "Object_ForceGround", Object_ForceGround_Entry, nullptr);

// 0x00455f10 (stack: object, vector, scale (float), index; callee pops 0x10; caller 0x0043ee90; not executed in
// recorded runs). Sets every ragdoll body's (+0x1bc records of 0x4c bytes) linear velocity to vector * scale (wake,
// motion virtual +0x50); for the record number `index` a force action is also queued (0x00455aa0(eax = &impulse;
// object, 0.5, index, 0, 0)) with impulse = mass * vector * scale * 7. Returns the record cursor.
uint32_t fn_00455f10(uint8_t* obj, const float* v, float scale, int32_t index)
{
    Frame<0x30> f;  // offsets from esp after the pushes of ebx, esi and edi
    uint8_t* record = Field<uint8_t*>(obj, 0x1bc);
    if (!record)
        return 0;
    uint8_t* entity = Field<uint8_t*>(record, 0);
    if (!entity)
        return u(record);
    f.L(0xc) = u(record);
    f.L(0x10) = 0;
    do
    {
        f.F(0x20) = scale * v[0];
        f.L(0x2c) = 0;
        f.F(0x24) = scale * v[1];
        f.F(0x28) = scale * v[2];
        WakeEntity(entity);
        VCall<void>(Motion(entity), 0x50 / 4, f.A(0x20));
        if (static_cast<int32_t>(f.L(0x10)) == index)
        {
            const float m = MassOf(Field<uint8_t*>(B(f.L(0xc)), 0));
            f.F(0x14) = ((m * v[0]) * scale) * K(0x005bcde8);
            f.F(0x18) = ((m * v[1]) * scale) * K(0x005bcde8);
            f.F(0x1c) = ((m * v[2]) * scale) * K(0x005bcde8);
            orig::fn_00455aa0(f.A(0x14), u(obj), 0x3f000000, static_cast<uint32_t>(index), 0, 0);
        }
        record = B(f.L(0xc));
        entity = Field<uint8_t*>(record, 0x4c);
        f.L(0xc) = u(record) + 0x4c;
        ++f.L(0x10);
    } while (entity);
    return f.L(0xc);
}

static uint32_t fn_00455f10_Abi(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_00455f10(B(a0), reinterpret_cast<const float*>(a1), bf(a2), static_cast<int32_t>(a3));
}
ABI_ENTRY_00455f10(fn_00455f10_Entry, fn_00455f10_Abi)
HOOK(0x00455f10, "fn_00455f10", fn_00455f10_Entry, nullptr);

// 0x00456250 Body_wakeAndSetLinearVelocity (stack: entity, velocity; callee pops 8; callers 0x0048cec0, the
// speed-trap agent 0x004a88a0 and 0x004df8a0): wakes the body and calls motion virtual +0x50 (setLinearVelocity).
uint32_t Body_wakeAndSetLinearVelocity(uint8_t* entity, const float* velocity)
{
    WakeEntity(entity);
    return VCall<uint32_t>(Motion(entity), 0x50 / 4, velocity);
}

static uint32_t Body_wakeAndSetLinearVelocity_Abi(uint32_t a0, uint32_t a1)
{
    return Body_wakeAndSetLinearVelocity(B(a0), reinterpret_cast<const float*>(a1));
}
ABI_ENTRY_00456250(Body_wakeAndSetLinearVelocity_Entry, Body_wakeAndSetLinearVelocity_Abi)
HOOK(0x00456250, "Body_wakeAndSetLinearVelocity", Body_wakeAndSetLinearVelocity_Entry, nullptr);

// ================================================================================================================
// Point collector (vtable 0x005b0b18 {0x00456300 dtor, 0x005136d0 Havok addPoint, ...}) and game shape wrappers
// ================================================================================================================

// 0x004562a0 (eax = collision input, ecx = other collidable, edx = collidable, esi = collector memory; caller
// 0x00455720): constructor: vtable 0x005b0b18, inline point array (+0x10 = +0x20, capacity 4 not owned), +0xa8 /
// +0xac collidables, +0xa4 = -1, +0xa0 = FLT_MAX ([0x00588dcc]), +0xb0 = the collision agent from 0x00513420(ecx =
// input +0; collidable, other, input). Returns the collector.
uint32_t PointCollector_Construct(uint8_t* input, uint32_t other, uint32_t collidable, uint8_t* c)
{
    const uint32_t big = GameGlobal<uint32_t>(0x00588dcc);
    Field<uint32_t>(c, 0) = 0x005b0b18;
    Field<uint32_t>(c, 0x10) = u(c) + 0x20;
    Field<uint32_t>(c, 0x18) = 0x80000004;
    Field<uint32_t>(c, 0x14) = 0;
    Field<uint32_t>(c, 0xa8) = collidable;
    Field<uint32_t>(c, 0xac) = other;
    Field<uint32_t>(c, 0xa4) = 0xffffffff;
    Field<uint32_t>(c, 0xa0) = big;
    Field<uint32_t>(c, 0x14) = 0;
    Field<uint32_t>(c, 0xb0) = orig::fn_00513420(Field<uint32_t>(input, 0), collidable, other, u(input));
    return u(c);
}

static uint32_t PointCollector_Construct_Abi(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t esi)
{
    return PointCollector_Construct(B(eax), ecx, edx, B(esi));
}
ABI_ENTRY_004562a0(PointCollector_Construct_Entry, PointCollector_Construct_Abi)
HOOK(0x004562a0, "PointCollector_Construct", PointCollector_Construct_Entry, nullptr);

// 0x00456300 (ecx = collector; stack: flags; callee pops 4; slot 0 of vtable 0x005b0b18, the scalar deleting
// destructor): frees an owned point array (+0x18 >= 0: capacity * 0x20 bytes, class 0x10), base destructor
// 0x0051a5f0, and with flags bit 0 frees the collector (u16 size +4, class 0x18). Returns the collector.
uint32_t PointCollector_Delete(uint8_t* c, uint32_t flags)
{
    Field<uint32_t>(c, 0) = 0x005b0b18;
    const uint32_t capacity = Field<uint32_t>(c, 0x18);
    if (static_cast<int32_t>(capacity) >= 0)
        HkFree(Field<uint32_t>(c, 0x10), (capacity & 0x7fffffff) << 5, 0x10);
    orig::fn_0051a5f0(u(c));
    if (flags & 1)
        HkFree(u(c), Field<uint16_t>(c, 4), 0x18);
    return u(c);
}

static uint32_t PointCollector_Delete_Abi(uint32_t ecx, uint32_t a0)
{
    return PointCollector_Delete(B(ecx), a0);
}
ABI_ENTRY_00456300(PointCollector_Delete_Entry, PointCollector_Delete_Abi)
HOOK(0x00456300, "PointCollector_Delete", PointCollector_Delete_Entry, nullptr);

// 0x00456360 (ecx = shape; stack: flags; callee pops 4; slot 0 of vtable 0x005b0af8, the scalar deleting destructor
// of the pair shape built by 0x004527d0 / 0x004574e0 (+0xc child, +0x10 game shape); not executed in recorded runs):
// releases +0x10 and +0xc, restores the base vtable 0x0058e3c4 and with flags bit 0 frees it (class 0x1d).
uint32_t fn_00456360(uint8_t* shape, uint32_t flags)
{
    Field<uint32_t>(shape, 0) = 0x005b0af8;
    HkRelease(Field<uint8_t*>(shape, 0x10));
    HkRelease(Field<uint8_t*>(shape, 0xc));
    Field<uint32_t>(shape, 0) = 0x0058e3c4;
    if (flags & 1)
        HkFree(u(shape), Field<uint16_t>(shape, 4), 0x1d);
    return u(shape);
}

static uint32_t fn_00456360_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_00456360(B(ecx), a0);
}
ABI_ENTRY_00456360(fn_00456360_Entry, fn_00456360_Abi)
HOOK(0x00456360, "fn_00456360", fn_00456360_Entry, nullptr);

// 0x004563c0 (slots 1 and 2 of vtable 0x005b0af8; not executed in recorded runs): shape type getter, returns 3.
uint32_t fn_004563c0()
{
    return 3;
}

static uint32_t fn_004563c0_Abi()
{
    return fn_004563c0();
}
ABI_ENTRY_004563c0(fn_004563c0_Entry, fn_004563c0_Abi)
HOOK(0x004563c0, "fn_004563c0", fn_004563c0_Entry, nullptr);

// 0x004563d0 (ecx = child shape, esi = shape memory; callers 0x004527d0, 0x00475220): constructor of the wrapper with
// vtable 0x005b0ad8 (type 0x12): u16 +6 = 1, +8 = 0, +0x1c = 0, +0xc = child (reference taken), +0x10 = 25.0,
// +0x18 = 0.016, +0x14 = the child's bounding-sphere radius (child virtual +0x10(&sphere) -> sphere +0xc), +0x20 = 0,
// +0x24 = 0.8. Returns the shape.
uint32_t fn_004563d0(uint8_t* child, uint8_t* shape)
{
    alignas(16) uint32_t sphere[4];
    Field<uint16_t>(shape, 6) = 1;
    Field<uint32_t>(shape, 8) = 0;
    Field<uint32_t>(shape, 0) = 0x005b0ad8;
    Field<uint32_t>(shape, 0x1c) = 0;
    Field<uint8_t*>(shape, 0xc) = child;
    ++Field<uint16_t>(child, 6);
    Field<uint32_t>(shape, 0x10) = 0x41c80000;  // 25.0
    Field<uint32_t>(shape, 0x18) = 0x3c83126f;  // 0.016
    sphere[0] = 0;
    sphere[1] = 0;
    sphere[2] = 0;
    sphere[3] = 0x3f800000;
    VCall<void>(child, 0x10 / 4, sphere);
    Field<uint32_t>(shape, 0x14) = sphere[3];
    shape[0x20] = 0;
    Field<uint32_t>(shape, 0x24) = 0x3f4ccccd;  // 0.8
    return u(shape);
}

static uint32_t fn_004563d0_Abi(uint32_t ecx, uint32_t esi)
{
    return fn_004563d0(B(ecx), B(esi));
}
ABI_ENTRY_004563d0(fn_004563d0_Entry, fn_004563d0_Abi)
HOOK(0x004563d0, "fn_004563d0", fn_004563d0_Entry, nullptr);

// 0x00456450 (ecx = shape; stack: flags; callee pops 4; slot 0 of vtable 0x005b0ad8, the scalar deleting destructor
// of 0x004563d0's wrapper; not executed in recorded runs): releases the child +0xc, restores the base vtable
// 0x0058e3c4 and with flags bit 0 frees it (class 0x1d).
uint32_t fn_00456450(uint8_t* shape, uint32_t flags)
{
    Field<uint32_t>(shape, 0) = 0x005b0ad8;
    HkRelease(Field<uint8_t*>(shape, 0xc));
    Field<uint32_t>(shape, 0) = 0x0058e3c4;
    if (flags & 1)
        HkFree(u(shape), Field<uint16_t>(shape, 4), 0x1d);
    return u(shape);
}

static uint32_t fn_00456450_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_00456450(B(ecx), a0);
}
ABI_ENTRY_00456450(fn_00456450_Entry, fn_00456450_Abi)
HOOK(0x00456450, "fn_00456450", fn_00456450_Entry, nullptr);

// 0x004564a0 (slots 1 and 2 of vtable 0x005b0ad8; not executed in recorded runs): shape type getter, returns 0x12.
uint32_t fn_004564a0()
{
    return 0x12;
}

static uint32_t fn_004564a0_Abi()
{
    return fn_004564a0();
}
ABI_ENTRY_004564a0(fn_004564a0_Entry, fn_004564a0_Abi)
HOOK(0x004564a0, "fn_004564a0", fn_004564a0_Entry, nullptr);

// 0x004564b0 (eax = constraint body descriptor, ecx = ragdoll definition; stack: body table; caller pops; caller
// 0x00456570). Looks up the descriptor's name key (u16 at +0x8c -> +8) in the hash 0x004e0f40 of descriptor +4 +0x18
// +4 (second lookup slot, only with more than one bucket), then the found entry's name (entry + u16 +2 + 4) in the
// definition's skeleton hash (+0x2c +4); the bone's index (u16 +0xa) selects the entity in table +0x10. Returns the
// entity or 0. Note: a failed first lookup dereferences a null entry (reproduced).
uint32_t fn_004564b0(uint8_t* desc, uint8_t* def, uint8_t* table)
{
    uint32_t key = Field<uint16_t>(Field<uint8_t*>(desc, 0x8c), 8);
    uint8_t* skeleton = Field<uint8_t*>(def, 0x2c);
    uint8_t* hash = Field<uint8_t*>(Field<uint8_t*>(Field<uint8_t*>(desc, 4), 0x18), 4);
    uint8_t* entry = nullptr;
    uint32_t buckets = Field<uint32_t>(hash, 0x1c);
    if (buckets && Field<uint32_t>(hash, 0x20) > 1)
    {
        uint8_t* query = Field<uint8_t*>(hash, 0x24) + 0x1c;
        Field<uint32_t>(query, 0) = buckets;
        Field<uint32_t>(query, 0x18) = u(&key);
        orig::fn_004e0f40(u(query));
        if (!Field<uint32_t>(query, 0xc))
            entry = B(Field<uint32_t*>(query, 0x10)[Field<uint32_t>(query, 8)]);
    }
    const uint32_t name = u(entry) + Field<uint16_t>(entry, 2) + 4;
    hash = Field<uint8_t*>(skeleton, 4);
    buckets = Field<uint32_t>(hash, 0x1c);
    if (!buckets || !Field<uint32_t>(hash, 0x20))
        return 0;
    uint8_t* query = Field<uint8_t*>(hash, 0x24);
    Field<uint32_t>(query, 0) = buckets;
    Field<uint32_t>(query, 0x18) = name;
    orig::fn_004e0f40(u(query));
    if (Field<uint32_t>(query, 0xc))
        return 0;
    uint8_t* bone = B(Field<uint32_t*>(query, 0x10)[Field<uint32_t>(query, 8)]);
    if (!bone)
        return 0;
    uint8_t* info = B(Field<uint32_t>(bone, Field<uint16_t>(bone, 2)));
    if (!info)
        return 0;
    uint32_t* entities = Field<uint32_t*>(table, 0x10);
    if (!entities)
        return 0;
    return entities[Field<uint16_t>(info, 0xa)];
}

static uint32_t fn_004564b0_Abi(uint32_t eax, uint32_t ecx, uint32_t a0)
{
    return fn_004564b0(B(eax), B(ecx), B(a0));
}
ABI_ENTRY_004564b0(fn_004564b0_Entry, fn_004564b0_Abi)
HOOK(0x004564b0, "fn_004564b0", fn_004564b0_Entry, nullptr);

// ================================================================================================================
// Ragdoll bodies and constraints
// ================================================================================================================

// 0x00456570 Body_addConstraints (stack: object; callee pops 4; callers 0x00405d10, 0x0040abc0, 0x0043ee90 ...;
// string "RigidBodiesAddConstraints: LimitedPoint2Point constraint not supported"). The ragdoll definition
// (0x00453120(eax = object, edx = 0x00453100(object); animation slot 4 entry)) +0xc +0x14 lists constraint
// descriptors (+0x14 count, +0x18 array). For each: bodies A / B from +0x14 / +0x18 (0x004564b0 with object +0x410),
// strength = (mass A + mass B) * 20. The object matrix (virtual +0x60) and scale (0x004535b0) place the pivot (+0x1c,
// scaled, 0x004e36a0) and axes (0x004e37f0 with -0x11). Type 2: hinge: plain (0x00459810, 0x004af7c0, 0xa0 bytes)
// when it has no limits (+0x34) and zero strength, else limited (0x0045a1b0, 0x00459bb0, limits +0x3c/+0x40, strength
// +0x38 and +0xd0; 0x004af020, 0xe0 bytes). Type 1: unsupported (logged with 0x00407ff0). Type 0: ragdoll joint
// (0x0045a3d0 cinfo, axes +0x34 / +0x28, cone cos(+0x4c), sin(+0x50), sin(+0x54), sin(+0x40), sin(+0x44), 0x0045a1e0,
// 0x004ae980, 0xe0 bytes). Each constraint is added to the world (0x004ac3e0) and released. Returns the loop counter.
uint32_t Body_addConstraints(uint8_t* obj)
{
    Frame<0x240> f;  // the original's frame (offsets from esp after the three register pushes)
    const uint32_t id = orig::fn_00453100(u(obj));
    const uint32_t slot = orig::fn_00408800(4, u(obj));
    uint8_t* def = B(orig::fn_00453120(u(obj), id, *reinterpret_cast<uint32_t*>(slot)));
    f.L(0x2c) = u(def);
    if (!def || !Field<uint32_t>(def, 0xc))
        return 0;
    uint8_t* set = Field<uint8_t*>(Field<uint8_t*>(def, 0xc), 0x14);
    f.L(0x48) = u(set);
    if (!set)
        return 0;
    f.L(0x14) = VCall<uint32_t>(obj, 0x60 / 4);
    orig::fn_004535b0(f.A(0x3c), u(obj));
    f.L(0x28) = 0;
    if (Field<int32_t>(set, 0x14) <= 0)
        return 0;
    f.L(0x4c) = Field<uint32_t>(set, 0x18);
    do
    {
        uint8_t* c = B(*reinterpret_cast<uint32_t*>(f.L(0x4c)));
        f.L(0x4c) += 4;
        if (c)
        {
            float sum = K(0x0056c05c);
            uint8_t* bodyA = nullptr;
            uint8_t* bodyB = nullptr;
            if (Field<uint32_t>(c, 0x14))
            {
                bodyA = B(fn_004564b0(Field<uint8_t*>(c, 0x14), Field<uint8_t*>(B(f.L(0x2c)), 8),
                                      Field<uint8_t*>(obj, 0x410)));
                sum = MassOf(bodyA);
            }
            if (Field<uint32_t>(c, 0x18))
            {
                bodyB = B(fn_004564b0(Field<uint8_t*>(c, 0x18), Field<uint8_t*>(B(f.L(0x2c)), 8),
                                      Field<uint8_t*>(obj, 0x410)));
                sum = sum + MassOf(bodyB);
            }
            f.F(0x18) = sum * K(0x005bcbe8);  // 20.0
            const uint32_t type = Field<uint32_t>(c, 4);
            if (type == 2)
            {
                f.F(0x78) = f.F(0x3c) * Field<float>(c, 0x1c);
                f.F(0x7c) = f.F(0x40) * Field<float>(c, 0x20);
                f.F(0x80) = f.F(0x44) * Field<float>(c, 0x24);
                orig::fn_004e36a0(f.A(0x78), f.L(0x14), 1, f.A(0x30));
                f.L(0x60) = f.L(0x30);
                f.L(0x64) = f.L(0x34);
                f.L(0x68) = f.L(0x38);
                f.L(0x6c) = 0;
                orig::fn_004e37f0(u(c) + 0x28, f.L(0x14), 1, f.A(0x30), 0xffffffef);
                f.L(0x54) = f.L(0x34);
                f.L(0x58) = f.L(0x38);
                f.L(0x50) = f.L(0x30);
                f.L(0x5c) = 0;
                Field<uint32_t>(c, 0x38) = f.L(0x18);
                uint8_t* constraint;
                if (!Field<uint32_t>(c, 0x34) && f.F(0x18) == K(0x0056c05c))
                {
                    f.L(0x150) = 0;
                    f.L(0x154) = 0;
                    fn_00459810(reinterpret_cast<const float*>(f.b + 0x50), bodyA, bodyB, f.b + 0x150,
                                reinterpret_cast<const float*>(f.b + 0x60));
                    uint8_t* memory = HkAlloc(0xa0, 0x22);
                    Field<uint16_t>(memory, 4) = 0xa0;
                    constraint = B(orig::fn_004af7c0(u(memory), f.A(0x150)));
                }
                else
                {
                    orig::fn_0045a1b0(f.A(0x1b0));
                    orig::fn_00459bb0(f.A(0x50), u(bodyA), u(bodyB), f.A(0x1b0), f.A(0x60));
                    if (Field<uint32_t>(c, 0x34))
                    {
                        f.L(0x230) = Field<uint32_t>(c, 0x3c);
                        f.L(0x234) = Field<uint32_t>(c, 0x40);
                    }
                    f.L(0x238) = Field<uint32_t>(c, 0x38);
                    uint8_t* memory = HkAlloc(0xe0, 0x22);
                    Field<uint16_t>(memory, 4) = 0xe0;
                    constraint = B(orig::fn_004af020(u(memory), f.A(0x1b0)));
                    Field<uint32_t>(constraint, 0xd0) = f.L(0x18);
                }
                orig::fn_004ac3e0(Field<uint32_t>(Field<uint8_t*>(g_game, 0xa8c), 8), u(constraint));
                HkRelease(constraint);
            }
            else if (type == 1)
                orig::fn_00407ff0(Field<uint32_t>(g_game, 0x34), 0x005b0a90, 0, 0);
            else if (type == 0)
            {
                orig::fn_0045a3d0(f.A(0xc0));
                f.F(0x84) = f.F(0x3c) * Field<float>(c, 0x1c);
                f.F(0x88) = f.F(0x40) * Field<float>(c, 0x20);
                f.F(0x8c) = f.F(0x44) * Field<float>(c, 0x24);
                orig::fn_004e36a0(f.A(0x84), f.L(0x14), 1, f.A(0x1c));
                f.L(0x90) = f.L(0x1c);
                f.L(0x94) = f.L(0x20);
                f.L(0x98) = f.L(0x24);
                f.L(0x9c) = 0;
                orig::fn_004e37f0(u(c) + 0x34, f.L(0x14), 1, f.A(0x1c), 0xffffffef);
                f.L(0xb0) = f.L(0x1c);
                f.L(0xb4) = f.L(0x20);
                f.L(0xb8) = f.L(0x24);
                f.L(0xbc) = 0;
                orig::fn_004e37f0(u(c) + 0x28, f.L(0x14), 1, f.A(0x1c), 0xffffffef);
                const float cone = X87Cos(Field<float>(c, 0x4c));
                f.L(0xa0) = f.L(0x1c);
                f.L(0xa4) = f.L(0x20);
                f.L(0xa8) = f.L(0x24);
                f.L(0xac) = 0;
                f.F(0x130) = cone;
                f.F(0x134) = X87Sin(Field<float>(c, 0x50));
                f.F(0x138) = X87Sin(Field<float>(c, 0x54));
                f.F(0x13c) = X87Sin(Field<float>(c, 0x40));
                f.F(0x140) = X87Sin(Field<float>(c, 0x44));
                orig::fn_0045a1e0(f.A(0xc0), u(bodyA), u(bodyB), f.A(0x90), f.A(0xb0), f.A(0xa0));
                f.L(0x144) = f.L(0x18);
                uint8_t* memory = HkAlloc(0xe0, 0x22);
                Field<uint16_t>(memory, 4) = 0xe0;
                uint8_t* constraint = B(orig::fn_004ae980(u(memory), f.A(0xc0)));
                orig::fn_004ac3e0(Field<uint32_t>(Field<uint8_t*>(g_game, 0xa8c), 8), u(constraint));
                HkRelease(constraint);
            }
        }
        ++f.L(0x28);
    } while (static_cast<int32_t>(f.L(0x28)) < Field<int32_t>(B(f.L(0x48)), 0x14));
    return f.L(0x28);
}

static uint32_t Body_addConstraints_Abi(uint32_t a0)
{
    return Body_addConstraints(B(a0));
}
ABI_ENTRY_00456570(Body_addConstraints_Entry, Body_addConstraints_Abi)
HOOK(0x00456570, "Body_addConstraints", Body_addConstraints_Entry, nullptr);

// 0x00456a90 (esi = object; stack: physics manager, a1, keyframed (byte); callee pops 0xc; caller 0x004527d0).
// Creates the ragdoll bodies: the ragdoll definition (0x00453120(eax = object, edx = 0x00453100(object);
// 0x00458950(edx = id; object))) has +0x14 parts; an array of count + 1 records of 0x4c bytes ("new[]", constructed
// by 0x00403c40 with 0x004597d0). For each part a cinfo (0x00449510, mass properties cleared) gets its shape from
// Body_BuildShape 0x00413450(eax = definition; manager, object, part, a1, 0, &cinfo), cinfo +0x7a = 1, +0x10 = 1,
// +0x11 = keyframed, and the body (0x00457030) is stored in the record. Returns the array (0 without object).
uint32_t fn_00456a90(uint8_t* obj, uint32_t manager, uint32_t a1, uint8_t keyframed)
{
    Frame<0xe0> f;  // offsets from esp after the pushes of ebx and edi
    if (!obj)
        return 0;
    orig::fn_00449510(f.A(0x10));
    const uint32_t id = orig::fn_00453100(u(obj));
    const uint32_t k = orig::fn_00458950(id, u(obj));
    uint8_t* def = B(orig::fn_00453120(u(obj), id, k));
    const uint32_t n = Field<uint32_t>(def, 0x14) + 1;
    const uint32_t array = Alloc(0x10, n * 0x4c, 2, 0x0058b5e0);  // "new[]"
    f.L(8) = array;
    if (array)
    {
        orig::fn_00403c40(array, 0x4c, n, 0x004597d0);
        f.L(0xc) = f.L(8);
    }
    else
        f.L(0xc) = 0;
    f.L(8) = f.L(0xc);
    if (Field<int32_t>(def, 0x14) <= 0)
        return f.L(0xc);
    for (int32_t i = 0; i < Field<int32_t>(def, 0x14); ++i)
    {
        f.L(0x30) = 0;
        f.L(0x34) = 0;
        std::memset(f.b + 0x40, 0, 0x40);
        orig::fn_00413450(u(def), manager, u(obj), static_cast<uint32_t>(i), a1, 0, f.A(0x10));
        f.C(0x8a) = 1;
        f.C(0x20) = 1;
        if (keyframed)
            f.C(0x21) = 1;
        const uint32_t body = Body_Create(manager, obj, f.b + 0x10);
        *reinterpret_cast<uint32_t*>(f.L(8)) = body;
        f.L(8) += 0x4c;
    }
    return f.L(0xc);
}

static uint32_t fn_00456a90_Abi(uint32_t esi, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return fn_00456a90(B(esi), a0, a1, static_cast<uint8_t>(a2));
}
ABI_ENTRY_00456a90(fn_00456a90_Entry, fn_00456a90_Abi)
HOOK(0x00456a90, "fn_00456a90", fn_00456a90_Entry, nullptr);

// 0x00456c40 (stack: physics manager, object, definition, a3, a4, cinfo; callee pops 0x18; callers 0x00448470,
// 0x004527d0). Builds one compound shape for all parts of a definition (with an object, the definition comes from
// 0x00453120(eax = object, edx = 0x00453100(object); virtual +0x40)): each part's shape (Body_BuildShape 0x00413450)
// is collected (hkArray of pointers) and its mass properties (cinfo +0x20) and transform (+0x50 of the 0x90-byte
// entry) are collected for 0x004a5ed0, which combines them into cinfo +0x20 (no part: cleared mass properties). The
// shapes become a list shape (0x00511350, +8 = -1) in cinfo +0x74 and are released; both arrays are freed. Returns
// the last free's eax.
uint32_t fn_00456c40(uint32_t manager, uint8_t* obj, uint8_t* def, uint32_t a3, uint32_t a4, uint8_t* desc)
{
    Frame<0xc0> f;  // the original's frame (offsets from esp after the three register pushes)
    uint32_t shapes = 0;
    uint32_t masses = 0;
    if (obj)
    {
        const uint32_t id = orig::fn_00453100(u(obj));
        const uint32_t k = VCall<uint32_t>(obj, 0x40 / 4);
        def = B(orig::fn_00453120(u(obj), id, k));
    }
    f.L(0x2c) = 0x80000000;  // shape pointers {+0x24 data, +0x28 size, +0x2c capacity}
    f.L(0x20) = 0x80000000;  // mass entries {+0x18 data, +0x1c size, +0x20 capacity}
    f.L(0x24) = 0;
    f.L(0x28) = 0;
    f.L(0x18) = 0;
    f.L(0x1c) = 0;
    f.L(0x14) = 0;
    if (Field<int32_t>(def, 0x14) > 0)
    {
        do
        {
            orig::fn_00413450(u(def), manager, u(obj), f.L(0x14), a3, a4, u(desc));
            if (Field<uint32_t>(desc, 0x74))
            {
                if (shapes == (f.L(0x2c) & 0x7fffffff))
                {
                    shapes = shapes ? shapes * 2 : 1;
                    orig::fn_004aa830(f.A(0x24), shapes, 4);
                    shapes = f.L(0x28);
                }
                reinterpret_cast<uint32_t*>(f.L(0x24))[shapes] = Field<uint32_t>(desc, 0x74);
                ++shapes;
                f.L(0x28) = shapes;
                orig::fn_004142b0(f.A(0x30));
                orig::fn_00413770(f.A(0x30), u(desc) + 0x20);
                if (masses == (f.L(0x20) & 0x7fffffff))
                {
                    orig::fn_004aa830(f.A(0x18), masses ? masses * 2 : 1, 0x90);
                    masses = f.L(0x1c);
                }
                const uint32_t entry = f.L(0x18) + masses * 0x90;
                ++masses;
                f.L(0x1c) = masses;
                orig::fn_00413770(entry, f.A(0x30));
                std::memcpy(P(entry + 0x50), f.b + 0x80, 0x40);
            }
            ++f.L(0x14);
        } while (static_cast<int32_t>(f.L(0x14)) < Field<int32_t>(def, 0x14));
        if (masses)
        {
            orig::fn_004a5ed0(u(desc) + 0x20, f.A(0x18));
            goto massDone;
        }
    }
    f.L(0x30) = 0;
    f.L(0x34) = 0;
    std::memset(f.b + 0x40, 0, 0x40);
    orig::fn_00413770(u(desc) + 0x20, f.A(0x30));
massDone:
    uint32_t* data = reinterpret_cast<uint32_t*>(f.L(0x24));
    if (shapes)
    {
        uint8_t* memory = HkAlloc(0x28, 0x1d);
        data = reinterpret_cast<uint32_t*>(f.L(0x24));
        Field<uint16_t>(memory, 4) = 0x28;
        uint8_t* list = B(orig::fn_00511350(u(memory), u(data), shapes));
        Field<uint8_t*>(desc, 0x74) = list;
        Field<uint32_t>(list, 8) = 0xffffffff;
        for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(shapes); ++i)
            HkRelease(B(data[i]));
    }
    uint32_t result = f.L(0x20);
    if (static_cast<int32_t>(f.L(0x20)) >= 0)
        result = HkFree(f.L(0x18), (f.L(0x20) & 0x7fffffff) * 0x90, 0x10);
    result = f.L(0x2c);
    if (static_cast<int32_t>(f.L(0x2c)) >= 0)
        result = HkFree(u(data), (f.L(0x2c) & 0x7fffffff) * 4, 0x10);
    return result;
}

static uint32_t fn_00456c40_Abi(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
    return fn_00456c40(a0, B(a1), B(a2), a3, a4, B(a5));
}
ABI_ENTRY_00456c40(fn_00456c40_Entry, fn_00456c40_Abi)
HOOK(0x00456c40, "fn_00456c40", fn_00456c40_Entry, nullptr);

// ================================================================================================================
// Rigid body creation, removal and state
// ================================================================================================================

void RigidBodyCinfo_Init(uint8_t* cinfo);
uint32_t Matrix_ToPosQuat(const float* m, float* quat, float* pos);
uint32_t fn_00459630(uint8_t* obj, uint32_t unused);
uint32_t Body_wakeAndSetPositionAndRotation(uint8_t* entity, const float* position, const float* rotation);

// 0x00456fa0 (edi = ragdoll body records; stack: one slot that the original overwrites with the records before
// tail-jumping to the heap free; callee pops 4; caller 0x004512b0). For each record (0x4c bytes, entity at +0, until
// a null entity) the entity is removed from its world (0x004ac0e0) and released, and the slot cleared. Then the array
// is freed (0x004e08d0, heap 0x0063a210). Null records: nothing.
uint32_t BodyRecords_Destroy(uint32_t* records)
{
    if (!records)
        return 0;
    for (uint32_t* r = records; r[0]; r += 0x4c / 4)
    {
        uint8_t* entity = B(r[0]);
        if (Field<uint32_t>(entity, 0x44))
            orig::fn_004ac0e0(Field<uint32_t>(entity, 0x44), u(entity));
        HkRelease(B(r[0]));
        r[0] = 0;
    }
    return orig::fn_004e08d0(0x0063a210u, u(records));
}

static uint32_t BodyRecords_Destroy_Abi(uint32_t edi, uint32_t /*a0*/)
{
    return BodyRecords_Destroy(reinterpret_cast<uint32_t*>(edi));
}
ABI_ENTRY_00456fa0(BodyRecords_Destroy_Entry, BodyRecords_Destroy_Abi)
HOOK(0x00456fa0, "BodyRecords_Destroy", BodyRecords_Destroy_Entry, nullptr);

// 0x00457000 (esi = entity; stack: one slot reused as the destructor's argument; callee pops 4; callers 0x00448c70,
// 0x004512b0): removes the entity from its world (+0x44, 0x004ac0e0) and drops a reference; at zero the original
// tail-jumps into the deleting destructor (virtual 0) with 1.
uint32_t Body_RemoveAndRelease(uint8_t* entity)
{
    if (!entity)
        return 0;
    uint32_t result = Field<uint32_t>(entity, 0x44);
    if (result)
        result = orig::fn_004ac0e0(result, u(entity));
    --Field<uint16_t>(entity, 6);
    if (Field<uint16_t>(entity, 6) == 0)
        return VCall<uint32_t>(entity, 0, 1u);
    return result;
}

static uint32_t Body_RemoveAndRelease_Abi(uint32_t esi, uint32_t /*a0*/)
{
    return Body_RemoveAndRelease(B(esi));
}
ABI_ENTRY_00457000(Body_RemoveAndRelease_Entry, Body_RemoveAndRelease_Abi)
HOOK(0x00457000, "Body_RemoveAndRelease", Body_RemoveAndRelease_Entry, nullptr);

// 0x00457030 Body_Create (stack: physics manager, object, cinfo; callee pops 0xc; callers 0x00448470, 0x004517b0,
// 0x00456a90). Builds a Havok rigid body: Havok cinfo (frame +0x20, 0x00459730) with position / rotation from the
// cinfo transform (+0x80, 0x00457ab0). No shape: the manager's default shape (+0xc) and mass properties (+0x10),
// cinfo +0 = 0x4000. Motion type 5 (fixed) when cinfo +0xe or (object present, 0x004531e0 false and cinfo +0x7a
// clear; this also sets +0xe); else mass properties from the cinfo (the manager's when the mass <= 0; identity
// inertia 0x005cad50 and mass 1 without a shape) and motion type cinfo +0x70. Filter info = cinfo +0, shape = +0x74.
// The body (0x84 bytes, class 0x23, 0x004afd80) takes over the shape (released unless it was the default) and
// cinfo +0x74 is cleared. cinfo +0x11: a single-child list collapses to the child, and a type-1 shape is wrapped by
// the game's 0x004dfaa0 (0x30 bytes, 20.0, keeps the filter word) plus a pair exclusion (0x004aaf20(1, 1)). cinfo
// +0xd (unless fixed): keyframed (0x004aff70(1)). cinfo +0xf: drops the body's +0x40. The owner is stored as pair key
// 0 (0x004aaf20(0, object)); u16 +0x4c = 0. Unless cinfo +0x7a: with an object and no save being loaded (Game+0xab8
// +0x14 flags 2 / 1) the object goes on the pending-add list (0x004546d0, Game+0xa84 +0xf20), otherwise the body is
// added to the world (0x004aba90). Returns the body.
uint32_t Body_Create(uint32_t manager, uint8_t* obj, uint8_t* cinfo)
{
    Frame<0x100> f;  // the original's frame (offsets from esp after the three register pushes)
    RigidBodyCinfo_Init(f.b + 0x20);
    f.C(0xf) = 0;
    Matrix_ToPosQuat(reinterpret_cast<const float*>(cinfo + 0x80), reinterpret_cast<float*>(f.b + 0x40),
                     reinterpret_cast<float*>(f.b + 0x30));
    if (!Field<uint32_t>(cinfo, 0x74))
    {
        Field<uint32_t>(cinfo, 0x74) = Field<uint32_t>(B(manager), 0xc);
        f.C(0xf) = 1;
        orig::fn_00413770(u(cinfo) + 0x20, manager + 0x10);
        Field<uint32_t>(cinfo, 0) = 0x4000;
    }
    const uint8_t fixed = (obj && !static_cast<uint8_t>(orig::fn_004531e0(u(obj))) && !cinfo[0x7a]) ? 1 : 0;
    cinfo[0xe] |= fixed;
    if (cinfo[0xe])
        f.L(0xa4) = 5;
    else
    {
        if (Field<uint32_t>(cinfo, 0x74))
        {
            if (Field<float>(cinfo, 0x24) <= K(0x0056c05c))
                orig::fn_00413770(u(cinfo) + 0x20, manager + 0x10);
            f.L(0x90) = Field<uint32_t>(cinfo, 0x24);
            std::memcpy(f.b + 0x80, cinfo + 0x30, 0x10);
            std::memcpy(f.b + 0x50, cinfo + 0x40, 0x30);
        }
        else
        {
            std::memcpy(f.b + 0xd8, P(0x005cad58), 0x28);
            f.F(0x50) = GameGlobal<float>(0x005cad50);
            f.F(0x54) = GameGlobal<float>(0x005cad54);
            std::memcpy(f.b + 0x58, f.b + 0xd8, 0x28);
            f.L(0x90) = 0x3f800000;
            f.L(0x80) = 0;
            f.L(0x84) = 0;
            f.L(0x88) = 0;
            f.L(0x8c) = 0;
        }
        f.L(0xa4) = Field<uint32_t>(cinfo, 0x70);
    }
    f.L(0x28) = Field<uint32_t>(cinfo, 0);
    f.L(0x24) = Field<uint32_t>(cinfo, 0x74);
    uint8_t* memory = HkAlloc(0x84, 0x23);
    Field<uint16_t>(memory, 4) = 0x84;
    uint8_t* body = B(orig::fn_004afd80(u(memory), f.A(0x20)));
    f.L(0x10) = u(body);
    uint8_t* shape = Field<uint8_t*>(cinfo, 0x74);
    if (shape)
    {
        if (!f.C(0xf))
            HkRelease(shape);
        Field<uint32_t>(cinfo, 0x74) = 0;
    }
    if (cinfo[0x11])
    {
        uint8_t* s = Field<uint8_t*>(body, 0x24);
        const uint32_t tag = Field<uint32_t>(s, 8);
        f.L(0x18) = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(tag >> 16)));
        f.L(0x14) = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(tag & 0xffff)));
        if (VCall<int32_t>(s, 1) == 0x10)
        {
            f.L(0x1c) = u(s);
            if (VCall<int32_t>(s, 0x20 / 4) == 1)
            {
                s = B(Field<uint32_t*>(s, 0xc)[0]);
                if (VCall<int32_t>(s, 2) == 1)
                {
                    ++Field<uint16_t>(s, 6);
                    HkRelease(B(f.L(0x1c)));
                }
            }
        }
        if (VCall<int32_t>(s, 2) == 1)
        {
            uint8_t* m2 = HkAlloc(0x30, 0x1d);
            Field<uint16_t>(m2, 4) = 0x30;
            uint8_t* wrapper = B(orig::fn_004dfaa0(u(s), u(m2), u(body), 0x41a00000));  // 20.0
            Field<uint32_t>(wrapper, 8) = (f.L(0x14) & 0xffff) | (f.L(0x18) << 16);
            HkRelease(s);
            Field<uint8_t*>(body, 0x24) = wrapper;
            orig::fn_004aaf20(u(body), 1, 1);
        }
    }
    if (cinfo[0xd] && !cinfo[0xe])
        orig::fn_004aff70(u(body), 1);
    if (cinfo[0xf])
    {
        uint8_t* extra = Field<uint8_t*>(body, 0x40);
        if (extra)
            HkRelease(extra);
        Field<uint32_t>(body, 0x40) = 0;
    }
    orig::fn_004aaf20(u(body), 0, u(obj));
    Field<uint16_t>(body, 0x4c) = 0;
    if (cinfo[0x7a])
        return u(body);
    if (obj)
    {
        uint8_t* loader = Field<uint8_t*>(Field<uint8_t*>(g_game, 0xab8), 0x14);
        if (!loader || !(loader[0] & 2) || (loader[0] & 1))
        {
            orig::fn_004546d0(Field<uint32_t>(Field<uint8_t*>(g_game, 0xa84), 0xf20), u(obj));
            return f.L(0x10);
        }
    }
    orig::fn_004aba90(Field<uint32_t>(B(manager), 8), u(body));
    return u(body);
}

static uint32_t Body_Create_Abi(uint32_t a0, uint32_t a1, uint32_t a2)
{
    return Body_Create(a0, B(a1), B(a2));
}
ABI_ENTRY_00457030(Body_Create_Entry, Body_Create_Abi)
HOOK(0x00457030, "Body_Create", Body_Create_Entry, nullptr);

// 0x004574b0 (eax = index, ecx = object, edx = out; callers 0x00403c70, 0x00403fb0, 0x00456050, 0x0045da90): *out =
// the object's body (+0x1c4) for index < 0, else the entity of ragdoll record `index` (+0x1bc, 0x4c bytes) or 0
// without records. Returns eax as the original leaves it.
uint32_t Object_GetBody(int32_t index, uint8_t* obj, uint32_t* out)
{
    if (index < 0)
    {
        const uint32_t body = Field<uint32_t>(obj, 0x1c4);
        *out = body;
        return body;
    }
    uint8_t* records = Field<uint8_t*>(obj, 0x1bc);
    if (records)
    {
        *out = Field<uint32_t>(records, static_cast<uint32_t>(index) * 0x4c);
        return static_cast<uint32_t>(index) * 0x4c;
    }
    *out = 0;
    return static_cast<uint32_t>(index);
}

static uint32_t Object_GetBody_Abi(uint32_t eax, uint32_t ecx, uint32_t edx)
{
    return Object_GetBody(static_cast<int32_t>(eax), B(ecx), reinterpret_cast<uint32_t*>(edx));
}
ABI_ENTRY_004574b0(Object_GetBody_Entry, Object_GetBody_Abi)
HOOK(0x004574b0, "Object_GetBody", Object_GetBody_Entry, nullptr);

// 0x004574e0 (stack: unused, cinfo, radius, filter word; callee pops 0x10; callers 0x00405880, 0x00471fd0). Adds a
// sphere above the body: convex radius shape (0x0050f190, radius) in a transform shape (0x00512ab0) at (0, 0,
// radius), paired (0x005b0af8 wrapper, +8 = -3) with a new game shape (0x00411590), in a phantom wrapper (0x004a4530,
// edx = filter word, +8 = -3). Without a cinfo shape (+0x74) it becomes the shape; otherwise both are combined in a
// list shape (0x00511350, +8 = -4). Returns the last call's eax (the free's, or the list).
uint32_t fn_004574e0(uint32_t /*unused*/, uint8_t* cinfo, float radius, uint32_t filter)
{
    Frame<0x60> f;  // offsets from esp after the pushes of ebx, esi and edi
    uint8_t* memory = HkAlloc(0x10, 0x1d);
    Field<uint16_t>(memory, 4) = 0x10;
    uint8_t* sphere = B(orig::fn_0050f190(u(memory), fb(radius)));
    Field<uint32_t>(sphere, 8) = 0xfffffffd;
    memory = HkAlloc(0x50, 0x1d);
    uint8_t* transform = memory ? B(orig::fn_00512ab0(u(memory), u(sphere))) : nullptr;
    Field<uint32_t>(transform, 8) = 0xfffffffd;
    HkRelease(sphere);
    f.L(0x10) = 0;
    f.L(0x14) = 0;
    f.L(0x18) = 0;
    f.L(0x1c) = 0x3f800000;
    orig::fn_004de840(f.A(0x10), f.A(0x20));
    f.F(0x58) = radius;
    f.L(0x50) = 0;
    f.L(0x54) = 0;
    f.L(0x5c) = 0;
    orig::fn_00414240(u(transform) + 0x10, f.A(0x20));
    memory = HkAlloc(0xc, 0x1d);
    Field<uint16_t>(memory, 4) = 0xc;
    orig::fn_00411590(u(memory));
    uint8_t* gameShape = memory;
    Field<uint32_t>(gameShape, 8) = 0xfffffffd;
    uint8_t* pair = HkAlloc(0x14, 0x1d);
    Field<uint8_t*>(pair, 0xc) = transform;
    Field<uint16_t>(pair, 4) = 0x14;
    Field<uint16_t>(pair, 6) = 1;
    Field<uint32_t>(pair, 8) = 0;
    Field<uint32_t>(pair, 0) = 0x005b0af8;
    Field<uint8_t*>(pair, 0x10) = gameShape;
    ++Field<uint16_t>(gameShape, 6);
    ++Field<uint16_t>(Field<uint8_t*>(pair, 0xc), 6);
    Field<uint32_t>(pair, 8) = 0xfffffffd;
    HkRelease(gameShape);
    HkRelease(transform);
    memory = HkAlloc(0x18, 0x1d);
    uint8_t* phantom = memory ? B(orig::fn_004a4530(u(memory), u(pair), filter, CollisionInput4())) : nullptr;
    Field<uint32_t>(phantom, 8) = 0xfffffffd;
    HkRelease(pair);
    if (!Field<uint32_t>(cinfo, 0x74))
    {
        Field<uint8_t*>(cinfo, 0x74) = phantom;
        return u(phantom);
    }
    f.L(0x10) = 0;
    f.L(0x14) = 0;
    f.L(0x18) = 0x80000000;
    orig::fn_004aa830(f.A(0x10), 1, 4);
    uint32_t size = f.L(0x14);
    uint32_t* data = reinterpret_cast<uint32_t*>(f.L(0x10));
    data[size] = Field<uint32_t>(cinfo, 0x74);
    ++size;
    f.L(0x14) = size;
    if (size == (f.L(0x18) & 0x7fffffff))
    {
        orig::fn_004aa830(f.A(0x10), size ? size * 2 : 1, 4);
        size = f.L(0x14);
        data = reinterpret_cast<uint32_t*>(f.L(0x10));
    }
    data[size] = u(phantom);
    memory = HkAlloc(0x28, 0x1d);
    ++size;
    Field<uint16_t>(memory, 4) = 0x28;
    uint8_t* list = B(orig::fn_00511350(u(memory), u(data), size));
    f.L(0xc) = u(list);
    Field<uint32_t>(list, 8) = 0xfffffffc;
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(size); ++i)
        HkRelease(B(data[i]));
    Field<uint8_t*>(cinfo, 0x74) = list;
    if (static_cast<int32_t>(f.L(0x18)) < 0)
        return f.L(0x18);
    return HkFree(u(data), (f.L(0x18) & 0x7fffffff) * 4, 0x10);
}

static uint32_t fn_004574e0_Abi(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_004574e0(a0, B(a1), bf(a2), a3);
}
ABI_ENTRY_004574e0(fn_004574e0_Entry, fn_004574e0_Abi)
HOOK(0x004574e0, "fn_004574e0", fn_004574e0_Entry, nullptr);

// 0x00457790 Body_isKeyframed (eax = object; callers 0x004577b0, 0x00459000 and others): motion virtual +0x18
// (getType) == 4, in AL (EAX's upper bytes from the type, as the original's sete).
uint32_t Body_isKeyframed(uint8_t* obj)
{
    const uint32_t type = VCall<uint32_t>(Motion(Field<uint8_t*>(obj, 0x1c4)), 0x18 / 4);
    return (type & 0xffffff00u) | (type == 4 ? 1u : 0u);
}

static uint32_t Body_isKeyframed_Abi(uint32_t eax)
{
    return Body_isKeyframed(B(eax));
}
ABI_ENTRY_00457790(Body_isKeyframed_Entry, Body_isKeyframed_Abi)
HOOK(0x00457790, "Body_isKeyframed", Body_isKeyframed_Entry, nullptr);

// 0x004577b0 Object_setPhysicsEnabled (eax = object; stack: keyframed (byte, the full dword is passed on); callee
// pops 4; 72 callers). Unless the body is flagged (+0xc bit 0) or already in that state (0x00457790), sets keyframed
// (0x004aff70(edi = body; value)); when switching to dynamic while in a world, re-filters it (0x004ac850(world,
// body)). Returns eax as the last call left it (edx not reproduced).
uint64_t Object_setPhysicsEnabled(uint8_t* obj, uint32_t keyframed)
{
    uint8_t* body = Field<uint8_t*>(obj, 0x1c4);
    if (body[0xc] & 1)
        return u(body);
    const uint32_t state = Body_isKeyframed(obj);
    if (static_cast<uint8_t>(state) == static_cast<uint8_t>(keyframed))
        return state;
    uint32_t result = orig::fn_004aff70(Field<uint32_t>(obj, 0x1c4), keyframed);
    if (static_cast<uint8_t>(keyframed))
        return result;
    body = Field<uint8_t*>(obj, 0x1c4);
    if (!Field<uint32_t>(body, 0x44))
        return 0;
    return orig::fn_004ac850(Field<uint32_t>(Field<uint8_t*>(g_game, 0xa8c), 8), u(body));
}

static uint64_t Object_setPhysicsEnabled_Abi(uint32_t eax, uint32_t a0)
{
    return Object_setPhysicsEnabled(B(eax), a0);
}
ABI_ENTRY_004577b0(Object_setPhysicsEnabled_Entry, Object_setPhysicsEnabled_Abi)
HOOK(0x004577b0, "Object_setPhysicsEnabled", Object_setPhysicsEnabled_Entry, nullptr);

// 0x00457810 (eax = object; 12 callers): +0x424 |= 0x40. guess: marks the object as having no collision body.
void fn_00457810(uint8_t* obj)
{
    Field<uint32_t>(obj, 0x424) |= 0x40;
}

static void fn_00457810_Abi(uint32_t eax)
{
    fn_00457810(B(eax));
}
ABI_ENTRY_00457810(fn_00457810_Entry, fn_00457810_Abi)
HOOK(0x00457810, "fn_00457810", fn_00457810_Entry, nullptr);

// 0x00457870 (al = skip, ecx = position, edx = object; symbols/abi.csv lists three stack arguments but none is read
// or pushed by the callers). Teleports the object: unless al, drops its constraint +0x1c8 (0x00459630), takes a
// flagged body (+0xc bit 0) out of its world (0x004ac0e0), sets the body position to position - offset (+0x2dc,
// Entity_SetPosition 0x00406580) and puts it back (0x004aba90). Always: +0x370 = -1.0 (matrix cache time).
// Returns eax as left (the input when skipped); edx = the object when skipped (not reproduced otherwise).
uint64_t Object_SetPosition(uint32_t eax, const float* position, uint8_t* obj)
{
    uint32_t result = eax;
    if (!static_cast<uint8_t>(eax))
    {
        if (Field<uint32_t>(obj, 0x1c8))
            fn_00459630(obj, u(Field<uint8_t*>(g_game, 0xa84)) + 0xf2c);
        alignas(16) float local[4];
        local[0] = position[0] - Field<float>(obj, 0x2dc);
        local[3] = 0.0f;
        local[1] = position[1] - Field<float>(obj, 0x2e0);
        local[2] = position[2] - Field<float>(obj, 0x2e4);
        uint8_t* body = Field<uint8_t*>(obj, 0x1c4);
        uint32_t world = 0;
        if (body[0xc] & 1)
        {
            world = Field<uint32_t>(body, 0x44);
            if (world)
                orig::fn_004ac0e0(world, u(body));
        }
        result = orig::fn_00406580(Field<uint32_t>(obj, 0x1c4), u(local));
        if (world)
            result = orig::fn_004aba90(world, Field<uint32_t>(obj, 0x1c4));
    }
    Field<uint32_t>(obj, 0x370) = 0xbf800000;
    return (static_cast<uint64_t>(u(obj)) << 32) | result;
}

static uint64_t Object_SetPosition_Abi(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t /*a0*/, uint32_t /*a1*/,
                                       uint32_t /*a2*/)
{
    return Object_SetPosition(eax, reinterpret_cast<const float*>(ecx), B(edx));
}
ABI_ENTRY_00457870(Object_SetPosition_Entry, Object_SetPosition_Abi)
HOOK(0x00457870, "Object_SetPosition", Object_SetPosition_Entry, nullptr);

// 0x00457930 Body_getPosition (ecx = object; stack: out; callee pops 4): out = body position (motion +0xb0) + the
// object's offset (+0x2dc). Returns out.
uint32_t Body_getPosition(uint8_t* obj, float* out)
{
    const float* p = &Field<float>(Motion(Field<uint8_t*>(obj, 0x1c4)), 0xb0);
    const float z = p[2];
    out[0] = p[0] + Field<float>(obj, 0x2dc);
    out[1] = p[1] + Field<float>(obj, 0x2e0);
    out[2] = z + Field<float>(obj, 0x2e4);
    return u(out);
}

static uint32_t Body_getPosition_Abi(uint32_t ecx, uint32_t a0)
{
    return Body_getPosition(B(ecx), reinterpret_cast<float*>(a0));
}
ABI_ENTRY_00457930(Body_getPosition_Entry, Body_getPosition_Abi)
HOOK(0x00457930, "Body_getPosition", Body_getPosition_Entry, nullptr);

// 0x00457990 (al = skip; stack: object, matrix (0x44 bytes); callee pops 8; 12+ callers). Sets the object's transform:
// unless al, drops the constraint +0x1c8 (0x00459630), converts the matrix to position / rotation (0x00457ab0),
// subtracts the offset +0x2dc and moves the body (out of / back into its world when flagged, 0x004596d0). Always:
// +0x370 = -1.0 and the cached render matrix +0x32c = Matrix_Scale(object scale 0x004535b0) * matrix. Returns eax as
// left by Matrix_Multiply.
uint32_t Object_SetTransform(uint32_t eax, uint8_t* obj, const float* matrix)
{
    Frame<0x80> f;  // the original's frame (offsets from esp after the three register pushes)
    if (!static_cast<uint8_t>(eax))
    {
        if (Field<uint32_t>(obj, 0x1c8))
            fn_00459630(obj, u(Field<uint8_t*>(g_game, 0xa84)) + 0xf2c);
        Matrix_ToPosQuat(matrix, reinterpret_cast<float*>(f.b + 0x20), reinterpret_cast<float*>(f.b + 0x10));
        f.F(0x10) = f.F(0x10) - Field<float>(obj, 0x2dc);
        f.F(0x14) = f.F(0x14) - Field<float>(obj, 0x2e0);
        f.F(0x18) = f.F(0x18) - Field<float>(obj, 0x2e4);
        uint8_t* body = Field<uint8_t*>(obj, 0x1c4);
        uint32_t world = 0;
        if (body[0xc] & 1)
        {
            world = Field<uint32_t>(body, 0x44);
            if (world)
                orig::fn_004ac0e0(world, u(body));
        }
        Body_wakeAndSetPositionAndRotation(Field<uint8_t*>(obj, 0x1c4), reinterpret_cast<const float*>(f.b + 0x10),
                                           reinterpret_cast<const float*>(f.b + 0x20));
        if (world)
            orig::fn_004aba90(world, Field<uint32_t>(obj, 0x1c4));
    }
    Field<uint32_t>(obj, 0x370) = 0xbf800000;
    orig::fn_004535b0(f.A(0x10), u(obj));
    orig::fn_004e2d90(f.A(0x38), f.A(0x10), 0, 0, 0);
    return orig::fn_004e2910(f.A(0x38), u(obj) + 0x32c, u(matrix));
}

static uint32_t Object_SetTransform_Abi(uint32_t eax, uint32_t a0, uint32_t a1)
{
    return Object_SetTransform(eax, B(a0), reinterpret_cast<const float*>(a1));
}
ABI_ENTRY_00457990(Object_SetTransform_Entry, Object_SetTransform_Abi)
HOOK(0x00457990, "Object_SetTransform", Object_SetTransform_Entry, nullptr);

// 0x00457ab0 (esi = matrix (rows of 4, translation in the 4th column), edi = quaternion out; stack: position out;
// caller pops; callers 0x00457030, 0x00457990, 0x00458970, 0x00487a90): the 3x3 rotation, transposed into Havok's
// column layout, becomes a quaternion (0x004de920); unless 0x004dea40 accepts it as normalised it is normalised
// (1/sqrt of the squared length, 0 for a zero quaternion). Position = (m[3], m[7], m[11], 0). Returns AL = 1.
uint32_t Matrix_ToPosQuat(const float* m, float* quat, float* pos)
{
    alignas(16) float rotation[12];
    rotation[0] = m[0];
    rotation[1] = m[4];
    rotation[2] = m[8];
    rotation[3] = 0.0f;  // not written by the original (stack garbage)
    rotation[4] = m[1];
    rotation[5] = m[5];
    rotation[6] = m[9];
    rotation[7] = 0.0f;
    rotation[8] = m[2];
    rotation[9] = m[6];
    rotation[10] = m[10];
    rotation[11] = 0.0f;
    orig::fn_004de920(u(rotation), u(quat));
    if (!static_cast<uint8_t>(orig::fn_004dea40(u(quat))))
    {
        float sum = quat[0] * quat[0];
        sum = sum + quat[1] * quat[1];
        sum = sum + quat[2] * quat[2];
        sum = sum + quat[3] * quat[3];
        float inv;
        if (sum == K(0x0056c05c))
            inv = K(0x0056c05c);
        else
            inv = K(0x005bcb2c) / std::sqrt(sum);
        quat[0] = inv * quat[0];
        quat[1] = inv * quat[1];
        quat[2] = inv * quat[2];
        quat[3] = inv * quat[3];
    }
    const uint32_t x = reinterpret_cast<const uint32_t*>(m)[3];
    reinterpret_cast<uint32_t*>(pos)[0] = x;
    pos[1] = m[7];
    pos[3] = 0.0f;
    pos[2] = m[11];
    return (x & 0xffffff00u) | 1;
}

static uint32_t Matrix_ToPosQuat_Abi(uint32_t esi, uint32_t edi, uint32_t a0)
{
    return Matrix_ToPosQuat(reinterpret_cast<const float*>(esi), reinterpret_cast<float*>(edi),
                            reinterpret_cast<float*>(a0));
}
ABI_ENTRY_00457ab0(Matrix_ToPosQuat_Entry, Matrix_ToPosQuat_Abi)
HOOK(0x00457ab0, "Matrix_ToPosQuat", Matrix_ToPosQuat_Entry, nullptr);

// 0x00457c20 (ebx = object; callers 0x00419060, 0x00457d60, 0x00457d90, 0x00457ea0, 0x00457eb0). Refreshes the
// object's matrices from its body once per game time (0x00604a98 vs +0x370): position (motion +0xb0) + offset
// (+0x2dc) and rotation (motion +0x30, 0x004de840) give +0x2e8 (0x00457ba0); +0x32c = Matrix_Scale(scale) * +0x2e8
// when 0x004535a0 says the object is scaled, else a copy. Returns eax as the last call leaves it.
uint32_t fn_00457c20(uint8_t* obj)
{
    Frame<0x80> f;  // offsets from esp after the pushes of esi and edi
    const float now = GameGlobal<float>(0x00604a98);
    f.F(0xc) = now;
    if (now == Field<float>(obj, 0x370))
        return 0;
    uint8_t* motion = Motion(Field<uint8_t*>(obj, 0x1c4));
    const float* p = &Field<float>(motion, 0xb0);
    f.L(0x18) = Field<uint32_t>(motion, 0xb8);
    f.L(0x1c) = Field<uint32_t>(motion, 0xbc);
    f.L(0x20) = Field<uint32_t>(motion, 0x30);
    f.L(0x24) = Field<uint32_t>(motion, 0x34);
    f.L(0x28) = Field<uint32_t>(motion, 0x38);
    f.L(0x2c) = Field<uint32_t>(motion, 0x3c);
    f.F(0x10) = p[0] + Field<float>(obj, 0x2dc);
    f.F(0x14) = p[1] + Field<float>(obj, 0x2e0);
    f.F(0x18) = f.F(0x18) + Field<float>(obj, 0x2e4);
    orig::fn_004de840(f.A(0x20), f.A(0x30));
    f.L(0x60) = f.L(0x10);
    f.L(0x64) = f.L(0x14);
    f.L(0x6c) = f.L(0x1c);
    f.L(0x68) = f.L(0x18);
    orig::fn_00457ba0(u(obj) + 0x2e8, f.A(0x30));
    if (static_cast<uint8_t>(orig::fn_004535a0(u(obj))))
    {
        orig::fn_004535b0(f.A(0x10), u(obj));
        orig::fn_004e2d90(f.A(0x30), f.A(0x10), 0, 0, 0);
        const uint32_t result = orig::fn_004e2910(f.A(0x30), u(obj) + 0x32c, u(obj) + 0x2e8);
        Field<uint32_t>(obj, 0x370) = f.L(0xc);
        return result;
    }
    std::memcpy(obj + 0x32c, obj + 0x2e8, 0x44);
    Field<uint32_t>(obj, 0x370) = f.L(0xc);
    return 0;
}

static uint32_t fn_00457c20_Abi(uint32_t ebx)
{
    return fn_00457c20(B(ebx));
}
ABI_ENTRY_00457c20(fn_00457c20_Entry, fn_00457c20_Abi)
HOOK(0x00457c20, "fn_00457c20", fn_00457c20_Entry, nullptr);

// 0x00457dc0 (ecx = object; stack: out; callee pops 4; no direct callers: slot 0x58 of the object vtables, e.g.
// 0x005a8550; string "can't find center bone for GetInstanceLocationRagDoll"). out = location + offset (+0x2dc): a
// ragdolled object (+0x421) uses the centre of mass (motion +0x70) of its "center" body (0x004586a0), else of the
// first ragdoll body, else the body position (logged); otherwise the body position (motion +0xb0). Returns out.
uint32_t Object_GetInstanceLocation(uint8_t* obj, float* out)
{
    const float* p;
    if (obj[0x421])
    {
        uint8_t* entity = B(orig::fn_004586a0(0x005a009c, u(obj)));  // "center"
        if (entity)
            p = &Field<float>(Motion(entity), 0x70);
        else
        {
            uint8_t* first = B(Field<uint32_t*>(obj, 0x1bc)[0]);
            if (first)
                p = &Field<float>(Motion(first), 0x70);
            else
            {
                p = &Field<float>(Motion(Field<uint8_t*>(obj, 0x1c4)), 0xb0);
                const float x = p[0], y = p[1], z = p[2];
                const uint32_t name = orig::fn_00453270(u(obj));
                orig::fn_00407ff0(Field<uint32_t>(g_game, 0x34), 0x005b0a50, name, 0);
                out[0] = x + Field<float>(obj, 0x2dc);
                out[1] = y + Field<float>(obj, 0x2e0);
                out[2] = z + Field<float>(obj, 0x2e4);
                return u(out);
            }
        }
    }
    else
        p = &Field<float>(Motion(Field<uint8_t*>(obj, 0x1c4)), 0xb0);
    const float x = p[0], y = p[1], z = p[2];
    out[0] = x + Field<float>(obj, 0x2dc);
    out[1] = y + Field<float>(obj, 0x2e0);
    out[2] = z + Field<float>(obj, 0x2e4);
    return u(out);
}

static uint32_t Object_GetInstanceLocation_Abi(uint32_t ecx, uint32_t a0)
{
    return Object_GetInstanceLocation(B(ecx), reinterpret_cast<float*>(a0));
}
ABI_ENTRY_00457dc0(Object_GetInstanceLocation_Entry, Object_GetInstanceLocation_Abi)
HOOK(0x00457dc0, "Object_GetInstanceLocation", Object_GetInstanceLocation_Entry, nullptr);

// 0x00457fc0 Body_getAngularVelocity (eax = object, ecx = out): copies motion +0x50 (3 floats). Returns the z bits.
uint32_t Body_getAngularVelocity(uint8_t* obj, float* out)
{
    const float* v = &Field<float>(Motion(Field<uint8_t*>(obj, 0x1c4)), 0x50);
    const uint32_t z = reinterpret_cast<const uint32_t*>(v)[2];
    out[0] = v[0];
    reinterpret_cast<uint32_t*>(out)[2] = z;
    out[1] = v[1];
    return z;
}

static uint32_t Body_getAngularVelocity_Abi(uint32_t eax, uint32_t ecx)
{
    return Body_getAngularVelocity(B(eax), reinterpret_cast<float*>(ecx));
}
ABI_ENTRY_00457fc0(Body_getAngularVelocity_Entry, Body_getAngularVelocity_Abi)
HOOK(0x00457fc0, "Body_getAngularVelocity", Body_getAngularVelocity_Entry, nullptr);

// 0x00458000 Body_getLinearVelocity (eax = object, ecx = out; 26 callers): copies motion +0x40 (3 floats). Returns
// the z bits.
uint32_t Body_getLinearVelocity(uint8_t* obj, float* out)
{
    const float* v = &Field<float>(Motion(Field<uint8_t*>(obj, 0x1c4)), 0x40);
    const uint32_t z = reinterpret_cast<const uint32_t*>(v)[2];
    out[0] = v[0];
    reinterpret_cast<uint32_t*>(out)[2] = z;
    out[1] = v[1];
    return z;
}

static uint32_t Body_getLinearVelocity_Abi(uint32_t eax, uint32_t ecx)
{
    return Body_getLinearVelocity(B(eax), reinterpret_cast<float*>(ecx));
}
ABI_ENTRY_00458000(Body_getLinearVelocity_Entry, Body_getLinearVelocity_Abi)
HOOK(0x00458000, "Body_getLinearVelocity", Body_getLinearVelocity_Entry, nullptr);

// 0x004581c0 (eax = object; returns st0; 12+ callers): the body's mass, 1 / inverse mass (motion +0x2c) or 0. For a
// keyframed body (type 4) the original motion (+0xc0) is used (0 when the second type query differs).
float Body_GetMass(uint8_t* obj)
{
    uint8_t* motion;
    if (VCall<int32_t>(Motion(Field<uint8_t*>(obj, 0x1c4)), 0x18 / 4) == 4)
    {
        uint8_t* body = Field<uint8_t*>(obj, 0x1c4);
        if (VCall<int32_t>(Motion(body), 0x18 / 4) == 4)
            motion = Field<uint8_t*>(Motion(body), 0xc0);
        else
            motion = nullptr;
    }
    else
        motion = Motion(Field<uint8_t*>(obj, 0x1c4));
    const float inv = Field<float>(motion, 0x2c);
    if (inv == K(0x0056c05c))
        return K(0x0056c05c);
    return K(0x005bcb2c) / inv;
}

static float Body_GetMass_Abi(uint32_t eax)
{
    return Body_GetMass(B(eax));
}
ABI_ENTRY_004581c0(Body_GetMass_Entry, Body_GetMass_Abi)
HOOK(0x004581c0, "Body_GetMass", Body_GetMass_Entry, nullptr);

// 0x00458230 (eax = object; stack: value; callee pops 4; caller 0x004517b0 with the "restitution" property): body
// +0x3c = value (restitution).
void Body_SetRestitution(uint8_t* obj, uint32_t value)
{
    Field<uint32_t>(Field<uint8_t*>(obj, 0x1c4), 0x3c) = value;
}

static void Body_SetRestitution_Abi(uint32_t eax, uint32_t a0)
{
    Body_SetRestitution(B(eax), a0);
}
ABI_ENTRY_00458230(Body_SetRestitution_Entry, Body_SetRestitution_Abi)
HOOK(0x00458230, "Body_SetRestitution", Body_SetRestitution_Entry, nullptr);

// 0x00458240 (eax = object; returns st0; callers 0x004122f0, 0x00413120): body +0x3c (restitution).
float Body_GetRestitution(uint8_t* obj)
{
    return Field<float>(Field<uint8_t*>(obj, 0x1c4), 0x3c);
}

static float Body_GetRestitution_Abi(uint32_t eax)
{
    return Body_GetRestitution(B(eax));
}
ABI_ENTRY_00458240(Body_GetRestitution_Entry, Body_GetRestitution_Abi)
HOOK(0x00458240, "Body_GetRestitution", Body_GetRestitution_Entry, nullptr);

// 0x00458250 (eax = object; stack: value; callee pops 4; callers 0x004517b0 with the "friction" property and 6
// others): body +0x38 = value (friction).
void Body_SetFriction(uint8_t* obj, uint32_t value)
{
    Field<uint32_t>(Field<uint8_t*>(obj, 0x1c4), 0x38) = value;
}

static void Body_SetFriction_Abi(uint32_t eax, uint32_t a0)
{
    Body_SetFriction(B(eax), a0);
}
ABI_ENTRY_00458250(Body_SetFriction_Entry, Body_SetFriction_Abi)
HOOK(0x00458250, "Body_SetFriction", Body_SetFriction_Entry, nullptr);

// 0x00458260 (eax = object; returns st0; callers 0x004122f0, 0x00413120): body +0x38 (friction).
float Body_GetFriction(uint8_t* obj)
{
    return Field<float>(Field<uint8_t*>(obj, 0x1c4), 0x38);
}

static float Body_GetFriction_Abi(uint32_t eax)
{
    return Body_GetFriction(B(eax));
}
ABI_ENTRY_00458260(Body_GetFriction_Entry, Body_GetFriction_Abi)
HOOK(0x00458260, "Body_GetFriction", Body_GetFriction_Entry, nullptr);

// 0x004582d0 (esi = inertia matrix (rows of 4); stack: object; callee pops 4; caller 0x00458270). Unless the body is
// flagged (+0xc bit 0): a keyframed body (type 4) first gets its original motion back (+0xc0 into +8, 0x004b5a00,
// the keyframed motion deleted, +0x28 = motion +0x60); the transposed 3x3 goes to motion virtual +0x2c
// (setInertiaLocal); then the body is made keyframed again (0x004aff70(1)). Returns eax as left.
uint32_t Body_SetInertiaLocal(const float* m, uint8_t* obj)
{
    Frame<0x40> f;  // offsets from esp after the pushes of ebx and edi
    uint8_t* body = Field<uint8_t*>(obj, 0x1c4);
    if (body[0xc] & 1)
        return u(body);
    if (VCall<int32_t>(Motion(body), 0x18 / 4) != 4)
        f.C(0xf) = 0;
    else
    {
        uint8_t* entity = Field<uint8_t*>(obj, 0x1c4);
        f.C(0xf) = 1;
        if (VCall<int32_t>(Motion(entity), 0x18 / 4) == 4)
        {
            VCall<int32_t>(Motion(entity), 0x18 / 4);
            uint8_t* keyframed = Motion(entity);
            Field<uint32_t>(entity, 8) = Field<uint32_t>(keyframed, 0xc0);
            orig::fn_004b5a00(u(keyframed));
            Field<uint32_t>(keyframed, 0xc0) = 0;
            VCall<void>(keyframed, 0, 1u);
            Field<uint32_t>(entity, 0x28) = Field<uint32_t>(entity, 8) + 0x60;
        }
    }
    const uint32_t* s = reinterpret_cast<const uint32_t*>(m);
    f.L(0x10) = s[0];
    f.L(0x30) = s[2];
    f.L(0x20) = s[1];
    f.L(0x14) = s[4];
    f.L(0x34) = s[6];
    f.L(0x24) = s[5];
    f.L(0x18) = s[8];
    f.L(0x38) = s[10];
    f.L(0x28) = s[9];
    uint32_t result = VCall<uint32_t>(Motion(Field<uint8_t*>(obj, 0x1c4)), 0x2c / 4, f.A(0x10));
    if (f.C(0xf))
        result = orig::fn_004aff70(Field<uint32_t>(obj, 0x1c4), 1);
    return result;
}

static uint32_t Body_SetInertiaLocal_Abi(uint32_t esi, uint32_t a0)
{
    return Body_SetInertiaLocal(reinterpret_cast<const float*>(esi), B(a0));
}
ABI_ENTRY_004582d0(Body_SetInertiaLocal_Entry, Body_SetInertiaLocal_Abi)
HOOK(0x004582d0, "Body_SetInertiaLocal", Body_SetInertiaLocal_Entry, nullptr);

// 0x004583d0 (eax = impulse (3 floats), edx = object; caller 0x00482870; not executed in recorded runs): wakes the
// body and calls motion virtual +0x64 (applyAngularImpulse) with (x, y, z, 0).
uint32_t Body_ApplyAngularImpulse(const float* impulse, uint8_t* obj)
{
    alignas(16) float v[4];
    v[1] = impulse[1];
    v[2] = impulse[2];
    v[0] = impulse[0];
    v[3] = 0.0f;
    uint8_t* body = Field<uint8_t*>(obj, 0x1c4);
    WakeEntity(body);
    return VCall<uint32_t>(Motion(body), 0x64 / 4, v);
}

static uint32_t Body_ApplyAngularImpulse_Abi(uint32_t eax, uint32_t edx)
{
    return Body_ApplyAngularImpulse(reinterpret_cast<const float*>(eax), B(edx));
}
ABI_ENTRY_004583d0(Body_ApplyAngularImpulse_Entry, Body_ApplyAngularImpulse_Abi)
HOOK(0x004583d0, "Body_ApplyAngularImpulse", Body_ApplyAngularImpulse_Entry, nullptr);

// 0x00458440 (eax = point or null, ecx = impulse, edx = object; 12+ callers): wakes the body and applies the impulse
// (x, y, z, 0) at the point (motion virtual +0x60, applyPointImpulse(impulse, point)) or at the centre (+0x5c).
uint32_t Body_ApplyImpulse(const float* point, const float* impulse, uint8_t* obj)
{
    alignas(16) float v[4];
    alignas(16) float at[4];
    v[1] = impulse[1];
    uint8_t* body = Field<uint8_t*>(obj, 0x1c4);
    v[2] = impulse[2];
    v[0] = impulse[0];
    v[3] = 0.0f;
    if (point)
    {
        at[1] = point[1];
        at[0] = point[0];
        at[2] = point[2];
        at[3] = 0.0f;
        WakeEntity(body);
        return VCall<uint32_t>(Motion(body), 0x60 / 4, v, at);
    }
    WakeEntity(body);
    return VCall<uint32_t>(Motion(body), 0x5c / 4, v);
}

static uint32_t Body_ApplyImpulse_Abi(uint32_t eax, uint32_t ecx, uint32_t edx)
{
    return Body_ApplyImpulse(reinterpret_cast<const float*>(eax), reinterpret_cast<const float*>(ecx), B(edx));
}
ABI_ENTRY_00458440(Body_ApplyImpulse_Entry, Body_ApplyImpulse_Abi)
HOOK(0x00458440, "Body_ApplyImpulse", Body_ApplyImpulse_Entry, nullptr);

// 0x00458510 Body_setLinearVelocity (eax = velocity (3 floats), edx = object): wakes the body and calls motion
// virtual +0x50 with (x, y, z, 0). Returns the virtual's eax (edx not reproduced).
uint64_t Body_setLinearVelocity(const float* velocity, uint8_t* obj)
{
    alignas(16) float v[4];
    v[1] = velocity[1];
    v[2] = velocity[2];
    v[0] = velocity[0];
    v[3] = 0.0f;
    uint8_t* body = Field<uint8_t*>(obj, 0x1c4);
    WakeEntity(body);
    return VCall<uint32_t>(Motion(body), 0x50 / 4, v);
}

static uint64_t Body_setLinearVelocity_Abi(uint32_t eax, uint32_t edx)
{
    return Body_setLinearVelocity(reinterpret_cast<const float*>(eax), B(edx));
}
ABI_ENTRY_00458510(Body_setLinearVelocity_Entry, Body_setLinearVelocity_Abi)
HOOK(0x00458510, "Body_setLinearVelocity", Body_setLinearVelocity_Entry, nullptr);

// 0x00458580 Body_setAngularVelocity (eax = velocity (3 floats), edx = object): wakes the body and calls motion
// virtual +0x54 with (x, y, z, 0).
uint32_t Body_setAngularVelocity(const float* velocity, uint8_t* obj)
{
    alignas(16) float v[4];
    v[1] = velocity[1];
    v[2] = velocity[2];
    v[0] = velocity[0];
    v[3] = 0.0f;
    uint8_t* body = Field<uint8_t*>(obj, 0x1c4);
    WakeEntity(body);
    return VCall<uint32_t>(Motion(body), 0x54 / 4, v);
}

static uint32_t Body_setAngularVelocity_Abi(uint32_t eax, uint32_t edx)
{
    return Body_setAngularVelocity(reinterpret_cast<const float*>(eax), B(edx));
}
ABI_ENTRY_00458580(Body_setAngularVelocity_Entry, Body_setAngularVelocity_Abi)
HOOK(0x00458580, "Body_setAngularVelocity", Body_setAngularVelocity_Entry, nullptr);

// ================================================================================================================
// Ragdolls
// ================================================================================================================

// The wake idiom of 0x00458dd0 (world tested first, no island null test).
static void WakeEntityWorldFirst(uint8_t* entity)
{
    uint8_t* world = Field<uint8_t*>(entity, 0x44);
    if (world)
    {
        uint8_t* island = Field<uint8_t*>(entity, 0x10);
        if (!island[0x29] && Field<int32_t>(island, 0x24) != -1)
            orig::fn_004ad130(u(world), u(island));
    }
}
// A keyframed body (motion type 4) gets its original motion back: motion +0xc0 into entity +8, 0x004b5a00 on the
// keyframed motion, which is then deleted; entity +0x28 = motion +0x60.
static void RestoreDynamicMotion(uint8_t* entity)
{
    if (VCall<int32_t>(Motion(entity), 0x18 / 4) != 4)
        return;
    VCall<int32_t>(Motion(entity), 0x18 / 4);
    uint8_t* keyframed = Motion(entity);
    Field<uint32_t>(entity, 8) = Field<uint32_t>(keyframed, 0xc0);
    orig::fn_004b5a00(u(keyframed));
    Field<uint32_t>(keyframed, 0xc0) = 0;
    VCall<void>(keyframed, 0, 1u);
    Field<uint32_t>(entity, 0x28) = Field<uint32_t>(entity, 8) + 0x60;
}
// The stamp reference count idiom: ++(+0x410 +8), never 0; without a stamp "Error: Can't generate stamp code, no
// cache" is logged and 0 used.
static uint32_t StampAddRef(uint8_t* obj)
{
    uint8_t* stamp = Field<uint8_t*>(obj, 0x410);
    if (!stamp)
    {
        GameLog(reinterpret_cast<const char*>(0x0058af8c));
        return 0;
    }
    if (++Field<int32_t>(stamp, 8) == 0)
        Field<int32_t>(stamp, 8) = 1;
    return Field<uint32_t>(stamp, 8);
}

// 0x00458970 (stack: object, fromAnimation (byte), bake (byte); callee pops 0xc; callers 0x00413a90, 0x00459000).
// Poses the ragdoll bodies. fromAnimation: the default animation is evaluated into a local pose (0x00458950,
// 0x004087d0, Anim_Set 0x00408d90(flags 0x90), 0x00408ee0) and applied with 0x004543f0(pose); otherwise
// 0x004543f0(0, 1, 0). The object matrix comes from virtual +0x60 (ragdolled) or +0x74. For each ragdoll record and
// part descriptor (definition +0x18) with a body: velocities zeroed (wake, motion +0x50 / +0x54), the part's bone
// matrix (part +4 +0x8c +0xc) times the stamp's bone matrix (+0x410 +0x18, 0x48 bytes per bone) with the translation
// scaled by the object scale, times the object matrix, becomes the body's position and rotation (0x00457ab0, motion
// virtual +0x48; a flagged body is taken out of / put back into its world). bake: the stamp matrix becomes the scaled
// bone matrix times the object matrix with the translation unscaled, its +0x44 = +0x1d0 and the other stamp's +0x44
// = 0. fromAnimation: the record's +8 becomes the inverse of the part's bone matrix (0x004e2b40). Returns the part
// cursor.
uint32_t fn_00458970(uint8_t* obj, uint32_t fromAnimation, uint32_t bake)
{
    Frame<0x1a0> f;  // the original's frame (offsets from esp after the three register pushes)
    orig::fn_004535b0(f.A(0x1c), u(obj));
    const uint32_t id = orig::fn_00453100(u(obj));
    if (static_cast<uint8_t>(fromAnimation))
    {
        const uint32_t animation = orig::fn_00458950(id, u(obj));
        orig::fn_004087d0(f.A(0xb0), u(obj));
        orig::fn_00408d90(animation, u(obj), 0, 0x90, 0, 0, f.A(0xb0), 0);
        const uint32_t pose = orig::fn_00408ee0(f.A(0x130), u(obj), f.A(0xb0), id);
        const uint32_t refs = StampAddRef(obj);
        orig::fn_004543f0(f.A(0x130), u(obj), refs, pose, 0, 0);
    }
    else
    {
        const uint32_t refs = StampAddRef(obj);
        orig::fn_004543f0(0, u(obj), refs, 0, 1, 0);
    }
    f.L(0x14) = obj[0x421] ? VCall<uint32_t>(obj, 0x60 / 4) : VCall<uint32_t>(obj, 0x74 / 4);
    f.L(0x10) = Field<uint32_t>(obj, 0x1bc);
    f.L(0x3c) = 0;
    f.L(0x38) = 0;
    f.L(0x34) = 0;
    f.L(0x30) = 0;
    const uint32_t id2 = orig::fn_00453100(u(obj));
    const uint32_t slot = orig::fn_00408800(4, u(obj));
    uint32_t cursor = orig::fn_00453120(u(obj), id2, *reinterpret_cast<uint32_t*>(slot)) + 0x18;
    uint8_t* entity = B(*reinterpret_cast<uint32_t*>(f.L(0x10)));
    f.L(0xc) = u(entity);
    if (!entity)
        return cursor;
    do
    {
        uint8_t* part = B(*reinterpret_cast<uint32_t*>(cursor));
        cursor += 4;
        f.L(0x18) = u(part);
        f.L(0x28) = cursor;
        if (Field<uint32_t>(part, 8))
        {
            const uint32_t bone = Field<uint16_t>(Field<uint8_t*>(part, 0xc), 0xa);
            uint8_t* stamp = Field<uint8_t*>(obj, 0x410);
            uint8_t* boneMatrix = B(Field<uint32_t>(stamp, 0x18) + bone * 0x48);
            f.L(0x2c) = Field<uint32_t>(stamp, 0x1c) + bone * 0x48;
            WakeEntity(entity);
            VCall<void>(Motion(entity), 0x50 / 4, f.A(0x30));
            entity = B(*reinterpret_cast<uint32_t*>(f.L(0x10)));
            f.L(0xc) = u(entity);
            WakeEntity(entity);
            VCall<void>(Motion(entity), 0x54 / 4, f.A(0x30));
            orig::fn_004e2910(Field<uint32_t>(Field<uint8_t*>(part, 4), 0x8c) + 0xc, f.A(0x68), u(boneMatrix));
            f.F(0x74) = f.F(0x74) * f.F(0x1c);
            f.F(0x84) = f.F(0x84) * f.F(0x20);
            f.F(0x94) = f.F(0x94) * f.F(0x24);
            orig::fn_004e2910(f.A(0x68), f.A(0x130), f.L(0x14));
            if (static_cast<uint8_t>(bake))
            {
                std::memcpy(f.b + 0xb0, boneMatrix, 0x44);
                f.F(0xbc) = f.F(0xbc) * f.F(0x1c);
                f.F(0xcc) = f.F(0xcc) * f.F(0x20);
                f.F(0xdc) = f.F(0xdc) * f.F(0x24);
                orig::fn_004e2910(f.A(0xb0), u(boneMatrix), f.L(0x14));
                Field<float>(boneMatrix, 0xc) = Field<float>(boneMatrix, 0xc) / f.F(0x1c);
                Field<float>(boneMatrix, 0x1c) = Field<float>(boneMatrix, 0x1c) / f.F(0x20);
                Field<float>(boneMatrix, 0x2c) = Field<float>(boneMatrix, 0x2c) / f.F(0x24);
                Field<uint32_t>(boneMatrix, 0x44) = Field<uint32_t>(obj, 0x1d0);
                Field<uint32_t>(B(f.L(0x2c)), 0x44) = 0;
            }
            Matrix_ToPosQuat(reinterpret_cast<const float*>(f.b + 0x130), reinterpret_cast<float*>(f.b + 0x40),
                             reinterpret_cast<float*>(f.b + 0x50));
            uint8_t* body = Field<uint8_t*>(obj, 0x1c4);
            uint32_t world = 0;
            if (body[0xc] & 1)
            {
                world = Field<uint32_t>(body, 0x44);
                if (world)
                    orig::fn_004ac0e0(world, u(body));
            }
            f.L(0xc) = world;
            uint8_t* e = B(*reinterpret_cast<uint32_t*>(f.L(0x10)));
            WakeEntity(e);
            if (!((e[0xc] & 1) && Field<uint32_t>(e, 0x44)))
                VCall<void>(Motion(e), 0x48 / 4, f.A(0x50), f.A(0x40));
            if (f.L(0xc))
                orig::fn_004aba90(f.L(0xc), Field<uint32_t>(obj, 0x1c4));
            if (static_cast<uint8_t>(fromAnimation))
                orig::fn_004e2b40(Field<uint32_t>(Field<uint8_t*>(B(f.L(0x18)), 4), 0x8c) + 0xc, f.L(0x10) + 8);
            cursor = f.L(0x28);
        }
        f.L(0x10) += 0x4c;
        entity = B(*reinterpret_cast<uint32_t*>(f.L(0x10)));
        f.L(0xc) = u(entity);
    } while (entity);
    return cursor;
}

static uint32_t fn_00458970_Abi(uint32_t a0, uint32_t a1, uint32_t a2)
{
    return fn_00458970(B(a0), a1, a2);
}
ABI_ENTRY_00458970(fn_00458970_Entry, fn_00458970_Abi)
HOOK(0x00458970, "fn_00458970", fn_00458970_Entry, nullptr);

// 0x00458db0 (eax = object; caller 0x004547b0): removes the body (+0x1c4) from its world (0x004ac0e0) when it has
// one. Returns eax as left.
uint32_t Object_RemoveBody(uint8_t* obj)
{
    uint8_t* body = Field<uint8_t*>(obj, 0x1c4);
    if (!body || !Field<uint32_t>(body, 0x44))
        return u(body);
    return orig::fn_004ac0e0(Field<uint32_t>(body, 0x44), u(body));
}

static uint32_t Object_RemoveBody_Abi(uint32_t eax)
{
    return Object_RemoveBody(B(eax));
}
ABI_ENTRY_00458db0(Object_RemoveBody_Entry, Object_RemoveBody_Abi)
HOOK(0x00458db0, "Object_RemoveBody", Object_RemoveBody_Entry, nullptr);

// 0x00458dd0 (stack: object, enable (byte), keyframed (byte), flags; callee pops 0x10; callers 0x004527d0,
// 0x004547b0, 0x00459000). For each ragdoll body (+0x1bc records): enable: added to the world when outside it;
// keyframed: made keyframed (0x004aff70(1)), woken, object virtual +0xf8(flags | 0x80, body); else made dynamic
// again (original motion restored), woken, +0xf8(flags | 0x860020, body). Disable: +0xf8(0x4000, body), keyframed,
// velocities zeroed, island bookkeeping (0x004ad070 when the island is active) and removed from the world. Returns 0.
uint32_t fn_00458dd0(uint8_t* obj, uint32_t enable, uint32_t keyframed, uint32_t flags)
{
    Frame<0x20> f;  // offsets from esp after the three register pushes
    uint32_t* record = Field<uint32_t*>(obj, 0x1bc);
    if (!record)
        return 0;
    f.L(0x1c) = 0;
    f.L(0x18) = 0;
    f.L(0x14) = 0;
    f.L(0x10) = 0;
    if (!record[0])
        return 0;
    do
    {
        uint8_t* entity = B(record[0]);
        if (static_cast<uint8_t>(enable))
        {
            if (!Field<uint32_t>(entity, 0x44))
                orig::fn_004aba90(Field<uint32_t>(Field<uint8_t*>(g_game, 0xa8c), 8), u(entity));
            if (static_cast<uint8_t>(keyframed))
            {
                orig::fn_004aff70(record[0], 1);
                WakeEntityWorldFirst(B(record[0]));
                VCall<void>(obj, 0xf8 / 4, flags | 0x80, record[0]);
            }
            else
            {
                RestoreDynamicMotion(B(record[0]));
                WakeEntityWorldFirst(B(record[0]));
                VCall<void>(obj, 0xf8 / 4, flags | 0x860020, record[0]);
            }
        }
        else
        {
            VCall<void>(obj, 0xf8 / 4, 0x4000u, record[0]);
            orig::fn_004aff70(record[0], 1);
            WakeEntity(B(record[0]));
            VCall<void>(Motion(B(record[0])), 0x50 / 4, f.A(0x10));
            WakeEntity(B(record[0]));
            VCall<void>(Motion(B(record[0])), 0x54 / 4, f.A(0x10));
            uint8_t* e = B(record[0]);
            const uint32_t world = Field<uint32_t>(e, 0x44);
            if (world)
            {
                uint8_t* island = Field<uint8_t*>(e, 0x10);
                if (island[0x29] && Field<int32_t>(island, 0x24) != -1)
                    orig::fn_004ad070(world, u(e));
                orig::fn_004ac0e0(Field<uint32_t>(Field<uint8_t*>(g_game, 0xa8c), 8), record[0]);
            }
        }
        record += 0x4c / 4;
    } while (record[0]);
    return 0;
}

static uint32_t fn_00458dd0_Abi(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_00458dd0(B(a0), a1, a2, a3);
}
ABI_ENTRY_00458dd0(fn_00458dd0_Entry, fn_00458dd0_Abi)
HOOK(0x00458dd0, "fn_00458dd0", fn_00458dd0_Entry, nullptr);

// 0x00459000 (al = ragdoll on, esi = object; stack: fromAnimation (byte), a1, a2; callee pops 0xc; callers
// 0x00405d10, 0x0040abc0, 0x0043ee90 ...). guess: switches the object to / from ragdoll. On: scale (0x004535b0);
// when not yet ragdolled, saves 0x004592c0(0, object) into +0x41c and the keyframed state into +0x420, keyframes the
// body (Object_setPhysicsEnabled 1), virtual +0xf8(0x4000, 0), enables the ragdoll bodies (0x00458dd0(1, 0, a2)) and
// sets +0x421. fromAnimation: poses them (0x00458970(a1, 1)); otherwise each part's stamp matrix (+0x410 +0x18) becomes
// record +8 times the body transform (0x004de840 / 0x00457ba0) with the translation divided by the scale, +0x44 =
// +0x1d0 (the other stamp's = 0). Off (when ragdolled): disables them (0x00458dd0(0, 0, 0)), restores the keyframed
// state and virtual +0xf8(+0x41c, 0), clears +0x421. Returns the previous +0x421 in AL.
uint32_t Object_SetRagdoll(uint32_t on, uint8_t* obj, uint32_t fromAnimation, uint32_t a1, uint32_t a2)
{
    Frame<0xd0> f;  // offsets from esp after the pushes of ebx and edi
    const uint8_t was = obj[0x421];
    f.C(0xb) = was;
    if (!static_cast<uint8_t>(on))
    {
        if (was)
        {
            fn_00458dd0(obj, 0, 0, 0);
            Object_setPhysicsEnabled(obj, obj[0x420]);
            VCall<void>(obj, 0xf8 / 4, Field<uint32_t>(obj, 0x41c), 0u);
            obj[0x421] = 0;
        }
        return was;
    }
    orig::fn_004535b0(f.A(0x10), u(obj));
    if (!was)
    {
        Field<uint32_t>(obj, 0x41c) = orig::fn_004592c0(0, u(obj));
        obj[0x420] = static_cast<uint8_t>(Body_isKeyframed(obj));
        Object_setPhysicsEnabled(obj, 1);
        VCall<void>(obj, 0xf8 / 4, 0x4000u, 0u);
        fn_00458dd0(obj, 1, 0, a2);
        obj[0x421] = 1;
    }
    if (static_cast<uint8_t>(fromAnimation))
    {
        fn_00458970(obj, a1, 1);
        return was;
    }
    f.L(0xc) = Field<uint32_t>(obj, 0x1bc);
    const uint32_t id = orig::fn_00453100(u(obj));
    const uint32_t slot = orig::fn_00408800(4, u(obj));
    uint32_t cursor = orig::fn_00453120(u(obj), id, *reinterpret_cast<uint32_t*>(slot)) + 0x18;
    uint8_t* entity = B(*reinterpret_cast<uint32_t*>(f.L(0xc)));
    if (!entity)
        return was;
    f.F(0x24) = K(0x005bcb2c) / f.F(0x10);
    f.F(0x20) = K(0x005bcb2c) / f.F(0x14);
    f.F(0x28) = K(0x005bcb2c) / f.F(0x18);
    do
    {
        uint8_t* part = B(*reinterpret_cast<uint32_t*>(cursor));
        cursor += 4;
        f.L(0x2c) = cursor;
        const uint32_t bone = Field<uint16_t>(Field<uint8_t*>(part, 0xc), 0xa);
        uint8_t* stamp = Field<uint8_t*>(obj, 0x410);
        uint8_t* boneMatrix = B(Field<uint32_t>(stamp, 0x18) + bone * 0x48);
        uint8_t* other = B(Field<uint32_t>(stamp, 0x1c) + bone * 0x48);
        uint8_t* motion = Motion(entity);
        f.L(0x38) = Field<uint32_t>(motion, 0xb8);
        f.L(0x3c) = Field<uint32_t>(motion, 0xbc);
        f.F(0xc0) = Field<float>(motion, 0xb0);
        f.L(0x10) = Field<uint32_t>(motion, 0x30);
        f.F(0xc4) = Field<float>(motion, 0xb4);
        f.L(0x14) = Field<uint32_t>(motion, 0x34);
        f.L(0x18) = Field<uint32_t>(motion, 0x38);
        f.L(0xc8) = f.L(0x38);
        f.L(0xcc) = f.L(0x3c);
        f.L(0x1c) = Field<uint32_t>(motion, 0x3c);
        orig::fn_004de840(f.A(0x10), f.A(0x90));
        orig::fn_00457ba0(f.A(0x48), f.A(0x90));
        f.F(0x54) = f.F(0x24) * f.F(0x54);
        f.F(0x64) = f.F(0x20) * f.F(0x64);
        f.F(0x74) = f.F(0x28) * f.F(0x74);
        orig::fn_004e2910(f.L(0xc) + 8, u(boneMatrix), f.A(0x48));
        Field<uint32_t>(boneMatrix, 0x44) = Field<uint32_t>(obj, 0x1d0);
        f.L(0xc) += 0x4c;
        Field<uint32_t>(other, 0x44) = 0;
        entity = B(*reinterpret_cast<uint32_t*>(f.L(0xc)));
        cursor = f.L(0x2c);
    } while (entity);
    return f.C(0xb);
}

static uint32_t Object_SetRagdoll_Abi(uint32_t eax, uint32_t esi, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return Object_SetRagdoll(eax, B(esi), a0, a1, a2);
}
ABI_ENTRY_00459000(Object_SetRagdoll_Entry, Object_SetRagdoll_Abi)
HOOK(0x00459000, "Object_SetRagdoll", Object_SetRagdoll_Entry, nullptr);

// 0x00459260 (ecx = object; stack: filter info, entity or 0; callee pops 8; callers 0x00432630, 0x00436720). Sets the
// collision filter info (+0x20) of the entity (default: the object's body) and, when it is in a world, re-filters it
// (the original tail-jumps to 0x004ac850(world, entity)) unless it is flagged (+0xc bit 0), the manager's deferred
// flag (Game+0xa8c +0x60) is set or the world is locked (+0x13d); then the deferred flag is set instead.
uint32_t Body_SetCollisionFilter(uint8_t* obj, uint32_t filter, uint8_t* entity)
{
    if (!entity)
        entity = Field<uint8_t*>(obj, 0x1c4);
    Field<uint32_t>(entity, 0x20) = filter;
    if (!Field<uint32_t>(entity, 0x44))
        return u(entity);
    uint8_t* manager = Field<uint8_t*>(g_game, 0xa8c);
    if (!(entity[0xc] & 1) && !manager[0x60])
    {
        uint8_t* world = Field<uint8_t*>(manager, 8);
        if (!world[0x13d])
            return orig::fn_004ac850(u(world), u(entity));
    }
    Field<uint8_t*>(g_game, 0xa8c)[0x60] = 1;
    return u(entity);
}

static uint32_t Body_SetCollisionFilter_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return Body_SetCollisionFilter(B(ecx), a0, B(a1));
}
ABI_ENTRY_00459260(Body_SetCollisionFilter_Entry, Body_SetCollisionFilter_Abi)
HOOK(0x00459260, "Body_SetCollisionFilter", Body_SetCollisionFilter_Entry, nullptr);

// 0x004594b0 (eax = passed on as 0x00459630's unused argument, edi = object; caller 0x0045da90). guess: pins the
// object to the world. Drops a previous constraint (0x00459630); unless one remains (+0x1c8) or the body is not in a
// world: the point 10 above the object's position (virtual +0x54) is expressed in the local frames of the world's
// fixed body (world +0x60) and of the object's body (motion +0x80 rotation, translation +0xb0), and a ball-socket
// constraint (0x60 bytes, class 0x22, 0x004b0230) between them is stored in +0x1c8 and added to the world
// (0x004ac3e0).
uint32_t fn_004594b0(uint32_t eax, uint8_t* obj)
{
    Frame<0x50> f;  // offsets from esp after the push of esi
    const uint32_t r0 = fn_00459630(obj, eax);
    if (Field<uint32_t>(obj, 0x1c8))
        return Field<uint32_t>(obj, 0x1c8);
    if (!Field<uint32_t>(Field<uint8_t*>(obj, 0x1c4), 0x44))
        return r0;
    VCall<void>(obj, 0x54 / 4, f.A(0x14));
    f.F(0x1c) = f.F(0x1c) + K(0x005bcb68);  // + 10.0
    uint8_t* fixed = Field<uint8_t*>(Field<uint8_t*>(Field<uint8_t*>(g_game, 0xa8c), 8), 0x60);
    f.L(0x20) = u(fixed);
    const float* r = &Field<float>(Motion(fixed), 0x80);
    const float y = f.F(0x18);
    const float z = f.F(0x1c);
    const float dx = f.F(0x14) - r[12];
    f.L(0x24) = Field<uint32_t>(obj, 0x1c4);
    const float dy = y - r[13];
    const float dz = z - r[14];
    f.F(0x30) = (dz * r[2] + dy * r[1]) + dx * r[0];
    f.F(0x34) = (dz * r[6] + dy * r[5]) + dx * r[4];
    f.F(0x38) = (dz * r[10] + dy * r[9]) + dx * r[8];
    const float* b = &Field<float>(Motion(Field<uint8_t*>(obj, 0x1c4)), 0x80);
    f.F(0x10) = f.F(0x14) - b[12];
    f.F(0xc) = y - b[13];
    const float ez = z - b[14];
    f.F(0x40) = (ez * b[2] + f.F(0xc) * b[1]) + f.F(0x10) * b[0];
    f.F(0x44) = (ez * b[6] + f.F(0xc) * b[5]) + f.F(0x10) * b[4];
    f.F(0x48) = (ez * b[10] + f.F(0xc) * b[9]) + f.F(0x10) * b[8];
    uint8_t* memory = HkAlloc(0x60, 0x22);
    Field<uint16_t>(memory, 4) = 0x60;
    const uint32_t constraint = orig::fn_004b0230(u(memory), f.A(0x20));
    Field<uint32_t>(obj, 0x1c8) = constraint;
    return orig::fn_004ac3e0(Field<uint32_t>(Field<uint8_t*>(g_game, 0xa8c), 8), constraint);
}

static uint32_t fn_004594b0_Abi(uint32_t eax, uint32_t edi)
{
    return fn_004594b0(eax, B(edi));
}
ABI_ENTRY_004594b0(fn_004594b0_Entry, fn_004594b0_Abi)
HOOK(0x004594b0, "fn_004594b0", fn_004594b0_Entry, nullptr);

// 0x00459630 (esi = object; stack: one unused argument; callee pops 4; callers 0x004547b0, 0x00457870, 0x00457990,
// 0x004594b0, 0x0045da90). Drops the object's constraint +0x1c8: removed from its world (+0x14, 0x004ac460(eax =
// world)) and released; +0x1c8 = 0.
uint32_t fn_00459630(uint8_t* obj, uint32_t /*unused*/)
{
    uint8_t* constraint = Field<uint8_t*>(obj, 0x1c8);
    if (!constraint)
        return 0;
    if (Field<uint32_t>(constraint, 0x14))
        orig::fn_004ac460(Field<uint32_t>(Field<uint8_t*>(g_game, 0xa8c), 8), u(constraint));
    HkRelease(Field<uint8_t*>(obj, 0x1c8));
    Field<uint32_t>(obj, 0x1c8) = 0;
    return 0;
}

static uint32_t fn_00459630_Abi(uint32_t esi, uint32_t a0)
{
    return fn_00459630(B(esi), a0);
}
ABI_ENTRY_00459630(fn_00459630_Entry, fn_00459630_Abi)
HOOK(0x00459630, "fn_00459630", fn_00459630_Entry, nullptr);

// 0x00459680 (stack: entity, velocity; callee pops 8; caller 0x0048cec0): wakes the body and calls motion virtual
// +0x54 (setAngularVelocity).
uint32_t Body_wakeAndSetAngularVelocity(uint8_t* entity, const float* velocity)
{
    WakeEntity(entity);
    return VCall<uint32_t>(Motion(entity), 0x54 / 4, velocity);
}

static uint32_t Body_wakeAndSetAngularVelocity_Abi(uint32_t a0, uint32_t a1)
{
    return Body_wakeAndSetAngularVelocity(B(a0), reinterpret_cast<const float*>(a1));
}
ABI_ENTRY_00459680(Body_wakeAndSetAngularVelocity_Entry, Body_wakeAndSetAngularVelocity_Abi)
HOOK(0x00459680, "Body_wakeAndSetAngularVelocity", Body_wakeAndSetAngularVelocity_Entry, nullptr);

// 0x004596d0 (stack: entity, position, rotation; callee pops 0xc; caller 0x00457990): wakes the body and, unless it
// is flagged (+0xc bit 0) while in a world, calls motion virtual +0x48 (setPositionAndRotation).
uint32_t Body_wakeAndSetPositionAndRotation(uint8_t* entity, const float* position, const float* rotation)
{
    WakeEntity(entity);
    if ((entity[0xc] & 1) && Field<uint32_t>(entity, 0x44))
        return Field<uint32_t>(entity, 0x44);
    return VCall<uint32_t>(Motion(entity), 0x48 / 4, position, rotation);
}

static uint32_t Body_wakeAndSetPositionAndRotation_Abi(uint32_t a0, uint32_t a1, uint32_t a2)
{
    return Body_wakeAndSetPositionAndRotation(B(a0), reinterpret_cast<const float*>(a1),
                                              reinterpret_cast<const float*>(a2));
}
ABI_ENTRY_004596d0(Body_wakeAndSetPositionAndRotation_Entry, Body_wakeAndSetPositionAndRotation_Abi)
HOOK(0x004596d0, "Body_wakeAndSetPositionAndRotation", Body_wakeAndSetPositionAndRotation_Entry, nullptr);

// 0x00459730 (eax = Havok rigid body cinfo; callers 0x00457030 and Havok's world ctor 0x004addb0 (ICF-folded
// helper)): defaults: u16 +0 = 0, u16 +2 = 0xffff, +4 / +8 / +0x74 = 0, +0x78 = 0.05, +0x7c = 0.5, +0x80 = 0.4,
// +0x84 = 6, +0x88 = 1, +0x70 = -1.0, position +0x10 and rotation +0x20 (identity quaternion) zeroed, inertia
// +0x30..+0x5c = diagonal -1.0, centre of mass +0x60 = 0.
void RigidBodyCinfo_Init(uint8_t* c)
{
    Field<uint16_t>(c, 0) = 0;
    Field<uint16_t>(c, 2) = 0xffff;
    Field<uint32_t>(c, 4) = 0;
    Field<uint32_t>(c, 8) = 0;
    Field<uint32_t>(c, 0x74) = 0;
    Field<uint32_t>(c, 0x78) = 0x3d4ccccd;
    Field<uint32_t>(c, 0x7c) = 0x3f000000;
    Field<uint32_t>(c, 0x80) = 0x3ecccccd;
    Field<uint32_t>(c, 0x84) = 6;
    Field<uint32_t>(c, 0x88) = 1;
    Field<uint32_t>(c, 0x70) = 0xbf800000;
    for (uint32_t o = 0x10; o < 0x20; o += 4)
        Field<uint32_t>(c, o) = 0;
    Field<uint32_t>(c, 0x20) = 0;
    Field<uint32_t>(c, 0x24) = 0;
    Field<uint32_t>(c, 0x28) = 0;
    Field<uint32_t>(c, 0x2c) = 0x3f800000;
    for (uint32_t o = 0x30; o < 0x60; o += 4)
        Field<uint32_t>(c, o) = 0;
    Field<uint32_t>(c, 0x30) = 0xbf800000;
    Field<uint32_t>(c, 0x44) = 0xbf800000;
    Field<uint32_t>(c, 0x58) = 0xbf800000;
    for (uint32_t o = 0x60; o < 0x70; o += 4)
        Field<uint32_t>(c, o) = 0;
}

static void RigidBodyCinfo_Init_Abi(uint32_t eax)
{
    RigidBodyCinfo_Init(B(eax));
}
ABI_ENTRY_00459730(RigidBodyCinfo_Init_Entry, RigidBodyCinfo_Init_Abi)
HOOK(0x00459730, "RigidBodyCinfo_Init", RigidBodyCinfo_Init_Entry, nullptr);

// 0x004597d0 (ecx = record; no direct callers: the element constructor 0x00456a90 passes to 0x00403c40): ragdoll body
// record (0x4c bytes): +0 = 0, +4 = 0, identity matrix at +8 (0x44 bytes). Returns the record.
uint32_t BodyRecord_Construct(uint8_t* record)
{
    Field<uint32_t>(record, 0) = 0;
    Field<uint32_t>(record, 4) = 0;
    std::memset(record + 8, 0, 0x44);
    Field<uint32_t>(record, 8) = 0x3f800000;
    Field<uint32_t>(record, 8 + 0x14) = 0x3f800000;
    Field<uint32_t>(record, 8 + 0x28) = 0x3f800000;
    Field<uint32_t>(record, 8 + 0x3c) = 0x3f800000;
    return u(record);
}

static uint32_t BodyRecord_Construct_Abi(uint32_t ecx)
{
    return BodyRecord_Construct(B(ecx));
}
ABI_ENTRY_004597d0(BodyRecord_Construct_Entry, BodyRecord_Construct_Abi)
HOOK(0x004597d0, "BodyRecord_Construct", BodyRecord_Construct_Entry, nullptr);

// 0x00459810 (eax = hinge axis (vec4), ecx = body A, edx = body B, esi = hinge cinfo out; stack: pivot (world);
// callee pops 4; caller 0x00456570; not executed in recorded runs). Fills a plain hinge constraint cinfo: +0 / +4
// bodies; the axis is normalised (0 for a zero axis) and rotated into each body's frame by the inverse of its rotation
// quaternion (motion +0x30, the inlined v' = (2w^2 - 1)v + 2(q.v)q - 2w(v x q) form, constant -2 at 0x005bcc40): A's
// into the frame (+0x30 of the original's stack), B's into +0x30; the pivot into each body's local space (0x004de720
// with motion +0x80) at +0x10 / +0x20; +0x40 = a vector perpendicular to A's axis (smallest component zeroed, the
// other two swapped and one negated) and +0x50 = axis x perpendicular (+0x5c = 0). Returns the last index * 4.
uint32_t fn_00459810(const float* axis, uint8_t* bodyA, uint8_t* bodyB, uint8_t* out, const float* pivot)
{
    Frame<0x50> f;  // offsets from esp after the pushes of ebx and edi
    Field<uint8_t*>(out, 0) = bodyA;
    Field<uint8_t*>(out, 4) = bodyB;
    std::memcpy(f.b + 0x20, axis, 0x10);
    float len2 = f.F(0x28) * f.F(0x28);
    len2 = len2 + f.F(0x24) * f.F(0x24);
    len2 = len2 + f.F(0x20) * f.F(0x20);
    float inv;
    if (len2 == K(0x0056c05c))
        inv = K(0x0056c05c);
    else
        inv = K(0x005bcb2c) / std::sqrt(len2);
    const float x = f.F(0x20) * inv;
    f.F(0x24) = f.F(0x24) * inv;
    f.F(0x28) = f.F(0x28) * inv;
    f.F(0x2c) = f.F(0x2c) * inv;
    const float y = f.F(0x24);
    const float z = f.F(0x28);
    const float w = f.F(0x2c);
    const float k = K(0x005bcc40);  // -2.0

    // Body A.
    const float* qa = &Field<float>(Motion(bodyA), 0x30);
    float s = qa[3] * qa[3];
    s = s + s;
    s = s - K(0x005bcb2c);
    float d = z * qa[2] + y * qa[1];
    d = d + x * qa[0];
    d = d + d;
    f.F(0x1c) = qa[3] * k;
    f.F(0x40) = z * qa[1] - y * qa[2];
    f.F(0x44) = x * qa[2] - z * qa[0];
    f.F(0x48) = y * qa[0] - x * qa[1];
    const float sx = s * x;
    const float ys = y * s;
    f.F(0x38) = z * s;
    f.F(0x3c) = w * s;
    f.F(0x30) = d * qa[0] + sx;
    f.F(0x34) = d * qa[1] + ys;
    f.F(0x38) = d * qa[2] + f.F(0x38);
    const float tw = d * qa[3] + f.F(0x3c);
    f.F(0x30) = f.F(0x40) * f.F(0x1c) + f.F(0x30);
    f.F(0x34) = f.F(0x44) * f.F(0x1c) + f.F(0x34);
    f.F(0x38) = f.F(0x48) * f.F(0x1c) + f.F(0x38);
    f.F(0x3c) = f.F(0x1c) * K(0x0056c05c) + tw;

    // Body B (into the cinfo).
    const float* qb = &Field<float>(Motion(bodyB), 0x30);
    float s2 = qb[3] * qb[3];
    s2 = s2 + s2;
    f.F(0x14) = s2 - K(0x005bcb2c);
    float d2 = z * qb[2] + y * qb[1];
    d2 = d2 + x * qb[0];
    f.F(0x18) = d2 + d2;
    f.F(0x1c) = qb[3] * k;
    f.F(0x40) = z * qb[1] - y * qb[2];
    f.F(0x44) = x * qb[2] - z * qb[0];
    f.F(0x48) = y * qb[0] - x * qb[1];
    float* o = reinterpret_cast<float*>(out + 0x30);
    const float s2x = f.F(0x14) * x;
    o[0] = s2x;
    const float ys2 = y * f.F(0x14);
    o[1] = ys2;
    f.F(0xc) = z * f.F(0x14);
    o[2] = f.F(0xc);
    f.F(0x10) = w * f.F(0x14);
    o[3] = f.F(0x10);
    o[0] = f.F(0x18) * qb[0] + s2x;
    o[1] = f.F(0x18) * qb[1] + ys2;
    o[2] = f.F(0x18) * qb[2] + f.F(0xc);
    o[3] = f.F(0x18) * qb[3] + f.F(0x10);
    o[0] = f.F(0x40) * f.F(0x1c) + o[0];
    o[1] = f.F(0x44) * f.F(0x1c) + o[1];
    o[2] = f.F(0x48) * f.F(0x1c) + o[2];
    o[3] = f.F(0x1c) * K(0x0056c05c) + o[3];

    orig::fn_004de720(u(out) + 0x10, u(Motion(bodyA)) + 0x80, u(pivot));
    orig::fn_004de720(u(out) + 0x20, u(Motion(bodyB)) + 0x80, u(pivot));

    // Perpendicular to A's axis: zero the smallest component, swap the other two and negate one.
    const float ax = std::fabs(f.F(0x30));
    f.F(0x10) = std::fabs(f.F(0x34));
    f.F(0xc) = std::fabs(f.F(0x38));
    uint32_t other1 = 1;
    uint32_t smallest = 0;
    uint32_t other2 = 2;
    float m = ax;
    if (f.F(0x10) < ax)
    {
        other1 = 0;
        m = f.F(0x10);
        smallest = 1;
    }
    if (f.F(0xc) < m)
    {
        other2 = smallest;
        smallest = 2;
    }
    Field<uint32_t>(out, 0x40 + smallest * 4) = 0;
    const float negated = -f.F(0x30 + other1 * 4);
    Field<uint32_t>(out, 0x40 + other1 * 4) = f.L(0x30 + other2 * 4);
    Field<float>(out, 0x40 + other2 * 4) = negated;
    const float p0 = Field<float>(out, 0x40);
    const float p1 = Field<float>(out, 0x44);
    const float p2 = Field<float>(out, 0x48);
    const float c1 = f.F(0x38) * p0 - f.F(0x30) * p2;
    const float c2 = f.F(0x30) * p1 - f.F(0x34) * p0;
    const float c0 = f.F(0x34) * p2 - f.F(0x38) * p1;
    Field<uint32_t>(out, 0x5c) = 0;
    Field<float>(out, 0x50) = c0;
    Field<float>(out, 0x54) = c1;
    Field<float>(out, 0x58) = c2;
    return other2 * 4;
}

static uint32_t fn_00459810_Abi(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t esi, uint32_t a0)
{
    return fn_00459810(reinterpret_cast<const float*>(eax), B(ecx), B(edx), B(esi), reinterpret_cast<const float*>(a0));
}
ABI_ENTRY_00459810(fn_00459810_Entry, fn_00459810_Abi)
HOOK(0x00459810, "fn_00459810", fn_00459810_Entry, nullptr);
