/*
 * vjolt.cpp — Jolt Physics wrapper for vlcrlib
 * C API in vjolt.h; requires Jolt linked (vcpkg: joltphysics).
 */

#include "vjolt.h"

#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <thread>

#if VLCRLIB_HAS_JOLT

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Physics/Body/BodyInterface.h>

using namespace JPH;
using namespace JPH::literals;

/* ---- Layer setup (simple: all objects collide) ---- */
namespace Layers {
    static constexpr ObjectLayer NON_MOVING = 0;
    static constexpr ObjectLayer MOVING     = 1;
    static constexpr ObjectLayer NUM_LAYERS = 2;
}

namespace BroadPhaseLayers {
    static constexpr BroadPhaseLayer NON_MOVING(0);
    static constexpr BroadPhaseLayer MOVING(1);
    static constexpr uint NUM_LAYERS = 2;
}

class BPLayerInterfaceImpl final : public BroadPhaseLayerInterface {
public:
    BPLayerInterfaceImpl() {
        mMap[Layers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
        mMap[Layers::MOVING]     = BroadPhaseLayers::MOVING;
    }
    uint GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::NUM_LAYERS; }
    BroadPhaseLayer GetBroadPhaseLayer(ObjectLayer layer) const override {
        return mMap[layer];
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char *GetBroadPhaseLayerName(BroadPhaseLayer layer) const override {
        switch ((BroadPhaseLayer::Type)layer) {
        case (BroadPhaseLayer::Type)BroadPhaseLayers::NON_MOVING: return "NON_MOVING";
        case (BroadPhaseLayer::Type)BroadPhaseLayers::MOVING:     return "MOVING";
        default: return "INVALID";
        }
    }
#endif
private:
    BroadPhaseLayer mMap[Layers::NUM_LAYERS];
};

class ObjectVsBroadPhaseLayerFilterImpl final : public ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(ObjectLayer layer, BroadPhaseLayer bp) const override {
        switch (layer) {
        case Layers::NON_MOVING: return bp == BroadPhaseLayers::MOVING;
        case Layers::MOVING:     return true;
        default: return false;
        }
    }
};

class ObjectLayerPairFilterImpl final : public ObjectLayerPairFilter {
public:
    bool ShouldCollide(ObjectLayer a, ObjectLayer b) const override {
        switch (a) {
        case Layers::NON_MOVING: return b == Layers::MOVING;
        case Layers::MOVING:     return true;
        default: return false;
        }
    }
};

/* ---- Global Jolt state ---- */
static bool g_joltInit = false;
static TempAllocatorImpl *g_temp = nullptr;
static JobSystemThreadPool *g_jobs = nullptr;

static void JoltTrace(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    fprintf(stderr, "[Jolt] %s\n", buf);
}

#ifdef JPH_ENABLE_ASSERTS
static bool JoltAssert(const char *expr, const char *msg, const char *file, uint line) {
    fprintf(stderr, "[Jolt Assert] %s:%u: (%s) %s\n", file, line, expr, msg ? msg : "");
    return true;
}
#endif

struct JoltWorld {
    PhysicsSystem *system;
    BPLayerInterfaceImpl *bpInterface;
    ObjectVsBroadPhaseLayerFilterImpl *objVsBp;
    ObjectLayerPairFilterImpl *objPair;
    float accumulator;
};

struct JoltBody {
    JoltWorld *world;
    BodyID     id;
};

static inline RVec3 toJ(Vector3 v) { return RVec3(v.x, v.y, v.z); }
static inline Vector3 fromJ(RVec3 v) {
    return Vector3{ (float)v.GetX(), (float)v.GetY(), (float)v.GetZ() };
}
static inline Quat toJQ(Quaternion q) { return Quat(q.x, q.y, q.z, q.w); }
static inline Quaternion fromJQ(Quat q) {
    return Quaternion{ q.GetX(), q.GetY(), q.GetZ(), q.GetW() };
}

extern "C" bool Jolt_Init(void) {
    if (g_joltInit) return true;

    RegisterDefaultAllocator();
    Trace = JoltTrace;
#ifdef JPH_ENABLE_ASSERTS
    AssertFailed = JoltAssert;
#endif
    Factory::sInstance = new Factory();
    RegisterTypes();

    g_temp = new TempAllocatorImpl(32 * 1024 * 1024);
    unsigned threads = std::thread::hardware_concurrency();
    if (threads < 1) threads = 1;
    g_jobs = new JobSystemThreadPool(cMaxPhysicsJobs, cMaxPhysicsBarriers, (int)threads - 1);

    g_joltInit = true;
    return true;
}

extern "C" void Jolt_Shutdown(void) {
    if (!g_joltInit) return;
    delete g_jobs; g_jobs = nullptr;
    delete g_temp; g_temp = nullptr;
    UnregisterTypes();
    delete Factory::sInstance;
    Factory::sInstance = nullptr;
    g_joltInit = false;
}

extern "C" bool Jolt_IsInitialized(void) {
    return g_joltInit;
}

extern "C" JoltWorld *Jolt_CreateWorld(Vector3 gravity) {
    if (!g_joltInit && !Jolt_Init()) return nullptr;

    JoltWorld *w = new JoltWorld();
    w->bpInterface = new BPLayerInterfaceImpl();
    w->objVsBp     = new ObjectVsBroadPhaseLayerFilterImpl();
    w->objPair     = new ObjectLayerPairFilterImpl();
    w->system      = new PhysicsSystem();
    w->accumulator = 0.f;

    const uint cMaxBodies = 10240;
    const uint cNumBodyMutexes = 0;
    const uint cMaxBodyPairs = 65536;
    const uint cMaxContactConstraints = 10240;

    w->system->Init(cMaxBodies, cNumBodyMutexes, cMaxBodyPairs, cMaxContactConstraints,
                    *w->bpInterface, *w->objVsBp, *w->objPair);
    w->system->SetGravity(Vec3(gravity.x, gravity.y, gravity.z));
    return w;
}

extern "C" void Jolt_DestroyWorld(JoltWorld *world) {
    if (!world) return;
    delete world->system;
    delete world->objPair;
    delete world->objVsBp;
    delete world->bpInterface;
    delete world;
}

extern "C" void Jolt_Step(JoltWorld *world, float dt) {
    if (!world || !g_temp || !g_jobs) return;
    /* Fixed 60 Hz step with accumulator */
    const float fixed = 1.0f / 60.0f;
    world->accumulator += dt;
    int steps = 0;
    while (world->accumulator >= fixed && steps < 5) {
        world->system->Update(fixed, 1, g_temp, g_jobs);
        world->accumulator -= fixed;
        ++steps;
    }
}

extern "C" void Jolt_SetGravity(JoltWorld *world, Vector3 gravity) {
    if (!world) return;
    world->system->SetGravity(Vec3(gravity.x, gravity.y, gravity.z));
}

extern "C" Vector3 Jolt_GetGravity(const JoltWorld *world) {
    if (!world) return Vector3{ 0, 0, 0 };
    Vec3 g = world->system->GetGravity();
    return Vector3{ g.GetX(), g.GetY(), g.GetZ() };
}

extern "C" JoltBody *Jolt_CreateBody(JoltWorld *world,
                                     JoltShapeType shape,
                                     Vector3 position,
                                     Vector3 shapeParam,
                                     JoltBodyType type,
                                     float mass,
                                     float friction,
                                     float restitution) {
    if (!world) return nullptr;

    ShapeSettings::ShapeResult shapeResult;
    switch (shape) {
    case JOLT_SHAPE_BOX: {
        BoxShapeSettings s(Vec3(shapeParam.x, shapeParam.y, shapeParam.z));
        s.SetEmbedded();
        shapeResult = s.Create();
        break;
    }
    case JOLT_SHAPE_SPHERE: {
        SphereShapeSettings s(shapeParam.x > 0.f ? shapeParam.x : 0.5f);
        s.SetEmbedded();
        shapeResult = s.Create();
        break;
    }
    case JOLT_SHAPE_CAPSULE: {
        float r = shapeParam.x > 0.f ? shapeParam.x : 0.3f;
        float h = shapeParam.y > 0.f ? shapeParam.y : 0.5f;
        CapsuleShapeSettings s(h, r);
        s.SetEmbedded();
        shapeResult = s.Create();
        break;
    }
    case JOLT_SHAPE_CYLINDER: {
        float r = shapeParam.x > 0.f ? shapeParam.x : 0.5f;
        float h = shapeParam.y > 0.f ? shapeParam.y : 0.5f;
        CylinderShapeSettings s(h, r);
        s.SetEmbedded();
        shapeResult = s.Create();
        break;
    }
    default:
        return nullptr;
    }
    if (shapeResult.HasError()) {
        fprintf(stderr, "[vjolt] shape error: %s\n", shapeResult.GetError().c_str());
        return nullptr;
    }
    ShapeRefC shapeRef = shapeResult.Get();

    EMotionType motion = EMotionType::Static;
    ObjectLayer layer = Layers::NON_MOVING;
    if (type == JOLT_BODY_DYNAMIC) {
        motion = EMotionType::Dynamic;
        layer = Layers::MOVING;
    } else if (type == JOLT_BODY_KINEMATIC) {
        motion = EMotionType::Kinematic;
        layer = Layers::MOVING;
    }

    BodyCreationSettings settings(shapeRef, toJ(position), Quat::sIdentity(), motion, layer);
    if (type == JOLT_BODY_DYNAMIC && mass > 0.f) {
        settings.mOverrideMassProperties = EOverrideMassProperties::CalculateInertia;
        settings.mMassPropertiesOverride.mMass = mass;
    }
    settings.mFriction = friction;
    settings.mRestitution = restitution;

    BodyInterface &bi = world->system->GetBodyInterface();
    Body *body = bi.CreateBody(settings);
    if (!body) return nullptr;
    bi.AddBody(body->GetID(), EActivation::Activate);

    JoltBody *out = new JoltBody();
    out->world = world;
    out->id = body->GetID();
    bi.SetUserData(out->id, (uint64)(uintptr_t)out);
    return out;
}

extern "C" void Jolt_DestroyBody(JoltBody *body) {
    if (!body || !body->world) return;
    BodyInterface &bi = body->world->system->GetBodyInterface();
    bi.RemoveBody(body->id);
    bi.DestroyBody(body->id);
    delete body;
}

extern "C" void Jolt_SetPosition(JoltBody *body, Vector3 position) {
    if (!body) return;
    BodyInterface &bi = body->world->system->GetBodyInterface();
    bi.SetPosition(body->id, toJ(position), EActivation::Activate);
}

extern "C" Vector3 Jolt_GetPosition(const JoltBody *body) {
    if (!body) return Vector3{ 0, 0, 0 };
    BodyInterface &bi = body->world->system->GetBodyInterface();
    return fromJ(bi.GetPosition(body->id));
}

extern "C" void Jolt_SetRotation(JoltBody *body, Quaternion rotation) {
    if (!body) return;
    BodyInterface &bi = body->world->system->GetBodyInterface();
    bi.SetRotation(body->id, toJQ(rotation), EActivation::Activate);
}

extern "C" Quaternion Jolt_GetRotation(const JoltBody *body) {
    if (!body) return Quaternion{ 0, 0, 0, 1 };
    BodyInterface &bi = body->world->system->GetBodyInterface();
    return fromJQ(bi.GetRotation(body->id));
}

extern "C" void Jolt_SetLinearVelocity(JoltBody *body, Vector3 v) {
    if (!body) return;
    body->world->system->GetBodyInterface().SetLinearVelocity(body->id, Vec3(v.x, v.y, v.z));
}

extern "C" Vector3 Jolt_GetLinearVelocity(const JoltBody *body) {
    if (!body) return Vector3{ 0, 0, 0 };
    Vec3 v = body->world->system->GetBodyInterface().GetLinearVelocity(body->id);
    return Vector3{ v.GetX(), v.GetY(), v.GetZ() };
}

extern "C" void Jolt_SetAngularVelocity(JoltBody *body, Vector3 w) {
    if (!body) return;
    body->world->system->GetBodyInterface().SetAngularVelocity(body->id, Vec3(w.x, w.y, w.z));
}

extern "C" Vector3 Jolt_GetAngularVelocity(const JoltBody *body) {
    if (!body) return Vector3{ 0, 0, 0 };
    Vec3 v = body->world->system->GetBodyInterface().GetAngularVelocity(body->id);
    return Vector3{ v.GetX(), v.GetY(), v.GetZ() };
}

extern "C" void Jolt_AddForce(JoltBody *body, Vector3 force) {
    if (!body) return;
    body->world->system->GetBodyInterface().AddForce(body->id, Vec3(force.x, force.y, force.z));
}

extern "C" void Jolt_AddImpulse(JoltBody *body, Vector3 impulse) {
    if (!body) return;
    body->world->system->GetBodyInterface().AddImpulse(body->id, Vec3(impulse.x, impulse.y, impulse.z));
}

extern "C" void Jolt_AddTorque(JoltBody *body, Vector3 torque) {
    if (!body) return;
    body->world->system->GetBodyInterface().AddTorque(body->id, Vec3(torque.x, torque.y, torque.z));
}

extern "C" void Jolt_ActivateBody(JoltBody *body) {
    if (!body) return;
    body->world->system->GetBodyInterface().ActivateBody(body->id);
}

extern "C" JoltRayHit Jolt_Raycast(JoltWorld *world, Vector3 origin, Vector3 direction, float maxDist) {
    JoltRayHit hit = {};
    if (!world) return hit;

    Vec3 dir = Vec3(direction.x, direction.y, direction.z);
    float len = dir.Length();
    if (len < 1e-6f) return hit;
    dir = dir * (1.0f / len);

    RRayCast ray{ toJ(origin), dir * maxDist };
    RayCastResult result;
    if (!world->system->GetNarrowPhaseQuery().CastRay(ray, result))
        return hit;

    hit.hit = 1;
    hit.distance = result.mFraction * maxDist;
    RVec3 pt = ray.GetPointOnRay(result.mFraction);
    hit.point = fromJ(pt);
    /* Normal approximate from body surface — use default up if unavailable */
    hit.normal = Vector3{ 0, 1, 0 };
    BodyLockRead lock(world->system->GetBodyLockInterface(), result.mBodyID);
    if (lock.Succeeded()) {
        const Body &b = lock.GetBody();
        hit.body = (JoltBody *)(uintptr_t)b.GetUserData();
        Vec3 n = b.GetWorldSpaceSurfaceNormal(result.mSubShapeID2, pt);
        hit.normal = Vector3{ n.GetX(), n.GetY(), n.GetZ() };
    }
    return hit;
}

#else /* !VLCRLIB_HAS_JOLT */

/* Stubs when Jolt is not linked */
extern "C" bool Jolt_Init(void) { fprintf(stderr, "[vjolt] Jolt not built (VLCRLIB_HAS_JOLT off)\n"); return false; }
extern "C" void Jolt_Shutdown(void) {}
extern "C" bool Jolt_IsInitialized(void) { return false; }
extern "C" JoltWorld *Jolt_CreateWorld(Vector3) { return nullptr; }
extern "C" void Jolt_DestroyWorld(JoltWorld *) {}
extern "C" void Jolt_Step(JoltWorld *, float) {}
extern "C" void Jolt_SetGravity(JoltWorld *, Vector3) {}
extern "C" Vector3 Jolt_GetGravity(const JoltWorld *) { return Vector3{0,0,0}; }
extern "C" JoltBody *Jolt_CreateBody(JoltWorld *, JoltShapeType, Vector3, Vector3, JoltBodyType, float, float, float) { return nullptr; }
extern "C" void Jolt_DestroyBody(JoltBody *) {}
extern "C" void Jolt_SetPosition(JoltBody *, Vector3) {}
extern "C" Vector3 Jolt_GetPosition(const JoltBody *) { return Vector3{0,0,0}; }
extern "C" void Jolt_SetRotation(JoltBody *, Quaternion) {}
extern "C" Quaternion Jolt_GetRotation(const JoltBody *) { return Quaternion{0,0,0,1}; }
extern "C" void Jolt_SetLinearVelocity(JoltBody *, Vector3) {}
extern "C" Vector3 Jolt_GetLinearVelocity(const JoltBody *) { return Vector3{0,0,0}; }
extern "C" void Jolt_SetAngularVelocity(JoltBody *, Vector3) {}
extern "C" Vector3 Jolt_GetAngularVelocity(const JoltBody *) { return Vector3{0,0,0}; }
extern "C" void Jolt_AddForce(JoltBody *, Vector3) {}
extern "C" void Jolt_AddImpulse(JoltBody *, Vector3) {}
extern "C" void Jolt_AddTorque(JoltBody *, Vector3) {}
extern "C" void Jolt_ActivateBody(JoltBody *) {}
extern "C" JoltRayHit Jolt_Raycast(JoltWorld *, Vector3, Vector3, float) { return JoltRayHit{}; }

#endif /* VLCRLIB_HAS_JOLT */
