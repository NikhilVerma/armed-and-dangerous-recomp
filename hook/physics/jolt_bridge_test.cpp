// Headless test of the Jolt bridge (hook/physics/jolt_bridge.h): no game. A 2 x 2 x 2 box falls from z = 10 under the
// game's gravity (0, 0, -64.348) onto a triangle-mesh floor at z = 0 (the shape type level collision will use), with
// after 3 s it must rest on the floor (centre z ~ 1) and be nearly still. Also checks that a compound of offset
// spheres and a convex hull can be created. Prints PASS / FAIL.
#include <cmath>
#include <cstdio>

#include "jolt_bridge.h"

int main()
{
    const float gravity[3] = {0.0f, 0.0f, -64.348f};
    if (!JB_Init(gravity, 1024))
        return std::printf("FAIL: init\n"), 1;
    // Floor: a 200 x 200 quad as a mesh (two triangles).
    const float verts[] = {-100, -100, 0, 100, -100, 0, 100, 100, 0, -100, 100, 0};
    const uint32_t tris[] = {0, 1, 2, 0, 2, 3};
    const uint32_t floorShape = JB_ShapeMesh(verts, 4, tris, 2);
    const float origin[3] = {0, 0, 0}, identity[4] = {0, 0, 0, 1};
    const uint32_t floorBody = JB_BodyCreate(floorShape, origin, identity, JB_Static, 0, 0.6f, 0.0f, 1);
    // Falling box.
    const float half[3] = {1, 1, 1};
    const uint32_t boxShape = JB_ShapeBox(half, 0.05f);
    const float start[3] = {0, 0, 10};
    const uint32_t box = JB_BodyCreate(boxShape, start, identity, JB_Dynamic, 10.0f, 0.6f, 0.0f, 2);
    // Other shape kinds used by the game.
    const uint32_t sphere = JB_ShapeSphere(0.5f);
    const uint32_t children[2] = {sphere, sphere};
    const float offsets[6] = {0, 0, 0.5f, 0, 0, -0.5f}, rots[8] = {0, 0, 0, 1, 0, 0, 0, 1};
    const uint32_t compound = JB_ShapeCompound(children, offsets, rots, 2);
    const float hull[] = {0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 1};
    const uint32_t convex = JB_ShapeConvexHull(hull, 5, 0.05f);
    if (!floorShape || !floorBody || !boxShape || !box || !compound || !convex)
        return std::printf("FAIL: creation (floor %u/%u box %u/%u compound %u hull %u)\n", floorShape, floorBody,
                           boxShape, box, compound, convex),
               1;
    for (int i = 0; i < 180; ++i)  // 3 s at 60 Hz, as PhysicsManager_Step (dt >= 1/60)
        JB_Step(1.0f / 60.0f, 1);
    float p[3], r[4], lin[3], ang[3];
    JB_BodyGetState(box, p, r, lin, ang);
    const float speed = std::sqrt(lin[0] * lin[0] + lin[1] * lin[1] + lin[2] * lin[2]);
    const bool ok = std::fabs(p[2] - 1.0f) < 0.1f && speed < 0.1f && JB_BodyCount() == 2;
    std::printf("%s: box at (%.3f, %.3f, %.3f) speed %.4f, bodies %u\n", ok ? "PASS" : "FAIL", p[0], p[1], p[2], speed,
                JB_BodyCount());
    JB_Shutdown();
    return ok ? 0 : 1;
}
