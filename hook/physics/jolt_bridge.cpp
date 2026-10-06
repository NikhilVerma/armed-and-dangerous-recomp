// Jolt Physics behind the plain C interface of jolt_bridge.h. Compiled with SSE2 into the static library and_physics
// (hook/CMakeLists.txt); the only file in the project that includes Jolt.
#include "jolt_bridge.h"

#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/OffsetCenterOfMassShape.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

using namespace JPH;

namespace
{
// Two object layers, as in Jolt's samples: static geometry never collides with itself.
namespace Layers
{
constexpr ObjectLayer kNonMoving = 0;
constexpr ObjectLayer kMoving = 1;
constexpr ObjectLayer kCount = 2;
}  // namespace Layers

namespace BroadPhaseLayers
{
constexpr BroadPhaseLayer kNonMoving(0);
constexpr BroadPhaseLayer kMoving(1);
constexpr uint kCount = 2;
}  // namespace BroadPhaseLayers

class BroadPhaseLayerMap final : public BroadPhaseLayerInterface
{
public:
    uint GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::kCount; }
    BroadPhaseLayer GetBroadPhaseLayer(ObjectLayer layer) const override
    {
        return layer == Layers::kNonMoving ? BroadPhaseLayers::kNonMoving : BroadPhaseLayers::kMoving;
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(BroadPhaseLayer layer) const override
    {
        return layer == BroadPhaseLayers::kNonMoving ? "non-moving" : "moving";
    }
#endif
};

class ObjectVsBroadPhase final : public ObjectVsBroadPhaseLayerFilter
{
public:
    bool ShouldCollide(ObjectLayer layer, BroadPhaseLayer bp) const override
    {
        return layer == Layers::kMoving || bp == BroadPhaseLayers::kMoving;
    }
};

class ObjectPairs final : public ObjectLayerPairFilter
{
public:
    bool ShouldCollide(ObjectLayer a, ObjectLayer b) const override
    {
        return a == Layers::kMoving || b == Layers::kMoving;
    }
};

class PairFilterListener final : public ContactListener
{
public:
    JB_PairFilter filter = nullptr;
    ValidateResult OnContactValidate(const Body& a, const Body& b, RVec3Arg, const CollideShapeResult&) override
    {
        if (filter && !filter(a.GetUserData(), b.GetUserData()))
            return ValidateResult::RejectAllContactsForThisBodyPair;
        return ValidateResult::AcceptAllContactsForThisBodyPair;
    }
};

struct World
{
    BroadPhaseLayerMap broadPhaseMap;
    ObjectVsBroadPhase objectVsBroadPhase;
    ObjectPairs objectPairs;
    PairFilterListener contacts;
    std::unique_ptr<TempAllocatorImpl> temp;
    std::unique_ptr<JobSystemSingleThreaded> jobs;  // deterministic: no worker threads
    std::unique_ptr<PhysicsSystem> system;
    std::vector<RefConst<Shape>> shapes;  // handle = index + 1
    std::vector<uint32_t> freeShapes;
};
World* g_world;

Vec3 V(const float v[3])
{
    return Vec3(v[0], v[1], v[2]);
}
Quat Q(const float q[4])
{
    return Quat(q[0], q[1], q[2], q[3]).Normalized();
}
void Out(Vec3 v, float out[3])
{
    out[0] = v.GetX(), out[1] = v.GetY(), out[2] = v.GetZ();
}
void Out(Quat q, float out[4])
{
    out[0] = q.GetX(), out[1] = q.GetY(), out[2] = q.GetZ(), out[3] = q.GetW();
}

uint32_t AddShape(const ShapeSettings::ShapeResult& r)
{
    if (!g_world || r.HasError())
        return 0;
    uint32_t index;
    if (!g_world->freeShapes.empty())
    {
        index = g_world->freeShapes.back();
        g_world->freeShapes.pop_back();
        g_world->shapes[index] = r.Get();
    }
    else
    {
        index = static_cast<uint32_t>(g_world->shapes.size());
        g_world->shapes.push_back(r.Get());
    }
    return index + 1;
}

const Shape* GetShape(uint32_t handle)
{
    if (!g_world || handle == 0 || handle > g_world->shapes.size())
        return nullptr;
    return g_world->shapes[handle - 1].GetPtr();
}

BodyID BodyOf(uint32_t handle)
{
    return handle ? BodyID(handle - 1) : BodyID();
}

EMotionType Motion(int motion)
{
    return motion == JB_Dynamic ? EMotionType::Dynamic
                                : motion == JB_Kinematic ? EMotionType::Kinematic : EMotionType::Static;
}
}  // namespace

extern "C" {

int JB_Init(const float gravity[3], uint32_t maxBodies)
{
    if (g_world)
        return 1;
    RegisterDefaultAllocator();
    Factory::sInstance = new Factory();
    RegisterTypes();
    g_world = new World();
    g_world->temp = std::make_unique<TempAllocatorImpl>(16 * 1024 * 1024);
    g_world->jobs = std::make_unique<JobSystemSingleThreaded>(cMaxPhysicsJobs);
    g_world->system = std::make_unique<PhysicsSystem>();
    g_world->system->Init(maxBodies, 0, maxBodies * 4, maxBodies * 4, g_world->broadPhaseMap,
                          g_world->objectVsBroadPhase, g_world->objectPairs);
    g_world->system->SetGravity(V(gravity));
    g_world->system->SetContactListener(&g_world->contacts);
    return 1;
}

void JB_Shutdown()
{
    if (!g_world)
        return;
    delete g_world;
    g_world = nullptr;
    UnregisterTypes();
    delete Factory::sInstance;
    Factory::sInstance = nullptr;
}

int JB_IsReady()
{
    return g_world != nullptr;
}

void JB_Step(float dt, int collisionSteps)
{
    if (!g_world || dt <= 0.0f)
        return;
    g_world->system->Update(dt, std::max(collisionSteps, 1), g_world->temp.get(), g_world->jobs.get());
}

void JB_SetPairFilter(JB_PairFilter filter)
{
    if (g_world)
        g_world->contacts.filter = filter;
}

void JB_SetGravity(const float gravity[3])
{
    if (g_world)
        g_world->system->SetGravity(V(gravity));
}

uint32_t JB_ShapeSphere(float radius)
{
    return AddShape(SphereShapeSettings(radius).Create());
}

uint32_t JB_ShapeBox(const float halfExtents[3], float convexRadius)
{
    const float smallest = std::min({halfExtents[0], halfExtents[1], halfExtents[2]});
    return AddShape(BoxShapeSettings(V(halfExtents), std::min(convexRadius, smallest)).Create());
}

uint32_t JB_ShapeConvexHull(const float* points, uint32_t count, float convexRadius)
{
    Array<Vec3> pts;
    pts.reserve(count);
    for (uint32_t i = 0; i < count; ++i)
        pts.push_back(V(points + i * 3));
    return AddShape(ConvexHullShapeSettings(pts, convexRadius).Create());
}

uint32_t JB_ShapeMesh(const float* vertices, uint32_t vertexCount, const uint32_t* triangles, uint32_t triangleCount)
{
    VertexList verts;
    verts.reserve(vertexCount);
    for (uint32_t i = 0; i < vertexCount; ++i)
        verts.push_back(Float3(vertices[i * 3], vertices[i * 3 + 1], vertices[i * 3 + 2]));
    IndexedTriangleList tris;
    tris.reserve(triangleCount);
    for (uint32_t i = 0; i < triangleCount; ++i)
        tris.push_back(IndexedTriangle(triangles[i * 3], triangles[i * 3 + 1], triangles[i * 3 + 2], 0));
    return AddShape(MeshShapeSettings(std::move(verts), std::move(tris)).Create());  // by value: no extra copy
}

uint32_t JB_ShapeTransformed(uint32_t child, const float position[3], const float rotation[4])
{
    const Shape* c = GetShape(child);
    if (!c)
        return 0;
    return AddShape(RotatedTranslatedShapeSettings(V(position), Q(rotation), c).Create());
}

uint32_t JB_ShapeCompound(const uint32_t* children, const float* positions, const float* rotations, uint32_t count)
{
    StaticCompoundShapeSettings settings;
    for (uint32_t i = 0; i < count; ++i)
    {
        const Shape* c = GetShape(children[i]);
        if (!c)
            return 0;
        settings.AddShape(V(positions + i * 3), Q(rotations + i * 4), c);
    }
    return AddShape(settings.Create());
}

uint32_t JB_ShapeWithCenterOfMass(uint32_t shape, const float centerOfMass[3])
{
    const Shape* s = GetShape(shape);
    if (!s)
        return 0;
    return AddShape(OffsetCenterOfMassShapeSettings(V(centerOfMass) - s->GetCenterOfMass(), s).Create());
}

void JB_ShapeRelease(uint32_t shape)
{
    if (!GetShape(shape))
        return;
    g_world->shapes[shape - 1] = nullptr;
    g_world->freeShapes.push_back(shape - 1);
}

uint32_t JB_BodyCreate(uint32_t shape, const float position[3], const float rotation[4], int motion, float mass,
                       float friction, float restitution, uint64_t userData)
{
    const Shape* s = GetShape(shape);
    if (!s)
        return 0;
    const EMotionType type = Motion(motion);
    BodyCreationSettings settings(s, RVec3(V(position)), Q(rotation), type,
                                  type == EMotionType::Static ? Layers::kNonMoving : Layers::kMoving);
    settings.mAllowDynamicOrKinematic = true;  // Havok bodies change motion type (keyframed <-> dynamic, ragdolls)
    settings.mFriction = friction;
    settings.mRestitution = restitution;
    settings.mUserData = userData;
    settings.mMaxLinearVelocity = 10000.0f;  // game units are feet; Jolt's default (500) is for metres
    settings.mMaxAngularVelocity = 1000.0f;
    if (type == EMotionType::Dynamic && mass > 0.0f)
    {
        // Shapes without volume (meshes) have no mass properties of their own: provide a unit inertia, which
        // JB_BodySetMassProperties replaces.
        if (s->GetMassProperties().mMass > 0.0f)
            settings.mOverrideMassProperties = EOverrideMassProperties::CalculateInertia;
        else
        {
            settings.mOverrideMassProperties = EOverrideMassProperties::MassAndInertiaProvided;
            settings.mMassPropertiesOverride.mInertia = Mat44::sScale(mass);
        }
        settings.mMassPropertiesOverride.mMass = mass;
    }
    BodyInterface& bi = g_world->system->GetBodyInterface();
    const BodyID id = bi.CreateAndAddBody(settings, type == EMotionType::Static ? EActivation::DontActivate
                                                                                : EActivation::Activate);
    return id.IsInvalid() ? 0 : id.GetIndexAndSequenceNumber() + 1;
}

void JB_BodyDestroy(uint32_t body)
{
    if (!g_world || !body)
        return;
    BodyInterface& bi = g_world->system->GetBodyInterface();
    bi.RemoveBody(BodyOf(body));
    bi.DestroyBody(BodyOf(body));
}

void JB_BodySetState(uint32_t body, const float position[3], const float rotation[4], const float linearVelocity[3],
                     const float angularVelocity[3])
{
    if (!g_world || !body)
        return;
    BodyInterface& bi = g_world->system->GetBodyInterface();
    bi.SetPositionAndRotation(BodyOf(body), RVec3(V(position)), Q(rotation), EActivation::DontActivate);
    if (bi.GetMotionType(BodyOf(body)) != EMotionType::Static)
        bi.SetLinearAndAngularVelocity(BodyOf(body), V(linearVelocity), V(angularVelocity));
}

void JB_BodyGetState(uint32_t body, float position[3], float rotation[4], float linearVelocity[3],
                     float angularVelocity[3])
{
    if (!g_world || !body)
        return;
    BodyInterface& bi = g_world->system->GetBodyInterface();
    RVec3 p;
    Quat q;
    bi.GetPositionAndRotation(BodyOf(body), p, q);
    Out(Vec3(p), position);
    Out(q, rotation);
    Out(bi.GetLinearVelocity(BodyOf(body)), linearVelocity);
    Out(bi.GetAngularVelocity(BodyOf(body)), angularVelocity);
}

void JB_BodyAddImpulse(uint32_t body, const float impulse[3])
{
    if (g_world && body)
        g_world->system->GetBodyInterface().AddImpulse(BodyOf(body), V(impulse));
}

void JB_BodySetMotion(uint32_t body, int motion)
{
    if (g_world && body)
        g_world->system->GetBodyInterface().SetMotionType(BodyOf(body), Motion(motion), EActivation::Activate);
}

void JB_BodySetMassProperties(uint32_t body, float invMass, const float invInertia[3], float linearDamping,
                              float angularDamping)
{
    if (!g_world || !body)
        return;
    BodyLockWrite lock(g_world->system->GetBodyLockInterface(), BodyOf(body));
    if (!lock.Succeeded() || !lock.GetBody().IsDynamic())
        return;
    MotionProperties* mp = lock.GetBody().GetMotionProperties();
    mp->SetInverseMass(invMass);
    mp->SetInverseInertia(V(invInertia), Quat::sIdentity());
    mp->SetLinearDamping(linearDamping);
    mp->SetAngularDamping(angularDamping);
}

void JB_BodyGetCenterOfMass(uint32_t body, float centerOfMass[3])
{
    if (g_world && body)
        Out(Vec3(g_world->system->GetBodyInterface().GetCenterOfMassPosition(BodyOf(body))), centerOfMass);
}

uint32_t JB_BodyCount()
{
    return g_world ? g_world->system->GetNumBodies() : 0;
}

}  // extern "C"
