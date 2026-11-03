#include "vphysics2d.h"
#include "vshapes.h"
#include <box2d/box2d.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <cmath>

/* ================================================================== */
/*  Internal structs                                                  */
/* ================================================================== */

struct PhysicsWorld2D {
    b2World *world;
    Vector2  gravity;
    int      velocityIterations;
    int      positionIterations;
    float    timeAccumulator;
};

struct PhysicsBody2D {
    PhysicsWorld2D *world;
    b2Body         *body;
    void           *userData;
    PhysicsBodyType2D type;
};

struct PhysicsJoint2D {
    PhysicsWorld2D *world;
    b2Joint        *joint;
    JointType2D     type;
};

struct Character2D {
    PhysicsWorld2D *world;
    PhysicsBody2D  *body;   /* dynamic capsule-like (box) */
    float           radius;
    float           stepHeight;
    float           slopeMax;
    bool            grounded;
};

/* ================================================================== */
/*  Helpers                                                           */
/* ================================================================== */

static inline b2Vec2  toB2(Vector2 v)        { return b2Vec2(v.x, v.y); }
static inline Vector2 fromB2(b2Vec2 v)       { return Vector2{ v.x, v.y }; }

static inline b2BodyType toB2Type(PhysicsBodyType2D t) {
    switch (t) {
        case PHYS_BODY_STATIC2D:    return b2_staticBody;
        case PHYS_BODY_DYNAMIC2D:   return b2_dynamicBody;
        case PHYS_BODY_KINEMATIC2D: return b2_kinematicBody;
    }
    return b2_staticBody;
}

static PhysicsBody2D *wrapBody(PhysicsWorld2D *world, b2Body *body,
                               PhysicsBodyType2D type)
{
    PhysicsBody2D *out = new PhysicsBody2D();
    out->world    = world;
    out->body     = body;
    out->userData = NULL;
    out->type     = type;
    body->GetUserData().pointer = (uintptr_t)out;
    return out;
}

static PhysicsBody2D *findBodyWrap(b2Body *b) {
    if (!b) return NULL;
    return (PhysicsBody2D *)b->GetUserData().pointer;
}

/* ================================================================== */
/*  World                                                             */
/* ================================================================== */

extern "C" PhysicsWorld2D *CreatePhysicsWorld2D(Vector2 gravity) {
    PhysicsWorld2D *w = new PhysicsWorld2D();
    w->world = new b2World(toB2(gravity));
    w->gravity = gravity;
    w->velocityIterations = 8;
    w->positionIterations = 3;
    return w;
}

extern "C" void DestroyPhysicsWorld2D(PhysicsWorld2D *world) {
    if (!world) return;
    delete world->world;
    delete world;
}

extern "C" void StepPhysicsWorld2D(PhysicsWorld2D *world, float dt) {
    if (!world || !world->world || !std::isfinite(dt) || dt <= 0.0f) return;

    /* Box2D expects a stable step size. Accept render-frame delta from the
     * public API, then advance the simulation in fixed 60 Hz steps. Bound
     * catch-up work after a stall to avoid a spiral of death. */
    constexpr float fixedStep = 1.0f / 60.0f;
    constexpr int maxSubSteps = 8;
    constexpr float maxFrameDelta = fixedStep * maxSubSteps;
    world->timeAccumulator += dt < maxFrameDelta ? dt : maxFrameDelta;

    int steps = 0;
    while (world->timeAccumulator >= fixedStep && steps < maxSubSteps) {
        world->world->Step(fixedStep, world->velocityIterations,
                           world->positionIterations);
        world->timeAccumulator -= fixedStep;
        ++steps;
    }
}

extern "C" void SetPhysicsWorld2DGravity(PhysicsWorld2D *w, Vector2 g) {
    if (!w) return;
    w->gravity = g;
    w->world->SetGravity(toB2(g));
}

extern "C" Vector2 GetPhysicsWorld2DGravity(const PhysicsWorld2D *w) {
    if (!w) return Vector2{0, 0};
    return w->gravity;
}

extern "C" void SetPhysicsWorld2DSubSteps(PhysicsWorld2D *w,
                                          int velocityIterations,
                                          int positionIterations)
{
    if (!w) return;
    if (velocityIterations > 0) w->velocityIterations = velocityIterations;
    if (positionIterations > 0) w->positionIterations = positionIterations;
}

extern "C" void SetPhysicsWorld2DAllowSleep(PhysicsWorld2D *w, bool allow) {
    if (!w) return;
    w->world->SetAllowSleeping(allow);
}

extern "C" bool GetPhysicsWorld2DAllowSleep(const PhysicsWorld2D *w) {
    if (!w) return true;
    return w->world->GetAllowSleeping();
}

/* ================================================================== */
/*  Body creation                                                     */
/* ================================================================== */

extern "C" PhysicsBody2D *CreatePhysicsBody2D(PhysicsWorld2D *world,
                                              PhysicsShapeType2D shape,
                                              Vector2 position,
                                              Vector2 sizeOrRadius,
                                              PhysicsBodyType2D type,
                                              float density,
                                              float friction,
                                              float restitution)
{
    switch (shape) {
        case PHYS_SHAPE_BOX2D:
            return CreatePhysicsBody2DBox(world, position, sizeOrRadius,
                                          type, density, friction, restitution);
        case PHYS_SHAPE_CIRCLE2D:
            return CreatePhysicsBody2DCircle(world, position, sizeOrRadius.x,
                                             type, density, friction, restitution);
        default:
            return NULL;
    }
}

extern "C" PhysicsBody2D *CreatePhysicsBody2DBox(PhysicsWorld2D *world,
                                                 Vector2 position,
                                                 Vector2 halfSize,
                                                 PhysicsBodyType2D type,
                                                 float density,
                                                 float friction,
                                                 float restitution)
{
    if (!world) return NULL;

    b2BodyDef def;
    def.position = toB2(position);
    def.type = toB2Type(type);
    b2Body *body = world->world->CreateBody(&def);

    b2PolygonShape shape;
    shape.SetAsBox(halfSize.x, halfSize.y);

    b2FixtureDef fd;
    fd.shape = &shape;
    fd.density = density;
    fd.friction = friction;
    fd.restitution = restitution;
    body->CreateFixture(&fd);

    return wrapBody(world, body, type);
}

extern "C" PhysicsBody2D *CreatePhysicsBody2DCircle(PhysicsWorld2D *world,
                                                    Vector2 position,
                                                    float radius,
                                                    PhysicsBodyType2D type,
                                                    float density,
                                                    float friction,
                                                    float restitution)
{
    if (!world) return NULL;

    b2BodyDef def;
    def.position = toB2(position);
    def.type = toB2Type(type);
    b2Body *body = world->world->CreateBody(&def);

    b2CircleShape shape;
    shape.m_radius = radius;

    b2FixtureDef fd;
    fd.shape = &shape;
    fd.density = density;
    fd.friction = friction;
    fd.restitution = restitution;
    body->CreateFixture(&fd);

    return wrapBody(world, body, type);
}

extern "C" PhysicsBody2D *CreatePhysicsBody2DPolygon(PhysicsWorld2D *world,
                                                     Vector2 position,
                                                     const Vector2 *points, int count,
                                                     PhysicsBodyType2D type,
                                                     float density,
                                                     float friction,
                                                     float restitution)
{
    if (!world || !points || count < 3 || count > b2_maxPolygonVertices) return NULL;

    b2BodyDef def;
    def.position = toB2(position);
    def.type = toB2Type(type);
    b2Body *body = world->world->CreateBody(&def);

    b2Vec2 verts[b2_maxPolygonVertices];
    for (int i = 0; i < count; ++i)
        verts[i] = toB2(points[i]);

    b2PolygonShape shape;
    shape.Set(verts, count);

    b2FixtureDef fd;
    fd.shape = &shape;
    fd.density = density;
    fd.friction = friction;
    fd.restitution = restitution;
    body->CreateFixture(&fd);

    return wrapBody(world, body, type);
}

extern "C" PhysicsBody2D *CreatePhysicsBody2DEdge(PhysicsWorld2D *world,
                                                  Vector2 a, Vector2 b,
                                                  float friction, float restitution)
{
    if (!world) return NULL;

    b2BodyDef def;
    def.position = b2Vec2(0, 0);
    def.type = b2_staticBody;
    b2Body *body = world->world->CreateBody(&def);

    b2EdgeShape shape;
    shape.SetTwoSided(toB2(a), toB2(b));

    b2FixtureDef fd;
    fd.shape = &shape;
    fd.friction = friction;
    fd.restitution = restitution;
    body->CreateFixture(&fd);

    return wrapBody(world, body, PHYS_BODY_STATIC2D);
}

extern "C" PhysicsBody2D *CreatePhysicsBody2DChain(PhysicsWorld2D *world,
                                                   const Vector2 *points, int count,
                                                   bool loop,
                                                   float friction, float restitution)
{
    if (!world || !points || count < 2) return NULL;

    b2BodyDef def;
    def.position = b2Vec2(0, 0);
    def.type = b2_staticBody;
    b2Body *body = world->world->CreateBody(&def);

    b2Vec2 *verts = (b2Vec2 *)malloc(sizeof(b2Vec2) * count);
    for (int i = 0; i < count; ++i) verts[i] = toB2(points[i]);

     b2ChainShape shape;
    if (loop) {
        shape.CreateLoop(verts, count);
    } else {
        shape.CreateChain(verts, count, verts[0], verts[count - 1]);
    }

    b2FixtureDef fd;
    fd.shape = &shape;
    fd.friction = friction;
    fd.restitution = restitution;
    body->CreateFixture(&fd);

    free(verts);
    return wrapBody(world, body, PHYS_BODY_STATIC2D);
}

extern "C" void DestroyPhysicsBody2D(PhysicsBody2D *body) {
    if (!body) return;
    if (body->body) {
        b2World *w = body->body->GetWorld();
        if (w) w->DestroyBody(body->body);
    }
    delete body;
}

/* ================================================================== */
/*  Transform / velocity                                              */
/* ================================================================== */

extern "C" void SetPhysicsBody2DPosition(PhysicsBody2D *body, Vector2 position) {
    if (!body || !body->body) return;
    body->body->SetTransform(toB2(position), body->body->GetAngle());
}

extern "C" Vector2 GetPhysicsBody2DPosition(const PhysicsBody2D *body) {
    if (!body || !body->body) return Vector2{0, 0};
    return fromB2(body->body->GetPosition());
}

extern "C" void SetPhysicsBody2DRotation(PhysicsBody2D *body, float radians) {
    if (!body || !body->body) return;
    body->body->SetTransform(body->body->GetPosition(), radians);
}

extern "C" float GetPhysicsBody2DRotation(const PhysicsBody2D *body) {
    if (!body || !body->body) return 0.0f;
    return body->body->GetAngle();
}

extern "C" void SetPhysicsBody2DVelocity(PhysicsBody2D *body, Vector2 v) {
    if (!body || !body->body) return;
    body->body->SetLinearVelocity(toB2(v));
    body->body->SetAwake(true);
}

extern "C" Vector2 GetPhysicsBody2DVelocity(const PhysicsBody2D *body) {
    if (!body || !body->body) return Vector2{0, 0};
    return fromB2(body->body->GetLinearVelocity());
}

extern "C" void SetPhysicsBody2DAngularVelocity(PhysicsBody2D *body, float omega) {
    if (!body || !body->body) return;
    body->body->SetAngularVelocity(omega);
    body->body->SetAwake(true);
}

extern "C" float GetPhysicsBody2DAngularVelocity(const PhysicsBody2D *body) {
    if (!body || !body->body) return 0.0f;
    return body->body->GetAngularVelocity();
}

extern "C" Vector2 GetPhysicsBody2DVelocityAtPoint(const PhysicsBody2D *body,
                                                   Vector2 worldPoint)
{
    if (!body || !body->body) return Vector2{0, 0};
    return fromB2(body->body->GetLinearVelocityFromWorldPoint(toB2(worldPoint)));
}

/* ================================================================== */
/*  Forces / impulses                                                 */
/* ================================================================== */

extern "C" void ApplyPhysicsBody2DForce(PhysicsBody2D *body, Vector2 force) {
    if (!body || !body->body) return;
    body->body->ApplyForceToCenter(toB2(force), true);
}

extern "C" void ApplyPhysicsBody2DForceAtPoint(PhysicsBody2D *body,
                                               Vector2 force, Vector2 worldPoint)
{
    if (!body || !body->body) return;
    body->body->ApplyForce(toB2(force), toB2(worldPoint), true);
}

extern "C" void ApplyPhysicsBody2DImpulse(PhysicsBody2D *body, Vector2 impulse) {
    if (!body || !body->body) return;
    body->body->ApplyLinearImpulseToCenter(toB2(impulse), true);
}

extern "C" void ApplyPhysicsBody2DImpulseAtPoint(PhysicsBody2D *body,
                                                 Vector2 impulse, Vector2 worldPoint)
{
    if (!body || !body->body) return;
    body->body->ApplyLinearImpulse(toB2(impulse), toB2(worldPoint), true);
}

extern "C" void ApplyPhysicsBody2DTorque(PhysicsBody2D *body, float torque) {
    if (!body || !body->body) return;
    body->body->ApplyTorque(torque, true);
}

extern "C" void ApplyPhysicsBody2DAngularImpulse(PhysicsBody2D *body,
                                                 float angularImpulse)
{
    if (!body || !body->body) return;
    body->body->ApplyAngularImpulse(angularImpulse, true);
}

extern "C" void ClearPhysicsBody2DForces(PhysicsBody2D *body) {
    if (!body || !body->body) return;
    body->body->SetLinearVelocity(b2Vec2(0, 0));
    body->body->SetAngularVelocity(0.0f);
    /* SetForce / SetTorque в Box2D 2.4.1 отсутствуют.
       Накопленные силы сбрасываются в Step() автоматически. */
}

/* ================================================================== */
/*  Body properties                                                   */
/* ================================================================== */

extern "C" void SetPhysicsBody2DType(PhysicsBody2D *body, PhysicsBodyType2D type) {
    if (!body || !body->body) return;
    body->body->SetType(toB2Type(type));
    body->type = type;
}

extern "C" PhysicsBodyType2D GetPhysicsBody2DType(const PhysicsBody2D *body) {
    if (!body || !body->body) return PHYS_BODY_STATIC2D;
    return body->type;
}

extern "C" void SetPhysicsBody2DMass(PhysicsBody2D *body, float mass) {
    if (!body || !body->body || mass <= 0.0f) return;
    b2MassData md;
    body->body->GetMassData(&md);
    md.mass = mass;
    body->body->SetMassData(&md);
}

extern "C" float GetPhysicsBody2DMass(const PhysicsBody2D *body) {
    if (!body || !body->body) return 0.0f;
    return body->body->GetMass();
}

extern "C" float GetPhysicsBody2DInertia(const PhysicsBody2D *body) {
    if (!body || !body->body) return 0.0f;
    return body->body->GetInertia();
}

extern "C" void SetPhysicsBody2DLinearDamping(PhysicsBody2D *body, float d) {
    if (!body || !body->body) return;
    body->body->SetLinearDamping(d);
}

extern "C" float GetPhysicsBody2DLinearDamping(const PhysicsBody2D *body) {
    if (!body || !body->body) return 0.0f;
    return body->body->GetLinearDamping();
}

extern "C" void SetPhysicsBody2DAngularDamping(PhysicsBody2D *body, float d) {
    if (!body || !body->body) return;
    body->body->SetAngularDamping(d);
}

extern "C" float GetPhysicsBody2DAngularDamping(const PhysicsBody2D *body) {
    if (!body || !body->body) return 0.0f;
    return body->body->GetAngularDamping();
}

extern "C" void SetPhysicsBody2DGravityScale(PhysicsBody2D *body, float scale) {
    if (!body || !body->body) return;
    body->body->SetGravityScale(scale);
}

extern "C" float GetPhysicsBody2DGravityScale(const PhysicsBody2D *body) {
    if (!body || !body->body) return 0.0f;
    return body->body->GetGravityScale();
}

extern "C" void SetPhysicsBody2DFixedRotation(PhysicsBody2D *body, bool fixed) {
    if (!body || !body->body) return;
    body->body->SetFixedRotation(fixed);
}

extern "C" void SetPhysicsBody2DBullet(PhysicsBody2D *body, bool bullet) {
    if (!body || !body->body) return;
    body->body->SetBullet(bullet);
}

extern "C" void SetPhysicsBody2DEnabled(PhysicsBody2D *body, bool enabled) {
    if (!body || !body->body) return;
    body->body->SetEnabled(enabled);
}

extern "C" bool IsPhysicsBody2DEnabled(const PhysicsBody2D *body) {
    if (!body || !body->body) return false;
    return body->body->IsEnabled();
}

extern "C" void SetPhysicsBody2DSleepingAllowed(PhysicsBody2D *body, bool allowed) {
    if (!body || !body->body) return;
    body->body->SetSleepingAllowed(allowed);
}

extern "C" void SetPhysicsBody2DAwake(PhysicsBody2D *body, bool awake) {
    if (!body || !body->body) return;
    body->body->SetAwake(awake);
}

extern "C" bool IsPhysicsBody2DAwake(const PhysicsBody2D *body) {
    if (!body || !body->body) return false;
    return body->body->IsAwake();
}

/* --- fixture level --- */

static void foreachFixture(PhysicsBody2D *body, void (*fn)(b2Fixture *, void *), void *ctx) {
    if (!body || !body->body) return;
    for (b2Fixture *f = body->body->GetFixtureList(); f; f = f->GetNext()) fn(f, ctx);
}

static void applyFriction(b2Fixture *f, void *ctx) { f->SetFriction(*(float *)ctx); }
static void applyRestitution(b2Fixture *f, void *ctx) { f->SetRestitution(*(float *)ctx); }
static void applyDensity(b2Fixture *f, void *ctx) { f->SetDensity(*(float *)ctx); }
static void applySensor(b2Fixture *f, void *ctx) { f->SetSensor(*(bool *)ctx); }

extern "C" void SetPhysicsBody2DFriction(PhysicsBody2D *body, float friction) {
    foreachFixture(body, applyFriction, &friction);
}

extern "C" float GetPhysicsBody2DFriction(const PhysicsBody2D *body) {
    if (!body || !body->body) return 0.0f;
    b2Fixture *f = body->body->GetFixtureList();
    return f ? f->GetFriction() : 0.0f;
}

extern "C" void SetPhysicsBody2DRestitution(PhysicsBody2D *body, float restitution) {
    foreachFixture(body, applyRestitution, &restitution);
}

extern "C" float GetPhysicsBody2DRestitution(const PhysicsBody2D *body) {
    if (!body || !body->body) return 0.0f;
    b2Fixture *f = body->body->GetFixtureList();
    return f ? f->GetRestitution() : 0.0f;
}

extern "C" void SetPhysicsBody2DDensity(PhysicsBody2D *body, float density) {
    foreachFixture(body, applyDensity, &density);
    if (body && body->body) body->body->ResetMassData();
}

extern "C" void SetPhysicsBody2DSensor(PhysicsBody2D *body, bool sensor) {
    foreachFixture(body, applySensor, &sensor);
}

extern "C" bool IsPhysicsBody2DSensor(const PhysicsBody2D *body) {
    if (!body || !body->body) return false;
    b2Fixture *f = body->body->GetFixtureList();
    return f ? f->IsSensor() : false;
}

static void applyFilter(b2Fixture *f, void *ctx) {
    unsigned short *params = (unsigned short *)ctx;
    b2Filter filter = f->GetFilterData();
    filter.categoryBits = params[0];
    filter.maskBits     = params[1];
    filter.groupIndex   = (short)params[2];
    f->SetFilterData(filter);
}

extern "C" void SetPhysicsBody2DFilter(PhysicsBody2D *body,
                                       unsigned short categoryBits,
                                       unsigned short maskBits,
                                       int groupIndex)
{
    unsigned short params[3] = { categoryBits, maskBits, (unsigned short)groupIndex };
    foreachFixture(body, applyFilter, params);
}

extern "C" void SetPhysicsBody2DUserData(PhysicsBody2D *body, void *userData) {
    if (body) body->userData = userData;
}

extern "C" void *GetPhysicsBody2DUserData(const PhysicsBody2D *body) {
    return body ? body->userData : NULL;
}

extern "C" PhysicsAABB2D GetPhysicsBody2DAABB(const PhysicsBody2D *body) {
    PhysicsAABB2D out = {{0,0},{0,0}};
    if (!body || !body->body) return out;
    b2AABB aabb;
    b2Fixture *f = body->body->GetFixtureList();
    if (!f) return out;
    f->GetShape()->ComputeAABB(&aabb, body->body->GetTransform(), 0);
    out.lowerBound = fromB2(aabb.lowerBound);
    out.upperBound = fromB2(aabb.upperBound);
    return out;
}

/* ================================================================== */
/*  Joints                                                            */
/* ================================================================== */

extern "C" PhysicsJoint2D *CreatePhysicsJoint2D(PhysicsWorld2D *world,
                                                JointType2D type,
                                                PhysicsBody2D *bodyA,
                                                PhysicsBody2D *bodyB,
                                                Vector2 anchorA,
                                                Vector2 anchorB,
                                                bool collideConnected)
{
    if (!world || !bodyA || !bodyA->body) return NULL;

    b2Body *A = bodyA->body;
    b2Body *B = (bodyB && bodyB->body) ? bodyB->body : world->world->CreateBody(new b2BodyDef());
    /* Если B NULL — используем "ground" тело для anchorA в мире. */

    b2Joint *j = NULL;
    bool createdGroundB = (bodyB == NULL || bodyB->body == NULL);

    switch (type) {
        case JOINT2D_REVOLUTE: {
            b2RevoluteJointDef d;
            d.Initialize(A, B, toB2(anchorA));
            d.collideConnected = collideConnected;
            j = world->world->CreateJoint(&d);
            break;
        }
        case JOINT2D_PRISMATIC: {
            b2PrismaticJointDef d;
            d.Initialize(A, B, toB2(anchorA), b2Vec2(1, 0));
            d.collideConnected = collideConnected;
            j = world->world->CreateJoint(&d);
            break;
        }
        case JOINT2D_DISTANCE: {
            b2DistanceJointDef d;
            d.Initialize(A, B, toB2(anchorA), toB2(anchorB));
            d.collideConnected = collideConnected;
            j = world->world->CreateJoint(&d);
            break;
        }
        case JOINT2D_PULLEY: {
            b2PulleyJointDef d;
            d.Initialize(A, B,
                         toB2(anchorA), toB2(anchorB),
                         A->GetWorldPoint(toB2(anchorA)),
                         B->GetWorldPoint(toB2(anchorB)),
                         1.0f);
            d.collideConnected = collideConnected;
            j = world->world->CreateJoint(&d);
            break;
        }
        case JOINT2D_GEAR: {
            b2GearJointDef d;
            d.bodyA = A;
            d.bodyB = B;
            d.joint1 = NULL;
            d.joint2 = NULL;
            d.ratio  = 1.0f;
            d.collideConnected = collideConnected;
            j = world->world->CreateJoint(&d);
            break;
        }
        case JOINT2D_WHEEL: {
            b2WheelJointDef d;
            d.Initialize(A, B, toB2(anchorA), b2Vec2(0, 1));
            d.collideConnected = collideConnected;
            j = world->world->CreateJoint(&d);
            break;
        }
        case JOINT2D_WELD: {
            b2WeldJointDef d;
            d.Initialize(A, B, toB2(anchorA));
            d.collideConnected = collideConnected;
            j = world->world->CreateJoint(&d);
            break;
        }
        case JOINT2D_FRICTION: {
            b2FrictionJointDef d;
            d.Initialize(A, B, toB2(anchorA));
            d.collideConnected = collideConnected;
            j = world->world->CreateJoint(&d);
            break;
        }
        case JOINT2D_MOTOR: {
            b2MotorJointDef d;
            d.Initialize(A, B);
            d.collideConnected = collideConnected;
            j = world->world->CreateJoint(&d);
            break;
        }
        case JOINT2D_MOUSE: {
            b2MouseJointDef d;
            d.bodyA = A;
            d.bodyB = B;
            d.target = toB2(anchorA);
            d.maxForce = 1000.0f * B->GetMass();
            d.collideConnected = collideConnected;
            j = world->world->CreateJoint(&d);
            break;
        }
    }

    if (!j) {
        if (createdGroundB) world->world->DestroyBody(B);
        return NULL;
    }

    PhysicsJoint2D *out = new PhysicsJoint2D();
    out->world = world;
    out->joint = j;
    out->type  = type;
    return out;
}

extern "C" void DestroyPhysicsJoint2D(PhysicsJoint2D *joint) {
    if (!joint) return;
    if (joint->world && joint->joint) joint->world->world->DestroyJoint(joint->joint);
    delete joint;
}

extern "C" void SetJoint2DLimits(PhysicsJoint2D *j, float lower, float upper) {
    if (!j || !j->joint) return;
    switch (j->type) {
        case JOINT2D_REVOLUTE: {
            b2RevoluteJoint *r = (b2RevoluteJoint *)j->joint;
            r->SetLimits(lower, upper);
            break;
        }
        case JOINT2D_PRISMATIC: {
            b2PrismaticJoint *p = (b2PrismaticJoint *)j->joint;
            p->SetLimits(lower, upper);
            break;
        }
        case JOINT2D_WHEEL: {
            b2WheelJoint *w = (b2WheelJoint *)j->joint;
            w->SetLimits(lower, upper);
            break;
        }
        default: break;
    }
}

extern "C" void EnableJoint2DLimit(PhysicsJoint2D *j, bool enable) {
    if (!j || !j->joint) return;
    switch (j->type) {
        case JOINT2D_REVOLUTE:  ((b2RevoluteJoint *)j->joint)->EnableLimit(enable); break;
        case JOINT2D_PRISMATIC: ((b2PrismaticJoint *)j->joint)->EnableLimit(enable); break;
        case JOINT2D_WHEEL:     ((b2WheelJoint *)j->joint)->EnableLimit(enable); break;
        default: break;
    }
}

extern "C" void EnableJoint2DMotor(PhysicsJoint2D *j, bool enable) {
    if (!j || !j->joint) return;
    switch (j->type) {
        case JOINT2D_REVOLUTE:  ((b2RevoluteJoint *)j->joint)->EnableMotor(enable); break;
        case JOINT2D_PRISMATIC: ((b2PrismaticJoint *)j->joint)->EnableMotor(enable); break;
        case JOINT2D_WHEEL:     ((b2WheelJoint *)j->joint)->EnableMotor(enable); break;
        default: break;
    }
}

extern "C" void SetJoint2DMotorSpeed(PhysicsJoint2D *j, float speed) {
    if (!j || !j->joint) return;
    switch (j->type) {
        case JOINT2D_REVOLUTE:  ((b2RevoluteJoint *)j->joint)->SetMotorSpeed(speed); break;
        case JOINT2D_PRISMATIC: ((b2PrismaticJoint *)j->joint)->SetMotorSpeed(speed); break;
        case JOINT2D_WHEEL:     ((b2WheelJoint *)j->joint)->SetMotorSpeed(speed); break;
        default: break;
    }
}

extern "C" void SetJoint2DMotorMaxTorque(PhysicsJoint2D *j, float torque) {
    if (!j || !j->joint) return;
    switch (j->type) {
        case JOINT2D_REVOLUTE:  ((b2RevoluteJoint *)j->joint)->SetMaxMotorTorque(torque); break;
        case JOINT2D_WHEEL:     ((b2WheelJoint *)j->joint)->SetMaxMotorTorque(torque); break;
        default: break;
    }
}

extern "C" void SetJoint2DMotorMaxForce(PhysicsJoint2D *j, float force) {
    if (!j || !j->joint) return;
    switch (j->type) {
        case JOINT2D_PRISMATIC: ((b2PrismaticJoint *)j->joint)->SetMaxMotorForce(force); break;
        case JOINT2D_WHEEL:     ((b2WheelJoint *)j->joint)->SetMaxMotorTorque(force); break;
        default: break;
    }
}

extern "C" float GetJoint2DAngle(const PhysicsJoint2D *j) {
    if (!j || !j->joint) return 0.0f;
    if (j->type == JOINT2D_REVOLUTE)
        return ((b2RevoluteJoint *)j->joint)->GetJointAngle();
    return 0.0f;
}

extern "C" float GetJoint2DSpeed(const PhysicsJoint2D *j) {
    if (!j || !j->joint) return 0.0f;
    if (j->type == JOINT2D_REVOLUTE)
        return ((b2RevoluteJoint *)j->joint)->GetJointSpeed();
    if (j->type == JOINT2D_WHEEL)
        return ((b2WheelJoint *)j->joint)->GetJointLinearSpeed();
    return 0.0f;
}

extern "C" float GetJoint2DLength(const PhysicsJoint2D *j) {
    if (!j || !j->joint) return 0.0f;
    if (j->type == JOINT2D_DISTANCE)
        return ((b2DistanceJoint *)j->joint)->GetLength();
    return 0.0f;
}

extern "C" Vector2 GetJoint2DReactionForce(const PhysicsJoint2D *j, float inv_dt) {
    if (!j || !j->joint) return Vector2{0, 0};
    return fromB2(j->joint->GetReactionForce(inv_dt));
}

extern "C" void SetJoint2DPulleyRatio(PhysicsJoint2D *j, float ratio) {
    if (!j || !j->joint) return;
    /* В Box2D 2.4.1 нет SetRatio() — ratio задаётся при создании. */
    (void)j; (void)ratio;
}

extern "C" void SetJoint2DGearRatio(PhysicsJoint2D *j, float ratio) {
    if (!j || !j->joint) return;
    if (j->type == JOINT2D_GEAR)
        ((b2GearJoint *)j->joint)->SetRatio(ratio);
}

extern "C" void SetJoint2DWeldStiffness(PhysicsJoint2D *j, float stiffness) {
    if (!j || !j->joint) return;
    if (j->type == JOINT2D_WELD)
        ((b2WeldJoint *)j->joint)->SetStiffness(stiffness);
}

extern "C" void SetJoint2DMouseTarget(PhysicsJoint2D *j, Vector2 target) {
    if (!j || !j->joint) return;
    if (j->type == JOINT2D_MOUSE)
        ((b2MouseJoint *)j->joint)->SetTarget(toB2(target));
}

extern "C" void SetJoint2DMouseMaxForce(PhysicsJoint2D *j, float maxForce) {
    if (!j || !j->joint) return;
    if (j->type == JOINT2D_MOUSE)
        ((b2MouseJoint *)j->joint)->SetMaxForce(maxForce);
}

extern "C" void SetJoint2DMouseFrequency(PhysicsJoint2D *j, float hz, float dampingRatio) {
    if (!j || !j->joint) return;
    if (j->type == JOINT2D_MOUSE)
        ((b2MouseJoint *)j->joint)->SetStiffness(hz);
    (void)dampingRatio;
}

/* ================================================================== */
/*  Contacts                                                          */
/* ================================================================== */

extern "C" int GetPhysics2DContactCount(const PhysicsWorld2D *world) {
    if (!world) return 0;
    int total = 0;
    for (b2Contact *c = world->world->GetContactList(); c; c = c->GetNext())
        if (c->IsTouching()) total++;
    return total;
}

extern "C" bool GetPhysics2DContact(const PhysicsWorld2D *world, int index,
                                    PhysicsContact2D *out)
{
    if (!world || !out) return false;
    int k = 0;
    for (b2Contact *c = world->world->GetContactList(); c; c = c->GetNext()) {
        if (!c->IsTouching()) continue;
        if (k != index) { k++; continue; }

        b2WorldManifold wm;
        c->GetWorldManifold(&wm);

        out->bodyA = findBodyWrap(c->GetFixtureA()->GetBody());
        out->bodyB = findBodyWrap(c->GetFixtureB()->GetBody());
        out->point = fromB2(wm.points[0]);
        out->normal = fromB2(wm.normal);
        out->separation = wm.separations[0];
        out->normalImpulse = 0.0f;
        out->tangentImpulse = 0.0f;
        out->isTouching = c->IsTouching() ? 1 : 0;
        out->isSensor = c->IsEnabled() ? 0 : 1;
        return true;
    }
    return false;
}

extern "C" bool CheckCollisionPhysics2D(PhysicsBody2D *a, PhysicsBody2D *b,
                                        PhysicsContact2D *out)
{
    if (!a || !b || !a->body || !b->body) return false;

    b2AABB a1, a2;
    b2Fixture *f1 = a->body->GetFixtureList();
    b2Fixture *f2 = b->body->GetFixtureList();
    if (!f1 || !f2) return false;
    f1->GetShape()->ComputeAABB(&a1, a->body->GetTransform(), 0);
    f2->GetShape()->ComputeAABB(&a2, b->body->GetTransform(), 0);

    bool hit = b2TestOverlap(a1, a2);
    if (hit && out) {
        out->bodyA = a;
        out->bodyB = b;
        out->isTouching = 1;
        out->separation = 0.0f;
    }
    return hit;
}

/* ================================================================== */
/*  Raycast                                                           */
/* ================================================================== */

struct RayCastCB : public b2RayCastCallback {
    PhysicsRayHit2D *best;
    float bestFrac;
    RayCastCB() : best(NULL), bestFrac(1.0f) {}

    float ReportFixture(b2Fixture *fixture,
                        const b2Vec2 &point,
                        const b2Vec2 &normal,
                        float fraction) override
    {
        if (fraction < bestFrac && best) {
            bestFrac = fraction;
            best->point = Vector2{ point.x, point.y };
            best->normal = Vector2{ normal.x, normal.y };
            best->fraction = fraction;
            best->body = findBodyWrap(fixture->GetBody());
        }
        return fraction;
    }
};

extern "C" PhysicsRayHit2D RaycastPhysics2D(PhysicsWorld2D *world,
                                            Vector2 origin, Vector2 direction,
                                            float maxDist)
{
    PhysicsRayHit2D out = {0};
    if (!world) return out;

    Vector2 end = Vector2Add(origin, Vector2Scale(Vector2Normalize(direction), maxDist));

    RayCastCB cb;
    cb.best = &out;
    world->world->RayCast(&cb, toB2(origin), toB2(end));

    if (out.body) {
        out.hit = 1;
        out.distance = out.fraction * maxDist;
    }
    return out;
}

struct RayCastAllCB : public b2RayCastCallback {
    PhysicsRayHit2D *out;
    int maxCount;
    int count;
    float maxDist;
    Vector2 origin;

    RayCastAllCB(PhysicsRayHit2D *o, int m, float md, Vector2 org)
        : out(o), maxCount(m), count(0), maxDist(md), origin(org) {}

    float ReportFixture(b2Fixture *fixture,
                        const b2Vec2 &point,
                        const b2Vec2 &normal,
                        float fraction) override
    {
        if (count >= maxCount) return 0.0f;
        out[count].hit = 1;
        out[count].point = Vector2{ point.x, point.y };
        out[count].normal = Vector2{ normal.x, normal.y };
        out[count].fraction = fraction;
        out[count].distance = fraction * maxDist;
        out[count].body = findBodyWrap(fixture->GetBody());
        count++;
        return -1.0f;   /* продолжить обход */
    }
};

extern "C" int RaycastPhysics2DAll(PhysicsWorld2D *world,
                                   Vector2 origin, Vector2 direction, float maxDist,
                                   PhysicsRayHit2D *out, int maxCount)
{
    if (!world || !out || maxCount <= 0) return 0;
    Vector2 end = Vector2Add(origin, Vector2Scale(Vector2Normalize(direction), maxDist));
    RayCastAllCB cb(out, maxCount, maxDist, origin);
    world->world->RayCast(&cb, toB2(origin), toB2(end));
    return cb.count;
}

/* ================================================================== */
/*  Overlap queries                                                   */
/* ================================================================== */

struct AABBQueryCB : public b2QueryCallback {
    PhysicsBody2D **out;
    int maxCount;
    int count;

    AABBQueryCB(PhysicsBody2D **o, int m) : out(o), maxCount(m), count(0) {}

    bool ReportFixture(b2Fixture *fixture) override {
        if (count >= maxCount) return false;
        PhysicsBody2D *wrap = findBodyWrap(fixture->GetBody());
        if (wrap) {
            /* избежать дубликатов */
            for (int i = 0; i < count; ++i)
                if (out[i] == wrap) return true;
            out[count++] = wrap;
        }
        return true;
    }
};

extern "C" int QueryPhysics2DOverlapAABB(PhysicsWorld2D *world,
                                         PhysicsAABB2D aabb,
                                         PhysicsBody2D **out, int maxCount)
{
    if (!world || !out || maxCount <= 0) return 0;

    b2AABB b;
    b.lowerBound = toB2(aabb.lowerBound);
    b.upperBound = toB2(aabb.upperBound);

    AABBQueryCB cb(out, maxCount);
    world->world->QueryAABB(&cb, b);
    return cb.count;
}

extern "C" int QueryPhysics2DOverlapCircle(PhysicsWorld2D *world,
                                           Vector2 center, float radius,
                                           PhysicsBody2D **out, int maxCount)
{
    PhysicsAABB2D aabb;
    aabb.lowerBound = Vector2{ center.x - radius, center.y - radius };
    aabb.upperBound = Vector2{ center.x + radius, center.y + radius };
    return QueryPhysics2DOverlapAABB(world, aabb, out, maxCount);
}

extern "C" int QueryPhysics2DPoint(PhysicsWorld2D *world,
                                   Vector2 point,
                                   PhysicsBody2D **out, int maxCount)
{
    return QueryPhysics2DOverlapCircle(world, point, 0.01f, out, maxCount);
}

/* ================================================================== */
/*  Character                                                         */
/* ================================================================== */

extern "C" Character2D *CreateCharacter2D(PhysicsWorld2D *world,
                                          Vector2 position,
                                          float radius,
                                          float stepHeight,
                                          float slopeMaxRadians)
{
    if (!world) return NULL;

    Character2D *c = new Character2D();
    c->world      = world;
    c->radius     = radius;
    c->stepHeight = stepHeight;
    c->slopeMax   = slopeMaxRadians;
    c->grounded   = false;

    /* Простая реализация: dynamic box с фиксированным вращением. */
    Vector2 halfSize = { radius, radius };
    c->body = CreatePhysicsBody2DBox(world, position, halfSize,
                                     PHYS_BODY_DYNAMIC2D,
                                     1.0f, 0.5f, 0.0f);
    SetPhysicsBody2DFixedRotation(c->body, true);
    SetPhysicsBody2DLinearDamping(c->body, 0.0f);

    return c;
}

extern "C" void DestroyCharacter2D(Character2D *c) {
    if (!c) return;
    DestroyPhysicsBody2D(c->body);
    delete c;
}

extern "C" void SetCharacter2DVelocity(Character2D *c, Vector2 v) {
    if (!c || !c->body) return;
    SetPhysicsBody2DVelocity(c->body, v);
}

extern "C" Vector2 GetCharacter2DVelocity(const Character2D *c) {
    if (!c || !c->body) return Vector2{0, 0};
    return GetPhysicsBody2DVelocity(c->body);
}

extern "C" void JumpCharacter2D(Character2D *c, float height) {
    if (!c || !c->body) return;
    /* v = sqrt(2 * g * h) */
    Vector2 g = GetPhysicsWorld2DGravity(c->world);
    float gy = fabsf(g.y);
    if (gy < 0.01f) gy = 9.81f;
    float v = sqrtf(2.0f * gy * height);
    Vector2 curr = GetPhysicsBody2DVelocity(c->body);
    SetPhysicsBody2DVelocity(c->body, Vector2{ curr.x, v });
}

extern "C" Vector2 GetCharacter2DPosition(const Character2D *c) {
    if (!c || !c->body) return Vector2{0, 0};
    return GetPhysicsBody2DPosition(c->body);
}

extern "C" void WarpCharacter2D(Character2D *c, Vector2 position) {
    if (!c || !c->body) return;
    SetPhysicsBody2DPosition(c->body, position);
    SetPhysicsBody2DVelocity(c->body, Vector2{0, 0});
}

extern "C" bool IsCharacter2DGrounded(const Character2D *c) {
    if (!c || !c->body) return false;
    /* Грубая проверка: рейкаст вниз */
    Vector2 pos = GetPhysicsBody2DPosition(c->body);
    Vector2 origin = { pos.x, pos.y };
    Vector2 dir = { 0, -1 };
    PhysicsRayHit2D hit = RaycastPhysics2D(c->world, origin, dir,
                                           c->radius + 0.1f);
    return hit.hit != 0;
}

/* ================================================================== */
/*  Debug draw                                                        */
/* ================================================================== */

extern "C" void VPhysics2DDrawBodies(PhysicsWorld2D *world, bool drawAABB) {
    if (!world) return;

    for (b2Body *b = world->world->GetBodyList(); b; b = b->GetNext()) {
        Color col;
        if (b->GetType() == b2_staticBody) {
            col.r = 130; col.g = 130; col.b = 130; col.a = 255;
        } else {
            col.r = 102; col.g = 191; col.b = 255; col.a = 255;
        }

        for (b2Fixture *f = b->GetFixtureList(); f; f = f->GetNext()) {
            b2Shape::Type t = f->GetType();
            if (t == b2Shape::e_circle) {
                b2CircleShape *cs = (b2CircleShape *)f->GetShape();
                Vector2 c = fromB2(b->GetWorldPoint(cs->m_p));
                DrawCircleV(c, cs->m_radius, col);
            } else if (t == b2Shape::e_polygon) {
                b2PolygonShape *ps = (b2PolygonShape *)f->GetShape();
                Vector2 pts[8];
                int n = ps->m_count;
                if (n > 8) n = 8;
                for (int i = 0; i < n; ++i)
                    pts[i] = fromB2(b->GetWorldPoint(ps->m_vertices[i]));
                for (int i = 0; i < n; ++i) {
                    int jj = (i + 1) % n;
                    DrawLineEx(pts[i], pts[jj], 1.5f, col);
                }
            } else if (t == b2Shape::e_edge) {
                b2EdgeShape *es = (b2EdgeShape *)f->GetShape();
                Vector2 a = fromB2(b->GetWorldPoint(es->m_vertex1));
                Vector2 c = fromB2(b->GetWorldPoint(es->m_vertex2));
                DrawLineEx(a, c, 1.5f, col);
            }
        }

        if (drawAABB) {
            PhysicsBody2D *wrap = (PhysicsBody2D *)b->GetUserData().pointer;
            if (wrap) {
                PhysicsAABB2D bb = GetPhysicsBody2DAABB(wrap);
                Rectangle r;
                r.x = bb.lowerBound.x;
                r.y = bb.lowerBound.y;
                r.width  = bb.upperBound.x - bb.lowerBound.x;
                r.height = bb.upperBound.y - bb.lowerBound.y;

                Color aabbColor;
                aabbColor.r = 255; aabbColor.g = 203;
                aabbColor.b = 0;   aabbColor.a = 100;

                DrawRectangleLines((int)r.x, (int)r.y,
                                   (int)r.width, (int)r.height,
                                   aabbColor);
            }
        }
    }
}
